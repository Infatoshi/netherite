/* Environmental damage units + fixture baker.
 *
 * Units: drown air 300/-20/2.0, IN_WALL, LAVA 4 + setFire(15), ON_FIRE
 * 1 per 20, cactus 1, magma HOT_FLOOR unless sneak/frost, void 4,
 * extinguish in water. --write-fixture FROM OUT plants a +Z hazard
 * strip and writes blaze/rl/fixtures/hazards_s10.json. */
#define _POSIX_C_SOURCE 200809L
#include "player_survival.h"
#include "blaze_snapshot.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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

static void fill_floor(Chunk *win) {
    int ci, lx, lz, y;
    memset(win, 0, sizeof(Chunk) * PSV_NCHUNKS);
    for (ci = 0; ci < PSV_NCHUNKS; ++ci) {
        win[ci].cx = (ci % PSV_DIM) - PSV_R;
        win[ci].cz = (ci / PSV_DIM) - PSV_R;
        for (lx = 0; lx < 16; ++lx)
            for (lz = 0; lz < 16; ++lz)
                for (y = 0; y <= 2; ++y)
                    mc_set(&win[ci], lx, y, lz, mc_state(BLK_STONE, 0));
    }
}

static void spawn_at(PsvPlayer *pl, double x, double y, double z) {
    psv_player_init(pl);
    pl->ent.posX = x;
    pl->ent.posY = y;
    pl->ent.posZ = z;
    pl->ent.box = psv_player_box(x, y, z);
    pl->health = PSV_MAX_HEALTH;
}

static void idle_tick(Chunk *win, McSinTable *st, PsvPlayer *pl) {
    McAABB blocks[PSV_MAX_BLOCKS];
    PsvAction a;
    memset(&a, 0, sizeof a);
    psv_physics_tick(win, st, pl, &a, blocks);
}

static int cell_id(const CuSnapshot *s, int wx, int wy, int wz) {
    int lx = wx - s->head.rx0;
    int ly = wy - s->head.ry0;
    int lz = wz - s->head.rz0;
    long idx;
    if (lx < 0 || ly < 0 || lz < 0 ||
        lx >= s->head.rnx || ly >= s->head.rny || lz >= s->head.rnz)
        return 0;
    idx = ((long)lx * s->head.rny + ly) * s->head.rnz + lz;
    return (int)(s->cells[idx] >> 4);
}

static void plant_cell(CuSnapshot *s, int wx, int wy, int wz, int id, int meta) {
    int lx = wx - s->head.rx0;
    int ly = wy - s->head.ry0;
    int lz = wz - s->head.rz0;
    long idx;
    if (lx < 0 || ly < 0 || lz < 0 ||
        lx >= s->head.rnx || ly >= s->head.rny || lz >= s->head.rnz)
        return;
    idx = ((long)lx * s->head.rny + ly) * s->head.rnz + lz;
    s->cells[idx] = (unsigned short)(((id & 4095) << 4) | (meta & 15));
}

static int write_chain(const char *path) {
    FILE *f;
    int i, n = 0;
    f = fopen(path, "w");
    if (!f) {
        fprintf(stderr, "cannot write %s\n", path);
        return 0;
    }
    fputc('[', f);
    /* Walk +Z into the water tank. */
    for (i = 0; i < 40; ++i) {
        if (n++) fputc(',', f);
        fputs("{\"forward\":1}", f);
    }
    /* Idle until drowning starts (300 air + 20 to -20). */
    for (i = 0; i < 330; ++i) {
        if (n++) fputc(',', f);
        fputs("{}", f);
    }
    /* Jump-out then walk through lava, cactus, magma. */
    for (i = 0; i < 8; ++i) {
        if (n++) fputc(',', f);
        fputs("{\"forward\":1,\"jump\":1}", f);
    }
    for (i = 0; i < 50; ++i) {
        if (n++) fputc(',', f);
        fputs("{\"forward\":1}", f);
    }
    for (i = 0; i < 20; ++i) {
        if (n++) fputc(',', f);
        fputs("{}", f);
    }
    fputs("]\n", f);
    fclose(f);
    fprintf(stderr, "WROTE %s actions=%d\n", path, n);
    return n;
}

