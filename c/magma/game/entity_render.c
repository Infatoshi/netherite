/* game/entity_render.c - owner: ENTITY-RENDER agent.
 *
 * Emits vanilla-faithful multi-box mob models as world-space CrVertex triangle
 * lists, bound with gm_entity_atlas() (real MC skins at native resolution in
 * assets/mob_atlas.h). Box dims, texture offsets and rotation points are
 * transcribed from the decompiled 1.11.2 oracle models
 * (java/oracle-src/net/minecraft/client/model/Model*.java).
 *
 * COORDINATES: vanilla model space is Y-DOWN with the ground plane at y=24 and
 * X mirrored (RenderLivingBase does GlStateManager.scale(-1,-1,1)); model units
 * are 1/16 block. We map model (mx,my,mz) -> world (-mx/16, (24-my)/16, mz/16),
 * which is orientation-PRESERVING (two reflections), then rotate the whole
 * model about Y by (180 - yaw) exactly like vanilla applyRotations
 * (GlStateManager.rotate(180 - yaw, 0, 1, 0)), then translate to entity feet.
 *
 * UV NET: per-face texel rects follow vanilla ModelBox exactly. For a box with
 * texture offset (u,v) and dims (W=dx, H=dy, D=dz):
 *   top    (u+D,     v)   .. (u+D+W,     v+D)
 *   bottom (u+D+W,   v+D) .. (u+D+W+W,   v)      (V flipped)
 *   right  (u,       v+D) .. (u+D,       v+D+H)  (model -X face)
 *   front  (u+D,     v+D) .. (u+D+W,     v+D+H)  (model -Z face)
 *   left   (u+D+W,   v+D) .. (u+D+W+D,   v+D+H)  (model +X face)
 *   back   (u+D+W+D, v+D) .. (u+D+W+D+W, v+D+H)  (model +Z face)
 * with the exact per-vertex corner assignment from TexturedQuad. UVs are
 * computed in skin-texel space and offset into the packed atlas by each
 * sprite's rect (CR_MOB_SPRITES[i].x0/y0 + native w/h).
 *
 * WINDING: quads must come out CCW-seen-from-outside (world/mesh_mc.c FACES
 * convention, CR_FRONT_SIGN) or the rasterizer backface-culls them. Instead of
 * trusting the ported quad orderings through rotations, each transformed quad
 * is checked against the box's world center (dot(normal, centroid-center)) and
 * reversed if it faces inward. Boxes are convex so this is exact.
 *
 * Entity type ids are EW_TYPE_* from c/mc-sim/core/ew_entity_store.h +
 * entity_hostile_spine.h + game/mob_live.h (hardcoded below to avoid an mc-sim
 * include dependency in the render path):
 *   2 zombie, 3 skeleton, 4 creeper, 5 spider, 6 enderman, 7 blaze,
 *   10 sheep, 11 pig, 12 cow, 13 chicken -> table-driven full models.
 *   8 crystal, 9 dragon -> dedicated full render paths below.
 *   0 NONE / 1 PLAYER -> skipped.
 *   anything else (20 projectile, 21 xp orb, ...) keeps
 *   the legacy single 0.6x1.8x0.4 zombie-wrapped marker box (previous behavior
 *   for unmodeled types, preserved).
 */
#include "game/entity_render.h"
#include "assets/mob_atlas.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* EW_TYPE_* / GM_MOB_* ids (see file comment). */
#define ER_TYPE_NONE     0
#define ER_TYPE_PLAYER   1
#define ER_TYPE_ZOMBIE   2
#define ER_TYPE_SKELETON 3
#define ER_TYPE_CREEPER  4
#define ER_TYPE_SPIDER   5
#define ER_TYPE_ENDERMAN 6
#define ER_TYPE_BLAZE    7
#define ER_TYPE_SHEEP    10
#define ER_TYPE_PIG      11
#define ER_TYPE_COW      12
#define ER_TYPE_CHICKEN  13
#define ER_TYPE_SQUID    14
/* render-only types (no EW/live-sim id; tape ghost views only). 22 is
 * GM_VIEW_ITEM in game.h. */
#define ER_TYPE_WITCH    23
#define ER_TYPE_BAT      24
#define ER_TYPE_LLAMA    25
#define ER_TYPE_GHAST    26
#define ER_TYPE_MAGMA    27
#define ER_TYPE_MINECART 28
#define ER_TYPE_ARROW    29
#define ER_TYPE_CRYSTAL  31   /* EntityEnderCrystal (30 = GM_VIEW_BILLBOARD) */
#define ER_TYPE_WITHER_SKELETON 32

#define ER_VERTS_PER_BOX 36  /* 6 faces * 2 tris * 3 verts */
#define ER_PI 3.14159265358979323846f
#define ER_DEG2RAD 0.017453292519943295f
#define ER_MAX_PARTS 20

/* One vanilla ModelRenderer.addBox() worth of geometry. All values in model
 * units (1/16 block), model space Y-down, ground at y=24. Rotations are the
 * ModelRenderer rotateAngle* (radians), applied X then Y then Z about the
 * rotation point (GL order translate,rotZ,rotY,rotX => vertex sees X first). */
typedef struct {
    int   sprite;              /* CR_MOB_* atlas sprite */
    int   u, v;                /* texture offset (texels, skin space) */
    float x, y, z;             /* box origin relative to rotation point */
    int   dx, dy, dz;          /* box dims (texels == model units) */
    float rx, ry, rz;          /* rotation point */
    float ax, ay, az;          /* rotateAngleX/Y/Z, radians */
    float delta;               /* box inflation (ModelBox delta) */
    int   mirror;              /* ModelRenderer.mirror (left limbs) */
} ErPart;

typedef struct {
    int    nparts;
    ErPart parts[ER_MAX_PARTS];
    float  scale;   /* RenderLivingBase preRenderCallback scale; 0 == 1.0 */
} ErModel;

/* -------------------------------------------------------------------------- */
/* Part tables, transcribed from the oracle model constructors.               */

#define ARM_DOWN 0.0f
#define ZOMBIE_ARM (-ER_PI / 2.25f)  /* ModelZombie f2, arms raised forward */

/* ModelBiped (ModelZombie, 64x64 skin): head/body/arms/legs + bipedHeadwear
 * hat overlay (u32,v0 delta+0.5, part 6 - copies head rotation). The hat
 * layer is load-bearing for the pigman skin: its base head is the decayed
 * skull half and the pink flesh face lives ONLY on the overlay. */
static const ErModel M_ZOMBIE = { 7, {
    { CR_MOB_ZOMBIE,  0,  0, -4,-8,-4, 8, 8,8,  0.0f, 0,0,  0,0,0, 0,0 },
    { CR_MOB_ZOMBIE, 16, 16, -4, 0,-2, 8,12,4,  0.0f, 0,0,  0,0,0, 0,0 },
    { CR_MOB_ZOMBIE, 40, 16, -3,-2,-2, 4,12,4, -5.0f, 2,0,  ZOMBIE_ARM,0,0, 0,0 },
    { CR_MOB_ZOMBIE, 40, 16, -1,-2,-2, 4,12,4,  5.0f, 2,0,  ZOMBIE_ARM,0,0, 0,1 },
    { CR_MOB_ZOMBIE,  0, 16, -2, 0,-2, 4,12,4, -1.9f,12,0,  0,0,0, 0,0 },
    { CR_MOB_ZOMBIE,  0, 16, -2, 0,-2, 4,12,4,  1.9f,12,0,  0,0,0, 0,1 },
    { CR_MOB_ZOMBIE, 32,  0, -4,-8,-4, 8, 8,8,  0.0f, 0,0,  0,0,0, 0.5f,0 },
} };

/* ModelSkeleton (64x32): biped head/body, thin 2x12x2 limbs. */
static const ErModel M_SKELETON = { 6, {
    { CR_MOB_SKELETON,  0,  0, -4,-8,-4, 8, 8,8,  0.0f, 0,0,  0,0,0, 0,0 },
    { CR_MOB_SKELETON, 16, 16, -4, 0,-2, 8,12,4,  0.0f, 0,0,  0,0,0, 0,0 },
    { CR_MOB_SKELETON, 40, 16, -1,-2,-1, 2,12,2, -5.0f, 2,0,  ARM_DOWN,0,0, 0,0 },
    { CR_MOB_SKELETON, 40, 16, -1,-2,-1, 2,12,2,  5.0f, 2,0,  ARM_DOWN,0,0, 0,1 },
    { CR_MOB_SKELETON,  0, 16, -1, 0,-1, 2,12,2, -2.0f,12,0,  0,0,0, 0,0 },
    { CR_MOB_SKELETON,  0, 16, -1, 0,-1, 2,12,2,  2.0f,12,0,  0,0,0, 0,1 },
} };

/* RenderWitherSkeleton: ModelSkeleton with its own 64x32 skin and a 1.2x
 * preRenderCallback scale. */
static const ErModel M_WITHER_SKELETON = { .nparts = 6, .scale = 1.2f, .parts = {
    { CR_MOB_WITHER_SKELETON,  0,  0, -4,-8,-4, 8, 8,8,  0.0f, 0,0,  0,0,0, 0,0 },
    { CR_MOB_WITHER_SKELETON, 16, 16, -4, 0,-2, 8,12,4,  0.0f, 0,0,  0,0,0, 0,0 },
    { CR_MOB_WITHER_SKELETON, 40, 16, -1,-2,-1, 2,12,2, -5.0f, 2,0,  ARM_DOWN,0,0, 0,0 },
    { CR_MOB_WITHER_SKELETON, 40, 16, -1,-2,-1, 2,12,2,  5.0f, 2,0,  ARM_DOWN,0,0, 0,1 },
    { CR_MOB_WITHER_SKELETON,  0, 16, -1, 0,-1, 2,12,2, -2.0f,12,0,  0,0,0, 0,0 },
    { CR_MOB_WITHER_SKELETON,  0, 16, -1, 0,-1, 2,12,2,  2.0f,12,0,  0,0,0, 0,1 },
} };

/* ModelCreeper (64x32): head + body + 4 stumpy legs. */
static const ErModel M_CREEPER = { 6, {
    { CR_MOB_CREEPER,  0,  0, -4,-8,-4, 8, 8,8,  0, 6, 0,  0,0,0, 0,0 },
    { CR_MOB_CREEPER, 16, 16, -4, 0,-2, 8,12,4,  0, 6, 0,  0,0,0, 0,0 },
    { CR_MOB_CREEPER,  0, 16, -2, 0,-2, 4, 6,4, -2,18, 4,  0,0,0, 0,0 },
    { CR_MOB_CREEPER,  0, 16, -2, 0,-2, 4, 6,4,  2,18, 4,  0,0,0, 0,0 },
    { CR_MOB_CREEPER,  0, 16, -2, 0,-2, 4, 6,4, -2,18,-4,  0,0,0, 0,0 },
    { CR_MOB_CREEPER,  0, 16, -2, 0,-2, 4, 6,4,  2,18,-4,  0,0,0, 0,0 },
} };

/* ModelSpider (64x32): head+neck+body + 8 splayed legs (static idle pose). */
#define SPL_A (ER_PI / 4.0f)      /* legs 1,2,7,8 roll */
#define SPL_B 0.58119464f         /* legs 3,4,5,6 roll */
#define SPY_A (ER_PI / 4.0f)      /* legs 1,2,7,8 yaw */
#define SPY_B 0.3926991f          /* legs 3,4,5,6 yaw */
static const ErModel M_SPIDER = { 11, {
    { CR_MOB_SPIDER, 32,  4, -4,-4,-8,  8,8, 8,  0,15,-3,  0,0,0, 0,0 },
    { CR_MOB_SPIDER,  0,  0, -3,-3,-3,  6,6, 6,  0,15, 0,  0,0,0, 0,0 },
    { CR_MOB_SPIDER,  0, 12, -5,-4,-6, 10,8,12,  0,15, 9,  0,0,0, 0,0 },
    { CR_MOB_SPIDER, 18,  0,-15,-1,-1, 16,2, 2, -4,15, 2,  0, SPY_A,-SPL_A, 0,0 },
    { CR_MOB_SPIDER, 18,  0, -1,-1,-1, 16,2, 2,  4,15, 2,  0,-SPY_A, SPL_A, 0,0 },
    { CR_MOB_SPIDER, 18,  0,-15,-1,-1, 16,2, 2, -4,15, 1,  0, SPY_B,-SPL_B, 0,0 },
    { CR_MOB_SPIDER, 18,  0, -1,-1,-1, 16,2, 2,  4,15, 1,  0,-SPY_B, SPL_B, 0,0 },
    { CR_MOB_SPIDER, 18,  0,-15,-1,-1, 16,2, 2, -4,15, 0,  0,-SPY_B,-SPL_B, 0,0 },
    { CR_MOB_SPIDER, 18,  0, -1,-1,-1, 16,2, 2,  4,15, 0,  0, SPY_B, SPL_B, 0,0 },
    { CR_MOB_SPIDER, 18,  0,-15,-1,-1, 16,2, 2, -4,15,-1,  0,-SPY_A,-SPL_A, 0,0 },
    { CR_MOB_SPIDER, 18,  0, -1,-1,-1, 16,2, 2,  4,15,-1,  0, SPY_A, SPL_A, 0,0 },
} };

