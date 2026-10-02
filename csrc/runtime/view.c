/* The observation's view per environment (view.h). */
#define _GNU_SOURCE
#include "view.h"

#include <dlfcn.h>
#include <elf.h>
#include <link.h>
#include <fcntl.h>
#include <limits.h>
#include <pthread.h>
#include <linux/perf_event.h>
#include <sys/syscall.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#include "../engine/env.h"
#include "../engine/image.h"
#include "../engine/raster_obs.h"
#include "../engine/tape.h"
#include "pool.h"

/* what an env's view spent (view_times' fields): its own, written only by
 * the worker stepping the env and summed by view_times with the envs idle,
 * so no core writes a line another core's env uses (a line of its own) */
struct view_sums {
    _Alignas(64) uint64_t tick_ns, frame_ns, reset_ns, tick_ins, frame_ins, reset_ins, tick_cyc, frame_cyc, frames, meshed, drawn;
    double feed_ms;
    uint64_t reused, empty, checked, check_bad, tex_checked, tex_bad, band_checked, band_bad;
};

struct view {
    int fd;                     /* the copy's memory file */
    void *h;                    /* the loaded copy */
    const struct pv_api *api;
    int open;                   /* api->open succeeded since the last load */
    struct raster_obs obs;
    unsigned char *rgb;         /* draw mode: the C frame */
    uint64_t tex_checked, tex_bad, band_checked, band_bad;   /* the copy's pv_frame_stats totals, as last read */
    struct view_sums sum;
};

struct view_set {
    int n, draw;                /* n: the envs' views (vs->v holds n + spares) */
    int spares;
    int counters;               /* view_set_new's: the tick hooks timed and the counters read */
    struct pv_config cfg;
    char assets[PATH_MAX];
    struct view *v;
};

/* the calling thread's user instructions and cycles (0 without the
 * counters): one group, read at once, with its time enabled and running
 * (ctr_add scales a difference by them: a profiler's events multiplex with
 * the group, pool.c pmu_read) */
struct thread_ctr { uint64_t ins, cyc, en, run; };
static struct thread_ctr thread_ctr(void)
{
    static _Thread_local int fd = -2;
    if (fd == -2)
    {
        struct perf_event_attr a;
        memset(&a, 0, sizeof a);
        a.size = sizeof a;
        a.type = PERF_TYPE_HARDWARE;
        a.config = PERF_COUNT_HW_INSTRUCTIONS;
        a.exclude_kernel = 1;
        a.exclude_hv = 1;
        a.read_format = PERF_FORMAT_GROUP | PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING;
        fd = (int)syscall(SYS_perf_event_open, &a, 0, -1, -1, 0);
        a.config = PERF_COUNT_HW_CPU_CYCLES;
        if (fd >= 0 && syscall(SYS_perf_event_open, &a, 0, -1, fd, 0) < 0) { close(fd); fd = -1; }
    }
    uint64_t v[5] = {0};
    struct thread_ctr c = {0, 0, 0, 0};
    if (fd < 0 || read(fd, v, sizeof v) != (ssize_t)sizeof v) return c;
    c.en = v[1];
    c.run = v[2];
    c.ins = v[3];
    c.cyc = v[4];
    return c;
}

/* b less a, scaled to the time enabled, into *ins and *cyc */
static void ctr_add(struct thread_ctr a, struct thread_ctr b, uint64_t *ins, uint64_t *cyc)
{
    uint64_t en = b.en - a.en, run = b.run - a.run, di = b.ins - a.ins, dc = b.cyc - a.cyc;
    if (run > 0 && run < en)
    {
        di = (uint64_t)((double)di * (double)en / (double)run);
        dc = (uint64_t)((double)dc * (double)en / (double)run);
    }
    *ins += di;
    if (cyc) *cyc += dc;
}

/* The views' shared store (pv_config.shared_json): each table parsed once
 * for the process, on the process's own heap (never an environment's,
 * whose reset would take it), and never freed; read-only after. */
