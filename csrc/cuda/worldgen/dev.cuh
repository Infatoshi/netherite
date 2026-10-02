/* What the CUDA generator's translation units share: the device tables, the
 * per-seed state, java.util.Random and MathHelper, the layer graph, and the
 * kernels' prototypes. The units are compiled one at a time with -rdc=true and
 * device-linked (csrc/Makefile), so no one unit carries the whole pipeline:
 * as one file cicc peaked at 8.8 GB.
 *
 *   host.cu     the tables' definitions, the host API, tables, construct
 *   seeds.cu    gen_seeds (chunkgen_init's state per world seed)
 *   layers.cu   layers (the GenLayer graph)
 *   density.cu  density (the density field, the fill, the surface noise)
 *   surface.cu  surface (func_147422_a)
 *   carve.cu    the caves and ravines (carve.cuh's phases), the carvers' work area
 *   built.cu    built (the Chunk constructor's bands and generateSkylightMap over them)
 *
 * The Nether and the End (dim_*.cu) reuse the tables, octaves_at and
 * improved_init, and add four units of their own:
 *   dim_host.cu     their host API, dim_seeds, dim_construct
 *   dim_density.cu  dim_density (both density fields and fills, the Nether's surface noise)
 *   dim_surface.cu  dim_surface (func_147418_b, func_147421_b)
 *   dim_caves.cu    MapGenCavesHell (carve.cuh's phases) */
#ifndef NETHERITE_WORLDGEN_DEV_CUH
#define NETHERITE_WORLDGEN_DEV_CUH

#include "worldgen.h"

#include <cuda_runtime.h>
#include <limits.h>
#include <stdint.h>

#define CELLS WORLDGEN_CELLS
#define NODES 44

/* the enums of csrc/engine/biomes.h and terrain.h the kernels read */
enum { SURF_GENERIC, SURF_HILLS, SURF_TAIGA, SURF_SAVANNA_M, SURF_SWAMP, SURF_MESA, SURF_MUTATED };
enum { TEMP_OCEAN, TEMP_COLD, TEMP_MEDIUM, TEMP_WARM };
enum { CLS_JUNGLE = 14 };
enum {
    BLK_AIR = 0, BLK_STONE = 1, BLK_GRASS = 2, BLK_DIRT = 3, BLK_BEDROCK = 7, BLK_FLOWING_WATER = 8, BLK_WATER = 9,
    BLK_FLOWING_LAVA = 10, BLK_LAVA = 11, BLK_SAND = 12, BLK_GRAVEL = 13, BLK_SANDSTONE = 24, BLK_ICE = 79,
    BLK_STAINED_CLAY = 159, BLK_HARDENED_CLAY = 172,
};

/* ---------------------------------------------------------------- tables (host.cu) */

extern __device__ struct worldgen_biome d_biomes[256];
extern __device__ float d_sin[65536];      /* MathHelper.SIN_TABLE */
extern __device__ float d_parabolic[25];   /* ChunkProviderGenerate.parabolicField */
extern __device__ unsigned d_err;
extern __constant__ int c_negative;

/* ---------------------------------------------------------------- java.util.Random */

#define JR_MASK ((1ULL << 48) - 1)

struct jrand { uint64_t seed; };

__device__ static inline void jr_seed(jrand *r, int64_t s)
{
    r->seed = ((uint64_t)s ^ 0x5DEECE66DULL) & JR_MASK;
}

__device__ static inline int32_t jr_next(jrand *r, int bits)
{
    r->seed = (r->seed * 0x5DEECE66DULL + 0xBULL) & JR_MASK;
    return (int32_t)(uint32_t)(r->seed >> (48 - bits));
}

__device__ static inline int32_t jr_int_n(jrand *r, int32_t n)
{
    if ((n & -n) == n) return (int32_t)(((int64_t)n * (int64_t)jr_next(r, 31)) >> 31);
    int32_t bits, val;
    do
    {
        bits = jr_next(r, 31);
        val = bits % n;
    } while ((int32_t)((uint32_t)bits - (uint32_t)val + (uint32_t)(n - 1)) < 0);
    return val;
}

__device__ static inline int64_t jr_long(jrand *r)
{
    uint64_t hi = (uint64_t)(int64_t)jr_next(r, 32);
    uint64_t lo = (uint64_t)(int64_t)jr_next(r, 32);
    return (int64_t)((hi << 32) + lo);
}

