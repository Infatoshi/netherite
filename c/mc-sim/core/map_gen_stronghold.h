/* map_gen_stronghold: port of MC 1.11.2 structure generator (stronghold). */
#ifndef MC_MAP_GEN_STRONGHOLD_H
#define MC_MAP_GEN_STRONGHOLD_H
#include "mc.h"
#include "mc_rng.h"
#include <string.h>
#include <math.h>

enum { SH_AIR=0, SH_STONE=1, SH_STONE_BRICK=45, SH_MOSSY_STONE_BRICK=46, SH_CRACKED_STONE_BRICK=46,
    SH_IRON_BARS=101, SH_LAVA=10, SH_END_PORTAL_FRAME=120, SH_PLANKS=5, SH_TORCH=50, SH_CHEST=54, SH_MOB_SPAWNER=52,
    SH_COBBLESTONE=4, SH_COBBLE_STAIRS=67, SH_WATER=9, SH_FLOWING_WATER=8 };
enum { SH_P_STAIRS2=100, SH_P_STRAIGHT, SH_P_PRISON, SH_P_LEFT_TURN, SH_P_RIGHT_TURN,
    SH_P_ROOM_CROSSING, SH_P_STAIRS_STRAIGHT, SH_P_STAIRS, SH_P_CROSSING, SH_CHEST_CORRIDOR, SH_P_LIBRARY, SH_P_PORTAL_ROOM };
#define SH_RANGE 8
#define SH_MAX_PIECES 64
#define SH_MAX_CHEST_SITES 32
/* Loot table ids for real placed stronghold chests (stronghold_loot SHL_*). */
enum { SH_LOOT_CORRIDOR = 0, SH_LOOT_LIBRARY = 1, SH_LOOT_CROSSING = 2 };
typedef struct { int minX,minY,minZ,maxX,maxY,maxZ; } SHBB;
typedef struct { int type,coord_base,component_type,has_spawner,has_rails,has_spiders,spawner_placed,section_count,is_multiple_floors,corridor_direction,is_large_room,portal_room,chest_placed; SHBB bb; } SHPiece;
/* One real chest placed by sh_place_* after that piece's prior RNG draws. */
typedef struct {
    int x, y, z;
    int table_id;
    i64 loot_seed; /* JavaRandom.nextLong() at generateChest site */
} SHChestSite;
#define SH_P_WEIGHT_COUNT 11

typedef struct {
    int type;
    int weight;
    int max_count;
    int cur_count;
    int min_depth;   /* minimum component depth to be eligible */
} SHPieceWeight;

typedef struct {
    int cx, cz, piece_count, valid;
    SHBB total_bb;
    SHPiece pieces[SH_MAX_PIECES];
    SHPieceWeight weights[SH_P_WEIGHT_COUNT];
    int total_weight;
    int portal_room_idx;
    /* Filled only during sh_place_blocks: real chest sites + placement-stream seeds. */
    SHChestSite chest_sites[SH_MAX_CHEST_SITES];
    int n_chest_sites;
} SHStart;
#ifndef MC_CHUNK_PROVIDER_H
typedef struct { u16 data[65536]; } ChunkPrimer;
#endif
typedef struct { ChunkPrimer *primer; int chunkX,chunkZ; i64 worldSeed; int seaLevel; } SHWorld;
MC_HD static inline int sh_idx(int x,int y,int z){ return x<<12|z<<8|y; }
MC_HD static inline int sh_in_chunk(int x,int y,int z,int cx,int cz){ return x>=cx*16&&x<cx*16+16&&z>=cz*16&&z<cz*16+16&&y>=0&&y<256; }
MC_HD static inline void sh_set(SHWorld *w,int x,int y,int z,int v){ if(sh_in_chunk(x,y,z,w->chunkX,w->chunkZ)) w->primer->data[sh_idx(x&15,y,z&15)]=(u16)v; }
MC_HD static inline int sh_get(SHWorld *w,int x,int y,int z){ if(!sh_in_chunk(x,y,z,w->chunkX,w->chunkZ)) return SH_AIR; return (int)w->primer->data[sh_idx(x&15,y,z&15)]; }
MC_HD static inline int sh_is_solid(int b){ return b!=SH_AIR && b!=SH_WATER && b!=SH_FLOWING_WATER; }

MC_HD static inline SHBB shbb_create(int x0,int y0,int z0,int x1,int y1,int z1){ SHBB b; b.minX=x0;b.minY=y0;b.minZ=z0;b.maxX=x1;b.maxY=y1;b.maxZ=z1; return b; }
MC_HD static inline int shbb_intersects(const SHBB *a,const SHBB *b){ return a->maxX>=b->minX&&a->minX<=b->maxX&&a->maxZ>=b->minZ&&a->minZ<=b->maxZ&&a->maxY>=b->minY&&a->minY<=b->maxY; }
MC_HD static inline int shbb_contains(const SHBB *bb,int x,int y,int z){ return x>=bb->minX&&x<=bb->maxX&&z>=bb->minZ&&z<=bb->maxZ&&y>=bb->minY&&y<=bb->maxY; }
MC_HD static inline void shbb_expand(SHBB *a,const SHBB *b){ if(b->minX<a->minX)a->minX=b->minX; if(b->minY<a->minY)a->minY=b->minY; if(b->minZ<a->minZ)a->minZ=b->minZ; if(b->maxX>a->maxX)a->maxX=b->maxX; if(b->maxY>a->maxY)a->maxY=b->maxY; if(b->maxZ>a->maxZ)a->maxZ=b->maxZ; }
MC_HD static inline void shbb_offset(SHBB *bb,int dx,int dy,int dz){ bb->minX+=dx; bb->minY+=dy; bb->minZ+=dz; bb->maxX+=dx; bb->maxY+=dy; bb->maxZ+=dz; }
MC_HD static inline int shbb_x_size(const SHBB *bb){ return bb->maxX-bb->minX+1; }
MC_HD static inline int shbb_z_size(const SHBB *bb){ return bb->maxZ-bb->minZ+1; }
MC_HD static inline int sh_get_x(const SHPiece *p,int x,int z){ switch(p->coord_base){case 0:case 2:return p->bb.minX+x;case 1:return p->bb.maxX-z;case 3:return p->bb.minX+z;default:return x;} }
MC_HD static inline int sh_get_y(const SHPiece *p,int y){ return p->coord_base==-1?y:y+p->bb.minY; }
MC_HD static inline int sh_get_z(const SHPiece *p,int x,int z){ switch(p->coord_base){case 0:return p->bb.minZ+z;case 1:case 3:return p->bb.minZ+x;case 2:return p->bb.maxZ-z;default:return z;} }
MC_HD MC_NOINLINE static SHBB shbb_component_bb(int x,int y,int z,int ox,int oy,int oz,int sx,int sy,int sz,int cb){
    SHBB bb; switch(cb){ case 0: bb.minX=x+ox;bb.minY=y+oy;bb.minZ=z+oz;bb.maxX=x+sx-1+ox;bb.maxY=y+sy-1+oy;bb.maxZ=z+sz-1+oz;break;
    case 1: bb.minX=x-sz+1+oz;bb.minY=y+oy;bb.minZ=z+ox;bb.maxX=x+oz;bb.maxY=y+sy-1+oy;bb.maxZ=z+sx-1+ox;break;
    case 2: bb.minX=x+ox;bb.minY=y+oy;bb.minZ=z-sz+1+oz;bb.maxX=x+sx-1+ox;bb.maxY=y+sy-1+oy;bb.maxZ=z+oz;break;
    case 3: bb.minX=x+oz;bb.minY=y+oy;bb.minZ=z+ox;bb.maxX=x+sz-1+oz;bb.maxY=y+sy-1+oy;bb.maxZ=z+sx-1+ox;break;
    default: bb.minX=x+ox;bb.minY=y+oy;bb.minZ=z+oz;bb.maxX=x+sx-1+ox;bb.maxY=y+sy-1+oy;bb.maxZ=z+sz-1+oz; } return bb; }
