/* map_gen_mineshaft: port of MC 1.11.2 structure generator (mineshaft). */
#ifndef MC_MAP_GEN_MINESHAFT_H
#define MC_MAP_GEN_MINESHAFT_H
#include "mc.h"
#include "mc_rng.h"
#include <string.h>

enum { MS_AIR=0, MS_STONE=1, MS_GRASS=2, MS_DIRT=3, MS_PLANKS=5, MS_WATER=9, MS_FLOWING_WATER=8,
    MS_TORCH=50, MS_WEB=30, MS_FENCE=85, MS_RAIL=66, MS_CHEST=54, MS_MOB_SPAWNER=52, MS_LAVA=11, MS_FLOWING_LAVA=10 };
enum { MS_P_ROOM=200, MS_P_CORRIDOR, MS_P_STAIRS, MS_P_CROSS };
#define MS_RANGE 8
#define MS_MAX_PIECES 64
typedef struct { int minX,minY,minZ,maxX,maxY,maxZ; } MSBB;
typedef struct { int type,coord_base,component_type,has_spawner,has_rails,has_spiders,spawner_placed,section_count,is_multiple_floors,corridor_direction,is_large_room,portal_room,chest_placed; MSBB bb; } MSPiece;
typedef struct { int cx,cz,piece_count,valid; MSBB total_bb; MSPiece pieces[MS_MAX_PIECES]; } MSStart;
#ifndef MC_CHUNK_PROVIDER_H
typedef struct { u16 data[65536]; } ChunkPrimer;
#endif
typedef struct { ChunkPrimer *primer; int chunkX,chunkZ; i64 worldSeed; int seaLevel; } MSWorld;
MC_HD static inline int ms_idx(int x,int y,int z){ return x<<12|z<<8|y; }
MC_HD static inline int ms_in_chunk(int x,int y,int z,int cx,int cz){ return x>=cx*16&&x<cx*16+16&&z>=cz*16&&z<cz*16+16&&y>=0&&y<256; }
MC_HD static inline void ms_set(MSWorld *w,int x,int y,int z,int v){ if(ms_in_chunk(x,y,z,w->chunkX,w->chunkZ)) w->primer->data[ms_idx(x&15,y,z&15)]=(u16)v; }
MC_HD static inline int ms_get(MSWorld *w,int x,int y,int z){ if(!ms_in_chunk(x,y,z,w->chunkX,w->chunkZ)) return MS_AIR; return (int)w->primer->data[ms_idx(x&15,y,z&15)]; }
MC_HD static inline int ms_is_solid(int b){ return b!=MS_AIR && b!=MS_WATER && b!=MS_FLOWING_WATER; }

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
    int wx=ms_get_x(p,lx,lz),wy=ms_get_y(p,ly),wz=ms_get_z(p,lx,lz); (void)meta;
    if(msbb_contains(clip,wx,wy,wz)) ms_set(w,wx,wy,wz,id); }
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
    if(jrand_float(r)<=prob) ms_place(w,p,clip,id,meta,lx,ly,lz); }
MC_HD MC_NOINLINE static void ms_replace_down(MSWorld *w,const MSPiece *p,const MSBB *clip,int id,int meta,int lx,int ly,int lz){
    int wx=ms_get_x(p,lx,lz),wz=ms_get_z(p,lx,lz),wy=ms_get_y(p,ly); (void)meta;
    while(wy>0 && msbb_contains(clip,wx,wy,wz)){ int cur=ms_get(w,wx,wy,wz); if(cur!=MS_AIR && ms_is_solid(cur)) break; ms_set(w,wx,wy,wz,id); wy--; } }
MC_HD MC_NOINLINE static int ms_find_intersect(const MSPiece *ps,int n,const MSBB *bb){
    for(int i=0;i<n;i++) if(msbb_intersects(&ps[i].bb,bb)) return i; return -1; }

/* ---- Piece creation ---- */

