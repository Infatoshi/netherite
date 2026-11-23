/* batchtick.c: the batched fast-mode tick, the model behind `engine.tick_scope: batched`
 * (prototyped 2026-10-24, DEVLOG; the port of /tmp/batchproto/batchproto.c into the tree).
 *
 * One thread steps many worlds in flat SoA arrays: the parts of the client tick a speedrun
 * policy touches, in the order the C engine runs them (player draws and the sprint
 * blockcrack; cp_on_living_update's jump, friction and move_flying; the pick, the dig and a
 * place; WorldClient.tick's draws, a nextInt(1), three nextInt(11) and the 1,000-cell scan
 * of func_147451_t). Each world's blocks are one flat int32 32x32x32 region and its 3x3x3
 * block cache one 27-int slot.
 *
 * The gate is this file's selftest plus the recorded comparison:
 * `make -C csrc/runtime batchtick-check` runs nwbt_selftest (the walk, sprint, jump, post
 * and drop values against vanilla's own arithmetic with their relative errors printed,
 * the dig rate, and the whole per-world state hashed -- every field and all 32,768 cells
 * -- equal on 1 and 32 threads after worlds_init) and then nwbt_compare (two stored
 * recordings under out/java/snapshots replayed by the model and by the engine from the
 * snapshot's own start state; the engine's per-tick rows are proved first by
 * out/native/test_snapshots, and then the model's per-tick divergence from them is
 * printed, not gated -- where and how much the model parts is the number the check
 * exists to give). That comparison is what found the two defects this file's physics
 * first had: the move input is 0.98F before travel (EntityLivingBase.onLivingUpdate
 * 2014-2015) and the step trial starts from the box as it was before the flat move
 * (Entity.moveEntity's var19), not from where the flat move left the player.
 *
 * What it does not model is listed in the DEVLOG entry: one flat region a world, no
 * sections or light maps, no water, ladders, sneak, food, damage or portals, one tool,
 * and the rest of the exact tick (server player, packets, entities, the cast).
 *
 * Build: gcc -O2 -fopenmp -o batchtick batchtick.c batchtick_check.c -lm -lz (the check's
 * binary; its own main is the bench).
 */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>
#include <time.h>
#include <sys/stat.h>
#include <zlib.h>
#include <omp.h>

#include "batchtick.h"

#define WX 32
#define WY 32
#define WZ 32
#define WVOL (WX * WY * WZ)

enum { MODE_MOVE = 0, MODE_PHYS = 1, MODE_FULL = 2 };

/* ---------------------------------------------------------------- blocks
 *
 * A world's cells are kind indices into BLK (not vanilla ids): the model only
 * reads a cell's solidity, its slipperiness and the damage the held tool does
 * to it a tick. The synthetic terrain uses the four kinds below; the recorded
 * comparison (nwbt_compare) adds the vanilla ids its window holds, each with
 * the hardness and material the oracle's blocks declare.
 *
 * The dig is vanilla 1.7.10's, not 1/hardness: Block.getPlayerRelativeBlockHardness
 * (oracle/src/block/Block.java:688-691) is
 *
 *     hardness < 0 ? 0 : canHarvestBlock(this)
 *         ? getCurrentPlayerStrVsBlock(this, true) / hardness / 30.0F
 *         : getCurrentPlayerStrVsBlock(this, false) / hardness / 100.0F
 *
 * and PlayerControllerMP.onPlayerDamageBlock adds that to curblockDamageMP each
 * tick the same block is hit. str comes from EntityPlayer.getCurrentPlayerStrVsBlock
 * (EntityPlayer.java:848), which is InventoryPlayer.func_146023_a
 * (InventoryPlayer.java:535-545) = ItemStack.func_150997_a (ItemStack.java:155-158)
 * = Item.func_150893_a (Item.java:459-462: 1.0F by default) and, for a tool,
 * ItemTool.func_150893_a (ItemTool.java:35-38) / ItemPickaxe.func_150893_a
 * (ItemPickaxe.java:24-27): a stone pickaxe's efficiencyOnProperMaterial is
 * 4.0 (EnumToolMaterial.STONE, Item.java:859) on Material.rock, iron and
 * anvil, and 1.0 on everything else. canHarvestBlock is
 * InventoryPlayer.func_146025_b (InventoryPlayer.java:658-668): the material
 * asks for no tool (Material.isToolNotRequired, Material.java:176-179), or the
 * held item's Item.func_150897_b (Item.java:540-543: false; ItemPickaxe's
 * override, ItemPickaxe.java:19-22, true on rock and iron).
 */
enum { K_AIR = 0, K_STONE = 1, K_DIRT = 2, K_GRASS = 3, K_MAX = 32 };
enum { BT_TOOL_HAND = 0, BT_TOOL_STONE_PICK = 1 };

struct blkdef { float hardness, slip, dmg; int solid; int vid; };

static struct blkdef BLK[K_MAX];
static int BLK_N;                    /* kinds in use */
static int KIND_OF[4096];            /* vanilla id -> kind, -1 until added */
static int BT_TOOL = BT_TOOL_STONE_PICK;

/* Material.rock, Material.iron, Material.anvil: the materials that ask for a
 * tool and that a pickaxe is the proper tool for (Material.java:9-11).
 * Vanilla ids only, the blocks a recording's window can hold. */
static int van_rock(int vid)
{
    switch (vid)
    {
    case 1: case 4: case 14: case 15: case 16: case 21: case 22: case 41: case 42: case 48: case 49:
    case 52: case 56: case 57: case 73: case 74: case 89: case 98: case 101: case 102: case 103:
    case 107: case 108: case 109: case 110: case 112: case 113: case 114: case 115: case 116: case 117:
    case 118: case 120: case 121: case 123: case 124: case 125: case 126: case 128: case 129: case 130:
    case 131: case 132: case 133: case 134: case 135: case 136: case 137: case 138: case 139: case 140:
    case 141: case 142: case 143: case 144: case 145: case 146: case 147: case 148: case 149: case 150:
    case 151: case 152: case 153: case 154: case 155: case 156: case 157: case 158: case 159: case 160:
    case 161: case 162: case 163: case 164: case 166: case 167: case 168: case 169: case 170: case 172:
    case 173: case 174:
        return 1;
    default:
        return 0;
    }
}

/* Block.getBlockHardness (Block.java:209-388 for the registry, -1 unbreakable) */
static float van_hardness(int vid)
{
    switch (vid)
    {
    case 0: return 0.0f;                       /* air */
    case 1: return 1.5f;                       /* Block.java:209 stone */
    case 2: return 0.6f;                       /* Block.java:210 grass */
    case 3: return 0.5f;                       /* Block.java:211 dirt */
    case 4: return 2.0f;                       /* cobblestone (stonebrick's Block, 212) */
    case 7: return -1.0f;                      /* bedrock, setBlockUnbreakable (217) */
    case 8: case 9: case 10: case 11: return 100.0f;  /* water, lava (218-221) */
    case 12: return 0.5f;                      /* Block.java:222 sand */
    case 13: return 0.6f;                      /* Block.java:223 gravel */
    case 14: case 15: case 16: return 3.0f;    /* ores (224-226) */
    case 17: return 2.0f;                      /* BlockLog's constructor: log (227) */
    case 18: return 0.2f;                      /* BlockLeaves' constructor: leaves (228) */
    case 20: return 0.3f;                      /* glass (230) */
    case 31: case 37: case 38: case 39: case 40: return 0.0f;   /* plants (242-251) */
    case 175: return 0.0f;                     /* double_plant (388) */
    default: return 1.5f;                      /* an id the table does not name */
    }
}

/* a block with a full collision box: everything except air, liquids and the
 * crossed plants (Block.getCollisionBoundingBoxFromPool returns null for those) */
static int van_solid(int vid)
{
    switch (vid)
    {
    case 0: case 6: case 8: case 9: case 10: case 11: case 27: case 28: case 30: case 31: case 32:
    case 37: case 38: case 39: case 40: case 50: case 51: case 55: case 59: case 63: case 65: case 66:
    case 69: case 70: case 72: case 75: case 76: case 77: case 78: case 83: case 90: case 93: case 94:
    case 104: case 105: case 106: case 111: case 115: case 127: case 132: case 141: case 142: case 143:
    case 147: case 148: case 149: case 150: case 157: case 164: case 171: case 175:
        return 0;
    default:
        return 1;
    }
}

/* Block.slipperiness: 0.6 everywhere, ice 0.98, slime 0.8 (Block.java's
 * slipperiness field, BlockIce/BlockSlime's setSlipperiness) */
static float van_slip(int vid) { return vid == 79 || vid == 174 ? 0.98f : vid == 165 ? 0.8f : 0.6f; }

/* Block.getPlayerRelativeBlockHardness (Block.java:688-691) with the tool's
 * str, two float divisions in vanilla's order */
static float van_dmg(int tool, int vid, float h)
{
    int harvest = van_rock(vid) ? (tool != BT_TOOL_HAND) : 1;
    float str = (tool != BT_TOOL_HAND && van_rock(vid)) ? 4.0f : 1.0f;

    if (h < 0.0f) return 0.0f;
    return harvest ? str / h / 30.0f : str / h / 100.0f;
}

static void blk_tool(int tool)
{
    BT_TOOL = tool;
    for (int k = 1; k < BLK_N; ++k) BLK[k].dmg = van_dmg(tool, BLK[k].vid, BLK[k].hardness);
}

/* a kind for a vanilla id (the table's first entry is air) */
static int blk_kind(int vid)
{
    if (vid < 0 || vid >= (int)(sizeof KIND_OF / sizeof *KIND_OF)) vid = 1;
    if (KIND_OF[vid] >= 0) return KIND_OF[vid];
    if (BLK_N >= K_MAX) return K_STONE;
    BLK[BLK_N].vid = vid;
    BLK[BLK_N].hardness = van_hardness(vid);
    BLK[BLK_N].slip = van_slip(vid);
    BLK[BLK_N].solid = van_solid(vid);
    BLK[BLK_N].dmg = van_dmg(BT_TOOL, vid, BLK[BLK_N].hardness);
    return KIND_OF[vid] = BLK_N++;
}

