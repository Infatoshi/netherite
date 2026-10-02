/* The device renderer (render.h): raster.c's live terrain passes over many
 * environments. Every function below that computes a float is a port of the
 * raster.c or raster_sky2.c function it names, operation for operation, so
 * the build's --fmad=false, -prec-div=true and -prec-sqrt=true give the C
 * engine's bits (csrc/cuda/render/mirrors.txt lists them for the mirror
 * rule: a change to one of those C functions needs its port here). */
#include "dev.cuh"
#include <cuda.h>
#include <nvtx3/nvToolsExt.h>

#include <algorithm>
#include <array>
#include <condition_variable>
#include <mutex>
#include <pthread.h>
#include <map>
#include <thread>
#include <unordered_map>
#include <vector>

/* ------------------------------------------------------------ host */

/* a section's mesh in the arena: where, its vertices, its version, the
 * block it holds (int32s) and the slot's frame it was last drawn in */
struct mesh_slot { int64_t off; int count; uint32_t version; int64_t cap; uint64_t last; };
/* a section pass the device meshed (raster_obs_draw device): by the
 * section's place (a window's rebase moves the host's keys, not these) */
struct dmesh_slot { int64_t off, cap; int count, flags; uint32_t version; int cx, s, cz; };
static uint64_t dmesh_key(int cx, int s, int cz, int pass)
{
    return (uint64_t)((uint32_t)cx & 0xffffffu) << 40 | (uint64_t)((uint32_t)cz & 0xffffffu) << 16 | (uint64_t)(s & 255) << 1 | (uint64_t)(pass & 1);
}
struct tex_slot { unsigned long long off; int w, h; };
/* the device mesher's work begun (mesh_begin) and not yet ended: the slots
 * with a feed and each feed's first request in the launch (-1: not applied) */
struct mesh_job { std::vector<int> slots, first; int active; };

struct render {
    int max_envs, w, h, tiles_x, tiles_y, ntiles;
    int pm;                             /* the render's frames' precision (RASTER_PM_*: raster's) */
    size_t arena_bytes;
    /* the frames set for the next render */
    std::vector<const raster_obs *> obs;
    /* the sky's sheets (the first frame's) */
    int have_sheets;
    std::vector<unsigned char> sun, moon, clouds, end_sky;
    std::vector<float> stars;
    int nstars;
    /* each env's sheets last found equal, by address: a view's sheets are
     * its assets, loaded once and never written, so the same addresses
     * (and star count) are the same bytes until the env is forgotten */
    struct sheets_seen { const void *p[5]; int nstars; };
    std::vector<sheets_seen> seen;
    unsigned char *d_sun, *d_moon, *d_clouds, *d_end_sky;
    float *d_stars;
    /* atlases: one per environment, and the host's copy of what each holds */
    int aw, ah;
    size_t atlas_bytes;
    unsigned char *d_atlas;
    std::vector<std::vector<unsigned char>> atlas_shadow;
    /* meshes */
    int32_t *d_mesh;
    int64_t mesh_cap, mesh_used;              /* int32s: the arena, its bump pointer */
    /* the arena's reserved addresses and the memory mapped at their start,
     * a handle a step (mesh_grow) */
    CUdeviceptr mesh_va;
    size_t mesh_va_len, mesh_mapped;
    std::vector<CUmemGenericAllocationHandle> mesh_h;
    std::multimap<int64_t, int64_t> mesh_free; /* freed blocks by size (int32s): offset */
    std::vector<std::unordered_map<uint64_t, mesh_slot>> mesh_of;
    std::vector<uint64_t> slot_frame;         /* each slot's frames drawn */
    /* the device mesher (cuda/meshing), made at the first frame with a feed,
     * its meshes per slot, the frames of the last render it could not draw
     * (their views resync), the translucent sections it orders */
    struct meshing *mesher;
    std::vector<std::unordered_map<uint64_t, dmesh_slot>> dmesh_of;
    std::vector<char> failed;
    /* the mesher's own stream, its work in flight, the event after its last
     * write pass (each render's stream waits for it), each slot's frame
     * meshed ahead of its render (and failed there), and the mesher's
     * statistics since the last render (added to its stats) */
    cudaStream_t mst;
    int prio_low, prio_high;
    /* the mesher's work may run on another thread than the renders
     * (render_mesh_start): mu guards the mesh arena (its blocks, its
     * growth), the device meshes' maps, the slot marks and mstat; drawing
     * is set while a render runs (on the thread drawer), and the arena
     * grows or starts over, for another thread, only when none does */
    std::mutex mu;
    std::condition_variable cv;
    int drawing;
    pthread_t drawer;
    mesh_job mj;
    cudaEvent_t ev_mesh;
    std::vector<char> slot_meshed, slot_fail;
    std::vector<std::array<int32_t, 4>> dmesh_square;   /* each slot's view square at its last drop scan */
    struct render_stats mstat;
    std::vector<wjob> wjobs;
    wjob *d_wjobs;
    float *d_wdist;
    size_t wdist_cap;
    /* the recorded passes' textures, by content */
    unsigned char *d_pool;
    unsigned long long pool_cap, pool_used;
    std::unordered_map<uint64_t, tex_slot> pool_of;
    /* per render (the uploaded arrays point into d_up) */
    envp *d_env;
    ddraw *d_draws;
    int32_t *d_orders;
    uint32_t *d_counts, *d_offsets, *d_nclip;
    xunit *d_clip_list, *d_xlist, *d_elist;
    size_t units_cap;
    cquad *d_equad;
    cextra *d_eextra;
    /* the entity quads' extra records, each environment's, and each
     * environment's first quad (kept for their capacity) */
    std::vector<cextra> hEX;
    std::vector<std::vector<cextra>> hexe;
    std::vector<uint32_t> hqbase;
    /* the model boxes (raster_obs ebox): each environment's first box and
     * matrix, each box's extra records' first (-1 none); with boxes the
     * quads the host sends are packed (their places in hqdst) and the
     * render's quads are made on the device in d_equad_res */
    std::vector<uint32_t> hbbase, hibase, hxbase;
    std::vector<int> hbex, hiex;
    cquad *d_equad_res;
    size_t equad_res_cap;
    raster_obs_estate *d_estate;
    raster_obs_crack *d_crack;
    raster_obs_portal *d_portal;
    raster_obs_line *d_line;
    float *d_depth;
    uint32_t (*d_lm)[256];
    dtex *d_dtex;
    tri *d_tris;
    uint32_t *d_tiles, *d_pair_off, *d_tri_env, *d_env_tri0;
    size_t tris_cap;
    uint32_t *d_keys, *d_vals, *d_keys2, *d_vals2;
    size_t pairs_cap;
    uint32_t *d_key_start, *d_key_n;            /* each key's run of the sorted pairs: start, end */
    uint32_t *d_order;                          /* the keys, heaviest first (tile_order) */
    size_t keys_cap;
    void *d_temp;
    size_t temp_cap;
    unsigned char *d_out;
    uint32_t *d_patch_idx;
    unsigned char *d_patch;
    uint32_t patch_n;           /* atlas tiles to patch after the flush */
    unsigned char *h_stage;
    size_t stage_cap, stage_at;
    /* the render's uploads in one copy: every region staged before the
     * flush goes to d_up at its own offset (the per-render arrays point
     * there), and the resident meshes and textures are scattered from it to
     * their arenas on the device (scat: the jobs, in words) */
    unsigned char *d_up;
    size_t up_cap;
    struct scat { unsigned long long dst; unsigned long long src; uint32_t words, pad; };
    std::vector<scat> scats;
    /* the per-render host lists, kept for their capacity */
    std::vector<envp> P;
    std::vector<ddraw> D;
    std::vector<int32_t> orders;
    std::vector<uint32_t> draw_base;
    std::vector<dtex> DT;
    std::vector<xunit> X, XE;
    std::vector<uint32_t> patch_idx;
    /* each group's four timing events (read after the render's last wait) */
    std::vector<cudaEvent_t> gev;
    uint32_t *d_gidx, *d_gout, *h_rb;           /* the gathers' indices and results, their copy back (pinned) */
    /* host memory (pinned) the render copies its frames and depth buffers
     * to, when set (cuda/render/framedev.cu) */
    unsigned char *h_frames;
    float *h_depths;
    /* one_trip: the steps past the count take their sizes on the device
     * and the render waits once (cuda/render: each wait is a turn of the
     * shared device's time slices); d_trip and h_trip hold its sizes */
    int one_trip;
    uint32_t *d_trip, *h_trip;
    /* the generic write's grid at most (gen_wgrid): 4 blocks an SM */
    uint32_t wgrid;
    /* the most triangles and pairs one frame of this renderer has made,
     * and their running means a frame: a one-trip render of N frames runs
     * its tail over what N such frames make (one_trip), not over the
     * arena; 0 until a render has made them */
    uint32_t trip_maxt, trip_maxp;
    double trip_meant, trip_meanp;
    /* no device mesher (render_no_mesher: the judges' plugin) */
    int no_mesher;
    /* the device mesher's table mode (render_mesh_tab) */
    int mesh_tab;
    cudaStream_t st;
    /* a render's independent kernels side by side (fork_aux, join_aux):
     * each is a small, latency-bound grid, so one after another they set
     * the render's length (lane/simclimb) */
    cudaStream_t aux[3];
    cudaEvent_t fev, jev[3];
    cudaEvent_t ev[6];
    struct render_stats stats;
    /* the render's frames: each one's resident slot and output */
    std::vector<size_t> bslot;
    std::vector<unsigned long long> bout;
};

template <class T> static void dgrow(T **p, size_t *cap, size_t need)
{
    if (need <= *cap) return;
    if (*p) CK(cudaFree(*p));
    size_t n = need + need / 2 + 64;
    CK(cudaMalloc((void **)p, n * sizeof(T)));
    *cap = n;
}

static void temp_need(render *r, size_t bytes)
{
    if (bytes <= r->temp_cap) return;
    if (r->d_temp) CK(cudaFree(r->d_temp));
    r->temp_cap = bytes + bytes / 2 + 4096;
    CK(cudaMalloc(&r->d_temp, r->temp_cap));
}

extern "C" struct render *render_new(int max_envs, int w, int h, int arena_mb)
{
    if (max_envs < 1 || w < 1 || h < 1) return NULL;
    render *r = new render();
    /* a device without the memory (it is shared): NULL, nothing kept */
#define CKN(x) do { if ((x) != cudaSuccess) { cudaGetLastError(); render_free(r); return NULL; } } while (0)
    r->max_envs = max_envs; r->w = w; r->h = h;
    {
        int dev = 0, sms = 82;
        if (cudaGetDevice(&dev) != cudaSuccess || cudaDeviceGetAttribute(&sms, cudaDevAttrMultiProcessorCount, dev) != cudaSuccess)
            cudaGetLastError();
        r->wgrid = 4 * (uint32_t)(sms > 0 ? sms : 82);
    }
    r->tiles_x = (w + TILE - 1) / TILE; r->tiles_y = (h + TILE - 1) / TILE;
    r->ntiles = r->tiles_x * r->tiles_y;
    r->arena_bytes = (size_t)(arena_mb > 0 ? arena_mb : 512) << 20;
    r->obs.assign((size_t)max_envs, NULL);
    r->mesh_of.resize((size_t)max_envs);
    r->dmesh_of.resize((size_t)max_envs);
    r->failed.assign((size_t)max_envs, 0);
    r->slot_meshed.assign((size_t)max_envs, 0);
    r->seen.assign((size_t)max_envs, render::sheets_seen{});
    r->slot_fail.assign((size_t)max_envs, 0);
    r->dmesh_square.assign((size_t)max_envs, std::array<int32_t, 4>{0, 0, -1, -1});
    r->slot_frame.assign((size_t)max_envs, 0);
    r->atlas_shadow.resize((size_t)max_envs);
    /* the draw's stream first, the mesher's last: a mesher block holds half
     * an SM's registers for milliseconds, and a pending draw kernel (small,
     * on the critical path of a render) takes the next free SM before the
     * mesher's next blocks do */
    CKN(cudaDeviceGetStreamPriorityRange(&r->prio_low, &r->prio_high));
    CKN(cudaStreamCreateWithPriority(&r->st, cudaStreamNonBlocking, r->prio_high));
    for (int i = 0; i < 3; ++i) {
        CKN(cudaStreamCreateWithPriority(&r->aux[i], cudaStreamNonBlocking, r->prio_high));
        CKN(cudaEventCreateWithFlags(&r->jev[i], cudaEventDisableTiming));
    }
    CKN(cudaEventCreateWithFlags(&r->fev, cudaEventDisableTiming));
    /* blocking: a wait on them sleeps (cudaEventSynchronize spins otherwise) */
    for (int i = 0; i < 6; ++i) CKN(cudaEventCreateWithFlags(&r->ev[i], cudaEventBlockingSync));
    CKN(cudaEventCreateWithFlags(&r->ev_mesh, cudaEventDisableTiming));
    CKN(cudaMalloc(&r->d_env_tri0, sizeof(uint32_t) * ((size_t)max_envs + 1)));
    CKN(cudaMalloc(&r->d_nclip, sizeof(uint32_t)));
    CKN(cudaMalloc(&r->d_out, (size_t)max_envs * w * h * 3));
    CKN(cudaMalloc(&r->d_depth, (size_t)max_envs * w * h * sizeof(float)));
    /* the triangle arena: triangles, their tile counts, offsets and env */
    size_t per_tri = sizeof(tri) + 3 * sizeof(uint32_t);
    r->tris_cap = r->arena_bytes / 2 / per_tri;
    CKN(cudaMalloc(&r->d_tris, r->tris_cap * sizeof(tri)));
    CKN(cudaMalloc(&r->d_tiles, (r->tris_cap + 1) * sizeof(uint32_t)));
    CKN(cudaMalloc(&r->d_pair_off, (r->tris_cap + 1) * sizeof(uint32_t)));
    CKN(cudaMalloc(&r->d_tri_env, r->tris_cap * sizeof(uint32_t)));
    /* the pairs, sorted into a second pair of buffers */
    r->pairs_cap = r->arena_bytes / 2 / (4 * sizeof(uint32_t));
    CKN(cudaMalloc(&r->d_keys, r->pairs_cap * sizeof(uint32_t)));
    CKN(cudaMalloc(&r->d_vals, r->pairs_cap * sizeof(uint32_t)));
    CKN(cudaMalloc(&r->d_keys2, r->pairs_cap * sizeof(uint32_t)));
    CKN(cudaMalloc(&r->d_vals2, r->pairs_cap * sizeof(uint32_t)));
    r->keys_cap = (size_t)max_envs * r->ntiles + 1;
    CKN(cudaMalloc(&r->d_key_start, r->keys_cap * sizeof(uint32_t)));
    CKN(cudaMalloc(&r->d_key_n, r->keys_cap * sizeof(uint32_t)));
    CKN(cudaMalloc(&r->d_order, r->keys_cap * sizeof(uint32_t)));
    CKN(cudaMalloc(&r->d_gidx, ((size_t)max_envs + 8) * sizeof(uint32_t)));
    CKN(cudaMalloc(&r->d_gout, ((size_t)max_envs + 8) * sizeof(uint32_t)));
    CKN(cudaMallocHost((void **)&r->h_rb, ((size_t)max_envs + 8) * sizeof(uint32_t)));
#undef CKN
    return r;
}

