/* magma - Metal rasterizer host (metal/raster_metal_host.m).
 *
 * Port of cuda/raster_cuda.cu's extern "C" host layer to the Metal framework.
 * Every cr_raster_metal_* entry point mirrors the cr_raster_cuda_* function of
 * the same suffix (identical signature, identical semantics); the kernels live
 * in raster_kernels.metal, compiled offline to raster_kernels.metallib and
 * loaded at runtime from the executable's directory (MAGMA_METALLIB env
 * overrides the path). Target: Apple silicon (unified memory), macOS 13+
 * (MTLBuffer.gpuAddress). Compile this file as ObjC with ARC (-fobjc-arc) and
 * -ffp-contract=off (host float math below must match the gcc/clang CPU path).
 *
 * CUDA -> Metal mapping decisions (unified memory first, perf follow-ups noted):
 *
 *  - cudaMalloc            -> MTLBuffer, MTLResourceStorageModeShared. On Apple
 *    silicon shared buffers are CPU/GPU coherent; "H2D copies" of host data are
 *    plain CPU memcpys into buffer contents performed at enqueue time.
 *  - cudaHostRegister/pin  -> NO-OP. Pinning exists on CUDA so pageable H2D
 *    "async" copies do not stage+stall; with unified memory there is no DMA
 *    from host malloc at all (we memcpy on the CPU), so pin/unpin keep no
 *    bookkeeping. cr_raster_metal_pin/unpin are documented no-ops.
 *  - stream                -> one MTLCommandQueue; each frame's work accumulates
 *    into ONE command buffer (serial compute encoders per layer chain) that is
 *    committed at frame_end / frame_end_async. Command buffers execute in
 *    queue order; hazard tracking orders inter-encoder buffer reuse.
 *  - copy/kernel ordering  -> CUDA relies on stream order so host buffers may
 *    be memcpy'd up while earlier GPU work is still running. Here the enqueue-
 *    time CPU memcpy would race in-flight GPU reads of the SAME device buffer,
 *    so cr_raster_metal_frame_begin DRAINS the previous frame (waits the last
 *    committed command buffer) before touching d_color/d_depth. The CUDA
 *    overlap that matters - GPU tail + readback overlapping the sim ticks
 *    between frame_end_async and the next frame_begin - is preserved; the
 *    additional CUDA overlap of frame N's GPU tail with frame N+1's CPU
 *    enqueue is sacrificed for correctness. PERF FOLLOW-UP: commit per-layer
 *    command buffers and stage hazardous uploads through blits to recover it.
 *  - events / frame_end_async -> frame_end_async encodes a blit of d_color
 *    into a dedicated pend buffer (never touched by the next frame), commits
 *    the frame's command buffer and remembers it; frame_wait waits that
 *    command buffer (usually already complete) and memcpys pend -> dst_color.
 *    cudaEvent "never recorded => sync is a no-op" semantics fall out of the
 *    end_pending flag, exactly like the CUDA code's guard.
 *  - uploads_mark/uploads_wait -> NO-OPS. They exist so the CUDA host knows
 *    when its pinned source buffers were consumed by in-flight async copies;
 *    here every upload is a synchronous CPU memcpy completed before the call
 *    returns, so the host may always mutate its buffers immediately after the
 *    enqueue call - the guarantee holds trivially.
 *  - kernel launches       -> compute command encoders with the same dispatch
 *    grid (threadgroups = CUDA blocks, threadsPerThreadgroup = blockDim).
 *    A plain computeCommandEncoder is SERIAL dispatch type, which is what the
 *    per-triangle legacy path needs for CPU-order depth resolution.
 *  - device pointers in structs (CrShadeCtx.atlas/.lightmap, CrTexture.texels/
 *    .mip[]) -> MTLBuffer.gpuAddress values written into the same struct
 *    layouts; every indirectly-referenced buffer is marked resident with
 *    useResource on each encoder that shades (argument buffer tier 2).
 *
 *  Ring-slot writes (shade ctx, lightmap, gather tables, vert staging) go
 *  straight into shared buffers with no copy: the CUDA pinned-mirror +
 *  MemcpyAsync pair collapses to one CPU write. Reuse safety is the same
 *  depth-1 pipeline bound the CUDA rings rely on (a slot 16 allocations back
 *  is >= 2 frames old), plus the frame_begin drain which retires everything
 *  older than the current frame anyway.
 *
 *  SEMANTIC ASSUMPTION (same as the CUDA stream ordering guarantees, just
 *  stated explicitly because uploads here are enqueue-time memcpys): within
 *  one frame the game calls slab_sync for a slot BEFORE any render_gather /
 *  render_terrain that reads that slot (world render does syncs first), and a
 *  frame issues at most CR_SH_RING render_layer calls.
 *
 * MEASURED-ON-MAC checklist (things chosen conservatively but unverifiable
 * off-mac; grep MEASURED-ON-MAC in both metal/ files):
 *  1. metallib built with -fno-fast-math -ffp-contract=off really contains no
 *     fused MADs / approximate div-sqrt (parity gate day frame must be
 *     bit-identical to the Mac CPU build).
 *  2. precise::sin (night stars) and precise::exp (eye-in-fluid sky) pixel
 *     divergence rate vs the Mac CPU build (CUDA class: <= 0.012%/frame).
 *  3. gpuAddress pointer-chasing + useResource residency (atlas/mip/lightmap
 *     sampling) works; requires macOS 13+ and argument buffer tier 2.
 *  4. tiled PSO maxTotalThreadsPerThreadgroup >= 256 (checked at init, error
 *     printed if not); threadExecutionWidth interaction with the barrier
 *     pattern (all-256-thread participation).
 *  5. serial dispatch ordering inside one compute encoder (legacy per-tri
 *     path) resolves overlapping triangles in CPU order.
 *  6. buffer sizing at max_tris=2,000,000: d_tris + d_dense are ~672 MB each,
 *     plus d_verts ~216 MB; shared allocations succeed ([device
 *     maxBufferLength], unified memory pressure on 36 GB M4 Max).
 *  7. Apple libm sinf/cosf/tanf (host mvp + sky ctx, via math.o/sky.o) differ
 *     from glibc in last ulps: Mac-CPU vs Mac-Metal parity is unaffected
 *     (both use the same libm), but cross-platform frame hashes vs anvil may
 *     differ; do not compare hashes across OSes.
 *  8. hand-verified struct offsets are additionally guarded by the
 *     _Static_asserts below and static_assert(sizeof) in the .metal - both
 *     must compile clean on the mac toolchain.
 */

#import <Metal/Metal.h>
#import <Foundation/Foundation.h>

#include <mach-o/dyld.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "core/types.h"
#include "game/sky.h"
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#include "assets/sky_atlas.h"   /* CR_SUN_RGBA / CR_MOON_RGBA for the sky kernel */
#pragma clang diagnostic pop

/* ---- struct-layout contract with raster_kernels.metal ------------------- */
_Static_assert(sizeof(CrRgba) == 4,        "CrRgba layout");
_Static_assert(sizeof(CrVec3) == 12,       "CrVec3 layout");
_Static_assert(sizeof(CrVertex) == 36,     "CrVertex layout");
_Static_assert(sizeof(CrScreenVert) == 56, "CrScreenVert layout");
_Static_assert(sizeof(CrScreenTri) == 168, "CrScreenTri layout");
_Static_assert(offsetof(CrTexture, texels) == 8,   "CrTexture.texels");
_Static_assert(offsetof(CrTexture, mip) == 24,     "CrTexture.mip");
_Static_assert(offsetof(CrTexture, mipw) == 144,   "CrTexture.mipw");
_Static_assert(offsetof(CrTexture, miph) == 204,   "CrTexture.miph");
_Static_assert(sizeof(CrTexture) == 264,           "CrTexture layout");
_Static_assert(offsetof(CrShadeCtx, lightmap) == 48, "CrShadeCtx.lightmap");
_Static_assert(offsetof(CrShadeCtx, entity_brightness) == 92,
               "CrShadeCtx.entity_brightness");