static void blk_reset(void)
{
    for (int i = 0; i < (int)(sizeof KIND_OF / sizeof *KIND_OF); ++i) KIND_OF[i] = -1;
    BLK_N = 1;
    BLK[K_AIR] = (struct blkdef){ 0.0f, 0.6f, 0.0f, 0, 0 };
    for (int i = 1; i < K_MAX; ++i) BLK[i] = (struct blkdef){ 0, 0, 0, 0, 0 };
    /* air, stone, dirt, grass: the synthetic terrain's four kinds (the
     * prototype's table, now carrying the vanilla hardness and dig rate) */
    if (blk_kind(1) != K_STONE || blk_kind(3) != K_DIRT || blk_kind(2) != K_GRASS)
    {
        fprintf(stderr, "batchtick: the default kinds are not 0..3\n");
        exit(1);
    }
    blk_tool(BT_TOOL_STONE_PICK);
}

/* MathHelper's sin table, built as Java builds it; mh_sin/mh_cos are
 * csrc/engine/jmath.h's */
static float MHS[65536];
static void jmath_init(void)
{
    for (int i = 0; i < 65536; ++i) MHS[i] = (float)sin(((double)i + 0.5) / 65536.0 * M_PI * 2.0);
}
static inline float mh_sin(float f) { return MHS[(int)(f * 10430.378f) & 65535]; }
static inline float mh_cos(float f) { return MHS[((int)(f * 10430.378f + 16384.0f)) & 65535]; }

/* java.util.Random, the engine's det_rng's own algorithm */
static inline uint32_t jnext(uint64_t *s, int bits)
{
    *s = (*s * 0x5DEECE66DULL + 0xBULL) & 0xFFFFFFFFFFFFULL;
    return (uint32_t)(*s >> (48 - bits));
}
static inline int jnext_int(uint64_t *s, int bound)
{
    if ((bound & -bound) == bound) return (int)(((long long)bound * (long long)jnext(s, 31)) >> 31);
    int bits, val;
    do {
        bits = (int)jnext(s, 31);
        val = bits % bound;
    } while (bits - val + (bound - 1) < 0);
    return val;
}
static inline float jnext_float(uint64_t *s) { return (float)(jnext(s, 24) / (double)(1 << 24)); }

struct worlds {
    int n;
    double *px, *py, *pz, *mx, *my, *mz;
    float *yaw, *pitch, *dig_dmg, *move_speed;
    uint8_t *on_ground, *sprinting, *digging;
    int32_t *dig_x, *dig_y, *dig_z;
    int16_t *atk_left;                       /* ticks of the attack still held */
    uint64_t *jrng, *arng;
    int32_t *cache;                          /* n * 27 */
    int32_t *cbx, *cby, *cbz;
    uint8_t *cok;
    int32_t *wblk;                           /* n * WVOL */
    uint64_t *ctr;                           /* n * 8: draws, breaks, places, jumps, ray steps, cache refills, ticks */
};

static inline int widx(int x, int y, int z) { return (y * WZ + z) * WX + x; }

static inline int32_t wget(const struct worlds *w, int i, int x, int y, int z)
{
    if (y < 0) return 1;
    if (x < 0 || x >= WX || y >= WY || z < 0 || z >= WZ) return 0;
    return w->wblk[(size_t)i * WVOL + widx(x, y, z)];
}
static inline void wset(struct worlds *w, int i, int x, int y, int z, int32_t v)
{
    if (x < 0 || x >= WX || y < 0 || y >= WY || z < 0 || z >= WZ) return;
    w->wblk[(size_t)i * WVOL + widx(x, y, z)] = v;
}

static void cache_refresh(struct worlds *w, int i, int bx, int by, int bz)
{
    w->cbx[i] = bx; w->cby[i] = by; w->cbz[i] = bz; w->cok[i] = 1;
    int32_t *c = &w->cache[(size_t)i * 27];
    int k = 0;
    for (int dy = -1; dy <= 1; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) c[k++] = wget(w, i, bx + dx, by + dy, bz + dz);
}

/* the 3x3x3 local block cache the prototype's physics and interactions read */
static inline int32_t cget(struct worlds *w, int i, int x, int y, int z)
{
    int dx = x - w->cbx[i], dy = y - w->cby[i], dz = z - w->cbz[i];
    if (!w->cok[i] || (unsigned)(dx + 1) > 2u || (unsigned)(dy + 1) > 2u || (unsigned)(dz + 1) > 2u)
    {
        cache_refresh(w, i, (int)floor(w->px[i]), (int)floor(w->py[i]), (int)floor(w->pz[i]));
        ++w->ctr[(size_t)i * 8 + 5];
        dx = x - w->cbx[i]; dy = y - w->cby[i]; dz = z - w->cbz[i];
        if ((unsigned)(dx + 1) > 2u || (unsigned)(dy + 1) > 2u || (unsigned)(dz + 1) > 2u)
            return wget(w, i, x, y, z);
    }
    return w->cache[(size_t)i * 27 + (dy + 1) * 9 + (dz + 1) * 3 + (dx + 1)];
}

static void worlds_alloc(struct worlds *w, int n)
{
    memset(w, 0, sizeof *w);
    w->n = n;
#define A(f, T, k) w->f = (T *)calloc((size_t)n * (k), sizeof(T)); if (!w->f) { fprintf(stderr, "oom %s\n", #f); exit(1); }
    A(px, double, 1); A(py, double, 1); A(pz, double, 1);
    A(mx, double, 1); A(my, double, 1); A(mz, double, 1);
    A(yaw, float, 1); A(pitch, float, 1); A(dig_dmg, float, 1); A(move_speed, float, 1);
    A(on_ground, uint8_t, 1); A(sprinting, uint8_t, 1); A(digging, uint8_t, 1);
    A(dig_x, int32_t, 1); A(dig_y, int32_t, 1); A(dig_z, int32_t, 1); A(atk_left, int16_t, 1);
    A(jrng, uint64_t, 1); A(arng, uint64_t, 1);
    A(cache, int32_t, 27); A(cbx, int32_t, 1); A(cby, int32_t, 1); A(cbz, int32_t, 1); A(cok, uint8_t, 1);
    A(wblk, int32_t, WVOL); A(ctr, uint64_t, 8);
#undef A
}

static void worlds_free(struct worlds *w)
{
    free(w->px); free(w->py); free(w->pz); free(w->mx); free(w->my); free(w->mz);
    free(w->yaw); free(w->pitch); free(w->dig_dmg); free(w->move_speed);
    free(w->on_ground); free(w->sprinting); free(w->digging);
    free(w->dig_x); free(w->dig_y); free(w->dig_z); free(w->atk_left); free(w->jrng); free(w->arng);
    free(w->cache); free(w->cbx); free(w->cby); free(w->cbz); free(w->cok);
    free(w->wblk); free(w->ctr);
}

/* flat terrain at y = 4 (grass over 0..3 stone) with a one-block stone post
 * every 7 blocks so the policy meets steps and blocks to break */
static void worlds_init(struct worlds *w, int flat)
{
    for (int i = 0; i < w->n; ++i)
    {
        int32_t *b = &w->wblk[(size_t)i * WVOL];
        for (int y = 0; y < WY; ++y)
            for (int z = 0; z < WZ; ++z)
                for (int x = 0; x < WX; ++x)
                    b[widx(x, y, z)] = (y <= 3) ? 1 : (y == 4 ? ((!flat && x % 7 == 3 && z % 7 == 5) ? 1 : 3) : 0);
        w->px[i] = 8.5 + (double)((i * 2654435761u) % 12u);
        w->py[i] = 5.0;
        w->pz[i] = 8.5 + (double)((i * 40503u) % 12u);
        w->mx[i] = w->my[i] = w->mz[i] = 0.0;
        w->yaw[i] = (float)((i * 37) % 360);
        w->pitch[i] = 0.0f;
        w->on_ground[i] = 1;
        w->sprinting[i] = 0;
        w->digging[i] = 0;
        w->dig_dmg[i] = 0.0f;
        w->move_speed[i] = 0.10000000149011612;
        w->dig_x[i] = w->dig_y[i] = w->dig_z[i] = -1;
        w->atk_left[i] = 0;
        w->jrng[i] = (uint64_t)(i + 1) * 0x9E3779B97F4A7C15ULL ^ 0x5DEECE66DULL;
        w->arng[i] = 0x2545F4914F6CDD1DULL * (uint64_t)(i + 1) ^ 0x9E3779B97F4A7C15ULL;
        cache_refresh(w, i, (int)w->px[i], 5, (int)w->pz[i]);
    }
}

/* vanilla's calculateX/Y/ZOffset over the boxes overlapping the swept AABB,
 * read through the 3x3x3 cache */
static double clip_move(struct worlds *w, int i, int axis, double d)
{
    if (d == 0.0) return 0.0;
    double x = w->px[i], y = w->py[i], z = w->pz[i];
    double x0 = x - 0.3, x1 = x + 0.3, y0 = y, y1 = y + 1.8, z0 = z - 0.3, z1 = z + 0.3;
    double sx0 = x0 + (axis == 0 && d < 0 ? d : 0.0), sx1 = x1 + (axis == 0 && d > 0 ? d : 0.0);
    double sy0 = y0 + (axis == 1 && d < 0 ? d : 0.0), sy1 = y1 + (axis == 1 && d > 0 ? d : 0.0);
    double sz0 = z0 + (axis == 2 && d < 0 ? d : 0.0), sz1 = z1 + (axis == 2 && d > 0 ? d : 0.0);
    int xi0 = (int)floor(sx0), xi1 = (int)floor(sx1 - 1e-9);
    int yi0 = (int)floor(sy0), yi1 = (int)floor(sy1 - 1e-9);
    int zi0 = (int)floor(sz0), zi1 = (int)floor(sz1 - 1e-9);

    for (int cy = yi0; cy <= yi1; ++cy)
        for (int cz = zi0; cz <= zi1; ++cz)
            for (int cx = xi0; cx <= xi1; ++cx)
            {
                int32_t id = cget(w, i, cx, cy, cz);
                if (!BLK[id].solid) continue;
                double bx0 = cx, bx1 = cx + 1.0, by0 = cy, by1 = cy + 1.0, bz0 = cz, bz1 = cz + 1.0;
                if (!(bx0 < sx1 && bx1 > sx0 && by0 < sy1 && by1 > sy0 && bz0 < sz1 && bz1 > sz0)) continue;
                /* the block is the receiver, the player the argument
                 * (AxisAlignedBB.calculateX/Y/ZOffset) */
                if (axis == 0)
                {
                    if (d > 0 && bx0 >= x1) { double dd = bx0 - x1; if (dd < d) d = dd; }
                    if (d < 0 && bx1 <= x0) { double dd = bx1 - x0; if (dd > d) d = dd; }
                }
                else if (axis == 1)
                {
                    if (d > 0 && by0 >= y1) { double dd = by0 - y1; if (dd < d) d = dd; }
                    if (d < 0 && by1 <= y0) { double dd = by1 - y0; if (dd > d) d = dd; }
                }
                else
                {
                    if (d > 0 && bz0 >= z1) { double dd = bz0 - z1; if (dd < d) d = dd; }
                    if (d < 0 && bz1 <= z0) { double dd = bz1 - z0; if (dd > d) d = dd; }
                }
            }
    return d;
}

