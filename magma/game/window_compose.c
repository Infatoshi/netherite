#include "game/window_compose.h"

#include "assets/blockmodels.h"
#include "core/config.h"
#include "game/caps.h"
#include "game/entity_render.h"
#include "game/frame_capture.h"
#include "game/hand.h"
#include "game/hud.h"
#include "game/item_render.h"
#include "game/overlay.h"
#include "game/screen.h"
#include "game/sel_box.h"
#include "game/sky.h"
#include "game/underwater.h"
#include "game/view.h"
#include "world/lightmap.h"
#include "world/mesh_mc.h"

#include <math.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern void cr_raster_cuda_pre(int, int, int) __attribute__((weak));
extern void cr_raster_cuda_into(CrFramebuffer *, const CrScreenTri *, int,
                                const CrShadeCtx *) __attribute__((weak));
extern void cr_raster_cuda_render_layer(CrFramebuffer *, const CrVertex *, int,
                                        const CrCamera *, const CrShadeCtx *)
    __attribute__((weak));
extern void cr_raster_cuda_frame_begin(const CrFramebuffer *) __attribute__((weak));
extern void cr_raster_cuda_frame_end(CrFramebuffer *) __attribute__((weak));
extern int cr_raster_cuda_screen_tris(void) __attribute__((weak));
extern void cr_raster_cuda_sky(const GmSkyCtx *, const float *, int, int)
    __attribute__((weak));
extern void cr_raster_cuda_atlas_dirty(void) __attribute__((weak));
extern void cr_raster_cuda_post(void) __attribute__((weak));
extern int cr_raster_cuda_slab_pool(int, int) __attribute__((weak));
extern void cr_raster_cuda_slab_sync(int, int, const void *, int)
    __attribute__((weak));
extern void cr_raster_cuda_slabs_reset(void) __attribute__((weak));
extern void cr_raster_cuda_render_gather(CrFramebuffer *, const int *,
                                         const int *, int, int,
                                         const CrCamera *, const CrShadeCtx *)
    __attribute__((weak));

#ifdef MAGMA_METAL
extern void cr_raster_metal_pre(int, int, int) __attribute__((weak));
extern void cr_raster_metal_into(CrFramebuffer *, const CrScreenTri *, int,
                                 const CrShadeCtx *) __attribute__((weak));
extern void cr_raster_metal_render_layer(CrFramebuffer *, const CrVertex *, int,
                                         const CrCamera *, const CrShadeCtx *)
    __attribute__((weak));
extern void cr_raster_metal_frame_begin(const CrFramebuffer *) __attribute__((weak));
extern void cr_raster_metal_frame_end(CrFramebuffer *) __attribute__((weak));
extern int cr_raster_metal_screen_tris(void) __attribute__((weak));
extern void cr_raster_metal_sky(const GmSkyCtx *, const float *, int, int)
    __attribute__((weak));
extern void cr_raster_metal_atlas_dirty(void) __attribute__((weak));
extern void cr_raster_metal_post(void) __attribute__((weak));
extern int cr_raster_metal_slab_pool(int, int) __attribute__((weak));
extern void cr_raster_metal_slab_sync(int, int, const void *, int)
    __attribute__((weak));
extern void cr_raster_metal_slabs_reset(void) __attribute__((weak));
extern void cr_raster_metal_render_gather(CrFramebuffer *, const int *,
                                          const int *, int, int,
                                          const CrCamera *, const CrShadeCtx *)
    __attribute__((weak));
#endif

struct GmWindowCompose {
    CrFramebuffer fb;
    CrScreenTri *tris;
    CrVertex *entity_verts;
    int max_tris;
    int max_entity_verts;
    GmBackend backend;
    int backend_open;
    int anim_textures;
    GmRuntime *runtime;
    GmParticlesLive *particles;
    CrTexture atlas;
    CrRgba lm_lut[256];
    float hand_bob;
    int swing_ticks;
    int prev_attack;
    float prev_attack_cooldown;
    int attack_cooldown_initialized;
    float equip_progress;
    int equip_item;
    int equip_meta;
    int equip_count;
    int equip_slot;
    GmHudState hud_state;
    int boss_latch;
    float boss_frac;
    int dragon_dying;
    int dragon_killed;
    float fog_c1;
    int fog_c1_init;
    int hud_cached;
    int hud_health;
    int hud_last_health;
    int hud_flash;
    int hud_state_valid;
    unsigned char *ppm_buf;
    CrRgba *portal_scratch; /* inverse of setupCameraTransform portal wobble */
    FILE *npy_f;
    int npy_frames;
    char frames_out[1024];
    /* W18 device-resident world layers: mirror world_live's toroidal mesh-slab
     * pool on the GPU and concatenate each layer's vert stream there, so a
     * frame's H2D traffic is proportional to REMESHED chunks instead of the
     * whole visible view (~65 MB/frame at vd8 854x480). Entities, particles,
     * overlays and the HUD keep the render_layer upload path. */
    int dev_mesh;
    int mesh_slots, slab_cap, runs_stride;
    GmChunkDraw *chunks;
    GmMeshRun *runs;
    int *gsrc, *gcnt;          /* gather table scratch, runs_stride entries */
    const GmWorld *slab_world; /* pool holds this world's slabs */
};

/* ---- backend dispatch for the slab-pool / gather entry points ---------- */

static int wc_dev_backend(const GmWindowCompose *c) {
    if (c->backend == GM_BACKEND_CUDA)
        return cr_raster_cuda_slab_pool && cr_raster_cuda_slab_sync &&
               cr_raster_cuda_render_gather;
#ifdef MAGMA_METAL
    if (c->backend == GM_BACKEND_METAL)
        return cr_raster_metal_slab_pool && cr_raster_metal_slab_sync &&
               cr_raster_metal_render_gather;
#endif
    return 0;
}

static int wc_slab_pool(const GmWindowCompose *c, int nslots, int slab_verts) {
    if (c->backend == GM_BACKEND_CUDA)
        return cr_raster_cuda_slab_pool(nslots, slab_verts);
#ifdef MAGMA_METAL
    if (c->backend == GM_BACKEND_METAL)
        return cr_raster_metal_slab_pool(nslots, slab_verts);
#endif
    return 0;
}

static void wc_slab_sync(const GmWindowCompose *c, int slot, int builds,
                         const void *host, int used_verts) {
    if (c->backend == GM_BACKEND_CUDA)
        cr_raster_cuda_slab_sync(slot, builds, host, used_verts);
#ifdef MAGMA_METAL
    else if (c->backend == GM_BACKEND_METAL)
        cr_raster_metal_slab_sync(slot, builds, host, used_verts);
#endif
}

