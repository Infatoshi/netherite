/* test_play_compose.c - live composition gates for dig/place/interact/inventory/worldTime.
 *
 * Drives the SHIPPED gm_player_tick / gm_world_* paths (not reimplementations) and
 * asserts concrete block/inventory/time outcomes. */
#include "game/game.h"
#include "game/player_ctl.h"
#include "game/sel_box.h"
#include "game/live_sim.h"
#include "player_survival.h"
#include "player_vitals.h"
#include "player_break.h"
#include "item_block_place.h"
#include "interact_blocks.h"
#include "container_click.h"
#include "items_core.h"
#include "items_tools_armor.h"
#include "mc_blocks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int g_fail;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); g_fail = 1; } \
    else fprintf(stderr, "ok: %s\n", msg); \
} while (0)

static void fill_flat(Chunk *win, int y_floor, int floor_id) {
    int i, x, y, z;
    for (i = 0; i < PSV_NCHUNKS; ++i) {
        memset(&win[i], 0, sizeof(Chunk));
        for (x = 0; x < 16; ++x)
            for (z = 0; z < 16; ++z)
                for (y = 0; y <= y_floor; ++y)
                    mc_set(&win[i], x, y, z, mc_state(floor_id, 0));
    }
}

/* Progressive dig: iron pick on stone must harvest in finite ticks with attack held. */
static void test_progressive_dig(void) {
    McSinTable st; mc_sin_table_init(&st);
    Chunk win[PSV_NCHUNKS];
    fill_flat(win, 64, BLK_STONE);
    /* stone in front of eyes at z+2 (yaw 0 = +Z) */
    mc_set(&win[4], 8, 66, 10, mc_state(BLK_STONE, 0)); /* center chunk index 4 for 3x3 */

    PsvPlayer pl; psv_player_init(&pl);
    pl.ent.posX = 8.5; pl.ent.posY = 65.0; pl.ent.posZ = 8.5;
    pl.ent.box = psv_player_box(pl.ent.posX, pl.ent.posY, pl.ent.posZ);
    pl.ent.onGround = 1;
    pl.yaw = 0.0f;   /* look +Z */
    pl.pitch = 0.0f; /* eyes ~66.62; target y=66 is in the ray */
    /* iron pick in hotbar */
    isr_set_stack(&pl.inv, 0, ic_mk(PB_IRON_PICKAXE, 1, 0));
    pl.inv.current_item = 0;

    PvStats vit; pv_init(&vit);
    gm_player_dig_reset();

    GmAction act;
    memset(&act, 0, sizeof act);
    act.attack = 1; /* hold dig */

    int harvested = 0;
    for (int t = 0; t < 200; ++t) {
        GmBlockEdit edits[8]; int ne = 0;
        gm_player_tick((struct Chunk *)win, (const struct McSinTable *)&st,
                       (struct PsvPlayer *)&pl, (struct PvStats *)&vit, act,
                       0, 0, 0, edits, &ne, 8);
        for (int e = 0; e < ne; ++e) {
            if (edits[e].id == 0 && edits[e].wy == 66 && edits[e].wz == 10)
                harvested = 1;
        }
        if (harvested) break;
    }
    CHECK(harvested, "progressive dig clears stone under iron pick");
    CHECK(psv_get_block(win, 8, 66, 10) == BLK_AIR,
          "target cell air after dig");
}

