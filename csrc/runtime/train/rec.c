/* The trainer's episodes as labelled data (lane/runrec): the recorder ntloop runs and the codec its reader shares
 * (rec.h has the format). */
#define _GNU_SOURCE
#include "rec.h"

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <zstd.h>

#include "../../engine/fidelity.h"

/* ---------------------------------------------------------------- the codec */

enum { C_CAM0, C_CAM1, C_KEYS, C_PICK, C_KC, C_REW, C_HOLD, C_PRESS, C_LM, C_LOOK0, C_LOOK1, C_HOT, C_NOPS, C_N };
static const uint8_t ES[C_N] = {1, 1, 2, 1, 1, 4, 4, 4, 1, 4, 4, 1, 1};
#define OP_BYTES (6 * 4)
#define EXC_BYTES (4 + sizeof(struct rec_step))

void rec_canon(struct rec_step *s)
{
    struct rec_step c;
    memset(&c, 0, sizeof c);
    memcpy(c.action, s->action, sizeof c.action);
    c.pick = s->pick;
    c.kind = s->kind, c.chosen = s->chosen, c.driver = s->driver, c.trunc = s->trunc;
    c.reward = s->reward;
    const struct neth_rl_act *a = &s->act;
    c.act.hold = a->hold;
    memcpy(c.act.press, a->press, sizeof c.act.press);
    c.act.look_mode = a->look_mode;
    c.act.look[0] = a->look[0];
    c.act.look[1] = a->look[1];
    c.act.hotbar = a->hotbar;
    c.act.nops = a->nops;
    int k = a->nops < 0 ? 0 : a->nops > NETH_RL_OPS ? NETH_RL_OPS : a->nops;
    for (int j = 0; j < k; ++j) c.act.ops[j] = a->ops[j];
    *s = c;
}

void rec_canon_act(struct neth_rl_act *a)
{
    struct rec_step s;
    memset(&s, 0, sizeof s);
    s.act = *a;
    rec_canon(&s);
    memcpy(a, &s.act, sizeof *a);
}

/* FNV-1a over 64-bit words, the tail's bytes one a word */
uint64_t rec_digest(const void *p, size_t n)
{
    const uint8_t *b = p;
    uint64_t h = 0xcbf29ce484222325ull, w;
    size_t i = 0;
    for (; i + 8 <= n; i += 8)
    {
        memcpy(&w, b + i, 8);
        h = (h ^ w) * 0x100000001b3ull;
    }
    for (; i < n; ++i) h = (h ^ b[i]) * 0x100000001b3ull;
    return h;
}

static uint64_t mix64(uint64_t z)
{
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
    return z ^ (z >> 31);
}

int rec_keep(const struct rec_rule *u, uint64_t seed, const struct neth_vec_episode *e, int env_ep)
{
    if (u->keep & REC_KEEP_ALL) return REC_KEEP_ALL;
    if ((u->keep & REC_KEEP_GAIN) && (e->ms & u->ms) && (e->start == 0 || __builtin_popcount(e->ms) > e->level))
        return REC_KEEP_GAIN;
    if (u->sample > 0)
    {
        uint64_t z = mix64(seed ^ mix64((uint64_t)(uint32_t)e->env * 0x9e3779b97f4a7c15ull + (uint64_t)(uint32_t)env_ep));
        if ((double)(z >> 11) * 0x1.0p-53 < u->sample) return REC_KEEP_SAMPLE;
    }
    return 0;
}

/* an episode's columns, grown as its steps come */
struct cols {
    uint32_t S, cap, nops, opcap, nexc, exccap;
    uint8_t *c[C_N];
    int32_t *ops;
    uint8_t *exc;
    uint32_t nsub, subcap;      /* lane/ticks-per-step: the subs (REC_SUB_BYTES each) */
    uint8_t *sub;
};

static void cols_reset(struct cols *k) { k->S = k->nops = k->nexc = k->nsub = 0; }

static void cols_free(struct cols *k)
{
    for (int i = 0; i < C_N; ++i) free(k->c[i]);
    free(k->ops), free(k->exc), free(k->sub);
    memset(k, 0, sizeof *k);
}

/* n subs (REC_SUB_BYTES each, the payload's form) onto a list */
static void subs_add(uint8_t **sub, uint32_t *nsub, uint32_t *cap, const uint8_t *src, uint32_t n)
{
    if (!n) return;
    if (*nsub + n > *cap)
    {
        uint32_t c = *cap ? 2 * *cap : 256;
        while (c < *nsub + n) c *= 2;
        *sub = realloc(*sub, (size_t)c * REC_SUB_BYTES);
        *cap = c;
    }
    memcpy(*sub + (size_t)*nsub * REC_SUB_BYTES, src, (size_t)n * REC_SUB_BYTES);
    *nsub += n;
}

/* one step packed: the columns' values side by side (the main thread makes it, the writer spreads it) */
struct row {
    uint8_t cam0, cam1;
    uint16_t keys;
    uint8_t pick, kc, lm, nops;
    float reward;
    uint32_t hold, press;
    float look0, look1;
    int8_t hotbar;
    uint8_t exc;                /* 1: the step does not fit the columns (its whole struct rec_step follows) */
    uint8_t trunc, pad;         /* trunc: the env's truncation (the episode's last step) */
};
_Static_assert(sizeof(struct row) == 32, "a row is 32 bytes");

