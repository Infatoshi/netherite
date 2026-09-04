/* blaze_cuda.cu - CUDA driver over blaze_core.h, exporting the SAME C ABI as
 * blaze_cpu.c so Python picks a .so at load time. One env per thread for the
 * tick, one thread per pixel for the camera (DESIGN Part 2.3):
 *   k_reset_scalar/_bulk : snapshot restore from the device-resident cache
 *             (host-compacted env list; bulk = 1 thread/cell)
 *   k_tick  : BUILD OPTION BLAZE_SCALAR_TICK, off by default (see
 *             blaze_cuda_int.h). blaze_decision_begin + `repeat` blaze_decision_subtick per env
 *             thread (dyaw/dpitch on sub-tick 0 only; craft/interact
 *             pre-tick on sub-tick 0), full 12-double raw action rows;
 *             physics-window recenter refills run WARP-COOPERATIVELY (see
 *             the kernel comment).
 *             blaze_tick (verify) launches this when warp_tick=0.
 *   k_tick_warp : one env per warp (create opts.warp_tick=1, default;
 *             blaze.conf / ppo.conf / blaze_abi.h). Lane 0 runs the serial
 *             decision body; recenter fill + coal sweep use the 32 lanes.
 *             blaze_tick (verify) launches this when warp_tick=1.
 * The verify-only kernels (k_tick_raw, the emit kernels and the parity
 * kernels) and their host entry points live in blaze_cuda_verify.cu - a
 * SECOND translation unit built from the same headers with the same flags and
 * linked into the same .so. See blaze_cuda_int.h for why.
 *   k_obs   : blaze_render_cam_pixel for envs whose decision frame is fresh,
 *             then copies the persisted frame into the caller's tensors
 *   k_final : blaze_decision_finalize - deferred crosshair/+10 reward terms
 *             (read the k_obs frame's center pixel), 6 scalars, done, pose
 * Envs are fully independent - no shared device state beyond the read-only
 * sin table, recipe table and snapshot cache.
 *
 * Action ABI (mirrors blaze_cpu.c): blaze_step takes double actions[n][12] =
 * the FULL raw action vector in blaze_tick_raw order {forward,strafe,dyaw,
 * dpitch,jump,sneak,sprint,attack,use,hotbar(-1),craft(-1),interact}.
 * craft/interact are pre-tick primitives, applied once before sub-tick 0 in
 * the SAME thread and order as the CPU driver. Legacy 5-head trainer actions
 * are expanded to this layout in blaze.py (bit-identical decode).
 *
 * Pointer convention: blaze_step's actions and all outputs are DEVICE
 * pointers on the .so's device (torch cuda tensors' .data_ptr()); everything
 * else (paths, snap_idx, reset mask, verify-helper buffers) is host memory.
 *
 * Region dims are DYNAMIC (snapshot header, e.g. fresh-spawn t0 snapshots
 * are 128x128x128): the region-sized pools (cells/light, n * rvol each)
 * are allocated ONCE at the FIRST blaze_load_snapshots (dims unknown at
 * create; every loaded snapshot must share them - the loader enforces it).
 *
 * Allocation rule (blaze discipline): every device allocation happens in
 * blaze_create/blaze_load_snapshots; kernels only mutate bytes. The per-env
 * Chunk[9] window and McAABB[512] scratch live in global pools indexed by
 * env id - NEVER on the device stack. t0 (128^3) snapshots MEASURE 8.44
 * MB/env (cells 4M + light 2M + window 1.2M + frame/scratch;
 * mem_get_info around create+load+reset at N=512, 3090) -> N=8192 ~ 69 GB.
 * Was 12.63 MB/env before the cam_cells id copy was deleted 2026-08: it
 * held exactly cells[i]>>4, so oc_block now does the shift instead - see
 * core/obs_camera.h. (Earlier comments here estimated 9.3 MB/env; that
 * accounting omitted light + the frame buffers and read ~3 MB low.)
 * blaze_create/blaze_load_snapshots fail gracefully (NULL / -1) with a GB
 * estimate if any cudaMalloc fails.
 *
 * Build: nvcc -O2 --fmad=false (blaze determinism flags; NEVER fast-math),
 * Makefile targets blaze_cuda_so (magma) and env-cuda (blaze/rl). Both
 * compile this unit and blaze_cuda_verify.cu to objects and link them. Optional kernel timing: create opts.ktime=1;
 * per-kernel totals print to stderr at destroy. */
#include "blaze_cuda_int.h"
#include "blaze_io.h"

/* =================== kernels =================== */

/* reset phase 1: one thread per RESETTING env (host-compacted active list) */
__global__ void k_reset_scalar(Blaze *envs, const int *active, int nactive,
                               const CuSnapDev *snaps, const int *assign,
                               const CuSnapshot *dim_ow,
                               int success_item, int mobs_enabled,
                               int natural_spawn, int natural_spawn_passive,
                               long long world_time_pin,
                               int elytra_kit) {
    int gi = blockIdx.x * blockDim.x + threadIdx.x;
    if (gi >= nactive) return;
    int i = active[gi];
    const CuSnapDev *s = &snaps[assign[i]];
    /* A prior transfer may have changed active pointers. Restore the original
     * pool before reset writes its overworld snapshot, including masked resets. */
    envs[i].cells = envs[i].dimensions[1].cells;
    envs[i].light = envs[i].dimensions[1].light;
    envs[i].biome = envs[i].dimensions[1].biome;
    for (int d = 0; d < 3; ++d) envs[i].dimensions[d].initialized = 0;
    envs[i].dimension_error = 0;
    envs[i].dim_ow = &dim_ow[assign[i]];
    blaze_reset_scalar(&envs[i], &s->head, s->items, s->coal, s->ncoal,
                       s->xy_off, s->cont, s->ncont, s->light != NULL,
                       s->mobs, s->n_mobs, s->orbs, s->n_orbs,
                       s->biome, s->world_rand_seed, success_item);
    envs[i].sky_under = (int *)s->sky_under;
    envs[i].sky_under_n = s->sky_under_n;
    envs[i].pl.fire = s->player_fire;
    envs[i].pl.air = s->player_air;
    {
        int k, n = s->n_potions;
        if (n < 0) n = 0;
        if (n > PSV_POTION_MAX) n = PSV_POTION_MAX;
        psv_potion_clear(&envs[i].pl);
        envs[i].pl.n_potions = n;
        for (k = 0; k < n; ++k) {
            envs[i].pl.potions[k].id = s->potions[k].id;
            envs[i].pl.potions[k].amplifier = s->potions[k].amplifier;
            envs[i].pl.potions[k].duration = s->potions[k].duration;
            envs[i].pl.potions[k].ambient = s->potions[k].ambient;
            envs[i].pl.potions[k].show_particles = s->potions[k].show_particles;
        }
    }
    envs[i].update_lcg = s->update_lcg;
    if (s->head.version >= BLAZE_SNAP_VERSION_RESUME) {
        unsigned pi, fi, ui, si;
        Blaze *e = &envs[i];
        e->ww.totalTime = s->ww_total_time;
        e->ww.worldTime = s->ww_world_time;
        e->ww.rainTime = s->ww_rain_time;
        e->ww.thunderTime = s->ww_thunder_time;
        e->ww.raining = s->ww_raining ? 1 : 0;
        e->ww.thundering = s->ww_thundering ? 1 : 0;
        e->ww.rand.seed = s->ww_rand_seed48 & MC_JR_MASK;
        e->parity_rt_mutations = s->rt_mutations;
        memset(e->projectiles, 0, sizeof e->projectiles);
        memset(e->proj_in_ground, 0, sizeof e->proj_in_ground);
        memset(e->proj_shake, 0, sizeof e->proj_shake);
        memset(e->proj_pickup, 0, sizeof e->proj_pickup);
        memset(e->proj_ground_ticks, 0, sizeof e->proj_ground_ticks);
        e->parity_proj_hits = s->parity_proj_hits;
        for (pi = 0; pi < s->n_proj && pi < CU_MAX_PROJECTILES; ++pi) {
            e->projectiles[pi].active = s->proj[pi].active;
            e->projectiles[pi].type = s->proj[pi].type;
            e->projectiles[pi].age = s->proj[pi].age;
            e->projectiles[pi].x = s->proj[pi].x;
            e->projectiles[pi].y = s->proj[pi].y;
            e->projectiles[pi].z = s->proj[pi].z;
            e->projectiles[pi].vx = s->proj[pi].vx;
            e->projectiles[pi].vy = s->proj[pi].vy;
            e->projectiles[pi].vz = s->proj[pi].vz;
            e->proj_in_ground[pi] = s->proj[pi].in_ground;
            e->proj_shake[pi] = s->proj[pi].shake;
            e->proj_pickup[pi] = s->proj[pi].pickup;
            e->proj_ground_ticks[pi] = s->proj[pi].ground_ticks;
        }
        memset(e->falls, 0, sizeof e->falls);
        e->n_falls = 0;
        for (fi = 0; fi < s->n_fall && fi < CU_MAX_ITEMS; ++fi) {
            e->falls[fi].active = s->falls[fi].active;
            e->falls[fi].type = s->falls[fi].type;
            e->falls[fi].x = s->falls[fi].x;
            e->falls[fi].y = s->falls[fi].y;
            e->falls[fi].z = s->falls[fi].z;
            e->falls[fi].mx = s->falls[fi].mx;
            e->falls[fi].my = s->falls[fi].my;
            e->falls[fi].mz = s->falls[fi].mz;
            e->falls[fi].on_ground = s->falls[fi].on_ground;
            e->falls[fi].age = s->falls[fi].age;
            e->falls[fi].item = s->falls[fi].item;
            e->falls[fi].count = s->falls[fi].count;
            e->falls[fi].meta = s->falls[fi].meta;
            e->falls[fi].pickup_delay = s->falls[fi].pickup_delay;
            e->falls[fi].lifespan = s->falls[fi].lifespan;
            if (e->falls[fi].active) e->n_falls++;
        }
        memset(e->fall_updates, 0, sizeof e->fall_updates);
        for (fi = 0; fi < s->n_fall_upd && fi < CU_FALL_UPDATES; ++fi) {
            e->fall_updates[fi].active = s->fall_upd[fi].active;
            e->fall_updates[fi].x = s->fall_upd[fi].x;
            e->fall_updates[fi].y = s->fall_upd[fi].y;
            e->fall_updates[fi].z = s->fall_upd[fi].z;
            e->fall_updates[fi].block_id = s->fall_upd[fi].block_id;
            e->fall_updates[fi].due_tick = s->fall_upd[fi].due_tick;
        }
        memset(e->fall_landings, 0, sizeof e->fall_landings);
        for (fi = 0; fi < s->n_fall_land && fi < CU_MAX_ITEMS; ++fi) {
            e->fall_landings[fi].active = s->fall_land[fi].active;
            e->fall_landings[fi].x = s->fall_land[fi].x;
            e->fall_landings[fi].y = s->fall_land[fi].y;
            e->fall_landings[fi].z = s->fall_land[fi].z;
            e->fall_landings[fi].block_id = s->fall_land[fi].block_id;
            e->fall_landings[fi].block_meta = s->fall_land[fi].block_meta;
            e->fall_landings[fi].due_tick = s->fall_land[fi].due_tick;
        }
        e->parity_fall_mutations = s->fall_mutations;
        e->live_ticks = s->live_ticks;
        memset(e->furnaces, 0, sizeof e->furnaces);
        e->active_furnace = s->active_furnace;
        for (ui = 0; ui < s->n_furn && ui < CU_MAX_FURNACES; ++ui) {
            e->furnaces[ui].active = s->furn[ui].active;
            e->furnaces[ui].wx = s->furn[ui].wx;
            e->furnaces[ui].wy = s->furn[ui].wy;
            e->furnaces[ui].wz = s->furn[ui].wz;
            e->furnaces[ui].input = sr_mk(s->furn[ui].in_item,
                                         s->furn[ui].in_count,
                                         s->furn[ui].in_meta);
            e->furnaces[ui].fuel = sr_mk(s->furn[ui].fuel_item,
                                        s->furn[ui].fuel_count,
                                        s->furn[ui].fuel_meta);
            e->furnaces[ui].output = sr_mk(s->furn[ui].out_item,
                                          s->furn[ui].out_count,
                                          s->furn[ui].out_meta);
            e->furnaces[ui].burn_time = s->furn[ui].burn_time;
            e->furnaces[ui].current_burn_time = s->furn[ui].current_burn_time;
            e->furnaces[ui].cook_time = s->furn[ui].cook_time;
            e->furnaces[ui].total_cook = s->furn[ui].total_cook;
        }
        memset(e->chests, 0, sizeof e->chests);
        e->active_chest = s->active_chest;
        for (ui = 0; ui < s->n_chest && ui < CU_MAX_CHESTS; ++ui) {
            e->chests[ui].active = s->chest[ui].active;
            e->chests[ui].wx = s->chest[ui].wx;
            e->chests[ui].wy = s->chest[ui].wy;
            e->chests[ui].wz = s->chest[ui].wz;
            e->chests[ui].te.num_players_using = s->chest[ui].num_using;
            for (si = 0; si < BLAZE_SNAP_CHEST_SLOTS; ++si) {
                int ei, n;
                TecStack ts = tec_mk(s->chest[ui].slot[si][0],
                                     s->chest[ui].slot[si][1],
                                     s->chest[ui].slot[si][2]);
                n = s->chest[ui].slot_ench[si].n;
                if (n < 0) n = 0;
                if (n > TEC_MAX_ENCHANTS) n = TEC_MAX_ENCHANTS;
                ts.n_enchants = n;
                for (ei = 0; ei < n; ++ei) {
                    ts.enchants[ei].id = s->chest[ui].slot_ench[si].id[ei];
                    ts.enchants[ei].level = s->chest[ui].slot_ench[si].level[ei];
                }
                e->chests[ui].te.slots[si] = ts;
            }
        }
        for (si = 0; si < 9; ++si)
            e->craft_grid[si] = ic_mk(s->craft[si][0], s->craft[si][1],
                                      s->craft[si][2]);
        e->cursor = ic_mk(s->cursor[0], s->cursor[1], s->cursor[2]);
        e->parity_craft_attempts = s->craft_attempts;
        e->parity_craft_successes = s->craft_successes;
        e->parity_container_opens = s->container_opens;
        e->left_click_counter = s->left_click_counter;
        e->eat_ticks = s->eat_ticks;
        e->eat_item = s->eat_item;
        e->bow_ticks = s->bow_ticks;
        e->bow_drawing = s->bow_drawing;
        e->pl.experienceLevel = s->xp_level;
        e->pl.experienceTotal = s->xp_total;
        e->pl.xpCooldown = s->xp_cooldown;
        e->pl.experience = s->xp_experience;
        for (si = 0; si < 4; ++si)
            isr_set_stack(&e->pl.inv, ISR_ARMOR0 + (int)si,
                          ic_mk(s->armor[si][0], s->armor[si][1],
                                s->armor[si][2]));
        e->fluid_dim = s->fluid_dim;
        e->parity_fluid_mutations = s->fluid_mutations;
        for (si = 0; si < CU_FLUID_REGIONS && si < BLAZE_SNAP_FLUID_REGS; ++si) {
            e->fluid_reg[si].active = s->fluid[si].active;
            e->fluid_reg[si].x0 = s->fluid[si].x0;
            e->fluid_reg[si].y0 = s->fluid[si].y0;
            e->fluid_reg[si].z0 = s->fluid[si].z0;
            e->fluid_reg[si].x1 = s->fluid[si].x1;
            e->fluid_reg[si].y1 = s->fluid[si].y1;
            e->fluid_reg[si].z1 = s->fluid[si].z1;
            e->fluid_reg[si].has_water = s->fluid[si].has_water;
            e->fluid_reg[si].quiet_steps = s->fluid[si].quiet_steps;
        }
        e->boat_ride = s->boat_ride;
        e->explosion_pending = s->explosion_pending;
        e->explosion_smoking = s->explosion_smoking;
        e->explosion_flaming = s->explosion_flaming;
        e->explosion_x = s->explosion_x;
        e->explosion_y = s->explosion_y;
        e->explosion_z = s->explosion_z;
        e->explosion_size = s->explosion_size;
        e->parity_xp_pickups = s->xtra.xp_pickups;
        e->next_orb_id = s->xtra.next_orb_id;
        e->spawn_world_seed48 = s->xtra.spawn_world_seed48;
        e->spawn_math_seed48 = s->xtra.spawn_math_seed48;
        e->spawn_shuffle_seed48 = s->xtra.spawn_shuffle_seed48;
        e->parity_ex_blasts = s->xtra.parity_ex_blasts;
        e->parity_ex_destroyed = s->xtra.parity_ex_destroyed;
        e->parity_ex_drop_n = s->xtra.parity_ex_drop_n;
        e->parity_ex_drop_ids = s->xtra.parity_ex_drop_ids;
        e->parity_ex_damage = s->xtra.parity_ex_damage;
        e->parity_ex_kb_x = s->xtra.parity_ex_kb_x;
        e->parity_ex_kb_y = s->xtra.parity_ex_kb_y;
        e->parity_ex_kb_z = s->xtra.parity_ex_kb_z;
        e->parity_ex_rays = s->xtra.parity_ex_rays;
        e->parity_ex_last_x = s->xtra.parity_ex_last_x;
        e->parity_ex_last_y = s->xtra.parity_ex_last_y;
        e->parity_ex_last_z = s->xtra.parity_ex_last_z;
        e->parity_ex_last_size = s->xtra.parity_ex_last_size;
        e->dead = s->xtra.player_dead ? 1 : 0;
        e->player_hurt_resistant = s->xtra.player_hurt_resistant;
        e->player_attack_cooldown = s->xtra.player_attack_cooldown;
        e->player_last_damage = s->xtra.player_last_damage;
        {
            int ei, n;
            ICStack lc = ic_mk(s->xtra.last_craft[0], s->xtra.last_craft[1],
                               s->xtra.last_craft[2]);
            n = s->xtra.last_craft_ench.n;
            if (n < 0) n = 0;
            if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
            lc.n_enchants = n;
            for (ei = 0; ei < n; ++ei) {
                lc.enchants[ei].id = s->xtra.last_craft_ench.id[ei];
                lc.enchants[ei].level = s->xtra.last_craft_ench.level[ei];
            }
            e->parity_last_craft = lc;
        }
        e->pl.elytra_equipped = s->xtra.elytra_equipped ? 1 : 0;
        e->pl.elytra_flying = s->xtra.elytra_flying ? 1 : 0;
        e->pl.elytra_flying_pending = s->xtra.elytra_pending ? 1 : 0;
        e->pl.elytra_pose = s->xtra.elytra_pose;
        e->pl.ticks_elytra_flying = s->xtra.ticks_elytra_flying;
        e->pl.elytra_wall_damage = s->xtra.elytra_wall_damage;
        for (si = 0; si < s->n_mobs && si < BLAZE_SNAP_MAX_MOBS; ++si) {
            e->boat_delta_rot[si] = s->xtra.boat_delta_rot[si];
            e->boat_glide[si] = s->xtra.boat_glide[si];
            e->mob_repath[si] = s->xtra.sidecar_repath[si];
            e->mob_despawn[si] = s->xtra.sidecar_despawn[si];
            e->mob_fire[si] = s->xtra.sidecar_fire[si];
        }
        for (si = 0; si < 37; ++si) {
            int slot = si < 36 ? (int)si : ISR_OFFHAND_SLOT;
            int ei, n;
            ICStack st = isr_get_stack(&e->pl.inv, slot);
            n = s->xtra.inv_ench[si].n;
            if (n < 0) n = 0;
            if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
            st.n_enchants = n;
            for (ei = 0; ei < n; ++ei) {
                st.enchants[ei].id = s->xtra.inv_ench[si].id[ei];
                st.enchants[ei].level = s->xtra.inv_ench[si].level[ei];
            }
            isr_set_stack(&e->pl.inv, slot, st);
        }
        for (si = 0; si < 4; ++si) {
            int ei, n;
            ICStack st = isr_get_stack(&e->pl.inv, ISR_ARMOR0 + (int)si);
            n = s->xtra.armor_ench[si].n;
            if (n < 0) n = 0;
            if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
            st.n_enchants = n;
            for (ei = 0; ei < n; ++ei) {
                st.enchants[ei].id = s->xtra.armor_ench[si].id[ei];
                st.enchants[ei].level = s->xtra.armor_ench[si].level[ei];
            }
            isr_set_stack(&e->pl.inv, ISR_ARMOR0 + (int)si, st);
        }
        for (si = 0; si < 9; ++si) {
            int ei, n;
            n = s->xtra.craft_ench[si].n;
            if (n < 0) n = 0;
            if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
            e->craft_grid[si].n_enchants = n;
            for (ei = 0; ei < n; ++ei) {
                e->craft_grid[si].enchants[ei].id = s->xtra.craft_ench[si].id[ei];
                e->craft_grid[si].enchants[ei].level =
                    s->xtra.craft_ench[si].level[ei];
            }
        }
        {
            int ei, n;
            n = s->xtra.cursor_ench.n;
            if (n < 0) n = 0;
            if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
            e->cursor.n_enchants = n;
            for (ei = 0; ei < n; ++ei) {
                e->cursor.enchants[ei].id = s->xtra.cursor_ench.id[ei];
                e->cursor.enchants[ei].level = s->xtra.cursor_ench.level[ei];
            }
        }
    }
    envs[i].mobs_enabled = mobs_enabled;
    envs[i].natural_spawn = natural_spawn;
    envs[i].natural_spawn_passive = natural_spawn_passive;
    if (world_time_pin >= 0)
        envs[i].ww.worldTime = world_time_pin;
    envs[i].elytra_kit = elytra_kit;
    if (elytra_kit) {
        if (s->head.version < BLAZE_SNAP_VERSION_RESUME) {
            isr_set_stack(&envs[i].pl.inv, ISR_ARMOR_CHEST,
                          ic_mk(ISR_ELYTRA_ITEM, 1, 0));
            envs[i].pl.elytra_equipped = 1;
        } else {
            ICStack st = isr_get_stack(&envs[i].pl.inv, ISR_ARMOR_CHEST);
            envs[i].pl.elytra_equipped = (st.item == ISR_ELYTRA_ITEM);
        }
    }
}

