#ifndef DIMENSION_SWAP_H
#define DIMENSION_SWAP_H

#include <limits.h>
#include <math.h>
#include <string.h>

MC_HD static inline int cu_dimension_block(const CuDimensionRegion *d,
                                            int x, int y, int z) {
    x -= d->rx0; y -= d->ry0; z -= d->rz0;
    if (!d->cells || x < 0 || y < 0 || z < 0 ||
        x >= d->rnx || y >= d->rny || z >= d->rnz) return -1;
    return mc_state_id(d->cells[((long)x * d->rny + y) * d->rnz + z]);
}
#define PORTAL_HD MC_HD
#define PORTAL_WORLD CuDimensionRegion
#define PORTAL_BLOCK(w,x,y,z) cu_dimension_block(w,x,y,z)
#include "portal_arrival.h"
#undef PORTAL_BLOCK
#undef PORTAL_WORLD
#undef PORTAL_HD

/* runtime.c transit order: contact after world/player tick. A missing region
 * is a sticky execution error; it must not consume the transfer cooldown. */
MC_HD static inline void cu_portal_tick(Blaze *env) {
    int wx, wy, wz, feet, head, in_nether, in_end;
    if (env->dimension_error) return;
    if (env->portal_cooldown > 0) --env->portal_cooldown;
    wx = (int)floor(env->pl.ent.posX + (double)env->ox);
    wy = (int)floor(env->pl.ent.posY);
    wz = (int)floor(env->pl.ent.posZ + (double)env->oz);
    feet = cu_world_block(env, wx, wy, wz);
    head = cu_world_block(env, wx, wy + 1, wz);
    in_nether = (feet == 90 || head == 90);
    in_end = (feet == 119 || head == 119);
    env->in_portal = in_nether ? 1 : (in_end ? 2 : 0);
    if (in_end) {
        env->dimension_error = CU_DIM_ERR_END_UNSUPPORTED;
        return;
    }
    if (in_nether && env->portal_cooldown > 0) env->portal_cooldown = 100;
    if (in_nether && env->portal_cooldown == 0 &&
        (env->dimension == 0 || env->dimension == -1)) {
        if (++env->portal_time >= 82) {
            env->swap_pending = 1;
            env->swap_target_dim = env->dimension == 0 ? -1 : 0;
        }
    } else if (!in_nether) env->portal_time = 0;
}

/* Store geometry with the region, not the transient player arrival pose. */
MC_HD static inline void cu_dimension_store(Blaze *e) {
    CuDimensionRegion *d = &e->dimensions[e->dimension + 1];
    d->cells = e->cells; d->light = e->light; d->biome = e->biome;
    d->rx0 = e->rx0; d->ry0 = e->ry0; d->rz0 = e->rz0;
    d->rnx = e->rnx; d->rny = e->rny; d->rnz = e->rnz;
    d->initialized = 1;
}

