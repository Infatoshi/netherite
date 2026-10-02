/* Gate: the native snapshot loader and tape reader against the oracle's
 * snapshot and tape (oracle/harness/netherite/oracle/Snapshot.java; recorded with
 * `make -C oracle script SCRIPT=tests/snap_smoke.jsonl TAPE=../out/java/snapshots/NAME/tape.jsonl`).
 *
 * Checks, in order:
 *   1. the manifest is a snapshot and its counts match the files;
 *   2. every chunk loads into struct world and its bytes, its own FNV-1a hash
 *      and its 3x3 hash (the Probe layout) equal what the snapshot recorded;
 *   3. every tile entity's and entity's canonical NBT round-trips through
 *      nbt_parse and nbt_render (this is what makes the recorded text a lossless
 *      form of the tree);
 *   4. the six NBT files parse and render;
 *   5. the pending block ticks are in the world's order and counted;
 *   6. the tape next to the snapshot parses, its rows are contiguous in t,
 *      and each row after the snapshot is replayed against the native client,
 *      server, inventory, world, and entity fields.
 *
 * 2 to 5 are the load check and run only with --load-check (and then 6 does
 * not): they depend on the snapshot's files and the loading code alone, so
 * make test runs them as a job of their own per recording, which test
 * selection (tests/fnkey.c) replays from a passing record until either
 * changes, and every other run (the replay job, recs, diverge, segments)
 * spends nothing on them (lane/harnessspeed: fnv_chunk was 34% of a short
 * recording's replay).
 *
 * Fails on any load or round-trip mismatch, on a tape row that will not parse,
 * and on a row past the snapshot that is missing cp or sp.
 */
#define _XOPEN_SOURCE 700 /* POSIX 2008 plus XSI: realpath */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "../engine/entity_nbt.h"
#include "../engine/env.h"
#include "../engine/regioncache.h"
#include "../engine/dev.h"
#include "../engine/nbtbin.h"
#include "../engine/nbtjson.h"
#include "../engine/nbtedit.h"
#include "../engine/player.h"
#include "../engine/serverreplay.h"
#include "../engine/endfight.h"
#include "../engine/grave.h"
#include "../engine/combat.h"
#include "../engine/session.h"
#include "../engine/snapshot.h"
#include "../engine/sha1.h"
#include "../engine/statediff.h"
#include "../engine/envmem.h"
#include "../engine/genahead.h"
#include "../engine/lightcap.h"
#include "../engine/lightdefer.h"
#include "../engine/phase.h"
#include "../engine/tape.h"
#include "../engine/ticks.h"
#include "../engine/trace.h"
#include "../engine/chatcomp.h"
#include "../engine/cwrand.h"
#include "rowrec.h"
#include "cwdigest.h"

static _Thread_local int fails;

/* Called before each replayed row: test_interleave.c, which runs this file's
 * main for two recordings on two threads, hands the other its turn here. */
static void (*snapshots_row_hook)(void);

static void fail(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    printf("FAIL ");
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");
    ++fails;
}

#include "phasekernel.h"

/* The canonical text a tree renders back to: the round-trip check. */
static void round_trip(const char *text, const char *what)
{
    nbt *t = nbt_parse(text);
    if (!t)
    {
        fail("%s: canonical text does not parse", what);
        return;
    }

    char *again = nbt_render(t);

    if (strcmp(again, text))
    {
        char buf[512];
        nbt *b = nbt_parse(again);
        if (b && nbt_diff(t, b, buf, sizeof buf)) fail("%s: round-trip differs at %s", what, buf);
        else fail("%s: round-trip text differs", what);
        nbt_free(b);
    }

    free(again);
    nbt_free(t);
}

/* A value out of a canonical compound as bare text, for the summary lines: the
 * canonical quotes come off, and a "str:" prefix with them. A canonical scalar
 * carries its type ("l:200080"), so the value is what the caller wants. */
static const char *scalar(const nbt *comp, const char *key)
{
    static _Thread_local char ring[8][64];
    static _Thread_local int i;
    char *b = ring[i = (i + 1) % 8];
    const nbt *v = comp ? nbt_get(comp, key) : NULL;

    if (!v) { snprintf(b, sizeof ring[0], "-"); return b; }

    char *t = nbt_render(v);
    size_t n = strlen(t);
    const char *p = t;

    if (n >= 2 && t[0] == '"' && t[n - 1] == '"') { t[n - 1] = 0; p = t + 1; }
    if (!strncmp(p, "str:", 4)) p += 4;

    snprintf(b, sizeof ring[0], "%s", p);
    free(t);
    return b;
}

struct tally {
    char names[64][64];
    int count[64];
    int n;
};

static void count_class(struct tally *t, const char *cls)
{
    for (int i = 0; i < t->n; ++i)
        if (!strcmp(t->names[i], cls)) { ++t->count[i]; return; }

    if (t->n < 64)
    {
        snprintf(t->names[t->n], sizeof t->names[0], "%s", cls);
        t->count[t->n] = 1;
        ++t->n;
    }
}

static _Thread_local int g_row;

/* stats.jsonl beside the tape (Stats.java): one oracle dump of the
 * StatisticsFile and the client mirror per line, each compared before the
 * row at its tick (a dump after the last row compares at the end). */
struct stats_dumps {
    int n, next, checked;
    long long tick[64];
    char *line[64];
};

static void stats_dumps_load(struct stats_dumps *d, const char *dir)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/stats.jsonl", dir);
    memset(d, 0, sizeof *d);
    FILE *f = fopen(path, "r");
    if (!f) return;

    char *buf = NULL;
    size_t cap = 0;
    ssize_t len;

    while ((len = getline(&buf, &cap, f)) > 0 && d->n < 64)
    {
        while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;
        const char *tk = strstr(buf, "\"tick\":");
        if (!tk) continue;
        d->tick[d->n] = strtoll(tk + 7, NULL, 10);
        d->line[d->n] = strdup(buf);
        ++d->n;
    }

    free(buf);
    fclose(f);
}

/* The end-state snapshot's stats.json (end/, a script's last Snapshot with
 * "sub":"end"): one more dump, at the end manifest's tick. */
static void stats_dumps_load_end(struct stats_dumps *d, const char *dir)
{
    char path[4096];
    snprintf(path, sizeof path, "%s/end/stats.json", dir);
    FILE *f = fopen(path, "r");
    if (!f || d->n >= 64) { if (f) fclose(f); return; }

    char *buf = NULL;
    size_t cap = 0;
    ssize_t len = getline(&buf, &cap, f);
    fclose(f);
    if (len <= 0) { free(buf); return; }
    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) buf[--len] = 0;

    snprintf(path, sizeof path, "%s/end/manifest.json", dir);
    FILE *mf = fopen(path, "r");
    char *mbuf = NULL;
    size_t mcap = 0;
    long long tick = -1;
    if (mf && getline(&mbuf, &mcap, mf) > 0)
    {
        const char *tk = strstr(mbuf, "\"tick\":");
        if (tk) tick = strtoll(tk + 7, NULL, 10);
    }
    if (mf) fclose(mf);
    free(mbuf);
    if (tick < 0) { free(buf); return; }

    d->tick[d->n] = tick;
    d->line[d->n] = buf;
    ++d->n;
}

static void stats_dumps_check(struct stats_dumps *d, int64_t t, const struct server_player *sp,
                              const struct client_player *cp)
{
    while (d->next < d->n && d->tick[d->next] <= t)
    {
        char why[512];
        if (d->tick[d->next] == t)
        {
            if (surv_stats_compare(sp, cp, d->line[d->next], (int)t - 1 - sp->paused_ticks, why, sizeof why))
            {
                fail("stats at tick %lld: %s", (long long)t, why);
                printf("native stats:");
                for (int s = 0; s < STAT_COUNT; ++s)
                    if (STAT_BIT(sp->sv.stats.present, s))
                    {
                        char name[64];
                        printf(" %s=%d%s", surv_stat_name(s, name, sizeof name), sp->sv.stats.v[s],
                               STAT_BIT(sp->sv.stats.dirty, s) ? "*" : "");
                    }
                printf("\n");
            }
            ++d->checked;
        }
        ++d->next;
    }
}

static void stats_dumps_free(struct stats_dumps *d)
{
    for (int i = 0; i < d->n; ++i) free(d->line[i]);
    d->n = 0;
}

/* dev-give-plus-one: a transfer bug planted at the first dev-given pickup
 * (one more of the item in the server inventory), which the rows must catch */
static _Thread_local int give_extra, give_mutated;

static void plant_give_extra(void *ctx)
{
    struct session *ss = ctx;
    if (!give_extra || give_mutated || !ss->sr) return;
    struct server_player *sp = &ss->sp;
    for (int i = 0; i < ss->sr->d->iew.n && !give_mutated; ++i)
    {
        const ie_ent *item = ie_ent_at(ss->sr->d->iew.slot[i]);
        if (!item->owner_name || !item->is_dead || item->stack_count != 0) continue;
        for (int slot = 0; slot < 36; ++slot)
            if (sp->sv.inv[slot].count > 0 && sp->sv.inv[slot].item == item->stack_item)
            {
                ++sp->sv.inv[slot].count; /* planted transfer bug at the pickup */
                give_mutated = 1;
                break;
            }
    }
}

/* Whole-server rows carry the inventory explicitly, including []. Compare all
 * 40 slots there; older movement rows only check slots they recorded. */
static int inventory_diff(const struct jval *player, const struct surv_stack *got, int exact,
                          int *slot, char *want, size_t want_size, char *have, size_t have_size)
{
    int64_t items[40], counts[40] = {0}, damages[40] = {0};
    for (int i = 0; i < 40; ++i) items[i] = -1;

    const struct jval *inv = json_get(player, "inv");
    if (!exact && !inv) return 0;
    for (int i = 0; i < json_len(inv); ++i)
    {
        const struct jval *entry = json_at(inv, i);
        int64_t s = -1;
        if (!json_int(json_at(entry, 0), &s) || s < 0 || s >= 40 || items[s] >= 0)
        {
            *slot = (int)s;
            snprintf(want, want_size, "valid slot");
            snprintf(have, have_size, "invalid or duplicate slot");
            return 1;
        }
        json_int(json_at(entry, 1), &items[s]);
        json_int(json_at(entry, 2), &counts[s]);
        json_int(json_at(entry, 3), &damages[s]);
    }

    for (int i = 0; i < 40; ++i)
    {
        int expected = items[i] >= 0;
        /* a Java stack at size 0 stays in its slot until something drops it
         * (EntitySheep.interact's worn-out shears); the native slot holds
         * the same item at count 0 */
        int actual = got[i].count > 0 || (expected && counts[i] == 0 && got[i].count == 0 && got[i].item == items[i]);
        if (!exact && !expected) continue;
        if (expected == actual && (!expected ||
            (items[i] == got[i].item && counts[i] == got[i].count && damages[i] == got[i].damage))) continue;

        *slot = i;
        if (expected) snprintf(want, want_size, "%lld,%lld,%lld",
                               (long long)items[i], (long long)counts[i], (long long)damages[i]);
        else snprintf(want, want_size, "empty");
        if (actual) snprintf(have, have_size, "%d,%d,%d", got[i].item, got[i].count, got[i].damage);
        else snprintf(have, have_size, "empty");
        return 1;
    }

    return 0;
}


/* --watch ID: the native entity's tree in whichever world holds it, NULL
 * when none does; the player's own dimension is entered again after. */
static nbt *native_entity(const struct snapshot *s, struct serverreplay *sr, const struct server_player *sp, int want)
{
    static const int dims[3] = {0, -1, 1};
    nbt *found = NULL;
    for (int d = 0; d < 3 && !found; ++d)
    {
        if (serverreplay_dim_nents(sr, dims[d]) == 0) continue;
        serverreplay_enter(sr, dims[d]);
        for (int k = 0; k < sr->d->nents && !found; ++k)
        {
            if (sr->d->ents[k].pool == 3 && sp->sv.removed) continue;
            int id;
            nbt *tag = sr_entry_nbt(s, sp, &sr->d->ents[k], &id);
            if (id == want) found = tag;
            else nbt_free(tag);
        }
    }
    serverreplay_enter(sr, serverreplay_player_dim(sr));
    return found;
}