/* the step's row from its parts as the env's buffers hold them; its ops (six i32 each) into ops; 1 when the
 * columns cannot hold it (trunc is the last step's alone, so a trunc past 1 is one too) */
static int make_row(struct row *w, int32_t *ops, const int32_t *action, int32_t pick, uint8_t kind, uint8_t chosen,
                    uint8_t driver, uint8_t trunc, float reward, const struct neth_rl_act *a)
{
    uint32_t keys = 0, wide = 0, press = 0;
    for (int j = 2; j < NETH_VEC_ACTION; ++j)
    {
        keys |= (uint32_t)(action[j] & 1) << (j - 2);
        wide |= (uint32_t)action[j] & ~1u;
    }
    for (int j = 0; j < NETH_RL_KEYS; ++j)
    {
        press |= (uint32_t)(a->press[j] & 1) << j;
        wide |= a->press[j] & ~1u;
    }
    int nops = a->nops < 0 ? 0 : a->nops > NETH_RL_OPS ? NETH_RL_OPS : a->nops;
    w->cam0 = (uint8_t)action[0];
    w->cam1 = (uint8_t)action[1];
    w->keys = (uint16_t)keys;
    w->pick = (uint8_t)pick;
    w->kc = (uint8_t)((kind & 7) | (chosen & 1) << 3 | (driver & 7) << 4);
    w->lm = (uint8_t)a->look_mode;
    w->nops = (uint8_t)nops;
    w->reward = reward;
    w->hold = a->hold;
    w->press = press;
    w->look0 = a->look[0];
    w->look1 = a->look[1];
    w->hotbar = (int8_t)a->hotbar;
    w->trunc = trunc;
    w->pad = 0;
    for (int j = 0; j < nops; ++j)
    {
        int32_t *o = ops + 6 * j;
        o[0] = a->ops[j].kind, o[1] = a->ops[j].window, o[2] = a->ops[j].slot;
        o[3] = a->ops[j].button, o[4] = a->ops[j].mode, o[5] = a->ops[j].index;
    }
    w->exc = wide || action[0] < 0 || action[0] > 255 || action[1] < 0 || action[1] > 255 || pick < 0 || pick > 255 ||
             kind > 7 || chosen > 1 || driver > 7 || trunc > 1 || a->look_mode < 0 || a->look_mode > 255 ||
             a->hotbar < -128 || a->hotbar > 127 || a->nops < 0 || a->nops > NETH_RL_OPS;
    return w->exc;
}

/* a row (its ops, and the whole step when it is an exception) onto the episode's columns */
static void cols_row(struct cols *k, const struct row *w, const int32_t *ops, const struct rec_step *exc)
{
    if (k->S == k->cap)
    {
        uint32_t nc = k->cap ? 2 * k->cap : 1024;
        for (int i = 0; i < C_N; ++i) k->c[i] = realloc(k->c[i], (size_t)nc * ES[i]);
        k->cap = nc;
    }
    uint32_t t = k->S++;
    k->c[C_CAM0][t] = w->cam0;
    k->c[C_CAM1][t] = w->cam1;
    memcpy(k->c[C_KEYS] + 2 * (size_t)t, &w->keys, 2);
    k->c[C_PICK][t] = w->pick;
    k->c[C_KC][t] = w->kc;
    memcpy(k->c[C_REW] + 4 * (size_t)t, &w->reward, 4);
    memcpy(k->c[C_HOLD] + 4 * (size_t)t, &w->hold, 4);
    memcpy(k->c[C_PRESS] + 4 * (size_t)t, &w->press, 4);
    k->c[C_LM][t] = w->lm;
    memcpy(k->c[C_LOOK0] + 4 * (size_t)t, &w->look0, 4);
    memcpy(k->c[C_LOOK1] + 4 * (size_t)t, &w->look1, 4);
    k->c[C_HOT][t] = (uint8_t)w->hotbar;
    k->c[C_NOPS][t] = w->nops;
    if (w->nops)
    {
        if (k->nops + w->nops > k->opcap)
        {
            k->opcap = k->opcap ? 2 * k->opcap : 256;
            while (k->opcap < k->nops + w->nops) k->opcap *= 2;
            k->ops = realloc(k->ops, (size_t)k->opcap * OP_BYTES);
        }
        memcpy(k->ops + 6 * (size_t)k->nops, ops, (size_t)w->nops * OP_BYTES);
        k->nops += w->nops;
    }
    if (exc)
    {
        if (k->nexc == k->exccap)
        {
            k->exccap = k->exccap ? 2 * k->exccap : 16;
            k->exc = realloc(k->exc, (size_t)k->exccap * EXC_BYTES);
        }
        uint8_t *e = k->exc + (size_t)k->nexc++ * EXC_BYTES;
        memcpy(e, &t, 4);
        memcpy(e + 4, exc, sizeof *exc);
    }
}

/* the step whole, canonical, from its parts */
static void make_step(struct rec_step *s, const int32_t *action, int32_t pick, uint8_t kind, uint8_t chosen,
                      uint8_t driver, uint8_t trunc, float reward, const struct neth_rl_act *a)
{
    memset(s, 0, sizeof *s);
    memcpy(s->action, action, sizeof s->action);
    s->pick = pick, s->kind = kind, s->chosen = chosen, s->driver = driver, s->trunc = trunc, s->reward = reward;
    s->act = *a;
    rec_canon(s);
}

