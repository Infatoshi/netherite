#include "structures.h"
#include "overworld_region.h"
#include "world/populate_mc.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int fail;
#define CHECK(c, m) do { if (!(c)) { fprintf(stderr, "FAIL: %s\n", m); fail = 1; } } while (0)

enum { FEATURE_DESERT, FEATURE_JUNGLE, FEATURE_SWAMP };

static int find_start(i64 seed, CpScratch *scratch, int feature,
                      int *out_cx, int *out_cz) {
    GLNode nodes[GL_MAX_NODES];
    int voronoi;
    gl_build(nodes, seed, &voronoi);
    int radius = feature == FEATURE_JUNGLE ? 64 : 16;
    for (int rx = -radius; rx <= radius; ++rx)
        for (int rz = -radius; rz <= radius; ++rz) {
            JavaRandom random;
            u64 mixed = (u64)seed + (u64)(i64)rx * 341873128712ULL
                                  + (u64)(i64)rz * 132897987541ULL + 14357617ULL;
            jrand_set(&random, (i64)mixed);
            int cx = rx * 32 + jrand_int_bound(&random, 24);
            int cz = rz * 32 + jrand_int_bound(&random, 24);
            scratch->arena.off = 0;
            int biome = gl_getInts(nodes, &scratch->arena, voronoi,
                                   cx * 16 + 8, cz * 16 + 8, 1, 1)[0];
            if (((feature == FEATURE_DESERT
                        && (biome == B_DESERT || biome == B_DESERT_HILLS))
                    || (feature == FEATURE_JUNGLE
                        && (biome == B_JUNGLE || biome == B_JUNGLE_HILLS))
                    || (feature == FEATURE_SWAMP && biome == B_SWAMP))
                    && owr_sd_is_candidate(seed, cx, cz)) {
                *out_cx = cx; *out_cz = cz;
                return 1;
            }
        }
    return 0;
}

