/* Fable-5 policy for the CUDA trainer. Implement once. Do not special-case
 * conv1 vs conv2. A new layer or shape is a new cache key, not a branch.
 *
 * Layout: NHWC activations, KRSC filters. Host ABI and checkpoints stay
 * NCHW KCRS / CHW-flat FC; permute on upload and on save.
 * Math stage A: fp32 store, TF32 compute, fp32 accumulate. Drop determinism.
 * Conv: cuDNN graph, FWD=conv+bias+ReLU, WGRAD, DGRAD (skip layer 0).
 * Plan: heur A/B, check_support, time top K=8, keep fastest. Never list order.
 * Cache key: (op, n, C,H,W,K,R,S,pad,stride,dil,dtype,layout,fusion,cudnn,gpu).
 * Batch: bucket n to multiple of 32 if max_n<256 else 256; never exceed max_n.
 *   If n >= 0.9*max_n, run at max_n. Pad extra rows to 0.
 * Workspace: one grow-only arena, shared. Grow outside the step.
 * No pre-activation store. ReLU bwd uses y>0.
 * Conv bwd: RELU_BWD_BIAS (full grid) then WGRAD then DGRAD.
 * Dense: cublasLt only. D = out^T (features x n). Hidden RELU_AUX_BIAS.
 *   Heads+value one GEMM with BIAS. Bwd DRELU_BGRAD / BGRADB.
 * FC input = slices, not a packed 6299 buffer: NHWC y_last as [n,H*W*C] and
 *   scalars [n,S]. Permute W columns CHW->HWC at load.
 * Ban: Get_v7, cudnnFind as steady state, k_add_bias_nchw, k_relu_store_pre,
 *   k_pack_fc_in, k_unpack_fc_in_bwd, calc_bias_diff / BackwardBias,
 *   k_bias_grad_rows, ensure_batch algo re-pick, FFT-by-name ban.
 * FFT/Winograd may win a timed race; if they do with a TF32 implicit-GEMM
 * also in the list, the filter is wrong.
 */
#pragma once
