/* map_gen_fortress: exact C port of MC 1.11.2 MapGenNetherBridge (nether fortress).
 * StructureNetherBridgePieces piece tree + block placement. Verified verbatim-Java == CPU == CUDA.
 * Harness: synthetic all-NETHERRACK ChunkPrimer, MapGenBase.generate (range=8) + generateStructure
 * for chunk (0,0), dump full 65536-cell primer (%04x). Block-state ids = vanilla numeric ids (meta 0).
 * Netherite-csrc reference adapted; spawn check matches MapGenNetherBridge.canSpawnStructureAtCoords. */
#ifndef MC_MAP_GEN_FORTRESS_H
#define MC_MAP_GEN_FORTRESS_H

#include "mc.h"
#include "mc_rng.h"
#include <string.h>

enum {
    FT_AIR = 0, FT_LAVA = 10, FT_MOB_SPAWNER = 52,
    FT_NETHERRACK = 87, FT_SOUL_SAND = 88, FT_NETHER_WART = 115,
    FT_NETHER_BRICK = 112, FT_NETHER_FENCE = 113, FT_NETHER_STAIRS = 114
};

#define FT_RANGE 8
#define FT_MAX_PIECES 64
#define MAX_STRUCT_PIECES FT_MAX_PIECES

enum {
    FT_P_START = 0, FT_P_STRAIGHT, FT_P_CROSSING, FT_P_CROSSING3, FT_P_STAIRS,
    FT_P_THRONE, FT_P_ENTRANCE, FT_P_CORRIDOR, FT_P_CORRIDOR2, FT_P_CORRIDOR3,
    FT_P_CORRIDOR4, FT_P_CORRIDOR5, FT_P_NETHER_STALK, FT_P_END
};

typedef struct { int minX, minY, minZ, maxX, maxY, maxZ; } FtBB;

typedef struct {
    int type, weight, max_count, cur_count, allow_in_row;
} FtPieceWeight;

#define FT_P_PRIMARY_COUNT 6
#define FT_P_SECONDARY_COUNT 6

typedef struct {
    int type, coord_base, component_type, has_spawner;
    FtBB bb;
} FtPiece;

typedef struct {
    int cx, cz, piece_count, valid;
    FtBB total_bb;
    FtPiece pieces[MAX_STRUCT_PIECES];
    FtPieceWeight pri[FT_P_PRIMARY_COUNT];
    FtPieceWeight sec[FT_P_SECONDARY_COUNT];
} FtStart;

#ifndef MC_CHUNK_PROVIDER_H
typedef struct { u16 data[65536]; } ChunkPrimer;
#endif

typedef struct { ChunkPrimer *primer; int chunkX, chunkZ; i64 worldSeed; } FtWorld;

MC_HD static inline int ft_idx(int x, int y, int z) { return x << 12 | z << 8 | y; }
MC_HD MC_NOINLINE static int ft_in_chunk(int x, int y, int z, int cx, int cz) {
    return x >= cx*16 && x < cx*16+16 && z >= cz*16 && z < cz*16+16 && y >= 0 && y < 256;
}
MC_HD MC_NOINLINE static void ft_set(FtWorld *w, int x, int y, int z, int v) {
    if (ft_in_chunk(x,y,z,w->chunkX,w->chunkZ))
        w->primer->data[ft_idx(x & 15, y, z & 15)] = (u16)v;
}
MC_HD MC_NOINLINE static int ft_get(FtWorld *w, int x, int y, int z) {
    if (!ft_in_chunk(x,y,z,w->chunkX,w->chunkZ)) return FT_AIR;
    return (int)w->primer->data[ft_idx(x & 15, y, z & 15)];
}
MC_HD static inline int ft_is_solid(int b) { return b != FT_AIR; }


