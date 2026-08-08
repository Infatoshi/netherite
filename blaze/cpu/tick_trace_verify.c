/* tick_trace_verify.c - REAL-GAME tick-trace verifier for the P1 block-tick families
 * (PORT_MATRIX P1). Loads a scenario JSONL captured from the live Java game
 * (verify/tick_trace/capture_scenarios.py) into a World, seeds the scheduled-tick queue
 * exactly as vanilla block placement (onBlockAdded) would, runs wt_vanilla_tick N times,
 * and compares the C cuboid against the Java cuboid at every dumped tick.
 *
 * VERIFICATION CONTRACT (per PORT_MATRIX P1 + the RNG caveat):
 *   - DETERMINISTIC families (fluid flow, falling): scheduled-tick driven, no world-rand
 *     in the flow rule -> EXACT per-tick cuboid match is the acceptance test. First
 *     divergent (tick, cell, expected, got) is reported.
 *   - RANDOM families (fire spread, grass, crops, ice/snow melt): the C world rand stream
 *     cannot align with the real game's draw history, so exact cells are impossible. We
 *     verify (a) TIMING: fire self-reschedule cadence 30 + rand(10) in [30,39]; (b) EVENT
 *     LEGALITY: every transition observed in the Java trace is a legal transition under
 *     the C block rules, and every transition C produces is legal / of an observed kind.
 *
 * Not a CPU==CUDA oracle kernel (reads external JSONL at runtime); built CPU-only like the
 * GAMERULES self-test. Usage: tick_trace_verify <scenario.jsonl> [<scenario.jsonl> ...].
 * Exit 0 iff every scenario meets its family's criterion. */
#define MC_WORLD_R 1                 /* 3x3 chunk region (48x48): fits every scenario cuboid */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../core/world_tick_vanilla.h"

#define TT_RBASE 16                  /* region origin of the cuboid (chunk (1,1)) */
#define TT_MAXVOL 4096               /* max cuboid cells (largest scenario is tiny) */

typedef struct {
    char name[32], family[16];
    int ox, oy, oz, nx, ny, nz, ticks;
    int doFireTick, randomTickSpeed, skylight, blocklight;
    int nframes;                     /* ticks+1 */
    u16 *frames;                     /* [nframes][vol], Java-captured */
    int *toff;                       /* [nframes] server-tick offset from frame 0 */
} Scenario;

/* ---- minimal JSONL parsing (integers only; the capture format is regular) ---- */

static int find_int(const char *line, const char *key, int dflt) {
    const char *p = strstr(line, key);
    if (!p) return dflt;
    p += strlen(key);
    while (*p && (*p == '"' || *p == ':' || *p == ' ')) ++p;
    return (int)strtol(p, NULL, 10);
}
static void find_str(const char *line, const char *key, char *out, int cap) {
    const char *p = strstr(line, key);
    int i = 0;
    out[0] = 0;
    if (!p) return;
    p += strlen(key);
    while (*p && *p != '"') ++p;      /* opening quote */
    if (*p == '"') ++p;
    while (*p && *p != '"' && i < cap - 1) out[i++] = *p++;
    out[i] = 0;
}

/* Parse "blocks":[a,b,c,...] into out[0..vol). Returns count parsed. */
static int parse_blocks(const char *line, u16 *out, int vol) {
    const char *p = strstr(line, "\"blocks\"");
    int n = 0;
    if (!p) return -1;
    p = strchr(p, '[');
    if (!p) return -1;
    ++p;
    while (*p && *p != ']' && n < vol) {
        while (*p == ' ' || *p == ',') ++p;
        if (*p == ']' || !*p) break;
        out[n++] = (u16)strtol(p, (char **)&p, 10);
    }
    return n;
}

