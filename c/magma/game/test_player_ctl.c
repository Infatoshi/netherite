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
    printf("case F: slime, web, soul sand, fence vanilla mechanics\n");
    {
        McAABB blocks[PSV_MAX_BLOCKS];
        PsvAction idle; memset(&idle, 0, sizeof idle);

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
    }

    printf(g_fail ? "\nRESULT: FAIL\n" : "\nRESULT: PASS (all cases)\n");
    free(win);
    return g_fail;
}
