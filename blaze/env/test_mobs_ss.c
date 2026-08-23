/* Spider + slime live-tick unit tests + fixture baker.
 *
 * Units: Java sizes/health/speed, hop delay, split offsets, slime-chunk,
 * swamp moon, spider climb pack, drops.
 * --write-fixture FROM OUT copies a magma region, grounds the s10 player
 * on a roofed stone floor, plants zombie +Z, skeleton +X, spider west,
 * slime size-2 NE. */
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
static int g_hs_last_type;
static int g_hs_last_extra;

static int hs_test_place(HsTestW *w, int type, double x, double y, double z,
                         float yaw, unsigned long long seed48, int have_g,
                         double g, int extra) {
    (void)w; (void)x; (void)y; (void)z;
    (void)yaw; (void)seed48; (void)have_g; (void)g;
    g_hs_last_type = type;
    g_hs_last_extra = extra;
    ++g_hs_placed;
    return 1;
}

#define HS_W HsTestW
#define HS_BLOCK(w, x, y, z) hs_test_block((w), (x), (y), (z))
#define HS_PLACE(w, type, x, y, z, yaw, seed48, have_g, g, extra) \
    hs_test_place((w), (type), (x), (y), (z), (yaw), (seed48), (have_g), (g), (extra))
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
    if (s->light)
        s->light[idx] = 0;
}

