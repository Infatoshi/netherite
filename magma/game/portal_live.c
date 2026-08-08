#include "game/portal_live.h"

#include "nether_portal.h"
#include "end_portal.h"
#include "game/block_normal_cube_1_11_2.h"

#include <math.h>
#include <string.h>
#include <stdlib.h>

static int gm_portal_empty(int id) {
    return id==NP_BLK_AIR||id==NP_BLK_FIRE||id==NP_BLK_PORTAL;
}

/* Match BlockPortal.Size.getDistanceUntilEdge on the resolved bottom
 * interior row. Returning the obsidian distance, rather than merely finding
 * a side block, keeps the staging origin faithful to the floor predicate. */
static int gm_portal_edge_distance(
        GmWorld *world,int x,int y,int z,int dx,int dz) {
    int distance;
    for(distance=0;distance<22;++distance){
        int cx=x+dx*distance,cz=z+dz*distance;
        int id=gm_world_block(world,cx,y,cz);
        if(!gm_portal_empty(id))return id==NP_BLK_OBSIDIAN?distance:0;
        if(gm_world_block(world,cx,y-1,cz)!=NP_BLK_OBSIDIAN)return 0;
    }
    return gm_world_block(world,x+dx*distance,y,z+dz*distance)==
        NP_BLK_OBSIDIAN?distance:0;
}

static int gm_portal_axis_origin(
        GmWorld *world,int x,int y,int z,int dx,int dz,int centered,
        int *valid) {
    int negative=gm_portal_edge_distance(world,x,y,z,-dx,-dz);
    int positive=gm_portal_edge_distance(world,x,y,z,dx,dz);
    int width=negative+positive-1;
    *valid=negative>=1&&positive>=1&&width>=2&&width<=21;
    if(*valid)
        return (dx?x:z)-negative;
    return centered;
}

int gm_portal_block_valid(
        GmWorld *world, int portal_x, int portal_y, int portal_z, int meta) {
    int axis_x;
    int left_dx, left_dz, right_dx, right_dz;
    int bottom_y, original_y;
    int edge, bottom_x, bottom_z, width;
    int height, portal_count = 0;
    if (!world || gm_world_block(
            world, portal_x, portal_y, portal_z) != NP_BLK_PORTAL)
        return 0;
    axis_x = (meta & 3) != 2;
    left_dx = axis_x ? 1 : 0;
    left_dz = axis_x ? 0 : -1;
    right_dx = -left_dx;
    right_dz = -left_dz;
    original_y = portal_y;
    bottom_y = portal_y;
    while (bottom_y > original_y - 21 && bottom_y > 0
            && gm_portal_empty(gm_world_block(
                world, portal_x, bottom_y - 1, portal_z)))
        --bottom_y;
    edge = gm_portal_edge_distance(
        world, portal_x, bottom_y, portal_z, left_dx, left_dz) - 1;
    if (edge < 0)
        return 0;
    bottom_x = portal_x + left_dx * edge;
    bottom_z = portal_z + left_dz * edge;
    width = gm_portal_edge_distance(
        world, bottom_x, bottom_y, bottom_z, right_dx, right_dz);
    if (width < 2 || width > 21)
        return 0;
    for (height = 0; height < 21; ++height) {
        int invalid = 0;
        for (int offset = 0; offset < width; ++offset) {
            int x = bottom_x + right_dx * offset;
            int z = bottom_z + right_dz * offset;
            int id = gm_world_block(world, x, bottom_y + height, z);
            if (!gm_portal_empty(id)) {
                invalid = 1;
                break;
            }
            if (id == NP_BLK_PORTAL)
                ++portal_count;
            if (offset == 0) {
                if (gm_world_block(
                        world, x + left_dx, bottom_y + height,
                        z + left_dz) != NP_BLK_OBSIDIAN) {
                    invalid = 1;
                    break;
                }
            } else if (offset == width - 1
                    && gm_world_block(
                        world, x + right_dx, bottom_y + height,
                        z + right_dz) != NP_BLK_OBSIDIAN) {
                invalid = 1;
                break;
            }
        }
        if (invalid)
            break;
    }
    for (int offset = 0; offset < width; ++offset)
        if (gm_world_block(
                world, bottom_x + right_dx * offset,
                bottom_y + height,
                bottom_z + right_dz * offset) != NP_BLK_OBSIDIAN)
            return 0;
    return height >= 3 && height <= 21
        && portal_count >= width * height;
}

