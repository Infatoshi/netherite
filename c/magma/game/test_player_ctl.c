/* game/test_player_ctl.c - standalone unit test for game/player_ctl.c.
 *
 * (A) LANDS ON FLOOR: flat stone floor (y in [0,64]) in a synthetic 9-Chunk window; a
 *     player dropped from local (24,80,24) with a neutral GmAction comes to rest at feet
 *     y == 65.0 with on_ground==1. Cross-checked bit-for-bit each tick against a raw
 *     psv_physics_tick reference loop (proves gm_player_tick reuses the verified math).
 *
 * (B) FLOATING-ORIGIN INVARIANCE: same window imagined at chunk (100,100) (flat floor is
 *     translation-invariant); landing LOCAL posY is bit-identical to (A). Then look straight
 *     down + hold attack (progressive dig): one WORLD-coord GmBlockEdit at
 *     (ox+floor(lx), 64, oz+floor(lz)), id==0, with a separate natural item drop.
 *
 * Build: bash game/test_player_ctl.sh
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

#include "player_survival.h"
#include "player_vitals.h"
#include "game/game.h"
#include "game/player_ctl.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { if (!(cond)) { printf("  FAIL: %s\n", msg); g_fail = 1; } } while (0)

static u64 double_bits(double value)
{
    u64 bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

/* Fill a 9-Chunk window with a flat stone floor: solid stone for y in [0,64], air above. */
static void fill_flat(Chunk *win)
{
    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
    for (int ci = 0; ci < PSV_NCHUNKS; ++ci) {
        win[ci].cx = (ci % PSV_DIM) - PSV_R;
        win[ci].cz = (ci / PSV_DIM) - PSV_R;
        for (int lx = 0; lx < 16; ++lx)
            for (int lz = 0; lz < 16; ++lz)
                for (int y = 0; y <= 64; ++y)
                    mc_set(&win[ci], lx, y, lz, mc_state(BLK_STONE, 0));
    }
}

static void fill_mechanics_floor(Chunk *win)
{
    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
    for (int ci = 0; ci < PSV_NCHUNKS; ++ci) {
        win[ci].cx = (ci % PSV_DIM) - PSV_R;
        win[ci].cz = (ci / PSV_DIM) - PSV_R;
        for (int lx = 0; lx < 16; ++lx)
            for (int lz = 0; lz < 16; ++lz)
                for (int y = 0; y <= 2; ++y)
                    mc_set(&win[ci], lx, y, lz, mc_state(BLK_STONE, 0));
    }
}

static void set_block_meta(Chunk *win, int wx, int wy, int wz, int id, int meta)
{
    int lx, lz;
    int ci = psv_chunk_index(wx, wz, &lx, &lz);
    if (ci >= 0)
        mc_set(&win[ci], lx, wy, lz, mc_state(id, meta));
}

/* Spawn a player at a given LOCAL feet position (overrides psv_player_init's spawn). */
static void spawn_at(PsvPlayer *pl, double x, double y, double z)
{
    psv_player_init(pl);
    pl->ent.posX = x; pl->ent.posY = y; pl->ent.posZ = z;
    pl->ent.box = psv_player_box(x, y, z);
    pl->ent.motionX = pl->ent.motionY = pl->ent.motionZ = 0.0;
    pl->ent.onGround = 0;
    pl->ent.collidedHorizontally = pl->ent.collidedVertically = pl->ent.isCollided = 0;
}

