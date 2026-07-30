/* Optional inspection API exported by blaze_metal.dylib in addition to the
 * existing blaze CPU/CUDA C ABI. The legacy entry points remain unchanged. */
#ifndef BLAZE_METAL_H
#define BLAZE_METAL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum BlazeOutputKind {
    BLAZE_OUT_CAM = 0,
    BLAZE_OUT_DEPTH,
    BLAZE_OUT_EDGE,
    BLAZE_OUT_SCAL,
    BLAZE_OUT_REWARD,
    BLAZE_OUT_DONE,
    BLAZE_OUT_POSE,
    BLAZE_OUT_STATUS,
    BLAZE_OUT_COUNT
};

typedef struct BlazeBackendInfo {
    uint32_t version;
    uint32_t backend;                 /* 2 = Metal */
    uint32_t n_envs;
    uint32_t n_snapshots;
    uint64_t recommended_working_set;
    uint64_t memory_budget;
    uint64_t metal_buffer_bytes;
    uint64_t host_snapshot_bytes;
    uint64_t max_buffer_length;
    uint64_t allocation_count;        /* newBuffer calls; stable while stepping */
    double last_tick_ms;
    double last_camera_ms;
    char device_name[128];
} BlazeBackendInfo;

/* Device-only availability probe. Creating a handle additionally compiles the
 * runtime MSL source and therefore provides the authoritative readiness test. */
int blaze_metal_available(int device, char *err, int err_cap);
const char *blaze_backend_name(void *h);
const char *blaze_last_error(void *h);
const char *blaze_last_create_error(void);
int blaze_get_backend_info(void *h, BlazeBackendInfo *out);
void *blaze_output_ptr(void *h, int kind);
size_t blaze_output_bytes(void *h, int kind);

#ifdef __cplusplus
}
#endif
#endif /* BLAZE_METAL_H */
