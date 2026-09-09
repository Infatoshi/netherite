/* Source blaze/env/blaze_core.h SHA256 925f5018c71b36e128d61c12fefcd5a2b6c84272295364d3eda3838198b56e21.
 * Exact reference span: blaze_runtime_player_phase, lines5724-5881 at c078993. */
/* Measurement probe with live outputs. Unlike a compiler microprobe that
 * discards edits, this preserves every emitted CuEdit and count, as well as
 * all mutations of Blaze. It is not a complete simulation tick by itself.
 * Source: blaze_runtime_player_phase's blaze_player_tick call. */
#include "../../blaze/env/blaze_cuda_int.h"

extern "C" __global__ void measure_pure_player(Blaze *envs, int n,
    const McSinTable *st, const CuAction *actions, McAABB *blocks,
    BlazePlayerContinuation *ctx, const int *exec) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || !(exec[i] & 2)) return;
    CuEdit edits[CU_MAX_EDITS];
    int count = 0;
    blaze_player_tick(envs + i, st, actions[i], edits, &count, CU_MAX_EDITS,
                      blocks + (size_t)i * PSV_MAX_BLOCKS);
    ctx[i].n = count;
    for (int j = 0; j < count; ++j) ctx[i].edits[j] = edits[j];
}
