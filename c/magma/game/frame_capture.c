#include "game/frame_capture.h"

#include "assets/blockmodels.h"
#include "game/caps.h"
#include "game/dragon_live.h"
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

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

/* 2 x 3 rotating entity vert buffers: a frame emits entities, then items,
 * then flat items (3 buffers), and the pipelined path preps frame N+1 while
 * frame N's uploads may still be in flight - so N+1 uses the OTHER set of 3
 * (ent_set parity). Non-pipelined paths just always use set 0. */
#define GM_FC_ENT_BUFS 6

struct GmFrameCapture {
    CrFramebuffer fb;
    CrScreenTri *tris;
    CrVertex *entity_verts[GM_FC_ENT_BUFS];
    unsigned char *ppm_buf; /* packed RGB scratch, one fwrite per frame */
    int max_tris, max_entity_verts;
    int use_cuda;
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
    float pend_uwb;         /* ... with this Entity.getBrightness tint */
    float pend_uwfov;       /* ... through this hand-projection fov */
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

/* TextureMap.tick is a client clock, independent of World.totalTime.  Replays
 * record portal.frameCounter every tick; all atlas sprites start together, so
 * choose the latest matching client tick not after the recorded total time.
 * Live magma has no recorded portal frame and uses its own total tick directly. */
static long long fc_texture_tick(long long total_time, int portal_frame) {
    long long rem;
    int delta;
    if (portal_frame < 0) return total_time;
    portal_frame &= 31;
    rem = total_time % 32;
    if (rem < 0) rem += 32;
    delta = ((int)rem - portal_frame + 32) & 31;
    return total_time - delta;
}

/* The frame's lightmap texture: exact updateLightmap texels for the current
 * sun brightness (torch flicker + gamma pinned 0, matching the light state). */
static const CrRgba *build_lightmap_lut(GmFrameCapture *c, const GmRuntime *r) {
    if (!c->lm_mode || r->dimension != 0) return NULL;
    float sun = fc_sun_brightness(&r->sin_table, r->clock.world_time);
    for (int sl = 0; sl < 16; ++sl)
        for (int bl = 0; bl < 16; ++bl)
            c->lut[sl * 16 + bl] =
                cr_lightmap_rgba8(cr_lightmap_rgb(0, sl, bl, sun, 0.0f, 0.0f));
    return c->lut;
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
    c.zfar = 600.0f;
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

static void terrain_shades(const CrTexture *atlas, CrRgba fog, int dimension,
                           int boss_fog, const CrRgba *lm,
                           const GmUnderwater *uw, CrShadeCtx shade[4]) {
    int enabled = gm_terrain_fog_enabled();
    /* EntityRenderer.setupFog: Nether doesXZShowFog OR the dragon fight's
     * BossInfo createFog flag (GuiBossOverlay.shouldCreateFog) pull the
     * linear ramp in to [far*0.05, min(far,192)*0.5]. */
    int dense = dimension == -1 || boss_fog;
    float fog_start = dense ? GM_TERRAIN_FOG_FAR * 0.05f
                            : GM_TERRAIN_FOG_START;
    float fog_end = dense ? GM_TERRAIN_FOG_FAR * 0.5f
                          : GM_TERRAIN_FOG_END;
    /* use_mips=0 on EVERY layer, including CUTOUT_MIPPED: both oracle launch
     * profiles (java/fast.yaml + java/vanilla.yaml) pin mipmapLevels:0, which
     * makes TextureMap.setBlurMipmap give the terrain atlas plain GL_NEAREST
     * (no mip chain). Sampling magma's mip chain would shift texel
     * boundaries and average colors on minified faces the oracle samples at
     * level 0. The vanilla layer NAME only selects alpha handling; the
     * sampler state follows mipmapLevels. */
    CrShadeCtx s[4] = {
        {atlas,fog,fog_start,fog_end,0,enabled,CR_LAYER_SOLID,0,0,0.f},
        {atlas,fog,fog_start,fog_end,1,enabled,CR_LAYER_CUTOUT_MIPPED,0,0,0.f},
        {atlas,fog,fog_start,fog_end,1,enabled,CR_LAYER_CUTOUT,0,0,0.f},
        {atlas,fog,fog_start,fog_end,0,enabled,CR_LAYER_TRANSLUCENT,1,0,0.f},
    };
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

static void render_world(GmFrameCapture *c, const CrCamera *cam,
                         const GmMeshView *mv, const CrTexture *atlas,
                         CrRgba fog, int dimension, int boss_fog,
                         const CrRgba *lm, const GmUnderwater *uw) {
    CrShadeCtx shade[4];
    terrain_shades(atlas, fog, dimension, boss_fog, lm, uw, shade);
    for (int layer=0;layer<4;++layer)
        render_layer(c,cam,mv->verts[layer],mv->nverts[layer],&shade[layer]);
}

/* Device-resident terrain: upload rebuilt slabs, then per layer hand the
 * gather list (pool vert offsets, in the SAME chunk order mesh_view used to
 * concat) to the GPU. Byte-identical draw buffers, zero host vert memcpy. */
static void render_world_dev(GmFrameCapture *c, const CrCamera *cam,
                             int nch, const int nv[4], const CrTexture *atlas,
                             CrRgba fog, int dimension, int boss_fog,
                             const CrRgba *lm, const GmUnderwater *uw) {
    CrShadeCtx shade[4];
    terrain_shades(atlas, fog, dimension, boss_fog, lm, uw, shade);
    for (int i = 0; i < nch; ++i) {
        const GmChunkDraw *d = &c->chunks[i];
        cr_raster_cuda_slab_sync(d->slot, d->builds, d->slab,
                                 d->off[3] + d->n[3]);
    }
    /* last host-buffer H2D of the frame that reads game-side memory (fb +
     * slabs) is now enqueued: mark it so the NEXT frame's prep can wait on
     * this instead of the whole frame. */
    if (cr_raster_cuda_uploads_mark) cr_raster_cuda_uploads_mark();
    if (c->merge_layers) {
        /* One layer-major entry list -> single gather + raster chain. The
         * tiled kernel selects each tri's shade ctx by layer boundary, and
         * per-pixel tri order is unchanged: pixel-identical to 4 launches. */
        int ne = 0;
        for (int layer = 0; layer < 4; ++layer) {
            for (int i = 0; i < nch; ++i) {
                const GmChunkDraw *d = &c->chunks[i];
                if (d->n[layer] <= 0) continue;
                c->gsrc[ne] = d->slot * c->slab_cap + d->off[layer];
                c->gcnt[ne] = d->n[layer];
                ++ne;
            }
        }
        if (ne > 0)
            cr_raster_cuda_render_terrain(&c->fb, c->gsrc, c->gcnt, ne, nv,
                                          cam, shade);
        return;
    }
    for (int layer = 0; layer < 4; ++layer) {
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
       !c->entity_verts[2]||!c->ppm_buf||!c->post_scratch){set_error(err,err_cap,"frame capture allocation failed");gm_frame_capture_close(c);return NULL;}
    if(npy){
        c->npy_f=fopen(c->dir,"wb");
        if(!c->npy_f){
            if(err&&err_cap>0)snprintf(err,(size_t)err_cap,"cannot open frames-out npy: %s",strerror(errno));
            gm_frame_capture_close(c);return NULL;
        }
        npy_stamp_header(c->npy_f,0,c->fb.h,c->fb.w);
    }
    c->use_cuda=cfg->backend==GM_BACKEND_CUDA;
    if(c->use_cuda){
        if(!cr_raster_cuda_pre||!cr_raster_cuda_into||!cr_raster_cuda_frame_begin||
           !cr_raster_cuda_frame_end||!cr_raster_cuda_post){
            set_error(err,err_cap,"CUDA frame capture unavailable in this binary");gm_frame_capture_close(c);return NULL;
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
                               const GmAction *action, float *swing,
                               float *equip) {
    if (action && (action->attack || action->do_break) &&
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
    *swing = (float)c->swing_progress_int / 6.0f;

    int sel = r->player.inv.current_item;
    if (sel < 0) sel = 0;
    if (sel > 8) sel = 8;
    const IsrInv *inv = r->tape_inv_active ? &r->tape_inv : &r->player.inv;
    ICStack held = isr_get_stack(inv, sel);
    int same = ((held.item == 0 && c->equip_item == 0) ||
                (sel == c->equip_slot && held.item == c->equip_item));
    float target = same ? 1.0f : 0.0f;
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
    gm_hand_set_item_override(c->pend_item, c->pend_meta, c->pend_count);
    if (!c->pend_v.dead && !getenv("MAGMA_NO_HAND"))
        gm_hand_draw(&pfb, &c->pend_v, c->pend_bob);
    /* ItemRenderer.renderOverlays runs with the hand, BEFORE the HUD. */
    if (c->pend_uwov)
        gm_uw_overlay_draw(&pfb, &c->pend_v, c->pend_uwb, c->pend_uwfov);
    if (c->pend_v.portal > 0.0f) {
        bm_atlas_set_portal_frame(c->pend_v.portal_frame);
        CrTexture atlas=bm_atlas();
        gm_overlay_portal_screen(&pfb,&atlas,c->pend_v.portal);
    }
    gm_hud_draw(&pfb, &c->pend_v);
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
    float swing, equip;
    advance_hand_state(c, r, action, &swing, &equip);
    /* fogColor1 smoother (light brightness at the player FEET): one vanilla
     * updateRenderer step per tick, rendered or not. */
    {
        GmPlayerView fv;gm_runtime_view(r,&fv);
        if(!c->fog_c1_init){
            c->fog_c1=gm_uw_fog_c1_seed(r->world,r->dimension,fv.x,fv.y,fv.z);
            c->fog_c1_init=1;
        }else{
            c->fog_c1=gm_uw_fog_c1_step(c->fog_c1,r->world,r->dimension,
                                        fv.x,fv.y,fv.z);
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
            gm_dragon_pose_tick(dv[i].ent_id,dv[i].yaw,dv[i].y);break;
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
    GmPlayerView v;gm_runtime_view(r,&v);gm_runtime_apply_tape_view(r,&v);
    CrCamera cam=camera_for(&v,c->fb.w,c->fb.h);float day=time_of_day(r);
    /* eye-in-fluid state (fog / FOV / overlay - game/underwater.h) */
    GmUnderwater uw;gm_uw_eval(r->world,r->dimension,&v,c->fog_c1,&uw);
    cam.fov_deg*=uw.fov_scale;   /* getFOVModifier: 60/70 with the eye in water */
    gm_sky_set_fog_c1(c->fog_c1);  /* updateFogColor f13 on clear/view fog */
    gm_sky_set_fluid_fog(uw.fluid?1:0,uw.fog01,uw.density);
    /* clearColor = updateFogColor result (view fog * fogColor1). Fluid path
     * already bakes fog_c1 into uw.fog_rgba. */
    CrRgba clear=gm_terrain_fog_color(day);
    if(r->dimension==-1){
        clear.r=(u8)(0.20f*c->fog_c1*255.0f+0.5f);
        clear.g=(u8)(0.03f*c->fog_c1*255.0f+0.5f);
        clear.b=(u8)(0.03f*c->fog_c1*255.0f+0.5f);
        clear.a=255;
    }else if(r->dimension==1){
        /* updateFogColor, End: WorldProviderEnd.getFogColor is constant
         * (0.627451,0.5019608,0.627451)*0.15 (its celestial-angle term
         * clamps to 0 at the fixed angle 0.5), then blended
         * f = 1 - pow(0.25 + 0.75*rd/32, 0.25) toward the sky color, which
         * is BLACK in the End (getSkyColor's cos(angle*2pi)*2+0.5 clamps to
         * 0), then scaled by fogColor1 like every dimension. */
        float bf=1.0f-powf(0.25f+0.75f*(GM_TERRAIN_FOG_FAR/16.0f)/32.0f,0.25f);
        clear.r=(u8)(0.09411765f*(1.0f-bf)*c->fog_c1*255.0f+0.5f);
        clear.g=(u8)(0.07529412f*(1.0f-bf)*c->fog_c1*255.0f+0.5f);
        clear.b=(u8)(0.09411765f*(1.0f-bf)*c->fog_c1*255.0f+0.5f);
        clear.a=255;
    }
    if(uw.fluid)clear=uw.fog_rgba;
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
    /* TextureAtlasSprite.updateAnimation for water/lava/fire. */
    bm_atlas_set_animation_tick(fc_texture_tick(r->clock.total_time,
                                                 v.portal_frame));
    bm_atlas_set_portal_frame(v.portal_frame);
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
    n+=gm_runtime_projectile_views(r,ents+n,FC_ENTS-n);
    n+=gm_live_fill_views(&r->entities,ents+n,FC_ENTS-n);
    n+=gm_runtime_ghost_views(r,ents+n,FC_ENTS-n);
    /* GuiBossOverlay is world-scoped (BossInfo packets), not proximity: a
     * dragon sighting in dim 1 LATCHES the bar until the dimension changes;
     * the nearest-8 tape ents lose a far dragon for most of the fight. */
    if(r->dimension!=1)c->boss_latch=0;
    for(int i=0;i<n;++i)if(ents[i].type==GM_ENTITY_DRAGON){
        if(!c->boss_latch){c->boss_latch=1;c->boss_frac=1.0f;}
        if(ents[i].health>=0.0f)c->boss_frac=ents[i].health/200.0f;break;
    }
    /* The fast oracle profile's MixinStripBossBar suppresses only HUD chrome;
     * BossInfo fog remains active. Replay passes the metadata-derived flag. */
    gm_hud_set_boss(c->boss_latch&&!getenv("MAGMA_STRIP_OVERLAYS"),c->boss_frac);
    if(c->dev_mesh){
        /* Slot rebuild counters are per-world; after a dimension switch the
         * new world's counters can collide with the cached ones and skip
         * uploads, so the GPU pool must forget the previous world's slabs. */
        if(r->world!=c->slab_world){
            if(cr_raster_cuda_slabs_reset)cr_raster_cuda_slabs_reset();
            c->slab_world=r->world;
        }
        int nv[4];
        int nch=gm_world_mesh_chunks(r->world,&cam,c->fb.w,c->fb.h,
                                     c->chunks,c->mesh_slots,nv);
        render_world_dev(c,&cam,nch,nv,&atlas,clear,r->dimension,
                         c->boss_latch,lm,&uw);
    }else{
        GmMeshView mv;gm_world_mesh_view(r->world,&cam,c->fb.w,c->fb.h,&mv);
        render_world(c,&cam,&mv,&atlas,clear,r->dimension,c->boss_latch,
                     lm,&uw);
    }
    /* Selection outline + dig crack decal (default ON, matching vanilla
     * RenderGlobal.drawSelectionBox / drawBlockDamageTexture). Opt out with
     * MAGMA_NO_OVERLAY=1 for goldens that predate the overlay pass. */
    if(!getenv("MAGMA_NO_OVERLAY")&&!v.dead){
        /* GPU uploads are asynchronous until frame_end. Selection and crack
         * therefore need distinct pinned host buffers: reusing one here lets
         * the crack emit overwrite selection vertices still in flight. */
        static CrVertex sel_ov[GM_OVERLAY_MAX_VERTS];
        static CrVertex crack_ov[GM_OVERLAY_MAX_VERTS];
        int hx=0,hy=0,hz=0,ax,ay,az;
        int have_sel=gm_raycast_sel((const struct Chunk *)r->window,
                                    (const struct McSinTable *)&r->sin_table,
                                    (const struct PsvPlayer *)&r->player,
                                    &hx,&hy,&hz,&ax,&ay,&az)>=0;
        int dx=0,dy=0,dz=0;float dmg=0.0f;
        int have_dig=gm_player_dig_state(&dx,&dy,&dz,&dmg);
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
    /* world light at each entity's eye block (RenderLivingBase brightness):
     * lightmap mode passes the raw 0..15 levels through the LUT; the legacy /
     * Nether/End path (lm==NULL) folds the exact updateLightmap color into the
     * tint since the shader then treats vtx.light as a plain scalar. */
    for(int i=0;i<n;++i){
        int ex=(int)floorf(ents[i].x);
        int ey=(int)floorf(ents[i].y+gm_entity_eye_y(ents[i].type));
        int ez=(int)floorf(ents[i].z);
        int sky=gm_world_sky_light(r->world,ex,ey,ez);
        int bl=gm_world_block_light(r->world,ex,ey,ez);
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
            CrLightmapRgb c3=cr_lightmap_rgb(r->dimension,sky,bl,
                cr_dimension_sun_brightness(r->dimension),0.f,0.f);
            ents[i].lm_lit=2;
            ents[i].lm_mul_r=c3.r;ents[i].lm_mul_g=c3.g;ents[i].lm_mul_b=c3.b;
        }
    }
    if(n>0){
        /* one rotating buffer per emit: async CUDA uploads may still be in
         * flight until frame_end, so same-frame emits must not alias.
         * eb selects this frame's 3-buffer set (pipeline parity). */
        CrVertex **eb=&c->entity_verts[c->ent_set*3];
        gm_entity_geom_tick(c->frame);
        int nv=gm_entities_emit(ents,n,eb[0],c->max_entity_verts);
        CrTexture ea=gm_entity_atlas();
        /* fog entities like terrain: underwater EXP fog so distant squid don't
         * punch through as bright unfogged blobs (vanilla setupFog applies to
         * the whole scene, including RenderLivingBase). Lightmap so outdoor
         * sheep wool is not fullbright white (updateLightmap noon-ish). */
        CrShadeCtx sh={0};
        sh.atlas=&ea; sh.fog_color=clear; sh.alpha_test=1;
        sh.layer=CR_LAYER_CUTOUT;
        sh.lightmap=lm;
        if(uw.fluid){ sh.enable_fog=1; sh.fog_exp_density=uw.density; sh.fog_color=uw.fog_rgba; }
        render_layer(c,&cam,eb[0],nv,&sh);
        /* dropped items: block cubes/plants on the TERRAIN atlas, then
         * non-block items on the item atlas. */
        nv=gm_items_emit(ents,n,eb[1],c->max_entity_verts);
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
    }
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
                c->pend_uwb=uw.brightness;c->pend_uwfov=cam.fov_deg;
                return 1;
            }
        }
        cr_raster_cuda_frame_end(&c->fb);
    }
    gm_hand_set_swing(swing);
    gm_hand_set_equip(equip);
    gm_hand_set_item_override(c->equip_item, c->equip_meta, c->equip_count);
    if(v.loading==2){
        /* GuiDownloadTerrain has closed, but the new player/chunk is not yet
         * renderable: vanilla shows only its sky/fog framebuffer + crosshair. */
        cr_fb_clear(&c->fb,clear);
        if(r->dimension==0)gm_sky_draw(&c->fb,&cam,day);
        else if(r->dimension==1)gm_end_sky_draw(&c->fb,&cam);
        gm_hud_draw(&c->fb,&v);
    }else{
        if(!v.dead&&!getenv("MAGMA_NO_HAND"))gm_hand_draw(&c->fb,&v,c->hand_bob);
        /* ItemRenderer.renderOverlays runs with the hand, BEFORE the HUD. */
        if(uw.overlay&&!v.dead)gm_uw_overlay_draw(&c->fb,&v,uw.brightness,cam.fov_deg);
        if(v.portal>0.0f)gm_overlay_portal_screen(&c->fb,&atlas,v.portal);
        gm_hud_draw(&c->fb,&v);
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