int gm_portal_ignite(GmWorld *world, int fire_x, int fire_y, int fire_z) {
    if(!world)return 0;
    NpWorld local;memset(&local,0,sizeof local);
    int bottom_y=fire_y;
    while(bottom_y>0&&gm_portal_empty(
              gm_world_block(world,fire_x,bottom_y-1,fire_z)))
        --bottom_y;
    int centered_x=fire_x-NP_DIM/2,centered_z=fire_z-NP_DIM/2;
    int valid_x=0,valid_z=0;
    int ox=gm_portal_axis_origin(
        world,fire_x,bottom_y,fire_z,1,0,centered_x,&valid_x);
    int oz=gm_portal_axis_origin(
        world,fire_x,bottom_y,fire_z,0,1,centered_z,&valid_z);
    int aligned=valid_x||valid_z;
    int oy=aligned?bottom_y-1:fire_y-NP_DIM/2;
    for(int x=0;x<NP_DIM;++x)for(int y=0;y<NP_DIM;++y)for(int z=0;z<NP_DIM;++z)
        np_set(&local,x,y,z,mc_state(gm_world_block(world,ox+x,oy+y,oz+z),
                                     gm_world_meta(world,ox+x,oy+y,oz+z)));
    int lx=fire_x-ox,ly=fire_y-oy,lz=fire_z-oz;
    if(!np_try_spawn_portal(&local,lx,ly,lz))return 0;
    int placed=0;
    for(int x=0;x<NP_DIM;++x)for(int y=0;y<NP_DIM;++y)for(int z=0;z<NP_DIM;++z){
        u16 s=np_get(&local,x,y,z);
        if(mc_state_id(s)!=NP_BLK_PORTAL)continue;
        int wx=ox+x,wy=oy+y,wz=oz+z;
        if(gm_world_block(world,wx,wy,wz)!=NP_BLK_PORTAL){
            gm_world_set_block_meta(world,wx,wy,wz,NP_BLK_PORTAL,mc_state_meta(s));++placed;
        }
    }
    return placed;
}

int gm_portal_find_existing(
        GmWorld *world, int near_x, int near_y, int near_z,
        double *out_x, double *out_y, double *out_z) {
    double best_distance = -1.0;
    int best_x = 0, best_y = 0, best_z = 0;
    if(!world||!out_x||!out_y||!out_z)return 0;
    /* Teleporter.placeInExistingPortal walks X then Z from -128 through 128,
     * descends each column, reduces a portal column to its bottom cell, and
     * keeps the first strict minimum in three-dimensional BlockPos distance.
     * Ring-first search is observably wrong when the nearest horizontal pane
     * is far above or below the entity. */
    enum { PORTAL_SEARCH_RADIUS = 128 };
    int max_y = gm_world_dimension(world) == -1 ? 127 : 255;
    gm_world_ensure(world,near_x>>4,near_z>>4,PORTAL_SEARCH_RADIUS/16+1);
    for(int dx=-PORTAL_SEARCH_RADIUS;dx<=PORTAL_SEARCH_RADIUS;++dx)
        for(int dz=-PORTAL_SEARCH_RADIUS;dz<=PORTAL_SEARCH_RADIUS;++dz){
        int x=near_x+dx,z=near_z+dz;
        for(int y=max_y;y>=0;--y)if(gm_world_block(world,x,y,z)==90){
            while(y>0&&gm_world_block(world,x,y-1,z)==90)--y;
            double ddx=(double)(x-near_x),ddy=(double)(y-near_y);
            double ddz=(double)(z-near_z);
            double distance=ddx*ddx+ddy*ddy+ddz*ddz;
            if(best_distance<0.0||distance<best_distance){
                best_distance=distance;best_x=x;best_y=y;best_z=z;
            }
        }
    }
    if(best_distance<0.0)return 0;
    *out_x=best_x+0.5;*out_y=best_y;*out_z=best_z+0.5;
    return 1;
}

