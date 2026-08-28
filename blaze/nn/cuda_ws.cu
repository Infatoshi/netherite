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

void nn_ws_free(NnWsArena *a) {
  if (!a)
    return;
  if (a->ptr)
    cudaFree(a->ptr);
  a->ptr = nullptr;
  a->bytes = 0;
}