__device__ static inline double jr_double(jrand *r)
{
    int64_t hi = jr_next(r, 26), lo = jr_next(r, 27);
    return (double)((hi << 27) + lo) * (1.0 / 9007199254740992.0);
}

__device__ static inline float jr_float(jrand *r)
{
    return (float)jr_next(r, 24) / (float)(1 << 24);
}

__device__ static inline int jr_bool(jrand *r)
{
    return jr_next(r, 1) != 0;
}

/* ---------------------------------------------------------------- MathHelper */

__device__ static inline int32_t j_f2i(float f)
{
    if (f != f) return 0;
    if (f >= 2147483647.0f) return INT32_MAX;
    if (f <= -2147483648.0f) return INT32_MIN;
    return (int32_t)f;
}

__device__ static inline float mh_sin(float f)
{
    return d_sin[j_f2i(f * 10430.378f) & 65535];
}

__device__ static inline float mh_cos(float f)
{
    return d_sin[j_f2i(f * 10430.378f + 16384.0f) & 65535];
}

__device__ static inline int mh_floor(double d)
{
    int i = (int)d;
    return d < (double)i ? i - 1 : i;
}

/* ---------------------------------------------------------------- per-seed state */

struct g_improved { double xo, yo, zo; uint8_t p[512]; };
struct g_simplex { double xo, yo, zo; uint8_t p[512]; };

struct cg_seed {
    int64_t seed;
    struct g_improved min_limit[16], max_limit[16], main[8], depth[16];
    struct g_simplex stone[4];
    struct g_simplex band_shift, spire[4], spire_cap;
    uint8_t bands[64];
    int64_t ws[NODES];          /* each layer node's world seed */
    int64_t carve_kx, carve_kz; /* MapGenBase's two placement longs */
};

/* ---------------------------------------------------------------- the layer graph */

enum layer_kind {
    L_ISLAND, L_ZOOM, L_FUZZY_ZOOM, L_ADD_ISLAND, L_REMOVE_OCEAN, L_ADD_SNOW,
    L_EDGE_COOL_WARM, L_EDGE_HEAT_ICE, L_EDGE_SPECIAL, L_ADD_MUSHROOM, L_DEEP_OCEAN,
    L_RIVER_INIT, L_BIOME, L_BIOME_EDGE, L_HILLS, L_RIVER, L_SMOOTH, L_RARE_BIOME,
    L_SHORE, L_RIVER_MIX, L_VORONOI,
};

struct lnode { int8_t kind, p1, p2, seeded; int32_t arg; };

/* layers_init for WorldType.DEFAULT, node for node in its mk() order: kind,
 * parent, second input, whether initWorldGenSeed reaches it (the two zooms that
 * feed hills' river noise keep world seed 0), and the base seed argument. */
#define LAYER_GRAPH { \
    {L_ISLAND, -1, -1, 1, 1},         {L_FUZZY_ZOOM, 0, -1, 1, 2000},   {L_ADD_ISLAND, 1, -1, 1, 1}, \
    {L_ZOOM, 2, -1, 1, 2001},         {L_ADD_ISLAND, 3, -1, 1, 2},      {L_ADD_ISLAND, 4, -1, 1, 50}, \
    {L_ADD_ISLAND, 5, -1, 1, 70},     {L_REMOVE_OCEAN, 6, -1, 1, 2},    {L_ADD_SNOW, 7, -1, 1, 2}, \
    {L_ADD_ISLAND, 8, -1, 1, 3},      {L_EDGE_COOL_WARM, 9, -1, 1, 2},  {L_EDGE_HEAT_ICE, 10, -1, 1, 2}, \
    {L_EDGE_SPECIAL, 11, -1, 1, 3},   {L_ZOOM, 12, -1, 1, 2002},        {L_ZOOM, 13, -1, 1, 2003}, \
    {L_ADD_ISLAND, 14, -1, 1, 4},     {L_ADD_MUSHROOM, 15, -1, 1, 5},   {L_DEEP_OCEAN, 16, -1, 1, 4}, \
    {L_RIVER_INIT, 17, -1, 1, 100},   {L_BIOME, 17, -1, 1, 200},        {L_ZOOM, 19, -1, 1, 1000}, \
    {L_ZOOM, 20, -1, 1, 1001},        {L_BIOME_EDGE, 21, -1, 1, 1000},  {L_ZOOM, 18, -1, 0, 1000}, \
    {L_ZOOM, 23, -1, 0, 1001},        {L_HILLS, 22, 24, 1, 1000},       {L_ZOOM, 18, -1, 1, 1000}, \
    {L_ZOOM, 26, -1, 1, 1001},        {L_ZOOM, 27, -1, 1, 1000},        {L_ZOOM, 28, -1, 1, 1001}, \
    {L_ZOOM, 29, -1, 1, 1002},        {L_ZOOM, 30, -1, 1, 1003},        {L_RIVER, 31, -1, 1, 1}, \
    {L_SMOOTH, 32, -1, 1, 1000},      {L_RARE_BIOME, 25, -1, 1, 1001},  {L_ZOOM, 34, -1, 1, 1000}, \
    {L_ADD_ISLAND, 35, -1, 1, 3},     {L_ZOOM, 36, -1, 1, 1001},        {L_SHORE, 37, -1, 1, 1000}, \
    {L_ZOOM, 38, -1, 1, 1002},        {L_ZOOM, 39, -1, 1, 1003},        {L_SMOOTH, 40, -1, 1, 1000}, \
    {L_RIVER_MIX, 41, 33, 1, 100},    {L_VORONOI, 42, -1, 1, 10}, \
}
#define NODE_MIX 42
#define NODE_VORONOI 43