/* reset phase 2: one thread per bulk cell (region copy + window fill +
 * frame clear) - the single-threaded multi-MB restore made masked resets
 * cost >100 ms. bulk = cu_reset_bulk_count for the (shared) region dims,
 * computed host-side after the scalar phase set the env dims. */
__global__ void k_reset_bulk(Blaze *envs, const int *active,
                             long long nactive, long long bulk,
                             const CuSnapDev *snaps, const int *assign) {
    long long gi = (long long)blockIdx.x * blockDim.x + threadIdx.x;
    if (gi >= nactive * bulk) return;
    int i = active[gi / bulk];
    blaze_reset_bulk(&envs[i], snaps[assign[i]].cells,
                     snaps[assign[i]].light, (long)(gi % bulk));
}

/* Cooperative decision kernel: one env per thread for the tick logic, but
 * physics-window recenter refills run WARP-COOPERATIVELY. The serial
 * per-thread refill (3 chunk gathers + 6 flat copies, ~150k memory ops) was
 * k_tick's dominant cost on open-surface snapshots: one crossing lane
 * stalled its whole warp for the full serial chain (~1 crossing per warp per
 * sub-tick at t0 walk rates). Here every lane of the warp strides over the
 * crossing env's copy/fill cells instead (coalesced, ~32x less warp-stall).
 *
 * Uniform-control-flow contract: NO thread returns early (tail lanes with
 * i >= n and done envs stay in the rep loop as helpers), so the full-mask
 * __ballot_sync/__shfl_sync/__syncwarp collectives are always valid. The
 * refill inputs (env index, post-pose ccx/ccz, shift) are broadcast by
 * value; __syncwarp() orders the owner lane's pose/window writes against
 * the helpers' reads. State evolution is bit-identical to the serial
 * blaze_decision_ticks: same recenter sequence point, same fill values
 * (window bytes are a pure function of region + chunk coords). */
/* cu_clk64 is static inline in blaze_core.h under __CUDACC__ (each TU). */

/* Verify-only: trainer blaze_step passes inv==NULL (13-wide ABI). Focused
 * M2 packs a[13..16] here so chests/furnaces keep the same chain as
 * blaze_tick_raw without editing blaze_subtick_phys. Sequence: after
 * decision_begin, before recenter. */
__device__ __forceinline__ void cu_apply_inv_click(Blaze *e, const double *inv,
                                                   int i) {
    const double *x;
    if (!inv) return;
    x = inv + (size_t)i * 4;
    if ((int)x[0])
        (void)blaze_container_click(e, (int)x[1], (int)x[2], (int)x[3]);
}

#if BLAZE_SCALAR_TICK
__global__ void k_tick(Blaze *envs, int n, const McSinTable *st,
                       const double *actions, int repeat, McAABB *aabb_pool,
                       const CRRecipe *recipes, int nrecipes,
                       double atk_gate, unsigned long long *stage_cycles,
                       const double *inv) {
    const unsigned FULL = 0xffffffffu;
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    int lane = (int)(threadIdx.x & 31u);
    int valid = i < n;
    Blaze *e = &envs[valid ? i : 0];
    int exec = 0;
    unsigned long long t0 = 0, t1 = 0;
    int want_clk = valid && e->phase != NULL;
    (void)stage_cycles;
    if (want_clk) t0 = cu_clk64();
    if (valid && !e->dimension_error)
        exec = blaze_decision_begin(e, st,
                                    actions + (size_t)i * BLAZE_ACT_HEADS,
                                    recipes, nrecipes);
    if (exec)
        cu_apply_inv_click(e, inv, i);
    if (want_clk) {
        t1 = cu_clk64();
        e->phase[CU_PHASE_BEGIN] += t1 - t0;
        t0 = t1;
    }
    for (int rep = 0; rep < repeat; ++rep) {
        int dcx = 0, dcz = 0, ccx = 0, ccz = 0;
        int need = 0;
        if (exec && !e->dead && cu_recenter_pose(e, &dcx, &dcz)) {
            need = 1;
            ccx = e->ccx;
            ccz = e->ccz;
        }
        unsigned m = __ballot_sync(FULL, need);
        if (m) {
            __syncwarp();            /* owner pose writes -> helper reads */
            do {
                int src = __ffs((int)m) - 1;
                m &= m - 1;
                int ei = __shfl_sync(FULL, i, src);
                int tccx = __shfl_sync(FULL, ccx, src);
                int tccz = __shfl_sync(FULL, ccz, src);
                int tdcx = __shfl_sync(FULL, dcx, src);
                int tdcz = __shfl_sync(FULL, dcz, src);
                cu_recenter_fill(&envs[ei], tccx, tccz, tdcx, tdcz, lane, 32);
            } while (m);
            __syncwarp();            /* helper fill writes -> owner reads */
        }
        if (want_clk) {
            t1 = cu_clk64();
            e->phase[CU_PHASE_RECENTER] += t1 - t0;
            t0 = t1;
        }
        if (exec) {
            blaze_decision_subtick(e, st,
                                   actions + (size_t)i * BLAZE_ACT_HEADS,
                                   rep, repeat,
                                   aabb_pool + (size_t)i * PSV_MAX_BLOCKS,
                                   0, atk_gate);
            if (e->done || e->dimension_error) exec = 0;
        }
        if (want_clk) t0 = cu_clk64();
    }
}
#endif /* BLAZE_SCALAR_TICK */

/* ---- warp-per-env tick (BLAZE_WARP_TICK, default ON) ----
 *
 * k_tick runs ONE thread per env: at N=8192 that is 256 warps over ~188 SMs
 * (~1.4/SM) and the whole kernel is dependent-instruction latency (measured:
 * neither FP64 cuts nor coalescing moved the 15 ms). Here each env owns a
 * FULL WARP (8192 resident warps): lane 0 runs the serial physics/reward
 * statements unchanged, the recenter refill reuses the existing cooperative
 * fill over the env's own warp, and the per-sub-tick coal pass fans the
 * candidate sweep across all 32 lanes.
 *
 * Warp-parallel coal selection is BIT-EXACT to the serial sweep because the
 * kept top-32 is a pure function of the eligible set: the order (d2, x, y,
 * z) is a strict total order (block coords unique), every candidate's d2 is
 * computed independently with the same expression, and when the total accept
 * count fits the 512 scratch cap the eligible set is exactly "all non-mined
 * cached candidates" regardless of scan order. The two order-dependent cases
 * fall back to the serial lane-0 sweep: candidate-cache overflow (n_cand<0)
 * and accept-count > CU_COAL_SCRATCH (cap truncation depends on scan order).
 * Nearest-coal takes the argmin by (d, list rank) - identical to the serial
 * first-strictly-lower scan - and only the winning lane evaluates the
 * atan2/asin path, matching the serial code's last-improvement values. */
__device__ __forceinline__ int cu_k2(const Blaze *e, int wx, int wy, int wz) {
    /* region-local pack, monotonic in world (x,y,z) lex order */
    return ((wx - e->rx0) << 16) | ((wy - e->ry0) << 8) | (wz - e->rz0);
}

