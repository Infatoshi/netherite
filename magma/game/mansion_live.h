#ifndef MAGMA_MANSION_LIVE_H
#define MAGMA_MANSION_LIVE_H

#define GM_MANSION_MAX_PIECES 1024

typedef struct {
    short template_index;
    int x, y, z;
    int min_x, min_y, min_z, max_x, max_y, max_z;
    unsigned char rotation;
    unsigned char mirror;
} GmMansionPiece;

typedef struct {
    GmMansionPiece pieces[GM_MANSION_MAX_PIECES];
    int count;
} GmMansion;

int gm_mansion_build(
    long long layout_seed, int start_x, int start_y, int start_z,
    int rotation, GmMansion *out);
int gm_mansion_build_at_chunk(
    long long world_seed, int chunk_x, int chunk_z, int start_y,
    GmMansion *out);
void gm_mansion_candidate_for_region(
    long long world_seed, int region_x, int region_z,
    int *chunk_x, int *chunk_z);
int gm_mansion_candidate(long long world_seed, int chunk_x, int chunk_z);
void gm_mansion_transform(
    int mirror, int rotation, int x, int y, int z,
    int *out_x, int *out_y, int *out_z);

#endif
