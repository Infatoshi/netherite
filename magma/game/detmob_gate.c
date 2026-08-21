/* detmob_gate.c — live magma vs tape pose compare with det_entity_rng=1.
 * Not wired into `make test`. See verify/trace/detmob_gate.py. */
#include "game/runtime.h"
#include "core/config.h"
#include "mc_rng.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DET_MAX 64

typedef struct {
    int eid, type, lst, age, tt, tasks, watch, idle, eat, egg, og, bht;
    double x, y, z, ix, iz;
    float yaw, pitch, hyaw, ryaw, bhp;
    unsigned long long seed48;
} DetEnt;

static int parse_type(const char *s) {
    if (!strcmp(s, "cow") || !strcmp(s, "EntityCow")) return EW_TYPE_COW;
    if (!strcmp(s, "sheep") || !strcmp(s, "EntitySheep")) return EW_TYPE_SHEEP;
    if (!strcmp(s, "pig") || !strcmp(s, "EntityPig")) return EW_TYPE_PIG;
    if (!strcmp(s, "chicken") || !strcmp(s, "EntityChicken")) return EW_TYPE_CHICKEN;
    return -1;
}

static unsigned u32bits(float f) {
    unsigned u;
    memcpy(&u, &f, 4);
    return u;
}
static unsigned long long u64bits(double d) {
    unsigned long long u;
    memcpy(&u, &d, 8);
    return u;
}

static int load_fixture(const char *path, long long *seed, double *px, double *py, double *pz,
                        float *pyaw, float *ppitch, int *nticks, DetEnt *ents, int *nents) {
    FILE *f = fopen(path, "r");
    char line[1024];
    if (!f) { perror(path); return 0; }
    *nents = 0; *nticks = 0; *seed = 0;
    *px = 8.5; *py = 5.0; *pz = 8.5; *pyaw = 0; *ppitch = 0;
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "seed %lld", seed) == 1) continue;
        if (sscanf(line, "ticks %d", nticks) == 1) continue;
        if (sscanf(line, "player %lf %lf %lf %f %f", px, py, pz, pyaw, ppitch) == 5) continue;
        if (line[0] == 'n' && (line[1] == ' ' || line[1] == '\t')) continue;
        if (line[0] == 'e') {
            DetEnt e;
            char tname[32];
            int got;
            memset(&e, 0, sizeof e);
            got = sscanf(line,
                "e %d %31s %lf %lf %lf %f %f %f %llu %d %d %d %d %d %d %lf %lf %d %d %d %f %f %d",
                &e.eid, tname, &e.x, &e.y, &e.z, &e.yaw, &e.pitch, &e.hyaw, &e.seed48,
                &e.lst, &e.age, &e.tt, &e.tasks, &e.watch, &e.idle, &e.ix, &e.iz,
                &e.eat, &e.egg, &e.og, &e.ryaw, &e.bhp, &e.bht);
            e.type = parse_type(tname);
            if (got >= 9 && e.type >= 0 && *nents < DET_MAX) {
                if (got < 21) e.ryaw = e.hyaw;
                if (got < 22) e.bhp = e.hyaw;
                if (got < 23) e.bht = 0;
                ents[(*nents)++] = e;
            }
        }
    }
    fclose(f);
    return 1;
}

