/* game/test_entity_render.c - standalone verification for game/entity_render.c.
 *
 * (A) PART COUNTS: each modeled type emits 36 verts per vanilla model box
 *     (zombie 216 ... blaze 468, dragon 2340); unmodeled types keep the legacy
 *     36-vert marker box; NONE/PLAYER emit nothing.
 * (B) GEOMETRY: zombie AABB (1.0 wide across the arms, 2.0 tall, feet at y);
 *     yaw 90 swaps the model's X/Z extents; Y untouched by yaw.
 * (C) UVS: every emitted vertex UV falls inside its mob's skin rect(s) in the
 *     packed atlas (native skin-texel space, no full-sprite face wraps).
 * (D) WINDING: every quad's normal points away from its model's center
 *     (CCW-seen-from-outside, mesh_mc convention, no inside-out boxes).
 * (E) OVERFLOW: max below a model's vert count emits nothing for it and never
 *     overruns `out` (canary vertex intact).
 * (F) RENDER SMOKE: transform + raster a zombie+pig from a front camera,
 *     dump a PPM, assert non-background pixels exist.
 *
 * Build/run: bash game/test_entity_render.sh
 */
#include "core/types.h"
#include "game/entity_render.h"
#include "assets/mob_atlas.h"
#include "assets/blockmodels.h"
#include "game/block_registry.h"
#include "assets/atlas_gen.h"     /* CR_SPRITE_* names for the particle class audit */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (!(cond)) { printf("FAIL: %s\n", msg); g_fail = 1; } \
    else         { printf("ok:   %s\n", msg); } \
} while (0)

static int approx(float a, float b, float eps) { return fabsf(a - b) <= eps; }

/* Expected emitted vertices. Most models use 36 per vanilla box; living
 * renderers whose disabled GL culling is observable emit both windings. */
typedef struct { int type; const char *name; int verts; int sprites[2]; } TypeSpec;
static const TypeSpec SPECS[] = {
    { 2,  "zombie",   7  * 36, { CR_MOB_ZOMBIE,   -1 } },  /* + headwear */
    { 3,  "skeleton", 6  * 72, { CR_MOB_SKELETON, -1 } },
    { 4,  "creeper",  6  * 36, { CR_MOB_CREEPER,  -1 } },
    { 5,  "spider",   11 * 36, { CR_MOB_SPIDER,   -1 } },
    { 6,  "enderman", 7  * 36, { CR_MOB_ENDERMAN, -1 } },
    { 7,  "blaze",    13 * 36, { CR_MOB_BLAZE,    -1 } },
    { 10, "sheep",    12 * 36, { CR_MOB_SHEEP, CR_MOB_SHEEP_FUR } },
    { 11, "pig",      7  * 36, { CR_MOB_PIG,      -1 } },
    { 12, "cow",      9  * 36, { CR_MOB_COW,      -1 } },
    { 13, "chicken",  8  * 36, { CR_MOB_CHICKEN,  -1 } },
    { 16, "wolf",     11 * 36, { CR_MOB_WOLF,     -1 } },
    { 17, "ocelot",   11 * 36, { CR_MOB_OCELOT,   -1 } },
    { 23, "witch",    14 * 36, { CR_MOB_WITCH,    -1 } },
    { 24, "bat",      9  * 72, { CR_MOB_BAT,      -1 } },
    { 26, "ghast",    10 * 36, { CR_MOB_GHAST,    -1 } },
    { 27, "magma",    9  * 36, { CR_MOB_MAGMACUBE,-1 } },
    { 28, "minecart", 6  * 36, { CR_MOB_MINECART, -1 } },
    { 32, "wither skeleton", 6 * 72, { CR_MOB_WITHER_SKELETON, -1 } },
    { 15, "pigman",   7  * 36, { CR_MOB_PIGMAN,   -1 } },
    { 35, "slime",    4  * 36, { CR_MOB_SLIME,    -1 } },
    { 36, "silverfish", 3 * 36, { CR_MOB_SILVERFISH, -1 } },
    { 63, "endermite", 4 * 36, { CR_MOB_ENDERMITE, -1 } },
    { 65, "giant",    7 * 36, { CR_MOB_ZOMBIE, -1 } },
    { 37, "boat",     9  * 36, { CR_MOB_BOAT,     -1 } },
    { 39, "cave spider", 11 * 36, { CR_MOB_CAVE_SPIDER, -1 } },
    { 40, "villager farmer", 9 * 36, { CR_MOB_VILLAGER_FARMER, -1 } },
    { 41, "zombie villager farmer", 8 * 36,
      { CR_MOB_ZOMBIE_FARMER, -1 } },
    { 42, "shulker", 3 * 36, { CR_MOB_SHULKER, -1 } },
    { 43, "shulker bullet", 3 * 36, { CR_MOB_SHULKER_SPARK, -1 } },
    { 51, "vindicator", 9 * 36, { CR_MOB_VINDICATOR, -1 } },
    { 52, "evoker", 9 * 36, { CR_MOB_EVOKER, -1 } },
    { 53, "vex", 7 * 36, { CR_MOB_VEX, -1 } },
    { 54, "evoker fangs", 3 * 36, { CR_MOB_FANGS, -1 } },
    { 55, "guardian", 22 * 36, { CR_MOB_GUARDIAN, -1 } },
    { 56, "elder guardian", 22 * 36, { CR_MOB_GUARDIAN_ELDER, -1 } },
    { 57, "iron golem", 8 * 36, { CR_MOB_IRON_GOLEM, -1 } },
    { 61, "rabbit", 12 * 36, { CR_MOB_RABBIT_BROWN, -1 } },
    { 62, "polar bear", 10 * 36, { CR_MOB_POLAR_BEAR, -1 } },
    { 64, "snow golem", 5 * 72, { CR_MOB_SNOWMAN, -1 } },
    { 66, "wither",    9 * 36, { CR_MOB_WITHER, -1 } },
};
#define NSPECS ((int)(sizeof(SPECS) / sizeof(SPECS[0])))
#define MAXV (65 * 36)

static void bounds(const CrVertex *v, int n, float *mn, float *mx) {
    mn[0] = mn[1] = mn[2] = 1e30f;
    mx[0] = mx[1] = mx[2] = -1e30f;
    for (int i = 0; i < n; ++i) {
        float p[3] = { v[i].pos.x, v[i].pos.y, v[i].pos.z };
        for (int k = 0; k < 3; ++k) {
            if (p[k] < mn[k]) mn[k] = p[k];
            if (p[k] > mx[k]) mx[k] = p[k];
        }
    }
}

static int box_positions_differ(
        const CrVertex *a, const CrVertex *b, int box) {
    int start = box * 36;
    for (int i = start; i < start + 36; ++i)
        if (a[i].pos.x != b[i].pos.x || a[i].pos.y != b[i].pos.y
                || a[i].pos.z != b[i].pos.z)
            return 1;
    return 0;
}

static int vertex_positions_differ(
        const CrVertex *a, const CrVertex *b, int count) {
    for (int i = 0; i < count; ++i)
        if (a[i].pos.x != b[i].pos.x || a[i].pos.y != b[i].pos.y
                || a[i].pos.z != b[i].pos.z)
            return 1;
    return 0;
}

/* ------------------------------------------------------------------ */
static void test_part_counts(void) {
    printf("\n== (A) PART COUNTS ==\n");
    CrVertex out[MAXV];
    char msg[128];
    for (int s = 0; s < NSPECS; ++s) {
        GmEntityView e = { SPECS[s].type, 0, 64, 0, 0, 20 };
        int n = gm_entities_emit(&e, 1, out, MAXV);
        snprintf(msg, sizeof msg, "%s emits %d verts (%d boxes)",
                 SPECS[s].name, SPECS[s].verts, SPECS[s].verts / 36);
        CHECK(n == SPECS[s].verts, msg);
        CHECK(n % 3 == 0, "vert count is a triangle list");
    }
    /* NONE / PLAYER skipped */
    GmEntityView sk[2] = { {1, 0,0,0, 0,0}, {0, 0,0,0, 0,0} };
    int ns = gm_entities_emit(sk, 2, out, MAXV);
    CHECK(ns == 0, "PLAYER + NONE entities are skipped (0 verts)");
    /* XP orb (21): skipped by gm_entities_emit (not marker); billboard path. */
    GmEntityView mk; memset(&mk, 0, sizeof mk);
    mk.type = 21; mk.y = 64; mk.health = 5; mk.item_id = 5;
    CHECK(gm_entities_emit(&mk, 1, out, MAXV) == 0,
          "xp orb (21) is not the legacy marker box in gm_entities_emit");
    int xp_n = gm_xp_orbs_emit(&mk, 1, 0.0f, 0.0f, out, MAXV);
    CHECK(xp_n == 6, "xp orb emits a 6-vert camera-facing billboard");
    CHECK(xp_n != 36, "xp orb is not 36-vert marker geometry");
    /* value-tier UV: xpValue 1 -> tier 0; 2477 -> tier 10 (different U). */
    {
        GmEntityView a, b;
        memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
        a.type = 21; a.item_id = 1; a.y = 64;
        b.type = 21; b.item_id = 2477; b.y = 64;
        CrVertex va[6], vb[6];
        CHECK(gm_xp_orbs_emit(&a, 1, 0.0f, 0.0f, va, 6) == 6, "tier0 orb emits");
        CHECK(gm_xp_orbs_emit(&b, 1, 0.0f, 0.0f, vb, 6) == 6, "tier10 orb emits");
        CHECK(fabsf(va[0].uv.x - vb[0].uv.x) > 1e-5f ||
              fabsf(va[0].uv.y - vb[0].uv.y) > 1e-5f,
              "xp value tiers select different experience_orb UVs");
        /* colour phase: xpColor changes tint (not pure white marker). */
        a.item_meta = 0;
        b.item_meta = 40;
        gm_xp_orbs_emit(&a, 1, 0.0f, 0.0f, va, 6);
        gm_xp_orbs_emit(&b, 1, 0.0f, 0.0f, vb, 6);
        CHECK(va[0].tint.r != vb[0].tint.r || va[0].tint.b != vb[0].tint.b,
              "xpColor phase modulates orb vertex colour");
        CHECK(va[0].tint.a == 128, "RenderXPOrb vertex alpha is 128");
        gm_xp_orbs_emit(&a, 1, 0.0f, 25.0f, va, 6);
        CHECK(va[0].tint.r == 241 && va[0].tint.g == 255
                  && va[0].tint.b == 0,
              "XP partial-1 phase + pitch-25 standard lighting is exact");
    }
    /* Type 9 is the dedicated RenderDragon + ModelDragon transcription: five
     * neck segments, head/jaw, body, paired wings/legs, and 12 tail segments. */
    mk.type = 9; mk.item_id = 0;
    CHECK(gm_entities_emit(&mk, 1, out, MAXV) == 65 * 36,
          "dragon (9) emits the full 65-box ModelDragon (2340 verts)");
}

