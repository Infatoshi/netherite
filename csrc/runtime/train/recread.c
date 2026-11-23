/* ntrec (lane/runrec): the reader of ntloop's episode recordings (rec.h's format, DIR/episodes.nrec).
 *
 *   ntrec list FILE                 the header (the config, start, fidelity, rule) and a line an episode: its index
 *                                   among the run's ended episodes, env, the env's episode number, start, steps,
 *                                   milestones, return, why kept, bytes
 *   ntrec show FILE I [N]           episode I's first N steps (default 20), a line each: the policy's action (camera
 *                                   bins, keys, pick, kind, chosen, driver), the reward, and the act the engine stepped
 *   ntrec check FILE RAW            every kept episode decoded against ntloop --rec-raw's copy of what was recorded,
 *                                   every field of every step and the episode record; and each digest
 *   ntrec stats FILE [LEVELS]       bytes an env-step (the env's buffers, packed, compressed) and the episodes
 *                                   recompressed at zstd LEVELS (a comma list, default 1,3,9,19) with the time
 *   ntrec keep FILE [--keep gain|all|none] [--ms NAME+...] [--sample X] [--rates R,...]
 *                                   a retention rule tried on a recording made with --rec-keep all: the episodes and
 *                                   env-steps it keeps, the milestones' counts, the bytes an hour at the rates
 *   ntrec tape FILE I -o SCRIPT [--world YAML] [--force]
 *                                   episode I as the oracle's script, written as neth_vec_tape writes one (a Snapshot
 *                                   line, then each world tick's act through moveact_script_line: the reset step's
 *                                   no act, then the policy's steps), and YAML the config's world section; prints the
 *                                   oracle's command. Refused: an episode from a saved start (the archive's: its state
 *                                   is another episode's replay, not a Java snapshot), a decision of more than one
 *                                   tick (the later ticks' acts are not recorded), and, unless --force, a config whose
 *                                   fidelity is not vanilla: the Java client runs vanilla 1.7.10, so a train-fast
 *                                   episode (spawning, random ticks and weather off) does not replay there; and so,
 *                                   in a ladder's run (lane/ladder: vanilla underneath), an episode whose rung's
 *                                   fidelity is not vanilla (its chunk's fid; list shows its rung and fidelity).
 *                                   lane/rawskill: an episode from a dev start made from a Java snapshot (its tape's
 *                                   header dev and a setup of Dev entries: rawstarts' starts) has the setup's lines
 *                                   after the Snapshot line, as vectape_gate's script_setup writes them, and DEV=1
 *                                   in the command
 *   ntrec end FILE I [TAPE]         episode I's end as its record holds it (the 40 items it counts); with TAPE, the
 *                                   oracle's rows of its script (tape's), the Java client's last row against it: END
 *                                   EQUAL, DIFF or INCOMPLETE (lane/polish; nether replay runs tape, the oracle, end)
 * I is the episode's index in the file (list's first column) or E:K, env E's episode K (list's env and ep columns:
 * its first episode is 0, kept or not; the same at any speed). Exit 0, 1 a
 * failed check or a bad file, 2 usage, 3 a refused tape. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <zstd.h>

#include "../../engine/tape.h"
#include "../moveact.h"
#include "../pool.h"
#include "rec.h"

struct ep {
    struct rec_chunk c;
    long off;           /* the payload's offset */
};

struct file {
    FILE *f;
    struct rec_head h;
    char *text;
    struct ep *ep;
    int n, truncated;
};

static const char *const MS[NETH_VEC_MILESTONES] = {
    "wooden_pickaxe", "stone_pickaxe", "iron_pickaxe", "diamond_pickaxe", "furnace", "iron_ingot", "bucket",
    "flint_and_steel", "obsidian", "blaze_rod", "blaze_powder", "ender_pearl", "ender_eye"};

/* a chunk's bytes in a file of this version */
static size_t chunk_bytes(uint32_t version)
{
    return version == 1 ? REC_CHUNK_V1_BYTES : version == 2 ? REC_CHUNK_V2_BYTES : sizeof(struct rec_chunk);
}

static int open_file(struct file *F, const char *path, int payload)
{
    memset(F, 0, sizeof *F);
    if (!(F->f = fopen(path, "rb"))) return fprintf(stderr, "ntrec: %s: %s\n", path, strerror(errno)), -1;
    if (fread(&F->h, sizeof F->h, 1, F->f) != 1 || memcmp(F->h.magic, REC_MAGIC, 8))
        return fprintf(stderr, "ntrec: %s: not a recording\n", path), -1;
    if (F->h.version < 1 || F->h.version > REC_VERSION || F->h.head_bytes != sizeof F->h ||
        F->h.ep_bytes != sizeof(struct neth_vec_episode) ||
        F->h.step_bytes != sizeof(struct rec_step))
        return fprintf(stderr, "ntrec: %s: format %u (header %u, record %u, step %u bytes), this reader's %d (%zu, %zu, %zu)\n",
                       path, F->h.version, F->h.head_bytes, F->h.ep_bytes, F->h.step_bytes, REC_VERSION, sizeof F->h,
                       sizeof(struct neth_vec_episode), sizeof(struct rec_step)), -1;
    F->text = malloc(F->h.text_bytes + 1);
    if (fread(F->text, 1, F->h.text_bytes, F->f) != F->h.text_bytes)
        return fprintf(stderr, "ntrec: %s: the header's text is cut\n", path), -1;
    F->text[F->h.text_bytes] = 0;
    int cap = 0;
    /* a version 1 chunk is version 2's without its rung and fid at the end (lane/ladder): no ladder, the header's
     * fidelity; a version 2 chunk version 3's without its nsub (lane/ticks-per-step): no subs */
    const size_t cb = chunk_bytes(F->h.version);
    for (;;)
    {
        struct rec_chunk c;
        memset(&c, 0, sizeof c);
        size_t got = fread(&c, 1, cb, F->f);
        if (got == 0) break;
        if (got != cb || c.magic != REC_CHUNK_MAGIC || c.chunk_bytes != cb)
        {
            F->truncated = 1;
            break;
        }
        if (F->h.version == 1) c.rung = -1, c.fid = F->h.vanilla ? 0 : REC_FID_UNKNOWN;
        long off = ftell(F->f);
        size_t len = payload ? c.zlen : (size_t)c.steps * sizeof(struct rec_step);
        if (fseek(F->f, (long)len, SEEK_CUR) || ftell(F->f) != off + (long)len)
        {
            F->truncated = 1;
            break;
        }
        /* a cut last payload: fseek past the end succeeds, the next read finds nothing */
        if (F->n == cap) F->ep = realloc(F->ep, (size_t)(cap = cap ? 2 * cap : 1024) * sizeof *F->ep);
        F->ep[F->n].c = c;
        F->ep[F->n++].off = off;
    }
    /* the last payload whole? */
    if (F->n)
    {
        struct ep *e = &F->ep[F->n - 1];
        size_t len = payload ? e->c.zlen : (size_t)e->c.steps * sizeof(struct rec_step);
        fseek(F->f, 0, SEEK_END);
        if (ftell(F->f) < e->off + (long)len) --F->n, F->truncated = 1;
    }
    return 0;
}

