/* blaze_metal.mm - correctness-first hybrid Blaze backend for Apple Metal.
 *
 * Simulation stays on the CPU in the existing double-precision Blaze core.
 * Only the pointer-free, float32 semantic camera is dispatched to Metal,
 * one thread per ray. All pools consumed by either side are persistent
 * MTLStorageModeShared buffers; a step performs no Metal allocations and
 * finalization runs only after the camera command buffer has completed. */
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include <dispatch/dispatch.h>
#include <dlfcn.h>

#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>

#include "blaze_core.h"
#include "blaze_metal.h"
#include "blaze_metal_shared.h"

#define BLAZE_MAX_SNAPS 128
#define BLAZE_ACT_HEADS 13

using BmClock = std::chrono::steady_clock;

struct MetalVec {
    int n = 0;
    McSinTable st = {};
    Blaze *envs = nullptr;
    int *assign = nullptr;
    CuSnapshot snaps[BLAZE_MAX_SNAPS] = {};
    int nsnaps = 0;
    int rnx = 0, rny = 0, rnz = 0;
    long rvol = 0;
    u16 *cells_pool = nullptr, *camcells_pool = nullptr, *cam_pool = nullptr;
    u8 *dep_pool = nullptr, *edg_pool = nullptr;
    Chunk *window_pool = nullptr;
    CuCand *cand_pool = nullptr;
    int *cont_pool = nullptr;
    McAABB *blocks = nullptr;
    unsigned long long *ops_pool = nullptr;
    CRRecipe recipes[CRF_NRECIPES] = {};
    int nrecipes = 0;
    double atk_gate = 0.0;
    int success_item = 263;

    id<MTLDevice> device = nil;
    id<MTLCommandQueue> queue = nil;
    id<MTLLibrary> library = nil;
    id<MTLComputePipelineState> camera_pipeline = nil;

    id<MTLBuffer> envs_b = nil, assign_b = nil;
    id<MTLBuffer> cells_b = nil, camcells_b = nil;
    id<MTLBuffer> cam_b = nil, dep_b = nil, edg_b = nil;
    id<MTLBuffer> window_b = nil, cand_b = nil, cont_b = nil, blocks_b = nil;
    id<MTLBuffer> ops_b = nil, desc_b = nil, params_b = nil, sin_b = nil;
    id<MTLBuffer> scal_b = nil, rew_b = nil, done_b = nil, pose_b = nil;
    id<MTLBuffer> status_b = nil;

    float *scal = nullptr, *rew = nullptr, *pose = nullptr;
    u8 *done = nullptr;
    int *status = nullptr;
    BmCameraDesc *desc = nullptr;
    BmCameraParams *params = nullptr;

    uint64_t recommended = 0;
    uint64_t budget = 0;
    uint64_t metal_bytes = 0;
    uint64_t allocation_count = 0;
    double last_tick_ms = 0.0;
    double last_camera_ms = 0.0;
    char error[768] = {};
};

static thread_local char g_create_error[768];

static double bm_ms(BmClock::time_point a, BmClock::time_point b) {
    return std::chrono::duration<double, std::milli>(b - a).count();
}

static void bm_set_error(MetalVec *v, const char *fmt, ...) {
    char *dst = v ? v->error : g_create_error;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(dst, 768, fmt, ap);
    va_end(ap);
}

static void bm_promote_error(const MetalVec *v) {
    if (!v) return;
    snprintf(g_create_error, sizeof g_create_error, "%s", v->error);
}

static bool bm_mul(size_t a, size_t b, size_t *out) {
    if (a && b > std::numeric_limits<size_t>::max() / a) return false;
    *out = a * b;
    return true;
}

static bool bm_add_u64(uint64_t a, uint64_t b, uint64_t *out) {
    if (b > std::numeric_limits<uint64_t>::max() - a) return false;
    *out = a + b;
    return true;
}

static id<MTLBuffer> bm_buffer(MetalVec *v, size_t bytes, NSString *label) {
    uint64_t total;
    if (!v || bytes == 0) {
        bm_set_error(v, "invalid zero-byte Metal allocation for %s",
                     label.UTF8String ?: "buffer");
        return nil;
    }
    if ((uint64_t)bytes > (uint64_t)v->device.maxBufferLength) {
        bm_set_error(v, "%s needs %.2f MiB, above device maxBufferLength %.2f MiB",
                     label.UTF8String, (double)bytes / 1048576.0,
                     (double)v->device.maxBufferLength / 1048576.0);
        return nil;
    }
    if (!bm_add_u64(v->metal_bytes, (uint64_t)bytes, &total) ||
        total > v->budget) {
        uint64_t max_n = total && v->n > 0
            ? (uint64_t)((long double)v->n * (long double)v->budget /
                         (long double)total)
            : 0;
        bm_set_error(v,
                     "%s: required %.2f MiB of Metal shared buffers for N=%d, above budget %.2f MiB (approximate max N=%llu at this layout); reduce N_ENVS or raise BLAZE_METAL_MEMORY_LIMIT_MB",
                     label.UTF8String, (double)total / 1048576.0, v->n,
                     (double)v->budget / 1048576.0,
                     (unsigned long long)max_n);
        return nil;
    }
    id<MTLBuffer> b = [v->device newBufferWithLength:bytes
                                            options:MTLResourceStorageModeShared];
    if (!b) {
        bm_set_error(v, "newBuffer failed for %s (%.2f MiB)", label.UTF8String,
                     (double)bytes / 1048576.0);
        return nil;
    }
    b.label = label;
    v->metal_bytes = total;
    v->allocation_count++;
    return b;
}

