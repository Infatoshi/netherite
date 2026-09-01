#include "cuda_ws.h"

#include <cuda_runtime.h>
#include <cstring>

int nn_ws_ensure(NnWsArena *a, size_t need) {
  if (!a || need == 0)
    return -1;
  if (a->ptr && a->bytes >= need)
    return 0;
  void *p = nullptr;
  if (cudaMalloc(&p, need) != cudaSuccess)
    return -1;
  if (a->ptr && a->bytes)
    cudaMemcpy(p, a->ptr, a->bytes, cudaMemcpyDeviceToDevice);
  if (a->ptr)
    cudaFree(a->ptr);
  a->ptr = p;
  a->bytes = need;
  return 0;
}

int nn_ws_shrink(NnWsArena *a, size_t need) {
  if (!a || need == 0)
    return -1;
  if (!a->ptr || a->bytes <= need)
    return 0;
  const size_t old = a->bytes;
  cudaFree(a->ptr);
  a->ptr = nullptr;
  a->bytes = 0;
  void *p = nullptr;
  if (cudaMalloc(&p, need) == cudaSuccess) {
    a->ptr = p;
    a->bytes = need;
    return 0;
  }
  /* Put the old size back so the caller keeps a usable arena. */
  if (cudaMalloc(&p, old) == cudaSuccess) {
    a->ptr = p;
    a->bytes = old;
  }
  return -1;
}

void nn_ws_free(NnWsArena *a) {
  if (!a)
    return;
  if (a->ptr)
    cudaFree(a->ptr);
  a->ptr = nullptr;
  a->bytes = 0;
}
