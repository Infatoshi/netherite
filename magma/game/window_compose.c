#include "game/window_compose.h"

#include "assets/blockmodels.h"
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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern void cr_raster_cuda_pre(int, int, int) __attribute__((weak));
extern void cr_raster_cuda_into(CrFramebuffer *, const CrScreenTri *, int,
                                const CrShadeCtx *) __attribute__((weak));
extern void cr_raster_cuda_frame_begin(const CrFramebuffer *) __attribute__((weak));
extern void cr_raster_cuda_frame_end(CrFramebuffer *) __attribute__((weak));
extern void cr_raster_cuda_sky(const GmSkyCtx *, const float *, int, int)
    __attribute__((weak));
extern void cr_raster_cuda_post(void) __attribute__((weak));

#ifdef MAGMA_METAL
extern void cr_raster_metal_pre(int, int, int) __attribute__((weak));
extern void cr_raster_metal_into(CrFramebuffer *, const CrScreenTri *, int,
                                 const CrShadeCtx *) __attribute__((weak));
extern void cr_raster_metal_frame_begin(const CrFramebuffer *) __attribute__((weak));
extern void cr_raster_metal_frame_end(CrFramebuffer *) __attribute__((weak));
extern void cr_raster_metal_sky(const GmSkyCtx *, const float *, int, int)
    __attribute__((weak));
extern void cr_raster_metal_post(void) __attribute__((weak));
#endif

struct GmWindowCompose {
    CrFramebuffer fb;
    CrScreenTri *tris;
    CrVertex *entity_verts;
    int max_tris;
    int max_entity_verts;
    GmBackend backend;
    GmRuntime *runtime;
    GmParticlesLive *particles;
    CrTexture atlas;
    CrRgba lm_lut[256];
    float hand_bob;
    int swing_ticks;
    int prev_attack;
    GmHudState hud_state;
};

static void set_error(char *err, int cap, const char *msg) {
    if (err && cap > 0) snprintf(err, (size_t)cap, "%s", msg);
}

static void stamp(const GmWindowComposeFrame *f, int slot) {
    if (f->stamp) f->stamp(slot);
}

static int render_layer(GmWindowCompose *c, const CrCamera *cam,
                        const CrVertex *verts, int nv,
                        const CrShadeCtx *sh) {
    if (nv < 3) return 0;
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

static int render_world(GmWindowCompose *c, const CrCamera *cam,
                        const GmMeshView *mv, const CrTexture *atlas,
                        float time_of_day, const GmUnderwater *uw,
                        const CrRgba *lm) {
    int fon = gm_terrain_fog_enabled();
    CrRgba fog = gm_terrain_fog_color(time_of_day);
    const float fst = GM_TERRAIN_FOG_START, fen = GM_TERRAIN_FOG_END;
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
        CrShadeCtx *all[4] = {&sh_solid, &sh_cmip, &sh_cut, &sh_trans};
        for (int i = 0; i < 4; ++i) {
            all[i]->fog_color = uw->fog_rgba;
            all[i]->fog_exp_density = uw->density;
        }
    }
    int ntris = 0;
    ntris += render_layer(c, cam, mv->verts[0], mv->nverts[0], &sh_solid);
    ntris += render_layer(c, cam, mv->verts[1], mv->nverts[1], &sh_cmip);
    ntris += render_layer(c, cam, mv->verts[2], mv->nverts[2], &sh_cut);
    ntris += render_layer(c, cam, mv->verts[3], mv->nverts[3], &sh_trans);
    return ntris;
}

