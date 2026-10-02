/* The device renderer's gate and benchmark over raster_obs dumps (play
 * --obs-dump: each shot's terrain passes and the C renderer's frame of them).
 *
 *   render_check [--batch N] [--arena MB] [--png DIR] FILE.obs...
 *     every file drawn again by the C renderer from the dump alone
 *     (raster_obs_render, against the live frame the file holds), then by
 *     the device, N files per render (default 32), each
 *     frame against the C frame byte for byte: one line per file
 *     (PASS, or FAIL with the pixels that differ and the first), then
 *     "render_check: P of N frames bit-exact ...". rc 0 when all pass.
 *   --one-trip: the renderers wait once a render (render_one_trip).
 *   --mesh-tab: the device mesher in its table mode (render_mesh_tab).
 *   --stored: the files' C redraw was checked when they were stored
 *     (gate.sh's store): the device against the live frame only.
 *   render_check --c-check FILE.obs...
 *     the C redraw alone: rc 0 when every file reproduces its live frame.
 *   --prec P: every file drawn at that precision (engine/raster_prec.h: exact,
 *     fast or fast:STAGE,...) by the device and by C; a file dumped at
 *     another is held to C's redraw at P (not to its live frame).
 *   render_check --prec-diff DIR FILE.obs...
 *     the precision's cost, C alone: every file (dumped exact) drawn by C
 *     at each fast stage alone (xform, edge, light, color, fog, shade, rcp) and
 *     at fast, against its exact frame: one line a mode (the frames that
 *     differ, the pixels that differ a frame, the largest, the mean and
 *     largest channel error), and the worst frame's pair as DIR/MODE.a.png
 *     (exact) and DIR/MODE.b.png for pxdiff (tools/pxdiff.c).
 *   render_check --c-only N FILE.obs...
 *     N frames of the C renderer alone on one thread (for perf stat).
 *   render_check --variants FILE.obs...
 *     the paths the recordings rarely reach, on both sides: every file again
 *     with the clouds on (both passes, at three heights), the stars and a
 *     sunrise forced, and each fog mode (linear, exp, exp2 at two
 *     densities), the device against raster_obs_render of the same variant.
 *   render_check --seq FILE.obs...
 *     the frames of each directory are one recording's, in order, dumped
 *     with the device mesher's feed (play --obs-mesh 2): each directory is
 *     one slot of the renderer, fed its frames in turn (frame k of every
 *     directory in one render), so its sections are meshed on the device
 *     from the feeds alone; every device mesh against the host's (byte for
 *     byte, every section pass the feeds asked for) and every frame, drawn
 *     with the device's meshes, against the C frame. rc 0 when all pass.
 *   render_check --bench B1,B2,... [--reps R] [--threads T] FILE.obs...
 *     the device over batches of B environments (the files in turn), the
 *     meshes resident after a first render (a steady frame uploads the
 *     frame's numbers and draw lists only), R renders each: device ms per
 *     render and per frame, frames per second, each stage's share; and the
 *     C renderer over the same frames on T host processes (one frame per
 *     process at a time: raster_entities.c is not thread safe): frames per
 *     second. */
#define _POSIX_C_SOURCE 200809L
#include "render.h"
#include "../../engine/raster_obs.h"
#include "../../engine/raster_sky2.h"
#include "../../engine/jmath.h"
#include "../../engine/raster.h"
#include "../../engine/render_blocks.h"
#include "../../engine/meshfeed.h"
#include "../../engine/raster_prec.h"

#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_ms(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec * 1e3 + t.tv_nsec / 1e6;
}

struct item { const char *path; struct raster_obs o; unsigned char *rgb; };

static int one_trip;   /* --one-trip: render_one_trip on every renderer */
static int mesh_tab;   /* --mesh-tab: render_mesh_tab on the --seq renderer */

static long diff(const unsigned char *a, const unsigned char *b, int w, int h, int *fx, int *fy)
{
    long n = 0;
    *fx = *fy = -1;
    for (int i = 0; i < w * h; ++i)
        if (memcmp(a + i * 3, b + i * 3, 3)) {
            if (!n) { *fx = i % w; *fy = i / w; }
            ++n;
        }
    return n;
}

