#ifndef MAGMA_GAME_STRUCTURES_LIVE_H
#define MAGMA_GAME_STRUCTURES_LIVE_H

typedef struct { int min_x,min_y,min_z,max_x,max_y,max_z; } GmStructureBox;

int gm_stronghold_locate(long long seed, int index, int *block_x, int *block_z);
int gm_stronghold_portal_room(long long seed, int index, GmStructureBox *box);
int gm_fortress_locate(long long seed, int search_radius, int *block_x, int *block_z);
int gm_fortress_spawner_room(long long seed, int search_radius, GmStructureBox *box);

/* If (x,y,z) is a C-generated stronghold chest placement, write the vanilla
 * CHESTS_STRONGHOLD_* table id (0 corridor / 1 library / 2 crossing) and a
 * deterministic loot seed. Returns 1 on match. */
int gm_stronghold_chest_info(long long seed, int x, int y, int z,
                             int *table_id, long long *loot_seed);

#endif