__device__ void cu_coal_warp(Blaze *env, int lane,
                             int *have_out, double *ry_out, double *rp_out,
                             double *dist_out) {
    const unsigned FULL = 0xffffffffu;
    float fx = (float)(env->pl.ent.posX + (double)env->ox);
    float fy = (float)(env->pl.ent.posY);
    float fz = (float)(env->pl.ent.posZ + (double)env->oz);
    int pwx = (int)floor((double)fx);
    int pwy = (int)floor((double)fy);
    int pwz = (int)floor((double)fz);
    int y0 = pwy - CU_Y_DOWN, y1 = pwy + CU_Y_UP;
    int ncand, c, r, k, total, myacc = 0, nloc = 0, head = 0, fell = 0;
    struct { double d2; int k2, x, y, z; } loc[(CU_COAL_CAND + 31) / 32], own;
    if (y0 < 0)   y0 = 0;
    if (y1 > 255) y1 = 255;
    if (lane == 0) {
        CU_OP(env, CU_OP_COAL_CALL);
        blaze_coal_cache_sync(env, pwx, pwy, pwz, y0, y1);
    }
    __syncwarp();
    ncand = env->n_cand;
    own.d2 = 0.0; own.x = own.y = own.z = 0;
    if (ncand >= 0) {
        for (c = lane; c < ncand; c += 32) {
            CuCand cd = env->coal_cand[c];
            double ddx, ddy, ddz, d2;
            int k2, j;
            if (cd.ri & CU_CAND_MINED) continue;
            ++myacc;
            ddx = cd.x + 0.5 - (double)fx;
            ddy = cd.y + 0.5 - (double)fy;
            ddz = cd.z + 0.5 - (double)fz;
            d2 = ddx * ddx + ddy * ddy + ddz * ddz;
            k2 = cu_k2(env, cd.x, cd.y, cd.z);
            j = nloc - 1;
            for (; j >= 0 && (d2 < loc[j].d2 ||
                              (d2 == loc[j].d2 && k2 < loc[j].k2)); --j)
                loc[j + 1] = loc[j];
            loc[j + 1].d2 = d2; loc[j + 1].k2 = k2;
            loc[j + 1].x = cd.x; loc[j + 1].y = cd.y; loc[j + 1].z = cd.z;
            ++nloc;
        }
        total = myacc;
        for (c = 16; c; c >>= 1)
            total += __shfl_down_sync(FULL, total, c);
        total = __shfl_sync(FULL, total, 0);
        if (total > CU_COAL_SCRATCH)
            fell = 1;             /* cap truncation is scan-order dependent */
        else if (lane == 0)
            CU_OP_ADD(env, CU_OP_COAL_SWEEP, ncand);
    } else {
        fell = 1;                 /* candidate-cache overflow */
        total = 0;
    }
    if (fell) {
        int have = 0;
        double ry = 0.0, rp = 0.0, dist = 0.0;
        if (lane == 0) {
            int coal_now[CU_NCOAL][3];
            /* serial sweep over the already-synced cache (no second
             * CU_OP_COAL_CALL: blaze_coal_list would re-count it) */
            (void)blaze_coal_sweep(env, fx, fy, fz, pwx, pwz, y0, y1,
                                   coal_now);
            have = blaze_nearest_coal(coal_now, (double)fx, (double)fy,
                                      (double)fz, (double)env->pl.yaw,
                                      (double)env->pl.pitch, &ry, &rp, &dist);
        }
        *have_out = __shfl_sync(FULL, have, 0);
        *ry_out = __shfl_sync(FULL, ry, 0);
        *rp_out = __shfl_sync(FULL, rp, 0);
        *dist_out = __shfl_sync(FULL, dist, 0);
        return;
    }
    k = total < CU_NCOAL ? total : CU_NCOAL;
    for (r = 0; r < k; ++r) {
        double hd2 = head < nloc ? loc[head].d2 : 1e300;
        int hk2 = head < nloc ? loc[head].k2 : 0x7fffffff;
        double bd2 = hd2;
        int bk2 = hk2, bl = lane, win;
        for (c = 16; c; c >>= 1) {
            double od2 = __shfl_down_sync(FULL, bd2, c);
            int ok2 = __shfl_down_sync(FULL, bk2, c);
            int ol = __shfl_down_sync(FULL, bl, c);
            if (od2 < bd2 || (od2 == bd2 && ok2 < bk2)) {
                bd2 = od2; bk2 = ok2; bl = ol;
            }
        }
        win = __shfl_sync(FULL, bl, 0);
        {
            int wx = __shfl_sync(FULL, head < nloc ? loc[head].x : 0, win);
            int wy = __shfl_sync(FULL, head < nloc ? loc[head].y : 0, win);
            int wz = __shfl_sync(FULL, head < nloc ? loc[head].z : 0, win);
            if (lane == r) { own.x = wx; own.y = wy; own.z = wz; }
        }
        if (lane == win) ++head;
    }
    {   /* nearest: argmin by (d, rank), winner evaluates the angles */
        double ex = (double)fx, ey = (double)fy + 1.62, ez = (double)fz;
        double dx = 0.0, dy = 0.0, dz = 0.0, d = 1e300;
        double ry = 0.0, rp = 0.0;
        int bl = lane, win;
        if (lane < k) {
            dx = own.x + 0.5 - ex;
            dy = own.y + 0.5 - ey;
            dz = own.z + 0.5 - ez;
            d = sqrt(dx * dx + dy * dy + dz * dz);
        }
        double bd = d;
        for (c = 16; c; c >>= 1) {
            double od = __shfl_down_sync(FULL, bd, c);
            int ol = __shfl_down_sync(FULL, bl, c);
            if (od < bd || (od == bd && ol < bl)) { bd = od; bl = ol; }
        }
        win = __shfl_sync(FULL, bl, 0);
        if (lane == win && k > 0) {
            double dd = d > 1e-9 ? d : 1e-9;
            ry = blaze_wrap180(atan2(-dx, dz) * (180.0 / CU_DEC_PI) -
                               (double)env->pl.yaw);
            rp = -asin(dy / dd) * (180.0 / CU_DEC_PI) -
                 (double)env->pl.pitch;
        }
        *have_out = k > 0;
        *ry_out = __shfl_sync(FULL, ry, win);
        *rp_out = __shfl_sync(FULL, rp, win);
        *dist_out = __shfl_sync(FULL, k > 0 ? bd : 0.0, 0);
    }
}

__global__ void k_tick_warp(Blaze *envs, int n, const McSinTable *st,
                            const double *actions, int repeat,
                            McAABB *aabb_pool, const CRRecipe *recipes,
                            int nrecipes, double atk_gate,
                            unsigned long long *stage_cycles,
                            const double *inv) {
    const unsigned FULL = 0xffffffffu;
    int w = (int)((blockIdx.x * (unsigned)blockDim.x + threadIdx.x) >> 5);
    int lane = (int)(threadIdx.x & 31u);
    if (w >= n) return;                    /* whole tail warp exits together */
    Blaze *e = &envs[w];
    const double *a = actions + (size_t)w * BLAZE_ACT_HEADS;
    int exec = 0;
    unsigned long long t0 = 0, t1 = 0;
    int want_clk = (lane == 0) && (e->phase != NULL);
    (void)stage_cycles;
    if (want_clk) t0 = cu_clk64();
    if (lane == 0 && !e->dimension_error)
        exec = blaze_decision_begin(e, st, a, recipes, nrecipes);
    exec = __shfl_sync(FULL, exec, 0);
    if (lane == 0 && exec)
        cu_apply_inv_click(e, inv, w);
    if (want_clk) {
        t1 = cu_clk64();
        e->phase[CU_PHASE_BEGIN] += t1 - t0;
        t0 = t1;
    }
    for (int rep = 0; rep < repeat; ++rep) {
        int need = 0, ccx = 0, ccz = 0, dcx = 0, dcz = 0;
        if (lane == 0 && exec && !e->dead &&
            cu_recenter_pose(e, &dcx, &dcz)) {
            need = 1;
            ccx = e->ccx;
            ccz = e->ccz;
        }
        need = __shfl_sync(FULL, need, 0);
        if (need) {
            ccx = __shfl_sync(FULL, ccx, 0);
            ccz = __shfl_sync(FULL, ccz, 0);
            dcx = __shfl_sync(FULL, dcx, 0);
            dcz = __shfl_sync(FULL, dcz, 0);
            __syncwarp();        /* owner pose writes -> helper reads */
            cu_recenter_fill(e, ccx, ccz, dcx, dcz, lane, 32);
            __syncwarp();        /* helper fill writes -> owner reads */
        }
        if (want_clk) {
            t1 = cu_clk64();
            e->phase[CU_PHASE_RECENTER] += t1 - t0;
            t0 = t1;
        }
        if (exec) {
            double fx = 0.0, fy = 0.0, fz = 0.0;
            double ry = 0.0, rp = 0.0, dist = 0.0;
            int have_nc = 0;
            if (lane == 0)
                blaze_subtick_phys(e, st, a, rep, repeat,
                                   aabb_pool + (size_t)w * PSV_MAX_BLOCKS,
                                   0, &fx, &fy, &fz);
            __syncwarp();        /* lane-0 world/pose writes -> lane reads */
            if (want_clk) t0 = cu_clk64();
            cu_coal_warp(e, lane, &have_nc, &ry, &rp, &dist);
            if (want_clk) {
                e->phase[CU_PHASE_COAL] += cu_clk64() - t0;
                t0 = cu_clk64();
            }
            if (lane == 0) {
                blaze_subtick_post(e, rep, repeat, atk_gate, have_nc,
                                   ry, rp, dist);
                if (e->done || e->dimension_error) exec = 0;
            }
            if (want_clk)
                e->phase[CU_PHASE_POST] += cu_clk64() - t0;
            exec = __shfl_sync(FULL, exec, 0);
        }
    }
}

__global__ void k_obs(Blaze *envs, int n, const McSinTable *st,
                      unsigned short *cam, unsigned char *depth,
                      unsigned char *edge) {
    int gi = blockIdx.x * blockDim.x + threadIdx.x;
    int i = gi / CU_NPIX, pix = gi % CU_NPIX;
    if (i >= n) return;
    Blaze *e = &envs[i];
    if (e->dimension_error) return;
    if (e->dec_cam_fresh)
        blaze_render_cam_pixel(e, st, pix);
    if (cam)   cam[(size_t)i * CU_NPIX + pix] = e->cam[pix];
    if (depth) depth[(size_t)i * CU_NPIX + pix] = e->dep[pix];
    if (edge)  edge[(size_t)i * CU_NPIX + pix] = e->edg[pix];
}

__global__ void k_final(Blaze *envs, int n, const McSinTable *st,
                        float *scal, float *rew,
                        unsigned char *done, float *pose, double atk_gate,
                        int *status) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n || envs[i].dimension_error) return;
    blaze_decision_finalize(&envs[i], st,
                            scal ? scal + (size_t)i * 6 : NULL,
                            rew ? rew + i : NULL,
                            done ? done + i : NULL,
                            pose ? pose + (size_t)i * 5 : NULL,
                            atk_gate);
    if (status) blaze_fill_status(&envs[i], status + (size_t)i * CU_STATUS_K);
}

/* =================== host driver =================== */

