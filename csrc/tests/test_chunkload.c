/* Gate: native chunk load, populate and unload order against the oracle's
 * ChunkLoadProbe recordings (out/java/chunkload/<name>/; recorded with
 *   make -C oracle script SCRIPT=tests/walkfar.jsonl TAPE=... \
 *     ARGS="--chunklog /abs/out/java/chunkload/<name>"
 * on the lane). The replay:
 *
 *   world_init(seed); cl_init(world, spawnX, spawnZ)
 *   tick 0: cl_replay_load of the spawn search's row, then cl_spawn_area
 *   the login at the first tape row with an sp (addPlayer, radius 10)
 *   every tick t >= 1, in WorldServer.tick's order:
 *     the mobspawn rows fed back, cl_unload_step(t), the tickPending rows,
 *     the tickBlocks rows, the village and portal rows, the entities rows,
 *     the c03 of tick t (the position tape row t - 1 carries, the packet tick
 *     t processes), cl_save_marks at t % 900 == 0, cl_view_radius(8) at tick 1
 *   the feed gives every populate call the loads the recording says it made
 *
 * The engine's event stream must equal events.jsonl row for row; the first
 * difference names the tick and the event. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../engine/chunkload.h"
#include "../engine/tape.h"

/* ------------------------------------------------------------- tiny json */

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");

    if (!f) return 0;

    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc((size_t)n + 1);
    size_t r = fread(b, 1, (size_t)n, f);
    b[r] = 0;
    fclose(f);
    return b;
}

static long long jnum(const char *json, const char *key)
{
    char pat[64];
    snprintf(pat, sizeof pat, "\"%s\":", key);
    const char *p = strstr(json, pat);

    if (!p) return -1;

    return strtoll(p + strlen(pat), 0, 10);
}

/* A double out of a tape row's sp object. */
static double jdbl(const struct jval *sp, const char *key)
{
    const struct jval *v = json_get(sp, key);
    return v && v->kind == J_NUM ? v->dbl : 0;
}

/* ------------------------------------------------------------- the driver */

struct rec
{
    int t, cx, cz, pop;
    char kind[8];
    char site[24];
};

static int rec_next(FILE *f, struct rec *r)
{
    char line[512];

    if (!fgets(line, sizeof line, f)) return 0;

    memset(r, 0, sizeof *r);
    r->pop = -1;

    char *p = strstr(line, "\"t\":");
    if (p) r->t = atoi(p + 4);
    p = strstr(line, "\"k\":\"");
    if (p) { p += 5; char *q = strchr(p, '"'); size_t n = q ? (size_t)(q - p) : strlen(p); if (n > 7) n = 7; memcpy(r->kind, p, n); }
    p = strstr(line, "\"cx\":");
    if (p) r->cx = atoi(p + 5);
    p = strstr(line, "\"cz\":");
    if (p) r->cz = atoi(p + 5);
    p = strstr(line, "\"pop\":");
    if (p) r->pop = atoi(p + 6);
    p = strstr(line, "\"site\":\"");
    if (p) { p += 8; char *q = strchr(p, '"'); size_t n = q ? (size_t)(q - p) : strlen(p); if (n > 23) n = 23; memcpy(r->site, p, n); }
    return 1;
}

#define MAXREC (1 << 17)

static struct rec recs[MAXREC];
static int nrecs;

#define EVCAP 8192

static struct cl_event evbuf[EVCAP];
static int evn;
static int fails, events;

static void take(void *ctx, const struct cl_event *e)
{
    (void)ctx;

    if (evn < EVCAP) evbuf[evn++] = *e;
}

/* The recording's cursor and the first difference. */
static int ri;
static int cmp_on = 1;

/* Drain the buffer against the recording. */
static void drain(void)
{
    for (int i = 0; i < evn; ++i)
    {
        const struct cl_event *e = &evbuf[i];

        if (ri >= nrecs)
        {
            if (cmp_on)
            {
                cmp_on = 0;
                ++fails;
                printf("FIRST DIFF: the engine produced t=%d %c (%d,%d) with no recording row left\n",
                       e->t, e->kind, e->cx, e->cz);
            }

            continue;
        }

        const struct rec *r = &recs[ri];

        if (r->t != e->t || r->kind[0] != e->kind || r->cx != e->cx || r->cz != e->cz)
        {
            if (cmp_on)
            {
                cmp_on = 0;
                ++fails;
                printf("FIRST DIFF at recording row %d: want t=%d %s (%d,%d), engine t=%d %c (%d,%d)\n",
                       ri, r->t, r->kind, r->cx, r->cz, e->t, e->kind, e->cx, e->cz);
            }

            continue;
        }

        ++events;
        ++ri;
    }

    evn = 0;
}

/* Feed: the loads the recording says populate call pop_index made, in row
 * order. The pop ids are call order on both sides, so the index is the id. */
static const struct cl_load *feed_pop_loads(void *ctx, int pop_index, int *n)
{
    static struct cl_load out[8192];
    int k = 0;

    (void)ctx;

    for (int i = 0; i < nrecs && k < 8192; ++i)
    {
        if (recs[i].kind[0] == 'l' && recs[i].pop == pop_index)
        {
            out[k].cx = recs[i].cx;
            out[k].cz = recs[i].cz;
            out[k].site = recs[i].site;
            ++k;
        }
    }

    *n = k;
    return out;
}

/* The rows of tick t with kind load, site in the list, pop -1: the top-level
 * loads the engine has no rule for, fed back at their phase point. */