extern "C" void render_free(struct render *r)
{
    if (!r) return;
    void *p[] = {r->d_sun, r->d_moon, r->d_clouds, r->d_end_sky, r->d_stars, r->d_atlas, r->d_pool,
                 r->d_counts, r->d_offsets, r->d_nclip, r->d_clip_list,
                 r->d_tris, r->d_tiles, r->d_pair_off, r->d_tri_env, r->d_env_tri0, r->d_keys, r->d_vals, r->d_keys2,
                 r->d_vals2, r->d_key_start, r->d_key_n, r->d_order, r->d_temp, r->d_out, r->d_up,
                 r->d_gidx, r->d_gout, r->d_wdist, r->d_trip, r->d_depth, r->d_equad_res};
    /* the mesher's work in flight first */
    if (r->mst) cudaStreamSynchronize(r->mst);
    for (void *q : p) if (q) cudaFree(q);
    if (r->mesh_va) {
        if (r->mesh_mapped) cuMemUnmap(r->mesh_va, r->mesh_mapped);
        for (CUmemGenericAllocationHandle h : r->mesh_h) cuMemRelease(h);
        cuMemAddressFree(r->mesh_va, r->mesh_va_len);
    }
    for (cudaEvent_t ev : r->gev) cudaEventDestroy(ev);
    if (!r->no_mesher) meshing_free(r->mesher);
    if (r->h_stage) cudaFreeHost(r->h_stage);
    if (r->h_rb) cudaFreeHost(r->h_rb);
    if (r->h_trip) cudaFreeHost(r->h_trip);
    for (int i = 0; i < 6; ++i) if (r->ev[i]) cudaEventDestroy(r->ev[i]);
    if (r->ev_mesh) cudaEventDestroy(r->ev_mesh);
    if (r->st) cudaStreamDestroy(r->st);
    for (int i = 0; i < 3; ++i) {
        if (r->aux[i]) cudaStreamDestroy(r->aux[i]);
        if (r->jev[i]) cudaEventDestroy(r->jev[i]);
    }
    if (r->fev) cudaEventDestroy(r->fev);
    if (r->mst) cudaStreamDestroy(r->mst);
    delete r;
}

extern "C" void render_one_trip(struct render *r, int on) { r->one_trip = on; }
extern "C" void render_mesh_tab(struct render *r, int on)
{
    r->mesh_tab = on;
    if (r->mesher) meshing_set_tab(r->mesher, on);
}

extern "C" void render_frames_to(struct render *r, unsigned char *rgb, float *depth)
{
    r->h_frames = rgb;
    r->h_depths = depth;
}

extern "C" void render_no_mesher(struct render *r) { r->no_mesher = 1; }

/* CK_FAIL (dev.cuh): set once, before any renderer, by the one
 * program that catches (cuda/render/framedev.cu) */
static bool fail_throws;
void render_fail_throws(int on) { fail_throws = on != 0; }
void render_fail(void)
{
    if (fail_throws) throw render_error();
    exit(1);
}

/* The arena grown to hold TRIS triangles and PAIRS pairs (either 0: as it
 * is), a quarter over, when the device has the memory and 1 GB to spare
 * (it is shared): every group is a wait and a raster launch whose time is
 * its slowest tile's, so one group a render is several times faster. 1
 * when it holds them; 0 leaves it as it was (the render runs in groups). */
static int arena_fit(render *r, size_t tris, size_t pairs)
{
    size_t nt = tris > r->tris_cap ? tris + tris / 4 : 0, np = pairs > r->pairs_cap ? pairs + pairs / 4 : 0;
    if (!nt && !np) return 1;
    size_t per_tri = sizeof(tri) + 3 * sizeof(uint32_t), fr = 0, tot = 0;
    if (cudaMemGetInfo(&fr, &tot) != cudaSuccess) { cudaGetLastError(); return 0; }
    if (nt * per_tri + np * 4 * sizeof(uint32_t) + ((size_t)1 << 30) > fr) return 0;
    tri *t = NULL;
    uint32_t *a[8] = {};
    bool ok = true;
    if (nt) {
        ok = cudaMalloc(&t, nt * sizeof(tri)) == cudaSuccess;
        for (int i = 0; i < 3 && ok; ++i) ok = cudaMalloc(&a[i], (nt + 1) * sizeof(uint32_t)) == cudaSuccess;
    }
    if (np)
        for (int i = 4; i < 8 && ok; ++i) ok = cudaMalloc(&a[i], np * sizeof(uint32_t)) == cudaSuccess;
    if (!ok) {
        cudaGetLastError();
        if (t) cudaFree(t);
        for (uint32_t *q : a) if (q) cudaFree(q);
        return 0;
    }
    /* the stream's work on the old buffers first */
    CK(cudaStreamSynchronize(r->st));
    if (nt) {
        uint32_t **old[3] = {&r->d_tiles, &r->d_pair_off, &r->d_tri_env};
        CK(cudaFree(r->d_tris));
        r->d_tris = t;
        for (int i = 0; i < 3; ++i) { CK(cudaFree(*old[i])); *old[i] = a[i]; }
        r->tris_cap = nt;
    }
    if (np) {
        uint32_t **old[4] = {&r->d_keys, &r->d_vals, &r->d_keys2, &r->d_vals2};
        for (int i = 0; i < 4; ++i) { CK(cudaFree(*old[i])); *old[i] = a[4 + i]; }
        r->pairs_cap = np;
    }
    return 1;
}

extern "C" void render_reset_meshes(struct render *r)
{
    for (auto &m : r->mesh_of) m.clear();
    for (auto &m : r->dmesh_of) m.clear();
    r->mesh_free.clear();
    r->mesh_used = 0;
}

extern "C" void render_forget(struct render *r, int env)
{
    if (env < 0 || env >= r->max_envs) return;
    std::lock_guard<std::mutex> g(r->mu);
    for (auto &kv : r->mesh_of[(size_t)env])
        if (kv.second.cap) r->mesh_free.insert({kv.second.cap, kv.second.off});
    r->mesh_of[(size_t)env].clear();
    for (auto &kv : r->dmesh_of[(size_t)env])
        if (kv.second.cap) r->mesh_free.insert({kv.second.cap, kv.second.off});
    r->dmesh_of[(size_t)env].clear();
    if (r->mesher) meshing_forget(r->mesher, env);
    r->slot_meshed[(size_t)env] = r->slot_fail[(size_t)env] = 0;
    r->seen[(size_t)env] = render::sheets_seen{};
    r->dmesh_square[(size_t)env] = std::array<int32_t, 4>{0, 0, -1, -1};
    r->atlas_shadow[(size_t)env].clear();
}

extern "C" int render_failed(const struct render *r, int i)
{
    return i >= 0 && i < r->max_envs && r->failed[(size_t)i];
}

extern "C" const struct render_stats *render_stats(const struct render *r) { return &r->stats; }

extern "C" size_t render_device_bytes(const struct render *r)
{
    size_t per_tri = sizeof(tri) + 3 * sizeof(uint32_t);
    return r->tris_cap * per_tri + r->pairs_cap * 4 * sizeof(uint32_t) + (size_t)r->mesh_cap * sizeof(int32_t) +
           r->atlas_bytes * (size_t)r->max_envs + (size_t)r->pool_cap + (size_t)r->max_envs * r->w * r->h * (3 + sizeof(float)) + r->temp_cap +
           r->units_cap * (2 * sizeof(uint32_t) + sizeof(xunit)) + r->keys_cap * 3 * sizeof(uint32_t) + r->up_cap;
}

static int sheets_equal(const render *r, const raster_obs *o)
{
    return !memcmp(r->sun.data(), o->sun, r->sun.size()) && !memcmp(r->moon.data(), o->moon, r->moon.size()) &&
           !memcmp(r->clouds.data(), o->clouds, r->clouds.size()) && o->nstars == r->nstars &&
           !memcmp(r->stars.data(), o->stars, r->stars.size() * sizeof(float)) &&
           (!o->end_sky || (!r->end_sky.empty() && !memcmp(r->end_sky.data(), o->end_sky, r->end_sky.size())));
}

extern "C" int render_set(struct render *r, int env, const struct raster_obs *o)
{
    if (env < 0 || env >= r->max_envs || o->w != r->w || o->h != r->h) return -1;
    if (o->nstars > MAX_STARS) return -1;
    for (int c = 0; c < o->ncmd; ++c)
        if (o->cmd[c].kind == RASTER_CMD_OTHER) return -1;
    for (int i = 0; !o->equads_in_range && i < o->nequad; ++i)
        if (o->equad[i].state < 0 || o->equad[i].state >= o->nestate || o->equad[i].tex < 0 || o->equad[i].tex >= o->ntex)
            return -1;
    for (int i = 0; i < o->nestate; ++i)
        if (!o->estate[i].no_light && (o->estate[i].lm < 0 || o->estate[i].lm >= o->nlm)) return -1;
    for (int i = 0; i < o->nportal; ++i)
        if (o->portal[i].tex < 0 || o->portal[i].tex >= o->ntex) return -1;
    if (!r->have_sheets) {
        r->sun.assign(o->sun, o->sun + 32 * 32 * 4);
        r->moon.assign(o->moon, o->moon + 128 * 64 * 4);
        r->clouds.assign(o->clouds, o->clouds + 256 * 256 * 4);
        if (o->end_sky) r->end_sky.assign(o->end_sky, o->end_sky + 128 * 128 * 3);
        r->stars.assign(o->stars, o->stars + (size_t)o->nstars * 12);
        r->nstars = o->nstars;
        CK(cudaMalloc(&r->d_sun, r->sun.size()));
        CK(cudaMalloc(&r->d_moon, r->moon.size()));
        CK(cudaMalloc(&r->d_clouds, r->clouds.size()));
        CK(cudaMalloc(&r->d_end_sky, 128 * 128 * 3));
        CK(cudaMalloc(&r->d_stars, (r->stars.size() + 1) * sizeof(float)));
        CK(cudaMemcpy(r->d_sun, r->sun.data(), r->sun.size(), cudaMemcpyHostToDevice));
        CK(cudaMemcpy(r->d_moon, r->moon.data(), r->moon.size(), cudaMemcpyHostToDevice));
        CK(cudaMemcpy(r->d_clouds, r->clouds.data(), r->clouds.size(), cudaMemcpyHostToDevice));
        if (o->end_sky) CK(cudaMemcpy(r->d_end_sky, r->end_sky.data(), r->end_sky.size(), cudaMemcpyHostToDevice));
        CK(cudaMemcpy(r->d_stars, r->stars.data(), r->stars.size() * sizeof(float), cudaMemcpyHostToDevice));
        r->aw = o->atlas_w; r->ah = o->atlas_h;
        r->atlas_bytes = (size_t)r->aw * r->ah * 4;
        CK(cudaMalloc(&r->d_atlas, r->atlas_bytes * (size_t)r->max_envs));
        r->have_sheets = 1;
    }
    else {
        if (o->atlas_w != r->aw || o->atlas_h != r->ah) return -1;
        if (o->end_sky && r->end_sky.empty()) {
            r->end_sky.assign(o->end_sky, o->end_sky + 128 * 128 * 3);
            CK(cudaMemcpy(r->d_end_sky, r->end_sky.data(), r->end_sky.size(), cudaMemcpyHostToDevice));
        }
        render::sheets_seen &k = r->seen[(size_t)env];
        const void *now[5] = {o->sun, o->moon, o->clouds, o->stars, o->end_sky};
        if (memcmp(k.p, now, sizeof now) || k.nstars != o->nstars) {
            if (!sheets_equal(r, o)) return -1;
            memcpy(k.p, now, sizeof now);
            k.nstars = o->nstars;
        }
    }
    if (r->aw % 16 || r->ah % 16) return -1;
    r->obs[(size_t)env] = o;
    return 0;
}

/* The render's staging, pinned: each upload takes a region of its own, the
 * regions start over with each render. The regions staged before the flush
 * go to the device in one copy (stage_up: their device address); a region
 * staged after it is copied on its own (stage). A larger buffer keeps what
 * the old one held, once the copies out of the old one are done. */
static size_t stage_off(render *r, size_t bytes)
{
    size_t at = (r->stage_at + 63) & ~(size_t)63;
    if (at + bytes > r->stage_cap) {
        size_t cap = 2 * (at + bytes) + 65536;
        unsigned char *grown = NULL;
        CK(cudaMallocHost((void **)&grown, cap));
        if (r->h_stage) {
            CK(cudaStreamSynchronize(r->st));
            memcpy(grown, r->h_stage, r->stage_at);
            CK(cudaFreeHost(r->h_stage));
        }
        r->h_stage = grown;
        r->stage_cap = cap;
    }
    r->stage_at = at + bytes;
    return at;
}

static unsigned char *stage(render *r, size_t bytes) { return r->h_stage + stage_off(r, bytes); }

/* a region of the next flush: its host bytes (valid until the next
 * staging call) and, in *dev, the device address it will have */
template <class T> static T *stage_up(render *r, size_t count, T **dev)
{
    size_t at = stage_off(r, count * sizeof(T) + 1);
    *dev = (T *)(uintptr_t)at;      /* an offset in d_up until the flush */
    return (T *)(r->h_stage + at);
}

/* the resident copies: WORDS 32-bit words from the flushed staging at SRC
 * (an offset in d_up) to DST, job blockIdx.y over the blocks of its row */
__global__ void upload_scatter(const render::scat *jobs, const unsigned char *up)
{
    const render::scat j = jobs[blockIdx.y];
    const uint32_t *s = (const uint32_t *)(up + j.src);
    uint32_t *d = (uint32_t *)j.dst;
    for (uint32_t i = blockIdx.x * blockDim.x + threadIdx.x; i < j.words; i += gridDim.x * blockDim.x) d[i] = s[i];
}

/* a resident copy from staged host bytes (the offset stage_off gave) */
static void scatter(render *r, void *dst, size_t src_off, size_t bytes)
{
    r->scats.push_back(render::scat{(unsigned long long)dst, (unsigned long long)src_off, (uint32_t)(bytes / 4), 0});
}

/* the staged regions to the device in one copy: an offset P stage_up gave
 * becomes d_up + P; then the resident copies */
static void flush(render *r)
{
    size_t jb = 0;
    if (!r->scats.empty()) {
        jb = stage_off(r, r->scats.size() * sizeof(render::scat));
        memcpy(r->h_stage + jb, r->scats.data(), r->scats.size() * sizeof(render::scat));
    }
    size_t bytes = r->stage_at;
    if (bytes > r->up_cap) {
        if (r->d_up) CK(cudaFree(r->d_up));
        r->up_cap = bytes + bytes / 2 + 65536;
        CK(cudaMalloc(&r->d_up, r->up_cap));
    }
    if (bytes) CK(cudaMemcpyAsync(r->d_up, r->h_stage, bytes, cudaMemcpyHostToDevice, r->st));
    r->stats.upload_bytes += bytes;
    if (!r->scats.empty()) {
        /* a row of blocks a job, as many as its longest job's words need;
         * at most 65535 rows a launch */
        uint32_t most = 0;
        for (const render::scat &j : r->scats) most = std::max(most, j.words);
        for (size_t j0 = 0; j0 < r->scats.size(); j0 += 65535) {
            dim3 grid(std::min<uint32_t>((most + 255) / 256, 64), (unsigned)std::min<size_t>(r->scats.size() - j0, 65535));
            upload_scatter<<<grid, 256, 0, r->st>>>((const render::scat *)(r->d_up + jb) + j0, r->d_up);
        }
        CK(cudaGetLastError());
    }
    r->scats.clear();
}