MC_HD MC_NOINLINE static MSPiece *ms_add_piece(MSStart *start) {
    if (start->piece_count >= MS_MAX_PIECES) return NULL;
    MSPiece *p = &start->pieces[start->piece_count++];
    memset(p, 0, sizeof(*p));
    return p;
}

/* Forward declarations */
MC_HD MC_NOINLINE static void ms_build_component(MSStart *start, MSPiece *piece,
                                  JavaRandom *r, MSPiece *root, int depth);

/* Get next component location from an exit */
MC_HD MC_NOINLINE static int ms_try_add_next(MSStart *start, JavaRandom *r,
                               MSPiece *root,
                               int x, int y, int z, int dir, int depth);

/* Create a corridor piece */
MC_HD MC_NOINLINE static int ms_create_corridor(MSStart *start, JavaRandom *r,
                                  int x, int y, int z, int dir, int depth)
{
    /* Calculate corridor length */
    int len = jrand_int_bound(r, 3) + 2;
    MSBB bb;
    switch (dir) {
        case 0: bb = msbb_create(x, y, z, x + 2, y + 2, z + len * 5 - 1); break;
        case 1: bb = msbb_create(x - len * 5 + 1, y, z, x, y + 2, z + 2); break;
        case 2: bb = msbb_create(x, y, z - len * 5 + 1, x + 2, y + 2, z); break;
        case 3: bb = msbb_create(x, y, z, x + len * 5 - 1, y + 2, z + 2); break;
        default: return 0;
    }

    if (ms_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    MSPiece *p = ms_add_piece(start);
    if (!p) return 0;
    p->type = MS_P_CORRIDOR;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    p->has_rails = jrand_int_bound(r, 3) == 0;
    p->has_spiders = !p->has_rails && jrand_int_bound(r, 23) == 0;
    p->section_count = len;
    return 1;
}

/* Create a cross (intersection) piece */
MC_HD MC_NOINLINE static int ms_create_cross(MSStart *start, JavaRandom *r,
                               int x, int y, int z, int dir, int depth)
{
    int is_multi = (jrand_int_bound(r, 4) == 0);
    int h = is_multi ? 6 : 3;
    MSBB bb;
    switch (dir) {
        case 0: bb = msbb_create(x - 1, y, z, x + 3, y + h - 1, z + 4); break;
        case 1: bb = msbb_create(x - 4, y, z - 1, x, y + h - 1, z + 3); break;
        case 2: bb = msbb_create(x - 1, y, z - 4, x + 3, y + h - 1, z); break;
        case 3: bb = msbb_create(x, y, z - 1, x + 4, y + h - 1, z + 3); break;
        default: return 0;
    }

    if (ms_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    MSPiece *p = ms_add_piece(start);
    if (!p) return 0;
    p->type = MS_P_CROSS;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    p->is_multiple_floors = is_multi;
    p->corridor_direction = dir;
    return 1;
}

/* Create a stairs piece */
MC_HD MC_NOINLINE static int ms_create_stairs(MSStart *start, JavaRandom *r,
                                int x, int y, int z, int dir, int depth)
{
    MSBB bb;
    switch (dir) {
        case 0: bb = msbb_create(x, y - 5, z, x + 2, y + 2, z + 8); break;
        case 1: bb = msbb_create(x - 8, y - 5, z, x, y + 2, z + 2); break;
        case 2: bb = msbb_create(x, y - 5, z - 8, x + 2, y + 2, z); break;
        case 3: bb = msbb_create(x, y - 5, z, x + 8, y + 2, z + 2); break;
        default: return 0;
    }

    if (ms_find_intersect(start->pieces, start->piece_count, &bb) >= 0) return 0;

    MSPiece *p = ms_add_piece(start);
    if (!p) return 0;
    p->type = MS_P_STAIRS;
    p->bb = bb;
    p->coord_base = dir;
    p->component_type = depth;
    return 1;
}

/* Try to add a random next component */
MC_HD MC_NOINLINE static int ms_try_add_next(MSStart *start, JavaRandom *r,
                               MSPiece *root,
                               int x, int y, int z, int dir, int depth)
{
    if (depth > 8) return 0;
    if (abs(x - root->bb.minX) > 80 || abs(z - root->bb.minZ) > 80) return 0;

    int choice = jrand_int_bound(r, 100);
    int old_count = start->piece_count;
    int ok;

    if (choice < 70) {
        ok = ms_create_corridor(start, r, x, y, z, dir, depth);
    } else if (choice < 85) {
        ok = ms_create_cross(start, r, x, y, z, dir, depth);
    } else {
        ok = ms_create_stairs(start, r, x, y, z, dir, depth);
    }

    if (ok) {
        /* Build from new piece */
        for (int i = old_count; i < start->piece_count; i++) {
            ms_build_component(start, &start->pieces[i], r, root, depth + 1);
        }
        return 1;
    }
    return 0;
}

/* Build child components from a piece */
MC_HD MC_NOINLINE static void ms_build_component(MSStart *start, MSPiece *piece,
                                  JavaRandom *r, MSPiece *root, int depth)
{
    if (depth > 8 || start->piece_count >= MS_MAX_PIECES - 10) return;

    int dir = piece->coord_base;

    switch (piece->type) {
        case MS_P_CORRIDOR: {
            /* Exit at end of corridor */
            int ex, ey, ez;
            switch (dir) {
                case 0: ex = piece->bb.minX + 1; ey = piece->bb.minY; ez = piece->bb.maxZ + 1; break;
                case 1: ex = piece->bb.minX - 1; ey = piece->bb.minY; ez = piece->bb.minZ + 1; break;
                case 2: ex = piece->bb.minX + 1; ey = piece->bb.minY; ez = piece->bb.minZ - 1; break;
                case 3: ex = piece->bb.maxX + 1; ey = piece->bb.minY; ez = piece->bb.minZ + 1; break;
                default: return;
            }
            ms_try_add_next(start, r, root, ex, ey, ez, dir, depth);

            /* Random side branches (1 per section, 50% chance left or right) */
            for (int s = 0; s < piece->section_count && start->piece_count < MS_MAX_PIECES - 10; s++) {
                int branch = jrand_int_bound(r, 4);
                if (branch == 0) {
                    /* Left branch */
                    int bx, bz, bdir;
                    switch (dir) {
                        case 0: bx = piece->bb.minX - 1; bz = piece->bb.minZ + 1 + s * 5; bdir = 1; break;
                        case 1: bx = piece->bb.minX + 1 + s * 5; bz = piece->bb.minZ - 1; bdir = 2; break;
                        case 2: bx = piece->bb.minX - 1; bz = piece->bb.maxZ - 1 - s * 5; bdir = 1; break;
                        case 3: bx = piece->bb.maxX - 1 - s * 5; bz = piece->bb.minZ - 1; bdir = 2; break;
                        default: continue;
                    }
                    ms_try_add_next(start, r, root, bx, piece->bb.minY, bz, bdir, depth);
                } else if (branch == 1) {
                    /* Right branch */
                    int bx, bz, bdir;
                    switch (dir) {
                        case 0: bx = piece->bb.maxX + 1; bz = piece->bb.minZ + 1 + s * 5; bdir = 3; break;
                        case 1: bx = piece->bb.minX + 1 + s * 5; bz = piece->bb.maxZ + 1; bdir = 0; break;
                        case 2: bx = piece->bb.maxX + 1; bz = piece->bb.maxZ - 1 - s * 5; bdir = 3; break;
                        case 3: bx = piece->bb.maxX - 1 - s * 5; bz = piece->bb.maxZ + 1; bdir = 0; break;
                        default: continue;
                    }
                    ms_try_add_next(start, r, root, bx, piece->bb.minY, bz, bdir, depth);
                }
            }
            break;
        }

        case MS_P_CROSS: {
            /* Exits in all 4 directions */
            int cx_c = (piece->bb.minX + piece->bb.maxX) / 2;
            int cz_c = (piece->bb.minZ + piece->bb.maxZ) / 2;
            int cy_c = piece->bb.minY;

            /* Forward */
            switch (dir) {
                case 0: ms_try_add_next(start, r, root, cx_c, cy_c, piece->bb.maxZ + 1, 0, depth); break;
                case 1: ms_try_add_next(start, r, root, piece->bb.minX - 1, cy_c, cz_c, 1, depth); break;
                case 2: ms_try_add_next(start, r, root, cx_c, cy_c, piece->bb.minZ - 1, 2, depth); break;
                case 3: ms_try_add_next(start, r, root, piece->bb.maxX + 1, cy_c, cz_c, 3, depth); break;
            }
            /* Left */
            int ldir = (dir + 1) % 4;
            switch (ldir) {
                case 0: ms_try_add_next(start, r, root, cx_c, cy_c, piece->bb.maxZ + 1, 0, depth); break;
                case 1: ms_try_add_next(start, r, root, piece->bb.minX - 1, cy_c, cz_c, 1, depth); break;
                case 2: ms_try_add_next(start, r, root, cx_c, cy_c, piece->bb.minZ - 1, 2, depth); break;
                case 3: ms_try_add_next(start, r, root, piece->bb.maxX + 1, cy_c, cz_c, 3, depth); break;
            }
            /* Right */
            int rdir = (dir + 3) % 4;
            switch (rdir) {
                case 0: ms_try_add_next(start, r, root, cx_c, cy_c, piece->bb.maxZ + 1, 0, depth); break;
                case 1: ms_try_add_next(start, r, root, piece->bb.minX - 1, cy_c, cz_c, 1, depth); break;
                case 2: ms_try_add_next(start, r, root, cx_c, cy_c, piece->bb.minZ - 1, 2, depth); break;
                case 3: ms_try_add_next(start, r, root, piece->bb.maxX + 1, cy_c, cz_c, 3, depth); break;
            }
            break;
        }

        case MS_P_STAIRS: {
            /* Exit at bottom of stairs */
            int ex, ey, ez;
            switch (dir) {
                case 0: ex = piece->bb.minX + 1; ey = piece->bb.minY; ez = piece->bb.maxZ + 1; break;
                case 1: ex = piece->bb.minX - 1; ey = piece->bb.minY; ez = piece->bb.minZ + 1; break;
                case 2: ex = piece->bb.minX + 1; ey = piece->bb.minY; ez = piece->bb.minZ - 1; break;
                case 3: ex = piece->bb.maxX + 1; ey = piece->bb.minY; ez = piece->bb.minZ + 1; break;
                default: return;
            }
            ms_try_add_next(start, r, root, ex, ey, ez, dir, depth);
            break;
        }

        case MS_P_ROOM: {
            /* Room spawns corridors from each linked exit */
            int room_cx = (piece->bb.minX + piece->bb.maxX) / 2;
            int room_cz = (piece->bb.minZ + piece->bb.maxZ) / 2;
            int room_y = piece->bb.minY + 1;

            /* Try exits in all 4 directions along each wall */
            for (int d = 0; d < 4; d++) {
                int num_exits = jrand_int_bound(r, 4);
                for (int e = 0; e < num_exits; e++) {
                    int ex, ez;
                    switch (d) {
                        case 0: /* south wall */
                            ex = piece->bb.minX + jrand_int_bound(r, msbb_x_size(&piece->bb));
                            ez = piece->bb.maxZ + 1;
                            ms_try_add_next(start, r, &start->pieces[0], ex, room_y, ez, 0, 1);
                            break;
                        case 1: /* west wall */
                            ex = piece->bb.minX - 1;
                            ez = piece->bb.minZ + jrand_int_bound(r, msbb_z_size(&piece->bb));
                            ms_try_add_next(start, r, &start->pieces[0], ex, room_y, ez, 1, 1);
                            break;
                        case 2: /* north wall */
                            ex = piece->bb.minX + jrand_int_bound(r, msbb_x_size(&piece->bb));
                            ez = piece->bb.minZ - 1;
                            ms_try_add_next(start, r, &start->pieces[0], ex, room_y, ez, 2, 1);
                            break;
                        case 3: /* east wall */
                            ex = piece->bb.maxX + 1;
                            ez = piece->bb.minZ + jrand_int_bound(r, msbb_z_size(&piece->bb));
                            ms_try_add_next(start, r, &start->pieces[0], ex, room_y, ez, 3, 1);
                            break;
                    }
                }
            }
            break;
        }
    }
}

/* ---- Structure Generation ---- */

MC_HD MC_NOINLINE static void ms_generate(MSStart *start, MSWorld *w, JavaRandom *r, int cx, int cz) {
    memset(start, 0, sizeof(*start));
    start->cx = cx;
    start->cz = cz;
    (void)w;

    int wx = (cx << 4) + 2;
    int wz = (cz << 4) + 2;

    /* Starting room */
    int room_w = 7 + jrand_int_bound(r, 6);
    int room_d = 7 + jrand_int_bound(r, 6);

    MSPiece *room = ms_add_piece(start);
    if (!room) return;
    room->type = MS_P_ROOM;
    room->bb = msbb_create(wx, 50, wz, wx + room_w - 1, 54, wz + room_d - 1);
    room->coord_base = 0;
    room->component_type = 0;

    /* Build piece tree */
    ms_build_component(start, room, r, room, 0);

    /* Compute total BB */
    start->total_bb = start->pieces[0].bb;
    for (int i = 1; i < start->piece_count; i++) {
        msbb_expand(&start->total_bb, &start->pieces[i].bb);
    }

    /* markAvailableHeight -- shift y to fit underground.
     * MC: clamp to y=10 minimum, random y offset */
    int max_y = start->total_bb.maxY - start->total_bb.minY;
    int target_y = jrand_int_bound(r, 40);
    if (target_y + max_y > 60) target_y = 60 - max_y;
    if (target_y < 10) target_y = 10;

    int y_offset = target_y - start->total_bb.minY;
    msbb_offset(&start->total_bb, 0, y_offset, 0);
    for (int i = 0; i < start->piece_count; i++) {
        msbb_offset(&start->pieces[i].bb, 0, y_offset, 0);
    }

    start->valid = 1;
}

/* ---- Block Placement ---- */

/* Check if any liquid overlaps the piece BB */
MC_HD MC_NOINLINE static int ms_is_liquid_in_bb(MSWorld *w, const MSBB *bb) {
    for (int x = bb->minX; x <= bb->maxX; x++) {
        for (int z = bb->minZ; z <= bb->maxZ; z++) {
            for (int y = bb->minY; y <= bb->maxY; y++) {
                int b = ms_get(w, x, y, z);
                if (b == MS_WATER || b == MS_FLOWING_WATER ||
                    b == MS_LAVA || b == MS_FLOWING_LAVA) {
                    return 1;
                }
            }
        }
    }
    return 0;
}

MC_HD MC_NOINLINE static void ms_place_room(MSWorld *w, MSPiece *p, const MSBB *clip) {
    if (ms_is_liquid_in_bb(w, &p->bb)) return;

    /* Dirt floor */
    for (int x = p->bb.minX; x <= p->bb.maxX; x++) {
        for (int z = p->bb.minZ; z <= p->bb.maxZ; z++) {
            int b = ms_get(w, x, p->bb.minY, z);
            if (b != MS_AIR && ms_is_solid(b)) {
                ms_set(w, x, p->bb.minY, z, MS_DIRT);
            }
        }
    }

    /* Clear interior air */
    for (int x = p->bb.minX; x <= p->bb.maxX; x++) {
        for (int z = p->bb.minZ; z <= p->bb.maxZ; z++) {
            int top = p->bb.minY + 3;
            if (top > p->bb.maxY) top = p->bb.maxY;
            for (int y = p->bb.minY + 1; y <= top; y++) {
                ms_set(w, x, y, z, MS_AIR);
            }
        }
    }
}

MC_HD MC_NOINLINE static void ms_place_corridor(MSWorld *w, MSPiece *p, JavaRandom *r, const MSBB *clip) {
    if (ms_is_liquid_in_bb(w, &p->bb)) return;

    int sections = p->section_count;
    int total_len = sections * 5 - 1;

    /* Clear main tunnel */
    ms_fill(w, p, clip, 0, 0, 0, 2, 1, total_len, MS_AIR, MS_AIR, 0);
    /* Random air above (80% chance per block) */
    ms_random_fill(w, p, clip, r, 0.8f, 0, 2, 0, 2, 2, total_len, MS_AIR, MS_AIR, 0);

    /* Cobwebs for spider corridors */
    if (p->has_spiders) {
        ms_random_fill(w, p, clip, r, 0.6f, 0, 0, 0, 2, 1, total_len, MS_WEB, MS_AIR, 0);
    }

    /* Support pillars every 5 blocks */
    for (int s = 0; s < sections; s++) {
        int sz = 2 + s * 5;

        /* Fence pillars */
        ms_fill(w, p, clip, 0, 0, sz, 0, 1, sz, MS_FENCE, MS_AIR, 0);
        ms_fill(w, p, clip, 2, 0, sz, 2, 1, sz, MS_FENCE, MS_AIR, 0);

        /* Plank ceiling */
        if (jrand_int_bound(r, 4) == 0) {
            ms_fill(w, p, clip, 0, 2, sz, 0, 2, sz, MS_PLANKS, MS_AIR, 0);
            ms_fill(w, p, clip, 2, 2, sz, 2, 2, sz, MS_PLANKS, MS_AIR, 0);
        } else {
            ms_fill(w, p, clip, 0, 2, sz, 2, 2, sz, MS_PLANKS, MS_AIR, 0);
        }

        /* Cobweb decoration around supports */
        ms_random_block(w, p, clip, r, 0.1f, 0, 2, sz - 1, MS_WEB, 0);
        ms_random_block(w, p, clip, r, 0.1f, 2, 2, sz - 1, MS_WEB, 0);
        ms_random_block(w, p, clip, r, 0.1f, 0, 2, sz + 1, MS_WEB, 0);
        ms_random_block(w, p, clip, r, 0.1f, 2, 2, sz + 1, MS_WEB, 0);
        ms_random_block(w, p, clip, r, 0.05f, 0, 2, sz - 2, MS_WEB, 0);
        ms_random_block(w, p, clip, r, 0.05f, 2, 2, sz - 2, MS_WEB, 0);
        ms_random_block(w, p, clip, r, 0.05f, 0, 2, sz + 2, MS_WEB, 0);
        ms_random_block(w, p, clip, r, 0.05f, 2, 2, sz + 2, MS_WEB, 0);

        /* Torches */
        ms_random_block(w, p, clip, r, 0.05f, 1, 2, sz - 1, MS_TORCH, 0);
        ms_random_block(w, p, clip, r, 0.05f, 1, 2, sz + 1, MS_TORCH, 0);

        /* Chests (1% chance per section, both sides) */
        if (jrand_int_bound(r, 100) == 0) {
            ms_place(w, p, clip, MS_CHEST, 0, 2, 0, sz - 1);
        }
        if (jrand_int_bound(r, 100) == 0) {
            ms_place(w, p, clip, MS_CHEST, 0, 0, 0, sz + 1);
        }

        /* Spider spawner */
        if (p->has_spiders && !p->spawner_placed) {
            int sy = ms_get_y(p, 0);
            int si = sz - 1 + jrand_int_bound(r, 3);
            int sx = ms_get_x(p, 1, si);
            int ssz = ms_get_z(p, 1, si);
            if (msbb_contains(clip, sx, sy, ssz)) {
                p->spawner_placed = 1;
                ms_set(w, sx, sy, ssz, MS_MOB_SPAWNER);
            }
        }
    }

    /* Plank floor under air gaps */
    for (int x = 0; x <= 2; x++) {
        for (int z = 0; z <= total_len; z++) {
            int b = ms_get_local(w, p, clip, x, -1, z);
            if (b == MS_AIR) {
                ms_place(w, p, clip, MS_PLANKS, 0, x, -1, z);
            }
        }
    }

    /* Rails */
    if (p->has_rails) {
        for (int z = 0; z <= total_len; z++) {
            int below = ms_get_local(w, p, clip, 1, -1, z);
            if (below != MS_AIR && ms_is_solid(below)) {
                ms_random_block(w, p, clip, r, 0.7f, 1, 0, z, MS_RAIL, 0);
            }
        }
    }
}

MC_HD MC_NOINLINE static void ms_place_cross(MSWorld *w, MSPiece *p, const MSBB *clip) {
    if (ms_is_liquid_in_bb(w, &p->bb)) return;

    if (p->is_multiple_floors) {
        /* Two-level crossing: clear air in + shape, both levels */
        /* Lower level */
        for (int x = p->bb.minX + 1; x < p->bb.maxX; x++) {
            for (int z = p->bb.minZ; z <= p->bb.maxZ; z++) {
                for (int y = p->bb.minY; y < p->bb.minY + 3; y++) {
                    ms_set(w, x, y, z, MS_AIR);
                }
            }
        }
        for (int x = p->bb.minX; x <= p->bb.maxX; x++) {
            for (int z = p->bb.minZ + 1; z < p->bb.maxZ; z++) {
                for (int y = p->bb.minY; y < p->bb.minY + 3; y++) {
                    ms_set(w, x, y, z, MS_AIR);
                }
            }
        }
        /* Upper level */
        for (int x = p->bb.minX + 1; x < p->bb.maxX; x++) {
            for (int z = p->bb.minZ; z <= p->bb.maxZ; z++) {
                for (int y = p->bb.maxY - 2; y <= p->bb.maxY; y++) {
                    ms_set(w, x, y, z, MS_AIR);
                }
            }
        }
        for (int x = p->bb.minX; x <= p->bb.maxX; x++) {
            for (int z = p->bb.minZ + 1; z < p->bb.maxZ; z++) {
                for (int y = p->bb.maxY - 2; y <= p->bb.maxY; y++) {
                    ms_set(w, x, y, z, MS_AIR);
                }
            }
        }
        /* Middle floor */
        for (int x = p->bb.minX + 1; x < p->bb.maxX; x++) {
            for (int z = p->bb.minZ + 1; z < p->bb.maxZ; z++) {
                ms_set(w, x, p->bb.minY + 3, z, MS_AIR);
            }
        }
    } else {
        /* Single level */
        for (int x = p->bb.minX + 1; x < p->bb.maxX; x++) {
            for (int z = p->bb.minZ; z <= p->bb.maxZ; z++) {
                for (int y = p->bb.minY; y <= p->bb.maxY; y++) {
                    ms_set(w, x, y, z, MS_AIR);
                }
            }
        }
        for (int x = p->bb.minX; x <= p->bb.maxX; x++) {
            for (int z = p->bb.minZ + 1; z < p->bb.maxZ; z++) {
                for (int y = p->bb.minY; y <= p->bb.maxY; y++) {
                    ms_set(w, x, y, z, MS_AIR);
                }
            }
        }
    }

    /* Corner pillars */
    for (int y = p->bb.minY; y <= p->bb.maxY; y++) {
        ms_set(w, p->bb.minX + 1, y, p->bb.minZ + 1, MS_PLANKS);
        ms_set(w, p->bb.minX + 1, y, p->bb.maxZ - 1, MS_PLANKS);
        ms_set(w, p->bb.maxX - 1, y, p->bb.minZ + 1, MS_PLANKS);
        ms_set(w, p->bb.maxX - 1, y, p->bb.maxZ - 1, MS_PLANKS);
    }

    /* Plank floor */
    for (int x = p->bb.minX; x <= p->bb.maxX; x++) {
        for (int z = p->bb.minZ; z <= p->bb.maxZ; z++) {
            int b = ms_get(w, x, p->bb.minY - 1, z);
            if (b == MS_AIR) {
                ms_set(w, x, p->bb.minY - 1, z, MS_PLANKS);
            }
        }
    }
}

MC_HD MC_NOINLINE static void ms_place_stairs(MSWorld *w, MSPiece *p, const MSBB *clip) {
    if (ms_is_liquid_in_bb(w, &p->bb)) return;

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

MC_HD MC_NOINLINE static void ms_place_blocks(MSWorld *w, MSStart *start) {
    MSBB clip = start->total_bb;
    clip.minX -= 16; clip.minZ -= 16;
    clip.maxX += 16; clip.maxZ += 16;
    clip.minY = 0; clip.maxY = 255;

    /* Need a per-structure RNG for corridor placement randomness */
    JavaRandom r;
    jrand_set(&r, (i64)start->cx * 341873128712LL + (i64)start->cz * 132897987541LL);

    for (int i = 0; i < start->piece_count; i++) {
        MSPiece *p = &start->pieces[i];
        switch (p->type) {
            case MS_P_ROOM:     ms_place_room(w, p, &clip); break;
            case MS_P_CORRIDOR: ms_place_corridor(w, p, &r, &clip); break;
            case MS_P_CROSS:    ms_place_cross(w, p, &clip); break;
            case MS_P_STAIRS:   ms_place_stairs(w, p, &clip); break;
        }
    }
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
    MSWorld dummy; dummy.primer = 0; dummy.chunkX = x; dummy.chunkZ = z; dummy.worldSeed = worldSeed; dummy.seaLevel = 63;
    int range = MS_RANGE;
    JavaRandom rand; jrand_set(&rand, worldSeed);
    i64 j = jrand_long(&rand), k = jrand_long(&rand);
    for (int l = x - range; l <= x + range; ++l)
        for (int i1 = z - range; i1 <= z + range; ++i1) {
            jrand_set(&rand, (i64)l * j ^ (i64)i1 * k ^ worldSeed);
            jrand_int(&rand);
            if (ms_try_spawn(g, &rand, l, i1) && g->count < MS_MAX_STARTS) {
                MSStart *s = &g->starts[g->count++];
                ms_generate(s, &dummy, &rand, l, i1);
            }
        }
}

MC_HD MC_NOINLINE static void ms_generate_structure(MSWorld *w,MSGen *g,int cx,int cz){
    MSBB clip={cx*16,0,cz*16,cx*16+15,255,cz*16+15};
    for(int i=0;i<g->count;++i) if(g->starts[i].valid && msbb_intersects(&g->starts[i].total_bb,&clip)) ms_place_all(w,&g->starts[i]);
}

#ifdef __CUDACC__
__device__ MSGen ms_cuda_gen;
#endif

MC_HD MC_NOINLINE static void ms_run(ChunkPrimer *primer,i64 seed,int cx,int cz){
    for(int i=0;i<65536;++i) primer->data[i]=(u16)MS_STONE;
    MSWorld w; w.primer=primer; w.chunkX=cx; w.chunkZ=cz; w.worldSeed=seed; w.seaLevel=63;
#ifdef __CUDA_ARCH__
    ms_generate_map(&ms_cuda_gen,seed,cx,cz);
    ms_generate_structure(&w,&ms_cuda_gen,cx,cz);
#else
    MSGen g; ms_generate_map(&g,seed,cx,cz);
    ms_generate_structure(&w,&g,cx,cz);
#endif
}
#endif
