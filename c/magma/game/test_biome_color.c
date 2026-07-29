/* test_biome_color.c - bit-verify magma's REAL biome grass/foliage/water tint
 * (world/light.c cr_*_color_biome, colormap-driven) against a verbatim-Minecraft
 * golden produced by game/Golden.java (ColorizerGrass/Foliage + Biome subclass
 * overrides, reading the real 256x256 colormap PNGs via ImageIO).
 *
 * The GOLDEN table below is the exact stdout of:
 *   javac game/Golden.java && java Golden <minecraft-1.11.2.jar>
 * (columns: biomeId grass foliage water; grass=-1 => BiomeSwamp noise dither,
 * asserted separately). Regenerate with game/test_biome_color.sh, which reruns
 * Golden.java and diffs it against this table before running the C asserts.
 *
 * Build: game/test_biome_color.sh  (no game Makefile dependency).
 */
#include <stdio.h>
#include <string.h>
#include "world/light.h"

/* Verbatim stdout of game/Golden.java against minecraft-1.11.2.jar. */
static const struct { int id, grass, foliage, water; } GOLDEN[] = {
    {  0,  9353585,  7448397, 16777215 },  /* ocean            */
    {  1,  9551193,  7842607, 16777215 },  /* plains           */
    {  2, 12564309, 11445290, 16777215 },  /* desert           */
    {  3,  9090697,  7185259, 16777215 },  /* extreme_hills    */
    {  4,  7979098,  5877296, 16777215 },  /* forest           */
    {  5,  8828803,  6857828, 16777215 },  /* taiga            */
    {  6,       -1,  6975545, 14745518 },  /* swampland        */
    { 14,  5622079,  2865935, 16777215 },  /* mushroom_island  */
    { 21,  5884220,  3193611, 16777215 },  /* jungle           */
    { 27,  8960870,  7055680, 16777215 },  /* birch_forest     */
    { 29,  5274162,  5877296, 16777215 },  /* roofed_forest    */
    { 30,  8434839,  6332795, 16777215 },  /* cold_taiga       */
    { 35, 12564309, 11445290, 16777215 },  /* savanna          */
    { 37,  9470285, 10387789, 16777215 },  /* mesa             */
    {131,  9090697,  7185259, 16777215 },  /* mutated_ext_hills*/
    {133,  8828803,  6857828, 16777215 },  /* mutated_taiga    */
    {134,       -1,  6975545, 14745518 },  /* mutated_swampland*/
    {160,  8828803,  6857828, 16777215 },  /* mega_spruce_taiga*/
};
enum { NGOLD = (int)(sizeof(GOLDEN) / sizeof(GOLDEN[0])) };

int main(int argc, char **argv) {
    /* dump mode: print the C-computed colours in Golden.java's exact line format
     * so test_biome_color.sh can diff them against live Golden.java stdout (proves
     * the baked table below is current + the two decode paths agree). Swamp grass
     * is printed as -1 to line up with Golden.java's noise sentinel. */
    if (argc > 1 && strcmp(argv[1], "dump") == 0) {
        for (int t = 0; t < NGOLD; ++t) {
            int id = GOLDEN[t].id;
            int grass = (id == 6 || id == 134) ? -1 : cr_grass_color_biome(id, 0, 0, 0);
            printf("%d %d %d %d\n", id, grass,
                   cr_foliage_color_biome(id, 0, 0, 0), cr_water_color_biome(id));
        }
        return 0;
    }

    int fails = 0;

    for (int t = 0; t < NGOLD; ++t) {
        int id = GOLDEN[t].id;
        int foliage = cr_foliage_color_biome(id, 0, 0, 0);
        int water   = cr_water_color_biome(id);

        if (foliage != GOLDEN[t].foliage) {
            printf("FAIL biome %d foliage: C=0x%06X golden=0x%06X\n",
                   id, foliage, GOLDEN[t].foliage);
            ++fails;
        }
        if (water != GOLDEN[t].water) {
            printf("FAIL biome %d water:   C=0x%06X golden=0x%06X\n",
                   id, water, GOLDEN[t].water);
            ++fails;
        }

        if (GOLDEN[t].grass == -1) {
            /* BiomeSwamp: grass dithers on GRASS_COLOR_NOISE between the two
             * constants. Assert every sampled column lands on one of them, and
             * that both branches are actually reachable over a coordinate sweep. */
            int seen_lo = 0, seen_hi = 0, bad = 0;
            for (int wz = -256; wz < 256; wz += 7)
                for (int wx = -256; wx < 256; wx += 7) {
                    int g = cr_grass_color_biome(id, wx, 0, wz);
                    if (g == 5011004) seen_lo = 1;
                    else if (g == 6975545) seen_hi = 1;
                    else { bad = 1; }
                }
            if (bad) { printf("FAIL biome %d swamp grass: off-palette value\n", id); ++fails; }
            if (!seen_lo || !seen_hi) {
                printf("FAIL biome %d swamp grass: branches lo=%d hi=%d (want both)\n",
                       id, seen_lo, seen_hi); ++fails;
            }
        } else {
            int grass = cr_grass_color_biome(id, 0, 0, 0);
            if (grass != GOLDEN[t].grass) {
                printf("FAIL biome %d grass:   C=0x%06X golden=0x%06X\n",
                       id, grass, GOLDEN[t].grass);
                ++fails;
            }
        }
    }

    if (fails) {
        printf("BIOME COLOR: %d FAIL(s)\n", fails);
        return 1;
    }
    printf("BIOME COLOR: all %d biomes match verbatim-Java golden "
           "(grass/foliage colormap + swamp/roofed/mesa overrides + waterColor)\n",
           NGOLD);
    return 0;
}