void *blaze_create(int device, int n, const BlazeCreateOpts *opts) {
    CuVecCu *v;
    int i;
    BlazeCreateOpts o;
    if (n <= 0) return NULL;
    if (opts) o = *opts;
    else blaze_create_opts_default(&o);
    /* k_tick_legacy (the pre-cooperative serial-recenter A/B control) is
     * deleted. The opts field stays for ABI and .conf compatibility, but a
     * nonzero value no longer selects anything, so refuse it instead of
     * running a different kernel than the caller asked for. */
    if (o.legacy_recenter) {
        fprintf(stderr, "blaze_cuda: legacy_recenter=%d is not supported: "
                        "k_tick_legacy was removed. Set legacy_recenter = 0.\n",
                o.legacy_recenter);
        return NULL;
    }
#if !BLAZE_SCALAR_TICK
    /* The scalar one-env-per-thread k_tick is a build option, off by default:
     * it is a whole extra inline of the sim for a kernel only the M2 scalar
     * gate row runs. The warp_tick knob stays; a build without the kernel
     * refuses warp_tick=0 rather than silently running the warp kernel. */
    if (!o.warp_tick) {
        fprintf(stderr, "blaze_cuda: warp_tick=0 needs the scalar k_tick, "
                        "which this .so was not built with. Rebuild with "
                        "BLAZE_SCALAR_TICK=1, or use warp_tick=1.\n");
        return NULL;
    }
#endif
    if (cu_ck(cudaSetDevice(device), "cudaSetDevice")) return NULL;
    /* BlockDynamicLiquid.getSlopeDistance recurses to depth 4 (java:178/196).
     * Default CUDA stack is 1024 B; live CA overflows it (k_tick_raw IMA).
     * Hostile live tick inlines add another frame; 32 KB was not enough.
     * getBlockDensity DDA (Explosion.java:2456) plus TNT tick overflowed 64 KB
     * (k_tick_raw misaligned address on explosions M2).
     * The limit costs VRAM: SMs * max_threads_per_SM * stack_kib. On an
     * RTX PRO 6000 (188 SM, 1536 threads/SM) 128 KiB reserves ~35.5 GiB.
     * opts.stack_kib lowers it for VRAM headroom experiments. Default 128
     * keeps shipped behaviour. Overflow depends on world content, so only
     * lower it after the explosion, liquid CA, and hostile tick repros pass. */
    {
        int kib = o.stack_kib > 0 ? o.stack_kib : 128;
        if (cu_ck(cudaDeviceSetLimit(cudaLimitStackSize,
                                     (size_t)kib * 1024),
                  "cudaLimitStackSize"))
            return NULL;
    }
    v = (CuVecCu *)calloc(1, sizeof *v);
    if (!v) return NULL;
    v->n = n;
    v->device = device;
    v->success_item = 263;
    v->world_time_pin = -1;
    v->ktime = o.ktime ? 1 : 0;
    v->stage_time = o.stage_time ? 1 : 0;
    v->warp_tick = o.warp_tick;   /* default 1; 0 = flat k_tick */
    v->op_trace = o.op_trace ? 1 : 0;
    v->no_ore_xy = o.no_ore_xy ? 1 : 0;
    v->nether_bank_path[0] = 0;
    v->end_bank_path[0] = 0;
    if (o.nether_bank && o.nether_bank[0])
        snprintf(v->nether_bank_path, sizeof v->nether_bank_path, "%s",
                 o.nether_bank);
    if (o.end_bank && o.end_bank[0])
        snprintf(v->end_bank_path, sizeof v->end_bank_path, "%s", o.end_bank);
    /* Named dimension banks are uploaded during snapshot loading. */
    v->h_assign = (int *)calloc((size_t)n, sizeof *v->h_assign);
    v->h_active = (int *)calloc((size_t)n, sizeof *v->h_active);
    v->h_envs = (Blaze *)calloc((size_t)n, sizeof *v->h_envs);
    if (!v->h_assign || !v->h_active || !v->h_envs) {
        free(v->h_assign); free(v->h_active); free(v->h_envs); free(v);
        return NULL;
    }
    for (i = 0; i < n; ++i) v->h_assign[i] = -1;

    if (cudaStreamCreate(&v->stream) != cudaSuccess ||
        cudaMalloc(&v->d_envs, (size_t)n * sizeof(Blaze)) != cudaSuccess ||
        cudaMalloc(&v->d_st, sizeof(McSinTable)) != cudaSuccess ||
        cudaMalloc(&v->d_tn, sizeof(CpPerlin)) != cudaSuccess ||
        cudaMalloc(&v->d_assign, (size_t)n * sizeof(int)) != cudaSuccess ||
        cudaMalloc(&v->d_active, (size_t)n * sizeof(int)) != cudaSuccess ||
        cudaMalloc(&v->d_cam,
                   (size_t)n * CU_NPIX * sizeof(u16)) != cudaSuccess ||
        cudaMalloc(&v->d_dep, (size_t)n * CU_NPIX) != cudaSuccess ||
        cudaMalloc(&v->d_edg, (size_t)n * CU_NPIX) != cudaSuccess ||
        cudaMalloc(&v->d_window,
                   (size_t)n * PSV_NCHUNKS * sizeof(Chunk)) != cudaSuccess ||
        cudaMalloc(&v->d_cand,
                   (size_t)n * CU_COAL_CAND * sizeof(CuCand)) != cudaSuccess ||
        cudaMalloc(&v->d_cont,
                   (size_t)n * BLAZE_SNAP_MAX_CONT * 3 *
                       sizeof(int)) != cudaSuccess ||
        cudaMalloc(&v->d_aabb,
                   (size_t)n * PSV_MAX_BLOCKS * sizeof(McAABB)) != cudaSuccess ||
        cudaMalloc(&v->d_recipes,
                   (size_t)CRF_NRECIPES * sizeof(CRRecipe)) != cudaSuccess ||
        cudaMalloc(&v->d_snaps,
                   (size_t)BLAZE_MAX_SNAPS * sizeof(CuSnapDev)) != cudaSuccess ||
        cudaMalloc(&v->d_dim_ow,
                   (size_t)BLAZE_MAX_SNAPS * sizeof(CuSnapshot)) != cudaSuccess ||
        cudaMalloc(&v->d_dim_bank, 3 * sizeof(CuSnapshot)) != cudaSuccess ||
        cudaMalloc(&v->d_dimension_error, sizeof(int)) != cudaSuccess ||
        cudaMalloc(&v->d_obs, sizeof(CuBinObs)) != cudaSuccess ||
        cudaMalloc(&v->d_fluid_cur,
                   (size_t)n * CU_FLUID_VOL * sizeof(u16)) != cudaSuccess ||
        cudaMalloc(&v->d_fluid_tmp,
                   (size_t)n * CU_FLUID_VOL * sizeof(u16)) != cudaSuccess ||
        cudaMalloc(&v->d_rt_leaf,
                   (size_t)n * RT_LIVE_SURR * sizeof(int)) != cudaSuccess ||
        cudaMalloc(&v->d_light_q,
                   (size_t)n * CU_LIGHT_Q * sizeof(int)) != cudaSuccess) {
        fprintf(stderr, "blaze_cuda: cudaMalloc failed for n=%d fixed pools "
                        "(~%.1f GB; region pools come later at snapshot "
                        "load)\n",
                n, (double)n * ((double)PSV_NCHUNKS * sizeof(Chunk) +
                                PSV_MAX_BLOCKS * sizeof(McAABB) +
                                CU_COAL_CAND * sizeof(CuCand) +
                                2.0 * CU_FLUID_VOL * sizeof(u16) +
                                (double)RT_LIVE_SURR * sizeof(int) +
                                (double)CU_LIGHT_Q * sizeof(int) +
                                sizeof(Blaze)) / 1e9);
        blaze_destroy(v);
        return NULL;
    }

    if (cu_ck(cudaMemset(v->d_dim_bank, 0, 3 * sizeof(CuSnapshot)),
              "dimension bank init")) {
        blaze_destroy(v);
        return NULL;
    }

    {   /* upload the LUT sin table + the crf recipe table (built once) */
        McSinTable *h_st = (McSinTable *)malloc(sizeof *h_st);
        CpPerlin *h_tn = (CpPerlin *)malloc(sizeof *h_tn);
        CRRecipe *h_rec =
            (CRRecipe *)malloc((size_t)CRF_NRECIPES * sizeof *h_rec);
        if (!h_st || !h_tn || !h_rec) {
            free(h_st); free(h_tn); free(h_rec); blaze_destroy(v);
            return NULL;
        }
        mc_sin_table_init(h_st);
        rt_live_temperature_noise_init(h_tn);
        v->nrecipes = crf_build(h_rec);
        if (cu_ck(cudaMemcpy(v->d_st, h_st, sizeof *h_st,
                             cudaMemcpyHostToDevice), "st upload") ||
            cu_ck(cudaMemcpy(v->d_tn, h_tn, sizeof *h_tn,
                             cudaMemcpyHostToDevice), "tn upload") ||
            cu_ck(cudaMemcpy(v->d_recipes, h_rec,
                             (size_t)v->nrecipes * sizeof *h_rec,
                             cudaMemcpyHostToDevice), "recipes upload")) {
            free(h_st); free(h_tn); free(h_rec); blaze_destroy(v);
            return NULL;
        }
        free(h_st); free(h_tn);
        free(h_rec);
    }

    if (v->op_trace) {
        size_t nb = (size_t)n * CU_OP_N * sizeof(unsigned long long);
        if (cudaMalloc(&v->d_ops, nb) != cudaSuccess) {
            fprintf(stderr, "blaze_cuda: op-trace counter alloc failed\n");
            blaze_destroy(v);
            return NULL;
        }
        cudaMemset(v->d_ops, 0, nb);
    }

    /* stage env structs host-side with the create-time pool pointers;
     * cells/light stay NULL until the first snapshot load sizes the
     * region pools. Uploaded (again) there. */
    for (i = 0; i < n; ++i) {
        Blaze *e = &v->h_envs[i];
        e->cam = v->d_cam + (size_t)i * CU_NPIX;
        e->dep = v->d_dep + (size_t)i * CU_NPIX;
        e->edg = v->d_edg + (size_t)i * CU_NPIX;
        e->window = v->d_window + (size_t)i * PSV_NCHUNKS;
        e->coal_cand = v->d_cand + (size_t)i * CU_COAL_CAND;
        e->cont = v->d_cont + (size_t)i * BLAZE_SNAP_MAX_CONT * 3;
        e->fluid_cur = v->d_fluid_cur + (size_t)i * CU_FLUID_VOL;
        e->fluid_tmp = v->d_fluid_tmp + (size_t)i * CU_FLUID_VOL;
        e->rt_leaf = v->d_rt_leaf + (size_t)i * RT_LIVE_SURR;
        e->light_q = v->d_light_q + (size_t)i * CU_LIGHT_Q;
        e->ops = v->d_ops ? v->d_ops + (size_t)i * CU_OP_N : NULL;
        e->tn = v->d_tn;
        e->phase = NULL;
    }
    if (cu_ck(cudaMemcpy(v->d_envs, v->h_envs, (size_t)n * sizeof(Blaze),
                         cudaMemcpyHostToDevice), "env upload")) {
        blaze_destroy(v);
        return NULL;
    }

    if (v->ktime)
        for (i = 0; i < 4; ++i) cudaEventCreate(&v->ev[i]);
    if (v->stage_time) {
        size_t nb = (size_t)n * (size_t)CU_PHASE_K * sizeof(unsigned long long);
        v->h_phase = (unsigned long long *)calloc((size_t)n * (size_t)CU_PHASE_K,
                                                  sizeof(unsigned long long));
        if (!v->h_phase || cudaMalloc(&v->d_phase, nb) != cudaSuccess) {
            fprintf(stderr, "blaze_cuda: stage_time counter alloc failed\n");
            blaze_destroy(v);
            return NULL;
        }
        cudaMemset(v->d_phase, 0, nb);
        for (i = 0; i < n; ++i)
            v->h_envs[i].phase = v->d_phase + (size_t)i * (size_t)CU_PHASE_K;
        if (cu_ck(cudaMemcpy(v->d_envs, v->h_envs, (size_t)n * sizeof(Blaze),
                             cudaMemcpyHostToDevice), "phase ptr upload")) {
            blaze_destroy(v);
            return NULL;
        }
    }
    return v;
}

static void cu_retire(CuVecCu *v, const void *p) {
    if (!p || !v) return;
    if (v->nretired < (int)(sizeof v->retired / sizeof v->retired[0]))
        v->retired[v->nretired++] = p;
}

void blaze_destroy(void *vh) {
    CuVecCu *v = (CuVecCu *)vh;
    int i;
    if (!v) return;
    cudaSetDevice(v->device);
    if (v->ktime && v->nsteps) {
        fprintf(stderr,
                "blaze_cuda ktime: %ld steps  k_tick %.1f ms (%.3f ms/step)  "
                "k_obs %.1f ms (%.3f ms/step)  k_final %.1f ms (%.3f ms/step)\n",
                v->nsteps, v->ms_tick, v->ms_tick / v->nsteps,
                v->ms_obs, v->ms_obs / v->nsteps,
                v->ms_final, v->ms_final / v->nsteps);
        for (i = 0; i < 4; ++i) cudaEventDestroy(v->ev[i]);
    }
    if (v->stage_time && v->d_phase && v->h_phase) {
        static const char *nm[CU_PHASE_K] = {
            "begin", "recenter", "phys", "light", "blk", "fluid",
            "randtick", "items", "mobs", "coal", "post", "rest",
            "rt_prefix", "rt_sec", "rt_pick", "rt_handler",
            "skycol", "skyseed", "skydrain"
        };
        unsigned long long tot[CU_PHASE_K], sum = 0;
        int k, ei;
        size_t nb = (size_t)v->n * (size_t)CU_PHASE_K * sizeof(unsigned long long);
        memset(tot, 0, sizeof tot);
        cudaMemcpy(v->h_phase, v->d_phase, nb, cudaMemcpyDeviceToHost);
        for (ei = 0; ei < v->n; ++ei)
            for (k = 0; k < CU_PHASE_K; ++k)
                tot[k] += v->h_phase[(size_t)ei * (size_t)CU_PHASE_K + (size_t)k];
        for (k = 0; k < CU_PHASE_K; ++k) sum += tot[k];
        if (sum) {
            fprintf(stderr, "blaze_cuda stage_time (thread-cycle share of k_tick):\n");
            for (k = 0; k < CU_PHASE_K; ++k)
                fprintf(stderr, "  %10s %6.1f%%  cycles=%llu\n", nm[k],
                        100.0 * (double)tot[k] / (double)sum,
                        (unsigned long long)tot[k]);
        }
        cudaFree(v->d_phase);
        v->d_phase = NULL;
        free(v->h_phase);
        v->h_phase = NULL;
    }
    for (i = 0; i < v->nsnaps; ++i) {
        cudaFree((void *)v->h_snaps[i].cells);
        cudaFree((void *)v->h_snaps[i].light);
        cudaFree((void *)v->h_snaps[i].sky_under);
        cudaFree((void *)v->h_snaps[i].biome);
        cudaFree((void *)v->h_snaps[i].coal);
        cudaFree((void *)v->h_snaps[i].xy_off);
        cudaFree((void *)v->h_snaps[i].cont);
    }
    for (i = 0; i < v->nretired; ++i)
        cudaFree((void *)v->retired[i]);
    cudaFree(v->d_ops);
    cudaFree(v->d_obs);
    cudaFree(v->d_tick_act);
    cudaFree(v->d_tick_inv);
    free(v->h_tick_act);
    free(v->h_tick_inv);
    cudaFree(v->d_obs_all);
    cudaFree(v->d_parity_all);
    for (i = 0; i < 3; ++i) {
        cudaFree(v->h_dim_bank[i].cells);
        cudaFree(v->h_dim_bank[i].light);
        cudaFree(v->h_dim_bank[i].biome);
        cudaFree(v->h_dim_bank[i].coal);
        cudaFree(v->h_dim_bank[i].xy_off);
        cudaFree(v->h_dim_bank[i].cont);
        cudaFree(v->d_dim_cells[i]);
        cudaFree(v->d_dim_light[i]);
        cudaFree(v->d_dim_biome[i]);
    }
    cudaFree(v->d_dim_bank);
    cudaFree(v->d_dim_ow);
    cudaFree(v->d_dimension_error);
    cudaFree(v->d_snaps);
    cudaFree(v->d_recipes);
    cudaFree(v->d_aabb);
    cudaFree(v->d_cont);
    cudaFree(v->d_cand);
    cudaFree(v->d_window);
    cudaFree(v->d_fluid_cur);
    cudaFree(v->d_fluid_tmp);
    cudaFree(v->d_rt_leaf);
    cudaFree(v->d_light_q);
    cudaFree(v->d_edg);
    cudaFree(v->d_dep);
    cudaFree(v->d_cam);
    cudaFree(v->d_grass);
    cudaFree(v->d_cells);
    cudaFree(v->d_light);
    cudaFree(v->d_biome);
    cudaFree(v->d_active);
    cudaFree(v->d_assign);
    cudaFree(v->d_st);
    cudaFree(v->d_tn);
    cudaFree(v->d_envs);
    if (v->stream) cudaStreamDestroy(v->stream);
    free(v->h_envs);
    free(v->h_assign);
    free(v->h_active);
    free(v);
}

/* Size the region pools (n * rvol cells + light) from the first-loaded
 * snapshot's dims, patch the staged env structs' pointers and re-upload
 * them. Init-time only; all further snapshots must match the dims. */
static int cu_alloc_region_pools(CuVecCu *v, int rnx, int rny, int rnz,
                                 char *err, int err_cap) {
    int i;
    long rvol = (long)rnx * rny * rnz;
    long bvol = (long)rnx * (long)rnz;
    /* worst-case section grid for these dims over any region origin */
    long nsec = (long)CU_SEC_SPAN(rnx) * CU_SEC_SPAN(rny) * CU_SEC_SPAN(rnz);
    double gb = (sizeof(u16) + sizeof(u8)) *
                (double)v->n * rvol / 1e9;
    if (cudaMalloc(&v->d_cells,
                   (size_t)v->n * rvol * sizeof(u16)) != cudaSuccess ||
        cudaMalloc(&v->d_light,
                   (size_t)v->n * rvol * sizeof(u8)) != cudaSuccess ||
        cudaMalloc(&v->d_biome,
                   (size_t)v->n * (size_t)bvol) != cudaSuccess ||
        cudaMalloc(&v->d_grass,
                   (size_t)v->n * nsec * sizeof(u16)) != cudaSuccess) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap,
                     "region pool cudaMalloc failed (%dx%dx%d x %d envs = "
                     "%.1f GB)", rnx, rny, rnz, v->n, gb);
        cudaFree(v->d_light); v->d_light = NULL;
        cudaFree(v->d_biome); v->d_biome = NULL;
        cudaFree(v->d_grass); v->d_grass = NULL;
        cudaFree(v->d_cells); v->d_cells = NULL;
        return 0;
    }
    v->rnx = rnx; v->rny = rny; v->rnz = rnz;
    v->rvol = rvol;
    for (i = 0; i < v->n; ++i) {
        v->h_envs[i].cells = v->d_cells + (size_t)i * rvol;
        v->h_envs[i].light = v->d_light + (size_t)i * rvol;
        v->h_envs[i].biome = v->d_biome + (size_t)i * (size_t)bvol;
        v->h_envs[i].grass_sec = v->d_grass + (size_t)i * nsec;
        v->h_envs[i].dimensions[1].cells = v->h_envs[i].cells;
        v->h_envs[i].dimensions[1].light = v->h_envs[i].light;
        v->h_envs[i].dimensions[1].biome = v->h_envs[i].biome;
        v->h_envs[i].dim_bank = v->d_dim_bank;
    }
    if (cudaMemcpy(v->d_envs, v->h_envs, (size_t)v->n * sizeof(Blaze),
                   cudaMemcpyHostToDevice) != cudaSuccess) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "env re-upload failed");
        return 0;
    }
    return 1;
}

/* Only the reduction scratch is cleared here. A failed transfer stays visible
 * on the environment until an explicit reset, across every stepping API. */
__global__ void k_dimension_status(const Blaze *envs, int n, int *error) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n && envs[i].dimension_error)
        atomicMax(error, envs[i].dimension_error);
}

int cu_dimension_status(CuVecCu *v, Blaze *envs, int n) {
    int error = 0;
    if (cu_ck(cudaMemsetAsync(v->d_dimension_error, 0, sizeof(int), v->stream),
              "dimension status clear")) return -1;
    k_dimension_status<<<(n + CU_TPB - 1) / CU_TPB, CU_TPB, 0, v->stream>>>(
        envs, n, v->d_dimension_error);
    if (cu_ck(cudaMemcpyAsync(&error, v->d_dimension_error, sizeof error,
                              cudaMemcpyDeviceToHost, v->stream),
              "dimension status read") ||
        cu_ck(cudaStreamSynchronize(v->stream), "dimension status")) return -1;
    if (error) {
        fprintf(stderr, "blaze_cuda: dimension transfer failed (code %d)\n", error);
        return -1;
    }
    return 0;
}

__global__ void k_bind_dimension_pool(Blaze *envs, int n, int bank,
                                      u16 *cells, u8 *light, u8 *biome,
                                      long rvol, long bvol) {
    int i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i >= n) return;
    CuDimensionRegion *r = &envs[i].dimensions[bank];
    r->cells = cells + (size_t)i * rvol;
    r->light = light + (size_t)i * rvol;
    r->biome = biome + (size_t)i * bvol;
    r->initialized = 0;
}