static void cols_add(struct cols *k, const struct rec_step *s)
{
    struct row w;
    int32_t ops[6 * NETH_RL_OPS];
    int x = make_row(&w, ops, s->action, s->pick, s->kind, s->chosen, s->driver, s->trunc, s->reward, &s->act);
    cols_row(k, &w, ops, x ? s : NULL);
}

static size_t cols_bytes(const struct cols *k)
{
    size_t n = 0;
    for (int i = 0; i < C_N; ++i) n += (size_t)k->S * ES[i];
    return n + (size_t)k->nops * OP_BYTES + (size_t)k->nexc * EXC_BYTES + (size_t)k->nsub * REC_SUB_BYTES;
}

/* each column's elements whole (byte planes, every element's byte 0 then byte 1 ..., measured worse: 7.04 bytes an
 * env-step at zstd 3 against 5.92 on the same 2048 episodes; the look's floats come from few values, which repeat
 * whole: DEVLOG lane/runrec) */
static size_t cols_finish(const struct cols *k, uint8_t *out)
{
    size_t p = 0;
    for (int i = 0; i < C_N; ++i)
    {
        memcpy(out + p, k->c[i], (size_t)k->S * ES[i]);
        p += (size_t)k->S * ES[i];
    }
    memcpy(out + p, k->ops, (size_t)k->nops * OP_BYTES);
    p += (size_t)k->nops * OP_BYTES;
    memcpy(out + p, k->exc, (size_t)k->nexc * EXC_BYTES);
    p += (size_t)k->nexc * EXC_BYTES;
    if (k->nsub) memcpy(out + p, k->sub, (size_t)k->nsub * REC_SUB_BYTES);
    return p + (size_t)k->nsub * REC_SUB_BYTES;
}

int rec_columns(uint32_t S, uint32_t nops, uint32_t nexc, size_t off[], size_t len[], const char *names[])
{
    static const char *const N[C_N + 2] = {"cam0", "cam1", "keys", "pick", "kind", "reward", "hold", "press",
                                           "look_mode", "look0", "look1", "hotbar", "nops", "ops", "exceptions"};
    size_t p = 0;
    for (int i = 0; i < C_N + 2; ++i)
    {
        size_t l = i < C_N ? (size_t)S * ES[i] : i == C_N ? (size_t)nops * OP_BYTES : (size_t)nexc * EXC_BYTES;
        off[i] = p, len[i] = l, names[i] = N[i];
        p += l;
    }
    return C_N + 2;
}

size_t rec_packed_max(uint32_t S)
{
    size_t n = 0;
    for (int i = 0; i < C_N; ++i) n += (size_t)S * ES[i];
    return n + (size_t)S * NETH_RL_OPS * OP_BYTES + (size_t)S * EXC_BYTES;
}

size_t rec_pack(const struct rec_step *steps, uint32_t S, uint8_t *out, uint32_t *nops, uint32_t *nexc)
{
    struct cols k;
    memset(&k, 0, sizeof k);
    for (uint32_t t = 0; t < S; ++t) cols_add(&k, &steps[t]);
    size_t n = cols_finish(&k, out);
    *nops = k.nops, *nexc = k.nexc;
    cols_free(&k);
    return n;
}

int rec_unpack(const uint8_t *in, size_t len, uint32_t S, uint32_t nops, uint32_t nexc, struct rec_step *steps)
{
    size_t want = 0;
    for (int i = 0; i < C_N; ++i) want += (size_t)S * ES[i];
    if (len != want + (size_t)nops * OP_BYTES + (size_t)nexc * EXC_BYTES) return -1;
    const uint8_t *c[C_N];
    size_t p = 0;
    for (int i = 0; i < C_N; ++i)
    {
        c[i] = in + p;
        p += (size_t)S * ES[i];
    }
    int rc = -1;
    const uint8_t *ops = in + p;
    uint32_t op = 0;
    for (uint32_t t = 0; t < S; ++t)
    {
        struct rec_step *s = &steps[t];
        memset(s, 0, sizeof *s);
        s->action[0] = c[C_CAM0][t];
        s->action[1] = c[C_CAM1][t];
        uint16_t keys;
        memcpy(&keys, c[C_KEYS] + 2 * (size_t)t, 2);
        for (int j = 2; j < NETH_VEC_ACTION; ++j) s->action[j] = keys >> (j - 2) & 1;
        s->pick = c[C_PICK][t];
        uint8_t kc = c[C_KC][t];
        s->kind = kc & 7, s->chosen = kc >> 3 & 1, s->driver = kc >> 4 & 7;
        s->trunc = t == S - 1;
        memcpy(&s->reward, c[C_REW] + 4 * (size_t)t, 4);
        struct neth_rl_act *a = &s->act;
        memcpy(&a->hold, c[C_HOLD] + 4 * (size_t)t, 4);
        uint32_t press;
        memcpy(&press, c[C_PRESS] + 4 * (size_t)t, 4);
        for (int j = 0; j < NETH_RL_KEYS; ++j) a->press[j] = press >> j & 1;
        a->look_mode = c[C_LM][t];
        memcpy(&a->look[0], c[C_LOOK0] + 4 * (size_t)t, 4);
        memcpy(&a->look[1], c[C_LOOK1] + 4 * (size_t)t, 4);
        a->hotbar = (int8_t)c[C_HOT][t];
        a->nops = c[C_NOPS][t];
        if (a->nops > NETH_RL_OPS || op + (uint32_t)a->nops > nops) goto out;
        for (int j = 0; j < a->nops; ++j, ++op)
        {
            int32_t o[6];
            memcpy(o, ops + (size_t)op * OP_BYTES, sizeof o);
            a->ops[j] = (struct neth_rl_op){o[0], o[1], o[2], o[3], o[4], o[5]};
        }
    }
    if (op != nops) goto out;
    const uint8_t *exc = ops + (size_t)nops * OP_BYTES;
    for (uint32_t x = 0; x < nexc; ++x)
    {
        uint32_t t;
        memcpy(&t, exc + (size_t)x * EXC_BYTES, 4);
        if (t >= S) goto out;
        memcpy(&steps[t], exc + (size_t)x * EXC_BYTES + 4, sizeof *steps);
    }
    rc = 0;
out:
    return rc;
}

