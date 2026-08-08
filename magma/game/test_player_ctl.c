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

static int aabb_exact(McAABB a, McAABB b)
{
    return a.minX == b.minX && a.minY == b.minY && a.minZ == b.minZ
        && a.maxX == b.maxX && a.maxY == b.maxY && a.maxZ == b.maxZ;
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

static void set_test_state(Chunk *win,int x,int y,int z,int id,int meta)
{
    int lx,lz,ci=psv_chunk_index(x,z,&lx,&lz);
    if(ci>=0&&y>=0&&y<=255)
        mc_set(&win[ci],lx,y,lz,mc_state(id,meta));
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
    int fall_sound_count = 0, fall_damage = 0, fall_state = 0;
    PvStats tv; pv_init(&tv);
    for (int t = 0; t < 120; ++t) {
        GmBlockEdit edits[8];
        int nedits = -1;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st, (struct PsvPlayer *)&pl, (struct PvStats *)&tv, neutral, 0, 0, 0, edits, &nedits, 8);
        CHECK(nedits == 0, "neutral action emitted no edits");
        if (gm_player_take_fall_sound(&fall_damage, &fall_state))
            ++fall_sound_count;

        /* raw verified physics reference over the same window + zeroed action */
        McAABB blocks[PSV_MAX_BLOCKS];
        psv_physics_tick(win, &st, &ref, &zero, blocks);

        if (pl.ent.posY != ref.ent.posY) { parity_ok = 0; }
    }
    CHECK(parity_ok, "gm_player_tick posY bit-identical to psv_physics_tick every tick");
    printf("  landed feet y = %.10f  on_ground = %d\n", pl.ent.posY, pl.ent.onGround);
    CHECK(fabs(pl.ent.posY - 65.0) < 1e-6, "player rests at feet y == 65.0");
    CHECK(pl.ent.onGround == 1, "player on_ground == 1 at rest");
    CHECK(fall_sound_count == 1 && fall_damage == 12
          && fall_state == BLK_STONE,
          "damage landing emits one exact big-fall material transient");

    /* EntityPlayer selects small/big from the computed damage, not distance. */
    fill_flat(win);
    spawn_at(&pl, 24.0, 69.0, 24.0);
    pv_init(&tv);
    gm_player_dig_reset();
    fall_sound_count = fall_damage = fall_state = 0;
    float fall_health = 0.0F;
    for (int t = 0; t < 60; ++t) {
        GmBlockEdit edits[8];
        int nedits = -1;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                       (struct PsvPlayer *)&pl, (struct PvStats *)&tv,
                       neutral, 0, 0, 0, edits, &nedits, 8);
        if (gm_player_take_fall_sound(&fall_damage, &fall_state)) {
            ++fall_sound_count;
            fall_health = tv.health;
        }
    }
    CHECK(fall_sound_count == 1 && fall_damage == 1
          && fall_state == BLK_STONE && fall_health == 19.0F,
          "short damage landing emits one exact small-fall transient");

    /* BlockHay invokes Entity.fall with damageMultiplier=0.2F. */
    fill_flat(win);
    set_test_state(win, 24, 64, 24, 170, 0);
    spawn_at(&pl, 24.0, 80.0, 24.0);
    pv_init(&tv);
    gm_player_dig_reset();
    fall_sound_count = fall_damage = fall_state = 0;
    fall_health = 0.0F;
    for (int t = 0; t < 120; ++t) {
        GmBlockEdit edits[8];
        int nedits = -1;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                       (struct PsvPlayer *)&pl, (struct PvStats *)&tv,
                       neutral, 0, 0, 0, edits, &nedits, 8);
        if (gm_player_take_fall_sound(&fall_damage, &fall_state)) {
            ++fall_sound_count;
            fall_health = tv.health;
        }
    }
    CHECK(fall_sound_count == 1 && fall_damage == 3
          && fall_state == 170 && fall_health == 17.0F,
          "hay landing applies 0.2 multiplier before sound selection");

    double landed_local_y = pl.ent.posY;

    /* Entity.move accumulates actual displacement * 0.6F and emits only
     * after crossing the next integer distance threshold. */
    fill_flat(win);
    spawn_at(&pl, 24.0, 65.0, 24.0);
    pv_init(&tv);
    gm_player_dig_reset();
    GmAction walk = neutral;
    walk.forward = 1.0F;
    int step_count = 0, step_state = 0, step_tick = -1;
    for (int t = 0; t < 80 && step_count == 0; ++t) {
        GmBlockEdit edits[8];
        int nedits = -1;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                       (struct PsvPlayer *)&pl, (struct PvStats *)&tv,
                       walk, 0, 0, 0, edits, &nedits, 8);
        if (gm_player_take_step_sound(&step_state)) {
            ++step_count;
            step_tick = t;
        }
    }
    printf("  first stone step tick = %d\n", step_tick);
    CHECK(step_count == 1 && step_state == BLK_STONE && step_tick == 10,
          "walking crosses the first stone distance threshold at tick 10");

    fill_flat(win);
    for (int x = 20; x <= 30; ++x)
        for (int z = 20; z <= 30; ++z)
            set_test_state(win, x, 65, z, 78, 0);
    spawn_at(&pl, 24.0, 65.125, 24.0);
    pv_init(&tv);
    gm_player_dig_reset();
    step_count = step_state = 0;
    for (int t = 0; t < 80 && step_count == 0; ++t) {
        GmBlockEdit edits[8];
        int nedits = -1;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                       (struct PsvPlayer *)&pl, (struct PvStats *)&tv,
                       walk, 0, 0, 0, edits, &nedits, 8);
        if (gm_player_take_step_sound(&step_state)) ++step_count;
    }
    CHECK(step_count == 1 && (step_state & 4095) == 78,
          "snow layer above support overrides the footstep material");

    fill_flat(win);
    spawn_at(&pl, 24.0, 65.0, 24.0);
    pv_init(&tv);
    gm_player_dig_reset();
    walk.sneak = 1;
    step_count = step_state = 0;
    for (int t = 0; t < 80; ++t) {
        GmBlockEdit edits[8];
        int nedits = -1;
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                       (struct PsvPlayer *)&pl, (struct PvStats *)&tv,
                       walk, 0, 0, 0, edits, &nedits, 8);
        if (gm_player_take_step_sound(&step_state)) ++step_count;
    }
    CHECK(step_count == 0,
          "ground sneaking suppresses distance accumulation and footsteps");

    fill_flat(win);
    spawn_at(&pl, 24.0, 65.0, 24.0);
    pv_init(&tv);
    gm_player_dig_reset();
    walk.sneak = 0;
    step_count = step_state = 0;
    for (int t = 0; t < 80; ++t) {
        GmBlockEdit edits[8];
        int nedits = -1;
        gm_player_tick_network_client_effects(
            (struct Chunk *)win, (struct McSinTable *)&st,
            (struct PsvPlayer *)&pl, (struct PvStats *)&tv,
            walk, 0, 0, 0, edits, &nedits, 8, -1, -1, 1);
        if (gm_player_take_step_sound(&step_state)) ++step_count;
    }
    CHECK(step_count == 0,
          "riding suppresses distance accumulation and footsteps");

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
    float before_exhaustion = tvb.exhaustion;
    int   lxi = (int)floor(plb.ent.posX);
    int   lzi = (int)floor(plb.ent.posZ);
    gm_player_dig_reset();

    GmAction look_break;
    memset(&look_break, 0, sizeof look_break);
    look_break.dpitch = 89.0f;   /* first tick: clamp pitch to +89 */
    look_break.attack = 1;       /* hold dig */

    GmBlockEdit last_edit = {0};
    int saw_break = 0, last_nedits = 0;
    int hit_count = 0, hit_ticks[4] = {0};
    for (int t = 0; t < 40; ++t) {
        GmBlockEdit edits[8];
        int nedits = 0;
        if (t > 0) look_break.dpitch = 0.0f; /* already looking down */
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st, (struct PsvPlayer *)&plb,
                       (struct PvStats *)&tvb, look_break, ox, oy, oz, edits, &nedits, 8);
        {
            int sx, sy, sz, state_id;
            if (gm_player_take_dig_sound(&sx, &sy, &sz, &state_id)) {
                CHECK(hit_count < 4, "bounded progressive-dig sound cadence");
                hit_ticks[hit_count++] = t;
                CHECK(sx == ox + lxi && sy == oy + 64 && sz == oz + lzi,
                      "dig sound keeps exact world-space block center source");
                CHECK(state_id == BLK_STONE,
                      "dig sound keeps the target's exact legacy state");
            }
        }
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
        CHECK(last_edit.break_effect == 1,
              "player destruction requests world event 2001");
    }
    CHECK(plb.break_events == before_break + 1, "break_events incremented");
    CHECK(hit_count == 2 && hit_ticks[0] == 0 && hit_ticks[1] == 4,
          "iron-pick stone emits hit audio on damage updates 0 and 4 only");
    CHECK(isr_hotbar_total(&plb.inv) + isr_main_total(&plb.inv) == before_total,
          "break does not teleport its drop into inventory");
    CHECK(last_edit.drop_id == 4 && last_edit.drop_count == 1,
          "stone harvest emits one cobblestone item entity request");
    CHECK(fabsf(tvb.exhaustion - (before_exhaustion + 0.005f)) < 1e-7f,
          "harvestable block charges 0.005 exhaustion");

    /* ---------------- (B2) ITEMBLOCK PLACEMENT EFFECT ---------------- */
    printf("case B2: successful ItemBlock placement requests its sound\n");
    {
        PsvPlayer place;
        PvStats place_vitals;
        GmAction place_action;
        GmBlockEdit edits[4];
        int nedits = 0;
        fill_flat(win);
        set_test_state(win, 24, 66, 27, BLK_STONE, 0);
        spawn_at(&place, 24.5, 65.0, 24.5);
        place.yaw = 0.0f;
        place.pitch = 0.0f;
        isr_set_stack(&place.inv, 0, ic_mk(3 /* dirt */, 1, 0));
        place.inv.current_item = 0;
        pv_init(&place_vitals);
        memset(&place_action, 0, sizeof place_action);
        place_action.do_place = 1;
        gm_player_dig_reset();
        gm_player_tick((struct Chunk *)win, (struct McSinTable *)&st,
                       (struct PsvPlayer *)&place,
                       (struct PvStats *)&place_vitals,
                       place_action, ox, oy, oz, edits, &nedits, 4);
        CHECK(nedits == 1 && edits[0].id == 3,
              "successful dirt placement emits one block edit");
        if (nedits == 1) {
            CHECK(edits[0].wx == ox + 24 && edits[0].wy == oy + 66
                  && edits[0].wz == oz + 26,
                  "placement edit keeps exact world coordinates");
            CHECK(edits[0].place_effect == 1,
                  "successful ItemBlock edit requests placement SoundType");
            CHECK(edits[0].break_effect == 0,
                  "placement does not fabricate world event 2001");
        }
    }

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
    printf("case F: shaped blocks, slime, web, soul sand, fence, diode, brewing mechanics\n");
    {
        McAABB blocks[PSV_MAX_BLOCKS];
        PsvAction idle; memset(&idle, 0, sizeof idle);

        fill_mechanics_floor(win);
        psv_set_block(win, 24, 3, 24, BLK_CACTUS);
        McAABB cactus_query = mc_aabb_make(24.0, 3.0, 24.0, 25.0, 4.0, 25.0);
        int ncactus = psv_collect_blocks(win, &cactus_query, blocks, PSV_MAX_BLOCKS);
        int cactus_ok = 0;
        for (int i = 0; i < ncactus; ++i)
            if (blocks[i].minX == 24.0625 && blocks[i].minY == 3.0 &&
                blocks[i].minZ == 24.0625 && blocks[i].maxX == 24.9375 &&
                blocks[i].maxY == 3.9375 && blocks[i].maxZ == 24.9375)
                cactus_ok = 1;
        CHECK(cactus_ok, "BlockCactus collision is inset 1/16 on X/Z and at the top");

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

        {
            static const int diode_ids[] = {93, 94, 149, 150};
            int lx, lz;
            int ci = psv_chunk_index(24, 24, &lx, &lz);
            int diode_exact = 1;
            for (int id_index = 0; id_index < 4; ++id_index) {
                for (int meta = 0; meta < 16; ++meta) {
                    fill_mechanics_floor(win);
                    mc_set(&win[ci], lx, 3, lz,
                           mc_state(diode_ids[id_index], meta));
                    PsvPlayer diode;
                    spawn_at(&diode, 24.5, 3.125, 24.5);
                    diode.ent.motionY = -0.0784000015258789;
                    psv_physics_tick(win, &st, &diode, &idle, blocks);
                    if (diode.ent.posY != 3.125 || !diode.ent.onGround)
                        diode_exact = 0;
                }
            }
            CHECK(diode_exact,
                  "all repeater/comparator states collide at y + 0.125");

            fill_mechanics_floor(win);
            mc_set(&win[ci], lx, 3, lz, mc_state(55, 0));
            PsvPlayer wire;
            spawn_at(&wire, 24.5, 3.125, 24.5);
            wire.ent.motionY = -0.0784000015258789;
            psv_physics_tick(win, &st, &wire, &idle, blocks);
            CHECK(wire.ent.posY < 3.125 && !wire.ent.onGround,
                  "redstone wire remains collision-free");

            int brewing_exact = 1;
            for (int meta = 0; meta < 8; ++meta) {
                fill_mechanics_floor(win);
                mc_set(&win[ci], lx, 3, lz, mc_state(117, meta));
                PsvPlayer stem;
                spawn_at(&stem, 24.5, 3.875, 24.5);
                stem.ent.motionY = -0.0784000015258789;
                psv_physics_tick(win, &st, &stem, &idle, blocks);
                if (stem.ent.posY != 3.875 || !stem.ent.onGround)
                    brewing_exact = 0;

                PsvPlayer base;
                spawn_at(&base, 24.9, 3.125, 24.5);
                base.ent.motionY = -0.0784000015258789;
                psv_physics_tick(win, &st, &base, &idle, blocks);
                if (base.ent.posY != 3.125 || !base.ent.onGround)
                    brewing_exact = 0;
            }
            CHECK(brewing_exact,
                  "all brewing states collide on exact stem and base boxes");

            int enchanting_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                fill_mechanics_floor(win);
                mc_set(&win[ci], lx, 3, lz, mc_state(116, meta));
                PsvPlayer enchanting;
                spawn_at(&enchanting, 24.5, 3.75, 24.5);
                enchanting.ent.motionY = -0.0784000015258789;
                psv_physics_tick(win, &st, &enchanting, &idle, blocks);
                if (enchanting.ent.posY != 3.75 || !enchanting.ent.onGround)
                    enchanting_exact = 0;
            }
            CHECK(enchanting_exact,
                  "all enchanting-table states collide at y + 0.75");

            static const int low_full_ids[] = {60, 208};
            int low_full_exact = 1;
            for (int id_index = 0; id_index < 2; ++id_index) {
                for (int meta = 0; meta < 16; ++meta) {
                    fill_mechanics_floor(win);
                    mc_set(&win[ci], lx, 3, lz,
                           mc_state(low_full_ids[id_index], meta));
                    PsvPlayer low_full;
                    spawn_at(&low_full, 24.5, 3.9375, 24.5);
                    low_full.ent.motionY = -0.0784000015258789;
                    psv_physics_tick(win, &st, &low_full, &idle, blocks);
                    if (low_full.ent.posY != 3.9375 || !low_full.ent.onGround)
                        low_full_exact = 0;
                }
            }
            CHECK(low_full_exact,
                  "farmland/grass-path states collide at y + 0.9375");

            static const int slab_ids[] = {44, 126, 182, 205};
            int slab_exact = 1;
            for (int id_index = 0; id_index < 4; ++id_index) {
                for (int meta = 0; meta < 16; ++meta) {
                    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                    for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                        win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                        win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                    }
                    mc_set(&win[ci], lx, 3, lz,
                           mc_state(slab_ids[id_index], meta));
                    McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                    int count = psv_collect_blocks(
                        win, &query, blocks, PSV_MAX_BLOCKS);
                    double expected_min = (meta & 8) ? 3.5 : 3.0;
                    double expected_max = (meta & 8) ? 4.0 : 3.5;
                    if (count != 1 || blocks[0].minY != expected_min
                            || blocks[0].maxY != expected_max)
                        slab_exact = 0;
                }
            }
            CHECK(slab_exact,
                  "all single-slab states retain exact top/bottom half boxes");

            int carpet_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(171, meta));
                McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                if (count != 1 || blocks[0].minX != 24.0
                        || blocks[0].maxX != 25.0
                        || blocks[0].minY != 3.0
                        || blocks[0].maxY != 3.0625
                        || blocks[0].minZ != 24.0
                        || blocks[0].maxZ != 25.0)
                    carpet_exact = 0;
            }
            CHECK(carpet_exact,
                  "all carpet states retain the exact 1/16 collision box");

            int snow_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(78, meta));
                McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                int layers = meta & 7;
                if (layers == 0) {
                    if (count != 0) snow_exact = 0;
                } else if (count != 1 || blocks[0].minY != 3.0
                           || blocks[0].maxY != 3.0 + layers * 0.125) {
                    snow_exact = 0;
                }
            }
            CHECK(snow_exact,
                  "all snow-layer states retain exact metadata height");

            int cake_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(92, meta));
                McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                int bites = meta > 6 ? 6 : meta;
                double min_x = 24.0 + (1 + bites * 2) * 0.0625;
                if (count != 1 || blocks[0].minX != min_x
                        || blocks[0].maxX != 24.9375
                        || blocks[0].minY != 3.0
                        || blocks[0].maxY != 3.5
                        || blocks[0].minZ != 24.0625
                        || blocks[0].maxZ != 24.9375)
                    cake_exact = 0;
            }
            CHECK(cake_exact,
                  "all cake states retain exact bitten inset collision box");

            int bed_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(26, meta));
                McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                if (count != 1 || blocks[0].minX != 24.0
                        || blocks[0].maxX != 25.0
                        || blocks[0].minY != 3.0
                        || blocks[0].maxY != 3.5625
                        || blocks[0].minZ != 24.0
                        || blocks[0].maxZ != 25.0)
                    bed_exact = 0;
            }
            CHECK(bed_exact,
                  "all bed parts and facings retain the exact 9/16 box");

            static const int daylight_ids[] = {151, 178};
            int daylight_exact = 1;
            for (int id_index = 0; id_index < 2; ++id_index) {
                for (int meta = 0; meta < 16; ++meta) {
                    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                    for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                        win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                        win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                    }
                    mc_set(&win[ci], lx, 3, lz,
                           mc_state(daylight_ids[id_index], meta));
                    McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                    int count = psv_collect_blocks(
                        win, &query, blocks, PSV_MAX_BLOCKS);
                    if (count != 1 || blocks[0].minY != 3.0
                            || blocks[0].maxY != 3.375)
                        daylight_exact = 0;
                }
            }
            CHECK(daylight_exact,
                  "both daylight detectors retain the exact 3/8 box");

            int frame_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(120, meta));
                McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                int expected_count = (meta & 4) ? 2 : 1;
                if (count != expected_count || blocks[0].minY != 3.0
                        || blocks[0].maxY != 3.8125)
                    frame_exact = 0;
                if ((meta & 4) && (blocks[1].minX != 24.3125
                        || blocks[1].maxX != 24.6875
                        || blocks[1].minY != 3.8125
                        || blocks[1].maxY != 4.0
                        || blocks[1].minZ != 24.3125
                        || blocks[1].maxZ != 24.6875))
                    frame_exact = 0;
            }
            CHECK(frame_exact,
                  "end portal frames retain exact base and optional eye boxes");

            int ender_chest_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(130, meta));
                McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                if (count != 1 || blocks[0].minX != 24.0625
                        || blocks[0].maxX != 24.9375
                        || blocks[0].minY != 3.0
                        || blocks[0].maxY != 3.875
                        || blocks[0].minZ != 24.0625
                        || blocks[0].maxZ != 24.9375)
                    ender_chest_exact = 0;
            }
            CHECK(ender_chest_exact,
                  "all ender-chest states retain the exact inset 7/8 box");

            static const int trapdoor_ids[] = {96, 167};
            int trapdoor_exact = 1;
            for (int id_index = 0; id_index < 2; ++id_index) {
                for (int meta = 0; meta < 16; ++meta) {
                    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                    for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                        win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                        win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                    }
                    mc_set(&win[ci], lx, 3, lz,
                           mc_state(trapdoor_ids[id_index], meta));
                    McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                    int count = psv_collect_blocks(
                        win, &query, blocks, PSV_MAX_BLOCKS);
                    double x0 = 24.0, x1 = 25.0;
                    double y0 = 3.0, y1 = 4.0;
                    double z0 = 24.0, z1 = 25.0;
                    if (meta & 4) {
                        if ((meta & 3) == 0) z0 = 24.8125;
                        else if ((meta & 3) == 1) z1 = 24.1875;
                        else if ((meta & 3) == 2) x0 = 24.8125;
                        else x1 = 24.1875;
                    } else if (meta & 8) {
                        y0 = 3.8125;
                    } else {
                        y1 = 3.1875;
                    }
                    if (count != 1 || !aabb_exact(
                            blocks[0], mc_aabb_make(x0, y0, z0, x1, y1, z1)))
                        trapdoor_exact = 0;
                }
            }
            CHECK(trapdoor_exact,
                  "both trapdoors retain all exact open/top/bottom panels");

            memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
            for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
            }
            mc_set(&win[ci], lx, 3, lz, mc_state(199, 0));
            mc_set(&win[ci], lx - 1, 3, lz, mc_state(199, 0));
            mc_set(&win[ci], lx + 1, 3, lz, mc_state(200, 0));
            mc_set(&win[ci], lx, 4, lz, mc_state(199, 0));
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(200, 0));
            mc_set(&win[ci], lx, 3, lz + 1, mc_state(199, 0));
            McAABB chorus_query = mc_aabb_make(
                24.1, 3.1, 24.1, 24.9, 3.9, 24.9);
            int chorus_count = psv_collect_blocks(
                win, &chorus_query, blocks, PSV_MAX_BLOCKS);
            static const McAABB chorus_expected[] = {
                {24.1875, 3.1875, 24.1875, 24.8125, 3.8125, 24.8125},
                {24.0,    3.1875, 24.1875, 24.1875, 3.8125, 24.8125},
                {24.8125, 3.1875, 24.1875, 25.0,    3.8125, 24.8125},
                {24.1875, 3.8125, 24.1875, 24.8125, 4.0,    24.8125},
                {24.1875, 3.1875, 24.0,    24.8125, 3.8125, 24.1875},
                {24.1875, 3.1875, 24.8125, 24.8125, 3.8125, 25.0},
            };
            int chorus_exact = chorus_count == 6;
            for (int shape = 0; shape < 6 && chorus_exact; ++shape)
                chorus_exact = aabb_exact(blocks[shape], chorus_expected[shape]);

            memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
            for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
            }
            mc_set(&win[ci], lx, 2, lz, mc_state(121, 0));
            mc_set(&win[ci], lx, 3, lz, mc_state(199, 0));
            chorus_count = psv_collect_blocks(
                win, &chorus_query, blocks, PSV_MAX_BLOCKS);
            chorus_exact = chorus_exact && chorus_count == 3
                && aabb_exact(blocks[1], chorus_expected[0])
                && aabb_exact(blocks[2], mc_aabb_make(
                    24.1875, 3.0, 24.1875, 24.8125, 3.1875, 24.8125));
            CHECK(chorus_exact,
                  "chorus plant retains center and all six connection arms");

            static const int basin_ids[] = {118, 154};
            int basin_exact = 1;
            for (int id_index = 0; id_index < 2; ++id_index) {
                for (int meta = 0; meta < 16; ++meta) {
                    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                    for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                        win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                        win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                    }
                    mc_set(&win[ci], lx, 3, lz,
                           mc_state(basin_ids[id_index], meta));
                    McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                    int count = psv_collect_blocks(
                        win, &query, blocks, PSV_MAX_BLOCKS);
                    double base_max = basin_ids[id_index] == 118
                        ? 3.3125 : 3.625;
                    McAABB expected[] = {
                        mc_aabb_make(24, 3, 24, 25, base_max, 25),
                        mc_aabb_make(24, 3, 24, 24.125, 4, 25),
                        mc_aabb_make(24.875, 3, 24, 25, 4, 25),
                        mc_aabb_make(24, 3, 24, 25, 4, 24.125),
                        mc_aabb_make(24, 3, 24.875, 25, 4, 25),
                    };
                    if (count != 5) basin_exact = 0;
                    for (int shape = 0; shape < 5 && basin_exact; ++shape)
                        basin_exact = aabb_exact(blocks[shape], expected[shape]);
                }
            }
            CHECK(basin_exact,
                  "cauldron and hopper retain exact basin plus four rims");

            int flower_pot_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(140, meta));
                McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                if (count != 1 || !aabb_exact(blocks[0], mc_aabb_make(
                        24.3125, 3.0, 24.3125,
                        24.6875, 3.375, 24.6875)))
                    flower_pot_exact = 0;
            }
            CHECK(flower_pot_exact,
                  "all flower-pot states retain the exact inset 3/8 box");

            int cactus_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(81, meta));
                McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                if (count != 1 || !aabb_exact(blocks[0], mc_aabb_make(
                        24.0625, 3.0, 24.0625,
                        24.9375, 3.9375, 24.9375)))
                    cactus_exact = 0;

                PsvPlayer cactus;
                spawn_at(&cactus, 24.5, 3.9375, 24.5);
                cactus.ent.motionY = -0.0784000015258789;
                psv_physics_tick(win, &st, &cactus, &idle, blocks);
                if (cactus.ent.posY != 3.9375 || !cactus.ent.onGround
                        || !cactus.cactus_contact)
                    cactus_exact = 0;
            }
            CHECK(cactus_exact,
                  "all cactus states retain exact inset 15/16 collision and contact callback");

            int end_rod_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(198, meta));
                McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                int rod_meta = (meta & 7);
                int axis = rod_meta < 2 ? 1 : rod_meta < 4 ? 2 : 0;
                McAABB expected = mc_aabb_make(
                    axis == 0 ? 24.0 : 24.375,
                    axis == 1 ? 3.0 : 3.375,
                    axis == 2 ? 24.0 : 24.375,
                    axis == 0 ? 25.0 : 24.625,
                    axis == 1 ? 4.0 : 3.625,
                    axis == 2 ? 25.0 : 24.625);
                if (count != 1 || !aabb_exact(blocks[0], expected))
                    end_rod_exact = 0;
            }
            CHECK(end_rod_exact,
                  "all end-rod states retain exact metadata-derived axis boxes");

            int skull_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(144, meta));
                McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                int facing = (meta & 7) % 6;
                double x0 = 24.25, x1 = 24.75;
                double y0 = 3.25, y1 = 3.75;
                double z0 = 24.25, z1 = 24.75;
                if (facing == 0 || facing == 1) {
                    y0 = 3.0; y1 = 3.5;
                } else if (facing == 2) {
                    z0 = 24.5; z1 = 25.0;
                } else if (facing == 3) {
                    z0 = 24.0; z1 = 24.5;
                } else if (facing == 4) {
                    x0 = 24.5; x1 = 25.0;
                } else {
                    x0 = 24.0; x1 = 24.5;
                }
                if (count != 1 || !aabb_exact(blocks[0],
                        mc_aabb_make(x0, y0, z0, x1, y1, z1)))
                    skull_exact = 0;
            }
            CHECK(skull_exact,
                  "all skull states retain exact six-facing half-block boxes");

            int lily_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(111, meta));
                McAABB query = mc_aabb_make(24, 3, 24, 25, 4, 25);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                if (count != 1 || !aabb_exact(blocks[0], mc_aabb_make(
                        24.0625, 3.0, 24.0625,
                        24.9375, 3.09375, 24.9375)))
                    lily_exact = 0;
            }
            CHECK(lily_exact,
                  "all lily-pad states retain the exact inset 3/32 box");

            static const int chest_ids[] = {54, 146};
            int chest_exact = 1;
            for (int id_index = 0; id_index < 2; ++id_index) {
                for (int neighbor = -1; neighbor < 4; ++neighbor) {
                    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                    for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                        win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                        win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                    }
                    int id = chest_ids[id_index];
                    mc_set(&win[ci], lx, 3, lz, mc_state(id, 2));
                    if (neighbor >= 0) {
                        static const int dx[] = {0, 0, -1, 1};
                        static const int dz[] = {-1, 1, 0, 0};
                        mc_set(&win[ci], lx + dx[neighbor], 3,
                               lz + dz[neighbor], mc_state(id, 3));
                    }
                    McAABB query = mc_aabb_make(
                        24.1, 3.0, 24.1, 24.9, 4.0, 24.9);
                    int count = psv_collect_blocks(
                        win, &query, blocks, PSV_MAX_BLOCKS);
                    double min_x = neighbor == 2 ? 24.0 : 24.0625;
                    double max_x = neighbor == 3 ? 25.0 : 24.9375;
                    double min_z = neighbor == 0 ? 24.0 : 24.0625;
                    double max_z = neighbor == 1 ? 25.0 : 24.9375;
                    if (count != 1 || !aabb_exact(blocks[0], mc_aabb_make(
                            min_x, 3.0, min_z,
                            max_x, 3.875, max_z)))
                        chest_exact = 0;
                }
            }
            memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
            for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
            }
            mc_set(&win[ci], lx, 3, lz, mc_state(54, 2));
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(146, 3));
            McAABB chest_query = mc_aabb_make(
                24.1, 3.0, 24.1, 24.9, 4.0, 24.9);
            int chest_count = psv_collect_blocks(
                win, &chest_query, blocks, PSV_MAX_BLOCKS);
            if (chest_count != 1 || !aabb_exact(blocks[0], mc_aabb_make(
                    24.0625, 3.0, 24.0625,
                    24.9375, 3.875, 24.9375)))
                chest_exact = 0;
            CHECK(chest_exact,
                  "ordinary and trapped chests retain exact joined 7/8 boxes");

            static const int stair_ids[] = {
                53, 67, 108, 109, 114, 128, 134,
                135, 136, 156, 163, 164, 180, 203,
            };
            int stair_exact = 1;
            for (int id_index = 0; id_index < 14; ++id_index) {
                for (int meta = 0; meta < 8; ++meta) {
                    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                    for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                        win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                        win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                    }
                    mc_set(&win[ci], lx, 3, lz,
                           mc_state(stair_ids[id_index], meta));
                    McAABB query = mc_aabb_make(
                        24.1, 3.0, 24.1, 24.9, 4.0, 24.9);
                    int count = psv_collect_blocks(
                        win, &query, blocks, PSV_MAX_BLOCKS);
                    int facing = 5 - (meta & 3);
                    int top = (meta & 4) != 0;
                    double x0 = 24.0, x1 = 25.0;
                    double z0 = 24.0, z1 = 25.0;
                    if (facing == 2) z1 = 24.5;
                    else if (facing == 3) z0 = 24.5;
                    else if (facing == 4) x1 = 24.5;
                    else x0 = 24.5;
                    McAABB base = mc_aabb_make(
                        24.0, top ? 3.5 : 3.0, 24.0,
                        25.0, top ? 4.0 : 3.5, 25.0);
                    McAABB step = mc_aabb_make(
                        x0, top ? 3.0 : 3.5, z0,
                        x1, top ? 3.5 : 4.0, z1);
                    if (count != 2 || !aabb_exact(blocks[0], base)
                            || !aabb_exact(blocks[1], step))
                        stair_exact = 0;
                }
            }
            static const int corner_neighbor_z[] = {-1, -1, 1, 1};
            static const int corner_neighbor_meta[] = {1, 0, 1, 0};
            static const PsvStairShape corner_shape[] = {
                PSV_STAIR_OUTER_LEFT, PSV_STAIR_OUTER_RIGHT,
                PSV_STAIR_INNER_LEFT, PSV_STAIR_INNER_RIGHT,
            };
            static const double corner_x0[] = {24.0, 24.5, 24.0, 24.5};
            static const double corner_z0[] = {24.0, 24.0, 24.5, 24.5};
            for (int top = 0; top < 2; ++top) {
                for (int corner = 0; corner < 4; ++corner) {
                    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                    for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                        win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                        win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                    }
                    int top_bit = top ? 4 : 0;
                    mc_set(&win[ci], lx, 3, lz, mc_state(53, 3 | top_bit));
                    mc_set(&win[ci], lx, 3,
                           lz + corner_neighbor_z[corner],
                           mc_state(53, corner_neighbor_meta[corner] | top_bit));
                    McAABB shapes[3];
                    int count = psv_stair_collision_shapes(
                        win, 24, 3, 24, 3 | top_bit, shapes);
                    McAABB expected_corner = mc_aabb_make(
                        corner_x0[corner], top ? 3.0 : 3.5,
                        corner_z0[corner], corner_x0[corner] + 0.5,
                        top ? 3.5 : 4.0, corner_z0[corner] + 0.5);
                    int expected_count = corner < 2 ? 2 : 3;
                    if (psv_stair_shape(
                            win, 24, 3, 24, 3 | top_bit)
                                != corner_shape[corner]
                            || count != expected_count
                            || !aabb_exact(shapes[count - 1], expected_corner))
                        stair_exact = 0;
                }
            }
            CHECK(stair_exact,
                  "all stair IDs retain exact straight and corner boxes");

            static const int pane_ids[] = {101, 102, 160};
            int pane_exact = 1;
            for (int id_index = 0; id_index < 3; ++id_index) {
                for (int meta = 0; meta < 16; ++meta) {
                    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                    for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                        win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                        win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                    }
                    mc_set(&win[ci], lx, 3, lz,
                           mc_state(pane_ids[id_index], meta));
                    McAABB shapes[5];
                    int count = psv_pane_collision_shapes(
                        win, 24, 3, 24, shapes);
                    if (count != 1 || !aabb_exact(shapes[0], mc_aabb_make(
                            24.4375, 3.0, 24.4375,
                            24.5625, 4.0, 24.5625)))
                        pane_exact = 0;
                }
            }

            memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
            for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
            }
            mc_set(&win[ci], lx, 3, lz, mc_state(102, 0));
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(101, 0));
            mc_set(&win[ci], lx + 1, 3, lz, mc_state(20, 0));
            mc_set(&win[ci], lx, 3, lz + 1, mc_state(95, 5));
            mc_set(&win[ci], lx - 1, 3, lz, mc_state(1, 0));
            McAABB pane_shapes[5];
            int pane_count = psv_pane_collision_shapes(
                win, 24, 3, 24, pane_shapes);
            static const McAABB pane_expected[] = {
                {24.4375, 3.0, 24.4375, 24.5625, 4.0, 24.5625},
                {24.4375, 3.0, 24.0,    24.5625, 4.0, 24.4375},
                {24.5625, 3.0, 24.4375, 25.0,    4.0, 24.5625},
                {24.4375, 3.0, 24.5625, 24.5625, 4.0, 25.0},
                {24.0,    3.0, 24.4375, 24.4375, 4.0, 24.5625},
            };
            if (pane_count != 5) pane_exact = 0;
            for (int shape = 0; shape < 5 && pane_exact; ++shape)
                pane_exact = aabb_exact(pane_shapes[shape], pane_expected[shape]);

            /* Forge side-solid exceptions and actual-state stair sides. */
            memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
            for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
            }
            mc_set(&win[ci], lx, 3, lz, mc_state(102, 0));
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(60, 0));
            if (!psv_pane_connects(win, 24, 3, 24, 2)) pane_exact = 0;
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(78, 6));
            if (psv_pane_connects(win, 24, 3, 24, 2)) pane_exact = 0;
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(78, 7));
            if (!psv_pane_connects(win, 24, 3, 24, 2)) pane_exact = 0;
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(53, 2));
            if (!psv_pane_connects(win, 24, 3, 24, 2)) pane_exact = 0;
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(53, 3));
            if (psv_pane_connects(win, 24, 3, 24, 2)) pane_exact = 0;
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(152, 0));
            if (!psv_pane_connects(win, 24, 3, 24, 2)) pane_exact = 0;
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(54, 0));
            if (psv_pane_connects(win, 24, 3, 24, 2)) pane_exact = 0;
            CHECK(pane_exact,
                  "all pane IDs retain exact connected post/arm collision boxes");

            static const int piston_base_ids[] = {29, 33};
            int piston_base_exact = 1;
            for (int id_index = 0; id_index < 2; ++id_index) {
                for (int meta = 0; meta < 16; ++meta) {
                    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                    for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                        win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                        win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                    }
                    mc_set(&win[ci], lx, 3, lz,
                           mc_state(piston_base_ids[id_index], meta));
                    McAABB query = mc_aabb_make(
                        24.1, 3.0, 24.1, 24.9, 4.0, 24.9);
                    int count = psv_collect_blocks(
                        win, &query, blocks, PSV_MAX_BLOCKS);
                    int facing = meta & 7;
                    double x0 = 24.0, x1 = 25.0;
                    double y0 = 3.0, y1 = 4.0;
                    double z0 = 24.0, z1 = 25.0;
                    if ((meta & 8) && facing <= 5) {
                        if (facing == 0) y0 = 3.25;
                        else if (facing == 1) y1 = 3.75;
                        else if (facing == 2) z0 = 24.25;
                        else if (facing == 3) z1 = 24.75;
                        else if (facing == 4) x0 = 24.25;
                        else x1 = 24.75;
                    }
                    if (count != 1 || !aabb_exact(blocks[0],
                            mc_aabb_make(x0, y0, z0, x1, y1, z1)))
                        piston_base_exact = 0;
                }
            }
            CHECK(piston_base_exact,
                  "both piston bases retain exact retracted/extended bodies");

            int anvil_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(145, meta));
                McAABB query = mc_aabb_make(
                    24.1, 3.0, 24.1, 24.9, 4.0, 24.9);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                McAABB expected = (meta & 1)
                    ? mc_aabb_make(24.0, 3.0, 24.125,
                                   25.0, 4.0, 24.875)
                    : mc_aabb_make(24.125, 3.0, 24.0,
                                   24.875, 4.0, 25.0);
                if (count != 1 || !aabb_exact(blocks[0], expected))
                    anvil_exact = 0;
            }
            CHECK(anvil_exact,
                  "all anvil damage/facing states retain exact inset axes");

            int dragon_egg_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(122, meta));
                McAABB query = mc_aabb_make(
                    24.1, 3.0, 24.1, 24.9, 4.0, 24.9);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                if (count != 1 || !aabb_exact(blocks[0], mc_aabb_make(
                        24.0625, 3.0, 24.0625,
                        24.9375, 4.0, 24.9375)))
                    dragon_egg_exact = 0;
            }
            CHECK(dragon_egg_exact,
                  "all dragon-egg states retain exact horizontal inset");

            static const int fence_ids[] = {
                85, 113, 188, 189, 190, 191, 192,
            };
            int fence_exact = 1;
            for (int id_index = 0; id_index < 7; ++id_index) {
                for (int meta = 0; meta < 16; ++meta) {
                    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                    for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                        win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                        win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                    }
                    mc_set(&win[ci], lx, 3, lz,
                           mc_state(fence_ids[id_index], meta));
                    McAABB query = mc_aabb_make(
                        24.1, 3.0, 24.1, 24.9, 4.0, 24.9);
                    int count = psv_collect_blocks(
                        win, &query, blocks, PSV_MAX_BLOCKS);
                    if (count != 1 || !aabb_exact(blocks[0], mc_aabb_make(
                            24.375, 3.0, 24.375,
                            24.625, 4.5, 24.625)))
                        fence_exact = 0;
                }
            }

            memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
            for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
            }
            mc_set(&win[ci], lx, 3, lz, mc_state(188, 0));
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(189, 0));
            mc_set(&win[ci], lx + 1, 3, lz, mc_state(107, 0));
            mc_set(&win[ci], lx, 3, lz + 1, mc_state(152, 0));
            mc_set(&win[ci], lx - 1, 3, lz, mc_state(218, 0));
            McAABB fence_query = mc_aabb_make(
                24.1, 3.0, 24.1, 24.9, 4.0, 24.9);
            int fence_count = psv_collect_blocks(
                win, &fence_query, blocks, PSV_MAX_BLOCKS);
            static const McAABB fence_expected[] = {
                {24.375, 3.0, 24.375, 24.625, 4.5, 24.625},
                {24.375, 3.0, 24.0,   24.625, 4.5, 24.375},
                {24.625, 3.0, 24.375, 25.0,   4.5, 24.625},
                {24.375, 3.0, 24.625, 24.625, 4.5, 25.0},
                {24.0,   3.0, 24.375, 24.375, 4.5, 24.625},
            };
            if (fence_count != 5) fence_exact = 0;
            for (int shape = 0; shape < 5 && fence_exact; ++shape)
                fence_exact = aabb_exact(blocks[shape], fence_expected[shape]);

            mc_set(&win[ci], lx, 3, lz - 1, mc_state(113, 0));
            mc_set(&win[ci], lx + 1, 3, lz, mc_state(86, 0));
            mc_set(&win[ci], lx, 3, lz + 1, mc_state(166, 0));
            mc_set(&win[ci], lx - 1, 3, lz, mc_state(79, 0));
            if (psv_fence_connects(win, 188, 24, 3, 23)
                    || psv_fence_connects(win, 188, 25, 3, 24)
                    || psv_fence_connects(win, 188, 24, 3, 25)
                    || psv_fence_connects(win, 188, 23, 3, 24))
                fence_exact = 0;
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(113, 0));
            if (!psv_fence_connects(win, 113, 24, 3, 23))
                fence_exact = 0;
            CHECK(fence_exact,
                  "all fence IDs retain exact posts, arms, and connections");

            static const int gate_ids[] = {107, 183, 184, 185, 186, 187};
            int gate_exact = 1;
            for (int id_index = 0; id_index < 6; ++id_index) {
                for (int meta = 0; meta < 16; ++meta) {
                    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                    for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                        win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                        win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                    }
                    mc_set(&win[ci], lx, 3, lz,
                           mc_state(gate_ids[id_index], meta));
                    McAABB query = mc_aabb_make(
                        24.1, 3.0, 24.1, 24.9, 4.0, 24.9);
                    int count = psv_collect_blocks(
                        win, &query, blocks, PSV_MAX_BLOCKS);
                    if (meta & 4) {
                        if (count != 0) gate_exact = 0;
                    } else {
                        McAABB expected = (meta & 1)
                            ? mc_aabb_make(24.375, 3.0, 24.0,
                                           24.625, 4.5, 25.0)
                            : mc_aabb_make(24.0, 3.0, 24.375,
                                           25.0, 4.5, 24.625);
                        if (count != 1 || !aabb_exact(blocks[0], expected))
                            gate_exact = 0;
                    }
                }
            }
            CHECK(gate_exact,
                  "all fence gates retain exact open and closed-axis collision");

            static const int door_ids[] = {64, 71, 193, 194, 195, 196, 197};
            static const unsigned char door_closed_panel[] = {0, 2, 1, 3};
            static const unsigned char door_open_panel[4][2] = {
                {2, 3}, {1, 0}, {3, 2}, {0, 1},
            };
            int door_exact = 1;
            for (int id_index = 0; id_index < 7; ++id_index) {
                for (int lower_meta = 0; lower_meta < 8; ++lower_meta) {
                    for (int upper_bits = 0; upper_bits < 4; ++upper_bits) {
                        memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                        for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                            win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                            win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                        }
                        int id = door_ids[id_index];
                        int upper_meta = 8 | upper_bits;
                        mc_set(&win[ci], lx, 3, lz,
                               mc_state(id, lower_meta));
                        mc_set(&win[ci], lx, 4, lz,
                               mc_state(id, upper_meta));
                        int panel = (lower_meta & 4)
                            ? door_open_panel[lower_meta & 3][upper_bits & 1]
                            : door_closed_panel[lower_meta & 3];
                        double x0 = 24.0, x1 = 25.0;
                        double z0 = 24.0, z1 = 25.0;
                        if (panel == 0) x1 = 24.1875;
                        else if (panel == 1) x0 = 24.8125;
                        else if (panel == 2) z1 = 24.1875;
                        else z0 = 24.8125;
                        McAABB lower = psv_door_collision_shape(
                            win, 24, 3, 24, id, lower_meta);
                        McAABB upper = psv_door_collision_shape(
                            win, 24, 4, 24, id, upper_meta);
                        if (!aabb_exact(lower, mc_aabb_make(
                                x0, 3.0, z0, x1, 4.0, z1))
                                || !aabb_exact(upper, mc_aabb_make(
                                    x0, 4.0, z0, x1, 5.0, z1)))
                            door_exact = 0;
                    }
                }
            }
            CHECK(door_exact,
                  "all paired door IDs retain exact facing/open/hinge panels");

            int ladder_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(65, meta));
                McAABB query = mc_aabb_make(
                    24.1, 3.0, 24.1, 24.9, 4.0, 24.9);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                int facing = meta % 6;
                double x0 = 24.0, x1 = 25.0;
                double z0 = 24.0, z1 = 25.0;
                if (facing <= 2) z0 = 24.8125;
                else if (facing == 3) z1 = 24.1875;
                else if (facing == 4) x0 = 24.8125;
                else x1 = 24.1875;
                if (count != 1 || !aabb_exact(blocks[0],
                        mc_aabb_make(x0, 3.0, z0, x1, 4.0, z1)))
                    ladder_exact = 0;
            }
            CHECK(ladder_exact,
                  "all ladder metadata retains exact six-facing panels");

            fill_mechanics_floor(win);
            mc_set(&win[ci], lx, 3, lz - 2, mc_state(BLK_STONE, 0));
            mc_set(&win[ci], lx, 3, lz - 1, mc_state(65, 3));
            PsvPlayer ladder;
            spawn_at(&ladder, 24.5, 3.0, 24.5);
            ladder.ent.onGround = 1;
            ladder.ent.motionY = -0.0784000015258789;
            ladder.yaw = -180.0f;
            PsvAction ladder_move;
            memset(&ladder_move, 0, sizeof ladder_move);
            ladder_move.forward = 1.0f;
            ladder_move.yaw = -180.0f;
            double ladder_contact_z = 0.0;
            double ladder_climb_y = 0.0;
            for (int tick = 0; tick < 20; ++tick) {
                psv_physics_tick(
                    win, &st, &ladder, &ladder_move, blocks);
                if (tick == 6) ladder_contact_z = ladder.ent.posZ;
                if (tick == 15) ladder_climb_y = ladder.ent.posY;
            }
            CHECK(ladder_contact_z == 23.487500011920929
                      && ladder_climb_y == 4.0584000205993656
                      && ladder.ent.posY == 4.0
                      && ladder.ent.posZ == 23.148083054549105,
                  "ladder travel retains exact clamp, climb, and release path");

            fill_mechanics_floor(win);
            PsvPlayer ladder_probe;
            spawn_at(&ladder_probe, 24.5, 3.0, 24.5);
            mc_set(&win[ci], lx, 3, lz, mc_state(106, 0));
            int ladder_identity_exact = psv_is_on_ladder(
                win, &ladder_probe.ent);
            mc_set(&win[ci], lx, 3, lz, mc_state(65, 3));
            ladder_probe.ent.posY = 4.0;
            ladder_probe.ent.box = psv_player_box(24.5, 4.0, 24.5);
            mc_set(&win[ci], lx, 4, lz, mc_state(96, 5));
            ladder_identity_exact = ladder_identity_exact
                && psv_is_on_ladder(win, &ladder_probe.ent);
            mc_set(&win[ci], lx, 4, lz, mc_state(96, 4));
            ladder_identity_exact = ladder_identity_exact
                && !psv_is_on_ladder(win, &ladder_probe.ent);
            CHECK(ladder_identity_exact,
                  "vine and matching open trapdoor retain ladder identity");

            int cocoa_exact = 1;
            for (int meta = 0; meta < 16; ++meta) {
                memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
                for (int init_ci = 0; init_ci < PSV_NCHUNKS; ++init_ci) {
                    win[init_ci].cx = (init_ci % PSV_DIM) - PSV_R;
                    win[init_ci].cz = (init_ci / PSV_DIM) - PSV_R;
                }
                mc_set(&win[ci], lx, 3, lz, mc_state(127, meta));
                McAABB query = mc_aabb_make(
                    24.1, 3.0, 24.1, 24.9, 4.0, 24.9);
                int count = psv_collect_blocks(
                    win, &query, blocks, PSV_MAX_BLOCKS);
                int facing = meta & 3;
                int age = (meta >> 2) & 3;
                if (age > 2) age = 2;
                double width = (age + 2) / 8.0;
                double half = width * 0.5;
                double x0 = 24.5 - half, x1 = 24.5 + half;
                double z0 = 24.5 - half, z1 = 24.5 + half;
                if (facing == 0) {
                    z0 = 24.9375 - width; z1 = 24.9375;
                } else if (facing == 1) {
                    x0 = 24.0625; x1 = x0 + width;
                } else if (facing == 2) {
                    z0 = 24.0625; z1 = z0 + width;
                } else {
                    x0 = 24.9375 - width; x1 = 24.9375;
                }
                if (count != 1 || !aabb_exact(blocks[0], mc_aabb_make(
                        x0, 3.4375 - age * 0.125, z0,
                        x1, 3.75, z1)))
                    cocoa_exact = 0;
            }
            CHECK(cocoa_exact,
                  "all cocoa ages/facings retain exact attached pod boxes");
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

    /* ---- ItemGlassBottle: water ray + exact InventoryPlayer transform ---- */
    printf("case glass-bottle: source/flowing water + stack/full inventory\n");
    {
        GmAction fill;memset(&fill,0,sizeof fill);fill.do_place=1;
        GmBlockEdit edits[4];int nedits=0;
        PvStats bv;pv_init(&bv);
        PsvPlayer bottle;

        fill_flat(win);
        set_test_state(win,24,66,27,9,0);
        spawn_at(&bottle,24.5,65.0,24.5);
        isr_set_stack(&bottle.inv,0,ic_mk(374,1,0));
        bottle.inv.current_item=0;
        gm_player_tick((struct Chunk *)win,(struct McSinTable *)&st,
                       (struct PsvPlayer *)&bottle,(struct PvStats *)&bv,
                       fill,0,0,0,edits,&nedits,4);
        ICStack held=isr_get_stack(&bottle.inv,0);
        CHECK(held.item==373&&held.count==1&&held.meta==1,
              "one glass bottle becomes one water potion");
        CHECK(psv_get_block(win,24,66,27)==9,
              "bottle fill does not consume water block");
        CHECK(nedits==0,"bottle fill emits no block edit");

        fill_flat(win);
        set_test_state(win,24,66,27,8,3);
        spawn_at(&bottle,24.5,65.0,24.5);
        isr_set_stack(&bottle.inv,0,ic_mk(374,3,0));
        bottle.inv.current_item=0;nedits=0;
        gm_player_tick((struct Chunk *)win,(struct McSinTable *)&st,
                       (struct PsvPlayer *)&bottle,(struct PvStats *)&bv,
                       fill,0,0,0,edits,&nedits,4);
        held=isr_get_stack(&bottle.inv,0);
        ICStack water=isr_get_stack(&bottle.inv,1);
        CHECK(held.item==374&&held.count==2,
              "bottle stack shrinks by one");
        CHECK(water.item==373&&water.count==1&&water.meta==1,
              "flowing water fills a potion into first empty inventory slot");

        fill_flat(win);
        set_test_state(win,24,66,27,9,0);
        spawn_at(&bottle,24.5,65.0,24.5);
        for(int q=0;q<ISR_MAIN_SLOTS;++q)
            isr_set_stack(&bottle.inv,q,ic_mk(1,64,0));
        isr_set_stack(&bottle.inv,0,ic_mk(374,2,0));
        bottle.inv.current_item=0;nedits=0;
        gm_player_tick((struct Chunk *)win,(struct McSinTable *)&st,
                       (struct PsvPlayer *)&bottle,(struct PvStats *)&bv,
                       fill,0,0,0,edits,&nedits,4);
        held=isr_get_stack(&bottle.inv,0);
        water=gm_player_take_item_use_drop();
        CHECK(held.item==374&&held.count==1,
              "full inventory still consumes exactly one bottle");
        CHECK(water.item==373&&water.count==1&&water.meta==1,
              "full inventory exposes one water-potion ground drop");

        fill_flat(win);
        set_test_state(win,24,66,26,8,3);
        set_test_state(win,24,66,27,9,0);
        spawn_at(&bottle,24.5,65.0,24.5);
        isr_set_stack(&bottle.inv,0,ic_mk(325,1,0));
        bottle.inv.current_item=0;nedits=0;
        gm_player_tick((struct Chunk *)win,(struct McSinTable *)&st,
                       (struct PsvPlayer *)&bottle,(struct PvStats *)&bv,
                       fill,0,0,0,edits,&nedits,4);
        held=isr_get_stack(&bottle.inv,0);
        CHECK(held.item==325&&held.count==1,
              "flowing water stops bucket ray before source behind it");

        fill_flat(win);
        spawn_at(&bottle,24.5,65.0,24.5);
        isr_set_stack(&bottle.inv,0,ic_mk(373,1,15));
        bottle.inv.current_item=0;
        GmAction drink;memset(&drink,0,sizeof drink);drink.use=1;
        gm_player_dig_reset();
        for(int t=0;t<32;++t) {
            nedits=0;
            gm_player_tick((struct Chunk *)win,(struct McSinTable *)&st,
                           (struct PsvPlayer *)&bottle,(struct PvStats *)&bv,
                           drink,0,0,0,edits,&nedits,4);
        }
        held=isr_get_stack(&bottle.inv,0);
        ICStack finished=gm_player_take_finished_drink();
        CHECK(held.item==374&&held.count==1&&held.meta==0,
              "finished potion returns one glass bottle");
        CHECK(finished.item==373&&finished.count==1&&finished.meta==15,
              "finished drink preserves potion-type identity for runtime effect");

        spawn_at(&bottle,24.5,65.0,24.5);
        isr_set_stack(&bottle.inv,0,ic_mk(335,1,0));
        bottle.inv.current_item=0;
        gm_player_dig_reset();
        for(int t=0;t<32;++t) {
            nedits=0;
            gm_player_tick((struct Chunk *)win,(struct McSinTable *)&st,
                           (struct PsvPlayer *)&bottle,(struct PvStats *)&bv,
                           drink,0,0,0,edits,&nedits,4);
        }
        held=isr_get_stack(&bottle.inv,0);
        finished=gm_player_take_finished_drink();
        CHECK(held.item==325&&held.count==1&&held.meta==0,
              "finished milk returns one empty bucket");
        CHECK(finished.item==335&&finished.count==1,
              "finished milk exposes cure event to runtime");
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