/* The shared transfer implementation reads CuSnapshot, not CuSnapDev. Keep
 * its metadata in that exact layout and point into the reset cache's immutable
 * allocations. No ownership or host pointers cross into this device table. */
static int cu_upload_dimension_ow(CuVecCu *v, int slot) {
    const CuSnapDev *d = &v->h_snaps[slot];
    CuSnapshot s;
    memset(&s, 0, sizeof s);
    s.head = d->head;
    s.cells = (u16 *)d->cells;
    s.light = (u8 *)d->light;
    s.biome = (u8 *)d->biome;
    s.coal = (int *)d->coal;
    s.ncoal = d->ncoal;
    s.xy_off = (int *)d->xy_off;
    s.cont = (int *)d->cont;
    s.ncont = d->ncont;
    return cu_ck(cudaMemcpy(v->d_dim_ow + slot, &s, sizeof s,
                            cudaMemcpyHostToDevice), "dimension overworld upload");
}

static int cu_upload_bank_bytes(void **out, const void *src, size_t n) {
    *out = NULL;
    if (!src || !n) return 0;
    if (cudaMalloc(out, n) != cudaSuccess) return -1;
    if (cudaMemcpy(*out, src, n, cudaMemcpyHostToDevice) != cudaSuccess) {
        cudaFree(*out);
        *out = NULL;
        return -1;
    }
    return 0;
}

static int cu_load_named_bank(CuVecCu *v, int bank, const char *path,
                              const char *label, char *err, int err_cap) {
    CuSnapshot source, uploaded;
    const RlSnapHead *h;
    size_t vol, bvol;
    u16 *cells = NULL;
    u8 *light = NULL, *biome = NULL;
    if (!path || !path[0]) return 0;
    if (v->h_dim_bank[bank].cells) {
        if (!strcmp(path, v->dim_bank_loaded_path[bank])) return 0;
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "%s bank already loaded from %s; refusing %s",
                     label, v->dim_bank_loaded_path[bank], path);
        return -1;
    }
    if (strlen(path) >= sizeof v->dim_bank_loaded_path[bank]) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "%s bank path too long", label);
        return -1;
    }
    memset(&uploaded, 0, sizeof uploaded);
    if (!blaze_snapshot_load(path, &source, err, err_cap, v->no_ore_xy)) return -1;
    h = &source.head;
    if (!v->rvol || h->rnx != v->rnx || h->rny != v->rny || h->rnz != v->rnz) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap,
                     "%s bank region dims %dx%dx%d != pool %dx%dx%d: %s",
                     label, h->rnx, h->rny, h->rnz, v->rnx, v->rny, v->rnz, path);
        blaze_snapshot_free(&source);
        return -1;
    }
    vol = (size_t)v->rvol;
    bvol = (size_t)v->rnx * v->rnz;
    uploaded.head = source.head;
    uploaded.ncoal = source.ncoal;
    uploaded.ncont = source.ncont;
    if (cu_upload_bank_bytes((void **)&uploaded.cells, source.cells, vol * sizeof(u16)) ||
        cu_upload_bank_bytes((void **)&uploaded.light, source.light, vol) ||
        cu_upload_bank_bytes((void **)&uploaded.biome, source.biome, bvol) ||
        cu_upload_bank_bytes((void **)&uploaded.coal, source.coal,
                              (size_t)source.ncoal * 3 * sizeof(int)) ||
        cu_upload_bank_bytes((void **)&uploaded.xy_off, source.xy_off,
                              ((size_t)v->rnx * v->rny + 1) * sizeof(int)) ||
        cu_upload_bank_bytes((void **)&uploaded.cont, source.cont,
                              source.ncont > 0 ? (size_t)source.ncont * 3 * sizeof(int) : 0) ||
        cudaMalloc(&cells, (size_t)v->n * vol * sizeof(u16)) != cudaSuccess ||
        cudaMalloc(&light, (size_t)v->n * vol) != cudaSuccess ||
        cudaMalloc(&biome, (size_t)v->n * bvol) != cudaSuccess) {
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "%s bank device allocation/upload failed: %s", label, path);
        cudaFree(uploaded.cells); cudaFree(uploaded.light); cudaFree(uploaded.biome);
        cudaFree(uploaded.coal); cudaFree(uploaded.xy_off); cudaFree(uploaded.cont);
        cudaFree(cells); cudaFree(light); cudaFree(biome);
        blaze_snapshot_free(&source);
        return -1;
    }
    blaze_snapshot_free(&source);
    /* Allocate and publish once: existing environments can hold aliases into
     * these immutable sources. Later snapshot loads must not replace them. */
    v->h_dim_bank[bank] = uploaded;
    memcpy(v->dim_bank_loaded_path[bank], path, strlen(path) + 1);
    v->d_dim_cells[bank] = cells;
    v->d_dim_light[bank] = light;
    v->d_dim_biome[bank] = biome;
    if (cu_ck(cudaMemcpy(v->d_dim_bank + bank, &uploaded, sizeof uploaded,
                         cudaMemcpyHostToDevice), "dimension bank upload")) return -1;
    k_bind_dimension_pool<<<(v->n + CU_TPB - 1) / CU_TPB, CU_TPB, 0, v->stream>>>(
        v->d_envs, v->n, bank, cells, light, biome, (long)vol, (long)bvol);
    return cu_ck(cudaStreamSynchronize(v->stream), "dimension pool binding");
}

int blaze_load_snapshots(void *vh, const char *const *paths, int count,
                         char *err, int err_cap) {
    CuVecCu *v = (CuVecCu *)vh;
    int i;
    if (!v || count < 0 || v->nsnaps + count > BLAZE_MAX_SNAPS) return -1;
    cudaSetDevice(v->device);
    for (i = 0; i < count; ++i) {
        CuSnapshot s;
        CuSnapDev *d = &v->h_snaps[v->nsnaps];
        const RlSnapHead *h;
        long svol;
        u16 *d_cells = NULL;
        u8 *d_light = NULL;
        u8 *d_biome = NULL;
        int *d_coal = NULL;
        if (!blaze_snapshot_load(paths[i], &s, err, err_cap, v->no_ore_xy))
            return -1;
        h = &s.head;
        if (h->rny > CU_RNY_MAX) {   /* window y>=128 air invariant */
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap, "region rny %d > %d: %s",
                         h->rny, CU_RNY_MAX, paths[i]);
            blaze_snapshot_free(&s);
            return -1;
        }
        if (v->rvol == 0) {
            if (!cu_alloc_region_pools(v, h->rnx, h->rny, h->rnz,
                                       err, err_cap)) {
                blaze_snapshot_free(&s);
                return -1;
            }
        } else if (h->rnx != v->rnx || h->rny != v->rny || h->rnz != v->rnz) {
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap,
                         "region dims %dx%dx%d != pool %dx%dx%d: %s",
                         h->rnx, h->rny, h->rnz, v->rnx, v->rny, v->rnz,
                         paths[i]);
            blaze_snapshot_free(&s);
            return -1;
        }
        svol = (long)h->rnx * h->rny * h->rnz;
        {
        int *d_xy = NULL;
        int *d_cn = NULL;
        size_t xy_nb = ((size_t)h->rnx * h->rny + 1) * sizeof(int);
        size_t bvol = (size_t)h->rnx * (size_t)h->rnz;
        if (cudaMalloc(&d_cells, (size_t)svol * sizeof(u16)) !=
                cudaSuccess ||
            cudaMemcpy(d_cells, s.cells, (size_t)svol * sizeof(u16),
                       cudaMemcpyHostToDevice) != cudaSuccess ||
            (s.light &&
             (cudaMalloc(&d_light, (size_t)svol) != cudaSuccess ||
              cudaMemcpy(d_light, s.light, (size_t)svol,
                         cudaMemcpyHostToDevice) != cudaSuccess)) ||
            (s.biome && bvol &&
             (cudaMalloc(&d_biome, bvol) != cudaSuccess ||
              cudaMemcpy(d_biome, s.biome, bvol,
                         cudaMemcpyHostToDevice) != cudaSuccess)) ||
            (s.ncoal &&
             (cudaMalloc(&d_coal, (size_t)s.ncoal * 3 * sizeof(int)) !=
                  cudaSuccess ||
              cudaMemcpy(d_coal, s.coal, (size_t)s.ncoal * 3 * sizeof(int),
                         cudaMemcpyHostToDevice) != cudaSuccess)) ||
            (s.xy_off &&
             (cudaMalloc(&d_xy, xy_nb) != cudaSuccess ||
              cudaMemcpy(d_xy, s.xy_off, xy_nb,
                         cudaMemcpyHostToDevice) != cudaSuccess)) ||
            /* Fixed-cap like the CPU loader and blaze_capture: live n_cont
             * can grow past the baked count (placed table/furnace). An
             * exact-ncont malloc makes capture's DeviceToDevice copy
             * cudaErrorInvalidValue. ncont == -1 keeps NULL (overflow). */
            (s.ncont >= 0 &&
             (cudaMalloc(&d_cn, (size_t)BLAZE_SNAP_MAX_CONT * 3 *
                                    sizeof(int)) != cudaSuccess ||
              (s.ncont > 0 &&
               cudaMemcpy(d_cn, s.cont,
                          (size_t)s.ncont * 3 * sizeof(int),
                          cudaMemcpyHostToDevice) != cudaSuccess)))) {
            if (err && err_cap > 0)
                snprintf(err, (size_t)err_cap, "device upload failed: %s",
                         paths[i]);
            cudaFree(d_cells);
            cudaFree(d_light);
            cudaFree(d_biome);
            cudaFree(d_coal);
            cudaFree(d_xy);
            cudaFree(d_cn);
            blaze_snapshot_free(&s);
            return -1;
        }
        memset(d, 0, sizeof *d);
        d->head = s.head;
        memcpy(d->items, s.items, sizeof d->items);
        d->cells = d_cells;
        d->light = d_light;
        d->sky_under = NULL;
        d->sky_under_n = 0;
        if (s.light && s.cells) {
            Blaze e;
            int un, *ub;
            int *d_under = NULL;
            memset(&e, 0, sizeof e);
            e.cells = s.cells;
            e.light = s.light;
            e.rx0 = s.head.rx0; e.ry0 = s.head.ry0; e.rz0 = s.head.rz0;
            e.rnx = s.head.rnx; e.rny = s.head.rny; e.rnz = s.head.rnz;
            size_t nb;
            un = cu_sky_under_collect(&e, NULL, 0);
            nb = (un > 0 ? (size_t)un : 1) * sizeof(int);
            ub = (int *)malloc(nb);
            if (un > 0 && ub) cu_sky_under_collect(&e, ub, un);
            /* Same algorithm on both backends: a failed list is a failed
             * load, not a silent fall back to the 46x46 scan. */
            if (!ub || cudaMalloc(&d_under, nb) != cudaSuccess ||
                cudaMemcpy(d_under, ub, nb, cudaMemcpyHostToDevice) !=
                    cudaSuccess) {
                if (err)
                    snprintf(err, (size_t)err_cap, "sky_under upload failed: %s",
                             paths[i]);
                free(ub);
                cudaFree(d_under);
                cudaFree(d_cells);
                cudaFree(d_light);
                cudaFree(d_biome);
                cudaFree(d_coal);
                cudaFree(d_xy);
                cudaFree(d_cn);
                blaze_snapshot_free(&s);
                return -1;
            }
            free(ub);
            d->sky_under = d_under;
            d->sky_under_n = un;
        }
        d->biome = d_biome;
        d->coal = d_coal;
        d->ncoal = (int)s.ncoal;
        d->xy_off = d_xy;
        d->cont = d_cn;
        d->ncont = s.ncont;
        d->n_mobs = s.n_mobs;
        if (s.n_mobs)
            memcpy(d->mobs, s.mobs, (size_t)s.n_mobs * sizeof d->mobs[0]);
        d->n_orbs = s.n_orbs;
        if (s.n_orbs)
            memcpy(d->orbs, s.orbs, (size_t)s.n_orbs * sizeof d->orbs[0]);
        d->world_rand_seed = s.world_rand_seed;
        d->update_lcg = s.update_lcg;
        d->player_fire = s.player_fire;
        d->player_air = s.player_air;
        d->ww_total_time = s.ww_total_time;
        d->ww_world_time = s.ww_world_time;
        d->ww_rain_time = s.ww_rain_time;
        d->ww_thunder_time = s.ww_thunder_time;
        d->ww_raining = s.ww_raining;
        d->ww_thundering = s.ww_thundering;
        d->ww_rand_seed48 = s.ww_rand_seed48;
        d->rt_mutations = s.rt_mutations;
        d->n_proj = s.n_proj;
        memcpy(d->proj, s.proj, sizeof d->proj);
        d->parity_proj_hits = s.parity_proj_hits;
        d->n_fall = s.n_fall;
        memcpy(d->falls, s.falls, sizeof d->falls);
        d->n_fall_upd = s.n_fall_upd;
        memcpy(d->fall_upd, s.fall_upd, sizeof d->fall_upd);
        d->n_fall_land = s.n_fall_land;
        memcpy(d->fall_land, s.fall_land, sizeof d->fall_land);
        d->fall_mutations = s.fall_mutations;
        d->live_ticks = s.live_ticks;
        d->n_furn = s.n_furn;
        memcpy(d->furn, s.furn, sizeof d->furn);
        d->active_furnace = s.active_furnace;
        d->n_chest = s.n_chest;
        memcpy(d->chest, s.chest, sizeof d->chest);
        d->active_chest = s.active_chest;
        memcpy(d->craft, s.craft, sizeof d->craft);
        memcpy(d->cursor, s.cursor, sizeof d->cursor);
        d->craft_attempts = s.craft_attempts;
        d->craft_successes = s.craft_successes;
        d->container_opens = s.container_opens;
        d->left_click_counter = s.left_click_counter;
        d->eat_ticks = s.eat_ticks;
        d->eat_item = s.eat_item;
        d->bow_ticks = s.bow_ticks;
        d->bow_drawing = s.bow_drawing;
        d->xp_level = s.xp_level;
        d->xp_total = s.xp_total;
        d->xp_cooldown = s.xp_cooldown;
        d->xp_experience = s.xp_experience;
        memcpy(d->armor, s.armor, sizeof d->armor);
        d->fluid_dim = s.fluid_dim;
        memcpy(d->fluid, s.fluid, sizeof d->fluid);
        d->fluid_mutations = s.fluid_mutations;
        d->boat_ride = s.boat_ride;
        d->explosion_pending = s.explosion_pending;
        d->explosion_smoking = s.explosion_smoking;
        d->explosion_flaming = s.explosion_flaming;
        d->explosion_x = s.explosion_x;
        d->explosion_y = s.explosion_y;
        d->explosion_z = s.explosion_z;
        d->explosion_size = s.explosion_size;
        d->xtra = s.xtra;
        d->n_potions = s.n_potions;
        memcpy(d->potions, s.potions, sizeof d->potions);
        }
        v->has_liquid[v->nsnaps] = s.has_liquid;
        v->has_unrepresented[v->nsnaps] = s.head.container != 0;
        blaze_snapshot_free(&s);
        if (cu_upload_dimension_ow(v, v->nsnaps)) return -1;
        v->nsnaps++;
    }
    if (cu_ck(cudaMemcpy(v->d_snaps, v->h_snaps,
                         (size_t)v->nsnaps * sizeof(CuSnapDev),
                         cudaMemcpyHostToDevice), "snap table upload"))
        return -1;
    {
        char sc_nether[1024] = {0}, sc_end[1024] = {0};
        if (count && cu_read_banks_sidecar(paths[0], sc_nether, sizeof sc_nether,
                                           sc_end, sizeof sc_end, err, err_cap)) return -1;
        const char *nether = v->nether_bank_path[0] ? v->nether_bank_path : sc_nether;
        const char *end = v->end_bank_path[0] ? v->end_bank_path : sc_end;
        if (cu_load_named_bank(v, 0, nether, "nether", err, err_cap) ||
            cu_load_named_bank(v, 2, end, "end", err, err_cap)) return -1;
    }
    return v->nsnaps;
}

