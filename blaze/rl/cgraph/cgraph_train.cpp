#include <ATen/Context.h>
#include <ATen/autocast_mode.h>
#include <ATen/cuda/CUDAGraph.h>
#include <c10/cuda/CUDACachingAllocator.h>
#include <c10/cuda/CUDAGuard.h>
#include <c10/cuda/CUDAStream.h>
#include <cuda_profiler_api.h>
#include <nvtx3/nvToolsExt.h>
#include <torch/cuda.h>
#include <torch/nn/utils/clip_grad.h>
#include <torch/torch.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "cgraph_kernels.h"
#include "native_env.h"
#include "native_reward.h"
#include "ppo_model.h"

namespace fs = std::filesystem;
using netherite::ppo::ChainPolicy;
using netherite::ppo::NativeEnv;
using netherite::ppo::NativeReward;
using netherite::ppo::RewardSpec;
using torch::indexing::Slice;
using namespace netherite::ppo;

namespace {

constexpr int kRepeat = 4;
constexpr int kEpisodeDecisions = 1500;
constexpr int kStages = 5;
constexpr int kInventory = 9;
constexpr float kGamma = 0.995F;
constexpr float kLambda = 0.95F;
constexpr float kClip = 0.2F;
constexpr float kEntropy = 0.01F;
constexpr float kGradClip = 0.5F;
constexpr int kSelectedItems[kInventory] = {17,  5,   280, 4, 58,
                                            270, 274, 263, 50};

int env_int(const char *name, int fallback) {
  if (const char *value = std::getenv(name))
    return std::stoi(value);
  return fallback;
}

int64_t env_i64(const char *name, int64_t fallback) {
  if (const char *value = std::getenv(name))
    return static_cast<int64_t>(std::stod(value));
  return fallback;
}

double env_double(const char *name, double fallback) {
  if (const char *value = std::getenv(name))
    return std::stod(value);
  return fallback;
}

bool env_bool(const char *name, bool fallback = false) {
  if (const char *value = std::getenv(name))
    return std::stoi(value) != 0;
  return fallback;
}

std::vector<int> training_seeds() {
  const char *raw = std::getenv("TRAIN_SEEDS");
  std::string text = raw == nullptr ? "2,3,10,14,16,20,27,29,32,44,46" : raw;
  std::vector<int> result;
  size_t begin = 0;
  while (begin < text.size()) {
    size_t end = text.find(',', begin);
    result.push_back(std::stoi(text.substr(begin, end - begin)));
    if (end == std::string::npos)
      break;
    begin = end + 1;
  }
  return result;
}

template <class T> T read_le(const std::vector<uint8_t> &bytes, size_t offset) {
  T value{};
  std::memcpy(&value, bytes.data() + offset, sizeof(T));
  return value;
}

std::vector<std::array<float, 3>> snapshot_logs(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read " + path.string());
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(input)), {});
  if (bytes.size() < 752 || std::memcmp(bytes.data(), "BSNP", 4) != 0) {
    throw std::runtime_error("invalid snapshot " + path.string());
  }
  uint32_t item_count = read_le<uint32_t>(bytes, 724);
  int32_t x0 = read_le<int32_t>(bytes, 728);
  int32_t y0 = read_le<int32_t>(bytes, 732);
  int32_t z0 = read_le<int32_t>(bytes, 736);
  int32_t nx = read_le<int32_t>(bytes, 740);
  int32_t ny = read_le<int32_t>(bytes, 744);
  int32_t nz = read_le<int32_t>(bytes, 748);
  size_t cells_offset = 752 + static_cast<size_t>(item_count) * 76;
  size_t cells = static_cast<size_t>(nx) * ny * nz;
  if (cells_offset + cells * sizeof(uint16_t) > bytes.size()) {
    throw std::runtime_error("truncated snapshot " + path.string());
  }
  std::vector<std::array<float, 3>> result;
  for (size_t index = 0; index < cells; ++index) {
    uint16_t state = read_le<uint16_t>(bytes, cells_offset + index * 2);
    if ((state >> 4) != 17)
      continue;
    int x = static_cast<int>(index / (static_cast<size_t>(ny) * nz));
    int yz = static_cast<int>(index % (static_cast<size_t>(ny) * nz));
    int y = yz / nz;
    int z = yz % nz;
    result.push_back({x0 + x + 0.5F, y0 + y + 0.5F, z0 + z + 0.5F});
  }
  return result;
}

torch::Tensor make_log_tensor(const std::vector<fs::path> &paths,
                              torch::Device device) {
  std::vector<std::vector<std::array<float, 3>>> logs;
  size_t maximum = 0;
  for (const auto &path : paths) {
    logs.push_back(snapshot_logs(path));
    maximum = std::max(maximum, logs.back().size());
  }
  auto host = torch::full(
      {static_cast<int64_t>(logs.size()), static_cast<int64_t>(maximum), 3},
      1.0e9F, torch::TensorOptions().dtype(torch::kFloat32));
  auto accessor = host.accessor<float, 3>();
  for (size_t seed = 0; seed < logs.size(); ++seed) {
    for (size_t item = 0; item < logs[seed].size(); ++item) {
      for (int axis = 0; axis < 3; ++axis) {
        accessor[seed][item][axis] = logs[seed][item][axis];
      }
    }
  }
  return host.to(device);
}

struct StageCurriculum {
  StageCurriculum(int seed_count, uint64_t seed, double t0_share)
      : seed_count(seed_count), t0_share(t0_share), rng(seed) {
    available.assign(seed_count, std::vector<bool>(kStages, false));
    history.resize(seed_count);
    for (int seed_i = 0; seed_i < seed_count; ++seed_i) {
      available[seed_i][0] = true;
      history[seed_i].resize(kStages);
    }
  }

  void record(int seed_i, int stage, bool success) {
    auto &values = history[seed_i][stage];
    if (values.size() == 60)
      values.pop_front();
    values.push_back(success ? 1.0 : 0.0);
  }

  double success(int seed_i, int stage) const {
    const auto &values = history[seed_i][stage];
    if (values.empty())
      return 0.0;
    return std::accumulate(values.begin(), values.end(), 0.0) / values.size();
  }

  std::pair<std::vector<int>, std::vector<int>> sample(int count) {
    std::uniform_int_distribution<int> seed_dist(0, seed_count - 1);
    std::uniform_real_distribution<double> real(0.0, 1.0);
    std::vector<int> seeds(count);
    std::vector<int> stages(count, 0);
    for (int lane = 0; lane < count; ++lane) {
      int seed_i = seed_dist(rng);
      seeds[lane] = seed_i;
      if (real(rng) < t0_share)
        continue;
      std::vector<int> choices;
      for (int stage = 0; stage < kStages; ++stage) {
        if (available[seed_i][stage])
          choices.push_back(stage);
      }
      int frontier = choices.back();
      for (int stage : choices) {
        const auto &values = history[seed_i][stage];
        if (values.size() < 15 || success(seed_i, stage) < 0.6) {
          frontier = stage;
          break;
        }
      }
      std::vector<double> weights(choices.size(), 0.15);
      auto found = std::find(choices.begin(), choices.end(), frontier);
      weights[found - choices.begin()] += 1.0 - 0.15 * choices.size();
      std::discrete_distribution<int> choose(weights.begin(), weights.end());
      stages[lane] = choices[choose(rng)];
    }
    return {std::move(seeds), std::move(stages)};
  }

  int seed_count;
  double t0_share;
  std::mt19937_64 rng;
  std::vector<std::vector<bool>> available;
  std::vector<std::vector<std::deque<double>>> history;
};

struct AutocastGuard {
  explicit AutocastGuard(bool enabled) : enabled(enabled) {
    if (enabled) {
      at::autocast::set_autocast_dtype(at::kCUDA, at::kBFloat16);
      at::autocast::set_autocast_enabled(at::kCUDA, true);
    }
  }
  ~AutocastGuard() {
    if (enabled)
      at::autocast::set_autocast_enabled(at::kCUDA, false);
  }
  bool enabled;
};