/* ModelEnderman (64x32): tall biped, head + jaw overlay + thin 30-long limbs. */
static const ErModel M_ENDERMAN = { 7, {
    { CR_MOB_ENDERMAN,  0,  0, -4,-8,-4, 8, 8,8,  0,-14,0,  0,0,0,  0.0f,0 },
    { CR_MOB_ENDERMAN,  0, 16, -4,-8,-4, 8, 8,8,  0,-14,0,  0,0,0, -0.5f,0 },
    { CR_MOB_ENDERMAN, 32, 16, -4, 0,-2, 8,12,4,  0,-14,0,  0,0,0,  0.0f,0 },
    { CR_MOB_ENDERMAN, 56,  0, -1,-2,-1, 2,30,2, -3,-12,0,  0,0,0,  0.0f,0 },
    { CR_MOB_ENDERMAN, 56,  0, -1,-2,-1, 2,30,2,  5,-12,0,  0,0,0,  0.0f,1 },
    { CR_MOB_ENDERMAN, 56,  0, -1, 0,-1, 2,30,2, -2, -2,0,  0,0,0,  0.0f,0 },
    { CR_MOB_ENDERMAN, 56,  0, -1, 0,-1, 2,30,2,  2, -2,0,  0,0,0,  0.0f,1 },
} };

/* ModelQuadruped bodies have rotateAngleX = +pi/2 baked by the ax field. */
#define QUAD_BODY_ROT (ER_PI / 2.0f)

/* ModelPig (64x32): quadruped height 6 + snout. */
static const ErModel M_PIG = { 7, {
    { CR_MOB_PIG,  0,  0, -4, -4,-8,  8, 8,8,  0,12,-6,  0,0,0, 0,0 },
    { CR_MOB_PIG, 16, 16, -2,  0,-9,  4, 3,1,  0,12,-6,  0,0,0, 0,0 },
    { CR_MOB_PIG, 28,  8, -5,-10,-7, 10,16,8,  0,11, 2,  QUAD_BODY_ROT,0,0, 0,0 },
    { CR_MOB_PIG,  0, 16, -2,  0,-2,  4, 6,4, -3,18, 7,  0,0,0, 0,0 },
    { CR_MOB_PIG,  0, 16, -2,  0,-2,  4, 6,4,  3,18, 7,  0,0,0, 0,0 },
    { CR_MOB_PIG,  0, 16, -2,  0,-2,  4, 6,4, -3,18,-5,  0,0,0, 0,0 },
    { CR_MOB_PIG,  0, 16, -2,  0,-2,  4, 6,4,  3,18,-5,  0,0,0, 0,0 },
} };

/* ModelCow (64x32): head + horns + rotated body + udder + 4 tall legs. */
static const ErModel M_COW = { 9, {
    { CR_MOB_COW,  0,  0, -4, -4,-6,  8, 8, 6,  0, 4,-8,  0,0,0, 0,0 },
    { CR_MOB_COW, 22,  0, -5, -5,-4,  1, 3, 1,  0, 4,-8,  0,0,0, 0,0 },
    { CR_MOB_COW, 22,  0,  4, -5,-4,  1, 3, 1,  0, 4,-8,  0,0,0, 0,0 },
    { CR_MOB_COW, 18,  4, -6,-10,-7, 12,18,10,  0, 5, 2,  QUAD_BODY_ROT,0,0, 0,0 },
    { CR_MOB_COW, 52,  0, -2,  2,-8,  4, 6, 1,  0, 5, 2,  QUAD_BODY_ROT,0,0, 0,0 },
    { CR_MOB_COW,  0, 16, -2,  0,-2,  4,12, 4, -3,12, 7,  0,0,0, 0,0 },
    { CR_MOB_COW,  0, 16, -2,  0,-2,  4,12, 4,  3,12, 7,  0,0,0, 0,0 },
    { CR_MOB_COW,  0, 16, -2,  0,-2,  4,12, 4, -3,12,-5,  0,0,0, 0,0 },
    { CR_MOB_COW,  0, 16, -2,  0,-2,  4,12, 4,  3,12,-5,  0,0,0, 0,0 },
} };

/* ModelSheep2 (skin, sheep.png) + ModelSheep1 wool overlay (sheep_fur.png). */
static const ErModel M_SHEEP = { 12, {
    { CR_MOB_SHEEP,      0,  0, -3, -4,-6, 6, 6,8,  0, 6,-8,  0,0,0, 0.0f,0 },
    { CR_MOB_SHEEP,     28,  8, -4,-10,-7, 8,16,6,  0, 5, 2,  QUAD_BODY_ROT,0,0, 0.0f,0 },
    { CR_MOB_SHEEP,      0, 16, -2,  0,-2, 4,12,4, -3,12, 7,  0,0,0, 0.0f,0 },
    { CR_MOB_SHEEP,      0, 16, -2,  0,-2, 4,12,4,  3,12, 7,  0,0,0, 0.0f,0 },
    { CR_MOB_SHEEP,      0, 16, -2,  0,-2, 4,12,4, -3,12,-5,  0,0,0, 0.0f,0 },
    { CR_MOB_SHEEP,      0, 16, -2,  0,-2, 4,12,4,  3,12,-5,  0,0,0, 0.0f,0 },
    { CR_MOB_SHEEP_FUR,  0,  0, -3, -4,-4, 6, 6,6,  0, 6,-8,  0,0,0, 0.60f,0 },
    { CR_MOB_SHEEP_FUR, 28,  8, -4,-10,-7, 8,16,6,  0, 5, 2,  QUAD_BODY_ROT,0,0, 1.75f,0 },
    { CR_MOB_SHEEP_FUR,  0, 16, -2,  0,-2, 4, 6,4, -3,12, 7,  0,0,0, 0.50f,0 },
    { CR_MOB_SHEEP_FUR,  0, 16, -2,  0,-2, 4, 6,4,  3,12, 7,  0,0,0, 0.50f,0 },
    { CR_MOB_SHEEP_FUR,  0, 16, -2,  0,-2, 4, 6,4, -3,12,-5,  0,0,0, 0.50f,0 },
    { CR_MOB_SHEEP_FUR,  0, 16, -2,  0,-2, 4, 6,4,  3,12,-5,  0,0,0, 0.50f,0 },
} };

/* ModelChicken (64x32): head+bill+chin, rotated body, legs, wings. */
static const ErModel M_CHICKEN = { 8, {
    { CR_MOB_CHICKEN,  0,  0, -2,-6,-2, 4,6,3,  0,15,-4,  0,0,0, 0,0 },
    { CR_MOB_CHICKEN, 14,  0, -2,-4,-4, 4,2,2,  0,15,-4,  0,0,0, 0,0 },
    { CR_MOB_CHICKEN, 14,  4, -1,-2,-3, 2,2,2,  0,15,-4,  0,0,0, 0,0 },
    { CR_MOB_CHICKEN,  0,  9, -3,-4,-3, 6,8,6,  0,16, 0,  QUAD_BODY_ROT,0,0, 0,0 },
    { CR_MOB_CHICKEN, 26,  0, -1, 0,-3, 3,5,3, -2,19, 1,  0,0,0, 0,0 },
    { CR_MOB_CHICKEN, 26,  0, -1, 0,-3, 3,5,3,  1,19, 1,  0,0,0, 0,0 },
    { CR_MOB_CHICKEN, 24, 13,  0, 0,-3, 1,4,6, -4,13, 0,  0,0,0, 0,0 },
    { CR_MOB_CHICKEN, 24, 13, -1, 0,-3, 1,4,6,  4,13, 0,  0,0,0, 0,0 },
} };

/* ModelSquid (64x32): body 12x16x12 + 8 tentacles 2x18x2 around a ring.
 * rotationPointY of body is +8 (constructor); tentacles at Y=15, ring r=5.
 * Tentacle rotateAngleY fixed at construction; rotateAngleX = ageInTicks
 * (static pose uses 0). */
static ErModel g_squid;
static int     g_squid_init;
static void squid_build(void) {
    int n = 0;
    /* body: addBox(-6,-8,-6, 12,16,12); rotationPointY += 8 */
    g_squid.parts[n++] = (ErPart){ CR_MOB_SQUID, 0, 0, -6, -8, -6, 12, 16, 12,
                                   0, 8, 0,  0, 0, 0,  0, 0 };
    for (int j = 0; j < 8; ++j) {
        float d0 = (float)j * ER_PI * 2.0f / 8.0f;
        float f  = cosf(d0) * 5.0f;
        float f1 = sinf(d0) * 5.0f;
        float d1 = (float)j * ER_PI * -2.0f / 8.0f + ER_PI / 2.0f;
        g_squid.parts[n++] = (ErPart){ CR_MOB_SQUID, 48, 0, -1, 0, -1, 2, 18, 2,
                                       f, 15.0f, f1,  0, d1, 0,  0, 0 };
    }
    g_squid.nparts = n;
    g_squid_init = 1;
}

/* ModelBlaze (64x32): floating head + 12 orbiting 2x8x2 rods; rod rotation
 * points come from ModelBlaze.setRotationAngles with ageInTicks pinned to 0
 * (static pose). Built once at first use. */
static ErModel g_blaze;
static int     g_blaze_init;

static void blaze_build(void) {
    int n = 0;
    g_blaze.parts[n++] = (ErPart){ CR_MOB_BLAZE, 0, 0, -4,-4,-4, 8,8,8,
                                   0,0,0, 0,0,0, 0,0 };
    float f = 0.0f;
    for (int i = 0; i < 4; ++i, f += 1.0f) {
        float ry = -2.0f + cosf((float)(i * 2) * 0.25f);
        g_blaze.parts[n++] = (ErPart){ CR_MOB_BLAZE, 0, 16, 0,0,0, 2,8,2,
                                       cosf(f) * 9.0f, ry, sinf(f) * 9.0f,
                                       0,0,0, 0,0 };
    }
    f = ER_PI / 4.0f;
    for (int j = 4; j < 8; ++j, f += 1.0f) {
        float ry = 2.0f + cosf((float)(j * 2) * 0.25f);
        g_blaze.parts[n++] = (ErPart){ CR_MOB_BLAZE, 0, 16, 0,0,0, 2,8,2,
                                       cosf(f) * 7.0f, ry, sinf(f) * 7.0f,
                                       0,0,0, 0,0 };
    }
    f = 0.47123894f;
    for (int k = 8; k < 12; ++k, f += 1.0f) {
        float ry = 11.0f + cosf((float)k * 1.5f * 0.5f);
        g_blaze.parts[n++] = (ErPart){ CR_MOB_BLAZE, 0, 16, 0,0,0, 2,8,2,
                                       cosf(f) * 5.0f, ry, sinf(f) * 5.0f,
                                       0,0,0, 0,0 };
    }
    g_blaze.nparts = n;
    g_blaze_init = 1;
}

/* ModelWitch (ModelVillager base, 64x128, render scale 0.9375): head+nose+
 * mole, 4 stacked hat boxes (nested children flattened - cumulative rotation
 * points, own small tilts kept, parent tilt composition dropped), body+robe,
 * crossed arms (villager pose ax=-0.75 rp(0,3,-1)), legs. Head kept static
 * (nose/hat rotation points differ, a flat table cannot share the pivot). */
#define WITCH_ARM_AX (-0.75f)
static const ErModel M_WITCH = { .nparts = 14, .scale = 0.9375f, .parts = {
    { CR_MOB_WITCH,  0,  0, -4,-10,-4,    8,10,8,  0,0,0,       0,0,0, 0,0 },
    { CR_MOB_WITCH, 24,  0, -1, -1,-6,    2, 4,2,  0,-2,0,      0,0,0, 0,0 },
    { CR_MOB_WITCH,  0,  0,  0,  3,-6.75f,1, 1,1,  0,-4,0,      0,0,0, -0.25f,0 },
    { CR_MOB_WITCH,  0, 64,  0,  0, 0,   10, 2,10, -5,-10.03125f,-5, 0,0,0, 0,0 },
    { CR_MOB_WITCH,  0, 76,  0,  0, 0,    7, 4, 7, -3.25f,-14.03125f,-3, -0.05235988f,0,0.02617994f, 0,0 },
    { CR_MOB_WITCH,  0, 87,  0,  0, 0,    4, 4, 4, -1.5f,-18.03125f,-1, -0.10471976f,0,0.05235988f, 0,0 },
    { CR_MOB_WITCH,  0, 95,  0,  0, 0,    1, 2, 1,  0.25f,-20.03125f,1, -0.20943952f,0,0.10471976f, 0.25f,0 },
    { CR_MOB_WITCH, 16, 20, -4,  0,-3,    8,12,6,  0,0,0,       0,0,0, 0,0 },
    { CR_MOB_WITCH,  0, 38, -4,  0,-3,    8,18,6,  0,0,0,       0,0,0, 0.5f,0 },
    { CR_MOB_WITCH, 44, 22, -8, -2,-2,    4, 8,4,  0,3,-1,      WITCH_ARM_AX,0,0, 0,0 },
    { CR_MOB_WITCH, 44, 22,  4, -2,-2,    4, 8,4,  0,3,-1,      WITCH_ARM_AX,0,0, 0,0 },
    { CR_MOB_WITCH, 40, 38, -4,  2,-2,    8, 4,4,  0,3,-1,      WITCH_ARM_AX,0,0, 0,0 },
    { CR_MOB_WITCH,  0, 22, -2,  0,-2,    4,12,4, -2,12,0,      0,0,0, 0,0 },
    { CR_MOB_WITCH,  0, 22, -2,  0,-2,    4,12,4,  2,12,0,      0,0,0, 0,1 },
} };

