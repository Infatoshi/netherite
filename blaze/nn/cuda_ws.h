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
 * this drops the old contents. Call only outside a PPO step.
 *
 * It frees the old block before it allocates the smaller one. The shrink runs
 * when VRAM is nearly full, so allocating first would defeat it.
 *
 * Return 0: the arena is exactly need bytes.
 * Return -1: the smaller allocation failed. It then tries to allocate the old
 * size back. If that succeeds the arena is the old size and is usable. If that
 * also fails the arena is empty (ptr == NULL, bytes == 0) and no further use
 * is safe. The caller cannot tell the two apart, so it MUST treat -1 as fatal
 * and stop the run. */
int nn_ws_shrink(NnWsArena *a, size_t need);

void nn_ws_free(NnWsArena *a);

#ifdef __cplusplus
}
#endif