static void wc_slabs_reset(const GmWindowCompose *c) {
    if (c->backend == GM_BACKEND_CUDA) {
        if (cr_raster_cuda_slabs_reset) cr_raster_cuda_slabs_reset();
    }
#ifdef MAGMA_METAL
    else if (c->backend == GM_BACKEND_METAL) {
        if (cr_raster_metal_slabs_reset) cr_raster_metal_slabs_reset();
    }
#endif
}

static void wc_render_gather(GmWindowCompose *c, const CrCamera *cam,
                             int nents, int nverts, const CrShadeCtx *sh) {
    if (c->backend == GM_BACKEND_CUDA)
        cr_raster_cuda_render_gather(&c->fb, c->gsrc, c->gcnt, nents, nverts,
                                     cam, sh);
#ifdef MAGMA_METAL
    else if (c->backend == GM_BACKEND_METAL)
        cr_raster_metal_render_gather(&c->fb, c->gsrc, c->gcnt, nents, nverts,
                                      cam, sh);
#endif
}

static void set_error(char *err, int cap, const char *msg) {
    if (err && cap > 0) snprintf(err, (size_t)cap, "%s", msg);
}

static void stamp(const GmWindowComposeFrame *f, int slot) {
    if (f->stamp) f->stamp(slot);
}

static void npy_stamp_header(FILE *f, int n, int h, int w) {
    char dict[119];
    int len = snprintf(dict, sizeof dict,
        "{'descr': '|u1', 'fortran_order': False, 'shape': (%8d, %d, %d, 3), }",
        n, h, w);
    memset(dict + len, ' ', 117 - (size_t)len);
    dict[117] = '\n';
    unsigned char pre[10] = {0x93, 'N', 'U', 'M', 'P', 'Y', 1, 0, 118, 0};
    fseek(f, 0, SEEK_SET);
    fwrite(pre, 1, 10, f);
    fwrite(dict, 1, 118, f);
}

static int emit_ppm(GmWindowCompose *c, int tick) {
    char path[1200];
    int len = snprintf(path, sizeof path, "%s/frame_%06d.ppm",
                       c->frames_out, tick);
    if (len < 0 || len >= (int)sizeof path) return 0;
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    int n = c->fb.w * c->fb.h;
    for (int i = 0; i < n; ++i) {
        c->ppm_buf[i * 3 + 0] = c->fb.color[i].r;
        c->ppm_buf[i * 3 + 1] = c->fb.color[i].g;
        c->ppm_buf[i * 3 + 2] = c->fb.color[i].b;
    }
    int ok = fprintf(f, "P6\n%d %d\n255\n", c->fb.w, c->fb.h) >= 0 &&
             fwrite(c->ppm_buf, 3, (size_t)n, f) == (size_t)n;
    return fclose(f) == 0 && ok;
}

static int render_layer(GmWindowCompose *c, const CrCamera *cam,
                        const CrVertex *verts, int nv,
                        const CrShadeCtx *sh) {
    if (nv < 3) return 0;
    if (c->backend == GM_BACKEND_CUDA && cr_raster_cuda_render_layer) {
        cr_raster_cuda_render_layer(&c->fb, verts, nv, cam, sh);
        return nv / 3;
    }
#ifdef MAGMA_METAL
    if (c->backend == GM_BACKEND_METAL && cr_raster_metal_render_layer) {
        cr_raster_metal_render_layer(&c->fb, verts, nv, cam, sh);
        return nv / 3;
    }
#endif
    int ntris = cr_transform(verts, nv, NULL, 0, cam, c->fb.w, c->fb.h,
                             c->tris, c->max_tris);
    if (ntris > 0) {
        if (c->backend == GM_BACKEND_CUDA)
            cr_raster_cuda_into(&c->fb, c->tris, ntris, sh);
#ifdef MAGMA_METAL
        else if (c->backend == GM_BACKEND_METAL)
            cr_raster_metal_into(&c->fb, c->tris, ntris, sh);
#endif
        else
            cr_raster_cpu(&c->fb, c->tris, ntris, sh);
    }
    return ntris;
}

static void terrain_shades(const CrTexture *atlas, CrRgba fog, int dimension,
                           int boss_fog, const GmUnderwater *uw,
                           const CrRgba *lm, CrShadeCtx out[4]) {
    int fon;
    float fst, fen;
    gm_frame_world_fog_params(dimension, boss_fog, &fon, &fst, &fen);
#define TSH(at, ly, bl) { .atlas = atlas, .fog_color = fog,                  \
                          .fog_start = fst, .fog_end = fen,                  \
                          .alpha_test = (at), .enable_fog = fon,             \
                          .layer = (ly), .blend = (bl) }
    CrShadeCtx sh_solid = TSH(0, CR_LAYER_SOLID,         0);
    CrShadeCtx sh_cmip  = TSH(1, CR_LAYER_CUTOUT_MIPPED, 0);
    sh_cmip.depth_lequal = 1;
    CrShadeCtx sh_cut   = TSH(1, CR_LAYER_CUTOUT,        0);
    CrShadeCtx sh_trans = TSH(0, CR_LAYER_TRANSLUCENT,   1);
#undef TSH
    sh_solid.lightmap = lm;
    sh_cmip.lightmap = lm;
    sh_cut.lightmap = lm;
    sh_trans.lightmap = lm;
    if (uw && uw->fluid) {
        /* EntityRenderer.setupFog fluid: GL_EXP replaces the linear ramp. */
        CrShadeCtx *all[4] = {&sh_solid, &sh_cmip, &sh_cut, &sh_trans};
        for (int i = 0; i < 4; ++i) {
            all[i]->enable_fog = 1;
            all[i]->fog_color = uw->fog_rgba;
            all[i]->fog_exp_density = uw->density;
        }
    }
    out[0] = sh_solid;
    out[1] = sh_cmip;
    out[2] = sh_cut;
    out[3] = sh_trans;
}

static int render_world_layers(GmWindowCompose *c, const CrCamera *cam,
                               const GmMeshView *mv, const CrTexture *atlas,
                               CrRgba fog, int dimension, int boss_fog,
                               const GmUnderwater *uw, const CrRgba *lm,
                               int first_layer, int end_layer) {
    CrShadeCtx shade[4];
    terrain_shades(atlas, fog, dimension, boss_fog, uw, lm, shade);
    int ntris = 0;
    for (int layer = first_layer; layer < end_layer; ++layer)
        ntris += render_layer(c, cam, mv->verts[layer], mv->nverts[layer],
                              &shade[layer]);
    return ntris;
}

/* Device-resident twin of render_world_layers. The slab runs recorded by
 * gm_world_mesh_runs are exactly the ranges the host concat would have copied,
 * in the identical order, so the gathered device stream is byte-identical to
 * mv->verts[layer] and the pipeline behind it (transform compact -> scan ->
 * scatter -> bbox -> tiled) is the same one render_layer runs. */
