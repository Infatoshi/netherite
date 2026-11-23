/* layers: the GenLayer graph of layers.c, a block per chunk. GenLayer is
 * recursive over areas in C, with a malloc per call; here every layer node gets
 * one fixed area per chunk (the union of what its consumers read, layer_boxes),
 * evaluated bottom up in shared memory, one thread per cell. A layer's value at
 * a cell depends only on the seed and the cell, so a larger area than C's gives
 * the same values where C has them. */
#include "dev.cuh"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* the area each node computes for one chunk */
struct lbox { int x0, z0, w, h; };

struct lrng { int64_t cs, ws; };

__device__ static inline void cseed(lrng *r, int64_t ws, int64_t x, int64_t z)
{
    r->ws = ws;
    r->cs = ws;
    r->cs = lcg(r->cs, x);
    r->cs = lcg(r->cs, z);
    r->cs = lcg(r->cs, x);
    r->cs = lcg(r->cs, z);
}

__device__ static inline int next_int(lrng *r, int n)
{
    int v = (int)((r->cs >> 24) % (int64_t)n);
    if (v < 0) v += n;
    r->cs = lcg(r->cs, r->ws);
    return v;
}

__device__ static inline int pick2(lrng *r, int a, int b)
{
    return next_int(r, 2) == 0 ? a : b;
}

__device__ static inline int pick4(lrng *r, int a, int b, int c, int d)
{
    int k = next_int(r, 4);
    return k == 0 ? a : k == 1 ? b : k == 2 ? c : d;
}

__device__ static int mode4(lrng *r, int a, int b, int c, int d)
{
    return b == c && c == d ? b
        : a == b && a == c ? a
        : a == b && a == d ? a
        : a == c && a == d ? a
        : a == b && c != d ? a
        : a == c && b != d ? a
        : a == d && b != c ? a
        : b == c && a != d ? b
        : b == d && a != c ? b
        : c == d && a != b ? c
        : pick4(r, a, b, c, d);
}

enum {
    OCEAN = 0, PLAINS = 1, DESERT = 2, EXTREME_HILLS = 3, FOREST = 4, TAIGA = 5, SWAMPLAND = 6,
    RIVER = 7, FROZEN_OCEAN = 10, FROZEN_RIVER = 11, ICE_PLAINS = 12, ICE_MOUNTAINS = 13,
    MUSHROOM_ISLAND = 14, MUSHROOM_SHORE = 15, BEACH = 16, DESERT_HILLS = 17, FOREST_HILLS = 18,
    TAIGA_HILLS = 19, EXTREME_HILLS_EDGE = 20, JUNGLE = 21, JUNGLE_HILLS = 22, JUNGLE_EDGE = 23,
    DEEP_OCEAN = 24, STONE_BEACH = 25, COLD_BEACH = 26, BIRCH_FOREST = 27, BIRCH_FOREST_HILLS = 28,
    ROOFED_FOREST = 29, COLD_TAIGA = 30, COLD_TAIGA_HILLS = 31, MEGA_TAIGA = 32, MEGA_TAIGA_HILLS = 33,
    EXTREME_HILLS_PLUS = 34, SAVANNA = 35, SAVANNA_PLATEAU = 36, MESA = 37, MESA_PLATEAU_F = 38,
    MESA_PLATEAU = 39,
};

__device__ static inline int exists(int id)
{
    return id < 0 || id > 255 ? 1 : d_biomes[id].exists;
}

__device__ static inline const struct worldgen_biome *def(int id)
{
    return id < 0 || id > 255 ? &d_biomes[OCEAN] : &d_biomes[id];
}

__device__ static int same(int a, int b)
{
    if (a == b) return 1;
    if (a == MESA_PLATEAU_F || a == MESA_PLATEAU) return b == MESA_PLATEAU_F || b == MESA_PLATEAU;
    return exists(a) && exists(b) ? def(a)->cls == def(b)->cls : 0;
}

__device__ static inline int oceanic(int id)
{
    return id == OCEAN || id == DEEP_OCEAN || id == FROZEN_OCEAN;
}

__device__ static int edge_compatible(int a, int b)
{
    if (same(a, b)) return 1;
    if (!exists(a) || !exists(b)) return 0;
    int ta = def(a)->temp_cat, tb = def(b)->temp_cat;
    return ta == tb || ta == TEMP_MEDIUM || tb == TEMP_MEDIUM;
}

__device__ static int jungle_compatible(int id)
{
    return (exists(id) && def(id)->cls == CLS_JUNGLE) || id == JUNGLE_EDGE || id == JUNGLE || id == JUNGLE_HILLS
        || id == FOREST || id == TAIGA || oceanic(id);
}