/* Entity.moveEntity's order: y, then x, then z, then the step attempt */
static void move_player(struct worlds *w, int i, double dx, double dy, double dz)
{
    int was_ground = w->on_ground[i];
    double yv = clip_move(w, i, 1, dy);
    int hit_y = (yv != dy);
    w->py[i] += yv;
    double xv = clip_move(w, i, 0, dx);
    int hit_x = (xv != dx);
    w->px[i] += xv;
    double zv = clip_move(w, i, 2, dz);
    int hit_z = (zv != dz);
    w->pz[i] += zv;

    if (hit_y) w->my[i] = 0.0;
    if (hit_x) w->mx[i] = 0.0;
    if (hit_z) w->mz[i] = 0.0;

    if ((hit_x || hit_z) && (was_ground || (hit_y && dy < 0)))
    {
        /* the flat move stands; the step is a trial from the box as it was
         * before the flat move (Entity.moveEntity's var19, restored with
         * setBB before the trial's offsets), not from where the flat move
         * left the player -- from there the trial would add the horizontal
         * move twice on the ticks it is taken, which is what the recorded
         * comparison caught at the first jump into a step (t=40 of
         * walk300-s1: 0.6 of y and a doubling of the step's x/z). */
        double fx = w->px[i], fy = w->py[i], fz = w->pz[i];
        double ox = w->px[i] - xv, oy = w->py[i] - yv, oz = w->pz[i] - zv;
        double omx = w->mx[i], omy = w->my[i], omz = w->mz[i];
        w->px[i] = ox; w->py[i] = oy; w->pz[i] = oz;
        double up = clip_move(w, i, 1, 0.6);
        w->py[i] += up;
        double nx = clip_move(w, i, 0, dx);
        w->px[i] += nx;
        double nz = clip_move(w, i, 2, dz);
        w->pz[i] += nz;
        double down = clip_move(w, i, 1, yv - up);
        w->py[i] += down;
        if (nx * nx + nz * nz > xv * xv + zv * zv + 1e-12)
        {
            if (nx != dx) w->mx[i] = 0.0;
            if (nz != dz) w->mz[i] = 0.0;
            if (down != yv - up) { w->my[i] = 0.0; hit_y = 1; }
            hit_x = (nx != dx); hit_z = (nz != dz);
        }
        else
        {
            w->px[i] = fx; w->py[i] = fy; w->pz[i] = fz;
            w->mx[i] = omx; w->my[i] = omy; w->mz[i] = omz;
        }
    }

    if (hit_y && dy < 0) w->on_ground[i] = 1;
    else if (hit_y && dy > 0) { /* a ceiling leaves the flag */ }
    else w->on_ground[i] = 0;

    int bx = (int)floor(w->px[i]), by = (int)floor(w->py[i]), bz = (int)floor(w->pz[i]);
    if (bx != w->cbx[i] || by != w->cby[i] || bz != w->cbz[i]) cache_refresh(w, i, bx, by, bz);
}
/* PlayerControllerMP.onPlayerDamageBlock's accumulation: the block's
 * getPlayerRelativeBlockHardness a tick (BLK[kind].dmg, built from vanilla's
 * str/hardness/30 or /100 above), the break at 1.0, the same block's damage
 * kept and a new block's restarted. The prototype's one tool is a stone
 * pickaxe: stone 4/1.5/30 = 0.0889 a tick (11.25 ticks), dirt 1/0.5/30 =
 * 0.0667 (15), grass 1/0.6/30 = 0.0556 (18). */
static void tick_dig(struct worlds *w, int i, int tx, int ty, int tz, int want_place)
{
    int32_t id = cget(w, i, tx, ty, tz);
    if (!w->digging[i])
    {
        if (BLK[id].solid)
        {
            w->digging[i] = 1;
            w->dig_x[i] = tx; w->dig_y[i] = ty; w->dig_z[i] = tz;
            w->dig_dmg[i] = 0.0f;
        }
    }
    else
    {
        if (tx != w->dig_x[i] || ty != w->dig_y[i] || tz != w->dig_z[i])
        {
            /* the abandon and the new click: one damage this tick */
            w->dig_x[i] = tx; w->dig_y[i] = ty; w->dig_z[i] = tz;
            w->dig_dmg[i] = 0.0f;
            id = cget(w, i, tx, ty, tz);
        }
        if (!BLK[id].solid) w->digging[i] = 0;
        else
        {
            w->dig_dmg[i] += BLK[id].dmg;   /* vanilla's str/hardness/30 a tick */
            if (w->dig_dmg[i] >= 1.0f)
            {
                wset(w, i, tx, ty, tz, 0);
                cache_refresh(w, i, w->cbx[i], w->cby[i], w->cbz[i]);
                w->dig_dmg[i] = 0.0f;
                w->digging[i] = 0;
                ++w->ctr[(size_t)i * 8 + 1];
                /* the drop's draws on the world's Random */
                (void)jnext_int(&w->jrng[i], 3);
                (void)jnext_float(&w->jrng[i]);
                w->ctr[(size_t)i * 8] += 2;
            }
        }
    }
    if (want_place && BLK[id].solid)
    {
        int px = tx, py = ty + 1, pz = tz;
        if (!BLK[cget(w, i, px, py, pz)].solid)
        {
            wset(w, i, px, py, pz, K_DIRT);
            cache_refresh(w, i, w->cbx[i], w->cby[i], w->cbz[i]);
            ++w->ctr[(size_t)i * 8 + 2];
        }
    }
}

/* one tick's input: the move keys, the look (degrees) and the buttons */
struct bt_in { float forward, strafe, yaw, pitch; int jump, sprint, attack, place; };

/* the tick from a given input (the synthetic policy's or a recording's) */
static void tick_world_in(struct worlds *w, int i, int mode, const struct bt_in *in)
{
    ++w->ctr[(size_t)i * 8 + 6];
    w->yaw[i] = in->yaw;
    w->pitch[i] = in->pitch;
    float forward = in->forward, strafe = in->strafe;
    int want_jump = in->jump, want_attack = in->attack, want_place = in->place;

    /* ---------- EntityLivingBase.onEntityUpdate: the sprint blockcrack draws */
    if (w->sprinting[i])
    {
        (void)jnext_float(&w->jrng[i]);
        (void)jnext_float(&w->jrng[i]);
        w->ctr[(size_t)i * 8] += 2;
    }

    /* sprint gates: updatePlayerMoveState + EntityPlayerSP.onLivingUpdate
     * (the key held and moving forward; hunger and the collision stop are
     * not modelled) */
    w->sprinting[i] = (uint8_t)(in->sprint && forward >= 0.8f);


    uint64_t r = w->arng[i];
    r ^= r << 13; r ^= r >> 7; r ^= r << 17;
    w->arng[i] = r;
    /* ------------------------------------------------- the living update */
    float jmf = 0.02f;
    if (w->sprinting[i]) jmf = (float)((double)jmf + (double)0.02f * 0.3);

    if (want_jump && w->on_ground[i])
    {
        w->my[i] = 0.41999998688697815; /* jump_common */
        if (w->sprinting[i])
        {
            float var1 = w->yaw[i] * 0.017453292F;
            w->mx[i] -= (double)(mh_sin(var1) * 0.2F);
            w->mz[i] += (double)(mh_cos(var1) * 0.2F);
        }
        ++w->ctr[(size_t)i * 8 + 3];
    }

    int bx = (int)floor(w->px[i]), by = (int)floor(w->py[i]), bz = (int)floor(w->pz[i]);
    float slip = 1.0f;
    if (w->on_ground[i]) slip = BLK[cget(w, i, bx, by - 1, bz)].slip;
    float v3 = slip * 0.91F;
    float v4 = 0.16277136F / (v3 * v3 * v3);
    float v5 = w->on_ground[i] ? (float)((double)w->move_speed[i] * (double)v4) : jmf;

    /* EntityLivingBase.onLivingUpdate (oracle/src/entity/EntityLivingBase.java:2014-2015):
     * the move input is scaled by 0.98F before travel. With a full input that
     * is a 2% gentler move (move_flying's normalisation clamps the length to
     * 1.0F, so the 0.98 is not undone) -- the drift the recorded comparison
     * found, and the reason the walk step is 0.2158 and not 0.2203. */
    forward *= 0.98F;
    strafe *= 0.98F;

    {   /* move_flying (player.c:380) */
        float f4 = strafe * strafe + forward * forward;
        if (f4 >= 1.0E-4F)
        {
            f4 = sqrtf(f4);
            if (f4 < 1.0F) f4 = 1.0F;
            f4 = v5 / f4;
            float s = strafe * f4, f = forward * f4;
            float sy = mh_sin((float)w->yaw[i] * (float)M_PI / 180.0F);
            float cy = mh_cos((float)w->yaw[i] * (float)M_PI / 180.0F);
            w->mx[i] += (double)(s * cy - f * sy);
            w->mz[i] += (double)(f * cy + s * sy);
        }
    }

    slip = 1.0f;
    if (w->on_ground[i]) slip = BLK[cget(w, i, bx, by - 1, bz)].slip;
    v3 = slip * 0.91F;

    move_player(w, i, w->mx[i], w->my[i], w->mz[i]);

    w->my[i] -= 0.08;
    w->my[i] *= 0.9800000190734863;
    w->mx[i] *= (double)v3;
    w->mz[i] *= (double)v3;

    if (mode == MODE_MOVE) return;

    /* -------------------------------------------------------------- pick */
    double ex = w->px[i], ey = w->py[i] + 1.6200000047683716, ez = w->pz[i];
    double cp = cos((double)(-w->pitch[i]) * M_PI / 180.0), sp = sin((double)(-w->pitch[i]) * M_PI / 180.0);
    double cyaw = cos((double)(-w->yaw[i]) * M_PI / 180.0), syaw = sin((double)(-w->yaw[i]) * M_PI / 180.0);
    double dirx = syaw * cp, diry = sp, dirz = cyaw * cp;
    int32_t tx = -1, ty = -1, tz = -1;
    for (double t = 0.2; t <= 4.5; t += 0.2)
    {
        int cx = (int)floor(ex + dirx * t), cy = (int)floor(ey + diry * t), cz = (int)floor(ez + dirz * t);
        ++w->ctr[(size_t)i * 8 + 4];
        if (BLK[wget(w, i, cx, cy, cz)].solid) { tx = cx; ty = cy; tz = cz; break; }
    }

    /* --------------------------------------------------------- dig / place */
    if (tx >= 0)
    {
        if (!want_attack) w->digging[i] = 0;
        tick_dig(w, i, tx, ty, tz, want_place);
    }
    else w->digging[i] = 0;

    if (mode == MODE_PHYS) return;

    /* ----------------------------- WorldClient.tick: cp_world_tick's draws */
    (void)jnext_int(&w->jrng[i], 1);
    (void)jnext_int(&w->jrng[i], 11);
    (void)jnext_int(&w->jrng[i], 11);
    (void)jnext_int(&w->jrng[i], 11);
    w->ctr[(size_t)i * 8] += 4;
    int32_t *b = &w->wblk[(size_t)i * WVOL];
    int ipx = (int)floor(w->px[i]), ipy = (int)floor(w->py[i]), ipz = (int)floor(w->pz[i]);
    int draws = 0;
    for (int k = 0; k < 1000; ++k)
    {
        int x = ipx + jnext_int(&w->jrng[i], 16); x -= jnext_int(&w->jrng[i], 16);
        int y = ipy + jnext_int(&w->jrng[i], 16); y -= jnext_int(&w->jrng[i], 16);
        int z = ipz + jnext_int(&w->jrng[i], 16); z -= jnext_int(&w->jrng[i], 16);
        draws += 6;
        int32_t id;
        if (x < 0 || x >= WX || y < 0 || y >= WY || z < 0 || z >= WZ) id = 0;
        else id = b[widx(x, y, z)];
        if (!BLK[id].solid)
        {
            int d = jnext_int(&w->jrng[i], 8);
            ++draws;
            if (d > y)
            {
                (void)jnext_float(&w->jrng[i]);
                (void)jnext_float(&w->jrng[i]);
                (void)jnext_float(&w->jrng[i]);
                draws += 3;
            }
        }
    }
    w->ctr[(size_t)i * 8] += (uint64_t)draws;
}

