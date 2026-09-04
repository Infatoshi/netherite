/* test_portals_dimensions.c - portal contact, dimension transit units, and fixture baker.
 *
 * Units: coordinate scaling, portal ignition frame detection, dimension light properties.
 * --write-fixture FROM OUT: loads an overworld snapshot, plants a lit nether portal
 * directly in front of the player, and writes the portals fixture and action chain.
 */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "blaze_core.h"
#include "mc_blocks.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int fails;

static void expect(int cond, const char *msg) {
    if (cond) {
        fprintf(stderr, "OK: %s\n", msg);
    } else {
        fprintf(stderr, "FAIL: %s\n", msg);
        fails = 1;
    }
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
    if (s->light) {
        unsigned char sky = (id == 0) ? 15 : (id == 90 ? 11 : 0);
        unsigned char blk = (id == 90) ? 11 : 0;
        s->light[idx] = (unsigned char)((sky << 4) | (blk & 15));
    }
}

/* 10 forward into the overworld portal, idle through the 82-tick transfer,
 * 20 forward out of the arrival pane onto netherrack (+Z), 60 idle in the
 * Nether. Swap is observed at t=87 on this fixture; 83 ticks then follow
 * in dimension -1. */
#define PORTALS_WALK_IN 10
#define PORTALS_WAIT 80
#define PORTALS_WALK_OUT 20
#define PORTALS_STAY 60
#define PORTALS_CHAIN (PORTALS_WALK_IN + PORTALS_WAIT + PORTALS_WALK_OUT + PORTALS_STAY)

static int write_chain(const char *path) {
    FILE *f = fopen(path, "w");
    int t;
    if (!f) {
        fprintf(stderr, "open %s failed\n", path);
        return 0;
    }
    fputc('[', f);
    for (t = 0; t < PORTALS_CHAIN; ++t) {
        if (t) fputc(',', f);
        if (t < PORTALS_WALK_IN ||
            (t >= PORTALS_WALK_IN + PORTALS_WAIT &&
             t < PORTALS_WALK_IN + PORTALS_WAIT + PORTALS_WALK_OUT))
            fputs("{\"forward\":1.0}", f);
        else
            fputs("{}", f);
    }
    fputs("]\n", f);
    fclose(f);
    fprintf(stderr, "WROTE %s %d actions\n", path, PORTALS_CHAIN);
    return 1;
}

static int write_fixture(const char *from, const char *out_path) {
    CuSnapshot s;
    char err[256];
    int x, y, z, i;
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(from, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", from, err);
        return 0;
    }

    s.head.py = 65.0;
    s.head.box[1] = 65.0;
    s.head.box[4] = 65.0 + 1.8;
    s.head.on_ground = 1;
    s.head.mx = s.head.my = s.head.mz = 0.0;
    s.head.yaw = 0.0f;    /* facing +Z */
    s.head.pitch = 0.0f;
    s.head.px = 8.5;
    s.head.pz = 10.0;
    s.head.box[0] = 8.5 - 0.3;
    s.head.box[2] = 10.0 - 0.3;
    s.head.box[3] = 8.5 + 0.3;
    s.head.box[5] = 10.0 + 0.3;
    s.head.hotbar_sel = 0;
    s.n_mobs = 0;
    s.n_orbs = 0;
    for (i = 0; i < 37; ++i) {
        s.head.inv[i][0] = s.head.inv[i][1] = s.head.inv[i][2] = 0;
    }

    /* Floor at y=64, air at y=65..70 from z=6..14, x=6..10 */
    for (x = 6; x <= 10; ++x) {
        for (z = 6; z <= 14; ++z) {
            plant_cell(&s, x, 64, z, BLK_STONE, 0);
            for (y = 65; y <= 70; ++y) {
                plant_cell(&s, x, y, z, 0, 0);
            }
        }
    }

    /* Nether portal frame at z=11 (spanning x=7..10, y=64..68) */
    for (x = 7; x <= 10; ++x) {
        plant_cell(&s, x, 64, 11, BLK_OBSIDIAN, 0); /* bottom */
        plant_cell(&s, x, 68, 11, BLK_OBSIDIAN, 0); /* top */
    }
    for (y = 65; y <= 67; ++y) {
        plant_cell(&s, 7, y, 11, BLK_OBSIDIAN, 0);  /* left */
        plant_cell(&s, 10, y, 11, BLK_OBSIDIAN, 0); /* right */
    }

    /* Portal pane interior: block 90, meta 1 (axis X) */
    for (x = 8; x <= 9; ++x) {
        for (y = 65; y <= 67; ++y) {
            plant_cell(&s, x, y, 11, 90, 1);
        }
    }

    /* Backstop wall at z=12 behind portal */
    for (x = 7; x <= 10; ++x) {
        for (y = 64; y <= 68; ++y) {
            plant_cell(&s, x, y, 12, BLK_STONE, 0);
        }
    }

    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr, "WROTE %s player (8.5, 65.0, 10.0), lit portal at z=11\n", out_path);
    blaze_snapshot_free(&s);

    return write_chain("blaze/rl/fixtures/portals_s10.json");
}

