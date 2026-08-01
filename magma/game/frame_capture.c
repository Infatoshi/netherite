#include "game/frame_capture.h"

#include "assets/blockmodels.h"
#include "game/block_registry.h"   /* vanilla state -> particle model key */
#include "game/caps.h"
#include "game/dragon_live.h"
#include "game/entity_render.h"
#include "game/hand.h"
#include "game/hud.h"
#include "game/item_render.h"
#include "game/overlay.h"
#include "game/screen.h"
#include "game/sel_box.h"
#include "game/sky.h"
#include "game/underwater.h"
#include "game/view.h"
#include "world/mesh_mc.h"
#include "world/lightmap.h"
#include "mc_math.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef MAGMA_METAL
/* Metal backend seam (magma_game_metal, game/frame_capture_metal.o): this file
 * dispatches the GPU raster path through the cr_raster_cuda_* names below.
 * Under -DMAGMA_METAL those names map onto their cr_raster_metal_* mirrors
 * (identical signatures; metal/raster_metal_host.m), so every call site,
 * NULL-check and CPU fallback stays textually identical across backends. */
extern void cr_raster_metal_pre(int, int, int) __attribute__((weak));
extern void cr_raster_metal_into(CrFramebuffer *, const CrScreenTri *, int,
                                 const CrShadeCtx *) __attribute__((weak));
extern void cr_raster_metal_frame_begin(const CrFramebuffer *) __attribute__((weak));
extern void cr_raster_metal_frame_end(CrFramebuffer *) __attribute__((weak));
extern void cr_raster_metal_post(void) __attribute__((weak));
extern void cr_raster_metal_atlas_dirty(void) __attribute__((weak));
extern void cr_raster_metal_pin(void *, size_t) __attribute__((weak));
extern void cr_raster_metal_unpin(void *) __attribute__((weak));
extern void cr_raster_metal_render_layer(CrFramebuffer *, const CrVertex *, int,
                                         const CrCamera *, const CrShadeCtx *)
    __attribute__((weak));
extern void cr_raster_metal_sky(const GmSkyCtx *, const float *, int, int)
    __attribute__((weak));
extern int cr_raster_metal_frame_end_async(CrFramebuffer *, CrRgba *)
    __attribute__((weak));
extern void cr_raster_metal_frame_wait(void) __attribute__((weak));
extern int cr_raster_metal_slab_pool(int, int) __attribute__((weak));
extern void cr_raster_metal_slab_sync(int, int, const void *, int)
    __attribute__((weak));
extern void cr_raster_metal_slabs_reset(void) __attribute__((weak));
extern void cr_raster_metal_render_gather(CrFramebuffer *, const int *,
                                          const int *, int, int,
                                          const CrCamera *, const CrShadeCtx *)
    __attribute__((weak));
extern void cr_raster_metal_render_terrain(CrFramebuffer *, const int *,
                                           const int *, int, const int[4],
                                           const CrCamera *, const CrShadeCtx *)
    __attribute__((weak));
extern void cr_raster_metal_uploads_mark(void) __attribute__((weak));
extern void cr_raster_metal_uploads_wait(void) __attribute__((weak));
#define cr_raster_cuda_pre             cr_raster_metal_pre
#define cr_raster_cuda_into            cr_raster_metal_into
#define cr_raster_cuda_frame_begin     cr_raster_metal_frame_begin
#define cr_raster_cuda_frame_end       cr_raster_metal_frame_end
#define cr_raster_cuda_post            cr_raster_metal_post
#define cr_raster_cuda_atlas_dirty     cr_raster_metal_atlas_dirty
#define cr_raster_cuda_pin             cr_raster_metal_pin
#define cr_raster_cuda_unpin           cr_raster_metal_unpin
#define cr_raster_cuda_render_layer    cr_raster_metal_render_layer
#define cr_raster_cuda_sky             cr_raster_metal_sky
#define cr_raster_cuda_frame_end_async cr_raster_metal_frame_end_async
#define cr_raster_cuda_frame_wait      cr_raster_metal_frame_wait
#define cr_raster_cuda_slab_pool       cr_raster_metal_slab_pool
#define cr_raster_cuda_slab_sync       cr_raster_metal_slab_sync
#define cr_raster_cuda_slabs_reset     cr_raster_metal_slabs_reset
#define cr_raster_cuda_render_gather   cr_raster_metal_render_gather
#define cr_raster_cuda_render_terrain  cr_raster_metal_render_terrain
#define cr_raster_cuda_uploads_mark    cr_raster_metal_uploads_mark
#define cr_raster_cuda_uploads_wait    cr_raster_metal_uploads_wait
#else
extern void cr_raster_cuda_pre(int, int, int) __attribute__((weak));
extern void cr_raster_cuda_into(CrFramebuffer *, const CrScreenTri *, int,
                                const CrShadeCtx *) __attribute__((weak));
extern void cr_raster_cuda_frame_begin(const CrFramebuffer *) __attribute__((weak));
extern void cr_raster_cuda_frame_end(CrFramebuffer *) __attribute__((weak));
extern void cr_raster_cuda_post(void) __attribute__((weak));
extern void cr_raster_cuda_atlas_dirty(void) __attribute__((weak));
extern void cr_raster_cuda_pin(void *, size_t) __attribute__((weak));
extern void cr_raster_cuda_unpin(void *) __attribute__((weak));
extern void cr_raster_cuda_render_layer(CrFramebuffer *, const CrVertex *, int,
                                        const CrCamera *, const CrShadeCtx *)
    __attribute__((weak));
extern void cr_raster_cuda_sky(const GmSkyCtx *, const float *, int, int)
    __attribute__((weak));
extern int cr_raster_cuda_frame_end_async(CrFramebuffer *, CrRgba *)
    __attribute__((weak));
extern void cr_raster_cuda_frame_wait(void) __attribute__((weak));
extern int cr_raster_cuda_slab_pool(int, int) __attribute__((weak));
extern void cr_raster_cuda_slab_sync(int, int, const void *, int)
    __attribute__((weak));
extern void cr_raster_cuda_slabs_reset(void) __attribute__((weak));
extern void cr_raster_cuda_render_gather(CrFramebuffer *, const int *,
                                         const int *, int, int,
                                         const CrCamera *, const CrShadeCtx *)
    __attribute__((weak));
extern void cr_raster_cuda_render_terrain(CrFramebuffer *, const int *,
                                          const int *, int, const int[4],
                                          const CrCamera *, const CrShadeCtx *)
    __attribute__((weak));
extern void cr_raster_cuda_uploads_mark(void) __attribute__((weak));
extern void cr_raster_cuda_uploads_wait(void) __attribute__((weak));
#endif /* MAGMA_METAL */

/* 2 x 4 rotating entity vert buffers: a frame emits entities, terrain items,
 * item-atlas geometry, then the post-render fire overlay (4 buffers), and the
 * pipelined path preps frame N+1 while frame N's uploads may still be in
 * flight - so N+1 uses the OTHER set of 4
 * (ent_set parity). Non-pipelined paths just always use set 0. */
#define GM_FC_ENT_BUFS 8

struct GmFrameCapture {
    CrFramebuffer fb;
    CrScreenTri *tris;
    CrVertex *entity_verts[GM_FC_ENT_BUFS];
    unsigned char *ppm_buf; /* packed RGB scratch, one fwrite per frame */
    int max_tris, max_entity_verts;
    GmParticlesLive *particles; /* replay-owned recorded particle pool */
    int use_cuda;           /* GPU raster on (CUDA; Metal under MAGMA_METAL) */
    int dev_mesh;           /* terrain layers gathered from the device slab pool */
    const void *slab_world; /* world whose slabs the GPU pool currently mirrors */
    GmChunkDraw *chunks;    /* caps.mesh_slots entries (dev_mesh scratch) */
    int *gsrc, *gcnt;       /* gather entry scratch (4 * mesh_slots: merged
                               terrain packs all layers into one call) */
    int mesh_slots, slab_cap;
    int merge_layers;       /* all 4 terrain layers in ONE gpu launch chain */
    int frame;
    float hand_bob;
    int swing_progress_int, swing_active;
    int prev_attack;
    float prev_attack_cooldown;
    int attack_cooldown_initialized;
    float equip_progress;
    int equip_item, equip_meta, equip_count, equip_slot;
    /* deferred frame end (CUDA): frame N's color readback lands in pend_color
     * while the next 19 sim ticks run; hand/hud/ppm happen at the wait, right
     * before frame N+1 renders (or at close). Depth never comes back - hand
     * clears it and hud ignores it. */
    CrRgba *pend_color;     /* pinned; readback target */
    int pend_active;
    int pend_frame;         /* tick-numbered filename */
    GmPlayerView pend_v;
    float pend_swing, pend_equip, pend_bob;
    int pend_item, pend_meta, pend_count;
    int pend_uwov;          /* deferred frame draws the underwater overlay */
    int boss_latch;         /* GuiBossOverlay: dragon bar latched (world-scoped;
                             * the nearest-8 tape ents lose a far dragon) */
    float boss_frac;        /* last seen health/200 */
    int dragon_dying;       /* a dragon view with deathTicks>0 and hp<=0 */
    int dragon_killed;      /* processDragonDeath: bar + createFog gone */
    float pend_uwb;         /* ... with this Entity.getBrightness tint */
    float pend_uwfov;       /* ... through this hand-projection fov */
    float pend_fovscale;    /* ... and this eye-in-fluid fov scale (fire overlay) */
    long long pend_tick;         /* r->tick of the deferred frame (hand gate) */
    int pend_bih, pend_bih_id, pend_bih_meta; /* suffocate overlay, resolved at
                             * arm time: the live world has moved on by retire */
    GmHudState hud_state;
    /* depth-1 pipeline (CUDA + devmesh): frame N+1's CPU prep + enqueue run
     * BEFORE waiting out frame N, so the CPU meshes while the GPU rasters.
     * pend_depth is the hand/hud depth scratch (they must not scribble the
     * live fb.depth: its H2D for the next frame may still be pending). */
    int pipeline;
    int ent_set;            /* which 3-buffer entity set this frame uses */
    float *pend_depth;
    CrRgba *post_scratch;       /* allocate-once portal projection scratch */
    /* npy-direct mode (frames-out path ends in .npy): every rendered frame
     * appends packed RGB to ONE uint8 [N,H,W,3] file instead of 500+ PPMs;
     * the shape header is patched with the final N at close. */
    FILE *npy_f;
    int npy_frames;
    /* shade-time lightmap (worldmc lightmap mode): the frame's 16x16
     * EntityRenderer.updateLightmap texels, rebuilt from world time each
     * frame and handed to the terrain shade ctxs (overworld only). */
    int lm_mode;
    CrRgba lut[256];
    /* EntityRenderer.updateRenderer fogColor1: light-at-feet fog brightness
     * smoother (0.1/tick), advanced EVERY tick (rendered or not). Seeded at
     * the first call with its steady state - the oracle client has been
     * running long before recstart, so its smoother has converged. */
    float fog_c1;
    int fog_c1_init;
    char dir[1024];
};

