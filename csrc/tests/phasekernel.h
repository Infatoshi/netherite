/* The per-phase kernel harness (GPU plan L13), part of test_snapshots:
 *
 *   test_snapshots --phase-kernel LIST [--phase-kernel-ref TRACE]
 *                  [--phase-kernel-plant ROW:LABEL:WHAT] [--phase-kernel-verify N] DIR
 *
 * LIST is phase labels (S6, or 0.S6 for one world; comma separated) or all.
 * The replay runs in an image environment (image.h), H, with a device copy
 * D in another slot of the window. At every tested phase's start the host's
 * writes since the last exchange go down (env_export of H, env_import into
 * D), H's mapping is moved aside and D's onto H's address (image_move), and
 * the stand-in kernel runs there: the C phase itself, on nothing but what
 * the transfers carried. At its end the kernel's writes come back
 * (env_export of D, the mappings moved back, env_import into H), and the
 * tracer digests P's classes in H. A CUDA kernel replaces the stand-in: it
 * gets the same pages and must send back the same writes.
 *
 * --phase-kernel-ref TRACE (a --phase-trace of the same recording) compares
 * each row's digests with the trace's line: the first difference fails with
 * its row and phase. Without it the tracer is off (the row comparison with
 * the oracle's tape still holds every row) and the costs are the phases'
 * own. --phase-kernel-plant flips one byte of D after the
 * transfer at that row and phase (LABEL as in the trace, 0.S6): WHAT is a
 * byte offset in the image or wrand (the phase's world's World.rand seed in
 * the RNG table). --phase-kernel-verify N compares H and D over every live
 * page after the transfer of every Nth row: a write the tracker missed fails
 * there.
 *
 * The cost per row (pages each way; user and kernel instructions of the
 * host's export and import, and of the stand-in's device side) is printed at
 * the end, with the whole image's first transfer: what a split host/device
 * tick pays per row with these phases on the device. */
#include <stddef.h>
#include <unistd.h>

#include "../engine/image.h"
#include "../engine/imgxfer.h"
#if defined(NETHERITE_GPU_TICK)
#include "../cuda/tick/host.h"
#endif

/* per world phases by dim + 1; the others count as the overworld's */
#define PK_DIMS 3

struct pk_row {
    int64_t t;
    uint32_t runs, down, up;          /* phase runs, pages down, pages up */
    uint64_t ins[4][2];               /* host export, device import, device export, host import: user, kernel */
    /* --phase-kernel-cuda: the launches' device time, the child's upload
     * and download around them, the environments per launch */
    float dev_ms, down_ms, up_ms;
    uint32_t batch, faults;
};

struct pk_phase {
    uint64_t runs, down, up, up_max;
    uint64_t dev, faults;             /* --phase-kernel-cuda: runs the device made, runs it flagged */
    double dev_ms;                    /* their launches' device time */
};

/* per runner thread: cuda/tick/batch.c runs several replays in one process */
static _Thread_local struct {
    int on;
    unsigned dims[PH_COUNT];          /* the tested worlds of each phase, by bit dim + 1 */
    const char *list;
    const char *ref_path;
    FILE *ref;
    char *ref_line;
    size_t ref_cap;
    int64_t plant_row;
    int plant_id, plant_dim, planted;
    char plant_what[64];
    int verify_every;
    long verify_runs, verify_bad;

    struct img_track *trk;
    struct img_counters ctr;
    struct img_xfer wire;
    struct env *host, *dev, *park;    /* H at X, D at Y, the parking slot Z */
    size_t size;
    int in;                           /* a stand-in is running (D at X) */
    int cur_id, cur_dim;
    int64_t verify_row;

    /* the first, whole transfer */
    long full_pages;
    uint64_t full_ins[2][2];

    struct pk_row *rows;
    size_t nrows, caprows;
    struct pk_row cur;
    struct pk_phase ph[PH_COUNT][PK_DIMS];
    int64_t mismatch_row;
    char mismatch_label[32], mismatch_got[32], mismatch_want[32];
    long mismatches;
    long long runs_total;

    /* --phase-kernel-cuda: S6 runs on the device (cuda/tick/host.h) */
    int cuda, cuda_diff, diff_pending;
    long diff_runs, diff_pages, diff_bad, diff_c_only, diff_c_bad;
    size_t cuda_stack;
    struct tick_env *ce;
    long dev_runs, dev_faults;
    uint32_t fault_bits;
    int64_t first_fault_row;
    char name[128];                   /* the recording's, for the lines of a batch of replays */
    int partial;                      /* the replay stopped early (--rows): the trace goes on */
} PK = {.plant_row = -1, .mismatch_row = -1};

#define PK_MAX_ROWS ((size_t)1 << 21)

