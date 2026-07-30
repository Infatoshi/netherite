/* world_step: UNIFIED world-step tick - one composed tick that runs the ENVIRONMENT CAs
 * (halo fluid + light + block tickers) AND the PLAYER update (physics/collision + break/place
 * + inventory + vitals) over ONE shared double-buffered multi-chunk world. This is the backbone
 * the eventual game loop drives.
 *
 * It COMPOSES two already-verified drivers, editing neither:
 *   - tick_world_halo.h : TwmWorld (double-buffered 3x3 Chunk region) + halo-aware CAs
 *       bth_tick_grid (block updates + random ticks), ff_ca_step_halo (cross-chunk fluid),
 *       lp_propagate_halo (region-wide light fixpoint). Provides gen (twh_gen) + fixtures.
 *   - player_survival.h : PsvPlayer + the verified survival tick pieces psv_action_for_tick,
 *       psv_physics_tick, psv_raycast, psv_vitals_tick, and the block break/place + inventory
 *       primitives (psv_set_block, isr_*). Player runs over the SAME TwmWorld chunks: both
 *       drivers use a 3x3 origin-centered region with the identical chunk-index formula
 *       (cx=(i%3)-1, cz=(i/3)-1), so psv_get_block/psv_set_block address TwmWorld directly.
 *
 * PER-TICK COMPOSED ORDER (matches SPEC's intended full order, minus entities which are a
 * separate task): block updates / random ticks / fluid / light / [ENTITY UPDATE hook] / player.
 *   1. copy now -> next (all chunks)   -- establish the tick N+1 write buffer
 *   2. bth_tick_grid   (env: block updates + random ticks)  read now, write next
 *   3. ff_ca_step_halo (env: fluid CA, crosses chunk borders) refine next
 *   4. lp_propagate_halo (env: light fixpoint, crosses borders) refine next
 *   5. ws_entity_phase (NO-OP hook; entities slot here later)  read now, write next
 *   6. player update   (physics reads now; break/place write next; inventory/vitals)
 *   7. tick++, swap
 *
 * DOUBLE-BUFFERING (SPEC rule 3), preserved across BOTH halves: every phase READS tick N from
 * `now` and WRITES tick N+1 into `next`; nothing reads its own tick's writes across the now/next
 * boundary. The env CAs read `now` blocks (bth) then progressively refine `next` (fluid then
 * light chain on next - the established, verified tick_world_halo behavior). The player reads
 * `now` for physics/collision/raycast and writes its block edits to `next`. Because both halves
 * read the SAME immutable `now`, their ordering (env-then-player) only decides who wins when they
 * write the SAME `next` cell - a deterministic, documented choice, so CPU==CUDA holds by
 * construction. Runtime RNG is the stateless world-coord hash (rule 1); no floats in the CAs and
 * left-to-right float order in physics (rule 4). world_step OWNS the single copy-forward and the
 * single swap - it does NOT call twh_tick/psv_tick wholesale (each of those does its own
 * copy+swap, which would double-buffer twice); it re-composes their verified sub-steps in the
 * unified order.
 *
 * NOTE: world_step is its OWN oracle. Its per-tick hashes differ from standalone tick_world_halo
 * / player_survival (the player now edits the fixtured world, and the world now carries a player)
 * - that is expected. The contract is CPU==CUDA bitwise for world_step, and that BOTH halves'
 * effects still occur: env fluid/light cross the chunk boundary (tail evidence) AND the player's
 * block edits + inventory + vitals evolve (per-tick player fields).
 *
 * READ-ONLY deps: tick_world_halo.h, player_survival.h (and everything they pull). */
#ifndef MC_WORLD_STEP_H
#define MC_WORLD_STEP_H

#include "tick_world_halo.h"    /* TwmWorld, TwhScratch, twh_gen, halo CAs, twm_now/next/swap */
#include "player_survival.h"    /* PsvPlayer, psv_* verified survival sub-steps + inventory */
#include "entities_world.h"     /* EwState/EwStore/EwScratch + VERIFIED entity helpers (READ-ONLY reuse):
                                 * ew_overlay_arena, ew_mob_move, ew_repath, ew_darken_ms, ms_run,
                                 * ew_store_spawn, mc_combat_*, ew_dump_tick. entities_world.* itself
                                 * is UNTOUCHED so it stays independently verified. */

