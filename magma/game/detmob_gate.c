/* detmob_gate.c — live magma vs tape pose compare with det_entity_rng=1.
 * Not wired into `make test`. See verify/trace/detmob_gate.py.
 *
 * World: seed-0 default overworld via gm_world_create_type / gm_world_ensure,
 * the same generator magma_game and tape replay use. No superflat, no planted
 * grass pads. Ground stencil (3x3x3 ids around floor(pos)) is asserted against
 * the tape header before any pose compare. */
#include "game/runtime.h"
#include "core/config.h"
#include "mc_rng.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DET_MAX 64
#define DET_G 27
#define DET_ATK_MAX 64

typedef struct {
    int eid, type, lst, age, tt, tasks, watch, idle, eat, egg, og, bht;
    int have_g, g_bx, g_by, g_bz, g[DET_G];
    int have_h, ttt, ttasks, tgt, fuse, mdelay, see, stime, atime, scw, sback, cstate;
    double x, y, z, ix, iz, hp;
    float yaw, pitch, hyaw, ryaw, bhp;
    unsigned long long seed48;
} DetEnt;

static int parse_type(const char *s) {
    if (!strcmp(s, "cow") || !strcmp(s, "EntityCow")) return EW_TYPE_COW;
    if (!strcmp(s, "sheep") || !strcmp(s, "EntitySheep")) return EW_TYPE_SHEEP;
    if (!strcmp(s, "pig") || !strcmp(s, "EntityPig")) return EW_TYPE_PIG;
    if (!strcmp(s, "chicken") || !strcmp(s, "EntityChicken")) return EW_TYPE_CHICKEN;
    if (!strcmp(s, "zombie") || !strcmp(s, "EntityZombie")) return EW_TYPE_ZOMBIE;
    if (!strcmp(s, "skeleton") || !strcmp(s, "EntitySkeleton")) return EW_TYPE_SKELETON;
    if (!strcmp(s, "creeper") || !strcmp(s, "EntityCreeper")) return EW_TYPE_CREEPER;
    return -1;
}

