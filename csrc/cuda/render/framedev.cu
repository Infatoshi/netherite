/* The judges' device frames (../../play/framedev.h): cuda/render's
 * renderer behind the ABI play loads, out/native/cuda/libframedev.so.
 *
 * The device is shared (other lanes' kernels, the nightly's light checks),
 * so a judge takes it only when it can do so politely: one of a few
 * machine-wide slots (flock on /tmp/netherite-gpu/judge.N.lock, SLOTS of
 * them, /tmp/netherite-gpu/judge.slots overrides), and only when the device
 * keeps MARGIN_MB free after the renderer's own buffers (judge.margin
 * overrides, in MB). Otherwise it declines and play draws with the C
 * renderer, which gives the same bytes. A CUDA call that fails later is
 * reported to play (-1), which closes the renderer and draws the rest in C. */
#include "../../play/framedev.h"

/* the renderer itself (render.mk's objects, linked), whose failed CUDA
 * calls unwind to the calls below instead of ending play
 * (render_fail_throws), without the device mesher (render_no_mesher,
 * nomesher.c) */
#include "dev.cuh"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

enum { SLOTS = 4, MARGIN_MB = 1536, ARENA_MB = 192 };

struct framedev {
    struct render *r;
    int slot_fd;
    unsigned char *h_rgb;   /* pinned: every frame and depth buffer of a render */
    float *h_depth;
    int n;                  /* the last render's frames */
    int w, h;
};

/* a setting's file under /tmp/netherite-gpu, or DFLT */
static int setting(const char *name, int dflt)
{
    char path[128];
    snprintf(path, sizeof path, "/tmp/netherite-gpu/%s", name);
    FILE *f = fopen(path, "r");
    int n = dflt;
    if (f) {
        if (fscanf(f, "%d", &n) != 1 || n < 0) n = dflt;
        fclose(f);
    }
    return n;
}

static int slots(void) { return setting("judge.slots", SLOTS); }

/* a free slot's lock, held (the descriptor) for the renderer's life */
static int take_slot(void)
{
    mkdir("/tmp/netherite-gpu", 0777);
    chmod("/tmp/netherite-gpu", 0777);
    int n = slots();
    for (int i = 0; i < n; ++i) {
        char path[64];
        snprintf(path, sizeof path, "/tmp/netherite-gpu/judge.%d.lock", i);
        int fd = open(path, O_RDWR | O_CREAT | O_CLOEXEC, 0666);
        if (fd < 0) continue;
        if (!flock(fd, LOCK_EX | LOCK_NB)) return fd;
        close(fd);
    }
    return -1;
}

static void fdv_close(void *p);

/* judge.slow.WxH: the last time a judge at that size found the device's
 * frames slower than the C renderer's, and why */
enum { SLOW_S = 120 };
static void slow_path(char *path, size_t n, int w, int h)
{
    snprintf(path, n, "/tmp/netherite-gpu/judge.slow.%dx%d", w, h);
}

