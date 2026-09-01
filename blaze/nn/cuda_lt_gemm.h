/* cublasLt dense path. D is features x n (column-major). TF32 compute.
 * See cuda_fable_contract.h. */
#pragma once

#include "cuda_ws.h"

#include <cublasLt.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NnLtGemm NnLtGemm;

NnLtGemm *nn_lt_create(cublasLtHandle_t lt, int device, int max_n,
                       NnWsArena *ws);
void nn_lt_destroy(NnLtGemm *g);
int nn_lt_prepare(NnLtGemm *g, int n);

/* Max workspace over the chosen plans. The arena is shared with the conv net,
 * which must not shrink below this. */
long long nn_lt_max_ws(const NnLtGemm *g);

/* y[out, n] = relu(W[out,k] @ x[k,n] + b[out]). aux = ReLU bitmask. */
int nn_lt_fwd_relu_bias(NnLtGemm *g, int n, int out, int k, const float *W,
                        const float *x, const float *b, float *y, void *aux);

/* y[out, n] = W[out,k] @ x[k,n] + b[out]. */
int nn_lt_fwd_bias(NnLtGemm *g, int n, int out, int k, const float *W,
                   const float *x, const float *b, float *y);

/* dx[k,n], db[out] from dy and saved y/aux. beta_dx 0 or 1. */
int nn_lt_bwd_drelu_bgrad(NnLtGemm *g, int n, int out, int k, const float *W,
                          const float *dy, const void *aux, float *dx,
                          float *db, float beta_dx);

/* dW[out,k] += dy[out,n] @ x[k,n]^T ; optional db from BGRADB. */
int nn_lt_bwd_dw_bgrad(NnLtGemm *g, int n, int out, int k, const float *dy,
                       const float *x, float *dW, float *db, float beta_dw);

/* dx[k,n] = W[out,k]^T @ dy[out,n]. beta_dx 0 or 1. */
int nn_lt_bwd_dx(NnLtGemm *g, int n, int out, int k, const float *W,
                 const float *dy, float *dx, float beta_dx);

/* Slice GEMM: W is row-major [out, lda], uses first k columns. */
int nn_lt_gemm(NnLtGemm *g, int n, int out, int k, int lda, const float *W,
               const float *x, float *y, float beta);
int nn_lt_fwd_relu_bias_ex(NnLtGemm *g, int n, int out, int k, int lda,
                           const float *W, const float *x, const float *b,
                           float *y, void *aux, float beta);
int nn_lt_drelu_bgrad(NnLtGemm *g, int n, int out, const float *dy,
                      const void *aux, float *dpre, float *db);
int nn_lt_bwd_dw_ex(NnLtGemm *g, int n, int out, int k, int lda,
                    const float *dy, const float *x, float *dW, float *db,
                    float beta_dw);
int nn_lt_bwd_dx_ex(NnLtGemm *g, int n, int out, int k, int lda,
                    const float *W, const float *dy, float *dx, float beta_dx);

#ifdef __cplusplus
}
#endif