int main(void) {
    const i64 seed = 0;
    CpScratch *scratch = (CpScratch *)calloc(1, sizeof *scratch);
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof *primer);
    McSinTable *sin_table = (McSinTable *)malloc(sizeof *sin_table);
    u16 *blocks = (u16 *)malloc(4 * 65536 * sizeof *blocks);
    u8 *meta = (u8 *)calloc(4 * 65536, sizeof *meta);
    int start_cx = 0, start_cz = 0;
    CHECK(scratch && primer && sin_table && blocks && meta, "scratch allocation");
    if (fail) return 1;
    mc_sin_table_init(sin_table);
    CHECK(find_start(seed, scratch, FEATURE_DESERT, &start_cx, &start_cz),
          "seed 0 desert pyramid within searched regions");
    if (fail) return 1;

    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz) {
            int ci = dx * 2 + dz;
            int cx = start_cx + dx, cz = start_cz + dz;
            st_run_features(primer, scratch, sin_table, seed, cx, cz, -1);
            memcpy(blocks + ci * 65536, primer->data, sizeof primer->data);
            popmc_decorate_chunk_meta(seed, cx, cz,
                blocks + ci * 65536, meta + ci * 65536);
        }

    int plate_y = -1, tnt_count = 0, chest_count = 0;
    for (int x = 0; x <= 20; ++x)
        for (int z = 0; z <= 20; ++z)
            for (int y = 1; y < 128; ++y) {
                int dx = x >> 4, dz = z >> 4;
                int ci = dx * 2 + dz;
                int index = ((x & 15) << 12) | ((z & 15) << 8) | y;
                int block = blocks[ci * 65536 + index];
                if (block == PB_STONE_PRESSURE_PLATE && x == 10 && z == 10)
                    plate_y = y;
                if (block == PB_TNT) ++tnt_count;
                if (block == PB_CHEST &&
                        popmc_desert_chest_info(seed, start_cx * 16 + x, y,
                            start_cz * 16 + z, NULL, NULL))
                    ++chest_count;
            }
    CHECK(plate_y > 0, "live pressure plate at pyramid center");
    CHECK(tnt_count == 9, "live trap has exact 3x3 TNT charge");
    CHECK(chest_count == 4, "live placement retains four deferred-loot chests");
    if (plate_y > 0) {
        int ci = 0;
        int index = (10 << 12) | (10 << 8) | (plate_y - 2);
        CHECK(blocks[ci * 65536 + index] == PB_TNT,
              "center TNT is two blocks below pressure plate");
    }

    CHECK(find_start(seed, scratch, FEATURE_JUNGLE, &start_cx, &start_cz),
          "seed 0 jungle pyramid within searched regions");
    memset(meta, 0, 4 * 65536 * sizeof *meta);
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz) {
            int ci = dx * 2 + dz;
            int cx = start_cx + dx, cz = start_cz + dz;
            st_run_features(primer, scratch, sin_table, seed, cx, cz, -1);
            memcpy(blocks + ci * 65536, primer->data, sizeof primer->data);
            popmc_decorate_chunk_meta(seed, cx, cz,
                blocks + ci * 65536, meta + ci * 65536);
        }
    int jungle_chests = 0, jungle_dispensers = 0;
    int tripwire_hooks = 0, tripwire = 0, sticky_pistons = 0, levers = 0;
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 1; y < 128; ++y) {
                int ci = (x >> 4) * 2 + (z >> 4);
                int index = ((x & 15) << 12) | ((z & 15) << 8) | y;
                int block = blocks[ci * 65536 + index];
                int id = pb_tag_id(block);
                int wx = start_cx * 16 + x, wz = start_cz * 16 + z;
                int facing = -1;
                if (block == PB_CHEST
                        && popmc_jungle_chest_info(
                            seed, wx, y, wz, NULL, &facing)) {
                    CHECK(meta[ci * 65536 + index] == facing,
                          "jungle chest facing metadata retained");
                    ++jungle_chests;
                }
                if (id == 23 && popmc_jungle_dispenser_info(
                            seed, wx, y, wz, NULL, &facing)) {
                    CHECK(meta[ci * 65536 + index] == facing,
                          "jungle dispenser facing metadata retained");
                    ++jungle_dispensers;
                }
                if (id == 131) ++tripwire_hooks;
                if (id == 132) ++tripwire;
                if (id == 29) ++sticky_pistons;
                if (id == 69) ++levers;
            }
    CHECK(jungle_chests == 2, "live jungle temple has two deferred-loot chests");
    CHECK(jungle_dispensers == 2,
          "live jungle temple has two deferred-loot dispensers");
    CHECK(tripwire_hooks == 4 && tripwire == 5,
          "live jungle temple has both complete tripwire traps");
    CHECK(sticky_pistons == 3 && levers == 3,
          "live jungle temple has hidden-room piston puzzle");

    CHECK(find_start(seed, scratch, FEATURE_SWAMP, &start_cx, &start_cz),
          "seed 0 swamp hut within searched regions");
    memset(meta, 0, 4 * 65536 * sizeof *meta);
    for (int dx = 0; dx < 2; ++dx)
        for (int dz = 0; dz < 2; ++dz) {
            int ci = dx * 2 + dz;
            int cx = start_cx + dx, cz = start_cz + dz;
            st_run_features(primer, scratch, sin_table, seed, cx, cz, -1);
            memcpy(blocks + ci * 65536, primer->data, sizeof primer->data);
            popmc_decorate_chunk_meta(seed, cx, cz,
                blocks + ci * 65536, meta + ci * 65536);
        }
    int swamp_pots = 0, swamp_witches = 0;
    int swamp_stairs = 0, swamp_crafting = 0, swamp_cauldrons = 0;
    for (int x = 0; x < 16; ++x)
        for (int z = 0; z < 16; ++z)
            for (int y = 1; y < 128; ++y) {
                int ci = (x >> 4) * 2 + (z >> 4);
                int index = ((x & 15) << 12) | ((z & 15) << 8) | y;
                int id = pb_tag_id(blocks[ci * 65536 + index]);
                int wx = start_cx * 16 + x, wz = start_cz * 16 + z;
                int item = 0, item_meta = -1;
                if (id == 140 && popmc_swamp_pot_info(
                            seed, wx, y, wz, &item, &item_meta)) {
                    CHECK(item == 0 && item_meta == 0,
                          "live swamp flower pot retains real empty TE payload");
                    ++swamp_pots;
                }
                if (popmc_swamp_witch_info(seed, wx, y, wz))
                    ++swamp_witches;
                if (id == 134) ++swamp_stairs;
                if (id == 58) ++swamp_crafting;
                if (id == 118) ++swamp_cauldrons;
            }
    CHECK(swamp_pots == 1, "live swamp hut has one populated flower pot");
    CHECK(swamp_witches == 1,
          "live swamp hut retains its one-time witch spawn site");
    CHECK(swamp_stairs == 26,
          "live swamp hut has exact spruce-stair roof perimeter");
    CHECK(swamp_crafting == 1 && swamp_cauldrons == 1,
          "live swamp hut has crafting table and cauldron interior");
    free(meta); free(blocks); free(sin_table); free(primer); free(scratch);
    if (fail) return 1;
    printf("scattered_live: PASS desert, jungle, and swamp; swamp start=(%d,%d) facing=%d\n",
           start_cx, start_cz, owr_sd_facing(seed, start_cx, start_cz));
    return 0;
}
