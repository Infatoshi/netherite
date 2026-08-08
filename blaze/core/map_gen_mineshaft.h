/* map_gen_mineshaft: port of MC 1.11.2 structure generator (mineshaft). */
#ifndef MC_MAP_GEN_MINESHAFT_H
#define MC_MAP_GEN_MINESHAFT_H
#include "mc.h"
#include "mc_rng.h"
#include <string.h>

enum { MS_AIR=0, MS_STONE=1, MS_GRASS=2, MS_DIRT=3, MS_PLANKS=5, MS_WATER=9, MS_FLOWING_WATER=8,
    MS_TORCH=50, MS_WEB=30, MS_FENCE=85, MS_DARK_OAK_FENCE=191,
    MS_RAIL=66, MS_CHEST=54, MS_MOB_SPAWNER=52, MS_LAVA=11, MS_FLOWING_LAVA=10 };
enum { MS_P_ROOM=200, MS_P_CORRIDOR, MS_P_STAIRS, MS_P_CROSS };
enum { MS_DIR_SOUTH=0, MS_DIR_WEST=1, MS_DIR_NORTH=2, MS_DIR_EAST=3 };
enum { MS_TYPE_NORMAL=0, MS_TYPE_MESA=1 };
#define MS_RANGE 8
#define MS_MAX_PIECES 160
#define MS_MAX_ENTRANCES 32
#define MS_MAX_CARTS 32
#define MS_MAX_SPAWNERS 16
#ifndef __CUDA_ARCH__
enum { MS_EVENT_CART = 1, MS_EVENT_SPAWNER = 2 };
static void (*g_ms_event)(int baseCx, int baseCz, int kind,
                          int x, int y, int z, i64 value);
static int (*g_ms_type_at)(i64 seed, int x, int z);
#endif
typedef struct { int minX,minY,minZ,maxX,maxY,maxZ; } MSBB;
typedef struct { int type,coord_base,component_type,has_spawner,has_rails,has_spiders,spawner_placed,section_count,is_multiple_floors,corridor_direction,is_large_room,portal_room,chest_placed,mine_type; MSBB bb; } MSPiece;
typedef struct { int x,y,z; i64 loot_seed; } MSCartEvent;
typedef struct { int x,y,z; } MSSpawnerEvent;
typedef struct {
    int cx,cz,piece_count,valid,mine_type,entrance_count,cart_count,spawner_count;
    u64 rng_seed_before, rng_seed_after;
    MSBB total_bb;
    MSBB entrances[MS_MAX_ENTRANCES];
    MSCartEvent carts[MS_MAX_CARTS];
    MSSpawnerEvent spawners[MS_MAX_SPAWNERS];
    MSPiece pieces[MS_MAX_PIECES];
} MSStart;
#ifndef MC_CHUNK_PROVIDER_H
typedef struct { u16 data[65536]; } ChunkPrimer;
#endif
typedef struct {
    ChunkPrimer *primer;
    int chunkX,chunkZ;
    i64 worldSeed;
    int seaLevel,storeMeta;
    u16 *window;
    int windowBaseX,windowBaseZ,windowWidth;
} MSWorld;
MC_HD static inline int ms_idx(int x,int y,int z){ return x<<12|z<<8|y; }
MC_HD static inline int ms_in_chunk(int x,int y,int z,int cx,int cz){ return x>=cx*16&&x<cx*16+16&&z>=cz*16&&z<cz*16+16&&y>=0&&y<256; }
MC_HD static inline int ms_in_window(const MSWorld *w,int x,int y,int z){
    return w->window&&x>=w->windowBaseX&&x<w->windowBaseX+w->windowWidth&&
        z>=w->windowBaseZ&&z<w->windowBaseZ+w->windowWidth&&y>=0&&y<256;
}
MC_HD static inline void ms_set(MSWorld *w,int x,int y,int z,int v){
    if(ms_in_window(w,x,y,z)){
        int lx=x-w->windowBaseX,lz=z-w->windowBaseZ;
        w->window[((lx*w->windowWidth+lz)<<8)|y]=(u16)v;
    }else if(w->primer&&ms_in_chunk(x,y,z,w->chunkX,w->chunkZ))
        w->primer->data[ms_idx(x&15,y,z&15)]=(u16)v;
}
MC_HD static inline int ms_get(MSWorld *w,int x,int y,int z){
    int v;
    if(ms_in_window(w,x,y,z)){
        int lx=x-w->windowBaseX,lz=z-w->windowBaseZ;
        v=(int)w->window[((lx*w->windowWidth+lz)<<8)|y];
    }else{
        if(!w->primer||!ms_in_chunk(x,y,z,w->chunkX,w->chunkZ)) return MS_AIR;
        v=(int)w->primer->data[ms_idx(x&15,y,z&15)];
    }
    if(w->storeMeta==1) return v>>4;
    if(w->storeMeta==2 && (v&0x4000)) return (v>>4)&0x03ff;
    if(w->window){
        if(v==2) return MS_WATER;
        if(v==3) return MS_GRASS;
        if(v==4) return MS_DIRT;
        if(v==5) return 7;
        if(v==11) return MS_LAVA;
        if(v==12) return MS_FLOWING_LAVA;
        if(v==13) return MS_FLOWING_WATER;
    }
    return v;
}
MC_HD static inline void ms_set_id(MSWorld *w,int x,int y,int z,int id){
    ms_set(w,x,y,z,w->storeMeta==1?id<<4:w->storeMeta==2?(0x4000|(id<<4)):id);
}
MC_HD static inline int ms_is_solid(int b){ return b!=MS_AIR && b!=MS_WATER && b!=MS_FLOWING_WATER; }
MC_HD static inline int ms_is_full(int b){ return ms_is_solid(b)&&b!=MS_FENCE&&b!=MS_DARK_OAK_FENCE&&b!=MS_WEB&&b!=MS_RAIL&&b!=MS_TORCH; }
MC_HD static inline int ms_min(int a,int b){ return a<b?a:b; }
MC_HD static inline int ms_max(int a,int b){ return a>b?a:b; }
MC_HD static inline int ms_is_liquid(int b){ return b==MS_WATER||b==MS_FLOWING_WATER||b==MS_LAVA||b==MS_FLOWING_LAVA; }
MC_HD static inline int ms_rail_meta(const MSPiece *p,int meta){
    return (p->coord_base==MS_DIR_WEST||p->coord_base==MS_DIR_EAST)?(meta^1):meta;
}
MC_HD static inline int ms_torch_meta(const MSPiece *p,int meta){
    if(p->coord_base==MS_DIR_SOUTH) return meta==4?3:meta==3?4:meta;
    if(p->coord_base==MS_DIR_WEST) return meta==4?2:meta==3?1:meta;
    if(p->coord_base==MS_DIR_EAST) return meta==4?1:meta==3?2:meta;
    return meta;
}

