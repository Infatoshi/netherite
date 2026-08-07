/* tests/test_mesh_models.c - non-cube model mesher unit test.
 *
 * Builds a SYNTHETIC test chunk containing one of each new block type high in
 * the air (y=100, above terrain, isolated) via the light_debug_set_block hook,
 * meshes it with the MC-faithful mesher, and asserts the emitted non-cube
 * geometry. The GOLDEN is the render-opt facebakery kernel itself
 * (rk_facebakery_make_quad): the test independently bakes each box/plane with the
 * SAME model bounds the mesher uses and asserts the mesher's vertex positions and
 * atlas UVs reproduce the kernel output (do NOT hand-verify against invented
 * numbers). It also checks per-layer vertex counts and render-layer routing.
 */
#include "world/light.h"
#include "world/mesh_mc.h"
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"   /* CR_SPRITE_* indices */
#include "renderkernels/rk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* --- block-state ids under test (mirror assets/blockmodels.c) --- */
enum {
    T_STAIRS = 201, T_SLAB = 202, T_FENCE = 203, T_GLASS = 200,
    T_TALLGRASS = 39, T_LEAVES = 34, T_WATER = 2, T_LAVA = 11, T_LILY = 14,
    T_SNOW = 16, T_VINE = 71, T_CACTUS = 81, T_FIRE = 213,
    T_PORTAL = 211, T_END_FRAME = 216, T_MAGMA = 220,
    T_IRON_BARS = 221, T_TORCH = 222, T_GLASS_PANE = 253,
    T_STONE = 1
};

/* --- test chunk placement (chunk (10,10), local columns, y=100) --- */
#define CCX 10
#define CCZ 10
#define BASE (16 * CCX)   /* world x/z base of chunk (10,10) = 160 */
#define TY 100

static int g_fail = 0;
#define CHECK(cond, ...) do { if (!(cond)) { printf("FAIL: "); printf(__VA_ARGS__); \
    printf("\n"); g_fail = 1; } } while (0)

/* ---- facebakery golden: replicate the mesher's bake for one box/plane face ---- */
static float bits2f(int32_t b) { float f; memcpy(&f, &b, sizeof f); return f; }

static void face_uv_model(int facing, const float from[3], const float to[3], float uvs[4]) {
    switch (facing) {
        case BM_DOWN: case BM_UP:
            uvs[0]=from[0]; uvs[1]=from[2]; uvs[2]=to[0]; uvs[3]=to[2]; break;
        case BM_NORTH: case BM_SOUTH:
            uvs[0]=from[0]; uvs[1]=from[1]; uvs[2]=to[0]; uvs[3]=to[1]; break;
        default:
            uvs[0]=from[2]; uvs[1]=from[1]; uvs[2]=to[2]; uvs[3]=to[1]; break;
    }
}

typedef struct { float x,y,z,u,v; } GVert;
typedef struct { GVert *v; int n, cap; } GList;
static void gl_push(GList *g, GVert q) {
    if (g->n == g->cap) { g->cap = g->cap ? g->cap*2 : 64;
        g->v = realloc(g->v, g->cap*sizeof(GVert)); }
    g->v[g->n++] = q;
}

/* bake one face (TRI-expanded to 6 verts, matching the mesher) into g */
static void golden_face_custom(GList *g, int wx, int wy, int wz,
                               const float from[3], const float to[3], int facing,
                               const float custom_uv[4], int partPresent, int axis,
                               float angle, const float origin[3], int rescale,
                               int sprite) {
    float u0,v0,u1,v1; bm_sprite_uv(sprite,&u0,&v0,&u1,&v1);
    float uvs[4];
    if (custom_uv) memcpy(uvs, custom_uv, sizeof uvs);
    else face_uv_model(facing, from, to, uvs);
    int32_t d[28];
    rk_facebakery_make_quad(from[0],from[1],from[2], to[0],to[1],to[2],
        facing, 0, uvs, u0,u1,v0,v1, partPresent, axis, angle, origin, rescale, d);
    GVert corner[4];
    for (int c=0;c<4;++c){ int o=c*7;
        corner[c].x=bits2f(d[o])+wx; corner[c].y=bits2f(d[o+1])+wy; corner[c].z=bits2f(d[o+2])+wz;
        corner[c].u=bits2f(d[o+4]); corner[c].v=bits2f(d[o+5]); }
    static const int TRI[6] = {0,1,2,0,2,3};
    for (int k=0;k<6;++k) gl_push(g, corner[TRI[k]]);
}
static void golden_face(GList *g, int wx, int wy, int wz,
                        const float from[3], const float to[3], int facing,
                        int partPresent, int axis, float angle, const float origin[3],
                        int rescale, int sprite) {
    golden_face_custom(g, wx, wy, wz, from, to, facing, NULL,
                       partPresent, axis, angle, origin, rescale, sprite);
}
static void golden_box(GList *g, int wx,int wy,int wz,
                       const float from[3], const float to[3], int sprite) {
    for (int f=0; f<6; ++f)
        golden_face(g, wx,wy,wz, from,to, f, 0,3,0.0f,NULL,0, sprite);
}

static void golden_rotate_y(GList *g, int wx, int wz, int quarter_turns) {
    int qn = quarter_turns & 3;
    float cx = (float)wx + 0.5f, cz = (float)wz + 0.5f;
    for (int i=0; i<g->n; ++i) {
        float x=g->v[i].x-cx, z=g->v[i].z-cz, rx=x, rz=z;
        if (qn == 1) { rx=-z; rz=x; }
        else if (qn == 2) { rx=-x; rz=-z; }
        else if (qn == 3) { rx=z; rz=-x; }
        g->v[i].x=cx+rx; g->v[i].z=cz+rz;
    }
}

static void golden_torch_wall(GList *g, int wx, int wy, int wz, int turns) {
    const float cap_down[4]={7,13,9,15}, cap_up[4]={7,6,9,8};
    const float full[4]={0,0,16,16};
    const float origin[3]={0,3.5f/16.0f,0.5f};
    const float s0[3]={-1,3.5f,7},s1[3]={1,13.5f,9};
    const float x0[3]={-1,3.5f,0},x1[3]={1,19.5f,16};
    const float z0[3]={-8,3.5f,7},z1[3]={8,19.5f,9};
    golden_face_custom(g,wx,wy,wz,s0,s1,BM_DOWN,cap_down,1,2,-22.5f,origin,0,CR_SPRITE_TORCH_ON);
    golden_face_custom(g,wx,wy,wz,s0,s1,BM_UP,cap_up,1,2,-22.5f,origin,0,CR_SPRITE_TORCH_ON);
    golden_face_custom(g,wx,wy,wz,x0,x1,BM_WEST,full,1,2,-22.5f,origin,0,CR_SPRITE_TORCH_ON);
    golden_face_custom(g,wx,wy,wz,x0,x1,BM_EAST,full,1,2,-22.5f,origin,0,CR_SPRITE_TORCH_ON);
    golden_face_custom(g,wx,wy,wz,z0,z1,BM_NORTH,full,1,2,-22.5f,origin,0,CR_SPRITE_TORCH_ON);
    golden_face_custom(g,wx,wy,wz,z0,z1,BM_SOUTH,full,1,2,-22.5f,origin,0,CR_SPRITE_TORCH_ON);
    golden_rotate_y(g,wx,wz,turns);
}

