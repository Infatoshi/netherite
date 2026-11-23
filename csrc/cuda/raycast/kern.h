/* cuda/raycast's two units' shared declarations: the tables as the device
 * reads them and the launches cast.cu makes for host.cu */
#ifndef NETHERITE_CUDA_RAYCAST_KERN_H
#define NETHERITE_CUDA_RAYCAST_KERN_H

#include <cuda_runtime.h>
#include <stdint.h>

#include "../../runtime/rchead.h"

/* raycast.c's struct rctab as the walk reads it, its arrays on the device */
struct rcd_tabdev {
    const uint32_t (*tex)[256];
    const uint16_t *face[2];
    const double *bounds;
    const uint8_t *kind, *tint, *emit, *cull_same;
    const float (*grass)[3], (*foliage)[3], (*water)[3];
    float end_sky[3];
    uint16_t water_spr, lava_spr;
    uint16_t dp_low[6], dp_high[6];
};

extern "C" int rc_tables_set(const struct rcd_tabdev *t);
extern "C" int rc_tables_set_fast(const struct rcd_tabdev *t);
extern "C" void rc_launch_scatter(const unsigned char *stage, size_t cap, const int2 *refs, int nref, struct rcd_col *cols,
                                  struct rcd_sec *secs, int R, cudaStream_t s);
/* a section in host memory between world.h's cell order (to_dev 0) and the device ring's (1, cast.cu SEC_XYZ) */
extern "C" void rc_sec_order(struct rcd_sec *s, int to_dev);
extern "C" void rc_launch_cast(const struct rcd_head *heads, const struct rcd_col *cols, const struct rcd_sec *secs,
                               const int *ids, int k, int R, int w, int h, int ch, uint8_t *frames, unsigned long long *seen_key,
                               int *seen_n, int *seen_xyz, struct rcd_seen *seen, int fp, cudaStream_t s);
extern "C" void rc_launch_cast_fast(const struct rcd_head *heads, const struct rcd_col *cols, const struct rcd_sec *secs,
                                    const int *ids, int k, int R, int w, int h, int ch, uint8_t *frames,
                                    unsigned long long *seen_key, int *seen_n, int *seen_xyz, struct rcd_seen *seen,
                                    int mode, cudaStream_t s);

#endif