/* the header text's line KEY: VALUE */
static const char *hval(const struct file *F, const char *key, char *out, size_t n)
{
    size_t kl = strlen(key);
    for (const char *p = F->text; p && *p; p = strchr(p, '\n') ? strchr(p, '\n') + 1 : NULL)
    {
        if (!strncmp(p, "--- ", 4)) break;
        if (!strncmp(p, key, kl) && p[kl] == ':' && p[kl + 1] == ' ')
        {
            const char *v = p + kl + 2, *e = strchr(v, '\n');
            size_t l = e ? (size_t)(e - v) : strlen(v);
            snprintf(out, n, "%.*s", (int)(l < n ? l : n - 1), v);
            return out;
        }
    }
    snprintf(out, n, "?");
    return out;
}

/* episode I: "N" (the file's index) or "E:K" */
static int find(const struct file *F, const char *spec)
{
    const char *c = strchr(spec, ':');
    if (!c)
    {
        int i = atoi(spec);
        return i >= 0 && i < F->n ? i : -1;
    }
    int env = atoi(spec), k = atoi(c + 1);
    for (int i = 0; i < F->n; ++i)
        if (F->ep[i].c.env == env && F->ep[i].c.env_ep == k) return i;
    return -1;
}

/* episode i's steps, decoded (NULL on a bad payload); its digest checked; with subs (lane/ticks-per-step) its c->nsub
 * subs (REC_SUB_BYTES each, malloc'd; NULL with none) */
static struct rec_step *load_subs(const struct file *F, int i, int *digest_ok, uint8_t **subs)
{
    const struct rec_chunk *c = &F->ep[i].c;
    uint8_t *z = malloc(c->zlen ? c->zlen : 1), *pk = malloc(c->raw_bytes ? c->raw_bytes : 1);
    struct rec_step *s = malloc((size_t)(c->steps ? c->steps : 1) * sizeof *s);
    if (subs) *subs = NULL;
    fseek(F->f, F->ep[i].off, SEEK_SET);
    if (fread(z, 1, c->zlen, F->f) != c->zlen) goto bad;
    size_t n = ZSTD_decompress(pk, c->raw_bytes, z, c->zlen);
    if (ZSTD_isError(n) || n != c->raw_bytes) goto bad;
    *digest_ok = rec_digest(pk, n) == c->digest;
    size_t sb = (size_t)c->nsub * REC_SUB_BYTES;
    if (sb > n || rec_unpack(pk, n - sb, c->steps, c->nops, c->nexc, s)) goto bad;
    if (subs && sb)
    {
        *subs = malloc(sb);
        memcpy(*subs, pk + n - sb, sb);
    }
    free(z), free(pk);
    return s;
bad:
    free(z), free(pk), free(s);
    return NULL;
}

static struct rec_step *load(const struct file *F, int i, int *digest_ok) { return load_subs(F, i, digest_ok, NULL); }

static void ms_names(uint32_t ms, char *out, size_t n)
{
    size_t p = 0;
    out[0] = 0;
    for (int i = 0; i < NETH_VEC_MILESTONES; ++i)
        if (ms >> i & 1) p += (size_t)snprintf(out + p, n - p, "%s%s", p ? "+" : "", MS[i]);
    if (!p) snprintf(out, n, "-");
}

static const char *keep_name(uint32_t k)
{
    return k == REC_KEEP_ALL ? "all" : k == REC_KEEP_GAIN ? "gain" : k == REC_KEEP_SAMPLE ? "sample" : "?";
}

static void header(const struct file *F, const char *path)
{
    char v[1024], w[1024];
    printf("%s: %d episodes%s; %d envs in %d groups, episodes of %d ticks, %d tick%s a decision, macros %d, seed %lld\n",
           path, F->n, F->truncated ? " (the last one cut: the run did not close the file)" : "", F->h.n, F->h.groups,
           F->h.ep_len, F->h.ticks_per_step, F->h.ticks_per_step == 1 ? "" : "s", F->h.macros, (long long)F->h.seed);
    printf("config %s, start %s", hval(F, "config", v, sizeof v), hval(F, "start", w, sizeof w));
    printf(", fidelity %s%s\n", hval(F, "fidelity", v, sizeof v),
           F->h.vanilla ? "" : " (not vanilla: its episodes do not replay on the Java client)");
    printf("rule: %s\n", hval(F, "rule", v, sizeof v));
    if (strcmp(hval(F, "ladder", v, sizeof v), "?")) printf("ladder: %s\n", v);
}

/* an episode's fidelity code (rec.h REC_FID_*) as text: "vanilla", or what it ran without */
static void fid_text(uint32_t f, char *out, size_t n)
{
    static const char *const N[7] = {"spawning", "random_ticks", "weather", "mobs", "spawn_types", "ambient", "distant_ticks"};
    size_t p = 0;
    out[0] = 0;
    if (f & REC_FID_UNKNOWN) { snprintf(out, n, "not-vanilla"); return; }
    for (int i = 0; i < 7; ++i)
        if (f >> i & 1) p += (size_t)snprintf(out + p, n - p, "%sno-%s", p ? "+" : "", N[i]);
    if (!p) snprintf(out, n, "vanilla");
}

static int cmd_list(const char *path)
{
    struct file F;
    if (open_file(&F, path, 1)) return 1;
    header(&F, path);
    printf("%6s %8s %5s %4s %5s %6s %10s %-6s %8s %4s %-24s %-16s  %s\n", "i", "index", "env", "ep", "start", "steps",
           "return", "kept", "bytes", "rung", "fidelity", "digest", "milestones");
    for (int i = 0; i < F.n; ++i)
    {
        const struct rec_chunk *c = &F.ep[i].c;
        char m[512], fd[96];
        ms_names(c->rec.ms, m, sizeof m);
        fid_text(c->fid, fd, sizeof fd);
        printf("%6d %8llu %5d %4d %5d %6u %10.3f %-6s %8u %4d %-24s %016llx  %s%s\n", i, (unsigned long long)c->index,
               c->env, c->env_ep, c->rec.start, c->steps, c->rec.ret, keep_name(c->keep),
               c->zlen + (unsigned)chunk_bytes(F.h.version), c->rung, fd,
               (unsigned long long)c->digest, m, c->rec.level ? " (from a saved start)" : "");
    }
    return 0;
}