/* ModelBat (64x64, render scale 0.35), FLYING pose (setRotationAngles else-
 * branch, flap phase applied per frame from view age in gm_entities_emit):
 * head+ears at rp0, body ax=pi/4 (+tail box), wings as flattened children. */
#define BAT_BODY_AX (ER_PI / 4.0f)
static const ErModel M_BAT = { .nparts = 9, .scale = 0.35f, .parts = {
    { CR_MOB_BAT,  0,  0,  -3,-3,-3,    6, 6,6,  0,0,0,        0,0,0, 0,0 },
    { CR_MOB_BAT, 24,  0,  -4,-6,-2,    3, 4,1,  0,0,0,        0,0,0, 0,0 },
    { CR_MOB_BAT, 24,  0,   1,-6,-2,    3, 4,1,  0,0,0,        0,0,0, 0,1 },
    { CR_MOB_BAT,  0, 16,  -3, 4,-3,    6,12,6,  0,0,0,        BAT_BODY_AX,0,0, 0,0 },
    { CR_MOB_BAT,  0, 34,  -5,16, 0,   10, 6,1,  0,0,0,        BAT_BODY_AX,0,0, 0,0 },
    { CR_MOB_BAT, 42,  0, -12, 1, 1.5f,10,16,1,  0,0,0,        BAT_BODY_AX,0,0, 0,0 },
    { CR_MOB_BAT, 24, 16,  -8, 1, 0,    8,12,1, -12,1,1.5f,    BAT_BODY_AX,0,0, 0,0 },
    { CR_MOB_BAT, 42,  0,   2, 1, 1.5f,10,16,1,  0,0,0,        BAT_BODY_AX,0,0, 0,1 },
    { CR_MOB_BAT, 24, 16,   0, 1, 0,    8,12,1,  12,1,1.5f,    BAT_BODY_AX,0,0, 0,1 },
} };

/* ModelLlama (128x64, llama_creamy variant): 4 head boxes at rp(0,7,-6),
 * quadruped body ax=pi/2, 4 tall legs (chest boxes omitted - variant llamas
 * in the tapes are wild). Head parts 0-3 share the pivot so tape head
 * yaw/pitch applies to all four. */
static const ErModel M_LLAMA = { .nparts = 9, .parts = {
    { CR_MOB_LLAMA,  0,  0, -2,-14,-10,  4, 4,9,  0, 7,-6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA,  0, 14, -4,-16, -6,  8,18,6,  0, 7,-6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 17,  0, -4,-19, -4,  3, 3,2,  0, 7,-6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 17,  0,  1,-19, -4,  3, 3,2,  0, 7,-6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 29,  0, -6,-10, -7, 12,18,10, 0, 5, 2,  QUAD_BODY_ROT,0,0, 0,0 },
    { CR_MOB_LLAMA, 29, 29, -2,  0, -2,  4,14,4, -3.5f,10, 6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 29, 29, -2,  0, -2,  4,14,4,  3.5f,10, 6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 29, 29, -2,  0, -2,  4,14,4, -3.5f,10,-5,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 29, 29, -2,  0, -2,  4,14,4,  3.5f,10,-5,  0,0,0, 0,0 },
} };

/* ModelGhast (64x32, render scale 4.5): 16^3 body + 9 tentacles. The model
 * render()'s translate(0,0.6,0) is baked as +9.6 texels on every rotation
 * point. Tentacle lengths are vanilla's fixed java.util.Random(1660) draws;
 * tentacle wave (ax = 0.2*sin(age*0.3+i)+0.4) is applied per frame. */
#define GHAST_TENT_AX 0.4f
static const ErModel M_GHAST = { .nparts = 10, .scale = 4.5f, .parts = {
    { CR_MOB_GHAST, 0, 0, -8,-8,-8, 16,16,16,  0,17.6f,0,  0,0,0, 0,0 },
    { CR_MOB_GHAST, 0, 0, -1, 0,-1,  2, 8, 2, -3.75f,24.6f,-5,  GHAST_TENT_AX,0,0, 0,0 },
    { CR_MOB_GHAST, 0, 0, -1, 0,-1,  2,13, 2,  1.25f,24.6f,-5,  GHAST_TENT_AX,0,0, 0,0 },
    { CR_MOB_GHAST, 0, 0, -1, 0,-1,  2, 9, 2,  6.25f,24.6f,-5,  GHAST_TENT_AX,0,0, 0,0 },
    { CR_MOB_GHAST, 0, 0, -1, 0,-1,  2,11, 2, -6.25f,24.6f, 0,  GHAST_TENT_AX,0,0, 0,0 },
    { CR_MOB_GHAST, 0, 0, -1, 0,-1,  2,11, 2, -1.25f,24.6f, 0,  GHAST_TENT_AX,0,0, 0,0 },
    { CR_MOB_GHAST, 0, 0, -1, 0,-1,  2,10, 2,  3.75f,24.6f, 0,  GHAST_TENT_AX,0,0, 0,0 },
    { CR_MOB_GHAST, 0, 0, -1, 0,-1,  2,12, 2, -3.75f,24.6f, 5,  GHAST_TENT_AX,0,0, 0,0 },
    { CR_MOB_GHAST, 0, 0, -1, 0,-1,  2, 9, 2,  1.25f,24.6f, 5,  GHAST_TENT_AX,0,0, 0,0 },
    { CR_MOB_GHAST, 0, 0, -1, 0,-1,  2,12, 2,  6.25f,24.6f, 5,  GHAST_TENT_AX,0,0, 0,0 },
} };

/* ModelMagmaCube (64x32): 8 stacked 8x1x8 segments + 4^3 core. Rendered at
 * slime size 2 (fortress magma cubes are mostly smalls/mediums; the tape does
 * not record getSlimeSize). Squish animation omitted. */
static const ErModel M_MAGMA = { .nparts = 9, .scale = 2.0f, .parts = {
    { CR_MOB_MAGMACUBE,  0,  0, -4,16,-4, 8,1,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_MAGMACUBE,  0,  1, -4,17,-4, 8,1,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_MAGMACUBE, 24, 10, -4,18,-4, 8,1,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_MAGMACUBE, 24, 19, -4,19,-4, 8,1,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_MAGMACUBE,  0,  4, -4,20,-4, 8,1,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_MAGMACUBE,  0,  5, -4,21,-4, 8,1,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_MAGMACUBE,  0,  6, -4,22,-4, 8,1,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_MAGMACUBE,  0,  7, -4,23,-4, 8,1,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_MAGMACUBE,  0, 16, -2,18,-2, 4,4,4,  0,0,0, 0,0,0, 0,0 },
} };

/* ModelMinecart (64x32): bottom plate + 4 sides + floor lining. RenderMinecart
 * translates +0.375 blocks (not the living -1.5): baked as +18 on every
 * rotation point so the shared (24-y)/16 mapping lands the cart on the rail. */
static const ErModel M_MINECART = { .nparts = 6, .parts = {
    { CR_MOB_MINECART,  0, 10, -10,-8,-1, 20,16,2,  0,22, 0,   ER_PI/2.0f,0,0, 0,0 },
    { CR_MOB_MINECART,  0,  0,  -8,-9,-1, 16, 8,2, -9,22, 0,   0,ER_PI*1.5f,0, 0,0 },
    { CR_MOB_MINECART,  0,  0,  -8,-9,-1, 16, 8,2,  9,22, 0,   0,ER_PI/2.0f,0, 0,0 },
    { CR_MOB_MINECART,  0,  0,  -8,-9,-1, 16, 8,2,  0,22,-7,   0,ER_PI,0, 0,0 },
    { CR_MOB_MINECART,  0,  0,  -8,-9,-1, 16, 8,2,  0,22, 7,   0,0,0, 0,0 },
    { CR_MOB_MINECART, 44, 10,  -9,-7,-1, 18,14,1,  0,22, 0,  -ER_PI/2.0f,0,0, 0,0 },
} };

/* Legacy marker box for unmodeled types (dragon/crystal/projectile/xp orb...):
 * one 0.6x1.8x0.4 box wrapped with the whole zombie skin, as before. */
static const ErModel M_MARKER = { 1, {
    { CR_MOB_ZOMBIE, 0, 0, -4.8f,0,-3.2f, 0,0,0,  0,24,0,  0,0,0, 0,0 },
} };
/* (dims 0 flags the special legacy wrap; geometry hardcoded in emit_marker) */

static const ErModel *er_model_for_type(int type) {
    switch (type) {
        case ER_TYPE_NONE:
        case ER_TYPE_PLAYER:   return 0;         /* skipped */
        case 22 /* GM_VIEW_ITEM */: return 0;    /* drawn by the item pass */
        case 30 /* GM_VIEW_BILLBOARD */: return 0; /* item pass (camera-facing) */
        case ER_TYPE_ZOMBIE:   return &M_ZOMBIE;
        case ER_TYPE_SKELETON: return &M_SKELETON;
        case ER_TYPE_WITHER_SKELETON: return &M_WITHER_SKELETON;
        case ER_TYPE_CREEPER:  return &M_CREEPER;
        case ER_TYPE_SPIDER:   return &M_SPIDER;
        case ER_TYPE_ENDERMAN: return &M_ENDERMAN;
        case ER_TYPE_BLAZE:
            if (!g_blaze_init) blaze_build();
            return &g_blaze;
        case ER_TYPE_SHEEP:    return &M_SHEEP;
        case ER_TYPE_PIG:      return &M_PIG;
        case ER_TYPE_COW:      return &M_COW;
        case ER_TYPE_CHICKEN:  return &M_CHICKEN;
        case ER_TYPE_SQUID:
            if (!g_squid_init) squid_build();
            return &g_squid;
        case ER_TYPE_WITCH:    return &M_WITCH;
        case ER_TYPE_BAT:      return &M_BAT;
        case ER_TYPE_LLAMA:    return &M_LLAMA;
        case ER_TYPE_GHAST:    return &M_GHAST;
        case ER_TYPE_MAGMA:    return &M_MAGMA;
        case ER_TYPE_MINECART: return &M_MINECART;
        default:               return &M_MARKER; /* legacy marker box */
    }
}

/* -------------------------------------------------------------------------- */
/* Geometry emission.                                                          */

typedef struct { float x, y, z, u, v; } ErVtx;

/* vanilla ModelBox quads: vertex indices into the 8 box corners + texel rect.
 * corner i bits: (i&1) X max, (i&2) Y max, (i&4) Z max, matching:
 * 0=(x0,y0,z0) 1=(x1,y0,z0) 2=(x1,y1,z0) 3=(x0,y1,z0)
 * 4=(x0,y0,z1) 5=(x1,y0,z1) 6=(x1,y1,z1) 7=(x0,y1,z1)                        */
typedef struct { int idx[4]; int u1, v1, u2, v2; } ErQuadDef;

static void er_quad_defs(int u, int v, int W, int H, int D, ErQuadDef q[6]) {
    /* order matches ModelBox quadList[0..5] */
    q[0] = (ErQuadDef){ {5,1,2,6}, u+D+W,   v+D, u+D+W+D,   v+D+H }; /* +X */
    q[1] = (ErQuadDef){ {0,4,7,3}, u,       v+D, u+D,       v+D+H }; /* -X */
    q[2] = (ErQuadDef){ {5,4,0,1}, u+D,     v,   u+D+W,     v+D   }; /* -Y (top) */
    q[3] = (ErQuadDef){ {2,3,7,6}, u+D+W,   v+D, u+D+W+W,   v     }; /* +Y (bottom) */
    q[4] = (ErQuadDef){ {1,0,3,2}, u+D,     v+D, u+D+W,     v+D+H }; /* -Z (front) */
    q[5] = (ErQuadDef){ {4,5,6,7}, u+D+W+D, v+D, u+D+W+D+W, v+D+H }; /* +Z (back) */
}

/* per-face shade by dominant world-normal axis (mesh_mc convention). */
static float er_shade(float nx, float ny, float nz) {
    float axx = fabsf(nx), ayy = fabsf(ny), azz = fabsf(nz);
    if (ayy >= axx && ayy >= azz) return ny > 0 ? 1.0f : 0.5f;
    if (azz >= axx)               return 0.8f;
    return 0.6f;
}

/* emit one vanilla box: model-space transform -> world space -> 12 tris.
 * cs/sn are cos/sin of the whole-entity yaw rotation; (fx,fy,fz) = feet.
 * tint multiplies the white vertex colour (hurt flash uses red-leaning). */