static int pk_label_parse(const char *s, int *id, int *dim)
{
    const char *dot = strchr(s, '.');
    *dim = 99;
    if (dot != NULL)
    {
        *dim = atoi(s);
        s = dot + 1;
    }
    for (int i = 0; i < PH_COUNT; ++i)
        if (!strcmp(PHASES[i].label, s))
        {
            *id = i;
            return dot == NULL || (*dim >= -1 && *dim <= 1);
        }
    return 0;
}

/* the flags; 1 taken (with its argument, *i moved past), 0 not ours, -1 bad */
static int pk_flag(int argc, char **argv, int *i)
{
    const char *a = argv[*i];
    if (!strcmp(a, "--phase-kernel") && *i + 1 < argc)
    {
        PK.on = 1;
        PK.list = argv[++*i];
        return 1;
    }
    if (!strcmp(a, "--phase-kernel-ref") && *i + 1 < argc)
    {
        PK.ref_path = argv[++*i];
        return 1;
    }
    if (!strcmp(a, "--phase-kernel-cuda"))
    {
#if defined(NETHERITE_GPU_TICK)
        PK.cuda = 1;
        return 1;
#else
        return -1;   /* the CUDA build only: out/native/cuda/tick/test_snapshots */
#endif
    }
    if (!strcmp(a, "--phase-kernel-cuda-diff"))
    {
        PK.cuda = PK.cuda_diff = 1;   /* the device runs S6, then C does, and every page the device wrote is compared */
        return 1;
    }
    if (!strcmp(a, "--phase-kernel-stack") && *i + 1 < argc)
    {
        PK.cuda_stack = (size_t)atol(argv[++*i]);
        return PK.cuda_stack >= 4096 ? 1 : -1;
    }
    if (!strcmp(a, "--phase-kernel-verify") && *i + 1 < argc)
    {
        PK.verify_every = atoi(argv[++*i]);
        return PK.verify_every > 0 ? 1 : -1;
    }
    if (!strcmp(a, "--phase-kernel-plant") && *i + 1 < argc)
    {
        char buf[128], *c1, *c2;
        snprintf(buf, sizeof buf, "%s", argv[++*i]);
        if ((c1 = strchr(buf, ':')) == NULL || (c2 = strchr(c1 + 1, ':')) == NULL) return -1;
        *c1 = *c2 = 0;
        PK.plant_row = atoll(buf);
        snprintf(PK.plant_what, sizeof PK.plant_what, "%s", c2 + 1);
        return pk_label_parse(c1 + 1, &PK.plant_id, &PK.plant_dim) ? 1 : -1;
    }
    return 0;
}

static int pk_list_parse(void)
{
    if (!strcmp(PK.list, "all"))
    {
        for (int i = 0; i < PH_COUNT; ++i) PK.dims[i] = 7;
        return 1;
    }
    char buf[512];
    snprintf(buf, sizeof buf, "%s", PK.list);
    for (char *tok = strtok(buf, ","); tok != NULL; tok = strtok(NULL, ","))
    {
        int id, dim;
        if (!pk_label_parse(tok, &id, &dim)) return 0;
        PK.dims[id] |= dim == 99 ? 7u : 1u << (dim + 1);
    }
    return 1;
}

/* the world a phase runs in, as the tracer names it */
static int pk_dim_of(int id)
{
    return id >= PH_WORLD_FIRST && id <= PH_WORLD_LAST ? nw_env->phase.world_dim : 0;
}

static void pk_die(const char *what)
{
    printf("FAIL phase-kernel: %s\n", what);
    fflush(stdout);
    _exit(1);
}

static void pk_count(uint64_t v[2]) { img_counters_read(&PK.ctr, v); }

static void pk_add(uint64_t acc[2], const uint64_t a[2], const uint64_t b[2])
{
    acc[0] += b[0] - a[0];
    acc[1] += b[1] - a[1];
}

static const unsigned char pk_zero[IMG_PAGE];

/* H against D over every live page of H: what the transfers left different
 * (a page that holds no memory reads zero, and is not read: that would map
 * the zero page there, which the tracker takes for a write) */
static void pk_verify(int64_t row, int id)
{
    struct img_extent ext[64];
    int n = image_extents(PK.host, ext, 64);
    const unsigned char *h = (const unsigned char *)PK.host, *d = (const unsigned char *)PK.dev;
    unsigned char mh[256], md[256];
    ++PK.verify_runs;
    for (int i = 0; i < n; ++i)
    {
        size_t lo = ext[i].off & ~(size_t)(IMG_PAGE - 1), hi = (ext[i].off + ext[i].len + IMG_PAGE - 1) & ~(size_t)(IMG_PAGE - 1);
        for (size_t p = lo; p < hi; p += IMG_PAGE)
        {
            size_t k = ((p - lo) / IMG_PAGE) % sizeof mh;
            if (k == 0)
            {
                size_t len = hi - p < sizeof mh * IMG_PAGE ? hi - p : sizeof mh * IMG_PAGE;
                if (img_resident(h + p, len, mh) != 0) memset(mh, 1, sizeof mh);
                if (img_resident(d + p, len, md) != 0) memset(md, 1, sizeof md);
            }
            const unsigned char *hp = (mh[k] & 1) ? h + p : pk_zero, *dp = (md[k] & 1) ? d + p : pk_zero;
            if (hp != dp && memcmp(hp, dp, IMG_PAGE) != 0)
            {
                if (PK.verify_bad++ == 0)
                    printf("FAIL phase-kernel: row %lld before %s: the device copy differs from the host at image offset %#zx "
                           "(extent %d, part %d): a write the tracker missed\n",
                           (long long)row, PHASES[id].label, p, i, ext[i].part);
                return;
            }
        }
    }
}