static int gm_portal_air(GmWorld *world,int x,int y,int z){
    return gm_world_block(world,x,y,z)==0;
}

static int gm_portal_solid(GmWorld *world,int x,int y,int z){
    int id=gm_world_block(world,x,y,z);
    return gm_block_material_is_solid_1_11_2(
        id,gm_world_meta(world,x,y,z));
}

static int gm_portal_make_exact(
        GmWorld *world,double entity_x,double entity_y,double entity_z,
        int orientation){
    double best=-1.0;
    int origin_x=(int)floor(entity_x),origin_y=(int)floor(entity_y);
    int origin_z=(int)floor(entity_z);
    int best_x=origin_x,best_y=origin_y,best_z=origin_z,best_orientation=0;
    int actual_height = gm_world_dimension(world) == -1 ? 128 : 256;
    int max_y = actual_height - 1;
    gm_world_ensure(world,origin_x>>4,origin_z>>4,3);
    for(int x=origin_x-16;x<=origin_x+16;++x){
        double dx=(double)x+0.5-entity_x;
        for(int z=origin_z-16;z<=origin_z+16;++z){
            double dz=(double)z+0.5-entity_z;
            for(int y=max_y;y>=0;--y){
                if(!gm_portal_air(world,x,y,z))continue;
                while(y>0&&gm_portal_air(world,x,y-1,z))--y;
                int accepted=0;
                for(int turn=orientation;turn<orientation+4;++turn){
                    int axis=turn%2,depth=1-axis;
                    if(turn%4>=2){axis=-axis;depth=-depth;}
                    int valid=1;
                    for(int across=0;across<3&&valid;++across)
                        for(int width=0;width<4&&valid;++width)
                            for(int height=-1;height<4;++height){
                                int px=x+(width-1)*axis+across*depth;
                                int py=y+height;
                                int pz=z+(width-1)*depth-across*axis;
                                if((height<0&&!gm_portal_solid(
                                            world,px,py,pz))
                                        ||(height>=0&&!gm_portal_air(
                                            world,px,py,pz))){
                                    valid=0;break;
                                }
                            }
                    if(!valid)continue;
                    double dy=(double)y+0.5-entity_y;
                    double distance=dx*dx+dy*dy+dz*dz;
                    if(best<0.0||distance<best){
                        best=distance;best_x=x;best_y=y;best_z=z;
                        best_orientation=turn%4;
                    }
                    accepted=1;
                }
                /* Java's labeled continue advances to the next horizontal
                 * column after evaluating the bottom of this air shaft. */
                if(accepted)break;
            }
        }
    }
    if(best<0.0){
        for(int x=origin_x-16;x<=origin_x+16;++x){
            double dx=(double)x+0.5-entity_x;
            for(int z=origin_z-16;z<=origin_z+16;++z){
                double dz=(double)z+0.5-entity_z;
                for(int y=max_y;y>=0;--y){
                    if(!gm_portal_air(world,x,y,z))continue;
                    while(y>0&&gm_portal_air(world,x,y-1,z))--y;
                    int accepted=0;
                    for(int turn=orientation;turn<orientation+2;++turn){
                        int axis=turn%2,depth=1-axis,valid=1;
                        for(int width=0;width<4&&valid;++width)
                            for(int height=-1;height<4;++height){
                                int px=x+(width-1)*axis;
                                int py=y+height;
                                int pz=z+(width-1)*depth;
                                if((height<0&&!gm_portal_solid(
                                            world,px,py,pz))
                                        ||(height>=0&&!gm_portal_air(
                                            world,px,py,pz))){
                                    valid=0;break;
                                }
                            }
                        if(!valid)continue;
                        double dy=(double)y+0.5-entity_y;
                        double distance=dx*dx+dy*dy+dz*dz;
                        if(best<0.0||distance<best){
                            best=distance;best_x=x;best_y=y;best_z=z;
                            best_orientation=turn%4;
                        }
                        accepted=1;
                    }
                    if(accepted)break;
                }
            }
        }
    }
    int frame_x=best_x,frame_y=best_y,frame_z=best_z;
    int axis=best_orientation%2,depth=1-axis;
    if(best_orientation%4>=2){axis=-axis;depth=-depth;}
    if(best<0.0){
        if(frame_y<70)frame_y=70;
        if(frame_y>actual_height-10)frame_y=actual_height-10;
        for(int side=-1;side<=1;++side)
            for(int across=1;across<3;++across)
                for(int height=-1;height<3;++height){
                    int x=frame_x+(across-1)*axis+side*depth;
                    int y=frame_y+height;
                    int z=frame_z+(across-1)*depth-side*axis;
                    gm_world_set_block(world,x,y,z,height<0?49:0);
                }
    }
    int portal_meta=axis==0?2:1;
    for(int pass=0;pass<4;++pass){
        for(int width=0;width<4;++width)
            for(int height=-1;height<4;++height){
                int x=frame_x+(width-1)*axis;
                int y=frame_y+height;
                int z=frame_z+(width-1)*depth;
                int frame=width==0||width==3||height==-1||height==3;
                gm_world_set_block_meta(
                    world,x,y,z,frame?49:90,frame?0:portal_meta);
            }
        /* Native neighbor propagation is driven by the block edit path. The
         * four Java notification passes intentionally leave the same volume. */
    }
    return 1;
}