template <class T> static T *up_ptr(const render *r, T *off) { return (T *)(r->d_up + (uintptr_t)off); }
/* a region stage_up placed, on the host (until the next staging call) */
template <class T> static T *host_of(const render *r, T *off) { return (T *)(r->h_stage + (uintptr_t)off); }

static float ms(cudaEvent_t a, cudaEvent_t b)
{
    float t = 0;
    cudaEventElapsedTime(&t, a, b);
    return t;
}

static const raster_obs_draw *draw_of(const raster_obs *o, int i)
{
    return i < o->nopaque ? &o->opaque[i] : &o->water[i - o->nopaque];
}

/* A block of N int32s in the mesh arena: the smallest freed block that
 * holds it (its rest freed again), else the bump pointer, else the arena
 * grown (mesh_grow: it does not move); -1 when the device has no memory for that. */
/* (mu held) no render runs, unless on this thread */
static void draw_idle(render *r, std::unique_lock<std::mutex> &lk)
{
    r->cv.wait(lk, [r] { return !r->drawing || pthread_equal(r->drawer, pthread_self()); });
}

/* The mesh arena holds at least BYTES: device memory mapped at the end of
 * its reserved addresses, a MESH_STEP at a time, so the arena never moves
 * and is never copied (CUDA's virtual memory management). It was a
 * cudaMalloc grown by half and copied, which left a stream's arena a
 * quarter over its meshes on average and two and a half times them while
 * it grew (lane/heapprof: 710 MB of meshes a stream at the speedrun's 128
 * envs). 0 when the device has no memory for it. */
enum : size_t { MESH_STEP = (size_t)64 << 20 };

static int mesh_grow(render *r, size_t bytes)
{
    int dev = 0;
    if (cudaGetDevice(&dev) != cudaSuccess || cudaFree(0) != cudaSuccess) { cudaGetLastError(); return 0; }
    CUmemAllocationProp prop = {};
    prop.type = CU_MEM_ALLOCATION_TYPE_PINNED;
    prop.location.type = CU_MEM_LOCATION_TYPE_DEVICE;
    prop.location.id = dev;
    if (!r->mesh_va) {
        /* addresses for the device's whole memory: they cost nothing */
        size_t fr = 0, total = 0, gran = 0;
        if (cudaMemGetInfo(&fr, &total) != cudaSuccess) { cudaGetLastError(); return 0; }
        if (cuMemGetAllocationGranularity(&gran, &prop, CU_MEM_ALLOC_GRANULARITY_MINIMUM) != CUDA_SUCCESS ||
            MESH_STEP % gran) return 0;
        size_t len = (total + MESH_STEP - 1) / MESH_STEP * MESH_STEP;
        if (cuMemAddressReserve(&r->mesh_va, len, 0, 0, 0) != CUDA_SUCCESS) { r->mesh_va = 0; return 0; }
        r->mesh_va_len = len;
        r->d_mesh = (int32_t *)r->mesh_va;
    }
    CUmemAccessDesc acc = {};
    acc.location = prop.location;
    acc.flags = CU_MEM_ACCESS_FLAGS_PROT_READWRITE;
    while (r->mesh_mapped < bytes) {
        if (r->mesh_mapped + MESH_STEP > r->mesh_va_len) return 0;
        CUmemGenericAllocationHandle h;
        if (cuMemCreate(&h, MESH_STEP, &prop, 0) != CUDA_SUCCESS) return 0;
        CUdeviceptr at = r->mesh_va + r->mesh_mapped;
        if (cuMemMap(at, MESH_STEP, 0, h, 0) != CUDA_SUCCESS) { cuMemRelease(h); return 0; }
        if (cuMemSetAccess(at, MESH_STEP, &acc, 1) != CUDA_SUCCESS) {
            cuMemUnmap(at, MESH_STEP);
            cuMemRelease(h);
            return 0;
        }
        r->mesh_h.push_back(h);
        r->mesh_mapped += MESH_STEP;
        r->mesh_cap = (int64_t)(r->mesh_mapped / sizeof(int32_t));
    }
    return 1;
}

/* (mu held, as LK) */
static int64_t mesh_alloc(render *r, int64_t n, int64_t *cap, std::unique_lock<std::mutex> &lk)
{
    auto it = r->mesh_free.lower_bound(n);
    if (it != r->mesh_free.end()) {
        int64_t off = it->second, size = it->first;
        r->mesh_free.erase(it);
        /* a remainder worth keeping goes back */
        if (size - n >= 1024) { r->mesh_free.insert({size - n, off + n}); *cap = n; }
        else *cap = size;
        return off;
    }
    (void)lk;   /* the arena does not move: a render in flight reads on */
    if (r->mesh_used + n > r->mesh_cap && !mesh_grow(r, (size_t)(r->mesh_used + n) * sizeof(int32_t))) return -1;
    int64_t off = r->mesh_used;
    r->mesh_used += n;
    *cap = n;
    return off;
}

/* frame E of the render cannot be drawn: its view meshes everything again */
static void fail_frame(render *r, int e)
{
    r->failed[(size_t)e] = 1;
    const raster_obs *o = r->obs[r->bslot[(size_t)e]];
    if (o->mesh && o->mesh->resync) *o->mesh->resync = 1;
}

/* the same for the frame set on SLOT, by the device mesher's work before
 * its render (the render reads slot_fail) */
static void fail_slot(render *r, int slot)
{
    r->slot_fail[(size_t)slot] = 1;
    const raster_obs *o = r->obs[(size_t)slot];
    if (o->mesh && o->mesh->resync) *o->mesh->resync = 1;
}

/* The device mesher's work for the frames set on SLOTS[0..N) (those with a
 * feed, raster_obs mesh, engine/meshfeed.h), on its own stream: mesh_begin
 * applies each feed to its slot's world and launches the section passes'
 * count (no wait); mesh_end waits for the counts, gives each pass a block of
 * the mesh arena and launches the write pass there (no wait: ev_mesh, which
 * every render's stream waits for, follows it); with the host's meshes
 * beside them, each is compared byte for byte. Only one begun at a time: a
 * render between the two draws other frames. When the arena cannot hold
 * them every frame of the work fails and resyncs. */
static void mesh_begin(render *r, const int *slots, int n)
{
    mesh_job &J = r->mj;
    J.slots.clear();
    J.first.clear();
    for (int e = 0; e < n; ++e)
        if (r->obs[(size_t)slots[e]] && r->obs[(size_t)slots[e]]->mesh) J.slots.push_back(slots[e]);
    J.active = !J.slots.empty();
    if (!J.active) return;
    if (r->no_mesher) {
        std::lock_guard<std::mutex> g(r->mu);
        for (int slot : J.slots) { fail_slot(r, slot); r->slot_meshed[(size_t)slot] = 1; }
        J.active = 0;
        return;
    }
    if (!r->mesher) {
        if (!r->mst) CK(cudaStreamCreateWithPriority(&r->mst, cudaStreamNonBlocking, r->prio_low));
        if ((r->mesher = meshing_new(r->mst, r->max_envs, NULL))) meshing_set_tab(r->mesher, r->mesh_tab);
        else {
            fprintf(stderr, "render: the device mesher does not load\n");
            exit(1);
        }
    }
    meshing_begin(r->mesher);
    {
        std::lock_guard<std::mutex> g(r->mu);
        r->mstat.dmesh_write_ms += meshing_stats(r->mesher)->prev_write_ms;
    }
    for (int slot : J.slots) {
        const raster_obs *o = r->obs[(size_t)slot];
        if (!o->mesher || meshing_context(r->mesher, o->mesher)) {
            fprintf(stderr, "render: a frame meshed with other tables than the device mesher's\n");
            exit(1);
        }
        int f = meshing_add(r->mesher, slot, o->mesh);
        if (f < 0) {
            std::lock_guard<std::mutex> g(r->mu);
            fail_slot(r, slot);
        }
        J.first.push_back(f);
    }
    if (meshing_count_start(r->mesher)) { fprintf(stderr, "render: the device mesher failed\n"); exit(1); }
}

static int mesh_end(render *r)
{
    mesh_job &J = r->mj;
    if (!J.active) return 0;
    J.active = 0;
    if (r->no_mesher) return -1;
    const size_t ns = J.slots.size();
    const struct meshing_count *cnt = NULL;
    int nc = 0;
    if (meshing_count_wait(r->mesher, &cnt, &nc)) { fprintf(stderr, "render: the device mesher failed\n"); exit(1); }
    std::unique_lock<std::mutex> lk(r->mu);
    for (int slot : J.slots) r->slot_meshed[(size_t)slot] = 1;
    std::vector<int64_t> off((size_t)nc, -1);
    int full = 0;
    for (size_t j = 0; j < ns && !full; ++j) {
        if (J.first[j] < 0) continue;
        size_t slot = (size_t)J.slots[j];
        const meshfeed_out *f = r->obs[slot]->mesh;
        auto &map = r->dmesh_of[slot];
        for (int i = 0; i < f->nreq && !full; ++i) {
            const meshfeed_req &q = f->req[i];
            const struct meshing_count &c = cnt[J.first[j] + i];
            if (c.err) {
                fprintf(stderr, "render: the device mesher could not mesh section %d %d %d pass %d (error %d)\n",
                        q.cx, q.s, q.cz, q.pass, c.err);
                exit(1);
            }
            dmesh_slot &m = map[dmesh_key(q.cx, q.s, q.cz, q.pass)];
            if (m.cap) r->mesh_free.insert({m.cap, m.off});
            m = dmesh_slot{0, 0, c.count, c.flags, q.version, q.cx, q.s, q.cz};
            if (c.count > 0) {
                int64_t cap = 0, o = mesh_alloc(r, (int64_t)c.count * 8, &cap, lk);
                if (o < 0) { full = 1; break; }
                m.off = o;
                m.cap = cap;
                off[(size_t)(J.first[j] + i)] = o;
            }
            if (f->count_back) f->count_back[i] = c.count;
        }
    }
    const struct meshing_stats *ms_ = meshing_stats(r->mesher);
    r->mstat.dmesh_count_ms += ms_->count_ms;
    if (full) {
        /* the device holds no more: every mesh starts over, every view
         * resyncs (once no render reads the arena) */
        draw_idle(r, lk);
        ++r->mstat.mesh_resets;
        render_reset_meshes(r);
        for (int slot : J.slots) fail_slot(r, slot);
        for (int i = 0; i < r->max_envs; ++i) meshing_forget(r->mesher, i);
        return -1;
    }
    std::vector<int32_t *> outs((size_t)nc);
    for (int i = 0; i < nc; ++i) outs[(size_t)i] = off[(size_t)i] >= 0 ? r->d_mesh + off[(size_t)i] : NULL;
    if (meshing_write(r->mesher, outs.data())) { fprintf(stderr, "render: the device mesher failed\n"); exit(1); }
    CK(cudaEventRecord(r->ev_mesh, r->mst));
    r->mstat.dmesh_requests += ms_->requests;
    r->mstat.dmesh_upload_bytes += ms_->upload_bytes;
    r->mstat.upload_bytes += ms_->upload_bytes;
    r->mstat.dmesh_band_bytes += ms_->band_bytes;
    r->mstat.dmesh_chunk_bytes += ms_->chunk_bytes;
    r->mstat.dmesh_list_bytes += ms_->list_bytes;
    r->mstat.dmesh_req_bytes += ms_->req_bytes;
    r->mstat.dmesh_ctl_bytes += ms_->ctl_bytes;
    r->mstat.dmesh_band_run_bytes += ms_->band_run_bytes;
    /* the host's meshes of the same requests: equal, byte for byte */
    std::vector<int32_t> dev;
    for (size_t j = 0; j < ns; ++j) {
        size_t slot = (size_t)J.slots[j];
        const raster_obs *o = r->obs[slot];
        if (J.first[j] < 0 || !o->mesh_host) continue;
        for (int i = 0; i < o->mesh->nreq; ++i) {
            const meshfeed_req &q = o->mesh->req[i];
            const raster_obs_hostmesh &h = o->mesh_host[i];
            const struct meshing_count &c = cnt[J.first[j] + i];
            ++r->mstat.dmesh_checked;
            int bad = h.count != c.count || (h.count > 0 && h.flags != c.flags);
            long at = -1;
            if (!bad && c.count > 0) {
                dev.resize((size_t)c.count * 8);
                CK(cudaMemcpyAsync(dev.data(), r->d_mesh + off[(size_t)(J.first[j] + i)], dev.size() * 4,
                                   cudaMemcpyDeviceToHost, r->mst));
                CK(cudaStreamSynchronize(r->mst));
                /* the fields the Tessellator wrote (position always, the
                 * rest by the section's flags: an unset field keeps what the
                 * host's buffer held before, which nothing reads) */
                int used = 7 | (c.flags & 1 ? 24 : 0) | (c.flags & 2 ? 32 : 0) | (c.flags & 8 ? 64 : 0) | (c.flags & 4 ? 128 : 0);
                for (size_t k = 0; k < dev.size() && at < 0; ++k)
                    if ((used >> (k % 8) & 1) && dev[k] != h.raw[k]) at = (long)k;
                bad = at >= 0;
            }
            if (bad) {
                if (r->mstat.dmesh_diffs + r->stats.dmesh_diffs < 20)
                    fprintf(stderr, "render: device mesh differs: section %d %d %d pass %d: count %d/%d flags %d/%d, first int %ld (vertex %ld field %ld): device %d host %d\n",
                            q.cx, q.s, q.cz, q.pass, c.count, h.count, c.flags, h.flags, at, at < 0 ? -1 : at / 8,
                            at < 0 ? -1 : at % 8, at < 0 ? 0 : dev[(size_t)at], at < 0 ? 0 : h.raw[at]);
                ++r->mstat.dmesh_diffs;
            }
        }
    }
    /* the sections their views dropped (outside the square they keep): a
     * view requests only inside its square, so only when it moved */
    for (size_t j = 0; j < ns; ++j) {
        size_t slot = (size_t)J.slots[j];
        const meshfeed_out *f = r->obs[slot]->mesh;
        if (J.first[j] < 0) continue;
        std::array<int32_t, 4> sq = {f->cx0, f->cz0, f->cx1, f->cz1};
        if (r->dmesh_square[slot] == sq) continue;
        r->dmesh_square[slot] = sq;
        auto &map = r->dmesh_of[slot];
        for (auto it = map.begin(); it != map.end();) {
            const dmesh_slot &m = it->second;
            if (m.cx < f->cx0 || m.cx > f->cx1 || m.cz < f->cz0 || m.cz > f->cz1) {
                if (m.cap) r->mesh_free.insert({m.cap, m.off});
                it = map.erase(it);
            }
            else ++it;
        }
    }
    return 0;
}

extern "C" int render_mesh_start(struct render *r, int n, const int *slots)
{
    for (int e = 0; e < n; ++e) if (slots[e] < 0 || slots[e] >= r->max_envs || !r->obs[(size_t)slots[e]]) return -1;
    nvtxRangePushA("mesh start");
    mesh_end(r);
    mesh_begin(r, slots, n);
    nvtxRangePop();
    return 0;
}