static void pk_plant(void)
{
    size_t off;
    char *end;
    if (!strcmp(PK.plant_what, "wrand"))
        off = offsetof(struct env, rng) + offsetof(struct rng_table, world) +
              (size_t)sr_dim_index(PK.plant_dim == 99 ? 0 : PK.plant_dim) * sizeof(PK.host->rng.world[0]);
    else
    {
        off = (size_t)strtoull(PK.plant_what, &end, 0);
        if (*end || off >= PK.size) pk_die("--phase-kernel-plant: WHAT is wrand or a byte offset in the image");
    }
    unsigned char *b = (unsigned char *)PK.dev + off;
    printf("phase-kernel: planted at row %lld before %d.%s: byte %#zx (%s) of the device copy, %02x to %02x\n",
           (long long)PK.plant_row, pk_dim_of(PK.plant_id), PHASES[PK.plant_id].label, off, PK.plant_what, *b, *b ^ 1);
    *b ^= 1;
    PK.planted = 1;
}

#if defined(NETHERITE_GPU_TICK)
/* --phase-kernel-cuda's run of a phase (S6, or another of the world tick's
 * that servertick_phase runs alone): the host's writes down, the device's
 * run, its writes back; 1 when the device ran it, 0 when it flagged the run
 * (the pages it wrote are restored from the host, and C runs the phase) */
static int pk_cuda(int id, int dim)
{
    uint64_t c0[2], c1[2], c2[2], c3[2];
    struct env *x = PK.host;
    const struct serverreplay *sr = nw_env->phase.ss->sr;
    uint64_t st = 0;
    for (int i = 0; i < sr->nworlds; ++i)
        if (sr->w[i].dim == dim) st = (uint64_t)(uintptr_t)&sr->w[i].st;
    if (!st) pk_die("no world for the phase's dimension");
    pk_count(c0);
    long n = env_export(x, tick_down(PK.ce), PK.trk);
    if (n < 0) pk_die("the host's export");
    pk_count(c1);
    struct tick_result r;
    if (tick_run_phase(PK.ce, st, id, &r) != 0) pk_die("the device child failed");
    PK.cur.dev_ms += (float)r.kernel_ms;
    PK.cur.down_ms += (float)r.down_ms;
    PK.cur.up_ms += (float)r.up_ms;
    PK.cur.batch = r.batch;
    PK.cur.down += (uint32_t)n;
    PK.ph[id][dim + 1].down += (uint64_t)n;
    pk_add(PK.cur.ins[0], c0, c1);
    ++PK.cur.runs;
    ++PK.runs_total;
    struct pk_phase *p = &PK.ph[id][dim + 1];
    ++p->runs;
    p->dev_ms += r.kernel_ms;
    if (r.fault)
    {
        ++p->faults;
        /* the device's writes are undone from the host image, and C runs */
        if (PK.dev_faults++ == 0) PK.first_fault_row = nw_env->phase.row;
        PK.fault_bits |= r.fault;
        ++PK.cur.faults;
        if (tick_restore(PK.ce) != 0) pk_die("restoring the device copy after a flagged run");
        return 0;
    }
    ++PK.dev_runs;
    ++p->dev;
    if (PK.cuda_diff)
    {
        /* C runs the phase too; pk_cuda_diff compares at its end */
        ++PK.diff_runs;
        PK.diff_pending = 1;
        return 0;
    }
    pk_count(c2);
    if (env_import(x, tick_up(PK.ce), PK.trk) != (long)r.pages_up) pk_die("the host's import");
    pk_count(c3);
    pk_add(PK.cur.ins[3], c2, c3);
    PK.cur.up += (uint32_t)r.pages_up;
    p->up += r.pages_up;
    if (r.pages_up > p->up_max) p->up_max = r.pages_up;
    return 1;
}
#endif

#if defined(NETHERITE_GPU_TICK)
static uint32_t *proc_realloc_u32(uint32_t *p, size_t n)
{
    uint32_t *q = proc_malloc(n * sizeof *q);
    if (p != NULL)
    {
        memcpy(q, p, n / 2 * sizeof *q);
        proc_free(p);
    }
    return q;
}

/* --phase-kernel-cuda-diff at the phase's end: every page the device wrote
 * against the page C left, then the device made equal to the host again
 * (C's pages and the device's) */