extern __constant__ struct lnode c_graph[NODES];
/* each node's area capacity and its offset in the layer kernel's shared memory */
extern __constant__ int c_off[NODES], c_maxw[NODES], c_maxh[NODES];

__host__ __device__ static inline int64_t lcg(int64_t s, int64_t add)
{
    uint64_t u = (uint64_t)s;
    u = u * (u * 6364136223846793005ULL + 1442695040888963407ULL);
    return (int64_t)(u + (uint64_t)add);
}

__host__ __device__ static inline void box_demand(int *b, int x0, int z0, int x1, int z1)
{
    if (x0 < b[0]) b[0] = x0;
    if (z0 < b[1]) b[1] = z0;
    if (x1 > b[2]) b[2] = x1;
    if (z1 > b[3]) b[3] = z1;
}

/* Top down from the two areas provideChunk asks for (the generation biomes at
 * (cx*4-2, cz*4-2, 10, 10) and the block biomes at (cx*16, cz*16, 16, 16)),
 * each node's half-open box [x0, x1) x [z0, z1) is the union of the cells its
 * consumers read. Parents always precede their consumers in the graph. */
__host__ __device__ static inline void layer_boxes(const struct lnode *graph, int cx, int cz, int (*b)[4])
{
    for (int n = 0; n < NODES; ++n)
    {
        b[n][0] = INT_MAX;
        b[n][1] = INT_MAX;
        b[n][2] = INT_MIN;
        b[n][3] = INT_MIN;
    }
    box_demand(b[NODE_MIX], cx * 4 - 2, cz * 4 - 2, cx * 4 + 8, cz * 4 + 8);
    box_demand(b[NODE_VORONOI], cx * 16, cz * 16, cx * 16 + 16, cz * 16 + 16);
    for (int n = NODES - 1; n >= 0; --n)
    {
        int x0 = b[n][0], z0 = b[n][1], x1 = b[n][2], z1 = b[n][3];
        if (x0 >= x1) continue;
        const struct lnode *l = &graph[n];
        switch (l->kind)
        {
        case L_ISLAND:
            break;
        case L_ZOOM:
        case L_FUZZY_ZOOM:
            box_demand(b[l->p1], x0 >> 1, z0 >> 1, ((x1 - 1) >> 1) + 2, ((z1 - 1) >> 1) + 2);
            break;
        case L_VORONOI:
            box_demand(b[l->p1], (x0 - 2) >> 2, (z0 - 2) >> 2, ((x1 - 3) >> 2) + 2, ((z1 - 3) >> 2) + 2);
            break;
        case L_EDGE_SPECIAL:
        case L_RIVER_INIT:
        case L_BIOME:
            box_demand(b[l->p1], x0, z0, x1, z1);
            break;
        case L_RIVER_MIX:
            box_demand(b[l->p1], x0, z0, x1, z1);
            box_demand(b[l->p2], x0, z0, x1, z1);
            break;
        case L_HILLS:
            box_demand(b[l->p1], x0 - 1, z0 - 1, x1 + 1, z1 + 1);
            box_demand(b[l->p2], x0, z0, x1, z1);
            break;
        default:  /* the 3x3 neighbourhood layers */
            box_demand(b[l->p1], x0 - 1, z0 - 1, x1 + 1, z1 + 1);
            break;
        }
    }
}

