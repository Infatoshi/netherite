/* blaze_metal_obs_host.m - ObjC/Metal host for blaze_metal_obs.metal k_obs.
 *
 * Style mirror of magma/metal/raster_metal_host.m (shared buffers, alloc-once,
 * metallib path search, -ffp-contract=off on the host float path). Exposes the
 * C API in blaze_metal_obs.h for the Python gate (verify_metal_obs.py).
 *
 * Build (Darwin only; from repo root / via magma/Makefile blaze_metal_obs):
 *   xcrun -sdk macosx metal -fno-fast-math -ffp-contract=off \
 *       blaze/env/blaze_metal_obs.metal -o blaze/env/blaze_metal_obs.air
 *   xcrun -sdk macosx metallib blaze/env/blaze_metal_obs.air \
 *       -o blaze/env/blaze_metal_obs.metallib
 *   cc -O2 -ffp-contract=off -fPIC -fobjc-arc -shared \
 *       -Iblaze/core -Iblaze/env \
 *       blaze/env/blaze_metal_obs_host.m \
 *       -framework Metal -framework Foundation -lm \
 *       -o blaze/env/blaze_metal_obs.so
 */
#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <mach-o/dyld.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "blaze_metal_obs.h"
#include "mc_math.h"

/* Must match BlazeMetalReg / BlazeMetalPose in blaze_metal_obs.metal. */
typedef struct {
    int x0, y0, z0, nx, ny, nz;
} BlazeMetalReg;
typedef struct {
    float ex, ey, ez, yaw, pitch;
} BlazeMetalPose;

_Static_assert(sizeof(BlazeMetalReg) == 24, "BlazeMetalReg layout");
_Static_assert(sizeof(BlazeMetalPose) == 20, "BlazeMetalPose layout");
_Static_assert(sizeof(McSinTable) == MC_SIN_TABLE_LEN * sizeof(float),
               "McSinTable layout");

struct BlazeMetalObs {
    id<MTLDevice>               dev;
    id<MTLLibrary>              lib;
    id<MTLCommandQueue>         queue;
    id<MTLComputePipelineState> pso;
    id<MTLBuffer>               cells;   /* max_cells * u16 */
    id<MTLBuffer>               st;      /* McSinTable */
    id<MTLBuffer>               reg;     /* BlazeMetalReg */
    id<MTLBuffer>               pose;    /* BlazeMetalPose */
    id<MTLBuffer>               cam;     /* OC_NPIX u16 */
    id<MTLBuffer>               depth;   /* OC_NPIX u8 */
    id<MTLBuffer>               edge;    /* OC_NPIX u8 */
    int                         max_cells;
};

static void bm_err(const char *what, NSError *e) {
    fprintf(stderr, "blaze Metal obs error (%s): %s\n", what,
            e ? e.localizedDescription.UTF8String : "unknown");
}

