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
#define ER_TYPE_WOLF     16
#define ER_TYPE_OCELOT   17
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
#define ER_TYPE_ZOMBIE_VILLAGER 41
#define ER_TYPE_SHULKER 42
#define ER_TYPE_SHULKER_BULLET 43
#define ER_TYPE_VINDICATOR 51
#define ER_TYPE_EVOKER 52
#define ER_TYPE_VEX 53
#define ER_TYPE_EVOKER_FANGS 54
#define ER_TYPE_GUARDIAN 55
#define ER_TYPE_ELDER_GUARDIAN 56
#define ER_TYPE_IRON_GOLEM 57
#define ER_TYPE_RABBIT 61
#define ER_TYPE_POLAR_BEAR 62
#define ER_TYPE_ENDERMITE 63
#define ER_TYPE_SNOWMAN 64
#define ER_TYPE_GIANT 65
#define ER_TYPE_WITHER GM_VIEW_WITHER
#define ER_TYPE_WITHER_SKULL GM_VIEW_WITHER_SKULL
#define ER_TYPE_HORSE 68
#define ER_TYPE_DONKEY 69
#define ER_TYPE_MULE 70
#define ER_TYPE_SKELETON_HORSE 71
#define ER_TYPE_ZOMBIE_HORSE 72
#define ER_TYPE_LLAMA_SPIT 73
#define ER_TYPE_MINECART_CHEST GM_VIEW_MINECART_CHEST
#define ER_TYPE_MINECART_FURNACE GM_VIEW_MINECART_FURNACE
#define ER_TYPE_MINECART_HOPPER GM_VIEW_MINECART_HOPPER
#define ER_TYPE_MINECART_TNT GM_VIEW_MINECART_TNT
#define ER_TYPE_MINECART_SPAWNER GM_VIEW_MINECART_SPAWNER
#define ER_TYPE_MINECART_COMMAND GM_VIEW_MINECART_COMMAND

#define ER_VERTS_PER_BOX 36  /* 6 faces * 2 tris * 3 verts */
#define ER_PI 3.14159265358979323846f
#define ER_DEG2RAD 0.017453292519943295f
#define ER_RAD2DEG 57.29577951308232f
#define ER_MAX_PARTS 24

/* Compile-time-only oracle sweep controls. Production builds keep all zero;
 * the ui_entities Wither probe can compile a private entity_render.o with a
 * texel-scale offset without adding a branch or getenv to the render loop. */

/* MathHelper.sin/cos are 65,536-entry float LUTs, not libm trig.  Render-only
 * call sites do not own the runtime's 256 KiB table, so reconstruct the one
 * selected entry from the same double expression used by its static init. */
static float er_mathhelper_sin(float value) {
    int i = (int)(value * 10430.378f) & 65535;
    return (float)sin((double)i * 3.14159265358979323846 * 2.0 / 65536.0);
}

static float er_mathhelper_cos(float value) {
    int i = (int)(value * 10430.378f + 16384.0f) & 65535;
    return (float)sin((double)i * 3.14159265358979323846 * 2.0 / 65536.0);
}

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

/* ModelZombieVillager(false): tall villager head plus nose, six-wide body
 * and robe overlay, zombie arms, and ordinary biped legs. */
static const ErModel M_ZOMBIE_VILLAGER = { 8, {
    { CR_MOB_ZOMBIE_VILLAGER,  0,  0, -4,-10,-4, 8,10,8,
       0.0f, 0,0,  0,0,0, 0,0 },
    { CR_MOB_ZOMBIE_VILLAGER, 24,  0, -1, -3,-6, 2, 4,2,
       0.0f, 0,0,  0,0,0, 0,0 },
    { CR_MOB_ZOMBIE_VILLAGER, 16, 20, -4,  0,-3, 8,12,6,
       0.0f, 0,0,  0,0,0, 0,0 },
    { CR_MOB_ZOMBIE_VILLAGER,  0, 38, -4,  0,-3, 8,18,6,
       0.0f, 0,0,  0,0,0, 0.05f,0 },
    { CR_MOB_ZOMBIE_VILLAGER, 44, 38, -3, -2,-2, 4,12,4,
      -5.0f, 2,0,  ZOMBIE_ARM,0,0, 0,0 },
    { CR_MOB_ZOMBIE_VILLAGER, 44, 38, -1, -2,-2, 4,12,4,
       5.0f, 2,0,  ZOMBIE_ARM,0,0, 0,1 },
    { CR_MOB_ZOMBIE_VILLAGER,  0, 22, -2,  0,-2, 4,12,4,
      -2.0f,12,0,  0,0,0, 0,0 },
    { CR_MOB_ZOMBIE_VILLAGER,  0, 22, -2,  0,-2, 4,12,4,
       2.0f,12,0,  0,0,0, 0,1 },
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

/* ModelWither. The four boxes in upperBodyParts[1] share one pivot/rotation;
 * the last spine segment receives its animated pivot in er_wither_pose. */
static const ErModel M_WITHER = { 9, {
    { CR_MOB_WITHER,  0,  0, -4,-4,-4, 8,8,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_WITHER, 32,  0, -4,-4,-4, 6,6,6, -8,4,0, 0,0,0, 0,0 },
    { CR_MOB_WITHER, 32,  0, -4,-4,-4, 6,6,6, 10,4,0, 0,0,0, 0,0 },
    { CR_MOB_WITHER,  0, 16, -10,3.9f,-.5f, 20,3,3,
      0,0,0, 0,0,0, 0,0 },
    { CR_MOB_WITHER,  0, 22, 0,0,0, 3,10,3,
      -2,6.9f,-.5f, 0,0,0, 0,0 },
    { CR_MOB_WITHER, 24, 22, -4,1.5f,.5f, 11,2,2,
      -2,6.9f,-.5f, 0,0,0, 0,0 },
    { CR_MOB_WITHER, 24, 22, -4,4,.5f, 11,2,2,
      -2,6.9f,-.5f, 0,0,0, 0,0 },
    { CR_MOB_WITHER, 24, 22, -4,6.5f,.5f, 11,2,2,
      -2,6.9f,-.5f, 0,0,0, 0,0 },
    { CR_MOB_WITHER, 12, 22, 0,0,0, 3,6,3,
      -2,16.9f,-.5f, 0,0,0, 0,0 },
} };

static const ErPart M_WITHER_SKULL = {
    CR_MOB_WITHER, 0,35, -4,-8,-4, 8,8,8, 0,0,0, 0,0,0, 0,0
};

/* ModelHorse (128x128). Upper/lower mouth are children of HEAD; all other
 * parts are roots. The dynamic pose is applied in er_horse_pose before the
 * affine emitter reproduces ModelRenderer's parent transform. */
enum {
    HP_BODY, HP_TAIL_BASE, HP_TAIL_MIDDLE, HP_TAIL_TIP,
    HP_BACK_LEFT_LEG, HP_BACK_LEFT_SHIN, HP_BACK_LEFT_HOOF,
    HP_BACK_RIGHT_LEG, HP_BACK_RIGHT_SHIN, HP_BACK_RIGHT_HOOF,
    HP_FRONT_LEFT_LEG, HP_FRONT_LEFT_SHIN, HP_FRONT_LEFT_HOOF,
    HP_FRONT_RIGHT_LEG, HP_FRONT_RIGHT_SHIN, HP_FRONT_RIGHT_HOOF,
    HP_HEAD, HP_UPPER_MOUTH, HP_LOWER_MOUTH,
    HP_HORSE_LEFT_EAR, HP_HORSE_RIGHT_EAR,
    HP_MULE_LEFT_EAR, HP_MULE_RIGHT_EAR, HP_NECK,
    HP_MULE_LEFT_CHEST, HP_MULE_RIGHT_CHEST,
    HP_SADDLE_BOTTOM, HP_SADDLE_FRONT, HP_SADDLE_BACK,
    HP_LEFT_SADDLE_METAL, HP_LEFT_SADDLE_ROPE,
    HP_RIGHT_SADDLE_METAL, HP_RIGHT_SADDLE_ROPE,
    HP_LEFT_FACE_METAL, HP_RIGHT_FACE_METAL,
    HP_LEFT_REIN, HP_RIGHT_REIN, HP_MANE, HP_FACE_ROPES,
    HP_COUNT
};
static const ErPart M_HORSE_PARTS[HP_COUNT] = {
    {0,  0,34, -5,-8,-19, 10,10,24,  0,11, 9, 0,0,0, 0,0},
    {0, 44, 0, -1,-1,  0,  2, 2, 3,  0, 3,14,-1.134464f,0,0,0,0},
    {0, 38, 7,-1.5f,-2,3, 3,4,7, 0,3,14,-1.134464f,0,0,0,0},
    {0, 24, 3,-1.5f,-4.5f,9,3,4,7,0,3,14,-1.3962634f,0,0,0,0},
    {0, 78,29,-2.5f,-2,-2.5f,4,9,5, 4,9,11,0,0,0,0,0},
    {0, 78,43,-2,0,-1.5f,3,5,3, 4,16,11,0,0,0,0,0},
    {0, 78,51,-2.5f,5.1f,-2,4,3,4, 4,16,11,0,0,0,0,0},
    {0, 96,29,-1.5f,-2,-2.5f,4,9,5,-4,9,11,0,0,0,0,0},
    {0, 96,43,-1,0,-1.5f,3,5,3,-4,16,11,0,0,0,0,0},
    {0, 96,51,-1.5f,5.1f,-2,4,3,4,-4,16,11,0,0,0,0,0},
    {0, 44,29,-1.9f,-1,-2.1f,3,8,4, 4,9,-8,0,0,0,0,0},
    {0, 44,41,-1.9f,0,-1.6f,3,5,3, 4,16,-8,0,0,0,0,0},
    {0, 44,51,-2.4f,5.1f,-2.1f,4,3,4, 4,16,-8,0,0,0,0,0},
    {0, 60,29,-1.1f,-1,-2.1f,3,8,4,-4,9,-8,0,0,0,0,0},
    {0, 60,41,-1.1f,0,-1.6f,3,5,3,-4,16,-8,0,0,0,0,0},
    {0, 60,51,-1.6f,5.1f,-2.1f,4,3,4,-4,16,-8,0,0,0,0,0},
    {0,  0, 0,-2.5f,-10,-1.5f,5,5,7,0,4,-10,.5235988f,0,0,0,0},
    {0, 24,18,-2,-10,-7,4,3,6,0,3.95f,-10,.5235988f,0,0,0,0},
    {0, 24,27,-2,-7,-6.5f,4,2,5,0,4,-10,.5235988f,0,0,0,0},
    {0,  0, 0,.45f,-12,4,2,3,1,0,4,-10,.5235988f,0,0,0,0},
    {0,  0, 0,-2.45f,-12,4,2,3,1,0,4,-10,.5235988f,0,0,0,0},
    {0,  0,12,-2,-16,4,2,7,1,0,4,-10,.5235988f,0,.2617994f,0,0},
    {0,  0,12,0,-16,4,2,7,1,0,4,-10,.5235988f,0,-.2617994f,0,0},
    {0,  0,12,-2.05f,-9.8f,-2,4,14,8,0,4,-10,.5235988f,0,0,0,0},
    {0,  0,34,-3,0,0,8,8,3,-7.5f,3,10,0,ER_PI/2,0,0,0},
    {0,  0,47,-3,0,0,8,8,3, 4.5f,3,10,0,ER_PI/2,0,0,0},
    {0, 80, 0,-5,0,-3,10,1,8,0,2,2,0,0,0,0,0},
    {0,106, 9,-1.5f,-1,-3,3,1,2,0,2,2,0,0,0,0,0},
    {0, 80, 9,-4,-1,3,8,1,2,0,2,2,0,0,0,0,0},
    {0, 74, 0,-.5f,6,-1,1,2,2,5,3,2,0,0,0,0,0},
    {0, 70, 0,-.5f,0,-.5f,1,6,1,5,3,2,0,0,0,0,0},
    {0, 74, 4,-.5f,6,-1,1,2,2,-5,3,2,0,0,0,0,0},
    {0, 80, 0,-.5f,0,-.5f,1,6,1,-5,3,2,0,0,0,0,0},
    {0, 74,13,1.5f,-8,-4,1,2,2,0,4,-10,.5235988f,0,0,0,0},
    {0, 74,13,-2.5f,-8,-4,1,2,2,0,4,-10,.5235988f,0,0,0,0},
    {0, 44,10,2.6f,-6,-6,0,3,16,0,4,-10,.5235988f,0,0,0,0},
    {0, 44, 5,-2.6f,-6,-6,0,3,16,0,4,-10,.5235988f,0,0,0,0},
    {0, 58, 0,-1,-11.5f,5,2,16,4,0,4,-10,.5235988f,0,0,0,0},
    {0, 80,12,-2.5f,-10.1f,-7,5,5,12,0,4,-10,.5235988f,0,0,.2f,0},
};

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
    /* ModelCow mutates ModelQuadruped's pivots after construction: all four
     * legs move one pixel outward, and the rear pair one pixel forward. */
    { CR_MOB_COW,  0, 16, -2,  0,-2,  4,12, 4, -4,12, 7,  0,0,0, 0,0 },
    { CR_MOB_COW,  0, 16, -2,  0,-2,  4,12, 4,  4,12, 7,  0,0,0, 0,0 },
    { CR_MOB_COW,  0, 16, -2,  0,-2,  4,12, 4, -4,12,-6,  0,0,0, 0,0 },
    { CR_MOB_COW,  0, 16, -2,  0,-2,  4,12, 4,  4,12,-6,  0,0,0, 0,0 },
} };

/* ModelPolarBear (128x64). Head child boxes are flattened onto one pivot;
 * the two body boxes likewise share the torso transform. Standing animation
 * is applied from GmEntityView.anim_time below. */
static const ErModel M_POLAR_BEAR = { 10, {
    { CR_MOB_POLAR_BEAR,  0, 0, -3.5f,-3,-3, 7,7,7,
       0,10,-16, 0,0,0, 0,0 },
    { CR_MOB_POLAR_BEAR,  0,44, -2.5f,1,-6, 5,3,3,
       0,10,-16, 0,0,0, 0,0 },
    { CR_MOB_POLAR_BEAR, 26, 0, -4.5f,-4,-1, 2,2,1,
       0,10,-16, 0,0,0, 0,0 },
    { CR_MOB_POLAR_BEAR, 26, 0,  2.5f,-4,-1, 2,2,1,
       0,10,-16, 0,0,0, 0,1 },
    { CR_MOB_POLAR_BEAR,  0,19, -5,-13,-7, 14,14,11,
      -2,9,12, QUAD_BODY_ROT,0,0, 0,0 },
    { CR_MOB_POLAR_BEAR, 39, 0, -4,-25,-7, 12,12,10,
      -2,9,12, QUAD_BODY_ROT,0,0, 0,0 },
    { CR_MOB_POLAR_BEAR, 50,22, -2,0,-2, 4,10,8,
      -4.5f,14,6, 0,0,0, 0,0 },
    { CR_MOB_POLAR_BEAR, 50,22, -2,0,-2, 4,10,8,
       4.5f,14,6, 0,0,0, 0,0 },
    { CR_MOB_POLAR_BEAR, 50,40, -2,0,-2, 4,10,6,
      -3.5f,14,-8, 0,0,0, 0,0 },
    { CR_MOB_POLAR_BEAR, 50,40, -2,0,-2, 4,10,6,
       3.5f,14,-8, 0,0,0, 0,0 },
} };

/* ModelRabbit (64x32). Jump pose and split child transforms are applied in
 * the entity loop; RenderRabbit supplies the model's 0.6 outer scale. */
static const ErModel M_RABBIT = { 12, {
    { CR_MOB_RABBIT_BROWN,26,24, -1,5.5f,-3.7f, 2,1,7,  3,17.5f,3.7f, 0,0,0, 0,1 },
    { CR_MOB_RABBIT_BROWN, 8,24, -1,5.5f,-3.7f, 2,1,7, -3,17.5f,3.7f, 0,0,0, 0,0 },
    { CR_MOB_RABBIT_BROWN,30,15, -1,0,-1, 2,4,5,  3,17.5f,3.7f, -.34906584f,0,0, 0,1 },
    { CR_MOB_RABBIT_BROWN,16,15, -1,0,-1, 2,4,5, -3,17.5f,3.7f, -.34906584f,0,0, 0,0 },
    { CR_MOB_RABBIT_BROWN, 0, 0, -3,-2,-10, 6,5,10, 0,19,8, -.34906584f,0,0, 0,0 },
    { CR_MOB_RABBIT_BROWN, 8,15, -1,0,-1, 2,7,2,  3,17,-1, -.17453292f,0,0, 0,0 },
    { CR_MOB_RABBIT_BROWN, 0,15, -1,0,-1, 2,7,2, -3,17,-1, -.17453292f,0,0, 0,0 },
    { CR_MOB_RABBIT_BROWN,32, 0, -2.5f,-4,-5, 5,4,5, 0,16,-1, 0,0,0, 0,0 },
    { CR_MOB_RABBIT_BROWN,52, 0, -2.5f,-9,-1, 2,5,1, 0,16,-1, 0,-.2617994f,0, 0,0 },
    { CR_MOB_RABBIT_BROWN,58, 0,  .5f,-9,-1, 2,5,1, 0,16,-1, 0, .2617994f,0, 0,0 },
    { CR_MOB_RABBIT_BROWN,52, 6, -1.5f,-1.5f,0, 3,3,2, 0,20,7, -.3490659f,0,0, 0,0 },
    { CR_MOB_RABBIT_BROWN,32, 9, -.5f,-2.5f,-5.5f, 1,1,1, 0,16,-1, 0,0,0, 0,0 },
} , 0.6f };

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

/* ModelWolf: head with ears/muzzle, rotated body/mane, four legs and tail. */
static const ErModel M_WOLF = { 11, {
    { CR_MOB_WOLF,  0,  0, -3,-3,-2, 6,6,4, -1,13.5f,-7, 0,0,0, 0,0 },
    { CR_MOB_WOLF, 16, 14, -3,-5, 0, 2,2,1, -1,13.5f,-7, 0,0,0, 0,0 },
    { CR_MOB_WOLF, 16, 14,  1,-5, 0, 2,2,1, -1,13.5f,-7, 0,0,0, 0,1 },
    { CR_MOB_WOLF,  0, 10, -1.5f,0,-5, 3,3,4, -1,13.5f,-7, 0,0,0, 0,0 },
    { CR_MOB_WOLF, 18, 14, -4,-2,-3, 6,9,6, 0,14,2, QUAD_BODY_ROT,0,0, 0,0 },
    { CR_MOB_WOLF, 21,  0, -4,-3,-3, 8,6,7, -1,14,-3, QUAD_BODY_ROT,0,0, 0,0 },
    { CR_MOB_WOLF,  0, 18, -1,0,-1, 2,8,2, -2.5f,16,7, 0,0,0, 0,0 },
    { CR_MOB_WOLF,  0, 18, -1,0,-1, 2,8,2,  0.5f,16,7, 0,0,0, 0,1 },
    { CR_MOB_WOLF,  0, 18, -1,0,-1, 2,8,2, -2.5f,16,-4, 0,0,0, 0,0 },
    { CR_MOB_WOLF,  0, 18, -1,0,-1, 2,8,2,  0.5f,16,-4, 0,0,0, 0,1 },
    { CR_MOB_WOLF,  9, 18, -1,0,-1, 2,8,2, -1,12,8, 0.9f,0,0, 0,0 },
} };

/* ModelOcelot: head, muzzle/ears, body, legs and two-part tail. */
static const ErModel M_OCELOT = { 11, {
    { CR_MOB_OCELOT,  0,  0, -2.5f,-2,-3, 5,4,5, 0,15,-9, 0,0,0, 0,0 },
    { CR_MOB_OCELOT,  0, 24, -1.5f,0,-4, 3,2,2, 0,15,-9, 0,0,0, 0,0 },
    { CR_MOB_OCELOT,  0, 10, -2,-3,-1, 1,1,2, 0,15,-9, 0,0,0, 0,0 },
    { CR_MOB_OCELOT,  6, 10,  1,-3,-1, 1,1,2, 0,15,-9, 0,0,0, 0,1 },
    { CR_MOB_OCELOT, 20,  0, -2,-3,-8, 4,16,6, 0,12,-10, QUAD_BODY_ROT,0,0, 0,0 },
    { CR_MOB_OCELOT,  8, 13, -1,0,1, 2,6,2,  1.1f,18,5, 0,0,0, 0,0 },
    { CR_MOB_OCELOT,  8, 13, -1,0,1, 2,6,2, -1.1f,18,5, 0,0,0, 0,1 },
    { CR_MOB_OCELOT, 40,  0, -1,0,0, 2,10,2,  1.2f,13,-5, 0,0,0, 0,0 },
    { CR_MOB_OCELOT, 40,  0, -1,0,0, 2,10,2, -1.2f,13,-5, 0,0,0, 0,1 },
    { CR_MOB_OCELOT,  0, 15, -0.5f,0,0, 1,8,1, 0,15,8, 0.9f,0,0, 0,0 },
    { CR_MOB_OCELOT,  4, 15, -0.5f,0,0, 1,8,1, 0,20,14, 1.7278761f,0,0, 0,0 },
} };

/* ModelSquid (64x32): body 12x16x12 + 8 tentacles 2x18x2 around a ring.
 * rotationPointY of body is +8 (constructor); tentacles at Y=15, ring r=5.
 * Tentacle rotateAngleY is fixed at construction; emit_squid supplies the
 * live interpolated tentacle angle for rotateAngleX. */
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

/* ModelIronGolem(0,-7): head/nose, two-piece torso, long arms and legs. */
static const ErModel M_IRON_GOLEM = { .nparts = 8, .parts = {
    { CR_MOB_IRON_GOLEM,  0, 0, -4,-12,-5.5f, 8,10,8,
       0,-7,-2, 0,0,0, 0,0 },
    { CR_MOB_IRON_GOLEM, 24, 0, -1, -5,-7.5f, 2, 4,2,
       0,-7,-2, 0,0,0, 0,0 },
    { CR_MOB_IRON_GOLEM,  0,40, -9, -2,-6, 18,12,11,
       0,-7,0, 0,0,0, 0,0 },
    { CR_MOB_IRON_GOLEM,  0,70, -4.5f,10,-3, 9,5,6,
       0,-7,0, 0,0,0, .5f,0 },
    { CR_MOB_IRON_GOLEM, 60,21, -13,-2.5f,-3, 4,30,6,
       0,-7,0, 0,0,0, 0,0 },
    { CR_MOB_IRON_GOLEM, 60,58,   9,-2.5f,-3, 4,30,6,
       0,-7,0, 0,0,0, 0,0 },
    { CR_MOB_IRON_GOLEM, 37, 0, -3.5f,-3,-3, 6,16,5,
      -4,11,0, 0,0,0, 0,0 },
    { CR_MOB_IRON_GOLEM, 60, 0, -3.5f,-3,-3, 6,16,5,
       5,11,0, 0,0,0, 0,1 },
} };

/* ModelIllager base pose. Evoker and non-aggressive Vindicator both render
 * the crossed-arm body; their identical 64x64 UV layout differs only by skin. */
