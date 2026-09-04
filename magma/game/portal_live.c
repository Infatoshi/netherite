#include "game/portal_live.h"

#include "nether_portal.h"
#include "end_portal.h"

#include <string.h>
#include <stdlib.h>

#define PORTAL_WORLD GmWorld
#define PORTAL_BLOCK(w,x,y,z) gm_world_block((w),(x),(y),(z))
#include "portal_arrival.h"
#undef PORTAL_BLOCK
#undef PORTAL_WORLD

int gm_portal_ignite(GmWorld *world, int fire_x, int fire_y, int fire_z) {
    if(!world)return 0;
    NpWorld local;memset(&local,0,sizeof local);
    int ox=fire_x-NP_DIM/2,oy=fire_y-NP_DIM/2,oz=fire_z-NP_DIM/2;
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
    PortalArrival p;
    gm_world_ensure(world,near_x>>4,near_z>>4,128/16+1);
    if (portal_plan_arrival(world, near_x, near_z, &p) != 1) return 0;
    if (p.create) {
        for (int x=p.bx;x<p.bx+4;++x) {
            gm_world_set_block(world,x,p.by-1,p.bz,49);
            gm_world_set_block(world,x,p.by+3,p.bz,49);
        }
        for (int y=p.by;y<p.by+3;++y) {
            gm_world_set_block(world,p.bx,y,p.bz,49);
            gm_world_set_block(world,p.bx+3,y,p.bz,49);
            for (int x=p.bx+1;x<=p.bx+2;++x)
                gm_world_set_block_meta(world,x,y,p.bz,90,1);
        }
    }
    *out_x=p.x; *out_y=p.y; *out_z=p.z;
    return 1;
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
