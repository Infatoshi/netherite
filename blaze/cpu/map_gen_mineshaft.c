#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../core/genlayer_biomes.h"
#include "../core/map_gen_mineshaft.h"

static GlArena *g_biome_arena;
static int g_forced_type;
static int cpu_forced_type_at(i64 seed,int x,int z){
    (void)seed; (void)x; (void)z; return g_forced_type;
}
static int cpu_mineshaft_type_at(i64 seed,int x,int z){
    GLNode nodes[GL_MAX_NODES]; int voronoi;
    if(!g_biome_arena) g_biome_arena=(GlArena *)malloc(sizeof(*g_biome_arena));
    if(!g_biome_arena) return MS_TYPE_NORMAL;
    gl_build(nodes,seed,&voronoi); g_biome_arena->off=0;
    int biome=gl_getInts(nodes,g_biome_arena,voronoi,x,z,1,1)[0];
    return biome==B_MESA||biome==B_MESA_ROCK||biome==B_MESA_CLEAR_ROCK||
           biome==165||biome==166||biome==167 ? MS_TYPE_MESA:MS_TYPE_NORMAL;
}

static const char *piece_kind(int type) {
    switch (type) {
        case MS_P_ROOM: return "room";
        case MS_P_CORRIDOR: return "corridor";
        case MS_P_CROSS: return "cross";
        case MS_P_STAIRS: return "stairs";
        default: return "unknown";
    }
}

static void print_box(const MSBB *b) {
    printf("[%d,%d,%d,%d,%d,%d]", b->minX,b->minY,b->minZ,
           b->maxX,b->maxY,b->maxZ);
}

static void print_topology(i64 seed, int cx, int cz) {
    MSGen *g = (MSGen *)malloc(sizeof(*g));
    ms_generate_map(g, seed, cx, cz);
    printf("{\"seed\":%lld,\"starts\":[", (long long)seed);
    for (int s=0; s<g->count; ++s) {
        MSStart *st=&g->starts[s];
        JavaRandom probe;
        jrand_set_seed48(&probe,st->rng_seed_after);
        if (s) putchar(',');
        printf("{\"cx\":%d,\"cz\":%d,\"seed48_before\":%llu,\"box\":",
               st->cx,st->cz,(unsigned long long)st->rng_seed_before);
        print_box(&st->total_bb);
        printf(",\"rng_probe\":%lld,\"pieces\":[",(long long)jrand_long(&probe));
        for (int i=0; i<st->piece_count; ++i) {
            MSPiece *p=&st->pieces[i];
            if (i) putchar(',');
            printf("{\"index\":%d,\"component\":%d,\"box\":",i,p->component_type);
            print_box(&p->bb);
            printf(",\"facing\":%d,\"kind\":\"%s\"",p->coord_base,piece_kind(p->type));
            if (p->type==MS_P_ROOM) {
                printf(",\"entrances\":[");
                for (int e=0;e<st->entrance_count;++e) {
                    if(e)putchar(','); print_box(&st->entrances[e]);
                }
                putchar(']');
            } else if (p->type==MS_P_CORRIDOR) {
                printf(",\"rails\":%s,\"spiders\":%s,\"spawner\":%s,\"sections\":%d",
                    p->has_rails?"true":"false",p->has_spiders?"true":"false",
                    p->spawner_placed?"true":"false",p->section_count);
            } else if (p->type==MS_P_CROSS) {
                printf(",\"multiple\":%s",p->is_multiple_floors?"true":"false");
            }
            putchar('}');
        }
        printf("]}");
    }
    printf("]}\n");
    free(g);
}

