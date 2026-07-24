#include "game/structures_live.h"

#include <stdlib.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#include "chunk_provider.h"
#include "map_gen_stronghold.h"
#include "map_gen_fortress.h"
#include "stronghold_loot.h"
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

/* Match C map_gen_stronghold chest placements (corridor local 3,1,3; library
 * local 7,1,7). Crossing pieces currently place no chest in the C port. */
static int piece_chest_match(const SHPiece *p, int x, int y, int z,
                             int *table_id)
{
    int wx, wy, wz;
    if (p->type == SH_CHEST_CORRIDOR) {
        wy = sh_get_y(p, 1);
        wx = sh_get_x(p, 3, 3);
        wz = sh_get_z(p, 3, 3);
        if (wx == x && wy == y && wz == z) {
            if (table_id) *table_id = SHL_CORRIDOR;
            return 1;
        }
    } else if (p->type == SH_P_LIBRARY) {
        wy = sh_get_y(p, 1);
        wx = sh_get_x(p, 7, 7);
        wz = sh_get_z(p, 7, 7);
        if (wx == x && wy == y && wz == z) {
            if (table_id) *table_id = SHL_LIBRARY;
            return 1;
        }
        if (p->is_large_room) {
            /* Java large library second chest at local 12,8,1 - C port does
             * not place it; keep the branch for future parity. */
            wy = sh_get_y(p, 8);
            wx = sh_get_x(p, 12, 1);
            wz = sh_get_z(p, 12, 1);
            if (wx == x && wy == y && wz == z) {
                if (table_id) *table_id = SHL_LIBRARY;
                return 1;
            }
        }
    } else if (p->type == SH_P_CROSSING) {
        /* Java places crossing chest at 3,4,8; C place_crossing omits it. */
        wy = sh_get_y(p, 4);
        wx = sh_get_x(p, 3, 8);
        wz = sh_get_z(p, 3, 8);
        if (wx == x && wy == y && wz == z) {
            if (table_id) *table_id = SHL_CROSSING;
            return 1;
        }
    }
    return 0;
}

int gm_stronghold_chest_info(long long seed, int x, int y, int z,
                             int *table_id, long long *loot_seed)
{
    int xs[128], zs[128], n = 0;
    sh_find_positions((i64)seed, xs, zs, &n);
    for (int i = 0; i < n; ++i) {
        SHStart *s = (SHStart *)malloc(sizeof *s);
        if (!s) return 0;
        sh_generate(s, (i64)seed, xs[i], zs[i]);
        if (!s->valid) { free(s); continue; }
        for (int p = 0; p < s->piece_count; ++p) {
            int tid = -1;
            if (piece_chest_match(&s->pieces[p], x, y, z, &tid)) {
                if (table_id) *table_id = tid;
                if (loot_seed)
                    *loot_seed = (long long)shl_pos_loot_seed((i64)seed, x, y, z);
                free(s);
                return 1;
            }
        }
        free(s);
    }
    return 0;
}
