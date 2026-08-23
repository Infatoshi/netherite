/* Hostile live-tick unit tests + fixture baker.
 *
 * Units: hurt i-frames, zombie melee 3, drop ids.
 * --write-fixture FROM OUT copies a magma region, grounds the s10 player
 * on a roofed stone floor, plants a zombie +Z and a skeleton +X. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "entity_spine.h"
#include "hostile_live.h"
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
        s->light[idx] = 0;
    }
}

static void plant_hostile(RlSnapMob *o, int slot, int id, int type,
                          double x, double y, double z) {
    float w, h;
    ehs_size((u8)type, &w, &h);
    memset(o, 0, sizeof *o);
    o->slot = slot;
    o->id = id;
    o->type = type;
    o->alive = 1;
    o->persist = 1;
    o->x = x;
    o->y = y;
    o->z = z;
    o->health = 20.0f;
    o->on_ground = 1;
    o->box_on = 1;
    o->box_minx = x - (double)(w * 0.5f);
    o->box_miny = y;
    o->box_minz = z - (double)(w * 0.5f);
    o->box_maxx = x + (double)(w * 0.5f);
    o->box_maxy = y + (double)h;
    o->box_maxz = z + (double)(w * 0.5f);
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

    for (x = 5; x <= 14; ++x)
        for (z = 5; z <= 14; ++z) {
            plant_cell(&s, x, 64, z, BLK_STONE, 0);
            for (y = 65; y <= 67; ++y)
                plant_cell(&s, x, y, z, 0, 0);
            plant_cell(&s, x, 68, z, BLK_STONE, 0);
        }
    for (y = 65; y <= 68; ++y) {
        for (x = 5; x <= 14; ++x) {
            plant_cell(&s, x, y, 5, BLK_STONE, 0);
            plant_cell(&s, x, y, 14, BLK_STONE, 0);
        }
        for (z = 5; z <= 14; ++z) {
            plant_cell(&s, 5, y, z, BLK_STONE, 0);
            plant_cell(&s, 14, y, z, BLK_STONE, 0);
        }
    }

    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 2;
    plant_hostile(&s.mobs[0], 1, 1, EW_TYPE_ZOMBIE, 8.5, 65.0, 11.5);
    plant_hostile(&s.mobs[1], 2, 2, EW_TYPE_SKELETON, 12.5, 65.0, 8.5);
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s zombie (8.5,65,11.5) skeleton (12.5,65,8.5) "
            "player (8.5,65,8.5) n_mobs=%u digest=0x%016llx\n",
            out_path, s.n_mobs,
            (unsigned long long)blaze_snap_mobs_digest(s.mobs, s.n_mobs));
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    int hurt = 0;
    float last = 0.0f, applied = 0.0f;

    expect(bits_eq_f(ml_melee_damage(EW_TYPE_ZOMBIE), 3.0f),
           "zombie ATTACK_DAMAGE is 3.0F");
    expect(ml_drop_item(EW_TYPE_ZOMBIE) == 367, "zombie drops rotten flesh 367");
    expect(ml_drop_item(EW_TYPE_SKELETON) == 352, "skeleton drops bone 352");
    expect(ml_follow_range(EW_TYPE_ZOMBIE) == 40.0, "magma zombie follow 40");
    expect(ml_attack_cooldown(EW_TYPE_ZOMBIE) == 20, "zombie melee interval 20");

    expect(ml_hurt_gate(&hurt, &last, 3.0f, &applied) && applied == 3.0f
           && hurt == ML_HURT_MAX,
           "first hit sets hurtResistantTime=20");
    expect(!ml_hurt_gate(&hurt, &last, 3.0f, &applied),
           "same-or-less damage during i-frames is rejected");
    expect(ml_hurt_gate(&hurt, &last, 5.0f, &applied) && applied == 2.0f,
           "greater hit during i-frames applies the delta");
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