int main(int argc, char **argv) {
    DetEnt ents[DET_MAX];
    int nents = 0, nticks = 0, i, t;
    long long seed = 0;
    double px, py, pz;
    float pyaw, ppitch;
    GmRuntime r;
    GmConfig c;
    GmAction idle;
    char err[256];
    FILE *out;
    if (argc != 3) {
        fprintf(stderr, "usage: %s FIXTURE OUT.jsonl\n", argv[0]);
        return 2;
    }
    if (cr_cfg_set("det_entity_rng", "1") != 0) {
        fprintf(stderr, "detmob_gate: cannot set det_entity_rng\n");
        return 2;
    }
    if (!load_fixture(argv[1], &seed, &px, &py, &pz, &pyaw, &ppitch, &nticks, ents, &nents))
        return 2;
    if (nents <= 0) { fprintf(stderr, "detmob_gate: no entities in fixture\n"); return 2; }
    if (nticks <= 0) nticks = 1;
    gm_config_defaults(&c);
    c.world = GM_WORLD_SUPERFLAT;
    c.view_distance = 2;
    c.seed = seed;
    c.mobs = 1;
    if (!gm_runtime_init(&r, &c, err, sizeof err)) {
        fprintf(stderr, "init: %s\n", err);
        return 1;
    }
    gm_runtime_set_pose(&r, px, py, pz, pyaw, ppitch);
    gm_world_ensure(r.world, (int)floor(px) >> 4, (int)floor(pz) >> 4, 2);
    for (i = 0; i < nents; ++i) {
        int bx = (int)floor(ents[i].x), by = (int)floor(ents[i].y) - 1, bz = (int)floor(ents[i].z);
        int dx, dz;
        if (by < 1) by = 1;
        gm_world_ensure(r.world, bx >> 4, bz >> 4, 1);
        for (dx = -1; dx <= 1; ++dx)
            for (dz = -1; dz <= 1; ++dz)
                gm_world_set_block(r.world, bx + dx, by, bz + dz, 2);
        if (gm_mobs_det_place(&r.mobs, ents[i].eid, ents[i].type,
                              ents[i].x, ents[i].y, ents[i].z,
                              ents[i].yaw, ents[i].pitch, ents[i].hyaw,
                              ents[i].seed48, ents[i].lst, ents[i].age, ents[i].tt,
                              (unsigned)ents[i].tasks, ents[i].watch, ents[i].idle,
                              ents[i].ix, ents[i].iz, ents[i].eat, ents[i].egg,
                              ents[i].og, ents[i].ryaw, ents[i].bhp, ents[i].bht) < 0) {
            fprintf(stderr, "det_place failed eid=%d\n", ents[i].eid);
            gm_runtime_destroy(&r);
            return 1;
        }
    }
    out = fopen(argv[2], "w");
    if (!out) { perror(argv[2]); gm_runtime_destroy(&r); return 2; }
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    for (t = 0; t < nticks; ++t) {
        const EwStore *s;
        int slot;
        gm_runtime_set_pose(&r, px, py, pz, pyaw, ppitch);
        gm_runtime_tick(&r, idle);
        s = r.mobs.current ? &r.mobs.b : &r.mobs.a;
        for (slot = 1; slot < EW_MAX_ENTITIES; ++slot) {
            if (!s->alive[slot]) continue;
            if (s->type[slot] != EW_TYPE_COW && s->type[slot] != EW_TYPE_SHEEP &&
                s->type[slot] != EW_TYPE_PIG && s->type[slot] != EW_TYPE_CHICKEN) continue;
            fprintf(out,
                "{\"t\":%d,\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"yaw\":%.9g,\"pitch\":%.9g,\"hyaw\":%.9g,\"ryaw\":%.9g,\"bt\":%d,"
                "\"x_bits\":\"%016llx\",\"y_bits\":\"%016llx\",\"z_bits\":\"%016llx\","
                "\"yaw_bits\":\"%08x\",\"pitch_bits\":\"%08x\",\"hyaw_bits\":\"%08x\","
                "\"seed48\":%llu}\n",
                t, s->id[slot],
                s->x[slot], s->y[slot], s->z[slot],
                s->yaw[slot], r.mobs.passive_head_pitch[slot], r.mobs.passive_head_yaw[slot],
                r.mobs.passive_render_yaw[slot], r.mobs.passive_body_ticks[slot],
                u64bits(s->x[slot]), u64bits(s->y[slot]), u64bits(s->z[slot]),
                u32bits(s->yaw[slot]), u32bits(r.mobs.passive_head_pitch[slot]),
                u32bits(r.mobs.passive_head_yaw[slot]),
                (unsigned long long)r.mobs.ent_jr_seed[slot]);
        }
    }
    fclose(out);
    gm_runtime_destroy(&r);
    return 0;
}