static void test_geometry(void) {
    printf("\n== (B) GEOMETRY ==\n");
    const float E = 1e-3f;
    CrVertex out[MAXV];

    /* zombie at (10,65,20) yaw 0: arms span x 10 +- 0.5625 (rot point 5 + box
     * 4 wide + ModelBox construction), head top at +2.0, feet at 65. */
    GmEntityView a = { 2, 10.0f, 65.0f, 20.0f, 0.0f, 20.0f };
    int n = gm_entities_emit(&a, 1, out, MAXV);
    float mn[3], mx[3];
    bounds(out, n, mn, mx);
    CHECK(approx(mx[0] - mn[0], 1.0f, E), "zombie yaw0 X extent 1.0 (arm to arm)");
    CHECK(approx(mn[1], 65.0f, E),        "zombie yaw0 minY == feet (legs reach ground)");
    CHECK(approx(mx[1], 67.03125f, E),    "zombie yaw0 maxY == feet + 2.03 (headwear top)");
    float zext = mx[2] - mn[2];

    /* yaw 90 -> X and Z extents swap, Y unchanged. */
    GmEntityView b = { 2, 10.0f, 65.0f, 20.0f, 90.0f, 20.0f };
    int nb = gm_entities_emit(&b, 1, out, MAXV);
    bounds(out, nb, mn, mx);
    CHECK(approx(mx[2] - mn[2], 1.0f, E), "zombie yaw90 Z extent == old X extent");
    CHECK(approx(mx[0] - mn[0], zext, E), "zombie yaw90 X extent == old Z extent");
    CHECK(approx(mn[1], 65.0f, E) && approx(mx[1], 67.03125f, E),
          "zombie yaw90 Y unchanged by yaw");

    GmEntityView giant = { 65, 10.0f, 65.0f, 20.0f, 0.0f, 100.0f };
    int ng = gm_entities_emit(&giant, 1, out, MAXV);
    bounds(out, ng, mn, mx);
    CHECK(ng == 7 * 36
              && approx(mx[0] - mn[0], 6.0f, 6.0f * E)
              && approx(mx[1], 65.0f + 12.1875f, 6.0f * E),
          "Giant reuses the zombie model at exact uniform scale 6");

    /* enderman is the tallest biped: head top = (24+22)/16 = 2.875 */
    GmEntityView em = { 6, 0.0f, 64.0f, 0.0f, 0.0f, 40.0f };
    int ne = gm_entities_emit(&em, 1, out, MAXV);
    bounds(out, ne, mn, mx);
    CHECK(approx(mx[1], 64.0f + 2.875f, 0.05f), "enderman head top ~2.875 above feet");

    /* pig is a low quadruped: top of back at (24-10)/16 = 0.875 */
    GmEntityView pg = { 11, 0.0f, 64.0f, 0.0f, 0.0f, 10.0f };
    int np = gm_entities_emit(&pg, 1, out, MAXV);
    bounds(out, np, mn, mx);
    CHECK(mx[1] < 64.0f + 1.05f, "pig stays under ~1 block tall");
    CHECK(approx(mn[1], 64.0f, E), "pig feet on the ground");

    /* RenderCreeper fuse: preRenderCallback swells X/Z faster than Y, while
     * odd flash phases pack the RenderLivingBase white-combiner strength. */
    {
        GmEntityView idle = {0}, primed = {0}, flashing = {0};
        idle.type = primed.type = flashing.type = 4;
        idle.y = primed.y = flashing.y = 64.0f;
        idle.health = primed.health = flashing.health = 20.0f;
        float i0[3], i1[3], p0[3], p1[3];
        int ni = gm_entities_emit(&idle, 1, out, MAXV);
        bounds(out, ni, i0, i1);
        primed.creeper_fuse = 25; /* non-flashing phase, strong swell */
        int npr = gm_entities_emit(&primed, 1, out, MAXV);
        bounds(out, npr, p0, p1);
        CHECK((p1[0] - p0[0]) > (i1[0] - i0[0]) * 1.15f,
              "primed creeper swells laterally");
        CHECK((p1[1] - p0[1]) > (i1[1] - i0[1]),
              "primed creeper applies the smaller vertical swell");

        flashing.creeper_fuse = 3; /* int(f*10) == 1 */
        int nf = gm_entities_emit(&flashing, 1, out, MAXV);
        CHECK(nf == ni && out[0].blk < 0.0f,
              "flashing creeper packs its white brightness combiner");
        CrRgba texel = {64, 64, 64, 255};
        CrTexture tex = {1, 1, &texel, 0, 0, {0}, {0}, {0}};
        CrShadeCtx sh = {0};
        sh.atlas = &tex;
        sh.layer = CR_LAYER_CUTOUT;
        sh.entity_brightness = 1;
        CrFragment frag = {0};
        frag.light = frag.ao = 1.0f;
        frag.tint = (CrRgba){255, 255, 255, 255};
        frag.blk = out[0].blk;
        CrRgba bright = cr_shade(&sh, &frag);
        CHECK(bright.r > 200 && bright.g == bright.r && bright.b == bright.r,
              "creeper flash interpolates the lit texture strongly toward white");

        CrRgba horse_texel = {222, 222, 207, 255};
        CrTexture horse_tex = {
            1, 1, &horse_texel, 0, 0, {0}, {0}, {0}
        };
        CrRgba lightmap[16 * 16];
        for (int i = 0; i < 16 * 16; ++i)
            lightmap[i] = (CrRgba){250, 250, 250, 255};
        sh.atlas = &horse_tex;
        sh.lightmap = lightmap;
        frag.light = 15.0F;
        frag.blk = 0.0F;
        frag.ao = 0.49638021F;
        CrRgba horse_lit = cr_shade(&sh, &frag);
        CHECK(horse_lit.r == 108 && horse_lit.g == 108
                  && horse_lit.b == 101,
              "entity fixed-function lightmap modulation keeps exact unorm8 bytes");
    }

    /* RenderCaveSpider reuses ModelSpider and applies a uniform 0.7 scale. */
    GmEntityView spider = { 5, 0.0f, 64.0f, 0.0f, 0.0f, 16.0f };
    int nsp = gm_entities_emit(&spider, 1, out, MAXV);
    bounds(out, nsp, mn, mx);
    float spider_ext[3] = { mx[0] - mn[0], mx[1] - mn[1], mx[2] - mn[2] };
    spider.skin = CR_MOB_CAVE_SPIDER + 1;
    int ncsp = gm_entities_emit(&spider, 1, out, MAXV);
    bounds(out, ncsp, mn, mx);
    CHECK(approx(mx[0] - mn[0], spider_ext[0] * 0.7f, E) &&
          approx(mx[1] - mn[1], spider_ext[1] * 0.7f, E) &&
          approx(mx[2] - mn[2], spider_ext[2] * 0.7f, E),
          "cave spider model is uniformly 0.7 scale");

    GmEntityView stand = {0};
    stand.type = 34;
    stand.health = 20.0f;
    CHECK(gm_entities_emit(&stand, 1, out, MAXV) == 8 * 36,
          "default armor stand hides arms and keeps base plate");
    stand.stand_flags = 1;
    CHECK(gm_entities_emit(&stand, 1, out, MAXV) == 10 * 36,
          "ShowArms armor stand emits both wooden arms");
    {
        float adult_head_min[3], adult_head_max[3];
        float adult_base_min[3], adult_base_max[3];
        float small_head_min[3], small_head_max[3];
        float small_base_min[3], small_base_max[3];
        bounds(out, 36, adult_head_min, adult_head_max);
        bounds(out + 9 * 36, 36, adult_base_min, adult_base_max);
        stand.stand_flags = 1 | 4;
        CHECK(gm_entities_emit(&stand, 1, out, MAXV) == 10 * 36,
              "Small armor stand retains all enabled wooden parts");
        bounds(out, 36, small_head_min, small_head_max);
        bounds(out + 9 * 36, 36, small_base_min, small_base_max);
        CHECK(approx(small_head_max[0] - small_head_min[0],
                     (adult_head_max[0] - adult_head_min[0]) * 0.75f, E)
              && approx(small_head_max[1] - small_head_min[1],
                        (adult_head_max[1] - adult_head_min[1]) * 0.75f, E),
              "Small armor-stand head uses ModelBiped child scale 0.75");
        CHECK(approx(small_base_max[0] - small_base_min[0],
                     (adult_base_max[0] - adult_base_min[0]) * 0.5f, E)
              && approx(small_base_max[1] - small_base_min[1],
                        (adult_base_max[1] - adult_base_min[1]) * 0.5f, E),
              "Small armor-stand body and base use child scale 0.5");
        stand.stand_flags = 1;
    }
    stand.armor_head = 306;
    stand.armor_chest = 307;
    stand.armor_legs = 308;
    stand.armor_feet = 309;
    int na = gm_entities_emit(&stand, 1, out, MAXV);
    CHECK(na == 20 * 36,
          "fully equipped armor stand emits vanilla base plus ten armor boxes");
    CHECK(out[10 * 36].uv.x >=
              (float)CR_MOB_SPRITES[CR_MOB_IRON_LAYER_1].x0 / CR_MOB_ATLAS_W &&
          out[10 * 36].uv.x <=
              (float)CR_MOB_SPRITES[CR_MOB_IRON_LAYER_1].x1 / CR_MOB_ATLAS_W,
          "armor stand chest samples iron layer-1 texture");
    {
        static const float defaults[6][3] = {
            { 0, 0, 0 }, { 0, 0, 0 }, { -10, 0, -10 },
            { -15, 0, 10 }, { -1, 0, -1 }, { 1, 0, 1 },
        };
        static const int wood_box[6] = { 0, 1, 3, 2, 5, 4 };
        static const int armor_box[6] = { 18, 10, 12, 11, 15, 14 };
        CrVertex posed[MAXV];
        memcpy(stand.stand_pose, defaults, sizeof defaults);
        stand.stand_pose_valid = 1;
        int nb = gm_entities_emit(&stand, 1, out, MAXV);
        memcpy(posed, out, (size_t)nb * sizeof *posed);
        CHECK(nb == na, "explicit default armor-stand pose preserves box count");
        for (int part = 0; part < 6; ++part) {
            memcpy(stand.stand_pose, defaults, sizeof defaults);
            stand.stand_pose[part][0] += 37.0f;
            int np = gm_entities_emit(&stand, 1, out, MAXV);
            CHECK(np == nb && box_positions_differ(
                      out, posed, wood_box[part]),
                  "each saved armor-stand pose rotates its wooden part");
            CHECK(np == nb && box_positions_differ(
                      out, posed, armor_box[part]),
                  "each saved armor-stand pose rotates its armor layer");
            CHECK(np == nb && !box_positions_differ(out, posed, 9),
                  "saved armor-stand pose never rotates the base plate");
        }
        memcpy(stand.stand_pose, defaults, sizeof defaults);
        stand.stand_punch_time_valid = 1;
        stand.stand_punch_time = 2.0f;
        CHECK(gm_entities_emit(&stand, 1, out, MAXV) == nb
                  && box_positions_differ(out, posed, 9),
              "recent armor-stand hit applies RenderArmorStand yaw wobble");
        stand.stand_punch_time = 5.0f;
        CHECK(gm_entities_emit(&stand, 1, out, MAXV) == nb
                  && !box_positions_differ(out, posed, 9),
              "armor-stand hit wobble ends at Java's five-tick boundary");
        stand.stand_punch_time_valid = 0;
    }
    stand.armor_head = 298;
    stand.armor_chest = 299;
    stand.armor_legs = 300;
    stand.armor_feet = 301;
    stand.armor_color_valid = 1 << 2;
    stand.armor_color[2] = 0x204080;
    int nl = gm_entities_emit(&stand, 1, out, MAXV);
    CHECK(nl == 30 * 36,
          "leather armor stand emits dyed base and overlay for every slot");
    CHECK(out[10 * 36].tint.r == 32 && out[10 * 36].tint.g == 64
              && out[10 * 36].tint.b == 128,
          "leather chest base uses its exact saved RGB dye");
    CHECK(out[13 * 36].tint.r == 255 && out[13 * 36].tint.g == 255
              && out[13 * 36].tint.b == 255,
          "leather chest overlay remains untinted");
    CHECK(out[13 * 36].uv.x >=
              (float)CR_MOB_SPRITES[CR_MOB_LEATHER_LAYER_1_OVERLAY].x0
                  / CR_MOB_ATLAS_W
              && out[13 * 36].uv.x <=
              (float)CR_MOB_SPRITES[CR_MOB_LEATHER_LAYER_1_OVERLAY].x1
                  / CR_MOB_ATLAS_W,
          "leather armor overlay samples the jar overlay texture");
    stand.armor_color_valid = 0;
    stand.armor_head = 302;
    stand.armor_chest = 315;
    stand.armor_legs = 304;
    stand.armor_feet = 317;
    CHECK(gm_entities_emit(&stand, 1, out, MAXV) == 20 * 36,
          "chainmail and gold armor materials emit all vanilla layers");

    stand.armor_head = stand.armor_legs = stand.armor_feet = 0;
    stand.armor_chest = 443;
    int nelytra = gm_entities_emit(&stand, 1, out, MAXV);
    CHECK(nelytra == 12 * 36,
          "elytra-equipped armor stand emits both ModelElytra wings");
    CHECK(out[10 * 36].uv.x >=
              (float)CR_MOB_SPRITES[CR_MOB_ELYTRA].x0 / CR_MOB_ATLAS_W &&
          out[10 * 36].uv.x <=
              (float)CR_MOB_SPRITES[CR_MOB_ELYTRA].x1 / CR_MOB_ATLAS_W,
          "armor-stand elytra samples the real entity elytra texture");
    stand.armor_chest = 0;
    stand.armor_head = 397;
    stand.armor_head_meta = 0;
    CHECK(gm_entities_emit(&stand, 1, out, MAXV) == 11 * 36,
          "skeleton skull head stack emits one custom-head cube");
    CHECK(out[10 * 36].uv.x >=
              (float)CR_MOB_SPRITES[CR_MOB_SKELETON].x0 / CR_MOB_ATLAS_W &&
          out[10 * 36].uv.x <=
              (float)CR_MOB_SPRITES[CR_MOB_SKELETON].x1 / CR_MOB_ATLAS_W,
          "skeleton skull samples the real skeleton skin");
    stand.armor_head_meta = 3;
    CHECK(gm_entities_emit(&stand, 1, out, MAXV) == 12 * 36,
          "player skull emits base skin and hat overlay custom-head cubes");
    CHECK(out[10 * 36].uv.x >=
              (float)CR_MOB_SPRITES[CR_MOB_STEVE].x0 / CR_MOB_ATLAS_W &&
          out[10 * 36].uv.x <=
              (float)CR_MOB_SPRITES[CR_MOB_STEVE].x1 / CR_MOB_ATLAS_W,
          "profile-free player skull samples DefaultPlayerSkin legacy Steve");
    stand.armor_head_meta = 5;
    CHECK(gm_entities_emit(&stand, 1, out, MAXV) == 17 * 36,
          "dragon head emits the seven ModelDragonHead boxes");
    CHECK(out[10 * 36].uv.x >=
              (float)CR_MOB_SPRITES[CR_MOB_DRAGON].x0 / CR_MOB_ATLAS_W &&
          out[10 * 36].uv.x <=
              (float)CR_MOB_SPRITES[CR_MOB_DRAGON].x1 / CR_MOB_ATLAS_W,
          "dragon head samples the real dragon texture");

    /* RenderCaveSpider.preRenderCallback scales the shared spider model 0.7. */
    {
        GmEntityView spider, cave;
        float smn[3], smx[3], cmn[3], cmx[3];
        memset(&spider, 0, sizeof spider);
        memset(&cave, 0, sizeof cave);
        spider.type = 5; spider.y = 64.0f; spider.health = 16.0f;
        cave.type = 39; cave.y = 64.0f; cave.health = 12.0f;
        n = gm_entities_emit(&spider, 1, out, MAXV);
        bounds(out, n, smn, smx);
        n = gm_entities_emit(&cave, 1, out, MAXV);
        bounds(out, n, cmn, cmx);
        CHECK(approx((cmx[0] - cmn[0]) / (smx[0] - smn[0]), 0.7f, E),
              "cave spider model is 0.7x ordinary spider");
    }
}

static void test_uvs(void) {
    printf("\n== (C) UVS ==\n");
    CrVertex out[MAXV];
    char msg[128];
    const float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
    for (int s = 0; s < NSPECS; ++s) {
        GmEntityView e = { SPECS[s].type, 0, 64, 0, 0, 20 };
        int n = gm_entities_emit(&e, 1, out, MAXV);
        int bad = 0;
        for (int i = 0; i < n; ++i) {
            float px = out[i].uv.x * aw, py = out[i].uv.y * ah;
            int inside = 0;
            for (int r = 0; r < 2 && SPECS[s].sprites[r] >= 0; ++r) {
                const CrMobSprite *sp = &CR_MOB_SPRITES[SPECS[s].sprites[r]];
                if (px >= (float)sp->x0 - 1e-3f && px <= (float)sp->x1 + 1e-3f &&
                    py >= (float)sp->y0 - 1e-3f && py <= (float)sp->y1 + 1e-3f)
                    inside = 1;
            }
            if (!inside) bad++;
        }
        snprintf(msg, sizeof msg, "%s: all %d UVs inside its skin rect(s)",
                 SPECS[s].name, n);
        CHECK(bad == 0, msg);
    }
    {
        static const int skins[6] = {
            CR_MOB_VILLAGER_FARMER, CR_MOB_VILLAGER_LIBRARIAN,
            CR_MOB_VILLAGER_PRIEST, CR_MOB_VILLAGER_SMITH,
            CR_MOB_VILLAGER_BUTCHER, CR_MOB_VILLAGER
        };
        for (int profession = 0; profession < 6; ++profession) {
            GmEntityView e = {0};
            e.type = 40; e.y = 64; e.health = 20;
            e.item_id = profession;
            int n = gm_entities_emit(&e, 1, out, MAXV);
            const CrMobSprite *sp = &CR_MOB_SPRITES[skins[profession]];
            int bad = n != 9 * 36;
            for (int i = 0; i < n; ++i) {
                float px = out[i].uv.x * aw, py = out[i].uv.y * ah;
                if (px < sp->x0 - 1e-3f || px > sp->x1 + 1e-3f
                        || py < sp->y0 - 1e-3f || py > sp->y1 + 1e-3f)
                    ++bad;
            }
            snprintf(msg, sizeof msg,
                     "villager profession %d uses its exact jar skin",
                     profession);
            CHECK(bad == 0, msg);
        }
    }
    {
        static const int skins[6] = {
            CR_MOB_ZOMBIE_FARMER, CR_MOB_ZOMBIE_LIBRARIAN,
            CR_MOB_ZOMBIE_PRIEST, CR_MOB_ZOMBIE_SMITH,
            CR_MOB_ZOMBIE_BUTCHER, CR_MOB_ZOMBIE_VILLAGER
        };
        for (int profession = 0; profession < 6; ++profession) {
            GmEntityView e = {0};
            e.type = 41; e.y = 64; e.health = 20;
            e.item_id = profession;
            int n = gm_entities_emit(&e, 1, out, MAXV);
            const CrMobSprite *sp = &CR_MOB_SPRITES[skins[profession]];
            int bad = n != 8 * 36;
            for (int i = 0; i < n; ++i) {
                float px = out[i].uv.x * aw, py = out[i].uv.y * ah;
                if (px < sp->x0 - 1e-3f || px > sp->x1 + 1e-3f
                        || py < sp->y0 - 1e-3f || py > sp->y1 + 1e-3f)
                    ++bad;
            }
            snprintf(msg, sizeof msg,
                     "zombie villager profession %d uses its exact jar skin",
                     profession);
            CHECK(bad == 0, msg);
        }
    }
}

static void test_winding(void) {
    printf("\n== (D) WINDING ==\n");
    CrVertex out[MAXV];
    char msg[128];
    for (int s = 0; s < NSPECS; ++s) {
        GmEntityView e = { SPECS[s].type, 0, 64, 0, 33.0f, 20 };
        int n = gm_entities_emit(&e, 1, out, MAXV);
        /* model center */
        float mn[3], mx[3];
        bounds(out, n, mn, mx);
        float cx = (mn[0] + mx[0]) * 0.5f, cy = (mn[1] + mx[1]) * 0.5f,
              cz = (mn[2] + mx[2]) * 0.5f;
        /* each box = 36 verts = 6 quads of (0,1,2)(0,2,3); per box use the BOX
         * center (average of its verts), then check every quad normal points
         * away from it. */
        int bad = 0;
        for (int b = 0; b < n; b += 36) {
            float bcx = 0, bcy = 0, bcz = 0;
            for (int i = 0; i < 36; ++i) {
                bcx += out[b+i].pos.x; bcy += out[b+i].pos.y; bcz += out[b+i].pos.z;
            }
            bcx /= 36; bcy /= 36; bcz /= 36;
            for (int q = 0; q < 6; ++q) {
                const CrVertex *t = &out[b + q * 6];
                float e1[3] = { t[1].pos.x - t[0].pos.x, t[1].pos.y - t[0].pos.y,
                                t[1].pos.z - t[0].pos.z };
                float e2[3] = { t[2].pos.x - t[0].pos.x, t[2].pos.y - t[0].pos.y,
                                t[2].pos.z - t[0].pos.z };
                float nx = e1[1]*e2[2] - e1[2]*e2[1];
                float ny = e1[2]*e2[0] - e1[0]*e2[2];
                float nz = e1[0]*e2[1] - e1[1]*e2[0];
                float qx = (t[0].pos.x + t[1].pos.x + t[2].pos.x) / 3 - bcx;
                float qy = (t[0].pos.y + t[1].pos.y + t[2].pos.y) / 3 - bcy;
                float qz = (t[0].pos.z + t[1].pos.z + t[2].pos.z) / 3 - bcz;
                if (nx*qx + ny*qy + nz*qz <= 0.0f) bad++;
            }
        }
        (void)cx; (void)cy; (void)cz;
        snprintf(msg, sizeof msg, "%s: all quads wound CCW-from-outside",
                 SPECS[s].name);
        CHECK(bad == 0, msg);
    }
}

static void test_overflow(void) {
    printf("\n== (E) OVERFLOW ==\n");
    CrVertex buf[253];
    const float CANARY = 12345.678f;
    memset(buf, 0, sizeof(buf));
    buf[252].pos.x = CANARY;

    GmEntityView z = { 2, 1,2,3, 0, 20 };   /* zombie: 252 verts (7 boxes) */

    int r0 = gm_entities_emit(&z, 1, buf, 251);
    CHECK(r0 == 0, "max=251 (< zombie 252): no partial model written");
    CHECK(buf[252].pos.x == CANARY, "canary past out[251] intact (no overflow)");

    int r1 = gm_entities_emit(&z, 1, buf, 10);
    CHECK(r1 == 0 && buf[252].pos.x == CANARY, "max=10: returns 0, canary intact");

    /* two zombies, room for exactly one. */
    GmEntityView two[2] = { {2,0,0,0,0,20}, {2,5,0,0,0,20} };
    int r2 = gm_entities_emit(two, 2, buf, 252);
    CHECK(r2 == 252 && buf[252].pos.x == CANARY,
          "two zombies, max=252: emits one full model, canary intact");
}