static NSString *bm_read_text(NSString *path, MetalVec *v) {
    NSError *err = nil;
    NSString *s = [NSString stringWithContentsOfFile:path
                                            encoding:NSUTF8StringEncoding
                                               error:&err];
    if (!s)
        bm_set_error(v, "cannot read Metal source %s: %s", path.UTF8String,
                     err.localizedDescription.UTF8String);
    return s;
}

static NSString *bm_sibling_path(const char *name) {
    Dl_info info;
    if (dladdr((const void *)&bm_sibling_path, &info) == 0 || !info.dli_fname)
        return nil;
    NSString *dylib = [NSString stringWithUTF8String:info.dli_fname];
    return [[dylib stringByDeletingLastPathComponent]
        stringByAppendingPathComponent:[NSString stringWithUTF8String:name]];
}

static bool bm_compile_camera(MetalVec *v) {
    const char *source_env = getenv("BLAZE_METAL_SOURCE");
    const char *shared_env = getenv("BLAZE_METAL_SHARED");
    NSString *source_path = source_env ? [NSString stringWithUTF8String:source_env]
                                       : bm_sibling_path("blaze_metal.metal");
    NSString *shared_path = shared_env ? [NSString stringWithUTF8String:shared_env]
                                       : bm_sibling_path("blaze_metal_shared.h");
    if (!source_path || !shared_path) {
        bm_set_error(v, "cannot resolve blaze_metal.metal next to the dylib");
        return false;
    }
    NSString *shared = bm_read_text(shared_path, v);
    if (!shared) return false;
    NSString *body = bm_read_text(source_path, v);
    if (!body) return false;
    NSString *source = [NSString stringWithFormat:@"%@\n%@", shared, body];

    MTLCompileOptions *options = [MTLCompileOptions new];
    options.fastMathEnabled = NO;
    if (@available(macOS 15.0, *)) {
        options.mathMode = MTLMathModeSafe;
        options.mathFloatingPointFunctions = MTLMathFloatingPointFunctionsPrecise;
    }
    NSError *err = nil;
    v->library = [v->device newLibraryWithSource:source options:options error:&err];
    if (!v->library) {
        bm_set_error(v, "runtime Metal compilation failed: %s",
                     err.localizedDescription.UTF8String ?: "unknown error");
        return false;
    }
    id<MTLFunction> fn = [v->library newFunctionWithName:@"blaze_camera"];
    if (!fn) {
        bm_set_error(v, "runtime Metal library has no blaze_camera kernel");
        return false;
    }
    v->camera_pipeline =
        [v->device newComputePipelineStateWithFunction:fn error:&err];
    if (!v->camera_pipeline) {
        bm_set_error(v, "cannot create blaze_camera pipeline: %s",
                     err.localizedDescription.UTF8String ?: "unknown error");
        return false;
    }
    return true;
}

static void bm_parallel(size_t n, void (^body)(size_t)) {
    if (n <= 1) {
        if (n) body(0);
        return;
    }
    dispatch_apply(n,
                   dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0),
                   body);
}

static uint64_t bm_budget(id<MTLDevice> dev) {
    const char *s = getenv("BLAZE_METAL_MEMORY_LIMIT_MB");
    if (s && *s) {
        char *end = nullptr;
        unsigned long long mb = strtoull(s, &end, 10);
        if (end != s && *end == '\0' && mb > 0 &&
            mb <= std::numeric_limits<uint64_t>::max() / 1048576ull)
            return (uint64_t)mb * 1048576ull;
    }
    uint64_t rec = dev.recommendedMaxWorkingSetSize;
    if (!rec) rec = (uint64_t)[NSProcessInfo processInfo].physicalMemory;
    return rec / 100u * 45u;
}