static int render_world_dev_layers(GmWindowCompose *c, const CrCamera *cam,
                                   const int nruns[4], const int nverts[4],
                                   int nch, const CrTexture *atlas,
                                   CrRgba fog, int dimension, int boss_fog,
                                   const GmUnderwater *uw, const CrRgba *lm,
                                   int first_layer, int end_layer) {
    CrShadeCtx shade[4];
    terrain_shades(atlas, fog, dimension, boss_fog, uw, lm, shade);
    if (first_layer == CR_LAYER_SOLID) {
        /* Upload only the chunks whose mesh was rebuilt since their last sync
         * (slab_sync no-ops on an unchanged `builds`). This is the ENTIRE
         * per-frame world H2D: a static camera over a static world moves
         * nothing at all. */
        for (int i = 0; i < nch; ++i) {
            const GmChunkDraw *d = &c->chunks[i];
            wc_slab_sync(c, d->slot, d->builds, d->slab, d->off[3] + d->n[3]);
        }
    }
    int ntris = 0;
    for (int layer = first_layer; layer < end_layer; ++layer) {
        if (nruns[layer] <= 0 || nverts[layer] < 3) continue;
        const GmMeshRun *bank = c->runs + (size_t)layer * (size_t)c->runs_stride;
        for (int i = 0; i < nruns[layer]; ++i) {
            c->gsrc[i] = bank[i].slot * c->slab_cap + bank[i].off;
            c->gcnt[i] = bank[i].n;
        }
        wc_render_gather(c, cam, nruns[layer], nverts[layer], &shade[layer]);
        ntris += nverts[layer] / 3;
    }
    return ntris;
}

static void apply_fluid_fog(CrShadeCtx *shade, const GmUnderwater *uw) {
    if (!uw->fluid) return;
    shade->enable_fog = 1;
    shade->fog_exp_density = uw->density;
    shade->fog_color = uw->fog_rgba;
}

static CrCamera camera_for(const GmPlayerView *v, int w, int h) {
    CrCamera c;
    c.pos.x = v->x;
    c.pos.y = v->y + v->eye_height;
    c.pos.z = v->z;
    c.yaw = gm_view_cam_yaw_rad(v->yaw);
    c.pitch = gm_view_cam_pitch_rad(v->pitch);
    c.fov_deg = 70.0f * (v->fov_mult > 0.01f ? v->fov_mult : 1.0f);
    c.aspect = (float)w / (float)h;
    c.znear = 0.05f;
    c.zfar = GM_TERRAIN_ZFAR;
    c.hurt_yaw_deg = v->hurt_yaw;
    c.hurt_roll_deg = gm_view_hurt_roll_deg(v->hurt_time, v->max_hurt_time);
    return c;
}

static void render_selection(GmWindowCompose *c, const CrCamera *cam,
                             CrRgba fog) {
    GmRuntime *r = c->runtime;
    static CrVertex verts[GM_OVERLAY_MAX_VERTS];
    int hx = 0, hy = 0, hz = 0, ax, ay, az;
    int have = gm_raycast_sel(r->window, &r->sin_table, &r->player,
                              &hx, &hy, &hz, &ax, &ay, &az) >= 0;
    float bounds[6];
    if (have) gm_sel_box_at(r->window, hx, hy, hz, bounds);
    if (!have) return;
    int nv = gm_overlay_emit_sel(verts, GM_OVERLAY_MAX_VERTS,
                                 hx + r->ox, hy, hz + r->oz, bounds,
                                 cam->pos.x, cam->pos.y, cam->pos.z);
    if (nv <= 0) return;
    CrShadeCtx shade = {0};
    shade.atlas = &c->atlas;
    shade.fog_color = fog;
    shade.layer = CR_LAYER_TRANSLUCENT;
    shade.blend = 1;
    shade.depth_lequal = 1;
    render_layer(c, cam, verts, nv, &shade);
}

static void render_crack(GmWindowCompose *c, const CrCamera *cam, CrRgba fog) {
    GmRuntime *r = c->runtime;
    static CrVertex verts[GM_OVERLAY_MAX_VERTS];
    int dx = 0, dy = 0, dz = 0;
    float damage = 0.0f;
    int have = gm_player_dig_state(&dx, &dy, &dz, &damage);
    if (!have || damage <= 0.0f || cr_cfg()->no_crack) return;
    int nv = gm_overlay_emit_crack(verts, GM_OVERLAY_MAX_VERTS,
                                   dx + r->ox, dy, dz + r->oz, damage, -1);
    if (nv <= 0) return;
    CrShadeCtx shade = {0};
    shade.atlas = &c->atlas;
    shade.fog_color = fog;
    shade.alpha_test = 1;
    shade.layer = CR_LAYER_CUTOUT;
    shade.blend = 2;
    shade.depth_lequal = 1;
    render_layer(c, cam, verts, nv, &shade);
}

static int collect_entities(GmWindowCompose *c, GmEntityView *ents) {
    GmRuntime *r = c->runtime;
    enum { WC_ENTS = GM_LIVE_MAX + GM_RUNTIME_GHOST_VIEWS };
    int n = gm_dragon_fill_views(&r->dragon, ents, WC_ENTS);
    n += gm_mobs_fill_views(&r->mobs, ents + n, WC_ENTS - n);
    int projectile0 = n;
    n += gm_runtime_projectile_views(r, ents + n, WC_ENTS - n);
    {
        int types[GM_RUNTIME_PROJECTILES], nt = 0;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (r->projectiles[i].active)
                types[nt++] = r->projectiles[i].type;
        gm_entity_patch_large_fireballs(types, nt, ents + projectile0,
                                        n - projectile0);
    }
    {
        int tape_falling = 0;
        for (int i = 0; i < r->nghost_views; ++i)
            if (r->ghost_views[i].type == GM_VIEW_FALLING_BLOCK) {
                tape_falling = 1;
                break;
            }
        n += gm_live_fill_views_filtered(&r->entities, ents + n, WC_ENTS - n,
                                         tape_falling);
    }
    n += gm_runtime_ghost_views(r, ents + n, WC_ENTS - n);
    if (r->dragon.initialized) {
        int death_ticks = r->dragon.state.arena.dragon.death_ticks;
        for (int i = 0; i < n; ++i)
            if (ents[i].type == GM_ENTITY_DRAGON &&
                ents[i].death_ticks <= 0 && death_ticks > 0)
                ents[i].death_ticks = death_ticks;
    }
    return n;
}

