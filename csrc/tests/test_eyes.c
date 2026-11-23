#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/det.h"
#include "../engine/item_entity.h"
#include "../engine/projectile.h"
#include "../engine/stronghold.h"
#include "../engine/world.h"

struct stream_ref {
    int64_t seed;
    int32_t next_id[DET_ROLES];
    uint64_t seeder[DET_ROLES], math[DET_ROLES];
    char names[64][DET_NAME_MAX];
    uint64_t state[64][DET_ROLES];
    uint8_t used[64][DET_ROLES];
    int nsplits;
};

static int read_state(const char *path, struct stream_ref *ref)
{
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    memset(ref, 0, sizeof *ref);
    char line[1024];
    while (fgets(line, sizeof line, f))
    {
        long long seed;
        int role;
        unsigned long long a, b, c, d;
        if (sscanf(line, "worldSeed %lld", &seed) == 1) ref->seed = seed;
        else if (sscanf(line, "nextId %d %d %d %d", &ref->next_id[0], &ref->next_id[1],
                        &ref->next_id[2], &ref->next_id[3]) == 4) {}
        else if (sscanf(line, "digest %d %llx %llx %llx", &role, &a, &b, &c) == 4)
        { ref->seeder[role] = a; ref->math[role] = b; }
        else if (ref->nsplits < 64)
        {
            int u[4];
            char name[DET_NAME_MAX];
            if (sscanf(line, "split %127s %llx %llx %llx %llx %d %d %d %d",
                       name, &a, &b, &c, &d, &u[0], &u[1], &u[2], &u[3]) == 9)
            {
                int n = ref->nsplits++;
                snprintf(ref->names[n], DET_NAME_MAX, "%s", name);
                ref->state[n][0] = a; ref->state[n][1] = b;
                ref->state[n][2] = c; ref->state[n][3] = d;
                for (int i = 0; i < 4; ++i) ref->used[n][i] = (uint8_t)u[i];
            }
        }
    }
    fclose(f);
    return 1;
}

static void cmpd(int t, int i, const char *name, double want, double got, int *fails)
{
    if (want == got) return;
    if (*fails < 12) printf("FAIL eye t=%d i=%d %s want=%a got=%a\n", t, i, name, want, got);
    ++*fails;
}