/* ------------------------------------------------------------------ */
static void look_at(CrVec3 p, CrVec3 t, float *yaw, float *pitch) {
    float dx = t.x - p.x, dy = t.y - p.y, dz = t.z - p.z;
    *yaw   = atan2f(dx, -dz);
    *pitch = atan2f(dy, sqrtf(dx * dx + dz * dz));
}

static void test_render(void) {
    printf("\n== (F) RENDER SMOKE ==\n");
    const int W = 256, H = 256;

    GmEntityView ents[2] = {
        { 2,  10.0f, 65.0f, 20.0f,  0.0f, 20.0f },   /* zombie */
        { 11, 12.0f, 65.0f, 20.0f, 45.0f, 10.0f },   /* pig */
    };
    CrVertex verts[2 * MAXV];
    int nv = gm_entities_emit(ents, 2, verts, 2 * MAXV);
    CHECK(nv == 252 + 252, "zombie + pig emit 504 verts");

    CrCamera cam = {0};
    cam.pos.x = 11.0f; cam.pos.y = 65.9f; cam.pos.z = 26.0f;
    CrVec3 tgt = { 11.0f, 65.9f, 20.0f };
    look_at(cam.pos, tgt, &cam.yaw, &cam.pitch);
    cam.fov_deg = 70.0f;
    cam.aspect  = (float)W / (float)H;
    cam.znear   = 0.05f;
    cam.zfar    = 256.0f;

    CrScreenTri tris[2 * MAXV];
    int nt = cr_transform(verts, nv, NULL, 0, &cam, W, H, tris, 2 * MAXV);
    CHECK(nt > 0, "transform produced screen triangles");

    CrFramebuffer fb;
    cr_fb_alloc(&fb, W, H);
    CrRgba bg = { 30, 30, 40, 255 };
    cr_fb_clear(&fb, bg);

    CrTexture atlas = gm_entity_atlas();
    /* Flat full-bright lightmap so light=15 / ao=face-shade entity verts
     * shade correctly (same contract as the game entity pass). */
    static CrRgba lm[256];
    for (int i = 0; i < 256; ++i) lm[i] = (CrRgba){255, 255, 255, 255};
    CrShadeCtx sh;
    memset(&sh, 0, sizeof(sh));
    sh.atlas = &atlas;
    sh.alpha_test = 1;
    sh.layer = CR_LAYER_CUTOUT;
    sh.lightmap = lm;
    cr_raster_cpu(&fb, tris, nt, &sh);

    long drawn = 0;
    for (int i = 0; i < W * H; ++i) {
        CrRgba c = fb.color[i];
        if (c.r != bg.r || c.g != bg.g || c.b != bg.b) drawn++;
    }
    printf("      drawn (non-bg) pixels: %ld / %d\n", drawn, W * H);
    CHECK(drawn > 500, "rasterized mobs produce non-background pixels");

    const char *path = "/tmp/magma_entities.ppm";
    FILE *f = fopen(path, "wb");
    if (f) {
        fprintf(f, "P6\n%d %d\n255\n", W, H);
        for (int i = 0; i < W * H; ++i) {
            unsigned char rgb[3] = { fb.color[i].r, fb.color[i].g, fb.color[i].b };
            fwrite(rgb, 1, 3, f);
        }
        fclose(f);
        printf("      wrote PPM: %s\n", path);
    }
    cr_fb_free(&fb);
}

/* (G) NAME MAPPING: tape type strings resolve to the modeled EW_TYPE_* ids;
 *     skin-variant bipeds fold onto their base model; no-model types -> -1. */
static void test_name_map(void) {
    printf("\n[G] tape type-string -> model id mapping\n");
    static const struct { const char *name; int type; } POS[] = {
        { "EntitySheep", 10 }, { "EntityZombie", 2 },
        { "EntityGiantZombie", 65 }, { "EntityCreeper", 4 },
        { "EntitySkeleton", 3 }, { "EntityChicken", 13 }, { "EntityCow", 12 },
        { "EntityPig", 11 }, { "EntitySpider", 5 }, { "EntityEnderman", 6 },
        { "EntityWolf", 16 }, { "EntityOcelot", 17 },
        { "EntityBlaze", 7 },
        { "EntityWitherSkeleton", 32 },
        { "EntitySmallFireball", 30 },
        { "EntityDragonFireball", 33 },
        { "EntityArmorStand", 34 },
        /* skin variants fold to the base silhouette */
        { "EntityHusk", 2 }, { "EntityZombieVillager", 41 },
        { "EntityPigZombie", 15 }, { "EntityStray", 3 },
        { "EntityCaveSpider", 39 }, { "EntityMooshroom", 12 },
        { "EntitySlime", 35 }, { "EntitySilverfish", 36 },
        { "EntityBoat", 37 },
        { "EntityVillager", 40 },
        { "EntityVindicator", 51 }, { "EntityEvoker", 52 },
        { "EntityGuardian", 55 }, { "EntityElderGuardian", 56 },
        { "EntityIronGolem", 57 },
        { "EntityWither", 66 }, { "EntityWitherSkull", 67 },
    };
    for (unsigned i = 0; i < sizeof POS / sizeof POS[0]; ++i) {
        char msg[96];
        snprintf(msg, sizeof msg, "%s -> %d", POS[i].name, POS[i].type);
        CHECK(gm_entity_type_for_name(POS[i].name) == POS[i].type, msg);
    }
    /* no model: skipped by callers, never the marker box */
    /* squid model added for tape-replay ocean frames */
    CHECK(gm_entity_type_for_name("EntitySquid") == 14, "EntitySquid -> 14");
    /* 2026-07-13 coverage pass: own models */
    CHECK(gm_entity_type_for_name("EntityWitch") == 23, "EntityWitch -> 23");
    CHECK(gm_entity_type_for_name("EntityBat") == 24, "EntityBat -> 24");
    CHECK(gm_entity_type_for_name("EntityLlama") == 25, "EntityLlama -> 25");
    CHECK(gm_entity_type_for_name("EntityLlamaSpit") == 73,
          "EntityLlamaSpit -> 73");
    CHECK(gm_entity_type_for_name("EntityGhast") == 26, "EntityGhast -> 26");
    CHECK(gm_entity_type_for_name("EntityMagmaCube") == 27, "EntityMagmaCube -> 27");
    CHECK(gm_entity_type_for_name("EntityMinecartEmpty") == 28,
          "EntityMinecartEmpty -> 28");
    CHECK(gm_entity_type_for_name("EntityMinecartChest") == 46,
          "EntityMinecartChest -> 46");
    CHECK(gm_entity_type_for_name("EntityMinecartFurnace") == 47,
          "EntityMinecartFurnace -> 47");
    CHECK(gm_entity_type_for_name("EntityMinecartHopper") == 48,
          "EntityMinecartHopper -> 48");
    CHECK(gm_entity_type_for_name("EntityMinecartTNT") == 49,
          "EntityMinecartTNT -> 49");
    CHECK(gm_entity_type_for_name("EntityShulker") == 42,
          "EntityShulker -> 42");
    CHECK(gm_entity_type_for_name("EntityShulkerBullet") == 43,
          "EntityShulkerBullet -> 43");
    CHECK(approx(gm_entity_eye_y(40), 1.62f, 1e-6f),
          "EntityVillager eye height is 1.62");
    CHECK(approx(gm_entity_eye_y(41), 1.74f, 1e-6f),
          "EntityZombieVillager eye height is 1.74");
    CHECK(approx(gm_entity_eye_y(51), 1.62f, 1e-6f)
              && approx(gm_entity_eye_y(52), 1.62f, 1e-6f),
          "illager eye heights are 1.62");
    CHECK(approx(gm_entity_eye_y(55), 0.425f, 1e-6f)
              && approx(gm_entity_eye_y(56), 0.99875f, 1e-6f),
          "guardian eye heights include Elder's 2.35 scale");
    CHECK(approx(gm_entity_eye_y(57), 2.295f, 1e-6f),
          "EntityIronGolem eye height is height times 0.85");
    CHECK(approx(gm_entity_eye_y(65), 10.440001f, 1e-6f),
          "EntityGiantZombie eye height is the exact override");
    /* skin-variant sprite overrides */
    CHECK(gm_entity_skin_for_name("EntityPigZombie") == CR_MOB_PIGMAN + 1,
          "EntityPigZombie skin -> pigman sprite");
    CHECK(gm_entity_skin_for_name("EntityZombie") == 0, "EntityZombie skin -> 0");
    CHECK(gm_entity_skin_for_name("EntityZombieVillager")
              == CR_MOB_ZOMBIE_VILLAGER + 1,
          "EntityZombieVillager skin -> zombie villager sprite");
    CHECK(gm_entity_skin_for_name("EntityVindicator")
              == CR_MOB_VINDICATOR + 1
              && gm_entity_skin_for_name("EntityEvoker")
                  == CR_MOB_EVOKER + 1,
          "illager names resolve owned skins");
    CHECK(gm_entity_skin_for_name("EntityGuardian")
              == CR_MOB_GUARDIAN + 1
              && gm_entity_skin_for_name("EntityElderGuardian")
                  == CR_MOB_GUARDIAN_ELDER + 1,
          "guardian names resolve ordinary and Elder skins");
    CHECK(gm_entity_type_for_name("EntityXPOrb") == 21,
          "EntityXPOrb -> 21 (RenderXPOrb billboard)");
    CHECK(gm_entity_type_for_name("EntityFallingBlock") == 38,
          "EntityFallingBlock -> 38 (RenderFallingBlock full cube)");
    CHECK(gm_entity_type_for_name("EntityTNTPrimed") == 44,
          "EntityTNTPrimed -> primed TNT block pass");
    static const char *NEG[] = { "EntityItem", "EntityNoSuchThing" };
    for (unsigned i = 0; i < sizeof NEG / sizeof NEG[0]; ++i) {
        char msg[96];
        snprintf(msg, sizeof msg, "%s -> -1 (no model)", NEG[i]);
        CHECK(gm_entity_type_for_name(NEG[i]) == -1, msg);
    }
    CHECK(gm_entity_type_for_name(NULL) == -1, "NULL -> -1");
    CHECK(gm_entity_billboard_item("EntitySmallFireball") == 385,
          "small fireball -> fire charge item id");
    CHECK(gm_entity_billboard_item("EntityDragonFireball") == 9003,
          "dragon fireball -> dedicated atlas sprite id");
}

static void test_wither_render(void) {
    printf("\n== WITHER RENDER ==\n");
    GmEntityView w;
    memset(&w, 0, sizeof w);
    w.type = 66;
    w.x = 3.0f; w.y = 64.0f; w.z = -2.0f;
    w.health = 300.0f;
    w.ticks_existed = 10;
    w.yaw = 20.0f;
    w.head_yaw = 35.0f;
    w.pitch = -8.0f;
    w.wither_head_yaw[0] = -25.0f;
    w.wither_head_yaw[1] = 70.0f;
    w.wither_head_pitch[0] = 12.0f;
    w.wither_head_pitch[1] = -18.0f;
    CrVertex base[9 * 36], scaled[9 * 36], blink[9 * 36];
    int n = gm_entities_emit(&w, 1, base, 9 * 36);
    CHECK(n == 9 * 36, "ModelWither emits its exact nine ModelRenderer boxes");
    CHECK(gm_entity_fullbright(w.type),
          "EntityWither getBrightnessForRender pins both lightmap halves");

    float bmn[3], bmx[3], smn[3], smx[3];
    bounds(base, n, bmn, bmx);
    w.wither_invul_time = 220;
    CHECK(gm_entities_emit(&w, 1, scaled, 9 * 36) == 9 * 36,
          "invulnerable Wither retains complete geometry");
    bounds(scaled, n, smn, smx);
    float expected = (2.0f - 219.0f / 220.0f * 0.5f) / 2.0f;
    CHECK(approx((smx[1] - smn[1]) / (bmx[1] - bmn[1]), expected, 2e-5f),
          "invulnerability pre-render scale follows RenderWither exactly");
    CHECK(!approx(base[0].uv.x, scaled[0].uv.x, 1e-7f)
              || !approx(base[0].uv.y, scaled[0].uv.y, 1e-7f),
          "high invulnerability time selects wither_invulnerable.png");
    w.wither_invul_time = 5;
    gm_entities_emit(&w, 1, blink, 9 * 36);
    CHECK(approx(base[0].uv.x, blink[0].uv.x, 1e-7f)
              && approx(base[0].uv.y, blink[0].uv.y, 1e-7f),
          "late invulnerability blink returns to the normal skin on Java phase");

    GmEntityView pose = w;
    pose.wither_invul_time = 0;
    pose.wither_head_yaw[0] += 50.0f;
    pose.wither_head_pitch[0] -= 25.0f;
    CrVertex moved[9 * 36];
    gm_entities_emit(&pose, 1, moved, 9 * 36);
    int side_changed = 0, body_stable = 1;
    for (int i = 36; i < 72; ++i)
        if (!approx(base[i].pos.x, moved[i].pos.x, 1e-6f)
                || !approx(base[i].pos.y, moved[i].pos.y, 1e-6f)
                || !approx(base[i].pos.z, moved[i].pos.z, 1e-6f))
            side_changed = 1;
    for (int i = 3 * 36; i < 9 * 36; ++i)
        if (!approx(base[i].pos.x, moved[i].pos.x, 1e-6f)
                || !approx(base[i].pos.y, moved[i].pos.y, 1e-6f)
                || !approx(base[i].pos.z, moved[i].pos.z, 1e-6f))
            body_stable = 0;
    CHECK(side_changed && body_stable,
          "each side-head pose is independent of the animated spine");

    w.wither_invul_time = 0;
    w.health = 150.0f;
    CrVertex aura[18 * 36];
    CHECK(gm_wither_aura_emit(&w, 1, aura, 18 * 36 - 1) == 0,
          "Wither armor layer never emits a partial model");
    int na = gm_wither_aura_emit(&w, 1, aura, 18 * 36);
    CHECK(na == 18 * 36,
          "armored half-health Wither emits the complete two-sided aura layer");
    int aura_style = 1, repeats = 0;
    for (int i = 0; i < na; ++i) {
        if (aura[i].tint.r != 128 || aura[i].tint.g != 128
                || aura[i].tint.b != 128 || aura[i].tint.a != 255
                || aura[i].light != 15.0f || aura[i].blk != 15.0f
                || aura[i].ao != 1.0f)
            aura_style = 0;
        if (aura[i].uv.x < 0.0f || aura[i].uv.x > 1.0f
                || aura[i].uv.y < 0.0f || aura[i].uv.y > 1.0f)
            repeats = 1;
    }
    CHECK(aura_style, "aura uses Java's half-gray fullbright additive inputs");
    CHECK(repeats, "aura carries animated repeating texture-matrix UVs");
    CrTexture armor = gm_wither_armor_texture();
    CHECK(armor.w == 64 && armor.h == 64 && armor.texels,
          "aura binds the standalone 64x64 wither_armor texture");
    w.health = 150.01f;
    CHECK(gm_wither_aura_emit(&w, 1, aura, 18 * 36) == 0,
          "aura is absent strictly above half health");

    GmEntityView skull;
    memset(&skull, 0, sizeof skull);
    skull.type = 67;
    skull.x = 9.0f; skull.y = 70.0f; skull.z = 4.0f;
    skull.yaw = 32.0f; skull.pitch = -17.0f;
    skull.lm_lit = 1; skull.lm_light = 7.0f; skull.lm_blk = 3.0f;
    CrVertex normal_skull[36], armored_skull[36];
    CHECK(gm_entities_emit(&skull, 1, normal_skull, 35) == 0,
          "Wither skull render respects atomic vertex capacity");
    CHECK(gm_entities_emit(&skull, 1, normal_skull, 36) == 36,
          "ModelSkeletonHead skull emits one exact box");
    skull.wither_skull_invulnerable = 1;
    CHECK(gm_entities_emit(&skull, 1, armored_skull, 36) == 36,
          "invulnerable Wither skull retains complete geometry");
    int skull_lit = 1, skull_skin_diff = 0;
    for (int i = 0; i < 36; ++i) {
        if (normal_skull[i].light != 7.0f || normal_skull[i].blk != 3.0f)
            skull_lit = 0;
        if (!approx(normal_skull[i].uv.x, armored_skull[i].uv.x, 1e-7f)
                || !approx(normal_skull[i].uv.y, armored_skull[i].uv.y, 1e-7f))
            skull_skin_diff = 1;
    }
    CHECK(skull_lit && !gm_entity_fullbright(67),
          "Wither skull renderer samples ordinary world light");
    CHECK(skull_skin_diff, "invulnerable skull selects its distinct Java skin");
}

static int uv_inside_mob_sprite(const CrVertex *v, int sprite) {
    const CrMobSprite *sp = &CR_MOB_SPRITES[sprite];
    float x = v->uv.x * (float)CR_MOB_ATLAS_W;
    float y = v->uv.y * (float)CR_MOB_ATLAS_H;
    return x >= (float)sp->x0 - 1e-3f && x <= (float)sp->x1 + 1e-3f
        && y >= (float)sp->y0 - 1e-3f && y <= (float)sp->y1 + 1e-3f;
}