static void update_boss_state(GmWindowCompose *c,
                              const GmEntityView *ents, int n) {
    GmRuntime *r = c->runtime;
    if (r->dimension != 1) {
        c->boss_latch = 0;
        c->dragon_killed = 0;
        c->dragon_dying = 0;
    }
    int dragon_seen = 0;
    for (int i = 0; i < n; ++i) {
        if (ents[i].type != GM_ENTITY_DRAGON) continue;
        dragon_seen = 1;
        if (ents[i].death_ticks >= 200) c->dragon_killed = 1;
        if (ents[i].death_ticks > 0 && ents[i].health <= 0.0f)
            c->dragon_dying = 1;
        if (!c->boss_latch && !c->dragon_killed) {
            c->boss_latch = 1;
            c->boss_frac = 1.0f;
        }
        if (ents[i].health >= 0.0f) c->boss_frac = ents[i].health / 200.0f;
        break;
    }
    if (!dragon_seen && c->dragon_dying) c->dragon_killed = 1;
    if (c->dragon_killed) {
        c->boss_latch = 0;
        c->boss_frac = 0.0f;
    } else if (!c->boss_latch) {
        for (int i = 0; i < n; ++i) {
            if (ents[i].type == GM_ENTITY_CRYSTAL || ents[i].type == 31) {
                c->boss_latch = 1;
                c->boss_frac = 1.0f;
                break;
            }
        }
    }
    gm_hud_set_boss(c->boss_latch && !cr_cfg()->strip_overlays,
                    c->boss_frac);
}

static void advance_equip_state(GmWindowCompose *c,
                                const GmPlayerView *view) {
    GmRuntime *r = c->runtime;
    int slot = r->player.inv.current_item;
    if (slot < 0) slot = 0;
    if (slot > 8) slot = 8;
    const IsrInv *inv = r->tape_inv_active ? &r->tape_inv : &r->player.inv;
    ICStack held = isr_get_stack(inv, slot);
    int same = (held.item == 0 && c->equip_item == 0) ||
               (slot == c->equip_slot && held.item == c->equip_item);
    float cooldown = view->attack_cooldown;
    if (cooldown < 0.0f) cooldown = 0.0f;
    if (cooldown > 1.0f) cooldown = 1.0f;
    float target = same ? cooldown * cooldown * cooldown : 0.0f;
    float delta = target - c->equip_progress;
    if (delta < -0.4f) delta = -0.4f;
    if (delta > 0.4f) delta = 0.4f;
    c->equip_progress += delta;
    if (c->equip_progress < 0.1f) {
        c->equip_item = held.item;
        c->equip_meta = held.meta;
        c->equip_count = held.count;
        c->equip_slot = slot;
    } else if (same) {
        c->equip_meta = held.meta;
        c->equip_count = held.count;
    }
}

static void advance_fog_state(GmWindowCompose *c, int nticks) {
    GmRuntime *r = c->runtime;
    double x, y, z;
    gm_runtime_tick_entry_feet(r, &x, &y, &z);
    int steps = nticks;
    if (!c->fog_c1_init) {
        const char *initial = cr_cfg()->fog_c1_init;
        c->fog_c1 = initial && *initial
            ? (float)atof(initial)
            : gm_uw_fog_c1_seed(r->world, r->dimension, x, y, z);
        c->fog_c1_init = 1;
        if (steps > 0) --steps;
    }
    for (int i = 0; i < steps; ++i)
        c->fog_c1 = gm_uw_fog_c1_step(c->fog_c1, r->world, r->dimension,
                                      x, y, z);
}

static void advance_hud_state(GmWindowCompose *c, GmPlayerView *view,
                              int nticks) {
    long long first = c->runtime->tick - nticks + 1;
    if (nticks <= 0 && !c->hud_cached) {
        gm_hud_state_step(&c->hud_state, view, c->runtime->tick);
    } else {
        for (int i = 0; i < nticks; ++i)
            gm_hud_state_step(&c->hud_state, view, first + i);
    }
    if (nticks > 0 || !c->hud_cached) {
        c->hud_health = view->hud_health;
        c->hud_last_health = view->hud_last_health;
        c->hud_flash = view->hud_flash;
        c->hud_state_valid = view->hud_state_valid;
        c->hud_cached = 1;
    } else {
        view->hud_health = c->hud_health;
        view->hud_last_health = c->hud_last_health;
        view->hud_flash = c->hud_flash;
        view->hud_state_valid = c->hud_state_valid;
    }
}