static int det_track(int type) {
    return type == EW_TYPE_COW || type == EW_TYPE_SHEEP ||
           type == EW_TYPE_PIG || type == EW_TYPE_CHICKEN ||
           type == EW_TYPE_ZOMBIE || type == EW_TYPE_SKELETON ||
           type == EW_TYPE_CREEPER;
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

static int floordiv16(int x) {
    if (x >= 0) return x / 16;
    return -(((-x) + 15) / 16);
}

static void stencil_ids(const GmWorld *w, int bx, int by, int bz, int *out) {
    int n = 0, dy, dz, dx;
    for (dy = -1; dy <= 1; ++dy)
        for (dz = -1; dz <= 1; ++dz)
            for (dx = -1; dx <= 1; ++dx)
                out[n++] = gm_world_block(w, bx + dx, by + dy, bz + dz);
}

static int load_fixture(const char *path, long long *seed, long long *wtime,
                        double *px, double *py, double *pz,
                        float *pyaw, float *ppitch, int *nticks, DetEnt *ents, int *nents,
                        int *atk, int *natk) {
    FILE *f = fopen(path, "r");
    char line[2048];
    if (!f) { perror(path); return 0; }
    *nents = 0; *nticks = 0; *seed = 0; *wtime = 6000; *natk = 0;
    *px = 8.5; *py = 5.0; *pz = 8.5; *pyaw = 0; *ppitch = 0;
    while (fgets(line, sizeof line, f)) {
        int t;
        if (line[0] == '#' || line[0] == '\n') continue;
        if (sscanf(line, "seed %lld", seed) == 1) continue;
        if (sscanf(line, "time %lld", wtime) == 1) continue;
        if (sscanf(line, "ticks %d", nticks) == 1) continue;
        if (sscanf(line, "player %lf %lf %lf %f %f", px, py, pz, pyaw, ppitch) == 5)
            continue;
        if (sscanf(line, "atk %d", &t) == 1) {
            if (*natk < DET_ATK_MAX) atk[(*natk)++] = t;
            continue;
        }
        if (line[0] == 'n' && (line[1] == ' ' || line[1] == '\t')) continue;
        if (line[0] == 'h' && (line[1] == ' ' || line[1] == '\t')) {
            int eid, ttt, ttasks, tgt, fuse, mdelay, see, stime, atime, scw, sback, cstate;
            int got, i;
            got = sscanf(line, "h %d %d %d %d %d %d %d %d %d %d %d %d",
                         &eid, &ttt, &ttasks, &tgt, &fuse, &mdelay,
                         &see, &stime, &atime, &scw, &sback, &cstate);
            if (got == 12) {
                for (i = 0; i < *nents; ++i) if (ents[i].eid == eid) {
                    ents[i].have_h = 1;
                    ents[i].ttt = ttt; ents[i].ttasks = ttasks; ents[i].tgt = tgt;
                    ents[i].fuse = fuse; ents[i].mdelay = mdelay;
                    ents[i].see = see; ents[i].stime = stime; ents[i].atime = atime;
                    ents[i].scw = scw; ents[i].sback = sback; ents[i].cstate = cstate;
                    break;
                }
            }
            continue;
        }
        if (line[0] == 'g' && (line[1] == ' ' || line[1] == '\t')) {
            int eid, bx, by, bz, ids[DET_G], got, i, k;
            got = sscanf(line,
                "g %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d %d "
                "%d %d %d %d %d %d %d %d %d %d %d",
                &eid, &bx, &by, &bz,
                &ids[0], &ids[1], &ids[2], &ids[3], &ids[4], &ids[5], &ids[6],
                &ids[7], &ids[8], &ids[9], &ids[10], &ids[11], &ids[12],
                &ids[13], &ids[14], &ids[15], &ids[16], &ids[17], &ids[18],
                &ids[19], &ids[20], &ids[21], &ids[22], &ids[23], &ids[24],
                &ids[25], &ids[26]);
            if (got == 4 + DET_G) {
                for (i = 0; i < *nents; ++i) if (ents[i].eid == eid) {
                    ents[i].have_g = 1;
                    ents[i].g_bx = bx; ents[i].g_by = by; ents[i].g_bz = bz;
                    for (k = 0; k < DET_G; ++k) ents[i].g[k] = ids[k];
                    break;
                }
            }
            continue;
        }
        if (line[0] == 'e') {
            DetEnt e;
            char tname[32];
            int got;
            memset(&e, 0, sizeof e);
            got = sscanf(line,
                "e %d %31s %lf %lf %lf %f %f %f %llu %d %d %d %d %d %d %lf %lf %d %d %d %f %f %d %lf",
                &e.eid, tname, &e.x, &e.y, &e.z, &e.yaw, &e.pitch, &e.hyaw, &e.seed48,
                &e.lst, &e.age, &e.tt, &e.tasks, &e.watch, &e.idle, &e.ix, &e.iz,
                &e.eat, &e.egg, &e.og, &e.ryaw, &e.bhp, &e.bht, &e.hp);
            e.type = parse_type(tname);
            if (got >= 9 && e.type >= 0 && *nents < DET_MAX) {
                if (got < 21) e.ryaw = e.hyaw;
                if (got < 22) e.bhp = e.hyaw;
                if (got < 23) e.bht = 0;
                if (got < 24) e.hp = 0;
                ents[(*nents)++] = e;
            }
        }
    }
    fclose(f);
    return 1;
}

int main(int argc, char **argv) {
    DetEnt ents[DET_MAX];
    int nents = 0, nticks = 0, i, t, natk = 0, atk[DET_ATK_MAX];
    long long seed = 0, wtime = 6000;
    double px, py, pz;
    float pyaw, ppitch;
    GmRuntime r;
    GmConfig c;
    GmAction idle;
    McGameRules gr;
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
    if (!load_fixture(argv[1], &seed, &wtime, &px, &py, &pz, &pyaw, &ppitch,
                      &nticks, ents, &nents, atk, &natk))
        return 2;
    if (nents <= 0) { fprintf(stderr, "detmob_gate: no entities in fixture\n"); return 2; }
    if (nticks <= 0) nticks = 1;
    gm_config_defaults(&c);
    c.world = GM_WORLD_DEFAULT;
    c.view_distance = 8;
    c.seed = seed;
    c.mobs = 1;
    c.daylight = 0;
    if (!gm_runtime_init(&r, &c, err, sizeof err)) {
        fprintf(stderr, "init: %s\n", err);
        return 1;
    }
    /* Tape replay: no live random ticks. Freeze the recorded clock. */
    r.randtick_enabled = 0;
    gr = r.gamerules;
    gr.doDaylightCycle = 0;
    gr.doWeatherCycle = 0;
    gr.doMobSpawning = 0;
    gr.randomTickSpeed = 0;
    gr.mobGriefing = 0;
    gm_runtime_set_gamerules(&r, &gr);
    gm_runtime_set_time(&r, wtime);
    gm_runtime_set_pose(&r, px, py, pz, pyaw, ppitch);
    gm_world_ensure(r.world, floordiv16((int)floor(px)),
                    floordiv16((int)floor(pz)), 4);
    for (i = 0; i < nents; ++i)
        gm_world_ensure(r.world, floordiv16((int)floor(ents[i].x)),
                        floordiv16((int)floor(ents[i].z)), 3);

    out = fopen(argv[2], "w");
    if (!out) { perror(argv[2]); gm_runtime_destroy(&r); return 2; }

    for (i = 0; i < nents; ++i) {
        int bx = ents[i].have_g ? ents[i].g_bx : (int)floor(ents[i].x);
        int by = ents[i].have_g ? ents[i].g_by : (int)floor(ents[i].y);
        int bz = ents[i].have_g ? ents[i].g_bz : (int)floor(ents[i].z);
        int mag[DET_G], k, slot;
        stencil_ids(r.world, bx, by, bz, mag);
        fprintf(out, "{\"ground\":1,\"eid\":%d,\"bx\":%d,\"by\":%d,\"bz\":%d,\"ids\":[",
                ents[i].eid, bx, by, bz);
        for (k = 0; k < DET_G; ++k) {
            if (k) fputc(',', out);
            fprintf(out, "%d", mag[k]);
        }
        fprintf(out, "]}\n");
        if (ents[i].have_g) {
            int mismatch = 0;
            for (k = 0; k < DET_G; ++k) if (ents[i].g[k] != mag[k]) mismatch = 1;
            if (mismatch) {
                fprintf(stderr,
                    "GROUND eid=%d tape_anchor=(%d,%d,%d) magma_anchor=(%d,%d,%d)\n",
                    ents[i].eid, ents[i].g_bx, ents[i].g_by, ents[i].g_bz, bx, by, bz);
                fprintf(stderr, "GROUND eid=%d tape_ids=", ents[i].eid);
                for (k = 0; k < DET_G; ++k) fprintf(stderr, "%s%d", k ? "," : "", ents[i].g[k]);
                fprintf(stderr, " magma_ids=");
                for (k = 0; k < DET_G; ++k) fprintf(stderr, "%s%d", k ? "," : "", mag[k]);
                fprintf(stderr, "\n");
                fclose(out);
                gm_runtime_destroy(&r);
                return 3;
            }
        }
        slot = gm_mobs_det_place(&r.mobs, ents[i].eid, ents[i].type,
                                 ents[i].x, ents[i].y, ents[i].z,
                                 ents[i].yaw, ents[i].pitch, ents[i].hyaw,
                                 ents[i].seed48, ents[i].lst, ents[i].age, ents[i].tt,
                                 (unsigned)ents[i].tasks, ents[i].watch, ents[i].idle,
                                 ents[i].ix, ents[i].iz, ents[i].eat, ents[i].egg,
                                 ents[i].og, ents[i].ryaw, ents[i].bhp, ents[i].bht);
        if (slot < 0) {
            fprintf(stderr, "det_place failed eid=%d\n", ents[i].eid);
            fclose(out);
            gm_runtime_destroy(&r);
            return 1;
        }
        if (ents[i].hp > 0) {
            r.mobs.a.health[slot] = (float)ents[i].hp;
            r.mobs.b.health[slot] = (float)ents[i].hp;
        }
        if (ents[i].have_h)
            gm_mobs_det_hydrate_hostile(&r.mobs, slot,
                                        ents[i].ttt, (unsigned)ents[i].ttasks,
                                        ents[i].tgt, ents[i].fuse, ents[i].mdelay,
                                        ents[i].see, ents[i].stime, ents[i].atime,
                                        ents[i].scw, ents[i].sback, ents[i].cstate);
    }

    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    for (t = 0; t < nticks; ++t) {
        const EwStore *s;
        int slot, a;
        idle.attack = 0;
        for (a = 0; a < natk; ++a) if (atk[a] == t) idle.attack = 1;
        gm_runtime_set_pose(&r, px, py, pz, pyaw, ppitch);
        gm_runtime_tick(&r, idle);
        s = r.mobs.current ? &r.mobs.b : &r.mobs.a;
        for (slot = 1; slot < EW_MAX_ENTITIES; ++slot) {
            if (!s->alive[slot]) continue;
            if (!det_track(s->type[slot])) continue;
            fprintf(out,
                "{\"t\":%d,\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                "\"yaw\":%.9g,\"pitch\":%.9g,\"hyaw\":%.9g,\"ryaw\":%.9g,\"bt\":%d,"
                "\"hp\":%.9g,\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
                "\"tasks\":%u,\"plen\":%u,\"ptx\":%.17g,\"pty\":%.17g,\"ptz\":%.17g,"
                "\"x_bits\":\"%016llx\",\"y_bits\":\"%016llx\",\"z_bits\":\"%016llx\","
                "\"yaw_bits\":\"%08x\",\"pitch_bits\":\"%08x\",\"hyaw_bits\":\"%08x\","
                "\"seed48\":%llu}\n",
                t, s->id[slot],
                s->x[slot], s->y[slot], s->z[slot],
                s->yaw[slot], r.mobs.passive_head_pitch[slot], r.mobs.passive_head_yaw[slot],
                r.mobs.passive_render_yaw[slot], r.mobs.passive_body_ticks[slot],
                (double)s->health[slot], s->vx[slot], s->vy[slot], s->vz[slot],
                r.mobs.passive_tasks[slot], s->path_len[slot],
                s->path_tx[slot], s->path_ty[slot], s->path_tz[slot],
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
