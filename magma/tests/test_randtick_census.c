/* tests/test_randtick_census.c - the O(1) per-section random-tick census
 * (LChunk.rt_count, light.c) must answer EXACTLY what the 4096-cell scan in
 * randtick_live.h answered, for every section, under every parity setting.
 *
 * The scan is the contract:
 *
 *     for all 4096 cells: bp_is_randtick_id(gm_world_rt_block(w, ...))
 *
 * with gm_world_rt_block clipping to the configured parity AABB (reads outside
 * it as air). RT_SECTION_NEEDS gates the three updateLCG draws per section in
 * rt_live_pass, so ANY disagreement moves the LCG cursor and desyncs the world
 * from the Java oracle - a silent parity break the tapes need not exercise.
 *
 * Covered: parity_valid == 0 (interactive / no snapshot), sections fully
 * inside / fully outside / straddling a 16-unaligned parity AABB, and census
 * drift after randomized block edits.
 *
 * Build+run: make -C magma test-randtick-census
 */
#include "game/game.h"
#include "game/config.h"

#include "port_parity.h"

#include <stdio.h>
#include <stdlib.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (cond)
        fprintf(stderr, "OK: %s\n", msg);
    else {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails = 1;
    }
}

/* The pre-census default from blaze/core/randtick_live.h (rt_live_section_needs
 * with rt_live_id = gm_world_rt_block). Reference answer, by definition. */
static int scan_needs(const GmWorld *w, int cx, int sec, int cz) {
    int lx, ly, lz, base_y = sec * 16;
    for (lx = 0; lx < 16; ++lx)
        for (ly = 0; ly < 16; ++ly)
            for (lz = 0; lz < 16; ++lz)
                if (bp_is_randtick_id(gm_world_rt_block(w, cx * 16 + lx,
                                                        base_y + ly,
                                                        cz * 16 + lz)))
                    return 1;
    return 0;
}

#define CR 1 /* chunk radius swept */

/* Compare census vs scan over every section of the swept chunks. Returns the
 * number of disagreements and prints the first few. */
static int sweep(const GmWorld *w, const char *phase) {
    int cx, cz, sec, bad = 0;
    for (cx = -CR; cx <= CR; ++cx)
        for (cz = -CR; cz <= CR; ++cz)
            for (sec = 0; sec < 16; ++sec) {
                int want = scan_needs(w, cx, sec, cz);
                int got  = gm_world_section_needs_randtick(w, cx, sec, cz);
                if (!!want != !!got) {
                    if (bad < 8)
                        fprintf(stderr,
                                "  %s: chunk(%d,%d) sec %d: scan=%d census=%d\n",
                                phase, cx, cz, sec, want, got);
                    ++bad;
                }
            }
    return bad;
}

static unsigned rstate = 0x1234567u;
static unsigned rnd(void) {
    rstate ^= rstate << 13;
    rstate ^= rstate >> 17;
    rstate ^= rstate << 5;
    return rstate;
}

/* Mix of random-tickable (2 grass, 18 leaves, 59 wheat, 60 farmland, 78 snow,
 * 79 ice, 110 mycelium) and inert (0 air, 1 stone, 3 dirt, 12 sand, 20 glass)
 * ids, so both the increment and the decrement side of the census run. */
static const int PAL[] = { 0, 1, 2, 3, 12, 18, 20, 59, 60, 78, 79, 110 };
#define NPAL ((int)(sizeof PAL / sizeof PAL[0]))

static void random_edits(GmWorld *w, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        int wx = (int)(rnd() % (unsigned)(32 * CR + 16)) - (16 * CR + 8);
        int wz = (int)(rnd() % (unsigned)(32 * CR + 16)) - (16 * CR + 8);
        int wy = (int)(rnd() % 80u);
        gm_world_set_block_meta(w, wx, wy, wz, PAL[rnd() % NPAL], 0);
    }
}