MC_HD MC_NOINLINE static void sh_place(SHWorld *w,const SHPiece *p,const SHBB *clip,int id,int meta,int lx,int ly,int lz){
    int wx=sh_get_x(p,lx,lz),wy=sh_get_y(p,ly),wz=sh_get_z(p,lx,lz); (void)meta;
    if(shbb_contains(clip,wx,wy,wz)) sh_set(w,wx,wy,wz,id); }
MC_HD MC_NOINLINE static int sh_get_local(SHWorld *w,const SHPiece *p,const SHBB *clip,int lx,int ly,int lz){
    int wx=sh_get_x(p,lx,lz),wy=sh_get_y(p,ly),wz=sh_get_z(p,lx,lz);
    if(!shbb_contains(clip,wx,wy,wz)) return SH_AIR;
    return sh_get(w,wx,wy,wz); }
MC_HD MC_NOINLINE static void sh_fill(SHWorld *w,const SHPiece *p,const SHBB *clip,int x0,int y0,int z0,int x1,int y1,int z1,int outer,int inner,int air_only){
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) for(int z=z0;z<=z1;z++){
        if(air_only && sh_get_local(w,p,clip,x,y,z)==SH_AIR) continue;
        int edge=(y==y0||y==y1||x==x0||x==x1||z==z0||z==z1);
        sh_place(w,p,clip,edge?outer:inner,0,x,y,z); }}
MC_HD MC_NOINLINE static void shrandom_fill(SHWorld *w,const SHPiece *p,const SHBB *clip,JavaRandom *r,float prob,int x0,int y0,int z0,int x1,int y1,int z1,int outer,int inner,int air_only){
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) for(int z=z0;z<=z1;z++) if(jrand_float(r)<=prob){
        if(air_only && sh_get_local(w,p,clip,x,y,z)==SH_AIR) continue;
        int edge=(y==y0||y==y1||x==x0||x==x1||z==z0||z==z1);
        sh_place(w,p,clip,edge?outer:inner,0,x,y,z); }}
MC_HD MC_NOINLINE static void shrandom_block(SHWorld *w,const SHPiece *p,const SHBB *clip,JavaRandom *r,float prob,int lx,int ly,int lz,int id,int meta){
    if(jrand_float(r)<=prob) sh_place(w,p,clip,id,meta,lx,ly,lz); }
MC_HD MC_NOINLINE static void shreplace_down(SHWorld *w,const SHPiece *p,const SHBB *clip,int id,int meta,int lx,int ly,int lz){
    int wx=sh_get_x(p,lx,lz),wz=sh_get_z(p,lx,lz),wy=sh_get_y(p,ly); (void)meta;
    while(wy>0 && shbb_contains(clip,wx,wy,wz)){ int cur=sh_get(w,wx,wy,wz); if(cur!=SH_AIR && sh_is_solid(cur)) break; sh_set(w,wx,wy,wz,id); wy--; } }
MC_HD MC_NOINLINE static void sh_fill_sh_stones(SHWorld *w,const SHPiece *p,const SHBB *clip,JavaRandom *r,int x0,int y0,int z0,int x1,int y1,int z1,int air_only){
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) for(int z=z0;z<=z1;z++){
        if(air_only && sh_get_local(w,p,clip,x,y,z)==SH_AIR) continue;
        int edge=(y==y0||y==y1||x==x0||x==x1||z==z0||z==z1);
        if(edge){ float f=jrand_float(r); int meta=f<0.2f?1:(f<0.3f?2:0); (void)meta; sh_place(w,p,clip,SH_STONE_BRICK,meta,x,y,z); }
        else sh_place(w,p,clip,SH_AIR,0,x,y,z); }}
MC_HD MC_NOINLINE static int sh_find_intersect(const SHPiece *ps,int n,const SHBB *bb){
    for(int i=0;i<n;i++) if(shbb_intersects(&ps[i].bb,bb)) return i; return -1; }


/* ============================================================
 * Stronghold -- port of MC 1.7.10 StructureStrongholdPieces
 * ============================================================ */

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Piece Weight System ---- */