static bool bm_alloc_fixed(MetalVec *v) {
    size_t nb;
#define BM_ALLOC(field, type, count, label)                                      \
    do {                                                                          \
        if (!bm_mul((size_t)(count), sizeof(type), &nb)) {                        \
            bm_set_error(v, "size overflow allocating %s", label);               \
            return false;                                                         \
        }                                                                         \
        v->field = bm_buffer(v, nb, @label);                                       \
        if (!v->field) return false;                                               \
    } while (0)
    BM_ALLOC(envs_b, Blaze, v->n, "envs");
    BM_ALLOC(assign_b, int, v->n, "snapshot assignments");
    BM_ALLOC(cam_b, u16, (size_t)v->n * CU_NPIX, "camera ids");
    BM_ALLOC(dep_b, u8, (size_t)v->n * CU_NPIX, "camera depth");
    BM_ALLOC(edg_b, u8, (size_t)v->n * CU_NPIX, "camera edges");
    BM_ALLOC(window_b, Chunk, (size_t)v->n * PSV_NCHUNKS, "physics windows");
    BM_ALLOC(cand_b, CuCand, (size_t)v->n * CU_COAL_CAND, "coal candidates");
    BM_ALLOC(cont_b, int, (size_t)v->n * BLAZE_SNAP_MAX_CONT * 3,
             "container candidates");
    BM_ALLOC(blocks_b, McAABB, (size_t)v->n * PSV_MAX_BLOCKS, "AABB scratch");
    if (getenv("BLAZE_OP_TRACE") && atoi(getenv("BLAZE_OP_TRACE"))) {
        BM_ALLOC(ops_b, unsigned long long, (size_t)v->n * CU_OP_N,
                 "op trace");
    }
    BM_ALLOC(desc_b, BmCameraDesc, v->n, "camera descriptors");
    BM_ALLOC(params_b, BmCameraParams, 1, "camera parameters");
    BM_ALLOC(sin_b, float, MC_SIN_TABLE_LEN, "sine table");
    BM_ALLOC(scal_b, float, (size_t)v->n * 6, "scalar observations");
    BM_ALLOC(rew_b, float, v->n, "rewards");
    BM_ALLOC(done_b, u8, v->n, "done flags");
    BM_ALLOC(pose_b, float, (size_t)v->n * 5, "poses");
    BM_ALLOC(status_b, int, (size_t)v->n * CU_STATUS_K, "status observations");
#undef BM_ALLOC

    v->envs = (Blaze *)v->envs_b.contents;
    v->assign = (int *)v->assign_b.contents;
    v->cam_pool = (u16 *)v->cam_b.contents;
    v->dep_pool = (u8 *)v->dep_b.contents;
    v->edg_pool = (u8 *)v->edg_b.contents;
    v->window_pool = (Chunk *)v->window_b.contents;
    v->cand_pool = (CuCand *)v->cand_b.contents;
    v->cont_pool = (int *)v->cont_b.contents;
    v->blocks = (McAABB *)v->blocks_b.contents;
    v->ops_pool = v->ops_b ? (unsigned long long *)v->ops_b.contents : nullptr;
    v->desc = (BmCameraDesc *)v->desc_b.contents;
    v->params = (BmCameraParams *)v->params_b.contents;
    v->scal = (float *)v->scal_b.contents;
    v->rew = (float *)v->rew_b.contents;
    v->done = (u8 *)v->done_b.contents;
    v->pose = (float *)v->pose_b.contents;
    v->status = (int *)v->status_b.contents;

    memset(v->envs, 0, (size_t)v->n * sizeof *v->envs);
    memset(v->assign, 0xff, (size_t)v->n * sizeof *v->assign);
    memset(v->cam_pool, 0, (size_t)v->n * CU_NPIX * sizeof *v->cam_pool);
    memset(v->dep_pool, 255, (size_t)v->n * CU_NPIX);
    memset(v->edg_pool, 0, (size_t)v->n * CU_NPIX);
    if (v->ops_pool)
        memset(v->ops_pool, 0,
               (size_t)v->n * CU_OP_N * sizeof *v->ops_pool);
    memcpy(v->sin_b.contents, v->st.sin_table, sizeof v->st.sin_table);
    v->params->n_envs = (BmU32)v->n;
    v->params->npix = CU_NPIX;
    v->params->reserved0 = v->params->reserved1 = 0;

    for (int i = 0; i < v->n; ++i) {
        Blaze *e = &v->envs[i];
        e->cam = v->cam_pool + (size_t)i * CU_NPIX;
        e->dep = v->dep_pool + (size_t)i * CU_NPIX;
        e->edg = v->edg_pool + (size_t)i * CU_NPIX;
        e->window = v->window_pool + (size_t)i * PSV_NCHUNKS;
        e->coal_cand = v->cand_pool + (size_t)i * CU_COAL_CAND;
        e->cont = v->cont_pool + (size_t)i * BLAZE_SNAP_MAX_CONT * 3;
        e->ops = v->ops_pool ? v->ops_pool + (size_t)i * CU_OP_N : nullptr;
    }
    return true;
}

