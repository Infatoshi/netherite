/* The phase profiler (phase.h): per row, every phase run's and sub-mark's
 * TSC time, user-space instructions and cycles (the process's own hardware
 * counters, perf_event_open; load-independent where the time is not) and
 * heap allocations (tests/allocount.c preloaded), and a summary per phase.
 * The harness's: it runs only when a test turns it on (test_snapshots
 * --phase-profile), reads the counters at the marks and writes nothing the
 * tick reads. Its rows live in one mapping made at the start, so the profile
 * itself allocates nothing inside a row. */
#define _GNU_SOURCE
#include "phase.h"

#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <time.h>
#include <unistd.h>
#if defined(__linux__)
#include <linux/perf_event.h>
#include <sys/syscall.h>
#endif

#include "arena.h"
#include "env.h"

#define PP (nw_env->phase.prof)

/* rows the mapping holds (reserved, committed as written) */
#define PHP_ROWS_CAP ((size_t)1 << 19)

static const char *const SUB_NAMES[PS_COUNT] = {"LOAD", "GEN", "POP", "LIGHT", "NEWLIGHT", "LFLAGS", "RELIGHT", "RTICK", "PATH", "SEEDW",
                                                  "GRAVE", "WBEGIN", "PARK", "S2CCOPY"};

static inline uint64_t now_cyc(void)
{
#if defined(__x86_64__)
    return __builtin_ia32_rdtsc();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
#endif
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

/* ---------------------------------------------------- the hardware counters */

#if defined(__linux__)
static int pmu_open(uint64_t config)
{
    struct perf_event_attr a;
    memset(&a, 0, sizeof a);
    a.size = sizeof a;
    a.type = PERF_TYPE_HARDWARE;
    a.config = config;
    a.exclude_kernel = 1;
    a.exclude_hv = 1;
    return (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
}

/* the count through the event's mmap page (rdpmc under its seqlock) */
static inline uint64_t pmu_rdpmc(const struct perf_event_mmap_page *pc)
{
    uint32_t seq;
    uint64_t count;
    do
    {
        seq = pc->lock;
        __atomic_signal_fence(__ATOMIC_SEQ_CST);
        uint32_t idx = pc->index;
        count = (uint64_t)pc->offset;
#if defined(__x86_64__)
        if (pc->cap_user_rdpmc && idx)
        {
            uint64_t v = __builtin_ia32_rdpmc((int)idx - 1);
            unsigned w = pc->pmc_width;
            v <<= 64 - w;
            count += (uint64_t)((int64_t)v >> (64 - w));
        }
#endif
        __atomic_signal_fence(__ATOMIC_SEQ_CST);
    } while (pc->lock != seq);
    return count;
}
#endif

static inline uint64_t pmu_read(int k)
{
#if defined(__linux__)
    if (PP.pmu_mode == 2) return pmu_rdpmc(PP.pmu_page[k]);
    if (PP.pmu_mode == 1)
    {
        uint64_t v = 0;
        if (read(PP.pmu_fd[k], &v, sizeof v) != (ssize_t)sizeof v) return 0;
        return v;
    }
#endif
    (void)k;
    return 0;
}

uint64_t phase_prof_instructions(void)
{
    return pmu_read(0);
}

static void pmu_start(void)
{
    PP.pmu_fd[0] = PP.pmu_fd[1] = -1;
    PP.pmu_mode = 0;
#if defined(__linux__)
    PP.pmu_fd[0] = pmu_open(PERF_COUNT_HW_INSTRUCTIONS);
    PP.pmu_fd[1] = pmu_open(PERF_COUNT_HW_CPU_CYCLES);
    if (PP.pmu_fd[0] < 0 || PP.pmu_fd[1] < 0) return;
    PP.pmu_mode = 2;
    long page = sysconf(_SC_PAGESIZE);
    for (int k = 0; k < 2; ++k)
    {
        PP.pmu_page[k] = mmap(NULL, (size_t)page, PROT_READ, MAP_SHARED, PP.pmu_fd[k], 0);
        if (PP.pmu_page[k] == MAP_FAILED)
        {
            PP.pmu_page[k] = NULL;
            PP.pmu_mode = 1;
        }
        else if (!((const struct perf_event_mmap_page *)PP.pmu_page[k])->cap_user_rdpmc) PP.pmu_mode = 1;
    }
#endif
}

static void pmu_stop(void)
{
#if defined(__linux__)
    long page = sysconf(_SC_PAGESIZE);
    for (int k = 0; k < 2; ++k)
    {
        if (PP.pmu_page[k] != NULL) munmap(PP.pmu_page[k], (size_t)page);
        if (PP.pmu_fd[k] >= 0) close(PP.pmu_fd[k]);
        PP.pmu_page[k] = NULL;
        PP.pmu_fd[k] = -1;
    }
#endif
    PP.pmu_mode = 0;
}

/* every metric now; the TSC last on the way in, first on the way out */
static inline void sample_in(uint64_t *x)
{
    x[PHX_ALLOCS] = PP.allocs != NULL ? *PP.allocs : 0;
    x[PHX_CYC] = pmu_read(1);
    x[PHX_INS] = pmu_read(0);
    x[PHX_TSC] = now_cyc();
}

static inline void sample_out(uint64_t *x)
{
    x[PHX_TSC] = now_cyc();
    x[PHX_INS] = pmu_read(0);
    x[PHX_CYC] = pmu_read(1);
    x[PHX_ALLOCS] = PP.allocs != NULL ? *PP.allocs : 0;
}

static inline void add_col(int col, const uint64_t *x0)
{
    uint64_t x[PHX_COUNT];
    sample_out(x);
    for (int m = 0; m < PHX_COUNT; ++m) PP.cur.v[m][col] += x[m] - x0[m];
    ++PP.runs[col];
}

void phase_profile_start(void)
{
    memset(&PP, 0, sizeof PP);
    /* a named mapping (arena.h fixed_array): it holds no references, and the
     * grave's scan skips it rather than reading its gigabyte of address space */
    PP.rows = fixed_array(PHP_ROWS_CAP, sizeof(struct phase_prof_row));
    PP.cap = PHP_ROWS_CAP;
    /* tests/allocount.c, when preloaded, counts every malloc, calloc,
     * realloc and aligned allocation of the process */
    PP.allocs = dlsym(RTLD_DEFAULT, "nw_alloc_calls");
    pmu_start();
    PP.t0_ns = now_ns();
    PP.t0_cyc = now_cyc();
    nw_env->phase.on |= PHASE_PROF;
}

static int slot_of(int id)
{
    int d = 1;
    if (id >= PH_WORLD_FIRST && id <= PH_WORLD_LAST) d = nw_env->phase.world_dim + 1;
    if (d < 0 || d > 2) d = 1;
    return id * 3 + d;
}

void phase_prof_begin_(int id)
{
    (void)id;
    if (PP.in_row) sample_in(PP.ph0);
}

void phase_prof_end_(int id)
{
    if (PP.in_row) add_col(slot_of(id), PP.ph0);
}

void phase_sub_begin_(int sub)
{
    if (PP.sub_depth[sub]++ == 0 && PP.in_row) sample_in(PP.sub0[sub]);
}

void phase_sub_end_(int sub)
{
    if (--PP.sub_depth[sub] == 0 && PP.in_row) add_col(PHP_SLOTS + sub, PP.sub0[sub]);
}

void phase_prof_row_begin_(int server_rows)
{
    if (!server_rows)
    {
        ++PP.skipped;
        PP.in_row = 0;
        return;
    }
    memset(&PP.cur, 0, sizeof PP.cur);
    PP.cur.t = nw_env->phase.row;
    PP.in_row = 1;
    /* a sub-mark open across the row's start (none in the tick) is not counted */
    memset(PP.sub_depth, 0, sizeof PP.sub_depth);
    sample_in(PP.row0);
    if (PP.first_row_cyc == 0) PP.first_row_cyc = PP.row0[PHX_TSC];
}

void phase_prof_row_end_(void)
{
    if (!PP.in_row) return;
    uint64_t x[PHX_COUNT];
    sample_out(x);
    PP.in_row = 0;
    for (int m = 0; m < PHX_COUNT; ++m) PP.cur.total[m] = x[m] - PP.row0[m];
    if (PP.n < PP.cap) PP.rows[PP.n] = PP.cur;
    ++PP.n;
}

/* ------------------------------------------------------------- the summary */

static int cmp_u64(const void *a, const void *b)
{
    uint64_t x = *(const uint64_t *)a, y = *(const uint64_t *)b;
    return x < y ? -1 : x > y;
}

/* nearest rank */
static uint64_t pct(const uint64_t *sorted, size_t n, double q)
{
    if (n == 0) return 0;
    size_t k = (size_t)(q * (double)n + 0.999999);
    if (k < 1) k = 1;
    if (k > n) k = n;
    return sorted[k - 1];
}

static void col_name(int col, char *buf, size_t n)
{
    if (col >= PHP_COLS) snprintf(buf, n, "(between)");
    else if (col >= PHP_SLOTS) snprintf(buf, n, "~%s", SUB_NAMES[col - PHP_SLOTS]);
    else
    {
        int id = col / 3, d = col % 3 - 1;
        if (id >= PH_WORLD_FIRST && id <= PH_WORLD_LAST) snprintf(buf, n, "%d.%s", d, PHASES[id].label);
        else snprintf(buf, n, "%s", PHASES[id].label);
    }
}

/* a row's value of metric m in column col; col PHP_COLS is the row outside every phase mark */
static uint64_t colv(const struct phase_prof_row *r, int m, int col)
{
    if (col < PHP_COLS) return r->v[m][col];
    uint64_t s = 0;
    for (int k = 0; k < PHP_SLOTS; ++k) s += r->v[m][k];
    return r->total[m] > s ? r->total[m] - s : 0;
}

struct dist { uint64_t sum, p50, p99, max; int64_t tmax; };

/* the distribution over rows of metric m in column col (col -1: the row's total) */
static struct dist distribution(uint64_t *scratch, size_t n, int m, int col)
{
    struct dist d = {0, 0, 0, 0, 0};
    size_t imax = 0;
    for (size_t i = 0; i < n; ++i)
    {
        const struct phase_prof_row *r = &PP.rows[i];
        scratch[i] = col < 0 ? r->total[m] : colv(r, m, col);
        d.sum += scratch[i];
        if (scratch[i] > scratch[imax]) imax = i;
    }
    d.tmax = PP.rows[imax].t;
    d.max = scratch[imax];
    qsort(scratch, n, sizeof *scratch, cmp_u64);
    d.p50 = pct(scratch, n, 0.5);
    d.p99 = pct(scratch, n, 0.99);
    return d;
}

/* the row's phases, then its sub-marks, each at 5% of the row's metric m
 * or more, largest first */
static void row_parts(FILE *to, const struct phase_prof_row *r, int m)
{
    for (int pass = 0; pass < 2; ++pass)
    {
        int lo = pass ? PHP_SLOTS : 0, hi = pass ? PHP_COLS : PHP_SLOTS, shown = 0;
        uint64_t cap = UINT64_MAX;
        for (int k = 0; k < 4; ++k)
        {
            int best = -1;
            for (int col = lo; col < hi; ++col)
                if (r->v[m][col] < cap && r->v[m][col] >= r->total[m] / 20 && (best < 0 || r->v[m][col] > r->v[m][best]))
                    best = col;
            if (best < 0) break;
            char name[32];
            col_name(best, name, sizeof name);
            fprintf(to, "%s %s %.1f%%", shown++ ? "," : pass ? " |" : "", name,
                    100.0 * (double)r->v[m][best] / (double)(r->total[m] ? r->total[m] : 1));
            cap = r->v[m][best];
        }
    }
}

/* the five worst rows by metric m */
static void worst_rows(FILE *to, size_t n, int m, const char *what, double us)
{
    size_t order[5];
    int norder = 0;
    for (int k = 0; k < 5 && (size_t)k < n; ++k)
    {
        size_t best = n;
        for (size_t i = 0; i < n; ++i)
        {
            int taken = 0;
            for (int j = 0; j < norder; ++j) taken |= order[j] == i;
            if (!taken && (best == n || PP.rows[i].total[m] > PP.rows[best].total[m])) best = i;
        }
        order[norder++] = best;
    }
    for (int k = 0; k < norder; ++k)
    {
        const struct phase_prof_row *r = &PP.rows[order[k]];
        fprintf(to, "phase profile: worst by %s #%d row %lld: %.1f us, %.1f k instructions, %llu allocations:", what, k + 1,
                (long long)r->t, (double)r->total[PHX_TSC] * us, (double)r->total[PHX_INS] / 1e3,
                (unsigned long long)r->total[PHX_ALLOCS]);
        row_parts(to, r, m);
        fprintf(to, "\n");
    }
}

size_t phase_profile_totals(uint64_t *rows_ins, uint64_t *sub_ins)
{
    size_t n = PP.n < PP.cap ? PP.n : PP.cap;
    *rows_ins = 0;
    for (int k = 0; k < PS_COUNT; ++k) sub_ins[k] = 0;
    if (PP.pmu_mode == 0) return 0;
    for (size_t i = 0; i < n; ++i)
    {
        *rows_ins += PP.rows[i].total[PHX_INS];
        for (int k = 0; k < PS_COUNT; ++k) sub_ins[k] += PP.rows[i].v[PHX_INS][PHP_SLOTS + k];
    }
    return n;
}

void phase_profile_summary(FILE *to, FILE *rows_out)
{
    size_t n = PP.n < PP.cap ? PP.n : PP.cap;
    uint64_t end_cyc = now_cyc(), end_ns = now_ns();
    double ns_per = end_cyc > PP.t0_cyc ? (double)(end_ns - PP.t0_ns) / (double)(end_cyc - PP.t0_cyc) : 1.0;
    double us = ns_per / 1000.0;
    int have_allocs = PP.allocs != NULL, have_pmu = PP.pmu_mode != 0;
    static const char *const pmu_modes[3] = {"not counted (perf_event_open failed)", "counted (read)", "counted (rdpmc)"};

    fprintf(to, "phase profile: %zu rows profiled, %ld without the server; clock %.4f ns per count; allocations %s; "
                "user instructions and cycles %s\n",
            n, PP.skipped, ns_per, have_allocs ? "counted (allocount.so)" : "not counted (preload out/native/allocount.so)",
            pmu_modes[PP.pmu_mode]);
    if (n == 0)
    {
        pmu_stop();
        return;
    }

    uint64_t *v = malloc(n * sizeof *v);
    struct dist t = distribution(v, n, PHX_TSC, -1);
    double setup_ms = PP.first_row_cyc > PP.t0_cyc ? (double)(PP.first_row_cyc - PP.t0_cyc) * ns_per / 1e6 : 0.0;
    fprintf(to,
            "phase profile: tick time total %.1f ms, mean %.1f us, p50 %.1f us, p99 %.1f us, max %.1f us (row %lld), "
            "%.0f ticks/s simulated; setup before the first row %.1f ms\n",
            (double)t.sum * ns_per / 1e6, (double)t.sum * us / (double)n, (double)t.p50 * us, (double)t.p99 * us,
            (double)t.max * us, (long long)t.tmax, 1e9 / ((double)t.sum * ns_per / (double)n), setup_ms);
    struct dist in = distribution(v, n, PHX_INS, -1);
    struct dist cy = distribution(v, n, PHX_CYC, -1);
    if (have_pmu)
        fprintf(to,
                "phase profile: tick instructions total %.3f G, mean %.1f k, p50 %.1f k, p99 %.1f k, max %.1f k (row %lld); "
                "user cycles mean %.1f k; IPC %.2f\n",
                (double)in.sum / 1e9, (double)in.sum / 1e3 / (double)n, (double)in.p50 / 1e3, (double)in.p99 / 1e3,
                (double)in.max / 1e3, (long long)in.tmax, (double)cy.sum / 1e3 / (double)n,
                cy.sum ? (double)in.sum / (double)cy.sum : 0.0);
    if (have_allocs)
    {
        struct dist a = distribution(v, n, PHX_ALLOCS, -1);
        size_t zero = 0;
        for (size_t i = 0; i < n; ++i) zero += PP.rows[i].total[PHX_ALLOCS] == 0;
        fprintf(to, "phase profile: allocations per tick mean %.2f, p50 %llu, p99 %llu, max %llu (row %lld); %zu of %zu ticks allocate nothing\n",
                (double)a.sum / (double)n, (unsigned long long)a.p50, (unsigned long long)a.p99,
                (unsigned long long)a.max, (long long)a.tmax, zero, n);
    }

    /* per column: time (share, per-row p50, p99, max), allocations, instructions */
    fprintf(to, "phase profile: %-10s %8s %10s %7s %9s %9s %9s %10s %9s %7s %9s %9s %10s %5s %s\n", "column", "runs",
            "total ms", "share", "mean us", "p50 us", "p99 us", "max us", "allocs/t", "ins%", "kins/t", "p99 kins",
            "max kins", "IPC", "max row");
    for (int col = 0; col <= PHP_COLS; ++col)
    {
        if (col < PHP_COLS && PP.runs[col] == 0) continue;
        uint64_t allocs = 0;
        for (size_t i = 0; i < n; ++i) allocs += colv(&PP.rows[i], PHX_ALLOCS, col);
        struct dist ct = distribution(v, n, PHX_TSC, col);
        struct dist ci = distribution(v, n, PHX_INS, col);
        struct dist cc = distribution(v, n, PHX_CYC, col);
        char name[32];
        col_name(col, name, sizeof name);
        fprintf(to, "phase profile: %-10s %8llu %10.2f %6.2f%% %9.2f %9.2f %9.2f %10.1f %9.2f %6.2f%% %9.2f %9.2f %10.1f %5.2f %lld\n",
                name, (unsigned long long)(col < PHP_COLS ? PP.runs[col] : n), (double)ct.sum * ns_per / 1e6,
                100.0 * (double)ct.sum / (double)t.sum, (double)ct.sum * us / (double)n, (double)ct.p50 * us,
                (double)ct.p99 * us, (double)ct.max * us, (double)allocs / (double)n,
                in.sum ? 100.0 * (double)ci.sum / (double)in.sum : 0.0, (double)ci.sum / 1e3 / (double)n,
                (double)ci.p99 / 1e3, (double)ci.max / 1e3, cc.sum ? (double)ci.sum / (double)cc.sum : 0.0,
                (long long)ct.tmax);
    }

    worst_rows(to, n, PHX_TSC, "time", us);
    if (have_pmu) worst_rows(to, n, PHX_INS, "instructions", us);

    /* the environment's bytes by owner: test_snapshots --mem (envmem.c) */
    struct rusage ru;
    getrusage(RUSAGE_SELF, &ru);
    fprintf(to, "phase profile: peak RSS %.1f MB (the process, harness included)\n", (double)ru.ru_maxrss / 1024.0);

    if (rows_out != NULL)
    {
        /* each column's time (us), then its instructions (i:) */
        fprintf(rows_out, "t\ttotal_us\ttotal_ins\tallocs");
        for (int pass = 0; pass < 2; ++pass)
            for (int col = 0; col < PHP_COLS; ++col)
                if (PP.runs[col])
                {
                    char name[32];
                    col_name(col, name, sizeof name);
                    fprintf(rows_out, "\t%s%s", pass ? "i:" : "", name);
                }
        fprintf(rows_out, "\n");
        for (size_t i = 0; i < n; ++i)
        {
            const struct phase_prof_row *r = &PP.rows[i];
            fprintf(rows_out, "%lld\t%.2f\t%llu\t%llu", (long long)r->t, (double)r->total[PHX_TSC] * us,
                    (unsigned long long)r->total[PHX_INS], (unsigned long long)r->total[PHX_ALLOCS]);
            for (int col = 0; col < PHP_COLS; ++col)
                if (PP.runs[col]) fprintf(rows_out, "\t%.2f", (double)r->v[PHX_TSC][col] * us);
            for (int col = 0; col < PHP_COLS; ++col)
                if (PP.runs[col]) fprintf(rows_out, "\t%llu", (unsigned long long)r->v[PHX_INS][col]);
            fprintf(rows_out, "\n");
        }
    }
    free(v);
    pmu_stop();
    munmap(PP.rows, PP.cap * sizeof(struct phase_prof_row));
    PP.rows = NULL;
    PP.n = PP.cap = 0;
}
