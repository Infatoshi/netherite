/* ntloop's episode recorder (--rec, the run file's record.*): rec.h's writer opened with the run's header text. */
#define _GNU_SOURCE
#include <sys/stat.h>

#include "neth_pipe_internal.h"
#include "neth_runconf.h"
#include "curric.h"

/* the recorder on DIR/episodes.nrec, its header's text the config whole with what the reader needs to
 * make a tape (the start, the fidelity, the decision's ticks) */
static struct rec *record_start(struct neth_pipe *L, const struct neth_runconf *rc, const char *dir, const char *config,
                                 int argc, char **argv, const struct rec_rule *rule, const char *raw, int ep_len,
                                 int macros, uint64_t seed, int ring_mb, const char *start_over)
{
    char path[4096], err[1024];
    if (mkdir(dir, 0755) && errno != EEXIST)
    {
        printf("FAIL --rec %s: %s\n", dir, strerror(errno));
        return NULL;
    }
    snprintf(path, sizeof path, "%s/episodes.nrec", dir);
    static struct wconf c;
    if (wconf_load(&c, config, err, sizeof err))
    {
        printf("FAIL --rec: %s\n", err);
        return NULL;
    }
    struct fidelity fid;
    if (fidelity_from_conf(&fid, &c, err, sizeof err))
    {
        printf("FAIL --rec: %s\n", err);
        return NULL;
    }
    if (rc->train_skip_ambient) fid.bits |= FID_SKIP_AMBIENT;
    if (rc->distant_tick_radius > 0)
    {
        fid.distant_tick_radius = rc->distant_tick_radius > 255 ? 255 : rc->distant_tick_radius;
        fid.bits |= FID_DISTANT_TICKS;
    }
    /* start: PATH_MAX, realpath's own size (glibc's fortify aborted --rec with --start on a 1024-byte buffer:
     * "buffer overflow detected") */
    char fdesc[256], start[PATH_MAX];
    fidelity_describe(&fid, fdesc, sizeof fdesc);
    wconf_start_path(&c, start, sizeof start);
    /* --start DIR is the episodes' start (ntrec tape's FROM=) */
    if (start_over && !realpath(start_over, start))
    {
        printf("FAIL --rec: --start %s: %s\n", start_over, strerror(errno));
        return NULL;
    }
    size_t clen = 0;
    char *ctext = slurp(config, &clen);
    if (!ctext)
    {
        printf("FAIL --rec: %s: %s\n", config, strerror(errno));
        return NULL;
    }
    size_t cap = clen + 16384 + (L->crc ? (size_t)L->crc->nstart * 4200 : 0);
    char *text = malloc(cap);
    size_t p = 0;
    p += (size_t)snprintf(text + p, cap - p, "config: %s\nstart: %s\nstart_path: %s\nseed: %s\nfidelity: %s\n"
                          "ticks_per_step: %s\naction_repeat: %s\nrule: keep %s, milestones 0x%x, sample %g, zstd %d\n"
                          "argv:",
                          config, start_over ? start_over : c.val[WC_START], start, c.val[WC_SEED], fdesc,
                          c.val[WC_TICKS_PER_STEP],
                          c.val[WC_ACTION_REPEAT], rule->keep == REC_KEEP_ALL ? "all" : rule->keep ? "gain" : "none",
                          rule->ms, rule->sample, rule->level);
    for (int i = 0; i < argc && p + 1024 < cap; ++i) p += (size_t)snprintf(text + p, cap - p, " %s", argv[i]);
    if (L->lad)
    {
        /* the rungs a chunk's rung indexes (each episode's fidelity is in its chunk) */
        const struct neth_ladder *D = L->lad;
        p += (size_t)snprintf(text + p, cap - p, "\nladder:");
        for (int r = 0; r < D->nr && p + 512 < cap; ++r)
            p += (size_t)snprintf(text + p, cap - p, "%s %d %s", r ? "," : "", r, D->name[r]);
        p += (size_t)snprintf(text + p, cap - p, "; schedule %s; -1 the make's episode, at the sim's fidelity",
                              neth_lad_sched[D->sched]);
    }
    if (L->crc)
    {
        /* the mix's starts a record's start indexes (1 + k: mix_K's; ntrec tape replays one from it) */
        for (int k = 0; k < L->crc->nstart; ++k)
            p += (size_t)snprintf(text + p, cap - p, "\nmix_%d: %s", k + 1, L->crc->path[k]);
    }
    p += (size_t)snprintf(text + p, cap - p, "\n--- %s\n", config);
    memcpy(text + p, ctext, clen);
    text[p + clen] = 0;
    free(ctext);
    struct rec_opts o;
    memset(&o, 0, sizeof o);
    o.path = path;
    o.raw = raw;
    o.rule = *rule;
    o.text = text;
    o.head.n = L->n;
    o.head.groups = L->G;
    o.head.ep_len = ep_len;
    o.head.ticks_per_step = wconf_int(&c, WC_TICKS_PER_STEP) * wconf_int(&c, WC_ACTION_REPEAT);
    o.head.macros = macros;
    o.head.vanilla = fidelity_vanilla(&fid);
    o.head.seed = strtoll(c.val[WC_SEED], NULL, 10);
    o.head.neth_rl_version = NWRL_VERSION;
    o.head.t0 = wall_s();
    o.seed = seed * 0x9e3779b97f4a7c15ull ^ 0x72756e726563ull;
    o.ring_mb = (size_t)ring_mb;
    struct rec *r = rec_open(&o, err, sizeof err);
    free(text);
    if (!r) printf("FAIL %s\n", err);
    else
        printf("--rec %s: keep %s, sample %g, zstd %d; %s fidelity (%s)%s\n", path,
               rule->keep == REC_KEEP_ALL ? "all" : rule->keep ? "the episodes that gain a milestone" : "none",
               rule->sample, rule->level, o.head.vanilla ? "vanilla" : "not vanilla", fdesc,
               o.head.vanilla ? "" : ": its episodes do not replay on the Java client");
    return r;
}