/* Place furnace with two facing orientations -> distinct meta. */
static void test_place_meta(void) {
    McSinTable st; mc_sin_table_init(&st);
    Chunk win[PSV_NCHUNKS];
    fill_flat(win, 64, BLK_STONE);

    PsvPlayer pl; psv_player_init(&pl);
    pl.ent.posX = 8.5; pl.ent.posY = 65.0; pl.ent.posZ = 8.5;
    pl.ent.box = psv_player_box(pl.ent.posX, pl.ent.posY, pl.ent.posZ);
    pl.ent.onGround = 1;
    /* place furnace: look at floor slightly */
    pl.pitch = 30.0f;
    isr_set_stack(&pl.inv, 0, ic_mk(61 /* furnace */, 2, 0));
    pl.inv.current_item = 0;
    PvStats vit; pv_init(&vit);

    int meta_a = -1, meta_b = -1;
    {
        pl.yaw = 0.0f; /* face south-ish */
        GmAction act; memset(&act, 0, sizeof act);
        act.do_place = 1;
        GmBlockEdit edits[8]; int ne = 0;
        gm_player_tick((struct Chunk *)win, (const struct McSinTable *)&st,
                       (struct PsvPlayer *)&pl, (struct PvStats *)&vit, act,
                       0, 0, 0, edits, &ne, 8);
        for (int e = 0; e < ne; ++e)
            if (edits[e].id == 61) meta_a = edits[e].meta;
    }
    {
        /* reset air, second place with different yaw */
        fill_flat(win, 64, BLK_STONE);
        isr_set_stack(&pl.inv, 0, ic_mk(61, 2, 0));
        pl.yaw = 90.0f;
        GmAction act; memset(&act, 0, sizeof act);
        act.do_place = 1;
        GmBlockEdit edits[8]; int ne = 0;
        gm_player_tick((struct Chunk *)win, (const struct McSinTable *)&st,
                       (struct PsvPlayer *)&pl, (struct PvStats *)&vit, act,
                       0, 0, 0, edits, &ne, 8);
        for (int e = 0; e < ne; ++e)
            if (edits[e].id == 61) meta_b = edits[e].meta;
    }
    CHECK(meta_a >= 0 && meta_b >= 0, "place emitted furnace twice");
    CHECK(meta_a != meta_b, "place orientation meta differs for two yaws");
    /* pure table cross-check */
    int t0 = ibp_placed_meta(61, IBP_UP, 0, 0, 0) & 15;
    int t1 = ibp_placed_meta(61, IBP_UP, 1, 0, 0) & 15;
    CHECK(t0 != t1, "ibp furnace meta differs for yaw quads 0 vs 1");
}

/* A descending ray enters the placed torch's voxel through its top, then hits
 * the recessed wall-torch AABB on its west face.  Vanilla ItemBlock uses that
 * AABB sideHit (west), not the voxel-entry face (up), for the follow-up click. */
static void test_torch_click_uses_aabb_face(void) {
    McSinTable st; mc_sin_table_init(&st);
    Chunk win[PSV_NCHUNKS];
    fill_flat(win, 68, BLK_STONE);
    mc_set(&win[4], 8, 69, 8, mc_state(BLK_STONE, 0)); /* pit edge under player */
    mc_set(&win[4], 10, 69, 8, mc_state(BLK_STONE, 0)); /* lower pit wall */
    mc_set(&win[4], 10, 70, 8, mc_state(BLK_STONE, 0)); /* tempting upper support */

    PsvPlayer pl; psv_player_init(&pl);
    pl.ent.posX = 8.50601965444; pl.ent.posY = 70.0; pl.ent.posZ = 8.30000001192;
    pl.ent.box = psv_player_box(pl.ent.posX, pl.ent.posY, pl.ent.posZ);
    pl.ent.onGround = 1; pl.yaw = -84.272285f; pl.pitch = 57.997467f;
    isr_set_stack(&pl.inv, 0, ic_mk(IBP_BLK_TORCH, 2, 0));
    pl.inv.current_item = 0;
    PvStats vit; pv_init(&vit);

    GmAction act; memset(&act, 0, sizeof act); act.do_place = 1;
    GmBlockEdit edits[8]; int ne = 0;
    gm_player_tick((struct Chunk *)win, (const struct McSinTable *)&st,
                   (struct PsvPlayer *)&pl, (struct PvStats *)&vit, act,
                   0, 0, 0, edits, &ne, 8);
    CHECK(ne == 1 && edits[0].id == IBP_BLK_TORCH &&
          edits[0].wx == 9 && edits[0].wy == 69 && edits[0].wz == 8,
          "first pit-wall torch placement succeeds in the expected cell");

    int hx, hy, hz, ax, ay, az;
    int hit = gm_raycast_sel_reach(win, &st, &pl, PSV_REACH,
                                   &hx, &hy, &hz, &ax, &ay, &az);
    CHECK(hit == 1 && hx == 9 && hy == 69 && hz == 8,
          "follow-up selection ray stops on the placed wall torch");
    CHECK(ax == 8 && ay == 69 && az == 8,
          "wall-torch hit reports its recessed west AABB face, not voxel-entry top");

    ne = 0;
    gm_player_tick((struct Chunk *)win, (const struct McSinTable *)&st,
                   (struct PsvPlayer *)&pl, (struct PvStats *)&vit, act,
                   0, 0, 0, edits, &ne, 8);
    ICStack left = isr_get_stack(&pl.inv, 0);
    CHECK(ne == 0 && left.item == IBP_BLK_TORCH && left.count == 1,
          "follow-up click places no upper-wall torch and consumes nothing");
}