static bool bm_alloc_regions(MetalVec *v, int nx, int ny, int nz) {
    size_t vol, count, bytes;
    if (!bm_mul((size_t)nx, (size_t)ny, &vol) ||
        !bm_mul(vol, (size_t)nz, &vol) ||
        vol > (size_t)std::numeric_limits<long>::max() ||
        !bm_mul(vol, (size_t)v->n, &count) ||
        !bm_mul(count, sizeof(u16), &bytes)) {
        bm_set_error(v, "region pool size overflow (%dx%dx%d x %d envs)",
                     nx, ny, nz, v->n);
        return false;
    }
    v->cells_b = bm_buffer(v, bytes, @"packed region cells");
    if (!v->cells_b) return false;
    v->camcells_b = bm_buffer(v, bytes, @"camera region ids");
    if (!v->camcells_b) {
        v->metal_bytes -= bytes;
        v->allocation_count--;
        v->cells_b = nil;
        return false;
    }
    v->rnx = nx; v->rny = ny; v->rnz = nz; v->rvol = (long)vol;
    v->cells_pool = (u16 *)v->cells_b.contents;
    v->camcells_pool = (u16 *)v->camcells_b.contents;
    for (int i = 0; i < v->n; ++i) {
        v->envs[i].cells = v->cells_pool + (size_t)i * vol;
        v->envs[i].cam_cells = v->camcells_pool + (size_t)i * vol;
    }
    return true;
}

static void bm_fill_desc(MetalVec *v, int selected) {
    for (int i = 0; i < v->n; ++i) {
        Blaze *e = &v->envs[i];
        BmCameraDesc *r = &v->desc[i];
        memset(r, 0, sizeof *r);
        r->cell_offset = (BmU64)((size_t)i * (size_t)v->rvol);
        r->x0 = e->rx0; r->y0 = e->ry0; r->z0 = e->rz0;
        r->nx = e->rnx; r->ny = e->rny; r->nz = e->rnz;
        r->ex = (float)(e->pl.ent.posX + (double)e->ox);
        r->ey = (float)(e->pl.ent.posY + PSV_EYE_HEIGHT);
        r->ez = (float)(e->pl.ent.posZ + (double)e->oz);
        r->yaw = e->pl.yaw; r->pitch = e->pl.pitch;
        r->active = selected >= 0 ? (BmU32)(i == selected)
                                  : (BmU32)(e->dec_cam_fresh != 0);
    }
}

static bool bm_render(MetalVec *v, int selected) {
    if (!v->camcells_b || v->rvol <= 0) {
        bm_set_error(v, "camera requested before a snapshot was loaded");
        return false;
    }
    bm_fill_desc(v, selected);
    bool any = false;
    for (int i = 0; i < v->n; ++i) any |= v->desc[i].active != 0;
    if (!any) { v->last_camera_ms = 0.0; return true; }
    BmClock::time_point t0 = BmClock::now();
    @autoreleasepool {
        id<MTLCommandBuffer> cb = [v->queue commandBuffer];
        if (!cb) {
            bm_set_error(v, "cannot create Metal command buffer");
            return false;
        }
        cb.label = @"Blaze camera";
        id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
        if (!enc) {
            bm_set_error(v, "cannot create Metal compute encoder");
            return false;
        }
        [enc setComputePipelineState:v->camera_pipeline];
        [enc setBuffer:v->desc_b offset:0 atIndex:0];
        [enc setBuffer:v->camcells_b offset:0 atIndex:1];
        [enc setBuffer:v->sin_b offset:0 atIndex:2];
        [enc setBuffer:v->cam_b offset:0 atIndex:3];
        [enc setBuffer:v->dep_b offset:0 atIndex:4];
        [enc setBuffer:v->edg_b offset:0 atIndex:5];
        [enc setBuffer:v->params_b offset:0 atIndex:6];
        NSUInteger total = (NSUInteger)v->n * CU_NPIX;
        NSUInteger width = v->camera_pipeline.maxTotalThreadsPerThreadgroup;
        /* Five 32-wide SIMD groups gives a deliberate partial final
         * threadgroup for the 2304-ray frame, exercising the gid tail guard
         * for every batch size instead of relying on a never-taken check. */
        if (width > 160) width = 160;
        if (width == 0) width = 1;
        NSUInteger groups = (total + width - 1) / width;
        [enc dispatchThreadgroups:MTLSizeMake(groups, 1, 1)
             threadsPerThreadgroup:MTLSizeMake(width, 1, 1)];
        [enc endEncoding];
        [cb commit];
        [cb waitUntilCompleted];
        if (cb.status != MTLCommandBufferStatusCompleted) {
            bm_set_error(v, "Metal camera command failed: %s",
                         cb.error.localizedDescription.UTF8String ?: "unknown error");
            return false;
        }
    }
    v->last_camera_ms = bm_ms(t0, BmClock::now());
    return true;
}

