#ifndef VERIFY_TAPE_MCA_H
#define VERIFY_TAPE_MCA_H

#include <stdint.h>

#define MCA_CHUNK_CELLS 65536

typedef struct {
    int t;
    int ax, ay, az;
} McaPose;

typedef struct {
    int cx0, cz0, ncx, ncz;
    uint16_t **grid;
    int loaded;
    uint64_t file_bytes;
} McaStore;

uint16_t mca_pack_state(unsigned id, unsigned meta);
void mca_store_free(McaStore *st);
int mca_load(McaStore *st, const char *world, const McaPose *poses, int npose,
             int rd, int rmax);
uint16_t mca_world_get(const McaStore *st, int x, int y, int z);
/* FNV over 9x9x9 at (cx,cy,cz). Same basis, prime, order, and block-175
 * packing as magma/game/script.c nearby_hash. */
unsigned long long mca_nearby_hash(const McaStore *st, int cx, int cy, int cz);

#endif