/* ---------------------------------------------------------------- simplex noise (density, surface) */

__device__ static inline int sfloor(double d)
{
    return d > 0.0 ? (int)d : (int)d - 1;
}

__device__ static inline double sdot(int g, double x, double y)
{
    /* NoiseGeneratorSimplex.grad3's first two columns */
    const int gx = g < 4 ? ((g & 1) ? -1 : 1) : g < 8 ? ((g & 1) ? -1 : 1) : 0;
    const int gy = g < 4 ? ((g & 2) ? -1 : 1) : g < 8 ? 0 : ((g & 1) ? -1 : 1);
    return (double)gx * x + (double)gy * y;
}

/* NoiseGeneratorSimplex at (xin, yin): n0 + n1 + n2, before the * 70 */
__device__ static __noinline__ double simplex_sum(const struct g_simplex *s, double xin, double yin)
{
    const double F2 = 0.5 * (sqrt(3.0) - 1.0);
    const double G2 = (3.0 - sqrt(3.0)) / 6.0;
    const uint8_t *p = s->p;
    double t = (xin + yin) * F2;
    int ii = sfloor(xin + t), jj = sfloor(yin + t);
    double u = (double)(ii + jj) * G2;
    double X0 = (double)ii - u, Y0 = (double)jj - u;
    double x = xin - X0, y = yin - Y0;
    int i1, j1;
    if (x > y) { i1 = 1; j1 = 0; }
    else { i1 = 0; j1 = 1; }
    double x1 = x - (double)i1 + G2, y1 = y - (double)j1 + G2;
    double x2 = x - 1.0 + 2.0 * G2, y2 = y - 1.0 + 2.0 * G2;
    int a = ii & 255, b = jj & 255;
    int g0 = p[a + p[b]] % 12;
    int g1 = p[a + i1 + p[b + j1]] % 12;
    int g2 = p[a + 1 + p[b + 1]] % 12;
    double n0, n1, n2;
    double t0 = 0.5 - x * x - y * y;
    if (t0 < 0.0) n0 = 0.0;
    else { t0 *= t0; n0 = t0 * t0 * sdot(g0, x, y); }
    double t1 = 0.5 - x1 * x1 - y1 * y1;
    if (t1 < 0.0) n1 = 0.0;
    else { t1 *= t1; n1 = t1 * t1 * sdot(g1, x1, y1); }
    double t2 = 0.5 - x2 * x2 - y2 * y2;
    if (t2 < 0.0) n2 = 0.0;
    else { t2 *= t2; n2 = t2 * t2 * sdot(g2, x2, y2); }
    return n0 + n1 + n2;
}

/* ---------------------------------------------------------------- the Nether and the End */

/* one world seed's ChunkProviderHell and ChunkProviderEnd generators. Each
 * provider also builds noise it never reads (the Nether's netherNoiseGen6 and
 * 7 feed only locals vanilla discards, the End's noiseGen5 likewise); they are
 * the last draws of the construction, so they are left out. */
struct cd_seed {
    int64_t seed;
    struct g_improved n_lo[16], n_hi[16], n_main[8], n_sand[4], n_excl[4];
    struct g_improved e_lo[16], e_hi[16], e_main[8], e_island[10];
    int64_t carve_kx, carve_kz;
};

enum { BLK_NETHERRACK = 87, BLK_SOUL_SAND = 88, BLK_END_STONE = 121, BIOME_HELL = 8, BIOME_SKY = 9 };

extern __constant__ int c_dneg;          /* the dimension pipeline's worldgen_dim_negative */
extern __constant__ double c_curve[17];  /* the Nether's height curve, host libm cos (dim_host.cu) */

__device__ double octaves_at(const struct g_improved *g, int oct, int x, int y, int z, int i, int m, int j, int ys,
                             double sx, double sy, double sz);
__device__ void improved_init(struct g_improved *n, jrand *r);

/* ---------------------------------------------------------------- the carvers' two phases (carve.cuh) */

#define CV_RANGE 8
#define CV_SIDE (2 * CV_RANGE + 1)
#define CV_SRC (CV_SIDE * CV_SIDE)   /* 289 source chunks per target */