static int emit_box(const ErPart *p, float cs, float sn, float sc,
                    float fx, float fy, float fz, CrRgba tint,
                    float lv, float blk, CrVertex *out) {
    const CrMobSprite *spr = &CR_MOB_SPRITES[p->sprite];
    const float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;

    /* 8 inflated corners in part-local model space (pre-mirror x0/x1 swap). */
    float x0 = p->x - p->delta, x1 = p->x + (float)p->dx + p->delta;
    float y0 = p->y - p->delta, y1 = p->y + (float)p->dy + p->delta;
    float z0 = p->z - p->delta, z1 = p->z + (float)p->dz + p->delta;
    if (p->mirror) { float t = x0; x0 = x1; x1 = t; }

    float corner[8][3] = {
        { x0,y0,z0 },{ x1,y0,z0 },{ x1,y1,z0 },{ x0,y1,z0 },
        { x0,y0,z1 },{ x1,y0,z1 },{ x1,y1,z1 },{ x0,y1,z1 },
    };

    /* part rotation about the rotation point: X, then Y, then Z (GL order). */
    float cx = cosf(p->ax), sx = sinf(p->ax);
    float cy = cosf(p->ay), sy = sinf(p->ay);
    float cz = cosf(p->az), sz = sinf(p->az);
    float world[8][3];
    float ctr[3] = { 0, 0, 0 };
    for (int i = 0; i < 8; ++i) {
        float px = corner[i][0], py = corner[i][1], pz = corner[i][2];
        /* Rx */ float ty =  py * cx - pz * sx, tz =  py * sx + pz * cx; py = ty; pz = tz;
        /* Ry */ float tx =  px * cy + pz * sy;       tz = -px * sy + pz * cy; px = tx; pz = tz;
        /* Rz */       tx =  px * cz - py * sz;       ty =  px * sz + py * cz; px = tx; py = ty;
        px += p->rx; py += p->ry; pz += p->rz;
        /* model -> world: mirror X, flip Y about ground plane y=24, /16,
         * then the renderer's preRenderCallback uniform scale. */
        float wx = -px / 16.0f * sc;
        float wy = (24.0f - py) / 16.0f * sc;
        float wz = pz / 16.0f * sc;
        /* whole-entity yaw about Y (cs/sn precomputed for 180-yaw) + feet */
        world[i][0] = fx + wx * cs + wz * sn;
        world[i][1] = fy + wy;
        world[i][2] = fz - wx * sn + wz * cs;
        ctr[0] += world[i][0]; ctr[1] += world[i][1]; ctr[2] += world[i][2];
    }
    ctr[0] *= 0.125f; ctr[1] *= 0.125f; ctr[2] *= 0.125f;

    ErQuadDef q[6];
    er_quad_defs(p->u, p->v, p->dx, p->dy, p->dz, q);

    int written = 0;
    for (int f = 0; f < 6; ++f) {
        /* per-vertex UVs, TexturedQuad corner assignment:
         * [0]=(u2,v1) [1]=(u1,v1) [2]=(u1,v2) [3]=(u2,v2) */
        float qu[4] = { (float)q[f].u2, (float)q[f].u1, (float)q[f].u1, (float)q[f].u2 };
        float qv[4] = { (float)q[f].v1, (float)q[f].v1, (float)q[f].v2, (float)q[f].v2 };
        int   ord[4] = { 0, 1, 2, 3 };

        /* winding: CCW-seen-from-outside (dot(normal, centroid-center) > 0). */
        const float *a = world[q[f].idx[0]];
        const float *b = world[q[f].idx[1]];
        const float *c = world[q[f].idx[2]];
        float e1[3] = { b[0]-a[0], b[1]-a[1], b[2]-a[2] };
        float e2[3] = { c[0]-a[0], c[1]-a[1], c[2]-a[2] };
        float nx = e1[1]*e2[2] - e1[2]*e2[1];
        float ny = e1[2]*e2[0] - e1[0]*e2[2];
        float nz = e1[0]*e2[1] - e1[1]*e2[0];
        float cx4 = (world[q[f].idx[0]][0] + world[q[f].idx[1]][0] +
                     world[q[f].idx[2]][0] + world[q[f].idx[3]][0]) * 0.25f;
        float cy4 = (world[q[f].idx[0]][1] + world[q[f].idx[1]][1] +
                     world[q[f].idx[2]][1] + world[q[f].idx[3]][1]) * 0.25f;
        float cz4 = (world[q[f].idx[0]][2] + world[q[f].idx[1]][2] +
                     world[q[f].idx[2]][2] + world[q[f].idx[3]][2]) * 0.25f;
        float ox = cx4 - ctr[0], oy = cy4 - ctr[1], oz = cz4 - ctr[2];
        if (nx*ox + ny*oy + nz*oz < 0.0f) {
            ord[0] = 3; ord[1] = 2; ord[2] = 1; ord[3] = 0;  /* reverse */
            nx = -nx; ny = -ny; nz = -nz;
        }
        float shade = er_shade(nx, ny, nz);

        CrVertex quad[4];
        for (int k = 0; k < 4; ++k) {
            int s = ord[k];
            const float *w = world[q[f].idx[s]];
            CrVertex vtx;
            vtx.pos.x = w[0]; vtx.pos.y = w[1]; vtx.pos.z = w[2];
            /* clamp into the sprite rect: vanilla lets a few oversized face
             * nets (minecart floor bottom/back) wrap via GL_REPEAT; in a
             * packed atlas that would bleed into the neighboring skin. */
            float cu = qu[s] > (float)spr->w ? (float)spr->w : qu[s];
            float cv = qv[s] > (float)spr->h ? (float)spr->h : qv[s];
            vtx.uv.x = ((float)spr->x0 + cu) / aw;
            vtx.uv.y = ((float)spr->y0 + cv) / ah;
            /* Face directional shade (mesh_mc UP=1 / NS=0.8 / EW=0.6 / DOWN=0.5).
             * Game entity pass sets shade.lightmap so light/blk are sky/block
             * levels 0..15; face shade rides ao. Unit tests leave lightmap
             * NULL and treat light as a 0..1 scalar - they keep working if
             * light=shade and ao=1. Game path: caller-sampled world light. */
            vtx.light = lv;
            vtx.blk = blk;
            vtx.tint = tint;
            vtx.ao = shade;
            quad[k] = vtx;
        }
        static const int TRI[6] = { 0, 1, 2, 0, 2, 3 };
        for (int k = 0; k < 6; ++k) out[written++] = quad[TRI[k]];
    }
    return written;
}

/* legacy marker box (unmodeled types): 0.6 x 1.8 x 0.4 box at the feet, every
 * face wrapped with the full zombie sprite, yaw about Y (previous behavior). */
static int emit_marker(float cs, float sn, float fx, float fy, float fz,
                       CrVertex *out) {
    const CrMobSprite *spr = &CR_MOB_SPRITES[CR_MOB_ZOMBIE];
    const float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
    float u0 = (float)spr->x0 / aw, u1 = (float)spr->x1 / aw;
    float v0 = (float)spr->y0 / ah, v1 = (float)spr->y1 / ah;

    /* mesh_mc FACES template: corners CCW seen from outside. */
    static const struct { float shade; int c[4][3]; } FACES[6] = {
        { 0.5f, { {0,0,0},{1,0,0},{1,0,1},{0,0,1} } },
        { 1.0f, { {0,1,0},{0,1,1},{1,1,1},{1,1,0} } },
        { 0.8f, { {0,0,0},{0,1,0},{1,1,0},{1,0,0} } },
        { 0.8f, { {0,0,1},{1,0,1},{1,1,1},{0,1,1} } },
        { 0.6f, { {0,0,0},{0,0,1},{0,1,1},{0,1,0} } },
        { 0.6f, { {1,0,1},{1,0,0},{1,1,0},{1,1,1} } },
    };
    static const float CUV[4][2] = { {0,1}, {1,1}, {1,0}, {0,0} };
    static const int TRI[6] = { 0, 1, 2, 0, 2, 3 };

    int written = 0;
    for (int f = 0; f < 6; ++f) {
        CrVertex quad[4];
        for (int c = 0; c < 4; ++c) {
            float lx = FACES[f].c[c][0] ? 0.3f : -0.3f;
            float ly = FACES[f].c[c][1] ? 1.8f :  0.0f;
            float lz = FACES[f].c[c][2] ? 0.2f : -0.2f;
            CrVertex vtx;
            vtx.pos.x = fx + lx * cs + lz * sn;
            vtx.pos.y = fy + ly;
            vtx.pos.z = fz - lx * sn + lz * cs;
            vtx.uv.x = u0 + CUV[c][0] * (u1 - u0);
            vtx.uv.y = v0 + CUV[c][1] * (v1 - v0);
            vtx.light = FACES[f].shade;
            vtx.tint.r = vtx.tint.g = vtx.tint.b = vtx.tint.a = 255;
            vtx.ao = 1.0f;
            quad[c] = vtx;
        }
        for (int k = 0; k < 6; ++k) out[written++] = quad[TRI[k]];
    }
    return written;
}

/* RenderArrow: 6 flat textured quads on entity/projectiles/arrow.png (32x32).
 * GL chain: T(pos) Ry(yaw-90) Rz(pitch) Rx(45) S(0.05625) T(-4,0,0); two
 * back-fin quads (both windings) at x=-7, then 4 shaft quads each preceded by
 * a further Rx(90). Tape rows have no pitch -> uses view pitch (0 for tape
 * ghosts, live sim value otherwise). */
static int emit_arrow(const GmEntityView *ent, CrVertex *out) {
    const CrMobSprite *spr = &CR_MOB_SPRITES[CR_MOB_ARROW];
    const float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;

    float yawr   = (ent->yaw - 90.0f) * ER_DEG2RAD;
    float pitchr = ent->pitch * ER_DEG2RAD;
    float cy = cosf(yawr),   sy = sinf(yawr);
    float cp = cosf(pitchr), sp = sinf(pitchr);

    /* local vertex: (x,y,z) pre-scale model units, (u,v) fraction of the
     * 32px texture. quad q: 0,1 = fins (Rx 45); 2..5 = shaft (Rx 135/225/
     * 315/405). */
    static const struct { float x, y, z, u, v; } Q[6][4] = {
        { {-7,-2,-2, 0.0f,    0.15625f}, {-7,-2, 2, 0.15625f, 0.15625f},
          {-7, 2, 2, 0.15625f,0.3125f }, {-7, 2,-2, 0.0f,     0.3125f } },
        { {-7, 2,-2, 0.0f,    0.15625f}, {-7, 2, 2, 0.15625f, 0.15625f},
          {-7,-2, 2, 0.15625f,0.3125f }, {-7,-2,-2, 0.0f,     0.3125f } },
        { {-8,-2, 0, 0.0f, 0.0f}, { 8,-2, 0, 0.5f, 0.0f},
          { 8, 2, 0, 0.5f, 0.15625f}, {-8, 2, 0, 0.0f, 0.15625f} },
        { {-8,-2, 0, 0.0f, 0.0f}, { 8,-2, 0, 0.5f, 0.0f},
          { 8, 2, 0, 0.5f, 0.15625f}, {-8, 2, 0, 0.0f, 0.15625f} },
        { {-8,-2, 0, 0.0f, 0.0f}, { 8,-2, 0, 0.5f, 0.0f},
          { 8, 2, 0, 0.5f, 0.15625f}, {-8, 2, 0, 0.0f, 0.15625f} },
        { {-8,-2, 0, 0.0f, 0.0f}, { 8,-2, 0, 0.5f, 0.0f},
          { 8, 2, 0, 0.5f, 0.15625f}, {-8, 2, 0, 0.0f, 0.15625f} },
    };

    float lv = 15.0f, blk = 0.0f;
    CrRgba tint = { 255, 255, 255, 255 };
    if (ent->lm_lit == 1) {
        lv = ent->lm_light; blk = ent->lm_blk;
    } else if (ent->lm_lit == 2) {
        lv = 1.0f; blk = 0.0f;
        tint.r = (u8)(tint.r * ent->lm_mul_r + 0.5f);
        tint.g = (u8)(tint.g * ent->lm_mul_g + 0.5f);
        tint.b = (u8)(tint.b * ent->lm_mul_b + 0.5f);
    }

    static const int TRI[6] = { 0, 1, 2, 0, 2, 3 };
    int written = 0;
    for (int q = 0; q < 6; ++q) {
        float xrot = (45.0f + (q >= 2 ? 90.0f * (float)(q - 1) : 0.0f)) *
                     ER_DEG2RAD;
        float cx = cosf(xrot), sx = sinf(xrot);
        CrVertex quad[4];
        float w[4][3];
        for (int c = 0; c < 4; ++c) {
            /* S(0.05625) T(-4,0,0) */
            float px = (Q[q][c].x - 4.0f) * 0.05625f;
            float py = Q[q][c].y * 0.05625f;
            float pz = Q[q][c].z * 0.05625f;
            /* Rx(xrot) */
            float ty = py * cx - pz * sx, tz = py * sx + pz * cx;
            py = ty; pz = tz;
            /* Rz(pitch) */
            float tx = px * cp - py * sp;
            ty = px * sp + py * cp; px = tx; py = ty;
            /* Ry(yaw-90) */
            tx = px * cy + pz * sy; tz = -px * sy + pz * cy;
            px = tx; pz = tz;
            w[c][0] = ent->x + px; w[c][1] = ent->y + py; w[c][2] = ent->z + pz;
        }
        float e1[3] = { w[1][0]-w[0][0], w[1][1]-w[0][1], w[1][2]-w[0][2] };
        float e2[3] = { w[2][0]-w[0][0], w[2][1]-w[0][1], w[2][2]-w[0][2] };
        float nx = e1[1]*e2[2] - e1[2]*e2[1];
        float ny = e1[2]*e2[0] - e1[0]*e2[2];
        float nz = e1[0]*e2[1] - e1[1]*e2[0];
        float shade = er_shade(nx, ny, nz);
        for (int c = 0; c < 4; ++c) {
            CrVertex vtx;
            vtx.pos.x = w[c][0]; vtx.pos.y = w[c][1]; vtx.pos.z = w[c][2];
            vtx.uv.x = ((float)spr->x0 + Q[q][c].u * (float)spr->w) / aw;
            vtx.uv.y = ((float)spr->y0 + Q[q][c].v * (float)spr->h) / ah;
            vtx.light = lv;
            vtx.blk = blk;
            vtx.tint = tint;
            vtx.ao = shade;
            quad[c] = vtx;
        }
        for (int k = 0; k < 6; ++k) out[written++] = quad[TRI[k]];
    }
    return written;
}