GmWindowCompose *gm_window_compose_open(const GmConfig *cfg,
                                         char *err, int err_cap) {
    if (!cfg) {
        set_error(err, err_cap, "invalid window compose config");
        return NULL;
    }
    GmWindowCompose *c = calloc(1, sizeof *c);
    if (!c) {
        set_error(err, err_cap, "window compose allocation failed");
        return NULL;
    }
    const CrCaps *caps = cr_caps();
    c->max_tris = caps->max_tris;
    c->max_entity_verts = caps->ent_max_verts;
    c->backend = cfg->backend;
    c->anim_textures = cr_cfg()->anim_textures;
    cr_fb_alloc(&c->fb, cfg->width, cfg->height);
    c->tris = malloc((size_t)c->max_tris * sizeof *c->tris);
    c->entity_verts = malloc((size_t)c->max_entity_verts *
                             sizeof *c->entity_verts);
    c->portal_scratch = malloc((size_t)cfg->width * (size_t)cfg->height *
                               sizeof *c->portal_scratch);
    if (!c->fb.color || !c->tris || !c->entity_verts || !c->portal_scratch) {
        set_error(err, err_cap, "window compose allocation failed");
        gm_window_compose_close(c);
        return NULL;
    }
    /* ItemRenderer equippedProgress idle is 1; gm_hand_set_equip takes
     * 1-equippedProgress, so 0 is the rest pose used by first-person goldens. */
    c->equip_progress = 1.0f;
    if (cfg->frames_out_dir) {
        size_t len = strlen(cfg->frames_out_dir);
        if (len >= sizeof c->frames_out) {
            set_error(err, err_cap, "frames-out path is too long");
            gm_window_compose_close(c);
            return NULL;
        }
        strcpy(c->frames_out, cfg->frames_out_dir);
        c->ppm_buf = malloc((size_t)c->fb.w * (size_t)c->fb.h * 3);
        if (!c->ppm_buf) {
            set_error(err, err_cap, "window compose frame output allocation failed");
            gm_window_compose_close(c);
            return NULL;
        }
        int npy = len > 4 && !strcmp(c->frames_out + len - 4, ".npy");
        if (npy) {
            c->npy_f = fopen(c->frames_out, "wb");
            if (!c->npy_f) {
                set_error(err, err_cap, "cannot open frames-out npy");
                gm_window_compose_close(c);
                return NULL;
            }
            npy_stamp_header(c->npy_f, 0, c->fb.h, c->fb.w);
        } else {
            if (mkdir(c->frames_out, 0775) != 0 && errno != EEXIST) {
                set_error(err, err_cap, "cannot create frames-out directory");
                gm_window_compose_close(c);
                return NULL;
            }
            struct stat st;
            if (stat(c->frames_out, &st) != 0 || !S_ISDIR(st.st_mode)) {
                set_error(err, err_cap, "frames-out path is not a directory");
                gm_window_compose_close(c);
                return NULL;
            }
        }
    }
    if (c->backend == GM_BACKEND_CUDA) {
        if (!cr_raster_cuda_pre || !cr_raster_cuda_into ||
            !cr_raster_cuda_frame_begin || !cr_raster_cuda_frame_end ||
            !cr_raster_cuda_sky || !cr_raster_cuda_atlas_dirty ||
            !cr_raster_cuda_post) {
            set_error(err, err_cap, "CUDA window compose unavailable");
            gm_window_compose_close(c);
            return NULL;
        }
        cr_raster_cuda_pre(c->fb.w, c->fb.h, c->max_tris);
        c->backend_open = 1;
    }
#ifdef MAGMA_METAL
    else if (c->backend == GM_BACKEND_METAL) {
        if (!cr_raster_metal_pre || !cr_raster_metal_into ||
            !cr_raster_metal_frame_begin || !cr_raster_metal_frame_end ||
            !cr_raster_metal_sky || !cr_raster_metal_atlas_dirty ||
            !cr_raster_metal_post) {
            set_error(err, err_cap, "Metal window compose unavailable");
            gm_window_compose_close(c);
            return NULL;
        }
        cr_raster_metal_pre(c->fb.w, c->fb.h, c->max_tris);
        c->backend_open = 1;
    }
#endif
    /* Device-resident world layers: mirror world_live's mesh-slab pool on the
     * GPU. runs_stride is the worst case (every kept chunk contributing a run
     * per section); the backend gather takes at most GM_GATHER_MAX_ENTRIES, so
     * an over-large configured view radius simply keeps the host-concat path.
     * no_devmesh forces that path too (same switch frame_capture uses). */
    if (c->backend_open && wc_dev_backend(c) && !cr_cfg()->no_devmesh) {
        int stride = caps->mesh_slots * GM_MESH_SECTIONS;
        if (stride <= GM_GATHER_MAX_ENTRIES &&
            wc_slab_pool(c, caps->mesh_slots, caps->max_verts_per_chunk)) {
            c->mesh_slots  = caps->mesh_slots;
            c->slab_cap    = caps->max_verts_per_chunk;
            c->runs_stride = stride;
            c->chunks = malloc((size_t)caps->mesh_slots * sizeof *c->chunks);
            c->runs   = malloc(4 * (size_t)stride * sizeof *c->runs);
            c->gsrc   = malloc((size_t)stride * sizeof *c->gsrc);
            c->gcnt   = malloc((size_t)stride * sizeof *c->gcnt);
            c->dev_mesh = c->chunks && c->runs && c->gsrc && c->gcnt;
        }
    }
    return c;
}

void gm_window_compose_bind(GmWindowCompose *c, GmRuntime *runtime,
                            GmParticlesLive *particles) {
    if (!c) return;
    c->runtime = runtime;
    c->particles = particles;
    if (runtime) c->atlas = gm_world_atlas(runtime->world);
}

void gm_window_compose_advance(GmWindowCompose *c, GmPlayerView *view,
                               const GmAction *action, int nticks) {
    if (!c || !c->runtime || !view || !action) return;
    advance_fog_state(c, nticks);
    advance_hud_state(c, view, nticks);
    float mv_mag = fabsf(action->forward) + fabsf(action->strafe);
    if (mv_mag > 0.01f) c->hand_bob += 0.30f * (float)nticks;
    int attack = action->attack || action->do_break;
    int cooldown_reset = c->attack_cooldown_initialized &&
        view->attack_cooldown + 1e-6f < c->prev_attack_cooldown;
    c->prev_attack_cooldown = view->attack_cooldown;
    c->attack_cooldown_initialized = 1;
    int swing_arm = (attack && !c->prev_attack) || cooldown_reset ||
                    gm_player_dig_swing();
    c->prev_attack = attack;
    if (swing_arm && c->swing_ticks <= 3) c->swing_ticks = 6;
    float swing = c->swing_ticks > 0
        ? (float)(6 - c->swing_ticks) / 6.0f : 0.0f;
    gm_hand_set_swing(swing);
    c->swing_ticks -= nticks;
    if (c->swing_ticks < 0) c->swing_ticks = 0;
    for (int i = 0; i < nticks; ++i) advance_equip_state(c, view);
}

