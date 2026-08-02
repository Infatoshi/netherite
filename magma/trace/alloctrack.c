/* trace/alloctrack.c - LD_PRELOAD allocation tripwire for magma_game.
 *
 * Purpose (MEASUREMENT ONLY): quantify how much the game allocates after the
 * first frame, and attribute allocations to call sites, so a future
 * allocate-once (fixed-pool) conversion can be sized with concrete numbers.
 * Does NOT change game behaviour: it only counts + (optionally) prints. Build:
 *
 *   gcc -O2 -fPIC -shared -o trace/alloctrack.so trace/alloctrack.c -ldl
 *
 * Run:
 *   LD_PRELOAD=$PWD/trace/alloctrack.so SDL_VIDEODRIVER=dummy \
 *     ./magma_game --set alloctrack=1 --seed 19 --frames 60
 *
 * ARMING. The flag lives in the config registry (core/config.def key
 * `alloctrack`), not in this file. That flag is only known once main() has
 * parsed argv, and this .so is loaded and interposing malloc long before that,
 * so the two halves split cleanly:
 *   - counting is UNCONDITIONAL (a preloaded tripwire is already an opt-in, and
 *     counting from process start is what makes the cumulative totals honest);
 *   - REPORTING is armed by the game through the weak symbol alloctrack_arm(on),
 *     which app/game_main.c calls with cr_cfg()->alloctrack before the frame loop.
 * Nothing arms it -> nothing is printed, exactly like the old unset env var.
 *
 * The game also calls the weak symbol alloctrack_frame(frame) at the top of
 * every frame; if the .so is not preloaded both weak symbols are NULL and
 * nothing happens. Each such call prints a per-frame delta line. The destructor
 * prints cumulative totals and the top call sites (by count and by bytes) as
 * executable offsets, resolvable with addr2line.
 */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef void *(*malloc_fn)(size_t);
typedef void *(*calloc_fn)(size_t, size_t);
typedef void *(*realloc_fn)(void *, size_t);
typedef void  (*free_fn)(void *);

static malloc_fn  real_malloc;
static calloc_fn  real_calloc;
static realloc_fn real_realloc;
static free_fn    real_free;

static int   g_report;      /* armed by the game from the `alloctrack` registry key */
static int   g_init;
static void *g_fbase;       /* executable load base (dladdr dli_fbase) */

/* cumulative counters */
static unsigned long long c_malloc, c_calloc, c_realloc, c_free;
static unsigned long long b_malloc, b_calloc, b_realloc; /* bytes requested */

/* per-frame snapshot for delta reporting */
static unsigned long long snap_allocs, snap_bytes;

/* ---- call-site table (keyed by caller PC) ---- */
#define NSITE 4096
typedef struct { uintptr_t pc; unsigned long long n, bytes; } Site;
static Site sites[NSITE];
static int  nsites;

/* bootstrap buffer: dlsym() may call calloc before real_calloc is resolved. */
static char boot[65536];
static size_t boot_off;

static void resolve(void) {
    if (g_init) return;
    g_init = 1;
    real_malloc  = (malloc_fn) dlsym(RTLD_NEXT, "malloc");
    real_calloc  = (calloc_fn) dlsym(RTLD_NEXT, "calloc");
    real_realloc = (realloc_fn)dlsym(RTLD_NEXT, "realloc");
    real_free    = (free_fn)   dlsym(RTLD_NEXT, "free");
    Dl_info di;
    if (dladdr((void *)&resolve, &di)) g_fbase = di.dli_fbase;
}

static void record(uintptr_t pc, size_t bytes) {
    for (int i = 0; i < nsites; ++i)
        if (sites[i].pc == pc) { sites[i].n++; sites[i].bytes += bytes; return; }
    if (nsites < NSITE) {
        sites[nsites].pc = pc; sites[nsites].n = 1; sites[nsites].bytes = bytes;
        nsites++;
    }
}

void *malloc(size_t n) {
    if (!g_init) resolve();
    void *p = real_malloc(n);
    c_malloc++; b_malloc += n;
    record((uintptr_t)__builtin_return_address(0), n);
    return p;
}