static void plant_hostile(RlSnapMob *o, int slot, int id, int type,
                          double x, double y, double z, int slime_size) {
    float w, h;
    ehs_size_scaled((u8)type, slime_size, &w, &h);
    memset(o, 0, sizeof *o);
    o->slot = slot;
    o->id = id;
    o->type = type;
    o->alive = 1;
    o->persist = 1;
    o->x = x;
    o->y = y;
    o->z = z;
    o->health = ehs_max_health_of((u8)type, slime_size);
    o->on_ground = 1;
    o->box_on = 1;
    o->box_minx = x - (double)(w * 0.5f);
    o->box_miny = y;
    o->box_minz = z - (double)(w * 0.5f);
    o->box_maxx = x + (double)(w * 0.5f);
    o->box_maxy = y + (double)h;
    o->box_maxz = z + (double)(w * 0.5f);
    o->seed48 = 1;
    if (type == EW_TYPE_SLIME || type == EW_TYPE_MAGMA)
        o->swell = slime_size > 0 ? slime_size : 2;
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
    s.n_mobs = 4;
    plant_hostile(&s.mobs[0], 1, 1, EW_TYPE_ZOMBIE, 8.5, 65.0, 11.5, 0);
    plant_hostile(&s.mobs[1], 2, 2, EW_TYPE_SKELETON, 12.5, 65.0, 8.5, 0);
    plant_hostile(&s.mobs[2], 3, 3, EW_TYPE_SPIDER, 6.5, 65.0, 8.5, 0);
    plant_hostile(&s.mobs[3], 4, 4, EW_TYPE_SLIME, 11.5, 65.0, 11.5, 2);
    s.n_orbs = 1;
    {
        RlSnapOrb *o = &s.orbs[0];
        memset(o, 0, sizeof *o);
        o->x = 6.5;
        o->y = 66.5;
        o->z = 6.5;
        o->my = 0.2;
        o->xpValue = 3;
        o->delayBeforeCanPickup = 10;
        o->eid = 1000;
    }

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
            "spider (6.5,65,8.5) slime2 (11.5,65,11.5) "
            "player (8.5,65,8.5) n_mobs=%u digest=0x%016llx\n",
            out_path, s.n_mobs,
            (unsigned long long)blaze_snap_mobs_digest(s.mobs, s.n_mobs));
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    float w, h;
    ehs_size(EW_TYPE_SPIDER, &w, &h);
    expect(bits_eq_f(w, 1.4f) && bits_eq_f(h, 0.9f), "spider 1.4x0.9");
    expect(bits_eq_f(ehs_max_health(EW_TYPE_SPIDER), 16.0f), "spider health 16");
    expect(bits_eq_f(ml_melee_damage(EW_TYPE_SPIDER), 2.0f), "spider attack 2");
    ehs_size_scaled(EW_TYPE_SLIME, 2, &w, &h);
    expect(bits_eq_f(w, 0.51000005f * 2.0f) && bits_eq_f(h, 0.51000005f * 2.0f),
           "setSlimeSize 2 is 0.51000005*2");
    expect(bits_eq_f(ehs_max_health_of(EW_TYPE_SLIME, 2), 4.0f),
           "slime size 2 health is 4");
    expect(bits_eq_f(ehs_max_health_of(EW_TYPE_SLIME, 4), 16.0f),
           "slime size 4 health is 16");
    expect(bits_eq_f(ehs_land_speed_of(EW_TYPE_SLIME, 2), 0.4f),
           "slime size 2 speed is 0.2+0.1*2");
    expect(ml_is_roster(EW_TYPE_SLIME) && hs_is_roster(HS_SLIME),
           "slime is on live and spawn roster");
    expect(ml_xp_points(EW_TYPE_SLIME, 2) == 2, "slime XP is size");
    expect(ml_xp_points(EW_TYPE_SLIME, 4) == 4, "slime XP size 4 is 4");
    {
        float ox, oz;
        ml_slime_split_off(0, 2, &ox, &oz);
        expect(bits_eq_f(ox, -0.25f) && bits_eq_f(oz, -0.25f),
               "split k=0 offset (-0.25,-0.25) for size 2");
        ml_slime_split_off(1, 2, &ox, &oz);
        expect(bits_eq_f(ox, 0.25f) && bits_eq_f(oz, -0.25f),
               "split k=1 offset (0.25,-0.25)");
        ml_slime_split_off(2, 2, &ox, &oz);
        expect(bits_eq_f(ox, -0.25f) && bits_eq_f(oz, 0.25f),
               "split k=2 offset (-0.25,0.25)");
    }
    {
        JavaRandom er;
        int n;
        jrand_set(&er, 7);
        n = ml_slime_split_n(&er);
        expect(n >= 2 && n <= 4, "split count is 2+nextInt(3)");
    }
    {
        JavaRandom er;
        MlDrop d[1];
        int n;
        jrand_set(&er, 3);
        n = ml_slime_drop(&er, 2, d, 1);
        expect(n == 0, "size-2 slime drops nothing");
        jrand_set(&er, 3);
        n = ml_slime_drop(&er, 1, d, 1);
        expect(n == 0 || (n == 1 && d[0].item == ML_ITEM_SLIME_BALL
                          && d[0].count >= 1 && d[0].count <= 2)
               || (n == 1 && d[0].count == 0),
               "size-1 slimeball nextInt(3) count");
        /* count 0 is not emitted (n==0). nextInt(3) is 0..2. */
        expect(n == 0 || (d[0].item == ML_ITEM_SLIME_BALL && d[0].count >= 1
                          && d[0].count <= 2),
               "size-1 drop is 1..2 slimeballs or empty");
    }
    expect(bits_eq_f(hs_moon_phase_factor(0), 1.0f), "moon phase 0 factor 1.0");
    expect(bits_eq_f(hs_moon_phase_factor(24000LL), 0.75f),
           "moon phase 1 factor 0.75");
    expect(bits_eq_f(hs_moon_phase_factor(96000LL), 0.0f),
           "moon phase 4 factor 0.0");
    {
        JavaRandom er;
        int have = 0;
        double g = 0.0;
        int sz;
        jrand_set(&er, 11);
        sz = hs_slime_init(&er, &have, &g, 18000LL, 2);
        expect(sz == 1 || sz == 2 || sz == 4, "onInitialSpawn size is 1<<i");
    }
    {
        /* Chunk.getRandomWithSeed overflow: just a deterministic pin. */
        int a = hs_is_slime_chunk(0, 0, 0);
        int b = hs_is_slime_chunk(0, 0, 0);
        expect(a == b, "slime-chunk is a pure function of seed+chunk");
    }
    expect(hs_is_roster(HS_SPIDER) && hs_is_roster(HS_SLIME),
           "spider and slime insert on the MONSTER path");
    expect(!hs_is_roster(HS_WITCH) && !hs_is_roster(HS_ENDERMAN),
           "witch still consume then skip; enderman spawn insert is a later commit");
    expect(ml_is_roster(EW_TYPE_ENDERMAN), "enderman is on the live roster");
    expect(bits_eq_f(ehs_max_health(EW_TYPE_ENDERMAN), 40.0f),
           "enderman MAX_HEALTH 40");
    expect(bits_eq_f(ehs_land_speed(EW_TYPE_ENDERMAN), 0.30000001192092896f),
           "enderman SPEED 0.30000001192092896");
    expect(bits_eq_f(ml_melee_damage(EW_TYPE_ENDERMAN), 7.0f),
           "enderman ATTACK_DAMAGE 7");
    expect(ml_follow_range(EW_TYPE_ENDERMAN) == 64.0, "enderman FOLLOW_RANGE 64");
    {
        float w, h;
        ehs_size(EW_TYPE_ENDERMAN, &w, &h);
        expect(bits_eq_f(w, 0.6f) && bits_eq_f(h, 2.9f),
               "enderman setSize 0.6x2.9");
    }
    {
        JavaRandom er;
        MlDrop d[1];
        int n;
        jrand_set(&er, 1);
        n = ml_enderman_drop(&er, d, 1);
        expect(n == 0 || (n == 1 && d[0].item == ML_ITEM_ENDER_PEARL
                          && d[0].count == 1),
               "ender pearl dropFewItems nextInt(2) is 0 or 1");
    }
    {
        RlSnapMob dying;
        int t, keep;
        memset(&dying, 0, sizeof dying);
        dying.type = EW_TYPE_SLIME;
        dying.alive = 1;
        dying.health = 0.0f;
        dying.swell = 2;
        keep = 1;
        for (t = 0; t < 19 && keep; ++t)
            keep = ml_on_death_update(&dying);
        expect(keep == 1 && dying.death_time == 19,
               "onDeathUpdate keeps the slot through deathTime 19");
        keep = ml_on_death_update(&dying);
        expect(keep == 0 && dying.death_time == 20,
               "EntityLivingBase.onDeathUpdate setDead at deathTime==20");
    }
    expect(hs_total_weight(1) == 515, "plains monster list weight is 515");
    expect(hs_total_weight(HS_BIOME_SWAMP) == 516,
           "BiomeSwamp extra slime weight 1 -> 516");
    expect(hs_weight_at_biome(HS_SLIME, 1) == 100, "plains slime weight 100");
    expect(hs_weight_at_biome(HS_SLIME, HS_BIOME_SWAMP) == 101,
           "swamp slime weight 100+1");
    {
        JavaRandom wr, er;
        int have = 0;
        double g = 0.0;
        u64 before, after_norm, after_hard;
        jrand_set(&wr, 99);
        jrand_set(&er, 7);
        before = wr.seed;
        hs_spider_init(&wr, &er, &have, &g, 2, 18000LL);
        after_norm = wr.seed;
        expect(after_norm != before, "spider jockey draw consumes world.rand");
        jrand_set(&wr, 99);
        have = 0;
        g = 0.0;
        jrand_set(&er, 7);
        hs_spider_init(&wr, &er, &have, &g, 3, 2000000LL);
        after_hard = wr.seed;
        expect(after_hard != 0, "HARD spider potion roll consumes or skips nextFloat");
        (void)after_norm;
    }
    {
        HsState st0;
        HsTestW tw0;
        tw0.stone_y = 64;
        hs_init(&st0, 1);
        st0.difficulty = 0;
        (void)hs_find_chunks_for_creatures(&tw0, &st0, 8.5, 65.0, 8.5);
        (void)hs_find_chunks_for_spawning(&tw0, &st0, 8.5, 65.0, 8.5);
        (void)hs_creature_cap(289);
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