enum class Bf16Scope { kDisabled, kConv, kFull };

Bf16Scope bf16_scope(bool enabled) {
  if (!enabled)
    return Bf16Scope::kDisabled;
  const char *raw = std::getenv("NATIVE_BF16_SCOPE");
  if (raw == nullptr || std::string(raw) == "full")
    return Bf16Scope::kFull;
  if (std::string(raw) == "conv")
    return Bf16Scope::kConv;
  throw std::runtime_error("NATIVE_BF16_SCOPE must be full or conv");
}

std::pair<std::vector<torch::Tensor>, torch::Tensor>
policy_forward(ChainPolicy &model, const torch::Tensor &planes,
               const torch::Tensor &scalars, Bf16Scope scope) {
  if (scope == Bf16Scope::kFull) {
    AutocastGuard autocast(true);
    return model->forward(planes, scalars);
  }
  if (scope == Bf16Scope::kDisabled)
    return model->forward(planes, scalars);

  torch::Tensor convolution;
  {
    AutocastGuard autocast(true);
    convolution = model->conv->forward(planes);
  }
  auto hidden = model->fc->forward(
      torch::cat({convolution.to(torch::kFloat32), scalars}, 1));
  std::vector<torch::Tensor> logits;
  logits.reserve(kHeads);
  for (auto &module : *model->heads) {
    logits.push_back(module->as<torch::nn::Linear>()->forward(hidden));
  }
  return {std::move(logits), model->value->forward(hidden).squeeze(-1)};
}

struct ProfileRange {
  ProfileRange(bool enabled, const char *name) : enabled(enabled) {
    if (enabled)
      nvtxRangePushA(name);
  }
  ~ProfileRange() {
    if (enabled)
      nvtxRangePop();
  }
  bool enabled;
};

// Reusable obs conversion buffer (mirrors Python obs_float). Avoids allocating
// a multi-GB temporary on every forward; depth plane scaled /255 in-place.
// When to_bf16 is true, convert uint8->BF16 directly so autocast does not emit
// a second float32->bf16 copy kernel (measured ~112ms/chunk).
torch::Tensor &obs_convert_buffer(torch::Device device, int64_t batch,
                                  int64_t channels, int64_t height,
                                  int64_t width, torch::ScalarType dtype) {
  thread_local torch::Tensor buffer_f32;
  thread_local torch::Tensor buffer_bf16;
  torch::Tensor *buffer =
      dtype == torch::kBFloat16 ? &buffer_bf16 : &buffer_f32;
  if (!buffer->defined() || buffer->device() != device ||
      buffer->size(0) < batch || buffer->size(1) != channels ||
      buffer->size(2) != height || buffer->size(3) != width ||
      buffer->scalar_type() != dtype) {
    int64_t rows = buffer->defined() ? std::max(batch, buffer->size(0)) : batch;
    *buffer = torch::empty({rows, channels, height, width},
                           torch::TensorOptions().dtype(dtype).device(device));
  }
  return *buffer;
}

torch::Tensor obs_float(const torch::Tensor &input, bool channels_last,
                        bool to_bf16 = false) {
  auto dtype = to_bf16 ? torch::kBFloat16 : torch::kFloat32;
  auto &buffer =
      obs_convert_buffer(input.device(), input.size(0), input.size(1),
                         input.size(2), input.size(3), dtype);
  auto output = buffer.narrow(0, 0, input.size(0));
  output.copy_(input, /*non_blocking=*/true);
  for (int stack = 0; stack < kStack; ++stack) {
    output.index({Slice(), stack * kPlanes + 7}).div_(255.0);
  }
  if (channels_last) {
    return output.contiguous(torch::MemoryFormat::ChannelsLast);
  }
  return output;
}

torch::Tensor flattened_parameters(const ChainPolicy &model) {
  std::vector<torch::Tensor> values;
  values.reserve(model->parameters().size());
  for (const auto &parameter : model->parameters()) {
    values.push_back(parameter.detach().to(torch::kFloat32).flatten());
  }
  return torch::cat(values);
}

torch::Tensor build_frame(const NativeEnv &env) {
  auto cam = env.cam;
  return torch::stack({cam.eq(17), cam.eq(18), cam.eq(16),
                       cam.eq(1) | cam.eq(4), cam.eq(2) | cam.eq(3), cam.eq(58),
                       cam.ne(0), env.depth, env.edge},
                      1)
      .to(torch::kUInt8);
}

torch::Tensor build_scalars(const NativeEnv &env,
                            const torch::Tensor &episode_decisions) {
  auto inventory = env.status.index({Slice(), Slice(0, 9)})
                       .to(torch::kFloat32)
                       .clamp_max(10.0) /
                   10.0;
  auto held = env.status.index({Slice(), 10});
  std::vector<torch::Tensor> one_hot;
  for (int item : kSelectedItems) {
    one_hot.push_back(held.eq(item).to(torch::kFloat32));
  }
  auto selected = torch::stack(one_hot, 1);
  auto container =
      env.status.index({Slice(), 11}).gt(0).to(torch::kFloat32).unsqueeze(1);
  auto y = (env.pose.index({Slice(), 1}) / 64.0).unsqueeze(1);
  auto fraction =
      (episode_decisions.to(torch::kFloat32) / kEpisodeDecisions).unsqueeze(1);
  return torch::cat({env.scal, inventory, container, selected, y, fraction}, 1);
}

struct CategoricalResult {
  torch::Tensor actions;
  torch::Tensor logprob;
  torch::Tensor entropy;
};

// Padded fused Gumbel-max sampler and joint log-prob/entropy. This matches the
// accepted Python FUSED_SAMPLE path: padding is -inf, so it contributes zero
// probability and can never win argmax.
CategoricalResult categorical(const std::vector<torch::Tensor> &logits,
                              const torch::Tensor *fixed_actions = nullptr,
                              const torch::Tensor *force_actions = nullptr,
                              const torch::Tensor *force_mask = nullptr) {
  constexpr int maximum = 10;
  auto padded = torch::full({logits.front().size(0), kHeads, maximum},
                            -std::numeric_limits<float>::infinity(),
                            logits.front().options());
  for (int head = 0; head < kHeads; ++head) {
    padded.index_put_({Slice(), head, Slice(0, kHeadSizes[head])},
                      logits[head]);
  }
  auto log_probs = torch::log_softmax(padded, -1);
  torch::Tensor actions;
  if (fixed_actions != nullptr) {
    actions = *fixed_actions;
  } else {
    auto uniform = torch::rand_like(padded);
    auto exponential =
        (-torch::log(uniform.clamp_min(1.0e-20))).clamp_min(1.0e-20);
    auto gumbel = -torch::log(exponential);
    actions = (padded + gumbel).argmax(-1);
    if (force_actions != nullptr && force_mask != nullptr) {
      actions = torch::where(force_mask->unsqueeze(1),
                             force_actions->unsqueeze(0), actions);
    }
  }
  auto logprob = log_probs.gather(-1, actions.unsqueeze(-1)).squeeze(-1).sum(1);
  auto probabilities = log_probs.exp();
  auto finite_log_probs = torch::where(torch::isfinite(log_probs), log_probs,
                                       torch::zeros_like(log_probs));
  auto terms = probabilities * finite_log_probs;
  auto entropy = -terms.sum({1, 2});
  return {actions, logprob, entropy};
}

struct AdamSnapshot {
  std::vector<torch::Tensor> parameters;
  std::vector<torch::Tensor> first;
  std::vector<torch::Tensor> second;
  torch::Tensor step;
};