static int load_scenario(const char *path, Scenario *s) {
    FILE *f = fopen(path, "r");
    char *line = NULL;
    size_t cap = 0;
    ssize_t len;
    int vol, ti = 0;
    if (!f) { fprintf(stderr, "cannot open %s\n", path); return -1; }
    if ((len = getline(&line, &cap, f)) <= 0) { fclose(f); return -1; }
    find_str(line, "\"name\"", s->name, sizeof s->name);
    find_str(line, "\"family\"", s->family, sizeof s->family);
    s->ox = find_int(line, "\"ox\"", 0);
    s->oy = find_int(line, "\"oy\"", 100);
    s->oz = find_int(line, "\"oz\"", 0);
    s->nx = find_int(line, "\"nx\"", 0);
    s->ny = find_int(line, "\"ny\"", 0);
    s->nz = find_int(line, "\"nz\"", 0);
    s->ticks = find_int(line, "\"ticks\"", 0);
    s->doFireTick = find_int(line, "\"doFireTick\"", 0);
    s->randomTickSpeed = find_int(line, "\"randomTickSpeed\"", 0);
    s->skylight = find_int(line, "\"skylight\"", 15);
    s->blocklight = find_int(line, "\"blocklight\"", 0);
    vol = s->nx * s->ny * s->nz;
    if (vol <= 0 || vol > TT_MAXVOL) { fprintf(stderr, "bad vol %d\n", vol); fclose(f); return -1; }
    s->nframes = s->ticks + 1;
    s->frames = (u16 *)malloc((size_t)s->nframes * vol * sizeof(u16));
    s->toff = (int *)malloc((size_t)s->nframes * sizeof(int));
    while ((len = getline(&line, &cap, f)) > 0) {
        int got;
        if (ti >= s->nframes) break;
        s->toff[ti] = find_int(line, "\"t\"", ti);
        got = parse_blocks(line, s->frames + (size_t)ti * vol, vol);
        if (got != vol) { fprintf(stderr, "%s: frame %d parsed %d != %d\n", path, ti, got, vol); fclose(f); return -1; }
        ++ti;
    }
    free(line);
    fclose(f);
    s->nframes = ti;                 /* actual frames captured */
    return vol;
}

/* Java cuboid index (getblocks order: y-major, then z, then x). */
static int jidx(const Scenario *s, int lx, int ly, int lz) {
    return (ly * s->nz + lz) * s->nx + lx;
}

/* ---- world setup: load frame 0 + uniform light, seed the queue like placement ---- */

static void world_load(World *w, WtvState *st, const Scenario *s, const McGameRules *gr) {
    int lx, ly, lz, ci;
    (void)gr;
    /* clear region to air, uniform light + plains biome */
    for (ci = 0; ci < MC_WORLD_CHUNKS; ++ci) {
        Chunk *c = &w->chunk[ci];
        int i;
        c->cx = ci % MC_WORLD_DIM; c->cz = ci / MC_WORLD_DIM;
        for (i = 0; i < MC_CHUNK_VOL; ++i) {
            c->blocks[i] = mc_state(BLK_AIR, 0);
            c->light[i] = mc_light(s->skylight, s->blocklight);
        }
        for (i = 0; i < MC_COL_AREA; ++i) c->biome[i] = 1;   /* plains: no freeze */
    }
    /* load frame 0 into the region at (TT_RBASE + lx, oy + ly, TT_RBASE + lz) */
    for (ly = 0; ly < s->ny; ++ly)
        for (lz = 0; lz < s->nz; ++lz)
            for (lx = 0; lx < s->nx; ++lx) {
                u16 v = s->frames[jidx(s, lx, ly, lz)];
                wt_set(w, TT_RBASE + lx, s->oy + ly, TT_RBASE + lz, v);
            }
    /* seed the scheduled queue: onBlockAdded equivalents for placed flowing liquid / fire.
     * flowing water schedules @tickRate 5, flowing lava @30 (deterministic - no rand);
     * fire @30 + rand(10) (world rand; cannot match Java's value, cadence is what matters).
     * Frame 0's "t" label (toff[0]) is its offset from the PLACEMENT tick (onBlockAdded
     * ran toff[0] ticks before the frame-0 dump), so seeded delays shrink by toff[0]. */
    for (ly = 0; ly < s->ny; ++ly)
        for (lz = 0; lz < s->nz; ++lz)
            for (lx = 0; lx < s->nx; ++lx) {
                int rx = TT_RBASE + lx, ry = s->oy + ly, rz = TT_RBASE + lz;
                int id = mc_state_id(wt_get(w, rx, ry, rz));
                int d0 = s->toff[0];
                if (id == BLK_FLOWING_WATER)
                    stq_update_block_tick(&st->stq, rx, ry, rz, id,
                                          (5 - d0 > 0) ? 5 - d0 : 0, 0, st->totalWorldTime, 0, 1);
                else if (id == BLK_FLOWING_LAVA)
                    stq_update_block_tick(&st->stq, rx, ry, rz, id,
                                          (30 - d0 > 0) ? 30 - d0 : 0, 0, st->totalWorldTime, 0, 1);
                else if (id == BLK_SAND || id == BLK_GRAVEL)
                    stq_update_block_tick(&st->stq, rx, ry, rz, id,
                                          (2 - d0 > 0) ? 2 - d0 : 0, 0, st->totalWorldTime, 0, 1);
                else if (id == WT_BLK_FIRE)
                    wt_fire_on_block_added(w, st, rx, ry, rz);
            }
}

/* ---- event-legality predicate for the random families ---- */

