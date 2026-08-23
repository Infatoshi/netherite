/* Creeper fuse + explosion apply unit tests + fixture baker.
 *
 * Units: EntityCreeper fuse 30 ignited, density scale, center damage.
 * --write-fixture FROM OUT copies a magma region, grounds the s10 player
 * on stone, plants an ignited creeper 4 blocks +Z. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "entity_spine.h"
#include "explosion_live.h"
#include "mc_blocks.h"

#include <math.h>
#include <stdint.h>
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

static int bits_eq_f(float a, float b) {
    uint32_t ua, ub;
    memcpy(&ua, &a, 4);
    memcpy(&ub, &b, 4);
    return ua == ub;
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
        unsigned char sky = (id == 0) ? 15 : 0;
        s->light[idx] = (unsigned char)(sky << 4);
    }
}

static void plant_creeper(RlSnapMob *o, int slot, int id,
                          double x, double y, double z) {
    float hf = 0.6f / 2.0f;
    float h = 1.7f;
    memset(o, 0, sizeof *o);
    o->slot = slot;
    o->id = id;
    o->type = EW_TYPE_CREEPER;
    o->alive = 1;
    o->persist = 1;
    o->x = x;
    o->y = y;
    o->z = z;
    o->health = 20.0f;
    o->on_ground = 1;
    o->target_idx = 1; /* ignited: EntityCreeper.hasIgnited */
    o->box_on = 1;
    o->box_minx = x - (double)hf;
    o->box_miny = y;
    o->box_minz = z - (double)hf;
    o->box_maxx = x + (double)hf;
    o->box_maxy = y + (double)h;
    o->box_maxz = z + (double)hf;
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
    s.head.yaw = 0.0f;
    s.head.pitch = 0.0f;
    s.head.px = 8.5;
    s.head.pz = 8.5;
    s.head.box[0] = 8.5 - 0.3;
    s.head.box[2] = 8.5 - 0.3;
    s.head.box[3] = 8.5 + 0.3;
    s.head.box[5] = 8.5 + 0.3;
    for (i = 0; i < 37; ++i)
        s.head.inv[i][0] = s.head.inv[i][1] = s.head.inv[i][2] = 0;

    for (x = 6; x <= 10; ++x)
        for (z = 6; z <= 14; ++z) {
            plant_cell(&s, x, 64, z, BLK_STONE, 0);
            for (y = 65; y <= 68; ++y)
                plant_cell(&s, x, y, z, 0, 0);
        }
    /* Dirt off the player-+Z LOS so getBlockDensity is not 0, still a crater.
     * Explosion centre (8.5,65.5,12.5) is cell (8,65,12); keep that air. */
    plant_cell(&s, 9, 65, 12, BLK_DIRT, 0);
    plant_cell(&s, 9, 65, 13, BLK_DIRT, 0);
    plant_cell(&s, 10, 65, 12, BLK_DIRT, 0);

    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 1;
    plant_creeper(&s.mobs[0], 1, 1, 8.5, 65.0, 12.5);
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s ignited creeper (8.5,65,12.5) player (8.5,65,8.5) "
            "n_mobs=%u\n",
            out_path, s.n_mobs);
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    int fuse, t;
    float dens, dmg;
    u16 grid[EX_VOL];
    ExBlast blast;

    fuse = 0;
    expect(!exl_fuse_tick(&fuse, 0), "idle creeper does not swell");
    expect(fuse == 0, "idle fuse stays 0");

    fuse = 0;
    for (t = 0; t < EXL_FUSE_TIME - 1; ++t)
        expect(!exl_fuse_tick(&fuse, 1) && fuse == t + 1, "ignited fuse counts");
    expect(exl_fuse_tick(&fuse, 1), "fuse 30 explodes");
    expect(fuse == 0, "explode resets fuse");

    dens = ex_density_scale();
    expect(bits_eq_f(dens, 0.7f + 0.5f * 0.6f),
           "density scale is 0.7F + 0.5F * 0.6F");

    dmg = ex_entity_damage(8.0, 8.0, 8.0, 8.0, 8.0, 8.0, EXL_RADIUS, 1.0f);
    expect(dmg > 0.0f, "center size-3 damage is positive");
    expect(bits_eq_f(EXL_RADIUS, 3.0f), "creeper radius is 3.0F");

    ex_fill(grid, mc_state(BLK_AIR, 0));
    dens = ex_block_density(grid, 0, 0, 0, 8.0, 8.0, 8.0,
                            7.7, 8.0, 7.7, 8.3, 9.8, 8.3);
    expect(bits_eq_f(dens, 1.0f), "air AABB getBlockDensity is 1.0F");

    ex_fill(grid, mc_state(BLK_STONE, 0));
    dens = ex_block_density(grid, 0, 0, 0, 8.0, 8.0, 8.0,
                            0.2, 0.2, 0.2, 0.8, 1.8, 0.8);
    expect(bits_eq_f(dens, 0.0f), "stone-occluded getBlockDensity is 0.0F");

    ex_entity_blast(8.5, 65.0, 8.5, 1.62f, 8.5, 65.5, 12.5, EXL_RADIUS,
                    1.0f, 0, &blast);
    expect(blast.hit, "size-3 blast 4 blocks -Z is inside f3");
    expect(blast.damage > 0.0f, "open-exposure d2i damage is positive");
    expect(blast.addz < 0.0, "motion add is away from +Z explosion");
    expect(blast.mapz == blast.addz,
           "blast-prot 0: playerKnockbackMap equals motion add");
    expect(ex_blast_reduction(1.0, 0) == 1.0,
           "EnchantmentProtection level 0 is identity");
    expect(bits_eq_f(exl_eye_height(EW_TYPE_ZOMBIE, 1.95f), 1.74f),
           "zombie getEyeHeight is 1.74F");
    expect(bits_eq_f(exl_eye_height(EW_TYPE_CREEPER, 1.7f), 1.7f * 0.85f),
           "creeper eye is height * 0.85F");
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
            fprintf(stderr, "usage: %s [--write-fixture FROM.bsnp OUT.bsnp]\n",
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