class GraphAdam {
public:
  GraphAdam(const ChainPolicy &model, torch::Device device, float learning_rate)
      : parameters_(model->parameters()),
        learning_rate_(torch::full(
            {1}, learning_rate,
            torch::TensorOptions().dtype(torch::kFloat32).device(device))),
        step_(torch::zeros({1}, learning_rate_.options())),
        step_size_(torch::zeros({1}, learning_rate_.options())),
        denom_scale_(torch::zeros({1}, learning_rate_.options())),
        grad_scale_(torch::ones({1}, learning_rate_.options())) {
    std::size_t total_blocks = 0;
    for (const auto &parameter : parameters_) {
      if (parameter.scalar_type() != torch::kFloat32 ||
          !parameter.is_contiguous()) {
        throw std::runtime_error(
            "GraphAdam requires contiguous FP32 parameters");
      }
      first_.push_back(torch::zeros_like(parameter));
      second_.push_back(torch::zeros_like(parameter));
      partial_offsets_.push_back(total_blocks);
      int blocks = cgraph_reduction_blocks(parameter.numel());
      partial_counts_.push_back(blocks);
      total_blocks += blocks;
    }
    partials_ = torch::zeros({static_cast<int64_t>(total_blocks)},
                             learning_rate_.options());
  }

  void set_learning_rate(float value) { learning_rate_.fill_(value); }

  void zero_grad() {
    cudaStream_t stream = current_stream();
    for (auto &parameter : parameters_) {
      if (parameter.grad().defined()) {
        cgraph_zero(parameter.mutable_grad().data_ptr<float>(),
                    parameter.numel(), stream);
      }
    }
  }

  void step() {
    cudaStream_t stream = current_stream();
    for (size_t index = 0; index < parameters_.size(); ++index) {
      const auto &gradient = parameters_[index].grad();
      if (!gradient.defined() || gradient.scalar_type() != torch::kFloat32 ||
          !gradient.is_contiguous()) {
        throw std::runtime_error(
            "GraphAdam requires contiguous FP32 gradients");
      }
      cgraph_grad_sumsq(gradient.data_ptr<float>(),
                        partials_.data_ptr<float>() + partial_offsets_[index],
                        gradient.numel(), stream);
    }
    cgraph_finish_grad_norm(partials_.data_ptr<float>(), partials_.numel(),
                            kGradClip, grad_scale_.data_ptr<float>(), stream);
    cgraph_adam_bias(step_.data_ptr<float>(), step_size_.data_ptr<float>(),
                     denom_scale_.data_ptr<float>(),
                     learning_rate_.data_ptr<float>(), beta1_, beta2_, stream);
    for (size_t index = 0; index < parameters_.size(); ++index) {
      auto &parameter = parameters_[index];
      cgraph_adam_step(
          parameter.data_ptr<float>(), parameter.grad().data_ptr<float>(),
          first_[index].data_ptr<float>(), second_[index].data_ptr<float>(),
          parameter.numel(), grad_scale_.data_ptr<float>(),
          step_size_.data_ptr<float>(), denom_scale_.data_ptr<float>(), beta1_,
          beta2_, epsilon_, stream);
    }
  }

  AdamSnapshot snapshot() const {
    AdamSnapshot result;
    for (const auto &parameter : parameters_) {
      result.parameters.push_back(parameter.detach().clone());
    }
    for (const auto &value : first_) {
      result.first.push_back(value.clone());
    }
    for (const auto &value : second_) {
      result.second.push_back(value.clone());
    }
    result.step = step_.clone();
    return result;
  }

  void restore(const AdamSnapshot &snapshot) {
    torch::NoGradGuard no_grad;
    for (size_t index = 0; index < parameters_.size(); ++index) {
      parameters_[index].copy_(snapshot.parameters[index]);
      first_[index].copy_(snapshot.first[index]);
      second_[index].copy_(snapshot.second[index]);
    }
    step_.copy_(snapshot.step);
  }

  const std::vector<torch::Tensor> &parameters() const { return parameters_; }
  const torch::Tensor &grad_scale() const { return grad_scale_; }

private:
  cudaStream_t current_stream() const {
    return c10::cuda::getCurrentCUDAStream(parameters_.front().get_device())
        .stream();
  }

  std::vector<torch::Tensor> parameters_;
  std::vector<torch::Tensor> first_;
  std::vector<torch::Tensor> second_;
  std::vector<std::size_t> partial_offsets_;
  std::vector<int> partial_counts_;
  torch::Tensor learning_rate_;
  torch::Tensor step_;
  torch::Tensor step_size_;
  torch::Tensor denom_scale_;
  torch::Tensor grad_scale_;
  torch::Tensor partials_;
  float beta1_ = 0.9F;
  float beta2_ = 0.999F;
  float epsilon_ = 1.0e-8F;
};

struct UpdateResult {
  torch::Tensor ratio;
  torch::Tensor entropy;
  torch::Tensor loss;
};

UpdateResult
update_body(ChainPolicy &model, GraphAdam &optimizer,
            const torch::Tensor &observations, const torch::Tensor &scalars,
            const torch::Tensor &actions, const torch::Tensor &old_logprob,
            const torch::Tensor &advantages, const torch::Tensor &returns,
            const torch::Tensor &weights) {
  optimizer.zero_grad();
  auto output = policy_forward(model, obs_float(observations, false), scalars,
                               Bf16Scope::kDisabled);
  auto distribution = categorical(output.first, &actions);
  auto ratio = torch::exp(distribution.logprob - old_logprob);
  auto denominator = weights.sum();
  auto policy_loss =
      (-torch::min(ratio * advantages,
                   ratio.clamp(1.0 - kClip, 1.0 + kClip) * advantages) *
       weights)
          .sum() /
      denominator;
  auto value_loss =
      0.5 * ((returns - output.second).square() * weights).sum() / denominator;
  auto entropy = (distribution.entropy * weights).sum() / denominator;
  auto loss = policy_loss + value_loss - kEntropy * entropy;
  loss.backward();
  optimizer.step();
  return {ratio.detach(), entropy.detach(), loss.detach()};
}

class RolloutGraph {
public:
  RolloutGraph(ChainPolicy &model, torch::Device device, int64_t batch,
               const torch::Tensor &noop)
      : model_(model),
        input_obs_(torch::zeros(
            {batch, kChannels, kCamH, kCamW},
            torch::TensorOptions().dtype(torch::kUInt8).device(device))),
        input_scalars_(torch::zeros(
            {batch, kScalars},
            torch::TensorOptions().dtype(torch::kFloat32).device(device))),
        input_burnin_(torch::zeros(
            {batch},
            torch::TensorOptions().dtype(torch::kBool).device(device))),
        noop_(noop), device_(device.index()) {
    capture();
  }

  CategoricalResult replay(const torch::Tensor &observations,
                           const torch::Tensor &scalars,
                           const torch::Tensor &burnin) {
    input_obs_.copy_(observations);
    input_scalars_.copy_(scalars);
    input_burnin_.copy_(burnin);
    graph_.replay();
    return sampled_;
  }

  const torch::Tensor &values() const { return values_; }

private:
  void body() {
    torch::NoGradGuard no_grad;
    auto output = policy_forward(model_, obs_float(input_obs_, false),
                                 input_scalars_, Bf16Scope::kDisabled);
    sampled_ = categorical(output.first, nullptr, &noop_, &input_burnin_);
    values_ = output.second;
  }

  void capture() {
    torch::cuda::synchronize(device_);
    auto stream = c10::cuda::getStreamFromPool(false, device_);
    {
      c10::cuda::CUDAStreamGuard guard(stream);
      for (int warmup = 0; warmup < 3; ++warmup) {
        body();
      }
      stream.synchronize();
      graph_.capture_begin();
      body();
      graph_.capture_end();
    }
    stream.synchronize();
    std::cout << "CGRAPH_CAPTURE rollout graph="
              << static_cast<void *>(graph_.raw_cuda_graph_exec()) << std::endl;
  }