static pthread_mutex_t shared_mu = PTHREAD_MUTEX_INITIALIZER;
static struct { char path[1200]; const struct jval *j; } shared[32];
static int nshared;

static const struct jval *view_shared_json(const char *path)
{
    pthread_mutex_lock(&shared_mu);
    const struct jval *j = NULL;
    for (int i = 0; i < nshared && !j; ++i)
        if (!strcmp(shared[i].path, path)) j = shared[i].j;
    if (!j && nshared < (int)(sizeof shared / sizeof shared[0]))
    {
        int heap = image_heap_set(nw_env, 0);
        FILE *f = fopen(path, "rb");
        char *text = NULL;
        if (f)
        {
            fseek(f, 0, SEEK_END);
            long len = ftell(f);
            fseek(f, 0, SEEK_SET);
            text = len >= 0 ? malloc((size_t)len + 1) : NULL;
            if (text && fread(text, 1, (size_t)len, f) == (size_t)len) text[len] = 0;
            else { free(text); text = NULL; }
            fclose(f);
        }
        j = text ? json_parse(text) : NULL;
        image_heap_set(nw_env, heap);
        if (j)
        {
            snprintf(shared[nshared].path, sizeof shared[nshared].path, "%s", path);
            shared[nshared++].j = j;
        }
    }
    pthread_mutex_unlock(&shared_mu);
    return j;
}

static uint64_t now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000u + (uint64_t)ts.tv_nsec;
}

/* the library's bytes; the copies are made from them */
static unsigned char *read_all(const char *path, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (f == NULL) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    /* the process's own memory: the envs never own it */
    unsigned char *b = len > 0 ? proc_malloc((size_t)len) : NULL;
    if (b && fread(b, 1, (size_t)len, f) != (size_t)len) { proc_free(b); b = NULL; }
    fclose(f);
    *n = (size_t)len;
    return b;
}

/* A copy's memory file without the bytes no load reads: dlopen reads the
 * ELF and program headers and maps the segments, so the rest (the debug
 * sections, about 3.1 MB of the 4 MB library) becomes a hole. The file is
 * shared memory charged to the process, not in its RSS: 3.97 MB a copy,
 * 508 MB at 128 envs (lane/heapprof); 0.84 MB now. */
static void punch_unloaded(int fd, const unsigned char *blob, size_t bytes)
{
    Elf64_Ehdr eh;
    if (bytes < sizeof eh) return;
    memcpy(&eh, blob, sizeof eh);
    if (memcmp(eh.e_ident, ELFMAG, SELFMAG) || eh.e_ident[EI_CLASS] != ELFCLASS64 ||
        eh.e_phoff + (uint64_t)eh.e_phnum * eh.e_phentsize > bytes || eh.e_phentsize < sizeof(Elf64_Phdr))
        return;
    uint64_t end = eh.e_phoff + (uint64_t)eh.e_phnum * eh.e_phentsize;
    for (int i = 0; i < eh.e_phnum; ++i)
    {
        Elf64_Phdr ph;
        memcpy(&ph, blob + eh.e_phoff + (uint64_t)i * eh.e_phentsize, sizeof ph);
        if (ph.p_offset + ph.p_filesz > end) end = ph.p_offset + ph.p_filesz;
    }
    /* the section headers stay (they are past the debug sections) */
    uint64_t lo = (end + 4095) & ~(uint64_t)4095, hi = (eh.e_shoff > lo && eh.e_shoff < bytes ? eh.e_shoff : bytes) & ~(uint64_t)4095;
    if (hi > lo) (void)fallocate(fd, FALLOC_FL_PUNCH_HOLE | FALLOC_FL_KEEP_SIZE, (off_t)lo, (off_t)(hi - lo));
}

