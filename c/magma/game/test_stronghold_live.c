#include "game/game.h"
#include "game/structures_live.h"

#include <stdio.h>

int main(void) {
    int x,z;GmStructureBox b;
    if(!gm_stronghold_locate(0,0,&x,&z)||!gm_stronghold_portal_room(0,0,&b)){
        fprintf(stderr,"stronghold_live: locate failed\n");return 1;
    }
    GmWorld *w=gm_world_create_type(0,0);
    if(!w)return 1;
    for(int cx=b.min_x>>4;cx<=b.max_x>>4;++cx)
        for(int cz=b.min_z>>4;cz<=b.max_z>>4;++cz)gm_world_ensure(w,cx,cz,0);
    int frames=0;
    for(int wx=b.min_x;wx<=b.max_x;++wx)for(int wy=b.min_y;wy<=b.max_y;++wy)
        for(int wz=b.min_z;wz<=b.max_z;++wz)if(gm_world_block(w,wx,wy,wz)==120)++frames;
    gm_world_destroy(w);
    if(frames!=12){fprintf(stderr,"stronghold_live: expected 12 frames, got %d at locate %d,%d\n",frames,x,z);return 1;}
    fprintf(stderr,"stronghold_live: PASS locate=%d,%d frames=%d\n",x,z,frames);
    return 0;
}
