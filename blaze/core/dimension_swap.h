#ifndef DIMENSION_SWAP_H
#define DIMENSION_SWAP_H

#include <limits.h>
#include <math.h>
#include <string.h>

#ifndef MC_HD
#if defined(__CUDACC__)
#define MC_HD __host__ __device__
#else
#define MC_HD
#endif
#endif

/* Portal contact and transit tick (runtime.c:1601-1646 port) */
MC_HD static inline void cu_portal_tick(Blaze *env) {
    if (env->portal_cooldown > 0) --env->portal_cooldown;

    int wx = (int)floor(env->pl.ent.posX + (double)env->ox);
    int wy = (int)floor(env->pl.ent.posY);
    int wz = (int)floor(env->pl.ent.posZ + (double)env->oz);
    int feet = cu_world_block(env, wx, wy, wz);
    int head = cu_world_block(env, wx, wy + 1, wz);

    int in_nether_portal = (feet == 90 || head == 90);
    int in_end_portal = (feet == 119 || head == 119);

    env->in_portal = in_nether_portal ? 1 : (in_end_portal ? 2 : 0);
    /* End portal (119): contact is recorded (in_portal=2) but transit is
     * not implemented. No End snapshot bank, no End spawn platform.
     * Block reason: dragon_victory in port_matrix.yaml. */

    /* Entity.setPortal: in-pane collision refreshes pending cooldown */
    if (in_nether_portal && env->portal_cooldown > 0)
        env->portal_cooldown = 100;

    if (in_nether_portal && env->portal_cooldown == 0 && (env->dimension == 0 || env->dimension == -1)) {
        if (++env->portal_time >= 82) {
            /* NOTE: Teleporter.placeInPortal is NOT ported. Magma runs
             * gm_portal_find_or_make on a live-generated destination world
             * (portal_live.c:30-63) and teleports to its result; blaze reads
             * the arrival pose straight out of the destination bank's
             * RlSnapHead (cu_dimension_swap_apply below). The 8:1 / 1:8
             * coordinate scaling that would pick the search origin is
             * therefore not applied anywhere yet - do not add hardcoded
             * per-fixture arrival constants here to paper over that. */
            env->swap_pending = 1;
            env->swap_target_dim = (env->dimension == 0) ? -1 : 0;
            env->portal_cooldown = 100;
            env->portal_time = 0;
        }
    } else if (!in_nether_portal && !in_end_portal) {
        env->portal_time = 0;
    }
}