static CrCamera camera_for(const GmPlayerView *v, int w, int h) {
    CrCamera c;
    c.pos.x = v->x;
    c.pos.y = v->y + v->eye_height;
    c.pos.z = v->z;
    c.yaw = gm_view_cam_yaw_rad(v->yaw);
    c.pitch = gm_view_cam_pitch_rad(v->pitch);
    c.fov_deg = 70.0f;
    c.aspect = (float)w / (float)h;
    c.znear = 0.05f;
    c.zfar = GM_TERRAIN_ZFAR;
    c.hurt_yaw_deg = v->hurt_yaw;
    c.hurt_roll_deg = gm_view_hurt_roll_deg(v->hurt_time, v->max_hurt_time);
    return c;
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
    cr_fb_alloc(&c->fb, cfg->width, cfg->height);
    c->tris = malloc((size_t)c->max_tris * sizeof *c->tris);
    c->entity_verts = malloc((size_t)c->max_entity_verts *
                             sizeof *c->entity_verts);
    if (!c->fb.color || !c->tris || !c->entity_verts) {
        set_error(err, err_cap, "window compose allocation failed");
        gm_window_compose_close(c);
        return NULL;
    }
    if (c->backend == GM_BACKEND_CUDA) {
        if (!cr_raster_cuda_pre || !cr_raster_cuda_into ||
            !cr_raster_cuda_frame_begin || !cr_raster_cuda_frame_end ||
            !cr_raster_cuda_sky || !cr_raster_cuda_post) {
            set_error(err, err_cap, "CUDA window compose unavailable");
            gm_window_compose_close(c);
            return NULL;
        }
        cr_raster_cuda_pre(c->fb.w, c->fb.h, c->max_tris);
    }
#ifdef MAGMA_METAL
    else if (c->backend == GM_BACKEND_METAL) {
        if (!cr_raster_metal_pre || !cr_raster_metal_into ||
            !cr_raster_metal_frame_begin || !cr_raster_metal_frame_end ||
            !cr_raster_metal_sky || !cr_raster_metal_post) {
            set_error(err, err_cap, "Metal window compose unavailable");
            gm_window_compose_close(c);
            return NULL;
        }
        cr_raster_metal_pre(c->fb.w, c->fb.h, c->max_tris);
    }
#endif
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
    gm_hud_state_step(&c->hud_state, view, c->runtime->tick);
    float mv_mag = fabsf(action->forward) + fabsf(action->strafe);
    if (mv_mag > 0.01f) c->hand_bob += 0.30f * (float)nticks;
    int attack = action->attack || action->do_break;
    int swing_arm = (attack && !c->prev_attack) || gm_player_dig_swing();
    c->prev_attack = attack;
    if (swing_arm && c->swing_ticks <= 3) c->swing_ticks = 6;
    float swing = c->swing_ticks > 0
        ? (float)(6 - c->swing_ticks) / 6.0f : 0.0f;
    gm_hand_set_swing(swing);
    c->swing_ticks -= nticks;
    if (c->swing_ticks < 0) c->swing_ticks = 0;
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
    const GmPlayerView *pv = frame->view;
    const GmPlayerView *cpv = frame->camera_view;
    CrCamera cam = camera_for(cpv, c->fb.w, c->fb.h);
    GmUnderwater uw;
    {
        float c1 = gm_uw_fog_c1_seed(r->world, r->dimension,
                                     cpv->x, cpv->y, cpv->z);
        gm_uw_eval(r->world, r->dimension, cpv, c1, &uw);
    }
    cam.fov_deg *= uw.fov_scale;
    bm_atlas_set_animation_tick(r->clock.total_time);
    gm_sky_set_eye_height(cpv->eye_height > 0.01f ? cpv->eye_height : 1.62f);
    gm_sky_set_fluid_fog(uw.fluid ? 1 : 0, uw.fog01, uw.density);
    stamp(frame, 3);

    const CrRgba sky = {135, 206, 235, 255};
    cr_fb_clear(&c->fb, uw.fluid ? uw.fog_rgba : sky);
    long long day_tick = r->clock.world_time % 24000LL;
    if (day_tick < 0) day_tick += 24000LL;
    float day = (float)day_tick / 24000.0f;
    int gpu_sky = c->backend != GM_BACKEND_CPU && r->dimension == 0 &&
                  !getenv("MAGMA_CPU_SKY");
    if (!gpu_sky) gm_sky_draw(&c->fb, &cam, day);
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
    GmMeshView mv;
    gm_world_mesh_view(r->world, &cam, c->fb.w, c->fb.h, &mv);
    stamp(frame, 6);
    const CrRgba *lm = NULL;
    if (worldmc_lightmap_mode() && r->dimension == 0) {
        gm_frame_lightmap_fill(&r->sin_table, r->clock.world_time, c->lm_lut);
        lm = c->lm_lut;
    }
    int ntris = render_world(c, &cam, &mv, &c->atlas, day, &uw, lm);
    stamp(frame, 7);

    /* MAGMA_OVERLAY_DUMP: open the selection/crack passes on the headless dump
     * path; unset keeps the interactive-only gate (byte-identical to today). */
    if ((frame->interactive || getenv("MAGMA_OVERLAY_DUMP")) &&
        !frame->screen_open && !r->dead && !getenv("MAGMA_NO_OVERLAY")) {
        static CrVertex sel_ov[GM_OVERLAY_MAX_VERTS];
        static CrVertex crack_ov[GM_OVERLAY_MAX_VERTS];
        int hx = 0, hy = 0, hz = 0, ax, ay, az;
        int have_sel = gm_raycast_sel(r->window, &r->sin_table, &r->player,
                                      &hx, &hy, &hz, &ax, &ay, &az) >= 0;
        float selb[6];
        if (have_sel) gm_sel_box_at(r->window, hx, hy, hz, selb);
        if (have_sel) {
            int ns = gm_overlay_emit_sel(sel_ov, GM_OVERLAY_MAX_VERTS,
                                         hx + r->ox, hy, hz + r->oz, selb,
                                         cam.pos.x, cam.pos.y, cam.pos.z);
            if (ns > 0) {
                CrShadeCtx osh = {0};
                osh.atlas = &c->atlas;
                osh.fog_color = sky;
                osh.alpha_test = 0;
                osh.enable_fog = 0;
                osh.layer = CR_LAYER_TRANSLUCENT;
                osh.blend = 1;
                osh.depth_lequal = 1;
                render_layer(c, &cam, sel_ov, ns, &osh);
            }
        }
        int dx = 0, dy = 0, dz = 0;
        float dmg = 0.0f;
        int have_dig = gm_player_dig_state(&dx, &dy, &dz, &dmg);
        if (have_dig && dmg > 0.0f && !getenv("MAGMA_NO_CRACK")) {
            int nc = gm_overlay_emit_crack(crack_ov, GM_OVERLAY_MAX_VERTS,
                                           dx + r->ox, dy, dz + r->oz, dmg, -1);
            if (nc > 0) {
                CrShadeCtx csh = {0};
                csh.atlas = &c->atlas;
                csh.fog_color = sky;
                csh.alpha_test = 1;
                csh.enable_fog = 0;
                csh.layer = CR_LAYER_CUTOUT;
                csh.blend = 2;
                csh.depth_lequal = 1;
                render_layer(c, &cam, crack_ov, nc, &csh);
            }
        }
    }
    stamp(frame, 8);

    GmEntityView ents[GM_LIVE_MAX];
    int nents = gm_dragon_fill_views(&r->dragon, ents, GM_LIVE_MAX);
    nents += gm_mobs_fill_views(&r->mobs, ents + nents, GM_LIVE_MAX - nents);
    int n_proj0 = nents;
    nents += gm_runtime_projectile_views(r, ents + nents,
                                         GM_LIVE_MAX - nents);
    {
        int ptypes[GM_RUNTIME_PROJECTILES], npt = 0;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (r->projectiles[i].active)
                ptypes[npt++] = r->projectiles[i].type;
        gm_entity_patch_large_fireballs(ptypes, npt, ents + n_proj0,
                                        nents - n_proj0);
    }
    nents += gm_live_fill_views(&r->entities, ents + nents,
                                GM_LIVE_MAX - nents);
    if (r->dragon.initialized) {
        int dt = r->dragon.state.arena.dragon.death_ticks;
        for (int i = 0; i < nents; ++i)
            if (ents[i].type == GM_ENTITY_DRAGON &&
                ents[i].death_ticks <= 0 && dt > 0)
                ents[i].death_ticks = dt;
    }
    gm_frame_entities_light(ents, nents, r->world, r->dimension, lm);
    if (nents > 0) {
        int nv = gm_entities_emit(ents, nents, c->entity_verts,
                                  c->max_entity_verts);
        gm_particles_dragon_latch(r->tick, ents, nents);
        nv += gm_particles_emit(ents, nents, pv->yaw, pv->pitch,
                                c->entity_verts + nv,
                                c->max_entity_verts - nv);
        CrTexture eatlas = gm_entity_atlas();
        CrRgba fog = sky;
        CrShadeCtx esh = {0};
        esh.atlas = &eatlas;
        esh.fog_color = fog;
        esh.alpha_test = 1;
        esh.layer = CR_LAYER_CUTOUT;
        esh.alpha_mask = 1;
        esh.lightmap = lm;
        gm_entity_dissolve_mask(&esh.mask_u_off, &esh.mask_v_off);
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
            render_layer(c, &cam, c->entity_verts, nv, &beam);
        }
        nv = gm_items_emit(ents, nents, c->entity_verts,
                           c->max_entity_verts);
        if (nv > 0) {
            CrShadeCtx ish = {0};
            ish.atlas = &c->atlas;
            ish.fog_color = fog;
            ish.alpha_test = 1;
            ish.layer = CR_LAYER_CUTOUT;
            render_layer(c, &cam, c->entity_verts, nv, &ish);
        }
        nv = gm_items_emit_flat(ents, nents, c->entity_verts,
                                c->max_entity_verts);
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
            render_layer(c, &cam, c->entity_verts, nv, &fire_sh);
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
            dig.fog_color = sky;
            dig.alpha_test = 1;
            dig.layer = CR_LAYER_CUTOUT;
            render_layer(c, &cam, c->entity_verts, nv, &dig);
        }
    }
    stamp(frame, 9);
    if (c->backend == GM_BACKEND_CUDA)
        cr_raster_cuda_frame_end(&c->fb);