/* the synthetic policy: a speedrun's shape -- walk, turn, look down at the
 * ground ahead (the crosshair on a block within reach), a jump now and then,
 * and now and then stop and dig. Vanilla takes 11.25 ticks on stone with a
 * stone pickaxe and keeps the damage only while the same block is hit, so a
 * walk that keeps moving never breaks what it aims at (the pick's target
 * moves with the player): a dig burst stands still, holds the crosshair where
 * it is and holds the attack for 10 to 41 ticks, then walks on. A place now
 * and then. The inputs are the model's; the state is tick_world_in's. */
static void tick_world(struct worlds *w, int i, int mode)
{
    uint64_t r = w->arng[i];
    r ^= r << 13; r ^= r >> 7; r ^= r << 17;
    w->arng[i] = r;

    if (w->atk_left[i] > 0) --w->atk_left[i];
    else if (((r >> 20) & 15) == 0) w->atk_left[i] = (int16_t)(10 + ((r >> 24) & 31));

    struct bt_in in;
    memset(&in, 0, sizeof in);
    in.attack = w->atk_left[i] > 0;
    in.jump = !in.attack && ((r >> 9) & 7) == 0;
    in.place = ((r >> 22) & 7) == 0;
    in.forward = (r & 7) ? 1.0f : 0.0f;
    in.strafe = ((r >> 3) & 7) == 0 ? (((r >> 6) & 1) ? 1.0f : -1.0f) : 0.0f;
    if (in.attack) in.forward = 0.0f;
    else
    {
        w->yaw[i] += (float)((int)((r >> 12) & 15) - 8) * 0.6f;
        w->pitch[i] = 35.0f + (float)((int)((r >> 16) & 7) - 3) * 5.0f; /* + is down */
    }
    in.sprint = in.forward >= 0.8f;     /* the policy's own gate: walk speed */
    in.yaw = w->yaw[i];
    in.pitch = w->pitch[i];
    tick_world_in(w, i, mode, &in);
}

/* FNV-1a over 8-byte words */
static inline uint64_t hw(uint64_t h, uint64_t v) { h ^= v; return h * 1099511628211ULL; }

/* The whole state: every field of every world and every one of the 32,768
 * cells of its region, not a 33-cell sample of 32,768 (the old `k += 1021`
 * stride read 33 blocks a world: 0.1%, and it skipped the cells a dig or a
 * physics step is most likely to change). */
static uint64_t state_hash(const struct worlds *w)
{
    uint64_t h = 1469598103934665603ULL;
    for (int i = 0; i < w->n; ++i)
    {
        uint64_t b[6];
        memcpy(&b[0], &w->px[i], 8); memcpy(&b[1], &w->py[i], 8); memcpy(&b[2], &w->pz[i], 8);
        memcpy(&b[3], &w->mx[i], 8); memcpy(&b[4], &w->my[i], 8); memcpy(&b[5], &w->mz[i], 8);
        for (int k = 0; k < 6; ++k) h = hw(h, b[k]);
        uint32_t f[4];
        memcpy(&f[0], &w->yaw[i], 4); memcpy(&f[1], &w->pitch[i], 4);
        memcpy(&f[2], &w->dig_dmg[i], 4); memcpy(&f[3], &w->move_speed[i], 4);
        for (int k = 0; k < 4; ++k) h = hw(h, f[k]);
        h = hw(h, (uint64_t)w->on_ground[i] | ((uint64_t)w->sprinting[i] << 8) | ((uint64_t)w->digging[i] << 16));
        h = hw(h, (uint64_t)(uint32_t)w->dig_x[i] | ((uint64_t)(uint32_t)w->dig_y[i] << 32));
        h = hw(h, (uint64_t)(uint32_t)w->dig_z[i]);
        h = hw(h, (uint64_t)(uint16_t)w->atk_left[i]);
        h = hw(h, w->jrng[i]);
        h = hw(h, w->arng[i]);
        h = hw(h, (uint64_t)(uint32_t)w->cbx[i] | ((uint64_t)(uint32_t)w->cby[i] << 32));
        h = hw(h, (uint64_t)(uint32_t)w->cbz[i] | ((uint64_t)w->cok[i] << 32));
        for (int k = 0; k < 27; ++k) h = hw(h, (uint64_t)(uint32_t)w->cache[(size_t)i * 27 + k]);
        /* the constants are whole: the region, two cells a word */
        const uint64_t *blk = (const uint64_t *)(const void *)&w->wblk[(size_t)i * WVOL];
        for (int k = 0; k < WVOL / 2; ++k) h = hw(h, blk[k]);
        for (int k = 0; k < 8; ++k) h = hw(h, w->ctr[(size_t)i * 8 + k]);
    }
    return h;
}

/* what differs between two world sets: the fields that are not equal, in how
 * many worlds, and the first world and field that part. A hash says "they
 * differ"; this says where. */
struct sdiff { int worlds; long long fields; int first_world; const char *first_field; };

