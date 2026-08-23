/* Witch lockstep fixture baker + units.
 *
 * --write-fixture FROM OUT copies the mobs_end region and plants a
 * witch next to that set. */
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

#define ML_W HsTestW
#define ML_BLOCK(w, x, y, z) hs_test_block((w), (x), (y), (z))
#define ML_SKY(w, x, y, z) ((void)(w), (void)(x), (void)(y), (void)(z), 15)
#define ML_BLK(w, x, y, z) ((void)(w), (void)(x), (void)(y), (void)(z), 0)
#include "hostile_live.h"

typedef struct {
    HsTestW blocks;
    MlMob *hit;
    int hits;
} PlHitW;

static int pl_test_hit_mob(PlHitW *w, double x, double y, double z,
                           double radius, float damage) {
    MlMob *m;
    double dx, dy, dz;
    if (!w || !w->hit || !w->hit->snap.alive) return 0;
    m = w->hit;
    dx = m->snap.x - x;
    dy = (m->snap.y + 0.9) - y;
    dz = m->snap.z - z;
    if (dx * dx + dy * dy + dz * dz > radius * radius) return 0;
    ++w->hits;
    if (m->snap.type == EW_TYPE_ENDERMAN)
        return ml_enderman_arrow_hit(m, &w->blocks);
    m->snap.health -= damage;
    if (m->snap.health < 0.0f) m->snap.health = 0.0f;
    return 1;
}

#define PL_W PlHitW
#define PL_BLOCK(w, x, y, z) hs_test_block(&(w)->blocks, (x), (y), (z))
#define PL_HIT_MOB(w, x, y, z, rad, dmg) \
    pl_test_hit_mob((w), (x), (y), (z), (rad), (dmg))
#include "projectile_live.h"

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
    s.n_mobs = 6;
    plant_hostile(&s.mobs[0], 1, 1, EW_TYPE_ZOMBIE, 8.5, 65.0, 11.5, 0);
    plant_hostile(&s.mobs[1], 2, 2, EW_TYPE_SKELETON, 12.5, 65.0, 8.5, 0);
    plant_hostile(&s.mobs[2], 3, 3, EW_TYPE_SPIDER, 6.5, 65.0, 8.5, 0);
    plant_hostile(&s.mobs[3], 4, 4, EW_TYPE_SLIME, 11.5, 65.0, 11.5, 2);
    plant_hostile(&s.mobs[4], 5, 5, EW_TYPE_ENDERMAN, 10.5, 65.0, 6.5, 0);
    plant_hostile(&s.mobs[5], 6, 6, EW_TYPE_WITCH, 10.5, 65.0, 11.5, 0);
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
            "enderman (10.5,65,6.5) witch (10.5,65,11.5) "
            "player (8.5,65,8.5) n_mobs=%u digest=0x%016llx\n",
            out_path, s.n_mobs,
            (unsigned long long)blaze_snap_mobs_digest(s.mobs, s.n_mobs));
    blaze_snapshot_free(&s);
    return 1;
}