static void bm_reset_one(MetalVec *v, int i) {
    const CuSnapshot *s = &v->snaps[v->assign[i]];
    blaze_reset_from_snapshot(&v->envs[i], &s->head, s->items, s->cells,
                              s->coal, (int)s->ncoal, s->xy_off,
                              s->cont, s->ncont, v->success_item);
}

static void bm_copy_outputs(MetalVec *v, unsigned short *cam,
                            unsigned char *depth, unsigned char *edge,
                            float *scal, float *rew, unsigned char *done,
                            float *pose, int *status) {
    if (cam && cam != v->cam_pool)
        memcpy(cam, v->cam_pool, (size_t)v->n * CU_NPIX * sizeof *cam);
    if (depth && depth != v->dep_pool)
        memcpy(depth, v->dep_pool, (size_t)v->n * CU_NPIX);
    if (edge && edge != v->edg_pool)
        memcpy(edge, v->edg_pool, (size_t)v->n * CU_NPIX);
    if (scal && scal != v->scal)
        memcpy(scal, v->scal, (size_t)v->n * 6 * sizeof *scal);
    if (rew && rew != v->rew)
        memcpy(rew, v->rew, (size_t)v->n * sizeof *rew);
    if (done && done != v->done)
        memcpy(done, v->done, (size_t)v->n);
    if (pose && pose != v->pose)
        memcpy(pose, v->pose, (size_t)v->n * 5 * sizeof *pose);
    if (status && status != v->status)
        memcpy(status, v->status, (size_t)v->n * CU_STATUS_K * sizeof *status);
}

extern "C" {

int blaze_metal_available(int device, char *err, int err_cap) {
    @autoreleasepool {
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
        if (device < 0 || device >= (int)devices.count) {
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap, "Metal device %d unavailable (%lu devices)",
                         device, (unsigned long)devices.count);
            return 0;
        }
        return 1;
    }
}

void *blaze_create(int device, int n) {
    g_create_error[0] = '\0';
    if (n <= 0) {
        bm_set_error(nullptr, "n_envs must be positive (got %d)", n);
        return nullptr;
    }
    @autoreleasepool {
        NSArray<id<MTLDevice>> *devices = MTLCopyAllDevices();
        if (device < 0 || device >= (int)devices.count) {
            bm_set_error(nullptr, "Metal device %d unavailable (%lu devices)",
                         device, (unsigned long)devices.count);
            return nullptr;
        }
        MetalVec *v = new (std::nothrow) MetalVec();
        if (!v) {
            bm_set_error(nullptr, "cannot allocate Metal backend handle");
            return nullptr;
        }
        v->n = n;
        v->device = devices[(NSUInteger)device];
        v->recommended = v->device.recommendedMaxWorkingSetSize;
        v->budget = bm_budget(v->device);
        mc_sin_table_init(&v->st);
        v->nrecipes = crf_build(v->recipes);
        v->queue = [v->device newCommandQueue];
        if (!v->queue) bm_set_error(v, "cannot create Metal command queue");
        if (!v->queue || !bm_compile_camera(v) || !bm_alloc_fixed(v)) {
            bm_promote_error(v);
            delete v;
            return nullptr;
        }
        return v;
    }
}

void blaze_destroy(void *vh) {
    MetalVec *v = (MetalVec *)vh;
    if (!v) return;
    for (int i = 0; i < v->nsnaps; ++i) blaze_snapshot_free(&v->snaps[i]);
    delete v;
}

int blaze_load_snapshots(void *vh, const char *const *paths, int count,
                         char *err, int err_cap) {
    MetalVec *v = (MetalVec *)vh;
    if (!v || !paths || count < 0 || v->nsnaps + count > BLAZE_MAX_SNAPS)
        return -1;
    for (int i = 0; i < count; ++i) {
        CuSnapshot *s = &v->snaps[v->nsnaps];
        if (!blaze_snapshot_load(paths[i], s, err, err_cap)) return -1;
        const RlSnapHead *h = &s->head;
        if (h->rny > CU_RNY_MAX) {
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap, "region rny %d > %d: %s",
                         h->rny, CU_RNY_MAX, paths[i]);
            blaze_snapshot_free(s);
            return -1;
        }
        if (v->rvol == 0) {
            if (!bm_alloc_regions(v, h->rnx, h->rny, h->rnz)) {
                if (err && err_cap > 0)
                    snprintf(err, (size_t)err_cap, "%s", v->error);
                blaze_snapshot_free(s);
                return -1;
            }
        } else if (h->rnx != v->rnx || h->rny != v->rny ||
                   h->rnz != v->rnz) {
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap,
                         "region dims %dx%dx%d != pool %dx%dx%d: %s",
                         h->rnx, h->rny, h->rnz, v->rnx, v->rny, v->rnz,
                         paths[i]);
            blaze_snapshot_free(s);
            return -1;
        }
        v->nsnaps++;
    }
    return v->nsnaps;
}

