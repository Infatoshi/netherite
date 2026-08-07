/* magma - full C/CUDA software rasterizer for the Minecraft C engine.
 *
 * THIS HEADER IS THE INTERFACE CONTRACT. Every module implements the prototypes
 * declared here, in its own .c/.cu file, against these exact types. Do NOT change
 * signatures without updating SPEC.md; adding new internal helpers in your own
 * file is fine. Types are POD and host/device shareable.
 *
 * Coordinate + data flow (see SPEC.md):
 *   world CrVertex[]  --transform.c-->  CrScreenTri[]  --raster(.c/.cu)-->  CrFramebuffer
 *   CrFramebuffer     --present.c-->     window/pixels + CrInput
 */
#ifndef MAGMA_TYPES_H
#define MAGMA_TYPES_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* host/device portability: functions usable from C and CUDA */
#if defined(__CUDACC__)
#define CR_HD __host__ __device__
#else
#define CR_HD
#endif

typedef uint8_t  u8;
typedef uint32_t u32;
typedef int32_t  i32;
typedef int64_t  i64;

/* ---- math ---- */
typedef struct { float x, y; }       CrVec2;
typedef struct { float x, y, z; }    CrVec3;
typedef struct { float x, y, z, w; } CrVec4;
/* column-major 4x4, m[col*4+row], matches GL convention */
typedef struct { float m[16]; }      CrMat4;

/* packed RGBA8, byte order R,G,B,A in memory (little-endian u32 = 0xAABBGGRR) */
typedef struct { u8 r, g, b, a; }    CrRgba;

/* ---- vertex (input to the pipeline, one per mesh corner, world space) ---- */
typedef struct {
    CrVec3 pos;      /* world-space position */
    CrVec2 uv;       /* atlas texture coords in [0,1] over the whole atlas */
    float  light;    /* combined block+sky light 0..1 (lightmap already resolved);
                        with CrShadeCtx.lightmap set: SKY light level 0..15 (the
                        lightmap v coord, resolved per fragment at shade time) */
    CrRgba tint;     /* biome/vertex tint multiplied into texel (a = 255 opaque) */
    float  ao;       /* ambient occlusion 0..1 (1 = no occlusion) */
    float  blk;      /* BLOCK light level 0..15 (only read when ctx lightmap set) */
} CrVertex;

/* ---- screen-space triangle (output of transform, input to raster) ---- */
/* Positions are in framebuffer pixel space: x in [0,W], y in [0,H] (y down),
 * z = ndc depth in [0,1] for the z-buffer, invw = 1/clip.w for perspective-correct
 * interpolation. Attributes are stored pre-divided by w (attr/w); raster divides by
 * the interpolated invw to recover perspective-correct values. */
typedef struct {
    CrVec3 spos;     /* screen x,y (pixels) + z (0..1 depth) */
    float  invw;     /* 1/clip.w */
    CrVec2 uv_w;     /* uv * invw */
    float  light_w;  /* light * invw */
    float  ao_w;     /* ao * invw */
    float  eye_dist_w; /* radial eye distance * invw (GL_EYE_RADIAL_NV fog) */
    /* Perspective-correct tint (attr * invw). Smooth-shaded vertex color for
     * LayerEnderDragonDeath rays; uniform per face for textured meshes. */
    float  tint_r_w, tint_g_w, tint_b_w, tint_a_w;
    float  blk_w;    /* block light level * invw (only read when ctx lightmap set) */
} CrScreenVert;

typedef struct { CrScreenVert v[3]; } CrScreenTri;

/* ---- framebuffer ---- */
typedef struct {
    int    w, h;
    CrRgba *color;   /* w*h, row-major, y=0 at top */
    float  *depth;   /* w*h, cleared to 1.0 (far) */
} CrFramebuffer;