static int run_units(void) {
    float w, h;
    expect(ml_is_roster(EW_TYPE_WITCH) && hs_is_roster(HS_WITCH),
           "witch is on live and spawn roster");
    expect(hs_weight_at(HS_WITCH) == 5, "Biome.java:153 witch weight 5/1-1");
    ehs_size(EW_TYPE_WITCH, &w, &h);
    expect(bits_eq_f(w, 0.6f) && bits_eq_f(h, 1.95f),
           "witch setSize 0.6x1.95 EntityWitch.java:59");
    expect(bits_eq_f(ehs_max_health(EW_TYPE_WITCH), 26.0f),
           "witch MAX_HEALTH 26 EntityWitch.java:115");
    expect(bits_eq_f(ehs_land_speed(EW_TYPE_WITCH), 0.25f),
           "witch SPEED 0.25 EntityWitch.java:116");
    expect(ml_xp_points(EW_TYPE_WITCH, 0) == 5, "witch XP 5 EntityMob.java:27");
    expect(ml_attack_cooldown(EW_TYPE_WITCH) == 60,
           "EntityAIAttackRanged interval 60");
    expect(ML_WITCH_DRINK_TICKS == 32, "ItemPotion.java:90 drink 32 ticks");
    {
        JavaRandom er;
        MlDrop d[7];
        int n, i, ok = 1;
        jrand_set(&er, 1);
        n = ml_witch_drop(&er, d, 7);
        expect(n >= 0 && n <= 3, "witch loot rolls 1..3, empty counts dropped");
        for (i = 0; i < n; ++i) {
            int item = d[i].item;
            if (item != ML_ITEM_GLOWSTONE && item != ML_ITEM_SUGAR &&
                item != ML_ITEM_REDSTONE && item != ML_ITEM_SPIDER_EYE &&
                item != ML_ITEM_GLASS_BOTTLE && item != ML_ITEM_GUNPOWDER &&
                item != ML_ITEM_STICK)
                ok = 0;
            if (d[i].count < 1 || d[i].count > 2) ok = 0;
        }
        expect(ok, "witch drop items are the 7-item table counts 1..2");
    }
    {
        HsTestW tw;
        MlMob mm;
        float hp0;
        unsigned long long seed0;
        tw.stone_y = 64;
        memset(&mm, 0, sizeof mm);
        mm.snap.type = EW_TYPE_ENDERMAN;
        mm.snap.alive = 1;
        mm.snap.health = 40.0f;
        mm.snap.x = 8.5;
        mm.snap.y = 65.0;
        mm.snap.z = 8.5;
        mm.snap.on_ground = 1;
        mm.snap.seed48 = 1;
        hp0 = mm.snap.health;
        seed0 = mm.snap.seed48;
        (void)ml_enderman_arrow_hit(&mm, &tw);
        expect(mm.snap.health == hp0,
               "arrow vs enderman skips HP EntityEnderman.java:371-381");
        expect(mm.snap.seed48 != seed0,
               "64-try teleportRandomly consumes entity.rand");
    }
    {
        /* Type 1 is the mob-hit arrow (projectile_live.h). Type 2 skeleton
         * arrows only hit the player, same as magma runtime.c. */
        PlHitW aw;
        PlProj p;
        MlMob em, zm;
        float ehp, zhp;
        unsigned long long eseed;
        memset(&aw, 0, sizeof aw);
        aw.blocks.stone_y = 64;
        memset(&em, 0, sizeof em);
        em.snap.type = EW_TYPE_ENDERMAN;
        em.snap.alive = 1;
        em.snap.health = 40.0f;
        em.snap.x = 8.5;
        em.snap.y = 65.0;
        em.snap.z = 8.5;
        em.snap.on_ground = 1;
        em.snap.seed48 = 1;
        ehp = em.snap.health;
        eseed = em.snap.seed48;
        memset(&p, 0, sizeof p);
        p.active = 1;
        p.type = 1;
        p.x = 8.5;
        p.y = 65.9;
        p.z = 8.5;
        p.vx = 0.0;
        p.vy = 0.0;
        p.vz = 0.1;
        aw.hit = &em;
        pl_tick_arrow(&p, &aw);
        expect(aw.hits == 1, "type-1 arrow PL_HIT_MOB reaches planted enderman");
        expect(em.snap.health == ehp,
               "type-1 arrow vs enderman skips HP EntityEnderman.java:371-381");
        expect(em.snap.seed48 != eseed,
               "type-1 arrow runs 64-try teleportRandomly");
        expect(!p.active, "arrow deactivates on the enderman hit");
        memset(&zm, 0, sizeof zm);
        zm.snap.type = EW_TYPE_ZOMBIE;
        zm.snap.alive = 1;
        zm.snap.health = 20.0f;
        zm.snap.x = 8.5;
        zm.snap.y = 65.0;
        zm.snap.z = 8.5;
        zhp = zm.snap.health;
        memset(&p, 0, sizeof p);
        p.active = 1;
        p.type = 1;
        p.x = 8.5;
        p.y = 65.9;
        p.z = 8.5;
        p.vx = 0.0;
        p.vy = 0.0;
        p.vz = 0.1;
        aw.hit = &zm;
        aw.hits = 0;
        pl_tick_arrow(&p, &aw);
        expect(aw.hits == 1 && zm.snap.health < zhp,
               "type-1 arrow applies HP to a planted zombie");
    }
    {
        JavaRandom er;
        RlSnapMob s;
        memset(&s, 0, sizeof s);
        jrand_set(&er, 3);
        expect(jrand_float(&er) >= 0.0f, "drink pick stream is live");
        expect(ml_witch_has_effect(&s, ML_POTION_SPEED) == 0,
               "empty effect table is inactive");
        s.effect_id = ML_POTION_SPEED;
        s.effect_duration = 10;
        expect(ml_witch_has_effect(&s, ML_POTION_SPEED) == 1,
               "stored speed effect is active");
    }
    expect(hs_total_weight(1) == 515, "plains monster list weight is 515");
    {
        HsState st0;
        HsTestW tw0;
        tw0.stone_y = 64;
        hs_init(&st0, 1);
        st0.difficulty = 0;
        (void)hs_find_chunks_for_creatures(&tw0, &st0, 8.5, 65.0, 8.5);
        (void)hs_find_chunks_for_spawning(&tw0, &st0, 8.5, 65.0, 8.5);
        (void)g_hs_placed;
        (void)g_hs_last_type;
        (void)g_hs_last_extra;
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