/* Execute dimension region swap from snapshot bank */
MC_HD static inline void cu_dimension_swap_apply(Blaze *env) {
    int target_dim = env->swap_target_dim;
    const CuSnapshot *target = NULL;
    if (target_dim == 0) {
        /* Return uses this env's assigned overworld snapshot, never a
         * shared snaps[0] alias that would mix seeds across a batch. */
        target = (const CuSnapshot *)env->dim_ow;
    } else {
        int bank_idx = target_dim + 1; /* -1 -> 0, 1 -> 2 */
        const CuSnapshot *bank = (const CuSnapshot *)env->dim_bank;
        if (bank && (bank_idx == 0 || bank_idx == 2))
            target = &bank[bank_idx];
    }

    if (!target || !target->cells) {
        env->swap_pending = 0;
        return;
    }

    /* 1. Swap active region voxels, lighting, and biomes.
     * Fail closed: if the destination region does not fit the env pool there
     * is no swap at all - do NOT fall through and stamp the new dimension id
     * and arrival pose onto the old region. */
    long vol = (long)target->head.rnx * target->head.rny * target->head.rnz;
    if (!env->cells || vol > env->rvol) {
        env->swap_pending = 0;
        return;
    }
    {
        memcpy(env->cells, target->cells, vol * sizeof(u16));
        if (env->light && target->light)
            memcpy(env->light, target->light, vol);
        else if (env->light)
            memset(env->light, 0, vol);
        long bvol = (long)target->head.rnx * target->head.rnz;
        if (env->biome && target->biome)
            memcpy(env->biome, target->biome, bvol);
        env->rx0 = target->head.rx0;
        env->ry0 = target->head.ry0;
        env->rz0 = target->head.rz0;
        env->rnx = target->head.rnx;
        env->rny = target->head.rny;
        env->rnz = target->head.rnz;
    }

    /* 2. Update dimension identity. Inventory, vitals, yaw/pitch, world_rand,
     * update_lcg, and ww.* stay on the env: magma keeps those on GmRuntime
     * (runtime.c:1638-1647) and only replaces the world pointer. */
    env->dimension = target_dim;
    cu_fluid_init(env);
    env->fluid_dim = target_dim;

    /* 3. Arrival pose from the destination bank (Teleporter.placeInPortal is
     * not ported). Yaw/pitch are NOT taken from the bank: magma's
     * gm_runtime_set_pose keeps the pre-transit view (runtime.c:1643). */
    env->ox = target->head.ox;
    env->oz = target->head.oz;
    env->ccx = psv_floordiv16(env->ox);
    env->ccz = psv_floordiv16(env->oz);
    env->pl.ent.posX = target->head.px;
    env->pl.ent.posY = target->head.py;
    env->pl.ent.posZ = target->head.pz;
    for (int b = 0; b < 6; ++b)
        ((double *)&env->pl.ent.box)[b] = target->head.box[b];
    env->pl.ent.motionX = 0.0;
    env->pl.ent.motionY = 0.0;
    env->pl.ent.motionZ = 0.0;
    env->pl.ent.onGround = 0;
    env->pl.fall_distance = 0.0f;

    /* 4. Reset dimension-local transients. Dig/atk match gm_player_dig_reset
     * (player_ctl.c:993) which gm_runtime_set_pose runs on every transit. */
    env->n_items = 0;
    env->n_mobs = 0;
    for (int o = 0; o < XL_MAX; ++o) {
        memset(&env->orbs[o], 0, sizeof env->orbs[o]);
        env->orbs[o].dead = 1;
    }
    for (int p = 0; p < CU_MAX_PROJECTILES; ++p) env->projectiles[p].active = 0;
    for (int f = 0; f < CU_MAX_ITEMS; ++f) env->falls[f].active = 0;
    for (int u = 0; u < CU_FALL_UPDATES; ++u) env->fall_updates[u].active = 0;
    for (int l = 0; l < CU_MAX_ITEMS; ++l) env->fall_landings[l].active = 0;
    env->dig_progress = 0.0f;
    env->dig_hx = INT_MIN;
    env->dig_hy = 0;
    env->dig_hz = 0;
    env->dig_hitting = 0;
    env->dig_delay = 0;
    env->atk_prev = 0;
    env->left_click_counter = 0;
    env->eat_ticks = 0;
    env->eat_item = 0;
    env->hurt_vel_reset = 0;
    env->server_motion_x = 0.0;
    env->server_motion_z = 0.0;
    env->container = 0;
    env->container_wx = 0;
    env->container_wy = 0;
    env->container_wz = 0;

    /* 5. Refill physics window and the randtick section census. memcpy of
     * cells does not go through cu_world_set_state, so grass_sec still held
     * the overworld occupancy and the nether pass drew extra LCG steps. */
    cu_recenter_fill(env, env->ccx, env->ccz, 999, 999, 0, 1);
    cu_grass_census_rebuild(env);
    env->sky_under = NULL;
    env->sky_under_n = 0;
    cu_sky_all_unknown(env);

    /* 6. Re-anchor every parity cache on the destination region, mirroring
     * magma gm_world_parity_configure (world_live.c:634-676): a fresh region
     * means fresh digests and zeroed mutation counters on both sides. Must
     * come after the window refill, which itself writes cells. */
    env->parity_world_valid = 0;
    env->parity_world_mutations = 0;
    env->parity_fluid_cells_valid = 0;
    env->parity_fluid_mutations = 0;
    env->parity_rt_cells_valid = 0;
    env->parity_rt_mutations = 0;
    env->parity_fall_cells_valid = 0;
    env->parity_fall_mutations = 0;

    env->swap_pending = 0;
}

#endif /* DIMENSION_SWAP_H */