/* the episode records taken at each recv (the recorder's, the ladder's), then the recorder (--rec): 0, or the exit
 * code. After neth_report_open: the stash is --out's rows' */
int neth_record_setup(struct neth_pipe *L, const struct neth_runconf *c, const struct neth_vec_opts *o, int argc,
                      char **argv)
{
    L->t_rec0 = now_s();
    if (c->rec_on || L->lad)
    {
        L->stash_cap = L->epcap;
        if (L->rows)
        {
            L->stash = malloc((size_t)L->epcap * sizeof *L->stash);
            L->stash_rung = malloc((size_t)L->epcap * sizeof *L->stash_rung);
        }
        L->eptmp = malloc((size_t)c->n * sizeof *L->eptmp);
        L->pend = malloc((size_t)2 * c->n * sizeof *L->pend);
        L->eprung = malloc((size_t)c->n * sizeof *L->eprung);
        L->epfid = malloc((size_t)c->n * sizeof *L->epfid);
    }
    if (!c->rec_on) return 0;
    struct rec_rule rule = {0, c->rec_sample, c->rec_level, REC_MS_DEFAULT};
    if (c->rec_ms)
    {
        char err[256];
        int64_t m = neth_ms_mask(c->rec_ms, err, sizeof err);
        if (m < 0)
        {
            printf("FAIL: --rec-ms (record.milestones) %s: %s\n", c->rec_ms, err);
            return 2;
        }
        rule.ms = (uint32_t)m;
    }
    if (!strcmp(c->rec_keep, "gain")) rule.keep = REC_KEEP_GAIN;
    else if (!strcmp(c->rec_keep, "all")) rule.keep = REC_KEEP_ALL;
    else if (strcmp(c->rec_keep, "none"))
    {
        printf("FAIL: --rec-keep %s: gain, all or none\n", c->rec_keep);
        return 2;
    }
    const char *dir = c->rec_dir ? c->rec_dir : c->out;
    if (!dir || c->rec_sample < 0 || c->rec_sample > 1 || c->rec_level < -7 || c->rec_level > 22)
    {
        printf("FAIL: --rec on needs --out DIR (or run.out), or --rec DIR; --rec-sample 0 to 1, --rec-level -7 to 22\n");
        return 2;
    }
    /* the rows an episode long are the longest start's */
    int rec_ep = c->ep_len;
    for (int k = 0; o->start_ep_len && k <= o->nstarts; ++k)
        rec_ep = o->start_ep_len[k] > rec_ep ? o->start_ep_len[k] : rec_ep;
    if (!(L->rec = record_start(L, c, dir, c->config, argc, argv, &rule, c->rec_raw, rec_ep, o->macros, c->seed,
                                c->rec_ring, c->start_dir)))
        return 1;
    L->t_rec0 = now_s();
    return 0;
}

/* the recorder closed and its books */
void neth_record_finish(struct neth_pipe *L)
{
    if (!L->rec) return;
    double rb[12], win = now_s() - L->t_rec0;
    rec_close(L->rec, rb);
    L->rec = NULL;
    double sk = rb[3] > 0 ? rb[3] : 1;
    printf("rec: %.0f episodes ended, %.0f kept (%.1f%%), %.0f cut (a dropped group step or no record), %.0f "
           "unfinished at the end; %.0f env-steps packed, %.0f kept; bytes an env-step: the buffers %zu, packed "
           "%.2f, compressed %.2f (%.2fx); %.0f group steps dropped; the writer (%s) busy %.2f s of %.2f (%.1f%%); "
           "%.0f bytes written\n",
           rb[0], rb[1], 100 * rb[1] / (rb[0] > 0 ? rb[0] : 1), rb[7], rb[10], rb[2], rb[3],
           (size_t)NETH_VEC_ACTION * 4 + 4 + 4 + 4 + sizeof(struct neth_rl_act), rb[4] / sk, rb[5] / sk,
           rb[5] > 0 ? rb[4] / rb[5] : 0, rb[6],
           rb[11] > 0 ? "SCHED_IDLE" : rb[11] < 0 ? "SCHED_IDLE refused" : "default priority", rb[8], win,
           100 * rb[8] / (win > 0 ? win : 1), rb[9]);
    if (L->stash_drop) printf("rec: %lld episode records past the stash dropped\n", (long long)L->stash_drop);
}
