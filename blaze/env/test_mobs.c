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
#include "mc_rng.h"

typedef struct {
    int stone_y;
} HsTestW;

static int hs_test_block(const HsTestW *w, int x, int y, int z) {
    (void)x;
    (void)z;
    if (!w) return 0;
    if (y == w->stone_y) return BLK_STONE;
    if (y == w->stone_y - 1) return BLK_BEDROCK;
    return 0;
}

static int g_hs_placed;

static int hs_test_place(HsTestW *w, int type, double x, double y, double z,
                         float yaw, unsigned long long seed48, int have_g,
                         double g) {
    (void)w; (void)type; (void)x; (void)y; (void)z;
    (void)yaw; (void)seed48; (void)have_g; (void)g;
    ++g_hs_placed;
    return 1;
}

#define HS_W HsTestW
#define HS_BLOCK(w, x, y, z) hs_test_block((w), (x), (y), (z))
#define HS_PLACE(w, type, x, y, z, yaw, seed48, have_g, g) \
    hs_test_place((w), (type), (x), (y), (z), (yaw), (seed48), (have_g), (g))
#include "hostile_spawn.h"

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

    {
        int plat_x = s.head.ox + (int)s.head.px + 32;
        int plat_z = s.head.oz + (int)s.head.pz;
        int px, pz, py;
        if (plat_x < s.head.rx0 + 2 || plat_x >= s.head.rx0 + s.head.rnx - 2)
            plat_x = s.head.ox + (int)s.head.px - 32;
        for (px = plat_x - 2; px <= plat_x + 2; ++px)
            for (pz = plat_z - 2; pz <= plat_z + 2; ++pz) {
                plant_cell(&s, px, 64, pz, BLK_STONE, 0);
                for (py = 65; py <= 67; ++py)
                    plant_cell(&s, px, py, pz, 0, 0);
            }
    }
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

    expect(ml_hurt_gate(&hurt, &last, 3.0f, &applied) == 1 && applied == 3.0f
           && hurt == ML_HURT_MAX,
           "first hit sets hurtResistantTime=20 flag1");
    expect(!ml_hurt_gate(&hurt, &last, 3.0f, &applied),
           "same-or-less damage during i-frames is rejected");
    expect(ml_hurt_gate(&hurt, &last, 5.0f, &applied) == 2 && applied == 2.0f,
           "greater hit during i-frames applies the delta flag1=false");

    {
        double mx = 0.2, my = 0.0, mz = 0.0;
        expect(ml_knockback(&mx, &my, &mz, 1, 0.4f, 0.0, 3.0),
               "knockBack on ground applies at KR=0");
        expect(mx == 0.1, "knockBack halves motionX");
        expect(mz == -0.4000000059604645,
               "knockBack -Z is -xRatio/f*(double)0.4F");
        expect(my == 0.4000000059604645,
               "on-ground motionY caps at (double)0.4F");
        mx = 0.2;
        my = 0.1;
        mz = 0.0;
        expect(ml_knockback(&mx, &my, &mz, 0, 0.4f, 0.0, 3.0),
               "air knockBack skips Y");
        expect(my == 0.1, "air knockBack leaves motionY");
    }

    expect(hs_round_up(1, 16) == 16, "MathHelper.roundUp(1,16)=16");
    expect(hs_round_up(0, 16) == 16, "MathHelper.roundUp(0,16)=16");
    expect(hs_round_up(16, 16) == 16, "MathHelper.roundUp(16,16)=16");
    expect(hs_round_up(65, 16) == 80, "MathHelper.roundUp(65,16)=80");
    expect(hs_monster_cap(289) == 70, "MONSTER cap 70 * 289 / 289 = 70");
    expect(hs_ceil_d(0.0) == 0, "ceil(0)=0");
    expect(hs_ceil_d(0.1) == 1, "ceil(0.1)=1");
    expect(hs_ceil_d(3.0) == 3, "ceil(3)=3");
    expect(hs_ceil_d(3.01) == 4, "ceil(3.01)=4");
    expect(hs_valid_empty(0), "air is a valid empty spawn block");
    expect(!hs_valid_empty(BLK_STONE), "stone is not empty");
    expect(hs_is_normal_cube(BLK_STONE), "stone isBlockNormalCube");
    expect(!hs_is_normal_cube(0), "air is not a normal cube");
    expect(hs_clamped_add(2, 18000LL) == 0.0f,
           "NORMAL worldTime 18000 clamped additional difficulty is 0");
    expect(hs_skylight_sub(18000LL) == 11, "midnight skylightSubtracted is 11");
    expect(hs_skylight_sub(6000LL) == 0, "noon skylightSubtracted is 0");
    expect(hs_skylight_sub(13000LL) == 6, "dusk 13000 skylightSubtracted is 6");
    expect(HS_TABLE_CAP == EW_MAX_ENTITIES, "shared table cap is EW_MAX_ENTITIES");
    {
        HsTestW tw;
        tw.stone_y = 64;
        expect(hs_can_spawn_at(&tw, 10, 65, 10),
               "ON_GROUND: stone below, air at pos and pos.up");
        expect(!hs_can_spawn_at(&tw, 10, 64, 10),
               "cannot spawn inside the stone");
        expect(!hs_can_spawn_at(&tw, 10, 63, 10),
               "bedrock below is not a spawn floor");
    }
    {
        JavaRandom r;
        int seen[HS_NTYPES];
        int i, t;
        memset(seen, 0, sizeof seen);
        jrand_set(&r, 12345);
        for (i = 0; i < 2000; ++i) {
            t = hs_weighted_pick(&r);
            if (t >= 0 && t < HS_NTYPES) seen[t] = 1;
        }
        expect(seen[HS_ZOMBIE] && seen[HS_SKELETON] && seen[HS_CREEPER],
               "WeightedRandom hits zombie/skeleton/creeper");
        expect(seen[HS_SPIDER] && seen[HS_SLIME],
               "WeightedRandom also hits spider/slime on the biome list");
    }
    {
        int age = 0;
        u64 seed48;
        JavaRandom r;
        jrand_set(&r, 1);
        seed48 = r.seed;
        expect(!hs_despawn_tick(1, 200.0, &age, &seed48) && age == 0,
               "persist skips despawn and zeros age");
        age = 0;
        expect(hs_despawn_tick(0, 129.0, &age, &seed48),
               "128-block hard despawn");
        age = 601;
        expect(!hs_despawn_tick(0, 10.0, &age, &seed48) && age == 0,
               "inside 32 blocks resets age");
    }
    {
        HsTestW tw;
        HsState st;
        int i;
        tw.stone_y = 64;
        hs_init(&st, 10);
        st.world_time = 18000;
        g_hs_placed = 0;
        for (i = 0; i < 50 && g_hs_placed == 0; ++i)
            (void)hs_find_chunks_for_spawning(&tw, &st, 8.5, 65.0, 8.5);
        expect(g_hs_placed > 0,
               "night findChunksForSpawning places a roster hostile");
    }
#ifndef __CUDA_ARCH__
    (void)mc_probe_fn;
    (void)mc_probe_cx;
    (void)mc_probe_cz;
    (void)mc_redirect_apply;
    (void)mc_redirect_pending;
    (void)mc_redirect_restore;
    (void)mc_jr_watch_n;
    (void)mc_jr_watch_before;
    (void)mc_jr_watch_after;
    (void)mc_jr_watch_fired;
    (void)mc_jr_watch_hookfn;
#endif
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
