#include "game/portal_live.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc,char **argv){
    if(argc!=6)return 2;
    long long seed=strtoll(argv[1],NULL,10);
    double x=strtod(argv[2],NULL),y=strtod(argv[3],NULL),z=strtod(argv[4],NULL);
    uint64_t random_seed=strtoull(argv[5],NULL,10);
    GmWorld *world=gm_world_create_type(seed,1);
    if(!world)return 3;
    double out_x,out_y,out_z;
    if(!gm_portal_find_or_make(
            world,x,y,z,&random_seed,&out_x,&out_y,&out_z)){
        gm_world_destroy(world);return 4;
    }
    int cx=(int)floor(x),cz=(int)floor(z),first=1;
    printf("{\"ok\":true,\"random_seed48\":%llu,\"blocks\":[",
           (unsigned long long)random_seed);
    for(int dx=-20;dx<=20;++dx)for(int py=0;py<=20;++py)
        for(int dz=-20;dz<=20;++dz){
            int bx=cx+dx,bz=cz+dz,id=gm_world_block(world,bx,py,bz);
            if(id!=49&&id!=90)continue;
            int packed=(id<<4)|(gm_world_meta(world,bx,py,bz)&15);
            printf("%s[%d,%d,%d,%d]",first?"":",",dx,py,dz,packed);
            first=0;
        }
    puts("]}");
    gm_world_destroy(world);
    return 0;
}