/* ---- EntityEnderCrystal (RenderEnderCrystal + ModelEnderCrystal) --------- */
/* Plain Render subclass: NO RenderLivingBase x-mirror / y-flip; GL chain is
 *   T(pos) S(2) T(0,-0.5,0)
 *     [base 12x4x12 @uv(0,16), origin (-6,0,-6)]           (shouldShowBottom)
 *     Ry(f*3) T(0, 0.8 + f1*0.2, 0) Raxis(60, 0.7071,0,0.7071)
 *     [glass 8x8x8 @uv(0,0), origin (-4,-4,-4)]
 *     S(0.875) Raxis(60) Ry(f*3)  [glass again]
 *     S(0.875) Raxis(60) Ry(f*3)  [cube 8x8x8 @uv(32,0)]
 * with f = innerRotation + partialTicks (capture partial is 1.0),
 * f1 = (sin(f*.2)/2+.5)^2 + (sin(f*.2)/2+.5), vertices at
 * texel*0.0625. */
typedef struct { float m[3][3]; float t[3]; } ErAff;

static void er_aff_identity(ErAff *a) {
    memset(a, 0, sizeof *a);
    a->m[0][0] = a->m[1][1] = a->m[2][2] = 1.0f;
}
/* post-multiply by a 3x3 (GL order: later ops act on vertices first). */
static void er_aff_mul3(ErAff *a, const float r[3][3]) {
    float o[3][3];
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            o[i][j] = a->m[i][0]*r[0][j] + a->m[i][1]*r[1][j] + a->m[i][2]*r[2][j];
    memcpy(a->m, o, sizeof o);
}
static void er_aff_rot_y(ErAff *a, float deg) {
    float c = cosf(deg * ER_DEG2RAD), s = sinf(deg * ER_DEG2RAD);
    /* glRotatef(a,0,1,0): x' = x c + z s; z' = -x s + z c */
    const float r[3][3] = { {c,0,s}, {0,1,0}, {-s,0,c} };
    er_aff_mul3(a, r);
}
static void er_aff_rot_x(ErAff *a, float deg) {
    float c = cosf(deg * ER_DEG2RAD), s = sinf(deg * ER_DEG2RAD);
    /* glRotatef(a,1,0,0): y' = y c - z s; z' = y s + z c */
    const float r[3][3] = { {1,0,0}, {0,c,-s}, {0,s,c} };
    er_aff_mul3(a, r);
}
static void er_aff_rot_z(ErAff *a, float deg) {
    float c = cosf(deg * ER_DEG2RAD), s = sinf(deg * ER_DEG2RAD);
    /* glRotatef(a,0,0,1): x' = x c - y s; y' = x s + y c */
    const float r[3][3] = { {c,-s,0}, {s,c,0}, {0,0,1} };
    er_aff_mul3(a, r);
}
static void er_aff_rot_axis(ErAff *a, float deg, float ux, float uy, float uz) {
    float n = sqrtf(ux*ux + uy*uy + uz*uz);
    ux /= n; uy /= n; uz /= n;
    float c = cosf(deg * ER_DEG2RAD), s = sinf(deg * ER_DEG2RAD), o = 1.0f - c;
    const float r[3][3] = {
        { c + ux*ux*o,      ux*uy*o - uz*s,  ux*uz*o + uy*s },
        { uy*ux*o + uz*s,   c + uy*uy*o,     uy*uz*o - ux*s },
        { uz*ux*o - uy*s,   uz*uy*o + ux*s,  c + uz*uz*o    },
    };
    er_aff_mul3(a, r);
}
static void er_aff_scale(ErAff *a, float s) {
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j) a->m[i][j] *= s;
}
static void er_aff_scale3(ErAff *a, float sx, float sy, float sz) {
    for (int i = 0; i < 3; ++i) {
        a->m[i][0] *= sx; a->m[i][1] *= sy; a->m[i][2] *= sz;
    }
}
static void er_aff_translate(ErAff *a, float x, float y, float z) {
    a->t[0] += a->m[0][0]*x + a->m[0][1]*y + a->m[0][2]*z;
    a->t[1] += a->m[1][0]*x + a->m[1][1]*y + a->m[1][2]*z;
    a->t[2] += a->m[2][0]*x + a->m[2][1]*y + a->m[2][2]*z;
}

/* one ModelBox under an affine (texel coords * 0.0625), UVs via er_quad_defs.
 * mirror swaps the x corners (ModelRenderer.mirror UV flip). uvscale maps
 * model texel coords to image pixels: ModelBase UVs are normalized by the
 * model's textureWidth/Height (default 64x32), so a larger PNG samples at
 * pixel = texel * (png_w / model_tex_w). ModelEnderCrystal keeps the 64x32
 * default with a 128x64 PNG -> 2; ModelDragon sets 256x256 -> 1. */
static int er_aff_box(const ErAff *a, int sprite, int uvscale, int mirror,
                      int u, int v,
                      float bx, float by, float bz, int dx, int dy, int dz,
                      CrRgba tint, float lv, float blk, CrVertex *out) {
    const CrMobSprite *spr = &CR_MOB_SPRITES[sprite];
    const float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
    float x0 = bx * 0.0625f, x1 = (bx + (float)dx) * 0.0625f;
    float y0 = by * 0.0625f, y1 = (by + (float)dy) * 0.0625f;
    float z0 = bz * 0.0625f, z1 = (bz + (float)dz) * 0.0625f;
    if (mirror) { float t = x0; x0 = x1; x1 = t; }
    float corner[8][3] = {
        { x0,y0,z0 },{ x1,y0,z0 },{ x1,y1,z0 },{ x0,y1,z0 },
        { x0,y0,z1 },{ x1,y0,z1 },{ x1,y1,z1 },{ x0,y1,z1 },
    };
    float world[8][3];
    float ctr[3] = { 0, 0, 0 };
    for (int i = 0; i < 8; ++i) {
        const float *p = corner[i];
        for (int r = 0; r < 3; ++r) {
            world[i][r] = a->m[r][0]*p[0] + a->m[r][1]*p[1] + a->m[r][2]*p[2]
                        + a->t[r];
            ctr[r] += world[i][r];
        }
    }
    for (int r = 0; r < 3; ++r) ctr[r] *= 0.125f;

    ErQuadDef q[6];
    er_quad_defs(u, v, dx, dy, dz, q);
    int written = 0;
    for (int f = 0; f < 6; ++f) {
        float qu[4] = { (float)(q[f].u2*uvscale), (float)(q[f].u1*uvscale),
                        (float)(q[f].u1*uvscale), (float)(q[f].u2*uvscale) };
        float qv[4] = { (float)(q[f].v1*uvscale), (float)(q[f].v1*uvscale),
                        (float)(q[f].v2*uvscale), (float)(q[f].v2*uvscale) };
        int   ord[4] = { 0, 1, 2, 3 };
        const float *pa = world[q[f].idx[0]];
        const float *pb = world[q[f].idx[1]];
        const float *pc = world[q[f].idx[2]];
        float e1[3] = { pb[0]-pa[0], pb[1]-pa[1], pb[2]-pa[2] };
        float e2[3] = { pc[0]-pa[0], pc[1]-pa[1], pc[2]-pa[2] };
        float nx = e1[1]*e2[2] - e1[2]*e2[1];
        float ny = e1[2]*e2[0] - e1[0]*e2[2];
        float nz = e1[0]*e2[1] - e1[1]*e2[0];
        float c4[3] = { 0, 0, 0 };
        for (int k = 0; k < 4; ++k)
            for (int r = 0; r < 3; ++r) c4[r] += world[q[f].idx[k]][r] * 0.25f;
        float ox = c4[0]-ctr[0], oy = c4[1]-ctr[1], oz = c4[2]-ctr[2];
        if (nx*ox + ny*oy + nz*oz < 0.0f) {
            ord[0] = 3; ord[1] = 2; ord[2] = 1; ord[3] = 0;
            nx = -nx; ny = -ny; nz = -nz;
        }
        float shade = er_shade(nx, ny, nz);
        CrVertex quad[4];
        for (int k = 0; k < 4; ++k) {
            int s = ord[k];
            const float *w = world[q[f].idx[s]];
            CrVertex vtx;
            vtx.pos.x = w[0]; vtx.pos.y = w[1]; vtx.pos.z = w[2];
            vtx.uv.x = ((float)spr->x0 + qu[s]) / aw;
            vtx.uv.y = ((float)spr->y0 + qv[s]) / ah;
            vtx.light = lv;
            vtx.blk = blk;
            vtx.tint = tint;
            vtx.ao = shade;
            quad[k] = vtx;
        }
        static const int TRI[6] = { 0, 1, 2, 0, 2, 3 };
        for (int k = 0; k < 6; ++k) out[written++] = quad[TRI[k]];
    }
    return written;
}

static int emit_crystal(const GmEntityView *ent, CrVertex *out) {
    /* RenderEnderCrystal.doRender passes innerRotation + partialTicks. */
    float f = ent->crystal_rot + 1.0f;
    float f1 = sinf(f * 0.2f) / 2.0f + 0.5f;
    f1 = f1 * f1 + f1;

    float lv = 15.0f, blk = 0.0f;
    CrRgba tint = { 255, 255, 255, 255 };
    if (ent->lm_lit == 1) {
        lv = ent->lm_light; blk = ent->lm_blk;
    } else if (ent->lm_lit == 2) {
        lv = 1.0f; blk = 0.0f;
        tint.r = (u8)(tint.r * ent->lm_mul_r + 0.5f);
        tint.g = (u8)(tint.g * ent->lm_mul_g + 0.5f);
        tint.b = (u8)(tint.b * ent->lm_mul_b + 0.5f);
    }

    ErAff a;
    er_aff_identity(&a);
    a.t[0] = ent->x; a.t[1] = ent->y; a.t[2] = ent->z;
    er_aff_scale(&a, 2.0f);
    er_aff_translate(&a, 0.0f, -0.5f, 0.0f);
    int written = 0;
    if (ent->show_bottom)
        written += er_aff_box(&a, CR_MOB_ENDERCRYSTAL, 2, 0, 0, 16,
                              -6, 0, -6, 12, 4, 12, tint, lv, blk,
                              out + written);
    er_aff_rot_y(&a, f * 3.0f);
    er_aff_translate(&a, 0.0f, 0.8f + f1 * 0.2f, 0.0f);
    er_aff_rot_axis(&a, 60.0f, 0.7071f, 0.0f, 0.7071f);
    written += er_aff_box(&a, CR_MOB_ENDERCRYSTAL, 2, 0, 0, 0,
                          -4, -4, -4, 8, 8, 8, tint, lv, blk, out + written);
    er_aff_scale(&a, 0.875f);
    er_aff_rot_axis(&a, 60.0f, 0.7071f, 0.0f, 0.7071f);
    er_aff_rot_y(&a, f * 3.0f);
    written += er_aff_box(&a, CR_MOB_ENDERCRYSTAL, 2, 0, 0, 0,
                          -4, -4, -4, 8, 8, 8, tint, lv, blk, out + written);
    er_aff_scale(&a, 0.875f);
    er_aff_rot_axis(&a, 60.0f, 0.7071f, 0.0f, 0.7071f);
    er_aff_rot_y(&a, f * 3.0f);
    written += er_aff_box(&a, CR_MOB_ENDERCRYSTAL, 2, 0, 32, 0,
                          -4, -4, -4, 8, 8, 8, tint, lv, blk, out + written);
    return written;
}

/* ---- EntityDragon (RenderDragon + ModelDragon) --------------------------- */
/* Type id stays GM_ENTITY_DRAGON (9): the boss-bar latch in frame_capture.c
 * keys on it. 65 ModelBoxes: 5-seg neck + head/jaw + body + wings/legs (both
 * sides) + 12-seg tail.
 *
 * Vanilla trails the neck/tail behind the head via a 64-entry ring buffer of
 * per-tick (rotationYaw, posY) pushed in onLivingUpdate. The tape carries only
 * the current tick's values, so magma rebuilds the ring here: one push per
 * emitted frame (replay renders exactly one frame per tick). Cold start fills
 * the ring flat exactly like vanilla's first onLivingUpdate; a dragon first
 * seen mid-flight therefore straightens its tail for <=64 ticks (residual).
 * getMovementOffsets(p, partial): render partial is 1.0 -> ring[idx-p]; when
 * health<=0 vanilla forces partial=0 -> ring[idx-p-1]. */
#define ER_RAD2DEG 57.29577951308232f
#define ER_TYPE_DRAGON 9

typedef struct {
    int ent_id, inited;
    int idx;
    float yaw[64], y[64];
} ErDragonRing;
static ErDragonRing er_dragon_ring;   /* one dragon per fight */

/* ---- geometry-oracle dump (MAGMA_GEOM_DUMP=path) ----------------------
 * One line per dragon model part per rendered tick, mirroring vanilla
 * ModelRenderer state: "D <tick> <label> rpx rpy rpz rx ry rz" (rotation
 * points in texels, angles in radians - the exact er_dragon_part inputs,
 * which are the exact ModelDragon.render assignments). geom_diff.py joins
 * this against the recorder's <tape>.geom.jsonl sidecar. */
static FILE *er_geom_fp;
static int   er_geom_checked;
static long  er_geom_tick = -1;

void gm_entity_geom_tick(long tick) { er_geom_tick = tick; }

