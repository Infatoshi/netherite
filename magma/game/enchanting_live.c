#include "game/enchanting_live.h"

#include "game/game.h"

#include <string.h>

static int enchanting_item_kind(const ICStack *stack)
{
    if (!stack || stack->item <= 0 || stack->count <= 0
            || stack->n_enchants != 0)
        return -1;
    return et_item_kind_from_id(stack->item);
}

void enchanting_live_init(GmEnchantingLive *e)
{
    if (!e) return;
    memset(e, 0, sizeof *e);
    e->slots[0] = ic_empty();
    e->slots[1] = ic_empty();
    for (int i = 0; i < 3; ++i) {
        e->offer.clue_id[i] = -1;
        e->offer.clue_lvl[i] = -1;
    }
}

static int enchanting_is_air_pair(
    const GmWorld *world, int x, int y, int z)
{
    return gm_world_block(world, x, y, z) == 0
        && gm_world_block(world, x, y + 1, z) == 0;
}

static int enchanting_power_at(
    const GmWorld *world, int x, int y, int z)
{
    return gm_world_block(world, x, y, z) == 47;
}

int enchanting_live_power(
    const GmWorld *world, int wx, int wy, int wz)
{
    int power = 0;
    if (!world) return 0;
    for (int j = -1; j <= 1; ++j) {
        for (int k = -1; k <= 1; ++k) {
            if ((j == 0 && k == 0)
                    || !enchanting_is_air_pair(
                        world, wx + k, wy, wz + j))
                continue;
            power += enchanting_power_at(
                world, wx + k * 2, wy, wz + j * 2);
            power += enchanting_power_at(
                world, wx + k * 2, wy + 1, wz + j * 2);
            if (k != 0 && j != 0) {
                power += enchanting_power_at(
                    world, wx + k * 2, wy, wz + j);
                power += enchanting_power_at(
                    world, wx + k * 2, wy + 1, wz + j);
                power += enchanting_power_at(
                    world, wx + k, wy, wz + j * 2);
                power += enchanting_power_at(
                    world, wx + k, wy + 1, wz + j * 2);
            }
        }
    }
    return power;
}

void enchanting_live_recompute(
    GmEnchantingLive *e, const GmWorld *world)
{
    int kind;
    if (!e) return;
    e->power = enchanting_live_power(world, e->wx, e->wy, e->wz);
    kind = enchanting_item_kind(&e->slots[0]);
    if (kind < 0) {
        memset(&e->offer, 0, sizeof e->offer);
        for (int i = 0; i < 3; ++i) {
            e->offer.clue_id[i] = -1;
            e->offer.clue_lvl[i] = -1;
        }
        return;
    }
    et_compute_offers(e->xp_seed, e->power, kind, &e->offer);
}

void enchanting_live_open(
    GmEnchantingLive *e, const GmWorld *world,
    int wx, int wy, int wz, int xp_seed)
{
    if (!e) return;
    e->open = 1;
    e->wx = wx;
    e->wy = wy;
    e->wz = wz;
    e->xp_seed = xp_seed;
    enchanting_live_recompute(e, world);
}

int enchanting_live_slot_valid(int slot, const ICStack *stack)
{
    if (slot == 0)
        return !stack || stack->count <= 0
            || enchanting_item_kind(stack) >= 0;
    if (slot == 1)
        return !stack || stack->count <= 0
            || (stack->item == 351 && stack->meta == 4);
    return 0;
}

void enchanting_live_set_slot(
    GmEnchantingLive *e, const GmWorld *world,
    int slot, ICStack stack)
{
    if (!e || slot < 0 || slot > 1
            || !enchanting_live_slot_valid(slot, &stack))
        return;
    e->slots[slot] = stack.count > 0 ? stack : ic_empty();
    enchanting_live_recompute(e, world);
}

int enchanting_live_apply(
    GmEnchantingLive *e, const GmWorld *world, int button,
    int creative, int *player_level, JavaRandom *player_random)
{
    int kind, cost, level, n;
    EtData list[ET_MAX_LIST];
    JavaRandom offer_random;
    if (!e || !player_level || !player_random
            || button < 0 || button > 2)
        return 0;
    kind = enchanting_item_kind(&e->slots[0]);
    cost = button + 1;
    level = e->offer.levels[button];
    if (kind < 0 || level <= 0
            || (!creative && (e->slots[1].item != 351
                || e->slots[1].meta != 4
                || e->slots[1].count < cost
                || *player_level < cost || *player_level < level)))
        return 0;
    n = et_get_enchantment_list(
        &offer_random, e->xp_seed, button, level,
        kind, list, ET_MAX_LIST);
    if (n <= 0) return 0;
    if (!creative) {
        *player_level -= cost;
        e->slots[1].count -= cost;
        if (e->slots[1].count <= 0) e->slots[1] = ic_empty();
    }
    if (et_item_is_book(kind)) {
        e->slots[0] = ic_mk(403, 1, 0);
    }
    if (n > IC_MAX_ENCHANTS) n = IC_MAX_ENCHANTS;
    e->slots[0].n_enchants = n;
    for (int i = 0; i < n; ++i) {
        e->slots[0].enchants[i].id = (i16)list[i].id;
        e->slots[0].enchants[i].level = (i16)list[i].level;
    }
    for (int i = n; i < IC_MAX_ENCHANTS; ++i) {
        e->slots[0].enchants[i].id = 0;
        e->slots[0].enchants[i].level = 0;
    }
    e->xp_seed = (i32)jrand_next(player_random, 32);
    enchanting_live_recompute(e, world);
    return 1;
}
