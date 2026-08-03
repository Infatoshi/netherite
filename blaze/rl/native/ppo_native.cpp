#include <ATen/autocast_mode.h>
#include <c10/cuda/CUDACachingAllocator.h>
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
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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
    return std::stoll(value);
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
  // Save/restore so nested FP32 value-head scopes can force-disable autocast.
  explicit AutocastGuard(bool enable)
      : prev_enabled(at::autocast::is_autocast_enabled(at::kCUDA)) {
    if (enable) {
      at::autocast::set_autocast_dtype(at::kCUDA, at::kBFloat16);
      at::autocast::set_autocast_enabled(at::kCUDA, true);
    } else {
      at::autocast::set_autocast_enabled(at::kCUDA, false);
    }
  }
  ~AutocastGuard() {
    at::autocast::set_autocast_enabled(at::kCUDA, prev_enabled);
  }
  bool prev_enabled;
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

// Value head 256->1: force FP32 matmul (params stay float; autocast would
// otherwise run the linear in BF16). Negligible cost vs conv/fc.
torch::Tensor value_forward(ChainPolicy &model, const torch::Tensor &hidden,
                            bool fp32_value_head) {
  if (!fp32_value_head)
    return model->value->forward(hidden).squeeze(-1);
  AutocastGuard no_autocast(false);
  return model->value->forward(hidden.to(torch::kFloat32)).squeeze(-1);
}

std::pair<std::vector<torch::Tensor>, torch::Tensor>
policy_forward(ChainPolicy &model, const torch::Tensor &planes,
               const torch::Tensor &scalars, Bf16Scope scope,
               bool fp32_value_head = false) {
  auto finish = [&](const torch::Tensor &hidden)
      -> std::pair<std::vector<torch::Tensor>, torch::Tensor> {
    std::vector<torch::Tensor> logits;
    logits.reserve(kHeads);
    for (auto &module : *model->heads) {
      logits.push_back(module->as<torch::nn::Linear>()->forward(hidden));
    }
    return {std::move(logits), value_forward(model, hidden, fp32_value_head)};
  };

  if (scope == Bf16Scope::kFull) {
    AutocastGuard autocast(true);
    if (!fp32_value_head)
      return model->forward(planes, scalars);
    auto hidden =
        model->fc->forward(torch::cat({model->conv->forward(planes), scalars}, 1));
    return finish(hidden);
  }
  if (scope == Bf16Scope::kDisabled) {
    auto hidden =
        model->fc->forward(torch::cat({model->conv->forward(planes), scalars}, 1));
    return finish(hidden);
  }

  torch::Tensor convolution;
  {
    AutocastGuard autocast(true);
    convolution = model->conv->forward(planes);
  }
  auto hidden = model->fc->forward(
      torch::cat({convolution.to(torch::kFloat32), scalars}, 1));
  return finish(hidden);
}