MC_HD MC_NOINLINE static void sh_init_weights(SHStart *start) {
    start->total_weight = 0;
    start->portal_room_idx = -1;
    SHPieceWeight defs[SH_P_WEIGHT_COUNT] = {
        { SH_P_STRAIGHT,        40, 0, 0, 0 },
        { SH_P_PRISON,           5, 5, 0, 0 },
        { SH_P_LEFT_TURN,       20, 0, 0, 0 },
        { SH_P_RIGHT_TURN,      20, 0, 0, 0 },
        { SH_P_ROOM_CROSSING,   10, 6, 0, 0 },
        { SH_P_STAIRS_STRAIGHT,  5, 5, 0, 0 },
        { SH_P_STAIRS,           5, 5, 0, 0 },
        { SH_P_CROSSING,         5, 4, 0, 0 },
        { SH_CHEST_CORRIDOR,     5, 4, 0, 0 },
        { SH_P_LIBRARY,         10, 2, 0, 5 },
        { SH_P_PORTAL_ROOM,     20, 1, 0, 6 },
    };
    for (int i = 0; i < SH_P_WEIGHT_COUNT; i++) {
        start->weights[i] = defs[i];
        start->total_weight += defs[i].weight;
    }
}

MC_HD MC_NOINLINE static int sh_can_add_pieces(SHStart *start) {
    int any_limited = 0;
    start->total_weight = 0;
    for (int i = 0; i < SH_P_WEIGHT_COUNT; i++) {
        if (start->weights[i].max_count > 0 &&
            start->weights[i].cur_count < start->weights[i].max_count) {
            any_limited = 1;
        }
        start->total_weight += start->weights[i].weight;
    }
    return any_limited;
}

/* ---- Position Computation ---- */

MC_HD MC_NOINLINE static void sh_find_positions(i64 seed, int *out_cx, int *out_cz, int *count) {
    JavaRandom r;
    jrand_set(&r, seed);

    double angle = jrand_double(&r) * M_PI * 2.0;
    int ring = 1;
    int spread = 3;

    *count = 3;

    for (int i = 0; i < 3; i++) {
        double dist = (1.25 * (double)ring + jrand_double(&r)) * 32.0 * (double)ring;
        int cx = (int)round(cos(angle) * dist);
        int cz = (int)round(sin(angle) * dist);

        /* Skip biome search -- use raw coords (simplified) */
        out_cx[i] = cx;
        out_cz[i] = cz;

        angle += (M_PI * 2.0) * (double)ring / (double)spread;

        if (i == spread) {
            ring += 2 + jrand_int_bound(&r, 5);
            spread += 1 + jrand_int_bound(&r, 2);
        }
    }
}

/* ---- Piece Creation Functions ---- */

MC_HD MC_NOINLINE static SHPiece *sh_add_piece(SHStart *start) {
    if (start->piece_count >= SH_MAX_PIECES) return NULL;
    SHPiece *p = &start->pieces[start->piece_count++];
    memset(p, 0, sizeof(*p));
    return p;
}

/* Forward declarations */
MC_HD MC_NOINLINE static void sh_build_component(SHStart *start, SHPiece *piece,
                                JavaRandom *r, int depth);
MC_HD MC_NOINLINE static int sh_try_add_next(SHStart *start, JavaRandom *r,
                             int x, int y, int z, int dir, int depth);

