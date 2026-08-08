/* game/chest_live.h - live chest inventory core (TileEntityChest, 27 slots).
 *
 * Wraps blaze tile_entity_chest.h for ordinary/trapped insert/extract and lid
 * open-count. The runtime world block controls type-specific behavior.
 * Loot fill is deferred until first access (vanilla LockableLoot pattern).
 *
 * Comparator queries compose represented ordinary/trapped double halves.
 * CUT: double-chest GUI composition, ender chest, and sounds. Lid angle is
 * ticked for TE state fidelity but the chunk mesh does not animate the lid
 * (TESR cut). */
#ifndef MAGMA_GAME_CHEST_LIVE_H
#define MAGMA_GAME_CHEST_LIVE_H

#include "tile_entity_chest.h"
#include "items_core.h"

enum {
    CHEST_LIVE_SLOTS = TEC_SLOTS, /* 27 */
    /* stronghold loot tables (vanilla LootTableList CHESTS_STRONGHOLD_*) */
    CHEST_LOOT_NONE     = -1,
    CHEST_LOOT_CORRIDOR = 0,
    CHEST_LOOT_LIBRARY  = 1,
    CHEST_LOOT_CROSSING = 2,
    CHEST_LOOT_SIMPLE_DUNGEON = 3,
    CHEST_LOOT_ABANDONED_MINESHAFT = 4,
    CHEST_LOOT_END_CITY = 5,
    CHEST_LOOT_DESERT_PYRAMID = 6,
    CHEST_LOOT_JUNGLE_TEMPLE = 7,
    CHEST_LOOT_VILLAGE_BLACKSMITH = 9
};

typedef struct {
    TeChest te;
    int loot_table;     /* CHEST_LOOT_* or NONE */
    long long loot_seed;
    int loot_filled;    /* 1 once generateLoot has run (or no table) */
} ChestLive;

void chest_live_init(ChestLive *c);
void chest_live_set_loot(ChestLive *c, int table, long long seed);

/* Ensure loot is generated into slots if a table is pending. */
void chest_live_ensure_loot(ChestLive *c);

ICStack chest_live_get(const ChestLive *c, int slot);
/* Set slot contents (after ensure_loot). Empty stack clears. */
void chest_live_set(ChestLive *c, int slot, ICStack stack);

/* PICKUP/THROW helpers: take up to `amount` from slot (ensure_loot first). */
ICStack chest_live_extract(ChestLive *c, int slot, int amount);
/* Insert stack into slot; returns number of items accepted. */
int chest_live_insert(ChestLive *c, int slot, ICStack stack);

void chest_live_open(ChestLive *c);
void chest_live_close(ChestLive *c);
void chest_live_tick(ChestLive *c);

int chest_live_total_items(const ChestLive *c);

/* Container.calcRedstoneFromInventory for a 27-slot single chest. */
int chest_live_comparator_strength(const ChestLive *c);
/* The same formula over both halves of a 54-slot ordinary double chest. */
int chest_live_double_comparator_strength(
    const ChestLive *first, const ChestLive *second);

#endif /* MAGMA_GAME_CHEST_LIVE_H */
