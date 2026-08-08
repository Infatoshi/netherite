#include "game/village_live.h"
#include "overworld_region.h"
#include "world/populate_mc.h"
#include "stronghold_loot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail;
#define CHECK(c,m) do { if (!(c)) { fprintf(stderr,"FAIL: %s\n",m); fail=1; } } while (0)

static int biome_type_at(i64 seed, CpScratch *scratch, int cx, int cz) {
    GLNode nodes[GL_MAX_NODES];
    int voronoi;
    gl_build(nodes, seed, &voronoi);
    scratch->arena.off = 0;
    int biome = gl_getInts(nodes, &scratch->arena, voronoi,
                           cx * 16 + 8, cz * 16 + 8, 1, 1)[0];
    if (biome == B_PLAINS) return GM_VILLAGE_PLAINS;
    if (biome == B_DESERT) return GM_VILLAGE_DESERT;
    if (biome == B_SAVANNA) return GM_VILLAGE_SAVANNA;
    if (biome == B_TAIGA) return GM_VILLAGE_TAIGA;
    return -1;
}

static int block_id(int value) {
    int tagged = pb_tag_id(value);
    if (tagged >= 0) return tagged;
    if (value == PB_AIR) return 0;
    if (value == PB_WATER || value == PB_FLOWING_WATER) return 9;
    if (value == PB_COBBLESTONE) return 4;
    if (value == PB_DIRT) return 3;
    if (value == PB_CHEST) return 54;
    return -1;
}