static void pk_cuda_diff(void)
{
    struct img_xfer *dn = tick_down(PK.ce), *up = tick_up(PK.ce);
    long n = env_export(PK.host, dn, PK.trk);
    if (n < 0) pk_die("the host's export after C's phase");
    const unsigned char *h = (const unsigned char *)PK.host;
    size_t d = 0;
    for (size_t k = 0; k < up->npages; ++k)
    {
        uint32_t pg = up->page[k];
        ++PK.diff_pages;
        const unsigned char *dev = up->bytes + k * IMG_PAGE, *c = h + (size_t)pg * IMG_PAGE;
        if (memcmp(dev, c, IMG_PAGE) != 0)
        {
            size_t b = 0;
            while (dev[b] == c[b]) ++b;
            if (PK.diff_bad++ < 20)
                printf("phase-kernel cuda diff: row %lld: image offset %#zx differs (device %02x, C %02x)\n",
                       (long long)nw_env->phase.row, (size_t)pg * IMG_PAGE + b, dev[b], c[b]);
        }
    }
    /* the pages C wrote and the device did not: the device's copy must
     * already hold what C left (a write of the value it had, a freed block
     * whose pages were zero) */
    {
        size_t nc = 0;
        static _Thread_local uint32_t *conly;
        static _Thread_local size_t conly_cap;
        for (size_t k = 0, u = 0; k < dn->npages; ++k)
        {
            uint32_t pg = dn->page[k] & ~IMG_XFER_ZERO;
            while (u < up->npages && up->page[u] < pg) ++u;
            if (u < up->npages && up->page[u] == pg) continue;
            if (nc == conly_cap) conly = proc_realloc_u32(conly, conly_cap = conly_cap ? conly_cap * 2 : 1024);
            conly[nc++] = pg;
        }
        if (nc)
        {
            /* the up area is the device's written pages still: read the
             * C-only ones into its tail */
            size_t base = up->npages;
            uint32_t *save = proc_malloc(nc * sizeof *save);
            for (size_t k = 0; k < nc; ++k) save[k] = dn->page[k];
            for (size_t k = 0; k < nc; ++k) dn->page[k] = conly[k];
            unsigned char *keep = proc_malloc(nc * IMG_PAGE);
            memcpy(keep, dn->bytes, nc * IMG_PAGE);
            (void)base;
            if (tick_read(PK.ce, nc) != 0) pk_die("reading the device copy");
            for (size_t k = 0; k < nc; ++k)
            {
                const unsigned char *dev = up->bytes + k * IMG_PAGE, *c = h + (size_t)conly[k] * IMG_PAGE;
                if (memcmp(dev, c, IMG_PAGE) != 0 && PK.diff_c_bad++ < 20)
                    printf("phase-kernel cuda diff: row %lld: C wrote image page %#zx, the device did not, and holds other bytes\n",
                           (long long)nw_env->phase.row, (size_t)conly[k] * IMG_PAGE);
            }
            for (size_t k = 0; k < nc; ++k) dn->page[k] = save[k];
            memcpy(dn->bytes, keep, nc * IMG_PAGE);
            proc_free(save);
            proc_free(keep);
        }
    }
    /* C's pages are in the down area (sorted); the device's that C did not
     * write follow, from the host */
    size_t j = 0;
    for (size_t k = 0; k < up->npages; ++k)
    {
        while (j < dn->npages && (dn->page[j] & ~IMG_XFER_ZERO) < up->page[k]) ++j;
        if (j < dn->npages && (dn->page[j] & ~IMG_XFER_ZERO) == up->page[k]) continue;
        dn->page[dn->npages + d] = up->page[k];
        memcpy(dn->bytes + (dn->ndata + d) * IMG_PAGE, h + (size_t)up->page[k] * IMG_PAGE, IMG_PAGE);
        ++d;
    }
    PK.diff_c_only += (long)dn->npages - (long)(up->npages - d);
    /* zero pages carry no bytes: data pages must come first in the bytes */
    dn->npages += d;
    dn->ndata += d;
    if (tick_upload(PK.ce) != 0) pk_die("bringing the device copy back to the host's");
}
#endif