int blaze_parity_size(void) { return (int)sizeof(BpParityRecord); }

unsigned long long blaze_capabilities(void) {
    return (unsigned long long)BP_IMPLEMENTED_MASK;
}

unsigned long long blaze_snapshot_requirements(void *vh, int snap) {
    CuVecCu *v = (CuVecCu *)vh;
    unsigned long long requirements = 0;
    if (!v || snap < 0 || snap >= v->nsnaps) return ~0ULL;
    if (v->has_liquid[snap])
        requirements |= (unsigned long long)BP_BIT(BP_FLUIDS);
    if (v->has_unrepresented[snap])
        requirements |= (unsigned long long)BP_REQ_UNREPRESENTED_SNAPSHOT;
    return requirements;
}

int blaze_snapshot_has_liquid(void *vh, int snap) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || snap < 0 || snap >= v->nsnaps) return -1;
    return v->has_liquid[snap];
}

/* OPT-IN training-reward mode: gate the +0.03 crosshair-attack bonus on
 * nearest-coal dist <= dist_gate. dist_gate <= 0 restores the default
 * (exact bitwise-gated ppo_coal semantics). */
int blaze_set_reward_gate(void *vh, double dist_gate) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v) return -1;
    v->atk_gate = dist_gate;
    return 0;
}

/* OPT-IN chain-training mode: which inventory item id fires the in-kernel
 * +10/done=1 on count increase vs its at-reset baseline. 263 (default) =
 * exact legacy mine-coal semantics; 50 = torches (full chain); 0 = never.
 * Applies to envs at their NEXT reset. */
int blaze_set_success_item(void *vh, int item) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || item < 0) return -1;
    v->success_item = item;
    return 0;
}

int blaze_set_mobs_enabled(void *vh, int on) {
    CuVecCu *v = (CuVecCu *)vh;
    int i, flag;
    if (!v) return -1;
    v->mobs_enabled = on ? 1 : 0;
    flag = v->mobs_enabled;
    if (v->h_envs)
        for (i = 0; i < v->n; ++i)
            v->h_envs[i].mobs_enabled = flag;
    if (v->d_envs) {
        cudaSetDevice(v->device);
        for (i = 0; i < v->n; ++i) {
            if (cudaMemcpy(&v->d_envs[i].mobs_enabled, &flag, sizeof flag,
                           cudaMemcpyHostToDevice) != cudaSuccess)
                return -1;
        }
    }
    return 0;
}

int blaze_set_natural_spawn(void *vh, int on) {
    CuVecCu *v = (CuVecCu *)vh;
    int i, flag;
    if (!v) return -1;
    v->natural_spawn = on ? 1 : 0;
    flag = v->natural_spawn;
    if (v->h_envs)
        for (i = 0; i < v->n; ++i)
            v->h_envs[i].natural_spawn = flag;
    if (v->d_envs) {
        cudaSetDevice(v->device);
        for (i = 0; i < v->n; ++i) {
            if (cudaMemcpy(&v->d_envs[i].natural_spawn, &flag, sizeof flag,
                           cudaMemcpyHostToDevice) != cudaSuccess)
                return -1;
        }
    }
    return 0;
}

int blaze_set_natural_spawn_passive(void *vh, int on) {
    CuVecCu *v = (CuVecCu *)vh;
    int i, flag;
    if (!v) return -1;
    v->natural_spawn_passive = on ? 1 : 0;
    flag = v->natural_spawn_passive;
    if (v->h_envs)
        for (i = 0; i < v->n; ++i)
            v->h_envs[i].natural_spawn_passive = flag;
    if (v->d_envs) {
        cudaSetDevice(v->device);
        for (i = 0; i < v->n; ++i) {
            if (cudaMemcpy(&v->d_envs[i].natural_spawn_passive, &flag,
                           sizeof flag, cudaMemcpyHostToDevice) != cudaSuccess)
                return -1;
        }
    }
    return 0;
}

int blaze_set_world_time(void *vh, long long world_time) {
    CuVecCu *v = (CuVecCu *)vh;
    int i;
    if (!v) return -1;
    v->world_time_pin = world_time;
    if (v->h_envs)
        for (i = 0; i < v->n; ++i)
            v->h_envs[i].ww.worldTime = world_time;
    if (v->d_envs) {
        cudaSetDevice(v->device);
        for (i = 0; i < v->n; ++i) {
            if (cudaMemcpy(&v->d_envs[i].ww.worldTime, &world_time,
                           sizeof world_time, cudaMemcpyHostToDevice)
                != cudaSuccess)
                return -1;
        }
    }
    return 0;
}

int blaze_set_elytra_enabled(void *vh, int on) {
    CuVecCu *v = (CuVecCu *)vh;
    int i, flag;
    if (!v) return -1;
    v->elytra_kit = on ? 1 : 0;
    flag = v->elytra_kit;
    if (v->h_envs)
        for (i = 0; i < v->n; ++i) {
            v->h_envs[i].elytra_kit = flag;
            if (flag) {
                isr_set_stack(&v->h_envs[i].pl.inv, ISR_ARMOR_CHEST,
                              ic_mk(ISR_ELYTRA_ITEM, 1, 0));
                v->h_envs[i].pl.elytra_equipped = 1;
            }
        }
    if (v->d_envs) {
        cudaSetDevice(v->device);
        for (i = 0; i < v->n; ++i) {
            Blaze he;
            if (cudaMemcpy(&he, v->d_envs + i, sizeof he,
                           cudaMemcpyDeviceToHost) != cudaSuccess)
                return -1;
            he.elytra_kit = flag;
            if (flag) {
                isr_set_stack(&he.pl.inv, ISR_ARMOR_CHEST,
                              ic_mk(ISR_ELYTRA_ITEM, 1, 0));
                he.pl.elytra_equipped = 1;
            }
            if (cudaMemcpy(v->d_envs + i, &he, sizeof he,
                           cudaMemcpyHostToDevice) != cudaSuccess)
                return -1;
        }
    }
    return 0;
}

int blaze_assign(void *vh, const int *snap_idx) {
    CuVecCu *v = (CuVecCu *)vh;
    int i;
    if (!v || !snap_idx) return -1;
    for (i = 0; i < v->n; ++i)
        if (snap_idx[i] < 0 || snap_idx[i] >= v->nsnaps) return -1;
    memcpy(v->h_assign, snap_idx, (size_t)v->n * sizeof(int));
    cudaSetDevice(v->device);
    return cu_ck(cudaMemcpy(v->d_assign, snap_idx,
                            (size_t)v->n * sizeof(int),
                            cudaMemcpyHostToDevice), "assign upload");
}

int blaze_reset(void *vh, const unsigned char *mask) {
    CuVecCu *v = (CuVecCu *)vh;
    int i, nact = 0;
    long long bulk, bulk_blocks;
    if (!v || v->rvol == 0) return -1;   /* pools exist after first load */
    for (i = 0; i < v->n; ++i) {   /* compact the resetting envs host-side */
        if (mask && !mask[i]) continue;
        if (v->h_assign[i] < 0) return -1;
        v->h_active[nact++] = i;
    }
    if (!nact) return 0;
    cudaSetDevice(v->device);
    if (cu_ck(cudaMemcpyAsync(v->d_active, v->h_active,
                              (size_t)nact * sizeof(int),
                              cudaMemcpyHostToDevice, v->stream),
              "active upload"))
        return -1;
    k_reset_scalar<<<(nact + CU_TPB - 1) / CU_TPB, CU_TPB, 0, v->stream>>>(
        v->d_envs, v->d_active, nact, v->d_snaps, v->d_assign,
        v->d_dim_ow, v->success_item, v->mobs_enabled, v->natural_spawn,
        v->natural_spawn_passive, v->world_time_pin, v->elytra_kit);
    /* all snapshots share the pool dims, so the bulk count is uniform - the
     * grass census grid is sized off the dims alone for exactly this reason
     * (cu_grass_grid_init); keep this in step with cu_reset_bulk_count. */
    bulk = v->rvol + (long long)PSV_NCHUNKS * MC_CHUNK_VOL + CU_NPIX +
           (long long)CU_SEC_SPAN(v->rnx) * CU_SEC_SPAN(v->rny) *
               CU_SEC_SPAN(v->rnz);
    bulk_blocks = ((long long)nact * bulk + CU_TPB - 1) / CU_TPB;
    k_reset_bulk<<<(unsigned)bulk_blocks, CU_TPB, 0, v->stream>>>(
        v->d_envs, v->d_active, nact, bulk, v->d_snaps, v->d_assign);
    return cu_ck(cudaStreamSynchronize(v->stream), "k_reset");
}

/* blaze_step + an optional int32[n][CU_STATUS_K] status readout (device
 * pointer; the 9 rl_inv_ids counts, hotbar_sel, held item id, container).
 * status == NULL is the legacy blaze_step. */
int blaze_step_full(void *vh, const double *actions, int repeat,
                    unsigned short *cam, unsigned char *depth,
                    unsigned char *edge, float *scal, float *rew,
                    unsigned char *done, float *pose, int *status) {
    CuVecCu *v = (CuVecCu *)vh;
    int eblocks, pblocks;
    if (!v || !actions || repeat < 1) return -1;
    cudaSetDevice(v->device);
    eblocks = (v->n + CU_TPB - 1) / CU_TPB;
    pblocks = (int)(((size_t)v->n * CU_NPIX + CU_TPB - 1) / CU_TPB);
    if (v->ktime) cudaEventRecord(v->ev[0], v->stream);
    if (v->warp_tick)
        k_tick_warp<<<(unsigned)(((size_t)v->n * 32 + 127) / 128), 128, 0,
                      v->stream>>>(v->d_envs, v->n, v->d_st, actions,
                                   repeat, v->d_aabb, v->d_recipes,
                                   v->nrecipes, v->atk_gate,
                                   NULL,
                                   NULL);
#if BLAZE_SCALAR_TICK
    else
        k_tick<<<(v->n + CU_TICK_TPB - 1) / CU_TICK_TPB, CU_TICK_TPB, 0,
                 v->stream>>>(v->d_envs, v->n, v->d_st, actions, repeat,
                              v->d_aabb, v->d_recipes, v->nrecipes,
                              v->atk_gate,
                              NULL,
                              NULL);
#endif  /* else unreachable: blaze_create rejects warp_tick == 0 */
    if (v->ktime) cudaEventRecord(v->ev[1], v->stream);
    k_obs<<<pblocks, CU_TPB, 0, v->stream>>>(v->d_envs, v->n, v->d_st,
                                             cam, depth, edge);
    if (v->ktime) cudaEventRecord(v->ev[2], v->stream);
    k_final<<<eblocks, CU_TPB, 0, v->stream>>>(v->d_envs, v->n, v->d_st,
                                               scal, rew, done, pose,
                                               v->atk_gate, status);
    if (v->ktime) cudaEventRecord(v->ev[3], v->stream);
    if (cu_ck(cudaStreamSynchronize(v->stream), "blaze_step")) return -1;
    if (v->ktime) {
        float ms;
        cudaEventElapsedTime(&ms, v->ev[0], v->ev[1]); v->ms_tick += ms;
        cudaEventElapsedTime(&ms, v->ev[1], v->ev[2]); v->ms_obs += ms;
        cudaEventElapsedTime(&ms, v->ev[2], v->ev[3]); v->ms_final += ms;
        v->nsteps++;
    }
    return cu_dimension_status(v, v->d_envs, v->n);
}

int blaze_step(void *vh, const double *actions, int repeat,
               unsigned short *cam, unsigned char *depth, unsigned char *edge,
               float *scal, float *rew, unsigned char *done, float *pose) {
    return blaze_step_full(vh, actions, repeat, cam, depth, edge, scal, rew,
                           done, pose, NULL);
}

/* Capture a LIVE env's full state into snapshot slot `slot` (self-generated
 * start-state curriculum). slot overwrites an existing snapshot or appends
 * at nsnaps (dense growth). The slot inherits the env's current region cells
 * (post-edit world), its static ore list and the source snapshot's liquid
 * flag. Rare host call; the per-slot cudaMallocs happen once per slot (all
 * snapshots share the region dims) except the coal list, which grows a new
 * buffer when its length increases. Do not cudaFree the previous coal/xy_off
 * here: blaze_reset_scalar binds env->ore / env->ore_xy by pointer into the
 * slot, and other live envs may still be reading that buffer. Retired
 * pointers are freed at destroy. Same-length overwrites stay in place. */