static void geom_log(const char *lbl, float rpx, float rpy, float rpz,
                     float rx, float ry, float rz) {
    if (!er_geom_checked) {
        er_geom_checked = 1;
        const char *p = getenv("MAGMA_GEOM_DUMP");
        if (p && *p) er_geom_fp = fopen(p, "w");
    }
    if (!er_geom_fp) return;
    fprintf(er_geom_fp, "D %ld %s %.6f %.6f %.6f %.7f %.7f %.7f\n",
            er_geom_tick, lbl, rpx, rpy, rpz, rx, ry, rz);
}

/* Advance the trail ring one tick WITHOUT emitting (sparse frame capture:
 * vanilla pushes to the ring every onLivingUpdate, so skipped-render ticks
 * must still push or every getMovementOffsets lookback reaches N-frames-per-
 * tick too far back and the flying body/neck/tail pose goes stale). Rendered
 * ticks keep pushing inside emit_dragon; callers use exactly one of the two
 * per tick. */
void gm_dragon_pose_tick(int ent_id, float yaw, float y) {
    ErDragonRing *rb = &er_dragon_ring;
    if (!rb->inited || rb->ent_id != ent_id) {
        rb->inited = 1;
        rb->ent_id = ent_id;
        rb->idx = 0;
        for (int i = 0; i < 64; ++i) { rb->yaw[i] = yaw; rb->y[i] = y; }
    } else {
        rb->idx = (rb->idx + 1) & 63;
        rb->yaw[rb->idx] = yaw;
        rb->y[rb->idx] = y;
    }
}

static void er_dragon_mo(const ErDragonRing *rb, int p, int dead, float o[2]) {
    int i = (rb->idx - p - (dead ? 1 : 0)) & 63;
    o[0] = rb->yaw[i];
    o[1] = rb->y[i];
}

/* ModelDragon.updateRotations: wrap degrees to [-180, 180). */
static float er_dragon_wrap(float deg) {
    while (deg >= 180.0f) deg -= 360.0f;
    while (deg < -180.0f) deg += 360.0f;
    return deg;
}

/* EntityDragon.getHeadPartYOffset via the recorded AI phase. LANDING(3) /
 * TAKEOFF(4) divide by the distance to the exit-podium top; magma has no
 * getTopSolidOrLiquidBlock here, so the podium top y is approximated at 64
 * (seed-0 End; only biases those two transient phases). */
static float er_dragon_head_off(const GmEntityView *ent, int idx,
                                const float a[2], const float a1[2]) {
    if (ent->phase_id == 3 || ent->phase_id == 4) {
        float dx = ent->x - 0.5f, dy = ent->y - 64.5f, dz = ent->z - 0.5f;
        float f = sqrtf(dx*dx + dy*dy + dz*dz) / 4.0f;
        if (f < 1.0f) f = 1.0f;
        return (float)idx / f;
    }
    if (ent->stationary) return (float)idx;
    if (idx == 6) return 0.0f;
    return a1[1] - a[1];
}

/* ModelRenderer.render(0.0625): T(rotationPoint*s) then Rz, Ry, Rx (radians).*/
static void er_dragon_part(ErAff *a, float rpx, float rpy, float rpz,
                           float rx, float ry, float rz) {
    er_aff_translate(a, rpx * 0.0625f, rpy * 0.0625f, rpz * 0.0625f);
    if (rz != 0.0f) er_aff_rot_z(a, rz * ER_RAD2DEG);
    if (ry != 0.0f) er_aff_rot_y(a, ry * ER_RAD2DEG);
    if (rx != 0.0f) er_aff_rot_x(a, rx * ER_RAD2DEG);
}

/* q85 alpha of dragon_exploding.png over each box's UV rect (u..u+2(dz+dx),
 * v..v+dz+dy, clipped to the 256x256 image), precomputed offline. Keyed by
 * (u,v,dx) - unique across ModelDragon's 19 distinct boxes. */
static float er_dragon_expl_q85(int u, int v, int dx) {
    static const struct { short u, v, dx; float q; } T[] = {
        {192, 104, 10, 0.6359f}, /* neck.box */
        {48, 0, 2, 0.7418f},     /* neck.scale */
        {176, 44, 12, 0.6461f},  /* upperlip */
        {112, 30, 16, 0.6076f},  /* upperhead */
        {0, 0, 2, 0.5104f},      /* head scale */
        {112, 0, 2, 0.4980f},    /* nostril */
        {176, 65, 12, 0.6471f},  /* jaw */
        {0, 0, 24, 0.6745f},     /* body */
        {220, 53, 2, 0.6902f},   /* body scales */
        {112, 88, 56, 0.6353f},  /* wing.bone */
        {-56, 88, 56, 0.6118f},  /* wing.skin */
        {112, 136, 56, 0.6510f}, /* wingtip.bone */
        {-56, 144, 56, 0.6000f}, /* wingtip.skin */
        {112, 104, 8, 0.6627f},  /* frontleg */
        {226, 138, 6, 0.6790f},  /* frontlegtip */
        {144, 104, 8, 0.6510f},  /* frontfoot */
        {0, 0, 16, 0.6745f},     /* rearleg */
        {196, 0, 12, 0.7059f},   /* rearlegtip */
        {112, 0, 18, 0.5882f},   /* rearfoot */
    };
    for (size_t i = 0; i < sizeof(T) / sizeof(T[0]); ++i)
        if (T[i].u == u && T[i].v == v && T[i].dx == dx) return T[i].q;
    return 1.0f;
}

static int emit_dragon(const GmEntityView *ent, CrVertex *out, int cap) {
    if (cap < 65 * ER_VERTS_PER_BOX) return 0;

    ErDragonRing *rb = &er_dragon_ring;
    gm_dragon_pose_tick(ent->ent_id, ent->yaw, ent->y);
    int dead = ent->health <= 0.0f;

    float lv = 15.0f, blk = 0.0f;
    CrRgba tint = { 255, 255, 255, 255 };
    if (ent->hurt_time > 0) { tint.g = 178; tint.b = 178; }
    if (ent->lm_lit == 1) {
        lv = ent->lm_light; blk = ent->lm_blk;
    } else if (ent->lm_lit == 2) {
        lv = 1.0f; blk = 0.0f;
        tint.r = (u8)(tint.r * ent->lm_mul_r + 0.5f);
        tint.g = (u8)(tint.g * ent->lm_mul_g + 0.5f);
        tint.b = (u8)(tint.b * ent->lm_mul_b + 0.5f);
    }

    float mo0[2], mo5[2], mo6[2], mo7[2], mo10[2], mo11[2];
    er_dragon_mo(rb, 0, dead, mo0);
    er_dragon_mo(rb, 5, dead, mo5);
    er_dragon_mo(rb, 6, dead, mo6);
    er_dragon_mo(rb, 7, dead, mo7);
    er_dragon_mo(rb, 10, dead, mo10);
    er_dragon_mo(rb, 11, dead, mo11);

    ErAff a;
    er_aff_identity(&a);
    a.t[0] = ent->x; a.t[1] = ent->y; a.t[2] = ent->z;
    /* RenderDragon.applyRotations (deathTime stays 0 on dragons: no z-roll) */
    er_aff_rot_y(&a, -mo7[0]);
    er_aff_rot_x(&a, (mo5[1] - mo10[1]) * 10.0f);
    er_aff_translate(&a, 0.0f, 0.0f, 1.0f);
    /* RenderLivingBase.prepareScale */
    er_aff_scale3(&a, -1.0f, -1.0f, 1.0f);
    er_aff_translate(&a, 0.0f, -1.501f, 0.0f);

    /* ModelDragon.render, f = animTime (render partial 1.0) */
    const float TAU = 6.2831853071795865f;
    float f = ent->anim_time;
    float jaw_rx = (sinf(f * TAU) + 1.0f) * 0.2f;
    float f1 = sinf(f * TAU - 1.0f) + 1.0f;
    f1 = (f1 * f1 + f1 * 2.0f) * 0.05f;
    er_aff_translate(&a, 0.0f, f1 - 2.0f, -3.0f);
    er_aff_rot_x(&a, f1 * 2.0f);
    float f6 = er_dragon_wrap(mo5[0] - mo10[0]);
    float f7 = er_dragon_wrap(mo5[0] + f6 / 2.0f);
    float f8 = f * TAU;
    float f2 = 20.0f, f3 = -12.0f, f4 = 0.0f;
    int w = 0;
    /* death dissolve: vanilla masks the skin per-texel to dragon_exploding
     * alpha > deathTicks/200 (alpha-test pass + depth EQUAL repaint). magma
     * approximates per BOX: each box drops once f exceeds its q85 exploding
     * alpha (precomputed from the PNG over the box's UV rect), so parts
     * dissolve staggered, all gone by f=0.74 vs vanilla's sparse-texel 1.0. */
    float deadf = ent->death_ticks > 0 ? (float)ent->death_ticks / 200.0f
                                       : 0.0f;
#define DBOX(AF, U, V, X, Y, Z, DX, DY, DZ, MIR) \
    (w += (deadf > 0.0f && er_dragon_expl_q85((U), (V), (DX)) <= deadf) ? 0 : \
     er_aff_box((AF), CR_MOB_DRAGON, 1, (MIR), (U), (V), (X), (Y), (Z), \
                     (DX), (DY), (DZ), tint, lv, blk, out + w))

    for (int i = 0; i < 5; ++i) {                          /* neck */
        float a1[2];
        er_dragon_mo(rb, 5 - i, dead, a1);
        float f9 = cosf((float)i * 0.45f + f8) * 0.15f;
        float ry = er_dragon_wrap(a1[0] - mo6[0]) * ER_DEG2RAD * 1.5f;
        float rx = f9 + er_dragon_head_off(ent, i, mo6, a1)
                        * ER_DEG2RAD * 1.5f * 5.0f;
        float rz = -er_dragon_wrap(a1[0] - f7) * ER_DEG2RAD * 1.5f;
        ErAff sp = a;
        er_dragon_part(&sp, f4, f2, f3, rx, ry, rz);
        { char gl[16]; snprintf(gl, sizeof gl, "neck%d", i);
          geom_log(gl, f4, f2, f3, rx, ry, rz); }
        DBOX(&sp, 192, 104, -5, -5, -5, 10, 10, 10, 0);    /* neck.box */
        DBOX(&sp, 48, 0, -1, -9, -3, 2, 4, 6, 0);          /* neck.scale */
        f2 += sinf(rx) * 10.0f;
        f3 -= cosf(ry) * cosf(rx) * 10.0f;
        f4 -= sinf(ry) * cosf(rx) * 10.0f;
    }
    {                                                      /* head + jaw */
        float ry = er_dragon_wrap(mo0[0] - mo6[0]) * ER_DEG2RAD;
        float rx = er_dragon_wrap(er_dragon_head_off(ent, 6, mo6, mo0))
                   * ER_DEG2RAD * 1.5f * 5.0f;
        float rz = -er_dragon_wrap(mo0[0] - f7) * ER_DEG2RAD;
        ErAff hd = a;
        er_dragon_part(&hd, f4, f2, f3, rx, ry, rz);
        geom_log("head", f4, f2, f3, rx, ry, rz);
        DBOX(&hd, 176, 44, -6, -1, -24, 12, 5, 16, 0);     /* upperlip */
        DBOX(&hd, 112, 30, -8, -8, -10, 16, 16, 16, 0);    /* upperhead */
        DBOX(&hd, 0, 0, -5, -12, -4, 2, 4, 6, 1);          /* scale (mirror) */
        DBOX(&hd, 112, 0, -5, -3, -22, 2, 2, 4, 1);        /* nostril (mirror)*/
        DBOX(&hd, 0, 0, 3, -12, -4, 2, 4, 6, 0);           /* scale */
        DBOX(&hd, 112, 0, 3, -3, -22, 2, 2, 4, 0);         /* nostril */
        ErAff jw = hd;
        er_dragon_part(&jw, 0.0f, 4.0f, -8.0f, jaw_rx, 0.0f, 0.0f);
        geom_log("jaw", 0.0f, 4.0f, -8.0f, jaw_rx, 0.0f, 0.0f);
        DBOX(&jw, 176, 65, -6, 0, -16, 12, 4, 16, 0);      /* jaw */
    }
    ErAff bd = a;                                          /* body group */
    er_aff_translate(&bd, 0.0f, 1.0f, 0.0f);
    er_aff_rot_z(&bd, -f6 * 1.5f);
    er_aff_translate(&bd, 0.0f, -1.0f, 0.0f);
    {
        ErAff bp = bd;
        er_dragon_part(&bp, 0.0f, 4.0f, 8.0f, 0.0f, 0.0f, 0.0f);
        geom_log("body", 0.0f, 4.0f, 8.0f, 0.0f, 0.0f, 0.0f);
        DBOX(&bp, 0, 0, -12, 0, -16, 24, 24, 64, 0);       /* body */
        DBOX(&bp, 220, 53, -1, -6, -10, 2, 6, 12, 0);      /* scales */
        DBOX(&bp, 220, 53, -1, -6, 10, 2, 6, 12, 0);
        DBOX(&bp, 220, 53, -1, -6, 30, 2, 6, 12, 0);
    }
    float f11 = f * TAU;
    float wing_rx = 0.125f - cosf(f11) * 0.2f;
    float wing_rz = (sinf(f11) + 0.125f) * 0.8f;
    float tip_rz = -(sinf(f11 + 2.0f) + 0.5f) * 0.75f;
    for (int j = 0; j < 2; ++j) {                          /* wings + legs */
        ErAff side = bd;
        if (j == 1) er_aff_scale3(&side, -1.0f, 1.0f, 1.0f);
        ErAff wg = side;
        er_dragon_part(&wg, -12.0f, 5.0f, 2.0f, wing_rx, 0.25f, wing_rz);
        if (j == 0) {
            geom_log("wing", -12.0f, 5.0f, 2.0f, wing_rx, 0.25f, wing_rz);
            geom_log("wingTip", -56.0f, 0.0f, 0.0f, 0.0f, 0.0f, tip_rz);
            geom_log("frontLeg", -12.0f, 20.0f, 2.0f, 1.3f + f1*0.1f, 0.0f, 0.0f);
            geom_log("frontLegTip", 0.0f, 20.0f, -1.0f, -0.5f - f1*0.1f, 0.0f, 0.0f);
            geom_log("frontFoot", 0.0f, 23.0f, 0.0f, 0.75f + f1*0.1f, 0.0f, 0.0f);
            geom_log("rearLeg", -16.0f, 16.0f, 42.0f, 1.0f + f1*0.1f, 0.0f, 0.0f);
            geom_log("rearLegTip", 0.0f, 32.0f, -4.0f, 0.5f + f1*0.1f, 0.0f, 0.0f);
            geom_log("rearFoot", 0.0f, 31.0f, 4.0f, 0.75f + f1*0.1f, 0.0f, 0.0f);
        }
        DBOX(&wg, 112, 88, -56, -4, -4, 56, 8, 8, 0);      /* wing.bone */
        DBOX(&wg, -56, 88, -56, 0, 2, 56, 0, 56, 0);       /* wing.skin */
        ErAff wt = wg;
        er_dragon_part(&wt, -56.0f, 0.0f, 0.0f, 0.0f, 0.0f, tip_rz);
        DBOX(&wt, 112, 136, -56, -2, -2, 56, 4, 4, 0);     /* wingtip.bone */
        DBOX(&wt, -56, 144, -56, 0, 2, 56, 0, 56, 0);      /* wingtip.skin */
        ErAff fl = side;
        er_dragon_part(&fl, -12.0f, 20.0f, 2.0f, 1.3f + f1*0.1f, 0.0f, 0.0f);
        DBOX(&fl, 112, 104, -4, -4, -4, 8, 24, 8, 0);      /* frontleg */
        ErAff ft = fl;
        er_dragon_part(&ft, 0.0f, 20.0f, -1.0f, -0.5f - f1*0.1f, 0.0f, 0.0f);
        DBOX(&ft, 226, 138, -3, -1, -3, 6, 24, 6, 0);      /* frontlegtip */
        ErAff ff = ft;
        er_dragon_part(&ff, 0.0f, 23.0f, 0.0f, 0.75f + f1*0.1f, 0.0f, 0.0f);
        DBOX(&ff, 144, 104, -4, 0, -12, 8, 4, 16, 0);      /* frontfoot */
        ErAff rl = side;
        er_dragon_part(&rl, -16.0f, 16.0f, 42.0f, 1.0f + f1*0.1f, 0.0f, 0.0f);
        DBOX(&rl, 0, 0, -8, -4, -8, 16, 32, 16, 0);        /* rearleg */
        ErAff rt = rl;
        er_dragon_part(&rt, 0.0f, 32.0f, -4.0f, 0.5f + f1*0.1f, 0.0f, 0.0f);
        DBOX(&rt, 196, 0, -6, -2, 0, 12, 32, 12, 0);       /* rearlegtip */
        ErAff rf = rt;
        er_dragon_part(&rf, 0.0f, 31.0f, 4.0f, 0.75f + f1*0.1f, 0.0f, 0.0f);
        DBOX(&rf, 112, 0, -9, 0, -20, 18, 6, 24, 0);       /* rearfoot */
    }
    float f10 = 0.0f;                                      /* tail */
    f2 = 10.0f; f3 = 60.0f; f4 = 0.0f;
    for (int k = 0; k < 12; ++k) {
        float a2[2];
        er_dragon_mo(rb, 12 + k, dead, a2);
        f10 += sinf((float)k * 0.45f + f8) * 0.05f;
        float ry = (er_dragon_wrap(a2[0] - mo11[0]) * 1.5f + 180.0f)
                   * ER_DEG2RAD;
        float rx = f10 + (a2[1] - mo11[1]) * ER_DEG2RAD * 1.5f * 5.0f;
        float rz = er_dragon_wrap(a2[0] - f7) * ER_DEG2RAD * 1.5f;
        ErAff sp = a;
        er_dragon_part(&sp, f4, f2, f3, rx, ry, rz);
        { char gl[16]; snprintf(gl, sizeof gl, "tail%d", k);
          geom_log(gl, f4, f2, f3, rx, ry, rz); }
        DBOX(&sp, 192, 104, -5, -5, -5, 10, 10, 10, 0);
        DBOX(&sp, 48, 0, -1, -9, -3, 2, 4, 6, 0);
        f2 += sinf(rx) * 10.0f;
        f3 -= cosf(ry) * cosf(rx) * 10.0f;
        f4 -= sinf(ry) * cosf(rx) * 10.0f;
    }
#undef DBOX
    return w;
}