int gm_portal_find_or_make(
        GmWorld *world,double entity_x,double entity_y,double entity_z,
        uint64_t *random_seed48,
        double *out_x,double *out_y,double *out_z){
    int near_x=(int)floor(entity_x),near_y=(int)floor(entity_y);
    int near_z=(int)floor(entity_z);
    if(!world||!random_seed48||*random_seed48>=UINT64_C(1)<<48
            ||!out_x||!out_y||!out_z)return 0;
    if(gm_portal_find_existing(
            world,near_x,near_y,near_z,out_x,out_y,out_z))return 1;
    JavaRandom random={*random_seed48};
    int orientation=jrand_int_bound(&random,4);
    *random_seed48=random.seed;
    if(!gm_portal_make_exact(
            world,entity_x,entity_y,entity_z,orientation))return 0;
    return gm_portal_find_existing(
        world,near_x,near_y,near_z,out_x,out_y,out_z);
}

int gm_portal_find_or_make_cached(
        GmWorld *world,GmPortalCache *cache,long long total_time,
        double entity_x,double entity_y,double entity_z,
        uint64_t *random_seed48,
        double *out_x,double *out_y,double *out_z){
    int key_x=(int)floor(entity_x),key_z=(int)floor(entity_z);
    if(!world||!cache||total_time<0||!random_seed48
            ||!out_x||!out_y||!out_z)return 0;
    for(int index=0;index<cache->count;++index){
        GmPortalCacheEntry *entry=&cache->entries[index];
        if(entry->key_x!=key_x||entry->key_z!=key_z)continue;
        if(gm_world_block(world,entry->portal_x,entry->portal_y,
                          entry->portal_z)!=NP_BLK_PORTAL){
            memmove(entry,entry+1,
                    (size_t)(cache->count-index-1)*sizeof *entry);
            --cache->count;
            break;
        }
        entry->last_update_time=total_time;
        *out_x=(double)entry->portal_x+0.5;
        *out_y=(double)entry->portal_y;
        *out_z=(double)entry->portal_z+0.5;
        return 1;
    }
    if(!gm_portal_find_or_make(
            world,entity_x,entity_y,entity_z,random_seed48,
            out_x,out_y,out_z))return 0;
    if(cache->count==cache->cap){
        int cap=cache->cap?cache->cap*2:16;
        if(cap<cache->cap||cap>1048576)return 0;
        int old_cap=cache->cap;
        GmPortalCacheEntry *entries=(GmPortalCacheEntry *)realloc(
            cache->entries,(size_t)cap*sizeof *entries);
        if(!entries)return 0;
        memset(entries+old_cap,0,
            (size_t)(cap-old_cap)*sizeof *entries);
        cache->entries=entries;cache->cap=cap;
    }
    cache->entries[cache->count++]=(GmPortalCacheEntry){
        key_x,key_z,(int)floor(*out_x),(int)floor(*out_y),
        (int)floor(*out_z),total_time};
    return 1;
}