static const ErModel M_ILLAGER = { .nparts = 11, .scale = 0.9375f, .parts = {
    { CR_MOB_VINDICATOR,  0,  0, -4,-10,-4, 8,10,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_VINDICATOR, 24,  0, -1, -1,-6, 2, 4,2,  0,-2,0, 0,0,0, 0,0 },
    { CR_MOB_VINDICATOR, 16, 20, -4,  0,-3, 8,12,6,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_VINDICATOR,  0, 38, -4,  0,-3, 8,18,6,  0,0,0, 0,0,0, 0.5f,0 },
    { CR_MOB_VINDICATOR, 44, 22, -8, -2,-2, 4, 8,4,  0,3,-1, VILLAGER_ARM_AX,0,0, 0,0 },
    { CR_MOB_VINDICATOR, 44, 22,  4, -2,-2, 4, 8,4,  0,3,-1, VILLAGER_ARM_AX,0,0, 0,0 },
    { CR_MOB_VINDICATOR, 40, 38, -4,  2,-2, 8, 4,4,  0,3,-1, VILLAGER_ARM_AX,0,0, 0,0 },
    { CR_MOB_VINDICATOR, 40, 46, -3, -2,-2, 4,12,4, -5,2,0, 0,0,0, 0,0 },
    { CR_MOB_VINDICATOR, 40, 46, -1, -2,-2, 4,12,4,  5,2,0, 0,0,0, 0,1 },
    { CR_MOB_VINDICATOR,  0, 22, -2,  0,-2, 4,12,4, -2,12,0, 0,0,0, 0,0 },
    { CR_MOB_VINDICATOR,  0, 22, -2,  0,-2, 4,12,4,  2,12,0, 0,0,0, 0,1 },
} };

/* ModelVex: ModelBiped without headwear/left leg, replacement six-wide right
 * leg, and the two one-texel wings. RenderVex scales the full model by 0.4. */
static const ErModel M_VEX = { .nparts = 7, .scale = 0.4f, .parts = {
    { CR_MOB_VEX,  0,  0, -4,-8,-4, 8, 8,8,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_VEX, 16, 16, -4, 0,-2, 8,12,4,  0,0,0, 0,0,0, 0,0 },
    { CR_MOB_VEX, 40, 16, -3,-2,-2, 4,12,4, -5,2,0, 0,0,0, 0,0 },
    { CR_MOB_VEX, 40, 16, -1,-2,-2, 4,12,4,  5,2,0, 0,0,0, 0,1 },
    { CR_MOB_VEX, 32,  0, -1,-1,-2, 6,10,4, -1.9f,12,0,
      ER_PI/5.0f,0,0, 0,0 },
    { CR_MOB_VEX,  0, 32, -20,0,0, 20,12,1, 0,1,2,
      0.47123894f,0.47123894f,0.47123894f, 0,0 },
    { CR_MOB_VEX,  0, 32,   0,0,0, 20,12,1, 0,1,2,
      0.47123894f,-0.47123894f,-0.47123894f, 0,1 },
} };

/* ModelEvokerFangs. The +3.968 Y pivot folds RenderEvokerFangs' -0.626
 * translation into the standard feet-space emitter. Its animated outer scale
 * is applied in gm_entities_emit. */
static const ErModel M_EVOKER_FANGS = { .nparts = 3, .parts = {
    { CR_MOB_FANGS,  0,0, 0,0,0, 10,12,10, -5,25.968f,-5, 0,0,0, 0,0 },
    { CR_MOB_FANGS, 40,0, 0,0,0,  4,14, 8, 1.5f,25.968f,-4, 0,0,0, 0,0 },
    { CR_MOB_FANGS, 40,0, 0,0,0,  4,14, 8,-1.5f,25.968f, 4, 0,ER_PI,0, 0,0 },
} };

/* ModelGuardian: five body boxes, twelve articulated spines, eye, and the
 * four tail/fin boxes. Child pivots are flattened per frame below. */
static const ErModel M_GUARDIAN = { .nparts = 22, .parts = {
    { CR_MOB_GUARDIAN,  0, 0, -6,10,-8, 12,12,16, 0,0,0, 0,0,0, 0,0 },
    { CR_MOB_GUARDIAN,  0,28, -8,10,-6,  2,12,12, 0,0,0, 0,0,0, 0,0 },
    { CR_MOB_GUARDIAN,  0,28,  6,10,-6,  2,12,12, 0,0,0, 0,0,0, 0,1 },
    { CR_MOB_GUARDIAN, 16,40, -6, 8,-6, 12, 2,12, 0,0,0, 0,0,0, 0,0 },
    { CR_MOB_GUARDIAN, 16,40, -6,22,-6, 12, 2,12, 0,0,0, 0,0,0, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2, 0,16, 0,
      1.75f*ER_PI,0,0, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2, 0,16, 0,
      0.25f*ER_PI,0,0, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2, 0,16, 8,
      0,0,0.25f*ER_PI, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2, 0,16,-8,
      0,0,1.75f*ER_PI, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2,-8,16,-8,
      0.5f*ER_PI,0.25f*ER_PI,0, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2, 8,16,-8,
      0.5f*ER_PI,1.75f*ER_PI,0, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2, 8,16, 8,
      0.5f*ER_PI,1.25f*ER_PI,0, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2,-8,16, 8,
      0.5f*ER_PI,0.75f*ER_PI,0, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2, 0,24, 8,
      1.25f*ER_PI,0,0, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2, 0,24,-8,
      0.75f*ER_PI,0,0, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2, 8,24, 0,
      0,0,0.75f*ER_PI, 0,0 },
    { CR_MOB_GUARDIAN,  0, 0, -1,-4.5f,-1, 2,9,2,-8,24, 0,
      0,0,1.25f*ER_PI, 0,0 },
    { CR_MOB_GUARDIAN,  8, 0, -1,15, 0, 2,2,1, 0,1,-8.25f,
      0,0,0, 0,0 },
    { CR_MOB_GUARDIAN, 40, 0, -2,14, 7, 4,4,8, 0,0,0,
      0,0,0, 0,0 },
    { CR_MOB_GUARDIAN,  0,54,  0,14, 0, 3,3,7,-1.5f,0.5f,14,
      0,0,0, 0,0 },
    { CR_MOB_GUARDIAN, 41,32,  0,14, 0, 2,2,6,-1.0f,1.0f,20,
      0,0,0, 0,0 },
    { CR_MOB_GUARDIAN, 25,19,  1,10.5f,3, 1,9,9,-1.0f,1.0f,20,
      0,0,0, 0,0 },
} };

/* ModelBat (64x64, render scale 0.35). A dedicated emitter below applies the
 * exact ModelRenderer parent-child hierarchy for both flying and hanging. */
static const ErModel M_BAT = { .nparts = 9, .scale = 0.35f, .parts = {
    { CR_MOB_BAT,  0,  0,  -3,-3,-3,    6, 6,6,  0,0,0,        0,0,0, 0,0 },
    { CR_MOB_BAT, 24,  0,  -4,-6,-2,    3, 4,1,  0,0,0,        0,0,0, 0,0 },
    { CR_MOB_BAT, 24,  0,   1,-6,-2,    3, 4,1,  0,0,0,        0,0,0, 0,1 },
    { CR_MOB_BAT,  0, 16,  -3, 4,-3,    6,12,6,  0,0,0,        0,0,0, 0,0 },
    { CR_MOB_BAT,  0, 34,  -5,16, 0,   10, 6,1,  0,0,0,        0,0,0, 0,0 },
    { CR_MOB_BAT, 42,  0, -12, 1, 1.5f,10,16,1,  0,0,0,        0,0,0, 0,0 },
    { CR_MOB_BAT, 24, 16,  -8, 1, 0,    8,12,1, -12,1,1.5f,    0,0,0, 0,0 },
    { CR_MOB_BAT, 42,  0,   2, 1, 1.5f,10,16,1,  0,0,0,        0,0,0, 0,1 },
    { CR_MOB_BAT, 24, 16,   0, 1, 0,    8,12,1,  12,1,1.5f,    0,0,0, 0,1 },
} };

/* ModelLlama (128x64): four head boxes share one ModelRenderer, followed by
 * body, legs, and the two conditional chest boxes. A dedicated emitter below
 * reproduces the child groups and LayerLlamaDecor's inflated second model. */