int blaze_snapshot_has_liquid(void *vh, int snap) {
    MetalVec *v = (MetalVec *)vh;
    if (!v || snap < 0 || snap >= v->nsnaps) return -1;
    return v->snaps[snap].has_liquid;
}

int blaze_set_reward_gate(void *vh, double dist_gate) {
    MetalVec *v = (MetalVec *)vh;
    if (!v) return -1;
    v->atk_gate = dist_gate;
    return 0;
}

int blaze_set_success_item(void *vh, int item) {
    MetalVec *v = (MetalVec *)vh;
    if (!v || item < 0) return -1;
    v->success_item = item;
    return 0;
}

int blaze_assign(void *vh, const int *snap_idx) {
    MetalVec *v = (MetalVec *)vh;
    if (!v || !snap_idx) return -1;
    for (int i = 0; i < v->n; ++i)
        if (snap_idx[i] < 0 || snap_idx[i] >= v->nsnaps) return -1;
    memcpy(v->assign, snap_idx, (size_t)v->n * sizeof *v->assign);
    return 0;
}

int blaze_reset(void *vh, const unsigned char *mask) {
    MetalVec *v = (MetalVec *)vh;
    if (!v) return -1;
    if (v->rvol == 0) {
        bm_set_error(v, "blaze_reset requires at least one loaded snapshot");
        return -1;
    }
    for (int i = 0; i < v->n; ++i)
        if ((!mask || mask[i]) && v->assign[i] < 0) return -1;
    bm_parallel((size_t)v->n, ^(size_t i) {
        if (!mask || mask[i]) bm_reset_one(v, (int)i);
    });
    return 0;
}

int blaze_step_full(void *vh, const double *actions, int repeat,
                    unsigned short *cam, unsigned char *depth,
                    unsigned char *edge, float *scal, float *rew,
                    unsigned char *done, float *pose, int *status) {
    MetalVec *v = (MetalVec *)vh;
    if (!v || !actions || repeat < 1) return -1;
    if (v->rvol == 0) {
        bm_set_error(v, "blaze_step requires at least one loaded snapshot");
        return -1;
    }
    for (int i = 0; i < v->n; ++i) {
        if (v->envs[i].rvol <= 0) {
            bm_set_error(v, "blaze_step requires reset env %d", i);
            return -1;
        }
        if (!blaze_action_valid(&actions[(size_t)i * BLAZE_ACT_HEADS])) {
            bm_set_error(v, "invalid action row %d", i);
            return -1;
        }
    }
    v->error[0] = '\0';
    BmClock::time_point t0 = BmClock::now();
    bm_parallel((size_t)v->n, ^(size_t i) {
        Blaze *e = &v->envs[i];
        blaze_decision_ticks(e, &v->st, &actions[i * BLAZE_ACT_HEADS], repeat,
                             v->blocks + i * PSV_MAX_BLOCKS, 0, v->atk_gate,
                             v->recipes, v->nrecipes);
    });
    BmClock::time_point t1 = BmClock::now();
    if (!bm_render(v, -1)) return -1;
    BmClock::time_point t2 = BmClock::now();
    bm_parallel((size_t)v->n, ^(size_t i) {
        Blaze *e = &v->envs[i];
        blaze_decision_finalize(e, &v->st, v->scal + i * 6,
                                v->rew + i, v->done + i, v->pose + i * 5,
                                v->atk_gate);
        blaze_fill_status(e, v->status + i * CU_STATUS_K);
    });
    BmClock::time_point t3 = BmClock::now();
    v->last_tick_ms = bm_ms(t0, t1) + bm_ms(t2, t3);
    bm_copy_outputs(v, cam, depth, edge, scal, rew, done, pose, status);
    return 0;
}

int blaze_step(void *vh, const double *actions, int repeat,
               unsigned short *cam, unsigned char *depth,
               unsigned char *edge, float *scal, float *rew,
               unsigned char *done, float *pose) {
    return blaze_step_full(vh, actions, repeat, cam, depth, edge, scal, rew,
                           done, pose, nullptr);
}