void *calloc(size_t nm, size_t sz) {
    if (!g_init) {
        /* serve dlsym's early calloc from the bootstrap buffer */
        if (!real_calloc) {
            size_t need = nm * sz;
            need = (need + 15) & ~(size_t)15;
            if (boot_off + need <= sizeof boot) {
                void *p = boot + boot_off; boot_off += need; return p;
            }
        }
        resolve();
    }
    void *p = real_calloc(nm, sz);
    c_calloc++; b_calloc += nm * sz;
    record((uintptr_t)__builtin_return_address(0), nm * sz);
    return p;
}

void *realloc(void *q, size_t n) {
    if (!g_init) resolve();
    void *p = real_realloc(q, n);
    c_realloc++; b_realloc += n;
    record((uintptr_t)__builtin_return_address(0), n);
    return p;
}

void free(void *q) {
    if (q >= (void *)boot && q < (void *)(boot + sizeof boot)) return; /* bootstrap */
    if (!g_init) resolve();
    c_free++;
    real_free(q);
}

/* Armed by the game (weak) from the `alloctrack` registry key, once, before the
 * frame loop. Until then nothing is printed; the counters run regardless. */
void alloctrack_arm(int on) { g_report = on ? 1 : 0; }

/* called by the game (weak) at the top of every frame while armed. */
void alloctrack_frame(int frame) {
    if (!g_report) return;
    unsigned long long allocs = c_malloc + c_calloc + c_realloc;
    unsigned long long bytes  = b_malloc + b_calloc + b_realloc;
    fprintf(stderr,
        "[alloctrack] frame %d cum_allocs=%llu cum_bytes=%llu | delta_allocs=%llu delta_bytes=%llu\n",
        frame, allocs, bytes, allocs - snap_allocs, bytes - snap_bytes);
    snap_allocs = allocs; snap_bytes = bytes;
}

static int cmp_n(const void *a, const void *b) {
    const Site *x = a, *y = b;
    return (y->n > x->n) - (y->n < x->n);
}
static int cmp_b(const void *a, const void *b) {
    const Site *x = a, *y = b;
    return (y->bytes > x->bytes) - (y->bytes < x->bytes);
}

/* Resolve one site's PC to its object + object-relative offset (feed offset to
 * addr2line -e <object>). dli_sname is the nearest exported symbol (may be blank
 * for a file-static function; the offset still resolves via addr2line). */
static void at_print(const Site *s) {
    Dl_info di;
    const char *obj = "?", *sym = "";
    size_t off = 0;
    if (dladdr((void *)s->pc, &di) && di.dli_fbase) {
        obj = di.dli_fname ? di.dli_fname : "?";
        const char *slash = strrchr(obj, '/');
        if (slash) obj = slash + 1;
        off = (size_t)(s->pc - (uintptr_t)di.dli_fbase);
        if (di.dli_sname) sym = di.dli_sname;
    }
    fprintf(stderr, "  n=%-8llu bytes=%-12llu %s+0x%zx  %s\n",
            s->n, s->bytes, obj, off, sym);
}

__attribute__((destructor))
static void summary(void) {
    if (!g_report) return;
    fprintf(stderr, "\n[alloctrack] ===== SUMMARY =====\n");
    fprintf(stderr, "[alloctrack] malloc=%llu (%llu B)  calloc=%llu (%llu B)  "
                    "realloc=%llu (%llu B)  free=%llu\n",
            c_malloc, b_malloc, c_calloc, b_calloc, c_realloc, b_realloc, c_free);
    fprintf(stderr, "[alloctrack] total_allocs=%llu total_bytes=%llu  fbase=%p\n",
            c_malloc + c_calloc + c_realloc, b_malloc + b_calloc + b_realloc, g_fbase);
    fprintf(stderr, "[alloctrack] (offset = pc - fbase; resolve with: addr2line -f -e magma_game_dbg <offset>)\n");

    fprintf(stderr, "[alloctrack] distinct_call_sites=%d\n", nsites);
    static Site tmp[NSITE];
    memcpy(tmp, sites, sizeof(Site) * nsites);
    qsort(tmp, nsites, sizeof(Site), cmp_n);
    fprintf(stderr, "[alloctrack] --- top call sites by COUNT ---\n");
    for (int i = 0; i < nsites && i < 16; ++i) at_print(&tmp[i]);
    memcpy(tmp, sites, sizeof(Site) * nsites);
    qsort(tmp, nsites, sizeof(Site), cmp_b);
    fprintf(stderr, "[alloctrack] --- top call sites by BYTES ---\n");
    for (int i = 0; i < nsites && i < 16; ++i) at_print(&tmp[i]);
}