__device__ static inline double jitter(lrng *r)
{
    double k = c_negative == WORLDGEN_NEG_LAYERS ? 3.5 : 3.6;
    return ((double)next_int(r, 1024) / 1024.0 - 0.5) * k;
}

struct lview { const int *buf; struct lbox b; };

__device__ static inline int at(const lview &v, int x, int z)
{
    return v.buf[(z - v.b.z0) * v.b.w + (x - v.b.x0)];
}

/* One cell (x, z) of node n, from its parents' areas. Each case is the body of
 * the matching function in layers.c for one output cell. */
__device__ static __noinline__ int layer_cell(int n, int64_t ws, int x, int z, const lview &p, const lview &q)
{
    const struct lnode *l = &c_graph[n];
    lrng r;
    switch (l->kind)
    {
    case L_ISLAND:
    {
        cseed(&r, ws, x, z);
        int v = next_int(&r, 10) == 0 ? 1 : 0;
        return x == 0 && z == 0 ? 1 : v;
    }
    case L_ZOOM:
    case L_FUZZY_ZOOM:
    {
        /* the parent cell's four children, drawn in the order C fills them */
        int px = x >> 1, pz = z >> 1;
        int a = at(p, px, pz), b = at(p, px, pz + 1), c = at(p, px + 1, pz), d = at(p, px + 1, pz + 1);
        int sx = x & 1, sz = z & 1;
        if (!sx && !sz) return a;
        cseed(&r, ws, (int64_t)px * 2, (int64_t)pz * 2);
        int v = pick2(&r, a, b);
        if (!sx) return v;
        v = pick2(&r, a, c);
        if (!sz) return v;
        return l->kind == L_FUZZY_ZOOM ? pick4(&r, a, c, b, d) : mode4(&r, a, c, b, d);
    }
    case L_ADD_ISLAND:
    {
        int a = at(p, x - 1, z - 1), b = at(p, x + 1, z - 1), c = at(p, x - 1, z + 1), d = at(p, x + 1, z + 1);
        int v = at(p, x, z);
        cseed(&r, ws, x, z);
        if (v == 0 && (a != 0 || b != 0 || c != 0 || d != 0))
        {
            int k = 1, pick = 1;
            if (a != 0 && next_int(&r, k++) == 0) pick = a;
            if (b != 0 && next_int(&r, k++) == 0) pick = b;
            if (c != 0 && next_int(&r, k++) == 0) pick = c;
            if (d != 0 && next_int(&r, k++) == 0) pick = d;
            if (next_int(&r, 3) == 0) return pick;
            return pick == 4 ? 4 : 0;
        }
        if (v > 0 && (a == 0 || b == 0 || c == 0 || d == 0))
        {
            if (next_int(&r, 5) == 0) return v == 4 ? 4 : 0;
            return v;
        }
        return v;
    }
    case L_REMOVE_OCEAN:
    {
        int nn = at(p, x, z - 1), e = at(p, x + 1, z), w = at(p, x - 1, z), s = at(p, x, z + 1), v = at(p, x, z);
        cseed(&r, ws, x, z);
        if (v == 0 && nn == 0 && e == 0 && w == 0 && s == 0 && next_int(&r, 2) == 0) return 1;
        return v;
    }
    case L_ADD_SNOW:
    {
        int v = at(p, x, z);
        cseed(&r, ws, x, z);
        if (v == 0) return 0;
        int k = next_int(&r, 6);
        return k == 0 ? 4 : k <= 1 ? 3 : 1;
    }
    case L_EDGE_COOL_WARM:
    {
        int v = at(p, x, z);
        if (v == 1)
        {
            int nn = at(p, x, z - 1), e = at(p, x + 1, z), w = at(p, x - 1, z), s = at(p, x, z + 1);
            if (nn == 3 || e == 3 || w == 3 || s == 3 || nn == 4 || e == 4 || w == 4 || s == 4) v = 2;
        }
        return v;
    }
    case L_EDGE_HEAT_ICE:
    {
        int v = at(p, x, z);
        if (v == 4)
        {
            int nn = at(p, x, z - 1), e = at(p, x + 1, z), w = at(p, x - 1, z), s = at(p, x, z + 1);
            if (nn == 2 || e == 2 || w == 2 || s == 2 || nn == 1 || e == 1 || w == 1 || s == 1) v = 3;
        }
        return v;
    }
    case L_EDGE_SPECIAL:
    {
        int v = at(p, x, z);
        cseed(&r, ws, x, z);
        if (v != 0 && next_int(&r, 13) == 0) v |= (1 + next_int(&r, 15)) << 8 & 0xF00;
        return v;
    }
    case L_ADD_MUSHROOM:
    {
        int a = at(p, x - 1, z - 1), b = at(p, x + 1, z - 1), c = at(p, x - 1, z + 1), d = at(p, x + 1, z + 1);
        int v = at(p, x, z);
        cseed(&r, ws, x, z);
        if (v == 0 && a == 0 && b == 0 && c == 0 && d == 0 && next_int(&r, 100) == 0) return MUSHROOM_ISLAND;
        return v;
    }
    case L_DEEP_OCEAN:
    {
        int nn = at(p, x, z - 1), e = at(p, x + 1, z), w = at(p, x - 1, z), s = at(p, x, z + 1), v = at(p, x, z);
        int k = (nn == 0) + (e == 0) + (w == 0) + (s == 0);
        return v == 0 && k > 3 ? DEEP_OCEAN : v;
    }
    case L_RIVER_INIT:
    {
        int v = at(p, x, z);
        cseed(&r, ws, x, z);
        return v > 0 ? next_int(&r, 299999) + 2 : 0;
    }
    case L_BIOME:
    {
        const int warm[] = {DESERT, DESERT, DESERT, SAVANNA, SAVANNA, PLAINS};
        const int medium[] = {FOREST, ROOFED_FOREST, EXTREME_HILLS, PLAINS, BIRCH_FOREST, SWAMPLAND};
        const int cold[] = {FOREST, EXTREME_HILLS, TAIGA, PLAINS};
        const int ice[] = {ICE_PLAINS, ICE_PLAINS, ICE_PLAINS, COLD_TAIGA};
        cseed(&r, ws, x, z);
        int v = at(p, x, z);
        int special = (v & 0xF00) >> 8;
        v &= ~0xF00;
        if (oceanic(v)) return v;
        if (v == MUSHROOM_ISLAND) return v;
        if (v == 1) return special > 0 ? (next_int(&r, 3) == 0 ? MESA_PLATEAU : MESA_PLATEAU_F) : warm[next_int(&r, 6)];
        if (v == 2) return special > 0 ? JUNGLE : medium[next_int(&r, 6)];
        if (v == 3) return special > 0 ? MEGA_TAIGA : cold[next_int(&r, 4)];
        if (v == 4) return ice[next_int(&r, 4)];
        return MUSHROOM_ISLAND;
    }
    case L_BIOME_EDGE:
    {
        int v = at(p, x, z);
        int nn = at(p, x, z - 1), e = at(p, x + 1, z), w = at(p, x - 1, z), s = at(p, x, z + 1);
        if (same(v, EXTREME_HILLS))
        {
            int ok = edge_compatible(nn, EXTREME_HILLS) && edge_compatible(e, EXTREME_HILLS)
                  && edge_compatible(w, EXTREME_HILLS) && edge_compatible(s, EXTREME_HILLS);
            return ok ? v : EXTREME_HILLS_EDGE;
        }
        if (v == MESA_PLATEAU_F || v == MESA_PLATEAU)
            return same(nn, v) && same(e, v) && same(w, v) && same(s, v) ? v : MESA;
        if (v == MEGA_TAIGA)
            return same(nn, v) && same(e, v) && same(w, v) && same(s, v) ? v : TAIGA;
        if (v == DESERT)
            return nn != ICE_PLAINS && e != ICE_PLAINS && w != ICE_PLAINS && s != ICE_PLAINS ? v : EXTREME_HILLS_PLUS;
        if (v == SWAMPLAND)
        {
            if (nn != DESERT && e != DESERT && w != DESERT && s != DESERT
                && nn != COLD_TAIGA && e != COLD_TAIGA && w != COLD_TAIGA && s != COLD_TAIGA
                && nn != ICE_PLAINS && e != ICE_PLAINS && w != ICE_PLAINS && s != ICE_PLAINS)
                return nn != JUNGLE && s != JUNGLE && e != JUNGLE && w != JUNGLE ? v : JUNGLE_EDGE;
            return PLAINS;
        }
        return v;
    }
    case L_HILLS:
    {
        cseed(&r, ws, x, z);
        int v = at(p, x, z);
        int k = at(q, x, z);
        int mutate = (k - 2) % 29 == 0;
        if (v != 0 && k >= 2 && (k - 2) % 29 == 1 && v < 128) return exists(v + 128) ? v + 128 : v;
        if (next_int(&r, 3) != 0 && !mutate) return v;
        int t = v;
        if (v == DESERT) t = DESERT_HILLS;
        else if (v == FOREST) t = FOREST_HILLS;
        else if (v == BIRCH_FOREST) t = BIRCH_FOREST_HILLS;
        else if (v == ROOFED_FOREST) t = PLAINS;
        else if (v == TAIGA) t = TAIGA_HILLS;
        else if (v == MEGA_TAIGA) t = MEGA_TAIGA_HILLS;
        else if (v == COLD_TAIGA) t = COLD_TAIGA_HILLS;
        else if (v == PLAINS) t = next_int(&r, 3) == 0 ? FOREST_HILLS : FOREST;
        else if (v == ICE_PLAINS) t = ICE_MOUNTAINS;
        else if (v == JUNGLE) t = JUNGLE_HILLS;
        else if (v == OCEAN) t = DEEP_OCEAN;
        else if (v == EXTREME_HILLS) t = EXTREME_HILLS_PLUS;
        else if (v == SAVANNA) t = SAVANNA_PLATEAU;
        else if (same(v, MESA_PLATEAU_F)) t = MESA;
        else if (v == DEEP_OCEAN && next_int(&r, 3) == 0) t = next_int(&r, 2) == 0 ? PLAINS : FOREST;
        if (mutate && t != v) t = exists(t + 128) ? t + 128 : v;
        if (t == v) return v;
        int nn = at(p, x, z - 1), e = at(p, x + 1, z), w = at(p, x - 1, z), s = at(p, x, z + 1);
        int c = same(nn, v) + same(e, v) + same(w, v) + same(s, v);
        return c >= 3 ? t : v;
    }
    case L_RIVER:
    {
#define RC(v) ((v) >= 2 ? 2 + ((v) & 1) : (v))
        int w = RC(at(p, x - 1, z)), e = RC(at(p, x + 1, z));
        int nn = RC(at(p, x, z - 1)), s = RC(at(p, x, z + 1));
        int v = RC(at(p, x, z));
#undef RC
        return v == w && v == nn && v == e && v == s ? -1 : RIVER;
    }
    case L_SMOOTH:
    {
        int w = at(p, x - 1, z), e = at(p, x + 1, z), nn = at(p, x, z - 1), s = at(p, x, z + 1), v = at(p, x, z);
        if (w == e && nn == s)
        {
            cseed(&r, ws, x, z);
            return next_int(&r, 2) == 0 ? w : nn;
        }
        if (w == e) v = w;
        if (nn == s) v = nn;
        return v;
    }
    case L_RARE_BIOME:
    {
        cseed(&r, ws, x, z);
        int v = at(p, x, z);
        return next_int(&r, 57) == 0 && v == PLAINS ? PLAINS + 128 : v;
    }
    case L_SHORE:
    {
        /* cseed runs in C but shore draws nothing */
        int v = at(p, x, z);
        int nn = at(p, x, z - 1), e = at(p, x + 1, z), w = at(p, x - 1, z), s = at(p, x, z + 1);
        int near_ocean = oceanic(nn) || oceanic(e) || oceanic(w) || oceanic(s);
        if (v == MUSHROOM_ISLAND) return nn != OCEAN && e != OCEAN && w != OCEAN && s != OCEAN ? v : MUSHROOM_SHORE;
        if (exists(v) && def(v)->cls == CLS_JUNGLE)
        {
            if (jungle_compatible(nn) && jungle_compatible(e) && jungle_compatible(w) && jungle_compatible(s))
                return near_ocean ? BEACH : v;
            return JUNGLE_EDGE;
        }
        if (v != EXTREME_HILLS && v != EXTREME_HILLS_PLUS && v != EXTREME_HILLS_EDGE)
        {
            if (exists(v) && def(v)->snow) return oceanic(v) ? v : near_ocean ? COLD_BEACH : v;
            if (v != MESA && v != MESA_PLATEAU_F)
            {
                if (v != OCEAN && v != DEEP_OCEAN && v != RIVER && v != SWAMPLAND) return near_ocean ? BEACH : v;
                return v;
            }
            if (near_ocean) return v;
            int all_mesa = def(nn)->mesa && exists(nn) && def(e)->mesa && exists(e)
                        && def(w)->mesa && exists(w) && def(s)->mesa && exists(s);
            return all_mesa ? v : DESERT;
        }
        return oceanic(v) ? v : near_ocean ? STONE_BEACH : v;
    }
    case L_RIVER_MIX:
    {
        int b = at(p, x, z), rv = at(q, x, z);
        if (b != OCEAN && b != DEEP_OCEAN && rv == RIVER)
        {
            if (b == ICE_PLAINS) return FROZEN_RIVER;
            if (b != MUSHROOM_ISLAND && b != MUSHROOM_SHORE) return rv & 255;
            return MUSHROOM_SHORE;
        }
        return b;
    }
    case L_VORONOI:
    {
        /* C shifts the area by -2 and reads a from the parent unmasked in its
         * first column only; every value here is below 256 and the object-id
         * map masks with 255 anyway, so all four are masked */
        int X = x - 2, Z = z - 2;
        int px = X >> 2, pz = Z >> 2, xx = X & 3, zz = Z & 3;
        cseed(&r, ws, (int64_t)px * 4, (int64_t)pz * 4);
        double ax = jitter(&r), az = jitter(&r);
        cseed(&r, ws, (int64_t)(px + 1) * 4, (int64_t)pz * 4);
        double cx = jitter(&r) + 4.0, cz = jitter(&r);
        cseed(&r, ws, (int64_t)px * 4, (int64_t)(pz + 1) * 4);
        double bx = jitter(&r), bz = jitter(&r) + 4.0;
        cseed(&r, ws, (int64_t)(px + 1) * 4, (int64_t)(pz + 1) * 4);
        double dx = jitter(&r) + 4.0, dz = jitter(&r) + 4.0;
        int a = at(p, px, pz) & 255, b = at(p, px, pz + 1) & 255;
        int c = at(p, px + 1, pz) & 255, d = at(p, px + 1, pz + 1) & 255;
        double da = (zz - az) * (zz - az) + (xx - ax) * (xx - ax);
        double dc = (zz - cz) * (zz - cz) + (xx - cx) * (xx - cx);
        double db = (zz - bz) * (zz - bz) + (xx - bx) * (xx - bx);
        double dd = (zz - dz) * (zz - dz) + (xx - dx) * (xx - dx);
        if (da < dc && da < db && da < dd) return a;
        if (dc < da && dc < db && dc < dd) return c;
        if (db < da && db < dc && db < dd) return b;
        return d;
    }
    }
    return 0;
}