int blaze_capture(void *vh, int env, int slot) {
    MetalVec *v = (MetalVec *)vh;
    Blaze *e;
    const CuSnapshot *src;
    CuSnapshot next = {}, old = {};
    size_t cell_bytes, coal_elems, coal_bytes, xy_cells, xy_bytes;
    size_t cont_elems, cont_bytes;
    int append;
    if (!v || env < 0 || env >= v->n || slot < 0 ||
        slot >= BLAZE_MAX_SNAPS || slot > v->nsnaps || v->rvol == 0 ||
        v->assign[env] < 0)
        return -1;
    e = &v->envs[env];
    if (e->nore < 0 || e->nore > v->rvol ||
        e->n_cont > BLAZE_SNAP_MAX_CONT)
        return -1;
    src = &v->snaps[v->assign[env]];
    (void)blaze_capture_head(e, &next.head, next.items);
    if (!bm_mul((size_t)v->rvol, sizeof *next.cells, &cell_bytes))
        goto fail;
    next.cells = (u16 *)malloc(cell_bytes);
    if (!next.cells) goto fail;
    memcpy(next.cells, e->cells, cell_bytes);

    next.ncoal = (unsigned)e->nore;
    if (e->nore) {
        if (!bm_mul((size_t)e->nore, 3, &coal_elems) ||
            !bm_mul(coal_elems, sizeof *next.coal, &coal_bytes))
            goto fail;
        next.coal = (int *)malloc(coal_bytes);
        if (!next.coal) goto fail;
        memcpy(next.coal, e->ore, coal_bytes);
    }

    if (src->xy_off) {
        if (!bm_mul((size_t)v->rnx, (size_t)v->rny, &xy_cells) ||
            xy_cells == std::numeric_limits<size_t>::max() ||
            !bm_mul(xy_cells + 1, sizeof *next.xy_off, &xy_bytes))
            goto fail;
        next.xy_off = (int *)malloc(xy_bytes);
        if (!next.xy_off) goto fail;
        memcpy(next.xy_off, src->xy_off, xy_bytes);
    }

    if (!bm_mul((size_t)BLAZE_SNAP_MAX_CONT, 3, &cont_elems) ||
        !bm_mul(cont_elems, sizeof *next.cont, &cont_bytes))
        goto fail;
    next.cont = (int *)malloc(cont_bytes);
    if (!next.cont) goto fail;
    next.ncont = e->n_cont;
    if (e->n_cont > 0)
        memcpy(next.cont, e->cont,
               (size_t)e->n_cont * 3 * sizeof *next.cont);
    next.has_liquid = src->has_liquid;

    append = slot == v->nsnaps;
    old = v->snaps[slot];
    v->snaps[slot] = next;
    if (append) v->nsnaps++;
    for (int i = 0; i < v->n; ++i) {
        if (old.coal && v->envs[i].ore == old.coal) {
            v->envs[i].ore = v->snaps[slot].coal;
            v->envs[i].nore = (int)v->snaps[slot].ncoal;
        }
        if (old.xy_off && v->envs[i].ore_xy == old.xy_off)
            v->envs[i].ore_xy = v->snaps[slot].xy_off;
    }
    blaze_snapshot_free(&old);
    return 0;

fail:
    blaze_snapshot_free(&next);
    return -1;
}

int blaze_obs_size(void) { return (int)sizeof(CuBinObs); }

int blaze_emit(void *vh, int env, int want_cam, void *out) {
    MetalVec *v = (MetalVec *)vh;
    if (!v || env < 0 || env >= v->n || !out || v->envs[env].rvol <= 0) {
        if (v) bm_set_error(v, "blaze_emit requires a reset env");
        return -1;
    }
    if (want_cam && !bm_render(v, env)) return -1;
    blaze_emit_bolr(&v->envs[env], &v->st, (CuBinObs *)out, 0);
    return 0;
}

static int bm_tick_one(MetalVec *v, int env, const double a[13]) {
    CuAction act;
    memset(&act, 0, sizeof act);
    act.forward = (float)a[0]; act.strafe = (float)a[1];
    act.dyaw = (float)a[2]; act.dpitch = (float)a[3];
    act.jump = (int)a[4]; act.sneak = (int)a[5];
    act.sprint = (int)a[6]; act.attack = (int)a[7]; act.use = (int)a[8];
    act.hotbar_sel = (int)a[9];
    if ((int)a[10] >= 0)
        (void)blaze_do_craft(&v->envs[env], (int)a[10], v->recipes,
                             v->nrecipes);
    if ((int)a[11]) (void)blaze_do_interact(&v->envs[env]);
    if ((int)a[12]) (void)blaze_do_smelt(&v->envs[env]);
    blaze_runtime_tick(&v->envs[env], &v->st, act,
                       v->blocks + (size_t)env * PSV_MAX_BLOCKS);
    return 0;
}

