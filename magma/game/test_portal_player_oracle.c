#include "game/portal_live.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint64_t double_bits(double value){
    uint64_t bits;memcpy(&bits,&value,sizeof bits);return bits;
}

static uint32_t float_bits(float value){
    uint32_t bits;memcpy(&bits,&value,sizeof bits);return bits;
}

int main(int argc,char **argv){
    if(argc!=14)return 2;
    int portal_x=atoi(argv[1]),portal_y=atoi(argv[2]);
    int portal_z=atoi(argv[3]),meta=atoi(argv[4]);
    double vec_x=strtod(argv[5],NULL),vec_y=strtod(argv[6],NULL);
    int direction=atoi(argv[7]),obstruction=atoi(argv[8]);
    double vx=strtod(argv[9],NULL),vz=strtod(argv[10],NULL);
    float yaw=strtof(argv[11],NULL),pitch=strtof(argv[12],NULL);
    long long seed=strtoll(argv[13],NULL,10);
    GmWorld *world=gm_world_create_type(seed,1);
    if(!world)return 1;
    for(int pa=-1;pa<=2;++pa)for(int py=portal_y-1;py<=portal_y+3;++py){
        int frame=pa==-1||pa==2||py==portal_y-1||py==portal_y+3;
        gm_world_set_block_meta(
            world,meta==2?portal_x:portal_x+pa,py,
            meta==2?portal_z+pa:portal_z,
            frame?49:90,frame?0:meta);
    }
    if(obstruction){
        int offset=obstruction>0?1:-1;
        gm_world_set_block(world,
            meta==2?portal_x+offset:portal_x,portal_y,
            meta==1?portal_z+offset:portal_z,1);
    }
    double x=(double)portal_x+0.25,y=(double)portal_y;
    double z=(double)portal_z+0.25,vy=0.125;
    if(!gm_portal_place_player_existing(
            world,portal_x,portal_y,portal_z,vec_x,vec_y,direction,
            &x,&y,&z,&vx,&vz,&yaw)){
        gm_world_destroy(world);return 1;
    }
    printf("{\"ok\":true,\"portal_x\":%d,\"portal_y\":%d,"
           "\"portal_z\":%d,\"position_bits\":[\"%016" PRIx64
           "\",\"%016" PRIx64 "\",\"%016" PRIx64
           "\"],\"motion_bits\":[\"%016" PRIx64 "\",\"%016"
           PRIx64 "\",\"%016" PRIx64
           "\"],\"rotation_bits\":[\"%08" PRIx32
           "\",\"%08" PRIx32 "\"]}\n",
           portal_x,portal_y,portal_z,double_bits(x),double_bits(y),
           double_bits(z),double_bits(vx),double_bits(vy),double_bits(vz),
           float_bits(yaw),float_bits(pitch));
    gm_world_destroy(world);return 0;
}