static int write_fixture(const char *from, const char *out_path) {
    CuSnapshot s;
    char err[256];
    int x, y, z, px, pz, py, nact;
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(from, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", from, err);
        return 0;
    }
    px = (int)floor(s.head.px + (double)s.head.ox);
    pz = (int)floor(s.head.pz + (double)s.head.oz);
    py = (int)floor(s.head.py);
    if (py < 2) py = 64;
    /* Strip +Z: stone floor, air, then water tank / lava / cactus / magma. */
    for (x = px - 2; x <= px + 2; ++x)
        for (z = pz; z <= pz + 16; ++z) {
            plant_cell(&s, x, py - 1, z, BLK_STONE, 0);
            for (y = py; y <= py + 4; ++y)
                plant_cell(&s, x, y, z, 0, 0);
        }
    /* 2-deep still water at +3..+5 Z, glass walls so the CA does not run. */
    for (x = px - 1; x <= px + 1; ++x)
        for (z = pz + 3; z <= pz + 5; ++z) {
            plant_cell(&s, x, py - 1, z, BLK_STONE, 0);
            plant_cell(&s, x, py, z, BLK_WATER, 0);
            plant_cell(&s, x, py + 1, z, BLK_WATER, 0);
            plant_cell(&s, x, py + 2, z, 20, 0); /* glass lid */
        }
    for (z = pz + 3; z <= pz + 5; ++z) {
        plant_cell(&s, px - 2, py, z, 20, 0);
        plant_cell(&s, px + 2, py, z, 20, 0);
        plant_cell(&s, px - 2, py + 1, z, 20, 0);
        plant_cell(&s, px + 2, py + 1, z, 20, 0);
    }
    /* Lava pool +7..+8 Z. */
    for (x = px - 1; x <= px + 1; ++x)
        for (z = pz + 7; z <= pz + 8; ++z) {
            plant_cell(&s, x, py - 1, z, BLK_STONE, 0);
            plant_cell(&s, x, py, z, BLK_LAVA, 0);
        }
    /* Cactus +10 Z on sand. */
    plant_cell(&s, px, py - 1, pz + 10, BLK_SAND, 0);
    plant_cell(&s, px, py, pz + 10, BLK_CACTUS, 0);
    /* Magma walkway +12..+13 Z. */
    for (x = px - 1; x <= px + 1; ++x)
        for (z = pz + 12; z <= pz + 13; ++z)
            plant_cell(&s, x, py - 1, z, BLK_MAGMA, 0);

    s.head.version = BLAZE_SNAP_VERSION;
    s.head.yaw = 0.0f;
    s.head.pitch = 0.0f;
    s.player_fire = 0;
    s.player_air = PSV_AIR_MAX;
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s hazards strip px=%d py=%d pz=%d water=%d lava=%d cactus=%d magma=%d\n",
            out_path, px, py, pz,
            cell_id(&s, px, py, pz + 4),
            cell_id(&s, px, py, pz + 7),
            cell_id(&s, px, py, pz + 10),
            cell_id(&s, px, py - 1, pz + 12));
    blaze_snapshot_free(&s);
    nact = write_chain("blaze/rl/fixtures/hazards_s10.json");
    return nact > 0;
}