uint32_t rec_fid_code(const struct fidelity *f)
{
    return (f->no_spawning ? REC_FID_SPAWNING : 0) | (f->bits & FID_NO_RANDOM_TICKS ? REC_FID_RANDOM_TICKS : 0) |
           (f->bits & FID_NO_WEATHER ? REC_FID_WEATHER : 0) | (f->keep_on ? REC_FID_MOBS : 0) |
           (f->skip_types ? REC_FID_SPAWN_TYPES : 0) | (f->bits & FID_SKIP_AMBIENT ? REC_FID_SKIP_AMBIENT : 0) |
           (f->distant_tick_radius > 0 ? REC_FID_DISTANT_TICKS : 0);
}

/* ---------------------------------------------------------------- the recorder */

/* a group step in the ring: this, then gs rows (struct row), the nops ops (six i32 each, in the envs' order), nexc
 * exceptions (an i32 env index, an i32 0, the struct rec_step), the k episode records, their rungs and fidelities,
 * nsub subs (lane/ticks-per-step: an i32 env index, an i32 tick, the act; in the envs' order), and with --rec-raw the
 * gs steps whole (struct rec_step) */
struct msg {
    uint32_t bytes, k;
    int32_t g, lo, gs, raw;
    uint32_t nops, nexc;
    uint64_t seq;
    uint32_t nsub, pad;
};
#define WRAP 0xffffffffu

/* an env's episode so far: its rows as they came (spread into columns only when the episode is kept), their ops,
 * their exceptions */
struct envrec {
    struct row *rows;
    uint32_t S, cap;
    int32_t *ops;
    uint32_t nops, opcap;
    struct rec_step *exc;
    uint32_t nexc, exccap;
    struct rec_step *raw;       /* --rec-raw: the steps as copied */
    uint32_t rawcap;
    int32_t ep, cut;           /* its episode number; 1 when a dropped group step cut this one */
    uint8_t *sub;               /* lane/ticks-per-step: its subs (the payload's form, the step its row's index) */
    uint32_t nsub, subcap;
};

struct rec {
    uint8_t *ring;
    size_t cap;
    _Atomic uint64_t head, tail;
    _Atomic int closing;
    uint64_t *gseq;             /* the main thread's: [G] each group's next message */
    uint64_t *wseq;             /* the writer's: [G] the message it expects */
    /* the main thread's: [G] each group's step begun at recv (staged, 0 none), its actions, picks and records */
    int *staged, *stage_k;
    int32_t *stage_act, *stage_pick;
    struct neth_vec_episode *stage_eps;
    int32_t *stage_rung;        /* lane/ladder: the records' rungs and fidelities, beside stage_eps */
    uint32_t *stage_fid;
    struct row *rows;           /* rec_group_done's scratch: a group's rows, ops and exceptions */
    int32_t *ops;
    struct rec_step *exc;
    int32_t *exc_i;
    uint8_t *subs;              /* lane/ticks-per-step: rec_group_done's scratch, [gs * (T - 1)] subs */
    int T;                      /* a step's ticks (rec_head.ticks_per_step) */
    int raw_on;
    uint32_t ep_steps;          /* an episode's policy steps (the rows made that long at the writer's start) */
    struct cols tmp;            /* the writer's: a kept episode's columns */
    int G, gs, n, aw;
    struct envrec *e;
    struct rec_rule rule;
    uint64_t seed;
    FILE *f, *raw;
    ZSTD_CCtx *z;
    uint8_t *pk, *zb;
    size_t pkcap, zbcap;
    pthread_t th;
    /* the books (rec_close's out) */
    uint64_t ended, kept, steps, steps_kept, pk_kept, z_kept, dropped, cut, written;
    double busy;
    int err, idle;              /* idle: the writer at SCHED_IDLE (1 asked, then 1 set or -1 refused) */
};

/* the writer's own CPU time (its wall time counts the waits SCHED_IDLE puts it in) */
static double cpu_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_THREAD_CPUTIME_ID, &t);
    return (double)t.tv_sec + 1e-9 * (double)t.tv_nsec;
}