int gm_window_compose_draw(GmWindowCompose *c,
                           const GmWindowComposeFrame *frame,
                           GmWindowComposeStats *stats,
                           char *err, int err_cap) {
    if (!c || !c->runtime || !c->particles || !frame || !frame->view ||
        !frame->camera_view) {
        set_error(err, err_cap, "invalid window compose state");
        return 0;
    }
    GmRuntime *r = c->runtime;
    GmPlayerView mapped_view = *frame->view;
    if (!r->tape_xp_active) {
        mapped_view.portal = (float)r->portal_time / 80.0f;
        if (mapped_view.portal > 1.0f) mapped_view.portal = 1.0f;
        long long portal_frame = r->clock.total_time % 32;
        if (portal_frame < 0) portal_frame += 32;
        mapped_view.portal_frame = (int)portal_frame;
        /* EntityRenderer.rendererUpdateCount: one bump per client tick. */
        mapped_view.portal_phase = (int)r->clock.total_time;
    }
    const GmPlayerView *pv = &mapped_view;
    const GmPlayerView *cpv = frame->camera_view;
    CrCamera cam = camera_for(cpv, c->fb.w, c->fb.h);
    if (!c->fog_c1_init) advance_fog_state(c, 0);
    float fog_c1 = c->fog_c1;
    GmUnderwater uw;
    gm_uw_eval(r->world, r->dimension, cpv, fog_c1, &uw);
    cam.fov_deg *= uw.fov_scale;
    if (c->anim_textures) {
        long long portal_frame = r->clock.total_time % 32;
        if (portal_frame < 0) portal_frame += 32;
        bm_atlas_set_animation_tick(r->clock.total_time);
        bm_atlas_set_portal_frame((int)portal_frame);
    } else {
        bm_atlas_set_animation_physical_zero();
        bm_atlas_set_portal_frame(0);
    }
    c->atlas = gm_world_atlas(r->world);
    if (c->anim_textures && c->backend == GM_BACKEND_CUDA)
        cr_raster_cuda_atlas_dirty();
#ifdef MAGMA_METAL
    else if (c->anim_textures && c->backend == GM_BACKEND_METAL)
        cr_raster_metal_atlas_dirty();
#endif
    gm_sky_set_weather(r->rain_strength, r->thunder_strength);
    gm_sky_set_fog_c1(fog_c1);
    gm_sky_set_eye_height(cpv->eye_height > 0.01f ? cpv->eye_height : 1.62f);
    gm_sky_set_fluid_fog(uw.fluid ? 1 : 0, uw.fog01, uw.density);
    stamp(frame, 3);

    long long day_tick = r->clock.world_time % 24000LL;
    if (day_tick < 0) day_tick += 24000LL;
    float day = (float)day_tick / 24000.0f;
    CrRgba clear = gm_frame_clear_color(day, r->dimension, fog_c1, &uw);
    cr_fb_clear(&c->fb, clear);
    int gpu_sky = c->backend != GM_BACKEND_CPU && r->dimension == 0 &&
                  !cr_cfg()->cpu_sky;
    if (!gpu_sky) {
        if (r->dimension == 0) gm_sky_draw(&c->fb, &cam, day);
        else if (r->dimension == 1) gm_end_sky_draw(&c->fb, &cam);
    }
    stamp(frame, 4);
    if (c->backend == GM_BACKEND_CUDA) {
        cr_raster_cuda_frame_begin(&c->fb);
        if (gpu_sky) {
            GmSkyCtx sc;
            float basis[11];
            gm_sky_frame_args(&cam, day, &sc, basis);
            cr_raster_cuda_sky(&sc, basis, c->fb.w, c->fb.h);
        }
    }
#ifdef MAGMA_METAL
    else if (c->backend == GM_BACKEND_METAL) {
        cr_raster_metal_frame_begin(&c->fb);
        if (gpu_sky) {
            GmSkyCtx sc;
            float basis[11];
            gm_sky_frame_args(&cam, day, &sc, basis);
            cr_raster_metal_sky(&sc, basis, c->fb.w, c->fb.h);
        }
    }
#endif
    stamp(frame, 5);
    GmEntityView ents[GM_LIVE_MAX + GM_RUNTIME_GHOST_VIEWS];
    int nents = collect_entities(c, ents);
    update_boss_state(c, ents, nents);
    GmMeshView mv;
    int dev_nruns[4] = {0, 0, 0, 0}, dev_nv[4] = {0, 0, 0, 0};
    int dev_nch = -1;
    if (c->dev_mesh) {
        /* Slot rebuild counters restart per world, so a dimension switch must
         * invalidate the pool or the new world's chunks look already-uploaded
         * (see frame_capture.c). */
        if (r->world != c->slab_world) {
            wc_slabs_reset(c);
            c->slab_world = r->world;
        }
        dev_nch = gm_world_mesh_runs(r->world, &cam, c->fb.w, c->fb.h,
                                     c->chunks, c->mesh_slots,
                                     c->runs, c->runs_stride,
                                     dev_nruns, dev_nv,
                                     &mv.n_kept, &mv.n_culled);
    }
    if (dev_nch < 0) {
        gm_world_mesh_view(r->world, &cam, c->fb.w, c->fb.h, &mv);
        for (int l = 0; l < 4; ++l) dev_nv[l] = mv.nverts[l];
    } else {
        for (int l = 0; l < 4; ++l) {
            mv.verts[l] = NULL;
            mv.nverts[l] = dev_nv[l];
        }
    }
    stamp(frame, 6);
    const CrRgba *lm = NULL;
    if (worldmc_lightmap_mode() && r->dimension == 0) {
        gm_frame_lightmap_fill(&r->sin_table, r->clock.world_time,
                               r->rain_strength, r->thunder_strength, 0,
                               c->lm_lut);
        lm = c->lm_lut;
    }
    /* EntityRenderer.renderWorldPass: opaque terrain first; entities,
     * overlays, and particles are interleaved before translucent terrain. */
    int ntris = dev_nch >= 0
        ? render_world_dev_layers(c, &cam, dev_nruns, dev_nv, dev_nch,
                                  &c->atlas, clear, r->dimension, c->boss_latch,
                                  &uw, lm, CR_LAYER_SOLID, CR_LAYER_TRANSLUCENT)
        : render_world_layers(c, &cam, &mv, &c->atlas, clear,
                              r->dimension, c->boss_latch, &uw, lm,
                              CR_LAYER_SOLID, CR_LAYER_TRANSLUCENT);
    stamp(frame, 7);

    stamp(frame, 8);

    gm_frame_prepare_minecarts(ents, nents, r->world);
    gm_frame_entities_light(ents, nents, r->world, r->dimension, lm);
    {
        int nv = gm_entities_emit(ents, nents, c->entity_verts,
                                  c->max_entity_verts);
        nv += gm_frame_spawners_emit(r, c->entity_verts + nv,
                                     c->max_entity_verts - nv);
        gm_particles_dragon_latch(r->tick, ents, nents);
        nv += gm_particles_emit_filtered(
            ents, nents, pv->yaw, pv->pitch,
            gm_particles_live_suppresses_explosion(c->particles),
            c->entity_verts + nv, c->max_entity_verts - nv);
        CrTexture eatlas = gm_entity_atlas();
        CrRgba fog = clear;
        CrShadeCtx esh = {0};
        esh.atlas = &eatlas;
        esh.fog_color = fog;
        esh.alpha_test = 1;
        esh.layer = CR_LAYER_CUTOUT;
        esh.alpha_mask = 1;
        esh.entity_brightness = 1;
        esh.lightmap = lm;
        gm_entity_dissolve_mask(&esh.mask_u_off, &esh.mask_v_off);
        gm_frame_world_fog_params(r->dimension, c->boss_latch, &esh.enable_fog,
                                  &esh.fog_start, &esh.fog_end);
        apply_fluid_fog(&esh, &uw);
        render_layer(c, &cam, c->entity_verts, nv, &esh);
        int nx = gm_xp_orbs_emit(ents, nents, pv->yaw, pv->pitch,
                                 c->entity_verts, c->max_entity_verts);
        if (nx > 0) {
            CrShadeCtx xp = {0};
            xp.atlas = &eatlas;
            xp.fog_color = fog;
            xp.alpha_test = 1;
            xp.alpha_ref = 0.1f;
            xp.layer = CR_LAYER_TRANSLUCENT;
            xp.blend = 1;
            xp.lightmap = lm;
            apply_fluid_fog(&xp, &uw);
            render_layer(c, &cam, c->entity_verts, nx, &xp);
        }
        nv = gm_slime_gel_emit(ents, nents, c->entity_verts,
                               c->max_entity_verts);
        if (nv > 0) {
            CrShadeCtx gel = {0};
            gel.atlas = &eatlas;
            gel.fog_color = fog;
            gel.alpha_test = 1;
            gel.alpha_ref = 0.1f;
            gel.layer = CR_LAYER_TRANSLUCENT;
            gel.blend = 4;
            apply_fluid_fog(&gel, &uw);
            render_layer(c, &cam, c->entity_verts, nv, &gel);
        }
        nv = gm_dragon_death_rays_emit(ents, nents, c->entity_verts,
                                       c->max_entity_verts);
        if (nv > 0) {
            CrShadeCtx rays = {0};
            rays.atlas = &eatlas;
            rays.fog_color = fog;
            rays.untextured = 1;
            rays.blend = 3;
            rays.layer = CR_LAYER_TRANSLUCENT;
            rays.lightmap = lm;
            gm_frame_world_fog_params(r->dimension, c->boss_latch,
                                      &rays.enable_fog,
                                      &rays.fog_start, &rays.fog_end);
            apply_fluid_fog(&rays, &uw);
            render_layer(c, &cam, c->entity_verts, nv, &rays);
        }
        nv = gm_crystal_beams_emit(ents, nents, c->entity_verts,
                                   c->max_entity_verts);
        if (nv > 0) {
            CrShadeCtx beam = {0};
            beam.atlas = &eatlas;
            beam.fog_color = fog;
            beam.alpha_test = 1;
            beam.alpha_ref = 0.1f;
            beam.layer = CR_LAYER_CUTOUT;
            beam.lightmap = lm;
            gm_frame_world_fog_params(r->dimension, c->boss_latch,
                                      &beam.enable_fog,
                                      &beam.fog_start, &beam.fog_end);
            apply_fluid_fog(&beam, &uw);
            render_layer(c, &cam, c->entity_verts, nv, &beam);
        }
        nv = gm_items_emit(ents, nents, c->entity_verts,
                           c->max_entity_verts);
        nv += gm_falling_blocks_emit(ents, nents, c->entity_verts + nv,
                                     c->max_entity_verts - nv);
        nv += gm_minecart_contents_emit(ents, nents, c->entity_verts + nv,
                                        c->max_entity_verts - nv);
        if (nv > 0) {
            CrShadeCtx ish = {0};
            ish.atlas = &c->atlas;
            ish.fog_color = fog;
            ish.alpha_test = 1;
            ish.layer = CR_LAYER_CUTOUT;
            apply_fluid_fog(&ish, &uw);
            render_layer(c, &cam, c->entity_verts, nv, &ish);
        }
        nv = gm_items_emit_flat(ents, nents, c->entity_verts,
                                c->max_entity_verts);
        nv += gm_held_items_emit(ents, nents, c->entity_verts + nv,
                                 c->max_entity_verts - nv);
        nv += gm_items_emit_billboard(ents, nents, pv->yaw, pv->pitch,
                                      c->entity_verts + nv,
                                      c->max_entity_verts - nv);
        if (nv > 0) {
            CrTexture iatlas = gm_item_atlas();
            CrShadeCtx fsh = {0};
            fsh.atlas = &iatlas;
            fsh.fog_color = fog;
            fsh.alpha_test = 1;
            fsh.layer = CR_LAYER_CUTOUT;
            apply_fluid_fog(&fsh, &uw);
            render_layer(c, &cam, c->entity_verts, nv, &fsh);
        }
        gm_entity_prep_large_fireball_fire(ents, nents);
        nv = gm_small_fireball_fire_emit(ents, nents, pv->yaw,
                                         c->entity_verts,
                                         c->max_entity_verts);
        gm_entity_restore_large_fireball_types(ents, nents);
        nv += gm_entity_fire_emit(ents, nents, pv->yaw,
                                  c->entity_verts + nv,
                                  c->max_entity_verts - nv);
        if (nv > 0) {
            CrShadeCtx fire_sh = {0};
            fire_sh.atlas = &c->atlas;
            fire_sh.fog_color = fog;
            fire_sh.alpha_test = 1;
            fire_sh.layer = CR_LAYER_CUTOUT;
            apply_fluid_fog(&fire_sh, &uw);
            render_layer(c, &cam, c->entity_verts, nv, &fire_sh);
        }
    }
    /* overlay_dump: open the selection/crack passes on the headless dump
     * path; empty keeps the interactive-only gate. */
    const CrConfig *knobs = cr_cfg();
    int overlay = (frame->interactive || knobs->overlay_dump[0]) &&
                  !frame->screen_open && !r->dead &&
                  !knobs->no_overlay;
    if (overlay) render_selection(c, &cam, clear);
    {
        int nv = gm_particles_live_emit_recorded(
            c->particles, 0, frame->partial_ticks, cpv->yaw, cpv->pitch,
            c->entity_verts, c->max_entity_verts);
        if (nv > 0) {
            CrTexture eatlas = gm_entity_atlas();
            CrShadeCtx ps = {0};
            ps.atlas = &eatlas; ps.fog_color = clear;
            ps.alpha_test = 1; ps.alpha_ref = 0.003921569f;
            /* ParticleManager.renderParticles: SRC_ALPHA / ONE_MINUS_SRC_ALPHA,
             * alphaFunc(GL_GREATER, 1/255). Not DestFactor.ONE. */
            ps.layer = CR_LAYER_TRANSLUCENT; ps.blend = 1; ps.lightmap = lm;
            gm_frame_world_fog_params(r->dimension, c->boss_latch,
                                      &ps.enable_fog, &ps.fog_start, &ps.fog_end);
            apply_fluid_fog(&ps, &uw);
            render_layer(c, &cam, c->entity_verts, nv, &ps);
        }
    }
    {
        int nv = gm_particles_live_emit(c->particles, frame->partial_ticks,
                                        cpv->yaw, cpv->pitch,
                                        c->entity_verts,
                                        c->max_entity_verts);
        if (nv > 0) {
            CrShadeCtx dig = {0};
            dig.atlas = &c->atlas;
            dig.fog_color = clear;
            dig.alpha_test = 1;
            dig.layer = CR_LAYER_CUTOUT;
            apply_fluid_fog(&dig, &uw);
            render_layer(c, &cam, c->entity_verts, nv, &dig);
        }
    }
    {
        int nv = gm_particles_live_emit_recorded(
            c->particles, 3, frame->partial_ticks, cpv->yaw, cpv->pitch,
            c->entity_verts, c->max_entity_verts);
        if (nv > 0) {
            CrTexture eatlas = gm_entity_atlas();
            CrShadeCtx ps = {0};
            ps.atlas = &eatlas; ps.fog_color = clear;
            ps.alpha_test = 1; ps.layer = CR_LAYER_CUTOUT;
            ps.entity_brightness = 1;
            gm_frame_world_fog_params(r->dimension, c->boss_latch,
                                      &ps.enable_fog, &ps.fog_start, &ps.fog_end);
            apply_fluid_fog(&ps, &uw);
            render_layer(c, &cam, c->entity_verts, nv, &ps);
        }
    }
    if (overlay) render_crack(c, &cam, clear);
    ntris += dev_nch >= 0
        ? render_world_dev_layers(c, &cam, dev_nruns, dev_nv, dev_nch,
                                  &c->atlas, clear, r->dimension, c->boss_latch,
                                  &uw, lm, CR_LAYER_TRANSLUCENT,
                                  CR_LAYER_TRANSLUCENT + 1)
        : render_world_layers(c, &cam, &mv, &c->atlas, clear,
                              r->dimension, c->boss_latch, &uw, lm,
                              CR_LAYER_TRANSLUCENT,
                              CR_LAYER_TRANSLUCENT + 1);
    stamp(frame, 9);
    if (c->backend == GM_BACKEND_CUDA) {
        cr_raster_cuda_frame_end(&c->fb);
        if (cr_raster_cuda_screen_tris)
            ntris = cr_raster_cuda_screen_tris();
    }
#ifdef MAGMA_METAL
    else if (c->backend == GM_BACKEND_METAL) {
        cr_raster_metal_frame_end(&c->fb);
        if (cr_raster_metal_screen_tris)
            ntris = cr_raster_metal_screen_tris();
    }
#endif
    stamp(frame, 10);

    /* EntityRenderer.setupCameraTransform (oracle-src EntityRenderer.java:746-761)
     * rotate-scale-rotate is on the WORLD modelview only. renderHand
     * (EntityRenderer.java:791-835) loads a fresh gluPerspective
     * (getFOVModifier(*, false)) + identity modelview, then draws the
     * viewmodel and ItemRenderer overlays. Inverse-map the world buffer
     * before those 2D/hand passes. Tick-boundary partialTicks=1 so the
     * warp uses phase+1. */
    if (pv->portal > 0.0f && c->portal_scratch)
        gm_overlay_portal_warp(&c->fb, c->portal_scratch, pv->portal,
                               pv->portal_phase, cam.fov_deg);

    {
        int hx = (int)floorf(cpv->x);
        int hy = (int)floorf(cpv->y + cpv->eye_height);
        int hz = (int)floorf(cpv->z);
        int hsky = gm_world_sky_light(r->world, hx, hy, hz);
        int hblk = gm_world_block_light(r->world, hx, hy, hz);
        if (lm) {
            gm_hand_set_env(lm, (float)hsky, (float)hblk,
                            1.f, 1.f, 1.f, uw.fov_scale,
                            cpv->yaw, cpv->pitch);
        } else {
            CrLightmapRgb hc3 = cr_lightmap_rgb(
                r->dimension, hsky, hblk,
                cr_dimension_sun_brightness(r->dimension), 0.f, 0.f);
            gm_hand_set_env(0, 15.f, 0.f, hc3.r, hc3.g, hc3.b,
                            uw.fov_scale, cpv->yaw, cpv->pitch);
        }
        gm_hand_set_equip(1.0f - c->equip_progress);
        gm_hand_set_hurt(pv->hurt_time, pv->max_hurt_time, pv->hurt_yaw);
        gm_hand_set_item_override(c->equip_item, c->equip_meta,
                                  c->equip_count);
        if (!pv->dead && !pv->riding_boat && !cr_cfg()->no_hand &&
            !frame->skip_hand)
            gm_hand_draw(&c->fb, pv, c->hand_bob);
        if (!pv->dead)
            gm_overlay_block_in_hand_live(&c->fb, &c->atlas, r->world, cpv);
        /* ItemRenderer.renderOverlays uses the hand projection
         * (getFOVModifier(partialTicks, false) = 70 * water 60/70). */
        if (uw.overlay && !pv->dead)
            gm_uw_overlay_draw(&c->fb, cpv, uw.brightness,
                               70.0f * uw.fov_scale);
        if (pv->fire && !pv->creative && !pv->dead)
            gm_hand_fire_overlay_draw(&c->fb, &c->atlas, uw.fov_scale);
    }
    if (pv->portal > 0.0f)
        gm_overlay_portal_screen(&c->fb, &c->atlas, pv->portal);
    if (!frame->skip_hud) {
        if (pv->dead) gm_hud_set_pointer(frame->mouse_x, frame->mouse_y);
        gm_hud_draw(&c->fb, pv);
    }
    if (frame->screen_open && !pv->dead)
        gm_screen_draw(&c->fb, r, frame->mouse_x, frame->mouse_y);
    stamp(frame, 11);

    if (stats) {
        stats->ntris = ntris;
        stats->mesh_kept = mv.n_kept;
        stats->mesh_culled = mv.n_culled;
        for (int i = 0; i < 4; ++i) stats->mesh_nverts[i] = mv.nverts[i];
    }
    return 1;
}