static int load_copy(struct view *v, char *err, size_t n)
{
    if (v->h) dlclose(v->h);
    v->h = NULL;
    v->api = NULL;
    v->open = 0;
    v->tex_checked = v->tex_bad = v->band_checked = v->band_bad = 0;
    char path[64];
    snprintf(path, sizeof path, "/proc/self/fd/%d", v->fd);
    v->h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (v->h == NULL)
    {
        snprintf(err, n, "dlopen: %s", dlerror());
        return -1;
    }
    v->api = dlsym(v->h, "pv_api");
    if (v->api == NULL || v->api->version != PV_API_VERSION)
    {
        snprintf(err, n, "the library has no pv_api of version %d", PV_API_VERSION);
        return -1;
    }
    return 0;
}

struct view_set *view_set_new(const char *so, int n, int spares, const struct pv_config *cfg, int draw, int counters,
                              char *err, size_t en)
{
    char path[PATH_MAX];
    if (so == NULL)
    {
        ssize_t k = readlink("/proc/self/exe", path, sizeof path - 32);
        if (k <= 0) { snprintf(err, en, "cannot find the program's directory"); return NULL; }
        path[k] = 0;
        char *slash = strrchr(path, '/');
        snprintf(slash ? slash + 1 : path, 32, "playview.so");
        so = path;
    }
    size_t bytes = 0;
    unsigned char *blob = read_all(so, &bytes);
    if (blob == NULL) { snprintf(err, en, "cannot read %s", so); return NULL; }
    struct view_set *vs = proc_calloc(1, sizeof *vs);
    vs->n = n;
    vs->spares = spares;
    vs->draw = draw;
    vs->counters = counters;
    vs->cfg = *cfg;
    vs->cfg.shared_json = view_shared_json;
    if (cfg->assets)
    {
        snprintf(vs->assets, sizeof vs->assets, "%s", cfg->assets);
        vs->cfg.assets = vs->assets;
    }
    /* each view's sums on lines of their own */
    const int all = n + spares;
    vs->v = aligned_alloc(64, ((size_t)all * sizeof *vs->v + 63) & ~(size_t)63);
    if (vs->v) memset(vs->v, 0, (size_t)all * sizeof *vs->v);
    for (int i = 0; i < all; ++i)
    {
        struct view *v = &vs->v[i];
        char name[32];
        snprintf(name, sizeof name, "playview-%d", i);
        v->fd = memfd_create(name, MFD_CLOEXEC);
        if (v->fd < 0 || write(v->fd, blob, bytes) != (ssize_t)bytes)
        {
            snprintf(err, en, "view %d: the library's copy cannot be made", i);
            vs->n = i + 1;
            vs->spares = 0;
            proc_free(blob);
            view_set_free(vs);
            return NULL;
        }
        punch_unloaded(v->fd, blob, bytes);
        if (draw && i < n) v->rgb = proc_malloc((size_t)cfg->w * cfg->h * 3);
    }
    proc_free(blob);
    return vs;
}

void view_set_free(struct view_set *vs)
{
    if (vs == NULL) return;
    for (int i = 0; i < vs->n + vs->spares; ++i)
    {
        if (vs->v[i].h) dlclose(vs->v[i].h);
        if (vs->v[i].fd >= 0) close(vs->v[i].fd);
        proc_free(vs->v[i].rgb);
    }
    free(vs->v);
    proc_free(vs);
}

int view_reset_cb(void *ctx, int id, struct session *ss, char *err, size_t n)
{
    struct view_set *vs = ctx;
    struct view *v = &vs->v[id];
    uint64_t t0 = now_ns();
    struct thread_ctr c0 = vs->counters ? thread_ctr() : (struct thread_ctr){0, 0, 0, 0};
    int rc = load_copy(v, err, n);
    if (rc == 0) v->open = v->api->open(ss, &vs->cfg, err, n);
    v->sum.reset_ns += now_ns() - t0;
    if (vs->counters) ctr_add(c0, thread_ctr(), &v->sum.reset_ins, NULL);
    return rc == 0 && v->open ? 0 : -1;
}