static void ep_write(struct rec *r, struct envrec *x, const struct cols *k, const struct neth_vec_episode *ep, int keep,
                     int32_t rung, uint32_t fid)
{
    size_t pk = cols_bytes(k);
    if (pk > r->pkcap) r->pk = realloc(r->pk, r->pkcap = pk + pk / 2);
    cols_finish(k, r->pk);
    size_t zc = ZSTD_compressBound(pk);
    if (zc > r->zbcap) r->zb = realloc(r->zb, r->zbcap = zc + zc / 2);
    size_t zl = ZSTD_compressCCtx(r->z, r->zb, r->zbcap, r->pk, pk, r->rule.level);
    if (ZSTD_isError(zl))
    {
        fprintf(stderr, "rec: zstd: %s\n", ZSTD_getErrorName(zl));
        r->err = 1;
        return;
    }
    struct rec_chunk c;
    memset(&c, 0, sizeof c);
    c.magic = REC_CHUNK_MAGIC;
    c.chunk_bytes = sizeof c;
    c.index = r->ended - 1;
    c.env = ep->env;
    c.env_ep = x->ep;
    c.steps = k->S;
    c.nops = k->nops;
    c.nexc = k->nexc;
    c.keep = (uint32_t)keep;
    c.raw_bytes = (uint32_t)pk;
    c.zlen = (uint32_t)zl;
    c.digest = rec_digest(r->pk, pk);
    c.rec = *ep;
    c.rung = rung;
    c.fid = fid;
    c.nsub = k->nsub;
    if (fwrite(&c, sizeof c, 1, r->f) != 1 || fwrite(r->zb, 1, zl, r->f) != zl) r->err = 1;
    r->written += sizeof c + zl;
    if (r->raw)
    {
        c.zlen = 0;
        if (fwrite(&c, sizeof c, 1, r->raw) != 1 || fwrite(x->raw, sizeof *x->raw, k->S, r->raw) != k->S) r->err = 1;
    }
    ++r->kept;
    r->steps_kept += k->S;
    r->pk_kept += pk;
    r->z_kept += zl;
}

static void take_msg(struct rec *r, const struct msg *m)
{
    int gs = m->gs;
    const struct row *rows = (const struct row *)(m + 1);
    const int32_t *ops = (const int32_t *)(rows + gs);
    const uint8_t *ex = (const uint8_t *)(ops + 6 * (size_t)m->nops);
    const struct neth_vec_episode *eps = (const struct neth_vec_episode *)(ex + (size_t)m->nexc * (8 + sizeof(struct rec_step)));
    const uint8_t *rf = (const uint8_t *)(eps + m->k);     /* each record's rung and fidelity */
    const uint8_t *sb = rf + 8 * (size_t)m->k;              /* lane/ticks-per-step: the subs */
    const struct rec_step *raw = m->raw ? (const struct rec_step *)(sb + (size_t)m->nsub * REC_SUB_BYTES) : NULL;
    uint32_t s_at = 0;
    /* a gap in the group's messages: the steps dropped cut every episode of the group in play */
    if (m->seq != r->wseq[m->g])
        for (int i = 0; i < gs; ++i) r->e[m->lo + i].cut = 1;
    r->wseq[m->g] = m->seq + 1;
    uint32_t x_at = 0;
    for (int i = 0; i < gs; ++i)
    {
        struct envrec *x = &r->e[m->lo + i];
        const struct row *w = &rows[i];
        const struct rec_step *exc = NULL;
        if (w->exc)
        {
            /* the exceptions in the envs' order */
            int32_t ei;
            memcpy(&ei, ex + (size_t)x_at * (8 + sizeof *exc), 4);
            if (ei == i) exc = (const struct rec_step *)(ex + (size_t)x_at++ * (8 + sizeof *exc) + 8);
        }
        /* lane/ticks-per-step: the env's subs (in the envs' order), its step the row's index in the episode */
        uint32_t s0 = s_at;
        for (int32_t si; s_at < m->nsub && (memcpy(&si, sb + (size_t)s_at * REC_SUB_BYTES, 4), si == i);) ++s_at;
        if (!x->cut && s_at > s0)
        {
            uint32_t at = x->nsub;
            subs_add(&x->sub, &x->nsub, &x->subcap, sb + (size_t)s0 * REC_SUB_BYTES, s_at - s0);
            for (uint32_t q = at; q < x->nsub; ++q) memcpy(x->sub + (size_t)q * REC_SUB_BYTES, &x->S, 4);
        }
        if (!x->cut)
        {
            /* the row appended as it is (the columns are made for a kept episode only) */
            if (x->S == x->cap) x->rows = realloc(x->rows, (size_t)(x->cap = x->cap ? 2 * x->cap : 1024) * sizeof *w);
            x->rows[x->S] = *w;
            if (w->nops)
            {
                if (x->nops + w->nops > x->opcap)
                {
                    x->opcap = x->opcap ? 2 * x->opcap : 256;
                    while (x->opcap < x->nops + w->nops) x->opcap *= 2;
                    x->ops = realloc(x->ops, (size_t)x->opcap * OP_BYTES);
                }
                memcpy(x->ops + 6 * (size_t)x->nops, ops, (size_t)w->nops * OP_BYTES);
                x->nops += w->nops;
            }
            if (exc)
            {
                if (x->nexc == x->exccap) x->exc = realloc(x->exc, (size_t)(x->exccap = 2 * x->exccap + 16) * sizeof *exc);
                x->exc[x->nexc++] = *exc;
            }
            if (raw)
            {
                if (x->S >= x->rawcap) x->raw = realloc(x->raw, (size_t)(x->rawcap = 2 * x->S + 1024) * sizeof *raw);
                x->raw[x->S] = raw[i];
            }
            ++x->S;
            ++r->steps;
        }
        ops += 6 * (size_t)w->nops;
        if (!w->trunc) continue;
        /* the episode ended with this step: its record is among the ones recv made */
        const struct neth_vec_episode *ep = NULL;
        int32_t rung = -1;
        uint32_t fid = 0;
        for (uint32_t q = 0; q < m->k && !ep; ++q)
            if (eps[q].env == m->lo + i)
            {
                ep = &eps[q];
                memcpy(&rung, rf + 8 * (size_t)q, 4);
                memcpy(&fid, rf + 8 * (size_t)q + 4, 4);
            }
        ++r->ended;
        int keep = ep && !x->cut ? rec_keep(&r->rule, r->seed, ep, x->ep) : 0;
        if (keep)
        {
            /* kept: its rows spread into the columns */
            struct cols *k = &r->tmp;
            cols_reset(k);
            const int32_t *o = x->ops;
            uint32_t xe = 0;
            for (uint32_t t = 0; t < x->S; ++t)
            {
                cols_row(k, &x->rows[t], o, x->rows[t].exc && xe < x->nexc ? &x->exc[xe++] : NULL);
                o += 6 * (size_t)x->rows[t].nops;
            }
            subs_add(&k->sub, &k->nsub, &k->subcap, x->sub, x->nsub);
            ep_write(r, x, k, ep, keep, rung, fid);
        }
        else if (x->cut || !ep)
            ++r->cut;
        x->S = x->nops = x->nexc = x->nsub = 0;
        x->cut = 0;
        ++x->ep;
    }
}