int main(void)
{
    McSinTable st;
    mc_sin_table_init(&st);

    GmAction neutral;
    memset(&neutral, 0, sizeof neutral);

    Chunk *win = malloc(sizeof(Chunk) * PSV_NCHUNKS);

    /* ---------------- (A) LANDS ON FLOOR + reference parity ---------------- */
    printf("case A: land on floor + bitwise reference parity\n");
    fill_flat(win);

    PsvPlayer pl, ref;
    spawn_at(&pl,  24.0, 80.0, 24.0);
    spawn_at(&ref, 24.0, 80.0, 24.0);

    PsvAction zero;
    memset(&zero, 0, sizeof zero);   /* forward=strafe=yaw=pitch=jump=break=place=attack=0 */

    int parity_ok = 1;
    PvStats tv; pv_init(&tv);
    for (int t = 0; t < 120; ++t) {
        GmBlockEdit edits[8];
        int nedits = -1;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st, (struct PsvPlayer *)&pl, (struct PvStats *)&tv, neutral, 0, 0, 0, edits, &nedits, 8);
        CHECK(nedits == 0, "neutral action emitted no edits");

        /* raw verified physics reference over the same window + zeroed action */
        McAABB blocks[PSV_MAX_BLOCKS];
        psv_physics_tick(win, &st, &ref, &zero, blocks);

        if (pl.ent.posY != ref.ent.posY) { parity_ok = 0; }
    }
    CHECK(parity_ok, "gm_player_tick posY bit-identical to psv_physics_tick every tick");
    printf("  landed feet y = %.10f  on_ground = %d\n", pl.ent.posY, pl.ent.onGround);
    CHECK(fabs(pl.ent.posY - 65.0) < 1e-6, "player rests at feet y == 65.0");
    CHECK(pl.ent.onGround == 1, "player on_ground == 1 at rest");

    double landed_local_y = pl.ent.posY;

    /* ---------------- (B) FLOATING-ORIGIN INVARIANCE ---------------- */
    printf("case B: floating-origin invariance + world-coord break edit\n");
    fill_flat(win);   /* identical flat floor; imagined centered at chunk (100,100) */
    const int ox = 100 * 16, oy = 0, oz = 100 * 16;

    PsvPlayer plb;
    spawn_at(&plb, 24.0, 80.0, 24.0);
    PvStats tvb; pv_init(&tvb);
    for (int t = 0; t < 120; ++t) {
        GmBlockEdit edits[8];
        int nedits = -1;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st, (struct PsvPlayer *)&plb, (struct PvStats *)&tvb, neutral, ox, oy, oz, edits, &nedits, 8);
        CHECK(nedits == 0, "neutral action emitted no edits (offset frame)");
    }
    printf("  landed LOCAL feet y = %.10f (case A = %.10f)\n", plb.ent.posY, landed_local_y);
    CHECK(plb.ent.posY == landed_local_y, "LOCAL landing posY bit-identical across offsets");
    CHECK(plb.ent.onGround == 1, "player on_ground == 1 at rest (offset frame)");

    /* look straight down + hold attack: progressive dig (player_break) clears the floor.
     * Iron pickaxe -> ~8 ticks on stone. do_break edge is no longer instant. */
    isr_set_stack(&plb.inv, 0, ic_mk(257 /* iron pick */, 1, 0));
    plb.inv.current_item = 0;
    int   before_total = isr_hotbar_total(&plb.inv) + isr_main_total(&plb.inv);
    u32   before_break = plb.break_events;
    int   lxi = (int)floor(plb.ent.posX);
    int   lzi = (int)floor(plb.ent.posZ);
    gm_player_dig_reset();

    GmAction look_break;
    memset(&look_break, 0, sizeof look_break);
    look_break.dpitch = 89.0f;   /* first tick: clamp pitch to +89 */
    look_break.attack = 1;       /* hold dig */

    GmBlockEdit last_edit = {0};
    int saw_break = 0, last_nedits = 0;
    for (int t = 0; t < 40; ++t) {
        GmBlockEdit edits[8];
        int nedits = 0;
        if (t > 0) look_break.dpitch = 0.0f; /* already looking down */
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st, (struct PsvPlayer *)&plb,
                       (struct PvStats *)&tvb, look_break, ox, oy, oz, edits, &nedits, 8);
        last_nedits = nedits;
        if (nedits == 1 && edits[0].id == 0) {
            last_edit = edits[0];
            saw_break = 1;
            break;
        }
    }

    printf("  nedits = %d  edit=(%d,%d,%d) id=%d  pitch=%.2f\n",
           last_nedits, saw_break ? last_edit.wx : -1, saw_break ? last_edit.wy : -1,
           saw_break ? last_edit.wz : -1, saw_break ? last_edit.id : -1, plb.pitch);

    CHECK(saw_break, "exactly one break edit emitted");
    if (saw_break) {
        CHECK(last_edit.wx == ox + lxi, "edit wx == ox + floor(localX)");
        CHECK(last_edit.wz == oz + lzi, "edit wz == oz + floor(localZ)");
        CHECK(last_edit.wy == oy + 64,  "edit wy == oy + 64 (top floor block)");
        CHECK(last_edit.id == 0,        "break edit id == 0 (air)");
    }
    CHECK(plb.break_events == before_break + 1, "break_events incremented");
    CHECK(isr_hotbar_total(&plb.inv) + isr_main_total(&plb.inv) == before_total,
          "break does not teleport its drop into inventory");
    CHECK(last_edit.drop_id == 4 && last_edit.drop_count == 1,
          "stone harvest emits one cobblestone item entity request");

    /* ---------------- (C) UNDERWATER DIG PENALTY ---------------- */
    /* EntityPlayer.getDigSpeed: eye inside water without aqua affinity divides
     * dig speed by 5 (again by 5 if airborne). Iron pick vs stone is ~8 ticks
     * dry (case B); submerged it must take clearly longer but still finish. */
    printf("case C: underwater dig penalty (getDigSpeed /5)\n");
    fill_flat(win);
    isr_set_stack(&plb.inv, 0, ic_mk(257 /* iron pick */, 1, 0));
    plb.inv.current_item = 0;
    {
        /* flood the player's eye cell (and one above, so bobbing stays submerged) */
        int exi = (int)floor(plb.ent.posX), ezi = (int)floor(plb.ent.posZ);
        int eyi = (int)floor(plb.ent.posY + PSV_EYE_HEIGHT);
        psv_set_block(win, exi, eyi, ezi, 9);
        psv_set_block(win, exi, eyi + 1, ezi, 9);
        gm_player_dig_reset();
        GmAction wet = look_break;   /* pitch already at +89, attack held */
        wet.dpitch = 0.0f;
        int break_tick = -1;
        for (int t = 0; t < 400 && break_tick < 0; ++t) {
            GmBlockEdit edits[8];
            int nedits = 0;
            gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st, (struct PsvPlayer *)&plb,
                           (struct PvStats *)&tvb, wet, ox, oy, oz, edits, &nedits, 8);
            for (int i = 0; i < nedits; ++i)
                if (edits[i].id == 0 && edits[i].wy == oy + 64) break_tick = t;
        }
        printf("  underwater break tick = %d (dry ~8)\n", break_tick);
        CHECK(break_tick >= 0, "underwater dig still completes");
        CHECK(break_tick > 20, "underwater dig is penalized (>20 ticks vs ~8 dry)");
    }

    /* ---------------- (D) SWIMMING ---------------- */
    /* Vanilla water travel: 0.8 drag, sink 0.02/tick (slow terminal fall), and
     * holding jump (handleJumpWater +0.04/tick) swims UP. */
    printf("case D: swim physics (float up with jump, slow sink without)\n");
    fill_flat(win);
    {
        int cx = 24, cz = 24;
        for (int x = cx - 3; x <= cx + 3; ++x)
            for (int z = cz - 3; z <= cz + 3; ++z)
                for (int y = 65; y <= 72; ++y)
                    psv_set_block(win, x, y, z, 9);
        PsvPlayer sw; spawn_at(&sw, cx + 0.5, 66.0, cz + 0.5);
        PvStats sv; pv_init(&sv);
        GmAction swim; memset(&swim, 0, sizeof swim); swim.jump = 1;
        double y0 = sw.ent.posY;
        for (int t = 0; t < 40; ++t) {
            GmBlockEdit ed[4]; int ne = 0;
            gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st, (struct PsvPlayer *)&sw,
                           (struct PvStats *)&sv, swim, 0, 0, 0, ed, &ne, 4);
        }
        printf("  swim-up: y %.3f -> %.3f\n", y0, sw.ent.posY);
        CHECK(sw.ent.posY > y0 + 1.0, "holding jump in water swims up");

        double ytop = sw.ent.posY, max_fall = 0.0, prev = sw.ent.posY;
        GmAction idle2; memset(&idle2, 0, sizeof idle2);
        for (int t = 0; t < 40; ++t) {
            GmBlockEdit ed[4]; int ne = 0;
            gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st, (struct PsvPlayer *)&sw,
                           (struct PvStats *)&sv, idle2, 0, 0, 0, ed, &ne, 4);
            double d = prev - sw.ent.posY; if (d > max_fall) max_fall = d;
            prev = sw.ent.posY;
        }
        printf("  sink: y %.3f -> %.3f  max fall/tick %.4f\n", ytop, sw.ent.posY, max_fall);
        CHECK(sw.ent.posY < ytop, "released jump sinks");
        CHECK(max_fall < 0.15, "water sink is slow (terminal ~0.1/tick, not freefall)");
    }

    /* ---------------- (E) SNEAK EDGE-HANG ---------------- */
    /* Entity.move sneak clamp: sneaking on the ground clamps x/z motion so the
     * player hangs on the ledge instead of walking off. */
    printf("case E: sneak edge-hang\n");
    fill_flat(win);
    {
        /* 1-block-high pedestal column at (24,65,24); floor is y<=64 stone */
        psv_set_block(win, 24, 65, 24, 1);
        GmAction walk; memset(&walk, 0, sizeof walk); walk.forward = 1.0f;
        GmAction walk_sneak = walk; walk_sneak.sneak = 1;

        PsvPlayer sp; spawn_at(&sp, 24.5, 66.0, 24.5); sp.yaw = 0.0f; /* +Z */
        PvStats vv; pv_init(&vv);
        for (int t = 0; t < 60; ++t) {
            GmBlockEdit ed[4]; int ne = 0;
            gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st, (struct PsvPlayer *)&sp,
                           (struct PvStats *)&vv, walk_sneak, 0, 0, 0, ed, &ne, 4);
        }
        printf("  sneak walk-forward: y=%.3f z=%.3f on_ground=%d\n",
               sp.ent.posY, sp.ent.posZ, sp.ent.onGround);
        CHECK(sp.ent.posY == 66.0 && sp.ent.onGround, "sneaking player hangs on the ledge");

        PsvPlayer np; spawn_at(&np, 24.5, 66.0, 24.5); np.yaw = 0.0f;
        for (int t = 0; t < 60; ++t) {
            GmBlockEdit ed[4]; int ne = 0;
            gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st, (struct PsvPlayer *)&np,
                           (struct PvStats *)&vv, walk, 0, 0, 0, ed, &ne, 4);
        }
        printf("  plain walk-forward: y=%.3f z=%.3f\n", np.ent.posY, np.ent.posZ);
        CHECK(np.ent.posY < 66.0, "non-sneaking player walks off the ledge");
    }

    /* ---------------- (F) BLOCK CALLBACK/COLLISION EDGE CASES ------------ */
    printf("case F: slabs, trapdoors, slime, web, soul sand, fence vanilla mechanics\n");
    {
        McAABB blocks[PSV_MAX_BLOCKS];
        PsvAction idle; memset(&idle, 0, sizeof idle);

        fill_mechanics_floor(win);
        set_block_meta(win, 24, 3, 24, BLK_STONE_SLAB, 0);
        McAABB slab_query = mc_aabb_make(24.0, 3.0, 24.0, 25.0, 4.0, 25.0);
        int nslab = psv_collect_blocks(win, &slab_query, blocks, PSV_MAX_BLOCKS);
        int bottom_ok = 0;
        for (int i = 0; i < nslab; ++i)
            if (blocks[i].minX == 24.0 && blocks[i].minY == 3.0 &&
                blocks[i].minZ == 24.0 && blocks[i].maxY == 3.5)
                bottom_ok = 1;
        CHECK(bottom_ok, "bottom slab collision box is y..y+0.5");
        set_block_meta(win, 24, 3, 24, BLK_STONE_SLAB, 8);
        nslab = psv_collect_blocks(win, &slab_query, blocks, PSV_MAX_BLOCKS);
        int top_ok = 0;
        for (int i = 0; i < nslab; ++i)
            if (blocks[i].minX == 24.0 && blocks[i].minY == 3.5 &&
                blocks[i].minZ == 24.0 && blocks[i].maxY == 4.0)
                top_ok = 1;
        CHECK(top_ok, "top slab collision box is y+0.5..y+1");

        static const double trap_boxes[4][6] = {
            {0.0, 0.0, 0.8125, 1.0, 1.0, 1.0},
            {0.0, 0.0, 0.0, 1.0, 1.0, 0.1875},
            {0.8125, 0.0, 0.0, 1.0, 1.0, 1.0},
            {0.0, 0.0, 0.0, 0.1875, 1.0, 1.0},
        };
        for (int meta = 0; meta < 16; ++meta) {
            set_block_meta(win, 24, 3, 24, BLK_TRAPDOOR, meta);
            int nt = psv_collect_blocks(win, &slab_query, blocks, PSV_MAX_BLOCKS);
            double want[6];
            if (meta & 4) {
                for (int j = 0; j < 6; ++j)
                    want[j] = trap_boxes[meta & 3][j];
            } else {
                want[0] = 0.0; want[2] = 0.0;
                want[3] = 1.0; want[5] = 1.0;
                want[1] = (meta & 8) ? 0.8125 : 0.0;
                want[4] = (meta & 8) ? 1.0 : 0.1875;
            }
            int trap_ok = 0;
            for (int i = 0; i < nt; ++i)
                if (blocks[i].minX == 24.0 + want[0] &&
                    blocks[i].minY == 3.0 + want[1] &&
                    blocks[i].minZ == 24.0 + want[2] &&
                    blocks[i].maxX == 24.0 + want[3] &&
                    blocks[i].maxY == 3.0 + want[4] &&
                    blocks[i].maxZ == 24.0 + want[5])
                    trap_ok = 1;
            CHECK(trap_ok, "trapdoor collision box preserves facing/open/half");
        }

        fill_mechanics_floor(win);
        psv_set_block(win, 24, 3, 24, BLK_SLIME);
        PsvPlayer sl; spawn_at(&sl, 24.5, 4.0, 24.5);
        sl.ent.motionY = -1.2089724228714358;
        psv_physics_tick(win, &st, &sl, &idle, blocks);
        CHECK(sl.ent.posY == 4.0, "slime collision lands at the block top");
        CHECK(sl.ent.motionY == 1.106392995947447,
              "BlockSlime.onLanded negates motionY before gravity and drag");
        PsvPlayer sneaking; spawn_at(&sneaking, 24.5, 4.0, 24.5);
        sneaking.ent.motionY = -1.2089724228714358;
        PsvAction sneak = idle; sneak.sneak = 1;
        psv_physics_tick(win, &st, &sneaking, &sneak, blocks);
        CHECK(sneaking.ent.motionY == -0.0784000015258789,
              "sneaking suppresses BlockSlime.onLanded bounce");

        fill_mechanics_floor(win);
        psv_set_block(win, 24, 5, 24, BLK_WEB);
        PsvPlayer web; spawn_at(&web, 24.5, 6.436443751762071, 24.5);
        web.ent.motionY = -1.3163291646385942;
        psv_physics_tick(win, &st, &web, &idle, blocks);
        CHECK(web.ent.posY == 5.120114587123477,
              "BlockWeb first contact does not scale the current move");
        CHECK(web.is_in_web && web.fall_distance == 0.0f,
              "BlockWeb.onEntityCollidedWithBlock calls setInWeb");
        psv_physics_tick(win, &st, &web, &idle, blocks);
        CHECK(web.ent.posY == 5.051694455705003,
              "Entity.move consumes web latch with the 0.05 Y multiplier");
        CHECK(web.ent.motionY == -0.0784000015258789,
              "Entity.move clears webbed motion before gravity and drag");

        fill_mechanics_floor(win);
        psv_set_block(win, 24, 3, 24, BLK_SOUL_SAND);
        PsvPlayer soul; spawn_at(&soul, 24.5, 4.0, 24.5);
        soul.ent.motionX = 0.25; soul.ent.motionY = -0.0784000015258789;
        psv_physics_tick(win, &st, &soul, &idle, blocks);
        CHECK(soul.ent.posY == 3.921599998474121,
              "soul sand 0.875 AABB permits the first fall step");
        CHECK(soul.ent.motionX == 0.25 * 0.4 * (double)0.91f,
              "BlockSoulSand.onEntityCollidedWithBlock applies 0.4 XZ");
        psv_physics_tick(win, &st, &soul, &idle, blocks);
        CHECK(soul.ent.posY == 3.875, "player rests on soul sand at y + 0.875");

        fill_mechanics_floor(win);
        for (int z = -1; z <= 1; ++z)
            psv_set_block(win, 3, 4, z, BLK_NETHER_BRICK_FENCE);
        PsvPlayer fence; spawn_at(&fence, 3.0202805722711217, 5.252203340253724, 0.5);
        fence.ent.motionX = 0.15795508041190304;
        fence.ent.motionY = -0.07544406518948656;
        PsvAction east = idle; east.forward = 1.0f; east.yaw = -90.0f;
        psv_physics_tick(win, &st, &fence, &east, blocks);
        CHECK(fence.ent.posX == 3.074999988079071,
              "BlockFence 1.5-high arm clamps player center at x=3.075");

        /* BlockStairs straight collision: metadata rotates the raised half,
         * and bit 2 flips the slab and step vertically. */
        fill_mechanics_floor(win);
        {
            int lx, lz, ci = psv_chunk_index(24, 24, &lx, &lz);
            McAABB query = mc_aabb_make(24.0, 10.0, 24.0,
                                        25.0, 11.0, 25.0);
            for (int meta = 0; meta < 8; ++meta) {
                mc_set(&win[ci], lx, 10, lz,
                       mc_state(BLK_STONE_STAIRS, meta));
                int nstairs = psv_collect_blocks(win, &query, blocks,
                                                  PSV_MAX_BLOCKS);
                CHECK(nstairs == 2, "BlockStairs emits slab + step AABBs");
                CHECK(blocks[0].minY == (meta & 4 ? 10.5 : 10.0) &&
                      blocks[0].maxY == (meta & 4 ? 11.0 : 10.5),
                      "BlockStairs half metadata selects slab Y");
                CHECK(blocks[1].minY == (meta & 4 ? 10.0 : 10.5) &&
                      blocks[1].maxY == (meta & 4 ? 10.5 : 11.0),
                      "BlockStairs half metadata selects step Y");
                if ((meta & 3) == 0)
                    CHECK(blocks[1].minX == 24.5, "east stair raises east half");
                else if ((meta & 3) == 1)
                    CHECK(blocks[1].maxX == 24.5, "west stair raises west half");
                else if ((meta & 3) == 2)
                    CHECK(blocks[1].minZ == 24.5, "south stair raises south half");
                else
                    CHECK(blocks[1].maxZ == 24.5, "north stair raises north half");
            }
        }

        /* BlockLadder collision is the facing-specific 3/16 wall panel.
         * Travel while inside clamps horizontal speed, holds a sneaking fall,
         * and converts a forward wall collision into the 0.2 climb kick. */
        fill_mechanics_floor(win);
        {
            CHECK(mc_bpt_props(BLK_LADDER).light_opacity == 0,
                  "BlockLadder has vanilla zero light opacity");
            const double expected[4][4] = {
                {24.0, 25.0, 24.8125, 25.0}, /* north, metadata 2 */
                {24.0, 25.0, 24.0, 24.1875}, /* south, metadata 3 */
                {24.8125, 25.0, 24.0, 25.0}, /* west, metadata 4 */
                {24.0, 24.1875, 24.0, 25.0}  /* east, metadata 5 */
            };
            McAABB query = mc_aabb_make(24.0, 10.0, 24.0,
                                        25.0, 11.0, 25.0);
            for (int meta = 2; meta <= 5; ++meta) {
                set_block_meta(win, 24, 10, 24, BLK_LADDER, meta);
                int nladder = psv_collect_blocks(win, &query, blocks,
                                                  PSV_MAX_BLOCKS);
                CHECK(nladder == 1, "BlockLadder emits one panel AABB");
                CHECK(blocks[0].minX == expected[meta - 2][0] &&
                      blocks[0].maxX == expected[meta - 2][1] &&
                      blocks[0].minZ == expected[meta - 2][2] &&
                      blocks[0].maxZ == expected[meta - 2][3],
                      "BlockLadder metadata rotates the 3/16 panel");
            }

            set_block_meta(win, 24, 3, 24, BLK_LADDER, 2);
            PsvPlayer ladder;
            spawn_at(&ladder, 24.5, 3.0, 24.2);
            ladder.ent.motionZ = 0.3;
            ladder.fall_distance = 4.0f;
            psv_physics_tick(win, &st, &ladder, &idle, blocks);
            CHECK(ladder.ent.posZ == 24.350000005960464,
                  "ladder travel clamps horizontal displacement to 0.15");
            CHECK(ladder.fall_distance == 0.0f,
                  "ladder travel clears fall distance");

            PsvPlayer wall_climb;
            spawn_at(&wall_climb, 24.5, 3.0, 24.5);
            wall_climb.ent.motionZ = 0.3;
            psv_physics_tick(win, &st, &wall_climb, &idle, blocks);
            CHECK(wall_climb.ent.posZ == 24.51249998807907,
                  "ladder panel clamps the player center at its collision face");
            CHECK(wall_climb.ent.motionY == 0.11760000228881837,
                  "horizontal ladder collision applies the 0.2 climb kick");

            PsvPlayer sneak_hold;
            spawn_at(&sneak_hold, 24.5, 3.5, 24.5);
            sneak_hold.ent.motionY = -0.1;
            PsvAction ladder_sneak = idle;
            ladder_sneak.sneak = 1;
            psv_physics_tick(win, &st, &sneak_hold, &ladder_sneak, blocks);
            CHECK(sneak_hold.ent.posY == 3.5,
                  "sneaking on a ladder holds downward movement");
        }
    }

    /* ---------------- (G) ELYTRA TRAVEL BITWISE FIXTURE ------------------ */
    /* scenario_elytra_dip tape: oracle t55->t56 is the first elytra travel
     * tick (jump edge at t55, flag consumed at t56). Without the 1.11.2 elytra
     * branch the air path stays at x≈5.7460 (pre-port magma); with it, motion
     * matches the oracle binary64 payloads and position clears 5.8095. */
    printf("case G: elytra travel() oracle t55->t56 + activation\n");
    {
        McAABB blocks[PSV_MAX_BLOCKS];
        PsvAction idle; memset(&idle, 0, sizeof idle);
        memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
        for (int ci = 0; ci < PSV_NCHUNKS; ++ci) {
            win[ci].cx = (ci % PSV_DIM) - PSV_R;
            win[ci].cz = (ci / PSV_DIM) - PSV_R;
        }

        const double t55_x = 5.647897003352311;
        const double t55_y = 20.6537296175886;
        const double t55_vx = 0.09812996242519258;
        const double t55_vy = -0.7170746714356033;

        /* Freefall control: same seed, no elytra -> old magma x≈5.7460. */
        PsvPlayer fall;
        spawn_at(&fall, t55_x, t55_y, 0.5);
        fall.yaw = -90.0f;
        fall.pitch = 8.0f;
        fall.ent.motionX = t55_vx;
        fall.ent.motionY = t55_vy;
        psv_physics_tick(win, &st, &fall, &idle, blocks);
        CHECK(double_bits(fall.ent.posX) == 0x4016fbee7e2fcb41ULL,
              "non-elytra air path keeps the pre-port t56 x≈5.7460");

        /* Direct elytra branch: LUT look vector + lift/dive/couple/damp. */
        PsvPlayer fly;
        spawn_at(&fly, t55_x, t55_y, 0.5);
        fly.yaw = -90.0f;
        fly.pitch = 8.0f;
        fly.elytra_equipped = fly.elytra_flying = 1;
        fly.ent.motionX = t55_vx;
        fly.ent.motionY = t55_vy;
        psv_elytra_travel(win, &st, &fly, &idle, blocks);
        CHECK(double_bits(fly.ent.motionX) == 0x3fc4b10404bc5c59ULL,
              "elytra motionX matches oracle t56 binary64");
        CHECK(double_bits(fly.ent.motionY) == 0xbfe4e17c245e20d0ULL,
              "elytra motionY matches oracle t56 binary64");
        CHECK(double_bits(fly.ent.motionZ) == 0x0000000000000000ULL,
              "elytra motionZ matches oracle t56 binary64");
        CHECK(double_bits(fly.ent.posY) == 0x4034004ef1dd0732ULL,
              "elytra posY matches oracle t56 binary64");
        /* posX = t55_x + motionX (no collision); 1 ULP above the JSON tape
         * digitization of the same value — motion/Y are the fidelity anchors. */
        CHECK(double_bits(fly.ent.posX) == 0x40173cfa70082f41ULL,
              "elytra posX is t55_x+motionX (oracle x≈5.809549093722865)");
        CHECK(fly.ent.posX > 5.80 && fall.ent.posX < 5.75,
              "elytra path diverges from freefall at t56 (5.8095 vs 5.7460)");

        /* Looking up exercises Vec3d.lengthVector's float MathHelper.sqrt
         * boundary before the climb and coupling terms. */
        PsvPlayer climb;
        spawn_at(&climb, 8.0, 40.0, 8.0);
        climb.yaw = -90.0f;
        climb.pitch = -15.0f;
        climb.elytra_equipped = climb.elytra_flying = 1;
        climb.ent.motionX = 0.4;
        climb.ent.motionY = -0.2;
        psv_elytra_travel(win, &st, &climb, &idle, blocks);
        CHECK(double_bits(climb.ent.motionX) == 0x3fda4cbdf4026447ULL,
              "elytra climb motionX matches Java-order binary64");
        CHECK(double_bits(climb.ent.motionY) == 0xbfc7d140d45d8861ULL,
              "elytra climb motionY matches Java-order binary64");

        /* Full physics_tick entry + second oracle tick (t56 -> t57). */
        PsvPlayer path;
        spawn_at(&path, t55_x, t55_y, 0.5);
        path.yaw = -90.0f;
        path.pitch = 8.0f;
        path.elytra_equipped = path.elytra_flying = 1;
        path.ent.motionX = t55_vx;
        path.ent.motionY = t55_vy;
        psv_physics_tick(win, &st, &path, &idle, blocks);
        CHECK(path.ent.posX == fly.ent.posX && path.ent.motionX == fly.ent.motionX,
              "psv_physics_tick elytra branch matches psv_elytra_travel");
        psv_physics_tick(win, &st, &path, &idle, blocks);
        CHECK(double_bits(path.ent.posX) == 0x40181d217d244e14ULL,
              "elytra t57 posX matches oracle 6.028448062268144");
        CHECK(double_bits(path.ent.posY) == 0x403367de3d39b9bcULL,
              "elytra t57 posY matches oracle 19.405734850495477");
        CHECK(double_bits(path.ent.motionX) == 0x3fcc04e1a383da5fULL,
              "elytra t57 motionX matches oracle 0.218898968545278");
        CHECK(double_bits(path.ent.motionY) == 0xbfe30e169469aeb4ULL,
              "elytra t57 motionY matches oracle -0.5954697512329035");

        /* Fall-distance clamp when motionY > -0.5 (elytra branch). */
        PsvPlayer soft;
        spawn_at(&soft, 0.5, 40.0, 0.5);
        soft.elytra_equipped = soft.elytra_flying = 1;
        soft.fall_distance = 12.0f;
        soft.ent.motionY = -0.4;
        soft.yaw = -90.0f;
        soft.pitch = 8.0f;
        psv_elytra_travel(win, &st, &soft, &idle, blocks);
        CHECK(soft.fall_distance == 1.0f,
              "elytra sets fallDistance=1.0F when motionY > -0.5");

        /* Ground contact clears flag 7. */
        fill_flat(win);
        PsvPlayer land;
        spawn_at(&land, 24.0, 65.0, 24.0);
        land.elytra_equipped = land.elytra_flying = land.elytra_pose = 1;
        land.ent.box = psv_player_box(land.ent.posX, land.ent.posY, land.ent.posZ);
        land.ent.box.maxY = land.ent.box.minY + (double)0.6f;
        land.ent.onGround = 1;
        land.ent.motionY = 0.0;
        psv_physics_tick(win, &st, &land, &idle, blocks);
        CHECK(!land.elytra_flying, "updateElytra clears flag 7 on ground");
        /* EntityPlayer.updateSize: expand 0.6->1.8 when floor only touches
         * feet (strict AABB intersects). Broadphase-only collect wrongly
         * treated the floor as a blocker and left eye height at 0.4F. */
        psv_update_elytra_size(win, &land, blocks);
        CHECK(!land.elytra_pose, "updateSize clears elytra pose on open ground");
        CHECK(psv_player_eye_height(&land) == PSV_EYE_HEIGHT,
              "standing eye height restored after elytra land");
        CHECK(land.ent.box.maxY - land.ent.box.minY == (double)1.8f,
              "standing height 1.8F after expand");
        land.prev_sneak = 1;
        CHECK(psv_player_eye_height(&land) == (double)(1.62f - 0.08f),
              "sneaking eye height subtracts Java's 0.08F");
        land.prev_sneak = 0;

        /* Jump-edge deploy from tape t54: freefall to t55, elytra travel to t56. */
        memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
        for (int ci = 0; ci < PSV_NCHUNKS; ++ci) {
            win[ci].cx = (ci % PSV_DIM) - PSV_R;
            win[ci].cz = (ci / PSV_DIM) - PSV_R;
        }
        PsvPlayer deploy;
        spawn_at(&deploy, 5.540061882915932, 21.305438451751215, 0.5);
        deploy.yaw = -90.0f;
        deploy.pitch = 8.0f;
        deploy.elytra_equipped = 1;
        deploy.ent.motionX = 0.10783512043637802;
        deploy.ent.motionY = -0.6517088341626173;
        deploy.prev_jump = 0;
        PvStats dv; pv_init(&dv);
        GmAction jump_act; memset(&jump_act, 0, sizeof jump_act);
        jump_act.jump = 1;
        GmBlockEdit edits[4];
        int nedits = -1;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                       (struct PsvPlayer *)&deploy, (struct PvStats *)&dv,
                       jump_act, 0, 0, 0, edits, &nedits, 4);
        CHECK(deploy.elytra_flying_pending == 1,
              "jump edge stages START_FALL_FLYING after travel");
        CHECK(deploy.elytra_flying == 0,
              "flag 7 is not client-visible on the arming tick (metadata lag)");
        CHECK(!deploy.elytra_pose && psv_player_eye_height(&deploy) == PSV_EYE_HEIGHT,
              "arming tick keeps the 1.8F box and the 1.62 eye height");
        CHECK(deploy.ticks_elytra_flying == 0,
              "arming tick does not advance ticksElytraFlying");
        CHECK(double_bits(deploy.ent.motionX) == 0x3fb91f0b935fb8a1ULL,
              "arming tick freefall motionX matches oracle t55");
        CHECK(double_bits(deploy.ent.motionY) == 0xbfe6f24694d36338ULL,
              "arming tick freefall motionY matches oracle t55");
        nedits = -1;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                       (struct PsvPlayer *)&deploy, (struct PvStats *)&dv,
                       jump_act, 0, 0, 0, edits, &nedits, 4);
        CHECK(deploy.elytra_flying == 1, "elytra stays armed while airborne");
        CHECK(deploy.ticks_elytra_flying == 1,
              "first elytra travel tick advances ticksElytraFlying to 1");
        CHECK(double_bits(deploy.ent.motionX) == 0x3fc4b10404bc5c59ULL,
              "first armed travel motionX matches oracle t56");
        CHECK(double_bits(deploy.ent.motionY) == 0xbfe4e17c245e20d0ULL,
              "first armed travel motionY matches oracle t56");
        CHECK(double_bits(deploy.ent.posX) == 0x40173cfa70082f40ULL,
              "chained t54->t56 posX matches oracle x=5.809549093722865");
        CHECK(psv_player_eye_height(&deploy) == (double)0.4f,
              "elytra pose uses eye height 0.4F");

        /* Rising motion must not deploy (MC-111444). */
        PsvPlayer rise;
        spawn_at(&rise, 0.5, 30.0, 0.5);
        rise.elytra_equipped = 1;
        rise.ent.motionY = 0.2;
        rise.prev_jump = 0;
        GmAction rise_jump; memset(&rise_jump, 0, sizeof rise_jump);
        rise_jump.jump = 1;
        nedits = -1;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                       (struct PsvPlayer *)&rise, (struct PvStats *)&dv,
                       rise_jump, 0, 0, 0, edits, &nedits, 4);
        CHECK(!rise.elytra_flying && !rise.elytra_flying_pending,
              "MC-111444: jump while motionY>=0 does not start fall-flying");
    }

    /* ---- MC 1.11.2 item use: swords NONE, shield BLOCK; absorption not faked ---- */
    printf("case use-action: sword none / shield block / absorption zero\n");
    {
        fill_flat(win);
        PsvPlayer pu;
        spawn_at(&pu, 24.0, 65.0, 24.0);
        pu.ent.onGround = 1;
        PvStats vu; pv_init(&vu);
        GmAction use_act; memset(&use_act, 0, sizeof use_act);
        use_act.use = 1;
        GmBlockEdit edits[4];
        int nedits = 0;
        GmPlayerView v;
        int sword_ids[] = {267, 268, 272, 276, 283}; /* iron/wood/stone/diamond/gold */

        for (int si = 0; si < (int)(sizeof sword_ids / sizeof sword_ids[0]); ++si) {
            gm_player_dig_reset();
            isr_set_stack(&pu.inv, 0, ic_mk(sword_ids[si], 1, 0));
            pu.inv.current_item = 0;
            nedits = 0;
            gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                           (struct PsvPlayer *)&pu, (struct PvStats *)&vu,
                           use_act, 0, 0, 0, edits, &nedits, 4);
            memset(&v, 0, sizeof v);
            gm_player_view((const struct PsvPlayer *)&pu, 0, 0, &v);
            CHECK(v.use_action == 0,
                  "right-click sword does not set use_action (EnumAction.NONE)");
        }

        gm_player_dig_reset();
        isr_set_stack(&pu.inv, 0, ic_mk(442, 1, 0)); /* shield */
        pu.inv.current_item = 0;
        nedits = 0;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                       (struct PsvPlayer *)&pu, (struct PvStats *)&vu,
                       use_act, 0, 0, 0, edits, &nedits, 4);
        memset(&v, 0, sizeof v);
        gm_player_view((const struct PsvPlayer *)&pu, 0, 0, &v);
        CHECK(v.use_action == 2, "right-click shield sets use_action BLOCK");
        CHECK(v.use_max == 72000, "shield getMaxItemUseDuration is 72000");
        CHECK(v.use_remaining > 0 && v.use_remaining <= 72000,
              "shield use countdown started");
        CHECK(v.absorption == 0.0f,
              "live absorption stays 0 without vitals absorption field");
    }

    printf(g_fail ? "\nRESULT: FAIL\n" : "\nRESULT: PASS (all cases)\n");
    free(win);
    return g_fail;
}
