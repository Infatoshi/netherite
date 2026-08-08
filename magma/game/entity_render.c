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
 * Entity type ids are EW_TYPE_* from blaze/core/ew_entity_store.h +
 * entity_hostile_spine.h + game/mob_live.h (hardcoded below to avoid an blaze
 * include dependency in the render path):
 *   2 zombie, 3 skeleton, 4 creeper, 5 spider, 6 enderman, 7 blaze,
 *   10 sheep, 11 pig, 12 cow, 13 chicken -> table-driven full models.
 *   8 crystal, 9 dragon -> dedicated full render paths below.
 *   0 NONE / 1 PLAYER -> skipped.
 *   21 GM_ENTITY_XP_ORB -> RenderXPOrb camera-facing billboard
 *     (gm_xp_orbs_emit; skipped here so it is not a marker box).
 *   anything else (20 projectile, ...) keeps the legacy single 0.6x1.8x0.4
 *   zombie-wrapped marker box (previous behavior for unmodeled types).
 */
#include "game/game.h"
#include "game/entity_render.h"
#include "assets/mob_atlas.h"
#include "assets/blockmodels.h"
#include "core/config.h"

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
#define ER_TYPE_DRAGON_FIREBALL 33
#define ER_TYPE_ARMOR_STAND 34
#define ER_TYPE_PIGMAN   15
#define ER_TYPE_SLIME    35
#define ER_TYPE_SILVERFISH 36
#define ER_TYPE_BOAT     37
#define ER_TYPE_CAVE_SPIDER 39
#define ER_TYPE_VILLAGER 40
#define ER_TYPE_MINECART_CHEST GM_VIEW_MINECART_CHEST
#define ER_TYPE_MINECART_FURNACE GM_VIEW_MINECART_FURNACE
#define ER_TYPE_MINECART_HOPPER GM_VIEW_MINECART_HOPPER
#define ER_TYPE_MINECART_TNT GM_VIEW_MINECART_TNT

#define ER_VERTS_PER_BOX 36  /* 6 faces * 2 tris * 3 verts */
#define ER_PI 3.14159265358979323846f
#define ER_DEG2RAD 0.017453292519943295f
#define ER_RAD2DEG 57.29577951308232f
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

/* ModelVillager(0), rendered at RenderVillager's 0.9375 scale. The child
 * nose is flattened at its exact head-relative pivot. Arms use the values
 * assigned by setRotationAngles, not their constructor pivot. */