_Static_assert(sizeof(CrShadeCtx) == 96,   "CrShadeCtx layout");
_Static_assert(sizeof(GmSkyCtx) == 92,     "GmSkyCtx layout");

/* ---- kernel parameter blocks (byte-mirrors of the *ParamsM in .metal) --- */
typedef struct {
    int W, H, minx, miny, bw, bh;
    CrScreenTri tri;
} TriParams;
typedef struct { int ntris, W, H, use_device_count; } BboxParams;
typedef struct {
    int W, H, ntris, sb1, sb2, sb3, shbase, use_device_count;
} TileParams;
typedef struct {
    int W, H;
    GmSkyCtx sc;
    float b[11];
} SkyParams;
typedef struct {
    int ntris_in, W, H, compact;
    CrMat4 mvp;
    CrVec3 campos;
} XformParams;
typedef struct { int nents, total_words, base; } GatherParams;

_Static_assert(sizeof(TriParams) == 24 + sizeof(CrScreenTri), "TriParams");
_Static_assert(sizeof(SkyParams) == 8 + sizeof(GmSkyCtx) + 44, "SkyParams");
_Static_assert(sizeof(XformParams) == 16 + 64 + 12, "XformParams");

/* Must match cpu/raster_cpu.c / raster_cuda.cu. */
#define CR_FRONT_SIGN -1.0f

/* ---- public API (cr_raster_cuda_* mirrored, s/cuda/metal/) -------------- */
void cr_raster_metal(CrFramebuffer *fb, const CrScreenTri *tris, int ntris,
                     const CrShadeCtx *sh);
void cr_raster_metal_pre(int w, int h, int max_tris);
void cr_raster_metal_atlas_dirty(void);
void cr_raster_metal_uploads_mark(void);
void cr_raster_metal_uploads_wait(void);
void cr_raster_metal_pin(void *p, size_t bytes);
void cr_raster_metal_unpin(void *p);
void cr_raster_metal_frame_begin(const CrFramebuffer *fb);
void cr_raster_metal_sky(const GmSkyCtx *sc, const float *b, int W, int H);
void cr_raster_metal_frame_end(CrFramebuffer *fb);
int  cr_raster_metal_screen_tris(void);
int  cr_raster_metal_frame_end_async(CrFramebuffer *fb, CrRgba *dst_color);
void cr_raster_metal_frame_wait(void);
void cr_raster_metal_into(CrFramebuffer *fb, const CrScreenTri *tris,
                          int ntris, const CrShadeCtx *sh);
void cr_raster_metal_render_layer(CrFramebuffer *fb, const CrVertex *verts,
                                  int nverts, const CrCamera *cam,
                                  const CrShadeCtx *sh);
int  cr_raster_metal_slab_pool(int nslots, int slab_verts);
void cr_raster_metal_slab_sync(int slot, int builds, const void *host,
                               int used_verts);
void cr_raster_metal_slabs_reset(void);
void cr_raster_metal_render_gather(CrFramebuffer *fb, const int *src_vert,
                                   const int *nvert, int nents,
                                   int total_verts, const CrCamera *cam,
                                   const CrShadeCtx *sh);
void cr_raster_metal_render_terrain(CrFramebuffer *fb, const int *src_vert,
                                    const int *nvert, int nents,
                                    const int lay_verts[4],
                                    const CrCamera *cam,
                                    const CrShadeCtx sh[4]);
void cr_raster_metal_post(void);

#define CR_ATLAS_CACHE 6
#define CR_SH_RING 16
#define CR_GR_RING 8      /* gather calls per frame (terrain merges into 1) */
/* Entries per call. Replay needs 4 layers * mesh_slots (289). The window path
 * submits per-16-block-SECTION runs to keep W16's vertical cull, so its worst
 * case is mesh_slots * GM_MESH_SECTIONS = 4624; 8192 covers it with headroom.
 * Must stay >= game/game.h GM_GATHER_MAX_ENTRIES (and its CUDA twin). */
#define CR_GR_MAX  8192
#define CR_VERT_WORDS ((int)(sizeof(CrVertex) / sizeof(uint32_t)))

/* ---- global Metal objects (statics, not struct fields: ARC-safe) -------- */
static id<MTLDevice>               g_dev;
static id<MTLLibrary>              g_lib;
static id<MTLCommandQueue>         g_queue;
static id<MTLComputePipelineState> g_pso_tri, g_pso_bbox, g_pso_tiled,
                                   g_pso_sky, g_pso_xform, g_pso_scan,
                                   g_pso_scatter, g_pso_gather;
static id<MTLCommandBuffer>        g_cmd;      /* current (uncommitted) frame cb */
static id<MTLCommandBuffer>        g_last;     /* last committed, not yet waited */
static id<MTLCommandBuffer>        g_end_cb;   /* frame_end_async's cb */

static id<MTLBuffer> g_d_color, g_d_depth, g_d_tris, g_d_dense,
                     g_d_box, g_d_tminz, g_d_batchz, g_d_xcounts, g_d_xoffsets,
                     g_d_screen_tris,
                     g_d_verts, g_d_sh, g_d_lm, g_d_pend;
static id<MTLBuffer> g_vstage[CR_SH_RING];   /* render_layer vert staging ring */
static id<MTLBuffer> g_into_tris;            /* cr_raster_metal_into tris staging */
static id<MTLBuffer> g_d_slabs, g_d_gsrc, g_d_gpfx;
static id<MTLBuffer> g_sun_buf, g_moon_buf;

/* atlas cache (mirrors CrAtlasSlot; ObjC members split into static arrays) */
static const void   *g_at_key[CR_ATLAS_CACHE];
static id<MTLBuffer> g_at_tex[CR_ATLAS_CACHE];       /* device CrTexture */
static id<MTLBuffer> g_at_texels[CR_ATLAS_CACHE];
static id<MTLBuffer> g_at_mip[CR_ATLAS_CACHE][15];
static int           g_at_nmip[CR_ATLAS_CACHE];
static int           g_n_atlas;
static int           g_atlas_host_dirty;

static struct {
    int inited;
    int W, H, max_tris;
    int sh_idx;          /* shade-ctx ring cursor (reset by sync frame_end) */
    int vs_idx;          /* vert-staging ring cursor */
    int gr_idx;          /* gather-table ring cursor */
    int frame_open;      /* 1 between frame_begin/frame_end: fb is resident */
    int screen_tris;     /* compacted screen tris after frame_end */
    int end_pending;     /* frame_end_async armed, frame_wait not yet called */
    CrRgba *end_dst;     /* frame_end_async's caller-owned readback target */
    size_t  end_npix;
    size_t  vstage_sz[CR_SH_RING];
    size_t  into_sz;
    int    *slab_builds; /* host: last-uploaded builds per slot (-1 = never) */
    int     slab_nslots, slab_cap;
} g;

/* ---- boot / command-buffer plumbing ------------------------------------- */

static void mg_err(const char *what, NSError *e) {
    fprintf(stderr, "magma Metal error (%s): %s\n", what,
            e ? e.localizedDescription.UTF8String : "unknown");
}

