/* Grow-only device workspace. Grow only outside a PPO step. */
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct NnWsArena {
  void *ptr;
  size_t bytes;
  int device;
} NnWsArena;

/* cudaMalloc a new buffer if need > bytes. Copies old contents. */
int nn_ws_ensure(NnWsArena *a, size_t need);

/* Re-allocate smaller if need < bytes. Workspace is scratch between ops, so
 * this drops the old contents. Call only outside a PPO step. On failure it
 * restores the old size and returns -1; it never leaves a smaller arena. */
int nn_ws_shrink(NnWsArena *a, size_t need);

void nn_ws_free(NnWsArena *a);

#ifdef __cplusplus
}
#endif