static void test_skeleton_render(void) {
    printf("\n== SKELETON RENDER ==\n");
    GmEntityView s;
    memset(&s, 0, sizeof s);
    s.type = 3; s.x = 2.0f; s.y = 64.0f; s.z = -3.0f;
    s.tape_pose = 1; s.ticks_existed = 17;
    s.pitch = 25.0f; s.yaw = s.head_yaw = 0.0f;
    CrVertex idle[6 * 72], combat[6 * 72], armored[8 * 72];
    CHECK(gm_entities_emit(&s, 1, idle, 6 * 72) == 6 * 72,
          "skeleton base model emits six cull-disabled boxes");
    s.flags = 131072; /* AbstractSkeleton swinging-arms bow pose */
    CHECK(gm_entities_emit(&s, 1, combat, 6 * 72) == 6 * 72,
          "bow-and-arrow pose retains complete skeleton geometry");
    int arms_changed = 0, head_body_stable = 1;
    for (int i = 0; i < 6 * 72; ++i) {
        int changed = !approx(idle[i].pos.x, combat[i].pos.x, 1e-6f)
            || !approx(idle[i].pos.y, combat[i].pos.y, 1e-6f)
            || !approx(idle[i].pos.z, combat[i].pos.z, 1e-6f);
        if (i < 2 * 72 && changed) head_body_stable = 0;
        if (i >= 2 * 72 && i < 4 * 72 && changed) arms_changed = 1;
    }
    CHECK(head_body_stable && arms_changed,
          "bow-and-arrow state moves only the two ModelBiped arms");

    s.armor_head = 306; /* iron helmet, guaranteed on trap riders */
    CHECK(gm_entities_emit(&s, 1, armored, 8 * 72 - 1) == 0,
          "skeleton helmet layer capacity is atomic");
    int n = gm_entities_emit(&s, 1, armored, 8 * 72);
    CHECK(n == 8 * 72,
          "iron helmet emits armor head and headwear boxes over the skeleton");
    int helmet_uv = 1;
    for (int i = 6 * 72; i < n; ++i)
        if (!uv_inside_mob_sprite(&armored[i], CR_MOB_IRON_LAYER_1))
            helmet_uv = 0;
    CHECK(helmet_uv, "trap-rider helmet samples iron_layer_1.png");
}

static void test_snowman_render(void) {
    printf("\n== SNOW GOLEM RENDER ==\n");
    CHECK(gm_entity_type_for_name("EntitySnowman") == 64,
          "EntitySnowman maps to the native Snow Golem model");
    float width = 0.0f, height = 0.0f;
    gm_entity_size(64, &width, &height);
    CHECK(approx(width, 0.7f, 1e-7f) && approx(height, 1.9f, 1e-7f)
              && approx(gm_entity_eye_y(64), 1.7f, 1e-7f),
          "Snow Golem collision dimensions and eye height match Java");

    GmEntityView snowman;
    memset(&snowman, 0, sizeof snowman);
    snowman.type = 64;
    snowman.x = 2.0f; snowman.y = 64.0f; snowman.z = -3.0f;
    snowman.yaw = 17.0f; snowman.head_yaw = 38.0f;
    snowman.pitch = -14.0f; snowman.health = 4.0f;
    CrVertex bare[6 * 72], pumpkin[6 * 72], posed[6 * 72];
    CHECK(gm_entities_emit(&snowman, 1, bare, 5 * 72 - 1) == 0,
          "bare Snow Golem capacity is atomic");
    CHECK(gm_entities_emit(&snowman, 1, bare, 5 * 72) == 5 * 72,
          "bare Snow Golem emits five two-sided ModelSnowMan boxes");
    int base_uv = 1;
    for (int i = 0; i < 5 * 72; ++i)
        if (!uv_inside_mob_sprite(&bare[i], CR_MOB_SNOWMAN)) base_uv = 0;
    CHECK(base_uv, "Snow Golem body samples only textures/entity/snowman.png");

    snowman.flags = GM_ENTITY_FLAG_SNOWMAN_PUMPKIN;
    CHECK(gm_entities_emit(&snowman, 1, pumpkin, 6 * 72 - 1) == 0,
          "pumpkin-layer Snow Golem capacity is atomic");
    CHECK(gm_entities_emit(&snowman, 1, pumpkin, 6 * 72) == 6 * 72,
          "equipped pumpkin adds one two-sided block-model cube");
    int pumpkin_uv = 1;
    for (int i = 5 * 72; i < 6 * 72; ++i)
        if (!uv_inside_mob_sprite(
                &pumpkin[i], CR_MOB_SNOWMAN_PUMPKIN)) pumpkin_uv = 0;
    CHECK(pumpkin_uv,
          "Snow Golem head layer samples the generated pumpkin cube net");

    snowman.head_yaw = -41.0f;
    snowman.pitch = 21.0f;
    CHECK(gm_entities_emit(&snowman, 1, posed, 6 * 72) == 6 * 72
              && vertex_positions_differ(pumpkin, posed, 6 * 72),
          "head yaw and pitch drive head, pumpkin, body, and stick-arm pose");
}

static void test_endermite_render(void) {
    printf("\n== ENDERMITE RENDER ==\n");
    CHECK(gm_entity_type_for_name("EntityEndermite") == 63,
          "EntityEndermite maps to the native ModelEnderMite");
    float width = 0.0f, height = 0.0f;
    gm_entity_size(63, &width, &height);
    CHECK(approx(width, 0.4f, 1e-7f) && approx(height, 0.3f, 1e-7f)
              && approx(gm_entity_eye_y(63), 0.1f, 1e-7f),
          "Endermite dimensions and explicit eye height match Java");

    GmEntityView mite;
    memset(&mite, 0, sizeof mite);
    mite.type = 63;
    mite.x = 2.0f; mite.y = 64.0f; mite.z = -3.0f;
    mite.yaw = 17.0f; mite.health = 8.0f;
    mite.ticks_existed = 0;
    CrVertex first[4 * 36], aged[4 * 36];
    CHECK(gm_entities_emit(&mite, 1, first, 4 * 36 - 1) == 0,
          "Endermite model capacity is atomic");
    CHECK(gm_entities_emit(&mite, 1, first, 4 * 36) == 4 * 36,
          "Endermite emits four ModelEnderMite body boxes");
    int exact_uv = 1;
    for (int i = 0; i < 4 * 36; ++i)
        if (!uv_inside_mob_sprite(&first[i], CR_MOB_ENDERMITE)) exact_uv = 0;
    CHECK(exact_uv,
          "Endermite body samples only textures/entity/endermite.png");
    mite.ticks_existed = 9;
    CHECK(gm_entities_emit(&mite, 1, aged, 4 * 36) == 4 * 36
              && vertex_positions_differ(first, aged, 4 * 36),
          "ticksExisted drives all four exact body-wave hinges");
    mite.death_time = 20;
    CHECK(approx(er_death_roll(&mite), (float)M_PI, 1e-6f),
          "RenderEndermite death keel uses its 180-degree override");
}

static void test_polar_bear_render(void) {
    printf("\n== POLAR BEAR RENDER ==\n");
    CHECK(gm_entity_type_for_name("EntityPolarBear") == 62,
          "EntityPolarBear maps to the native ModelPolarBear");
    float width = 0.0f, height = 0.0f;
    gm_entity_size(62, &width, &height);
    CHECK(approx(width, 1.3f, 1e-7f) && approx(height, 1.4f, 1e-7f)
              && approx(gm_entity_eye_y(62), 1.19f, 1e-7f),
          "Polar Bear dimensions and eye height match Java");

    GmEntityView bear;
    memset(&bear, 0, sizeof bear);
    bear.type = 62;
    bear.y = 64.0f;
    bear.health = 30.0f;
    bear.head_yaw = bear.yaw = 10.0f;
    CrVertex idle[10 * 36], standing[10 * 36], child[10 * 36];
    CHECK(gm_entities_emit(&bear, 1, idle, 10 * 36 - 1) == 0,
          "Polar Bear model capacity is atomic");
    CHECK(gm_entities_emit(&bear, 1, idle, 10 * 36) == 10 * 36,
          "Polar Bear emits ten flattened ModelPolarBear boxes");
    int exact_uv = 1;
    for (int i = 0; i < 10 * 36; ++i)
        if (!uv_inside_mob_sprite(&idle[i], CR_MOB_POLAR_BEAR)) exact_uv = 0;
    CHECK(exact_uv,
          "Polar Bear samples only textures/entity/bear/polarbear.png");

    bear.anim_time = 1.0f;
    CHECK(gm_entities_emit(&bear, 1, standing, 10 * 36) == 10 * 36
              && vertex_positions_differ(idle, standing, 10 * 36),
          "standing scale drives torso, forelegs, and head pose");
    bear.anim_time = 0.0f;
    bear.flags = 8;
    CHECK(gm_entities_emit(&bear, 1, child, 10 * 36) == 10 * 36
              && vertex_positions_differ(idle, child, 10 * 36),
          "child path applies the exact split head and body scales");
}

static void test_rabbit_render(void) {
    printf("\n== RABBIT RENDER ==\n");
    CHECK(gm_entity_type_for_name("EntityRabbit") == 61,
          "EntityRabbit maps to the native ModelRabbit");
    float width = 0.0f, height = 0.0f;
    gm_entity_size(61, &width, &height);
    CHECK(approx(width, 0.4f, 1e-7f) && approx(height, 0.5f, 1e-7f)
              && approx(gm_entity_eye_y(61), 0.425f, 1e-7f),
          "Rabbit dimensions and eye height match Java");

    GmEntityView rabbit;
    memset(&rabbit, 0, sizeof rabbit);
    rabbit.type = 61;
    rabbit.y = 64.0f;
    rabbit.health = 3.0f;
    rabbit.head_yaw = rabbit.yaw = 10.0f;
    CrVertex idle[12 * 36], jumping[12 * 36], child[12 * 36];
    CHECK(gm_entities_emit(&rabbit, 1, idle, 12 * 36 - 1) == 0,
          "Rabbit model capacity is atomic");
    CHECK(gm_entities_emit(&rabbit, 1, idle, 12 * 36) == 12 * 36,
          "Rabbit emits twelve flattened ModelRabbit boxes");
    int exact_uv = 1;
    for (int i = 0; i < 12 * 36; ++i)
        if (!uv_inside_mob_sprite(&idle[i], CR_MOB_RABBIT_BROWN)) exact_uv = 0;
    CHECK(exact_uv, "Rabbit type zero samples the brown coat");

    rabbit.anim_time = 0.5f;
    CHECK(gm_entities_emit(&rabbit, 1, jumping, 12 * 36) == 12 * 36
              && vertex_positions_differ(idle, jumping, 12 * 36),
          "jump completion drives feet, thighs, and forelegs");
    rabbit.item_meta = 99;
    CHECK(gm_entities_emit(&rabbit, 1, jumping, 12 * 36) == 12 * 36
              && uv_inside_mob_sprite(
                  &jumping[0], CR_MOB_RABBIT_CAERBANNOG),
          "type 99 selects the Killer Bunny coat");
    rabbit.item_meta = 0;
    rabbit.anim_time = 0.0f;
    rabbit.flags = 8;
    CHECK(gm_entities_emit(&rabbit, 1, child, 12 * 36) == 12 * 36
              && vertex_positions_differ(idle, child, 12 * 36),
          "child path applies split head and body scales");
}

static void test_bat_render(void) {
    printf("\n== BAT RENDER ==\n");
    GmEntityView bat;
    memset(&bat, 0, sizeof bat);
    bat.type = 24;
    bat.x = 2.0f; bat.y = 64.0f; bat.z = -3.0f;
    bat.yaw = bat.head_yaw = 37.0f;
    bat.health = 6.0f;
    bat.ticks_existed = 40;
    CrVertex flying[9 * 72], aged[9 * 72], hanging[9 * 72];
    CrVertex flying_after_hanging[9 * 72];
    CHECK(gm_entities_emit(&bat, 1, flying, 9 * 72 - 1) == 0,
          "Bat cull-disabled model capacity is atomic");
    int count = gm_entities_emit(&bat, 1, flying, 9 * 72);
    CHECK(count == 9 * 72,
          "flying Bat emits nine two-sided ModelBat boxes");
    bat.ticks_existed = 41;
    CHECK(gm_entities_emit(&bat, 1, aged, 9 * 72) == 9 * 72
              && vertex_positions_differ(flying, aged, 9 * 72),
          "client ticksExisted advances Bat body bob and wing phase");
    bat.ticks_existed = 40;
    bat.flags = GM_ENTITY_FLAG_BAT_HANGING;
    CHECK(gm_entities_emit(&bat, 1, hanging, 9 * 72) == 9 * 72
              && vertex_positions_differ(flying, hanging, 9 * 72),
          "hanging bit selects the ceiling pose and folded hierarchy");
    bat.flags = 0;
    CHECK(gm_entities_emit(
              &bat, 1, flying_after_hanging, 9 * 72) == 9 * 72
              && vertex_positions_differ(
                  flying, flying_after_hanging, 9 * 72),
          "shared ModelBat retains vanilla wing-X state after hanging render");
    int bat_uv = 1;
    for (int i = 0; i < 9 * 72; ++i)
        if (!uv_inside_mob_sprite(&hanging[i], CR_MOB_BAT)) bat_uv = 0;
    CHECK(bat_uv, "both Bat poses sample only textures/entity/bat.png");
}

static void test_squid_render(void) {
    printf("\n== SQUID RENDER ==\n");
    GmEntityView squid;
    memset(&squid, 0, sizeof squid);
    squid.type = 14;
    squid.x = 2.0f; squid.y = 64.0f; squid.z = -3.0f;
    squid.yaw = 37.0f;
    squid.pitch = 28.0f;
    squid.head_yaw = -19.0f;
    squid.anim_time = 0.72f;
    squid.health = 10.0f;
    CrVertex posed[9 * 72], tentacles[9 * 72], rotated[9 * 72];
    CHECK(gm_entities_emit(&squid, 1, posed, 9 * 72 - 1) == 0,
          "Squid cull-disabled model capacity is atomic");
    int count = gm_entities_emit(&squid, 1, posed, 9 * 72);
    CHECK(count == 9 * 72,
          "Squid emits nine two-sided ModelSquid boxes");
    squid.anim_time = 0.24f;
    CHECK(gm_entities_emit(&squid, 1, tentacles, 9 * 72) == 9 * 72
              && vertex_positions_differ(posed, tentacles, 9 * 72),
          "tentacleAngle drives all eight live tentacle hinges");
    squid.anim_time = 0.72f;
    squid.pitch = -63.0f;
    squid.head_yaw = 11.0f;
    CHECK(gm_entities_emit(&squid, 1, rotated, 9 * 72) == 9 * 72
              && vertex_positions_differ(posed, rotated, 9 * 72),
          "squidPitch and squidYaw drive RenderSquid's whole-model pose");
    int squid_uv = 1;
    for (int i = 0; i < 9 * 72; ++i)
        if (!uv_inside_mob_sprite(&posed[i], CR_MOB_SQUID)) squid_uv = 0;
    CHECK(squid_uv, "Squid body and tentacles sample only squid.png");
}