CrFramebuffer *gm_window_compose_framebuffer(GmWindowCompose *c) {
    return c ? &c->fb : NULL;
}

int gm_window_compose_emit_frame(GmWindowCompose *c, int tick,
                                 char *err, int err_cap) {
    if (!c || !c->frames_out[0] || !c->ppm_buf) {
        set_error(err, err_cap, "window compose frame output is not open");
        return 0;
    }
    if (c->npy_f) {
        int n = c->fb.w * c->fb.h;
        for (int i = 0; i < n; ++i) {
            c->ppm_buf[i * 3 + 0] = c->fb.color[i].r;
            c->ppm_buf[i * 3 + 1] = c->fb.color[i].g;
            c->ppm_buf[i * 3 + 2] = c->fb.color[i].b;
        }
        if (fwrite(c->ppm_buf, 3, (size_t)n, c->npy_f) != (size_t)n) {
            set_error(err, err_cap, "cannot write frames-out npy");
            return 0;
        }
        c->npy_frames++;
        return 1;
    }
    if (!emit_ppm(c, tick)) {
        set_error(err, err_cap, "cannot write frames-out image");
        return 0;
    }
    return 1;
}

void gm_window_compose_close(GmWindowCompose *c) {
    if (!c) return;
    if (c->backend_open && c->backend == GM_BACKEND_CUDA && cr_raster_cuda_post)
        cr_raster_cuda_post();
#ifdef MAGMA_METAL
    else if (c->backend_open && c->backend == GM_BACKEND_METAL && cr_raster_metal_post)
        cr_raster_metal_post();
#endif
    if (c->npy_f) {
        npy_stamp_header(c->npy_f, c->npy_frames, c->fb.h, c->fb.w);
        fclose(c->npy_f);
    }
    free(c->tris);
    free(c->entity_verts);
    free(c->ppm_buf);
    free(c->portal_scratch);
    free(c->chunks);
    free(c->runs);
    free(c->gsrc);
    free(c->gcnt);
    cr_fb_free(&c->fb);
    free(c);
}