static void *writer(void *arg)
{
    struct rec *r = arg;
#if defined(__linux__)
    /* the workers and the main thread first: the writer runs on a CPU none of them wants (the ring takes the
     * bursts, a full one drops the group step and counts it) */
    if (r->idle)
    {
        struct sched_param sp = {0};
        r->idle = pthread_setschedparam(pthread_self(), SCHED_IDLE, &sp) == 0 ? 1 : -1;
    }
#endif
    /* each env's rows an episode long and touched, and the kept episode's columns, before the first row (no page
     * faults in the run) */
    for (int i = 0; i < r->n && r->ep_steps; ++i)
    {
        struct envrec *x = &r->e[i];
        x->cap = r->ep_steps;
        x->rows = malloc((size_t)x->cap * sizeof *x->rows);
        memset(x->rows, 0, (size_t)x->cap * sizeof *x->rows);
    }
    if (r->ep_steps)
    {
        struct cols *k = &r->tmp;
        k->cap = r->ep_steps;
        for (int c = 0; c < C_N; ++c)
        {
            k->c[c] = malloc((size_t)k->cap * ES[c]);
            memset(k->c[c], 0, (size_t)k->cap * ES[c]);
        }
    }
    for (;;)
    {
        uint64_t t = atomic_load_explicit(&r->tail, memory_order_relaxed);
        uint64_t h = atomic_load_explicit(&r->head, memory_order_acquire);
        if (t == h)
        {
            if (atomic_load_explicit(&r->closing, memory_order_acquire) &&
                atomic_load_explicit(&r->head, memory_order_acquire) == t)
                break;
            /* nothing queued: a millisecond's sleep (the main thread never wakes this one) */
            struct timespec ts = {0, 1000000};
            nanosleep(&ts, NULL);
            continue;
        }
        size_t pos = (size_t)(t % r->cap);
        uint32_t bytes;
        memcpy(&bytes, r->ring + pos, 4);
        if (bytes == WRAP)
        {
            atomic_store_explicit(&r->tail, t + (r->cap - pos), memory_order_release);
            continue;
        }
        take_msg(r, (const struct msg *)(r->ring + pos));
        atomic_store_explicit(&r->tail, t + bytes, memory_order_release);
    }
    r->busy = cpu_s();
    return NULL;
}

static void rec_free(struct rec *r)
{
    for (int i = 0; r->e && i < r->n; ++i)
        free(r->e[i].rows), free(r->e[i].ops), free(r->e[i].exc), free(r->e[i].raw), free(r->e[i].sub);
    free(r->subs);
    cols_free(&r->tmp);
    free(r->ring), free(r->gseq), free(r->wseq), free(r->e), free(r->pk), free(r->zb);
    free(r->staged), free(r->stage_k), free(r->stage_act), free(r->stage_pick), free(r->stage_eps);
    free(r->stage_rung), free(r->stage_fid);
    free(r->rows), free(r->ops), free(r->exc), free(r->exc_i);
    ZSTD_freeCCtx(r->z);
    free(r);
}