static int pk_kernel(int id, int end)
{
    int dim = pk_dim_of(id);
    if (!((PK.dims[id] >> (dim + 1)) & 1)) return 0;
#if defined(NETHERITE_GPU_TICK)
    if (PK.cuda && end && PK.cuda_diff && PK.diff_pending)
    {
        PK.diff_pending = 0;
        pk_cuda_diff();
    }
    if (PK.cuda) return end ? 0 : pk_cuda(id, dim);
#endif
    struct env *x = PK.host;          /* the address both run at */
    uint64_t c0[2], c1[2], c2[2];
    long n;

    if (!end)
    {
        if (PK.in) pk_die("a phase began inside another");
        int64_t row = nw_env->phase.row;
        pk_count(c0);
        if ((n = env_export(x, &PK.wire, PK.trk)) < 0) pk_die("the host's export");
        pk_count(c1);
        if (env_import(PK.dev, &PK.wire, PK.trk) != n) pk_die("the device copy's import");
        pk_count(c2);
        pk_add(PK.cur.ins[0], c0, c1);
        pk_add(PK.cur.ins[1], c1, c2);
        PK.cur.down += (uint32_t)n;
        PK.ph[id][dim + 1].down += (uint64_t)n;
        if (PK.verify_every && row != PK.verify_row && row % PK.verify_every == 0)
        {
            PK.verify_row = row;
            pk_verify(row, id);
        }
        if (!PK.planted && row == PK.plant_row && id == PK.plant_id && (PK.plant_dim == 99 || PK.plant_dim == dim))
            pk_plant();
        /* the stand-in runs on the device copy, at the host's address */
        if (image_move(x, PK.park, PK.size) != 0 || image_move(PK.dev, x, PK.size) != 0) pk_die("moving the device copy in");
        PK.in = 1;
        PK.cur_id = id;
        PK.cur_dim = dim;
        return 0;
    }
    if (!PK.in || PK.cur_id != id || PK.cur_dim != dim) return 0;   /* the tracer did not begin it */
    pk_count(c0);
    if ((n = env_export(x, &PK.wire, PK.trk)) < 0) pk_die("the device copy's export");
    pk_count(c1);
    if (image_move(x, PK.dev, PK.size) != 0 || image_move(PK.park, x, PK.size) != 0) pk_die("moving the host back");
    PK.in = 0;
    pk_count(c2);
    if (env_import(x, &PK.wire, PK.trk) != n) pk_die("the host's import");
    uint64_t c3[2];
    pk_count(c3);
    pk_add(PK.cur.ins[2], c0, c1);
    pk_add(PK.cur.ins[3], c2, c3);
    PK.cur.up += (uint32_t)n;
    ++PK.cur.runs;
    ++PK.runs_total;
    struct pk_phase *p = &PK.ph[id][dim + 1];
    ++p->runs;
    p->up += (uint64_t)n;
    if ((uint64_t)n > p->up_max) p->up_max = (uint64_t)n;
    return 0;
}

/* After session_open: the tracker on H, the device copy made from the whole
 * image, the hook in. */
static void pk_setup(const char *dir)
{
    const char *slash = strrchr(dir, '/');
    snprintf(PK.name, sizeof PK.name, "%s", slash != NULL && slash[1] ? slash + 1 : dir);
    char err[256];
    if (!pk_list_parse()) pk_die("--phase-kernel takes phase labels (S6, 0.S6; comma separated) or all");
    PK.host = nw_env;
    PK.size = nw_env->img.size;
    if (!PK.size) pk_die("the replay is not in an image environment");
#if defined(NETHERITE_GPU_TICK)
    if (PK.cuda)
    {
        for (int i = 0; i < PH_COUNT; ++i)
            if (PK.dims[i] && i != PH_S1 && i != PH_S2 && i != PH_S1T && i != PH_S4 && i != PH_S5 && i != PH_S6 && i != PH_SG)
                pk_die("--phase-kernel-cuda runs S1, S2, S1T, S4, S5, S6 and SG (servertick_phase)");
        /* the child before the tracker's thread: a fork keeps only this one */
        if (tick_start(PK.cuda_stack) != 0 || (PK.ce = tick_env_add(PK.host)) == NULL) pk_die("no device child");
    }
#endif
    img_counters_open(&PK.ctr);
    if ((PK.trk = img_track_open(err, sizeof err)) == NULL || img_track_add(PK.trk, PK.host, err, sizeof err) != 0)
    {
        printf("FAIL phase-kernel: no write tracker: %s\n", err);
        fflush(stdout);
        _exit(1);
    }
#if defined(NETHERITE_GPU_TICK)
    if (PK.cuda)
    {
        uint64_t c0[2], c1[2];
        pk_count(c0);
        PK.full_pages = env_export(PK.host, tick_down(PK.ce), NULL);
        pk_count(c1);
        if (PK.full_pages < 0 || tick_upload(PK.ce) != 0) pk_die("the first transfer to the device");
        for (int k = 0; k < 2; ++k) PK.full_ins[0][k] = c1[k] - c0[k];
        goto hook;
    }
#endif
    if (img_xfer_init(&PK.wire, PK.size) != 0) pk_die("no address space for the transfer");
    PK.dev = image_env_new();
    PK.park = image_slot_reserve(PK.size);
    if (PK.dev == NULL || PK.park == NULL || PK.dev->img.size != PK.size) pk_die("no slot for the device copy");
    if (img_track_add(PK.trk, PK.dev, err, sizeof err) != 0)
    {
        printf("FAIL phase-kernel: the device copy is not tracked: %s\n", err);
        fflush(stdout);
        _exit(1);
    }
    uint64_t c0[2], c1[2], c2[2];
    pk_count(c0);
    PK.full_pages = env_export(PK.host, &PK.wire, NULL);
    pk_count(c1);
    if (PK.full_pages < 0 || env_import(PK.dev, &PK.wire, PK.trk) != PK.full_pages) pk_die("the first transfer");
    pk_count(c2);
    for (int k = 0; k < 2; ++k)
    {
        PK.full_ins[0][k] = c1[k] - c0[k];
        PK.full_ins[1][k] = c2[k] - c1[k];
    }
    img_xfer_trim(&PK.wire);
#if defined(NETHERITE_GPU_TICK)
hook:
#endif
    /* the rows: a named mapping (the grave skips it), committed as written */
    PK.caprows = PK_MAX_ROWS;
    PK.rows = img_map_named(PK.caprows * sizeof *PK.rows, "nw-phasekernel");
    if (PK.rows == NULL) pk_die("no room for the rows");
    if (PK.ref_path != NULL && (PK.ref = fopen(PK.ref_path, "r")) == NULL)
    {
        printf("FAIL phase-kernel: --phase-kernel-ref %s does not open\n", PK.ref_path);
        fflush(stdout);
        _exit(2);
    }
    uint32_t mask = 0;
    for (int i = 0; i < PH_COUNT; ++i)
        if (PK.dims[i]) mask |= 1u << i;
    nw_env->phase.kernel = pk_kernel;
    nw_env->phase.kernel_mask = mask;
    /* without a reference the tracer stays off (its digests would write the
     * image too, and count as the host's writes) */
    if (PK.ref_path == NULL) nw_env->phase.on |= PHASE_KERNEL;
    printf("phase-kernel: %s on the device copy; the whole image is %ld pages (%.1f MB) of the live extents\n", PK.list,
           PK.full_pages, (double)PK.full_pages * IMG_PAGE / 1e6);
}

