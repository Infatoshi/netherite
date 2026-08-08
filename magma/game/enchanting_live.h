/* Live 1.11.2 enchanting-table state. Offer construction is shared with the
 * CPU/CUDA enchant_table oracle; this layer owns world bookshelf scanning and
 * the two-slot/XP/lapis mutation boundary. */
#ifndef MAGMA_GAME_ENCHANTING_LIVE_H
#define MAGMA_GAME_ENCHANTING_LIVE_H

#include "enchant_table.h"
#include "items_core.h"

struct GmWorld;

typedef struct {
    int open;
    int wx, wy, wz;
    int xp_seed;
    int power;
    ICStack slots[2]; /* item, lapis */
    EtOffer offer;
} GmEnchantingLive;

void enchanting_live_init(GmEnchantingLive *e);
int enchanting_live_power(
    const struct GmWorld *world, int wx, int wy, int wz);
void enchanting_live_open(
    GmEnchantingLive *e, const struct GmWorld *world,
    int wx, int wy, int wz, int xp_seed);
void enchanting_live_recompute(
    GmEnchantingLive *e, const struct GmWorld *world);
int enchanting_live_slot_valid(int slot, const ICStack *stack);
void enchanting_live_set_slot(
    GmEnchantingLive *e, const struct GmWorld *world,
    int slot, ICStack stack);
/* Returns 1 on a completed enchant and updates player_level/xp_seed. */
int enchanting_live_apply(
    GmEnchantingLive *e, const struct GmWorld *world, int button,
    int creative, int *player_level, JavaRandom *player_random);

#endif