static void test_horse_render(void) {
    printf("\n== HORSE FAMILY RENDER ==\n");
    CHECK(gm_entity_type_for_name("EntityHorse") == 68
          && gm_entity_type_for_name("EntityDonkey") == 69
          && gm_entity_type_for_name("EntityMule") == 70
          && gm_entity_type_for_name("EntitySkeletonHorse") == 71
          && gm_entity_type_for_name("EntityZombieHorse") == 72,
          "all five AbstractHorse render classes have native type ids");
    float width = 0.0f, height = 0.0f;
    gm_entity_size(68, &width, &height);
    CHECK(approx(width, 1.3964844f, 1e-7f) && approx(height, 1.6f, 1e-7f)
          && approx(gm_entity_eye_y(68), 1.36f, 1e-6f),
          "horse collision dimensions and eye sample match AbstractHorse");

    GmEntityView h;
    memset(&h, 0, sizeof h);
    h.type = 68; h.x = 2.0f; h.y = 64.0f; h.z = -3.0f;
    h.health = 20.0f; h.tape_pose = 1;
    h.yaw = 20.0f; h.head_yaw = 35.0f; h.pitch = -8.0f;
    h.item_id = 6 | (4 << 8); h.item_meta = 3;
    h.lm_lit = 1; h.lm_light = 8.0f; h.lm_blk = 5.0f;
    CrVertex base[37 * 72], pose[37 * 72];
    CHECK(gm_entities_emit(&h, 1, base, 23 * 72 - 1) == 0,
          "horse model capacity is atomic");
    int n = gm_entities_emit(&h, 1, base, 23 * 72);
    CHECK(n == 23 * 72,
          "idle horse emits cull-disabled ModelHorse geometry");
    int exact_skin = 1, exact_light = 1;
    int expected_skin = CR_MOB_HORSE_6_4_3;
    for (int i = 0; i < n; ++i) {
        if (!uv_inside_mob_sprite(&base[i], expected_skin)) exact_skin = 0;
        if (base[i].light != 8.0f || base[i].blk != 5.0f) exact_light = 0;
    }
    CHECK(exact_skin,
          "horse variant selects exact color, marking and armor composite");
    CHECK(exact_light, "horse model retains sampled world lightmap levels");

    h.graze_y = 0.7f; h.swing_progress = 0.4f; h.squish = 0.8f;
    h.limb_swing = 3.0f; h.limb_swing_amount = 0.65f;
    h.ticks_existed = 41; h.flags = 65536;
    CHECK(gm_entities_emit(&h, 1, pose, 23 * 72) == 23 * 72,
          "eating, rearing, mouth, gait and tail poses retain complete geometry");
    int changed = 0;
    for (int i = 0; i < n; ++i)
        if (!approx(base[i].pos.x, pose[i].pos.x, 1e-6f)
                || !approx(base[i].pos.y, pose[i].pos.y, 1e-6f)
                || !approx(base[i].pos.z, pose[i].pos.z, 1e-6f)) {
            changed = 1;
            break;
        }
    CHECK(changed, "ModelHorse live animation state changes emitted geometry");

    h.flags = 16384;
    CHECK(gm_entities_emit(&h, 1, pose, 33 * 72) == 33 * 72,
          "saddled horse emits ten saddle and bridle boxes");
    h.flags |= 32768;
    CHECK(gm_entities_emit(&h, 1, pose, 35 * 72) == 35 * 72,
          "ridden saddled horse emits both reins");

    h.type = 69; h.flags = 8192 | 16384 | 32768;
    n = gm_entities_emit(&h, 1, pose, 37 * 72);
    CHECK(n == 37 * 72,
          "chested ridden donkey emits ears, panniers, saddle and reins");
    int donkey_skin = 1;
    for (int i = 0; i < n; ++i)
        if (!uv_inside_mob_sprite(&pose[i], CR_MOB_DONKEY)) donkey_skin = 0;
    CHECK(donkey_skin, "donkey model uses its fixed Java skin");

    h.flags |= 8;
    n = gm_entities_emit(&h, 1, pose, 23 * 72);
    CHECK(n == 23 * 72,
          "child donkey suppresses saddle, reins and panniers exactly as Java");
    float adult_min[3], adult_max[3], child_min[3], child_max[3];
    h.flags &= ~8;
    gm_entities_emit(&h, 1, base, 37 * 72);
    bounds(base, 37 * 72, adult_min, adult_max);
    h.flags |= 8;
    gm_entities_emit(&h, 1, pose, 23 * 72);
    bounds(pose, 23 * 72, child_min, child_max);
    CHECK(child_max[1] - child_min[1] < adult_max[1] - adult_min[1],
          "child ModelHorse three-group transforms reduce visible height");

    static const int fixed_type[4] = {69, 70, 71, 72};
    static const int fixed_skin[4] = {
        CR_MOB_DONKEY, CR_MOB_MULE,
        CR_MOB_HORSE_SKELETON, CR_MOB_HORSE_ZOMBIE
    };
    for (int k = 0; k < 4; ++k) {
        h.type = fixed_type[k]; h.flags = 0;
        n = gm_entities_emit(&h, 1, pose, 23 * 72);
        int good = n == 23 * 72;
        for (int i = 0; i < n; ++i)
            if (!uv_inside_mob_sprite(&pose[i], fixed_skin[k])) good = 0;
        CHECK(good, "non-horse equid emits complete model with fixed skin");
    }
}

static void test_llama_render(void) {
    printf("\n== LLAMA FAMILY RENDER ==\n");
    GmEntityView llama;
    memset(&llama, 0, sizeof llama);
    llama.type = 25; llama.x = 2.0f; llama.y = 64.0f; llama.z = -3.0f;
    llama.health = 15.0f; llama.tape_pose = 1;
    llama.yaw = 20.0f; llama.head_yaw = 35.0f; llama.pitch = -8.0f;
    llama.limb_swing = 3.0f; llama.limb_swing_amount = 0.65f;
    CrVertex out[MAXV], child[MAXV];

    CHECK(gm_entities_emit(&llama, 1, out, 9 * 72 - 1) == 0,
          "llama model capacity is atomic");
    int n = gm_entities_emit(&llama, 1, out, MAXV);
    CHECK(n == 9 * 72,
          "adult llama emits nine cull-disabled ModelLlama boxes");

    static const int coats[4] = {
        CR_MOB_LLAMA, CR_MOB_LLAMA_WHITE,
        CR_MOB_LLAMA_BROWN, CR_MOB_LLAMA_GRAY
    };
    for (int variant = 0; variant < 4; ++variant) {
        llama.item_id = variant;
        n = gm_entities_emit(&llama, 1, out, MAXV);
        int good = n == 9 * 72;
        for (int i = 0; i < n; ++i)
            if (!uv_inside_mob_sprite(&out[i], coats[variant])) good = 0;
        CHECK(good, "llama variant selects its exact coat texture");
    }

    llama.flags = 8192;
    n = gm_entities_emit(&llama, 1, out, MAXV);
    CHECK(n == 11 * 72,
          "adult chested llama emits both pannier boxes");
    llama.item_meta = 16;
    n = gm_entities_emit(&llama, 1, out, MAXV);
    CHECK(n == 22 * 72,
          "decorated chested llama emits inflated base-and-decor models");
    int coat_good = 1, decor_good = 1;
    for (int i = 0; i < 11 * 72; ++i)
        if (!uv_inside_mob_sprite(&out[i], CR_MOB_LLAMA_GRAY)) coat_good = 0;
    for (int i = 11 * 72; i < n; ++i)
        if (!uv_inside_mob_sprite(&out[i], CR_MOB_LLAMA_DECOR_BLACK))
            decor_good = 0;
    CHECK(coat_good && decor_good,
          "coat and black carpet layer retain separate exact atlas textures");

    float adult_min[3], adult_max[3], child_min[3], child_max[3];
    bounds(out, n, adult_min, adult_max);
    llama.flags |= 8;
    n = gm_entities_emit(&llama, 1, child, MAXV);
    CHECK(n == 18 * 72,
          "child decorated llama suppresses both chest boxes in both layers");
    bounds(child, n, child_min, child_max);
    CHECK(child_max[1] - child_min[1] < adult_max[1] - adult_min[1],
          "child ModelLlama three-group transforms reduce visible height");

    GmEntityView spit;
    memset(&spit, 0, sizeof spit);
    spit.type = 73; spit.x = 1.0f; spit.y = 65.0f; spit.z = 2.0f;
    spit.yaw = 30.0f; spit.pitch = -20.0f; spit.health = 1.0f;
    n = gm_entities_emit(&spit, 1, out, MAXV);
    CHECK(n == 7 * 36,
          "EntityLlamaSpit emits all seven ModelLlamaSpit cubes");
    int spit_uv = 1;
    for (int i = 0; i < n; ++i)
        if (!uv_inside_mob_sprite(&out[i], CR_MOB_LLAMA_SPIT)) spit_uv = 0;
    CHECK(spit_uv, "llama spit uses textures/entity/llama/spit.png");
    CHECK(approx(gm_entity_eye_y(73), 0.125f, 1e-6f),
          "llama spit light sample matches its 0.25-block height");
}

static void test_recorded_state(void) {
    printf("\n[H] exact recorded entity state\n");
    CrVertex out[MAXV];
    GmEntityView sheep;memset(&sheep,0,sizeof sheep);
    sheep.type=10;sheep.y=64;sheep.health=8;sheep.tape_pose=1;
    sheep.head_yaw=45;sheep.pitch=10;sheep.graze_y=0.5f;sheep.graze_x=0.9f;
    sheep.fleece_color=14;
    int wool_n=gm_entities_emit(&sheep,1,out,MAXV);
    CHECK(wool_n==12*36,"recorded unsheared sheep keeps skin + wool layers");
    int red_wool=0;
    for(int i=0;i<wool_n;++i)if(out[i].tint.r>out[i].tint.g*2)red_wool++;
    CHECK(red_wool>0,"recorded red fleece tints wool vertices");
    sheep.sheared=1;
    CHECK(gm_entities_emit(&sheep,1,out,MAXV)==6*36,
          "recorded sheared sheep omits all six wool boxes");
    sheep.tape_pose=0;sheep.sheared=0;
    wool_n=gm_entities_emit(&sheep,1,out,MAXV);red_wool=0;
    for(int i=0;i<wool_n;++i)if(out[i].tint.r>out[i].tint.g*2)red_wool++;
    CHECK(red_wool>0,"live red fleece tints wool vertices");
    sheep.sheared=1;
    CHECK(gm_entities_emit(&sheep,1,out,MAXV)==6*36,
          "live sheared sheep omits all six wool boxes");
    sheep.tape_pose=1;
    sheep.flags=4;
    CHECK(gm_entities_emit(&sheep,1,out,MAXV)==0,
          "recorded invisible entity emits no geometry");
    GmEntityView wolf;memset(&wolf,0,sizeof wolf);
    wolf.type=16;wolf.y=64;wolf.health=20;wolf.flags=128;wolf.item_meta=14;
    int wolf_n=gm_entities_emit(&wolf,1,out,MAXV),collar_red=0;
    CHECK(wolf_n==22*36,
          "tamed wolf emits base model plus full collar layer");
    for(int i=11*36;i<wolf_n;++i)
        if(out[i].tint.r>out[i].tint.g*2)collar_red++;
    CHECK(collar_red==11*36,
          "red wolf collar tint covers the complete overlay geometry");
    sheep.tape_pose=0;
    CHECK(gm_entities_emit(&sheep,1,out,MAXV)==0,
          "live invisible entity emits no geometry");
    sheep.flags=4|GM_ENTITY_FLAG_GLOWING;
    CHECK(gm_entities_glowing_emit(&sheep,1,out,MAXV)==6*36,
          "outline pass restores an invisible glowing living model");
    sheep.flags=4;
    CHECK(gm_entities_glowing_emit(&sheep,1,out,MAXV)==0,
          "outline pass excludes a non-glowing living model");
}

/* (I) slime/magma size scale from item_meta (RenderSlime/RenderMagmaCube). */
static void test_slime_magma_size(void) {
    printf("\n[I] slime/magma size scale\n");
    CrVertex out[MAXV];
    float mn[3], mx[3];
    GmEntityView s1; memset(&s1, 0, sizeof s1);
    s1.type = 35; s1.y = 64; s1.health = 4; s1.item_meta = 1;
    int n1 = gm_entities_emit(&s1, 1, out, MAXV);
    bounds(out, n1, mn, mx);
    float h1 = mx[1] - mn[1], min_y1 = mn[1];
    GmEntityView s2 = s1; s2.item_meta = 2;
    int n2 = gm_entities_emit(&s2, 1, out, MAXV);
    bounds(out, n2, mn, mx);
    float h2 = mx[1] - mn[1];
    GmEntityView s4 = s1; s4.item_meta = 4;
    int n4 = gm_entities_emit(&s4, 1, out, MAXV);
    bounds(out, n4, mn, mx);
    float h4 = mx[1] - mn[1];
    CHECK(n1 == 4 * 36 && n2 == 4 * 36 && n4 == 4 * 36,
          "slime emits 4 boxes at every size");
    CHECK(approx(h2 / h1, 2.0f, 0.05f), "slime size 2 is 2x size 1 height");
    CHECK(approx(h4 / h1, 4.0f, 0.05f), "slime size 4 is 4x size 1 height");
    /* ModelSlime body bottom at model y=23 -> world (24-23)/16 = 0.0625 above feet. */
    CHECK(approx(min_y1, 64.0f + 0.0625f, 0.05f),
          "slime size 1 rests near ground (model y=23 floor)");
    CHECK(gm_slime_gel_emit(&s1, 1, out, MAXV) == 36,
          "visible slime emits its gel layer");
    s1.flags = GM_ENTITY_FLAG_GLOWING;
    CHECK(gm_entities_glowing_slime_gel_emit(&s1, 1, out, MAXV) == 36,
          "glowing slime emits one gel box in the outline pass");
    s1.flags = 4;
    CHECK(gm_slime_gel_emit(&s1, 1, out, MAXV) == 0,
          "invisible slime omits its gel layer");
    s1.flags = 4 | GM_ENTITY_FLAG_GLOWING;
    CHECK(gm_entities_glowing_slime_gel_emit(&s1, 1, out, MAXV) == 36,
          "glowing outline restores an invisible slime gel layer");
    s1.flags = 0;
    CHECK(gm_entities_glowing_slime_gel_emit(&s1, 1, out, MAXV) == 0,
          "non-glowing slime omits the outline gel layer");

    GmEntityView m2; memset(&m2, 0, sizeof m2);
    m2.type = 27; m2.y = 64; m2.health = 4; m2.item_meta = 2;
    int nm2 = gm_entities_emit(&m2, 1, out, MAXV);
    bounds(out, nm2, mn, mx);
    float mh2 = mx[1] - mn[1];
    GmEntityView m4 = m2; m4.item_meta = 4;
    int nm4 = gm_entities_emit(&m4, 1, out, MAXV);
    bounds(out, nm4, mn, mx);
    float mh4 = mx[1] - mn[1];
    CHECK(nm2 == 9 * 36, "magma emits 9 boxes");
    CHECK(approx(mh4 / mh2, 2.0f, 0.05f), "magma size 4 is 2x size 2 height");
    /* default meta 0 -> size 2 for magma */
    GmEntityView md = m2; md.item_meta = 0;
    int nmd = gm_entities_emit(&md, 1, out, MAXV);
    bounds(out, nmd, mn, mx);
    CHECK(approx(mx[1] - mn[1], mh2, 0.05f),
          "magma item_meta 0 defaults to size 2");
    m2.flags = GM_ENTITY_FLAG_GLOWING;
    CHECK(gm_entities_glowing_slime_gel_emit(&m2, 1, out, MAXV) == 0,
          "glowing magma cube does not receive the slime gel layer");
}

