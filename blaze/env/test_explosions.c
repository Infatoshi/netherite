/* Creeper fuse + explosion apply unit tests + fixture baker.
 *
 * Units: EntityCreeper fuse 30 ignited, density scale, center damage.
 * --write-fixture FROM OUT copies a magma region, grounds the s10 player
 * on stone, plants an ignited creeper 4 blocks +Z. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"
#include "entity_spine.h"
#include "mc_blocks.h"
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#include "explosion_live.h"
#pragma GCC diagnostic pop

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
    /* TNT block in the size-4 blast so chain fuse world.rand.nextInt is live. */
    plant_cell(&s, 7, 65, 12, BLK_TNT, 0);

    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 2;
    plant_creeper(&s.mobs[0], 1, 1, 8.5, 65.0, 12.5);
    /* Planted EntityTNTPrimed already counting down (fuse 20) so the 64-tick
     * chain observes explode size 4.0F. Java spawn fuse is 80. */
    {
        RlSnapMob *t = &s.mobs[1];
        memset(t, 0, sizeof *t);
        t->slot = 2;
        t->id = 2;
        t->type = EW_TYPE_TNT_PRIMED;
        t->alive = 1;
        t->x = 6.5;
        t->y = 65.0;
        t->z = 8.5;
        t->on_ground = 1;
        t->swell = 20;
        t->box_on = 1;
        t->box_minx = 6.5 - 0.49;
        t->box_miny = 65.0;
        t->box_minz = 8.5 - 0.49;
        t->box_maxx = 6.5 + 0.49;
        t->box_maxy = 65.0 + (double)EXL_TNT_HEIGHT;
        t->box_maxz = 8.5 + 0.49;
    }
    if (!blaze_snapshot_write(out_path, &s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", out_path, err);
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr,
            "WROTE %s ignited creeper (8.5,65,12.5) TNT fuse20 (6.5,65,8.5) "
            "TNT block (7,65,12) player (8.5,65,8.5) n_mobs=%u\n",
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
           "NULL-rand density scale is 0.7F + 0.5F * 0.6F");
    expect(ex_face_ray_count() == 16 * 16 * 16 - 14 * 14 * 14,
           "face rays are 16^3 - 14^3");
    expect(ex_face_ray_count() == 1352, "face-ray count is 1352");
    {
        JavaRandom r;
        u16 grid[EX_VOL];
        u8 bitset[EX_VOL];
        u64 before, after;
        int n, j, k, l, face = 0;
        jrand_set(&r, 0);
        for (j = 0; j < 16; ++j)
            for (k = 0; k < 16; ++k)
                for (l = 0; l < 16; ++l)
                    if (j == 0 || j == 15 || k == 0 || k == 15 || l == 0 ||
                        l == 15)
                        ++face;
        expect(face == ex_face_ray_count(),
               "Explosion.java:88-94 face predicate matches 16^3-14^3");
        before = r.seed;
        ex_fill(grid, mc_state(BLK_AIR, 0));
        ex_do_explosion_blocks(grid, 8.0, 8.0, 8.0, 4.0f, bitset, &r);
        after = r.seed;
        {
            JavaRandom c;
            jrand_set(&c, 0);
            for (n = 0; n < face; ++n)
                (void)jrand_float(&c);
            expect(c.seed == after,
                   "doExplosionA consumes one nextFloat per face ray");
            expect(before != after, "ray loop advances World.rand");
        }
        expect(bits_eq_f(ex_ray_strength(4.0f, NULL), 4.0f * ex_density_scale()),
               "NULL rand ray strength is the fixed 0.5F scale");
    }
    {
        JavaRandom r;
        int i, f, lo = 99, hi = -1, bad = 0;
        jrand_set(&r, 1);
        for (i = 0; i < 256; ++i) {
            f = exl_chain_fuse(&r);
            if (f < lo) lo = f;
            if (f > hi) hi = f;
            if (f < EXL_TNT_FUSE / 8 ||
                f >= EXL_TNT_FUSE / 8 + EXL_TNT_FUSE / 4)
                bad = 1;
        }
        expect(!bad && lo >= EXL_TNT_FUSE / 8 &&
                   hi < EXL_TNT_FUSE / 8 + EXL_TNT_FUSE / 4,
               "chain fuse is nextInt(fuse/4)+fuse/8 in [10,29]");
    }
    {
        CuSnapshot a, b;
        char err[256];
        char pa[128], pb[128];
        snprintf(pa, sizeof pa, "/tmp/worldrand_wr_a_%d.bsnp", (int)getpid());
        snprintf(pb, sizeof pb, "/tmp/worldrand_wr_b_%d.bsnp", (int)getpid());
        memset(&a, 0, sizeof a);
        a.head.magic[0] = 'B'; a.head.magic[1] = 'S';
        a.head.magic[2] = 'N'; a.head.magic[3] = 'P';
        a.head.version = BLAZE_SNAP_VERSION;
        a.head.rnx = 2; a.head.rny = 2; a.head.rnz = 2;
        a.cells = (unsigned short *)calloc(8, sizeof(unsigned short));
        a.light = (unsigned char *)calloc(8, 1);
        a.world_rand_seed = 0x123456789abULL & ((1ULL << 48) - 1);
        expect(a.cells && a.light, "tiny snapshot alloc");
        expect(blaze_snapshot_write(pa, &a, err, (int)sizeof err),
               "write v5 world_rand");
        memset(&b, 0, sizeof b);
        expect(blaze_snapshot_load(pa, &b, err, (int)sizeof err, 1),
               "load v5 world_rand");
        expect(b.head.version == BLAZE_SNAP_VERSION, "loaded version is 5");
        expect(b.world_rand_seed == a.world_rand_seed,
               "world_rand_seed round-trips");
        expect(blaze_snapshot_write(pb, &b, err, (int)sizeof err),
               "rewrite v5");
        blaze_snapshot_free(&a);
        blaze_snapshot_free(&b);
        unlink(pa);
        unlink(pb);
    }

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
    expect(bits_eq_f(EXL_TNT_SIZE, 4.0f), "TNT explosion size is 4.0F");
    expect(EXL_TNT_FUSE == 80, "TNT fuse is 80");
    {
        double x = 8.5, y = 65.0, z = 8.5, mx = 0.0, my = EXL_TNT_SPAWN_MY,
               mz = 0.0;
        int og = 0, fuse = 2;
        expect(exl_tnt_on_update(&x, &y, &z, &mx, &my, &mz, &og, &fuse, 1, 65.0)
               == 0,
               "TNT fuse 2 does not explode on first tick");
        expect(fuse == 1, "TNT fuse decrements");
        expect(exl_tnt_on_update(&x, &y, &z, &mx, &my, &mz, &og, &fuse, 1, 65.0),
               "TNT fuse 0 explodes");
    }
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