/* the C renderer over BATCH frames on PROCS processes (raster_entities.c
 * keeps its rasterizer's state in file statics, so one frame at a time per
 * process, as an env pool's worker would): ms of wall time */
static double c_batch(struct item *items, int nitems, int batch, int procs)
{
    const struct raster_obs *o0 = &items[0].o;
    double t0 = now_ms();
    pid_t pid[256];
    if (procs > 256) procs = 256;
    int np = 0;
    for (int k = 0; k < procs; ++k) {
        pid_t p = fork();
        if (p == 0) {
            unsigned char *rgb = malloc((size_t)o0->w * o0->h * 3);
            float *depth = malloc((size_t)o0->w * o0->h * sizeof(float));
            for (int i = k; i < batch; i += procs) raster_obs_render(&items[i % nitems].o, rgb, depth);
            _exit(0);
        }
        if (p > 0) pid[np++] = p;
    }
    for (int k = 0; k < np; ++k) waitpid(pid[k], NULL, 0);
    return now_ms() - t0;
}

/* what the frames exercise: dimensions, fog modes, the sky's passes */
static void coverage(const struct item *items, int n)
{
    int dim[4] = {0}, fog[3] = {0}, stars = 0, rise = 0, clouds = 0, water = 0, ents = 0, cracks = 0, clears = 0, portals = 0;
    long quads = 0;
    for (int i = 0; i < n; ++i) {
        const struct raster_obs *o = &items[i].o;
        int d = o->sky.dimension;
        ++dim[d == -1 ? 0 : d == 0 ? 1 : d == 1 ? 2 : 3];
        ++fog[o->fogm == 9729 ? 0 : o->fogm == 2048 ? 1 : 2];
        stars += o->sky.dimension == 0 && o->sky.stars;
        rise += o->sky.dimension == 0 && o->sky.sunrise[3] > 0.0f;
        clouds += o->sky.dimension == 0 && o->sky.clouds;
        water += o->nwater > 0;
        ents += o->nequad > 0;
        quads += o->nequad;
        cracks += o->ncrack > 0;
        portals += o->nportal > 0;
        for (int c = 0; c < o->ncmd; ++c) if (o->cmd[c].kind == RASTER_CMD_CLEAR_DEPTH) { ++clears; break; }
    }
    printf("render_check: %d frames: nether %d, overworld %d, end %d, other %d; fog linear %d, exp %d, exp2 %d; "
           "stars %d, sunrise %d, clouds %d, translucent sections %d; entity passes %d (%ld quads), cracks %d, "
           "end portals %d, overlay passes %d\n",
           n, dim[0], dim[1], dim[2], dim[3], fog[0], fog[1], fog[2], stars, rise, clouds, water, ents, quads, cracks, portals, clears);
}