/* (J) large vs small fireball scale patch + dragon death rays + particles. */
static void test_fireball_rays_particles(void) {
    printf("\n[J] fireball scale, death rays, particles\n");
    GmEntityView fb[2];
    memset(fb, 0, sizeof fb);
    fb[0].type = 30; fb[0].item_id = 385; fb[0].y = 70; /* small */
    fb[1].type = 30; fb[1].item_id = 385; fb[1].y = 72; /* large candidate */
    int ptypes[2] = { 3, 5 };
    gm_entity_patch_large_fireballs(ptypes, 2, fb, 2);
    CHECK(fb[0].type == 30 && fb[0].item_meta == 1,
          "small fireball stays BILLBOARD meta 1");
    CHECK(fb[1].type == 33 && fb[1].item_id == 385 && fb[1].item_meta == 2,
          "large fireball morphs to dragon-fireball type with fire_charge id");
    /* item_meta-only path */
    GmEntityView solo; memset(&solo, 0, sizeof solo);
    solo.type = 30; solo.item_id = 385; solo.item_meta = 2;
    gm_entity_patch_large_fireballs(NULL, 0, &solo, 1);
    CHECK(solo.type == 33 && solo.item_id == 385,
          "item_meta>=2 alone promotes large fireball scale path");
    gm_entity_prep_large_fireball_fire(&solo, 1);
    CHECK(solo.type == 30, "prep retypes large fireball as BILLBOARD for fire");
    gm_entity_restore_large_fireball_types(&solo, 1);
    CHECK(solo.type == 33, "restore returns large fireball type");

    CHECK(gm_entity_type_for_name("EntityLargeFireball") == 33,
          "EntityLargeFireball maps to scale-2 fireball billboard");
    CHECK(gm_entity_billboard_item("EntityLargeFireball") == 385,
          "EntityLargeFireball uses fire_charge particle icon");

    CrVertex out[8192];
    GmEntityView drag; memset(&drag, 0, sizeof drag);
    drag.type = 9; drag.y = 80; drag.health = 0; drag.death_ticks = 100;
    int rays = gm_dragon_death_rays_emit(&drag, 1, out, 8192);
    CHECK(rays > 0 && rays % 9 == 0, "death rays are 5-vert fans = 3 tris (9 verts)");
    /* f=(100+1)/200; Java float loop (float)i < bound */
    {
        float f = 101.0f / 200.0f;
        float bound = (f + f * f) / 2.0f * 60.0f;
        int nray = 0;
        while ((float)nray < bound) ++nray;
        CHECK(rays == nray * 9, "deathTicks=100+pt1 ray count matches LayerEnderDragonDeath");
    }
    /* Smooth center alpha vs rim alpha 0 on first ray center verts. */
    CHECK(out[0].tint.r == 255 && out[0].tint.a > 0, "ray center is white+alpha");
    CHECK(out[1].tint.r == 255 && out[1].tint.g == 0 && out[1].tint.a == 0,
          "ray rim is magenta transparent");
    /* Random(432) 48-bit LCG: first-ray span must stay model-scale, not the
     * 52-bit mask overflow that produced 100+ block pure-white beams. */
    {
        float maxr = 0.0f;
        for (int i = 0; i < 9 && i < rays; ++i) {
            float dx = out[i].pos.x - drag.x;
            float dy = out[i].pos.y - drag.y;
            float dz = out[i].pos.z - drag.z;
            float r = sqrtf(dx * dx + dy * dy + dz * dz);
            if (r > maxr) maxr = r;
        }
        CHECK(maxr > 4.0f && maxr < 40.0f,
              "death-ray length uses java.util.Random 48-bit nextFloat");
    }
    drag.death_ticks = 0;
    CHECK(gm_dragon_death_rays_emit(&drag, 1, out, 8192) == 0,
          "living dragon emits no death rays");

    GmEntityView em; memset(&em, 0, sizeof em);
    em.type = 6; em.y = 64; em.health = 40; em.age = 10; em.ent_id = 7;
    int pn = gm_particles_emit(&em, 1, 0.0f, 0.0f, out, 8192);
    CHECK(pn == 90 * 6, "enderman: 2 portal/tick * ~45 age = 90 quads");
    /* UVs must land in CR_MOB_PARTICLES sheet, not enderman skin. */
    {
        const CrMobSprite *ps = &CR_MOB_SPRITES[CR_MOB_PARTICLES];
        float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
        int bad = 0;
        for (int i = 0; i < pn && i < 48; ++i) {
            float px = out[i].uv.x * aw, py = out[i].uv.y * ah;
            if (px < ps->x0 - 0.5f || px > ps->x1 + 0.5f ||
                py < ps->y0 - 0.5f || py > ps->y1 + 0.5f) bad++;
        }
        CHECK(bad == 0, "portal particle UVs sample particles.png sheet");
    }
    /* EntityDragon: EXPLOSION_LARGE every death tick; lifeTime=6+nextInt(4).
     * Only when health<=0 (onUpdate). Oracle pins keep health full. */
    GmEntityView dd = drag; dd.death_ticks = 50; dd.health = 200.0f;
    CHECK(gm_particles_emit(&dd, 1, 0.0f, 0.0f, out, 8192) == 0,
          "alive+deathTicks pin: no explosion particle recon");
    dd.health = 0.0f;
    int sn = gm_particles_emit(&dd, 1, 0.0f, 0.0f, out, 8192);
    CHECK(sn > 0 && sn % 6 == 0, "mid-death: reconstructed EXPLOSION_LARGE quads");
    CHECK(sn / 6 <= 9, "LARGE lookback capped by max lifeTime=9");
    /* UVs sample explosion.png (4x4 frames), NOT particles.png interior.
     * Atlas packing may put explosion | particles on a shared x boundary. */
    {
        const CrMobSprite *ex = &CR_MOB_SPRITES[CR_MOB_EXPLOSION];
        const CrMobSprite *ps = &CR_MOB_SPRITES[CR_MOB_PARTICLES];
        float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
        int in_expl = 0, mid_in_part = 0;
        for (int i = 0; i < sn; ++i) {
            float px = out[i].uv.x * aw, py = out[i].uv.y * ah;
            if (px >= ex->x0 - 0.5f && px <= ex->x1 + 0.5f &&
                py >= ex->y0 - 0.5f && py <= ex->y1 + 0.5f) in_expl++;
        }
        /* Quad mid-UV must not sit inside particles.png (strict interior). */
        for (int q = 0; q < sn; q += 6) {
            float mx = 0, my = 0;
            for (int k = 0; k < 6; ++k) {
                mx += out[q + k].uv.x * aw;
                my += out[q + k].uv.y * ah;
            }
            mx /= 6.0f; my /= 6.0f;
            if (mx > ps->x0 + 1.0f && mx < ps->x1 - 1.0f &&
                my > ps->y0 + 1.0f && my < ps->y1 - 1.0f) mid_in_part++;
        }
        CHECK(in_expl == sn, "EXPLOSION_LARGE UVs sample explosion.png");
        CHECK(mid_in_part == 0, "EXPLOSION_LARGE does not use particles.png");
        /* Gray tint in [0.4,1.0] * 255 from ParticleExplosionLarge ctor. */
        int gray_ok = 1;
        for (int i = 0; i < sn; i += 6) {
            if (out[i].tint.r < 100 || out[i].tint.r != out[i].tint.g ||
                out[i].tint.g != out[i].tint.b) gray_ok = 0;
        }
        CHECK(gray_ok, "EXPLOSION_LARGE gray tint (rand*0.6+0.4)");
        /* Half-extent at progress=0 is 2.0*size with size=1 → full edge 4. */
        float mn = 1e30f, mx = -1e30f;
        for (int i = 0; i < 6; ++i) {
            if (out[i].pos.x < mn) mn = out[i].pos.x;
            if (out[i].pos.x > mx) mx = out[i].pos.x;
        }
        /* Camera yaw=0: billboard X extent = 2*half = 4.0 for size=1. */
        CHECK(fabsf((mx - mn) - 4.0f) < 0.05f || (mx - mn) > 0.5f,
              "EXPLOSION_LARGE scale uses f4=2*size (progress 0)");
    }
    dd.death_ticks = 190;
    int hn = gm_particles_emit(&dd, 1, 0.0f, 0.0f, out, 8192);
    CHECK(hn > sn, "deathTicks 180-200 adds EXPLOSION_HUGE (6 LARGE/tick)");
    CHECK(hn % 6 == 0, "HUGE-window particle verts stay as quads");
    {
        const CrMobSprite *ex = &CR_MOB_SPRITES[CR_MOB_EXPLOSION];
        float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
        int bad = 0;
        for (int i = 0; i < hn && i < 48; ++i) {
            float px = out[i].uv.x * aw, py = out[i].uv.y * ah;
            if (px < ex->x0 - 1 || px > ex->x1 + 1 ||
                py < ex->y0 - 1 || py > ex->y1 + 1) bad++;
        }
        CHECK(bad == 0, "HUGE children sample explosion.png");
    }

    CHECK(gm_entity_rot_rx90_maps_y_to_z(), "Rx(+90) +Y -> +Z");
    CHECK(gm_entity_rot_axes_are_unit(), "+90 axis compositions stay unit");

    /* Dig particles: model particle icon (ParticleDigging), not BM_NORTH. */
    {
        int dn = gm_block_break_particles_emit(0, 64, 0, 1 /* stone */, 5,
                                               1 /* UP face */, 0 /* count=stage */,
                                               0.0f, 0.0f, 1.f, 1.f, 1.f, out, 8192);
        CHECK(dn == 5 * 6, "dig stage 5 emits 5 hit-effect quads");
        float bu0, bv0, bu1, bv1;
        bm_sprite_uv(bm_particle_sprite(1), &bu0, &bv0, &bu1, &bv1);
        int outside = 0;
        for (int i = 0; i < dn; ++i) {
            if (out[i].uv.x < bu0 - 1e-4f || out[i].uv.x > bu1 + 1e-4f ||
                out[i].uv.y < bv0 - 1e-4f || out[i].uv.y > bv1 + 1e-4f)
                outside++;
        }
        CHECK(outside == 0, "dig UVs stay inside model particle sprite");
        /* CLASS AUDIT: the emit takes a MODEL KEY, and stone happens to be a
         * fixed point of the vanilla->key map, so testing stone alone hid a
         * live bug where callers passed the vanilla id (grass -> water_flow,
         * sand -> lava_flow). Drive the whole class through the SAME
         * conversion the callers use, and assert by sprite NAME. */
        {
            typedef struct { int id, meta; const char *name; int sprite; } PSpec;
            static const PSpec PS[] = {
                { 1,  0, "stone",          CR_SPRITE_STONE },
                { 2,  0, "grass",          CR_SPRITE_DIRT },   /* vanilla: dirt */
                { 3,  0, "dirt",           CR_SPRITE_DIRT },
                { 5,  0, "planks_oak",     CR_SPRITE_PLANKS_OAK },
                { 12, 0, "sand",           CR_SPRITE_SAND },
                { 13, 0, "gravel",         CR_SPRITE_GRAVEL },
                { 16, 0, "coal_ore",       CR_SPRITE_COAL_ORE },
                { 17, 0, "log_oak",        CR_SPRITE_LOG_OAK },
                { 18, 0, "leaves_oak",     CR_SPRITE_LEAVES_OAK },
                { 58, 0, "crafting_table", CR_SPRITE_CRAFTING_TABLE_FRONT },
            };
            for (unsigned k = 0; k < sizeof PS / sizeof PS[0]; ++k) {
                int key = gm_state_to_model_key(
                    gm_pack_state(PS[k].id, PS[k].meta & 15));
                float eu0, ev0, eu1, ev1;
                bm_sprite_uv(PS[k].sprite, &eu0, &ev0, &eu1, &ev1);
                int n = gm_block_break_particles_emit(0, 64, 0, key, 5, 1, 0,
                                                      0.0f, 0.0f, 1.f, 1.f, 1.f,
                                                      out, 8192);
                int bad = (n != 5 * 6);
                for (int i = 0; i < n; ++i) {
                    if (out[i].uv.x < eu0 - 1e-4f || out[i].uv.x > eu1 + 1e-4f ||
                        out[i].uv.y < ev0 - 1e-4f || out[i].uv.y > ev1 + 1e-4f)
                        bad = 1;
                }
                char msg[96];
                snprintf(msg, sizeof msg, "%s particles use its own sprite",
                         PS[k].name);
                CHECK(bad == 0, msg);
            }
        }
        CHECK(out[0].tint.r == 153 && out[0].tint.a == 255,
              "dig particles use ParticleDigging 0.6 gray tint");
        /* Face UP: py = maxY+0.1 = 1.1 relative → world y = 64 + 1.1 */
        float ymean = 0;
        for (int i = 0; i < 6; ++i) ymean += out[i].pos.y;
        ymean /= 6.0f;
        CHECK(fabsf(ymean - 65.1f) < 0.15f,
              "UP-face dig particles spawn at maxY+0.1");
        /* Java: ctor scale/=2 then *0.6 → f4=0.1*scale in [0.03,0.06].
         * yaw=0 pitch=0: billboard X span = 2*half. */
        {
            int half_ok = 1;
            float hmin = 1e30f, hmax = -1e30f;
            for (int q = 0; q < dn; q += 6) {
                float xmin = 1e30f, xmax = -1e30f;
                for (int i = 0; i < 6; ++i) {
                    float x = out[q + i].pos.x;
                    if (x < xmin) xmin = x;
                    if (x > xmax) xmax = x;
                }
                float half = 0.5f * (xmax - xmin);
                if (half < hmin) hmin = half;
                if (half > hmax) hmax = half;
                if (half < 0.03f - 1e-5f || half > 0.06f + 1e-5f) half_ok = 0;
            }
            CHECK(half_ok && hmin >= 0.03f - 1e-5f && hmax <= 0.06f + 1e-5f,
                  "dig ParticleDigging half-extent in Java [0.03,0.06]");
        }
        /* CB_GRASS=3: model particle is dirt (DOWN), not grass_side (NORTH). */
        CHECK(bm_particle_sprite(3) == bm_block(3)->face[0 /* BM_DOWN */].sprite,
              "grass particle icon is dirt (not grass_side)");
        CHECK(bm_particle_sprite(3) != bm_block(3)->face[2 /* BM_NORTH */].sprite,
              "grass particle is not BM_NORTH grass_side");
        /* entity_pin dig_hit freezes N billboards independent of crack stage. */
        int dn8 = gm_block_break_particles_emit(0, 64, 0, 1, 4 /* stage */,
                                                1 /* UP */, 8 /* pin count */,
                                                0.0f, 0.0f, 1.f, 1.f, 1.f,
                                                out, 8192);
        CHECK(dn8 == 8 * 6, "dig particle_count override emits N quads");
    }

    /* Dragon dissolve: preserve both mask threshold and face lighting. */
    drag.death_ticks = 100;
    int mid = gm_entities_emit(&drag, 1, out, 8192);
    CHECK(mid > 0, "mid-death dragon still emits full geometry");
    int marked = 0;
    float ao_min = 1.0f, ao_max = 0.0f;
    for (int i = 0; i < mid; ++i) {
        float packed = -out[i].light;
        float whole = floorf(packed);
        float threshold = (packed - whole) * 32.0f;
        if (whole == 16.0f && fabsf(threshold - 0.5f) < 1e-5f)
            marked++;
        if (out[i].ao < ao_min) ao_min = out[i].ao;
        if (out[i].ao > ao_max) ao_max = out[i].ao;
    }
    CHECK(marked == mid, "all death verts pack light=15 + dissolve f=0.5");
    CHECK(ao_min < ao_max && ao_min >= 0.0f && ao_max <= 1.0f,
          "dragon dissolve retains directional face lighting in ao");
    drag.death_ticks = 200;
    CHECK(gm_entities_emit(&drag, 1, out, 8192) == 0,
          "deathTicks=200 emits no dragon body");
}

/* (K) death keel (RenderLivingBase.applyRotations) + spawner miniature. */
static void test_death_and_spawner(void) {
    printf("\n[K] death keel + spawner miniature\n");
    static CrVertex out[65536], out2[65536];

    /* er_death_roll: f = min(1, sqrt(deathTime/20*1.6)) * 90deg, partial=1. */
    GmEntityView v; memset(&v, 0, sizeof v);
    v.type = 7; v.tape_pose = 1;                      /* EntityBlaze */
    v.x = 0.5f; v.y = 64.0f; v.z = 0.5f; v.health = 0.0f;
    CHECK(er_death_roll(&v) == 0.0f, "alive (deathTime 0) has no keel");
    v.death_time = 1;
    CHECK(fabsf(er_death_roll(&v) - sqrtf(1.0f/20.0f*1.6f)*(float)M_PI/2.0f)
          < 1e-6f, "deathTime=1 keel = sqrt(0.08)*90deg");
    v.death_time = 20;
    CHECK(fabsf(er_death_roll(&v) - (float)M_PI/2.0f) < 1e-6f,
          "deathTime=20 saturates at getDeathMaxRotation (90deg)");
    v.death_time = 100;
    CHECK(fabsf(er_death_roll(&v) - (float)M_PI/2.0f) < 1e-6f,
          "keel clamps at 90deg past deathTime=20");
    /* monotone rise over the vanilla 0..20 window */
    /* sqrt(deathTime * 0.08) reaches 1 at deathTime 12.5, so the body is
     * fully keeled from deathTime 13 - a full 7 ticks before onDeathUpdate
     * removes the entity at 20. Strictly rising to 12, flat after. */
    float prev = -1.0f;
    int rising = 1, flat = 1;
    for (int d = 1; d <= 20; ++d) {
        v.death_time = d;
        float r = er_death_roll(&v);
        if (d <= 12 && r <= prev) rising = 0;
        if (d >= 13 && fabsf(r - (float)M_PI / 2.0f) > 1e-6f) flat = 0;
        prev = r;
    }
    CHECK(rising, "keel rises strictly over deathTime 1..12");
    CHECK(flat, "keel is saturated at 90deg from deathTime 13 on");

    /* The keel must actually move geometry, about the FEET, without changing
     * the vertex count: a fully keeled blaze is as wide as it was tall. */
    v.death_time = 0;
    int nlive = gm_entities_emit(&v, 1, out, 65536);
    v.death_time = 20;
    int ndead = gm_entities_emit(&v, 1, out2, 65536);
    CHECK(nlive == ndead && nlive > 0, "keel changes no vertex count");
    float ly0 = 1e9f, ly1 = -1e9f, lx0 = 1e9f, lx1 = -1e9f;
    float dy0 = 1e9f, dy1 = -1e9f, dx0 = 1e9f, dx1 = -1e9f;
    for (int i = 0; i < nlive; ++i) {
        if (out[i].pos.y < ly0) ly0 = out[i].pos.y;
        if (out[i].pos.y > ly1) ly1 = out[i].pos.y;
        if (out[i].pos.x < lx0) lx0 = out[i].pos.x;
        if (out[i].pos.x > lx1) lx1 = out[i].pos.x;
        if (out2[i].pos.y < dy0) dy0 = out2[i].pos.y;
        if (out2[i].pos.y > dy1) dy1 = out2[i].pos.y;
        if (out2[i].pos.x < dx0) dx0 = out2[i].pos.x;
        if (out2[i].pos.x > dx1) dx1 = out2[i].pos.x;
    }
    CHECK(dy1 - dy0 < ly1 - ly0, "keeled body is shorter than the live one");
    CHECK(dx1 - dx0 > lx1 - lx0, "keeled body is wider than the live one");
    /* 90deg about Z at the feet: the dead height ~= the live half-width span,
     * and the dead x-span ~= the live height. Both within a texel. */
    CHECK(fabsf((dx1 - dx0) - (ly1 - ly0)) < 0.07f,
          "90deg keel maps the live height onto the dead x-span");
    /* the feet stay put (rotation pivot is the entity origin) */
    CHECK(fabsf(dy0 - v.y) < 0.7f && ly0 >= v.y - 0.7f,
          "keel pivots on the feet, not the model centre");

    /* Hurt/death tint: RenderLivingBase.setBrightness flag1 covers BOTH. */
    GmEntityView t = v;
    t.death_time = 0; t.hurt_time = 0;
    gm_entities_emit(&t, 1, out, 65536);
    CrRgba plain = out[0].tint;
    t.hurt_time = 5;
    gm_entities_emit(&t, 1, out, 65536);
    CrRgba hurt = out[0].tint;
    t.hurt_time = 0; t.death_time = 10;   /* hurtTime expired, still dying */
    gm_entities_emit(&t, 1, out, 65536);
    CrRgba dead = out[0].tint;
    CHECK(hurt.g < plain.g && hurt.b < plain.b && hurt.r == plain.r,
          "hurtTime>0 leans the tint red");
    CHECK(dead.g == hurt.g && dead.b == hurt.b,
          "deathTime>0 keeps the red tint after hurtTime hits 0");

    /* ---- TileEntityMobSpawnerRenderer miniature ---- */
    float w = 0.0f, h = 0.0f;
    gm_entity_size(7, &w, &h);
    CHECK(fabsf(w - 0.6f) < 1e-6f && fabsf(h - 1.8f) < 1e-6f,
          "EntityBlaze setSize(0.6, 1.8)");
    CHECK(fabsf(gm_spawner_mini_scale(7) - 0.53125f / 1.8f) < 1e-6f,
          "renderMob f = 0.53125 / max(width,height) for a blaze");
    gm_entity_size(13, &w, &h);              /* chicken 0.4 x 0.7, both < 1 */
    CHECK(fabsf(gm_spawner_mini_scale(13) - 0.53125f) < 1e-6f,
          "max(w,h) <= 1 leaves f at 0.53125 (no shrink)");

    GmSpawnerView sp; memset(&sp, 0, sizeof sp);
    sp.wx = -325; sp.wy = 56; sp.wz = -102; sp.type = 7;
    sp.mob_rotation = 0.0f; sp.partial_ticks = 1.0f;
    int ns = gm_spawner_miniatures_emit(&sp, 1, out, 65536);
    CHECK(ns > 0 && ns % 36 == 0, "miniature emits whole 36-vert boxes");
    CHECK(ns == nlive, "miniature has the blaze's full part count");
    float mx0 = 1e9f, mx1 = -1e9f, my0 = 1e9f, my1 = -1e9f,
          mz0 = 1e9f, mz1 = -1e9f;
    for (int i = 0; i < ns; ++i) {
        if (out[i].pos.x < mx0) mx0 = out[i].pos.x;
        if (out[i].pos.x > mx1) mx1 = out[i].pos.x;
        if (out[i].pos.y < my0) my0 = out[i].pos.y;
        if (out[i].pos.y > my1) my1 = out[i].pos.y;
        if (out[i].pos.z < mz0) mz0 = out[i].pos.z;
        if (out[i].pos.z > mz1) mz1 = out[i].pos.z;
    }
    /* It must live INSIDE the spawner block, not at world scale. */
    CHECK(mx0 > -326.0f && mx1 < -324.0f && mz0 > -103.0f && mz1 < -101.0f,
          "miniature stays inside the spawner block footprint");
    CHECK(my0 > 55.5f && my1 < 57.5f, "miniature stays within the cage in y");
    /* full-size blaze spans ~1.8 blocks tall; the miniature is f times that */
    CHECK((my1 - my0) < (ly1 - ly0) * 0.6f,
          "miniature is much smaller than a world-scale blaze");
    /* the -30deg pitch means it is NOT axis aligned: some box is tilted */
    CHECK((mz1 - mz0) > 0.2f, "the -30deg X tilt gives the mini real z extent");

    /* mobRotation spins it about Y: a different rotation moves vertices, and
     * the +36deg (x10) case must equal a 360-degree wrap of itself. */
    GmSpawnerView sp2 = sp; sp2.mob_rotation = 9.0f;   /* 90 degrees rendered */
    int ns2 = gm_spawner_miniatures_emit(&sp2, 1, out2, 65536);
    CHECK(ns2 == ns, "rotation does not change the vertex count");
    int moved = 0;
    for (int i = 0; i < ns; ++i)
        if (fabsf(out[i].pos.x - out2[i].pos.x) > 1e-4f ||
            fabsf(out[i].pos.z - out2[i].pos.z) > 1e-4f) moved++;
    CHECK(moved > ns / 2, "mobRotation actually spins the miniature about Y");
    GmSpawnerView sp3 = sp; sp3.mob_rotation = 36.0f;  /* 360 degrees */
    gm_spawner_miniatures_emit(&sp3, 1, out2, 65536);
    int same = 1;
    for (int i = 0; i < ns; ++i)
        if (fabsf(out[i].pos.x - out2[i].pos.x) > 2e-3f ||
            fabsf(out[i].pos.y - out2[i].pos.y) > 2e-3f ||
            fabsf(out[i].pos.z - out2[i].pos.z) > 2e-3f) { same = 0; break; }
    CHECK(same, "mobRotation 36 (=360 deg) is a full turn back to identity");

    /* No cached entity (unknown spawn id) draws nothing - vanilla's null guard.
     * This is the state every magma spawner is in today: no tile-entity data
     * reaches the renderer (see OPEN_DIVERGENCES "Spawner-cage miniature"). */
    GmSpawnerView none = sp; none.type = -1;
    CHECK(gm_spawner_miniatures_emit(&none, 1, out, 65536) == 0,
          "no cached entity type emits no miniature");
    /* overflow safety */
    CHECK(gm_spawner_miniatures_emit(&sp, 1, out, 10) == 0,
          "miniature emit respects the vertex budget");
}

