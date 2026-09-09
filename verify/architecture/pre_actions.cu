/* Source blaze/env/blaze_core.h SHA256 925f5018c71b36e128d61c12fefcd5a2b6c84272295364d3eda3838198b56e21.
 * Exact reference span: blaze_runtime_player_phase, lines5724-5881 at c078993. */
/* Measurement-only exact extraction from blaze_runtime_player_phase.
 * Copied statements retain source order; no gameplay rule changes.
 * Paired pure_player.cu retains all emitted edits for the existing world phase. */
#include "../../blaze/env/blaze_cuda_int.h"

extern "C" __global__ void measure_pre_actions(Blaze *envs, int n,
    const McSinTable *st, const double *rows, int rep, CuAction *out, int *exec) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || !(exec[i] & 1)) return;
    Blaze *env = envs + i;
    CuAction act;
    CU_OP(env, CU_OP_SUBTICK);
    blaze_act_from_row(&act, rows + (size_t)i * BLAZE_ACT_HEADS, rep);
    exec[i] = 1;
    if (env->dead || env->dimension_error) return;
    /* Magma runtime.c:927-944: use mounts a nearby boat; sneak dismounts;
     * riding suppresses player WASD so controlBoat owns motion. */
    if (act.use && cu_boat_mount(env))
        act.use = 0;
    if (act.sneak && env->boat_ride >= 0)
        cu_boat_dismount(env);
    {
        float boat_fwd = 0.0f, boat_str = 0.0f;
        if (env->boat_ride >= 0) {
            boat_fwd = act.forward;
            boat_str = act.strafe;
            act.forward = 0.0f;
            act.strafe = 0.0f;
        }
        env->boat_fwd = boat_fwd;
        env->boat_str = boat_str;
    }

    /* bow draw / release (runtime.c spawn_bow_arrow). Magma fires on the
     * use falling edge while holding item 261. Eye-of-ender (381) stays
     * reachable via act.use (a[8]) the same way magma maps use_fire. */
    {
        ICStack held_now = isr_get_stack(&env->pl.inv, env->pl.inv.current_item);
        if (held_now.item == 261 && act.use) {
            env->bow_drawing = 1;
            ++env->bow_ticks;
        } else if (env->bow_drawing) {
            float f = pl_bow_curve(env->bow_ticks);
            int pickup = 1;
            int was_active[CU_MAX_PROJECTILES];
            int si;
            /* magma spawn_bow_arrow runtime.c:365-391 */
            if (!(f < 0.1f || !cu_take_arrow(env, &pickup))) {
                if (f > 1.0f) f = 1.0f;
                for (si = 0; si < CU_MAX_PROJECTILES; ++si)
                    was_active[si] = env->projectiles[si].active;
                if (pl_spawn_arrow(env->projectiles, CU_MAX_PROJECTILES,
                                   env->pl.ent.posX + (double)env->ox,
                                   env->pl.ent.posY,
                                   env->pl.ent.posZ + (double)env->oz,
                                   env->pl.yaw, env->pl.pitch, f)) {
                    for (si = 0; si < CU_MAX_PROJECTILES; ++si) {
                        if (was_active[si] || !env->projectiles[si].active)
                            continue;
                        pl_arrow_life_on_spawn(
                            &env->proj_in_ground[si], &env->proj_shake[si],
                            &env->proj_pickup[si],
                            &env->proj_ground_ticks[si], pickup);
                        break;
                    }
                }
            }
            env->bow_drawing = 0;
            env->bow_ticks = 0;
        }
    }

    /* open-container distance check (runtime.c:270-281): live for the full
     * chain (interact opens the table; walking >6 blocks or breaking it
     * closes). gm_container_close is a no-op on an empty grid/cursor
     * (neither is snapshot state and the craft primitive never fills them). */
    if (env->container) {
        float cvx = (float)(env->pl.ent.posX + (double)env->ox);
        float cvy = (float)(env->pl.ent.posY);
        float cvz = (float)(env->pl.ent.posZ + (double)env->oz);
        double dx = (env->container_wx + 0.5) - cvx;
        double dy = (env->container_wy + 0.5) - (cvy + (float)PSV_EYE_HEIGHT);
        double dz = (env->container_wz + 0.5) - cvz;
        int id = cu_world_block(env, env->container_wx, env->container_wy,
                                env->container_wz);
        int valid = env->container == 1 ? id == 58
                  : env->container == 2 ? (id == 61 || id == 62)
                  : env->container == 3 ? id == 54
                  : 0;
        if (!valid || dx * dx + dy * dy + dz * dz > 36.0) {
            if (env->container == 3 && env->active_chest >= 0)
                tec_close(&env->chests[env->active_chest].te);
            blaze_container_close(env);
            env->container = 0;
            env->active_furnace = -1;
            env->active_chest = -1;
        }
    }
    if (act.inv_click)
        (void)blaze_container_click(env, act.inv_slot, act.inv_button,
                                    act.inv_type);

    /* EntityLivingBase.onUpdate ages hurtResistantTime even when --mobs off. */
    if (env->player_hurt_resistant > 0) --env->player_hurt_resistant;

    /* Magma gm_player_left_click_allows peek, then gm_mobs_player_attack. */
    {
        int can_click = 0;
        if (act.attack) {
            int c = env->ctl.left_click_counter;
            if (c > 0) --c;
            can_click = (c <= 0);
        }
        if (can_click && cu_mobs_player_attack(env))
            act.attack_entity = 1;
    }

    /* Magma gm_live_pre_player_tick: landing packets before player raycast. */
    fl_pre_player_tick(env, env);
    /* Magma runtime.c:1071 attack_hits_falling_block after landings. */
    {
        int can_click = 0;
        if (act.attack) {
            int c = env->ctl.left_click_counter;
            if (c > 0) --c;
            can_click = (c <= 0);
        }
        if (can_click && !act.attack_entity &&
            cu_attack_hits_falling_block(env, st))
            act.attack_entity = 1;
    }
    env->pl.health = env->vit.health;
    out[i] = act;
    exec[i] = 3;
}