#ifndef WS_NTICKS
#define WS_NTICKS 64
#endif

/* per-tick emitted u64s: [0] world light FNV hash, [1..19] PSV_FIELDS player+block-hash state, then
 * WS_ENT_PER_TICK entity-store lines (ew_dump_tick: per slot meta/pos/vel/yaw|health/path/chunk). */
#define WS_FIELDS       (1 + PSV_FIELDS)
#define WS_ENT_PER_TICK (EW_MAX_ENTITIES * EW_LINES_PER_SLOT)
#define WS_PERTICK      (WS_FIELDS + WS_ENT_PER_TICK)
/* tail: 4 env boundary-evidence pairs (fluid packed, block light) + per-chunk combined hashes +
 * 4 entity-summary lines (cross-chunk crossings, spawn count, total melee damage, final player HP). */
#define WS_ENT_SUMMARY  4
#define WS_TAIL         (4 * 2 + TWM_NCHUNKS + WS_ENT_SUMMARY)

/* Per-env scratch: env-CA buffers (halo) + the player's collision-AABB workspace + the persistent
 * cross-chunk entity store (double-buffered) + the entity tick's A-star/collision/spawn workspace. All
 * of this is allocated ONCE by the driver and reused across ticks (device-heap friendly, no stack). */
typedef struct {
    TwhScratch env;                 /* fluid + light region buffers (off the CUDA stack) */
    McAABB     blocks[PSV_MAX_BLOCKS];
    EwState    estate;              /* persistent double-buffered entity store + running counters */
    EwScratch  escratch;           /* pathfinding window/work + collision list + spawn scene */
} WsScratch;

/* ============================ ENTITY-PHASE HOOK (ACTIVATED) ============================
 * The entity-update phase runs each tick in canonical order AFTER light and BEFORE the player
 * (SPEC: block updates / random ticks / fluid / light / ENTITY UPDATE / player). It reuses the
 * VERIFIED entities_world logic - mob spawn cycle (light + solid-ground gated), zombie AI
 * (idle/chase/attack), A* pathfinding, cross-chunk movement/collision, and melee combat both
 * directions - by CALLING entities_world's helper functions directly. Only the ORCHESTRATION here
 * differs from entities_world's own ew_tick: the mobs target the world_step PsvPlayer (not the
 * scripted slot-0 target), and mob melee damages the real player's health (entities_world.* stays
 * untouched and independently verified).
 *
 * DOUBLE-BUFFER CONTRACT (SPEC rule 3), preserved exactly:
 *   - The mob tick READS tick-N state: the `enow` entity store, the immutable `now` world blocks
 *     (real terrain + the flat entity arena overlaid at EW_ARENA_Y), and the player's tick-N pos
 *     (read BEFORE the player phase runs, so pl still holds tick N).
 *   - It WRITES tick-N+1 into the `enext` entity store, and applies mob melee to pl->health (which
 *     the player phase THEN reads in psv_vitals_tick -> the player's next-tick health). No phase
 *     reads its own tick's writes across the now/next boundary.
 *   - Runtime RNG is the stateless world-coord hash (rule 1: mc_hash_seed on seed/tick/world-coords/
 *     id), identical to entities_world -> CPU==CUDA holds by construction.
 * INTRA-TICK ORDER: env CAs -> ENTITY update (mobs melee the player) -> player (physics + the
 * survival action tape's own attack retaliates on the nearest in-reach mob) -> tick++/swap.
 *
 * MC_NOINLINE (device): the A-star/collision frame must not share the per-thread stack with worldgen
 * (256KB device cap) - mirrors entities_world's ew_tick. */