static int add_unique_pos(CrVec3 *p, int n, CrVec3 v) {
    for (int i = 0; i < n; ++i)
        if (fabsf(p[i].x-v.x)<1e-5f && fabsf(p[i].y-v.y)<1e-5f
                && fabsf(p[i].z-v.z)<1e-5f)
            return n;
    p[n] = v;
    return n + 1;
}

static void test_crystal_beam(void) {
    printf("\n== ENDER-CRYSTAL BEAM ==\n");
    GmEntityView e;
    memset(&e, 0, sizeof e);
    e.type = 31;
    e.x = 2.5f; e.y = 9.0f; e.z = -3.5f;
    e.crystal_rot = 19.0f;
    e.beam_x = -6; e.beam_y = 4; e.beam_z = 7;
    CrVertex out[96];
    CHECK(gm_crystal_beams_emit(&e, 1, out, 96) == 0,
          "crystal without has_beam emits no beam");
    e.has_beam = 1;
    CHECK(gm_crystal_beams_emit(&e, 1, out, 95) == 0,
          "beam capacity is atomic (95 cannot hold its two-sided strip)");
    int n = gm_crystal_beams_emit(&e, 1, out, 96);
    CHECK(n == 96, "8-sided cull-disabled beam emits 16 tris in both windings");

    CrVec3 lo[8], hi[8]; int nlo = 0, nhi = 0;
    int tint_ok = 1, uv_ok = 1;
    float dx=e.x-((float)e.beam_x+0.5f);
    float dy=e.y-1.0f-((float)e.beam_y+0.5f);
    float dz=e.z-((float)e.beam_z+0.5f);
    float length=sqrtf(dx*dx+dy*dy+dz*dz);
    float v0=-(e.crystal_rot+1.0f)*0.01f;
    float v1=length/32.0f+v0;
    for (int i = 0; i < n; ++i) {
        if (out[i].tint.r == 0 && out[i].tint.g == 0 && out[i].tint.b == 0) {
            nlo=add_unique_pos(lo,nlo,out[i].pos);
            if (!approx(out[i].uv.y,v0,1e-6f)) uv_ok=0;
        } else if (out[i].tint.r == 255 && out[i].tint.g == 255
                   && out[i].tint.b == 255) {
            nhi=add_unique_pos(hi,nhi,out[i].pos);
            if (!approx(out[i].uv.y,v1,1e-6f)) uv_ok=0;
        } else tint_ok=0;
    }
    CHECK(nlo == 8 && nhi == 8, "strip closes over eight unique vertices per ring");
    CHECK(tint_ok, "smooth-shade endpoints are black at target and white at crystal");
    CHECK(uv_ok && v0 < 0.0f, "scrolling beam V is unclamped geometry input");
    CrVec3 lc={0}, hc={0};
    for (int i=0;i<nlo;++i){lc.x+=lo[i].x;lc.y+=lo[i].y;lc.z+=lo[i].z;}
    for (int i=0;i<nhi;++i){hc.x+=hi[i].x;hc.y+=hi[i].y;hc.z+=hi[i].z;}
    lc.x/=nlo;lc.y/=nlo;lc.z/=nlo;hc.x/=nhi;hc.y/=nhi;hc.z/=nhi;
    float phase=e.crystal_rot+1.0f;
    float bob=sinf(phase*0.2f)/2.0f+0.5f;bob=bob*bob+bob;
    CHECK(approx(lc.x,(float)e.beam_x+0.5f,1e-4f)
          && approx(lc.y,(float)e.beam_y+2.2f+bob*0.4f,1e-4f)
          && approx(lc.z,(float)e.beam_z+0.5f,1e-4f),
          "target ring center matches RenderEnderCrystal translation");
    CHECK(approx(hc.x,e.x,1e-4f)
          && approx(hc.y,e.y+0.7f+bob*0.4f,1e-4f)
          && approx(hc.z,e.z,1e-4f),
          "far ring center lands on the bobbing crystal endpoint");

    CrTexture bt=gm_crystal_beam_texture();
    CHECK(bt.w==16 && bt.h==256 && bt.texels,
          "beam texture is the standalone 16x256 jar image");
    int opaque=0,transparent=0;
    for(int i=0;i<bt.w*bt.h;++i){opaque+=bt.texels[i].a==255;transparent+=bt.texels[i].a==0;}
    CHECK(opaque==838 && transparent==3258,
          "beam alpha histogram matches the real 1.11.2 texture");

    GmEntityView d;memset(&d,0,sizeof d);
    d.type=9;d.x=3.0f;d.y=80.0f;d.z=-2.0f;
    d.ticks_existed=39;d.has_heal_beam=1;
    d.heal_x=18.5f;d.heal_y=91.0f;d.heal_z=7.5f;
    d.heal_crystal_ticks=29;d.lm_lit=1;d.lm_light=3.0f;d.lm_blk=2.0f;
    CrVertex dragon_out[96];
    int dn=gm_crystal_beams_emit(&d,1,dragon_out,96);
    CHECK(dn==96,"dragon healing crystal emits one two-sided beam strip");
    CrVec3 db={0},dw={0},dbp[8],dwp[8];
    int ndb=0,ndw=0;float dv0=-0.4f;int duv=1;
    for(int i=0;i<dn;++i){
        if(dragon_out[i].tint.r==0){
            ndb=add_unique_pos(dbp,ndb,dragon_out[i].pos);
            if(!approx(dragon_out[i].uv.y,dv0,1e-6f))duv=0;}
        else if(dragon_out[i].tint.r==255)
            ndw=add_unique_pos(dwp,ndw,dragon_out[i].pos);
    }
    for(int i=0;i<ndb;++i){db.x+=dbp[i].x;db.y+=dbp[i].y;db.z+=dbp[i].z;}
    for(int i=0;i<ndw;++i){dw.x+=dwp[i].x;dw.y+=dwp[i].y;dw.z+=dwp[i].z;}
    db.x/=ndb;db.y/=ndb;db.z/=ndb;dw.x/=ndw;dw.y/=ndw;dw.z/=ndw;
    float hphase=(float)d.heal_crystal_ticks+1.0f;
    float hb=sinf(hphase*0.2f)/2.0f+0.5f;hb=(hb*hb+hb)*0.2f;
    CHECK(ndb>0&&approx(db.x,d.x,1e-4f)&&approx(db.y,d.y+2.0f,1e-4f)
          &&approx(db.z,d.z,1e-4f),
          "dragon beam black ring is centered at dragon render Y+2");
    CHECK(ndw>0&&approx(dw.x,d.heal_x,1e-4f)
          &&approx(dw.y,d.heal_y+hb+1.0f,1e-4f)
          &&approx(dw.z,d.heal_z,1e-4f),
          "dragon beam white ring lands on the bobbing healing crystal");
    CHECK(duv,"dragon ticksExisted drives exact scrolling V origin");
    int dlm=1;
    for(int i=0;i<dn;++i)
        if(dragon_out[i].light!=3.0f||dragon_out[i].blk!=2.0f)dlm=0;
    CHECK(dlm,"dragon healing beam retains owning entity lightmap levels");
    d.has_heal_beam=0;
    CHECK(gm_crystal_beams_emit(&d,1,dragon_out,96)==0,
          "dragon without healing crystal emits no beam");
    d.has_heal_beam=1;
    GmEntityView both[2]={e,d};CrVertex both_out[192];
    CHECK(gm_crystal_beams_emit(both,2,both_out,191)==96
          &&gm_crystal_beams_emit(both,2,both_out,192)==192,
          "mixed crystal-target and dragon-heal beams retain atomic capacity");
    {
        CrShadeCtx sh={0};sh.atlas=&bt;sh.alpha_test=1;sh.alpha_ref=0.1f;
        sh.sample_mode=1;sh.repeat_uv=1;
        CrFragment a={0},b={0};
        a.uv=(CrVec2){0.5f/16.0f,0.5f/256.0f};a.light=1;a.ao=1;
        a.tint=(CrRgba){255,255,255,255};b=a;
        b.uv.x+=2.0f;b.uv.y-=3.0f;
        CrRgba ca=cr_shade(&sh,&a),cb=cr_shade(&sh,&b);
        CHECK(!memcmp(&ca,&cb,sizeof ca),"beam shade repeats integer-shifted UVs");
    }
}

static void test_guardian_beam(void) {
    printf("\n== GUARDIAN BEAM ==\n");
    GmEntityView g;
    memset(&g, 0, sizeof g);
    g.type = 55; g.x = 1.0f; g.y = 62.0f; g.z = -2.0f;
    g.ticks_existed = 40; g.flags = 4096; g.has_heal_beam = 1;
    g.heal_x = 8.0f; g.heal_y = 63.9f; g.heal_z = 3.0f;
    g.swing_progress = .5f;
    CrVertex out[36];
    CHECK(gm_guardian_beams_emit(&g, 1, out, 35) == 0,
          "guardian beam capacity is atomic");
    int n = gm_guardian_beams_emit(&g, 1, out, 36);
    CHECK(n == 36,
          "guardian beam emits two crossed sides and cap in both windings");
    int fullbright = 1, color = 1, repeated_v = 0;
    for (int i = 0; i < n; ++i) {
        if (out[i].light != 15.0f || out[i].blk != 15.0f) fullbright = 0;
        if (out[i].tint.r != 111 || out[i].tint.g != 79
                || out[i].tint.b != 112 || out[i].tint.a != 255)
            color = 0;
        if (out[i].uv.y > 1.0f) repeated_v = 1;
    }
    CHECK(fullbright, "guardian beam pins both lightmap channels to 240");
    CHECK(color, "guardian beam color follows squared attack progress");
    CHECK(repeated_v, "guardian beam carries unclamped repeating V");
    CrTexture gt = gm_guardian_beam_texture();
    CHECK(gt.w == 32 && gt.h == 32 && gt.texels,
          "guardian beam texture is the standalone 32x32 jar image");
    g.flags = 0;
    CHECK(gm_guardian_beams_emit(&g, 1, out, 36) == 0,
          "guardian without targeted-entity data emits no beam");
}