void gm_frame_capture_bind_particles(GmFrameCapture *c,
                                     GmParticlesLive *particles) {
    if (c) c->particles = particles;
}

static void set_error(char *err, int cap, const char *msg) {
    if (err && cap > 0) snprintf(err, (size_t)cap, "%s", msg);
}

/* World.getSunBrightnessBody(1.0F), clear weather: celestial angle (float,
 * with the double Math.cos of calculateCelestialAngle), MathHelper.cos via
 * the MC sin table, ranged [0.2, 1.0]. This is the `f` of updateLightmap. */
static float fc_sun_brightness(const McSinTable *st, long long wt) {
    long long i = wt % 24000LL;
    if (i < 0) i += 24000LL;
    float f = ((float)i + 1.0f) / 24000.0f - 0.25f;   /* partialTicks = 1 */
    if (f < 0.0f) f += 1.0f;
    if (f > 1.0f) f -= 1.0f;
    float f1 = 1.0f - (float)((cos((double)f * 3.141592653589793) + 1.0) / 2.0);
    f = f + (f1 - f) / 3.0f;
    float g = 1.0f - (mc_cos(st, f * 6.2831855f) * 2.0f + 0.2f);
    if (g < 0.0f) g = 0.0f;
    if (g > 1.0f) g = 1.0f;
    g = 1.0f - g;
    return g * 0.8f + 0.2f;
}

/* The frame's lightmap texture: exact updateLightmap texels for the current
 * sun brightness (torch flicker + gamma pinned 0, matching the light state).
 * Shared with the interactive window loop (game_main.c), which must feed the
 * SAME texels to its terrain shade ctxs or lightmap-mode meshes shade garbage. */
void gm_frame_lightmap_fill(const McSinTable *st, long long world_time,
                            CrRgba lut[256]) {
    float sun = fc_sun_brightness(st, world_time);
    for (int sl = 0; sl < 16; ++sl)
        for (int bl = 0; bl < 16; ++bl)
            lut[sl * 16 + bl] =
                cr_lightmap_rgba8(cr_lightmap_rgb(0, sl, bl, sun, 0.0f, 0.0f));
}

static const CrRgba *build_lightmap_lut(GmFrameCapture *c, const GmRuntime *r) {
    if (!c->lm_mode || r->dimension != 0) return NULL;
    gm_frame_lightmap_fill(&r->sin_table, r->clock.world_time, c->lut);
    return c->lut;
}

/* World light at each entity's eye block (RenderLivingBase brightness):
 * lightmap mode passes the raw 0..15 levels through the LUT; the legacy /
 * Nether/End path (lm==NULL) folds the exact updateLightmap color into the
 * tint since the shader then treats vtx.light as a plain scalar. Shared with
 * the interactive window loop: unfilled fields render entities unlit-white. */
void gm_frame_entities_light(GmEntityView *ents, int n, GmWorld *world,
                             int dimension, const CrRgba *lm) {
    for(int i=0;i<n;++i){
        int ex=(int)floorf(ents[i].x);
        int ey=(int)floorf(ents[i].y+gm_entity_eye_y(ents[i].type));
        int ez=(int)floorf(ents[i].z);
        int sky=gm_world_sky_light(world,ex,ey,ez);
        int bl=gm_world_block_light(world,ex,ey,ez);
        /* getBrightnessForRender overrides (EntityBlaze: 15728880) pin the
         * lightmap coords at max, so the model is drawn glowing whatever the
         * cell light is. Forcing sky/bl here keeps both the LUT and the folded
         * (Nether/End) path exact for the dimension. */
        if(gm_entity_fullbright(ents[i].type)){sky=15;bl=15;}
        if(lm){
            ents[i].lm_lit=1;
            ents[i].lm_light=(float)sky;ents[i].lm_blk=(float)bl;
            /* The held-item pass has no shade lightmap binding. Preserve the
             * exact current LUT texel so it can fold the same night/day color
             * into its tint instead of assuming full daylight. */
            CrRgba lc=lm[sky*16+bl];
            ents[i].lm_mul_r=(float)lc.r/255.0f;
            ents[i].lm_mul_g=(float)lc.g/255.0f;
            ents[i].lm_mul_b=(float)lc.b/255.0f;
        }else{
            CrLightmapRgb c3=cr_lightmap_rgb(dimension,sky,bl,
                cr_dimension_sun_brightness(dimension),0.f,0.f);
            ents[i].lm_lit=2;
            ents[i].lm_mul_r=c3.r;ents[i].lm_mul_g=c3.g;ents[i].lm_mul_b=c3.b;
        }
    }
}

void gm_frame_prepare_minecarts(GmEntityView *ents, int n, GmWorld *world) {
    for (int i = 0; i < n; ++i) {
        int type = ents[i].type;
        if (type != GM_VIEW_MINECART_EMPTY &&
            type != GM_VIEW_MINECART_CHEST &&
            type != GM_VIEW_MINECART_FURNACE &&
            type != GM_VIEW_MINECART_HOPPER &&
            type != GM_VIEW_MINECART_TNT)
            continue;
        int rx = (int)floorf(ents[i].x);
        int ry = (int)floorf(ents[i].y);
        int rz = (int)floorf(ents[i].z);
        if (gm_world_block(world, rx, ry, rz) != 66 &&
            gm_world_block(world, rx, ry - 1, rz) == 66)
            --ry;
        if (gm_world_block(world, rx, ry, rz) == 66) {
            int shape = gm_world_meta(world, rx, ry, rz) & 15;
            if (shape == 0) {
                ents[i].x = (float)rx + 0.5f;
                ents[i].y = (float)ry + 0.0625f;
                ents[i].yaw = -90.0f;
            } else if (shape == 1) {
                ents[i].y = (float)ry + 0.0625f;
                ents[i].z = (float)rz + 0.5f;
                ents[i].yaw = 0.0f;
            }
        }
        uint64_t seed = (uint64_t)(int64_t)ents[i].ent_id *
                        UINT64_C(493286711);
        seed = seed * seed * UINT64_C(4392167121) + seed * UINT64_C(98761);
        ents[i].x += (((float)((seed >> 16) & 7) + 0.5f) / 8.0f - 0.5f) *
                     0.004f;
        ents[i].y += (((float)((seed >> 20) & 7) + 0.5f) / 8.0f - 0.5f) *
                     0.004f;
        ents[i].z += (((float)((seed >> 24) & 7) + 0.5f) / 8.0f - 0.5f) *
                     0.004f;
    }
}

static float time_of_day(const GmRuntime *r) {
    long long t = r->clock.world_time % 24000LL;
    if (t < 0) t += 24000LL;
    return (float)t / 24000.0f;
}

static CrCamera camera_for(const GmPlayerView *v, int w, int h) {
    CrCamera c;
    c.pos = (CrVec3){v->x, v->y + v->eye_height, v->z};
    /* MC-degrees -> magma camera radians: the ONE pixel-verified mapping
     * (game/view.h). (yaw - 180) X-mirrors the view at any yaw != 180. */
    c.yaw = gm_view_cam_yaw_rad(v->yaw);
    c.pitch = gm_view_cam_pitch_rad(v->pitch);
    /* getFOVModifier: base fov * smoothed fovModifierHand (sprint ease). */
    c.fov_deg = 70.0f * (v->fov_mult > 0.01f ? v->fov_mult : 1.0f);
    c.aspect = (float)w / (float)h;
    c.znear = 0.05f;
    /* EntityRenderer.setupCameraTransform: far = RD*16 * sqrt(2). See GM_TERRAIN_ZFAR. */
    c.zfar = GM_TERRAIN_ZFAR;
    c.hurt_yaw_deg = v->hurt_yaw;
    c.hurt_roll_deg = gm_view_hurt_roll_deg(v->hurt_time, v->max_hurt_time);
    return c;
}

static int render_layer(GmFrameCapture *c, const CrCamera *cam,
                        const CrVertex *verts, int nverts,
                        const CrShadeCtx *shade) {
    if (nverts < 3) return 0;
    if (c->use_cuda && cr_raster_cuda_render_layer) {
        /* transform + raster fully on-device (verts up once; the host
         * cr_transform was ~29% of the frames-run CPU profile) */
        cr_raster_cuda_render_layer(&c->fb, verts, nverts, cam, shade);
        return nverts / 3;
    }
    int n = cr_transform(verts, nverts, NULL, 0, cam, c->fb.w, c->fb.h,
                         c->tris, c->max_tris);
    if (n > 0) {
        if (c->use_cuda) cr_raster_cuda_into(&c->fb, c->tris, n, shade);
        else cr_raster_cpu(&c->fb, c->tris, n, shade);
    }
    return n;
}

/* EntityRenderer.setupFog: Nether doesXZShowFog OR the dragon fight's
 * BossInfo createFog flag (GuiBossOverlay.shouldCreateFog) pull the linear
 * ramp in to [far*0.05, min(far,192)*0.5]. setupFog is scene-wide state, so
 * every pass drawn after it - terrain AND entity layers - is fogged. */
void gm_frame_world_fog_params(int dimension, int boss_fog, int *enabled,
                               float *fog_start, float *fog_end) {
    int dense = dimension == -1 || boss_fog;
    *enabled = gm_terrain_fog_enabled();
    *fog_start = dense ? GM_TERRAIN_FOG_FAR * 0.05f : GM_TERRAIN_FOG_START;
    *fog_end = dense ? GM_TERRAIN_FOG_FAR * 0.5f : GM_TERRAIN_FOG_END;
}

CrRgba gm_frame_clear_color(float time_of_day, int dimension, float fog_c1,
                            const GmUnderwater *uw) {
    CrRgba clear = gm_terrain_fog_color(time_of_day);
    if (dimension == -1) {
        clear.r = (u8)(0.20f * fog_c1 * 255.0f + 0.5f);
        clear.g = (u8)(0.03f * fog_c1 * 255.0f + 0.5f);
        clear.b = (u8)(0.03f * fog_c1 * 255.0f + 0.5f);
        clear.a = 255;
    } else if (dimension == 1) {
        float blend = 1.0f - powf(
            0.25f + 0.75f * (GM_TERRAIN_FOG_FAR / 16.0f) / 32.0f, 0.25f);
        clear.r = (u8)(0.09411765f * (1.0f - blend) * fog_c1 * 255.0f + 0.5f);
        clear.g = (u8)(0.07529412f * (1.0f - blend) * fog_c1 * 255.0f + 0.5f);
        clear.b = (u8)(0.09411765f * (1.0f - blend) * fog_c1 * 255.0f + 0.5f);
        clear.a = 255;
    }
    if (uw && uw->fluid) clear = uw->fog_rgba;
    return clear;
}