static void test_tripwire_selection_box(void) {
    GmSelIn in;
    float box[6];
    memset(&in,0,sizeof in);
    in.id=132;
    in.meta=4;
    gm_sel_box(&in,box);
    CHECK(box[0]==0.0f && box[1]==0.0625f && box[2]==0.0f &&
          box[3]==1.0f && box[4]==0.15625f && box[5]==1.0f,
          "attached tripwire uses exact thin selection box");
    in.meta=0;
    gm_sel_box(&in,box);
    CHECK(box[0]==0.0f && box[1]==0.0f && box[2]==0.0f &&
          box[3]==1.0f && box[4]==0.5f && box[5]==1.0f,
          "detached tripwire uses exact half-block selection box");
}

static void test_redstone_diode_selection_box(void) {
    static const int ids[4]={93,94,149,150};
    GmSelIn in;
    float box[6];
    memset(&in,0,sizeof in);
    for(int i=0;i<4;++i){
        in.id=ids[i];
        in.meta=i*5;
        gm_sel_box(&in,box);
        CHECK(box[0]==0.0f && box[1]==0.0f && box[2]==0.0f &&
              box[3]==1.0f && box[4]==0.125f && box[5]==1.0f,
              "repeater/comparator selection is full-footprint and 1/8 high");
    }
}

static void test_brewing_stand_selection_box(void) {
    GmSelIn in;
    float box[6];
    memset(&in,0,sizeof in);
    in.id=117;
    for(int meta=0;meta<8;++meta){
        in.meta=meta;
        gm_sel_box(&in,box);
        CHECK(box[0]==0.0f && box[1]==0.0f && box[2]==0.0f &&
              box[3]==1.0f && box[4]==0.125f && box[5]==1.0f,
              "brewing-stand selection uses its base, not its collision stem");
    }
}

static void test_piston_base_selection_boxes(void) {
    static const int ids[2]={29,33};
    static const float extended[6][6]={
        {0.f,0.25f,0.f,1.f,1.f,1.f},
        {0.f,0.f,0.f,1.f,0.75f,1.f},
        {0.f,0.f,0.25f,1.f,1.f,1.f},
        {0.f,0.f,0.f,1.f,1.f,0.75f},
        {0.25f,0.f,0.f,1.f,1.f,1.f},
        {0.f,0.f,0.f,0.75f,1.f,1.f},
    };
    GmSelIn in;
    float box[6];
    memset(&in,0,sizeof in);
    for(int block=0;block<2;++block)
        for(int facing=0;facing<6;++facing){
            in.id=ids[block];
            in.meta=facing;
            gm_sel_box(&in,box);
            CHECK(box[0]==0.f && box[1]==0.f && box[2]==0.f &&
                  box[3]==1.f && box[4]==1.f && box[5]==1.f,
                  "retracted normal/sticky piston base selects a full cube");
            in.meta=facing|8;
            gm_sel_box(&in,box);
            CHECK(memcmp(box,extended[facing],sizeof box)==0,
                  "extended normal/sticky piston base selects its facing 3/4 body");
        }
}

/* Interact: wooden door at eye height; live gm_player_tick must emit open meta.
 * Door at y=66 (eye ~66.62); look slightly down so ray hits the door block. */