void view_tick_cb(void *ctx, int id, int64_t t, const struct act *a, int end)
{
    struct view_set *vs = ctx;
    struct view *v = &vs->v[id];
    if (!v->open) return;
    if (!vs->counters)
    {
        if (end) v->api->tick_end(t, a);
        else v->api->tick_begin(t, a);
        return;
    }
    uint64_t t0 = now_ns();
    struct thread_ctr c0 = thread_ctr();
    if (end) v->api->tick_end(t, a);
    else v->api->tick_begin(t, a);
    struct thread_ctr c1 = thread_ctr();
    v->sum.tick_ns += now_ns() - t0;
    ctr_add(c0, c1, &v->sum.tick_ins, &v->sum.tick_cyc);
}

void view_step_cb(void *ctx, int id, struct pool_result *r)
{
    struct view_set *vs = ctx;
    struct view *v = &vs->v[id];
    if (!v->open) { r->obs = NULL; r->obs_rc = -1; return; }
    uint64_t t0 = now_ns();
    struct thread_ctr c0 = vs->counters ? thread_ctr() : (struct thread_ctr){0, 0, 0, 0};
    r->obs_rc = v->api->frame(&v->obs, !vs->draw, v->rgb);
    r->obs = r->obs_rc == 0 ? &v->obs : NULL;
    struct view_sums *m = &v->sum;
    if (vs->counters)
    {
        struct thread_ctr c1 = thread_ctr();
        ctr_add(c0, c1, &m->frame_ins, &m->frame_cyc);
    }
    m->frame_ns += now_ns() - t0;
    struct pv_frame_stats fs;
    v->api->frame_stats(&fs);
    m->feed_ms += fs.feed_ms;
    m->frames += 1;
    m->meshed += (uint64_t)fs.meshed;
    m->drawn += (uint64_t)fs.draws;
    m->reused += (uint64_t)fs.reused;
    m->empty += (uint64_t)fs.empty;
    m->checked += (uint64_t)fs.checked;
    m->check_bad += (uint64_t)fs.check_bad;
    m->tex_checked += fs.tex_checked - v->tex_checked;
    m->tex_bad += fs.tex_bad - v->tex_bad;
    v->tex_checked = fs.tex_checked;
    v->tex_bad = fs.tex_bad;
    /* a dimension change makes the renderer again: its counts start over */
    if (fs.band_checked < v->band_checked || fs.band_bad < v->band_bad) v->band_checked = v->band_bad = 0;
    m->band_checked += fs.band_checked - v->band_checked;
    m->band_bad += fs.band_bad - v->band_bad;
    v->band_checked = fs.band_checked;
    v->band_bad = fs.band_bad;
}

/* the loaded copy's mapping and writable data (pool.h struct pool_vmem):
 * its PT_LOAD segments from the loader's own list, the writable ones less
 * what the loader made read-only after relocating (PT_GNU_RELRO, page
 * granular as glibc protects it) */
struct vmem_find { uintptr_t base; struct pool_vmem *m; int found; };

static int vmem_phdr(struct dl_phdr_info *info, size_t size, void *arg)
{
    struct vmem_find *f = arg;
    (void)size;
    if ((uintptr_t)info->dlpi_addr != f->base) return 0;
    struct pool_vmem *m = f->m;
    const uintptr_t pg = (uintptr_t)sysconf(_SC_PAGESIZE);
    uintptr_t lo = UINTPTR_MAX, hi = 0, rlo = 0, rhi = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        uintptr_t a = f->base + ph->p_vaddr, b = a + ph->p_memsz;
        if (ph->p_type == PT_LOAD)
        {
            if (a < lo) lo = a;
            if (b > hi) hi = b;
        }
        else if (ph->p_type == PT_GNU_RELRO)
        {
            rlo = a & ~(pg - 1);
            rhi = b & ~(pg - 1);
        }
    }
    m->lo = lo & ~(pg - 1);
    m->hi = (hi + pg - 1) & ~(pg - 1);
    m->nseg = 0;
    for (int i = 0; i < info->dlpi_phnum; ++i)
    {
        const ElfW(Phdr) *ph = &info->dlpi_phdr[i];
        if (ph->p_type != PT_LOAD || !(ph->p_flags & PF_W)) continue;
        uintptr_t a = f->base + ph->p_vaddr, b = a + ph->p_memsz, fe = a + ph->p_filesz;
        /* the read-only part: before the writable rest, or all of it */
        if (rhi > rlo && a >= rlo && a < rhi) a = rhi;
        if (a >= b) continue;
        if (m->nseg == POOL_VMEM_SEGS) return 1;
        m->seg[m->nseg].addr = a;
        m->seg[m->nseg].len = b - a;
        m->seg[m->nseg].init = fe > a ? fe - a : 0;
        ++m->nseg;
    }
    f->found = 1;
    return 1;
}