  ChainPolicy &model_;
  torch::Tensor input_obs_;
  torch::Tensor input_scalars_;
  torch::Tensor input_burnin_;
  torch::Tensor noop_;
  CategoricalResult sampled_;
  torch::Tensor values_;
  int device_;
  at::cuda::CUDAGraph graph_;
};

class UpdateGraph {
public:
  UpdateGraph(ChainPolicy &model, GraphAdam &optimizer, torch::Device device,
              int64_t batch)
      : model_(model), optimizer_(optimizer),
        observations_(torch::zeros(
            {batch, kChannels, kCamH, kCamW},
            torch::TensorOptions().dtype(torch::kUInt8).device(device))),
        scalars_(torch::zeros({batch, kScalars},
                              observations_.options().dtype(torch::kFloat32))),
        actions_(torch::zeros({batch, kHeads},
                              observations_.options().dtype(torch::kInt64))),
        old_logprob_(torch::zeros(
            {batch}, observations_.options().dtype(torch::kFloat32))),
        advantages_(torch::zeros_like(old_logprob_)),
        returns_(torch::zeros_like(old_logprob_)),
        weights_(torch::ones_like(old_logprob_)), device_(device.index()) {}

  UpdateResult replay(const torch::Tensor &observations,
                      const torch::Tensor &scalars,
                      const torch::Tensor &actions,
                      const torch::Tensor &old_logprob,
                      const torch::Tensor &advantages,
                      const torch::Tensor &returns, float learning_rate) {
    const int64_t rows = observations.size(0);
    if (rows <= 0 || rows > observations_.size(0)) {
      throw std::runtime_error("invalid cgraph update minibatch size");
    }
    weights_.zero_();
    weights_.narrow(0, 0, rows).fill_(1.0);
    observations_.narrow(0, 0, rows).copy_(observations);
    scalars_.narrow(0, 0, rows).copy_(scalars);
    actions_.narrow(0, 0, rows).copy_(actions);
    old_logprob_.narrow(0, 0, rows).copy_(old_logprob);
    advantages_.narrow(0, 0, rows).copy_(advantages);
    returns_.narrow(0, 0, rows).copy_(returns);
    optimizer_.set_learning_rate(learning_rate);
    if (!captured_) {
      capture();
    }
    graph_.replay();
    UpdateResult result = result_;
    result.ratio = result_.ratio.narrow(0, 0, rows);
    return result;
  }

private:
  void body() {
    result_ = update_body(model_, optimizer_, observations_, scalars_, actions_,
                          old_logprob_, advantages_, returns_, weights_);
  }

  void capture() {
    auto snapshot = optimizer_.snapshot();
    torch::cuda::synchronize(device_);
    auto stream = c10::cuda::getStreamFromPool(false, device_);
    {
      c10::cuda::CUDAStreamGuard guard(stream);
      body();
      for (int warmup = 0; warmup < 2; ++warmup) {
        body();
      }
      stream.synchronize();
      graph_.capture_begin();
      body();
      graph_.capture_end();
    }
    stream.synchronize();
    optimizer_.restore(snapshot);
    captured_ = true;
    std::cout << "CGRAPH_CAPTURE update graph="
              << static_cast<void *>(graph_.raw_cuda_graph_exec()) << std::endl;
  }

  ChainPolicy &model_;
  GraphAdam &optimizer_;
  torch::Tensor observations_;
  torch::Tensor scalars_;
  torch::Tensor actions_;
  torch::Tensor old_logprob_;
  torch::Tensor advantages_;
  torch::Tensor returns_;
  torch::Tensor weights_;
  UpdateResult result_;
  int device_;
  bool captured_ = false;
  at::cuda::CUDAGraph graph_;
};

torch::Tensor actions_to_rows(const torch::Tensor &actions,
                              torch::Device device) {
  auto rows = torch::zeros(
      {actions.size(0), 13},
      torch::TensorOptions().dtype(torch::kFloat64).device(device));
  auto yaw = torch::tensor({-15.0, 0.0, 15.0}, rows.options());
  auto pitch = torch::tensor({-10.0, 0.0, 10.0}, rows.options());
  auto forward = torch::tensor({-1.0, 0.0, 1.0}, rows.options());
  rows.index_put_({Slice(), 2}, yaw.index({actions.index({Slice(), 0})}));
  rows.index_put_({Slice(), 3}, pitch.index({actions.index({Slice(), 1})}));
  rows.index_put_({Slice(), 0}, forward.index({actions.index({Slice(), 2})}));
  rows.index_put_({Slice(), 4},
                  actions.index({Slice(), 3}).to(torch::kFloat64));
  rows.index_put_({Slice(), 7},
                  actions.index({Slice(), 4}).to(torch::kFloat64));
  rows.index_put_({Slice(), 8},
                  actions.index({Slice(), 5}).to(torch::kFloat64));
  rows.index_put_({Slice(), 10},
                  actions.index({Slice(), 6}).to(torch::kFloat64) - 1.0);
  rows.index_put_({Slice(), 11},
                  actions.index({Slice(), 7}).to(torch::kFloat64));
  rows.index_put_({Slice(), 9},
                  actions.index({Slice(), 8}).to(torch::kFloat64) - 1.0);
  return rows;
}

torch::Tensor stage_of_best(const torch::Tensor &best) {
  auto stage =
      torch::zeros({best.size(0)}, best.options().dtype(torch::kInt64));
  stage.index_put_(
      {best.index({Slice(), 0}).ge(3) | best.index({Slice(), 1}).ge(1)}, 1);
  stage.index_put_({best.index({Slice(), 5}).ge(1)}, 2);
  stage.index_put_(
      {best.index({Slice(), 5}).ge(1) & best.index({Slice(), 3}).ge(3)}, 3);
  stage.index_put_({best.index({Slice(), 7}).ge(1)}, 4);
  return stage;
}

std::vector<int64_t> tensor_i64(const torch::Tensor &tensor) {
  auto cpu = tensor.to(torch::kCPU).contiguous();
  const int64_t *data = cpu.data_ptr<int64_t>();
  return {data, data + cpu.numel()};
}

std::vector<uint8_t> tensor_u8(const torch::Tensor &tensor) {
  auto cpu = tensor.to(torch::kUInt8).to(torch::kCPU).contiguous();
  const uint8_t *data = cpu.data_ptr<uint8_t>();
  return {data, data + cpu.numel()};
}

void save_native_checkpoint(const ChainPolicy &model, const fs::path &path) {
  if (!path.parent_path().empty())
    fs::create_directories(path.parent_path());
  std::ofstream output(path, std::ios::binary);
  if (!output)
    throw std::runtime_error("cannot write checkpoint");
  const char magic[8] = {'N', 'B', 'F', '1', '6', 'C', 'K', '1'};
  output.write(magic, sizeof(magic));
  auto parameters = model->named_parameters(true);
  uint32_t count = static_cast<uint32_t>(parameters.size());
  output.write(reinterpret_cast<const char *>(&count), sizeof(count));
  for (const auto &item : parameters) {
    auto tensor =
        item.value().detach().to(torch::kCPU).contiguous().to(torch::kFloat32);
    uint32_t name_size = item.key().size();
    uint32_t rank = tensor.dim();
    output.write(reinterpret_cast<const char *>(&name_size), sizeof(name_size));
    output.write(item.key().data(), name_size);
    output.write(reinterpret_cast<const char *>(&rank), sizeof(rank));
    for (int64_t size : tensor.sizes()) {
      output.write(reinterpret_cast<const char *>(&size), sizeof(size));
    }
    uint64_t bytes = tensor.numel() * sizeof(float);
    output.write(reinterpret_cast<const char *>(&bytes), sizeof(bytes));
    output.write(static_cast<const char *>(tensor.data_ptr()), bytes);
  }
}