MC_HD MC_NOINLINE static FtBB ftbb_create(int x0,int y0,int z0,int x1,int y1,int z1){
    FtBB b; b.minX=x0;b.minY=y0;b.minZ=z0;b.maxX=x1;b.maxY=y1;b.maxZ=z1; return b;
}
MC_HD MC_NOINLINE static int ftbb_intersects(const FtBB *a,const FtBB *b){
    return a->maxX>=b->minX&&a->minX<=b->maxX&&a->maxZ>=b->minZ&&a->minZ<=b->maxZ&&a->maxY>=b->minY&&a->minY<=b->maxY;
}
MC_HD MC_NOINLINE static int ftbb_contains(const FtBB *bb,int x,int y,int z){
    return x>=bb->minX&&x<=bb->maxX&&z>=bb->minZ&&z<=bb->maxZ&&y>=bb->minY&&y<=bb->maxY;
}
MC_HD MC_NOINLINE static void ftbb_expand(FtBB *a,const FtBB *b){
    if(b->minX<a->minX)a->minX=b->minX; if(b->minY<a->minY)a->minY=b->minY; if(b->minZ<a->minZ)a->minZ=b->minZ;
    if(b->maxX>a->maxX)a->maxX=b->maxX; if(b->maxY>a->maxY)a->maxY=b->maxY; if(b->maxZ>a->maxZ)a->maxZ=b->maxZ;
}
MC_HD static inline int ftbb_above_ground(const FtBB *bb){ return bb && bb->minY>10; }
MC_HD MC_NOINLINE static int ft_get_x(const FtPiece *p,int x,int z){
    switch(p->coord_base){case 0:case 2:return p->bb.minX+x;case 1:return p->bb.maxX-z;case 3:return p->bb.minX+z;default:return x;}
}
MC_HD static inline int ft_get_y(const FtPiece *p,int y){ return p->coord_base==-1?y:y+p->bb.minY; }
MC_HD MC_NOINLINE static int ft_get_z(const FtPiece *p,int x,int z){
    switch(p->coord_base){case 0:return p->bb.minZ+z;case 1:case 3:return p->bb.minZ+x;case 2:return p->bb.maxZ-z;default:return z;}
}
MC_HD MC_NOINLINE static FtBB ftbb_component_bb(int x,int y,int z,int ox,int oy,int oz,int sx,int sy,int sz,int cb){
    FtBB bb;
    switch(cb){
    case 0: bb.minX=x+ox; bb.minY=y+oy; bb.minZ=z+oz; bb.maxX=x+sx-1+ox; bb.maxY=y+sy-1+oy; bb.maxZ=z+sz-1+oz; break;
    case 1: bb.minX=x-sz+1+oz; bb.minY=y+oy; bb.minZ=z+ox; bb.maxX=x+oz; bb.maxY=y+sy-1+oy; bb.maxZ=z+sx-1+ox; break;
    case 2: bb.minX=x+ox; bb.minY=y+oy; bb.minZ=z-sz+1+oz; bb.maxX=x+sx-1+ox; bb.maxY=y+sy-1+oy; bb.maxZ=z+oz; break;
    case 3: bb.minX=x+oz; bb.minY=y+oy; bb.minZ=z+ox; bb.maxX=x+sz-1+oz; bb.maxY=y+sy-1+oy; bb.maxZ=z+sx-1+ox; break;
    default: bb.minX=x+ox; bb.minY=y+oy; bb.minZ=z+oz; bb.maxX=x+sx-1+ox; bb.maxY=y+sy-1+oy; bb.maxZ=z+sz-1+oz;
    }
    return bb;
}
MC_HD MC_NOINLINE static void ft_place(FtWorld *w,const FtPiece *p,const FtBB *clip,int id,int meta,int lx,int ly,int lz){
    int wx=ft_get_x(p,lx,lz), wy=ft_get_y(p,ly), wz=ft_get_z(p,lx,lz);
    (void)meta;
    if(ftbb_contains(clip,wx,wy,wz)) ft_set(w,wx,wy,wz,id);
}
MC_HD MC_NOINLINE static int ft_get_local(FtWorld *w,const FtPiece *p,const FtBB *clip,int lx,int ly,int lz){
    int wx=ft_get_x(p,lx,lz), wy=ft_get_y(p,ly), wz=ft_get_z(p,lx,lz);
    if(!ftbb_contains(clip,wx,wy,wz)) return FT_AIR;
    return ft_get(w,wx,wy,wz);
}
MC_HD MC_NOINLINE static void ft_fill(FtWorld *w,const FtPiece *p,const FtBB *clip,int x0,int y0,int z0,int x1,int y1,int z1,int outer,int inner,int air_only){
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) for(int z=z0;z<=z1;z++){
        if(air_only && ft_get_local(w,p,clip,x,y,z)==FT_AIR) continue;
        int edge=(y==y0||y==y1||x==x0||x==x1||z==z0||z==z1);
        ft_place(w,p,clip,edge?outer:inner,0,x,y,z);
    }
}
MC_HD MC_NOINLINE static void ft_replace_down(FtWorld *w,const FtPiece *p,const FtBB *clip,int id,int meta,int lx,int ly,int lz){
    int wx=ft_get_x(p,lx,lz), wz=ft_get_z(p,lx,lz), wy=ft_get_y(p,ly);
    (void)meta;
    while(wy>0 && ftbb_contains(clip,wx,wy,wz)){
        int cur=ft_get(w,wx,wy,wz);
        if(cur!=FT_AIR && ft_is_solid(cur)) break;
        ft_set(w,wx,wy,wz,id); wy--;
    }
}
MC_HD MC_NOINLINE static int ft_find_intersect(const FtPiece *ps,int n,const FtBB *bb){
    for(int i=0;i<n;i++) if(ftbb_intersects(&ps[i].bb,bb)) return i;
    return -1;
}


/* ============================================================
 * Nether Fortress -- port of MC 1.7.10 StructureNetherBridgePieces
 * ============================================================ */

/* ---- Piece Weight System ---- */

MC_HD MC_NOINLINE static void ft_init_primary(FtPieceWeight *w) {
    w[0] = (FtPieceWeight){ FT_P_STRAIGHT,   30, 0, 0, 1 };
    w[1] = (FtPieceWeight){ FT_P_CROSSING3,  10, 4, 0, 0 };
    w[2] = (FtPieceWeight){ FT_P_CROSSING,   10, 4, 0, 0 };
    w[3] = (FtPieceWeight){ FT_P_STAIRS,     10, 3, 0, 0 };
    w[4] = (FtPieceWeight){ FT_P_THRONE,      5, 2, 0, 0 };
    w[5] = (FtPieceWeight){ FT_P_ENTRANCE,    5, 1, 0, 0 };
}
MC_HD MC_NOINLINE static void ft_init_secondary(FtPieceWeight *w) {
    w[0] = (FtPieceWeight){ FT_P_CORRIDOR5,   25, 0, 0, 1 };
    w[1] = (FtPieceWeight){ FT_P_CORRIDOR2,    1, 0, 0, 1 };
    w[2] = (FtPieceWeight){ FT_P_CORRIDOR,     1, 0, 0, 1 };
    w[3] = (FtPieceWeight){ FT_P_CORRIDOR3,    1, 0, 0, 1 };
    w[4] = (FtPieceWeight){ FT_P_CORRIDOR4,    1, 0, 0, 1 };
    w[5] = (FtPieceWeight){ FT_P_NETHER_STALK, 1, 0, 0, 1 };
}
MC_HD MC_NOINLINE static void ft_reset_weights(FtStart *start) {
    ft_init_primary(start->pri);
    ft_init_secondary(start->sec);
}