static void terrain_shades(const CrTexture *atlas, CrRgba fog, int dimension,
                           int boss_fog, const CrRgba *lm,
                           const GmUnderwater *uw, CrShadeCtx shade[4]) {
    int enabled;
    float fog_start, fog_end;
    gm_frame_world_fog_params(dimension, boss_fog, &enabled,
                              &fog_start, &fog_end);
    /* use_mips=0 on EVERY layer, including CUTOUT_MIPPED: both oracle launch
     * profiles (java/fast.yaml + java/vanilla.yaml) pin mipmapLevels:0, which
     * makes TextureMap.setBlurMipmap give the terrain atlas plain GL_NEAREST
     * (no mip chain). Sampling magma's mip chain would shift texel
     * boundaries and average colors on minified faces the oracle samples at
     * level 0. The vanilla layer NAME only selects alpha handling; the
     * sampler state follows mipmapLevels. */
    /* Designated, NOT positional: these four predate CrShadeCtx.alpha_ref
     * (added in 3819bcf) and the positional forms silently shifted every
     * value from slot 6 on - fog flag into alpha_ref (=1.0 -> threshold 255
     * -> every CUTOUT texel discarded), layer into enable_fog, blend into
     * layer. Keep designated so the next field insert cannot repeat it. */
#define TSH(at, ly, bl) { .atlas = atlas, .fog_color = fog,                  \
                          .fog_start = fog_start, .fog_end = fog_end,        \
                          .alpha_test = (at), .enable_fog = enabled,         \
                          .layer = (ly), .blend = (bl) }
    CrShadeCtx s[4] = {
        TSH(0, CR_LAYER_SOLID,         0),
        TSH(1, CR_LAYER_CUTOUT_MIPPED, 0),
        TSH(1, CR_LAYER_CUTOUT,        0),
        TSH(0, CR_LAYER_TRANSLUCENT,   1),
    };
#undef TSH
    for (int i = 0; i < 4; ++i) { shade[i] = s[i]; shade[i].lightmap = lm; }
    /* grass_side_overlay quads are coplanar with their SOLID base faces. */
    shade[CR_LAYER_CUTOUT_MIPPED].depth_lequal = 1;
    if (uw && uw->fluid) {
        /* EntityRenderer.setupFog fluid branch: GL_EXP replaces the linear
         * ramp on every terrain layer, colored by updateFogColor's fluid
         * color * fogColor1 (game/underwater.h). */
        for (int i = 0; i < 4; ++i) {
            shade[i].fog_color = uw->fog_rgba;
            shade[i].fog_exp_density = uw->density;
        }
    }
}

static void render_world_layers(GmFrameCapture *c, const CrCamera *cam,
                                const GmMeshView *mv, const CrTexture *atlas,
                                CrRgba fog, int dimension, int boss_fog,
                                const CrRgba *lm, const GmUnderwater *uw,
                                int first_layer, int end_layer) {
    CrShadeCtx shade[4];
    terrain_shades(atlas, fog, dimension, boss_fog, lm, uw, shade);
    for (int layer=first_layer;layer<end_layer;++layer)
        render_layer(c,cam,mv->verts[layer],mv->nverts[layer],&shade[layer]);
}

/* Device-resident terrain: upload rebuilt slabs, then per layer hand the
 * gather list (pool vert offsets, in the SAME chunk order mesh_view used to
 * concat) to the GPU. Byte-identical draw buffers, zero host vert memcpy. */
static void render_world_dev_layers(GmFrameCapture *c, const CrCamera *cam,
                                    int nch, const int nv[4],
                                    const CrTexture *atlas, CrRgba fog,
                                    int dimension, int boss_fog,
                                    const CrRgba *lm, const GmUnderwater *uw,
                                    int first_layer, int end_layer) {
    CrShadeCtx shade[4];
    terrain_shades(atlas, fog, dimension, boss_fog, lm, uw, shade);
    if (first_layer == CR_LAYER_SOLID) {
        for (int i = 0; i < nch; ++i) {
            const GmChunkDraw *d = &c->chunks[i];
            cr_raster_cuda_slab_sync(d->slot, d->builds, d->slab,
                                     d->off[3] + d->n[3]);
        }
        /* last host-buffer H2D of the frame that reads game-side memory (fb +
         * slabs) is now enqueued: mark it so the NEXT frame's prep can wait on
         * this instead of the whole frame. */
        if (cr_raster_cuda_uploads_mark) cr_raster_cuda_uploads_mark();
    }
    if (c->merge_layers && first_layer == CR_LAYER_SOLID &&
        end_layer == CR_LAYER_TRANSLUCENT) {
        /* Merge only the three opaque terrain layers. Translucent must remain
         * a later gather so entity/overlay/particle draws stay between them. */
        int ne = 0;
        int opaque_nv[4] = {nv[0], nv[1], nv[2], 0};
        for (int layer = first_layer; layer < end_layer; ++layer) {
            for (int i = 0; i < nch; ++i) {
                const GmChunkDraw *d = &c->chunks[i];
                if (d->n[layer] <= 0) continue;
                c->gsrc[ne] = d->slot * c->slab_cap + d->off[layer];
                c->gcnt[ne] = d->n[layer];
                ++ne;
            }
        }
        if (ne > 0)
            cr_raster_cuda_render_terrain(&c->fb, c->gsrc, c->gcnt, ne,
                                          opaque_nv, cam, shade);
        return;
    }
    for (int layer = first_layer; layer < end_layer; ++layer) {
        int ne = 0;
        for (int i = 0; i < nch; ++i) {
            const GmChunkDraw *d = &c->chunks[i];
            if (d->n[layer] <= 0) continue;
            c->gsrc[ne] = d->slot * c->slab_cap + d->off[layer];
            c->gcnt[ne] = d->n[layer];
            ++ne;
        }
        if (ne > 0 && nv[layer] >= 3)
            cr_raster_cuda_render_gather(&c->fb, c->gsrc, c->gcnt, ne,
                                         nv[layer], cam, &shade[layer]);
    }
}

static int write_ppm(const char *path, const CrFramebuffer *fb,
                     unsigned char *buf) {
    /* Pack RGB into the preallocated scratch and fwrite once: the per-pixel
     * fwrite loop was ~410k libc calls per 854x480 frame (~5% of the
     * frames-run profile). */
    int n=fb->w*fb->h;
    for(int i=0;i<n;++i){
        buf[i*3+0]=fb->color[i].r;buf[i*3+1]=fb->color[i].g;buf[i*3+2]=fb->color[i].b;
    }
    FILE *f=fopen(path,"wb");
    if(!f)return 0;
    int ok=fprintf(f,"P6\n%d %d\n255\n",fb->w,fb->h)>=0
        && fwrite(buf,3,(size_t)n,f)==(size_t)n;
    return (fclose(f)==0)&&ok;
}

/* v1.0 npy header, fixed 128 bytes so the shape can be re-stamped at close.
 * %8d keeps the dict length constant for any frame count. */
static void npy_stamp_header(FILE *f, int n, int h, int w) {
    char dict[119];
    int len=snprintf(dict,sizeof dict,
        "{'descr': '|u1', 'fortran_order': False, 'shape': (%8d, %d, %d, 3), }",
        n,h,w);
    memset(dict+len,' ',117-(size_t)len);dict[117]='\n';
    unsigned char pre[10]={0x93,'N','U','M','P','Y',1,0,118,0};
    fseek(f,0,SEEK_SET);
    fwrite(pre,1,10,f);fwrite(dict,1,118,f);
}

/* Append one packed-RGB frame: npy-direct file or a tick-numbered PPM. */
static int emit_frame(GmFrameCapture *c, const CrFramebuffer *fb, int tick) {
    if (c->npy_f) {
        int n=fb->w*fb->h;
        for(int i=0;i<n;++i){
            c->ppm_buf[i*3+0]=fb->color[i].r;
            c->ppm_buf[i*3+1]=fb->color[i].g;
            c->ppm_buf[i*3+2]=fb->color[i].b;
        }
        if(fwrite(c->ppm_buf,3,(size_t)n,c->npy_f)!=(size_t)n)return 0;
        c->npy_frames++;
        return 1;
    }
    char path[1200];
    int len=snprintf(path,sizeof path,"%s/frame_%06d.ppm",c->dir,tick);
    return len>=0&&len<(int)sizeof path&&write_ppm(path,fb,c->ppm_buf);
}