/* Exercise the real shared transfer on two private environments backed by
 * the same immutable snapshots. The snapshot pose is deliberately wrong. */
static Blaze *test_env(CuSnapshot *ow, CuSnapshot *banks) {
    Blaze *e = calloc(1, sizeof *e);
    if (!e) abort();
    e->rnx = e->rnz = 32; e->rny = 128;
    e->rx0 = e->rz0 = -16; e->rvol = 32 * 128 * 32;
    e->cells = calloc((size_t)e->rvol, sizeof(u16));
    e->light = calloc((size_t)e->rvol, 1);
    e->biome = calloc(32 * 32, 1);
    e->window = calloc(PSV_NCHUNKS, sizeof(Chunk));
    e->dimensions[0].cells = calloc((size_t)e->rvol, sizeof(u16));
    e->dimensions[0].light = calloc((size_t)e->rvol, 1);
    e->dimensions[0].biome = calloc(32 * 32, 1);
    if (!e->cells || !e->light || !e->biome || !e->window ||
        !e->dimensions[0].cells || !e->dimensions[0].light || !e->dimensions[0].biome) abort();
    memcpy(e->cells, ow->cells, (size_t)e->rvol * sizeof(u16));
    e->pl.ent.posX = e->pl.ent.posZ = 4.5; e->pl.ent.posY = 65;
    e->pl.ent.box = psv_player_box(4.5, 65, 4.5);
    e->pl.yaw = 13; e->pl.pitch = -7;
    e->dim_ow = ow; e->dim_bank = banks;
    e->active_chest = e->active_furnace = -1;
    cu_dimension_store(e);
    return e;
}
static void free_test_env(Blaze *e) {
    for (int i = 0; i < 3; ++i) {
        free(e->dimensions[i].cells); free(e->dimensions[i].light);
        free(e->dimensions[i].biome);
    }
    free(e->fluid_cur); free(e->fluid_tmp);
    free(e->window); free(e);
}
static void transfer(Blaze *e, int dim) {
    e->swap_target_dim = dim; e->swap_pending = 1;
    cu_dimension_swap_apply(e);
}
static int run_units(void) {
    CuSnapshot *snaps = calloc(4, sizeof *snaps);
    Blaze *a, *b;
    long marker;
    expect(snaps != NULL, "snapshot storage allocated");
    if (!snaps) return 1;
    for (int i = 0; i < 2; ++i) {
        snaps[i].head.rnx = snaps[i].head.rnz = 32; snaps[i].head.rny = 128;
        snaps[i].head.rx0 = snaps[i].head.rz0 = -16;
        snaps[i].head.px = snaps[i].head.py = snaps[i].head.pz = 999;
        snaps[i].cells = calloc(32 * 128 * 32, sizeof(u16));
        if (!snaps[i].cells) abort();
    }
    plant_cell(&snaps[0], 4, 65, 4, 90, 1);
    plant_cell(&snaps[1], 0, 65, 0, 90, 1);
    a = test_env(&snaps[0], &snaps[1]);
    b = test_env(&snaps[0], &snaps[1]);
    marker = ((long)(5 + 16) * 128 + 70) * 32 + 5 + 16;
    a->cells[marker] = mc_state(57, 0);
    a->projectiles[0].active = 1;
    a->world_rand.seed = 123;
    transfer(a, -1);
    expect(!a->dimension_error && a->dimension == -1, "outbound transfer succeeds");
    expect(a->pl.ent.posX == 0.5 && a->pl.ent.posY == 65 && a->pl.ent.posZ == 0.5,
           "arrival computed from live portal, not snapshot pose");
    expect(a->pl.yaw == 13 && a->pl.pitch == -7, "arrival preserves view");
    expect(a->pl.ent.box.minX == psv_player_box(0.5,65,0.5).minX,
           "arrival recomputes collision box");
    expect(a->projectiles[0].active == 1 && a->world_rand.seed == 123,
           "runtime projectiles and world random state survive transit");
    a->cells[marker] = mc_state(41, 0);
    transfer(a, 0);
    expect(!a->dimension_error && a->dimension == 0, "return transfer succeeds");
    expect(a->pl.ent.posX == 4.5 && a->pl.ent.posZ == 4.5, "return resolves overworld portal");
    expect(a->cells[marker] == mc_state(57,0), "overworld edits survive round trip");
    transfer(a, -1);
    expect(a->cells[marker] == mc_state(41,0), "nether edits survive second visit");
    transfer(b, -1);
    expect(!b->dimension_error && b->cells[marker] == 0, "batched worlds do not share mutations");
    expect(snaps[0].cells[marker] == 0 && snaps[1].cells[marker] == 0,
           "source snapshots remain immutable");
    /* Java/Magma use floor before coordinate scaling, including negatives. */
    {
        Blaze *c;
        plant_cell(&snaps[1], -1, 65, -1, 90, 1);
        c = test_env(&snaps[0], &snaps[1]);
        c->pl.ent.posX = c->pl.ent.posZ = -4.5;
        c->fluid_dim = 0; c->fluid_reg[0].active = 1;
        c->fluid_reg[0].quiet_steps = 1; c->parity_fluid_mutations = 7;
        c->mob_watch_time[0] = 23; c->mob_task_tick[0] = 17;
        transfer(c, -1);
        expect(!c->dimension_error && c->pl.ent.posX + c->ox == -0.5 &&
               c->pl.ent.posZ + c->oz == -0.5, "negative coordinate scaling floors correctly");
        expect(c->fluid_dim == 0 && c->fluid_reg[0].active &&
               c->fluid_reg[0].quiet_steps == 1 && c->parity_fluid_mutations == 7,
               "dimension-tagged runtime fluid scheduler survives transit");
        expect(c->mob_watch_time[0] == 0 && c->mob_task_tick[0] == 0,
               "new dimension clears mob AI side state");
        c->fluid_cur = calloc(CU_FLUID_VOL, sizeof(u16));
        c->fluid_tmp = calloc(CU_FLUID_VOL, sizeof(u16));
        if (!c->fluid_cur || !c->fluid_tmp) abort();
        c->cells[((long)(2+16)*128+65)*32+2+16] = mc_state(10,0);
        c->fluid_reg[0].x0 = c->fluid_reg[0].x1 = 2;
        c->fluid_reg[0].y0 = c->fluid_reg[0].y1 = 65;
        c->fluid_reg[0].z0 = c->fluid_reg[0].z1 = 2;
        c->fluid_reg[0].quiet_steps = 0;
        expect(cu_fluid_tick(c, -1, 10) == 0,
               "other-dimension fluid work remains paused");
        expect(cu_fluid_tick(c, 0, 10) == 0,
               "overworld lava waits for its 30-tick cadence");
        c->fluid_dim = -1;
        expect(cu_fluid_tick(c, -1, 10) > 0,
               "nether lava evolves on its 10-tick cadence");
        free_test_env(c);
    }
    a->pl.ent.posX = 10000;
    transfer(a, 0);
    expect(a->dimension_error == CU_DIM_ERR_BOUNDS && a->dimension == -1,
           "unavailable portal search data fails without switching worlds");
    b->dimension_error = 0; b->dimension = 0; b->dim_bank = NULL;
    transfer(b, -1);
    expect(b->dimension_error == CU_DIM_ERR_MISSING_BANK && b->dimension == 0,
           "missing bank fails without changing dimension");
    /* Full +/-128 coverage is required before creating a missing portal. */
    {
        CuDimensionRegion d = {0};
        PortalArrival p;
        d.rx0 = d.rz0 = -129; d.rnx = d.rnz = 259; d.rny = 128;
        d.cells = calloc((size_t)259 * 128 * 259, sizeof(u16));
        if (!d.cells) abort();
        expect(portal_plan_arrival(&d, 0, 0, &p) == 1 && p.create &&
               p.by == 70 && p.x == 0.5 && p.z == 0.5,
               "complete empty destination plans fallback portal at y70");
        d.cells[((long)129 * 128 + 64) * 259 + 129] = mc_state(1,0);
        expect(portal_plan_arrival(&d, 0, 0, &p) == 1 && p.create && p.by == 65,
               "portal creation prefers a supported air site");
        d.rnx = 128;
        expect(portal_plan_arrival(&d, 0, 0, &p) == -1,
               "incomplete destination cannot silently create a portal");
        free(d.cells);
    }
    free_test_env(a); free_test_env(b);
    free(snaps[0].cells); free(snaps[1].cells); free(snaps);
    return fails ? 1 : 0;
}

int main(int argc, char **argv) {
    int i;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--write-fixture") && i + 2 < argc) {
            return write_fixture(argv[i + 1], argv[i + 2]) ? 0 : 1;
        }
    }
    return run_units();
}