static void state_diff(const struct worlds *a, const struct worlds *b, struct sdiff *d)
{
    d->worlds = 0; d->fields = 0; d->first_world = -1; d->first_field = "none";
    if (a->n != b->n) { d->first_world = 0; d->first_field = "n"; d->fields = 1; return; }
    unsigned char *bad = (unsigned char *)calloc((size_t)a->n, 1);
    if (!bad) return;
#define SDC(field) \
    do { for (int i = 0; i < a->n; ++i) if (a->field[i] != b->field[i]) { ++d->fields; bad[i] = 1; \
             if (d->first_world < 0) { d->first_world = i; d->first_field = #field; } } } while (0)
    SDC(px); SDC(py); SDC(pz);
    SDC(mx); SDC(my); SDC(mz);
    SDC(yaw); SDC(pitch); SDC(dig_dmg); SDC(move_speed);
    SDC(on_ground); SDC(sprinting); SDC(digging);
    SDC(dig_x); SDC(dig_y); SDC(dig_z); SDC(atk_left);
    SDC(jrng); SDC(arng);
    SDC(cbx); SDC(cby); SDC(cbz); SDC(cok);
#undef SDC
    for (int i = 0; i < a->n; ++i)
    {
        if (memcmp(&a->cache[(size_t)i * 27], &b->cache[(size_t)i * 27], 27 * sizeof(int32_t)))
        { ++d->fields; bad[i] = 1; if (d->first_world < 0) { d->first_world = i; d->first_field = "cache"; } }
        if (memcmp(&a->wblk[(size_t)i * WVOL], &b->wblk[(size_t)i * WVOL], WVOL * sizeof(int32_t)))
        { ++d->fields; bad[i] = 1; if (d->first_world < 0) { d->first_world = i; d->first_field = "wblk"; } }
        if (memcmp(&a->ctr[(size_t)i * 8], &b->ctr[(size_t)i * 8], 8 * sizeof(uint64_t)))
        { ++d->fields; bad[i] = 1; if (d->first_world < 0) { d->first_world = i; d->first_field = "ctr"; } }
    }
    for (int i = 0; i < a->n; ++i) d->worlds += bad[i];
    free(bad);
}

static double now_s(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}

/* one timed run: chunks of worlds, one chunk a thread, every tick inside the
 * parallel region */
static double run_ticks(struct worlds *w, int threads, int ticks, int mode)
{
    double t0 = now_s();
#pragma omp parallel num_threads(threads)
    {
        int tid = omp_get_thread_num(), nth = omp_get_num_threads();
        int chunk = (w->n + nth - 1) / nth;
        int lo = tid * chunk, hi = lo + chunk;
        if (hi > w->n) hi = w->n;
        for (int t = 0; t < ticks; ++t)
            for (int i = lo; i < hi; ++i) tick_world(w, i, mode);
    }
    return now_s() - t0;
}
/* -------------------------------------------------------------- selftest */

/* the same physics calls the tick makes, with the input held: forward, an
 * optional jump, no dig; returns the horizontal speed */
static double step_fixed(struct worlds *w, int i, float forward, int jump)
{
    if (jump && w->on_ground[i])
    {
        w->my[i] = 0.41999998688697815;
        if (w->sprinting[i])
        {
            float var1 = w->yaw[i] * 0.017453292F;
            w->mx[i] -= (double)(mh_sin(var1) * 0.2F);
            w->mz[i] += (double)(mh_cos(var1) * 0.2F);
        }
    }
    float jmf = 0.02f;
    if (w->sprinting[i]) jmf = (float)((double)jmf + (double)0.02f * 0.3);
    int bx = (int)floor(w->px[i]), by = (int)floor(w->py[i]), bz = (int)floor(w->pz[i]);
    float slip = w->on_ground[i] ? BLK[cget(w, i, bx, by - 1, bz)].slip : 1.0f;
    float v3 = slip * 0.91F;
    float v4 = 0.16277136F / (v3 * v3 * v3);
    float v5 = w->on_ground[i] ? (float)((double)w->move_speed[i] * (double)v4) : jmf;
    forward *= 0.98F;                    /* EntityLivingBase.onLivingUpdate:2014 */
    float f4 = sqrtf(forward * forward);
    if (f4 >= 1.0E-4F)
    {
        if (f4 < 1.0F) f4 = 1.0F;
        f4 = v5 / f4;
        float f = forward * f4;
        float sy = mh_sin((float)w->yaw[i] * (float)M_PI / 180.0F);
        float cy = mh_cos((float)w->yaw[i] * (float)M_PI / 180.0F);
        w->mx[i] += (double)(-f * sy);
        w->mz[i] += (double)(f * cy);
    }
    slip = w->on_ground[i] ? BLK[cget(w, i, bx, by - 1, bz)].slip : 1.0f;
    v3 = slip * 0.91F;
    move_player(w, i, w->mx[i], w->my[i], w->mz[i]);
    w->my[i] -= 0.08;
    w->my[i] *= 0.9800000190734863;
    w->mx[i] *= (double)v3;
    w->mz[i] *= (double)v3;
    return sqrt(w->mx[i] * w->mx[i] + w->mz[i] * w->mz[i]);
}

static int near_(const char *what, double got, double want, double tol)
{
    int ok = fabs(got - want) <= tol;
    printf("%-10s %.6f (want %.6f +/- %g) %s\n", what, got, want, tol, ok ? "ok" : "FAIL");
    return ok;
}

/* The same comparison as a relative error, so a drift of any size shows: what
 * the old sprint test's 2e-3 absolute tolerance hid was 0.35% (0.287339 for
 * 0.286343), and it was hiding an off-the-world measurement, not just slack. */
static int near_rel(const char *what, double got, double want, double reltol)
{
    double rel = want != 0.0 ? fabs(got - want) / fabs(want) : fabs(got);
    int ok = rel <= reltol;
    printf("%-10s %.17g (want %.17g, rel err %.3e <= %g) %s\n", what, got, want, rel, reltol, ok ? "ok" : "FAIL");
    return ok;
}

/* The step one tick gives at steady state on the ground, from vanilla's own
 * arithmetic and nothing else: v3 = slip * 0.91F, v4 = 0.16277136F / v3^3,
 * v5 = move_speed * v4 (a float, as the tick takes it), the motion before the
 * move v3 * v5 / (1 - v3) (the previous ticks' friction), and the tick's
 * displacement that motion plus v5. Compared against a measured step it is the
 * closed form of the same float computation, so the two agree to double
 * rounding (~1e-11) or something in the tick is wrong. */
static double ref_step(double move_speed, float yaw)
{
    float v3 = BLK[K_GRASS].slip * 0.91F;
    float v4 = 0.16277136F / (v3 * v3 * v3);
    float v5 = (float)(move_speed * (double)v4);
    /* the velocity one tick adds: move_flying's f = forward * f4 with a full
     * forward key (0.98 after EntityLivingBase.onLivingUpdate:2014, which
     * move_flying's clamp to a length of 1.0F does not undo), in float as the
     * tick multiplies it */
    float inc = (float)(0.98F * v5);
    /* the direction: -sin(yaw) x, +cos(yaw) z out of MathHelper's table, and
     * the step's length is (inc + the motion the previous ticks' friction
     * left) times that vector's length */
    float sy = mh_sin((float)yaw * (float)M_PI / 180.0F);
    float cy = mh_cos((float)yaw * (float)M_PI / 180.0F);
    double steady = (double)v3 * (double)inc / (1.0 - (double)v3);
    return (steady + (double)inc) * sqrt((double)sy * (double)sy + (double)cy * (double)cy);
}

/* the distance one step_fixed call moved the player */
static double step_dist(struct worlds *w, int i)
{
    double x0 = w->px[i], z0 = w->pz[i];
    step_fixed(w, i, 1.0f, 0);
    return sqrt((w->px[i] - x0) * (w->px[i] - x0) + (w->pz[i] - z0) * (w->pz[i] - z0));
}

/* The gate: the numbers vanilla's arithmetic gives for the physics it models, the post and the
 * drop, and the same per-world state whether one thread or 32 stepped it. */
int nwbt_selftest(void)
{
    struct worlds w;
    int ok = 1;
    jmath_init();
    blk_reset();
    double vw = 0.0;
    worlds_alloc(&w, 1);
    worlds_init(&w, 1);
    w.px[0] = 16.5; w.pz[0] = 16.5; w.py[0] = 5.0; w.yaw[0] = 90.0f; w.pitch[0] = 0.0f;
    cache_refresh(&w, 0, 16, 5, 16);
    for (int t = 0; t < 60; ++t) vw = step_fixed(&w, 0, 1.0f, 0);
    (void)vw;
    double moved = step_dist(&w, 0);
    ok &= near_rel("walk", moved, ref_step(0.10000000149011612, 90.0f), 1e-12);

    /* The sprint step, measured while the player is still on the world: from
     * x 16.5 the old 80-tick run left the flat region at tick 60 (x < 0 is air,
     * the player falls) and measured an air step, 0.35% over the sprint step.
     * From 28.5, 40 ticks stay on the ground (x 28.5 -> 17.1). The reference
     * is the same closed form with the sprint boost on the attribute
     * (0.10000000149011612 * (1 + 0.30000001192092896), the modifier's
     * operation 2), not a rounded 1.3 multiple of the walking step. */
    worlds_init(&w, 1);
    w.px[0] = 30.5; w.pz[0] = 16.5; w.py[0] = 5.0; w.yaw[0] = 90.0f;
    cache_refresh(&w, 0, 30, 5, 16);
    w.move_speed[0] = 0.10000000149011612 * (1.0 + 0.30000001192092896);
    w.sprinting[0] = 1;
    for (int t = 0; t < 90; ++t) vw = step_fixed(&w, 0, 1.0f, 0);
    moved = step_dist(&w, 0);
    ok &= near_rel("sprint", moved, ref_step(0.10000000149011612 * (1.0 + 0.30000001192092896), 90.0f), 1e-12);
    ok &= near_("sprint/on-ground", w.on_ground[0], 1, 0);

    worlds_init(&w, 1);
    w.px[0] = 16.5; w.pz[0] = 16.5; w.py[0] = 5.0; w.yaw[0] = 90.0f;
    cache_refresh(&w, 0, 16, 5, 16);
    double apex = 5.0;
    for (int t = 0; t < 30; ++t)
    {
        step_fixed(&w, 0, 0.0f, t == 0);
        if (w.py[0] > apex) apex = w.py[0];
    }
    ok &= near_("jump apex", apex - 5.0, 1.252203, 2e-4);

    worlds_init(&w, 0);
    wset(&w, 0, 12, 5, 16, 1);            /* a one-block stone post ahead */
    w.px[0] = 16.5; w.pz[0] = 16.5; w.py[0] = 5.0; w.yaw[0] = 90.0f;
    w.mx[0] = w.my[0] = w.mz[0] = 0.0;
    cache_refresh(&w, 0, 16, 5, 16);
    w.move_speed[0] = 0.13;
    w.sprinting[0] = 1;
    for (int t = 0; t < 60; ++t) step_fixed(&w, 0, 1.0f, 0);
    ok &= near_("post stop", w.px[0], 13.3, 1e-2);   /* the post at x 12 has its east face at 13.0; the player is 0.6 wide */

    w.px[0] = 12.5; w.py[0] = 7.0; w.mz[0] = 0.0; w.my[0] = -0.1; w.on_ground[0] = 0;
    w.mx[0] = 0.0;
    cache_refresh(&w, 0, 12, 7, 16);
    for (int t = 0; t < 40; ++t) step_fixed(&w, 0, 0.0f, 0);
    ok &= near_("drop py", w.py[0], 6.0, 1e-3);
    printf("%-10s on_ground %d (want 1) %s\n", "drop land", w.on_ground[0], w.on_ground[0] ? "ok" : "FAIL");

    /* The dig rate, straight from the tick's own path (the pick's frozen target, the
     * accumulated damage, the break): vanilla's Block.getPlayerRelativeBlockHardness
     * (oracle/src/block/Block.java:688-691) is str/hardness/30 when the tool harvests
     * the block and str/hardness/100 when it does not, str being Item.getStrVsBlock's
     * 4.0 for a stone pick on Material.rock (Item.java:540-543, EnumToolMaterial.STONE
     * at Item.java:859, ItemTool.java:35-38) and 1.0 for the hand. Stone (hardness 1.5)
     * is harvestable by any pickaxe and not by the hand (ItemPickaxe.canHarvestBlock,
     * ItemPickaxe.java:19-27), so the model must take 150 ticks a stone block by hand
     * and 11.25 with a stone pick, and 15 for dirt (hardness 0.5) by hand. The damage
     * a tick is checked against a separately written expression; the tick the block
     * breaks is checked against 1/damage plus the click that first picks the target. */
    {
        struct { int tool, kind; double want_dmg, want_ticks; const char *name; } cases[] = {
            { BT_TOOL_HAND, K_STONE, 1.0 / 1.5 / 100.0, 1.0 / (1.0 / 1.5 / 100.0), "stone/hand" },
            { BT_TOOL_STONE_PICK, K_STONE, 4.0 / 1.5 / 30.0, 1.0 / (4.0 / 1.5 / 30.0), "stone/pick" },
            { BT_TOOL_HAND, K_DIRT, 1.0 / 0.5 / 30.0, 1.0 / (1.0 / 0.5 / 30.0), "dirt/hand" },
        };
        for (int c = 0; c < 3; ++c)
        {
            blk_tool(cases[c].tool);
            worlds_init(&w, 0);
            wset(&w, 0, 14, 5, 16, cases[c].kind);        /* a two-high wall to -x, in reach */
            wset(&w, 0, 14, 6, 16, cases[c].kind);
            w.px[0] = 16.5; w.py[0] = 5.0; w.pz[0] = 16.5;
            w.yaw[0] = 90.0f; w.pitch[0] = 0.0f;
            w.mx[0] = w.my[0] = w.mz[0] = 0.0;
            cache_refresh(&w, 0, 16, 5, 16);
            struct bt_in in = { 0, 0, 90.0f, 0.0f, 0, 0, 1, 0 };
            int broke = 0;
            double inc = 0.0, prev = 0.0;
            for (int t = 1; t <= 400; ++t)
            {
                tick_world_in(&w, 0, MODE_FULL, &in);
                if (w.dig_dmg[0] > prev) inc = (double)w.dig_dmg[0] - prev;
                prev = (double)w.dig_dmg[0];
                if (wget(&w, 0, 14, 6, 16) == 0) { broke = t; break; }
            }
            char nm[32], n2[32];
            snprintf(nm, sizeof nm, "dig %s", cases[c].name);
            ok &= near_rel((const char *)nm, inc, cases[c].want_dmg, 1e-6);
            snprintf(n2, sizeof n2, "break %s", cases[c].name);
            ok &= near_((const char *)n2, (double)broke, cases[c].want_ticks + 1.0, 1.0);
        }
        blk_tool(BT_TOOL_STONE_PICK);
        worlds_init(&w, 0);
    }
    ok &= w.on_ground[0];

    /* The same per-world state after the same ticks on 1 and on 32 threads,
     * from the same *initialised* worlds. The old test allocated both sets and
     * ran them: calloc leaves every block air, every position 0 and every arng
     * 0, xorshift leaves 0 at 0, so all 1024 worlds ran one identical all-air
     * trajectory, the hash was a constant, and "1 thread == 32 threads" was
     * true of nothing. Here worlds_init lays the post terrain and the per-world
     * seeds, and the test also says the run moved the state and did work. */
    {
        struct worlds a, b, c;
        struct sdiff d;
        int n = 256, ticks = 200;
        worlds_alloc(&a, n);
        worlds_alloc(&b, n);
        worlds_alloc(&c, n);
        worlds_init(&a, 0);
        worlds_init(&b, 0);
        worlds_init(&c, 0);
        uint64_t h0 = state_hash(&c);
        uint64_t t0 = now_s();
        double t1 = run_ticks(&a, 1, ticks, MODE_FULL);
        double t32 = run_ticks(&b, 32, ticks, MODE_FULL);
        uint64_t ha = state_hash(&a), hb = state_hash(&b);
        int same = (ha == hb);
        if (same) state_diff(&a, &b, &d);
        else { d.worlds = -1; d.fields = -1; d.first_world = -1; d.first_field = "hash"; }
        printf("%-10s n=%d ticks=%d mode=full 1 thread %016llx  32 threads %016llx  %s",
               "determinism", n, ticks, (unsigned long long)ha, (unsigned long long)hb, same ? "equal" : "DIFFER");
        if (same)
            printf(" (%d of %d worlds differ, %lld fields, first %s in world %d)", d.worlds, n, d.fields,
                   d.first_field, d.first_world);
        printf("\n");
        ok &= same && d.worlds == 0;

        struct sdiff m;
        state_diff(&a, &c, &m);
        uint64_t draws = 0, breaks = 0, places = 0, jumps = 0;
        for (int i = 0; i < n; ++i)
        {
            draws += a.ctr[(size_t)i * 8]; breaks += a.ctr[(size_t)i * 8 + 1];
            places += a.ctr[(size_t)i * 8 + 2]; jumps += a.ctr[(size_t)i * 8 + 3];
        }
        printf("%-10s vs the initial state: %d of %d worlds differ (%lld fields, first %s); the run's work: "
               "%.0f draws, %.0f breaks, %.0f places, %.0f jumps a world; hash %016llx -> %016llx\n",
               "moved", m.worlds, n, m.fields, m.first_field, (double)draws / n, (double)breaks / n,
               (double)places / n, (double)jumps / n, (unsigned long long)h0, (unsigned long long)ha);
        ok &= m.worlds == n && m.fields > 0 && draws > 0 && jumps > 0;

        /* the timing the model's table reports, at the gate's own shape */
        printf("%-10s 1 thread %.1f ns/world-tick, 32 threads %.1f ns/world-tick (%d worlds, %d ticks, "
               "%.2f of %.2f s)\n", "timing", t1 * 1e9 / ((double)n * ticks), t32 * 1e9 / ((double)n * ticks), n,
               ticks, t1, t32);
        (void)t0;
        worlds_free(&a);
        worlds_free(&b);
        worlds_free(&c);
    }
    worlds_free(&w);
    return ok ? 0 : 1;
}

/* ------------------------------------------------ recorded replay vs the engine
 *
 * nwbt_compare runs the model and the engine from one start state on a stored
 * recording (out/java/snapshots/NAME) and prints, tick by tick, where and how
 * much they part.
 *
 * What is compared: the recording's own `cp` rows, which carry the client
 * player's posX/Y/Z, motionX/Y/Z, rotation, onGround and fall distance every
 * tick. Those are the engine's per-tick state for these rows: the replay gate
 * (out/native/test_snapshots DIR --rows N) opens its session from the same
 * snapshot keyframe and reports "0 field mismatches", which means every one of
 * the engine's cp values equals the tape's. This check prints that line before
 * it compares, so the reference is named. Compared field for field: x, y, z,
 * mx, my, mz and on_ground -- the seven the model has (yaw and pitch are
 * handed to the model each tick as input; fall distance, health, food and the
 * inventory are not modelled).
 *
 * The start state is the tape row at (the snapshot's tick - 1): the state "at
 * the start of tick T, before preTick" the manifest's align names, the same
 * state the engine's session_open builds. Tick T's own act is the first input.
 *
 * The region: the model's world is 32x32x32, so the window is 32 blocks of x
 * and z with the start 8 blocks in, and 32 of y from 64 (the recordings start
 * at feet 70). The recording's vanilla ids inside the window become kinds with
 * their vanilla hardness, slipperiness and solidity (blk_kind), and the model
 * keeps its documented gaps: one client region (no server, packets or
 * entities), a held tool fixed for the run, no water flow, and the sprint stop
 * read from the key and forward only.
 */

#define BT_MAXROWS 4096
#define BT_ROW_LEN  65536
#define BT_Y0       64

/* the value of "key" in the JSON object s (s at its '{'), or NULL */
static const char *jget(const char *s, const char *key)
{
    if (!s || *s != '{') return NULL;
    size_t klen = strlen(key);
    const char *p = s + 1;
    while (*p && *p != '}')
    {
        while (*p == ' ' || *p == ',') ++p;
        if (*p != '"') return NULL;
        const char *ks = ++p;
        while (*p && *p != '"') ++p;
        size_t len = (size_t)(p - ks);
        if (*p) ++p;
        while (*p == ' ' || *p == ':') ++p;
        if (len == klen && !strncmp(ks, key, klen)) return p;
        int depth = 0, instr = 0;
        while (*p)
        {
            char c = *p;
            if (instr) { if (c == '\\') { p += 2; continue; } if (c == '"') instr = 0; }
            else if (c == '"') instr = 1;
            else if (c == '{' || c == '[') ++depth;
            else if (c == '}' || c == ']') { if (depth == 0) return NULL; --depth; }
            else if (c == ',' && depth == 0) { ++p; break; }
            ++p;
        }
    }
    return NULL;
}

static int jnum(const char *s, const char *key, double *out)
{
    const char *v = jget(s, key);
    if (!v) return 0;
    char *end;
    double d = strtod(v, &end);
    if (end == v) return 0;
    *out = d;
    return 1;
}

static int jint(const char *s, const char *key, long long *out)
{
    double d;
    if (!jnum(s, key, &d)) return 0;
    *out = (long long)d;
    return 1;
}

/* a key's state in an input row: "key.forward": [1, 0] -> 1 (the first
 * number, the press; the second is Java's pressed-and-not-released count) */
static int jkey(const char *act, const char *name)
{
    const char *in = jget(act, "in");
    const char *keys = in ? jget(in, "keys") : NULL;
    const char *v = keys ? jget(keys, name) : NULL;
    if (!v) return 0;
    while (*v == '[' || *v == ' ') ++v;            /* "[1, 0]": the press */
    return strtod(v, NULL) != 0.0 ? 1 : 0;
}

struct btrow {
    double x, y, z, mx, my, mz;
    float yaw, pitch;
    int og;
    float forward, strafe;
    int jump, sprint, attack, place;
    int have_cp;
};

static int row_parse(const char *line, struct btrow *r)
{
    const char *cp = jget(line, "cp");
    memset(r, 0, sizeof *r);
    if (!cp) return 0;
    if (!jnum(cp, "x", &r->x) || !jnum(cp, "y", &r->y) || !jnum(cp, "z", &r->z)) return 0;
    jnum(cp, "mx", &r->mx); jnum(cp, "my", &r->my); jnum(cp, "mz", &r->mz);
    { double d = 0; jnum(cp, "yaw", &d); r->yaw = (float)d; jnum(cp, "pitch", &d); r->pitch = (float)d; }
    { long long i = 0; jint(cp, "og", &i); r->og = (int)i; }
    r->have_cp = 1;

    const char *act = jget(line, "act");
    if (act)
    {
        r->jump = jkey(act, "key.jump");
        r->sprint = jkey(act, "key.sprint");
        r->attack = jkey(act, "key.attack");
        r->place = jkey(act, "key.use");
        int fwd = jkey(act, "key.forward"), back = jkey(act, "key.back");
        int left = jkey(act, "key.left"), right = jkey(act, "key.right");
        r->forward = (float)(fwd - back);
        r->strafe = (float)(left - right);
    }
    return 1;
}

/* the client player's feet (the model's y) from the tape's posY: a player's
 * bounding box is posY - yOffset .. posY - yOffset + 1.8 (Entity.setPosition,
 * csrc/engine/entity.c:65-75), yOffset 1.62 for a player (player.c:608) */
#define BT_EYE 1.6200000047683716

/* Run one recording: load its rows, import the window, start from the row
 * before the snapshot's tick, step the model and diff it against the rows.
 * Returns the number of ticks that differ in any compared field. */
struct btreplay { int rows, exact, first_t; const char *first_field; double first_model, first_engine;
                  double max_dpos, max_dmot, dpos_end; unsigned long long breaks; };

static int replay_one(const char *dir, int rows, struct btreplay *out)
{
    char path[1200], line[BT_ROW_LEN];
    memset(out, 0, sizeof *out);
    out->first_t = -1;

    /* the snapshot's tick, from its manifest */
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("  %s: no manifest.json\n", dir); return 0; }
    long long tick = -1;
    {
        size_t n = fread(line, 1, sizeof line - 1, f);
        line[n] = 0;
        fclose(f);
        if (!jint(line, "tick", &tick) || tick < 1) { printf("  %s: manifest has no tick\n", dir); return 0; }
    }

    /* the rows */
    snprintf(path, sizeof path, "%s/tape.jsonl", dir);
    f = fopen(path, "rb");
    if (!f) { printf("  %s: no tape.jsonl\n", dir); return 0; }
    char **row = (char **)calloc(BT_MAXROWS, sizeof *row);
    int nr = 0, hdr = 0;
    for (int n = 0; n < BT_MAXROWS && fgets(line, sizeof line, f); )
        if (!hdr) hdr = 1;                       /* the tape's header */
        else { row[nr] = strdup(line); if (!row[nr]) break; ++nr; }
    fclose(f);
    if (nr == 0) { free(row); printf("  %s: no rows\n", dir); return 0; }

    /* the start state: the row before the snapshot's tick (its cp is the state
     * the snapshot is at), then the rows it replays */
    struct btrow start, r;
    int i0 = -1;
    for (int k = 0; k < nr; ++k)
    {
        long long t;
        if (!jint(row[k], "t", &t)) continue;
        if (t == tick - 1 && row_parse(row[k], &start)) { i0 = k; break; }
    }
    if (i0 < 0 && row_parse(row[0], &start)) i0 = 0;
    if (i0 < 0) { for (int k = 0; k < nr; ++k) free(row[k]); free(row); printf("  %s: no start row\n", dir); return 0; }
    /* the join prologue: a row whose cp is exactly the one before it was a
     * tick the client did not run (the integrated server was still paused:
     * session_tick skips the client tick, and the tape's own rows say so).
     * Start after those, so the first compared tick is one that ran. */
    for (int k = 0; k < 4 && i0 + 1 < nr; ++k)
    {
        struct btrow a, b;
        if (!row_parse(row[i0], &a) || !row_parse(row[i0 + 1], &b)) break;
        if (a.x != b.x || a.y != b.y || a.z != b.z || a.mx != b.mx || a.my != b.my || a.mz != b.mz || a.og != b.og) break;
        ++i0;
    }
    if (row_parse(row[i0], &start) == 0)
    { for (int k = 0; k < nr; ++k) free(row[k]); free(row); printf("  %s: unreadable start row\n", dir); return 0; }
    if (rows > nr - i0 - 1) rows = nr - i0 - 1;

    double feet = start.y - BT_EYE;
    int x0 = (int)floor(start.x) - 8, y0 = BT_Y0, z0 = (int)floor(start.z) - 8;
    if (feet < y0 + 2 || feet > y0 + 30) y0 = (int)floor(feet) - 8;

    struct worlds w;
    worlds_alloc(&w, 1);
    worlds_init(&w, 1);                          /* the flat fill under the window */

    /* ---- the window: chunks.bin.gz (csrc/engine/snapshot.c:388-402: 24 bytes
     * of cx, cz, hash and near, then SNAP_CHUNK_BYTES; cell index
     * ((x&15)*16 + (z&15))*256 + y, csrc/engine/world.c:566-593) */
    snprintf(path, sizeof path, "%s/chunks.bin.gz", dir);
    gzFile gz = gzopen(path, "rb");
    if (!gz) { printf("  %s: no chunks.bin.gz\n", dir); worlds_free(&w); for (int k = 0; k < nr; ++k) free(row[k]); free(row); return 0; }
    enum { SNAP_CHUNK = 2 * 65536 + 3 * 65536 + 2 * 256 * 4 + 4 + 2, SNAP_REC = 24 + SNAP_CHUNK };
    unsigned char *rec = (unsigned char *)malloc(SNAP_REC);
    int ids[4096] = { 0 }, nids = 0, nchunks = 0;
    while (rec && gzread(gz, rec, SNAP_REC) == SNAP_REC)
    {
        int cx, cz;
        memcpy(&cx, rec, 4); memcpy(&cz, rec + 4, 4);
        const unsigned char *p = rec + 24;
        int lx0 = cx * 16, lz0 = cz * 16;
        if (lx0 + 16 <= x0 || lx0 >= x0 + WX || lz0 + 16 <= z0 || lz0 >= z0 + WZ) continue;
        ++nchunks;
        for (int x = lx0; x < lx0 + 16; ++x)
        {
            if (x < x0 || x >= x0 + WX) continue;
            for (int z = lz0; z < lz0 + 16; ++z)
            {
                if (z < z0 || z >= z0 + WZ) continue;
                for (int y = 0; y < 256; ++y)
                {
                    if (y < y0 || y >= y0 + WY) continue;
                    int idx = (((x & 15) * 16) + (z & 15)) * 256 + y;
                    int vid = p[2 * idx] | (p[2 * idx + 1] << 8);
                    w.wblk[widx(x - x0, y - y0, z - z0)] = blk_kind(vid);
                    int seen = 0;
                    for (int k = 0; k < nids && !seen; ++k) seen = ids[k] == vid;
                    if (!seen && nids < 4096) ids[nids++] = vid;
                }
            }
        }
    }
    gzclose(gz);
    free(rec);

    /* the recording's held item: an empty hand in these (cp.inv is empty), a
     * stone pickaxe otherwise */
    int tool = BT_TOOL_HAND;
    {
        const char *cp = jget(row[i0], "cp");
        const char *inv = cp ? jget(cp, "inv") : NULL;
        if (inv && *inv == '[' && inv[1] != ']') tool = BT_TOOL_STONE_PICK;
    }
    blk_tool(tool);
    cache_refresh(&w, 0, (int)floor(start.x - x0), (int)floor(0), (int)floor(start.z - z0));

    /* the start state, in the model's coordinates: the region's origin off the
     * position, the eye height off the y */
    w.px[0] = start.x - x0;
    w.py[0] = feet - y0;
    w.pz[0] = start.z - z0;
    w.mx[0] = start.mx; w.my[0] = start.my; w.mz[0] = start.mz;
    w.yaw[0] = start.yaw; w.pitch[0] = start.pitch;
    w.on_ground[0] = (uint8_t)start.og;
    w.sprinting[0] = 0;
    w.digging[0] = 0;
    w.dig_dmg[0] = 0.0f;
    w.atk_left[0] = 0;
    cache_refresh(&w, 0, (int)floor(w.px[0]), (int)floor(w.py[0]), (int)floor(w.pz[0]));

    printf("  %s: start t=%lld pos=(%.3f %.3f %.3f) feet=%.3f, window x %d..%d y %d..%d z %d..%d "
           "(%d chunks), %d vanilla ids, tool=%s\n",
           dir, tick, start.x, start.y, start.z, feet, x0, x0 + WX - 1, y0, y0 + WY - 1, z0, z0 + WZ - 1,
           nchunks, nids, tool == BT_TOOL_HAND ? "hand" : "stone pickaxe");
    printf("    ids:");
    for (int k = 0; k < nids; ++k) printf(" %d(h%.2f%s)", ids[k], (double)BLK[blk_kind(ids[k])].hardness,
                                           BLK[blk_kind(ids[k])].solid ? "" : ",not solid");
    printf("\n");
    blk_tool(tool);

    /* ---- the ticks */
    for (int k = 0; k < rows; ++k)
    {
        /* the state is row i0's cp; row i0+k+1's act is the input, and that
         * row's cp is what the tick produced (row t's act -> row t's cp) */
        const char *ln = row[i0 + 1 + k];
        if (!row_parse(ln, &r) || !r.have_cp) break;
        struct bt_in in;
        in.forward = r.forward; in.strafe = r.strafe;
        in.yaw = r.yaw; in.pitch = r.pitch;
        in.jump = r.jump; in.sprint = r.sprint; in.attack = r.attack; in.place = r.place;

        tick_world_in(&w, 0, MODE_PHYS, &in);

        double gx = w.px[0] + x0, gy = w.py[0] + y0 + BT_EYE, gz = w.pz[0] + z0;
        double dx = gx - r.x, dy = gy - r.y, dz = gz - r.z;
        double dmx = w.mx[0] - r.mx, dmy = w.my[0] - r.my, dmz = w.mz[0] - r.mz;
        double dpos = sqrt(dx * dx + dy * dy + dz * dz);
        double dmot = sqrt(dmx * dmx + dmy * dmy + dmz * dmz);
        int ogok = (int)w.on_ground[0] == r.og;
        int exact = (dx == 0.0 && dy == 0.0 && dz == 0.0 && dmx == 0.0 && dmy == 0.0 && dmz == 0.0 && ogok);
        ++out->rows;
        if (exact) ++out->exact;
        else if (out->first_t < 0)
        {
            out->first_t = k;
            out->first_field = !ogok ? "on_ground" : dx != 0.0 ? "x" : dy != 0.0 ? "y" : dz != 0.0 ? "z"
                              : dmx != 0.0 ? "mx" : dmy != 0.0 ? "my" : "mz";
            out->first_model = gx; out->first_engine = r.x;
        }
        if (dpos > out->max_dpos) out->max_dpos = dpos;
        if (dmot > out->max_dmot) out->max_dmot = dmot;
        out->dpos_end = dpos;
        out->breaks += w.ctr[1];

        if (rows <= 80 || k < 24 || !exact)
            printf("    t=%-3d dpos=%.3e dmot=%.3e og=%d/%d  model=(%.6f %.6f %.6f) engine=(%.6f %.6f %.6f)%s\n",
                   k, dpos, dmot, (int)w.on_ground[0], r.og, gx, gy, gz, r.x, r.y, r.z,
                   exact ? "  exact" : "");
    }

    worlds_free(&w);
    for (int k = 0; k < nr; ++k) free(row[k]);
    free(row);
    return out->rows - out->exact;
}

/* the engine's own gate on the same rows: its line, 1 when every row matched
 * (0 field mismatches), -1 when the binary is not built */
static int engine_gate(const char *bin, const char *dir, int rows)
{
    char cmd[2400];
    struct stat st;
    if (stat(bin, &st) != 0)
    {
        printf("    engine: %s is not built; the tape's cp rows are the reference (make -C csrc test_snapshots)\n", bin);
        return -1;
    }
    snprintf(cmd, sizeof cmd, "%s --rows %d %s 2>&1", bin, rows, dir);
    FILE *p = popen(cmd, "r");
    if (!p) return -1;
    char line[4096];
    int ok = 0;
    while (fgets(line, sizeof line, p))
    {
        if (strstr(line, "field mismatches") || strstr(line, "every row replayed") || strstr(line, "FAIL"))
        {
            printf("    engine: %s", line);
            if (strstr(line, "0 field mismatches") || strstr(line, "every row replayed")) ok = 1;
            if (strstr(line, "FAIL")) ok = 0;
        }
    }
    return pclose(p) == 0 && ok ? 1 : 0;
}

/* The recorded comparison the gate runs: two recordings, each from its
 * snapshot's own start state, the model against the engine's per-tick rows.
 * 0 when both replays ran and the engine's gate passed (the model's own
 * divergences are reported, not gated: it is a bounded yardstick, and where
 * and how much it parts is the number this check exists to print). */
int nwbt_compare(const char *snaps, const char *engine_bin, int rows)
{
    static const char *const RECS[] = { "walk300-s1", "pickwalk-s1" };
    int bad = 0;
    printf("recorded comparison: the model and the engine from one start state\n");
    for (int i = 0; i < (int)(sizeof RECS / sizeof *RECS); ++i)
    {
        char dir[1200];
        snprintf(dir, sizeof dir, "%s/%s", snaps, RECS[i]);
        struct stat st;
        if (stat(dir, &st) != 0)
        {
            printf("  %s: not present under %s (no recordings in this checkout)\n", RECS[i], snaps);
            continue;
        }
        /* the engine's gate first: it is what makes the tape's cp rows the
         * engine's per-tick state */
        printf("  %s:\n", RECS[i]);
        int g = engine_gate(engine_bin, dir, rows);
        if (g == 0) ++bad;
        struct btreplay r;
        int bad_ticks = replay_one(dir, rows, &r);
        printf("  %s: %d of %d ticks differ (%d exact); first divergence at t=%d in %s "
               "(model %.6f, engine %.6f); max |dpos| %.3e, max |dmot| %.3e, final |dpos| %.3e; "
               "%llu blocks broken\n",
               RECS[i], bad_ticks, r.rows, r.exact, r.first_t, r.first_field, r.first_model, r.first_engine,
               r.max_dpos, r.max_dmot, r.dpos_end, r.breaks);
        if (r.rows == 0) ++bad;
    }
    return bad;
}

/* ------------------------------------------------------------------ table */

static const char *mode_name(int m) { return m == MODE_MOVE ? "move" : m == MODE_PHYS ? "phys" : "full"; }

/* ticks for a target-length run of this arm, from a short probe */
static int size_run(struct worlds *w, int threads, int mode, double target)
{
    int probe = threads > 1 ? 100 : 8;
    double p = run_ticks(w, threads, probe, mode) / (double)probe;
    int ticks = (int)(target / (p > 1e-9 ? p : 1e-9));
    if (ticks < (threads > 1 ? 200 : 8)) ticks = (threads > 1 ? 200 : 8);
    if (ticks > 200000) ticks = 200000;
    return ticks;
}

struct arm { int ticks; double wall[2]; };

int nwbt_main(int argc, char **argv)
{
    jmath_init();
    blk_reset();
    double target = 0.30;
    int only_n = 0, only_mode = -1, dump = 0;
    for (int i = 1; i < argc; ++i)
    {
        if (!strcmp(argv[i], "--selftest")) { jmath_init(); blk_reset(); return nwbt_selftest(); }
        else if (!strcmp(argv[i], "--dump")) { dump = atoi(argv[++i]); }
        else if (!strcmp(argv[i], "--target") && i + 1 < argc) target = atof(argv[++i]);
        else if (!strcmp(argv[i], "--n") && i + 1 < argc) only_n = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--mode") && i + 1 < argc) only_mode = atoi(argv[++i]);
        else { fprintf(stderr, "usage: %s [--selftest] [--n N] [--mode 0|1|2] [--target s]\n", argv[0]); return 2; }
    }

    if (dump)
    {
        struct worlds w;
        worlds_alloc(&w, dump);
        worlds_init(&w, 0);
        for (int t = 0; t < 8; ++t)
        {
            run_ticks(&w, 1, 1, MODE_FULL);
        {
            double ex = w.px[0], ey = w.py[0] + 1.6200000047683716, ez = w.pz[0];
            double cp2 = cos((double)(-w.pitch[0]) * M_PI / 180.0), sp2 = sin((double)(-w.pitch[0]) * M_PI / 180.0);
            double cy2 = cos((double)(-w.yaw[0]) * M_PI / 180.0), sy2 = sin((double)(-w.yaw[0]) * M_PI / 180.0);
            double dx2 = sy2 * cp2, dy2 = sp2, dz2 = cy2 * cp2;
            int hx = -1, hy = -1, hz = -1;
            for (double tt = 0.2; tt <= 4.5; tt += 0.2) {
                int cx = (int)floor(ex + dx2 * tt), cy = (int)floor(ey + dy2 * tt), cz = (int)floor(ez + dz2 * tt);
                if (BLK[wget(&w, 0, cx, cy, cz)].solid) { hx = cx; hy = cy; hz = cz; break; }
            }
            printf("      dir=(%.3f %.3f %.3f) eye=(%.2f %.2f %.2f) hit=(%d %d %d)\n", dx2, dy2, dz2, ex, ey, ez, hx, hy, hz);
        }
            printf("t=%d px=%.4f py=%.4f pz=%.4f mx=%.5f my=%.5f mz=%.5f g=%d sp=%d yaw=%.1f pitch=%.1f\n", t,
                   w.px[0], w.py[0], w.pz[0], w.mx[0], w.my[0], w.mz[0], w.on_ground[0], w.sprinting[0], w.yaw[0], w.pitch[0]);
        }
        worlds_free(&w);
        return 0;
    }
    const int ns[] = { 256, 1024, 4096 };
    const int ths[] = { 1, 32 };
    printf("# batchproto: worlds x ticks/s through flat SoA arrays and a 3x3x3 block cache a world\n");
    printf("# exact reference: 0.246 ms = 246000 ns a world-step (DEVLOG, 768/3, perf stat)\n");
    printf("# move = player physics only; phys = + pick, dig, place; full = + WorldClient.tick's draws\n");
    for (int mi = 0; mi < 3; ++mi)
    {
        int mode = mi == 0 ? MODE_MOVE : mi == 1 ? MODE_PHYS : MODE_FULL;
        if (only_mode >= 0 && mode != only_mode) continue;
        for (int ni = 0; ni < 3; ++ni)
        {
            int n = ns[ni];
            if (only_n && n != only_n) continue;
            struct worlds w;
            worlds_alloc(&w, n);
            worlds_init(&w, 0);
            struct arm a[2];
            /* warmup on both arms, then size each arm's run */
            run_ticks(&w, 1, 3, mode);
            run_ticks(&w, 32, 3, mode);
            for (int k = 0; k < 2; ++k)
            {
                a[k].ticks = size_run(&w, ths[k], mode, target);
                a[k].wall[0] = a[k].wall[1] = 0.0;
            }
            /* interleaved and repeated: 1, 32, 1, 32 */
            for (int rep = 0; rep < 2; ++rep)
                for (int k = 0; k < 2; ++k)
                {
                    a[k].wall[rep] = run_ticks(&w, ths[k], a[k].ticks, mode);
                    fflush(stdout);
                }
            /* counters over the last run of each arm is the same work */
            for (int k = 0; k < 2; ++k)
            {
                double wticks = (double)n * a[k].ticks;
                double best = a[k].wall[0] < a[k].wall[1] ? a[k].wall[0] : a[k].wall[1];
                double worst = a[k].wall[0] > a[k].wall[1] ? a[k].wall[0] : a[k].wall[1];
                double r0 = wticks / a[k].wall[0], r1 = wticks / a[k].wall[1];
                double nspt = best * 1e9 / wticks;
                printf("mode=%-4s n=%4d threads=%2d ticks=%6d wall=[%.4f %.4f]s rate=[%.0f %.0f] worldticks/s ns/world-tick=%7.1f ratio=%6.1fx spread=%4.1f%%\n",
                       mode_name(mode), n, ths[k], a[k].ticks, a[k].wall[0], a[k].wall[1], r0, r1, nspt,
                       246000.0 / nspt, 100.0 * (worst - best) / best);
            }
            {
                /* the counters of one clean 1-thread run at this shape */
                for (int pass = 0; pass < 3; ++pass) /* warm */
                    run_ticks(&w, 1, a[0].ticks / 4 + 1, mode);
                for (int i = 0; i < n; ++i) for (int k = 0; k < 6; ++k) w.ctr[(size_t)i * 8 + k] = 0;
                int ct = a[0].ticks;
                if (ct < 50) ct = 50;
                run_ticks(&w, 1, ct, mode);
                uint64_t tot[6] = { 0 };
                for (int i = 0; i < n; ++i) for (int k = 0; k < 6; ++k) tot[k] += w.ctr[(size_t)i * 8 + k];
                double d = (double)n * (double)ct;
                printf("         per world-tick: draws=%.0f ray-steps=%.1f cache-refills=%.2f breaks=%.3f places=%.3f jumps=%.3f\n",
                       (double)tot[0] / d, (double)tot[4] / d, (double)tot[5] / d,
                       (double)tot[1] / d, (double)tot[2] / d, (double)tot[3] / d);
            }
            fflush(stdout);
            worlds_free(&w);
        }
    }

    /* determinism: the same per-world state after the same ticks whether one
     * thread or 32 stepped it (from worlds_init, both) */
    {
        struct worlds a, b;
        int n = 1024, ticks = 400;
        worlds_alloc(&a, n); worlds_alloc(&b, n);
        worlds_init(&a, 0); worlds_init(&b, 0);
        run_ticks(&a, 1, ticks, MODE_PHYS);
        run_ticks(&b, 32, ticks, MODE_PHYS);
        uint64_t ha = state_hash(&a), hb = state_hash(&b);
        printf("determinism: 1 thread %016llx  32 threads %016llx  %s\n",
               (unsigned long long)ha, (unsigned long long)hb, ha == hb ? "equal" : "DIFFER");
        worlds_free(&a); worlds_free(&b);
    }
    return 0;
}