/* the next trace line of the reference, without its newline */
static const char *pk_ref_next(void)
{
    for (;;)
    {
        ssize_t n = getline(&PK.ref_line, &PK.ref_cap, PK.ref);
        if (n < 0) return NULL;
        if (n > 0 && PK.ref_line[n - 1] == '\n') PK.ref_line[--n] = 0;
        if (PK.ref_line[0] != '#') return PK.ref_line;
    }
}

/* one entry's label and digest, from s; the end of the entry */
static const char *pk_entry(const char *s, char *label, size_t ln, char *hex, size_t hn)
{
    while (*s == ' ') ++s;
    const char *eq = s, *e;
    while (*eq && *eq != '=' && *eq != ' ') ++eq;
    for (e = eq; *e && *e != ' '; ++e) {}
    snprintf(label, ln, "%.*s", (int)(eq - s), s);
    snprintf(hex, hn, "%.*s", *eq == '=' ? (int)(e - eq - 1) : 0, *eq == '=' ? eq + 1 : "");
    return e;
}

/* After each row's tick: the row's cost, and its digests against the
 * reference's line (traced says the tracer wrote one) */
static void pk_row_end(int64_t t, int traced)
{
    PK.cur.t = t;
    if (PK.cur.runs && PK.nrows < PK.caprows) PK.rows[PK.nrows++] = PK.cur;
    memset(&PK.cur, 0, sizeof PK.cur);
    if (PK.ref == NULL || !traced) return;

    const struct phase_env *ph = &nw_env->phase;
    int len = ph->linen;
    while (len > 0 && ph->line[len - 1] == '\n') --len;
    char got[16384];
    snprintf(got, sizeof got, "%.*s", len, ph->line);
    const char *want = pk_ref_next();
    if (want == NULL)
    {
        if (PK.mismatches++ == 0) printf("FAIL phase-kernel: row %lld: the reference trace has ended\n", (long long)t);
        return;
    }
    if (!strcmp(got, want)) return;
    if (PK.mismatches++) return;
    const char *a = got, *b = want;
    char la[32], lb[32], ha[32], hb[32];
    a = pk_entry(a, la, sizeof la, ha, sizeof ha);
    b = pk_entry(b, lb, sizeof lb, hb, sizeof hb);
    if (strcmp(la, lb))
    {
        printf("FAIL phase-kernel: row %lld: the reference trace's line is for row %s\n", (long long)t, lb);
        PK.mismatch_row = t;
        return;
    }
    while (*a || *b)
    {
        a = pk_entry(a, la, sizeof la, ha, sizeof ha);
        b = pk_entry(b, lb, sizeof lb, hb, sizeof hb);
        if (strcmp(la, lb) || strcmp(ha, hb))
        {
            PK.mismatch_row = t;
            snprintf(PK.mismatch_label, sizeof PK.mismatch_label, "%s", la[0] ? la : lb);
            snprintf(PK.mismatch_got, sizeof PK.mismatch_got, "%s", ha);
            snprintf(PK.mismatch_want, sizeof PK.mismatch_want, "%s", hb);
            printf("FAIL phase-kernel: row %lld phase %s: digest %s, the trace has %s%s%s\n", (long long)t,
                   PK.mismatch_label, ha[0] ? ha : "(none)", strcmp(la, lb) ? lb : "", strcmp(la, lb) ? "=" : "",
                   hb[0] ? hb : "(none)");
            return;
        }
    }
}