double value_head_grad_norm(const ChainPolicy &model) {
  auto weight_grad = model->value->weight.grad();
  auto bias_grad = model->value->bias.grad();
  double total_sq = 0.0;
  if (weight_grad.defined()) {
    double n = weight_grad.detach().to(torch::kFloat32).norm().item<double>();
    total_sq += n * n;
  }
  if (bias_grad.defined()) {
    double n = bias_grad.detach().to(torch::kFloat32).norm().item<double>();
    total_sq += n * n;
  }
  return std::sqrt(total_sq);
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
    int64_t rows =
        buffer->defined() ? std::max(batch, buffer->size(0)) : batch;
    *buffer = torch::empty(
        {rows, channels, height, width},
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

// Single-pass multi-head categorical: one softmax/log_softmax per head.
// Matches Python's Categorical(sample) then log_prob on the same dist, with
// optional burn-in action override before the log_prob gather.
CategoricalResult categorical(const std::vector<torch::Tensor> &logits,
                              const torch::Tensor *fixed_actions = nullptr,
                              const torch::Tensor *force_actions = nullptr,
                              const torch::Tensor *force_mask = nullptr) {
  std::vector<torch::Tensor> actions;
  actions.reserve(kHeads);
  auto total_logprob =
      torch::zeros({logits.front().size(0)},
                   logits.front().options().dtype(torch::kFloat32));
  auto total_entropy = torch::zeros_like(total_logprob);
  for (int head = 0; head < kHeads; ++head) {
    auto logits_f = logits[head].to(torch::kFloat32);
    auto log_probs = torch::log_softmax(logits_f, -1);
    auto probs = log_probs.exp();
    torch::Tensor action;
    if (fixed_actions != nullptr) {
      action = fixed_actions->index({Slice(), head});
    } else {
      action = torch::multinomial(probs, 1).squeeze(1);
      if (force_actions != nullptr && force_mask != nullptr) {
        action = torch::where((*force_mask),
                              force_actions->index({head}).expand_as(action),
                              action);
      }
    }
    actions.push_back(action);
    total_logprob += log_probs.gather(1, action.unsqueeze(1)).squeeze(1);
    total_entropy -= (probs * log_probs).sum(1);
  }
  return {torch::stack(actions, 1), total_logprob, total_entropy};
}

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
  torch::optim::Adam optimizer(model->parameters(),
                               torch::optim::AdamOptions(3.0e-4));
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
  int64_t sampling_seed =
      fixture.at("input/sampling_seed").data_ptr<int64_t>()[0];
  torch::manual_seed(sampling_seed);
  torch::cuda::manual_seed_all(sampling_seed);
  auto sampled = categorical(output.first).actions;
  bool actions_exact = torch::equal(
      sampled.cpu(), fixture.at(oracle_bf16 ? "expected/sample_actions_bf16"
                                            : "expected/sample_actions_fp32"));

  loss.backward();
  std::vector<torch::Tensor> gradient_parts;
  for (const auto &parameter : model->parameters()) {
    gradient_parts.push_back(
        parameter.grad().detach().to(torch::kFloat32).flatten());
  }
  auto gradients = torch::cat(gradient_parts);
  auto grad_norm =
      torch::nn::utils::clip_grad_norm_(model->parameters(), kGradClip);
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
  actual["grad_norm"] = torch::tensor(
      {grad_norm},
      torch::TensorOptions().dtype(torch::kFloat32).device(device));
  actual["update_delta"] = update_delta;
  bool passed = actions_exact;
  std::cout << "ORACLE sample_actions_exact=" << actions_exact << std::endl;
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
    const int measure_chunks = env_int("BENCH_MEASURE_CHUNKS", 1);
    const int device_index = env_int("BLAZE_DEV", 0);
    if (const char *fixture = std::getenv("NATIVE_ORACLE_FIXTURE")) {
      bool oracle_bf16 = env_bool("NATIVE_ORACLE_BF16", true);
      return run_oracle(fixture,
                        fs::current_path() /
                            (oracle_bf16
                                 ? "blaze/rl/native/bf16_tolerances.tsv"
                                 : "blaze/rl/native/fp32_tolerances.tsv"),
                        device_index, oracle_bf16);
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
    const bool bf16 = env_bool("NATIVE_BF16", true);
    const Bf16Scope compute_scope = bf16_scope(bf16);
    const Bf16Scope update_scope = env_bool("NATIVE_BF16_UPDATE", true)
                                       ? compute_scope
                                       : Bf16Scope::kDisabled;
    // NCHW is the accepted layout; channels-last was a measured negative
    // (nhwcAddPaddingKernel ~397ms/chunk). Default off.
    const bool channels_last = env_bool("NATIVE_CHANNELS_LAST", false);
    const bool profile = env_bool("NATIVE_PROFILE", false);
    const bool telemetry = env_bool("NATIVE_TELEMETRY", false);
    // Value-head explosion fixes (default off: oracle parity unchanged).
    const bool fp32_value_head = env_bool("FP32_VALUE_HEAD", false);
    const bool value_clip = env_bool("VALUE_CLIP", false);
    const bool ret_norm = env_bool("RET_NORM", false);
    const int64_t max_ticks = env_i64("MAX_TICKS", 3'000'000'000LL);
    const double max_wall = env_double("MAX_WALL", 6.5 * 3600.0);
    const double learning_rate = env_double("LR", 3.0e-4);
    const double learning_rate_floor = env_double("LR_FLOOR", 1.0e-4);
    const double learning_rate_ticks = env_double("LR_DECAY_TICKS", 1.5e9);
    const double t0_share = env_double("T0_SHARE", 0.30);
    const int cap_refresh = env_int("CAP_REFRESH", 25);
    // RET_NORM: EMA of return mean/std (popart-lite targets only).
    const double ret_norm_momentum = env_double("RET_NORM_MOMENTUM", 0.99);
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
    if (channels_last)
      set_channels_last(model);
    torch::optim::Adam optimizer(model->parameters(),
                                 torch::optim::AdamOptions(learning_rate));
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
    std::vector<std::vector<int64_t>> capture_last(
        seeds.size(), std::vector<int64_t>(kStages, -1'000'000'000LL));
    int64_t ticks = 0;
    int chunk = 0;
    int64_t episodes = 0;
    std::array<int64_t, kStages + 1> milestone_hist{};
    std::deque<double> trailing_t0;
    auto process_start = std::chrono::steady_clock::now();
    std::ofstream curve_output;
    if (const char *curve = std::getenv("NATIVE_CURVE")) {
      fs::path curve_path(curve);
      if (!curve_path.parent_path().empty()) {
        fs::create_directories(curve_path.parent_path());
      }
      curve_output.open(curve_path);
      if (!curve_output)
        throw std::runtime_error("cannot write native curve");
      curve_output
          << "chunk,ticks,wall_s,reward_mean,loss_mean,grad_norm_mean,"
             "grad_norm_max,update_norm,episodes,t0_success,"
             "available_cells,allocated_gib,peak_allocated_gib,finite\n";
    }
    std::ofstream telemetry_json;
    if (telemetry) {
      // Optional JSONL path; stdout NATIVE_SMOKE lines always emit when
      // NATIVE_TELEMETRY=1. JSONL is the Phase-1 health contract surface.
      if (const char *json_path = std::getenv("NATIVE_TELEMETRY_JSON")) {
        fs::path path(json_path);
        if (!path.parent_path().empty())
          fs::create_directories(path.parent_path());
        telemetry_json.open(path);
        if (!telemetry_json)
          throw std::runtime_error("cannot write NATIVE_TELEMETRY_JSON");
      }
    }
    // RET_NORM running stats (updated once per chunk from valid returns).
    double ret_run_mean = 0.0;
    double ret_run_var = 1.0;
    bool ret_run_init = false;

    std::cout << "native PPO N=" << count << " T=" << chunk_length
              << " EPOCHS=" << epochs << " MB=" << minibatch << " bf16=" << bf16
              << " channels_last=" << channels_last << " bf16_scope="
              << (compute_scope == Bf16Scope::kFull
                      ? "full"
                      : (compute_scope == Bf16Scope::kConv ? "conv" : "off"))
              << " update_bf16=" << (update_scope != Bf16Scope::kDisabled)
              << " fp32_value_head=" << fp32_value_head
              << " value_clip=" << value_clip << " ret_norm=" << ret_norm
              << std::endl;

    while (ticks < max_ticks) {
      auto parameters_before =
          telemetry ? flattened_parameters(model) : torch::Tensor();
      double loss_sum = 0.0;
      double grad_norm_sum = 0.0;
      double grad_norm_max = 0.0;
      double policy_loss_sum = 0.0;
      double value_loss_sum = 0.0;
      double entropy_sum = 0.0;
      double approx_kl_sum = 0.0;
      double clip_frac_sum = 0.0;
      double value_grad_norm_sum = 0.0;
      int64_t grad_clip_hits = 0;
      int64_t update_count = 0;
      bool measured =
          chunk >= warmup_chunks && chunk < warmup_chunks + measure_chunks;
      // Always time chunks when health telemetry is on (speed comparisons).
      const bool time_chunk = measured || telemetry;
      std::chrono::steady_clock::time_point sample_start;
      if (time_chunk) {
        torch::cuda::synchronize(device_index);
        sample_start = std::chrono::steady_clock::now();
        if (profile && measured) {
          cudaProfilerStart();
          nvtxRangePushA("native_training_chunk");
        }
      }

      {
        ProfileRange rollout_range(profile && measured, "native/rollout");
        for (int step = 0; step < chunk_length; ++step) {
          torch::NoGradGuard no_grad;
          std::pair<std::vector<torch::Tensor>, torch::Tensor> output;
          CategoricalResult sampled;
          torch::Tensor actions;
          {
            ProfileRange policy_range(profile && measured,
                                      "native/rollout_policy");
            torch::Tensor policy_obs =
                obs_float(stack, channels_last, /*to_bf16=*/bf16);
            output = policy_forward(model, policy_obs, scalars, compute_scope,
                                    fp32_value_head);
            // One categorical pass: sample, force burn-in noops, gather
            // logprob.
            sampled = categorical(output.first, nullptr, &noop, &burnin);
            actions = sampled.actions;
          }

          obs_buffer.index_put_({step}, stack);
          scalar_buffer.index_put_({step}, scalars);
          action_buffer.index_put_({step}, actions);
          logprob_buffer.index_put_({step}, sampled.logprob);
          value_buffer.index_put_({step}, output.second.to(torch::kFloat32));
          valid_buffer.index_put_({step}, ~burnin);

          {
            ProfileRange env_range(profile && measured, "native/rollout_env");
            env.step(actions_to_rows(actions, device), kRepeat);
          }
          torch::Tensor decision_reward;
          {
            ProfileRange reward_range(profile && measured,
                                      "native/rollout_reward");
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
          // Sparse burn-in fill (matches Python). Full-tensor where/repeat every
          // step was a measured regression (~+11ms/chunk).
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
        ProfileRange gae_range(profile && measured, "native/gae");
        torch::NoGradGuard no_grad;
        std::pair<std::vector<torch::Tensor>, torch::Tensor> output;
        output = policy_forward(
            model, obs_float(stack, channels_last, /*to_bf16=*/bf16), scalars,
            compute_scope, fp32_value_head);
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
        ProfileRange ppo_range(profile && measured, "native/ppo");
        auto selected = torch::nonzero(valid_buffer.flatten()).squeeze(1);
        int64_t transitions = selected.numel();
        if (transitions > 0) {
          auto flat_obs = obs_buffer.flatten(0, 1);
          auto flat_scalars = scalar_buffer.flatten(0, 1);
          auto flat_actions = action_buffer.flatten(0, 1);
          auto flat_logprob = logprob_buffer.flatten();
          auto flat_advantages = advantages.flatten().index({selected});
          auto flat_returns = returns.flatten().index({selected});
          auto flat_old_values = value_buffer.flatten().index({selected});
          // RET_NORM: EMA mean/var of valid returns; value targets become
          // (R - mu) / (sigma + eps). Value head then predicts unit-scale
          // targets (popart-lite style targets; GAE still uses raw V).
          double ret_mu = 0.0;
          double ret_sigma = 1.0;
          if (ret_norm) {
            double batch_mean = flat_returns.mean().item<double>();
            double batch_var =
                flat_returns.var(/*unbiased=*/false).item<double>();
            if (!ret_run_init) {
              ret_run_mean = batch_mean;
              ret_run_var = std::max(batch_var, 1.0e-6);
              ret_run_init = true;
            } else {
              ret_run_mean = ret_norm_momentum * ret_run_mean +
                             (1.0 - ret_norm_momentum) * batch_mean;
              ret_run_var = ret_norm_momentum * ret_run_var +
                            (1.0 - ret_norm_momentum) * batch_var;
            }
            ret_mu = ret_run_mean;
            ret_sigma = std::sqrt(std::max(ret_run_var, 1.0e-6));
          }
          flat_advantages = (flat_advantages - flat_advantages.mean()) /
                            (flat_advantages.std() + 1.0e-8);
          double progress = std::min(1.0, ticks / learning_rate_ticks);
          double current_lr = std::max(
              learning_rate_floor,
              learning_rate * (1.0 - progress * (1.0 - learning_rate_floor /
                                                           learning_rate)));
          for (auto &group : optimizer.param_groups()) {
            static_cast<torch::optim::AdamOptions &>(group.options())
                .lr(current_lr);
          }
          for (int epoch = 0; epoch < epochs; ++epoch) {
            auto permutation =
                selected.index({torch::randperm(transitions, i64)});
            auto order = torch::searchsorted(selected, permutation);
            for (int64_t begin = 0; begin < transitions; begin += minibatch) {
              int64_t end = std::min(begin + minibatch, transitions);
              auto mb = permutation.index({Slice(begin, end)});
              auto positions = order.index({Slice(begin, end)});
              std::pair<std::vector<torch::Tensor>, torch::Tensor> output;
              CategoricalResult distribution;
              torch::Tensor loss;
              torch::Tensor policy_loss_t;
              torch::Tensor value_loss_t;
              torch::Tensor entropy_mean_t;
              torch::Tensor ratio;
              {
                ProfileRange forward_range(profile && measured,
                                           "native/ppo_forward_loss");
                output = policy_forward(
                    model,
                    obs_float(flat_obs.index({mb}), channels_last,
                              /*to_bf16=*/bf16),
                    flat_scalars.index({mb}), update_scope, fp32_value_head);
                auto actions = flat_actions.index({mb});
                distribution = categorical(output.first, &actions);
                ratio =
                    torch::exp(distribution.logprob - flat_logprob.index({mb}));
                auto mb_advantage = flat_advantages.index({positions});
                policy_loss_t = -torch::min(
                    ratio * mb_advantage,
                    ratio.clamp(1.0 - kClip, 1.0 + kClip) * mb_advantage);
                auto values_f = output.second.to(torch::kFloat32);
                auto mb_returns = flat_returns.index({positions});
                if (ret_norm) {
                  // Target-side popart-lite: value head learns unit-scale
                  // targets (R - mu) / (sigma + eps). GAE still uses raw V.
                  // See cand_c_VERDICT for early-transient note.
                  mb_returns = (mb_returns - ret_mu) / (ret_sigma + 1.0e-8);
                }
                if (value_clip) {
                  // PPO2-style clipped value loss vs rollout V_old (clip=0.2).
                  auto v_old = flat_old_values.index({positions});
                  auto v_clipped =
                      v_old + (values_f - v_old).clamp(-kClip, kClip);
                  auto unclipped = (mb_returns - values_f).square();
                  auto clipped = (mb_returns - v_clipped).square();
                  value_loss_t = 0.5 * torch::max(unclipped, clipped).mean();
                } else {
                  value_loss_t =
                      0.5 * (mb_returns - values_f).square().mean();
                }
                entropy_mean_t = distribution.entropy.mean();
                loss = policy_loss_t.mean() + value_loss_t -
                       kEntropy * entropy_mean_t;
              }
              {
                ProfileRange backward_range(profile && measured,
                                            "native/ppo_backward");
                optimizer.zero_grad();
                loss.backward();
              }
              {
                ProfileRange adam_range(profile && measured,
                                        "native/ppo_grad_adam");
                // value-head grad norm before clip_grad_norm_ mutates grads.
                double v_gn =
                    telemetry ? value_head_grad_norm(model) : 0.0;
                // clip_grad_norm_ returns total norm BEFORE clipping.
                double grad_norm = torch::nn::utils::clip_grad_norm_(
                    model->parameters(), kGradClip);
                if (telemetry) {
                  loss_sum += loss.detach().item<double>();
                  policy_loss_sum += policy_loss_t.mean().detach().item<double>();
                  value_loss_sum += value_loss_t.detach().item<double>();
                  entropy_sum += entropy_mean_t.detach().item<double>();
                  // Schulman approx KL: 0.5 * mean((log_ratio)^2)
                  auto log_ratio =
                      distribution.logprob - flat_logprob.index({mb});
                  approx_kl_sum +=
                      (0.5 * log_ratio.square().mean()).item<double>();
                  clip_frac_sum +=
                      (ratio - 1.0)
                          .abs()
                          .gt(kClip)
                          .to(torch::kFloat32)
                          .mean()
                          .item<double>();
                  value_grad_norm_sum += v_gn;
                  grad_norm_sum += grad_norm;
                  grad_norm_max = std::max(grad_norm_max, grad_norm);
                  if (grad_norm > kGradClip)
                    ++grad_clip_hits;
                  ++update_count;
                }
                optimizer.step();
              }
            }
          }
        }
      }

      double chunk_wall_ms = 0.0;
      if (time_chunk) {
        torch::cuda::synchronize(device_index);
        chunk_wall_ms = std::chrono::duration<double, std::milli>(
                            std::chrono::steady_clock::now() - sample_start)
                            .count();
      }

      if (telemetry) {
        double reward_mean = reward_buffer.mean().item<double>();
        double loss_mean = update_count > 0 ? loss_sum / update_count : 0.0;
        double grad_norm_mean =
            update_count > 0 ? grad_norm_sum / update_count : 0.0;
        double policy_loss_mean =
            update_count > 0 ? policy_loss_sum / update_count : 0.0;
        double value_loss_mean =
            update_count > 0 ? value_loss_sum / update_count : 0.0;
        double entropy_mean =
            update_count > 0 ? entropy_sum / update_count : 0.0;
        double approx_kl_mean =
            update_count > 0 ? approx_kl_sum / update_count : 0.0;
        double clip_frac_mean =
            update_count > 0 ? clip_frac_sum / update_count : 0.0;
        double value_grad_norm_mean =
            update_count > 0 ? value_grad_norm_sum / update_count : 0.0;
        double grad_clip_frac =
            update_count > 0
                ? static_cast<double>(grad_clip_hits) /
                      static_cast<double>(update_count)
                : 0.0;
        auto params_now = flattened_parameters(model);
        double update_norm =
            (params_now - parameters_before).norm().item<double>();
        double param_norm = params_now.norm().item<double>();
        // Rollout V / returns over valid transitions only.
        auto valid_vals = value_buffer.masked_select(valid_buffer);
        auto valid_rets = returns.masked_select(valid_buffer);
        double v_min = valid_vals.numel() > 0
                           ? valid_vals.min().item<double>()
                           : 0.0;
        double v_max = valid_vals.numel() > 0
                           ? valid_vals.max().item<double>()
                           : 0.0;
        double v_mean = valid_vals.numel() > 0
                            ? valid_vals.mean().item<double>()
                            : 0.0;
        double ret_min = valid_rets.numel() > 0
                             ? valid_rets.min().item<double>()
                             : 0.0;
        double ret_max = valid_rets.numel() > 0
                             ? valid_rets.max().item<double>()
                             : 0.0;
        int available_cells = 0;
        for (const auto &row : curriculum.available) {
          available_cells += std::count(row.begin(), row.end(), true);
        }
        double t0_success =
            trailing_t0.empty()
                ? std::numeric_limits<double>::quiet_NaN()
                : std::accumulate(trailing_t0.begin(), trailing_t0.end(), 0.0) /
                      trailing_t0.size();
        auto allocator =
            c10::cuda::CUDACachingAllocator::getDeviceStats(device_index);
        constexpr size_t aggregate =
            static_cast<size_t>(c10::CachingAllocator::StatType::AGGREGATE);
        constexpr double gib = 1024.0 * 1024.0 * 1024.0;
        double allocated_gib =
            allocator.allocated_bytes[aggregate].current / gib;
        double peak_allocated_gib =
            allocator.allocated_bytes[aggregate].peak / gib;
        bool finite = std::isfinite(reward_mean) && std::isfinite(loss_mean) &&
                      std::isfinite(grad_norm_mean) &&
                      std::isfinite(grad_norm_max) &&
                      std::isfinite(update_norm) && update_norm > 0.0 &&
                      std::isfinite(v_mean) && std::isfinite(policy_loss_mean) &&
                      std::isfinite(value_loss_mean);
        std::cout << "NATIVE_SMOKE chunk=" << (chunk + 1) << " ticks=" << ticks
                  << " reward_mean=" << reward_mean
                  << " loss_mean=" << loss_mean
                  << " policy_loss=" << policy_loss_mean
                  << " value_loss=" << value_loss_mean
                  << " entropy=" << entropy_mean
                  << " approx_kl=" << approx_kl_mean
                  << " clip_frac=" << clip_frac_mean
                  << " grad_norm_pre=" << grad_norm_mean
                  << " grad_clip_frac=" << grad_clip_frac
                  << " value_grad_norm=" << value_grad_norm_mean
                  << " param_norm=" << param_norm << " v_min=" << v_min
                  << " v_max=" << v_max << " v_mean=" << v_mean
                  << " ret_min=" << ret_min << " ret_max=" << ret_max
                  << " wall_ms=" << chunk_wall_ms
                  << " update_norm=" << update_norm
                  << " updates=" << update_count << " episodes=" << episodes
                  << " reached=";
        for (size_t stage = 0; stage < milestone_hist.size(); ++stage) {
          if (stage != 0)
            std::cout << '/';
          std::cout << milestone_hist[stage];
        }
        std::cout << " t0_success=" << t0_success
                  << " available_cells=" << available_cells
                  << " allocated_gib=" << allocated_gib
                  << " peak_allocated_gib=" << peak_allocated_gib
                  << " finite=" << finite << std::endl;
        if (telemetry_json) {
          telemetry_json << std::setprecision(9) << '{' << "\"chunk\":"
                         << (chunk + 1) << ",\"ticks\":" << ticks
                         << ",\"wall_ms\":" << chunk_wall_ms
                         << ",\"reward_mean\":" << reward_mean
                         << ",\"policy_loss\":" << policy_loss_mean
                         << ",\"value_loss\":" << value_loss_mean
                         << ",\"entropy\":" << entropy_mean
                         << ",\"approx_kl\":" << approx_kl_mean
                         << ",\"clip_frac\":" << clip_frac_mean
                         << ",\"grad_norm_pre\":" << grad_norm_mean
                         << ",\"grad_norm_max\":" << grad_norm_max
                         << ",\"grad_clip_frac\":" << grad_clip_frac
                         << ",\"value_grad_norm\":" << value_grad_norm_mean
                         << ",\"param_norm\":" << param_norm
                         << ",\"update_norm\":" << update_norm
                         << ",\"v_min\":" << v_min << ",\"v_max\":" << v_max
                         << ",\"v_mean\":" << v_mean << ",\"ret_min\":" << ret_min
                         << ",\"ret_max\":" << ret_max
                         << ",\"available_cells\":" << available_cells
                         << ",\"episodes\":" << episodes
                         << ",\"updates\":" << update_count
                         << ",\"finite\":" << (finite ? 1 : 0) << "}\n";
          telemetry_json.flush();
        }
        if (curve_output) {
          double wall = std::chrono::duration<double>(
                            std::chrono::steady_clock::now() - process_start)
                            .count();
          curve_output << (chunk + 1) << ',' << ticks << ',' << wall << ','
                       << reward_mean << ',' << loss_mean << ','
                       << grad_norm_mean << ',' << grad_norm_max << ','
                       << update_norm << ',' << episodes << ',' << t0_success
                       << ',' << available_cells << ',' << allocated_gib << ','
                       << peak_allocated_gib << ',' << finite << '\n';
          curve_output.flush();
        }
        if (!finite) {
          throw std::runtime_error("non-finite or stalled native PPO update");
        }
      }

      if (measured) {
        double sample_ticks =
            static_cast<double>(count) * chunk_length * kRepeat;
        std::cout << "NATIVE_BENCH chunk=" << (chunk - warmup_chunks)
                  << " wall_ms=" << chunk_wall_ms
                  << " env_ticks_per_s="
                  << sample_ticks / (chunk_wall_ms / 1000.0) << std::endl;
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

    const char *checkpoint = std::getenv("NATIVE_CHECKPOINT");
    if (checkpoint != nullptr) {
      save_native_checkpoint(model, checkpoint);
      std::cout << "NATIVE_CHECKPOINT path=" << checkpoint << std::endl;
    }
    return 0;
  } catch (const std::exception &error) {
    std::cerr << "ppo_native: " << error.what() << std::endl;
    return 1;
  }
}