int main(void) {
    GmWorld *w;
    int bad;

    /* ---- 0. default overworld: gen_chunk's from-scratch census over
     * populated/decorated chunks (grass, leaves, saplings from popmc). ---- */
    w = gm_world_create_type(10, GM_WORLD_DEFAULT);
    if (!w) { fprintf(stderr, "FAIL: overworld create\n"); return 1; }
    gm_world_ensure(w, 0, 0, CR);
    bad = sweep(w, "overworld/gen");
    expect(bad == 0, "overworld gen_chunk census matches scan");
    random_edits(w, 1500);
    bad = sweep(w, "overworld/edited");
    expect(bad == 0, "overworld census matches scan after 1500 random edits");
    gm_world_destroy(w);

    w = gm_world_create_type(1337, GM_WORLD_SUPERFLAT);
    if (!w) { fprintf(stderr, "FAIL: world create\n"); return 1; }
    gm_world_ensure(w, 0, 0, CR);

    /* ---- 1. no snapshot: parity_valid == 0, the full world ---------------
     * game.h: "parity_valid=0 is the full world (interactive / unit tests
     * without a snapshot)". The census must still gate empty sections. */
    bad = sweep(w, "fresh/no-parity");
    expect(bad == 0, "no-parity census matches scan on a fresh superflat world");

    random_edits(w, 3000);
    bad = sweep(w, "edited/no-parity");
    expect(bad == 0, "no-parity census matches scan after 3000 random edits");

    /* ---- 2. 16-aligned parity AABB: sections fully in / fully out -------- */
    expect(gm_world_parity_configure(w, -16, 32, -16, 32, 32, 32) == 1,
           "aligned parity AABB configured");
    bad = sweep(w, "aligned-parity");
    expect(bad == 0, "aligned-parity census matches scan (fully in / fully out)");

    random_edits(w, 3000);
    bad = sweep(w, "aligned-parity/edited");
    expect(bad == 0, "aligned-parity census matches scan after edits");

    /* ---- 3. unaligned parity AABB: straddling sections ------------------- */
    expect(gm_world_parity_configure(w, -13, 27, -5, 29, 41, 23) == 1,
           "unaligned parity AABB configured");
    bad = sweep(w, "unaligned-parity");
    expect(bad == 0, "unaligned-parity census matches scan (straddling sections)");

    random_edits(w, 3000);
    bad = sweep(w, "unaligned-parity/edited");
    expect(bad == 0, "unaligned-parity census matches scan after edits");

    /* ---- 4. targeted straddle: ticker present, but only OUTSIDE parity ---
     * Section (0,sec3,0) spans y 48..63. Parity covers y 48..55 only, x/z
     * 0..7. Grass at (9,60,9) is in the section, outside parity: the scan
     * says no, so the census path must too. */
    {
        int sec = 3, y;
        for (y = 48; y < 64; ++y) {
            int a, b;
            for (a = 0; a < 16; ++a)
                for (b = 0; b < 16; ++b)
                    gm_world_set_block_meta(w, a, y, b, 1, 0); /* stone */
        }
        expect(gm_world_parity_configure(w, 0, 48, 0, 8, 8, 8) == 1,
               "targeted parity AABB configured");
        gm_world_set_block_meta(w, 9, 60, 9, 2, 0); /* grass outside parity */
        expect(scan_needs(w, 0, sec, 0) == 0, "scan: outside-parity grass ignored");
        expect(gm_world_section_needs_randtick(w, 0, sec, 0) == 0,
               "census: outside-parity grass ignored");
        gm_world_set_block_meta(w, 3, 50, 3, 2, 0); /* grass inside parity */
        expect(scan_needs(w, 0, sec, 0) == 1, "scan: inside-parity grass counted");
        expect(gm_world_section_needs_randtick(w, 0, sec, 0) == 1,
               "census: inside-parity grass counted");
        gm_world_set_block_meta(w, 3, 50, 3, 1, 0); /* remove it again */
        expect(scan_needs(w, 0, sec, 0) == 0, "scan: census decrement observed");
        expect(gm_world_section_needs_randtick(w, 0, sec, 0) == 0,
               "census: decrement observed");
    }

    gm_world_destroy(w);
    fprintf(stderr, "test_randtick_census: %s\n", fails ? "FAIL" : "PASS");
    return fails;
}