GmFrameCapture *gm_frame_capture_open(const GmConfig *cfg, char *err, int err_cap) {
    if(!cfg||!cfg->frames_out_dir){set_error(err,err_cap,"invalid frame capture config");return NULL;}
    size_t fol=strlen(cfg->frames_out_dir);
    if(fol>=sizeof(((GmFrameCapture *)0)->dir)){
        set_error(err,err_cap,"frames-out path is too long");return NULL;
    }
    int npy=fol>4&&strcmp(cfg->frames_out_dir+fol-4,".npy")==0;
    if(!npy){
        if(mkdir(cfg->frames_out_dir,0775)!=0&&errno!=EEXIST){
            if(err&&err_cap>0)snprintf(err,(size_t)err_cap,"cannot create frames-out directory: %s",strerror(errno));
            return NULL;
        }
        struct stat st;
        if(stat(cfg->frames_out_dir,&st)!=0||!S_ISDIR(st.st_mode)){
            set_error(err,err_cap,"frames-out path is not a directory");return NULL;
        }
    }
    GmFrameCapture *c=calloc(1,sizeof *c);if(!c){set_error(err,err_cap,"frame capture allocation failed");return NULL;}
    strcpy(c->dir,cfg->frames_out_dir);
    c->lm_mode=worldmc_lightmap_mode();
    const CrCaps *caps=cr_caps();c->max_tris=caps->max_tris;c->max_entity_verts=caps->ent_max_verts;
    cr_fb_alloc(&c->fb,cfg->width,cfg->height);
    c->tris=malloc((size_t)c->max_tris*sizeof *c->tris);
    for(int i=0;i<GM_FC_ENT_BUFS;++i)
        c->entity_verts[i]=malloc((size_t)c->max_entity_verts*sizeof *c->entity_verts[i]);
    c->ppm_buf=malloc((size_t)cfg->width*(size_t)cfg->height*3);
    c->post_scratch=malloc((size_t)cfg->width*(size_t)cfg->height*sizeof *c->post_scratch);
    if(!c->fb.color||!c->tris||!c->entity_verts[0]||!c->entity_verts[1]||
       !c->entity_verts[2]||!c->entity_verts[3]||!c->ppm_buf||!c->post_scratch){set_error(err,err_cap,"frame capture allocation failed");gm_frame_capture_close(c);return NULL;}
    if(npy){
        c->npy_f=fopen(c->dir,"wb");
        if(!c->npy_f){
            if(err&&err_cap>0)snprintf(err,(size_t)err_cap,"cannot open frames-out npy: %s",strerror(errno));
            gm_frame_capture_close(c);return NULL;
        }
        npy_stamp_header(c->npy_f,0,c->fb.h,c->fb.w);
    }
#ifdef MAGMA_METAL
    c->use_cuda=cfg->backend==GM_BACKEND_METAL; /* use_cuda == "GPU raster on" */
#else
    c->use_cuda=cfg->backend==GM_BACKEND_CUDA;
#endif
    if(c->use_cuda){
        if(!cr_raster_cuda_pre||!cr_raster_cuda_into||!cr_raster_cuda_frame_begin||
           !cr_raster_cuda_frame_end||!cr_raster_cuda_post){
#ifdef MAGMA_METAL
            set_error(err,err_cap,"Metal frame capture unavailable in this binary");gm_frame_capture_close(c);return NULL;
#else
            set_error(err,err_cap,"CUDA frame capture unavailable in this binary");gm_frame_capture_close(c);return NULL;
#endif
        }
        cr_raster_cuda_pre(c->fb.w,c->fb.h,c->max_tris);
        /* device-resident chunk meshes: mirror world_live's slab pool on the
         * GPU; falls back to the host-concat path if alloc fails or
         * MAGMA_NO_DEVMESH is set. */
        if(cr_raster_cuda_slab_pool&&cr_raster_cuda_slab_sync&&
           cr_raster_cuda_render_gather&&!getenv("MAGMA_NO_DEVMESH")&&
           cr_raster_cuda_slab_pool(caps->mesh_slots,caps->max_verts_per_chunk)){
            c->mesh_slots=caps->mesh_slots;c->slab_cap=caps->max_verts_per_chunk;
            c->chunks=malloc((size_t)caps->mesh_slots*sizeof *c->chunks);
            c->gsrc=malloc(4*(size_t)caps->mesh_slots*sizeof *c->gsrc);
            c->gcnt=malloc(4*(size_t)caps->mesh_slots*sizeof *c->gcnt);
            c->dev_mesh=c->chunks&&c->gsrc&&c->gcnt;
            c->merge_layers=c->dev_mesh&&cr_raster_cuda_render_terrain&&
                            !getenv("MAGMA_NO_LAYERMERGE");
        }
        if(cr_raster_cuda_pin){
            /* page-lock the per-frame H2D/D2H buffers (see raster_cuda.cu) */
            size_t npix=(size_t)c->fb.w*(size_t)c->fb.h;
            cr_raster_cuda_pin(c->fb.color,npix*sizeof *c->fb.color);
            cr_raster_cuda_pin(c->fb.depth,npix*sizeof *c->fb.depth);
            cr_raster_cuda_pin(c->tris,(size_t)c->max_tris*sizeof *c->tris);
            /* deferred-frame readback target (MAGMA_NO_DEFER=1 disables) */
            if(cr_raster_cuda_frame_end_async&&cr_raster_cuda_frame_wait&&
               !getenv("MAGMA_NO_DEFER")){
                c->pend_color=malloc(npix*sizeof *c->pend_color);
                if(c->pend_color)
                    cr_raster_cuda_pin(c->pend_color,npix*sizeof *c->pend_color);
            }
            /* depth-1 pipeline: prep+enqueue N+1 before waiting out N.
             * Needs devmesh (host-concat vert buffers are not double-
             * buffered), the deferred readback, and the uploads event.
             * Overlay (selection+crack, default ON) draws from one static
             * vert buffer - torn-upload risk - so it forces serial order
             * unless MAGMA_NO_OVERLAY is set. */
            c->pipeline=c->dev_mesh&&c->pend_color&&
                        cr_raster_cuda_uploads_mark&&cr_raster_cuda_uploads_wait&&
                        !getenv("MAGMA_NO_PIPELINE")&&getenv("MAGMA_NO_OVERLAY");
            if(c->pipeline){
                c->pend_depth=malloc(npix*sizeof *c->pend_depth);
                if(!c->pend_depth)c->pipeline=0;
            }
        }
    }
    return c;
}

/* Advance the two ItemRenderer/EntityLivingBase values which affect the hand
 * transform. Vanilla swingArm may restart once swingProgressInt reaches half
 * of the six-tick animation; held block damage calls it every client tick.
 * updateEquippedItem lowers by 0.4/tick, swaps the retained stack below 0.1,
 * then raises it by 0.4/tick. */
static void advance_hand_state(GmFrameCapture *c, const GmRuntime *r,
                               const GmPlayerView *view,
                               const GmAction *action, float *swing,
                               float *equip) {
    int attack = action && (action->attack || action->do_break);
    int cooldown_reset = 0;
    if (view) {
        if (c->attack_cooldown_initialized &&
            view->attack_cooldown + 1e-6f < c->prev_attack_cooldown)
            cooldown_reset = 1;
        c->prev_attack_cooldown = view->attack_cooldown;
        c->attack_cooldown_initialized = 1;
    }
    /* `attack` is held-key state, not swingProgress. Entity/miss clicks swing
     * on the press edge; newer tapes expose later clicks as cooldown resets;
     * a held dig re-swings on EVERY damaged tick (sendClickBlockToController),
     * which is what makes the arm keep cycling instead of freezing. */
    if (((attack && !c->prev_attack) || cooldown_reset ||
         gm_player_dig_swing()) &&
        (!c->swing_active || c->swing_progress_int >= 3 ||
         c->swing_progress_int < 0)) {
        c->swing_progress_int = -1;
        c->swing_active = 1;
    }
    if (c->swing_active) {
        if (++c->swing_progress_int >= 6) {
            c->swing_progress_int = 0;
            c->swing_active = 0;
        }
    } else {
        c->swing_progress_int = 0;
    }
    c->prev_attack = attack;
    *swing = (float)c->swing_progress_int / 6.0f;

    int sel = r->player.inv.current_item;
    if (sel < 0) sel = 0;
    if (sel > 8) sel = 8;
    const IsrInv *inv = r->tape_inv_active ? &r->tape_inv : &r->player.inv;
    ICStack held = isr_get_stack(inv, sel);
    int same = ((held.item == 0 && c->equip_item == 0) ||
                (sel == c->equip_slot && held.item == c->equip_item));
    float cd = view ? view->attack_cooldown : 1.0f;
    if (cd < 0.0f) cd = 0.0f;
    if (cd > 1.0f) cd = 1.0f;
    float target = same ? cd * cd * cd : 0.0f;
    float delta = target - c->equip_progress;
    if (delta < -0.4f) delta = -0.4f;
    if (delta > 0.4f) delta = 0.4f;
    c->equip_progress += delta;
    if (c->equip_progress < 0.1f) {
        c->equip_item = held.item;
        c->equip_meta = held.meta;
        c->equip_count = held.count;
        c->equip_slot = sel;
    } else if (same) {
        /* Count/damage mutate the retained ItemStack object in place. */
        c->equip_meta = held.meta;
        c->equip_count = held.count;
    }
    *equip = 1.0f - c->equip_progress;
}

/* MAGMA_HIDE_GUI means "the recorder did not draw the overlay pass", and the
 * name is a historical misnomer worth knowing about before you reason from it.
 * The goldens of 20260712T055346Z have no hearts, hotbar or crosshair on any of
 * their 157 frames, against all 181 of 20260721T215812Z which do, and that was
 * first read as Malmo's ClientStateMachine forcing gameSettings.hideGUI. It is
 * not: that tape's own meta records qrl_launch.hide_gui FALSE with
 * strip.overlays TRUE, i.e. the recorder skipped the overlay draw itself.
 *
 * The distinction is not academic, because vanilla's hideGUI hides more than
 * the overlay. EntityRenderer.renderHand gates renderItemInFirstPerson on
 * (thirdPersonView == 0 && !sleeping && !hideGUI && !spectator), and
 * GuiIngame.renderPortal is called from inside renderGameOverlay. Under a real
 * hideGUI the hand and the portal wash go too; under "overlays stripped" only
 * the overlay does, and the goldens confirm the latter - they draw a
 * first-person held item throughout (a block filling the lower-right corner
 * over t=600..1340, the wooden shovel from t=2440). So the portal wash is
 * still correctly tied to this flag, since it is drawn inside the pass the
 * recorder skipped, but hiding the HAND is a deliberate workaround, not
 * fidelity: see hand_hidden below.
 * What no version of this touches is itemRenderer.renderOverlays
 * (block-in-hand, water, fire) and hurtCameraEffect, which sit outside the
 * hideGUI branch a few lines further down.
 *
 * One more recorder setting matters here: that meta has options.bobView FALSE,
 * so the oracle's held item does not bob. It sits at exactly the same screen
 * pixels frame after frame, which is what let a 60 degree yaw sweep prove the
 * corner object was a viewmodel and not terrain. */
static int hud_hidden(void) {
    const char *s = getenv("MAGMA_HIDE_GUI");
    return s && *s && *s != '0';
}

/* Suppressing the hand is a WORKAROUND for a magma bug, and the only honest
 * way to describe it. The oracle draws a viewmodel on nearly every frame of
 * 20260712T055346Z, and on most of them it is the BARE ARM - it is empty
 * handed. magma has that path and puts it on the right pixels; it draws it far
 * too bright, so no hand currently scores better than our hand. That is the
 * whole justification, and it disappears when the arm's shading is fixed.
 * MAGMA_HAND_FROM_TICK is therefore NOT "the tick the oracle's hand comes
 * back" - the oracle has one throughout. It is the first golden at which OUR
 * viewmodel is known to be right. Forcing it on for the whole tape moves 110
 * of 157 frames the wrong way on the gate-independent whole mean/ch and only
 * 10 the right way. The over-brightness is measured on rain-free frames where
 * terrain and sky agree to 1/255: at t=900 the arm is golden (139,103,84)
 * against magma (192,173,148), a per-channel 0.72/0.60/0.58, which is a
 * lightmap colour and not a brightness scalar. See OPEN_DIVERGENCES.
 * The two set_inventory rows in the worldpatch sidecar (t=2274 crafting table,
 * t=2320 wooden shovel slot 6) are why 2440 in particular: it is the first
 * golden after magma's inventory is re-anchored, so it is the first frame
 * where a HELD item, not just the arm, is known right. An explicit
 * MAGMA_HAND_FROM_TICK in the environment overrides the sidecar so the arm can
 * be investigated without editing demo/, which is shared across worktrees. */