void gm_portal_cache_prune(GmPortalCache *cache,long long total_time){
    if(!cache||total_time<0||total_time%100!=0)return;
    long long cutoff=total_time-300;
    int write=0;
    for(int read=0;read<cache->count;++read){
        if(cache->entries[read].last_update_time<cutoff)continue;
        if(write!=read)cache->entries[write]=cache->entries[read];
        ++write;
    }
    cache->count=write;
}

void gm_portal_cache_clear(GmPortalCache *cache){
    if(!cache)return;
    free(cache->entries);
    memset(cache,0,sizeof *cache);
}

double gm_portal_transfer_coordinate(double coordinate,double scale){
    double scaled=coordinate*scale;
    if(scaled>29999872.0)return 29999872.0;
    if(scaled<-29999872.0)return -29999872.0;
    /* Java's double-to-int narrowing truncates toward zero before the
     * Teleporter receives the entity. */
    return (double)(int)scaled;
}

typedef struct {
    int axis_z;
    int min_axis, max_axis, bottom_y, top_y;
    int front_x, front_y, front_z, forwards;
    int width, height;
} GmPortalPattern;

static int gm_portal_pattern(
        GmWorld *world, int x, int y, int z, GmPortalPattern *out) {
    int meta, min_axis, max_axis, bottom_y, top_y;
    int positive_count = 0, negative_count = 0;
    if (!world || !out || gm_world_block(world, x, y, z) != NP_BLK_PORTAL)
        return 0;
    meta = gm_world_meta(world, x, y, z) & 3;
    bottom_y = y;
    while (bottom_y > 0
            && gm_world_block(world, x, bottom_y - 1, z)
                == NP_BLK_PORTAL)
        --bottom_y;
    top_y = y;
    while (top_y < 255
            && gm_world_block(world, x, top_y + 1, z)
                == NP_BLK_PORTAL)
        ++top_y;
    if (meta == 2) {
        min_axis = max_axis = z;
        while (gm_world_block(world, x, bottom_y, min_axis - 1)
                == NP_BLK_PORTAL)
            --min_axis;
        while (gm_world_block(world, x, bottom_y, max_axis + 1)
                == NP_BLK_PORTAL)
            ++max_axis;
        for (int py = bottom_y; py <= top_y; ++py)
            for (int pz = min_axis; pz <= max_axis; ++pz) {
                positive_count += gm_world_block(world, x + 1, py, pz) != 0;
                negative_count += gm_world_block(world, x - 1, py, pz) != 0;
            }
        out->axis_z = 1;
        out->forwards = negative_count < positive_count ? 1 : 3;
        out->front_x = x;
        out->front_z = out->forwards == 3 ? min_axis : max_axis;
    } else {
        min_axis = max_axis = x;
        while (gm_world_block(world, min_axis - 1, bottom_y, z)
                == NP_BLK_PORTAL)
            --min_axis;
        while (gm_world_block(world, max_axis + 1, bottom_y, z)
                == NP_BLK_PORTAL)
            ++max_axis;
        for (int py = bottom_y; py <= top_y; ++py)
            for (int px = min_axis; px <= max_axis; ++px) {
                positive_count += gm_world_block(world, px, py, z + 1) != 0;
                negative_count += gm_world_block(world, px, py, z - 1) != 0;
            }
        out->axis_z = 0;
        out->forwards = negative_count < positive_count ? 2 : 0;
        out->front_x = out->forwards == 0 ? max_axis : min_axis;
        out->front_z = z;
    }
    out->min_axis = min_axis;
    out->max_axis = max_axis;
    out->bottom_y = bottom_y;
    out->top_y = top_y;
    out->front_y = top_y;
    out->width = max_axis - min_axis + 1;
    out->height = top_y - bottom_y + 1;
    return out->width >= 1 && out->height >= 1;
}