MC_HD MC_NOINLINE static void ws_entity_phase(EwState *es, EwScratch *esc, const Chunk *now,
                                              const McSinTable *stab, PsvPlayer *pl, i64 seed,
                                              i64 tick) {
    EwStore *enow  = ew_now(es);
    EwStore *enext = ew_next(es);
    int i;

    ew_store_copy(enext, enow);        /* double-buffer baseline (rule 3) */

    /* player position observed by every mob this tick = the world_step player's tick-N pos. */
    double pnx = pl->ent.posX, pny = pl->ent.posY, pnz = pl->ent.posZ;

    /* ---- mob AI + pathfinding + movement + melee (read now/enow, write enext + pl->health) ---- */
    for (i = 1; i < EW_MAX_ENTITIES; ++i) {
        if (enow->type[i] != EW_TYPE_ZOMBIE || !enow->alive[i]) continue;

        if (enext->attack_time[i] > 0) enext->attack_time[i]--;

        double zx = enow->x[i], zy = enow->y[i], zz = enow->z[i];
        double d3 = sqrt((zx - pnx) * (zx - pnx) + (zy - pny) * (zy - pny) + (zz - pnz) * (zz - pnz));
        double dxz = ew_dist_xz(zx, zz, pnx, pnz);

        if (dxz <= EW_ATTACK_REACH) {
            /* ATTACK: stop, face player, melee on cooldown -> damage the world_step player. */
            enext->ai_state[i] = EW_AI_ATTACK;
            enext->yaw[i] = ew_yaw_toward(pnx - zx, pnz - zz);
            if (enext->attack_time[i] <= 0) {
                McCombatArmor pa = mc_combat_armor_set(EW_PLAYER_ARMOR);
                float dmg = mc_combat_final_damage(EW_ZOMBIE_RAW, &pa);
                pl->health -= dmg;                    /* mob melee -> player's NEXT-tick health */
                es->total_damage += dmg;
                enext->attack_time[i] = EW_ATTACK_COOLDOWN;
            }
            ew_mob_move(now, esc, stab, enext, i, 0 /* stationary */);
        } else if (d3 <= EW_FOLLOW_RANGE) {
            /* CHASE: A* repath on interval toward the player, follow the waypoint. */
            enext->ai_state[i] = EW_AI_CHASE;
            if (enext->repath_timer[i] <= 0) {
                ew_repath(now, esc, enext, i, pnx, pnz);
                u64 h = mc_hash_seed(es->seed, tick, mc_floor(zx), mc_floor(zy), mc_floor(zz),
                                     (u32)(0x52455000u ^ (u32)enow->id[i]));
                enext->repath_timer[i] = EW_REPATH_INTERVAL + mc_hash_bound(h, 4);
            } else {
                enext->repath_timer[i]--;
            }
            ew_mob_move(now, esc, stab, enext, i, 1);
        } else {
            /* IDLE: hash-RNG wander target keyed on world coords + entity id (rule 1). */
            enext->ai_state[i] = EW_AI_IDLE;
            u64 h = mc_hash_seed(es->seed, tick, mc_floor(zx), mc_floor(zy), mc_floor(zz),
                                 EW_PURPOSE_WANDER ^ (u32)enow->id[i]);
            int rx = mc_hash_bound(h, 7) - 3;
            int rz = mc_hash_bound(mc_hash64(h + 1ULL), 7) - 3;
            enext->path_tx[i] = zx + (double)rx;
            enext->path_tz[i] = zz + (double)rz;
            enext->path_ty[i] = EW_STAND_Y;
            ew_mob_move(now, esc, stab, enext, i, 1);
        }

        /* cross-chunk crossing evidence: chunk index changed this tick? */
        {
            int ncx = ew_chunk_of(enext->x[i]);
            int ncz = ew_chunk_of(enext->z[i]);
            if (ncx != enow->cx[i] || ncz != enow->cz[i]) es->cross_crossings++;
            enext->cx[i] = ncx;
            enext->cz[i] = ncz;
        }
    }

    /* ---- player retaliation: honor the survival action tape - if this tick's action attacks and a
     * zombie is within reach, hit the nearest one (verified combat_math, opposite direction). Reads
     * tick-N mob positions from `enow`; slot 0's attack_time doubles as the player's melee cooldown
     * (slot 0 carries no entity here - the real player lives in the PsvPlayer struct). ---- */
    {
        PsvAction pa = psv_action_for_tick(seed, (int)tick, pl->ent.onGround);
        if (enext->attack_time[0] > 0) enext->attack_time[0]--;
        if (pa.attack && enext->attack_time[0] <= 0) {
            int best = -1;
            double bestd = EW_ATTACK_REACH;
            for (i = 1; i < EW_MAX_ENTITIES; ++i) {
                if (enow->type[i] != EW_TYPE_ZOMBIE || !enow->alive[i]) continue;
                double d = ew_dist_xz(enow->x[i], enow->z[i], pnx, pnz);
                if (d <= bestd) { bestd = d; best = i; }
            }
            if (best >= 0) {
                float raw = mc_combat_weapon_raw(EW_PLAYER_WEAPON);
                McCombatArmor za = mc_combat_armor_set(0);   /* zombie: no armor */
                float dmg = mc_combat_final_damage(raw, &za);
                enext->health[best] -= dmg;
                es->total_damage += dmg;
                enext->attack_time[0] = EW_ATTACK_COOLDOWN;
                if (enext->health[best] <= 0.0f) enext->alive[best] = 0;
            }
        }
    }

    /* ---- mob spawning cycle: verified WorldEntitySpawner gates (light + solid-ground), spawn under
     * cap onto the walkable arena; the spawned mob then chases the player on later ticks. ---- */
    {
        int alive_mobs = 0, s;
        ms_run(&esc->ms, tick);         /* one hostile spawn cycle (hash RNG over the dark shaft) */
        for (i = 1; i < EW_MAX_ENTITIES; ++i)
            if (enext->type[i] == EW_TYPE_ZOMBIE && enext->alive[i]) alive_mobs++;
        if (alive_mobs < EW_MOB_CAP) {
            for (s = 0; s < esc->ms.n_decisions; ++s) {
                u64 d = esc->ms.decisions[s];
                int result = (int)((d >> 56) & 0xF);
                if (result != MS_RES_SPAWN) continue;
                {
                    int dxl = (int)((d >> 16) & 0xFF);
                    int dzl = (int)((d >> 32) & 0xFF);
                    double wx = (double)(-14 + 2 * dxl) + 0.5;
                    double wz = (double)(-14 + (dzl % 8)) + 0.5;
                    int slot;
                    if (wx < -15.0) wx = -15.0; if (wx > 31.0) wx = 31.0;
                    slot = ew_store_spawn(enext, EW_TYPE_ZOMBIE, es->next_id, wx, EW_STAND_Y, wz,
                                          EW_ZOMBIE_HEALTH);
                    if (slot >= 0) {
                        enext->cx[slot] = ew_chunk_of(wx);
                        enext->cz[slot] = ew_chunk_of(wz);
                        es->next_id++;
                        es->spawn_count++;
                    }
                    break;   /* at most one spawn per tick */
                }
            }
        }
    }

    es->tick = tick + 1;
    ew_swap(es);
}