extern "C" int render_mesh_finish(struct render *r)
{
    nvtxRangePushA("mesh finish");
    mesh_end(r);
    nvtxRangePop();
    return 0;
}

/* the render's frames: those set with a feed the mesher has not taken yet
 * meshed now (after the work begun for others), each frame's failure
 * noted; the render's stream waits for the meshes' write */
static void device_meshes(render *r, int n)
{
    std::vector<int> now;
    {
        std::lock_guard<std::mutex> g(r->mu);
        for (int e = 0; e < n; ++e) {
            size_t slot = r->bslot[(size_t)e];
            if (r->obs[slot]->mesh && !r->slot_meshed[slot]) now.push_back((int)slot);
        }
    }
    if (!now.empty()) {
        mesh_end(r);
        mesh_begin(r, now.data(), (int)now.size());
        mesh_end(r);
    }
    std::lock_guard<std::mutex> g(r->mu);
    for (int e = 0; e < n; ++e) {
        size_t slot = r->bslot[(size_t)e];
        if (r->slot_fail[slot]) r->failed[(size_t)e] = 1;
        r->slot_fail[slot] = 0;
        r->slot_meshed[slot] = 0;
    }
    if (r->mesher) CK(cudaStreamWaitEvent(r->st, r->ev_mesh, 0));
}

/* every environment's meshes resident: a section uploads once per version
 * into a block of the arena (its old version's block freed); a section its
 * environment has not drawn for 64 frames is dropped; only when the device
 * cannot hold the arena does it start over */
static void upload_meshes(render *r, int n, std::vector<ddraw> &D, std::vector<int32_t> &orders,
                          std::vector<uint32_t> &draw_base)
{
    enum { KEEP_FRAMES = 64 };
    std::unique_lock<std::mutex> lk(r->mu);
    std::vector<std::pair<int64_t, const raster_obs_draw *>> fresh;
    int64_t fresh_ints = 0;
    for (int attempt = 0;; ++attempt) {
        fresh.clear();
        fresh_ints = 0;
        int failed = 0;
        for (int e = 0; e < n && !failed; ++e) {
            size_t slot = r->bslot[(size_t)e];
            const raster_obs *o = r->obs[slot];
            uint64_t now = r->slot_frame[slot] + 1;
            for (int i = 0; i < o->nopaque + o->nwater && !failed; ++i) {
                const raster_obs_draw *d = draw_of(o, i);
                if (d->device) continue;
                mesh_slot &m = r->mesh_of[slot][d->key];
                m.last = now;
                if (m.cap && m.count == d->count && m.version == d->version) continue;
                if (m.cap) r->mesh_free.insert({m.cap, m.off});
                m.cap = 0;
                int64_t need = (int64_t)d->count * 8, cap = 0;
                int64_t off = need ? mesh_alloc(r, need, &cap, lk) : 0;
                if (off < 0) { failed = 1; break; }
                m.off = off;
                m.cap = cap;
                m.count = d->count;
                m.version = d->version;
                if (need) { fresh.push_back({off, d}); fresh_ints += need; }
            }
        }
        if (!failed) break;
        /* the device holds no more: every mesh again from an empty arena */
        if (attempt) { fprintf(stderr, "render: the meshes of one render do not fit the device\n"); exit(1); }
        ++r->stats.mesh_resets;
        render_reset_meshes(r);
    }
    /* the sections an environment stopped drawing */
    for (int e = 0; e < n; ++e) {
        size_t slot = r->bslot[(size_t)e];
        uint64_t now = ++r->slot_frame[slot];
        auto &map = r->mesh_of[slot];
        for (auto it = map.begin(); it != map.end();) {
            if (it->second.last + KEEP_FRAMES < now) {
                if (it->second.cap) r->mesh_free.insert({it->second.cap, it->second.off});
                it = map.erase(it);
            }
            else ++it;
        }
    }
    D.clear(); orders.clear();
    r->wjobs.clear();
    uint32_t wdist = 0;
    draw_base.assign((size_t)n + 1, 0);
    for (int e = 0; e < n; ++e) {
        size_t slot = r->bslot[(size_t)e];
        const raster_obs *o = r->obs[slot];
        draw_base[(size_t)e] = (uint32_t)D.size();
        size_t wjob0 = r->wjobs.size();
        uint32_t wdist0 = wdist;
        for (int i = 0; i < o->nopaque + o->nwater && !r->failed[(size_t)e]; ++i) {
            const raster_obs_draw *d = draw_of(o, i);
            ddraw dd;
            memset(&dd, 0, sizeof dd);
            dd.cx = d->cx; dd.s = d->s; dd.cz = d->cz; dd.flags = d->flags;
            dd.water = i >= o->nopaque;
            dd.env = e;
            dd.order = -1;
            if (d->device) {
                /* the device's mesh of this version, and its order */
                auto it = r->dmesh_of[slot].find(dmesh_key(d->cx, d->s, d->cz, dd.water));
                if (it == r->dmesh_of[slot].end() || it->second.version != d->version) {
                    fail_frame(r, e);
                    D.resize(draw_base[(size_t)e]);
                    r->wjobs.resize(wjob0);
                    wdist = wdist0;
                    break;
                }
                const dmesh_slot &m = it->second;
                dd.flags = m.flags;
                dd.nquads = (uint32_t)(m.count / 4);
                dd.mesh = m.off;
                if (dd.water && dd.nquads) {
                    dd.order = (int64_t)orders.size();
                    orders.resize(orders.size() + dd.nquads);
                    r->wjobs.push_back(wjob{(uint32_t)D.size(), wdist});
                    wdist += dd.nquads;
                }
                D.push_back(dd);
                continue;
            }
            const mesh_slot &m = r->mesh_of[slot][d->key];
            dd.nquads = (uint32_t)(d->count / 4);
            dd.mesh = m.off;
            if (d->order) {
                dd.order = (int64_t)orders.size();
                orders.insert(orders.end(), d->order, d->order + d->count / 4);
            }
            D.push_back(dd);
        }
    }
    draw_base[(size_t)n] = (uint32_t)D.size();
    size_t mesh_bytes = (size_t)fresh_ints * sizeof(int32_t);
    if (mesh_bytes) {
        /* each fresh mesh staged, and scattered to its block at the flush
         * (the arena is where it will stay: its growth is behind us) */
        size_t at = stage_off(r, mesh_bytes);
        for (auto &f : fresh) {
            size_t b = (size_t)f.second->count * 8 * sizeof(int32_t);
            memcpy(r->h_stage + at, f.second->raw, b);
            scatter(r, r->d_mesh + f.first, at, b);
            at += b;
        }
    }
    r->stats.mesh_bytes += mesh_bytes;
    r->stats.fresh_meshes += fresh.size();
}

/* the atlas tiles that differ from what each environment's copy holds
 * (after the first upload, only where the animation patches) */
static void upload_atlases(render *r, int n)
{
    int tx = r->aw / 16, ty = r->ah / 16;
    std::vector<uint32_t> idx;
    std::vector<unsigned char> data;
    std::vector<char> look((size_t)tx * ty);
    for (int e = 0; e < n; ++e) {
        const raster_obs *o = r->obs[r->bslot[(size_t)e]];
        std::vector<unsigned char> &sh = r->atlas_shadow[r->bslot[(size_t)e]];
        int all = sh.empty();
        if (all) sh.assign(r->atlas_bytes, 0);
        std::fill(look.begin(), look.end(), (char)all);
        for (int a = 0; !all && a < o->nanim; ++a)
            for (int y = o->anim[a][1] / 16; y <= (o->anim[a][1] + o->anim[a][3] - 1) / 16 && y < ty; ++y)
                for (int x = o->anim[a][0] / 16; x <= (o->anim[a][0] + o->anim[a][2] - 1) / 16 && x < tx; ++x)
                    if (x >= 0 && y >= 0) look[(size_t)y * tx + x] = 1;
        for (int t = 0; t < tx * ty; ++t) {
            if (!look[(size_t)t]) continue;
            int x0 = (t % tx) * 16, y0 = (t / tx) * 16, diff = all;
            for (int y = 0; y < 16 && !diff; ++y)
                diff = memcmp(&sh[((size_t)(y0 + y) * r->aw + x0) * 4], o->atlas + ((size_t)(y0 + y) * r->aw + x0) * 4, 64) != 0;
            if (!diff) continue;
            idx.push_back((uint32_t)r->bslot[(size_t)e]); idx.push_back((uint32_t)t);
            for (int y = 0; y < 16; ++y) {
                const unsigned char *src = o->atlas + ((size_t)(y0 + y) * r->aw + x0) * 4;
                data.insert(data.end(), src, src + 64);
                memcpy(&sh[((size_t)(y0 + y) * r->aw + x0) * 4], src, 64);
            }
        }
    }
    /* staged for the flush; patched after it (patch_atlases) */
    r->patch_n = (uint32_t)(idx.size() / 2);
    if (!r->patch_n) return;
    size_t ib = idx.size() * sizeof(uint32_t);
    memcpy(stage_up(r, idx.size(), &r->d_patch_idx), idx.data(), ib);
    memcpy(stage_up(r, data.size(), &r->d_patch), data.data(), data.size());
    r->stats.atlas_bytes += ib + data.size();
}

static void patch_atlases(render *r)
{
    if (!r->patch_n) return;
    atlas_patch<<<r->patch_n, 256, 0, r->st>>>(up_ptr(r, r->d_patch_idx), up_ptr(r, r->d_patch), r->patch_n, r->d_atlas,
                                                  r->atlas_bytes, r->aw);
    CK(cudaGetLastError());
    r->patch_n = 0;
}

/* The texture pool's least size. A pool as small as one render's textures
 * started over hundreds of times a run: the 512x256 item atlas takes a new
 * version whenever an env's clock or compass tile turns, each version
 * filled it, and every start over sent every texture again (lane/villscale:
 * 215 KB a headline env-step, the drawer's largest host read, to 12 KB with
 * the versions kept). It was 512 MB a stream, which those versions filled
 * all the same (12 MB/s a stream at the speedrun's 128 envs): 64 MB starts
 * over every few seconds for 3 KB more textures an env-step (15.1 to 18.0
 * KB of 148) and 1.9 GB less device memory over 4 streams (lane/heapprof). */
#define TEX_POOL_MIN (64ull << 20)

/* the recorded passes' textures in the pool, by content: DT gets one
 * entry per texture of every environment, in order */
static void upload_textures(render *r, int n, std::vector<dtex> &DT)
{
    for (int pass = 0; pass < 2; ++pass) {
        DT.clear();
        std::vector<std::pair<unsigned long long, const raster_obs_tex *>> fresh;
        std::vector<uint64_t> keys;
        unsigned long long first = r->pool_used;
        int overflow = 0;
        for (int e = 0; e < n && !overflow; ++e) {
            const raster_obs *o = r->obs[r->bslot[(size_t)e]];
            for (int i = 0; i < o->ntex; ++i) {
                const raster_obs_tex *t = &o->tex[i];
                size_t bytes = (size_t)t->w * t->h * 4;
                /* the recording's key (raster_rec_attach) */
                uint64_t k = t->key ? t->key : raster_obs_tex_key(t->rgba, t->w, t->h);
                auto it = r->pool_of.find(k);
                if (it == r->pool_of.end()) {
                    unsigned long long off = (r->pool_used + 15) & ~15ULL;
                    if (off + bytes > r->pool_cap) { overflow = 1; break; }
                    r->pool_used = off + bytes;
                    it = r->pool_of.emplace(k, tex_slot{off, t->w, t->h}).first;
                    fresh.push_back({off, t});
                }
                DT.push_back(dtex{it->second.off, t->w, t->h});
            }
        }
        if (overflow) {
            /* start the pool over, larger if one frame's textures need it */
            unsigned long long need = 0;
            for (int e = 0; e < n; ++e)
                for (int i = 0; i < r->obs[r->bslot[(size_t)e]]->ntex; ++i)
                    need += (unsigned long long)r->obs[r->bslot[(size_t)e]]->tex[i].w * r->obs[r->bslot[(size_t)e]]->tex[i].h * 4 + 16;
            if (need > r->pool_cap) {
                if (r->d_pool) CK(cudaFree(r->d_pool));
                r->pool_cap = need + need / 2 + (1 << 20);
                if (r->pool_cap < TEX_POOL_MIN) r->pool_cap = TEX_POOL_MIN;
                CK(cudaMalloc(&r->d_pool, r->pool_cap));
            }
            r->pool_of.clear();
            r->pool_used = 0;
            continue;
        }
        unsigned long long bytes = r->pool_used - first;
        if (bytes) {
            size_t at = stage_off(r, (size_t)bytes);
            for (auto &f : fresh)
                memcpy(r->h_stage + at + (f.first - first), f.second->rgba, (size_t)f.second->w * f.second->h * 4);
            scatter(r, r->d_pool + first, at, (size_t)bytes);
            r->stats.tex_bytes += (size_t)bytes;
        }
        return;
    }
}

/* a vector staged for the flush; *d its device array after it */
template <class T> static void send(render *r, T **d, const std::vector<T> &v)
{
    T *h = stage_up(r, v.size(), d);
    if (!v.empty()) memcpy(h, v.data(), v.size() * sizeof(T));
}

/* render_render's steps past the count in one wait (render.one_trip),
 * every environment in one group: the sizes stay on the device, the sort
 * runs to a bound, and the frames (h_frames, h_depths) come back with the
 * sizes. 1 when the frames are drawn; 0 when the triangles or pairs passed
 * the arena (the offsets are then scanned again for the usual steps). */
/* raster at the render's precision (RASTER_PM_*) */
static void raster_launch(const render *r, uint32_t nkeys, int e0, const texes &Xt)
{
    tile_order<<<1, 1024, 0, r->st>>>(r->d_key_start, r->d_key_n, nkeys, r->d_order);
#define GO(PM) raster<PM><<<nkeys * RASTER_SPLIT, RASTER_THREADS, 0, r->st>>>(r->d_env, e0, r->d_tris, r->d_vals2, \
                                  r->d_key_start, r->d_key_n, r->d_order, r->w, r->h, r->tiles_x, r->ntiles, r->d_atlas, \
                                  r->atlas_bytes, Xt, r->d_out, r->d_depth)
    if (r->pm == RASTER_PM_EXACT) GO(RASTER_PM_EXACT);
    else if (r->pm == RASTER_PM_FAST) GO(RASTER_PM_FAST);
    else GO(RASTER_PM_ANY);
#undef GO
}

/* the side streams 0..K-1 take the work queued on the render's stream so
 * far (fork_aux); the render's stream waits for side stream I's (join_aux) */
static void fork_aux(render *r, int k)
{
    CK(cudaEventRecord(r->fev, r->st));
    for (int i = 0; i < k; ++i) CK(cudaStreamWaitEvent(r->aux[i], r->fev, 0));
}

static void join_aux(render *r, int i)
{
    CK(cudaEventRecord(r->jev[i], r->aux[i]));
    CK(cudaStreamWaitEvent(r->st, r->jev[i], 0));
}

/* the write pass: the section quads on the render's stream, the sky's and
 * the recorded units, the entity quads and the clipped units beside it
 * (they write disjoint triangles: each unit from its offset on) */