int main(int argc, char **argv)
{
    if (argc != 2) { fprintf(stderr, "usage: test_eyes DIR\n"); return 2; }
    char path[1024];
    snprintf(path, sizeof path, "%s/manifest.json", argv[1]);
    FILE *mf = fopen(path, "r");
    if (!mf) { printf("skip %s: no manifest\n", argv[1]); return 0; }
    char manifest[1024];
    if (!fgets(manifest, sizeof manifest, mf)) return 2;
    fclose(mf);
    if (!strstr(manifest, "\"kind\":\"eyes2\"")) return 0;
    char *p = strstr(manifest, "\"seed\":");
    int64_t seed = p ? strtoll(p + 7, NULL, 10) : 0;
    p = strstr(manifest, "\"drops\":");
    int want_drops = p ? atoi(p + 8) : -1;

    struct world world;
    world_init(&world, seed);
    snprintf(path, sizeof path, "%s/regions.txt", argv[1]);
    FILE *rf = fopen(path, "r");
    if (!rf) return 2;
    int cx, cz;
    while (fscanf(rf, "%d %d", &cx, &cz) == 2)
        for (int x = cx - 5; x <= cx + 5; ++x)
            for (int z = cz - 5; z <= cz + 5; ++z) world_load_chunk(&world, x, z);
    fclose(rf);

    struct stream_ref start, end;
    snprintf(path, sizeof path, "%s/start.txt", argv[1]);
    if (!read_state(path, &start)) return 2;
    snprintf(path, sizeof path, "%s/end.txt", argv[1]);
    if (!read_state(path, &end)) return 2;
    det_state det;
    det_init(&det);
    det_load(&det, start.seed, start.seeder, start.math, start.next_id);
    for (int i = 0; i < start.nsplits; ++i)
        det_split_add(&det, start.names[i], start.state[i], start.used[i]);
    ie_world iew;
    ie_init(&iew, &world, &det);
    iew.role = DET_OTHER;

    int fails = 0, eyes = 0;
    ie_ent *eye_ptrs[16] = {0};
    snprintf(path, sizeof path, "%s/starts.txt", argv[1]);
    FILE *sf = fopen(path, "r");
    if (!sf) return 2;
    char xs[64], ys[64], zs[64];
    int i, tx, ty, tz, id, drop;
    while (fscanf(sf, "%d %63s %63s %63s %d %d %d %d %d",
                  &i, xs, ys, zs, &tx, &ty, &tz, &id, &drop) == 9)
    {
        /* The recorded target is ItemEnderEye.findClosestStructure's result. */
        {
            int nx = 0, ny = 0, nz = 0;
            stronghold_nearest(seed, (int)strtod(xs, NULL), (int)strtod(ys, NULL),
                               (int)strtod(zs, NULL), &nx, &ny, &nz);
            if (nx != tx || ny != ty || nz != tz)
            { printf("FAIL eye %d stronghold target want=%d,%d,%d got=%d,%d,%d\n",
                     i, tx, ty, tz, nx, ny, nz); ++fails; }
        }
        ie_ent *e = proj_spawn_ender_eye(&iew, strtod(xs, NULL), strtod(ys, NULL),
                                          strtod(zs, NULL), (double)tx, ty, (double)tz);
        e->spawn_index = i;
        eye_ptrs[i] = e;
        if (e->entity_id != id || e->eye_drop != drop) ++fails;
        ++eyes;
    }
    fclose(sf);
    snprintf(path, sizeof path, "%s/ticks.txt", argv[1]);
    FILE *tf = fopen(path, "r");
    if (!tf) return 2;
    char line[1024], sx[64], sy[64], sz[64], mx[64], my[64], mz[64];
    int rt, ri, rid, timer;
    unsigned yaw, pitch;
    unsigned long long rand;
    int has_line = fgets(line, sizeof line, tf) != NULL;
    for (int t = 0; t < 82; ++t)
    {
        for (int j = 0; j < eyes; ++j)
            if (eye_ptrs[j] && ie_tick_one(&iew, eye_ptrs[j], t, NULL, 0, NULL))
                eye_ptrs[j] = NULL;
        while (has_line && sscanf(line, "%d", &rt) == 1 && rt == t)
        {
            if (sscanf(line, "%d %d %d %63s %63s %63s %63s %63s %63s %x %x %d %llx",
                       &rt, &ri, &rid, sx, sy, sz, mx, my, mz, &yaw, &pitch, &timer, &rand) != 13)
                return 2;
            ie_ent *e = NULL;
            for (int j = 0; j < iew.n; ++j)
                if (ie_ent_at(iew.slot[j])->kind == IE_ENDER_EYE && ie_ent_at(iew.slot[j])->spawn_index == ri) e = ie_ent_at(iew.slot[j]);
            if (!e) { if (fails < 12) printf("FAIL eye t=%d i=%d missing\n", t, ri); ++fails; }
            else
            {
                cmpd(t, ri, "x", strtod(sx, NULL), e->e.pos_x, &fails);
                cmpd(t, ri, "y", strtod(sy, NULL), e->e.pos_y, &fails);
                cmpd(t, ri, "z", strtod(sz, NULL), e->e.pos_z, &fails);
                cmpd(t, ri, "mx", strtod(mx, NULL), e->e.motion_x, &fails);
                cmpd(t, ri, "my", strtod(my, NULL), e->e.motion_y, &fails);
                cmpd(t, ri, "mz", strtod(mz, NULL), e->e.motion_z, &fails);
                uint32_t yb, pb;
                memcpy(&yb, &e->rotation_yaw, 4);
                memcpy(&pb, &e->rotation_pitch, 4);
                if (yb != yaw || pb != pitch || e->eye_timer != timer ||
                    det_rng_state(&e->rand) != rand)
                { if (fails < 12) printf("FAIL eye t=%d i=%d yaw/pitch/timer/random\n", t, ri); ++fails; }
            }
            has_line = fgets(line, sizeof line, tf) != NULL;
        }
    }
    fclose(tf);
    for (int j = 0; j < eyes; ++j)
        if (eye_ptrs[j] != NULL)
        { printf("FAIL eye %d survived past tick 81\n", j); ++fails; }
    int drops = 0;
    for (int j = 0; j < iew.n; ++j) if (ie_ent_at(iew.slot[j])->kind == IE_ITEM) ++drops;
    if (drops != want_drops || det.next_id[DET_OTHER] != end.next_id[DET_OTHER] ||
        det_seeder_state(&det, DET_OTHER) != end.seeder[DET_OTHER] ||
        det_math_state(&det, DET_OTHER) != end.math[DET_OTHER])
    { printf("FAIL eyes end state: drops %d/%d, id %d/%d\n", drops, want_drops,
             det.next_id[DET_OTHER], end.next_id[DET_OTHER]); ++fails; }

    struct stream_ref use_start, use_end;
    snprintf(path, sizeof path, "%s/use_start.txt", argv[1]);
    if (!read_state(path, &use_start)) return 2;
    snprintf(path, sizeof path, "%s/use_end.txt", argv[1]);
    if (!read_state(path, &use_end)) return 2;
    det_state use_det;
    det_init(&use_det);
    det_load(&use_det, use_start.seed, use_start.seeder, use_start.math, use_start.next_id);
    for (int j = 0; j < use_start.nsplits; ++j)
        det_split_add(&use_det, use_start.names[j], use_start.state[j], use_start.used[j]);
    ie_world use_world;
    ie_init(&use_world, &world, &use_det);
    use_world.role = DET_OTHER;
    snprintf(path, sizeof path, "%s/use.txt", argv[1]);
    FILE *uf = fopen(path, "r");
    if (!uf) return 2;
    char pxs[64], pys[64], pzs[64], offs[64], exs[64], eys[64], ezs[64];
    char txs[64], tys[64], tzs[64];
    int uid, uindex, udrop, stack_count, held = 2;
    unsigned long long urand;
    while (fscanf(uf, "%d %63s %63s %63s %63s %d %63s %63s %63s %63s %63s %63s %d %d %llx",
                  &uindex, pxs, pys, pzs, offs, &uid, exs, eys, ezs,
                  txs, tys, tzs, &udrop, &stack_count, &urand) == 15)
    {
        ie_ent *e = proj_use_ender_eye(&use_world, strtod(pxs, NULL), strtod(pys, NULL),
                                        strtod(pzs, NULL), strtof(offs, NULL), 0, &held);
        if (!e || e->entity_id != uid || e->eye_drop != udrop || held != stack_count ||
            det_rng_state(&e->rand) != urand)
        { printf("FAIL eye item use %d constructor/random/stack\n", uindex); ++fails; }
        if (e)
        {
            cmpd(82, uindex, "use x", strtod(exs, NULL), e->e.pos_x, &fails);
            cmpd(82, uindex, "use y", strtod(eys, NULL), e->e.pos_y, &fails);
            cmpd(82, uindex, "use z", strtod(ezs, NULL), e->e.pos_z, &fails);
            cmpd(82, uindex, "target x", strtod(txs, NULL), e->eye_target_x, &fails);
            cmpd(82, uindex, "target y", strtod(tys, NULL), e->eye_target_y, &fails);
            cmpd(82, uindex, "target z", strtod(tzs, NULL), e->eye_target_z, &fails);
        }
    }
    fclose(uf);
    if (use_det.next_id[DET_OTHER] != use_end.next_id[DET_OTHER] ||
        det_seeder_state(&use_det, DET_OTHER) != use_end.seeder[DET_OTHER] ||
        det_math_state(&use_det, DET_OTHER) != use_end.math[DET_OTHER])
    { printf("FAIL eye item use Det streams\n"); ++fails; }
    for (int j = 0; j < use_end.nsplits; ++j)
    {
        const det_split *sp = det_split_find(&use_det, use_end.names[j]);
        if (!sp || det_rng_state(&sp->d[DET_OTHER]) != use_end.state[j][DET_OTHER] ||
            sp->used[DET_OTHER] != use_end.used[j][DET_OTHER])
        { printf("FAIL eye item use split random %s want=%llx got=%llx used=%u/%u\n",
                 use_end.names[j], (unsigned long long)use_end.state[j][DET_OTHER],
                 (unsigned long long)(sp ? det_rng_state(&sp->d[DET_OTHER]) : 0),
                 (unsigned)use_end.used[j][DET_OTHER], (unsigned)(sp ? sp->used[DET_OTHER] : 0));
          ++fails; }
    }
    ie_free(&use_world);
    det_free(&use_det);
    if (!fails) printf("PASS %s (%d eyes, %d drops, 82 ticks, 2 item throws)\n", argv[1], eyes, drops);
    else printf("FAIL %s: %d differences\n", argv[1], fails);
    ie_free(&iew);
    world_free(&world);
    det_free(&det);
    return fails ? 1 : 0;
}
