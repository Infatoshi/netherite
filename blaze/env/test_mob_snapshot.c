/* Snapshot v3 format: v2 load (n_mobs=0), v3 save/load/save identity,
 * BP_MOBS digest over packed RlSnapMob. Host-only; no magma, no tick. */
#define _POSIX_C_SOURCE 200809L
#include "blaze_snapshot.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
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

static int file_eq(const char *a, const char *b) {
    FILE *fa = fopen(a, "rb"), *fb = fopen(b, "rb");
    unsigned char ba[4096], bb[4096];
    size_t na, nb;
    int same = 1;
    if (!fa || !fb) {
        if (fa) fclose(fa);
        if (fb) fclose(fb);
        return 0;
    }
    for (;;) {
        na = fread(ba, 1, sizeof ba, fa);
        nb = fread(bb, 1, sizeof bb, fb);
        if (na != nb || memcmp(ba, bb, na) != 0) {
            same = 0;
            break;
        }
        if (na < sizeof ba) break;
    }
    if (same && (ferror(fa) || ferror(fb))) same = 0;
    fclose(fa);
    fclose(fb);
    return same;
}

static void fill_tiny(CuSnapshot *s, unsigned version) {
    long vol;
    unsigned i;
    memset(s, 0, sizeof *s);
    s->head.magic[0] = 'B';
    s->head.magic[1] = 'S';
    s->head.magic[2] = 'N';
    s->head.magic[3] = 'P';
    s->head.version = version;
    s->head.seed = 14;
    s->head.px = 8.5;
    s->head.py = 5.0;
    s->head.pz = 8.5;
    s->head.box[0] = 8.2;
    s->head.box[1] = 5.0;
    s->head.box[2] = 8.2;
    s->head.box[3] = 8.8;
    s->head.box[4] = 6.8;
    s->head.box[5] = 8.8;
    s->head.on_ground = 1;
    s->head.health = 20.f;
    s->head.food = 20;
    s->head.rnx = 4;
    s->head.rny = 8;
    s->head.rnz = 4;
    vol = 4L * 8L * 4L;
    s->cells = (unsigned short *)calloc((size_t)vol, sizeof *s->cells);
    s->light = (unsigned char *)calloc((size_t)vol, 1);
    if (!s->cells || !s->light) abort();
    for (i = 0; i < (unsigned)vol; ++i)
        if ((i / 4) % 8 == 0)
            s->cells[i] = (unsigned short)(1 << 4);
}

static void fill_synth_mob(RlSnapMob *o, int slot, int type) {
    int p;
    memset(o, 0, sizeof *o);
    o->slot = slot;
    o->id = 100 + slot;
    o->type = type;
    o->alive = 1;
    o->persist = 1;
    o->x = 10.5 + (double)slot;
    o->y = 64.0;
    o->z = -3.25;
    o->yaw = 45.0f;
    o->pitch = -12.5f;
    o->yaw_body = 40.0f;
    o->mx = 0.1;
    o->my = -0.08;
    o->mz = 0.02;
    o->on_ground = 1;
    o->health = 20.0f;
    o->hurt_time = 9;
    o->death_time = 0;
    o->task_bits = 0x15u;
    o->target_tasks = 0x2u;
    o->wander_x = 12.0;
    o->wander_z = -4.0;
    o->panic = 17;
    o->target_idx = 1;
    o->see_time = 3;
    o->stime = -5;
    o->melee_delay = 4;
    o->bow_attack_time = 11;
    o->attack_time = 6;
    o->swell = 7;
    o->anger = 400;
    o->path_x[0] = 1;
    o->path_y[0] = 64;
    o->path_z[0] = 2;
    o->path_x[1] = 2;
    o->path_y[1] = 64;
    o->path_z[1] = 2;
    o->path_x[2] = 3;
    o->path_y[2] = 64;
    o->path_z[2] = 3;
    for (p = 3; p < BLAZE_SNAP_PATH_CAP; ++p) {
        o->path_x[p] = (short)(p + slot);
        o->path_y[p] = 64;
        o->path_z[p] = (short)p;
    }
    o->path_n = 3;
    o->path_i = 1;
    o->nav_ticks = 20;
    o->nav_stuck_at = 8;
    o->nav_stuck_x = o->x;
    o->nav_stuck_y = o->y;
    o->nav_stuck_z = o->z;
    o->box_on = 1;
    o->box_minx = o->x - 0.3;
    o->box_miny = o->y;
    o->box_minz = o->z - 0.3;
    o->box_maxx = o->x + 0.3;
    o->box_maxy = o->y + 1.95;
    o->box_maxz = o->z + 0.3;
    o->seed48 = 0x123456789abULL + (unsigned long long)slot;
    o->have_gauss = 1;
    o->gauss = 0.314159;
}