static int print_placement(i64 seed,int cx,int cz,int selected,i64 placement_seed,
                           int clip_dx,int clip_dz,int emit) {
    MSGen *g=(MSGen *)malloc(sizeof(*g));
    ChunkPrimer *primer=(ChunkPrimer *)malloc(sizeof(*primer));
    if(!g||!primer) return 2;
    ms_generate_map(g,seed,cx,cz);
    if(selected<0||selected>=g->count) return 3;
    MSStart *start=&g->starts[selected];
    int dx=1024-start->total_bb.minX, dz=1024-start->total_bb.minZ;
    msbb_offset(&start->total_bb,dx,0,dz);
    for(int i=0;i<start->piece_count;i++) msbb_offset(&start->pieces[i].bb,dx,0,dz);
    for(int i=0;i<start->entrance_count;i++) msbb_offset(&start->entrances[i],dx,0,dz);
    for(int i=0;i<65536;i++) primer->data[i]=(u16)(MS_STONE<<4);
    int base_x=1024+clip_dx*16, base_z=1024+clip_dz*16;
    MSWorld w={primer,base_x/16,base_z/16,seed,63,1};
    MSBB clip={base_x,0,base_z,base_x+15,63,base_z+15};
    JavaRandom placement; jrand_set(&placement,placement_seed);
    ms_place_blocks_clip(&w,start,&placement,&clip);
    int carts=0,spawners=0;
    for(int i=0;i<start->piece_count;i++){
        carts+=start->pieces[i].chest_placed;
        spawners+=start->pieces[i].spawner_placed;
    }
    fprintf(stderr,"pieces=%d carts=%d spawners=%d rng_seed48_after=%llu\n",
            start->piece_count,carts,spawners,(unsigned long long)placement.seed);
    for(int i=0;i<start->cart_count;i++)
        fprintf(stderr,"cart=%d,%d,%d,%lld\n",start->carts[i].x,start->carts[i].y,
                start->carts[i].z,(long long)start->carts[i].loot_seed);
    for(int i=0;i<start->spawner_count;i++)
        fprintf(stderr,"spawner=%d,%d,%d,cave_spider\n",start->spawners[i].x,
                start->spawners[i].y,start->spawners[i].z);
    if(emit) for(int y=0;y<=63;y++) for(int z=0;z<16;z++) for(int x=0;x<16;x++)
        printf("%04x\n",(unsigned)primer->data[ms_idx(x,y,z)]);
    free(primer); free(g); return 0;
}

int main(int argc, char **argv) {
    if (argc >= 6 && strcmp(argv[1], "--topology-type") == 0) {
        g_forced_type=atoi(argv[5]); g_ms_type_at=cpu_forced_type_at;
        print_topology(strtoll(argv[2],0,10),atoi(argv[3]),atoi(argv[4]));
        return 0;
    }
    if (argc >= 5 && strcmp(argv[1], "--topology-live") == 0) {
        g_ms_type_at=cpu_mineshaft_type_at;
        print_topology(strtoll(argv[2],0,10),atoi(argv[3]),atoi(argv[4]));
        return 0;
    }
    if (argc >= 5 && strcmp(argv[1], "--topology") == 0) {
        print_topology(strtoll(argv[2],0,10),atoi(argv[3]),atoi(argv[4]));
        return 0;
    }
    if (argc >= 9 && strcmp(argv[1], "--placement") == 0)
        return print_placement(strtoll(argv[2],0,10),atoi(argv[3]),atoi(argv[4]),
            atoi(argv[5]),strtoll(argv[6],0,10),atoi(argv[7]),atoi(argv[8]),1);
    if (argc >= 9 && strcmp(argv[1], "--placement-summary") == 0)
        return print_placement(strtoll(argv[2],0,10),atoi(argv[3]),atoi(argv[4]),
            atoi(argv[5]),strtoll(argv[6],0,10),atoi(argv[7]),atoi(argv[8]),0);
    if (argc >= 10 && strcmp(argv[1], "--placement-type") == 0) {
        g_forced_type=atoi(argv[9]); g_ms_type_at=cpu_forced_type_at;
        return print_placement(strtoll(argv[2],0,10),atoi(argv[3]),atoi(argv[4]),
            atoi(argv[5]),strtoll(argv[6],0,10),atoi(argv[7]),atoi(argv[8]),1);
    }
    i64 seed = (argc > 1) ? strtoll(argv[1], 0, 10) : 12345LL;
    ChunkPrimer *p = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    ms_run(p, seed, 0, 0);
    for (int i = 0; i < 65536; ++i)
        printf("%04x\n", (unsigned)p->data[i]);
    free(p);
    return 0;
}