static id<MTLLibrary> mg_load_lib(id<MTLDevice> dev) {
    /* Candidates: $MAGMA_METALLIB wins outright; otherwise search relative to
     * the executable. The Makefile leaves the build product at
     * magma/metal/raster_kernels.metallib while the game binary sits in
     * magma/ and the parity-test binary in magma/tests/, so "next to the
     * executable" alone boot-fails BOTH real consumers - and a boot failure
     * degrades to the CPU fallback, which once made the parity gate compare
     * CPU with CPU and "pass" (caught on the first MacBook run, 2026-07-30). */
    char cand[3][1024];
    int ncand = 0;
    const char *env = getenv("MAGMA_METALLIB");
    if (env && *env) {
        snprintf(cand[0], sizeof cand[0], "%s", env);
        ncand = 1;
    } else {
        char exedir[1024];
        uint32_t sz = (uint32_t)sizeof exedir;
        if (_NSGetExecutablePath(exedir, &sz) != 0) {
            fprintf(stderr, "magma Metal: executable path too long; set "
                            "MAGMA_METALLIB to the metallib path\n");
            return nil;
        }
        char *slash = strrchr(exedir, '/');
        if (slash) *slash = 0;
        else snprintf(exedir, sizeof exedir, ".");
        snprintf(cand[0], sizeof cand[0],
                 "%s/raster_kernels.metallib", exedir);
        snprintf(cand[1], sizeof cand[1],
                 "%s/metal/raster_kernels.metallib", exedir);        /* magma/ */
        snprintf(cand[2], sizeof cand[2],
                 "%s/../metal/raster_kernels.metallib", exedir);     /* tests/ */
        ncand = 3;
    }
    id<MTLLibrary> lib = nil;
    NSError *e = nil;
    for (int i = 0; i < ncand && !lib; ++i) {
        if (access(cand[i], R_OK) != 0) continue;
        lib = [dev newLibraryWithURL:[NSURL fileURLWithPath:
                                         [NSString stringWithUTF8String:cand[i]]]
                               error:&e];
    }
    if (!lib) {
        fprintf(stderr, "magma Metal: cannot load raster_kernels.metallib "
                        "(%s); tried:\n",
                e ? e.localizedDescription.UTF8String : "no readable candidate");
        for (int i = 0; i < ncand; ++i)
            fprintf(stderr, "  %s\n", cand[i]);
        fprintf(stderr,
                "  Build it:  make -C magma metal/raster_kernels.metallib\n"
                "  Or set MAGMA_METALLIB to its path.\n");
    }
    return lib;
}

static id<MTLComputePipelineState> mg_pso(const char *name) {
    id<MTLFunction> f =
        [g_lib newFunctionWithName:[NSString stringWithUTF8String:name]];
    if (!f) {
        fprintf(stderr, "magma Metal: kernel %s missing from metallib\n", name);
        return nil;
    }
    NSError *e = nil;
    id<MTLComputePipelineState> p =
        [g_dev newComputePipelineStateWithFunction:f error:&e];
    if (!p) mg_err(name, e);
    return p;
}

/* MAGMA_METAL_REQUIRE=1 turns any boot failure fatal instead of letting the
 * caller degrade to cr_raster_cpu. The parity gate sets it so a metallib or
 * device problem can never produce a vacuous CPU-vs-CPU "pass" again (the
 * failure mode caught on the first MacBook run, 2026-07-30). */
static int mg_boot_failed(void) {
    const char *req = getenv("MAGMA_METAL_REQUIRE");
    if (req && *req == '1') {
        fprintf(stderr, "magma Metal: boot failed and MAGMA_METAL_REQUIRE=1; "
                        "refusing CPU fallback\n");
        exit(2);
    }
    return 0;
}

/* Device + library + PSOs; shared by _pre and the legacy cr_raster_metal.
 * Returns 1 on success. Idempotent; failures print once per call site. */
static int mg_boot(void) {
    if (g_queue) return 1;
    g_dev = MTLCreateSystemDefaultDevice();
    if (!g_dev) {
        NSArray<id<MTLDevice>> *all = MTLCopyAllDevices();
        if (all.count > 0) g_dev = all[0];
    }
    if (!g_dev) {
        fprintf(stderr, "magma Metal: no Metal device\n");
        return mg_boot_failed();
    }
    g_lib = mg_load_lib(g_dev);
    if (!g_lib) { g_dev = nil; return mg_boot_failed(); }
    g_pso_tri    = mg_pso("cr_raster_tri_kernel");
    g_pso_bbox   = mg_pso("cr_raster_bbox_kernel");
    g_pso_tiled  = mg_pso("cr_raster_tiled_kernel");
    g_pso_sky    = mg_pso("k_sky");
    g_pso_xform  = mg_pso("cr_transform_kernel");
    g_pso_scan   = mg_pso("cr_compact_scan_kernel");
    g_pso_scatter = mg_pso("cr_compact_scatter_kernel");
    g_pso_gather = mg_pso("cr_gather_kernel");
    if (!g_pso_tri || !g_pso_bbox || !g_pso_tiled || !g_pso_sky ||
        !g_pso_xform || !g_pso_scan || !g_pso_scatter || !g_pso_gather) {
        g_lib = nil; g_dev = nil;
        return mg_boot_failed();
    }
    if (g_pso_tiled.maxTotalThreadsPerThreadgroup < 256) {
        /* MEASURED-ON-MAC 4: tiled kernel NEEDS full 16x16 threadgroups. */
        fprintf(stderr, "magma Metal: tiled kernel maxTotalThreadsPerThreadgroup"
                        " = %lu < 256; tiled raster unusable on this device\n",
                (unsigned long)g_pso_tiled.maxTotalThreadsPerThreadgroup);
        g_lib = nil; g_dev = nil;
        return mg_boot_failed();
    }
    g_queue = [g_dev newCommandQueue];
    if (!g_queue) { g_lib = nil; g_dev = nil; return mg_boot_failed(); }
    return 1;
}

static id<MTLBuffer> mg_newbuf(size_t len, const char *what) {
    id<MTLBuffer> b = [g_dev newBufferWithLength:len
                                         options:MTLResourceStorageModeShared];
    if (!b)
        fprintf(stderr, "magma Metal: alloc failed (%s, %zu bytes)\n", what, len);
    return b;
}

/* Grow-only staging buffer helper. */
static id<MTLBuffer> mg_ensure(id<MTLBuffer> buf, size_t *cap, size_t need,
                               const char *what) {
    if (buf && *cap >= need) return buf;
    id<MTLBuffer> nb = mg_newbuf(need, what);
    if (nb) *cap = need;
    return nb ? nb : buf;
}

/* Current frame command buffer (created lazily). */
static id<MTLCommandBuffer> mg_cb(void) {
    if (!g_cmd) g_cmd = [g_queue commandBuffer];
    return g_cmd;
}

/* Commit the current command buffer (keeps it as the wait target). */
static void mg_flush(void) {
    if (g_cmd) {
        [g_cmd commit];
        g_last = g_cmd;
        g_cmd = nil;
    }
}

/* cr_cuda_check equivalent: report a failed command buffer. */
static void mg_check(id<MTLCommandBuffer> cb, const char *what) {
    if (cb && cb.status == MTLCommandBufferStatusError)
        fprintf(stderr, "magma Metal error (%s): %s\n", what,
                cb.error ? cb.error.localizedDescription.UTF8String : "unknown");
}

/* == cudaStreamSynchronize(g_gpu.stream): commit + wait everything. Command
 * buffers on one queue start in order and g_last is the newest, so waiting it
 * retires all earlier ones too. */
static void mg_sync(void) {
    mg_flush();
    if (g_last) {
        [g_last waitUntilCompleted];
        mg_check(g_last, "sync");
        g_last = nil;
    }
}

/* Mark every buffer reachable only through gpuAddress pointers (atlas structs,
 * texels, mip chains, lightmap ring) resident for this encoder. */
static void mg_use_indirect(id<MTLComputeCommandEncoder> enc) {
    for (int i = 0; i < g_n_atlas; ++i) {
        if (g_at_tex[i])    [enc useResource:g_at_tex[i]
                                       usage:MTLResourceUsageRead];
        if (g_at_texels[i]) [enc useResource:g_at_texels[i]
                                       usage:MTLResourceUsageRead];
        for (int l = 0; l < g_at_nmip[i]; ++l)
            if (g_at_mip[i][l]) [enc useResource:g_at_mip[i][l]
                                           usage:MTLResourceUsageRead];
    }
    if (g_d_lm) [enc useResource:g_d_lm usage:MTLResourceUsageRead];
}

/* ---- encode helpers (dispatch geometry == CUDA launch geometry) --------- */