static int gm_portal_rotate_y(int direction) {
    return (direction + 1) & 3;
}

static int gm_portal_opposite(int direction) {
    return (direction + 2) & 3;
}

static int gm_portal_axis_direction(int direction) {
    return direction == 0 || direction == 3 ? 1 : -1;
}

int gm_portal_capture_entry(
        GmWorld *world, int portal_x, int portal_y, int portal_z,
        double entity_x, double entity_y, double entity_z,
        double *out_vec_x, double *out_vec_y, int *out_direction) {
    GmPortalPattern pattern;
    double d0, d1;
    int side_offset;
    if (!out_vec_x || !out_vec_y || !out_direction
            || !gm_portal_pattern(
                world, portal_x, portal_y, portal_z, &pattern))
        return 0;
    d0 = pattern.forwards == 1 || pattern.forwards == 3
        ? (double)pattern.front_z : (double)pattern.front_x;
    d1 = pattern.forwards == 1 || pattern.forwards == 3
        ? entity_z : entity_x;
    side_offset = gm_portal_axis_direction(
        gm_portal_rotate_y(pattern.forwards)) < 0;
    *out_vec_x = fabs(
        (d1 - (double)side_offset - d0) / -(double)pattern.width);
    *out_vec_y = ((entity_y - 1.0) - (double)pattern.front_y)
        / -(double)pattern.height;
    *out_direction = pattern.forwards;
    return 1;
}

static int gm_portal_place_existing_kind(
        GmWorld *world, int portal_x, int portal_y, int portal_z,
        double portal_vec_x, double portal_vec_y, int teleport_direction,
        double *io_x, double *io_y, double *io_z,
        double *io_vx, double *io_vz, float *io_yaw, int player) {
    GmPortalPattern pattern;
    double d2, d3, d4, placed_x, placed_y, placed_z;
    double f = 0.0, f1 = 0.0, f2 = 0.0, f3 = 0.0;
    int destination_opposite;
    if (!io_x || !io_y || !io_z || !io_vx || !io_vz || !io_yaw
            || teleport_direction < 0 || teleport_direction > 3
            || !isfinite(portal_vec_x) || !isfinite(portal_vec_y)
            || !gm_portal_pattern(
                world, portal_x, portal_y, portal_z, &pattern))
        return 0;
    placed_x = (double)portal_x + 0.5;
    placed_z = (double)portal_z + 0.5;
    d2 = pattern.forwards == 1 || pattern.forwards == 3
        ? (double)pattern.front_z : (double)pattern.front_x;
    placed_y = (double)(pattern.front_y + 1)
        - portal_vec_y * (double)pattern.height;
    if (gm_portal_axis_direction(gm_portal_rotate_y(pattern.forwards)) < 0)
        d2 += 1.0;
    if (pattern.forwards == 1 || pattern.forwards == 3)
        placed_z = d2 + (1.0 - portal_vec_x) * (double)pattern.width
            * (double)gm_portal_axis_direction(
                gm_portal_rotate_y(pattern.forwards));
    else
        placed_x = d2 + (1.0 - portal_vec_x) * (double)pattern.width
            * (double)gm_portal_axis_direction(
                gm_portal_rotate_y(pattern.forwards));
    destination_opposite = gm_portal_opposite(pattern.forwards);
    if (destination_opposite == teleport_direction) {
        f = 1.0; f1 = 1.0;
    } else if (destination_opposite
            == gm_portal_opposite(teleport_direction)) {
        f = -1.0; f1 = -1.0;
    } else if (destination_opposite
            == gm_portal_rotate_y(teleport_direction)) {
        f2 = 1.0; f3 = -1.0;
    } else {
        f2 = -1.0; f3 = 1.0;
    }
    d3 = *io_vx;
    d4 = *io_vz;
    *io_vx = d3 * f + d4 * f3;
    *io_vz = d3 * f2 + d4 * f1;
    *io_yaw = *io_yaw
        - (float)(gm_portal_opposite(teleport_direction) * 90)
        + (float)(pattern.forwards * 90);
    if (player) {
        /* PlayerList transfers the same EntityPlayerMP and the connection
         * installs Teleporter's exact doubles. */
        *io_x = placed_x;
        *io_y = placed_y;
        *io_z = placed_z;
    } else {
        /* Entity.changeDimension creates a replacement at BlockPos(old),
         * deliberately quantizing ordinary entities to block centers. */
        *io_x = floor(placed_x) + 0.5;
        *io_y = floor(placed_y);
        *io_z = floor(placed_z) + 0.5;
    }
    /* Both PlayerList transfer and Entity.changeDimension ultimately call
     * Entity.setRotation through setLocationAndAngles. */
    *io_yaw = fmodf(*io_yaw, 360.0F);
    return 1;
}