/* The recording's keyframe set: DIR/../../keyframes/NAME for the recording
 * DIR = .../snapshots/NAME (the path as given: a lane's recordings are links
 * into master's tree, and its keyframes sit beside them). */
/* tests/clientdraw.allow: the client randomness fields (d.cseed, d.cmath,
 * d.cstat, d.cw) a recording is not compared on yet, each with its cause.
 * A line is RECORDING FIELD[,FIELD...] # cause; RECORDING * names every
 * recording. skip[] follows CLIENT_FIELDS. */
static const char *const CLIENT_FIELDS[4] = { "d.cseed", "d.cmath", "d.cstat", "d.cw" };

static void clientdraw_allow(const char *dir, int skip[4])
{
    /* from csrc/, the checkout, or oracle/ (make -C oracle keyframes) */
    const char *paths[] = { "tests/clientdraw.allow", "csrc/tests/clientdraw.allow", "../csrc/tests/clientdraw.allow" };
    FILE *f = NULL;
    for (int i = 0; i < 3 && !f; ++i) f = fopen(paths[i], "r");
    if (!f) return;
    char name[256];
    size_t n = strlen(dir);
    while (n > 0 && dir[n - 1] == '/') --n;
    size_t b = n;
    while (b > 0 && dir[b - 1] != '/') --b;
    snprintf(name, sizeof name, "%.*s", (int)(n - b), dir + b);
    char line[1024];
    while (fgets(line, sizeof line, f))
    {
        char *hash = strchr(line, '#');
        if (hash) *hash = 0;
        char rec[256], fields[256];
        if (sscanf(line, "%255s %255s", rec, fields) != 2) continue;
        if (strcmp(rec, "*") && strcmp(rec, name)) continue;
        for (char *tok = strtok(fields, ","); tok; tok = strtok(NULL, ","))
            for (int k = 0; k < 4; ++k)
                if (!strcmp(tok, CLIENT_FIELDS[k])) skip[k] = 1;
    }
    fclose(f);
}

/* The client world's Random at the bind (cwrand.h cwrand_from_tape); *from
 * is the first row whose d.cw is compared (-1: not recovered). */
static void cw_recover(const char *path, int64_t t0, uint64_t *seed, int32_t *lcg, int64_t *from)
{
    cwrand_from_tape(path, t0, seed, lcg, from);
    if (*from < 0) printf("d.cw: the client world's Random is not recoverable from rows %lld..%lld: not compared\n",
                          (long long)(t0 - 1), (long long)(t0 + 2));
}

static void keyframe_set_dir(const char *dir, char *out, size_t n)
{
    char d[4096];
    snprintf(d, sizeof d, "%s", dir);
    size_t len = strlen(d);
    while (len > 1 && d[len - 1] == '/') d[--len] = 0;
    const char *name = strrchr(d, '/');
    if (name) { d[name - d] = 0; ++name; snprintf(out, n, "%s/../keyframes/%s", d, name); }
    else snprintf(out, n, "../keyframes/%s", d);
}

/* The tape's fingerprint as set.json records it: the SHA-1 of its last
 * line (the final row carries every chained digest), in hex. */
static int tape_last_sha1(const char *tape, char hex[41])
{
    FILE *f = fopen(tape, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END)) { fclose(f); return 0; }
    long end = ftell(f);
    long want = end < (8L << 20) ? end : (8L << 20);
    char *buf = malloc((size_t)want + 1);
    if (!buf || fseek(f, end - want, SEEK_SET) || fread(buf, 1, (size_t)want, f) != (size_t)want) { free(buf); fclose(f); return 0; }
    fclose(f);
    long e = want;
    while (e > 0 && (buf[e - 1] == '\n' || buf[e - 1] == '\r')) --e;
    long b = e;
    while (b > 0 && buf[b - 1] != '\n') --b;
    if (b == 0 && want < end) { free(buf); return 0; }
    uint8_t h[20];
    sha1((const uint8_t *)buf + b, (size_t)(e - b), h);
    for (int i = 0; i < 20; ++i) snprintf(hex + 2 * i, 3, "%02x", h[i]);
    free(buf);
    return 1;
}

/* --from-row N: the verified keyframe with the largest tick at or before N
 * (set.json's "keyframes"), or the recording's own start when none is; 0
 * with the reason when the set is not this tape's. */
