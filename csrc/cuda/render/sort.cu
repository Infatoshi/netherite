/* The device renderer's device-wide scans and sorts (dev.cuh): cub,
 * in a unit of its own, compiled once (lane/cudasplit). */
#include "dev.cuh"

#include <cub/cub.cuh>

cudaError_t rs_exclusive_sum(void *temp, size_t &bytes, uint32_t *in, uint32_t *out, int n, cudaStream_t st)
{
    return cub::DeviceScan::ExclusiveSum(temp, bytes, in, out, n, st);
}

cudaError_t rs_sort_pairs(void *temp, size_t &bytes, uint32_t *keys_in, uint32_t *keys_out, uint32_t *vals_in,
                          uint32_t *vals_out, int n, int bit0, int bit1, cudaStream_t st)
{
    return cub::DeviceRadixSort::SortPairs(temp, bytes, keys_in, keys_out, vals_in, vals_out, n, bit0, bit1, st);
}