static void *fdv_open(int max_envs, int w, int h, const struct framedev_sizes *z, int force, char *why, size_t why_n)
{
    const struct framedev_sizes mine = {sizeof(struct raster_obs), sizeof(struct raster_obs_draw),
                                        sizeof(struct raster_obs_sky), sizeof(struct raster_obs_cmd),
                                        sizeof(struct raster_obs_estate), sizeof(struct raster_obs_equad),
                                        sizeof(struct raster_obs_tex), sizeof(struct raster_obs_crack),
                                        sizeof(struct raster_obs_portal), sizeof(struct raster_obs_line)};
    if (!z || memcmp(z, &mine, sizeof mine)) {
        snprintf(why, why_n, "the plugin was built from another raster_obs.h (make -C csrc gpu-render-judge)");
        return NULL;
    }
    char sp[128];
    slow_path(sp, sizeof sp, w, h);
    struct stat st;
    time_t age = 0;
    if (!force && !stat(sp, &st) && (age = time(NULL) - st.st_mtime) >= 0 && age < SLOW_S) {
        char note[160] = "";
        FILE *f = fopen(sp, "r");
        if (f) {
            if (!fgets(note, sizeof note, f)) note[0] = 0;
            fclose(f);
        }
        note[strcspn(note, "\n")] = 0;
        snprintf(why, why_n, "a judge found the device slower %lds ago: %.150s", (long)age, note);
        return NULL;
    }
    int fd = take_slot();
    if (fd < 0) {
        snprintf(why, why_n, "no free device slot (%d, /tmp/netherite-gpu)", slots());
        return NULL;
    }
    int count = 0;
    /* a wait for the device sleeps (the suite's other jobs want the cores) */
    cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync);
    if (cudaGetDeviceCount(&count) != cudaSuccess || count < 1) {
        snprintf(why, why_n, "no CUDA device");
        close(fd);
        return NULL;
    }
    size_t fr = 0, total = 0;
    if (cudaMemGetInfo(&fr, &total) != cudaSuccess) {
        snprintf(why, why_n, "cudaMemGetInfo failed");
        close(fd);
        return NULL;
    }
    /* the arena, the frames and depth, the atlases, a start of meshes and textures */
    size_t need = ((size_t)ARENA_MB << 20) + (size_t)max_envs * w * h * 7 + ((size_t)max_envs * 64 << 20);
    int margin = setting("judge.margin", MARGIN_MB);
    if (fr < need + ((size_t)margin << 20)) {
        snprintf(why, why_n, "the device has %zu MB free, the renderer needs %zu MB and leaves %d", fr >> 20, need >> 20,
                 margin);
        close(fd);
        return NULL;
    }
    struct render *r = NULL;
    render_fail_throws(1);
    try { r = render_new(max_envs, w, h, ARENA_MB); } catch (const render_error &) { r = NULL; }
    if (!r) {
        snprintf(why, why_n, "render_new failed");
        close(fd);
        return NULL;
    }
    render_no_mesher(r);
    render_one_trip(r, 1);   /* one wait a render: each is a turn of the shared device's time slices */
    struct framedev *d = (struct framedev *)calloc(1, sizeof *d);
    d->r = r;
    d->slot_fd = fd;
    d->w = w;
    d->h = h;
    if (cudaMallocHost((void **)&d->h_rgb, (size_t)max_envs * w * h * 3) != cudaSuccess ||
        cudaMallocHost((void **)&d->h_depth, (size_t)max_envs * w * h * sizeof(float)) != cudaSuccess) {
        snprintf(why, why_n, "cudaMallocHost failed");
        fdv_close(d);
        return NULL;
    }
    return d;
}

static int fdv_set(void *p, int env, const struct raster_obs *o)
{
    try { return render_set(((struct framedev *)p)->r, env, o); } catch (const render_error &) { return -1; }
}

static int fdv_render(void *p, int n, double *ms)
{
    struct framedev *d = (struct framedev *)p;
    /* the frames come back with the render (no second wait) */
    render_frames_to(d->r, d->h_rgb, d->h_depth);
    try { if (!render_render(d->r, n)) return -1; } catch (const render_error &) { return -1; }
    if (ms) *ms = render_stats(d->r)->total_ms;
    d->n = n;
    return 0;
}

static int fdv_download(void *p, int n, unsigned char *const *rgb, float *const *depth)
{
    struct framedev *d = (struct framedev *)p;
    if (n < 1 || n != d->n) return -1;
    size_t px = (size_t)d->w * d->h;
    for (int e = 0; e < n; ++e) {
        memcpy(rgb[e], d->h_rgb + (size_t)e * px * 3, px * 3);
        memcpy(depth[e], d->h_depth + (size_t)e * px, px * sizeof(float));
    }
    return 0;
}

static void fdv_close(void *p)
{
    struct framedev *d = (struct framedev *)p;
    if (!d) return;
    render_free(d->r);
    if (d->h_rgb) cudaFreeHost(d->h_rgb);
    if (d->h_depth) cudaFreeHost(d->h_depth);
    close(d->slot_fd);
    free(d);
}

static void fdv_slow(void *p, const char *why)
{
    struct framedev *d = (struct framedev *)p;
    char sp[128], tmp[160];
    slow_path(sp, sizeof sp, d->w, d->h);
    snprintf(tmp, sizeof tmp, "%s.%d", sp, (int)getpid());
    FILE *f = fopen(tmp, "w");
    if (!f) return;
    fprintf(f, "%s\n", why);
    fclose(f);
    chmod(tmp, 0666);
    rename(tmp, sp);
}

static const struct framedev_api API = {FRAMEDEV_ABI, fdv_open, fdv_set, fdv_render, fdv_download, fdv_close, fdv_slow};

extern "C" __attribute__((visibility("default"))) const struct framedev_api *framedev_api(void) { return &API; }