/* A top tunnel as its source lists it (the walk's inputs), and what the walk
 * made of it: one segment of spheres, or three when it branched (its own
 * steps up to the branch, then the two children's). */
struct cv_tun {
    int64_t seed;
    double x, y, z, yscale;
    float width, yaw, pitch;
    int step, steps;
    int seg_base[3], seg_n[3];
    double seg_reach[3];      /* (double)(width + 2.0f + 16.0f), the segment's own width */
    int nseg, room, prof;     /* prof: the ravine's height profile (cv_work.prof + prof * 256) */
};

/* A tunnel's reach: every sphere's x - 2w .. x + 2w and z - 2w .. z + 2w (a
 * target no sphere's box test can pass skips the tunnel). Apart from struct
 * cv_tun: nvcc 13.3's cicc crashes on carve.cu when these four doubles
 * are cv_tun's. */
struct cv_box { double lo_x, hi_x, lo_z, hi_z; };

/* A step that reaches the target tests: its centre and radii, and the steps
 * left (steps - step) for the reach test. */
struct cv_sph {
    double x, y, z, w, h;
    int left;
};

struct cv_counts {
    int nuniq, ntun, nsph, nprof;
    unsigned err;
};

/* The work area, device pointers (cv_work_alloc in carve.cu). */
struct cv_work {
    int max_batch, ht_bits, cap_uniq, cap_tun, cap_sph, cap_prof;
    unsigned long long *ht_key;
    int *ht_uid;
    int *tgt_slot;             /* max_batch * CV_SRC: each target's sources' table slots */
    unsigned long long *uniq_key;
    int *src_first, *src_n;
    struct cv_tun *tun;
    struct cv_box *box;         /* each tunnel's */
    struct cv_sph *sph;
    float *prof;
    struct cv_counts *cnt;
};

int cv_work_alloc(struct cv_work *w, int max_batch);
void cv_work_free(struct cv_work *w);
/* the stages' work areas: each run starts them empty */
int cv_reset(struct cv_work *w, cudaStream_t s);

/* the overworld's caves (ravines 0) or ravines (1), and the Nether's caves, over a batch */
int worldgen_carve_run(struct cv_work *w, int ravines, const struct cg_seed *seeds, const struct worldgen_req *req, int n,
                    const uint8_t *full256, const uint8_t *tops_all, uint8_t *ids_all, cudaStream_t s);
int worldgen_dim_carve_run(struct cv_work *w, const struct cd_seed *seeds, const struct worldgen_dim_req *req, int n, uint8_t *raw_all,
                    cudaStream_t s);

/* ---------------------------------------------------------------- kernels */

__global__ void tables(void);
__global__ void gen_seeds(struct cg_seed *seeds, const int64_t *values, int n);
__global__ void layers(const struct cg_seed *seeds, const struct worldgen_req *req, int n, uint8_t *gen100,
                         uint8_t *full256);
__global__ void density(const struct cg_seed *seeds, const struct worldgen_req *req, int n, const uint8_t *gen100,
                          double *field_out, double *stone_out, uint8_t *ids_out);
__global__ void surface(const struct cg_seed *seeds, const struct worldgen_req *req, int n, const uint8_t *full256,
                          const double *stone, uint8_t *ids_all, uint8_t *metas_all, uint8_t *tops_all,
                          uint64_t *rand_all);
__global__ void construct(const uint8_t *ids, uint8_t *metas, size_t cells);
extern __constant__ uint8_t c_opacity[256], c_tick[256];   /* built.cu's: worldgen_block_rows */
__global__ void built(const uint8_t *ids_all, const uint8_t *metas_all, int n, struct worldgen_built *hdr_all,
                      uint8_t *bands_all);

__global__ void dim_seeds(struct cd_seed *seeds, const int64_t *values, int n);
__global__ void dim_density(const struct cd_seed *seeds, const struct worldgen_dim_req *req, int n, double *field_out,
                              double *noise_out, uint8_t *raw_out);
__global__ void dim_surface(const struct worldgen_dim_req *req, int n, const double *noise, uint8_t *raw_all,
                              uint64_t *rand_all);
__global__ void dim_construct(const struct worldgen_dim_req *req, int n, const uint8_t *raw_all, uint8_t *ids_all,
                                uint8_t *biomes_all);

#endif