static void step_line(const struct rec_step *s, int t)
{
    printf("%5d cam %2d %2d keys ", t, s->action[0], s->action[1]);
    for (int j = 2; j < NETH_VEC_ACTION; ++j) putchar('0' + (s->action[j] & 1));
    printf(" pick %d kind %d chosen %d driver %d reward %.6g | hold %06x press ", s->pick, s->kind, s->chosen,
           s->driver, (double)s->reward, s->act.hold);
    int any = 0;
    for (int j = 0; j < NETH_RL_KEYS; ++j)
        if (s->act.press[j]) printf("%s%d*%d", any++ ? "," : "", j, s->act.press[j]);
    if (!any) putchar('-');
    printf(" look %d %.9g %.9g hotbar %d ops %d", s->act.look_mode, (double)s->act.look[0], (double)s->act.look[1],
           s->act.hotbar, s->act.nops);
    for (int j = 0; j < s->act.nops && j < NETH_RL_OPS; ++j)
        printf(" [%d %d %d %d %d %d]", s->act.ops[j].kind, s->act.ops[j].window, s->act.ops[j].slot,
               s->act.ops[j].button, s->act.ops[j].mode, s->act.ops[j].index);
    putchar('\n');
}

static int cmd_show(const char *path, const char *spec, int nshow)
{
    struct file F;
    if (open_file(&F, path, 1)) return 1;
    int i = find(&F, spec), ok = 0;
    if (i < 0) return fprintf(stderr, "ntrec: no episode %s in %s\n", spec, path), 2;
    struct rec_step *s = load(&F, i, &ok);
    if (!s) return fprintf(stderr, "ntrec: episode %d: a bad payload\n", i), 1;
    const struct rec_chunk *c = &F.ep[i].c;
    printf("episode %d: env %d's %d-th, start %d, %u steps, return %.6f, digest %s\n", i, c->env, c->env_ep,
           c->rec.start, c->steps, c->rec.ret, ok ? "ok" : "WRONG");
    for (int t = 0; t < (int)c->steps && t < nshow; ++t) step_line(&s[t], t);
    free(s);
    return ok ? 0 : 1;
}

/* the first field of two steps that differs, or NULL */
static const char *step_diff(const struct rec_step *a, const struct rec_step *b)
{
    for (int j = 0; j < NETH_VEC_ACTION; ++j)
        if (a->action[j] != b->action[j]) return j == 0 ? "action[0] (pitch bin)" : j == 1 ? "action[1] (yaw bin)" : "action[key]";
    if (a->pick != b->pick) return "pick";
    if (a->kind != b->kind) return "kind";
    if (a->chosen != b->chosen) return "chosen";
    if (a->driver != b->driver) return "driver";
    if (a->trunc != b->trunc) return "truncation";
    if (memcmp(&a->reward, &b->reward, 4)) return "reward";
    if (a->act.hold != b->act.hold) return "act.hold";
    if (memcmp(a->act.press, b->act.press, sizeof a->act.press)) return "act.press";
    if (a->act.look_mode != b->act.look_mode) return "act.look_mode";
    if (memcmp(a->act.look, b->act.look, sizeof a->act.look)) return "act.look";
    if (a->act.hotbar != b->act.hotbar) return "act.hotbar";
    if (a->act.nops != b->act.nops) return "act.nops";
    for (int j = 0; j < a->act.nops && j < NETH_RL_OPS; ++j)
        if (memcmp(&a->act.ops[j], &b->act.ops[j], sizeof a->act.ops[j])) return "act.ops";
    return NULL;
}

static int cmd_check(const char *path, const char *raw)
{
    struct file F, R;
    if (open_file(&F, path, 1) || open_file(&R, raw, 0)) return 1;
    int bad = 0;
    long steps = 0;
    if (F.n != R.n)
    {
        printf("FAIL: %s holds %d episodes, %s %d\n", path, F.n, raw, R.n);
        return 1;
    }
    for (int i = 0; i < F.n && bad < 5; ++i)
    {
        const struct rec_chunk *c = &F.ep[i].c, *r = &R.ep[i].c;
        int ok = 0;
        struct rec_step *s = load(&F, i, &ok);
        if (!s)
        {
            printf("FAIL episode %d: a bad payload\n", i);
            ++bad;
            continue;
        }
        if (!ok) printf("FAIL episode %d: the digest\n", i), ++bad;
        if (c->index != r->index || c->env != r->env || c->env_ep != r->env_ep || c->steps != r->steps ||
            c->keep != r->keep || c->digest != r->digest || c->rung != r->rung || c->fid != r->fid ||
            memcmp(&c->rec, &r->rec, sizeof c->rec))
            printf("FAIL episode %d: its chunk or record differs from the copy's\n", i), ++bad;
        struct rec_step *w = malloc((size_t)(r->steps ? r->steps : 1) * sizeof *w);
        fseek(R.f, R.ep[i].off, SEEK_SET);
        if (fread(w, sizeof *w, r->steps, R.f) != r->steps) printf("FAIL episode %d: the copy is cut\n", i), ++bad;
        else
            for (uint32_t t = 0; t < c->steps && t < r->steps; ++t)
            {
                struct rec_step x = w[t];
                rec_canon(&x);
                const char *d = step_diff(&s[t], &x);
                if (d || memcmp(&s[t], &x, sizeof x))
                {
                    printf("FAIL episode %d (env %d) step %u: %s\n", i, c->env, t, d ? d : "padding");
                    ++bad;
                    break;
                }
                ++steps;
            }
        free(w), free(s);
    }
    if (bad) return printf("NTREC-CHECK FAIL\n"), 1;
    printf("%d episodes, %ld steps: every field of every step, every episode record and digest equal to the copy's\n",
           F.n, steps);
    printf("NTREC-CHECK PASS\n");
    return 0;
}