/* Create individual piece types */
MC_HD MC_NOINLINE static int sh_create_straight(SHStart *start, JavaRandom *r,
                                int x, int y, int z, int dir, int depth)
{
    SHBB bb = shbb_component_bb(x, y, z, -1, -1, 0, 5, 5, 7, dir);
    if (sh_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    SHPiece *p = sh_add_piece(start);
    if (!p) return 0;
    p->type = SH_P_STRAIGHT;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int sh_create_prison(SHStart *start, JavaRandom *r,
                              int x, int y, int z, int dir, int depth)
{
    SHBB bb = shbb_component_bb(x, y, z, -1, -1, 0, 9, 5, 11, dir);
    if (sh_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    SHPiece *p = sh_add_piece(start);
    if (!p) return 0;
    p->type = SH_P_PRISON;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int sh_create_left_turn(SHStart *start, JavaRandom *r,
                                 int x, int y, int z, int dir, int depth)
{
    SHBB bb = shbb_component_bb(x, y, z, -1, -1, 0, 5, 5, 5, dir);
    if (sh_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    SHPiece *p = sh_add_piece(start);
    if (!p) return 0;
    p->type = SH_P_LEFT_TURN;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int sh_create_right_turn(SHStart *start, JavaRandom *r,
                                  int x, int y, int z, int dir, int depth)
{
    SHBB bb = shbb_component_bb(x, y, z, -1, -1, 0, 5, 5, 5, dir);
    if (sh_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    SHPiece *p = sh_add_piece(start);
    if (!p) return 0;
    p->type = SH_P_RIGHT_TURN;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int sh_create_room_crossing(SHStart *start, JavaRandom *r,
                                     int x, int y, int z, int dir, int depth)
{
    SHBB bb = shbb_component_bb(x, y, z, -4, -1, 0, 11, 7, 11, dir);
    if (sh_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    SHPiece *p = sh_add_piece(start);
    if (!p) return 0;
    p->type = SH_P_ROOM_CROSSING;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int sh_create_stairs_straight(SHStart *start, JavaRandom *r,
                                       int x, int y, int z, int dir, int depth)
{
    SHBB bb = shbb_component_bb(x, y, z, -1, -7, 0, 5, 11, 8, dir);
    if (sh_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    SHPiece *p = sh_add_piece(start);
    if (!p) return 0;
    p->type = SH_P_STAIRS_STRAIGHT;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int sh_create_stairs(SHStart *start, JavaRandom *r,
                              int x, int y, int z, int dir, int depth)
{
    SHBB bb = shbb_component_bb(x, y, z, -1, -7, 0, 5, 11, 5, dir);
    if (sh_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    SHPiece *p = sh_add_piece(start);
    if (!p) return 0;
    p->type = SH_P_STAIRS;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int sh_create_crossing(SHStart *start, JavaRandom *r,
                                int x, int y, int z, int dir, int depth)
{
    SHBB bb = shbb_component_bb(x, y, z, -4, -3, 0, 10, 9, 11, dir);
    if (sh_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    SHPiece *p = sh_add_piece(start);
    if (!p) return 0;
    p->type = SH_P_CROSSING;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int sh_create_chest_corridor(SHStart *start, JavaRandom *r,
                                      int x, int y, int z, int dir, int depth)
{
    SHBB bb = shbb_component_bb(x, y, z, -1, -1, 0, 5, 5, 7, dir);
    if (sh_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    SHPiece *p = sh_add_piece(start);
    if (!p) return 0;
    p->type = SH_CHEST_CORRIDOR;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

MC_HD MC_NOINLINE static int sh_create_library(SHStart *start, JavaRandom *r,
                               int x, int y, int z, int dir, int depth)
{
    int is_large = (jrand_int_bound(r, 4) == 0);
    int height = is_large ? 11 : 6;
    SHBB bb = shbb_component_bb(x, y, z, -4, -1, 0, 14, height, 15, dir);
    if (sh_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    SHPiece *p = sh_add_piece(start);
    if (!p) return 0;
    p->type = SH_P_LIBRARY;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    p->is_large_room = is_large;
    return 1;
}

MC_HD MC_NOINLINE static int sh_create_portal_room(SHStart *start, JavaRandom *r,
                                   int x, int y, int z, int dir, int depth)
{
    SHBB bb = shbb_component_bb(x, y, z, -4, -1, 0, 11, 8, 16, dir);
    if (sh_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    SHPiece *p = sh_add_piece(start);
    if (!p) return 0;
    p->type = SH_P_PORTAL_ROOM;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    p->portal_room = 1;
    p->has_spawner = 0;
    start->portal_room_idx = start->piece_count - 1;
    return 1;
}

/* Create piece by type */
MC_HD MC_NOINLINE static int sh_create_by_type(SHStart *start, JavaRandom *r,
                               int type, int x, int y, int z, int dir, int depth)
{
    switch (type) {
        case SH_P_STRAIGHT:        return sh_create_straight(start, r, x, y, z, dir, depth);
        case SH_P_PRISON:          return sh_create_prison(start, r, x, y, z, dir, depth);
        case SH_P_LEFT_TURN:       return sh_create_left_turn(start, r, x, y, z, dir, depth);
        case SH_P_RIGHT_TURN:      return sh_create_right_turn(start, r, x, y, z, dir, depth);
        case SH_P_ROOM_CROSSING:   return sh_create_room_crossing(start, r, x, y, z, dir, depth);
        case SH_P_STAIRS_STRAIGHT: return sh_create_stairs_straight(start, r, x, y, z, dir, depth);
        case SH_P_STAIRS:          return sh_create_stairs(start, r, x, y, z, dir, depth);
        case SH_P_CROSSING:        return sh_create_crossing(start, r, x, y, z, dir, depth);
        case SH_CHEST_CORRIDOR:  return sh_create_chest_corridor(start, r, x, y, z, dir, depth);
        case SH_P_LIBRARY:         return sh_create_library(start, r, x, y, z, dir, depth);
        case SH_P_PORTAL_ROOM:     return sh_create_portal_room(start, r, x, y, z, dir, depth);
        default: return 0;
    }
}

/* ---- Piece Tree Building ---- */

/* Get exit position going forward */
MC_HD MC_NOINLINE static void sh_get_normal_exit(const SHPiece *p, int offX, int offY,
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

/* Get exit going left */
MC_HD MC_NOINLINE static void sh_get_left_exit(const SHPiece *p, int offY, int offZ,
                              int *ox, int *oy, int *oz, int *odir)
{
    *oy = p->bb.minY + offY;
    switch (p->coord_base) {
        case 0: *ox = p->bb.minX - 1;    *oz = p->bb.minZ + offZ; *odir = 1; break;
        case 1: *ox = p->bb.minX + offZ; *oz = p->bb.minZ - 1;    *odir = 2; break;
        case 2: *ox = p->bb.maxX + 1;    *oz = p->bb.minZ + offZ; *odir = 3; break;
        case 3: *ox = p->bb.minX + offZ; *oz = p->bb.maxZ + 1;    *odir = 0; break;
        default: *ox = p->bb.minX - 1;   *oz = p->bb.minZ + offZ; *odir = 1; break;
    }
}

/* Get exit going right */
MC_HD MC_NOINLINE static void sh_get_right_exit(const SHPiece *p, int offY, int offZ,
                               int *ox, int *oy, int *oz, int *odir)
{
    *oy = p->bb.minY + offY;
    switch (p->coord_base) {
        case 0: *ox = p->bb.maxX + 1;    *oz = p->bb.minZ + offZ; *odir = 3; break;
        case 1: *ox = p->bb.minX + offZ; *oz = p->bb.maxZ + 1;    *odir = 0; break;
        case 2: *ox = p->bb.minX - 1;    *oz = p->bb.minZ + offZ; *odir = 1; break;
        case 3: *ox = p->bb.minX + offZ; *oz = p->bb.minZ - 1;    *odir = 2; break;
        default: *ox = p->bb.maxX + 1;   *oz = p->bb.minZ + offZ; *odir = 3; break;
    }
}

/* Try to add next piece from weight list */
MC_HD MC_NOINLINE static int sh_try_add_next(SHStart *start, JavaRandom *r,
                             int x, int y, int z, int dir, int depth)
{
    if (!sh_can_add_pieces(start)) return 0;
    if (depth > 50) return 0;

    /* Distance limit from first piece */
    if (abs(x - start->pieces[0].bb.minX) > 112 ||
        abs(z - start->pieces[0].bb.minZ) > 112) return 0;

    for (int attempt = 0; attempt < 5; attempt++) {
        int roll = jrand_int_bound(r, start->total_weight);
        for (int i = 0; i < SH_P_WEIGHT_COUNT; i++) {
            roll -= start->weights[i].weight;
            if (roll < 0) {
                /* Check depth constraint */
                if (start->weights[i].min_depth > 0 && depth <= start->weights[i].min_depth) {
                    break;
                }
                /* Check max count */
                if (start->weights[i].max_count > 0 &&
                    start->weights[i].cur_count >= start->weights[i].max_count) {
                    break;
                }

                int old_count = start->piece_count;
                if (sh_create_by_type(start, r, start->weights[i].type,
                                       x, y, z, dir, depth))
                {
                    start->weights[i].cur_count++;

                    /* Remove exhausted pieces from pool */
                    if (start->weights[i].max_count > 0 &&
                        start->weights[i].cur_count >= start->weights[i].max_count) {
                        /* Don't actually remove -- just skip via count check */
                    }

                    /* Recursively build from new pieces */
                    for (int j = old_count; j < start->piece_count; j++) {
                        sh_build_component(start, &start->pieces[j], r, depth + 1);
                    }
                    return 1;
                }
                start->piece_count = old_count;
                break;
            }
        }
    }
    return 0;
}

/* Build child components from a piece */
MC_HD MC_NOINLINE static void sh_build_component(SHStart *start, SHPiece *piece,
                                JavaRandom *r, int depth)
{
    if (depth > 50 || start->piece_count >= SH_MAX_PIECES - 10) return;

    int ox, oy, oz, odir;

    switch (piece->type) {
        case SH_P_STAIRS2:
        case SH_P_STAIRS_STRAIGHT:
        case SH_P_STAIRS:
            /* Forward exit */
            sh_get_normal_exit(piece, 1, 1, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            break;

        case SH_P_STRAIGHT:
            /* Forward exit */
            sh_get_normal_exit(piece, 1, 1, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            /* Random side corridors */
            if (jrand_int_bound(r, 2) == 0) {
                sh_get_left_exit(piece, 1, 1, &ox, &oy, &oz, &odir);
                sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            }
            if (jrand_int_bound(r, 2) == 0) {
                sh_get_right_exit(piece, 1, 1, &ox, &oy, &oz, &odir);
                sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            }
            break;

        case SH_P_LEFT_TURN:
            sh_get_left_exit(piece, 1, 1, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            break;

        case SH_P_RIGHT_TURN:
            sh_get_right_exit(piece, 1, 1, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            break;

        case SH_P_ROOM_CROSSING:
            sh_get_normal_exit(piece, 4, 1, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            sh_get_left_exit(piece, 1, 4, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            sh_get_right_exit(piece, 1, 4, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            break;

        case SH_P_CROSSING:
            sh_get_normal_exit(piece, 4, 1, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            sh_get_left_exit(piece, 1, 4, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            sh_get_right_exit(piece, 1, 4, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            break;

        case SH_CHEST_CORRIDOR:
            sh_get_normal_exit(piece, 1, 1, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            break;

        case SH_P_PRISON:
            sh_get_normal_exit(piece, 1, 1, &ox, &oy, &oz, &odir);
            sh_try_add_next(start, r, ox, oy, oz, odir, depth);
            break;

        /* Portal room and library are leaf nodes -- no exits */
        default:
            break;
    }
}

/* ---- Structure Generation ---- */

MC_HD MC_NOINLINE static void sh_generate(SHStart *start, i64 seed, int cx, int cz) {
    /* Retry until we get a portal room (up to 10 attempts) */
    for (int attempt = 0; attempt < 10; attempt++) {
        memset(start, 0, sizeof(*start));
        start->cx = cx;
        start->cz = cz;

        sh_init_weights(start);

        JavaRandom r;
        /* Use different seed per attempt so we don't repeat the same layout */
        jrand_set(&r, seed ^ ((i64)cx * 341873128712LL + (i64)cz * 132897987541LL + attempt * 7));

        int dir = jrand_int_bound(&r, 4);
        int wx = cx * 16 + 2;
        int wz = cz * 16 + 2;

        /* Starting piece: Stairs2 (entry staircase) */
        SHPiece *sp = sh_add_piece(start);
        if (!sp) return;
        sp->type = SH_P_STAIRS2;
        sp->coord_base = dir;
        sp->component_type = 0;
        sp->bb = shbb_component_bb(wx, 64, wz, -1, -7, 0, 5, 11, 5, dir);

        /* Build piece tree */
        sh_build_component(start, sp, &r, 0);

        /* Compute total bounding box */
        start->total_bb = start->pieces[0].bb;
        for (int i = 1; i < start->piece_count; i++) {
            shbb_expand(&start->total_bb, &start->pieces[i].bb);
        }

        /* Shift underground: target Y around 10-30 */
        int shift_y = 10 - start->total_bb.minY;
        if (shift_y > 0) {
            /* Already above y=10, try to bring to a reasonable depth */
        } else {
            shbb_offset(&start->total_bb, 0, shift_y, 0);
            for (int i = 0; i < start->piece_count; i++) {
                shbb_offset(&start->pieces[i].bb, 0, shift_y, 0);
            }
        }

        /* Check if we got a portal room */
        if (start->portal_room_idx >= 0) {
            start->valid = 1;
            return;
        }
    }

    /* If we still don't have a portal room after 10 attempts,
     * mark as valid anyway -- the structure is still useful */
    start->valid = (start->piece_count > 0);
}

/* ---- Block Placement ---- */

MC_HD MC_NOINLINE static void sh_place_straight(SHWorld *w, SHPiece *p, JavaRandom *r, const SHBB *clip) {
    sh_fill_sh_stones(w, p, clip, r, 0, 0, 0, 4, 4, 6, 0);
    /* Clear interior */
    sh_fill(w, p, clip, 1, 1, 0, 3, 3, 6, SH_AIR, SH_AIR, 0);
    /* Torches */
    sh_place(w, p, clip, SH_TORCH, 0, 1, 2, 1);
    sh_place(w, p, clip, SH_TORCH, 0, 3, 2, 1);
    sh_place(w, p, clip, SH_TORCH, 0, 1, 2, 5);
    sh_place(w, p, clip, SH_TORCH, 0, 3, 2, 5);
}

MC_HD MC_NOINLINE static void sh_place_prison(SHWorld *w, SHPiece *p, JavaRandom *r, const SHBB *clip) {
    sh_fill_sh_stones(w, p, clip, r, 0, 0, 0, 8, 4, 10, 0);
    /* Clear interior */
    sh_fill(w, p, clip, 1, 1, 0, 7, 3, 10, SH_AIR, SH_AIR, 0);
    /* Iron bars for cells */
    sh_fill(w, p, clip, 4, 1, 1, 4, 3, 1, SH_IRON_BARS, SH_IRON_BARS, 0);
    sh_fill(w, p, clip, 4, 1, 3, 4, 3, 3, SH_IRON_BARS, SH_IRON_BARS, 0);
    sh_fill(w, p, clip, 4, 1, 7, 4, 3, 7, SH_IRON_BARS, SH_IRON_BARS, 0);
    sh_fill(w, p, clip, 4, 1, 9, 4, 3, 9, SH_IRON_BARS, SH_IRON_BARS, 0);
    /* Iron door (simplified -- just iron bars) */
    sh_fill(w, p, clip, 4, 1, 4, 4, 3, 6, SH_IRON_BARS, SH_IRON_BARS, 0);
    sh_fill(w, p, clip, 1, 1, 4, 1, 3, 4, SH_IRON_BARS, SH_IRON_BARS, 0);
    sh_fill(w, p, clip, 7, 1, 4, 7, 3, 4, SH_IRON_BARS, SH_IRON_BARS, 0);
}

MC_HD MC_NOINLINE static void sh_place_left_turn(SHWorld *w, SHPiece *p, JavaRandom *r, const SHBB *clip) {
    sh_fill_sh_stones(w, p, clip, r, 0, 0, 0, 4, 4, 4, 0);
    sh_fill(w, p, clip, 1, 1, 0, 3, 3, 4, SH_AIR, SH_AIR, 0);
}

MC_HD MC_NOINLINE static void sh_place_right_turn(SHWorld *w, SHPiece *p, JavaRandom *r, const SHBB *clip) {
    sh_fill_sh_stones(w, p, clip, r, 0, 0, 0, 4, 4, 4, 0);
    sh_fill(w, p, clip, 1, 1, 0, 3, 3, 4, SH_AIR, SH_AIR, 0);
}

MC_HD MC_NOINLINE static void sh_place_room_crossing(SHWorld *w, SHPiece *p, JavaRandom *r, const SHBB *clip) {
    sh_fill_sh_stones(w, p, clip, r, 0, 0, 0, 10, 6, 10, 0);
    /* Clear interior */
    sh_fill(w, p, clip, 1, 1, 1, 9, 5, 9, SH_AIR, SH_AIR, 0);
    /* Central pillar */
    sh_fill(w, p, clip, 4, 1, 4, 6, 1, 6, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    sh_fill(w, p, clip, 4, 2, 4, 6, 5, 4, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    sh_fill(w, p, clip, 4, 2, 6, 6, 5, 6, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    sh_fill(w, p, clip, 4, 2, 5, 4, 5, 5, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    sh_fill(w, p, clip, 6, 2, 5, 6, 5, 5, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    /* Torches */
    sh_place(w, p, clip, SH_TORCH, 0, 5, 3, 5);
}

MC_HD MC_NOINLINE static void sh_place_stairs_straight(SHWorld *w, SHPiece *p, JavaRandom *r, const SHBB *clip) {
    sh_fill_sh_stones(w, p, clip, r, 0, 0, 0, 4, 10, 7, 0);
    sh_fill(w, p, clip, 1, 1, 0, 3, 4, 7, SH_AIR, SH_AIR, 0);
    /* Stairs (cobblestone stairs) */
    for (int i = 0; i <= 6; i++) {
        sh_place(w, p, clip, SH_COBBLE_STAIRS, 3, 1, 6 - i, i);
        sh_place(w, p, clip, SH_COBBLE_STAIRS, 3, 2, 6 - i, i);
        sh_place(w, p, clip, SH_COBBLE_STAIRS, 3, 3, 6 - i, i);
        if (i < 6) {
            sh_fill(w, p, clip, 1, 7 - i, i + 1, 3, 9 - i, 7, SH_STONE_BRICK, SH_STONE_BRICK, 0);
        }
    }
}

MC_HD MC_NOINLINE static void sh_place_stairs(SHWorld *w, SHPiece *p, JavaRandom *r, const SHBB *clip) {
    sh_fill_sh_stones(w, p, clip, r, 0, 0, 0, 4, 10, 4, 0);
    sh_fill(w, p, clip, 1, 1, 0, 3, 9, 4, SH_AIR, SH_AIR, 0);
    /* Spiral stairs */
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 2, 1, 0);
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 1, 1, 0);
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 1, 2, 0);
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 1, 3, 1);
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 1, 4, 2);
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 1, 5, 3);
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 1, 6, 4);
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 2, 6, 4);
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 3, 6, 4);
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 3, 7, 3);
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 3, 8, 2);
    sh_place(w, p, clip, SH_STONE_BRICK, 0, 3, 9, 1);
}

MC_HD MC_NOINLINE static void sh_place_crossing(SHWorld *w, SHPiece *p, JavaRandom *r, const SHBB *clip) {
    sh_fill_sh_stones(w, p, clip, r, 0, 0, 0, 9, 8, 10, 0);
    /* Clear interior */
    sh_fill(w, p, clip, 1, 1, 1, 8, 7, 9, SH_AIR, SH_AIR, 0);
    /* Central arch */
    sh_fill(w, p, clip, 4, 1, 0, 4, 3, 0, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    sh_fill(w, p, clip, 4, 5, 0, 4, 7, 0, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    sh_fill(w, p, clip, 5, 1, 0, 5, 3, 0, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    sh_fill(w, p, clip, 5, 5, 0, 5, 7, 0, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    /* Pillar */
    sh_fill(w, p, clip, 4, 1, 10, 5, 7, 10, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    sh_fill(w, p, clip, 0, 1, 4, 0, 7, 5, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    sh_fill(w, p, clip, 9, 1, 4, 9, 7, 5, SH_STONE_BRICK, SH_STONE_BRICK, 0);
}

/* Record a real generateChest-equivalent: nextLong after prior piece RNG. */
MC_HD MC_NOINLINE static void sh_record_chest(SHStart *start, JavaRandom *r,
                                              int wx, int wy, int wz, int table_id) {
    i64 seed = jrand_long(r);
    if (!start || start->n_chest_sites >= SH_MAX_CHEST_SITES) return;
    {
        SHChestSite *cs = &start->chest_sites[start->n_chest_sites++];
        cs->x = wx; cs->y = wy; cs->z = wz;
        cs->table_id = table_id;
        cs->loot_seed = seed;
    }
}

MC_HD MC_NOINLINE static void sh_place_chest_corridor(SHWorld *w, SHStart *start, SHPiece *p,
                                                       JavaRandom *r, const SHBB *clip) {
    /* Stones consume nextFloat per edge cell before the chest (Java STRONGHOLD_STONES). */
    sh_fill_sh_stones(w, p, clip, r, 0, 0, 0, 4, 4, 6, 0);
    sh_fill(w, p, clip, 1, 1, 0, 3, 3, 6, SH_AIR, SH_AIR, 0);
    /* Real C chest site (local 3,1,3). No phantom ordinals for unplaced pieces. */
    if (!p->chest_placed) {
        int wy = sh_get_y(p, 1);
        int wx = sh_get_x(p, 3, 3);
        int wz = sh_get_z(p, 3, 3);
        if (shbb_contains(clip, wx, wy, wz)) {
            p->chest_placed = 1;
            sh_set(w, wx, wy, wz, SH_CHEST);
            sh_record_chest(start, r, wx, wy, wz, SH_LOOT_CORRIDOR);
        }
    }
}

MC_HD MC_NOINLINE static void sh_place_library(SHWorld *w, SHStart *start, SHPiece *p,
                                               JavaRandom *r, const SHBB *clip) {
    int height = p->is_large_room ? 11 : 6;
    (void)height;

    sh_fill_sh_stones(w, p, clip, r, 0, 0, 0, 13, p->is_large_room ? 10 : 5, 14, 0);
    /* Clear interior */
    sh_fill(w, p, clip, 1, 1, 1, 12, p->is_large_room ? 9 : 4, 13, SH_AIR, SH_AIR, 0);

    /* Bookshelves along walls */
    for (int z = 1; z <= 13; z++) {
        sh_place(w, p, clip, SH_PLANKS, 0, 1, 1, z);
        sh_place(w, p, clip, SH_PLANKS, 0, 12, 1, z);
        if (z % 2 == 1) {
            sh_fill(w, p, clip, 1, 2, z, 1, 4, z, SH_PLANKS, SH_PLANKS, 0);
            sh_fill(w, p, clip, 12, 2, z, 12, 4, z, SH_PLANKS, SH_PLANKS, 0);
        }
    }

    /* Torches */
    sh_place(w, p, clip, SH_TORCH, 0, 3, 3, 1);
    sh_place(w, p, clip, SH_TORCH, 0, 10, 3, 1);
    sh_place(w, p, clip, SH_TORCH, 0, 3, 3, 13);
    sh_place(w, p, clip, SH_TORCH, 0, 10, 3, 13);

    /* Real C library chest at local 7,1,7 only (no phantom second large-room chest). */
    {
        int wy = sh_get_y(p, 1);
        int wx = sh_get_x(p, 7, 7);
        int wz = sh_get_z(p, 7, 7);
        if (shbb_contains(clip, wx, wy, wz)) {
            sh_set(w, wx, wy, wz, SH_CHEST);
            sh_record_chest(start, r, wx, wy, wz, SH_LOOT_LIBRARY);
        }
    }
}

MC_HD MC_NOINLINE static void sh_place_portal_room(SHWorld *w, SHPiece *p, JavaRandom *r, const SHBB *clip) {
    /* CRITICAL: End portal room -- this is what makes the game beatable */
    sh_fill_sh_stones(w, p, clip, r, 0, 0, 0, 10, 7, 15, 0);

    /* Clear interior */
    sh_fill(w, p, clip, 1, 1, 1, 9, 6, 14, SH_AIR, SH_AIR, 0);

    /* Iron bars on sides */
    for (int i = 3; i < 14; i += 2) {
        sh_fill(w, p, clip, 0, 3, i, 0, 4, i, SH_IRON_BARS, SH_IRON_BARS, 0);
        sh_fill(w, p, clip, 10, 3, i, 10, 4, i, SH_IRON_BARS, SH_IRON_BARS, 0);
    }

    /* Lava pools */
    sh_fill(w, p, clip, 4, 1, 9, 6, 1, 11, SH_LAVA, SH_LAVA, 0);

    /* Platform for portal */
    sh_fill(w, p, clip, 3, 2, 8, 7, 2, 12, SH_STONE_BRICK, SH_STONE_BRICK, 0);
    sh_fill(w, p, clip, 3, 3, 8, 7, 3, 12, SH_AIR, SH_AIR, 0);

    /* Stairs up to portal */
    sh_fill(w, p, clip, 3, 1, 8, 7, 1, 8, SH_COBBLE_STAIRS, SH_COBBLE_STAIRS, 0);

    /* END PORTAL FRAMES -- 12 frames in 3x4 ring */
    /* Orientation metadata based on coord_base */
    int b4 = 2, b1 = 0, b2 = 3, b3 = 1;
    switch (p->coord_base) {
        case 0: b4 = 0; b1 = 2; break;
        case 1: b4 = 1; b1 = 3; b2 = 0; b3 = 2; break;
        case 2: /* default */ break;
        case 3: b4 = 3; b1 = 1; b2 = 0; b3 = 2; break;
    }

    /* Front row (z=8): 3 frames facing south */
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b4 + (jrand_float(r) > 0.9f ? 4 : 0), 4, 3, 8);
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b4 + (jrand_float(r) > 0.9f ? 4 : 0), 5, 3, 8);
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b4 + (jrand_float(r) > 0.9f ? 4 : 0), 6, 3, 8);

    /* Back row (z=12): 3 frames facing north */
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b1 + (jrand_float(r) > 0.9f ? 4 : 0), 4, 3, 12);
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b1 + (jrand_float(r) > 0.9f ? 4 : 0), 5, 3, 12);
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b1 + (jrand_float(r) > 0.9f ? 4 : 0), 6, 3, 12);

    /* Left column (x=3): 3 frames facing east */
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b2 + (jrand_float(r) > 0.9f ? 4 : 0), 3, 3, 9);
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b2 + (jrand_float(r) > 0.9f ? 4 : 0), 3, 3, 10);
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b2 + (jrand_float(r) > 0.9f ? 4 : 0), 3, 3, 11);

    /* Right column (x=7): 3 frames facing west */
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b3 + (jrand_float(r) > 0.9f ? 4 : 0), 7, 3, 9);
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b3 + (jrand_float(r) > 0.9f ? 4 : 0), 7, 3, 10);
    sh_place(w, p, clip, SH_END_PORTAL_FRAME,
        b3 + (jrand_float(r) > 0.9f ? 4 : 0), 7, 3, 11);

    /* Silverfish spawner at center */
    if (!p->has_spawner) {
        int wy = sh_get_y(p, 3);
        int wx = sh_get_x(p, 5, 6);
        int wz = sh_get_z(p, 5, 6);
        if (shbb_contains(clip, wx, wy, wz)) {
            p->has_spawner = 1;
            sh_set(w, wx, wy, wz, SH_MOB_SPAWNER);
        }
    }
}

MC_HD MC_NOINLINE static void sh_place_blocks(SHWorld *w, SHStart *start) {
    SHBB clip = start->total_bb;
    clip.minX -= 16; clip.minZ -= 16;
    clip.maxX += 16; clip.maxZ += 16;
    clip.minY = 0; clip.maxY = 255;

    JavaRandom r;
    /* Placement stream seed matches MapGenStructure chunk random mix used by C. */
    jrand_set(&r, (i64)start->cx * 341873128712LL + (i64)start->cz * 132897987541LL);
    start->n_chest_sites = 0;

    for (int i = 0; i < start->piece_count; i++) {
        SHPiece *p = &start->pieces[i];
        switch (p->type) {
            case SH_P_STAIRS2:
            case SH_P_STAIRS:          sh_place_stairs(w, p, &r, &clip); break;
            case SH_P_STRAIGHT:        sh_place_straight(w, p, &r, &clip); break;
            case SH_P_PRISON:          sh_place_prison(w, p, &r, &clip); break;
            case SH_P_LEFT_TURN:       sh_place_left_turn(w, p, &r, &clip); break;
            case SH_P_RIGHT_TURN:      sh_place_right_turn(w, p, &r, &clip); break;
            case SH_P_ROOM_CROSSING:   sh_place_room_crossing(w, p, &r, &clip); break;
            case SH_P_STAIRS_STRAIGHT: sh_place_stairs_straight(w, p, &r, &clip); break;
            case SH_P_CROSSING:        sh_place_crossing(w, p, &r, &clip); break;
            case SH_CHEST_CORRIDOR:  sh_place_chest_corridor(w, start, p, &r, &clip); break;
            case SH_P_LIBRARY:         sh_place_library(w, start, p, &r, &clip); break;
            case SH_P_PORTAL_ROOM:     sh_place_portal_room(w, p, &r, &clip); break;
        }
    }
}

/* Replay place_blocks RNG to fill start->chest_sites without needing world chunks. */
MC_HD MC_NOINLINE static void sh_capture_chest_sites(SHStart *start) {
    ChunkPrimer primer;
    SHWorld w;
    int i;
    if (!start) return;
    memset(&primer, 0, sizeof primer);
    w.primer = &primer;
    w.chunkX = start->cx;
    w.chunkZ = start->cz;
    w.worldSeed = 0;
    w.seaLevel = 63;
    for (i = 0; i < start->piece_count; ++i)
        start->pieces[i].chest_placed = 0;
    sh_place_blocks(&w, start);
}

#define SH_MAX_STARTS 8
typedef struct { SHStart starts[SH_MAX_STARTS]; int count; i64 worldSeed; int sh_cx[128], sh_cz[128], sh_count; } SHGen;

MC_HD MC_NOINLINE static void sh_init_positions(SHGen *g, i64 worldSeed) {
    g->sh_count = 3;
    sh_find_positions(worldSeed, g->sh_cx, g->sh_cz, &g->sh_count);
}

MC_HD MC_NOINLINE static int sh_try_spawn(SHGen *g, JavaRandom *r, int cx, int cz) {
    (void)r;
    for (int i = 0; i < g->sh_count; ++i)
        if (g->sh_cx[i] == cx && g->sh_cz[i] == cz) return 1;
    return 0;
}

MC_HD static inline void sh_place_all(SHWorld *w, SHStart *s) { sh_place_blocks(w, s); }

MC_HD MC_NOINLINE static void sh_generate_map(SHGen *g, i64 worldSeed, int x, int z) {
    g->count = 0; g->worldSeed = worldSeed;
    sh_init_positions(g, worldSeed);
    int range = SH_RANGE;
    JavaRandom rand; jrand_set(&rand, worldSeed);
    i64 j = jrand_long(&rand), k = jrand_long(&rand);
    for (int l = x - range; l <= x + range; ++l)
        for (int i1 = z - range; i1 <= z + range; ++i1) {
            jrand_set(&rand, (i64)l * j ^ (i64)i1 * k ^ worldSeed);
            jrand_int(&rand);
            if (sh_try_spawn(g, &rand, l, i1) && g->count < SH_MAX_STARTS) {
                SHStart *s = &g->starts[g->count++];
                sh_generate(s, worldSeed, l, i1);
            }
        }
}

MC_HD MC_NOINLINE static void sh_generate_structure(SHWorld *w,SHGen *g,int cx,int cz){
    SHBB clip={cx*16,0,cz*16,cx*16+15,255,cz*16+15};
    for(int i=0;i<g->count;++i) if(g->starts[i].valid && shbb_intersects(&g->starts[i].total_bb,&clip)) sh_place_all(w,&g->starts[i]);
}

#ifdef __CUDACC__
__device__ SHGen sh_cuda_gen;
#endif

MC_HD MC_NOINLINE static void sh_run(ChunkPrimer *primer,i64 seed,int cx,int cz){
    for(int i=0;i<65536;++i) primer->data[i]=(u16)SH_STONE;
    SHWorld w; w.primer=primer; w.chunkX=cx; w.chunkZ=cz; w.worldSeed=seed; w.seaLevel=63;
#ifdef __CUDA_ARCH__
    sh_generate_map(&sh_cuda_gen,seed,cx,cz);
    sh_generate_structure(&w,&sh_cuda_gen,cx,cz);
#else
    SHGen g; sh_generate_map(&g,seed,cx,cz);
    sh_generate_structure(&w,&g,cx,cz);
#endif
}
#endif