static void mg_encode_bbox(id<MTLComputeCommandEncoder> enc,
                           id<MTLBuffer> trisBuf, int ntris, int W, int H,
                           int use_device_count) {
    BboxParams bp = { ntris, W, H, use_device_count };
    [enc setComputePipelineState:g_pso_bbox];
    [enc setBuffer:trisBuf offset:0 atIndex:0];
    [enc setBytes:&bp length:sizeof bp atIndex:1];
    [enc setBuffer:g_d_box offset:0 atIndex:2];
    [enc setBuffer:g_d_tminz offset:0 atIndex:3];
    [enc setBuffer:g_d_screen_tris offset:0 atIndex:4];
    [enc setBuffer:g_d_batchz offset:0 atIndex:5];
    /* 256 == CR_BBOX_THREADS == CR_TILE_N: threadgroup b owns tiled batch b. */
    [enc dispatchThreadgroups:MTLSizeMake((NSUInteger)((ntris + 255) / 256), 1, 1)
        threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
}

static void mg_encode_tiled(id<MTLComputeCommandEncoder> enc,
                            id<MTLBuffer> trisBuf, int ntris, int W, int H,
                            int shbase, int sb1, int sb2, int sb3,
                            int use_device_count) {
    TileParams tp = {
        W, H, ntris, sb1, sb2, sb3, shbase, use_device_count
    };
    [enc setComputePipelineState:g_pso_tiled];
    [enc setBuffer:g_d_color offset:0 atIndex:0];
    [enc setBuffer:g_d_depth offset:0 atIndex:1];
    [enc setBytes:&tp length:sizeof tp atIndex:2];
    [enc setBuffer:trisBuf offset:0 atIndex:3];
    [enc setBuffer:g_d_box offset:0 atIndex:4];
    [enc setBuffer:g_d_sh offset:0 atIndex:5];
    [enc setBuffer:g_d_tminz offset:0 atIndex:6];
    [enc setBuffer:g_d_screen_tris offset:0 atIndex:7];
    [enc setBuffer:g_d_batchz offset:0 atIndex:8];
    mg_use_indirect(enc);
    [enc dispatchThreadgroups:MTLSizeMake((NSUInteger)((W + 15) / 16),
                                          (NSUInteger)((H + 15) / 16), 1)
        threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
}

static void mg_encode_xform(id<MTLComputeCommandEncoder> enc,
                            id<MTLBuffer> vertsBuf, int ntris_in,
                            const CrMat4 *mvp, CrVec3 campos, int W, int H,
                            int compact) {
    XformParams xp;
    xp.ntris_in = ntris_in; xp.W = W; xp.H = H; xp.compact = compact;
    xp.mvp = *mvp; xp.campos = campos;
    [enc setComputePipelineState:g_pso_xform];
    [enc setBuffer:vertsBuf offset:0 atIndex:0];
    [enc setBytes:&xp length:sizeof xp atIndex:1];
    [enc setBuffer:g_d_tris offset:0 atIndex:2];
    [enc setBuffer:g_d_xcounts offset:0 atIndex:3];
    [enc setBuffer:g_d_screen_tris offset:0 atIndex:4];
    [enc dispatchThreadgroups:MTLSizeMake((NSUInteger)((ntris_in + 255) / 256), 1, 1)
        threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
}

static void mg_encode_compact(id<MTLComputeCommandEncoder> enc, int nxblocks) {
    [enc setComputePipelineState:g_pso_scan];
    [enc setBuffer:g_d_xcounts offset:0 atIndex:0];
    [enc setBytes:&nxblocks length:sizeof nxblocks atIndex:1];
    [enc setBuffer:g_d_xoffsets offset:0 atIndex:2];
    [enc setBuffer:g_d_screen_tris offset:0 atIndex:3];
    [enc dispatchThreadgroups:MTLSizeMake(1, 1, 1)
        threadsPerThreadgroup:MTLSizeMake(1, 1, 1)];

    [enc setComputePipelineState:g_pso_scatter];
    [enc setBuffer:g_d_tris offset:0 atIndex:0];
    [enc setBuffer:g_d_xcounts offset:0 atIndex:1];
    [enc setBuffer:g_d_xoffsets offset:0 atIndex:2];
    [enc setBytes:&nxblocks length:sizeof nxblocks atIndex:3];
    [enc setBuffer:g_d_dense offset:0 atIndex:4];
    [enc dispatchThreadgroups:MTLSizeMake((NSUInteger)(nxblocks * 2), 1, 1)
        threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
}

/* ---- shade-ctx ring (h_sh and d_sh are the SAME shared buffer) ---------- */

static CrShadeCtx *mg_sh_slot(int *slot_out) {
    if (g.sh_idx == CR_SH_RING) g.sh_idx = 0;
    int i = g.sh_idx++;
    *slot_out = i;
    return (CrShadeCtx *)g_d_sh.contents + i;
}

/* Stage the host lightmap LUT into the slot's shared ring texels and repoint
 * the ctx at their GPU address (raster_cuda.cu cr_cuda_patch_lightmap). */
static void mg_patch_lightmap(CrShadeCtx *h_sh, int slot) {
    if (!h_sh->lightmap) return;
    CrRgba *dst = (CrRgba *)g_d_lm.contents + (size_t)slot * 256;
    memcpy(dst, h_sh->lightmap, 256 * sizeof(CrRgba));
    h_sh->lightmap = (const CrRgba *)(uintptr_t)
        (g_d_lm.gpuAddress + (uint64_t)slot * 256 * sizeof(CrRgba));
}

/* ---- atlas mirror cache (raster_cuda.cu cr_cuda_sync_atlas) -------------
 * Returns the GPU address of the device CrTexture, 0 on failure/NULL atlas. */
void cr_raster_metal_atlas_dirty(void) { g_atlas_host_dirty = 1; }

static uint64_t mg_sync_atlas(const CrTexture *atlas) {
    if (!atlas) return 0;
    for (int i = 0; i < g_n_atlas; ++i)
        if (g_at_key[i] == (const void *)atlas->texels) {
            if (g_atlas_host_dirty) {
                size_t ntex = (size_t)atlas->w * (size_t)atlas->h;
                memcpy(g_at_texels[i].contents, atlas->texels,
                       ntex * sizeof(CrRgba));
                for (int l = 0; l < g_at_nmip[i]; ++l) {
                    size_t nl = (size_t)atlas->mipw[l] * (size_t)atlas->miph[l];
                    if (nl > 0 && atlas->mip[l] && g_at_mip[i][l])
                        memcpy(g_at_mip[i][l].contents, atlas->mip[l],
                               nl * sizeof(CrRgba));
                }
                g_atlas_host_dirty = 0;
            }
            return g_at_tex[i].gpuAddress;
        }

    if (g_n_atlas == CR_ATLAS_CACHE) {   /* never in practice: ~4 atlases */
        mg_sync();                       /* == cudaDeviceSynchronize */
        g_at_tex[0] = nil; g_at_texels[0] = nil;
        for (int l = 0; l < 15; ++l) g_at_mip[0][l] = nil;
        for (int i = 1; i < CR_ATLAS_CACHE; ++i) {
            g_at_key[i - 1] = g_at_key[i];
            g_at_tex[i - 1] = g_at_tex[i];
            g_at_texels[i - 1] = g_at_texels[i];
            g_at_nmip[i - 1] = g_at_nmip[i];
            for (int l = 0; l < 15; ++l) g_at_mip[i - 1][l] = g_at_mip[i][l];
        }
        int last = CR_ATLAS_CACHE - 1;
        g_at_key[last] = NULL; g_at_tex[last] = nil; g_at_texels[last] = nil;
        for (int l = 0; l < 15; ++l) g_at_mip[last][l] = nil;
        g_at_nmip[last] = 0;
        g_n_atlas--;
    }

    int s = g_n_atlas;
    size_t ntex = (size_t)atlas->w * (size_t)atlas->h;
    g_at_texels[s] = mg_newbuf(ntex * sizeof(CrRgba), "atlas texels");
    if (!g_at_texels[s]) return 0;
    memcpy(g_at_texels[s].contents, atlas->texels, ntex * sizeof(CrRgba));

    CrTexture h_tex = *atlas;
    h_tex.texels = (const CrRgba *)(uintptr_t)g_at_texels[s].gpuAddress;
    int n_mip = atlas->mip_levels;
    if (n_mip < 0) n_mip = 0;
    if (n_mip > 15) n_mip = 15;
    for (int l = 0; l < n_mip; l++) {
        size_t nl = (size_t)atlas->mipw[l] * (size_t)atlas->miph[l];
        g_at_mip[s][l] = nil;
        h_tex.mip[l] = NULL;
        if (nl > 0 && atlas->mip[l]) {
            g_at_mip[s][l] = mg_newbuf(nl * sizeof(CrRgba), "atlas mip");
            if (g_at_mip[s][l]) {
                memcpy(g_at_mip[s][l].contents, atlas->mip[l],
                       nl * sizeof(CrRgba));
                h_tex.mip[l] = (const CrRgba *)(uintptr_t)
                    g_at_mip[s][l].gpuAddress;
            }
        }
    }
    g_at_nmip[s] = n_mip;
    g_at_tex[s] = mg_newbuf(sizeof(CrTexture), "atlas CrTexture");
    if (!g_at_tex[s]) { g_at_texels[s] = nil; return 0; }
    memcpy(g_at_tex[s].contents, &h_tex, sizeof(CrTexture));
    g_at_key[s] = atlas->texels;
    g_n_atlas++;
    return g_at_tex[s].gpuAddress;
}

/* ==================== public API ========================================= */

void cr_raster_metal_pre(int w, int h, int max_tris) {
    if (g.inited) return;
    if (!mg_boot()) return;
    g.W = w; g.H = h; g.max_tris = max_tris;
    size_t npix = (size_t)w * (size_t)h;
    g_d_color = mg_newbuf(npix * sizeof(CrRgba), "d_color");
    g_d_depth = mg_newbuf(npix * sizeof(float), "d_depth");
    g_d_tris  = mg_newbuf(2 * (size_t)max_tris * sizeof(CrScreenTri), "d_tris");
    g_d_dense = mg_newbuf(2 * (size_t)max_tris * sizeof(CrScreenTri), "d_dense");
    g_d_box   = mg_newbuf(2 * (size_t)max_tris * sizeof(uint32_t), "d_box");
    g_d_tminz = mg_newbuf(2 * (size_t)max_tris * sizeof(float), "d_tminz");
    /* one slot per bbox threadgroup over the same 2*max_tris slot space. */
    g_d_batchz = mg_newbuf((size_t)((2 * max_tris + 255) / 256 + 1) * sizeof(float),
                           "d_batchz");
    g_d_xcounts = mg_newbuf((size_t)((max_tris + 255) / 256) * sizeof(int),
                            "d_xcounts");
    g_d_xoffsets = mg_newbuf((size_t)((max_tris + 255) / 256) * sizeof(int),
                             "d_xoffsets");
    g_d_screen_tris = mg_newbuf(2 * sizeof(int), "d_screen_tris");
    g_d_verts = mg_newbuf(3 * (size_t)max_tris * sizeof(CrVertex), "d_verts");
    g_d_sh    = mg_newbuf(CR_SH_RING * sizeof(CrShadeCtx), "d_sh");
    g_d_lm    = mg_newbuf(CR_SH_RING * 256 * sizeof(CrRgba), "d_lm");
    g_d_pend  = mg_newbuf(npix * sizeof(CrRgba), "d_pend");
    if (!g_d_color || !g_d_depth || !g_d_tris || !g_d_dense || !g_d_box ||
        !g_d_tminz || !g_d_batchz || !g_d_xcounts || !g_d_xoffsets ||
        !g_d_screen_tris ||
        !g_d_verts || !g_d_sh || !g_d_lm || !g_d_pend) {
        fprintf(stderr, "magma Metal: cr_raster_metal_pre allocation failed\n");
        g_d_color = g_d_depth = g_d_tris = g_d_dense = g_d_box = nil;
        g_d_tminz = g_d_batchz = g_d_xcounts = g_d_xoffsets = nil;
        g_d_screen_tris = nil;
        g_d_verts = g_d_sh = g_d_lm = g_d_pend = nil;
        return;
    }
    g.sh_idx = 0;
    g.vs_idx = 0;
    g_n_atlas = 0;
    g.inited = 1;
}

/* Unified memory: uploads are enqueue-time CPU memcpys that complete before
 * the enqueue call returns, so the "uploads done" event is always satisfied.
 * See the header block for why this is exactly the CUDA guarantee. */
void cr_raster_metal_uploads_mark(void) {}
void cr_raster_metal_uploads_wait(void) {}

/* Unified memory: no page-locking exists or is needed (host buffers are only
 * ever read by CPU memcpy, never DMA'd). Deliberate no-ops. */
void cr_raster_metal_pin(void *p, size_t bytes) { (void)p; (void)bytes; }
void cr_raster_metal_unpin(void *p) { (void)p; }

void cr_raster_metal_frame_begin(const CrFramebuffer *fb) {
    if (!g.inited) return;
    /* Pipeline drain (see header): everything committed - including the
     * previous frame's tail + pend blit - must retire before the resident
     * fb is overwritten. CUDA got this ordering from the stream for free. */
    mg_sync();
    size_t npix = (size_t)fb->w * (size_t)fb->h;
    memcpy(g_d_color.contents, fb->color, npix * sizeof(CrRgba));
    memcpy(g_d_depth.contents, fb->depth, npix * sizeof(float));
    memset(g_d_screen_tris.contents, 0, 2 * sizeof(int));
    g.frame_open = 1;
}

void cr_raster_metal_sky(const GmSkyCtx *sc, const float *b, int W, int H) {
    if (!g.inited || !g.frame_open) return;
    if (!g_sun_buf) {
        g_sun_buf  = mg_newbuf(sizeof CR_SUN_RGBA, "sun tex");
        g_moon_buf = mg_newbuf(sizeof CR_MOON_RGBA, "moon tex");
        if (!g_sun_buf || !g_moon_buf) { g_sun_buf = g_moon_buf = nil; return; }
        memcpy(g_sun_buf.contents, CR_SUN_RGBA, sizeof CR_SUN_RGBA);
        memcpy(g_moon_buf.contents, CR_MOON_RGBA, sizeof CR_MOON_RGBA);
    }
    SkyParams sp;
    sp.W = W; sp.H = H; sp.sc = *sc;
    for (int i = 0; i < 11; ++i) sp.b[i] = b[i];
    id<MTLComputeCommandEncoder> enc = [mg_cb() computeCommandEncoder];
    [enc setComputePipelineState:g_pso_sky];
    [enc setBuffer:g_d_color offset:0 atIndex:0];
    [enc setBytes:&sp length:sizeof sp atIndex:1];
    [enc setBuffer:g_sun_buf offset:0 atIndex:2];
    [enc setBuffer:g_moon_buf offset:0 atIndex:3];
    [enc dispatchThreadgroups:MTLSizeMake((NSUInteger)((W + 15) / 16),
                                          (NSUInteger)((H + 15) / 16), 1)
        threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    [enc endEncoding];
}

void cr_raster_metal_frame_end(CrFramebuffer *fb) {
    if (!g.inited || !g.frame_open) return;
    size_t npix = (size_t)fb->w * (size_t)fb->h;
    /* THE frame barrier: commit + wait every enqueued layer, then bring the
     * fb home (shared memory: readback is a CPU memcpy after completion). */
    mg_sync();
    g.screen_tris = *(int *)g_d_screen_tris.contents;
    memcpy(fb->color, g_d_color.contents, npix * sizeof(CrRgba));
    memcpy(fb->depth, g_d_depth.contents, npix * sizeof(float));
    g.sh_idx = 0;
    g.gr_idx = 0;
    g.frame_open = 0;
}

int cr_raster_metal_screen_tris(void) {
    return g.screen_tris;
}

/* Deferred frame end: blit d_color into the dedicated pend buffer (the next
 * frame never touches it), commit, remember the cb. NO wait here - the GPU
 * tail + blit overlap the sim ticks until the next rendered frame. Depth is
 * NOT read back (same as CUDA: the frames-path consumers never read it). */
int cr_raster_metal_frame_end_async(CrFramebuffer *fb, CrRgba *dst_color) {
    if (!g.inited || !g.frame_open || g.end_pending) return 0;
    size_t npix = (size_t)fb->w * (size_t)fb->h;
    id<MTLBlitCommandEncoder> blit = [mg_cb() blitCommandEncoder];
    [blit copyFromBuffer:g_d_color sourceOffset:0
                toBuffer:g_d_pend destinationOffset:0
                    size:npix * sizeof(CrRgba)];
    [blit endEncoding];
    mg_flush();
    g_end_cb = g_last;
    g.end_dst = dst_color;
    g.end_npix = npix;
    g.end_pending = 1;
    g.frame_open = 0;
    return 1;
}

void cr_raster_metal_frame_wait(void) {
    if (!g.inited || !g.end_pending) return;
    if (g_end_cb) {
        [g_end_cb waitUntilCompleted];   /* usually already complete: the next
                                            frame_begin's drain waited it */
        mg_check(g_end_cb, "frame_wait");
        g_end_cb = nil;
    }
    if (g.end_dst)
        memcpy(g.end_dst, g_d_pend.contents, g.end_npix * sizeof(CrRgba));
    /* rings are NOT reset here (mirrors CUDA): reuse is bounded by the
     * depth-1 pipeline, see mg_sh_slot / the header block. */
    g.end_pending = 0;
}

/* Matches cr_raster_cpu / cr_raster_metal signature. Alloc-once fast path. */
void cr_raster_metal_into(CrFramebuffer *fb, const CrScreenTri *tris,
                          int ntris, const CrShadeCtx *sh) {
    if (!g.inited) { cr_raster_metal(fb, tris, ntris, sh); return; }
    if (ntris <= 0) return;
    if (ntris > g.max_tris) ntris = g.max_tris; /* bounded; caps guarantee fit */

    int W = fb->w, H = fb->h;
    size_t npix = (size_t)W * (size_t)H;

    /* Standalone call may follow an un-waited async frame; drain so the
     * fb upload + tris staging below cannot race in-flight GPU reads. */
    if (!g.frame_open) mg_sync();

    uint64_t d_tex = mg_sync_atlas(sh->atlas);
    int slot;
    CrShadeCtx *h_sh = mg_sh_slot(&slot);
    *h_sh = *sh;
    h_sh->atlas = (const CrTexture *)(uintptr_t)d_tex;
    mg_patch_lightmap(h_sh, slot);

    if (!g.frame_open) {
        memcpy(g_d_color.contents, fb->color, npix * sizeof(CrRgba));
        memcpy(g_d_depth.contents, fb->depth, npix * sizeof(float));
    }
    /* tris staging: dedicated buffer (never a GPU-write target, so this
     * enqueue-time memcpy cannot alias the frame's transform outputs). */
    size_t tbytes = (size_t)ntris * sizeof(CrScreenTri);
    g_into_tris = mg_ensure(g_into_tris, &g.into_sz, tbytes, "into tris");
    if (!g_into_tris || g.into_sz < tbytes) return;
    memcpy(g_into_tris.contents, tris, tbytes);

    id<MTLComputeCommandEncoder> enc = [mg_cb() computeCommandEncoder];
    mg_encode_bbox(enc, g_into_tris, ntris, W, H, 0);
    mg_encode_tiled(enc, g_into_tris, ntris, W, H, slot,
                    0x7fffffff, 0x7fffffff, 0x7fffffff, 0);
    [enc endEncoding];

    /* Legacy path stays synchronous (mirrors cudaStreamSynchronize). */
    mg_sync();

    if (!g.frame_open) {
        memcpy(fb->color, g_d_color.contents, npix * sizeof(CrRgba));
        memcpy(fb->depth, g_d_depth.contents, npix * sizeof(float));
    }
}

/* Common tail for the layer paths: verts are already GPU-visible (staging
 * ring slot, or d_verts filled by the gather kernel); encode transform +
 * bbox + raster. Mirrors cr_cuda_run_layer, MVP built with the PUBLIC
 * cr_perspective/cr_camera_view/cr_mat4_mul from core/math.o - the same
 * source the CUDA host _dev copies compiled, so identical values on this
 * machine's libm. */
static void mg_run_layer(CrFramebuffer *fb, int nverts, const CrCamera *cam,
                         const CrShadeCtx *sh, id<MTLBuffer> vertsBuf) {
    int ntris_in = nverts / 3;
    if (ntris_in > g.max_tris) ntris_in = g.max_tris; /* caps guarantee fit */

    int W = fb->w, H = fb->h;
    size_t npix = (size_t)W * (size_t)H;

    float aspect = (float)W / (float)H; /* fb dims authoritative (transform.c) */
    CrMat4 proj = cr_perspective(cam->fov_deg, aspect, cam->znear, cam->zfar);
    /* Full camera view including hurt roll (see raster_cuda.cu). */
    CrMat4 view = cr_camera_view(cam);
    CrMat4 mvp  = cr_mat4_mul(proj, view);

    uint64_t d_tex = mg_sync_atlas(sh->atlas);
    int slot;
    CrShadeCtx *h_sh = mg_sh_slot(&slot);
    *h_sh = *sh;
    h_sh->atlas = (const CrTexture *)(uintptr_t)d_tex;
    mg_patch_lightmap(h_sh, slot);

    int ntris = 2 * ntris_in; /* worst-case dense count; dispatch sizing only */
    id<MTLComputeCommandEncoder> enc = [mg_cb() computeCommandEncoder];
    int nxblocks = (ntris_in + 255) / 256;
    mg_encode_xform(enc, vertsBuf, ntris_in, &mvp, cam->pos, W, H, 1);
    mg_encode_compact(enc, nxblocks);
    mg_encode_bbox(enc, g_d_dense, ntris, W, H, 1);
    mg_encode_tiled(enc, g_d_dense, ntris, W, H, slot,
                    0x7fffffff, 0x7fffffff, 0x7fffffff, 1);
    [enc endEncoding];
    /* No sync while the frame is open: layers queue back-to-back in the
     * frame's command buffer; frame_end is the barrier. */

    if (!g.frame_open) {
        mg_sync();
        memcpy(fb->color, g_d_color.contents, npix * sizeof(CrRgba));
        memcpy(fb->depth, g_d_depth.contents, npix * sizeof(float));
    }
}

void cr_raster_metal_render_layer(CrFramebuffer *fb, const CrVertex *verts,
                                  int nverts, const CrCamera *cam,
                                  const CrShadeCtx *sh) {
    if (!g.inited || !verts || nverts < 3) return;
    int ntris_in = nverts / 3;
    if (ntris_in > g.max_tris) ntris_in = g.max_tris;

    if (!g.frame_open) {
        mg_sync();   /* standalone: drain before overwriting the resident fb */
        size_t npix = (size_t)fb->w * (size_t)fb->h;
        memcpy(g_d_color.contents, fb->color, npix * sizeof(CrRgba));
        memcpy(g_d_depth.contents, fb->depth, npix * sizeof(float));
    }
    /* Stage verts into a ring slot and point the transform kernel straight at
     * it (CUDA pinned the host buffer + async-copied into d_verts; here the
     * snapshot happens NOW, which the host-buffer contract already allows).
     * Ring depth CR_SH_RING carries the same per-frame call bound the CUDA
     * shade-ctx ring relies on. */
    int vs = g.vs_idx;
    g.vs_idx = (g.vs_idx + 1) % CR_SH_RING;
    size_t vbytes = (size_t)ntris_in * 3 * sizeof(CrVertex);
    g_vstage[vs] = mg_ensure(g_vstage[vs], &g.vstage_sz[vs], vbytes, "vstage");
    if (!g_vstage[vs] || g.vstage_sz[vs] < vbytes) return;
    memcpy(g_vstage[vs].contents, verts, vbytes);
    mg_run_layer(fb, ntris_in * 3, cam, sh, g_vstage[vs]);
}

/* ---- device-resident chunk meshes ------------------------------------- */

int cr_raster_metal_slab_pool(int nslots, int slab_verts) {
    if (!g.inited) return 0;
    if (g_d_slabs) return g.slab_nslots == nslots && g.slab_cap == slab_verts;
    if (nslots <= 0 || nslots > CR_GR_MAX || slab_verts <= 0) return 0;
    size_t vbytes = (size_t)nslots * (size_t)slab_verts * sizeof(CrVertex);
    g_d_slabs = [g_dev newBufferWithLength:vbytes
                                   options:MTLResourceStorageModeShared];
    if (!g_d_slabs) return 0;
    size_t tab = (size_t)CR_GR_RING * (CR_GR_MAX + 1) * sizeof(int);
    g_d_gsrc = mg_newbuf(tab, "gather src");
    g_d_gpfx = mg_newbuf(tab, "gather pfx");
    g.slab_builds = (int *)malloc((size_t)nslots * sizeof(int));
    if (!g_d_gsrc || !g_d_gpfx || !g.slab_builds) {
        g_d_slabs = nil; g_d_gsrc = nil; g_d_gpfx = nil;
        free(g.slab_builds); g.slab_builds = NULL;
        return 0;
    }
    for (int i = 0; i < nslots; ++i) g.slab_builds[i] = -1;
    g.slab_nslots = nslots;
    g.slab_cap = slab_verts;
    g.gr_idx = 0;
    return 1;
}

/* Upload slot's packed slab if its rebuild counter moved. The copy is an
 * enqueue-time memcpy into the shared pool; ordering vs GPU readers: older
 * frames were drained at this frame's frame_begin, this frame's command
 * buffer is not yet committed, and the game syncs a slot before any gather
 * of the same frame references it (see header SEMANTIC ASSUMPTION). */
void cr_raster_metal_slab_sync(int slot, int builds, const void *host,
                               int used_verts) {
    if (!g_d_slabs || slot < 0 || slot >= g.slab_nslots) return;
    if (g.slab_builds[slot] == builds) return;
    if (used_verts > g.slab_cap) used_verts = g.slab_cap;
    if (g.end_pending && !g.frame_open)
        mg_sync();   /* standalone caller with an un-waited async frame */
    if (used_verts > 0)
        memcpy((char *)g_d_slabs.contents +
                   (size_t)slot * (size_t)g.slab_cap * sizeof(CrVertex),
               host, (size_t)used_verts * sizeof(CrVertex));
    g.slab_builds[slot] = builds;
}

void cr_raster_metal_slabs_reset(void) {
    if (!g.slab_builds) return;
    for (int i = 0; i < g.slab_nslots; ++i) g.slab_builds[i] = -1;
}

/* Build the gather tables in a shared ring slot and encode the gather. */
static void mg_encode_gather(id<MTLComputeCommandEncoder> enc,
                             const int *src_vert, const int *nvert, int nents,
                             int total_verts) {
    int base = g.gr_idx * (CR_GR_MAX + 1);
    g.gr_idx = (g.gr_idx + 1) % CR_GR_RING; /* frame_end sync retires */
    int *hs = (int *)g_d_gsrc.contents + base;
    int *hp = (int *)g_d_gpfx.contents + base;
    int words = 0;
    for (int i = 0; i < nents; ++i) {
        hs[i] = src_vert[i] * CR_VERT_WORDS;
        hp[i] = words;
        words += nvert[i] * CR_VERT_WORDS;
    }
    int cap_words = total_verts * CR_VERT_WORDS;
    if (words > cap_words) words = cap_words;
    if (words <= 0) return;   /* CUDA's 0-block launch is a no-op-with-error;
                                 Metal validation would reject the dispatch */
    GatherParams gp = { nents, words, base };
    [enc setComputePipelineState:g_pso_gather];
    [enc setBuffer:g_d_verts offset:0 atIndex:0];
    [enc setBuffer:g_d_slabs offset:0 atIndex:1];
    [enc setBuffer:g_d_gsrc offset:0 atIndex:2];
    [enc setBuffer:g_d_gpfx offset:0 atIndex:3];
    [enc setBytes:&gp length:sizeof gp atIndex:4];
    [enc dispatchThreadgroups:MTLSizeMake((NSUInteger)((words + 255) / 256), 1, 1)
        threadsPerThreadgroup:MTLSizeMake(256, 1, 1)];
}

void cr_raster_metal_render_gather(CrFramebuffer *fb, const int *src_vert,
                                   const int *nvert, int nents,
                                   int total_verts, const CrCamera *cam,
                                   const CrShadeCtx *sh) {
    if (!g.inited || !g_d_slabs || nents <= 0 || total_verts < 3) return;
    if (nents > CR_GR_MAX) return;
    if (total_verts > 3 * g.max_tris) total_verts = 3 * g.max_tris;

    id<MTLComputeCommandEncoder> enc = [mg_cb() computeCommandEncoder];
    mg_encode_gather(enc, src_vert, nvert, nents, total_verts);
    [enc endEncoding];
    mg_run_layer(fb, total_verts, cam, sh, g_d_verts);
}

/* All 4 terrain layers as ONE gather + transform + bbox + raster chain
 * (raster_cuda.cu cr_raster_cuda_render_terrain). */
void cr_raster_metal_render_terrain(CrFramebuffer *fb, const int *src_vert,
                                    const int *nvert, int nents,
                                    const int lay_verts[4],
                                    const CrCamera *cam,
                                    const CrShadeCtx sh[4]) {
    if (!g.inited || !g_d_slabs || nents <= 0 || nents > CR_GR_MAX) return;
    int total_verts = lay_verts[0] + lay_verts[1] + lay_verts[2] + lay_verts[3];
    if (total_verts < 3) return;
    if (total_verts > 3 * g.max_tris) total_verts = 3 * g.max_tris;

    int ntris_in = total_verts / 3;
    if (ntris_in > g.max_tris) ntris_in = g.max_tris;
    int W = fb->w, H = fb->h;
    size_t npix = (size_t)W * (size_t)H;

    float aspect = (float)W / (float)H;
    CrMat4 proj = cr_perspective(cam->fov_deg, aspect, cam->znear, cam->zfar);
    CrMat4 view = cr_camera_view(cam);
    CrMat4 mvp  = cr_mat4_mul(proj, view);

    /* 4 CONTIGUOUS shade-ctx ring slots (kernel indexes sh[0..3]). */
    if (g.sh_idx + 4 > CR_SH_RING) g.sh_idx = 0;
    int si = g.sh_idx;
    g.sh_idx += 4;
    CrShadeCtx *ring = (CrShadeCtx *)g_d_sh.contents;
    for (int l = 0; l < 4; ++l) {
        ring[si + l] = sh[l];
        ring[si + l].atlas =
            (const CrTexture *)(uintptr_t)mg_sync_atlas(sh[l].atlas);
        mg_patch_lightmap(&ring[si + l], si + l);
    }

    /* layer boundaries in OUTPUT tri-slot space (input tri i -> slots 2i,2i+1) */
    int sb1 = 2 * (lay_verts[0] / 3);
    int sb2 = sb1 + 2 * (lay_verts[1] / 3);
    int sb3 = sb2 + 2 * (lay_verts[2] / 3);

    int ntris = 2 * ntris_in;
    id<MTLComputeCommandEncoder> enc = [mg_cb() computeCommandEncoder];
    mg_encode_gather(enc, src_vert, nvert, nents, total_verts);
    mg_encode_xform(enc, g_d_verts, ntris_in, &mvp, cam->pos, W, H, 0);
    mg_encode_bbox(enc, g_d_tris, ntris, W, H, 0);
    mg_encode_tiled(enc, g_d_tris, ntris, W, H, si, sb1, sb2, sb3, 0);
    [enc endEncoding];

    if (!g.frame_open) {
        mg_sync();
        memcpy(fb->color, g_d_color.contents, npix * sizeof(CrRgba));
        memcpy(fb->depth, g_d_depth.contents, npix * sizeof(float));
    }
}

void cr_raster_metal_post(void) {
    if (!g.inited) return;
    mg_sync();
    g_d_color = g_d_depth = g_d_tris = g_d_dense = g_d_box = nil;
    g_d_tminz = g_d_batchz = g_d_xcounts = g_d_xoffsets = nil;
    g_d_screen_tris = nil;
    g_d_verts = g_d_sh = g_d_lm = g_d_pend = nil;
    for (int i = 0; i < CR_SH_RING; ++i) g_vstage[i] = nil;
    g_into_tris = nil;
    g_d_slabs = g_d_gsrc = g_d_gpfx = nil;
    g_sun_buf = g_moon_buf = nil;
    for (int i = 0; i < CR_ATLAS_CACHE; ++i) {
        g_at_key[i] = NULL;
        g_at_tex[i] = nil;
        g_at_texels[i] = nil;
        for (int l = 0; l < 15; ++l) g_at_mip[i][l] = nil;
        g_at_nmip[i] = 0;
    }
    g_n_atlas = 0;
    free(g.slab_builds);
    g_end_cb = nil;
    g_cmd = nil;
    g_last = nil;
    memset(&g, 0, sizeof g);
    /* device / library / queue / PSOs stay (cheap, reusable on re-pre). */
}

/* ==================== per-call parity entry (mirrors cr_raster_cuda) =====
 * Allocates transient buffers per call and dispatches ONE serial-encoder
 * dispatch PER TRIANGLE, in CPU order, so overlapping-triangle depth
 * resolution matches without atomics. Correct but slow; the game uses the
 * alloc-once paths above. If Metal is unavailable this falls back to
 * cr_raster_cpu (semantically identical by the parity contract) after
 * printing one loud error - unlike CUDA there is no partial-init state where
 * launches silently no-op. */
void cr_raster_metal(CrFramebuffer *fb, const CrScreenTri *tris, int ntris,
                     const CrShadeCtx *sh) {
    static int warned = 0;
    if (!mg_boot()) {
        if (!warned) {
            fprintf(stderr, "magma Metal: unavailable; cr_raster_metal falling"
                            " back to cr_raster_cpu\n");
            warned = 1;
        }
        cr_raster_cpu(fb, tris, ntris, sh);
        return;
    }
    int W = fb->w, H = fb->h;
    size_t npix = (size_t)W * (size_t)H;

    id<MTLBuffer> t_color = mg_newbuf(npix * sizeof(CrRgba), "tmp color");
    id<MTLBuffer> t_depth = mg_newbuf(npix * sizeof(float), "tmp depth");
    if (!t_color || !t_depth) return;
    memcpy(t_color.contents, fb->color, npix * sizeof(CrRgba));
    memcpy(t_depth.contents, fb->depth, npix * sizeof(float));

    /* Deep-copy the shading context: atlas texels -> shared buffers, a device
     * CrTexture pointing at them (GPU addresses), a device CrShadeCtx
     * pointing at that. */
    const CrTexture *atlas = sh->atlas;
    id<MTLBuffer> t_texels = nil, t_tex = nil, t_lm = nil;
    id<MTLBuffer> t_mip[15] = {nil};
    int n_mip = 0;
    CrShadeCtx h_sh = *sh;
    if (atlas) {
        size_t ntex = (size_t)atlas->w * (size_t)atlas->h;
        t_texels = mg_newbuf(ntex * sizeof(CrRgba), "tmp texels");
        if (!t_texels) return;
        memcpy(t_texels.contents, atlas->texels, ntex * sizeof(CrRgba));
        CrTexture h_tex = *atlas;
        h_tex.texels = (const CrRgba *)(uintptr_t)t_texels.gpuAddress;
        n_mip = atlas->mip_levels;
        if (n_mip < 0) n_mip = 0;
        if (n_mip > 15) n_mip = 15;
        for (int l = 0; l < n_mip; l++) {
            size_t nl = (size_t)atlas->mipw[l] * (size_t)atlas->miph[l];
            h_tex.mip[l] = NULL;
            if (nl > 0 && atlas->mip[l]) {
                t_mip[l] = mg_newbuf(nl * sizeof(CrRgba), "tmp mip");
                if (t_mip[l]) {
                    memcpy(t_mip[l].contents, atlas->mip[l],
                           nl * sizeof(CrRgba));
                    h_tex.mip[l] = (const CrRgba *)(uintptr_t)
                        t_mip[l].gpuAddress;
                }
            }
        }
        t_tex = mg_newbuf(sizeof(CrTexture), "tmp CrTexture");
        if (!t_tex) return;
        memcpy(t_tex.contents, &h_tex, sizeof(CrTexture));
        h_sh.atlas = (const CrTexture *)(uintptr_t)t_tex.gpuAddress;
    } else {
        h_sh.atlas = NULL;   /* defensive; CUDA path would deref (callers
                                always pass a real atlas here) */
    }
    if (h_sh.lightmap) {
        t_lm = mg_newbuf(256 * sizeof(CrRgba), "tmp lightmap");
        if (!t_lm) return;
        memcpy(t_lm.contents, h_sh.lightmap, 256 * sizeof(CrRgba));
        h_sh.lightmap = (const CrRgba *)(uintptr_t)t_lm.gpuAddress;
    }
    id<MTLBuffer> t_sh = mg_newbuf(sizeof(CrShadeCtx), "tmp shade ctx");
    if (!t_sh) return;
    memcpy(t_sh.contents, &h_sh, sizeof(CrShadeCtx));

    id<MTLCommandBuffer> cb = [g_queue commandBuffer];
    /* SERIAL dispatch encoder: dispatches run one at a time in encode order,
     * matching the CUDA default-stream per-triangle launches. */
    id<MTLComputeCommandEncoder> enc = [cb computeCommandEncoder];
    [enc setComputePipelineState:g_pso_tri];
    [enc setBuffer:t_color offset:0 atIndex:0];
    [enc setBuffer:t_depth offset:0 atIndex:1];
    [enc setBuffer:t_sh offset:0 atIndex:3];
    if (t_tex)    [enc useResource:t_tex usage:MTLResourceUsageRead];
    if (t_texels) [enc useResource:t_texels usage:MTLResourceUsageRead];
    for (int l = 0; l < n_mip; l++)
        if (t_mip[l]) [enc useResource:t_mip[l] usage:MTLResourceUsageRead];
    if (t_lm)     [enc useResource:t_lm usage:MTLResourceUsageRead];

    for (int t = 0; t < ntris; t++) {
        /* Host-side cull + clamped bbox: expression-identical to the CUDA
         * host loop (this .m must compile with -ffp-contract=off). */
        const CrScreenVert *v0 = &tris[t].v[0];
        const CrScreenVert *v1 = &tris[t].v[1];
        const CrScreenVert *v2 = &tris[t].v[2];
        float x0 = v0->spos.x, y0 = v0->spos.y;
        float x1 = v1->spos.x, y1 = v1->spos.y;
        float x2 = v2->spos.x, y2 = v2->spos.y;

        float area = (x1 - x0) * (y2 - y0) - (y1 - y0) * (x2 - x0);
        if (area * CR_FRONT_SIGN <= 0.0f) continue;

        float fminx = fminf(x0, fminf(x1, x2));
        float fmaxx = fmaxf(x0, fmaxf(x1, x2));
        float fminy = fminf(y0, fminf(y1, y2));
        float fmaxy = fmaxf(y0, fmaxf(y1, y2));
        int minx = (int)floorf(fminx); if (minx < 0) minx = 0;
        int maxx = (int)ceilf(fmaxx);  if (maxx > W) maxx = W;
        int miny = (int)floorf(fminy); if (miny < 0) miny = 0;
        int maxy = (int)ceilf(fmaxy);  if (maxy > H) maxy = H;
        int bw = maxx - minx;
        int bh = maxy - miny;
        if (bw <= 0 || bh <= 0) continue;

        TriParams tp;
        tp.W = W; tp.H = H; tp.minx = minx; tp.miny = miny;
        tp.bw = bw; tp.bh = bh; tp.tri = tris[t];
        [enc setBytes:&tp length:sizeof tp atIndex:2];
        [enc dispatchThreadgroups:MTLSizeMake((NSUInteger)((bw + 15) / 16),
                                              (NSUInteger)((bh + 15) / 16), 1)
            threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
    }
    [enc endEncoding];
    [cb commit];
    [cb waitUntilCompleted];
    mg_check(cb, "raster");

    memcpy(fb->color, t_color.contents, npix * sizeof(CrRgba));
    memcpy(fb->depth, t_depth.contents, npix * sizeof(float));
    /* transient buffers released by ARC when the locals go out of scope */
}