/* Tape type strings (EntityList class simple names, as recorded by the qrl
 * recorder) -> EW_TYPE_* ids with a full model. Returns -1 for types with no
 * model (witch/bat/squid/items/...) so callers can skip them instead of
 * drawing the legacy marker box. Skin-variant bipeds map to their base model
 * (husk/zombie villager -> zombie skin; stray -> skeleton; cave spider ->
 * spider; mooshroom -> cow): right silhouette, wrong skin, filed residual. */
int gm_entity_type_for_name(const char *name) {
    static const struct { const char *name; int type; } MAP[] = {
        { "EntityZombie",         ER_TYPE_ZOMBIE },
        { "EntityHusk",           ER_TYPE_ZOMBIE },
        { "EntityZombieVillager", ER_TYPE_ZOMBIE },
        { "EntityPigZombie",      ER_TYPE_ZOMBIE },
        { "EntitySkeleton",       ER_TYPE_SKELETON },
        { "EntityStray",          ER_TYPE_SKELETON },
        { "EntityWitherSkeleton", ER_TYPE_WITHER_SKELETON },
        { "EntityCreeper",        ER_TYPE_CREEPER },
        { "EntitySpider",         ER_TYPE_SPIDER },
        { "EntityCaveSpider",     ER_TYPE_SPIDER },
        { "EntityEnderman",       ER_TYPE_ENDERMAN },
        { "EntityBlaze",          ER_TYPE_BLAZE },
        { "EntitySheep",          ER_TYPE_SHEEP },
        { "EntityPig",            ER_TYPE_PIG },
        { "EntityCow",            ER_TYPE_COW },
        { "EntityMooshroom",      ER_TYPE_COW },
        { "EntityChicken",        ER_TYPE_CHICKEN },
        { "EntitySquid",          ER_TYPE_SQUID },
        { "EntityWitch",          ER_TYPE_WITCH },
        { "EntityBat",            ER_TYPE_BAT },
        { "EntityLlama",          ER_TYPE_LLAMA },
        { "EntityGhast",          ER_TYPE_GHAST },
        { "EntityMagmaCube",      ER_TYPE_MAGMA },
        { "EntityMinecartEmpty",  ER_TYPE_MINECART },
        { "EntityMinecartChest",  ER_TYPE_MINECART },
        { "EntityMinecartFurnace",ER_TYPE_MINECART },
        { "EntityMinecartHopper", ER_TYPE_MINECART },
        { "EntityMinecartTNT",    ER_TYPE_MINECART },
        /* full ModelDragon transcription (emit_dragon); id 9 also drives
         * the boss-bar latch in frame_capture.c */
        { "EntityDragon",         9 /* ER_TYPE_DRAGON / GM_ENTITY_DRAGON */ },
        /* stuck/flying bow arrows: RenderArrow flat quads (tape rows carry
         * no pitch, so ghosts render yaw-only - fine for flat shots). */
        { "EntityArrow",          ER_TYPE_ARROW },
        { "EntityTippedArrow",    ER_TYPE_ARROW },
        { "EntitySpectralArrow",  ER_TYPE_ARROW },
        /* RenderSnowball: camera-facing item sprite, drawn by the item pass
         * (gm_items_emit_billboard); item id from gm_entity_billboard_item. */
        { "EntityEnderCrystal",   ER_TYPE_CRYSTAL },
        { "EntityEnderPearl",     30 /* GM_VIEW_BILLBOARD */ },
        { "EntityEnderEye",       30 /* GM_VIEW_BILLBOARD */ },
        { "EntitySnowball",       30 /* GM_VIEW_BILLBOARD */ },
        { "EntityEgg",            30 /* GM_VIEW_BILLBOARD */ },
    };
    if (!name) return -1;
    for (unsigned i = 0; i < sizeof MAP / sizeof MAP[0]; ++i)
        if (!strcmp(name, MAP[i].name)) return MAP[i].type;
    return -1;
}

/* GM_VIEW_BILLBOARD types -> the item id RenderSnowball draws (getStackToRender). */
int gm_entity_billboard_item(const char *name) {
    if (!name) return 0;
    if (!strcmp(name, "EntityEnderPearl")) return 368;
    if (!strcmp(name, "EntityEnderEye"))   return 381;
    if (!strcmp(name, "EntitySnowball"))   return 332;
    if (!strcmp(name, "EntityEgg"))        return 344;
    return 0;
}

/* Skin-variant sprite overrides (see gm_entity_type_for_name): variants that
 * reuse a base model with their own jar texture. Zombie villager stays on the
 * zombie skin - its texture is a different (villager-head) layout. */
int gm_entity_skin_for_name(const char *name) {
    static const struct { const char *name; int sprite; } MAP[] = {
        { "EntityPigZombie",  CR_MOB_PIGMAN },
        { "EntityHusk",       CR_MOB_HUSK },
        { "EntityStray",      CR_MOB_STRAY },
        { "EntityCaveSpider", CR_MOB_CAVE_SPIDER },
        { "EntityMooshroom",  CR_MOB_MOOSHROOM },
    };
    if (!name) return 0;
    for (unsigned i = 0; i < sizeof MAP / sizeof MAP[0]; ++i)
        if (!strcmp(name, MAP[i].name)) return MAP[i].sprite + 1;
    return 0;
}

/* Vanilla Entity.getEyeHeight per rendered type (world-light sample point).
 * Default height*0.85; explicit overrides where vanilla has them. */
float gm_entity_eye_y(int type) {
    switch (type) {
        case ER_TYPE_ZOMBIE:   return 1.74f;          /* EntityZombie override */
        case ER_TYPE_SKELETON: return 1.99f * 0.85f;
        case ER_TYPE_WITHER_SKELETON: return 2.1f;
        case ER_TYPE_CREEPER:  return 1.7f * 0.85f;
        case ER_TYPE_SPIDER:   return 0.65f;          /* EntitySpider override */
        case ER_TYPE_ENDERMAN: return 2.55f;          /* EntityEnderman override */
        case ER_TYPE_BLAZE:    return 1.8f * 0.85f;
        case ER_TYPE_SHEEP:    return 1.3f * 0.85f;
        case ER_TYPE_PIG:      return 0.9f * 0.85f;
        case ER_TYPE_COW:      return 1.4f * 0.85f;
        case ER_TYPE_CHICKEN:  return 0.7f * 0.85f;
        case ER_TYPE_SQUID:    return 0.4f;           /* height * 0.5 */
        case ER_TYPE_WITCH:    return 1.95f * 0.85f;
        case ER_TYPE_BAT:      return 0.9f * 0.85f;
        case ER_TYPE_LLAMA:    return 1.87f * 0.85f;
        case ER_TYPE_GHAST:    return 4.0f * 0.85f;
        case ER_TYPE_MAGMA:    return 1.02f * 0.85f;  /* size-2 cube */
        case ER_TYPE_DRAGON:   return 8.0f * 0.85f;   /* setSize(16, 8) */
        case ER_TYPE_CRYSTAL:  return 2.0f * 0.85f;   /* setSize(2, 2) */
        default:               return 0.5f;
    }
}

/* ModelQuadruped leg indices (after head/body[/extras]): alternate pairs.
 * Sheep: parts 0 head, 1 body, 2-5 legs, 6-11 fur. Legs 2,3,4,5.
 * Pig/cow similar. Vanilla: leg1/4 cos(ls*0.6662)*1.4*lsa,
 *            leg2/3 cos(ls*0.6662+pi)*1.4*lsa. */
static void apply_quad_limb_swing(ErPart *parts, int nparts, int leg0,
                                  float limb_swing, float limb_amount) {
    if (leg0 + 3 >= nparts) return;
    float a = cosf(limb_swing * 0.6662f) * 1.4f * limb_amount;
    float b = cosf(limb_swing * 0.6662f + ER_PI) * 1.4f * limb_amount;
    parts[leg0 + 0].ax = a;  /* leg1 */
    parts[leg0 + 1].ax = b;  /* leg2 */
    parts[leg0 + 2].ax = b;  /* leg3 */
    parts[leg0 + 3].ax = a;  /* leg4 */
}