static void test_interact_door(void) {
    McSinTable st; mc_sin_table_init(&st);
    Chunk win[PSV_NCHUNKS];
    fill_flat(win, 64, BLK_STONE);
    /* facing north (meta 3): the closed door slab sits on the far z edge of
     * the cell, so the selection-box raycast down x=8.5 actually strikes it
     * (with meta 0 the slab hugs the west edge and the ray legitimately
     * passes through the empty 13/16 of the cell, as in vanilla). */
    mc_set(&win[4], 8, 66, 10, mc_state(IB_WOODEN_DOOR, 3)); /* closed, OPEN bit 0 */

    PsvPlayer pl; psv_player_init(&pl);
    pl.ent.posX = 8.5; pl.ent.posY = 65.0; pl.ent.posZ = 8.5;
    pl.ent.box = psv_player_box(pl.ent.posX, pl.ent.posY, pl.ent.posZ);
    pl.ent.onGround = 1;
    pl.yaw = 0.0f;   /* +Z toward door at z=10 */
    pl.pitch = 5.0f; /* slight down into y=66 cell */
    PvStats vit; pv_init(&vit);

    GmAction act; memset(&act, 0, sizeof act);
    act.do_place = 1; /* use edge */
    GmBlockEdit edits[8]; int ne = 0;
    gm_player_tick((struct Chunk *)win, (const struct McSinTable *)&st,
                   (struct PsvPlayer *)&pl, (struct PvStats *)&vit, act,
                   0, 0, 0, edits, &ne, 8);
    int opened = 0;
    for (int e = 0; e < ne; ++e)
        if (edits[e].id == IB_WOODEN_DOOR && (edits[e].meta & 4)
            && edits[e].wx == 8 && edits[e].wy == 66 && edits[e].wz == 10)
            opened = 1;
    CHECK(ne >= 1, "live interact emitted at least one edit");
    CHECK(opened == 1, "live gm_player_tick opened wooden door (meta OPEN bit)");
    {
        u16 stt = mc_get(&win[4], 8, 66, 10);
        CHECK((stt & 15) & 4, "window door meta OPEN after live tick");
    }
}

static void test_closed_shulker_selection_boxes(void) {
    GmSelIn in;
    float box[6];
    memset(&in,0,sizeof in);
    for(int id=219;id<=234;++id)
        for(int facing=0;facing<6;++facing){
            in.id=id;
            in.meta=facing;
            gm_sel_box(&in,box);
            CHECK(box[0]==0.f && box[1]==0.f && box[2]==0.f &&
                  box[3]==1.f && box[4]==1.f && box[5]==1.f,
                  "every closed shulker color/facing selects a full cube");
        }
}

/* Inventory slotClick: PICKUP + QUICK_MOVE + THROW via gm_player_inv_click (live API). */
static void test_inventory_click(void) {
    PsvPlayer pl; psv_player_init(&pl);
    isr_set_stack(&pl.inv, 0, ic_mk(1, 10, 0)); /* 10 stone */
    isr_set_stack(&pl.inv, 1, ic_empty());
    isr_set_stack(&pl.inv, 2, ic_empty());
    gm_player_cursor_set(ic_empty());

    /* PICKUP: left-click slot 0 -> all into cursor */
    gm_player_inv_click((struct PsvPlayer *)&pl, 0, 0, CC_CLICK_PICKUP);
    ICStack cur = gm_player_cursor();
    ICStack s0 = isr_get_stack(&pl.inv, 0);
    CHECK(isr_is_empty(&s0), "PICKUP empties slot 0");
    CHECK(cur.item == 1 && cur.count == 10, "PICKUP cursor holds 10 stone");

    /* PICKUP place into slot 1 */
    gm_player_inv_click((struct PsvPlayer *)&pl, 1, 0, CC_CLICK_PICKUP);
    ICStack s1 = isr_get_stack(&pl.inv, 1);
    cur = gm_player_cursor();
    CHECK(s1.item == 1 && s1.count == 10, "place into slot 1");
    CHECK(isr_is_empty(&cur), "cursor empty after place");

    /* QUICK_MOVE: shift-click slot 1 -> transfer into empty slot 0 (mergeItemStack path) */
    gm_player_inv_click((struct PsvPlayer *)&pl, 1, 0, CC_CLICK_QUICK_MOVE);
    s0 = isr_get_stack(&pl.inv, 0);
    s1 = isr_get_stack(&pl.inv, 1);
    CHECK(s0.item == 1 && s0.count == 10, "QUICK_MOVE fills slot 0 from slot 1");
    CHECK(isr_is_empty(&s1), "QUICK_MOVE empties source slot 1");

    /* THROW: drop one from slot 0 (cursor empty) */
    int before = s0.count;
    gm_player_inv_click((struct PsvPlayer *)&pl, 0, 0, CC_CLICK_THROW);
    s0 = isr_get_stack(&pl.inv, 0);
    CHECK(s0.item == 1 && s0.count == before - 1, "THROW drops one from slot 0");
    CHECK(s0.count == 9, "THROW leaves 9 stone in slot 0");
}

