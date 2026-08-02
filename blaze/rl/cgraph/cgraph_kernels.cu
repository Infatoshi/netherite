#include "cgraph_kernels.h"

#include <cuda_runtime.h>

#include <cmath>
#include <stdexcept>
#include <string>

namespace {

constexpr int kThreads = 256;

void check(cudaError_t error, const char *operation) {
  if (error != cudaSuccess) {
    throw std::runtime_error(std::string(operation) + ": " +
                             cudaGetErrorString(error));
  }
}

__global__ void zero_kernel(float *values, std::size_t count) {
  for (std::size_t index = blockIdx.x * blockDim.x + threadIdx.x; index < count;
       index += gridDim.x * blockDim.x) {
    values[index] = 0.0F;
  }
}

__global__ void sumsq_kernel(const float *grad, float *partials,
                             std::size_t count) {
  __shared__ float shared[kThreads];
  float sum = 0.0F;
  for (std::size_t index = blockIdx.x * blockDim.x + threadIdx.x; index < count;
       index += gridDim.x * blockDim.x) {
    float value = grad[index];
    sum = fmaf(value, value, sum);
  }
  shared[threadIdx.x] = sum;
  __syncthreads();
  for (int offset = kThreads / 2; offset != 0; offset /= 2) {
    if (threadIdx.x < offset) {
      shared[threadIdx.x] += shared[threadIdx.x + offset];
    }
    __syncthreads();
  }
  if (threadIdx.x == 0) {
    partials[blockIdx.x] = shared[0];
  }
}

__global__ void finish_norm_kernel(const float *partials, std::size_t count,
                                   float clip, float *scale) {
  __shared__ float shared[kThreads];
  float sum = 0.0F;
  for (std::size_t index = threadIdx.x; index < count; index += blockDim.x) {
    sum += partials[index];
  }
  shared[threadIdx.x] = sum;
  __syncthreads();
  for (int offset = kThreads / 2; offset != 0; offset /= 2) {
    if (threadIdx.x < offset) {
      shared[threadIdx.x] += shared[threadIdx.x + offset];
    }
    __syncthreads();
  }
  if (threadIdx.x == 0) {
    float norm = sqrtf(shared[0]);
    scale[0] = norm > clip ? clip / (norm + 1.0e-6F) : 1.0F;
  }
}

__global__ void adam_bias_kernel(float *step, float *step_size,
                                 float *denom_scale, const float *lr,
                                 float beta1, float beta2) {
  if (threadIdx.x == 0) {
    float next = step[0] + 1.0F;
    step[0] = next;
    step_size[0] = lr[0] / (1.0F - powf(beta1, next));
    denom_scale[0] = rsqrtf(1.0F - powf(beta2, next));
  }
}

__global__ void adam_kernel(float *parameter, const float *grad, float *first,
                            float *second, std::size_t count,
                            const float *grad_scale, const float *step_size,
                            const float *denom_scale, float beta1, float beta2,
                            float epsilon) {
  float scale = grad_scale[0];
  float alpha = step_size[0];
  float denominator_scale = denom_scale[0];
  for (std::size_t index = blockIdx.x * blockDim.x + threadIdx.x; index < count;
       index += gridDim.x * blockDim.x) {
    float g = grad[index] * scale;
    float m = beta1 * first[index] + (1.0F - beta1) * g;
    float v = beta2 * second[index] + (1.0F - beta2) * g * g;
    first[index] = m;
    second[index] = v;
    parameter[index] -= alpha * m / (sqrtf(v) * denominator_scale + epsilon);
  }
}

} // namespace

int cgraph_reduction_blocks(std::size_t count) {
  int blocks = static_cast<int>((count + kThreads - 1) / kThreads);
  return blocks > 4096 ? 4096 : (blocks > 0 ? blocks : 1);
}

void cgraph_zero(float *values, std::size_t count, cudaStream_t stream) {
  int blocks = cgraph_reduction_blocks(count);
  zero_kernel<<<blocks, kThreads, 0, stream>>>(values, count);
  check(cudaGetLastError(), "cgraph_zero");
}

void cgraph_grad_sumsq(const float *grad, float *partials, std::size_t count,
                       cudaStream_t stream) {
  int blocks = cgraph_reduction_blocks(count);
  sumsq_kernel<<<blocks, kThreads, 0, stream>>>(grad, partials, count);
  check(cudaGetLastError(), "cgraph_grad_sumsq");
}

void cgraph_finish_grad_norm(const float *partials, std::size_t count,
                             float clip, float *scale, cudaStream_t stream) {
  finish_norm_kernel<<<1, kThreads, 0, stream>>>(partials, count, clip, scale);
  check(cudaGetLastError(), "cgraph_finish_grad_norm");
}

void cgraph_adam_bias(float *step, float *step_size, float *denom_scale,
                      const float *learning_rate, float beta1, float beta2,
                      cudaStream_t stream) {
  adam_bias_kernel<<<1, 1, 0, stream>>>(step, step_size, denom_scale,
                                        learning_rate, beta1, beta2);
  check(cudaGetLastError(), "cgraph_adam_bias");
}

void cgraph_adam_step(float *parameter, const float *grad, float *first,
                      float *second, std::size_t count, const float *grad_scale,
                      const float *step_size, const float *denom_scale,
                      float beta1, float beta2, float epsilon,
                      cudaStream_t stream) {
  int blocks = cgraph_reduction_blocks(count);
  adam_kernel<<<blocks, kThreads, 0, stream>>>(
      parameter, grad, first, second, count, grad_scale, step_size, denom_scale,
      beta1, beta2, epsilon);
  check(cudaGetLastError(), "cgraph_adam_step");
}