/* the generic write's grid: each block (one warp) gathers the units with a
 * triangle from its share of the list (geom_gen.cu), so fewer blocks than
 * the list's warps fill theirs: 4 an SM (r->wgrid; 328 on the 3090)
 * measured best of 1 to 12 an SM on stored speedrun frames (lane/clipclimb) */
static unsigned gen_wgrid(const render *r, uint32_t n) { return std::min((n + 31) / 32, r->wgrid); }
#define WRITE_PASS(QG, QD0, NX, NE, NCLIP_GRID, DN, U0, U1, TB) do { \
        fork_aux(r, 3); \
        if (QG) quads<true><<<(QG), 256, 0, r->st>>>(r->d_env, r->d_draws, (QD0), r->d_mesh, r->d_orders, \
                                                     r->w, r->h, NULL, NULL, NULL, r->d_offsets, (TB), r->d_tris, r->d_tiles); \
        if (NX) generic<true><<<gen_wgrid(r, NX), 32, 0, r->aux[0]>>>(r->d_env, n, r->d_xlist, (NX), NULL, (U0), (U1), r->d_draws, \
                                     r->d_mesh, r->d_orders, r->d_stars, R, r->w, r->h, NULL, r->d_offsets, (TB), r->d_tris, r->d_tiles); \
        if (NE) equads<true><<<((NE) + 127) / 128, 128, 0, r->aux[1]>>>(r->d_env, n, r->d_elist, (NE), (U0), (U1), R, r->w, r->h, \
                                     NULL, NULL, NULL, r->d_offsets, (TB), r->d_tris, r->d_tiles); \
        if (NCLIP_GRID) generic<true><<<(NCLIP_GRID), 32, 0, r->aux[2]>>>(r->d_env, n, r->d_clip_list, clip_n, (DN), (U0), (U1), \
                                     r->d_draws, r->d_mesh, r->d_orders, r->d_stars, R, r->w, r->h, NULL, r->d_offsets, (TB), \
                                     r->d_tris, r->d_tiles); \
        for (int a_ = 0; a_ < 3; ++a_) join_aux(r, a_); \
    } while (0)

/* a render's N frames made TRIS triangles and PAIRS pairs: the means a
 * frame (one_trip's bounds) */
static void trip_learn(render *r, int n, uint32_t tris, uint32_t pairs)
{
    if (n <= 0) return;
    double t = (double)tris / n, p = (double)pairs / n;
    if (r->trip_meant == 0) { r->trip_meant = t; r->trip_meanp = p; return; }
    r->trip_meant += (t - r->trip_meant) / 16;
    r->trip_meanp += (p - r->trip_meanp) / 16;
}

static int one_trip(render *r, int n, const std::vector<envp> &P, const std::vector<ddraw> &D, uint32_t units,
                    uint32_t nx, uint32_t ne, uint32_t clip_bound, const recs &R)
{
    /* the tail's bounds: N frames make at most N times the most one frame
     * has made, and (the frames being of different environments) about N
     * times the mean: half over the mean and twice the most besides (past
     * them the render runs again the usual way, which learns the new most) */
    if (!r->trip_maxt) return 0;
    if (!r->d_trip) {
        CK(cudaMalloc(&r->d_trip, ((size_t)r->max_envs + 1 + TRIP_WORDS) * sizeof(uint32_t)));
        CK(cudaMallocHost((void **)&r->h_trip, TRIP_WORDS * sizeof(uint32_t)));
    }
    auto need = [n](uint32_t most, double mean) {
        uint64_t a = (uint64_t)n * most, b = (uint64_t)(1.5 * n * mean) + 2 * (uint64_t)most;
        return std::min(a + a / 8, b) + 1024;
    };
    uint32_t tcap = (uint32_t)std::min((uint64_t)r->tris_cap, need(r->trip_maxt, r->trip_meant));
    uint32_t bound = (uint32_t)std::min((uint64_t)std::min(r->pairs_cap, r->tris_cap * 4), need(r->trip_maxp, r->trip_meanp));
    uint32_t *ub = (uint32_t *)stage(r, ((size_t)n + 1) * sizeof(uint32_t));
    for (int e = 0; e < n; ++e) ub[e] = P[(size_t)e].unit_base;
    ub[n] = units;
    uint32_t *d_ub = r->d_trip + TRIP_WORDS;
    CK(cudaMemcpyAsync(d_ub, ub, ((size_t)n + 1) * sizeof(uint32_t), cudaMemcpyHostToDevice, r->st));
    CK(cudaEventRecord(r->ev[2], r->st));
    trip_tris<<<1, 256, 0, r->st>>>(r->d_offsets, d_ub, n, tcap, r->d_env_tri0, r->d_trip);
    trip_squash<<<(units + 256) / 256, 256, 0, r->st>>>(r->d_offsets, units, r->d_trip);
    /* the clipped units: their number is the device's */
    uint32_t clip_n = clip_bound;
    WRITE_PASS((unsigned)D.size(), 0, nx, ne, clip_bound ? gen_wgrid(r, clip_bound) : 0,
               r->d_nclip, 0, units, 0);
    CK(cudaGetLastError());
    CK(cudaEventRecord(r->ev[3], r->st));
    trip_tri_envs<<<(tcap + 255) / 256, 256, 0, r->st>>>(r->d_env_tri0, n, r->d_trip, r->d_tri_env);
    /* the tiles past the triangles count nothing */
    size_t tb = 0;
    trip_zero_tail<<<(tcap + 256) / 256, 256, 0, r->st>>>(r->d_tiles, tcap + 1, r->d_trip);
    rs_exclusive_sum(NULL, tb, r->d_tiles, r->d_pair_off, tcap + 1, r->st);
    temp_need(r, tb);
    rs_exclusive_sum(r->d_temp, tb, r->d_tiles, r->d_pair_off, tcap + 1, r->st);
    trip_pairs_n<<<1, 1, 0, r->st>>>(r->d_pair_off, bound, r->d_trip);
    trip_env_max<<<1, 256, 0, r->st>>>(r->d_env_tri0, r->d_pair_off, n, r->d_nclip, r->d_trip);
    uint32_t nkeys = (uint32_t)n * r->ntiles;
    trip_pairs<<<(std::max(tcap, bound / 8) + 255) / 256, 256, 0, r->st>>>(r->d_tris, r->d_trip, r->d_tri_env, r->d_pair_off,
                                                                            r->tiles_x, r->ntiles, bound, nkeys, r->d_keys, r->d_vals);
    CK(cudaGetLastError());
    int bits = 1;
    while ((1u << bits) <= nkeys) ++bits;
    tb = 0;
    rs_sort_pairs(NULL, tb, r->d_keys, r->d_keys2, r->d_vals, r->d_vals2, bound, 0, bits, r->st);
    temp_need(r, tb);
    rs_sort_pairs(r->d_temp, tb, r->d_keys, r->d_keys2, r->d_vals, r->d_vals2, bound, 0, bits, r->st);
    CK(cudaMemsetAsync(r->d_key_start, 0, (nkeys + 1) * sizeof(uint32_t), r->st));
    CK(cudaMemsetAsync(r->d_key_n, 0, (nkeys + 1) * sizeof(uint32_t), r->st));
    key_runs<<<(bound + 255) / 256, 256, 0, r->st>>>(r->d_keys2, bound, r->d_trip, r->d_key_start, r->d_key_n);
    CK(cudaEventRecord(r->ev[4], r->st));
    texes Xt;
    memset(&Xt, 0, sizeof Xt);
    Xt.aw = r->aw; Xt.ah = r->ah;
    Xt.sun = r->d_sun; Xt.moon = r->d_moon; Xt.clouds = r->d_clouds; Xt.end_sky = r->d_end_sky;
    Xt.pool = r->d_pool; Xt.tex = r->d_dtex; Xt.estate = r->d_estate; Xt.lm = r->d_lm; Xt.crack = r->d_crack;
    Xt.portal = r->d_portal;
    Xt.line = r->d_line;
    raster_launch(r, nkeys, 0, Xt);
    CK(cudaGetLastError());
    CK(cudaEventRecord(r->ev[5], r->st));
    CK(cudaMemcpyAsync(r->h_trip, r->d_trip, TRIP_WORDS * sizeof(uint32_t), cudaMemcpyDeviceToHost, r->st));
    if (r->h_frames) CK(cudaMemcpyAsync(r->h_frames, r->d_out, (size_t)n * r->w * r->h * 3, cudaMemcpyDeviceToHost, r->st));
    if (r->h_depths)
        CK(cudaMemcpyAsync(r->h_depths, r->d_depth, (size_t)n * r->w * r->h * sizeof(float), cudaMemcpyDeviceToHost, r->st));
    CK(cudaStreamSynchronize(r->st));
    if (r->h_trip[TRIP_OVER]) {
        ++r->stats.trip_overs;
        /* the offsets again (squashed): the usual steps take it from here */
        tb = 0;
        rs_exclusive_sum(NULL, tb, r->d_counts, r->d_offsets, units + 1, r->st);
        temp_need(r, tb);
        rs_exclusive_sum(r->d_temp, tb, r->d_counts, r->d_offsets, units + 1, r->st);
        return 0;
    }
    r->trip_maxt = std::max(r->trip_maxt, r->h_trip[TRIP_MAXT]);
    r->trip_maxp = std::max(r->trip_maxp, r->h_trip[TRIP_MAXP]);
    trip_learn(r, n, r->h_trip[TRIP_TRIS], r->h_trip[TRIP_PAIRS]);
    r->stats.units = units;
    r->stats.sky_units = nx + ne;
    r->stats.clip_units = r->h_trip[TRIP_NCLIP];
    r->stats.triangles = r->h_trip[TRIP_TRIS];
    r->stats.pairs = r->h_trip[TRIP_PAIRS];
    r->stats.groups = 1;
    r->stats.trips = 1;
    r->stats.upload_ms = ms(r->ev[0], r->ev[1]);
    r->stats.units_ms = ms(r->ev[1], r->ev[3]);
    r->stats.bin_ms = ms(r->ev[3], r->ev[4]);
    r->stats.raster_ms = ms(r->ev[4], r->ev[5]);
    r->stats.total_ms = ms(r->ev[0], r->ev[5]);
    return 1;
}

static int render_batch(struct render *r, int n);

extern "C" const unsigned char *render_render(struct render *r, int n)
{
    if (n < 1 || n > r->max_envs) return NULL;
    r->bslot.resize((size_t)n);
    r->bout.resize((size_t)n);
    for (int e = 0; e < n; ++e) {
        r->bslot[(size_t)e] = (size_t)e;
        r->bout[(size_t)e] = (unsigned long long)(r->d_out + (size_t)e * r->w * r->h * 3);
    }
    return render_batch(r, n) ? NULL : r->d_out;
}

extern "C" int render_render_list(struct render *r, int n, const int *slots, unsigned char *const *outs)
{
    if (n < 1 || n > r->max_envs) return -1;
    r->bslot.resize((size_t)n);
    r->bout.resize((size_t)n);
    for (int e = 0; e < n; ++e) {
        if (slots[e] < 0 || slots[e] >= r->max_envs) return -1;
        r->bslot[(size_t)e] = (size_t)slots[e];
        r->bout[(size_t)e] = (unsigned long long)outs[e];
    }
    return render_batch(r, n);
}

/* one environment's entity quads into their compact copy HQ, the extra
 * records (their indices the environment's own) into EX. A model box's six
 * quads and a flat item's are the device's to make (ebox, eitem): with
 * either (DST not NULL) the others go packed, DST[j] the environment's quad
 * index of HQ[j] (QBASE added), and BEX[b] (IEX[i]) is box b's (item i's)
 * first extra record (-1: none). */
static void fill_quads(const raster_obs *o, cquad *hq, uint32_t *dst, uint32_t qbase, int *bex, int *iex,
                       std::vector<cextra> &ex)
{
    ex.clear();
    int bi = 0, ii = 0, j = 0;
    for (int q = 0; q < o->nequad; ++q) {
        const raster_obs_equad &s = o->equad[q];
        const raster_obs_estate &st = o->estate[s.state];
        int extra = st.vertex_color || st.vertex_alpha || st.blend == 6;
        if (o->nebox) {
            while (bi < o->nebox && o->ebox[bi].first + 6 <= q) ++bi;
            if (bi < o->nebox && o->ebox[bi].first <= q) {
                /* its extra records: raster_ebox_quads' white, alpha 0 */
                if (q == o->ebox[bi].first) {
                    bex[bi] = extra ? (int)ex.size() : -1;
                    cextra x;
                    memset(&x, 0, sizeof x);
                    for (int i = 0; i < 4; ++i) for (int k = 0; k < 4; ++k) x.color[i][k] = 1.0f;
                    for (int f = 0; extra && f < 6; ++f) ex.push_back(x);
                }
                continue;
            }
        }
        if (o->neitem) {
            while (ii < o->neitem && o->eitem[ii].first + raster_eitem_quads(&o->eitem[ii]) <= q) ++ii;
            if (ii < o->neitem && o->eitem[ii].first <= q) {
                /* its extra records: white, glColor's alpha */
                const raster_obs_eitem &it = o->eitem[ii];
                if (q == it.first) {
                    iex[ii] = extra ? (int)ex.size() : -1;
                    cextra x;
                    for (int i = 0; i < 4; ++i) {
                        for (int k = 0; k < 4; ++k) x.color[i][k] = 1.0f;
                        x.alpha[i] = it.alpha;
                    }
                    for (int f = 0; extra && f < raster_eitem_quads(&it); ++f) ex.push_back(x);
                }
                continue;
            }
        }
        if (dst) dst[j] = qbase + (uint32_t)q;
        cquad &d = hq[j++];
        d.state = s.state;
        d.tex = s.tex;
        d.extra = extra ? (int)ex.size() : -1;
        for (int i = 0; i < 4; ++i) {
            const entity_clip_vertex &a = s.v[i];
            cvtx &c = d.v[i];
            for (int k = 0; k < 4; ++k) c.clip[k] = a.clip[k];
            c.fogcoord = a.fogcoord; c.u = a.u; c.v = a.v;
            c.light[0] = a.light[0]; c.light[1] = a.light[1];
            c.diffuse = a.diffuse;
        }
        if (d.extra >= 0) {
            cextra x;
            for (int i = 0; i < 4; ++i) {
                for (int k = 0; k < 4; ++k) x.color[i][k] = s.v[i].color[k];
                x.alpha[i] = s.v[i].alpha;
            }
            ex.push_back(x);
        }
    }
}

/* the packed quads to their places */
__global__ void equad_place(const cquad *src, const uint32_t *dst, uint32_t n, cquad *out)
{
    uint32_t i = blockIdx.x * blockDim.x + threadIdx.x;
    if (i < n) out[dst[i]] = src[i];
}

/* A warp's cquads out through shared memory (lane/coalesce: a thread's
 * word stores 172 bytes from its neighbour's met a sector each): lane L
 * built its record at S + L * CQ_WORDS, and the warp stores the 32
 * records' words 32 consecutive ones at a time, each to its record's
 * place DST (lane L's; < 0: none). Every lane of the warp calls it. */