/* World clock advances. */
static void test_world_clock(void) {
    GmWorldClock c;
    gm_world_clock_init(&c, 12345);
    i64 t0 = c.world_time;
    int rain0 = c.rain_time;
    for (int i = 0; i < 50; ++i) gm_world_tick(&c);
    CHECK(c.world_time == t0 + 50, "worldTime advances 50 ticks");
    CHECK(c.total_time == 50, "totalTime advances 50 ticks");
    CHECK(c.rain_time != rain0 || c.raining != 1, "weather timers move");

    gm_world_clock_init(&c, 12345);
    t0 = c.world_time;
    for (int i = 0; i < 50; ++i) gm_world_tick_clear(&c);
    CHECK(c.world_time == t0 + 50 && c.total_time == 50,
          "weather-off mode still advances world time");
    CHECK(c.rain_time == 0 && c.thunder_time == 0 && !c.raining && !c.thundering,
          "weather-off mode remains permanently clear");
}

/* Live world dig/place through gm_world_set_block_meta. */
static void test_live_world_edits(void) {
    GmWorld *w = gm_world_create(0);
    CHECK(w != NULL, "gm_world_create");
    if (!w) return;
    gm_world_ensure(w, 0, 0, 1);
    int sy = gm_world_surface_y(w, 8, 8);
    gm_world_set_block_meta(w, 8, sy, 8, 61, 3); /* furnace meta 3 */
    CHECK(gm_world_block(w, 8, sy, 8) == 61, "live set furnace id");
    CHECK(gm_world_meta(w, 8, sy, 8) == 3, "live set furnace meta");
    gm_world_set_block_meta(w, 8, sy, 8, 0, 0);
    CHECK(gm_world_block(w, 8, sy, 8) == 0, "live clear block");
    gm_world_destroy(w);
}

/* GmAction.inv_click is consumed by gm_runtime_tick -> gm_container_click
 * (full-inventory Container.slotClick with grid/result/furnace ids); the shipped
 * click seam is covered by game/test_container_live.c. gm_player_tick must now
 * IGNORE inv_click so the action cannot be double-applied. */
static void test_live_inv_action(void) {
    McSinTable st; mc_sin_table_init(&st);
    Chunk win[PSV_NCHUNKS];
    fill_flat(win, 64, BLK_STONE);
    PsvPlayer pl; psv_player_init(&pl);
    pl.ent.posX = 8.5; pl.ent.posY = 65.0; pl.ent.posZ = 8.5;
    pl.ent.box = psv_player_box(pl.ent.posX, pl.ent.posY, pl.ent.posZ);
    isr_set_stack(&pl.inv, 0, ic_mk(1, 5, 0));
    pl.inv.current_item = 0;
    gm_player_cursor_set(ic_empty());
    PvStats vit; pv_init(&vit);
    GmBlockEdit edits[4]; int ne = 0;
    GmAction act; memset(&act, 0, sizeof act);
    act.inv_click = 1; act.inv_slot = 0; act.inv_button = 0;
    act.inv_type = CC_CLICK_PICKUP;
    gm_player_tick((struct Chunk *)win, (const struct McSinTable *)&st,
                   (struct PsvPlayer *)&pl, (struct PvStats *)&vit, act,
                   0, 0, 0, edits, &ne, 4);
    ICStack s0 = isr_get_stack(&pl.inv, 0);
    ICStack cur = gm_player_cursor();
    CHECK(s0.item == 1 && s0.count == 5 && isr_is_empty(&cur),
          "gm_player_tick leaves inv_click to the runtime container seam");
}