static const ErModel M_LLAMA = { .nparts = 11, .parts = {
    { CR_MOB_LLAMA,  0,  0, -2,-14,-10,  4, 4,9,  0, 7,-6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA,  0, 14, -4,-16, -6,  8,18,6,  0, 7,-6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 17,  0, -4,-19, -4,  3, 3,2,  0, 7,-6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 17,  0,  1,-19, -4,  3, 3,2,  0, 7,-6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 29,  0, -6,-10, -7, 12,18,10, 0, 5, 2,  QUAD_BODY_ROT,0,0, 0,0 },
    { CR_MOB_LLAMA, 29, 29, -2,  0, -2,  4,14,4, -3.5f,10, 6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 29, 29, -2,  0, -2,  4,14,4,  3.5f,10, 6,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 29, 29, -2,  0, -2,  4,14,4, -3.5f,10,-5,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 29, 29, -2,  0, -2,  4,14,4,  3.5f,10,-5,  0,0,0, 0,0 },
    { CR_MOB_LLAMA, 45, 28, -3, 0, 0, 8,8,3, -8.5f,3,3,
      0,ER_PI/2.0f,0, 0,0 },
    { CR_MOB_LLAMA, 45, 41, -3, 0, 0, 8,8,3,  5.5f,3,3,
      0,ER_PI/2.0f,0, 0,0 },
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

/* ModelEnderMite: four independently animated body segments. */
static const ErModel M_ENDERMITE = { .nparts = 4, .scale = 1.0f, .parts = {
    { CR_MOB_ENDERMITE, 0,  0, -2.0f, 0,-1.0f, 4,3,2,
      0,21,-3.5f, 0,0,0, 0,0 },
    { CR_MOB_ENDERMITE, 0,  5, -3.0f, 0,-2.5f, 6,4,5,
      0,20, 0.0f, 0,0,0, 0,0 },
    { CR_MOB_ENDERMITE, 0, 14, -1.5f, 0,-0.5f, 3,3,1,
      0,21, 3.0f, 0,0,0, 0,0 },
    { CR_MOB_ENDERMITE, 0, 18, -0.5f, 0,-0.5f, 1,2,1,
      0,22, 4.0f, 0,0,0, 0,0 },
} };

/* ModelSnowMan (64x64): body, lower body, head, then both stick arms. The
 * pumpkin is LayerSnowmanHead and is appended conditionally below. */
static const ErModel M_SNOWMAN = { .nparts = 5, .scale = 1.0f, .parts = {
    { CR_MOB_SNOWMAN,  0,16, -5,-10,-5, 10,10,10, 0,13,0,
      0,0,0, -0.5f,0 },
    { CR_MOB_SNOWMAN,  0,36, -6,-12,-6, 12,12,12, 0,24,0,
      0,0,0, -0.5f,0 },
    { CR_MOB_SNOWMAN,  0, 0, -4, -8,-4,  8, 8, 8, 0, 4,0,
      0,0,0, -0.5f,0 },
    { CR_MOB_SNOWMAN, 32, 0, -1,  0,-1, 12, 2, 2, 0, 6,0,
      0,0,1.0f, -0.5f,0 },
    { CR_MOB_SNOWMAN, 32, 0, -1,  0,-1, 12, 2, 2, 0, 6,0,
      0,ER_PI,-1.0f, -0.5f,0 },
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
    { CR_MOB_ARMORSTAND,  0,  0, -1,-7,-1,  2, 7, 2,  0, 1,0,  0,0,0, 0,0 },
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

/* ModelShulker. The lid rotation point/yaw are filled from peekAmount for
 * each view; the head is rendered by RenderShulker.HeadLayer. */
static const ErModel M_SHULKER = { .nparts = 3, .scale = 0.999f, .parts = {
    { CR_MOB_SHULKER,  0,28, -8,-8,-8, 16, 8,16, 0,24,0,
      0,0,0, 0,0 },
    { CR_MOB_SHULKER,  0, 0, -8,-16,-8, 16,12,16, 0,24,0,
      0,0,0, 0,0 },
    { CR_MOB_SHULKER,  0,52, -3, 0,-3, 6,6,6, 0,12,0,
      0,0,0, 0,0 },
} };

/* ModelShulkerBullet's one ModelRenderer with three mutually perpendicular
 * boxes. Pivot y=24 adapts its origin-centred model to the feet-space emitter. */
static const ErModel M_SHULKER_BULLET = {
    .nparts = 3, .scale = 1.0f, .parts = {
    { CR_MOB_SHULKER_SPARK,  0, 0, -4,-4,-1, 8,8,2, 0,24,0,
      0,0,0, 0,0 },
    { CR_MOB_SHULKER_SPARK,  0,10, -1,-4,-4, 2,8,8, 0,24,0,
      0,0,0, 0,0 },
    { CR_MOB_SHULKER_SPARK, 20, 0, -4,-1,-4, 8,2,8, 0,24,0,
      0,0,0, 0,0 },
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

/* ModelElytra. LayerElytra translates +0.125 on model Z before rendering;
 * two model units in this table encode that translation. */
static const ErPart ARMOR_STAND_ELYTRA_PARTS[] = {
    { CR_MOB_ELYTRA, 22,0, -10,0,0, 10,20,2,  5,0,2,
      0.2617994f,0,-0.2617994f, 1.0f,0 },
    { CR_MOB_ELYTRA, 22,0,   0,0,0, 10,20,2, -5,0,2,
      0.2617994f,0, 0.2617994f, 1.0f,1 },
};

static int er_armor_stand_skull_sprite(int meta) {
    switch (meta) {
        case 0: return CR_MOB_SKELETON;
        case 1: return CR_MOB_WITHER_SKELETON;
        case 2: return CR_MOB_ZOMBIE;
        case 3: return CR_MOB_STEVE;
        case 4: return CR_MOB_CREEPER;
        default: return -1;
    }
}

static int emit_box(const ErPart *p, float cs, float sn, float sc,
                    float fx, float fy, float fz, CrRgba tint,
                    float light, float blk, float roll_c, float roll_s,
                    CrVertex *out);

static int er_emit_armor_stand_dragon_head(
        const GmEntityView *v, float cs, float sn, float head_sc,
        float fx, float fy, float fz, CrRgba tint, float lv, float blk,
        float roll_c, float roll_s, CrVertex *out) {
    /* LayerCustomHead * TileEntitySkullRenderer * ModelDragonHead.  The two
     * skull scales and the model's .75 scale collapse to .890625.  Y and Z
     * are both reflected; 7.9866667 model units carry the dragon model's
     * -0.374375 Y translation through that scale. */
    static const ErPart boxes[] = {
        { CR_MOB_DRAGON,176,44, -6, 7.9866667f-(-1+5),  8,12,5,16,
          0,1,0, 0,0,0, 0,0 },
        { CR_MOB_DRAGON,112,30, -8, 7.9866667f-(-8+16),-6,16,16,16,
          0,1,0, 0,0,0, 0,0 },
        { CR_MOB_DRAGON,  0, 0, -5, 7.9866667f-(-12+4),-2,2,4,6,
          0,1,0, 0,0,0, 0,1 },
        { CR_MOB_DRAGON,112, 0, -5, 7.9866667f-(-3+2),18,2,2,4,
          0,1,0, 0,0,0, 0,1 },
        { CR_MOB_DRAGON,  0, 0,  3, 7.9866667f-(-12+4),-2,2,4,6,
          0,1,0, 0,0,0, 0,0 },
        { CR_MOB_DRAGON,112, 0,  3, 7.9866667f-(-3+2),18,2,2,4,
          0,1,0, 0,0,0, 0,0 },
        { CR_MOB_DRAGON,176,65, -6, 7.9866667f-(0+4), 8,12,4,16,
          0,1,0, 0.2f,0,0, 0,0 },
    };
    const float nested_sc = head_sc * 0.890625f;
    const float nested_fy = fy + (23.0f / 16.0f)
        * (head_sc - nested_sc);
    int written = 0;
    for (int index = 0; index < 7; ++index) {
        ErPart part = boxes[index];
        if (v->stand_pose_valid) {
            part.ax += v->stand_pose[0][0] * ER_DEG2RAD;
            part.ay = v->stand_pose[0][1] * ER_DEG2RAD;
            part.az = v->stand_pose[0][2] * ER_DEG2RAD;
        }
        written += emit_box(
            &part, cs, sn, nested_sc, fx, nested_fy, fz,
            tint, lv, blk, roll_c, roll_s, out + written);
    }
    return written;
}

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
        case ER_TYPE_GIANT:    return &M_ZOMBIE;
        case ER_TYPE_ZOMBIE_VILLAGER: return &M_ZOMBIE_VILLAGER;
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
        case ER_TYPE_RABBIT:   return &M_RABBIT;
        case ER_TYPE_POLAR_BEAR: return &M_POLAR_BEAR;
        case ER_TYPE_CHICKEN:  return &M_CHICKEN;
        case ER_TYPE_WOLF:     return &M_WOLF;
        case ER_TYPE_OCELOT:   return &M_OCELOT;
        case ER_TYPE_SQUID:
            if (!g_squid_init) squid_build();
            return &g_squid;
        case ER_TYPE_WITCH:    return &M_WITCH;
        case ER_TYPE_VILLAGER: return &M_VILLAGER;
        case ER_TYPE_VINDICATOR:
        case ER_TYPE_EVOKER: return &M_ILLAGER;
        case ER_TYPE_VEX: return &M_VEX;
        case ER_TYPE_EVOKER_FANGS: return &M_EVOKER_FANGS;
        case ER_TYPE_GUARDIAN:
        case ER_TYPE_ELDER_GUARDIAN: return &M_GUARDIAN;
        case ER_TYPE_IRON_GOLEM: return &M_IRON_GOLEM;
        case ER_TYPE_SNOWMAN: return &M_SNOWMAN;
        case ER_TYPE_BAT:      return &M_BAT;
        case ER_TYPE_LLAMA:    return 0; /* dedicated coat/decor/chest emitter */
        case ER_TYPE_GHAST:    return &M_GHAST;
        case ER_TYPE_MAGMA:    return &M_MAGMA;
        case ER_TYPE_SLIME:    return &M_SLIME;
        case ER_TYPE_SILVERFISH: return &M_SILVERFISH;
        case ER_TYPE_ENDERMITE: return &M_ENDERMITE;
        case ER_TYPE_BOAT:     return &M_BOAT;
        case ER_TYPE_MINECART:
        case ER_TYPE_MINECART_CHEST:
        case ER_TYPE_MINECART_FURNACE:
        case ER_TYPE_MINECART_HOPPER:
        case ER_TYPE_MINECART_TNT:
        case ER_TYPE_MINECART_SPAWNER:
        case ER_TYPE_MINECART_COMMAND:
            return &M_MINECART;
        case ER_TYPE_ARMOR_STAND: return &M_ARMOR_STAND;
        case ER_TYPE_SHULKER: return &M_SHULKER;
        case ER_TYPE_SHULKER_BULLET: return &M_SHULKER_BULLET;
        case ER_TYPE_WITHER: return &M_WITHER;
        case ER_TYPE_WITHER_SKULL: return 0; /* dedicated direct head render */
        default:               return &M_MARKER; /* legacy marker box */
    }
}

static int er_is_minecart(int type) {
    return type == ER_TYPE_MINECART ||
           type == ER_TYPE_MINECART_CHEST ||
           type == ER_TYPE_MINECART_FURNACE ||
           type == ER_TYPE_MINECART_HOPPER ||
           type == ER_TYPE_MINECART_TNT ||
           type == ER_TYPE_MINECART_SPAWNER ||
           type == ER_TYPE_MINECART_COMMAND;
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

static float er_shade_item(float nx, float ny, float nz);

/* RenderLivingBase.applyRotations death keel, in radians about the local Z:
 *   f = sqrt((deathTime + partialTicks - 1) / 20 * 1.6), clamped to 1
 *   rotate(f * getDeathMaxRotation, 0, 0, 1)
 * Most represented renderers use 90 degrees. RenderEndermite overrides it
 * with 180 degrees.
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
    return f * (v->type == ER_TYPE_ENDERMITE ? ER_PI : ER_PI / 2.0f);
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
        /* The entity pass runs under RenderHelper.enableStandardItemLighting.
         * Its two world-space directional lights are not Minecraft's block
         * face 1/.8/.6/.5 constants, including on ordinary ModelBox faces. */
        float shade = er_shade_item(nx, ny, nz);

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

/* RenderLivingBase disables GL_CULL_FACE before drawing both its base model
 * and layers. The shared magma transform culls globally, so expand a selected
 * model pass to both windings while preserving Java's original triangle
 * order. Exactly one winding survives for an ordinary face; a back-facing
 * textured face can still show through an alpha hole in the nearer face. */
static int er_expand_twosided(CrVertex *out, int start, int end, int max) {
    int n = end - start;
    if (n < 0 || n % 3 || end + n > max) return end;
    for (int tri = n / 3 - 1; tri >= 0; --tri) {
        CrVertex a = out[start + tri * 3 + 0];
        CrVertex b = out[start + tri * 3 + 1];
        CrVertex c = out[start + tri * 3 + 2];
        int dst = start + tri * 6;
        out[dst + 0] = a;
        out[dst + 1] = b;
        out[dst + 2] = c;
        out[dst + 3] = a;
        out[dst + 4] = c;
        out[dst + 5] = b;
    }
    return end + n;
}

int gm_chests_emit(const GmChestRenderView *views, int n,
                   float partial_ticks, CrVertex *out, int max) {
    if (!views || n <= 0 || !out || max < 108) return 0;
    int written = 0;
    if (partial_ticks < 0.0f) partial_ticks = 0.0f;
    if (partial_ticks > 1.0f) partial_ticks = 1.0f;
    for (int i = 0; i < n && written + 108 <= max; ++i) {
        const GmChestRenderView *v = &views[i];
        float open = v->prev_lid + (v->lid - v->prev_lid) * partial_ticks;
        float closed = 1.0f - open;
        float lid_angle = -(1.0f - closed * closed * closed)
            * (ER_PI / 2.0f);
        int sprite = v->sprite == GM_CHEST_TEXTURE_NORMAL
            ? CR_MOB_CHEST_NORMAL
            : v->sprite == GM_CHEST_TEXTURE_TRAPPED
            ? CR_MOB_CHEST_TRAPPED
            : v->sprite == GM_CHEST_TEXTURE_NORMAL_DOUBLE
            ? CR_MOB_CHEST_NORMAL_DOUBLE
            : v->sprite == GM_CHEST_TEXTURE_TRAPPED_DOUBLE
            ? CR_MOB_CHEST_TRAPPED_DOUBLE
            : CR_MOB_ENDER_CHEST;
        int width = v->large ? 30 : 14;
        int knob_x = v->large ? 16 : 8;
        ErPart parts[3] = {
            { sprite, 0, 0, 0,-5,-14, width,5,14,
              1,7,15, lid_angle,0,0, 0,0 },
            { sprite, 0, 0, -1,-2,-15, 2,4,1,
              knob_x,7,15, lid_angle,0,0, 0,0 },
            { sprite, 0,19, 0,0,0, width,10,14,
              1,6,1, 0,0,0, 0,0 },
        };
        float cs, sn, fx, fz;
        switch (v->meta & 7) {
            case 2:
                cs = 1.0f; sn = 0.0f;
                fx = (float)v->x + 1.0f; fz = (float)v->z;
                break;
            case 4:
                cs = 0.0f; sn = 1.0f;
                fx = (float)v->x; fz = (float)v->z;
                break;
            case 5:
                cs = 0.0f; sn = -1.0f;
                fx = (float)v->x + 1.0f; fz = (float)v->z + 1.0f;
                break;
            default:
                cs = -1.0f; sn = 0.0f;
                fx = (float)v->x; fz = (float)v->z + 1.0f;
                break;
        }
        fx += (float)v->shift_x;
        fz += (float)v->shift_z;
        for (int part = 0; part < 3; ++part)
            written += emit_box(
                &parts[part], cs, sn, 1.0f,
                fx, (float)v->y - 0.5f, fz,
                (CrRgba){255,255,255,255}, v->light, v->blk,
                1.0f, 0.0f, out + written);
    }
    return written;
}

int gm_ender_chests_emit(const GmEnderChestRenderView *views, int n,
                         float partial_ticks, CrVertex *out, int max) {
    if (!views || n <= 0 || !out || max < 108) return 0;
    int written = 0;
    for (int i = 0; i < n && written + 108 <= max; ++i) {
        GmChestRenderView chest = {
            views[i].x, views[i].y, views[i].z, views[i].meta,
            views[i].prev_lid, views[i].lid,
            views[i].light, views[i].blk,
            GM_CHEST_TEXTURE_ENDER, 0, 0, 0,
        };
        written += gm_chests_emit(
            &chest, 1, partial_ticks, out + written, max - written);
    }
    return written;
}

/* -------------------------------------------------------------------------- */
/* EntityPainting / EntityLeashKnot / EntityLiving leash renderers.           */

typedef struct { int w, h, u, v; } ErPaintingArt;
static const ErPaintingArt ER_PAINTING_ART[26] = {
    {16,16,0,0}, {16,16,16,0}, {16,16,32,0}, {16,16,48,0},
    {16,16,64,0}, {16,16,80,0}, {16,16,96,0},
    {32,16,0,32}, {32,16,32,32}, {32,16,64,32},
    {32,16,96,32}, {32,16,128,32}, {16,32,0,64},
    {16,32,16,64}, {32,32,0,128}, {32,32,32,128},
    {32,32,64,128}, {32,32,96,128}, {32,32,128,128},
    {32,32,160,128}, {64,32,0,96}, {64,64,0,192},
    {64,64,64,192}, {64,64,128,192}, {64,48,192,64},
    {64,48,192,112},
};

static float er_hanging_yaw(int facing) {
    static const float yaw[6] = {0,0,180,0,90,270};
    return facing >= 2 && facing <= 5 ? yaw[facing] : 0.0f;
}

static CrVec3 er_painting_pos(const GmPaintingView *p,
                              float x, float y, float z) {
    float a = (180.0f - er_hanging_yaw(p->facing)) * ER_DEG2RAD;
    float cs = cosf(a), sn = sinf(a);
    x *= 0.0625f; y *= 0.0625f; z *= 0.0625f;
    return (CrVec3){p->x + x*cs + z*sn, p->y + y,
                    p->z - x*sn + z*cs};
}

static int er_painting_quad(const GmPaintingView *p,
                            const float xyz[4][3], const float uv[4][2],
                            float sky, float blk, CrVertex *out) {
    CrVertex q[4];
    const CrMobSprite *s = &CR_MOB_SPRITES[CR_MOB_PAINTINGS];
    CrVec3 a = er_painting_pos(p, xyz[0][0], xyz[0][1], xyz[0][2]);
    CrVec3 b = er_painting_pos(p, xyz[1][0], xyz[1][1], xyz[1][2]);
    CrVec3 c = er_painting_pos(p, xyz[2][0], xyz[2][1], xyz[2][2]);
    float e1x=b.x-a.x,e1y=b.y-a.y,e1z=b.z-a.z;
    float e2x=c.x-a.x,e2y=c.y-a.y,e2z=c.z-a.z;
    float nx=e1y*e2z-e1z*e2y;
    float ny=e1z*e2x-e1x*e2z;
    float nz=e1x*e2y-e1y*e2x;
    float shade = er_shade_item(nx,ny,nz);
    for (int i=0;i<4;++i) {
        q[i]=(CrVertex){0};
        q[i].pos=er_painting_pos(p,xyz[i][0],xyz[i][1],xyz[i][2]);
        q[i].uv.x=((float)s->x0+uv[i][0])/(float)CR_MOB_ATLAS_W;
        q[i].uv.y=((float)s->y0+uv[i][1])/(float)CR_MOB_ATLAS_H;
        q[i].light=sky;q[i].blk=blk;q[i].ao=shade;
        q[i].tint=(CrRgba){255,255,255,255};
    }
    static const int tri[6]={0,1,2,0,2,3};
    for(int i=0;i<6;++i)out[i]=q[tri[i]];
    return 6;
}

int gm_painting_light_cells(const GmPaintingView *p,int out[][3],int max){
    if(!p||!out||max<=0||p->art<0||p->art>=26||p->facing<2||p->facing>5)
        return 0;
    ErPaintingArt a=ER_PAINTING_ART[p->art];
    int n=0;float left=-(float)a.w*.5f,bottom=-(float)a.h*.5f;
    for(int i=0;i<a.w/16&&n<max;++i)for(int j=0;j<a.h/16&&n<max;++j){
        float x1=left+(float)((i+1)*16),x0=left+(float)(i*16);
        float y1=bottom+(float)((j+1)*16),y0=bottom+(float)(j*16);
        int x=(int)floorf(p->x),y=(int)floorf(p->y+(y1+y0)/32.0f);
        int z=(int)floorf(p->z);float mid=(x1+x0)/32.0f;
        if(p->facing==2)x=(int)floorf(p->x+mid);
        if(p->facing==4)z=(int)floorf(p->z-mid);
        if(p->facing==3)x=(int)floorf(p->x-mid);
        if(p->facing==5)z=(int)floorf(p->z+mid);
        out[n][0]=x;out[n][1]=y;out[n][2]=z;++n;
    }return n;
}

int gm_paintings_emit(const GmPaintingView *views, int n,
                      CrVertex *out, int max) {
    int written=0;
    if(!views||!out||n<=0||max<=0)return 0;
    for(int e=0;e<n;++e){
        const GmPaintingView *p=&views[e];
        if(p->art<0||p->art>=26||p->facing<2||p->facing>5)continue;
        ErPaintingArt a=ER_PAINTING_ART[p->art];
        int need=(a.w/16)*(a.h/16)*36; /* six Java quads per art tile */
        if(written+need>max)break;
        float left=-(float)a.w*0.5f,bottom=-(float)a.h*0.5f;
        for(int i=0;i<a.w/16;++i)for(int j=0;j<a.h/16;++j){
            float x1=left+(float)((i+1)*16),x0=left+(float)(i*16);
            float y1=bottom+(float)((j+1)*16),y0=bottom+(float)(j*16);
            int li=i*(a.h/16)+j;
            float sky=p->lm_light[li],blk=p->lm_blk[li];
            float u1=(float)(a.u+a.w-i*16),u0=(float)(a.u+a.w-(i+1)*16);
            float v1=(float)(a.v+a.h-j*16),v0=(float)(a.v+a.h-(j+1)*16);
            const float front[4][3]={{x1,y0,-.5f},{x0,y0,-.5f},
                                      {x0,y1,-.5f},{x1,y1,-.5f}};
            const float fuv[4][2]={{u0,v1},{u1,v1},{u1,v0},{u0,v0}};
            written+=er_painting_quad(p,front,fuv,sky,blk,out+written);
            const float back[4][3]={{x1,y1,.5f},{x0,y1,.5f},
                                     {x0,y0,.5f},{x1,y0,.5f}};
            const float buv[4][2]={{192,0},{208,0},{208,16},{192,16}};
            written+=er_painting_quad(p,back,buv,sky,blk,out+written);
            const float top[4][3]={{x1,y1,-.5f},{x0,y1,-.5f},
                                    {x0,y1,.5f},{x1,y1,.5f}};
            const float tuv[4][2]={{192,.5f},{208,.5f},{208,.5f},{192,.5f}};
            written+=er_painting_quad(p,top,tuv,sky,blk,out+written);
            const float bot[4][3]={{x1,y0,.5f},{x0,y0,.5f},
                                    {x0,y0,-.5f},{x1,y0,-.5f}};
            written+=er_painting_quad(p,bot,tuv,sky,blk,out+written);
            const float leftq[4][3]={{x1,y1,.5f},{x1,y0,.5f},
                                      {x1,y0,-.5f},{x1,y1,-.5f}};
            const float suv[4][2]={{192.5f,0},{192.5f,16},
                                    {192.5f,16},{192.5f,0}};
            written+=er_painting_quad(p,leftq,suv,sky,blk,out+written);
            const float rightq[4][3]={{x0,y1,-.5f},{x0,y0,-.5f},
                                       {x0,y0,.5f},{x0,y1,.5f}};
            written+=er_painting_quad(p,rightq,suv,sky,blk,out+written);
        }
    }
    return written;
}

int gm_leash_knots_emit(const GmLeashKnotView *views, int n,
                        CrVertex *out, int max) {
    int written=0;
    if(!views||!out||n<=0||max<=0)return 0;
    const ErPart knot={CR_MOB_LEAD_KNOT,0,0,-3,-6,-3,6,8,6,
                       0,0,0,0,0,0,0,0};
    for(int i=0;i<n;++i){
        if(written+72>max)break;
        int start=written;
        written+=emit_box(&knot,1,0,1,views[i].x,views[i].y-1.5f,
                          views[i].z,(CrRgba){255,255,255,255},
                          views[i].lm_light,views[i].lm_blk,1,0,out+written);
        written=er_expand_twosided(out,start,written,max);
    }
    return written;
}

static CrVertex er_leash_vertex(float x,float y,float z,int dark){
    CrVertex v={0};v.pos=(CrVec3){x,y,z};v.light=1;v.ao=1;
    v.tint=dark?(CrRgba){89,71,54,255}:(CrRgba){128,102,77,255};
    return v;
}

static int er_leash_segment(CrVertex a,CrVertex b,CrVertex c,CrVertex d,
                            CrVertex *out){
    /* The entity pass inherits GL_FLAT in the 1.11.2 client.  A triangle
     * strip's provoking (last) vertex therefore gives both triangles in one
     * j..j+1 segment the j+1 color; interpolating these alternating colors
     * creates a non-vanilla gradient along the entire leash. */
    a.tint=b.tint=c.tint=d.tint=c.tint;
    CrVertex t[6]={a,b,c,b,d,c};
    for(int i=0;i<6;++i)out[i]=t[i];
    for(int i=0;i<6;i+=3){out[6+i]=t[i];out[7+i]=t[i+2];out[8+i]=t[i+1];}
    return 12;
}

int gm_living_leashes_emit(const GmLeashRenderView *views, int n,
                           CrVertex *out, int max){
    int written=0;
    if(!views||!out||n<=0||max<=0)return 0;
    for(int e=0;e<n;++e){
        const GmLeashRenderView *v=&views[e];
        if(written+576>max)break;
        double y=(double)v->y-(1.6-(double)v->height)*.5;
        double d0=(double)v->holder_yaw*ER_DEG2RAD;
        double d1=(double)v->holder_pitch*ER_DEG2RAD;
        double d2=cos(d0),d3=sin(d0),d4=sin(d1),d5=cos(d1);
        if(v->holder_hanging){d2=0;d3=0;d4=-1;}
        double hx=(double)v->holder_x-d2*.7-d3*.5*d5;
        double hy=(double)v->holder_y+(double)v->holder_eye*.7-d4*.5-.25;
        double hz=(double)v->holder_z-d3*.7+d2*.5*d5;
        double a=(double)v->yaw*ER_DEG2RAD+3.14159265358979323846/2.0;
        d2=cos(a)*(double)v->width*.4;d3=sin(a)*(double)v->width*.4;
        double lx=(double)v->x+d2,ly=(double)v->y,lz=(double)v->z+d3;
        float dx=(float)(hx-lx),dy=(float)(hy-ly),dz=(float)(hz-lz);
        float p[2][25][2][3];
        for(int strip=0;strip<2;++strip)for(int j=0;j<=24;++j){
            float f=(float)j/24.0f;
            float bx=(float)((double)v->x+d2+(double)dx*f);
            float by=(float)(y+(double)dy*(double)((f*f+f)*.5f)
                +(double)((24.0f-(float)j)/18.0f+.125f));
            float bz=(float)((double)v->z+d3+(double)dz*f);
            if(strip==0){p[strip][j][0][0]=bx;p[strip][j][0][1]=by;p[strip][j][0][2]=bz;
                p[strip][j][1][0]=bx+.025f;p[strip][j][1][1]=by+.025f;p[strip][j][1][2]=bz;}
            else{p[strip][j][0][0]=bx;p[strip][j][0][1]=by+.025f;p[strip][j][0][2]=bz;
                p[strip][j][1][0]=bx+.025f;p[strip][j][1][1]=by;p[strip][j][1][2]=bz+.025f;}
        }
        for(int strip=0;strip<2;++strip)for(int j=0;j<24;++j){
            CrVertex a0=er_leash_vertex(p[strip][j][0][0],p[strip][j][0][1],p[strip][j][0][2],!(j&1));
            CrVertex b0=er_leash_vertex(p[strip][j][1][0],p[strip][j][1][1],p[strip][j][1][2],!(j&1));
            CrVertex a1=er_leash_vertex(p[strip][j+1][0][0],p[strip][j+1][0][1],p[strip][j+1][0][2],!((j+1)&1));
            CrVertex b1=er_leash_vertex(p[strip][j+1][1][0],p[strip][j+1][1][1],p[strip][j+1][1][2],!((j+1)&1));
            written+=er_leash_segment(a0,b0,a1,b1,out+written);
        }
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
 * 0.6 each, model ambient 0.4, colorMaterial AMBIENT_AND_DIFFUSE. Mesa's
 * fixed-function path stores light*material as unorm8 first: ambient 102/255,
 * diffuse 152/255 (the 153*255 unorm product rounds to 152). The light
 * POSITIONs are set while the modelview is the camera matrix, so the
 * directions are fixed in WORLD space and the shade is a pure function of the
 * world face normal:
 *     shade = min(1, 0.4 + 0.6*max(0, n.l0) + 0.6*max(0, n.l1))
 * On an axis-aligned box this gives 1.0 up / 0.737 north-south / 0.496
 * east-west / 0.4 down. ModelEnderCrystal's cubes are rotated 60 degrees about
 * (0.7071,0,0.7071), so their oblique normals use the same calculation too.
 * Exact for boxes drawn with unit normals under a uniform scale, which is
 * every ModelBase box. */
static float er_shade_item(float nx, float ny, float nz) {
    float n = sqrtf(nx*nx + ny*ny + nz*nz);
    if (n <= 0.0f) return 1.0f;
    nx /= n; ny /= n; nz /= n;
    /* Exact Java Vec3d.normalize result after float upload to glLight. */
    const float lx = 0x1.4b2458p-3f;
    const float ly = 0x1.9ded6ep-1f;
    const float lz = -0x1.21bfcep-1f;
    float d0 = nx*lx + ny*ly + nz*lz;
    float d1 = nx*-lx + ny*ly + nz*-lz;
    if (d0 < 0.0f) d0 = 0.0f;
    if (d1 < 0.0f) d1 = 0.0f;
    float s = 102.0f/255.0f + (152.0f/255.0f)*d0
                                + (152.0f/255.0f)*d1;
    return s > 1.0f ? 1.0f : s;
}

/* RenderLivingBase enables GL_RESCALE_NORMAL, not GL_NORMALIZE.  With the
 * non-uniform Slime/Magma squish transform, Mesa rescales from the common X/Z
 * factor: horizontal normals retain length scx/scy.  Ambient is independent
 * of normal length while both diffuse dot products scale with it. */
static float er_shade_item_rescaled(float nx, float ny, float nz,
                                    float normal_length) {
    float n = sqrtf(nx*nx + ny*ny + nz*nz);
    if (n <= 0.0f) return 1.0f;
    nx = nx / n * normal_length;
    ny = ny / n * normal_length;
    nz = nz / n * normal_length;
    const float lx = 0x1.4b2458p-3f;
    const float ly = 0x1.9ded6ep-1f;
    const float lz = -0x1.21bfcep-1f;
    float d0 = nx*lx + ny*ly + nz*lz;
    float d1 = nx*-lx + ny*ly + nz*-lz;
    if (d0 < 0.0f) d0 = 0.0f;
    if (d1 < 0.0f) d1 = 0.0f;
    float s = 102.0f/255.0f + (152.0f/255.0f)*d0
                                + (152.0f/255.0f)*d1;
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

int gm_shulker_boxes_emit(const GmShulkerBoxRenderView *views, int n,
                          float partial_ticks, CrVertex *out, int max) {
    if (!views || n <= 0 || !out || max < 144) return 0;
    if (partial_ticks < 0.0f) partial_ticks = 0.0f;
    if (partial_ticks > 1.0f) partial_ticks = 1.0f;
    int written = 0;
    for (int i = 0; i < n && written + 144 <= max; ++i) {
        const GmShulkerBoxRenderView *v = &views[i];
        float progress = v->prev_progress
            + (v->progress - v->prev_progress) * partial_ticks;
        int color = v->color;
        if (color < 0 || color > 15) color = 10;
        int sprite = CR_MOB_SHULKER_BOX_WHITE + color;
        ErAff base, part;
        er_aff_identity(&base);
        er_aff_translate(
            &base, (float)v->x + 0.5f, (float)v->y + 1.5f,
            (float)v->z + 0.5f);
        er_aff_scale3(&base, 1.0f, -1.0f, -1.0f);
        er_aff_translate(&base, 0.0f, 1.0f, 0.0f);
        er_aff_scale(&base, 0.9995f);
        er_aff_translate(&base, 0.0f, -1.0f, 0.0f);
        switch (v->facing) {
        case 0:
            er_aff_translate(&base, 0.0f, 2.0f, 0.0f);
            er_aff_rot_x(&base, 180.0f);
            break;
        case 2:
            er_aff_translate(&base, 0.0f, 1.0f, 1.0f);
            er_aff_rot_x(&base, 90.0f);
            er_aff_rot_z(&base, 180.0f);
            break;
        case 3:
            er_aff_translate(&base, 0.0f, 1.0f, -1.0f);
            er_aff_rot_x(&base, 90.0f);
            break;
        case 4:
            er_aff_translate(&base, -1.0f, 1.0f, 0.0f);
            er_aff_rot_x(&base, 90.0f);
            er_aff_rot_z(&base, -90.0f);
            break;
        case 5:
            er_aff_translate(&base, 1.0f, 1.0f, 0.0f);
            er_aff_rot_x(&base, 90.0f);
            er_aff_rot_z(&base, 90.0f);
            break;
        default:
            break;
        }
        part = base;
        er_aff_translate(&part, 0.0f, 24.0f * 0.0625f, 0.0f);
        written += er_aff_box_m(
            &part, sprite, 1, 0, 1, 0, 28,
            -8.0f, -8.0f, -8.0f, 16, 8, 16,
            (CrRgba){255,255,255,255}, v->light, v->blk,
            out + written);
        part = base;
        er_aff_translate(&part, 0.0f, -progress * 0.5f, 0.0f);
        er_aff_rot_y(&part, 270.0f * progress);
        er_aff_translate(&part, 0.0f, 24.0f * 0.0625f, 0.0f);
        written += er_aff_box_m(
            &part, sprite, 1, 0, 1, 0, 0,
            -8.0f, -16.0f, -8.0f, 16, 12, 16,
            (CrRgba){255,255,255,255}, v->light, v->blk,
            out + written);
        written = er_expand_twosided(out, written - 72, written, max);
    }
    return written;
}

/* TileEntityBeaconRenderer. VertexBuffer.color(float) truncates each channel
 * to UBYTE before upload, which is exactly CrVertex.tint's representation. */
static CrVertex er_beacon_vertex(double x, double y, double z,
                                 double u, double v,
                                 float red, float green, float blue,
                                 int alpha) {
    CrVertex out;
    memset(&out, 0, sizeof out);
    out.pos.x = (float)x;
    out.pos.y = (float)y;
    out.pos.z = (float)z;
    out.uv.x = (float)u;
    out.uv.y = (float)v;
    out.light = 1.0f;
    out.blk = 0.0f;
    out.tint.r = (u8)(red * 255.0f);
    out.tint.g = (u8)(green * 255.0f);
    out.tint.b = (u8)(blue * 255.0f);
    out.tint.a = (u8)alpha;
    out.ao = 1.0f;
    return out;
}

/* GL_QUADS plus disableCull. Emit the ordinary quad triangulation and its
 * reverse winding so magma's globally culled raster has the same two sides. */
static void er_beacon_quad(double y0, double y1,
                           const double a[2], const double b[2],
                           double v0, double v1,
                           float red, float green, float blue, int alpha,
                           CrVertex *out) {
    CrVertex q[4];
    static const int tri[12] = {0,1,2,0,2,3, 2,1,0,3,2,0};
    q[0] = er_beacon_vertex(a[0], y1, a[1], 1.0, v1,
                            red, green, blue, alpha);
    q[1] = er_beacon_vertex(a[0], y0, a[1], 1.0, v0,
                            red, green, blue, alpha);
    q[2] = er_beacon_vertex(b[0], y0, b[1], 0.0, v0,
                            red, green, blue, alpha);
    q[3] = er_beacon_vertex(b[0], y1, b[1], 0.0, v1,
                            red, green, blue, alpha);
    for (int i = 0; i < 12; ++i) out[i] = q[tri[i]];
}

int gm_beacon_beams_emit(const GmBeaconRenderView *views, int n,
                         float partial_ticks,
                         CrVertex *inner, int inner_max,
                         CrVertex *glow, int glow_max,
                         int *glow_written) {
    int ni = 0, ng = 0;
    if (glow_written) *glow_written = 0;
    if (!views || n <= 0 || !inner || !glow
            || inner_max < 48 || glow_max < 48)
        return 0;
    for (int view_index = 0; view_index < n; ++view_index) {
        const GmBeaconRenderView *view = &views[view_index];
        int y_offset = 0;
        if (!view->segments || view->segment_count <= 0
                || view->texture_scale <= 0.0f)
            continue;
        for (int segment_index = 0;
                segment_index < view->segment_count; ++segment_index) {
            const GmBeaconBeamSegmentView *segment =
                &view->segments[segment_index];
            const double beam_radius = 0.2;
            const double glow_radius = 0.25;
            double d0, d1, d2, angle;
            double inner_p[4][2], glow_p[4][2];
            double inner_v0, inner_v1, glow_v0, glow_v1;
            double y0, y1;
            if (segment->height == 0) continue;
            if (ni + 48 > inner_max || ng + 48 > glow_max) {
                if (glow_written) *glow_written = ng;
                return ni;
            }
            d0 = view->total_world_time + (double)partial_ticks;
            d1 = segment->height < 0 ? d0 : -d0;
            d2 = d1 * 0.2 - floor(d1 * 0.1);
            d2 -= floor(d2);
            angle = d0 * 0.025 * -1.5;
            inner_p[0][0] = view->x + 0.5
                + cos(angle + 2.356194490192345) * beam_radius;
            inner_p[0][1] = view->z + 0.5
                + sin(angle + 2.356194490192345) * beam_radius;
            inner_p[1][0] = view->x + 0.5
                + cos(angle + 0.7853981633974483) * beam_radius;
            inner_p[1][1] = view->z + 0.5
                + sin(angle + 0.7853981633974483) * beam_radius;
            inner_p[2][0] = view->x + 0.5
                + cos(angle + 5.497787143782138) * beam_radius;
            inner_p[2][1] = view->z + 0.5
                + sin(angle + 5.497787143782138) * beam_radius;
            inner_p[3][0] = view->x + 0.5
                + cos(angle + 3.9269908169872414) * beam_radius;
            inner_p[3][1] = view->z + 0.5
                + sin(angle + 3.9269908169872414) * beam_radius;
            y0 = view->y + y_offset;
            y1 = view->y + y_offset + segment->height;
            inner_v0 = -1.0 + d2;
            inner_v1 = segment->height * (double)view->texture_scale
                * (0.5 / beam_radius) + inner_v0;
            er_beacon_quad(y0, y1, inner_p[0], inner_p[1],
                           inner_v0, inner_v1, segment->red, segment->green,
                           segment->blue, 255, inner + ni); ni += 12;
            er_beacon_quad(y0, y1, inner_p[2], inner_p[3],
                           inner_v0, inner_v1, segment->red, segment->green,
                           segment->blue, 255, inner + ni); ni += 12;
            er_beacon_quad(y0, y1, inner_p[1], inner_p[2],
                           inner_v0, inner_v1, segment->red, segment->green,
                           segment->blue, 255, inner + ni); ni += 12;
            er_beacon_quad(y0, y1, inner_p[3], inner_p[0],
                           inner_v0, inner_v1, segment->red, segment->green,
                           segment->blue, 255, inner + ni); ni += 12;

            glow_p[0][0] = view->x + 0.5 - glow_radius;
            glow_p[0][1] = view->z + 0.5 - glow_radius;
            glow_p[1][0] = view->x + 0.5 + glow_radius;
            glow_p[1][1] = view->z + 0.5 - glow_radius;
            glow_p[2][0] = view->x + 0.5 + glow_radius;
            glow_p[2][1] = view->z + 0.5 + glow_radius;
            glow_p[3][0] = view->x + 0.5 - glow_radius;
            glow_p[3][1] = view->z + 0.5 + glow_radius;
            glow_v0 = -1.0 + d2;
            glow_v1 = segment->height * (double)view->texture_scale + glow_v0;
            er_beacon_quad(y0, y1, glow_p[0], glow_p[1],
                           glow_v0, glow_v1, segment->red, segment->green,
                           segment->blue, 31, glow + ng); ng += 12;
            er_beacon_quad(y0, y1, glow_p[2], glow_p[3],
                           glow_v0, glow_v1, segment->red, segment->green,
                           segment->blue, 31, glow + ng); ng += 12;
            er_beacon_quad(y0, y1, glow_p[1], glow_p[2],
                           glow_v0, glow_v1, segment->red, segment->green,
                           segment->blue, 31, glow + ng); ng += 12;
            er_beacon_quad(y0, y1, glow_p[3], glow_p[0],
                           glow_v0, glow_v1, segment->red, segment->green,
                           segment->blue, 31, glow + ng); ng += 12;
            y_offset += segment->height;
        }
    }
    if (glow_written) *glow_written = ng;
    return ni;
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
    static const int BOAT_SPRITES[6] = {
        CR_MOB_BOAT, CR_MOB_BOAT_SPRUCE, CR_MOB_BOAT_BIRCH,
        CR_MOB_BOAT_JUNGLE, CR_MOB_BOAT_ACACIA, CR_MOB_BOAT_DARKOAK,
    };
    int variant = ent->item_meta;
    if (variant < 0 || variant >= 6) variant = 0;
    int sprite = BOAT_SPRITES[variant];
#define BOAT_BOX(U,V,BX,BY,BZ,DX,DY,DZ) \
    n += er_aff_box_m(&part, sprite, 1, 0, 1, (U), (V), \
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
        /* frame capture renders at partialTicks=1, so getRowingTime returns
         * the current paddlePosition (or exactly zero while not rowing). */
        float f = ent->boat_paddle[p] * 40.0f;
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
     * skin. Pack the marker, original light, and f into light; ao must retain
     * ModelBox's standard-item-lighting face factor. */
    float deadf = ent->death_ticks > 0 ? (float)ent->death_ticks / 200.0f
                                       : 0.0f;
    if (deadf >= 1.0f) return 0;
#define DBOX(AF, U, V, X, Y, Z, DX, DY, DZ, MIR) do { \
        float _lv = (deadf > 0.0f) ? -(lv + 1.0f + deadf / 32.0f) : (lv); \
        w += er_aff_box((AF), CR_MOB_DRAGON, 1, (MIR), (U), (V), (X), (Y), (Z), \
                        (DX), (DY), (DZ), tint, _lv, blk, out + w); \
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
 * (husk -> zombie; stray -> skeleton; cave spider -> spider; mooshroom ->
 * cow). Zombie villagers retain their distinct model and skin family. */
int gm_entity_type_for_name(const char *name) {
    static const struct { const char *name; int type; } MAP[] = {
        { "EntityZombie",         ER_TYPE_ZOMBIE },
        { "EntityGiantZombie",    ER_TYPE_GIANT },
        { "EntityHusk",           ER_TYPE_ZOMBIE },
        { "EntityZombieVillager", ER_TYPE_ZOMBIE_VILLAGER },
        { "EntityShulker",        ER_TYPE_SHULKER },
        { "EntityShulkerBullet",  ER_TYPE_SHULKER_BULLET },
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
        { "EntityWolf",           ER_TYPE_WOLF },
        { "EntityOcelot",         ER_TYPE_OCELOT },
        { "EntitySquid",          ER_TYPE_SQUID },
        { "EntityWitch",          ER_TYPE_WITCH },
        { "EntityVillager",       ER_TYPE_VILLAGER },
        { "EntityVindicator",     ER_TYPE_VINDICATOR },
        { "EntityEvoker",         ER_TYPE_EVOKER },
        { "EntityVex",            ER_TYPE_VEX },
        { "EntityEvokerFangs",    ER_TYPE_EVOKER_FANGS },
        { "EntityGuardian",       ER_TYPE_GUARDIAN },
        { "EntityElderGuardian",  ER_TYPE_ELDER_GUARDIAN },
        { "EntityIronGolem",      ER_TYPE_IRON_GOLEM },
        { "EntityRabbit",         ER_TYPE_RABBIT },
        { "EntityPolarBear",      ER_TYPE_POLAR_BEAR },
        { "EntitySnowman",        ER_TYPE_SNOWMAN },
        { "EntityWither",         ER_TYPE_WITHER },
        { "EntityWitherSkull",    ER_TYPE_WITHER_SKULL },
        { "EntityHorse",          ER_TYPE_HORSE },
        { "EntityDonkey",         ER_TYPE_DONKEY },
        { "EntityMule",           ER_TYPE_MULE },
        { "EntitySkeletonHorse",  ER_TYPE_SKELETON_HORSE },
        { "EntityZombieHorse",    ER_TYPE_ZOMBIE_HORSE },
        { "EntityBat",            ER_TYPE_BAT },
        { "EntityLlama",          ER_TYPE_LLAMA },
        { "EntityLlamaSpit",      ER_TYPE_LLAMA_SPIT },
        { "EntityGhast",          ER_TYPE_GHAST },
        { "EntityMagmaCube",      ER_TYPE_MAGMA },
        { "EntitySlime",          ER_TYPE_SLIME },
        { "EntitySilverfish",     ER_TYPE_SILVERFISH },
        { "EntityEndermite",      ER_TYPE_ENDERMITE },
        { "EntityBoat",           ER_TYPE_BOAT },
        { "EntityMinecartEmpty",  ER_TYPE_MINECART },
        { "EntityMinecartChest",  ER_TYPE_MINECART_CHEST },
        { "EntityMinecartFurnace",ER_TYPE_MINECART_FURNACE },
        { "EntityMinecartHopper", ER_TYPE_MINECART_HOPPER },
        { "EntityMinecartTNT",    ER_TYPE_MINECART_TNT },
        { "EntityMinecartMobSpawner", ER_TYPE_MINECART_SPAWNER },
        { "EntityMinecartCommandBlock", ER_TYPE_MINECART_COMMAND },
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
        /* The strict frame path renders at partialTicks=1.0, exactly like the
         * oracle frame_pair contract. RenderXPOrb includes that partial in its
         * color clock even when xpColor itself is pinned to zero. */
        float f9 = ((float)xp_color + 1.0f) / 2.0f;
        float crf = (sinf(f9 + 0.0f) + 1.0f) * 0.5f * 255.0f;
        float cgf = 255.0f;
        float cbf = (sinf(f9 + 4.1887903f) + 1.0f) * 0.1f * 255.0f;
        /* RenderXPOrb enables standard item lighting before applying its
         * billboard rotations. The local +Y normal receives the 0.4 ambient
         * term plus both 0.6 diffuse lights; fixed function then clamps this
         * lit primary colour before the texture combiner. */
        const float l0x = 0.16169041f, l0y = 0.80845207f,
                    l0z = -0.56591642f;
        const float l1x = -0.16169041f, l1y = 0.80845207f,
                    l1z = 0.56591642f;
        float nx = sp * sy, ny = cp, nz = sp * cy;
        float d0 = nx*l0x + ny*l0y + nz*l0z;
        float d1 = nx*l1x + ny*l1y + nz*l1z;
        if (d0 < 0.0f) d0 = 0.0f;
        if (d1 < 0.0f) d1 = 0.0f;
        float item_light = 0.4f + 0.6f * (d0 + d1);
        int cr = (int)fminf(255.0f, crf * item_light);
        int cg = (int)fminf(255.0f, cgf * item_light);
        int cb = (int)fminf(255.0f, cbf * item_light);
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

/* Skin-variant sprite overrides (see gm_entity_type_for_name). */
int gm_entity_skin_for_name(const char *name) {
    static const struct { const char *name; int sprite; } MAP[] = {
        { "EntityPigZombie",  CR_MOB_PIGMAN },
        { "EntityHusk",       CR_MOB_HUSK },
        { "EntityStray",      CR_MOB_STRAY },
        { "EntityCaveSpider", CR_MOB_CAVE_SPIDER },
        { "EntityMooshroom",  CR_MOB_MOOSHROOM },
        { "EntityZombieVillager", CR_MOB_ZOMBIE_VILLAGER },
        { "EntityVindicator", CR_MOB_VINDICATOR },
        { "EntityEvoker", CR_MOB_EVOKER },
        { "EntityVex", CR_MOB_VEX },
        { "EntityEvokerFangs", CR_MOB_FANGS },
        { "EntityGuardian", CR_MOB_GUARDIAN },
        { "EntityElderGuardian", CR_MOB_GUARDIAN_ELDER },
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
        case ER_TYPE_GIANT:    return 10.440001f;
        case ER_TYPE_ZOMBIE_VILLAGER: return 1.74f;
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
        case ER_TYPE_RABBIT:   return 0.5f * 0.85f;
        case ER_TYPE_POLAR_BEAR: return 1.4f * 0.85f;
        case ER_TYPE_CHICKEN:  return 0.7f * 0.85f;
        case ER_TYPE_WOLF:     return 0.85f * 0.85f;
        case ER_TYPE_OCELOT:   return 0.7f * 0.85f;
        case ER_TYPE_SQUID:    return 0.4f;           /* height * 0.5 */
        case ER_TYPE_WITCH:    return 1.95f * 0.85f;
        case ER_TYPE_VILLAGER: return 1.62f;
        case ER_TYPE_VINDICATOR:
        case ER_TYPE_EVOKER: return 1.62f;
        case ER_TYPE_VEX: return 0.4f;
        case ER_TYPE_EVOKER_FANGS: return 0.4f;
        case ER_TYPE_GUARDIAN: return 0.425f;
        case ER_TYPE_ELDER_GUARDIAN: return 0.99875f;
        case ER_TYPE_IRON_GOLEM: return 2.7f * 0.85f;
        case ER_TYPE_SNOWMAN: return 1.7f;
        case ER_TYPE_BAT:      return 0.9f * 0.85f;
        case ER_TYPE_LLAMA:    return 1.87f * 0.85f;
        case ER_TYPE_LLAMA_SPIT: return 0.125f;
        case ER_TYPE_GHAST:    return 4.0f * 0.85f;
        case ER_TYPE_MAGMA:    return 0.51f * 0.85f;  /* size-1 base; caller * size */
        case ER_TYPE_SLIME:    return 0.51f * 0.85f;  /* size-1 base; caller * size */
        case ER_TYPE_SILVERFISH: return 0.3f * 0.85f;
        case ER_TYPE_ENDERMITE: return 0.1f;
        case ER_TYPE_BOAT:     return 0.5625f * 0.85f;
        case ER_TYPE_DRAGON:   return 8.0f * 0.85f;   /* setSize(16, 8) */
        case ER_TYPE_CRYSTAL:  return 2.0f * 0.85f;   /* setSize(2, 2) */
        case ER_TYPE_ARMOR_STAND: return 1.975f * 0.85f;
        case ER_TYPE_SHULKER: return 0.5f;
        case ER_TYPE_SHULKER_BULLET: return 0.0f;
        case ER_TYPE_WITHER: return 3.5f * 0.85f;
        case ER_TYPE_WITHER_SKULL: return 0.15625f;
        case ER_TYPE_HORSE:
        case ER_TYPE_DONKEY:
        case ER_TYPE_MULE:
        case ER_TYPE_SKELETON_HORSE:
        case ER_TYPE_ZOMBIE_HORSE: return 1.6f * 0.85f;
        default:               return 0.5f;
    }
}

/* EntityBlaze.getBrightnessForRender and EntityWither.getBrightnessForRender
 * return 15728880 == (240 << 16) | 240, i.e. the model always samples the
 * lightmap at sky 15 / block 15 no matter how dark the world cell is. Callers
 * that sample world light for an entity apply this exemption first. */
int gm_entity_fullbright(int type) {
    return type == ER_TYPE_BLAZE || type == ER_TYPE_SHULKER_BULLET
        || type == ER_TYPE_VEX || type == ER_TYPE_WITHER;
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
    if (item_id >= 298 && item_id <= 301)
        return layer == 2 ? CR_MOB_LEATHER_LAYER_2
                          : CR_MOB_LEATHER_LAYER_1;
    if (item_id >= 302 && item_id <= 305)
        return layer == 2 ? CR_MOB_CHAINMAIL_LAYER_2
                          : CR_MOB_CHAINMAIL_LAYER_1;
    if (item_id >= 306 && item_id <= 309)
        return layer == 2 ? CR_MOB_IRON_LAYER_2 : CR_MOB_IRON_LAYER_1;
    if (item_id >= 310 && item_id <= 313)
        return layer == 2 ? CR_MOB_DIAMOND_LAYER_2 : CR_MOB_DIAMOND_LAYER_1;
    if (item_id >= 314 && item_id <= 317)
        return layer == 2 ? CR_MOB_GOLD_LAYER_2 : CR_MOB_GOLD_LAYER_1;
    return -1;
}

static int er_armor_overlay_sprite(int item_id, int layer) {
    if (item_id < 298 || item_id > 301) return -1;
    return layer == 2 ? CR_MOB_LEATHER_LAYER_2_OVERLAY
                      : CR_MOB_LEATHER_LAYER_1_OVERLAY;
}

static CrRgba er_armor_tint(
        const GmEntityView *v, int slot, int item_id, CrRgba base) {
    if (item_id < 298 || item_id > 301) return base;
    int color = (v->armor_color_valid & (1 << slot))
        ? v->armor_color[slot] : 0xa06540;
    base.r = (u8)((int)base.r * ((color >> 16) & 255) / 255);
    base.g = (u8)((int)base.g * ((color >> 8) & 255) / 255);
    base.b = (u8)((int)base.b * (color & 255) / 255);
    return base;
}

static int er_armor_stand_box_count(const GmEntityView *v) {
    int n = 0;
    if (er_armor_sprite(v->armor_chest, 1) >= 0) n += 3;
    if (er_armor_sprite(v->armor_legs, 2) >= 0) n += 3;
    if (er_armor_sprite(v->armor_feet, 1) >= 0) n += 2;
    if (er_armor_sprite(v->armor_head, 1) >= 0) n += 2;
    if (er_armor_overlay_sprite(v->armor_chest, 1) >= 0) n += 3;
    if (er_armor_overlay_sprite(v->armor_legs, 2) >= 0) n += 3;
    if (er_armor_overlay_sprite(v->armor_feet, 1) >= 0) n += 2;
    if (er_armor_overlay_sprite(v->armor_head, 1) >= 0) n += 2;
    if (v->armor_chest == 443) n += 2;
    if (v->armor_head == 397 && er_armor_stand_skull_sprite(
            v->armor_head_meta) >= 0)
        n += (v->armor_head_meta == 2 || v->armor_head_meta == 3) ? 2 : 1;
    if (v->armor_head == 397 && v->armor_head_meta == 5) n += 7;
    return n;
}

static void er_armor_stand_pose_part(
        const GmEntityView *v, ErPart *part, int pose_part) {
    if (!v->stand_pose_valid) return;
    part->ax = v->stand_pose[pose_part][0] * ER_DEG2RAD;
    part->ay = v->stand_pose[pose_part][1] * ER_DEG2RAD;
    part->az = v->stand_pose[pose_part][2] * ER_DEG2RAD;
}

static int er_emit_armor_parts(const ErPart *parts, int nparts, int sprite,
                               const GmEntityView *v, const int *pose_parts,
                               float cs, float sn, float sc,
                               float fx, float fy, float fz, CrRgba tint,
                               float lv, float blk, float roll_c, float roll_s,
                               CrVertex *out) {
    int written = 0;
    for (int i = 0; i < nparts; ++i) {
        ErPart part = parts[i];
        part.sprite = sprite;
        er_armor_stand_pose_part(v, &part, pose_parts[i]);
        written += emit_box(&part, cs, sn, sc, fx, fy, fz, tint, lv, blk,
                            roll_c, roll_s, out + written);
    }
    return written;
}

static int er_emit_armor_slot(
        const GmEntityView *v, const ErPart *parts, int nparts,
        const int *pose_parts, int item_id, int slot, int layer,
        float cs, float sn, float sc, float fx, float fy, float fz,
        CrRgba tint, float lv, float blk, float roll_c, float roll_s,
        CrVertex *out) {
    int sprite = er_armor_sprite(item_id, layer);
    int written = 0;
    if (sprite < 0) return 0;
    written += er_emit_armor_parts(
        parts, nparts, sprite, v, pose_parts, cs, sn, sc, fx, fy, fz,
        er_armor_tint(v, slot, item_id, tint), lv, blk, roll_c, roll_s,
        out + written);
    sprite = er_armor_overlay_sprite(item_id, layer);
    if (sprite >= 0)
        written += er_emit_armor_parts(
            parts, nparts, sprite, v, pose_parts, cs, sn, sc, fx, fy, fz,
            tint, lv, blk, roll_c, roll_s, out + written);
    return written;
}

static int er_emit_armor_stand_layers(const GmEntityView *v,
                                      float cs, float sn, float sc,
                                      float fx, float fy, float fz, CrRgba tint,
                                      float lv, float blk,
                                      float roll_c, float roll_s,
                                      CrVertex *out) {
    static const int chest_pose[3] = { 1, 3, 2 };
    static const int legs_pose[3] = { 1, 5, 4 };
    static const int feet_pose[2] = { 5, 4 };
    static const int head_pose[2] = { 0, 0 };
    float body_sc = sc;
    float head_sc = sc;
    float head_fy = fy;
    if (v->stand_flags & 4) {
        body_sc *= 0.5f;
        head_sc *= 0.75f;
        head_fy -= 0.375f * sc;
    }
    int written = er_emit_armor_slot(
        v, ARMOR_CHEST_PARTS, 3, chest_pose, v->armor_chest, 2, 1,
        cs, sn, body_sc, fx, fy, fz, tint, lv, blk, roll_c, roll_s, out);
    written += er_emit_armor_slot(
        v, ARMOR_LEGS_PARTS, 3, legs_pose, v->armor_legs, 1, 2,
        cs, sn, body_sc, fx, fy, fz, tint, lv, blk, roll_c, roll_s,
        out + written);
    written += er_emit_armor_slot(
        v, ARMOR_FEET_PARTS, 2, feet_pose, v->armor_feet, 0, 1,
        cs, sn, body_sc, fx, fy, fz, tint, lv, blk, roll_c, roll_s,
        out + written);
    written += er_emit_armor_slot(
        v, ARMOR_HEAD_PARTS, 2, head_pose, v->armor_head, 3, 1,
        cs, sn, head_sc, fx, head_fy, fz, tint, lv, blk, roll_c, roll_s,
        out + written);
    if (v->armor_chest == 443) {
        for (int i = 0; i < 2; ++i) {
            ErPart wing = ARMOR_STAND_ELYTRA_PARTS[i];
            written += emit_box(
                &wing, cs, sn, body_sc, fx, fy, fz, tint, lv, blk,
                roll_c, roll_s, out + written);
        }
    }
    if (v->armor_head == 397) {
        int sprite = er_armor_stand_skull_sprite(v->armor_head_meta);
        if (sprite >= 0) {
            ErPart skull = {
                sprite, 0,0, -4,-8.75f,-4, 8,8,8, 0,1,0,
                0,0,0, 0.75f,0
            };
            er_armor_stand_pose_part(v, &skull, 0);
            written += emit_box(
                &skull, cs, sn, head_sc, fx, head_fy, fz, tint, lv, blk,
                roll_c, roll_s, out + written);
            if (v->armor_head_meta == 2 || v->armor_head_meta == 3) {
                skull.u = 32;
                skull.delta = 1.0f;
                written += emit_box(
                    &skull, cs, sn, head_sc, fx, head_fy, fz, tint, lv, blk,
                    roll_c, roll_s, out + written);
            }
        } else if (v->armor_head_meta == 5) {
            written += er_emit_armor_stand_dragon_head(
                v, cs, sn, head_sc, fx, head_fy, fz,
                tint, lv, blk, roll_c, roll_s, out + written);
        }
    }
    return written;
}

/* LayerBipedArmor HEAD slot for the bow skeleton used by a skeleton trap.
 * ModelBiped's armor head/headwear share the base head pivot and pose; only
 * inflation and the armor atlas sprite differ. */
static int er_biped_head_armor_emit(const GmEntityView *v,
                                    const ErPart *head,
                                    float cs, float sn, float sc,
                                    float fx, float fy, float fz, CrRgba tint,
                                    float lv, float blk,
                                    float roll_c, float roll_s,
                                    CrVertex *out) {
    int sprite = er_armor_sprite(v->armor_head, 1);
    if (sprite < 0) return 0;
    int written = 0;
    ErPart layer = *head;
    layer.sprite = sprite;
    layer.u = 0;
    layer.v = 0;
    layer.delta = 1.0f;
    written += emit_box(
        &layer, cs, sn, sc, fx, fy, fz,
        er_armor_tint(v, 3, v->armor_head, tint), lv, blk,
        roll_c, roll_s, out + written);
    layer.u = 32;
    layer.delta = 1.5f;
    written += emit_box(
        &layer, cs, sn, sc, fx, fy, fz,
        er_armor_tint(v, 3, v->armor_head, tint), lv, blk,
        roll_c, roll_s, out + written);
    sprite = er_armor_overlay_sprite(v->armor_head, 1);
    if (sprite >= 0) {
        layer.sprite = sprite;
        layer.u = 0;
        layer.delta = 1.0f;
        written += emit_box(
            &layer, cs, sn, sc, fx, fy, fz, tint, lv, blk,
            roll_c, roll_s, out + written);
        layer.u = 32;
        layer.delta = 1.5f;
        written += emit_box(
            &layer, cs, sn, sc, fx, fy, fz, tint, lv, blk,
            roll_c, roll_s, out + written);
    }
    return written;
}

static void er_wither_pose(const GmEntityView *v, ErPart parts[9],
                           int sprite, float delta) {
    memcpy(parts, M_WITHER.parts, 9 * sizeof *parts);
    for (int i = 0; i < 9; ++i) {
        parts[i].sprite = sprite;
        parts[i].delta = delta;
    }
    float age = (float)v->ticks_existed + 1.0f;
    float wave = er_mathhelper_cos(age * 0.1f);
    float bend1 = (0.065f + 0.05f * wave) * ER_PI;
    float bend2 = (0.265f + 0.1f * wave) * ER_PI;
    parts[0].ay = (v->head_yaw - v->yaw) * ER_DEG2RAD;
    parts[0].ax = v->pitch * ER_DEG2RAD;
    for (int side = 0; side < 2; ++side) {
        parts[side + 1].ay =
            (v->wither_head_yaw[side] - v->yaw) * ER_DEG2RAD;
        parts[side + 1].ax = v->wither_head_pitch[side] * ER_DEG2RAD;
    }
    for (int i = 4; i <= 7; ++i) parts[i].ax = bend1;
    parts[8].rx = -2.0f;
    parts[8].ry = 6.9f + er_mathhelper_cos(bend1) * 10.0f;
    parts[8].rz = -0.5f + er_mathhelper_sin(bend1) * 10.0f;
    parts[8].ax = bend2;
}

static float er_wither_scale(const GmEntityView *v) {
    float scale = 2.0f;
    if (v->wither_invul_time > 0)
        scale -= ((float)v->wither_invul_time - 1.0f) / 220.0f * 0.5f;
    return scale;
}

/* RenderShulker.applyRotations, applied after the ordinary feet-space model
 * emitter. EntityShulker hard-pins body yaw to 180, so the superclass Y
 * rotation is identity and these six transforms are direct world mappings. */
static void er_shulker_face_transform(
        int face, float ox, float oy, float oz,
        CrVertex *verts, int n) {
    for (int i = 0; i < n; ++i) {
        float x = verts[i].pos.x - ox;
        float y = verts[i].pos.y - oy;
        float z = verts[i].pos.z - oz;
        float nx = x, ny = y, nz = z;
        switch (face) {
        case 1: nx = x;  ny = 1.0f - y; nz = -z; break;
        case 2: nx = x;  ny = 0.5f - z; nz = y - 0.5f; break;
        case 3: nx = -x; ny = 0.5f - z; nz = 0.5f - y; break;
        case 4: nx = y - 0.5f; ny = 0.5f - z; nz = -x; break;
        case 5: nx = 0.5f - y; ny = 0.5f - z; nz = x; break;
        default: break;
        }
        verts[i].pos.x = ox + nx;
        verts[i].pos.y = oy + ny;
        verts[i].pos.z = oz + nz;
    }
}

static int er_is_horse(int type) {
    return type >= ER_TYPE_HORSE && type <= ER_TYPE_ZOMBIE_HORSE;
}

static int er_horse_sprite(const GmEntityView *v) {
    if (v->type == ER_TYPE_DONKEY) return CR_MOB_DONKEY;
    if (v->type == ER_TYPE_MULE) return CR_MOB_MULE;
    if (v->type == ER_TYPE_SKELETON_HORSE) return CR_MOB_HORSE_SKELETON;
    if (v->type == ER_TYPE_ZOMBIE_HORSE) return CR_MOB_HORSE_ZOMBIE;
    int color = (v->item_id & 255) % 7;
    int marking = ((v->item_id & 65280) >> 8) % 5;
    int armor = v->item_meta;
    if (armor < 0 || armor > 3) armor = 0;
    return CR_MOB_HORSE_0_0_0 + (color * 5 + marking) * 4 + armor;
}

static int er_horse_box_count(const GmEntityView *v) {
    int child = (v->flags & 8) != 0;
    int boxes = 23; /* legs 12 + torso 6 + ears/head/mouth 5 */
    if (!child && (v->flags & 16384))
        boxes += 10 + ((v->flags & 32768) ? 2 : 0);
    if (!child && (v->flags & 8192)
            && (v->type == ER_TYPE_DONKEY || v->type == ER_TYPE_MULE))
        boxes += 2;
    return boxes;
}

static void er_horse_pose(const GmEntityView *v, ErPart p[HP_COUNT]) {
    memcpy(p, M_HORSE_PARTS, sizeof M_HORSE_PARTS);
    float f3 = v->head_yaw - v->yaw;
    float f4 = v->pitch * ER_DEG2RAD;
    if (f3 > 20.0f) f3 = 20.0f;
    if (f3 < -20.0f) f3 = -20.0f;
    if (v->limb_swing_amount > 0.2f)
        f4 += er_mathhelper_cos(v->limb_swing * 0.4f)
            * 0.15f * v->limb_swing_amount;
    float eat = v->graze_y;
    float rear = v->swing_progress;
    float mouth = v->squish;
    if (eat < 0.0f) eat = 0.0f; else if (eat > 1.0f) eat = 1.0f;
    if (rear < 0.0f) rear = 0.0f; else if (rear > 1.0f) rear = 1.0f;
    if (mouth < 0.0f) mouth = 0.0f; else if (mouth > 1.0f) mouth = 1.0f;
    float stand = 1.0f - rear;
    float age = (float)v->ticks_existed + 1.0f;
    float gait = er_mathhelper_cos(v->limb_swing * 0.6662f + ER_PI);
    float stride = gait * 0.8f * v->limb_swing_amount;

    p[HP_HEAD].ry = 4.0f;
    p[HP_HEAD].rz = -10.0f;
    p[HP_TAIL_BASE].ry = 3.0f;
    p[HP_TAIL_MIDDLE].rz = 14.0f;
    p[HP_MULE_RIGHT_CHEST].ry = 3.0f;
    p[HP_MULE_RIGHT_CHEST].rz = 10.0f;
    p[HP_BODY].ax = 0.0f;
    p[HP_HEAD].ax = 0.5235988f + f4;
    p[HP_HEAD].ay = f3 * ER_DEG2RAD;
    {
        float active = fmaxf(rear, eat);
        p[HP_HEAD].ax = rear * (0.2617994f + f4)
            + eat * 2.1816616f + (1.0f - active) * p[HP_HEAD].ax;
        p[HP_HEAD].ay = rear * f3 * ER_DEG2RAD
            + (1.0f - active) * p[HP_HEAD].ay;
        p[HP_HEAD].ry = rear * -6.0f + eat * 11.0f
            + (1.0f - active) * p[HP_HEAD].ry;
        p[HP_HEAD].rz = rear * -1.0f + eat * -10.0f
            + (1.0f - active) * p[HP_HEAD].rz;
    }
    p[HP_TAIL_BASE].ry = rear * 9.0f + stand * p[HP_TAIL_BASE].ry;
    p[HP_TAIL_MIDDLE].rz = rear * 18.0f + stand * p[HP_TAIL_MIDDLE].rz;
    p[HP_MULE_RIGHT_CHEST].ry = rear * 5.5f
        + stand * p[HP_MULE_RIGHT_CHEST].ry;
    p[HP_MULE_RIGHT_CHEST].rz = rear * 15.0f
        + stand * p[HP_MULE_RIGHT_CHEST].rz;
    p[HP_BODY].ax = rear * -ER_PI / 4.0f;

    static const int head_parts[] = {
        HP_HORSE_LEFT_EAR, HP_HORSE_RIGHT_EAR,
        HP_MULE_LEFT_EAR, HP_MULE_RIGHT_EAR,
        HP_NECK, HP_MANE,
    };
    for (unsigned i = 0; i < sizeof head_parts / sizeof head_parts[0]; ++i) {
        ErPart *part = &p[head_parts[i]];
        part->ry = p[HP_HEAD].ry; part->rz = p[HP_HEAD].rz;
        part->ax = p[HP_HEAD].ax; part->ay = p[HP_HEAD].ay;
    }
    p[HP_UPPER_MOUTH].ry = 0.02f;
    p[HP_LOWER_MOUTH].ry = 0.0f;
    p[HP_UPPER_MOUTH].rz = 0.02f - mouth;
    p[HP_LOWER_MOUTH].rz = mouth;
    p[HP_UPPER_MOUTH].ax = -0.09424778f * mouth;
    p[HP_LOWER_MOUTH].ax = 0.15707964f * mouth;
    p[HP_UPPER_MOUTH].ay = p[HP_LOWER_MOUTH].ay = 0.0f;
    p[HP_MULE_LEFT_CHEST].ax = stride / 5.0f;
    p[HP_MULE_RIGHT_CHEST].ax = -stride / 5.0f;

    float front_stand = 0.2617994f * rear;
    float rear_wave = er_mathhelper_cos(age * 0.6f + ER_PI);
    p[HP_FRONT_LEFT_LEG].ry = -2.0f * rear + 9.0f * stand;
    p[HP_FRONT_LEFT_LEG].rz = -2.0f * rear - 8.0f * stand;
    p[HP_FRONT_RIGHT_LEG].ry = p[HP_FRONT_LEFT_LEG].ry;
    p[HP_FRONT_RIGHT_LEG].rz = p[HP_FRONT_LEFT_LEG].rz;
    p[HP_BACK_LEFT_SHIN].ry = p[HP_BACK_LEFT_LEG].ry
        + er_mathhelper_sin(ER_PI / 2.0f + front_stand
            + stand * -gait * 0.5f * v->limb_swing_amount) * 7.0f;
    p[HP_BACK_LEFT_SHIN].rz = p[HP_BACK_LEFT_LEG].rz
        + er_mathhelper_cos(-ER_PI / 2.0f + front_stand
            + stand * -gait * 0.5f * v->limb_swing_amount) * 7.0f;
    p[HP_BACK_RIGHT_SHIN].ry = p[HP_BACK_RIGHT_LEG].ry
        + er_mathhelper_sin(ER_PI / 2.0f + front_stand
            + stand * gait * 0.5f * v->limb_swing_amount) * 7.0f;
    p[HP_BACK_RIGHT_SHIN].rz = p[HP_BACK_RIGHT_LEG].rz
        + er_mathhelper_cos(-ER_PI / 2.0f + front_stand
            + stand * gait * 0.5f * v->limb_swing_amount) * 7.0f;
    float front_left = (-1.0471976f + rear_wave) * rear + stride * stand;
    float front_right = (-1.0471976f - rear_wave) * rear - stride * stand;
    p[HP_FRONT_LEFT_SHIN].ry = p[HP_FRONT_LEFT_LEG].ry
        + er_mathhelper_sin(ER_PI / 2.0f + front_left) * 7.0f;
    p[HP_FRONT_LEFT_SHIN].rz = p[HP_FRONT_LEFT_LEG].rz
        + er_mathhelper_cos(-ER_PI / 2.0f + front_left) * 7.0f;
    p[HP_FRONT_RIGHT_SHIN].ry = p[HP_FRONT_RIGHT_LEG].ry
        + er_mathhelper_sin(ER_PI / 2.0f + front_right) * 7.0f;
    p[HP_FRONT_RIGHT_SHIN].rz = p[HP_FRONT_RIGHT_LEG].rz
        + er_mathhelper_cos(-ER_PI / 2.0f + front_right) * 7.0f;

    p[HP_BACK_LEFT_LEG].ax = front_stand
        - gait * 0.5f * v->limb_swing_amount * stand;
    p[HP_BACK_LEFT_SHIN].ax = -0.08726646f * rear
        + (-gait * 0.5f * v->limb_swing_amount
            - fmaxf(0.0f, gait * 0.5f * v->limb_swing_amount)) * stand;
    p[HP_BACK_LEFT_HOOF].ax = p[HP_BACK_LEFT_SHIN].ax;
    p[HP_BACK_RIGHT_LEG].ax = front_stand
        + gait * 0.5f * v->limb_swing_amount * stand;
    p[HP_BACK_RIGHT_SHIN].ax = -0.08726646f * rear
        + (gait * 0.5f * v->limb_swing_amount
            - fmaxf(0.0f, -gait * 0.5f * v->limb_swing_amount)) * stand;
    p[HP_BACK_RIGHT_HOOF].ax = p[HP_BACK_RIGHT_SHIN].ax;
    p[HP_FRONT_LEFT_LEG].ax = front_left;
    p[HP_FRONT_LEFT_SHIN].ax = (front_left
        + ER_PI * fmaxf(0.0f, 0.2f + rear_wave * 0.2f)) * rear
        + (stride + fmaxf(0.0f, gait * 0.5f * v->limb_swing_amount)) * stand;
    p[HP_FRONT_LEFT_HOOF].ax = p[HP_FRONT_LEFT_SHIN].ax;
    p[HP_FRONT_RIGHT_LEG].ax = front_right;
    p[HP_FRONT_RIGHT_SHIN].ax = (front_right
        + ER_PI * fmaxf(0.0f, 0.2f - rear_wave * 0.2f)) * rear
        + (-stride + fmaxf(0.0f, -gait * 0.5f * v->limb_swing_amount)) * stand;
    p[HP_FRONT_RIGHT_HOOF].ax = p[HP_FRONT_RIGHT_SHIN].ax;

    static const int shin_hoof[][2] = {
        {HP_BACK_LEFT_SHIN, HP_BACK_LEFT_HOOF},
        {HP_BACK_RIGHT_SHIN, HP_BACK_RIGHT_HOOF},
        {HP_FRONT_LEFT_SHIN, HP_FRONT_LEFT_HOOF},
        {HP_FRONT_RIGHT_SHIN, HP_FRONT_RIGHT_HOOF},
    };
    for (unsigned i = 0; i < sizeof shin_hoof / sizeof shin_hoof[0]; ++i) {
        p[shin_hoof[i][1]].ry = p[shin_hoof[i][0]].ry;
        p[shin_hoof[i][1]].rz = p[shin_hoof[i][0]].rz;
    }

    if (v->flags & 16384) {
        p[HP_SADDLE_BOTTOM].ry = rear * 0.5f + stand * 2.0f;
        p[HP_SADDLE_BOTTOM].rz = rear * 11.0f + stand * 2.0f;
        static const int saddle_parts[] = {
            HP_SADDLE_FRONT, HP_SADDLE_BACK,
            HP_LEFT_SADDLE_ROPE, HP_RIGHT_SADDLE_ROPE,
            HP_LEFT_SADDLE_METAL, HP_RIGHT_SADDLE_METAL,
        };
        for (unsigned i = 0; i < sizeof saddle_parts / sizeof saddle_parts[0]; ++i) {
            p[saddle_parts[i]].ry = p[HP_SADDLE_BOTTOM].ry;
            p[saddle_parts[i]].rz = p[HP_SADDLE_BOTTOM].rz;
        }
        p[HP_MULE_LEFT_CHEST].ry = p[HP_MULE_RIGHT_CHEST].ry;
        p[HP_MULE_LEFT_CHEST].rz = p[HP_MULE_RIGHT_CHEST].rz;
        p[HP_SADDLE_BOTTOM].ax = p[HP_BODY].ax;
        p[HP_SADDLE_FRONT].ax = p[HP_BODY].ax;
        p[HP_SADDLE_BACK].ax = p[HP_BODY].ax;
        static const int face_parts[] = {
            HP_LEFT_REIN, HP_RIGHT_REIN, HP_FACE_ROPES,
            HP_LEFT_FACE_METAL, HP_RIGHT_FACE_METAL,
        };
        for (unsigned i = 0; i < sizeof face_parts / sizeof face_parts[0]; ++i) {
            p[face_parts[i]].ry = p[HP_HEAD].ry;
            p[face_parts[i]].rz = p[HP_HEAD].rz;
        }
        p[HP_LEFT_REIN].ax = p[HP_RIGHT_REIN].ax = f4;
        p[HP_FACE_ROPES].ax = p[HP_LEFT_FACE_METAL].ax
            = p[HP_RIGHT_FACE_METAL].ax = p[HP_HEAD].ax;
        p[HP_FACE_ROPES].ay = p[HP_LEFT_FACE_METAL].ay
            = p[HP_RIGHT_FACE_METAL].ay = p[HP_HEAD].ay;
        p[HP_LEFT_REIN].ay = p[HP_RIGHT_REIN].ay = p[HP_HEAD].ay;
        int chest_horse = v->type == ER_TYPE_DONKEY || v->type == ER_TYPE_MULE;
        if (chest_horse) {
            p[HP_LEFT_SADDLE_ROPE].ax = p[HP_LEFT_SADDLE_METAL].ax
                = p[HP_RIGHT_SADDLE_ROPE].ax
                = p[HP_RIGHT_SADDLE_METAL].ax = -1.0471976f;
            p[HP_LEFT_SADDLE_ROPE].az = p[HP_LEFT_SADDLE_METAL].az
                = p[HP_RIGHT_SADDLE_ROPE].az
                = p[HP_RIGHT_SADDLE_METAL].az = 0.0f;
        } else {
            p[HP_LEFT_SADDLE_ROPE].ax = p[HP_LEFT_SADDLE_METAL].ax
                = p[HP_RIGHT_SADDLE_ROPE].ax
                = p[HP_RIGHT_SADDLE_METAL].ax = stride / 3.0f;
            p[HP_LEFT_SADDLE_ROPE].az = p[HP_LEFT_SADDLE_METAL].az
                = stride / 5.0f;
            p[HP_RIGHT_SADDLE_ROPE].az = p[HP_RIGHT_SADDLE_METAL].az
                = -stride / 5.0f;
        }
    }

    float tail_x = -1.3089969f + v->limb_swing_amount * 1.5f;
    if (tail_x > 0.0f) tail_x = 0.0f;
    if (v->flags & 65536) {
        p[HP_TAIL_BASE].ay = er_mathhelper_cos(age * 0.7f);
        tail_x = 0.0f;
    } else {
        p[HP_TAIL_BASE].ay = 0.0f;
    }
    p[HP_TAIL_MIDDLE].ay = p[HP_TAIL_TIP].ay = p[HP_TAIL_BASE].ay;
    p[HP_TAIL_MIDDLE].ry = p[HP_TAIL_TIP].ry = p[HP_TAIL_BASE].ry;
    p[HP_TAIL_TIP].rz = p[HP_TAIL_MIDDLE].rz = p[HP_TAIL_BASE].rz;
    p[HP_TAIL_BASE].ax = p[HP_TAIL_MIDDLE].ax = tail_x;
    p[HP_TAIL_TIP].ax = -0.2617994f + tail_x;
}

static ErAff er_horse_part_affine(const ErAff *base, const ErPart *p) {
    ErAff part = *base;
    er_aff_translate(&part, p->rx * 0.0625f, p->ry * 0.0625f,
                     p->rz * 0.0625f);
    if (p->az != 0.0f) er_aff_rot_z(&part, p->az * ER_RAD2DEG);
    if (p->ay != 0.0f) er_aff_rot_y(&part, p->ay * ER_RAD2DEG);
    if (p->ax != 0.0f) er_aff_rot_x(&part, p->ax * ER_RAD2DEG);
    return part;
}

static int er_horse_emit_part(const ErAff *base, const ErPart *p, int sprite,
                              CrRgba tint, float lv, float blk,
                              CrVertex *out) {
    ErAff part = er_horse_part_affine(base, p);
    if (p->delta > 0.0f && p->dx > 0 && p->dy > 0 && p->dz > 0) {
        float cx = p->x + p->dx * 0.5f;
        float cy = p->y + p->dy * 0.5f;
        float cz = p->z + p->dz * 0.5f;
        er_aff_translate(&part, cx * 0.0625f, cy * 0.0625f,
                         cz * 0.0625f);
        er_aff_scale3(&part,
            1.0f + 2.0f * p->delta / p->dx,
            1.0f + 2.0f * p->delta / p->dy,
            1.0f + 2.0f * p->delta / p->dz);
        er_aff_translate(&part, -cx * 0.0625f, -cy * 0.0625f,
                         -cz * 0.0625f);
    }
    return er_aff_box_m(&part, sprite, 1, p->mirror, 1, p->u, p->v,
                        p->x, p->y, p->z, p->dx, p->dy, p->dz,
                        tint, lv, blk, out);
}

static int er_bat_emit_box(const ErAff *part, const ErPart *box,
                           CrRgba tint, float lv, float blk,
                           CrVertex *out) {
    return er_aff_box_m(part, box->sprite, 1, box->mirror, 1,
                        box->u, box->v,
                        box->x, box->y, box->z,
                        box->dx, box->dy, box->dz,
                        tint, lv, blk, out);
}

/* ModelBat keeps one subtle vanilla state leak: the flying branch does not
 * reset the inner wings' X rotation after any hanging Bat has rendered. The
 * Java RenderBat owns one shared ModelBat, so retain that value across calls
 * and across Bats in render order. */
static float g_bat_wing_x;

static int emit_bat(const GmEntityView *v, CrVertex *out) {
    const ErPart *p = M_BAT.parts;
    const float java_rad_to_deg = 57.295776f;
    int hanging = (v->flags & GM_ENTITY_FLAG_BAT_HANGING) != 0;
    float age = (float)v->ticks_existed + 1.0f;
    float net_head_yaw = (v->head_yaw - v->yaw) * ER_DEG2RAD;
    float head_pitch = v->pitch * ER_DEG2RAD;
    float body_x;
    float right_y;
    float outer_right_y;
    float bob;
    if (hanging) {
        body_x = ER_PI;
        g_bat_wing_x = -0.15707964f;
        right_y = -1.2566371f;
        outer_right_y = -1.7278761f;
        bob = -0.1f;
    } else {
        body_x = ER_PI / 4.0f + er_mathhelper_cos(age * 0.1f) * 0.15f;
        right_y = er_mathhelper_cos(age * 1.3f) * ER_PI * 0.25f;
        outer_right_y = right_y * 0.5f;
        bob = er_mathhelper_cos(age * 0.3f) * 0.1f;
    }

    CrRgba tint = {255,255,255,255};
    if (v->hurt_time > 0 || v->death_time > 0)
        tint = (CrRgba){255,178,178,255};
    float lv = 15.0f, blk = 0.0f;
    if (v->lm_lit == 1) {
        lv = v->lm_light;
        blk = v->lm_blk;
    } else if (v->lm_lit == 2) {
        lv = 1.0f;
        tint.r = (u8)(tint.r * v->lm_mul_r + 0.5f);
        tint.g = (u8)(tint.g * v->lm_mul_g + 0.5f);
        tint.b = (u8)(tint.b * v->lm_mul_b + 0.5f);
    }

    /* RenderLivingBase GL chain:
     * T(position) T(RenderBat bob) Ry(180-yaw) Rz(death)
     * S(-1,-1,1) S(.35) T(0,-1.501,0). */
    ErAff base;
    er_aff_identity(&base);
    er_aff_translate(&base, v->x, v->y, v->z);
    er_aff_translate(&base, 0.0f, bob, 0.0f);
    er_aff_rot_y(&base, 180.0f - v->yaw);
    float death = er_death_roll(v);
    if (death != 0.0f) er_aff_rot_z(&base, death * ER_RAD2DEG);
    er_aff_scale3(&base, -1.0f, -1.0f, 1.0f);
    er_aff_scale(&base, 0.35f);
    er_aff_translate(&base, 0.0f, -1.501f, 0.0f);

    int written = 0;
    ErAff head = base;
    if (hanging) er_aff_translate(&head, 0.0f, -2.0f/16.0f, 0.0f);
    if (hanging) er_aff_rot_z(&head, 180.0f);
    er_aff_rot_y(&head,
        (hanging ? ER_PI - net_head_yaw : net_head_yaw) * java_rad_to_deg);
    er_aff_rot_x(&head, head_pitch * java_rad_to_deg);
    for (int i = 0; i < 3; ++i)
        written += er_bat_emit_box(
            &head, &p[i], tint, lv, blk, out + written);

    ErAff body = base;
    er_aff_rot_x(&body, body_x * java_rad_to_deg);
    written += er_bat_emit_box(&body, &p[3], tint, lv, blk, out + written);
    written += er_bat_emit_box(&body, &p[4], tint, lv, blk, out + written);

    ErAff right = body;
    if (hanging)
        er_aff_translate(&right, -3.0f/16.0f, 0.0f, 3.0f/16.0f);
    er_aff_rot_y(&right, right_y * java_rad_to_deg);
    if (g_bat_wing_x != 0.0f)
        er_aff_rot_x(&right, g_bat_wing_x * java_rad_to_deg);
    written += er_bat_emit_box(&right, &p[5], tint, lv, blk, out + written);
    ErAff outer_right = right;
    er_aff_translate(
        &outer_right, -12.0f/16.0f, 1.0f/16.0f, 1.5f/16.0f);
    er_aff_rot_y(&outer_right, outer_right_y * java_rad_to_deg);
    written += er_bat_emit_box(
        &outer_right, &p[6], tint, lv, blk, out + written);

    ErAff left = body;
    if (hanging)
        er_aff_translate(&left, 3.0f/16.0f, 0.0f, 3.0f/16.0f);
    er_aff_rot_y(&left, -right_y * java_rad_to_deg);
    if (g_bat_wing_x != 0.0f)
        er_aff_rot_x(&left, g_bat_wing_x * java_rad_to_deg);
    written += er_bat_emit_box(&left, &p[7], tint, lv, blk, out + written);
    ErAff outer_left = left;
    er_aff_translate(
        &outer_left, 12.0f/16.0f, 1.0f/16.0f, 1.5f/16.0f);
    er_aff_rot_y(&outer_left, -outer_right_y * java_rad_to_deg);
    written += er_bat_emit_box(
        &outer_left, &p[8], tint, lv, blk, out + written);
    return written;
}

static int emit_squid(const GmEntityView *v, CrVertex *out) {
    if (!g_squid_init) squid_build();
    CrRgba tint = {255,255,255,255};
    if (v->hurt_time > 0 || v->death_time > 0)
        tint = (CrRgba){255,178,178,255};
    float lv = 15.0f, blk = 0.0f;
    if (v->lm_lit == 1) {
        lv = v->lm_light;
        blk = v->lm_blk;
    } else if (v->lm_lit == 2) {
        lv = 1.0f;
        tint.r = (u8)(tint.r * v->lm_mul_r + 0.5f);
        tint.g = (u8)(tint.g * v->lm_mul_g + 0.5f);
        tint.b = (u8)(tint.b * v->lm_mul_b + 0.5f);
    }

    /* RenderSquid.applyRotations followed by RenderLivingBase.prepareScale:
     * T(position) T(0,.5,0) Ry(180-renderYawOffset) Rx(squidPitch)
     * Ry(squidYaw) T(0,-1.2,0) S(-1,-1,1) T(0,-1.501,0). */
    ErAff base;
    er_aff_identity(&base);
    er_aff_translate(&base, v->x, v->y, v->z);
    er_aff_translate(&base, 0.0f, 0.5f, 0.0f);
    er_aff_rot_y(&base, 180.0f - v->yaw);
    er_aff_rot_x(&base, v->pitch);
    er_aff_rot_y(&base, v->head_yaw);
    er_aff_translate(&base, 0.0f, -1.2f, 0.0f);
    er_aff_scale3(&base, -1.0f, -1.0f, 1.0f);
    er_aff_translate(&base, 0.0f, -1.501f, 0.0f);

    int written = 0;
    for (int i = 0; i < g_squid.nparts; ++i) {
        ErPart part = g_squid.parts[i];
        if (i > 0) part.ax = v->anim_time;
        ErAff affine = base;
        er_aff_translate(&affine, part.rx * 0.0625f,
                         part.ry * 0.0625f, part.rz * 0.0625f);
        if (part.az != 0.0f) er_aff_rot_z(&affine, part.az * ER_RAD2DEG);
        if (part.ay != 0.0f) er_aff_rot_y(&affine, part.ay * ER_RAD2DEG);
        if (part.ax != 0.0f) er_aff_rot_x(&affine, part.ax * ER_RAD2DEG);
        written += er_aff_box_m(&affine, part.sprite, 1, part.mirror, 1,
                                part.u, part.v,
                                part.x, part.y, part.z,
                                part.dx, part.dy, part.dz,
                                tint, lv, blk, out + written);
    }
    return written;
}

static int emit_horse(const GmEntityView *v, CrVertex *out) {
    ErPart p[HP_COUNT];
    er_horse_pose(v, p);
    int sprite = er_horse_sprite(v);
    CrRgba tint = {255,255,255,255};
    if (v->hurt_time > 0 || v->death_time > 0)
        tint = (CrRgba){255,178,178,255};
    float lv = 15.0f, blk = 0.0f;
    if (v->lm_lit == 1) { lv = v->lm_light; blk = v->lm_blk; }
    else if (v->lm_lit == 2) {
        lv = 1.0f;
        tint.r = (u8)(tint.r * v->lm_mul_r + 0.5f);
        tint.g = (u8)(tint.g * v->lm_mul_g + 0.5f);
        tint.b = (u8)(tint.b * v->lm_mul_b + 0.5f);
    }
    ErAff base;
    er_aff_identity(&base);
    er_aff_translate(&base, v->x, v->y, v->z);
    er_aff_rot_y(&base, 180.0f - v->yaw);
    float death = er_death_roll(v);
    if (death != 0.0f) er_aff_rot_z(&base, death * ER_RAD2DEG);
    er_aff_scale3(&base, -1.0f, -1.0f, 1.0f);
    /* RenderManager registers the two chest-horse renderers with their
     * vanilla preRenderCallback scales.  Skeleton/zombie horses and ordinary
     * horses use the default 1.0 scale. */
    if (v->type == ER_TYPE_DONKEY) er_aff_scale(&base, 0.87f);
    else if (v->type == ER_TYPE_MULE) er_aff_scale(&base, 0.92f);
    er_aff_translate(&base, 0.0f, -1.501f, 0.0f);
    int written = 0;
    int child = (v->flags & 8) != 0;
#define HROOT(I,B) do { written += er_horse_emit_part( \
        &(B), &p[(I)], sprite, tint, lv, blk, out + written); } while (0)
    if (!child && (v->flags & 16384)) {
        static const int saddle[] = {
            HP_FACE_ROPES, HP_SADDLE_BOTTOM, HP_SADDLE_FRONT, HP_SADDLE_BACK,
            HP_LEFT_SADDLE_ROPE, HP_LEFT_SADDLE_METAL,
            HP_RIGHT_SADDLE_ROPE, HP_RIGHT_SADDLE_METAL,
            HP_LEFT_FACE_METAL, HP_RIGHT_FACE_METAL,
        };
        for (unsigned i = 0; i < sizeof saddle / sizeof saddle[0]; ++i)
            HROOT(saddle[i], base);
        if (v->flags & 32768) {
            HROOT(HP_LEFT_REIN, base);
            HROOT(HP_RIGHT_REIN, base);
        }
    }
    ErAff legs = base;
    if (child) {
        er_aff_scale3(&legs, 0.5f, 0.75f, 0.5f);
        er_aff_translate(&legs, 0.0f, 0.475f, 0.0f);
    }
    for (int i = HP_BACK_LEFT_LEG; i <= HP_FRONT_RIGHT_HOOF; ++i)
        HROOT(i, legs);
    ErAff torso = base;
    if (child) {
        er_aff_scale(&torso, 0.5f);
        er_aff_translate(&torso, 0.0f, 0.675f, 0.0f);
    }
    HROOT(HP_BODY, torso); HROOT(HP_TAIL_BASE, torso);
    HROOT(HP_TAIL_MIDDLE, torso); HROOT(HP_TAIL_TIP, torso);
    HROOT(HP_NECK, torso); HROOT(HP_MANE, torso);
    ErAff head_base = base;
    if (child) {
        float eat = v->graze_y;
        if (eat < 0.0f) eat = 0.0f; else if (eat > 1.0f) eat = 1.0f;
        er_aff_scale(&head_base, 0.625f);
        er_aff_translate(&head_base, 0.0f,
            0.45f * eat + 0.675f * (1.0f - eat), 0.075f * eat);
    }
    int chest_horse = v->type == ER_TYPE_DONKEY || v->type == ER_TYPE_MULE;
    HROOT(chest_horse ? HP_MULE_LEFT_EAR : HP_HORSE_LEFT_EAR, head_base);
    HROOT(chest_horse ? HP_MULE_RIGHT_EAR : HP_HORSE_RIGHT_EAR, head_base);
    {
        ErAff head = er_horse_part_affine(&head_base, &p[HP_HEAD]);
        written += er_aff_box_m(&head, sprite, 1, p[HP_HEAD].mirror, 1,
            p[HP_HEAD].u, p[HP_HEAD].v,
            p[HP_HEAD].x, p[HP_HEAD].y, p[HP_HEAD].z,
            p[HP_HEAD].dx, p[HP_HEAD].dy, p[HP_HEAD].dz,
            tint, lv, blk, out + written);
        HROOT(HP_UPPER_MOUTH, head);
        HROOT(HP_LOWER_MOUTH, head);
    }
    if (!child && chest_horse && (v->flags & 8192)) {
        HROOT(HP_MULE_LEFT_CHEST, base);
        HROOT(HP_MULE_RIGHT_CHEST, base);
    }
#undef HROOT
    return written;
}

static int er_llama_coat_sprite(const GmEntityView *v) {
    static const int coats[4] = {
        CR_MOB_LLAMA, CR_MOB_LLAMA_WHITE,
        CR_MOB_LLAMA_BROWN, CR_MOB_LLAMA_GRAY
    };
    int variant = v->item_id;
    if (variant < 0 || variant > 3) variant = 0;
    return coats[variant];
}

static int er_llama_box_count(const GmEntityView *v) {
    int child = (v->flags & 8) != 0;
    int chested = !child && ((v->flags & 8192) || v->item_count != 0);
    int decor = v->item_meta >= 1 && v->item_meta <= 16;
    int boxes = 9 + (chested ? 2 : 0);
    if (decor) boxes += 9 + (chested ? 2 : 0);
    return boxes;
}

static int er_llama_emit_model(
        const ErAff *base, const ErPart p[11], int sprite,
        int child, int chested, CrRgba tint, float lv, float blk,
        CrVertex *out) {
    int written = 0;
#define LROOT(I,B) do { written += er_horse_emit_part( \
        &(B), &p[(I)], sprite, tint, lv, blk, out + written); } while (0)
    if (child) {
        ErAff head = *base;
        er_aff_scale3(&head, 0.71428573f, 0.64935064f, 0.7936508f);
        er_aff_translate(&head, 0.0f, 21.0f * 0.0625f, 0.22f);
        for (int i = 0; i < 4; ++i) LROOT(i, head);

        ErAff body = *base;
        er_aff_scale3(&body, 0.625f, 0.45454544f, 0.45454544f);
        er_aff_translate(&body, 0.0f, 33.0f * 0.0625f, 0.0f);
        LROOT(4, body);

        ErAff legs = *base;
        er_aff_scale3(&legs, 0.45454544f, 0.41322312f, 0.45454544f);
        er_aff_translate(&legs, 0.0f, 33.0f * 0.0625f, 0.0f);
        for (int i = 5; i < 9; ++i) LROOT(i, legs);
    } else {
        for (int i = 0; i < 9; ++i) LROOT(i, *base);
        if (chested) {
            LROOT(9, *base);
            LROOT(10, *base);
        }
    }
#undef LROOT
    return written;
}

static int emit_llama(const GmEntityView *v, CrVertex *out) {
    ErPart p[11];
    memcpy(p, M_LLAMA.parts, sizeof p);
    float head_yaw = (v->head_yaw - v->yaw) * ER_DEG2RAD;
    float head_pitch = v->pitch * ER_DEG2RAD;
    for (int i = 0; i < 4; ++i) {
        p[i].ay = head_yaw;
        p[i].ax = head_pitch;
    }
    float gait_a = er_mathhelper_cos(v->limb_swing * 0.6662f)
        * 1.4f * v->limb_swing_amount;
    float gait_b = er_mathhelper_cos(v->limb_swing * 0.6662f + ER_PI)
        * 1.4f * v->limb_swing_amount;
    p[5].ax = p[8].ax = gait_a;
    p[6].ax = p[7].ax = gait_b;

    CrRgba tint = {255,255,255,255};
    if (v->hurt_time > 0 || v->death_time > 0)
        tint = (CrRgba){255,178,178,255};
    float lv = 15.0f, blk = 0.0f;
    if (v->lm_lit == 1) { lv = v->lm_light; blk = v->lm_blk; }
    else if (v->lm_lit == 2) {
        lv = 1.0f;
        tint.r = (u8)(tint.r * v->lm_mul_r + 0.5f);
        tint.g = (u8)(tint.g * v->lm_mul_g + 0.5f);
        tint.b = (u8)(tint.b * v->lm_mul_b + 0.5f);
    }

    ErAff base;
    er_aff_identity(&base);
    er_aff_translate(&base, v->x, v->y, v->z);
    er_aff_rot_y(&base, 180.0f - v->yaw);
    float death = er_death_roll(v);
    if (death != 0.0f) er_aff_rot_z(&base, death * ER_RAD2DEG);
    er_aff_scale3(&base, -1.0f, -1.0f, 1.0f);
    er_aff_translate(&base, 0.0f, -1.501f, 0.0f);

    int child = (v->flags & 8) != 0;
    int chested = !child && ((v->flags & 8192) || v->item_count != 0);
    int written = er_llama_emit_model(
        &base, p, er_llama_coat_sprite(v), child, chested,
        tint, lv, blk, out);
    if (v->item_meta >= 1 && v->item_meta <= 16) {
        ErPart decor[11];
        memcpy(decor, p, sizeof decor);
        for (int i = 0; i < 11; ++i) decor[i].delta = 0.5f;
        written += er_llama_emit_model(
            &base, decor, CR_MOB_LLAMA_DECOR_WHITE + v->item_meta - 1,
            child, chested, tint, lv, blk, out + written);
    }
    return written;
}

static int emit_llama_spit(const GmEntityView *v, CrVertex *out) {
    static const float boxes[7][3] = {
        {-4,0,0}, {0,-4,0}, {0,0,-4}, {0,0,0},
        {2,0,0}, {0,2,0}, {0,0,2}
    };
    CrRgba tint = {255,255,255,255};
    float lv = 15.0f, blk = 0.0f;
    if (v->lm_lit == 1) { lv = v->lm_light; blk = v->lm_blk; }
    else if (v->lm_lit == 2) {
        lv = 1.0f;
        tint.r = (u8)(255.0f * v->lm_mul_r + 0.5f);
        tint.g = (u8)(255.0f * v->lm_mul_g + 0.5f);
        tint.b = (u8)(255.0f * v->lm_mul_b + 0.5f);
    }
    ErAff base;
    er_aff_identity(&base);
    er_aff_translate(&base, v->x, v->y + 0.15f, v->z);
    er_aff_rot_y(&base, v->yaw - 90.0f);
    er_aff_rot_z(&base, v->pitch);
    int written = 0;
    for (int i = 0; i < 7; ++i)
        written += er_aff_box_m(
            &base, CR_MOB_LLAMA_SPIT, 1, 0, 1, 0, 0,
            boxes[i][0], boxes[i][1], boxes[i][2], 2, 2, 2,
            tint, lv, blk, out + written);
    return written;
}

/* RenderMinecart -> ChestRenderer -> TileEntityChestRenderer, retaining the
 * exact nested GL transform instead of approximating the animated chest block
 * with oak-plank cubes. The default minecart chest is closed and faces north;
 * ChestRenderer's +90 degree rotation is observable from the fixed camera. */
static int er_emit_minecart_chest(const GmEntityView *v, CrRgba tint,
                                  float lv, float blk, CrVertex *out) {
    ErAff base;
    er_aff_identity(&base);
    er_aff_translate(&base, v->x, v->y + 0.375f, v->z);
    er_aff_rot_y(&base, 180.0f - v->yaw);
    er_aff_scale(&base, 0.75f);
    er_aff_translate(&base, -0.5f, 0.0f, 0.5f); /* display offset 8 */
    er_aff_rot_y(&base, 90.0f);                 /* ChestRenderer */
    er_aff_translate(&base, 0.0f, 1.0f, 1.0f);
    er_aff_scale3(&base, 1.0f, -1.0f, -1.0f);
    er_aff_translate(&base, 0.5f, 0.5f, 0.5f);
    er_aff_translate(&base, -0.5f, -0.5f, -0.5f);

    int written = 0;
    ErAff part = base;
    er_aff_translate(&part, 1.0f/16.0f, 7.0f/16.0f, 15.0f/16.0f);
    written += er_aff_box_m(
        &part, CR_MOB_CHEST_NORMAL, 1, 0, 1, 0, 0,
        0,-5,-14, 14,5,14, tint,lv,blk,out+written);
    part = base;
    er_aff_translate(&part, 8.0f/16.0f, 7.0f/16.0f, 15.0f/16.0f);
    written += er_aff_box_m(
        &part, CR_MOB_CHEST_NORMAL, 1, 0, 1, 0, 0,
        -1,-2,-15, 2,4,1, tint,lv,blk,out+written);
    part = base;
    er_aff_translate(&part, 1.0f/16.0f, 6.0f/16.0f, 1.0f/16.0f);
    written += er_aff_box_m(
        &part, CR_MOB_CHEST_NORMAL, 1, 0, 1, 0, 19,
        0,0,0, 14,10,14, tint,lv,blk,out+written);
    return written;
}

int gm_entities_emit(const GmEntityView *ents, int n, CrVertex *out, int max) {
    int written = 0;
    for (int e = 0; e < n; ++e) {
        int entity_start = written;
        if (ents[e].flags & 4) continue; /* EntityLivingBase.isInvisible */
        if (ents[e].type == ER_TYPE_LLAMA_SPIT) {
            if (written + 7 * ER_VERTS_PER_BOX > max) break;
            written += emit_llama_spit(&ents[e], out + written);
            continue;
        }
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
        if (ents[e].type == ER_TYPE_WITHER_SKULL) {
            if (written + ER_VERTS_PER_BOX > max) break;
            ErPart head = M_WITHER_SKULL;
            head.sprite = ents[e].wither_skull_invulnerable
                ? CR_MOB_WITHER_INVULNERABLE : CR_MOB_WITHER;
            head.ay = ents[e].yaw * ER_DEG2RAD;
            head.ax = ents[e].pitch * ER_DEG2RAD;
            /* RenderWitherSkull has no RenderLivingBase -1.501 translation.
             * Removing the emitter's baked 24/16 ground lift gives the direct
             * scale(-1,-1,1) ModelSkeletonHead transform. */
            CrRgba tint = {255,255,255,255};
            float lv = 15.0f, blk = 0.0f;
            if (ents[e].lm_lit == 1) {
                lv = ents[e].lm_light;
                blk = ents[e].lm_blk;
            } else if (ents[e].lm_lit == 2) {
                lv = 1.0f;
                tint.r = (u8)(255.0f * ents[e].lm_mul_r + 0.5f);
                tint.g = (u8)(255.0f * ents[e].lm_mul_g + 0.5f);
                tint.b = (u8)(255.0f * ents[e].lm_mul_b + 0.5f);
            }
            written += emit_box(
                &head, 1.0f, 0.0f, 1.0f,
                ents[e].x, ents[e].y - 1.5f, ents[e].z,
                tint, lv, blk,
                1.0f, 0.0f, out + written);
            continue;
        }
        if (ents[e].type == ER_TYPE_BAT) {
            /* RenderLivingBase disables culling around ModelBat. */
            if (written + 9 * ER_VERTS_PER_BOX * 2 > max) break;
            written += emit_bat(&ents[e], out + written);
            written = er_expand_twosided(out, entity_start, written, max);
            continue;
        }
        if (ents[e].type == ER_TYPE_SQUID) {
            /* RenderLivingBase disables culling around ModelSquid. */
            if (written + 9 * ER_VERTS_PER_BOX * 2 > max) break;
            written += emit_squid(&ents[e], out + written);
            written = er_expand_twosided(out, entity_start, written, max);
            continue;
        }
        if (er_is_horse(ents[e].type)) {
            /* RenderLivingBase disables GL_CULL_FACE before ModelHorse.
             * This is observable on the undead skins: back-facing mane
             * texels show through transparent holes in the neck. */
            if (written + er_horse_box_count(&ents[e])
                    * ER_VERTS_PER_BOX * 2 > max) break;
            written += emit_horse(&ents[e], out + written);
            written = er_expand_twosided(out, entity_start, written, max);
            continue;
        }
        if (ents[e].type == ER_TYPE_LLAMA) {
            if (written + er_llama_box_count(&ents[e])
                    * ER_VERTS_PER_BOX * 2 > max) break;
            written += emit_llama(&ents[e], out + written);
            written = er_expand_twosided(out, entity_start, written, max);
            continue;
        }
        const ErModel *m = er_model_for_type(ents[e].type);
        if (!m) continue;                            /* NONE / PLAYER */
        int need = m->nparts * ER_VERTS_PER_BOX;
        if (ents[e].type == ER_TYPE_MINECART_CHEST)
            need += 3 * ER_VERTS_PER_BOX;
        if (ents[e].type == ER_TYPE_ARMOR_STAND)
            need += er_armor_stand_box_count(&ents[e]) * ER_VERTS_PER_BOX;
        if (ents[e].type == ER_TYPE_SKELETON
                && er_armor_sprite(ents[e].armor_head, 1) >= 0)
            need += (er_armor_overlay_sprite(ents[e].armor_head, 1) >= 0
                    ? 4 : 2) * ER_VERTS_PER_BOX;
        if (ents[e].type == ER_TYPE_SKELETON
                || ents[e].type == ER_TYPE_WITHER_SKELETON)
            need *= 2; /* RenderLivingBase keeps GL_CULL_FACE disabled. */
        if (ents[e].type == ER_TYPE_SNOWMAN) {
            if (ents[e].flags & GM_ENTITY_FLAG_SNOWMAN_PUMPKIN)
                need += ER_VERTS_PER_BOX;
            need *= 2;
        }
        if (ents[e].type == ER_TYPE_WOLF && (ents[e].flags & 128))
            need += m->nparts * ER_VERTS_PER_BOX;
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
        if (ents[e].type == ER_TYPE_WITHER) {
            int invul = ents[e].wither_invul_time;
            int sprite = invul > 0 && (invul > 80 || invul / 5 % 2 != 1)
                ? CR_MOB_WITHER_INVULNERABLE : CR_MOB_WITHER;
            er_wither_pose(&ents[e], local, sprite, 0.0f);
        }

        /* skin-variant override (pigman/husk/stray/cave spider/mooshroom):
         * same model, different atlas sprite. All variant bases are
         * single-skin models (sheep, the only two-sprite model, has none). */
        {
            int skin = ents[e].skin;
            if (skin <= 0 && ents[e].type == ER_TYPE_PIGMAN)
                skin = CR_MOB_PIGMAN + 1;
            if (skin <= 0 && ents[e].type == ER_TYPE_CAVE_SPIDER)
                skin = CR_MOB_CAVE_SPIDER + 1;
            if (ents[e].type == ER_TYPE_WOLF)
                skin = ((ents[e].flags & 128)
                    ? CR_MOB_WOLF_TAME : CR_MOB_WOLF) + 1;
            if (ents[e].type == ER_TYPE_OCELOT) {
                static const int cat_skins[4] = {
                    CR_MOB_OCELOT, CR_MOB_CAT_BLACK,
                    CR_MOB_CAT_RED, CR_MOB_CAT_SIAMESE
                };
                int variant = ents[e].item_meta;
                if (variant < 0 || variant > 3) variant = 0;
                skin = cat_skins[variant] + 1;
            }
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
            if (skin <= 0 && ents[e].type == ER_TYPE_ZOMBIE_VILLAGER) {
                static const int zombie_profession_skins[5] = {
                    CR_MOB_ZOMBIE_FARMER,
                    CR_MOB_ZOMBIE_LIBRARIAN,
                    CR_MOB_ZOMBIE_PRIEST,
                    CR_MOB_ZOMBIE_SMITH,
                    CR_MOB_ZOMBIE_BUTCHER
                };
                int profession = ents[e].item_id;
                skin = (profession >= 0 && profession < 5
                        ? zombie_profession_skins[profession]
                        : CR_MOB_ZOMBIE_VILLAGER) + 1;
            }
            if (skin <= 0 && ents[e].type == ER_TYPE_VINDICATOR)
                skin = CR_MOB_VINDICATOR + 1;
            if (skin <= 0 && ents[e].type == ER_TYPE_EVOKER)
                skin = CR_MOB_EVOKER + 1;
            if (skin <= 0 && ents[e].type == ER_TYPE_VEX)
                skin = ((ents[e].flags & 1024)
                    ? CR_MOB_VEX_CHARGING : CR_MOB_VEX) + 1;
            if (skin <= 0 && ents[e].type == ER_TYPE_EVOKER_FANGS)
                skin = CR_MOB_FANGS + 1;
            if (skin <= 0 && ents[e].type == ER_TYPE_GUARDIAN)
                skin = CR_MOB_GUARDIAN + 1;
            if (skin <= 0 && ents[e].type == ER_TYPE_ELDER_GUARDIAN)
                skin = CR_MOB_GUARDIAN_ELDER + 1;
            if (ents[e].type == ER_TYPE_RABBIT) {
                static const int rabbit_skins[7] = {
                    CR_MOB_RABBIT_BROWN, CR_MOB_RABBIT_WHITE,
                    CR_MOB_RABBIT_BLACK, CR_MOB_RABBIT_WHITE_SPLOTCHED,
                    CR_MOB_RABBIT_GOLD, CR_MOB_RABBIT_SALT,
                    CR_MOB_RABBIT_CAERBANNOG
                };
                int variant = ents[e].item_meta;
                if (variant == 99) variant = 6;
                if (variant < 0 || variant > 6) variant = 0;
                skin = rabbit_skins[variant] + 1;
            }
            if (skin > 0)
                for (int p = 0; p < np; ++p)
                    local[p].sprite = skin - 1;
        }

        float lsa = ents[e].limb_swing_amount;
        float ls  = ents[e].limb_swing;
        int t = ents[e].type;
        /* RenderLivingBase triples limbSwing before setRotationAngles for a
         * child model. This is independent of ModelQuadruped's later split
         * head/body scale transform. */
        if ((ents[e].flags & 8)
                && (t == ER_TYPE_COW || t == ER_TYPE_POLAR_BEAR))
            ls *= 3.0f;
        if (t == ER_TYPE_SHULKER && np >= 3) {
            float peek = ents[e].graze_y;
            float f1 = (0.5f + peek) * ER_PI;
            float f2 = -1.0f + sinf(f1);
            float bob = f1 > ER_PI
                ? sinf((float)ents[e].ticks_existed * 0.1f) * 0.7f
                : 0.0f;
            local[1].ry = 16.0f + sinf(f1) * 8.0f + bob;
            local[1].ay = peek > 0.3f
                ? f2 * f2 * f2 * f2 * ER_PI * 0.125f : 0.0f;
            local[2].ay = (ents[e].head_yaw - ents[e].yaw) * ER_DEG2RAD;
            local[2].ax = ents[e].pitch * ER_DEG2RAD;
        }
        if ((t == ER_TYPE_GUARDIAN || t == ER_TYPE_ELDER_GUARDIAN)
                && np >= 22) {
            static const float spine_ax[12] = {
                1.75f,.25f,0,0,.5f,.5f,.5f,.5f,1.25f,.75f,0,0
            };
            static const float spine_ay[12] = {
                0,0,0,0,.25f,1.75f,1.25f,.75f,0,0,0,0
            };
            static const float spine_az[12] = {
                0,0,.25f,1.75f,0,0,0,0,0,0,.75f,1.25f
            };
            static const float spine_px[12] = {
                0,0,8,-8,-8,8,8,-8,0,0,8,-8
            };
            static const float spine_py[12] = {
                -8,-8,-8,-8,0,0,0,0,8,8,8,8
            };
            static const float spine_pz[12] = {
                8,-8,0,0,-8,-8,8,8,8,-8,0,0
            };
            float retract = (1.0f - ents[e].graze_y) * 0.55f;
            float age = (float)ents[e].ticks_existed;
            for (int i = 0; i < 12; ++i) {
                int p = 5 + i;
                float reach = 1.0f + cosf(age * 1.5f + (float)i) * .01f
                            - retract;
                local[p].ax = ER_PI * spine_ax[i];
                local[p].ay = ER_PI * spine_ay[i];
                local[p].az = ER_PI * spine_az[i];
                local[p].rx = spine_px[i] * reach;
                local[p].ry = 16.0f + spine_py[i] * reach;
                local[p].rz = spine_pz[i] * reach;
            }

            /* ModelGuardian's eye follows the targeted entity. The live view
             * carries the same target midpoint in heal_*; tape ghosts without
             * a target retain the centered eye. */
            if (ents[e].flags & 4096) {
                float eye_y = fy + (t == ER_TYPE_ELDER_GUARDIAN
                    ? 0.99875f : 0.425f);
                local[17].ry = ents[e].heal_y > eye_y ? 0.0f : 1.0f;
                float dx = ents[e].heal_x - fx;
                float dz = ents[e].heal_z - fz;
                float dl = sqrtf(dx * dx + dz * dz);
                if (dl > 1.0e-5f) {
                    float yaw = ents[e].yaw * ER_DEG2RAD;
                    float lx = (dx * cosf(yaw) - dz * sinf(yaw)) / dl;
                    local[17].rx = copysignf(sqrtf(fabsf(lx)) * 2.0f, lx);
                }
            }

            /* Flatten guardianTail[0] -> [1] -> [2] child pivots into the
             * independent box emitter while retaining the composed yaw. */
            float wave = sinf(ents[e].anim_time);
            float a0 = wave * ER_PI * .05f;
            float a1 = wave * ER_PI * .10f;
            float a2 = wave * ER_PI * .15f;
            local[18].ay = a0;
            local[19].rx = -1.5f * cosf(a0) + 14.0f * sinf(a0);
            local[19].ry = .5f;
            local[19].rz =  1.5f * sinf(a0) + 14.0f * cosf(a0);
            local[19].ay = a0 + a1;
            float a01 = a0 + a1;
            float tail2x = local[19].rx
                         + .5f * cosf(a01) + 6.0f * sinf(a01);
            float tail2z = local[19].rz
                         - .5f * sinf(a01) + 6.0f * cosf(a01);
            for (int p = 20; p <= 21; ++p) {
                local[p].rx = tail2x;
                local[p].ry = 1.0f;
                local[p].rz = tail2z;
                local[p].ay = a01 + a2;
            }
        }
        if ((ents[e].tape_pose || t == ER_TYPE_SHEEP || t == ER_TYPE_PIG ||
             t == ER_TYPE_COW || t == ER_TYPE_CHICKEN ||
             t == ER_TYPE_WOLF || t == ER_TYPE_OCELOT
             || t == ER_TYPE_ZOMBIE_VILLAGER
             || t == ER_TYPE_VINDICATOR || t == ER_TYPE_EVOKER
             || t == ER_TYPE_VEX || t == ER_TYPE_IRON_GOLEM
             || t == ER_TYPE_POLAR_BEAR) && np > 0) {
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
            else if (t == ER_TYPE_ZOMBIE_VILLAGER) { h0 = 0; h1 = 1; }
            else if (t == ER_TYPE_VINDICATOR || t == ER_TYPE_EVOKER) {
                h0 = 0; h1 = 1;
            }
            else if (t == ER_TYPE_VEX) { h0 = 0; h1 = 0; }
            else if (t == ER_TYPE_IRON_GOLEM) { h0 = 0; h1 = 1; }
            else if (t == ER_TYPE_WOLF || t == ER_TYPE_OCELOT) {
                h0 = 0; h1 = 3;
            }
            else if (t == ER_TYPE_COW) { h0 = 0; h1 = 2; }
            else if (t == ER_TYPE_POLAR_BEAR) { h0 = 0; h1 = 3; }
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
            else if (t == ER_TYPE_POLAR_BEAR)
                apply_quad_limb_swing(local, np, 6, ls, lsa);
            else if (t == ER_TYPE_WOLF)   apply_quad_limb_swing(local, np, 6, ls, lsa);
            else if (t == ER_TYPE_OCELOT) apply_quad_limb_swing(local, np, 5, ls, lsa);
            else if (t == ER_TYPE_LLAMA)  apply_quad_limb_swing(local, np, 5, ls, lsa);
            else if (t == ER_TYPE_CHICKEN) {
                /* chicken legs at parts 4,5 */
                if (np > 5) {
                    float a = cosf(ls * 0.6662f) * 1.4f * lsa;
                    float b = cosf(ls * 0.6662f + ER_PI) * 1.4f * lsa;
                    local[4].ax = a; local[5].ax = b;
                }
            } else if (t == ER_TYPE_ZOMBIE_VILLAGER) {
                if (np > 7) {
                    local[6].ax = cosf(ls * 0.6662f) * 1.4f * lsa;
                    local[7].ax = cosf(ls * 0.6662f + ER_PI) * 1.4f * lsa;
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
            } else if (t == ER_TYPE_IRON_GOLEM && np >= 8) {
                float wave = (fabsf(fmodf(ls, 13.0f) - 6.5f) - 3.25f)
                    / 3.25f;
                local[6].ax = -1.5f * wave * lsa;
                local[7].ax =  1.5f * wave * lsa;
            } else if ((t == ER_TYPE_VINDICATOR || t == ER_TYPE_EVOKER)
                    && np >= 11) {
                local[9].ax = cosf(ls * 0.6662f) * 1.4f * lsa * 0.5f;
                local[10].ax = cosf(ls * 0.6662f + ER_PI) * 1.4f * lsa * 0.5f;
            } else if (t == ER_TYPE_VEX && np >= 7) {
                local[4].ax = cosf(ls * 0.6662f) * 1.4f * lsa
                    + ER_PI / 5.0f;
            }
        }
        if (t == ER_TYPE_SKELETON && np > 3) {
            /* ModelBiped's idle arm oscillation runs even at zero limb swing.
             * ageInTicks is ticksExisted + render partialTicks (pinned at 1). */
            float age = (float)ents[e].ticks_existed + 1.0f;
            float sway_z = er_mathhelper_cos(age * 0.09f) * 0.05f + 0.05f;
            float sway_x = er_mathhelper_sin(age * 0.067f) * 0.05f;
            local[2].az += sway_z;
            local[3].az -= sway_z;
            local[2].ax += sway_x;
            local[3].ax -= sway_x;
            if (ents[e].flags & 131072) {
                float head_yaw = (ents[e].head_yaw - ents[e].yaw)
                    * ER_DEG2RAD;
                float head_pitch = ents[e].pitch * ER_DEG2RAD;
                local[2].ay = -0.1f + head_yaw;
                local[3].ay = 0.5f + head_yaw;
                local[2].ax = local[3].ax = -ER_PI / 2.0f + head_pitch;
            }
        }
        if (t == ER_TYPE_IRON_GOLEM && np >= 8) {
            if (ents[e].golem_attack_timer > 0) {
                float phase = (float)ents[e].golem_attack_timer;
                float wave = (fabsf(fmodf(phase, 10.0f) - 5.0f) - 2.5f)
                    / 2.5f;
                local[4].ax = local[5].ax = -2.0f + 1.5f * wave;
            } else if (ents[e].golem_rose_timer > 0) {
                float phase = (float)ents[e].golem_rose_timer;
                float wave = (fabsf(fmodf(phase, 70.0f) - 35.0f) - 17.5f)
                    / 17.5f;
                local[4].ax = -0.8f + 0.025f * wave;
                local[5].ax = 0.0f;
            } else {
                float wave = (fabsf(fmodf(ls, 13.0f) - 6.5f) - 3.25f)
                    / 3.25f;
                local[4].ax = (-0.2f + 1.5f * wave) * lsa;
                local[5].ax = (-0.2f - 1.5f * wave) * lsa;
            }
        }
        if (t == ER_TYPE_EVOKER && (ents[e].flags & 512) && np >= 11) {
            float age = (float)ents[e].ticks_existed;
            local[7].ax = cosf(age * 0.6662f) * 0.25f;
            local[8].ax = local[7].ax;
            local[7].az = 2.3561945f;
            local[8].az = -2.3561945f;
        } else if (t == ER_TYPE_VINDICATOR && (ents[e].flags & 256)
                && np >= 11) {
            float age = (float)ents[e].ticks_existed;
            float sp = ents[e].swing_progress;
            float f = sinf(sp * ER_PI);
            float f1 = sinf((1.0f - (1.0f - sp) * (1.0f - sp)) * ER_PI);
            local[7].ay = 0.15707964f;
            local[8].ay = -0.15707964f;
            local[7].ax = -1.8849558f + cosf(age * 0.09f) * 0.15f
                + f * 2.2f - f1 * 0.4f;
            local[8].ax = cosf(age * 0.19f) * 0.5f
                + f * 1.2f - f1 * 0.4f;
            local[7].az = cosf(age * 0.09f) * 0.05f + 0.05f;
            local[8].az = -local[7].az;
            local[7].ax += sinf(age * 0.067f) * 0.05f;
            local[8].ax -= sinf(age * 0.067f) * 0.05f;
        }
        if (t == ER_TYPE_VEX && np >= 7) {
            float age = (float)ents[e].ticks_existed;
            float idle_z = cosf(age * 0.09f) * 0.05f + 0.05f;
            local[2].ax = cosf(ls * 0.6662f + ER_PI) * lsa;
            local[3].ax = cosf(ls * 0.6662f) * lsa;
            local[2].ax = local[2].ax * 0.5f - ER_PI / 10.0f;
            local[2].az = idle_z;
            local[3].az = -idle_z;
            local[2].ax += sinf(age * 0.067f) * 0.05f;
            local[3].ax -= sinf(age * 0.067f) * 0.05f;
            if (ents[e].flags & 1024) local[2].ax = 3.7699115f;
            local[5].ay = 0.47123894f
                + cosf(age * 0.8f) * ER_PI * 0.05f;
            local[6].ay = -local[5].ay;
        }
        if (t == ER_TYPE_EVOKER_FANGS && np >= 3) {
            float progress = ents[e].swing_progress;
            float closed = progress * 2.0f;
            if (closed > 1.0f) closed = 1.0f;
            closed = 1.0f - closed * closed * closed;
            local[1].az = ER_PI - closed * 0.35f * ER_PI;
            local[2].az = ER_PI + closed * 0.35f * ER_PI;
            float rise = (progress + sinf(progress * 2.7f)) * 7.2f;
            local[0].ry = local[1].ry = local[2].ry = 27.968f - rise;
        }
        if ((ents[e].flags & 64) && t == ER_TYPE_WOLF && np >= 11) {
            local[4].ry = 18.0f; local[4].rz = 0.0f;
            local[4].ax = ER_PI / 4.0f;
            local[5].ry = 16.0f; local[5].rz = -3.0f;
            local[5].ax = 1.2566371f;
            local[6].ry = local[7].ry = 22.0f;
            local[6].rz = local[7].rz = 2.0f;
            local[6].ax = local[7].ax = 4.7123890f;
            local[8].ry = local[9].ry = 17.0f;
            local[8].rz = local[9].rz = -4.0f;
            local[8].ax = local[9].ax = 5.8119470f;
            local[10].ry = 21.0f; local[10].rz = 6.0f;
        } else if ((ents[e].flags & 64)
                && t == ER_TYPE_OCELOT && np >= 11) {
            local[4].ry = 16.0f; local[4].rz = -6.0f;
            local[4].ax = ER_PI / 4.0f;
            local[5].ax = local[6].ax = ER_PI / 2.0f;
            local[7].ry = local[8].ry = 21.0f;
            local[9].ax = 1.7278761f;
            local[10].ax = 2.6703540f;
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
        if (t == ER_TYPE_GHAST && np >= 10) {
            float age = (float)ents[e].ticks_existed + 1.0f;
            for (int p = 1; p < 10; ++p)
                local[p].ax = 0.2f * sinf(age * 0.3f + (float)(p - 1)) + 0.4f;
        } else if (t == ER_TYPE_MAGMA && np >= 8) {
            /* ModelMagmaCube.setLivingAnimations: segment.rotationPointY =
             * -(4-i)*squish*1.7 (squish from EntitySlime.squishFactor view field). */
            float sq = ents[e].squish;
            if (sq < 0.0f) sq = 0.0f;
            for (int p = 0; p < 8; ++p)
                local[p].ry = -(float)(4 - p) * sq * 1.7f;
        } else if (t == ER_TYPE_ENDERMITE && np >= 4) {
            float age = (float)ents[e].ticks_existed;
            for (int p = 0; p < 4; ++p) {
                float phase = age * 0.9f
                    + (float)p * 0.15f * ER_PI;
                float distance = (float)abs(p - 2);
                local[p].ay = er_mathhelper_cos(phase) * ER_PI * 0.01f
                    * (1.0f + distance);
                local[p].rx = er_mathhelper_sin(phase) * ER_PI * 0.1f
                    * distance;
            }
        }
        if (t == ER_TYPE_ARMOR_STAND && np >= 10) {
            static const int pose_part[9] = {
                0, 1, 3, 2, 5, 4, 1, 1, 1
            };
            for (int p = 0; p < 9; ++p)
                er_armor_stand_pose_part(
                    &ents[e], &local[p], pose_part[p]);
            /* ModelArmorStand.setRotationAngles keeps the square plate fixed
             * in world orientation by applying -entity.rotationYaw inside the
             * renderer's outer (180-yaw) transform. */
            local[9].ay = -ents[e].yaw * ER_DEG2RAD;
        }
        if (t == ER_TYPE_SNOWMAN && np >= 5) {
            float head_yaw = ents[e].head_yaw * ER_DEG2RAD;
            float body_yaw = head_yaw * 0.25f;
            float body_sin = er_mathhelper_sin(body_yaw);
            float body_cos = er_mathhelper_cos(body_yaw);
            local[2].ay = head_yaw;
            local[2].ax = ents[e].pitch * ER_DEG2RAD;
            local[0].ay = body_yaw;
            local[3].az = 1.0f;
            local[4].az = -1.0f;
            local[3].ay = body_yaw;
            local[4].ay = ER_PI + body_yaw;
            local[3].rx = body_cos * 5.0f;
            local[3].rz = -body_sin * 5.0f;
            local[4].rx = -body_cos * 5.0f;
            local[4].rz = body_sin * 5.0f;
        }
        if (t == ER_TYPE_POLAR_BEAR && np >= 10) {
            float stand = ents[e].anim_time;
            if (stand < 0.0f) stand = 0.0f;
            if (stand > 1.0f) stand = 1.0f;
            stand *= stand;
            float lowered = 1.0f - stand;
            for (int p = 4; p <= 5; ++p) {
                local[p].ax = QUAD_BODY_ROT - stand * ER_PI * 0.35f;
                local[p].ry = 9.0f * lowered + 11.0f * stand;
            }
            for (int p = 8; p <= 9; ++p) {
                local[p].ry = 14.0f * lowered - 6.0f * stand;
                local[p].rz = -8.0f * lowered - 4.0f * stand;
                local[p].ax -= stand * ER_PI * 0.45f;
            }
            for (int p = 0; p <= 3; ++p) {
                local[p].ry = 10.0f * lowered - 12.0f * stand;
                local[p].rz = -16.0f * lowered - 3.0f * stand;
                local[p].ax += stand * ER_PI * 0.15f;
            }
        }
        if (t == ER_TYPE_RABBIT && np >= 12) {
            float jump = er_mathhelper_sin(ents[e].anim_time * ER_PI);
            float hay = (ents[e].head_yaw - ents[e].yaw) * ER_DEG2RAD;
            float hax = ents[e].pitch * ER_DEG2RAD;
            local[2].ax = local[3].ax =
                (jump * 50.0f - 21.0f) * ER_DEG2RAD;
            local[0].ax = local[1].ax = jump * 50.0f * ER_DEG2RAD;
            local[5].ax = local[6].ax =
                (jump * -40.0f - 11.0f) * ER_DEG2RAD;
            local[7].ax = local[8].ax = local[9].ax = local[11].ax = hax;
            local[7].ay = local[11].ay = hay;
            local[8].ay = hay - .2617994f;
            local[9].ay = hay + .2617994f;
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
        float render_yaw = 180.0f - ents[e].yaw;
        if (t == ER_TYPE_ARMOR_STAND && ents[e].stand_punch_time_valid
                && ents[e].stand_punch_time < 5.0f)
            render_yaw += sinf(
                ents[e].stand_punch_time / 1.5f * ER_PI) * 3.0f;
        float rad = render_yaw * ER_DEG2RAD;
        float cs = cosf(rad), sn = sinf(rad);
        float sc = m->scale > 0.0f ? m->scale : 1.0f;
        if (t == ER_TYPE_WITHER) sc *= er_wither_scale(&ents[e]);
        /* RenderLivingBase.prepareScale translates by -1.501 before the
         * model draw.  emit_box's shared 24/16 ground lift represents -1.5;
         * retain the final scaled milliblock for the strict Wither fixture. */
        if (t == ER_TYPE_WITHER) fy += 0.001f * sc;
        if (t == ER_TYPE_GIANT) sc *= 6.0f;
        if (t == ER_TYPE_EVOKER_FANGS) {
            float outer = 2.0f;
            if (ents[e].swing_progress > 0.9f)
                outer *= (1.0f - ents[e].swing_progress)
                    / 0.10000000149011612f;
            if (outer < 0.0f) outer = 0.0f;
            sc = outer * 0.5f;
        }
        if (t == ER_TYPE_ELDER_GUARDIAN) sc *= 2.35f;
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
            /* prepareScale's -1.501 translation contributes +0.001 after the
             * outer negative Y scale.  ModelSlime.render's +0.001 modelview
             * translation cancels it exactly; ModelMagmaCube has no matching
             * translation, so retain the milliblock after its Y scale. */
            float fy_slime = t == ER_TYPE_MAGMA ? fy + 0.001f * scy : fy;
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
                    /* These two models contain only axis-aligned boxes.  On
                     * an upright entity, ModelBox face 2 is the upward face;
                     * reproduce GL_RESCALE_NORMAL before stretching Y.  The
                     * downward face remains ambient-only and is unchanged. */
                    if (roll_s == 0.0f) {
                        float top_shade = er_shade_item_rescaled(
                            0.0f, 1.0f, 0.0f, scx / scy);
                        for (int vi = s0 + 12; vi < s0 + 18; ++vi)
                            out[vi].ao = top_shade;
                    }
                    for (int vi = s0; vi < written; ++vi) {
                        out[vi].pos.y = fy_slime + (out[vi].pos.y - fy_slime) * ymul;
                    }
                }
            }
        } else if (t == ER_TYPE_RABBIT && (ents[e].flags & 8)) {
            for (int p = 0; p < np; ++p) {
                ErPart child_part = local[p];
                float child_sc;
                if ((p >= 7 && p <= 9) || p == 11) {
                    child_part.ry += 22.0f;
                    child_part.rz += 2.0f;
                    child_sc = sc * 0.56666666f;
                } else {
                    child_part.ry += 36.0f;
                    child_sc = sc * 0.4f;
                }
                written += emit_box(
                    &child_part, cs, sn, child_sc, fx, fy, fz, tint,
                    lv, blk, roll_c, roll_s, out + written);
            }
        } else if ((t == ER_TYPE_COW || t == ER_TYPE_POLAR_BEAR)
                && (ents[e].flags & 8)) {
            /* ModelQuadruped's child path renders the head at full size after
             * the cow-specific (8,6) child offset, then renders the body and
             * legs at half scale after a 24-pixel downward translation.  In
             * emit_box coordinates the latter translation is already encoded
             * by scaling the model pivots around y=24. */
            for (int p = 0; p < np; ++p) {
                ErPart child_part = local[p];
                float child_sc = sc;
                int head_last = t == ER_TYPE_POLAR_BEAR ? 3 : 2;
                if (p <= head_last) {
                    child_part.ry += t == ER_TYPE_POLAR_BEAR ? 16.0f : 8.0f;
                    child_part.rz += t == ER_TYPE_POLAR_BEAR ? 4.0f : 6.0f;
                    if (t == ER_TYPE_POLAR_BEAR)
                        child_sc *= 0.6666667f;
                } else {
                    child_sc *= 0.5f;
                }
                written += emit_box(
                    &child_part, cs, sn, child_sc, fx, fy, fz, tint,
                    lv, blk, roll_c, roll_s, out + written);
            }
        } else if (t == ER_TYPE_WOLF && (ents[e].flags & 128)) {
            for (int p = 0; p < np; ++p)
                written += emit_box(&local[p], cs, sn, sc, fx, fy, fz, tint,
                                    lv, blk, roll_c, roll_s, out + written);
            CrRgba collar = sheep_wool_tint(
                ents[e].item_meta,
                ents[e].hurt_time > 0 || ents[e].death_time > 0);
            if (ents[e].lm_lit == 2) {
                collar.r = (u8)(collar.r * ents[e].lm_mul_r + 0.5f);
                collar.g = (u8)(collar.g * ents[e].lm_mul_g + 0.5f);
                collar.b = (u8)(collar.b * ents[e].lm_mul_b + 0.5f);
            }
            for (int p = 0; p < np; ++p) {
                ErPart overlay = local[p];
                overlay.sprite = CR_MOB_WOLF_COLLAR;
                written += emit_box(
                    &overlay, cs, sn, sc, fx, fy, fz, collar,
                    lv, blk, roll_c, roll_s, out + written);
            }
        } else if ((t == ER_TYPE_VINDICATOR || t == ER_TYPE_EVOKER)
                && np >= 11) {
            int active_arms = (t == ER_TYPE_VINDICATOR
                    && (ents[e].flags & 256))
                || (t == ER_TYPE_EVOKER && (ents[e].flags & 512));
            for (int p = 0; p < np; ++p) {
                if (active_arms ? (p >= 4 && p <= 6) : (p == 7 || p == 8))
                    continue;
                written += emit_box(&local[p], cs, sn, sc, fx, fy, fz, tint,
                                    lv, blk, roll_c, roll_s, out + written);
            }
        } else if (t == ER_TYPE_ARMOR_STAND) {
            for (int p = 0; p < np; ++p) {
                if ((p == 2 || p == 3) && !(ents[e].stand_flags & 1))
                    continue;
                if (p == 9 && (ents[e].stand_flags & 2))
                    continue;
                float part_sc = sc;
                float part_fy = fy;
                if (ents[e].stand_flags & 4) {
                    part_sc *= p == 0 ? 0.75f : 0.5f;
                    if (p == 0) part_fy -= 0.375f * sc;
                }
                written += emit_box(
                    &local[p], cs, sn, part_sc, fx, part_fy, fz, tint,
                                    lv, blk, roll_c, roll_s, out + written);
            }
            written += er_emit_armor_stand_layers(
                &ents[e], cs, sn, sc, fx, fy, fz, tint, lv, blk,
                roll_c, roll_s, out + written);
        } else {
            if (t == ER_TYPE_MINECART_CHEST)
                written += er_emit_minecart_chest(
                    &ents[e], tint, lv, blk, out + written);
            for (int p = 0; p < np; ++p)
                written += emit_box(&local[p], cs, sn, sc, fx, fy, fz, tint,
                                    lv, blk, roll_c, roll_s, out + written);
            if (t == ER_TYPE_SNOWMAN
                    && (ents[e].flags & GM_ENTITY_FLAG_SNOWMAN_PUMPKIN)) {
                /* LayerSnowmanHead: head.postRender, translate -5.5 model
                 * units, then a 0.625 block-item scale. A 16-unit UV cube
                 * inflated by -3 has the exact ten-unit visible extent. */
                ErPart pumpkin = {
                    CR_MOB_SNOWMAN_PUMPKIN, 0,0,
                    -8,-13.5f,-8, 16,16,16, 0,4,0,
                    local[2].ax,local[2].ay,local[2].az, -3.0f,0
                };
                written += emit_box(
                    &pumpkin, cs, sn, sc, fx, fy, fz, tint,
                    lv, blk, roll_c, roll_s, out + written);
            }
            if (t == ER_TYPE_SKELETON && np > 0)
                written += er_biped_head_armor_emit(
                    &ents[e], &local[0], cs, sn, sc, fx, fy, fz, tint,
                    lv, blk, roll_c, roll_s, out + written);
        }
        if (t == ER_TYPE_SKELETON || t == ER_TYPE_WITHER_SKELETON
                || t == ER_TYPE_SNOWMAN)
            written = er_expand_twosided(
                out, entity_start, written, max);
        if (t == ER_TYPE_SHULKER)
            er_shulker_face_transform(
                ents[e].item_meta, fx, fy, fz,
                out + entity_start, written - entity_start);
        if (t == ER_TYPE_SHULKER_BULLET) {
            for (int vi = entity_start; vi < written; ++vi)
                out[vi].pos.y += 0.15f;
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

int gm_entities_glowing_emit(const GmEntityView *ents, int n,
                             CrVertex *out, int max) {
    int written = 0;
    if (!ents || !out || max <= 0) return 0;
    for (int i = 0; i < n; ++i) {
        GmEntityView view;
        int one;
        if (!(ents[i].flags & GM_ENTITY_FLAG_GLOWING)) continue;
        view = ents[i];
        view.flags &= ~4; /* renderModel: !invisible || renderOutlines */
        one = gm_entities_emit(&view, 1, out + written, max - written);
        written += one;
    }
    return written;
}

int gm_entities_glowing_slime_gel_emit(const GmEntityView *ents, int n,
                                       CrVertex *out, int max) {
    int written = 0;
    if (!ents || !out || max <= 0) return 0;
    for (int i = 0; i < n; ++i) {
        GmEntityView view;
        if (!(ents[i].flags & GM_ENTITY_FLAG_GLOWING)
                || ents[i].type != ER_TYPE_SLIME)
            continue;
        view = ents[i];
        view.flags &= ~4;
        written += gm_slime_gel_emit(
            &view, 1, out + written, max - written);
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

static CrVertex er_guardian_beam_vertex(
        const float p[3], float u, float v, CrRgba tint) {
    CrVertex out;
    out.pos.x = p[0]; out.pos.y = p[1]; out.pos.z = p[2];
    out.uv.x = u; out.uv.y = v;
    out.light = 15.0f; out.blk = 15.0f;
    out.tint = tint; out.ao = 1.0f;
    return out;
}

/* RenderGuardian disables culling. Magma culls globally, so duplicate each
 * quad with reverse winding just like the dragon/crystal beam path. */
static int er_guardian_beam_quad(
        const float p[4][3], const float uv[4][2], CrRgba tint,
        CrVertex *out, int max) {
    static const int tri[12] = {
        0,1,2, 0,2,3,
        2,1,0, 3,2,0
    };
    if (max < 12) return 0;
    CrVertex q[4];
    for (int i = 0; i < 4; ++i)
        q[i] = er_guardian_beam_vertex(p[i], uv[i][0], uv[i][1], tint);
    for (int i = 0; i < 12; ++i) out[i] = q[tri[i]];
    return 12;
}

int gm_guardian_beams_emit(const GmEntityView *ents, int n, CrVertex *out,
                           int max) {
    if (!ents || !out || max < 36) return 0;
    int written = 0;
    for (int e = 0; e < n; ++e) {
        const GmEntityView *g = &ents[e];
        if ((g->type != ER_TYPE_GUARDIAN
                    && g->type != ER_TYPE_ELDER_GUARDIAN)
                || !(g->flags & 4096) || !g->has_heal_beam)
            continue;
        if (written + 36 > max) break;

        float origin[3] = {
            g->x,
            g->y + (g->type == ER_TYPE_ELDER_GUARDIAN
                ? 0.99875f : 0.425f),
            g->z
        };
        float d[3] = {
            g->heal_x - origin[0],
            g->heal_y - origin[1],
            g->heal_z - origin[2]
        };
        float target_len = sqrtf(d[0]*d[0] + d[1]*d[1] + d[2]*d[2]);
        if (target_len < 1.0e-5f) continue;
        for (int k = 0; k < 3; ++k) d[k] /= target_len;
        float beam_len = target_len + 1.0f;

        /* Any stable orthonormal plane around the beam axis is equivalent to
         * RenderGuardian's two rotations; the scrolling phase supplies its
         * observable roll. */
        float p[3] = { -d[2], 0.0f, d[0] };
        float pl = sqrtf(p[0]*p[0] + p[2]*p[2]);
        if (pl < 1.0e-5f) {
            p[0] = 1.0f; p[1] = 0.0f; p[2] = 0.0f;
        } else {
            p[0] /= pl; p[2] /= pl;
        }
        float q[3] = {
            d[1]*p[2] - d[2]*p[1],
            d[2]*p[0] - d[0]*p[2],
            d[0]*p[1] - d[1]*p[0]
        };
        float time = (float)g->ticks_existed + 1.0f;
        float roll = time * -0.075f;
        float cr = cosf(roll), sr = sinf(roll);
        float pr[3], qr[3];
        for (int k = 0; k < 3; ++k) {
            pr[k] = p[k] * cr + q[k] * sr;
            qr[k] = q[k] * cr - p[k] * sr;
        }

        float progress = g->swing_progress;
        if (progress < 0.0f) progress = 0.0f;
        if (progress > 1.0f) progress = 1.0f;
        float power = progress * progress;
        CrRgba tint = {
            (u8)(64.0f + power * 191.0f),
            (u8)(32.0f + power * 191.0f),
            (u8)(128.0f - power * 64.0f), 255
        };
        float v0 = -1.0f + fmodf(time * 0.5f, 1.0f);
        float v1 = beam_len * 2.5f + v0;
        float far[3];
        for (int k = 0; k < 3; ++k)
            far[k] = origin[k] + d[k] * beam_len;

        const float side_uv[4][2] = {
            {.4999f,v1}, {.4999f,v0}, {0,v0}, {0,v1}
        };
        float quad[4][3];
        for (int k = 0; k < 3; ++k) {
            quad[0][k] = far[k] - pr[k] * .2f;
            quad[1][k] = origin[k] - pr[k] * .2f;
            quad[2][k] = origin[k] + pr[k] * .2f;
            quad[3][k] = far[k] + pr[k] * .2f;
        }
        written += er_guardian_beam_quad(
            quad, side_uv, tint, out + written, max - written);
        for (int k = 0; k < 3; ++k) {
            quad[0][k] = far[k] + qr[k] * .2f;
            quad[1][k] = origin[k] + qr[k] * .2f;
            quad[2][k] = origin[k] - qr[k] * .2f;
            quad[3][k] = far[k] - qr[k] * .2f;
        }
        written += er_guardian_beam_quad(
            quad, side_uv, tint, out + written, max - written);

        float cap_uv[4][2] = {
            {.5f,.5f}, {1,.5f}, {1,0}, {.5f,0}
        };
        const float cap_sign[4][2] = {
            {-.70710678f,.70710678f}, {.70710678f,.70710678f},
            {.70710678f,-.70710678f}, {-.70710678f,-.70710678f}
        };
        float cap_row = (g->ticks_existed & 1) ? .5f : 0.0f;
        for (int i = 0; i < 4; ++i) {
            cap_uv[i][1] += cap_row;
            for (int k = 0; k < 3; ++k)
                quad[i][k] = far[k] + .282f
                    * (pr[k] * cap_sign[i][0] + qr[k] * cap_sign[i][1]);
        }
        written += er_guardian_beam_quad(
            quad, cap_uv, tint, out + written, max - written);
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

typedef struct {
    double x, y, z;
    float scale, color_r, color_g, color_b, jitter_x, jitter_y;
    float sky_light, block_light;
} ErRecordedDigParticle;

static ErRecordedDigParticle er_recorded_dig_particles[32];
static int er_recorded_dig_particle_count;
static int er_recorded_dig_source_block;

void gm_recorded_dig_particles_clear(void) {
    er_recorded_dig_particle_count = 0;
    er_recorded_dig_source_block = 0;
}

void gm_recorded_dig_particles_set_source(int block_id) {
    er_recorded_dig_source_block = block_id;
}

int gm_recorded_dig_particles_source(void) {
    return er_recorded_dig_source_block;
}

int gm_recorded_dig_particle_add(double x, double y, double z,
                                 float scale, float color_r, float color_g,
                                 float color_b, float jitter_x,
                                 float jitter_y, float sky_light,
                                 float block_light) {
    if (er_recorded_dig_particle_count >= 32 || scale <= 0.0f
            || sky_light < 0.0f || sky_light > 15.0f
            || block_light < 0.0f || block_light > 15.0f) return 0;
    ErRecordedDigParticle *p =
        &er_recorded_dig_particles[er_recorded_dig_particle_count++];
    p->x = x; p->y = y; p->z = z; p->scale = scale;
    p->color_r = color_r; p->color_g = color_g; p->color_b = color_b;
    p->jitter_x = jitter_x; p->jitter_y = jitter_y;
    p->sky_light = sky_light; p->block_light = block_light;
    return 1;
}

int gm_recorded_dig_particles_count(void) {
    return er_recorded_dig_particle_count;
}

int gm_recorded_dig_particles_emit(int block_id,
                                   float view_yaw, float view_pitch,
                                   float lm_r, float lm_g, float lm_b,
                                   CrVertex *out, int max) {
    (void)lm_r; (void)lm_g; (void)lm_b;
    float yr = (180.0f - view_yaw) * ER_DEG2RAD;
    float pr = -view_pitch * ER_DEG2RAD;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    float bu0, bv0, bu1, bv1;
    bm_sprite_uv(bm_particle_sprite(block_id), &bu0, &bv0, &bu1, &bv1);
    float du = bu1 - bu0, dv = bv1 - bv0;
    int written = 0;
    for (int i = 0; i < er_recorded_dig_particle_count; ++i) {
        if (written + 6 > max) break;
        const ErRecordedDigParticle *p = &er_recorded_dig_particles[i];
        float u0 = bu0 + (p->jitter_x / 4.0f) * du;
        float u1 = bu0 + ((p->jitter_x + 1.0f) / 4.0f) * du;
        float v0 = bv0 + (p->jitter_y / 4.0f) * dv;
        float v1 = bv0 + ((p->jitter_y + 1.0f) / 4.0f) * dv;
        float half = 0.1f * p->scale;
        CrRgba tint = {
            (u8)(fmaxf(0.0f, fminf(1.0f, p->color_r))
                 * 255.0f + 0.5f),
            (u8)(fmaxf(0.0f, fminf(1.0f, p->color_g))
                 * 255.0f + 0.5f),
            (u8)(fmaxf(0.0f, fminf(1.0f, p->color_b))
                 * 255.0f + 0.5f),
            255,
        };
        int start = written;
        written += er_emit_billboard(
            (float)p->x, (float)p->y, (float)p->z, half,
            u0, v0, u1, v1, tint, cy, sy, cp, sp,
            out + written, max - written);
        for (int v = start; v < written; ++v) {
            out[v].light = p->sky_light;
            out[v].blk = p->block_light;
        }
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
        /* ModelSlime's +0.001 translation cancels prepareScale's -1.501
         * against the outer negative Y scale, leaving the 1.500 origin used
         * by emit_box. */
        float base_y = ents[e].y;
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

int gm_wither_aura_emit(const GmEntityView *ents, int n,
                        CrVertex *out, int max) {
    int written = 0;
    const CrMobSprite *sprite = &CR_MOB_SPRITES[CR_MOB_WITHER_ARMOR];
    for (int e = 0; e < n; ++e) {
        const GmEntityView *view = &ents[e];
        if (view->type != ER_TYPE_WITHER || view->health <= 0.0f
                || view->health > 150.0f || (view->flags & 4))
            continue;
        if (written + 18 * ER_VERTS_PER_BOX > max) break;
        ErPart parts[9];
        er_wither_pose(view, parts, CR_MOB_WITHER_ARMOR, 0.5f);
        float rad = (180.0f - view->yaw) * ER_DEG2RAD;
        float sc = er_wither_scale(view);
        int start = written;
        for (int p = 0; p < 9; ++p)
            written += emit_box(
                &parts[p], cosf(rad), sinf(rad), sc,
                view->x, view->y + 0.001f * sc, view->z,
                (CrRgba){128,128,128,255}, 15.0f, 15.0f,
                1.0f, 0.0f, out + written);
        float age = (float)view->ticks_existed + 1.0f;
        float du = er_mathhelper_cos(age * 0.02f) * 3.0f;
        float dv = age * 0.01f;
        for (int i = start; i < written; ++i) {
            out[i].uv.x =
                (out[i].uv.x * (float)CR_MOB_ATLAS_W - sprite->x0)
                / (float)sprite->w + du;
            out[i].uv.y =
                (out[i].uv.y * (float)CR_MOB_ATLAS_H - sprite->y0)
                / (float)sprite->h + dv;
            out[i].ao = 1.0f;
        }
        written = er_expand_twosided(out, start, written, max);
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
        case ER_TYPE_ZOMBIE_VILLAGER:
        case ER_TYPE_PIGMAN:   ww = 0.6f;  hh = 1.95f; break;
        case ER_TYPE_SKELETON: ww = 0.6f;  hh = 1.99f; break;
        case ER_TYPE_WITHER_SKELETON: ww = 0.7f; hh = 2.4f; break;
        case ER_TYPE_CREEPER:  ww = 0.6f;  hh = 1.7f;  break;
        case ER_TYPE_SPIDER:   ww = 1.4f;  hh = 0.9f;  break;
        case ER_TYPE_SILVERFISH: ww = 0.4f; hh = 0.3f; break;
        case ER_TYPE_ENDERMITE: ww = 0.4f; hh = 0.3f; break;
        case ER_TYPE_ENDERMAN: ww = 0.6f;  hh = 2.9f;  break;
        case ER_TYPE_WITCH:    ww = 0.6f;  hh = 1.95f; break;
        case ER_TYPE_VINDICATOR:
        case ER_TYPE_EVOKER:   ww = 0.6f;  hh = 1.95f; break;
        case ER_TYPE_VEX:      ww = 0.4f;  hh = 0.8f;  break;
        case ER_TYPE_EVOKER_FANGS: ww = 0.5f; hh = 0.8f; break;
        case ER_TYPE_SHEEP:    ww = 0.9f;  hh = 1.3f;  break;
        case ER_TYPE_COW:      ww = 0.9f;  hh = 1.4f;  break;
        case ER_TYPE_RABBIT:   ww = 0.4f;  hh = 0.5f;  break;
        case ER_TYPE_POLAR_BEAR: ww = 1.3f; hh = 1.4f; break;
        case ER_TYPE_PIG:      ww = 0.9f;  hh = 0.9f;  break;
        case ER_TYPE_CHICKEN:  ww = 0.4f;  hh = 0.7f;  break;
        case ER_TYPE_WOLF:     ww = 0.6f;  hh = 0.85f; break;
        case ER_TYPE_OCELOT:   ww = 0.6f;  hh = 0.7f;  break;
        case ER_TYPE_BAT:      ww = 0.5f;  hh = 0.9f;  break;
        case ER_TYPE_LLAMA:    ww = 0.9f;  hh = 1.87f; break;
        case ER_TYPE_LLAMA_SPIT: ww = 0.25f; hh = 0.25f; break;
        case ER_TYPE_SHULKER:  ww = 1.0f;  hh = 1.0f;  break;
        case ER_TYPE_SHULKER_BULLET: ww = 0.3125f; hh = 0.3125f; break;
        case ER_TYPE_SNOWMAN: ww = 0.7f; hh = 1.9f; break;
        case ER_TYPE_WITHER: ww = 0.9f; hh = 3.5f; break;
        case ER_TYPE_WITHER_SKULL: ww = 0.3125f; hh = 0.3125f; break;
        case ER_TYPE_HORSE:
        case ER_TYPE_DONKEY:
        case ER_TYPE_MULE:
        case ER_TYPE_SKELETON_HORSE:
        case ER_TYPE_ZOMBIE_HORSE: ww = 1.3964844f; hh = 1.6f; break;
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
            ErPart posed = m->parts[p];
            /* A cached spawner entity never ticks and has swingProgress 0,
             * but RenderLivingBase still supplies ageInTicks=partialTicks.
             * ModelZombie applies its idle arm yaw/roll and small age wave
             * after ModelBiped. Keep this local to the cached miniature;
             * live zombies use their recorded animation state elsewhere. */
            if ((sp[i].type == ER_TYPE_ZOMBIE ||
                 sp[i].type == ER_TYPE_ZOMBIE_VILLAGER ||
                 sp[i].type == ER_TYPE_PIGMAN) &&
                ((sp[i].type == ER_TYPE_ZOMBIE_VILLAGER &&
                  (p == 4 || p == 5)) ||
                 (sp[i].type != ER_TYPE_ZOMBIE_VILLAGER &&
                  (p == 2 || p == 3)))) {
                int right = sp[i].type == ER_TYPE_ZOMBIE_VILLAGER
                          ? p == 4 : p == 2;
                float age = sp[i].partial_ticks;
                posed.ay = right ? -0.1f : 0.1f;
                float roll = cosf(age * 0.09f) * 0.05f + 0.05f;
                float pitch = sinf(age * 0.067f) * 0.05f;
                posed.az = right ? roll : -roll;
                posed.ax += right ? pitch : -pitch;
            }
            const ErPart *q = &posed;
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
            /* TileEntityRendererDispatcher enables standard item lighting
             * before TileEntityMobSpawnerRenderer enters this affine stack. */
            written += er_aff_box_m(
                &pa, q->sprite, 1, q->mirror, 1, q->u, q->v,
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

CrTexture gm_guardian_beam_texture(void) {
    CrTexture t;
    t.w = CR_GUARDIAN_BEAM_W;
    t.h = CR_GUARDIAN_BEAM_H;
    t.texels = (const CrRgba *)CR_GUARDIAN_BEAM_RGBA;
    t.tile = 0;
    t.mip_levels = 0;
    for (int i = 0; i < 15; ++i) { t.mip[i] = 0; t.mipw[i] = 0; t.miph[i] = 0; }
    return t;
}

CrTexture gm_beacon_beam_texture(void) {
    CrTexture t;
    t.w = CR_BEACON_BEAM_W;
    t.h = CR_BEACON_BEAM_H;
    t.texels = (const CrRgba *)CR_BEACON_BEAM_RGBA;
    t.tile = 0;
    t.mip_levels = 0;
    for (int i = 0; i < 15; ++i) {
        t.mip[i] = 0;
        t.mipw[i] = 0;
        t.miph[i] = 0;
    }
    return t;
}

CrTexture gm_wither_armor_texture(void) {
    CrTexture t;
    t.w = CR_WITHER_ARMOR_W;
    t.h = CR_WITHER_ARMOR_H;
    t.texels = (const CrRgba *)CR_WITHER_ARMOR_RGBA;
    t.tile = 0;
    t.mip_levels = 0;
    for (int i = 0; i < 15; ++i) {
        t.mip[i] = 0;
        t.mipw[i] = 0;
        t.miph[i] = 0;
    }
    return t;
}
