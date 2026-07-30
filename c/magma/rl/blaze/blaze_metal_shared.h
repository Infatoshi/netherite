/* Fixed-layout values shared by the Objective-C++ Metal host and MSL.
 * Keep this file free of host pointers and platform-sized integer types. */
#ifndef BLAZE_METAL_SHARED_H
#define BLAZE_METAL_SHARED_H

#if defined(__METAL_VERSION__)
#include <metal_stdlib>
using namespace metal;
typedef uchar  BmU8;
typedef ushort BmU16;
typedef uint   BmU32;
typedef ulong  BmU64;
#else
#include <stdint.h>
typedef uint8_t  BmU8;
typedef uint16_t BmU16;
typedef uint32_t BmU32;
typedef uint64_t BmU64;
#endif

#define BM_CAM_W 64u
#define BM_CAM_H 36u
#define BM_NPIX  (BM_CAM_W * BM_CAM_H)

typedef struct BmCameraDesc {
    BmU64 cell_offset;       /* u16 element offset into the cam-cell pool */
    int x0, y0, z0;
    int nx, ny, nz;
    float ex, ey, ez;        /* already narrowed exactly like oc_pixel */
    float yaw, pitch;
    BmU32 active;
    BmU32 reserved;
} BmCameraDesc;

typedef struct BmCameraParams {
    BmU32 n_envs;
    BmU32 npix;
    BmU32 reserved0;
    BmU32 reserved1;
} BmCameraParams;

#if !defined(__METAL_VERSION__) && defined(__cplusplus)
static_assert(sizeof(BmCameraDesc) == 64, "BmCameraDesc ABI");
static_assert(sizeof(BmCameraParams) == 16, "BmCameraParams ABI");
#endif

#endif /* BLAZE_METAL_SHARED_H */
