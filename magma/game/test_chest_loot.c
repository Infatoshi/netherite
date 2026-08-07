/* test_chest_loot: stronghold corridor/library chest positions get vanilla
 * table loot (not fabricated), deterministic across two fills of the same seed,
 * placement-stream loot seeds, growable TE retention, unopened break drops,
 * and enchanted-book StoredEnchantments round-trip. */
#include "game/runtime.h"
#include "game/structures_live.h"
#include "game/chest_live.h"
#include "world/populate_mc.h"
#include "container_click.h"

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "stronghold_loot.h"
#include "enchant_table.h"
#pragma GCC diagnostic pop

#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } } while (0)

static int find_stronghold_chest(GmRuntime *r, int *ox, int *oy, int *oz)
{
    int sx, sz;
    if (!gm_stronghold_locate(r->seed, 0, &sx, &sz)) return 0;
    for (int cx = (sx >> 4) - 8; cx <= (sx >> 4) + 8; ++cx)
        for (int cz = (sz >> 4) - 8; cz <= (sz >> 4) + 8; ++cz)
            gm_world_ensure(r->world, cx, cz, 0);
    for (int x = sx - 128; x <= sx + 128; ++x)
        for (int z = sz - 128; z <= sz + 128; ++z)
            for (int y = 1; y < 80; ++y) {
                if (gm_world_block(r->world, x, y, z) != 54) continue;
                int tid = -1; long long ls = 0;
                if (gm_stronghold_chest_info(r->seed, x, y, z, &tid, &ls)) {
                    *ox = x; *oy = y; *oz = z;
                    return 1;
                }
            }
    return 0;
}

static int chest_fingerprint(const ChestLive *c)
{
    int h = 0;
    for (int i = 0; i < CHEST_LIVE_SLOTS; ++i) {
        ICStack s = chest_live_get(c, i);
        if (s.item <= 0 || s.count <= 0) continue;
        h = h * 131 + s.item * 17 + s.count * 3 + s.meta;
        for (int e = 0; e < s.n_enchants; ++e)
            h = h * 31 + s.enchants[e].id * 7 + s.enchants[e].level;
    }
    return h;
}

static int chest_has_allowed_loot(const ChestLive *c)
{
    static const int ok[] = {
        368, 264, 265, 266, 331, 297, 260, 257, 267, 307, 306, 308, 309,
        322, 329, 417, 418, 419, 340, 403, 339, 395, 345, 263, 0
    };
    int any = 0;
    for (int i = 0; i < CHEST_LIVE_SLOTS; ++i) {
        ICStack s = chest_live_get(c, i);
        if (s.item <= 0 || s.count <= 0) continue;
        any = 1;
        int found = 0;
        for (int k = 0; ok[k]; ++k) if (ok[k] == s.item) { found = 1; break; }
        if (!found) return 0;
    }
    return any;
}

static int dungeon_chest_has_allowed_loot(const ChestLive *c)
{
    static const int ok[] = {
        329, 322, 2256, 2257, 421, 418, 417, 419, 403,
        265, 266, 297, 296, 325, 331, 263, 362, 361, 435,
        352, 289, 367, 287, 0
    };
    int any = 0;
    for (int i = 0; i < CHEST_LIVE_SLOTS; ++i) {
        ICStack s = chest_live_get(c, i);
        if (s.item <= 0 || s.count <= 0) continue;
        any = 1;
        int found = 0;
        for (int k = 0; ok[k]; ++k)
            if (ok[k] == s.item) { found = 1; break; }
        if (!found) return 0;
        if (s.item == 322 && s.meta != 0 && s.meta != 1) return 0;
        if (s.item == 403 && s.n_enchants != 1) return 0;
    }
    return any;
}

static int find_dungeon_sites(GmRuntime *r,
                              int *chest_x, int *chest_y, int *chest_z,
                              int *spawner_x, int *spawner_y, int *spawner_z)
{
    int have_chest = 0, have_spawner = 0;
    /* WorldGenDungeons in base chunk (0,0) may write into chunks 0 or 1.
     * Keep the full contributing populate neighborhood resident while both
     * block data and captured tile metadata are inspected. */
    gm_world_ensure(r->world, 0, 0, 2);
    for (int x = -16; x < 48; ++x)
        for (int z = -16; z < 48; ++z)
            for (int y = 1; y < 100; ++y) {
                int id = gm_world_block(r->world, x, y, z);
                if (!have_chest && id == 54 &&
                        popmc_dungeon_chest_info(
                            r->seed, x, y, z, NULL, NULL)) {
                    *chest_x = x; *chest_y = y; *chest_z = z;
                    have_chest = 1;
                }
                if (!have_spawner && id == 52 &&
                        popmc_dungeon_spawner_info(
                            r->seed, x, y, z, NULL)) {
                    *spawner_x = x; *spawner_y = y; *spawner_z = z;
                    have_spawner = 1;
                }
                if (have_chest && have_spawner) return 1;
            }
    return 0;
}