static void test_hanging_render(void) {
    printf("\n== HANGING RENDER ==\n");
    GmPaintingView p;
    memset(&p, 0, sizeof p);
    p.x = 10.5f; p.y = 64.5f; p.z = 20.5f;
    p.facing = 2; p.art = 0;
    p.lm_light[0] = 11.0f; p.lm_blk[0] = 4.0f;
    CrVertex painting[576];
    CHECK(gm_paintings_emit(&p, 1, painting, 35) == 0,
          "one painting tile has an atomic 36-vertex capacity");
    int np = gm_paintings_emit(&p, 1, painting, 36);
    CHECK(np == 36, "16x16 painting emits six Java quads");
    float mn[3], mx[3];
    bounds(painting, np, mn, mx);
    CHECK(approx(mx[0] - mn[0], 1.0f, 1e-5f)
          && approx(mx[1] - mn[1], 1.0f, 1e-5f)
          && approx(mx[2] - mn[2], 0.0625f, 1e-5f),
          "16x16 painting has exact one-pixel wood thickness");
    int light_ok = 1;
    for (int i = 0; i < np; ++i)
        if (painting[i].light != 11.0f || painting[i].blk != 4.0f)
            light_ok = 0;
    CHECK(light_ok, "painting tile retains its sampled lightmap pair");
    const CrMobSprite *art = &CR_MOB_SPRITES[CR_MOB_PAINTINGS];
    float au0 = (float)art->x0 / CR_MOB_ATLAS_W;
    float au1 = (float)(art->x0 + 16) / CR_MOB_ATLAS_W;
    float av0 = (float)art->y0 / CR_MOB_ATLAS_H;
    float av1 = (float)(art->y0 + 16) / CR_MOB_ATLAS_H;
    int front_uv = 1;
    for (int i = 0; i < 6; ++i)
        if (painting[i].uv.x < au0 - 1e-6f || painting[i].uv.x > au1 + 1e-6f
                || painting[i].uv.y < av0 - 1e-6f || painting[i].uv.y > av1 + 1e-6f)
            front_uv = 0;
    CHECK(front_uv, "Kebab front samples its exact 16x16 art rectangle");

    p.art = 21; /* Pointer: 64x64, sixteen independently-lit tiles. */
    for (int i = 0; i < 16; ++i) {
        p.lm_light[i] = (float)i;
        p.lm_blk[i] = (float)(15 - i);
    }
    CHECK(gm_paintings_emit(&p, 1, painting, 575) == 0,
          "64x64 painting remains atomic at capacity boundary");
    np = gm_paintings_emit(&p, 1, painting, 576);
    CHECK(np == 576, "64x64 painting emits sixteen independently-lit tiles");
    int tiled_light = 1;
    for (int tile = 0; tile < 16; ++tile)
        for (int i = tile * 36; i < tile * 36 + 36; ++i)
            if (painting[i].light != (float)tile
                    || painting[i].blk != (float)(15 - tile))
                tiled_light = 0;
    CHECK(tiled_light, "painting lightmap changes only at 16x16 tile boundaries");
    int cells[16][3];
    int nc = gm_painting_light_cells(&p, cells, 16);
    CHECK(nc == 16, "64x64 painting requests sixteen Java light samples");
    CHECK(cells[0][0] == 9 && cells[0][1] == 63 && cells[0][2] == 20
          && cells[15][0] == 12 && cells[15][1] == 66 && cells[15][2] == 20,
          "north painting light cells follow exact horizontal and vertical tile centers");
    p.facing = 5;
    nc = gm_painting_light_cells(&p, cells, 16);
    CHECK(nc == 16 && cells[0][0] == 10 && cells[0][2] == 19
          && cells[15][0] == 10 && cells[15][2] == 22,
          "east painting light cells rotate onto the Z axis");

    GmLeashKnotView k = {3.5f, 65.5f, -2.5f, 12.0f, 3.0f};
    CrVertex knot[72];
    CHECK(gm_leash_knots_emit(&k, 1, knot, 71) == 0,
          "leash-knot model has atomic two-sided capacity");
    int nk = gm_leash_knots_emit(&k, 1, knot, 72);
    CHECK(nk == 72, "leash knot emits a two-sided six-face ModelBox");
    bounds(knot, nk, mn, mx);
    CHECK(approx(mx[0] - mn[0], .375f, 1e-5f)
          && approx(mx[1] - mn[1], .5f, 1e-5f)
          && approx(mx[2] - mn[2], .375f, 1e-5f),
          "leash knot has exact ModelLeashKnot dimensions");
    int knot_light = 1, reverse = 1;
    for (int i = 0; i < nk; ++i)
        if (knot[i].light != 12.0f || knot[i].blk != 3.0f) knot_light = 0;
    for (int i = 0; i < 72; i += 6)
        if (memcmp(&knot[i].pos, &knot[i + 3].pos, sizeof(CrVec3))
                || memcmp(&knot[i + 1].pos, &knot[i + 5].pos, sizeof(CrVec3))
                || memcmp(&knot[i + 2].pos, &knot[i + 4].pos, sizeof(CrVec3)))
            reverse = 0;
    CHECK(knot_light, "leash knot retains owning entity lightmap levels");
    CHECK(reverse, "leash knot duplicates each triangle with reverse winding");

    GmLeashRenderView l;
    memset(&l, 0, sizeof l);
    l.x = 0.0f; l.y = 64.0f; l.z = 0.0f;
    l.yaw = 0.0f; l.width = .9f; l.height = 1.4f;
    l.holder_x = 3.5f; l.holder_y = 65.5f; l.holder_z = .5f;
    l.holder_eye = -.0625f; l.holder_hanging = 1;
    CrVertex leash[576];
    CHECK(gm_living_leashes_emit(&l, 1, leash, 575) == 0,
          "two leash triangle strips have atomic capacity");
    int nl = gm_living_leashes_emit(&l, 1, leash, 576);
    CHECK(nl == 576, "leash emits two exact 24-segment triangle strips");
    CHECK(leash[0].tint.r == 128 && leash[0].tint.g == 102
          && leash[0].tint.b == 77 && leash[12].tint.r == 89
          && leash[12].tint.g == 71 && leash[12].tint.b == 54,
          "leash uses each Java flat strip segment's provoking color");
    CHECK(approx(leash[0].pos.x, 0.0f, 1e-5f)
          && approx(leash[0].pos.z, .36f, 1e-5f)
          && approx(leash[0].pos.y, 65.358333f, 1e-4f),
          "leash begins at the living render-offset endpoint");
    CHECK(approx(leash[566].pos.x, 3.5f, 1e-5f)
          && approx(leash[566].pos.z, .5f, 1e-5f)
          && approx(leash[566].pos.y, 65.75625f, 1e-4f),
          "leash terminates at an EntityHanging holder endpoint");
}

static void test_ender_chest_render(void) {
    GmEnderChestRenderView chest = {
        3, 70, -4, 3, 0.0f, 0.0f, 14.0f, 2.0f
    };
    CrVertex closed[108], open[108];
    CHECK(gm_ender_chests_emit(&chest, 1, 1.0f, closed, 107) == 0,
          "Ender Chest TESR model has atomic three-box capacity");
    int nc = gm_ender_chests_emit(&chest, 1, 1.0f, closed, 108);
    CHECK(nc == 108, "Ender Chest emits lid, knob, and base ModelBoxes");
    float mn[3], mx[3];
    bounds(closed, nc, mn, mx);
    int bounds_ok = approx(mn[0], 3.0625f, 1e-5f)
          && approx(mx[0], 3.9375f, 1e-5f)
          && approx(mn[1], 70.0f, 1e-5f)
          && approx(mx[1], 70.875f, 1e-5f)
          && approx(mn[2], -3.9375f, 1e-5f)
          && approx(mx[2], -3.0f, 1e-5f);
    if (!bounds_ok)
        printf("      actual bounds x=%g..%g y=%g..%g z=%g..%g\n",
               mn[0], mx[0], mn[1], mx[1], mn[2], mx[2]);
    CHECK(bounds_ok,
          "closed Ender Chest has exact inset body and protruding latch bounds");
    const CrMobSprite *sprite = &CR_MOB_SPRITES[CR_MOB_ENDER_CHEST];
    int uv_ok = 1, light_ok = 1;
    for (int i = 0; i < nc; ++i) {
        float tx = closed[i].uv.x * CR_MOB_ATLAS_W;
        float ty = closed[i].uv.y * CR_MOB_ATLAS_H;
        if (tx < sprite->x0 || tx > sprite->x1
                || ty < sprite->y0 || ty > sprite->y1)
            uv_ok = 0;
        if (closed[i].light != 14.0f || closed[i].blk != 2.0f)
            light_ok = 0;
    }
    CHECK(uv_ok, "Ender Chest samples only entity/chest/ender.png");
    CHECK(light_ok, "Ender Chest retains tile lightmap coordinates");
    chest.prev_lid = chest.lid = 1.0f;
    int no = gm_ender_chests_emit(&chest, 1, 1.0f, open, 108);
    CHECK(no == 108 && memcmp(closed + 72, open + 72,
                              36 * sizeof *closed) == 0,
          "lid animation leaves the base ModelBox bit-stable");
    CHECK(memcmp(closed, open, 72 * sizeof *closed) != 0,
          "full-open lid rotates lid and knob about the vanilla hinge");
}

static void test_wood_chest_render(void) {
    GmChestRenderView chest = {
        3, 70, -4, 3, 0.0f, 0.0f, 14.0f, 2.0f,
        GM_CHEST_TEXTURE_NORMAL, 0, 0, 0
    };
    CrVertex single[108], large[108], shifted[108];
    CHECK(gm_chests_emit(&chest, 1, 1.0f, single, 107) == 0,
          "wooden chest TESR model has atomic three-box capacity");
    int ns = gm_chests_emit(&chest, 1, 1.0f, single, 108);
    CHECK(ns == 108, "single wooden chest emits three ModelChest boxes");
    const CrMobSprite *normal = &CR_MOB_SPRITES[CR_MOB_CHEST_NORMAL];
    int uv_ok = 1;
    for (int i = 0; i < ns; ++i) {
        float tx = single[i].uv.x * CR_MOB_ATLAS_W;
        float ty = single[i].uv.y * CR_MOB_ATLAS_H;
        if (tx < normal->x0 || tx > normal->x1
                || ty < normal->y0 || ty > normal->y1)
            uv_ok = 0;
    }
    CHECK(uv_ok, "single chest samples only entity/chest/normal.png");

    chest.large = 1;
    chest.sprite = GM_CHEST_TEXTURE_NORMAL_DOUBLE;
    int nl = gm_chests_emit(&chest, 1, 1.0f, large, 108);
    float mn[3], mx[3];
    bounds(large, nl, mn, mx);
    CHECK(nl == 108 && (approx(mx[0] - mn[0], 1.875f, 1e-5f)
                        || approx(mx[2] - mn[2], 1.875f, 1e-5f)),
          "double chest uses the exact 30x14 ModelLargeChest footprint");
    const CrMobSprite *double_normal =
        &CR_MOB_SPRITES[CR_MOB_CHEST_NORMAL_DOUBLE];
    uv_ok = 1;
    for (int i = 0; i < nl; ++i) {
        float tx = large[i].uv.x * CR_MOB_ATLAS_W;
        float ty = large[i].uv.y * CR_MOB_ATLAS_H;
        if (tx < double_normal->x0 || tx > double_normal->x1
                || ty < double_normal->y0 || ty > double_normal->y1)
            uv_ok = 0;
    }
    CHECK(uv_ok, "double chest samples only normal_double.png");

    chest.meta = 2;
    chest.shift_x = 1;
    gm_chests_emit(&chest, 1, 1.0f, shifted, 108);
    chest.shift_x = 0;
    gm_chests_emit(&chest, 1, 1.0f, large, 108);
    int shift_ok = 1;
    for (int i = 0; i < 108; ++i)
        if (!approx(shifted[i].pos.x - large[i].pos.x, 1.0f, 1e-5f)
                || !approx(shifted[i].pos.y, large[i].pos.y, 1e-5f)
                || !approx(shifted[i].pos.z, large[i].pos.z, 1e-5f))
            shift_ok = 0;
    CHECK(shift_ok, "north-facing X double applies Java pre-rotation shift");

    chest.meta = 5;
    chest.shift_x = 0;
    chest.shift_z = 1;
    gm_chests_emit(&chest, 1, 1.0f, shifted, 108);
    chest.shift_z = 0;
    gm_chests_emit(&chest, 1, 1.0f, large, 108);
    shift_ok = 1;
    for (int i = 0; i < 108; ++i)
        if (!approx(shifted[i].pos.x, large[i].pos.x, 1e-5f)
                || !approx(shifted[i].pos.y, large[i].pos.y, 1e-5f)
                || !approx(shifted[i].pos.z - large[i].pos.z, 1.0f, 1e-5f))
            shift_ok = 0;
    CHECK(shift_ok, "east-facing Z double applies Java post-scale shift");
}

static void test_shulker_box_render(void) {
    GmShulkerBoxRenderView box = {
        3, 70, -4, 1, 0, 0.0f, 0.0f, 14.0f, 2.0f
    };
    CrVertex closed[144], open[144];
    float mn[3], mx[3];
    int n;

    CHECK(gm_shulker_boxes_emit(&box, 1, 1.0f, closed, 143) == 0,
          "Shulker Box TESR has atomic two-sided capacity");
    n = gm_shulker_boxes_emit(&box, 1, 1.0f, closed, 144);
    CHECK(n == 144,
          "Shulker Box emits base and lid with Java's disabled culling");
    {
        const CrMobSprite *sprite =
            &CR_MOB_SPRITES[CR_MOB_SHULKER_BOX_WHITE];
        int uv_ok = 1, light_ok = 1;
        for (int i = 0; i < n; ++i) {
            float tx = closed[i].uv.x * CR_MOB_ATLAS_W;
            float ty = closed[i].uv.y * CR_MOB_ATLAS_H;
            if (tx < sprite->x0 || tx > sprite->x1
                    || ty < sprite->y0 || ty > sprite->y1)
                uv_ok = 0;
            if (closed[i].light != 14.0f || closed[i].blk != 2.0f)
                light_ok = 0;
        }
        CHECK(uv_ok, "white Shulker Box samples only shulker_white.png");
        CHECK(light_ok, "Shulker Box retains tile lightmap coordinates");
    }

    box.color = 15;
    n = gm_shulker_boxes_emit(&box, 1, 1.0f, open, 144);
    {
        const CrMobSprite *sprite =
            &CR_MOB_SPRITES[CR_MOB_SHULKER_BOX_BLACK];
        int uv_ok = n == 144;
        for (int i = 0; i < n; ++i) {
            float tx = open[i].uv.x * CR_MOB_ATLAS_W;
            float ty = open[i].uv.y * CR_MOB_ATLAS_H;
            if (tx < sprite->x0 || tx > sprite->x1
                    || ty < sprite->y0 || ty > sprite->y1)
                uv_ok = 0;
        }
        CHECK(uv_ok, "dye metadata selects the matching Shulker Box texture");
    }

    box.color = 10;
    for (int facing = 0; facing < 6; ++facing) {
        box.facing = facing;
        box.prev_progress = box.progress = 1.0f;
        n = gm_shulker_boxes_emit(&box, 1, 1.0f, open, 144);
        bounds(open, n, mn, mx);
        if (facing == 0)
            CHECK(mn[1] < 69.51f && mn[1] > 69.49f,
                  "DOWN-facing open lid extends one half-block downward");
        else if (facing == 1)
            CHECK(mx[1] > 71.49f && mx[1] < 71.51f,
                  "UP-facing open lid extends one half-block upward");
        else if (facing == 2)
            CHECK(mn[2] < -4.49f && mn[2] > -4.51f,
                  "NORTH-facing open lid extends one half-block north");
        else if (facing == 3)
            CHECK(mx[2] > -2.51f && mx[2] < -2.49f,
                  "SOUTH-facing open lid extends one half-block south");
        else if (facing == 4)
            CHECK(mn[0] < 2.51f && mn[0] > 2.49f,
                  "WEST-facing open lid extends one half-block west");
        else
            CHECK(mx[0] > 4.49f && mx[0] < 4.51f,
                  "EAST-facing open lid extends one half-block east");
    }
}

static void test_beacon_render(void) {
    GmBeaconBeamSegmentView segments[2] = {
        {0.85f, 0.5f, 0.2f, 2},
        {0.525f, 0.4f, 0.45f, 3},
    };
    GmBeaconRenderView view = {
        10, 64, -2, 0.5f, 100.0, 2, segments
    };
    CrVertex inner[96], glow[96];
    int ng = -1;
    CHECK(gm_beacon_beams_emit(
              &view, 1, 1.0f, inner, 47, glow, 96, &ng) == 0
              && ng == 0,
          "Beacon beam segment capacity is atomic");
    int ni = gm_beacon_beams_emit(
        &view, 1, 1.0f, inner, 96, glow, 96, &ng);
    CHECK(ni == 96 && ng == 96,
          "two Beacon segments emit four two-sided core and glow quads each");
    {
        double d0 = 101.0;
        double angle = d0 * 0.025 * -1.5;
        double d1 = -d0;
        double d2 = d1 * 0.2 - floor(d1 * 0.1);
        d2 -= floor(d2);
        double x = 10.5 + cos(angle + 2.356194490192345) * 0.2;
        double z = -1.5 + sin(angle + 2.356194490192345) * 0.2;
        double top_v = 2.0 * 0.5 * 2.5 + (-1.0 + d2);
        CHECK(approx(inner[0].pos.x, (float)x, 1e-7f)
                  && inner[0].pos.y == 66.0f
                  && approx(inner[0].pos.z, (float)z, 1e-7f)
                  && inner[0].uv.x == 1.0f
                  && approx(inner[0].uv.y, (float)top_v, 1e-7f),
              "Beacon rotating core first vertex matches Java double math");
    }
    CHECK(inner[0].tint.r == 216 && inner[0].tint.g == 127
              && inner[0].tint.b == 51 && inner[0].tint.a == 255,
          "Beacon core colors use VertexBuffer float-to-UBYTE truncation");
    CHECK(glow[0].tint.r == 216 && glow[0].tint.g == 127
              && glow[0].tint.b == 51 && glow[0].tint.a == 31,
          "Beacon glow preserves Java's truncated 0.125 alpha");
    CHECK(inner[48].pos.y == 69.0f && inner[49].pos.y == 66.0f,
          "Beacon segments stack from the prior segment height");
    {
        CrTexture texture = gm_beacon_beam_texture();
        CHECK(texture.w == 16 && texture.h == 16 && texture.texels != NULL,
              "Beacon binds the real standalone 16x16 repeating beam texture");
    }
}

int main(void) {
    test_part_counts();
    test_geometry();
    test_uvs();
    test_winding();
    test_overflow();
    test_render();
    test_name_map();
    test_recorded_state();
    test_slime_magma_size();
    test_fireball_rays_particles();
    test_death_and_spawner();
    test_crystal_beam();
    test_guardian_beam();
    test_hanging_render();
    test_ender_chest_render();
    test_wood_chest_render();
    test_shulker_box_render();
    test_beacon_render();
    test_wither_render();
    test_skeleton_render();
    test_snowman_render();
    test_endermite_render();
    test_polar_bear_render();
    test_rabbit_render();
    test_bat_render();
    test_squid_render();
    test_horse_render();
    test_llama_render();
    printf("\n%s\n", g_fail ? "*** SOME TESTS FAILED ***" : "ALL TESTS PASSED");
    return g_fail;
}