int blaze_capture(void *vh, int env, int slot) {
    CuVecCu *v = (CuVecCu *)vh;
    Blaze he;
    CuSnapDev *d;
    if (!v || env < 0 || env >= v->n || slot < 0 ||
        slot >= BLAZE_MAX_SNAPS || slot > v->nsnaps || v->rvol == 0)
        return -1;
    if (v->h_assign[env] < 0) return -1;
    cudaSetDevice(v->device);
    /* Same discipline as blaze_emit: step/reset kernels use v->stream. */
    if (cu_ck(cudaStreamSynchronize(v->stream), "capture sync"))
        return -1;
    if (cu_ck(cudaMemcpy(&he, v->d_envs + env, sizeof he,
                         cudaMemcpyDeviceToHost), "capture env readback"))
        return -1;
    d = &v->h_snaps[slot];
    if (slot == v->nsnaps) {
        memset(d, 0, sizeof *d);
        v->nsnaps++;
    }
    (void)blaze_capture_head(&he, &d->head, d->items);
    d->head.version = BLAZE_SNAP_VERSION;
    d->player_fire = he.pl.fire;
    d->player_air = he.pl.air;
    d->ww_total_time = he.ww.totalTime;
    d->ww_world_time = he.ww.worldTime;
    d->ww_rain_time = he.ww.rainTime;
    d->ww_thunder_time = he.ww.thunderTime;
    d->ww_raining = he.ww.raining;
    d->ww_thundering = he.ww.thundering;
    d->ww_rand_seed48 = he.ww.rand.seed;
    d->rt_mutations = he.parity_rt_mutations;
    {
        int k, n = he.pl.n_potions;
        if (n < 0) n = 0;
        if (n > BLAZE_SNAP_POTION_MAX) n = BLAZE_SNAP_POTION_MAX;
        d->n_potions = n;
        memset(d->potions, 0, sizeof d->potions);
        for (k = 0; k < n; ++k) {
            d->potions[k].id = he.pl.potions[k].id;
            d->potions[k].amplifier = he.pl.potions[k].amplifier;
            d->potions[k].duration = he.pl.potions[k].duration;
            d->potions[k].ambient = he.pl.potions[k].ambient;
            d->potions[k].show_particles = he.pl.potions[k].show_particles;
        }
    }
    d->n_mobs = he.n_mobs;
    if (he.n_mobs) {
        unsigned mi;
        memcpy(d->mobs, he.mobs, (size_t)he.n_mobs * sizeof d->mobs[0]);
        for (mi = 0; mi < he.n_mobs; ++mi) {
            d->mobs[mi].repath_timer = he.mob_repath[mi];
            d->mobs[mi].despawn_ticks = he.mob_despawn[mi];
            d->mobs[mi].fire_ticks = he.mob_fire[mi];
        }
    } else
        memset(d->mobs, 0, sizeof d->mobs);
    if (!d->cells) {
        u16 *cells = NULL;
        if (cu_ck(cudaMalloc(&cells, (size_t)v->rvol * sizeof(u16)),
                  "capture cells alloc"))
            return -1;
        d->cells = cells;
    }
    if (cu_ck(cudaMemcpy((void *)d->cells, v->d_cells + (size_t)env * v->rvol,
                         (size_t)v->rvol * sizeof(u16),
                         cudaMemcpyDeviceToDevice), "capture cells copy"))
        return -1;
    {   /* the raisable list is a function of cells+light: rebuild it from
         * the captured cells, or drop it when this slot carries no light.
         * The old list is retired, never freed: live envs may alias it. */
        int *d_under = NULL, un = 0;
        if (d->light) {
            Blaze t;
            u16 *hc = (u16 *)malloc((size_t)v->rvol * sizeof(u16));
            u8 *hl = (u8 *)malloc((size_t)v->rvol);
            int *ub = NULL;
            size_t nb;
            if (!hc || !hl) { free(hc); free(hl); return -1; }
            if (cu_ck(cudaMemcpy(hc, d->cells, (size_t)v->rvol * sizeof(u16),
                                 cudaMemcpyDeviceToHost), "capture cells D2H") ||
                cu_ck(cudaMemcpy(hl, d->light, (size_t)v->rvol,
                                 cudaMemcpyDeviceToHost), "capture light D2H")) {
                free(hc); free(hl); return -1;
            }
            memset(&t, 0, sizeof t);
            t.cells = hc; t.light = hl;
            t.rx0 = d->head.rx0; t.ry0 = d->head.ry0; t.rz0 = d->head.rz0;
            t.rnx = d->head.rnx; t.rny = d->head.rny; t.rnz = d->head.rnz;
            un = cu_sky_under_collect(&t, NULL, 0);
            nb = (un > 0 ? (size_t)un : 1) * sizeof(int);
            ub = (int *)malloc(nb);
            if (un > 0 && ub) cu_sky_under_collect(&t, ub, un);
            free(hc); free(hl);
            if (!ub || cu_ck(cudaMalloc(&d_under, nb), "capture sky_under alloc") ||
                cu_ck(cudaMemcpy(d_under, ub, nb, cudaMemcpyHostToDevice),
                      "capture sky_under upload")) {
                free(ub); cudaFree(d_under); return -1;
            }
            free(ub);
        }
        cu_retire(v, d->sky_under);
        d->sky_under = d_under;
        d->sky_under_n = un;
    }
    {
        size_t bvol = (size_t)v->rnx * (size_t)v->rnz;
        if (bvol && he.biome) {
            if (!d->biome) {
                u8 *biome = NULL;
                if (cu_ck(cudaMalloc(&biome, bvol), "capture biome alloc"))
                    return -1;
                d->biome = biome;
            }
            if (cu_ck(cudaMemcpy((void *)d->biome, he.biome, bvol,
                                 cudaMemcpyDeviceToDevice),
                      "capture biome copy"))
                return -1;
        }
    }
    if (d->ncoal != he.nore) {
        /* Grow without freeing: live envs may still alias d->coal. Shrink
         * keeps the existing allocation and just updates ncoal. */
        if (he.nore > d->ncoal || !d->coal) {
            int *coal = NULL;
            if (he.nore &&
                cu_ck(cudaMalloc(&coal, (size_t)he.nore * 3 * sizeof(int)),
                      "capture coal alloc"))
                return -1;
            cu_retire(v, d->coal);
            d->coal = coal;
        }
        d->ncoal = he.nore;
    }
    if (he.nore &&
        cu_ck(cudaMemcpy((void *)d->coal, he.ore,
                         (size_t)he.nore * 3 * sizeof(int),
                         cudaMemcpyDeviceToDevice), "capture coal copy"))
        return -1;
    {   /* the captured ore list IS the assign-source snapshot's (he.ore was
         * bound at reset and never mutates), so its spatial index carries
         * over verbatim; all snapshots share the region dims. */
        const int *src_xy = v->h_snaps[v->h_assign[env]].xy_off;
        size_t xy_nb = ((size_t)v->rnx * v->rny + 1) * sizeof(int);
        if (src_xy) {
            if (!d->xy_off) {
                int *xy = NULL;
                if (cu_ck(cudaMalloc(&xy, xy_nb), "capture xy_off alloc"))
                    return -1;
                d->xy_off = xy;
            }
            if (cu_ck(cudaMemcpy((void *)d->xy_off, src_xy, xy_nb,
                                 cudaMemcpyDeviceToDevice),
                      "capture xy_off copy"))
                return -1;
        } else {
            cu_retire(v, d->xy_off);
            d->xy_off = NULL;
        }
    }
    {   /* container list: the env's LIVE device list is exactly the captured
         * region's (maintained on every edit). Fixed-cap slot buffer,
         * allocated once at load/append; overflow (-1) rides along. */
        if (he.n_cont < -1 || he.n_cont > BLAZE_SNAP_MAX_CONT)
            return -1;
        if (he.n_cont >= 0 && !d->cont) {
            int *cn = NULL;
            if (cu_ck(cudaMalloc(&cn, (size_t)BLAZE_SNAP_MAX_CONT * 3 *
                                          sizeof(int)),
                      "capture cont alloc"))
                return -1;
            d->cont = cn;
        }
        d->ncont = he.n_cont;
        if (he.n_cont > 0 &&
            (!he.cont || !d->cont ||
             cu_ck(cudaMemcpy((void *)d->cont, he.cont,
                              (size_t)he.n_cont * 3 * sizeof(int),
                              cudaMemcpyDeviceToDevice), "capture cont copy")))
            return -1;
    }
    v->has_liquid[slot] = v->has_liquid[v->h_assign[env]];
    v->has_unrepresented[slot] = d->head.container != 0;
    if (cu_upload_dimension_ow(v, slot)) return -1;
    return cu_ck(cudaMemcpy(v->d_snaps + slot, d, sizeof *d,
                            cudaMemcpyHostToDevice), "capture snap upload");
}