/* --variants: each frame with the sky's and the fog's other paths forced */
static int run_variants(struct item *items, int n, int arena)
{
    enum { NV = 8 };
    int w = items[0].o.w, h = items[0].o.h;
    struct raster_obs *v = malloc(sizeof *v * (size_t)n * NV);
    unsigned char *ref = malloc((size_t)w * h * 3 * (size_t)n * NV), *mine = malloc((size_t)w * h * 3);
    float *depth = malloc((size_t)w * h * sizeof(float));
    const char *what[NV] = {"clouds low", "clouds mid", "clouds high", "stars and sunrise",
                            "fog exp 0.1", "fog exp 2.0", "fog exp2 0.05", "half a day on"};
    for (int i = 0; i < n; ++i)
        for (int k = 0; k < NV; ++k) {
            struct raster_obs *o = &v[i * NV + k];
            *o = items[i].o;
            o->owned = NULL;
            struct raster_obs_sky *s = &o->sky;
            if (k < 3) {
                /* the clouds' bottom above, level with and below the camera */
                s->dimension = 0;
                s->clouds = 1;
                s->cloud_height = k == 0 ? 20.33f : k == 1 ? 2.33f : -12.67f;
                s->cloud_color[0] = 0.9f; s->cloud_color[1] = 0.85f; s->cloud_color[2] = 0.8f;
            }
            else if (k == 3 || k == 7) {
                s->dimension = 0;
                s->stars = 1;
                s->star = 0.75f;
                s->sunrise[0] = 0.9f; s->sunrise[1] = 0.45f; s->sunrise[2] = 0.2f; s->sunrise[3] = 0.6f;
                if (k == 7) {
                    /* half a day on: the sun, the moon, the stars and the fan
                     * on the other side, each derived as raster.c derives it */
                    s->celestial_angle += s->celestial_angle < 0.5f ? 0.5f : -0.5f;
                    s->rise_dir = mh_sin(s->celestial_angle * 6.2831855f) < 0.0f ? 1.0f : -1.0f;
                    raster_sky2_rotation(s->celestial_angle, &s->cel_cs, &s->cel_sn);
                }
                for (int c = 0; c < 4; ++c) s->rise_center[c] = (float)(int)(s->sunrise[c] * 255.0f) / 255.0f;
            }
            else {
                o->fogm = k == 6 ? 2049 : 2048;
                o->fogd = k == 4 ? 0.1f : k == 5 ? 2.0f : 0.05f;
            }
            raster_obs_render(o, ref + (size_t)(i * NV + k) * w * h * 3, depth);
        }
    int total = n * NV, pass = 0;
    int batch = total < 64 ? total : 64;
    struct render *R = render_new(batch, w, h, arena);
    if (R && one_trip) render_one_trip(R, 1);
    if (!R) return 1;
    for (int i0 = 0; i0 < total; i0 += batch) {
        int b = total - i0 < batch ? total - i0 : batch;
        int refused = 0;
        for (int e = 0; e < b; ++e) {
            render_forget(R, e);
            if (render_set(R, e, &v[i0 + e])) {
                printf("FAIL render_check %s (%s): the device renderer refuses it\n", items[(i0 + e) / NV].path, what[(i0 + e) % NV]);
                refused = 1;
            }
        }
        if (refused) continue;
        render_render(R, b);
        for (int e = 0; e < b; ++e) {
            int j = i0 + e, fx, fy;
            render_download(R, e, mine);
            long d = diff(mine, ref + (size_t)j * w * h * 3, w, h, &fx, &fy);
            if (d) printf("FAIL render_check %s (%s): %ld px differ, first %d,%d\n", items[j / NV].path, what[j % NV], d, fx, fy);
            else ++pass;
        }
    }
    printf("render_check: %d of %d variant frames bit-exact at %dx%d (clouds at three heights, stars and sunrise, "
           "exp and exp2 fog, over %d frames)\n", pass, total, w, h, n);
    render_free(R);
    free(v); free(ref); free(mine); free(depth);
    return pass == total ? 0 : 1;
}

/* the mesher the dumps were meshed with, from their scene (one scene) */
static struct rb_table seq_tab;
static struct rb_atlas seq_atlas;
static struct rb_mesher seq_mesher;
static struct rb_tess seq_tess;

/* the order play drew the frames in: by tick (play's dump names,
 * o_TTTTTT.pNNN.obs), then by the mesh feed's sequence (the shots of one
 * tick come in the shots file's order) */
static long frame_tick(const char *path)
{
    const char *b = strrchr(path, '/');
    b = b ? b + 1 : path;
    long t = 0;
    if (sscanf(b, "o_%ld", &t) != 1) return 0;
    return t;
}

static int item_order(const void *a, const void *b)
{
    const struct item *x = a, *y = b;
    long u = frame_tick(x->path), v = frame_tick(y->path);
    if (u != v) return u < v ? -1 : 1;
    uint32_t p = x->o.mesh ? x->o.mesh->seq : 0, q = y->o.mesh ? y->o.mesh->seq : 0;
    return p < q ? -1 : p > q;
}