/* ---- entity scene init: overlay the flat combat arena, seed the entity store + spawn scene ----
 * Reuses entities_world's ew_overlay_arena (flat stone platform at EW_ARENA_Y across all chunks,
 * BOTH buffers) so mobs have a deterministic walkable multi-chunk surface. The player spawns high
 * (PSV_SPAWN_Y=120) and falls onto this same arena (top at EW_STAND_Y), unifying player + mobs at
 * one y so 3D-follow / XZ-reach combat is natural. The arena sits at y=100, well ABOVE the env
 * fixtures (fluid y=62..65, torch y=50), so the fluid/light cross-chunk evidence is unaffected.
 * MC_NOINLINE: keep the recursive-free but loop-heavy overlay off any shared worldgen frame. */
MC_HD MC_NOINLINE static void ws_entity_init(WsScratch *s, TwmWorld *w, u64 seed) {
    EwState *es = &s->estate;

    ew_overlay_arena(w);               /* flat stone arena at EW_ARENA_Y, both buffers (verified) */

    ew_store_clear(&es->ea);
    ew_store_clear(&es->eb);
    es->ecur = 0;
    es->tick = 0;
    es->seed = seed;
    es->next_id = 100;
    es->cross_crossings = 0;
    es->spawn_count = 0;
    es->total_damage = 0.0f;

    ms_init_flat(&s->escratch.ms, seed);
    ew_darken_ms(&s->escratch.ms);     /* dark roofed shaft so the spawn gates actually pass */

    {
        EwStore *st0 = &es->ea;        /* slot 0 unused (the real player is the PsvPlayer). */
        /* scenario zombies near the player's spawn column (PSV_SPAWN_X/Z=8.5): one adjacent for an
         * immediate melee interaction, two farther out that chase the player across chunk borders. */
        ew_store_spawn(st0, EW_TYPE_ZOMBIE, 1, PSV_SPAWN_X + 1.0, EW_STAND_Y, PSV_SPAWN_Z, EW_ZOMBIE_HEALTH);
        ew_store_spawn(st0, EW_TYPE_ZOMBIE, 2, 20.5, EW_STAND_Y, 8.5, EW_ZOMBIE_HEALTH);
        ew_store_spawn(st0, EW_TYPE_ZOMBIE, 3, 27.5, EW_STAND_Y, 10.5, EW_ZOMBIE_HEALTH);
        for (int i = 1; i < EW_MAX_ENTITIES; ++i)
            if (st0->type[i] == EW_TYPE_ZOMBIE) {
                st0->cx[i] = ew_chunk_of(st0->x[i]);
                st0->cz[i] = ew_chunk_of(st0->z[i]);
            }
    }
    ew_store_copy(&es->eb, &es->ea);
}

