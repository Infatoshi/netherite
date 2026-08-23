/* ItemBed / trySleep / sleepTimer / time skip / spawn / safe exit. */
#include "player_bed.h"

#include <stdio.h>
#include <string.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (cond)
        fprintf(stderr, "OK: %s\n", msg);
    else {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails = 1;
    }
}

static void fill_flat(Chunk *win) {
    int ci, lx, lz, y;
    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
    for (ci = 0; ci < PSV_NCHUNKS; ++ci) {
        win[ci].cx = (ci % PSV_DIM) - PSV_R;
        win[ci].cz = (ci / PSV_DIM) - PSV_R;
        for (lx = 0; lx < 16; ++lx)
            for (lz = 0; lz < 16; ++lz)
                for (y = 0; y <= 64; ++y)
                    mc_set(&win[ci], lx, y, lz, mc_state(BLK_STONE, 0));
    }
}

static void set_cell(Chunk *win, int x, int y, int z, int id, int meta) {
    int lx, lz, ci;
    ci = psv_chunk_index(x, z, &lx, &lz);
    if (ci < 0 || y < 0 || y > 255) return;
    mc_set(&win[ci], lx, y, lz, mc_state(id, meta & 15));
}

int main(void) {
    Chunk win[PSV_NCHUNKS];
    BedSleep p;
    double px, py, pz;
    int ox, oy, oz, st, i, facing;
    WwState ww, ww2;

    fill_flat(win);

    /* ---- ItemBed placement (ItemBed.java:34-71) ---- */
    expect(!bed_item_can_place(win, 8, 64, 8, 0),
           "cannot place bed replacing stone (not replaceable)");
    expect(bed_item_can_place(win, 8, 65, 8, 0),
           "place on stone: air cells + fullyOpaque below");
    set_cell(win, 8, 65, 8, 31, 0); /* tallgrass replaceable */
    expect(bed_item_can_place(win, 8, 65, 8, 0),
           "replaceable grass cell is a legal foot");
    set_cell(win, 8, 65, 8, 0, 0);
    set_cell(win, 8, 64, 8, 20, 0); /* glass: not fullyOpaque */
    expect(!bed_item_can_place(win, 8, 65, 8, 0),
           "glass below foot is not isFullyOpaque");
    set_cell(win, 8, 64, 8, BLK_STONE, 0);
    set_cell(win, 8, 65, 9, 1, 0); /* SOUTH head cell occupied by stone */
    expect(!bed_item_can_place(win, 8, 65, 8, 0),
           "head cell must be replaceable");
    set_cell(win, 8, 65, 9, 0, 0);
    expect(bed_foot_meta(3) == 3 && bed_head_meta(3) == 11,
           "foot meta=facing, head meta=facing|8");
    expect(bed_yaw_quad(0.0f) == 0 && bed_yaw_quad(90.0f) == 1 &&
               bed_yaw_quad(180.0f) == 2 && bed_yaw_quad(-90.0f) == 3,
           "yaw quad S=0 W=1 N=2 E=3");

    /* ---- trySleep failure order (EntityPlayer.java:1645-1672) ---- */
    memset(&p, 0, sizeof p);
    p.px = 8.5;
    p.py = 65.0;
    p.pz = 8.5;
    p.sleeping = 1;
    st = bed_try_sleep(&p, 8, 65, 8, 0, 1, 1, 0, 0);
    expect(st == BED_OTHER_PROBLEM, "already sleeping -> OTHER_PROBLEM");
    p.sleeping = 0;
    st = bed_try_sleep(&p, 8, 65, 8, 0, 0, 1, 0, 0);
    expect(st == BED_OTHER_PROBLEM, "dead -> OTHER_PROBLEM");
    st = bed_try_sleep(&p, 8, 65, 8, 0, 1, 0, 0, 0);
    expect(st == BED_NOT_POSSIBLE_HERE, "nether/end -> NOT_POSSIBLE_HERE");
    st = bed_try_sleep(&p, 8, 65, 8, 0, 1, 1, 1, 0);
    expect(st == BED_NOT_POSSIBLE_NOW, "daytime -> NOT_POSSIBLE_NOW");
    p.px = 20.5;
    p.pz = 20.5;
    st = bed_try_sleep(&p, 8, 65, 8, 0, 1, 1, 0, 0);
    expect(st == BED_TOO_FAR_AWAY, "|dx|>3 -> TOO_FAR_AWAY");
    p.px = 8.5;
    p.pz = 8.5;
    st = bed_try_sleep(&p, 8, 65, 8, 0, 1, 1, 0, 1);
    expect(st == BED_NOT_SAFE, "EntityMob in 8/5/8 AABB -> NOT_SAFE");
    st = bed_try_sleep(&p, 8, 65, 8, 0, 1, 1, 0, 0);
    expect(st == BED_OK, "night, in range, no mob -> OK");
    expect(p.sleeping && p.sleep_timer == 0, "sleeping, sleepTimer=0");
    expect(p.mx == 0.0 && p.my == 0.0 && p.mz == 0.0, "trySleep zeroes motion");

    /* ---- pose per facing (EntityPlayer.java:1685-1688) ---- */
    for (facing = 0; facing < 4; ++facing) {
        bed_sleep_pose(10, 70, 12, facing, &px, &py, &pz);
        expect(py == (double)((float)70 + 0.6875f), "sleep Y is (float)y+0.6875F");
        expect(px == (double)((float)10 + 0.5f + (float)bed_facing_dx(facing) * 0.4f),
               "sleep X is 0.5F + dx*0.4F");
        expect(pz == (double)((float)12 + 0.5f + (float)bed_facing_dz(facing) * 0.4f),
               "sleep Z is 0.5F + dz*0.4F");
    }
    expect(bed_in_range(8.5, 65.0, 8.5, 8, 65, 8, 0), "standing on head is in range");
    expect(bed_in_range(8.5, 65.0, 7.5, 8, 65, 8, 0),
           "foot (opposite SOUTH) is in range");
    expect(!bed_in_range(8.5, 68.1, 8.5, 8, 65, 8, 0), "|dy|>2 is too far");

    /* monster AABB 8/5/8 */
    expect(bed_mob_hits_sleep_box(8.5, 65.0, 8.5, 0.6f, 1.95f, 8, 65, 8),
           "zombie on the bed hits the sleep box");
    expect(!bed_mob_hits_sleep_box(8.5, 65.0, 20.0, 0.6f, 1.95f, 8, 65, 8),
           "zombie 12 blocks south is outside z+8");
    expect(!bed_mob_hits_sleep_box(8.5, 72.0, 8.5, 0.6f, 1.95f, 8, 65, 8),
           "zombie 7 blocks above is outside y+5");

    /* ---- sleepTimer 100 / fully asleep / world skip ---- */
    memset(&p, 0, sizeof p);
    p.px = 8.5;
    p.py = 65.0;
    p.pz = 8.5;
    expect(bed_try_sleep(&p, 8, 65, 8, 0, 1, 1, 0, 0) == BED_OK, "sleep OK for timer test");
    expect(!bed_fully_asleep(&p), "timer 0 is not fully asleep");
    for (i = 0; i < 99; ++i)
        bed_player_on_update(&p, 1, 0);
    expect(p.sleep_timer == 99 && !bed_fully_asleep(&p),
           "99 onUpdate ticks: timer 99, not fully asleep");
    bed_player_on_update(&p, 1, 0);
    expect(p.sleep_timer == 100 && bed_fully_asleep(&p),
           "100th onUpdate: isPlayerFullyAsleep");
    bed_player_on_update(&p, 1, 0);
    expect(p.sleep_timer == 100, "sleepTimer caps at 100");

    expect(bed_time_skip(18000) == 24000, "night 18000 skips to 24000");
    expect(bed_time_skip(13000) == 24000, "dusk 13000 skips to 24000");
    expect(bed_time_skip(0) == 24000, "time 0 skip is 24000");
    ww_init(&ww, 1);
    ww.worldTime = 18000;
    ww.raining = 1;
    ww.rainTime = 50;
    ww.thundering = 1;
    ww.thunderTime = 40;
    ww_tick_gated_sleep(&ww, 0, 0, 1);
    expect(ww.worldTime == 24001,
           "WorldServer: skip to 24000 then worldTime++ -> 24001");
    expect(ww.raining == 0 && ww.thundering == 0 && ww.rainTime == 0 &&
               ww.thunderTime == 0,
           "wakeAllPlayers resetRainAndThunder");
    ww_init(&ww, 99);
    ww_init(&ww2, 99);
    for (i = 0; i < 64; ++i) {
        ww_tick_gated(&ww, 0, 0);
        ww_tick_gated_sleep(&ww2, 0, 0, 0);
    }
    expect(ww.worldTime == ww2.worldTime && ww.rainTime == ww2.rainTime &&
               ww.raining == ww2.raining,
           "sleep_skip=0 matches ww_tick_gated (weather_optional unchanged)");

    /* world skip then wakeAllPlayers(false,false,true) */
    bed_wake(&p, 0, 1);
    expect(!p.sleeping && p.sleep_timer == 100, "wake immediately=false -> timer 100");
    expect(p.spawn_set && p.spawn_x == 8 && p.spawn_y == 65 && p.spawn_z == 8,
           "wakeAllPlayers setSpawnPoint(bedLocation, false)");
    expect(p.spawn_forced == 0, "sleep spawn is not forced");

    /* ---- wake when bed removed (onUpdate isInBed false) ---- */
    memset(&p, 0, sizeof p);
    p.px = 8.5;
    p.py = 65.0;
    p.pz = 8.5;
    expect(bed_try_sleep(&p, 8, 65, 8, 0, 1, 1, 0, 0) == BED_OK, "sleep for break test");
    p.spawn_set = 0;
    bed_player_on_update(&p, 0, 0);
    expect(!p.sleeping && p.sleep_timer == 0, "bed gone: wake immediately, timer 0");
    expect(!p.spawn_set, "bed gone: setSpawn false");

    /* daytime wake while sleeping: wakeUpPlayer(false, true, true) */
    memset(&p, 0, sizeof p);
    p.px = 8.5;
    p.py = 65.0;
    p.pz = 8.5;
    expect(bed_try_sleep(&p, 8, 65, 8, 0, 1, 1, 0, 0) == BED_OK, "sleep for day wake");
    bed_player_on_update(&p, 1, 1);
    expect(!p.sleeping && p.sleep_timer == 100 && p.spawn_set,
           "isDaytime wake sets spawn, timer 100");

    expect(bed_is_daytime_time(6000), "noon worldTime 6000 is daytime");
    expect(!bed_is_daytime_time(18000), "midnight 18000 is not daytime");

    /* ---- safe exit / obstructed bed ---- */
    fill_flat(win);
    set_cell(win, 8, 65, 8, BED_BLK, bed_head_meta(0));
    set_cell(win, 8, 65, 7, BED_BLK, bed_foot_meta(0));
    expect(bed_safe_exit(win, 8, 65, 8, 0, 0, &ox, &oy, &oz),
           "open air around bed has a safe exit");
    expect(oy == 65, "safe exit Y is the bed Y");
    expect(bed_spawn_location(win, 8, 65, 8, 0, &ox, &oy, &oz),
           "getBedSpawnLocation on a bed with room");
    /* obstruct: fill the 3x3 at y=65 with stone so hasRoomForPlayer fails
     * (here is solid). Keep the bed cells as bed (still solid-ish). */
    {
        int x, z;
        for (x = 6; x <= 10; ++x)
            for (z = 5; z <= 10; ++z)
                if (!(x == 8 && (z == 8 || z == 7)))
                    set_cell(win, x, 65, z, BLK_STONE, 0);
        /* also fill y=66 so up is solid, and y=64 is already stone */
        for (x = 6; x <= 10; ++x)
            for (z = 5; z <= 10; ++z)
                set_cell(win, x, 66, z, BLK_STONE, 0);
    }
    expect(!bed_safe_exit(win, 8, 65, 8, 0, 0, &ox, &oy, &oz),
           "obstructed bed: getSafeExitLocation returns null");
    expect(!bed_spawn_location(win, 8, 65, 8, 0, &ox, &oy, &oz),
           "obstructed bed + spawnForced false -> null (world spawn fallback)");
    expect(!bed_spawn_location(win, 4, 65, 4, 0, &ox, &oy, &oz),
           "missing bed + spawnForced false -> null");
    set_cell(win, 4, 65, 4, 0, 0);
    set_cell(win, 4, 66, 4, 0, 0);
    expect(bed_spawn_location(win, 4, 65, 4, 1, &ox, &oy, &oz) && ox == 4 &&
               oy == 65 && oz == 4,
           "missing bed + spawnForced + canSpawnInBlock here/up");

    bed_exit_pose(9, 65, 10, &px, &py, &pz);
    expect(px == (double)((float)9 + 0.5f) && py == (double)((float)65 + 0.1f) &&
               pz == (double)((float)10 + 0.5f),
           "respawn/wake pose is (float)+0.5 / +0.1 / +0.5");

    expect(bed_head_meta(1) == 9 && bed_is_head_meta(9) &&
               bed_facing_meta(9) == 1,
           "HEAD WEST meta 9");
    {
        int hx, hy, hz;
        bed_head_pos(8, 65, 7, bed_foot_meta(0), &hx, &hy, &hz);
        expect(hx == 8 && hy == 65 && hz == 8,
               "FOOT SOUTH looks up HEAD at z+1");
    }

    return fails ? 1 : 0;
}