int gm_portal_place_existing(
        GmWorld *world, int portal_x, int portal_y, int portal_z,
        double portal_vec_x, double portal_vec_y, int teleport_direction,
        double *io_x, double *io_y, double *io_z,
        double *io_vx, double *io_vz, float *io_yaw) {
    return gm_portal_place_existing_kind(
        world, portal_x, portal_y, portal_z,
        portal_vec_x, portal_vec_y, teleport_direction,
        io_x, io_y, io_z, io_vx, io_vz, io_yaw, 0);
}

int gm_portal_place_player_existing(
        GmWorld *world, int portal_x, int portal_y, int portal_z,
        double portal_vec_x, double portal_vec_y, int teleport_direction,
        double *io_x, double *io_y, double *io_z,
        double *io_vx, double *io_vz, float *io_yaw) {
    return gm_portal_place_existing_kind(
        world, portal_x, portal_y, portal_z,
        portal_vec_x, portal_vec_y, teleport_direction,
        io_x, io_y, io_z, io_vx, io_vz, io_yaw, 1);
}

int gm_end_portal_insert_eye(GmWorld *world, int frame_x, int frame_y, int frame_z) {
    if(!world)return 0;
    EpWorld local;memset(&local,0,sizeof local);local.eyes_left=EP_NFRAMES;
    int minx=frame_x,maxx=frame_x,minz=frame_z,maxz=frame_z;
    for(int x=frame_x-5;x<=frame_x+5;++x)for(int z=frame_z-5;z<=frame_z+5;++z)
        if(gm_world_block(world,x,frame_y,z)==120){
            if(x<minx)minx=x;
            if(x>maxx)maxx=x;
            if(z<minz)minz=z;
            if(z>maxz)maxz=z;
        }
    int ox=(minx+maxx)/2-EP_W/2,oy=frame_y-1,oz=(minz+maxz)/2-EP_D/2;
    for(int x=0;x<EP_W;++x)for(int y=0;y<EP_H;++y)for(int z=0;z<EP_D;++z){
        int wx=ox+x,wz=oz+z,id=gm_world_block(world,wx,oy+y,wz);
        int meta=gm_world_meta(world,wx,oy+y,wz);
        /* The stronghold primer is id-only. Recover BlockEndPortalFrame facing
         * from the generated 5x5 ring before the verified activation matcher
         * sees it; preserve an already inserted eye bit. */
        if(id==120){int eye=meta&4,face=meta&3;
            if(wz==minz)face=EP_FACE_NORTH;else if(wz==maxz)face=EP_FACE_SOUTH;
            else if(wx==minx)face=EP_FACE_WEST;else if(wx==maxx)face=EP_FACE_EAST;
            meta=eye|face;
        }
        ep_set(&local,x,y,z,mc_state(id,meta));
    }
    int result=ep_insert_eye(&local,frame_x-ox,frame_y-oy,frame_z-oz);
    if(!result)return 0;
    for(int x=0;x<EP_W;++x)for(int y=0;y<EP_H;++y)for(int z=0;z<EP_D;++z){
        u16 s=ep_get(&local,x,y,z);int id=mc_state_id(s),meta=mc_state_meta(s);
        if((id==120||id==119)&&(gm_world_block(world,ox+x,oy+y,oz+z)!=id||
            gm_world_meta(world,ox+x,oy+y,oz+z)!=meta))
            gm_world_set_block_meta(world,ox+x,oy+y,oz+z,id,meta);
    }
    return result;
}