#define VILLAGER_ARM_AX (-0.75f)
static const ErModel M_VILLAGER = { .nparts = 9, .scale = 0.9375f, .parts = {
    { CR_MOB_VILLAGER,  0,  0, -4,-10,-4, 8,10,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_VILLAGER, 24,  0, -1, -1,-6, 2, 4,2,  0,-2,0, 0,0,0, 0,0 },
    { CR_MOB_VILLAGER, 16, 20, -4,  0,-3, 8,12,6,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_VILLAGER,  0, 38, -4,  0,-3, 8,18,6,  0,0,0, 0,0,0, 0.5f,0 },
    { CR_MOB_VILLAGER, 44, 22, -8, -2,-2, 4, 8,4,  0,3,-1, VILLAGER_ARM_AX,0,0, 0,0 },
    { CR_MOB_VILLAGER, 44, 22,  4, -2,-2, 4, 8,4,  0,3,-1, VILLAGER_ARM_AX,0,0, 0,0 },
    { CR_MOB_VILLAGER, 40, 38, -4,  2,-2, 8, 4,4,  0,3,-1, VILLAGER_ARM_AX,0,0, 0,0 },
    { CR_MOB_VILLAGER,  0, 22, -2,  0,-2, 4,12,4, -2,12,0, 0,0,0, 0,0 },
    { CR_MOB_VILLAGER,  0, 22, -2,  0,-2, 4,12,4,  2,12,0, 0,0,0, 0,1 },
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

/* ModelSlime(16) outer (body + eyes + mouth). RenderSlime.preRenderCallback
 * scales by getSlimeSize() (item_meta); idle squish is identity.
 * Mouth addBox(0,21,-3.5, 1,1,1) — not centered at -0.5 (oracle ModelSlime). */
static const ErModel M_SLIME = { .nparts = 4, .scale = 1.0f, .parts = {
    { CR_MOB_SLIME, 0, 16, -3,17,-3, 6,6,6,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_SLIME, 32, 0, -3.25f,18,-3.5f, 2,2,2,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_SLIME, 32, 4, 1.25f,18,-3.5f, 2,2,2,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_SLIME, 32, 8, 0,21,-3.5f, 1,1,1,  0,0,0, 0,0,0, 0,0 },
} };

/* ModelSilverfish: body segments approximate (body / shell pieces). */
static const ErModel M_SILVERFISH = { .nparts = 3, .scale = 1.0f, .parts = {
    { CR_MOB_SILVERFISH, 20, 0, -1.5f,22,-0.5f, 3,2,1,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_SILVERFISH, 20, 0, -1.5f,21, 0.5f, 3,2,2,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_SILVERFISH,  2, 0, -0.5f,22, 2.5f, 1,1,1,  0,0,0, 0,0,0, 0,0 },
} };

/* ModelBoat has five hull boxes and two boxes in each paddle. It has a
 * dedicated affine emitter below because RenderBoat is not a living renderer
 * and its transform origin is unrelated to the shared y=24 model convention. */
static const ErModel M_BOAT = { .nparts = 9, .scale = 1.0f, .parts = {
    { CR_MOB_BOAT, 0,  0, -14,-9,-3, 28,16,3,  0,3, 1, ER_PI/2,0,0, 0,0 },
    { CR_MOB_BOAT, 0, 19, -13,-7,-1, 18, 6,2, -15,4,4, 0,ER_PI*1.5f,0, 0,0 },
    { CR_MOB_BOAT, 0, 27,  -8,-7,-1, 16, 6,2,  15,4,0, 0,ER_PI/2,0, 0,0 },
    { CR_MOB_BOAT, 0, 35, -14,-7,-1, 28, 6,2,   0,4,-9,0,ER_PI,0, 0,0 },
    { CR_MOB_BOAT, 0, 43, -14,-7,-1, 28, 6,2,   0,4, 9,0,0,0, 0,0 },
    { CR_MOB_BOAT,62,  0,  -1, 0,-5,  2, 2,18,  3,-5, 9,0,0,0, 0,0 },
    { CR_MOB_BOAT,62,  0,  -1,-3, 8,  1, 6, 7,  3,-5, 9,0,0,0, 0,0 },
    { CR_MOB_BOAT,62, 20,  -1, 0,-5,  2, 2,18,  3,-5,-9,0,0,0, 0,0 },
    { CR_MOB_BOAT,62, 20,   0,-3, 8,  1, 6, 7,  3,-5,-9,0,0,0, 0,0 },
} };

/* ModelMagmaCube (64x32): 8 stacked 8x1x8 segments + 4^3 core. Base scale 1;
 * RenderMagmaCube.preRenderCallback multiplies by getSlimeSize() (item_meta).
 * Segment squish (setLivingAnimations) applied per frame from limb_swing_amount. */
static const ErModel M_MAGMA = { .nparts = 9, .scale = 1.0f, .parts = {
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
    { CR_MOB_MINECART, 44, 10,  -9,-7,-1, 18,14,1,  0,22.1f, 0, -ER_PI/2.0f,0,0, 0,0 },
} };

/* ModelArmorStand: biped wood pieces followed by standRightSide/LeftSide,
 * standWaist and standBase. Arm visibility and base-plate visibility come
 * from GmEntityView.stand_flags; the default pose constants are owned by
 * EntityArmorStand, not ModelBiped's walk cycle. */
static const ErModel M_ARMOR_STAND = { .nparts = 10, .parts = {
    { CR_MOB_ARMORSTAND,  0,  0, -1,-7,-1,  2, 7, 2,  0, 0,0,  0,0,0, 0,0 },
    { CR_MOB_ARMORSTAND,  0, 26, -6, 0,-1.5f, 12,3,3,  0, 0,0,  0,0,0, 0,0 },
    { CR_MOB_ARMORSTAND, 24,  0, -2,-2,-1,  2,12,2, -5, 2,0,
      -15.0f*ER_DEG2RAD,0, 10.0f*ER_DEG2RAD, 0,0 },
    { CR_MOB_ARMORSTAND, 32, 16,  0,-2,-1,  2,12,2,  5, 2,0,
      -10.0f*ER_DEG2RAD,0,-10.0f*ER_DEG2RAD, 0,1 },
    { CR_MOB_ARMORSTAND,  8,  0, -1, 0,-1,  2,11,2, -1.9f,12,0,
      ER_DEG2RAD,0,ER_DEG2RAD, 0,0 },
    { CR_MOB_ARMORSTAND, 40, 16, -1, 0,-1,  2,11,2,  1.9f,12,0,
      -ER_DEG2RAD,0,-ER_DEG2RAD, 0,1 },
    { CR_MOB_ARMORSTAND, 16,  0, -3, 3,-1,  2, 7,2,  0, 0,0,  0,0,0, 0,0 },
    { CR_MOB_ARMORSTAND, 48, 16,  1, 3,-1,  2, 7,2,  0, 0,0,  0,0,0, 0,0 },
    { CR_MOB_ARMORSTAND,  0, 48, -4,10,-1,  8, 2,2,  0, 0,0,  0,0,0, 0,0 },
    { CR_MOB_ARMORSTAND,  0, 32, -6,11,-6, 12, 1,12, 0,12,0,  0,0,0, 0,0 },
} };

/* RenderArmorStand installs ModelArmorStandArmor(1.0) for layer 1 and
 * ModelArmorStandArmor(0.5) for leggings/layer 2. These are the visible
 * ModelBiped boxes for LayerBipedArmor's CHEST, LEGS, FEET, HEAD order. The
 * sprite is replaced per equipped material immediately before emission. */
static const ErPart ARMOR_CHEST_PARTS[] = {
    { CR_MOB_IRON_LAYER_1, 16,16, -4, 0,-2, 8,12,4,  0,0,0,
      0,0,0, 1.0f,0 },
    { CR_MOB_IRON_LAYER_1, 40,16, -3,-2,-2, 4,12,4, -5,2,0,
      -15.0f*ER_DEG2RAD,0, 10.0f*ER_DEG2RAD, 1.0f,0 },
    { CR_MOB_IRON_LAYER_1, 40,16, -1,-2,-2, 4,12,4,  5,2,0,
      -10.0f*ER_DEG2RAD,0,-10.0f*ER_DEG2RAD, 1.0f,1 },
};
static const ErPart ARMOR_LEGS_PARTS[] = {
    { CR_MOB_IRON_LAYER_2, 16,16, -4,0,-2, 8,12,4,  0,0,0,
      0,0,0, 0.5f,0 },
    { CR_MOB_IRON_LAYER_2,  0,16, -2,0,-2, 4,12,4, -1.9f,11,0,
       ER_DEG2RAD,0, ER_DEG2RAD, 0.5f,0 },
    { CR_MOB_IRON_LAYER_2,  0,16, -2,0,-2, 4,12,4,  1.9f,11,0,
      -ER_DEG2RAD,0,-ER_DEG2RAD, 0.5f,1 },
};
static const ErPart ARMOR_FEET_PARTS[] = {
    { CR_MOB_IRON_LAYER_1, 0,16, -2,0,-2, 4,12,4, -1.9f,11,0,
       ER_DEG2RAD,0, ER_DEG2RAD, 1.0f,0 },
    { CR_MOB_IRON_LAYER_1, 0,16, -2,0,-2, 4,12,4,  1.9f,11,0,
      -ER_DEG2RAD,0,-ER_DEG2RAD, 1.0f,1 },
};
static const ErPart ARMOR_HEAD_PARTS[] = {
    { CR_MOB_IRON_LAYER_1,  0,0, -4,-8,-4, 8,8,8, 0,1,0,
      0,0,0, 1.0f,0 },
    { CR_MOB_IRON_LAYER_1, 32,0, -4,-8,-4, 8,8,8, 0,1,0,
      0,0,0, 1.5f,0 },
};

/* Legacy marker box for unmodeled types (dragon/crystal/projectile...):
 * one 0.6x1.8x0.4 box wrapped with the whole zombie skin, as before. */
static const ErModel M_MARKER = { 1, {
    { CR_MOB_ZOMBIE, 0, 0, -4.8f,0,-3.2f, 0,0,0,  0,24,0,  0,0,0, 0,0 },
} };
/* (dims 0 flags the special legacy wrap; geometry hardcoded in emit_marker) */

#define ER_TYPE_XP_ORB 21  /* GM_ENTITY_XP_ORB */

static const ErModel *er_model_for_type(int type) {
    switch (type) {
        case ER_TYPE_NONE:
        case ER_TYPE_PLAYER:   return 0;         /* skipped */
        case ER_TYPE_XP_ORB:   return 0;         /* gm_xp_orbs_emit (billboard) */
        case 22 /* GM_VIEW_ITEM */: return 0;    /* drawn by the item pass */
        case 30 /* GM_VIEW_BILLBOARD */: return 0; /* item pass (camera-facing) */
        case 38 /* GM_VIEW_FALLING_BLOCK */: return 0; /* gm_falling_blocks_emit */
        case GM_VIEW_EXPLOSION_LARGE: return 0; /* particle pass */
        case 44 /* GM_VIEW_TNT_PRIMED */: return 0; /* gm_falling_blocks_emit */
        case ER_TYPE_DRAGON_FIREBALL: return 0; /* dedicated item-atlas billboard */
        case ER_TYPE_ZOMBIE:   return &M_ZOMBIE;
        case ER_TYPE_PIGMAN:   return &M_ZOMBIE; /* same biped; pigman skin via .skin */
        case ER_TYPE_SKELETON: return &M_SKELETON;
        case ER_TYPE_WITHER_SKELETON: return &M_WITHER_SKELETON;
        case ER_TYPE_CREEPER:  return &M_CREEPER;
        case ER_TYPE_SPIDER:   return &M_SPIDER;
        case ER_TYPE_CAVE_SPIDER: return &M_SPIDER;
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
        case ER_TYPE_VILLAGER: return &M_VILLAGER;
        case ER_TYPE_BAT:      return &M_BAT;
        case ER_TYPE_LLAMA:    return &M_LLAMA;
        case ER_TYPE_GHAST:    return &M_GHAST;
        case ER_TYPE_MAGMA:    return &M_MAGMA;
        case ER_TYPE_SLIME:    return &M_SLIME;
        case ER_TYPE_SILVERFISH: return &M_SILVERFISH;
        case ER_TYPE_BOAT:     return &M_BOAT;
        case ER_TYPE_MINECART:
        case ER_TYPE_MINECART_CHEST:
        case ER_TYPE_MINECART_FURNACE:
        case ER_TYPE_MINECART_HOPPER:
        case ER_TYPE_MINECART_TNT:
            return &M_MINECART;
        case ER_TYPE_ARMOR_STAND: return &M_ARMOR_STAND;
        default:               return &M_MARKER; /* legacy marker box */
    }
}

static int er_is_minecart(int type) {
    return type == ER_TYPE_MINECART ||
           type == ER_TYPE_MINECART_CHEST ||
           type == ER_TYPE_MINECART_FURNACE ||
           type == ER_TYPE_MINECART_HOPPER ||
           type == ER_TYPE_MINECART_TNT;
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

/* RenderLivingBase.applyRotations death keel, in radians about the local Z:
 *   f = sqrt((deathTime + partialTicks - 1) / 20 * 1.6), clamped to 1
 *   rotate(f * getDeathMaxRotation, 0, 0, 1)   (90 deg, not overridden by any
 *                                               mob renderer in 1.11.2)
 * Capture partial is 1.0 throughout this file, so the numerator is deathTime;
 * measured, partial=0 (deathTime-1) costs 155k unexplained px on
 * scenario_portal_fortress_blaze relative to partial=1.
 * deathTime is recorded per entity row by the tape (field 11 of a 14+ field
 * row -> ent_view "death" -> GmEntityView.death_time); nothing here is
 * inferred from health, which vanilla's applyRotations never consults. */
float er_death_roll(const GmEntityView *v) {
    if (v->death_time <= 0) return 0.0f;
    float f = sqrtf((float)v->death_time / 20.0f * 1.6f);
    if (f > 1.0f) f = 1.0f;
    return f * (ER_PI / 2.0f);
}

/* emit one vanilla box: model-space transform -> world space -> 12 tris.
 * cs/sn are cos/sin of the whole-entity yaw rotation; (fx,fy,fz) = feet.
 * rc/rs are cos/sin of the RenderLivingBase.applyRotations death keel about
 * the local Z axis (identity = 1,0). Vanilla call order is
 *   translate(pos) . rotateY(180-yaw) . rotateZ(deathRoll) . prepareScale,
 * so the roll acts on the flipped/scaled model vector before the body yaw and
 * pivots on the entity's feet (the origin at that point in the stack).
 * tint multiplies the white vertex colour (hurt flash uses red-leaning). */
static int emit_box(const ErPart *p, float cs, float sn, float sc,
                    float fx, float fy, float fz, CrRgba tint,
                    float lv, float blk, float rc, float rs, CrVertex *out) {
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
        /* death keel about local Z (GL rotate(a,0,0,1)) before the body yaw */
        if (rs != 0.0f) {
            float rx2 = wx * rc - wy * rs;
            wy        = wx * rs + wy * rc;
            wx        = rx2;
        }
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

/* RenderHelper.enableStandardItemLighting: two directional lights, diffuse
 * 0.6 each, model ambient 0.4, colorMaterial AMBIENT_AND_DIFFUSE. The light
 * POSITIONs are set while the modelview is the camera matrix, so the
 * directions are fixed in WORLD space and the shade is a pure function of the
 * world face normal:
 *     shade = min(1, 0.4 + 0.6*max(0, n.l0) + 0.6*max(0, n.l1))
 * On an axis-aligned box this gives 1.0 up / 0.737 north-south / 0.496
 * east-west / 0.4 down, which er_shade's block-face quantization (1/0.8/0.6/
 * 0.5) approximates well enough for mob models. ModelEnderCrystal's cubes are
 * rotated 60 degrees about (0.7071,0,0.7071), so every face normal is oblique
 * and the quantization lands on the wrong bucket: measured on
 * scenario_dragon_kill the whole crystal came out a uniform ~1.47x too bright
 * (magma 0.6 where vanilla gives ~0.41). Exact only for boxes drawn with unit
 * normals under a uniform scale, which is every ModelBase box. */
static float er_shade_item(float nx, float ny, float nz) {
    float n = sqrtf(nx*nx + ny*ny + nz*nz);
    if (n <= 0.0f) return 1.0f;
    nx /= n; ny /= n; nz /= n;
    /* Vec3d(0.2, 1.0, -0.7).normalize() and its x/z mirror. */
    const float il = 1.0f / 1.2449899f;
    float d0 = (nx*0.2f + ny*1.0f + nz*-0.7f) * il;
    float d1 = (nx*-0.2f + ny*1.0f + nz*0.7f) * il;
    if (d0 < 0.0f) d0 = 0.0f;
    if (d1 < 0.0f) d1 = 0.0f;
    float s = 0.4f + 0.6f*d0 + 0.6f*d1;
    return s > 1.0f ? 1.0f : s;
}

/* shade_mode 0: er_shade block-face quantization (mob/dragon models).
 * shade_mode 1: er_shade_item exact standard item lighting. */
static int er_aff_box_m(const ErAff *a, int sprite, int uvscale, int mirror,
                        int shade_mode, int u, int v,
                        float bx, float by, float bz, int dx, int dy, int dz,
                        CrRgba tint, float lv, float blk, CrVertex *out);

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
    return er_aff_box_m(a, sprite, uvscale, mirror, 0, u, v,
                        bx, by, bz, dx, dy, dz, tint, lv, blk, out);
}

static int er_aff_box_m(const ErAff *a, int sprite, int uvscale, int mirror,
                        int shade_mode, int u, int v,
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
        float shade = shade_mode ? er_shade_item(nx, ny, nz)
                                 : er_shade(nx, ny, nz);
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

static void er_boat_part_affine(const ErAff *base, ErAff *part,
                                float rx, float ry, float rz,
                                float ax, float ay, float az) {
    *part = *base;
    er_aff_translate(part, rx * 0.0625f, ry * 0.0625f, rz * 0.0625f);
    if (az != 0.0f) er_aff_rot_z(part, az);
    if (ay != 0.0f) er_aff_rot_y(part, ay);
    if (ax != 0.0f) er_aff_rot_x(part, ax);
}

/* RenderBoat + ModelBoat. ModelBoat's textureWidth/Height are 128x64, matching
 * boat_oak.png, so UV texels map directly to the packed sprite. */
static int emit_boat(const GmEntityView *ent, CrVertex *out) {
    ErAff base, part;
    er_aff_identity(&base);
    er_aff_translate(&base, ent->x, ent->y + 0.375f, ent->z);
    er_aff_rot_y(&base, 180.0f - ent->yaw);
    er_aff_scale3(&base, -1.0f, -1.0f, 1.0f);
    er_aff_rot_y(&base, 90.0f);

    CrRgba tint = { 255, 255, 255, 255 };
    float lv = 15.0f, blk = 0.0f;
    if (ent->lm_lit == 1) {
        lv = ent->lm_light;
        blk = ent->lm_blk;
    } else if (ent->lm_lit == 2) {
        lv = 1.0f;
        tint.r = (u8)(255.0f * ent->lm_mul_r + 0.5f);
        tint.g = (u8)(255.0f * ent->lm_mul_g + 0.5f);
        tint.b = (u8)(255.0f * ent->lm_mul_b + 0.5f);
    }

    int n = 0;
#define BOAT_BOX(U,V,BX,BY,BZ,DX,DY,DZ) \
    n += er_aff_box_m(&part, CR_MOB_BOAT, 1, 0, 1, (U), (V), \
                      (BX), (BY), (BZ), (DX), (DY), (DZ), tint, lv, blk, out+n)
    er_boat_part_affine(&base, &part, 0, 3, 1, 90, 0, 0);
    BOAT_BOX(0, 0, -14, -9, -3, 28, 16, 3);
    er_boat_part_affine(&base, &part, -15, 4, 4, 0, 270, 0);
    BOAT_BOX(0, 19, -13, -7, -1, 18, 6, 2);
    er_boat_part_affine(&base, &part, 15, 4, 0, 0, 90, 0);
    BOAT_BOX(0, 27, -8, -7, -1, 16, 6, 2);
    er_boat_part_affine(&base, &part, 0, 4, -9, 0, 180, 0);
    BOAT_BOX(0, 35, -14, -7, -1, 28, 6, 2);
    er_boat_part_affine(&base, &part, 0, 4, 9, 0, 0, 0);
    BOAT_BOX(0, 43, -14, -7, -1, 28, 6, 2);

    for (int p = 0; p < 2; ++p) {
        float f = (ent->boat_paddle[p] - 0.01f) * 40.0f;
        float lerp_x = (sinf(-f) + 1.0f) * 0.5f;
        float lerp_y = (sinf(-f + 1.0f) + 1.0f) * 0.5f;
        float ax = (-60.0f) + lerp_x * 45.0f;
        float ay = -45.0f + lerp_y * 90.0f;
        if (p == 1) ay = 180.0f - ay;
        er_boat_part_affine(&base, &part, 3, -5, p ? -9 : 9,
                            ax, ay, 11.25f);
        BOAT_BOX(62, p ? 20 : 0, -1, 0, -5, 2, 2, 18);
        BOAT_BOX(62, p ? 20 : 0, p ? 0.001f : -1.001f, -3, 8,
                 1, 6, 7);
    }
#undef BOAT_BOX
    return n;
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
        written += er_aff_box_m(&a, CR_MOB_ENDERCRYSTAL, 2, 0, 1, 0, 16,
                                -6, 0, -6, 12, 4, 12, tint, lv, blk,
                                out + written);
    er_aff_rot_y(&a, f * 3.0f);
    er_aff_translate(&a, 0.0f, 0.8f + f1 * 0.2f, 0.0f);
    er_aff_rot_axis(&a, 60.0f, 0.7071f, 0.0f, 0.7071f);
    written += er_aff_box_m(&a, CR_MOB_ENDERCRYSTAL, 2, 0, 1, 0, 0,
                            -4, -4, -4, 8, 8, 8, tint, lv, blk, out + written);
    er_aff_scale(&a, 0.875f);
    er_aff_rot_axis(&a, 60.0f, 0.7071f, 0.0f, 0.7071f);
    er_aff_rot_y(&a, f * 3.0f);
    written += er_aff_box_m(&a, CR_MOB_ENDERCRYSTAL, 2, 0, 1, 0, 0,
                            -4, -4, -4, 8, 8, 8, tint, lv, blk, out + written);
    er_aff_scale(&a, 0.875f);
    er_aff_rot_axis(&a, 60.0f, 0.7071f, 0.0f, 0.7071f);
    er_aff_rot_y(&a, f * 3.0f);
    written += er_aff_box_m(&a, CR_MOB_ENDERCRYSTAL, 2, 0, 1, 32, 0,
                            -4, -4, -4, 8, 8, 8, tint, lv, blk, out + written);
    return written;
}

/* RenderDragon.renderCrystalBeams, called by RenderEnderCrystal after its model.
 * Vanilla submits an 18-vertex GL_TRIANGLE_STRIP with culling disabled. Magma's
 * rasterizer always culls, so each of the resulting 16 triangles is emitted in
 * both windings (96 verts) to preserve the two-sided draw without changing the
 * shared raster contract. The separate beam pass binds the standalone 16x256
 * texture and repeats both UV axes. RenderManager leaves the owning entity's
 * lightmap coordinates active for RenderDragon/RenderEnderCrystal, so every
 * beam vertex carries those same sky/block levels. */
#define ER_CRYSTAL_BEAM_VERTS 96

static CrVec3 er_aff_point(const ErAff *a, float x, float y, float z) {
    CrVec3 p;
    p.x = a->m[0][0]*x + a->m[0][1]*y + a->m[0][2]*z + a->t[0];
    p.y = a->m[1][0]*x + a->m[1][1]*y + a->m[1][2]*z + a->t[1];
    p.z = a->m[2][0]*x + a->m[2][1]*y + a->m[2][2]*z + a->t[2];
    return p;
}

static int emit_beam_geometry(
        float source_x, float source_y, float source_z,
        float target_x, float target_y, float target_z,
        float render_x, float render_y, float render_z,
        float phase, CrVertex *out) {
    float dx = target_x - source_x;
    float dy = target_y - 1.0f - source_y;
    float dz = target_z - source_z;
    float horizontal = (float)sqrt((double)(dx*dx + dz*dz));
    float length = (float)sqrt((double)(dx*dx + dy*dy + dz*dz));

    ErAff a;
    er_aff_identity(&a);
    a.t[0] = render_x;
    a.t[1] = render_y + 2.0f;
    a.t[2] = render_z;
    er_aff_rot_y(&a, (float)(-atan2((double)dz, (double)dx)) * ER_RAD2DEG
                         - 90.0f);
    er_aff_rot_x(&a, (float)(-atan2((double)horizontal, (double)dy))
                         * ER_RAD2DEG - 90.0f);

    float v0 = -phase * 0.01f;
    float v1 = length / 32.0f - phase * 0.01f;
    CrVertex strip[18];
    for (int j = 0; j <= 8; ++j) {
        int ring = j & 7;
        float angle = (float)ring * (ER_PI * 2.0f) / 8.0f;
        float rx = sinf(angle) * 0.75f;
        float ry = cosf(angle) * 0.75f;
        float u = (float)ring / 8.0f;
        CrVertex lo = {0}, hi = {0};
        lo.pos = er_aff_point(&a, rx * 0.2f, ry * 0.2f, 0.0f);
        hi.pos = er_aff_point(&a, rx, ry, length);
        lo.uv = (CrVec2){u, v0}; hi.uv = (CrVec2){u, v1};
        lo.light = hi.light = 1.0f;
        lo.ao = hi.ao = 1.0f;
        lo.tint = (CrRgba){0, 0, 0, 255};
        hi.tint = (CrRgba){255, 255, 255, 255};
        strip[j*2] = lo;
        strip[j*2+1] = hi;
    }

    int written = 0;
    for (int i = 0; i < 16; ++i) {
        int a0, b0, c0 = i + 2;
        if ((i & 1) == 0) { a0 = i; b0 = i + 1; }
        else              { a0 = i + 1; b0 = i; }
        out[written++] = strip[a0];
        out[written++] = strip[b0];
        out[written++] = strip[c0];
        out[written++] = strip[c0];
        out[written++] = strip[b0];
        out[written++] = strip[a0];
    }
    return written;
}

static int emit_crystal_beam(const GmEntityView *ent, CrVertex *out) {
    float tx = (float)ent->beam_x + 0.5f;
    float ty = (float)ent->beam_y + 0.5f;
    float tz = (float)ent->beam_z + 0.5f;
    float phase = ent->crystal_rot + 1.0f; /* capture partialTicks == 1 */
    float bob = sinf(phase * 0.2f) / 2.0f + 0.5f;
    bob = bob * bob + bob;
    /* RenderEnderCrystal translates the black ring to the target block with
     * the crystal's bob, while direction remains anchored to the block. */
    return emit_beam_geometry(
        tx,ty,tz,ent->x,ent->y,ent->z,
        tx,ty-0.3f+bob*0.4f,tz,phase,out);
}

static int emit_dragon_heal_beam(const GmEntityView *ent, CrVertex *out) {
    float crystal_phase=(float)ent->heal_crystal_ticks+1.0f;
    float bob=sinf(crystal_phase*0.2f)/2.0f+0.5f;
    bob=(bob*bob+bob)*0.2f;
    return emit_beam_geometry(
        ent->x,ent->y,ent->z,
        ent->heal_x,ent->heal_y+bob,ent->heal_z,
        ent->x,ent->y,ent->z,
        (float)ent->ticks_existed+1.0f,out);
}

static void apply_beam_light(const GmEntityView *ent, CrVertex *out, int n) {
    for(int i=0;i<n;++i){
        if(ent->lm_lit==1){
            out[i].light=ent->lm_light;
            out[i].blk=ent->lm_blk;
        }else if(ent->lm_lit==2){
            out[i].tint.r=(u8)((float)out[i].tint.r*ent->lm_mul_r+0.5f);
            out[i].tint.g=(u8)((float)out[i].tint.g*ent->lm_mul_g+0.5f);
            out[i].tint.b=(u8)((float)out[i].tint.b*ent->lm_mul_b+0.5f);
        }
    }
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
#define ER_TYPE_DRAGON 9

typedef struct {
    int ent_id, inited;
    int idx;
    float yaw[64], y[64];
    float pend_yaw, pend_y;   /* row awaiting the NEXT tick's push (see below) */
} ErDragonRing;
static ErDragonRing er_dragon_ring;   /* one dragon per fight */

/* ---- geometry-oracle dump (geom_dump=path) ---------------------------
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
        const char *p = cr_cfg()->geom_dump;
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
 * per tick.
 *
 * PHASE. Vanilla's push (EntityDragon.onLivingUpdate:239-240) runs BEFORE the
 * tick's own motion: the interpolation block that advances rotationYaw/posY
 * sits at :242-255 and the phase movement below it. So ringBuffer[idx] holds
 * the pose as of the END of tick T-1, while the render at partialTicks=1.0
 * draws the body at the END of tick T. The tape's ent row is post-tick state,
 * so magma pushes the PREVIOUS row and holds the current one in pend_*; that
 * makes ring[] a literal ringBuffer[] and keeps er_dragon_mo a literal
 * getMovementOffsets. Pushing the current row instead ran the whole
 * neck/head/tail chain and the applyRotations body yaw one tick early.
 *
 * DEATH. `health <= 0` takes onLivingUpdate's :191-197 branch (explosion
 * particles) and never reaches the push at :225-240, so the ring - and with it
 * the entire model pose - FREEZES at death. onDeathUpdate meanwhile spins
 * rotationYaw +20 deg/tick (EntityDragon.java:701) and that spin is recorded
 * into the tape, but vanilla never feeds it to the ring: applyRotations reads
 * getMovementOffsets(7), not rotationYaw. Pushing it rotated magma's dying
 * dragon ~20 deg/tick, reading as a mirrored body within ~9 death ticks. */
void gm_dragon_pose_tick(int ent_id, float yaw, float y, float health) {
    ErDragonRing *rb = &er_dragon_ring;
    if (!rb->inited || rb->ent_id != ent_id) {
        rb->inited = 1;
        rb->ent_id = ent_id;
        rb->idx = 0;
        for (int i = 0; i < 64; ++i) { rb->yaw[i] = yaw; rb->y[i] = y; }
        rb->pend_yaw = yaw; rb->pend_y = y;
        return;
    }
    if (health <= 0.0f) return;          /* dead: ring frozen, see above */
    rb->idx = (rb->idx + 1) & 63;
    rb->yaw[rb->idx] = rb->pend_yaw;
    rb->y[rb->idx] = rb->pend_y;
    rb->pend_yaw = yaw; rb->pend_y = y;
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

static int emit_dragon(const GmEntityView *ent, CrVertex *out, int cap) {
    if (cap < 65 * ER_VERTS_PER_BOX) return 0;

    ErDragonRing *rb = &er_dragon_ring;
    gm_dragon_pose_tick(ent->ent_id, ent->yaw, ent->y, ent->health);
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
    /* death dissolve: RenderDragon.renderModel alphaFunc(GL_GREATER, f) on
     * dragon_exploding.png per texel (f = deathTicks/200), then repaints the
     * skin. Geometry is always emitted; cr_shade discards via alpha_mask when
     * light < 0 and ao holds f. Fully dissolved at f=1 (no fragments pass). */
    float deadf = ent->death_ticks > 0 ? (float)ent->death_ticks / 200.0f
                                       : 0.0f;
    if (deadf >= 1.0f) return 0;
#define DBOX(AF, U, V, X, Y, Z, DX, DY, DZ, MIR) do { \
        int _ds = w; \
        float _lv = (deadf > 0.0f) ? -1.0f : (lv); \
        w += er_aff_box((AF), CR_MOB_DRAGON, 1, (MIR), (U), (V), (X), (Y), (Z), \
                        (DX), (DY), (DZ), tint, _lv, blk, out + w); \
        if (deadf > 0.0f) { \
            for (int _i = _ds; _i < w; ++_i) { \
                out[_i].light = -1.0f; \
                out[_i].ao = deadf; \
            } \
        } \
    } while (0)

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
        /* Tape pigmen historically fold to zombie silhouette; live product uses
         * ER_TYPE_PIGMAN=15 with the same model + pigman skin. Map tape name to
         * the live type so fill_views and ghosts share one id. */
        { "EntityPigZombie",      ER_TYPE_PIGMAN },
        { "EntitySkeleton",       ER_TYPE_SKELETON },
        { "EntityStray",          ER_TYPE_SKELETON },
        { "EntityWitherSkeleton", ER_TYPE_WITHER_SKELETON },
        { "EntityCreeper",        ER_TYPE_CREEPER },
        { "EntitySpider",         ER_TYPE_SPIDER },
        { "EntityCaveSpider",     ER_TYPE_CAVE_SPIDER },
        { "EntityEnderman",       ER_TYPE_ENDERMAN },
        { "EntityBlaze",          ER_TYPE_BLAZE },
        { "EntitySheep",          ER_TYPE_SHEEP },
        { "EntityPig",            ER_TYPE_PIG },
        { "EntityCow",            ER_TYPE_COW },
        { "EntityMooshroom",      ER_TYPE_COW },
        { "EntityChicken",        ER_TYPE_CHICKEN },
        { "EntitySquid",          ER_TYPE_SQUID },
        { "EntityWitch",          ER_TYPE_WITCH },
        { "EntityVillager",       ER_TYPE_VILLAGER },
        { "EntityBat",            ER_TYPE_BAT },
        { "EntityLlama",          ER_TYPE_LLAMA },
        { "EntityGhast",          ER_TYPE_GHAST },
        { "EntityMagmaCube",      ER_TYPE_MAGMA },
        { "EntitySlime",          ER_TYPE_SLIME },
        { "EntitySilverfish",     ER_TYPE_SILVERFISH },
        { "EntityBoat",           ER_TYPE_BOAT },
        { "EntityMinecartEmpty",  ER_TYPE_MINECART },
        { "EntityMinecartChest",  ER_TYPE_MINECART_CHEST },
        { "EntityMinecartFurnace",ER_TYPE_MINECART_FURNACE },
        { "EntityMinecartHopper", ER_TYPE_MINECART_HOPPER },
        { "EntityMinecartTNT",    ER_TYPE_MINECART_TNT },
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
        /* RenderFireball uses the fire_charge item model's particle icon;
         * RenderManager registers EntitySmallFireball at scale 0.5. */
        /* RenderManager: EntitySmallFireball scale 0.5, EntityLargeFireball 2.0;
         * both use RenderFireball + fire_charge particle icon. Live views mark
         * large shots via gm_entity_patch_large_fireballs (type morph). */
        { "EntitySmallFireball",  30 /* GM_VIEW_BILLBOARD */ },
        { "EntityLargeFireball",  ER_TYPE_DRAGON_FIREBALL /* scale 2, fire_charge UV */ },
        { "EntityFireball",       30 /* GM_VIEW_BILLBOARD */ },
        /* RenderDragonFireball binds its own texture and scales the direct
         * camera-facing quad by 2.0. */
        { "EntityDragonFireball", ER_TYPE_DRAGON_FIREBALL },
        { "EntityArmorStand",     ER_TYPE_ARMOR_STAND },
        /* RenderXPOrb camera-facing billboard (gm_xp_orbs_emit). */
        { "EntityXPOrb",          ER_TYPE_XP_ORB },
        /* RenderFallingBlock: full-size block model (gm_falling_blocks_emit). */
        { "EntityFallingBlock",   38 /* GM_VIEW_FALLING_BLOCK */ },
        /* RenderTNTPrimed: lifted TNT block model (gm_falling_blocks_emit). */
        { "EntityTNTPrimed",      44 /* GM_VIEW_TNT_PRIMED */ },
    };
    if (!name) return -1;
    for (unsigned i = 0; i < sizeof MAP / sizeof MAP[0]; ++i)
        if (!strcmp(name, MAP[i].name)) return MAP[i].type;
    return -1;
}

/* EntityXPOrb.getTextureByXP: tier index 0..10 into experience_orb.png. */
static int er_xp_texture_tier(int xp_value) {
    if (xp_value >= 2477) return 10;
    if (xp_value >= 1237) return 9;
    if (xp_value >= 617)  return 8;
    if (xp_value >= 307)  return 7;
    if (xp_value >= 149)  return 6;
    if (xp_value >= 73)   return 5;
    if (xp_value >= 37)   return 4;
    if (xp_value >= 17)   return 3;
    if (xp_value >= 7)    return 2;
    if (xp_value >= 3)    return 1;
    return 0;
}

/* RenderXPOrb.doRender: camera-facing quad on experience_orb.png.
 *   T(pos) T(0,0.1,0) Ry(180-playerViewY) Rx(-playerViewX) S(0.3)
 * verts (-.5,-.25,0)..(.5,.75,0); UV from getTextureByXP; colour from xpColor
 * phase; alpha 128. xpValue in item_id (or health for live fill), xpColor in
 * item_meta (legacy age when meta==0 and age set). World lighting via lm_*. */
int gm_xp_orbs_emit(const GmEntityView *ents, int n, float view_yaw,
                    float view_pitch, CrVertex *out, int max) {
    if (!ents || !out || max < 6) return 0;
    const CrMobSprite *spr = &CR_MOB_SPRITES[CR_MOB_EXPERIENCE_ORB];
    const float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
    const float sw = (float)(spr->x1 - spr->x0); /* native 64 */
    const float sh = (float)(spr->y1 - spr->y0);
    float yr = (180.0f - view_yaw) * ER_DEG2RAD;
    float pr = -view_pitch * ER_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    static const float CORN[4][2] = {
        { -0.5f, -0.25f }, {  0.5f, -0.25f },
        {  0.5f,  0.75f }, { -0.5f,  0.75f },
    };
    static const int TRI[6] = { 0, 1, 2, 0, 2, 3 };
    int written = 0;
    for (int e = 0; e < n; ++e) {
        if (ents[e].type != ER_TYPE_XP_ORB) continue;
        if (written + 6 > max) break;
        int xp_value = ents[e].item_id > 0 ? ents[e].item_id
                     : (ents[e].health > 0 ? (int)ents[e].health : 1);
        int tier = er_xp_texture_tier(xp_value);
        /* UV in skin-texel space of the 64x64 sheet, then into the atlas. */
        float u0 = (float)(tier % 4 * 16 + 0) / 64.0f;
        float u1 = (float)(tier % 4 * 16 + 16) / 64.0f;
        float v0 = (float)(tier / 4 * 16 + 0) / 64.0f;
        float v1 = (float)(tier / 4 * 16 + 16) / 64.0f;
        float au0 = ((float)spr->x0 + u0 * sw) / aw;
        float au1 = ((float)spr->x0 + u1 * sw) / aw;
        float av0 = ((float)spr->y0 + v0 * sh) / ah;
        float av1 = ((float)spr->y0 + v1 * sh) / ah;
        /* colour: (sin(f9)+1)*0.5*255 red, 255 green, (sin(f9+4.18879)+1)*0.1*255 blue */
        int xp_color = ents[e].item_meta;
        if (xp_color <= 0 && ents[e].age > 0) xp_color = ents[e].age;
        float f9 = ((float)xp_color /* + partialTicks=0 */) / 2.0f;
        int cr = (int)((sinf(f9 + 0.0f) + 1.0f) * 0.5f * 255.0f);
        int cg = 255;
        int cb = (int)((sinf(f9 + 4.1887903f) + 1.0f) * 0.1f * 255.0f);
        if (cr < 0) cr = 0; else if (cr > 255) cr = 255;
        if (cb < 0) cb = 0; else if (cb > 255) cb = 255;
        CrRgba tint = { (u8)cr, (u8)cg, (u8)cb, 128 };
        float lv = 1.0f, blk = 0.0f;
        if (ents[e].lm_lit == 1) {
            lv = ents[e].lm_light; blk = ents[e].lm_blk;
        } else if (ents[e].lm_lit == 2) {
            tint.r = (u8)(tint.r * ents[e].lm_mul_r + 0.5f);
            tint.g = (u8)(tint.g * ents[e].lm_mul_g + 0.5f);
            tint.b = (u8)(tint.b * ents[e].lm_mul_b + 0.5f);
        }
        /* getBrightnessForRender boosts block light; leave levels as sampled. */
        static const float UVS[4][2] = {
            { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 },
        };
        CrVertex quad[4];
        for (int c = 0; c < 4; ++c) {
            float px = CORN[c][0], py = CORN[c][1], pz = 0.0f;
            float ty = py * cp - pz * sp, tz = py * sp + pz * cp;
            py = ty; pz = tz;
            float tx = px * cy + pz * sy;
            tz = -px * sy + pz * cy;
            px = tx; pz = tz;
            const float scale = 0.3f;
            CrVertex vtx;
            vtx.pos.x = ents[e].x + px * scale;
            vtx.pos.y = ents[e].y + 0.1f + py * scale;
            vtx.pos.z = ents[e].z + pz * scale;
            vtx.uv.x = au0 + UVS[c][0] * (au1 - au0);
            vtx.uv.y = av0 + UVS[c][1] * (av1 - av0);
            vtx.light = lv;
            vtx.blk = blk;
            vtx.tint = tint;
            vtx.ao = 1.0f;
            quad[c] = vtx;
        }
        for (int k = 0; k < 6; ++k) out[written++] = quad[TRI[k]];
    }
    return written;
}

/* GM_VIEW_BILLBOARD types -> the item id RenderSnowball draws (getStackToRender). */
int gm_entity_billboard_item(const char *name) {
    if (!name) return 0;
    if (!strcmp(name, "EntityEnderPearl")) return 368;
    if (!strcmp(name, "EntityEnderEye"))   return 381;
    if (!strcmp(name, "EntitySnowball"))   return 332;
    if (!strcmp(name, "EntityEgg"))        return 344;
    if (!strcmp(name, "EntitySmallFireball")) return 385;
    if (!strcmp(name, "EntityLargeFireball")) return 385;
    if (!strcmp(name, "EntityFireball")) return 385;
    if (!strcmp(name, "EntityDragonFireball")) return 9003;
    return 0;
}

/* RenderManager registers EntityLargeFireball at scale 2.0 and EntitySmallFireball
 * at 0.5, both with the fire_charge particle icon. Live projectile views collapse
 * both to GM_VIEW_BILLBOARD+385 (item_render scale 0.5). Morph large shots to
 * GM_VIEW_DRAGON_FIREBALL with item_id 385 so the existing billboard path uses
 * scale 2.0 while keeping the fire_charge sprite (item_id selects the UV).
 * item_meta is set to 2 so the fire-overlay pass can still treat them as fiery
 * (dragon fireballs keep item_id 9003 and never get the fire layers). */
void gm_entity_patch_large_fireballs(const int *proj_types, int nproj,
                                     GmEntityView *views, int nviews) {
    if (!views || nviews <= 0) return;
    int vi = 0;
    if (proj_types && nproj > 0) {
        for (int p = 0; p < nproj && vi < nviews; ++p) {
            /* Skip non-fireball slots only when types array is dense active list. */
            int t = proj_types[p];
            if (t != 3 && t != 5) continue;
            /* Advance to next fireball-like view. */
            while (vi < nviews &&
                   !(views[vi].type == 30 /* BILLBOARD */ && views[vi].item_id == 385) &&
                   !(views[vi].type == ER_TYPE_DRAGON_FIREBALL && views[vi].item_id == 385))
                ++vi;
            if (vi >= nviews) break;
            if (t == 5) {
                views[vi].type = ER_TYPE_DRAGON_FIREBALL;
                views[vi].item_id = 385;
                views[vi].item_meta = 2; /* large + fiery */
            } else {
                views[vi].item_meta = 1; /* small */
            }
            ++vi;
        }
        return;
    }
    /* No projectile list: honour item_meta already set by callers/tests. */
    for (int i = 0; i < nviews; ++i) {
        if (views[i].type == 30 && views[i].item_id == 385 && views[i].item_meta >= 2) {
            views[i].type = ER_TYPE_DRAGON_FIREBALL;
            views[i].item_id = 385;
        }
    }
}

/* Prepare views for gm_small_fireball_fire_emit: large fireballs temporarily look
 * like BILLBOARD+385 so the fire layers run (vanilla EntityLargeFireball is fiery).
 * Call after billboard emit; restore with gm_entity_restore_large_fireball_types. */
void gm_entity_prep_large_fireball_fire(GmEntityView *views, int nviews) {
    if (!views) return;
    for (int i = 0; i < nviews; ++i) {
        if (views[i].type == ER_TYPE_DRAGON_FIREBALL && views[i].item_id == 385 &&
            views[i].item_meta >= 2)
            views[i].type = 30; /* BILLBOARD for fire overlay only */
    }
}

void gm_entity_restore_large_fireball_types(GmEntityView *views, int nviews) {
    if (!views) return;
    for (int i = 0; i < nviews; ++i) {
        if (views[i].type == 30 && views[i].item_id == 385 && views[i].item_meta >= 2)
            views[i].type = ER_TYPE_DRAGON_FIREBALL;
    }
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
        case 38 /* GM_VIEW_FALLING_BLOCK */: return 0.49f; /* mid-block */
        case 44 /* GM_VIEW_TNT_PRIMED */: return 0.49f; /* mid-block */
        case ER_TYPE_ZOMBIE:   return 1.74f;          /* EntityZombie override */
        case ER_TYPE_PIGMAN:   return 1.74f;
        case ER_TYPE_SKELETON: return 1.99f * 0.85f;
        case ER_TYPE_WITHER_SKELETON: return 2.1f;
        case ER_TYPE_CREEPER:  return 1.7f * 0.85f;
        case ER_TYPE_SPIDER:   return 0.65f;          /* EntitySpider override */
        case ER_TYPE_CAVE_SPIDER: return 0.45f;
        case ER_TYPE_ENDERMAN: return 2.55f;          /* EntityEnderman override */
        case ER_TYPE_BLAZE:    return 1.8f * 0.85f;
        case ER_TYPE_SHEEP:    return 1.3f * 0.85f;
        case ER_TYPE_PIG:      return 0.9f * 0.85f;
        case ER_TYPE_COW:      return 1.4f * 0.85f;
        case ER_TYPE_CHICKEN:  return 0.7f * 0.85f;
        case ER_TYPE_SQUID:    return 0.4f;           /* height * 0.5 */
        case ER_TYPE_WITCH:    return 1.95f * 0.85f;
        case ER_TYPE_VILLAGER: return 1.62f;
        case ER_TYPE_BAT:      return 0.9f * 0.85f;
        case ER_TYPE_LLAMA:    return 1.87f * 0.85f;
        case ER_TYPE_GHAST:    return 4.0f * 0.85f;
        case ER_TYPE_MAGMA:    return 0.51f * 0.85f;  /* size-1 base; caller * size */
        case ER_TYPE_SLIME:    return 0.51f * 0.85f;  /* size-1 base; caller * size */
        case ER_TYPE_SILVERFISH: return 0.3f * 0.85f;
        case ER_TYPE_BOAT:     return 0.5625f * 0.85f;
        case ER_TYPE_DRAGON:   return 8.0f * 0.85f;   /* setSize(16, 8) */
        case ER_TYPE_CRYSTAL:  return 2.0f * 0.85f;   /* setSize(2, 2) */
        case ER_TYPE_ARMOR_STAND: return 1.975f * 0.85f;
        default:               return 0.5f;
    }
}

/* EntityBlaze.getBrightnessForRender (EntityBlaze.java:99-102) returns
 * 15728880 == (240 << 16) | 240, i.e. the model always samples the lightmap
 * at sky 15 / block 15 no matter how dark the world cell is. Callers that
 * sample world light for an entity apply this exemption first. */
int gm_entity_fullbright(int type) {
    return type == ER_TYPE_BLAZE;
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

static int er_armor_sprite(int item_id, int layer) {
    if (item_id >= 306 && item_id <= 309)
        return layer == 2 ? CR_MOB_IRON_LAYER_2 : CR_MOB_IRON_LAYER_1;
    if (item_id >= 310 && item_id <= 313)
        return layer == 2 ? CR_MOB_DIAMOND_LAYER_2 : CR_MOB_DIAMOND_LAYER_1;
    return -1;
}

static int er_armor_stand_box_count(const GmEntityView *v) {
    int n = 0;
    if (er_armor_sprite(v->armor_chest, 1) >= 0) n += 3;
    if (er_armor_sprite(v->armor_legs, 2) >= 0) n += 3;
    if (er_armor_sprite(v->armor_feet, 1) >= 0) n += 2;
    if (er_armor_sprite(v->armor_head, 1) >= 0) n += 2;
    return n;
}

static int er_emit_armor_parts(const ErPart *parts, int nparts, int sprite,
                               float cs, float sn, float sc,
                               float fx, float fy, float fz, CrRgba tint,
                               float lv, float blk, float roll_c, float roll_s,
                               CrVertex *out) {
    int written = 0;
    for (int i = 0; i < nparts; ++i) {
        ErPart part = parts[i];
        part.sprite = sprite;
        written += emit_box(&part, cs, sn, sc, fx, fy, fz, tint, lv, blk,
                            roll_c, roll_s, out + written);
    }
    return written;
}

static int er_emit_armor_stand_layers(const GmEntityView *v,
                                      float cs, float sn, float sc,
                                      float fx, float fy, float fz, CrRgba tint,
                                      float lv, float blk,
                                      float roll_c, float roll_s,
                                      CrVertex *out) {
    int written = 0;
    int sprite = er_armor_sprite(v->armor_chest, 1);
    if (sprite >= 0)
        written += er_emit_armor_parts(
            ARMOR_CHEST_PARTS, 3, sprite, cs, sn, sc, fx, fy, fz, tint,
            lv, blk, roll_c, roll_s, out + written);
    sprite = er_armor_sprite(v->armor_legs, 2);
    if (sprite >= 0)
        written += er_emit_armor_parts(
            ARMOR_LEGS_PARTS, 3, sprite, cs, sn, sc, fx, fy, fz, tint,
            lv, blk, roll_c, roll_s, out + written);
    sprite = er_armor_sprite(v->armor_feet, 1);
    if (sprite >= 0)
        written += er_emit_armor_parts(
            ARMOR_FEET_PARTS, 2, sprite, cs, sn, sc, fx, fy, fz, tint,
            lv, blk, roll_c, roll_s, out + written);
    sprite = er_armor_sprite(v->armor_head, 1);
    if (sprite >= 0)
        written += er_emit_armor_parts(
            ARMOR_HEAD_PARTS, 2, sprite, cs, sn, sc, fx, fy, fz, tint,
            lv, blk, roll_c, roll_s, out + written);
    return written;
}

int gm_entities_emit(const GmEntityView *ents, int n, CrVertex *out, int max) {
    int written = 0;
    for (int e = 0; e < n; ++e) {
        int entity_start = written;
        if (ents[e].flags & 4) continue; /* EntityLivingBase.isInvisible */
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
        if (ents[e].type == ER_TYPE_BOAT) {
            if (written + 9 * ER_VERTS_PER_BOX > max) break;
            written += emit_boat(&ents[e], out + written);
            continue;
        }
        const ErModel *m = er_model_for_type(ents[e].type);
        if (!m) continue;                            /* NONE / PLAYER */
        int need = m->nparts * ER_VERTS_PER_BOX;
        if (ents[e].type == ER_TYPE_ARMOR_STAND)
            need += er_armor_stand_box_count(&ents[e]) * ER_VERTS_PER_BOX;
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
         * approximates as mix(tex, red, 0.3) -> tint (255, 178, 178).
         * flag1 there is `hurtTime > 0 || deathTime > 0`, so the whole death
         * animation stays tinted after hurtTime counts back down to 0
         * (measured: dropping the deathTime half costs 44693 unexplained px
         * on scenario_portal_fortress_blaze). */
        CrRgba tint = { 255, 255, 255, 255 };
        if (ents[e].hurt_time > 0 || ents[e].death_time > 0) {
            tint.r = 255; tint.g = 178; tint.b = 178;
        } else if (er_is_minecart(ents[e].type)) {
            /* RenderMinecart is a non-living renderer under the fixed-function
             * entity lights. Its broad vertical panels receive both material
             * and directional attenuation; the living-model face heuristic
             * otherwise leaves the cart texture substantially over-bright. */
            tint.r = tint.g = tint.b = 184;
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
        } else if (gm_entity_fullbright(ents[e].type)) {
            /* legacy callers sample no world light; a blaze still needs the
             * block half of its getBrightnessForRender max (sky 15/block 15).
             * lm_lit 1/2 callers already pinned both levels. */
            blk = 15.0f;
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
        {
            int skin = ents[e].skin;
            if (skin <= 0 && ents[e].type == ER_TYPE_PIGMAN)
                skin = CR_MOB_PIGMAN + 1;
            if (skin <= 0 && ents[e].type == ER_TYPE_CAVE_SPIDER)
                skin = CR_MOB_CAVE_SPIDER + 1;
            if (ents[e].type == ER_TYPE_VILLAGER) {
                static const int profession_skins[5] = {
                    CR_MOB_VILLAGER_FARMER,
                    CR_MOB_VILLAGER_LIBRARIAN,
                    CR_MOB_VILLAGER_PRIEST,
                    CR_MOB_VILLAGER_SMITH,
                    CR_MOB_VILLAGER_BUTCHER
                };
                int profession = ents[e].item_id;
                skin = (profession >= 0 && profession < 5
                        ? profession_skins[profession]
                        : CR_MOB_VILLAGER) + 1;
            }
            if (skin > 0)
                for (int p = 0; p < np; ++p)
                    local[p].sprite = skin - 1;
        }

        float lsa = ents[e].limb_swing_amount;
        float ls  = ents[e].limb_swing;
        int t = ents[e].type;
        if ((ents[e].tape_pose || t == ER_TYPE_SHEEP || t == ER_TYPE_PIG ||
             t == ER_TYPE_COW || t == ER_TYPE_CHICKEN) && np > 0) {
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
                     t == ER_TYPE_MAGMA || t == ER_TYPE_ARMOR_STAND ||
                     er_is_minecart(t)) { /* none */ }
            else if (t == ER_TYPE_VILLAGER) { h0 = 0; h1 = 1; }
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
            } else if (t == ER_TYPE_ZOMBIE || t == ER_TYPE_PIGMAN ||
                       t == ER_TYPE_SKELETON ||
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
            } else if (t == ER_TYPE_VILLAGER && np >= 9) {
                local[7].ax = cosf(ls * 0.6662f) * 1.4f * lsa * 0.5f;
                local[8].ax = cosf(ls * 0.6662f + ER_PI) * 1.4f * lsa * 0.5f;
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
        } else if (t == ER_TYPE_MAGMA && np >= 8) {
            /* ModelMagmaCube.setLivingAnimations: segment.rotationPointY =
             * -(4-i)*squish*1.7 (squish from EntitySlime.squishFactor view field). */
            float sq = ents[e].squish;
            if (sq < 0.0f) sq = 0.0f;
            for (int p = 0; p < 8; ++p)
                local[p].ry = -(float)(4 - p) * sq * 1.7f;
        }
        if (t == ER_TYPE_ARMOR_STAND && np >= 10) {
            /* ModelArmorStand.setRotationAngles keeps the square plate fixed
             * in world orientation by applying -entity.rotationYaw inside the
             * renderer's outer (180-yaw) transform. */
            local[9].ay = -ents[e].yaw * ER_DEG2RAD;
        }

        /* ModelSheep1/2.setLivingAnimations + setRotationAngles: head pitch and
         * rotationPointY come from EntitySheep.sheepTimer (AI eat-grass). Tape
         * has no sheepTimer; idle (limbSwingAmount near 0) is the graze-stand
         * pose used most of the time the player stares at a flock. Mid-graze
         * constants from EntitySheep.getHeadRotation* at sheepTimer=20:
         *   head.ry = 6 + 1.0*9
         *   head.ax = PI/5 + (PI*7/100)*sin((20-4)/32 * 28.7)
         * Applied to skin head (0) and fur head (6). */
        if (t == ER_TYPE_SHEEP
                && (ents[e].tape_pose || (ents[e].flags & 16))
                && np >= 7) {
            local[0].ry = 6.0f + ents[e].graze_y * 9.0f;
            local[0].ax = ents[e].graze_x;
            local[6].ry = local[0].ry;
            local[6].ax = local[0].ax;
        }

        float death_roll = er_death_roll(&ents[e]);

        /* vanilla applyRotations: rotate(180 - yaw) about Y. */
        float rad = (180.0f - ents[e].yaw) * ER_DEG2RAD;
        float cs = cosf(rad), sn = sinf(rad);
        float sc = m->scale > 0.0f ? m->scale : 1.0f;
        /* RenderCaveSpider.preRenderCallback scales the shared ModelSpider
         * uniformly to 70%; the skin override distinguishes it from a normal
         * spider without adding a duplicate render type/model table entry. */
        if (t == ER_TYPE_CAVE_SPIDER ||
            (t == ER_TYPE_SPIDER && ents[e].skin == CR_MOB_CAVE_SPIDER + 1))
            sc *= 0.7f;
        /* RenderSlime / RenderMagmaCube.preRenderCallback:
         *   RenderSlime also GlStateManager.scale(0.999) before size/squish.
         *   f2 = squish / (size*0.5+1); f3 = 1/(f2+1);
         *   scale(f3*size, (1/f3)*size, f3*size)
         * item_meta = getSlimeSize; squish = EntitySlime.squishFactor. */
        float scx = sc, scy = sc;
        if (t == ER_TYPE_SLIME || t == ER_TYPE_MAGMA) {
            int sz = ents[e].item_meta;
            if (sz <= 0) sz = (t == ER_TYPE_MAGMA) ? 2 : 1;
            if (sz > 8) sz = 8;
            float size = (float)sz;
            float sq = ents[e].squish;
            float f2 = sq / (size * 0.5f + 1.0f);
            float f3 = 1.0f / (f2 + 1.0f);
            float base = sc;
            if (t == ER_TYPE_SLIME) base *= 0.999f; /* RenderSlime f=0.999 */
            scx = f3 * size * base;
            scy = (1.0f / f3) * size * base;
        } else if (t == ER_TYPE_CREEPER && ents[e].creeper_fuse > 0) {
            /* RenderCreeper.preRenderCallback(getCreeperFlashIntensity(1)).
             * Java clamps only before the fourth power, not before the pulse. */
            float f = (float)ents[e].creeper_fuse / 28.0f;
            float pulse = 1.0f + sinf(f * 100.0f) * f * 0.01f;
            float fc = f;
            if (fc < 0.0f) fc = 0.0f;
            if (fc > 1.0f) fc = 1.0f;
            fc *= fc;
            fc *= fc;
            scx = sc * (1.0f + fc * 0.4f) * pulse;
            scy = sc * (1.0f + fc * 0.1f) / pulse;
        }
        float roll_c = cosf(death_roll), roll_s = sinf(death_roll);
        /* Sheep: emit fur body/legs first, then skin, then fur head last so the
         * face snout (skin head, longer -Z) wins near-coplanar depth against the
         * expanded fur head (ModelSheep1 delta 0.6). Vanilla order is base then
         * LayerSheepWool; skin-last for the head only preserves face texels. */
        if (t == ER_TYPE_SHEEP && ents[e].sheared && np >= 12) {
            for (int p = 0; p < 6; ++p)
                written += emit_box(&local[p], cs, sn, scx, fx, fy, fz, tint,
                                    lv, blk, roll_c, roll_s, out + written);
        } else if (t == ER_TYPE_SHEEP && np >= 12) {
            static const int order[12] = { 7, 8, 9, 10, 11, 1, 2, 3, 4, 5, 6, 0 };
            CrRgba wool = sheep_wool_tint(ents[e].fleece_color,
                                          ents[e].hurt_time > 0);
            if (ents[e].lm_lit == 2) {
                wool.r = (u8)(wool.r * ents[e].lm_mul_r + 0.5f);
                wool.g = (u8)(wool.g * ents[e].lm_mul_g + 0.5f);
                wool.b = (u8)(wool.b * ents[e].lm_mul_b + 0.5f);
            }
            for (int i = 0; i < 12; ++i) {
                int p = order[i];
                written += emit_box(&local[p], cs, sn, scx, fx, fy, fz,
                                    p >= 6 ? wool : tint,
                                    lv, blk, roll_c, roll_s, out + written);
            }
        } else if (t == ER_TYPE_SLIME || t == ER_TYPE_MAGMA ||
                   t == ER_TYPE_CREEPER) {
            /* Non-uniform preRenderCallback axes via per-axis scale in emit.
             * ModelMagmaCube.render draws core first, then segments — so the
             * outer shell depth-occludes the bright core eyes (Java golden). */
            /* ModelSlime.render: GlStateManager.translate(0, 0.001, 0). */
            float fy_slime = (t == ER_TYPE_SLIME) ? (fy + 0.001f) : fy;
            int n_emit = np;
            int order_buf[ER_MAX_PARTS];
            const int *order = NULL;
            if (t == ER_TYPE_MAGMA && np >= 9) {
                order_buf[0] = 8; /* core */
                for (int i = 0; i < 8; ++i) order_buf[i + 1] = i;
                order = order_buf;
                n_emit = 9;
            }
            for (int i = 0; i < n_emit; ++i) {
                int p = order ? order[i] : i;
                ErPart bp = local[p];
                int s0 = written;
                written += emit_box(&bp, cs, sn, scx, fx, fy_slime, fz, tint,
                                    lv, blk, roll_c, roll_s, out + written);
                if (scy != scx && written > s0) {
                    float ymul = scy / scx;
                    for (int vi = s0; vi < written; ++vi) {
                        out[vi].pos.y = fy_slime + (out[vi].pos.y - fy_slime) * ymul;
                    }
                }
            }
        } else if (t == ER_TYPE_ARMOR_STAND) {
            for (int p = 0; p < np; ++p) {
                if ((p == 2 || p == 3) && !(ents[e].stand_flags & 1))
                    continue;
                if (p == 9 && (ents[e].stand_flags & 2))
                    continue;
                written += emit_box(&local[p], cs, sn, sc, fx, fy, fz, tint,
                                    lv, blk, roll_c, roll_s, out + written);
            }
            written += er_emit_armor_stand_layers(
                &ents[e], cs, sn, sc, fx, fy, fz, tint, lv, blk,
                roll_c, roll_s, out + written);
        } else {
            for (int p = 0; p < np; ++p)
                written += emit_box(&local[p], cs, sn, sc, fx, fy, fz, tint,
                                    lv, blk, roll_c, roll_s, out + written);
        }
        if (t == ER_TYPE_CREEPER && ents[e].creeper_fuse > 0 &&
            ents[e].hurt_time <= 0 && ents[e].death_time <= 0) {
            /* RenderCreeper.getColorMultiplier returns 0x(30|i)FFFFFF on
             * alternating phases. RenderLivingBase then interpolates the
             * lit texture toward white by 1-alpha. Pack that mix into blk;
             * the entity shade context decodes it without changing CrVertex. */
            float f = (float)ents[e].creeper_fuse / 28.0f;
            if (((int)(f * 10.0f) & 1) != 0) {
                int i = (int)(f * 0.2f * 255.0f);
                if (i < 0) i = 0;
                if (i > 255) i = 255;
                int alpha = i | 0x30;
                float mix = 1.0f - (float)alpha / 255.0f;
                for (int vi = entity_start; vi < written; ++vi)
                    out[vi].blk = -(out[vi].blk + 1.0f + mix / 32.0f);
            }
        }
    }
    return written;
}

static int er_crystal_beams_emit_exact(const GmEntityView *ents, int n,
                                       CrVertex *out, int max) {
    if (!ents || !out || n <= 0 || max <= 0) return 0;
    int written = 0;
    for (int e = 0; e < n; ++e) {
        int crystal=ents[e].type==ER_TYPE_CRYSTAL&&ents[e].has_beam;
        int dragon=ents[e].type==ER_TYPE_DRAGON&&ents[e].has_heal_beam;
        if(!crystal&&!dragon)continue;
        if (written + ER_CRYSTAL_BEAM_VERTS > max) break;
        int nw=crystal?emit_crystal_beam(&ents[e],out+written)
                      :emit_dragon_heal_beam(&ents[e],out+written);
        apply_beam_light(&ents[e],out+written,nw);
        written+=nw;
    }
    return written;
}

/* java.util.Random (48-bit LCG). new Random(seed) xors multiplier.
 * Mask MUST be (1<<48)-1 = 0xFFFFFFFFFFFF (12 hex F); a 13-F mask lets the
 * seed grow past 48 bits and nextFloat() returns values >> 1, which stretched
 * LayerEnderDragonDeath rays ~10x (solid white beams vs soft pink). */
#define ER_JRAND_MASK 0xFFFFFFFFFFFFull /* 48-bit, 12 F */
static float er_jrand_float(unsigned long long *s) {
    *s = (*s * 0x5DEECE66Dull + 0xBull) & ER_JRAND_MASK;
    return (float)((int)(*s >> 24)) / 1.6777216e7f; /* 2^24 */
}

/* Row-major 3x3: apply GlStateManager.rotate(deg, ax,ay,az) as M = M * R.
 * Rodrigues is right-handed OpenGL: positive angle is RH thumb-along-axis.
 * Verified: Rx(+90) maps +Y -> +Z, +Z -> -Y. */
static void er_mat3_mul_axis(float m[9], float deg, float ax, float ay, float az) {
    float rad = deg * ER_DEG2RAD;
    float c = cosf(rad), s = sinf(rad);
    float len = sqrtf(ax * ax + ay * ay + az * az);
    if (len < 1e-8f) return;
    ax /= len; ay /= len; az /= len;
    float t = 1.0f - c;
    /* Standard RH Rodrigues (sin skew signs match glRotate). */
    float r[9] = {
        t*ax*ax + c,    t*ax*ay - s*az, t*ax*az + s*ay,
        t*ax*ay + s*az, t*ay*ay + c,    t*ay*az - s*ax,
        t*ax*az - s*ay, t*ay*az + s*ax, t*az*az + c
    };
    float o[9];
    for (int row = 0; row < 3; ++row)
        for (int col = 0; col < 3; ++col)
            o[row * 3 + col] =
                m[row * 3 + 0] * r[0 * 3 + col] +
                m[row * 3 + 1] * r[1 * 3 + col] +
                m[row * 3 + 2] * r[2 * 3 + col];
    memcpy(m, o, sizeof o);
}

static void er_mat3_xform(const float m[9], float x, float y, float z,
                          float *ox, float *oy, float *oz) {
    *ox = m[0] * x + m[1] * y + m[2] * z;
    *oy = m[3] * x + m[4] * y + m[5] * z;
    *oz = m[6] * x + m[7] * y + m[8] * z;
}

/* Unit-test helpers for RH Rodrigues (death-ray orientation). */
int gm_entity_rot_rx90_maps_y_to_z(void) {
    float m[9] = { 1,0,0, 0,1,0, 0,0,1 };
    er_mat3_mul_axis(m, 90.0f, 1, 0, 0);
    float ox, oy, oz;
    er_mat3_xform(m, 0, 1, 0, &ox, &oy, &oz);
    return fabsf(ox) < 1e-5f && fabsf(oy) < 1e-5f && fabsf(oz - 1.0f) < 1e-5f;
}
int gm_entity_rot_axes_are_unit(void) {
    /* Identity * Rx(90) * Ry(90) * Rz(90) columns stay unit length. */
    float m[9] = { 1,0,0, 0,1,0, 0,0,1 };
    er_mat3_mul_axis(m, 90.0f, 1, 0, 0);
    er_mat3_mul_axis(m, 90.0f, 0, 1, 0);
    er_mat3_mul_axis(m, 90.0f, 0, 0, 1);
    for (int col = 0; col < 3; ++col) {
        float lx = m[0 * 3 + col], ly = m[1 * 3 + col], lz = m[2 * 3 + col];
        float n = sqrtf(lx * lx + ly * ly + lz * lz);
        if (fabsf(n - 1.0f) > 1e-5f) return 0;
    }
    return 1;
}

/* LayerEnderDragonDeath: Random(432) axis rotations ACCUMULATE across the ray
 * loop (vanilla never reloads the matrix). Fans sit after applyRotations +
 * prepareScale, then Layer translate(0,-1,-2). f = (deathTicks+partial)/200
 * with partial=1.0 (frame_capture / qrl frame pin). POSITION_COLOR, SRC_ALPHA/ONE. */
int gm_dragon_death_rays_emit(const GmEntityView *ents, int n, CrVertex *out,
                              int max) {
    if (!ents || !out || max < 9) return 0;
    int written = 0;
    for (int e = 0; e < n; ++e) {
        if (ents[e].type != 9 /* dragon */) continue;
        int dt = ents[e].death_ticks;
        if (dt <= 0) continue;
        float f = ((float)dt + 1.0f) / 200.0f;
        float f1 = 0.0f;
        if (f > 0.8f) f1 = (f - 0.8f) / 0.2f;
        /* Vanilla's loop is `for (i = 0; (float)i < (f+f*f)/2*60; ++i)`: it
         * runs whenever the bound is > 0, so deathTicks 1 already draws ONE
         * ray. Skipping until bound >= 1 delayed the onset by ~5 death ticks
         * (~3 tape frames). No upper clamp either - the CLIENT keeps ticking
         * deathTicks past 200 (setDead is server-side only), and 60 was a
         * bound on f<=1. The output buffer is the only real limit. */
        float bound = (f + f * f) / 2.0f * 60.0f;
        if (bound <= 0.0f) continue;

        /* Body orientation from the same ring as emit_dragon. Do not pose_tick
         * here (frame_capture already advanced via gm_entities_emit); seed only
         * if this entity never entered the ring. */
        ErDragonRing *rb = &er_dragon_ring;
        if (!rb->inited || rb->ent_id != ents[e].ent_id)
            gm_dragon_pose_tick(ents[e].ent_id, ents[e].yaw, ents[e].y,
                                ents[e].health);
        int dead = ents[e].health <= 0.0f;
        float mo5[2], mo7[2], mo10[2];
        er_dragon_mo(rb, 5, dead, mo5);
        er_dragon_mo(rb, 7, dead, mo7);
        er_dragon_mo(rb, 10, dead, mo10);

        ErAff base;
        er_aff_identity(&base);
        base.t[0] = ents[e].x;
        base.t[1] = ents[e].y;
        base.t[2] = ents[e].z;
        er_aff_rot_y(&base, -mo7[0]);
        er_aff_rot_x(&base, (mo5[1] - mo10[1]) * 10.0f);
        er_aff_translate(&base, 0.0f, 0.0f, 1.0f);
        er_aff_scale3(&base, -1.0f, -1.0f, 1.0f);
        er_aff_translate(&base, 0.0f, -1.501f, 0.0f);
        er_aff_translate(&base, 0.0f, -1.0f, -2.0f);

        /* The layer's disableTexture2D() turns off the ACTIVE unit (0) only;
         * the lightmap on OpenGlHelper.lightmapTexUnit stays bound and keeps
         * MODULATing, so the fans carry the DRAGON's brightness, not white.
         * In the End (lm_lit==2, no bound LUT) that folds a ~0.2 multiplier
         * into the color - unmodulated fans were several times too bright. */
        float lv = 1.0f, blk = 15.0f;
        float lmr = 1.0f, lmg = 1.0f, lmb = 1.0f;
        if (ents[e].lm_lit == 1) {
            lv = ents[e].lm_light; blk = ents[e].lm_blk;
        } else if (ents[e].lm_lit == 2) {
            lv = 1.0f; blk = 0.0f;
            lmr = ents[e].lm_mul_r; lmg = ents[e].lm_mul_g;
            lmb = ents[e].lm_mul_b;
        }

        unsigned long long js = (432ull ^ 0x5DEECE66Dull) & ER_JRAND_MASK;
        float m[9] = { 1,0,0, 0,1,0, 0,0,1 };
        for (int i = 0; (float)i < bound; ++i) {
            if (written + 9 > max) return written;
            er_mat3_mul_axis(m, er_jrand_float(&js) * 360.0f, 1, 0, 0);
            er_mat3_mul_axis(m, er_jrand_float(&js) * 360.0f, 0, 1, 0);
            er_mat3_mul_axis(m, er_jrand_float(&js) * 360.0f, 0, 0, 1);
            er_mat3_mul_axis(m, er_jrand_float(&js) * 360.0f, 1, 0, 0);
            er_mat3_mul_axis(m, er_jrand_float(&js) * 360.0f, 0, 1, 0);
            er_mat3_mul_axis(m, er_jrand_float(&js) * 360.0f + f * 90.0f, 0, 0, 1);
            float f2 = er_jrand_float(&js) * 20.0f + 5.0f + f1 * 10.0f;
            float f3 = er_jrand_float(&js) * 2.0f + 1.0f + f1 * 2.0f;
            float loc[5][3] = {
                { 0, 0, 0 },
                { -0.866f * f3, f2, -0.5f * f3 },
                {  0.866f * f3, f2, -0.5f * f3 },
                {  0.0f,        f2,  1.0f * f3 },
                { -0.866f * f3, f2, -0.5f * f3 },
            };
            /* vertexbuffer.color(255,255,255,(int)(255.0F*(1.0F-f1))) stores
             * the alpha through a Java `(byte)` narrowing cast (VertexBuffer
             * .color, UBYTE branch), NOT a clamp. Past deathTicks 200 f1 > 1,
             * the int goes negative and wraps to ~250: that wrap IS vanilla's
             * final starburst. Clamping to 0 made magma's rays vanish exactly
             * when the oracle's peak. Mask, do not clamp. */
            int alpha0 = (int)(255.0f * (1.0f - f1)) & 255;
#define ER_RAY_LM(r, g, b, a) { (u8)((float)(r) * lmr + 0.5f),               \
                                (u8)((float)(g) * lmg + 0.5f),               \
                                (u8)((float)(b) * lmb + 0.5f), (u8)(a) }
            CrRgba cols[5] = {
                ER_RAY_LM(255, 255, 255, alpha0),
                ER_RAY_LM(255, 0, 255, 0),
                ER_RAY_LM(255, 0, 255, 0),
                ER_RAY_LM(255, 0, 255, 0),
                ER_RAY_LM(255, 0, 255, 0),
            };
#undef ER_RAY_LM
            static const int tri[9] = { 0,1,2, 0,2,3, 0,3,4 };
            for (int k = 0; k < 9; ++k) {
                int pi = tri[k];
                float lx, ly, lz;
                er_mat3_xform(m, loc[pi][0], loc[pi][1], loc[pi][2],
                              &lx, &ly, &lz);
                CrVertex vtx;
                vtx.pos.x = base.m[0][0]*lx + base.m[0][1]*ly
                          + base.m[0][2]*lz + base.t[0];
                vtx.pos.y = base.m[1][0]*lx + base.m[1][1]*ly
                          + base.m[1][2]*lz + base.t[1];
                vtx.pos.z = base.m[2][0]*lx + base.m[2][1]*ly
                          + base.m[2][2]*lz + base.t[2];
                vtx.uv.x = 0.0f; vtx.uv.y = 0.0f;
                vtx.light = lv; vtx.blk = blk;
                vtx.tint = cols[pi];
                vtx.ao = 1.0f;
                out[written++] = vtx;
            }
        }
    }
    return written;
}

/* ---- RenderDragon.renderCrystalBeams (end-crystal healing beam) ----------
 *
 * A closed 8-sided cone: an inner ring of radius 0.75*0.2 at the beam origin
 * and an outer ring of radius 0.75 at distance f4, drawn as GL_TRIANGLE_STRIP
 * (18 verts, 16 triangles) with POSITION_TEX_COLOR, smooth shading, vertex
 * colour black at the origin ring and white at the far ring. Vanilla draws it
 * with GL_CULL_FACE OFF, so every triangle is emitted in BOTH windings (the
 * texture is a sparse alpha-0/255 rune sheet, so the far wall shows through
 * the near one). RenderHelper.disableStandardItemLighting only kills GL_LIGHT*;
 * the lightmap texture unit keeps MODULATing at the coords RenderManager set
 * from the *rendered entity's* getBrightnessForRender - i.e. the dragon's for
 * the healing beam, the crystal's for RenderEnderCrystal's own beam target -
 * so the caller's per-entity lm_* fields are folded in exactly like the death
 * rays.
 *
 * TEXTURE V WRAP. endercrystal_beam.png is sampled with GL_REPEAT and the V
 * range runs from f5 = -(ticksExisted+partial)*0.01 to f5 + f4/32, i.e. off
 * both ends of [0,1] and scrolling one 1/100th of the sheet per tick. The mob
 * atlas has no wrap mode, so each of the 16 strip triangles is CLIPPED at every
 * integer V and each piece re-based into [0,1). Clipping a triangle and
 * interpolating the cut vertices along the cut edges is exact: attributes are
 * affine inside a triangle, so the sub-triangles reproduce the same
 * perspective-correct interpolation the unclipped triangle would have. */
typedef struct { float p[3]; float u, v, c; } ErBeamV;

static void er_beamv_lerp(const ErBeamV *a, const ErBeamV *b, float t,
                          ErBeamV *o) {
    for (int i = 0; i < 3; ++i) o->p[i] = a->p[i] + (b->p[i] - a->p[i]) * t;
    o->u = a->u + (b->u - a->u) * t;
    o->v = a->v + (b->v - a->v) * t;
    o->c = a->c + (b->c - a->c) * t;
}

/* Sutherland-Hodgman clip of a convex polygon against v >= bound (keep_ge) or
 * v <= bound. Returns the new vertex count (<= nin + 1). */
static int er_beam_clip_v(const ErBeamV *in, int nin, float bound, int keep_ge,
                          ErBeamV *out) {
    int nout = 0;
    for (int i = 0; i < nin; ++i) {
        const ErBeamV *a = &in[i];
        const ErBeamV *b = &in[(i + 1) % nin];
        float da = keep_ge ? (a->v - bound) : (bound - a->v);
        float db = keep_ge ? (b->v - bound) : (bound - b->v);
        int ina = da >= 0.0f, inb = db >= 0.0f;
        if (ina) out[nout++] = *a;
        if (ina != inb) {
            float t = da / (da - db);
            er_beamv_lerp(a, b, t, &out[nout++]);
        }
    }
    return nout;
}

typedef struct {
    float lv, blk;            /* lightmap levels (CrVertex.light / .blk) */
    float lmr, lmg, lmb;      /* folded lightmap multiplier (lm_lit == 2) */
} ErBeamShade;

static void er_beam_vertex(const ErBeamV *v, int band, const ErBeamShade *sh,
                           CrVertex *out) {
    const CrMobSprite *spr = &CR_MOB_SPRITES[CR_MOB_ENDERCRYSTAL_BEAM];
    const float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
    out->pos.x = v->p[0]; out->pos.y = v->p[1]; out->pos.z = v->p[2];
    out->uv.x = ((float)spr->x0 + v->u * (float)spr->w) / aw;
    out->uv.y = ((float)spr->y0 + (v->v - (float)band) * (float)spr->h) / ah;
    out->light = sh->lv;
    out->blk = sh->blk;
    float g = v->c * 255.0f;
    out->tint.r = (u8)(g * sh->lmr + 0.5f);
    out->tint.g = (u8)(g * sh->lmg + 0.5f);
    out->tint.b = (u8)(g * sh->lmb + 0.5f);
    out->tint.a = 255;
    out->ao = 1.0f;
}

/* One strip triangle -> clipped, re-based, both windings. */
static int er_beam_tri(const ErBeamV *a, const ErBeamV *b, const ErBeamV *c,
                       const ErBeamShade *sh, CrVertex *out, int max) {
    float vmin = fminf(a->v, fminf(b->v, c->v));
    float vmax = fmaxf(a->v, fmaxf(b->v, c->v));
    int k0 = (int)floorf(vmin), k1 = (int)floorf(vmax);
    if (vmax == (float)k1 && k1 > k0) --k1;   /* upper edge sits on a boundary */
    if (k1 - k0 > 256) return 0;              /* degenerate; never in practice */
    int written = 0;
    for (int k = k0; k <= k1; ++k) {
        ErBeamV poly[8], tmp[8];
        poly[0] = *a; poly[1] = *b; poly[2] = *c;
        int np = er_beam_clip_v(poly, 3, (float)k, 1, tmp);
        if (np < 3) continue;
        np = er_beam_clip_v(tmp, np, (float)(k + 1), 0, poly);
        if (np < 3) continue;
        for (int i = 1; i + 1 < np; ++i) {
            if (written + 6 > max) return written;
            CrVertex t[3];
            er_beam_vertex(&poly[0],     k, sh, &t[0]);
            er_beam_vertex(&poly[i],     k, sh, &t[1]);
            er_beam_vertex(&poly[i + 1], k, sh, &t[2]);
            out[written++] = t[0]; out[written++] = t[1]; out[written++] = t[2];
            /* GlStateManager.disableCull: same triangle, reversed winding. */
            out[written++] = t[2]; out[written++] = t[1]; out[written++] = t[0];
        }
    }
    return written;
}

/* Literal transcription of RenderDragon.renderCrystalBeams with
 * partialTicks = 1.0 (magma renders on the tick boundary). ox/oy/oz is the
 * caller's already-world-space translate origin (vanilla passes the camera
 * relative x,y,z and adds 2.0 to y here). */
static int er_crystal_beams(double ox, double oy, double oz,
                            double fromx, double fromy, double fromz,
                            int ticks,
                            double tox, double toy, double toz,
                            const ErBeamShade *sh, CrVertex *out, int max) {
    float f  = (float)(tox - fromx);
    float f1 = (float)(toy - 1.0 - fromy);
    float f2 = (float)(toz - fromz);
    float f3 = sqrtf(f * f + f2 * f2);
    float f4 = sqrtf(f * f + f1 * f1 + f2 * f2);

    ErAff a;
    er_aff_identity(&a);
    a.t[0] = (float)ox; a.t[1] = (float)oy + 2.0f; a.t[2] = (float)oz;
    er_aff_rot_y(&a, (float)(-atan2((double)f2, (double)f)) * (180.0f / ER_PI)
                     - 90.0f);
    er_aff_rot_x(&a, (float)(-atan2((double)f3, (double)f1)) * (180.0f / ER_PI)
                     - 90.0f);

    float f5 = 0.0f - ((float)ticks + 1.0f) * 0.01f;
    float f6 = f4 / 32.0f - ((float)ticks + 1.0f) * 0.01f;

    ErBeamV strip[18];
    for (int j = 0; j <= 8; ++j) {
        float ang = (float)(j % 8) * (ER_PI * 2.0f) / 8.0f;
        float f7 = sinf(ang) * 0.75f;
        float f8 = cosf(ang) * 0.75f;
        float f9 = (float)(j % 8) / 8.0f;
        const float lp[2][3] = {
            { f7 * 0.2f, f8 * 0.2f, 0.0f },
            { f7,        f8,        f4   },
        };
        for (int k = 0; k < 2; ++k) {
            ErBeamV *v = &strip[j * 2 + k];
            for (int r = 0; r < 3; ++r)
                v->p[r] = a.m[r][0] * lp[k][0] + a.m[r][1] * lp[k][1]
                        + a.m[r][2] * lp[k][2] + a.t[r];
            v->u = f9;
            v->v = k ? f6 : f5;
            v->c = k ? 1.0f : 0.0f;
        }
    }

    /* GL_TRIANGLE_STRIP over the 18 vertices: triangle i is (i, i+1, i+2) with
     * odd i reversed, so both triangles of a quad share the inner->outer
     * diagonal exactly like the fixed-function pipeline. */
    int written = 0;
    for (int i = 0; i + 2 < 18; ++i) {
        const ErBeamV *v0 = &strip[i], *v1 = &strip[i + 1], *v2 = &strip[i + 2];
        if (i & 1) { const ErBeamV *t = v0; v0 = v1; v1 = (ErBeamV *)t; }
        written += er_beam_tri(v0, v1, v2, sh, out + written, max - written);
        if (written + 6 > max) break;
    }
    return written;
}

static void er_beam_shade_from(const GmEntityView *e, ErBeamShade *sh) {
    sh->lv = 15.0f; sh->blk = 0.0f;
    sh->lmr = sh->lmg = sh->lmb = 1.0f;
    if (e->lm_lit == 1) {
        sh->lv = e->lm_light; sh->blk = e->lm_blk;
    } else if (e->lm_lit == 2) {
        sh->lv = 1.0f; sh->blk = 0.0f;
        sh->lmr = e->lm_mul_r; sh->lmg = e->lm_mul_g; sh->lmb = e->lm_mul_b;
    }
}

/* EntityDragon.healingEnderCrystal (EntityDragon.updateDragonEnderCrystal).
 *
 * Vanilla runs this on BOTH sides (EntityLivingBase.onUpdate calls
 * onLivingUpdate unconditionally), and the client's own `rand` decides WHEN it
 * re-picks: `if (rand.nextInt(10) == 0)` then nearest crystal whose bounding
 * box intersects the dragon's expanded by 32. The tape cannot carry that RNG,
 * so magma re-picks every tick - the pick itself (nearest in range) is
 * deterministic and only the ~10-tick latency after the nearest crystal
 * CHANGES is approximated.
 *
 * The freeze is exact and matters more: `getHealth() <= 0` takes the
 * explosion-particle branch of onLivingUpdate and never reaches
 * updateDragonEnderCrystal, so the beam target latches at the moment the dragon
 * dies and stays there for the whole 200-tick death animation. */
static struct { int inited, dragon_id, crystal_id; } er_heal_latch;

static const GmEntityView *er_heal_crystal(const GmEntityView *ents, int n,
                                           const GmEntityView *d) {
    if (!er_heal_latch.inited || er_heal_latch.dragon_id != d->ent_id) {
        er_heal_latch.inited = 1;
        er_heal_latch.dragon_id = d->ent_id;
        er_heal_latch.crystal_id = -1;
    }
    if (d->health > 0.0f) {
        /* getEntityBoundingBox().expandXyz(32): setSize(16, 8) -> half width 8,
         * height 8 above posY. AABB intersect against the crystal's own
         * setSize(2, 2) box. */
        float x0 = d->x - 8.0f - 32.0f, x1 = d->x + 8.0f + 32.0f;
        float y0 = d->y - 32.0f,        y1 = d->y + 8.0f + 32.0f;
        float z0 = d->z - 8.0f - 32.0f, z1 = d->z + 8.0f + 32.0f;
        int best = -1;
        double bestd = 0.0;
        for (int i = 0; i < n; ++i) {
            const GmEntityView *c = &ents[i];
            if (c->type != ER_TYPE_CRYSTAL) continue;
            if (c->x + 1.0f <= x0 || c->x - 1.0f >= x1) continue;
            if (c->y + 2.0f <= y0 || c->y >= y1) continue;
            if (c->z + 1.0f <= z0 || c->z - 1.0f >= z1) continue;
            double dx = (double)c->x - d->x, dy = (double)c->y - d->y,
                   dz = (double)c->z - d->z;
            double dd = dx * dx + dy * dy + dz * dz;
            if (best < 0 || dd < bestd) { bestd = dd; best = c->ent_id; }
        }
        er_heal_latch.crystal_id = best;
    }
    if (er_heal_latch.crystal_id < 0) return NULL;
    for (int i = 0; i < n; ++i)
        if (ents[i].type == ER_TYPE_CRYSTAL
            && ents[i].ent_id == er_heal_latch.crystal_id)
            return &ents[i];
    return NULL;   /* crystal destroyed: isDead clears healingEnderCrystal */
}

int gm_crystal_beams_emit(const GmEntityView *ents, int n, CrVertex *out,
                          int max) {
    if (!ents || !out || max < 6) return 0;
    int written = er_crystal_beams_emit_exact(ents, n, out, max);
    for (int e = 0; e < n; ++e) {
        if (ents[e].type != ER_TYPE_DRAGON) continue;
        const GmEntityView *d = &ents[e];
        if (d->has_heal_beam) continue;
        const GmEntityView *c = er_heal_crystal(ents, n, d);
        if (!c) continue;
        /* RenderDragon.doRender: f = sin((crystal.ticksExisted + partial) *
         * 0.2)/2 + 0.5; f = (f*f + f) * 0.2 raises the beam's crystal end. */
        float f = sinf(((float)c->ticks_existed + 1.0f) * 0.2f) / 2.0f + 0.5f;
        f = (f * f + f) * 0.2f;
        ErBeamShade sh;
        er_beam_shade_from(d, &sh);
        written += er_crystal_beams(d->x, d->y, d->z,
                                    d->x, d->y, d->z, d->ticks_existed,
                                    c->x, (double)f + (double)c->y, c->z,
                                    &sh, out + written, max - written);
        if (written + 6 > max) return written;
    }
    return written;
}

/* particles.png cell UV: setParticleTextureIndex uses 16x16 grid over the sheet. */
static void er_particle_uv(int index, float *u0, float *v0, float *u1, float *v1) {
    const CrMobSprite *sp = &CR_MOB_SPRITES[CR_MOB_PARTICLES];
    const float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
    int ix = index % 16, iy = index / 16;
    float cell = (float)sp->w / 16.0f; /* 8 px cells on 128 sheet */
    float x0 = (float)sp->x0 + (float)ix * cell;
    float y0 = (float)sp->y0 + (float)iy * cell;
    *u0 = x0 / aw; *v0 = y0 / ah;
    *u1 = (x0 + cell) / aw; *v1 = (y0 + cell) / ah;
}

/* ParticleExplosionLarge: textures/entity/explosion.png, 4x4 frames of 32 px
 * on the 128 sheet. Frame i uses u = (i%4)/4 .. +0.24975, v = (i/4)/4. */
static void er_explosion_png_uv(int frame, float *u0, float *v0,
                                float *u1, float *v1) {
    const CrMobSprite *sp = &CR_MOB_SPRITES[CR_MOB_EXPLOSION];
    const float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
    if (frame < 0) frame = 0;
    if (frame > 15) frame = 15;
    float fu = (float)(frame % 4) / 4.0f;
    float fv = (float)(frame / 4) / 4.0f;
    float su = (float)sp->w / aw, sv = (float)sp->h / ah;
    float bx = (float)sp->x0 / aw, by = (float)sp->y0 / ah;
    /* Java: f = (i%4)/4, f1 = f+0.24975; f2 = (i/4)/4, f3 = f2+0.24975.
     * Atlas-local: multiply 0.24975 by sheet span (native 128 -> full sprite). */
    *u0 = bx + fu * su;
    *u1 = bx + (fu + 0.24975f) * su;
    *v0 = by + fv * sv;
    *v1 = by + (fv + 0.24975f) * sv;
}

/* Camera-facing billboard. half_extent is the half-width of each axis of the
 * quad (Java f4 = 0.1*particleScale for dig; f4 = 2.0*size for explosion). */
static int er_emit_billboard(float px0, float py0, float pz0, float half_extent,
                             float u0, float v0, float u1, float v1,
                             CrRgba tint, float cy, float sy, float cp, float sp,
                             CrVertex *out, int max) {
    if (max < 6) return 0;
    /* Corner layout matches Particle.renderParticle / ParticleExplosionLarge
     * (rotationX/Z billboard axes); scale = full edge = 2*half. */
    static const float CORN[4][2] = {
        { -1.0f, -1.0f }, { 1.0f, -1.0f }, { 1.0f, 1.0f }, { -1.0f, 1.0f }
    };
    static const int TRI[6] = { 0, 1, 2, 0, 2, 3 };
    float uus[4] = { u0, u1, u1, u0 };
    float vvs[4] = { v1, v1, v0, v0 };
    CrVertex quad[4];
    for (int c = 0; c < 4; ++c) {
        float px = CORN[c][0] * half_extent, py = CORN[c][1] * half_extent, pz = 0.0f;
        float ty = py * cp - pz * sp, tz = py * sp + pz * cp;
        py = ty; pz = tz;
        float tx = px * cy + pz * sy;
        tz = -px * sy + pz * cy;
        CrVertex vtx;
        vtx.pos.x = px0 + tx;
        vtx.pos.y = py0 + py;
        vtx.pos.z = pz0 + tz;
        vtx.uv.x = uus[c]; vtx.uv.y = vvs[c];
        vtx.light = 1.0f; vtx.blk = 15.0f;
        vtx.tint = tint;
        vtx.ao = 1.0f;
        quad[c] = vtx;
    }
    for (int k = 0; k < 6; ++k) out[k] = quad[TRI[k]];
    return 6;
}

/* Deterministic LCG float in [0,1) from seed (Java-ish stream for recon). */
static float er_seed_f(unsigned *s) {
    *s = (*s) * 1664525u + 1013904223u;
    return (float)(*s & 0xffff) / 65535.0f;
}
static int er_seed_i(unsigned *s, int n) {
    *s = (*s) * 1664525u + 1013904223u;
    return (int)((*s >> 16) % (unsigned)(n > 0 ? n : 1));
}

/* ParticleExplosionLarge semantics (MC 1.11.2):
 *   texture = entity/explosion.png (not particles.png)
 *   lifeTime = 6 + nextInt(4)
 *   color = nextFloat()*0.6+0.4 gray
 *   size = 1.0 - progress*0.5  (progress is the spawn xSpeed arg)
 *   frame = (life+pt)*15/lifeTime  clamped 0..15  (4x4 grid)
 *   half-extent f4 = 2.0 * size
 *   onUpdate: life++ only; NO motion integration (vel forced 0 at construct)
 * Recon: spawn pos fixed; age = life; no fake positional integration. */
static int er_emit_explosion_large(float px, float py, float pz,
                                   int age, int life_time, float progress,
                                   float gray,
                                   float cy, float sy, float cp, float sp,
                                   CrVertex *out, int max) {
    if (life_time < 1) life_time = 1;
    if (age < 0 || age >= life_time || max < 6) return 0;
    float size = 1.0f - progress * 0.5f;
    if (size < 0.05f) size = 0.05f;
    float half = 2.0f * size;
    int frame = (int)(((float)age /* +0 partial */) * 15.0f / (float)life_time);
    if (frame > 15) frame = 15;
    float u0, v0, u1, v1;
    er_explosion_png_uv(frame, &u0, &v0, &u1, &v1);
    u8 g = (u8)(gray * 255.0f + 0.5f);
    CrRgba tint = { g, g, g, 255 };
    return er_emit_billboard(px, py, pz, half, u0, v0, u1, v1, tint,
                             cy, sy, cp, sp, out, max);
}

/* EntityDragon death burst, reconstructed from deathTicks alone.
 *
 * Java timeline (server spawn -> client ParticleManager):
 *   - onUpdate, health<=0: ONE EXPLOSION_LARGE per tick at pos + (rand-0.5)*
 *     {8,4,8}, y+2, speed 0 -> ParticleExplosionLarge size = 1.0.
 *   - onDeathUpdate, deathTicks in [180,200]: ONE EXPLOSION_HUGE per tick at
 *     the same randomized offset. ParticleExplosionHuge draws nothing itself:
 *     on EACH of its 8 onUpdate ticks it spawns SIX EXPLOSION_LARGE at
 *     (rand-rand)*4 around its origin with speed = timeSinceStart/8, which
 *     ParticleExplosionLarge turns into size = 1 - progress*0.5.
 *   - every child lives 6 + nextInt(4) ticks and walks explosion.png frames.
 *
 * So one HUGE contributes 8 batches x 6 puffs, not one batch: at deathTicks
 * 190 roughly 8 live HUGEs x 6 x ~7.5 surviving ticks = ~360 live puffs. The
 * previous recon emitted only the newest batch per HUGE (~48) and drew a cloud
 * about 7x too thin. Emitting every batch is what makes the dense core.
 *
 * The particles also OUTLIVE the entity: the dragon is removed at deathTicks
 * 200, the last HUGE keeps spawning children through deathTicks 208 and those
 * live to ~217, which is the bright cloud the oracle shows for ~15 ticks after
 * the dragon is gone. gm_particles_dragon_latch() keeps that window alive. */
static int er_dragon_death_particles(float ex, float ey, float ez, int ent_id,
                                     int dt, float cy, float sy, float cp,
                                     float sp, CrVertex *out, int max) {
    if (max < 6 || dt <= 0) return 0;
    int written = 0;
    const int max_life = 9;      /* lifeTime = 6 + nextInt(4) */
    const int huge_time = 8;     /* ParticleExplosionHuge.maximumTime */
    /* (1) one LARGE per dead tick, while the entity still exists (<=200). */
    int t0 = dt - max_life + 1;
    if (t0 < 1) t0 = 1;
    int t1 = dt > 200 ? 200 : dt;
    for (int st = t0; st <= t1; ++st) {
        int age = dt - st;
        unsigned seed = (unsigned)ent_id * 2654435761u
                      + (unsigned)st * 2246822519u + 0x4c415247u;
        int life = 6 + er_seed_i(&seed, 4);
        if (age >= life) continue;
        float r0 = er_seed_f(&seed), r1 = er_seed_f(&seed),
              r2 = er_seed_f(&seed);
        float ox = (r0 - 0.5f) * 8.0f;
        float oy = 2.0f + (r1 - 0.5f) * 4.0f;
        float oz = (r2 - 0.5f) * 8.0f;
        float gray = er_seed_f(&seed) * 0.6f + 0.4f;
        if (written + 6 > max) return written;
        written += er_emit_explosion_large(
            ex + ox, ey + oy, ez + oz, age, life, 0.0f, gray,
            cy, sy, cp, sp, out + written, max - written);
    }
    if (dt < 181) return written;
    /* (2) every HUGE still inside the child window, every batch it spawned. */
    int hs0 = dt - (huge_time - 1 + max_life);
    if (hs0 < 180) hs0 = 180;
    int hs1 = dt > 200 ? 200 : dt;
    for (int hs = hs0; hs <= hs1; ++hs) {
        unsigned hseed = (unsigned)ent_id * 1597334677u
                       + (unsigned)hs * 3812015801u + 0x48554745u;
        /* HUGE origin: the dragon's own onDeathUpdate offset. */
        float hx = (er_seed_f(&hseed) - 0.5f) * 8.0f;
        float hy = 2.0f + (er_seed_f(&hseed) - 0.5f) * 4.0f;
        float hz = (er_seed_f(&hseed) - 0.5f) * 8.0f;
        for (int k = 0; k < huge_time; ++k) {
            /* Minecraft.runTick updates entities (line 1881) before the
             * ParticleManager (line 1934), so the HUGE spawned by the dragon
             * on tick hs already runs its first onUpdate that same tick: the
             * k-th batch lands on tick hs+k with timeSinceStart = k, and its
             * children keep size 1 - (k/8)*0.5 for their whole life. Children
             * queued during updateEffects only start ageing the next tick. */
            int u = hs + k;
            if (u > dt) break;
            int age = dt - u;
            if (age >= max_life) continue;
            float progress = (float)k / (float)huge_time;
            for (int c = 0; c < 6; ++c) {
                unsigned lseed = hseed ^ ((unsigned)u * 2654435761u);
                lseed += (unsigned)c * 747796405u + 0x9e3779b9u;
                float d0 = (er_seed_f(&lseed) - er_seed_f(&lseed)) * 4.0f;
                float d1 = (er_seed_f(&lseed) - er_seed_f(&lseed)) * 4.0f;
                float d2 = (er_seed_f(&lseed) - er_seed_f(&lseed)) * 4.0f;
                int life = 6 + er_seed_i(&lseed, 4);
                if (age >= life) continue;
                float gray = er_seed_f(&lseed) * 0.6f + 0.4f;
                if (written + 6 > max) return written;
                written += er_emit_explosion_large(
                    ex + hx + d0, ey + hy + d1, ez + hz + d2,
                    age, life, progress, gray,
                    cy, sy, cp, sp, out + written, max - written);
            }
        }
    }
    return written;
}

/* Last dying dragon seen by the renderer, so its burst survives the entity.
 * Vanilla removes EntityDragon at deathTicks 200 but the ParticleManager keeps
 * ticking the cloud; a purely entity-derived recon pops it off instead. */
static struct {
    int active, present, ent_id, dt;
    long long tick;
    float x, y, z;
} er_dragon_death;

/* Called once per simulated tick with that tick's entity list, BEFORE
 * gm_particles_emit. Arms on a dying dragon and keeps counting deathTicks
 * after the entity disappears until every child LARGE spawned by the last
 * HUGE (deathTicks 200 -> children through 208 -> life <=9) has expired.
 * Idempotent within a tick, so extra rendered frames cannot advance it. */
void gm_particles_dragon_latch(long long tick, const GmEntityView *ents, int n) {
    const int last_dt = 200 + 8 + 9;
    for (int i = 0; i < n; ++i) {
        if (ents[i].type != 9 /* dragon */) continue;
        if (ents[i].death_ticks <= 0 || ents[i].health > 0.0f) continue;
        er_dragon_death.active = 1;
        er_dragon_death.ent_id = ents[i].ent_id;
        er_dragon_death.dt = ents[i].death_ticks;
        er_dragon_death.tick = tick;
        er_dragon_death.x = ents[i].x;
        er_dragon_death.y = ents[i].y;
        er_dragon_death.z = ents[i].z;
        er_dragon_death.present = 1;
        return;
    }
    if (!er_dragon_death.active) return;
    er_dragon_death.present = 0;
    long long d = tick - er_dragon_death.tick;
    /* Same tick re-render, a rewind, or a gap long enough that the cloud is
     * certainly gone: no advance / disarm. */
    if (d == 0) return;
    if (d < 0 || d > 64) { er_dragon_death.active = 0; return; }
    er_dragon_death.dt += (int)d;
    er_dragon_death.tick = tick;
    /* The dragon keeps drifting up 0.1/tick through onDeathUpdate; it is
     * removed at 200, so the cloud origin simply stays where it was. */
    if (er_dragon_death.dt > last_dt) er_dragon_death.active = 0;
}

/* Deterministic particle billboards from entity state.
 * Portal: particles.png. EXPLOSION_LARGE/HUGE: explosion.png (FXLayer 3).
 * EntityDragon: LARGE every dead tick (onUpdate health<=0); HUGE in [180,200]
 * (onDeathUpdate) expands via ParticleExplosionHuge (6 LARGE/tick, maxTime=8). */
int gm_particles_emit_filtered(const GmEntityView *ents, int n, float view_yaw,
                               float view_pitch, int suppress_explosion,
                               CrVertex *out, int max) {
    if (!ents || !out || max < 6) return 0;
    float yr = (180.0f - view_yaw) * ER_DEG2RAD;
    float pr = -view_pitch * ER_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    int written = 0;
    for (int e = 0; e < n; ++e) {
        if (ents[e].type == GM_VIEW_EXPLOSION_LARGE) {
            if (suppress_explosion) continue;
            unsigned seed = (unsigned)ents[e].ent_id * 1664525u + 99u;
            int life = 6 + er_seed_i(&seed, 4);
            /* The recorder does not carry ParticleManager's global Random
             * cursor. Use a stable in-range tint for the single anchored puff;
             * later random frames remain in the particle divergence class. */
            float gray = 0.56f;
            written += er_emit_explosion_large(
                ents[e].x, ents[e].y, ents[e].z,
                ents[e].age, life, 0.0f, gray, cy, sy, cp, sp,
                out + written, max - written);
            continue;
        }
        if (ents[e].type == ER_TYPE_ENDERMAN) {
            /* EntityEnderman: 2 PORTAL/tick; maxAge ~40-50. Reconstruct cloud. */
            int count = 90;
            unsigned seed = (unsigned)ents[e].ent_id * 1664525u
                          + (unsigned)ents[e].age * 1013904223u + 7u;
            for (int i = 0; i < count; ++i) {
                if (written + 6 > max) return written;
                float r0 = er_seed_f(&seed), r1 = er_seed_f(&seed),
                      r2 = er_seed_f(&seed);
                float width = 0.6f, height = 2.9f;
                float ox = (r0 - 0.5f) * width;
                float oy = r1 * height - 0.25f;
                float oz = (r2 - 0.5f) * width;
                int max_age = 40 + (int)(r0 * 10.0f); /* ParticlePortal 40-50 */
                int age = (ents[e].age + i) % max_age;
                float agef = (float)age / (float)max_age;
                float sf = 1.0f - agef; sf = 1.0f - sf * sf;
                float pscale = (0.1f + r0 * 0.2f) * sf;
                if (pscale < 0.02f) pscale = 0.02f;
                float col = 0.4f + r1 * 0.6f;
                CrRgba tint = {
                    (u8)(col * 0.9f * 255.0f),
                    (u8)(col * 0.3f * 255.0f),
                    (u8)(col * 255.0f), 220
                };
                int tex = (int)(r2 * 8.0f) % 8;
                float f2 = 1.0f - (-agef + agef * agef * 2.0f);
                ox *= f2; oz *= f2;
                oy = oy * f2 + (1.0f - agef) * 0.5f;
                float u0, v0, u1, v1;
                er_particle_uv(tex, &u0, &v0, &u1, &v1);
                /* Portal half-extent ≈ 0.5 * pscale (legacy full-edge scale). */
                written += er_emit_billboard(
                    ents[e].x + ox, ents[e].y + oy, ents[e].z + oz,
                    pscale * 0.5f, u0, v0, u1, v1, tint, cy, sy, cp, sp,
                    out + written, max - written);
            }
            continue;
        }

        /* EntityDragon spawns EXPLOSION_LARGE only while health<=0 (onUpdate).
         * Oracle deathTicks pins keep health full so rays/dissolve run without
         * a multi-tick ParticleManager recon that Java never built. */
        if (ents[e].type == 9 /* dragon */ && ents[e].death_ticks > 0
            && ents[e].health <= 0.0f) {
            if (suppress_explosion) continue;
            written += er_dragon_death_particles(
                ents[e].x, ents[e].y, ents[e].z, ents[e].ent_id,
                ents[e].death_ticks, cy, sy, cp, sp,
                out + written, max - written);
            continue;
        }

        /* EntityEnderCrystal is not an EntityLivingBase: the recorder writes
         * hp = -1 for every non-living entity and the live arena view writes a
         * positive placeholder, so `health <= 0` fired the destruction burst on
         * EVERY intact crystal of EVERY tick (a 28 px grey ball parked on each
         * pillar top, mostly hidden behind the pillar and swallowed by the
         * gate's `particles` class). A destroyed crystal is setDead() and drops
         * out of the entity list entirely, so only an explicit health == 0
         * (never the -1 "no health" sentinel) may burst. */
        if (ents[e].type == ER_TYPE_CRYSTAL && ents[e].health == 0.0f) {
            if (suppress_explosion) continue;
            /* Burst recon: several LARGE at crystal origin, progress 0. */
            unsigned seed = (unsigned)ents[e].ent_id * 1664525u + 99u;
            for (int i = 0; i < 8; ++i) {
                int age = i % 4;
                int life = 6 + er_seed_i(&seed, 4);
                float gray = er_seed_f(&seed) * 0.6f + 0.4f;
                float ox = (er_seed_f(&seed) - 0.5f) * 1.0f;
                float oy = (er_seed_f(&seed) - 0.5f) * 1.0f;
                float oz = (er_seed_f(&seed) - 0.5f) * 1.0f;
                if (age >= life) continue;
                if (written + 6 > max) return written;
                written += er_emit_explosion_large(
                    ents[e].x + ox, ents[e].y + oy, ents[e].z + oz,
                    age, life, 0.0f, gray, cy, sy, cp, sp,
                    out + written, max - written);
            }
            continue;
        }
    }
    /* The dragon is gone but its cloud is not: keep drawing the latched burst
     * (see gm_particles_dragon_latch) until the last child LARGE expires. */
    if (!suppress_explosion && er_dragon_death.active && !er_dragon_death.present)
        written += er_dragon_death_particles(
            er_dragon_death.x, er_dragon_death.y, er_dragon_death.z,
            er_dragon_death.ent_id, er_dragon_death.dt, cy, sy, cp, sp,
            out + written, max - written);
    return written;
}

int gm_particles_emit(const GmEntityView *ents, int n, float view_yaw,
                      float view_pitch, CrVertex *out, int max) {
    return gm_particles_emit_filtered(ents, n, view_yaw, view_pitch, 0,
                                      out, max);
}

/* Dig hit dust while progressive break: stage 1..10 is dig progress * 10.
 *
 * Java (ParticleManager.addBlockHitEffects): one ParticleDigging per client
 * tick on the hit face, multiplyVelocity(0.2), multipleParticleScaleBy(0.6),
 * texture = model particle icon, gray 0.6; ParticleDigging ctor scale/=2 so
 * half-extent f4=0.1*scale lands in [0.03,0.06]. renderParticle then multiplies
 * that gray by the lightmap (VertexBuffer.color * .lightmap) from
 * getBrightnessForRender at the particle pos — without the lightmap fold the
 * dust reads as near-full texture brightness on unlit End stone.
 *
 * Input limits of the interactive dig signal:
 *   - face is available via dig_state_ex when progressive dig is live
 *   - no live particle age list / rand stream; we reconstruct a static spray
 *     of `stage` quads (progress proxy), not one-per-tick over lifetime
 *   - no gravity / collision motion integration
 * Caller draws with the terrain atlas. */
int gm_block_break_particles_emit(int wx, int wy, int wz, int block_id,
                                  int stage, int face, int particle_count,
                                  float view_yaw, float view_pitch,
                                  float lm_r, float lm_g, float lm_b,
                                  CrVertex *out, int max) {
    if (!out || max < 6 || stage <= 0) return 0;
    float yr = (180.0f - view_yaw) * ER_DEG2RAD;
    float pr = -view_pitch * ER_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    float bu0, bv0, bu1, bv1;
    bm_sprite_uv(bm_particle_sprite(block_id), &bu0, &bv0, &bu1, &bv1);
    float du = (bu1 - bu0), dv = (bv1 - bv0);
    /* entity_pin dig_hit may freeze N particles independent of crack stage. */
    int count = particle_count > 0 ? particle_count : stage;
    if (count < 1) count = 1;
    if (count > 16) count = 16;
    /* Clamp lightmap so a bad caller cannot blow the u8 pack. */
    if (lm_r < 0.0f) lm_r = 0.0f;
    if (lm_r > 1.0f) lm_r = 1.0f;
    if (lm_g < 0.0f) lm_g = 0.0f;
    if (lm_g > 1.0f) lm_g = 1.0f;
    if (lm_b < 0.0f) lm_b = 0.0f;
    if (lm_b > 1.0f) lm_b = 1.0f;
    /* ParticleDigging base gray 0.6 * updateLightmap RGB (GL_MODULATE). */
    CrRgba base_tint = {
        (u8)(0.6f * 255.0f * lm_r + 0.5f),
        (u8)(0.6f * 255.0f * lm_g + 0.5f),
        (u8)(0.6f * 255.0f * lm_b + 0.5f),
        255
    };
    unsigned seed = (unsigned)(wx * 73856093 ^ wy * 19349663 ^ wz * 83492791)
                  ^ (unsigned)stage * 2246822519u
                  ^ (unsigned)(face + 3) * 2654435761u;
    int written = 0;
    /* Full-cube AABB 0..1 (hit-effect uses block collision AABB; we lack TE). */
    float minx = 0.0f, miny = 0.0f, minz = 0.0f;
    float maxx = 1.0f, maxy = 1.0f, maxz = 1.0f;
    for (int i = 0; i < count; ++i) {
        if (written + 6 > max) return written;
        float r0 = er_seed_f(&seed), r1 = er_seed_f(&seed), r2 = er_seed_f(&seed);
        float jx = er_seed_f(&seed) * 3.0f; /* particleTextureJitter 0..3 */
        float jy = er_seed_f(&seed) * 3.0f;
        /* Particle ctor: (rand*0.5+0.5)*2 → [1,2]; ParticleDigging: /=2 → [0.5,1]. */
        float sc0 = (er_seed_f(&seed) * 0.5f + 0.5f) * 2.0f;
        sc0 /= 2.0f;
        /* addBlockHitEffects position in block-local then face snap. */
        float px = r0 * (maxx - minx - 0.2f) + 0.1f + minx;
        float py = r1 * (maxy - miny - 0.2f) + 0.1f + miny;
        float pz = r2 * (maxz - minz - 0.2f) + 0.1f + minz;
        if (face == 0 /* DOWN */)  py = miny - 0.1f;
        if (face == 1 /* UP */)    py = maxy + 0.1f;
        if (face == 2 /* NORTH */) pz = minz - 0.1f;
        if (face == 3 /* SOUTH */) pz = maxz + 0.1f;
        if (face == 4 /* WEST */)  px = minx - 0.1f;
        if (face == 5 /* EAST */)  px = maxx + 0.1f;
        /* multipleParticleScaleBy(0.6) → scale [0.3,0.6]; f4 = 0.1*scale → [0.03,0.06]. */
        float pscale = sc0 * 0.6f;
        float half = 0.1f * pscale;
        /* UV crop: jitter/4 of the particle icon (ParticleDigging.render). */
        float u0 = bu0 + (jx / 4.0f) * du;
        float u1 = bu0 + ((jx + 1.0f) / 4.0f) * du;
        float v0 = bv0 + (jy / 4.0f) * dv;
        float v1 = bv0 + ((jy + 1.0f) / 4.0f) * dv;
        /* Spawn at face; no velocity residual (would need live particle list). */
        written += er_emit_billboard(
            (float)wx + px, (float)wy + py, (float)wz + pz, half,
            u0, v0, u1, v1, base_tint, cy, sy, cp, sp,
            out + written, max - written);
    }
    return written;
}

/* LayerSlimeGel: ModelSlime(0) outer 8x8x8 gel shell after the base model.
 * Java GlStateManager.color(1,1,1,1) + texture alpha; depthMask stays true.
 * Caller draws with blend=4 (src-over + depth write). */
int gm_slime_gel_emit(const GmEntityView *ents, int n, CrVertex *out, int max) {
    if (!ents || !out || max < ER_VERTS_PER_BOX) return 0;
    int written = 0;
    for (int e = 0; e < n; ++e) {
        if (ents[e].type != ER_TYPE_SLIME) continue;
        if (ents[e].flags & 4) continue; /* LayerSlimeGel skips invisible */
        if (written + ER_VERTS_PER_BOX > max) break;
        int sz = ents[e].item_meta;
        if (sz <= 0) sz = 1;
        if (sz > 8) sz = 8;
        float size = (float)sz;
        float sq = ents[e].squish;
        float f2 = sq / (size * 0.5f + 1.0f);
        float f3 = 1.0f / (f2 + 1.0f);
        float scx = f3 * size, scy = (1.0f / f3) * size;
        float rad = (180.0f - ents[e].yaw) * ER_DEG2RAD;
        float cs = cosf(rad), sn = sinf(rad);
        ErPart gel = {
            CR_MOB_SLIME, 0, 0, -4, 16, -4, 8, 8, 8, 0, 0, 0, 0, 0, 0, 0, 0
        };
        /* color(1,1,1,1): translucency from slime.png texel alpha only. */
        CrRgba tint = { 255, 255, 255, 255 };
        float lv = 15.0f, blk = 0.0f;
        if (ents[e].lm_lit == 1) { lv = ents[e].lm_light; blk = ents[e].lm_blk; }
        else if (ents[e].lm_lit == 2) {
            lv = 1.0f;
            tint.r = (u8)(tint.r * ents[e].lm_mul_r + 0.5f);
            tint.g = (u8)(tint.g * ents[e].lm_mul_g + 0.5f);
            tint.b = (u8)(tint.b * ents[e].lm_mul_b + 0.5f);
        }
        /* ModelSlime.render translate(0, 0.001, 0); RenderSlime scale 0.999. */
        float base_y = ents[e].y + 0.001f;
        scx *= 0.999f;
        scy *= 0.999f;
        /* LayerSlimeGel draws inside the same applyRotations as the body, so
         * it keels with it (see the death_roll block in gm_entities_emit). */
        float roll = er_death_roll(&ents[e]);
        int s0 = written;
        written += emit_box(&gel, cs, sn, scx, ents[e].x, base_y, ents[e].z,
                            tint, lv, blk, cosf(roll), sinf(roll),
                            out + written);
        if (scy != scx) {
            float ymul = scy / scx;
            for (int vi = s0; vi < written; ++vi)
                out[vi].pos.y = base_y + (out[vi].pos.y - base_y) * ymul;
        }
    }
    return written;
}

/* Atlas UV offset from dragon skin -> dragon_exploding for dissolve shade. */
void gm_entity_dissolve_mask(float *u_off, float *v_off) {
    const CrMobSprite *d = &CR_MOB_SPRITES[CR_MOB_DRAGON];
    const CrMobSprite *x = &CR_MOB_SPRITES[CR_MOB_DRAGON_EXPLODING];
    if (u_off) *u_off = ((float)x->x0 - (float)d->x0) / (float)CR_MOB_ATLAS_W;
    if (v_off) *v_off = ((float)x->y0 - (float)d->y0) / (float)CR_MOB_ATLAS_H;
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

/* ------------------------------------------------------------------ */
/* TileEntityMobSpawnerRenderer: the spinning miniature inside the cage. */

/* Vanilla Entity width/height, for the renderer's
 * `f1 = max(width, height); if (f1 > 1) f /= f1` shrink-to-fit. Values are the
 * setSize() calls in each entity constructor (they match replay_tape.ENT_SIZE,
 * which is the same table on the harness side). */
void gm_entity_size(int type, float *w, float *h) {
    float ww = 0.6f, hh = 1.8f;                 /* EntityLiving default-ish */
    switch (type) {
        case ER_TYPE_BLAZE:    ww = 0.6f;  hh = 1.8f;  break;
        case ER_TYPE_ZOMBIE:
        case ER_TYPE_PIGMAN:   ww = 0.6f;  hh = 1.95f; break;
        case ER_TYPE_SKELETON: ww = 0.6f;  hh = 1.99f; break;
        case ER_TYPE_WITHER_SKELETON: ww = 0.7f; hh = 2.4f; break;
        case ER_TYPE_CREEPER:  ww = 0.6f;  hh = 1.7f;  break;
        case ER_TYPE_SPIDER:   ww = 1.4f;  hh = 0.9f;  break;
        case ER_TYPE_SILVERFISH: ww = 0.4f; hh = 0.3f; break;
        case ER_TYPE_ENDERMAN: ww = 0.6f;  hh = 2.9f;  break;
        case ER_TYPE_WITCH:    ww = 0.6f;  hh = 1.95f; break;
        case ER_TYPE_SHEEP:    ww = 0.9f;  hh = 1.3f;  break;
        case ER_TYPE_COW:      ww = 0.9f;  hh = 1.4f;  break;
        case ER_TYPE_PIG:      ww = 0.9f;  hh = 0.9f;  break;
        case ER_TYPE_CHICKEN:  ww = 0.4f;  hh = 0.7f;  break;
        case ER_TYPE_BAT:      ww = 0.5f;  hh = 0.9f;  break;
        case ER_TYPE_SLIME:
        case ER_TYPE_MAGMA:    ww = 0.51f; hh = 0.51f; break;
        default: break;
    }
    if (w) *w = ww;
    if (h) *h = hh;
}

float gm_spawner_mini_scale(int type) {
    float w, h, f = 0.53125f;
    gm_entity_size(type, &w, &h);
    float f1 = w > h ? w : h;
    if (f1 > 1.0f) f /= f1;
    return f;
}

/* TileEntityMobSpawnerRenderer.renderTileEntityAt + renderMob, transcribed:
 *   translate(x + 0.5, y, z + 0.5)          // note: y, NOT y + 0.5
 *   translate(0, 0.4, 0)
 *   rotate(lerp(prevMobRotation, mobRotation, partial) * 10, 0,1,0)
 *   translate(0, -0.2, 0)
 *   rotate(-30, 1,0,0)
 *   scale(f, f, f)                          // gm_spawner_mini_scale
 *   doRenderEntity(entity, 0,0,0, 0, partial)
 * The cached entity is setLocationAndAngles(...,0,0) first, so inside
 * RenderLivingBase the body yaw is 0 and applyRotations is a flat rotate(180)
 * about Y with no death keel; prepareScale is the usual scale(-1,-1,1),
 * preRenderCallback, translate(0,-1.501,0).
 *
 * sp[i].mob_rotation is MobSpawnerBaseLogic.mobRotation in its pre-x10 units
 * (0 while the spawner is not activated: updateSpawner only copies
 * mobRotation into prevMobRotation when no player is in range, so an inert
 * spawner's miniature is frozen, not spinning). */
int gm_spawner_miniatures_emit(const GmSpawnerView *sp, int n,
                               CrVertex *out, int max) {
    int written = 0;
    for (int i = 0; i < n; ++i) {
        const ErModel *m = er_model_for_type(sp[i].type);
        if (!m || m == &M_MARKER) continue;   /* no cached entity / no model */
        if (written + m->nparts * ER_VERTS_PER_BOX > max) break;

        ErAff a;
        er_aff_identity(&a);
        er_aff_translate(&a, (float)sp[i].wx + 0.5f, (float)sp[i].wy,
                         (float)sp[i].wz + 0.5f);
        er_aff_translate(&a, 0.0f, 0.4f, 0.0f);
        er_aff_rot_y(&a, sp[i].mob_rotation * 10.0f);
        er_aff_translate(&a, 0.0f, -0.2f, 0.0f);
        er_aff_rot_x(&a, -30.0f);
        er_aff_scale(&a, gm_spawner_mini_scale(sp[i].type));
        /* RenderLivingBase.applyRotations with renderYawOffset 0 */
        er_aff_rot_y(&a, 180.0f);
        /* prepareScale */
        er_aff_scale3(&a, -1.0f, -1.0f, 1.0f);
        if (m->scale > 0.0f) er_aff_scale(&a, m->scale);
        er_aff_translate(&a, 0.0f, -1.501f, 0.0f);

        /* Spawner miniatures are drawn at the block's own light; the entity is
         * never hurt or dying, so no tint and no keel. Blaze-like fullbright
         * types keep getBrightnessForRender's block-15. */
        CrRgba tint = { 255, 255, 255, 255 };
        float lv = 15.0f, blk = gm_entity_fullbright(sp[i].type) ? 15.0f : 0.0f;

        for (int p = 0; p < m->nparts; ++p) {
            const ErPart *q = &m->parts[p];
            ErAff pa = a;
            er_aff_translate(&pa, q->rx * 0.0625f, q->ry * 0.0625f,
                             q->rz * 0.0625f);
            /* ModelRenderer.render: Rz, then Ry, then Rx */
            if (q->az != 0.0f)
                er_aff_rot_z(&pa, q->az / ER_DEG2RAD);
            if (q->ay != 0.0f)
                er_aff_rot_y(&pa, q->ay / ER_DEG2RAD);
            if (q->ax != 0.0f)
                er_aff_rot_x(&pa, q->ax / ER_DEG2RAD);
            written += er_aff_box(&pa, q->sprite, 1, q->mirror, q->u, q->v,
                                  q->x, q->y, q->z, q->dx, q->dy, q->dz,
                                  tint, lv, blk, out + written);
        }
    }
    return written;
}

CrTexture gm_crystal_beam_texture(void) {
    CrTexture t;
    t.w = CR_ENDERCRYSTAL_BEAM_W;
    t.h = CR_ENDERCRYSTAL_BEAM_H;
    t.texels = (const CrRgba *)CR_ENDERCRYSTAL_BEAM_RGBA;
    t.tile = 0;
    t.mip_levels = 0;
    for (int i = 0; i < 15; ++i) { t.mip[i] = 0; t.mipw[i] = 0; t.miph[i] = 0; }
    return t;
}