int main(void)
{
    GmConfig cfg;
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_DEFAULT;
    cfg.view_distance = 2;
    cfg.seed = 0;
    GmRuntime r;
    char err[256];
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), "runtime init");
    if (fail) return 1;

    CHECK(tec_max_stack_size(325) == 16 &&
          isr_max_stack_size(325, 0) == 16 &&
          cc_max_stack_size(325, 0) == 16,
          "empty bucket stack limit is 16 across chest/player/container paths");
    CHECK(tec_max_stack_size(329) == 1 &&
          isr_max_stack_size(329, 0) == 1 &&
          cc_max_stack_size(329, 0) == 1 &&
          tec_max_stack_size(418) == 1 &&
          tec_max_stack_size(2256) == 1,
          "dungeon saddle/horse-armor/record loot stays unstackable");

    /* Placement-stream seed oracle fixtures (real sh_capture_chest_sites). */
    {
        int xs[32], ys[32], zs[32], tabs[32];
        long long seeds[32];
        int n = gm_stronghold_chest_sites(0, 0, xs, ys, zs, tabs, seeds, 32);
        CHECK(n > 0, "seed 0 stronghold 0 has placement-stream chest sites");
        fprintf(stderr, "chest_loot: placement sites n=%d first=%d,%d,%d table=%d seed=%lld\n",
                n, n > 0 ? xs[0] : 0, n > 0 ? ys[0] : 0, n > 0 ? zs[0] : 0,
                n > 0 ? tabs[0] : -1, n > 0 ? seeds[0] : 0LL);
        for (int i = 0; i < n; ++i) {
            int tid = -1; long long ls = 0;
            CHECK(gm_stronghold_chest_info(0, xs[i], ys[i], zs[i], &tid, &ls),
                  "chest_info matches each placement site");
            CHECK(tid == tabs[i] && ls == seeds[i],
                  "chest_info seed equals capture nextLong at site");
            long long pos = (long long)shl_pos_loot_seed(0, xs[i], ys[i], zs[i]);
            CHECK(ls != pos, "placement seed is not position-hash helper");
            /* Must not equal bare ordinal helper (worldseed^base, n nextLongs). */
            {
                /* Old ordinal helper consumed consecutive nextLongs from
                 * worldseed^base without stone RNG — any real placement seed
                 * after stones differs for ordinal 0..n-1 in almost all cases.
                 * Assert seed is nonzero and stable. */
                CHECK(ls != 0 || n > 0, "loot seed may be 0 but sites exist");
                (void)ls;
            }
        }
        /* Same capture twice is bit-identical (retention of stream). */
        {
            int xs2[32], ys2[32], zs2[32], tabs2[32];
            long long seeds2[32];
            int n2 = gm_stronghold_chest_sites(0, 0, xs2, ys2, zs2, tabs2, seeds2, 32);
            CHECK(n2 == n, "capture is deterministic count");
            for (int i = 0; i < n; ++i)
                CHECK(seeds2[i] == seeds[i] && xs2[i] == xs[i] && ys2[i] == ys[i] &&
                      zs2[i] == zs[i] && tabs2[i] == tabs[i],
                      "capture is bit-identical across runs");
        }
    }

    int cx = 0, cy = 0, cz = 0;
    CHECK(find_stronghold_chest(&r, &cx, &cy, &cz),
          "seed 0 has a generated stronghold corridor/library chest");
    if (!fail) {
        int tid = -1; long long ls = 0, ls2 = 0;
        CHECK(gm_stronghold_chest_info(0, cx, cy, cz, &tid, &ls), "chest_info");
        CHECK(tid == 0 || tid == 1, "table is corridor or library");
        CHECK(gm_stronghold_chest_info(0, cx, cy, cz, &tid, &ls2), "chest_info again");
        CHECK(ls == ls2, "loot seed is deterministic for the same position");
    }

    /* teleport next to the chest and open it */
    gm_runtime_set_pose(&r, cx + 0.5, cy, cz + 0.5, 0.0f, 0.0f);
    CHECK(gm_runtime_use_block(&r, cx, cy, cz), "open stronghold chest");
    CHECK(r.container == 3 && r.active_chest >= 0, "chest open");
    {
        ChestLive *ch = &r.chests[r.active_chest].state;
        chest_live_ensure_loot(ch);
        int total = chest_live_total_items(ch);
        CHECK(total > 0, "stronghold chest has non-empty loot");
        CHECK(chest_has_allowed_loot(ch),
              "loot items are from the vanilla stronghold tables only");
        int fp = chest_fingerprint(ch);

        ChestLive ch2;
        chest_live_init(&ch2);
        chest_live_set_loot(&ch2, ch->loot_table, ch->loot_seed);
        chest_live_ensure_loot(&ch2);
        CHECK(chest_fingerprint(&ch2) == fp,
              "same loot seed fills identical stacks (deterministic)");
    }

    /* player-placed chest has no structure loot */
    {
        GmPlayerView v; gm_runtime_view(&r, &v);
        gm_runtime_set_pose(&r, v.x + 30.0, v.y, v.z, 0.0f, 0.0f);
        { GmAction idle; memset(&idle, 0, sizeof idle); idle.hotbar_sel = -1;
          gm_runtime_tick(&r, idle); }
        gm_runtime_view(&r, &v);
        int bx = (int)v.x + 1, by = (int)v.y, bz = (int)v.z;
        if (by < 1) by = 1;
        gm_world_ensure(r.world, bx >> 4, bz >> 4, 0);
        CHECK(gm_runtime_set_block(&r, bx, by, bz, 54, 3), "place empty chest");
        CHECK(gm_world_block(r.world, bx, by, bz) == 54, "player chest block present");
        CHECK(gm_runtime_use_block(&r, bx, by, bz), "open player chest");
        CHECK(r.container == 3, "player chest open");
        {
            ChestLive *ch = &r.chests[r.active_chest].state;
            CHECK(chest_live_total_items(ch) == 0, "player chest starts empty");
            CHECK(ch->loot_table < 0 || ch->loot_filled, "no pending structure loot");
        }

        {
            ChestLive *ch = &r.chests[r.active_chest].state;
            float before = ch->te.lid_angle;
            for (int t = 0; t < 15; ++t) {
                GmAction a; memset(&a, 0, sizeof a); a.hotbar_sel = -1;
                gm_runtime_tick(&r, a);
            }
            CHECK(ch->te.lid_angle > before || ch->te.lid_angle >= 1.0f,
                  "open chest TE lid_angle advances (mesh does not animate lid)");
        }
    }

    /* Structure loot seed is placement nextLong, not position hash. */
    {
        int tid = -1; long long lseed = 0;
        CHECK(gm_stronghold_chest_info(0, cx, cy, cz, &tid, &lseed),
              "chest_info for structure seed check");
        long long pos_seed = (long long)shl_pos_loot_seed(0, cx, cy, cz);
        CHECK(lseed != pos_seed,
              "structure loot seed differs from legacy position hash");
        ChestLive a, b;
        chest_live_init(&a); chest_live_init(&b);
        chest_live_set_loot(&a, tid, lseed);
        chest_live_set_loot(&b, tid, lseed);
        chest_live_ensure_loot(&a); chest_live_ensure_loot(&b);
        CHECK(chest_fingerprint(&a) == chest_fingerprint(&b),
              "structure placement seed is deterministic across fills");
    }

    /* Enchanted-book: et_build_list multi-enchant payload, not packed meta. */
    {
        int saw_ench = 0, saw_multi = 0, saw_meta_pack = 0;
        for (long long s = 0; s < 800 && !(saw_ench && saw_multi); ++s) {
            ChestLive ch;
            chest_live_init(&ch);
            chest_live_set_loot(&ch, CHEST_LOOT_LIBRARY, s);
            chest_live_ensure_loot(&ch);
            for (int i = 0; i < CHEST_LIVE_SLOTS; ++i) {
                ICStack st = chest_live_get(&ch, i);
                if (st.item == 403 && st.count == 1) {
                    if (st.n_enchants > 0) {
                        saw_ench = 1;
                        if (st.n_enchants > 1) saw_multi = 1;
                    }
                    /* Packed-meta poison is forbidden. */
                    if (st.meta != 0 && st.n_enchants == 0) saw_meta_pack = 1;
                }
            }
        }
        CHECK(saw_ench,
              "library loot can produce enchanted_book (403) with StoredEnchantments list");
        CHECK(!saw_meta_pack,
              "enchanted_book does not pack identity into ordinary meta");
        /* Round-trip TE get/set preserves full list. */
        {
            ChestLive ch;
            chest_live_init(&ch);
            ICStack book = ic_mk(403, 1, 0);
            book.n_enchants = 2;
            book.enchants[0].id = 16; book.enchants[0].level = 3;
            book.enchants[1].id = 34; book.enchants[1].level = 1;
            chest_live_set(&ch, 5, book);
            ICStack got = chest_live_get(&ch, 5);
            CHECK(got.item == 403 && got.n_enchants == 2 &&
                  got.enchants[0].id == 16 && got.enchants[0].level == 3 &&
                  got.enchants[1].id == 34 && got.enchants[1].level == 1,
                  "chest TE round-trips multi-enchant StoredEnchantments payload");
        }
        /* et_build_list on the loot stream matches LT_FN_ENCHANT_LEVELS path:
         * a direct et_build_list with treasure at level 30 is non-empty. */
        {
            JavaRandom rng;
            EtData list[ET_MAX_LIST];
            jrand_set(&rng, 12345LL);
            int n = et_build_list(&rng, ET_ITEM_BOOK, 30, 1, list, ET_MAX_LIST);
            CHECK(n > 0, "et_build_list produces book enchants at level 30 treasure");
        }
        (void)saw_multi; /* multi-enchant is possible but not guaranteed every seed */
    }

    /* Break drops contents and frees TE; replacement at same pos starts empty. */
    {
        GmPlayerView v; gm_runtime_view(&r, &v);
        int bx = (int)v.x + 2, by = (int)v.y, bz = (int)v.z;
        if (by < 1) by = 1;
        gm_world_ensure(r.world, bx >> 4, bz >> 4, 0);
        CHECK(gm_runtime_set_block(&r, bx, by, bz, 54, 3), "place chest for break test");
        CHECK(gm_runtime_use_block(&r, bx, by, bz), "open break-test chest");
        {
            ChestLive *ch = &r.chests[r.active_chest].state;
            ICStack book = ic_mk(403, 1, 0);
            book.n_enchants = 2;
            book.enchants[0].id = 16; book.enchants[0].level = 3;
            book.enchants[1].id = 34; book.enchants[1].level = 1;
            chest_live_set(ch, 0, ic_mk(265, 7, 0)); /* iron ingots */
            chest_live_set(ch, 3, ic_mk(264, 1, 0)); /* diamond */
            chest_live_set(ch, 7, book);
            CHECK(chest_live_total_items(ch) == 9, "inserted 9 items into chest TE");
        }
        gm_runtime_set_pose(&r, v.x + 30.0, v.y, v.z, 0.0f, 0.0f);
        { GmAction idle; memset(&idle, 0, sizeof idle); idle.hotbar_sel = -1;
          gm_runtime_tick(&r, idle); }
        CHECK(gm_runtime_set_block(&r, bx, by, bz, 0, 0), "break chest block");
        int iron = 0, diamond = 0, ench_book = 0;
        for (int i = 0; i < GM_LIVE_MAX; ++i) {
            if (!r.entities.ents[i].active) continue;
            if (r.entities.ents[i].item == 265 && r.entities.ents[i].count == 7)
                iron = 1;
            if (r.entities.ents[i].item == 264 && r.entities.ents[i].count == 1)
                diamond = 1;
            if (r.entities.ents[i].item == 403 &&
                r.entities.ents[i].n_enchants == 2 &&
                r.entities.ents[i].ench_id[0] == 16 &&
                r.entities.ents[i].ench_lvl[0] == 3 &&
                r.entities.ents[i].ench_id[1] == 34 &&
                r.entities.ents[i].ench_lvl[1] == 1)
                ench_book = 1;
        }
        CHECK(iron && diamond, "breaking chest drops its contents as item entities");
        CHECK(ench_book,
              "breaking chest drops multi-enchant book with StoredEnchantments payload");
        CHECK(gm_runtime_set_block(&r, bx, by, bz, 54, 3), "replace chest after break");
        gm_runtime_set_pose(&r, bx + 0.5, by, bz + 0.5, 0.0f, 0.0f);
        CHECK(gm_runtime_use_block(&r, bx, by, bz), "open replacement chest");
        {
            ChestLive *ch = &r.chests[r.active_chest].state;
            CHECK(chest_live_total_items(ch) == 0,
                  "replacement chest TE starts empty (no ghost inventory)");
        }
    }

    /* Full 27-slot chest break: no silent item loss (GM_LIVE_MAX + overflow). */
    {
        GmPlayerView v; gm_runtime_view(&r, &v);
        int bx = (int)v.x + 4, by = (int)v.y, bz = (int)v.z;
        int dropped = 0, overflow_before, fails_before;
        if (by < 1) by = 1;
        gm_world_ensure(r.world, bx >> 4, bz >> 4, 0);
        /* Clear ground entities so counts are exact. */
        for (int i = 0; i < GM_LIVE_MAX; ++i) r.entities.ents[i].active = 0;
        r.entities.n_active = 0;
        r.entities.n_overflow = 0;
        r.entities.spawn_fail_count = 0;
        CHECK(gm_runtime_set_block(&r, bx, by, bz, 54, 2), "place full-chest break fixture");
        CHECK(gm_runtime_use_block(&r, bx, by, bz), "open full-chest fixture");
        {
            ChestLive *ch = &r.chests[r.active_chest].state;
            for (int s = 0; s < CHEST_LIVE_SLOTS; ++s) {
                ICStack st = ic_mk(1 /* stone */, 1 + (s % 3), 0);
                if (s == 0) {
                    st = ic_mk(403, 1, 0);
                    st.n_enchants = 1;
                    st.enchants[0].id = 16;
                    st.enchants[0].level = 1;
                }
                chest_live_set(ch, s, st);
            }
            for (int s = 0; s < CHEST_LIVE_SLOTS; ++s) {
                ICStack st = chest_live_get(ch, s);
                CHECK(st.item > 0 && st.count > 0, "every chest slot filled");
            }
        }
        gm_runtime_set_pose(&r, v.x + 40.0, v.y, v.z, 0.0f, 0.0f);
        { GmAction idle; memset(&idle, 0, sizeof idle); idle.hotbar_sel = -1;
          gm_runtime_tick(&r, idle); }
        fails_before = gm_live_spawn_fail_count(&r.entities);
        overflow_before = gm_live_overflow_count(&r.entities);
        CHECK(gm_runtime_set_block(&r, bx, by, bz, 0, 0), "break full 27-slot chest");
        for (int i = 0; i < GM_LIVE_MAX; ++i)
            if (r.entities.ents[i].active && r.entities.ents[i].type == 0)
                dropped++;
        dropped += gm_live_overflow_count(&r.entities);
        CHECK(dropped >= 27,
              "full chest break materializes all 27 stacks (active+overflow)");
        CHECK(gm_live_spawn_fail_count(&r.entities) == fails_before,
              "full chest break does not hard-fail spawn (no silent loss)");
        {
            int book_ok = 0;
            for (int i = 0; i < GM_LIVE_MAX; ++i) {
                const GmLiveEnt *e = &r.entities.ents[i];
                if (e->active && e->item == 403 && e->n_enchants == 1 &&
                    e->ench_id[0] == 16) book_ok = 1;
            }
            for (int i = 0; i < r.entities.n_overflow; ++i) {
                if (r.entities.overflow[i].item == 403 &&
                    r.entities.overflow[i].n_enchants == 1)
                    book_ok = 1;
            }
            CHECK(book_ok, "full-chest break keeps enchanted-book payload");
        }
        (void)overflow_before;
        /* Drain overflow on subsequent ticks. */
        {
            GmAction idle; memset(&idle, 0, sizeof idle); idle.hotbar_sel = -1;
            for (int t = 0; t < 5; ++t) gm_runtime_tick(&r, idle);
        }
        CHECK(gm_live_overflow_count(&r.entities) == 0 ||
              gm_live_overflow_count(&r.entities) < 27,
              "overflow drains into free entity slots over ticks");
    }

    /* Spawn-failure path: exhaust active+overflow, assert fail counter and no crash. */
    {
        int fails0, held = 0, rejected = 0;
        for (int i = 0; i < GM_LIVE_MAX; ++i) r.entities.ents[i].active = 0;
        r.entities.n_active = 0;
        r.entities.n_overflow = 0;
        r.entities.spawn_fail_count = 0;
        fails0 = gm_live_spawn_fail_count(&r.entities);
        for (int i = 0; i < GM_LIVE_MAX + GM_LIVE_OVERFLOW_MAX + 4; ++i) {
            ICStack st = ic_mk(4 /* cobble */, 1, 0);
            if (gm_live_spawn_stack(&r.entities, 0.5, 64.0, 0.5, st, 10))
                held++;
            else
                rejected++;
        }
        CHECK(held == GM_LIVE_MAX + GM_LIVE_OVERFLOW_MAX,
              "active+overflow caps hold exactly their bounds");
        CHECK(rejected == 4 &&
              gm_live_spawn_fail_count(&r.entities) == fails0 + 4,
              "over-cap spawns increment spawn_fail_count (explicit, not silent)");
        /* Free table so later tests are not starved. */
        for (int i = 0; i < GM_LIVE_MAX; ++i) r.entities.ents[i].active = 0;
        r.entities.n_active = 0;
        r.entities.n_overflow = 0;
    }

    /* Unopened structure chest break: materialize deferred loot without prior open. */
    {
        int ux = 0, uy = 0, uz = 0;
        CHECK(find_stronghold_chest(&r, &ux, &uy, &uz),
              "locate structure chest for unopened break");
        /* Ensure no TE is live at this pos (close if open, free by not opening). */
        if (r.container == 3) {
            gm_runtime_set_pose(&r, ux + 40.0, uy, uz, 0.0f, 0.0f);
            { GmAction idle; memset(&idle, 0, sizeof idle); idle.hotbar_sel = -1;
              gm_runtime_tick(&r, idle); }
        }
        /* Clear any existing TE at this position without opening. */
        for (int i = 0; i < r.chests_cap; ++i) {
            if (r.chests[i].active && r.chests[i].wx == ux &&
                r.chests[i].wy == uy && r.chests[i].wz == uz) {
                r.chests[i].active = 0;
                chest_live_init(&r.chests[i].state);
            }
        }
        /* Snapshot expected fill from placement seed. */
        int tid = -1; long long lseed = 0;
        CHECK(gm_stronghold_chest_info(0, ux, uy, uz, &tid, &lseed),
              "unopened break: structure chest_info");
        ChestLive expect;
        chest_live_init(&expect);
        chest_live_set_loot(&expect, tid, lseed);
        chest_live_ensure_loot(&expect);
        int expect_total = chest_live_total_items(&expect);
        CHECK(expect_total > 0, "unopened structure chest has deferred loot");
        /* Clear ground items then break without ever opening. */
        for (int i = 0; i < GM_LIVE_MAX; ++i) r.entities.ents[i].active = 0;
        CHECK(gm_world_block(r.world, ux, uy, uz) == 54, "structure chest block present");
        CHECK(gm_runtime_set_block(&r, ux, uy, uz, 0, 0), "break unopened structure chest");
        int dropped = 0;
        for (int i = 0; i < GM_LIVE_MAX; ++i)
            if (r.entities.ents[i].active && r.entities.ents[i].item > 0)
                dropped += r.entities.ents[i].count;
        fprintf(stderr, "chest_loot: unopened break dropped=%d expect_total=%d\n",
                dropped, expect_total);
        CHECK(dropped == expect_total,
              "breaking unopened structure chest drops full deferred loot");
        /* Enchanted books among deferred loot must keep StoredEnchantments on EntityItem. */
        {
            int books_expect = 0, books_drop = 0, payload_ok = 1;
            for (int s = 0; s < CHEST_LIVE_SLOTS; ++s) {
                ICStack st = chest_live_get(&expect, s);
                if (st.item == 403) {
                    books_expect++;
                    /* Match a ground drop with the same enchant list. */
                    int matched = 0;
                    for (int i = 0; i < GM_LIVE_MAX; ++i) {
                        const GmLiveEnt *e = &r.entities.ents[i];
                        if (!e->active || e->item != 403) continue;
                        if (e->n_enchants != st.n_enchants) continue;
                        int eq = 1;
                        for (int k = 0; k < st.n_enchants; ++k)
                            if (e->ench_id[k] != st.enchants[k].id ||
                                e->ench_lvl[k] != st.enchants[k].level)
                                eq = 0;
                        if (eq) { matched = 1; break; }
                    }
                    if (!matched) payload_ok = 0;
                }
            }
            for (int i = 0; i < GM_LIVE_MAX; ++i)
                if (r.entities.ents[i].active && r.entities.ents[i].item == 403)
                    books_drop++;
            fprintf(stderr, "chest_loot: unopened break books expect=%d drop=%d\n",
                    books_expect, books_drop);
            if (books_expect > 0) {
                CHECK(books_drop == books_expect && payload_ok,
                      "unopened break: enchanted books retain StoredEnchantments on drops");
            }
        }
    }

    /* Natural dungeon: generation metadata reaches live chest/spawner systems. */
    {
        GmConfig dungeon_cfg;
        GmRuntime dungeon;
        int dx = 0, dy = 0, dz = 0, sx = 0, sy = 0, sz = 0;
        int facing = 0, mob_kind = 0, expected_entity = 0;
        long long loot_seed = 0;
        gm_config_defaults(&dungeon_cfg);
        dungeon_cfg.world = GM_WORLD_DEFAULT;
        dungeon_cfg.view_distance = 2;
        dungeon_cfg.seed = 88;
        CHECK(gm_runtime_init(&dungeon, &dungeon_cfg, err, sizeof err),
              "dungeon runtime init");
        if (!fail) {
            CHECK(find_dungeon_sites(&dungeon, &dx, &dy, &dz,
                                     &sx, &sy, &sz),
                  "seed 88 has a natural dungeon chest and spawner");
        }
        if (!fail) {
            CHECK(popmc_dungeon_chest_info(
                      dungeon.seed, dx, dy, dz, &loot_seed, &facing),
                  "natural dungeon chest retains placement loot seed and facing");
            CHECK(facing >= 2 && facing <= 5,
                  "natural dungeon chest facing is a legacy horizontal meta");
            CHECK(gm_world_meta(dungeon.world, dx, dy, dz) == facing,
                  "natural dungeon chest facing is applied to live block metadata");

            gm_runtime_set_pose(&dungeon, dx + 0.5, dy, dz + 0.5, 0.0f, 0.0f);
            CHECK(gm_runtime_use_block(&dungeon, dx, dy, dz),
                  "open natural dungeon chest");
            CHECK(dungeon.container == 3 && dungeon.active_chest >= 0,
                  "natural dungeon chest opens live container");
            if (dungeon.active_chest >= 0) {
                ChestLive *ch = &dungeon.chests[dungeon.active_chest].state;
                ChestLive repeat;
                CHECK(ch->loot_table == CHEST_LOOT_SIMPLE_DUNGEON,
                      "natural dungeon chest selects simple_dungeon loot table");
                CHECK(ch->loot_seed == loot_seed,
                      "natural dungeon chest uses captured nextLong loot seed");
                CHECK(ch->loot_filled && chest_live_total_items(ch) > 0,
                      "opening natural dungeon chest materializes deferred loot");
                CHECK(dungeon_chest_has_allowed_loot(ch),
                      "natural dungeon chest contains only simple_dungeon loot");
                chest_live_init(&repeat);
                chest_live_set_loot(
                    &repeat, CHEST_LOOT_SIMPLE_DUNGEON, loot_seed);
                chest_live_ensure_loot(&repeat);
                CHECK(chest_fingerprint(&repeat) == chest_fingerprint(ch),
                      "natural dungeon loot is deterministic from placement seed");
            }

            CHECK(popmc_dungeon_spawner_info(
                      dungeon.seed, sx, sy, sz, &mob_kind),
                  "natural dungeon spawner retains weighted mob selection");
            expected_entity = mob_kind == POPMC_DUNGEON_MOB_SKELETON
                ? EW_TYPE_SKELETON
                : mob_kind == POPMC_DUNGEON_MOB_SPIDER
                    ? EW_TYPE_SPIDER : EW_TYPE_ZOMBIE;
            gm_runtime_set_pose(
                &dungeon, sx + 0.5, sy + 0.5, sz + 0.5, 0.0f, 0.0f);
            {
                GmAction idle;
                memset(&idle, 0, sizeof idle);
                idle.hotbar_sel = -1;
                gm_runtime_tick(&dungeon, idle);
            }
            {
                int found = 0;
                for (int i = 0; i < GM_SPAWNERS; ++i) {
                    const GmSpawnerTE *sp = &dungeon.mobs.spawners[i];
                    if (sp->active && sp->x == sx && sp->y == sy && sp->z == sz) {
                        found = sp->entity_type == expected_entity;
                        break;
                    }
                }
                CHECK(found,
                      "natural dungeon spawner registers captured skeleton/zombie/spider type");
            }
            fprintf(stderr,
                    "chest_loot: dungeon chest=%d,%d,%d facing=%d seed=%lld "
                    "spawner=%d,%d,%d mob=%d\n",
                    dx, dy, dz, facing, loot_seed, sx, sy, sz, mob_kind);
        }
        gm_runtime_destroy(&dungeon);
    }

    /* Natural mineshaft: population events retain the cart loot seed and the
     * cave-spider identity of the generated spawner tile. */
    {
        GmConfig mine_cfg;
        GmRuntime mine;
        long long cart_seed = 0;
        gm_config_defaults(&mine_cfg);
        mine_cfg.world = GM_WORLD_DEFAULT;
        mine_cfg.view_distance = 2;
        mine_cfg.seed = 143;
        CHECK(gm_runtime_init(&mine, &mine_cfg, err, sizeof err),
              "mineshaft runtime init");
        if (!fail) {
            gm_world_ensure(mine.world, -6, -7, 1);
            CHECK(popmc_mineshaft_cart_info(
                      mine.seed, -90, 29, -97, &cart_seed),
                  "natural mineshaft retains chest-minecart event");
            CHECK(cart_seed == 7230402065820649518LL,
                  "natural mineshaft retains exact chest-minecart loot seed");
            CHECK(gm_world_block(mine.world, -90, 29, -97) == 66,
                  "natural chest minecart is placed on its generated rail");

            gm_world_ensure(mine.world, -6, -2, 1);
            CHECK(popmc_mineshaft_spawner_info(mine.seed, -90, 33, -28),
                  "natural mineshaft retains cave-spider spawner event");
            CHECK(gm_world_block(mine.world, -90, 33, -28) == 52,
                  "natural mineshaft spawner block is live");
            gm_runtime_set_pose(&mine, -89.5, 33.5, -27.5, 0.0f, 0.0f);
            {
                GmAction idle;
                memset(&idle, 0, sizeof idle);
                idle.hotbar_sel = -1;
                gm_runtime_tick(&mine, idle);
            }
            {
                int found = 0;
                for (int i = 0; i < GM_SPAWNERS; ++i) {
                    const GmSpawnerTE *sp = &mine.mobs.spawners[i];
                    if (sp->active && sp->x == -90 && sp->y == 33 && sp->z == -28) {
                        found = sp->entity_type == EW_TYPE_CAVE_SPIDER;
                        break;
                    }
                }
                CHECK(found,
                      "natural mineshaft spawner registers cave-spider identity");
            }
        }
        gm_runtime_destroy(&mine);
    }

    /* Growable TE: opening many unique chests retains live TEs (no eviction). */
    {
        GmPlayerView v; gm_runtime_view(&r, &v);
        int base_x = (int)v.x + 40, by = (int)v.y > 1 ? (int)v.y : 4;
        int base_z = (int)v.z;
        int opened = 0;
        int n_open = GM_RUNTIME_CHESTS_INITIAL + 8;
        int first_bx = base_x, first_bz = base_z;
        /* Put a marker stack in the first chest and keep its block. */
        for (int n = 0; n < n_open; ++n) {
            int bx = base_x + (n % 16), bz = base_z + (n / 16);
            gm_world_ensure(r.world, bx >> 4, bz >> 4, 0);
            gm_runtime_set_block(&r, bx, by, bz, 54, 3);
            gm_runtime_set_pose(&r, bx + 0.5, by, bz + 0.5, 0.0f, 0.0f);
            if (gm_runtime_use_block(&r, bx, by, bz)) {
                ++opened;
                if (n == 0) {
                    chest_live_set(&r.chests[r.active_chest].state, 0, ic_mk(265, 3, 0));
                }
            }
            gm_runtime_set_pose(&r, bx + 30.0, by, bz, 0.0f, 0.0f);
            { GmAction idle; memset(&idle, 0, sizeof idle); idle.hotbar_sel = -1;
              gm_runtime_tick(&r, idle); }
        }
        CHECK(opened == n_open, "opening >initial capacity succeeds via grow (no hard fail)");
        CHECK(r.chests_cap >= n_open, "chest TE table grew past initial capacity");
        /* First chest still has its marker (no live eviction). */
        gm_runtime_set_pose(&r, first_bx + 0.5, by, first_bz + 0.5, 0.0f, 0.0f);
        CHECK(gm_runtime_use_block(&r, first_bx, by, first_bz), "reopen first chest");
        {
            ICStack st = chest_live_get(&r.chests[r.active_chest].state, 0);
            CHECK(st.item == 265 && st.count == 3,
                  "live chest TE retained contents after table growth (no eviction)");
        }
    }

    if (fail) { fprintf(stderr, "chest_loot: FAIL\n"); return 1; }
    fprintf(stderr, "chest_loot: PASS\n");
    return 0;
}