static int run_seq(struct item *items, int n, int arena, const char *png_dir, int verbose)
{
    int w = items[0].o.w, h = items[0].o.h;
    /* each directory's frames in the order they were drawn */
    qsort(items, (size_t)n, sizeof *items, item_order);
    /* the directories, in order of first appearance */
    int *dir = calloc((size_t)n, sizeof *dir), ndir = 0;
    const char **names = calloc((size_t)n, sizeof *names);
    int *len = calloc((size_t)n, sizeof *len);
    for (int i = 0; i < n; ++i) {
        const char *sl = strrchr(items[i].path, '/');
        int l = sl ? (int)(sl - items[i].path) : 0, d = 0;
        while (d < ndir && !(len[d] == l && !strncmp(names[d], items[i].path, (size_t)l))) ++d;
        if (d == ndir) { names[d] = items[i].path; len[d] = l; ++ndir; }
        dir[i] = d;
        if (!items[i].o.mesh) { printf("FAIL render_check %s: no mesh feed (play --obs-mesh 2)\n", items[i].path); return 1; }
    }
    const char *assets = items[0].o.assets;
    if (!assets || rb_table_load(&seq_tab, assets) || rb_atlas_load(&seq_atlas, assets) ||
        rb_tess_init(&seq_tess, 1024) || rb_mesher_init(&seq_mesher, &seq_tab, &seq_atlas, NULL, &seq_tess, 0, 0, 0)) {
        fprintf(stderr, "render_check: cannot load the mesher's tables from %s\n", assets ? assets : "(no scene)");
        return 2;
    }
    for (int i = 0; i < n; ++i) {
        if (!items[i].o.assets || strcmp(items[i].o.assets, assets)) {
            fprintf(stderr, "render_check: %s was meshed with another scene\n", items[i].path);
            return 2;
        }
        items[i].o.mesher = &seq_mesher;
    }
    /* the device is shared: its memory comes and goes, so a renderer that
     * does not fit is tried again for a while */
    struct render *R = NULL;
    unsigned char *dout = NULL;
    for (int tries = 0; tries < 120 && !(R && dout); ++tries) {
        if (R) { render_free(R); R = NULL; }
        R = render_new(ndir, w, h, arena);
        dout = R ? render_dev_alloc((size_t)ndir * w * h * 3) : NULL;
        if (!(R && dout)) nanosleep(&(struct timespec){0, 500000000}, NULL);
    }
    if (!R || !dout) { fprintf(stderr, "render_check: no device renderer\n"); return 1; }
    render_mesh_tab(R, mesh_tab);
    int *next = calloc((size_t)ndir, sizeof *next), *slots = calloc((size_t)ndir, sizeof *slots);
    int *which = calloc((size_t)ndir, sizeof *which);
    unsigned char **outs = calloc((size_t)ndir, sizeof *outs);
    unsigned char *mine = malloc((size_t)w * h * 3);
    int pass = 0, renders = 0, failed = 0;
    size_t checked = 0, diffs = 0, requests = 0, bytes = 0;
    double ms = 0, cms = 0, wms = 0;
    for (;;) {
        int b = 0;
        for (int d = 0; d < ndir; ++d) {
            int i = next[d];
            while (i < n && dir[i] != d) ++i;
            if (i >= n) continue;
            next[d] = i + 1;
            if (render_set(R, d, &items[i].o)) { printf("FAIL render_check %s: the device renderer refuses it\n", items[i].path); return 1; }
            slots[b] = d;
            which[b] = i;
            outs[b] = dout + (size_t)d * w * h * 3;
            ++b;
        }
        if (!b) break;
        if (render_render_list(R, b, slots, outs)) { printf("FAIL render_check: the render failed\n"); return 1; }
        ++renders;
        const struct render_stats *s = render_stats(R);
        checked += s->dmesh_checked; diffs += s->dmesh_diffs; requests += s->dmesh_requests;
        bytes += s->dmesh_upload_bytes; ms += s->total_ms;
        cms += s->dmesh_count_ms; wms += s->dmesh_write_ms;
        if (verbose)
            printf("render %d: %d frames, %zu section passes meshed, count %.1f ms, write %.1f ms, render %.1f ms in all\n",
                   renders, b, s->dmesh_requests, s->dmesh_count_ms, s->dmesh_write_ms, s->total_ms);
        for (int k = 0; k < b; ++k) {
            const struct item *it = &items[which[k]];
            if (render_failed(R, k)) { printf("FAIL render_check %s: the device lost its meshes\n", it->path); ++failed; continue; }
            render_dev_download(mine, outs[k], (size_t)w * h * 3);
            int fx, fy;
            long d = diff(mine, it->rgb, w, h, &fx, &fy);
            if (d) {
                printf("FAIL render_check %s: %ld of %d px differ (device meshes), first %d,%d\n", it->path, d, w * h, fx, fy);
                if (png_dir) {
                    char p[1200];
                    const char *bn = strrchr(it->path, '/');
                    snprintf(p, sizeof p, "%s/%d_%s.device.png", png_dir, which[k], bn ? bn + 1 : it->path);
                    raster_png(p, w, h, mine);
                }
            }
            else ++pass;
        }
    }
    printf("render_check: %zu of %zu device meshes equal to the host's (%zu section passes meshed on the device, "
           "%.1f MB of feeds), %d of %d frames bit-exact with the device's meshes over %d recordings, %d renders, "
           "%.1f ms of device time (meshing: %.1f ms counting, %.1f ms writing)\n",
           checked - diffs, checked, requests, bytes / 1e6, pass, n, ndir, renders, ms, cms, wms);
    render_dev_free(dout);
    render_free(R);
    return pass == n && !diffs && !failed && checked == requests ? 0 : 1;
}

