#pragma once

#include <dlfcn.h>
#include <torch/torch.h>

#include <cstdint>
#include <stdexcept>
#include <string>
#include <vector>

#include "ppo_model.h"

namespace netherite::ppo {

class NativeEnv {
public:
  NativeEnv(const std::string &library_path, int device, int count)
      : count_(count), device_(torch::kCUDA, device) {
    library_ = dlopen(library_path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (library_ == nullptr) {
      throw std::runtime_error(dlerror());
    }
    create_ = symbol<CreateFn>("blaze_create");
    destroy_ = symbol<DestroyFn>("blaze_destroy");
    load_ = symbol<LoadFn>("blaze_load_snapshots");
    assign_ = symbol<AssignFn>("blaze_assign");
    reset_ = symbol<ResetFn>("blaze_reset");
    step_ = symbol<StepFn>("blaze_step_full");
    success_item_ = symbol<SuccessFn>("blaze_set_success_item");
    capture_ = symbol<CaptureFn>("blaze_capture");
    handle_ = create_(device, count);
    if (handle_ == nullptr) {
      throw std::runtime_error("blaze_create failed");
    }
    auto u8 = torch::TensorOptions().dtype(torch::kUInt8).device(device_);
    auto i16 = torch::TensorOptions().dtype(torch::kInt16).device(device_);
    auto i32 = torch::TensorOptions().dtype(torch::kInt32).device(device_);
    auto f32 = torch::TensorOptions().dtype(torch::kFloat32).device(device_);
    cam = torch::zeros({count, kCamH, kCamW}, i16);
    depth = torch::zeros({count, kCamH, kCamW}, u8);
    edge = torch::zeros({count, kCamH, kCamW}, u8);
    scal = torch::zeros({count, 6}, f32);
    reward = torch::zeros({count}, f32);
    done = torch::zeros({count}, u8);
    pose = torch::zeros({count, 5}, f32);
    status = torch::zeros({count, 17}, i32);
  }

  NativeEnv(const NativeEnv &) = delete;
  NativeEnv &operator=(const NativeEnv &) = delete;

  ~NativeEnv() {
    if (handle_ != nullptr) {
      destroy_(handle_);
    }
    if (library_ != nullptr) {
      dlclose(library_);
    }
  }

  int load_snapshots(const std::vector<std::string> &paths) {
    std::vector<const char *> raw;
    raw.reserve(paths.size());
    for (const auto &path : paths)
      raw.push_back(path.c_str());
    char error[512]{};
    int result = load_(handle_, raw.data(), static_cast<int>(raw.size()), error,
                       sizeof(error));
    if (result < 0)
      throw std::runtime_error(error);
    snapshot_count_ = result;
    return result;
  }

  void set_success_item(int item) {
    if (success_item_(handle_, item) != 0) {
      throw std::runtime_error("blaze_set_success_item failed");
    }
  }

  void assign(const std::vector<int> &slots) {
    if (slots.size() != static_cast<size_t>(count_) ||
        assign_(handle_, slots.data()) != 0) {
      throw std::runtime_error("blaze_assign failed");
    }
  }

  void reset(const std::vector<uint8_t> *mask = nullptr) {
    const uint8_t *raw = mask == nullptr ? nullptr : mask->data();
    if (reset_(handle_, raw) != 0) {
      throw std::runtime_error("blaze_reset failed");
    }
  }

  void capture(int lane, int slot) {
    if (capture_(handle_, lane, slot) != 0) {
      throw std::runtime_error("blaze_capture failed");
    }
    if (slot == snapshot_count_)
      ++snapshot_count_;
  }

  void step(const torch::Tensor &actions, int repeat) {
    if (!actions.is_cuda() || actions.scalar_type() != torch::kFloat64 ||
        !actions.is_contiguous() || actions.size(0) != count_ ||
        actions.size(1) != 13) {
      throw std::runtime_error("invalid native environment action tensor");
    }
    int result = step_(handle_, actions.data_ptr<double>(), repeat,
                       cam.data_ptr(), depth.data_ptr(), edge.data_ptr(),
                       scal.data_ptr(), reward.data_ptr(), done.data_ptr(),
                       pose.data_ptr(), status.data_ptr());
    if (result != 0)
      throw std::runtime_error("blaze_step_full failed");
  }

  torch::Tensor cam;
  torch::Tensor depth;
  torch::Tensor edge;
  torch::Tensor scal;
  torch::Tensor reward;
  torch::Tensor done;
  torch::Tensor pose;
  torch::Tensor status;

private:
  template <class Function> Function symbol(const char *name) {
    void *address = dlsym(library_, name);
    if (address == nullptr)
      throw std::runtime_error(dlerror());
    return reinterpret_cast<Function>(address);
  }

  using CreateFn = void *(*)(int, int);
  using DestroyFn = void (*)(void *);
  using LoadFn = int (*)(void *, const char *const *, int, char *, int);
  using AssignFn = int (*)(void *, const int *);
  using ResetFn = int (*)(void *, const uint8_t *);
  using StepFn = int (*)(void *, const double *, int, void *, void *, void *,
                         void *, void *, void *, void *, void *);
  using SuccessFn = int (*)(void *, int);
  using CaptureFn = int (*)(void *, int, int);

  int count_;
  torch::Device device_;
  int snapshot_count_ = 0;
  void *library_ = nullptr;
  void *handle_ = nullptr;
  CreateFn create_ = nullptr;
  DestroyFn destroy_ = nullptr;
  LoadFn load_ = nullptr;
  AssignFn assign_ = nullptr;
  ResetFn reset_ = nullptr;
  StepFn step_ = nullptr;
  SuccessFn success_item_ = nullptr;
  CaptureFn capture_ = nullptr;
};

} // namespace netherite::ppo