static int pk_u64_cmp(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* mean, p50, p99 and max of one column over the rows but the first (the
 * steady rows: the first builds the region store) */
static void pk_stat(size_t off, int is32, double out[4])
{
    size_t n = PK.nrows > 1 ? PK.nrows - 1 : 0;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (n == 0) return;
    uint64_t *v = proc_malloc(n * sizeof *v);
    double sum = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char *r = (const unsigned char *)&PK.rows[i + 1] + off;
        uint64_t x;
        if (is32) { uint32_t y; memcpy(&y, r, 4); x = y; }
        else memcpy(&x, r, 8);
        v[i] = x;
        sum += (double)x;
    }
    qsort(v, n, sizeof *v, pk_u64_cmp);
    out[0] = sum / (double)n;
    out[1] = (double)v[n / 2];
    out[2] = (double)v[(n * 99) / 100 < n ? (n * 99) / 100 : n - 1];
    out[3] = (double)v[n - 1];
    proc_free(v);
}

#if defined(NETHERITE_GPU_TICK)
/* the same for a float column (the batch's count is a uint32) */
static void pk_statf(size_t off, double out[4])
{
    size_t n = PK.nrows > 1 ? PK.nrows - 1 : 0;
    out[0] = out[1] = out[2] = out[3] = 0;
    if (n == 0) return;
    uint64_t *v = proc_malloc(n * sizeof *v);
    double sum = 0;
    int is_batch = off == offsetof(struct pk_row, batch);
    for (size_t i = 0; i < n; ++i)
    {
        const unsigned char *r = (const unsigned char *)&PK.rows[i + 1] + off;
        double x;
        if (is_batch) { uint32_t y; memcpy(&y, r, 4); x = y; }
        else { float y; memcpy(&y, r, 4); x = y; }
        sum += x;
        v[i] = (uint64_t)(x * 1e6);
    }
    qsort(v, n, sizeof *v, pk_u64_cmp);
    out[0] = sum / (double)n;
    out[1] = (double)v[n / 2] / 1e6;
    out[2] = (double)v[(n * 99) / 100 < n ? (n * 99) / 100 : n - 1] / 1e6;
    out[3] = (double)v[n - 1] / 1e6;
    proc_free(v);
}
#endif