static int write_copy(const char *path, const CuSnapshot *s) {
    char err[256];
    if (!blaze_snapshot_write(path, s, err, (int)sizeof err)) {
        fprintf(stderr, "write %s: %s\n", path, err);
        return 0;
    }
    return 1;
}

static int roundtrip(const char *a, const char *b, CuSnapshot *s) {
    char err[256];
    CuSnapshot loaded;
    if (!write_copy(a, s)) return 0;
    memset(&loaded, 0, sizeof loaded);
    if (!blaze_snapshot_load(a, &loaded, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load %s: %s\n", a, err);
        return 0;
    }
    if (!write_copy(b, &loaded)) {
        blaze_snapshot_free(&loaded);
        return 0;
    }
    if (s->n_mobs != loaded.n_mobs ||
        memcmp(s->mobs, loaded.mobs, (size_t)s->n_mobs * sizeof s->mobs[0]) !=
            0) {
        fprintf(stderr, "packed mobs changed on load of %s\n", a);
        blaze_snapshot_free(&loaded);
        return 0;
    }
    blaze_snapshot_free(&loaded);
    return file_eq(a, b);
}

static int populate_from(const char *in_path, const char *out_path) {
    char err[256];
    CuSnapshot s;
    memset(&s, 0, sizeof s);
    if (!blaze_snapshot_load(in_path, &s, err, (int)sizeof err, 1)) {
        fprintf(stderr, "load fixture %s: %s\n", in_path, err);
        return 0;
    }
    if (s.head.version < BLAZE_SNAP_VERSION_LIGHT || !s.light) {
        fprintf(stderr, "fixture %s lacks v2 light\n", in_path);
        blaze_snapshot_free(&s);
        return 0;
    }
    s.head.version = BLAZE_SNAP_VERSION;
    s.n_mobs = 2;
    fill_synth_mob(&s.mobs[0], 1, 2); /* EW_TYPE_ZOMBIE */
    fill_synth_mob(&s.mobs[1], 4, 4); /* EW_TYPE_CREEPER */
    if (!write_copy(out_path, &s)) {
        blaze_snapshot_free(&s);
        return 0;
    }
    fprintf(stderr, "POP_SEED=%lld\n", s.head.seed);
    fprintf(stderr, "POP_DIGEST=0x%016llx\n",
            (unsigned long long)blaze_snap_mobs_digest(s.mobs, s.n_mobs));
    blaze_snapshot_free(&s);
    return 1;
}

int main(int argc, char **argv) {
    const char *write_pop = NULL, *write_empty = NULL, *from = NULL;
    char dir[128];
    char p_v2[256], p_a[256], p_b[256];
    CuSnapshot s, loaded;
    char err[256];
    uint64_t empty_h, pop_h, pop_h2;
    int i;

    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--write-pop") && i + 1 < argc)
            write_pop = argv[++i];
        else if (!strcmp(argv[i], "--write-empty") && i + 1 < argc)
            write_empty = argv[++i];
        else if (!strcmp(argv[i], "--from") && i + 1 < argc)
            from = argv[++i];
        else {
            fprintf(stderr, "usage: %s [--from FIXTURE] [--write-pop PATH] "
                    "[--write-empty PATH]\n", argv[0]);
            return 2;
        }
    }

    expect(sizeof(RlSnapHead) == 752, "RlSnapHead is 752 bytes packed");
    expect(sizeof(RlSnapMob) == 544, "RlSnapMob is 544 bytes packed");
    expect(BLAZE_SNAP_VERSION == 3, "snapshot version is 3");
    expect(BLAZE_SNAP_MAX_MOBS == 96, "mob cap is EW_MAX_ENTITIES 96");
    expect(BLAZE_SNAP_PATH_CAP == 48, "path cap is 48");

    empty_h = blaze_snap_mobs_digest(NULL, 0);
    expect(empty_h == blaze_snap_mobs_digest(NULL, 0),
           "empty digest is stable");
    fprintf(stderr, "EMPTY_DIGEST=0x%016llx\n",
            (unsigned long long)empty_h);

    snprintf(dir, sizeof dir, "/tmp/mobsnap_%d", (int)getpid());
    if (mkdir(dir, 0700) != 0) {
        perror("mkdir");
        return 1;
    }
    snprintf(p_v2, sizeof p_v2, "%s/v2.bsnp", dir);
    snprintf(p_a, sizeof p_a, "%s/a.bsnp", dir);
    snprintf(p_b, sizeof p_b, "%s/b.bsnp", dir);

    fill_tiny(&s, 2);
    s.n_mobs = 0;
    expect(write_copy(p_v2, &s), "write synthetic v2");
    memset(&loaded, 0, sizeof loaded);
    expect(blaze_snapshot_load(p_v2, &loaded, err, (int)sizeof err, 1),
           "load synthetic v2");
    expect(loaded.head.version == 2 && loaded.n_mobs == 0,
           "v2 load keeps n_mobs=0");
    expect(blaze_snap_mobs_digest(loaded.mobs, loaded.n_mobs) == empty_h,
           "v2 digest equals empty");
    blaze_snapshot_free(&loaded);

    s.head.version = 3;
    s.n_mobs = 0;
    expect(roundtrip(p_a, p_b, &s), "v3 zero-mob save/load/save identical");

    fill_synth_mob(&s.mobs[0], 1, 2);
    fill_synth_mob(&s.mobs[1], 4, 4);
    s.n_mobs = 2;
    pop_h = blaze_snap_mobs_digest(s.mobs, s.n_mobs);
    expect(pop_h != empty_h, "populated digest differs from empty");
    s.mobs[0].x += 1.0;
    pop_h2 = blaze_snap_mobs_digest(s.mobs, s.n_mobs);
    expect(pop_h2 != pop_h, "pose change flips digest");
    s.mobs[0].x -= 1.0;
    expect(blaze_snap_mobs_digest(s.mobs, s.n_mobs) == pop_h,
           "restored pose restores digest");
    fprintf(stderr, "POP_DIGEST=0x%016llx\n", (unsigned long long)pop_h);
    expect(roundtrip(p_a, p_b, &s), "v3 populated save/load/save identical");

    blaze_snapshot_free(&s);

    if ((write_pop || write_empty) && !from) {
        fprintf(stderr, "FAIL: --write-pop/--write-empty require --from\n");
        fails = 1;
    }
    if (from && write_empty) {
        CuSnapshot e;
        memset(&e, 0, sizeof e);
        if (!blaze_snapshot_load(from, &e, err, (int)sizeof err, 1)) {
            fprintf(stderr, "load fixture %s: %s\n", from, err);
            fails = 1;
        } else {
            e.head.version = BLAZE_SNAP_VERSION;
            e.n_mobs = 0;
            if (!write_copy(write_empty, &e))
                fails = 1;
            else
                fprintf(stderr, "OK: wrote empty v3 %s\n", write_empty);
            blaze_snapshot_free(&e);
        }
    }
    if (from && write_pop) {
        if (!populate_from(from, write_pop))
            fails = 1;
        else
            fprintf(stderr, "OK: wrote populated fixture %s\n", write_pop);
    }

    return fails ? 1 : 0;
}
