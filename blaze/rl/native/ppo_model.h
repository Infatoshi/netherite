#pragma once

#include <torch/torch.h>

#include <utility>
#include <vector>

namespace netherite::ppo {

inline constexpr int kCamH = 36;
inline constexpr int kCamW = 64;
inline constexpr int kPlanes = 9;
inline constexpr int kStack = 2;
inline constexpr int kChannels = kPlanes * kStack;
inline constexpr int kScalars = 27;
inline constexpr int kHeads = 9;
inline constexpr int kHeadSizes[kHeads] = {3, 3, 3, 2, 2, 2, 7, 2, 10};

struct ChainPolicyImpl : torch::nn::Module {
  ChainPolicyImpl()
      : conv(register_module(
            "conv",
            torch::nn::Sequential(
                torch::nn::Conv2d(
                    torch::nn::Conv2dOptions(kChannels, 32, 5).stride(2)),
                torch::nn::ReLU(),
                torch::nn::Conv2d(
                    torch::nn::Conv2dOptions(32, 64, 3).stride(2)),
                torch::nn::ReLU(), torch::nn::Flatten()))),
        fc(register_module(
            "fc", torch::nn::Sequential(torch::nn::Linear(6272 + kScalars, 256),
                                        torch::nn::ReLU()))),
        heads(register_module("heads", torch::nn::ModuleList())),
        value(register_module("value", torch::nn::Linear(256, 1))) {
    for (int size : kHeadSizes) {
      heads->push_back(torch::nn::Linear(256, size));
    }
  }

  std::pair<std::vector<torch::Tensor>, torch::Tensor>
  forward(const torch::Tensor &planes, const torch::Tensor &scalars) {
    auto hidden = fc->forward(torch::cat({conv->forward(planes), scalars}, 1));
    std::vector<torch::Tensor> logits;
    logits.reserve(kHeads);
    for (auto &module : *heads) {
      logits.push_back(module->as<torch::nn::Linear>()->forward(hidden));
    }
    return {std::move(logits), value->forward(hidden).squeeze(-1)};
  }

  torch::nn::Sequential conv{nullptr};
  torch::nn::Sequential fc{nullptr};
  torch::nn::ModuleList heads{nullptr};
  torch::nn::Linear value{nullptr};
};
TORCH_MODULE(ChainPolicy);

} // namespace netherite::ppo