static int hand_hidden(long long tick) {
    const char *s;
    if (!hud_hidden()) return 0;
    s = getenv("MAGMA_HAND_FROM_TICK");
    if (!s || !*s) return 1;
    return tick < atoll(s);
}

/* Retire the deferred frame: wait for its readback, then draw hand/hud and
 * write the PPM from the pinned pending buffer. Returns write success. */
static int finish_pending(GmFrameCapture *c) {
    if (!c->pend_active) return 1;
    c->pend_active = 0;
    cr_raster_cuda_frame_wait();
    /* fb view over the pending color. Depth: hand clears whatever depth
     * buffer it gets before use and hud ignores it, but in pipeline mode the
     * next frame's fb.depth H2D may still be in flight, so hand/hud get the
     * dedicated pend_depth scratch instead of the live fb.depth. */
    CrFramebuffer pfb = c->fb;
    pfb.color = c->pend_color;
    if (c->pend_depth) pfb.depth = c->pend_depth;
    gm_hand_set_swing(c->pend_swing);
    gm_hand_set_equip(c->pend_equip);
    gm_hand_set_hurt(c->pend_v.hurt_time, c->pend_v.max_hurt_time,
                     c->pend_v.hurt_yaw);
    gm_hand_set_item_override(c->pend_item, c->pend_meta, c->pend_count);
    if (!c->pend_v.dead && !c->pend_v.riding_boat &&
        !getenv("MAGMA_NO_HAND") && !hand_hidden(c->pend_tick))
        gm_hand_draw(&pfb, &c->pend_v, c->pend_bob);
    /* ItemRenderer.renderOverlays: block, water, fire; then portal; then HUD. */
    /* Capture goldens are recorded with MixinPinTextureAnimations. Keep this
     * path on physical frame zero even when MAGMA_ANIM_TEXTURES is set. */
    bm_atlas_set_animation_physical_zero();
    bm_atlas_set_portal_frame(0);
    CrTexture atlas=bm_atlas();
    if (c->pend_bih)
        gm_overlay_block_in_hand_draw(&pfb, &atlas, c->pend_bih_id,
                                      c->pend_bih_meta);
    if (c->pend_uwov)
        gm_uw_overlay_draw(&pfb, &c->pend_v, c->pend_uwb, c->pend_uwfov);
    if (c->pend_v.fire && !c->pend_v.creative && !c->pend_v.dead)
        gm_hand_fire_overlay_draw(&pfb,&atlas,c->pend_fovscale);
    if (c->pend_v.portal > 0.0f && !hud_hidden()) {
        gm_overlay_portal_screen(&pfb,&atlas,c->pend_v.portal);
    }
    /* No open GUI on this path (see the gui_view note below), so the vanilla
     * condition reduces to !hideGUI. */
    if (!hud_hidden()) gm_hud_draw(&pfb, &c->pend_v);
    if (c->pend_v.loading) gm_overlay_loading_screen(&pfb);
    /* gui_view frames never defer (inventory must match this tick); see
     * gm_frame_capture_write. pend path has no open GUI to composite. */
    return emit_frame(c, &pfb, c->pend_frame);
}

