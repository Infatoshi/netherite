#ifndef MAGMA_GAME_STRUCTURES_LIVE_H
#define MAGMA_GAME_STRUCTURES_LIVE_H

typedef struct { int min_x,min_y,min_z,max_x,max_y,max_z; } GmStructureBox;

int gm_stronghold_locate(long long seed, int index, int *block_x, int *block_z);
int gm_stronghold_portal_room(long long seed, int index, GmStructureBox *box);
int gm_fortress_locate(long long seed, int search_radius, int *block_x, int *block_z);
int gm_fortress_spawner_room(long long seed, int search_radius, GmStructureBox *box);

#endif