/* ---- one unified tick over the shared double-buffered world (env CAs THEN player) ---- */
MC_HD static inline void ws_tick(TwmWorld *w, WsScratch *s, const McSinTable *st, PsvPlayer *pl) {
    Chunk *now  = twm_now(w);
    Chunk *next = twm_next(w);
    i64    tick = w->tick;
    i64    seed = (i64)w->seed;
    int    i;

    /* 1) establish the tick N+1 write buffer. */
    for (i = 0; i < TWM_NCHUNKS; ++i) twc_copy_chunk(&next[i], &now[i]);

    /* ---- ENVIRONMENT PHASE (halo CAs; read now, refine next) ---- */
    /* 2) block tickers: block updates + random ticks (world-coord hash RNG, rule 1). */
    bth_tick_grid(now, next, TWH_DIM, w->seed, tick, TWH_BT_OY, TWH_BT_H);
    /* 3) fluid CA on the post-ticker next state; crosses chunk borders. */
    ff_ca_step_halo(next, next, TWH_DIM, TWH_FLUID_OY, TWH_FLUID_NY,
                    0, TWH_FLUID_OY, 0, s->env.fcur, s->env.ftmp);
    /* 4) light fixpoint on the post-fluid next state; crosses chunk borders. */
    lp_propagate_halo(next, TWH_DIM, s->env.sky, s->env.blk, s->env.tsky, s->env.tblk,
                      s->env.lblocks, s->env.hm, TWH_LIGHT_ITERS);

    /* 5) ENTITY UPDATE: mob spawn / AI / A-star / cross-chunk move / melee over now, targeting player.
     *    Reads tick-N (`now` blocks + entity store + player pos), writes tick-N+1 (entity store +
     *    the player's next-tick health). Runs BEFORE the player phase (see the hook contract). */
    ws_entity_phase(&s->estate, &s->escratch, now, st, pl, seed, tick);

    /* ---- PLAYER PHASE (reads now for physics/raycast; writes edits to next) ---- *
     * Re-composes player_survival's verified psv_tick body EXACTLY (same calls, same order),
     * MINUS psv_tick's own copy-forward/swap - world_step already owns the double buffer. */
    {
        PsvAction act = psv_action_for_tick(seed, (int)tick, pl->ent.onGround);
        int    was_air    = !pl->ent.onGround;
        double prev_min_y = pl->ent.box.minY;
        pl->yaw   = act.yaw;
        pl->pitch = act.pitch;

        psv_physics_tick(now, st, pl, &act, s->blocks);

        /* block break: drop the broken block into the inventory (verified stack rules) */
        if (act.do_break) {
            int hx, hy, hz, ax, ay, az;
            int r = psv_raycast(now, st, pl, &hx, &hy, &hz, &ax, &ay, &az);
            if (r >= 0) {
                int bid = psv_get_block(now, hx, hy, hz);
                BptProps bp = mc_bpt_props(bid);
                if (psv_solid(bid) && bp.hardness >= 0.0f) {
                    psv_set_block(next, hx, hy, hz, BLK_AIR);
                    ICStack drop = ic_mk(bid, 1, 0);
                    isr_add_item_stack_to_inventory(&pl->inv, &drop);
                    pl->break_events++;
                }
            }
        }

        /* block place: consume from the held stack, set the world block */
        if (act.do_place) {
            int hx, hy, hz, ax, ay, az;
            int r = psv_raycast(now, st, pl, &hx, &hy, &hz, &ax, &ay, &az);
            if (r == 1) {
                ICStack held = isr_get_stack(&pl->inv, pl->inv.current_item);
                if (!isr_is_empty(&held) && psv_get_block(now, ax, ay, az) == BLK_AIR) {
                    ICStack used = isr_decr_stack_size(&pl->inv, pl->inv.current_item, 1);
                    if (!isr_is_empty(&used)) {
                        psv_set_block(next, ax, ay, az, used.item);
                        pl->place_events++;
                    }
                }
            }
        }

        if (act.attack) pl->swing_events++;

        psv_vitals_tick(pl, was_air, prev_min_y);
    }

    /* 7) advance + swap: next becomes tick N+1's now. */
    w->tick = tick + 1;
    twm_swap(w);
}

