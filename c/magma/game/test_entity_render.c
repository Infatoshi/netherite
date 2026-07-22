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

/* expected verts per type: 36 * vanilla part count. */
typedef struct { int type; const char *name; int verts; int sprites[2]; } TypeSpec;
static const TypeSpec SPECS[] = {
    { 2,  "zombie",   7  * 36, { CR_MOB_ZOMBIE,   -1 } },  /* + headwear */
    { 3,  "skeleton", 6  * 36, { CR_MOB_SKELETON, -1 } },
    { 4,  "creeper",  6  * 36, { CR_MOB_CREEPER,  -1 } },
    { 5,  "spider",   11 * 36, { CR_MOB_SPIDER,   -1 } },
    { 6,  "enderman", 7  * 36, { CR_MOB_ENDERMAN, -1 } },
    { 7,  "blaze",    13 * 36, { CR_MOB_BLAZE,    -1 } },
    { 10, "sheep",    12 * 36, { CR_MOB_SHEEP, CR_MOB_SHEEP_FUR } },
    { 11, "pig",      7  * 36, { CR_MOB_PIG,      -1 } },
    { 12, "cow",      9  * 36, { CR_MOB_COW,      -1 } },
    { 13, "chicken",  8  * 36, { CR_MOB_CHICKEN,  -1 } },
    { 23, "witch",    14 * 36, { CR_MOB_WITCH,    -1 } },
    { 24, "bat",      9  * 36, { CR_MOB_BAT,      -1 } },
    { 25, "llama",    9  * 36, { CR_MOB_LLAMA,    -1 } },
    { 26, "ghast",    10 * 36, { CR_MOB_GHAST,    -1 } },
    { 27, "magma",    9  * 36, { CR_MOB_MAGMACUBE,-1 } },
    { 28, "minecart", 6  * 36, { CR_MOB_MINECART, -1 } },
    { 32, "wither skeleton", 6 * 36, { CR_MOB_WITHER_SKELETON, -1 } },
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
    /* Unmodeled types keep the legacy 36-vert marker box. */
    GmEntityView mk = { 21, 0, 64, 0, 0, 5 };
    CHECK(gm_entities_emit(&mk, 1, out, MAXV) == 36,
          "xp orb (21) keeps the legacy 36-vert marker box");
    /* Type 9 is the dedicated RenderDragon + ModelDragon transcription: five
     * neck segments, head/jaw, body, paired wings/legs, and 12 tail segments. */
    mk.type = 9;
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

    CrCamera cam;
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
        { "EntitySheep", 10 }, { "EntityZombie", 2 }, { "EntityCreeper", 4 },
        { "EntitySkeleton", 3 }, { "EntityChicken", 13 }, { "EntityCow", 12 },
        { "EntityPig", 11 }, { "EntitySpider", 5 }, { "EntityEnderman", 6 },
        { "EntityBlaze", 7 },
        { "EntityWitherSkeleton", 32 },
        { "EntitySmallFireball", 30 },
        { "EntityDragonFireball", 33 },
        /* skin variants fold to the base silhouette */
        { "EntityHusk", 2 }, { "EntityZombieVillager", 2 },
        { "EntityPigZombie", 2 }, { "EntityStray", 3 },
        { "EntityCaveSpider", 5 }, { "EntityMooshroom", 12 },
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
    CHECK(gm_entity_type_for_name("EntityGhast") == 26, "EntityGhast -> 26");
    CHECK(gm_entity_type_for_name("EntityMagmaCube") == 27, "EntityMagmaCube -> 27");
    CHECK(gm_entity_type_for_name("EntityMinecartChest") == 28,
          "EntityMinecartChest -> 28");
    /* skin-variant sprite overrides */
    CHECK(gm_entity_skin_for_name("EntityPigZombie") == CR_MOB_PIGMAN + 1,
          "EntityPigZombie skin -> pigman sprite");
    CHECK(gm_entity_skin_for_name("EntityZombie") == 0, "EntityZombie skin -> 0");
    static const char *NEG[] = { "EntityItem", "EntityXPOrb",
                                 "EntityFallingBlock", "EntityNoSuchThing" };
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
    sheep.flags=4;
    CHECK(gm_entities_emit(&sheep,1,out,MAXV)==0,
          "recorded invisible entity emits no geometry");
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
    printf("\n%s\n", g_fail ? "*** SOME TESTS FAILED ***" : "ALL TESTS PASSED");
    return g_fail;
}