void load_native_checkpoint(const ChainPolicy &model, const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read checkpoint " + path.string());
  char magic[8]{};
  input.read(magic, sizeof(magic));
  if (std::memcmp(magic, "NBF16CK1", 8) != 0)
    throw std::runtime_error("invalid cgraph checkpoint magic");
  uint32_t count = 0;
  input.read(reinterpret_cast<char *>(&count), sizeof(count));
  auto named = model->named_parameters(true);
  if (count != named.size())
    throw std::runtime_error("cgraph checkpoint parameter count mismatch");
  torch::NoGradGuard no_grad;
  for (uint32_t item = 0; item < count; ++item) {
    uint32_t name_size = 0;
    input.read(reinterpret_cast<char *>(&name_size), sizeof(name_size));
    std::string name(name_size, '\0');
    input.read(name.data(), name_size);
    uint32_t rank = 0;
    input.read(reinterpret_cast<char *>(&rank), sizeof(rank));
    std::vector<int64_t> shape(rank);
    input.read(reinterpret_cast<char *>(shape.data()), rank * sizeof(int64_t));
    uint64_t bytes = 0;
    input.read(reinterpret_cast<char *>(&bytes), sizeof(bytes));
    auto found = named.find(name);
    if (found == nullptr || found->sizes().vec() != shape ||
        bytes != static_cast<uint64_t>(found->numel() * sizeof(float))) {
      throw std::runtime_error("cgraph checkpoint mismatch at " + name);
    }
    auto host =
        torch::empty(shape, torch::TensorOptions().dtype(torch::kFloat32));
    input.read(static_cast<char *>(host.data_ptr()), bytes);
    found->copy_(host.to(found->device()));
  }
  if (!input)
    throw std::runtime_error("truncated cgraph checkpoint");
  if (input.peek() != std::char_traits<char>::eof())
    throw std::runtime_error("trailing cgraph checkpoint bytes");
}

void set_channels_last(const ChainPolicy &model) {
  for (auto &item : model->named_parameters(true)) {
    if (item.value().dim() == 4) {
      item.value().set_data(
          item.value().contiguous(torch::MemoryFormat::ChannelsLast));
    }
  }
}

using TensorMap = std::unordered_map<std::string, torch::Tensor>;

TensorMap load_oracle_fixture(const fs::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input)
    throw std::runtime_error("cannot read oracle fixture");
  char magic[8]{};
  input.read(magic, sizeof(magic));
  if (std::memcmp(magic, "NBORCL1\0", 8) != 0) {
    throw std::runtime_error("invalid oracle fixture magic");
  }
  uint32_t count = 0;
  input.read(reinterpret_cast<char *>(&count), sizeof(count));
  TensorMap tensors;
  for (uint32_t item = 0; item < count; ++item) {
    uint32_t name_size = 0;
    input.read(reinterpret_cast<char *>(&name_size), sizeof(name_size));
    std::string name(name_size, '\0');
    input.read(name.data(), name_size);
    uint8_t dtype = 0;
    uint32_t rank = 0;
    input.read(reinterpret_cast<char *>(&dtype), sizeof(dtype));
    input.read(reinterpret_cast<char *>(&rank), sizeof(rank));
    std::vector<int64_t> sizes(rank);
    input.read(reinterpret_cast<char *>(sizes.data()), rank * sizeof(int64_t));
    uint64_t byte_count = 0;
    input.read(reinterpret_cast<char *>(&byte_count), sizeof(byte_count));
    auto scalar_type = dtype == 1 ? torch::kFloat32 : torch::kInt64;
    auto tensor =
        torch::empty(sizes, torch::TensorOptions().dtype(scalar_type));
    if (byte_count != static_cast<uint64_t>(tensor.nbytes())) {
      throw std::runtime_error("oracle tensor byte count mismatch: " + name);
    }
    input.read(static_cast<char *>(tensor.data_ptr()), byte_count);
    if (!input)
      throw std::runtime_error("truncated oracle tensor: " + name);
    tensors.emplace(std::move(name), std::move(tensor));
  }
  return tensors;
}

std::unordered_map<std::string, double> load_tolerances(const fs::path &path) {
  std::ifstream input(path);
  if (!input)
    throw std::runtime_error("cannot read BF16 tolerances");
  std::unordered_map<std::string, double> result;
  std::string line;
  while (std::getline(input, line)) {
    if (line.empty() || line.front() == '#')
      continue;
    size_t tab = line.find('\t');
    if (tab == std::string::npos)
      continue;
    result.emplace(line.substr(0, tab), std::stod(line.substr(tab + 1)));
  }
  return result;
}

int run_oracle(const fs::path &fixture_path, const fs::path &tolerance_path,
               int device_index, bool oracle_bf16) {
  auto fixture = load_oracle_fixture(fixture_path);
  auto tolerances = load_tolerances(tolerance_path);
  torch::Device device(torch::kCUDA, device_index);
  ChainPolicy model;
  model->to(device);
  {
    torch::NoGradGuard no_grad;
    for (auto &item : model->named_parameters(true)) {
      auto found = fixture.find("state/" + item.key());
      if (found == fixture.end()) {
        throw std::runtime_error("fixture missing parameter " + item.key());
      }
      item.value().copy_(found->second.to(device));
    }
  }
  auto planes = fixture.at("input/planes").to(device);
  auto scalars = fixture.at("input/scalars").to(device);
  auto actions = fixture.at("input/actions").to(device);
  auto advantages = fixture.at("input/advantages").to(device);
  auto old_logprob = fixture.at("input/old_logprob").to(device);
  auto returns = fixture.at("input/returns").to(device);
  auto initial = torch::cat([&]() {
    std::vector<torch::Tensor> values;
    for (const auto &item : model->named_parameters(true)) {
      values.push_back(item.value().detach().flatten());
    }
    return values;
  }());
  GraphAdam optimizer(model, device, 3.0e-4F);
  optimizer.zero_grad();
  std::pair<std::vector<torch::Tensor>, torch::Tensor> output;
  CategoricalResult distribution;
  torch::Tensor policy_loss;
  torch::Tensor value_loss;
  torch::Tensor entropy_loss;
  torch::Tensor loss;
  {
    AutocastGuard autocast(oracle_bf16);
    output = model->forward(planes, scalars);
    distribution = categorical(output.first, &actions);
    auto ratio = torch::exp(distribution.logprob - old_logprob);
    policy_loss =
        -torch::min(ratio * advantages,
                    ratio.clamp(1.0 - kClip, 1.0 + kClip) * advantages)
             .mean();
    value_loss =
        0.5 * (returns - output.second.to(torch::kFloat32)).square().mean();
    entropy_loss = -kEntropy * distribution.entropy.mean();
    loss = policy_loss + value_loss + entropy_loss;
  }
  loss.backward();
  std::vector<torch::Tensor> gradient_parts;
  for (const auto &parameter : model->parameters()) {
    gradient_parts.push_back(
        parameter.grad().detach().to(torch::kFloat32).flatten());
  }
  auto gradients = torch::cat(gradient_parts);
  auto grad_norm = gradients.norm();
  optimizer.step();
  std::vector<torch::Tensor> updated_parts;
  for (const auto &parameter : model->parameters()) {
    updated_parts.push_back(parameter.detach().to(torch::kFloat32).flatten());
  }
  auto update_delta = torch::cat(updated_parts) - initial;

  TensorMap actual;
  std::vector<torch::Tensor> logit_parts;
  for (const auto &tensor : output.first) {
    logit_parts.push_back(tensor.detach().to(torch::kFloat32).flatten());
  }
  actual["logits"] = torch::cat(logit_parts);
  actual["values"] = output.second.detach().to(torch::kFloat32);
  actual["logprob"] = distribution.logprob.detach().to(torch::kFloat32);
  actual["entropy"] = distribution.entropy.detach().to(torch::kFloat32);
  actual["policy_loss"] = policy_loss.detach().reshape({1}).to(torch::kFloat32);
  actual["value_loss"] = value_loss.detach().reshape({1}).to(torch::kFloat32);
  actual["entropy_loss"] =
      entropy_loss.detach().reshape({1}).to(torch::kFloat32);
  actual["loss"] = loss.detach().reshape({1}).to(torch::kFloat32);
  actual["gradients"] = gradients;
  actual["grad_norm"] = grad_norm.detach().reshape({1}).to(torch::kFloat32);
  actual["update_delta"] = update_delta;
  std::vector<torch::Tensor> greedy_parts;
  for (const auto &head : output.first)
    greedy_parts.push_back(head.detach().argmax(1));
  auto greedy = torch::stack(greedy_parts, 1);
  auto expected_greedy = fixture.at("expected/greedy_actions").to(device);
  double greedy_agreement =
      greedy.eq(expected_greedy).to(torch::kFloat32).mean().item<double>();
  bool passed = greedy_agreement >= 0.999;
  std::cout << "ORACLE greedy_agreement=" << greedy_agreement
            << " floor=0.999 pass=" << passed << std::endl;
  for (const auto &[name, ceiling] : tolerances) {
    auto reference =
        fixture.at(oracle_bf16 ? "expected/" + name : "expected/fp32/" + name)
            .to(device);
    double max_abs = (actual.at(name) - reference).abs().max().item<double>();
    bool component_ok = max_abs <= ceiling;
    passed &= component_ok;
    std::cout << "ORACLE component=" << name << " max_abs=" << max_abs
              << " ceiling=" << ceiling << " pass=" << component_ok
              << std::endl;
  }
  auto gradient_reference = fixture.at("expected/fp32/gradients").to(device);
  double gradient_rel_l2 =
      (gradients - gradient_reference).norm().item<double>() /
      gradient_reference.norm().item<double>();
  double gradient_budget =
      fixture.at("input/gradient_budget").data_ptr<float>()[0];
  bool gradient_direction_ok = gradient_rel_l2 <= gradient_budget;
  passed &= gradient_direction_ok;
  std::cout << "ORACLE gradient_rel_l2=" << gradient_rel_l2
            << " ceiling=" << gradient_budget
            << " pass=" << gradient_direction_ok << std::endl;
  std::cout << "ORACLE_RESULT pass=" << passed << std::endl;
  return passed ? 0 : 2;
}

} // namespace