static CrRgba sheep_wool_tint(int meta, int hurt) {
    /* EntitySheep.DYE_TO_RGB, indexed by EnumDyeColor metadata. */
    static const unsigned char rgb[16][3] = {
        {255,255,255},{217,128,51},{179,77,217},{102,153,217},
        {230,230,51},{128,204,26},{242,128,166},{77,77,77},
        {153,153,153},{77,128,153},{128,64,179},{51,77,179},
        {102,77,51},{102,128,51},{153,51,51},{26,26,26}
    };
    if (meta < 0 || meta > 15) meta = 0;
    CrRgba c = { rgb[meta][0], rgb[meta][1], rgb[meta][2], 255 };
    if (hurt) {
        c.r = (u8)((c.r * 178 + 255 * 77 + 127) / 255);
        c.g = (u8)((c.g * 178 + 127) / 255);
        c.b = (u8)((c.b * 178 + 127) / 255);
    }
    return c;
}

int gm_entities_emit(const GmEntityView *ents, int n, CrVertex *out, int max) {
    int written = 0;
    for (int e = 0; e < n; ++e) {
        if (ents[e].tape_pose && (ents[e].flags & 4)) continue; /* invisible */
        if (ents[e].type == ER_TYPE_ARROW) {
            if (written + ER_VERTS_PER_BOX > max) break;   /* 6 quads = 36 */
            written += emit_arrow(&ents[e], out + written);
            continue;
        }
        if (ents[e].type == ER_TYPE_CRYSTAL) {
            if (written + 4 * ER_VERTS_PER_BOX > max) break;
            written += emit_crystal(&ents[e], out + written);
            continue;
        }
        if (ents[e].type == ER_TYPE_DRAGON) {
            written += emit_dragon(&ents[e], out + written, max - written);
            continue;
        }
        const ErModel *m = er_model_for_type(ents[e].type);
        if (!m) continue;                            /* NONE / PLAYER */
        int need = m->nparts * ER_VERTS_PER_BOX;
        if (written + need > max) break;             /* would overflow -> stop */

        float fx = ents[e].x, fy = ents[e].y, fz = ents[e].z;

        if (m == &M_MARKER) {
            /* legacy marker uses raw yaw (previous behavior). */
            float rad = ents[e].yaw * ER_DEG2RAD;
            written += emit_marker(cosf(rad), sinf(rad), fx, fy, fz,
                                   out + written);
            continue;
        }

        /* hurt flash: RenderLivingBase.setBrightness red (1,0,0,0.3) blend
         * approximates as mix(tex, red, 0.3) -> tint (255, 178, 178). */
        CrRgba tint = { 255, 255, 255, 255 };
        if (ents[e].hurt_time > 0) {
            tint.r = 255; tint.g = 178; tint.b = 178;
        }

        /* world lighting (see GmEntityView.lm_lit). Legacy callers (0) keep
         * fullbright; 1 = LUT levels; 2 = folded multiplier (no lightmap). */
        float lv = 15.0f, blk = 0.0f;
        if (ents[e].lm_lit == 1) {
            lv = ents[e].lm_light; blk = ents[e].lm_blk;
        } else if (ents[e].lm_lit == 2) {
            lv = 1.0f; blk = 0.0f;
            tint.r = (u8)(tint.r * ents[e].lm_mul_r + 0.5f);
            tint.g = (u8)(tint.g * ents[e].lm_mul_g + 0.5f);
            tint.b = (u8)(tint.b * ents[e].lm_mul_b + 0.5f);
        }

        /* copy parts so limb swing / death pose can mutate ax without
         * clobbering the static model tables. */
        ErPart local[ER_MAX_PARTS];
        int np = m->nparts;
        if (np > ER_MAX_PARTS) np = ER_MAX_PARTS;
        memcpy(local, m->parts, (size_t)np * sizeof(ErPart));

        /* skin-variant override (pigman/husk/stray/cave spider/mooshroom):
         * same model, different atlas sprite. All variant bases are
         * single-skin models (sheep, the only two-sprite model, has none). */
        if (ents[e].skin > 0)
            for (int p = 0; p < np; ++p)
                local[p].sprite = ents[e].skin - 1;

        float lsa = ents[e].limb_swing_amount;
        float ls  = ents[e].limb_swing;
        int t = ents[e].type;
        if (ents[e].tape_pose && np > 0) {
            /* ModelLivingBase.setRotationAngles: head yaw is relative to the
             * renderYawOffset body rotation; pitch is absolute. Only applied
             * where part 0 IS a head sharing its pivot with any companions
             * (witch nose/hat and ghast/magma/minecart have no such head). */
            float hay = (ents[e].head_yaw - ents[e].yaw) * ER_DEG2RAD;
            float hax = ents[e].pitch * ER_DEG2RAD;
            int h0 = -1, h1 = -1;                    /* inclusive head span */
            if (t == ER_TYPE_LLAMA)      { h0 = 0; h1 = 3; }
            else if (t == ER_TYPE_BAT)   { h0 = 0; h1 = 2; }
            else if (t == ER_TYPE_WITCH || t == ER_TYPE_GHAST ||
                     t == ER_TYPE_MAGMA || t == ER_TYPE_MINECART) { /* none */ }
            else                         { h0 = 0; h1 = 0; }
            for (int p = h0; p >= 0 && p <= h1 && p < np; ++p) {
                local[p].ay = hay; local[p].ax = hax;
            }
            if ((t == ER_TYPE_SHEEP || t == ER_TYPE_ZOMBIE) && np >= 7) {
                /* sheep fur head / biped headwear copy the head rotation */
                local[6].ay = hay;
                local[6].ax = hax;
            }
        }
        if (lsa > 1e-4f) {
            if (t == ER_TYPE_SHEEP) {
                apply_quad_limb_swing(local, np, 2, ls, lsa);
                apply_quad_limb_swing(local, np, 8, ls, lsa); /* wool leg sleeves */
            } else if (t == ER_TYPE_PIG)    apply_quad_limb_swing(local, np, 3, ls, lsa);
            else if (t == ER_TYPE_COW)    apply_quad_limb_swing(local, np, 5, ls, lsa);
            else if (t == ER_TYPE_LLAMA)  apply_quad_limb_swing(local, np, 5, ls, lsa);
            else if (t == ER_TYPE_CHICKEN) {
                /* chicken legs at parts 4,5 */
                if (np > 5) {
                    float a = cosf(ls * 0.6662f) * 1.4f * lsa;
                    float b = cosf(ls * 0.6662f + ER_PI) * 1.4f * lsa;
                    local[4].ax = a; local[5].ax = b;
                }
            } else if (t == ER_TYPE_ZOMBIE || t == ER_TYPE_SKELETON ||
                       t == ER_TYPE_WITHER_SKELETON) {
                /* ModelBiped legs at parts 4,5; wither arms use the walk cycle. */
                if (np > 5) {
                    float a = cosf(ls * 0.6662f) * 1.4f * lsa;
                    float b = cosf(ls * 0.6662f + ER_PI) * 1.4f * lsa;
                    local[4].ax = a; local[5].ax = b;
                    if (t == ER_TYPE_WITHER_SKELETON) {
                        local[2].ax = b;
                        local[3].ax = a;
                    }
                }
            } else if (t == ER_TYPE_WITCH && np >= 14) {
                /* ModelVillager legs (parts 12,13), half amplitude */
                local[12].ax = cosf(ls * 0.6662f) * 1.4f * lsa * 0.5f;
                local[13].ax = cosf(ls * 0.6662f + ER_PI) * 1.4f * lsa * 0.5f;
            }
        }
        if (t == ER_TYPE_WITHER_SKELETON && np >= 4 &&
            ents[e].swing_progress > 0.0f) {
            /* ModelBiped.setRotationAngles swingProgress path. The recorder
             * does not expose AbstractSkeleton.SWINGING_ARMS; the captured
             * melee frames have that AI flag clear and retain this base pose. */
            float sp = ents[e].swing_progress;
            float body = sinf(sqrtf(sp) * ER_PI * 2.0f) * 0.2f;
            local[1].ay = body;
            local[2].rz = sinf(body) * 5.0f;
            local[2].rx = -cosf(body) * 5.0f;
            local[3].rz = -sinf(body) * 5.0f;
            local[3].rx = cosf(body) * 5.0f;
            local[2].ay += body;
            local[3].ay += body;
            local[3].ax += body;
            float q = 1.0f - sp;
            q *= q; q *= q;
            float f2 = sinf((1.0f - q) * ER_PI);
            float f3 = sinf(sp * ER_PI) * -(local[0].ax - 0.7f) * 0.75f;
            local[2].ax -= f2 * 1.2f + f3;
            local[2].ay += body * 2.0f;
            local[2].az -= sinf(sp * ER_PI) * 0.4f;
        }
        if (t == ER_TYPE_BAT && np >= 9) {
            /* flying flap from age: body ax = pi/4 + cos(age*0.1)*0.15;
             * wing ay = cos(age*1.3)*pi*0.25, outer wing +50% (flattened
             * child composition). */
            float age = (float)ents[e].age;
            float bod = BAT_BODY_AX + cosf(age * 0.1f) * 0.15f;
            float w = cosf(age * 1.3f) * ER_PI * 0.25f;
            local[3].ax = bod; local[4].ax = bod;
            local[5].ax = bod; local[6].ax = bod;
            local[7].ax = bod; local[8].ax = bod;
            local[5].ay =  w;        local[6].ay =  w * 1.5f;
            local[7].ay = -w;        local[8].ay = -w * 1.5f;
        } else if (t == ER_TYPE_GHAST && np >= 10) {
            float age = (float)ents[e].age;
            for (int p = 1; p < 10; ++p)
                local[p].ax = 0.2f * sinf(age * 0.3f + (float)(p - 1)) + 0.4f;
        }

        /* ModelSheep1/2.setLivingAnimations + setRotationAngles: head pitch and
         * rotationPointY come from EntitySheep.sheepTimer (AI eat-grass). Tape
         * has no sheepTimer; idle (limbSwingAmount near 0) is the graze-stand
         * pose used most of the time the player stares at a flock. Mid-graze
         * constants from EntitySheep.getHeadRotation* at sheepTimer=20:
         *   head.ry = 6 + 1.0*9
         *   head.ax = PI/5 + (PI*7/100)*sin((20-4)/32 * 28.7)
         * Applied to skin head (0) and fur head (6). */
        if (t == ER_TYPE_SHEEP && ents[e].tape_pose && np >= 7) {
            local[0].ry = 6.0f + ents[e].graze_y * 9.0f;
            local[0].ax = ents[e].graze_x;
            local[6].ry = local[0].ry;
            local[6].ax = local[0].ax;
        } else if (t == ER_TYPE_SHEEP && lsa < 0.08f && np >= 7) {
            float f = (20.0f - 4.0f) / 32.0f;
            float head_ax = (ER_PI / 5.0f)
                + (ER_PI * 7.0f / 100.0f) * sinf(f * 28.7f);
            local[0].ry = 15.0f;
            local[0].ax = head_ax;
            local[6].ry = 15.0f;
            local[6].ax = head_ax;
        }

        /* death: rotateCorpse tilts about Z when health <= 0 */
        float death_roll = 0.f;
        if (ents[e].health >= 0.f && ents[e].health <= 0.f)
            death_roll = ER_PI / 2.0f;  /* fully tipped (no deathTime in tape) */

        /* vanilla applyRotations: rotate(180 - yaw) about Y. */
        float rad = (180.0f - ents[e].yaw) * ER_DEG2RAD;
        float cs = cosf(rad), sn = sinf(rad);
        float sc = m->scale > 0.0f ? m->scale : 1.0f;
        (void)death_roll;  /* full death tilt needs entity-level rot; residual */
        /* Sheep: emit fur body/legs first, then skin, then fur head last so the
         * face snout (skin head, longer -Z) wins near-coplanar depth against the
         * expanded fur head (ModelSheep1 delta 0.6). Vanilla order is base then
         * LayerSheepWool; skin-last for the head only preserves face texels. */
        if (t == ER_TYPE_SHEEP && ents[e].tape_pose && ents[e].sheared && np >= 12) {
            for (int p = 0; p < 6; ++p)
                written += emit_box(&local[p], cs, sn, sc, fx, fy, fz, tint,
                                    lv, blk, out + written);
        } else if (t == ER_TYPE_SHEEP && np >= 12) {
            static const int order[12] = { 7, 8, 9, 10, 11, 1, 2, 3, 4, 5, 6, 0 };
            CrRgba wool = sheep_wool_tint(ents[e].tape_pose ? ents[e].fleece_color : 0,
                                          ents[e].hurt_time > 0);
            if (ents[e].lm_lit == 2) {
                wool.r = (u8)(wool.r * ents[e].lm_mul_r + 0.5f);
                wool.g = (u8)(wool.g * ents[e].lm_mul_g + 0.5f);
                wool.b = (u8)(wool.b * ents[e].lm_mul_b + 0.5f);
            }
            for (int i = 0; i < 12; ++i) {
                int p = order[i];
                written += emit_box(&local[p], cs, sn, sc, fx, fy, fz,
                                    p >= 6 ? wool : tint,
                                    lv, blk, out + written);
            }
        } else {
            for (int p = 0; p < np; ++p)
                written += emit_box(&local[p], cs, sn, sc, fx, fy, fz, tint,
                                    lv, blk, out + written);
        }
    }
    return written;
}

CrTexture gm_entity_atlas(void) {
    CrTexture t;
    t.w = CR_MOB_ATLAS_W;
    t.h = CR_MOB_ATLAS_H;
    t.texels = (const CrRgba *)CR_MOB_ATLAS_RGBA;
    t.tile = CR_MOB_ATLAS_TILE;
    t.mip_levels = 0;
    for (int i = 0; i < 15; ++i) { t.mip[i] = 0; t.mipw[i] = 0; t.miph[i] = 0; }
    return t;
}