enum { CQ_WORDS = sizeof(cquad) / 4 };
static_assert(sizeof(cquad) % 4 == 0 && alignof(cquad) == 4, "cquad is moved as words");
__device__ __forceinline__ void cquad_flush(const uint32_t *S, long long dst, cquad *out)
{
    __syncwarp();
    int lane = threadIdx.x & 31;
    for (int w = lane; w < 32 * CQ_WORDS; w += 32) {
        int r = w / CQ_WORDS;
        long long d = __shfl_sync(0xffffffffu, dst, r);
        if (d >= 0) ((uint32_t *)(out + d))[w - r * CQ_WORDS] = S[w];
    }
    __syncwarp();
}

/* raster_eitem_quad (raster_obs.c), operation for operation: item i's
 * quads (a block an item, a thread a quad) into OUT[first ..], each with
 * its extra records IEX[i] + quad (-1: none) */
__global__ void eitem(const raster_obs_eitem *I, const int *iex, const float (*xf)[32], uint32_t nitem, cquad *out)
{
    const raster_obs_eitem &it = I[blockIdx.x];
    const float *mv = xf[it.xf], *proj = xf[it.xf] + 16;
    const float p1 = it.p1, p2 = it.p2, p3 = it.p3, p4 = it.p4;
    const int w = it.w, h = it.h, nq = 2 + 2 * w + 2 * h;
    __shared__ uint32_t S[128 / 32][32 * CQ_WORDS];  /* the launch's 128 threads */
    uint32_t *mine = &S[threadIdx.x >> 5][(threadIdx.x & 31) * CQ_WORDS];
    for (int k0 = 0; k0 < nq; k0 += blockDim.x) {
        int k = k0 + (int)threadIdx.x;
        if (k < nq) {
        float t = 0.0f - it.thick;
        float p[4][3], uv[4][2];
        int dir;
        if (k == 0) {
            const float pp[4][3] = {{0, 0, 0}, {1, 0, 0}, {1, 1, 0}, {0, 1, 0}};
            const float uu[4][2] = {{p1, p4}, {p3, p4}, {p3, p2}, {p1, p2}};
            memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 0;
        }
        else if (k == 1) {
            const float pp[4][3] = {{0, 1, t}, {1, 1, t}, {1, 0, t}, {0, 0, t}};
            const float uu[4][2] = {{p1, p2}, {p3, p2}, {p3, p4}, {p1, p4}};
            memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 1;
        }
        else if (k < 2 + 2 * w) {
            int i = k < 2 + w ? k - 2 : k - 2 - w;
            float v8 = 0.5f * (p1 - p3) / (float)w;
            float x = (float)i / (float)w;
            float u = p1 + (p3 - p1) * x - v8;
            if (k < 2 + w) {
                const float pp[4][3] = {{x, 0, t}, {x, 0, 0}, {x, 1, 0}, {x, 1, t}};
                const float uu[4][2] = {{u, p4}, {u, p4}, {u, p2}, {u, p2}};
                memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 2;
            }
            else {
                float x1 = x + 1.0f / (float)w;
                const float pp[4][3] = {{x1, 1, t}, {x1, 1, 0}, {x1, 0, 0}, {x1, 0, t}};
                const float uu[4][2] = {{u, p2}, {u, p2}, {u, p4}, {u, p4}};
                memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 3;
            }
        }
        else {
            int i = k < 2 + 2 * w + h ? k - 2 - 2 * w : k - 2 - 2 * w - h;
            float v9 = 0.5f * (p4 - p2) / (float)h;
            float y = (float)i / (float)h;
            float v = p4 + (p2 - p4) * y - v9;
            if (k < 2 + 2 * w + h) {
                float y1 = y + 1.0f / (float)h;
                const float pp[4][3] = {{0, y1, 0}, {1, y1, 0}, {1, y1, t}, {0, y1, t}};
                const float uu[4][2] = {{p1, v}, {p3, v}, {p3, v}, {p1, v}};
                memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 4;
            }
            else {
                const float pp[4][3] = {{1, y, 0}, {0, y, 0}, {0, y, t}, {1, y, t}};
                const float uu[4][2] = {{p3, v}, {p1, v}, {p1, v}, {p3, v}};
                memcpy(p, pp, sizeof p); memcpy(uv, uu, sizeof uv); dir = 5;
            }
        }
        cquad &q = *(cquad *)mine;
        q.state = it.state;
        q.tex = it.tex;
        q.extra = iex[blockIdx.x] < 0 ? -1 : iex[blockIdx.x] + k;
        for (int c = 0; c < 4; ++c) {
            float eye[4], wv[4] = {p[c][0], p[c][1], p[c][2], 1.0f};
            for (int r = 0; r < 4; ++r)
                eye[r] = mv[r] * wv[0] + mv[4 + r] * wv[1] + mv[8 + r] * wv[2] + mv[12 + r] * wv[3];
            cvtx &cv = q.v[c];
            for (int r = 0; r < 4; ++r)
                cv.clip[r] = proj[r] * eye[0] + proj[4 + r] * eye[1] + proj[8 + r] * eye[2] + proj[12 + r] * eye[3];
            cv.fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
            float u = uv[c][0], vv = uv[c][1];
            if (it.tex_matrix) {
                float tu = it.tm[0] * u + it.tm[1] * vv + it.tm[2];
                float tv = it.tm[3] * u + it.tm[4] * vv + it.tm[5];
                u = tu;
                vv = tv;
            }
            cv.u = u;
            cv.v = vv;
            cv.diffuse = it.diffuse[dir];
            cv.light[0] = it.light[0];
            cv.light[1] = it.light[1];
        }
        }
        cquad_flush(S[threadIdx.x >> 5], k < nq ? (long long)it.first + k : -1, out);
    }
}

/* raster_ebox_diffuse, operation for operation */
__device__ float ebox_diffuse(float nx, float ny, float nz)
{
    const float n = 1.236931687687298f;
    float d0 = (nx * 0.2f + ny - nz * 0.7f) / n;
    float d1 = (-nx * 0.2f + ny + nz * 0.7f) / n;
    float d = 0.4f + 0.6f * fmaxf(0, d0) + 0.6f * fmaxf(0, d1);
    return fmaxf(0, d);
}

static __constant__ int EBOX_FACE[6][4] = {{5, 1, 2, 6}, {0, 4, 7, 3}, {5, 4, 0, 1},
                                            {2, 3, 7, 6}, {1, 0, 3, 2}, {4, 5, 6, 7}};

/* raster_ebox_quads (raster_obs.c), operation for operation: box i's six
 * quads into OUT[first ..] (B's first and xf the render's), each with its
 * extra records BEX[i] + face (-1: none); a thread a face (lane/coalesce:
 * a thread a box made 258 word stores one after another, a sector each,
 * from a grid of a few blocks), its four corners computed as the box's */