#ifdef MAGMA_METAL
    else if (c->backend == GM_BACKEND_METAL)
        cr_raster_metal_frame_end(&c->fb);
#endif
    stamp(frame, 10);

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
        if (!pv->dead && !getenv("MAGMA_NO_HAND"))
            gm_hand_draw(&c->fb, pv, c->hand_bob);
        if (!pv->dead)
            gm_overlay_block_in_hand_live(&c->fb, &c->atlas, r->world, cpv);
        if (uw.overlay && !pv->dead)
            gm_uw_overlay_draw(&c->fb, cpv, uw.brightness, cam.fov_deg);
        if (pv->fire && !pv->creative && !pv->dead)
            gm_hand_fire_overlay_draw(&c->fb, &c->atlas, uw.fov_scale);
    }
    if (pv->portal > 0.0f)
        gm_overlay_portal_screen(&c->fb, &c->atlas, pv->portal);
    if (pv->dead) gm_hud_set_pointer(frame->mouse_x, frame->mouse_y);
    gm_hud_draw(&c->fb, pv);
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

void gm_window_compose_close(GmWindowCompose *c) {
    if (!c) return;
    if (c->backend == GM_BACKEND_CUDA && cr_raster_cuda_post)
        cr_raster_cuda_post();
#ifdef MAGMA_METAL
    else if (c->backend == GM_BACKEND_METAL && cr_raster_metal_post)
        cr_raster_metal_post();
#endif
    free(c->tris);
    free(c->entity_verts);
    cr_fb_free(&c->fb);
    free(c);
}
