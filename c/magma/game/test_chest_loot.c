/* test_chest_loot: stronghold corridor/library chest positions get vanilla
 * table loot (not fabricated), deterministic across two fills of the same seed,
 * and player-placed chests stay empty until the player inserts items. */
#include "game/runtime.h"
#include "game/structures_live.h"
#include "game/chest_live.h"
#include "container_click.h"

#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } } while (0)

static int find_stronghold_chest(GmRuntime *r, int *ox, int *oy, int *oz)
{
    int sx, sz;
    if (!gm_stronghold_locate(r->seed, 0, &sx, &sz)) return 0;
    /* Scan a generous region around the first stronghold for block 54 that
     * map_gen placed (corridor / library). */
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
    }
    return h;
}

static int chest_has_allowed_loot(const ChestLive *c)
{
    /* Every non-empty slot must be an item from corridor or library tables. */
    static const int ok[] = {
        368, 264, 265, 266, 331, 297, 260, 257, 267, 307, 306, 308, 309,
        322, 329, 417, 418, 419, 340, 339, 395, 345, 263, 0
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

        /* re-seed a second ChestLive with the same table/seed and compare */
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
        /* close stronghold chest by walking away */
        gm_runtime_set_pose(&r, v.x + 30.0, v.y, v.z, 0.0f, 0.0f);
        { GmAction idle; memset(&idle, 0, sizeof idle); idle.hotbar_sel = -1;
          gm_runtime_tick(&r, idle); }
        gm_runtime_view(&r, &v);
        int bx = (int)v.x + 1, by = (int)v.y, bz = (int)v.z;
        if (by < 1) by = 1;
        CHECK(gm_runtime_set_block(&r, bx, by, bz, 54, 3), "place empty chest");
        CHECK(gm_world_block(r.world, bx, by, bz) == 54, "player chest block present");
        CHECK(gm_runtime_use_block(&r, bx, by, bz), "open player chest");
        CHECK(r.container == 3, "player chest open");
        {
            ChestLive *ch = &r.chests[r.active_chest].state;
            CHECK(chest_live_total_items(ch) == 0, "player chest starts empty");
            CHECK(ch->loot_table < 0 || ch->loot_filled, "no pending structure loot");
        }

        /* lid TE state advances while open (mesh does not animate - documented) */
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

    if (fail) { fprintf(stderr, "chest_loot: FAIL\n"); return 1; }
    fprintf(stderr, "chest_loot: PASS\n");
    return 0;
}