__global__ void ebox(const raster_obs_ebox *B, const int *bex, const float (*xf)[32], uint32_t nbox, cquad *out)
{
    __shared__ uint32_t S[128 / 32][32 * CQ_WORDS];  /* the launch's 128 threads */
    uint32_t t = blockIdx.x * blockDim.x + threadIdx.x, i = t / 6;
    int f = (int)(t % 6);
    if (i < nbox) {
    const raster_obs_ebox &b = B[i];
    const float *mv = xf[b.xf], *proj = xf[b.xf] + 16;
    const int u = b.u, vv = b.vv, dx = b.dx, dy = b.dy, dz = b.dz;
    int ids[4];
    for (int k = 0; k < 4; ++k) ids[k] = EBOX_FACE[f][b.mirror ? 3 - k : k];
    /* the face's corners (raster_obs.c ebox_corner): clip position and eye distance */
    float cclip[4][4], cfog[4];
    for (int k = 0; k < 4; ++k) {
        const float *v = b.v[ids[k]];
        float w[3] = {b.base[0] + v[0], b.base[1] + v[1], b.base[2] + v[2]};
        if (b.outer) { w[0] = v[0]; w[1] = v[1]; w[2] = v[2]; }
        float eye[4];
        for (int r = 0; r < 4; ++r)
            eye[r] = mv[r] * w[0] + mv[4 + r] * w[1] + mv[8 + r] * w[2] + mv[12 + r];
        for (int r = 0; r < 4; ++r)
            cclip[k][r] = proj[r] * eye[0] + proj[4 + r] * eye[1] + proj[8 + r] * eye[2] + proj[12 + r] * eye[3];
        cfog[k] = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    }
    int uv[4];
    switch (f) {
    case 0: uv[0] = u + dz + dx; uv[1] = vv + dz; uv[2] = u + dz + dx + dz; uv[3] = vv + dz + dy; break;
    case 1: uv[0] = u; uv[1] = vv + dz; uv[2] = u + dz; uv[3] = vv + dz + dy; break;
    case 2: uv[0] = u + dz; uv[1] = vv; uv[2] = u + dz + dx; uv[3] = vv + dz; break;
    case 3: uv[0] = u + dz + dx; uv[1] = vv + dz; uv[2] = u + dz + 2 * dx; uv[3] = vv; break;
    case 4: uv[0] = u + dz; uv[1] = vv + dz; uv[2] = u + dz + dx; uv[3] = vv + dz + dy; break;
    default: uv[0] = u + 2 * dz + dx; uv[1] = vv + dz; uv[2] = u + 2 * dz + 2 * dx; uv[3] = vv + dz + dy; break;
    }
    {
        float a[3], c[3], n[3];
        for (int j = 0; j < 3; ++j) {
            a[j] = b.v[ids[1]][j] - b.v[ids[0]][j];
            c[j] = b.v[ids[1]][j] - b.v[ids[2]][j];
        }
        n[0] = c[1] * a[2] - c[2] * a[1];
        n[1] = c[2] * a[0] - c[0] * a[2];
        n[2] = c[0] * a[1] - c[1] * a[0];
        float len = sqrtf(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (b.flip_normal) { n[0] = -n[0]; n[1] = -n[1]; n[2] = -n[2]; }
        float d = len > 0 ? ebox_diffuse(n[0] / len, n[1] / len, n[2] / len) : 1;
        float us[4] = {(float)uv[2], (float)uv[0], (float)uv[0], (float)uv[2]};
        float vs_lo = (float)uv[1], vs_hi = (float)uv[3];
        float vs[4];
        if (b.mirror) { vs[0] = vs[1] = vs_hi; vs[2] = vs[3] = vs_lo; }
        else { vs[0] = vs[1] = vs_lo; vs[2] = vs[3] = vs_hi; }
        cquad &q = *(cquad *)&S[threadIdx.x >> 5][(threadIdx.x & 31) * CQ_WORDS];
        q.state = b.state;
        q.tex = b.tex;
        q.extra = bex[i] < 0 ? -1 : bex[i] + f;
        for (int k = 0; k < 4; ++k) {
            float qu = us[k] / b.tex_w + b.tex_u, qv = vs[k] / b.tex_h + b.tex_v;
            if (b.tex_matrix) {
                float ta = b.tm[0] * qu + b.tm[2] * qv + b.tm[4];
                float tb = b.tm[1] * qu + b.tm[3] * qv + b.tm[5];
                qu = ta;
                qv = tb;
            }
            cvtx &cv = q.v[k];
            for (int r = 0; r < 4; ++r) cv.clip[r] = cclip[k][r];
            cv.fogcoord = cfog[k];
            cv.u = qu;
            cv.v = qv;
            cv.light[0] = b.light[0];
            cv.light[1] = b.light[1];
            cv.diffuse = d;
        }
    }
    }
    cquad_flush(S[threadIdx.x >> 5], i < nbox ? (long long)B[i].first + f : -1, out);
}

/* a render in flight (render.drawing) for its scope */
struct draw_scope {
    render *r;
    explicit draw_scope(render *r_) : r(r_)
    {
        std::lock_guard<std::mutex> g(r->mu);
        r->drawing = 1;
        r->drawer = pthread_self();
    }
    ~draw_scope()
    {
        std::lock_guard<std::mutex> g(r->mu);
        r->drawing = 0;
        r->cv.notify_all();
    }
};

static int render_batch(struct render *r, int n)
{
    for (int e = 0; e < n; ++e) if (!r->obs[r->bslot[(size_t)e]]) return -1;
    draw_scope in_flight(r);
    memset(&r->stats, 0, sizeof r->stats);
    r->stage_at = 0;
    r->scats.clear();
    r->patch_n = 0;
    CK(cudaEventRecord(r->ev[0], r->st));
    std::vector<envp> &P = r->P;
    std::vector<ddraw> &D = r->D;
    std::vector<int32_t> &orders = r->orders;
    std::vector<uint32_t> &draw_base = r->draw_base;
    std::vector<dtex> &DT = r->DT;
    std::vector<xunit> &X = r->X, &XE = r->XE;   /* the generic kernel's units; the recorded entity quads */
    P.resize((size_t)n);
    X.clear();
    XE.clear();
    std::fill(r->failed.begin(), r->failed.end(), 0);
    nvtxRangePushA("device meshes");
    device_meshes(r, n);
    nvtxRangePop();
    /* the mesher's work since the last render (ahead of this one, or just now) */
    {
        std::lock_guard<std::mutex> g(r->mu);
        const struct render_stats &m = r->mstat;
        struct render_stats &t = r->stats;
        t.upload_bytes += m.upload_bytes;
        t.mesh_resets += m.mesh_resets;
        t.dmesh_requests += m.dmesh_requests; t.dmesh_upload_bytes += m.dmesh_upload_bytes;
        t.dmesh_checked += m.dmesh_checked; t.dmesh_diffs += m.dmesh_diffs;
        t.dmesh_count_ms += m.dmesh_count_ms; t.dmesh_write_ms += m.dmesh_write_ms;
        t.dmesh_band_bytes += m.dmesh_band_bytes; t.dmesh_chunk_bytes += m.dmesh_chunk_bytes;
        t.dmesh_list_bytes += m.dmesh_list_bytes; t.dmesh_req_bytes += m.dmesh_req_bytes;
        t.dmesh_ctl_bytes += m.dmesh_ctl_bytes;
        t.dmesh_band_run_bytes += m.dmesh_band_run_bytes;
        memset(&r->mstat, 0, sizeof r->mstat);
    }
    nvtxRangePushA("stage meshes");
    upload_meshes(r, n, D, orders, draw_base);
    nvtxRangePop();
    nvtxRangePushA("stage atlas+tex");
    upload_atlases(r, n);
    upload_textures(r, n, DT);
    nvtxRangePop();
    nvtxRangePushA("stage units");

    /* the recorded passes' tables: each environment's copied straight into
     * its place in the render's (regions of the flush, filled once all are
     * placed: a staging call may move the buffer) */
    std::vector<uint32_t> &qb = r->hqbase;
    qb.assign((size_t)n + 1, 0);
    size_t nes = 0, nlm = 0, ncr = 0, npo = 0, nln = 0;
    for (int e = 0; e < n; ++e) {
        const raster_obs *o = r->obs[r->bslot[(size_t)e]];
        nes += (size_t)o->nestate; nlm += (size_t)o->nlm; ncr += (size_t)o->ncrack;
        npo += (size_t)o->nportal; nln += (size_t)o->nline;
        qb[(size_t)e + 1] = qb[(size_t)e] + (uint32_t)o->nequad;
    }
    size_t nq_all = qb[(size_t)n];
    /* the model boxes: each one's six quads are made on the device (ebox)
     * from its record, the other quads go packed (hp: each environment's
     * first packed quad) */
    std::vector<uint32_t> &bb = r->hbbase, &ib = r->hibase, &xb = r->hxbase;
    bb.assign((size_t)n + 1, 0);
    ib.assign((size_t)n + 1, 0);
    xb.assign((size_t)n + 1, 0);
    std::vector<uint32_t> gq((size_t)n + 1, 0);   /* each environment's first generated quad */
    for (int e = 0; e < n; ++e) {
        const raster_obs *o = r->obs[r->bslot[(size_t)e]];
        bb[(size_t)e + 1] = bb[(size_t)e] + (uint32_t)o->nebox;
        ib[(size_t)e + 1] = ib[(size_t)e] + (uint32_t)o->neitem;
        xb[(size_t)e + 1] = xb[(size_t)e] + (uint32_t)o->nebox_xf;
        uint32_t g = 6 * (uint32_t)o->nebox;
        for (int i = 0; i < o->neitem; ++i) g += (uint32_t)raster_eitem_quads(&o->eitem[i]);
        gq[(size_t)e + 1] = gq[(size_t)e] + g;
    }
    const size_t nbox = bb[(size_t)n], nitem = ib[(size_t)n], nxf = xb[(size_t)n], nq_host = nq_all - gq[(size_t)n];
    const int gen = nbox || nitem;
    auto hp = [&](int e) { return qb[(size_t)e] - gq[(size_t)e]; };
    cquad *d_qsrc = NULL;
    uint32_t *d_qdst = NULL;
    raster_obs_ebox *d_box = NULL;
    raster_obs_eitem *d_item = NULL;
    float (*d_xf)[32] = NULL;
    int *d_bex = NULL, *d_iex = NULL;
    stage_up(r, nes, &r->d_estate);
    stage_up(r, nlm, &r->d_lm);
    stage_up(r, ncr, &r->d_crack);
    stage_up(r, nq_host, &d_qsrc);
    if (gen) {
        stage_up(r, nq_host, &d_qdst);
        stage_up(r, nbox, &d_box);
        stage_up(r, nitem, &d_item);
        stage_up(r, nxf, &d_xf);
        stage_up(r, nbox, &d_bex);
        stage_up(r, nitem, &d_iex);
    }
    stage_up(r, npo, &r->d_portal);
    stage_up(r, nln, &r->d_line);
    /* the entity quads straight to their staging, compact (cquad), the
     * environments over up to 8 threads (a copy bound by the memory of one
     * thread: the quads are 58 MB a 64-frame render in the village) */
    cquad *hq = host_of(r, d_qsrc);
    uint32_t *hdst = gen ? host_of(r, d_qdst) : NULL;
    r->hbex.assign(nbox + 1, -1);
    r->hiex.assign(nitem + 1, -1);
    std::vector<cextra> &EX = r->hEX;
    EX.clear();
    if (r->hexe.size() < (size_t)n) r->hexe.resize((size_t)n);
    {
        int T = nq_host < 8192 ? 1 : std::min(8, n);
        auto work = [&](int t) {
            for (int e = t; e < n; e += T)
                fill_quads(r->obs[r->bslot[(size_t)e]], hq + hp(e), hdst ? hdst + hp(e) : NULL, qb[(size_t)e],
                           r->hbex.data() + bb[(size_t)e], r->hiex.data() + ib[(size_t)e], r->hexe[(size_t)e]);
        };
        std::vector<std::thread> th;
        for (int t = 1; t < T; ++t) th.emplace_back(work, t);
        work(0);
        for (std::thread &t : th) t.join();
    }
    for (int e = 0; e < n; ++e) {
        std::vector<cextra> &ex = r->hexe[(size_t)e];
        if (ex.empty()) continue;
        int base = (int)EX.size();
        for (uint32_t q = hp(e); q < hp(e + 1); ++q)
            if (hq[q].extra >= 0) hq[q].extra += base;
        for (uint32_t b = bb[(size_t)e]; b < bb[(size_t)e + 1]; ++b)
            if (r->hbex[b] >= 0) r->hbex[b] += base;
        for (uint32_t i = ib[(size_t)e]; i < ib[(size_t)e + 1]; ++i)
            if (r->hiex[i] >= 0) r->hiex[i] += base;
        EX.insert(EX.end(), ex.begin(), ex.end());
    }
    if (gen) {
        raster_obs_ebox *HB = host_of(r, d_box);
        raster_obs_eitem *HI = host_of(r, d_item);
        float (*HX)[32] = host_of(r, d_xf);
        for (int e = 0; e < n; ++e) {
            const raster_obs *o = r->obs[r->bslot[(size_t)e]];
            for (int i = 0; i < o->nebox; ++i) {
                raster_obs_ebox &b = HB[bb[(size_t)e] + (uint32_t)i];
                b = o->ebox[i];
                b.first += (int)qb[(size_t)e];
                b.xf += (int)xb[(size_t)e];
            }
            for (int i = 0; i < o->neitem; ++i) {
                raster_obs_eitem &it = HI[ib[(size_t)e] + (uint32_t)i];
                it = o->eitem[i];
                it.first += (int)qb[(size_t)e];
                it.xf += (int)xb[(size_t)e];
            }
            if (o->nebox_xf) memcpy(HX[xb[(size_t)e]], o->ebox_xf, sizeof *HX * (size_t)o->nebox_xf);
        }
        memcpy(host_of(r, d_bex), r->hbex.data(), nbox * sizeof(int));
        memcpy(host_of(r, d_iex), r->hiex.data(), nitem * sizeof(int));
    }
    raster_obs_estate *ES = host_of(r, r->d_estate);
    uint32_t (*LM)[256] = host_of(r, r->d_lm);
    raster_obs_crack *CR = host_of(r, r->d_crack);
    raster_obs_portal *PO = host_of(r, r->d_portal);
    raster_obs_line *LN = host_of(r, r->d_line);

    /* each environment's units: the sky's slots, the opaque section quads,
     * then its recorded passes in order with the translucent sections where
     * raster_live_translucent ran (after the opaque ones when none ran) */
    uint32_t units = 0, tex_base = 0;
    uint32_t es_n = 0, lm_n = 0, cr_n = 0, po_n = 0, ln_n = 0;
    int nexact = 0, nfast = 0;
    for (int e = 0; e < n; ++e) {
        const raster_obs *o = r->obs[r->bslot[(size_t)e]];
        nexact += o->prec == RP_EXACT;
        nfast += o->prec == RP_FAST;
        envp &p = P[(size_t)e];
        memcpy(p.proj, o->proj, sizeof p.proj);
        memcpy(p.mv, o->mv, sizeof p.mv);
        for (int i = 0; i < 3; ++i) { p.cam[i] = o->cam[i]; p.fog[i] = o->fog[i]; }
        p.fogs = o->fogs; p.foge = o->foge; p.fogd = o->fogd; p.fogm = o->fogm;
        p.prec = o->prec;
        memcpy(p.lm, o->lm, sizeof p.lm);
        p.sky = o->sky;
        p.nstars = (uint32_t)o->nstars;
        p.unit_base = units;
        p.draw_base = draw_base[(size_t)e];
        p.ndraws = draw_base[(size_t)e + 1] - draw_base[(size_t)e];
        p.estate_base = es_n;
        p.tex_base = tex_base;
        p.lm_base = lm_n;
        p.crack_base = cr_n;
        p.equad_base = qb[(size_t)e];
        p.portal_base = po_n;
        p.line_base = ln_n;
        if (o->nline) memcpy(LN + ln_n, o->line, sizeof *LN * (size_t)o->nline);
        ln_n += (uint32_t)o->nline;
        p.slot = (uint32_t)r->bslot[(size_t)e];
        p.out = r->bout[(size_t)e];
        if (o->nportal) memcpy(PO + po_n, o->portal, sizeof *PO * (size_t)o->nportal);
        po_n += (uint32_t)o->nportal;
        tex_base += (uint32_t)o->ntex;
        if (o->nestate) memcpy(ES + es_n, o->estate, sizeof *ES * (size_t)o->nestate);
        es_n += (uint32_t)o->nestate;
        for (int i = 0; i < o->nlm; ++i) memcpy(LM[lm_n + (uint32_t)i], o->lm_table[i], sizeof LM[0]);
        lm_n += (uint32_t)o->nlm;
        if (o->ncrack) memcpy(CR + cr_n, o->crack, sizeof *CR * (size_t)o->ncrack);
        cr_n += (uint32_t)o->ncrack;
        const raster_obs_sky &k = p.sky;
        uint32_t b = units;
        auto sky = [&](uint32_t a, uint32_t z) { for (uint32_t u = a; u < z; ++u) X.push_back(xunit{b + u, XU_SKY, u, 0}); };
        if (k.dimension == 1) sky(0, 6);
        else if (k.dimension == 0) {
            sky(U_PLANE1, U_RISE);
            if (k.sunrise[3] > 0.0f) sky(U_RISE, U_SUN);
            sky(U_SUN, U_STARS);
            if (k.stars) sky(U_STARS, U_STARS + (uint32_t)o->nstars);
            sky(U_PLANE2, U_CLOUDS);
            if (k.clouds) sky(U_CLOUDS, SKY_UNITS);
        }
        uint32_t local = SKY_UNITS;
        auto sections = [&](int water) {
            for (uint32_t i = p.draw_base; i < p.draw_base + p.ndraws; ++i)
                if (D[i].water == water) { D[i].unit0 = b + local; local += D[i].nquads; }
        };
        sections(0);
        int water = 0;
        for (int c = 0; c < o->ncmd; ++c) {
            const raster_obs_cmd &cmd = o->cmd[c];
            if (cmd.kind == RASTER_CMD_WATER) { sections(1); water = 1; }
            else if (cmd.kind == RASTER_CMD_CLEAR_DEPTH) X.push_back(xunit{b + local++, XU_CLEAR, 0, 0});
            else if (cmd.kind == RASTER_CMD_DEPTH_SNAP) X.push_back(xunit{b + local++, XU_SNAP, 0, 0});
            else if (cmd.kind == RASTER_CMD_QUAD)
                for (int q = cmd.first; q < cmd.first + cmd.count; ++q)
                    XE.push_back(xunit{b + local++, XU_EQUAD, p.equad_base + (uint32_t)q, 0});
            else if (cmd.kind == RASTER_CMD_CRACK)
                for (int q = cmd.first; q < cmd.first + cmd.count; ++q)
                    X.push_back(xunit{b + local++, XU_CRACK, p.crack_base + (uint32_t)q, 0});
            else if (cmd.kind == RASTER_CMD_PORTAL)
                for (int q = cmd.first; q < cmd.first + cmd.count; ++q)
                    X.push_back(xunit{b + local++, XU_PORTAL, p.portal_base + (uint32_t)q, 0});
            else if (cmd.kind == RASTER_CMD_LINE)
                for (int q = cmd.first; q < cmd.first + cmd.count; ++q)
                    X.push_back(xunit{b + local++, XU_LINE, p.line_base + (uint32_t)q, 0});
        }
        if (!water) sections(1);
        p.nunits = local;
        units += local;
    }
    r->pm = nexact == n ? RASTER_PM_EXACT : nfast == n ? RASTER_PM_FAST : RASTER_PM_ANY;
    nvtxRangePop();
    nvtxRangePushA("stage send");
    send(r, &r->d_env, P);
    send(r, &r->d_draws, D);
    send(r, &r->d_orders, orders);
    send(r, &r->d_wjobs, r->wjobs);
    send(r, &r->d_xlist, X);
    send(r, &r->d_elist, XE);
    send(r, &r->d_eextra, EX);
    send(r, &r->d_dtex, DT);
    r->stats.quad_bytes += nq_host * sizeof(cquad) + EX.size() * sizeof(cextra) +
                           (gen ? nq_host * sizeof(uint32_t) + nbox * (sizeof(raster_obs_ebox) + sizeof(int)) +
                                  nitem * (sizeof(raster_obs_eitem) + sizeof(int)) + nxf * sizeof *d_xf : 0);
    /* the units each environment starts at, for the copy back of its first
     * triangle */
    uint32_t *d_ub;
    {
        uint32_t *ub = stage_up(r, (size_t)n + 1, &d_ub);
        for (int e = 0; e <= n; ++e) ub[e] = e < n ? P[(size_t)e].unit_base : units;
    }
    flush(r);
    r->d_env = up_ptr(r, r->d_env);
    r->d_draws = up_ptr(r, r->d_draws);
    r->d_orders = up_ptr(r, r->d_orders);
    r->d_wjobs = up_ptr(r, r->d_wjobs);
    r->d_xlist = up_ptr(r, r->d_xlist);
    r->d_elist = up_ptr(r, r->d_elist);
    r->d_eextra = up_ptr(r, r->d_eextra);
    r->d_dtex = up_ptr(r, r->d_dtex);
    r->d_estate = up_ptr(r, r->d_estate);
    r->d_lm = up_ptr(r, r->d_lm);
    r->d_crack = up_ptr(r, r->d_crack);
    /* the buffers first (an allocation waits for the device), then the
     * kernels: the section quads' order and count on one side stream, the
     * sky's and the recorded units' count on another, the entity quads
     * made and counted on the render's stream, then the clipped units */
    if (gen) dgrow(&r->d_equad_res, &r->equad_res_cap, nq_all + 1);
    r->d_equad = gen ? r->d_equad_res : up_ptr(r, d_qsrc);
    r->d_portal = up_ptr(r, r->d_portal);
    r->d_line = up_ptr(r, r->d_line);
    d_ub = up_ptr(r, d_ub);
    if (!r->wjobs.empty()) {
        uint32_t nd = 0;
        for (const wjob &j : r->wjobs) nd += D[j.draw].nquads;
        dgrow(&r->d_wdist, &r->wdist_cap, (size_t)nd + 1);
    }
    if ((size_t)units + 1 > r->units_cap) {
        if (r->d_counts) CK(cudaFree(r->d_counts));
        if (r->d_offsets) CK(cudaFree(r->d_offsets));
        if (r->d_clip_list) CK(cudaFree(r->d_clip_list));
        r->units_cap = (size_t)units + units / 2 + 1024;
        CK(cudaMalloc(&r->d_counts, r->units_cap * sizeof(uint32_t)));
        CK(cudaMalloc(&r->d_offsets, r->units_cap * sizeof(uint32_t)));
        CK(cudaMalloc(&r->d_clip_list, r->units_cap * sizeof(xunit)));
    }
    recs R{r->d_equad, r->d_eextra, r->d_estate, r->d_crack, r->d_portal, r->d_line};
    uint32_t nx = (uint32_t)X.size(), ne = (uint32_t)XE.size();
    CK(cudaEventRecord(r->ev[1], r->st));
    nvtxRangePop();
    nvtxRangePushA("launch count");

    /* every unit's triangle count, and its place */
    CK(cudaMemsetAsync(r->d_counts, 0, ((size_t)units + 1) * sizeof(uint32_t), r->st));
    CK(cudaMemsetAsync(r->d_nclip, 0, sizeof(uint32_t), r->st));
    fork_aux(r, 2);
    /* the translucent sections the device meshed, ordered there, then the
     * section quads */
    if (!r->wjobs.empty())
        water_order<<<(unsigned)r->wjobs.size(), 256, 0, r->aux[0]>>>(r->d_env, r->d_draws, r->d_wjobs, r->d_mesh,
                                                                        r->d_orders, r->d_wdist);
    if (!D.empty())
        quads<false><<<(unsigned)D.size(), 256, 0, r->aux[0]>>>(r->d_env, r->d_draws, 0, r->d_mesh, r->d_orders,
                                                                  r->w, r->h, r->d_counts, r->d_clip_list, r->d_nclip, NULL, 0, NULL, NULL);
    if (nx)
        generic<false><<<(nx + 31) / 32, 32, 0, r->aux[1]>>>(r->d_env, n, r->d_xlist, nx, NULL, 0, units, r->d_draws, r->d_mesh,
                                                                   r->d_orders, r->d_stars, R, r->w, r->h, r->d_counts, NULL, 0, NULL, NULL);
    if (gen) {
        /* the render's quads on the device: the packed ones placed, the
         * boxes' and the items' made */
        if (nq_host)
            equad_place<<<(unsigned)((nq_host + 255) / 256), 256, 0, r->st>>>(up_ptr(r, d_qsrc), up_ptr(r, d_qdst),
                                                                               (uint32_t)nq_host, r->d_equad_res);
        if (nbox)
            ebox<<<(unsigned)((6 * nbox + 127) / 128), 128, 0, r->st>>>(up_ptr(r, d_box), up_ptr(r, d_bex), up_ptr(r, d_xf),
                                                                      (uint32_t)nbox, r->d_equad_res);
        if (nitem)
            eitem<<<(unsigned)nitem, 128, 0, r->st>>>(up_ptr(r, d_item), up_ptr(r, d_iex), up_ptr(r, d_xf),
                                                       (uint32_t)nitem, r->d_equad_res);
    }
    patch_atlases(r);
    if (ne)
        equads<false><<<(ne + 127) / 128, 128, 0, r->st>>>(r->d_env, n, r->d_elist, ne, 0, units, R, r->w, r->h,
                                                              r->d_counts, r->d_clip_list, r->d_nclip, NULL, 0, NULL, NULL);
    join_aux(r, 0);
    /* the clipped units (at most every section and entity quad), their
     * number read on the device */
    uint32_t clip_bound = ne;
    for (const ddraw &dd : D) clip_bound += dd.nquads;
    if (clip_bound)
        generic<false><<<std::min((clip_bound + 31) / 32, (uint32_t)GEN_GRID), 32, 0, r->st>>>(r->d_env, n, r->d_clip_list, clip_bound, r->d_nclip, 0, units,
                                                                       r->d_draws, r->d_mesh, r->d_orders, r->d_stars, R, r->w, r->h,
                                                                       r->d_counts, NULL, 0, NULL, NULL);
    join_aux(r, 1);
    CK(cudaGetLastError());
    size_t tb = 0;
    rs_exclusive_sum(NULL, tb, r->d_counts, r->d_offsets, units + 1, r->st);
    temp_need(r, tb);
    rs_exclusive_sum(r->d_temp, tb, r->d_counts, r->d_offsets, units + 1, r->st);
    nvtxRangePop();
    if (r->one_trip && one_trip(r, n, P, D, units, nx, ne, clip_bound, R)) {
        for (int e = 0; e < n; ++e) r->obs[r->bslot[(size_t)e]] = NULL;
        return 0;
    }
    /* each environment's first triangle and the clipped units' number, in
     * one copy back */
    std::vector<uint32_t> env_tri0((size_t)n + 1);
    uint32_t nclip = 0;
    {
        gather<<<(n + 1 + 127) / 128, 128, 0, r->st>>>(r->d_offsets, d_ub, (uint32_t)n + 1, r->d_gout);
        CK(cudaMemcpyAsync(r->h_rb, r->d_gout, ((size_t)n + 1) * sizeof(uint32_t), cudaMemcpyDeviceToHost, r->st));
        CK(cudaMemcpyAsync(r->h_rb + n + 1, r->d_nclip, sizeof(uint32_t), cudaMemcpyDeviceToHost, r->st));
        CK(cudaEventRecord(r->ev[2], r->st));
        nvtxRangePushA("read back offsets");
        CK(cudaStreamSynchronize(r->st));
        nvtxRangePop();
        for (int e = 0; e <= n; ++e) env_tri0[(size_t)e] = r->h_rb[e];
        nclip = r->h_rb[n + 1];
        for (int e = 0; e < n; ++e)
            r->trip_maxt = std::max(r->trip_maxt, env_tri0[(size_t)e + 1] - env_tri0[(size_t)e]);
    }
    r->stats.units = units;
    r->stats.sky_units = nx + ne;
    r->stats.clip_units = nclip;
    r->stats.triangles = env_tri0[(size_t)n];
    float t_units = ms(r->ev[1], r->ev[2]), t_bin = 0, t_raster = 0;

    /* the environments in groups whose triangles fit the arena (one, when
     * it can grow to take them all) */
    arena_fit(r, env_tri0[(size_t)n], 0);
    for (int e0 = 0; e0 < n;) {
        int e1 = e0 + 1;
        while (e1 < n && env_tri0[(size_t)e1 + 1] - env_tri0[(size_t)e0] <= r->tris_cap) ++e1;
        uint32_t tri0 = env_tri0[(size_t)e0], ntris = env_tri0[(size_t)e1] - tri0;
        if (ntris > r->tris_cap) { fprintf(stderr, "render: one frame has %u triangles, over the arena's %zu\n", ntris, r->tris_cap); CK_FAIL; }
        /* the group's timing events, read once the render is done */
        size_t g4 = 4 * (size_t)r->stats.groups;
        while (r->gev.size() < g4 + 4) {
            cudaEvent_t ev;
            CK(cudaEventCreate(&ev));
            r->gev.push_back(ev);
        }
        cudaEvent_t *gev = &r->gev[g4];
        CK(cudaEventRecord(gev[0], r->st));
        uint32_t u0 = P[(size_t)e0].unit_base, u1 = e1 < n ? P[(size_t)e1].unit_base : units;
        uint32_t d0 = P[(size_t)e0].draw_base, d1 = e1 < n ? P[(size_t)e1].draw_base : (uint32_t)D.size();
        {
            uint32_t clip_n = nclip;
            WRITE_PASS(d1 - d0, d0, nx, ne, gen_wgrid(r, nclip), (const uint32_t *)NULL, u0, u1, tri0);
        }
        CK(cudaGetLastError());
        CK(cudaEventRecord(gev[1], r->st));
        /* the pairs */
        uint32_t *g0 = (uint32_t *)stage(r, (size_t)(e1 - e0) * sizeof(uint32_t));
        for (int e = e0; e < e1; ++e) g0[e - e0] = env_tri0[(size_t)e] - tri0;
        CK(cudaMemcpyAsync(r->d_env_tri0, g0, (size_t)(e1 - e0) * sizeof(uint32_t), cudaMemcpyHostToDevice, r->st));
        if (ntris) tri_envs<<<(ntris + 255) / 256, 256, 0, r->st>>>(r->d_env_tri0, e1 - e0, ntris, r->d_tri_env);
        CK(cudaMemsetAsync(r->d_tiles + ntris, 0, sizeof(uint32_t), r->st));
        tb = 0;
        rs_exclusive_sum(NULL, tb, r->d_tiles, r->d_pair_off, ntris + 1, r->st);
        temp_need(r, tb);
        rs_exclusive_sum(r->d_temp, tb, r->d_tiles, r->d_pair_off, ntris + 1, r->st);
        /* the pairs each environment of the group ends at: a group whose
         * pairs overflow keeps the environments that fit (their triangles
         * and pairs are a prefix of the group's) */
        std::vector<uint32_t> pend((size_t)(e1 - e0));
        {
            uint32_t *gi = (uint32_t *)stage(r, (size_t)(e1 - e0) * sizeof(uint32_t));
            for (int e = e0 + 1; e <= e1; ++e) gi[e - e0 - 1] = env_tri0[(size_t)e] - tri0;
            CK(cudaMemcpyAsync(r->d_gidx, gi, (size_t)(e1 - e0) * sizeof(uint32_t), cudaMemcpyHostToDevice, r->st));
            gather<<<(e1 - e0 + 127) / 128, 128, 0, r->st>>>(r->d_pair_off, r->d_gidx, (uint32_t)(e1 - e0), r->d_gout);
            CK(cudaMemcpyAsync(r->h_rb, r->d_gout, (size_t)(e1 - e0) * sizeof(uint32_t), cudaMemcpyDeviceToHost, r->st));
            nvtxRangePushA("read back pairs");
            CK(cudaStreamSynchronize(r->st));
            nvtxRangePop();
            for (int e = 0; e < e1 - e0; ++e) {
                pend[(size_t)e] = r->h_rb[e];
                r->trip_maxp = std::max(r->trip_maxp, pend[(size_t)e] - (e ? pend[(size_t)e - 1] : 0));
            }
        }
        int fit = 0;
        arena_fit(r, 0, pend[(size_t)(e1 - e0 - 1)]);
        while (fit < e1 - e0 && pend[(size_t)fit] <= r->pairs_cap) ++fit;
        if (!fit) { fprintf(stderr, "render: one frame has %u tile pairs, over the arena's %zu\n", pend[0], r->pairs_cap); CK_FAIL; }
        e1 = e0 + fit;
        ntris = env_tri0[(size_t)e1] - tri0;
        uint32_t npairs = pend[(size_t)fit - 1];
        trip_learn(r, fit, ntris, npairs);
        int nenv = e1 - e0;
        uint32_t nkeys = (uint32_t)nenv * r->ntiles;
        if (ntris)
            pairs<<<(ntris + 255) / 256, 256, 0, r->st>>>(r->d_tris, ntris, r->d_tri_env, r->d_pair_off,
                                                           r->tiles_x, r->ntiles, nkeys, r->d_keys, r->d_vals);
        CK(cudaGetLastError());
        int bits = 1;
        while ((1u << bits) <= nkeys) ++bits;   /* the sentinel too */
        tb = 0;
        rs_sort_pairs(NULL, tb, r->d_keys, r->d_keys2, r->d_vals, r->d_vals2, npairs, 0, bits, r->st);
        temp_need(r, tb);
        if (npairs)
            rs_sort_pairs(r->d_temp, tb, r->d_keys, r->d_keys2, r->d_vals, r->d_vals2, npairs, 0, bits, r->st);
        CK(cudaMemsetAsync(r->d_key_start, 0, (nkeys + 1) * sizeof(uint32_t), r->st));
        CK(cudaMemsetAsync(r->d_key_n, 0, (nkeys + 1) * sizeof(uint32_t), r->st));
        if (npairs) key_runs<<<(npairs + 255) / 256, 256, 0, r->st>>>(r->d_keys2, npairs, NULL, r->d_key_start, r->d_key_n);
        CK(cudaEventRecord(gev[2], r->st));
        texes Xt;
        memset(&Xt, 0, sizeof Xt);
        Xt.aw = r->aw; Xt.ah = r->ah;
        Xt.sun = r->d_sun; Xt.moon = r->d_moon; Xt.clouds = r->d_clouds; Xt.end_sky = r->d_end_sky;
        Xt.pool = r->d_pool; Xt.tex = r->d_dtex; Xt.estate = r->d_estate; Xt.lm = r->d_lm; Xt.crack = r->d_crack;
        Xt.portal = r->d_portal;
        Xt.line = r->d_line;
        raster_launch(r, nkeys, e0, Xt);
        CK(cudaGetLastError());
        CK(cudaEventRecord(gev[3], r->st));
        CK(cudaEventRecord(r->ev[5], r->st));
        /* the next group reuses the arena (stream order keeps it: the
         * group's wait is its pairs' copy back); the last one's frames go
         * straight on to the host copies asked for (one wait in all) */
        if (e1 >= n) {
            if (r->h_frames) CK(cudaMemcpyAsync(r->h_frames, r->d_out, (size_t)n * r->w * r->h * 3, cudaMemcpyDeviceToHost, r->st));
            if (r->h_depths)
                CK(cudaMemcpyAsync(r->h_depths, r->d_depth, (size_t)n * r->w * r->h * sizeof(float), cudaMemcpyDeviceToHost, r->st));
            nvtxRangePushA("sync");
            CK(cudaStreamSynchronize(r->st));
            nvtxRangePop();
        }
        r->stats.pairs += npairs;
        ++r->stats.groups;
        e0 = e1;
    }
    if (!r->stats.groups) {
        CK(cudaEventRecord(r->ev[5], r->st));
        CK(cudaEventSynchronize(r->ev[5]));
    }
    for (int g = 0; g < r->stats.groups; ++g) {
        const cudaEvent_t *gev = &r->gev[4 * (size_t)g];
        t_units += ms(gev[0], gev[1]);
        t_bin += ms(gev[1], gev[2]);
        t_raster += ms(gev[2], gev[3]);
    }
    r->stats.upload_ms = ms(r->ev[0], r->ev[1]);
    r->stats.units_ms = t_units;
    r->stats.bin_ms = t_bin;
    r->stats.raster_ms = t_raster;
    r->stats.total_ms = ms(r->ev[0], r->ev[5]);
    for (int e = 0; e < n; ++e) r->obs[r->bslot[(size_t)e]] = NULL;
    return 0;
}

extern "C" int render_device(int dev)
{
    /* a thread waiting on the device sleeps rather than spins: the host's
     * other threads step the worlds. The flag takes only before the
     * device's primary context exists (cudaSetDevice makes it), so it is
     * set on the context through the driver first; refused once the
     * context is active (a later call), which is fine. */
    CUdevice cd;
    if (cuInit(0) == CUDA_SUCCESS && cuDeviceGet(&cd, dev) == CUDA_SUCCESS)
        cuDevicePrimaryCtxSetFlags(cd, CU_CTX_SCHED_BLOCKING_SYNC);
    if (cudaSetDevice(dev) != cudaSuccess) return -1;
    return 0;
}

extern "C" void *render_dev_alloc(size_t bytes)
{
    void *p = NULL;
    return cudaMalloc(&p, bytes) == cudaSuccess ? p : NULL;
}

extern "C" void render_dev_free(void *p) { if (p) cudaFree(p); }

extern "C" int render_dev_download(void *host, const void *dev, size_t bytes)
{
    return cudaMemcpy(host, dev, bytes, cudaMemcpyDeviceToHost) == cudaSuccess ? 0 : -1;
}

extern "C" void render_range(const char *name, int push)
{
    if (push) nvtxRangePushA(name);
    else nvtxRangePop();
}

extern "C" int render_dev_mem(size_t *free_bytes, size_t *total_bytes)
{
    return cudaMemGetInfo(free_bytes, total_bytes) == cudaSuccess ? 0 : -1;
}

extern "C" int render_download(struct render *r, int env, unsigned char *rgb)
{
    if (env < 0 || env >= r->max_envs) return -1;
    size_t n = (size_t)r->w * r->h * 3;
    CK(cudaMemcpy(rgb, r->d_out + (size_t)env * n, n, cudaMemcpyDeviceToHost));
    return 0;
}

extern "C" int render_download_depth(struct render *r, int env, float *depth)
{
    if (env < 0 || env >= r->max_envs) return -1;
    size_t n = (size_t)r->w * r->h;
    CK(cudaMemcpy(depth, r->d_depth + (size_t)env * n, n * sizeof(float), cudaMemcpyDeviceToHost));
    return 0;
}