MC_HD static inline MSBB msbb_create(int x0,int y0,int z0,int x1,int y1,int z1){ MSBB b; b.minX=x0;b.minY=y0;b.minZ=z0;b.maxX=x1;b.maxY=y1;b.maxZ=z1; return b; }
MC_HD static inline int msbb_intersects(const MSBB *a,const MSBB *b){ return a->maxX>=b->minX&&a->minX<=b->maxX&&a->maxZ>=b->minZ&&a->minZ<=b->maxZ&&a->maxY>=b->minY&&a->minY<=b->maxY; }
MC_HD static inline int msbb_contains(const MSBB *bb,int x,int y,int z){ return x>=bb->minX&&x<=bb->maxX&&z>=bb->minZ&&z<=bb->maxZ&&y>=bb->minY&&y<=bb->maxY; }
MC_HD static inline void msbb_expand(MSBB *a,const MSBB *b){ if(b->minX<a->minX)a->minX=b->minX; if(b->minY<a->minY)a->minY=b->minY; if(b->minZ<a->minZ)a->minZ=b->minZ; if(b->maxX>a->maxX)a->maxX=b->maxX; if(b->maxY>a->maxY)a->maxY=b->maxY; if(b->maxZ>a->maxZ)a->maxZ=b->maxZ; }
MC_HD static inline void msbb_offset(MSBB *bb,int dx,int dy,int dz){ bb->minX+=dx; bb->minY+=dy; bb->minZ+=dz; bb->maxX+=dx; bb->maxY+=dy; bb->maxZ+=dz; }
MC_HD static inline int msbb_x_size(const MSBB *bb){ return bb->maxX-bb->minX+1; }
MC_HD static inline int msbb_z_size(const MSBB *bb){ return bb->maxZ-bb->minZ+1; }
MC_HD static inline int ms_get_x(const MSPiece *p,int x,int z){ switch(p->coord_base){case 0:case 2:return p->bb.minX+x;case 1:return p->bb.maxX-z;case 3:return p->bb.minX+z;default:return x;} }
MC_HD static inline int ms_get_y(const MSPiece *p,int y){ return p->coord_base==-1?y:y+p->bb.minY; }
MC_HD static inline int ms_get_z(const MSPiece *p,int x,int z){ switch(p->coord_base){case 0:return p->bb.minZ+z;case 1:case 3:return p->bb.minZ+x;case 2:return p->bb.maxZ-z;default:return z;} }
MC_HD MC_NOINLINE static MSBB msbb_component_bb(int x,int y,int z,int ox,int oy,int oz,int sx,int sy,int sz,int cb){
    MSBB bb; switch(cb){ case 0: bb.minX=x+ox;bb.minY=y+oy;bb.minZ=z+oz;bb.maxX=x+sx-1+ox;bb.maxY=y+sy-1+oy;bb.maxZ=z+sz-1+oz;break;
    case 1: bb.minX=x-sz+1+oz;bb.minY=y+oy;bb.minZ=z+ox;bb.maxX=x+oz;bb.maxY=y+sy-1+oy;bb.maxZ=z+sx-1+ox;break;
    case 2: bb.minX=x+ox;bb.minY=y+oy;bb.minZ=z-sz+1+oz;bb.maxX=x+sx-1+ox;bb.maxY=y+sy-1+oy;bb.maxZ=z+oz;break;
    case 3: bb.minX=x+oz;bb.minY=y+oy;bb.minZ=z+ox;bb.maxX=x+sz-1+oz;bb.maxY=y+sy-1+oy;bb.maxZ=z+sx-1+ox;break;
    default: bb.minX=x+ox;bb.minY=y+oy;bb.minZ=z+oz;bb.maxX=x+sx-1+ox;bb.maxY=y+sy-1+oy;bb.maxZ=z+sz-1+oz; } return bb; }
MC_HD MC_NOINLINE static void ms_place(MSWorld *w,const MSPiece *p,const MSBB *clip,int id,int meta,int lx,int ly,int lz){
    int wx=ms_get_x(p,lx,lz),wy=ms_get_y(p,ly),wz=ms_get_z(p,lx,lz);
    if(p->mine_type==MS_TYPE_MESA){
        if(id==MS_PLANKS) meta=5;
        else if(id==MS_FENCE) id=MS_DARK_OAK_FENCE;
    }
    if(id==MS_RAIL) meta=ms_rail_meta(p,meta);
    else if(id==MS_TORCH){
        meta=ms_torch_meta(p,meta);
        int sx=wx,sy=wy,sz=wz;
        if(meta==1) sx--; else if(meta==2) sx++;
        else if(meta==3) sz--; else if(meta==4) sz++; else sy--;
        if(!ms_is_full(ms_get(w,sx,sy,sz))) return;
    }
    if(msbb_contains(clip,wx,wy,wz)){
        int value=w->storeMeta==1?(id<<4|meta):w->storeMeta==2?
            (0x4000|(id<<4)|meta):id;
        ms_set(w,wx,wy,wz,value);
    } }
MC_HD MC_NOINLINE static int ms_get_local(MSWorld *w,const MSPiece *p,const MSBB *clip,int lx,int ly,int lz){
    int wx=ms_get_x(p,lx,lz),wy=ms_get_y(p,ly),wz=ms_get_z(p,lx,lz);
    if(!msbb_contains(clip,wx,wy,wz)) return MS_AIR;
    return ms_get(w,wx,wy,wz); }
MC_HD MC_NOINLINE static void ms_fill(MSWorld *w,const MSPiece *p,const MSBB *clip,int x0,int y0,int z0,int x1,int y1,int z1,int outer,int inner,int air_only){
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) for(int z=z0;z<=z1;z++){
        if(air_only && ms_get_local(w,p,clip,x,y,z)==MS_AIR) continue;
        int edge=(y==y0||y==y1||x==x0||x==x1||z==z0||z==z1);
        ms_place(w,p,clip,edge?outer:inner,0,x,y,z); }}
MC_HD MC_NOINLINE static void ms_random_fill(MSWorld *w,const MSPiece *p,const MSBB *clip,JavaRandom *r,float prob,int x0,int y0,int z0,int x1,int y1,int z1,int outer,int inner,int air_only){
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) for(int z=z0;z<=z1;z++) if(jrand_float(r)<=prob){
        if(air_only && ms_get_local(w,p,clip,x,y,z)==MS_AIR) continue;
        int edge=(y==y0||y==y1||x==x0||x==x1||z==z0||z==z1);
        ms_place(w,p,clip,edge?outer:inner,0,x,y,z); }}
MC_HD MC_NOINLINE static void ms_random_block(MSWorld *w,const MSPiece *p,const MSBB *clip,JavaRandom *r,float prob,int lx,int ly,int lz,int id,int meta){
    if(jrand_float(r)<prob) ms_place(w,p,clip,id,meta,lx,ly,lz); }
MC_HD MC_NOINLINE static void ms_replace_down(MSWorld *w,const MSPiece *p,const MSBB *clip,int id,int meta,int lx,int ly,int lz){
    int wx=ms_get_x(p,lx,lz),wz=ms_get_z(p,lx,lz),wy=ms_get_y(p,ly); (void)meta;
    while(wy>0 && msbb_contains(clip,wx,wy,wz)){ int cur=ms_get(w,wx,wy,wz); if(cur!=MS_AIR && ms_is_solid(cur)) break; ms_set_id(w,wx,wy,wz,id); wy--; } }
