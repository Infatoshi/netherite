#include <stdio.h>
#include <stdlib.h>

#include "game/structures_live.h"
#include "map_gen_fortress.h"

int main(int argc, char **argv) {
    long long seed;
    int sx, sz, fx, fz;
    GmStructureBox portal, spawner;
    if (argc != 2) {
        fprintf(stderr, "usage: route_query SEED\n");
        return 2;
    }
    seed = strtoll(argv[1], NULL, 10);
    if (!gm_stronghold_locate(seed, 0, &sx, &sz) ||
        !gm_stronghold_portal_room(seed, 0, &portal) ||
        !gm_fortress_locate(seed, 64, &fx, &fz) ||
        !gm_fortress_spawner_room(seed, 64, &spawner)) {
        fprintf(stderr, "route_query: structure lookup failed\n");
        return 1;
    }
    printf("{\"seed\":%lld,\"stronghold\":{\"locate\":[%d,%d],"
           "\"portal_room\":[%d,%d,%d,%d,%d,%d]},"
           "\"fortress\":{\"locate\":[%d,%d],"
           "\"spawner_room\":[%d,%d,%d,%d,%d,%d]}}\n",
           seed, sx, sz,
           portal.min_x, portal.min_y, portal.min_z,
           portal.max_x, portal.max_y, portal.max_z,
           fx, fz,
           spawner.min_x, spawner.min_y, spawner.min_z,
           spawner.max_x, spawner.max_y, spawner.max_z);
    {
        FtStart *start = malloc(sizeof *start);
        int cx = (fx - 8) / 16;
        int cz = (fz - 8) / 16;
        if (!start) return 1;
        ft_generate(start, (i64)seed, cx, cz);
        printf("{\"fortress_pieces\":[");
        for (int i = 0; i < start->piece_count; ++i) {
            FtPiece *p = &start->pieces[i];
            printf("%s[%d,%d,%d,%d,%d,%d,%d,%d,%d]",
                   i ? "," : "", i, p->type, p->component_type,
                   p->bb.minX, p->bb.minY, p->bb.minZ,
                   p->bb.maxX, p->bb.maxY, p->bb.maxZ);
        }
        printf("]}\n");
        free(start);
    }
    return 0;
}