/* --prec-diff: C at each fast stage alone and at fast against the exact
 * frames the files hold (one file in memory at a time) */
static int run_prec_diff(char **paths, int n, const char *dir)
{
    enum { NM = 8 };
    static const char *names[NM] = {"xform", "edge", "light", "color", "fog", "shade", "rcp", "fast"};
    int w = 0, h = 0, nd[NM] = {0}, wi[NM];
    long px[NM] = {0}, pmax[NM] = {0}, err[NM] = {0}, emax[NM] = {0};
    unsigned char *mine = NULL, *wa[NM] = {0}, *wb[NM] = {0};
    float *depth = NULL;
    for (int m = 0; m < NM; ++m) wi[m] = -1;
    for (int i = 0; i < n; ++i) {
        struct raster_obs o;
        unsigned char *rgb = NULL;
        if (raster_obs_read(paths[i], &o, &rgb) || !rgb) { fprintf(stderr, "render_check: cannot read %s\n", paths[i]); return 2; }
        if (o.prec != RP_EXACT) { fprintf(stderr, "render_check: %s was not dumped exact\n", paths[i]); return 2; }
        if (!mine) {
            w = o.w; h = o.h;
            mine = malloc((size_t)w * h * 3);
            depth = malloc((size_t)w * h * sizeof(float));
            for (int m = 0; m < NM; ++m) { wa[m] = malloc((size_t)w * h * 3); wb[m] = malloc((size_t)w * h * 3); }
        } else if (o.w != w || o.h != h) { fprintf(stderr, "render_check: %s is not %dx%d\n", paths[i], w, h); return 2; }
        for (int m = 0; m < NM; ++m) {
            o.prec = m < NM - 1 ? 1 << m : RP_FAST;
            raster_obs_render(&o, mine, depth);
            long d = 0;
            for (int k = 0; k < w * h; ++k) {
                int dk = 0;
                for (int c = 0; c < 3; ++c) {
                    int e = abs((int)mine[k * 3 + c] - rgb[k * 3 + c]);
                    err[m] += e;
                    if (e > emax[m]) emax[m] = e;
                    dk |= e;
                }
                d += dk != 0;
            }
            px[m] += d;
            nd[m] += d > 0;
            if (d > pmax[m] || wi[m] < 0) {
                pmax[m] = d; wi[m] = i;
                memcpy(wa[m], rgb, (size_t)w * h * 3);
                memcpy(wb[m], mine, (size_t)w * h * 3);
            }
        }
        raster_obs_free(&o);
        free(rgb);
    }
    for (int m = 0; m < NM; ++m) {
        printf("prec_diff %s: %d of %d frames differ, %.2f px a frame (%.3f%%), most %ld (%s), channel error mean %.5f max %ld\n",
               names[m], nd[m], n, (double)px[m] / n, 100.0 * px[m] / ((double)n * w * h), pmax[m], paths[wi[m]],
               (double)err[m] / ((double)n * w * h * 3), emax[m]);
        if (dir) {
            char p[1200];
            snprintf(p, sizeof p, "%s/%s.a.png", dir, names[m]);
            raster_png(p, w, h, wa[m]);
            snprintf(p, sizeof p, "%s/%s.b.png", dir, names[m]);
            raster_png(p, w, h, wb[m]);
        }
        free(wa[m]); free(wb[m]);
    }
    free(mine); free(depth);
    return 0;
}