/* Full run: gen the fixtured world into both buffers, spawn the player, tick WS_FIELDS/tail. */
MC_HD static inline void ws_run(TwmWorld *w, WsScratch *s, ChunkPrimer *primer, CpScratch *sc,
                                const McSinTable *st, u64 seed, int nticks, u64 *out) {
    PsvPlayer pl;
    int t, k, i;

    twh_gen(w, &s->env, primer, sc, st, seed);   /* real terrain + halo fixtures, light settled */
    psv_player_init(&pl);                         /* spawn survival player high above spawn */
    ws_entity_init(s, w, seed);                   /* overlay arena + seed scenario mobs (both buffers) */

    for (t = 0; t < nticks; ++t) {
        const Chunk *now;
        ws_tick(w, s, st, &pl);
        now = twm_now(w);                         /* post-swap: holds this tick's writes */
        out[(size_t)t * WS_PERTICK + 0] = twh_light_hash(now);  /* env light CA ran */
        psv_emit(now, &pl, &out[(size_t)t * WS_PERTICK + 1]);   /* player + block-edit state */
        /* entity store (post-swap holds this tick's mob writes): per-slot meta/pos/vel/hp/path/chunk */
        ew_dump_tick(ew_now(&s->estate), out, (int)((size_t)t * WS_PERTICK + WS_FIELDS));
    }

    /* ---- tail: prove the env CAs still cross the chunk boundary + spatial coverage + entity summary ---- */
    {
        const Chunk *now = twm_now(w);
        int ci = twh_center_index();
        const Chunk *c1 = &now[ci + 1];           /* +x neighbour of the centre chunk */
        size_t base = (size_t)nticks * WS_PERTICK;
        for (k = 0; k < 4; ++k) {
            u16 fs = mc_get(c1, k, TWH_WATER_Y, TWH_CHAN_Z);
            u8  ls = c1->light[mc_idx(k, TWH_TORCH_Y, TWH_CHAN_Z)];
            out[base + (size_t)k * 2 + 0] = ((u64)mc_state_id(fs) << 8) | (u64)mc_state_meta(fs);
            out[base + (size_t)k * 2 + 1] = (u64)mc_light_block(ls);
        }
        base += 8;
        for (i = 0; i < TWM_NCHUNKS; ++i) out[base + (size_t)i] = twh_chunk_hash(&now[i]);
        base += TWM_NCHUNKS;
        /* entity evidence: cross-chunk crossings, spawns via the gated cycle, total melee damage
         * (both directions), and the final player health (mob melee applied + vitals). */
        out[base + 0] = s->estate.cross_crossings;
        out[base + 1] = s->estate.spawn_count;
        out[base + 2] = (u64)ew_fbits(s->estate.total_damage);
        out[base + 3] = (u64)psv_f2u(pl.health);
    }
}

#endif /* MC_WORLD_STEP_H */