static int is_fuel(int id) { return wt_fire_flammability(id) > 0; }

static int legal_transition(const char *fam, u16 from, u16 to) {
    int fi = mc_state_id(from), ti = mc_state_id(to);
    int fm = mc_state_meta(from), tm = mc_state_meta(to);
    if (from == to) return 1;
    if (!strcmp(fam, "fire")) {
        /* fire appears on air/fuel; fire or fuel burns to air; fuel -> fire;
         * fire age bumps in place (updateTick: age + rand(3)/2, monotone up) */
        if (ti == WT_BLK_FIRE && (fi == BLK_AIR || is_fuel(fi))) return 1;
        if (ti == BLK_AIR && (fi == WT_BLK_FIRE || is_fuel(fi))) return 1;
        if (fi == WT_BLK_FIRE && ti == WT_BLK_FIRE && tm >= fm) return 1;
        return 0;
    }
    if (!strcmp(fam, "grass")) {
        if (fi == BLK_DIRT && ti == BLK_GRASS) return 1;   /* spread */
        if (fi == BLK_GRASS && ti == BLK_DIRT) return 1;   /* decay */
        return 0;
    }
    if (!strcmp(fam, "crops")) {
        if (fi == BTC_BLK_WHEAT && ti == BTC_BLK_WHEAT && tm >= fm) return 1;  /* growth */
        if (fi == BTC_BLK_WHEAT && ti == BLK_AIR) return 1;                    /* uprooted */
        /* farmland moisture dry-out (60 meta 7->0) and farmland->dirt are legal vanilla
         * transitions; the wt driver does not yet random-tick farmland (documented gap),
         * so they appear only in the Java trace - legal, not an illegality. */
        if (fi == BTC_BLK_FARMLAND && ti == BTC_BLK_FARMLAND && tm <= fm) return 1;
        if (fi == BTC_BLK_FARMLAND && ti == BLK_DIRT) return 1;
        return 0;
    }
    if (!strcmp(fam, "melt")) {
        if (fi == BLK_ICE && (ti == BLK_WATER || ti == BLK_FLOWING_WATER)) return 1;
        if (fi == BLK_SNOW_LAYER && ti == BLK_AIR) return 1;
        /* melt aftermath: BlockIce.turnIntoWater leaves FLOWING water that settles to
         * static (placeStaticBlock), and flag-3 notifications reawaken static water
         * (BlockStaticLiquid.updateLiquid) - same-meta 8<->9 flips are legal. */
        if (fi == BLK_FLOWING_WATER && ti == BLK_WATER && tm == fm) return 1;
        if (fi == BLK_WATER && ti == BLK_FLOWING_WATER && tm == fm) return 1;
        return 0;
    }
    return 0;
}

/* count illegal transitions between two frames of a trace */
static int illegal_between(const char *fam, const u16 *a, const u16 *b, int vol) {
    int i, n = 0;
    for (i = 0; i < vol; ++i)
        if (!legal_transition(fam, a[i], b[i])) ++n;
    return n;
}

/* ---- fire reschedule cadence from the dispatch log ---- */

static int fire_cadence_ok(const WtvState *st, int *out_min, int *out_max) {
    int i, j, mn = 1 << 30, mx = -1, ok = 1;
    for (i = 0; i < st->fired_n; ++i) {
        if (st->fired[i].block != WT_BLK_FIRE) continue;
        for (j = i + 1; j < st->fired_n; ++j) {
            if (st->fired[j].block != WT_BLK_FIRE) continue;
            if (st->fired[j].x == st->fired[i].x && st->fired[j].y == st->fired[i].y &&
                st->fired[j].z == st->fired[i].z) {
                int d = (int)(st->fired_at[j] - st->fired_at[i]);
                if (d < mn) mn = d;
                if (d > mx) mx = d;
                if (d < 30 || d > 39) ok = 0;
                break;                               /* next reschedule of this cell */
            }
        }
    }
    *out_min = (mx < 0) ? 0 : mn;
    *out_max = (mx < 0) ? 0 : mx;
    return ok;
}

/* ---- run one scenario ---- */