/* ---- texture atlas (nearest sampling; optional mip levels) ---- */
typedef struct {
    int    w, h;      /* level-0 dimensions */
    const CrRgba *texels; /* level 0, w*h row-major */
    int    tile;      /* atlas tile size in px (e.g. 16), 0 if n/a */
    /* optional gamma-correct mip chain (render-opt kernels 7/8). mip_levels=0 -> none.
     * mip[l] is level l+1 (level 0 == texels); mipw/miph give its dimensions. */
    int    mip_levels;
    const CrRgba *mip[15];
    int    mipw[15], miph[15];
} CrTexture;

/* MC render layers (draw order + blend/alpha state). */
typedef enum {
    CR_LAYER_SOLID = 0,          /* opaque, no blend, no alpha test */
    CR_LAYER_CUTOUT_MIPPED = 1,  /* alpha test, mipmapped (leaves) */
    CR_LAYER_CUTOUT = 2,         /* alpha test, no mips (glass/foliage) */
    CR_LAYER_TRANSLUCENT = 3     /* alpha blend, back-to-front (water/ice) */
} CrRenderLayer;

/* ---- shading context passed to raster; fragment color = texel*tint*light*ao + fog ---- */
typedef struct {
    const CrTexture *atlas;
    CrRgba fog_color;
    float  fog_start; /* world/eye distance where fog begins */
    float  fog_end;   /* fully fogged distance */
    int    alpha_test;/* 1 = discard by alpha threshold (see alpha_ref) */
    /* GL alphaFunc ref when alpha_test: 0 = default cutout 0.5 (a < 128);
     * RenderLivingBase / LayerSlimeGel living path uses 0.1 (a <= 25). */
    float  alpha_ref;
    int    enable_fog;
    int    layer;     /* CrRenderLayer; SOLID (0) if unset for back-compat */
    /* blend: 0 = replace + depth write; 1 = src-over (SRC_ALPHA,
     * ONE_MINUS_SRC_ALPHA), no depth write; 2 = multiply-2x
     * (DST_COLOR, SRC_COLOR): out = 2*src*dst, no depth write - vanilla
     * RenderGlobal.preRenderDamagedBlocks dig-crack path;
     * 3 = additive (SRC_ALPHA, ONE): out = src*src.a + dst, no depth write
     * (LayerEnderDragonDeath light rays);
     * 4 = src-over + depth write (LayerSlimeGel: enableBlend without
     * depthMask(false), texture alpha drives translucency). */
    int    blend;
    int    use_mips;  /* 1 = sample the atlas mip chain via CrFragment.lod */
    float  mip_bias;  /* LOD bias applied when use_mips */
    /* 16x16 lightmap texture, row-major [sky*16 + blk] - the exact
     * EntityRenderer.updateLightmap texels for the frame's sun brightness.
     * NULL = legacy path: CrFragment.light is a prefolded 0..1 scalar.
     * Set = GL semantics: fragment light/blk are 0..15 lightmap coords,
     * bilinearly sampled from the 8-bit texels (GL_LINEAR on the 16x16
     * lightmap texture at coords (level*16+8)/256, i.e. texel-space == level). */
    const CrRgba *lightmap;
    /* 1 = fragments at EXACTLY the stored depth also pass (GL_LEQUAL - MC's
     * depth func). Needed by draws whose quads are coplanar with an earlier
     * layer (grass_side_overlay over grass_side); default 0 keeps the strict
     * z< behavior every existing golden was pinned with. */
    int    depth_lequal;
    /* > 0 = GL_EXP fog (EntityRenderer.setupFog fluid branches: eye in water
     * density 0.1, lava 2.0): factor = exp(-density * eye_dist), overriding
     * the linear fog_start/fog_end ramp. 0 (default; every positional
     * initializer that predates this field leaves it 0) keeps GL_LINEAR. */
    float  fog_exp_density;
    /* Optional per-texel dissolve (RenderDragon death): when alpha_mask is 1,
     * fragments with light < 0 sample atlas at uv+(mask_u_off,mask_v_off) and
     * discard when mask.a/255 <= ao (ao carries deathTicks/200). Color still
     * samples the primary UV. Zero-init keeps every existing caller unchanged. */
    int    alpha_mask;
    float  mask_u_off, mask_v_off;
    /* 1 = skip atlas sample; texel is opaque white (untextured POSITION_COLOR
     * geometry such as LayerEnderDragonDeath). */
    int    untextured;
    /* 1 = convert float RGB to u8 by truncation (int)(c*255), matching fixed-
     * function GL / DynamicTexture lightmap stores. 0 (default) keeps the
     * historical round-half-up (*255+0.5) used by terrain goldens.
     * Preview-only path (player_preview); appended so entity fields keep ABI. */
    int    color_trunc;
    /* >0 = pixel-space outward edge slack (px). Samples just outside a float
     * edge by less than this still count as covered (Java/Mesa inventory
     * preview). 0 (default) keeps strict top-left. */
    float  cover_eps;
    /* Nearest sample phase: 0 (default) = floor(u*w - 1e-4) for terrain atlas
     * high-edge; 1 = pure floor(u*w) (fixed-function / entity skin, exact
     * integer model UVs). Other values reserved. Does not change when 0. */
    int    sample_mode;
    /* Entity-only RenderLivingBase brightness combiner. A negative fragment
     * blk packs the original block-light level plus a white-overlay mix. */
    int    entity_brightness;
    /* GL_REPEAT on both axes before sampling. Used by the standalone
     * endercrystal_beam texture whose scrolling V coordinates leave [0,1].
     * Zero preserves clamp-to-edge for every atlas pass. */
    int    repeat_uv;
} CrShadeCtx;