int blaze_tick_raw(void *vh, int env, const double a[13], int want_cam,
                   void *out) {
    MetalVec *v = (MetalVec *)vh;
    if (!v || env < -1 || env >= v->n || !blaze_action_valid(a)) {
        if (v) bm_set_error(v, "invalid raw action row");
        return -1;
    }
    if (env == -1) {
        for (int i = 0; i < v->n; ++i) {
            if (v->envs[i].rvol <= 0) {
                bm_set_error(v, "blaze_tick_raw requires reset env %d", i);
                return -1;
            }
        }
        bm_parallel((size_t)v->n,
                    ^(size_t i) { (void)bm_tick_one(v, (int)i, a); });
        return 0;
    }
    if (v->envs[env].rvol <= 0) {
        bm_set_error(v, "blaze_tick_raw requires a reset env");
        return -1;
    }
    (void)bm_tick_one(v, env, a);
    if (out) {
        if (want_cam && !bm_render(v, env)) return -1;
        blaze_emit_bolr(&v->envs[env], &v->st, (CuBinObs *)out, 0);
    }
    return 0;
}

int blaze_op_count(void) { return CU_OP_N; }

int blaze_op_trace(void *vh, unsigned long long *out) {
    MetalVec *v = (MetalVec *)vh;
    if (!v || !out || !v->ops_pool) return -1;
    memcpy(out, v->ops_pool,
           (size_t)v->n * CU_OP_N * sizeof *v->ops_pool);
    return 0;
}

int blaze_debug_state(void *vh, int env, double *out, int cap) {
    MetalVec *v = (MetalVec *)vh;
    if (!v || env < 0 || env >= v->n || !out || cap < 21) return -1;
    return blaze_debug_fill(&v->envs[env], out);
}

const char *blaze_backend_name(void *h) { return h ? "metal-hybrid" : nullptr; }

const char *blaze_last_error(void *h) {
    MetalVec *v = (MetalVec *)h;
    return v ? v->error : g_create_error;
}

const char *blaze_last_create_error(void) { return g_create_error; }

int blaze_get_backend_info(void *h, BlazeBackendInfo *out) {
    MetalVec *v = (MetalVec *)h;
    if (!v || !out) return -1;
    memset(out, 0, sizeof *out);
    out->version = 1;
    out->backend = 2;
    out->n_envs = (uint32_t)v->n;
    out->n_snapshots = (uint32_t)v->nsnaps;
    out->recommended_working_set = v->recommended;
    out->memory_budget = v->budget;
    out->metal_buffer_bytes = v->metal_bytes;
    out->host_snapshot_bytes = (uint64_t)v->nsnaps * sizeof(CuSnapshot);
    for (int i = 0; i < v->nsnaps; ++i) {
        const CuSnapshot *s = &v->snaps[i];
        out->host_snapshot_bytes += (uint64_t)v->rvol * sizeof(u16);
        out->host_snapshot_bytes += (uint64_t)s->ncoal * 3 * sizeof(int);
        if (s->xy_off)
            out->host_snapshot_bytes +=
                ((uint64_t)v->rnx * v->rny + 1) * sizeof(int);
        if (s->cont)
            out->host_snapshot_bytes +=
                (uint64_t)BLAZE_SNAP_MAX_CONT * 3 * sizeof(int);
    }
    out->max_buffer_length = v->device.maxBufferLength;
    out->allocation_count = v->allocation_count;
    out->last_tick_ms = v->last_tick_ms;
    out->last_camera_ms = v->last_camera_ms;
    snprintf(out->device_name, sizeof out->device_name, "%s",
             v->device.name.UTF8String ?: "Metal");
    return 0;
}

void *blaze_output_ptr(void *h, int kind) {
    MetalVec *v = (MetalVec *)h;
    if (!v) return nullptr;
    switch (kind) {
        case BLAZE_OUT_CAM: return v->cam_pool;
        case BLAZE_OUT_DEPTH: return v->dep_pool;
        case BLAZE_OUT_EDGE: return v->edg_pool;
        case BLAZE_OUT_SCAL: return v->scal;
        case BLAZE_OUT_REWARD: return v->rew;
        case BLAZE_OUT_DONE: return v->done;
        case BLAZE_OUT_POSE: return v->pose;
        case BLAZE_OUT_STATUS: return v->status;
        default: return nullptr;
    }
}

size_t blaze_output_bytes(void *h, int kind) {
    MetalVec *v = (MetalVec *)h;
    if (!v) return 0;
    switch (kind) {
        case BLAZE_OUT_CAM: return (size_t)v->n * CU_NPIX * sizeof(u16);
        case BLAZE_OUT_DEPTH: return (size_t)v->n * CU_NPIX;
        case BLAZE_OUT_EDGE: return (size_t)v->n * CU_NPIX;
        case BLAZE_OUT_SCAL: return (size_t)v->n * 6 * sizeof(float);
        case BLAZE_OUT_REWARD: return (size_t)v->n * sizeof(float);
        case BLAZE_OUT_DONE: return (size_t)v->n;
        case BLAZE_OUT_POSE: return (size_t)v->n * 5 * sizeof(float);
        case BLAZE_OUT_STATUS: return (size_t)v->n * CU_STATUS_K * sizeof(int);
        default: return 0;
    }
}

} /* extern "C" */