static double mono(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

static int cmd_stats(const char *path, const char *levels)
{
    struct file F;
    if (open_file(&F, path, 1)) return 1;
    header(&F, path);
    double steps = 0, pk = 0, z = 0, chunk = 0;
    int lv[16], nl = 0;
    char b[256];
    snprintf(b, sizeof b, "%s", levels);
    for (char *sv, *t = strtok_r(b, ",", &sv); t && nl < 16; t = strtok_r(NULL, ",", &sv)) lv[nl++] = atoi(t);
    double zl[16] = {0}, tl[16] = {0}, td[16] = {0}, craw[16] = {0}, cz[16] = {0};
    const char *cn[16];
    int nc = 0;
    ZSTD_CCtx *cc = ZSTD_createCCtx();
    ZSTD_DCtx *dc = ZSTD_createDCtx();
    int keep[3] = {0, 0, 0};
    for (int i = 0; i < F.n; ++i)
    {
        const struct rec_chunk *c = &F.ep[i].c;
        steps += c->steps, pk += c->raw_bytes, z += c->zlen, chunk += sizeof *c;
        keep[c->keep == REC_KEEP_GAIN ? 0 : c->keep == REC_KEEP_SAMPLE ? 1 : 2]++;
        if (!nl) continue;
        int ok;
        struct rec_step *s = load(&F, i, &ok);
        if (!s) return fprintf(stderr, "ntrec: episode %d: a bad payload\n", i), 1;
        uint8_t *p = malloc(rec_packed_max(c->steps));
        uint32_t no, ne;
        size_t n = rec_pack(s, c->steps, p, &no, &ne);
        size_t cap = ZSTD_compressBound(n);
        uint8_t *q = malloc(cap), *u = malloc(n ? n : 1);
        /* each column alone at zstd 3: where the bytes are */
        size_t off[16], len[16];
        nc = rec_columns(c->steps, no, ne, off, len, cn);
        for (int j = 0; j < nc; ++j)
        {
            craw[j] += (double)len[j];
            if (len[j]) cz[j] += (double)ZSTD_compressCCtx(cc, q, cap, p + off[j], len[j], 3);
        }
        for (int k = 0; k < nl; ++k)
        {
            double t0 = mono();
            size_t m = ZSTD_compressCCtx(cc, q, cap, p, n, lv[k]);
            double t1 = mono();
            ZSTD_decompressDCtx(dc, u, n, q, m);
            zl[k] += (double)m, tl[k] += t1 - t0, td[k] += mono() - t1;
        }
        free(p), free(q), free(u), free(s);
    }
    double S = steps > 0 ? steps : 1;
    printf("%d episodes (%d gained a milestone, %d sampled, %d kept as all), %.0f env-steps\n", F.n, keep[0], keep[1],
           keep[2], steps);
    printf("bytes an env-step: the env's buffers %zu (struct rec_step %zu), packed %.2f, compressed %.2f (%.2fx the "
           "packed, %.1fx the buffers), with the chunk headers %.2f\n",
           (size_t)NETH_VEC_ACTION * 4 + 4 + 4 + 4 + sizeof(struct neth_rl_act), sizeof(struct rec_step), pk / S, z / S,
           z > 0 ? pk / z : 0, z > 0 ? (double)(NETH_VEC_ACTION * 4 + 12 + sizeof(struct neth_rl_act)) * S / z : 0,
           (z + chunk) / S);
    for (int k = 0; k < nl; ++k)
        printf("  zstd %2d: %.2f bytes an env-step, %.2fx; compress %.0f M env-steps/s (%.1f MB/s), decompress %.0f M "
               "env-steps/s\n", lv[k], zl[k] / S, zl[k] > 0 ? pk / zl[k] : 0, S / (tl[k] > 0 ? tl[k] : 1) / 1e6,
               pk / (tl[k] > 0 ? tl[k] : 1) / 1e6, S / (td[k] > 0 ? td[k] : 1) / 1e6);
    if (nc)
    {
        printf("  by column (packed, and alone at zstd 3; bytes an env-step):");
        for (int j = 0; j < nc; ++j) printf("%s %s %.2f %.3f", j ? "," : "", cn[j], craw[j] / S, cz[j] / S);
        printf("\n");
    }
    ZSTD_freeCCtx(cc);
    ZSTD_freeDCtx(dc);
    return 0;
}

/* a rule tried on a recording made with --rec-keep all: what it keeps, and the bytes an hour at the given rates */
static int cmd_keep(const char *path, const char *keep, const char *ms, double sample, const char *rates)
{
    struct file F;
    if (open_file(&F, path, 1)) return 1;
    struct rec_rule u = {0, sample, 3, REC_MS_DEFAULT};
    u.keep = !strcmp(keep, "all") ? REC_KEEP_ALL : !strcmp(keep, "gain") ? REC_KEEP_GAIN : 0;
    if (ms)
    {
        u.ms = 0;
        char b[512];
        snprintf(b, sizeof b, "%s", ms);
        for (char *sv, *t = strtok_r(b, "+", &sv); t; t = strtok_r(NULL, "+", &sv))
        {
            int k = -1;
            for (int i = 0; i < NETH_VEC_MILESTONES; ++i)
                if (!strcmp(t, MS[i])) k = i;
            if (k < 0) return fprintf(stderr, "ntrec keep: no such milestone %s\n", t), 2;
            u.ms |= 1u << k;
        }
    }
    double n[3] = {0}, st[3] = {0}, zb[3] = {0}, all_st = 0, all_z = 0;
    int spawn = 0, ms_n[NETH_VEC_MILESTONES] = {0}, ms_sp[NETH_VEC_MILESTONES] = {0};
    for (int i = 0; i < F.n; ++i)
    {
        const struct rec_chunk *c = &F.ep[i].c;
        all_st += c->steps, all_z += c->zlen + (double)sizeof *c;
        spawn += c->rec.start == 0;
        for (int j = 0; j < NETH_VEC_MILESTONES; ++j)
            if (c->rec.ms >> j & 1) ++ms_n[j], ms_sp[j] += c->rec.start == 0;
        int k = rec_keep(&u, 0x72756e726563ull, &c->rec, c->env_ep);
        int q = k == REC_KEEP_SAMPLE ? 1 : k ? 0 : 2;
        n[q] += 1, st[q] += c->steps, zb[q] += c->zlen + (double)sizeof *c;
    }
    header(&F, path);
    printf("%d episodes (%d from the config's start, %d from saved ones), %.0f env-steps; milestones held at the end "
           "(from the start):", F.n, spawn, F.n - spawn, all_st);
    for (int j = 0; j < NETH_VEC_MILESTONES; ++j)
        if (ms_n[j]) printf(" %s %d (%d)", MS[j], ms_n[j], ms_sp[j]);
    printf("\nthe rule keep %s, milestones 0x%x, sample %g: %.0f kept by the rule (%.1f%% of episodes), %.0f sampled "
           "(%.1f%%), %.0f left (%.1f%%)\n", keep, u.ms, sample, n[0], 100 * n[0] / (F.n ? F.n : 1), n[1],
           100 * n[1] / (F.n ? F.n : 1), n[2], 100 * n[2] / (F.n ? F.n : 1));
    double kept_z = zb[0] + zb[1], share = all_st > 0 ? (st[0] + st[1]) / all_st : 0,
           bps = all_st > 0 ? kept_z / all_st : 0;
    printf("kept: %.1f%% of the env-steps, %.0f of %.0f bytes (%.2f bytes an env-step of the run, %.2f a kept one)\n",
           100 * share, kept_z, all_z, bps, st[0] + st[1] > 0 ? kept_z / (st[0] + st[1]) : 0);
    char b[256];
    snprintf(b, sizeof b, "%s", rates);
    printf("bytes an hour:");
    for (char *sv, *t = strtok_r(b, ",", &sv); t; t = strtok_r(NULL, ",", &sv))
    {
        double r = atof(t);
        printf(" %.0fk env-steps/s %.2f GB (all %.2f GB);", r / 1e3, bps * r * 3600 / 1e9,
               all_st > 0 ? all_z / all_st * r * 3600 / 1e9 : 0);
    }
    printf("\n");
    return 0;
}

/* vecenv.c to_pool, neth_rl_step_ticks' conversion: the act hook's out for a policy step */
static void to_pool(const struct neth_rl_act *s, struct pool_act *a)
{
    memset(a, 0, sizeof *a);
    a->kind = PA_AGENT;
    a->hold = s->hold;
    memcpy(a->press, s->press, sizeof a->press);
    a->look_mode = s->look_mode == 1 ? PL_LOOK : s->look_mode == 2 ? PL_DLOOK : PL_NONE;
    a->look[0] = s->look[0];
    a->look[1] = s->look[1];
    a->hotbar = s->hotbar;
    a->nops = s->nops < 0 ? 0 : s->nops > POOL_GUI_OPS ? POOL_GUI_OPS : s->nops;
    for (int j = 0; j < a->nops; ++j)
    {
        a->ops[j].kind = s->ops[j].kind;
        a->ops[j].window = s->ops[j].window;
        a->ops[j].slot = s->ops[j].slot;
        a->ops[j].button = s->ops[j].button;
        a->ops[j].mode = s->ops[j].mode;
        a->ops[j].index = s->ops[j].index;
    }
}

/* the config text's world section as a config of its own (a run file's sim.world de-indented) */
/* the config's world section to path; seed >= 0 (lane/starts: a mix start of another world seed) in place of its
 * seed line */
static int world_yaml(const struct file *F, const char *path, long long seed)
{
    const char *t = strstr(F->text, "\n--- ");
    if (!t || !(t = strchr(t + 1, '\n'))) return -1;
    FILE *o = fopen(path, "w");
    if (!o) return -1;
    int in = -1;
    for (const char *p = t + 1; *p;)
    {
        const char *e = strchr(p, '\n');
        size_t l = e ? (size_t)(e - p) : strlen(p);
        int ind = 0;
        while (ind < (int)l && p[ind] == ' ') ++ind;
        int blank = ind == (int)l || p[ind] == '#';
        if (in < 0 && !blank && l - (size_t)ind == 6 && !strncmp(p + ind, "world:", 6)) in = ind, fputs("world:\n", o);
        else if (in >= 0 && !blank)
        {
            if (ind <= in) break;
            if (seed >= 0 && !strncmp(p + ind, "seed:", 5)) fprintf(o, "  seed: %lld\n", seed);
            else fprintf(o, "  %.*s\n", (int)(l - (size_t)ind), p + ind);
        }
        p = e ? e + 1 : p + l;
    }
    return fclose(o) || in < 0 ? -1 : 0;
}

/* lane/starts: where the oracle begins an episode that began from snapshot `name` (a directory, or a name as
 * pipeline.start takes one: configs/checkpoints.tsv's, else out/java/snapshots/NAME, under this tree's root): the
 * snapshot's own start as its tape's header says, a fresh world of its seed (from "") or a checkpoint's join (from
 * that checkpoint; one whose recorded path is gone, a lane's tree since removed, by its name under
 * out/java/checkpoints/SEED). 0; -1 not found (why); 3 refused (why: a dev start, a lost checkpoint). lane/rawskill:
 * *dev 1 for a dev start made from a Java snapshot (the tape's header dev, the manifest not), its directory in sdir */
static int start_from(const char *name, long long *seed, char *from, size_t fn, char *why, size_t wn, int *dev,
                      char *sdir, size_t sn, int force)
{
    char root[4096] = ".", exe[4096], dir[4096];
    ssize_t l = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (l > 0)
    {
        exe[l] = 0;
        char *s = strstr(exe, "/out/runtime/");
        if (s) *s = 0, snprintf(root, sizeof root, "%s", exe);
    }
    from[0] = 0;
    char tp[4200];
    snprintf(tp, sizeof tp, "%s/tape.jsonl", name);
    if (access(tp, R_OK) == 0)
        snprintf(dir, sizeof dir, "%s", name);
    else
    {
        const char *b = strrchr(name, '/');
        b = b ? b + 1 : name;
        dir[0] = 0;
        char tsv[4200], line[4096];
        snprintf(tsv, sizeof tsv, "%s/configs/checkpoints.tsv", root);
        FILE *f = fopen(tsv, "r");
        while (f && !dir[0] && fgets(line, sizeof line, f))
        {
            if (line[0] == '#') continue;
            char *t2 = strchr(line, '\t');
            if (!t2) continue;
            *t2++ = 0;
            char *e = strchr(t2, '\t');
            if (e) *e = 0;
            if (!strcmp(line, b)) snprintf(dir, sizeof dir, "%.2000s/%.2000s", root, t2);
        }
        if (f) fclose(f);
        if (!dir[0]) snprintf(dir, sizeof dir, "%.2000s/out/java/snapshots/%.1000s", root, b);
        snprintf(tp, sizeof tp, "%s/tape.jsonl", dir);
        if (access(tp, R_OK)) return snprintf(why, wn, "no snapshot (not a directory, not in configs/checkpoints.tsv, "
                                              "not out/java/snapshots/%.200s)", b), -1;
    }
    char mp[4200], man[8192] = "";
    snprintf(mp, sizeof mp, "%s/manifest.json", dir);
    FILE *mf = fopen(mp, "r");
    size_t ml = mf ? fread(man, 1, sizeof man - 1, mf) : 0;
    if (mf) fclose(mf);
    man[ml] = 0;
    if (!force && strstr(man, "\"dev\":true"))
        return snprintf(why, wn, "%.300s is a dev start: its state comes from its script's dev ops, which the oracle "
                        "replays with DEV=1 before the episode's acts (--force allows checkpoint-backed dev starts)", dir), 3;
    FILE *tf = fopen(tp, "r");
    char *line = NULL;
    size_t cap = 0;
    int ok = 0;
    char cpd[2048] = "";
    if (tf && getline(&line, &cap, tf) > 0)
    {
        struct jval *h = json_parse(line);
        int64_t s;
        if (h && json_int(json_get(h, "seed"), &s)) *seed = s, ok = 1;
        const struct jval *st = h ? json_get(h, "start") : NULL;
        const char *kind = st ? json_str(json_get(st, "kind")) : NULL, *d = st ? json_str(json_get(st, "dir")) : NULL;
        if (kind && !strcmp(kind, "checkpoint") && d) snprintf(cpd, sizeof cpd, "%s", d);
        /* lane/rawskill: a dev start made from a Java snapshot (its header's dev and setup, the manifest not dev) */
        const struct jval *dv = h ? json_get(h, "dev") : NULL;
        if (dv && dv->kind == J_BOOL && dv->boolean) *dev = 1;
        json_free(h);
        line = NULL;
    }
    free(line);
    if (tf) fclose(tf);
    if (!ok) return snprintf(why, wn, "%.300s/tape.jsonl has no header with its seed", dir), 3;
    if (cpd[0])
    {
        char m2[2200];
        snprintf(m2, sizeof m2, "%s/manifest.json", cpd);
        if (access(m2, R_OK))
        {
            const char *b = strrchr(cpd, '/');
            char alt[4200];
            snprintf(alt, sizeof alt, "%.2000s/out/java/checkpoints/%lld/%.200s", root, *seed, b ? b + 1 : cpd);
            snprintf(m2, sizeof m2, "%.2100s/manifest.json", alt);
            if (access(m2, R_OK))
                return snprintf(why, wn, "its checkpoint %.300s is gone (and not under out/java/checkpoints/%lld)", cpd,
                                *seed), 3;
            snprintf(cpd, sizeof cpd, "%.2000s", alt);
        }
        snprintf(from, fn, "%s", cpd);
    }
    snprintf(sdir, sn, "%s", dir);
    return 0;
}

/* lane/rawskill: a dev start's setup (its tape header's "setup":[{"class":"Dev","tick":T,"cmd":{...}}, ...]) as the
 * oracle script's lines, {"cmd":"dev",...} each in its order (vectape_gate.c script_setup's rule); how many, -1 not
 * read */
static int setup_lines(const char *dir, FILE *o)
{
    char path[4200];
    snprintf(path, sizeof path, "%.4000s/tape.jsonl", dir);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char *hdr = NULL;
    size_t cap = 0;
    ssize_t l = getline(&hdr, &cap, f);
    fclose(f);
    if (l <= 0) { free(hdr); return -1; }
    const char *p = strstr(hdr, "\"setup\":["), *send = NULL;
    if (p)
    {
        int depth = 0, instr = 0;
        for (send = p + 8; *send; ++send)
        {
            if (instr) { if (*send == '\\' && send[1]) ++send; else if (*send == '"') instr = 0; }
            else if (*send == '"') instr = 1;
            else if (*send == '[' || *send == '{') ++depth;
            else if ((*send == ']' || *send == '}') && --depth == 0) break;
        }
    }
    int n = 0;
    while (p && (p = strstr(p, "\"cmd\":{")) != NULL && p < send)
    {
        const char *b = p + 7, *q = b;
        int depth = 1, instr = 0;
        for (; *q && depth > 0; ++q)
        {
            if (instr) { if (*q == '\\' && q[1]) ++q; else if (*q == '"') instr = 0; }
            else if (*q == '"') instr = 1;
            else if (*q == '{' || *q == '[') ++depth;
            else if (*q == '}' || *q == ']') --depth;
        }
        if (depth) break;
        fprintf(o, "{\"cmd\":\"dev\",%.*s}\n", (int)(q - 1 - b), b);
        ++n;
        p = q;
    }
    free(hdr);
    return n;
}

static int cmd_tape(const char *path, const char *spec, const char *out, const char *world, int force)
{
    struct file F;
    if (open_file(&F, path, 1)) return 1;
    int i = find(&F, spec), ok = 0;
    if (i < 0) return fprintf(stderr, "ntrec: no episode %s in %s\n", spec, path), 2;
    const struct rec_chunk *c = &F.ep[i].c;
    char fid[256], start[1024], spath[2048];
    hval(&F, "fidelity", fid, sizeof fid);
    hval(&F, "start", start, sizeof start);
    hval(&F, "start_path", spath, sizeof spath);
    /* lane/starts: a start of the mix (the seed set's, the library's) is a Java snapshot: its seed is its tape's, and
     * a library start's checkpoint (LIB/ID/cp beside LIB/ID/start) is where the oracle begins (FROM) */
    long long seed = (long long)F.h.seed;
    int mixed = 0, dev = 0;             /* lane/rawskill: dev, a dev start made from a Java snapshot (sdir) */
    char sdir[4096] = "";
    {
        /* the snapshot the episode began from: a mix start's path (mix_K), else the config's start by name or path */
        char key[32], mp[2048] = "?", why[512];
        if (c->rec.start > 0)
        {
            snprintf(key, sizeof key, "mix_%d", c->rec.start);
            hval(&F, key, mp, sizeof mp);
        }
        else
            snprintf(mp, sizeof mp, "%s", strcmp(spath, "?") && spath[0] ? spath : start);
        if (strcmp(mp, "?"))
        {
            long long sd = seed;
            char cp[2048];
            int r = start_from(mp, &sd, cp, sizeof cp, why, sizeof why, &dev, sdir, sizeof sdir, force);
            if (r == 3) return printf("REFUSED: episode %d's start %s: %s\n", i, mp, why), 3;
            if (r == 0)
            {
                seed = sd, mixed = 1;
                snprintf(start, sizeof start, "%s", cp[0] ? "checkpoint" : "spawn");
                snprintf(spath, sizeof spath, "%s", cp);
            }
            else if (c->rec.start > 0)
                return printf("REFUSED: episode %d's start %s: %s\n", i, mp, why), 3;
        }
    }
    if (c->rec.start != 0 && !mixed)
    {
        printf("REFUSED: episode %d began from start %d, a start saved from another episode (the archive's): its "
               "state is that episode's replay, not a Java snapshot, so it has no tape\n", i, c->rec.start);
        return 3;
    }
    const int T = F.h.ticks_per_step > 1 ? F.h.ticks_per_step : 1;
    if (T > 1 && F.h.version < 3)
    {
        printf("REFUSED: a decision of %d ticks in a version %u recording: it holds each decision's first tick's act, "
               "not the later ticks' a macro made (version 3's subs), so it has no tape\n", T, F.h.version);
        return 3;
    }
    if (!F.h.vanilla && !force)
    {
        printf("REFUSED: the run's fidelity is %s, not vanilla: the Java client runs vanilla 1.7.10, so this episode "
               "does not replay there row for row (--force writes the script anyway)\n", fid);
        return 3;
    }
    /* lane/ladder: a ladder's run is vanilla underneath, its episodes each at their rung's fidelity */
    if (c->fid != 0 && !force)
    {
        char fd[96];
        fid_text(c->fid, fd, sizeof fd);
        printf("REFUSED: episode %d ran at the ladder's rung %d, fidelity %s, not vanilla: the Java client runs vanilla "
               "1.7.10, so this episode does not replay there row for row (--force writes the script anyway)\n", i,
               c->rung, fd);
        return 3;
    }
    uint8_t *sub = NULL;
    struct rec_step *s = load_subs(&F, i, &ok, &sub);
    if (!s || !ok) return fprintf(stderr, "ntrec: episode %d: %s\n", i, s ? "the digest is wrong" : "a bad payload"), 1;
    FILE *o = fopen(out, "w");
    if (!o) return fprintf(stderr, "ntrec: %s: %s\n", out, strerror(errno)), 1;
    /* neth_vec_tape's: the Snapshot line, the reset step's no act (act_noop: hotbar -1), then each step's act */
    fputs("{\"cmd\":\"run\",\"class\":\"Snapshot\"}\n", o);
    if (dev && setup_lines(sdir, o) < 0)
    {
        fclose(o);
        return fprintf(stderr, "ntrec: %s: the dev start's setup not read\n", sdir), 1;
    }
    struct neth_rl_act noop;
    memset(&noop, 0, sizeof noop);
    noop.hotbar = -1;
    struct pool_act a;
    /* lane/ticks-per-step: a step of T ticks is T lines, vecenv.c vec_act's: its act, then each later tick's sub
     * (the act a macro made) or the hold-only act of the tick before's holds; the reset step's later ticks hold
     * nothing */
    uint32_t sx = 0, bad = 0;
    for (int32_t t = -1; t < (int32_t)c->steps; ++t)
    {
        to_pool(t < 0 ? &noop : &s[t].act, &a);
        moveact_script_line(o, &a);
        for (int k = 1; k < T; ++k)
        {
            int32_t at[2] = {-1, -1};
            if (sx < c->nsub) memcpy(at, sub + (size_t)sx * REC_SUB_BYTES, 8);
            if (t >= 0 && at[0] == t && at[1] == k)
            {
                struct neth_rl_act m;
                memcpy(&m, sub + (size_t)sx++ * REC_SUB_BYTES + 8, sizeof m);
                to_pool(&m, &a);
            }
            else
            {
                uint32_t hold = a.hold;
                memset(&a, 0, sizeof a);
                a.kind = PA_AGENT;
                a.hold = hold;
                a.look_mode = PL_NONE;
                a.hotbar = -1;
            }
            moveact_script_line(o, &a);
        }
    }
    if (sx != c->nsub) bad = 1;
    free(s);
    free(sub);
    if (bad)
    {
        fclose(o);
        return fprintf(stderr, "ntrec: episode %d: %u of its %u subs out of order\n", i, c->nsub - sx, c->nsub), 1;
    }
    if (fclose(o)) return fprintf(stderr, "ntrec: %s: %s\n", out, strerror(errno)), 1;
    if (world && world_yaml(&F, world, mixed ? seed : -1))
        return fprintf(stderr, "ntrec: %s: the config's world section\n", world), 1;
    printf("episode %d (env %d's %d-th, %u steps, milestones 0x%x) to %s: %u ticks%s\n", i, c->env, c->env_ep,
           c->steps, c->rec.ms, out, (c->steps + 1) * (uint32_t)T,
           F.h.vanilla && c->fid == 0 ? "" : "; NOT VANILLA (--force): the Java client will not replay it row for row");
    printf("replay: make -C oracle script SEED=%lld CONF=%s SCRIPT=%s TAPE=../out/java/try/NAME/tape.jsonl%s%s%s\n",
           seed, world ? world : "WORLD.yaml", out, strcmp(start, "spawn") ? " FROM=" : "",
           strcmp(start, "spawn") ? spath : "", dev ? " DEV=1" : "");
    return 0;
}

/* lane/polish: the items the episode record counts at its end (vecenv.c's TEACH_ITEMS, semteach.ITEMS, then
 * EXTRA_ITEMS: record.items[] and record.extra[] in this order) */
static const int16_t END_IDS[NETH_VEC_ITEMS + 8] = {
    17, 162, 5, 280, 58, 270, 271, 269, 268, 4, 274, 275, 273, 272, 263, 15, 265, 61, 3, 12, 13, 6, 260, 295, 318, 1, 14,
    264, 56, 331, 325, 259, 326, 327, 49, 257, 278, 369, 368, 381};

/* the tape's last line (malloc'd, NUL terminated) and its row count, NULL when unreadable or empty */
static char *last_line(const char *path, long *rows)
{
    FILE *f = fopen(path, "r");
    if (!f) return NULL;
    char *line = NULL, *last = NULL;
    size_t cap = 0;
    ssize_t l;
    *rows = 0;
    while ((l = getline(&line, &cap, f)) > 0)
    {
        if (line[0] != '{' || !strncmp(line, "{\"tape\"", 7)) continue;
        ++*rows;
        free(last);
        last = strdup(line);
    }
    free(line);
    fclose(f);
    return last;
}

/* ntrec end FILE I [TAPE]: episode I's end as its record holds it (the 40 items' counts); with TAPE (the oracle's rows
 * of the episode's script, ntrec tape's), the Java client's last row against it: each item summed over every slot
 * of the server player's inventory (sp.inv: slot, id, count, damage), the items outside the 40 listed, not compared.
 * One line: EQUAL, DIFF (exit 1) or INCOMPLETE (the tape ends before the episode's last act, exit 1) */
static int cmd_end(const char *path, const char *spec, const char *tape)
{
    struct file F;
    if (open_file(&F, path, 1)) return 1;
    int i = find(&F, spec);
    if (i < 0) return fprintf(stderr, "ntrec: no episode %s in %s\n", spec, path), 2;
    const struct rec_chunk *c = &F.ep[i].c;
    int32_t rec[NETH_VEC_ITEMS + 8];
    for (int k = 0; k < NETH_VEC_ITEMS; ++k) rec[k] = c->rec.items[k];
    for (int k = 0; k < 8; ++k) rec[NETH_VEC_ITEMS + k] = c->rec.extra[k];
    char ms[512];
    ms_names(c->rec.ms, ms, sizeof ms);
    printf("episode %d (env %d's %d-th, %u steps), milestones %s; held at the end:", i, c->env, c->env_ep, c->steps,
           c->rec.ms ? ms : "none");
    for (int k = 0; k < NETH_VEC_ITEMS + 8; ++k)
        if (rec[k]) printf(" %d x%d", END_IDS[k], rec[k]);
    printf("\n");
    if (!tape) return 0;
    long rows = 0;
    char *text = last_line(tape, &rows);
    struct jval *row = text ? json_parse(text) : NULL;
    const struct jval *inv = row ? json_get(json_get(row, "sp"), "inv") : NULL;
    int64_t t = -1;
    if (!row || !json_int(json_get(row, "t"), &t) || !inv)
    {
        printf("end: %s has no row with a server player's inventory\n", tape);
        return 1;
    }
    int64_t java[NETH_VEC_ITEMS + 8] = {0}, other[4096] = {0};
    for (int s = 0; s < json_len(inv); ++s)
    {
        const struct jval *e = json_at(inv, s);
        int64_t id = -1, n = 0;
        if (!json_int(json_at(e, 1), &id) || !json_int(json_at(e, 2), &n) || id < 0 || id >= 4096) continue;
        int k = 0;
        while (k < NETH_VEC_ITEMS + 8 && END_IDS[k] != id) ++k;
        if (k < NETH_VEC_ITEMS + 8) java[k] += n;
        else other[id] += n;
    }
    json_free(row);
    /* the script's ticks: the reset step's no act and the episode's steps, after the join rows (lane/ticks-per-step:
     * T ticks a step) */
    long want = ((long)c->steps + 1) * (F.h.ticks_per_step > 1 ? F.h.ticks_per_step : 1);
    int diff = 0;
    printf("end: the Java client's last row (t %lld, %ld rows) against the record's end over %d items:",
           (long long)t, rows, NETH_VEC_ITEMS + 8);
    for (int k = 0; k < NETH_VEC_ITEMS + 8; ++k)
        if (java[k] != rec[k]) printf(" %d record %d java %lld", END_IDS[k], rec[k], (long long)java[k]), diff = 1;
    if (!diff) printf(" every count equal");
    int any = 0;
    for (int id = 0; id < 4096; ++id)
        if (other[id]) printf("%s%d x%lld", any++ ? ", " : "; the Java client also holds (not in the record) ", id,
                              (long long)other[id]);
    printf("\n");
    /* the join's two rows (t 0 and 1), then a row an act: the last act's row is t want + 1 */
    const char *state = t < want + 1 ? "INCOMPLETE" : diff ? "DIFF" : "EQUAL";
    printf("END %s (the last row t %lld, the script's %ld acts end at t %ld)\n", state, (long long)t, want, want + 1);
    return strcmp(state, "EQUAL") ? 1 : 0;
}

int main(int argc, char **argv)
{
    if (argc >= 3 && !strcmp(argv[1], "list")) return cmd_list(argv[2]);
    if ((argc == 4 || argc == 5) && !strcmp(argv[1], "end")) return cmd_end(argv[2], argv[3], argc == 5 ? argv[4] : NULL);
    if (argc >= 4 && !strcmp(argv[1], "show")) return cmd_show(argv[2], argv[3], argc > 4 ? atoi(argv[4]) : 20);
    if (argc == 4 && !strcmp(argv[1], "check")) return cmd_check(argv[2], argv[3]);
    if (argc >= 3 && !strcmp(argv[1], "stats")) return cmd_stats(argv[2], argc > 3 ? argv[3] : "1,3,9,19");
    if (argc >= 3 && !strcmp(argv[1], "keep"))
    {
        const char *keep = "gain", *ms = NULL, *rates = "180000,250000,350000";
        double sample = 0.01;
        for (int i = 3; i < argc; ++i)
        {
            if (!strcmp(argv[i], "--keep") && i + 1 < argc) keep = argv[++i];
            else if (!strcmp(argv[i], "--ms") && i + 1 < argc) ms = argv[++i];
            else if (!strcmp(argv[i], "--sample") && i + 1 < argc) sample = atof(argv[++i]);
            else if (!strcmp(argv[i], "--rates") && i + 1 < argc) rates = argv[++i];
            else return fprintf(stderr, "ntrec keep: %s?\n", argv[i]), 2;
        }
        return cmd_keep(argv[2], keep, ms, sample, rates);
    }
    if (argc >= 4 && !strcmp(argv[1], "tape"))
    {
        const char *out = NULL, *world = NULL;
        int force = 0;
        for (int i = 4; i < argc; ++i)
        {
            if (!strcmp(argv[i], "-o") && i + 1 < argc) out = argv[++i];
            else if (!strcmp(argv[i], "--world") && i + 1 < argc) world = argv[++i];
            else if (!strcmp(argv[i], "--force")) force = 1;
            else return fprintf(stderr, "ntrec tape: %s?\n", argv[i]), 2;
        }
        if (out) return cmd_tape(argv[2], argv[3], out, world, force);
    }
    fprintf(stderr, "usage: ntrec list FILE | show FILE I [N] | check FILE RAW | stats FILE [LEVELS] | keep FILE [--keep "
                    "gain|all|none] [--ms NAME+...] [--sample X] [--rates R,...] | tape FILE I -o SCRIPT [--world YAML] "
                    "[--force] | end FILE I [TAPE]   (I: the file's index, or E:K env E's episode K)\n");
    return 2;
}