static void inject_site(struct cl *cl, int t, const char *const *sites, int nsites)
{
    for (int i = 0; i < nrecs; ++i)
    {
        if (recs[i].t != t || recs[i].kind[0] != 'l' || recs[i].pop != -1) continue;

        int hit = 0;

        for (int s = 0; s < nsites; ++s)
            if (!strcmp(recs[i].site, sites[s])) hit = 1;

        if (!hit) continue;

        cl_replay_load(cl, t, recs[i].cx, recs[i].cz, recs[i].site);
    }
}

static void run_dir(const char *dir)
{
    char path[1100];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    char *manifest = slurp(path);

    if (!manifest) { printf("FAIL %s: no manifest\n", dir); ++fails; return; }

    int64_t seed = jnum(manifest, "seed");
    int spawn_x = (int)jnum(manifest, "spawnX");
    int spawn_z = (int)jnum(manifest, "spawnZ");

    snprintf(path, sizeof path, "%s/events.jsonl", dir);
    FILE *ev = fopen(path, "rb");

    if (!ev) { printf("FAIL %s: no events.jsonl\n", dir); ++fails; return; }

    while (nrecs < MAXREC && rec_next(ev, &recs[nrecs])) ++nrecs;
    fclose(ev);

    snprintf(path, sizeof path, "%s/tape.jsonl", dir);
    struct tape tp;

    if (tape_open(&tp, path) != 1) { printf("FAIL %s: no tape.jsonl\n", dir); ++fails; return; }

    struct world w;
    world_init(&w, seed);
    struct cl *cl = cl_init(&w, spawn_x, spawn_z);
    cl_on_event(cl, take, 0);

    struct cl_feed feed = {feed_pop_loads, 0};
    cl_set_feed(cl, &feed);

    /* the login position: the first tape row with an sp */
    double px = 0, pz = 0;
    int tape_t = -1, has_login = 0;
    const struct jval *row = 0;

    while (tape_next(&tp, &row))
    {
        ++tape_t;

        if (json_get(row, "sp"))
        {
            px = jdbl(json_get(row, "sp"), "x");
            pz = jdbl(json_get(row, "sp"), "z");
            break;
        }
    }

    has_login = tape_t >= 0;

    int horizon = nrecs ? recs[nrecs - 1].t : 0;
    int spawn_started = 0, radius_done = 0;

    static const char *MOB[] = {"mobspawn"};
    static const char *PENDING[] = {"tickPending"};
    static const char *BLOCKS[] = {"tickBlocks"};
    static const char *WORLD[] = {"village", "portal"};
    static const char *UPDATE[] = {"entities", "tileentity"};
    static const char *NETWORK[] = {"network"};

    /* tick 0: the spawn search's row, then the spawn loop */
    for (int i = 0; i < nrecs && recs[i].t == 0; ++i)
    {
        if (recs[i].kind[0] != 'l') continue;

        if (!strcmp(recs[i].site, "spawnsearch"))
        {
            cl_replay_load(cl, 0, recs[i].cx, recs[i].cz, recs[i].site);
        }
        else if (!strcmp(recs[i].site, "spawn") && !spawn_started)
        {
            spawn_started = 1;
            cl_spawn_area(cl, 0);
        }
    }

    /* the login, after the spawn area like the real run: addPlayer's square
     * loads nothing when the spawn area covered it */
    if (has_login) cl_login(cl, 0, px, pz);

    for (int t = 1; t <= horizon; ++t)
    {
        /* tape row t - 1: the position the client sent during tick t - 1,
         * which tick t's networkTick processes */
        while (tape_t < t - 1 && tape_next(&tp, &row))
        {
            ++tape_t;

            if (json_get(row, "sp"))
            {
                px = jdbl(json_get(row, "sp"), "x");
                pz = jdbl(json_get(row, "sp"), "z");
            }
        }

        inject_site(cl, t, MOB, 1);
        cl_unload_step(cl, t);
        inject_site(cl, t, PENDING, 1);
        inject_site(cl, t, BLOCKS, 1);
        inject_site(cl, t, WORLD, 2);
        inject_site(cl, t, UPDATE, 2);

        if (has_login && tape_t == t - 1) cl_c03(cl, t, px, pz);

        inject_site(cl, t, NETWORK, 1);

        if (t % 900 == 0) cl_save_marks(cl, t);

        /* the integrated server's first-tail view distance change, after
         * everything else of tick 1 */
        if (t == 1 && has_login && !radius_done)
        {
            radius_done = 1;
            cl_view_radius(cl, 1, 8);
        }

        drain();
    }

    drain();
    tape_close(&tp);

    if (ri < nrecs && cmp_on)
    {
        cmp_on = 0;
        ++fails;
        printf("FIRST DIFF: the engine did not produce recording row %d: t=%d %s (%d,%d)\n",
               ri, recs[ri].t, recs[ri].kind, recs[ri].cx, recs[ri].cz);
    }

    long counts[5];
    cl_counts(cl, counts);

    printf("%s %s: %d recording rows, %d matched, loads %ld, pops %ld, marks %ld, unmarks %ld, unloads %ld\n",
           fails ? "FAIL" : "PASS", dir, nrecs, events, counts[0], counts[1], counts[2], counts[3], counts[4]);

    cl_free(cl);
    world_free(&w);
    free(manifest);
}

int main(int argc, char **argv)
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: test_chunkload CHUNKLOAD_DIR\n");
        return 2;
    }

    run_dir(argv[1]);
    return fails != 0;
}