int main() {
  try {
    const int count = env_int("N_ENVS", 6144);
    const int chunk_length = env_int("T_CHUNK", 32);
    const int epochs = env_int("EPOCHS", 2);
    const int minibatch = env_int("MB", 8192);
    const int warmup_chunks = env_int("BENCH_WARMUP_CHUNKS", 2);
    const int measure_chunks = env_int("BENCH_MEASURE_CHUNKS", 0);
    const int device_index = env_int("BLAZE_DEV", 0);
    at::globalContext().setAllowTF32CuDNN(false);
    at::globalContext().setAllowTF32CuBLAS(false);
    if (const char *fixture = std::getenv("CGRAPH_EQUIV_FIXTURE")) {
      return run_oracle(fixture,
                        fs::current_path() / "blaze/rl/cgraph/tolerances.tsv",
                        device_index, false);
    }
    if (std::getenv("REWARD_JSON") != nullptr) {
      throw std::runtime_error(
          "REWARD_JSON is not supported by the native-v1 recipe");
    }
    if (env_bool("IRON_CHAIN", false)) {
      throw std::runtime_error(
          "IRON_CHAIN is not supported by the native-v1 recipe");
    }
    const int rng_seed = env_int("RNG_SEED", 0);
    const bool bf16 = false;
    const Bf16Scope compute_scope = bf16_scope(bf16);
    const Bf16Scope update_scope = Bf16Scope::kDisabled;
    // NCHW is the accepted layout; channels-last was a measured negative
    // (nhwcAddPaddingKernel ~397ms/chunk). Default off.
    const bool channels_last = false;
    const bool profile = env_bool("CGRAPH_PROFILE", false);
    const bool telemetry = env_bool("SMOKE_TELEMETRY", false);
    const int64_t max_ticks = env_i64("MAX_TICKS", 3'000'000'000LL);
    const double max_wall = env_double("MAX_WALL", 6.5 * 3600.0);
    const double learning_rate = env_double("LR", 3.0e-4);
    const double learning_rate_floor = env_double("LR_FLOOR", 1.0e-4);
    const double learning_rate_ticks = env_double("LR_DECAY_TICKS", 1.5e9);
    const double t0_share = env_double("T0_SHARE", 0.30);
    const int cap_refresh = env_int("CAP_REFRESH", 25);
    const auto seeds = training_seeds();
    torch::manual_seed(rng_seed);
    torch::cuda::manual_seed_all(rng_seed);
    torch::Device device(torch::kCUDA, device_index);

    fs::path repo = fs::current_path();
    fs::path shared_object = repo / "blaze/env/blaze_cuda.so";
    NativeEnv env(shared_object.string(), device_index, count);
    env.set_success_item(env_int("SUCCESS_ITEM", 50));
    std::vector<fs::path> snapshot_paths;
    std::vector<std::string> snapshot_strings;
    for (int seed : seeds) {
      fs::path path = repo / "blaze/rl/out/snaps" /
                      ("s" + std::to_string(seed) + "_t0.bsnp");
      snapshot_paths.push_back(path);
      snapshot_strings.push_back(path.string());
    }
    env.load_snapshots(snapshot_strings);
    std::vector<int> lane_seed(count);
    std::vector<int> lane_snapshot(count);
    std::vector<int> lane_stage_start(count, 0);
    std::vector<int> lane_stage(count, 0);
    for (int lane = 0; lane < count; ++lane) {
      lane_seed[lane] = lane % seeds.size();
      lane_snapshot[lane] = lane_seed[lane];
    }
    env.assign(lane_snapshot);
    env.reset();
    auto capture_slot = [&](int seed_i, int stage) {
      return static_cast<int>(seeds.size()) + seed_i * (kStages - 1) + stage -
             1;
    };
    for (int seed_i = 0; seed_i < static_cast<int>(seeds.size()); ++seed_i) {
      for (int stage = 1; stage < kStages; ++stage) {
        env.capture(seed_i, capture_slot(seed_i, stage));
      }
    }
    env.assign(lane_snapshot);
    env.reset();

    auto log_positions = make_log_tensor(snapshot_paths, device);
    ChainPolicy model;
    model->to(device);
    if (const char *initial = std::getenv("CGRAPH_INIT")) {
      load_native_checkpoint(model, initial);
      std::cout << "CGRAPH_INIT path=" << initial << std::endl;
    }
    if (channels_last)
      set_channels_last(model);
    GraphAdam optimizer(model, device, static_cast<float>(learning_rate));
    StageCurriculum curriculum(seeds.size(), rng_seed, t0_share);
    NativeReward reward(count, device, RewardSpec::resolve());

    auto u8 = torch::TensorOptions().dtype(torch::kUInt8).device(device);
    auto i64 = torch::TensorOptions().dtype(torch::kInt64).device(device);
    auto i32 = torch::TensorOptions().dtype(torch::kInt32).device(device);
    auto f32 = torch::TensorOptions().dtype(torch::kFloat32).device(device);
    auto boolean = torch::TensorOptions().dtype(torch::kBool).device(device);
    auto stack = torch::zeros({count, kChannels, kCamH, kCamW}, u8);
    auto scalars = torch::zeros({count, kScalars}, f32);
    auto burnin = torch::ones({count}, boolean);
    auto episode_decisions = torch::randint(0, kEpisodeDecisions, {count}, i32);
    auto lane_seed_tensor = torch::tensor(lane_seed, i64);
    auto lane_stage_tensor = torch::tensor(lane_stage, i64);
    auto obs_buffer =
        torch::zeros({chunk_length, count, kChannels, kCamH, kCamW}, u8);
    auto scalar_buffer = torch::zeros({chunk_length, count, kScalars}, f32);
    auto action_buffer = torch::zeros({chunk_length, count, kHeads}, i64);
    auto logprob_buffer = torch::zeros({chunk_length, count}, f32);
    auto value_buffer = torch::zeros({chunk_length, count}, f32);
    auto reward_buffer = torch::zeros({chunk_length, count}, f32);
    auto terminal_buffer = torch::zeros({chunk_length, count}, boolean);
    auto cut_buffer = torch::zeros({chunk_length, count}, boolean);
    auto valid_buffer = torch::zeros({chunk_length, count}, boolean);
    auto noop = torch::zeros({kHeads}, i64);
    noop.index_put_({0}, 1);
    noop.index_put_({1}, 1);
    noop.index_put_({2}, 1);
    RolloutGraph rollout_graph(model, device, count, noop);
    UpdateGraph update_graph(model, optimizer, device, minibatch);
    std::vector<std::vector<int64_t>> capture_last(
        seeds.size(), std::vector<int64_t>(kStages, -1'000'000'000LL));
    int64_t ticks = 0;
    int chunk = 0;
    int64_t episodes = 0;
    std::array<int64_t, kStages + 1> milestone_hist{};
    std::deque<double> trailing_t0;
    auto process_start = std::chrono::steady_clock::now();
    std::ofstream curve_output;
    if (const char *curve = std::getenv("CGRAPH_CURVE")) {
      fs::path curve_path(curve);
      if (!curve_path.parent_path().empty()) {
        fs::create_directories(curve_path.parent_path());
      }
      curve_output.open(curve_path);
      if (!curve_output)
        throw std::runtime_error("cannot write native curve");
      curve_output
          << "chunk,ticks,wall_s,reward_mean,adv_absmean,value_mean,entropy,"
             "kl,pnorm,episodes,t0_success,available_cells,finite\n";
    }

    std::cout << "cgraph PPO N=" << count << " T=" << chunk_length
              << " EPOCHS=" << epochs << " MB=" << minibatch << " bf16=" << bf16
              << " channels_last=" << channels_last << " bf16_scope="
              << (compute_scope == Bf16Scope::kFull
                      ? "full"
                      : (compute_scope == Bf16Scope::kConv ? "conv" : "off"))
              << " update_bf16=" << (update_scope != Bf16Scope::kDisabled)
              << std::endl;

    while (ticks < max_ticks) {
      double loss_sum = 0.0;
      double entropy_sum = 0.0;
      double kl_sum = 0.0;
      int64_t update_count = 0;
      bool measured =
          chunk >= warmup_chunks && chunk < warmup_chunks + measure_chunks;
      std::chrono::steady_clock::time_point sample_start;
      if (measured) {
        torch::cuda::synchronize(device_index);
        sample_start = std::chrono::steady_clock::now();
        if (profile) {
          cudaProfilerStart();
          nvtxRangePushA("cgraph_training_chunk");
        }
      }

      {
        ProfileRange rollout_range(profile && measured, "cgraph/rollout");
        for (int step = 0; step < chunk_length; ++step) {
          torch::NoGradGuard no_grad;
          CategoricalResult sampled;
          torch::Tensor actions;
          {
            ProfileRange policy_range(profile && measured,
                                      "cgraph/rollout_policy");
            sampled = rollout_graph.replay(stack, scalars, burnin);
            actions = sampled.actions;
          }

          obs_buffer.index_put_({step}, stack);
          scalar_buffer.index_put_({step}, scalars);
          action_buffer.index_put_({step}, actions);
          logprob_buffer.index_put_({step}, sampled.logprob);
          value_buffer.index_put_({step}, rollout_graph.values());
          valid_buffer.index_put_({step}, ~burnin);

          {
            ProfileRange env_range(profile && measured, "cgraph/rollout_env");
            env.step(actions_to_rows(actions, device), kRepeat);
          }
          torch::Tensor decision_reward;
          {
            ProfileRange reward_range(profile && measured,
                                      "cgraph/rollout_reward");
            decision_reward =
                reward.step(env.status, env.cam, actions, env.pose, env.scal,
                            env.done, lane_seed_tensor, log_positions);
          }
          auto terminal = env.done.gt(0);
          auto success = env.done.eq(1);
          episode_decisions += (~burnin).to(torch::kInt32);
          auto truncated = ~terminal & episode_decisions.ge(kEpisodeDecisions);
          auto ended = terminal | truncated;
          reward_buffer.index_put_({step}, decision_reward);
          terminal_buffer.index_put_({step}, terminal);
          cut_buffer.index_put_({step}, ended);

          auto frame = build_frame(env);
          stack.index_put_({Slice(), Slice(0, -kPlanes)},
                           stack.index({Slice(), Slice(kPlanes)}).clone());
          stack.index_put_({Slice(), Slice(-kPlanes)}, frame);
          // Sparse burn-in fill (matches Python). Full-tensor where/repeat
          // every step was a measured regression (~+11ms/chunk).
          if (burnin.any().item<bool>()) {
            stack.index_put_({burnin},
                             frame.index({burnin}).repeat({1, kStack, 1, 1}));
          }
          scalars = build_scalars(env, episode_decisions);
          burnin.fill_(false);

          auto current_stage = stage_of_best(reward.best);
          auto live = ~terminal;
          // One host sync for all stage-capture candidates (was 4× any().item).
          torch::Tensor capture_any = torch::zeros({}, boolean);
          std::array<torch::Tensor, kStages> stage_candidates{};
          for (int stage = 1; stage < kStages; ++stage) {
            auto candidates =
                current_stage.eq(stage) & live & lane_stage_tensor.lt(stage);
            if (stage == kStages - 1) {
              candidates &= env.status.index({Slice(), 2}).ge(1);
            }
            stage_candidates[stage] = candidates;
            capture_any = capture_any.logical_or(candidates.any());
          }
          if (capture_any.item<bool>()) {
            for (int stage = 1; stage < kStages; ++stage) {
              auto lanes = tensor_i64(
                  torch::nonzero(stage_candidates[stage]).squeeze(1));
              if (lanes.empty())
                continue;
              for (size_t item = 0; item < std::min<size_t>(8, lanes.size());
                   ++item) {
                int lane = static_cast<int>(lanes[item]);
                int seed_i = lane_seed[lane];
                if (chunk - capture_last[seed_i][stage] < cap_refresh)
                  continue;
                env.capture(lane, capture_slot(seed_i, stage));
                capture_last[seed_i][stage] = chunk;
                curriculum.available[seed_i][stage] = true;
              }
              for (int64_t lane : lanes) {
                lane_stage[lane] = std::max(lane_stage[lane], stage);
              }
            }
            lane_stage_tensor = torch::tensor(lane_stage, i64);
          }

          // Host sync only when at least one episode ended this step.
          if (ended.any().item<bool>()) {
            auto indices = torch::nonzero(ended).squeeze(1);
            auto lanes = tensor_i64(indices);
            auto reached = tensor_i64(current_stage.index({indices}));
            auto success_host = tensor_u8(success.index({indices}));
            for (size_t item = 0; item < lanes.size(); ++item) {
              int lane = static_cast<int>(lanes[item]);
              int start = lane_stage_start[lane];
              int achieved = success_host[item]
                                 ? kStages
                                 : static_cast<int>(reached[item]);
              bool okay = start >= kStages - 1 ? success_host[item] != 0
                                               : achieved >= start + 1;
              curriculum.record(lane_seed[lane], start, okay);
              ++milestone_hist[std::min(achieved, kStages)];
              if (start == 0) {
                if (trailing_t0.size() == 200)
                  trailing_t0.pop_front();
                trailing_t0.push_back(success_host[item] ? 1.0 : 0.0);
              }
            }
            episodes += static_cast<int64_t>(lanes.size());
            auto replacements =
                curriculum.sample(static_cast<int>(lanes.size()));
            std::vector<uint8_t> reset_mask(count, 0);
            for (size_t item = 0; item < lanes.size(); ++item) {
              int lane = static_cast<int>(lanes[item]);
              int seed_i = replacements.first[item];
              int stage = replacements.second[item];
              lane_seed[lane] = seed_i;
              lane_stage_start[lane] = stage;
              lane_stage[lane] = stage;
              lane_snapshot[lane] =
                  stage == 0 ? seed_i : capture_slot(seed_i, stage);
              reset_mask[lane] = 1;
            }
            env.assign(lane_snapshot);
            env.reset(&reset_mask);
            lane_seed_tensor = torch::tensor(lane_seed, i64);
            lane_stage_tensor = torch::tensor(lane_stage, i64);
            episode_decisions.index_put_({indices}, 0);
            burnin.index_put_({indices}, true);
            reward.reset(indices);
          }
          ticks += static_cast<int64_t>(count) * kRepeat;
        }
      }

      torch::Tensor advantages;
      torch::Tensor returns;
      {
        ProfileRange gae_range(profile && measured, "cgraph/gae");
        torch::NoGradGuard no_grad;
        std::pair<std::vector<torch::Tensor>, torch::Tensor> output;
        output = policy_forward(
            model, obs_float(stack, channels_last, /*to_bf16=*/bf16), scalars,
            compute_scope);
        advantages = torch::zeros_like(reward_buffer);
        auto gae = torch::zeros({count}, f32);
        auto next_value = output.second.to(torch::kFloat32);
        for (int step = chunk_length - 1; step >= 0; --step) {
          auto nonterminal =
              (~terminal_buffer.index({step})).to(torch::kFloat32);
          auto keep = (~cut_buffer.index({step})).to(torch::kFloat32);
          auto delta = reward_buffer.index({step}) +
                       kGamma * next_value * nonterminal -
                       value_buffer.index({step});
          gae = delta + kGamma * kLambda * keep * gae;
          advantages.index_put_({step}, gae);
          next_value = value_buffer.index({step});
        }
        returns = advantages + value_buffer;
      }

      {
        ProfileRange ppo_range(profile && measured, "cgraph/ppo");
        auto selected = torch::nonzero(valid_buffer.flatten()).squeeze(1);
        int64_t transitions = selected.numel();
        if (transitions > 0) {
          auto flat_obs = obs_buffer.flatten(0, 1);
          auto flat_scalars = scalar_buffer.flatten(0, 1);
          auto flat_actions = action_buffer.flatten(0, 1);
          auto flat_logprob = logprob_buffer.flatten();
          auto flat_advantages = advantages.flatten().index({selected});
          auto flat_returns = returns.flatten().index({selected});
          flat_advantages = (flat_advantages - flat_advantages.mean()) /
                            (flat_advantages.std() + 1.0e-8);
          double progress = std::min(1.0, ticks / learning_rate_ticks);
          double current_lr = std::max(
              learning_rate_floor,
              learning_rate * (1.0 - progress * (1.0 - learning_rate_floor /
                                                           learning_rate)));
          optimizer.set_learning_rate(static_cast<float>(current_lr));
          for (int epoch = 0; epoch < epochs; ++epoch) {
            auto permutation =
                selected.index({torch::randperm(transitions, i64)});
            auto order = torch::searchsorted(selected, permutation);
            for (int64_t begin = 0; begin < transitions; begin += minibatch) {
              int64_t end = std::min(begin + minibatch, transitions);
              auto mb = permutation.index({Slice(begin, end)});
              auto positions = order.index({Slice(begin, end)});
              UpdateResult result;
              {
                ProfileRange forward_range(profile && measured,
                                           "cgraph/ppo_forward_loss");
                auto mb_observations = flat_obs.index({mb});
                auto mb_scalars = flat_scalars.index({mb});
                auto mb_actions = flat_actions.index({mb});
                auto mb_logprob = flat_logprob.index({mb});
                auto mb_advantages = flat_advantages.index({positions});
                auto mb_returns = flat_returns.index({positions});
                result = update_graph.replay(
                    mb_observations, mb_scalars, mb_actions, mb_logprob,
                    mb_advantages, mb_returns, static_cast<float>(current_lr));
              }
              if (telemetry) {
                loss_sum += result.loss.item<double>();
                double entropy = result.entropy.item<double>();
                double kl = (result.ratio - 1.0 - torch::log(result.ratio))
                                .mean()
                                .item<double>();
                entropy_sum += entropy;
                kl_sum += kl;
                ++update_count;
              }
            }
          }
        }
      }

      if (telemetry) {
        torch::cuda::synchronize(device_index);
        double reward_mean = reward_buffer.mean().item<double>();
        double loss_mean = update_count > 0 ? loss_sum / update_count : 0.0;
        double entropy_mean =
            update_count > 0 ? entropy_sum / update_count : 0.0;
        double kl_mean = update_count > 0 ? kl_sum / update_count : 0.0;
        double advantage_absmean = advantages.abs().mean().item<double>();
        double value_mean = value_buffer.mean().item<double>();
        double parameter_norm =
            flattened_parameters(model).norm().item<double>();
        int available_cells = 0;
        for (const auto &row : curriculum.available) {
          available_cells += std::count(row.begin(), row.end(), true);
        }
        double t0_success =
            trailing_t0.empty()
                ? std::numeric_limits<double>::quiet_NaN()
                : std::accumulate(trailing_t0.begin(), trailing_t0.end(), 0.0) /
                      trailing_t0.size();
        bool finite = std::isfinite(reward_mean) && std::isfinite(loss_mean) &&
                      std::isfinite(entropy_mean) && std::isfinite(kl_mean) &&
                      std::isfinite(advantage_absmean) &&
                      std::isfinite(value_mean) &&
                      std::isfinite(parameter_norm) && parameter_norm > 0.0;
        std::cout << "SMOKE chunk=" << (chunk + 1) << " ticks=" << ticks
                  << " reward_mean=" << reward_mean
                  << " adv_absmean=" << advantage_absmean
                  << " value_mean=" << value_mean << " ent=" << entropy_mean
                  << " kl=" << kl_mean << " pnorm=" << parameter_norm
                  << std::endl;
        if (curve_output) {
          double wall = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - process_start)
                            .count();
          curve_output << (chunk + 1) << ',' << ticks << ',' << wall << ','
                       << reward_mean << ',' << advantage_absmean << ','
                       << value_mean << ',' << entropy_mean << ',' << kl_mean
                       << ',' << parameter_norm << ',' << episodes << ','
                       << t0_success << ',' << available_cells << ',' << finite
                       << '\n';
          curve_output.flush();
        }
        if (!finite) {
          throw std::runtime_error("non-finite or stalled cgraph PPO update");
        }
      }

      if (measured) {
        torch::cuda::synchronize(device_index);
        auto elapsed = std::chrono::duration<double, std::milli>(
                           std::chrono::steady_clock::now() - sample_start)
                           .count();
        double sample_ticks =
            static_cast<double>(count) * chunk_length * kRepeat;
        std::cout << "BENCH chunk=" << (chunk - warmup_chunks)
                  << " wall_ms=" << elapsed
                  << " env_ticks_per_s=" << sample_ticks / (elapsed / 1000.0)
                  << std::endl;
        if (profile) {
          nvtxRangePop();
          cudaProfilerStop();
        }
      }

      ++chunk;
      double wall = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - process_start)
                        .count();
      if (measure_chunks > 0 && chunk >= warmup_chunks + measure_chunks)
        break;
      if (wall >= max_wall)
        break;
    }

    const char *checkpoint = std::getenv("CGRAPH_CHECKPOINT");
    if (checkpoint != nullptr) {
      save_native_checkpoint(model, checkpoint);
      std::cout << "CGRAPH_CHECKPOINT path=" << checkpoint << std::endl;
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "cgraph_train: " << error.what() << std::endl;
    return 1;
  }
}