struct rec *rec_open(const struct rec_opts *o, char *err, size_t en)
{
    struct rec *r = calloc(1, sizeof *r);
    r->n = o->head.n;
    r->G = o->head.groups;
    r->gs = r->n / r->G;
    r->aw = (o->head.macros & NETH_VEC_HOTBAR) ? NETH_VEC_ACTION + 1 : NETH_VEC_ACTION;
    r->rule = o->rule;
    r->seed = o->seed;
    r->idle = 1;
    r->cap = (o->ring_mb ? o->ring_mb : 64) << 20;
    r->ring = malloc(r->cap);
    /* every page touched now, not one a group step on the main thread as the ring first fills */
    if (r->ring) memset(r->ring, 0, r->cap);
    r->ep_steps = o->head.ticks_per_step > 0 ? (uint32_t)(o->head.ep_len / o->head.ticks_per_step + 1) : 0;
    r->gseq = calloc((size_t)r->G, sizeof *r->gseq);
    r->wseq = calloc((size_t)r->G, sizeof *r->wseq);
    r->staged = calloc((size_t)r->G, sizeof *r->staged);
    r->stage_k = calloc((size_t)r->G, sizeof *r->stage_k);
    r->stage_act = malloc((size_t)r->n * NETH_VEC_ACTION * sizeof *r->stage_act);
    r->stage_pick = malloc((size_t)r->n * sizeof *r->stage_pick);
    r->stage_eps = malloc((size_t)r->n * sizeof *r->stage_eps);
    r->stage_rung = malloc((size_t)r->n * sizeof *r->stage_rung);
    r->stage_fid = malloc((size_t)r->n * sizeof *r->stage_fid);
    r->rows = malloc((size_t)r->gs * sizeof *r->rows);
    r->ops = malloc((size_t)r->gs * NETH_RL_OPS * 6 * sizeof *r->ops);
    r->exc = malloc((size_t)r->gs * sizeof *r->exc);
    r->exc_i = malloc((size_t)r->gs * sizeof *r->exc_i);
    r->T = o->head.ticks_per_step > 1 ? o->head.ticks_per_step : 1;
    if (r->T > 1) r->subs = malloc((size_t)r->gs * (size_t)(r->T - 1) * REC_SUB_BYTES);
    r->raw_on = o->raw != NULL;
    r->e = calloc((size_t)r->n, sizeof *r->e);
    r->z = ZSTD_createCCtx();
    int fd = open(o->path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0 || !(r->f = fdopen(fd, "wb")))
    {
        snprintf(err, en, "--rec %s: %s", o->path, strerror(errno));
        rec_free(r);
        return NULL;
    }
    setvbuf(r->f, NULL, _IOFBF, 4 << 20);
    if (o->raw && !(r->raw = fopen(o->raw, "wb")))
    {
        snprintf(err, en, "--rec-raw %s: %s", o->raw, strerror(errno));
        fclose(r->f);
        rec_free(r);
        return NULL;
    }
    struct rec_head h = o->head;
    memcpy(h.magic, REC_MAGIC, 8);
    h.version = REC_VERSION;
    h.head_bytes = sizeof h;
    h.text_bytes = (uint32_t)strlen(o->text);
    h.ep_bytes = sizeof(struct neth_vec_episode);
    h.step_bytes = sizeof(struct rec_step);
    fwrite(&h, sizeof h, 1, r->f);
    fwrite(o->text, 1, h.text_bytes, r->f);
    r->written = sizeof h + h.text_bytes;
    if (r->raw) fwrite(&h, sizeof h, 1, r->raw), fwrite(o->text, 1, h.text_bytes, r->raw);
    if (pthread_create(&r->th, NULL, writer, r))
    {
        snprintf(err, en, "--rec: the writer thread: %s", strerror(errno));
        fclose(r->f);
        if (r->raw) fclose(r->raw);
        rec_free(r);
        return NULL;
    }
    pthread_setname_np(r->th, "ntrec-writer");
    return r;
}

void rec_group(struct rec *r, int g, int lo, int gs, int was, const struct neth_vec_bufs *b,
               const struct neth_vec_episode *eps, int k, const int32_t *rung, const uint32_t *fid)
{
    if (!was) return;
    for (int q = 0; q < k; ++q)
    {
        r->stage_rung[lo + q] = rung ? rung[q] : -1;
        r->stage_fid[lo + q] = fid ? fid[q] : 0;
    }
    /* what the policy's next launch replaces (the actions, the picks), and the records */
    if (r->aw == NETH_VEC_ACTION)
        memcpy(r->stage_act + (size_t)lo * NETH_VEC_ACTION, b->actions + (size_t)lo * NETH_VEC_ACTION,
               (size_t)gs * NETH_VEC_ACTION * sizeof *r->stage_act);
    else
        for (int i = 0; i < gs; ++i)
            memcpy(r->stage_act + (size_t)(lo + i) * NETH_VEC_ACTION, b->actions + (size_t)(lo + i) * r->aw,
                   NETH_VEC_ACTION * sizeof *r->stage_act);
    if (b->picks) memcpy(r->stage_pick + lo, b->picks + lo, (size_t)gs * sizeof *r->stage_pick);
    else memset(r->stage_pick + lo, 0, (size_t)gs * sizeof *r->stage_pick);
    if (k) memcpy(r->stage_eps + lo, eps, (size_t)k * sizeof *eps);
    r->stage_k[g] = k;
    r->staged[g] = 1;
}