static int run_units(void) {
    McSinTable st;
    Chunk win[PSV_NCHUNKS];
    PsvPlayer pl;
    PsvAction idle;
    McAABB blocks[PSV_MAX_BLOCKS];
    int t;

    mc_sin_table_init(&st);
    memset(&idle, 0, sizeof idle);

    expect(psv_causes_suffocation(BLK_STONE) == 1, "stone causes suffocation");
    expect(psv_causes_suffocation(0) == 0, "air does not suffocate");
    expect(psv_causes_suffocation(BLK_CACTUS) == 0, "cactus does not suffocate");
    expect(psv_causes_suffocation(18) == 0, "leaves do not suffocate");

    /* Drown: 2-deep water, eyes in the upper cell. EntityLivingBase.java:297-320. */
    fill_floor(win);
    spawn_at(&pl, 8.5, 3.0, 8.5);
    psv_set_block(win, 8, 3, 8, BLK_WATER);
    psv_set_block(win, 8, 4, 8, BLK_WATER);
    psv_set_block(win, 8, 2, 8, BLK_STONE);
    expect(pl.air == 300, "air starts at 300");
    for (t = 0; t < 319; ++t) idle_tick(win, &st, &pl);
    expect(pl.air != -20 && pl.hz_drown == 0.0f,
           "no drown before air hits -20");
    idle_tick(win, &st, &pl);
    expect(pl.hz_drown == 2.0f, "DROWN 2.0 at air == -20");
    expect(pl.air == 0, "air resets to 0 after drown");

    /* Suffocation: player eye samples inside stone. Entity.java:2156-2186. */
    fill_floor(win);
    spawn_at(&pl, 8.5, 3.0, 8.5);
    psv_set_block(win, 8, 4, 8, BLK_STONE);
    idle_tick(win, &st, &pl);
    expect(pl.hz_wall == 1.0f, "IN_WALL 1.0 inside opaque");

    /* Fire ticks: ON_FIRE 1.0 when fire%20==0 then decrement. Entity.java:541-560. */
    fill_floor(win);
    spawn_at(&pl, 8.5, 3.0, 8.5);
    pl.fire = 20;
    idle_tick(win, &st, &pl);
    expect(pl.hz_fire == 1.0f, "ON_FIRE 1.0 at fire%20==0");
    expect(pl.fire == 19, "fire decrements after pulse");

    /* Lava: 4.0 + setFire(15). Entity.java:605-611. */
    fill_floor(win);
    spawn_at(&pl, 8.5, 3.0, 8.5);
    psv_set_block(win, 8, 3, 8, BLK_LAVA);
    idle_tick(win, &st, &pl);
    expect(pl.hz_lava == 4.0f, "LAVA 4.0");
    expect(pl.fire >= 300, "setFire(15) stores 300 ticks");

    /* Extinguish in water. EntityLivingBase.java:341-344. */
    fill_floor(win);
    spawn_at(&pl, 8.5, 3.0, 8.5);
    pl.fire = 100;
    psv_set_block(win, 8, 3, 8, BLK_WATER);
    idle_tick(win, &st, &pl);
    expect(pl.fire == 0, "isWet extinguishes fire");

    /* Rain branch of isWet: caller sets wet_rain. */
    fill_floor(win);
    spawn_at(&pl, 8.5, 3.0, 8.5);
    pl.fire = 80;
    pl.wet_rain = 1;
    idle_tick(win, &st, &pl);
    expect(pl.fire == 0, "rain isWet extinguishes fire");

    /* Cactus collision. BlockCactus.java:133-136. */
    fill_floor(win);
    spawn_at(&pl, 8.5, 3.0, 8.5);
    psv_set_block(win, 8, 3, 8, BLK_CACTUS);
    pl.ent.box = psv_player_box(8.5, 3.0, 8.5);
    psv_do_block_collisions(win, &pl);
    expect(pl.hz_cactus == 1.0f, "CACTUS 1.0 on overlap");

    /* Magma onEntityWalk; sneak skips. BlockMagma.java:45-50 / Entity.java:1010. */
    fill_floor(win);
    spawn_at(&pl, 8.5, 3.05, 8.5);
    psv_set_block(win, 8, 2, 8, BLK_MAGMA);
    expect(psv_get_block(win, 8, 2, 8) == BLK_MAGMA, "magma planted at 8,2,8");
    pl.ent.motionY = -0.2;
    memset(&idle, 0, sizeof idle);
    psv_physics_tick(win, &st, &pl, &idle, blocks);
    expect(pl.ent.onGround == 1, "landed on magma");
    expect(pl.hz_magma == 1.0f, "HOT_FLOOR 1.0 standing on magma");
    spawn_at(&pl, 8.5, 3.05, 8.5);
    psv_set_block(win, 8, 2, 8, BLK_MAGMA);
    pl.ent.motionY = -0.2;
    idle.sneak = 1;
    psv_physics_tick(win, &st, &pl, &idle, blocks);
    expect(pl.hz_magma == 0.0f, "sneaking skips magma onEntityWalk");
    spawn_at(&pl, 8.5, 3.05, 8.5);
    psv_set_block(win, 8, 2, 8, BLK_MAGMA);
    pl.ent.motionY = -0.2;
    pl.frost_walker = 1;
    memset(&idle, 0, sizeof idle);
    psv_physics_tick(win, &st, &pl, &idle, blocks);
    expect(pl.hz_magma == 0.0f, "frost walker skips HOT_FLOOR");

    /* Void. Entity.java:569-572 / EntityLivingBase.java:1647-1649. */
    fill_floor(win);
    spawn_at(&pl, 8.5, -65.0, 8.5);
    idle_tick(win, &st, &pl);
    expect(pl.hz_void == 4.0f, "OUT_OF_WORLD 4.0 below y=-64");

    /* setFire does not shorten a longer timer. Entity.java:626-629. */
    spawn_at(&pl, 8.5, 3.0, 8.5);
    pl.fire = 400;
    psv_set_fire(&pl, 15);
    expect(pl.fire == 400, "setFire cannot lower existing fire");

    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    int i;
    const char *from = NULL, *out = NULL;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--write-fixture") && i + 2 < argc) {
            from = argv[++i];
            out = argv[++i];
        } else {
            fprintf(stderr,
                    "usage: %s [--write-fixture FROM.bsnp OUT.bsnp]\n",
                    argv[0]);
            return 2;
        }
    }
    if (run_units())
        return 1;
    if (from && out && !write_fixture(from, out))
        return 1;
    return 0;
}
