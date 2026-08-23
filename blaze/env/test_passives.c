/* Passive live-tick unit tests + fixture baker.
 *
 * Units: Java sizes/health, CREATURE cap, 400-tick gate, drop draws, despawn.
 * --write-fixture FROM OUT copies a magma region, plants cow/pig/sheep/chicken
 * on a grass pad, plus a far grass pad for CREATURE ON_GROUND beyond 24. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "entity_spine.h"
#include "passive_live.h"
#include "mc_blocks.h"
#include "mc_rng.h"

typedef struct {
    int grass_y;
} PsTestW;

static int ps_test_block(const PsTestW *w, int x, int y, int z) {
    (void)x;
    (void)z;
    if (!w) return 0;
    if (y == w->grass_y) return BLK_GRASS;
    if (y == w->grass_y - 1) return BLK_DIRT;
    return 0;
}

static int g_ps_placed;
static int g_ps_last_type;

static int ps_test_place(PsTestW *w, int type, double x, double y, double z,
                         float yaw, unsigned long long seed48, int have_g,
                         double g, int extra) {
    (void)w; (void)x; (void)y; (void)z;
    (void)yaw; (void)seed48; (void)have_g; (void)g; (void)extra;
    g_ps_last_type = type;
    ++g_ps_placed;
    return 1;
}

#define HS_W PsTestW
#define HS_BLOCK(w, x, y, z) ps_test_block((w), (x), (y), (z))
#define HS_SKY(w, x, y, z) 15
#define HS_BLK(w, x, y, z) 0
#define HS_PLACE(w, type, x, y, z, yaw, seed48, have_g, g, extra) \
    ps_test_place((w), (type), (x), (y), (z), (yaw), (seed48), (have_g), (g), (extra))
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

static void plant_cell(CuSnapshot *s, int wx, int wy, int wz, int id, int meta,
                       unsigned char light) {
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
        s->light[idx] = light;
}

static void plant_passive(RlSnapMob *o, int slot, int id, int type,
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
    o->health = ehs_max_health((u8)type);
    o->on_ground = 1;
    o->box_on = 1;
    o->box_minx = x - (double)(w * 0.5f);
    o->box_miny = y;
    o->box_minz = z - (double)(w * 0.5f);
    o->box_maxx = x + (double)(w * 0.5f);
    o->box_maxy = y + (double)h;
    o->box_maxz = z + (double)(w * 0.5f);
    o->seed48 = 1;
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
            plant_cell(&s, x, 64, z, BLK_GRASS, 0, 0xF0);
            for (y = 65; y <= 67; ++y)
                plant_cell(&s, x, y, z, 0, 0, 0xF0);
        }

    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 4;
    plant_passive(&s.mobs[0], 1, 1, EW_TYPE_CHICKEN, 8.5, 65.0, 11.5);
    plant_passive(&s.mobs[1], 2, 2, EW_TYPE_COW, 11.5, 65.0, 11.5);
    plant_passive(&s.mobs[2], 3, 3, EW_TYPE_PIG, 11.5, 65.0, 8.5);
    plant_passive(&s.mobs[3], 4, 4, EW_TYPE_SHEEP, 5.5, 65.0, 8.5);
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
                plant_cell(&s, px, 64, pz, BLK_GRASS, 0, 0xF0);
                for (py = 65; py <= 67; ++py)
                    plant_cell(&s, px, py, pz, 0, 0, 0xF0);
            }
    }
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s cow/pig/sheep/chicken player (8.5,65,8.5) n_mobs=%u "
            "digest=0x%016llx\n",
            out_path, s.n_mobs,
            (unsigned long long)blaze_snap_mobs_digest(s.mobs, s.n_mobs));
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    float w, h;
    ehs_size(EW_TYPE_COW, &w, &h);
    expect(bits_eq_f(w, 0.9f) && bits_eq_f(h, 1.4f), "EntityCow setSize 0.9x1.4");
    ehs_size(EW_TYPE_PIG, &w, &h);
    expect(bits_eq_f(w, 0.9f) && bits_eq_f(h, 0.9f), "EntityPig setSize 0.9x0.9");
    ehs_size(EW_TYPE_SHEEP, &w, &h);
    expect(bits_eq_f(w, 0.9f) && bits_eq_f(h, 1.3f), "EntitySheep setSize 0.9x1.3");
    ehs_size(EW_TYPE_CHICKEN, &w, &h);
    expect(bits_eq_f(w, 0.4f) && bits_eq_f(h, 0.7f),
           "EntityChicken setSize 0.4x0.7");
    expect(bits_eq_f(ehs_max_health(EW_TYPE_COW), 10.0f), "cow health 10");
    expect(bits_eq_f(ehs_max_health(EW_TYPE_PIG), 10.0f), "pig health 10");
    expect(bits_eq_f(ehs_max_health(EW_TYPE_SHEEP), 8.0f), "sheep health 8");
    expect(bits_eq_f(ehs_max_health(EW_TYPE_CHICKEN), 4.0f), "chicken health 4");
    expect(ehs_is_passive(EW_TYPE_COW) && !ehs_is_hostile(EW_TYPE_COW),
           "cow is passive not hostile");
    expect(hs_creature_cap(289) == 10, "CREATURE cap 10 * 289 / 289 = 10");
    expect(pl_panic_mul(EW_TYPE_COW) == 2.0, "cow panic speed 2.0");
    expect(pl_panic_mul(EW_TYPE_CHICKEN) == 1.4, "chicken panic speed 1.4");
    expect(pl_panic_mul(EW_TYPE_SHEEP) == 1.25, "sheep panic speed 1.25");
    {
        JavaRandom er;
        PlDrop d[4];
        int n;
        jrand_set(&er, 1);
        n = pl_drop_few(EW_TYPE_COW, 0, 0, &er, d, 4);
        expect(n >= 1, "cow dropFewItems at least beef");
        {
            int xp = pl_xp_points(&er);
            expect(xp >= 1 && xp <= 3, "EntityAnimal XP is 1+nextInt(3)");
        }
    }
    {
        int age = 0;
        u64 seed48 = 1;
        (void)age; (void)seed48;
        {
            MlMob m;
            memset(&m, 0, sizeof m);
            m.snap.alive = 1;
            m.snap.type = EW_TYPE_COW;
            m.snap.persist = 0;
            pl_passive_pre(&m);
            expect(m.despawn_ticks == 1, "non-persist animal still ages");
            m.snap.persist = 1;
            pl_passive_pre(&m);
            expect(m.despawn_ticks == 0, "persist zeros age; canDespawn false never kills");
        }
    }
    {
        PsTestW tw;
        HsState st;
        int i;
        tw.grass_y = 64;
        hs_init(&st, 10);
        st.world_time = 6000;
        g_ps_placed = 0;
        for (i = 0; i < 80 && g_ps_placed == 0; ++i)
            (void)hs_find_chunks_for_creatures(&tw, &st, 8.5, 65.0, 8.5);
        expect(g_ps_placed > 0,
               "day CREATURE findChunksForSpawning places a roster animal");
    }
    {
        JavaRandom r;
        int seen[4];
        int i, t;
        memset(seen, 0, sizeof seen);
        jrand_set(&r, 12345);
        for (i = 0; i < 800; ++i) {
            t = hs_creature_weighted_pick(&r);
            if (t >= 0 && t < 4) seen[t] = 1;
        }
        expect(seen[0] && seen[1] && seen[2] && seen[3],
               "WeightedRandom hits sheep/pig/chicken/cow");
    }
    {
        HsState st0;
        PsTestW tw0;
        tw0.grass_y = 64;
        hs_init(&st0, 1);
        st0.difficulty = 0;
        (void)hs_find_chunks_for_spawning(&tw0, &st0, 8.5, 65.0, 8.5);
        (void)hs_monster_cap(289);
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