/* ---- Spawn Check ---- */

MC_HD MC_NOINLINE static int ft_can_spawn(i64 seed, int cx, int cz) {
    int k = cx >> 4;
    int l = cz >> 4;

    JavaRandom r;
    jrand_set(&r, (i64)((int32_t)k ^ ((int32_t)l << 4)) ^ seed);
    jrand_int(&r); /* consume 1 */

    if (jrand_int_bound(&r, 3) != 0) return 0;

    int expected_cx = (k << 4) + 4 + jrand_int_bound(&r, 8);
    if (cx != expected_cx) return 0;

    int expected_cz = (l << 4) + 4 + jrand_int_bound(&r, 8);
    return cz == expected_cz;
}

/* ---- Piece Creation Functions ---- */

/* Forward declarations */
MC_HD MC_NOINLINE static FtPiece *ft_add_piece(FtStart *start);
MC_HD MC_NOINLINE static void ft_build_component(FtStart *start, FtPiece *piece,
                                  JavaRandom *r, int depth);

MC_HD MC_NOINLINE static FtPiece *ft_add_piece(FtStart *start) {
    if (start->piece_count >= MAX_STRUCT_PIECES) return NULL;
    FtPiece *p = &start->pieces[start->piece_count++];
    memset(p, 0, sizeof(*p));
    return p;
}