int view_mem(struct view_set *vs, int id, struct pool_vmem *m)
{
    struct view *v = &vs->v[id];
    struct link_map *lm = NULL;
    if (v->h == NULL || dlinfo(v->h, RTLD_DI_LINKMAP, &lm) != 0 || lm == NULL) return -1;
    struct vmem_find f = {(uintptr_t)lm->l_addr, m, 0};
    memset(m, 0, sizeof *m);
    dl_iterate_phdr(vmem_phdr, &f);
    return f.found && m->nseg > 0 ? 0 : -1;
}

int view_reload(struct view_set *vs, int id, struct pool_vmem *m, char *err, size_t n)
{
    struct view *v = &vs->v[id];
    uint64_t t0 = now_ns();
    int rc = load_copy(v, err, n);
    if (rc == 0 && view_mem(vs, id, m) != 0)
    {
        snprintf(err, n, "the view's copy has no writable data the loader names");
        rc = -1;
    }
    v->sum.reset_ns += now_ns() - t0;
    return rc;
}

void view_resume(struct view_set *vs, int id)
{
    struct view *v = &vs->v[id];
    v->open = 1;
    /* the copy's totals as written: the sums go on from them */
    struct pv_frame_stats fs;
    v->api->frame_stats(&fs);
    v->tex_checked = fs.tex_checked;
    v->tex_bad = fs.tex_bad;
    v->band_checked = fs.band_checked;
    v->band_bad = fs.band_bad;
    /* the device holds none of the copy's meshes (the reset forgot them) */
    v->api->mesh_resync();
}

const unsigned char *view_drawn(struct view_set *vs, int id) { return vs->v[id].rgb; }

void view_mesh_resync(struct view_set *vs, int id)
{
    struct view *v = &vs->v[id];
    if (v->open) v->api->mesh_resync();
}

void view_render(struct view_set *vs, int id, const struct raster_obs *o, unsigned char *rgb)
{
    vs->v[id].api->render(o, rgb);
}

void view_times(struct view_set *vs, struct view_times *t)
{
    memset(t, 0, sizeof *t);
    for (int i = 0; i < vs->n; ++i)
    {
        const struct view_sums *m = &vs->v[i].sum;
        t->tick_ns += m->tick_ns;
        t->frame_ns += m->frame_ns;
        t->feed_ms += m->feed_ms;
        t->reset_ns += m->reset_ns;
        t->tick_ins += m->tick_ins;
        t->frame_ins += m->frame_ins;
        t->reset_ins += m->reset_ins;
        t->tick_cyc += m->tick_cyc;
        t->frame_cyc += m->frame_cyc;
        t->frames += m->frames;
        t->meshed += m->meshed;
        t->drawn += m->drawn;
        t->reused += m->reused;
        t->empty += m->empty;
        t->checked += m->checked;
        t->check_bad += m->check_bad;
        t->tex_checked += m->tex_checked;
        t->tex_bad += m->tex_bad;
        t->band_checked += m->band_checked;
        t->band_bad += m->band_bad;
    }
}
