#pragma once

#include <cuda_runtime_api.h>

#include <cstddef>

int cgraph_reduction_blocks(std::size_t count);

void cgraph_zero(float *values, std::size_t count, cudaStream_t stream);

void cgraph_grad_sumsq(const float *grad, float *partials, std::size_t count,
                       cudaStream_t stream);

void cgraph_finish_grad_norm(const float *partials, std::size_t count,
                             float clip, float *scale, cudaStream_t stream);

void cgraph_adam_bias(float *step, float *step_size, float *denom_scale,
                      const float *learning_rate, float beta1, float beta2,
                      cudaStream_t stream);

void cgraph_adam_step(float *parameter, const float *grad, float *first,
                      float *second, std::size_t count, const float *grad_scale,
                      const float *step_size, const float *denom_scale,
                      float beta1, float beta2, float epsilon,
                      cudaStream_t stream);