int blaze_dump_snapshot(void *vh, int env, const char *path,
                        char *err, int err_cap) {
    CuVecCu *v = (CuVecCu *)vh;
    Blaze he;
    CuSnapshot s;
    unsigned k, si, ei;
    int n;
    size_t bvol;
    if (!v || env < 0 || env >= v->n || !path)
        return -1;
    cudaSetDevice(v->device);
    if (cu_ck(cudaStreamSynchronize(v->stream), "dump sync"))
        return -1;
    if (cu_ck(cudaMemcpy(&he, v->d_envs + env, sizeof he,
                         cudaMemcpyDeviceToHost), "dump env readback"))
        return -1;
    memset(&s, 0, sizeof s);
    (void)blaze_capture_head(&he, &s.head, s.items);
    s.head.version = BLAZE_SNAP_VERSION;
    s.player_fire = he.pl.fire;
    s.player_air = he.pl.air;
    s.ww_total_time = he.ww.totalTime;
    s.ww_world_time = he.ww.worldTime;
    s.ww_rain_time = he.ww.rainTime;
    s.ww_thunder_time = he.ww.thunderTime;
    s.ww_raining = he.ww.raining;
    s.ww_thundering = he.ww.thundering;
    s.ww_rand_seed48 = he.ww.rand.seed & MC_JR_MASK;
    s.rt_mutations = he.parity_rt_mutations;
    s.world_rand_seed = he.world_rand.seed & MC_JR_MASK;
    s.update_lcg = he.update_lcg;
    s.n_mobs = he.n_mobs;
    if (he.n_mobs)
        memcpy(s.mobs, he.mobs, (size_t)he.n_mobs * sizeof s.mobs[0]);
    s.ncoal = (unsigned)he.nore;
    s.cells = (unsigned short *)malloc((size_t)v->rvol * sizeof *s.cells);
    s.light = (unsigned char *)malloc((size_t)v->rvol);
    bvol = (size_t)v->rnx * (size_t)v->rnz;
    s.biome = bvol ? (unsigned char *)malloc(bvol) : NULL;
    if (!s.cells || !s.light || (bvol && !s.biome)) {
        free(s.cells); free(s.light); free(s.biome);
        if (err && err_cap > 0)
            snprintf(err, (size_t)err_cap, "dump alloc failed");
        return -1;
    }
    if (cu_ck(cudaMemcpy(s.cells, v->d_cells + (size_t)env * v->rvol,
                         (size_t)v->rvol * sizeof *s.cells,
                         cudaMemcpyDeviceToHost), "dump cells") ||
        cu_ck(cudaMemcpy(s.light, v->d_light + (size_t)env * v->rvol,
                         (size_t)v->rvol, cudaMemcpyDeviceToHost),
              "dump light") ||
        (bvol && he.biome &&
         cu_ck(cudaMemcpy(s.biome, he.biome, bvol, cudaMemcpyDeviceToHost),
               "dump biome"))) {
        free(s.cells); free(s.light); free(s.biome);
        return -1;
    }
    if (he.nore) {
        s.coal = (int *)malloc((size_t)he.nore * 3 * sizeof *s.coal);
        if (!s.coal ||
            cu_ck(cudaMemcpy(s.coal, he.ore,
                             (size_t)he.nore * 3 * sizeof *s.coal,
                             cudaMemcpyDeviceToHost), "dump coal")) {
            free(s.cells); free(s.light); free(s.biome); free(s.coal);
            return -1;
        }
    }
    s.n_proj = 0;
    for (k = 0; k < CU_MAX_PROJECTILES && s.n_proj < BLAZE_SNAP_MAX_PROJ; ++k) {
        if (!he.projectiles[k].active) continue;
        s.proj[s.n_proj].active = 1;
        s.proj[s.n_proj].type = he.projectiles[k].type;
        s.proj[s.n_proj].age = he.projectiles[k].age;
        s.proj[s.n_proj].x = he.projectiles[k].x;
        s.proj[s.n_proj].y = he.projectiles[k].y;
        s.proj[s.n_proj].z = he.projectiles[k].z;
        s.proj[s.n_proj].vx = he.projectiles[k].vx;
        s.proj[s.n_proj].vy = he.projectiles[k].vy;
        s.proj[s.n_proj].vz = he.projectiles[k].vz;
        s.proj[s.n_proj].in_ground = he.proj_in_ground[k];
        s.proj[s.n_proj].shake = he.proj_shake[k];
        s.proj[s.n_proj].pickup = he.proj_pickup[k];
        s.proj[s.n_proj].ground_ticks = he.proj_ground_ticks[k];
        s.n_proj++;
    }
    s.parity_proj_hits = he.parity_proj_hits;
    s.n_fall = 0;
    for (k = 0; k < CU_MAX_ITEMS && s.n_fall < BLAZE_SNAP_MAX_FALL; ++k) {
        if (!he.falls[k].active) continue;
        s.falls[s.n_fall].active = 1;
        s.falls[s.n_fall].type = he.falls[k].type;
        s.falls[s.n_fall].x = he.falls[k].x;
        s.falls[s.n_fall].y = he.falls[k].y;
        s.falls[s.n_fall].z = he.falls[k].z;
        s.falls[s.n_fall].mx = he.falls[k].mx;
        s.falls[s.n_fall].my = he.falls[k].my;
        s.falls[s.n_fall].mz = he.falls[k].mz;
        s.falls[s.n_fall].on_ground = he.falls[k].on_ground;
        s.falls[s.n_fall].age = he.falls[k].age;
        s.falls[s.n_fall].item = he.falls[k].item;
        s.falls[s.n_fall].count = he.falls[k].count;
        s.falls[s.n_fall].meta = he.falls[k].meta;
        s.falls[s.n_fall].pickup_delay = he.falls[k].pickup_delay;
        s.falls[s.n_fall].lifespan = he.falls[k].lifespan;
        s.n_fall++;
    }
    s.n_fall_upd = 0;
    for (k = 0; k < CU_FALL_UPDATES && s.n_fall_upd < BLAZE_SNAP_MAX_FALL_UPD;
         ++k) {
        if (!he.fall_updates[k].active) continue;
        s.fall_upd[s.n_fall_upd].active = 1;
        s.fall_upd[s.n_fall_upd].x = he.fall_updates[k].x;
        s.fall_upd[s.n_fall_upd].y = he.fall_updates[k].y;
        s.fall_upd[s.n_fall_upd].z = he.fall_updates[k].z;
        s.fall_upd[s.n_fall_upd].block_id = he.fall_updates[k].block_id;
        s.fall_upd[s.n_fall_upd].due_tick = he.fall_updates[k].due_tick;
        s.n_fall_upd++;
    }
    s.n_fall_land = 0;
    for (k = 0; k < CU_MAX_ITEMS && s.n_fall_land < BLAZE_SNAP_MAX_FALL; ++k) {
        if (!he.fall_landings[k].active) continue;
        s.fall_land[s.n_fall_land].active = 1;
        s.fall_land[s.n_fall_land].x = he.fall_landings[k].x;
        s.fall_land[s.n_fall_land].y = he.fall_landings[k].y;
        s.fall_land[s.n_fall_land].z = he.fall_landings[k].z;
        s.fall_land[s.n_fall_land].block_id = he.fall_landings[k].block_id;
        s.fall_land[s.n_fall_land].block_meta = he.fall_landings[k].block_meta;
        s.fall_land[s.n_fall_land].due_tick = he.fall_landings[k].due_tick;
        s.n_fall_land++;
    }
    s.fall_mutations = he.parity_fall_mutations;
    s.live_ticks = he.live_ticks;
    s.n_furn = 0;
    s.active_furnace = he.active_furnace;
    for (k = 0; k < CU_MAX_FURNACES && s.n_furn < BLAZE_SNAP_MAX_FURN; ++k) {
        if (!he.furnaces[k].active) continue;
        s.furn[s.n_furn].active = 1;
        s.furn[s.n_furn].wx = he.furnaces[k].wx;
        s.furn[s.n_furn].wy = he.furnaces[k].wy;
        s.furn[s.n_furn].wz = he.furnaces[k].wz;
        s.furn[s.n_furn].in_item = he.furnaces[k].input.item;
        s.furn[s.n_furn].in_count = he.furnaces[k].input.count;
        s.furn[s.n_furn].in_meta = he.furnaces[k].input.meta;
        s.furn[s.n_furn].fuel_item = he.furnaces[k].fuel.item;
        s.furn[s.n_furn].fuel_count = he.furnaces[k].fuel.count;
        s.furn[s.n_furn].fuel_meta = he.furnaces[k].fuel.meta;
        s.furn[s.n_furn].out_item = he.furnaces[k].output.item;
        s.furn[s.n_furn].out_count = he.furnaces[k].output.count;
        s.furn[s.n_furn].out_meta = he.furnaces[k].output.meta;
        s.furn[s.n_furn].burn_time = he.furnaces[k].burn_time;
        s.furn[s.n_furn].current_burn_time = he.furnaces[k].current_burn_time;
        s.furn[s.n_furn].cook_time = he.furnaces[k].cook_time;
        s.furn[s.n_furn].total_cook = he.furnaces[k].total_cook;
        s.n_furn++;
    }
    s.n_chest = 0;
    s.active_chest = he.active_chest;
    for (k = 0; k < CU_MAX_CHESTS && s.n_chest < BLAZE_SNAP_MAX_CHEST; ++k) {
        if (!he.chests[k].active) continue;
        s.chest[s.n_chest].active = 1;
        s.chest[s.n_chest].wx = he.chests[k].wx;
        s.chest[s.n_chest].wy = he.chests[k].wy;
        s.chest[s.n_chest].wz = he.chests[k].wz;
        s.chest[s.n_chest].num_using = he.chests[k].te.num_players_using;
        for (si = 0; si < BLAZE_SNAP_CHEST_SLOTS; ++si) {
            const TecStack *ts = &he.chests[k].te.slots[si];
            s.chest[s.n_chest].slot[si][0] = ts->item;
            s.chest[s.n_chest].slot[si][1] = ts->count;
            s.chest[s.n_chest].slot[si][2] = ts->meta;
            n = ts->n_enchants;
            if (n < 0) n = 0;
            if (n > 8) n = 8;
            s.chest[s.n_chest].slot_ench[si].n = n;
            for (ei = 0; ei < (unsigned)n; ++ei) {
                s.chest[s.n_chest].slot_ench[si].id[ei] = ts->enchants[ei].id;
                s.chest[s.n_chest].slot_ench[si].level[ei] =
                    ts->enchants[ei].level;
            }
        }
        s.n_chest++;
    }
    for (k = 0; k < 9; ++k) {
        s.craft[k][0] = he.craft_grid[k].item;
        s.craft[k][1] = he.craft_grid[k].count;
        s.craft[k][2] = he.craft_grid[k].meta;
    }
    s.cursor[0] = he.cursor.item;
    s.cursor[1] = he.cursor.count;
    s.cursor[2] = he.cursor.meta;
    s.craft_attempts = he.parity_craft_attempts;
    s.craft_successes = he.parity_craft_successes;
    s.container_opens = he.parity_container_opens;
    s.left_click_counter = he.left_click_counter;
    s.eat_ticks = he.eat_ticks;
    s.eat_item = he.eat_item;
    s.bow_ticks = he.bow_ticks;
    s.bow_drawing = he.bow_drawing;
    s.xp_level = he.pl.experienceLevel;
    s.xp_total = he.pl.experienceTotal;
    s.xp_cooldown = he.pl.xpCooldown;
    s.xp_experience = he.pl.experience;
    for (k = 0; k < 4; ++k) {
        ICStack st = isr_get_stack(&he.pl.inv, ISR_ARMOR0 + (int)k);
        s.armor[k][0] = st.item;
        s.armor[k][1] = st.count;
        s.armor[k][2] = st.meta;
    }
    s.fluid_dim = he.fluid_dim;
    s.fluid_mutations = he.parity_fluid_mutations;
    for (k = 0; k < CU_FLUID_REGIONS && k < BLAZE_SNAP_FLUID_REGS; ++k) {
        s.fluid[k].active = he.fluid_reg[k].active;
        s.fluid[k].x0 = he.fluid_reg[k].x0;
        s.fluid[k].y0 = he.fluid_reg[k].y0;
        s.fluid[k].z0 = he.fluid_reg[k].z0;
        s.fluid[k].x1 = he.fluid_reg[k].x1;
        s.fluid[k].y1 = he.fluid_reg[k].y1;
        s.fluid[k].z1 = he.fluid_reg[k].z1;
        s.fluid[k].has_water = he.fluid_reg[k].has_water;
        s.fluid[k].quiet_steps = he.fluid_reg[k].quiet_steps;
    }
    s.boat_ride = he.boat_ride;
    s.explosion_pending = he.explosion_pending;
    s.explosion_smoking = he.explosion_smoking;
    s.explosion_flaming = he.explosion_flaming;
    s.explosion_x = he.explosion_x;
    s.explosion_y = he.explosion_y;
    s.explosion_z = he.explosion_z;
    s.explosion_size = he.explosion_size;
    s.xtra.xp_pickups = he.parity_xp_pickups;
    s.xtra.next_orb_id = he.next_orb_id;
    s.xtra.spawn_world_seed48 = he.spawn_world_seed48;
    s.xtra.spawn_math_seed48 = he.spawn_math_seed48;
    s.xtra.spawn_shuffle_seed48 = he.spawn_shuffle_seed48;
    s.xtra.parity_ex_blasts = he.parity_ex_blasts;
    s.xtra.parity_ex_destroyed = he.parity_ex_destroyed;
    s.xtra.parity_ex_drop_n = he.parity_ex_drop_n;
    s.xtra.parity_ex_drop_ids = he.parity_ex_drop_ids;
    s.xtra.parity_ex_damage = he.parity_ex_damage;
    s.xtra.parity_ex_kb_x = he.parity_ex_kb_x;
    s.xtra.parity_ex_kb_y = he.parity_ex_kb_y;
    s.xtra.parity_ex_kb_z = he.parity_ex_kb_z;
    s.xtra.parity_ex_rays = he.parity_ex_rays;
    s.xtra.parity_ex_last_x = he.parity_ex_last_x;
    s.xtra.parity_ex_last_y = he.parity_ex_last_y;
    s.xtra.parity_ex_last_z = he.parity_ex_last_z;
    s.xtra.parity_ex_last_size = he.parity_ex_last_size;
    s.xtra.player_dead = he.dead;
    s.xtra.player_hurt_resistant = he.player_hurt_resistant;
    s.xtra.player_attack_cooldown = he.player_attack_cooldown;
    s.xtra.player_last_damage = he.player_last_damage;
    s.xtra.last_craft[0] = he.parity_last_craft.item;
    s.xtra.last_craft[1] = he.parity_last_craft.count;
    s.xtra.last_craft[2] = he.parity_last_craft.meta;
    {
        int n = he.parity_last_craft.n_enchants, ei;
        if (n < 0) n = 0;
        if (n > 8) n = 8;
        s.xtra.last_craft_ench.n = n;
        for (ei = 0; ei < n; ++ei) {
            s.xtra.last_craft_ench.id[ei] = he.parity_last_craft.enchants[ei].id;
            s.xtra.last_craft_ench.level[ei] =
                he.parity_last_craft.enchants[ei].level;
        }
    }
    s.xtra.elytra_equipped = he.pl.elytra_equipped;
    s.xtra.elytra_flying = he.pl.elytra_flying;
    s.xtra.elytra_pending = he.pl.elytra_flying_pending;
    s.xtra.elytra_pose = he.pl.elytra_pose;
    s.xtra.ticks_elytra_flying = he.pl.ticks_elytra_flying;
    s.xtra.elytra_wall_damage = he.pl.elytra_wall_damage;
    for (si = 0; si < he.n_mobs && si < BLAZE_SNAP_MAX_MOBS; ++si) {
        s.xtra.boat_delta_rot[si] = he.boat_delta_rot[si];
        s.xtra.boat_glide[si] = he.boat_glide[si];
        s.xtra.sidecar_repath[si] = he.mob_repath[si];
        s.xtra.sidecar_despawn[si] = he.mob_despawn[si];
        s.xtra.sidecar_fire[si] = he.mob_fire[si];
    }
    for (si = 0; si < 37; ++si) {
        ICStack st = isr_get_stack(&he.pl.inv,
                                   si < 36 ? (int)si : ISR_OFFHAND_SLOT);
        n = st.n_enchants;
        if (n < 0) n = 0;
        if (n > 8) n = 8;
        s.xtra.inv_ench[si].n = n;
        for (ei = 0; ei < (unsigned)n; ++ei) {
            s.xtra.inv_ench[si].id[ei] = st.enchants[ei].id;
            s.xtra.inv_ench[si].level[ei] = st.enchants[ei].level;
        }
    }
    {
        int k, n = he.pl.n_potions;
        if (n < 0) n = 0;
        if (n > BLAZE_SNAP_POTION_MAX) n = BLAZE_SNAP_POTION_MAX;
        s.n_potions = n;
        memset(s.potions, 0, sizeof s.potions);
        for (k = 0; k < n; ++k) {
            s.potions[k].id = he.pl.potions[k].id;
            s.potions[k].amplifier = he.pl.potions[k].amplifier;
            s.potions[k].duration = he.pl.potions[k].duration;
            s.potions[k].ambient = he.pl.potions[k].ambient;
            s.potions[k].show_particles = he.pl.potions[k].show_particles;
        }
    }
    s.n_orbs = 0;
    for (k = 0; k < XL_MAX && s.n_orbs < BLAZE_SNAP_MAX_ORBS; ++k) {
        const McOrb *o = &he.orbs[k];
        RlSnapOrb *d;
        if (o->dead || o->xpValue <= 0) continue;
        d = &s.orbs[s.n_orbs++];
        memset(d, 0, sizeof *d);
        d->x = o->posX; d->y = o->posY; d->z = o->posZ;
        d->mx = o->motionX; d->my = o->motionY; d->mz = o->motionZ;
        d->on_ground = o->onGround;
        d->xpOrbAge = o->xpOrbAge;
        d->delayBeforeCanPickup = o->delayBeforeCanPickup;
        d->xpValue = o->xpValue;
        d->eid = o->eid;
        d->xpColor = o->xpColor;
        d->xpTargetColor = o->xpTargetColor;
        d->has_closest = o->has_closest;
        d->dead = o->dead;
    }
    {
        int rc = blaze_snapshot_write(path, &s, err, err_cap) ? 0 : -1;
        free(s.cells); free(s.light); free(s.biome); free(s.coal);
        return rc;
    }
}

/* Production tick for focused M2: same 17-double ABI as blaze_tick_raw, but
 * launches k_tick_warp (create opts.warp_tick=1, default; blaze.conf /
 * ppo.conf / blaze_abi.h) or k_tick (warp_tick=0). One env per fixture
 * lane (warp: 32 threads help that env). inv_click rides a side buffer so
 * blaze_step's 13-wide trainer ABI stays unchanged. */
static int cu_ensure_tick_act(CuVecCu *v) {
    size_t nact = (size_t)v->n * BLAZE_ACT_HEADS * sizeof(double);
    size_t ninv = (size_t)v->n * 4 * sizeof(double);
    if (v->d_tick_act) return 0;
    v->h_tick_act = (double *)malloc(nact);
    v->h_tick_inv = (double *)malloc(ninv);
    if (!v->h_tick_act || !v->h_tick_inv ||
        cudaMalloc(&v->d_tick_act, nact) != cudaSuccess ||
        cudaMalloc(&v->d_tick_inv, ninv) != cudaSuccess) {
        fprintf(stderr, "blaze_cuda: blaze_tick action staging alloc failed\n");
        return -1;
    }
    return 0;
}

static void cu_pack_tick_act(CuVecCu *v, int dst, const double a[17]) {
    int k;
    double *act = v->h_tick_act + (size_t)dst * BLAZE_ACT_HEADS;
    double *inv = v->h_tick_inv + (size_t)dst * 4;
    for (k = 0; k < BLAZE_ACT_HEADS; ++k) act[k] = a[k];
    for (k = 0; k < 4; ++k) inv[k] = a[13 + k];
}

static int cu_launch_prod_tick(CuVecCu *v, Blaze *envs, int n,
                               const double *act, McAABB *aabb,
                               const double *inv) {
    int repeat = 1;
    if (v->warp_tick)
        k_tick_warp<<<(unsigned)(((size_t)n * 32 + 127) / 128), 128, 0,
                      v->stream>>>(envs, n, v->d_st, act, repeat, aabb,
                                   v->d_recipes, v->nrecipes, v->atk_gate,
                                   NULL,
                                   inv);
#if BLAZE_SCALAR_TICK
    else
        k_tick<<<(n + CU_TICK_TPB - 1) / CU_TICK_TPB, CU_TICK_TPB, 0,
                 v->stream>>>(envs, n, v->d_st, act, repeat, aabb,
                              v->d_recipes, v->nrecipes, v->atk_gate,
                              NULL, inv);
#endif  /* else unreachable: blaze_create rejects warp_tick == 0 */
    if (cu_ck(cudaStreamSynchronize(v->stream), "blaze_tick")) return -1;
    return cu_dimension_status(v, envs, n);
}

int blaze_tick(void *vh, int env, const double a[17], int want_cam,
               void *out) {
    CuVecCu *v = (CuVecCu *)vh;
    int i, npack;
    size_t nact, ninv;
    if (!v || env < -1 || env >= v->n || !a) return -1;
    cudaSetDevice(v->device);
    if (cu_ensure_tick_act(v)) return -1;
    if (env == -1) {
        for (i = 0; i < v->n; ++i) cu_pack_tick_act(v, i, a);
        npack = v->n;
    } else {
        cu_pack_tick_act(v, 0, a);
        npack = 1;
    }
    nact = (size_t)npack * BLAZE_ACT_HEADS * sizeof(double);
    ninv = (size_t)npack * 4 * sizeof(double);
    if (cu_ck(cudaMemcpyAsync(v->d_tick_act, v->h_tick_act, nact,
                              cudaMemcpyHostToDevice, v->stream),
              "tick act upload") ||
        cu_ck(cudaMemcpyAsync(v->d_tick_inv, v->h_tick_inv, ninv,
                              cudaMemcpyHostToDevice, v->stream),
              "tick inv upload"))
        return -1;
    if (env == -1) {
        if (cu_launch_prod_tick(v, v->d_envs, v->n, v->d_tick_act, v->d_aabb,
                                v->d_tick_inv))
            return -1;
        return 0;
    }
    if (cu_launch_prod_tick(v, v->d_envs + env, 1, v->d_tick_act,
                            v->d_aabb + (size_t)env * PSV_MAX_BLOCKS,
                            v->d_tick_inv))
        return -1;
    if (!out) return 0;
    return blaze_emit(vh, env, want_cam, out);
}

/* op-trace readout: counters per env (buffer sizing) + the n * CU_OP_N
 * cumulative device counters copied to host `out` (row-major, env-major).
 * Returns -1 when tracing is off (op_trace was 0 at create). */
int blaze_op_count(void) { return CU_OP_N; }

int blaze_op_trace(void *vh, unsigned long long *out) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || !out || !v->d_ops) return -1;
    cudaSetDevice(v->device);
    return cu_ck(cudaMemcpy(out, v->d_ops,
                            (size_t)v->n * CU_OP_N *
                                sizeof(unsigned long long),
                            cudaMemcpyDeviceToHost), "op-trace readback");
}

int blaze_phase_k(void) { return CU_PHASE_K; }

int blaze_copy_phase(void *vh, unsigned long long *out) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || !out || !v->d_phase) return -1;
    cudaSetDevice(v->device);
    return cu_ck(cudaMemcpy(out, v->d_phase,
                            (size_t)v->n * (size_t)CU_PHASE_K *
                                sizeof(unsigned long long),
                            cudaMemcpyDeviceToHost), "phase readback");
}

int blaze_phase_clear(void *vh) {
    CuVecCu *v = (CuVecCu *)vh;
    if (!v || !v->d_phase) return -1;
    cudaSetDevice(v->device);
    return cu_ck(cudaMemset(v->d_phase, 0,
                            (size_t)v->n * (size_t)CU_PHASE_K *
                                sizeof(unsigned long long)), "phase clear");
}

int blaze_debug_state(void *vh, int env, double *out, int cap) {
    CuVecCu *v = (CuVecCu *)vh;
    Blaze e;
    if (!v || env < 0 || env >= v->n || !out || cap < 21) return -1;
    cudaSetDevice(v->device);
    if (cu_ck(cudaMemcpy(&e, v->d_envs + env, sizeof e,
                         cudaMemcpyDeviceToHost), "env readback"))
        return -1;
    return blaze_debug_fill(&e, out);
}