/* live_sim: entity motion + plant age side effects */
static void test_live_sim_side_effects(void) {
    GmWorld *w = gm_world_create(0);
    CHECK(w != NULL, "live_sim world create");
    if (!w) return;
    gm_world_ensure(w, 0, 0, 1);
    int sy = gm_world_surface_y(w, 10, 10);
    GmLiveSim live;
    gm_live_init(&live, 0, sy);
    double y0 = live.ents[0].y;
    int age0 = gm_live_plant_age(&live);
    for (int t = 0; t < 80; ++t) gm_live_tick(&live, w);
    CHECK(live.ents[0].age == 80, "live entity age advances");
    CHECK(live.ents[0].y != y0 || live.ents[0].on_ground,
          "live entity moved or settled");
    CHECK(gm_live_plant_age(&live) >= age0, "plant age non-decreasing");
    /* force plant growth observation over more ticks if still 0 */
    for (int t = 0; t < 500 && gm_live_plant_age(&live) == 0; ++t)
        gm_live_tick(&live, w);
    CHECK(gm_live_plant_age(&live) > 0, "plant age advanced under live_sim ticks");
    gm_world_destroy(w);
}

static void test_live_item_pickup(void) {
    GmWorld *w = gm_world_create_type(0, 1);
    CHECK(w != NULL, "pickup world create");
    if (!w) return;
    gm_world_ensure(w, 0, 0, 1);
    GmLiveSim live;
    memset(&live, 0, sizeof live);
    PsvPlayer pl;
    psv_player_init(&pl);
    isr_init(&pl.inv);
    pl.ent.posX = 8.5; pl.ent.posY = 4.0; pl.ent.posZ = 8.5;
    pl.ent.box = psv_player_box(pl.ent.posX, pl.ent.posY, pl.ent.posZ);
    CHECK(gm_live_spawn_item(&live, 8.5, 4.0, 8.5, 17, 1, 2, 2),
          "spawn natural log item");
    gm_live_tick_player(&live, w, (struct PsvPlayer *)&pl, 0, 0);
    CHECK(isr_hotbar_total(&pl.inv) + isr_main_total(&pl.inv) == 0,
          "pickup delay prevents immediate collection");
    gm_live_tick_player(&live, w, (struct PsvPlayer *)&pl, 0, 0);
    ICStack got = isr_get_stack(&pl.inv, 0);
    CHECK(got.item == 17 && got.count == 1 && got.meta == 2,
          "eligible item entity enters inventory with metadata");
    CHECK(live.n_active == 0, "collected item entity is removed");

    /* Entity.nextEntityID is post-incremented from its zero initializer. */
    memset(&live, 0, sizeof live);
    CHECK(gm_live_spawn_item_exact(
              &live, 0, 8.5, 4.0, 8.5,
              0.0, 0.0, 0.0, 0.0f, 17, 1, 0, 0, 10, 1),
          "exact item spawn accepts Java entity ID zero");
    CHECK(live.ents[0].active && live.ents[0].eid == 0,
          "exact item retains Java entity ID zero");
    gm_world_destroy(w);
}

int main(void) {
    g_fail = 0;
    test_progressive_dig();
    test_place_meta();
    test_torch_click_uses_aabb_face();
    test_tripwire_selection_box();
    test_redstone_diode_selection_box();
    test_brewing_stand_selection_box();
    test_piston_base_selection_boxes();
    test_closed_shulker_selection_boxes();
    test_interact_door();
    test_inventory_click();
    test_live_inv_action();
    test_world_clock();
    test_live_world_edits();
    test_live_sim_side_effects();
    test_live_item_pickup();
    if (g_fail) {
        fprintf(stderr, "test_play_compose: FAILED\n");
        return 1;
    }
    fprintf(stderr, "test_play_compose: ALL PASS\n");
    return 0;
}