static int keyframe_for_row(const char *dir, int64_t row, char *out, size_t n)
{
    char set[4200], path[4400], tape[4200];
    keyframe_set_dir(dir, set, sizeof set);
    snprintf(path, sizeof path, "%s/set.json", set);
    FILE *f = fopen(path, "rb");
    if (!f) { printf("keyframes: none for %s (%s), starting at the recording's own snapshot\n", dir, path); return 1; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)len + 1);
    size_t got = fread(text, 1, (size_t)len, f);
    fclose(f);
    text[got] = 0;
    struct jval *j = json_parse(text);
    if (!j) { printf("FAIL %s does not parse\n", path); return 0; }
    const char *want = json_str(json_get(json_get(j, "tape"), "lastSha1"));
    char have[41];
    snprintf(tape, sizeof tape, "%s/tape.jsonl", dir);
    if (!want || !tape_last_sha1(tape, have) || strcmp(want, have))
    {
        printf("FAIL keyframes %s were made from another tape (last row sha1 %s, this tape's %s): remake them "
               "(make -C oracle keyframes RECS=NAME)\n", set, want ? want : "?", have);
        json_free(j);
        return 0;
    }
    const struct jval *ks = json_get(j, "keyframes");
    int64_t best = -1;
    for (int i = 0; i < json_len(ks); ++i)
    {
        int64_t t;
        if (json_int(json_at(ks, i), &t) && t <= row && t > best) best = t;
    }
    json_free(j);
    if (best < 0) { printf("keyframes: none at or before row %lld, starting at the recording's own snapshot\n", (long long)row); return 1; }
    snprintf(out, n, "%.4000s/t%lld", set, (long long)best);
    return 1;
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: test_snapshots [FLAG...] SNAPSHOT_DIR, or SNAPSHOT_DIR [FLAG...] [--trace FILE] [--rows N]\n");
        return 2;
    }

    /* the input guard reads the directory as the last argument, so the
     * negative flags may also come first: test_snapshots FLAG... DIR */
    int dir_at = 1;
    struct stat dir_st;
    if (argc > 2 && (stat(argv[1], &dir_st) || !S_ISDIR(dir_st.st_mode))) dir_at = argc - 1;
    const char *dir = argv[dir_at];
    int load_check = 0;
    for (int i = 1; i < argc; ++i)
        if (i != dir_at && !strcmp(argv[i], "--load-check")) load_check = 1;
    int client_skip[4] = { 0 };
    clientdraw_allow(dir, client_skip);
    /* --phase-kernel (phasekernel.h) runs the replay in an image environment */
    for (int i = 1; i < argc; ++i)
        if (i != dir_at && !strcmp(argv[i], "--phase-kernel") && nw_env->img.size == 0)
        {
            struct env *e = env_new();
            if (e == NULL)
            {
                printf("FAIL --phase-kernel: no image environment\n");
                return 2;
            }
            nw_env = e;
            break;
        }
    /* the snapshot: 2.4 MB with the Nether and End, off the 8 MB stack; an
     * image environment's (image.h) is in its image, which a relocation
     * (tests/test_relocate.c) moves: after the row hook the harness takes
     * it, the session and the replay again from the image's directory */
    static _Thread_local struct snapshot snap_own;
    struct snapshot *snapp = &snap_own;
    int in_image = nw_env->img.size != 0;
    if (in_image)
    {
        int heap = image_heap_set(nw_env, 1);
        snapp = calloc(1, sizeof *snapp);
        image_heap_set(nw_env, heap);
    }

    /* the recording (its tape, end/ snapshot, stats dumps and checkpoint) is
     * dir; the state the replay starts from is dir's own snapshot, a
     * keyframe (--keyframe KDIR), or the nearest keyframe at or before a row
     * (--from-row N, the recording's keyframe set) */
    char snapdir[4200];
    snprintf(snapdir, sizeof snapdir, "%s", dir);
    for (int i = 1; i < argc; ++i)
    {
        if (i == dir_at) continue;
        if (!strcmp(argv[i], "--keyframe") && i + 1 < argc) snprintf(snapdir, sizeof snapdir, "%s", argv[++i]);
        else if (!strcmp(argv[i], "--from-row") && i + 1 < argc)
        {
            if (!keyframe_for_row(dir, atoll(argv[++i]), snapdir, sizeof snapdir)) return 1;
        }
    }
    if (strcmp(snapdir, dir)) printf("keyframe %s\n", snapdir);

    int load_heap = image_heap_set(nw_env, 1);
    /* the replay reads the gzip files without their CRC-32s: the load
     * check (--load-check, its own suite job) reads them with */
    int snap_loaded = snapshot_load_crc(snapp, snapdir, load_check);
    image_heap_set(nw_env, load_heap);
    if (!snap_loaded)
    {
        printf("FAIL %s: the snapshot did not load\n", snapdir);
        return 1;
    }

    const struct jval *counts = json_get(snapp->manifest_json, "counts");
    int64_t mchunks = -1, mtiles = -1, ments = -1, mticks = -1;

    if (!counts || !json_int(json_get(counts, "chunks"), &mchunks) || !json_int(json_get(counts, "tiles"), &mtiles) ||
        !json_int(json_get(counts, "entities"), &ments) || !json_int(json_get(counts, "pendingTicks"), &mticks))
    {
        fail("%s: the manifest has no counts", dir);
        return 1;
    }

    printf("snapshot %s: seed %lld, tick %lld\n", snapdir, (long long)snapp->seed, (long long)snapp->tick);

    /* 1. counts (the saved:1 region chunks are extras the manifest's chunks
     * and tiles counts do not include) */
    int tiles = 0, nsaved = 0;
    for (int i = 0; i < snapp->nchunks; ++i)
    {
        if (snapp->chunks[i].saved) ++nsaved;
        else tiles += snapp->chunks[i].ntiles;
    }

    if (snapp->nchunks - nsaved != mchunks) fail("chunks: loaded %d, the manifest says %lld", snapp->nchunks - nsaved, (long long)mchunks);
    if (tiles != mtiles) fail("tiles: loaded %d, the manifest says %lld", tiles, (long long)mtiles);
    if (snapp->nents != ments) fail("entities: loaded %d, the manifest says %lld", snapp->nents, (long long)ments);
    if (snapp->nticks != mticks) fail("pending ticks: loaded %d, the manifest says %lld", snapp->nticks, (long long)mticks);

    /* 2 to 5 are the load check (--load-check): what the snapshot's files
     * load into, which only changes with them or the loading code, so the
     * suite runs it as a job of its own (test selection reruns it when
     * either changes) and the replay (6) runs without it */
    if (load_check)
    {
    /* 2. chunks: bytes, own hash, 3x3 hash */
    uint8_t *got = malloc(SNAP_CHUNK_BYTES), *want = malloc(SNAP_CHUNK_BYTES);
    int bad = 0, bytes_bad = 0, firstx = 0, firstz = 0, own_ok = 0, near_ok = 0;
    char what[256];

    /* the own and 3x3 hashes of every chunk first, several chains at a time */
    const struct chunk **own_c = malloc((size_t)(snapp->nchunks ? snapp->nchunks : 1) * sizeof *own_c);
    struct world **near_w = malloc((size_t)(snapp->nchunks ? snapp->nchunks : 1) * sizeof *near_w);
    int *near_x = malloc((size_t)(snapp->nchunks ? snapp->nchunks : 1) * sizeof *near_x);
    int *near_z = malloc((size_t)(snapp->nchunks ? snapp->nchunks : 1) * sizeof *near_z);
    uint64_t *own_h = malloc((size_t)(snapp->nchunks ? snapp->nchunks : 1) * sizeof *own_h);
    uint64_t *near_h = malloc((size_t)(snapp->nchunks ? snapp->nchunks : 1) * sizeof *near_h);

    for (int i = 0; i < snapp->nchunks; ++i)
    {
        struct snap_chunk *sc = &snapp->chunks[i];
        struct world *cw = sc->dim == 0 ? &snapp->world : sc->dim == -1 ? &snapp->hell_world : &snapp->end_world;
        if (sc->saved)
            cw = sc->dim == -1 ? snapp->hell_saved : snapp->end_saved;
        own_c[i] = world_chunk(cw, sc->cx, sc->cz);
        near_w[i] = sc->saved ? (sc->dim == -1 ? &snapp->hell_world : &snapp->end_world) : cw;
        near_x[i] = sc->cx;
        near_z[i] = sc->cz;
    }

    snapshot_chunk_hash_around_all(near_w, near_x, near_z, snapp->nchunks, near_h, own_c, own_h);

    for (int i = 0; i < snapp->nchunks; ++i)
    {
        struct snap_chunk *sc = &snapp->chunks[i];
        struct world *cw = sc->dim == 0 ? &snapp->world : sc->dim == -1 ? &snapp->hell_world : &snapp->end_world;
        if (sc->saved)
            cw = sc->dim == -1 ? snapp->hell_saved : snapp->end_saved;
        struct chunk *c = world_chunk(cw, sc->cx, sc->cz);

        if (!c)
        {
            fail("chunk (%d,%d): not in the loaded world", sc->cx, sc->cz);
            continue;
        }

        /* the recorded bytes, when the snapshot kept them, or the live
         * chunk's when it is another than the world holds there (a chunk
         * is its own bytes: the own hash above checks them against the
         * oracle's) */
        if ((sc->bytes != NULL || sc->live != c) && (snapshot_chunk_bytes(c, got), 1) &&
            snapshot_chunk_diff(snapshot_chunk_want(sc, want), got, what, sizeof what))
        {
            if (!bad) { firstx = sc->cx; firstz = sc->cz; fail("chunk (%d,%d): %s", sc->cx, sc->cz, what); }
            ++bad;
            ++bytes_bad;
        }

        uint64_t h = own_h[i];
        if (h != sc->hash)
        {
            if (!bad) fail("chunk (%d,%d): own hash want %016llx got %016llx", sc->cx, sc->cz, (unsigned long long)sc->hash, (unsigned long long)h);
            ++bad;
        }
        else ++own_ok;

        uint64_t n = near_h[i];
        if (n != sc->near)
        {
            if (!bad) fail("chunk (%d,%d): 3x3 hash want %016llx got %016llx", sc->cx, sc->cz, (unsigned long long)sc->near, (unsigned long long)n);
            ++bad;
        }
        else ++near_ok;
    }

    free(own_c);
    free(near_w);
    free(near_x);
    free(near_z);
    free(own_h);
    free(near_h);

    if (bad) fail("%d chunk checks differ (first (%d,%d))", bad, firstx, firstz);
    printf("chunks: %d loaded, %d of them byte-identical with their recorded bytes, %d own hashes and %d 3x3 hashes match\n",
           snapp->nchunks, snapp->nchunks - bytes_bad, own_ok, near_ok);

    int unset = 0;
    for (int i = 0; i < snapp->nchunks; ++i)
        for (int k = 0; k < SNAP_BIOME; ++k)
            if (snapp->chunks[i].biome[k] == 0xff) ++unset;

    if (unset) fail("biomes: %d of %d entries are still the -1 sentinel", unset, snapp->nchunks * SNAP_BIOME);
    printf("biomes: %d arrays of %d ids, none unset; %d skylight column arrays\n", snapp->nchunks, SNAP_BIOME, snapp->nchunks);

    /* 3. tile entities and entities round-trip */
    for (int i = 0; i < snapp->nchunks; ++i)
        for (int t = 0; t < snapp->chunks[i].ntiles; ++t)
        {
            char label[128];
            snprintf(label, sizeof label, "tile entity %s at (%d,%d)", scalar(snapp->chunks[i].tiles[t].tag, "id"), snapp->chunks[i].cx, snapp->chunks[i].cz);
            round_trip(snapp->chunks[i].tiles[t].text, label);
        }

    printf("tile entities: %d of %d round-trip through nbt_parse and nbt_render\n", tiles, tiles);

    struct tally tally;
    memset(&tally, 0, sizeof tally);
    int weather = 0;

    for (int i = 0; i < snapp->nents; ++i)
    {
        char label[128];
        snprintf(label, sizeof label, "entity %d:%s", snapp->ents[i].id, snapp->ents[i].cls);
        round_trip(snapp->ents[i].text, label);
        count_class(&tally, snapp->ents[i].cls);
        if (snapp->ents[i].weather) ++weather;
    }

    printf("entities: %d all round-trip; %d players, %d weather effects\n", snapp->nents, snapp->players, weather);
    printf("entity classes:");
    for (int i = 0; i < tally.n; ++i) printf(" %s=%d", tally.names[i], tally.count[i]);
    printf("\n");

    /* 4. the six NBT files */
    struct { const char *name; const nbt *tree; } files[] = {
        {"worldinfo.nbt", snapp->worldinfo}, {"worldstate.nbt", snapp->worldstate}, {"det.nbt", snapp->det},
        {"clientworld.nbt", snapp->clientworld}, {"player_client.nbt", snapp->player_client}, {"player_server.nbt", snapp->player_server},
    };

    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i)
    {
        char *text = nbt_render(files[i].tree);
        nbt *again = nbt_parse(text);
        if (!again) fail("%s: rendered text does not parse back", files[i].name);
        else
        {
            char *twice = nbt_render(again);
            if (strcmp(text, twice)) fail("%s: render and re-parse differ", files[i].name);
            free(twice);
        }
        nbt_free(again);
        free(text);
    }

    printf("nbt files: 6 parse and render\n");

    /* 5. pending ticks in the world's order: each dimension's own TreeSet is
     * ordered; the snapshot lists the worlds one after another */
    int out_of_order = 0;
    for (int i = 1; i < snapp->nticks; ++i)
    {
        const struct snap_tick *a = &snapp->ticks[i - 1], *b = &snapp->ticks[i];
        if (a->dim != b->dim) continue;
        if (a->scheduled > b->scheduled || (a->scheduled == b->scheduled && a->priority > b->priority) ||
            (a->scheduled == b->scheduled && a->priority == b->priority && a->entry > b->entry))
            ++out_of_order;
    }

    if (out_of_order) fail("pending ticks: %d entries are out of the world's order", out_of_order);
    printf("pending ticks: %d in the tree set's order, %d being ticked this tick, next entry id %s\n", snapp->nticks, snapp->nthis,
           scalar(snapp->worldstate, "nextTickEntryID"));

    free(got);
    free(want);
    snapshot_free(snapp);
    printf("%s: %s\n", snapdir, fails ? "FAIL" : "OK: the snapshot round-trips");
    return fails ? 1 : 0;
    }

    /* 6. the replay: from the snapshot's tick on, apply each row's act to the
     * two players and compare what the row recorded. Rows before the snapshot
     * tick are the join prologue and only count. An optional flag after the
     * directory runs the negative checks: no-sprint-boost pins the movement
     * speed attribute at its base, no-ground-friction pins every ground
     * slipperiness at 1 (friction 0.91 either way). */
    char path[1200];
    snprintf(path, sizeof path, "%s/tape.jsonl", dir);
    /* --tape PATH: another tape of the same run as the reference (the oracle's
     * replay of it with --detail rows) */
    for (int i = 1; i < argc; ++i)
        if (i != dir_at && !strcmp(argv[i], "--tape") && i + 1 < argc) snprintf(path, sizeof path, "%s", argv[++i]);
    struct tape tape;

    if (!tape_open(&tape, path))
    {
        fail("cannot read the tape beside the snapshot: %s", path);
        snapshot_free(snapp);
        return 1;
    }
    if (session_refuse_dev(tape.hdr, snapp->manifest_json, 1))
    {
        fprintf(stderr, "REFUSE dev entry in a non-dev tape\n");
        tape_close(&tape);
        snapshot_free(snapp);
        return 4;
    }

    int no_sprint_boost = 0, no_ground_friction = 0;
    int no_sprint_exhaustion = 0, drown_fast = 0;
    const char *trace_path = NULL;
    int trace_opened = 0;
    int max_rows = 0; /* --rows N: stop N rows past the snapshot (the any-tick sweep) */
    int64_t to_row = -1; /* --to-row N: stop before row N (a keyframe segment) */
    int stop_first = 0; /* --stop-first: stop after the first row with a mismatch */
    const char *state_diff = NULL; /* --state-diff KDIR: the end state against a snapshot of that tick */
    int fault_kind = 0; /* --fault entity|block|scalar@ROW: a planted divergence */
    int64_t fault_row = -1;
    int64_t trace_from = -1; /* --trace-rows A: each row's server fields from row A on */
    int watch_id = -1; /* --watch ID: that entity's tree against the row's x.enbt from row A on */
    /* the phase tracer (phase.h): --phase-trace FILE writes each row's phase
     * digests, --phase-digest keeps only their hash for the summary line, and
     * --phase-check also fails a phase that wrote a class it does not declare */
    const char *phase_path = NULL;
    int mem_on = 0;   /* --mem: the environment's bytes by owner, at the start and at the peak */
    long mem_every = 0;   /* --mem-every N: also a line every N rows */
    static struct envmem mem_start, mem_peak;
    int64_t mem_peak_row = -1;
    int phase_on = 0, phase_check = 0;
    /* the phase profiler (phaseprof.c): --phase-profile prints each phase's
     * time and allocations after the replay, --phase-profile-rows FILE also
     * writes one line per row */
    const char *prof_rows_path = NULL;
    int prof_on = 0;
    /* --gen-ahead c|cuda: chunk generation served ahead of each step
     * (genahead.h) by the C engine or, in the CUDA build
     * (out/native/cuda/worldgen/test_snapshots), the device generators */
    const char *ga_name = NULL;
    int ga_misses = 0;   /* --gen-ahead-misses: a line per miss */
    /* --light-cap count|cuda|save:PATH: every light engine call captured
     * (lightcap.h); cuda replays each on the device (the CUDA build);
     * save:PATH stores the stream in PATH for a replay on the device alone
     * (lc_sink_file_create; a PATH ending in .zst is written through zstd) */
    const char *lc_name = NULL;
    int lc_drivers = 0;   /* --light-drivers: func_150809_p's and the relight checks' loops captured whole */
    FILE *lc_file = NULL; /* save:PATH's file (a pipe to zstd when lc_file_piped) */
    int lc_file_piped = 0;
    /* --row-dump FILE: each simulated row's record (rowrec.h), then the end
     * state's: what the env pool's gate compares its envs with */
    const char *row_dump_path = NULL;
    /* --light-defer never|c|c-write|c-frame|cuda: the client world's light
     * deferred (lightdefer.h): never run, run by C at every sync (c) or only
     * at the syncs named (c-write: writes and row ends, c-frame: row ends),
     * or by the device (cuda, the CUDA build); --cw-digest FILE: each row's
     * client world light digest (cwdigest.h), after the row's sync */
    const char *ld_name = NULL, *cw_digest_path = NULL;
    int cw_detail = 0;   /* --cw-digest-detail: the digest's parts, a line per chunk */

    for (int i = 1; i < argc; ++i)
    {
        if (i == dir_at) continue;
        if (!strcmp(argv[i], "no-sprint-boost")) no_sprint_boost = 1;
        else if (!strcmp(argv[i], "no-ground-friction")) no_ground_friction = 1;
        else if (!strcmp(argv[i], "no-sprint-exhaustion")) no_sprint_exhaustion = 1;
        else if (!strcmp(argv[i], "drown-fast")) drown_fast = 1;
        else if (!strcmp(argv[i], "dev-give-plus-one")) give_extra = 1;
        else if (!strcmp(argv[i], "--trace") && i + 1 < argc) trace_path = argv[++i];
        else if (!strcmp(argv[i], "--rows") && i + 1 < argc) max_rows = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--region-cache") && i + 1 < argc)
        {
            if (!regioncache_flag(argv[++i])) { printf("FAIL --region-cache takes on, off, verify, verify:DIR or an absolute DIR\n"); return 2; }
        }
        else if (!strcmp(argv[i], "--region-spill") && i + 1 < argc)
        {
            if (!rspill_flag(argv[++i])) { printf("FAIL --region-spill takes on, off or an absolute DIR\n"); return 2; }
        }
        else if ((!strcmp(argv[i], "--keyframe") || !strcmp(argv[i], "--from-row") || !strcmp(argv[i], "--tape")) && i + 1 < argc) ++i;
        else if (!strcmp(argv[i], "--to-row") && i + 1 < argc) to_row = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--stop-first")) stop_first = 1;
        else if (!strcmp(argv[i], "--load-check")) continue;
        else if (!strcmp(argv[i], "--state-diff") && i + 1 < argc) state_diff = argv[++i];
        else if (!strcmp(argv[i], "--trace-rows") && i + 1 < argc) trace_from = atoll(argv[++i]);
        else if (!strcmp(argv[i], "--watch") && i + 1 < argc) watch_id = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--phase-trace") && i + 1 < argc) { phase_path = argv[++i]; phase_on = 1; }
        else if (!strcmp(argv[i], "--phase-digest")) phase_on = 1;
        else if (!strcmp(argv[i], "--mem")) mem_on = 1;
        else if (!strcmp(argv[i], "--mem-every") && i + 1 < argc) { mem_every = atol(argv[++i]); mem_on = 1; }
        else if (!strcmp(argv[i], "--phase-check")) phase_on = phase_check = 1;
        else if (!strcmp(argv[i], "--phase-profile")) prof_on = 1;
        else if (!strcmp(argv[i], "--gen-ahead") && i + 1 < argc) ga_name = argv[++i];
        else if (!strcmp(argv[i], "--gen-ahead-misses")) ga_misses = 1;
        else if (!strcmp(argv[i], "--light-cap") && i + 1 < argc) lc_name = argv[++i];
        else if (!strcmp(argv[i], "--light-drivers")) lc_drivers = 1;
        else if (!strcmp(argv[i], "--row-dump") && i + 1 < argc) row_dump_path = argv[++i];
        else if (!strcmp(argv[i], "--light-defer") && i + 1 < argc) ld_name = argv[++i];
        else if (!strcmp(argv[i], "--cw-digest") && i + 1 < argc) cw_digest_path = argv[++i];
        else if (!strcmp(argv[i], "--cw-digest-detail")) cw_detail = 1;
        else if (!strcmp(argv[i], "--phase-profile-rows") && i + 1 < argc) { prof_rows_path = argv[++i]; prof_on = 1; }
        else if (!strcmp(argv[i], "--fault") && i + 1 < argc)
        {
            const char *f = argv[++i], *at = strchr(f, '@');
            if (at && !strncmp(f, "entity@", 7)) fault_kind = 1;
            else if (at && !strncmp(f, "block@", 6)) fault_kind = 2;
            else if (at && !strncmp(f, "scalar@", 7)) fault_kind = 3;
            else { printf("FAIL --fault takes entity@ROW, block@ROW or scalar@ROW\n"); return 2; }
            fault_row = atoll(at + 1);
        }
        else if (!strcmp(argv[i], "no-nether-tick")) nw_env->cfg.sr_negative |= 1;
        else if (!strcmp(argv[i], "drop-one-write")) nw_env->cfg.sr_negative |= 2;
        else if (!strcmp(argv[i], "fold-overworld-only")) nw_env->cfg.sr_negative |= 4;
        else if (!strcmp(argv[i], "skip-one-mob-tick")) nw_env->cfg.sr_negative |= 8;
        else if (!strcmp(argv[i], "freeze-inhabited")) nw_env->cfg.sr_negative |= 16;
        else
        {
            int pk = pk_flag(argc, argv, &i);
            if (pk < 0) { printf("FAIL bad %s argument\n", argv[i]); return 2; }
            if (pk == 0) { printf("FAIL unknown flag %s\n", argv[i]); return 2; }
        }
    }
    /* the digests only with a reference to compare them with */
    if (PK.on) phase_on |= PK.ref_path != NULL;
    else if (PK.ref_path != NULL || PK.plant_row >= 0 || PK.verify_every)
    {
        printf("FAIL the --phase-kernel-* flags go with --phase-kernel\n");
        return 2;
    }

    FILE *phase_out = NULL;
    if (phase_path != NULL && (phase_out = fopen(phase_path, "w")) == NULL)
    {
        printf("FAIL --phase-trace %s does not open\n", phase_path);
        return 2;
    }
    if (phase_on) phase_trace_start(phase_out, phase_check);
    if (prof_on && phase_on)
    {
        printf("FAIL --phase-profile times the phases, the tracer's digests would be in it: one at a time\n");
        return 2;
    }
    if (prof_on) phase_profile_start();

    struct ga_gen *ga_gen = NULL;
    struct genahead *ga = NULL;
    uint64_t ga_ins = 0, ga_ins_max = 0;
    if (ga_name != NULL)
    {
        if (!strcmp(ga_name, "c")) ga_gen = ga_gen_c_create();
#ifdef NETHERITE_GPU_WORLDGEN
        else if (!strcmp(ga_name, "cuda")) ga_gen = ga_gen_cuda_create();
#endif
        if (ga_gen == NULL)
        {
            printf("FAIL --gen-ahead %s: no such generator here\n", ga_name);
            return 2;
        }
        ga = genahead_create();
        if (ga_misses) genahead_log_misses(ga, stdout);
    }

    struct lightcap *lcap = NULL;
    if (lc_name != NULL)
    {
        struct lc_sink *lc_sink = NULL;
        int ok = !strcmp(lc_name, "count");
#ifdef NETHERITE_GPU_LIGHTING
        if (!strcmp(lc_name, "cuda")) ok = (lc_sink = lc_sink_cuda_create()) != NULL;
#endif
        if (!strncmp(lc_name, "save:", 5))
        {
            const char *p = lc_name + 5;
            size_t n = strlen(p);
            char cmd[4200];

            if (n > 4 && !strcmp(p + n - 4, ".zst") && n < 4096 && strchr(p, '\'') == NULL)
            {
                snprintf(cmd, sizeof cmd, "zstd -q -f -3 -T2 -o '%s'", p);
                lc_file = popen(cmd, "w");
                lc_file_piped = 1;
            }
            else lc_file = fopen(p, "wb");
            ok = lc_file != NULL && (lc_sink = lc_sink_file_create(lc_file, (size_t)64 << 20, lc_drivers)) != NULL;
        }
        if (!ok || (lcap = lightcap_new(lc_sink, lc_drivers)) == NULL)
        {
            printf("FAIL --light-cap %s: no such consumer here\n", lc_name);
            return 2;
        }
        nw_env->light.cap = lcap;
    }

    struct lightdefer *ldef = NULL;
    if (ld_name != NULL)
    {
        struct lightdefer_exec ex, *exp = NULL;
        int syncs = LD_SYNC_ALL, never = 0, ok = 1;

        if (!strcmp(ld_name, "never")) never = 1;
        else if (!strcmp(ld_name, "c-write")) syncs = LD_SYNC_WRITE;
        else if (!strcmp(ld_name, "c-frame")) syncs = 0;
#ifdef NETHERITE_GPU_LIGHTING
        else if (!strcmp(ld_name, "cuda")) ok = lc_defer_exec_cuda(&ex) == 0, exp = &ex;
#endif
        else ok = !strcmp(ld_name, "c");
        (void)ex;
        if (!ok || (ldef = lightdefer_new(exp)) == NULL)
        {
            printf("FAIL --light-defer %s: no such executor here\n", ld_name);
            return 2;
        }
        ldef->never = never;
        ldef->syncs = syncs;
        nw_env->light.defer = ldef;
    }
    FILE *cw_digest_out = NULL;
    if (cw_digest_path != NULL && (cw_digest_out = fopen(cw_digest_path, "w")) == NULL)
    {
        printf("FAIL --cw-digest %s does not open\n", cw_digest_path);
        return 2;
    }

    FILE *row_dump = NULL;
    if (row_dump_path != NULL && (row_dump = fopen(row_dump_path, "w")) == NULL)
    {
        printf("FAIL --row-dump %s does not open\n", row_dump_path);
        return 2;
    }

    struct stats_dumps stats_dumps;
    stats_dumps_load(&stats_dumps, dir);
    stats_dumps_load_end(&stats_dumps, dir);

    nw_env->cfg.player_no_sprint_boost = no_sprint_boost;
    nw_env->cfg.player_no_ground_friction = no_ground_friction;

    /* the load, the start and the tick pair are the playable client's own
     * (session.c); this harness adds the checks and the row comparison */
    /* the two players: off the stack (in the image for an image environment) */
    static _Thread_local struct session ss_own;
    struct session *ssp = &ss_own;
    if (in_image)
    {
        int heap = image_heap_set(nw_env, 1);
        ssp = calloc(1, sizeof *ssp);
        image_heap_set(nw_env, heap);
    }

    if (!session_open_at(ssp, snapp, snapdir, dir, tape.hdr))
    {
        fail("%s: %s", dir, ssp->err);
        snapshot_free(snapp);
        return 1;
    }
    ssp->dev_apply = dev_apply;
    ssp->dev_end = dev_end;
    if (mem_on)
    {
        envmem_session(ssp, &mem_start);
        mem_peak = mem_start;
    }
    ssp->hooks.ctx = ssp;
    ssp->hooks.after_player_tick = plant_give_extra;
    if (PK.on) pk_setup(dir);

    struct client_player *cp = &ssp->cp;
    struct server_player *sp = &ssp->sp;
    struct serverreplay *sr = ssp->sr;
    int server_rows = ssp->server_rows;

    if (server_rows)
    {
        if (snapp->version >= 2)
        {
            /* the snapshot's entities against the order the load built, the
             * player's entry aside */
            int k = 0, shown = 0, dim = -999;
            for (int i = 0; i < snapp->nents; ++i)
            {
                if (snapp->ents[i].player) continue;
                /* each dimension's own order, in worldServers order */
                if (snapp->ents[i].dim != dim)
                {
                    dim = snapp->ents[i].dim;
                    serverreplay_enter(sr, dim);
                    k = 0;
                }
                while (k < sr->d->nents && sr->d->ents[k].pool == 3) ++k;
                if (k >= sr->d->nents) continue;
                int gid;
                nbt *got = sr_entry_nbt(snapp, sp, &sr->d->ents[k], &gid);
                char diff[512];
                if (nbt_diff(snapp->ents[i].tag, got, diff, sizeof diff) && shown++ < 8)
                    printf("snapshot entity %d %s: %s\n", snapp->ents[i].id, snapp->ents[i].cls, diff);
                nbt_free(got);
                ++k;
            }
            serverreplay_enter(sr, 0);
            if (shown) fail("%s: %d entities differ at the snapshot boundary", dir, shown);
        }

        /* FoodStats.onUpdate's PEACEFUL gate reads the world's difficulty,
         * which the snapshot's worlds.nbt carries per world; the survival
         * module takes it from the same place. */
        printf("server: %d worlds, view %d, blkHash start %016llx, active set model matches the snapshot\n",
               sr->nworlds, sr->view, (unsigned long long)sr->blk_hash);

        /* the pending ticks are each world's own set inside the replay:
         * serverreplay_load checked every world's count */

        /* the loaded-chunk count per world, against the snapshot's per-dim
         * chunkstate lines */
        {
            int over = 0, hell = 0, end = 0;
            for (int i = 0; i < snapp->nchunks; ++i)
            {
                if (snapp->chunks[i].saved) continue;
                if (snapp->chunks[i].dim == 0) ++over;
                else if (snapp->chunks[i].dim == -1) ++hell;
                else if (snapp->chunks[i].dim == 1) ++end;
            }
            for (int i = 0; i < sr->nworlds; ++i)
            {
                int want = sr->w[i].dim == 0 ? over : sr->w[i].dim == -1 ? hell : end;
                if (sr->w[i].chunks != want)
                    fail("%s: world %d (dim %d): worlds.nbt says %d chunks, the snapshot holds %d",
                         dir, i, sr->w[i].dim, sr->w[i].chunks, want);
            }
        }

        if (snapp->version < 2 && ssp->no_gen != 1) fail("%s: the snapshot does not pin chunk generation off", dir);
    }

    /* the client world's Random, from the rows */
    uint64_t cw_seed = 0;
    int32_t cw_lcg = 0;
    int64_t cw_from;
    cw_recover(path, snapp->tick, &cw_seed, &cw_lcg, &cw_from);
    if (cw_from == snapp->tick) client_player_set_world_rand(cp, cw_seed, cw_lcg);

    surv.no_sprint_exhaustion = no_sprint_exhaustion;
    surv.drown_fast = drown_fast;

    int64_t hseed = -1;
    if (!json_int(json_get(tape.hdr, "seed"), &hseed) || hseed != snapp->seed)
        fail("the tape's header seed %lld is not the snapshot's %lld", (long long)hseed, (long long)snapp->seed);

    const struct jval *row;
    int64_t nrows = 0, join = 0, noinput = 0;
    int rc, bad_rows = 0;
    int64_t first_bad_row = -1;
    char first_bad_field[64] = "";
    char first_bad_want[64] = "", first_bad_got[64] = "";
    int sim_rows = 0, sim_bad = 0;

    /* dead livings are freed once nothing points at them (grave.h): between
     * rows, where no entity update is on the stack */
    grave_enable(GRAVE_THRESHOLD);
    while ((rc = tape_next(&tape, &row)) == 1)
    {
        int64_t t;
        if (!json_int(json_get(row, "t"), &t) || t != nrows)
        {
            fail("row %lld has t=%lld", (long long)nrows, (long long)t);
            ++bad_rows;
            break;
        }

        if (t < snapp->tick)
        {
            ++join;
            ++nrows;
            continue;
        }
        if (max_rows > 0 && t >= snapp->tick + max_rows) break;
        if (to_row >= 0 && t >= to_row) break;
        if (stop_first && first_bad_row >= 0) break;
        if (snapshots_row_hook != NULL)
        {
            const struct env *before = nw_env;
            snapshots_row_hook();
            if (nw_env != before)
            {
                /* the image moved: everything it holds is at its new place */
                snapp = (struct snapshot *)((unsigned char *)nw_env + nw_env->img.dir.snapshot);
                ssp = (struct session *)((unsigned char *)nw_env + nw_env->img.dir.session);
                cp = &ssp->cp;
                sp = &ssp->sp;
                sr = ssp->sr;
            }
        }

        stats_dumps_check(&stats_dumps, t, sp, cp);

        struct act act;
        char why[160];
        if (session_parse_act(row, &act, why, sizeof why))
        {
            fail("row %lld: %s", (long long)t, why);
            ++bad_rows;
        }
        if (!act.has_in) ++noinput;

        if (trace_path && !trace_opened)
        {
            trace_open(trace_path);
            trace_opened = 1;
        }

        /* --fault block@ROW: a stone written at the head of the row's server
         * tick through the Dev setblock path, two blocks above the server
         * player (the write moves w.bc and d.blk in this row) */
        struct jval *fault_entry = NULL;
        const struct jval *fault_list[1];
        if (fault_kind == 2 && t == fault_row && server_rows)
        {
            char text[300];
            snprintf(text, sizeof text,
                     "{\"tick\":%lld,\"class\":\"Dev\",\"cmd\":{\"op\":\"setblock\",\"x\":%d,\"y\":%d,\"z\":%d,\"id\":1}}",
                     (long long)t, (int)floor(sp->e.pos_x), (int)floor(sp->e.pos_y) + 2, (int)floor(sp->e.pos_z));
            fault_entry = json_parse(strdup(text));
            fault_list[0] = fault_entry;
            ssp->extra = fault_list;
            ssp->nextra = 1;
            ssp->next_extra = 0;
            printf("fault: row %lld, stone at (%d,%d,%d)\n", (long long)t, (int)floor(sp->e.pos_x),
                   (int)floor(sp->e.pos_y) + 2, (int)floor(sp->e.pos_z));
        }

        /* the chunks this tick may generate, made before it; a Dev tp
         * entry due this row moves the player where the world cannot see */
        if (ga != NULL && server_rows)
        {
            for (int k = 0; k < (int)json_len(ssp->setups); ++k)
            {
                const struct jval *e = json_at(ssp->setups, k), *cmd = json_get(e, "cmd");
                const char *cls = json_str(json_get(e, "class")), *op = json_str(json_get(cmd, "op"));
                int64_t at = -1;
                uint64_t bx, bz;
                double hx, hz;
                if (!json_int(json_get(e, "tick"), &at) || at != t || !cls || strcmp(cls, "Dev") || !op ||
                    strcmp(op, "tp") || !json_double(json_get(cmd, "x"), &bx) || !json_double(json_get(cmd, "z"), &bz))
                    continue;
                memcpy(&hx, &bx, 8);
                memcpy(&hz, &bz, 8);
                genahead_hint(ga, hx, hz);
            }
            uint64_t i0 = phase_prof_instructions();
            genahead_step(&ga, &sr, 1, ga_gen);
            uint64_t di = phase_prof_instructions() - i0;
            ga_ins += di;
            if (di > ga_ins_max) ga_ins_max = di;
        }

        /* ---- one tick pair ---- */
        long traced_before = nw_env->phase.rows;
        if (!session_tick(ssp, t, &act))
        {
            fail("%s", ssp->err);
            ++bad_rows;
            break;
        }
        if (cw_from == t + 1) client_player_set_world_rand(cp, cw_seed, cw_lcg);
        if (PK.on) pk_row_end(t, nw_env->phase.rows != traced_before);
        if (mem_on)
        {
            static struct envmem m;
            envmem_session(ssp, &m);
            if (envmem_total(&m) > envmem_total(&mem_peak)) { mem_peak = m; mem_peak_row = t; }
            if (mem_every > 0 && t % mem_every == 0)
            {
                char label[64];
                snprintf(label, sizeof label, "mem row %lld", (long long)t);
                envmem_print(stdout, label, &m, 0);
            }
        }
        if (fault_entry)
        {
            ssp->extra = NULL;
            ssp->nextra = ssp->next_extra = 0;
            json_free(fault_entry);
        }

        /* --fault entity@ROW and scalar@ROW: after the row's tick, the first
         * non-player entity of the player's world moves 1/1024 faster
         * upward, or the overworld's World.rand steps one state bit */
        if (fault_kind == 1 && t == fault_row && server_rows)
        {
            serverreplay_enter(sr, serverreplay_player_dim(sr));
            int done = 0;
            for (int k = 0; k < sr->d->nents && !done; ++k)
            {
                struct entity *e = NULL;
                int id = -1;
                if (sr->d->ents[k].pool == 0) { ie_ent *ie = sr_ent_p(sr, &sr->d->ents[k]); e = &ie->e; id = ie->entity_id; }
                else if (sr->d->ents[k].pool == 2)
                {
                    struct an_ent *an = sr_ent_p(sr, &sr->d->ents[k]);
                    e = an->is_living ? &lv_get(an->livh)->e : &ie_get(an->ieh)->e;
                    id = an->is_living ? lv_get(an->livh)->entity_id : ie_get(an->ieh)->entity_id;
                }
                if (!e) continue;
                e->motion_y += 1.0 / 1024;
                printf("fault: row %lld, entity %d's motionY + 1/1024\n", (long long)t, id);
                done = 1;
            }
            if (!done) printf("fault: row %lld, no non-player entity to change\n", (long long)t);
        }
        if (fault_kind == 3 && t == fault_row && server_rows)
        {
            ST_RAND(&sr->w[0].st).seed ^= 1;
            printf("fault: row %lld, the overworld's World.rand seed ^ 1\n", (long long)t);
        }

        /* ---- compare the row against the replay ---- */
        if (server_rows)
        {


            const struct jval *wj = json_get(row, "w");
            const struct jval *dj = json_get(row, "d");
            uint64_t seeder, math, split;
            sr_det(sr, &seeder, &math, &split);

            if (!wj || !dj)
            {
                fail("row %lld: the row carries no w or d block", (long long)t);
                ++bad_rows;
                break;
            }

            /* the server's fields, then the client's randomness: its seeder,
             * Math.random and split streams (Det's CLIENT role) */
            static const char *sfields[14] = { "w.wt", "w.tt", "w.bc", "w.ents", "d.blk", "d.sw",
                                               "d.sseed", "d.smath", "d.sstat", "d.ents",
                                               "d.cseed", "d.cmath", "d.cstat", "d.cw" };
            int ishex[14] = { 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1 };
            int64_t v = 0;
            int64_t swant[14], sgot[14];
            uint64_t hwant[14], hgot[14];
            /* a field the row does not carry or the allow list names is not
             * compared */
            int skipf[14] = { 0 };

            sgot[0] = sr_world_time(sr);
            sgot[1] = sr_total_time(sr);
            sgot[2] = sr_block_writes(sr);
            sgot[3] = snapp->players - (sp->sv.removed || sr->player_unlisted ? 1 : 0) + sr->native_entities;
            sgot[4] = 0; sgot[5] = 0; sgot[6] = 0; sgot[7] = 0; sgot[8] = 0;
            sgot[9] = 0;
            swant[0] = swant[1] = swant[2] = swant[3] = 0;
            hgot[0] = sr_block_hash(sr);
            hgot[1] = sr_world_rng(sr);
            hgot[2] = seeder;
            hgot[3] = math;
            hgot[4] = split;
            g_row = (int)t;
            hgot[5] = sr_entity_digest(snapp, sr, sp);
            hwant[0] = hwant[1] = hwant[2] = hwant[3] = hwant[4] = hwant[5] = 0;
            hgot[6] = det_seeder_state(&SR_DET(sr), DET_CLIENT);
            hgot[7] = det_math_state(&SR_DET(sr), DET_CLIENT);
            hgot[8] = det_split_state(&SR_DET(sr), DET_CLIENT);
            hgot[9] = (uint64_t)cwrand_digest(cp->cw_rand.r.seed, cp->cw_lcg);
            hwant[6] = hwant[7] = hwant[8] = hwant[9] = 0;
            for (int i = 0; i < 4; ++i)
            {
                const char *s = json_str(json_get(dj, CLIENT_FIELDS[i] + 2));
                if (s != NULL) hwant[6 + i] = strtoull(s, NULL, 16);
                skipf[10 + i] = s == NULL || client_skip[i];
            }
            /* d.cw from the recovered row on, while the client world's
             * Random is known */
            if (cw_from < 0 || t < cw_from || !cp->cw_rand_known) skipf[13] = 1;

            if (json_int(json_get(wj, "wt"), &v)) swant[0] = v;
            if (json_int(json_get(wj, "tt"), &v)) swant[1] = v;
            if (json_int(json_get(wj, "bc"), &v)) swant[2] = v;
            if (json_int(json_get(wj, "ents"), &v)) swant[3] = v;

            const char *hexfield[6] = { "blk", "sw", "sseed", "smath", "sstat", "ents" };

            for (int i = 0; i < 6; ++i)
            {
                const char *s = json_str(json_get(dj, hexfield[i]));

                if (s != NULL) hwant[i] = strtoull(s, NULL, 16);
            }

            int row_bad = 0;

            for (int i = 0; i < 14; ++i)
            {
                int bad = !skipf[i] && (ishex[i] ? (hwant[i - 4] != hgot[i - 4]) : (swant[i] != sgot[i]));

                if (!bad) continue;

                if (getenv("NW_SW_ALL") && !strcmp(sfields[i], "d.sw"))
                    printf("sw row %lld want %016llx got %016llx\n", (long long)t,
                           (unsigned long long)hwant[i - 4], (unsigned long long)hgot[i - 4]);

                ++sim_bad;
                row_bad = 1;

                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "%s", sfields[i]);

                    if (ishex[i])
                    {
                        snprintf(first_bad_want, sizeof first_bad_want, "%016llx", (unsigned long long)hwant[i - 4]);
                        snprintf(first_bad_got, sizeof first_bad_got, "%016llx", (unsigned long long)hgot[i - 4]);
                    }
                    else
                    {
                        snprintf(first_bad_want, sizeof first_bad_want, "%lld", (long long)swant[i]);
                        snprintf(first_bad_got, sizeof first_bad_got, "%lld", (long long)sgot[i]);
                    }
                }
            }

            if (trace_from >= 0 && t >= trace_from)
            {
                printf("row %lld:", (long long)t);
                int any = 0;
                for (int i = 0; i < 14; ++i)
                {
                    int bad = !skipf[i] && (ishex[i] ? (hwant[i - 4] != hgot[i - 4]) : (swant[i] != sgot[i]));
                    if (!bad) continue;
                    any = 1;
                    if (ishex[i]) printf(" %s %016llx/%016llx", sfields[i], (unsigned long long)hwant[i - 4], (unsigned long long)hgot[i - 4]);
                    else printf(" %s %lld/%lld", sfields[i], (long long)swant[i], (long long)sgot[i]);
                }
                if (!any) printf(" server fields equal");
                /* the server's entity id counter, the oracle's when the row
                 * carries it (--detail rows) */
                const struct jval *xj = json_get(row, "x");
                int64_t jid = 0;
                if (xj && json_int(json_get(xj, "nextId"), &jid))
                    printf("; nextId %lld/%d", (long long)jid, SR_DET(sr).next_id[DET_SERVER]);
                else printf("; nextId -/%d", SR_DET(sr).next_id[DET_SERVER]);
                printf(" (want/got)\n");
                if (watch_id >= 0)
                {
                    nbt *mine = native_entity(snapp, sr, sp, watch_id);
                    nbt *theirs = NULL;
                    const struct jval *en = xj ? json_get(xj, "enbt") : NULL;
                    char pre[32];
                    snprintf(pre, sizeof pre, "%d:", watch_id);
                    for (int k = 0; en && k < json_len(en) && !theirs; ++k)
                    {
                        const char *e = json_str(json_at(en, k));
                        if (e && !strncmp(e, pre, strlen(pre))) theirs = nbt_parse(e + strlen(pre));
                    }
                    if (!en) printf("  entity %d: native %s (the row has no x.enbt)\n", watch_id, mine ? "present" : "absent");
                    else if (!mine || !theirs)
                        printf("  entity %d: java %s, native %s\n", watch_id, theirs ? "present" : "absent", mine ? "present" : "absent");
                    else
                    {
                        int nd = statediff_nbt(theirs, mine, "", 0, NULL);
                        if (!nd) printf("  entity %d: equal\n", watch_id);
                        else
                        {
                            printf("  entity %d: %d NBT paths differ\n", watch_id, nd);
                            statediff_nbt(theirs, mine, "    ", 6, stdout);
                        }
                    }
                    nbt_free(mine);
                    nbt_free(theirs);
                }
            }

            if (row_bad && t == first_bad_row)
            {
                printf("row %lld server and client fields:", (long long)t);

                for (int i = 0; i < 14; ++i)
                {
                    if (skipf[i]) continue;
                    if (ishex[i]) printf(" %s=%016llx/%016llx", sfields[i], (unsigned long long)hwant[i - 4],
                                         (unsigned long long)hgot[i - 4]);
                    else printf(" %s=%lld/%lld", sfields[i], (long long)swant[i], (long long)sgot[i]);
                }

                printf("\n");
                printf("native entities (%d):", sr->d->iew.n);

                for (int i = 0; i < sr->d->iew.n; ++i)
                    printf(" %d:%d@%.2f,%.2f,%.2f", ie_ent_at(sr->d->iew.slot[i])->entity_id, ie_ent_at(sr->d->iew.slot[i])->kind,
                           ie_ent_at(sr->d->iew.slot[i])->e.pos_x, ie_ent_at(sr->d->iew.slot[i])->e.pos_y, ie_ent_at(sr->d->iew.slot[i])->e.pos_z);

                printf("\n");

                const struct jval *xj = json_get(row, "x");

                if (xj && json_get(xj, "sents"))
                {
                    const struct jval *sn = json_get(xj, "sents");
                    printf("tape entities (%d):", json_len(sn));

                    for (int i = 0; i < json_len(sn); ++i) printf(" %s", json_str(json_at(sn, i)));

                    printf("\n");
                    /* the native order over every world, as the digest walks it */
                    static struct sr_ent order[16384];
                    static int order_dim[16384];
                    int norder = 0;
                    static const int odims[3] = {0, -1, 1};
                    for (int d = 0; d < 3; ++d)
                    {
                        serverreplay_enter(sr, odims[d]);
                        for (int k = 0; k < sr->d->nents && norder < 16384; ++k)
                        {
                            if (sr->d->ents[k].pool == 3 && sp->sv.removed) continue;
                            order_dim[norder] = odims[d];
                            order[norder++] = sr->d->ents[k];
                        }
                    }
                    int common = json_len(sn) < norder ? json_len(sn) : norder;
                    for (int i = 0; i < common; ++i)
                    {
                        const char *expected = json_str(json_at(sn, i));
                        int want_id = 0, got_id;
                        unsigned long long want_hash = 0;
                        if (sscanf(expected, "%d:%*[^:]:%llx", &want_id, &want_hash) != 2) break;
                        serverreplay_enter(sr, order_dim[i]);
                        nbt *tag = sr_entry_nbt(snapp, sp, &order[i], &got_id);
                        uint64_t got_hash = nbtbin_long(tag);
                        if (want_id != got_id || want_hash != got_hash)
                        {
                            printf("first entity mismatch row %lld index %d want %d:%016llx got %d:%016llx\n",
                                   (long long)t, i, want_id, want_hash, got_id,
                                   (unsigned long long)got_hash);
                            {
                                char *debug = nbt_render(tag);
                                printf("native entity NBT: %s\n", debug);
                                free(debug);
                            }
                            nbt_free(tag);
                            break;
                        }
                        nbt_free(tag);
                    }
                    if (json_len(sn) != norder)
                    {
                        printf("entity count want %d got %d\n", json_len(sn), norder);
                        /* the surplus native entries, past the common prefix */
                        for (int i = common; i < norder && i < common + 8; ++i)
                        {
                            int gid;
                            serverreplay_enter(sr, order_dim[i]);
                            nbt *tag = sr_entry_nbt(snapp, sp, &order[i], &gid);
                            char *debug = nbt_render(tag);
                            printf("native extra %d dim %d: %s\n", gid, order_dim[i], debug);
                            free(debug);
                            nbt_free(tag);
                        }
                    }
                    serverreplay_enter(sr, serverreplay_player_dim(sr));
                }
            }
        }

        /* ---- compare the row ---- */
        const struct jval *cpj = json_get(row, "cp");
        const struct jval *spj = json_get(row, "sp");

        if (!cpj || !spj)
        {
            fail("row %lld (past the snapshot at %lld) has no cp or sp", (long long)t, (long long)snapp->tick);
            ++bad_rows;
            break;
        }

        ++sim_rows;

        /* yaw, pitch and fd are Java floats: the tape carries them through
         * Float.toString's shortest decimal, so they must be parsed back as
         * floats, not doubles */
        const char *cnames[9] = { "x", "y", "z", "mx", "my", "mz", "yaw", "pitch", "fd" };
        const int cisfloat[9] = { 0, 0, 0, 0, 0, 0, 1, 1, 1 };
        double cgots[9] = { cp->e.pos_x, cp->e.pos_y, cp->e.pos_z, cp->e.motion_x, cp->e.motion_y,
                            cp->e.motion_z, (double)cp->rotation_yaw, (double)cp->rotation_pitch,
                            (double)cp->e.fall_distance };

        for (int i = 0; i < 9; ++i)
        {
            uint64_t bits;
            double want;
            int bad = 0;

            if (!json_double(json_get(cpj, cnames[i]), &bits)) bad = 2;
            else
            {
                memcpy(&want, &bits, 8);

                if (cisfloat[i])
                {
                    char *raw = json_raw(json_get(cpj, cnames[i]));
                    float f = strtof(raw, NULL);
                    free(raw);
                    want = (double)f;
                }

                if (want != cgots[i]) bad = 1;
            }

            if (bad)
            {
                ++sim_bad;

                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "cp->%s%s", cnames[i],
                             bad == 2 ? " (missing)" : "");
                    snprintf(first_bad_want, sizeof first_bad_want, "%.17g", want);
                    snprintf(first_bad_got, sizeof first_bad_got, "%.17g", cgots[i]);
                }
            }
        }

        int64_t want_og = 0;
        json_int(json_get(cpj, "og"), &want_og);

        if (want_og != (int64_t)cp->e.on_ground)
        {
            ++sim_bad;

            if (first_bad_row < 0)
            {
                first_bad_row = t;
                snprintf(first_bad_field, sizeof first_bad_field, "cp->og");
                snprintf(first_bad_want, sizeof first_bad_want, "%lld", (long long)want_og);
                snprintf(first_bad_got, sizeof first_bad_got, "%d", cp->e.on_ground);
            }
        }

        /* the health and hunger the client was told, and the hotbar the act
         * left: cp->hp and cp->food are the S06 mirror, cp->hb the hotbar slot */
        {
            uint64_t bits;
            double want;

            if (!json_double(json_get(cpj, "hp"), &bits))
            {
                ++sim_bad;

                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "cp->hp (missing)");
                }
            }
            else
            {
                /* a Java float, printed by Float.toString: parse it as one */
                {
                    char *raw = json_raw(json_get(cpj, "hp"));
                    want = (double)strtof(raw, NULL);
                    free(raw);
                }
                if (want != (double)cp->sv.health)
                {
                    ++sim_bad;
                    if (first_bad_row < 0)
                    {
                        first_bad_row = t;
                        snprintf(first_bad_field, sizeof first_bad_field, "cp->hp");
                        snprintf(first_bad_want, sizeof first_bad_want, "%.17g", want);
                        snprintf(first_bad_got, sizeof first_bad_got, "%.17g", (double)cp->sv.health);
                    }
                }
            }

            int64_t want_food = -1;
            json_int(json_get(cpj, "food"), &want_food);

            if (want_food != cp->sv.food.level)
            {
                ++sim_bad;

                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "cp->food");
                    snprintf(first_bad_want, sizeof first_bad_want, "%lld", (long long)want_food);
                    snprintf(first_bad_got, sizeof first_bad_got, "%d", cp->sv.food.level);
                }
            }

            int64_t want_hb = -1;
            json_int(json_get(cpj, "hb"), &want_hb);

            if (want_hb != cp->hotbar)
            {
                ++sim_bad;

                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "cp->hb");
                    snprintf(first_bad_want, sizeof first_bad_want, "%lld", (long long)want_hb);
                    snprintf(first_bad_got, sizeof first_bad_got, "%d", cp->hotbar);
                }
            }

            int slot;
            char want_stack[64], got_stack[64];
            if (server_rows && inventory_diff(cpj, cp->sv.inv, 1, &slot, want_stack, sizeof want_stack,
                               got_stack, sizeof got_stack))
            {
                ++sim_bad;
                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "cp->inv[%d]", slot);
                    snprintf(first_bad_want, sizeof first_bad_want, "%s", want_stack);
                    snprintf(first_bad_got, sizeof first_bad_got, "%s", got_stack);
                }
            }

            const struct jval *curj = json_get(cpj, "cur");
            int64_t item = -1, count = 0, damage = 0;
            if (curj)
            {
                json_int(json_at(curj, 0), &item);
                json_int(json_at(curj, 1), &count);
                json_int(json_at(curj, 2), &damage);
            }
            /* InventoryPlayer.itemStack: the cursor of the container the
             * screen shows (cp_gui_container) */
            const struct craft_stack *cur = cp_gui_container(cp) ? &cp_gui_container(cp)->cursor : NULL;
            int gi = -1, gc = 0, gd = 0;
            if (cur && !craft_slot_empty(cur)) { gi = cur->item; gc = cur->count; gd = cur->damage; }
            if (server_rows && (item != gi || count != gc || damage != gd))
            {
                ++sim_bad;
                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "cp->cur");
                    snprintf(first_bad_want, sizeof first_bad_want, "%lld,%lld,%lld",
                             (long long)item, (long long)count, (long long)damage);
                    snprintf(first_bad_got, sizeof first_bad_got, "%d,%d,%d", gi, gc, gd);
                }
            }

            const char *want_gui = json_str(json_get(cpj, "gui"));
            /* the screen shows cp_gui_container: GuiInventory over the
             * player's own even while openContainer is another window */
            const char *got_gui = cp->screen_gameover ? "GuiGameOver" : cp->screen_sleep ? "GuiSleepMP" :
                cp->screen_chat ? "GuiChat" :
                cp->screen_credits ? "GuiWinGame" :
                (cp->screen_inventory ?
                    (cp_gui_container(cp) && cp_gui_container(cp)->kind == CONTAINER_WORKBENCH ?
                        "GuiCrafting" :
                     cp_gui_container(cp) && cp_gui_container(cp)->kind == CONTAINER_CHEST ?
                        "GuiChest" :
                     cp_gui_container(cp) && cp_gui_container(cp)->kind == CONTAINER_FURNACE ?
                        "GuiFurnace" :
                     cp_gui_container(cp) && cp_gui_container(cp)->kind == CONTAINER_MERCHANT ?
                        "GuiMerchant" :
                     cp_gui_container(cp) && cp_gui_container(cp)->kind == CONTAINER_DISPENSER ?
                        "GuiDispenser" :
                     cp_gui_container(cp) && cp_gui_container(cp)->kind == CONTAINER_HOPPER ?
                        "GuiHopper" : "GuiInventory") : NULL);
            if (server_rows && ((want_gui == NULL) != (got_gui == NULL) ||
                (want_gui && got_gui && strcmp(want_gui, got_gui))))
            {
                ++sim_bad;
                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "cp->gui");
                    snprintf(first_bad_want, sizeof first_bad_want, "%s", want_gui ? want_gui : "absent");
                    snprintf(first_bad_got, sizeof first_bad_got, "%s", got_gui ? got_gui : "absent");
                }
            }

            /* the client's open furnace's progress bars (cp->fur: cook, burn,
             * fuel total), the values only the S31s write; absent when the
             * client's container is not a furnace, which cp->gui already
             * pins, and on tapes recorded before the field existed */
            const struct jval *furj = json_get(cpj, "fur");
            const struct container *cfc = cp->open_container;
            int have_fur = cfc != NULL && cfc->kind == CONTAINER_FURNACE;
            if (server_rows && furj)
            {
                int64_t want[3] = {0, 0, 0};
                for (int k = 0; k < 3; ++k) json_int(json_at(furj, k), &want[k]);
                if (!have_fur || cfc->furnace_progress[0] != want[0] ||
                    cfc->furnace_progress[1] != want[1] || cfc->furnace_progress[2] != want[2])
                {
                    ++sim_bad;
                    if (first_bad_row < 0)
                    {
                        first_bad_row = t;
                        snprintf(first_bad_field, sizeof first_bad_field, "cp->fur");
                        snprintf(first_bad_want, sizeof first_bad_want, "%lld,%lld,%lld",
                                 (long long)want[0], (long long)want[1], (long long)want[2]);
                        if (have_fur)
                            snprintf(first_bad_got, sizeof first_bad_got, "%d,%d,%d",
                                     cfc->furnace_progress[0], cfc->furnace_progress[1],
                                     cfc->furnace_progress[2]);
                        else snprintf(first_bad_got, sizeof first_bad_got, "absent");
                    }
                }
            }
        }

        /* the S02 chat lines the server tick sent (Rows.s02), on tapes
         * recorded since the field (header "s02") */
        if (server_rows && json_get(tape.hdr, "s02"))
        {
            char why[512];
            if (chat_s02_check(row, s2c_sent_queue(), why, sizeof why))
            {
                ++sim_bad;
                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    printf("row %lld: %s\n", (long long)t, why);
                    snprintf(first_bad_field, sizeof first_bad_field, "s02");
                    snprintf(first_bad_want, sizeof first_bad_want, "%.60s", why);
                    snprintf(first_bad_got, sizeof first_bad_got, "(above)");
                }
            }
        }

        const char *snames[3] = { "x", "y", "z" };
        double sgots[3] = { sp->e.pos_x, sp->e.pos_y, sp->e.pos_z };

        for (int i = 0; i < 3; ++i)
        {
            uint64_t bits;
            double want;
            int bad = 0;

            if (!json_double(json_get(spj, snames[i]), &bits)) bad = 2;
            else
            {
                memcpy(&want, &bits, 8);
                if (want != sgots[i]) bad = 1;
            }

            if (bad)
            {
                ++sim_bad;

                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "sp->%s%s", snames[i],
                             bad == 2 ? " (missing)" : "");
                    snprintf(first_bad_want, sizeof first_bad_want, "%.17g", want);
                    snprintf(first_bad_got, sizeof first_bad_got, "%.17g", sgots[i]);
                }
            }
        }

        /* the survival fields the server owns: hp, food, saturation, total
         * XP and the dimension */
        {
            uint64_t bits;
            double want;

            if (!json_double(json_get(spj, "hp"), &bits))
            {
                ++sim_bad;

                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "sp->hp (missing)");
                }
            }
            else
            {
                /* a Java float, printed by Float.toString: parse it as one */
                {
                    char *raw = json_raw(json_get(spj, "hp"));
                    want = (double)strtof(raw, NULL);
                    free(raw);
                }
                if (want != (double)sp->sv.health)
                {
                    ++sim_bad;

                    if (first_bad_row < 0)
                    {
                        first_bad_row = t;
                        snprintf(first_bad_field, sizeof first_bad_field, "sp->hp");
                        snprintf(first_bad_want, sizeof first_bad_want, "%.17g", want);
                        snprintf(first_bad_got, sizeof first_bad_got, "%.17g", (double)sp->sv.health);
                    }
                }
            }

            int64_t want_food = -1;
            json_int(json_get(spj, "food"), &want_food);

            if (want_food != sp->sv.food.level)
            {
                ++sim_bad;

                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "sp->food");
                    snprintf(first_bad_want, sizeof first_bad_want, "%lld", (long long)want_food);
                    snprintf(first_bad_got, sizeof first_bad_got, "%d", sp->sv.food.level);
                }
            }

            {
                char *s = json_raw(json_get(spj, "sat"));
                float want_sat = strtof(s, NULL);
                free(s);

                if (want_sat != (double)sp->sv.food.saturation)
                {
                    ++sim_bad;

                    if (first_bad_row < 0)
                    {
                        first_bad_row = t;
                        snprintf(first_bad_field, sizeof first_bad_field, "sp->sat");
                        snprintf(first_bad_want, sizeof first_bad_want, "%.17g", (double)want_sat);
                        snprintf(first_bad_got, sizeof first_bad_got, "%.17g", (double)sp->sv.food.saturation);
                    }
                }
            }

            int64_t want_xp = -1;
            json_int(json_get(spj, "xp"), &want_xp);

            if (want_xp != sp->sv.xp_total)
            {
                ++sim_bad;

                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "sp->xp");
                    snprintf(first_bad_want, sizeof first_bad_want, "%lld", (long long)want_xp);
                    snprintf(first_bad_got, sizeof first_bad_got, "%d", sp->sv.xp_total);
                }
            }

            int64_t want_dim = 0;
            json_int(json_get(spj, "dim"), &want_dim);

            if (want_dim != sp->dimension)
            {
                ++sim_bad;

                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "sp->dim");
                    snprintf(first_bad_want, sizeof first_bad_want, "%lld", (long long)want_dim);
                    snprintf(first_bad_got, sizeof first_bad_got, "%d", sp->dimension);
                }
            }

            /* Every occupied and empty server slot, then the cursor stack. */
            int slot;
            char want_stack[64], got_stack[64];
            if (inventory_diff(spj, sp->sv.inv, server_rows, &slot, want_stack, sizeof want_stack,
                               got_stack, sizeof got_stack))
            {
                ++sim_bad;
                if (first_bad_row < 0)
                {
                    first_bad_row = t;
                    snprintf(first_bad_field, sizeof first_bad_field, "sp->inv[%d]", slot);
                    snprintf(first_bad_want, sizeof first_bad_want, "%s", want_stack);
                    snprintf(first_bad_got, sizeof first_bad_got, "%s", got_stack);
                }
            }

            /* the cursor: the row omits it when empty. The server's cursor is
             * the open container's; with no container open it is empty (the
             * inventory container's cursor is the same InventoryPlayer
             * stack). */
            {
                const struct jval *curj = json_get(spj, "cur");
                int64_t item = -1, count = 0, damage = 0;

                if (curj)
                {
                    json_int(json_at(curj, 0), &item);
                    json_int(json_at(curj, 1), &count);
                    json_int(json_at(curj, 2), &damage);
                }

                const struct craft_stack *cur = sp->open_container ? &sp->open_container->cursor : NULL;
                int gi = -1, gc = 0, gd = 0;

                if (cur && !craft_slot_empty(cur))
                {
                    gi = cur->item;
                    gc = cur->count;
                    gd = cur->damage;
                }

                if (item != gi || count != gc || damage != gd)
                {
                    ++sim_bad;

                    if (first_bad_row < 0)
                    {
                        first_bad_row = t;
                        snprintf(first_bad_field, sizeof first_bad_field, "sp->cur");
                        snprintf(first_bad_want, sizeof first_bad_want, "%lld,%lld,%lld",
                                 (long long)item, (long long)count, (long long)damage);
                        snprintf(first_bad_got, sizeof first_bad_got, "%d,%d,%d", gi, gc, gd);
                    }
                }
            }

            /* the open window: kind, grid stacks and result (sp->win) */
            {
                const struct jval *winj = json_get(spj, "win");
                if (winj && winj->kind == J_NULL) winj = NULL;
                const char *kind = winj ? json_str(json_get(winj, "kind")) : NULL;
                /* the player's own container is the default openContainer:
                 * Rows.winJson records nothing for it */
                surv_server_gui_pull(sp);
                int have_window = sp->open_container != NULL &&
                    sp->open_container->kind != CONTAINER_PLAYER;

                if ((winj != NULL) != have_window)
                {
                    ++sim_bad;

                    if (first_bad_row < 0)
                    {
                        first_bad_row = t;
                        snprintf(first_bad_field, sizeof first_bad_field, "sp->win");
                        snprintf(first_bad_want, sizeof first_bad_want, "%s",
                                 winj ? (kind ? kind : "present") : "absent");
                        snprintf(first_bad_got, sizeof first_bad_got, "%s",
                                 have_window ? "open" : "closed");
                    }
                }

                if (winj && have_window)
                {
                    /* the craft matrix: the row carries every slot, null for
                     * the empty ones */
                    const struct jval *gridj = json_get(winj, "grid");

                    if (gridj)
                    {
                        int gs = sp->open_container->grid.size;

                        if (json_len(gridj) != gs)
                        {
                            ++sim_bad;

                            if (first_bad_row < 0)
                            {
                                first_bad_row = t;
                                snprintf(first_bad_field, sizeof first_bad_field, "sp->win.grid");
                                snprintf(first_bad_want, sizeof first_bad_want, "%d slots", (int)json_len(gridj));
                                snprintf(first_bad_got, sizeof first_bad_got, "%d slots", gs);
                            }
                        }

                        for (int k = 0; k < json_len(gridj) && k < gs; ++k)
                        {
                            const struct jval *e = json_at(gridj, k);
                            int64_t item = -1, count = 0, damage = 0;
                            int row_has = e != NULL && e->kind != J_NULL;

                            if (row_has)
                            {
                                json_int(json_at(e, 0), &item);
                                json_int(json_at(e, 1), &count);
                                json_int(json_at(e, 2), &damage);
                            }

                            const struct craft_stack *g = &sp->open_container->grid.slot[k];
                            int gempty = craft_slot_empty(g);
                            int bad = row_has ? (gempty || g->item != item || g->count != count ||
                                                 g->damage != damage)
                                : !gempty;

                            if (bad)
                            {
                                ++sim_bad;

                                if (first_bad_row < 0)
                                {
                                    first_bad_row = t;
                                    snprintf(first_bad_field, sizeof first_bad_field,
                                             "sp->win.grid[%d]", k);
                                    snprintf(first_bad_want, sizeof first_bad_want, "%s",
                                             row_has ? "stack" : "null");
                                    snprintf(first_bad_got, sizeof first_bad_got, "%s",
                                             gempty ? "null" : "stack");
                                }
                            }
                        }
                    }

                    /* the result slot */
                    const struct jval *resj = json_get(winj, "res");
                    const struct craft_stack *res = container_slot(sp->open_container, 0);
                    int64_t ritem = -1, rcount = 0, rdamage = 0;
                    int row_res = resj != NULL;

                    if (resj)
                    {
                        json_int(json_at(resj, 0), &ritem);
                        json_int(json_at(resj, 1), &rcount);
                        json_int(json_at(resj, 2), &rdamage);
                    }

                    int rempty = res == NULL;
                    int rbad = row_res ? (rempty || res->item != ritem || res->count != rcount ||
                                          res->damage != rdamage)
                        : !rempty;

                    if (rbad)
                    {
                        ++sim_bad;

                        if (first_bad_row < 0)
                        {
                            first_bad_row = t;
                            snprintf(first_bad_field, sizeof first_bad_field, "sp->win.res");
                            snprintf(first_bad_want, sizeof first_bad_want, "%s",
                                     row_res ? "stack" : "null");
                            snprintf(first_bad_got, sizeof first_bad_got, "%s",
                                     rempty ? "null" : "stack");
                        }
                    }
                }
            }
        }

        /* the frame's sync: the row's deferred client light runs */
        if (ldef != NULL && lightdefer_sync(ldef)) fail("light defer: the executor failed at row %lld", (long long)t);
        if (cw_digest_out != NULL && cw_detail) cw_digest_detail(cw_digest_out, (long long)t, ssp->client_world);
        else if (cw_digest_out != NULL) fprintf(cw_digest_out, "%lld %016llx\n", (long long)t, (unsigned long long)cw_digest(ssp->client_world));
        if (row_dump != NULL)
        {
            char rec[16384];
            size_t len = rowrec_line(ssp, t, cw_from >= 0 && t >= cw_from, rec, sizeof rec);
            fwrite(rec, 1, len, row_dump);
        }

        ++nrows;
    }

    if (rc < 0) fail("the tape stopped at a malformed row");
    if (row_dump != NULL)
    {
        char rec[512];
        size_t len = rowrec_end(ssp, rec, sizeof rec);
        fwrite(rec, 1, len, row_dump);
        fclose(row_dump);
    }
    /* stopped before the tape's end (--rows, --to-row, --stop-first): the
     * end-state checks do not apply */
    int partial = rc == 1;
    PK.partial = partial;

    stats_dumps_check(&stats_dumps, nrows, sp, cp);
    if (stats_dumps.n > 0)
    {
        int in_range = 0;
        for (int i = 0; i < stats_dumps.n; ++i) in_range += stats_dumps.tick[i] >= snapp->tick && stats_dumps.tick[i] <= nrows;
        printf("stats: %d of %d oracle dumps compared (%d in the replayed rows)\n", stats_dumps.checked, stats_dumps.n, in_range);
        if (stats_dumps.checked != in_range || (!partial && !strcmp(snapdir, dir) && stats_dumps.checked != stats_dumps.n))
            fail("stats: a dump's tick is outside the replayed rows");
    }
    stats_dumps_free(&stats_dumps);
    /* An end-state snapshot beside the tape (end/, a script's last
     * {"cmd":"run","class":"Snapshot","sub":"end"}): every loaded overworld
     * chunk's Chunk.inhabitedTime against the replay's, after the last row,
     * and every container tile entity's Items list (the stacks and their
     * tags, which no row digest carries) against the native tile entity'snapp-> */
    {
        char epath[4200];
        snprintf(epath, sizeof epath, "%s/end/chunkstate.jsonl", dir);
        FILE *ef = server_rows && !partial ? fopen(epath, "rb") : NULL;

        if (ef != NULL)
        {
            char mpath[4200];
            snprintf(mpath, sizeof mpath, "%s/end/manifest.json", dir);
            FILE *mf = fopen(mpath, "rb");
            int64_t etick = -1;

            if (mf != NULL)
            {
                fseek(mf, 0, SEEK_END);
                long len = ftell(mf);
                fseek(mf, 0, SEEK_SET);
                char *text = malloc((size_t)len + 1);
                if (fread(text, 1, (size_t)len, mf) == (size_t)len)
                {
                    text[len] = 0;
                    struct jval *em = json_parse(text);
                    if (em) json_int(json_get(em, "tick"), &etick);
                    json_free(em);
                }
                else free(text);
                fclose(mf);
            }

            if (etick != nrows) fail("end snapshot at tick %lld, the tape has %lld rows", (long long)etick, (long long)nrows);

            serverreplay_enter(sr, 0);
            char line[1 << 16];
            int nchk = 0, nbad = 0, nmissing = 0, ntiles_chk = 0, ntiles_bad = 0;

            while (fgets(line, sizeof line, ef))
            {
                struct jval *v = json_parse(strdup(line));
                int64_t dim = 0, cx = 0, cz = 0, want = 0;
                if (!v) continue;
                json_int(json_get(v, "dim"), &dim);
                json_int(json_get(v, "cx"), &cx);
                json_int(json_get(v, "cz"), &cz);
                json_int(json_get(v, "inhabitedTime"), &want);
                const struct chunk *c = dim == 0 ? world_chunk(&sr->pop.world, (int)cx, (int)cz) : NULL;
                const struct jval *tiles = c != NULL ? json_get(v, "tiles") : NULL;
                for (int t = 0; tiles != NULL && t < json_len(tiles); ++t)
                {
                    char *text = json_raw(json_at(tiles, t));
                    nbt *want_te = text ? nbt_parse(text) : NULL;
                    const nbt *want_items = want_te ? nbt_get(want_te, "Items") : NULL;
                    if (want_items != NULL)
                    {
                        int x = (int)nbt_int_value(nbt_get(want_te, "x"));
                        int y = (int)nbt_int_value(nbt_get(want_te, "y"));
                        int z = (int)nbt_int_value(nbt_get(want_te, "z"));
                        const struct tile_entity *mine = NULL;
                        for (int k = 0; k < c->tes.n && mine == NULL; ++k)
                            if (c->tes.v[k]->x == x && c->tes.v[k]->y == y && c->tes.v[k]->z == z) mine = c->tes.v[k];
                        char *got_text = mine ? te_render(mine) : NULL;
                        nbt *got_te = got_text ? nbt_parse(got_text) : NULL;
                        const nbt *got_items = got_te ? nbt_get(got_te, "Items") : NULL;
                        char *w = nbt_render(want_items);
                        char *g = got_items ? nbt_render(got_items) : NULL;
                        ++ntiles_chk;
                        if (g == NULL || strcmp(w, g) != 0)
                        {
                            if (ntiles_bad++ == 0)
                                fail("end snapshot: tile entity at %d,%d,%d Items want %.600s got %.600s", x, y, z, w,
                                     g ? g : "(none)");
                        }
                        free(w);
                        free(g);
                        nbt_free(got_te);
                        free(got_text);
                    }
                    nbt_free(want_te);
                    free(text);
                }
                json_free(v);
                if (dim != 0) continue;
                ++nchk;
                if (c == NULL) { ++nmissing; continue; }
                if (c->inhabited_time != want)
                {
                    if (nbad++ == 0)
                        fail("end snapshot: chunk %lld,%lld inhabitedTime want %lld got %lld",
                             (long long)cx, (long long)cz, (long long)want, (long long)c->inhabited_time);
                }
            }

            fclose(ef);
            if (nbad) fail("end snapshot: %d of %d chunks differ in inhabitedTime", nbad, nchk);
            if (ntiles_bad) fail("end snapshot: %d of %d container tile entities differ in Items", ntiles_bad, ntiles_chk);
            printf("end snapshot: %d overworld chunks, %d inhabitedTime mismatches, %d not loaded natively, "
                   "%d container tile entities' Items, %d mismatches\n",
                   nchk, nbad, nmissing, ntiles_chk, ntiles_bad);
        }
    }

    if (state_diff && server_rows && (stop_first || max_rows > 0) && partial && (to_row < 0 || nrows != to_row))
        printf("state diff: skipped, the replay stopped at row %lld, before the end of its segment\n", (long long)nrows);
    else if (state_diff && server_rows)
    {
        struct statediff_sum sum;
        int nd = statediff_run(snapp, sr, sp, state_diff, stop_first ? 8 : 4, stdout, &sum);
        if (nd < 0) fail("state diff: %s does not load", state_diff);
        else if (nd > 0) fail("state diff: the state after row %lld differs from %s in %d places", (long long)nrows - 1, state_diff, nd);
        else printf("state diff: the state after row %lld equals %s\n", (long long)nrows - 1, state_diff);
    }

    if (first_bad_row >= 0)
        fail("replay: first differing row %lld field %s want %s got %s (%d field mismatches over %d simulated rows)",
             (long long)first_bad_row, first_bad_field, first_bad_want, first_bad_got, sim_bad, sim_rows);

    printf("tape: %lld rows read, %lld rows before the snapshot at %lld (the join prologue), %lld rows without an input snapshot, %d bad\n",
           (long long)nrows, (long long)join, (long long)snapp->tick, (long long)noinput, bad_rows);
    printf("replay: %d rows simulated from the snapshot, %d field mismatches\n", sim_rows, sim_bad);

    if (mem_on)
    {
        envmem_print(stdout, "mem start", &mem_start, 1);
        char label[64];
        snprintf(label, sizeof label, "mem peak (row %lld)", (long long)mem_peak_row);
        envmem_print(stdout, label, &mem_peak, 1);
    }
    if (ga != NULL)
    {
        genahead_report(ga, ga_gen->name, stdout);
        if (prof_on)
            printf("gen-ahead steps: %.1f M user instructions outside the tick (mean %.3f M per step, max %.1f M)\n",
                   (double)ga_ins / 1e6, (double)ga_ins / 1e6 / (double)(sim_rows ? sim_rows : 1),
                   (double)ga_ins_max / 1e6);
    }
    if (cw_digest_out != NULL) fclose(cw_digest_out);
    if (ldef != NULL)
    {
        if (lightdefer_sync(ldef)) fail("light defer: the executor failed at the end");
        lightdefer_report(ldef, stdout);
        if (ldef->st.failed) fail("light defer: an executor run failed");
        nw_env->light.defer = NULL;
        lightdefer_free(ldef);
    }
    if (lcap != NULL)
    {
        nw_env->light.cap = NULL;
        if (lightcap_finish(lcap, stdout)) fail("light capture: the device's light differs from C's (above)");
        lightcap_free(lcap);
        if (lc_file != NULL && (lc_file_piped ? pclose(lc_file) : fclose(lc_file)) != 0)
            fail("light capture: the stored capture did not close");
    }
    if (nrows == 0) fail("the tape is empty");
    if (sim_rows == 0) fail("the tape has no rows after the snapshot to compare");
    if (trace_path) trace_close();
    if (prof_on)
    {
        FILE *rows_out = prof_rows_path != NULL ? fopen(prof_rows_path, "w") : NULL;
        if (prof_rows_path != NULL && rows_out == NULL) fail("--phase-profile-rows %s does not open", prof_rows_path);
        phase_profile_summary(stdout, rows_out);
        if (rows_out != NULL) fclose(rows_out);
        nw_env->phase.on = 0;
    }
    if (PK.on && pk_summary()) fail("phase-kernel: see above");
    if (phase_on)
    {
        phase_trace_summary(stdout);
        if (phase_check && nw_env->phase.violations) fail("phase check: %s", nw_env->phase.first_violation);
        nw_env->phase.on = 0;
        if (phase_out != NULL) fclose(phase_out);
    }
    tape_close(&tape);
    snapshot_free(snapp);
    session_close(ssp);
    genahead_free(ga);
    ga_gen_free(ga_gen);
    printf("%s: %s\n", dir, fails ? "FAIL" : "OK: every row replayed");
    return fails ? 1 : 0;
}