static id<MTLLibrary> bm_load_lib(id<MTLDevice> dev, const char *path_arg) {
    char cand[6][1024];
    int ncand = 0;
    /* path_arg is the only explicit override (was BLAZE_METAL_OBS_METALLIB). */
    if (path_arg && *path_arg) {
        snprintf(cand[ncand++], sizeof cand[0], "%s", path_arg);
    }
    {
        char exedir[1024];
        uint32_t sz = (uint32_t)sizeof exedir;
        if (_NSGetExecutablePath(exedir, &sz) == 0) {
            char *slash = strrchr(exedir, '/');
            if (slash) *slash = 0;
            else snprintf(exedir, sizeof exedir, ".");
            snprintf(cand[ncand++], sizeof cand[0],
                     "%s/blaze_metal_obs.metallib", exedir);
            snprintf(cand[ncand++], sizeof cand[0],
                     "%s/../blaze/env/blaze_metal_obs.metallib", exedir);
        }
    }
    /* Common layout: cwd is repo root or blaze/env. */
    snprintf(cand[ncand++], sizeof cand[0],
             "blaze/env/blaze_metal_obs.metallib");
    snprintf(cand[ncand++], sizeof cand[0],
             "blaze_metal_obs.metallib");

    id<MTLLibrary> lib = nil;
    NSError *e = nil;
    for (int i = 0; i < ncand && !lib; ++i) {
        if (access(cand[i], R_OK) != 0) continue;
        lib = [dev newLibraryWithURL:[NSURL fileURLWithPath:
                                         [NSString stringWithUTF8String:cand[i]]]
                               error:&e];
    }
    if (!lib) {
        fprintf(stderr, "blaze Metal obs: cannot load blaze_metal_obs.metallib "
                        "(%s); tried:\n",
                e ? e.localizedDescription.UTF8String : "no readable candidate");
        for (int i = 0; i < ncand; ++i)
            fprintf(stderr, "  %s\n", cand[i]);
        fprintf(stderr,
                "  Build: make -C magma blaze_metal_obs\n"
                "  Or pass metallib_path to blaze_metal_obs_create.\n");
    }
    return lib;
}

BlazeMetalObs *blaze_metal_obs_create(int max_cells, const char *metallib_path) {
    BlazeMetalObs *h;
    McSinTable host_st;
    NSError *e = nil;

    if (max_cells <= 0) return NULL;
    h = (BlazeMetalObs *)calloc(1, sizeof *h);
    if (!h) return NULL;
    h->max_cells = max_cells;

    h->dev = MTLCreateSystemDefaultDevice();
    if (!h->dev) {
        NSArray<id<MTLDevice>> *all = MTLCopyAllDevices();
        if (all.count > 0) h->dev = all[0];
    }
    if (!h->dev) {
        fprintf(stderr, "blaze Metal obs: no Metal device\n");
        free(h);
        return NULL;
    }
    h->lib = bm_load_lib(h->dev, metallib_path);
    if (!h->lib) { free(h); return NULL; }

    id<MTLFunction> fn =
        [h->lib newFunctionWithName:@"k_obs"];
    if (!fn) {
        fprintf(stderr, "blaze Metal obs: kernel k_obs missing from metallib\n");
        free(h);
        return NULL;
    }
    h->pso = [h->dev newComputePipelineStateWithFunction:fn error:&e];
    if (!h->pso) {
        bm_err("k_obs PSO", e);
        free(h);
        return NULL;
    }
    h->queue = [h->dev newCommandQueue];
    if (!h->queue) {
        fprintf(stderr, "blaze Metal obs: newCommandQueue failed\n");
        free(h);
        return NULL;
    }

    MTLResourceOptions opt = MTLResourceStorageModeShared;
    h->cells = [h->dev newBufferWithLength:(NSUInteger)max_cells * sizeof(uint16_t)
                                   options:opt];
    h->st = [h->dev newBufferWithLength:sizeof(McSinTable) options:opt];
    h->reg = [h->dev newBufferWithLength:sizeof(BlazeMetalReg) options:opt];
    h->pose = [h->dev newBufferWithLength:sizeof(BlazeMetalPose) options:opt];
    h->cam = [h->dev newBufferWithLength:BLAZE_METAL_OBS_NPIX * sizeof(uint16_t)
                                 options:opt];
    h->depth = [h->dev newBufferWithLength:BLAZE_METAL_OBS_NPIX options:opt];
    h->edge = [h->dev newBufferWithLength:BLAZE_METAL_OBS_NPIX options:opt];
    if (!h->cells || !h->st || !h->reg || !h->pose || !h->cam || !h->depth ||
        !h->edge) {
        fprintf(stderr, "blaze Metal obs: buffer alloc failed (max_cells=%d)\n",
                max_cells);
        blaze_metal_obs_destroy(h);
        return NULL;
    }

    /* Same host-side table fill as blaze_cpu / CUDA path (libm sin -> float). */
    mc_sin_table_init(&host_st);
    memcpy(h->st.contents, &host_st, sizeof host_st);
    return h;
}

