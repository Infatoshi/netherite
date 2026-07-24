#include "game/structures_live.h"

#include <stdlib.h>
#include <string.h>

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

/* Look up table_id + loot_seed for a block that the real C placement stream
 * placed as a stronghold chest. Seed is the nextLong taken at that site after
 * all preceding stone-brick RNG in piece order — not a worldseed/xor/ordinal
 * helper, and not phantom sites for unplaced crossing/large-library chests. */
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
        sh_capture_chest_sites(s);
        for (int c = 0; c < s->n_chest_sites; ++c) {
            const SHChestSite *cs = &s->chest_sites[c];
            if (cs->x == x && cs->y == y && cs->z == z) {
                if (table_id) *table_id = cs->table_id;
                if (loot_seed) *loot_seed = (long long)cs->loot_seed;
                free(s);
                return 1;
            }
        }
        free(s);
    }
    return 0;
}

/* Oracle fixture helper: enumerate placement-stream chest sites for seed0 sh0. */
int gm_stronghold_chest_sites(long long seed, int index,
                              int *out_x, int *out_y, int *out_z,
                              int *out_table, long long *out_seed, int max_out)
{
    int cx, cz, n_out = 0;
    SHStart *s;
    if (max_out <= 0 || !locate_chunk(seed, index, &cx, &cz)) return 0;
    s = (SHStart *)malloc(sizeof *s);
    if (!s) return 0;
    sh_generate(s, (i64)seed, cx, cz);
    if (!s->valid) { free(s); return 0; }
    sh_capture_chest_sites(s);
    for (int c = 0; c < s->n_chest_sites && n_out < max_out; ++c) {
        if (out_x) out_x[n_out] = s->chest_sites[c].x;
        if (out_y) out_y[n_out] = s->chest_sites[c].y;
        if (out_z) out_z[n_out] = s->chest_sites[c].z;
        if (out_table) out_table[n_out] = s->chest_sites[c].table_id;
        if (out_seed) out_seed[n_out] = (long long)s->chest_sites[c].loot_seed;
        ++n_out;
    }
    free(s);
    return n_out;
}