int main(void) {
    const i64 seed = 0;
    CpScratch *scratch = (CpScratch *)calloc(1, sizeof *scratch);
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof *primer);
    McSinTable *sin_table = (McSinTable *)malloc(sizeof *sin_table);
    enum { RADIUS = 3, DIAMETER = RADIUS * 2 + 1 };
    u16 *chunks = (u16 *)malloc(
        (size_t)DIAMETER * DIAMETER * 65536 * sizeof *chunks);
    int start_cx = 0, start_cz = 0, biome_type = -1;
    GmVillage graph;
    CHECK(scratch && primer && sin_table && chunks, "scratch allocation");
    if (fail) return 1;
    mc_sin_table_init(sin_table);
    for (int rx = -32; rx <= 32 && biome_type < 0; ++rx)
        for (int rz = -32; rz <= 32; ++rz) {
            int cx, cz;
            gm_village_candidate_for_region(seed, rx, rz, &cx, &cz);
            int type = biome_type_at(seed, scratch, cx, cz);
            if (type < 0 || !gm_village_build_for_world(
                    seed, cx, cz, type, 0, &graph)
                    || !graph.valid || graph.zombie_infested)
                continue;
            int has_blacksmith = 0;
            for (int i = 0; i < graph.count; ++i)
                if (graph.pieces[i].kind == GM_VILLAGE_HOUSE2)
                    has_blacksmith = 1;
            if (!has_blacksmith) continue;
            start_cx = cx; start_cz = cz; biome_type = type;
            break;
        }
    CHECK(biome_type >= 0, "seed 0 non-zombie sizeable village start");
    if (fail) return 1;

    popmc_set_villages(1);
    for (int dx = -RADIUS; dx <= RADIUS; ++dx)
        for (int dz = -RADIUS; dz <= RADIUS; ++dz) {
            int index = (dx + RADIUS) * DIAMETER + dz + RADIUS;
            int cx = start_cx + dx, cz = start_cz + dz;
            st_run_features(primer, scratch, sin_table, seed, cx, cz, -1);
            memcpy(chunks + (size_t)index * 65536,
                   primer->data, sizeof primer->data);
            popmc_decorate_chunk(seed, cx, cz,
                chunks + (size_t)index * 65536);
        }

    int paths = 0, farmland = 0, doors = 0, crops = 0, chests = 0;
    int planks = 0, village_water = 0;
    int chest_x = 0, chest_y = 0, chest_z = 0;
    for (int ci = 0; ci < DIAMETER * DIAMETER; ++ci)
        for (int i = 0; i < 65536; ++i) {
            int id = block_id(chunks[(size_t)ci * 65536 + i]);
            if (id == 208) ++paths;
            if (id == 60) ++farmland;
            if (id == 64 || id == 193 || id == 196) ++doors;
            if (id == 59 || id == 141 || id == 142 || id == 207) ++crops;
            if (id == 54) {
                int dx = ci / DIAMETER - RADIUS;
                int dz = ci % DIAMETER - RADIUS;
                int lx = (i >> 12) & 15;
                int lz = (i >> 8) & 15;
                ++chests;
                chest_x = (start_cx + dx) * 16 + lx;
                chest_y = i & 255;
                chest_z = (start_cz + dz) * 16 + lz;
            }
            if (id == 5 || id == 24) ++planks;
            if (id == 9) ++village_water;
        }
    fprintf(stderr, "village counts path=%d farmland=%d door=%d crop=%d planks=%d water=%d\n",
            paths, farmland, doors, crops, planks, village_water);
    CHECK(paths > 20, "live village roads materialized");
    CHECK(farmland >= 28 && crops >= 28,
          "live village farms and seeded crops materialized");
    CHECK(doors >= 2 && planks > 50,
          "live biome houses and doors materialized");
    CHECK(village_water > 0, "live village wells and irrigation materialized");
    CHECK(chests > 0, "live blacksmith chest materialized");
    if (chests > 0) {
        long long loot_seed = 0;
        int facing = -1;
        TeChest chest;
        CHECK(popmc_village_chest_info(seed, chest_x, chest_y, chest_z,
                                       &loot_seed, &facing),
              "blacksmith deferred loot site captured");
        memset(&chest, 0, sizeof chest);
        shl_fill_chest(&chest, SHL_VILLAGE_BLACKSMITH, loot_seed);
        int total = 0;
        for (int i = 0; i < TEC_SLOTS; ++i) total += chest.slots[i].count;
        CHECK(facing >= 2 && facing <= 5,
              "blacksmith chest facing metadata retained");
        CHECK(total > 0, "blacksmith loot table materializes inventory");
    }
    {
        PopmcVillageResident residents[128];
        int min_x = (start_cx - RADIUS) * 16;
        int min_z = (start_cz - RADIUS) * 16;
        int max_x = (start_cx + RADIUS + 1) * 16 - 1;
        int max_z = (start_cz + RADIUS + 1) * 16 - 1;
        int count = popmc_village_residents(
            seed, min_x, min_z, max_x, max_z, residents, 128);
        int blacksmiths = 0;
        CHECK(count > 0, "generated resident sites retained in live world");
        for (int i = 0; i < count; ++i) {
            CHECK(residents[i].y > 0 && residents[i].y < 256,
                  "resident site has a valid world height");
            CHECK(residents[i].profession >= 0 && residents[i].profession <= 4,
                  "normal village resident has a vanilla profession");
            CHECK(!residents[i].zombie_infested,
                  "selected normal village has no zombie residents");
            if (residents[i].profession == 3) ++blacksmiths;
        }
        CHECK(blacksmiths > 0,
              "blacksmith house retains its generated blacksmith resident");
    }

    long builds = popmc_window_builds();
    {
        int ci = RADIUS * DIAMETER + RADIUS;
        popmc_decorate_chunk(seed, start_cx, start_cz,
                            chunks + (size_t)ci * 65536);
    }
    CHECK(popmc_window_builds() == builds,
          "repeat village decoration reuses cached windows");

    popmc_set_villages(0);
    free(chunks); free(sin_table); free(primer); free(scratch);
    if (fail) return 1;
    printf("village_live: PASS seed=%lld start=(%d,%d) biome=%d pieces=%d "
           "paths=%d farmland=%d doors=%d crops=%d\n",
           (long long)seed, start_cx, start_cz, biome_type, graph.count,
           paths, farmland, doors, crops);
    return 0;
}