/* ---- collect mesher verts in an explicit x/z/y box ---- */
static int collect_box(const CrChunkMeshMC *m, int layer,
                       float xlo, float xhi, float zlo, float zhi,
                       float ylo, float yhi, GVert *out, int maxout) {
    int n = 0;
    for (int i = 0; i < m->nverts[layer]; ++i) {
        const CrVertex *v = &m->verts[layer][i];
        if (v->pos.x < xlo || v->pos.x > xhi) continue;
        if (v->pos.z < zlo || v->pos.z > zhi) continue;
        if (v->pos.y < ylo || v->pos.y > yhi) continue;
        if (n < maxout) { out[n].x=v->pos.x; out[n].y=v->pos.y; out[n].z=v->pos.z;
                          out[n].u=v->uv.x; out[n].v=v->uv.y; }
        ++n;
    }
    return n;
}
/* one-cell footprint helper */
static int collect(const CrChunkMeshMC *m, int layer, int wx, int wz,
                   float ylo, float yhi, GVert *out, int maxout) {
    return collect_box(m, layer, wx-0.01f, wx+1.01f, wz-0.01f, wz+1.01f,
                       ylo, yhi, out, maxout);
}

/* Collect complete six-vertex quads whose centroid belongs to one block cell. */
static int collect_cell_quads(const CrChunkMeshMC *m, int layer, int wx, int wy,
                              int wz, GVert *out, int maxout) {
    int n = 0;
    for (int at=0; at+5<m->nverts[layer]; at+=6) {
        float cx=0.0f, cy=0.0f, cz=0.0f;
        for (int k=0;k<6;++k) {
            const CrVertex *v=&m->verts[layer][at+k];
            cx+=v->pos.x; cy+=v->pos.y; cz+=v->pos.z;
        }
        cx/=6.0f; cy/=6.0f; cz/=6.0f;
        if (cx < wx-1e-4f || cx > wx+1.0001f
            || cy < wy-1e-4f || cy > wy+1.0001f
            || cz < wz-1e-4f || cz > wz+1.0001f) continue;
        for (int k=0;k<6;++k) {
            const CrVertex *v=&m->verts[layer][at+k];
            if (n<maxout) out[n]=(GVert){v->pos.x,v->pos.y,v->pos.z,v->uv.x,v->uv.y};
            ++n;
        }
    }
    return n;
}

static int vsame(const GVert *a, const GVert *b, int with_uv) {
    int p = fabsf(a->x-b->x)<1e-4f && fabsf(a->y-b->y)<1e-4f && fabsf(a->z-b->z)<1e-4f;
    if (!with_uv) return p;
    return p && fabsf(a->u-b->u)<1e-4f && fabsf(a->v-b->v)<1e-4f;
}
/* every collected vert must appear (as a multiset) in the golden set */
static int multiset_eq(const GVert *got, int ng, const GList *golden, int with_uv) {
    if (ng != golden->n) return 0;
    int *used = calloc(golden->n, sizeof(int));
    for (int i=0;i<ng;++i){ int found=0;
        for (int j=0;j<golden->n;++j) if(!used[j] && vsame(&got[i],&golden->v[j],with_uv)){used[j]=1;found=1;break;}
        if(!found){ free(used); return 0; } }
    free(used); return 1;
}

static int count_sprite_uv(const GVert *v, int n, int sprite) {
    float u0, v0, u1, v1;
    int count = 0;
    bm_sprite_uv(sprite, &u0, &v0, &u1, &v1);
    for (int i = 0; i < n; ++i)
        if (v[i].u >= u0 - 1e-6f && v[i].u <= u1 + 1e-6f &&
            v[i].v >= v0 - 1e-6f && v[i].v <= v1 + 1e-6f)
            ++count;
    return count;
}