void rec_group_done(struct rec *r, int g, const struct neth_vec_bufs *b)
{
    if (!r->staged[g]) return;
    r->staged[g] = 0;
    int gs = r->gs, lo = g * gs, k = r->stage_k[g];
    uint64_t seq = r->gseq[g]++;
    /* the rows, ops and exceptions in scratch: the message's size */
    uint32_t nops = 0, nexc = 0;
    for (int i = 0; i < gs; ++i)
    {
        int e = lo + i;
        const int32_t *act = r->stage_act + (size_t)e * NETH_VEC_ACTION;
        if (make_row(&r->rows[i], r->ops + 6 * (size_t)nops, act, r->stage_pick[e], b->kind[e], b->chosen[e],
                     b->driver[e], b->truncations[e], b->rewards[e], &b->acts[e]))
        {
            make_step(&r->exc[nexc], act, r->stage_pick[e], b->kind[e], b->chosen[e], b->driver[e], b->truncations[e],
                      b->rewards[e], &b->acts[e]);
            r->exc_i[nexc++] = i;
        }
        nops += r->rows[i].nops;
    }
    /* lane/ticks-per-step: each later tick a macro drove (vecenv.h neth_vec_bufs.tick_acts) */
    uint32_t nsub = 0;
    if (r->T > 1 && b->tick_acts && b->ticks == r->T)
        for (int i = 0; i < gs; ++i)
        {
            size_t q0 = (size_t)(lo + i) * (size_t)(r->T - 1);
            for (int t = 1; t < r->T; ++t)
            {
                if (!b->tick_macro[q0 + t - 1]) continue;
                uint8_t *s = r->subs + (size_t)nsub++ * REC_SUB_BYTES;
                struct neth_rl_act a = b->tick_acts[q0 + t - 1];
                rec_canon_act(&a);
                int32_t two[2] = {i, t};
                memcpy(s, two, 8);
                memcpy(s + 8, &a, sizeof a);
            }
        }
    const struct neth_vec_episode *eps = r->stage_eps + lo;
    size_t need = sizeof(struct msg) + (size_t)gs * sizeof(struct row) + (size_t)nops * OP_BYTES +
                  (size_t)nexc * (8 + sizeof(struct rec_step)) + (size_t)k * (sizeof *eps + 8) +
                  (size_t)nsub * REC_SUB_BYTES + (r->raw_on ? (size_t)gs * sizeof(struct rec_step) : 0);
    need = (need + 7) & ~(size_t)7;
    uint64_t h = atomic_load_explicit(&r->head, memory_order_relaxed);
    uint64_t t = atomic_load_explicit(&r->tail, memory_order_acquire);
    size_t pos = (size_t)(h % r->cap), room = r->cap - (size_t)(h - t);
    if (pos + need > r->cap)
    {
        size_t pad = r->cap - pos;
        if (room < pad + need)
        {
            ++r->dropped;
            return;
        }
        uint32_t w = WRAP;
        memcpy(r->ring + pos, &w, 4);
        h += pad;
        pos = 0;
    }
    else if (room < need)
    {
        ++r->dropped;
        return;
    }
    struct msg *m = (struct msg *)(r->ring + pos);
    m->bytes = (uint32_t)need;
    m->k = (uint32_t)k;
    m->g = g, m->lo = lo, m->gs = gs, m->raw = r->raw_on;
    m->nops = nops, m->nexc = nexc;
    m->seq = seq;
    m->nsub = nsub, m->pad = 0;
    uint8_t *p = (uint8_t *)(m + 1);
    memcpy(p, r->rows, (size_t)gs * sizeof(struct row));
    p += (size_t)gs * sizeof(struct row);
    memcpy(p, r->ops, (size_t)nops * OP_BYTES);
    p += (size_t)nops * OP_BYTES;
    for (uint32_t x = 0; x < nexc; ++x)
    {
        int32_t two[2] = {r->exc_i[x], 0};
        memcpy(p, two, 8);
        memcpy(p + 8, &r->exc[x], sizeof(struct rec_step));
        p += 8 + sizeof(struct rec_step);
    }
    if (k) memcpy(p, eps, (size_t)k * sizeof *eps);
    p += (size_t)k * sizeof *eps;
    /* each record's rung and fidelity (lane/ladder) */
    for (int q = 0; q < k; ++q)
    {
        memcpy(p, &r->stage_rung[lo + q], 4);
        memcpy(p + 4, &r->stage_fid[lo + q], 4);
        p += 8;
    }
    if (nsub) memcpy(p, r->subs, (size_t)nsub * REC_SUB_BYTES);
    p += (size_t)nsub * REC_SUB_BYTES;
    if (r->raw_on)
        for (int i = 0; i < gs; ++i)
        {
            int e = lo + i;
            make_step((struct rec_step *)p + i, r->stage_act + (size_t)e * NETH_VEC_ACTION, r->stage_pick[e],
                      b->kind[e], b->chosen[e], b->driver[e], b->truncations[e], b->rewards[e], &b->acts[e]);
        }
    atomic_store_explicit(&r->head, h + need, memory_order_release);
}

void rec_close(struct rec *r, double out[12])
{
    if (!r) return;
    atomic_store_explicit(&r->closing, 1, memory_order_release);
    pthread_join(r->th, NULL);
    uint64_t open = 0;
    for (int i = 0; i < r->n; ++i)
        if (r->e[i].S) ++open;
    if (fclose(r->f)) r->err = 1;
    if (r->raw && fclose(r->raw)) r->err = 1;
    if (out)
    {
        double b[12] = {(double)r->ended, (double)r->kept, (double)r->steps, (double)r->steps_kept, (double)r->pk_kept,
                        (double)r->z_kept, (double)r->dropped, (double)r->cut, r->busy, (double)r->written,
                        (double)open, (double)r->idle};
        memcpy(out, b, sizeof b);
    }
    if (r->err) fprintf(stderr, "rec: a write failed\n");
    rec_free(r);
}