void blaze_metal_obs_destroy(BlazeMetalObs *h) {
    if (!h) return;
    /* ARC releases ObjC members when the owning C struct is freed; nil them
     * explicitly so teardown order is obvious under -fobjc-arc. */
    h->cells = nil;
    h->st = nil;
    h->reg = nil;
    h->pose = nil;
    h->cam = nil;
    h->depth = nil;
    h->edge = nil;
    h->pso = nil;
    h->queue = nil;
    h->lib = nil;
    h->dev = nil;
    free(h);
}

int blaze_metal_obs_render(BlazeMetalObs *h,
                           const uint16_t *cells,
                           int x0, int y0, int z0, int nx, int ny, int nz,
                           double ex, double ey, double ez,
                           float yaw_deg, float pitch_deg,
                           uint16_t *cam_out,
                           uint8_t *depth_out,
                           uint8_t *edge_out) {
    long vol;
    BlazeMetalReg reg;
    BlazeMetalPose pose;
    id<MTLCommandBuffer> cb;
    id<MTLComputeCommandEncoder> enc;

    if (!h || !cells || !cam_out || !depth_out || !edge_out) return -1;
    if (nx <= 0 || ny <= 0 || nz <= 0) return -1;
    vol = (long)nx * ny * nz;
    if (vol > h->max_cells) {
        fprintf(stderr, "blaze Metal obs: region vol %ld > max_cells %d\n",
                vol, h->max_cells);
        return -1;
    }

    /* Upload region (shared buffer: plain CPU memcpy). */
    memcpy(h->cells.contents, cells, (size_t)vol * sizeof(uint16_t));

    reg.x0 = x0; reg.y0 = y0; reg.z0 = z0;
    reg.nx = nx; reg.ny = ny; reg.nz = nz;
    memcpy(h->reg.contents, &reg, sizeof reg);

    /* Same double->float narrowing as oc_pixel entry (obs_camera.h:116). */
    pose.ex = (float)ex;
    pose.ey = (float)ey;
    pose.ez = (float)ez;
    pose.yaw = yaw_deg;
    pose.pitch = pitch_deg;
    memcpy(h->pose.contents, &pose, sizeof pose);

    cb = [h->queue commandBuffer];
    if (!cb) return -1;
    enc = [cb computeCommandEncoder];
    if (!enc) return -1;
    [enc setComputePipelineState:h->pso];
    [enc setBuffer:h->cells offset:0 atIndex:0];
    [enc setBuffer:h->st    offset:0 atIndex:1];
    [enc setBuffer:h->reg   offset:0 atIndex:2];
    [enc setBuffer:h->pose  offset:0 atIndex:3];
    [enc setBuffer:h->cam   offset:0 atIndex:4];
    [enc setBuffer:h->depth offset:0 atIndex:5];
    [enc setBuffer:h->edge  offset:0 atIndex:6];

    {
        NSUInteger tpg = h->pso.maxTotalThreadsPerThreadgroup;
        if (tpg > 256) tpg = 256;
        if (tpg < 1) tpg = 1;
        MTLSize grid = MTLSizeMake(BLAZE_METAL_OBS_NPIX, 1, 1);
        MTLSize tg = MTLSizeMake(tpg, 1, 1);
        [enc dispatchThreads:grid threadsPerThreadgroup:tg];
    }
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    if (cb.error) {
        bm_err("k_obs commit", cb.error);
        return -1;
    }

    memcpy(cam_out, h->cam.contents, BLAZE_METAL_OBS_NPIX * sizeof(uint16_t));
    memcpy(depth_out, h->depth.contents, BLAZE_METAL_OBS_NPIX);
    memcpy(edge_out, h->edge.contents, BLAZE_METAL_OBS_NPIX);
    return 0;
}