/* The summary; nonzero when a check failed. */
static int pk_summary(void)
{
    if (PK.in) pk_die("the replay ended inside a stand-in");
    printf("phase-kernel: %lld phase runs round-tripped over %zu rows (%s)\n", PK.runs_total, PK.nrows, PK.list);
    printf("phase-kernel image: the first transfer %ld pages (%.1f MB): export %.1f M user + %.1f M kernel instructions, "
           "the device copy's import %.1f M + %.1f M\n",
           PK.full_pages, (double)PK.full_pages * IMG_PAGE / 1e6, PK.full_ins[0][0] / 1e6, PK.full_ins[0][1] / 1e6,
           PK.full_ins[1][0] / 1e6, PK.full_ins[1][1] / 1e6);
    static const char *const part[4] = {"host export", "device import", "device export", "host import"};
    double down[4], up[4], ins[4][2][4];
    pk_stat(offsetof(struct pk_row, down), 1, down);
    pk_stat(offsetof(struct pk_row, up), 1, up);
    if (PK.nrows)
    {
        const struct pk_row *r = &PK.rows[0];
        printf("phase-kernel first row %lld: down %u pages, up %u; host export %.1f M user + %.1f M kernel instructions, "
               "host import %.1f M + %.1f M\n",
               (long long)r->t, r->down, r->up, r->ins[0][0] / 1e6, r->ins[0][1] / 1e6, r->ins[3][0] / 1e6,
               r->ins[3][1] / 1e6);
    }
    printf("phase-kernel per row (the %zu after the first): down (host to device) pages mean %.1f p50 %.0f p99 %.0f "
           "max %.0f (%.1f KB mean); up (device to host) mean %.1f p50 %.0f p99 %.0f max %.0f (%.1f KB mean)\n",
           PK.nrows ? PK.nrows - 1 : 0, down[0], down[1], down[2], down[3], down[0] * IMG_PAGE / 1e3, up[0], up[1], up[2],
           up[3], up[0] * IMG_PAGE / 1e3);
    for (int k = 0; k < 4; ++k)
    {
        pk_stat(offsetof(struct pk_row, ins) + (size_t)k * 16, 0, ins[k][0]);
        pk_stat(offsetof(struct pk_row, ins) + (size_t)k * 16 + 8, 0, ins[k][1]);
        const double *u = ins[k][0], *kn = ins[k][1];
        printf("phase-kernel per row: %s instructions user mean %.0f p50 %.0f p99 %.0f max %.0f; kernel mean %.0f p50 %.0f "
               "p99 %.0f max %.0f\n",
               part[k], u[0], u[1], u[2], u[3], kn[0], kn[1], kn[2], kn[3]);
    }
    /* one line for tables (tests/phase_kernel_table.sh): rows, runs, the whole
     * image's pages, then mean p50 p99 max of pages down, pages up, the host
     * export's user and kernel instructions, the host import's */
    printf("phase-kernel tsv\t%zu\t%lld\t%ld", PK.nrows, PK.runs_total, PK.full_pages);
    const double *cols[6] = {down, up, ins[0][0], ins[0][1], ins[3][0], ins[3][1]};
    for (int c = 0; c < 6; ++c)
        for (int q = 0; q < 4; ++q) printf("\t%.1f", cols[c][q]);
    printf("\n");
    for (int i = 0; i < PH_COUNT; ++i)
        for (int d = 0; d < PK_DIMS; ++d)
        {
            const struct pk_phase *p = &PK.ph[i][d];
            if (!p->runs) continue;
            printf("phase-kernel phase %s%s: %llu runs, down %.1f pages per run, up %.1f (max %llu)",
                   i >= PH_WORLD_FIRST && i <= PH_WORLD_LAST ? (d == 0 ? "-1." : d == 1 ? "0." : "1.") : "",
                   PHASES[i].label, (unsigned long long)p->runs, (double)p->down / (double)p->runs,
                   (double)p->up / (double)p->runs, (unsigned long long)p->up_max);
            if (PK.cuda)
                printf("; on the device %llu, flagged %llu, device ms per run %.3f",
                       (unsigned long long)p->dev, (unsigned long long)p->faults, p->dev_ms / (double)p->runs);
            printf("\n");
        }
#if defined(NETHERITE_GPU_TICK)
    if (PK.cuda)
    {
        double dv[4], dn[4], up_[4], bt[4];
        pk_statf(offsetof(struct pk_row, dev_ms), dv);
        pk_statf(offsetof(struct pk_row, down_ms), dn);
        pk_statf(offsetof(struct pk_row, up_ms), up_);
        pk_statf(offsetof(struct pk_row, batch), bt);
        printf("phase-kernel cuda %s: %ld %s runs on the device, %ld flagged and redone in C (why %#x, the first at row %lld)\n",
               PK.name, PK.dev_runs, !strcmp(PK.list, "S6") ? "S6" : "phase", PK.dev_faults, PK.fault_bits, (long long)PK.first_fault_row);
        printf("phase-kernel cuda %s per row: device time ms mean %.3f p50 %.3f p99 %.3f max %.3f; upload ms mean %.3f p50 %.3f "
               "max %.3f; download ms mean %.3f p50 %.3f max %.3f; environments per launch mean %.1f\n",
               PK.name, dv[0], dv[1], dv[2], dv[3], dn[0], dn[1], dn[3], up_[0], up_[1], up_[3], bt[0]);
        printf("phase-kernel cuda tsv\t%s\t%zu\t%ld\t%ld\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.4f\t%.1f\n", PK.name, PK.nrows, PK.dev_runs,
               PK.dev_faults, dv[0], dv[1], dv[2], dv[3], dn[0], up_[0], bt[0]);
        if (PK.cuda_diff)
            printf("phase-kernel cuda diff: %ld runs compared, %ld pages the device wrote, %ld differ from C's; %ld pages C "
                   "wrote the device did not, %ld of them different on the device\n",
                   PK.diff_runs, PK.diff_pages, PK.diff_bad, PK.diff_c_only, PK.diff_c_bad);
        tick_report(stdout);
    }
#endif
    int bad = 0;
    if (PK.verify_every)
    {
        printf("phase-kernel verify: %ld comparisons of the host and the device copy, %ld differ\n", PK.verify_runs,
               PK.verify_bad);
        bad |= PK.verify_bad != 0;
    }
    if (PK.plant_row >= 0 && !PK.planted)
    {
        printf("FAIL phase-kernel: the plant's row and phase never ran\n");
        bad = 1;
    }
    if (PK.ref != NULL)
    {
        if (PK.mismatches == 0 && !PK.partial && pk_ref_next() != NULL)
        {
            printf("FAIL phase-kernel: the reference trace has rows past the replay's\n");
            PK.mismatches = 1;
        }
        if (PK.mismatches)
            printf("phase-kernel: FAIL %ld rows of %s differ from the trace, the first row %lld phase %s\n", PK.mismatches, PK.name,
                   (long long)PK.mismatch_row, PK.mismatch_label[0] ? PK.mismatch_label : "?");
        else printf("phase-kernel: every row's digests equal the trace's (%s)\n", PK.ref_path);
        bad |= PK.mismatches != 0;
        fclose(PK.ref);
    }
    nw_env->phase.kernel_mask = 0;
    return bad;
}