/* Create bounding boxes for each piece type */
MC_HD MC_NOINLINE static int ft_create_straight(FtStart *start, JavaRandom *r,
                                  int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -1, -3, 0, 5, 10, 19, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_STRAIGHT;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_crossing3(FtStart *start, JavaRandom *r,
                                   int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -8, -3, 0, 19, 10, 19, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_CROSSING3;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_crossing(FtStart *start, JavaRandom *r,
                                  int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -2, 0, 0, 7, 9, 7, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_CROSSING;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_stairs(FtStart *start, JavaRandom *r,
                                int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -2, 0, 0, 7, 11, 7, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_STAIRS;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_throne(FtStart *start, JavaRandom *r,
                                int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -2, 0, 0, 7, 8, 9, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_THRONE;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    p->has_spawner = 0;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_entrance(FtStart *start, JavaRandom *r,
                                  int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -5, -3, 0, 13, 14, 13, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_ENTRANCE;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_corridor5(FtStart *start, JavaRandom *r,
                                   int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -1, 0, 0, 5, 7, 5, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_CORRIDOR5;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_corridor(FtStart *start, JavaRandom *r,
                                  int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -1, 0, 0, 5, 7, 5, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_CORRIDOR;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_corridor2(FtStart *start, JavaRandom *r,
                                   int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -1, 0, 0, 5, 7, 5, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_CORRIDOR2;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_corridor3(FtStart *start, JavaRandom *r,
                                   int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -1, -7, 0, 5, 14, 10, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_CORRIDOR3;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_corridor4(FtStart *start, JavaRandom *r,
                                   int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -1, 0, 0, 5, 7, 5, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_CORRIDOR4;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_nether_stalk(FtStart *start, JavaRandom *r,
                                      int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -5, -3, 0, 13, 14, 13, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_NETHER_STALK;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_end(FtStart *start, JavaRandom *r,
                             int x, int y, int z, int dir, int depth)
{
    FtBB bb = ftbb_component_bb(x, y, z, -1, -3, 0, 5, 10, 8, dir);
    if (!ftbb_above_ground(&bb)) return 0;
    if (ft_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    FtPiece *p = ft_add_piece(start);
    if (!p) return 0;
    p->type = FT_P_END;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int ft_create_by_type(FtStart *start, JavaRandom *r,
                                 int type, int x, int y, int z, int dir, int depth)
{
    switch (type) {
        case FT_P_STRAIGHT:     return ft_create_straight(start, r, x, y, z, dir, depth);
        case FT_P_CROSSING3:    return ft_create_crossing3(start, r, x, y, z, dir, depth);
        case FT_P_CROSSING:     return ft_create_crossing(start, r, x, y, z, dir, depth);
        case FT_P_STAIRS:       return ft_create_stairs(start, r, x, y, z, dir, depth);
        case FT_P_THRONE:       return ft_create_throne(start, r, x, y, z, dir, depth);
        case FT_P_ENTRANCE:     return ft_create_entrance(start, r, x, y, z, dir, depth);
        case FT_P_CORRIDOR5:    return ft_create_corridor5(start, r, x, y, z, dir, depth);
        case FT_P_CORRIDOR:     return ft_create_corridor(start, r, x, y, z, dir, depth);
        case FT_P_CORRIDOR2:    return ft_create_corridor2(start, r, x, y, z, dir, depth);
        case FT_P_CORRIDOR3:    return ft_create_corridor3(start, r, x, y, z, dir, depth);
        case FT_P_CORRIDOR4:    return ft_create_corridor4(start, r, x, y, z, dir, depth);
        case FT_P_NETHER_STALK: return ft_create_nether_stalk(start, r, x, y, z, dir, depth);
        default: return 0;
    }
}

/* ---- Piece Tree Building ---- */

/* Get exit position for a piece going forward (Normal direction) */
MC_HD MC_NOINLINE static void ft_get_normal_exit(const FtPiece *p, int offX, int offY,
                                  int *ox, int *oy, int *oz, int *odir)
{
    *odir = p->coord_base;
    *oy = p->bb.minY + offY;
    switch (p->coord_base) {
        case 0: *ox = p->bb.minX + offX; *oz = p->bb.maxZ + 1; break;
        case 1: *ox = p->bb.minX - 1;    *oz = p->bb.minZ + offX; break;
        case 2: *ox = p->bb.minX + offX; *oz = p->bb.minZ - 1; break;
        case 3: *ox = p->bb.maxX + 1;    *oz = p->bb.minZ + offX; break;
        default: *ox = p->bb.minX + offX; *oz = p->bb.maxZ + 1; break;
    }
}

/* Get exit going left (X direction in MC terms) */
MC_HD MC_NOINLINE static void ft_get_x_exit(const FtPiece *p, int offY, int offZ,
                              int *ox, int *oy, int *oz, int *odir)
{
    *oy = p->bb.minY + offY;
    switch (p->coord_base) {
        case 0: *ox = p->bb.minX - 1;    *oz = p->bb.minZ + offZ; *odir = 1; break;
        case 1: *ox = p->bb.minX + offZ; *oz = p->bb.minZ - 1;    *odir = 2; break;
        case 2: *ox = p->bb.minX - 1;    *oz = p->bb.minZ + offZ; *odir = 1; break;
        case 3: *ox = p->bb.minX + offZ; *oz = p->bb.minZ - 1;    *odir = 2; break;
        default: *ox = p->bb.minX - 1;   *oz = p->bb.minZ + offZ; *odir = 1; break;
    }
}

/* Get exit going right (Z direction in MC terms) */
MC_HD MC_NOINLINE static void ft_get_z_exit(const FtPiece *p, int offY, int offZ,
                              int *ox, int *oy, int *oz, int *odir)
{
    *oy = p->bb.minY + offY;
    switch (p->coord_base) {
        case 0: *ox = p->bb.maxX + 1;    *oz = p->bb.minZ + offZ; *odir = 3; break;
        case 1: *ox = p->bb.minX + offZ; *oz = p->bb.maxZ + 1;    *odir = 0; break;
        case 2: *ox = p->bb.maxX + 1;    *oz = p->bb.minZ + offZ; *odir = 3; break;
        case 3: *ox = p->bb.minX + offZ; *oz = p->bb.maxZ + 1;    *odir = 0; break;
        default: *ox = p->bb.maxX + 1;   *oz = p->bb.minZ + offZ; *odir = 3; break;
    }
}

/* Get total weight of available pieces in a weight list */
MC_HD MC_NOINLINE static int ft_total_weight(FtPieceWeight *weights, int count) {
    int any_limited = 0;
    int total = 0;
    for (int i = 0; i < count; i++) {
        if (weights[i].max_count > 0 && weights[i].cur_count < weights[i].max_count) {
            any_limited = 1;
        }
        total += weights[i].weight;
    }
    return any_limited ? total : -1;
}

/* Try to add next piece from weight list */
MC_HD MC_NOINLINE static int ft_add_next_from_weights(FtStart *start, JavaRandom *r,
                                        FtPieceWeight *weights, int wcount,
                                        int *last_weight_idx,
                                        int x, int y, int z, int dir, int depth)
{
    int total = ft_total_weight(weights, wcount);
    if (total <= 0 || depth > 30) return 0;

    for (int attempt = 0; attempt < 5; attempt++) {
        int roll = jrand_int_bound(r, total);
        for (int i = 0; i < wcount; i++) {
            roll -= weights[i].weight;
            if (roll < 0) {
                /* Check depth limit for this piece type */
                if (weights[i].max_count > 0 && weights[i].cur_count >= weights[i].max_count) {
                    break;
                }
                /* Don't allow same piece type consecutively (unless allow_in_row) */
                if (i == *last_weight_idx && !weights[i].allow_in_row) {
                    break;
                }

                int old_count = start->piece_count;
                if (ft_create_by_type(start, r, weights[i].type, x, y, z, dir, depth)) {
                    weights[i].cur_count++;
                    *last_weight_idx = i;
                    /* Remove exhausted weights (handled by total_weight returning -1) */
                    return 1;
                }
                /* Creation failed (intersection), restore count */
                start->piece_count = old_count;
                break;
            }
        }
    }

    /* Fall back to End piece */
    return ft_create_end(start, r, x, y, z, dir, depth);
}

/* Add next piece in given direction.
 * start_piece_idx: index of the piece we're building from (not a pointer, since
 * the pieces array may realloc via ft_add_piece). */
MC_HD MC_NOINLINE static void ft_add_next(FtStart *start, JavaRandom *r,
                            int start_piece_idx,
                            int x, int y, int z, int dir, int depth,
                            int secondary)
{
    /* Distance check (112 blocks from start) */
    if (abs(x - start->pieces[0].bb.minX) > 112 ||
        abs(z - start->pieces[0].bb.minZ) > 112)
    {
        ft_create_end(start, r, x, y, z, dir, depth);
        return;
    }

    FtPieceWeight *weights = secondary ? start->sec : start->pri;
    int wcount = secondary ? FT_P_SECONDARY_COUNT : FT_P_PRIMARY_COUNT;
    int dummy_last = -1;

    int old_count = start->piece_count;
    if (ft_add_next_from_weights(start, r, weights, wcount, &dummy_last,
                                    x, y, z, dir, depth + 1))
    {
        /* Recursively build from the newly added piece */
        for (int i = old_count; i < start->piece_count; i++) {
            ft_build_component(start, &start->pieces[i], r, depth + 1);
        }
    }
}

/* Build child components from a piece */
MC_HD MC_NOINLINE static void ft_build_component(FtStart *start, FtPiece *piece,
                                  JavaRandom *r, int depth)
{
    if (depth > 30 || start->piece_count >= MAX_STRUCT_PIECES - 10) return;

    int pidx = (int)(piece - start->pieces);
    int ox, oy, oz, odir;

    switch (piece->type) {
        case FT_P_STRAIGHT:
            ft_get_normal_exit(piece, 1, 3, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 0);
            break;

        case FT_P_CROSSING3:
        case FT_P_START:
            ft_get_normal_exit(piece, 8, 3, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 0);
            ft_get_x_exit(piece, 3, 8, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 0);
            ft_get_z_exit(piece, 3, 8, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 0);
            break;

        case FT_P_CROSSING:
            ft_get_normal_exit(piece, 2, 0, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 0);
            ft_get_x_exit(piece, 0, 2, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 0);
            ft_get_z_exit(piece, 0, 2, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 0);
            break;

        case FT_P_STAIRS:
            ft_get_normal_exit(piece, 1, 0, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 1);
            break;

        case FT_P_ENTRANCE:
            ft_get_normal_exit(piece, 5, 3, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 1);
            break;

        case FT_P_CORRIDOR5:
            ft_get_normal_exit(piece, 1, 0, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 1);
            break;

        case FT_P_CORRIDOR:
            ft_get_normal_exit(piece, 1, 0, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 1);
            break;

        case FT_P_CORRIDOR3:
            ft_get_normal_exit(piece, 1, 0, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 1);
            break;

        case FT_P_CORRIDOR4:
            ft_get_normal_exit(piece, 1, 0, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 1);
            ft_get_x_exit(piece, 0, 1, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 1);
            break;

        case FT_P_CORRIDOR2:
            ft_get_normal_exit(piece, 1, 0, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 1);
            ft_get_z_exit(piece, 0, 1, &ox, &oy, &oz, &odir);
            ft_add_next(start, r, pidx, ox, oy, oz, odir, depth, 1);
            break;

        default:
            break;
    }
}

/* ---- Structure Generation ---- */

MC_HD MC_NOINLINE static void ft_generate(FtStart *start, i64 seed, int cx, int cz) {
    memset(start, 0, sizeof(*start));
    start->cx = cx;
    start->cz = cz;
    ft_reset_weights(start);

    JavaRandom r;
    jrand_set(&r, seed ^ ((i64)cx * 341873128712LL + (i64)cz * 132897987541LL));

    /* Start piece: Crossing3 centered on chunk */
    int dir = jrand_int_bound(&r, 4);
    int wx = cx * 16 + 2;
    int wz = cz * 16 + 2;
    int wy = 64;

    FtPiece *sp = ft_add_piece(start);
    if (!sp) return;
    sp->type = FT_P_CROSSING3;
    sp->coord_base = dir;
    sp->component_type = 0;

    switch (dir) {
        case 0: case 2:
            sp->bb = ftbb_create(wx, wy, wz, wx + 19 - 1, wy + 9, wz + 19 - 1);
            break;
        default:
            sp->bb = ftbb_create(wx, wy, wz, wx + 19 - 1, wy + 9, wz + 19 - 1);
            break;
    }

    /* Build piece tree from start */
    ft_build_component(start, sp, &r, 0);

    /* Compute total bounding box */
    start->total_bb = start->pieces[0].bb;
    for (int i = 1; i < start->piece_count; i++) {
        ftbb_expand(&start->total_bb, &start->pieces[i].bb);
    }
    start->valid = 1;
}

/* ---- Block Placement ---- */

MC_HD MC_NOINLINE static void ft_place_straight(FtWorld *w, FtPiece *p, const FtBB *clip) {
    ft_fill(w, p, clip, 0, 3, 0, 4, 4, 18, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 1, 5, 0, 3, 7, 18, FT_AIR, FT_AIR, 0);
    ft_fill(w, p, clip, 0, 5, 0, 0, 5, 18, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 4, 5, 0, 4, 5, 18, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 2, 0, 4, 2, 5, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 2, 13, 4, 2, 18, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 0, 0, 4, 1, 3, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 0, 15, 4, 1, 18, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);

    for (int i = 0; i <= 4; i++) {
        for (int j = 0; j <= 2; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, 18 - j);
        }
    }

    /* Fence railings */
    ft_fill(w, p, clip, 0, 1, 1, 0, 4, 1, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 0, 3, 4, 0, 4, 4, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 0, 3, 14, 0, 4, 14, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 0, 1, 17, 0, 4, 17, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 4, 1, 1, 4, 4, 1, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 4, 3, 4, 4, 4, 4, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 4, 3, 14, 4, 4, 14, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 4, 1, 17, 4, 4, 17, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
}

MC_HD MC_NOINLINE static void ft_place_crossing3(FtWorld *w, FtPiece *p, const FtBB *clip) {
    /* Main bridge paths */
    ft_fill(w, p, clip, 7, 3, 0, 11, 4, 18, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 3, 7, 18, 4, 11, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);

    /* Clear air above paths */
    ft_fill(w, p, clip, 8, 5, 0, 10, 7, 18, FT_AIR, FT_AIR, 0);
    ft_fill(w, p, clip, 0, 5, 8, 18, 7, 10, FT_AIR, FT_AIR, 0);

    /* Side walls */
    ft_fill(w, p, clip, 7, 5, 0, 7, 5, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 7, 5, 11, 7, 5, 18, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 11, 5, 0, 11, 5, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 11, 5, 11, 11, 5, 18, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 5, 7, 7, 5, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 11, 5, 7, 18, 5, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 5, 11, 7, 5, 11, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 11, 5, 11, 18, 5, 11, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);

    /* Corner pillars */
    ft_fill(w, p, clip, 7, 2, 0, 7, 2, 5, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 7, 2, 13, 7, 2, 18, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 11, 2, 0, 11, 2, 5, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 11, 2, 13, 11, 2, 18, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 0, 2, 7, 5, 2, 7, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 13, 2, 7, 18, 2, 7, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 0, 2, 11, 5, 2, 11, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 13, 2, 11, 18, 2, 11, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);

    /* Foundation */
    for (int i = 7; i <= 11; i++) {
        for (int j = 0; j <= 18; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
        }
    }
    for (int i = 0; i <= 18; i++) {
        for (int j = 7; j <= 11; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
        }
    }
}

MC_HD MC_NOINLINE static void ft_place_crossing(FtWorld *w, FtPiece *p, const FtBB *clip) {
    ft_fill(w, p, clip, 0, 0, 0, 6, 1, 6, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 2, 0, 6, 7, 6, FT_AIR, FT_AIR, 0);

    /* Corner pillars */
    ft_fill(w, p, clip, 0, 2, 0, 1, 6, 0, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 2, 6, 1, 6, 6, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 5, 2, 0, 6, 6, 0, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 5, 2, 6, 6, 6, 6, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 2, 0, 0, 6, 1, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 2, 5, 0, 6, 6, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 6, 2, 0, 6, 6, 1, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 6, 2, 5, 6, 6, 6, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);

    /* Top fence */
    ft_fill(w, p, clip, 2, 6, 0, 4, 6, 0, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 2, 5, 0, 4, 5, 0, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 2, 6, 6, 4, 6, 6, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 2, 5, 6, 4, 5, 6, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 0, 6, 2, 0, 6, 4, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 5, 2, 0, 5, 4, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 6, 6, 2, 6, 6, 4, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 6, 5, 2, 6, 5, 4, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);

    for (int i = 0; i <= 6; i++) {
        for (int j = 0; j <= 6; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
        }
    }
}

MC_HD MC_NOINLINE static void ft_place_throne(FtWorld *w, FtPiece *p, const FtBB *clip) {
    /* Clear air */
    ft_fill(w, p, clip, 0, 2, 0, 6, 7, 7, FT_AIR, FT_AIR, 0);
    /* Stepped floor */
    ft_fill(w, p, clip, 1, 0, 0, 5, 1, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 1, 2, 1, 5, 2, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 1, 3, 2, 5, 3, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 1, 4, 3, 5, 4, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    /* Side walls */
    ft_fill(w, p, clip, 1, 2, 0, 1, 4, 2, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 5, 2, 0, 5, 4, 2, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    /* Pillars */
    ft_fill(w, p, clip, 1, 5, 2, 1, 5, 3, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 5, 5, 2, 5, 5, 3, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    /* Roof */
    ft_fill(w, p, clip, 0, 5, 3, 0, 5, 8, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 6, 5, 3, 6, 5, 8, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 1, 5, 8, 5, 5, 8, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    /* Fence posts */
    ft_place(w, p, clip, FT_NETHER_FENCE, 0, 1, 6, 3);
    ft_place(w, p, clip, FT_NETHER_FENCE, 0, 5, 6, 3);
    /* Fence railings */
    ft_fill(w, p, clip, 0, 6, 3, 0, 6, 8, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 6, 6, 3, 6, 6, 8, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 1, 6, 8, 5, 7, 8, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 2, 8, 8, 4, 8, 8, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);

    /* BLAZE SPAWNER -- critical for game progression */
    if (!p->has_spawner) {
        int wy = ft_get_y(p, 5);
        int wx = ft_get_x(p, 3, 5);
        int wz = ft_get_z(p, 3, 5);
        if (ftbb_contains(clip, wx, wy, wz)) {
            p->has_spawner = 1;
            ft_set(w, wx, wy, wz, FT_MOB_SPAWNER);
        }
    }

    /* Foundation */
    for (int i = 0; i <= 6; i++) {
        for (int j = 0; j <= 6; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
        }
    }
}

MC_HD MC_NOINLINE static void ft_place_stairs(FtWorld *w, FtPiece *p, const FtBB *clip) {
    ft_fill(w, p, clip, 0, 0, 0, 6, 1, 6, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 2, 0, 6, 10, 6, FT_AIR, FT_AIR, 0);
    ft_fill(w, p, clip, 0, 2, 0, 1, 8, 0, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 5, 2, 0, 6, 8, 0, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 2, 1, 0, 8, 6, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 6, 2, 1, 6, 8, 6, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 1, 2, 6, 5, 8, 6, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);

    /* Stairs */
    for (int i = 0; i <= 6; i++) {
        /* Nether brick stairs: meta 0=ascending east, 2=ascending south, 3=ascending north */
        ft_place(w, p, clip, FT_NETHER_STAIRS, 0, 1, 2 + i, i);
        ft_place(w, p, clip, FT_NETHER_STAIRS, 0, 2, 2 + i, i);
        ft_place(w, p, clip, FT_NETHER_STAIRS, 0, 3, 2 + i, i);
        ft_place(w, p, clip, FT_NETHER_STAIRS, 0, 4, 2 + i, i);
        ft_place(w, p, clip, FT_NETHER_STAIRS, 0, 5, 2 + i, i);
    }

    for (int i = 0; i <= 6; i++) {
        for (int j = 0; j <= 6; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
        }
    }
}

MC_HD MC_NOINLINE static void ft_place_entrance(FtWorld *w, FtPiece *p, const FtBB *clip) {
    ft_fill(w, p, clip, 0, 3, 0, 12, 4, 12, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 5, 0, 12, 13, 12, FT_AIR, FT_AIR, 0);
    ft_fill(w, p, clip, 0, 5, 0, 1, 12, 12, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 11, 5, 0, 12, 12, 12, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 2, 5, 11, 4, 12, 12, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 8, 5, 11, 10, 12, 12, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 5, 9, 11, 7, 12, 12, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 2, 5, 0, 4, 12, 1, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 8, 5, 0, 10, 12, 1, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 5, 9, 0, 7, 12, 1, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 2, 11, 2, 10, 12, 10, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);

    for (int i = 0; i <= 12; i++) {
        for (int j = 0; j <= 12; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
        }
    }
}

MC_HD MC_NOINLINE static void ft_place_corridor5(FtWorld *w, FtPiece *p, const FtBB *clip) {
    ft_fill(w, p, clip, 0, 0, 0, 4, 1, 4, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 2, 0, 4, 5, 4, FT_AIR, FT_AIR, 0);
    ft_fill(w, p, clip, 0, 2, 0, 0, 5, 4, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 4, 2, 0, 4, 5, 4, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 3, 1, 0, 4, 1, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 0, 3, 3, 0, 4, 3, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 4, 3, 1, 4, 4, 1, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 4, 3, 3, 4, 4, 3, FT_NETHER_FENCE, FT_NETHER_FENCE, 0);
    ft_fill(w, p, clip, 0, 6, 0, 4, 6, 4, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);

    for (int i = 0; i <= 4; i++) {
        for (int j = 0; j <= 4; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
        }
    }
}

MC_HD MC_NOINLINE static void ft_place_nether_stalk(FtWorld *w, FtPiece *p, const FtBB *clip) {
    ft_fill(w, p, clip, 0, 3, 0, 12, 4, 12, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 5, 0, 12, 13, 12, FT_AIR, FT_AIR, 0);
    ft_fill(w, p, clip, 0, 5, 0, 1, 12, 12, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 11, 5, 0, 12, 12, 12, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);

    /* Nether wart farm: soul sand floor with nether wart */
    ft_fill(w, p, clip, 2, 3, 3, 4, 3, 9, FT_SOUL_SAND, FT_SOUL_SAND, 0);
    ft_fill(w, p, clip, 8, 3, 3, 10, 3, 9, FT_SOUL_SAND, FT_SOUL_SAND, 0);

    /* Plant nether wart on soul sand */
    for (int x = 2; x <= 4; x++) {
        for (int z = 3; z <= 9; z++) {
            ft_place(w, p, clip, FT_NETHER_WART, 0, x, 4, z);
        }
    }
    for (int x = 8; x <= 10; x++) {
        for (int z = 3; z <= 9; z++) {
            ft_place(w, p, clip, FT_NETHER_WART, 0, x, 4, z);
        }
    }

    /* Lava in center */
    ft_fill(w, p, clip, 5, 3, 5, 7, 3, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 5, 4, 5, 7, 4, 7, FT_LAVA, FT_LAVA, 0);

    for (int i = 0; i <= 12; i++) {
        for (int j = 0; j <= 12; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
        }
    }
}

MC_HD MC_NOINLINE static void ft_place_end(FtWorld *w, FtPiece *p, const FtBB *clip) {
    ft_fill(w, p, clip, 0, 3, 0, 4, 4, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 1, 5, 0, 3, 7, 7, FT_AIR, FT_AIR, 0);
    ft_fill(w, p, clip, 0, 5, 0, 0, 5, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 4, 5, 0, 4, 5, 7, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);

    for (int i = 0; i <= 4; i++) {
        for (int j = 0; j <= 7; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
        }
    }
}

/* Simple corridor variants -- all similar small enclosed rooms */
MC_HD MC_NOINLINE static void ft_place_corridor_simple(FtWorld *w, FtPiece *p, const FtBB *clip) {
    ft_fill(w, p, clip, 0, 0, 0, 4, 1, 4, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 2, 0, 4, 5, 4, FT_AIR, FT_AIR, 0);
    ft_fill(w, p, clip, 0, 2, 0, 0, 5, 4, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 4, 2, 0, 4, 5, 4, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 6, 0, 4, 6, 4, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);

    for (int i = 0; i <= 4; i++) {
        for (int j = 0; j <= 4; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
        }
    }
}

MC_HD MC_NOINLINE static void ft_place_corridor3(FtWorld *w, FtPiece *p, const FtBB *clip) {
    /* Corridor with staircase going down */
    ft_fill(w, p, clip, 0, 0, 0, 4, 1, 9, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 2, 0, 4, 6, 9, FT_AIR, FT_AIR, 0);
    ft_fill(w, p, clip, 0, 2, 0, 0, 6, 9, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 4, 2, 0, 4, 6, 9, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    ft_fill(w, p, clip, 0, 7, 0, 4, 7, 9, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);

    /* Steps going down */
    for (int i = 0; i < 7; i++) {
        ft_fill(w, p, clip, 1, 2 + i, i, 3, 2 + i, i, FT_NETHER_BRICK, FT_NETHER_BRICK, 0);
    }

    for (int i = 0; i <= 4; i++) {
        for (int j = 0; j <= 9; j++) {
            ft_replace_down(w, p, clip, FT_NETHER_BRICK, 0, i, -1, j);
        }
    }
}

MC_HD MC_NOINLINE static void ft_place_all(FtWorld *w, FtStart *start) {
    /* Use total BB as clip region */
    FtBB clip = start->total_bb;
    /* Expand clip to be generous */
    clip.minX -= 16; clip.minZ -= 16;
    clip.maxX += 16; clip.maxZ += 16;
    clip.minY = 0; clip.maxY = 255;

    for (int i = 0; i < start->piece_count; i++) {
        FtPiece *p = &start->pieces[i];
        switch (p->type) {
            case FT_P_STRAIGHT:     ft_place_straight(w, p, &clip); break;
            case FT_P_CROSSING3:
            case FT_P_START:        ft_place_crossing3(w, p, &clip); break;
            case FT_P_CROSSING:     ft_place_crossing(w, p, &clip); break;
            case FT_P_STAIRS:       ft_place_stairs(w, p, &clip); break;
            case FT_P_THRONE:       ft_place_throne(w, p, &clip); break;
            case FT_P_ENTRANCE:     ft_place_entrance(w, p, &clip); break;
            case FT_P_CORRIDOR5:    ft_place_corridor5(w, p, &clip); break;
            case FT_P_NETHER_STALK: ft_place_nether_stalk(w, p, &clip); break;
            case FT_P_END:          ft_place_end(w, p, &clip); break;
            case FT_P_CORRIDOR:
            case FT_P_CORRIDOR2:
            case FT_P_CORRIDOR4:    ft_place_corridor_simple(w, p, &clip); break;
            case FT_P_CORRIDOR3:    ft_place_corridor3(w, p, &clip); break;
        }
    }
}


/* MapGenBase.generate + MapGenStructure recursiveGenerate + generateStructure for chunk (cx,cz). */
#define FT_MAX_STARTS 8

typedef struct { FtStart starts[FT_MAX_STARTS]; int count; i64 worldSeed; } FtGen;

MC_HD static inline i64 ft_chunk_key(int cx, int cz) { return ((i64)(u32)cx << 32) | (u32)(u32)cz; }

MC_HD MC_NOINLINE static int ft_can_spawn_at(JavaRandom *r, i64 worldSeed, int cx, int cz) {
    int i = cx >> 4, j = cz >> 4;
    jrand_set(r, (i64)((i32)(i ^ (j << 4))) ^ worldSeed);
    jrand_int(r);
    if (jrand_int_bound(r, 3) != 0) return 0;
    int ex = (i << 4) + 4 + jrand_int_bound(r, 8);
    if (cx != ex) return 0;
    int ez = (j << 4) + 4 + jrand_int_bound(r, 8);
    return cz == ez;
}

MC_HD MC_NOINLINE static void ft_gen_recursive(FtGen *g, JavaRandom *r, int chunkX, int chunkZ, int originX, int originZ) {
    i64 key = ft_chunk_key(chunkX, chunkZ);
    for (int i = 0; i < g->count; ++i)
        if (g->starts[i].cx == chunkX && g->starts[i].cz == chunkZ) return;
    jrand_int(r);
    if (!ft_can_spawn_at(r, g->worldSeed, chunkX, chunkZ)) return;
    if (g->count >= FT_MAX_STARTS) return;
    FtStart *s = &g->starts[g->count++];
    ft_generate(s, g->worldSeed, chunkX, chunkZ);
}

MC_HD MC_NOINLINE static void ft_generate_map(FtGen *g, i64 worldSeed, int x, int z) {
    g->count = 0; g->worldSeed = worldSeed;
    int range = FT_RANGE;
    JavaRandom rand; jrand_set(&rand, worldSeed);
    i64 j = jrand_long(&rand), k = jrand_long(&rand);
    for (int l = x - range; l <= x + range; ++l)
        for (int i1 = z - range; i1 <= z + range; ++i1) {
            jrand_set(&rand, (i64)l * j ^ (i64)i1 * k ^ worldSeed);
            ft_gen_recursive(g, &rand, l, i1, x, z);
        }
}

MC_HD MC_NOINLINE static void ft_generate_structure(FtWorld *w, FtGen *g, int cx, int cz) {
    FtBB clip; clip.minX = cx*16; clip.minY = 0; clip.minZ = cz*16;
    clip.maxX = cx*16+15; clip.maxY = 255; clip.maxZ = cz*16+15;
    for (int i = 0; i < g->count; ++i) {
        FtStart *s = &g->starts[i];
        if (!s->valid || !ftbb_intersects(&s->total_bb, &clip)) continue;
        ft_place_all(w, s);
    }
}

MC_HD MC_NOINLINE static void ft_run(ChunkPrimer *primer, i64 seed, int cx, int cz) {
    for (int i = 0; i < 65536; ++i) primer->data[i] = (u16)FT_NETHERRACK;
    FtWorld w; w.primer = primer; w.chunkX = cx; w.chunkZ = cz; w.worldSeed = seed;
    FtGen g;
    memset(&g, 0, sizeof(g));
    ft_generate_map(&g, seed, cx, cz);
    ft_generate_structure(&w, &g, cx, cz);
}

#endif