/* per-fragment inputs handed to the shader (perspective-corrected already) */
typedef struct {
    CrVec2 uv;
    float  light;
    float  ao;
    CrRgba tint;
    float  eye_dist; /* for fog; radial eye-space distance */
    float  lod;      /* mip LOD for use_mips sampling (0 = level 0) */
    float  blk;      /* block light level 0..15 (only read when ctx lightmap set) */
} CrFragment;

/* ---- camera ---- */
typedef struct {
    CrVec3 pos;
    float  yaw, pitch;   /* radians; MC convention */
    float  fov_deg;      /* vertical fov */
    float  aspect;       /* w/h */
    float  znear, zfar;
    /* EntityRenderer.hurtCameraEffect, applied before orientCamera. Degrees
     * match the vanilla GlStateManager.rotate arguments. */
    float  hurt_yaw_deg, hurt_roll_deg;
} CrCamera;

/* ---- input snapshot from the present layer ---- */
typedef struct {
    int  quit;
    int  key_w, key_a, key_s, key_d, key_space, key_shift, key_ctrl;
    int  mouse_dx, mouse_dy; /* relative since last poll */
    int  mouse_captured;
    /* --- appended (back-compat; older present.c leaves these 0) --- */
    int  mouse_left, mouse_right; /* 1 = button currently held */
    int  key_num;                 /* 1..9 pressed this poll -> hotbar slot 0..8; 0 = none */
    int  wheel;                   /* mouse wheel delta this poll (+ up / - down), for hotbar cycle */
    int  key_e;                   /* inventory toggle (held) */
    int  key_q;                   /* drop/throw from hotbar (edge handled in input_map) */
    /* arrow-key look (held). Primary look control over VNC, where SDL relative-mouse
     * capture is unreliable. Each maps to the same sign as pushing the mouse that way. */
    int  key_up, key_down, key_left, key_right;
    /* --- appended for the container screen (older present.c leaves these 0) --- */
    int  mouse_x, mouse_y;        /* absolute window-space cursor position */
    int  click_left, click_right; /* button PRESS edges seen this poll */
    int  key_esc;                 /* ESC edge, reported only while a GUI screen owns the cursor */
    int  key_tab;                 /* hotbar cycle (held; edge handled in input_map) */
} CrInput;

