#include "game/portal_live.h"

#include "nether_portal.h"
#include "end_portal.h"

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

int gm_portal_find_or_make(GmWorld *world, int near_x, int near_z,
                           double *out_x, double *out_y, double *out_z) {
    if(!world||!out_x||!out_y||!out_z)return 0;
    /* Teleporter.placeInExistingPortal scans the full +/-128-block square.
     * The old 16-block shortcut missed valid linked portals and created a
     * second one (seed-0 tape: existing x=8, scaled target x=25). */
    enum { PORTAL_SEARCH_RADIUS = 128 };
    gm_world_ensure(world,near_x>>4,near_z>>4,PORTAL_SEARCH_RADIUS/16+1);
    for(int r=0;r<=PORTAL_SEARCH_RADIUS;++r)
        for(int dx=-r;dx<=r;++dx)for(int dz=-r;dz<=r;++dz){
        if(r&&abs(dx)!=r&&abs(dz)!=r)continue;
        int x=near_x+dx,z=near_z+dz;
        for(int y=4;y<124;++y)if(gm_world_block(world,x,y,z)==90){
            *out_x=x+0.5;*out_y=y;*out_z=z+0.5;return 1;
        }
    }
    int bx=near_x-1,bz=near_z,by=-1;
    for(int r=0;r<=16&&by<0;++r)for(int dx=-r;dx<=r&&by<0;++dx)
        for(int dz=-r;dz<=r&&by<0;++dz){
            if(r&&abs(dx)!=r&&abs(dz)!=r)continue;
            int x=near_x+dx,z=near_z+dz;
            for(int y=118;y>=5;--y)if(gm_world_block(world,x,y,z)==0&&
                gm_world_block(world,x,y+1,z)==0&&gm_world_block(world,x,y-1,z)!=0&&
                gm_world_block(world,x,y-1,z)!=10&&gm_world_block(world,x,y-1,z)!=11){
                bx=x-1;bz=z;by=y;break;
            }
        }
    if(by<0)by=70;
    for(int x=bx;x<bx+4;++x){gm_world_set_block(world,x,by-1,bz,49);gm_world_set_block(world,x,by+3,bz,49);}
    for(int y=by;y<by+3;++y){gm_world_set_block(world,bx,y,bz,49);gm_world_set_block(world,bx+3,y,bz,49);}
    for(int x=bx+1;x<=bx+2;++x)for(int y=by;y<by+3;++y)
        gm_world_set_block_meta(world,x,y,bz,90,1);
    *out_x=bx+1.5;*out_y=by;*out_z=bz+0.5;return 1;
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