__global__ void layers(const struct cg_seed *seeds, const struct worldgen_req *req, int n,
                         uint8_t *gen100, uint8_t *full256)
{
    extern __shared__ int sbuf[];
    __shared__ struct lbox sb[NODES];
    __shared__ int bad;
    int k = blockIdx.x;
    if (k >= n) return;
    const struct cg_seed *s = &seeds[req[k].seed_index];
    int cx = req[k].cx, cz = req[k].cz;
    if (threadIdx.x == 0)
    {
        int b[NODES][4];
        layer_boxes(c_graph, cx, cz, b);
        bad = 0;
        for (int i = 0; i < NODES; ++i)
        {
            int w = b[i][2] - b[i][0], h = b[i][3] - b[i][1];
            if (w <= 0) w = h = 0;
            sb[i].x0 = b[i][0];
            sb[i].z0 = b[i][1];
            sb[i].w = w;
            sb[i].h = h;
            if (w > c_maxw[i] || h > c_maxh[i]) bad = 1;
        }
        if (bad) atomicOr(&d_err, WORLDGEN_ERR_LAYER_BOX);
    }
    __syncthreads();
    if (bad) return;
    for (int i = 0; i < NODES; ++i)
    {
        struct lbox b = sb[i];
        const struct lnode *l = &c_graph[i];
        lview p = {0, {0, 0, 0, 0}}, q = {0, {0, 0, 0, 0}};
        if (l->p1 >= 0) p = lview{sbuf + c_off[l->p1], sb[l->p1]};
        if (l->p2 >= 0) q = lview{sbuf + c_off[l->p2], sb[l->p2]};
        int *out = sbuf + c_off[i];
        int64_t ws = s->ws[i];
        for (int c = threadIdx.x; c < b.w * b.h; c += blockDim.x)
            out[c] = layer_cell(i, ws, b.x0 + c % b.w, b.z0 + c / b.w, p, q);
        __syncthreads();
    }
    /* to_object_ids */
    lview mix = {sbuf + c_off[NODE_MIX], sb[NODE_MIX]};
    lview vor = {sbuf + c_off[NODE_VORONOI], sb[NODE_VORONOI]};
    for (int c = threadIdx.x; c < 100; c += blockDim.x)
        gen100[(size_t)k * 100 + c] = (uint8_t)d_biomes[at(mix, cx * 4 - 2 + c % 10, cz * 4 - 2 + c / 10) & 255].object_id;
    for (int c = threadIdx.x; c < 256; c += blockDim.x)
        full256[(size_t)k * 256 + c] = (uint8_t)d_biomes[at(vor, cx * 16 + c % 16, cz * 16 + c / 16) & 255].object_id;
}