/* opaque window handle owned by present.c */
typedef struct CrWindow CrWindow;

/* ================= module prototypes ================= */

/* --- core/math.c (small, may be header-inline; provided by transform owner) --- */
CR_HD CrMat4 cr_mat4_identity(void);
CR_HD CrMat4 cr_mat4_mul(CrMat4 a, CrMat4 b);
CR_HD CrVec4 cr_mat4_mul_vec4(CrMat4 m, CrVec4 v);
CR_HD CrMat4 cr_perspective(float fov_deg, float aspect, float znear, float zfar);
CR_HD CrMat4 cr_look_yaw_pitch(CrVec3 pos, float yaw, float pitch);
CR_HD CrMat4 cr_camera_view(const CrCamera *cam);

/* --- transform.c : world verts -> screen tris (MVP + near-clip + viewport) ---
 * Returns number of screen triangles written to `out` (<= max_out). Performs
 * near-plane clipping (may emit 0..2 tris per input tri). `idx` may be NULL for
 * a flat vertex stream (verts grouped as triangles: 0,1,2, 3,4,5, ...). */
int cr_transform(const CrVertex *verts, int nverts,
                 const u32 *idx, int nidx,
                 const CrCamera *cam, int fb_w, int fb_h,
                 CrScreenTri *out, int max_out);

/* Per-triangle transform worker (3 consecutive verts -> 0..2 screen tris).
 * CR_HD: single source for the CPU loop and the CUDA transform kernel. */
CR_HD int cr_transform_tri(CrMat4 mvp, CrVec3 campos, const CrVertex *v3,
                           int fb_w, int fb_h, CrScreenTri out[2]);

/* --- raster : CPU and CUDA. Both rasterize `tris` into `fb` with z-test and
 * perspective-correct shading via `sh`. Must produce matching output (bit-exact
 * where possible; see SPEC tolerance). Do not clear fb here; caller clears. --- */
void cr_raster_cpu(CrFramebuffer *fb, const CrScreenTri *tris, int ntris, const CrShadeCtx *sh);
void cr_raster_cuda(CrFramebuffer *fb, const CrScreenTri *tris, int ntris, const CrShadeCtx *sh);
/* Host mutated atlas texels (water_still animation): force device re-upload. */
void cr_raster_cuda_atlas_dirty(void);
/* Metal mirror of the CUDA raster API (macOS, metal/raster_metal_host.m):
 * same signatures and contract, s/cuda/metal/ throughout. */
void cr_raster_metal(CrFramebuffer *fb, const CrScreenTri *tris, int ntris, const CrShadeCtx *sh);
void cr_raster_metal_atlas_dirty(void);

/* --- shade.c : per-fragment color. Called by the rasterizer for each covered,
 * depth-passing pixel. Keep it pure (no globals) so CPU and CUDA share it. --- */
CR_HD CrRgba cr_shade(const CrShadeCtx *sh, const CrFragment *frag);
CR_HD CrRgba cr_atlas_sample(const CrTexture *tex, float u, float v); /* nearest */

/* --- framebuffer helpers (raster owner provides) --- */
void cr_fb_alloc(CrFramebuffer *fb, int w, int h);
void cr_fb_free(CrFramebuffer *fb);
void cr_fb_clear(CrFramebuffer *fb, CrRgba color); /* color + depth=1.0 */

/* --- present.c : window + blit + input --- */
CrWindow *cr_window_open(int w, int h, const char *title);
int  cr_window_present(CrWindow *win, const CrFramebuffer *fb); /* blit color buffer */
void cr_window_poll(CrWindow *win, CrInput *out);
void cr_window_close(CrWindow *win);
/* Enable/disable FPS mouse capture. Disabled = capture released now and clicks no
 * longer auto-recapture (a GUI screen owns the cursor until re-enabled). */
void cr_window_capture_enable(CrWindow *win, int on);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_TYPES_H */
