#ifndef MAGMA_END_CITY_LIVE_H
#define MAGMA_END_CITY_LIVE_H

#include <stdint.h>

#define GM_END_CITY_MAX_PIECES 256

typedef struct {
    short template_index;
    int component_type;
    int x, y, z;
    int min_x, min_y, min_z, max_x, max_y, max_z;
    unsigned char rotation;
    unsigned char overwrite;
} GmEndCityPiece;

typedef struct {
    GmEndCityPiece pieces[GM_END_CITY_MAX_PIECES];
    int count;
    int ship_created;
} GmEndCity;

int gm_end_city_candidate(long long seed, int chunk_x, int chunk_z);
void gm_end_city_candidate_for_region(
    long long seed, int region_x, int region_z, int *chunk_x, int *chunk_z);
int gm_end_city_build(
    long long seed, int chunk_x, int chunk_z, int start_y, GmEndCity *out);
void gm_end_city_transform(
    int rotation, int x, int y, int z, int *out_x, int *out_y, int *out_z);
int gm_end_city_rotate_meta(int block_id, int meta, int rotation);

#endif