int gm_frame_capture_write(GmFrameCapture *c, GmRuntime *r,
                           const GmAction *action, int render, char *err, int err_cap) {
    if(!c||!r){set_error(err,err_cap,"invalid frame capture state");return 0;}
    /* Per-tick animation state advances on EVERY call, rendered or not, so a
     * sparse capture (--frame-every) writes frames pixel-identical to an
     * every-tick run. c->frame counts calls == tick, keeping tick-numbered
     * filenames stable across capture cadences. */
    if(action&&fabsf(action->forward)+fabsf(action->strafe)>0.01f)
        c->hand_bob+=0.30f;
    GmPlayerView tick_v;
    gm_runtime_view(r,&tick_v);
    gm_runtime_apply_tape_view(r,&tick_v);
    float swing, equip;
    advance_hand_state(c, r, &tick_v, action, &swing, &equip);
    gm_hud_state_step(&c->hud_state,&tick_v,c->frame);
    /* fogColor1 smoother (light brightness at the player FEET): one vanilla
     * updateRenderer step per tick, rendered or not. updateRenderer runs
     * BEFORE the local player's movement update, so it samples the position
     * the tick started with - after the network phase (a tape pre-tick
     * set_pose is already in) but before this tick's own physics. Sampling
     * the post-tick feet instead starts every ramp one tick early: on
     * soulsand_ice t=60 (the step down onto soul sand, feet light 15 -> 0)
     * that is a 77%-of-frame 5.37 mean_abs mild_shift, and solving the two
     * goldens for the c1 they were drawn with gives 0.8548 (2 steps, the
     * tick-entry sample) against our 0.7967 (3 steps). The teleport tape
     * water_dive t=1000 pins the other side: its jump arrives as a pre-tick
     * set_pose, so its ramp does start on the row tick (implied 0.5771 == 4
     * steps from t=997), which a blanket one-tick lag would break. */
    {
        double fx,fy,fz;
        gm_runtime_tick_entry_feet(r,&fx,&fy,&fz);
        if(!c->fog_c1_init){
            /* The steady-state seed assumes the oracle client had been
             * running long enough before recstart for fogColor1 to converge.
             * On several scenario tapes it had NOT: the goldens ramp for the
             * first ~40 ticks at exactly the vanilla 0.1/tick rate while
             * magma is flat from t=0. MAGMA_FOG_C1_INIT overrides the seed so
             * that ramp can be measured; the real fix is for the recorder to
             * write the client's fogColor1 into the tape header, since the
             * warmup depends on the session, not on anything in the tape. */
            const char *s=getenv("MAGMA_FOG_C1_INIT");
            if(s&&*s)c->fog_c1=(float)atof(s);
            else c->fog_c1=gm_uw_fog_c1_seed(r->world,r->dimension,fx,fy,fz);
            c->fog_c1_init=1;
        }else{
            c->fog_c1=gm_uw_fog_c1_step(c->fog_c1,r->world,r->dimension,
                                        fx,fy,fz);
        }
    }
    if(!render){
        /* the dragon trail ring is per-tick vanilla state (getMovementOffsets
         * ring, pushed every onLivingUpdate): skipped-render ticks must still
         * push or every lookback lands frame_every ticks too far back and the
         * flying pose (body yaw, pitch roll, neck/tail) renders stale. */
        GmEntityView dv[GM_RUNTIME_GHOST_VIEWS];
        int dn=gm_dragon_fill_views(&r->dragon,dv,GM_RUNTIME_GHOST_VIEWS);
        dn+=gm_runtime_ghost_views(r,dv+dn,GM_RUNTIME_GHOST_VIEWS-dn);
        for(int i=0;i<dn;++i)if(dv[i].type==GM_ENTITY_DRAGON){
            gm_dragon_pose_tick(dv[i].ent_id,dv[i].yaw,dv[i].y,
                                dv[i].health);break;
        }
        c->frame++;return 1;
    }
    /* PIPELINE (depth-1): frame N is still rendering on the GPU. Wait only
     * for N's host-buffer uploads (fb + slabs, ~1ms into the frame), then do
     * ALL of N+1's CPU prep (lighting, meshing, emits) and enqueue its GPU
     * work behind N on the stream - the expensive CPU slice overlaps N's
     * raster. N's readback is consumed (finish_pending) only after that,
     * right before arming N+1's. Serial path: retire N fully up front. */
    if(c->pipeline){
        cr_raster_cuda_uploads_wait();
        c->ent_set^=1;
    }else if(!finish_pending(c)){
        set_error(err,err_cap,"cannot write frames-out image (deferred)");return 0;
    }
    GmPlayerView v=tick_v;
    /* gm_player_view already applies EntityPlayerSP's 1.62 -> 1.54 sneaking
     * eye height through psv_player_eye_height(prev_sneak). Do not lower the
     * camera a second time from the current action. */
    CrCamera cam=camera_for(&v,c->fb.w,c->fb.h);float day=time_of_day(r);
    /* eye-in-fluid state (fog / FOV / overlay - game/underwater.h) */
    GmUnderwater uw;gm_uw_eval(r->world,r->dimension,&v,c->fog_c1,&uw);
    cam.fov_deg*=uw.fov_scale;   /* getFOVModifier: 60/70 with the eye in water */
    gm_sky_set_fog_c1(c->fog_c1);  /* updateFogColor f13 on clear/view fog */
    /* orientCamera glTranslate(0,-eyeHeight,0): sky plane at 16-eyeH. */
    gm_sky_set_eye_height(v.eye_height > 0.01f ? v.eye_height : 1.62f);
    gm_sky_set_fluid_fog(uw.fluid?1:0,uw.fog01,uw.density);
    /* clearColor = updateFogColor result (view fog * fogColor1). Fluid path
     * already bakes fog_c1 into uw.fog_rgba. */
    CrRgba clear=gm_frame_clear_color(day,r->dimension,c->fog_c1,&uw);
    cr_fb_clear(&c->fb,clear);
    if(r->dimension==0&&c->use_cuda&&cr_raster_cuda_sky&&!getenv("MAGMA_CPU_SKY")){
        /* sky on the GPU: upload the cleared fb, then the kernel fills every
         * pixel (depth is still far everywhere - sky is the first draw).
         * Frame ctx + camera basis stay host/glibc (gm_sky_frame_args), so
         * day frames are bit-identical to gm_sky_draw; night star pixels can
         * differ by device sinf in hash21 (measured <=0.012%/frame). */
        cr_raster_cuda_frame_begin(&c->fb);
        GmSkyCtx sc;float bas[11];
        gm_sky_frame_args(&cam,day,&sc,bas);
        cr_raster_cuda_sky(&sc,bas,c->fb.w,c->fb.h);
    }else{
        if(r->dimension==0)gm_sky_draw(&c->fb,&cam,day);
        else if(r->dimension==1)gm_end_sky_draw(&c->fb,&cam);
        if(c->use_cuda)cr_raster_cuda_frame_begin(&c->fb);
    }
    /* Tape capture is permanently pinned, independently of the window-only
     * MAGMA_ANIM_TEXTURES flag and any legacy tape header value. */
    bm_atlas_set_animation_physical_zero();
    bm_atlas_set_portal_frame(0);
    if(c->use_cuda&&cr_raster_cuda_atlas_dirty)cr_raster_cuda_atlas_dirty();
    CrTexture atlas=gm_world_atlas(r->world);
    const CrRgba *lm=build_lightmap_lut(c,r);
    /* Viewmodel environment: renderHand fov (70 * eye-in-water 60/70), the
     * eye-block combined light (ItemRenderer.setLightmap), and the player
     * rotation the RenderHelper item lights are anchored under. */
    {
        int hx=(int)floorf(v.x),hy=(int)floorf(v.y+v.eye_height),hz=(int)floorf(v.z);
        int hsky=gm_world_sky_light(r->world,hx,hy,hz);
        int hblk=gm_world_block_light(r->world,hx,hy,hz);
        if(lm){
            gm_hand_set_env(lm,(float)hsky,(float)hblk,1.f,1.f,1.f,
                            uw.fov_scale,v.yaw,v.pitch);
        }else{
            CrLightmapRgb hc3=cr_lightmap_rgb(r->dimension,hsky,hblk,
                cr_dimension_sun_brightness(r->dimension),0.f,0.f);
            gm_hand_set_env(0,15.f,0.f,hc3.r,hc3.g,hc3.b,
                            uw.fov_scale,v.yaw,v.pitch);
        }
    }
    /* live-sim entities + tape-replay renderable ghosts (divergence #10:
     * replayed oracle entities render through the same model path). */
    enum { FC_ENTS = GM_LIVE_MAX + GM_RUNTIME_GHOST_VIEWS };
    GmEntityView ents[FC_ENTS];int n=gm_dragon_fill_views(&r->dragon,ents,FC_ENTS);
    n+=gm_mobs_fill_views(&r->mobs,ents+n,FC_ENTS-n);
    int n_proj0=n;
    n+=gm_runtime_projectile_views(r,ents+n,FC_ENTS-n);
    /* Dense list of active projectile types in the same order as views. */
    {
        int ptypes[GM_RUNTIME_PROJECTILES], npt=0;
        for(int i=0;i<GM_RUNTIME_PROJECTILES;++i)
            if(r->projectiles[i].active)
                ptypes[npt++]=r->projectiles[i].type;
        gm_entity_patch_large_fireballs(ptypes,npt,ents+n_proj0,n-n_proj0);
    }
    {
        int tape_falling = 0;
        for (int i = 0; i < r->nghost_views; ++i)
            if (r->ghost_views[i].type == GM_VIEW_FALLING_BLOCK) {
                tape_falling = 1;
                break;
            }
        /* EntityItem keeps the established coexistence path: independent
         * local drops and tape ghosts are both appended. A falling block is
         * the same server entity simulated locally for world truth, so its
         * oracle ent_view owns replay pose and the live copy is sim-only. */
        n+=gm_live_fill_views_filtered(&r->entities,ents+n,FC_ENTS-n,
                                       tape_falling);
    }
    n+=gm_runtime_ghost_views(r,ents+n,FC_ENTS-n);
    /* Live dragon fill omits death_ticks; copy from sim so dissolve/rays run. */
    if(r->dragon.initialized){
        int dt=r->dragon.state.arena.dragon.death_ticks;
        for(int i=0;i<n;++i)if(ents[i].type==GM_ENTITY_DRAGON){
            if(ents[i].death_ticks<=0&&dt>0)ents[i].death_ticks=dt;
        }
    }
    /* GuiBossOverlay is world-scoped (BossInfo packets), not proximity.
     * DragonFightManager constructs bossInfo with setCreateFog(true)
     * (DragonFightManager.java:54); clients in VALID_PLAYER range keep that
     * entry while !dragonKilled (BossInfoServer.setVisible). EntityRenderer
     * setupFog then pulls the linear ramp to [far*0.05, min(far,192)*0.5]
     * (EntityRenderer.java:2044-2047) - independent of dragon proximity.
     * Magma has no BossInfo packets: latch createFog on a live dragon OR any
     * ender crystal (tape ghosts map EntityEnderCrystal -> type 31, live fill
     * uses GM_ENTITY_CRYSTAL=8). The nearest-8 window drops a far dragon for
     * most of this fight; crystals stay in the list and keep fog armed. */
    /* ... and it goes away for good the moment the dragon dies:
     * processDragonDeath does bossInfo.setVisible(false) at deathTicks 200
     * (DragonFightManager.java:390-391), so the fog ramp snaps back to the
     * normal one on the tick the entity is removed. That is a ~4x brightness
     * step on everything 40 blocks out - most visibly the death cloud, which
     * the oracle shows washed to fog grey while the dragon lives and near
     * white afterwards. Keeping the latch armed left magma's cloud grey. */
    if(r->dimension!=1){c->boss_latch=0;c->dragon_killed=0;c->dragon_dying=0;}
    int dragon_seen=0;
    for(int i=0;i<n;++i)if(ents[i].type==GM_ENTITY_DRAGON){
        dragon_seen=1;
        if(ents[i].death_ticks>=200)c->dragon_killed=1;
        if(ents[i].death_ticks>0&&ents[i].health<=0.0f)c->dragon_dying=1;
        if(!c->boss_latch&&!c->dragon_killed){c->boss_latch=1;c->boss_frac=1.0f;}
        if(ents[i].health>=0.0f)c->boss_frac=ents[i].health/200.0f;break;
    }
    /* A dying dragon that leaves the view list has reached deathTicks 200. */
    if(!dragon_seen&&c->dragon_dying)c->dragon_killed=1;
    if(c->dragon_killed){c->boss_latch=0;c->boss_frac=0.0f;}
    else if(!c->boss_latch){
        for(int i=0;i<n;++i){
            /* 31 = ER_TYPE_CRYSTAL (tape/name map); 8 = GM_ENTITY_CRYSTAL. */
            if(ents[i].type==GM_ENTITY_CRYSTAL || ents[i].type==31){
                c->boss_latch=1;c->boss_frac=1.0f;break;
            }
        }
    }
    /* The fast oracle profile's MixinStripBossBar suppresses only HUD chrome;
     * BossInfo fog remains active. Replay passes the metadata-derived flag. */
    gm_hud_set_boss(c->boss_latch&&!getenv("MAGMA_STRIP_OVERLAYS"),c->boss_frac);
    GmMeshView mv;
    int dev_nv[4] = {0,0,0,0};
    int dev_nch = 0;
    if(c->dev_mesh){
        /* Slot rebuild counters are per-world; after a dimension switch the
         * new world's counters can collide with the cached ones and skip
         * uploads, so the GPU pool must forget the previous world's slabs. */
        if(r->world!=c->slab_world){
            if(cr_raster_cuda_slabs_reset)cr_raster_cuda_slabs_reset();
            c->slab_world=r->world;
        }
        dev_nch=gm_world_mesh_chunks(r->world,&cam,c->fb.w,c->fb.h,
                                     c->chunks,c->mesh_slots,dev_nv);
        render_world_dev_layers(c,&cam,dev_nch,dev_nv,&atlas,clear,
                                r->dimension,c->boss_latch,lm,&uw,
                                CR_LAYER_SOLID,CR_LAYER_TRANSLUCENT);
    }else{
        gm_world_mesh_view(r->world,&cam,c->fb.w,c->fb.h,&mv);
        render_world_layers(c,&cam,&mv,&atlas,clear,r->dimension,
                            c->boss_latch,lm,&uw,CR_LAYER_SOLID,
                            CR_LAYER_TRANSLUCENT);
    }
    /* RenderMinecart reprojects the entity onto its rail with getPos(), then
     * derives render yaw from getPosOffset(+/-0.3), rather than using the
     * recorded Entity.rotationYaw. Reconstruct the two flat rail directions
     * before both the cart and its display tile are emitted. */
    gm_frame_prepare_minecarts(ents,n,r->world);
    /* world light at each entity's eye block: shared with the window loop. */
    gm_frame_entities_light(ents,n,r->world,r->dimension,lm);
    if(n>0){
        /* one rotating buffer per emit: async CUDA uploads may still be in
         * flight until frame_end, so same-frame emits must not alias.
         * eb selects this frame's 4-buffer set (pipeline parity). */
        CrVertex **eb=&c->entity_verts[c->ent_set*4];
        gm_entity_geom_tick(c->frame);
        int nv=gm_entities_emit(ents,n,eb[0],c->max_entity_verts);
        gm_particles_dragon_latch(r->tick,ents,n);
        nv+=gm_particles_emit_filtered(
            ents,n,v.yaw,v.pitch,
            gm_particles_live_suppresses_explosion(c->particles),
            eb[0]+nv,c->max_entity_verts-nv);
        CrTexture ea=gm_entity_atlas();
        /* fog entities like terrain: underwater EXP fog so distant squid don't
         * punch through as bright unfogged blobs (vanilla setupFog applies to
         * the whole scene, including RenderLivingBase). Lightmap so outdoor
         * sheep wool is not fullbright white (updateLightmap noon-ish).
         * alpha_mask: per-texel dragon dissolve via dragon_exploding. */
        CrShadeCtx sh={0};
        sh.atlas=&ea; sh.fog_color=clear; sh.alpha_test=1;
        sh.layer=CR_LAYER_CUTOUT;
        sh.lightmap=lm;
        sh.alpha_mask=1;
        sh.entity_brightness=1;
        gm_entity_dissolve_mask(&sh.mask_u_off,&sh.mask_v_off);
        /* setupFog is scene state, so RenderLivingBase draws under the same
         * linear ramp as the terrain (dense in the Nether / during a boss
         * fight). Without it a dissolving dragon 45 blocks out kept its full
         * white instead of washing to the End fog color. */
        gm_frame_world_fog_params(r->dimension,c->boss_latch,&sh.enable_fog,
                                  &sh.fog_start,&sh.fog_end);
        if(uw.fluid){ sh.enable_fog=1; sh.fog_exp_density=uw.density; sh.fog_color=uw.fog_rgba; }
        render_layer(c,&cam,eb[0],nv,&sh);
        /* RenderXPOrb: SRC_ALPHA blend, alpha 128, no cutout thr 0.5 kill. */
        {
            static CrVertex xp_ov[512];
            int nx=gm_xp_orbs_emit(ents,n,v.yaw,v.pitch,xp_ov,512);
            if(nx>0){
                CrShadeCtx xp={0};
                xp.atlas=&ea; xp.fog_color=clear;
                xp.alpha_test=1; xp.alpha_ref=0.1f;
                xp.layer=CR_LAYER_TRANSLUCENT; xp.blend=1;
                xp.lightmap=lm;
                if(uw.fluid){ xp.enable_fog=1; xp.fog_exp_density=uw.density; xp.fog_color=uw.fog_rgba; }
                render_layer(c,&cam,xp_ov,nx,&xp);
            }
        }
        /* LayerSlimeGel + LayerEnderDragonDeath use dedicated host buffers so
         * async CUDA uploads of eb[*] are not overwritten mid-flight. */
        {
            static CrVertex gel_ov[4096];
            static CrVertex ray_ov[8192];
            int ng=gm_slime_gel_emit(ents,n,gel_ov,4096);
            if(ng>0){
                CrShadeCtx gel={0};
                gel.atlas=&ea; gel.fog_color=clear;
                gel.alpha_test=1; gel.alpha_ref=0.1f; /* living GL_GREATER 0.1 */
                gel.layer=CR_LAYER_TRANSLUCENT; gel.blend=4; /* depth write */
                if(uw.fluid){ gel.enable_fog=1; gel.fog_exp_density=uw.density; gel.fog_color=uw.fog_rgba; }
                render_layer(c,&cam,gel_ov,ng,&gel);
            }
            int nr=gm_dragon_death_rays_emit(ents,n,ray_ov,8192);
            if(nr>0){
                CrShadeCtx rays={0};
                rays.atlas=&ea; rays.fog_color=clear;
                rays.untextured=1; rays.blend=3;
                rays.layer=CR_LAYER_TRANSLUCENT;
                /* lightmap unit 1 is never disabled by the layer (see
                 * gm_dragon_death_rays_emit): keep it bound here too. */
                rays.lightmap=lm;
                /* setupFog is scene state and the death fans are drawn inside
                 * the entity pass, so the SAME linear ramp applies. In the End
                 * the fight's BossInfo.createFog is live for the whole death
                 * animation (MixinStripBossBar cancels only the HUD draw, not
                 * the BossInfo), pulling the ramp to [6.4, 64]: the dragon is
                 * 39-54 blocks out over this tape, i.e. 57-83% fogged.
                 * Unfogged, the additive fans came out several times too
                 * bright and read as longer and wider than the oracle's. */
                gm_frame_world_fog_params(r->dimension,c->boss_latch,
                                          &rays.enable_fog,
                                          &rays.fog_start,&rays.fog_end);
                if(uw.fluid){ rays.enable_fog=1; rays.fog_exp_density=uw.density;
                              rays.fog_color=uw.fog_rgba; }
                render_layer(c,&cam,ray_ov,nr,&rays);
            }
            /* RenderDragon.renderCrystalBeams. Own pass, not appended to the
             * entity buffer, because the entity pass runs alpha_mask=1 (the
             * dragon dissolve mask) and that would punch holes in the beam. */
            static CrVertex beam_ov[16384];
            int nb=gm_crystal_beams_emit(ents,n,beam_ov,16384);
            if(nb>0){
                CrShadeCtx bm={0};
                bm.atlas=&ea; bm.fog_color=clear;
                bm.alpha_test=1; bm.alpha_ref=0.1f;
                bm.layer=CR_LAYER_CUTOUT;
                bm.lightmap=lm;
                gm_frame_world_fog_params(r->dimension,c->boss_latch,
                                          &bm.enable_fog,
                                          &bm.fog_start,&bm.fog_end);
                if(uw.fluid){ bm.enable_fog=1; bm.fog_exp_density=uw.density;
                              bm.fog_color=uw.fog_rgba; }
                render_layer(c,&cam,beam_ov,nb,&bm);
            }
        }
        /* dropped items + falling blocks: block cubes/plants on the TERRAIN
         * atlas, then non-block items on the item atlas. */
        nv=gm_items_emit(ents,n,eb[1],c->max_entity_verts);
        nv+=gm_falling_blocks_emit(ents,n,eb[1]+nv,c->max_entity_verts-nv);
        nv+=gm_minecart_contents_emit(ents,n,eb[1]+nv,c->max_entity_verts-nv);
        if(nv>0){
            CrShadeCtx ish={0};
            ish.atlas=&atlas; ish.fog_color=clear; ish.alpha_test=1;
            ish.layer=CR_LAYER_CUTOUT;
            if(uw.fluid){ ish.enable_fog=1; ish.fog_exp_density=uw.density; ish.fog_color=uw.fog_rgba; }
            render_layer(c,&cam,eb[1],nv,&ish);
        }
        nv=gm_items_emit_flat(ents,n,eb[2],c->max_entity_verts);
        nv+=gm_held_items_emit(ents,n,eb[2]+nv,c->max_entity_verts-nv);
        nv+=gm_items_emit_billboard(ents,n,v.yaw,v.pitch,eb[2]+nv,
                                    c->max_entity_verts-nv);
        if(nv>0){
            CrTexture ia=gm_item_atlas();
            CrShadeCtx fsh={0};
            fsh.atlas=&ia; fsh.fog_color=clear; fsh.alpha_test=1;
            fsh.layer=CR_LAYER_CUTOUT;
            if(uw.fluid){ fsh.enable_fog=1; fsh.fog_exp_density=uw.density; fsh.fog_color=uw.fog_rgba; }
            render_layer(c,&cam,eb[2],nv,&fsh);
        }
        /* Render.doRenderShadowAndFire: small width.3125 / large width 1.0. */
        gm_entity_prep_large_fireball_fire(ents,n);
        nv=gm_small_fireball_fire_emit(ents,n,v.yaw,eb[3],
                                      c->max_entity_verts);
        gm_entity_restore_large_fireball_types(ents,n);
        /* ... and for living entities whose recorded flags say isBurning()
         * (a charged blaze is engulfed: EntityBlaze.isBurning -> isCharged). */
        nv+=gm_entity_fire_emit(ents,n,v.yaw,eb[3]+nv,
                                c->max_entity_verts-nv);
        if(nv>0){
            CrShadeCtx fire_sh={0};
            fire_sh.atlas=&atlas; fire_sh.fog_color=clear;
            fire_sh.alpha_test=1; fire_sh.layer=CR_LAYER_CUTOUT;
            if(uw.fluid){ fire_sh.enable_fog=1; fire_sh.fog_exp_density=uw.density; fire_sh.fog_color=uw.fog_rgba; }
            render_layer(c,&cam,eb[3],nv,&fire_sh);
        }
    }
    /* EntityRenderer.renderWorldPass draw order: renderEntities, then
     * drawSelectionBox, then the particle passes, then drawBlockDamageTexture.
     * These used to run BEFORE the entity/item passes, so a dropped item could
     * overdraw the crack decal entirely (the decal emitted verts and changed
     * zero pixels on those ticks). Dig particles also sat inside the n>0
     * entity block, so digging with no entities in range drew none at all. */
    if(!getenv("MAGMA_NO_OVERLAY")&&!v.dead){
        /* GPU uploads are asynchronous until frame_end. Selection and crack
         * therefore need distinct pinned host buffers: reusing one here lets
         * the crack emit overwrite selection vertices still in flight. */
        static CrVertex sel_ov[GM_OVERLAY_MAX_VERTS];
        int hx=0,hy=0,hz=0,ax,ay,az;
        int have_sel=gm_raycast_sel(r->window,&r->sin_table,&r->player,
                                    &hx,&hy,&hz,&ax,&ay,&az)>=0;
        float selb[6];
        if(have_sel)gm_sel_box_at(r->window,hx,hy,hz,selb);
        /* Selection: SRC_ALPHA/ONE_MINUS_SRC_ALPHA (blend=1). Crack:
         * DST_COLOR/SRC_COLOR multiply-2x (blend=2). Separate passes -
         * vanilla RenderGlobal draws them with different blend state. */
        if(have_sel){
            int ns=gm_overlay_emit_sel(sel_ov,GM_OVERLAY_MAX_VERTS,
                                       hx+r->ox,hy,hz+r->oz,selb,
                                       cam.pos.x,cam.pos.y,cam.pos.z);
            if(ns>0){
                CrShadeCtx osh = {0};
                osh.atlas = &atlas;
                osh.fog_color = clear;
                osh.alpha_test = 0;
                osh.enable_fog = 0;
                osh.layer = CR_LAYER_TRANSLUCENT;
                osh.blend = 1;
                osh.depth_lequal = 1;
                render_layer(c,&cam,sel_ov,ns,&osh);
            }
        }
    }
    /* Recorded explosion particles use the real ParticleManager render
     * classes and outlive the entity/event row that spawned them. */
    if(c->particles){
        /* Keep this host buffer alive until the asynchronous CUDA upload ends. */
        static CrVertex pcl0_ov[GM_PARTICLES_LIVE_CAP*6];
        CrTexture ea=gm_entity_atlas();
        int nv=gm_particles_live_emit_recorded(
            c->particles,0,1.0f,v.yaw,v.pitch,pcl0_ov,
            GM_PARTICLES_LIVE_CAP*6);
        if(nv>0){
            CrShadeCtx ps={0};ps.atlas=&ea;ps.fog_color=clear;
            ps.alpha_test=1;ps.alpha_ref=0.003921569f;
            ps.layer=CR_LAYER_TRANSLUCENT;ps.blend=1;ps.lightmap=lm;
            gm_frame_world_fog_params(r->dimension,c->boss_latch,
                                      &ps.enable_fog,&ps.fog_start,&ps.fog_end);
            if(uw.fluid){ps.enable_fog=1;ps.fog_exp_density=uw.density;ps.fog_color=uw.fog_rgba;}
            render_layer(c,&cam,pcl0_ov,nv,&ps);
        }
    }
    /* Dig particles: terrain-atlas model particle icons (ParticleDigging).
     * dig_state is window-local; add r->ox/oz like the dig-crack overlay. */
    {
        int dx,dy,dz,dface=-1;float dmg;
        if(gm_player_dig_state_ex(&dx,&dy,&dz,&dmg,&dface)&&dmg>0.0f){
            int stage=(int)(dmg*10.0f);if(stage<1)stage=1;if(stage>10)stage=10;
            int wx=dx+r->ox, wz=dz+r->oz;
            /* emit takes a MODEL KEY (CB_/PB_ space), not the vanilla id
             * gm_world_block returns (grass -> water_flow otherwise). */
            int bid=gm_state_to_model_key(gm_pack_state(
                gm_world_block(r->world,wx,dy,wz),
                gm_world_meta(r->world,wx,dy,wz)&15));
            static CrVertex dig_ov[1024];
            /* ParticleDigging.getBrightnessForRender: light at the particle
             * (spawned just outside the hit face). Sample the face-neighbour
             * air cell when known; fall back to the dig cell. */
            int lx=wx, ly=dy, lz=wz;
            if (dface==0) ly=dy-1; else if (dface==1) ly=dy+1;
            else if (dface==2) lz=wz-1; else if (dface==3) lz=wz+1;
            else if (dface==4) lx=wx-1; else if (dface==5) lx=wx+1;
            int dsky=gm_world_sky_light(r->world,lx,ly,lz);
            int dblk=gm_world_block_light(r->world,lx,ly,lz);
            CrLightmapRgb dlm=cr_lightmap_rgb(r->dimension,dsky,dblk,
                cr_dimension_sun_brightness(r->dimension),0.f,0.f);
            int nd=gm_block_break_particles_emit(
                wx,dy,wz,bid,stage,dface,
                gm_player_dig_particle_count(),
                v.yaw,v.pitch,dlm.r,dlm.g,dlm.b,dig_ov,1024);
            if(nd>0){
                CrTexture ta=gm_world_atlas(r->world);
                CrShadeCtx dig={0};
                dig.atlas=&ta; dig.fog_color=clear;
                dig.alpha_test=1; dig.layer=CR_LAYER_CUTOUT;
                if(uw.fluid){ dig.enable_fog=1; dig.fog_exp_density=uw.density; dig.fog_color=uw.fog_rgba; }
                render_layer(c,&cam,dig_ov,nd,&dig);
            }
        }
    }
    /* ParticleManager layer 3 follows terrain-atlas layer 1 particles. */
    if(c->particles){
        static CrVertex pcl3_ov[GM_PARTICLES_LIVE_CAP*6];
        int nv=gm_particles_live_emit_recorded(
            c->particles,3,1.0f,v.yaw,v.pitch,pcl3_ov,
            GM_PARTICLES_LIVE_CAP*6);
        if(nv>0){
            CrTexture ea=gm_entity_atlas();
            CrShadeCtx ps={0};ps.atlas=&ea;ps.fog_color=clear;
            ps.alpha_test=1;ps.layer=CR_LAYER_CUTOUT;ps.entity_brightness=1;
            gm_frame_world_fog_params(r->dimension,c->boss_latch,
                                      &ps.enable_fog,&ps.fog_start,&ps.fog_end);
            if(uw.fluid){ps.enable_fog=1;ps.fog_exp_density=uw.density;ps.fog_color=uw.fog_rgba;}
            render_layer(c,&cam,pcl3_ov,nv,&ps);
        }
    }
    if(!getenv("MAGMA_NO_OVERLAY")&&!v.dead){
        static CrVertex crack_ov[GM_OVERLAY_MAX_VERTS];
        int dx=0,dy=0,dz=0;float dmg=0.0f;
        int have_dig=gm_player_dig_state(&dx,&dy,&dz,&dmg);
        if(have_dig && dmg>0.0f && !getenv("MAGMA_NO_CRACK")){
            /* BlockRendererDispatcher.renderBlockDamage re-renders the full
             * block model with the destroy sprite, not only the raycast face. */
            int nc=gm_overlay_emit_crack(crack_ov,GM_OVERLAY_MAX_VERTS,
                                         dx+r->ox,dy,dz+r->oz,dmg,-1);
            if(getenv("MAGMA_LOG_DIG"))
                fprintf(stderr,"CRK t%lld face=%d nc=%d at %d,%d,%d cam %.1f,%.1f,%.1f\n",
                        r->tick,-1,nc,dx+r->ox,dy,dz+r->oz,
                        cam.pos.x,cam.pos.y,cam.pos.z);
            if(nc>0){
                CrShadeCtx csh = {0};
                csh.atlas = &atlas;
                csh.fog_color = clear;
                /* alphaFunc(GL_GREATER, 0.1F): discard a <= ~26. Our cutout
                 * threshold is 128 - tighter than vanilla, keeps only solid
                 * crack strokes (destroy_stage bg is a≈1 white). */
                csh.alpha_test = 1;
                csh.enable_fog = 0;
                csh.layer = CR_LAYER_CUTOUT;
                csh.blend = 2;           /* DST_COLOR, SRC_COLOR → 2*src*dst */
                csh.depth_lequal = 1;
                render_layer(c,&cam,crack_ov,nc,&csh);
            }
        }
    }
    /* EntityRenderer.renderWorldPass draws translucent terrain only after
     * entity pass 0, selection/damage overlays, and both particle groups. */
    if(c->dev_mesh)
        render_world_dev_layers(c,&cam,dev_nch,dev_nv,&atlas,clear,
                                r->dimension,c->boss_latch,lm,&uw,
                                CR_LAYER_TRANSLUCENT,
                                CR_LAYER_TRANSLUCENT+1);
    else
        render_world_layers(c,&cam,&mv,&atlas,clear,r->dimension,
                            c->boss_latch,lm,&uw,CR_LAYER_TRANSLUCENT,
                            CR_LAYER_TRANSLUCENT+1);
    /* Open GUI screen this tick (tape gui_view / divergence #9): force the
     * synchronous hand/hud/gui path so gm_screen_draw sees this tick's
     * inventory. Deferred readback would finish after later ticks mutate it. */
    int gui_kind = -1, gui_gmx = 0, gui_gmy = 0;
    int have_gui = gm_runtime_gui_view_get(r, &gui_kind, &gui_gmx, &gui_gmy);
    int force_sync = have_gui || v.loading==2;
    if(c->use_cuda){
        if(c->pend_color&&cr_raster_cuda_frame_end_async){
            /* Always retire the previous deferred frame first. */
            if(!finish_pending(c)){
                set_error(err,err_cap,"cannot write frames-out image (deferred)");return 0;
            }
            /* Skip arming a new deferral when a GUI must composite this tick. */
            if(!force_sync&&cr_raster_cuda_frame_end_async(&c->fb,c->pend_color)){
                c->pend_active=1;c->pend_frame=c->frame++;
                c->pend_v=v;c->pend_swing=swing;c->pend_equip=equip;
                c->pend_item=c->equip_item;c->pend_meta=c->equip_meta;
                c->pend_count=c->equip_count;c->pend_bob=c->hand_bob;
                c->pend_uwov=uw.overlay&&!v.dead;
                /* The sync path passes uw.fov_scale to the fire overlay, not
                 * cam.fov_deg/70: cam.fov_deg is 70 * fov_mult * fov_scale, so
                 * deriving the scale back out of it silently folds in
                 * getFovModifier's bow-pull / sprint term. That is the whole
                 * CPU-vs-CUDA divergence on fire+bow ticks. Snapshot both. */
                c->pend_uwb=uw.brightness;c->pend_uwfov=cam.fov_deg;
                c->pend_fovscale=uw.fov_scale;
                c->pend_tick=r->tick;
                /* Resolve now, while r->world is this frame's world. */
                c->pend_bih=!v.dead&&gm_overlay_block_in_hand_pick(
                    r->world,&v,&c->pend_bih_id,&c->pend_bih_meta);
                return 1;
            }
        }
        cr_raster_cuda_frame_end(&c->fb);
    }
    gm_hand_set_swing(swing);
    gm_hand_set_equip(equip);
    gm_hand_set_hurt(v.hurt_time, v.max_hurt_time, v.hurt_yaw);
    gm_hand_set_item_override(c->equip_item, c->equip_meta, c->equip_count);
    if(v.loading==2){
        /* GuiDownloadTerrain has closed, but the new player/chunk is not yet
         * renderable: vanilla shows only its sky/fog framebuffer + crosshair. */
        cr_fb_clear(&c->fb,clear);
        if(r->dimension==0)gm_sky_draw(&c->fb,&cam,day);
        else if(r->dimension==1)gm_end_sky_draw(&c->fb,&cam);
        if(!hud_hidden()||have_gui)gm_hud_draw(&c->fb,&v);
    }else{
        if(!v.dead&&!v.riding_boat&&!getenv("MAGMA_NO_HAND")&&!hand_hidden(r->tick))
            gm_hand_draw(&c->fb,&v,c->hand_bob);
        /* ItemRenderer.renderOverlays: block, water, fire; then portal; HUD. */
        if(!v.dead)gm_overlay_block_in_hand_live(&c->fb,&atlas,r->world,&v);
        if(uw.overlay&&!v.dead)gm_uw_overlay_draw(&c->fb,&v,uw.brightness,cam.fov_deg);
        if(v.fire&&!v.creative&&!v.dead)
            gm_hand_fire_overlay_draw(&c->fb,&atlas,uw.fov_scale);
        if(v.portal>0.0f&&(!hud_hidden()||have_gui))
            gm_overlay_portal_screen(&c->fb,&atlas,v.portal);
        if(!hud_hidden()||have_gui)gm_hud_draw(&c->fb,&v);
        if(v.loading==1)gm_overlay_loading_screen(&c->fb);
    }
    /* Container GUI over the finished frame (after HUD), same order as
     * interactive game_main. Temporarily set r->container so gm_screen_draw
     * picks the right panel; physics container field restored after. */
    if(have_gui && !v.dead){
        int mx_fb, my_fb, saved = r->container;
        IsrInv saved_inv = r->player.inv;
        gm_screen_mouse_to_fb(c->fb.w, c->fb.h, gui_gmx, gui_gmy, &mx_fb, &my_fb);
        r->container = gui_kind;
        if(r->tape_inv_active)r->player.inv=r->tape_inv;
        gm_screen_draw(&c->fb, r, mx_fb, my_fb);
        r->player.inv=saved_inv;
        r->container = saved;
    }
    if(!emit_frame(c,&c->fb,c->frame++)){
        set_error(err,err_cap,"cannot write frames-out image");
        return 0;
    }
    return 1;
}

void gm_frame_capture_close(GmFrameCapture *c) {
    if(!c)return;
    finish_pending(c);   /* flush the deferred last frame */
    if(c->npy_f){
        npy_stamp_header(c->npy_f,c->npy_frames,c->fb.h,c->fb.w);
        fclose(c->npy_f);
    }
    if(c->use_cuda&&cr_raster_cuda_unpin){
        cr_raster_cuda_unpin(c->fb.color);cr_raster_cuda_unpin(c->fb.depth);
        cr_raster_cuda_unpin(c->tris);
        if(c->pend_color)cr_raster_cuda_unpin(c->pend_color);
    }
    if(c->use_cuda&&cr_raster_cuda_post)cr_raster_cuda_post();
    free(c->tris);
    for(int i=0;i<GM_FC_ENT_BUFS;++i)free(c->entity_verts[i]);
    free(c->chunks);free(c->gsrc);free(c->gcnt);free(c->pend_color);
    free(c->pend_depth);free(c->post_scratch);
    free(c->ppm_buf);cr_fb_free(&c->fb);free(c);
}