MC_HD static inline void cu_dimension_swap_apply(Blaze *env) {
    int target_dim = env->swap_target_dim;
    CuDimensionRegion *d;
    const CuSnapshot *source;
    PortalArrival arrival;
    JavaRandom rng;
    double scale;
    int nx, nz, i;
    if (env->dimension_error) return;
    if (target_dim != 0 && target_dim != -1) {
        env->dimension_error = CU_DIM_ERR_END_UNSUPPORTED; return;
    }
    d = &env->dimensions[target_dim + 1];
    source = target_dim == 0 ? (const CuSnapshot *)env->dim_ow :
        (env->dim_bank ? &((const CuSnapshot *)env->dim_bank)[0] : NULL);
    if (!source || !source->cells || !d->cells || !d->light || !d->biome) {
        env->dimension_error = CU_DIM_ERR_MISSING_BANK; return;
    }
    if (!d->initialized) {
        const RlSnapHead *h = &source->head;
        long vol = (long)h->rnx * h->rny * h->rnz;
        if (h->rnx != env->rnx || h->rny != env->rny || h->rnz != env->rnz ||
            vol <= 0 || vol > env->rvol) {
            env->dimension_error = CU_DIM_ERR_BOUNDS; return;
        }
        memcpy(d->cells, source->cells, (size_t)vol * sizeof(u16));
        if (source->light) memcpy(d->light, source->light, (size_t)vol);
        else memset(d->light, 0, (size_t)vol);
        if (source->biome) memcpy(d->biome, source->biome, (size_t)h->rnx * h->rnz);
        else memset(d->biome, BLAZE_SNAP_BIOME_PLAINS, (size_t)h->rnx * h->rnz);
        d->rx0 = h->rx0; d->ry0 = h->ry0; d->rz0 = h->rz0;
        d->rnx = h->rnx; d->rny = h->rny; d->rnz = h->rnz;
        d->initialized = 1;
    }
    /* PlayerList.transferEntityToWorld scaling; portal search itself is the
     * shared Magma ring-order implementation. Never use snapshot head pose. */
    scale = target_dim == -1 ? 0.125 : 8.0;
    nx = (int)floor((env->pl.ent.posX + env->ox) * scale);
    nz = (int)floor((env->pl.ent.posZ + env->oz) * scale);
    if (portal_plan_arrival(d, nx, nz, &arrival) != 1) {
        env->dimension_error = CU_DIM_ERR_BOUNDS; return;
    }
    cu_dimension_store(env);
    env->cells = d->cells; env->light = d->light; env->biome = d->biome;
    env->rx0 = d->rx0; env->ry0 = d->ry0; env->rz0 = d->rz0;
    env->rnx = d->rnx; env->rny = d->rny; env->rnz = d->rnz;
    env->dimension = target_dim;
    env->ore = source->coal; env->nore = (int)source->ncoal;
    env->ore_xy = source->xy_off;
    env->n_cont = -1; /* rebuild by the exact full-window interaction scan */
    env->n_cand = -1; env->cand_valid = 0; env->world_epoch++;
    env->light_valid = source->light != NULL;
    env->sky_under = NULL; env->sky_under_n = 0;
    cu_grass_census_rebuild(env);
    cu_sky_all_unknown(env);
    cu_fluid_init(env); env->fluid_dim = target_dim;
    if (arrival.create) {
        for (int x = arrival.bx; x < arrival.bx + 4; ++x) {
            cu_world_set_state(env, x, arrival.by - 1, arrival.bz, 49, 0);
            cu_world_set_state(env, x, arrival.by + 3, arrival.bz, 49, 0);
        }
        for (int y = arrival.by; y < arrival.by + 3; ++y) {
            cu_world_set_state(env, arrival.bx, y, arrival.bz, 49, 0);
            cu_world_set_state(env, arrival.bx + 3, y, arrival.bz, 49, 0);
            for (int x = arrival.bx + 1; x <= arrival.bx + 2; ++x)
                cu_world_set_state(env, x, y, arrival.bz, 90, 1);
        }
    }
    env->ccx = psv_floordiv16((int)floor(arrival.x));
    env->ccz = psv_floordiv16((int)floor(arrival.z));
    env->ox = env->ccx * 16; env->oz = env->ccz * 16;
    env->pl.ent.posX = arrival.x - env->ox;
    env->pl.ent.posY = arrival.y;
    env->pl.ent.posZ = arrival.z - env->oz;
    env->pl.ent.box = psv_player_box(env->pl.ent.posX, arrival.y, env->pl.ent.posZ);
    env->pl.ent.motionX = env->pl.ent.motionY = env->pl.ent.motionZ = 0.0;
    env->pl.ent.onGround = 0; env->pl.fall_distance = 0.0f;
    /* Match gm_runtime_set_pose and gm_mobs_init(seed ^ dimension).
     * Projectiles, inventory, world RNG, clocks, and TE stores remain owned
     * by the runtime and survive transit exactly as they do in Magma. */
    env->ctl.dig_progress = 0; env->ctl.dig_hx = INT_MIN;
    env->ctl.dig_hy = env->ctl.dig_hz = 0;
    env->ctl.dig_hitting = env->ctl.dig_delay = env->ctl.atk_prev = env->ctl.left_click_counter = 0;
    env->ctl.eat_ticks = env->ctl.eat_item = env->ctl.hurt_vel_reset = 0;
    env->ctl.server_motion_x = env->ctl.server_motion_z = 0;
    blaze_runtime_close_container(env);
    env->container = 0;
    env->active_chest = env->active_furnace = -1;
    env->container_wx = env->container_wy = env->container_wz = 0;
    memset(env->items, 0, sizeof env->items); env->n_items = 0;
    memset(env->overflow, 0, sizeof env->overflow); env->n_overflow = 0;
    env->spawn_fail_count = 0;
    memset(env->falls, 0, sizeof env->falls); env->n_falls = 0;
    memset(env->fall_updates, 0, sizeof env->fall_updates);
    memset(env->fall_landings, 0, sizeof env->fall_landings);
    env->live_ticks = 0;
    memset(env->mobs, 0, sizeof env->mobs); env->n_mobs = 0;
    env->mob_tick = 0; env->look_have = 0;
    env->look_px = env->look_py = env->look_pz = 0;
    env->player_hurt_resistant = 0; env->player_last_damage = 0;
    env->player_attack_cooldown = 0;
    env->next_orb_id = 1000; env->boat_ride = -1;
    env->parity_xp_pickups = 0;
    memset(env->boat_delta_rot, 0, sizeof env->boat_delta_rot);
    memset(env->boat_glide, 0, sizeof env->boat_glide);
    env->boat_fwd = env->boat_str = 0;
    for (i = 0; i < XL_MAX; ++i) {
        memset(&env->orbs[i], 0, sizeof env->orbs[i]);
        env->orbs[i].dead = 1; env->orb_dim[i] = 0;
    }
    jrand_set(&rng, env->seed ^ (i64)target_dim); env->spawn_world_seed48 = rng.seed;
    jrand_set(&rng, env->seed ^ (i64)target_dim ^ (i64)0x4D415448); env->spawn_math_seed48 = rng.seed;
    jrand_set(&rng, env->seed ^ (i64)target_dim ^ (i64)0x5348464C); env->spawn_shuffle_seed48 = rng.seed;
    cu_recenter_fill(env, env->ccx, env->ccz, 999, 999, 0, 1);
    env->parity_world_valid = env->parity_fluid_cells_valid = 0;
    env->parity_rt_cells_valid = env->parity_fall_cells_valid = 0;
    env->parity_world_mutations = env->parity_fluid_mutations = 0;
    env->parity_rt_mutations = env->parity_fall_mutations = 0;
    env->portal_time = 0; env->portal_cooldown = 100;
    env->swap_pending = 0;
}
#endif