int main(void) {
    CHECK(bm_block(T_IRON_BARS)->kind == BM_KIND_IRON_BARS,
          "iron bars: specialized model is not reachable");
    CHECK(bm_block(T_IRON_BARS)->layer == CR_LAYER_CUTOUT_MIPPED,
          "iron bars: wrong render layer");
    CHECK(bm_block(T_GLASS_PANE)->kind == BM_KIND_GLASS_PANE,
          "glass pane: specialized model is not reachable");
    CHECK(bm_block(T_GLASS_PANE)->layer == CR_LAYER_CUTOUT_MIPPED,
          "glass pane: wrong render layer");
    CHECK(bm_block(T_TORCH)->kind == BM_KIND_TORCH,
          "torch: specialized model is not reachable");
    CHECK(bm_block(T_TORCH)->layer == CR_LAYER_CUTOUT,
          "torch: wrong render layer");
    CrWorldMC *w = worldmc_create(0);
    if (!w) { printf("FAIL: worldmc_create\n"); return 1; }
    worldmc_ensure(w, CCX, CCZ, 1);

    /* light state lives inside CrWorldMC; reach it via the worldmc API is not
     * exposed, so place blocks through a fresh CrLight sharing the same seed is
     * not possible - instead worldmc_ensure already built chunk (10,10). We need
     * the CrLight* to set blocks; expose it via a tiny accessor in mesh_mc. */
    extern CrLight *worldmc_light(CrWorldMC *);
    CrLight *L = worldmc_light(w);
    extern uint64_t worldmc_test_position_random(int, int, int);
    extern int worldmc_test_fire_variant(int, int, int, int, int);

    /* Java MathHelper.getCoordinateRandom + WeightedBakedModel vectors. The last
     * vector crosses a rand++ selector boundary and catches accidental reuse. */
    CHECK(worldmc_test_position_random(167,100,162) == UINT64_C(0xb3bbd35e8f78cc02),
          "fire hash vector (167,100,162)");
    const int fv[4][4] = {
        {164,200,160,0}, {171,200,160,1},
        {175,200,162,2}, {160,200,160,3},
    };
    for (int i=0; i<4; ++i) {
        CHECK(worldmc_test_fire_variant(fv[i][0],fv[i][1],fv[i][2],0,2)
                  == (fv[i][3] & 1),
              "fire floor variant vector %d", i);
        for (int selector=1; selector<=4; ++selector)
            CHECK(worldmc_test_fire_variant(fv[i][0],fv[i][1],fv[i][2],selector,4)
                      == fv[i][3],
                  "fire side variant vector %d selector %d", i, selector);
    }
    CHECK(worldmc_test_fire_variant(0,70,88,0,2) == 1,
          "fire boundary floor variant");
    CHECK(worldmc_test_fire_variant(0,70,88,1,4) == 1
          && worldmc_test_fire_variant(0,70,88,2,4) == 2,
          "fire rand++ selector boundary");

    /* place one of each model in its own air column at y=100 */
    int xs   = BASE + 2,  zs  = BASE + 2;   /* stairs */
    int xsl  = BASE + 4,  zsl = BASE + 4;   /* slab */
    int xf   = BASE + 6,  zf  = BASE + 6;   /* fence */
    int xc   = BASE + 8,  zc  = BASE + 8;   /* cross (tall grass) */
    int xl   = BASE + 10, zl  = BASE + 10;  /* leaves */
    int xw   = BASE + 12, zw  = BASE + 12;  /* water column */
    int xli  = BASE + 14, zli = BASE + 14;  /* lily pad */
    int xsn  = BASE + 1,  zsn = BASE + 8;   /* snow layer */
    int xv   = BASE + 3,  zv  = BASE + 10;  /* vine */
    int xca  = BASE + 5,  zca = BASE + 12;  /* cactus */
    int xfi  = BASE + 7,  zfi = BASE + 2;   /* supported fire */
    int xp   = BASE + 9,  zp  = BASE + 2;   /* portal axis X */
    int xpz  = BASE + 13, zpz = BASE + 2;   /* portal axis Z */
    int xef  = BASE + 11, zef = BASE + 4;   /* filled End frame */
    int xm   = BASE + 15, zm  = BASE + 6;   /* fullbright magma */
    int xb   = BASE + 1,  zb  = BASE + 14;  /* isolated iron bars */
    int xb4  = BASE + 11, zb4 = BASE + 14;  /* four-way bars into full cubes */
    int xbp  = BASE + 5,  zbp = BASE + 9;   /* adjacent iron bars */
    int xgp  = BASE + 1,  zgp = BASE + 11;  /* isolated glass pane */
    int xt   = BASE + 3,  zt  = BASE + 14;  /* standing torch */
    int xtw  = BASE + 8,  ztw = BASE + 14;  /* east-facing wall torch */
    int xlv  = BASE + 5,  ylv = TY + 20, zlv = BASE + 5; /* sloped lava */
    int xdp  = BASE + 14, zdp = BASE + 8, ydp = TY + 40; /* six double plants */
    light_debug_set_block(L, xs,  TY, zs,  T_STAIRS);
    light_debug_set_block(L, xsl, TY, zsl, T_SLAB);
    light_debug_set_block(L, xf,  TY, zf,  T_FENCE);
    light_debug_set_block(L, xc,  TY, zc,  T_TALLGRASS);
    light_debug_set_block(L, xl,  TY, zl,  T_LEAVES);
    light_debug_set_block(L, xw,  TY,   zw, T_WATER);
    light_debug_set_block(L, xw,  TY+1, zw, T_WATER);
    light_debug_set_block(L, xli, TY, zli, T_LILY);
    light_debug_set_block(L, xsn, TY, zsn, T_SNOW);
    light_debug_set_block(L, xv,  TY, zv,  T_VINE);
    light_debug_set_block(L, xca, TY, zca, T_CACTUS);
    light_debug_set_block(L, xfi, TY - 1, zfi, T_STONE);
    light_debug_set_block(L, xfi, TY, zfi, T_FIRE);
    light_debug_set_block_meta(L, xp, TY, zp, T_PORTAL, 1);
    light_debug_set_block_meta(L, xpz, TY, zpz, T_PORTAL, 2);
    light_debug_set_block_meta(L, xef, TY, zef, T_END_FRAME, 4);
    light_debug_set_block(L, xm, TY, zm, T_MAGMA);
    light_debug_set_block(L, xb, TY, zb, T_IRON_BARS);
    light_debug_set_block(L, xb4, TY, zb4, T_IRON_BARS);
    light_debug_set_block(L, xb4, TY, zb4 - 1, T_STONE);
    light_debug_set_block(L, xb4 + 1, TY, zb4, T_STONE);
    light_debug_set_block(L, xb4, TY, zb4 + 1, T_STONE);
    light_debug_set_block(L, xb4 - 1, TY, zb4, T_STONE);
    light_debug_set_block(L, xbp, TY, zbp, T_IRON_BARS);
    light_debug_set_block(L, xbp + 1, TY, zbp, T_IRON_BARS);
    light_debug_set_block(L, xgp, TY, zgp, T_GLASS_PANE);
    light_debug_set_block_meta(L, xt, TY, zt, T_TORCH, 5);
    light_debug_set_block_meta(L, xtw, TY, ztw, T_TORCH, 1);
    light_debug_set_block_meta(L, xtw, TY + 4, ztw, T_TORCH, 2);
    light_debug_set_block_meta(L, xtw, TY + 8, ztw, T_TORCH, 3);
    light_debug_set_block_meta(L, xtw, TY + 12, ztw, T_TORCH, 4);
    light_debug_set_block_meta(L, xlv, ylv, zlv, T_LAVA, 3);
    light_debug_set_block_meta(L, xlv - 1, ylv, zlv - 1, T_LAVA, 4);
    light_debug_set_block_meta(L, xlv - 1, ylv, zlv + 1, T_LAVA, 5);
    light_debug_set_block_meta(L, xlv + 1, ylv, zlv + 1, T_LAVA, 6);
    light_debug_set_block_meta(L, xlv + 1, ylv, zlv - 1, T_LAVA, 7);
    for (int variant = 0; variant < 6; ++variant) {
        int y = ydp + variant * 4;
        light_set_state(L, xdp, y, zdp, (uint16_t)((175 << 4) | variant));
        light_set_state(L, xdp, y + 1, zdp, (uint16_t)((175 << 4) | 8));
    }
    light_set_render_state(L, -1, 0.0f, 0.0f);

    CrChunkMeshMC m;
    worldmc_mesh_chunk(w, CCX, CCZ, &m);

    GVert got[512];
    float ylo = 99.0f, yhi = 103.0f;
    int planks = CR_SPRITE_PLANKS_OAK;

    /* --- SLAB: 1 box {0,0,0}-{16,8,16}, SOLID, 36 verts, pos+uv match kernel.
     * Side faces carry vanilla half_slab explicit UVs (bottom half samples
     * V=8..16), not auto-UV (world/mesh_mc.c emit_slab). --- */
    {
        GList g = {0}; float from[3]={0,0,0}, to[3]={16,8,16};
        float full_uv[4] = {0.0f, 0.0f, 16.0f, 16.0f};
        float side_uv[4] = {0.0f, 8.0f, 16.0f, 16.0f};
        for (int f = 0; f < 6; ++f)
            golden_face_custom(&g, xsl, TY, zsl, from, to, f,
                               f >= BM_NORTH ? side_uv : full_uv,
                               0, 3, 0.0f, NULL, 0, planks);
        int n = collect(&m, CR_LAYER_SOLID, xsl, zsl, ylo, yhi, got, 512);
        CHECK(n == 36, "slab: %d verts (want 36)", n);
        CHECK(multiset_eq(got, n, &g, 1), "slab: positions/uv do not match facebakery golden");
        free(g.v);
    }
    /* --- STAIRS meta 0: bottom slab + raised east box, SOLID, 72 verts --- */
    {
        GList g = {0};
        float bf[3]={0,0,0}, bt[3]={16,8,16}, uf[3]={8,8,0}, ut[3]={16,16,16};
        const float buv[6][4] = {
            {0,0,16,16}, {0,0,16,16},
            {0,8,16,16}, {0,8,16,16},
            {0,8,16,16}, {0,8,16,16},
        };
        const float uuv[6][4] = {
            {8,0,16,16}, {8,0,16,16},
            {0,0,8,8}, {8,0,16,8},
            {0,0,16,8}, {0,0,16,8},
        };
        for (int f=0; f<6; ++f) {
            golden_face_custom(&g, xs,TY,zs, bf,bt, f,buv[f],
                               0,3,0.0f,NULL,0,planks);
            golden_face_custom(&g, xs,TY,zs, uf,ut, f,uuv[f],
                               0,3,0.0f,NULL,0,planks);
        }
        int n = collect(&m, CR_LAYER_SOLID, xs, zs, ylo, yhi, got, 512);
        CHECK(n == 72, "stairs: %d verts (want 72)", n);
        CHECK(multiset_eq(got, n, &g, 1), "stairs: positions/uv do not match facebakery golden");
        free(g.v);
    }
    /* --- FENCE: post + two rails in four directions = 9 boxes, 324 verts --- */
    {
        GList g = {0};
        float pf[3]={6,0,6}, pt[3]={10,16,10};
        golden_box(&g, xf, TY, zf, pf, pt, planks);
        float bars[8][2][3] = {
            {{7,6,0},{9,9,9}},   {{7,12,0},{9,15,9}},
            {{7,6,7},{9,9,16}},  {{7,12,7},{9,15,16}},
            {{0,6,7},{9,9,9}},   {{0,12,7},{9,15,9}},
            {{7,6,7},{16,9,9}},  {{7,12,7},{16,15,9}},
        };
        for (int i=0;i<8;++i) golden_box(&g, xf, TY, zf, bars[i][0], bars[i][1], planks);
        int n = collect(&m, CR_LAYER_SOLID, xf, zf, ylo, yhi, got, 512);
        CHECK(n == 324, "fence: %d verts (want 324)", n);
        CHECK(multiset_eq(got, n, &g, 1), "fence: positions/uv do not match facebakery golden");
        free(g.v);
    }
    /* --- CROSS (tall grass): 4 rotated planes, CUTOUT, 24 verts, GRASS tint --- */
    {
        GList g = {0}; const float o[3]={0.5f,0.5f,0.5f};
        const float aF[3]={0.8f,0,8}, aT[3]={15.2f,16,8};
        const float bF[3]={8,0,0.8f}, bT[3]={8,16,15.2f};
        const float uv[4]={0,0,16,16};
        int spr = CR_SPRITE_TALLGRASS;
        golden_face_custom(&g, xc,TY,zc, aF,aT, BM_NORTH, uv, 1,1,45.0f,o,1, spr);
        golden_face_custom(&g, xc,TY,zc, aF,aT, BM_SOUTH, uv, 1,1,45.0f,o,1, spr);
        golden_face_custom(&g, xc,TY,zc, bF,bT, BM_WEST,  uv, 1,1,45.0f,o,1, spr);
        golden_face_custom(&g, xc,TY,zc, bF,bT, BM_EAST,  uv, 1,1,45.0f,o,1, spr);
        uint64_t r = worldmc_test_position_random(xc, 0, zc);
        float ox = (((float)((r >> 16) & 15u) / 15.0f) - 0.5f) * 0.5f;
        float oy = (((float)((r >> 20) & 15u) / 15.0f) - 1.0f) * 0.2f;
        float oz = (((float)((r >> 24) & 15u) / 15.0f) - 0.5f) * 0.5f;
        for (int i=0; i<g.n; ++i) {
            g.v[i].x += ox; g.v[i].y += oy; g.v[i].z += oz;
        }
        /* rescaled 45deg planes poke slightly past the cell; widen the footprint
         * (tall grass is the only CUTOUT block placed, so this stays unambiguous). */
        int n = collect_box(&m, CR_LAYER_CUTOUT, xc-1.0f, xc+2.0f, zc-1.0f, zc+2.0f,
                            ylo, yhi, got, 512);
        CHECK(n == 24, "cross: %d verts (want 24)", n);
        CHECK(multiset_eq(got, n, &g, 1), "cross: positions/uv do not match facebakery golden");
        /* cross must NOT land on the SOLID layer */
        int solid = collect_box(&m, CR_LAYER_SOLID, xc-0.5f, xc+1.5f, zc-0.5f, zc+1.5f,
                                ylo, yhi, got, 512);
        CHECK(solid == 0, "cross: %d verts leaked onto SOLID layer", solid);
        free(g.v);
    }
    /* --- FIRE: supported multipart blockstate, 12 quads = 72 vertices. --- */
    {
        int n = collect_box(&m, CR_LAYER_CUTOUT,
                            xfi - 2.0f, xfi + 3.0f, zfi - 2.0f, zfi + 3.0f,
                            (float)TY - 1.0f, (float)TY + 2.0f, got, 512);
        CHECK(n == 72, "fire: %d verts (want 72 = 12 multipart quads)", n);
        CHECK(bm_block(T_FIRE)->kind == BM_KIND_FIRE, "fire: kind is not BM_KIND_FIRE");
        CHECK(bm_block(T_FIRE)->layer == CR_LAYER_CUTOUT, "fire: wrong render layer");
        float max_y = -1e9f;
        for (int i = 0; i < n; ++i) if (got[i].y > max_y) max_y = got[i].y;
        CHECK(max_y > (float)TY + 1.0f,
              "fire: max y %.4f must exceed one block (22.4 model units)", max_y);
        float u0,v0,u1,v1;
        bm_sprite_uv(CR_SPRITE_FIRE_LAYER_0, &u0,&v0,&u1,&v1);
        int floor_quads = 0, side_quads = 0, bad_attrs = 0, bad_uv = 0;
        for (int at=0; at<m.nverts[CR_LAYER_CUTOUT]; at+=6) {
            const CrVertex *q = &m.verts[CR_LAYER_CUTOUT][at];
            float cx=0,cy=0,cz=0, qminy=1e9f,qmaxy=-1e9f;
            for (int k=0;k<6;++k) {
                cx += q[k].pos.x; cy += q[k].pos.y; cz += q[k].pos.z;
                if (q[k].pos.y<qminy) qminy=q[k].pos.y;
                if (q[k].pos.y>qmaxy) qmaxy=q[k].pos.y;
            }
            cx/=6; cy/=6; cz/=6;
            if (fabsf(cx-(xfi+0.5f))>1.0f || fabsf(cz-(zfi+0.5f))>1.0f
                || cy<TY-0.1f || cy>TY+1.5f) continue;
            int floor = qminy < (float)TY - 1e-4f
                     || qmaxy > (float)TY + 1.4001f;
            if (floor) floor_quads++; else side_quads++;
            for (int k=0;k<6;++k) {
                if (q[k].ao != 1.0f || q[k].light != q[0].light) bad_attrs++;
                if (q[k].uv.x < u0 || q[k].uv.x > u1
                    || q[k].uv.y < v0 || q[k].uv.y > v1) bad_uv++;
            }
        }
        CHECK(floor_quads == 4 && side_quads == 8,
              "fire: floor/side quads=%d/%d want 4/8", floor_quads, side_quads);
        CHECK(bad_attrs == 0, "fire: AO or shade/light attribute mismatch (%d)", bad_attrs);
        CHECK(bad_uv == 0, "fire: variant0 UV escaped fire_layer_0 (%d)", bad_uv);
    }
    /* --- DOUBLE PLANTS: upper actual state inherits the lower species. --- */
    {
        const int lower_sprites[6] = {
            CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_BOTTOM,
            CR_SPRITE_DOUBLE_PLANT_SYRINGA_BOTTOM,
            CR_SPRITE_DOUBLE_PLANT_GRASS_BOTTOM,
            CR_SPRITE_DOUBLE_PLANT_FERN_BOTTOM,
            CR_SPRITE_DOUBLE_PLANT_ROSE_BOTTOM,
            CR_SPRITE_DOUBLE_PLANT_PAEONIA_BOTTOM,
        };
        const int upper_sprites[6] = {
            CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_TOP,
            CR_SPRITE_DOUBLE_PLANT_SYRINGA_TOP,
            CR_SPRITE_DOUBLE_PLANT_GRASS_TOP,
            CR_SPRITE_DOUBLE_PLANT_FERN_TOP,
            CR_SPRITE_DOUBLE_PLANT_ROSE_TOP,
            CR_SPRITE_DOUBLE_PLANT_PAEONIA_TOP,
        };
        for (int variant = 0; variant < 6; ++variant) {
            int y = ydp + variant * 4;
            int lower_n = collect_cell_quads(&m, CR_LAYER_CUTOUT,
                                             xdp, y, zdp, got, 512);
            CHECK(lower_n == 24, "double plant %d lower: %d verts (want 24)",
                  variant, lower_n);
            CHECK(count_sprite_uv(got, lower_n, lower_sprites[variant]) == lower_n,
                  "double plant %d lower: wrong sprite", variant);

            int upper_n = collect_cell_quads(&m, CR_LAYER_CUTOUT,
                                             xdp, y + 1, zdp, got, 512);
            int want_upper = variant == 0 ? 36 : 24;
            CHECK(upper_n == want_upper,
                  "double plant %d upper: %d verts (want %d)",
                  variant, upper_n, want_upper);
            if (variant == 0) {
                CHECK(count_sprite_uv(got, upper_n,
                                      CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_TOP) == 24,
                      "sunflower upper: top sprite vertex count");
                CHECK(count_sprite_uv(got, upper_n,
                                      CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_BACK) == 6,
                      "sunflower upper: back sprite vertex count");
                CHECK(count_sprite_uv(got, upper_n,
                                      CR_SPRITE_DOUBLE_PLANT_SUNFLOWER_FRONT) == 6,
                      "sunflower upper: front sprite vertex count");
            } else {
                CHECK(count_sprite_uv(got, upper_n, upper_sprites[variant]) == upper_n,
                      "double plant %d upper: contextual sprite", variant);
            }
            CHECK(collect_cell_quads(&m, CR_LAYER_SOLID,
                                      xdp, y, zdp, got, 512) == 0 &&
                  collect_cell_quads(&m, CR_LAYER_SOLID,
                                      xdp, y + 1, zdp, got, 512) == 0,
                  "double plant %d leaked onto SOLID", variant);
        }
    }
    /* --- IRON BARS: exact isolated post planes on CUTOUT_MIPPED. --- */
    {
        GList g = {0};
        const float end_uv[4] = {7,7,9,9};
        const float ys[2] = {0.001f,15.999f};
        for (int y=0;y<2;++y) {
            float from[3]={7,ys[y],7}, to[3]={9,ys[y],9};
            golden_face_custom(&g,xb,TY,zb,from,to,BM_DOWN,end_uv,0,3,0,NULL,0,
                               CR_SPRITE_IRON_BARS);
            golden_face_custom(&g,xb,TY,zb,from,to,BM_UP,end_uv,0,3,0,NULL,0,
                               CR_SPRITE_IRON_BARS);
        }
        const float a0[3]={8,0,7}, a1[3]={8,16,9};
        const float b0[3]={7,0,8}, b1[3]={9,16,8};
        const float uv0[4]={7,0,9,16}, uv1[4]={9,0,7,16};
        golden_face_custom(&g,xb,TY,zb,a0,a1,BM_WEST,uv0,0,3,0,NULL,0,
                           CR_SPRITE_IRON_BARS);
        golden_face_custom(&g,xb,TY,zb,a0,a1,BM_EAST,uv1,0,3,0,NULL,0,
                           CR_SPRITE_IRON_BARS);
        golden_face_custom(&g,xb,TY,zb,b0,b1,BM_NORTH,uv0,0,3,0,NULL,0,
                           CR_SPRITE_IRON_BARS);
        golden_face_custom(&g,xb,TY,zb,b0,b1,BM_SOUTH,uv1,0,3,0,NULL,0,
                           CR_SPRITE_IRON_BARS);
        int n=collect(&m,CR_LAYER_CUTOUT_MIPPED,xb,zb,ylo,yhi,got,512);
        CHECK(n==48,"iron bars isolated: %d verts (want 48)",n);
        CHECK(multiset_eq(got,n,&g,1),"iron bars isolated: positions/UV differ from JAR models");
        CHECK(collect(&m,CR_LAYER_CUTOUT,xb,zb,ylo,yhi,got,512)==0,
              "iron bars isolated: leaked onto CUTOUT");
        free(g.v);
    }
    /* Four opaque neighbours select all side models and suppress cullfaces:
     * post_ends 4 + N/E side 6 each + S/W side_alt 8 each = 32 quads. */
    {
        int n=collect(&m,CR_LAYER_CUTOUT_MIPPED,xb4,zb4,ylo,yhi,got,512);
        CHECK(n==192,"iron bars four-way: %d verts (want 192)",n);
    }
    /* Same-pane cullfaces are suppressed. The old implementation emitted one
     * extra edge quad in each direction, producing 192 instead of 180 verts. */
    {
        int n=collect_box(&m,CR_LAYER_CUTOUT_MIPPED,
                          xbp-0.01f,xbp+2.01f,zbp-0.01f,zbp+1.01f,
                          ylo,yhi,got,512);
        CHECK(n==180,"iron bars adjacent panes: %d verts (want 180)",n);
    }
    /* pane_post down/up plus one noside face per cardinal = 6 quads. */
    {
        int n=collect(&m,CR_LAYER_CUTOUT_MIPPED,xgp,zgp,ylo,yhi,got,512);
        CHECK(n==36,"glass pane isolated: %d verts (want 36)",n);
    }
    /* --- TORCH: exact six shade=false planes for standing and wall models. --- */
    {
        GList g={0};
        const float cap_down[4]={7,13,9,15}, cap_up[4]={7,6,9,8};
        const float full[4]={0,0,16,16};
        const float s0[3]={7,0,7},s1[3]={9,10,9};
        const float x0[3]={7,0,0},x1[3]={9,16,16};
        const float z0[3]={0,0,7},z1[3]={16,16,9};
        golden_face_custom(&g,xt,TY,zt,s0,s1,BM_DOWN,cap_down,0,3,0,NULL,0,CR_SPRITE_TORCH_ON);
        golden_face_custom(&g,xt,TY,zt,s0,s1,BM_UP,cap_up,0,3,0,NULL,0,CR_SPRITE_TORCH_ON);
        golden_face_custom(&g,xt,TY,zt,x0,x1,BM_WEST,full,0,3,0,NULL,0,CR_SPRITE_TORCH_ON);
        golden_face_custom(&g,xt,TY,zt,x0,x1,BM_EAST,full,0,3,0,NULL,0,CR_SPRITE_TORCH_ON);
        golden_face_custom(&g,xt,TY,zt,z0,z1,BM_NORTH,full,0,3,0,NULL,0,CR_SPRITE_TORCH_ON);
        golden_face_custom(&g,xt,TY,zt,z0,z1,BM_SOUTH,full,0,3,0,NULL,0,CR_SPRITE_TORCH_ON);
        int n=collect(&m,CR_LAYER_CUTOUT,xt,zt,ylo,yhi,got,512);
        CHECK(n==36,"standing torch: %d verts (want 36)",n);
        CHECK(multiset_eq(got,n,&g,1),"standing torch: positions/UV differ from torch.json");
        float light=-1.0f; int bad=0, seen=0;
        for(int i=0;i<m.nverts[CR_LAYER_CUTOUT];++i){const CrVertex *v=&m.verts[CR_LAYER_CUTOUT][i];
            if(v->pos.x<xt||v->pos.x>xt+1||v->pos.z<zt||v->pos.z>zt+1||v->pos.y<TY||v->pos.y>TY+1)continue;
            if(light<0)light=v->light; else if(v->light!=light)bad++; seen++;}
        CHECK(seen==36&&bad==0,"standing torch: shade=false light mismatch seen=%d bad=%d",seen,bad);
        free(g.v);
    }
    {
        const int ty[4]={TY,TY+4,TY+8,TY+12};
        const int turns[4]={0,2,1,3}; /* metadata 1=E, 2=W, 3=S, 4=N */
        for(int meta=1;meta<=4;++meta){
            GList g={0};
            golden_torch_wall(&g,xtw,ty[meta-1],ztw,turns[meta-1]);
            int n=collect_box(&m,CR_LAYER_CUTOUT,
                              xtw-1.0f,xtw+1.5f,ztw-1.0f,ztw+1.5f,
                              ty[meta-1],ty[meta-1]+2,got,512);
            CHECK(n==36,"wall torch meta %d: %d verts (want 36)",meta,n);
            CHECK(multiset_eq(got,n,&g,1),
                  "wall torch meta %d: positions/UV differ from torch_wall.json",meta);
            free(g.v);
        }
    }
    /* --- PORTAL: meta=1/axis X -> z-thin portal_ns, N/S faces only. --- */
    {
        GList g = {0};
        const float from[3] = {0,0,6}, to[3] = {16,16,10};
        golden_face(&g, xp, TY, zp, from, to, BM_NORTH,
                    0, 3, 0.0f, NULL, 0, CR_SPRITE_PORTAL);
        golden_face(&g, xp, TY, zp, from, to, BM_SOUTH,
                    0, 3, 0.0f, NULL, 0, CR_SPRITE_PORTAL);
        int n = collect(&m, CR_LAYER_TRANSLUCENT, xp, zp, ylo, yhi, got, 512);
        CHECK(n == 12, "portal: %d verts (want 12 = two faces)", n);
        CHECK(multiset_eq(got, n, &g, 1),
              "portal: axis-X panel positions/uv differ from JAR model");
        CHECK(bm_block(T_PORTAL)->kind == BM_KIND_PORTAL, "portal: wrong kind");
        free(g.v);
    }
    /* meta=2/axis Z -> x-thin portal_ew, E/W faces only. */
    {
        GList g = {0};
        const float from[3] = {6,0,0}, to[3] = {10,16,16};
        golden_face(&g, xpz, TY, zpz, from, to, BM_EAST,
                    0, 3, 0.0f, NULL, 0, CR_SPRITE_PORTAL);
        golden_face(&g, xpz, TY, zpz, from, to, BM_WEST,
                    0, 3, 0.0f, NULL, 0, CR_SPRITE_PORTAL);
        int n = collect(&m, CR_LAYER_TRANSLUCENT, xpz, zpz, ylo, yhi, got, 512);
        CHECK(n == 12, "portal axis Z: %d verts (want 12)", n);
        CHECK(multiset_eq(got, n, &g, 1),
              "portal: axis-Z panel positions/uv differ from JAR model");
        free(g.v);
    }
    /* --- END FRAME: meta=4/SOUTH+eye -> 13/16 base plus 8x3x8 eye. --- */
    {
        GList g = {0};
        const float bf[3] = {0,0,0}, bt[3] = {16,13,16};
        const float full[4] = {0,0,16,16}, side[4] = {0,3,16,16};
        for (int face=0; face<6; ++face) {
            int sprite = face == BM_DOWN ? CR_SPRITE_END_STONE
                       : face == BM_UP ? CR_SPRITE_ENDFRAME_TOP
                       : CR_SPRITE_ENDFRAME_SIDE;
            golden_face_custom(&g, xef, TY, zef, bf, bt, face,
                               face >= BM_NORTH ? side : full,
                               0,3,0.0f,NULL,0,sprite);
        }
        const float ef[3] = {4,13,4}, et[3] = {12,16,12};
        const float eye_tb[4] = {4,4,12,12}, eye_side[4] = {4,0,12,3};
        for (int face=0; face<6; ++face)
            golden_face_custom(&g, xef, TY, zef, ef, et, face,
                               face >= BM_NORTH ? eye_side : eye_tb,
                               0,3,0.0f,NULL,0,CR_SPRITE_ENDFRAME_EYE);
        int n = collect(&m, CR_LAYER_SOLID, xef, zef, ylo, yhi, got, 512);
        CHECK(n == 72, "end frame: %d verts (want 72 base+eye)", n);
        CHECK(multiset_eq(got, n, &g, 1),
              "end frame: base/eye positions/uv differ from JAR models");
        CHECK(bm_block(T_END_FRAME)->kind == BM_KIND_END_FRAME,
              "end frame: wrong kind");
        free(g.v);
    }
    /* --- MAGMA: BlockMagma forces packed sky/block light 15/15. --- */
    {
        CrRgba want = light_lightmap_rgba8(L, 15, 15);
        int found_fullbright_up = 0;
        for (int i=0; i<m.nverts[CR_LAYER_SOLID]; ++i) {
            const CrVertex *v = &m.verts[CR_LAYER_SOLID][i];
            if (v->pos.x < xm || v->pos.x > xm + 1.0f
                || v->pos.z < zm || v->pos.z > zm + 1.0f
                || fabsf(v->pos.y - (TY + 1.0f)) > 1e-4f)
                continue;
            if (fabsf(v->light - 1.0f) < 1e-6f
                && v->tint.r == want.r && v->tint.g == want.g
                && v->tint.b == want.b) {
                found_fullbright_up = 1;
                break;
            }
        }
        CHECK(found_fullbright_up,
              "magma: no UP vertex with forced packed light 15/15 (%u,%u,%u)",
              want.r, want.g, want.b);
    }
    /* --- LEAVES: capture pins fancyGraphics=false, so this is an opaque SOLID
     * full cube with 36 verts at the eight cube corners. --- */
    {
        int n = collect(&m, CR_LAYER_SOLID, xl, zl, ylo, yhi, got, 512);
        CHECK(n == 36, "leaves: %d verts on SOLID (want 36)", n);
        int all_corners = 1;
        for (int i = 0; i < n; ++i) {
            int okx = fabsf(got[i].x-xl)<1e-4f     || fabsf(got[i].x-(xl+1))<1e-4f;
            int oky = fabsf(got[i].y-TY)<1e-4f      || fabsf(got[i].y-(TY+1))<1e-4f;
            int okz = fabsf(got[i].z-zl)<1e-4f      || fabsf(got[i].z-(zl+1))<1e-4f;
            if (!(okx && oky && okz)) { all_corners = 0; break; }
        }
        CHECK(all_corners, "leaves: a vertex is not at an integer cube corner");
        int mipped = collect(&m, CR_LAYER_CUTOUT_MIPPED, xl, zl, ylo, yhi, got, 512);
        CHECK(mipped == 0, "leaves: %d verts leaked onto CUTOUT_MIPPED", mipped);
    }
    /* --- WATER column: exact double-sided fluid topology and lowered top. --- */
    {
        int n = collect(&m, CR_LAYER_TRANSLUCENT, xw, zw, ylo, yhi, got, 512);
        /* Lower: bottom + four double-sided walls = 9 quads. Upper: double-sided
         * top + four double-sided walls = 10 quads. */
        CHECK(n == 114, "water: %d translucent verts (want 114)", n);
        /* Isolated source cell: each Java corner averages one weighted source
         * and three non-solid air samples -> height 44/63. */
        float want_top = (float)(TY + 1) + 44.0f/63.0f - 0.001f;
        int found_top = 0;
        for (int i=0;i<n;++i) if (fabsf(got[i].y - want_top) < 1e-3f) { found_top = 1; break; }
        CHECK(found_top, "water: no lowered top surface at y=%.3f", want_top);
        int fullbright_top = 0;
        for (int i=0;i<m.nverts[CR_LAYER_TRANSLUCENT];++i) {
            const CrVertex *v=&m.verts[CR_LAYER_TRANSLUCENT][i];
            if (v->pos.x < xw || v->pos.x > xw+1 ||
                v->pos.z < zw || v->pos.z > zw+1 ||
                fabsf(v->pos.y-want_top)>=1e-3f) continue;
            if (fabsf(v->light-1.0f)<1e-7f) fullbright_top++;
        }
        CHECK(fullbright_top==12,
              "water: full-bright double-sided top has %d verts (want 12)",
              fullbright_top);
    }
    /* --- SLOPED LAVA: LEVEL 3 center with diagonal LEVEL 4..7 neighbours.
     * Java corner heights are {1/4, 2/9, 7/36, 1/6}; top rendering subtracts
     * 0.001 from each and all four exposed side planes are inset 0.001. --- */
    {
        int n=collect_cell_quads(&m,CR_LAYER_SOLID,xlv,ylv,zlv,got,512);
        CHECK(n==66,"sloped lava: %d verts (want 66 = 11 quads)",n);
        const float h[4]={1.0f/4.0f,2.0f/9.0f,7.0f/36.0f,1.0f/6.0f};
        const float px[4]={xlv,xlv,xlv+1,xlv+1};
        const float pz[4]={zlv,zlv+1,zlv+1,zlv};
        int top_seen[4]={0,0,0,0};
        int north_inset=0,south_inset=0,west_inset=0,east_inset=0;
        for(int i=0;i<n;++i){
            for(int c=0;c<4;++c)
                if(fabsf(got[i].x-px[c])<1e-5f
                    && fabsf(got[i].z-pz[c])<1e-5f
                    && fabsf(got[i].y-((float)ylv+h[c]-0.001f))<1e-5f)
                    top_seen[c]=1;
            if(fabsf(got[i].z-((float)zlv+0.001f))<1e-5f) north_inset=1;
            if(fabsf(got[i].z-((float)zlv+0.999f))<1e-5f) south_inset=1;
            if(fabsf(got[i].x-((float)xlv+0.001f))<1e-5f) west_inset=1;
            if(fabsf(got[i].x-((float)xlv+0.999f))<1e-5f) east_inset=1;
        }
        CHECK(top_seen[0]&&top_seen[1]&&top_seen[2]&&top_seen[3],
              "sloped lava: exact per-corner top heights missing");
        CHECK(north_inset&&south_inset&&west_inset&&east_inset,
              "sloped lava: side offsets missing N=%d S=%d W=%d E=%d",
              north_inset,south_inset,west_inset,east_inset);
    }
    /* --- LILY: zero-thickness plane at y=0.25/16, UP+DOWN only = 12 verts,
     * facebakery-golden, MUST NOT be a full cube (old bug: BM_KIND_CUBE). --- */
    {
        GList g = {0};
        float from[3] = {0.0f, 0.25f, 0.0f}, to[3] = {16.0f, 0.25f, 16.0f};
        int spr = CR_SPRITE_WATERLILY;
        golden_face(&g, xli, TY, zli, from, to, BM_DOWN, 0, 3, 0.0f, NULL, 0, spr);
        golden_face(&g, xli, TY, zli, from, to, BM_UP,   0, 3, 0.0f, NULL, 0, spr);
        /* waterlily blockstate: position-random y 0/90/180/270 variant */
        extern int worldmc_test_fire_variant(int,int,int,int,int);
        golden_rotate_y(&g, xli, zli, worldmc_test_fire_variant(xli, TY, zli, 0, 4));
        int n = collect(&m, CR_LAYER_CUTOUT, xli, zli, ylo, yhi, got, 512);
        CHECK(n == 12, "lily: %d verts (want 12 = 2 faces)", n);
        CHECK(multiset_eq(got, n, &g, 1), "lily: positions/uv do not match facebakery golden");
        /* every vert y must sit at TY + 0.25/16 - never a full-cube side */
        float want_y = (float)TY + 0.25f / 16.0f;
        int all_thin = 1;
        for (int i = 0; i < n; ++i) {
            if (fabsf(got[i].y - want_y) > 1e-3f) { all_thin = 0; break; }
        }
        CHECK(all_thin, "lily: verts not on the y=0.25 plane (full-cube regression?)");
        /* must not leak to solid / mipped / translucent */
        CHECK(collect(&m, CR_LAYER_SOLID, xli, zli, ylo, yhi, got, 512) == 0,
              "lily: leaked onto SOLID");
        free(g.v);
        /* tint class must be fixed lily green, not foliage */
        CHECK(bm_block(T_LILY)->kind == BM_KIND_LILY, "lily: kind is not BM_KIND_LILY");
        CHECK(bm_block(T_LILY)->face[BM_UP].tint == BM_TINT_LILY,
              "lily: tint is not BM_TINT_LILY (fixed 0x208030)");
        CHECK(bm_block(T_LILY)->is_full_cube == 0, "lily: is_full_cube must be 0");
    }
    /* --- SNOW LAYER: box y 0..2/16, SOLID, verts must not reach full cube top --- */
    {
        GList g = {0};
        float from[3] = {0,0,0}, to[3] = {16,2,16};
        golden_box(&g, xsn, TY, zsn, from, to, CR_SPRITE_SNOW);
        int n = collect(&m, CR_LAYER_SOLID, xsn, zsn, ylo, yhi, got, 512);
        CHECK(n == 36, "snow: %d verts (want 36 = 6 faces)", n);
        CHECK(multiset_eq(got, n, &g, 1), "snow: positions/uv do not match facebakery golden");
        float max_y = -1e9f;
        for (int i = 0; i < n; ++i) if (got[i].y > max_y) max_y = got[i].y;
        float want_top = (float)TY + 2.0f / 16.0f;
        CHECK(fabsf(max_y - want_top) < 1e-3f, "snow: max y=%.4f want %.4f (full-cube regression?)",
              max_y, want_top);
        CHECK(bm_block(T_SNOW)->kind == BM_KIND_SNOW_LAYER, "snow: kind");
        free(g.v);
    }
    /* --- VINE: 2 faces (N/S plane), CUTOUT, 12 verts --- */
    {
        GList g = {0};
        float from[3] = {0,0,15.2f}, to[3] = {16,16,15.2f};
        golden_face(&g, xv, TY, zv, from, to, BM_NORTH, 0, 3, 0.0f, NULL, 0, CR_SPRITE_VINE);
        golden_face(&g, xv, TY, zv, from, to, BM_SOUTH, 0, 3, 0.0f, NULL, 0, CR_SPRITE_VINE);
        int n = collect(&m, CR_LAYER_CUTOUT, xv, zv, ylo, yhi, got, 512);
        CHECK(n == 12, "vine: %d verts (want 12)", n);
        CHECK(multiset_eq(got, n, &g, 1), "vine: positions/uv do not match facebakery golden");
        free(g.v);
    }
    /* --- CACTUS: 6 faces (top/bottom + 4 sides), CUTOUT, 36 verts --- */
    {
        int n = collect(&m, CR_LAYER_CUTOUT, xca, zca, ylo, yhi, got, 512);
        CHECK(n == 36, "cactus: %d verts (want 36)", n);
        CHECK(bm_block(T_CACTUS)->kind == BM_KIND_CACTUS, "cactus: kind");
        /* sides inset: some verts at x=xca+1/16 or z=zca+1/16 */
        int inset = 0;
        for (int i = 0; i < n; ++i) {
            if (fabsf(got[i].x - ((float)xca + 1.0f/16.0f)) < 1e-3f ||
                fabsf(got[i].z - ((float)zca + 1.0f/16.0f)) < 1e-3f)
                inset = 1;
        }
        CHECK(inset, "cactus: no inset side verts (expected 1-unit inset)");
    }

    printf("layer totals: SOLID=%d CUTOUT_MIPPED=%d CUTOUT=%d TRANSLUCENT=%d\n",
           m.nverts[0], m.nverts[1], m.nverts[2], m.nverts[3]);
    worldmc_free_mesh(&m);
    worldmc_destroy(w);

    if (g_fail) { printf("FAIL\n"); return 1; }
    printf("PASS\n");
    return 0;
}