MC_HD MC_NOINLINE static int ms_find_intersect(const MSPiece *ps,int n,const MSBB *bb){
    for(int i=0;i<n;i++) if(msbb_intersects(&ps[i].bb,bb)) return i; return -1; }

/* ---- Piece creation ---- */

MC_HD MC_NOINLINE static MSPiece *ms_add_piece(MSStart *start) {
    if (start->piece_count >= MS_MAX_PIECES) return NULL;
    MSPiece *p = &start->pieces[start->piece_count++];
    memset(p, 0, sizeof(*p));
    p->mine_type = start->mine_type;
    return p;
}

MC_HD MC_NOINLINE static void ms_build_component(MSStart *start, MSPiece *piece,
                                                  JavaRandom *r);

MC_HD MC_NOINLINE static MSPiece *ms_create_random_piece(
        MSStart *start, JavaRandom *r, int x, int y, int z, int dir,
        int component_type) {
    int choice = jrand_int_bound(r, 100);
    MSBB bb = msbb_create(x, y, z, x, y + 2, z);
    int type;

    if (choice >= 80) {
        if (jrand_int_bound(r, 4) == 0) bb.maxY += 4;
        switch (dir) {
            case MS_DIR_NORTH: bb.minX=x-1; bb.maxX=x+3; bb.minZ=z-4; break;
            case MS_DIR_SOUTH: bb.minX=x-1; bb.maxX=x+3; bb.maxZ=z+4; break;
            case MS_DIR_WEST:  bb.minX=x-4; bb.minZ=z-1; bb.maxZ=z+3; break;
            case MS_DIR_EAST:  bb.maxX=x+4; bb.minZ=z-1; bb.maxZ=z+3; break;
            default: return NULL;
        }
        if (ms_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return NULL;
        type = MS_P_CROSS;
    } else if (choice >= 70) {
        bb = msbb_create(x, y - 5, z, x, y + 2, z);
        switch (dir) {
            case MS_DIR_NORTH: bb.maxX=x+2; bb.minZ=z-8; break;
            case MS_DIR_SOUTH: bb.maxX=x+2; bb.maxZ=z+8; break;
            case MS_DIR_WEST:  bb.minX=x-8; bb.maxZ=z+2; break;
            case MS_DIR_EAST:  bb.maxX=x+8; bb.maxZ=z+2; break;
            default: return NULL;
        }
        if (ms_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return NULL;
        type = MS_P_STAIRS;
    } else {
        int sections;
        for (sections = jrand_int_bound(r, 3) + 2; sections > 0; --sections) {
            int length = sections * 5;
            bb = msbb_create(x, y, z, x, y + 2, z);
            switch (dir) {
                case MS_DIR_NORTH: bb.maxX=x+2; bb.minZ=z-(length-1); break;
                case MS_DIR_SOUTH: bb.maxX=x+2; bb.maxZ=z+(length-1); break;
                case MS_DIR_WEST:  bb.minX=x-(length-1); bb.maxZ=z+2; break;
                case MS_DIR_EAST:  bb.maxX=x+(length-1); bb.maxZ=z+2; break;
                default: return NULL;
            }
            if (ms_find_intersect(start->pieces, start->piece_count, &bb) < 0) break;
        }
        if (sections <= 0) return NULL;
        type = MS_P_CORRIDOR;
    }

    MSPiece *p = ms_add_piece(start);
    if (!p) return NULL;
    p->type = type;
    p->bb = bb;
    p->coord_base = type == MS_P_CROSS ? -1 : dir;
    p->component_type = component_type;
    if (type == MS_P_CORRIDOR) {
        p->has_rails = jrand_int_bound(r, 3) == 0;
        p->has_spiders = !p->has_rails && jrand_int_bound(r, 23) == 0;
        p->section_count = (dir == MS_DIR_NORTH || dir == MS_DIR_SOUTH)
            ? msbb_z_size(&bb) / 5 : msbb_x_size(&bb) / 5;
    } else if (type == MS_P_CROSS) {
        p->corridor_direction = dir;
        p->is_multiple_floors = bb.maxY - bb.minY + 1 > 3;
    }
    return p;
}

MC_HD MC_NOINLINE static MSPiece *ms_generate_and_add(
        MSStart *start, JavaRandom *r, int x, int y, int z, int dir,
        int parent_component_type) {
    MSPiece *root = &start->pieces[0];
    if (parent_component_type > 8) return NULL;
    int dx = x - root->bb.minX, dz = z - root->bb.minZ;
    if ((dx < 0 ? -dx : dx) > 80 || (dz < 0 ? -dz : dz) > 80) return NULL;
    MSPiece *p = ms_create_random_piece(start, r, x, y, z, dir,
                                         parent_component_type + 1);
    if (p) ms_build_component(start, p, r);
    return p;
}

MC_HD static inline void ms_add_entrance(MSStart *start, MSBB bb) {
    if (start->entrance_count < MS_MAX_ENTRANCES)
        start->entrances[start->entrance_count++] = bb;
}

MC_HD MC_NOINLINE static void ms_build_room(MSStart *start, MSPiece *room,
                                             JavaRandom *r) {
    int width = msbb_x_size(&room->bb), depth = msbb_z_size(&room->bb);
    int yspan = room->bb.maxY - room->bb.minY + 1 - 4;
    if (yspan <= 0) yspan = 1;
    int l = 0;

    for (int j = 0; j < width; j = l + 4) {
        l = j + jrand_int_bound(r, width);
        if (l + 3 > width) break;
        MSPiece *p = ms_generate_and_add(start, r, room->bb.minX + l,
            room->bb.minY + jrand_int_bound(r, yspan) + 1, room->bb.minZ - 1,
            MS_DIR_NORTH, room->component_type);
        if (p) ms_add_entrance(start, msbb_create(p->bb.minX, p->bb.minY,
            room->bb.minZ, p->bb.maxX, p->bb.maxY, room->bb.minZ + 1));
    }
    for (int j = 0; j < width; j = l + 4) {
        l = j + jrand_int_bound(r, width);
        if (l + 3 > width) break;
        MSPiece *p = ms_generate_and_add(start, r, room->bb.minX + l,
            room->bb.minY + jrand_int_bound(r, yspan) + 1, room->bb.maxZ + 1,
            MS_DIR_SOUTH, room->component_type);
        if (p) ms_add_entrance(start, msbb_create(p->bb.minX, p->bb.minY,
            room->bb.maxZ - 1, p->bb.maxX, p->bb.maxY, room->bb.maxZ));
    }
    for (int j = 0; j < depth; j = l + 4) {
        l = j + jrand_int_bound(r, depth);
        if (l + 3 > depth) break;
        MSPiece *p = ms_generate_and_add(start, r, room->bb.minX - 1,
            room->bb.minY + jrand_int_bound(r, yspan) + 1, room->bb.minZ + l,
            MS_DIR_WEST, room->component_type);
        if (p) ms_add_entrance(start, msbb_create(room->bb.minX, p->bb.minY,
            p->bb.minZ, room->bb.minX + 1, p->bb.maxY, p->bb.maxZ));
    }
    for (int j = 0; j < depth; j = l + 4) {
        l = j + jrand_int_bound(r, depth);
        if (l + 3 > depth) break;
        MSPiece *p = ms_generate_and_add(start, r, room->bb.maxX + 1,
            room->bb.minY + jrand_int_bound(r, yspan) + 1, room->bb.minZ + l,
            MS_DIR_EAST, room->component_type);
        if (p) ms_add_entrance(start, msbb_create(room->bb.maxX - 1, p->bb.minY,
            p->bb.minZ, room->bb.maxX, p->bb.maxY, p->bb.maxZ));
    }
}

MC_HD MC_NOINLINE static void ms_build_corridor(MSStart *start, MSPiece *p,
                                                 JavaRandom *r) {
    int type = p->component_type;
    int branch = jrand_int_bound(r, 4);
    switch (p->coord_base) {
        case MS_DIR_NORTH:
            if (branch <= 1) ms_generate_and_add(start,r,p->bb.minX,p->bb.minY-1+jrand_int_bound(r,3),p->bb.minZ-1,MS_DIR_NORTH,type);
            else if (branch == 2) ms_generate_and_add(start,r,p->bb.minX-1,p->bb.minY-1+jrand_int_bound(r,3),p->bb.minZ,MS_DIR_WEST,type);
            else ms_generate_and_add(start,r,p->bb.maxX+1,p->bb.minY-1+jrand_int_bound(r,3),p->bb.minZ,MS_DIR_EAST,type);
            break;
        case MS_DIR_SOUTH:
            if (branch <= 1) ms_generate_and_add(start,r,p->bb.minX,p->bb.minY-1+jrand_int_bound(r,3),p->bb.maxZ+1,MS_DIR_SOUTH,type);
            else if (branch == 2) ms_generate_and_add(start,r,p->bb.minX-1,p->bb.minY-1+jrand_int_bound(r,3),p->bb.maxZ-3,MS_DIR_WEST,type);
            else ms_generate_and_add(start,r,p->bb.maxX+1,p->bb.minY-1+jrand_int_bound(r,3),p->bb.maxZ-3,MS_DIR_EAST,type);
            break;
        case MS_DIR_WEST:
            if (branch <= 1) ms_generate_and_add(start,r,p->bb.minX-1,p->bb.minY-1+jrand_int_bound(r,3),p->bb.minZ,MS_DIR_WEST,type);
            else if (branch == 2) ms_generate_and_add(start,r,p->bb.minX,p->bb.minY-1+jrand_int_bound(r,3),p->bb.minZ-1,MS_DIR_NORTH,type);
            else ms_generate_and_add(start,r,p->bb.minX,p->bb.minY-1+jrand_int_bound(r,3),p->bb.maxZ+1,MS_DIR_SOUTH,type);
            break;
        case MS_DIR_EAST:
            if (branch <= 1) ms_generate_and_add(start,r,p->bb.maxX+1,p->bb.minY-1+jrand_int_bound(r,3),p->bb.minZ,MS_DIR_EAST,type);
            else if (branch == 2) ms_generate_and_add(start,r,p->bb.maxX-3,p->bb.minY-1+jrand_int_bound(r,3),p->bb.minZ-1,MS_DIR_NORTH,type);
            else ms_generate_and_add(start,r,p->bb.maxX-3,p->bb.minY-1+jrand_int_bound(r,3),p->bb.maxZ+1,MS_DIR_SOUTH,type);
            break;
    }
    if (type < 8) {
        if (p->coord_base != MS_DIR_NORTH && p->coord_base != MS_DIR_SOUTH) {
            for (int x=p->bb.minX+3; x+3<=p->bb.maxX; x+=5) {
                int side=jrand_int_bound(r,5);
                if (side==0) ms_generate_and_add(start,r,x,p->bb.minY,p->bb.minZ-1,MS_DIR_NORTH,type+1);
                else if (side==1) ms_generate_and_add(start,r,x,p->bb.minY,p->bb.maxZ+1,MS_DIR_SOUTH,type+1);
            }
        } else {
            for (int z=p->bb.minZ+3; z+3<=p->bb.maxZ; z+=5) {
                int side=jrand_int_bound(r,5);
                if (side==0) ms_generate_and_add(start,r,p->bb.minX-1,p->bb.minY,z,MS_DIR_WEST,type+1);
                else if (side==1) ms_generate_and_add(start,r,p->bb.maxX+1,p->bb.minY,z,MS_DIR_EAST,type+1);
            }
        }
    }
}

MC_HD MC_NOINLINE static void ms_build_cross(MSStart *start, MSPiece *p,
                                              JavaRandom *r) {
    int t=p->component_type;
    switch (p->corridor_direction) {
        case MS_DIR_NORTH:
            ms_generate_and_add(start,r,p->bb.minX+1,p->bb.minY,p->bb.minZ-1,MS_DIR_NORTH,t);
            ms_generate_and_add(start,r,p->bb.minX-1,p->bb.minY,p->bb.minZ+1,MS_DIR_WEST,t);
            ms_generate_and_add(start,r,p->bb.maxX+1,p->bb.minY,p->bb.minZ+1,MS_DIR_EAST,t); break;
        case MS_DIR_SOUTH:
            ms_generate_and_add(start,r,p->bb.minX+1,p->bb.minY,p->bb.maxZ+1,MS_DIR_SOUTH,t);
            ms_generate_and_add(start,r,p->bb.minX-1,p->bb.minY,p->bb.minZ+1,MS_DIR_WEST,t);
            ms_generate_and_add(start,r,p->bb.maxX+1,p->bb.minY,p->bb.minZ+1,MS_DIR_EAST,t); break;
        case MS_DIR_WEST:
            ms_generate_and_add(start,r,p->bb.minX+1,p->bb.minY,p->bb.minZ-1,MS_DIR_NORTH,t);
            ms_generate_and_add(start,r,p->bb.minX+1,p->bb.minY,p->bb.maxZ+1,MS_DIR_SOUTH,t);
            ms_generate_and_add(start,r,p->bb.minX-1,p->bb.minY,p->bb.minZ+1,MS_DIR_WEST,t); break;
        case MS_DIR_EAST:
            ms_generate_and_add(start,r,p->bb.minX+1,p->bb.minY,p->bb.minZ-1,MS_DIR_NORTH,t);
            ms_generate_and_add(start,r,p->bb.minX+1,p->bb.minY,p->bb.maxZ+1,MS_DIR_SOUTH,t);
            ms_generate_and_add(start,r,p->bb.maxX+1,p->bb.minY,p->bb.minZ+1,MS_DIR_EAST,t); break;
    }
    if (p->is_multiple_floors) {
        if (jrand_next(r,1)) ms_generate_and_add(start,r,p->bb.minX+1,p->bb.minY+4,p->bb.minZ-1,MS_DIR_NORTH,t);
        if (jrand_next(r,1)) ms_generate_and_add(start,r,p->bb.minX-1,p->bb.minY+4,p->bb.minZ+1,MS_DIR_WEST,t);
        if (jrand_next(r,1)) ms_generate_and_add(start,r,p->bb.maxX+1,p->bb.minY+4,p->bb.minZ+1,MS_DIR_EAST,t);
        if (jrand_next(r,1)) ms_generate_and_add(start,r,p->bb.minX+1,p->bb.minY+4,p->bb.maxZ+1,MS_DIR_SOUTH,t);
    }
}

MC_HD MC_NOINLINE static void ms_build_stairs(MSStart *start, MSPiece *p,
                                               JavaRandom *r) {
    int t=p->component_type;
    switch (p->coord_base) {
        case MS_DIR_NORTH: ms_generate_and_add(start,r,p->bb.minX,p->bb.minY,p->bb.minZ-1,MS_DIR_NORTH,t); break;
        case MS_DIR_SOUTH: ms_generate_and_add(start,r,p->bb.minX,p->bb.minY,p->bb.maxZ+1,MS_DIR_SOUTH,t); break;
        case MS_DIR_WEST: ms_generate_and_add(start,r,p->bb.minX-1,p->bb.minY,p->bb.minZ,MS_DIR_WEST,t); break;
        case MS_DIR_EAST: ms_generate_and_add(start,r,p->bb.maxX+1,p->bb.minY,p->bb.minZ,MS_DIR_EAST,t); break;
    }
}

MC_HD MC_NOINLINE static void ms_build_component(MSStart *start, MSPiece *piece,
                                                  JavaRandom *r) {
    switch (piece->type) {
        case MS_P_ROOM: ms_build_room(start,piece,r); break;
        case MS_P_CORRIDOR: ms_build_corridor(start,piece,r); break;
        case MS_P_CROSS: ms_build_cross(start,piece,r); break;
        case MS_P_STAIRS: ms_build_stairs(start,piece,r); break;
    }
}

/* ---- Structure Generation ---- */

MC_HD MC_NOINLINE static void ms_generate(MSStart *start, MSWorld *w, JavaRandom *r,
                                          int cx, int cz, int mine_type) {
    memset(start, 0, sizeof(*start));
    start->cx = cx;
    start->cz = cz;
    start->mine_type = mine_type;
    start->rng_seed_before = r->seed;
    int wx = (cx << 4) + 2;
    int wz = (cz << 4) + 2;

    MSPiece *room = ms_add_piece(start);
    if (!room) return;
    room->type = MS_P_ROOM;
    int room_x = jrand_int_bound(r, 6);
    int room_y = jrand_int_bound(r, 6);
    int room_z = jrand_int_bound(r, 6);
    room->bb = msbb_create(wx, 50, wz,
        wx + 7 + room_x, 54 + room_y, wz + 7 + room_z);
    room->coord_base = -1;
    room->component_type = 0;

    ms_build_component(start, room, r);

    /* Compute total BB */
    start->total_bb = start->pieces[0].bb;
    for (int i = 1; i < start->piece_count; i++) {
        msbb_expand(&start->total_bb, &start->pieces[i].bb);
    }

    int y_offset;
    if(mine_type==MS_TYPE_MESA){
        int height=start->total_bb.maxY-start->total_bb.minY+1;
        y_offset=w->seaLevel-start->total_bb.maxY+height/2+5;
    }else{
        int available = w->seaLevel - 10;
        int height = start->total_bb.maxY - start->total_bb.minY + 2;
        if (height < available) height += jrand_int_bound(r, available - height);
        y_offset = height - start->total_bb.maxY;
    }
    msbb_offset(&start->total_bb, 0, y_offset, 0);
    for (int i = 0; i < start->piece_count; i++)
        msbb_offset(&start->pieces[i].bb, 0, y_offset, 0);
    for (int i = 0; i < start->entrance_count; ++i)
        msbb_offset(&start->entrances[i], 0, y_offset, 0);

    start->rng_seed_after = r->seed;
    start->valid = 1;
}

/* ---- Block Placement ---- */

/* Java checks only the expanded shell, clamped to the current population clip. */
MC_HD MC_NOINLINE static int ms_is_liquid_in_bb(MSWorld *w, const MSBB *bb,
                                                 const MSBB *clip) {
    int x0=ms_max(bb->minX-1,clip->minX), y0=ms_max(bb->minY-1,clip->minY);
    int z0=ms_max(bb->minZ-1,clip->minZ), x1=ms_min(bb->maxX+1,clip->maxX);
    int y1=ms_min(bb->maxY+1,clip->maxY), z1=ms_min(bb->maxZ+1,clip->maxZ);
    for(int x=x0;x<=x1;x++) for(int z=z0;z<=z1;z++)
        if(ms_is_liquid(ms_get(w,x,y0,z))||ms_is_liquid(ms_get(w,x,y1,z))) return 1;
    for(int x=x0;x<=x1;x++) for(int y=y0;y<=y1;y++)
        if(ms_is_liquid(ms_get(w,x,y,z0))||ms_is_liquid(ms_get(w,x,y,z1))) return 1;
    for(int z=z0;z<=z1;z++) for(int y=y0;y<=y1;y++)
        if(ms_is_liquid(ms_get(w,x0,y,z))||ms_is_liquid(ms_get(w,x1,y,z))) return 1;
    return 0;
}

/* StructureComponent.getSkyBrightness queries local y+1. Outside the clip it
 * returns SKY's default 15. ChunkPrimer has no light nibble, so for structure
 * placement use the exact binary distinction needed underground: any overhead
 * block closes the column. */
MC_HD MC_NOINLINE static int ms_sky(MSWorld *w,const MSPiece *p,const MSBB *clip,
                                    int lx,int ly,int lz){
    int x=ms_get_x(p,lx,lz),y=ms_get_y(p,ly+1),z=ms_get_z(p,lx,lz);
    if(!msbb_contains(clip,x,y,z)) return 15;
    for(int yy=y;yy<256;yy++) if(ms_get(w,x,yy,z)!=MS_AIR) return 0;
    return 15;
}

MC_HD MC_NOINLINE static void ms_fill_rare(MSWorld *w,const MSPiece *p,const MSBB *clip,
        int x0,int y0,int z0,int x1,int y1,int z1,int id){
    float sx=(float)(x1-x0+1), sy=(float)(y1-y0+1), sz=(float)(z1-z0+1);
    float cx=(float)x0+sx/2.0f, cz=(float)z0+sz/2.0f;
    for(int y=y0;y<=y1;y++){
        float fy=(float)(y-y0)/sy;
        for(int x=x0;x<=x1;x++){
            float fx=((float)x-cx)/(sx*0.5f);
            for(int z=z0;z<=z1;z++){
                float fz=((float)z-cz)/(sz*0.5f);
                if(fx*fx+fy*fy+fz*fz<=1.05f) ms_place(w,p,clip,id,0,x,y,z);
            }
        }
    }
}

MC_HD MC_NOINLINE static void ms_random_fill_sky(MSWorld *w,const MSPiece *p,
        const MSBB *clip,JavaRandom *r,float prob,int x0,int y0,int z0,
        int x1,int y1,int z1,int outer,int inner,int sky_limit){
    for(int y=y0;y<=y1;y++) for(int x=x0;x<=x1;x++) for(int z=z0;z<=z1;z++){
        float draw=jrand_float(r);
        if(draw<=prob && (sky_limit<=0 || ms_sky(w,p,clip,x,y,z)<sky_limit)){
            int edge=(y==y0||y==y1||x==x0||x==x1||z==z0||z==z1);
            ms_place(w,p,clip,edge?outer:inner,0,x,y,z);
        }
    }
}

MC_HD MC_NOINLINE static int ms_supporting_box(MSWorld *w,const MSPiece *p,
        const MSBB *clip,int x0,int x1,int ceiling_y,int z){
    for(int x=x0;x<=x1;x++)
        if(ms_get_local(w,p,clip,x,ceiling_y+1,z)==MS_AIR) return 0;
    return 1;
}

MC_HD MC_NOINLINE static void ms_place_support(MSWorld *w,const MSPiece *p,
        const MSBB *clip,JavaRandom *r,int x0,int y0,int z,int x1,int y1){
    if(!ms_supporting_box(w,p,clip,x0,x1,y1,z)) return;
    ms_fill(w,p,clip,x0,y0,z,x0,y1-1,z,MS_FENCE,MS_AIR,0);
    ms_fill(w,p,clip,x1,y0,z,x1,y1-1,z,MS_FENCE,MS_AIR,0);
    if(jrand_int_bound(r,4)==0){
        ms_fill(w,p,clip,x0,y1,z,x0,y1,z,MS_PLANKS,MS_AIR,0);
        ms_fill(w,p,clip,x1,y1,z,x1,y1,z,MS_PLANKS,MS_AIR,0);
    }else{
        ms_fill(w,p,clip,x0,y1,z,x1,y1,z,MS_PLANKS,MS_AIR,0);
        ms_random_block(w,p,clip,r,0.05f,x0+1,y1,z-1,MS_TORCH,4);
        ms_random_block(w,p,clip,r,0.05f,x0+1,y1,z+1,MS_TORCH,3);
    }
}

MC_HD MC_NOINLINE static void ms_place_cobweb(MSWorld *w,const MSPiece *p,
        const MSBB *clip,JavaRandom *r,float chance,int x,int y,int z){
    if(ms_sky(w,p,clip,x,y,z)<8)
        ms_random_block(w,p,clip,r,chance,x,y,z,MS_WEB,0);
}

MC_HD MC_NOINLINE static void ms_place_room(MSWorld *w,MSStart *start,MSPiece *p,
                                             const MSBB *clip) {
    if (ms_is_liquid_in_bb(w, &p->bb, clip)) return;
    ms_fill(w,p,clip,p->bb.minX,p->bb.minY,p->bb.minZ,
            p->bb.maxX,p->bb.minY,p->bb.maxZ,MS_DIRT,MS_AIR,1);
    ms_fill(w,p,clip,p->bb.minX,p->bb.minY+1,p->bb.minZ,
            p->bb.maxX,ms_min(p->bb.minY+3,p->bb.maxY),p->bb.maxZ,
            MS_AIR,MS_AIR,0);
    for(int i=0;i<start->entrance_count;i++){
        MSBB *e=&start->entrances[i];
        ms_fill(w,p,clip,e->minX,e->maxY-2,e->minZ,e->maxX,e->maxY,e->maxZ,
                MS_AIR,MS_AIR,0);
    }
    ms_fill_rare(w,p,clip,p->bb.minX,p->bb.minY+4,p->bb.minZ,
                 p->bb.maxX,p->bb.maxY,p->bb.maxZ,MS_AIR);
}

MC_HD MC_NOINLINE static void ms_place_corridor(MSWorld *w,MSStart *start,
                                                MSPiece *p,JavaRandom *r,const MSBB *clip) {
    if (ms_is_liquid_in_bb(w, &p->bb, clip)) return;

    int sections = p->section_count;
    int total_len = sections * 5 - 1;

    /* Clear main tunnel */
    ms_fill(w, p, clip, 0, 0, 0, 2, 1, total_len, MS_AIR, MS_AIR, 0);
    /* Random air above (80% chance per block) */
    ms_random_fill(w, p, clip, r, 0.8f, 0, 2, 0, 2, 2, total_len, MS_AIR, MS_AIR, 0);

    /* Cobwebs for spider corridors */
    if (p->has_spiders)
        ms_random_fill_sky(w,p,clip,r,0.6f,0,0,0,2,1,total_len,
                           MS_WEB,MS_AIR,8);

    /* Support pillars every 5 blocks */
    for (int s = 0; s < sections; s++) {
        int sz = 2 + s * 5;

        ms_place_support(w,p,clip,r,0,0,sz,2,2);

        /* Cobweb decoration around supports */
        ms_place_cobweb(w,p,clip,r,0.1f,0,2,sz-1);
        ms_place_cobweb(w,p,clip,r,0.1f,2,2,sz-1);
        ms_place_cobweb(w,p,clip,r,0.1f,0,2,sz+1);
        ms_place_cobweb(w,p,clip,r,0.1f,2,2,sz+1);
        ms_place_cobweb(w,p,clip,r,0.05f,0,2,sz-2);
        ms_place_cobweb(w,p,clip,r,0.05f,2,2,sz-2);
        ms_place_cobweb(w,p,clip,r,0.05f,0,2,sz+2);
        ms_place_cobweb(w,p,clip,r,0.05f,2,2,sz+2);

        /* Chests (1% chance per section, both sides) */
        if(jrand_int_bound(r,100)==0){
            int x=2,z=sz-1;
            if(ms_get_local(w,p,clip,x,0,z)==MS_AIR &&
               ms_get_local(w,p,clip,x,-1,z)!=MS_AIR){
                ms_place(w,p,clip,MS_RAIL,jrand_next(r,1)?0:1,x,0,z);
                i64 loot=jrand_long(r); p->chest_placed++;
                if(start->cart_count<MS_MAX_CARTS){
                    MSCartEvent *e=&start->carts[start->cart_count++];
                    e->x=ms_get_x(p,x,z); e->y=ms_get_y(p,0);
                    e->z=ms_get_z(p,x,z); e->loot_seed=loot;
                }
            }
        }
        if(jrand_int_bound(r,100)==0){
            int x=0,z=sz+1;
            if(ms_get_local(w,p,clip,x,0,z)==MS_AIR &&
               ms_get_local(w,p,clip,x,-1,z)!=MS_AIR){
                ms_place(w,p,clip,MS_RAIL,jrand_next(r,1)?0:1,x,0,z);
                i64 loot=jrand_long(r); p->chest_placed++;
                if(start->cart_count<MS_MAX_CARTS){
                    MSCartEvent *e=&start->carts[start->cart_count++];
                    e->x=ms_get_x(p,x,z); e->y=ms_get_y(p,0);
                    e->z=ms_get_z(p,x,z); e->loot_seed=loot;
                }
            }
        }

        /* Spider spawner */
        if (p->has_spiders && !p->spawner_placed) {
            int sy = ms_get_y(p, 0);
            int si = sz - 1 + jrand_int_bound(r, 3);
            int sx = ms_get_x(p, 1, si);
            int ssz = ms_get_z(p, 1, si);
            if (msbb_contains(clip, sx, sy, ssz) && ms_sky(w,p,clip,1,0,si)<8) {
                p->spawner_placed = 1;
                ms_set_id(w,sx,sy,ssz,MS_MOB_SPAWNER);
                if(start->spawner_count<MS_MAX_SPAWNERS){
                    MSSpawnerEvent *e=&start->spawners[start->spawner_count++];
                    e->x=sx; e->y=sy; e->z=ssz;
                }
            }
        }
    }

    /* Plank floor under air gaps */
    for (int x = 0; x <= 2; x++) {
        for (int z = 0; z <= total_len; z++) {
            int b = ms_get_local(w, p, clip, x, -1, z);
            if (b == MS_AIR && ms_sky(w,p,clip,x,-1,z)<8) {
                ms_place(w, p, clip, MS_PLANKS, 0, x, -1, z);
            }
        }
    }

    /* Rails */
    if (p->has_rails) {
        for (int z = 0; z <= total_len; z++) {
            int below = ms_get_local(w, p, clip, 1, -1, z);
            if (below != MS_AIR && ms_is_solid(below)) {
                float chance=ms_sky(w,p,clip,1,0,z)>8?0.9f:0.7f;
                ms_random_block(w,p,clip,r,chance,1,0,z,MS_RAIL,0);
            }
        }
    }
}

MC_HD MC_NOINLINE static void ms_cross_pillar(MSWorld *w,const MSPiece *p,
        const MSBB *clip,int x,int z){
    if(ms_get_local(w,p,clip,x,p->bb.maxY+1,z)!=MS_AIR)
        ms_fill(w,p,clip,x,p->bb.minY,z,x,p->bb.maxY,z,
                MS_PLANKS,MS_AIR,0);
}

MC_HD MC_NOINLINE static void ms_place_cross(MSWorld *w, MSPiece *p, const MSBB *clip) {
    if (ms_is_liquid_in_bb(w, &p->bb, clip)) return;
    if(p->is_multiple_floors){
        ms_fill(w,p,clip,p->bb.minX+1,p->bb.minY,p->bb.minZ,
                p->bb.maxX-1,p->bb.minY+2,p->bb.maxZ,MS_AIR,MS_AIR,0);
        ms_fill(w,p,clip,p->bb.minX,p->bb.minY,p->bb.minZ+1,
                p->bb.maxX,p->bb.minY+2,p->bb.maxZ-1,MS_AIR,MS_AIR,0);
        ms_fill(w,p,clip,p->bb.minX+1,p->bb.maxY-2,p->bb.minZ,
                p->bb.maxX-1,p->bb.maxY,p->bb.maxZ,MS_AIR,MS_AIR,0);
        ms_fill(w,p,clip,p->bb.minX,p->bb.maxY-2,p->bb.minZ+1,
                p->bb.maxX,p->bb.maxY,p->bb.maxZ-1,MS_AIR,MS_AIR,0);
        ms_fill(w,p,clip,p->bb.minX+1,p->bb.minY+3,p->bb.minZ+1,
                p->bb.maxX-1,p->bb.minY+3,p->bb.maxZ-1,MS_AIR,MS_AIR,0);
    }else{
        ms_fill(w,p,clip,p->bb.minX+1,p->bb.minY,p->bb.minZ,
                p->bb.maxX-1,p->bb.maxY,p->bb.maxZ,MS_AIR,MS_AIR,0);
        ms_fill(w,p,clip,p->bb.minX,p->bb.minY,p->bb.minZ+1,
                p->bb.maxX,p->bb.maxY,p->bb.maxZ-1,MS_AIR,MS_AIR,0);
    }
    ms_cross_pillar(w,p,clip,p->bb.minX+1,p->bb.minZ+1);
    ms_cross_pillar(w,p,clip,p->bb.minX+1,p->bb.maxZ-1);
    ms_cross_pillar(w,p,clip,p->bb.maxX-1,p->bb.minZ+1);
    ms_cross_pillar(w,p,clip,p->bb.maxX-1,p->bb.maxZ-1);
    for(int x=p->bb.minX;x<=p->bb.maxX;x++)
        for(int z=p->bb.minZ;z<=p->bb.maxZ;z++)
            if(ms_get_local(w,p,clip,x,p->bb.minY-1,z)==MS_AIR &&
               ms_sky(w,p,clip,x,p->bb.minY-1,z)<8)
                ms_place(w,p,clip,MS_PLANKS,0,x,p->bb.minY-1,z);
}

MC_HD MC_NOINLINE static void ms_place_stairs(MSWorld *w, MSPiece *p, const MSBB *clip) {
    if (ms_is_liquid_in_bb(w, &p->bb, clip)) return;

    /* Clear top section */
    ms_fill(w, p, clip, 0, 5, 0, 2, 7, 1, MS_AIR, MS_AIR, 0);
    /* Clear bottom section */
    ms_fill(w, p, clip, 0, 0, 7, 2, 2, 8, MS_AIR, MS_AIR, 0);

    /* Diagonal staircase */
    for (int i = 0; i < 5; i++) {
        int ytop = 5 - i - (i < 4 ? 1 : 0);
        ms_fill(w, p, clip, 0, ytop, 2 + i, 2, 7 - i, 2 + i, MS_AIR, MS_AIR, 0);
    }
}

MC_HD static inline int ms_has_rail(MSWorld *w,int x,int y,int z){
    return ms_get(w,x,y,z)==MS_RAIL||ms_get(w,x,y+1,z)==MS_RAIL||
           (y>0&&ms_get(w,x,y-1,z)==MS_RAIL);
}

/* BlockRail.onBlockAdded reconnects every placed rail. Mineshaft tracks are
 * sparse straight runs, but isolated draws reset to NORTH_SOUTH and adjacent
 * runs can form vanilla slopes/corners. Resolve the same final shapes after
 * all intersecting pieces have written the clip. */
MC_HD MC_NOINLINE static void ms_update_rails(MSWorld *w,const MSBB *clip){
    if(!w->storeMeta) return;
    for(int y=clip->minY;y<=clip->maxY;y++)
        for(int z=clip->minZ;z<=clip->maxZ;z++)
            for(int x=clip->minX;x<=clip->maxX;x++){
                if(ms_get(w,x,y,z)!=MS_RAIL) continue;
                int n=ms_has_rail(w,x,y,z-1),s=ms_has_rail(w,x,y,z+1);
                int west=ms_has_rail(w,x-1,y,z),east=ms_has_rail(w,x+1,y,z);
                int meta=0;
                if((west||east)&&!n&&!s) meta=1;
                else if(s&&east&&!n&&!west) meta=6;
                else if(s&&west&&!n&&!east) meta=7;
                else if(n&&west&&!s&&!east) meta=8;
                else if(n&&east&&!s&&!west) meta=9;
                else if(west||east) meta=1;
                if(meta==0){
                    if(ms_get(w,x,y+1,z-1)==MS_RAIL) meta=4;
                    if(ms_get(w,x,y+1,z+1)==MS_RAIL) meta=5;
                }else if(meta==1){
                    if(ms_get(w,x+1,y+1,z)==MS_RAIL) meta=2;
                    if(ms_get(w,x-1,y+1,z)==MS_RAIL) meta=3;
                }
                ms_set(w,x,y,z,w->storeMeta==2?
                       (0x4000|(MS_RAIL<<4)|meta):(MS_RAIL<<4|meta));
            }
}

MC_HD MC_NOINLINE static void ms_place_blocks_clip(MSWorld *w,MSStart *start,
                                                    JavaRandom *r,const MSBB *clip) {
    for (int i = 0; i < start->piece_count; i++) {
        MSPiece *p = &start->pieces[i];
        if(!msbb_intersects(&p->bb,clip)) continue;
        switch (p->type) {
            case MS_P_ROOM:     ms_place_room(w,start,p,clip); break;
            case MS_P_CORRIDOR: ms_place_corridor(w,start,p,r,clip); break;
            case MS_P_CROSS:    ms_place_cross(w,p,clip); break;
            case MS_P_STAIRS:   ms_place_stairs(w,p,clip); break;
        }
    }
    ms_update_rails(w,clip);
}

MC_HD MC_NOINLINE static void ms_place_blocks(MSWorld *w, MSStart *start) {
    MSBB clip={w->chunkX*16,0,w->chunkZ*16,w->chunkX*16+15,255,w->chunkZ*16+15};
    JavaRandom r;
    jrand_set(&r,(i64)start->cx*341873128712LL+(i64)start->cz*132897987541LL);
    ms_place_blocks_clip(w,start,&r,&clip);
}

#define MS_MAX_STARTS 8
typedef struct { MSStart starts[MS_MAX_STARTS]; int count; i64 worldSeed; } MSGen;

MC_HD MC_NOINLINE static int ms_can_spawn(JavaRandom *r, int cx, int cz) {
    if (jrand_double(r) >= 0.004) return 0;
    int m = cx < 0 ? -cx : cx;
    int n = cz < 0 ? -cz : cz;
    return jrand_int_bound(r, 80) < (m > n ? m : n);
}

MC_HD MC_NOINLINE static int ms_try_spawn(MSGen *g, JavaRandom *r, int cx, int cz) {
    (void)g;
    return ms_can_spawn(r, cx, cz);
}

MC_HD static inline void ms_place_all(MSWorld *w, MSStart *s) { ms_place_blocks(w, s); }

MC_HD MC_NOINLINE static void ms_generate_map(MSGen *g, i64 worldSeed, int x, int z) {
    g->count = 0; g->worldSeed = worldSeed;
    MSWorld dummy; memset(&dummy,0,sizeof(dummy)); dummy.chunkX=x; dummy.chunkZ=z; dummy.worldSeed=worldSeed; dummy.seaLevel=63;
    int range = MS_RANGE;
    JavaRandom rand; jrand_set(&rand, worldSeed);
    i64 j = jrand_long(&rand), k = jrand_long(&rand);
    for (int l = x - range; l <= x + range; ++l)
        for (int i1 = z - range; i1 <= z + range; ++i1) {
            u64 mixed = (u64)(i64)l * (u64)j
                      ^ (u64)(i64)i1 * (u64)k ^ (u64)worldSeed;
            jrand_set(&rand, (i64)mixed);
            jrand_int(&rand);
            if (ms_try_spawn(g, &rand, l, i1) && g->count < MS_MAX_STARTS) {
                MSStart *s = &g->starts[g->count++];
                int mine_type=MS_TYPE_NORMAL;
#ifndef __CUDA_ARCH__
                if(g_ms_type_at)
                    mine_type=g_ms_type_at(worldSeed,(l<<4)+8,(i1<<4)+8);
#endif
                ms_generate(s, &dummy, &rand, l, i1, mine_type);
            }
        }
}

MC_HD MC_NOINLINE static void ms_generate_structure(MSWorld *w,MSGen *g,int cx,int cz){
    MSBB clip={cx*16,0,cz*16,cx*16+15,255,cz*16+15};
    for(int i=0;i<g->count;++i) if(g->starts[i].valid && msbb_intersects(&g->starts[i].total_bb,&clip)) ms_place_all(w,&g->starts[i]);
}

/* Exact populate-time path. Java's StructureBoundingBox for a ChunkPos begins
 * at block +8 and shares the provider Random with every intersecting start. */
MC_HD MC_NOINLINE static void ms_generate_population_window(MSWorld *w,MSGen *g,
        JavaRandom *placement,i64 seed,int cx,int cz){
    MSBB clip={cx*16+8,0,cz*16+8,cx*16+23,255,cz*16+23};
    ms_generate_map(g,seed,cx,cz);
    for(int i=0;i<g->count;i++)
        if(g->starts[i].valid&&msbb_intersects(&g->starts[i].total_bb,&clip)){
            ms_place_blocks_clip(w,&g->starts[i],placement,&clip);
#ifndef __CUDA_ARCH__
            if(g_ms_event){
                for(int j=0;j<g->starts[i].cart_count;j++){
                    const MSCartEvent *e=&g->starts[i].carts[j];
                    g_ms_event(cx,cz,MS_EVENT_CART,e->x,e->y,e->z,e->loot_seed);
                }
                for(int j=0;j<g->starts[i].spawner_count;j++){
                    const MSSpawnerEvent *e=&g->starts[i].spawners[j];
                    g_ms_event(cx,cz,MS_EVENT_SPAWNER,e->x,e->y,e->z,0);
                }
            }
#endif
        }
}

#ifdef __CUDACC__
__device__ MSGen ms_cuda_gen;
#endif

MC_HD MC_NOINLINE static void ms_run(ChunkPrimer *primer,i64 seed,int cx,int cz){
    for(int i=0;i<65536;++i) primer->data[i]=(u16)MS_STONE;
    MSWorld w; memset(&w,0,sizeof(w)); w.primer=primer; w.chunkX=cx; w.chunkZ=cz; w.worldSeed=seed; w.seaLevel=63;
#ifdef __CUDA_ARCH__
    ms_generate_map(&ms_cuda_gen,seed,cx,cz);
    ms_generate_structure(&w,&ms_cuda_gen,cx,cz);
#else
    MSGen g; ms_generate_map(&g,seed,cx,cz);
    ms_generate_structure(&w,&g,cx,cz);
#endif
}
#endif
