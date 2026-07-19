#include "game/structures_live.h"

#include <stdlib.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "chunk_provider.h"
#include "map_gen_stronghold.h"
#include "map_gen_fortress.h"
#pragma GCC diagnostic pop

static int locate_chunk(long long seed, int index, int *cx, int *cz) {
    int xs[128],zs[128],n=0;
    sh_find_positions((i64)seed,xs,zs,&n);
    if(index<0||index>=n)return 0;
    *cx=xs[index];*cz=zs[index];return 1;
}

int gm_stronghold_locate(long long seed, int index, int *block_x, int *block_z) {
    int cx,cz;
    if(!block_x||!block_z||!locate_chunk(seed,index,&cx,&cz))return 0;
    *block_x=cx*16+8;*block_z=cz*16+8;return 1;
}

int gm_stronghold_portal_room(long long seed, int index, GmStructureBox *box) {
    int cx,cz;
    if(!box||!locate_chunk(seed,index,&cx,&cz))return 0;
    SHStart *s=(SHStart *)malloc(sizeof *s);
    if(!s)return 0;
    sh_generate(s,(i64)seed,cx,cz);
    if(!s->valid||s->portal_room_idx<0){free(s);return 0;}
    SHBB b=s->pieces[s->portal_room_idx].bb;
    box->min_x=b.minX;box->min_y=b.minY;box->min_z=b.minZ;
    box->max_x=b.maxX;box->max_y=b.maxY;box->max_z=b.maxZ;
    free(s);return 1;
}

static int fortress_chunk(long long seed,int radius,int *cx,int *cz){
    for(int r=0;r<=radius;++r)for(int x=-r;x<=r;++x)for(int z=-r;z<=r;++z){
        if(r&&abs(x)!=r&&abs(z)!=r)continue;
        if(ft_can_spawn((i64)seed,x,z)){*cx=x;*cz=z;return 1;}
    }return 0;
}

int gm_fortress_locate(long long seed,int radius,int *block_x,int *block_z){
    int cx,cz;if(!block_x||!block_z||!fortress_chunk(seed,radius,&cx,&cz))return 0;
    *block_x=cx*16+8;*block_z=cz*16+8;return 1;
}

int gm_fortress_spawner_room(long long seed,int radius,GmStructureBox *box){
    int cx,cz;if(!box||!fortress_chunk(seed,radius,&cx,&cz))return 0;
    FtStart *s=(FtStart *)malloc(sizeof *s);if(!s)return 0;
    ft_generate(s,(i64)seed,cx,cz);
    for(int i=0;i<s->piece_count;++i)if(s->pieces[i].type==FT_P_THRONE){
        FtBB b=s->pieces[i].bb;box->min_x=b.minX;box->min_y=b.minY;box->min_z=b.minZ;
        box->max_x=b.maxX;box->max_y=b.maxY;box->max_z=b.maxZ;free(s);return 1;
    }
    free(s);return 0;
}