int main(int argc, char **argv)
{
    int batch = 0, arena = 0, reps = 5, threads = 16, variants = 0, c_only = 0, seq = 0, verbose = 0, stored = 0, c_check = 0;
    const char *png_dir = NULL;   /* --png DIR: each failing frame's live, C-from-dump and device PNGs */
    const char *prec_diff = NULL;
    int prec = -1;                /* --prec: every file drawn at it */
    const char *bench = NULL;
    struct item *items = calloc((size_t)argc, sizeof *items);
    int n = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--batch") && i + 1 < argc) batch = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--arena") && i + 1 < argc) arena = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--bench") && i + 1 < argc) bench = argv[++i];
        else if (!strcmp(argv[i], "--reps") && i + 1 < argc) reps = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--threads") && i + 1 < argc) threads = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--variants")) variants = 1;
        else if (!strcmp(argv[i], "--seq")) seq = 1;
        else if (!strcmp(argv[i], "--verbose")) verbose = 1;
        else if (!strcmp(argv[i], "--c-only") && i + 1 < argc) c_only = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--png") && i + 1 < argc) png_dir = argv[++i];
        else if (!strcmp(argv[i], "--one-trip")) one_trip = 1;
        else if (!strcmp(argv[i], "--mesh-tab")) mesh_tab = 1;
        else if (!strcmp(argv[i], "--stored")) stored = 1;
        else if (!strcmp(argv[i], "--c-check")) c_check = 1;
        else if (!strcmp(argv[i], "--prec-diff") && i + 1 < argc) prec_diff = argv[++i];
        else if (!strcmp(argv[i], "--prec") && i + 1 < argc) {
            if ((prec = rp_parse(argv[++i])) < 0) { fprintf(stderr, "render_check: --prec takes exact, fast or fast:STAGE,...\n"); return 2; }
        }
        else if (argv[i][0] == '-') { fprintf(stderr, "render_check: unknown flag %s\n", argv[i]); return 2; }
        else items[n++].path = argv[i];
    }
    if (!n) { fprintf(stderr, "usage: render_check [--batch N] [--bench B,B..] FILE.obs...\n"); return 2; }
    if (prec_diff) {
        char **paths = malloc((size_t)n * sizeof *paths);
        for (int i = 0; i < n; ++i) paths[i] = (char *)items[i].path;
        return run_prec_diff(paths, n, prec_diff);
    }
    for (int i = 0; i < n; ++i)
        if (raster_obs_read(items[i].path, &items[i].o, &items[i].rgb) || !items[i].rgb) {
            fprintf(stderr, "render_check: cannot read %s\n", items[i].path);
            return 2;
        }
    int w = items[0].o.w, h = items[0].o.h;
    for (int i = 1; i < n; ++i)
        if (items[i].o.w != w || items[i].o.h != h) { fprintf(stderr, "render_check: %s is not %dx%d\n", items[i].path, w, h); return 2; }
    /* --prec: a file dumped at another precision is held to C's redraw at P */
    int redrawn = 0;
    if (prec >= 0) {
        float *dp = malloc((size_t)w * h * sizeof(float));
        for (int i = 0; i < n; ++i) {
            if (items[i].o.prec == prec) continue;
            items[i].o.prec = prec;
            raster_obs_render(&items[i].o, items[i].rgb, dp);
            ++redrawn;
        }
        free(dp);
        if (redrawn) stored = 1;   /* nothing live to check C's redraw against */
    }

    if (c_only > 0) {
        /* --c-only N: N frames of the C renderer on this thread and nothing
         * else (perf stat -e instructions:u counts a frame's cost) */
        unsigned char *rgb = malloc((size_t)w * h * 3);
        float *depth = malloc((size_t)w * h * sizeof(float));
        double t0 = now_ms();
        for (int i = 0; i < c_only; ++i) raster_obs_render(&items[i % n].o, rgb, depth);
        printf("C %dx%d: %d frames, %.3f ms a frame on one thread\n", w, h, c_only, (now_ms() - t0) / c_only);
        return 0;
    }
    coverage(items, n);
    if (bench) {
        int maxb = 0;
        for (const char *p = bench; *p;) { int b = atoi(p); if (b > maxb) maxb = b; p = strchr(p, ','); if (!p) break; ++p; }
        struct render *R = render_new(maxb, w, h, arena);
        if (R && one_trip) render_one_trip(R, 1);
        if (!R) { fprintf(stderr, "render_check: no device renderer\n"); return 1; }
        size_t tris = 0;
        for (const char *p = bench; *p;) {
            int b = atoi(p);
            for (int e = 0; e < b; ++e) { render_forget(R, e); render_set(R, e, &items[e % n].o); }
            render_render(R, b);
            double first = render_stats(R)->total_ms;
            size_t first_up = render_stats(R)->upload_bytes;
            /* the fastest render of R (the device is shared: the others'
             * kernels only add), its stages, and the median */
            double best = 1e30, up = 0, un = 0, bi = 0, ra = 0, wall = 0, all[64];
            size_t bytes = 0;
            if (reps > 64) reps = 64;
            for (int k = 0; k < reps; ++k) {
                for (int e = 0; e < b; ++e) render_set(R, e, &items[e % n].o);
                double t0 = now_ms();
                render_render(R, b);
                double wk = now_ms() - t0;
                const struct render_stats *s = render_stats(R);
                all[k] = s->total_ms;
                if (s->total_ms < best) {
                    best = s->total_ms; up = s->upload_ms; un = s->units_ms; bi = s->bin_ms; ra = s->raster_ms; wall = wk;
                }
                bytes = s->upload_bytes;
                tris = s->triangles;
            }
            for (int a = 0; a < reps; ++a) for (int c = a + 1; c < reps; ++c) if (all[c] < all[a]) { double t = all[a]; all[a] = all[c]; all[c] = t; }
            double tot = best;
            printf("device %dx%d batch %d: %.3f ms a render at best of %d (median %.3f; %.4f ms a frame, %.0f frames/s; wall %.3f ms), "
                   "upload %.3f, units %.3f, bins %.3f, raster %.3f ms; %zu triangles, %zu bytes up a steady render "
                   "(first render %.1f ms, %zu bytes); units %zu, sky %zu, clipped %zu\n",
                   w, h, b, tot, reps, all[reps / 2], tot / b, 1000.0 * b / tot, wall, up, un, bi, ra, tris, bytes, first, first_up,
                   render_stats(R)->units, render_stats(R)->sky_units, render_stats(R)->clip_units);
            double c1 = c_batch(items, n, b < 4 ? b : 4, 1) / (b < 4 ? b : 4);
            double ct = c_batch(items, n, b * 2 > threads ? b * 2 : threads, threads);
            int cf = b * 2 > threads ? b * 2 : threads;
            printf("C %dx%d: %.3f ms a frame on one process (%.0f frames/s); %.0f frames/s on %d processes\n",
                   w, h, c1, 1000.0 / c1, 1000.0 * cf / ct, threads);
            fflush(stdout);
            p = strchr(p, ',');
            if (!p) break;
            ++p;
        }
        render_free(R);
        return 0;
    }

    if (variants) return run_variants(items, n, arena);
    if (seq) return run_seq(items, n, arena, png_dir, verbose);

    /* the dump alone reproduces the live C frame */
    unsigned char *mine = malloc((size_t)w * h * 3);
    float *depth = malloc((size_t)w * h * sizeof(float));
    int cfail = 0;
    for (int i = 0; i < n && !stored; ++i) {
        raster_obs_render(&items[i].o, mine, depth);
        int fx, fy;
        long d = diff(mine, items[i].rgb, w, h, &fx, &fy);
        if (d) {
            printf("FAIL render_check C %s: %ld px differ from the live frame, first %d,%d\n", items[i].path, d, fx, fy);
            ++cfail;
            if (png_dir) {
                char p[1200];
                const char *b = strrchr(items[i].path, '/');
                snprintf(p, sizeof p, "%s/%d_%s.live.png", png_dir, i, b ? b + 1 : items[i].path);
                raster_png(p, w, h, items[i].rgb);
                snprintf(p, sizeof p, "%s/%d_%s.c.png", png_dir, i, b ? b + 1 : items[i].path);
                raster_png(p, w, h, mine);
            }
        }
    }
    if (c_check) {
        printf("render_check: C from the dumps: %d of %d exact\n", n - cfail, n);
        return cfail ? 1 : 0;
    }
    /* the device is shared: 32 frames a render unless asked */
    if (batch <= 0) batch = 32;
    if (batch > n) batch = n;
    struct render *R = render_new(batch, w, h, arena);
    if (R && one_trip) render_one_trip(R, 1);
    if (!R) { fprintf(stderr, "render_check: no device renderer\n"); return 1; }
    int pass = 0, groups = 0;
    size_t tris = 0;
    double ms = 0;
    for (int i0 = 0; i0 < n; i0 += batch) {
        int b = n - i0 < batch ? n - i0 : batch;
        for (int e = 0; e < b; ++e) {
            render_forget(R, e);
            if (render_set(R, e, &items[i0 + e].o)) { printf("FAIL render_check %s: the device renderer refuses it\n", items[i0 + e].path); return 1; }
        }
        render_render(R, b);
        const struct render_stats *s = render_stats(R);
        tris += s->triangles; ms += s->total_ms; groups += s->groups;
        for (int e = 0; e < b; ++e) {
            render_download(R, e, mine);
            int fx, fy;
            long d = diff(mine, items[i0 + e].rgb, w, h, &fx, &fy);
            if (d) {
                const unsigned char *a = mine + ((size_t)fy * w + fx) * 3, *c = items[i0 + e].rgb + ((size_t)fy * w + fx) * 3;
                printf("FAIL render_check %s: %ld of %d px differ, first %d,%d device %d %d %d C %d %d %d\n", items[i0 + e].path,
                       d, w * h, fx, fy, a[0], a[1], a[2], c[0], c[1], c[2]);
                if (png_dir) {
                    char p[1200];
                    const char *bn = strrchr(items[i0 + e].path, '/');
                    snprintf(p, sizeof p, "%s/%d_%s.device.png", png_dir, i0 + e, bn ? bn + 1 : items[i0 + e].path);
                    raster_png(p, w, h, mine);
                }
            }
            else { printf("PASS render_check %s\n", items[i0 + e].path); ++pass; }
        }
    }
    printf("render_check: %d of %d frames bit-exact at %dx%d, %d per render (%d renders, %d arena groups), "
           "%zu triangles, %.1f ms of device time; C from the dumps: %d of %d exact%s\n",
           pass, n, w, h, batch, (n + batch - 1) / batch, groups, tris, ms, n - cfail, n,
           redrawn ? " (redrawn at --prec)" : stored ? " (checked when stored)" : "");
    render_free(R);
    for (int i = 0; i < n; ++i) { raster_obs_free(&items[i].o); free(items[i].rgb); }
    free(items); free(mine); free(depth);
    return pass == n && !cfail ? 0 : 1;
}
