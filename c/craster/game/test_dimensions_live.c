#include "game/game.h"
#include "game/structures_live.h"

#include <stdio.h>

/* test hook (world_live.c): the render model key the mesher will draw. */
int gm_world__model_key(const GmWorld *w, int wx, int wy, int wz);

int main(void){
    GmStructureBox b;int fx,fz;
    if(!gm_fortress_locate(0,128,&fx,&fz)||!gm_fortress_spawner_room(0,128,&b)){
        fprintf(stderr,"dimensions_live: fortress locate failed\n");return 1;
    }
    GmWorld *n=gm_world_create_type(0,2);if(!n)return 1;
    for(int cx=b.min_x>>4;cx<=b.max_x>>4;++cx)for(int cz=b.min_z>>4;cz<=b.max_z>>4;++cz)
        gm_world_ensure(n,cx,cz,0);
    int spawners=0,bricks=0;
    for(int x=b.min_x;x<=b.max_x;++x)for(int y=b.min_y;y<=b.max_y;++y)
        for(int z=b.min_z;z<=b.max_z;++z){int id=gm_world_block(n,x,y,z);spawners+=id==52;bricks+=id==112;}
    /* Render-key bridge: generated netherrack must reach the mesher as the
     * netherrack model (210), never a raw-id passthrough key or fallback. */
    int rack_keys_ok=0,rack_seen=0;
    for(int x=b.min_x;x<=b.max_x&&rack_seen<64;++x)for(int y=0;y<128&&rack_seen<64;++y)
        for(int z=b.min_z;z<=b.max_z&&rack_seen<64;++z)
            if(gm_world_block(n,x,y,z)==87){
                ++rack_seen;
                rack_keys_ok+=gm_world__model_key(n,x,y,z)==210;
            }
    gm_world_destroy(n);
    GmWorld *e=gm_world_create_type(0,3);if(!e)return 1;gm_world_ensure(e,0,0,0);
    int endstone=0,end_keys_ok=0;for(int x=0;x<16;++x)for(int y=0;y<128;++y)for(int z=0;z<16;++z)
        if(gm_world_block(e,x,y,z)==121){
            ++endstone;
            end_keys_ok+=gm_world__model_key(e,x,y,z)==212;
        }
    gm_world_destroy(e);
    if(spawners<1||bricks<1||endstone<1||
       rack_seen<1||rack_keys_ok!=rack_seen||end_keys_ok!=endstone){
        fprintf(stderr,"dimensions_live: spawners=%d bricks=%d endstone=%d "
                "rack_keys=%d/%d end_keys=%d/%d\n",spawners,bricks,endstone,
                rack_keys_ok,rack_seen,end_keys_ok,endstone);return 1;
    }
    fprintf(stderr,"dimensions_live: PASS fortress=%d,%d spawners=%d endstone=%d\n",fx,fz,spawners,endstone);
    return 0;
}