static int run_scenario(const char *path, McSinTable *tbl) {
    Scenario s;
    World *w = (World *)malloc(sizeof(World));
    WtvState *st = (WtvState *)malloc(sizeof(WtvState));
    McGameRules gr = mc_gamerules_default();
    int vol = load_scenario(path, &s);
    int t, deterministic, pass = 1;
    int first_tick = -1, first_cell = -1, first_exp = 0, first_got = 0;
    int j_illegal = 0, c_illegal = 0;
    u16 cbuf[TT_MAXVOL], cprev[TT_MAXVOL];
    if (vol < 0) { free(w); free(st); return 2; }

    gr.doFireTick = s.doFireTick;
    gr.randomTickSpeed = s.randomTickSpeed;
    gr.doDaylightCycle = 0;                           /* time frozen in the capture */
    gr.doMobSpawning = 0;
    deterministic = (!strcmp(s.family, "fluid") || !strcmp(s.family, "falling"));

    wt_vanilla_init(st, (u64)s.oy * 1000003ULL + 12345ULL);   /* any seed: RNG can't align anyway */
    world_load(w, st, &s, &gr);

    /* snapshot C frame 0 */
    for (t = 0; t < vol; ++t) {
        int lx, ly, lz;
        lz = (t / s.nx) % s.nz; ly = t / (s.nx * s.nz); lx = t % s.nx;
        cprev[t] = wt_get(w, TT_RBASE + lx, s.oy + ly, TT_RBASE + lz);
    }

    for (t = 1; t < s.nframes; ++t) {
        int lx, ly, lz, i, d;
        const u16 *jframe = s.frames + (size_t)t * vol;
        int ndelta = s.toff[t] - s.toff[t - 1];
        if (ndelta < 1) ndelta = 1;
        for (d = 0; d < ndelta; ++d) {
            wt_vanilla_tick(w, st, &gr, tbl, (MswScene *)0);
            wt_vanilla_update_entities(w, st);
        }
        for (ly = 0; ly < s.ny; ++ly)
            for (lz = 0; lz < s.nz; ++lz)
                for (lx = 0; lx < s.nx; ++lx) {
                    int idx = jidx(&s, lx, ly, lz);
                    cbuf[idx] = wt_get(w, TT_RBASE + lx, s.oy + ly, TT_RBASE + lz);
                }
        if (deterministic) {
            for (i = 0; i < vol; ++i)
                if (cbuf[i] != jframe[i]) {
                    if (first_tick < 0) {
                        first_tick = s.toff[t]; first_cell = i;
                        first_exp = jframe[i]; first_got = cbuf[i];
                    }
                    pass = 0;
                    break;
                }
        } else {
            j_illegal += illegal_between(s.family, s.frames + (size_t)(t - 1) * vol, jframe, vol);
            c_illegal += illegal_between(s.family, cprev, cbuf, vol);
        }
        memcpy(cprev, cbuf, vol * sizeof(u16));
    }

    /* verdict */
    printf("=== %-8s [%s]  %dx%dx%d  %d ticks  (rts=%d fire=%d) ===\n",
           s.name, s.family, s.nx, s.ny, s.nz, s.ticks, s.randomTickSpeed, s.doFireTick);
    if (deterministic) {
        if (pass) {
            printf("  DETERMINISTIC EXACT: C == Java for all %d frames (through tick %d)\n",
                   s.nframes, s.toff[s.nframes - 1]);
        } else {
            int fx = first_cell % s.nx, fy = first_cell / (s.nx * s.nz);
            int fz = (first_cell / s.nx) % s.nz;
            printf("  DIVERGENCE at tick %d cell (lx=%d,ly=%d,lz=%d): "
                   "Java id=%d meta=%d  vs  C id=%d meta=%d\n",
                   first_tick, fx, fy, fz,
                   mc_state_id(first_exp), mc_state_meta(first_exp),
                   mc_state_id(first_got), mc_state_meta(first_got));
        }
    } else {
        printf("  Java illegal transitions: %d ; C illegal transitions: %d (0 = every "
               "observed transition legal under the C rules)\n", j_illegal, c_illegal);
        if (!strcmp(s.family, "fire")) {
            int mn, mx, ok = fire_cadence_ok(st, &mn, &mx);
            printf("  fire reschedule cadence: [%d,%d] ticks (contract 30..39) -> %s\n",
                   mn, mx, ok ? "OK" : "OUT OF BOUNDS");
            if (!ok) pass = 0;
        }
        if (j_illegal || c_illegal) pass = 0;
    }
    printf("  VERDICT: %s\n", pass ? "PASS" : "FAIL");

    free(s.frames); free(s.toff); free(w); free(st);
    return pass ? 0 : 1;
}

int main(int argc, char **argv) {
    McSinTable *tbl = (McSinTable *)malloc(sizeof(McSinTable));
    int i, rc = 0;
    if (argc < 2) {
        fprintf(stderr, "usage: %s <scenario.jsonl> [...]\n", argv[0]);
        free(tbl);
        return 2;
    }
    mc_sin_table_init(tbl);
    for (i = 1; i < argc; ++i) {
        int r = run_scenario(argv[i], tbl);
        if (r > rc) rc = r;
    }
    free(tbl);
    return rc;
}
