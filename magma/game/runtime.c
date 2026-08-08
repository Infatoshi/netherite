#include "game/runtime.h"
#include "game/randtick.h"
#include "game/sel_box.h"

#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "crafting_recipes_full.h"
#include "game/portal_live.h"
#include "game/structures_live.h"
#include "game/end_city_live.h"
#include "game/end_population_live.h"
#include "assets/end_city_templates.h"
#include "explosion.h"
#include "items_tools_armor.h"
#include "inventory_stack_rules.h"
#include "stronghold_loot.h"
#include "tile_entity_brewing.h"
#include "block_normal_cube_1_11_2.h"
#include "projectile_motion.h"
#include "world/populate_mc.h"
#include "chunk_provider_end.h"

static int runtime_java_random_next_int(GmRuntime *r, int bound);
static int runtime_java_random_seed_next_int(uint64_t *seed48, int bound);
static float runtime_java_random_next_float(GmRuntime *r);
static double runtime_math_random_next_double(GmRuntime *r);
static void runtime_redstone_player_pressure_plate_collisions(
    GmRuntime *r, int clock_offset);
static void runtime_redstone_mob_pressure_plate_collisions(
    GmRuntime *r, int controlled_only);
static void runtime_redstone_item_pressure_plate_collisions(GmRuntime *r);
static void runtime_redstone_xp_pressure_plate_collisions(GmRuntime *r);
static void runtime_redstone_update_observers_at(
    GmRuntime *r, int x, int y, int z);
static void runtime_redstone_piston_check(
    GmRuntime *r, int x, int y, int z);
static void runtime_tick_pistons(GmRuntime *r);
static void runtime_tick_primed_tnt(GmRuntime *r);
static void runtime_explode_with_rays(
    GmRuntime *r, double ex, double ey, double ez, float size,
    int exact_world_random);
static void runtime_explode_with_flags(
    GmRuntime *r, double ex, double ey, double ez, float size,
    int exact_world_random, int advance_player_boundary, int flaming);
static void runtime_fire_on_added(GmRuntime *r, int x, int y, int z);
static int runtime_is_stair_id(int id);
static int runtime_is_fence_id(int id);
static int runtime_is_fence_gate_id(int id);
static int runtime_stair_side_solid(
    const GmRuntime *r, int x, int y, int z, int meta, int side);
static int runtime_stair_collision_shapes(
    const GmRuntime *r, int x, int y, int z, int meta,
    McAABB shapes[5]);
static void runtime_flower_pot_remove(
    GmRuntime *r, int dimension, int x, int y, int z);
static void runtime_skull_remove(
    GmRuntime *r, int dimension, int x, int y, int z);
static void runtime_static_container_remove(
    GmRuntime *r, int dimension, int x, int y, int z);
static void runtime_redstone_update_comparator_output_level(
    GmRuntime *r, int x, int y, int z);
static int runtime_chest_ensure_tile(
    GmRuntime *r, int x, int y, int z);
static void runtime_ensure_nearby_end_cities(GmRuntime *r);
static void runtime_ensure_nearby_end_population(GmRuntime *r);
static int runtime_end_population_get(void *ctx, int x, int y, int z);
static void runtime_end_population_set(
    void *ctx, int x, int y, int z, int id, int meta);
static int runtime_attack_item_frame(GmRuntime *r);

static int runtime_generated_chest_info(
        const GmRuntime *r, int x, int y, int z,
        int *table_id, long long *loot_seed) {
    int tid = -1;
    long long seed = 0;
    if (!r || r->dimension != 0) return 0;
    if (gm_stronghold_chest_info(r->seed, x, y, z, &tid, &seed)) {
        if (table_id) *table_id = tid;
        if (loot_seed) *loot_seed = seed;
        return 1;
    }
    if (popmc_dungeon_chest_info(r->seed, x, y, z, &seed, NULL)) {
        if (table_id) *table_id = CHEST_LOOT_SIMPLE_DUNGEON;
        if (loot_seed) *loot_seed = seed;
        return 1;
    }
    if (popmc_desert_chest_info(r->seed, x, y, z, &seed, NULL)) {
        if (table_id) *table_id = CHEST_LOOT_DESERT_PYRAMID;
        if (loot_seed) *loot_seed = seed;
        return 1;
    }
    if (popmc_jungle_chest_info(r->seed, x, y, z, &seed, NULL)) {
        if (table_id) *table_id = CHEST_LOOT_JUNGLE_TEMPLE;
        if (loot_seed) *loot_seed = seed;
        return 1;
    }
    if (popmc_village_chest_info(r->seed, x, y, z, &seed, NULL)) {
        if (table_id) *table_id = CHEST_LOOT_VILLAGE_BLACKSMITH;
        if (loot_seed) *loot_seed = seed;
        return 1;
    }
    return 0;
}

/* Minecraft 1.11.2 MathHelper.atan2 uses a 257-entry arcsine table and one
 * inverse-square-root Newton step. Projectile rotation exposes the resulting
 * float approximation, so libc atan2 is observably different. */
static double runtime_java_math_atan2(double y, double x)
{
    static double asine[257];
    static double cosine[257];
    static int initialized;
    const double frac_bias = 17592186044416.0;
    double squared = x * x + y * y;
    double half;
    double inv;
    double biased;
    double table_value;
    double table_cosine;
    double quantized;
    double error;
    double correction;
    uint64_t bits;
    int negative_y;
    int negative_x;
    int swapped;
    int index;

    if (isnan(squared)) return NAN;
    if (!initialized) {
        for (int i = 0; i <= 256; ++i) {
            double value = asin((double)i / 256.0);
            asine[i] = value;
            cosine[i] = cos(value);
        }
        initialized = 1;
    }
    negative_y = y < 0.0;
    if (negative_y) y = -y;
    negative_x = x < 0.0;
    if (negative_x) x = -x;
    swapped = y > x;
    if (swapped) {
        double hold = x;
        x = y;
        y = hold;
    }
    half = 0.5 * squared;
    memcpy(&bits, &squared, sizeof bits);
    bits = UINT64_C(6910469410427058090) - (bits >> 1);
    memcpy(&inv, &bits, sizeof inv);
    inv = inv * (1.5 - half * inv * inv);
    x *= inv;
    y *= inv;
    biased = frac_bias + y;
    memcpy(&bits, &biased, sizeof bits);
    index = (int)(uint32_t)bits;
    table_value = asine[index];
    table_cosine = cosine[index];
    quantized = biased - frac_bias;
    error = y * table_cosine - x * quantized;
    correction = (6.0 + error * error) * error
        * 0.16666666666666666;
    table_value += correction;
    if (swapped) table_value = MC_PI / 2.0 - table_value;
    if (negative_x) table_value = MC_PI - table_value;
    if (negative_y) table_value = -table_value;
    return table_value;
}

static int runtime_controlled_tnt_precedes_mob(const GmRuntime *r)
{
    GmMobExplosionTarget targets[GM_MOB_CAPACITY];
    int first_mob_eid = 0;
    int first_tnt_eid = 0;
    int count;
    if (!r || r->mobs_enabled || !r->controlled_mobs_enabled
            || r->primed_tnt_count <= 0)
        return 0;
    count = gm_mobs_explosion_targets(
        &r->mobs, r->dimension, targets, GM_MOB_CAPACITY);
    for (int i = 0; i < count; ++i)
        if (first_mob_eid == 0 || targets[i].eid < first_mob_eid)
            first_mob_eid = targets[i].eid;
    for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i) {
        const GmRuntimePrimedTnt *tnt = &r->primed_tnt[i];
        if (!tnt->active || tnt->dimension != r->dimension)
            continue;
        if (first_tnt_eid == 0 || tnt->eid < first_tnt_eid)
            first_tnt_eid = tnt->eid;
    }
    /* Exact for bounded one-mob-or-boat/one-TNT saved-state rows. Interleaved
     * multi-entity ordering still needs an explicit loaded-entity order in
     * the state capsule rather than grouping by type. */
    return first_tnt_eid > 0 && first_mob_eid > 0
        && first_tnt_eid < first_mob_eid;
}

static int runtime_controlled_item_precedes_tnt(const GmRuntime *r)
{
    int first_item_eid = 0;
    int first_tnt_eid = 0;
    if (!r || r->primed_tnt_count <= 0)
        return 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *item = &r->entities.ents[i];
        if (!item->active || item->type != 0
                || !item->controlled_stationary || item->eid <= 0)
            continue;
        if (first_item_eid == 0 || item->eid < first_item_eid)
            first_item_eid = item->eid;
    }
    for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i) {
        const GmRuntimePrimedTnt *tnt = &r->primed_tnt[i];
        if (!tnt->active || tnt->dimension != r->dimension)
            continue;
        if (first_tnt_eid == 0 || tnt->eid < first_tnt_eid)
            first_tnt_eid = tnt->eid;
    }
    /* Exact for the bounded stationary-item/TNT saved-state row. General
     * interleaving still needs an explicit loaded-entity order. */
    return first_item_eid > 0 && first_tnt_eid > 0
        && first_item_eid < first_tnt_eid;
}

static int runtime_controlled_tnt_precedes_falling(const GmRuntime *r)
{
    int first_falling_eid = 0;
    int first_tnt_eid = 0;
    if (!r || r->falling_block_count <= 0 || r->primed_tnt_count <= 0)
        return 0;
    for (int i = 0; i < GM_RUNTIME_FALLING_BLOCKS; ++i) {
        const GmRuntimeFallingBlock *falling = &r->falling_blocks[i];
        if (!falling->active || falling->eid <= 0)
            continue;
        if (first_falling_eid == 0 || falling->eid < first_falling_eid)
            first_falling_eid = falling->eid;
    }
    for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i) {
        const GmRuntimePrimedTnt *tnt = &r->primed_tnt[i];
        if (!tnt->active || tnt->dimension != r->dimension)
            continue;
        if (first_tnt_eid == 0 || tnt->eid < first_tnt_eid)
            first_tnt_eid = tnt->eid;
    }
    /* Exact for the bounded one-falling-block/one-TNT proof. General loaded
     * entity interleaving still needs an explicit order in the capsule. */
    return first_tnt_eid > 0 && first_falling_eid > 0
        && first_tnt_eid < first_falling_eid;
}

static int runtime_controlled_tnt_precedes_small_fireball(const GmRuntime *r)
{
    int first_fireball_eid = 0;
    int first_tnt_eid = 0;
    if (!r || r->primed_tnt_count <= 0)
        return 0;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        const GmRuntimeProjectile *fireball = &r->projectiles[i];
        if (!fireball->active || fireball->type != 3
                || fireball->eid <= 0)
            continue;
        if (first_fireball_eid == 0 || fireball->eid < first_fireball_eid)
            first_fireball_eid = fireball->eid;
    }
    for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i) {
        const GmRuntimePrimedTnt *tnt = &r->primed_tnt[i];
        if (!tnt->active || tnt->dimension != r->dimension)
            continue;
        if (first_tnt_eid == 0 || tnt->eid < first_tnt_eid)
            first_tnt_eid = tnt->eid;
    }
    /* Exact for the bounded one-small-fireball/one-TNT proof. */
    return first_tnt_eid > 0 && first_fireball_eid > 0
        && first_tnt_eid < first_fireball_eid;
}

static int runtime_is_chest_block(int id)
{
    return id == 54 || id == 146;
}

static int runtime_is_command_block(int id)
{
    return id == 137 || id == 210 || id == 211;
}

static int runtime_is_door_block(int id)
{
    return id == 64 || id == 71 || (id >= 193 && id <= 197);
}

static int runtime_is_shulker_box(int id)
{
    return id >= 219 && id <= 234;
}

static int runtime_door_item(int id)
{
    switch (id) {
    case 64: return 324;
    case 71: return 330;
    case 193: return 427;
    case 194: return 428;
    case 195: return 429;
    case 196: return 430;
    case 197: return 431;
    default: return 0;
    }
}

static int runtime_flower_pot_payload(
        const GmRuntime *r, int x, int y, int z,
        int *item, int *meta) {
    if (!r) return 0;
    if (r->flower_pots) {
        for (int i = 0; i < r->flower_pots_cap; ++i) {
            const GmRuntimeFlowerPot *pot = &r->flower_pots[i];
            if (pot->active && pot->dimension == r->dimension
                    && pot->wx == x && pot->wy == y && pot->wz == z) {
                if (item) *item = pot->item;
                if (meta) *meta = pot->meta;
                return pot->item > 0;
            }
        }
    }
    /* The 1.11.2 hut source requests CONTENTS=MUSHROOM_RED, but block placement
     * creates its TileEntity from legacy metadata 0. The real generated and
     * save/reloaded pot is therefore empty. Preserve that observed site value
     * while allowing an explicit player-created record above to override it. */
    if (r->dimension == 0 && r->world
            && gm_world_block(r->world, x, y, z) == 140) {
        int generated_item = 0, generated_meta = 0;
        if (popmc_swamp_pot_info(
                    r->seed, x, y, z, &generated_item, &generated_meta)) {
            if (item) *item = generated_item;
            if (meta) *meta = generated_meta;
            return generated_item > 0;
        }
    }
    return 0;
}

static int runtime_flower_pot_item_supported(int item, int meta)
{
    if (item == 6) return meta >= 0 && meta <= 5;
    if (item == 31) return meta == 2;
    if (item == 38) return meta >= 0 && meta <= 8;
    return (item == 32 || item == 37 || item == 39
            || item == 40 || item == 81) && meta == 0;
}

static int runtime_flower_pot_insert(
        GmRuntime *r, int x, int y, int z,
        int item, int meta, int decrement_held)
{
    if (!r || gm_world_block(r->world, x, y, z) != 140
            || runtime_flower_pot_payload(r, x, y, z, NULL, NULL)
            || !runtime_flower_pot_item_supported(item, meta))
        return 0;
    if (!gm_runtime_flower_pot_set(
            r, r->dimension, x, y, z, item, meta))
        return 0;
    if (decrement_held
            && isr_decr_stack_size(
                &r->player.inv, r->player.inv.current_item, 1).count <= 0)
        return 0;
    return 1;
}

static const GmRuntimeSkull *runtime_skull_payload(
        const GmRuntime *r, int x, int y, int z) {
    if (!r || !r->skulls) return NULL;
    for (int i = 0; i < r->skulls_cap; ++i) {
        const GmRuntimeSkull *skull = &r->skulls[i];
        if (skull->active && skull->dimension == r->dimension
                && skull->wx == x && skull->wy == y && skull->wz == z)
            return skull;
    }
    return NULL;
}

static int runtime_is_server_block_use(int id)
{
    return id == 58 || id == 61 || id == 62
        || id == 116
        || runtime_is_chest_block(id) || id == 120 || id == 26
        || id == 69 || id == 77 || id == 143
        || id == 93 || id == 94 || id == 149 || id == 150
        || id == 151 || id == 178
        || id == 64 || (id >= 193 && id <= 197) || id == 96
        || id == 92 || id == 84 || id == 140 || id == 46
        || runtime_is_fence_gate_id(id);
}

static int isr_slot_ok(int slot)
{
    return (slot >= 0 && slot < ISR_MAIN_SLOTS) ||
           isr_is_armor_index(slot) ||
           slot == ISR_OFFHAND_SLOT;
}

static void sync_elytra_from_chest(GmRuntime *r)
{
    ICStack chest;
    if (!r) return;
    chest = isr_get_stack(&r->player.inv, ISR_ARMOR_CHEST);
    if (chest.item == ISR_ELYTRA_ITEM)
        r->player.elytra_equipped = isr_elytra_usable(&chest);
    else if (!isr_is_empty(&chest))
        r->player.elytra_equipped = 0;
    /* empty chest: leave set_elytra test-hook flag alone */
}

#define GM_RUNTIME_MAX_EDITS 8

static int floordiv16(int a) {
    return a >= 0 ? a >> 4 : -(((-a) + 15) >> 4);
}

static void set_error(char *err, int cap, const char *msg) {
    if (err && cap > 0) snprintf(err, (size_t)cap, "%s", msg);
}

static void recenter(GmRuntime *r) {
    double wx = r->player.ent.posX + r->ox;
    double wz = r->player.ent.posZ + r->oz;
    int nccx = floordiv16((int)floor(wx));
    int nccz = floordiv16((int)floor(wz));
    if (nccx == r->ccx && nccz == r->ccz) return;
    double dx = (double)((nccx - r->ccx) * 16);
    double dz = (double)((nccz - r->ccz) * 16);
    r->ccx = nccx; r->ccz = nccz;
    r->ox = nccx * 16; r->oz = nccz * 16;
    r->player.ent.posX -= dx; r->player.ent.posZ -= dz;
    r->player.ent.box.minX -= dx; r->player.ent.box.maxX -= dx;
    r->player.ent.box.minZ -= dz; r->player.ent.box.maxZ -= dz;
    r->server_player.ent.posX -= dx; r->server_player.ent.posZ -= dz;
    r->server_player.ent.box.minX -= dx;
    r->server_player.ent.box.maxX -= dx;
    r->server_player.ent.box.minZ -= dz;
    r->server_player.ent.box.maxZ -= dz;
    r->player_last_reported_x -= dx;
    r->player_last_reported_z -= dz;
    if (r->player_move_packet.pending && r->player_move_packet.moving) {
        r->player_move_packet.x -= dx;
        r->player_move_packet.z -= dz;
    }
}

int gm_runtime_sync_village_residents(GmRuntime *r) {
    PopmcVillageResident sites[GM_RUNTIME_VILLAGE_RESIDENTS];
    int radius, count, spawned = 0;
    long builds;
    if (!r || !r->world || !r->villages_enabled || !r->mobs_enabled
            || r->dimension != 0)
        return 0;
    builds = popmc_window_builds();
    if (r->village_scan_x == r->ccx
            && r->village_scan_z == r->ccz
            && r->village_scan_builds == builds)
        return 0;
    r->village_scan_x = r->ccx;
    r->village_scan_z = r->ccz;
    r->village_scan_builds = builds;
    radius = r->view_distance;
    if (radius < 2) radius = 2;
    if (radius > 8) radius = 8;
    count = popmc_village_residents(
        r->seed, (r->ccx - radius) * 16, (r->ccz - radius) * 16,
        (r->ccx + radius + 1) * 16 - 1,
        (r->ccz + radius + 1) * 16 - 1,
        sites, GM_RUNTIME_VILLAGE_RESIDENTS);
    for (int i = 0; i < count; ++i) {
        int claimed = 0;
        /* Zombie-village materialization needs the zombie-villager data
         * model, so keep those sites available for that later slice. */
        if (sites[i].zombie_infested) continue;
        for (int j = 0; j < r->village_resident_count; ++j)
            if (r->village_residents[j].x == sites[i].x
                    && r->village_residents[j].y == sites[i].y
                    && r->village_residents[j].z == sites[i].z) {
                claimed = 1;
                break;
            }
        if (claimed
                || r->village_resident_count >= GM_RUNTIME_VILLAGE_RESIDENTS)
            continue;
        int slot = gm_mobs_spawn_villager(
            &r->mobs, sites[i].x + 0.5, sites[i].y,
            sites[i].z + 0.5, sites[i].profession);
        if (slot < 0)
            continue;
        {
            const EwStore *store = r->mobs.current ? &r->mobs.b : &r->mobs.a;
            GmRuntimeVillageResident *resident =
                &r->village_residents[r->village_resident_count++];
            memset(resident, 0, sizeof *resident);
            resident->x = sites[i].x;
            resident->y = sites[i].y;
            resident->z = sites[i].z;
            resident->eid = store->id[slot];
            resident->profession = (unsigned char)sites[i].profession;
        }
        ++spawned;
    }
    return spawned;
}

/* World.isFlammableWithin(playerBox.contract(.001)). The player box spans
 * only a handful of cells, so this remains a bounded hot-path lookup. */
static int runtime_player_in_fire(
        const GmRuntime *r, const PsvPlayer *player) {
    const McAABB *bb = &player->ent.box;
    int x0 = mc_floor(bb->minX + 0.001);
    int x1 = psv_ceil(bb->maxX - 0.001);
    int y0 = mc_floor(bb->minY + 0.001);
    int y1 = psv_ceil(bb->maxY - 0.001);
    int z0 = mc_floor(bb->minZ + 0.001);
    int z1 = psv_ceil(bb->maxZ - 0.001);
    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z) {
                int id = psv_get_block(
                    (const Chunk *)r->window, x, y, z);
                if (id == 51 || id == 10 || id == 11)
                    return 1;
            }
    return 0;
}

static int runtime_has_potion(const GmRuntime *r, int id) {
    for (int i = 0; i < r->potion_count; ++i)
        if (r->potions[i].id == id)
            return 1;
    return 0;
}

static float runtime_attack_potion_bonus(const GmRuntime *r) {
    float bonus = 0.0f;
    for (int i = 0; i < r->potion_count; ++i) {
        const GmPotionEffectView *effect = &r->potions[i];
        if (effect->id == 5)
            bonus += 3.0f * (float)(effect->amplifier + 1);
        else if (effect->id == 18)
            bonus -= 4.0f * (float)(effect->amplifier + 1);
    }
    return bonus;
}

/* Entity.move fire tail. IN_FIRE is armorable and contributes 0.1 hunger
 * exhaustion when its damage is accepted. */
static void runtime_apply_fire_contact(GmRuntime *r, int wet) {
    int hit = 0;
    if (!runtime_has_potion(r, 12))
        hit = gm_mobs_attack_player(
            &r->mobs, (struct PvStats *)&r->vitals,
            &r->player.inv, 1.0f, 0);
    if (hit == 2)
        pv_add_exhaustion(&r->vitals, 0.1f);
    r->player.health = r->vitals.health;
    r->server_player.health = r->vitals.health;
    if (!wet) {
        ++r->player_fire_ticks;
        if (r->player_fire_ticks == 0)
            r->player_fire_ticks = 8 * 20;
    }
}

/* Entity.isWet rain branch: either feet or head is in a rain-enabled biome,
 * exposed to the sky, and at/above Chunk.getPrecipitationHeight. */
static int runtime_player_is_wet(
        const GmRuntime *r, const PsvPlayer *player) {
    if (psv_in_liquid((const Chunk *)r->window, &player->ent, 1))
        return 1;
    if (!r->weather_enabled
            || gm_world_rain_strength(&r->clock, 1.0f) <= 0.2f)
        return 0;
    int x=(int)floor(player->ent.posX+(double)r->ox);
    int z=(int)floor(player->ent.posZ+(double)r->oz);
    int feet=(int)floor(player->ent.posY);
    int head=(int)floor(player->ent.box.maxY);
    int py=gm_world_precipitation_y(r->world,x,z);
    if(gm_world_precipitation_kind(r->world,x,feet,z)!=1)
        return 0;
    return py<=feet||py<=head;
}

/* BlockCactus.onEntityCollidedWithBlock. The raw one-point hit enters the
 * ordinary armor and hurt-resistance path; accepted damage contributes the
 * DamageSource default 0.1 exhaustion. */
static void runtime_apply_cactus_contact(GmRuntime *r) {
    int hit = gm_mobs_attack_player(
        &r->mobs, (struct PvStats *)&r->vitals,
        &r->player.inv, 1.0f, 0);
    if (hit == 2)
        pv_add_exhaustion(&r->vitals, 0.1f);
    r->player.health = r->vitals.health;
    r->server_player.health = r->vitals.health;
}

static int runtime_eye_in_water(
        const GmRuntime *r, const PsvPlayer *player) {
    double eye_y = player->ent.posY + psv_player_eye_height(player);
    int id = psv_get_block(
        (const Chunk *)r->window,
        mc_floor(player->ent.posX), mc_floor(eye_y),
        mc_floor(player->ent.posZ));
    return id == 8 || id == 9;
}

/* EntityPlayer.addMovementStat. Server-authoritative hunger is driven by the
 * delayed CPacketPlayer path and the EntityPlayerMP travel that follows it,
 * not by this tick's client-local movement. */
static void runtime_add_server_movement_stats(
        GmRuntime *r, const PsvPlayer *player,
        double dx, double dy, double dz, int in_water_pre) {
    if (runtime_eye_in_water(r, player)) {
        int i = (int)floorf(sqrtf(
            (float)(dx * dx + dy * dy + dz * dz)) * 100.0f + 0.5f);
        if (i > 0)
            pv_add_exhaustion(&r->vitals, 0.01f * (float)i * 0.01f);
    } else if (in_water_pre) {
        int i = (int)floorf(sqrtf(
            (float)(dx * dx + dz * dz)) * 100.0f + 0.5f);
        if (i > 0)
            pv_add_exhaustion(&r->vitals, 0.01f * (float)i * 0.01f);
    } else if (player->ent.onGround && r->server_sprinting) {
        int i = (int)floorf(sqrtf(
            (float)(dx * dx + dz * dz)) * 100.0f + 0.5f);
        if (i > 0)
            pv_add_exhaustion(&r->vitals, 0.1f * (float)i * 0.01f);
    }
}

static void runtime_set_player_position(
        PsvPlayer *player, double x, double y, double z) {
    double height = player->ent.box.maxY - player->ent.box.minY;
    player->ent.posX = x;
    player->ent.posY = y;
    player->ent.posZ = z;
    player->ent.box = psv_player_box(x, y, z);
    player->ent.box.maxY = player->ent.box.minY + height;
}

/* Entity.move(MoverType.PLAYER, packetDelta), limited to the collision and
 * block-callback surface required by NetHandlerPlayServer.processPlayer. */
static void runtime_move_server_player(
        GmRuntime *r, double dx, double dy, double dz) {
    PsvPlayer *player = &r->server_player;
    McEntity *entity = &player->ent;
    McAABB blocks[PSV_MAX_BLOCKS];
    McAABB query = mc_aabb_addcoord(&entity->box, dx, dy, dz);
    if (entity->box.maxY + 0.6 > query.maxY)
        query.maxY = entity->box.maxY + 0.6;
    int nblocks = psv_collect_blocks(
        (const Chunk *)r->window, &query, blocks, PSV_MAX_BLOCKS);
    mc_entity_move_step(
        entity, dx, dy, dz, blocks, nblocks, 0.6f);
    player->cactus_contact = 0;
    psv_do_block_collisions((const Chunk *)r->window, player);
    if (player->cactus_contact)
        runtime_apply_cactus_contact(r);
    runtime_redstone_player_pressure_plate_collisions(r, 0);
    if (runtime_player_in_fire(r, player))
        runtime_apply_fire_contact(r, runtime_player_is_wet(r, player));
}

/* Process the client movement packet queued by the preceding client tick.
 * Vanilla applies sprint EntityAction first, then processPlayer, then runs the
 * ordinary EntityPlayerMP update in the same server tick. */
static void runtime_process_server_packet(GmRuntime *r) {
    PsvPlayer *server = &r->server_player;
    GmRuntimeMovePacket packet = r->player_move_packet;
    if (r->server_sprint_pending) {
        r->server_sprinting = r->server_sprint_pending_value;
        r->server_sprint_pending = 0;
    }
    r->player_move_packet.pending = 0;
    r->player_position_packet_pending = 0;
    if (!packet.pending) return;

    double old_x = server->ent.posX;
    double old_y = server->ent.posY;
    double old_z = server->ent.posZ;
    double target_x = packet.moving ? packet.x : old_x;
    double target_y = packet.moving ? packet.y : old_y;
    double target_z = packet.moving ? packet.z : old_z;
    float target_yaw = packet.rotating ? packet.yaw : server->yaw;
    float target_pitch = packet.rotating ? packet.pitch : server->pitch;
    double dx = target_x - old_x;
    double dy = target_y - old_y;
    double dz = target_z - old_z;

    if (server->ent.onGround && !packet.on_ground && dy > 0.0) {
        server->ent.motionY = 0.41999998688697815;
        if (server->jump_boost_amplifier >= 0)
            server->ent.motionY += (double)(
                (float)(server->jump_boost_amplifier + 1) * 0.1f);
        if (r->server_sprinting) {
            float yaw = server->yaw * 0.017453292f;
            server->ent.motionX -=
                (double)(mc_sin(&r->sin_table, yaw) * 0.2f);
            server->ent.motionZ +=
                (double)(mc_cos(&r->sin_table, yaw) * 0.2f);
        }
        pv_add_exhaustion(
            &r->vitals, r->server_sprinting ? 0.2f : 0.05f);
    }

    runtime_move_server_player(r, dx, dy, dz);
    server->ent.onGround = packet.on_ground;
    runtime_set_player_position(
        server, target_x, target_y, target_z);
    server->yaw = target_yaw;
    server->pitch = target_pitch;
    runtime_add_server_movement_stats(
        r, server, target_x - old_x, target_y - old_y,
        target_z - old_z,
        psv_in_liquid((const Chunk *)r->window, &server->ent, 1));
}

static uint64_t runtime_push_pig_vehicle_packet(
        GmRuntime *r, int eid, double x, double y, double z,
        float yaw, float pitch) {
    GmRuntimePigVehiclePacket *slot = NULL;
    if (!r->pig_vehicle_packet.pending)
        slot = &r->pig_vehicle_packet;
    else if (!r->pig_vehicle_packet_deferred.pending)
        slot = &r->pig_vehicle_packet_deferred;
    if (!slot) return 0;
    uint64_t seq = ++r->pig_vehicle_packet_seq;
    *slot = (GmRuntimePigVehiclePacket){
        .pending = 1,
        .eid = eid,
        .seq = seq,
        .x = x,
        .y = y,
        .z = z,
        .yaw = yaw,
        .pitch = pitch
    };
    return seq;
}

static int runtime_pop_pig_vehicle_packet(
        GmRuntime *r, GmRuntimePigVehiclePacket *out) {
    if (!r->pig_vehicle_packet.pending || !out) return 0;
    *out = r->pig_vehicle_packet;
    r->pig_vehicle_packet = r->pig_vehicle_packet_deferred;
    memset(&r->pig_vehicle_packet_deferred, 0,
           sizeof r->pig_vehicle_packet_deferred);
    if (out->seq > r->pig_vehicle_packet_seq)
        r->pig_vehicle_packet_seq = out->seq;
    return 1;
}

/* CPacketVehicleMove is a separate channel from CPacketPlayer. Consume the
 * previous client-predicted vehicle pose against the independent server pig
 * before the ordinary living tick, exactly where NetHandlerPlayServer handles
 * it. The mover owns Entity.move contacts at the resolved temporary AABB. */
static void runtime_process_server_pig_vehicle_packet(GmRuntime *r) {
    GmRuntimePigVehiclePacket packet;
    if (!runtime_pop_pig_vehicle_packet(r, &packet)) return;
    if (!gm_mobs_pig_packet_move_runtime_dry_exact(
            &r->mobs, (const struct Chunk *)r->window,
            r->ox, r->oz, packet.eid,
            packet.x, packet.y, packet.z,
            packet.yaw, packet.pitch, &r->math_random_seed48))
        return;
    GmPigVehicleServerState server;
    if (!gm_mobs_get_pig_vehicle_server_state(&r->mobs, &server)
            || server.last_move.correction_count == 0)
        return;
    const GmPigVehicleMoveResult *move = &server.last_move;
    if (!gm_mobs_pig_apply_client_vehicle_correction(
            &r->mobs, packet.eid,
            move->correction_x, move->correction_y, move->correction_z,
            move->correction_yaw, move->correction_pitch))
        return;
    uint64_t ack_seq = runtime_push_pig_vehicle_packet(
        r, packet.eid,
        move->correction_x, move->correction_y, move->correction_z,
        move->correction_yaw, move->correction_pitch);
    r->pig_vehicle_last_correction = (GmRuntimePigVehicleCorrection){
        .valid = 1,
        .eid = packet.eid,
        .source_seq = packet.seq,
        .ack_seq = ack_seq,
        .x = move->correction_x,
        .y = move->correction_y,
        .z = move->correction_z,
        .yaw = move->correction_yaw,
        .pitch = move->correction_pitch
    };
}

/* NetHandlerPlayServer.update: run the server player entity, then restore the
 * packet-authoritative position while retaining motion and collision flags. */
static void runtime_server_player_tick(GmRuntime *r) {
    PsvPlayer *server = &r->server_player;
    double first_x = server->ent.posX;
    double first_y = server->ent.posY;
    double first_z = server->ent.posZ;
    double before_x = first_x;
    double before_y = first_y;
    double before_z = first_z;
    int water_pre = psv_in_liquid(
        (const Chunk *)r->window, &server->ent, 1);
    PsvAction action;
    McAABB blocks[PSV_MAX_BLOCKS];
    memset(&action, 0, sizeof action);
    action.yaw = server->yaw;
    action.pitch = server->pitch;
    action.sprint = r->server_sprinting;
    psv_physics_tick(
        (const Chunk *)r->window, &r->sin_table,
        server, &action, blocks);
    if (server->cactus_contact)
        runtime_apply_cactus_contact(r);
    /*
     * Java increments World.totalWorldTime before its ordinary entity pass.
     * magma advances its mirrored clock later in gm_runtime_tick, so callbacks
     * created here need one extra absolute-time tick. Packet-driven collisions
     * above occur in the preceding server phase and retain tickRate() == 20.
     */
    runtime_redstone_player_pressure_plate_collisions(r, 1);
    runtime_add_server_movement_stats(
        r, server,
        server->ent.posX - before_x,
        server->ent.posY - before_y,
        server->ent.posZ - before_z,
        water_pre);
    if (runtime_player_in_fire(r, server)) {
        runtime_apply_fire_contact(r, runtime_player_is_wet(r, server));
    } else if (r->player_fire_ticks <= 0) {
        r->player_fire_ticks = -20;
    }
    if (runtime_player_is_wet(r, server)
            && r->player_fire_ticks > 0)
        r->player_fire_ticks = -20;
    runtime_set_player_position(server, first_x, first_y, first_z);
}

static void runtime_queue_client_move_packet(GmRuntime *r) {
    PsvPlayer *client = &r->player;
    double dx = client->ent.posX - r->player_last_reported_x;
    double dy = client->ent.box.minY - r->player_last_reported_y;
    double dz = client->ent.posZ - r->player_last_reported_z;
    double dyaw = (double)(client->yaw - r->player_last_reported_yaw);
    double dpitch = (double)(client->pitch - r->player_last_reported_pitch);
    ++r->player_position_update_ticks;
    int moving = dx * dx + dy * dy + dz * dz > 9.0e-4
        || r->player_position_update_ticks >= 20;
    int rotating = dyaw != 0.0 || dpitch != 0.0;
    int send = moving || rotating
        || r->player_prev_on_ground != client->ent.onGround;

    memset(&r->player_move_packet, 0, sizeof r->player_move_packet);
    if (send) {
        r->player_move_packet.pending = 1;
        r->player_move_packet.moving = moving;
        r->player_move_packet.rotating = rotating;
        r->player_move_packet.on_ground = client->ent.onGround;
        r->player_move_packet.x = client->ent.posX;
        r->player_move_packet.y = client->ent.box.minY;
        r->player_move_packet.z = client->ent.posZ;
        r->player_move_packet.yaw = client->yaw;
        r->player_move_packet.pitch = client->pitch;
    }
    if (moving) {
        r->player_last_reported_x = client->ent.posX;
        r->player_last_reported_y = client->ent.box.minY;
        r->player_last_reported_z = client->ent.posZ;
        r->player_position_update_ticks = 0;
    }
    if (rotating) {
        r->player_last_reported_yaw = client->yaw;
        r->player_last_reported_pitch = client->pitch;
    }
    r->player_prev_on_ground = client->ent.onGround;
    r->player_position_packet_pending = r->player_move_packet.pending;

    if (client->sprinting != r->player_sprint_sent) {
        r->player_sprint_sent = client->sprinting;
        r->server_sprint_pending = 1;
        r->server_sprint_pending_value = client->sprinting;
    }
}

/* EntityPlayerSP emits CPacketVehicleMove only while the controlling passenger
 * can steer. Queue after this tick's pig travel so the following server tick
 * observes the accepted client pose. */
static void runtime_queue_client_pig_vehicle_packet(GmRuntime *r) {
    int eid = 0;
    ICStack main = isr_get_stack(
        &r->player.inv, r->player.inv.current_item);
    ICStack off = isr_get_stack(&r->player.inv, ISR_OFFHAND_SLOT);
    if (!((main.item == 398 && main.count > 0)
            || (off.item == 398 && off.count > 0)))
        return;
    double x, y, z;
    float yaw, pitch;
    if (!gm_mobs_get_pig_client_packet_pose(
            &r->mobs, &eid, &x, &y, &z, &yaw, &pitch))
        return;
    (void)runtime_push_pig_vehicle_packet(
        r, eid, x, y, z, yaw, pitch);
}

static void runtime_sync_server_player(GmRuntime *r) {
    r->server_player = r->player;
    r->player_last_reported_x = r->player.ent.posX;
    r->player_last_reported_y = r->player.ent.box.minY;
    r->player_last_reported_z = r->player.ent.posZ;
    r->player_last_reported_yaw = r->player.yaw;
    r->player_last_reported_pitch = r->player.pitch;
    r->player_prev_on_ground = r->player.ent.onGround;
    memset(&r->player_move_packet, 0, sizeof r->player_move_packet);
    r->player_position_update_ticks = 0;
    r->player_position_packet_pending = 0;
    r->player_sprint_sent = r->player.sprinting;
    r->server_sprinting = r->player.sprinting;
    r->server_sprint_pending = 0;
    r->server_sprint_pending_value = r->player.sprinting;
    memset(&r->pig_vehicle_packet, 0, sizeof r->pig_vehicle_packet);
    memset(&r->pig_vehicle_packet_deferred, 0,
           sizeof r->pig_vehicle_packet_deferred);
    r->pig_vehicle_packet_seq = 0;
    memset(&r->pig_vehicle_last_correction, 0,
           sizeof r->pig_vehicle_last_correction);
}

static int runtime_tnt_prime(GmRuntime *r, int x, int y, int z);
static float runtime_java_random_next_float(GmRuntime *r);
static int runtime_java_random_next_int(GmRuntime *r, int bound);
static int runtime_explosion_ray_blocked(
    const GmRuntime *r, double sx, double sy, double sz,
    double ex, double ey, double ez);

static float runtime_explosion_density(
        const GmRuntime *r, double ex, double ey, double ez,
        const McAABB *box) {
    double min_x = box->minX;
    double max_x = box->maxX;
    double min_z = box->minZ;
    double max_z = box->maxZ;
    double dx = 1.0D / ((max_x - min_x) * 2.0D + 1.0D);
    double dy = 1.0D / ((box->maxY - box->minY) * 2.0D + 1.0D);
    double dz = 1.0D / ((max_z - min_z) * 2.0D + 1.0D);
    double offset_x = (1.0D - floor(1.0D / dx) * dx) / 2.0D;
    double offset_z = (1.0D - floor(1.0D / dz) * dz) / 2.0D;
    int clear = 0;
    int total = 0;
    if (dx < 0.0D || dy < 0.0D || dz < 0.0D)
        return 0.0F;
    for (float fx = 0.0F; fx <= 1.0F;
            fx = (float)((double)fx + dx))
        for (float fy = 0.0F; fy <= 1.0F;
                fy = (float)((double)fy + dy))
            for (float fz = 0.0F; fz <= 1.0F;
                    fz = (float)((double)fz + dz)) {
                double sx = min_x + (max_x - min_x) * (double)fx
                    + offset_x;
                double sy = box->minY
                    + (box->maxY - box->minY) * (double)fy;
                double sz = min_z + (max_z - min_z) * (double)fz
                    + offset_z;
                if (!runtime_explosion_ray_blocked(
                        r, sx, sy, sz, ex, ey, ez))
                    ++clear;
                ++total;
            }
    return total > 0 ? (float)clear / (float)total : 0.0F;
}

static float runtime_explosion_player_density(
        const GmRuntime *r, double ex, double ey, double ez,
        const PsvPlayer *player) {
    McAABB box = player->ent.box;
    box.minX += (double)r->ox;
    box.maxX += (double)r->ox;
    box.minZ += (double)r->oz;
    box.maxZ += (double)r->oz;
    return runtime_explosion_density(r, ex, ey, ez, &box);
}

static void runtime_explosion_mob_effects(
        GmRuntime *r, double ex, double ey, double ez, float size) {
    GmMobExplosionTarget targets[GM_MOB_CAPACITY];
    float diameter = size * 2.0F;
    int count;
    if (diameter <= 0.0F) return;
    count = gm_mobs_explosion_targets(
        &r->mobs, r->dimension, targets, GM_MOB_CAPACITY);
    for (int i = 0; i < count; ++i) {
        const GmMobExplosionTarget *target = &targets[i];
        double range_x = target->x - ex;
        double range_y = target->y - ey;
        double range_z = target->z - ez;
        double normalized_range = ex_sqrt_dist(
            range_x, range_y, range_z) / (double)diameter;
        double direction_x, direction_y, direction_z, direction_length;
        double strength;
        float density;
        float damage;
        if (normalized_range > 1.0D) continue;
        direction_x = range_x;
        direction_y = target->y + (double)target->eye_height - ey;
        direction_z = range_z;
        direction_length = ex_sqrt_dist(
            direction_x, direction_y, direction_z);
        if (direction_length == 0.0D) continue;
        density = runtime_explosion_density(
            r, ex, ey, ez, &target->box);
        strength = (1.0D - normalized_range) * (double)density;
        damage = (float)(int)(
            (strength * strength + strength) / 2.0D
            * 7.0D * (double)diameter + 1.0D);
        direction_x = direction_x / direction_length * strength;
        direction_y = direction_y / direction_length * strength;
        direction_z = direction_z / direction_length * strength;
        (void)gm_mobs_apply_explosion(
            &r->mobs, target->slot, damage,
            direction_x, direction_y, direction_z, &r->entities);
    }
}

static void runtime_explosion_item_effects(
        GmRuntime *r, double ex, double ey, double ez, float size) {
    GmLiveExplosionTarget targets[GM_LIVE_MAX];
    float diameter = size * 2.0F;
    int count;
    if (diameter <= 0.0F || r->entities.n_active <= 0) return;
    count = gm_live_explosion_targets(
        &r->entities, targets, GM_LIVE_MAX);
    for (int i = 0; i < count; ++i) {
        const GmLiveExplosionTarget *target = &targets[i];
        double range_x = target->x - ex;
        double range_y = target->y - ey;
        double range_z = target->z - ez;
        double normalized_range = ex_sqrt_dist(
            range_x, range_y, range_z) / (double)diameter;
        double direction_x, direction_y, direction_z, direction_length;
        double strength;
        float density;
        float damage;
        if (normalized_range > 1.0D) continue;
        direction_x = range_x;
        direction_y = target->y + (double)(0.25F * 0.85F) - ey;
        direction_z = range_z;
        direction_length = ex_sqrt_dist(
            direction_x, direction_y, direction_z);
        if (direction_length == 0.0D) continue;
        density = runtime_explosion_density(
            r, ex, ey, ez, &target->box);
        strength = (1.0D - normalized_range) * (double)density;
        damage = (float)(int)(
            (strength * strength + strength) / 2.0D
            * 7.0D * (double)diameter + 1.0D);
        direction_x = direction_x / direction_length * strength;
        direction_y = direction_y / direction_length * strength;
        direction_z = direction_z / direction_length * strength;
        (void)gm_live_apply_explosion(
            &r->entities, target->slot, damage,
            direction_x, direction_y, direction_z);
    }
}

static void runtime_explosion_arrow_effects(
        GmRuntime *r, double ex, double ey, double ez, float size) {
    const double half_width = (double)0.5F * 0.5D;
    const double height = (double)0.5F;
    float diameter = size * 2.0F;
    if (diameter <= 0.0F) return;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        GmRuntimeProjectile *target = &r->projectiles[i];
        double range_x, range_y, range_z, normalized_range;
        double direction_x, direction_y, direction_z, direction_length;
        double strength;
        float density;
        McAABB box;
        if (!target->active || target->type != 1) continue;
        range_x = target->x - ex;
        range_y = target->y - ey;
        range_z = target->z - ez;
        normalized_range = ex_sqrt_dist(
            range_x, range_y, range_z) / (double)diameter;
        if (normalized_range > 1.0D) continue;
        /* EntityArrow.getEyeHeight() is exactly zero in 1.11.2. */
        direction_x = range_x;
        direction_y = range_y;
        direction_z = range_z;
        direction_length = ex_sqrt_dist(
            direction_x, direction_y, direction_z);
        if (direction_length == 0.0D) continue;
        box = mc_aabb_make(
            target->x - half_width, target->y, target->z - half_width,
            target->x + half_width, target->y + height,
            target->z + half_width);
        density = runtime_explosion_density(r, ex, ey, ez, &box);
        strength = (1.0D - normalized_range) * (double)density;
        target->vx += direction_x / direction_length * strength;
        target->vy += direction_y / direction_length * strength;
        target->vz += direction_z / direction_length * strength;
    }
}

static void runtime_explosion_xp_effects(
        GmRuntime *r, double ex, double ey, double ez, float size) {
    const double half_width = (double)0.5F * 0.5D;
    const double height = (double)0.5F;
    const double eye_height = (double)(0.5F * 0.85F);
    float diameter = size * 2.0F;
    if (diameter <= 0.0F) return;
    for (int i = 0; i < GM_XP_ORBS; ++i) {
        McOrb *target = &r->mobs.xp_orbs[i];
        double range_x, range_y, range_z, normalized_range;
        double direction_x, direction_y, direction_z, direction_length;
        double strength;
        float density;
        int damage;
        McAABB box;
        if (target->dead || target->xpValue <= 0
                || r->mobs.orb_dimension[i] != r->dimension)
            continue;
        range_x = target->posX - ex;
        range_y = target->posY - ey;
        range_z = target->posZ - ez;
        normalized_range = ex_sqrt_dist(
            range_x, range_y, range_z) / (double)diameter;
        if (normalized_range > 1.0D) continue;
        direction_x = range_x;
        direction_y = target->posY + eye_height - ey;
        direction_z = range_z;
        direction_length = ex_sqrt_dist(
            direction_x, direction_y, direction_z);
        if (direction_length == 0.0D) continue;
        box = mc_aabb_make(
            target->posX - half_width, target->posY,
            target->posZ - half_width,
            target->posX + half_width, target->posY + height,
            target->posZ + half_width);
        density = runtime_explosion_density(r, ex, ey, ez, &box);
        strength = (1.0D - normalized_range) * (double)density;
        damage = (int)(
            (strength * strength + strength) / 2.0D
            * 7.0D * (double)diameter + 1.0D);
        target->health -= damage;
        if (target->health <= 0) {
            target->dead = 1;
            continue;
        }
        target->motionX += direction_x / direction_length * strength;
        target->motionY += direction_y / direction_length * strength;
        target->motionZ += direction_z / direction_length * strength;
    }
}

static void runtime_explosion_falling_block_effects(
        GmRuntime *r, double ex, double ey, double ez, float size) {
    const double half_width = (double)0.98F / 2.0D;
    const double height = (double)0.98F;
    const double eye_height = (double)(0.98F * 0.85F);
    float diameter = size * 2.0F;
    if (diameter <= 0.0F || r->falling_block_count <= 0) return;
    for (int i = 0; i < GM_RUNTIME_FALLING_BLOCKS; ++i) {
        GmRuntimeFallingBlock *target = &r->falling_blocks[i];
        double range_x, range_y, range_z, normalized_range;
        double direction_x, direction_y, direction_z, direction_length;
        double strength;
        float density;
        McAABB box;
        if (!target->active) continue;
        range_x = target->x - ex;
        range_y = target->y - ey;
        range_z = target->z - ez;
        normalized_range = ex_sqrt_dist(
            range_x, range_y, range_z) / (double)diameter;
        if (normalized_range > 1.0D) continue;
        direction_x = range_x;
        direction_y = target->y + eye_height - ey;
        direction_z = range_z;
        direction_length = ex_sqrt_dist(
            direction_x, direction_y, direction_z);
        if (direction_length == 0.0D) continue;
        box = mc_aabb_make(
            target->x - half_width, target->y,
            target->z - half_width,
            target->x + half_width, target->y + height,
            target->z + half_width);
        density = runtime_explosion_density(r, ex, ey, ez, &box);
        strength = (1.0D - normalized_range) * (double)density;
        /* EntityFallingBlock inherits Entity.attackEntityFrom=false, but
         * Explosion.doExplosionA applies motion independently of damage. */
        target->vx += direction_x / direction_length * strength;
        target->vy += direction_y / direction_length * strength;
        target->vz += direction_z / direction_length * strength;
    }
}

static void runtime_explosion_primed_tnt_effects(
        GmRuntime *r, double ex, double ey, double ez, float size) {
    const double half_width = (double)0.98F / 2.0D;
    const double height = (double)0.98F;
    float diameter = size * 2.0F;
    if (diameter <= 0.0F || r->primed_tnt_count <= 0) return;
    for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i) {
        GmRuntimePrimedTnt *target = &r->primed_tnt[i];
        double range_x, range_y, range_z, normalized_range;
        double direction_x, direction_y, direction_z, direction_length;
        double strength;
        float density;
        McAABB box;
        /* The detonating TNT has already decremented to zero and Java's
         * explosion query excludes its exploder entity. */
        if (!target->active || target->dimension != r->dimension
                || target->fuse <= 0)
            continue;
        range_x = target->x - ex;
        range_y = target->y - ey;
        range_z = target->z - ez;
        normalized_range = ex_sqrt_dist(
            range_x, range_y, range_z) / (double)diameter;
        if (normalized_range > 1.0D) continue;
        direction_x = range_x;
        /* EntityTNTPrimed overrides getEyeHeight() to zero. */
        direction_y = range_y;
        direction_z = range_z;
        direction_length = ex_sqrt_dist(
            direction_x, direction_y, direction_z);
        if (direction_length == 0.0D) continue;
        box = mc_aabb_make(
            target->x - half_width, target->y,
            target->z - half_width,
            target->x + half_width, target->y + height,
            target->z + half_width);
        density = runtime_explosion_density(r, ex, ey, ez, &box);
        strength = (1.0D - normalized_range) * (double)density;
        /* EntityTNTPrimed inherits attackEntityFrom=false, but the explosion
         * adds motion independently of the damage result. */
        target->vx += direction_x / direction_length * strength;
        target->vy += direction_y / direction_length * strength;
        target->vz += direction_z / direction_length * strength;
    }
}

static void runtime_explosion_end_crystal_effects(
        GmRuntime *r, double ex, double ey, double ez, float size) {
    float diameter = size * 2.0F;
    if (diameter <= 0.0F || r->end_crystal_count <= 0) return;
    for (int i = 0; i < GM_RUNTIME_END_CRYSTALS; ++i) {
        GmRuntimeEndCrystal *target = &r->end_crystals[i];
        double range_x, range_y, range_z, normalized_range;
        double direction_y, direction_length;
        double crystal_x, crystal_y, crystal_z;
        if (!target->active || target->dimension != r->dimension)
            continue;
        range_x = target->x - ex;
        range_y = target->y - ey;
        range_z = target->z - ez;
        normalized_range = ex_sqrt_dist(
            range_x, range_y, range_z) / (double)diameter;
        if (normalized_range > 1.0D) continue;
        direction_y = target->y + (double)(2.0F * 0.85F) - ey;
        direction_length = ex_sqrt_dist(range_x, direction_y, range_z);
        if (direction_length == 0.0D) continue;
        crystal_x = target->x;
        crystal_y = target->y;
        crystal_z = target->z;
        target->active = 0;
        --r->end_crystal_count;
        /* EntityEnderCrystal.attackEntityFrom destroys it for any non-dragon
         * hit, then synchronously creates a smoking size-six explosion. */
        runtime_explode_with_rays(
            r, crystal_x, crystal_y, crystal_z, 6.0F, 1);
    }
}

static void runtime_explosion_dragon_effects(
        GmRuntime *r, double ex, double ey, double ez, float size) {
    if(r->dimension!=1||!r->dragon.initialized)return;
    EdDragon *d=&r->dragon.state.arena.dragon;
    float dd=ex_entity_damage(d->x,d->y+2,d->z,ex,ey,ez,size,1.0f);
    d->health-=dd;if(d->health<0)d->health=0;
    for(int i=0;i<ED_NUM_CRYSTALS;++i)
        if(r->dragon.state.arena.crystals[i].alive){
            EdCrystal *c=&r->dragon.state.arena.crystals[i];
            double dx=c->x-ex,dy=c->y-ey,dz=c->z-ez;
            if(dx*dx+dy*dy+dz*dz<=size*size*4.0){
                double crystal_x=c->x,crystal_y=c->y,crystal_z=c->z;
                if(!ed_mark_crystal_destroyed(&r->dragon.state.arena,i))continue;
                /* EntityEnderCrystal runs this nested explosion completely
                 * before DragonFightManager notifies the dragon. Marking it
                 * first also makes recursive crystal chains finite. */
                runtime_explode_with_rays(
                    r,crystal_x,crystal_y,crystal_z,6.0F,1);
                gm_dragon_crystal_destroyed(&r->dragon,i,0,1);
            }
        }
}

static void runtime_explosion_small_fireball_effects(
        GmRuntime *r, double ex, double ey, double ez, float size) {
    const double half_width = (double)0.3125F / 2.0D;
    const double height = (double)0.3125F;
    const double eye_height = (double)(0.3125F * 0.85F);
    float diameter = size * 2.0F;
    if (diameter <= 0.0F) return;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        GmRuntimeProjectile *target = &r->projectiles[i];
        double range_x, range_y, range_z, normalized_range;
        double direction_x, direction_y, direction_z, direction_length;
        double strength;
        float density;
        McAABB box;
        if (!target->active || target->type != 3) continue;
        range_x = target->x - ex;
        range_y = target->y - ey;
        range_z = target->z - ez;
        normalized_range = ex_sqrt_dist(
            range_x, range_y, range_z) / (double)diameter;
        if (normalized_range > 1.0D) continue;
        direction_x = range_x;
        direction_y = target->y + eye_height - ey;
        direction_z = range_z;
        direction_length = ex_sqrt_dist(
            direction_x, direction_y, direction_z);
        if (direction_length == 0.0D) continue;
        box = mc_aabb_make(
            target->x - half_width, target->y,
            target->z - half_width,
            target->x + half_width, target->y + height,
            target->z + half_width);
        density = runtime_explosion_density(r, ex, ey, ez, &box);
        strength = (1.0D - normalized_range) * (double)density;
        target->vx += direction_x / direction_length * strength;
        target->vy += direction_y / direction_length * strength;
        target->vz += direction_z / direction_length * strength;
    }
}

static void runtime_explosion_player_effect(
        GmRuntime *r, double ex, double ey, double ez, float size,
        int exact_tnt_boundary) {
    PsvPlayer *target = &r->server_player;
    float diameter = size * 2.0F;
    float density;
    double range_dx, range_dy, range_dz, normalized_range;
    double direction_dx, direction_dy, direction_dz, direction_length;
    double strength;
    float damage;
    int hit;
    if (diameter <= 0.0F)
        return;
    range_dx = target->ent.posX + (double)r->ox - ex;
    range_dy = target->ent.posY - ey;
    range_dz = target->ent.posZ + (double)r->oz - ez;
    normalized_range = ex_sqrt_dist(
        range_dx, range_dy, range_dz) / (double)diameter;
    if (normalized_range > 1.0D)
        return;
    direction_dx = range_dx;
    direction_dy = target->ent.posY
        + psv_player_eye_height(target) - ey;
    direction_dz = range_dz;
    direction_length = ex_sqrt_dist(
        direction_dx, direction_dy, direction_dz);
    if (direction_length == 0.0D)
        return;
    density = runtime_explosion_player_density(
        r, ex, ey, ez, target);
    strength = (1.0D - normalized_range) * (double)density;
    damage = (float)(int)(
        (strength * strength + strength) / 2.0D
        * 7.0D * (double)diameter + 1.0D);
    hit = gm_mobs_attack_player_source(
        &r->mobs, (struct PvStats *)&r->vitals,
        &r->player.inv, damage, 0, GM_DAMAGE_SOURCE_EXPLOSION);
    {
        ICStack chest = isr_get_stack(
            &r->player.inv, ISR_ARMOR_CHEST);
        if (chest.item == ISR_ELYTRA_ITEM)
            r->player.elytra_equipped = isr_elytra_usable(&chest);
    }
    if (hit == 2)
        pv_add_exhaustion(&r->vitals, 0.1F);
    r->player.health = r->vitals.health;
    r->server_player.health = r->vitals.health;
    if (!exact_tnt_boundary)
        return;

    /* The explosion packet is followed by velocityChanged's self-tracking
     * SPacketEntityVelocity. That second packet overwrites client motion with
     * the server vector truncated to 1/8000 before the client travel tick. */
    direction_dx = direction_dx / direction_length * strength;
    direction_dy = direction_dy / direction_length * strength;
    direction_dz = direction_dz / direction_length * strength;
    r->server_player.ent.motionX += direction_dx;
    r->server_player.ent.motionY += direction_dy;
    r->server_player.ent.motionZ += direction_dz;
    r->player.ent.motionX =
        (double)(int)(r->server_player.ent.motionX * 8000.0D) / 8000.0D;
    r->player.ent.motionY =
        (double)(int)(r->server_player.ent.motionY * 8000.0D) / 8000.0D;
    r->player.ent.motionZ =
        (double)(int)(r->server_player.ent.motionZ * 8000.0D) / 8000.0D;
    {
        PsvAction idle;
        McAABB blocks[PSV_MAX_BLOCKS];
        double server_x = r->server_player.ent.posX;
        double server_y = r->server_player.ent.posY;
        double server_z = r->server_player.ent.posZ;
        memset(&idle, 0, sizeof idle);
        idle.yaw = r->player.yaw;
        idle.pitch = r->player.pitch;
        psv_physics_tick(
            (const Chunk *)r->window, &r->sin_table,
            &r->player, &idle, blocks);
        idle.yaw = r->server_player.yaw;
        idle.pitch = r->server_player.pitch;
        psv_physics_tick(
            (const Chunk *)r->window, &r->sin_table,
            &r->server_player, &idle, blocks);
        runtime_set_player_position(
            &r->server_player, server_x, server_y, server_z);
    }
    /* In this saved entity ordering the explosion precedes the ordinary
     * player update, which ages the fresh hurt pair and runs FoodStats once. */
    gm_mobs_player_hurt_tick(&r->mobs);
    pv_on_update(&r->vitals);
    r->player.health = r->vitals.health;
    r->player.food = (float)r->vitals.foodLevel;
    r->server_player.health = r->vitals.health;
    r->server_player.food = (float)r->vitals.foodLevel;
    if (r->player_position_update_ticks > 0)
        --r->player_position_update_ticks;
    runtime_queue_client_move_packet(r);
}

static void runtime_explode_with_flags(
        GmRuntime *r, double ex, double ey, double ez, float size,
        int exact_world_random, int advance_player_boundary, int flaming) {
    u16 grid[EX_VOL];u8 hit[EX_VOL],affected[EX_VOL];
    int ox=(int)floor(ex)-8,oy=(int)floor(ey)-8,oz=(int)floor(ez)-8;
    for(int x=0;x<EX_DIM;++x)for(int y=0;y<EX_DIM;++y)for(int z=0;z<EX_DIM;++z)
        grid[ex_idx(x,y,z)]=mc_state(gm_world_block(r->world,ox+x,oy+y,oz+z),
                                     gm_world_meta(r->world,ox+x,oy+y,oz+z));
    if (exact_world_random) {
        JavaRandom random;
        jrand_set_seed48(&random, r->world_random_seed48);
        if (flaming)
            ex_do_explosion_blocks_random_affected(
                grid, ex - ox, ey - oy, ez - oz, size,
                hit, affected, &random);
        else
            ex_do_explosion_blocks_random(
                grid, ex - ox, ey - oy, ez - oz, size, hit, &random);
        r->world_random_seed48 = random.seed;
    } else if (flaming) {
        ex_do_explosion_blocks_affected(
            grid, ex - ox, ey - oy, ez - oz, size, hit, affected);
    } else {
        ex_do_explosion_blocks(grid,ex-ox,ey-oy,ez-oz,size,hit);
    }
    runtime_explosion_player_effect(
        r, ex, ey, ez, size, advance_player_boundary);
    runtime_explosion_mob_effects(r, ex, ey, ez, size);
    runtime_explosion_item_effects(r, ex, ey, ez, size);
    runtime_explosion_arrow_effects(r, ex, ey, ez, size);
    runtime_explosion_xp_effects(r, ex, ey, ez, size);
    runtime_explosion_falling_block_effects(r, ex, ey, ez, size);
    runtime_explosion_primed_tnt_effects(r, ex, ey, ez, size);
    runtime_explosion_end_crystal_effects(r, ex, ey, ez, size);
    runtime_explosion_dragon_effects(r, ex, ey, ez, size);
    runtime_explosion_small_fireball_effects(r, ex, ey, ez, size);
    if (exact_world_random) {
        /* Explosion.doExplosionB computes sound pitch before visiting affected
         * blocks, even when product audio is disabled. Preserve both draws. */
        (void)runtime_java_random_next_float(r);
        (void)runtime_java_random_next_float(r);
    }
    for(int x=0;x<EX_DIM;++x)for(int y=0;y<EX_DIM;++y)for(int z=0;z<EX_DIM;++z)
        if(hit[ex_idx(x,y,z)]){
            int block = gm_world_block(r->world,ox+x,oy+y,oz+z);
            gm_world_set_block(r->world,ox+x,oy+y,oz+z,0);
            gm_live_block_changed(&r->entities, r->world, ox+x, oy+y, oz+z);
            if (exact_world_random && block == 46
                    && runtime_tnt_prime(r, ox+x, oy+y, oz+z)) {
                int spawned_eid = r->next_entity_id - 1;
                for (int index = 0;
                        index < GM_RUNTIME_PRIMED_TNT; ++index) {
                    GmRuntimePrimedTnt *spawned = &r->primed_tnt[index];
                    if (!spawned->active || spawned->eid != spawned_eid)
                        continue;
                    spawned->fuse = runtime_java_random_next_int(
                        r, spawned->fuse / 4) + spawned->fuse / 8;
                    break;
                }
            }
            gm_fluid_mark(&r->fluids,r->world,r->dimension,ox+x,oy+y,oz+z);
        }
    if (flaming) {
        JavaRandom random;
        if (r->next_explosion_random_valid) {
            jrand_set_seed48(&random, r->next_explosion_random_seed48);
            r->next_explosion_random_valid = 0;
        } else {
            /* java.util.Random() is seeded from a process-global clock and is
             * absent from world saves. Standalone magma deliberately derives
             * a stable event seed; oracle replay supplies the captured cursor. */
            uint64_t seed = (uint64_t)r->seed
                ^ (uint64_t)r->clock.total_time * UINT64_C(0x9E3779B97F4A7C15)
                ^ (uint64_t)(int64_t)mc_floor(ex * 31.0)
                ^ ((uint64_t)(int64_t)mc_floor(ey * 37.0) << 11)
                ^ ((uint64_t)(int64_t)mc_floor(ez * 41.0) << 22);
            jrand_set(&random, (i64)seed);
        }
        for(int x=0;x<EX_DIM;++x)for(int y=0;y<EX_DIM;++y)for(int z=0;z<EX_DIM;++z)
            if(affected[ex_idx(x,y,z)]){
                int wx=ox+x,wy=oy+y,wz=oz+z;
                int below=gm_world_block(r->world,wx,wy-1,wz);
                int below_meta=gm_world_meta(r->world,wx,wy-1,wz);
                if(gm_world_block(r->world,wx,wy,wz)==0
                        && gm_block_is_full_cube_1_11_2(below,below_meta)
                        && jrand_int_bound(&random,3)==0)
                    (void)gm_runtime_set_block(r,wx,wy,wz,51,0);
            }
    }
}

static void runtime_explode_with_rays(
        GmRuntime *r, double ex, double ey, double ez, float size,
        int exact_world_random) {
    runtime_explode_with_flags(
        r, ex, ey, ez, size,
        exact_world_random, exact_world_random, 0);
}

static void runtime_explode(
        GmRuntime *r, double ex, double ey, double ez, float size) {
    runtime_explode_with_rays(r, ex, ey, ez, size, 0);
}

/* Vanilla BlockBush.checkAndDropBlock on neighborChanged: after a block edit,
 * plants above the edit that lost their support break (dropping their item).
 * Mushrooms are exempt (vanilla lets them stay on any solid block). Walking up
 * cascades reed columns and double-plant tops. */
static int plant_support_ok(GmRuntime *r, int id, int meta, int below) {
    switch (id) {
    case 6: case 31: case 37: case 38: return below == 2 || below == 3 || below == 60;
    case 32:  return below == 3 || below == 12 || below == 159 || below == 172;
    case 83:  return below == 83 || below == 2 || below == 3 || below == 12;
    case 175: return meta >= 8 ? below == 175
                               : (below == 2 || below == 3 || below == 60);
    case 111: return below == 8 || below == 9;
    case 81:  return below == 12 || below == 81;
    default:  return 1;
    }
    (void)r;
}

/* item dropped by a broken plant (0 = nothing; vanilla grass seeds are a 1/8
 * roll we skip rather than diverge on RNG) */
static int plant_drop_item(int id, int meta) {
    switch (id) {
    case 6: case 37: case 38: return id;
    case 83:  return 338;                                  /* reeds item */
    case 81:  return 81;
    case 111: return 111;
    case 175: return meta >= 8 ? 0 : 0;                    /* tops drop nothing */
    default:  return 0;
    }
}

static void break_unsupported_plants(GmRuntime *r, int wx, int wy, int wz) {
    for (int y = wy + 1; y < 255; ++y) {
        int id = gm_world_block(r->world, wx, y, wz);
        int meta = gm_world_meta(r->world, wx, y, wz);
        int below = gm_world_block(r->world, wx, y - 1, wz);
        /* BlockDoublePlant owns both halves in one neighborChanged callback.
         * Leave it for the ordered notification pass so the lower variant's
         * payload is not lost and the upper half cannot drop independently. */
        if (id == 175)
            break;
        if (plant_support_ok(r, id, meta, below)) break;
        gm_world_set_block(r->world, wx, y, wz, 0);
        int drop = plant_drop_item(id, meta);
        if (drop > 0)
            gm_live_spawn_item(&r->entities, wx + 0.5, y + 0.5, wz + 0.5,
                               drop, 1, 0, 10);
    }
}

static int ray_axis(double start, double dir, double lo, double hi,
                    double *t0, double *t1) {
    if (fabs(dir) < 1.0e-12)
        return start >= lo && start <= hi;
    double a = (lo - start) / dir;
    double b = (hi - start) / dir;
    if (a > b) { double tmp = a; a = b; b = tmp; }
    if (a > *t0) *t0 = a;
    if (b < *t1) *t1 = b;
    return *t0 <= *t1;
}

/* Minecraft.getMouseOver resolves collidable entities before clickMouse.
 * A falling block crossing the look ray therefore absorbs the held attack and
 * resets block removing; without this, replay digs terrain through the live
 * cascade while Java is harmlessly hitting EntityFallingBlock. */
static int attack_hits_falling_block(const GmRuntime *r) {
    const double border = 0.1; /* Entity.getCollisionBorderSize */
    /* EntityRenderer.getMouseOver uses Entity.getVectorForRotation, whose
     * MathHelper float trig is shared with the block ray. A libm double ray
     * flips the grazing decision that controls blockHitDelay here. */
    float f = mc_cos(&r->sin_table,
                     -r->player.yaw * 0.017453292f - 3.1415927f);
    float f1 = mc_sin(&r->sin_table,
                      -r->player.yaw * 0.017453292f - 3.1415927f);
    float f2 = -mc_cos(&r->sin_table, -r->player.pitch * 0.017453292f);
    float f3 = mc_sin(&r->sin_table, -r->player.pitch * 0.017453292f);
    double dx = (double)(f1 * f2);
    double dy = (double)f3;
    double dz = (double)(f * f2);
    double sx = r->player.ent.posX + (double)r->ox;
    double sy = r->player.ent.posY + PSV_EYE_HEIGHT;
    double sz = r->player.ent.posZ + (double)r->oz;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *e = &r->entities.ents[i];
        if (!e->active || e->type != 2) continue;
        /* The client spawn pose observed by getMouseOver is one constructor
         * half-height offset above the integrated-server simulation pose.
         * This is exactly (1 - EntityFallingBlock.height) / 2 for height .98;
         * using the server y flips a grazing ray and advances blockHitDelay. */
        double client_y = e->y + (double)((1.0f - 0.98f) / 2.0f);
        double t0 = 0.0, t1 = PSV_REACH;
        if (ray_axis(sx, dx, e->x - 0.49 - border,
                     e->x + 0.49 + border, &t0, &t1) &&
            ray_axis(sy, dy, client_y - border,
                     client_y + 0.98 + border, &t0, &t1) &&
            ray_axis(sz, dz, e->z - 0.49 - border,
                     e->z + 0.49 + border, &t0, &t1))
            return 1;
    }
    return 0;
}

static int take_arrow(PsvPlayer *p) {
    for (int i = 0; i < ISR_MAIN_SLOTS; ++i) {
        if (isr_get_stack(&p->inv, i).item != 262) continue;
        (void)isr_decr_stack_size(&p->inv, i, 1);
        return 1;
    }
    return 0;
}

static void runtime_close_container(GmRuntime *r);
static void runtime_break_chest_te(GmRuntime *r, int wx, int wy, int wz);
static void runtime_break_furnace_te(GmRuntime *r, int wx, int wy, int wz);
static void runtime_break_static_container_te(
    GmRuntime *r, int wx, int wy, int wz);
static void runtime_break_command_block_te(
    GmRuntime *r, int wx, int wy, int wz);
static void runtime_break_item_frames_for_block(
    GmRuntime *r, int wx, int wy, int wz);
static int runtime_drop_stack(
    GmRuntime *r, int wx, int wy, int wz, ICStack stack);
static void runtime_redstone_arrow_collisions(
    GmRuntime *r, const GmRuntimeProjectile *projectile);
static int runtime_redstone_pressure_plate_strength(
    const GmRuntime *r, int x, int y, int z, int block);
static void runtime_redstone_tripwire_update_state(
    GmRuntime *r, int x, int y, int z, int schedule_offset);
static void runtime_redstone_pressure_plate_update(
    GmRuntime *r, int x, int y, int z, int block,
    int old_strength, int new_strength, int schedule_delay);
static void runtime_apply_potion_type(
    GmRuntime *r, int type, double instant_factor,
    double duration_factor, int minimum_duration);
static int runtime_potion_type_effect(
    int type, int *id, int *amplifier, int *duration);
static int runtime_aabb_intersects(const McAABB *a, const McAABB *b);
static void runtime_world_event_append(
    GmRuntime *r, int id, int x, int y, int z, int data);
static float runtime_java_random_seed_next_float(uint64_t *seed48);

static void spawn_bow_arrow(GmRuntime *r, int draw) {
    float f = (float)draw / 20.0f;
    f = (f * f + f * 2.0f) / 3.0f;
    if (f < 0.1f || !take_arrow(&r->player)) return;
    if (f > 1.0f) f = 1.0f;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        GmRuntimeProjectile *p = &r->projectiles[i];
        if (p->active) continue;
        double yr = r->player.yaw * MC_PI / 180.0;
        double pr = r->player.pitch * MC_PI / 180.0;
        double dx = -sin(yr) * cos(pr), dy = -sin(pr), dz = cos(yr) * cos(pr);
        p->active = 1; p->type = 1; p->age = 0;
        p->eid = 0; p->controlled_stationary = 0;
        p->x = r->player.ent.posX + r->ox + dx * 0.2;
        p->y = r->player.ent.posY + PSV_EYE_HEIGHT + dy * 0.2;
        p->z = r->player.ent.posZ + r->oz + dz * 0.2;
        p->vx = dx * (double)(f * 3.0f);
        p->vy = dy * (double)(f * 3.0f);
        p->vz = dz * (double)(f * 3.0f);
        return;
    }
}

static i64 runtime_entity_constructor_seed(
        const GmRuntime *r, int eid, u64 purpose) {
    return (i64)mc_hash64(
        (u64)r->seed
        ^ (u64)(u32)eid * UINT64_C(0x9E3779B97F4A7C15)
        ^ purpose);
}

static void runtime_fireball_random(
        GmRuntime *r, int eid, JavaGaussianRandom *random) {
    if (r->next_fireball_random_valid) {
        jrand_gaussian_set_state(
            random, r->next_fireball_random_seed48,
            r->next_fireball_random_have_gaussian,
            r->next_fireball_random_gaussian);
        r->next_fireball_random_valid = 0;
        return;
    }
    ebf_entity_random_init(
        random, runtime_entity_constructor_seed(
            r, eid, UINT64_C(0x534D414C4C464952)));
}

static void runtime_potion_random(
        GmRuntime *r, int eid, JavaGaussianRandom *random) {
    if (r->next_potion_random_valid) {
        jrand_gaussian_set_state(
            random, r->next_potion_random_seed48,
            r->next_potion_random_have_gaussian,
            r->next_potion_random_gaussian);
        r->next_potion_random_valid = 0;
        return;
    }
    ebf_entity_random_init(
        random, runtime_entity_constructor_seed(
            r, eid, UINT64_C(0x5448524F574E504F)));
}

/* ItemSplashPotion.onItemRightClick plus EntityThrowable heading. The sound's
 * static Item.itemRand stream is separate audio/event work; projectile state,
 * inventory consumption, entity ID, and motion are represented here. */
static int runtime_throw_potion(GmRuntime *r, ICStack held) {
    GmRuntimeProjectile *p = NULL;
    JavaGaussianRandom random;
    EbfVector heading;
    float yaw_rad, pitch_rad;
    double x, y, z;
    int eid;
    if (!r || (held.item != TB_SPLASH_POTION
                    && held.item != TB_LINGERING_POTION)
            || held.count <= 0
            || held.meta < TB_PT_EMPTY || held.meta >= TB_PT_COUNT)
        return 0;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (!r->projectiles[i].active) {
            p = &r->projectiles[i];
            break;
        }
    if (!p) return 0;
    eid = r->next_entity_id;
    yaw_rad = r->player.yaw * 0.017453292F;
    pitch_rad = r->player.pitch * 0.017453292F;
    x = (double)(-mc_sin(&r->sin_table, yaw_rad)
        * mc_cos(&r->sin_table, pitch_rad));
    y = (double)(-mc_sin(
        &r->sin_table, (r->player.pitch - 20.0F) * 0.017453292F));
    z = (double)(mc_cos(&r->sin_table, yaw_rad)
        * mc_cos(&r->sin_table, pitch_rad));
    runtime_potion_random(r, eid, &random);
    heading = ebf_throwable_heading(&random, x, y, z, 0.5F, 1.0F);
    memset(p, 0, sizeof *p);
    p->active = 1;
    p->type = 6;
    p->eid = eid;
    p->shooter_eid = 0;
    p->potion_item = held.item;
    p->potion_type = held.meta;
    p->x = r->player.ent.posX + (double)r->ox;
    p->y = r->player.ent.posY + psv_player_eye_height(&r->player)
        - 0.10000000149011612;
    p->z = r->player.ent.posZ + (double)r->oz;
    p->vx = heading.x + r->player.ent.motionX;
    p->vy = heading.y
        + (r->player.ent.onGround ? 0.0 : r->player.ent.motionY);
    p->vz = heading.z + r->player.ent.motionZ;
    ++r->next_entity_id;
    (void)isr_decr_stack_size(
        &r->player.inv, r->player.inv.current_item, 1);
    return 1;
}

static void spawn_hostile_projectiles(GmRuntime *r) {
    const EwStore *s=r->mobs.current?&r->mobs.b:&r->mobs.a;
    GmPlayerView v;gm_runtime_view(r,&v);
    for(int j=1;j<EW_MAX_ENTITIES;++j){
        if (!s->alive[j]) continue;
        int blaze = s->type[j] == GM_MOB_BLAZE;
        /* Skeleton fires on its 40-tick reload edge. Blaze shots are explicit
         * AIFireballAttack step-2/3/4 events retained if the pool is full. */
        if (blaze ? r->mobs.blaze_shots_pending[j] == 0
                  : s->type[j] != EW_TYPE_SKELETON || s->attack_time[j] != 40)
            continue;
        for(int i=0;i<GM_RUNTIME_PROJECTILES;++i){
            GmRuntimeProjectile *p=&r->projectiles[i];if(p->active)continue;
            double sx=s->x[j],sy=s->y[j]+1.5,sz=s->z[j];
            double dx=v.x-s->x[j];
            double dy=(v.y+v.eye_height)-sy;
            double dz=v.z-s->z[j];
            double len=sqrt(dx*dx+dy*dy+dz*dz);
            GmBlazeShot shot;
            if(blaze){
                if(!gm_mobs_take_blaze_shot(&r->mobs,j,&shot))break;
                sx=shot.x;sy=shot.y;sz=shot.z;
                dx=shot.aim_x;dy=shot.aim_y;dz=shot.aim_z;
            }else if(len<0.001)break;
            memset(p,0,sizeof *p);
            p->active=1;p->type=blaze?3:2;p->age=0;
            p->eid=r->mobs.next_id++;p->controlled_stationary=0;
            p->x=sx;p->y=sy;p->z=sz;
            if(blaze){
                JavaGaussianRandom random;
                runtime_fireball_random(r,p->eid,&random);
                EbfVector acceleration=ebf_small_fireball_acceleration(
                    &random,dx,dy,dz);
                p->ax=acceleration.x;
                p->ay=acceleration.y;
                p->az=acceleration.z;
                p->shooting_living=1;
                p->shooter_eid=s->id[j];
            }else{
                p->vx=dx/len*1.6;p->vy=dy/len*1.6;p->vz=dz/len*1.6;
            }
            break;
        }
    }
    /* Blaze small (type 3) or ghast large (type 5) fireball pending. */
    {
        for(int i=0;i<GM_RUNTIME_PROJECTILES;++i){
            GmRuntimeProjectile *p=&r->projectiles[i];if(p->active)continue;
            double x,y,z,vx,vy,vz;
            int kind=gm_mobs_take_fireball(&r->mobs,&x,&y,&z,&vx,&vy,&vz);
            if(kind){
                memset(p,0,sizeof *p);
                p->active=1;p->type=kind;p->age=0;
                p->eid=0;p->controlled_stationary=0;
                p->x=x;p->y=y;p->z=z;p->vx=vx;p->vy=vy;p->vz=vz;
            }
            break;
        }
    }
}

static int throw_eye_of_ender(GmRuntime *r) {
    if(r->dimension!=0)return 0;
    ICStack held=isr_get_stack(&r->player.inv,r->player.inv.current_item);
    if(held.item!=381||held.count<=0)return 0;
    int sx,sz;if(!gm_stronghold_locate(r->seed,0,&sx,&sz))return 0;
    GmPlayerView v;gm_runtime_view(r,&v);
    double dx=(sx+0.5)-v.x,dz=(sz+0.5)-v.z,len=sqrt(dx*dx+dz*dz);
    if(len<0.001)return 0;
    for(int i=0;i<GM_RUNTIME_PROJECTILES;++i){GmRuntimeProjectile *p=&r->projectiles[i];
        if(p->active)continue;
        p->active=1;p->type=4;p->age=0;
        p->eid=0;p->controlled_stationary=0;
        p->x=v.x;p->y=v.y+v.eye_height;p->z=v.z;
        p->vx=dx/len*0.5;p->vy=0.25;p->vz=dz/len*0.5;
        (void)isr_decr_stack_size(&r->player.inv,r->player.inv.current_item,1);return 1;
    }
    return 0;
}

typedef struct {
    int hit;
    int bx, by, bz;
    int side;
    double x, y, z;
} GmProjectileBlockHit;

/* World.rayTraceBlocks(..., false, true, false) excludes blocks whose
 * getCollisionBoundingBox is NULL before calling collisionRayTrace. The
 * latter uses getBoundingBox, which is why a projectile ray sees a shaped
 * slab but still sees soul sand as a full cube. */
static int runtime_projectile_has_collision_box(int id, int meta) {
    switch (id) {
    case 0: case 8: case 9: case 10: case 11:       /* air/liquids */
    case 30: case 50: case 51: case 55:             /* web/torch/fire/wire */
    case 36:                                         /* moving piston ray is null */
    case 63: case 68: case 69:                      /* signs/lever */
    case 75: case 76: case 77: case 83: case 90:    /* torches/button/reeds/portal */
    case 131: case 132: case 143:                   /* tripwire/button */
    case 176: case 177: case 209: case 217:         /* banners/gateway/void */
    case 27: case 28: case 66: case 157:            /* rails */
    case 6: case 31: case 32: case 37: case 38:     /* BlockBush family */
    case 39: case 40: case 59: case 104: case 105:
    case 111: case 115: case 141: case 142:
    case 175: case 207:
        return 0;
    default:
        break;
    }
    if ((id == 107 || (id >= 183 && id <= 187)) && (meta & 4))
        return 0; /* open fence gate */
    return 1;
}

static void runtime_projectile_block_box(
        const GmRuntime *r, int x, int y, int z,
        int id, int meta, McAABB *box) {
    if (id == 88) {
        /* BlockSoulSand overrides only getCollisionBoundingBox. Its inherited
         * getBoundingBox, used by collisionRayTrace, remains a full cube. */
        *box = mc_aabb_make(x, y, z, x + 1.0, y + 1.0, z + 1.0);
        return;
    }
    GmSelIn in;
    float b[6];
    memset(&in, 0, sizeof in);
    in.id = id;
    in.meta = meta;
    in.nid[0] = gm_world_block(r->world, x, y, z - 1);
    in.nid[1] = gm_world_block(r->world, x, y, z + 1);
    in.nid[2] = gm_world_block(r->world, x - 1, y, z);
    in.nid[3] = gm_world_block(r->world, x + 1, y, z);
    in.below_meta = gm_world_meta(r->world, x, y - 1, z);
    in.above_meta = gm_world_meta(r->world, x, y + 1, z);
    gm_sel_box(&in, b);
    *box = mc_aabb_make(
        x + (double)b[0], y + (double)b[1], z + (double)b[2],
        x + (double)b[3], y + (double)b[4], z + (double)b[5]);
}

/* Geometrically equivalent to the short-segment World DDA for ordinary
 * blocks: test every cell in the segment bounds and retain the strict nearest
 * Block.collisionRayTrace intercept. Projectile traces request Java's
 * ignoreBlockWithoutBoundingBox path; explosion density does not, so bushes,
 * torches, wire, and the other outline-only blocks still stop exposure rays. */
static int runtime_block_hit(
        const GmRuntime *r, double sx, double sy, double sz,
        double ex, double ey, double ez, int ignore_without_collision_box,
        GmProjectileBlockHit *out) {
    int x0 = mc_floor(fmin(sx, ex)), x1 = mc_floor(fmax(sx, ex));
    int y0 = mc_floor(fmin(sy, ey)), y1 = mc_floor(fmax(sy, ey));
    int z0 = mc_floor(fmin(sz, ez)), z1 = mc_floor(fmax(sz, ez));
    double best = 0.0;
    memset(out, 0, sizeof *out);
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int id = gm_world_block(r->world, x, y, z);
                int meta = id ? gm_world_meta(r->world, x, y, z) : 0;
                McAABB boxes[5];
                int box_count;
                if (ignore_without_collision_box) {
                    if (!runtime_projectile_has_collision_box(id, meta))
                        continue;
                } else if (id == 0 || (id >= 8 && id <= 11)
                        || id == 36 || id == 51) {
                    /* canCollideCheck(..., false) is false for air, liquids,
                     * and fire. Moving-piston collisionRayTrace is null. */
                    continue;
                }
                if (runtime_is_stair_id(id))
                    box_count = runtime_stair_collision_shapes(
                        r, x, y, z, meta, boxes);
                else {
                    runtime_projectile_block_box(
                        r, x, y, z, id, meta, &boxes[0]);
                    box_count = 1;
                }
                for (int q = 0; q < box_count; ++q) {
                    double hx, hy, hz, dx, dy, dz, dist;
                    int side;
                    if (!pm_aabb_intercept(
                            &boxes[q], sx, sy, sz, ex, ey, ez,
                            &hx, &hy, &hz, &side))
                        continue;
                    dx = hx - sx; dy = hy - sy; dz = hz - sz;
                    dist = dx * dx + dy * dy + dz * dz;
                    if (out->hit && !(dist < best))
                        continue;
                    out->hit = 1;
                    out->bx = x; out->by = y; out->bz = z;
                    out->side = side;
                    out->x = hx; out->y = hy; out->z = hz;
                    best = dist;
                }
            }
    return out->hit;
}

static int runtime_explosion_ray_blocked(
        const GmRuntime *r, double sx, double sy, double sz,
        double ex, double ey, double ez) {
    GmProjectileBlockHit hit;
    return runtime_block_hit(
        r, sx, sy, sz, ex, ey, ez, 0, &hit);
}

static int runtime_projectile_player_hit(
        const GmRuntime *r, double sx, double sy, double sz,
        double ex, double ey, double ez, double *distance_sq) {
    const double expand = 0.30000001192092896;
    McAABB box = r->player.ent.box;
    double hx, hy, hz;
    int side;
    box = mc_aabb_offset(&box, r->ox, 0.0, r->oz);
    box.minX -= expand; box.minY -= expand; box.minZ -= expand;
    box.maxX += expand; box.maxY += expand; box.maxZ += expand;
    if (!pm_aabb_intercept(
            &box, sx, sy, sz, ex, ey, ez, &hx, &hy, &hz, &side))
        return 0;
    if (distance_sq) {
        double dx = hx - sx, dy = hy - sy, dz = hz - sz;
        *distance_sq = dx * dx + dy * dy + dz * dz;
    }
    return 1;
}

static int runtime_splash_player_candidate(
        const GmRuntime *r, const GmRuntimeProjectile *p) {
    McAABB splash = mc_aabb_make(
        p->x - 0.125 - 4.0, p->y - 2.0, p->z - 0.125 - 4.0,
        p->x + 0.125 + 4.0, p->y + 0.25 + 2.0,
        p->z + 0.125 + 4.0);
    McAABB player = mc_aabb_offset(
        &r->player.ent.box, r->ox, 0.0, r->oz);
    return runtime_aabb_intersects(&splash, &player);
}

static void runtime_splash_potion_impact_player(
        GmRuntime *r, const GmRuntimeProjectile *p, int direct_hit) {
    double px, py, pz, distance_sq, factor;
    if (!runtime_splash_player_candidate(r, p)) return;
    px = r->player.ent.posX + (double)r->ox;
    py = r->player.ent.posY;
    pz = r->player.ent.posZ + (double)r->oz;
    distance_sq = (p->x - px) * (p->x - px)
        + (p->y - py) * (p->y - py)
        + (p->z - pz) * (p->z - pz);
    if (distance_sq >= 16.0) return;
    factor = direct_hit ? 1.0 : 1.0 - sqrt(distance_sq) / 4.0;
    runtime_apply_potion_type(r, p->potion_type, factor, factor, 21);
}

static void runtime_potion_impact_mobs(
        GmRuntime *r, const GmRuntimeProjectile *p,
        int direct_slot, int water) {
    GmMobExplosionTarget targets[GM_MOB_CAPACITY];
    McAABB splash = mc_aabb_make(
        p->x - 0.125 - 4.0, p->y - 2.0, p->z - 0.125 - 4.0,
        p->x + 0.125 + 4.0, p->y + 0.25 + 2.0,
        p->z + 0.125 + 4.0);
    int potion_id = 0, amplifier = 0, duration = 0;
    int count = gm_mobs_explosion_targets(
        &r->mobs, r->dimension, targets, GM_MOB_CAPACITY);
    if (!water && !runtime_potion_type_effect(
            p->potion_type, &potion_id, &amplifier, &duration))
        return;
    for (int i = 0; i < count; ++i) {
        const GmMobExplosionTarget *target = &targets[i];
        double dx, dy, dz, distance_sq, factor;
        if (target->type == EW_TYPE_BOAT
                || !runtime_aabb_intersects(&splash, &target->box))
            continue;
        dx = p->x - target->x;
        dy = p->y - target->y;
        dz = p->z - target->z;
        distance_sq = dx * dx + dy * dy + dz * dz;
        if (distance_sq >= 16.0) continue;
        if (water) {
            (void)gm_mobs_apply_water_potion(
                &r->mobs, target->slot, &r->entities);
            continue;
        }
        factor = target->slot == direct_slot
            ? 1.0 : 1.0 - sqrt(distance_sq) / 4.0;
        if (potion_id == 6 || potion_id == 7) {
            (void)gm_mobs_apply_instant_potion(
                &r->mobs, target->slot, potion_id, amplifier,
                factor, &r->entities);
        } else {
            int scaled_duration = pt_splash_effect_duration(
                duration, factor);
            if (scaled_duration > 20)
                (void)gm_mobs_apply_potion_effect(
                    &r->mobs, target->slot, potion_id, amplifier,
                    scaled_duration);
        }
    }
}

static void runtime_splash_water_extinguish(
        GmRuntime *r, const GmProjectileBlockHit *hit) {
    static const int side_dx[6] = {0, 0, 0, 0, -1, 1};
    static const int side_dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int side_dz[6] = {0, 0, -1, 1, 0, 0};
    static const int horizontal_dx[4] = {0, 0, -1, 1};
    static const int horizontal_dz[4] = {-1, 1, 0, 0};
    int x = hit->bx + side_dx[hit->side];
    int y = hit->by + side_dy[hit->side];
    int z = hit->bz + side_dz[hit->side];
    if (gm_world_block(r->world, x, y, z) == 51)
        (void)gm_runtime_set_block(r, x, y, z, 0, 0);
    for (int i = 0; i < 4; ++i)
        if (gm_world_block(
                r->world, x + horizontal_dx[i], y,
                z + horizontal_dz[i]) == 51)
            (void)gm_runtime_set_block(
                r, x + horizontal_dx[i], y,
                z + horizontal_dz[i], 0, 0);
}

static void runtime_spawn_area_effect_cloud(
        GmRuntime *r, const GmRuntimeProjectile *p) {
    for (int i = 0; i < GM_RUNTIME_AREA_EFFECT_CLOUDS; ++i) {
        GmRuntimeAreaEffectCloud *cloud = &r->area_effect_clouds[i];
        if (cloud->state.active) continue;
        memset(cloud, 0, sizeof *cloud);
        cloud->eid = r->next_entity_id++;
        cloud->potion_type = p->potion_type;
        pt_cloud_init(&cloud->state);
        cloud->x = p->x;
        cloud->y = p->y;
        cloud->z = p->z;
        ++r->area_effect_cloud_count;
        return;
    }
}

static int runtime_potion_color(int type) {
    static const int effect_color[28] = {
        0, 8171462, 5926017, 14270531, 4866583, 9643043, 16262179,
        4393481, 2293580, 5578058, 13458603, 10044730, 14981690,
        3035801, 8356754, 2039587, 2039713, 5797459, 4738376,
        5149489, 3484199, 16284963, 2445989, 16262179, 9740385,
        13565951, 3381504, 12624973
    };
    int id, amplifier, duration;
    if (type == TB_PT_EMPTY) return 16253176;
    if (!runtime_potion_type_effect(type, &id, &amplifier, &duration))
        return 3694022;
    (void)amplifier;
    (void)duration;
    return id >= 0 && id < (int)(sizeof effect_color / sizeof effect_color[0])
        ? effect_color[id] : 0;
}

static int runtime_cloud_contains_player(
        const GmRuntime *r, const GmRuntimeAreaEffectCloud *cloud) {
    McAABB cloud_box = mc_aabb_make(
        cloud->x - (double)cloud->state.radius, cloud->y,
        cloud->z - (double)cloud->state.radius,
        cloud->x + (double)cloud->state.radius, cloud->y + 0.5,
        cloud->z + (double)cloud->state.radius);
    McAABB player_box = mc_aabb_offset(
        &r->player.ent.box, r->ox, 0.0, r->oz);
    double dx = r->player.ent.posX + (double)r->ox - cloud->x;
    double dz = r->player.ent.posZ + (double)r->oz - cloud->z;
    return runtime_aabb_intersects(&cloud_box, &player_box)
        && dx * dx + dz * dz
            <= (double)cloud->state.radius * (double)cloud->state.radius;
}

static void runtime_tick_area_effect_clouds(GmRuntime *r) {
    if (r->area_effect_cloud_count <= 0) return;
    for (int i = 0; i < GM_RUNTIME_AREA_EFFECT_CLOUDS; ++i) {
        GmRuntimeAreaEffectCloud *cloud = &r->area_effect_clouds[i];
        if (!cloud->state.active) continue;
        int scan = pt_cloud_tick(&cloud->state);
        if (!cloud->state.active) {
            --r->area_effect_cloud_count;
            continue;
        }
        if (scan) {
            GmMobExplosionTarget targets[GM_MOB_CAPACITY];
            McAABB scan_box = mc_aabb_make(
                cloud->x - (double)cloud->state.radius, cloud->y,
                cloud->z - (double)cloud->state.radius,
                cloud->x + (double)cloud->state.radius,
                cloud->y + 0.5,
                cloud->z + (double)cloud->state.radius);
            int player_candidate = runtime_cloud_contains_player(r, cloud);
            int count = 0;
            int potion_id = 0, amplifier = 0, duration = 0;
            int has_effect = runtime_potion_type_effect(
                cloud->potion_type, &potion_id, &amplifier, &duration);
            int instant = has_effect
                && (potion_id == 6 || potion_id == 7);
            if (has_effect)
                count = gm_mobs_explosion_targets(
                    &r->mobs, r->dimension,
                    targets, GM_MOB_CAPACITY);
            if (has_effect && player_candidate
                    && pt_cloud_target_ready(&cloud->state)) {
                runtime_apply_potion_type(
                    r, cloud->potion_type, 0.5, 0.25, 1);
                pt_cloud_apply(&cloud->state);
            }
            for (int target_index = 0;
                    has_effect && cloud->state.active
                    && target_index < count; ++target_index) {
                const GmMobExplosionTarget *target = &targets[target_index];
                int slot = target->slot;
                double dx, dz;
                if (target->type == EW_TYPE_BOAT
                        || !runtime_aabb_intersects(
                            &scan_box, &target->box))
                    continue;
                dx = target->x - cloud->x;
                dz = target->z - cloud->z;
                if (dx * dx + dz * dz
                        > (double)cloud->state.radius
                            * (double)cloud->state.radius)
                    continue;
                if (cloud->mob_eid[slot] == target->eid
                        && cloud->state.age
                            < cloud->mob_next_application[slot])
                    continue;
                cloud->mob_eid[slot] = target->eid;
                if (instant) {
                    (void)gm_mobs_apply_instant_potion(
                        &r->mobs, slot, potion_id, amplifier,
                        0.5, &r->entities);
                } else {
                    (void)gm_mobs_apply_potion_effect(
                        &r->mobs, slot, potion_id, amplifier,
                        duration / 4);
                }
                pt_cloud_apply_target(
                    &cloud->state,
                    &cloud->mob_next_application[slot]);
            }
        }
        if (!cloud->state.active)
            --r->area_effect_cloud_count;
    }
}

/* small_fireball_mode: -1 skips type 3, +1 ticks only type 3, 0 ticks all. */
static void tick_projectiles(GmRuntime *r, int small_fireball_mode) {
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        GmRuntimeProjectile *p = &r->projectiles[i];
        if (!p->active) continue;
        if ((small_fireball_mode < 0 && p->type == 3)
                || (small_fireball_mode > 0 && p->type != 3))
            continue;
        if (p->controlled_stationary) {
            if (p->fire_ticks > 0)
                --p->fire_ticks;
            ++p->age;
            runtime_redstone_arrow_collisions(r, p);
            continue;
        }
        if (p->type >= 6 && p->type <= 9) {
            double sx = p->x, sy = p->y, sz = p->z;
            double ex = sx + p->vx, ey = sy + p->vy, ez = sz + p->vz;
            double target_x = ex, target_y = ey, target_z = ez;
            GmProjectileBlockHit block_hit;
            int hit_block = runtime_block_hit(
                r, sx, sy, sz, ex, ey, ez, 1, &block_hit);
            if (hit_block) {
                target_x = block_hit.x;
                target_y = block_hit.y;
                target_z = block_hit.z;
            }
            int mob_slot = -1;
            double mob_distance = 0.0;
            int hit_mob = gm_mobs_projectile_intercept(
                &r->mobs, r->dimension, 0, 1,
                sx, sy, sz, target_x, target_y, target_z,
                &mob_slot, &mob_distance);
            (void)mob_distance;
            if (hit_block || hit_mob) {
                if (p->type == 9) {
                    int xp = 3 + runtime_java_random_next_int(r, 5)
                        + runtime_java_random_next_int(r, 5);
                    runtime_world_event_append(
                        r, 2002, mc_floor(p->x), mc_floor(p->y),
                        mc_floor(p->z), 3694022);
                    gm_mobs_spawn_xp(&r->mobs, p->x, p->y, p->z, xp);
                } else if (p->type == 8) {
                    if (hit_mob)
                        (void)gm_mobs_damage_near(
                            &r->mobs, target_x, target_y, target_z,
                            0.25, 0.0F, &r->entities);
                } else if (p->type == 7) {
                    /* Chick creation depends on the throwable's private RNG;
                     * the represented no-hatch path still has exact flight,
                     * impact removal, and dispenser events. */
                } else if (p->potion_type == TB_PT_WATER) {
                    if (hit_block)
                        runtime_splash_water_extinguish(r, &block_hit);
                    runtime_potion_impact_mobs(r, p, mob_slot, 1);
                } else if (p->potion_item == TB_LINGERING_POTION) {
                    runtime_spawn_area_effect_cloud(r, p);
                } else {
                    runtime_splash_potion_impact_player(r, p, 0);
                    runtime_potion_impact_mobs(r, p, mob_slot, 0);
                }
                if (p->type == 6)
                    runtime_world_event_append(
                        r,
                        p->potion_type == TB_PT_HEALING
                            || p->potion_type == TB_PT_STRONG_HEALING
                            || p->potion_type == TB_PT_HARMING
                            || p->potion_type == TB_PT_STRONG_HARMING
                            ? 2007 : 2002,
                        mc_floor(p->x), mc_floor(p->y), mc_floor(p->z),
                        runtime_potion_color(p->potion_type));
                p->active = 0;
            }
            /* EntityThrowable completes movement even when onImpact dies. */
            p->x = ex; p->y = ey; p->z = ez;
            ++p->age;
            if (!p->active || p->age >= 1200) {
                p->active = 0;
                continue;
            }
            {
                double drag = (gm_world_block(
                    r->world, mc_floor(p->x), mc_floor(p->y),
                    mc_floor(p->z)) == 8
                    || gm_world_block(
                    r->world, mc_floor(p->x), mc_floor(p->y),
                    mc_floor(p->z)) == 9) ? (double)0.8F : (double)0.99F;
                p->vx *= drag;
                p->vy *= drag;
                p->vz *= drag;
                p->vy -= p->type == 9 ? (double)0.07F
                    : p->type == 6 ? (double)0.05F : (double)0.03F;
            }
            continue;
        }
        if(p->type==4){
            p->x+=p->vx;p->y+=p->vy;p->z+=p->vz;++p->age;
            p->vx*=0.95;p->vz*=0.95;p->vy=(p->age<20)?p->vy*0.9:-0.03;
            if(p->age>=80){
                if((mc_hash64((u64)r->seed^(u64)r->tick^(u64)i)&4ULL)!=0)
                    gm_live_spawn_item(&r->entities,p->x,p->y,p->z,381,1,0,10);
                p->active=0;
            }
            continue;
        }
        if(p->type==3){
            static const int side_dx[6] = {0, 0, 0, 0, -1, 1};
            static const int side_dy[6] = {-1, 1, 0, 0, 0, 0};
            static const int side_dz[6] = {0, 0, -1, 1, 0, 0};
            double sx=p->x,sy=p->y,sz=p->z;
            double ex=sx+p->vx,ey=sy+p->vy,ez=sz+p->vz;
            double target_x=ex,target_y=ey,target_z=ez;
            GmProjectileBlockHit block_hit;
            int hit_block=runtime_block_hit(
                r,sx,sy,sz,ex,ey,ez,1,&block_hit);
            if(hit_block){
                target_x=block_hit.x;
                target_y=block_hit.y;
                target_z=block_hit.z;
            }
            double player_dist=0.0,mob_dist=0.0,dragon_dist=0.0;
            int mob_slot=-1,dragon_target=0;
            int hit_player=runtime_projectile_player_hit(
                r,sx,sy,sz,target_x,target_y,target_z,&player_dist);
            int hit_mob=gm_mobs_projectile_intercept(
                &r->mobs,r->dimension,p->shooter_eid,p->age+1>=25,
                sx,sy,sz,target_x,target_y,target_z,&mob_slot,&mob_dist);
            int hit_dragon=r->dimension==1&&gm_dragon_projectile_intercept(
                &r->dragon,sx,sy,sz,target_x,target_y,target_z,
                &dragon_target,&dragon_dist);
            int entity_hit=0;
            double entity_dist=0.0;
            if(hit_player){entity_hit=1;entity_dist=player_dist;}
            if(hit_mob&&(!entity_hit||mob_dist<entity_dist)){
                entity_hit=2;entity_dist=mob_dist;
            }
            if(hit_dragon&&(!entity_hit||dragon_dist<entity_dist))
                entity_hit=3;
            if(entity_hit==1){
                int hit=gm_mobs_attack_player(&r->mobs,
                    (struct PvStats *)&r->vitals,&r->player.inv,5.0f,0);
                r->player.health=r->vitals.health;
                if(hit&&r->player_fire_ticks<5*20)
                    r->player_fire_ticks=5*20;
                p->active=0;
            }else if(entity_hit==2){
                (void)gm_mobs_small_fireball_hit(
                    &r->mobs,mob_slot,5.0f,&r->entities);
                p->active=0;
            }else if(entity_hit==3){
                GmDragonCrystalHit crystal_hit;
                int impact=gm_dragon_small_fireball_hit(
                    &r->dragon,dragon_target,&crystal_hit);
                p->active=0;
                if(impact==2){
                    runtime_explode_with_rays(
                        r,crystal_hit.x,crystal_hit.y,crystal_hit.z,6.0F,1);
                    gm_dragon_crystal_destroyed(
                        &r->dragon,crystal_hit.index,0,1);
                }
            }else if(hit_block){
                int fx=block_hit.bx+side_dx[block_hit.side];
                int fy=block_hit.by+side_dy[block_hit.side];
                int fz=block_hit.bz+side_dz[block_hit.side];
                if((!p->shooting_living||r->mob_griefing)&&
                   gm_world_block(r->world,fx,fy,fz)==0)
                    (void)gm_runtime_set_block(r,fx,fy,fz,51,0);
                p->active=0;
            }
            /* EntityFireball still completes its move after onImpact calls
             * setDead; keeping the position update here also preserves exact
             * no-impact trajectories. */
            p->x=ex;p->y=ey;p->z=ez;
            {
                float horizontal = (float)sqrt(
                    p->vx * p->vx + p->vz * p->vz);
                float target_yaw = (float)(
                    runtime_java_math_atan2(p->vz, p->vx)
                    * (180.0 / MC_PI)) + 90.0F;
                float target_pitch = (float)(
                    runtime_java_math_atan2((double)horizontal, p->vy)
                    * (180.0 / MC_PI)) - 90.0F;
                float previous_yaw = p->yaw;
                float previous_pitch = p->pitch;
                while (target_pitch - previous_pitch < -180.0F)
                    previous_pitch -= 360.0F;
                while (target_pitch - previous_pitch >= 180.0F)
                    previous_pitch += 360.0F;
                while (target_yaw - previous_yaw < -180.0F)
                    previous_yaw -= 360.0F;
                while (target_yaw - previous_yaw >= 180.0F)
                    previous_yaw += 360.0F;
                p->pitch = previous_pitch
                    + (target_pitch - previous_pitch) * 0.2F;
                p->yaw = previous_yaw
                    + (target_yaw - previous_yaw) * 0.2F;
            }
            ++p->age;
            if(!p->active||p->age>1200){p->active=0;continue;}
            p->vx=(p->vx+p->ax)*(double)0.95F;
            p->vy=(p->vy+p->ay)*(double)0.95F;
            p->vz=(p->vz+p->az)*(double)0.95F;
            continue;
        }
        double speed = sqrt(p->vx*p->vx + p->vy*p->vy + p->vz*p->vz);
        int steps = (int)ceil(speed / 0.25);
        if (steps < 1) steps = 1;
        float damage = (float)(speed * 2.0);
        for (int s = 0; s < steps && p->active; ++s) {
            double old_x=p->x,old_y=p->y,old_z=p->z;
            p->x += p->vx / steps; p->y += p->vy / steps; p->z += p->vz / steps;
            int block=gm_world_block(r->world,(int)floor(p->x),(int)floor(p->y),(int)floor(p->z));
            if (p->type == 1 || p->type == 2)
                runtime_redstone_arrow_collisions(r, p);
            if(p->type==1){
                GmDragonCrystalHit crystal_hit;
                int blocked=block && block != 77 && block != 143;
                int dragon_hit=0;
                /* Preserve the pre-existing block-first short circuit: an
                 * arrow entering a solid voxel cannot damage a dragon part
                 * or crystal behind that block. */
                if(!blocked)dragon_hit=gm_dragon_damage_near(
                    &r->dragon,p->x,p->y,p->z,0.75,damage,&crystal_hit);
                if(dragon_hit==2){
                    runtime_explode_with_rays(
                        r,crystal_hit.x,crystal_hit.y,crystal_hit.z,6.0F,1);
                    gm_dragon_crystal_destroyed(
                        &r->dragon,crystal_hit.index,1,1);
                }
                if(blocked||dragon_hit||
                   gm_mobs_damage_near(&r->mobs,p->x,p->y,p->z,0.75,damage,&r->entities))p->active=0;
            }else{
                GmPlayerView v;gm_runtime_view(r,&v);
                double dx=p->x-v.x,dy=p->y-(v.y+0.9),dz=p->z-v.z;
                if(dx*dx+dy*dy+dz*dz<=0.75*0.75){
                    float dmg=p->type==5?6.0f:(p->type==3?5.0f:4.0f);
                    int hit=gm_mobs_attack_player(&r->mobs,
                        (struct PvStats *)&r->vitals, &r->player.inv,
                        dmg, 0);
                    r->player.health=r->vitals.health;
                    if(hit&&p->type==3)r->player_fire_ticks=5*20;
                    if(p->type==5)runtime_explode(r,p->x,p->y,p->z,1.0f);
                    p->active=0;
                }else if(block
                        && !((p->type == 2)
                            && (block == 77 || block == 143))){
                    if(p->type==3){
                        /* RayTraceResult.sideHit.offset is the air voxel the
                         * projectile crossed immediately before the solid. */
                        int fx=(int)floor(old_x),fy=(int)floor(old_y),
                            fz=(int)floor(old_z);
                        if(gm_world_block(r->world,fx,fy,fz)==0)
                            (void)gm_runtime_set_block(r,fx,fy,fz,51,0);
                    }else if(p->type==5){
                        runtime_explode(r,p->x,p->y,p->z,1.0f);
                    }
                    p->active=0;
                }
            }
        }
        ++p->age;
        if (!p->active || p->age > 1200) { p->active = 0; continue; }
        if(p->type==3){
            /* EntityFireball.onUpdate: move by current motion, add constant
             * acceleration, then apply the 0.95 motion factor. */
            p->vx=(p->vx+p->ax)*0.95;
            p->vy=(p->vy+p->ay)*0.95;
            p->vz=(p->vz+p->az)*0.95;
        }else{
            if(p->type!=5)p->vy -= 0.05;
            p->vx *= 0.99;p->vy *= 0.99;p->vz *= 0.99;
        }
    }
}

static int scheduled_tick_cmp(
        const GmRuntimeScheduledTick *a,
        const GmRuntimeScheduledTick *b) {
    if (a->time != b->time) return a->time < b->time ? -1 : 1;
    if (a->priority != b->priority)
        return a->priority < b->priority ? -1 : 1;
    if (a->order != b->order) return a->order < b->order ? -1 : 1;
    return 0;
}

static int runtime_schedule_tick_insert(
        GmRuntime *r, int x, int y, int z, int block, long long time,
        int priority, long long order) {
    for (int i = 0; i < r->scheduled_tick_count; ++i) {
        const GmRuntimeScheduledTick *entry = &r->scheduled_ticks[i];
        if (entry->x == x && entry->y == y && entry->z == z
                && entry->block == block)
            return 1;
    }
    if (r->scheduled_tick_count >= GM_RUNTIME_SCHEDULED_TICKS)
        return 0;
    GmRuntimeScheduledTick entry = {
        x, y, z, block, time, priority, order
    };
    int at = r->scheduled_tick_count;
    while (at > 0
            && scheduled_tick_cmp(&entry, &r->scheduled_ticks[at - 1]) < 0) {
        r->scheduled_ticks[at] = r->scheduled_ticks[at - 1];
        --at;
    }
    r->scheduled_ticks[at] = entry;
    ++r->scheduled_tick_count;
    if (order >= r->scheduled_tick_next_order)
        r->scheduled_tick_next_order = order + 1;
    return 1;
}

static int runtime_is_water(int id) {
    return id == 8 || id == 9;
}

static int runtime_water_depth(const GmRuntime *r, int x, int y, int z) {
    return runtime_is_water(gm_world_block(r->world, x, y, z))
        ? gm_world_meta(r->world, x, y, z) : -1;
}

/* The promoted exact slice deliberately restricts itself to a flat stone
 * plane. That makes the material predicate exact without pretending doors,
 * signs, ladders, portals, replaceable plants, or modded materials are ported. */
static int runtime_water_blocked(const GmRuntime *r, int x, int y, int z) {
    int id = gm_world_block(r->world, x, y, z);
    return id != 0 && !runtime_is_water(id);
}

static int runtime_water_can_flow_into(
        const GmRuntime *r, int x, int y, int z) {
    int id = gm_world_block(r->world, x, y, z);
    return !runtime_is_water(id) && id != 10 && id != 11
        && !runtime_water_blocked(r, x, y, z);
}

static void runtime_water_schedule(
        GmRuntime *r, int x, int y, int z) {
    runtime_schedule_tick_insert(
        r, x, y, z, 8, r->clock.total_time + 5, 0,
        r->scheduled_tick_next_order);
}

/* World.notifyNeighborsOfStateChange order follows EnumFacing.values():
 * DOWN, UP, NORTH, SOUTH, WEST, EAST. Static water's neighborChanged converts
 * it to flowing water with the same level and schedules it five ticks out. */
static void runtime_water_wake_static_neighbors(
        GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    for (int i = 0; i < 6; ++i) {
        int nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if (gm_world_block(r->world, nx, ny, nz) != 9)
            continue;
        int level = gm_world_meta(r->world, nx, ny, nz);
        gm_world_set_block_meta(r->world, nx, ny, nz, 8, level);
        runtime_water_schedule(r, nx, ny, nz);
    }
}

static void runtime_water_set_dynamic_notify(
        GmRuntime *r, int x, int y, int z, int level) {
    gm_world_set_block_meta(r->world, x, y, z, 8, level);
    runtime_water_schedule(r, x, y, z);
    runtime_water_wake_static_neighbors(r, x, y, z);
}

static int runtime_water_slope_distance(
        const GmRuntime *r, int x, int y, int z,
        int distance, int excluded_direction) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dz[4] = {-1, 1, 0, 0};
    int best = 1000;
    for (int direction = 0; direction < 4; ++direction) {
        if (direction == excluded_direction) continue;
        int nx = x + dx[direction], nz = z + dz[direction];
        int depth = runtime_water_depth(r, nx, y, nz);
        if (runtime_water_blocked(r, nx, y, nz)
                || (depth >= 0 && depth == 0))
            continue;
        if (!runtime_water_blocked(r, nx, y - 1, nz))
            return distance;
        if (distance < 4) {
            int opposite = direction ^ 1;
            int candidate = runtime_water_slope_distance(
                r, nx, y, nz, distance + 1, opposite);
            if (candidate < best) best = candidate;
        }
    }
    return best;
}

static unsigned runtime_water_flow_directions(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dz[4] = {-1, 1, 0, 0};
    int best = 1000;
    unsigned selected = 0;
    for (int direction = 0; direction < 4; ++direction) {
        int nx = x + dx[direction], nz = z + dz[direction];
        int depth = runtime_water_depth(r, nx, y, nz);
        if (runtime_water_blocked(r, nx, y, nz)
                || (depth >= 0 && depth == 0))
            continue;
        int cost = !runtime_water_blocked(r, nx, y - 1, nz)
            ? 0 : runtime_water_slope_distance(
                r, nx, y, nz, 1, direction ^ 1);
        if (cost < best) selected = 0;
        if (cost <= best) {
            selected |= 1u << direction;
            best = cost;
        }
    }
    return selected;
}

static void runtime_water_try_flow(
        GmRuntime *r, int x, int y, int z, int level) {
    if (!runtime_water_can_flow_into(r, x, y, z)) return;
    runtime_water_set_dynamic_notify(r, x, y, z, level);
}

/* Line-for-line phase equivalent of 1.11.2 BlockDynamicLiquid.updateTick for
 * water on the promoted flat-stone subset. It preserves in-place mutation,
 * NORTH/SOUTH/WEST/EAST EnumSet order, static-water wakeups, queue dedup, and
 * the five-tick schedule cadence. */
static void runtime_tick_water_flat(
        GmRuntime *r, const GmRuntimeScheduledTick *entry) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dz[4] = {-1, 1, 0, 0};
    int x = entry->x, y = entry->y, z = entry->z;
    int level = gm_world_meta(r->world, x, y, z);
    if (level > 0) {
        int minimum = -100;
        int sources = 0;
        for (int direction = 0; direction < 4; ++direction) {
            int depth = runtime_water_depth(
                r, x + dx[direction], y, z + dz[direction]);
            if (depth < 0) continue;
            if (depth == 0) ++sources;
            if (depth >= 8) depth = 0;
            if (minimum < 0 || depth < minimum) minimum = depth;
        }
        int next_level = minimum + 1;
        if (next_level >= 8 || minimum < 0) next_level = -1;
        int above = runtime_water_depth(r, x, y + 1, z);
        if (above >= 0)
            next_level = above >= 8 ? above : above + 8;
        if (sources >= 2
                && gm_world_block(r->world, x, y - 1, z) == 1)
            next_level = 0;
        if (next_level == level) {
            gm_world_set_block_meta(r->world, x, y, z, 9, level);
        } else {
            level = next_level;
            if (next_level < 0) {
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
                runtime_water_wake_static_neighbors(r, x, y, z);
            } else {
                gm_world_set_block_meta(r->world, x, y, z, 8, next_level);
                runtime_water_schedule(r, x, y, z);
                runtime_water_wake_static_neighbors(r, x, y, z);
            }
        }
    } else {
        gm_world_set_block_meta(r->world, x, y, z, 9, 0);
    }

    if (runtime_water_can_flow_into(r, x, y - 1, z)) {
        runtime_water_try_flow(
            r, x, y - 1, z, level >= 8 ? level : level + 8);
    } else if (level >= 0
            && (level == 0 || runtime_water_blocked(r, x, y - 1, z))) {
        unsigned directions = runtime_water_flow_directions(r, x, y, z);
        int next_level = level >= 8 ? 1 : level + 1;
        if (next_level >= 8) return;
        for (int direction = 0; direction < 4; ++direction)
            if (directions & (1u << direction))
                runtime_water_try_flow(
                    r, x + dx[direction], y, z + dz[direction],
                    next_level);
    }
}

static int runtime_water_basin_at(
        const GmRuntime *r, int x, int floor_y, int z) {
    if (floor_y < 0 || floor_y > 252) return 0;
    for (int dz = -4; dz <= 4; ++dz)
        for (int dx = -4; dx <= 4; ++dx) {
            if (abs(dx) + abs(dz) > 4) continue;
            int lower = gm_world_block(
                r->world, x + dx, floor_y + 1, z + dz);
            int upper = gm_world_block(
                r->world, x + dx, floor_y + 2, z + dz);
            if (gm_world_block(r->world, x + dx, floor_y, z + dz) != 1
                    || (lower != 0 && !runtime_is_water(lower))
                    || (upper != 0 && !runtime_is_water(upper))
                    || gm_world_block(
                        r->world, x + dx, floor_y + 3, z + dz) != 0)
                return 0;
        }
    return 1;
}

/* Exact scheduled-water work is admitted only inside a two-air-layer basin
 * over a flat stone floor. A pending block may occupy the lower layer
 * (ordinary horizontal spread) or the upper layer (one-block downward flow).
 * This keeps every material query in the port's exact air/water/stone subset. */
static int runtime_water_flat_supported(
        const GmRuntime *r, int x, int y, int z) {
    if (y < 1 || y >= 255
            || gm_world_block(r->world, x, y, z) != 8)
        return 0;
    return runtime_water_basin_at(r, x, y - 1, z)
        || runtime_water_basin_at(r, x, y - 2, z);
}

static int runtime_water_enclosed_below_lava_supported(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dz[4] = {-1, 1, 0, 0};
    if (y < 1 || y >= 255
            || gm_world_block(r->world, x, y, z) != 8
            || gm_world_meta(r->world, x, y, z) != 0
            || gm_world_block(r->world, x, y - 1, z) != 1
            || gm_world_block(r->world, x, y + 1, z) != 10
            || gm_world_meta(r->world, x, y + 1, z) != 0)
        return 0;
    for (int direction = 0; direction < 4; ++direction)
        if (gm_world_block(
                r->world, x + dx[direction], y,
                z + dz[direction]) != 1)
            return 0;
    return 1;
}

static int runtime_water_supported(
        const GmRuntime *r, int x, int y, int z) {
    return runtime_water_flat_supported(r, x, y, z)
        || runtime_water_enclosed_below_lava_supported(r, x, y, z);
}

static int runtime_is_lava(int id) {
    return id == 10 || id == 11;
}

static int runtime_lava_depth(const GmRuntime *r, int x, int y, int z) {
    return runtime_is_lava(gm_world_block(r->world, x, y, z))
        ? gm_world_meta(r->world, x, y, z) : -1;
}

static int runtime_lava_blocked(const GmRuntime *r, int x, int y, int z) {
    int id = gm_world_block(r->world, x, y, z);
    return id != 0 && !runtime_is_lava(id);
}

static int runtime_lava_can_flow_into(
        const GmRuntime *r, int x, int y, int z) {
    return gm_world_block(r->world, x, y, z) == 0;
}

static void runtime_lava_schedule(
        GmRuntime *r, int x, int y, int z) {
    runtime_schedule_tick_insert(
        r, x, y, z, 10, r->clock.total_time + 30, 0,
        r->scheduled_tick_next_order);
}

static void runtime_lava_wake_static_neighbors(
        GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    for (int i = 0; i < 6; ++i) {
        int nx = x + dx[i], ny = y + dy[i], nz = z + dz[i];
        if (gm_world_block(r->world, nx, ny, nz) != 11)
            continue;
        int level = gm_world_meta(r->world, nx, ny, nz);
        gm_world_set_block_meta(r->world, nx, ny, nz, 10, level);
        runtime_lava_schedule(r, nx, ny, nz);
    }
}

static void runtime_lava_set_dynamic_notify(
        GmRuntime *r, int x, int y, int z, int level) {
    gm_world_set_block_meta(r->world, x, y, z, 10, level);
    runtime_lava_schedule(r, x, y, z);
    runtime_lava_wake_static_neighbors(r, x, y, z);
}

static int runtime_lava_slope_distance(
        const GmRuntime *r, int x, int y, int z,
        int distance, int excluded_direction) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dz[4] = {-1, 1, 0, 0};
    int best = 1000;
    for (int direction = 0; direction < 4; ++direction) {
        if (direction == excluded_direction) continue;
        int nx = x + dx[direction], nz = z + dz[direction];
        int depth = runtime_lava_depth(r, nx, y, nz);
        if (runtime_lava_blocked(r, nx, y, nz)
                || (depth >= 0 && depth == 0))
            continue;
        if (!runtime_lava_blocked(r, nx, y - 1, nz))
            return distance;
        if (distance < 2) {
            int candidate = runtime_lava_slope_distance(
                r, nx, y, nz, distance + 1, direction ^ 1);
            if (candidate < best) best = candidate;
        }
    }
    return best;
}

static unsigned runtime_lava_flow_directions(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dz[4] = {-1, 1, 0, 0};
    int best = 1000;
    unsigned selected = 0;
    for (int direction = 0; direction < 4; ++direction) {
        int nx = x + dx[direction], nz = z + dz[direction];
        int depth = runtime_lava_depth(r, nx, y, nz);
        if (runtime_lava_blocked(r, nx, y, nz)
                || (depth >= 0 && depth == 0))
            continue;
        int cost = !runtime_lava_blocked(r, nx, y - 1, nz)
            ? 0 : runtime_lava_slope_distance(
                r, nx, y, nz, 1, direction ^ 1);
        if (cost < best) selected = 0;
        if (cost <= best) {
            selected |= 1u << direction;
            best = cost;
        }
    }
    return selected;
}

static void runtime_lava_try_flow(
        GmRuntime *r, int x, int y, int z, int level) {
    if (!runtime_lava_can_flow_into(r, x, y, z)) return;
    runtime_lava_set_dynamic_notify(r, x, y, z, level);
}

/* Deterministic flat-plane trajectory from a level-0 source. Lava uses decay
 * two, slope radius two, and a 30-tick Overworld cadence. In this promoted
 * source expansion every non-source child recomputes its existing level, so
 * BlockDynamicLiquid's random 4x reschedule branch is never entered. */
static void runtime_tick_lava_flat(
        GmRuntime *r, const GmRuntimeScheduledTick *entry) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dz[4] = {-1, 1, 0, 0};
    int x = entry->x, y = entry->y, z = entry->z;
    int level = gm_world_meta(r->world, x, y, z);
    if (level > 0) {
        int minimum = -100;
        int sources = 0;
        for (int direction = 0; direction < 4; ++direction) {
            int depth = runtime_lava_depth(
                r, x + dx[direction], y, z + dz[direction]);
            if (depth < 0) continue;
            if (depth == 0) ++sources;
            if (depth >= 8) depth = 0;
            if (minimum < 0 || depth < minimum) minimum = depth;
        }
        int next_level = minimum + 2;
        if (next_level >= 8 || minimum < 0) next_level = -1;
        int above = runtime_lava_depth(r, x, y + 1, z);
        if (above >= 0)
            next_level = above >= 8 ? above : above + 8;
        if (sources >= 2
                && gm_world_block(r->world, x, y - 1, z) == 1)
            next_level = 0;
        if (next_level == level) {
            gm_world_set_block_meta(r->world, x, y, z, 11, level);
        } else {
            /* A next_level > level would consume rand.nextInt(4) to select
             * 30 vs 120 ticks. The admitted source-on-open-plane trajectory
             * cannot reach that branch before its finite spread completes. */
            if (level < 8 && next_level < 8 && next_level > level)
                return;
            level = next_level;
            if (next_level < 0) {
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
                runtime_lava_wake_static_neighbors(r, x, y, z);
            } else {
                gm_world_set_block_meta(r->world, x, y, z, 10, next_level);
                runtime_lava_schedule(r, x, y, z);
                runtime_lava_wake_static_neighbors(r, x, y, z);
            }
        }
    } else {
        gm_world_set_block_meta(r->world, x, y, z, 11, 0);
    }

    if (runtime_lava_can_flow_into(r, x, y - 1, z)) {
        runtime_lava_try_flow(
            r, x, y - 1, z, level >= 8 ? level : level + 8);
    } else if (level >= 0
            && (level == 0 || runtime_lava_blocked(r, x, y - 1, z))) {
        unsigned directions = runtime_lava_flow_directions(r, x, y, z);
        int next_level = level >= 8 ? 1 : level + 2;
        if (next_level >= 8) return;
        for (int direction = 0; direction < 4; ++direction)
            if (directions & (1u << direction))
                runtime_lava_try_flow(
                    r, x + dx[direction], y, z + dz[direction],
                    next_level);
    }
}

static int runtime_lava_flat_material_supported(
        const GmRuntime *r, int x, int y, int z) {
    if (y < 1 || y >= 255
            || gm_world_block(r->world, x, y, z) != 10)
        return 0;
    for (int dz = -4; dz <= 4; ++dz)
        for (int dx = -4; dx <= 4; ++dx) {
            if (abs(dx) + abs(dz) > 4) continue;
            int middle = gm_world_block(r->world, x + dx, y, z + dz);
            if (gm_world_block(r->world, x + dx, y - 1, z + dz) != 1
                    || (middle != 0 && !runtime_is_lava(middle))
                    || gm_world_block(r->world, x + dx, y + 1, z + dz) != 0)
                return 0;
        }
    return 1;
}

static int runtime_lava_source_flat_supported(
        const GmRuntime *r, int x, int y, int z) {
    return runtime_lava_flat_material_supported(r, x, y, z)
        && gm_world_meta(r->world, x, y, z) == 0;
}

static int runtime_lava_above_enclosed_water_supported(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dz[4] = {-1, 1, 0, 0};
    if (y < 2 || y >= 255
            || gm_world_block(r->world, x, y, z) != 10
            || gm_world_meta(r->world, x, y, z) != 0
            || !runtime_is_water(
                gm_world_block(r->world, x, y - 1, z))
            || gm_world_meta(r->world, x, y - 1, z) != 0
            || gm_world_block(r->world, x, y - 2, z) != 1)
        return 0;
    for (int direction = 0; direction < 4; ++direction)
        if (gm_world_block(
                r->world, x + dx[direction], y - 1,
                z + dz[direction]) != 1)
            return 0;
    return 1;
}

static void runtime_tick_lava_down_into_water(
        GmRuntime *r, const GmRuntimeScheduledTick *entry) {
    int x = entry->x, y = entry->y, z = entry->z;
    /* Source placement is flag 2. Replacing the water below with stone uses
     * flag 3; its UP notification converts the just-static source back to
     * dynamic lava and schedules it 30 ticks later. */
    gm_world_set_block_meta(r->world, x, y, z, 11, 0);
    gm_world_set_block_meta(r->world, x, y - 1, z, 1, 0);
    runtime_lava_wake_static_neighbors(r, x, y - 1, z);
}

static int runtime_falling_can_fall_through(int block) {
    return block == 0 || block == 8 || block == 9
        || block == 10 || block == 11 || block == 51;
}

static void runtime_falling_set_position(
        GmRuntimeFallingBlock *falling, double x, double y, double z) {
    const double half_width = (double)(0.98f / 2.0f);
    const double height = (double)0.98f;
    falling->x = x;
    falling->y = y;
    falling->z = z;
    falling->bb_min_x = x - half_width;
    falling->bb_min_y = y;
    falling->bb_min_z = z - half_width;
    falling->bb_max_x = x + half_width;
    falling->bb_max_y = y + height;
    falling->bb_max_z = z + half_width;
    falling->bounding_box_valid = 1;
}

/* Exact falling-block proof slice: sand, gravel, dragon egg, or a canonical
 * anvil above an admissible initial cell and supported by stone or a top slab,
 * plus failed placement on a bottom stone slab, an enchanting table, soul
 * sand, grass path, carpet, or snow layers. Pressure plates and tripwire have null
 * collision boxes, so an
 * already-active falling entity passes through them while still invoking block
 * collisions. This helper remains the cold callback proof fence and records
 * the centered expected surface; active entities use swept AABB motion and may
 * land in a different cell. Other nonreplaceable shapes and fixed-pool
 * pressure remain outside this promotion. */
static double runtime_falling_block_landing_y(
        const GmRuntime *r, int x, int y, int z, int falling_block,
        int *drop_on_land) {
    if (drop_on_land)
        *drop_on_land = 0;
    if (!r || y < 2 || y > 255
            || (falling_block != 12 && falling_block != 13
                && falling_block != 122 && falling_block != 145)
            || gm_world_block(r->world, x, y, z) != falling_block
            || (falling_block == 145
                ? gm_world_meta(r->world, x, y, z) < 0
                    || gm_world_meta(r->world, x, y, z) > 11
                : gm_world_meta(r->world, x, y, z) != 0)
            || (falling_block == 122
                ? gm_world_block(r->world, x, y - 1, z) != 0
                : !runtime_falling_can_fall_through(
                    gm_world_block(r->world, x, y - 1, z))))
        return -1.0;
    for (int support_y = y - 2; support_y >= 1; --support_y) {
        int block = gm_world_block(r->world, x, support_y, z);
        if (runtime_falling_can_fall_through(block)
                || block == 70 || block == 72
                || block == 132 || block == 147 || block == 148)
            continue;
        if (block == 1)
            return (double)(support_y + 1);
        if (block == 44) {
            if ((gm_world_meta(r->world, x, support_y, z) & 8) != 0)
                return (double)(support_y + 1);
            if (gm_world_meta(r->world, x, support_y, z) == 0) {
                if (drop_on_land)
                    *drop_on_land = 1;
                return (double)support_y + 0.5;
            }
        }
        if (block == 60) {
            int meta = gm_world_meta(r->world, x, support_y, z);
            if (meta >= 0 && meta <= 7) {
                if (drop_on_land)
                    *drop_on_land = 1;
                return (double)support_y + 0.9375;
            }
        }
        if (block == 78) {
            int meta = gm_world_meta(r->world, x, support_y, z);
            if (meta >= 0 && meta <= 7) {
                if (drop_on_land)
                    *drop_on_land = meta != 0;
                /* BlockSnow collision is one layer shorter than its outline.
                 * The one-layer state has no collision and is replaceable. */
                return (double)support_y + (double)meta * 0.125;
            }
        }
        if (block == 92
                && gm_world_meta(r->world, x, support_y, z) == 0) {
            if (drop_on_land)
                *drop_on_land = 1;
            return (double)support_y + 0.5;
        }
        if (block == 88
                && gm_world_meta(r->world, x, support_y, z) == 0) {
            if (drop_on_land)
                *drop_on_land = 1;
            return (double)support_y + 0.875;
        }
        if (block == 116
                && gm_world_meta(r->world, x, support_y, z) == 0) {
            if (drop_on_land)
                *drop_on_land = 1;
            return (double)support_y + 0.75;
        }
        if (block == 171
                && gm_world_meta(r->world, x, support_y, z) == 0) {
            if (drop_on_land)
                *drop_on_land = 1;
            return (double)support_y + 0.0625;
        }
        if (block == 208
                && gm_world_meta(r->world, x, support_y, z) == 0) {
            if (drop_on_land)
                *drop_on_land = 1;
            return (double)support_y + 0.9375;
        }
        return -1.0;
    }
    return -1.0;
}

static int runtime_spawn_falling_block(
        GmRuntime *r, const GmRuntimeScheduledTick *entry) {
    int drop_on_land = 0;
    double landing_y = runtime_falling_block_landing_y(
        r, entry->x, entry->y, entry->z, entry->block, &drop_on_land);
    if (landing_y < 0.0
            || r->falling_block_count >= GM_RUNTIME_FALLING_BLOCKS)
        return 0;
    for (int i = 0; i < GM_RUNTIME_FALLING_BLOCKS; ++i) {
        GmRuntimeFallingBlock *falling = &r->falling_blocks[i];
        if (falling->active)
            continue;
        memset(falling, 0, sizeof *falling);
        falling->active = 1;
        falling->eid = r->next_entity_id++;
        falling->block = entry->block;
        falling->meta = gm_world_meta(
            r->world, entry->x, entry->y, entry->z);
        falling->landing_y = landing_y;
        falling->drop_on_land = drop_on_land;
        falling->should_drop_item = 1;
        falling->hurt_entities = entry->block == 145;
        if (r->next_falling_random_valid) {
            falling->random_seed48 = r->next_falling_random_seed48;
            r->next_falling_random_valid = 0;
        } else {
            JavaRandom random;
            jrand_set(&random, runtime_entity_constructor_seed(
                r, falling->eid, UINT64_C(0x46414C4C494E4742)));
            falling->random_seed48 = random.seed;
        }
        falling->origin_x = entry->x;
        falling->origin_y = entry->y;
        falling->origin_z = entry->z;
        runtime_falling_set_position(
            falling, (double)entry->x + 0.5,
            (double)entry->y + (double)((1.0f - 0.98f) / 2.0f),
            (double)entry->z + 0.5);
        ++r->falling_block_count;
        return 1;
    }
    return 0;
}

/* BlockFalling.checkFallable's synchronous worldgen branch. It removes the
 * source, scans through AIR/WATER/LAVA/FIRE while Y remains above zero, and
 * restores the original full blockstate immediately above the first blocker.
 * No EntityFallingBlock constructor, cursor, impact hook, or item path runs. */
static int runtime_fall_block_instantly(
        GmRuntime *r, const GmRuntimeScheduledTick *entry) {
    int x = entry->x, y = entry->y, z = entry->z;
    int block = gm_world_block(r->world, x, y, z);
    int meta = gm_world_meta(r->world, x, y, z);
    if (block != entry->block || y < 0
            || !runtime_falling_can_fall_through(
                gm_world_block(r->world, x, y - 1, z)))
        return 1;
    if (!gm_runtime_set_block(r, x, y, z, 0, 0))
        return 0;
    int stop_y = y - 1;
    while (stop_y > 0
            && runtime_falling_can_fall_through(
                gm_world_block(r->world, x, stop_y, z)))
        --stop_y;
    if (stop_y <= 0)
        return 1;
    int placed_y = stop_y + 1;
    if (!gm_runtime_set_block(r, x, placed_y, z, block, meta))
        return 0;
    if (block == 12 || block == 13)
        runtime_schedule_tick_insert(
            r, x, placed_y, z, block,
            r->clock.total_time + 2, 0,
            r->scheduled_tick_next_order);
    return 1;
}

/* Vanilla 1.11.2 BlockFire.init tables. */
static int runtime_fire_flammability(int id) {
    switch (id) {
    case 5:
    case 125: case 126:
    case 107: case 183: case 184: case 185: case 186: case 187:
    case 85: case 188: case 189: case 190: case 191: case 192:
    case 53: case 134: case 135: case 136: case 163: case 164:
    case 47: case 170: case 171:
        return 20;
    case 17: case 162: case 173:
        return 5;
    case 18: case 161: case 35:
        return 60;
    case 46: case 31: case 175: case 37: case 38: case 32: case 106:
        return 100;
    default:
        return 0;
    }
}

static int runtime_fire_encouragement(int id) {
    switch (id) {
    case 5:
    case 125: case 126:
    case 107: case 183: case 184: case 185: case 186: case 187:
    case 85: case 188: case 189: case 190: case 191: case 192:
    case 53: case 134: case 135: case 136: case 163: case 164:
    case 17: case 162: case 173:
        return 5;
    case 18: case 161: case 35: case 47:
        return 30;
    case 46: case 106:
        return 15;
    case 31: case 175: case 37: case 38: case 32: case 170: case 171:
        return 60;
    default:
        return 0;
    }
}

static int runtime_fire_fully_opaque(int id) {
    BptProps props;
    if (id == 0)
        return 0;
    props = mc_bpt_props(id);
    return (props.flags & BF_SOLID) != 0
        && (props.flags & BF_LIQUID) == 0
        && props.light_opacity >= 255;
}

/* EnumFacing.values() order is DOWN, UP, NORTH, SOUTH, WEST, EAST. The
 * predicate has no side-sensitive blocks in this proof slice, but retaining
 * that order documents the Java traversal used by the exact port. */
static int runtime_fire_neighbor_catches(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    for (int face = 0; face < 6; ++face)
        if (runtime_fire_flammability(gm_world_block(
                r->world, x + dx[face], y + dy[face],
                z + dz[face])) > 0)
            return 1;
    return 0;
}

static int runtime_fire_neighbor_encouragement(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int best = 0;
    if (gm_world_block(r->world, x, y, z) != 0)
        return 0;
    for (int face = 0; face < 6; ++face) {
        int encouragement = runtime_fire_encouragement(gm_world_block(
            r->world, x + dx[face], y + dy[face], z + dz[face]));
        if (encouragement > best)
            best = encouragement;
    }
    return best;
}

/* Every valid 2..21 by 3..21 portal containing this fire has obsidian below
 * its interior column and on one horizontal axis at this height. Keep the
 * 32-cubed verified matcher off ordinary fire additions without narrowing
 * BlockPortal.Size's legal dimensions. */
static int runtime_fire_may_open_portal(
        const GmRuntime *r, int x, int y, int z) {
    int floor = 0;
    int side = 0;
    for (int distance = 1; distance <= 21 && y - distance >= 0; ++distance)
        if (gm_world_block(r->world, x, y - distance, z) == 49) {
            floor = 1;
            break;
        }
    for (int distance = 1; distance <= 21; ++distance)
        if (gm_world_block(r->world, x - distance, y, z) == 49
                || gm_world_block(r->world, x + distance, y, z) == 49
                || gm_world_block(r->world, x, y, z - distance) == 49
                || gm_world_block(r->world, x, y, z + distance) == 49) {
            side = 1;
            break;
        }
    return floor && side;
}

static void runtime_fire_on_added(
        GmRuntime *r, int x, int y, int z) {
    if (r->dimension <= 0
            && runtime_fire_may_open_portal(r, x, y, z)
            && gm_portal_ignite(r->world, x, y, z) > 0)
        return;
    if (!runtime_fire_fully_opaque(
            gm_world_block(r->world, x, y - 1, z))
            && !runtime_fire_neighbor_catches(r, x, y, z)) {
        gm_world_set_block_meta(r->world, x, y, z, 0, 0);
        return;
    }
    runtime_schedule_tick_insert(
        r, x, y, z, 51,
        r->clock.total_time + 30
            + runtime_java_random_next_int(r, 10),
        0, r->scheduled_tick_next_order);
}

/* BlockFire.tryCatchFire always consumes nextInt(chance), even when the
 * target is not flammable. A wet successful target burns to air without the
 * age or child-schedule draws. */
static void runtime_fire_try_catch(
        GmRuntime *r, int x, int y, int z, int chance, int age,
        int raining_at_target) {
    int original_id = gm_world_block(r->world, x, y, z);
    int flammability = runtime_fire_flammability(original_id);
    if (runtime_java_random_next_int(r, chance) >= flammability)
        return;
    if (runtime_java_random_next_int(r, age + 10) < 5
            && !raining_at_target) {
        int next_age = age + runtime_java_random_next_int(r, 5) / 4;
        if (next_age > 15)
            next_age = 15;
        gm_world_set_block_meta(r->world, x, y, z, 51, next_age);
        runtime_fire_on_added(r, x, y, z);
    } else {
        gm_world_set_block_meta(r->world, x, y, z, 0, 0);
    }
    /* BlockFire retains the pre-replacement state, then invokes TNT's
     * EXPLODE destruction hook after writing fire or air. The proof guard
     * reserves enough fixed entity slots before entering this callback. */
    if (original_id == 46)
        (void)runtime_tnt_prime(r, x, y, z);
}

/* Exact dry, NORMAL-difficulty BlockFire.updateTick body. This
 * keeps the original AGE value for spread math after writing the independently
 * randomized source metadata, matching Java's immutable IBlockState local. */
static void runtime_tick_fire_dry_normal(
        GmRuntime *r, int x, int y, int z, int high_humidity,
        int raining_at_east, int can_die_west_candidate) {
    int age = gm_world_meta(r->world, x, y, z);
    int below = gm_world_block(r->world, x, y - 1, z);
    int fire_source =
        below == 87 || (r->dimension == 1 && below == 7);

    if (!runtime_fire_fully_opaque(below)
            && !runtime_fire_neighbor_catches(r, x, y, z))
        gm_world_set_block_meta(r->world, x, y, z, 0, 0);

    if (age < 15) {
        int written_age = age
            + runtime_java_random_next_int(r, 3) / 2;
        gm_world_set_block_meta(r->world, x, y, z, 51, written_age);
    }
    runtime_schedule_tick_insert(
        r, x, y, z, 51,
        r->clock.total_time + 30
            + runtime_java_random_next_int(r, 10),
        0, r->scheduled_tick_next_order);

    if (!fire_source) {
        if (!runtime_fire_neighbor_catches(r, x, y, z)) {
            if (!runtime_fire_fully_opaque(below) || age > 3)
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
            return;
        }
        if (runtime_fire_flammability(below) == 0
                && age == 15
                && runtime_java_random_next_int(r, 4) == 0) {
            gm_world_set_block_meta(r->world, x, y, z, 0, 0);
            return;
        }
    }

    runtime_fire_try_catch(
        r, x + 1, y, z, high_humidity ? 250 : 300, age,
        raining_at_east);
    runtime_fire_try_catch(
        r, x - 1, y, z, high_humidity ? 250 : 300, age, 0);
    runtime_fire_try_catch(
        r, x, y - 1, z, high_humidity ? 200 : 250, age, 0);
    runtime_fire_try_catch(
        r, x, y + 1, z, high_humidity ? 200 : 250, age, 0);
    runtime_fire_try_catch(
        r, x, y, z - 1, high_humidity ? 250 : 300, age, 0);
    runtime_fire_try_catch(
        r, x, y, z + 1, high_humidity ? 250 : 300, age, 0);

    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            for (int dy = -1; dy <= 4; ++dy) {
                int chance = 100;
                int encouragement;
                int threshold;
                int next_age;
                if (dx == 0 && dy == 0 && dz == 0)
                    continue;
                if (dy > 1)
                    chance += (dy - 1) * 100;
                encouragement = runtime_fire_neighbor_encouragement(
                    r, x + dx, y + dy, z + dz);
                if (encouragement <= 0)
                    continue;
                threshold = (encouragement + 40 + 2 * 7) / (age + 30);
                if (high_humidity)
                    threshold /= 2;
                if (threshold <= 0
                        || runtime_java_random_next_int(r, chance) > threshold)
                    continue;
                if (can_die_west_candidate
                        && dx == -1 && dy == 0 && dz == 0)
                    continue;
                next_age = age
                    + runtime_java_random_next_int(r, 5) / 4;
                if (next_age > 15)
                    next_age = 15;
                gm_world_set_block_meta(
                    r->world, x + dx, y + dy, z + dz, 51, next_age);
                runtime_fire_on_added(
                    r, x + dx, y + dy, z + dz);
            }
        }
    }
}

/* Exact precipitation subsets transported by a Java state capsule: exposed
 * age-15 fire on stone with an inert neighborhood, or age-zero fire on
 * netherrack with one east direct target or one west volumetric candidate.
 * Java's canDie/isRainingAt results include biome precipitation and sky
 * visibility, so the capsule transports those predicates rather than
 * inferring them from C worldgen. */
static int runtime_fire_rain_proof_supported(
        const GmRuntime *r, int x, int y, int z) {
    static const int face_dx[6] = {0, 0, 0, 0, -1, 1};
    static const int face_dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int face_dz[6] = {0, 0, -1, 1, 0, 0};
    int direct_target_proof;
    int volume_west_proof;
    if (!r || r->dimension != 0 || !r->weather_enabled
            || !r->clock.raining
            || !r->fire_rain_context_valid || !r->fire_rain_can_die
            || r->fire_rain_x != x || r->fire_rain_y != y
            || r->fire_rain_z != z || y < 2 || y > 250
            || gm_world_block(r->world, x, y, z) != 51)
        return 0;
    direct_target_proof =
        gm_world_meta(r->world, x, y, z) == 0
        && gm_world_block(r->world, x, y - 1, z) == 87
        && gm_world_block(r->world, x + 1, y, z) == 31
        && gm_world_block(r->world, x + 1, y + 1, z) == 0
        && (gm_world_block(r->world, x + 1, y + 2, z) == 0
            || gm_world_block(r->world, x + 1, y + 2, z) == 1);
    volume_west_proof =
        gm_world_meta(r->world, x, y, z) == 0
        && gm_world_block(r->world, x, y - 1, z) == 87
        && gm_world_block(r->world, x - 1, y, z) == 0
        && gm_world_block(r->world, x - 2, y, z) == 171
        && gm_world_block(r->world, x - 1, y + 1, z) == 0
        && gm_world_block(r->world, x - 2, y + 1, z) == 0
        && gm_world_block(r->world, x, y + 1, z) == 0
        && gm_world_block(r->world, x - 1, y + 1, z - 1) == 0
        && gm_world_block(r->world, x - 1, y + 1, z + 1) == 0
        && gm_world_block(r->world, x - 1, y + 2, z)
            == gm_world_block(r->world, x - 2, y + 2, z)
        && gm_world_block(r->world, x - 1, y + 2, z)
            == gm_world_block(r->world, x, y + 2, z)
        && gm_world_block(r->world, x - 1, y + 2, z)
            == gm_world_block(r->world, x - 1, y + 2, z - 1)
        && gm_world_block(r->world, x - 1, y + 2, z)
            == gm_world_block(r->world, x - 1, y + 2, z + 1)
        && (gm_world_block(r->world, x - 1, y + 2, z) == 0
            || gm_world_block(r->world, x - 1, y + 2, z) == 1);
    if (!direct_target_proof && !volume_west_proof
            && (gm_world_meta(r->world, x, y, z) != 15
                || gm_world_block(r->world, x, y - 1, z) != 1))
        return 0;
    for (int dy = -1; dy <= 4; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                int id = gm_world_block(
                    r->world, x + dx, y + dy, z + dz);
                if (dx == 0 && dy == 0 && dz == 0) {
                    if (id != 51)
                        return 0;
                } else if (direct_target_proof || volume_west_proof) {
                    if ((id == 31 || id == 171)
                            && !(direct_target_proof
                                && id == 31
                                && dx == 1 && dy == 0 && dz == 0))
                        return 0;
                    if (id == 87
                            && !(dx == 0 && dy == -1 && dz == 0))
                        return 0;
                    if (id != 0 && id != 1 && id != 31 && id != 87
                            && id != 171)
                        return 0;
                } else if (id != 0 && id != 1) {
                    return 0;
                }
                if (id != 0)
                    continue;
                for (int face = 0; face < 6; ++face) {
                    int neighbor = gm_world_block(
                        r->world,
                        x + dx + face_dx[face],
                        y + dy + face_dy[face],
                        z + dz + face_dz[face]);
                    if (neighbor != 0 && neighbor != 1 && neighbor != 51
                            && !((direct_target_proof || volume_west_proof)
                                && (neighbor == 31 || neighbor == 87
                                    || neighbor == 171)))
                        return 0;
                }
            }
    return 1;
}

static void runtime_tick_fire(
        GmRuntime *r, int x, int y, int z) {
    int high_humidity = r->fire_humidity_context_valid
        && r->fire_humidity_x == x && r->fire_humidity_y == y
        && r->fire_humidity_z == z;
    int raining_at_east = r->clock.raining
        && r->fire_rain_context_valid
        && r->fire_rain_x == x && r->fire_rain_y == y
        && r->fire_rain_z == z && r->fire_rain_at_east;
    int can_die_west_candidate = r->clock.raining
        && r->fire_rain_context_valid
        && r->fire_rain_x == x && r->fire_rain_y == y
        && r->fire_rain_z == z
        && r->fire_rain_can_die_west_candidate;
    if (runtime_fire_rain_proof_supported(r, x, y, z)) {
        int age = gm_world_meta(r->world, x, y, z);
        int below = gm_world_block(r->world, x, y - 1, z);
        int fire_source =
            below == 87 || (r->dimension == 1 && below == 7);
        if (!fire_source) {
            float threshold = 0.2f + (float)age * 0.03f;
            if (runtime_java_random_next_float(r) < threshold) {
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
                return;
            }
        }
        /* The inert age-15 proof has no later encouragement. The netherrack
         * proof skips this source roll and reaches one transported wet-target
         * predicate in the ordinary direct spread path. */
    }
    runtime_tick_fire_dry_normal(
        r, x, y, z, high_humidity, raining_at_east,
        can_die_west_candidate);
}

/* Proof guard shared by capsule restore, direct callbacks, and scheduled
 * dispatch. It deliberately admits only air/stone/planks/logs/bookshelves/
 * wool/grass/hay/TNT/fire/netherrack and dimension-appropriate bedrock around
 * a source on stone, netherrack, or End bedrock; biome humidity and gamerule
 * context are validated by the capsule producer for queued callbacks. */
static int runtime_fire_proof_supported(
        const GmRuntime *r, int x, int y, int z) {
    static const int face_dx[6] = {0, 0, 0, 0, -1, 1};
    static const int face_dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int face_dz[6] = {0, 0, -1, 1, 0, 0};
    int below;
    int direct_tnt = 0;
    if (!r)
        return 0;
    if (r->weather_enabled)
        return runtime_fire_rain_proof_supported(r, x, y, z);
    if ((r->dimension != -1 && r->dimension != 0 && r->dimension != 1)
            || y < 2 || y > 250
            || gm_world_block(r->world, x, y, z) != 51)
        return 0;
    below = gm_world_block(r->world, x, y - 1, z);
    if (below != 1 && below != 87
            && !(r->dimension == 1 && below == 7))
        return 0;
    for (int face = 0; face < 6; ++face) {
        int id = gm_world_block(
            r->world, x + face_dx[face], y + face_dy[face],
            z + face_dz[face]);
        if (id == 46)
            ++direct_tnt;
        if (id != 0 && id != 1 && id != 5 && id != 17 && id != 31
                && id != 35 && id != 46 && id != 47 && id != 51 && id != 87
                && id != 170
                && !(r->dimension == 1 && id == 7))
            return 0;
    }
    if (r->primed_tnt_count + direct_tnt > GM_RUNTIME_PRIMED_TNT)
        return 0;
    for (int dy = -1; dy <= 4; ++dy)
        for (int dz = -1; dz <= 1; ++dz)
            for (int dx = -1; dx <= 1; ++dx) {
                int id;
                if (dx == 0 && dy == 0 && dz == 0)
                    continue;
                id = gm_world_block(
                    r->world, x + dx, y + dy, z + dz);
                if (id != 0)
                    continue;
                for (int face = 0; face < 6; ++face) {
                    int neighbor = gm_world_block(
                        r->world,
                        x + dx + face_dx[face],
                        y + dy + face_dy[face],
                        z + dz + face_dz[face]);
                    if (neighbor != 0 && neighbor != 1
                            && neighbor != 5 && neighbor != 17
                            && neighbor != 31 && neighbor != 35
                            && neighbor != 46 && neighbor != 47
                            && neighbor != 51 && neighbor != 170
                            && neighbor != 87
                            && !(r->dimension == 1 && neighbor == 7))
                        return 0;
                }
            }
    return 1;
}

/* First redstone foundation slice. World.isBlockPowered probes
 * DOWN, UP, NORTH, SOUTH, WEST, EAST. A redstone block, powered lever, and
 * powered stone button provide weak power 15 on every face. More producers
 * are added behind this query rather than teaching each consumer about every
 * component. */
static int runtime_redstone_control_facing(int id, int meta) {
    switch (meta & 7) {
    case 0: return 0; /* DOWN */
    case 1: return 5; /* EAST */
    case 2: return 4; /* WEST */
    case 3: return 3; /* SOUTH */
    case 4: return 2; /* NORTH */
    case 5: return 1; /* UP */
    case 6: return 1; /* lever UP_X; button defaults UP */
    case 7: return id == 69 ? 0 : 1; /* lever DOWN_Z; button defaults UP */
    default: return -1;
    }
}

/* BlockHorizontal metadata uses EnumFacing.HORIZONTALS in S-W-N-E order. */
static int runtime_redstone_repeater_face(int meta) {
    switch (meta & 3) {
    case 0: return 3; /* SOUTH */
    case 1: return 4; /* WEST */
    case 2: return 2; /* NORTH */
    case 3: return 5; /* EAST */
    default: return -1;
    }
}

/* BlockObserver stores its observed direction as the raw EnumFacing index in
 * metadata bits 0..2 and its transient powered state in bit 3. Java's
 * callback accepts only the six real EnumFacing values. */
static int runtime_redstone_observer_facing(int meta) {
    int face = meta & 7;
    return face <= 5 ? face : -1;
}

static int runtime_redstone_observer_power(
        int meta, int queried_face) {
    return (meta & 8) != 0
        && runtime_redstone_observer_facing(meta) == queried_face
        ? 15 : 0;
}

static int runtime_redstone_observer_supported(
        const GmRuntime *r, int x, int y, int z) {
    return gm_world_block(r->world, x, y, z) == 218
        && runtime_redstone_observer_facing(
            gm_world_meta(r->world, x, y, z)) >= 0;
}

static int runtime_redstone_observer_tick_pending(
        const GmRuntime *r, int x, int y, int z) {
    for (int i = 0; i < r->scheduled_tick_count; ++i) {
        const GmRuntimeScheduledTick *entry = &r->scheduled_ticks[i];
        if (entry->x == x && entry->y == y && entry->z == z
                && entry->block == 218)
            return 1;
    }
    return 0;
}

static void runtime_redstone_observer_observed_change(
        GmRuntime *r, int x, int y, int z,
        int changed_x, int changed_y, int changed_z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int meta;
    int face;
    if (!runtime_redstone_observer_supported(r, x, y, z))
        return;
    meta = gm_world_meta(r->world, x, y, z);
    face = runtime_redstone_observer_facing(meta);
    if ((meta & 8) != 0
            || x + dx[face] != changed_x
            || y + dy[face] != changed_y
            || z + dz[face] != changed_z
            || runtime_redstone_observer_tick_pending(r, x, y, z))
        return;
    runtime_schedule_tick_insert(
        r, x, y, z, 218, r->clock.total_time + 2, 0,
        r->scheduled_tick_next_order);
}

/* World.updateObservingBlocksAt order is WEST, EAST, DOWN, UP, NORTH, SOUTH.
 * This is separate from ordinary neighborChanged: observers intentionally
 * ignore that callback and react only on their watched face. */
static void runtime_redstone_update_observers_at(
        GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {-1, 1, 0, 0, 0, 0};
    static const int dy[6] = {0, 0, -1, 1, 0, 0};
    static const int dz[6] = {0, 0, 0, 0, -1, 1};
    for (int i = 0; i < 6; ++i)
        runtime_redstone_observer_observed_change(
            r, x + dx[i], y + dy[i], z + dz[i], x, y, z);
}

/* The ordinary placement state is unpowered. BlockObserver.onBlockAdded
 * starts the same delayed signal even when no watched neighbor has changed.
 * Snapshot loading deliberately bypasses this hook. */
static void runtime_redstone_observer_on_added(
        GmRuntime *r, int x, int y, int z) {
    int meta;
    if (!runtime_redstone_observer_supported(r, x, y, z))
        return;
    meta = gm_world_meta(r->world, x, y, z);
    if ((meta & 8) == 0
            && !runtime_redstone_observer_tick_pending(r, x, y, z))
        runtime_schedule_tick_insert(
            r, x, y, z, 218, r->clock.total_time + 2, 0,
            r->scheduled_tick_next_order);
}

static GmRuntimeComparator *runtime_comparator_find_mut(
        GmRuntime *r, int dimension, int x, int y, int z) {
    for (int i = 0; i < r->comparator_count; ++i) {
        GmRuntimeComparator *entry = &r->comparators[i];
        if (entry->active && entry->dimension == dimension
                && entry->x == x && entry->y == y && entry->z == z)
            return entry;
    }
    return NULL;
}

static const GmRuntimeComparator *runtime_comparator_find(
        const GmRuntime *r, int dimension, int x, int y, int z) {
    for (int i = 0; i < r->comparator_count; ++i) {
        const GmRuntimeComparator *entry = &r->comparators[i];
        if (entry->active && entry->dimension == dimension
                && entry->x == x && entry->y == y && entry->z == z)
            return entry;
    }
    return NULL;
}

static int runtime_comparator_ensure(
        GmRuntime *r, int dimension, int x, int y, int z) {
    GmRuntimeComparator *entry =
        runtime_comparator_find_mut(r, dimension, x, y, z);
    if (entry)
        return 1;
    if (r->comparator_count >= GM_RUNTIME_COMPARATORS)
        return 0;
    entry = &r->comparators[r->comparator_count++];
    *entry = (GmRuntimeComparator){
        1, dimension, x, y, z, 0,
    };
    return 1;
}

static void runtime_comparator_remove(
        GmRuntime *r, int dimension, int x, int y, int z) {
    for (int i = 0; i < r->comparator_count; ++i) {
        GmRuntimeComparator *entry = &r->comparators[i];
        if (!entry->active || entry->dimension != dimension
                || entry->x != x || entry->y != y || entry->z != z)
            continue;
        --r->comparator_count;
        if (i != r->comparator_count)
            r->comparators[i] = r->comparators[r->comparator_count];
        memset(
            &r->comparators[r->comparator_count], 0,
            sizeof r->comparators[r->comparator_count]);
        return;
    }
}

static int runtime_daylight_detector_find(
        const GmRuntime *r, int dimension, int x, int y, int z) {
    for (int i = 0; i < r->daylight_detector_count; ++i) {
        const GmRuntimeDaylightDetector *entry =
            &r->daylight_detectors[i];
        if (entry->dimension == dimension
                && entry->x == x && entry->y == y && entry->z == z)
            return i;
    }
    return -1;
}

static int runtime_daylight_detector_ensure(
        GmRuntime *r, int dimension, int x, int y, int z) {
    if (runtime_daylight_detector_find(r, dimension, x, y, z) >= 0)
        return 1;
    if (r->daylight_detector_count >= GM_RUNTIME_DAYLIGHT_DETECTORS)
        return 0;
    r->daylight_detectors[r->daylight_detector_count++] =
        (GmRuntimeDaylightDetector){dimension, x, y, z};
    return 1;
}

static void runtime_daylight_detector_remove(
        GmRuntime *r, int dimension, int x, int y, int z) {
    int index = runtime_daylight_detector_find(r, dimension, x, y, z);
    if (index < 0)
        return;
    --r->daylight_detector_count;
    if (index != r->daylight_detector_count)
        r->daylight_detectors[index] =
            r->daylight_detectors[r->daylight_detector_count];
    memset(
        &r->daylight_detectors[r->daylight_detector_count], 0,
        sizeof r->daylight_detectors[r->daylight_detector_count]);
}

static int runtime_redstone_comparator_output(
        const GmRuntime *r, int x, int y, int z) {
    const GmRuntimeComparator *entry =
        runtime_comparator_find(r, r->dimension, x, y, z);
    return entry ? entry->output_signal : 0;
}

static int runtime_redstone_comparator_powered(int block, int meta) {
    return block == 150 || (meta & 8) != 0;
}

static int runtime_redstone_torch_powers_face(int meta, int face) {
    int facing;
    switch (meta) {
    case 1: facing = 5; break; /* EAST */
    case 2: facing = 4; break; /* WEST */
    case 3: facing = 3; break; /* SOUTH */
    case 4: facing = 2; break; /* NORTH */
    case 5: facing = 1; break; /* UP */
    default: return 0;
    }
    return facing != face;
}

static int runtime_redstone_is_pressure_plate(int block) {
    return block == 70 || block == 72 || block == 147 || block == 148;
}

static int runtime_redstone_pressure_plate_tick_rate(int block) {
    return block == 147 || block == 148 ? 10 : 20;
}

/* Binary plates expose full redstone power when pressed. Weighted plates
 * store and expose their exact analog strength in metadata. */
static int runtime_redstone_pressure_plate_power(int block, int meta) {
    if (meta <= 0)
        return 0;
    return block == 147 || block == 148 ? meta : 15;
}

static float runtime_celestial_angle(long long world_time) {
    int day_tick = (int)(world_time % 24000LL);
    float angle = ((float)day_tick + 1.0f) / 24000.0f - 0.25f;
    float eased;
    if (angle < 0.0f) angle += 1.0f;
    if (angle > 1.0f) angle -= 1.0f;
    eased = 1.0f - (float)(
        (cos((double)angle * MC_PI) + 1.0) / 2.0);
    return angle + (eased - angle) / 3.0f;
}

static int runtime_skylight_subtracted(const GmRuntime *r) {
    float angle = runtime_celestial_angle(r->clock.world_time);
    float darkness = 1.0f - (
        mc_cos(&r->sin_table, angle * ((float)MC_PI * 2.0f))
            * 2.0f + 0.5f);
    if (darkness < 0.0f) darkness = 0.0f;
    if (darkness > 1.0f) darkness = 1.0f;
    return (int)(darkness * 11.0f);
}

static int runtime_redstone_daylight_power(
        const GmRuntime *r, int x, int y, int z, int inverted) {
    int power;
    float angle;
    float target;
    if (!r || r->dimension != 0)
        return gm_world_meta(r->world, x, y, z);
    power = gm_world_sky_light(r->world, x, y, z)
        - runtime_skylight_subtracted(r);
    if (inverted)
        power = 15 - power;
    if (power > 0 && !inverted) {
        angle = runtime_celestial_angle(r->clock.world_time)
            * ((float)MC_PI * 2.0f);
        target = angle < (float)MC_PI ? 0.0f : (float)MC_PI * 2.0f;
        angle += (target - angle) * 0.2f;
        power = (int)floorf(
            (float)power * mc_cos(&r->sin_table, angle) + 0.5f);
    }
    if (power < 0) return 0;
    return power > 15 ? 15 : power;
}

/* EnumFacing.getHorizontal(meta): SOUTH, WEST, NORTH, EAST. */
static int runtime_redstone_tripwire_hook_face(int meta) {
    static const int faces[4] = {3, 4, 2, 5};
    return faces[meta & 3];
}

/* BlockChest.Type.TRAP weak power is the live tile viewer count, clamped to
 * the redstone range.  This lookup is reached only after a neighboring block
 * is known to be ID 146, so the ordinary no-trapped-chest tick path is free. */
static int runtime_redstone_trapped_chest_power(
        const GmRuntime *r, int x, int y, int z) {
    if (!r || !r->chests
            || gm_world_block(r->world, x, y, z) != 146)
        return 0;
    for (int i = 0; i < r->chests_cap; ++i) {
        const GmRuntimeChest *chest = &r->chests[i];
        int viewers;
        if (!chest->active || chest->wx != x
                || chest->wy != y || chest->wz != z)
            continue;
        viewers = chest->state.te.num_players_using;
        if (viewers < 0) return 0;
        return viewers > 15 ? 15 : viewers;
    }
    return 0;
}

/* BlockRedstoneWire.canConnectTo for the vanilla 1.11.2 power-providing
 * blocks. Diodes connect only on their input/output axis, observers only on
 * their facing side, and a null vertical query connects only to dust. */
static int runtime_redstone_wire_can_connect_to(
        int block, int meta, int face) {
    if (block == 55)
        return 1;
    if (block == 93 || block == 94 || block == 149 || block == 150) {
        int facing = runtime_redstone_repeater_face(meta);
        return facing == face || (facing ^ 1) == face;
    }
    if (block == 218)
        return runtime_redstone_observer_facing(meta) == face;
    if (face < 0)
        return 0;
    return block == 28 || block == 69
        || runtime_redstone_is_pressure_plate(block)
        || block == 75 || block == 76 || block == 77 || block == 143
        || block == 131 || block == 146 || block == 151 || block == 152
        || block == 178;
}

/* Exact BlockRedstoneWire.isPowerSourceAt shape predicate. A normal adjacent
 * cube reaches dust one block above when the current dust has headroom; a
 * non-normal adjacent block reaches dust one block below. */
static int runtime_redstone_wire_power_source_at(
        const GmRuntime *r, int x, int y, int z, int face) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int nx = x + dx[face], nz = z + dz[face];
    int block = gm_world_block(r->world, nx, y, nz);
    int meta = gm_world_meta(r->world, nx, y, nz);
    int normal = gm_block_is_normal_cube_1_11_2(block, meta);
    int headroom_normal = y >= 255 ? 1
        : gm_block_is_normal_cube_1_11_2(
            gm_world_block(r->world, x, y + 1, z),
            gm_world_meta(r->world, x, y + 1, z));
    if (!headroom_normal && normal && y < 255
            && runtime_redstone_wire_can_connect_to(
                gm_world_block(r->world, nx, y + 1, nz),
                gm_world_meta(r->world, nx, y + 1, nz), -1))
        return 1;
    if (runtime_redstone_wire_can_connect_to(block, meta, face))
        return 1;
    if (block == 94
            && runtime_redstone_repeater_face(meta) == face)
        return 1;
    return !normal && y > 0
        && runtime_redstone_wire_can_connect_to(
            gm_world_block(r->world, nx, y - 1, nz),
            gm_world_meta(r->world, nx, y - 1, nz), -1);
}

static int runtime_redstone_wire_weak_power(
        const GmRuntime *r, int x, int y, int z, int face) {
    int power = gm_world_meta(r->world, x, y, z);
    if (power == 0)
        return 0;
    /* BlockRedstoneWire always powers upward. Horizontally it powers along a
     * connected axis, or every side when it has no horizontal connection. */
    if (face == 1)
        return power;
    if (face >= 2) {
        int cn = runtime_redstone_wire_power_source_at(r, x, y, z, 2);
        int cs = runtime_redstone_wire_power_source_at(r, x, y, z, 3);
        int cw = runtime_redstone_wire_power_source_at(r, x, y, z, 4);
        int ce = runtime_redstone_wire_power_source_at(r, x, y, z, 5);
        if (!(cn || cs || cw || ce)
                || (face == 2 && cn && !cw && !ce)
                || (face == 3 && cs && !cw && !ce)
                || (face == 4 && cw && !cn && !cs)
                || (face == 5 && ce && !cn && !cs))
            return power;
    }
    return 0;
}

/* Vanilla asks a normal cube for the strongest directional output among its
 * six neighbors. The normal-cube predicate is captured from the live 1.11.2
 * block-state registry rather than inferred from block IDs. During dust
 * attenuation, wire output must be disabled to match
 * BlockRedstoneWire.canProvidePower=false and prevent self-power. */
static int runtime_redstone_normal_strong_power(
        const GmRuntime *r, int x, int y, int z, int include_wire) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    if (!gm_block_is_normal_cube_1_11_2(
            gm_world_block(r->world, x, y, z),
            gm_world_meta(r->world, x, y, z)))
        return 0;
    for (int face = 0; face < 6; ++face) {
        int nx = x + dx[face], ny = y + dy[face], nz = z + dz[face];
        int id = gm_world_block(r->world, nx, ny, nz);
        int meta = gm_world_meta(r->world, nx, ny, nz);
        if ((id == 69 || id == 77 || id == 143) && (meta & 8) != 0
                && runtime_redstone_control_facing(id, meta) == face)
            return 15;
        if (id == 28 && (meta & 8) != 0 && face == 1)
            return 15;
        /* BlockBasePressurePlate strongly powers only its support below:
         * from that normal cube the plate is the UP neighbor. */
        if (runtime_redstone_is_pressure_plate(id) && face == 1) {
            int power = runtime_redstone_pressure_plate_power(id, meta);
            if (power > 0)
                return power;
        }
        if (id == 131 && (meta & 8) != 0
                && runtime_redstone_tripwire_hook_face(meta) == face)
            return 15;
        /* BlockChest.getStrongPower exposes trapped viewer power only when
         * queried with UP, so only the normal cube directly below carries it. */
        if (id == 146 && face == 1) {
            int power = runtime_redstone_trapped_chest_power(
                r, nx, ny, nz);
            if (power > 0)
                return power;
        }
        if (id == 76 && face == 0
                && runtime_redstone_torch_powers_face(meta, face))
            return 15;
        if (id == 94 && runtime_redstone_repeater_face(meta) == face)
            return 15;
        if ((id == 149 || id == 150)
                && runtime_redstone_comparator_powered(id, meta)
                && runtime_redstone_repeater_face(meta) == face) {
            int power = runtime_redstone_comparator_output(r, nx, ny, nz);
            if (power > 0)
                return power;
        }
        if (id == 218
                && runtime_redstone_observer_power(meta, face) > 0)
            return 15;
        if (include_wire && id == 55) {
            int power = runtime_redstone_wire_weak_power(
                r, nx, ny, nz, face);
            if (power > 0)
                return power;
        }
    }
    return 0;
}

static int runtime_redstone_is_powered(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    for (int face = 0; face < 6; ++face) {
        int nx = x + dx[face], ny = y + dy[face], nz = z + dz[face];
        int id = gm_world_block(r->world, nx, ny, nz);
        if (gm_block_is_normal_cube_1_11_2(
                    id, gm_world_meta(r->world, nx, ny, nz))
                && runtime_redstone_normal_strong_power(
                    r, nx, ny, nz, 1) > 0)
            return 1;
        if (id == 152
                || (id == 28
                    && (gm_world_meta(r->world, nx, ny, nz) & 8) != 0)
                || ((id == 69 || id == 77 || id == 143)
                    && (gm_world_meta(r->world, nx, ny, nz) & 8) != 0)
                || (runtime_redstone_is_pressure_plate(id)
                    && gm_world_meta(r->world, nx, ny, nz) != 0)
                || (id == 131
                    && (gm_world_meta(r->world, nx, ny, nz) & 8) != 0)
                || ((id == 151 || id == 178)
                    && gm_world_meta(r->world, nx, ny, nz) != 0))
            return 1;
        if (id == 146
                && runtime_redstone_trapped_chest_power(
                    r, nx, ny, nz) > 0)
            return 1;
        if (id == 76 && runtime_redstone_torch_powers_face(
                gm_world_meta(r->world, nx, ny, nz), face))
            return 1;
        if (id == 94 && runtime_redstone_repeater_face(
                gm_world_meta(r->world, nx, ny, nz)) == face)
            return 1;
        if ((id == 149 || id == 150)
                && runtime_redstone_comparator_powered(
                    id, gm_world_meta(r->world, nx, ny, nz))
                && runtime_redstone_repeater_face(
                    gm_world_meta(r->world, nx, ny, nz)) == face
                && runtime_redstone_comparator_output(r, nx, ny, nz) > 0)
            return 1;
        if (id == 218 && runtime_redstone_observer_power(
                gm_world_meta(r->world, nx, ny, nz), face) > 0)
            return 1;
        if (id == 55) {
            if (runtime_redstone_wire_weak_power(
                    r, nx, ny, nz, face) > 0)
                return 1;
        }
    }
    return 0;
}

/* BlockRedstoneLight.onBlockAdded normalizes immediately, unlike
 * neighborChanged's delayed lit-to-unlit path. The inner setBlockState uses
 * flag 2, so observers see the normalized state but no neighbor callback is
 * emitted until the outer direct placement completes. */
static void runtime_redstone_lamp_on_added(
        GmRuntime *r, int x, int y, int z, int block) {
    int powered = runtime_redstone_is_powered(r, x, y, z);
    int settled = block;
    if (block == 124 && !powered)
        settled = 123;
    else if (block == 123 && powered)
        settled = 124;
    if (settled != block) {
        gm_world_set_block_meta(r->world, x, y, z, settled, 0);
        runtime_redstone_update_observers_at(r, x, y, z);
    }
}

#define RUNTIME_REDSTONE_WIRES 256
#define RUNTIME_REDSTONE_WIRE_QUEUE (RUNTIME_REDSTONE_WIRES * 16)

static int runtime_redstone_direct_power(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    for (int face = 0; face < 6; ++face) {
        int nx = x + dx[face], ny = y + dy[face], nz = z + dz[face];
        int id = gm_world_block(r->world, nx, ny, nz);
        if (gm_block_is_normal_cube_1_11_2(
                id, gm_world_meta(r->world, nx, ny, nz))) {
            int power = runtime_redstone_normal_strong_power(
                r, nx, ny, nz, 0);
            if (power > 0)
                return power;
        }
        if (id == 152
                || (id == 28
                    && (gm_world_meta(r->world, nx, ny, nz) & 8) != 0)
                || ((id == 69 || id == 77 || id == 143)
                    && (gm_world_meta(r->world, nx, ny, nz) & 8) != 0)
                || ((id == 70 || id == 72)
                    && gm_world_meta(r->world, nx, ny, nz) != 0))
            return 15;
        if ((id == 147 || id == 148)
                && gm_world_meta(r->world, nx, ny, nz) != 0)
            return gm_world_meta(r->world, nx, ny, nz);
        if ((id == 151 || id == 178)
                && gm_world_meta(r->world, nx, ny, nz) != 0)
            return gm_world_meta(r->world, nx, ny, nz);
        if (id == 131
                && (gm_world_meta(r->world, nx, ny, nz) & 8) != 0)
            return 15;
        if (id == 146) {
            int power = runtime_redstone_trapped_chest_power(
                r, nx, ny, nz);
            if (power > 0)
                return power;
        }
        if (id == 76 && runtime_redstone_torch_powers_face(
                gm_world_meta(r->world, nx, ny, nz), face))
            return 15;
        if (id == 94 && runtime_redstone_repeater_face(
                gm_world_meta(r->world, nx, ny, nz)) == face)
            return 15;
        if ((id == 149 || id == 150)
                && runtime_redstone_comparator_powered(
                    id, gm_world_meta(r->world, nx, ny, nz))
                && runtime_redstone_repeater_face(
                    gm_world_meta(r->world, nx, ny, nz)) == face) {
            int power = runtime_redstone_comparator_output(r, nx, ny, nz);
            if (power > 0)
                return power;
        }
        if (id == 218 && runtime_redstone_observer_power(
                gm_world_meta(r->world, nx, ny, nz), face) > 0)
            return 15;
    }
    return 0;
}

static void runtime_redstone_neighbor_changed(
        GmRuntime *r, int x, int y, int z);
static void runtime_redstone_notify_neighbors(
        GmRuntime *r, int x, int y, int z);
static int runtime_redstone_piston_spawn_item_stack(
        GmRuntime *r, int x, int y, int z, int item, int item_meta);
static int runtime_redstone_piston_apply_destroy_payload(
        GmRuntime *r, int x, int y, int z, int id, int meta);
static void runtime_redstone_tripwire_hook_break_state(
        GmRuntime *r, int x, int y, int z, int meta);

static int runtime_redstone_cactus_can_stay(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[4] = {-1, 1, 0, 0};
    static const int dz[4] = {0, 0, -1, 1};
    for (int face = 0; face < 4; ++face) {
        int neighbor_id = gm_world_block(
            r->world, x + dx[face], y, z + dz[face]);
        int neighbor_meta = gm_world_meta(
            r->world, x + dx[face], y, z + dz[face]);
        if (gm_block_material_is_solid_1_11_2(
                neighbor_id, neighbor_meta)
                || neighbor_id == 10 || neighbor_id == 11)
            return 0;
    }
    {
        int below = gm_world_block(r->world, x, y - 1, z);
        int above = gm_world_block(r->world, x, y + 1, z);
        return (below == 12 || below == 81)
            && (above < 8 || above > 11);
    }
}

/* BlockChorusFlower.canSurvive in 1.11.2 accepts direct chorus-plant or
 * end-stone support. With air below, it instead requires exactly one
 * horizontal chorus-plant neighbor and air on the other three sides. */
static int runtime_redstone_chorus_flower_can_survive(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[4] = {-1, 1, 0, 0};
    static const int dz[4] = {0, 0, -1, 1};
    int below = gm_world_block(r->world, x, y - 1, z);
    int plants = 0;
    if (below == 199 || below == 121)
        return 1;
    if (below != 0)
        return 0;
    for (int face = 0; face < 4; ++face) {
        int neighbor = gm_world_block(
            r->world, x + dx[face], y, z + dz[face]);
        if (neighbor == 199)
            ++plants;
        else if (neighbor != 0)
            return 0;
    }
    return plants == 1;
}

/* A chorus plant is sustained directly by plant/end stone below, or by a
 * horizontal plant whose own support is plant/end stone. The side-supported
 * form additionally requires air either above or below this cell. */
static int runtime_redstone_chorus_plant_can_survive(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[4] = {-1, 1, 0, 0};
    static const int dz[4] = {0, 0, -1, 1};
    int air_above = gm_world_block(r->world, x, y + 1, z) == 0;
    int air_below = gm_world_block(r->world, x, y - 1, z) == 0;
    for (int face = 0; face < 4; ++face) {
        int neighbor_x = x + dx[face];
        int neighbor_z = z + dz[face];
        if (gm_world_block(r->world, neighbor_x, y, neighbor_z) != 199)
            continue;
        if (!air_above && !air_below)
            return 0;
        {
            int neighbor_below = gm_world_block(
                r->world, neighbor_x, y - 1, neighbor_z);
            if (neighbor_below == 199 || neighbor_below == 121)
                return 1;
        }
    }
    {
        int below = gm_world_block(r->world, x, y - 1, z);
        return below == 199 || below == 121;
    }
}

/* BlockObserver.updateNeighborsInFront notifies its output cell first, then
 * every neighbor of that cell except the face leading back to the observer. */
static void runtime_redstone_observer_notify_output(
        GmRuntime *r, int x, int y, int z, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int face = runtime_redstone_observer_facing(meta);
    int output_face;
    int output_x;
    int output_y;
    int output_z;
    if (face < 0)
        return;
    output_face = face ^ 1;
    output_x = x + dx[output_face];
    output_y = y + dy[output_face];
    output_z = z + dz[output_face];
    runtime_redstone_neighbor_changed(r, output_x, output_y, output_z);
    for (int notify_face = 0; notify_face < 6; ++notify_face)
        if (notify_face != face)
            runtime_redstone_neighbor_changed(
                r, output_x + dx[notify_face],
                output_y + dy[notify_face],
                output_z + dz[notify_face]);
}

/* Return the one wire reached horizontally in direction (dx,dz), including
 * vanilla's one-block up/down rules. This exact slice admits air-covered wire
 * on stone supports: it climbs when the adjacent cell is stone and the
 * current wire has headroom, and descends when the adjacent cell is not a
 * Java 1.11.2 normal cube. */
static int runtime_redstone_wire_neighbor_y(
        const GmRuntime *r, int x, int y, int z, int dx, int dz,
        int *neighbor_y) {
    int nx = x + dx, nz = z + dz;
    int adjacent = gm_world_block(r->world, nx, y, nz);
    int adjacent_normal = gm_block_is_normal_cube_1_11_2(
        adjacent, gm_world_meta(r->world, nx, y, nz));
    if (adjacent == 55) {
        *neighbor_y = y;
        return 1;
    }
    if (adjacent_normal && y < 255
            && !gm_block_is_normal_cube_1_11_2(
                gm_world_block(r->world, x, y + 1, z),
                gm_world_meta(r->world, x, y + 1, z))
            && gm_world_block(r->world, nx, y + 1, nz) == 55) {
        *neighbor_y = y + 1;
        return 1;
    }
    if (!adjacent_normal && y > 0
            && gm_world_block(r->world, nx, y - 1, nz) == 55) {
        *neighbor_y = y - 1;
        return 1;
    }
    return 0;
}

static int runtime_redstone_wire_support_supported(
        const GmRuntime *r, int x, int y, int z) {
    int id = gm_world_block(r->world, x, y, z);
    int meta = gm_world_meta(r->world, x, y, z);
    /* Exact BlockRedstoneWire.canPlaceBlockAt predicate. isFullyOpaque is
     * stateful for slabs, stairs, snow, pistons, piston heads, and hoppers;
     * glowstone is vanilla's explicit exception. */
    return id == 89 || gm_block_is_fully_opaque_1_11_2(id, meta);
}

/* Exact bounded dust slice: a connected component of air-covered wire on a
 * represented valid support, with flat and one-block vertical edges. The
 * fixed work set is built only after an edit reaches wire, then a multi-source
 * breadth-first relaxation gives vanilla's 15..0 attenuation. Components
 * larger than the cap or leaving this proof region are rejected before
 * mutation. */
static void runtime_redstone_update_wire_component(
        GmRuntime *r, int start_x, int start_y, int start_z) {
    static const int dx[4] = {0, 0, -1, 1};
    static const int dz[4] = {-1, 1, 0, 0};
    int wx[RUNTIME_REDSTONE_WIRES];
    int wy[RUNTIME_REDSTONE_WIRES];
    int wz[RUNTIME_REDSTONE_WIRES];
    unsigned char power[RUNTIME_REDSTONE_WIRES];
    unsigned char changed[RUNTIME_REDSTONE_WIRES];
    int queue[RUNTIME_REDSTONE_WIRE_QUEUE];
    int count = 0, gather_at = 0, queue_head = 0, queue_tail = 0;
    if (gm_world_block(r->world, start_x, start_y, start_z) != 55)
        return;
    wx[count] = start_x;
    wy[count] = start_y;
    wz[count++] = start_z;
    while (gather_at < count) {
        int x = wx[gather_at], y = wy[gather_at], z = wz[gather_at++];
        if (y <= 0 || y >= 255
                || !runtime_redstone_wire_support_supported(
                    r, x, y - 1, z))
            return;
        for (int face = 0; face < 4; ++face) {
            int nx = x + dx[face], ny, nz = z + dz[face];
            int known = 0;
            if (!runtime_redstone_wire_neighbor_y(
                    r, x, y, z, dx[face], dz[face], &ny))
                continue;
            for (int i = 0; i < count; ++i)
                if (wx[i] == nx && wy[i] == ny && wz[i] == nz) {
                    known = 1;
                    break;
                }
            if (!known && count >= RUNTIME_REDSTONE_WIRES)
                return;
            if (!known) {
                wx[count] = nx;
                wy[count] = ny;
                wz[count++] = nz;
            }
        }
    }
    for (int i = 0; i < count; ++i) {
        power[i] = (unsigned char)runtime_redstone_direct_power(
            r, wx[i], wy[i], wz[i]);
        if (power[i] > 0)
            queue[queue_tail++] = i;
    }
    while (queue_head < queue_tail) {
        int from = queue[queue_head++];
        if (power[from] <= 1)
            continue;
        for (int face = 0; face < 4; ++face) {
            int neighbor_y;
            if (!runtime_redstone_wire_neighbor_y(
                    r, wx[from], wy[from], wz[from],
                    dx[face], dz[face], &neighbor_y))
                continue;
            for (int to = 0; to < count; ++to)
                if (wx[to] == wx[from] + dx[face]
                        && wy[to] == neighbor_y
                        && wz[to] == wz[from] + dz[face]
                        && power[to] + 1 < power[from]) {
                    power[to] = power[from] - 1;
                    if (queue_tail < RUNTIME_REDSTONE_WIRE_QUEUE)
                        queue[queue_tail++] = to;
                }
        }
    }
    for (int i = 0; i < count; ++i) {
        changed[i] = (unsigned char)(
            gm_world_meta(r->world, wx[i], wy[i], wz[i]) != power[i]);
        if (changed[i])
            gm_world_set_block_meta(
                r->world, wx[i], wy[i], wz[i], 55, power[i]);
    }
    for (int i = 0; i < count; ++i) {
        if (!changed[i])
            continue;
        runtime_redstone_update_observers_at(
            r, wx[i], wy[i], wz[i]);
        static const int notify_dx[6] = {0, 0, 0, 0, -1, 1};
        static const int notify_dy[6] = {-1, 1, 0, 0, 0, 0};
        static const int notify_dz[6] = {0, 0, -1, 1, 0, 0};
        for (int face = 0; face < 6; ++face) {
            int nx = wx[i] + notify_dx[face];
            int ny = wy[i] + notify_dy[face];
            int nz = wz[i] + notify_dz[face];
            int neighbor = gm_world_block(r->world, nx, ny, nz);
            if (neighbor != 55)
                runtime_redstone_neighbor_changed(r, nx, ny, nz);
            /* Wire updates notify both their immediate neighbors and the
             * neighbors of those positions. Promote that second ring through
             * represented normal cube so indirect consumers see the changed
             * strong power without scanning the world. */
            if (gm_block_is_normal_cube_1_11_2(
                    neighbor, gm_world_meta(r->world, nx, ny, nz)))
                for (int second = 0; second < 6; ++second) {
                    int sx = nx + notify_dx[second];
                    int sy = ny + notify_dy[second];
                    int sz = nz + notify_dz[second];
                    if (gm_world_block(r->world, sx, sy, sz) != 55)
                        runtime_redstone_neighbor_changed(r, sx, sy, sz);
                }
        }
    }
}

/* BlockRedstoneWire.onBlockAdded runs this fixed notification traversal after
 * updateSurroundingRedstone. Plane.VERTICAL is UP,DOWN; Plane.HORIZONTAL is
 * NORTH,EAST,SOUTH,WEST. notifyWireNeighborsOfStateChange acts only when its
 * target is wire, then notifies around that wire and around all six adjacent
 * positions. The work exists only on direct wire placement. */
static void runtime_redstone_wire_notify_wire_neighbors(
        GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    if (gm_world_block(r->world, x, y, z) != 55)
        return;
    runtime_redstone_notify_neighbors(r, x, y, z);
    for (int face = 0; face < 6; ++face)
        runtime_redstone_notify_neighbors(
            r, x + dx[face], y + dy[face], z + dz[face]);
}

static void runtime_redstone_wire_on_added(
        GmRuntime *r, int x, int y, int z) {
    static const int dx[4] = {0, 1, 0, -1};
    static const int dz[4] = {-1, 0, 1, 0};
    runtime_redstone_notify_neighbors(r, x, y + 1, z);
    runtime_redstone_notify_neighbors(r, x, y - 1, z);
    for (int face = 0; face < 4; ++face)
        runtime_redstone_wire_notify_wire_neighbors(
            r, x + dx[face], y, z + dz[face]);
    for (int face = 0; face < 4; ++face) {
        int nx = x + dx[face];
        int nz = z + dz[face];
        int id = gm_world_block(r->world, nx, y, nz);
        int meta = gm_world_meta(r->world, nx, y, nz);
        runtime_redstone_wire_notify_wire_neighbors(
            r, nx, y + (gm_block_is_normal_cube_1_11_2(id, meta) ? 1 : -1),
            nz);
    }
}

static int runtime_redstone_repeater_input_powered(
        const GmRuntime *r, int x, int y, int z, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int face = runtime_redstone_repeater_face(meta);
    int nx = x + dx[face], ny = y + dy[face], nz = z + dz[face];
    int id = gm_world_block(r->world, nx, ny, nz);
    int neighbor_meta = gm_world_meta(r->world, nx, ny, nz);
    if (gm_block_is_normal_cube_1_11_2(id, neighbor_meta)
            && runtime_redstone_normal_strong_power(
                r, nx, ny, nz, 1) > 0)
        return 1;
    if (id == 152
            || ((id == 69 || id == 77 || id == 143)
                && (neighbor_meta & 8) != 0)
            || (runtime_redstone_is_pressure_plate(id)
                && neighbor_meta != 0)
            || (id == 131 && (neighbor_meta & 8) != 0)
            || ((id == 151 || id == 178) && neighbor_meta != 0))
        return 1;
    if (id == 76
            && runtime_redstone_torch_powers_face(neighbor_meta, face))
        return 1;
    if (id == 94
            && runtime_redstone_repeater_face(neighbor_meta) == face)
        return 1;
    if ((id == 149 || id == 150)
            && runtime_redstone_comparator_powered(id, neighbor_meta)
            && runtime_redstone_repeater_face(neighbor_meta) == face
            && runtime_redstone_comparator_output(r, nx, ny, nz) > 0)
        return 1;
    if (id == 218
            && runtime_redstone_observer_power(neighbor_meta, face) > 0)
        return 1;
    if (id == 146
            && runtime_redstone_trapped_chest_power(r, nx, ny, nz) > 0)
        return 1;
    return id == 55
        && runtime_redstone_wire_weak_power(r, nx, ny, nz, face) > 0;
}

static int runtime_redstone_repeater_locked(
        const GmRuntime *r, int x, int y, int z, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int face = runtime_redstone_repeater_face(meta);
    int sides[2];
    if (face == 2 || face == 3) {
        sides[0] = 4;
        sides[1] = 5;
    } else {
        sides[0] = 2;
        sides[1] = 3;
    }
    for (int i = 0; i < 2; ++i) {
        int side = sides[i];
        int id = gm_world_block(r->world, x + dx[side], y, z + dz[side]);
        int side_meta = gm_world_meta(
            r->world, x + dx[side], y, z + dz[side]);
        if (id == 94 && runtime_redstone_repeater_face(side_meta) == side)
            return 1;
        if ((id == 149 || id == 150)
                && runtime_redstone_comparator_powered(id, side_meta)
                && runtime_redstone_repeater_face(side_meta) == side
                && runtime_redstone_comparator_output(
                    r, x + dx[side], y, z + dz[side]) > 0)
            return 1;
    }
    return 0;
}

static int runtime_redstone_repeater_supported(
        const GmRuntime *r, int x, int y, int z, int block) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int meta;
    int face;
    int output_face;
    if ((block != 93 && block != 94)
            || gm_world_block(r->world, x, y, z) != block
            || y <= 0 || y >= 255
            || !gm_block_is_fully_opaque_1_11_2(
                gm_world_block(r->world, x, y - 1, z),
                gm_world_meta(r->world, x, y - 1, z))
            || gm_world_block(r->world, x, y + 1, z) != 0)
        return 0;
    meta = gm_world_meta(r->world, x, y, z);
    face = runtime_redstone_repeater_face(meta);
    output_face = face == 2 ? 3 : face == 3 ? 2 : face == 4 ? 5 : 4;
    for (int i = 0; i < 4; ++i) {
        static const int horizontal[4] = {2, 3, 4, 5};
        int neighbor_face = horizontal[i];
        int id = gm_world_block(
            r->world, x + dx[neighbor_face], y, z + dz[neighbor_face]);
        int neighbor_meta = gm_world_meta(
            r->world, x + dx[neighbor_face], y,
            z + dz[neighbor_face]);
        if (neighbor_face == face) {
            if (id != 0 && id != 55 && id != 69 && id != 70
                    && id != 72 && id != 75 && id != 76 && id != 77
                    && id != 93 && id != 94 && id != 143
                    && id != 149 && id != 150
                    && id != 146 && id != 147 && id != 148 && id != 152
                    && id != 218
                    && !gm_block_is_normal_cube_1_11_2(id, neighbor_meta))
                return 0;
        } else if (neighbor_face == output_face) {
            if (id != 0 && id != 55 && id != 93 && id != 94
                    && id != 149 && id != 150
                    && id != 123 && id != 124
                    && id != 218
                    && !gm_block_is_normal_cube_1_11_2(id, neighbor_meta))
                return 0;
        } else if (id != 0 && id != 93 && id != 94
                && id != 149 && id != 150 && id != 218
                && !gm_block_is_normal_cube_1_11_2(id, neighbor_meta)) {
            return 0;
        }
    }
    return 1;
}

static int runtime_redstone_repeater_tick_pending(
        const GmRuntime *r, int x, int y, int z) {
    for (int i = 0; i < r->scheduled_tick_count; ++i) {
        const GmRuntimeScheduledTick *entry = &r->scheduled_ticks[i];
        if (entry->x == x && entry->y == y && entry->z == z
                && (entry->block == 93 || entry->block == 94))
            return 1;
    }
    return 0;
}

static int runtime_redstone_repeater_priority(
        const GmRuntime *r, int x, int y, int z, int block, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int face = runtime_redstone_repeater_face(meta);
    int output_face = face == 2 ? 3 : face == 3 ? 2 : face == 4 ? 5 : 4;
    int output_id = gm_world_block(
        r->world, x + dx[output_face], y, z + dz[output_face]);
    int output_meta = gm_world_meta(
        r->world, x + dx[output_face], y, z + dz[output_face]);
    if ((output_id == 93 || output_id == 94
            || output_id == 149 || output_id == 150)
            && runtime_redstone_repeater_face(output_meta) != output_face)
        return -3;
    return block == 94 ? -2 : -1;
}

static void runtime_redstone_repeater_notify_output(
        GmRuntime *r, int x, int y, int z, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int face = runtime_redstone_repeater_face(meta);
    int output_face = face == 2 ? 3 : face == 3 ? 2 : face == 4 ? 5 : 4;
    int output_x = x + dx[output_face];
    int output_y = y + dy[output_face];
    int output_z = z + dz[output_face];
    runtime_redstone_neighbor_changed(r, output_x, output_y, output_z);
    for (int notify_face = 0; notify_face < 6; ++notify_face)
        if (notify_face != face)
            runtime_redstone_neighbor_changed(
                r, output_x + dx[notify_face],
                output_y + dy[notify_face],
                output_z + dz[notify_face]);
}

static void runtime_redstone_repeater_update_state(
        GmRuntime *r, int x, int y, int z, int block) {
    int meta;
    int input_powered;
    int powered;
    int delay;
    int priority;
    if (!runtime_redstone_repeater_supported(r, x, y, z, block))
        return;
    meta = gm_world_meta(r->world, x, y, z);
    if (runtime_redstone_repeater_locked(r, x, y, z, meta))
        return;
    input_powered = runtime_redstone_repeater_input_powered(
        r, x, y, z, meta);
    powered = block == 94;
    if (input_powered == powered
            || runtime_redstone_repeater_tick_pending(r, x, y, z))
        return;
    delay = ((meta >> 2) + 1) * 2;
    priority = runtime_redstone_repeater_priority(
        r, x, y, z, block, meta);
    runtime_schedule_tick_insert(
        r, x, y, z, block, r->clock.total_time + delay, priority,
        r->scheduled_tick_next_order);
}

static int runtime_redstone_comparator_signal_from_neighbor(
        const GmRuntime *r, int x, int y, int z, int face) {
    int id = gm_world_block(r->world, x, y, z);
    int meta = gm_world_meta(r->world, x, y, z);
    if (id == 152)
        return 15;
    if (id == 28 && (meta & 8) != 0)
        return 15;
    if ((id == 69 || id == 77 || id == 143) && (meta & 8) != 0)
        return 15;
    if (runtime_redstone_is_pressure_plate(id))
        return runtime_redstone_pressure_plate_power(id, meta);
    if (id == 151 || id == 178)
        return meta;
    if (id == 131 && (meta & 8) != 0)
        return 15;
    if (id == 76 && runtime_redstone_torch_powers_face(meta, face))
        return 15;
    if (id == 94 && runtime_redstone_repeater_face(meta) == face)
        return 15;
    if ((id == 149 || id == 150)
            && runtime_redstone_comparator_powered(id, meta)
            && runtime_redstone_repeater_face(meta) == face)
        return runtime_redstone_comparator_output(r, x, y, z);
    if (id == 218)
        return runtime_redstone_observer_power(meta, face);
    if (id == 146)
        return runtime_redstone_trapped_chest_power(r, x, y, z);
    /* BlockRedstoneDiode explicitly takes max(getRedstonePower, POWER) for
     * dust, and comparator side input also reads POWER directly. Neither path
     * applies dust's directional weak-power face filter. */
    if (id == 55)
        return meta;
    if (gm_block_is_normal_cube_1_11_2(id, meta))
        return runtime_redstone_normal_strong_power(r, x, y, z, 1);
    return 0;
}

static const GmRuntimeChest *runtime_chest_tile_at(
    const GmRuntime *r, int x, int y, int z);
static int runtime_chest_pair_at(
    const GmRuntime *r, int x, int y, int z,
    const GmRuntimeChest **pair);
static const GmRuntimeFurnace *runtime_furnace_at(
    const GmRuntime *r, int x, int y, int z);
static const GmRuntimeStaticContainer *runtime_static_container_at(
    const GmRuntime *r, int x, int y, int z);
static const GmRuntimeCommandBlock *runtime_command_block_at(
    const GmRuntime *r, int x, int y, int z);
static int runtime_item_frame_surface_valid(
    const GmRuntime *r, int x, int y, int z, int facing);
static const GmRuntimeItemFrame *runtime_item_frame_at(
    const GmRuntime *r, int x, int y, int z, int facing);
static int runtime_minecart_detector_signal(
    const GmRuntime *r, int x, int y, int z);

/* Container.calcRedstoneFromInventory. The represented dispenser/dropper
 * subset has Minecraft's ordinary inventory limit of 64; tec_max_stack_size
 * supplies the item limit already enforced when a capsule slot is restored.
 * Keep every intermediate as float to match Java's 1.11.2 calculation. */
static int runtime_static_container_comparator_strength(
        const GmRuntimeStaticContainer *container) {
    float fullness = 0.0f;
    int occupied = 0;
    if (!container || container->size <= 0
            || container->size > GM_RUNTIME_STATIC_CONTAINER_SLOTS)
        return 0;
    for (int slot = 0; slot < container->size; ++slot) {
        const ICStack *stack = &container->slots[slot];
        if (isr_is_empty(stack))
            continue;
        int item_limit = tec_max_stack_size(stack->item);
        int limit = item_limit < 64 ? item_limit : 64;
        fullness += (float)stack->count / (float)limit;
        occupied++;
    }
    fullness /= (float)container->size;
    return (int)floorf(fullness * 14.0f)
        + (occupied > 0 ? 1 : 0);
}

static int runtime_redstone_comparator_override_signal(
        const GmRuntime *r, int x, int y, int z, int *signal) {
    int id = gm_world_block(r->world, x, y, z);
    int meta = gm_world_meta(r->world, x, y, z);
    int value;
    if (id == 54 || id == 146) {
        const GmRuntimeChest *chest =
            runtime_chest_tile_at(r, x, y, z);
        const GmRuntimeChest *pair = NULL;
        if (!chest) return 0;
        int adjacent_count =
            runtime_chest_pair_at(r, x, y, z, &pair);
        if (adjacent_count == 0) {
            value = chest_live_comparator_strength(&chest->state);
        } else if (adjacent_count == 1 && pair) {
            value = chest_live_double_comparator_strength(
                &chest->state, &pair->state);
        } else {
            return 0;
        }
    } else if (id == 61 || id == 62) {
        const GmRuntimeFurnace *furnace =
            runtime_furnace_at(r, x, y, z);
        if (!furnace) return 0;
        value = furnace_live_comparator_strength(&furnace->state);
    } else if (id == 28 && (meta & 8) != 0) {
        value = runtime_minecart_detector_signal(r, x, y, z);
    } else if (id == 23 || id == 158) {
        const GmRuntimeStaticContainer *container =
            runtime_static_container_at(r, x, y, z);
        if (!container) return 0;
        value = runtime_static_container_comparator_strength(container);
    } else if (id == 117) {
        const GmRuntimeStaticContainer *container =
            runtime_static_container_at(r, x, y, z);
        if (!container) return 0;
        value = brewing_live_comparator_strength(container->slots);
    } else if (id == 84) {
        const GmRuntimeStaticContainer *jukebox =
            runtime_static_container_at(r, x, y, z);
        if (!jukebox) return 0;
        const ICStack *record = &jukebox->slots[0];
        value = isr_is_empty(record) ? 0 : record->item - 2255;
    } else if (runtime_is_command_block(id)) {
        const GmRuntimeCommandBlock *command =
            runtime_command_block_at(r, x, y, z);
        if (!command) return 0;
        value = command->success_count;
    } else if (id == 92 && meta <= 6) {
        value = (7 - meta) * 2;
    } else if (id == 118 && meta <= 3) {
        value = meta;
    } else if (id == 120 && meta <= 7) {
        value = (meta & 4) != 0 ? 15 : 0;
    } else {
        return 0;
    }
    if (signal)
        *signal = value;
    return 1;
}

static const GmRuntimeChest *runtime_chest_tile_at(
    const GmRuntime *r, int x, int y, int z) {
    if (!r || !r->chests
            || !runtime_is_chest_block(
                gm_world_block(r->world, x, y, z))
            || gm_world_block(r->world, x, y + 1, z) != 0)
        return NULL;
    for (int i = 0; i < r->chests_cap; ++i) {
        const GmRuntimeChest *chest = &r->chests[i];
        if (chest->active && chest->state.loot_filled
                && chest->wx == x && chest->wy == y && chest->wz == z)
            return chest;
    }
    return NULL;
}

/* TileEntityLockableLoot fills a generated jungle trap on its first inventory
 * access, including an automatic redstone dispense before any player opens it. */
static int runtime_generated_dispenser_ensure(
        GmRuntime *r, int x, int y, int z) {
    long long loot_seed;
    TecStack generated[9];
    GmRuntimeStaticContainer *container = NULL;
    if (!r || gm_world_block(r->world, x, y, z) != 23)
        return 0;
    if (runtime_static_container_at(r, x, y, z))
        return 1;
    if (!popmc_jungle_dispenser_info(
            r->seed, x, y, z, &loot_seed, NULL))
        return 0;
    if (!gm_runtime_static_container_set_slot(
            r, r->dimension, x, y, z, 0, 0, 0, 0))
        return 0;
    for (int i = 0; i < r->static_containers_cap; ++i) {
        GmRuntimeStaticContainer *candidate = &r->static_containers[i];
        if (candidate->active && candidate->dimension == r->dimension
                && candidate->wx == x && candidate->wy == y
                && candidate->wz == z && candidate->block == 23) {
            container = candidate;
            break;
        }
    }
    if (!container) return 0;
    for (int i = 0; i < 9; ++i) generated[i] = tec_empty();
    shl_fill_inventory(generated, 9,
        SHL_JUNGLE_TEMPLE_DISPENSER, (i64)loot_seed);
    for (int i = 0; i < 9; ++i) {
        const TecStack *src = &generated[i];
        if (tec_is_empty(src)) {
            container->slots[i] = ic_empty();
            continue;
        }
        container->slots[i] = ic_mk(src->item, src->count, src->meta);
        container->slots[i].n_enchants = src->n_enchants;
        for (int e = 0; e < src->n_enchants && e < IC_MAX_ENCHANTS; ++e) {
            container->slots[i].enchants[e].id = src->enchants[e].id;
            container->slots[i].enchants[e].level = src->enchants[e].level;
        }
    }
    return 1;
}

static int runtime_jukebox_insert_record(
        GmRuntime *r, int x, int y, int z,
        int item, int meta, int decrement_held)
{
    const GmRuntimeStaticContainer *jukebox =
        runtime_static_container_at(r, x, y, z);
    if (!r || gm_world_block(r->world, x, y, z) != 84
            || gm_world_meta(r->world, x, y, z) != 0
            || !jukebox || !isr_is_empty(&jukebox->slots[0])
            || item < 2256 || item > 2267 || meta != 0)
        return 0;
    gm_world_set_block_meta(r->world, x, y, z, 84, 1);
    if (!gm_runtime_static_container_set_slot(
            r, r->dimension, x, y, z, 0, item, 1, meta)) {
        gm_world_set_block_meta(r->world, x, y, z, 84, 0);
        return 0;
    }
    if (decrement_held)
        runtime_world_event_append(r, 1010, x, y, z, item);
    if (decrement_held
            && isr_decr_stack_size(
                &r->player.inv, r->player.inv.current_item, 1).count <= 0)
        return 0;
    return 1;
}

static int runtime_jukebox_eject_record(
        GmRuntime *r, int x, int y, int z)
{
    const GmRuntimeStaticContainer *jukebox =
        runtime_static_container_at(r, x, y, z);
    ICStack record;
    double spawn_x, spawn_y, spawn_z;
    double motion_x, motion_y, motion_z;
    float yaw;
    int eid;
    int free_entity = 0;
    if (!r || gm_world_block(r->world, x, y, z) != 84
            || gm_world_meta(r->world, x, y, z) == 0 || !jukebox)
        return 0;
    record = jukebox->slots[0];
    if (record.item < 2256 || record.item > 2267
            || record.count != 1 || record.meta != 0
            || r->next_entity_id <= 0)
        return 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (!r->entities.ents[i].active) {
            free_entity = 1;
            break;
        }
    if (!free_entity)
        return 0;

    runtime_world_event_append(r, 1010, x, y, z, 0);

    /* BlockJukebox.dropRecord uses three World.rand float offsets, then the
     * four Math.random values consumed by EntityItem's constructor. */
    spawn_x = (double)x
        + (double)(runtime_java_random_next_float(r) * 0.7f)
        + 0.15000000596046448;
    spawn_y = (double)y
        + (double)(runtime_java_random_next_float(r) * 0.7f)
        + 0.06000000238418579 + 0.6;
    spawn_z = (double)z
        + (double)(runtime_java_random_next_float(r) * 0.7f)
        + 0.15000000596046448;
    (void)runtime_math_random_next_double(r);
    yaw = (float)(runtime_math_random_next_double(r) * 360.0);
    motion_x = (double)(float)(
        runtime_math_random_next_double(r)
            * 0.20000000298023224 - 0.10000000149011612);
    motion_z = (double)(float)(
        runtime_math_random_next_double(r)
            * 0.20000000298023224 - 0.10000000149011612);

    /* The spawn box can overlap the still-present jukebox. EntityItem then
     * runs Entity.pushOutOfBlocks before move. The oracle pins the new
     * entity's otherwise clock-seeded Random cursor to internal seed zero;
     * its first nextFloat is zero, hence the exact 0.1F push magnitude. */
    if (spawn_y < (double)y + 1.0
            && spawn_y + 0.25 > (double)y) {
        int bx = (int)floor(spawn_x);
        int by = (int)floor(spawn_y + 0.125);
        int bz = (int)floor(spawn_z);
        double fx = spawn_x - (double)bx;
        double fy = spawn_y + 0.125 - (double)by;
        double fz = spawn_z - (double)bz;
        double nearest = INFINITY;
        int axis = 1;
        int sign = 1;
#define JUKEBOX_NEIGHBOR_FULL(dx, dy, dz) \
        gm_block_is_full_cube_1_11_2( \
            gm_world_block(r->world, bx + (dx), by + (dy), bz + (dz)), \
            gm_world_meta(r->world, bx + (dx), by + (dy), bz + (dz)))
        if (!JUKEBOX_NEIGHBOR_FULL(-1, 0, 0) && fx < nearest) {
            nearest = fx; axis = 0; sign = -1;
        }
        if (!JUKEBOX_NEIGHBOR_FULL(1, 0, 0) && 1.0 - fx < nearest) {
            nearest = 1.0 - fx; axis = 0; sign = 1;
        }
        if (!JUKEBOX_NEIGHBOR_FULL(0, 0, -1) && fz < nearest) {
            nearest = fz; axis = 2; sign = -1;
        }
        if (!JUKEBOX_NEIGHBOR_FULL(0, 0, 1) && 1.0 - fz < nearest) {
            nearest = 1.0 - fz; axis = 2; sign = 1;
        }
        if (!JUKEBOX_NEIGHBOR_FULL(0, 1, 0) && 1.0 - fy < nearest) {
            axis = 1; sign = 1;
        }
#undef JUKEBOX_NEIGHBOR_FULL
        if (axis == 0) {
            motion_x = (double)((float)sign * 0.1f);
            motion_z *= 0.75;
            /* Pre-compensate for gm_live_tick's gravity subtraction. */
            motion_y =
                (0.20000000298023224 - 0.03999999910593033) * 0.75
                + 0.03999999910593033;
        } else if (axis == 1) {
            motion_x *= 0.75;
            motion_y = (double)((float)sign * 0.1f)
                + 0.03999999910593033;
            motion_z *= 0.75;
        } else {
            motion_x *= 0.75;
            motion_y =
                (0.20000000298023224 - 0.03999999910593033) * 0.75
                + 0.03999999910593033;
            motion_z = (double)((float)sign * 0.1f);
        }
    } else {
        motion_y = 0.20000000298023224;
    }
    eid = r->next_entity_id++;
    if (!gm_live_spawn_item_exact(
            &r->entities, eid, spawn_x, spawn_y, spawn_z,
            motion_x, motion_y, motion_z, yaw,
            record.item, 1, 0, 0, 10, 0)) {
        --r->next_entity_id;
        return 0;
    }
    gm_world_set_block_meta(r->world, x, y, z, 84, 0);
    (void)gm_runtime_static_container_set_slot(
        r, r->dimension, x, y, z, 0, 0, 0, 0);
    return 1;
}

static const GmRuntimeFurnace *runtime_furnace_at(
        const GmRuntime *r, int x, int y, int z) {
    int id;
    if (!r) return NULL;
    id = gm_world_block(r->world, x, y, z);
    if (id != 61 && id != 62) return NULL;
    for (int i = 0; i < GM_RUNTIME_FURNACES; ++i) {
        const GmRuntimeFurnace *furnace = &r->furnaces[i];
        if (furnace->active && furnace->wx == x
                && furnace->wy == y && furnace->wz == z)
            return furnace;
    }
    return NULL;
}

static int runtime_static_container_size_for_block(int block) {
    if (block == 23 || block == 158) return 9;
    if (block == 117 || block == 154) return 5;
    if (block == 84) return 1;
    if (runtime_is_shulker_box(block)) return 27;
    return 0;
}

static int runtime_entity_eid_active(const GmRuntime *r, int eid) {
    if (!r || eid <= 0) return 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (r->entities.ents[i].active
                && r->entities.ents[i].eid == eid)
            return 1;
    return 0;
}

static GmRuntimeTaggedItem *runtime_tagged_item_reserve(GmRuntime *r) {
    int free_slot = -1;
    if (!r) return NULL;
    for (int i = 0; i < r->tagged_items_cap; ++i) {
        GmRuntimeTaggedItem *tagged = &r->tagged_items[i];
        if (tagged->active
                && !runtime_entity_eid_active(r, tagged->eid)) {
            gm_nbt_blob_clear(&tagged->tag);
            memset(tagged, 0, sizeof *tagged);
        }
        if (!tagged->active && free_slot < 0)
            free_slot = i;
    }
    if (free_slot < 0) {
        int old_cap = r->tagged_items_cap;
        int new_cap = old_cap > 0
            ? old_cap * 2 : GM_RUNTIME_TAGGED_ITEMS_INITIAL;
        GmRuntimeTaggedItem *grown;
        if (new_cap > GM_RUNTIME_TAGGED_ITEMS_MAX)
            new_cap = GM_RUNTIME_TAGGED_ITEMS_MAX;
        if (new_cap <= old_cap)
            return NULL;
        grown = (GmRuntimeTaggedItem *)realloc(
            r->tagged_items, (size_t)new_cap * sizeof *grown);
        if (!grown) return NULL;
        memset(grown + old_cap, 0,
               (size_t)(new_cap - old_cap) * sizeof *grown);
        r->tagged_items = grown;
        r->tagged_items_cap = new_cap;
        free_slot = old_cap;
    }
    return &r->tagged_items[free_slot];
}

static void runtime_tagged_item_fill_shulker(
        GmRuntimeTaggedItem *tagged, int eid, int item,
        const GmRuntimeStaticContainer *container) {
    if (!tagged) return;
    gm_nbt_blob_clear(&tagged->tag);
    memset(tagged, 0, sizeof *tagged);
    tagged->active = 1;
    tagged->eid = eid;
    tagged->item = item;
    tagged->size = GM_RUNTIME_STATIC_CONTAINER_SLOTS;
    for (int slot = 0; slot < tagged->size; ++slot)
        tagged->slots[slot] = container
            ? container->slots[slot] : ic_empty();
}

int gm_runtime_tagged_item_get_by_eid(
        const GmRuntime *r, int eid, GmRuntimeTaggedItem *out) {
    if (!r || !r->tagged_items || !out || eid <= 0)
        return 0;
    for (int i = 0; i < r->tagged_items_cap; ++i) {
        const GmRuntimeTaggedItem *tagged = &r->tagged_items[i];
        if (tagged->active && tagged->eid == eid) {
            *out = *tagged;
            return 1;
        }
    }
    return 0;
}

static const GmRuntimeStaticContainer *runtime_static_container_at(
        const GmRuntime *r, int x, int y, int z) {
    int block;
    if (!r || !r->static_containers)
        return NULL;
    block = gm_world_block(r->world, x, y, z);
    if (runtime_static_container_size_for_block(block) == 0)
        return NULL;
    for (int i = 0; i < r->static_containers_cap; ++i) {
        const GmRuntimeStaticContainer *container =
            &r->static_containers[i];
        if (container->active
                && container->dimension == r->dimension
                && container->wx == x && container->wy == y
                && container->wz == z && container->block == block
                && container->size
                    == runtime_static_container_size_for_block(block))
            return container;
    }
    return NULL;
}

static const GmRuntimeCommandBlock *runtime_command_block_at(
        const GmRuntime *r, int x, int y, int z) {
    int block;
    if (!r || !r->command_blocks)
        return NULL;
    block = gm_world_block(r->world, x, y, z);
    if (!runtime_is_command_block(block))
        return NULL;
    for (int i = 0; i < r->command_blocks_cap; ++i) {
        const GmRuntimeCommandBlock *command = &r->command_blocks[i];
        if (command->active
                && command->dimension == r->dimension
                && command->wx == x && command->wy == y
                && command->wz == z && command->block == block)
            return command;
    }
    return NULL;
}

static const GmRuntimeItemFrame *runtime_item_frame_at(
        const GmRuntime *r, int x, int y, int z, int facing) {
    const GmRuntimeItemFrame *found = NULL;
    int matches = 0;
    if (!r || !r->item_frames || facing < 2 || facing > 5)
        return NULL;
    for (int i = 0; i < r->item_frames_cap; ++i) {
        const GmRuntimeItemFrame *frame = &r->item_frames[i];
        if (!frame->active || frame->dimension != r->dimension
                || frame->hanging_x != x || frame->hanging_y != y
                || frame->hanging_z != z || frame->facing != facing
                || !runtime_item_frame_surface_valid(
                    r, x, y, z, facing))
            continue;
        found = frame;
        matches++;
    }
    return matches == 1 ? found : NULL;
}

static int runtime_chest_pair_at(
        const GmRuntime *r, int x, int y, int z,
        const GmRuntimeChest **pair) {
    static const int dx[4] = {0, 1, 0, -1};
    static const int dz[4] = {-1, 0, 1, 0};
    int adjacent_count = 0;
    const GmRuntimeChest *found = NULL;
    int block_id;
    if (pair) *pair = NULL;
    if (!r) return 0;
    block_id = gm_world_block(r->world, x, y, z);
    if (!runtime_is_chest_block(block_id)) return 0;
    for (int face = 0; face < 4; ++face) {
        int adjacent_x = x + dx[face];
        int adjacent_z = z + dz[face];
        if (gm_world_block(
                r->world, adjacent_x, y, adjacent_z) != block_id)
            continue;
        adjacent_count++;
        found = runtime_chest_tile_at(
            r, adjacent_x, y, adjacent_z);
    }
    if (pair && adjacent_count == 1)
        *pair = found;
    return adjacent_count;
}

enum {
    RUNTIME_HOPPER_INV_NONE = 0,
    RUNTIME_HOPPER_INV_STATIC = 1,
    RUNTIME_HOPPER_INV_CHEST = 2,
    RUNTIME_HOPPER_INV_FURNACE = 3
};

typedef struct {
    int kind, size;
    int x, y, z;
    GmRuntimeStaticContainer *container;
    GmRuntimeChest *chest[2];
    GmRuntimeFurnace *furnace;
} RuntimeHopperInventory;

static int runtime_hopper_inventory_resolve(
        GmRuntime *r, int x, int y, int z,
        RuntimeHopperInventory *inventory) {
    const GmRuntimeChest *pair = NULL;
    int block;
    if (!r || !inventory || y < 0 || y > 255)
        return 0;
    memset(inventory, 0, sizeof *inventory);
    inventory->x = x;
    inventory->y = y;
    inventory->z = z;
    block = gm_world_block(r->world, x, y, z);
    if ((block == 23 || block == 158 || block == 117 || block == 154
            || runtime_is_shulker_box(block))) {
        inventory->container = (GmRuntimeStaticContainer *)
            runtime_static_container_at(r, x, y, z);
        if (!inventory->container)
            return 0;
        inventory->kind = RUNTIME_HOPPER_INV_STATIC;
        inventory->size = inventory->container->size;
        return 1;
    }
    if (runtime_is_chest_block(block)) {
        GmRuntimeChest *current = (GmRuntimeChest *)
            runtime_chest_tile_at(r, x, y, z);
        int adjacent = runtime_chest_pair_at(r, x, y, z, &pair);
        if (!current || adjacent > 1 || (adjacent == 1 && !pair))
            return 0;
        inventory->kind = RUNTIME_HOPPER_INV_CHEST;
        inventory->size = adjacent == 1 ? 54 : 27;
        if (adjacent == 0) {
            inventory->chest[0] = current;
        } else {
            GmRuntimeChest *other = (GmRuntimeChest *)pair;
            /* BlockChest places west/north before the addressed half and
             * east/south after it when constructing InventoryLargeChest. */
            int other_first = other->wx < x || other->wz < z;
            inventory->chest[other_first ? 1 : 0] = current;
            inventory->chest[other_first ? 0 : 1] = other;
        }
        return 1;
    }
    if (block == 61 || block == 62) {
        inventory->furnace = (GmRuntimeFurnace *)
            runtime_furnace_at(r, x, y, z);
        if (!inventory->furnace)
            return 0;
        inventory->kind = RUNTIME_HOPPER_INV_FURNACE;
        inventory->size = FURNACE_LIVE_SLOT_COUNT;
        return 1;
    }
    return 0;
}

static ICStack runtime_hopper_inventory_get(
        const RuntimeHopperInventory *inventory, int slot) {
    if (!inventory || slot < 0 || slot >= inventory->size)
        return ic_empty();
    if (inventory->kind == RUNTIME_HOPPER_INV_STATIC)
        return inventory->container->slots[slot];
    if (inventory->kind == RUNTIME_HOPPER_INV_CHEST) {
        GmRuntimeChest *chest = inventory->chest[slot / 27];
        return chest ? chest_live_get(&chest->state, slot % 27) : ic_empty();
    }
    if (inventory->kind == RUNTIME_HOPPER_INV_FURNACE) {
        const FurnaceLive *furnace = &inventory->furnace->state;
        SRStack stack = slot == 0 ? furnace->input
            : slot == 1 ? furnace->fuel : furnace->output;
        return sr_isEmpty(stack)
            ? ic_empty() : ic_mk(stack.item, stack.count, stack.meta);
    }
    return ic_empty();
}

static void runtime_hopper_inventory_set(
        RuntimeHopperInventory *inventory, int slot, ICStack stack) {
    if (!inventory || slot < 0 || slot >= inventory->size)
        return;
    if (inventory->kind == RUNTIME_HOPPER_INV_STATIC) {
        inventory->container->slots[slot] = stack;
    } else if (inventory->kind == RUNTIME_HOPPER_INV_CHEST) {
        GmRuntimeChest *chest = inventory->chest[slot / 27];
        if (chest)
            chest_live_set(&chest->state, slot % 27, stack);
    } else if (inventory->kind == RUNTIME_HOPPER_INV_FURNACE) {
        SRStack converted = isr_is_empty(&stack)
            ? sr_empty() : sr_mk(stack.item, stack.count, stack.meta);
        if (slot == 0) inventory->furnace->state.input = converted;
        else if (slot == 1) inventory->furnace->state.fuel = converted;
        else inventory->furnace->state.output = converted;
    }
}

static int runtime_hopper_inventory_empty(
        const RuntimeHopperInventory *inventory) {
    for (int slot = 0; slot < inventory->size; ++slot) {
        ICStack stack = runtime_hopper_inventory_get(inventory, slot);
        if (!isr_is_empty(&stack))
            return 0;
    }
    return 1;
}

static int runtime_hopper_slot_for_face(
        const RuntimeHopperInventory *inventory, int slot, int side) {
    if (!inventory || slot < 0 || slot >= inventory->size || side < 0)
        return 1;
    if (inventory->kind == RUNTIME_HOPPER_INV_FURNACE) {
        if (side == 0) return slot == 2 || slot == 1;
        if (side == 1) return slot == 0;
        return slot == 1;
    }
    if (inventory->kind == RUNTIME_HOPPER_INV_STATIC
            && inventory->container->block == 117) {
        if (side == 1) return slot == 3;
        if (side == 0) return slot >= 0 && slot <= 3;
        return slot == 0 || slot == 1 || slot == 2 || slot == 4;
    }
    return 1;
}

static int runtime_hopper_can_insert(
        const RuntimeHopperInventory *inventory, int slot,
        const ICStack *stack, int side) {
    if (!runtime_hopper_slot_for_face(inventory, slot, side)
            || !stack || isr_is_empty(stack))
        return 0;
    if (inventory->kind == RUNTIME_HOPPER_INV_FURNACE) {
        if (slot == 2) return 0;
        if (slot == 1) {
            SRStack fuel = sr_mk(stack->item, stack->count, stack->meta);
            return sr_getItemBurnTime(fuel) > 0
                || stack->item == IC_BUCKET;
        }
        return 1;
    }
    if (inventory->kind == RUNTIME_HOPPER_INV_STATIC) {
        int block = inventory->container->block;
        if (runtime_is_shulker_box(block)
                && stack->item >= 219 && stack->item <= 234)
            return 0;
        if (block == 117) {
            if (slot >= 0 && slot <= 2
                    && !isr_is_empty(&inventory->container->slots[slot]))
                return 0;
            return brewing_live_slot_valid(slot, stack);
        }
    }
    return 1;
}

static int runtime_hopper_can_extract(
        const RuntimeHopperInventory *inventory, int slot,
        const ICStack *stack, int side) {
    if (!runtime_hopper_slot_for_face(inventory, slot, side)
            || !stack || isr_is_empty(stack))
        return 0;
    if (inventory->kind == RUNTIME_HOPPER_INV_FURNACE
            && side == 0 && slot == 1)
        return stack->item == IC_BUCKET || stack->item == IC_WATER_BUCKET;
    if (inventory->kind == RUNTIME_HOPPER_INV_STATIC
            && inventory->container->block == 117 && slot == 3)
        return stack->item == TB_GLASS_BOTTLE;
    return 1;
}

static int runtime_hopper_insert_stack(
        RuntimeHopperInventory *source,
        RuntimeHopperInventory *destination,
        ICStack *moving, int side) {
    int was_empty;
    if (!destination || !moving || isr_is_empty(moving))
        return 0;
    was_empty = runtime_hopper_inventory_empty(destination);
    for (int slot = 0; slot < destination->size
            && !isr_is_empty(moving); ++slot) {
        ICStack current;
        int limit, accepted;
        if (!runtime_hopper_can_insert(destination, slot, moving, side))
            continue;
        current = runtime_hopper_inventory_get(destination, slot);
        limit = tec_max_stack_size(moving->item);
        if (limit > 64) limit = 64;
        if (isr_is_empty(&current)) {
            accepted = moving->count < limit ? moving->count : limit;
            current = ic_with_count(moving, accepted);
        } else {
            if (!ic_stack_equal(&current, moving)
                    || current.count >= limit)
                continue;
            accepted = limit - current.count;
            if (accepted > moving->count) accepted = moving->count;
            current.count += accepted;
        }
        if (accepted <= 0)
            continue;
        moving->count -= accepted;
        if (moving->count <= 0)
            *moving = ic_empty();
        runtime_hopper_inventory_set(destination, slot, current);
    }
    if (was_empty && destination->kind == RUNTIME_HOPPER_INV_STATIC
            && destination->container->block == 154
            && !runtime_hopper_inventory_empty(destination)
            && destination->container->transfer_cooldown <= 8) {
        int offset = 0;
        if (source && source->kind == RUNTIME_HOPPER_INV_STATIC
                && source->container->block == 154
                && destination->container->ticked_game_time
                    >= source->container->ticked_game_time)
            offset = 1;
        destination->container->transfer_cooldown = 8 - offset;
    }
    return isr_is_empty(moving);
}

static int runtime_hopper_transfer_one(
        RuntimeHopperInventory *source, int source_slot, int source_side,
        RuntimeHopperInventory *destination, int destination_side) {
    ICStack original = runtime_hopper_inventory_get(source, source_slot);
    ICStack one;
    if (!runtime_hopper_can_extract(
            source, source_slot, &original, source_side))
        return 0;
    one = ic_with_count(&original, 1);
    if (!runtime_hopper_insert_stack(
            source, destination, &one, destination_side))
        return 0;
    if (--original.count <= 0)
        original = ic_empty();
    runtime_hopper_inventory_set(source, source_slot, original);
    return 1;
}

static int runtime_hopper_transfer_out(
        GmRuntime *r, RuntimeHopperInventory *hopper) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    RuntimeHopperInventory destination;
    int facing = gm_world_meta(
        r->world, hopper->x, hopper->y, hopper->z) & 7;
    if (facing < 0 || facing > 5
            || !runtime_hopper_inventory_resolve(
                r, hopper->x + dx[facing], hopper->y + dy[facing],
                hopper->z + dz[facing], &destination))
        return 0;
    for (int slot = 0; slot < hopper->size; ++slot)
        if (runtime_hopper_transfer_one(
                hopper, slot, -1, &destination, facing ^ 1)) {
            runtime_redstone_update_comparator_output_level(
                r, hopper->x, hopper->y, hopper->z);
            runtime_redstone_update_comparator_output_level(
                r, destination.x, destination.y, destination.z);
            return 1;
        }
    return 0;
}

static int runtime_hopper_capture_inventory(
        GmRuntime *r, RuntimeHopperInventory *hopper) {
    RuntimeHopperInventory source;
    if (!runtime_hopper_inventory_resolve(
            r, hopper->x, hopper->y + 1, hopper->z, &source))
        return 0;
    for (int slot = 0; slot < source.size; ++slot)
        if (runtime_hopper_transfer_one(
                &source, slot, 0, hopper, -1)) {
            runtime_redstone_update_comparator_output_level(
                r, source.x, source.y, source.z);
            runtime_redstone_update_comparator_output_level(
                r, hopper->x, hopper->y, hopper->z);
            return 1;
        }
    return 0;
}

static ICStack runtime_hopper_item_stack(const GmLiveEnt *entity) {
    ICStack stack = ic_mk(entity->item, entity->count, entity->meta);
    int count = entity->n_enchants;
    if (count > IC_MAX_ENCHANTS) count = IC_MAX_ENCHANTS;
    stack.n_enchants = count;
    for (int i = 0; i < count; ++i) {
        stack.enchants[i].id = entity->ench_id[i];
        stack.enchants[i].level = entity->ench_lvl[i];
    }
    return stack;
}

static int runtime_hopper_capture_items(
        GmRuntime *r, RuntimeHopperInventory *hopper) {
    McAABB capture = mc_aabb_make(
        (double)hopper->x, (double)hopper->y + 0.5,
        (double)hopper->z, (double)hopper->x + 1.0,
        (double)hopper->y + 2.0, (double)hopper->z + 1.0);
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        GmLiveEnt *entity = &r->entities.ents[slot];
        McAABB box;
        ICStack moving;
        if (!entity->active || entity->type != 0)
            continue;
        box = mc_aabb_make(
            entity->x - 0.125, entity->y, entity->z - 0.125,
            entity->x + 0.125, entity->y + 0.25, entity->z + 0.125);
        if (box.maxX <= capture.minX || box.minX >= capture.maxX
                || box.maxY <= capture.minY || box.minY >= capture.maxY
                || box.maxZ <= capture.minZ || box.minZ >= capture.maxZ)
            continue;
        moving = runtime_hopper_item_stack(entity);
        (void)runtime_hopper_insert_stack(NULL, hopper, &moving, -1);
        entity->count = moving.count;
        if (isr_is_empty(&moving)) {
            entity->active = 0;
            entity->n_enchants = 0;
            if (r->entities.n_active > 0)
                --r->entities.n_active;
            runtime_redstone_update_comparator_output_level(
                r, hopper->x, hopper->y, hopper->z);
            return 1;
        }
    }
    return 0;
}

/* Exact first BlockDropper slice. TileEntityDispenser's static slot RNG is
 * observationally irrelevant when exactly one slot is occupied. A target
 * inventory uses the same sided insertion routine as TileEntityHopper and
 * consumes one item only when that complete one-item stack was accepted. */
static int runtime_dropper_supported(
        GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    RuntimeHopperInventory source, destination;
    int occupied = 0;
    int facing;
    if (!r || gm_world_block(r->world, x, y, z) != 158
            || !runtime_hopper_inventory_resolve(r, x, y, z, &source))
        return 0;
    facing = gm_world_meta(r->world, x, y, z) & 7;
    if (facing > 5 || !runtime_hopper_inventory_resolve(
            r, x + dx[facing], y + dy[facing], z + dz[facing],
            &destination))
        return 0;
    for (int slot = 0; slot < source.size; ++slot)
        if (!isr_is_empty(&source.container->slots[slot]))
            ++occupied;
    return occupied == 1;
}

static void runtime_tick_dropper(GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    RuntimeHopperInventory source, destination;
    int facing = gm_world_meta(r->world, x, y, z) & 7;
    if (!runtime_dropper_supported(r, x, y, z)
            || !runtime_hopper_inventory_resolve(r, x, y, z, &source)
            || !runtime_hopper_inventory_resolve(
                r, x + dx[facing], y + dy[facing], z + dz[facing],
                &destination))
        return;
    for (int slot = 0; slot < source.size; ++slot) {
        ICStack original = source.container->slots[slot];
        ICStack one;
        if (isr_is_empty(&original)) continue;
        one = ic_with_count(&original, 1);
        if (runtime_hopper_insert_stack(
                &source, &destination, &one, facing ^ 1)) {
            if (--original.count <= 0) original = ic_empty();
            source.container->slots[slot] = original;
            runtime_redstone_update_comparator_output_level(r, x, y, z);
            runtime_redstone_update_comparator_output_level(
                r, destination.x, destination.y, destination.z);
        }
        return;
    }
}

enum {
    RUNTIME_DISPENSE_DEFAULT = 1,
    RUNTIME_DISPENSE_ARROW,
    RUNTIME_DISPENSE_POTION,
    RUNTIME_DISPENSE_FIRE_CHARGE,
    RUNTIME_DISPENSE_FIREWORK,
    RUNTIME_DISPENSE_BOAT,
    RUNTIME_DISPENSE_BUCKET,
    RUNTIME_DISPENSE_TNT,
    RUNTIME_DISPENSE_THROWABLE,
    RUNTIME_DISPENSE_FLINT
};

static int runtime_dispenser_behavior(const ICStack *stack) {
    if (!stack || isr_is_empty(stack)) return 0;
    if (stack->item == 1 && stack->meta == 0)
        return RUNTIME_DISPENSE_DEFAULT;
    if (stack->item == SHL_ARROW && stack->meta == 0)
        return RUNTIME_DISPENSE_ARROW;
    if ((stack->item == 344 || stack->item == 332 || stack->item == 384)
            && stack->meta == 0)
        return RUNTIME_DISPENSE_THROWABLE;
    if ((stack->item == TB_SPLASH_POTION
                || stack->item == TB_LINGERING_POTION)
            && stack->meta >= TB_PT_EMPTY && stack->meta < TB_PT_COUNT)
        return RUNTIME_DISPENSE_POTION;
    if (stack->item == 385 && stack->meta == 0)
        return RUNTIME_DISPENSE_FIRE_CHARGE;
    if (stack->item == 401
            && ic_firework_flight(stack) <= 3
            && ic_firework_explosions(stack) <= 8)
        return RUNTIME_DISPENSE_FIREWORK;
    if (stack->item == 333 && stack->meta == 0)
        return RUNTIME_DISPENSE_BOAT;
    if ((stack->item == IC_WATER_BUCKET
                || stack->item == IC_LAVA_BUCKET)
            && stack->meta == 0 && stack->count == 1)
        return RUNTIME_DISPENSE_BUCKET;
    if (stack->item == 46 && stack->meta == 0)
        return RUNTIME_DISPENSE_TNT;
    if (stack->item == 259 && stack->count == 1
            && stack->meta >= 0 && stack->meta <= 64
            && stack->n_enchants == 0)
        return RUNTIME_DISPENSE_FLINT;
    return 0;
}

/* The static TileEntityDispenser RNG is process-global and deliberately not
 * serialized by Java. Until that independently-seeded cursor is part of a
 * save capsule, live promotion is restricted to one occupied slot. */
static int runtime_dispenser_supported(
        GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    RuntimeHopperInventory source;
    int occupied = 0, behavior = 0;
    int facing, tx, ty, tz;
    if (!r || gm_world_block(r->world, x, y, z) != 23
            || (!runtime_static_container_at(r, x, y, z)
                && !runtime_generated_dispenser_ensure(r, x, y, z))
            || !runtime_hopper_inventory_resolve(r, x, y, z, &source)
            )
        return 0;
    facing = gm_world_meta(r->world, x, y, z) & 7;
    if (facing > 5)
        return 0;
    for (int slot = 0; slot < source.size; ++slot) {
        ICStack stack = source.container->slots[slot];
        if (isr_is_empty(&stack)) continue;
        behavior = runtime_dispenser_behavior(&stack);
        if (!behavior)
            return 0;
        ++occupied;
    }
    if (occupied != 1) return 0;
    tx = x + dx[facing];
    ty = y + dy[facing];
    tz = z + dz[facing];
    if (behavior == RUNTIME_DISPENSE_ARROW
            || behavior == RUNTIME_DISPENSE_POTION
            || behavior == RUNTIME_DISPENSE_THROWABLE
            || behavior == RUNTIME_DISPENSE_FIRE_CHARGE) {
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (!r->projectiles[i].active) return 1;
        return 0;
    }
    if (behavior == RUNTIME_DISPENSE_FIREWORK)
        return r->firework_count < GM_RUNTIME_FIREWORKS;
    if (behavior == RUNTIME_DISPENSE_TNT)
        return r->primed_tnt_count < GM_RUNTIME_PRIMED_TNT;
    if (behavior == RUNTIME_DISPENSE_FLINT)
        return gm_world_block(r->world, tx, ty, tz) != 46
            || r->primed_tnt_count < GM_RUNTIME_PRIMED_TNT;
    if (behavior == RUNTIME_DISPENSE_BUCKET)
        return gm_world_block(r->world, tx, ty, tz) == 0;
    if (behavior == RUNTIME_DISPENSE_BOAT) {
        int target = gm_world_block(r->world, tx, ty, tz);
        return runtime_is_water(target)
            || (target == 0
                && runtime_is_water(gm_world_block(r->world, tx, ty - 1, tz)));
    }
    return r->entities.n_active < GM_LIVE_MAX
        && gm_world_block(r->world, tx, ty, tz) == 0;
}

static void runtime_tick_dispenser(GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    RuntimeHopperInventory source;
    JavaGaussianRandom random;
    int facing = gm_world_meta(r->world, x, y, z) & 7;
    int slot = -1, behavior;
    double spawn_x, spawn_y, spawn_z;
    double speed, mx, my, mz;
    float hover, yaw;
    if (!runtime_dispenser_supported(r, x, y, z)
            || !runtime_hopper_inventory_resolve(r, x, y, z, &source))
        return;
    for (int i = 0; i < source.size; ++i)
        if (!isr_is_empty(&source.container->slots[i])) {
            slot = i;
            break;
        }
    if (slot < 0) return;
    behavior = runtime_dispenser_behavior(&source.container->slots[slot]);
    spawn_x = (double)x + 0.5 + 0.7 * (double)dx[facing];
    spawn_y = (double)y + 0.5 + 0.7 * (double)dy[facing]
        - (facing <= 1 ? 0.125 : 0.15625);
    spawn_z = (double)z + 0.5 + 0.7 * (double)dz[facing];
    if (behavior == RUNTIME_DISPENSE_ARROW
            || behavior == RUNTIME_DISPENSE_POTION
            || behavior == RUNTIME_DISPENSE_THROWABLE) {
        GmRuntimeProjectile *projectile = NULL;
        JavaGaussianRandom arrow_random;
        double hx = (double)dx[facing];
        double hy = (double)((float)dy[facing] + 0.1f);
        double hz = (double)dz[facing];
        double length = sqrt(hx * hx + hy * hy + hz * hz);
        int eid = r->next_entity_id;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (!r->projectiles[i].active) {
                projectile = &r->projectiles[i];
                break;
            }
        if (!projectile || length <= 0.0) return;
        if (behavior == RUNTIME_DISPENSE_ARROW) {
            hx /= length; hy /= length; hz /= length;
            jrand_gaussian_set(&arrow_random,
                runtime_entity_constructor_seed(
                    r, eid, UINT64_C(0x444953504152524F)));
            hx += jrand_gaussian_next(&arrow_random)
                * 0.007499999832361937 * 6.0;
            hy += jrand_gaussian_next(&arrow_random)
                * 0.007499999832361937 * 6.0;
            hz += jrand_gaussian_next(&arrow_random)
                * 0.007499999832361937 * 6.0;
        } else {
            EbfVector heading;
            runtime_potion_random(r, eid, &arrow_random);
            float velocity = 1.375F;
            float inaccuracy = 3.0F;
            if (behavior == RUNTIME_DISPENSE_THROWABLE
                    && source.container->slots[slot].item != 384) {
                velocity = 1.1F;
                inaccuracy = 6.0F;
            }
            heading = ebf_throwable_heading(
                &arrow_random, hx, hy, hz, velocity, inaccuracy);
            hx = heading.x; hy = heading.y; hz = heading.z;
        }
        memset(projectile, 0, sizeof *projectile);
        projectile->active = 1;
        projectile->type = behavior == RUNTIME_DISPENSE_ARROW ? 1
            : behavior == RUNTIME_DISPENSE_POTION ? 6
            : source.container->slots[slot].item == 344 ? 7
            : source.container->slots[slot].item == 332 ? 8 : 9;
        projectile->eid = eid;
        projectile->x = spawn_x;
        projectile->y = (double)y + 0.5 + 0.7 * (double)dy[facing];
        projectile->z = spawn_z;
        projectile->vx = behavior == RUNTIME_DISPENSE_ARROW ? hx * 1.1 : hx;
        projectile->vy = behavior == RUNTIME_DISPENSE_ARROW ? hy * 1.1 : hy;
        projectile->vz = behavior == RUNTIME_DISPENSE_ARROW ? hz * 1.1 : hz;
        if (behavior == RUNTIME_DISPENSE_POTION) {
            projectile->potion_item = source.container->slots[slot].item;
            projectile->potion_type = source.container->slots[slot].meta;
        } else if (behavior == RUNTIME_DISPENSE_THROWABLE) {
            projectile->potion_item = source.container->slots[slot].item;
        }
        projectile->yaw = (float)(runtime_java_math_atan2(
            projectile->vx, projectile->vz) * (180.0 / MC_PI));
        projectile->pitch = (float)(runtime_java_math_atan2(
            projectile->vy,
            sqrt(projectile->vx * projectile->vx
               + projectile->vz * projectile->vz)) * (180.0 / MC_PI));
        ++r->next_entity_id;
        if (--source.container->slots[slot].count <= 0)
            source.container->slots[slot] = ic_empty();
        runtime_world_event_append(r, 1002, x, y, z, 0);
        runtime_world_event_append(
            r, 2000, x, y, z,
            dx[facing] + 1 + (dz[facing] + 1) * 3);
        runtime_redstone_update_comparator_output_level(r, x, y, z);
        return;
    }
    if (behavior == RUNTIME_DISPENSE_FIRE_CHARGE) {
        GmRuntimeProjectile *projectile = NULL;
        JavaGaussianRandom world_random, constructor_random;
        EbfVector acceleration;
        double ax, ay, az;
        int eid = r->next_entity_id;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (!r->projectiles[i].active) {
                projectile = &r->projectiles[i];
                break;
            }
        if (!projectile) return;
        jrand_gaussian_set_state(
            &world_random, r->world_random_seed48,
            r->world_random_have_gaussian, r->world_random_gaussian);
        ax = jrand_gaussian_next(&world_random) * 0.05 + dx[facing];
        ay = jrand_gaussian_next(&world_random) * 0.05 + dy[facing];
        az = jrand_gaussian_next(&world_random) * 0.05 + dz[facing];
        r->world_random_seed48 = world_random.random.seed;
        r->world_random_have_gaussian = world_random.have_next_next_gaussian;
        r->world_random_gaussian = world_random.next_next_gaussian;
        runtime_fireball_random(r, eid, &constructor_random);
        acceleration = ebf_small_fireball_acceleration(
            &constructor_random, ax, ay, az);
        memset(projectile, 0, sizeof *projectile);
        projectile->active = 1;
        projectile->type = 3;
        projectile->eid = eid;
        projectile->x = spawn_x + (double)((float)dx[facing] * 0.3F);
        projectile->y = (double)y + 0.5 + 0.7 * (double)dy[facing]
            + (double)((float)dy[facing] * 0.3F);
        projectile->z = spawn_z + (double)((float)dz[facing] * 0.3F);
        projectile->ax = acceleration.x;
        projectile->ay = acceleration.y;
        projectile->az = acceleration.z;
        ++r->next_entity_id;
        if (--source.container->slots[slot].count <= 0)
            source.container->slots[slot] = ic_empty();
        runtime_world_event_append(r, 1018, x, y, z, 0);
        runtime_world_event_append(
            r, 2000, x, y, z,
            dx[facing] + 1 + (dz[facing] + 1) * 3);
        runtime_redstone_update_comparator_output_level(r, x, y, z);
        return;
    }
    if (behavior == RUNTIME_DISPENSE_FIREWORK) {
        ICStack stack = source.container->slots[slot];
        if (gm_runtime_spawn_firework_payload(
                r, (double)x + 0.5 + dx[facing],
                (double)((float)y + 0.2F),
                (double)z + 0.5 + dz[facing],
                ic_firework_flight(&stack),
                ic_firework_explosions(&stack),
                ic_firework_large(&stack),
                ic_firework_flicker(&stack), 0) < 0)
            return;
        if (--source.container->slots[slot].count <= 0)
            source.container->slots[slot] = ic_empty();
        runtime_world_event_append(r, 1004, x, y, z, 0);
        runtime_world_event_append(
            r, 2000, x, y, z,
            dx[facing] + 1 + (dz[facing] + 1) * 3);
        runtime_redstone_update_comparator_output_level(r, x, y, z);
        return;
    }
    if (behavior == RUNTIME_DISPENSE_TNT) {
        if (!runtime_tnt_prime(
                r, x + dx[facing], y + dy[facing], z + dz[facing]))
            return;
        if (--source.container->slots[slot].count <= 0)
            source.container->slots[slot] = ic_empty();
        runtime_world_event_append(r, 1000, x, y, z, 0);
        runtime_world_event_append(
            r, 2000, x, y, z,
            dx[facing] + 1 + (dz[facing] + 1) * 3);
        runtime_redstone_update_comparator_output_level(r, x, y, z);
        return;
    }
    if (behavior == RUNTIME_DISPENSE_FLINT) {
        ICStack *stack = &source.container->slots[slot];
        int tx = x + dx[facing], ty = y + dy[facing], tz = z + dz[facing];
        int target = gm_world_block(r->world, tx, ty, tz);
        int successful = 1;
        if (target == 0) {
            if (!gm_runtime_set_block(r, tx, ty, tz, 51, 0)) return;
            if (++stack->meta > 64) *stack = ic_empty();
        } else if (target == 46) {
            if (!runtime_tnt_prime(r, tx, ty, tz)
                    || !gm_runtime_set_block(r, tx, ty, tz, 0, 0))
                return;
        } else {
            successful = 0;
        }
        runtime_world_event_append(
            r, successful ? 1000 : 1001, x, y, z, 0);
        runtime_world_event_append(
            r, 2000, x, y, z,
            dx[facing] + 1 + (dz[facing] + 1) * 3);
        runtime_redstone_update_comparator_output_level(r, x, y, z);
        return;
    }
    if (behavior == RUNTIME_DISPENSE_BUCKET) {
        int item = source.container->slots[slot].item;
        if (!gm_runtime_set_block(
                r, x + dx[facing], y + dy[facing], z + dz[facing],
                item == IC_WATER_BUCKET ? 8 : 10, 0))
            return;
        source.container->slots[slot] = ic_mk(IC_BUCKET, 1, 0);
        runtime_world_event_append(r, 1000, x, y, z, 0);
        runtime_world_event_append(
            r, 2000, x, y, z,
            dx[facing] + 1 + (dz[facing] + 1) * 3);
        runtime_redstone_update_comparator_output_level(r, x, y, z);
        return;
    }
    if (behavior == RUNTIME_DISPENSE_BOAT) {
        int tx = x + dx[facing], ty = y + dy[facing], tz = z + dz[facing];
        double lift = runtime_is_water(gm_world_block(r->world, tx, ty, tz))
            ? 1.0 : 0.0;
        float boat_yaw = facing == 2 ? 180.0F : facing == 3 ? 0.0F
            : facing == 4 ? 90.0F : facing == 5 ? 270.0F
            : (float)((facing & 3) * 90);
        if (gm_mobs_spawn_boat_exact(
                &r->mobs, r->next_entity_id,
                (double)x + 0.5 + (double)((float)dx[facing] * 1.125F),
                (double)y + 0.5 + (double)((float)dy[facing] * 1.125F)
                    + lift,
                (double)z + 0.5 + (double)((float)dz[facing] * 1.125F),
                boat_yaw) < 0)
            return;
        ++r->next_entity_id;
        if (--source.container->slots[slot].count <= 0)
            source.container->slots[slot] = ic_empty();
        runtime_world_event_append(r, 1000, x, y, z, 0);
        runtime_world_event_append(
            r, 2000, x, y, z,
            dx[facing] + 1 + (dz[facing] + 1) * 3);
        runtime_redstone_update_comparator_output_level(r, x, y, z);
        return;
    }
    hover = (float)(runtime_math_random_next_double(r) * (MC_PI * 2.0));
    yaw = (float)(runtime_math_random_next_double(r) * 360.0);
    (void)runtime_math_random_next_double(r);
    (void)runtime_math_random_next_double(r);
    jrand_gaussian_set_state(
        &random, r->world_random_seed48,
        r->world_random_have_gaussian, r->world_random_gaussian);
    speed = jrand_double(&random.random) * 0.1 + 0.2;
    mx = (double)dx[facing] * speed
        + jrand_gaussian_next(&random) * 0.007499999832361937 * 6.0;
    my = 0.20000000298023224
        + jrand_gaussian_next(&random) * 0.007499999832361937 * 6.0;
    mz = (double)dz[facing] * speed
        + jrand_gaussian_next(&random) * 0.007499999832361937 * 6.0;
    r->world_random_seed48 = random.random.seed;
    r->world_random_have_gaussian = random.have_next_next_gaussian;
    r->world_random_gaussian = random.next_next_gaussian;
    if (!gm_live_spawn_item_exact_hover(
            &r->entities, r->next_entity_id,
            spawn_x, spawn_y, spawn_z, mx, my, mz, yaw, hover,
            1, 1, 0, 0, 0, 0))
        return;
    ++r->next_entity_id;
    if (--source.container->slots[slot].count <= 0)
        source.container->slots[slot] = ic_empty();
    runtime_world_event_append(r, 1000, x, y, z, 0);
    runtime_world_event_append(
        r, 2000, x, y, z,
        dx[facing] + 1 + (dz[facing] + 1) * 3);
    runtime_redstone_update_comparator_output_level(r, x, y, z);
}

static void runtime_tick_hoppers(GmRuntime *r) {
    if (!r || !r->static_containers)
        return;
    for (int i = 0; i < r->static_containers_cap; ++i) {
        GmRuntimeStaticContainer *tile = &r->static_containers[i];
        RuntimeHopperInventory hopper;
        int changed = 0;
        if (!tile->active || tile->dimension != r->dimension
                || tile->block != 154
                || gm_world_block(
                    r->world, tile->wx, tile->wy, tile->wz) != 154)
            continue;
        tile->transfer_cooldown = (int)(
            (uint32_t)tile->transfer_cooldown - 1u);
        tile->ticked_game_time = r->clock.total_time;
        if (tile->transfer_cooldown > 0)
            continue;
        tile->transfer_cooldown = 0;
        if ((gm_world_meta(
                r->world, tile->wx, tile->wy, tile->wz) & 8) != 0)
            continue;
        if (!runtime_hopper_inventory_resolve(
                r, tile->wx, tile->wy, tile->wz, &hopper))
            continue;
        if (!runtime_hopper_inventory_empty(&hopper))
            changed = runtime_hopper_transfer_out(r, &hopper);
        if (runtime_hopper_inventory_resolve(
                r, tile->wx, tile->wy + 1, tile->wz,
                &(RuntimeHopperInventory){0}))
            changed = runtime_hopper_capture_inventory(r, &hopper) || changed;
        else
            changed = runtime_hopper_capture_items(r, &hopper) || changed;
        if (changed)
            tile->transfer_cooldown = 8;
    }
}

static int runtime_redstone_comparator_override_supported(
        const GmRuntime *r, int x, int y, int z) {
    int id = gm_world_block(r->world, x, y, z);
    if (id == 61 || id == 62)
        return runtime_furnace_at(r, x, y, z) != NULL;
    return runtime_redstone_comparator_override_signal(
        r, x, y, z, NULL);
}

static int runtime_redstone_comparator_input_strength(
        const GmRuntime *r, int x, int y, int z, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int face = runtime_redstone_repeater_face(meta);
    int input_x = x + dx[face];
    int input_z = z + dz[face];
    int input_id = gm_world_block(r->world, input_x, y, input_z);
    int input_meta = gm_world_meta(r->world, input_x, y, input_z);
    int signal = runtime_redstone_comparator_signal_from_neighbor(
        r, input_x, y, input_z, face);
    /* Comparator overrides replace ordinary redstone input. If the immediate
     * input is a normal cube and its signal is below 15, vanilla checks one
     * additional block for an override (then, separately, an item frame). */
    if (runtime_redstone_comparator_override_signal(
            r, input_x, y, input_z, &signal))
        return signal;
    if (signal < 15
            && gm_block_is_normal_cube_1_11_2(input_id, input_meta)) {
        input_x += dx[face];
        input_z += dz[face];
        if (!runtime_redstone_comparator_override_signal(
                r, input_x, y, input_z, &signal)
                && gm_world_block(r->world, input_x, y, input_z) == 0) {
            const GmRuntimeItemFrame *frame =
                runtime_item_frame_at(r, input_x, y, input_z, face);
            if (frame)
                signal = frame->item == 0 ? 0 : frame->rotation + 1;
        }
    }
    return signal;
}

static int runtime_redstone_comparator_side_strength(
        const GmRuntime *r, int x, int y, int z, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int face = runtime_redstone_repeater_face(meta);
    int side_a = face == 2 || face == 3 ? 4 : 2;
    int side_b = face == 2 || face == 3 ? 5 : 3;
    int power_a = runtime_redstone_comparator_signal_from_neighbor(
        r, x + dx[side_a], y, z + dz[side_a], side_a);
    int power_b = runtime_redstone_comparator_signal_from_neighbor(
        r, x + dx[side_b], y, z + dz[side_b], side_b);
    return power_a > power_b ? power_a : power_b;
}

static int runtime_redstone_comparator_calculate_output(
        const GmRuntime *r, int x, int y, int z, int meta) {
    int input =
        runtime_redstone_comparator_input_strength(r, x, y, z, meta);
    if ((meta & 4) != 0) {
        int side =
            runtime_redstone_comparator_side_strength(r, x, y, z, meta);
        return input > side ? input - side : 0;
    }
    return input;
}

/* Exact rear-input, static block overrides, one-solid look-through, and
 * dust/redstone-block side-input slice. Piston moving blocks and settled
 * heads are valid non-normal, non-powering rear inputs: they contribute zero
 * while a destroyed comparator source is replaced and settled. Restored
 * ordinary/trapped single/double chests, furnaces, dispensers, droppers, and
 * a single exact item frame are admitted as saved-state sources; other
 * containers/entities remain separately proof-gated. */
static int runtime_redstone_comparator_supported(
        const GmRuntime *r, int x, int y, int z, int block) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int meta;
    int face;
    int output_face;
    if ((block != 149 && block != 150)
            || gm_world_block(r->world, x, y, z) != block
            || y <= 0 || y >= 255
            || !gm_block_is_fully_opaque_1_11_2(
                gm_world_block(r->world, x, y - 1, z),
                gm_world_meta(r->world, x, y - 1, z))
            || gm_world_block(r->world, x, y + 1, z) != 0
            || !runtime_comparator_find(
                r, r->dimension, x, y, z))
        return 0;
    meta = gm_world_meta(r->world, x, y, z);
    face = runtime_redstone_repeater_face(meta);
    output_face = face == 2 ? 3 : face == 3 ? 2 : face == 4 ? 5 : 4;
    for (int i = 0; i < 4; ++i) {
        static const int horizontal[4] = {2, 3, 4, 5};
        int neighbor_face = horizontal[i];
        int id = gm_world_block(
            r->world, x + dx[neighbor_face], y, z + dz[neighbor_face]);
        int neighbor_meta = gm_world_meta(
            r->world, x + dx[neighbor_face], y,
            z + dz[neighbor_face]);
        if (neighbor_face == face) {
            if (id != 0 && id != 29 && id != 33
                    && id != 34 && id != 36
                    && id != 54 && id != 55 && id != 61 && id != 62
                    && id != 69 && id != 70
                    && id != 72 && id != 75 && id != 76 && id != 77
                    && id != 92 && id != 93 && id != 94 && id != 120
                    && id != 143 && id != 146
                    && id != 118 && id != 147 && id != 148 && id != 149
                    && id != 150 && id != 152 && id != 218
                    && !gm_block_is_normal_cube_1_11_2(id, neighbor_meta))
                return 0;
            if ((id == 54 || id == 61 || id == 62 || id == 146
                        || id == 92 || id == 118 || id == 120)
                    && !runtime_redstone_comparator_override_supported(
                        r, x + dx[neighbor_face], y,
                        z + dz[neighbor_face]))
                return 0;
            if (gm_block_is_normal_cube_1_11_2(id, neighbor_meta)
                    && runtime_redstone_comparator_signal_from_neighbor(
                        r, x + dx[neighbor_face], y,
                        z + dz[neighbor_face], neighbor_face) < 15
                    && !runtime_redstone_comparator_override_supported(
                        r, x + dx[neighbor_face], y,
                        z + dz[neighbor_face])
                    && !runtime_redstone_comparator_override_supported(
                        r, x + 2 * dx[neighbor_face], y,
                        z + 2 * dz[neighbor_face])
                    && runtime_item_frame_at(
                        r, x + 2 * dx[neighbor_face], y,
                        z + 2 * dz[neighbor_face], neighbor_face) == NULL)
                return 0;
        } else if (neighbor_face == output_face) {
            if (id != 0 && id != 29 && id != 33
                    && id != 55 && id != 93 && id != 94
                    && id != 123 && id != 124 && id != 149 && id != 150
                    && id != 218
                    && !gm_block_is_normal_cube_1_11_2(id, neighbor_meta))
                return 0;
        } else if (id != 0 && id != 29 && id != 33
                && id != 55 && id != 146 && id != 152 && id != 218
                && !gm_block_is_normal_cube_1_11_2(id, neighbor_meta)) {
            return 0;
        }
    }
    return 1;
}

static int runtime_redstone_comparator_tick_pending(
        const GmRuntime *r, int x, int y, int z) {
    for (int i = 0; i < r->scheduled_tick_count; ++i) {
        const GmRuntimeScheduledTick *entry = &r->scheduled_ticks[i];
        if (entry->x == x && entry->y == y && entry->z == z
                && (entry->block == 149 || entry->block == 150))
            return 1;
    }
    return 0;
}

static int runtime_redstone_comparator_should_power(
        int input, int side) {
    return input >= 15 || (input > 0 && (side == 0 || input >= side));
}

static void runtime_redstone_comparator_update_state(
        GmRuntime *r, int x, int y, int z, int block) {
    int meta;
    int input;
    int side;
    int output;
    int current_output;
    int should_power;
    int powered;
    int priority = 0;
    int face;
    int output_face;
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    if (!runtime_redstone_comparator_supported(r, x, y, z, block)
            || runtime_redstone_comparator_tick_pending(r, x, y, z))
        return;
    meta = gm_world_meta(r->world, x, y, z);
    input = runtime_redstone_comparator_input_strength(r, x, y, z, meta);
    side = runtime_redstone_comparator_side_strength(r, x, y, z, meta);
    output = runtime_redstone_comparator_calculate_output(
        r, x, y, z, meta);
    current_output = runtime_redstone_comparator_output(r, x, y, z);
    should_power = runtime_redstone_comparator_should_power(input, side);
    powered = runtime_redstone_comparator_powered(block, meta);
    if (output == current_output && powered == should_power)
        return;
    face = runtime_redstone_repeater_face(meta);
    output_face = face == 2 ? 3 : face == 3 ? 2 : face == 4 ? 5 : 4;
    {
        int output_id = gm_world_block(
            r->world, x + dx[output_face], y, z + dz[output_face]);
        int output_meta = gm_world_meta(
            r->world, x + dx[output_face], y, z + dz[output_face]);
        if ((output_id == 93 || output_id == 94
                || output_id == 149 || output_id == 150)
                && runtime_redstone_repeater_face(output_meta)
                    != output_face)
            priority = -1;
    }
    runtime_schedule_tick_insert(
        r, x, y, z, block, r->clock.total_time + 2, priority,
        r->scheduled_tick_next_order);
}

static void runtime_redstone_comparator_dispatch(
        GmRuntime *r, int x, int y, int z, int block) {
    GmRuntimeComparator *entry =
        runtime_comparator_find_mut(r, r->dimension, x, y, z);
    int meta = gm_world_meta(r->world, x, y, z);
    int input;
    int side;
    int output;
    int old_output;
    int should_power;
    if (!entry
            || !runtime_redstone_comparator_supported(
                r, x, y, z, block))
        return;
    input = runtime_redstone_comparator_input_strength(r, x, y, z, meta);
    side = runtime_redstone_comparator_side_strength(r, x, y, z, meta);
    output = runtime_redstone_comparator_calculate_output(
        r, x, y, z, meta);
    old_output = entry->output_signal;
    entry->output_signal = output;
    /* Vanilla compare mode notifies on every accepted callback. Subtract
     * mode performs no powered-state transition when its analog output is
     * unchanged, even if shouldBePowered differs. */
    if (old_output != output || (meta & 4) == 0) {
        int new_meta;
        should_power =
            runtime_redstone_comparator_should_power(input, side);
        new_meta = should_power ? (meta | 8) : (meta & ~8);
        if (new_meta != meta) {
            gm_world_set_block_meta(r->world, x, y, z, 149, new_meta);
            runtime_redstone_update_observers_at(r, x, y, z);
        }
        meta = new_meta;
        runtime_redstone_repeater_notify_output(r, x, y, z, meta);
    }
}

/* A saved lamp callback is proof-safe when every power-providing state in its
 * direct/indirect neighborhood belongs to the represented producer set.
 * Ordinary vanilla blocks are admitted from the captured canProvidePower
 * predicate; unknown producers and post-1.11/custom IDs remain rejected. */
static int runtime_redstone_power_provider_supported(
        const GmRuntime *r, int x, int y, int z) {
    int id = gm_world_block(r->world, x, y, z);
    int meta = gm_world_meta(r->world, x, y, z);
    if (id < 0 || id >= 256)
        return 0;
    if (!gm_block_can_provide_power_1_11_2(id, meta))
        return 1;
    return id == 55 || id == 69 || id == 70 || id == 72
        || id == 75 || id == 76 || id == 77 || id == 93 || id == 94
        || id == 28 || id == 131
        || id == 143 || id == 149 || id == 150
        || id == 146 || id == 147 || id == 148 || id == 151 || id == 152
        || id == 178
        || id == 218;
}

static int runtime_redstone_lamp_off_supported(
        const GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    if (gm_world_block(r->world, x, y, z) != 124)
        return 0;
    if (runtime_redstone_is_powered(r, x, y, z))
        return 1;
    for (int face = 0; face < 6; ++face) {
        int nx = x + dx[face], ny = y + dy[face], nz = z + dz[face];
        int id = gm_world_block(r->world, nx, ny, nz);
        int meta = gm_world_meta(r->world, nx, ny, nz);
        if (!runtime_redstone_power_provider_supported(r, nx, ny, nz))
            return 0;
        if (!gm_block_is_normal_cube_1_11_2(id, meta))
            continue;
        for (int strong_face = 0; strong_face < 6; ++strong_face) {
            if (!runtime_redstone_power_provider_supported(
                    r, nx + dx[strong_face], ny + dy[strong_face],
                    nz + dz[strong_face]))
                return 0;
        }
    }
    return 1;
}

static int runtime_redstone_torch_support_offset(
    int meta, int *dx, int *dy, int *dz);
static void runtime_redstone_torch_notify_adjacent_neighbors(
    GmRuntime *r, int x, int y, int z);

/* Forge Block.isSideSolid is the attachment predicate used by BlockTorch.
 * It is deliberately not equivalent to normal-cube: half slabs, stair
 * orientation and actual shape, snow layers, farmland, hoppers, and the
 * powered compressed block all have direction-specific overrides. */
static int runtime_redstone_side_solid_1_11_2(
        const GmRuntime *r, int x, int y, int z, int side) {
    int id;
    int meta;
    if (y < 0 || y > 255)
        return 1; /* World.isSideSolid(..., true) default. */
    id = gm_world_block(r->world, x, y, z);
    meta = gm_world_meta(r->world, x, y, z);
    if (gm_block_is_fully_opaque_1_11_2(id, meta) && side == 1)
        return 1;
    if (id == 43 || id == 125 || id == 181 || id == 204)
        return 1; /* Double BlockSlab states are full blocks. */
    if (id == 44 || id == 126 || id == 182 || id == 205)
        return ((meta & 8) != 0 && side == 1)
            || ((meta & 8) == 0 && side == 0);
    if (id == 60)
        return side != 0 && side != 1;
    if (runtime_is_stair_id(id)) {
        int top = (meta & 4) != 0;
        if (side == 1)
            return top;
        if (side == 0)
            return !top;
        return runtime_stair_side_solid(r, x, y, z, meta, side);
    }
    if (id == 78)
        return (meta & 7) == 7;
    if (id == 154 && side == 1)
        return 1;
    if (id == 152)
        return 1;
    return gm_block_is_normal_cube_1_11_2(id, meta);
}

static int runtime_redstone_torch_support_valid(
        const GmRuntime *r, int x, int y, int z, int meta) {
    int support_dx, support_dy, support_dz;
    int sx, sy, sz;
    int support;
    if (!runtime_redstone_torch_support_offset(
            meta, &support_dx, &support_dy, &support_dz))
        return 0;
    sx = x + support_dx;
    sy = y + support_dy;
    sz = z + support_dz;
    if (meta != 5)
        return runtime_redstone_side_solid_1_11_2(
            r, sx, sy, sz, runtime_redstone_control_facing(76, meta));
    if (runtime_redstone_side_solid_1_11_2(r, sx, sy, sz, 1))
        return 1;
    support = gm_world_block(r->world, sx, sy, sz);
    return runtime_is_fence_id(support)
        || support == 20 || support == 95 || support == 139;
}

/* BlockRedstoneTorch.shouldBeOff asks the attached block for power on the
 * attachment face. Normal supports forward represented strong power from
 * their neighbors; a redstone block provides power directly. */
static int runtime_redstone_torch_should_be_off(
        const GmRuntime *r, int x, int y, int z, int meta) {
    int support_dx, support_dy, support_dz;
    int sx, sy, sz;
    int support;
    int support_meta;
    if (!runtime_redstone_torch_support_offset(
            meta, &support_dx, &support_dy, &support_dz))
        return 0;
    sx = x + support_dx;
    sy = y + support_dy;
    sz = z + support_dz;
    support = gm_world_block(r->world, sx, sy, sz);
    support_meta = gm_world_meta(r->world, sx, sy, sz);
    if (support == 152)
        return 1;
    return gm_block_is_normal_cube_1_11_2(support, support_meta)
        && runtime_redstone_normal_strong_power(
            r, sx, sy, sz, 1) > 0;
}

static int runtime_redstone_control_support_valid(
        const GmRuntime *r, int x, int y, int z, int block, int meta,
        int *support_x, int *support_y, int *support_z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int face = runtime_redstone_control_facing(block, meta);
    int sx, sy, sz;
    if (face < 0 || face >= 6)
        return 0;
    sx = x - dx[face];
    sy = y - dy[face];
    sz = z - dz[face];
    if (support_x) *support_x = sx;
    if (support_y) *support_y = sy;
    if (support_z) *support_z = sz;
    return runtime_redstone_side_solid_1_11_2(
        r, sx, sy, sz, face);
}

static int runtime_redstone_pressure_plate_support_valid(
        const GmRuntime *r, int x, int y, int z) {
    int id = gm_world_block(r->world, x, y - 1, z);
    int meta = gm_world_meta(r->world, x, y - 1, z);
    return gm_block_is_fully_opaque_1_11_2(id, meta)
        || runtime_is_fence_id(id);
}

static int runtime_redstone_diode_support_valid(
        const GmRuntime *r, int x, int y, int z) {
    return y > 0 && gm_block_is_fully_opaque_1_11_2(
        gm_world_block(r->world, x, y - 1, z),
        gm_world_meta(r->world, x, y - 1, z));
}

static int runtime_redstone_tripwire_hook_support_valid(
        const GmRuntime *r, int x, int y, int z, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int face = runtime_redstone_tripwire_hook_face(meta);
    int sx = x - dx[face];
    int sz = z - dz[face];
    return runtime_redstone_side_solid_1_11_2(
        r, sx, y, sz, face);
}

static int runtime_tnt_prime(GmRuntime *r, int x, int y, int z) {
    GmRuntimePrimedTnt *tnt = NULL;
    float angle;
    if (r->primed_tnt_count >= GM_RUNTIME_PRIMED_TNT)
        return 0;
    for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i)
        if (!r->primed_tnt[i].active) {
            tnt = &r->primed_tnt[i];
            break;
        }
    if (!tnt)
        return 0;
    memset(tnt, 0, sizeof *tnt);
    tnt->active = 1;
    tnt->dimension = r->dimension;
    /* Entity's base constructor allocates the process-global ID before the
     * EntityTNTPrimed constructor consumes Math.random. */
    tnt->eid = r->next_entity_id++;
    tnt->fuse = 80;
    tnt->x = (double)x + 0.5;
    tnt->y = (double)y;
    tnt->z = (double)z + 0.5;
    angle = (float)(runtime_math_random_next_double(r) * (MC_PI * 2.0));
    tnt->vx = (double)(-((float)sin((double)angle)) * 0.02f);
    tnt->vy = 0.20000000298023224;
    tnt->vz = (double)(-((float)cos((double)angle)) * 0.02f);
    ++r->primed_tnt_count;
    return 1;
}

static void runtime_redstone_neighbor_changed(
        GmRuntime *r, int x, int y, int z) {
    int id = gm_world_block(r->world, x, y, z);
    if (id == 122) {
        /* BlockDragonEgg.neighborChanged defers checkFall by five ticks.
         * Duplicate insertion preserves the first due time. */
        (void)runtime_schedule_tick_insert(
            r, x, y, z, 122, r->clock.total_time + 5, 0,
            r->scheduled_tick_next_order);
    } else if (id == 145) {
        /* BlockAnvil inherits BlockFalling's two-tick neighbor callback. */
        (void)runtime_schedule_tick_insert(
            r, x, y, z, 145, r->clock.total_time + 2, 0,
            r->scheduled_tick_next_order);
    } else if (id == 55) {
        /* BlockRedstoneWire.neighborChanged drops one redstone item before
         * replacing dust whose support is no longer valid with air. Keep the
         * fixed item-pool boundary atomic with respect to the wire and RNG. */
        if (y > 0 && runtime_redstone_wire_support_supported(
                r, x, y - 1, z)) {
            runtime_redstone_update_wire_component(r, x, y, z);
        } else if (r->entities.n_active < GM_LIVE_MAX
                && runtime_redstone_piston_spawn_item_stack(
                    r, x, y, z, 331, 0)) {
            gm_world_set_block_meta(r->world, x, y, z, 0, 0);
            runtime_redstone_notify_neighbors(r, x, y, z);
            runtime_redstone_update_observers_at(r, x, y, z);
        }
    } else if (id == 46 && runtime_redstone_is_powered(r, x, y, z)) {
        if (runtime_tnt_prime(r, x, y, z)) {
            gm_world_set_block_meta(r->world, x, y, z, 0, 0);
            runtime_redstone_notify_neighbors(r, x, y, z);
            runtime_redstone_update_observers_at(r, x, y, z);
        }
    } else if (id == 93 || id == 94 || id == 149 || id == 150) {
        int meta = gm_world_meta(r->world, x, y, z);
        int comparator = id == 149 || id == 150;
        if (!runtime_redstone_diode_support_valid(r, x, y, z)) {
            if (runtime_redstone_piston_apply_destroy_payload(
                    r, x, y, z, id, meta)) {
                /* Comparator breakBlock retires its tile before notifying
                 * through the old block; repeater output teardown sees air. */
                if (comparator) {
                    runtime_comparator_remove(
                        r, r->dimension, x, y, z);
                    runtime_redstone_repeater_notify_output(
                        r, x, y, z, meta);
                }
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
                if (!comparator)
                    runtime_redstone_repeater_notify_output(
                        r, x, y, z, meta);
                runtime_redstone_update_observers_at(r, x, y, z);
                runtime_redstone_torch_notify_adjacent_neighbors(
                    r, x, y, z);
            }
        } else if (id == 93 || id == 94) {
            runtime_redstone_repeater_update_state(r, x, y, z, id);
        } else {
            runtime_redstone_comparator_update_state(r, x, y, z, id);
        }
    } else if (id == 131) {
        int meta = gm_world_meta(r->world, x, y, z);
        if (!runtime_redstone_tripwire_hook_support_valid(
                r, x, y, z, meta)
                && runtime_redstone_piston_apply_destroy_payload(
                    r, x, y, z, id, meta)) {
            gm_world_set_block_meta(r->world, x, y, z, 0, 0);
            runtime_redstone_tripwire_hook_break_state(
                r, x, y, z, meta);
            runtime_redstone_notify_neighbors(r, x, y, z);
            runtime_redstone_update_observers_at(r, x, y, z);
        }
    } else if (runtime_redstone_is_pressure_plate(id)) {
        int meta = gm_world_meta(r->world, x, y, z);
        if (!runtime_redstone_pressure_plate_support_valid(r, x, y, z)
                && runtime_redstone_piston_apply_destroy_payload(
                    r, x, y, z, id, meta)) {
            gm_world_set_block_meta(r->world, x, y, z, 0, 0);
            runtime_redstone_notify_neighbors(r, x, y, z);
            runtime_redstone_update_observers_at(r, x, y, z);
            if (meta > 0)
                runtime_redstone_notify_neighbors(r, x, y - 1, z);
        }
    } else if (id == 69 || id == 77 || id == 143) {
        int support_x, support_y, support_z;
        int meta = gm_world_meta(r->world, x, y, z);
        if (!runtime_redstone_control_support_valid(
                r, x, y, z, id, meta,
                &support_x, &support_y, &support_z)
                && runtime_redstone_piston_apply_destroy_payload(
                    r, x, y, z, id, meta)) {
            gm_world_set_block_meta(r->world, x, y, z, 0, 0);
            runtime_redstone_notify_neighbors(r, x, y, z);
            runtime_redstone_update_observers_at(r, x, y, z);
            if ((meta & 8) != 0)
                runtime_redstone_notify_neighbors(
                    r, support_x, support_y, support_z);
        }
    } else if (id == 75 || id == 76) {
        int support_dx, support_dy, support_dz;
        int meta = gm_world_meta(r->world, x, y, z);
        if (!runtime_redstone_torch_support_offset(
                meta, &support_dx, &support_dy, &support_dz))
            return;
        if (!runtime_redstone_torch_support_valid(r, x, y, z, meta)) {
            if (runtime_redstone_piston_apply_destroy_payload(
                    r, x, y, z, id, meta)) {
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
                runtime_redstone_notify_neighbors(r, x, y, z);
                runtime_redstone_update_observers_at(r, x, y, z);
                if (id == 76)
                    runtime_redstone_torch_notify_adjacent_neighbors(
                        r, x, y, z);
            }
            return;
        }
        int should_be_off = runtime_redstone_torch_should_be_off(
            r, x, y, z, meta);
        if ((id == 76) == should_be_off)
            runtime_schedule_tick_insert(
                r, x, y, z, id, r->clock.total_time + 2, 0,
                r->scheduled_tick_next_order);
    } else if (id == 123 && runtime_redstone_is_powered(r, x, y, z)) {
        gm_world_set_block_meta(r->world, x, y, z, 124, 0);
        runtime_redstone_update_observers_at(r, x, y, z);
    } else if (id == 124
            && !runtime_redstone_is_powered(r, x, y, z)) {
        /* BlockRedstoneLight deliberately stays lit for four ticks. */
        runtime_schedule_tick_insert(
            r, x, y, z, 124, r->clock.total_time + 4, 0,
            r->scheduled_tick_next_order);
    } else if (id == 23 || id == 158) {
        int meta = gm_world_meta(r->world, x, y, z);
        int powered = runtime_redstone_is_powered(r, x, y, z);
        int triggered = (meta & 8) != 0;
        if (powered && !triggered) {
            if ((id == 23 && runtime_dispenser_supported(r, x, y, z))
                    || (id == 158
                        && runtime_dropper_supported(r, x, y, z)))
                (void)runtime_schedule_tick_insert(
                    r, x, y, z, id, r->clock.total_time + 4, 0,
                    r->scheduled_tick_next_order);
            gm_world_set_block_meta(
                r->world, x, y, z, id, meta | 8);
            runtime_redstone_update_observers_at(r, x, y, z);
        } else if (!powered && triggered) {
            gm_world_set_block_meta(
                r->world, x, y, z, id, meta & 7);
            runtime_redstone_update_observers_at(r, x, y, z);
        }
    } else if (id == 60) {
        int above_id = gm_world_block(r->world, x, y + 1, z);
        int above_meta = gm_world_meta(r->world, x, y + 1, z);
        /* BlockFarmland.neighborChanged turns the state into default dirt
         * whenever the material above is solid. The predicate is captured
         * from the live 1.11.2 registry because normal/full-cube is not an
         * equivalent test (the moving-piston state is the relevant proof). */
        if (gm_block_material_is_solid_1_11_2(above_id, above_meta)) {
            gm_world_set_block_meta(r->world, x, y, z, 3, 0);
            runtime_redstone_notify_neighbors(r, x, y, z);
            runtime_redstone_update_observers_at(r, x, y, z);
        }
    } else if (id == 83) {
        int below = gm_world_block(r->world, x, y - 1, z);
        int can_stay = below == 83;
        if (!can_stay && (below == 2 || below == 3 || below == 12)) {
            static const int dx[4] = {-1, 1, 0, 0};
            static const int dz[4] = {0, 0, -1, 1};
            for (int face = 0; face < 4 && !can_stay; ++face) {
                int neighbor = gm_world_block(
                    r->world, x + dx[face], y - 1, z + dz[face]);
                can_stay = neighbor == 8 || neighbor == 9
                    || neighbor == 212;
            }
        }
        /* BlockReed.checkForDrop calls dropBlockAsItem before setBlockToAir.
         * The air write then notifies the next reed above, recursively
         * collapsing a column in the same neighbor boundary. */
        if (!can_stay && runtime_redstone_piston_spawn_item_stack(
                r, x, y, z, 338, 0)) {
            gm_world_set_block_meta(r->world, x, y, z, 0, 0);
            runtime_redstone_notify_neighbors(r, x, y, z);
            runtime_redstone_update_observers_at(r, x, y, z);
        }
    } else if (id == 81) {
        /* BlockCactus.destroyBlock drops metadata zero before the air write.
         * That write notifies the block above, recursively collapsing the
         * rest of a cactus column in the same neighbor boundary. */
        if (!runtime_redstone_cactus_can_stay(r, x, y, z)
                && runtime_redstone_piston_spawn_item_stack(
                    r, x, y, z, 81, 0)) {
            gm_world_set_block_meta(r->world, x, y, z, 0, 0);
            runtime_redstone_notify_neighbors(r, x, y, z);
            runtime_redstone_update_observers_at(r, x, y, z);
        }
    } else if (id == 96 || id == 167) {
        int meta = gm_world_meta(r->world, x, y, z);
        int powered = runtime_redstone_is_powered(r, x, y, z);
        int new_meta = powered ? (meta | 4) : (meta & ~4);
        if (new_meta != meta) {
            gm_world_set_block_meta(r->world, x, y, z, id, new_meta);
            runtime_redstone_update_observers_at(r, x, y, z);
        }
    } else if (runtime_is_fence_gate_id(id)) {
        int meta = gm_world_meta(r->world, x, y, z);
        int powered = runtime_redstone_is_powered(r, x, y, z);
        if (powered != ((meta & 8) != 0)) {
            int new_meta = powered ? (meta | 12) : (meta & ~12);
            gm_world_set_block_meta(r->world, x, y, z, id, new_meta);
            runtime_redstone_update_observers_at(r, x, y, z);
        }
    } else if (runtime_is_door_block(id)) {
        int meta = gm_world_meta(r->world, x, y, z);
        int is_upper = (meta & 8) != 0;
        int pair_y = y + (is_upper ? -1 : 1);
        if (gm_world_block(r->world, x, pair_y, z) != id) {
            if (is_upper) {
                /* BlockDoor's upper half owns no item. A missing lower half
                 * removes it with the ordinary setBlockToAir notification. */
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
                runtime_redstone_notify_neighbors(r, x, y, z);
                runtime_redstone_update_observers_at(r, x, y, z);
            } else if (r->entities.n_active < GM_LIVE_MAX) {
                /* The lower half owns the type-specific ItemDoor. Java first
                 * sets it to air, then invokes dropBlockAsItem. */
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
                runtime_redstone_notify_neighbors(r, x, y, z);
                runtime_redstone_update_observers_at(r, x, y, z);
                (void)runtime_redstone_piston_apply_destroy_payload(
                    r, x, y, z, id, meta);
            }
        } else {
            int lower_y = is_upper ? pair_y : y;
            int upper_y = lower_y + 1;
            int lower_meta = gm_world_meta(r->world, x, lower_y, z);
            int upper_meta = gm_world_meta(r->world, x, upper_y, z);
            int powered = runtime_redstone_is_powered(
                    r, x, lower_y, z)
                || runtime_redstone_is_powered(r, x, upper_y, z);
            if (powered != ((upper_meta & 2) != 0)) {
                gm_world_set_block_meta(
                    r->world, x, upper_y, z, id,
                    powered ? (upper_meta | 2) : (upper_meta & ~2));
                runtime_redstone_update_observers_at(
                    r, x, upper_y, z);
                if (powered != ((lower_meta & 4) != 0)) {
                    gm_world_set_block_meta(
                        r->world, x, lower_y, z, id,
                        powered ? (lower_meta | 4) : (lower_meta & ~4));
                    runtime_redstone_update_observers_at(
                        r, x, lower_y, z);
                }
            }
        }
    } else if (id == 26) {
        static const int facing_dx[4] = {0, -1, 0, 1};
        static const int facing_dz[4] = {1, 0, -1, 0};
        int meta = gm_world_meta(r->world, x, y, z);
        int facing = meta & 3;
        int is_head = (meta & 8) != 0;
        int pair_x = x + (is_head ? -1 : 1) * facing_dx[facing];
        int pair_z = z + (is_head ? -1 : 1) * facing_dz[facing];
        if (gm_world_block(r->world, pair_x, y, pair_z) != 26) {
            if (is_head) {
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
                runtime_redstone_notify_neighbors(r, x, y, z);
                runtime_redstone_update_observers_at(r, x, y, z);
            } else if (r->entities.n_active < GM_LIVE_MAX) {
                /* Foot neighborChanged calls setBlockToAir before its bed
                 * drop. Capacity is preflighted only to preserve Magma's
                 * fixed-pool atomicity; notification order remains vanilla. */
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
                runtime_redstone_notify_neighbors(r, x, y, z);
                runtime_redstone_update_observers_at(r, x, y, z);
                (void)runtime_redstone_piston_apply_destroy_payload(
                    r, x, y, z, 26, meta);
            }
        }
    } else if (id == 175) {
        int meta = gm_world_meta(r->world, x, y, z);
        if ((meta & 8) != 0) {
            /* An upper half never drops. Its flag-2 air write also avoids a
             * second neighbor pass after the lower half has disappeared. */
            if (gm_world_block(r->world, x, y - 1, z) != 175)
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
        } else {
            int below = gm_world_block(r->world, x, y - 1, z);
            int upper_is_plant =
                gm_world_block(r->world, x, y + 1, z) == 175;
            int soil_ok = below == 2 || below == 3 || below == 60;
            if ((!upper_is_plant || !soil_ok)
                    && runtime_redstone_piston_apply_destroy_payload(
                        r, x, y, z, 175, meta)) {
                if (upper_is_plant)
                    gm_world_set_block_meta(r->world, x, y + 1, z, 0, 0);
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
                runtime_redstone_notify_neighbors(r, x, y, z);
                runtime_redstone_update_observers_at(r, x, y, z);
            }
        }
    } else if (id == 199 && gm_world_meta(r->world, x, y, z) == 0) {
        if (!runtime_redstone_chorus_plant_can_survive(r, x, y, z))
            runtime_schedule_tick_insert(
                r, x, y, z, 199, r->clock.total_time + 1, 0,
                r->scheduled_tick_next_order);
    } else if (id == 200
            && gm_block_meta_canonical_1_11_2(
                200, gm_world_meta(r->world, x, y, z))) {
        /* Chorus flowers defer invalid-support destruction by one tick. The
         * flower's ordinary destroyBlock(true) path has no item payload. */
        if (!runtime_redstone_chorus_flower_can_survive(r, x, y, z))
            runtime_schedule_tick_insert(
                r, x, y, z, 200, r->clock.total_time + 1, 0,
                r->scheduled_tick_next_order);
    } else if (id == 29 || id == 33) {
        runtime_redstone_piston_check(r, x, y, z);
    }
}

/* World.notifyNeighborsOfStateChange order is WEST, EAST, DOWN, UP, NORTH,
 * SOUTH. Keep it explicit: later dust/torch/piston callbacks are order
 * sensitive even though the first lamp proof has only one consumer. */
static void runtime_redstone_notify_neighbors(
        GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {-1, 1, 0, 0, 0, 0};
    static const int dy[6] = {0, 0, -1, 1, 0, 0};
    static const int dz[6] = {0, 0, 0, 0, -1, 1};
    for (int face = 0; face < 6; ++face)
        runtime_redstone_neighbor_changed(
            r, x + dx[face], y + dy[face], z + dz[face]);
}

static void runtime_tick_daylight_detectors(GmRuntime *r) {
    int index = 0;
    if (r->daylight_detector_count == 0
            || r->clock.total_time % 20LL != 0)
        return;
    while (index < r->daylight_detector_count) {
        GmRuntimeDaylightDetector entry = r->daylight_detectors[index];
        int id;
        int meta;
        int power;
        if (entry.dimension != r->dimension) {
            ++index;
            continue;
        }
        id = gm_world_block(r->world, entry.x, entry.y, entry.z);
        if (id != 151 && id != 178) {
            runtime_daylight_detector_remove(
                r, entry.dimension, entry.x, entry.y, entry.z);
            continue;
        }
        meta = gm_world_meta(r->world, entry.x, entry.y, entry.z);
        power = runtime_redstone_daylight_power(
            r, entry.x, entry.y, entry.z, id == 178);
        if (power != meta) {
            gm_world_set_block_meta(
                r->world, entry.x, entry.y, entry.z, id, power);
            runtime_redstone_notify_neighbors(
                r, entry.x, entry.y, entry.z);
            runtime_redstone_update_observers_at(
                r, entry.x, entry.y, entry.z);
        }
        ++index;
    }
}

/*
 * First R-04 piston proof: an unextended piston with empty space in front,
 * powered directly by a neighboring redstone block, powered lever, or
 * powered stone/wooden button, nonzero pressure plate, or directionally
 * emitting lit redstone torch, powered repeater, or powered comparator with a
 * positive tile output, a powered observer on its output face, directional
 * powered dust, or a normal cube receiving represented strong power.
 * BlockPistonBase queues a block event, but WorldServer drains that event in
 * the same server tick. The observable result is therefore an extended base
 * plus a moving-head block before observation zero.
 *
 * The same represented sources are accepted in BlockPistonBase's fixed
 * pos.up() quasi-connectivity neighborhood. Retraction and moving non-air
 * blocks remain outside this guard. Returning without mutation is deliberate:
 * unsupported configurations stay visible to the Java-vs-Magma comparator.
 */
static int runtime_redstone_piston_neighbor_supported_power(
        const GmRuntime *r, int x, int y, int z, int face) {
    int id = gm_world_block(r->world, x, y, z);
    int meta = gm_world_meta(r->world, x, y, z);
    return (gm_block_is_normal_cube_1_11_2(id, meta)
                && runtime_redstone_normal_strong_power(
                    r, x, y, z, 1) > 0)
        || id == 152
        || ((id == 69 || id == 77 || id == 143) && (meta & 8) != 0)
        || (runtime_redstone_is_pressure_plate(id) && meta != 0)
        || (id == 131 && (meta & 8) != 0)
        || ((id == 151 || id == 178) && meta != 0)
        || (id == 76 && runtime_redstone_torch_powers_face(meta, face))
        || (id == 94 && runtime_redstone_repeater_face(meta) == face)
        || ((id == 149 || id == 150)
            && runtime_redstone_comparator_powered(id, meta)
            && runtime_redstone_repeater_face(meta) == face
            && runtime_redstone_comparator_output(r, x, y, z) > 0)
        || (id == 218 && runtime_redstone_observer_power(meta, face) > 0)
        || (id == 55
            && runtime_redstone_wire_weak_power(r, x, y, z, face) > 0);
}

static int runtime_redstone_piston_direct_supported_power(
        const GmRuntime *r, int x, int y, int z, int facing) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    for (int face = 0; face < 6; ++face) {
        if (face != facing
                && runtime_redstone_piston_neighbor_supported_power(
                    r, x + dx[face], y + dy[face], z + dz[face], face))
            return 1;
    }
    /* BlockPistonBase's vanilla quasi-connectivity pass queries every face
     * except DOWN around pos.up(). This is still a fixed five-cell probe and
     * deliberately does not scan for arbitrary powered blocks. */
    for (int face = 1; face < 6; ++face) {
        if (runtime_redstone_piston_neighbor_supported_power(
                r, x + dx[face], y + 1 + dy[face], z + dz[face], face))
            return 1;
    }
    return 0;
}

static int runtime_redstone_piston_can_push_state(
        const GmRuntime *r, int x, int y, int z,
        int facing, int destroy_blocks) {
    int id = gm_world_block(r->world, x, y, z);
    int meta = gm_world_meta(r->world, x, y, z);
    if (id == 0)
        return 1;
    if (id < 0 || id >= 256 || y < 0 || y > 255
            || (facing == 0 && y == 0)
            || (facing == 1 && y == 255)
            || id == 49
            || gm_block_piston_unbreakable_1_11_2(id, meta))
        return 0;
    /* Vanilla handles unextended piston bases before consulting their
     * Material.PISTON BLOCK reaction. */
    if (id == 29 || id == 33)
        return (meta & 8) == 0;
    if (gm_block_piston_block_1_11_2(id, meta))
        return 0;
    if (gm_block_piston_destroy_1_11_2(id, meta))
        return destroy_blocks;
    return !gm_block_has_tile_entity_1_11_2(id, meta);
}

enum {
    RUNTIME_PISTON_DESTROY_UNSUPPORTED = 0,
    RUNTIME_PISTON_DESTROY_NO_ITEMS = 1,
    RUNTIME_PISTON_DESTROY_FILTERED_ITEMS = 2,
    RUNTIME_PISTON_DESTROY_SPAWN_ITEMS = 3,
    RUNTIME_PISTON_DESTROY_RANDOM_COUNT_ITEMS = 4,
    RUNTIME_PISTON_DESTROY_BLOCK_RANDOM_ITEM = 5,
    RUNTIME_PISTON_DESTROY_CROP_ITEMS = 6,
    RUNTIME_PISTON_DESTROY_LEAF_ITEMS = 7,
    RUNTIME_PISTON_DESTROY_WORLD_RANDOM_ITEM = 8,
    RUNTIME_PISTON_DESTROY_MELON_ITEMS = 9,
    RUNTIME_PISTON_DESTROY_STEM_ITEMS = 10,
    RUNTIME_PISTON_DESTROY_NETHER_WART_ITEMS = 11,
    RUNTIME_PISTON_DESTROY_FIXED_ITEMS = 12
};

/* Map the represented DESTROY states to their exact piston drop payload.
 * Flowers preserve BlockFlower damage; cobweb maps to the registered string
 * item; controls/plates/torch/ladder/pumpkin orientation and power are block
 * properties, and Block.damageDropped therefore emits item metadata zero.
 * Both redstone-torch blocks drop the lit torch item, while both repeater
 * blocks drop Items.REPEATER (numeric item ID 356). Both comparator blocks
 * drop Items.COMPARATOR (numeric item ID 404), retire their output tile, and
 * notify the output-side neighborhood from the destroyed orientation. Fire returns
 * quantityDropped=0 and structure void has an empty drop override, so
 * destroying either succeeds without consuming drop RNG or entity capacity.
 * Both flowing/static water and lava likewise have no item form; all sixteen
 * metadata states are DESTROY and consume no drop or entity RNG. Cake's
 * quantityDropped override returns zero and its item override returns air;
 * all seven bite states therefore use the same zero-drop path.
 * Melon consumes nextInt(5), then the fortune-zero nextInt(1), and creates
 * three through seven separate item-360 stacks before ordinary per-stack
 * chance/spawn RNG.
 * Pumpkin/melon stems consume three process-global Block.RANDOM nextInt(15)
 * trials. Every result at most the stem age creates one separate crop-specific
 * seed stack before the ordinary per-stack chance/spawn path.
 * Vine's ordinary piston break is not a shears harvest and emits no item for
 * any attachment mask. Waterlily maps every raw metadata value to its
 * registered block item 111:0 through the ordinary one-stack path.
 * Nether wart ages zero through two emit one item-372 stack. Mature age three
 * first consumes World.rand.nextInt(3) and emits two through four separate
 * stacks before their ordinary chance/spawn RNG.
 * Tripwire hooks preserve no facing/attached/powered metadata in their block
 * item. Tripwire maps every canonical powered/attached/disarmed state to the
 * registered string item 287:0.
 * Forge's piston path gives snow layers chance=-1 to retain vanilla's
 * no-snowball behavior: getDrops still creates meta+2 candidate stacks and
 * each consumes one World.rand chance draw, but none spawn.
 * Dead bush consumes one nextInt(3), then creates that many separate stick
 * stacks; every stack independently consumes the ordinary chance/spawn RNG
 * and one EntityItem/ID.
 * Tall grass/dead shrub/fern canonical metadata 0..2 shares BlockTallGrass's
 * Forge drop override. Block.RANDOM.nextInt(8) rejects seven branches; a
 * successful branch then selects the sole weight-10 wheat-seed entry and its
 * count through nextInt(10) and nextInt(1), before the ordinary World.rand
 * chance/spawn path creates one wheat-seed EntityItem.
 * Wheat ages 0..6 emit one seed stack without a count draw. Mature age 7
 * emits one wheat stack, then consumes three World.rand.nextInt(14) trials;
 * every result <=7 adds one separate seed stack before ordinary per-stack
 * chance/spawn draws. Carrots and potatoes share those three mature-crop
 * trials but use their crop item for both the base and bonus stacks. A mature
 * potato then consumes Block.RANDOM.nextInt(50) and appends one poisonous
 * potato when that independent trial is zero.
 * Old leaves expose four variants across all flag-bit combinations; leaves2
 * exposes acacia/dark-oak across the same flags. Sapling selection consumes
 * nextInt(20), except jungle's nextInt(40). Oak and dark oak then consume an
 * independent nextInt(200) apple roll. All selection happens before the
 * ordinary per-stack chance/spawn draws, in sapling-then-apple order.
 * Reeds preserve none of their age metadata: every canonical state drops the
 * registered reeds item 338:0. Cactus likewise drops item 81:0 rather than
 * its age. A column above either state is handled later by its ordered
 * neighborChanged cascade. Double-plant lower flower variants preserve their
 * variant metadata; the upper half is removed by the same ordered callback
 * without creating a second stack. All seven door blocks likewise drop only
 * from their lower half and map to distinct registered ItemDoor IDs; either
 * targeted half removes the vertical mate through BlockDoor.neighborChanged.
 * Beetroot mirrors the ordinary crop path with age three, beetroot seeds as
 * its immature/base seed drop, and a six-wide mature bonus trial. Other
 * registry DESTROY states remain rejected until their payload and side
 * effects are represented; they are never silently erased. */
static int runtime_redstone_piston_destroy_payload(
        int id, int meta, int *item, int *item_meta, int *quantity) {
    int out_meta;
    if (id == 51 && meta >= 0 && meta <= 15)
        return RUNTIME_PISTON_DESTROY_NO_ITEMS;
    if (id >= 8 && id <= 11 && meta >= 0 && meta <= 15)
        return RUNTIME_PISTON_DESTROY_NO_ITEMS;
    if (id == 92 && meta >= 0 && meta <= 6)
        return RUNTIME_PISTON_DESTROY_NO_ITEMS;
    if (id == 103 && meta >= 0 && meta <= 15) {
        if (item)
            *item = 360;
        if (item_meta)
            *item_meta = 0;
        if (quantity)
            *quantity = 7;
        return RUNTIME_PISTON_DESTROY_MELON_ITEMS;
    }
    if ((id == 104 || id == 105) && meta >= 0 && meta <= 7) {
        if (item)
            *item = id == 104 ? 361 : 362;
        if (item_meta)
            *item_meta = 0;
        if (quantity)
            *quantity = 3;
        return RUNTIME_PISTON_DESTROY_STEM_ITEMS;
    }
    if (id == 106 && meta >= 0 && meta <= 15)
        return RUNTIME_PISTON_DESTROY_NO_ITEMS;
    if (id == 115 && meta >= 0 && meta <= 3) {
        if (item)
            *item = 372;
        if (item_meta)
            *item_meta = 0;
        if (quantity)
            *quantity = 4;
        return RUNTIME_PISTON_DESTROY_NETHER_WART_ITEMS;
    }
    if (id == 127 && meta >= 0 && meta <= 11) {
        if (item)
            *item = 351;
        if (item_meta)
            *item_meta = 3;
        if (quantity)
            *quantity = meta >= 8 ? 3 : 1;
        return RUNTIME_PISTON_DESTROY_FIXED_ITEMS;
    }
    if (id == 217 && meta == 0)
        return RUNTIME_PISTON_DESTROY_NO_ITEMS;
    if (id == 26 && meta >= 8 && meta <= 15)
        return RUNTIME_PISTON_DESTROY_NO_ITEMS;
    if (id == 144 && meta >= 8 && meta <= 13)
        return RUNTIME_PISTON_DESTROY_NO_ITEMS;
    if (runtime_is_door_block(id) && meta >= 8 && meta <= 11)
        return RUNTIME_PISTON_DESTROY_NO_ITEMS;
    if (id == 78 && meta >= 0 && meta <= 7) {
        if (item)
            *item = 332;
        if (item_meta)
            *item_meta = 0;
        if (quantity)
            *quantity = meta + 2;
        return RUNTIME_PISTON_DESTROY_FILTERED_ITEMS;
    }
    if (id == 32 && meta == 0) {
        if (item)
            *item = 280;
        if (item_meta)
            *item_meta = 0;
        if (quantity)
            *quantity = 3;
        return RUNTIME_PISTON_DESTROY_RANDOM_COUNT_ITEMS;
    }
    if (id == 199 && meta == 0) {
        if (item)
            *item = 432;
        if (item_meta)
            *item_meta = 0;
        if (quantity)
            *quantity = 2;
        return RUNTIME_PISTON_DESTROY_RANDOM_COUNT_ITEMS;
    }
    if (id == 200 && meta >= 0 && meta <= 5)
        return RUNTIME_PISTON_DESTROY_NO_ITEMS;
    if (id == 31 && meta >= 0 && meta <= 2) {
        if (item)
            *item = 295;
        if (item_meta)
            *item_meta = 0;
        if (quantity)
            *quantity = 1;
        return RUNTIME_PISTON_DESTROY_BLOCK_RANDOM_ITEM;
    }
    if (id == 175 && meta == 2) {
        if (item)
            *item = 295;
        if (item_meta)
            *item_meta = 0;
        if (quantity)
            *quantity = 1;
        return RUNTIME_PISTON_DESTROY_WORLD_RANDOM_ITEM;
    }
    if (id == 175 && meta >= 8 && meta <= 11)
        return RUNTIME_PISTON_DESTROY_NO_ITEMS;
    if (id == 59 && meta >= 0 && meta <= 7) {
        if (item)
            *item = meta == 7 ? 296 : 295;
        if (item_meta)
            *item_meta = 0;
        if (quantity)
            *quantity = 1;
        return RUNTIME_PISTON_DESTROY_CROP_ITEMS;
    }
    if ((id == 141 || id == 142) && meta >= 0 && meta <= 7) {
        if (item)
            *item = id == 141 ? 391 : 392;
        if (item_meta)
            *item_meta = 0;
        if (quantity)
            *quantity = 1;
        return RUNTIME_PISTON_DESTROY_CROP_ITEMS;
    }
    if (id == 207 && meta >= 0 && meta <= 3) {
        if (item)
            *item = meta == 3 ? 434 : 435;
        if (item_meta)
            *item_meta = 0;
        if (quantity)
            *quantity = 1;
        return RUNTIME_PISTON_DESTROY_CROP_ITEMS;
    }
    if ((id == 18 && meta >= 0 && meta <= 15)
            || (id == 161 && meta >= 0 && meta <= 15
                && (meta & 2) == 0)) {
        if (item)
            *item = 6;
        if (item_meta)
            *item_meta = (meta & 3) + (id == 161 ? 4 : 0);
        if (quantity)
            *quantity = 0;
        return RUNTIME_PISTON_DESTROY_LEAF_ITEMS;
    }
    if (id == 6
            && ((meta >= 0 && meta <= 5)
                || (meta >= 8 && meta <= 13))) {
        out_meta = meta & 7;
    }
    else if (id == 30 && meta == 0) {
        id = 287;
        out_meta = 0;
    }
    else if (id == 37 && meta == 0)
        out_meta = 0;
    else if (id == 38 && meta >= 0 && meta <= 8)
        out_meta = meta;
    else if ((id == 39 || id == 40) && meta == 0)
        out_meta = 0;
    else if (id == 50 && meta >= 1 && meta <= 5)
        out_meta = 0;
    else if (id == 69 && meta >= 0 && meta <= 15)
        out_meta = 0;
    else if ((id == 70 || id == 72) && meta >= 0 && meta <= 1)
        out_meta = 0;
    else if ((id == 75 || id == 76) && meta >= 1 && meta <= 5) {
        id = 76;
        out_meta = 0;
    }
    else if ((id == 77 || id == 143)
            && ((meta >= 0 && meta <= 5)
                || (meta >= 8 && meta <= 13)))
        out_meta = 0;
    else if ((id == 93 || id == 94)
            && meta >= 0 && meta <= 15) {
        id = 356;
        out_meta = 0;
    }
    else if ((id == 149 || id == 150)
            && meta >= 0 && meta <= 15) {
        id = 404;
        out_meta = 0;
    }
    else if ((id == 147 || id == 148)
            && meta >= 0 && meta <= 15)
        out_meta = 0;
    else if (id == 65 && meta >= 2 && meta <= 5)
        out_meta = 0;
    else if ((id == 86 || id == 91) && meta >= 0 && meta <= 3)
        out_meta = 0;
    else if (id == 55 && meta >= 0 && meta <= 15) {
        id = 331;
        out_meta = 0;
    }
    else if (id == 83 && meta >= 0 && meta <= 15) {
        id = 338;
        out_meta = 0;
    }
    else if (id == 81 && meta >= 0 && meta <= 15)
        out_meta = 0;
    else if (id == 111 && meta >= 0 && meta <= 15)
        out_meta = 0;
    else if (id == 122 && meta >= 0 && meta <= 15)
        out_meta = 0;
    else if (id == 131 && meta >= 0 && meta <= 15)
        out_meta = 0;
    else if (id == 132
            && (meta == 0 || meta == 1 || meta == 4 || meta == 5
                || meta == 8 || meta == 9 || meta == 12 || meta == 13)) {
        id = 287;
        out_meta = 0;
    }
    else if (id == 26 && meta >= 0 && meta <= 3) {
        id = 355;
        out_meta = 0;
    }
    else if (id == 140 && meta == 0) {
        id = 390;
        out_meta = 0;
    }
    else if (id == 144 && meta >= 0 && meta <= 5) {
        id = 397;
        out_meta = 0;
    }
    else if (runtime_is_shulker_box(id) && meta >= 0 && meta <= 5)
        out_meta = 0;
    else if (runtime_is_door_block(id) && meta >= 0 && meta <= 7) {
        id = runtime_door_item(id);
        out_meta = 0;
    }
    else if (id == 175
            && (meta == 0 || meta == 1 || meta == 4 || meta == 5))
        out_meta = meta;
    else if (id == 175 && meta == 3)
        return RUNTIME_PISTON_DESTROY_NO_ITEMS;
    else
        return 0;
    if (item)
        *item = id;
    if (item_meta)
        *item_meta = out_meta;
    if (quantity)
        *quantity = 1;
    return RUNTIME_PISTON_DESTROY_SPAWN_ITEMS;
}

static int runtime_redstone_piston_destroy_supported(int id, int meta) {
    return runtime_redstone_piston_destroy_payload(
        id, meta, NULL, NULL, NULL);
}

static int runtime_spawn_item_stack_at_block(
        GmRuntime *r, int x, int y, int z, int item, int item_meta,
        int chance_draw) {
    double spawn_x, spawn_y, spawn_z;
    double motion_x, motion_z;
    float yaw;
    int eid;
    /* Block.dropBlockAsItemWithChance(1): one chance draw, then the three
     * float offsets used by Block.spawnAsEntity. */
    if (chance_draw)
        (void)runtime_java_random_next_float(r);
    spawn_x = (double)x
        + (double)(runtime_java_random_next_float(r) * 0.5f) + 0.25;
    spawn_y = (double)y
        + (double)(runtime_java_random_next_float(r) * 0.5f) + 0.25;
    spawn_z = (double)z
        + (double)(runtime_java_random_next_float(r) * 0.5f) + 0.25;

    /* EntityItem consumes Math.random for hoverStart, yaw, X motion, and Z
     * motion. hoverStart is render-only here but its draw remains causal. */
    (void)runtime_math_random_next_double(r);
    yaw = (float)(runtime_math_random_next_double(r) * 360.0);
    motion_x = (double)(float)(
        runtime_math_random_next_double(r)
            * 0.20000000298023224 - 0.10000000149011612);
    motion_z = (double)(float)(
        runtime_math_random_next_double(r)
            * 0.20000000298023224 - 0.10000000149011612);
    eid = r->next_entity_id++;
    return gm_live_spawn_item_exact(
        &r->entities, eid, spawn_x, spawn_y, spawn_z,
        motion_x, 0.20000000298023224, motion_z, yaw,
        item, 1, item_meta, 0, 10, 0);
}

/* EntityPlayer.dropItem(stack, false): used when an item transform cannot put
 * its result back into a full InventoryPlayer. EntityItem construction first
 * consumes four Math.random draws; dropItem then overwrites its motion from
 * Entity.rand and gives it a 40-tick pickup delay. */
static int runtime_drop_player_stack(GmRuntime *r, ICStack stack) {
    float yaw_radians, pitch_radians, spread_angle, spread;
    double mx, my, mz;
    float hover, yaw;
    if (!r || isr_is_empty(&stack)) return 0;
    hover=(float)(runtime_math_random_next_double(r)*(MC_PI*2.0));
    yaw=(float)(runtime_math_random_next_double(r)*360.0);
    (void)runtime_math_random_next_double(r);
    (void)runtime_math_random_next_double(r);
    yaw_radians=r->player.yaw*0.017453292f;
    pitch_radians=r->player.pitch*0.017453292f;
    mx=(double)(-mc_sin(&r->sin_table,yaw_radians)
        *mc_cos(&r->sin_table,pitch_radians)*0.3f);
    mz=(double)(mc_cos(&r->sin_table,yaw_radians)
        *mc_cos(&r->sin_table,pitch_radians)*0.3f);
    my=(double)(-mc_sin(&r->sin_table,pitch_radians)*0.3f+0.1f);
    spread_angle=jrand_float(&r->mobs.player_random)*(float)(MC_PI*2.0);
    spread=0.02f*jrand_float(&r->mobs.player_random);
    mx+=cos((double)spread_angle)*(double)spread;
    my+=(double)((jrand_float(&r->mobs.player_random)
        -jrand_float(&r->mobs.player_random))*0.1f);
    mz+=sin((double)spread_angle)*(double)spread;
    int eid=r->next_entity_id;
    int ok=gm_live_spawn_item_exact_hover(
        &r->entities,eid,
        r->player.ent.posX+(double)r->ox,
        r->player.ent.posY-0.30000001192092896
            +psv_player_eye_height(&r->player),
        r->player.ent.posZ+(double)r->oz,
        mx,my,mz,yaw,hover,
        stack.item,stack.count,stack.meta,0,40,0);
    if(ok)++r->next_entity_id;
    return ok;
}

static int runtime_redstone_piston_spawn_item_stack(
        GmRuntime *r, int x, int y, int z, int item, int item_meta) {
    return runtime_spawn_item_stack_at_block(
        r, x, y, z, item, item_meta, 1);
}

static int runtime_spawn_shulker_box_item(
        GmRuntime *r, int x, int y, int z, int item,
        const GmRuntimeStaticContainer *container) {
    GmNbtBlob item_tag = {0};
    GmRuntimeTaggedItem *tagged = runtime_tagged_item_reserve(r);
    int eid;
    if (!tagged) return 0;
    if (container && container->item_tag.data
            && !gm_nbt_blob_copy(&item_tag, &container->item_tag))
        return 0;
    eid = r->next_entity_id;
    if (!runtime_spawn_item_stack_at_block(
            r, x, y, z, item, 0, 0)) {
        gm_nbt_blob_clear(&item_tag);
        return 0;
    }
    runtime_tagged_item_fill_shulker(
        tagged, eid, item, container);
    tagged->tag = item_tag;
    return 1;
}

static int runtime_spawn_skull_item(
        GmRuntime *r, int x, int y, int z, int item) {
    const GmRuntimeSkull *skull = runtime_skull_payload(r, x, y, z);
    GmNbtBlob tag = {0};
    GmRuntimeTaggedItem *tagged;
    int eid;
    if (!skull)
        return runtime_redstone_piston_spawn_item_stack(
            r, x, y, z, item, 0);
    if (!skull->owner_profile.data || skull->owner_profile.len == 0)
        return runtime_redstone_piston_spawn_item_stack(
            r, x, y, z, item, skull->type);
    if (skull->type != 3
            || !gm_nbt_blob_wrap_named_compound(
                &tag, "SkullOwner", &skull->owner_profile))
        return 0;
    tagged = runtime_tagged_item_reserve(r);
    if (!tagged) {
        gm_nbt_blob_clear(&tag);
        return 0;
    }
    eid = r->next_entity_id;
    if (!runtime_spawn_item_stack_at_block(
            r, x, y, z, item, skull->type, 1)) {
        gm_nbt_blob_clear(&tag);
        return 0;
    }
    gm_nbt_blob_clear(&tagged->tag);
    memset(tagged, 0, sizeof *tagged);
    tagged->active = 1;
    tagged->eid = eid;
    tagged->item = item;
    tagged->tag = tag;
    return 1;
}

static int runtime_redstone_piston_apply_destroy_payload(
        GmRuntime *r, int x, int y, int z, int id, int meta) {
    int payload;
    int quantity;
    int item;
    int item_meta;
    payload = runtime_redstone_piston_destroy_payload(
        id, meta, &item, &item_meta, &quantity);
    if (payload == RUNTIME_PISTON_DESTROY_NO_ITEMS)
        return 1;
    if (payload == RUNTIME_PISTON_DESTROY_FILTERED_ITEMS) {
        for (int i = 0; i < quantity; ++i)
            (void)runtime_java_random_next_float(r);
        return 1;
    }
    if (payload == RUNTIME_PISTON_DESTROY_RANDOM_COUNT_ITEMS) {
        uint64_t seed_before_count = r->world_random_seed48;
        quantity = runtime_java_random_next_int(r, quantity);
        if (quantity < 0
                || r->entities.n_active > GM_LIVE_MAX - quantity) {
            r->world_random_seed48 = seed_before_count;
            return 0;
        }
        for (int i = 0; i < quantity; ++i)
            if (!runtime_redstone_piston_spawn_item_stack(
                    r, x, y, z, item, item_meta))
                return 0;
        return 1;
    }
    if (payload == RUNTIME_PISTON_DESTROY_MELON_ITEMS) {
        uint64_t seed_before_count = r->world_random_seed48;
        quantity = 3 + runtime_java_random_next_int(r, 5);
        (void)runtime_java_random_next_int(r, 1);
        if (quantity < 3 || quantity > 7
                || r->entities.n_active > GM_LIVE_MAX - quantity) {
            r->world_random_seed48 = seed_before_count;
            return 0;
        }
        for (int i = 0; i < quantity; ++i)
            if (!runtime_redstone_piston_spawn_item_stack(
                    r, x, y, z, item, item_meta))
                return 0;
        return 1;
    }
    if (payload == RUNTIME_PISTON_DESTROY_STEM_ITEMS) {
        uint64_t seed_before_drop = r->block_random_seed48;
        quantity = 0;
        for (int i = 0; i < 3; ++i)
            if (runtime_java_random_seed_next_int(
                    &r->block_random_seed48, 15) <= meta)
                ++quantity;
        if (r->entities.n_active > GM_LIVE_MAX - quantity) {
            r->block_random_seed48 = seed_before_drop;
            return 0;
        }
        for (int i = 0; i < quantity; ++i)
            if (!runtime_redstone_piston_spawn_item_stack(
                    r, x, y, z, item, item_meta))
                return 0;
        return 1;
    }
    if (payload == RUNTIME_PISTON_DESTROY_NETHER_WART_ITEMS) {
        uint64_t seed_before_count = r->world_random_seed48;
        quantity = meta == 3
            ? 2 + runtime_java_random_next_int(r, 3) : 1;
        if (quantity < 1 || quantity > 4
                || r->entities.n_active > GM_LIVE_MAX - quantity) {
            r->world_random_seed48 = seed_before_count;
            return 0;
        }
        for (int i = 0; i < quantity; ++i)
            if (!runtime_redstone_piston_spawn_item_stack(
                    r, x, y, z, item, item_meta))
                return 0;
        return 1;
    }
    if (payload == RUNTIME_PISTON_DESTROY_FIXED_ITEMS) {
        if (quantity < 1 || quantity > GM_LIVE_MAX
                || r->entities.n_active > GM_LIVE_MAX - quantity)
            return 0;
        for (int i = 0; i < quantity; ++i)
            if (!runtime_redstone_piston_spawn_item_stack(
                    r, x, y, z, item, item_meta))
                return 0;
        return 1;
    }
    if (payload == RUNTIME_PISTON_DESTROY_BLOCK_RANDOM_ITEM) {
        uint64_t seed_before_drop = r->block_random_seed48;
        if (runtime_java_random_seed_next_int(
                &r->block_random_seed48, 8) != 0)
            return 1;
        /* ForgeHooks.getGrassSeed chooses the sole vanilla weight-10 entry;
         * its overridden getStack then computes 1 + nextInt(1) at fortune 0. */
        (void)runtime_java_random_seed_next_int(
            &r->block_random_seed48, 10);
        (void)runtime_java_random_seed_next_int(
            &r->block_random_seed48, 1);
        if (r->entities.n_active >= GM_LIVE_MAX) {
            /* Fixed-pool rejection is an implementation boundary, not a Java
             * game event. Preserve the existing atomic extension contract. */
            r->block_random_seed48 = seed_before_drop;
            return 0;
        }
        return runtime_redstone_piston_spawn_item_stack(
            r, x, y, z, item, item_meta);
    }
    if (payload == RUNTIME_PISTON_DESTROY_WORLD_RANDOM_ITEM) {
        uint64_t seed_before_drop = r->world_random_seed48;
        if (runtime_java_random_next_int(r, 8) != 0)
            return 1;
        if (r->entities.n_active >= GM_LIVE_MAX) {
            r->world_random_seed48 = seed_before_drop;
            return 0;
        }
        return runtime_redstone_piston_spawn_item_stack(
            r, x, y, z, item, item_meta);
    }
    if (payload == RUNTIME_PISTON_DESTROY_CROP_ITEMS) {
        uint64_t seed_before_drop = r->world_random_seed48;
        uint64_t block_seed_before_drop = r->block_random_seed48;
        int mature_age = id == 207 ? 3 : 7;
        int bonus_bound = id == 207 ? 6 : 14;
        int seed_stacks = 0;
        int poison_stacks = 0;
        int seed_item = id == 59 ? 295 : id == 207 ? 435 : item;
        if (meta == mature_age) {
            for (int i = 0; i < 3; ++i) {
                int draw = runtime_java_random_next_int(r, bonus_bound);
                if (draw < 0) {
                    r->world_random_seed48 = seed_before_drop;
                    return 0;
                }
                if (draw <= mature_age)
                    ++seed_stacks;
            }
            if (id == 142)
                poison_stacks = runtime_java_random_seed_next_int(
                    &r->block_random_seed48, 50) == 0;
        }
        quantity = 1 + seed_stacks + poison_stacks;
        if (r->entities.n_active > GM_LIVE_MAX - quantity) {
            r->world_random_seed48 = seed_before_drop;
            r->block_random_seed48 = block_seed_before_drop;
            return 0;
        }
        if (!runtime_redstone_piston_spawn_item_stack(
                r, x, y, z, item, item_meta))
            return 0;
        for (int i = 0; i < seed_stacks; ++i) {
            if (!runtime_redstone_piston_spawn_item_stack(
                    r, x, y, z, seed_item, 0))
                return 0;
        }
        if (poison_stacks && !runtime_redstone_piston_spawn_item_stack(
                r, x, y, z, 394, 0))
            return 0;
        return 1;
    }
    if (payload == RUNTIME_PISTON_DESTROY_LEAF_ITEMS) {
        uint64_t seed_before_drop = r->world_random_seed48;
        int variant = meta & 3;
        int sapling_chance = id == 18 && variant == 3 ? 40 : 20;
        int sapling = runtime_java_random_next_int(r, sapling_chance) == 0;
        int apple = 0;
        if ((id == 18 && variant == 0)
                || (id == 161 && variant == 1))
            apple = runtime_java_random_next_int(r, 200) == 0;
        quantity = sapling + apple;
        if (r->entities.n_active > GM_LIVE_MAX - quantity) {
            r->world_random_seed48 = seed_before_drop;
            return 0;
        }
        if (sapling && !runtime_redstone_piston_spawn_item_stack(
                r, x, y, z, item, item_meta))
            return 0;
        if (apple && !runtime_redstone_piston_spawn_item_stack(
                r, x, y, z, 260, 0))
            return 0;
        return 1;
    }
    if (payload != RUNTIME_PISTON_DESTROY_SPAWN_ITEMS
            || r->entities.n_active >= GM_LIVE_MAX)
        return 0;
    if (runtime_is_shulker_box(id))
        return runtime_spawn_shulker_box_item(
            r, x, y, z, item,
            runtime_static_container_at(r, x, y, z));
    if (id == 144)
        return runtime_spawn_skull_item(r, x, y, z, item);
    if (!runtime_redstone_piston_spawn_item_stack(
            r, x, y, z, item, item_meta))
        return 0;
    if (id == 140) {
        int flower_item;
        int flower_meta;
        if (runtime_flower_pot_payload(
                r, x, y, z, &flower_item, &flower_meta))
            return runtime_redstone_piston_spawn_item_stack(
                r, x, y, z, flower_item, flower_meta);
    }
    return 1;
}

/* Chunk.setBlockState invokes the removed block's breakBlock hook even when
 * BlockPistonBase uses flag 4 and performs its ordered notification pass
 * separately. Retain the extra source-specific callbacks: powered controls
 * notify their attachment, powered plates also notify their support, a lit
 * torch walks all six adjacent notification centers, and a repeater notifies
 * its output neighborhood regardless of powered state.
 *
 * The runtime materializes the moving records before replaying this hook so a
 * callback into the same still-unextended piston can recognize the in-flight
 * operation instead of recursively starting it. All writes and callbacks
 * remain in the initiating server tick. */
static void runtime_redstone_tripwire_hook_sound(
        GmRuntime *r, int attached, int powered,
        int old_attached, int old_powered) {
    if (!powered && !old_powered && !attached && old_attached)
        (void)runtime_java_random_next_float(r);
}

static void runtime_redstone_tripwire_hook_calculate(
        GmRuntime *r, int x, int y, int z, int hook_meta,
        int breaking, int notify, int changed_step, int changed_meta,
        int schedule_offset) {
    static const int dx[4] = {0, -1, 0, 1};
    static const int dz[4] = {1, 0, -1, 0};
    int wire_meta[42] = {0};
    unsigned char wire_present[42] = {0};
    int facing = hook_meta & 3;
    int old_attached = (hook_meta & 4) != 0;
    int old_powered = (hook_meta & 8) != 0;
    int attached = !breaking;
    int powered = 0;
    int opposite = (facing + 2) & 3;
    int distance = 0;
    for (int step = 1; step < 42; ++step) {
        int bx = x + dx[facing] * step;
        int bz = z + dz[facing] * step;
        int id = gm_world_block(r->world, bx, y, bz);
        int state_meta = gm_world_meta(r->world, bx, y, bz);
        if (id == 131) {
            if ((state_meta & 3) == opposite)
                distance = step;
            break;
        }
        if (id != 132 && step != changed_step) {
            attached = 0;
            continue;
        }
        if (step == changed_step)
            state_meta = changed_meta;
        wire_present[step] = 1;
        wire_meta[step] = state_meta;
        if ((state_meta & 8) == 0 && (state_meta & 1) != 0)
            powered = 1;
        if (step == changed_step) {
            runtime_schedule_tick_insert(
                r, x, y, z, 131,
                r->clock.total_time + 10 + schedule_offset, 0,
                r->scheduled_tick_next_order);
            if ((state_meta & 8) != 0)
                attached = 0;
        }
    }
    attached = attached && distance > 1;
    powered = powered && attached;
    if (distance > 0) {
        int opposite_x = x + dx[facing] * distance;
        int opposite_z = z + dz[facing] * distance;
        int new_meta = opposite | (attached ? 4 : 0) | (powered ? 8 : 0);
        int old_meta = gm_world_meta(
            r->world, opposite_x, y, opposite_z);
        gm_world_set_block_meta(
            r->world, opposite_x, y, opposite_z, 131, new_meta);
        /* setBlockState(..., 3), then BlockTripWireHook.notifyNeighbors. */
        if (old_meta != new_meta)
            runtime_redstone_notify_neighbors(
                r, opposite_x, y, opposite_z);
        runtime_redstone_notify_neighbors(
            r, opposite_x, y, opposite_z);
        runtime_redstone_notify_neighbors(
            r, opposite_x + dx[facing], y,
            opposite_z + dz[facing]);
        runtime_redstone_tripwire_hook_sound(
            r, attached, powered, old_attached, old_powered);
    }
    runtime_redstone_tripwire_hook_sound(
        r, attached, powered, old_attached, old_powered);
    if (!breaking) {
        int new_meta = facing | (attached ? 4 : 0) | (powered ? 8 : 0);
        gm_world_set_block_meta(r->world, x, y, z, 131, new_meta);
        if (hook_meta != new_meta)
            runtime_redstone_notify_neighbors(r, x, y, z);
        if (notify) {
            runtime_redstone_notify_neighbors(r, x, y, z);
            runtime_redstone_notify_neighbors(
                r, x - dx[facing], y, z - dz[facing]);
        }
    }
    if (old_attached != attached) {
        for (int step = 1; step < distance; ++step) {
            int bx = x + dx[facing] * step;
            int bz = z + dz[facing] * step;
            if (!wire_present[step]
                    || gm_world_block(r->world, bx, y, bz) == 0)
                continue;
            {
                int new_meta = attached ? (wire_meta[step] | 4)
                                        : (wire_meta[step] & ~4);
                if (new_meta != gm_world_meta(r->world, bx, y, bz)) {
                    gm_world_set_block_meta(
                        r->world, bx, y, bz, 132, new_meta);
                    runtime_redstone_notify_neighbors(r, bx, y, bz);
                }
            }
        }
    }
}

static void runtime_redstone_tripwire_hook_break_state(
        GmRuntime *r, int x, int y, int z, int meta) {
    static const int dx[4] = {0, -1, 0, 1};
    static const int dz[4] = {1, 0, -1, 0};
    int facing = meta & 3;
    if ((meta & 12) == 0)
        return;
    runtime_redstone_tripwire_hook_calculate(
        r, x, y, z, meta, 1, 0, -1, 0, 0);
    if ((meta & 8) != 0) {
        runtime_redstone_notify_neighbors(r, x, y, z);
        runtime_redstone_notify_neighbors(
            r, x - dx[facing], y, z - dz[facing]);
    }
}

static void runtime_redstone_tripwire_notify_hook(
        GmRuntime *r, int x, int y, int z, int meta,
        int schedule_offset) {
    static const int dx[4] = {0, -1, 0, 1};
    static const int dz[4] = {1, 0, -1, 0};
    static const int directions[2] = {0, 1};
    for (int index = 0; index < 2; ++index) {
        int direction = directions[index];
        for (int step = 1; step < 42; ++step) {
            int bx = x + dx[direction] * step;
            int bz = z + dz[direction] * step;
            int id = gm_world_block(r->world, bx, y, bz);
            if (id == 131) {
                int hook_meta = gm_world_meta(r->world, bx, y, bz);
                if ((hook_meta & 3) == ((direction + 2) & 3))
                    runtime_redstone_tripwire_hook_calculate(
                        r, bx, y, bz, hook_meta, 0, 1, step, meta,
                        schedule_offset);
                break;
            }
            if (id != 132)
                break;
        }
    }
}

static void runtime_redstone_tripwire_break_state(
        GmRuntime *r, int x, int y, int z, int meta) {
    runtime_redstone_tripwire_notify_hook(r, x, y, z, meta | 1, 0);
}

static void runtime_redstone_break_replaced_state(
        GmRuntime *r, int x, int y, int z, int id, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    if (id == 18 || id == 161) {
        /* BlockLeaves.breakBlock marks every leaf in the surrounding 3-cube,
         * including diagonals and player-placed/nondecayable leaves. The
         * removed center is already air (or the moving head), so only the
         * remaining neighbors can change here. */
        for (int ox = -1; ox <= 1; ++ox)
            for (int oy = -1; oy <= 1; ++oy)
                for (int oz = -1; oz <= 1; ++oz) {
                    int neighbor_id = gm_world_block(
                        r->world, x + ox, y + oy, z + oz);
                    int neighbor_meta = gm_world_meta(
                        r->world, x + ox, y + oy, z + oz);
                    if ((neighbor_id == 18 || neighbor_id == 161)
                            && gm_block_meta_canonical_1_11_2(
                                neighbor_id, neighbor_meta)
                            && (neighbor_meta & 8) == 0)
                        gm_world_set_block_meta(
                            r->world, x + ox, y + oy, z + oz,
                            neighbor_id, neighbor_meta | 8);
                }
    } else if (id == 55) {
        /* BlockRedstoneWire.breakBlock first asks each adjacent position to
         * notify its own neighbors. This six-center pass reaches indirect
         * consumers through the support below and wakes same-level or stepped
         * neighboring wire before the outer replacement notification. */
        for (int face = 0; face < 6; ++face)
            runtime_redstone_notify_neighbors(
                r, x + dx[face], y + dy[face], z + dz[face]);
    } else if (id == 76) {
        /* EnumFacing.values(): DOWN, UP, NORTH, SOUTH, WEST, EAST. */
        for (int face = 0; face < 6; ++face)
            runtime_redstone_notify_neighbors(
                r, x + dx[face], y + dy[face], z + dz[face]);
    } else if (id == 93 || id == 94
            || id == 149 || id == 150) {
        runtime_redstone_repeater_notify_output(r, x, y, z, meta);
    } else if ((id == 69 || id == 77 || id == 143)
            && (meta & 8) != 0) {
        int face = runtime_redstone_control_facing(id, meta);
        runtime_redstone_notify_neighbors(r, x, y, z);
        runtime_redstone_notify_neighbors(
            r, x - dx[face], y - dy[face], z - dz[face]);
    } else if ((id == 70 || id == 72
                || id == 147 || id == 148)
            && meta > 0) {
        runtime_redstone_notify_neighbors(r, x, y, z);
        runtime_redstone_notify_neighbors(r, x, y - 1, z);
    } else if (id == 131)
        runtime_redstone_tripwire_hook_break_state(r, x, y, z, meta);
    else if (id == 132)
        runtime_redstone_tripwire_break_state(r, x, y, z, meta);
}

/* A moved solid block can invalidate horizontally adjacent cactus only when
 * its moving tile settles to the stored block state. Collect at most four
 * candidate cactus cells per moved block. If two candidates belong to the
 * same uninterrupted column, only the lower callback owns the shared upward
 * cascade. The caller uses the result for fixed-pool atomicity before any
 * piston, RNG, or world mutation. */
static int runtime_redstone_piston_settlement_cactus_drops_at_origins(
        const GmRuntime *r, const int *origin_x, const int *origin_y,
        const int *origin_z, int facing, int move_count) {
    static const int move_dx[6] = {0, 0, 0, 0, -1, 1};
    static const int move_dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int move_dz[6] = {0, 0, -1, 1, 0, 0};
    static const int side_dx[4] = {-1, 1, 0, 0};
    static const int side_dz[4] = {0, 0, -1, 1};
    int candidate_x[48];
    int candidate_y[48];
    int candidate_z[48];
    int candidate_count = 0;
    int drops = 0;
    for (int offset = 0; offset < move_count; ++offset) {
        int x = origin_x[offset];
        int y = origin_y[offset];
        int z = origin_z[offset];
        int moved_id = gm_world_block(
            r->world, x, y, z);
        int moved_meta = gm_world_meta(
            r->world, x, y, z);
        int destination_x = x + move_dx[facing];
        int destination_y = y + move_dy[facing];
        int destination_z = z + move_dz[facing];
        if (!gm_block_material_is_solid_1_11_2(moved_id, moved_meta)
                && moved_id != 10 && moved_id != 11)
            continue;
        for (int side = 0; side < 4; ++side) {
            int cactus_x = destination_x + side_dx[side];
            int cactus_y = destination_y;
            int cactus_z = destination_z + side_dz[side];
            int duplicate = 0;
            if (gm_world_block(
                    r->world, cactus_x, cactus_y, cactus_z) != 81)
                continue;
            for (int i = 0; i < candidate_count; ++i)
                if (candidate_x[i] == cactus_x
                        && candidate_y[i] == cactus_y
                        && candidate_z[i] == cactus_z)
                    duplicate = 1;
            if (!duplicate && candidate_count < 48) {
                candidate_x[candidate_count] = cactus_x;
                candidate_y[candidate_count] = cactus_y;
                candidate_z[candidate_count] = cactus_z;
                ++candidate_count;
            }
        }
    }
    for (int i = 0; i < candidate_count; ++i) {
        int covered = 0;
        for (int j = 0; j < candidate_count && !covered; ++j) {
            int contiguous = 1;
            if (i == j || candidate_x[i] != candidate_x[j]
                    || candidate_z[i] != candidate_z[j]
                    || candidate_y[j] >= candidate_y[i])
                continue;
            for (int y = candidate_y[j]; y <= candidate_y[i]; ++y)
                if (gm_world_block(
                        r->world, candidate_x[i], y,
                        candidate_z[i]) != 81)
                    contiguous = 0;
            covered = contiguous;
        }
        if (covered)
            continue;
        for (int y = candidate_y[i]; y <= 255
                && gm_world_block(
                    r->world, candidate_x[i], y,
                    candidate_z[i]) == 81; ++y)
            ++drops;
    }
    return drops;
}

static int runtime_redstone_piston_settlement_cactus_drops(
        const GmRuntime *r, int front_x, int front_y, int front_z,
        int facing, int move_count) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int origin_x[12];
    int origin_y[12];
    int origin_z[12];
    for (int offset = 0; offset < move_count; ++offset) {
        origin_x[offset] = front_x + offset * dx[facing];
        origin_y[offset] = front_y + offset * dy[facing];
        origin_z[offset] = front_z + offset * dz[facing];
    }
    return runtime_redstone_piston_settlement_cactus_drops_at_origins(
        r, origin_x, origin_y, origin_z, facing, move_count);
}

#define RUNTIME_PISTON_STRUCTURE_MOVES 12
#define RUNTIME_PISTON_STRUCTURE_DESTROYS 48

typedef struct {
    int piston_x;
    int piston_y;
    int piston_z;
    int facing;
    int move_count;
    int move_x[RUNTIME_PISTON_STRUCTURE_MOVES];
    int move_y[RUNTIME_PISTON_STRUCTURE_MOVES];
    int move_z[RUNTIME_PISTON_STRUCTURE_MOVES];
    int destroy_count;
    int destroy_x[RUNTIME_PISTON_STRUCTURE_DESTROYS];
    int destroy_y[RUNTIME_PISTON_STRUCTURE_DESTROYS];
    int destroy_z[RUNTIME_PISTON_STRUCTURE_DESTROYS];
} RuntimePistonStructure;

static int runtime_redstone_piston_structure_add_branches(
    const GmRuntime *r, RuntimePistonStructure *structure,
    int x, int y, int z);

static int runtime_redstone_piston_facing_axis(int facing) {
    if (facing < 2)
        return 1;
    if (facing < 4)
        return 2;
    return 0;
}

static int runtime_redstone_piston_structure_index(
        const RuntimePistonStructure *structure, int x, int y, int z) {
    for (int i = 0; i < structure->move_count; ++i)
        if (structure->move_x[i] == x
                && structure->move_y[i] == y
                && structure->move_z[i] == z)
            return i;
    return -1;
}

static void runtime_redstone_piston_structure_reorder(
        RuntimePistonStructure *structure, int appended, int collision) {
    int count = structure->move_count;
    int tx[RUNTIME_PISTON_STRUCTURE_MOVES];
    int ty[RUNTIME_PISTON_STRUCTURE_MOVES];
    int tz[RUNTIME_PISTON_STRUCTURE_MOVES];
    int out = 0;
    for (int i = 0; i < collision; ++i) {
        tx[out] = structure->move_x[i];
        ty[out] = structure->move_y[i];
        tz[out++] = structure->move_z[i];
    }
    for (int i = count - appended; i < count; ++i) {
        tx[out] = structure->move_x[i];
        ty[out] = structure->move_y[i];
        tz[out++] = structure->move_z[i];
    }
    for (int i = collision; i < count - appended; ++i) {
        tx[out] = structure->move_x[i];
        ty[out] = structure->move_y[i];
        tz[out++] = structure->move_z[i];
    }
    memcpy(structure->move_x, tx, sizeof tx);
    memcpy(structure->move_y, ty, sizeof ty);
    memcpy(structure->move_z, tz, sizeof tz);
}

/* Exact bounded port of BlockPistonStructureHelper.addBlockLine. The list is
 * kept in Java insertion order because doMove consumes it in reverse. */
static int runtime_redstone_piston_structure_add_line(
        const GmRuntime *r, RuntimePistonStructure *structure,
        int origin_x, int origin_y, int origin_z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int facing = structure->facing;
    int block = gm_world_block(
        r->world, origin_x, origin_y, origin_z);
    int meta = gm_world_meta(
        r->world, origin_x, origin_y, origin_z);
    int backward_count = 1;
    int appended = 0;
    if (block == 0)
        return 1;
    if (!gm_block_meta_canonical_1_11_2(block, meta))
        return 0;
    if (!runtime_redstone_piston_can_push_state(
            r, origin_x, origin_y, origin_z, facing, 0))
        return 1;
    if ((origin_x == structure->piston_x
            && origin_y == structure->piston_y
            && origin_z == structure->piston_z)
            || runtime_redstone_piston_structure_index(
                structure, origin_x, origin_y, origin_z) >= 0)
        return 1;
    if (backward_count + structure->move_count
            > RUNTIME_PISTON_STRUCTURE_MOVES)
        return 0;
    while (block == 165) {
        int x = origin_x - backward_count * dx[facing];
        int y = origin_y - backward_count * dy[facing];
        int z = origin_z - backward_count * dz[facing];
        block = gm_world_block(r->world, x, y, z);
        meta = gm_world_meta(r->world, x, y, z);
        if (block == 0
                || !gm_block_meta_canonical_1_11_2(block, meta)
                || !runtime_redstone_piston_can_push_state(
                    r, x, y, z, facing, 0)
                || (x == structure->piston_x
                    && y == structure->piston_y
                    && z == structure->piston_z))
            break;
        ++backward_count;
        if (backward_count + structure->move_count
                > RUNTIME_PISTON_STRUCTURE_MOVES)
            return 0;
    }
    for (int offset = backward_count - 1; offset >= 0; --offset) {
        int index = structure->move_count++;
        structure->move_x[index] = origin_x - offset * dx[facing];
        structure->move_y[index] = origin_y - offset * dy[facing];
        structure->move_z[index] = origin_z - offset * dz[facing];
        ++appended;
    }
    for (int forward = 1;; ++forward) {
        int x = origin_x + forward * dx[facing];
        int y = origin_y + forward * dy[facing];
        int z = origin_z + forward * dz[facing];
        int collision = runtime_redstone_piston_structure_index(
            structure, x, y, z);
        if (collision >= 0) {
            runtime_redstone_piston_structure_reorder(
                structure, appended, collision);
            for (int i = 0; i <= collision + appended; ++i)
                if (gm_world_block(
                        r->world,
                        structure->move_x[i],
                        structure->move_y[i],
                        structure->move_z[i]) == 165
                        && !runtime_redstone_piston_structure_add_branches(
                            r, structure,
                            structure->move_x[i],
                            structure->move_y[i],
                            structure->move_z[i]))
                    return 0;
            return 1;
        }
        block = gm_world_block(r->world, x, y, z);
        meta = gm_world_meta(r->world, x, y, z);
        if (block == 0)
            return 1;
        if (!gm_block_meta_canonical_1_11_2(block, meta)
                || !runtime_redstone_piston_can_push_state(
                    r, x, y, z, facing, 1)
                || (x == structure->piston_x
                    && y == structure->piston_y
                    && z == structure->piston_z))
            return 0;
        if (gm_block_piston_destroy_1_11_2(block, meta)) {
            int index = structure->destroy_count;
            if (index >= RUNTIME_PISTON_STRUCTURE_DESTROYS)
                return 0;
            structure->destroy_x[index] = x;
            structure->destroy_y[index] = y;
            structure->destroy_z[index] = z;
            ++structure->destroy_count;
            return 1;
        }
        if (structure->move_count >= RUNTIME_PISTON_STRUCTURE_MOVES)
            return 0;
        structure->move_x[structure->move_count] = x;
        structure->move_y[structure->move_count] = y;
        structure->move_z[structure->move_count] = z;
        ++structure->move_count;
        ++appended;
    }
}

static int runtime_redstone_piston_structure_add_branches(
        const GmRuntime *r, RuntimePistonStructure *structure,
        int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int axis = runtime_redstone_piston_facing_axis(structure->facing);
    for (int face = 0; face < 6; ++face) {
        if (runtime_redstone_piston_facing_axis(face) == axis)
            continue;
        if (!runtime_redstone_piston_structure_add_line(
                r, structure,
                x + dx[face], y + dy[face], z + dz[face]))
            return 0;
    }
    return 1;
}

static int runtime_redstone_piston_structure_build_from(
        const GmRuntime *r, RuntimePistonStructure *structure,
        int piston_x, int piston_y, int piston_z,
        int origin_x, int origin_y, int origin_z, int move_facing) {
    int block = gm_world_block(r->world, origin_x, origin_y, origin_z);
    int meta = gm_world_meta(r->world, origin_x, origin_y, origin_z);
    memset(structure, 0, sizeof *structure);
    structure->piston_x = piston_x;
    structure->piston_y = piston_y;
    structure->piston_z = piston_z;
    structure->facing = move_facing;
    if (!gm_block_meta_canonical_1_11_2(block, meta))
        return 0;
    if (!runtime_redstone_piston_can_push_state(
            r, origin_x, origin_y, origin_z, move_facing, 0)) {
        if (!gm_block_piston_destroy_1_11_2(block, meta))
            return 0;
        structure->destroy_x[0] = origin_x;
        structure->destroy_y[0] = origin_y;
        structure->destroy_z[0] = origin_z;
        structure->destroy_count = 1;
        return 1;
    }
    if (!runtime_redstone_piston_structure_add_line(
            r, structure, origin_x, origin_y, origin_z))
        return 0;
    for (int i = 0; i < structure->move_count; ++i)
        if (gm_world_block(
                r->world,
                structure->move_x[i],
                structure->move_y[i],
                structure->move_z[i]) == 165
                && !runtime_redstone_piston_structure_add_branches(
                    r, structure,
                    structure->move_x[i],
                    structure->move_y[i],
                    structure->move_z[i]))
            return 0;
    return 1;
}

static int runtime_redstone_piston_structure_destroys_position(
        const RuntimePistonStructure *structure, int x, int y, int z) {
    for (int i = 0; i < structure->destroy_count; ++i)
        if (structure->destroy_x[i] == x
                && structure->destroy_y[i] == y
                && structure->destroy_z[i] == z)
            return 1;
    return 0;
}

static int runtime_redstone_piston_structure_replaces_position(
        const RuntimePistonStructure *structure, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int facing = structure->facing;
    for (int i = 0; i < structure->move_count; ++i)
        if (structure->move_x[i] + dx[facing] == x
                && structure->move_y[i] + dy[facing] == y
                && structure->move_z[i] + dz[facing] == z)
            return 1;
    return 0;
}

/* Structure-helper DESTROY entries are processed as one Java list, in
 * reverse insertion order, before any moved block is written. Admit the
 * complete deterministic/simple payload class plus canonical paired beds,
 * doors, and double plants. A directly destroyed lower/foot owns
 * an item when its state has one; an upper/head owns none but its later
 * notification makes an otherwise untouched item-owning mate drop. Count
 * that deferred stack now so Magma's fixed entity pool remains atomic.
 * Cascading columns, inventory/profile tiles, comparators, and randomized-
 * count payloads remain visible and rejected until their multi-entry
 * preflight can preserve the same contract. */
static int runtime_redstone_piston_structure_destroy_admitted(
        const GmRuntime *r, const RuntimePistonStructure *structure,
        int index, int id, int meta, int *item_stacks) {
    int payload = runtime_redstone_piston_destroy_payload(
        id, meta, NULL, NULL, NULL);
    if (item_stacks)
        *item_stacks = 0;
    if (id == 26) {
        static const int facing_dx[4] = {0, -1, 0, 1};
        static const int facing_dz[4] = {1, 0, -1, 0};
        int facing = meta & 3;
        int is_head = (meta & 8) != 0;
        int pair_x = structure->destroy_x[index]
            + (is_head ? -1 : 1) * facing_dx[facing];
        int pair_y = structure->destroy_y[index];
        int pair_z = structure->destroy_z[index]
            + (is_head ? -1 : 1) * facing_dz[facing];
        int pair_meta = gm_world_meta(r->world, pair_x, pair_y, pair_z);
        if (gm_world_block(r->world, pair_x, pair_y, pair_z) != 26
                || (pair_meta & 3) != facing
                || ((pair_meta & 8) != 0) == is_head)
            return 0;
        if (item_stacks)
            *item_stacks = !is_head ? 1
                : (!runtime_redstone_piston_structure_destroys_position(
                        structure, pair_x, pair_y, pair_z)
                    && !runtime_redstone_piston_structure_replaces_position(
                        structure, pair_x, pair_y, pair_z));
        return 1;
    }
    if (runtime_is_door_block(id)) {
        int is_upper = (meta & 8) != 0;
        int pair_x = structure->destroy_x[index];
        int pair_y = structure->destroy_y[index] + (is_upper ? -1 : 1);
        int pair_z = structure->destroy_z[index];
        int pair_meta = gm_world_meta(r->world, pair_x, pair_y, pair_z);
        if (gm_world_block(r->world, pair_x, pair_y, pair_z) != id
                || ((pair_meta & 8) != 0) == is_upper)
            return 0;
        if (item_stacks)
            *item_stacks = !is_upper ? 1
                : (!runtime_redstone_piston_structure_destroys_position(
                        structure, pair_x, pair_y, pair_z)
                    && !runtime_redstone_piston_structure_replaces_position(
                        structure, pair_x, pair_y, pair_z));
        return 1;
    }
    if (id == 175) {
        int is_upper = (meta & 8) != 0;
        int pair_x = structure->destroy_x[index];
        int pair_y = structure->destroy_y[index] + (is_upper ? -1 : 1);
        int pair_z = structure->destroy_z[index];
        int pair_meta = gm_world_meta(r->world, pair_x, pair_y, pair_z);
        int lower_meta = is_upper ? pair_meta : meta;
        int owns_item;
        if (gm_world_block(r->world, pair_x, pair_y, pair_z) != 175
                || ((pair_meta & 8) != 0) == is_upper)
            return 0;
        if (lower_meta < 0 || lower_meta > 5)
            return 0;
        owns_item = lower_meta != 3;
        if (item_stacks)
            *item_stacks = lower_meta != 2 && owns_item && (!is_upper
                || (!runtime_redstone_piston_structure_destroys_position(
                        structure, pair_x, pair_y, pair_z)
                    && !runtime_redstone_piston_structure_replaces_position(
                        structure, pair_x, pair_y, pair_z)));
        return 1;
    }
    if (payload == RUNTIME_PISTON_DESTROY_NO_ITEMS
            || payload == RUNTIME_PISTON_DESTROY_FILTERED_ITEMS)
        return 1;
    if (payload != RUNTIME_PISTON_DESTROY_SPAWN_ITEMS
            || id == 81 || id == 140
            || id == 144 || id == 149 || id == 150 || id == 175
            || runtime_is_shulker_box(id))
        return 0;
    if (item_stacks)
        *item_stacks = 1;
    return 1;
}

/* Advance a World.rand shadow through one ordinary one-stack spawn. Java's
 * drop path consumes a chance float followed by three position floats. The
 * bit width does not change the 48-bit LCG cursor, so nextInt(1) is a compact
 * one-step cursor advance here. */
static void runtime_redstone_piston_structure_shadow_spawn(
        uint64_t *seed48) {
    for (int draw = 0; draw < 4; ++draw)
        (void)runtime_java_random_seed_next_int(seed48, 1);
}

static int runtime_redstone_piston_structure_shadow_direct(
        int id, int meta, uint64_t *seed48, int *required) {
    int quantity = 0;
    int payload = runtime_redstone_piston_destroy_payload(
        id, meta, NULL, NULL, &quantity);
    if (id == 175 && meta == 2) {
        if (runtime_java_random_seed_next_int(seed48, 8) == 0) {
            if (*required >= GM_LIVE_MAX)
                return 0;
            ++*required;
            runtime_redstone_piston_structure_shadow_spawn(seed48);
        }
        return 1;
    }
    if (payload == RUNTIME_PISTON_DESTROY_FILTERED_ITEMS) {
        for (int draw = 0; draw < quantity; ++draw)
            (void)runtime_java_random_seed_next_int(seed48, 1);
        return 1;
    }
    if (payload == RUNTIME_PISTON_DESTROY_NO_ITEMS)
        return 1;
    if (payload != RUNTIME_PISTON_DESTROY_SPAWN_ITEMS
            || *required >= GM_LIVE_MAX)
        return 0;
    ++*required;
    runtime_redstone_piston_structure_shadow_spawn(seed48);
    return 1;
}

static int runtime_redstone_piston_structure_shadow_deferred(
        const GmRuntime *r, const RuntimePistonStructure *structure,
        int index, int id, int meta, uint64_t *seed48, int *required,
        int *randomized) {
    int pair_x = structure->destroy_x[index];
    int pair_y = structure->destroy_y[index];
    int pair_z = structure->destroy_z[index];
    int lower_meta;
    if (id == 26 && (meta & 8) != 0) {
        static const int facing_dx[4] = {0, -1, 0, 1};
        static const int facing_dz[4] = {1, 0, -1, 0};
        int facing = meta & 3;
        pair_x -= facing_dx[facing];
        pair_z -= facing_dz[facing];
    } else if (runtime_is_door_block(id) && (meta & 8) != 0) {
        --pair_y;
    } else if (id == 175 && (meta & 8) != 0) {
        --pair_y;
    } else {
        return 1;
    }
    if (runtime_redstone_piston_structure_destroys_position(
            structure, pair_x, pair_y, pair_z)
            || runtime_redstone_piston_structure_replaces_position(
                structure, pair_x, pair_y, pair_z))
        return 1;
    lower_meta = gm_world_meta(r->world, pair_x, pair_y, pair_z);
    if (id == 175 && lower_meta == 2) {
        if (randomized)
            *randomized = 1;
        if (runtime_java_random_seed_next_int(seed48, 8) != 0)
            return 1;
        if (*required >= GM_LIVE_MAX)
            return 0;
        ++*required;
        runtime_redstone_piston_structure_shadow_spawn(seed48);
        return 1;
    }
    if (id == 175 && lower_meta == 3)
        return 1;
    if (*required >= GM_LIVE_MAX)
        return 0;
    ++*required;
    runtime_redstone_piston_structure_shadow_spawn(seed48);
    return 1;
}

/* The directly destroyed reed already consumed its item RNG in doMove's
 * reverse payload phase. Its later air notification recursively removes only
 * the still-present contiguous reed cells above it. A cell already destroyed
 * by another branch, or replaced by a moving destination, stops that Java
 * callback chain. */
static int runtime_redstone_piston_structure_shadow_reed_column(
        const GmRuntime *r, const RuntimePistonStructure *structure,
        int index, int id, uint64_t *seed48, int *required) {
    int x;
    int y;
    int z;
    if (id != 83)
        return 1;
    x = structure->destroy_x[index];
    y = structure->destroy_y[index] + 1;
    z = structure->destroy_z[index];
    while (y <= 255 && gm_world_block(r->world, x, y, z) == 83) {
        if (runtime_redstone_piston_structure_destroys_position(
                structure, x, y, z)
                || runtime_redstone_piston_structure_replaces_position(
                    structure, x, y, z))
            break;
        if (*required >= GM_LIVE_MAX)
            return 0;
        ++*required;
        runtime_redstone_piston_structure_shadow_spawn(seed48);
        ++y;
    }
    return 1;
}

static int runtime_redstone_piston_structure_destroy_preflight(
        const GmRuntime *r, const RuntimePistonStructure *structure,
        int *destroy_block, int *destroy_meta, int *item_stacks) {
    uint64_t shadow_seed = r->world_random_seed48;
    int required = 0;
    int has_random_deferred = 0;
    for (int i = 0; i < structure->destroy_count; ++i) {
        int id = gm_world_block(
            r->world,
            structure->destroy_x[i],
            structure->destroy_y[i],
            structure->destroy_z[i]);
        int meta = gm_world_meta(
            r->world,
            structure->destroy_x[i],
            structure->destroy_y[i],
            structure->destroy_z[i]);
        if (!gm_block_meta_canonical_1_11_2(id, meta)
                || !runtime_redstone_piston_structure_destroy_admitted(
                    r, structure, i, id, meta, NULL))
            return 0;
        destroy_block[i] = id;
        destroy_meta[i] = meta;
    }
    /* doMove applies all DESTROY payloads in reverse insertion order. */
    for (int i = structure->destroy_count - 1; i >= 0; --i)
        if (!runtime_redstone_piston_structure_shadow_direct(
                destroy_block[i], destroy_meta[i],
                &shadow_seed, &required))
            return 0;
    /* Paired upper halves defer their lower-owner payload until the later
     * reverse notification phase. This second pass is what makes mixed
     * direct/deferred double-grass rolls preserve Java's exact RNG order. */
    for (int i = structure->destroy_count - 1; i >= 0; --i)
        if (!runtime_redstone_piston_structure_shadow_deferred(
                r, structure, i, destroy_block[i], destroy_meta[i],
                &shadow_seed, &required, &has_random_deferred))
            return 0;
        else if (!runtime_redstone_piston_structure_shadow_reed_column(
                r, structure, i, destroy_block[i],
                &shadow_seed, &required))
            return 0;
    /* Tripwire break hooks can consume sound RNG between the two phases.
     * Keep that mixed callback topology explicit until its shadow hook can
     * reproduce the full represented wire state without mutating the world. */
    if (has_random_deferred)
        for (int i = 0; i < structure->destroy_count; ++i)
            if (destroy_block[i] == 131 || destroy_block[i] == 132)
                return 0;
    if (r->entities.n_active > GM_LIVE_MAX - required)
        return 0;
    if (item_stacks)
        *item_stacks = required;
    return 1;
}

static int runtime_redstone_piston_structure_apply_destroys(
        GmRuntime *r, const RuntimePistonStructure *structure,
        const int *destroy_block, const int *destroy_meta) {
    for (int i = structure->destroy_count - 1; i >= 0; --i) {
        if (!runtime_redstone_piston_apply_destroy_payload(
                r,
                structure->destroy_x[i],
                structure->destroy_y[i],
                structure->destroy_z[i],
                destroy_block[i], destroy_meta[i]))
            return 0;
        gm_world_set_block_meta(
            r->world,
            structure->destroy_x[i],
            structure->destroy_y[i],
            structure->destroy_z[i], 0, 0);
    }
    return 1;
}

static void runtime_redstone_piston_structure_break_destroys(
        GmRuntime *r, const RuntimePistonStructure *structure,
        const int *destroy_block, const int *destroy_meta) {
    for (int i = structure->destroy_count - 1; i >= 0; --i)
        runtime_redstone_break_replaced_state(
            r,
            structure->destroy_x[i],
            structure->destroy_y[i],
            structure->destroy_z[i],
            destroy_block[i], destroy_meta[i]);
}

static void runtime_redstone_piston_structure_notify_destroys(
        GmRuntime *r, const RuntimePistonStructure *structure) {
    for (int i = structure->destroy_count - 1; i >= 0; --i) {
        runtime_redstone_notify_neighbors(
            r,
            structure->destroy_x[i],
            structure->destroy_y[i],
            structure->destroy_z[i]);
        runtime_redstone_update_observers_at(
            r,
            structure->destroy_x[i],
            structure->destroy_y[i],
            structure->destroy_z[i]);
    }
}

static int runtime_redstone_piston_line_has_slime(
        const GmRuntime *r, int front_x, int front_y, int front_z,
        int facing) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    for (int offset = 0; offset < 12; ++offset) {
        int x = front_x + offset * dx[facing];
        int y = front_y + offset * dy[facing];
        int z = front_z + offset * dz[facing];
        int block = gm_world_block(r->world, x, y, z);
        int meta = gm_world_meta(r->world, x, y, z);
        if (block == 165)
            return 1;
        if (block == 0
                || !gm_block_meta_canonical_1_11_2(block, meta)
                || !runtime_redstone_piston_can_push_state(
                    r, x, y, z, facing, 0))
            return 0;
    }
    return 0;
}

static void runtime_redstone_piston_start_slime_extension(
        GmRuntime *r, int x, int y, int z, int block, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    RuntimePistonStructure structure;
    int moved_block[RUNTIME_PISTON_STRUCTURE_MOVES];
    int moved_meta[RUNTIME_PISTON_STRUCTURE_MOVES];
    int destroy_block[RUNTIME_PISTON_STRUCTURE_DESTROYS];
    int destroy_meta[RUNTIME_PISTON_STRUCTURE_DESTROYS];
    int destroy_item_stacks = 0;
    int facing = meta & 7;
    int head_meta = facing | (block == 29 ? 8 : 0);
    int front_x = x + dx[facing];
    int front_y = y + dy[facing];
    int front_z = z + dz[facing];
    GmRuntimePiston *moving;
    if (!runtime_redstone_piston_structure_build_from(
            r, &structure, x, y, z,
            front_x, front_y, front_z, facing)
            || !runtime_redstone_piston_structure_destroy_preflight(
                r, &structure, destroy_block, destroy_meta,
                &destroy_item_stacks)
            || r->piston_count
                > GM_RUNTIME_PISTONS - structure.move_count - 1)
        return;
    {
        int cactus_drops =
            runtime_redstone_piston_settlement_cactus_drops_at_origins(
                r, structure.move_x, structure.move_y, structure.move_z,
                facing, structure.move_count);
        if (cactus_drops > GM_LIVE_MAX - destroy_item_stacks
                || r->entities.n_active
                    > GM_LIVE_MAX - destroy_item_stacks - cactus_drops)
            return;
    }
    for (int i = 0; i < structure.move_count; ++i) {
        moved_block[i] = gm_world_block(
            r->world,
            structure.move_x[i],
            structure.move_y[i],
            structure.move_z[i]);
        moved_meta[i] = gm_world_meta(
            r->world,
            structure.move_x[i],
            structure.move_y[i],
            structure.move_z[i]);
    }
    if (!runtime_redstone_piston_structure_apply_destroys(
            r, &structure, destroy_block, destroy_meta))
        return;
    for (int i = structure.move_count - 1; i >= 0; --i) {
        int destination_x = structure.move_x[i] + dx[facing];
        int destination_y = structure.move_y[i] + dy[facing];
        int destination_z = structure.move_z[i] + dz[facing];
        moving = &r->pistons[r->piston_count++];
        *moving = (GmRuntimePiston){
            .active = 1,
            .dimension = r->dimension,
            .x = destination_x,
            .y = destination_y,
            .z = destination_z,
            .moved_block = moved_block[i],
            .moved_meta = moved_meta[i],
            .facing = facing,
            .extending = 1,
            .source = 0,
            .progress = 0.0f,
            .last_progress = 0.0f,
        };
        gm_world_set_block_meta(
            r->world,
            structure.move_x[i],
            structure.move_y[i],
            structure.move_z[i], 0, 0);
        gm_world_set_block_meta(
            r->world, destination_x, destination_y, destination_z,
            36, facing);
    }
    moving = &r->pistons[r->piston_count++];
    *moving = (GmRuntimePiston){
        .active = 1,
        .dimension = r->dimension,
        .x = front_x,
        .y = front_y,
        .z = front_z,
        .moved_block = 34,
        .moved_meta = head_meta,
        .facing = facing,
        .extending = 1,
        .source = 1,
        .progress = 0.0f,
        .last_progress = 0.0f,
    };
    gm_world_set_block_meta(
        r->world, front_x, front_y, front_z, 36, head_meta);
    runtime_redstone_piston_structure_break_destroys(
        r, &structure, destroy_block, destroy_meta);
    runtime_redstone_piston_structure_notify_destroys(r, &structure);
    for (int i = structure.move_count - 1; i >= 0; --i) {
        runtime_redstone_notify_neighbors(
            r,
            structure.move_x[i],
            structure.move_y[i],
            structure.move_z[i]);
        runtime_redstone_update_observers_at(
            r,
            structure.move_x[i],
            structure.move_y[i],
            structure.move_z[i]);
    }
    runtime_redstone_notify_neighbors(r, front_x, front_y, front_z);
    runtime_redstone_update_observers_at(r, front_x, front_y, front_z);
    gm_world_set_block_meta(r->world, x, y, z, block, meta | 8);
    runtime_redstone_notify_neighbors(r, x, y, z);
    runtime_redstone_update_observers_at(r, x, y, z);
    (void)runtime_java_random_next_float(r);
}

static void runtime_redstone_piston_start_extension(
        GmRuntime *r, int x, int y, int z, int block, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int facing = meta & 7;
    int head_meta = facing | (block == 29 ? 8 : 0);
    int front_x = x + dx[facing];
    int front_y = y + dy[facing];
    int front_z = z + dz[facing];
    int move_count = 0;
    int destroy_offset = -1;
    int destroy_block = 0;
    int destroy_meta = 0;
    GmRuntimePiston *moving;
    int needed;
    if (runtime_redstone_piston_line_has_slime(
            r, front_x, front_y, front_z, facing)) {
        runtime_redstone_piston_start_slime_extension(
            r, x, y, z, block, meta);
        return;
    }
    for (;;) {
        int bx = front_x + move_count * dx[facing];
        int by = front_y + move_count * dy[facing];
        int bz = front_z + move_count * dz[facing];
        int id = gm_world_block(r->world, bx, by, bz);
        int state_meta = gm_world_meta(r->world, bx, by, bz);
        if (id == 0)
            break;
        if (!gm_block_meta_canonical_1_11_2(id, state_meta))
            return;
        if (gm_block_piston_destroy_1_11_2(id, state_meta)) {
            if (!runtime_redstone_piston_can_push_state(
                    r, bx, by, bz, facing, 1)
                    || !runtime_redstone_piston_destroy_supported(
                        id, state_meta))
                return;
            destroy_offset = move_count;
            destroy_block = id;
            destroy_meta = state_meta;
            break;
        }
        if (move_count >= 12
                || !runtime_redstone_piston_can_push_state(
                    r, bx, by, bz, facing, 0))
            return;
        ++move_count;
    }
    needed = move_count + 1;
    if (r->piston_count > GM_RUNTIME_PISTONS - needed)
        return;
    {
        int cactus_drops = runtime_redstone_piston_settlement_cactus_drops(
            r, front_x, front_y, front_z, facing, move_count);
        if (cactus_drops > GM_LIVE_MAX
                || r->entities.n_active > GM_LIVE_MAX - cactus_drops)
            return;
    }
    if (destroy_offset >= 0) {
        int destroy_x = front_x + destroy_offset * dx[facing];
        int destroy_y = front_y + destroy_offset * dy[facing];
        int destroy_z = front_z + destroy_offset * dz[facing];
        if (destroy_block == 140) {
            int flower_item;
            int flower_meta;
            int required = 1 + runtime_flower_pot_payload(
                r, destroy_x, destroy_y, destroy_z,
                &flower_item, &flower_meta);
            if (r->entities.n_active > GM_LIVE_MAX - required)
                return;
        }
        if (destroy_block == 175 && (destroy_meta & 8) != 0
                && gm_world_block(
                    r->world, destroy_x, destroy_y - 1, destroy_z) == 175) {
            int lower_meta = gm_world_meta(
                r->world, destroy_x, destroy_y - 1, destroy_z);
            int pair_drop = lower_meta == 0 || lower_meta == 1
                || lower_meta == 4 || lower_meta == 5;
            if (lower_meta == 2) {
                uint64_t probe_seed = r->world_random_seed48;
                pair_drop = runtime_java_random_seed_next_int(
                    &probe_seed, 8) == 0;
            }
            /* Magma's entity pool is fixed. Predict the lower-half callback
             * before erasing the upper so capacity rejection stays atomic. */
            if (pair_drop && r->entities.n_active >= GM_LIVE_MAX)
                return;
        }
        if (destroy_block == 26 && (destroy_meta & 8) != 0) {
            static const int bed_dx[4] = {0, -1, 0, 1};
            static const int bed_dz[4] = {1, 0, -1, 0};
            int bed_facing = destroy_meta & 3;
            int foot_x = destroy_x - bed_dx[bed_facing];
            int foot_z = destroy_z - bed_dz[bed_facing];
            int foot_meta = gm_world_meta(
                r->world, foot_x, destroy_y, foot_z);
            if (gm_world_block(
                    r->world, foot_x, destroy_y, foot_z) == 26
                    && (foot_meta & 8) == 0
                    && r->entities.n_active >= GM_LIVE_MAX)
                return;
        }
        if (runtime_is_door_block(destroy_block)
                && (destroy_meta & 8) != 0) {
            int lower_meta = gm_world_meta(
                r->world, destroy_x, destroy_y - 1, destroy_z);
            if (gm_world_block(
                    r->world, destroy_x, destroy_y - 1, destroy_z)
                        == destroy_block
                    && (lower_meta & 8) == 0
                    && r->entities.n_active >= GM_LIVE_MAX)
                return;
        }
        if (destroy_block == 83 || destroy_block == 81) {
            int column_drops = 1;
            while (destroy_y + column_drops <= 255
                    && gm_world_block(
                        r->world, destroy_x, destroy_y + column_drops,
                        destroy_z) == destroy_block)
                ++column_drops;
            /* Java's entity list is unbounded. Magma's fixed pool therefore
             * rejects before consuming any drop RNG or mutating the world if
             * the complete same-boundary column cannot be represented. */
            if (column_drops > GM_LIVE_MAX
                    || r->entities.n_active > GM_LIVE_MAX - column_drops)
                return;
        }
        if (!runtime_redstone_piston_apply_destroy_payload(
                r, destroy_x, destroy_y, destroy_z,
                destroy_block, destroy_meta))
            return;
        /* Placing the piston notifies an immediately adjacent comparator
         * before Java drains the piston block event. A transient powered
         * block 150 can therefore enqueue its +2 self-correction before the
         * piston destroys it; WorldServer retains that stale entry until due. */
        if (destroy_offset == 0
                && (destroy_block == 149 || destroy_block == 150))
            runtime_redstone_comparator_update_state(
                r, destroy_x, destroy_y, destroy_z, destroy_block);
        if (destroy_block == 140)
            runtime_flower_pot_remove(
                r, r->dimension, destroy_x, destroy_y, destroy_z);
        if (destroy_block == 144)
            runtime_skull_remove(
                r, r->dimension, destroy_x, destroy_y, destroy_z);
        if (destroy_block == 149 || destroy_block == 150)
            runtime_comparator_remove(
                r, r->dimension, destroy_x, destroy_y, destroy_z);
        if (runtime_is_shulker_box(destroy_block))
            runtime_static_container_remove(
                r, r->dimension, destroy_x, destroy_y, destroy_z);
        gm_world_set_block_meta(
            r->world, destroy_x, destroy_y, destroy_z, 0, 0);
    }
    for (int offset = move_count - 1; offset >= 0; --offset) {
        int origin_x = front_x + offset * dx[facing];
        int origin_y = front_y + offset * dy[facing];
        int origin_z = front_z + offset * dz[facing];
        int destination_x = origin_x + dx[facing];
        int destination_y = origin_y + dy[facing];
        int destination_z = origin_z + dz[facing];
        int moved_meta = gm_world_meta(
            r->world, origin_x, origin_y, origin_z);
        int moved_block = gm_world_block(
            r->world, origin_x, origin_y, origin_z);
        moving = &r->pistons[r->piston_count++];
        *moving = (GmRuntimePiston){
            .active = 1,
            .dimension = r->dimension,
            .x = destination_x,
            .y = destination_y,
            .z = destination_z,
            .moved_block = moved_block,
            .moved_meta = moved_meta,
            .facing = facing,
            .extending = 1,
            .source = 0,
            .progress = 0.0f,
            .last_progress = 0.0f,
        };
        gm_world_set_block_meta(
            r->world, origin_x, origin_y, origin_z, 0, 0);
        gm_world_set_block_meta(
            r->world, destination_x, destination_y, destination_z,
            36, facing);
    }
    moving = &r->pistons[r->piston_count++];
    *moving = (GmRuntimePiston){
        .active = 1,
        .dimension = r->dimension,
        .x = front_x,
        .y = front_y,
        .z = front_z,
        .moved_block = 34,
        .moved_meta = head_meta,
        .facing = facing,
        .extending = 1,
        .source = 1,
        .progress = 0.0f,
        .last_progress = 0.0f,
    };
    gm_world_set_block_meta(
        r->world, front_x, front_y, front_z, 36, head_meta);

    if (destroy_offset >= 0) {
        int destroy_x = front_x + destroy_offset * dx[facing];
        int destroy_y = front_y + destroy_offset * dy[facing];
        int destroy_z = front_z + destroy_offset * dz[facing];
        runtime_redstone_break_replaced_state(
            r, destroy_x, destroy_y, destroy_z,
            destroy_block, destroy_meta);
    }

    /* doMove completes all destroy/move writes before its ordered notification
     * pass: destroyed terminal, moved origins far-to-near, then head. */
    if (destroy_offset >= 0) {
        int destroy_x = front_x + destroy_offset * dx[facing];
        int destroy_y = front_y + destroy_offset * dy[facing];
        int destroy_z = front_z + destroy_offset * dz[facing];
        runtime_redstone_notify_neighbors(
            r, destroy_x, destroy_y, destroy_z);
        runtime_redstone_update_observers_at(
            r, destroy_x, destroy_y, destroy_z);
    }
    for (int offset = move_count - 1; offset >= 0; --offset) {
        int origin_x = front_x + offset * dx[facing];
        int origin_y = front_y + offset * dy[facing];
        int origin_z = front_z + offset * dz[facing];
        runtime_redstone_notify_neighbors(
            r, origin_x, origin_y, origin_z);
        runtime_redstone_update_observers_at(
            r, origin_x, origin_y, origin_z);
    }
    runtime_redstone_notify_neighbors(r, front_x, front_y, front_z);
    runtime_redstone_update_observers_at(r, front_x, front_y, front_z);
    gm_world_set_block_meta(r->world, x, y, z, block, meta | 8);
    runtime_redstone_notify_neighbors(r, x, y, z);
    runtime_redstone_update_observers_at(r, x, y, z);

    /* BlockPistonBase.eventReceived samples the extension sound pitch only
     * after doMove and the extended-base state change have succeeded.  Audio
     * output is optional in magma, but this World.rand nextFloat remains
     * causal: the next random block/drop event must see the advanced cursor. */
    (void)runtime_java_random_next_float(r);
}

/* TileEntityPiston.clearPistonTileEntity settles an in-flight tile
 * immediately when BlockPistonBase receives a retraction event. Keep the tile
 * active through its settlement notifications so a recursive base check sees
 * the same in-flight source and cannot start a duplicate event. */
static int runtime_redstone_piston_clear_extending_at(
        GmRuntime *r, int x, int y, int z, int facing) {
    for (int i = 0; i < r->piston_count; ++i) {
        GmRuntimePiston *moving = &r->pistons[i];
        if (!moving->active || moving->dimension != r->dimension
                || moving->x != x || moving->y != y || moving->z != z
                || !moving->extending || moving->facing != facing
                || moving->last_progress >= 1.0f)
            continue;
        moving->progress = 1.0f;
        moving->last_progress = 1.0f;
        if (gm_world_block(r->world, x, y, z) == 36) {
            gm_world_set_block_meta(
                r->world, x, y, z,
                moving->moved_block, moving->moved_meta);
            runtime_redstone_notify_neighbors(r, x, y, z);
            runtime_redstone_update_observers_at(r, x, y, z);
        }
        --r->piston_count;
        if (i != r->piston_count)
            r->pistons[i] = r->pistons[r->piston_count];
        memset(
            &r->pistons[r->piston_count], 0,
            sizeof r->pistons[r->piston_count]);
        return 1;
    }
    return 0;
}

/* Piston retraction. BlockPistonBase replaces the base with a source moving
 * tile, removes the matching head, and samples one contraction-pitch float.
 * A sticky piston additionally pulls one represented non-slime NORMAL state
 * from two cells ahead into the former head cell. An extending tile found at
 * that cell is forcibly settled by the event and suppresses the pull. */
static void runtime_redstone_piston_start_retraction(
        GmRuntime *r, int x, int y, int z, int block, int meta,
        int suppress_sticky_pull) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int facing = meta & 7;
    int front_x;
    int front_y;
    int front_z;
    int pull_x;
    int pull_y;
    int pull_z;
    int pull_block = 0;
    int pull_meta = 0;
    int pulls_block = 0;
    int pulls_slime_structure = 0;
    int front_block;
    int front_meta;
    int clears_front;
    int slime_moved_block[RUNTIME_PISTON_STRUCTURE_MOVES];
    int slime_moved_meta[RUNTIME_PISTON_STRUCTURE_MOVES];
    int slime_destroy_block[RUNTIME_PISTON_STRUCTURE_DESTROYS];
    int slime_destroy_meta[RUNTIME_PISTON_STRUCTURE_DESTROYS];
    int slime_destroy_item_stacks = 0;
    RuntimePistonStructure slime_structure;
    int head_meta;
    GmRuntimePiston *moving;
    if ((block != 29 && block != 33) || facing > 5)
        return;
    front_x = x + dx[facing];
    front_y = y + dy[facing];
    front_z = z + dz[facing];
    head_meta = facing | (block == 29 ? 8 : 0);
    front_block = gm_world_block(r->world, front_x, front_y, front_z);
    front_meta = gm_world_meta(r->world, front_x, front_y, front_z);
    pull_x = front_x + dx[facing];
    pull_y = front_y + dy[facing];
    pull_z = front_z + dz[facing];
    if (block == 29 && !suppress_sticky_pull) {
        pull_block = gm_world_block(r->world, pull_x, pull_y, pull_z);
        pull_meta = gm_world_meta(r->world, pull_x, pull_y, pull_z);
        if (pull_block != 0) {
            if (!gm_block_meta_canonical_1_11_2(
                    pull_block, pull_meta))
                return;
            if (pull_block == 165) {
                int structure_built;
                int saved_front_block = gm_world_block(
                    r->world, front_x, front_y, front_z);
                int saved_front_meta = gm_world_meta(
                    r->world, front_x, front_y, front_z);
                /* BlockPistonBase clears the head before constructing the
                 * retraction helper, so its westward line sees front air. */
                gm_world_set_block_meta(
                    r->world, front_x, front_y, front_z, 0, 0);
                structure_built =
                    runtime_redstone_piston_structure_build_from(
                        r, &slime_structure, x, y, z,
                        pull_x, pull_y, pull_z, facing ^ 1);
                gm_world_set_block_meta(
                    r->world, front_x, front_y, front_z,
                    saved_front_block, saved_front_meta);
                pulls_slime_structure = structure_built
                    && slime_structure.move_count > 0
                    && runtime_redstone_piston_structure_destroy_preflight(
                        r, &slime_structure,
                        slime_destroy_block, slime_destroy_meta,
                        &slime_destroy_item_stacks);
                if (pulls_slime_structure) {
                    int cactus_drops =
                        runtime_redstone_piston_settlement_cactus_drops_at_origins(
                            r, slime_structure.move_x,
                            slime_structure.move_y,
                            slime_structure.move_z,
                            facing ^ 1, slime_structure.move_count);
                    if (cactus_drops
                                > GM_LIVE_MAX - slime_destroy_item_stacks
                            || r->entities.n_active
                                > GM_LIVE_MAX - slime_destroy_item_stacks
                                    - cactus_drops)
                        pulls_slime_structure = 0;
                }
                if (pulls_slime_structure)
                    for (int i = 0;
                            i < slime_structure.move_count; ++i) {
                        slime_moved_block[i] = gm_world_block(
                            r->world,
                            slime_structure.move_x[i],
                            slime_structure.move_y[i],
                            slime_structure.move_z[i]);
                        slime_moved_meta[i] = gm_world_meta(
                            r->world,
                            slime_structure.move_x[i],
                            slime_structure.move_y[i],
                            slime_structure.move_z[i]);
                    }
            } else {
                pulls_block = runtime_redstone_piston_can_push_state(
                    r, pull_x, pull_y, pull_z, facing ^ 1, 0);
            }
        }
    }
    if (r->piston_count
            > GM_RUNTIME_PISTONS - 1 - pulls_block
                - (pulls_slime_structure
                    ? slime_structure.move_count : 0))
        return;
    moving = &r->pistons[r->piston_count++];
    *moving = (GmRuntimePiston){
        .active = 1,
        .dimension = r->dimension,
        .x = x,
        .y = y,
        .z = z,
        .moved_block = block,
        .moved_meta = facing,
        .facing = facing,
        .extending = 0,
        .source = 1,
        .progress = 0.0f,
        .last_progress = 0.0f,
    };
    gm_world_set_block_meta(r->world, x, y, z, 36, head_meta);
    runtime_redstone_notify_neighbors(r, x, y, z);
    runtime_redstone_update_observers_at(r, x, y, z);
    /* Java accepts the queued event even when the serialized extended base
     * has already lost its head. A normal piston always clears its front;
     * sticky doMove clears it only for a pull, while a matching settled head
     * removes itself when the base becomes block 36. Do not erase an
     * unrelated sticky-front block when no pull is admitted. */
    clears_front = block == 33
        || (front_block == 34 && front_meta == head_meta)
        || pulls_block || pulls_slime_structure;
    if (clears_front) {
        gm_world_set_block_meta(
            r->world, front_x, front_y, front_z, 0, 0);
        runtime_redstone_notify_neighbors(r, front_x, front_y, front_z);
        runtime_redstone_update_observers_at(
            r, front_x, front_y, front_z);
    }
    if (pulls_block) {
        gm_world_set_block_meta(
            r->world, pull_x, pull_y, pull_z, 0, 0);
        moving = &r->pistons[r->piston_count++];
        *moving = (GmRuntimePiston){
            .active = 1,
            .dimension = r->dimension,
            .x = front_x,
            .y = front_y,
            .z = front_z,
            .moved_block = pull_block,
            .moved_meta = pull_meta,
            .facing = facing,
            .extending = 0,
            .source = 0,
            .progress = 0.0f,
            .last_progress = 0.0f,
        };
        gm_world_set_block_meta(
            r->world, front_x, front_y, front_z, 36, facing);
        runtime_redstone_notify_neighbors(r, pull_x, pull_y, pull_z);
        runtime_redstone_update_observers_at(
            r, pull_x, pull_y, pull_z);
    } else if (pulls_slime_structure) {
        int move_facing = facing ^ 1;
        if (!runtime_redstone_piston_structure_apply_destroys(
                r, &slime_structure,
                slime_destroy_block, slime_destroy_meta))
            return;
        for (int i = slime_structure.move_count - 1; i >= 0; --i) {
            int destination_x =
                slime_structure.move_x[i] + dx[move_facing];
            int destination_y =
                slime_structure.move_y[i] + dy[move_facing];
            int destination_z =
                slime_structure.move_z[i] + dz[move_facing];
            gm_world_set_block_meta(
                r->world,
                slime_structure.move_x[i],
                slime_structure.move_y[i],
                slime_structure.move_z[i], 0, 0);
            moving = &r->pistons[r->piston_count++];
            *moving = (GmRuntimePiston){
                .active = 1,
                .dimension = r->dimension,
                .x = destination_x,
                .y = destination_y,
                .z = destination_z,
                .moved_block = slime_moved_block[i],
                .moved_meta = slime_moved_meta[i],
                .facing = facing,
                .extending = 0,
                .source = 0,
                .progress = 0.0f,
                .last_progress = 0.0f,
            };
            gm_world_set_block_meta(
                r->world, destination_x, destination_y, destination_z,
                36, facing);
        }
        runtime_redstone_piston_structure_break_destroys(
            r, &slime_structure,
            slime_destroy_block, slime_destroy_meta);
        runtime_redstone_piston_structure_notify_destroys(
            r, &slime_structure);
        for (int i = slime_structure.move_count - 1; i >= 0; --i) {
            runtime_redstone_notify_neighbors(
                r,
                slime_structure.move_x[i],
                slime_structure.move_y[i],
                slime_structure.move_z[i]);
            runtime_redstone_update_observers_at(
                r,
                slime_structure.move_x[i],
                slime_structure.move_y[i],
                slime_structure.move_z[i]);
        }
    }
    (void)runtime_java_random_next_float(r);
}

static void runtime_redstone_piston_check(
        GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int block = gm_world_block(r->world, x, y, z);
    int meta = gm_world_meta(r->world, x, y, z);
    int facing = meta & 7;
    if ((block != 29 && block != 33) || facing > 5)
        return;
    for (int i = 0; i < r->piston_count; ++i) {
        const GmRuntimePiston *moving = &r->pistons[i];
        if (moving->active && moving->source && moving->extending
                && moving->facing == facing
                && moving->x - dx[facing] == x
                && moving->y - dy[facing] == y
                && moving->z - dz[facing] == z) {
            if (!runtime_redstone_piston_direct_supported_power(
                    r, x, y, z, facing)) {
                int front_x = x + dx[facing];
                int front_y = y + dy[facing];
                int front_z = z + dz[facing];
                int suppress_sticky_pull = 0;
                if (!runtime_redstone_piston_clear_extending_at(
                        r, front_x, front_y, front_z, facing))
                    return;
                if (block == 29)
                    suppress_sticky_pull =
                        runtime_redstone_piston_clear_extending_at(
                            r,
                            front_x + dx[facing],
                            front_y + dy[facing],
                            front_z + dz[facing],
                            facing);
                runtime_redstone_piston_start_retraction(
                    r, x, y, z, block, meta,
                    suppress_sticky_pull);
            }
            return;
        }
    }
    if ((meta & 8) != 0) {
        if (!runtime_redstone_piston_direct_supported_power(
                r, x, y, z, facing))
            runtime_redstone_piston_start_retraction(
                r, x, y, z, block, meta, 0);
        return;
    }
    if (!runtime_redstone_piston_direct_supported_power(
            r, x, y, z, facing))
        return;
    runtime_redstone_piston_start_extension(
        r, x, y, z, block, meta);
}

static int runtime_redstone_piston_collision_shapes(
        const GmRuntimePiston *moving, McAABB shapes[2]) {
    if (moving->moved_block == 34
            || (!moving->extending && moving->source
                && (moving->moved_block == 29
                    || moving->moved_block == 33))) {
        /* BlockPistonExtension's plate plus its non-SHORT arm. The source
         * moving tile stores this exact piston-head state while extending. */
        switch (moving->facing) {
            case 0:
                shapes[0] = mc_aabb_make(
                    0.0, 0.0, 0.0, 1.0, 0.25, 1.0);
                shapes[1] = mc_aabb_make(
                    0.375, 0.25, 0.375, 0.625, 1.25, 0.625);
                break;
            case 1:
                shapes[0] = mc_aabb_make(
                    0.0, 0.75, 0.0, 1.0, 1.0, 1.0);
                shapes[1] = mc_aabb_make(
                    0.375, -0.25, 0.375, 0.625, 0.75, 0.625);
                break;
            case 2:
                shapes[0] = mc_aabb_make(
                    0.0, 0.0, 0.0, 1.0, 1.0, 0.25);
                shapes[1] = mc_aabb_make(
                    0.375, 0.375, 0.25, 0.625, 0.625, 1.25);
                break;
            case 3:
                shapes[0] = mc_aabb_make(
                    0.0, 0.0, 0.75, 1.0, 1.0, 1.0);
                shapes[1] = mc_aabb_make(
                    0.375, 0.375, -0.25, 0.625, 0.625, 0.75);
                break;
            case 4:
                shapes[0] = mc_aabb_make(
                    0.0, 0.0, 0.0, 0.25, 1.0, 1.0);
                shapes[1] = mc_aabb_make(
                    0.25, 0.375, 0.375, 1.25, 0.625, 0.625);
                break;
            case 5:
                shapes[0] = mc_aabb_make(
                    0.75, 0.0, 0.0, 1.0, 1.0, 1.0);
                shapes[1] = mc_aabb_make(
                    -0.25, 0.375, 0.375, 0.75, 0.625, 0.625);
                break;
            default:
                return 0;
        }
        return 2;
    }
    if (gm_block_is_normal_cube_1_11_2(
            moving->moved_block, moving->moved_meta)) {
        shapes[0] = mc_aabb_make(
            0.0, 0.0, 0.0, 1.0, 1.0, 1.0);
        return 1;
    }
    return 0;
}

static McAABB runtime_redstone_piston_movement_area(
        const McAABB *shape, int direction, double distance) {
    switch (direction) {
        case 0:
            return mc_aabb_make(
                shape->minX, shape->minY - distance, shape->minZ,
                shape->maxX, shape->minY, shape->maxZ);
        case 1:
            return mc_aabb_make(
                shape->minX, shape->maxY, shape->minZ,
                shape->maxX, shape->maxY + distance, shape->maxZ);
        case 2:
            return mc_aabb_make(
                shape->minX, shape->minY, shape->minZ - distance,
                shape->maxX, shape->maxY, shape->minZ);
        case 3:
            return mc_aabb_make(
                shape->minX, shape->minY, shape->maxZ,
                shape->maxX, shape->maxY, shape->maxZ + distance);
        case 4:
            return mc_aabb_make(
                shape->minX - distance, shape->minY, shape->minZ,
                shape->minX, shape->maxY, shape->maxZ);
        case 5:
            return mc_aabb_make(
                shape->maxX, shape->minY, shape->minZ,
                shape->maxX + distance, shape->maxY, shape->maxZ);
        default:
            return mc_aabb_make(0.0, 0.0, 0.0, 0.0, 0.0, 0.0);
    }
}

static double runtime_redstone_piston_entity_movement(
        const McAABB *area, int direction, const McAABB *entity) {
    switch (direction) {
        case 0: return entity->maxY - area->minY;
        case 1: return area->maxY - entity->minY;
        case 2: return entity->maxZ - area->minZ;
        case 3: return area->maxZ - entity->minZ;
        case 4: return entity->maxX - area->minX;
        case 5: return area->maxX - entity->minX;
        default: return 0.0;
    }
}

static int runtime_redstone_piston_living_type(int type) {
    return ehs_is_hostile((u8)type)
        || type == EW_TYPE_SHEEP
        || type == EW_TYPE_PIG
        || type == EW_TYPE_COW
        || type == EW_TYPE_CHICKEN;
}

static double runtime_entity_static_axis_offset(
        const GmRuntime *r, const McAABB *entity,
        int axis, double offset,
        int x0, int y0, int z0, int x1, int y1, int z1);

static void runtime_redstone_piston_move_entities(
        GmRuntime *r, GmRuntimePiston *moving, float next_progress,
        double accumulated[GM_LIVE_MAX][3],
        double mob_accumulated[EW_MAX_ENTITIES][3]) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    McAABB local_shapes[2];
    McAABB areas[2];
    int shape_count;
    int direction;
    int axis;
    int sign;
    double distance = (double)(next_progress - moving->progress);
    float extended_progress;

    if (moving->dimension != r->dimension
            || distance <= 0.0
            || moving->facing < 0 || moving->facing > 5
            || (r->entities.n_active == 0
                && gm_mobs_living_count(&r->mobs) == 0))
        return;
    shape_count = runtime_redstone_piston_collision_shapes(
        moving, local_shapes);
    if (shape_count == 0)
        return;
    direction = moving->extending ? moving->facing : (moving->facing ^ 1);
    axis = direction < 2 ? 1 : (direction < 4 ? 2 : 0);
    sign = (direction & 1) ? 1 : -1;
    extended_progress = moving->extending
        ? moving->progress - 1.0f
        : 1.0f - moving->progress;
    for (int shape_index = 0; shape_index < shape_count; ++shape_index) {
        McAABB *shape = &local_shapes[shape_index];
        *shape = mc_aabb_offset(
            shape,
            (double)moving->x
                + (double)extended_progress * (double)dx[moving->facing],
            (double)moving->y
                + (double)extended_progress * (double)dy[moving->facing],
            (double)moving->z
                + (double)extended_progress * (double)dz[moving->facing]);
        areas[shape_index] = runtime_redstone_piston_movement_area(
            shape, direction, distance);
    }
    for (int item_index = 0; item_index < GM_LIVE_MAX; ++item_index) {
        GmLiveEnt *item = &r->entities.ents[item_index];
        McAABB item_box;
        double movement = 0.0;
        double requested;
        double total;
        double actual;
        if (!item->active || item->type != 0)
            continue;
        item_box = mc_aabb_make(
            item->x - 0.125, item->y, item->z - 0.125,
            item->x + 0.125, item->y + 0.25, item->z + 0.125);
        for (int shape_index = 0;
                shape_index < shape_count; ++shape_index) {
            double candidate;
            if (!mc_aabb_intersects(&areas[shape_index], &item_box))
                continue;
            candidate = runtime_redstone_piston_entity_movement(
                &areas[shape_index], direction, &item_box);
            if (candidate > movement)
                movement = candidate;
        }
        if (movement <= 0.0)
            continue;
        if (moving->moved_block == 165) {
            if (axis == 0) item->mx = (double)sign;
            else if (axis == 1) item->my = (double)sign;
            else item->mz = (double)sign;
        }
        requested = (movement < distance ? movement : distance) + 0.01;
        total = accumulated[item_index][axis]
            + (double)sign * requested;
        if (total < -0.51) total = -0.51;
        if (total > 0.51) total = 0.51;
        actual = total - accumulated[item_index][axis];
        accumulated[item_index][axis] = total;
        {
            double min_x = item_box.minX;
            double min_y = item_box.minY;
            double min_z = item_box.minZ;
            double max_x = item_box.maxX;
            double max_y = item_box.maxY;
            double max_z = item_box.maxZ;
            if (axis == 0) {
                if (actual < 0.0) min_x += actual;
                else max_x += actual;
            } else if (axis == 1) {
                if (actual < 0.0) min_y += actual;
                else max_y += actual;
            } else {
                if (actual < 0.0) min_z += actual;
                else max_z += actual;
            }
            actual = runtime_entity_static_axis_offset(
                r, &item_box, axis, actual,
                (int)floor(min_x), (int)floor(min_y),
                (int)floor(min_z),
                (int)floor(max_x - 1.0e-9),
                (int)floor(max_y - 1.0e-9),
                (int)floor(max_z - 1.0e-9));
        }
        if (axis == 0) item->x += actual;
        else if (axis == 1) item->y += actual;
        else item->z += actual;
    }
    {
        EwStore *mobs = r->mobs.current ? &r->mobs.b : &r->mobs.a;
        for (int mob_index = 1;
                mob_index < EW_MAX_ENTITIES; ++mob_index) {
            McAABB mob_box;
            float width, height;
            double movement = 0.0;
            double requested;
            double total;
            double actual;
            if (!mobs->alive[mob_index]
                    || r->mobs.entity_dimension[mob_index] != r->dimension
                    || !runtime_redstone_piston_living_type(
                        mobs->type[mob_index]))
                continue;
            ehs_size_scaled(
                mobs->type[mob_index], r->mobs.size[mob_index],
                &width, &height);
            mob_box = mc_aabb_make(
                mobs->x[mob_index] - (double)width * 0.5,
                mobs->y[mob_index],
                mobs->z[mob_index] - (double)width * 0.5,
                mobs->x[mob_index] + (double)width * 0.5,
                mobs->y[mob_index] + (double)height,
                mobs->z[mob_index] + (double)width * 0.5);
            for (int shape_index = 0;
                    shape_index < shape_count; ++shape_index) {
                double candidate;
                if (!mc_aabb_intersects(&areas[shape_index], &mob_box))
                    continue;
                candidate = runtime_redstone_piston_entity_movement(
                    &areas[shape_index], direction, &mob_box);
                if (candidate > movement)
                    movement = candidate;
            }
            if (movement <= 0.0)
                continue;
            if (moving->moved_block == 165) {
                if (axis == 0) mobs->vx[mob_index] = (double)sign;
                else if (axis == 1) mobs->vy[mob_index] = (double)sign;
                else mobs->vz[mob_index] = (double)sign;
            }
            requested = (movement < distance ? movement : distance) + 0.01;
            total = mob_accumulated[mob_index][axis]
                + (double)sign * requested;
            if (total < -0.51) total = -0.51;
            if (total > 0.51) total = 0.51;
            actual = total - mob_accumulated[mob_index][axis];
            mob_accumulated[mob_index][axis] = total;
            {
                double min_x = mob_box.minX;
                double min_y = mob_box.minY;
                double min_z = mob_box.minZ;
                double max_x = mob_box.maxX;
                double max_y = mob_box.maxY;
                double max_z = mob_box.maxZ;
                if (axis == 0) {
                    if (actual < 0.0) min_x += actual;
                    else max_x += actual;
                } else if (axis == 1) {
                    if (actual < 0.0) min_y += actual;
                    else max_y += actual;
                } else {
                    if (actual < 0.0) min_z += actual;
                    else max_z += actual;
                }
                actual = runtime_entity_static_axis_offset(
                    r, &mob_box, axis, actual,
                    (int)floor(min_x), (int)floor(min_y),
                    (int)floor(min_z),
                    (int)floor(max_x - 1.0e-9),
                    (int)floor(max_y - 1.0e-9),
                    (int)floor(max_z - 1.0e-9));
            }
            if (axis == 0) mobs->x[mob_index] += actual;
            else if (axis == 1) mobs->y[mob_index] += actual;
            else mobs->z[mob_index] += actual;
        }
    }
}

static void runtime_tick_pistons(GmRuntime *r) {
    int index = 0;
    if (r->piston_count == 0)
        return;
    double accumulated[GM_LIVE_MAX][3] = {{0.0}};
    double mob_accumulated[EW_MAX_ENTITIES][3] = {{0.0}};
    while (index < r->piston_count) {
        GmRuntimePiston *moving = &r->pistons[index];
        moving->last_progress = moving->progress;
        if (moving->last_progress >= 1.0f) {
            if (moving->dimension == r->dimension
                    && gm_world_block(
                        r->world, moving->x, moving->y, moving->z) == 36) {
                gm_world_set_block_meta(
                    r->world, moving->x, moving->y, moving->z,
                    moving->moved_block, moving->moved_meta);
                runtime_redstone_notify_neighbors(
                    r, moving->x, moving->y, moving->z);
                runtime_redstone_update_observers_at(
                    r, moving->x, moving->y, moving->z);
                /* TileEntityPiston settlement restores the moved state with
                 * setBlockState. A restored piston base runs onBlockAdded and
                 * must observe power that arrived while it was block 36.
                 * Java queues the resulting piston BlockEvent after the
                 * current WorldServer event flush, so it is not visible as a
                 * moving tile until the following tick. */
                if ((moving->moved_block == 29
                            || moving->moved_block == 33)
                        && runtime_redstone_piston_direct_supported_power(
                            r, moving->x, moving->y, moving->z,
                            moving->moved_meta & 7)
                        && r->piston_recheck_count < GM_RUNTIME_PISTONS) {
                    int duplicate = 0;
                    for (int recheck = 0;
                            recheck < r->piston_recheck_count; ++recheck)
                        if (r->piston_recheck_x[recheck] == moving->x
                                && r->piston_recheck_y[recheck] == moving->y
                                && r->piston_recheck_z[recheck] == moving->z) {
                            duplicate = 1;
                            break;
                        }
                    if (!duplicate) {
                        int recheck = r->piston_recheck_count++;
                        r->piston_recheck_x[recheck] = moving->x;
                        r->piston_recheck_y[recheck] = moving->y;
                        r->piston_recheck_z[recheck] = moving->z;
                    }
                }
            }
            --r->piston_count;
            if (index != r->piston_count)
                r->pistons[index] = r->pistons[r->piston_count];
            memset(
                &r->pistons[r->piston_count], 0,
                sizeof r->pistons[r->piston_count]);
            continue;
        }
        {
            float next_progress = moving->progress + 0.5f;
            if (next_progress > 1.0f)
                next_progress = 1.0f;
            runtime_redstone_piston_move_entities(
                r, moving, next_progress, accumulated, mob_accumulated);
            moving->progress = next_progress;
        }
        ++index;
    }
}

static void runtime_redstone_piston_process_rechecks(GmRuntime *r) {
    int count = r->piston_recheck_count;
    r->piston_recheck_count = 0;
    for (int recheck = 0; recheck < count; ++recheck)
        runtime_redstone_piston_check(
            r,
            r->piston_recheck_x[recheck],
            r->piston_recheck_y[recheck],
            r->piston_recheck_z[recheck]);
}

static double runtime_redstone_piston_dynamic_axis_offset(
        const McAABB *obstacle, const McAABB *entity,
        int axis, double offset) {
    if (axis == 0) {
        if (entity->maxY <= obstacle->minY
                || entity->minY >= obstacle->maxY
                || entity->maxZ <= obstacle->minZ
                || entity->minZ >= obstacle->maxZ)
            return offset;
        if (offset > 0.0 && entity->maxX <= obstacle->minX) {
            double gap = obstacle->minX - entity->maxX;
            if (gap < offset)
                offset = gap;
        } else if (offset < 0.0
                && entity->minX >= obstacle->maxX) {
            double gap = obstacle->maxX - entity->minX;
            if (gap > offset)
                offset = gap;
        }
    } else if (axis == 1) {
        if (entity->maxX <= obstacle->minX
                || entity->minX >= obstacle->maxX
                || entity->maxZ <= obstacle->minZ
                || entity->minZ >= obstacle->maxZ)
            return offset;
        if (offset > 0.0 && entity->maxY <= obstacle->minY) {
            double gap = obstacle->minY - entity->maxY;
            if (gap < offset)
                offset = gap;
        } else if (offset < 0.0
                && entity->minY >= obstacle->maxY) {
            double gap = obstacle->maxY - entity->minY;
            if (gap > offset)
                offset = gap;
        }
    } else {
        if (entity->maxX <= obstacle->minX
                || entity->minX >= obstacle->maxX
                || entity->maxY <= obstacle->minY
                || entity->minY >= obstacle->maxY)
            return offset;
        if (offset > 0.0 && entity->maxZ <= obstacle->minZ) {
            double gap = obstacle->minZ - entity->maxZ;
            if (gap < offset)
                offset = gap;
        } else if (offset < 0.0
                && entity->minZ >= obstacle->maxZ) {
            double gap = obstacle->maxZ - entity->minZ;
            if (gap > offset)
                offset = gap;
        }
    }
    return offset;
}

static int runtime_is_stair_id(int id) {
    switch (id) {
        case 53: case 67: case 108: case 109: case 114: case 128:
        case 134: case 135: case 136: case 156: case 163: case 164:
        case 180: case 203:
            return 1;
        default:
            return 0;
    }
}

static int runtime_is_fence_id(int id) {
    switch (id) {
        case 85: case 113: case 188: case 189:
        case 190: case 191: case 192:
            return 1;
        default:
            return 0;
    }
}

static int runtime_is_fence_gate_id(int id) {
    return id == 107 || (id >= 183 && id <= 187);
}

static int runtime_is_gourd_id(int id) {
    return id == 86 || id == 91 || id == 103;
}

static int runtime_is_opaque_full_cube(int id, int meta) {
    /* Captured 1.11.2 registry: opaque material && full cube equals the
     * normal-cube table except for power-providing redstone/observer blocks. */
    return gm_block_is_normal_cube_1_11_2(id, meta)
        || id == 152 || id == 218;
}

static int runtime_fence_connects(
        const GmRuntime *r, int x, int y, int z, int direction,
        int fence_id) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int id = gm_world_block(r->world, x + dx[direction], y, z + dz[direction]);
    int meta = gm_world_meta(r->world, x + dx[direction], y, z + dz[direction]);
    if (runtime_is_fence_gate_id(id))
        return 1;
    if (runtime_is_fence_id(id))
        return (id == 113) == (fence_id == 113);
    return id != 166 && !runtime_is_gourd_id(id)
        && runtime_is_opaque_full_cube(id, meta);
}

static int runtime_wall_connects(
        const GmRuntime *r, int x, int y, int z, int direction) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int id = gm_world_block(r->world, x + dx[direction], y, z + dz[direction]);
    int meta = gm_world_meta(r->world, x + dx[direction], y, z + dz[direction]);
    if (id == 139 || runtime_is_fence_gate_id(id))
        return 1;
    return id != 166 && !runtime_is_gourd_id(id)
        && runtime_is_opaque_full_cube(id, meta);
}

static int runtime_fence_collision_shapes(
        const GmRuntime *r, int x, int y, int z, int id,
        McAABB shapes[5]) {
    int count = 0;
    shapes[count++] = mc_aabb_make(
        (double)x + 0.375, (double)y, (double)z + 0.375,
        (double)x + 0.625, (double)y + 1.5, (double)z + 0.625);
    if (runtime_fence_connects(r, x, y, z, 2, id))
        shapes[count++] = mc_aabb_make(
            (double)x + 0.375, (double)y, (double)z,
            (double)x + 0.625, (double)y + 1.5, (double)z + 0.375);
    if (runtime_fence_connects(r, x, y, z, 5, id))
        shapes[count++] = mc_aabb_make(
            (double)x + 0.625, (double)y, (double)z + 0.375,
            (double)x + 1.0, (double)y + 1.5, (double)z + 0.625);
    if (runtime_fence_connects(r, x, y, z, 3, id))
        shapes[count++] = mc_aabb_make(
            (double)x + 0.375, (double)y, (double)z + 0.625,
            (double)x + 0.625, (double)y + 1.5, (double)z + 1.0);
    if (runtime_fence_connects(r, x, y, z, 4, id))
        shapes[count++] = mc_aabb_make(
            (double)x, (double)y, (double)z + 0.375,
            (double)x + 0.375, (double)y + 1.5, (double)z + 0.625);
    return count;
}

static int runtime_wall_collision_shapes(
        const GmRuntime *r, int x, int y, int z, McAABB shapes[5]) {
    int north = runtime_wall_connects(r, x, y, z, 2);
    int south = runtime_wall_connects(r, x, y, z, 3);
    int west = runtime_wall_connects(r, x, y, z, 4);
    int east = runtime_wall_connects(r, x, y, z, 5);
    double x0 = west ? 0.0 : 0.25;
    double x1 = east ? 1.0 : 0.75;
    double z0 = north ? 0.0 : 0.25;
    double z1 = south ? 1.0 : 0.75;
    if (north && south && !west && !east) {
        x0 = 0.3125;
        x1 = 0.6875;
    } else if (west && east && !north && !south) {
        z0 = 0.3125;
        z1 = 0.6875;
    }
    shapes[0] = mc_aabb_make(
        (double)x + x0, (double)y, (double)z + z0,
        (double)x + x1, (double)y + 1.5, (double)z + z1);
    return 1;
}

static int runtime_fence_gate_collision_shapes(
        int x, int y, int z, int meta, McAABB shapes[5]) {
    double x0 = 0.0, x1 = 1.0, z0 = 0.0, z1 = 1.0;
    if (meta & 4)
        return 0;
    if ((meta & 1) == 0) {
        z0 = 0.375;
        z1 = 0.625;
    } else {
        x0 = 0.375;
        x1 = 0.625;
    }
    shapes[0] = mc_aabb_make(
        (double)x + x0, (double)y, (double)z + z0,
        (double)x + x1, (double)y + 1.5, (double)z + z1);
    return 1;
}

typedef enum RuntimeStairShape {
    RUNTIME_STAIR_STRAIGHT,
    RUNTIME_STAIR_INNER_LEFT,
    RUNTIME_STAIR_INNER_RIGHT,
    RUNTIME_STAIR_OUTER_LEFT,
    RUNTIME_STAIR_OUTER_RIGHT
} RuntimeStairShape;

static int runtime_stair_rotate_y(int facing) {
    static const int rotated[6] = {0, 1, 5, 4, 2, 3};
    return facing >= 2 && facing <= 5 ? rotated[facing] : facing;
}

static int runtime_stair_rotate_y_ccw(int facing) {
    static const int rotated[6] = {0, 1, 4, 5, 3, 2};
    return facing >= 2 && facing <= 5 ? rotated[facing] : facing;
}

static int runtime_stair_is_different(
        const GmRuntime *r, int x, int y, int z, int direction,
        int facing, int top) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int id = gm_world_block(r->world, x + dx[direction], y, z + dz[direction]);
    int meta = gm_world_meta(r->world, x + dx[direction], y, z + dz[direction]);
    return !runtime_is_stair_id(id)
        || 5 - (meta & 3) != facing
        || ((meta & 4) != 0) != top;
}

static RuntimeStairShape runtime_stair_shape(
        const GmRuntime *r, int x, int y, int z, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    RuntimeStairShape shape = RUNTIME_STAIR_STRAIGHT;
    int facing = 5 - (meta & 3);
    int top = (meta & 4) != 0;
    int neighbor_id, neighbor_meta, neighbor_facing;

    neighbor_id = gm_world_block(
        r->world, x + dx[facing], y, z + dz[facing]);
    neighbor_meta = gm_world_meta(
        r->world, x + dx[facing], y, z + dz[facing]);
    if (runtime_is_stair_id(neighbor_id)
            && ((neighbor_meta & 4) != 0) == top) {
        neighbor_facing = 5 - (neighbor_meta & 3);
        if ((neighbor_facing < 4) != (facing < 4)
                && runtime_stair_is_different(
                    r, x, y, z, neighbor_facing ^ 1, facing, top))
            shape = neighbor_facing == runtime_stair_rotate_y_ccw(facing)
                ? RUNTIME_STAIR_OUTER_LEFT : RUNTIME_STAIR_OUTER_RIGHT;
    }
    if (shape == RUNTIME_STAIR_STRAIGHT) {
        int opposite = facing ^ 1;
        neighbor_id = gm_world_block(
            r->world, x + dx[opposite], y, z + dz[opposite]);
        neighbor_meta = gm_world_meta(
            r->world, x + dx[opposite], y, z + dz[opposite]);
        if (runtime_is_stair_id(neighbor_id)
                && ((neighbor_meta & 4) != 0) == top) {
            neighbor_facing = 5 - (neighbor_meta & 3);
            if ((neighbor_facing < 4) != (facing < 4)
                    && runtime_stair_is_different(
                        r, x, y, z, neighbor_facing, facing, top))
                shape = neighbor_facing == runtime_stair_rotate_y_ccw(facing)
                    ? RUNTIME_STAIR_INNER_LEFT : RUNTIME_STAIR_INNER_RIGHT;
        }
    }
    return shape;
}

static int runtime_stair_collision_shapes(
        const GmRuntime *r, int x, int y, int z, int meta,
        McAABB shapes[5]) {
    RuntimeStairShape shape = runtime_stair_shape(r, x, y, z, meta);
    int facing = 5 - (meta & 3);
    int top = (meta & 4) != 0;
    int count = 0;
    double part_y0 = top ? 0.0 : 0.5;
    double part_y1 = top ? 0.5 : 1.0;

    shapes[count++] = mc_aabb_make(
        (double)x, (double)y + (top ? 0.5 : 0.0), (double)z,
        (double)x + 1.0, (double)y + (top ? 1.0 : 0.5),
        (double)z + 1.0);
    if (shape == RUNTIME_STAIR_STRAIGHT
            || shape == RUNTIME_STAIR_INNER_LEFT
            || shape == RUNTIME_STAIR_INNER_RIGHT) {
        double x0 = 0.0, x1 = 1.0, z0 = 0.0, z1 = 1.0;
        if (facing == 2) z1 = 0.5;
        else if (facing == 3) z0 = 0.5;
        else if (facing == 4) x1 = 0.5;
        else x0 = 0.5;
        shapes[count++] = mc_aabb_make(
            (double)x + x0, (double)y + part_y0, (double)z + z0,
            (double)x + x1, (double)y + part_y1, (double)z + z1);
    }
    if (shape != RUNTIME_STAIR_STRAIGHT) {
        int corner_facing;
        double x0, x1, z0, z1;
        if (shape == RUNTIME_STAIR_OUTER_RIGHT)
            corner_facing = runtime_stair_rotate_y(facing);
        else if (shape == RUNTIME_STAIR_INNER_RIGHT)
            corner_facing = facing ^ 1;
        else if (shape == RUNTIME_STAIR_INNER_LEFT)
            corner_facing = runtime_stair_rotate_y_ccw(facing);
        else
            corner_facing = facing;
        x0 = corner_facing == 2 || corner_facing == 4 ? 0.0 : 0.5;
        x1 = x0 + 0.5;
        z0 = corner_facing == 2 || corner_facing == 5 ? 0.0 : 0.5;
        z1 = z0 + 0.5;
        shapes[count++] = mc_aabb_make(
            (double)x + x0, (double)y + part_y0, (double)z + z0,
            (double)x + x1, (double)y + part_y1, (double)z + z1);
    }
    return count;
}

static int runtime_is_pane_id(int id) {
    return id == 101 || id == 102 || id == 160;
}

static int runtime_stair_side_solid(
        const GmRuntime *r, int x, int y, int z, int meta, int side) {
    RuntimeStairShape shape = runtime_stair_shape(r, x, y, z, meta);
    int facing = 5 - (meta & 3);
    int top = (meta & 4) != 0;
    if (facing == side)
        return 1;
    if (shape == RUNTIME_STAIR_INNER_LEFT)
        return side == (top
            ? runtime_stair_rotate_y_ccw(facing)
            : runtime_stair_rotate_y(facing));
    if (shape == RUNTIME_STAIR_INNER_RIGHT)
        return side == (top
            ? runtime_stair_rotate_y(facing)
            : runtime_stair_rotate_y_ccw(facing));
    return 0;
}

static int runtime_pane_side_solid(
        const GmRuntime *r, int x, int y, int z,
        int id, int meta, int side) {
    if (id == 60)
        return 1; /* Forge BlockFarmland: every horizontal side is solid. */
    if (id == 78)
        return (meta & 7) == 7;
    if (runtime_is_stair_id(id))
        return runtime_stair_side_solid(r, x, y, z, meta, side);
    if (id == 152)
        return 1; /* Forge BlockCompressedPowered override. */
    return gm_block_is_normal_cube_1_11_2(id, meta);
}

static int runtime_pane_connects(
        const GmRuntime *r, int x, int y, int z, int direction) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int nx = x + dx[direction];
    int nz = z + dz[direction];
    int id = gm_world_block(r->world, nx, y, nz);
    int meta = gm_world_meta(r->world, nx, y, nz);
    return runtime_is_pane_id(id)
        || id == 20 || id == 95
        || gm_block_is_full_cube_1_11_2(id, 0)
        || runtime_pane_side_solid(
            r, nx, y, nz, id, meta, direction ^ 1);
}

static int runtime_pane_collision_shapes(
        const GmRuntime *r, int x, int y, int z, McAABB shapes[5]) {
    int count = 0;
    shapes[count++] = mc_aabb_make(
        (double)x + 0.4375, (double)y, (double)z + 0.4375,
        (double)x + 0.5625, (double)y + 1.0, (double)z + 0.5625);
    if (runtime_pane_connects(r, x, y, z, 2))
        shapes[count++] = mc_aabb_make(
            (double)x + 0.4375, (double)y, (double)z,
            (double)x + 0.5625, (double)y + 1.0,
            (double)z + 0.4375);
    if (runtime_pane_connects(r, x, y, z, 5))
        shapes[count++] = mc_aabb_make(
            (double)x + 0.5625, (double)y, (double)z + 0.4375,
            (double)x + 1.0, (double)y + 1.0,
            (double)z + 0.5625);
    if (runtime_pane_connects(r, x, y, z, 3))
        shapes[count++] = mc_aabb_make(
            (double)x + 0.4375, (double)y, (double)z + 0.5625,
            (double)x + 0.5625, (double)y + 1.0,
            (double)z + 1.0);
    if (runtime_pane_connects(r, x, y, z, 4))
        shapes[count++] = mc_aabb_make(
            (double)x, (double)y, (double)z + 0.4375,
            (double)x + 0.4375, (double)y + 1.0,
            (double)z + 0.5625);
    return count;
}

static int runtime_trapdoor_collision_shapes(
        int x, int y, int z, int meta, McAABB shapes[5]) {
    double x0 = 0.0, x1 = 1.0;
    double y0 = 0.0, y1 = 1.0;
    double z0 = 0.0, z1 = 1.0;
    if (meta & 4) {
        switch (meta & 3) {
            case 0: z0 = 0.8125; break; /* NORTH */
            case 1: z1 = 0.1875; break; /* SOUTH */
            case 2: x0 = 0.8125; break; /* WEST */
            default: x1 = 0.1875; break; /* EAST */
        }
    } else if (meta & 8) {
        y0 = 0.8125;
    } else {
        y1 = 0.1875;
    }
    shapes[0] = mc_aabb_make(
        (double)x + x0, (double)y + y0, (double)z + z0,
        (double)x + x1, (double)y + y1, (double)z + z1);
    return 1;
}

enum { RUNTIME_STATIC_MAX_SHAPES = 7 };

static int runtime_static_collision_shapes(
        const GmRuntime *r, int x, int y, int z, int id, int meta,
        McAABB shapes[RUNTIME_STATIC_MAX_SHAPES]) {
    if (id == 29 || id == 33) {
        /* BlockPistonBase is a full cube while retracted. Extended bases keep
         * the 3/4 body opposite their facing for both normal and sticky IDs. */
        double x0 = 0.0, x1 = 1.0;
        double y0 = 0.0, y1 = 1.0;
        double z0 = 0.0, z1 = 1.0;
        int facing = meta & 7;
        if ((meta & 8) && facing <= 5) {
            if (facing == 0) y0 = 0.25;
            else if (facing == 1) y1 = 0.75;
            else if (facing == 2) z0 = 0.25;
            else if (facing == 3) z1 = 0.75;
            else if (facing == 4) x0 = 0.25;
            else x1 = 0.75;
        }
        shapes[0] = mc_aabb_make(
            (double)x + x0, (double)y + y0, (double)z + z0,
            (double)x + x1, (double)y + y1, (double)z + z1);
        return 1;
    }
    if (id == 78) {
        /* BlockSnow collision is one layer shorter than its outline:
         * metadata 0 has no collision, metadata 7 reaches 7/8. */
        double height = (double)(meta & 7) * 0.125;
        if (height == 0.0)
            return 0;
        shapes[0] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + height, (double)z + 1.0);
        return 1;
    }
    if (id == 171) {
        /* BlockCarpet.CARPET_AABB: full footprint, 1/16 high. */
        shapes[0] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 0.0625, (double)z + 1.0);
        return 1;
    }
    if (id == 26) {
        /* Both bed parts share BlockBed.BED_AABB. */
        shapes[0] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 0.5625, (double)z + 1.0);
        return 1;
    }
    if (id == 92) {
        /* Each cake bite removes 1/8 from the west edge. */
        int bites = meta;
        if (bites < 0) bites = 0;
        if (bites > 6) bites = 6;
        shapes[0] = mc_aabb_make(
            (double)x + (double)(1 + 2 * bites) / 16.0,
            (double)y, (double)z + 0.0625,
            (double)x + 0.9375, (double)y + 0.5,
            (double)z + 0.9375);
        return 1;
    }
    if (id == 116) {
        /* BlockEnchantmentTable.AABB: full footprint, 3/4 high. */
        shapes[0] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 0.75, (double)z + 1.0);
        return 1;
    }
    if (id == 151 || id == 178) {
        /* Normal and inverted daylight detectors share a 3/8-high box. */
        shapes[0] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 0.375, (double)z + 1.0);
        return 1;
    }
    if (id == 93 || id == 94 || id == 149 || id == 150) {
        /* BlockRedstoneDiode.REDSTONE_DIODE_AABB is inherited by both
         * powered states of repeaters and comparators. */
        shapes[0] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 0.125, (double)z + 1.0);
        return 1;
    }
    if (id == 117) {
        /* BlockBrewingStand adds STICK_AABB before BASE_AABB. Bottle bits do
         * not change either collision box. */
        shapes[0] = mc_aabb_make(
            (double)x + 0.4375, (double)y, (double)z + 0.4375,
            (double)x + 0.5625, (double)y + 0.875,
            (double)z + 0.5625);
        shapes[1] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 0.125, (double)z + 1.0);
        return 2;
    }
    if (id == 120) {
        /* BlockEndPortalFrame is a 13/16-high base plus a centered eye. */
        int count = 0;
        shapes[count++] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 0.8125, (double)z + 1.0);
        if (meta & 4)
            shapes[count++] = mc_aabb_make(
                (double)x + 0.3125, (double)y + 0.8125,
                (double)z + 0.3125,
                (double)x + 0.6875, (double)y + 1.0,
                (double)z + 0.6875);
        return count;
    }
    if (id == 145) {
        /* BlockAnvil: horizontal facing selects a full-length axis and
         * leaves a 1/8 inset on both sides of the perpendicular axis. */
        double x0 = (meta & 1) ? 0.0 : 0.125;
        double x1 = (meta & 1) ? 1.0 : 0.875;
        double z0 = (meta & 1) ? 0.125 : 0.0;
        double z1 = (meta & 1) ? 0.875 : 1.0;
        shapes[0] = mc_aabb_make(
            (double)x + x0, (double)y, (double)z + z0,
            (double)x + x1, (double)y + 1.0, (double)z + z1);
        return 1;
    }
    if (id == 198) {
        /* BlockEndRod: metadata is EnumFacing's index; opposite facings
         * share one of the three 1/4-thick axis-aligned rods. */
        int axis = (meta & 7) < 2 ? 1 : (meta & 7) < 4 ? 2 : 0;
        double x0 = axis == 0 ? 0.0 : 0.375;
        double x1 = axis == 0 ? 1.0 : 0.625;
        double y0 = axis == 1 ? 0.0 : 0.375;
        double y1 = axis == 1 ? 1.0 : 0.625;
        double z0 = axis == 2 ? 0.0 : 0.375;
        double z1 = axis == 2 ? 1.0 : 0.625;
        shapes[0] = mc_aabb_make(
            (double)x + x0, (double)y + y0, (double)z + z0,
            (double)x + x1, (double)y + y1, (double)z + z1);
        return 1;
    }
    if (id == 122) {
        /* BlockDragonEgg: full height with a 1/16 horizontal inset. */
        shapes[0] = mc_aabb_make(
            (double)x + 0.0625, (double)y, (double)z + 0.0625,
            (double)x + 0.9375, (double)y + 1.0,
            (double)z + 0.9375);
        return 1;
    }
    if (id == 60 || id == 208) {
        /* Farmland and grass paths share a full-footprint 15/16 box. */
        shapes[0] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 0.9375,
            (double)z + 1.0);
        return 1;
    }
    if (id == 81) {
        /* BlockCactus.CACTUS_COLLISION_AABB is shorter than its outline. */
        shapes[0] = mc_aabb_make(
            (double)x + 0.0625, (double)y, (double)z + 0.0625,
            (double)x + 0.9375, (double)y + 0.9375,
            (double)z + 0.9375);
        return 1;
    }
    if (id == 111) {
        /* Boats are the sole BlockLilyPad collision exception; represented
         * piston entities use the ordinary 7/8-wide, 3/32-high box. */
        shapes[0] = mc_aabb_make(
            (double)x + 0.0625, (double)y, (double)z + 0.0625,
            (double)x + 0.9375, (double)y + 0.09375,
            (double)z + 0.9375);
        return 1;
    }
    if (id == 130) {
        /* Ender chests do not join: every state uses one inset 7/8 box. */
        shapes[0] = mc_aabb_make(
            (double)x + 0.0625, (double)y, (double)z + 0.0625,
            (double)x + 0.9375, (double)y + 0.875,
            (double)z + 0.9375);
        return 1;
    }
    if (id == 140) {
        shapes[0] = mc_aabb_make(
            (double)x + 0.3125, (double)y, (double)z + 0.3125,
            (double)x + 0.6875, (double)y + 0.375,
            (double)z + 0.6875);
        return 1;
    }
    if (id == 144) {
        /* BlockSkull metadata uses EnumFacing's D-U-N-S-W-E index. */
        int facing = (meta & 7) % 6;
        double x0 = 0.25, x1 = 0.75;
        double y0 = 0.25, y1 = 0.75;
        double z0 = 0.25, z1 = 0.75;
        if (facing == 0 || facing == 1) {
            y0 = 0.0;
            y1 = 0.5;
        } else if (facing == 2) {
            z0 = 0.5;
            z1 = 1.0;
        } else if (facing == 3) {
            z0 = 0.0;
            z1 = 0.5;
        } else if (facing == 4) {
            x0 = 0.5;
            x1 = 1.0;
        } else {
            x0 = 0.0;
            x1 = 0.5;
        }
        shapes[0] = mc_aabb_make(
            (double)x + x0, (double)y + y0, (double)z + z0,
            (double)x + x1, (double)y + y1, (double)z + z1);
        return 1;
    }
    if (id == 65) {
        /* Vertical metadata falls back to NORTH in BlockLadder. */
        int facing = meta % 6;
        double x0 = 0.0, x1 = 1.0;
        double z0 = 0.0, z1 = 1.0;
        if (facing == 0 || facing == 1 || facing == 2)
            z0 = 0.8125;
        else if (facing == 3)
            z1 = 0.1875;
        else if (facing == 4)
            x0 = 0.8125;
        else
            x1 = 0.1875;
        shapes[0] = mc_aabb_make(
            (double)x + x0, (double)y, (double)z + z0,
            (double)x + x1, (double)y + 1.0, (double)z + z1);
        return 1;
    }
    if (id == 127) {
        /* Cocoa metadata packs horizontal S-W-N-E facing in bits 0..1 and
         * age 0..2 in bits 2..3. Each age grows equally in both axes. */
        int facing = meta & 3;
        int age = (meta >> 2) & 3;
        double width, half, x0, x1, z0, z1;
        if (age > 2) age = 2;
        width = (double)(age + 2) / 8.0;
        half = width * 0.5;
        x0 = 0.5 - half;
        x1 = 0.5 + half;
        z0 = 0.5 - half;
        z1 = 0.5 + half;
        if (facing == 0) {
            z0 = 0.9375 - width;
            z1 = 0.9375;
        } else if (facing == 1) {
            x0 = 0.0625;
            x1 = 0.0625 + width;
        } else if (facing == 2) {
            z0 = 0.0625;
            z1 = 0.0625 + width;
        } else {
            x0 = 0.9375 - width;
            x1 = 0.9375;
        }
        shapes[0] = mc_aabb_make(
            (double)x + x0,
            (double)y + 0.4375 - (double)age * 0.125,
            (double)z + z0,
            (double)x + x1, (double)y + 0.75,
            (double)z + z1);
        return 1;
    }
    if (id == 199) {
        /* BlockChorusPlant is a centered 5/8 cube plus one 3/16 arm for
         * every actual-state connection. DOWN also connects to end stone;
         * every other direction accepts only chorus plants or flowers. */
        int count = 0;
        int north = gm_world_block(r->world, x, y, z - 1);
        int east = gm_world_block(r->world, x + 1, y, z);
        int south = gm_world_block(r->world, x, y, z + 1);
        int west = gm_world_block(r->world, x - 1, y, z);
        int up = gm_world_block(r->world, x, y + 1, z);
        int down = gm_world_block(r->world, x, y - 1, z);
        shapes[count++] = mc_aabb_make(
            (double)x + 0.1875, (double)y + 0.1875,
            (double)z + 0.1875,
            (double)x + 0.8125, (double)y + 0.8125,
            (double)z + 0.8125);
        if (west == 199 || west == 200)
            shapes[count++] = mc_aabb_make(
                (double)x, (double)y + 0.1875, (double)z + 0.1875,
                (double)x + 0.1875, (double)y + 0.8125,
                (double)z + 0.8125);
        if (east == 199 || east == 200)
            shapes[count++] = mc_aabb_make(
                (double)x + 0.8125, (double)y + 0.1875,
                (double)z + 0.1875,
                (double)x + 1.0, (double)y + 0.8125,
                (double)z + 0.8125);
        if (up == 199 || up == 200)
            shapes[count++] = mc_aabb_make(
                (double)x + 0.1875, (double)y + 0.8125,
                (double)z + 0.1875,
                (double)x + 0.8125, (double)y + 1.0,
                (double)z + 0.8125);
        if (down == 199 || down == 200 || down == 121)
            shapes[count++] = mc_aabb_make(
                (double)x + 0.1875, (double)y,
                (double)z + 0.1875,
                (double)x + 0.8125, (double)y + 0.1875,
                (double)z + 0.8125);
        if (north == 199 || north == 200)
            shapes[count++] = mc_aabb_make(
                (double)x + 0.1875, (double)y + 0.1875, (double)z,
                (double)x + 0.8125, (double)y + 0.8125,
                (double)z + 0.1875);
        if (south == 199 || south == 200)
            shapes[count++] = mc_aabb_make(
                (double)x + 0.1875, (double)y + 0.1875,
                (double)z + 0.8125,
                (double)x + 0.8125, (double)y + 0.8125,
                (double)z + 1.0);
        return count;
    }
    if (runtime_is_door_block(id)) {
        /* BlockDoor.getActualState combines facing/open from the lower half
         * with hinge from the upper half. A missing mate leaves that half's
         * default properties in place, exactly as the Java blockstate does. */
        static const unsigned char closed_panel[4] = {0, 2, 1, 3};
        static const unsigned char open_panel[4][2] = {
            {2, 3}, {1, 0}, {3, 2}, {0, 1}
        };
        int upper = (meta & 8) != 0;
        int lower_meta = upper ? 3 : meta;
        int hinge_right = upper ? (meta & 1) != 0 : 0;
        int pair_id = gm_world_block(
            r->world, x, y + (upper ? -1 : 1), z);
        int pair_meta = gm_world_meta(
            r->world, x, y + (upper ? -1 : 1), z);
        int panel;
        if (pair_id == id) {
            if (upper && (pair_meta & 8) == 0)
                lower_meta = pair_meta;
            else if (!upper && (pair_meta & 8) != 0)
                hinge_right = (pair_meta & 1) != 0;
        }
        lower_meta &= 7;
        panel = (lower_meta & 4)
            ? open_panel[lower_meta & 3][hinge_right]
            : closed_panel[lower_meta & 3];
        if (panel == 0)
            shapes[0] = mc_aabb_make(
                (double)x, (double)y, (double)z,
                (double)x + 0.1875, (double)y + 1.0,
                (double)z + 1.0);
        else if (panel == 1)
            shapes[0] = mc_aabb_make(
                (double)x + 0.8125, (double)y, (double)z,
                (double)x + 1.0, (double)y + 1.0,
                (double)z + 1.0);
        else if (panel == 2)
            shapes[0] = mc_aabb_make(
                (double)x, (double)y, (double)z,
                (double)x + 1.0, (double)y + 1.0,
                (double)z + 0.1875);
        else
            shapes[0] = mc_aabb_make(
                (double)x, (double)y, (double)z + 0.8125,
                (double)x + 1.0, (double)y + 1.0,
                (double)z + 1.0);
        return 1;
    }
    if (id == 54 || id == 146) {
        /* BlockChest selects the first same-registry neighbor in Java's
         * NORTH, SOUTH, WEST, EAST order. Ordinary and trapped chests do not
         * join one another even though they share the same geometry. */
        double x0 = 0.0625, x1 = 0.9375;
        double z0 = 0.0625, z1 = 0.9375;
        if (gm_world_block(r->world, x, y, z - 1) == id)
            z0 = 0.0;
        else if (gm_world_block(r->world, x, y, z + 1) == id)
            z1 = 1.0;
        else if (gm_world_block(r->world, x - 1, y, z) == id)
            x0 = 0.0;
        else if (gm_world_block(r->world, x + 1, y, z) == id)
            x1 = 1.0;
        shapes[0] = mc_aabb_make(
            (double)x + x0, (double)y, (double)z + z0,
            (double)x + x1, (double)y + 0.875,
            (double)z + z1);
        return 1;
    }
    if (runtime_is_shulker_box(id)) {
        /* Represented shulker tiles are closed: progress=0 makes
         * TileEntityShulkerBox.getBoundingBox exactly FULL_BLOCK_AABB. */
        shapes[0] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 1.0, (double)z + 1.0);
        return 1;
    }
    if (id == 118 || id == 154) {
        /* Cauldrons and hoppers are hollow five-box shapes. Their four
         * 1/8-thick walls are identical; only the basin height differs. */
        double base_height = id == 118 ? 0.3125 : 0.625;
        shapes[0] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + base_height,
            (double)z + 1.0);
        shapes[1] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 0.125, (double)y + 1.0,
            (double)z + 1.0);
        shapes[2] = mc_aabb_make(
            (double)x + 0.875, (double)y, (double)z,
            (double)x + 1.0, (double)y + 1.0,
            (double)z + 1.0);
        shapes[3] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 1.0,
            (double)z + 0.125);
        shapes[4] = mc_aabb_make(
            (double)x, (double)y, (double)z + 0.875,
            (double)x + 1.0, (double)y + 1.0,
            (double)z + 1.0);
        return 5;
    }
    if (runtime_is_pane_id(id))
        return runtime_pane_collision_shapes(r, x, y, z, shapes);
    if (id == 96 || id == 167)
        return runtime_trapdoor_collision_shapes(x, y, z, meta, shapes);
    if (id == 88) {
        /* BlockSoulSand.SOUL_SAND_AABB: full footprint, 7/8 high. */
        shapes[0] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 0.875, (double)z + 1.0);
        return 1;
    }
    if (id == 44 || id == 126 || id == 182 || id == 205) {
        /* All four single-slab registry classes share metadata HALF bit 8. */
        double min_y = (double)y + ((meta & 8) ? 0.5 : 0.0);
        double max_y = (double)y + ((meta & 8) ? 1.0 : 0.5);
        shapes[0] = mc_aabb_make(
            (double)x, min_y, (double)z,
            (double)x + 1.0, max_y, (double)z + 1.0);
        return 1;
    }
    if (runtime_is_stair_id(id))
        return runtime_stair_collision_shapes(r, x, y, z, meta, shapes);
    if (runtime_is_fence_id(id))
        return runtime_fence_collision_shapes(r, x, y, z, id, shapes);
    if (id == 139)
        return runtime_wall_collision_shapes(r, x, y, z, shapes);
    if (runtime_is_fence_gate_id(id))
        return runtime_fence_gate_collision_shapes(x, y, z, meta, shapes);
    if (gm_block_is_full_cube_1_11_2(id, meta)) {
        shapes[0] = mc_aabb_make(
            (double)x, (double)y, (double)z,
            (double)x + 1.0, (double)y + 1.0, (double)z + 1.0);
        return 1;
    }
    return 0;
}

static double runtime_entity_static_axis_offset(
        const GmRuntime *r, const McAABB *entity,
        int axis, double offset,
        int x0, int y0, int z0, int x1, int y1, int z1) {
    for (int y = y0; y <= y1; ++y)
        for (int z = z0; z <= z1; ++z)
            for (int x = x0; x <= x1; ++x) {
                int id = gm_world_block(r->world, x, y, z);
                int meta = gm_world_meta(r->world, x, y, z);
                McAABB shapes[RUNTIME_STATIC_MAX_SHAPES];
                int shape_count = runtime_static_collision_shapes(
                    r, x, y, z, id, meta, shapes);
                for (int shape_i = 0; shape_i < shape_count; ++shape_i)
                    offset = runtime_redstone_piston_dynamic_axis_offset(
                        &shapes[shape_i], entity, axis, offset);
            }
    /* A settled piston head is non-normal and its plate reaches the outer
     * block boundary while its arm can extend 0.25 into the adjacent block.
     * Search one cell beyond the swept item bounds, then reuse the exact
     * BlockPistonExtension plate/arm shapes used by moving source heads. */
    for (int y = y0 - 1; y <= y1 + 1; ++y)
        for (int z = z0 - 1; z <= z1 + 1; ++z)
            for (int x = x0 - 1; x <= x1 + 1; ++x) {
                int id = gm_world_block(r->world, x, y, z);
                int meta = gm_world_meta(r->world, x, y, z);
                int facing = meta & 7;
                GmRuntimePiston head;
                McAABB shapes[2];
                int shape_count;
                if (id != 34 || facing > 5)
                    continue;
                memset(&head, 0, sizeof head);
                head.moved_block = 34;
                head.facing = facing;
                shape_count = runtime_redstone_piston_collision_shapes(
                    &head, shapes);
                for (int shape_i = 0; shape_i < shape_count; ++shape_i) {
                    McAABB obstacle = mc_aabb_offset(
                        &shapes[shape_i],
                        (double)x, (double)y, (double)z);
                    offset = runtime_redstone_piston_dynamic_axis_offset(
                        &obstacle, entity, axis, offset);
                }
            }
    return offset;
}

/* EntityItem performs one ordinary SELF move before TileEntityPiston ticks.
 * Resolve that move against represented full cubes and against each moving
 * tile at its current progress. live_sim's cheap ground path intentionally
 * excludes raw block 36 and has no ceiling/horizontal AABB pass; this bounded
 * active-item path supplies those Java Entity.move semantics before the
 * piston performs its next swept push. The no-item hot path does not enter
 * this function or scan the world. */
static void runtime_resolve_item_self_motion(
        GmRuntime *r, const double before[GM_LIVE_MAX][3],
        const unsigned char tracked[GM_LIVE_MAX]) {
    McAABB obstacles[GM_RUNTIME_PISTONS * 2];
    int obstacle_count = 0;
    for (int piston_i = 0; piston_i < r->piston_count; ++piston_i) {
        const GmRuntimePiston *moving = &r->pistons[piston_i];
        McAABB shapes[2];
        float extended_progress;
        int shape_count;
        if (!moving->active || moving->dimension != r->dimension
                || moving->facing < 0 || moving->facing > 5)
            continue;
        shape_count = runtime_redstone_piston_collision_shapes(
            moving, shapes);
        if (shape_count == 0)
            continue;
        /* BlockPistonMoving uses a shortened source-head arm through progress
         * 0.75 for ordinary entity collision; its swept-push path above uses
         * the full arm. */
        if (moving->source && moving->extending
                && moving->moved_block == 34
                && moving->progress <= 0.75f
                && shape_count == 2) {
            switch (moving->facing) {
            case 0: shapes[1].maxY = 1.0; break;
            case 1: shapes[1].minY = 0.0; break;
            case 2: shapes[1].maxZ = 1.0; break;
            case 3: shapes[1].minZ = 0.0; break;
            case 4: shapes[1].maxX = 1.0; break;
            case 5: shapes[1].minX = 0.0; break;
            default: break;
            }
        }
        extended_progress = moving->extending
            ? moving->progress - 1.0f
            : 1.0f - moving->progress;
        for (int shape_i = 0; shape_i < shape_count; ++shape_i)
            obstacles[obstacle_count++] = mc_aabb_offset(
                &shapes[shape_i],
                (double)moving->x
                    + (double)(extended_progress
                        * (float)(moving->facing == 5
                            ? 1 : moving->facing == 4 ? -1 : 0)),
                (double)moving->y
                    + (double)(extended_progress
                        * (float)(moving->facing == 1
                            ? 1 : moving->facing == 0 ? -1 : 0)),
                (double)moving->z
                    + (double)(extended_progress
                        * (float)(moving->facing == 3
                            ? 1 : moving->facing == 2 ? -1 : 0)));
    }
    for (int item_i = 0; item_i < GM_LIVE_MAX; ++item_i) {
        GmLiveEnt *item = &r->entities.ents[item_i];
        McAABB box;
        double requested[3];
        double actual[3];
        double after_min[3], after_max[3];
        int x0, y0, z0, x1, y1, z1;
        if (!tracked[item_i] || !item->active || item->type != 0)
            continue;
        requested[0] = item->x - before[item_i][0];
        requested[1] = item->y - before[item_i][1];
        requested[2] = item->z - before[item_i][2];
        actual[0] = requested[0];
        actual[1] = requested[1];
        actual[2] = requested[2];
        after_min[0] = fmin(
            before[item_i][0] - 0.125,
            item->x - 0.125);
        after_min[1] = fmin(before[item_i][1], item->y);
        after_min[2] = fmin(
            before[item_i][2] - 0.125,
            item->z - 0.125);
        after_max[0] = fmax(
            before[item_i][0] + 0.125,
            item->x + 0.125);
        after_max[1] = fmax(
            before[item_i][1] + 0.25,
            item->y + 0.25);
        after_max[2] = fmax(
            before[item_i][2] + 0.125,
            item->z + 0.125);
        x0 = (int)floor(after_min[0]);
        y0 = (int)floor(after_min[1]);
        z0 = (int)floor(after_min[2]);
        x1 = (int)floor(after_max[0] - 1.0e-9);
        y1 = (int)floor(after_max[1] - 1.0e-9);
        z1 = (int)floor(after_max[2] - 1.0e-9);
        box = mc_aabb_make(
            before[item_i][0] - 0.125,
            before[item_i][1],
            before[item_i][2] - 0.125,
            before[item_i][0] + 0.125,
            before[item_i][1] + 0.25,
            before[item_i][2] + 0.125);
        /* Entity.move resolves Y, then X, then Z against one collision list. */
        for (int obstacle_i = 0;
                obstacle_i < obstacle_count; ++obstacle_i)
            actual[1] = runtime_redstone_piston_dynamic_axis_offset(
                &obstacles[obstacle_i], &box, 1, actual[1]);
        actual[1] = runtime_entity_static_axis_offset(
            r, &box, 1, actual[1], x0, y0, z0, x1, y1, z1);
        box = mc_aabb_offset(&box, 0.0, actual[1], 0.0);
        for (int obstacle_i = 0;
                obstacle_i < obstacle_count; ++obstacle_i)
            actual[0] = runtime_redstone_piston_dynamic_axis_offset(
                &obstacles[obstacle_i], &box, 0, actual[0]);
        actual[0] = runtime_entity_static_axis_offset(
            r, &box, 0, actual[0], x0, y0, z0, x1, y1, z1);
        box = mc_aabb_offset(&box, actual[0], 0.0, 0.0);
        for (int obstacle_i = 0;
                obstacle_i < obstacle_count; ++obstacle_i)
            actual[2] = runtime_redstone_piston_dynamic_axis_offset(
                &obstacles[obstacle_i], &box, 2, actual[2]);
        actual[2] = runtime_entity_static_axis_offset(
            r, &box, 2, actual[2], x0, y0, z0, x1, y1, z1);
        item->x = before[item_i][0] + actual[0];
        item->y = before[item_i][1] + actual[1];
        item->z = before[item_i][2] + actual[2];
        if (actual[0] != requested[0])
            item->mx = 0.0;
        if (actual[1] != requested[1]) {
            item->my = 0.0;
            if (requested[1] < 0.0)
                item->on_ground = 1;
        }
        if (actual[2] != requested[2])
            item->mz = 0.0;
    }
}

static void runtime_tick_live_items(GmRuntime *r) {
    if (r->entities.n_active > 0) {
        double item_before[GM_LIVE_MAX][3];
        unsigned char item_tracked[GM_LIVE_MAX];
        for (int i = 0; i < GM_LIVE_MAX; ++i) {
            item_tracked[i] =
                r->entities.ents[i].active
                && r->entities.ents[i].type == 0;
            if (item_tracked[i]) {
                item_before[i][0] = r->entities.ents[i].x;
                item_before[i][1] = r->entities.ents[i].y;
                item_before[i][2] = r->entities.ents[i].z;
            }
        }
        gm_live_tick_player(
            &r->entities, r->world,
            (struct PsvPlayer *)&r->player, r->ox, r->oz);
        runtime_resolve_item_self_motion(
            r, item_before, item_tracked);
    } else {
        gm_live_tick_player(
            &r->entities, r->world,
            (struct PsvPlayer *)&r->player, r->ox, r->oz);
    }
    /* Avoid even the bounded 48-slot collision scan in the no-item base
     * workload. Active EntityItems execute doBlockCollisions after movement. */
    if (r->entities.n_active > 0)
        runtime_redstone_item_pressure_plate_collisions(r);
}

/* World.updateComparatorOutputLevel scans the four horizontal directions.
 * It notifies a direct comparator or one hidden behind exactly one normal
 * cube. Static override blocks call this when their represented state edits. */
static void runtime_redstone_update_comparator_output_level(
        GmRuntime *r, int x, int y, int z) {
    static const int dx[4] = {0, 1, 0, -1};
    static const int dz[4] = {-1, 0, 1, 0};
    for (int face = 0; face < 4; ++face) {
        int nx = x + dx[face], nz = z + dz[face];
        int id = gm_world_block(r->world, nx, y, nz);
        int meta = gm_world_meta(r->world, nx, y, nz);
        if (id == 149 || id == 150) {
            runtime_redstone_neighbor_changed(r, nx, y, nz);
        } else if (gm_block_is_normal_cube_1_11_2(id, meta)) {
            nx += dx[face];
            nz += dz[face];
            id = gm_world_block(r->world, nx, y, nz);
            if (id == 149 || id == 150)
                runtime_redstone_neighbor_changed(r, nx, y, nz);
        }
    }
}

/* Lit BlockRedstoneTorch.onBlockAdded/breakBlock walks EnumFacing.values()
 * (DOWN, UP, NORTH, SOUTH, WEST, EAST) and asks each adjacent position to
 * notify its own neighbors. This fixed 6x6 traversal is the second ring that
 * lets an indirect consumer observe torch power through a normal support. */
static void runtime_redstone_torch_notify_adjacent_neighbors(
        GmRuntime *r, int x, int y, int z) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    for (int face = 0; face < 6; ++face)
        runtime_redstone_notify_neighbors(
            r, x + dx[face], y + dy[face], z + dz[face]);
}

static int runtime_redstone_button_supported(
        const GmRuntime *r, int x, int y, int z, int block,
        int require_powered) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int meta = gm_world_meta(r->world, x, y, z);
    int face = runtime_redstone_control_facing(block, meta);
    int sx, sy, sz;
    if ((block != 77 && block != 143)
            || gm_world_block(r->world, x, y, z) != block
            || (require_powered && (meta & 8) == 0)
            || face < 0 || face >= 6)
        return 0;
    if (!runtime_redstone_control_support_valid(
            r, x, y, z, block, meta, &sx, &sy, &sz))
        return 0;
    /* Bound the promoted callback to inert/represented neighbors of both
     * notification centers. Unknown consumers remain outside the exact slice. */
    const int centers[2][3] = {{x, y, z}, {sx, sy, sz}};
    for (int center = 0; center < 2; ++center)
        for (int neighbor = 0; neighbor < 6; ++neighbor) {
            int nx = centers[center][0] + dx[neighbor];
            int ny = centers[center][1] + dy[neighbor];
            int nz = centers[center][2] + dz[neighbor];
            int id = gm_world_block(r->world, nx, ny, nz);
            if ((nx != sx || ny != sy || nz != sz)
                    && id != 0 && id != 77 && id != 123 && id != 124
                    && id != 143
                    && !gm_block_is_normal_cube_1_11_2(
                        id, gm_world_meta(r->world, nx, ny, nz)))
                return 0;
        }
    return 1;
}

static int runtime_aabb_intersects(
        const McAABB *a, const McAABB *b) {
    return a->maxX > b->minX && a->minX < b->maxX
        && a->maxY > b->minY && a->minY < b->maxY
        && a->maxZ > b->minZ && a->minZ < b->maxZ;
}

static McAABB runtime_redstone_button_aabb(
        int x, int y, int z, int meta) {
    int face = runtime_redstone_control_facing(143, meta);
    int powered = (meta & 8) != 0;
    double depth = powered ? 0.0625 : 0.125;
    McAABB box;
    switch (face) {
    case 0: /* DOWN */
        box = mc_aabb_make(
            .3125, 1.0 - depth, .375, .6875, 1.0, .625);
        break;
    case 1: /* UP */
        box = mc_aabb_make(
            .3125, 0.0, .375, .6875, depth, .625);
        break;
    case 2: /* NORTH */
        box = mc_aabb_make(
            .3125, .375, 1.0 - depth, .6875, .625, 1.0);
        break;
    case 3: /* SOUTH */
        box = mc_aabb_make(
            .3125, .375, 0.0, .6875, .625, depth);
        break;
    case 4: /* WEST */
        box = mc_aabb_make(
            1.0 - depth, .375, .3125, 1.0, .625, .6875);
        break;
    case 5: /* EAST */
        box = mc_aabb_make(
            0.0, .375, .3125, depth, .625, .6875);
        break;
    default:
        return mc_aabb_make(0, 0, 0, 0, 0, 0);
    }
    box.minX += x;
    box.maxX += x;
    box.minY += y;
    box.maxY += y;
    box.minZ += z;
    box.maxZ += z;
    return box;
}

static McAABB runtime_redstone_arrow_aabb(
        const GmRuntimeProjectile *projectile) {
    return mc_aabb_make(
        projectile->x - 0.25, projectile->y, projectile->z - 0.25,
        projectile->x + 0.25, projectile->y + 0.5,
        projectile->z + 0.25);
}

static int runtime_redstone_arrow_count_intersects_aabb(
        const GmRuntime *r, const McAABB *box) {
    int count = 0;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        const GmRuntimeProjectile *projectile = &r->projectiles[i];
        if (!projectile->active
                || (projectile->type != 1 && projectile->type != 2))
            continue;
        McAABB arrow = runtime_redstone_arrow_aabb(projectile);
        if (runtime_aabb_intersects(box, &arrow))
            ++count;
    }
    return count;
}

/* Vanilla EVERYTHING occupancy queries see every Entity subclass even when
 * its movement path never calls doBlockCollisions. Keep arrow-only activation
 * separate, but include every represented projectile in scheduled hold polls.
 * Entity#setPosition centers width on X/Z and anchors height at Y. */
static McAABB runtime_redstone_projectile_aabb(
        const GmRuntimeProjectile *projectile) {
    double width;
    double height;
    switch (projectile->type) {
    case 1: /* player arrow */
    case 2: /* skeleton arrow */
        width = (double)0.5f;
        height = (double)0.5f;
        break;
    case 3: /* EntitySmallFireball */
        width = (double)0.3125f;
        height = (double)0.3125f;
        break;
    case 4: /* EntityEnderEye */
        width = (double)0.25f;
        height = (double)0.25f;
        break;
    case 6: /* EntityPotion */
    case 7: /* EntityEgg */
    case 8: /* EntitySnowball */
    case 9: /* EntityExpBottle */
        width = (double)0.25f;
        height = (double)0.25f;
        break;
    case 5: /* EntityLargeFireball */
    default:
        width = (double)1.0f;
        height = (double)1.0f;
        break;
    }
    return mc_aabb_make(
        projectile->x - width * 0.5, projectile->y,
        projectile->z - width * 0.5,
        projectile->x + width * 0.5, projectile->y + height,
        projectile->z + width * 0.5);
}

static int runtime_redstone_projectile_count_intersects_aabb(
        const GmRuntime *r, const McAABB *box) {
    int count = 0;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        const GmRuntimeProjectile *projectile = &r->projectiles[i];
        if (!projectile->active)
            continue;
        McAABB projectile_box =
            runtime_redstone_projectile_aabb(projectile);
        if (runtime_aabb_intersects(box, &projectile_box))
            ++count;
    }
    return count;
}

static McAABB runtime_redstone_falling_block_aabb(
        const GmRuntimeFallingBlock *falling) {
    double width = (double)0.98f;
    double height = (double)0.98f;
    return mc_aabb_make(
        falling->x - width * 0.5, falling->y,
        falling->z - width * 0.5,
        falling->x + width * 0.5, falling->y + height,
        falling->z + width * 0.5);
}

static int runtime_redstone_falling_block_count_intersects_aabb(
        const GmRuntime *r, const McAABB *box) {
    int count = 0;
    if (r->falling_block_count == 0)
        return 0;
    for (int i = 0; i < GM_RUNTIME_FALLING_BLOCKS; ++i) {
        const GmRuntimeFallingBlock *falling = &r->falling_blocks[i];
        if (!falling->active)
            continue;
        McAABB falling_box = runtime_redstone_falling_block_aabb(falling);
        if (runtime_aabb_intersects(box, &falling_box))
            ++count;
    }
    return count;
}

static int runtime_redstone_button_arrow_present(
        const GmRuntime *r, int x, int y, int z, int meta) {
    McAABB button = runtime_redstone_button_aabb(x, y, z, meta);
    return runtime_redstone_arrow_count_intersects_aabb(r, &button) > 0;
}

static void runtime_redstone_button_notify(
        GmRuntime *r, int x, int y, int z, int block, int meta) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int face = runtime_redstone_control_facing(block, meta);
    runtime_redstone_notify_neighbors(r, x, y, z);
    runtime_redstone_notify_neighbors(
        r, x - dx[face], y - dy[face], z - dz[face]);
}

static void runtime_redstone_wood_button_update(
        GmRuntime *r, int x, int y, int z) {
    int meta = gm_world_meta(r->world, x, y, z);
    int powered = (meta & 8) != 0;
    int arrow = runtime_redstone_button_arrow_present(
        r, x, y, z, meta);
    if (arrow != powered) {
        int new_meta = arrow ? meta | 8 : meta & 7;
        gm_world_set_block_meta(r->world, x, y, z, 143, new_meta);
        runtime_redstone_update_observers_at(r, x, y, z);
        runtime_redstone_button_notify(r, x, y, z, 143, meta);
    }
    if (arrow)
        runtime_schedule_tick_insert(
            r, x, y, z, 143, r->clock.total_time + 30, 0,
            r->scheduled_tick_next_order);
}

/* EntityArrow.doBlockCollisions runs after setPosition. Its inherited trigger
 * predicate activates tripwire and EVERYTHING plates as well as the
 * arrow-specific wooden button, while stone MOBS plates exclude it. */
static void runtime_redstone_arrow_collisions(
        GmRuntime *r, const GmRuntimeProjectile *projectile) {
    McAABB arrow = runtime_redstone_arrow_aabb(projectile);
    int min_x = (int)floor(arrow.minX + 0.001);
    int min_y = (int)floor(arrow.minY + 0.001);
    int min_z = (int)floor(arrow.minZ + 0.001);
    int max_x = (int)floor(arrow.maxX - 0.001);
    int max_y = (int)floor(arrow.maxY - 0.001);
    int max_z = (int)floor(arrow.maxZ - 0.001);
    for (int x = min_x; x <= max_x; ++x)
        for (int y = min_y; y <= max_y; ++y)
            for (int z = min_z; z <= max_z; ++z) {
                int block = gm_world_block(r->world, x, y, z);
                int meta = gm_world_meta(r->world, x, y, z);
                if (block == 46 && projectile->fire_ticks > 0) {
                    if (runtime_tnt_prime(r, x, y, z)) {
                        gm_world_set_block_meta(r->world, x, y, z, 0, 0);
                        runtime_redstone_notify_neighbors(r, x, y, z);
                        runtime_redstone_update_observers_at(r, x, y, z);
                    }
                    continue;
                }
                if (block == 143) {
                    McAABB button;
                    if ((meta & 8) != 0
                            || !runtime_redstone_button_supported(
                                r, x, y, z, 143, 0))
                        continue;
                    button = runtime_redstone_button_aabb(x, y, z, meta);
                    if (runtime_aabb_intersects(&button, &arrow))
                        runtime_redstone_wood_button_update(r, x, y, z);
                    continue;
                }
                if (block == 132 && (meta & 1) == 0) {
                    runtime_redstone_tripwire_update_state(
                        r, x, y, z, 0);
                    continue;
                }
                if ((block != 72 && block != 147 && block != 148)
                        || meta != 0)
                    continue;
                int strength = runtime_redstone_pressure_plate_strength(
                    r, x, y, z, block);
                if (strength > 0)
                    runtime_redstone_pressure_plate_update(
                        r, x, y, z, block, meta, strength,
                        runtime_redstone_pressure_plate_tick_rate(block));
            }
}

/* EntityFallingBlock.move invokes Entity.doBlockCollisions before drag and
 * landing settlement. Falling blocks use the inherited default trigger rule:
 * tripwire and EVERYTHING plates include them, while stone MOBS plates do
 * not. */
static void runtime_redstone_falling_block_collisions(
        GmRuntime *r, const GmRuntimeFallingBlock *falling) {
    McAABB box = runtime_redstone_falling_block_aabb(falling);
    int min_x = (int)floor(box.minX + 0.001);
    int min_y = (int)floor(box.minY + 0.001);
    int min_z = (int)floor(box.minZ + 0.001);
    int max_x = (int)floor(box.maxX - 0.001);
    int max_y = (int)floor(box.maxY - 0.001);
    int max_z = (int)floor(box.maxZ - 0.001);
    for (int x = min_x; x <= max_x; ++x)
        for (int y = min_y; y <= max_y; ++y)
            for (int z = min_z; z <= max_z; ++z) {
                int block = gm_world_block(r->world, x, y, z);
                int meta = gm_world_meta(r->world, x, y, z);
                if (block == 132 && (meta & 1) == 0) {
                    runtime_redstone_tripwire_update_state(
                        r, x, y, z, 0);
                    continue;
                }
                if ((block != 72 && block != 147 && block != 148)
                        || meta != 0)
                    continue;
                int strength = runtime_redstone_pressure_plate_strength(
                    r, x, y, z, block);
                if (strength > 0)
                    runtime_redstone_pressure_plate_update(
                        r, x, y, z, block, meta, strength,
                        runtime_redstone_pressure_plate_tick_rate(block));
            }
}

/* Compute the exact pressure-plate metadata from represented entities in
 * BlockBasePressurePlate's 1/8-inset, 1/4-block-high trigger AABB. Binary
 * stone/wood plates use MOBS/EVERYTHING sensitivity. Weighted plates count
 * entities (never ItemStack.count) and apply ceil(count * 15 / maxWeight),
 * where maxWeight is 15 for gold and 150 for iron.
 *
 * Entity positions in GmMobLive/items/dragon are world coordinates; the
 * integrated-server player box is window-local, so shift it by the current
 * origin. The fixed-capacity stores keep this scan bounded. */
static int runtime_redstone_pressure_plate_strength(
        const GmRuntime *r, int x, int y, int z, int block) {
    McAABB trigger = mc_aabb_make(
        x + 0.125, y, z + 0.125,
        x + 0.875, y + 0.25, z + 0.875);
    McAABB player = r->server_player.ent.box;
    int count = 0;
    player.minX += r->ox;
    player.maxX += r->ox;
    player.minZ += r->oz;
    player.maxZ += r->oz;
    if (runtime_aabb_intersects(&trigger, &player))
        ++count;
    count += gm_mobs_living_count_intersects_aabb(
        &r->mobs, r->dimension, &trigger);
    /* Stone uses Sensitivity.MOBS. Wood and both weighted plates query all
     * Entity instances. */
    if (block != 70)
        count += gm_live_items_count_intersects_aabb(
            &r->entities, &trigger);
    if (block != 70)
        count += gm_mobs_boat_count_intersects_aabb(
            &r->mobs, r->dimension, &trigger);
    if (block != 70)
        count += gm_mobs_xp_count_intersects_aabb(
            &r->mobs, r->dimension, &trigger);
    if (block != 70)
        count += runtime_redstone_projectile_count_intersects_aabb(
            r, &trigger);
    if (block != 70)
        count += runtime_redstone_falling_block_count_intersects_aabb(
            r, &trigger);
    if (r->dimension == 1 && r->dragon.initialized) {
        const EdDragon *dragon = &r->dragon.state.arena.dragon;
        if (dragon->alive) {
            McAABB dragon_box = mc_aabb_make(
                dragon->x - 8.0, dragon->y, dragon->z - 8.0,
                dragon->x + 8.0, dragon->y + 8.0, dragon->z + 8.0);
            if (runtime_aabb_intersects(&trigger, &dragon_box))
                ++count;
        }
    }
    if (block == 147 || block == 148) {
        int max_weight = block == 147 ? 15 : 150;
        if (count > max_weight)
            count = max_weight;
        return count > 0
            ? (count * 15 + max_weight - 1) / max_weight
            : 0;
    }
    return count > 0 ? 1 : 0;
}

/* BlockTripWire.updateState queries the state-dependent selection box. The
 * represented player, living mobs, boats, XP orbs, projectiles, falling
 * blocks, EntityItems, and dragon all use vanilla's default trigger
 * predicate; bats are not represented by the live simulator. */
static int runtime_redstone_tripwire_occupied(
        const GmRuntime *r, int x, int y, int z, int meta) {
    double min_y = (meta & 4) != 0 ? y + 0.0625 : (double)y;
    double max_y = (meta & 4) != 0 ? y + 0.15625 : y + 0.5;
    McAABB trigger = mc_aabb_make(
        x, min_y, z, x + 1.0, max_y, z + 1.0);
    McAABB player = r->server_player.ent.box;
    player.minX += r->ox;
    player.maxX += r->ox;
    player.minZ += r->oz;
    player.maxZ += r->oz;
    if (runtime_aabb_intersects(&trigger, &player)
            || gm_mobs_living_count_intersects_aabb(
                &r->mobs, r->dimension, &trigger) > 0
            || gm_mobs_boat_count_intersects_aabb(
                &r->mobs, r->dimension, &trigger) > 0
            || gm_mobs_xp_count_intersects_aabb(
                &r->mobs, r->dimension, &trigger) > 0
            || runtime_redstone_projectile_count_intersects_aabb(
                r, &trigger) > 0
            || runtime_redstone_falling_block_count_intersects_aabb(
                r, &trigger) > 0
            || gm_live_items_count_intersects_aabb(
                &r->entities, &trigger) > 0)
        return 1;
    if (r->dimension == 1 && r->dragon.initialized) {
        const EdDragon *dragon = &r->dragon.state.arena.dragon;
        if (dragon->alive) {
            McAABB dragon_box = mc_aabb_make(
                dragon->x - 8.0, dragon->y, dragon->z - 8.0,
                dragon->x + 8.0, dragon->y + 8.0, dragon->z + 8.0);
            if (runtime_aabb_intersects(&trigger, &dragon_box))
                return 1;
        }
    }
    return 0;
}

static void runtime_redstone_tripwire_update_state(
        GmRuntime *r, int x, int y, int z, int schedule_offset) {
    int meta = gm_world_meta(r->world, x, y, z);
    int was_powered = (meta & 1) != 0;
    int occupied = runtime_redstone_tripwire_occupied(
        r, x, y, z, meta);
    if (occupied != was_powered) {
        int new_meta = occupied ? (meta | 1) : (meta & ~1);
        gm_world_set_block_meta(r->world, x, y, z, 132, new_meta);
        runtime_redstone_update_observers_at(r, x, y, z);
        runtime_redstone_notify_neighbors(r, x, y, z);
        runtime_redstone_tripwire_notify_hook(
            r, x, y, z, new_meta, schedule_offset);
        meta = new_meta;
    }
    if (occupied)
        runtime_schedule_tick_insert(
            r, x, y, z, 132,
            r->clock.total_time + 10 + schedule_offset, 0,
            r->scheduled_tick_next_order);
}

/* BlockBasePressurePlate.updateState: metadata changes notify both the plate
 * and support; every positive result schedules the next occupancy poll. */
static void runtime_redstone_pressure_plate_update(
        GmRuntime *r, int x, int y, int z, int block,
        int old_strength, int new_strength, int schedule_delay) {
    if (old_strength != new_strength) {
        gm_world_set_block_meta(
            r->world, x, y, z, block, new_strength);
        runtime_redstone_update_observers_at(r, x, y, z);
        runtime_redstone_notify_neighbors(r, x, y, z);
        runtime_redstone_notify_neighbors(r, x, y - 1, z);
    }
    if (new_strength > 0)
        runtime_schedule_tick_insert(
            r, x, y, z, block,
            r->clock.total_time + schedule_delay, 0,
            r->scheduled_tick_next_order);
}

/* Server-side Entity.doBlockCollisions invokes
 * BlockBasePressurePlate.onEntityCollidedWithBlock after movement. Only an
 * unpowered plate recomputes its strength; the scheduled callback owns
 * subsequent occupied/release checks. The contracted cell traversal is the
 * same one used by psv_do_block_collisions, shifted from the physics window
 * to world X/Z. clock_offset is 0 for packet movement and 1 for the ordinary
 * entity pass because magma advances its mirrored total time later. */
static void runtime_redstone_player_pressure_plate_collisions(
        GmRuntime *r, int clock_offset) {
    const McAABB *box = &r->server_player.ent.box;
    int x0 = mc_floor(box->minX + 0.001) + r->ox;
    int x1 = mc_floor(box->maxX - 0.001) + r->ox;
    int y0 = mc_floor(box->minY + 0.001);
    int y1 = mc_floor(box->maxY - 0.001);
    int z0 = mc_floor(box->minZ + 0.001) + r->oz;
    int z1 = mc_floor(box->maxZ - 0.001) + r->oz;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int block = gm_world_block(r->world, x, y, z);
                int old_strength =
                    gm_world_meta(r->world, x, y, z);
                if (block == 132 && (old_strength & 1) == 0) {
                    runtime_redstone_tripwire_update_state(
                        r, x, y, z, clock_offset);
                    continue;
                }
                if (!runtime_redstone_is_pressure_plate(block)
                        || old_strength != 0)
                    continue;
                int new_strength =
                    runtime_redstone_pressure_plate_strength(
                        r, x, y, z, block);
                if (new_strength > 0)
                    runtime_redstone_pressure_plate_update(
                        r, x, y, z, block, old_strength, new_strength,
                        runtime_redstone_pressure_plate_tick_rate(block)
                            + clock_offset);
            }
}

/* EntityLivingBase.move and EntityBoat.onUpdate invoke the same contracted
 * doBlockCollisions cell walk as the server player. This runs after
 * gm_world_tick and the mob pass, matching Java's incremented totalWorldTime,
 * so tickRate() needs no additional clock offset here. The fixed-capacity
 * scan remains bounded by the existing represented-entity product cap. */
static void runtime_redstone_mob_pressure_plate_collisions(
        GmRuntime *r, int controlled_only) {
    McAABB boxes[GM_MOB_CAPACITY];
    int count = gm_mobs_trigger_collision_boxes(
        &r->mobs, r->dimension, controlled_only,
        boxes, GM_MOB_CAPACITY);
    for (int i = 0; i < count; ++i) {
        const McAABB *box = &boxes[i];
        int x0 = mc_floor(box->minX + 0.001);
        int x1 = mc_floor(box->maxX - 0.001);
        int y0 = mc_floor(box->minY + 0.001);
        int y1 = mc_floor(box->maxY - 0.001);
        int z0 = mc_floor(box->minZ + 0.001);
        int z1 = mc_floor(box->maxZ - 0.001);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) {
                    int block = gm_world_block(r->world, x, y, z);
                    int old_strength =
                        gm_world_meta(r->world, x, y, z);
                    if (block == 132 && (old_strength & 1) == 0) {
                        runtime_redstone_tripwire_update_state(
                            r, x, y, z, 0);
                        continue;
                    }
                    if (!runtime_redstone_is_pressure_plate(block)
                            || old_strength != 0)
                        continue;
                    int new_strength =
                        runtime_redstone_pressure_plate_strength(
                            r, x, y, z, block);
                    if (new_strength > 0)
                        runtime_redstone_pressure_plate_update(
                            r, x, y, z, block,
                            old_strength, new_strength,
                            runtime_redstone_pressure_plate_tick_rate(block));
                }
    }
}

/* EntityItem.onUpdate runs move(0,0,0) even for the locked stationary
 * fixture, then Entity.doBlockCollisions. Wood and weighted plates admit
 * items; stone deliberately does not. */
static void runtime_redstone_item_pressure_plate_collisions(GmRuntime *r) {
    McAABB boxes[GM_LIVE_MAX];
    int count = gm_live_item_boxes(
        &r->entities, boxes, GM_LIVE_MAX);
    for (int i = 0; i < count; ++i) {
        const McAABB *box = &boxes[i];
        int x0 = mc_floor(box->minX + 0.001);
        int x1 = mc_floor(box->maxX - 0.001);
        int y0 = mc_floor(box->minY + 0.001);
        int y1 = mc_floor(box->maxY - 0.001);
        int z0 = mc_floor(box->minZ + 0.001);
        int z1 = mc_floor(box->maxZ - 0.001);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) {
                    int block = gm_world_block(r->world, x, y, z);
                    int old_strength =
                        gm_world_meta(r->world, x, y, z);
                    if (block == 132 && (old_strength & 1) == 0) {
                        runtime_redstone_tripwire_update_state(
                            r, x, y, z, 0);
                        continue;
                    }
                    if ((block != 72 && block != 147 && block != 148)
                            || old_strength != 0)
                        continue;
                    int new_strength =
                        runtime_redstone_pressure_plate_strength(
                            r, x, y, z, block);
                    if (new_strength > 0)
                        runtime_redstone_pressure_plate_update(
                            r, x, y, z, block,
                            old_strength, new_strength,
                            runtime_redstone_pressure_plate_tick_rate(block));
                }
    }
}

/* EntityXPOrb.onUpdate reaches Entity.move before drag, aging, and pickup.
 * Preserve those post-move boxes so its inherited doBlockCollisions callback
 * can activate tripwire and EVERYTHING plates without an idle orb-store scan. */
static void runtime_redstone_xp_pressure_plate_collisions(GmRuntime *r) {
    McAABB boxes[GM_XP_ORBS];
    int count = gm_mobs_xp_collision_boxes(
        &r->mobs, boxes, GM_XP_ORBS);
    for (int i = 0; i < count; ++i) {
        const McAABB *box = &boxes[i];
        int x0 = mc_floor(box->minX + 0.001);
        int x1 = mc_floor(box->maxX - 0.001);
        int y0 = mc_floor(box->minY + 0.001);
        int y1 = mc_floor(box->maxY - 0.001);
        int z0 = mc_floor(box->minZ + 0.001);
        int z1 = mc_floor(box->maxZ - 0.001);
        for (int x = x0; x <= x1; ++x)
            for (int y = y0; y <= y1; ++y)
                for (int z = z0; z <= z1; ++z) {
                    int block = gm_world_block(r->world, x, y, z);
                    int old_strength =
                        gm_world_meta(r->world, x, y, z);
                    if (block == 132 && (old_strength & 1) == 0) {
                        runtime_redstone_tripwire_update_state(
                            r, x, y, z, 0);
                        continue;
                    }
                    if ((block != 72 && block != 147 && block != 148)
                            || old_strength != 0)
                        continue;
                    int new_strength =
                        runtime_redstone_pressure_plate_strength(
                            r, x, y, z, block);
                    if (new_strength > 0)
                        runtime_redstone_pressure_plate_update(
                            r, x, y, z, block,
                            old_strength, new_strength,
                            runtime_redstone_pressure_plate_tick_rate(block));
                }
    }
}

static int runtime_redstone_pressure_plate_callback_supported(
        const GmRuntime *r, int x, int y, int z, int block) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int meta = gm_world_meta(r->world, x, y, z);
    if (!runtime_redstone_is_pressure_plate(block)
            || gm_world_block(r->world, x, y, z) != block
            || meta <= 0 || meta > 15
            || ((block == 70 || block == 72) && meta != 1)
            || !runtime_redstone_pressure_plate_support_valid(
                r, x, y, z))
        return 0;
    /* A strength transition notifies neighbors of the plate and its support.
     * Admit only inert/represented consumers around those two centers. */
    const int centers[2][3] = {{x, y, z}, {x, y - 1, z}};
    for (int center = 0; center < 2; ++center)
        for (int neighbor = 0; neighbor < 6; ++neighbor) {
            int nx = centers[center][0] + dx[neighbor];
            int ny = centers[center][1] + dy[neighbor];
            int nz = centers[center][2] + dz[neighbor];
            int id = gm_world_block(r->world, nx, ny, nz);
            if ((nx != x || ny != y - 1 || nz != z)
                    && id != 0 && id != 55
                    && id != 70 && id != 72
                    && id != 147 && id != 148
                    && id != 123 && id != 124
                    && !gm_block_is_normal_cube_1_11_2(
                        id, gm_world_meta(r->world, nx, ny, nz)))
                return 0;
        }
    return 1;
}

static int runtime_redstone_torch_support_offset(
        int meta, int *dx, int *dy, int *dz) {
    *dx = *dy = *dz = 0;
    switch (meta) {
    case 1: *dx = -1; break; /* facing EAST, wall support WEST */
    case 2: *dx = 1; break;  /* facing WEST, wall support EAST */
    case 3: *dz = -1; break; /* facing SOUTH, wall support NORTH */
    case 4: *dz = 1; break;  /* facing NORTH, wall support SOUTH */
    case 5: *dy = -1; break; /* facing UP, floor support DOWN */
    default: return 0;
    }
    return 1;
}

static int runtime_redstone_torch_callback_supported(
        const GmRuntime *r, int x, int y, int z, int block) {
    static const int face_dx[6] = {0, 0, 0, 0, -1, 1};
    static const int face_dy[6] = {-1, 1, 0, 0, 0, 0};
    static const int face_dz[6] = {0, 0, -1, 1, 0, 0};
    int support_dx, support_dy, support_dz;
    int support_x, support_y, support_z;
    int support_id, support_meta;
    int meta;
    if ((block != 75 && block != 76)
            || gm_world_block(r->world, x, y, z) != block)
        return 0;
    meta = gm_world_meta(r->world, x, y, z);
    if (!runtime_redstone_torch_support_offset(
            meta, &support_dx, &support_dy, &support_dz)
            || !runtime_redstone_torch_support_valid(r, x, y, z, meta))
        return 0;
    support_x = x + support_dx;
    support_y = y + support_dy;
    support_z = z + support_dz;
    support_id = gm_world_block(
        r->world, support_x, support_y, support_z);
    support_meta = gm_world_meta(
        r->world, support_x, support_y, support_z);
    if (gm_block_is_normal_cube_1_11_2(support_id, support_meta))
        for (int face = 0; face < 6; ++face)
            if (!runtime_redstone_power_provider_supported(
                    r, support_x + face_dx[face],
                    support_y + face_dy[face],
                    support_z + face_dz[face]))
                return 0;
    for (int face = 0; face < 6; ++face) {
        int center_x = x + face_dx[face];
        int center_y = y + face_dy[face];
        int center_z = z + face_dz[face];
        for (int neighbor = 0; neighbor < 6; ++neighbor) {
            int nx = center_x + face_dx[neighbor];
            int ny = center_y + face_dy[neighbor];
            int nz = center_z + face_dz[neighbor];
            int id = gm_world_block(r->world, nx, ny, nz);
            int state_meta = gm_world_meta(r->world, nx, ny, nz);
            if (nx == support_x && ny == support_y && nz == support_z)
                continue;
            if (id != 0 && id != 55 && id != 75 && id != 76
                    && id != 93 && id != 94
                    && id != 123 && id != 124
                    && id != 149 && id != 150 && id != 218
                    && !gm_block_is_normal_cube_1_11_2(id, state_meta))
                return 0;
            if ((id == 93 || id == 94)
                    && !runtime_redstone_repeater_supported(
                        r, nx, ny, nz, id))
                return 0;
            if ((id == 149 || id == 150)
                    && !runtime_redstone_comparator_supported(
                        r, nx, ny, nz, id))
                return 0;
            if (id == 218
                    && !runtime_redstone_observer_supported(r, nx, ny, nz))
                return 0;
        }
    }
    return 1;
}

static int runtime_java_random_next(GmRuntime *r, int bits);
static void runtime_minecart_detector_update(
    GmRuntime *r, int x, int y, int z, int scheduled_callback);

static void runtime_redstone_torch_prune_toggles(GmRuntime *r) {
    int expired = 0;
    while (expired < r->redstone_torch_toggle_count
            && r->clock.total_time
                - r->redstone_torch_toggles[expired].time > 60)
        ++expired;
    if (expired == 0)
        return;
    r->redstone_torch_toggle_count -= expired;
    if (r->redstone_torch_toggle_count > 0) {
        memmove(
            &r->redstone_torch_toggles[0],
            &r->redstone_torch_toggles[expired],
            (size_t)r->redstone_torch_toggle_count
                * sizeof r->redstone_torch_toggles[0]);
    }
}

static int runtime_redstone_torch_append_toggle(
        GmRuntime *r, int x, int y, int z, long long time) {
    if (r->redstone_torch_toggle_count
            == r->redstone_torch_toggle_cap) {
        int next_cap = r->redstone_torch_toggle_cap
            ? r->redstone_torch_toggle_cap * 2 : 16;
        GmRuntimeRedstoneTorchToggle *grown =
            (GmRuntimeRedstoneTorchToggle *)realloc(
                r->redstone_torch_toggles,
                (size_t)next_cap * sizeof *grown);
        if (!grown)
            return 0;
        r->redstone_torch_toggles = grown;
        r->redstone_torch_toggle_cap = next_cap;
    }
    r->redstone_torch_toggles[r->redstone_torch_toggle_count++] =
        (GmRuntimeRedstoneTorchToggle){x, y, z, time};
    return 1;
}

static int runtime_redstone_torch_is_burned_out(
        GmRuntime *r, int x, int y, int z, int turn_off) {
    if (turn_off && !runtime_redstone_torch_append_toggle(
            r, x, y, z, r->clock.total_time))
        return 0;
    int count = 0;
    for (int i = 0; i < r->redstone_torch_toggle_count; ++i) {
        const GmRuntimeRedstoneTorchToggle *toggle =
            &r->redstone_torch_toggles[i];
        if (toggle->x == x && toggle->y == y && toggle->z == z
                && ++count >= 8)
            return 1;
    }
    return 0;
}

static void runtime_redstone_torch_consume_burnout_rng(GmRuntime *r) {
    /* Sound pitch: two nextFloat calls. Five smoke positions: three
     * nextDouble calls each, and Random.nextDouble consumes next(26)+next(27). */
    (void)runtime_java_random_next(r, 24);
    (void)runtime_java_random_next(r, 24);
    for (int i = 0; i < 15; ++i) {
        (void)runtime_java_random_next(r, 26);
        (void)runtime_java_random_next(r, 27);
    }
}

static void runtime_tick_scheduled(GmRuntime *r) {
    while (r->scheduled_tick_count > 0
            && r->scheduled_ticks[0].time <= r->clock.total_time) {
        GmRuntimeScheduledTick entry = r->scheduled_ticks[0];
        --r->scheduled_tick_count;
        if (r->scheduled_tick_count > 0) {
            memmove(
                &r->scheduled_ticks[0], &r->scheduled_ticks[1],
                (size_t)r->scheduled_tick_count
                    * sizeof r->scheduled_ticks[0]);
        }
        /* WorldServer drops stale entries whose block no longer matches. */
        if (gm_world_block(r->world, entry.x, entry.y, entry.z)
                != entry.block)
            continue;
        int support_dx = 0, support_dy = 0, support_dz = 0;
        if (entry.block == 8
                && runtime_water_supported(
                    r, entry.x, entry.y, entry.z))
            runtime_tick_water_flat(r, &entry);
        else if (entry.block == 10
                && runtime_lava_above_enclosed_water_supported(
                    r, entry.x, entry.y, entry.z))
            runtime_tick_lava_down_into_water(r, &entry);
        else if (entry.block == 10
                && runtime_lava_flat_material_supported(
                    r, entry.x, entry.y, entry.z))
            runtime_tick_lava_flat(r, &entry);
        else if (entry.block == 12 || entry.block == 13
                || entry.block == 122 || entry.block == 145) {
            if (r->falling_instant)
                (void)runtime_fall_block_instantly(r, &entry);
            else
                (void)runtime_spawn_falling_block(r, &entry);
        }
        else if (entry.block == 51) {
            /* BlockFire.updateTick's gamerule guard is outside the entire
             * callback body. The WorldServer queue entry has already drained,
             * but disabled fire consumes no RNG and schedules no replacement. */
            if (r->do_fire_tick
                    && runtime_fire_proof_supported(
                        r, entry.x, entry.y, entry.z))
                runtime_tick_fire(
                    r, entry.x, entry.y, entry.z);
        }
        else if (entry.block == 158
                && runtime_dropper_supported(
                    r, entry.x, entry.y, entry.z))
            runtime_tick_dropper(r, entry.x, entry.y, entry.z);
        else if (entry.block == 23
                && runtime_dispenser_supported(
                    r, entry.x, entry.y, entry.z))
            runtime_tick_dispenser(r, entry.x, entry.y, entry.z);
        else if (entry.block == 28)
            runtime_minecart_detector_update(
                r, entry.x, entry.y, entry.z, 1);
        else if (entry.block == 131) {
            int meta = gm_world_meta(
                r->world, entry.x, entry.y, entry.z);
            runtime_redstone_tripwire_hook_calculate(
                r, entry.x, entry.y, entry.z, meta,
                0, 1, -1, 0, 0);
        }
        else if (entry.block == 132) {
            int meta = gm_world_meta(
                r->world, entry.x, entry.y, entry.z);
            if ((meta & 1) != 0)
                runtime_redstone_tripwire_update_state(
                    r, entry.x, entry.y, entry.z, 0);
        }
        else if ((entry.block == 93 || entry.block == 94)
                && runtime_redstone_repeater_supported(
                    r, entry.x, entry.y, entry.z, entry.block)) {
            int meta = gm_world_meta(
                r->world, entry.x, entry.y, entry.z);
            int input_powered = runtime_redstone_repeater_input_powered(
                r, entry.x, entry.y, entry.z, meta);
            if (!runtime_redstone_repeater_locked(
                    r, entry.x, entry.y, entry.z, meta)) {
                if (entry.block == 94 && !input_powered) {
                    gm_world_set_block_meta(
                        r->world, entry.x, entry.y, entry.z, 93, meta);
                    runtime_redstone_update_observers_at(
                        r, entry.x, entry.y, entry.z);
                    runtime_redstone_repeater_notify_output(
                        r, entry.x, entry.y, entry.z, meta);
                } else if (entry.block == 93) {
                    gm_world_set_block_meta(
                        r->world, entry.x, entry.y, entry.z, 94, meta);
                    runtime_redstone_update_observers_at(
                        r, entry.x, entry.y, entry.z);
                    runtime_redstone_repeater_notify_output(
                        r, entry.x, entry.y, entry.z, meta);
                    if (!input_powered)
                        runtime_schedule_tick_insert(
                            r, entry.x, entry.y, entry.z, 94,
                            r->clock.total_time
                                + ((meta >> 2) + 1) * 2,
                            -1, r->scheduled_tick_next_order);
                }
            }
        }
        else if ((entry.block == 149 || entry.block == 150)
                && runtime_redstone_comparator_supported(
                    r, entry.x, entry.y, entry.z, entry.block))
            runtime_redstone_comparator_dispatch(
                r, entry.x, entry.y, entry.z, entry.block);
        else if (entry.block == 218
                && runtime_redstone_observer_supported(
                    r, entry.x, entry.y, entry.z)) {
            int meta = gm_world_meta(
                r->world, entry.x, entry.y, entry.z);
            int powered = (meta & 8) != 0;
            gm_world_set_block_meta(
                r->world, entry.x, entry.y, entry.z, 218,
                powered ? (meta & 7) : (meta | 8));
            /* setBlockState(..., 2) informs adjacent observers before
             * updateTick schedules its release and notifies the output. */
            runtime_redstone_update_observers_at(
                r, entry.x, entry.y, entry.z);
            if (!powered)
                runtime_schedule_tick_insert(
                    r, entry.x, entry.y, entry.z, 218,
                    r->clock.total_time + 2, 0,
                    r->scheduled_tick_next_order);
            runtime_redstone_observer_notify_output(
                r, entry.x, entry.y, entry.z, meta);
        }
        else if (entry.block == 77
                && runtime_redstone_button_supported(
                    r, entry.x, entry.y, entry.z, 77, 1)) {
            int old_meta = gm_world_meta(
                r->world, entry.x, entry.y, entry.z);
            gm_world_set_block_meta(
                r->world, entry.x, entry.y, entry.z, 77, old_meta & 7);
            runtime_redstone_update_observers_at(
                r, entry.x, entry.y, entry.z);
            runtime_redstone_button_notify(
                r, entry.x, entry.y, entry.z, 77, old_meta);
        }
        else if (entry.block == 143
                && runtime_redstone_button_supported(
                    r, entry.x, entry.y, entry.z, 143, 1)) {
            runtime_redstone_wood_button_update(
                r, entry.x, entry.y, entry.z);
        }
        else if (runtime_redstone_is_pressure_plate(entry.block)
                && runtime_redstone_pressure_plate_callback_supported(
                    r, entry.x, entry.y, entry.z, entry.block)) {
            int old_strength = gm_world_meta(
                r->world, entry.x, entry.y, entry.z);
            int new_strength =
                runtime_redstone_pressure_plate_strength(
                    r, entry.x, entry.y, entry.z, entry.block);
            runtime_redstone_pressure_plate_update(
                r, entry.x, entry.y, entry.z, entry.block,
                old_strength, new_strength,
                runtime_redstone_pressure_plate_tick_rate(entry.block));
        }
        else if ((entry.block == 75 || entry.block == 76)
                && runtime_redstone_torch_support_offset(
                    gm_world_meta(
                        r->world, entry.x, entry.y, entry.z),
                    &support_dx, &support_dy, &support_dz)) {
            int meta = gm_world_meta(
                r->world, entry.x, entry.y, entry.z);
            int should_be_off = runtime_redstone_torch_should_be_off(
                r, entry.x, entry.y, entry.z, meta);
            runtime_redstone_torch_prune_toggles(r);
            if (entry.block == 76 && should_be_off) {
                gm_world_set_block_meta(
                    r->world, entry.x, entry.y, entry.z, 75, meta);
                runtime_redstone_update_observers_at(
                    r, entry.x, entry.y, entry.z);
                runtime_redstone_notify_neighbors(
                    r, entry.x, entry.y, entry.z);
                runtime_redstone_torch_notify_adjacent_neighbors(
                    r, entry.x, entry.y, entry.z);
                if (runtime_redstone_torch_is_burned_out(
                        r, entry.x, entry.y, entry.z, 1)) {
                    runtime_redstone_torch_consume_burnout_rng(r);
                    runtime_schedule_tick_insert(
                        r, entry.x, entry.y, entry.z, 75,
                        r->clock.total_time + 160, 0,
                        r->scheduled_tick_next_order);
                }
            } else if (entry.block == 75 && !should_be_off) {
                if (!runtime_redstone_torch_is_burned_out(
                        r, entry.x, entry.y, entry.z, 0)) {
                    gm_world_set_block_meta(
                        r->world, entry.x, entry.y, entry.z, 76, meta);
                    runtime_redstone_update_observers_at(
                        r, entry.x, entry.y, entry.z);
                    runtime_redstone_notify_neighbors(
                        r, entry.x, entry.y, entry.z);
                    runtime_redstone_torch_notify_adjacent_neighbors(
                        r, entry.x, entry.y, entry.z);
                }
            }
        }
        else if (entry.block == 124
                && runtime_redstone_lamp_off_supported(
                    r, entry.x, entry.y, entry.z)
                && !runtime_redstone_is_powered(
                    r, entry.x, entry.y, entry.z)) {
            gm_world_set_block_meta(
                r->world, entry.x, entry.y, entry.z, 123, 0);
            runtime_redstone_update_observers_at(
                r, entry.x, entry.y, entry.z);
        }
        else if (entry.block == 200
                && gm_block_meta_canonical_1_11_2(
                    200, gm_world_meta(
                        r->world, entry.x, entry.y, entry.z))
                && !runtime_redstone_chorus_flower_can_survive(
                    r, entry.x, entry.y, entry.z)) {
            gm_world_set_block_meta(
                r->world, entry.x, entry.y, entry.z, 0, 0);
            runtime_redstone_notify_neighbors(
                r, entry.x, entry.y, entry.z);
            runtime_redstone_update_observers_at(
                r, entry.x, entry.y, entry.z);
        }
        else if (entry.block == 199
                && gm_world_meta(
                    r->world, entry.x, entry.y, entry.z) == 0
                && !runtime_redstone_chorus_plant_can_survive(
                    r, entry.x, entry.y, entry.z)) {
            uint64_t seed_before_drop = r->world_random_seed48;
            int quantity = runtime_java_random_next_int(r, 2);
            if (quantity == 0
                    || r->entities.n_active < GM_LIVE_MAX) {
                if (quantity == 0
                        || runtime_redstone_piston_spawn_item_stack(
                            r, entry.x, entry.y, entry.z, 432, 0)) {
                    gm_world_set_block_meta(
                        r->world, entry.x, entry.y, entry.z, 0, 0);
                    runtime_redstone_notify_neighbors(
                        r, entry.x, entry.y, entry.z);
                    runtime_redstone_update_observers_at(
                        r, entry.x, entry.y, entry.z);
                }
            } else {
                /* Preserve atomic fixed-pool rejection if Java selected its
                 * one-fruit branch and no EntityItem slot is available. */
                r->world_random_seed48 = seed_before_drop;
            }
        }
        /* Stone's Block.updateTick body is intentionally empty. */
    }
}

static int runtime_falling_spawn_item(
        GmRuntime *r, const GmRuntimeFallingBlock *falling) {
    /* BlockAnvil.damageDropped discards horizontal facing and retains only
     * the current damage tier. Other falling blocks use their state metadata
     * directly as the item damage. */
    int item_meta = falling->block == 145
        ? (falling->meta & 15) >> 2 : falling->meta;
    int free_slot = -1;
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
        if (!r->entities.ents[slot].active) {
            free_slot = slot;
            break;
        }
    if (free_slot < 0)
        return 0;
    uint64_t math_before = r->math_random_seed48;
    int eid_before = r->next_entity_id;
    double motion_x, motion_z;
    float yaw;
    (void)runtime_math_random_next_double(r);
    yaw = (float)(runtime_math_random_next_double(r) * 360.0);
    motion_x = (double)(float)(
        runtime_math_random_next_double(r)
            * 0.20000000298023224 - 0.10000000149011612);
    motion_z = (double)(float)(
        runtime_math_random_next_double(r)
            * 0.20000000298023224 - 0.10000000149011612);
    if (!gm_live_spawn_item_exact(
            &r->entities, r->next_entity_id++,
            falling->x, falling->y, falling->z,
            motion_x, 0.20000000298023224, motion_z, yaw,
            falling->block, 1, item_meta, 0, 10, 0)) {
        r->math_random_seed48 = math_before;
        r->next_entity_id = eid_before;
        return 0;
    }
    return 1;
}

static void runtime_falling_move_self(
        GmRuntime *r, GmRuntimeFallingBlock *falling) {
    double requested[3] = {falling->vx, falling->vy, falling->vz};
    double actual[3] = {falling->vx, falling->vy, falling->vz};
    McAABB box;
    if (!falling->bounding_box_valid)
        runtime_falling_set_position(
            falling, falling->x, falling->y, falling->z);
    box = mc_aabb_make(
        falling->bb_min_x, falling->bb_min_y, falling->bb_min_z,
        falling->bb_max_x, falling->bb_max_y, falling->bb_max_z);
    if (!falling->no_ground) {
        double after_min[3], after_max[3];
        int x0, y0, z0, x1, y1, z1;
        after_min[0] = fmin(box.minX, box.minX + requested[0]);
        after_min[1] = fmin(box.minY, box.minY + requested[1]);
        after_min[2] = fmin(box.minZ, box.minZ + requested[2]);
        after_max[0] = fmax(box.maxX, box.maxX + requested[0]);
        after_max[1] = fmax(box.maxY, box.maxY + requested[1]);
        after_max[2] = fmax(box.maxZ, box.maxZ + requested[2]);
        x0 = (int)floor(after_min[0]);
        y0 = (int)floor(after_min[1]);
        z0 = (int)floor(after_min[2]);
        x1 = (int)floor(after_max[0] - 1.0e-9);
        y1 = (int)floor(after_max[1] - 1.0e-9);
        z1 = (int)floor(after_max[2] - 1.0e-9);
        /* Entity.move resolves every shape from one swept list in Y-X-Z
         * order. Falling blocks have stepHeight zero. */
        actual[1] = runtime_entity_static_axis_offset(
            r, &box, 1, actual[1], x0, y0, z0, x1, y1, z1);
        box = mc_aabb_offset(&box, 0.0, actual[1], 0.0);
        actual[0] = runtime_entity_static_axis_offset(
            r, &box, 0, actual[0], x0, y0, z0, x1, y1, z1);
        box = mc_aabb_offset(&box, actual[0], 0.0, 0.0);
        actual[2] = runtime_entity_static_axis_offset(
            r, &box, 2, actual[2], x0, y0, z0, x1, y1, z1);
        box = mc_aabb_offset(&box, 0.0, 0.0, actual[2]);
        /* Entity.move calls resetPositionToBB after installing the moved box.
         * Recompute the horizontal center from its absolute endpoints rather
         * than adding motion directly; the rounding difference is observable
         * at large world coordinates. */
    } else {
        box = mc_aabb_offset(&box, actual[0], actual[1], actual[2]);
    }
    falling->bb_min_x = box.minX;
    falling->bb_min_y = box.minY;
    falling->bb_min_z = box.minZ;
    falling->bb_max_x = box.maxX;
    falling->bb_max_y = box.maxY;
    falling->bb_max_z = box.maxZ;
    falling->x = (box.minX + box.maxX) / 2.0;
    falling->y = box.minY;
    falling->z = (box.minZ + box.maxZ) / 2.0;
    falling->collided_horizontally =
        requested[0] != actual[0] || requested[2] != actual[2];
    falling->collided_vertically = requested[1] != actual[1];
    falling->on_ground =
        falling->collided_vertically && requested[1] < 0.0;
    if (falling->on_ground) {
        /* Entity.updateFallState invokes Block.onFallenUpon with the distance
         * accumulated before the clipped landing move, then clears it. */
        falling->impact_fall_distance = falling->fall_distance;
        falling->fall_distance = 0.0f;
    } else if (actual[1] < 0.0) {
        falling->impact_fall_distance = 0.0f;
        falling->fall_distance = (float)(
            (double)falling->fall_distance - actual[1]);
    }
    if (requested[0] != actual[0]) falling->vx = 0.0;
    if (requested[1] != actual[1]) falling->vy = 0.0;
    if (requested[2] != actual[2]) falling->vz = 0.0;
}

/* Bounded EntityFallingBlock.fall target: the authoritative player. Java
 * queries the landed entity's current 0.98 AABB, applies ANVIL damage before
 * the anvil's degradation roll, and uses ordinary armorable hurt immunity
 * plus DamageSource's 0.1 hunger exhaustion. Plain armor and the anvil
 * head-slot pre-hook are represented; broader loaded-entity enumeration and
 * enchanted/break side effects remain separate slices. */
static void runtime_falling_damage_player(
        GmRuntime *r, const GmRuntimeFallingBlock *falling, int impact) {
    if (impact <= 0 || r->dead || r->vitals.health <= 0.0F)
        return;
    McAABB falling_box = mc_aabb_make(
        falling->bb_min_x, falling->bb_min_y, falling->bb_min_z,
        falling->bb_max_x, falling->bb_max_y, falling->bb_max_z);
    McAABB player_box = mc_aabb_offset(
        &r->server_player.ent.box,
        (double)r->ox, 0.0, (double)r->oz);
    if (!mc_aabb_intersects(&falling_box, &player_box))
        return;
    float damage = (float)(impact * 2);
    if (damage > 40.0F)
        damage = 40.0F;
    damage = gm_mobs_anvil_helmet_pre_damage(
        &r->mobs, &r->player.inv, damage);
    int fresh_hurt = r->mobs.player_hurt_resistant <= 10;
    int hit = gm_mobs_attack_player_source(
        &r->mobs, (struct PvStats *)&r->vitals,
        &r->player.inv, damage, 0, GM_DAMAGE_SOURCE_GENERIC);
    /* With no attacking entity, EntityLivingBase.attackEntityFrom picks
     * attackedAtYaw with one Math.random() nextDouble whenever it accepts a
     * fresh raw hit, including a hit whose residual is fully absorbed.
     * Stronger damage inside the existing window applies only its delta and
     * skips that branch. */
    if (fresh_hurt && hit != 0) {
        /* setBeenAttacked nextDouble precedes the no-attacker global yaw;
         * player hurt-sound pitch then consumes two nextFloat calls. */
        (void)jrand_double(&r->mobs.player_random);
        (void)runtime_math_random_next_double(r);
        (void)jrand_float(&r->mobs.player_random);
        (void)jrand_float(&r->mobs.player_random);
    }
    if (hit == 2) {
        pv_add_exhaustion(&r->vitals, 0.1F);
    }
    r->player.health = r->vitals.health;
    r->server_player.health = r->vitals.health;
}

static void runtime_sound_event_append_delayed(
        GmRuntime *r, int sound, int category, int eid, int relative,
        double x, double y, double z, float volume, float pitch,
        int delay_ticks) {
    int index;
    if (!r || sound <= 0 || sound >= GM_SOUND_COUNT) return;
    if (r->sound_event_count < GM_RUNTIME_SOUND_EVENTS) {
        index = (r->sound_event_head + r->sound_event_count)
            % GM_RUNTIME_SOUND_EVENTS;
        ++r->sound_event_count;
    } else {
        index = r->sound_event_head;
        r->sound_event_head = (r->sound_event_head + 1)
            % GM_RUNTIME_SOUND_EVENTS;
        ++r->sound_event_dropped;
    }
    r->sound_events[index] = (GmRuntimeSoundEvent){
        r->sound_event_next_seq++, sound, category, eid, r->dimension,
        relative, delay_ticks, x, y, z, volume, pitch
    };
}

static void runtime_sound_event_append(
        GmRuntime *r, int sound, int category, int eid, int relative,
        double x, double y, double z, float volume, float pitch) {
    runtime_sound_event_append_delayed(
        r, sound, category, eid, relative,
        x, y, z, volume, pitch, 0);
}

static float runtime_sound_random_one(GmRuntime *r) {
    return 0.9F
        + runtime_java_random_seed_next_float(&r->sound_random_seed48) * 0.1F;
}

static float runtime_sound_random_diff(
        GmRuntime *r, float base, float scale) {
    float first = runtime_java_random_seed_next_float(
        &r->sound_random_seed48);
    float second = runtime_java_random_seed_next_float(
        &r->sound_random_seed48);
    return base + (first - second) * scale;
}

enum {
    RUNTIME_BLOCK_SOUND_WOOD,
    RUNTIME_BLOCK_SOUND_GRAVEL,
    RUNTIME_BLOCK_SOUND_GRASS,
    RUNTIME_BLOCK_SOUND_STONE,
    RUNTIME_BLOCK_SOUND_METAL,
    RUNTIME_BLOCK_SOUND_GLASS,
    RUNTIME_BLOCK_SOUND_CLOTH,
    RUNTIME_BLOCK_SOUND_SAND,
    RUNTIME_BLOCK_SOUND_SNOW,
    RUNTIME_BLOCK_SOUND_LADDER,
    RUNTIME_BLOCK_SOUND_ANVIL,
    RUNTIME_BLOCK_SOUND_SLIME
};

static int runtime_block_sound_family(int state_id) {
    int id, family = RUNTIME_BLOCK_SOUND_STONE;
    if (state_id < 0) return -1;
    id = state_id & 4095;
    if (id < 1 || (id > 234 && id != 255)) return -1;
    switch (id) {
    case 5: case 17: case 25: case 26: case 47: case 50: case 53:
    case 54: case 58: case 63: case 64: case 68: case 69: case 72:
    case 75: case 76: case 85: case 86: case 91: case 93: case 94:
    case 96: case 99: case 100: case 103: case 104: case 105: case 107:
    case 125: case 126: case 127: case 134: case 135: case 136:
    case 143: case 146: case 147: case 148: case 149: case 150:
    case 151: case 162: case 163: case 164: case 176: case 177:
    case 178: case 183: case 184: case 185: case 186: case 187:
    case 188: case 189: case 190: case 191: case 192: case 193:
    case 194: case 195: case 196: case 197: case 198: case 199:
    case 200: case 214:
        family = RUNTIME_BLOCK_SOUND_WOOD; break;
    case 3: case 13: case 60: case 82:
        family = RUNTIME_BLOCK_SOUND_GRAVEL; break;
    case 2: case 6: case 18: case 19: case 31: case 32: case 37:
    case 38: case 39: case 40: case 46: case 59: case 83: case 106:
    case 110: case 111: case 141: case 142: case 161: case 170:
    case 175: case 207: case 208:
        family = RUNTIME_BLOCK_SOUND_GRASS; break;
    case 27: case 28: case 41: case 42: case 52: case 57: case 66:
    case 71: case 101: case 133: case 152: case 154: case 157:
    case 167:
        family = RUNTIME_BLOCK_SOUND_METAL; break;
    case 20: case 79: case 89: case 90: case 95: case 102: case 120:
    case 123: case 124: case 160: case 169: case 174: case 212:
        family = RUNTIME_BLOCK_SOUND_GLASS; break;
    case 35: case 51: case 81: case 92: case 171:
        family = RUNTIME_BLOCK_SOUND_CLOTH; break;
    case 12: case 88:
        family = RUNTIME_BLOCK_SOUND_SAND; break;
    case 78: case 80:
        family = RUNTIME_BLOCK_SOUND_SNOW; break;
    case 65:
        family = RUNTIME_BLOCK_SOUND_LADDER; break;
    case 145:
        family = RUNTIME_BLOCK_SOUND_ANVIL; break;
    case 165:
        family = RUNTIME_BLOCK_SOUND_SLIME; break;
    default: break;
    }
    return family;
}

static int runtime_block_sound(
        int state_id, int first_sound,
        float volume_offset, float volume_divisor, float pitch_multiplier,
        int *sound, float *volume, float *pitch) {
    int family = runtime_block_sound_family(state_id);
    float type_volume, type_pitch;
    if (family < 0) return 0;
    if (sound) *sound = first_sound + family;
    type_volume = family == RUNTIME_BLOCK_SOUND_ANVIL ? 0.3F : 1.0F;
    type_pitch = family == RUNTIME_BLOCK_SOUND_METAL ? 1.5F : 1.0F;
    if (volume) *volume = (type_volume + volume_offset) / volume_divisor;
    if (pitch) *pitch = type_pitch * pitch_multiplier;
    return 1;
}

int gm_runtime_block_break_sound(
        int state_id, int *sound, float *volume, float *pitch) {
    return runtime_block_sound(
        state_id, GM_SOUND_BLOCK_WOOD_BREAK, 1.0F, 2.0F, 0.8F,
        sound, volume, pitch);
}

int gm_runtime_block_place_sound(
        int state_id, int *sound, float *volume, float *pitch) {
    return runtime_block_sound(
        state_id, GM_SOUND_BLOCK_WOOD_PLACE, 1.0F, 2.0F, 0.8F,
        sound, volume, pitch);
}

int gm_runtime_block_hit_sound(
        int state_id, int *sound, float *volume, float *pitch) {
    return runtime_block_sound(
        state_id, GM_SOUND_BLOCK_WOOD_HIT, 1.0F, 8.0F, 0.5F,
        sound, volume, pitch);
}

int gm_runtime_block_fall_sound(
        int state_id, int *sound, float *volume, float *pitch) {
    return runtime_block_sound(
        state_id, GM_SOUND_BLOCK_WOOD_FALL, 0.0F, 2.0F, 0.75F,
        sound, volume, pitch);
}

static int runtime_block_place_audio_append(
        GmRuntime *r, int x, int y, int z, int state_id) {
    int sound;
    float volume, pitch;
    if (!r || !gm_runtime_block_place_sound(
                  state_id, &sound, &volume, &pitch))
        return 0;
    runtime_sound_event_append(
        r, sound, GM_SOUND_CATEGORY_BLOCKS, 0, 0,
        (double)x + 0.5, (double)y + 0.5, (double)z + 0.5,
        volume, pitch);
    return 1;
}

static void runtime_sound_from_world_event(
        GmRuntime *r, int id, int x, int y, int z, int data) {
    int sound = 0, category = GM_SOUND_CATEGORY_BLOCKS, relative = 0;
    double sound_x = (double)x + 0.5;
    double sound_y = (double)y + 0.5;
    double sound_z = (double)z + 0.5;
    float volume = 1.0F, pitch = 1.0F;
    switch (id) {
    case 1000: sound = GM_SOUND_DISPENSER_DISPENSE; break;
    case 1001: sound = GM_SOUND_DISPENSER_FAIL; pitch = 1.2F; break;
    case 1002: sound = GM_SOUND_DISPENSER_LAUNCH; pitch = 1.2F; break;
    case 1003:
        sound = GM_SOUND_ENDEREYE_LAUNCH;
        category = GM_SOUND_CATEGORY_NEUTRAL; pitch = 1.2F; break;
    case 1004:
        sound = GM_SOUND_FIREWORK_SHOOT;
        category = GM_SOUND_CATEGORY_NEUTRAL; pitch = 1.2F; break;
    case 1005: sound = GM_SOUND_IRON_DOOR_OPEN;
        pitch = runtime_sound_random_one(r); break;
    case 1006: sound = GM_SOUND_WOODEN_DOOR_OPEN;
        pitch = runtime_sound_random_one(r); break;
    case 1007: sound = GM_SOUND_WOODEN_TRAPDOOR_OPEN;
        pitch = runtime_sound_random_one(r); break;
    case 1008: sound = GM_SOUND_FENCE_GATE_OPEN;
        pitch = runtime_sound_random_one(r); break;
    case 1009: sound = GM_SOUND_FIRE_EXTINGUISH; volume = 0.5F;
        pitch = runtime_sound_random_diff(r, 2.6F, 0.8F); break;
    case 1010:
        sound = data >= 2256 && data <= 2267
            ? GM_SOUND_RECORD_13 + data - 2256
            : GM_SOUND_RECORD_STOP;
        category = GM_SOUND_CATEGORY_RECORDS;
        volume = 4.0F;
        sound_x = (double)x;
        sound_y = (double)y;
        sound_z = (double)z;
        break;
    case 1011: sound = GM_SOUND_IRON_DOOR_CLOSE;
        pitch = runtime_sound_random_one(r); break;
    case 1012: sound = GM_SOUND_WOODEN_DOOR_CLOSE;
        pitch = runtime_sound_random_one(r); break;
    case 1013: sound = GM_SOUND_WOODEN_TRAPDOOR_CLOSE;
        pitch = runtime_sound_random_one(r); break;
    case 1014: sound = GM_SOUND_FENCE_GATE_CLOSE;
        pitch = runtime_sound_random_one(r); break;
    case 1015: sound = GM_SOUND_GHAST_WARN;
        category = GM_SOUND_CATEGORY_HOSTILE; volume = 10.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1016: sound = GM_SOUND_GHAST_SHOOT;
        category = GM_SOUND_CATEGORY_HOSTILE; volume = 10.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1017: sound = GM_SOUND_ENDERDRAGON_SHOOT;
        category = GM_SOUND_CATEGORY_HOSTILE; volume = 10.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1018: sound = GM_SOUND_BLAZE_SHOOT;
        category = GM_SOUND_CATEGORY_HOSTILE; volume = 2.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1019: sound = GM_SOUND_ZOMBIE_ATTACK_DOOR_WOOD;
        category = GM_SOUND_CATEGORY_HOSTILE; volume = 2.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1020: sound = GM_SOUND_ZOMBIE_ATTACK_IRON_DOOR;
        category = GM_SOUND_CATEGORY_HOSTILE; volume = 2.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1021: sound = GM_SOUND_ZOMBIE_BREAK_DOOR_WOOD;
        category = GM_SOUND_CATEGORY_HOSTILE; volume = 2.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1022: sound = GM_SOUND_WITHER_BREAK_BLOCK;
        category = GM_SOUND_CATEGORY_HOSTILE; volume = 2.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1024: sound = GM_SOUND_WITHER_SHOOT;
        category = GM_SOUND_CATEGORY_HOSTILE; volume = 2.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1025: sound = GM_SOUND_BAT_TAKEOFF;
        category = GM_SOUND_CATEGORY_NEUTRAL; volume = 0.05F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1026: sound = GM_SOUND_ZOMBIE_INFECT;
        category = GM_SOUND_CATEGORY_HOSTILE; volume = 2.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1027: sound = GM_SOUND_ZOMBIE_VILLAGER_CONVERTED;
        category = GM_SOUND_CATEGORY_NEUTRAL; volume = 2.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F); break;
    case 1029: sound = GM_SOUND_ANVIL_DESTROY;
        pitch = runtime_sound_random_one(r); break;
    case 1030: sound = GM_SOUND_ANVIL_USE;
        pitch = runtime_sound_random_one(r); break;
    case 1031: sound = GM_SOUND_ANVIL_LAND; volume = 0.3F;
        pitch = runtime_sound_random_one(r); break;
    case 1032: sound = GM_SOUND_PORTAL_TRAVEL;
        category = GM_SOUND_CATEGORY_MASTER; relative = 1;
        pitch = 0.8F + runtime_java_random_seed_next_float(
            &r->sound_random_seed48) * 0.4F; break;
    case 1033: sound = GM_SOUND_CHORUS_FLOWER_GROW; break;
    case 1034: sound = GM_SOUND_CHORUS_FLOWER_DEATH; break;
    case 1035: sound = GM_SOUND_BREWING_STAND_BREW; break;
    case 1036: sound = GM_SOUND_IRON_TRAPDOOR_CLOSE;
        pitch = runtime_sound_random_one(r); break;
    case 1037: sound = GM_SOUND_IRON_TRAPDOOR_OPEN;
        pitch = runtime_sound_random_one(r); break;
    case 2001:
        if (!gm_runtime_block_break_sound(data, &sound, &volume, &pitch))
            return;
        break;
    case 3000: sound = GM_SOUND_END_GATEWAY_SPAWN; volume = 10.0F;
        pitch = runtime_sound_random_diff(r, 1.0F, 0.2F) * 0.7F; break;
    case 3001: sound = GM_SOUND_ENDERDRAGON_GROWL;
        category = GM_SOUND_CATEGORY_HOSTILE; volume = 64.0F;
        pitch = 0.8F + runtime_java_random_seed_next_float(
            &r->sound_random_seed48) * 0.3F; break;
    default: return;
    }
    runtime_sound_event_append(
        r, sound, category, 0, relative,
        sound_x, sound_y, sound_z,
        volume, pitch);
}

static void runtime_sound_drain_mobs(GmRuntime *r) {
    int count;
    if (!r || r->sound_mob_next_seq == r->mobs.event_next_seq) return;
    count = gm_mobs_event_count(&r->mobs);
    for (int i = 0; i < count; ++i) {
        GmMobEvent event;
        int category = GM_SOUND_CATEGORY_NEUTRAL;
        if (!gm_mobs_event_get(&r->mobs, i, &event)
                || event.seq < r->sound_mob_next_seq
                || event.kind != GM_MOB_EVENT_SOUND
                || event.data < GM_MOB_SOUND_CHICKEN_HURT
                || event.data > GM_MOB_SOUND_PIG_SADDLE)
            continue;
        if (event.data == GM_MOB_SOUND_COW_MILK
                || event.data == GM_MOB_SOUND_ITEM_ARMOR_EQUIP_GENERIC)
            category = GM_SOUND_CATEGORY_PLAYERS;
        runtime_sound_event_append(
            r, GM_SOUND_CHICKEN_HURT + event.data - 1,
            category, event.eid, 0,
            event.x, event.y, event.z, event.volume, event.pitch);
    }
    r->sound_mob_next_seq = r->mobs.event_next_seq;
}

int gm_runtime_sound_event_count(const GmRuntime *r) {
    return r ? r->sound_event_count : 0;
}

int gm_runtime_sound_event_get(
        const GmRuntime *r, int index, GmRuntimeSoundEvent *out) {
    int slot;
    if (!r || !out || index < 0 || index >= r->sound_event_count)
        return 0;
    slot = (r->sound_event_head + index) % GM_RUNTIME_SOUND_EVENTS;
    *out = r->sound_events[slot];
    return 1;
}

int gm_runtime_set_sound_random_seed48(GmRuntime *r, uint64_t seed48) {
    if (!r || seed48 > ((UINT64_C(1) << 48) - UINT64_C(1))) return 0;
    r->sound_random_seed48 = seed48;
    return 1;
}

static GmRuntimeVillageResident *runtime_villager_resident(
        GmRuntime *r, int eid, int *mob_slot) {
    if (!r || eid <= 0) return NULL;
    for (int i = 0; i < r->village_resident_count; ++i) {
        GmRuntimeVillageResident *resident = &r->village_residents[i];
        if (resident->eid != eid) continue;
        int slot = gm_mobs_find_slot_by_eid(&r->mobs, eid);
        if (slot < 0) return NULL;
        if (mob_slot) *mob_slot = slot;
        if (!resident->trade.initialized)
            gm_villager_trade_init(
                &resident->trade, resident->profession,
                &r->mobs.entity_random[slot].random);
        return resident;
    }
    return NULL;
}

int gm_runtime_villager_offer_count(GmRuntime *r, int eid) {
    GmRuntimeVillageResident *resident =
        runtime_villager_resident(r, eid, NULL);
    return resident ? resident->trade.offer_count : 0;
}

int gm_runtime_villager_offer_get(
        GmRuntime *r, int eid, int index, GmVillagerOffer *out) {
    GmRuntimeVillageResident *resident =
        runtime_villager_resident(r, eid, NULL);
    const GmVillagerOffer *offer = resident
        ? gm_villager_trade_offer(&resident->trade, index) : NULL;
    if (!offer || !out) return 0;
    *out = *offer;
    return 1;
}

int gm_runtime_villager_trade_execute(
        GmRuntime *r, int eid, int offer_index,
        ICStack *first, ICStack *second, ICStack *output,
        int *xp_value) {
    int slot = -1;
    GmRuntimeVillageResident *resident =
        runtime_villager_resident(r, eid, &slot);
    JavaRandom *random;
    const EwStore *store;
    int xp;
    float pitch;
    if (!resident || slot < 0) return 0;
    if (!gm_villager_trade_execute(
            &resident->trade, offer_index, first, second, output))
        return 0;
    random = &r->mobs.entity_random[slot].random;
    if (!gm_villager_trade_use(
            &resident->trade, offer_index, random, &pitch, &xp))
        return 0;
    store = r->mobs.current ? &r->mobs.b : &r->mobs.a;
    runtime_sound_event_append(
        r, GM_SOUND_VILLAGER_YES, GM_SOUND_CATEGORY_NEUTRAL,
        eid, 0, store->x[slot], store->y[slot], store->z[slot],
        1.0F, pitch);
    if (resident->trade.offers[offer_index].rewards_exp)
        gm_mobs_spawn_xp(
            &r->mobs, store->x[slot], store->y[slot] + 0.5,
            store->z[slot], xp);
    if (xp_value) *xp_value = xp;
    return 1;
}

static void runtime_world_event_append(
        GmRuntime *r, int id, int x, int y, int z, int data) {
    int index;
    if (r->world_event_count < GM_RUNTIME_WORLD_EVENT_CAPACITY) {
        index = (r->world_event_head + r->world_event_count)
            % GM_RUNTIME_WORLD_EVENT_CAPACITY;
        ++r->world_event_count;
    } else {
        index = r->world_event_head;
        r->world_event_head = (r->world_event_head + 1)
            % GM_RUNTIME_WORLD_EVENT_CAPACITY;
        ++r->world_event_dropped;
    }
    r->world_events[index] = (GmRuntimeWorldEvent){
        r->world_event_next_seq++, id, r->dimension, x, y, z, data
    };
    runtime_sound_from_world_event(r, id, x, y, z, data);
}

int gm_runtime_block_break_audio_fixture(
        GmRuntime *r, int x, int y, int z, int state_id) {
    int sound;
    if (!r || !gm_runtime_block_break_sound(
            state_id, &sound, NULL, NULL))
        return 0;
    runtime_world_event_append(r, 2001, x, y, z, state_id);
    return 1;
}

int gm_runtime_block_place_audio_fixture(
        GmRuntime *r, int x, int y, int z, int state_id) {
    return runtime_block_place_audio_append(r, x, y, z, state_id);
}

int gm_runtime_world_event_count(const GmRuntime *r) {
    return r ? r->world_event_count : 0;
}

int gm_runtime_world_event_get(
        const GmRuntime *r, int index, GmRuntimeWorldEvent *out) {
    if (!r || !out || index < 0 || index >= r->world_event_count)
        return 0;
    int slot = (r->world_event_head + index)
        % GM_RUNTIME_WORLD_EVENT_CAPACITY;
    *out = r->world_events[slot];
    return 1;
}

static void runtime_tick_falling_blocks(GmRuntime *r) {
    if (r->falling_block_count == 0)
        return;
    for (int i = 0; i < GM_RUNTIME_FALLING_BLOCKS; ++i) {
        GmRuntimeFallingBlock *falling = &r->falling_blocks[i];
        if (!falling->active)
            continue;
        if (falling->fall_time++ == 0) {
            int x = (int)floor(falling->x);
            int y = (int)floor(falling->y);
            int z = (int)floor(falling->z);
            if (gm_world_block(r->world, x, y, z) == falling->block)
                gm_world_set_block_meta(r->world, x, y, z, 0, 0);
            else {
                falling->active = 0;
                --r->falling_block_count;
                continue;
            }
        }
        if (!falling->no_gravity)
            falling->vy -= 0.03999999910593033;
        runtime_falling_move_self(r, falling);
        if (falling->on_ground && falling->hurt_entities) {
            int impact = (int)ceilf(falling->impact_fall_distance - 1.0f);
            if (impact > 0) {
                runtime_falling_damage_player(r, falling, impact);
                {
                    McAABB falling_box = mc_aabb_make(
                        falling->bb_min_x, falling->bb_min_y,
                        falling->bb_min_z, falling->bb_max_x,
                        falling->bb_max_y, falling->bb_max_z);
                    float damage = (float)(impact * 2);
                    if (damage > 40.0F) damage = 40.0F;
                    (void)gm_mobs_falling_anvil_damage_controlled_passives(
                        &r->mobs, r->dimension, &falling_box, damage,
                        &r->math_random_seed48, &r->entities,
                        &r->next_entity_id, r->do_mob_loot);
                }
                JavaRandom random;
                jrand_set_seed48(&random, falling->random_seed48);
                float roll = jrand_float(&random);
                falling->random_seed48 = random.seed;
                if ((double)roll
                        < 0.05000000074505806 + (double)impact * 0.05) {
                    int damage = (falling->meta & 15) >> 2;
                    if (++damage > 2)
                        falling->dont_set_block = 1;
                    else
                        falling->meta = (falling->meta & 3) | (damage << 2);
                }
            }
        }
        runtime_redstone_falling_block_collisions(r, falling);
        falling->vx *= 0.9800000190734863;
        falling->vy *= 0.9800000190734863;
        falling->vz *= 0.9800000190734863;
        if (falling->on_ground) {
            int below_x = (int)floor(falling->x);
            int below_y = (int)floor(
                falling->y - 0.009999999776482582);
            int below_z = (int)floor(falling->z);
            /* Forge's 1.11.2 guard is the nested conjunction
             * isAirBlock(p) && BlockFalling.canFallThrough(p). */
            if (gm_world_block(
                    r->world, below_x, below_y, below_z) == 0) {
                falling->on_ground = 0;
                continue;
            }
        }
        int block_y = (int)floor(falling->y);
        int timed_out = !falling->on_ground && (
            (falling->fall_time > 100
                && (block_y < 1 || block_y > 256))
            || falling->fall_time > 600);
        if (timed_out) {
            if (falling->should_drop_item && r->do_entity_drops)
                (void)runtime_falling_spawn_item(r, falling);
            falling->active = 0;
            --r->falling_block_count;
            continue;
        }
        if (!falling->on_ground)
            continue;
        falling->vx *= 0.699999988079071;
        falling->vy *= -0.5;
        falling->vz *= 0.699999988079071;
        int x = (int)floor(falling->x);
        int y = (int)floor(falling->y);
        int z = (int)floor(falling->z);
        int current_block = gm_world_block(r->world, x, y, z);
        int current_meta = gm_world_meta(r->world, x, y, z);
        if (current_block == 34)
            continue;
        int replaceable = runtime_falling_can_fall_through(current_block)
            || (current_block == 78 && current_meta == 0);
        int supported = !runtime_falling_can_fall_through(
            gm_world_block(r->world, x, y - 1, z));
        int placed = 0;
        if (!falling->dont_set_block && replaceable && supported) {
            if (falling->block == 122 || falling->block == 145) {
                /* EntityFallingBlock uses World.setBlockState(..., 3).
                 * Route eggs/anvils through ordinary on-added and neighbor
                 * behavior; duplicate callbacks collapse to one entry. */
                placed = gm_runtime_set_block(
                    r, x, y, z, falling->block, falling->meta);
            } else {
                gm_world_set_block_meta(
                    r->world, x, y, z, falling->block, falling->meta);
                runtime_schedule_tick_insert(
                    r, x, y, z, falling->block,
                    r->clock.total_time + 2, 0,
                    r->scheduled_tick_next_order);
                placed = 1;
            }
            if (placed && falling->block == 145)
                runtime_world_event_append(r, 1031, x, y, z, 0);
        } else if (!falling->dont_set_block
                && falling->should_drop_item && r->do_entity_drops) {
            if (!runtime_falling_spawn_item(r, falling)) {
                falling->vy = 0.0;
                continue;
            }
        } else if (falling->dont_set_block && falling->block == 145) {
            runtime_world_event_append(r, 1029, x, y, z, 0);
        }
        falling->active = 0;
        --r->falling_block_count;
    }
}

void gm_runtime_tick_falling_fixture_phase(GmRuntime *r) {
    if (r) runtime_tick_falling_blocks(r);
}

static void runtime_tick_primed_tnt(GmRuntime *r) {
    const double half_width = (double)0.98f / 2.0;
    const double height = (double)0.98f;
    if (r->primed_tnt_count == 0)
        return;
    for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i) {
        GmRuntimePrimedTnt *tnt = &r->primed_tnt[i];
        McAABB box;
        double requested[3], actual[3];
        double after_min[3], after_max[3];
        int x0, y0, z0, x1, y1, z1;
        int on_ground;
        if (!tnt->active || tnt->dimension != r->dimension)
            continue;
        tnt->vy -= 0.03999999910593033;
        requested[0] = actual[0] = tnt->vx;
        requested[1] = actual[1] = tnt->vy;
        requested[2] = actual[2] = tnt->vz;
        box = mc_aabb_make(
            tnt->x - half_width, tnt->y, tnt->z - half_width,
            tnt->x + half_width, tnt->y + height,
            tnt->z + half_width);
        after_min[0] = fmin(box.minX, box.minX + requested[0]);
        after_min[1] = fmin(box.minY, box.minY + requested[1]);
        after_min[2] = fmin(box.minZ, box.minZ + requested[2]);
        after_max[0] = fmax(box.maxX, box.maxX + requested[0]);
        after_max[1] = fmax(box.maxY, box.maxY + requested[1]);
        after_max[2] = fmax(box.maxZ, box.maxZ + requested[2]);
        x0 = (int)floor(after_min[0]);
        y0 = (int)floor(after_min[1]);
        z0 = (int)floor(after_min[2]);
        x1 = (int)floor(after_max[0] - 1.0e-9);
        y1 = (int)floor(after_max[1] - 1.0e-9);
        z1 = (int)floor(after_max[2] - 1.0e-9);
        /* Entity.move resolves Y, X, then Z against one collision list. */
        actual[1] = runtime_entity_static_axis_offset(
            r, &box, 1, actual[1], x0, y0, z0, x1, y1, z1);
        box = mc_aabb_offset(&box, 0.0, actual[1], 0.0);
        actual[0] = runtime_entity_static_axis_offset(
            r, &box, 0, actual[0], x0, y0, z0, x1, y1, z1);
        box = mc_aabb_offset(&box, actual[0], 0.0, 0.0);
        actual[2] = runtime_entity_static_axis_offset(
            r, &box, 2, actual[2], x0, y0, z0, x1, y1, z1);
        tnt->x += actual[0];
        tnt->y += actual[1];
        tnt->z += actual[2];
        on_ground = requested[1] != actual[1] && requested[1] < 0.0;
        if (requested[0] != actual[0]) tnt->vx = 0.0;
        if (requested[1] != actual[1]) tnt->vy = 0.0;
        if (requested[2] != actual[2]) tnt->vz = 0.0;
        tnt->vx *= 0.9800000190734863;
        tnt->vy *= 0.9800000190734863;
        tnt->vz *= 0.9800000190734863;
        if (on_ground) {
            tnt->vx *= 0.699999988079071;
            tnt->vz *= 0.699999988079071;
            tnt->vy *= -0.5;
        }
        if (--tnt->fuse <= 0) {
            double explosion_y = tnt->y + height / 16.0;
            double explosion_x = tnt->x;
            double explosion_z = tnt->z;
            runtime_explode_with_rays(
                r, explosion_x, explosion_y, explosion_z, 4.0F, 1);
            tnt->active = 0;
            --r->primed_tnt_count;
        }
    }
}

static void runtime_tick_end_crystals(GmRuntime *r) {
    if (r->end_crystal_count <= 0) return;
    for (int i = 0; i < GM_RUNTIME_END_CRYSTALS; ++i) {
        GmRuntimeEndCrystal *crystal = &r->end_crystals[i];
        if (!crystal->active || crystal->dimension != r->dimension)
            continue;
        ++crystal->inner_rotation;
        if (r->dimension == 1) {
            int x = (int)floor(crystal->x);
            int y = (int)floor(crystal->y);
            int z = (int)floor(crystal->z);
            if (gm_world_block(r->world, x, y, z) != 51)
                (void)gm_runtime_set_block(r, x, y, z, 51, 0);
        }
    }
}

/* java.util.Random's specified 48-bit linear congruential generator. The
 * stored cursor is the private AtomicLong payload after setSeed scrambling. */
#define GM_JAVA_RANDOM_MASK ((UINT64_C(1) << 48) - UINT64_C(1))
#define GM_JAVA_RANDOM_MULT UINT64_C(0x5DEECE66D)
#define GM_JAVA_RANDOM_ADD UINT64_C(0xB)

static int runtime_java_random_seed_next(uint64_t *seed48, int bits) {
    *seed48 =
        (*seed48 * GM_JAVA_RANDOM_MULT + GM_JAVA_RANDOM_ADD)
        & GM_JAVA_RANDOM_MASK;
    return (int)(*seed48 >> (48 - bits));
}

static long long runtime_java_random_seed_next_long(uint64_t *seed48) {
    JavaRandom random;
    long long value;
    jrand_set_seed48(&random, *seed48);
    value = jrand_long(&random);
    *seed48 = random.seed;
    return value;
}

static float runtime_java_random_seed_next_float(uint64_t *seed48) {
    return (float)runtime_java_random_seed_next(seed48, 24)
        / (float)(UINT32_C(1) << 24);
}

static int runtime_java_random_next(GmRuntime *r, int bits) {
    return runtime_java_random_seed_next(&r->world_random_seed48, bits);
}

static float runtime_java_random_next_float(GmRuntime *r) {
    return (float)runtime_java_random_next(r, 24)
        / (float)(UINT32_C(1) << 24);
}

static double runtime_math_random_next_double(GmRuntime *r) {
    uint64_t high = (uint64_t)runtime_java_random_seed_next(
        &r->math_random_seed48, 26);
    uint64_t low = (uint64_t)runtime_java_random_seed_next(
        &r->math_random_seed48, 27);
    return (double)((high << 27) + low)
        / (double)(UINT64_C(1) << 53);
}

static int runtime_java_random_seed_next_int(
        uint64_t *seed48, int bound) {
    if (bound <= 0)
        return -1;
    if ((bound & -bound) == bound)
        return (int)(((long long)bound
            * (long long)runtime_java_random_seed_next(seed48, 31)) >> 31);
    for (;;) {
        int bits = runtime_java_random_seed_next(seed48, 31);
        int value = bits % bound;
        int32_t java_sum = (int32_t)(
            (uint32_t)bits - (uint32_t)value
                + (uint32_t)(bound - 1));
        if (java_sum >= 0)
            return value;
    }
}

static int runtime_java_random_next_int(GmRuntime *r, int bound) {
    return runtime_java_random_seed_next_int(
        &r->world_random_seed48, bound);
}

static uint32_t runtime_world_update_lcg_step(GmRuntime *r) {
    uint32_t value = (uint32_t)r->world_update_lcg;
    value = value * UINT32_C(3) + UINT32_C(1013904223);
    r->world_update_lcg = (int32_t)value;
    return value;
}

static void runtime_weather_event_append(
        GmRuntime *r, int kind, int eid, double x, double y, double z,
        float volume, float pitch) {
    int index;
    if (r->weather_event_count < GM_RUNTIME_WEATHER_EVENTS) {
        index = (r->weather_event_head + r->weather_event_count)
            % GM_RUNTIME_WEATHER_EVENTS;
        ++r->weather_event_count;
    } else {
        index = r->weather_event_head;
        r->weather_event_head = (r->weather_event_head + 1)
            % GM_RUNTIME_WEATHER_EVENTS;
        ++r->weather_event_dropped;
    }
    r->weather_events[index] = (GmRuntimeWeatherEvent){
        r->weather_event_next_seq++, kind, eid, x, y, z, volume, pitch
    };
    runtime_sound_event_append(
        r,
        kind == GM_WEATHER_EVENT_THUNDER
            ? GM_SOUND_LIGHTNING_THUNDER : GM_SOUND_LIGHTNING_IMPACT,
        GM_SOUND_CATEGORY_WEATHER, eid, 0,
        x, y, z, volume, pitch);
}

int gm_runtime_weather_event_count(const GmRuntime *r) {
    return r ? r->weather_event_count : 0;
}

int gm_runtime_weather_event_get(
        const GmRuntime *r, int index, GmRuntimeWeatherEvent *out) {
    int slot;
    if (!r || !out || index < 0 || index >= r->weather_event_count)
        return 0;
    slot = (r->weather_event_head + index) % GM_RUNTIME_WEATHER_EVENTS;
    *out = r->weather_events[slot];
    return 1;
}

int gm_runtime_lightning_views(
        const GmRuntime *r, GmLightningView *out, int max) {
    int count = 0;
    if (!r || !out || max <= 0) return 0;
    for (int i = 0; i < GM_RUNTIME_LIGHTNING && count < max; ++i) {
        const GmRuntimeLightning *bolt = &r->lightning[i];
        if (!bolt->active || bolt->dimension != r->dimension) continue;
        out[count++] = (GmLightningView){
            bolt->eid, bolt->bolt_vertex,
            (float)bolt->x, (float)bolt->y, (float)bolt->z
        };
    }
    return count;
}

static int runtime_lightning_place_fire(
        GmRuntime *r, int x, int y, int z) {
    if (!r->do_fire_tick || y < 0 || y > 255
            || gm_world_block(r->world, x, y, z) != 0)
        return 0;
    return gm_runtime_set_block(r, x, y, z, 51, 0)
        && gm_world_block(r->world, x, y, z) == 51;
}

int gm_runtime_set_next_lightning_random_seed48(
        GmRuntime *r, uint64_t seed48) {
    if (!r || seed48 > GM_JAVA_RANDOM_MASK)
        return 0;
    r->next_lightning_random_valid = 1;
    r->next_lightning_random_seed48 = seed48;
    return 1;
}

int gm_runtime_spawn_lightning(
        GmRuntime *r, double x, double y, double z, int effect_only) {
    GmRuntimeLightning *bolt = NULL;
    uint64_t seed48;
    int bx, by, bz;
    if (!r || !r->world || (effect_only != 0 && effect_only != 1)
            || r->lightning_count >= GM_RUNTIME_LIGHTNING)
        return -1;
    for (int i = 0; i < GM_RUNTIME_LIGHTNING; ++i) {
        if (!r->lightning[i].active) {
            bolt = &r->lightning[i];
            break;
        }
    }
    if (!bolt) return -1;
    seed48 = r->next_lightning_random_valid
        ? r->next_lightning_random_seed48
        : mc_hash_seed(
            (uint64_t)r->seed, r->tick, (int)floor(x), (int)floor(z),
            r->next_entity_id, UINT32_C(0x4C495447))
            & GM_JAVA_RANDOM_MASK;
    r->next_lightning_random_valid = 0;
    *bolt = (GmRuntimeLightning){
        1, r->dimension, r->next_entity_id++, 2, 0, effect_only, 0,
        seed48, x, y, z
    };
    bolt->bolt_vertex = runtime_java_random_seed_next_long(
        &bolt->random_seed48);
    bolt->living_time = runtime_java_random_seed_next_int(
        &bolt->random_seed48, 3) + 1;
    ++r->lightning_count;
    bx = (int)floor(x); by = (int)floor(y); bz = (int)floor(z);
    /* The live runtime models NORMAL difficulty. Constructor fire is skipped
     * for effect-only bolts and follows the entity-owned Random cursor. */
    if (!effect_only && r->do_fire_tick) {
        (void)runtime_lightning_place_fire(r, bx, by, bz);
        for (int i = 0; i < 4; ++i) {
            int fx = bx + runtime_java_random_seed_next_int(
                &bolt->random_seed48, 3) - 1;
            int fy = by + runtime_java_random_seed_next_int(
                &bolt->random_seed48, 3) - 1;
            int fz = bz + runtime_java_random_seed_next_int(
                &bolt->random_seed48, 3) - 1;
            (void)runtime_lightning_place_fire(r, fx, fy, fz);
        }
    }
    return bolt->eid;
}

static void runtime_lightning_strike_player(
        GmRuntime *r, const McAABB *strike_box) {
    McAABB player_box = mc_aabb_offset(
        &r->server_player.ent.box, (double)r->ox, 0.0, (double)r->oz);
    int hit;
    if (r->dead || r->vitals.health <= 0.0F
            || !mc_aabb_intersects(&player_box, strike_box))
        return;
    hit = gm_mobs_attack_player_source(
        &r->mobs, (struct PvStats *)&r->vitals, &r->player.inv,
        5.0F, 0, GM_DAMAGE_SOURCE_GENERIC);
    (void)hit;
    ++r->player_fire_ticks;
    if (r->player_fire_ticks == 0)
        r->player_fire_ticks = 8 * 20;
    r->player.health = r->vitals.health;
    r->server_player.health = r->vitals.health;
}

static void runtime_tick_lightning(GmRuntime *r) {
    if (r->last_lightning_bolt > 0)
        --r->last_lightning_bolt;
    if (r->lightning_count == 0)
        return;
    for (int i = 0; i < GM_RUNTIME_LIGHTNING; ++i) {
        GmRuntimeLightning *bolt = &r->lightning[i];
        McAABB strike_box;
        if (!bolt->active || bolt->dimension != r->dimension) continue;
        if (bolt->lightning_state == 2) {
            runtime_weather_event_append(
                r, GM_WEATHER_EVENT_THUNDER, bolt->eid,
                bolt->x, bolt->y, bolt->z, 10000.0F,
                0.8F + runtime_java_random_seed_next_float(
                    &bolt->random_seed48) * 0.2F);
            runtime_weather_event_append(
                r, GM_WEATHER_EVENT_IMPACT, bolt->eid,
                bolt->x, bolt->y, bolt->z, 2.0F,
                0.5F + runtime_java_random_seed_next_float(
                    &bolt->random_seed48) * 0.2F);
        }
        --bolt->lightning_state;
        if (bolt->lightning_state < 0) {
            if (bolt->living_time == 0) {
                bolt->active = 0;
                --r->lightning_count;
                continue;
            }
            if (bolt->lightning_state < -runtime_java_random_seed_next_int(
                    &bolt->random_seed48, 10)) {
                --bolt->living_time;
                bolt->lightning_state = 1;
                if (!bolt->effect_only) {
                    bolt->bolt_vertex = runtime_java_random_seed_next_long(
                        &bolt->random_seed48);
                    (void)runtime_lightning_place_fire(
                        r, (int)floor(bolt->x), (int)floor(bolt->y),
                        (int)floor(bolt->z));
                }
            }
        }
        if (bolt->lightning_state < 0)
            continue;
        r->last_lightning_bolt = 2;
        if (bolt->effect_only)
            continue;
        strike_box = mc_aabb_make(
            bolt->x - 3.0, bolt->y - 3.0, bolt->z - 3.0,
            bolt->x + 3.0, bolt->y + 9.0, bolt->z + 3.0);
        runtime_lightning_strike_player(r, &strike_box);
        (void)gm_mobs_lightning_strike(
            &r->mobs, r->dimension, &strike_box,
            &r->entities, &r->next_entity_id);
    }
}

static void runtime_firework_event_append(
        GmRuntime *r, int kind, const GmRuntimeFirework *rocket) {
    int index;
    if (r->firework_event_count < GM_RUNTIME_FIREWORK_EVENTS) {
        index = (r->firework_event_head + r->firework_event_count)
            % GM_RUNTIME_FIREWORK_EVENTS;
        ++r->firework_event_count;
    } else {
        index = r->firework_event_head;
        r->firework_event_head = (r->firework_event_head + 1)
            % GM_RUNTIME_FIREWORK_EVENTS;
        ++r->firework_event_dropped;
    }
    r->firework_events[index] = (GmRuntimeFireworkEvent){
        r->firework_event_next_seq++, kind, rocket->eid,
        rocket->explosion_count, rocket->x, rocket->y, rocket->z,
        kind == GM_FIREWORK_EVENT_LAUNCH ? 3.0F : 4.0F, 1.0F
    };
    if (kind == GM_FIREWORK_EVENT_LAUNCH)
        runtime_sound_event_append(
            r, GM_SOUND_FIREWORK_LAUNCH, GM_SOUND_CATEGORY_AMBIENT,
            rocket->eid, 0, rocket->x, rocket->y, rocket->z, 3.0F, 1.0F);
}

static int runtime_firework_sound_delay(
        const GmRuntime *r, double x, double y, double z, int *far) {
    double px = r->player.ent.posX + (double)r->ox;
    double py = r->player.ent.posY;
    double pz = r->player.ent.posZ + (double)r->oz;
    double dx = px - x, dy = py - y, dz = pz - z;
    double distance_sq = dx * dx + dy * dy + dz * dz;
    if (far) *far = distance_sq >= 256.0;
    return distance_sq > 100.0 ? (int)(sqrt(distance_sq) / 2.0) : 0;
}

static void runtime_firework_schedule_twinkle(
        GmRuntime *r, const GmRuntimeFirework *rocket) {
    GmRuntimeFireworkTwinkle *pending = NULL;
    if (!rocket->twinkle) return;
    for (int i = 0; i < GM_RUNTIME_FIREWORK_TWINKLES; ++i)
        if (!r->firework_twinkles[i].active) {
            pending = &r->firework_twinkles[i];
            break;
        }
    if (!pending) return;
    *pending = (GmRuntimeFireworkTwinkle){
        1, rocket->dimension, rocket->eid,
        rocket->explosion_count * 2 + 14,
        rocket->twinkle_random_seed48,
        rocket->x, rocket->y, rocket->z
    };
    ++r->firework_twinkle_count;
}

static void runtime_firework_explosion_audio(
        GmRuntime *r, GmRuntimeFirework *rocket) {
    int far, sound, delay_ticks;
    float pitch;
    if (rocket->explosion_count <= 0) return;
    delay_ticks = runtime_firework_sound_delay(
        r, rocket->x, rocket->y, rocket->z, &far);
    sound = rocket->large_blast
        ? (far ? GM_SOUND_FIREWORK_LARGE_BLAST_FAR
               : GM_SOUND_FIREWORK_LARGE_BLAST)
        : (far ? GM_SOUND_FIREWORK_BLAST_FAR : GM_SOUND_FIREWORK_BLAST);
    pitch = 0.95F + runtime_java_random_seed_next_float(
        &rocket->blast_random_seed48) * 0.1F;
    runtime_sound_event_append_delayed(
        r, sound, GM_SOUND_CATEGORY_AMBIENT, rocket->eid, 0,
        rocket->x, rocket->y, rocket->z, 20.0F, pitch, delay_ticks);
    runtime_firework_schedule_twinkle(r, rocket);
}

int gm_runtime_firework_audio_fixture(
        GmRuntime *r, int eid, double x, double y, double z,
        int explosion_count, int large_blast, int twinkle,
        uint64_t blast_seed48, uint64_t twinkle_seed48) {
    GmRuntimeFirework rocket;
    if (!r || eid <= 0 || explosion_count < 1 || explosion_count > 8
            || (large_blast != 0 && large_blast != 1)
            || (twinkle != 0 && twinkle != 1)
            || blast_seed48 > GM_JAVA_RANDOM_MASK
            || twinkle_seed48 > GM_JAVA_RANDOM_MASK)
        return 0;
    memset(&rocket, 0, sizeof rocket);
    rocket.dimension = r->dimension;
    rocket.eid = eid;
    rocket.explosion_count = explosion_count;
    rocket.large_blast = large_blast || explosion_count >= 3;
    rocket.twinkle = twinkle;
    rocket.blast_random_seed48 = blast_seed48;
    rocket.twinkle_random_seed48 = twinkle_seed48;
    rocket.x = x;
    rocket.y = y;
    rocket.z = z;
    runtime_firework_explosion_audio(r, &rocket);
    return 1;
}

static void runtime_tick_firework_twinkles(GmRuntime *r) {
    if (r->firework_twinkle_count == 0) return;
    for (int i = 0; i < GM_RUNTIME_FIREWORK_TWINKLES; ++i) {
        GmRuntimeFireworkTwinkle *pending = &r->firework_twinkles[i];
        int far, sound, delay_ticks;
        float pitch;
        if (!pending->active) continue;
        if (pending->dimension != r->dimension) {
            pending->active = 0;
            --r->firework_twinkle_count;
            continue;
        }
        if (--pending->ticks_left > 0) continue;
        delay_ticks = runtime_firework_sound_delay(
            r, pending->x, pending->y, pending->z, &far);
        sound = far
            ? GM_SOUND_FIREWORK_TWINKLE_FAR : GM_SOUND_FIREWORK_TWINKLE;
        pitch = 0.9F + runtime_java_random_seed_next_float(
            &pending->random_seed48) * 0.15F;
        runtime_sound_event_append_delayed(
            r, sound, GM_SOUND_CATEGORY_AMBIENT, pending->eid, 0,
            pending->x, pending->y, pending->z,
            20.0F, pitch, delay_ticks);
        pending->active = 0;
        --r->firework_twinkle_count;
    }
}

int gm_runtime_firework_event_count(const GmRuntime *r) {
    return r ? r->firework_event_count : 0;
}

int gm_runtime_firework_event_get(
        const GmRuntime *r, int index, GmRuntimeFireworkEvent *out) {
    int slot;
    if (!r || !out || index < 0 || index >= r->firework_event_count)
        return 0;
    slot = (r->firework_event_head + index) % GM_RUNTIME_FIREWORK_EVENTS;
    *out = r->firework_events[slot];
    return 1;
}

int gm_runtime_set_next_firework_random_state(
        GmRuntime *r, uint64_t seed48, int have_next_gaussian,
        double next_gaussian) {
    if (!r || seed48 > GM_JAVA_RANDOM_MASK
            || (have_next_gaussian != 0 && have_next_gaussian != 1))
        return 0;
    r->next_firework_random_valid = 1;
    r->next_firework_random_seed48 = seed48;
    r->next_firework_random_have_gaussian = have_next_gaussian;
    r->next_firework_random_gaussian = next_gaussian;
    return 1;
}

int gm_runtime_set_next_firework_audio_random_seeds(
        GmRuntime *r, uint64_t blast_seed48, uint64_t twinkle_seed48) {
    if (!r || blast_seed48 > GM_JAVA_RANDOM_MASK
            || twinkle_seed48 > GM_JAVA_RANDOM_MASK)
        return 0;
    r->next_firework_audio_random_valid = 1;
    r->next_firework_blast_seed48 = blast_seed48;
    r->next_firework_twinkle_seed48 = twinkle_seed48;
    return 1;
}

int gm_runtime_spawn_firework_payload(
        GmRuntime *r, double x, double y, double z,
        int flight, int explosion_count, int large_blast, int twinkle,
        int attached_player) {
    GmRuntimeFirework *rocket = NULL;
    JavaGaussianRandom random;
    if (!r || !r->world || flight < 0 || flight > 3
            || explosion_count < 0 || explosion_count > 8
            || (large_blast != 0 && large_blast != 1)
            || (twinkle != 0 && twinkle != 1)
            || (attached_player != 0 && attached_player != 1)
            || r->firework_count >= GM_RUNTIME_FIREWORKS)
        return -1;
    for (int i = 0; i < GM_RUNTIME_FIREWORKS; ++i)
        if (!r->fireworks[i].active) {
            rocket = &r->fireworks[i];
            break;
        }
    if (!rocket) return -1;
    jrand_gaussian_set_state(
        &random,
        r->next_firework_random_valid
            ? r->next_firework_random_seed48
            : mc_hash_seed(
                (uint64_t)r->seed, r->tick, (int)floor(x), (int)floor(z),
                r->next_entity_id, UINT32_C(0x4657524B))
                & GM_JAVA_RANDOM_MASK,
        r->next_firework_random_valid
            ? r->next_firework_random_have_gaussian : 0,
        r->next_firework_random_valid
            ? r->next_firework_random_gaussian : 0.0);
    r->next_firework_random_valid = 0;
    *rocket = (GmRuntimeFirework){0};
    rocket->active = 1;
    rocket->dimension = r->dimension;
    rocket->eid = r->next_entity_id++;
    rocket->attached_player = attached_player;
    rocket->flight = flight;
    rocket->explosion_count = explosion_count;
    rocket->large_blast = large_blast || explosion_count >= 3;
    rocket->twinkle = twinkle;
    rocket->blast_random_seed48 = r->next_firework_audio_random_valid
        ? r->next_firework_blast_seed48
        : mc_hash_seed(
            (uint64_t)r->seed, r->tick, (int)floor(x), (int)floor(z),
            rocket->eid, UINT32_C(0x424c5354)) & GM_JAVA_RANDOM_MASK;
    rocket->twinkle_random_seed48 = r->next_firework_audio_random_valid
        ? r->next_firework_twinkle_seed48
        : mc_hash_seed(
            (uint64_t)r->seed, r->tick, (int)floor(x), (int)floor(z),
            rocket->eid, UINT32_C(0x54574e4b)) & GM_JAVA_RANDOM_MASK;
    r->next_firework_audio_random_valid = 0;
    rocket->x = x; rocket->y = y; rocket->z = z;
    rocket->vx = jrand_gaussian_next(&random) * 0.001;
    rocket->vz = jrand_gaussian_next(&random) * 0.001;
    rocket->vy = 0.05;
    rocket->lifetime = 10 * (1 + flight)
        + jrand_int_bound(&random.random, 6)
        + jrand_int_bound(&random.random, 7);
    rocket->random_seed48 = random.random.seed;
    rocket->random_have_gaussian = random.have_next_next_gaussian;
    rocket->random_gaussian = random.next_next_gaussian;
    ++r->firework_count;
    return rocket->eid;
}

int gm_runtime_spawn_firework(
        GmRuntime *r, double x, double y, double z,
        int flight, int explosion_count, int attached_player) {
    return gm_runtime_spawn_firework_payload(
        r, x, y, z, flight, explosion_count, 0, 0, attached_player);
}

static int runtime_firework_line_clear(
        const GmRuntime *r, const GmRuntimeFirework *rocket,
        double x, double y, double z) {
    GmProjectileBlockHit hit;
    return !runtime_block_hit(
        r, rocket->x, rocket->y, rocket->z, x, y, z, 1, &hit);
}

static void runtime_firework_damage(GmRuntime *r, GmRuntimeFirework *rocket) {
    float base;
    if (rocket->explosion_count <= 0) return;
    base = (float)(5 + rocket->explosion_count * 2);
    if (rocket->attached_player) {
        (void)gm_mobs_attack_player_source(
            &r->mobs, (struct PvStats *)&r->vitals, &r->player.inv,
            base, 0, GM_DAMAGE_SOURCE_EXPLOSION);
        r->player.health = r->vitals.health;
        r->server_player.health = r->vitals.health;
    } else {
        double px = r->server_player.ent.posX + (double)r->ox;
        double py = r->server_player.ent.posY;
        double pz = r->server_player.ent.posZ + (double)r->oz;
        double dx = px - rocket->x;
        double dy = py - rocket->y;
        double dz = pz - rocket->z;
        double distance = sqrt(dx * dx + dy * dy + dz * dz);
        if (distance <= 5.0
                && (runtime_firework_line_clear(
                        r, rocket, px, py, pz)
                    || runtime_firework_line_clear(
                        r, rocket, px, py + 0.9, pz))) {
            float damage = base * (float)sqrt((5.0 - distance) / 5.0);
            (void)gm_mobs_attack_player_source(
                &r->mobs, (struct PvStats *)&r->vitals, &r->player.inv,
                damage, 0, GM_DAMAGE_SOURCE_EXPLOSION);
            r->player.health = r->vitals.health;
            r->server_player.health = r->vitals.health;
        }
    }
    {
        GmMobExplosionTarget targets[GM_MOB_CAPACITY];
        int count = gm_mobs_explosion_targets(
            &r->mobs, r->dimension, targets, GM_MOB_CAPACITY);
        for (int i = 0; i < count; ++i) {
            GmMobExplosionTarget *target = &targets[i];
            double dx = target->x - rocket->x;
            double dy = target->y - rocket->y;
            double dz = target->z - rocket->z;
            double distance = sqrt(dx * dx + dy * dy + dz * dz);
            double height = target->box.maxY - target->box.minY;
            if (distance > 5.0
                    || !(runtime_firework_line_clear(
                            r, rocket, target->x, target->y, target->z)
                        || runtime_firework_line_clear(
                            r, rocket, target->x,
                            target->y + height * 0.5, target->z)))
                continue;
            (void)gm_mobs_apply_explosion(
                &r->mobs, target->slot,
                base * (float)sqrt((5.0 - distance) / 5.0),
                0.0, 0.0, 0.0, &r->entities);
        }
    }
}

void gm_runtime_tick_fireworks(GmRuntime *r) {
    runtime_tick_firework_twinkles(r);
    if (r->firework_count == 0) return;
    for (int i = 0; i < GM_RUNTIME_FIREWORKS; ++i) {
        GmRuntimeFirework *rocket = &r->fireworks[i];
        if (!rocket->active || rocket->dimension != r->dimension) continue;
        if (rocket->attached_player) {
            if (r->player.elytra_flying) {
                double ex, ey, ez, look_x, look_y, look_z;
                gm_player_look_ray(
                    &r->sin_table, &r->player,
                    &ex, &ey, &ez, &look_x, &look_y, &look_z);
                (void)ex; (void)ey; (void)ez;
                r->player.ent.motionX += look_x * 0.1
                    + (look_x * 1.5 - r->player.ent.motionX) * 0.5;
                r->player.ent.motionY += look_y * 0.1
                    + (look_y * 1.5 - r->player.ent.motionY) * 0.5;
                r->player.ent.motionZ += look_z * 0.1
                    + (look_z * 1.5 - r->player.ent.motionZ) * 0.5;
                r->server_player.ent.motionX = r->player.ent.motionX;
                r->server_player.ent.motionY = r->player.ent.motionY;
                r->server_player.ent.motionZ = r->player.ent.motionZ;
            }
            rocket->x = r->player.ent.posX + (double)r->ox;
            rocket->y = r->player.ent.posY;
            rocket->z = r->player.ent.posZ + (double)r->oz;
            rocket->vx = r->player.ent.motionX;
            rocket->vy = r->player.ent.motionY;
            rocket->vz = r->player.ent.motionZ;
        } else {
            rocket->vx *= 1.15;
            rocket->vz *= 1.15;
            rocket->vy += 0.04;
            rocket->x += rocket->vx;
            rocket->y += rocket->vy;
            rocket->z += rocket->vz;
        }
        {
            float horizontal = (float)sqrt(
                rocket->vx * rocket->vx + rocket->vz * rocket->vz);
            rocket->yaw = (float)(runtime_java_math_atan2(
                rocket->vx, rocket->vz) * (180.0 / MC_PI));
            rocket->pitch = (float)(runtime_java_math_atan2(
                rocket->vy, (double)horizontal) * (180.0 / MC_PI));
        }
        if (rocket->age == 0)
            runtime_firework_event_append(
                r, GM_FIREWORK_EVENT_LAUNCH, rocket);
        ++rocket->age;
        if (rocket->age > rocket->lifetime) {
            runtime_firework_event_append(
                r, GM_FIREWORK_EVENT_EXPLODE, rocket);
            runtime_firework_explosion_audio(r, rocket);
            runtime_firework_damage(r, rocket);
            rocket->active = 0;
            --r->firework_count;
        }
    }
}

static void runtime_fish_event_append(
        GmRuntime *r, int kind, int item, int count, int meta,
        int xp, int rod_damage) {
    int index;
    GmRuntimeFishHook *hook = &r->fish_hook;
    if (r->fish_event_count < GM_RUNTIME_FISH_EVENTS) {
        index = (r->fish_event_head + r->fish_event_count)
            % GM_RUNTIME_FISH_EVENTS;
        ++r->fish_event_count;
    } else {
        index = r->fish_event_head;
        r->fish_event_head = (r->fish_event_head + 1)
            % GM_RUNTIME_FISH_EVENTS;
        ++r->fish_event_dropped;
    }
    r->fish_events[index] = (GmRuntimeFishEvent){
        r->fish_event_next_seq++, kind, hook->eid,
        item, count, meta, xp, rod_damage, hook->x, hook->y, hook->z
    };
}

int gm_runtime_fish_event_count(const GmRuntime *r) {
    return r ? r->fish_event_count : 0;
}

int gm_runtime_fish_event_get(
        const GmRuntime *r, int index, GmRuntimeFishEvent *out) {
    int slot;
    if (!r || !out || index < 0 || index >= r->fish_event_count)
        return 0;
    slot = (r->fish_event_head + index) % GM_RUNTIME_FISH_EVENTS;
    *out = r->fish_events[slot];
    return 1;
}

/* EntityMinecart's rail matrix, indexed by EnumRailDirection metadata. */
static const int runtime_minecart_matrix[10][2][3] = {
    {{ 0, 0,-1}, { 0, 0, 1}},
    {{-1, 0, 0}, { 1, 0, 0}},
    {{-1,-1, 0}, { 1, 0, 0}},
    {{-1, 0, 0}, { 1,-1, 0}},
    {{ 0, 0,-1}, { 0,-1, 1}},
    {{ 0,-1,-1}, { 0, 0, 1}},
    {{ 0, 0, 1}, { 1, 0, 0}},
    {{ 0, 0, 1}, {-1, 0, 0}},
    {{ 0, 0,-1}, {-1, 0, 0}},
    {{ 0, 0,-1}, { 1, 0, 0}},
};

static int runtime_is_rail(int block) {
    return block == 27 || block == 28 || block == 66 || block == 157;
}

static int runtime_rail_direction(int block, int meta) {
    int direction = block == 66 ? (meta & 15) : (meta & 7);
    return direction >= 0 && direction < 10 ? direction : 0;
}

static int runtime_minecart_get_pos(
        const GmRuntime *r, double x, double y, double z,
        double *out_x, double *out_y, double *out_z) {
    int bx = mc_floor(x), by = mc_floor(y), bz = mc_floor(z);
    int block, direction;
    double x0, y0, z0, x1, y1, z1, dx, dy, dz, along;
    if (runtime_is_rail(gm_world_block(r->world, bx, by - 1, bz)))
        --by;
    block = gm_world_block(r->world, bx, by, bz);
    if (!runtime_is_rail(block)) return 0;
    direction = runtime_rail_direction(
        block, gm_world_meta(r->world, bx, by, bz));
    x0 = bx + 0.5 + runtime_minecart_matrix[direction][0][0] * 0.5;
    y0 = by + 0.0625 + runtime_minecart_matrix[direction][0][1] * 0.5;
    z0 = bz + 0.5 + runtime_minecart_matrix[direction][0][2] * 0.5;
    x1 = bx + 0.5 + runtime_minecart_matrix[direction][1][0] * 0.5;
    y1 = by + 0.0625 + runtime_minecart_matrix[direction][1][1] * 0.5;
    z1 = bz + 0.5 + runtime_minecart_matrix[direction][1][2] * 0.5;
    dx = x1 - x0;
    dy = (y1 - y0) * 2.0;
    dz = z1 - z0;
    if (dx == 0.0)
        along = z - bz;
    else if (dz == 0.0)
        along = x - bx;
    else
        along = ((x - x0) * dx + (z - z0) * dz) * 2.0;
    x = x0 + dx * along;
    y = y0 + dy * along;
    z = z0 + dz * along;
    if (dy < 0.0) y += 1.0;
    if (dy > 0.0) y += 0.5;
    if (out_x) *out_x = x;
    if (out_y) *out_y = y;
    if (out_z) *out_z = z;
    return 1;
}

static int runtime_minecart_inventory_size(const GmRuntimeMinecart *cart) {
    if (cart->kind == GM_MINECART_CHEST) return 27;
    if (cart->kind == GM_MINECART_HOPPER) return 5;
    return 0;
}

static int runtime_minecart_comparator_strength(
        const GmRuntimeMinecart *cart) {
    float fullness = 0.0f;
    int occupied = 0;
    int size = runtime_minecart_inventory_size(cart);
    if (size <= 0) return -1;
    for (int slot = 0; slot < size; ++slot) {
        const ICStack *stack = &cart->slots[slot];
        if (isr_is_empty(stack)) continue;
        int item_limit = tec_max_stack_size(stack->item);
        int limit = item_limit < 64 ? item_limit : 64;
        fullness += (float)stack->count / (float)limit;
        ++occupied;
    }
    fullness /= (float)size;
    return (int)floorf(fullness * 14.0f) + (occupied > 0 ? 1 : 0);
}

static int runtime_minecart_intersects_detector(
        const GmRuntimeMinecart *cart, int x, int y, int z) {
    /* EntityMinecart is 0.98 x 0.7. Detector rail shrinks its one-block
     * search box by float 0.2 on X/Z and by float 0.2 at the upper Y face. */
    const double inset = (double)0.2f;
    return cart->x + 0.49 > x + inset
        && cart->x - 0.49 < x + 1.0 - inset
        && cart->y + 0.7 > y
        && cart->y < y + 1.0 - inset
        && cart->z + 0.49 > z + inset
        && cart->z - 0.49 < z + 1.0 - inset;
}

static int runtime_minecart_detector_signal(
        const GmRuntime *r, int x, int y, int z) {
    if (!r || gm_world_block(r->world, x, y, z) != 28
            || (gm_world_meta(r->world, x, y, z) & 8) == 0)
        return 0;
    for (int i = 0; i < GM_RUNTIME_MINECARTS; ++i) {
        const GmRuntimeMinecart *cart = &r->minecarts[i];
        int signal;
        if (!cart->active || cart->dimension != r->dimension
                || !runtime_minecart_intersects_detector(cart, x, y, z))
            continue;
        signal = runtime_minecart_comparator_strength(cart);
        if (signal >= 0) return signal;
    }
    return 0;
}

static GmRuntimeMinecart *runtime_minecart_first_on_detector(
        GmRuntime *r, int x, int y, int z) {
    for (int i = 0; i < GM_RUNTIME_MINECARTS; ++i) {
        GmRuntimeMinecart *cart = &r->minecarts[i];
        if (cart->active && cart->dimension == r->dimension
                && runtime_minecart_intersects_detector(cart, x, y, z))
            return cart;
    }
    return NULL;
}

static void runtime_minecart_detector_update(
        GmRuntime *r, int x, int y, int z, int scheduled_callback) {
    int meta = gm_world_meta(r->world, x, y, z);
    int was_powered = (meta & 8) != 0;
    int powered = runtime_minecart_first_on_detector(r, x, y, z) != NULL;
    if (powered != was_powered) {
        gm_world_set_block_meta(
            r->world, x, y, z, 28, powered ? (meta | 8) : (meta & 7));
        runtime_redstone_notify_neighbors(r, x, y, z);
        runtime_redstone_notify_neighbors(r, x, y - 1, z);
        runtime_redstone_update_observers_at(r, x, y, z);
        runtime_redstone_update_comparator_output_level(r, x, y, z);
    }
    if (powered && (!was_powered || scheduled_callback))
        (void)runtime_schedule_tick_insert(
            r, x, y, z, 28, r->clock.total_time + 20, 0,
            r->scheduled_tick_next_order);
}

static void runtime_minecart_apply_drag(GmRuntimeMinecart *cart) {
    if (cart->kind == GM_MINECART_FURNACE) {
        double length = cart->push_x * cart->push_x
            + cart->push_z * cart->push_z;
        if (length > 1.0e-4) {
            length = sqrt(length);
            cart->push_x /= length;
            cart->push_z /= length;
            cart->vx *= 0.800000011920929;
            cart->vy *= 0.0;
            cart->vz *= 0.800000011920929;
            cart->vx += cart->push_x;
            cart->vz += cart->push_z;
        } else {
            cart->vx *= 0.9800000190734863;
            cart->vy *= 0.0;
            cart->vz *= 0.9800000190734863;
        }
    }
    cart->vx *= 0.9599999785423279;
    cart->vy *= 0.0;
    cart->vz *= 0.9599999785423279;
}

static void runtime_minecart_move_along_track(
        GmRuntime *r, GmRuntimeMinecart *cart,
        int x, int y, int z, int block, int meta) {
    const double slope = 0.0078125;
    int direction = runtime_rail_direction(block, meta);
    const int (*matrix)[3] = runtime_minecart_matrix[direction];
    double old_track_x = 0.0, old_track_y = 0.0, old_track_z = 0.0;
    double dx, dz, length, speed, dot;
    double x0, z0, x1, z1, along;
    int powered = block == 27 && (meta & 8) != 0;
    int braking = block == 27 && !powered;
    (void)runtime_minecart_get_pos(
        r, cart->x, cart->y, cart->z,
        &old_track_x, &old_track_y, &old_track_z);
    cart->y = (double)y;
    switch (direction) {
    case 2: cart->vx -= slope; cart->y += 1.0; break;
    case 3: cart->vx += slope; cart->y += 1.0; break;
    case 4: cart->vz += slope; cart->y += 1.0; break;
    case 5: cart->vz -= slope; cart->y += 1.0; break;
    default: break;
    }
    dx = matrix[1][0] - matrix[0][0];
    dz = matrix[1][2] - matrix[0][2];
    length = sqrt(dx * dx + dz * dz);
    dot = cart->vx * dx + cart->vz * dz;
    if (dot < 0.0) { dx = -dx; dz = -dz; }
    speed = sqrt(cart->vx * cart->vx + cart->vz * cart->vz);
    if (speed > 2.0) speed = 2.0;
    cart->vx = speed * dx / length;
    cart->vz = speed * dz / length;
    if (braking) {
        length = sqrt(cart->vx * cart->vx + cart->vz * cart->vz);
        if (length < 0.03) {
            cart->vx *= 0.0;
            cart->vy *= 0.0;
            cart->vz *= 0.0;
        } else {
            cart->vx *= 0.5;
            cart->vy *= 0.0;
            cart->vz *= 0.5;
        }
    }
    x0 = x + 0.5 + matrix[0][0] * 0.5;
    z0 = z + 0.5 + matrix[0][2] * 0.5;
    x1 = x + 0.5 + matrix[1][0] * 0.5;
    z1 = z + 0.5 + matrix[1][2] * 0.5;
    dx = x1 - x0;
    dz = z1 - z0;
    if (dx == 0.0) {
        cart->x = x + 0.5;
        along = cart->z - z;
    } else if (dz == 0.0) {
        cart->z = z + 0.5;
        along = cart->x - x;
    } else {
        along = ((cart->x - x0) * dx + (cart->z - z0) * dz) * 2.0;
    }
    cart->x = x0 + dx * along;
    cart->z = z0 + dz * along;
    /* moveMinecartOnRail: the rail cap and current cap are both 0.4. */
    cart->x += fmax(-0.4, fmin(0.4, cart->vx));
    cart->z += fmax(-0.4, fmin(0.4, cart->vz));
    if (matrix[0][1] != 0
            && mc_floor(cart->x) - x == matrix[0][0]
            && mc_floor(cart->z) - z == matrix[0][2])
        cart->y += matrix[0][1];
    else if (matrix[1][1] != 0
            && mc_floor(cart->x) - x == matrix[1][0]
            && mc_floor(cart->z) - z == matrix[1][2])
        cart->y += matrix[1][1];
    runtime_minecart_apply_drag(cart);
    {
        double new_x, new_y, new_z;
        if (runtime_minecart_get_pos(
                r, cart->x, cart->y, cart->z,
                &new_x, &new_y, &new_z)) {
            double delta_y = (old_track_y - new_y) * 0.05;
            speed = sqrt(cart->vx * cart->vx + cart->vz * cart->vz);
            if (speed > 0.0) {
                cart->vx = cart->vx / speed * (speed + delta_y);
                cart->vz = cart->vz / speed * (speed + delta_y);
            }
            cart->x = new_x;
            cart->y = new_y;
            cart->z = new_z;
        }
    }
    {
        int nx = mc_floor(cart->x), nz = mc_floor(cart->z);
        if (nx != x || nz != z) {
            speed = sqrt(cart->vx * cart->vx + cart->vz * cart->vz);
            cart->vx = speed * (nx - x);
            cart->vz = speed * (nz - z);
        }
    }
    if (block == 28)
        runtime_minecart_detector_update(r, x, y, z, 0);
    if (block == 157 && (meta & 8) != 0) {
        if (cart->kind == GM_MINECART_TNT && cart->tnt_fuse < 0)
            cart->tnt_fuse = 80;
        if (cart->kind == GM_MINECART_HOPPER)
            cart->hopper_enabled = 0;
    } else if (block == 157 && cart->kind == GM_MINECART_HOPPER) {
        cart->hopper_enabled = 1;
    }
    if (powered) {
        length = sqrt(cart->vx * cart->vx + cart->vz * cart->vz);
        if (length > 0.01) {
            cart->vx += cart->vx / length * 0.06;
            cart->vz += cart->vz / length * 0.06;
        } else if (direction == 1) {
            if (gm_block_is_normal_cube_1_11_2(
                    gm_world_block(r->world, x - 1, y, z),
                    gm_world_meta(r->world, x - 1, y, z)))
                cart->vx = 0.02;
            else if (gm_block_is_normal_cube_1_11_2(
                    gm_world_block(r->world, x + 1, y, z),
                    gm_world_meta(r->world, x + 1, y, z)))
                cart->vx = -0.02;
        } else if (direction == 0) {
            if (gm_block_is_normal_cube_1_11_2(
                    gm_world_block(r->world, x, y, z - 1),
                    gm_world_meta(r->world, x, y, z - 1)))
                cart->vz = 0.02;
            else if (gm_block_is_normal_cube_1_11_2(
                    gm_world_block(r->world, x, y, z + 1),
                    gm_world_meta(r->world, x, y, z + 1)))
                cart->vz = -0.02;
        }
    }
}

int gm_runtime_spawn_minecart_fixture(
        GmRuntime *r, int kind, int eid,
        double x, double y, double z, double vx, double vy, double vz,
        float yaw) {
    if (!r || !r->world || kind < GM_MINECART_RIDEABLE
            || kind > GM_MINECART_COMMAND || eid <= 0
            || r->minecart_count >= GM_RUNTIME_MINECARTS)
        return 0;
    for (int i = 0; i < GM_RUNTIME_MINECARTS; ++i) {
        GmRuntimeMinecart *cart = &r->minecarts[i];
        if (cart->active) continue;
        memset(cart, 0, sizeof *cart);
        cart->active = 1;
        cart->dimension = r->dimension;
        cart->eid = eid;
        cart->kind = kind;
        cart->x = x; cart->y = y; cart->z = z;
        cart->vx = vx; cart->vy = vy; cart->vz = vz;
        cart->yaw = yaw;
        cart->rolling_direction = 1;
        cart->tnt_fuse = -1;
        cart->hopper_enabled = 1;
        cart->transfer_cooldown = -1;
        {
            JavaRandom random;
            jrand_set(&random, runtime_entity_constructor_seed(
                r, eid, UINT64_C(0x4d494e4543415254)));
            cart->random_seed48 = random.seed;
        }
        ++r->minecart_count;
        if (eid >= r->next_entity_id) r->next_entity_id = eid + 1;
        return 1;
    }
    return 0;
}

int gm_runtime_minecart_get(
        const GmRuntime *r, int index, GmRuntimeMinecart *out) {
    int found = 0;
    if (!r || !out || index < 0) return 0;
    for (int i = 0; i < GM_RUNTIME_MINECARTS; ++i) {
        if (!r->minecarts[i].active) continue;
        if (found++ == index) { *out = r->minecarts[i]; return 1; }
    }
    return 0;
}

static GmRuntimeMinecart *runtime_minecart_by_eid(GmRuntime *r, int eid) {
    for (int i = 0; i < GM_RUNTIME_MINECARTS; ++i)
        if (r->minecarts[i].active && r->minecarts[i].eid == eid)
            return &r->minecarts[i];
    return NULL;
}

int gm_runtime_minecart_set_slot(
        GmRuntime *r, int eid, int slot, int item, int count, int meta) {
    GmRuntimeMinecart *cart = r ? runtime_minecart_by_eid(r, eid) : NULL;
    int size = cart ? runtime_minecart_inventory_size(cart) : 0;
    if (!cart || slot < 0 || slot >= size || item < 0 || count < 0
            || count > 64 || meta < 0 || meta > 32767
            || (count == 0) != (item == 0))
        return 0;
    cart->slots[slot] = count ? ic_mk(item, count, meta) : ic_empty();
    return 1;
}

int gm_runtime_minecart_set_state(
        GmRuntime *r, int eid, int fuel, double push_x, double push_z,
        int tnt_fuse, int hopper_enabled, int transfer_cooldown) {
    GmRuntimeMinecart *cart = r ? runtime_minecart_by_eid(r, eid) : NULL;
    if (!cart || fuel < -32768 || fuel > 32767 || tnt_fuse < -1
            || (hopper_enabled != 0 && hopper_enabled != 1))
        return 0;
    cart->fuel = fuel;
    cart->push_x = push_x;
    cart->push_z = push_z;
    cart->tnt_fuse = tnt_fuse;
    cart->hopper_enabled = hopper_enabled;
    cart->transfer_cooldown = transfer_cooldown;
    return 1;
}

int gm_runtime_minecart_set_base_state(
        GmRuntime *r, int eid, int reverse, int rolling_amplitude,
        int rolling_direction, float damage, float pitch) {
    GmRuntimeMinecart *cart = r ? runtime_minecart_by_eid(r, eid) : NULL;
    if (!cart || (reverse != 0 && reverse != 1)
            || rolling_amplitude < 0 || rolling_direction == 0
            || !isfinite(damage) || damage < 0.0f || !isfinite(pitch))
        return 0;
    cart->reverse = reverse;
    cart->rolling_amplitude = rolling_amplitude;
    cart->rolling_direction = rolling_direction;
    cart->damage = damage;
    cart->pitch = pitch;
    return 1;
}

int gm_runtime_minecart_set_random_state(
        GmRuntime *r, int eid, uint64_t seed48,
        int have_next_gaussian, double next_gaussian) {
    GmRuntimeMinecart *cart = r ? runtime_minecart_by_eid(r, eid) : NULL;
    if (!cart || seed48 > GM_JAVA_RANDOM_MASK
            || (have_next_gaussian != 0 && have_next_gaussian != 1)
            || !isfinite(next_gaussian))
        return 0;
    cart->random_seed48 = seed48;
    cart->random_have_gaussian = have_next_gaussian;
    cart->random_gaussian = next_gaussian;
    return 1;
}

static int runtime_minecart_insert_stack(
        GmRuntimeMinecart *cart, ICStack *moving) {
    int size = runtime_minecart_inventory_size(cart);
    if (size <= 0 || !moving || isr_is_empty(moving)) return 0;
    for (int slot = 0; slot < size && !isr_is_empty(moving); ++slot) {
        ICStack current = cart->slots[slot];
        int limit = tec_max_stack_size(moving->item);
        int accepted;
        if (limit > 64) limit = 64;
        if (isr_is_empty(&current)) {
            accepted = moving->count < limit ? moving->count : limit;
            current = ic_with_count(moving, accepted);
        } else {
            if (!ic_stack_equal(&current, moving) || current.count >= limit)
                continue;
            accepted = limit - current.count;
            if (accepted > moving->count) accepted = moving->count;
            current.count += accepted;
        }
        moving->count -= accepted;
        if (moving->count <= 0) *moving = ic_empty();
        cart->slots[slot] = current;
    }
    return isr_is_empty(moving);
}

static int runtime_minecart_capture_item_box(
        GmRuntime *r, GmRuntimeMinecart *cart, McAABB capture) {
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot) {
        GmLiveEnt *entity = &r->entities.ents[slot];
        McAABB box;
        ICStack moving;
        if (!entity->active || entity->type != 0) continue;
        box = mc_aabb_make(
            entity->x - 0.125, entity->y, entity->z - 0.125,
            entity->x + 0.125, entity->y + 0.25, entity->z + 0.125);
        if (box.maxX <= capture.minX || box.minX >= capture.maxX
                || box.maxY <= capture.minY || box.minY >= capture.maxY
                || box.maxZ <= capture.minZ || box.minZ >= capture.maxZ)
            continue;
        moving = runtime_hopper_item_stack(entity);
        (void)runtime_minecart_insert_stack(cart, &moving);
        entity->count = moving.count;
        if (isr_is_empty(&moving)) {
            entity->active = 0;
            entity->n_enchants = 0;
            if (r->entities.n_active > 0) --r->entities.n_active;
            return 1;
        }
        return 0;
    }
    return 0;
}

static int runtime_minecart_hopper_capture(
        GmRuntime *r, GmRuntimeMinecart *cart) {
    int x = mc_floor(cart->x);
    int y = mc_floor(cart->y + 1.5);
    int z = mc_floor(cart->z);
    RuntimeHopperInventory source;
    if (runtime_hopper_inventory_resolve(r, x, y, z, &source)) {
        for (int slot = 0; slot < source.size; ++slot) {
            ICStack original = runtime_hopper_inventory_get(&source, slot);
            ICStack one;
            if (!runtime_hopper_can_extract(&source, slot, &original, 0))
                continue;
            one = ic_with_count(&original, 1);
            if (!runtime_minecart_insert_stack(cart, &one)) continue;
            if (--original.count <= 0) original = ic_empty();
            runtime_hopper_inventory_set(&source, slot, original);
            runtime_redstone_update_comparator_output_level(
                r, source.x, source.y, source.z);
            return 1;
        }
        return 0;
    }
    if (runtime_minecart_capture_item_box(
            r, cart, mc_aabb_make(
                cart->x - 0.5, cart->y + 0.5, cart->z - 0.5,
                cart->x + 0.5, cart->y + 2.0, cart->z + 0.5)))
        return 1;
    return runtime_minecart_capture_item_box(
        r, cart, mc_aabb_make(
            cart->x - 0.74, cart->y, cart->z - 0.74,
            cart->x + 0.74, cart->y + 0.7, cart->z + 0.74));
}

void gm_runtime_tick_minecarts(GmRuntime *r) {
    if (!r || r->minecart_count == 0) return;
    for (int i = 0; i < GM_RUNTIME_MINECARTS; ++i) {
        GmRuntimeMinecart *cart = &r->minecarts[i];
        double old_x, old_z, dx, dz, turn;
        float old_yaw;
        int x, y, z, block, meta;
        if (!cart->active || cart->dimension != r->dimension) continue;
        if (cart->rolling_amplitude > 0) --cart->rolling_amplitude;
        if (cart->damage > 0.0f) cart->damage -= 1.0f;
        old_x = cart->x; old_z = cart->z; old_yaw = cart->yaw;
        cart->vy -= 0.03999999910593033;
        x = mc_floor(cart->x); y = mc_floor(cart->y); z = mc_floor(cart->z);
        if (runtime_is_rail(gm_world_block(r->world, x, y - 1, z))) --y;
        block = gm_world_block(r->world, x, y, z);
        meta = gm_world_meta(r->world, x, y, z);
        if (runtime_is_rail(block)) {
            runtime_minecart_move_along_track(
                r, cart, x, y, z, block, meta);
        } else {
            cart->vx = fmax(-0.4, fmin(0.4, cart->vx));
            cart->vz = fmax(-0.4, fmin(0.4, cart->vz));
            cart->x += cart->vx;
            cart->y += cart->vy;
            cart->z += cart->vz;
            cart->vx *= 0.94999998807907104;
            cart->vy *= 0.94999998807907104;
            cart->vz *= 0.94999998807907104;
        }
        cart->pitch = 0.0f;
        dx = old_x - cart->x;
        dz = old_z - cart->z;
        if (dx * dx + dz * dz > 0.001) {
            cart->yaw = (float)(atan2(dz, dx) * 180.0 / MC_PI);
            if (cart->reverse) cart->yaw += 180.0f;
        }
        turn = fmod((double)cart->yaw - (double)old_yaw, 360.0);
        if (turn >= 180.0) turn -= 360.0;
        if (turn < -180.0) turn += 360.0;
        if (turn < -170.0 || turn >= 170.0) {
            cart->yaw += 180.0f;
            cart->reverse = !cart->reverse;
        }
        cart->yaw = fmodf(cart->yaw, 360.0f);
        if (cart->yaw < 0.0f) cart->yaw += 360.0f;
        if (cart->kind == GM_MINECART_FURNACE) {
            if (cart->fuel > 0) --cart->fuel;
            if (cart->fuel <= 0)
                cart->push_x = cart->push_z = 0.0;
            else {
                JavaRandom random;
                random.seed = cart->random_seed48;
                (void)jrand_int_bound(&random, 4);
                cart->random_seed48 = random.seed;
            }
        } else if (cart->kind == GM_MINECART_TNT) {
            if (cart->tnt_fuse > 0) --cart->tnt_fuse;
            else if (cart->tnt_fuse == 0) {
                JavaRandom random;
                double speed = sqrt(
                    cart->vx * cart->vx + cart->vz * cart->vz);
                float strength;
                if (speed > 5.0) speed = 5.0;
                random.seed = cart->random_seed48;
                strength = (float)(
                    4.0 + jrand_double(&random) * 1.5 * speed);
                cart->random_seed48 = random.seed;
                cart->active = 0;
                --r->minecart_count;
                runtime_explode(
                    r, cart->x, cart->y, cart->z, strength);
            }
        } else if (cart->kind == GM_MINECART_HOPPER
                && cart->hopper_enabled) {
            /* 1.11.2's lastPosition is the immutable ORIGIN constant, so a
             * moving/non-origin hopper cart resets this ticker to zero before
             * every capture attempt. Preserve the origin quirk exactly. */
            if (mc_floor(cart->x) == 0 && mc_floor(cart->y) == 0
                    && mc_floor(cart->z) == 0)
                --cart->transfer_cooldown;
            else
                cart->transfer_cooldown = 0;
            if (cart->transfer_cooldown <= 0) {
                cart->transfer_cooldown = 0;
                if (runtime_minecart_hopper_capture(r, cart))
                    cart->transfer_cooldown = 4;
            }
        }
    }
}

int gm_runtime_set_next_fishing_random_state(
        GmRuntime *r, uint64_t seed48, int have_next_gaussian,
        double next_gaussian) {
    if (!r || seed48 > GM_JAVA_RANDOM_MASK
            || (have_next_gaussian != 0 && have_next_gaussian != 1))
        return 0;
    r->next_fishing_random_valid = 1;
    r->next_fishing_random_seed48 = seed48;
    r->next_fishing_random_have_gaussian = have_next_gaussian;
    r->next_fishing_random_gaussian = next_gaussian;
    return 1;
}

int gm_runtime_spawn_fish_hook_fixture(
        GmRuntime *r, int eid,
        double x, double y, double z, double vx, double vy, double vz,
        float yaw, float pitch, int state, int in_ground,
        int ticks_in_ground, int ticks_in_air, int ticks_catchable,
        int ticks_caught_delay, int ticks_catchable_delay,
        float approach_angle, int lure, int luck, int caught_eid,
        uint64_t seed48, int have_next_gaussian, double next_gaussian) {
    GmRuntimeFishHook *hook;
    int caught_kind = 0, caught_slot = -1;
    if (!r || !r->world || r->fish_hook.active || eid <= 0
            || state < 0 || state > 2 || (in_ground != 0 && in_ground != 1)
            || ticks_in_ground < 0 || ticks_in_air < 0
            || ticks_catchable < 0 || lure < 0 || lure > 3
            || luck < 0 || luck > 3 || seed48 > GM_JAVA_RANDOM_MASK
            || (have_next_gaussian != 0 && have_next_gaussian != 1)
            || !isfinite(x) || !isfinite(y) || !isfinite(z)
            || !isfinite(vx) || !isfinite(vy) || !isfinite(vz)
            || !isfinite(yaw) || !isfinite(pitch)
            || !isfinite(approach_angle) || !isfinite(next_gaussian))
        return 0;
    if (caught_eid != 0) {
        if (state == 2) return 0;
        caught_slot = gm_mobs_find_slot_by_eid(&r->mobs, caught_eid);
        if (caught_slot >= 0) {
            caught_kind = 1;
        } else {
            for (int i = 0; i < GM_LIVE_MAX; ++i) {
                if (r->entities.ents[i].active
                        && r->entities.ents[i].type == 0
                        && r->entities.ents[i].eid == caught_eid) {
                    caught_kind = 2;
                    caught_slot = i;
                    break;
                }
            }
        }
        if (!caught_kind) return 0;
    }
    hook = &r->fish_hook;
    memset(hook, 0, sizeof *hook);
    hook->active = 1;
    hook->dimension = r->dimension;
    hook->eid = eid;
    hook->state = state == 1 ? GM_FISH_STATE_HOOKED
        : state == 2 ? GM_FISH_STATE_BOBBING : GM_FISH_STATE_FLYING;
    hook->in_ground = in_ground;
    hook->ticks_in_ground = ticks_in_ground;
    hook->ticks_in_air = ticks_in_air;
    hook->caught_eid = caught_eid;
    hook->caught_kind = caught_kind;
    hook->caught_slot = caught_slot;
    hook->catch_state.ticks_catchable = ticks_catchable;
    hook->catch_state.ticks_caught_delay = ticks_caught_delay;
    hook->catch_state.ticks_catchable_delay = ticks_catchable_delay;
    hook->catch_state.lure = lure;
    hook->catch_state.luck = luck;
    hook->catch_state.approach_angle = approach_angle;
    hook->catch_state.motion_y = vy;
    jrand_gaussian_set_state(
        &hook->random, seed48, have_next_gaussian, next_gaussian);
    hook->yaw = yaw;
    hook->pitch = pitch;
    hook->x = x; hook->y = y; hook->z = z;
    hook->vx = vx; hook->vy = vy; hook->vz = vz;
    return 1;
}

int gm_runtime_cast_fishing_rod(GmRuntime *r, int lure, int luck) {
    GmRuntimeFishHook *hook;
    float pitch, yaw, f2, f3, f4, f5, speed;
    if (!r || !r->world || r->fish_hook.active
            || lure < 0 || lure > 3 || luck < 0 || luck > 3)
        return -1;
    hook = &r->fish_hook;
    memset(hook, 0, sizeof *hook);
    hook->active = 1;
    hook->dimension = r->dimension;
    hook->eid = r->next_entity_id++;
    hook->state = GM_FISH_STATE_FLYING;
    hook->catch_state.lure = lure;
    hook->catch_state.luck = luck;
    jrand_gaussian_set_state(
        &hook->random,
        r->next_fishing_random_valid
            ? r->next_fishing_random_seed48
            : mc_hash_seed(
                (uint64_t)r->seed, r->tick,
                mc_floor(r->player.ent.posX + r->ox),
                mc_floor(r->player.ent.posZ + r->oz),
                hook->eid, UINT32_C(0x46495348)) & GM_JAVA_RANDOM_MASK,
        r->next_fishing_random_valid
            ? r->next_fishing_random_have_gaussian : 0,
        r->next_fishing_random_valid
            ? r->next_fishing_random_gaussian : 0.0);
    r->next_fishing_random_valid = 0;
    pitch = r->player.pitch;
    yaw = r->player.yaw;
    f2 = mc_cos(&r->sin_table,
        -yaw * 0.017453292F - (float)MC_PI);
    f3 = mc_sin(&r->sin_table,
        -yaw * 0.017453292F - (float)MC_PI);
    f4 = -mc_cos(&r->sin_table, -pitch * 0.017453292F);
    f5 = mc_sin(&r->sin_table, -pitch * 0.017453292F);
    hook->x = r->player.ent.posX + (double)r->ox - (double)f3 * 0.3;
    hook->y = r->player.ent.posY + psv_player_eye_height(&r->player);
    hook->z = r->player.ent.posZ + (double)r->oz - (double)f2 * 0.3;
    hook->vx = (double)(-f3);
    hook->vy = (double)fmaxf(-5.0F, fminf(5.0F, -(f5 / f4)));
    hook->vz = (double)(-f2);
    speed = (float)sqrt(
        hook->vx * hook->vx + hook->vy * hook->vy + hook->vz * hook->vz);
    hook->vx *= 0.6 / (double)speed + 0.5
        + jrand_gaussian_next(&hook->random) * 0.0045;
    hook->vy *= 0.6 / (double)speed + 0.5
        + jrand_gaussian_next(&hook->random) * 0.0045;
    hook->vz *= 0.6 / (double)speed + 0.5
        + jrand_gaussian_next(&hook->random) * 0.0045;
    runtime_fish_event_append(r, GM_FISH_EVENT_THROW, 0, 0, 0, 0, 0);
    return hook->eid;
}

static int runtime_fishing_damage_rod(GmRuntime *r, int damage) {
    int slots[2] = {r->player.inv.current_item, ISR_OFFHAND_SLOT};
    for (int i = 0; i < 2; ++i) {
        ICStack stack = isr_get_stack(&r->player.inv, slots[i]);
        if (stack.item != 346 || stack.count <= 0) continue;
        stack.meta += damage;
        if (stack.meta > 64)
            (void)isr_decr_stack_size(&r->player.inv, slots[i], 1);
        else
            isr_set_stack(&r->player.inv, slots[i], stack);
        return 1;
    }
    return 0;
}

static int runtime_fishing_item_position(
        const GmRuntime *r, int slot, double *x, double *y, double *z) {
    const GmLiveEnt *item;
    if (!r || slot < 0 || slot >= GM_LIVE_MAX)
        return 0;
    item = &r->entities.ents[slot];
    if (!item->active || item->type != 0)
        return 0;
    if (x) *x = item->x;
    if (y) *y = item->y + 0.2;
    if (z) *z = item->z;
    return 1;
}

static int runtime_fishing_reel_item(
        GmRuntime *r, int slot,
        double angler_x, double angler_y, double angler_z) {
    GmLiveEnt *item;
    double dx, dy, dz, distance;
    if (!r || slot < 0 || slot >= GM_LIVE_MAX)
        return 0;
    item = &r->entities.ents[slot];
    if (!item->active || item->type != 0)
        return 0;
    dx = angler_x - item->x;
    dy = angler_y - item->y;
    dz = angler_z - item->z;
    distance = sqrt(dx * dx + dy * dy + dz * dz);
    item->mx += dx * 0.1;
    item->my += dy * 0.1 + sqrt(distance) * 0.08;
    item->mz += dz * 0.1;
    return 1;
}

static int runtime_fishing_item_intercept(
        const GmRuntime *r, double sx, double sy, double sz,
        double ex, double ey, double ez,
        int *slot, double *distance_sq) {
    int best = -1;
    double best_distance = 0.0;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *item = &r->entities.ents[i];
        McAABB box;
        double hx, hy, hz, dx, dy, dz, distance;
        int side;
        if (!item->active || item->type != 0)
            continue;
        box = mc_aabb_make(
            item->x - 0.42500001192092896,
            item->y - 0.30000001192092896,
            item->z - 0.42500001192092896,
            item->x + 0.42500001192092896,
            item->y + 0.550000011920929,
            item->z + 0.42500001192092896);
        if (!pm_aabb_intercept(
                &box, sx, sy, sz, ex, ey, ez,
                &hx, &hy, &hz, &side))
            continue;
        dx = hx - sx; dy = hy - sy; dz = hz - sz;
        distance = dx * dx + dy * dy + dz * dz;
        if (best >= 0 && !(distance < best_distance))
            continue;
        best = i;
        best_distance = distance;
    }
    if (best < 0) return 0;
    if (slot) *slot = best;
    if (distance_sq) *distance_sq = best_distance;
    return 1;
}

int gm_runtime_retract_fishing_rod(GmRuntime *r) {
    GmRuntimeFishHook *hook;
    int rod_damage = 0;
    if (!r || !r->fish_hook.active) return 0;
    hook = &r->fish_hook;
    if (hook->caught_kind == 1 && gm_mobs_fishing_reel(
            &r->mobs, hook->caught_slot, r->dimension,
            r->player.ent.posX + (double)r->ox,
            r->player.ent.posY,
            r->player.ent.posZ + (double)r->oz)) {
        rod_damage = 5;
    } else if (hook->caught_kind == 2 && runtime_fishing_reel_item(
            r, hook->caught_slot,
            r->player.ent.posX + (double)r->ox,
            r->player.ent.posY,
            r->player.ent.posZ + (double)r->oz)) {
        rod_damage = 3;
    } else if (hook->catch_state.ticks_catchable > 0) {
        FishLoot loot = fish_generate_loot(
            &hook->random, (float)hook->catch_state.luck);
        double dx = r->player.ent.posX + (double)r->ox - hook->x;
        double dy = r->player.ent.posY - hook->y;
        double dz = r->player.ent.posZ + (double)r->oz - hook->z;
        double distance = sqrt(dx * dx + dy * dy + dz * dz);
        double motion_y = dy * 0.1 + sqrt(distance) * 0.08;
        int xp = jrand_int_bound(&hook->random.random, 6) + 1;
        int loot_eid = r->next_entity_id++;
        (void)gm_live_spawn_item_exact(
            &r->entities, loot_eid, hook->x, hook->y, hook->z,
            dx * 0.1, motion_y, dz * 0.1, 0.0F,
            loot.item, loot.count, loot.meta, 0, 0, 0);
        for (int i = 0; i < GM_LIVE_MAX; ++i) {
            GmLiveEnt *entity = &r->entities.ents[i];
            if (!entity->active || entity->eid != loot_eid) continue;
            entity->n_enchants = loot.n_enchants;
            for (int e = 0; e < loot.n_enchants; ++e) {
                entity->ench_id[e] = loot.enchant_id[e];
                entity->ench_lvl[e] = loot.enchant_level[e];
            }
            break;
        }
        gm_mobs_spawn_xp(
            &r->mobs, r->player.ent.posX + (double)r->ox,
            r->player.ent.posY + 0.5,
            r->player.ent.posZ + (double)r->oz + 0.5, xp);
        rod_damage = 1;
        runtime_fish_event_append(
            r, GM_FISH_EVENT_CATCH,
            loot.item, loot.count, loot.meta, xp, rod_damage);
    }
    if (hook->in_ground) rod_damage = 2;
    runtime_fish_event_append(
        r, GM_FISH_EVENT_RETRACT, 0, 0, 0, 0, rod_damage);
    (void)runtime_fishing_damage_rod(r, rod_damage);
    hook->active = 0;
    return rod_damage;
}

void gm_runtime_tick_fishing(GmRuntime *r) {
    GmRuntimeFishHook *hook;
    int x, y, z, block, water;
    double player_x, player_y, player_z;
    if (!r || !r->fish_hook.active) return;
    hook = &r->fish_hook;
    if (hook->dimension != r->dimension) return;
    player_x = r->player.ent.posX + (double)r->ox;
    player_y = r->player.ent.posY;
    player_z = r->player.ent.posZ + (double)r->oz;
    {
        ICStack main = isr_get_stack(
            &r->player.inv, r->player.inv.current_item);
        ICStack off = isr_get_stack(&r->player.inv, ISR_OFFHAND_SLOT);
        double dx = hook->x - player_x;
        double dy = hook->y - player_y;
        double dz = hook->z - player_z;
        if (r->dead || ((main.item != 346 || main.count <= 0)
                && (off.item != 346 || off.count <= 0))
                || dx * dx + dy * dy + dz * dz > 1024.0) {
            hook->active = 0;
            return;
        }
    }
    x = mc_floor(hook->x); y = mc_floor(hook->y); z = mc_floor(hook->z);
    if (hook->state == GM_FISH_STATE_HOOKED) {
        int found = hook->caught_kind == 1
            ? gm_mobs_fishing_target_position(
                &r->mobs, hook->caught_slot, r->dimension,
                &hook->x, &hook->y, &hook->z)
            : runtime_fishing_item_position(
                r, hook->caught_slot, &hook->x, &hook->y, &hook->z);
        if (found) return;
        hook->caught_eid = hook->caught_kind = 0;
        hook->caught_slot = -1;
        hook->state = GM_FISH_STATE_FLYING;
    }
    if (hook->state == GM_FISH_STATE_FLYING && hook->caught_kind) {
        hook->vx = hook->vy = hook->vz = 0.0;
        hook->state = GM_FISH_STATE_HOOKED;
        return;
    }
    block = gm_world_block(r->world, x, y, z);
    water = block == 8 || block == 9;
    if (hook->state == GM_FISH_STATE_FLYING) {
        if (water) {
            hook->vx *= 0.3;
            hook->vy *= 0.2;
            hook->vz *= 0.3;
            hook->state = GM_FISH_STATE_BOBBING;
            return;
        }
        {
            GmProjectileBlockHit hit;
            double end_x = hook->x + hook->vx;
            double end_y = hook->y + hook->vy;
            double end_z = hook->z + hook->vz;
            double block_distance = -1.0;
            int target_slot = -1, target_kind = 0;
            double target_distance = 0.0;
            if (runtime_block_hit(
                    r, hook->x, hook->y, hook->z,
                    end_x, end_y, end_z, 1, &hit)) {
                hook->in_ground = 1;
                block_distance =
                    (hit.x - hook->x) * (hit.x - hook->x)
                    + (hit.y - hook->y) * (hit.y - hook->y)
                    + (hit.z - hook->z) * (hit.z - hook->z);
            }
            if (gm_mobs_projectile_intercept(
                    &r->mobs, r->dimension, 0, 1,
                    hook->x, hook->y, hook->z, end_x, end_y, end_z,
                    &target_slot, &target_distance))
                target_kind = 1;
            {
                int item_slot;
                double item_distance;
                if (runtime_fishing_item_intercept(
                        r, hook->x, hook->y, hook->z,
                        end_x, end_y, end_z,
                        &item_slot, &item_distance)
                        && (!target_kind || item_distance < target_distance)) {
                    target_kind = 2;
                    target_slot = item_slot;
                    target_distance = item_distance;
                }
            }
            if (target_kind
                    && (block_distance < 0.0 || target_distance < block_distance)) {
                hook->caught_kind = target_kind;
                hook->caught_slot = target_slot;
                hook->caught_eid = target_kind == 2
                    ? r->entities.ents[target_slot].eid : 0;
                hook->state = GM_FISH_STATE_HOOKED;
                hook->in_ground = 0;
            }
        }
        if (!hook->in_ground) ++hook->ticks_in_air;
        else {
            hook->ticks_in_air = 0;
            hook->vx = hook->vy = hook->vz = 0.0;
        }
    } else if (hook->state == GM_FISH_STATE_BOBBING) {
        int meta = gm_world_meta(r->world, x, y, z);
        float height = ((meta & 7) == 0
                && (gm_world_block(r->world, x, y + 1, z) == 8
                    || gm_world_block(r->world, x, y + 1, z) == 9))
            ? 1.0F : 1.0F - (float)(meta + 1) / 9.0F;
        double delta;
        hook->vx *= 0.9;
        hook->vz *= 0.9;
        delta = hook->y + hook->vy - (double)y - (double)height;
        if (fabs(delta) < 0.01)
            delta += (delta > 0.0) ? 0.1 : (delta < 0.0 ? -0.1 : 0.0);
        hook->vy -= delta * (double)jrand_float(&hook->random.random) * 0.2;
        if (water && height > 0.0F) {
            int rain = gm_world_is_raining_at(
                r->world, &r->clock, x, y + 1, z);
            int sky = gm_world_precipitation_y(r->world, x, z) <= y + 1;
            hook->catch_state.motion_y = hook->vy;
            int event = fish_catch_tick(
                &hook->catch_state, &hook->random,
                rain, sky, 1, 1);
            hook->vy = hook->catch_state.motion_y;
            if (event & FISH_EVENT_BITE)
                runtime_fish_event_append(
                    r, GM_FISH_EVENT_SPLASH, 0, 0, 0, 0, 0);
            if (event & FISH_EVENT_BITE)
                runtime_sound_event_append(
                    r, GM_SOUND_BOBBER_SPLASH, GM_SOUND_CATEGORY_NEUTRAL,
                    hook->eid, 0, hook->x, hook->y, hook->z,
                    0.25F, hook->catch_state.bite_pitch);
        }
    }
    if (!water) hook->vy -= 0.03;
    hook->x += hook->vx;
    hook->y += hook->vy;
    hook->z += hook->vz;
    hook->vx *= 0.92;
    hook->vy *= 0.92;
    hook->vz *= 0.92;
    if (hook->in_ground && ++hook->ticks_in_ground >= 1200)
        hook->active = 0;
}

int gm_runtime_end_gateway_count(const GmRuntime *r) {
    return r ? r->end_gateway_count : 0;
}

int gm_runtime_end_gateway_get(
        const GmRuntime *r, int index, GmRuntimeEndGateway *out) {
    int at = 0;
    if (!r || !out || index < 0) return 0;
    for (int i = 0; i < GM_RUNTIME_END_GATEWAYS; ++i) {
        if (!r->end_gateways[i].active) continue;
        if (at++ == index) {
            *out = r->end_gateways[i];
            return 1;
        }
    }
    return 0;
}

int gm_runtime_spawn_end_gateway(
        GmRuntime *r, int x, int y, int z,
        int has_exit, int exit_x, int exit_y, int exit_z,
        int exact_teleport) {
    GmRuntimeEndGateway *gateway = NULL;
    if (!r || !r->world || r->dimension != 1 || y < 2 || y > 253
            || (has_exit != 0 && has_exit != 1)
            || (exact_teleport != 0 && exact_teleport != 1))
        return 0;
    for (int i = 0; i < GM_RUNTIME_END_GATEWAYS; ++i) {
        GmRuntimeEndGateway *candidate = &r->end_gateways[i];
        if (candidate->active && candidate->dimension == 1
                && candidate->x == x && candidate->y == y
                && candidate->z == z) {
            gateway = candidate;
            break;
        }
        if (!candidate->active && !gateway)
            gateway = candidate;
    }
    if (!gateway) return 0;
    /* Generate every potentially crossed chunk before the first edit. A raw
     * set into an ungenerated toroidal slot would otherwise be replaced by
     * that slot's initial terrain generation on the next ensure. */
    gm_world_ensure(r->world, x >> 4, z >> 4, 1);
    if (!gateway->active) ++r->end_gateway_count;
    *gateway = (GmRuntimeEndGateway){
        1, 1, x, y, z, 0, 0, has_exit, exact_teleport,
        exit_x, exit_y, exit_z
    };
    {
        GmEndBlockAccess access;
        access.ctx = r->world;
        access.get = runtime_end_population_get;
        access.set = runtime_end_population_set;
        gm_end_generate_gateway(&access, x, y, z);
    }
    return 1;
}

static int runtime_end_chunk_nonempty(GmRuntime *r, int cx, int cz) {
    gm_world_ensure(r->world, cx, cz, 0);
    for (int x = cx * 16; x < cx * 16 + 16; ++x)
        for (int z = cz * 16; z < cz * 16 + 16; ++z)
            for (int y = 127; y >= 0; --y)
                if (gm_world_block(r->world, x, y, z) != 0)
                    return 1;
    return 0;
}

static int runtime_end_find_spawnpoint(
        GmRuntime *r, int cx, int cz, int *out_x, int *out_y, int *out_z) {
    int found = 0;
    double best = 0.0;
    for (int x = cx * 16; x < cx * 16 + 16; ++x)
        for (int z = cz * 16; z < cz * 16 + 16; ++z)
            for (int y = 30; y < 256; ++y) {
                double distance;
                if (gm_world_block(r->world, x, y, z) != 121
                        || gm_block_is_normal_cube_1_11_2(
                            gm_world_block(r->world, x, y + 1, z),
                            gm_world_meta(r->world, x, y + 1, z))
                        || gm_block_is_normal_cube_1_11_2(
                            gm_world_block(r->world, x, y + 2, z),
                            gm_world_meta(r->world, x, y + 2, z)))
                    continue;
                distance = (x + 0.5) * (x + 0.5)
                    + (y + 0.5) * (y + 0.5)
                    + (z + 0.5) * (z + 0.5);
                if (!found || distance < best) {
                    found = 1;
                    best = distance;
                    *out_x = x; *out_y = y; *out_z = z;
                }
            }
    return found;
}

static void runtime_end_highest(
        GmRuntime *r, int x, int y, int z, int radius,
        int include_center, int *out_x, int *out_y, int *out_z) {
    int found = 0, best_y = 0;
    *out_x = x; *out_y = y; *out_z = z;
    for (int dx = -radius; dx <= radius; ++dx)
        for (int dz = -radius; dz <= radius; ++dz) {
            if (!include_center && dx == 0 && dz == 0) continue;
            for (int py = 255; py > (found ? best_y : 0); --py) {
                int id = gm_world_block(r->world, x + dx, py, z + dz);
                int meta = gm_world_meta(r->world, x + dx, py, z + dz);
                if (gm_block_is_normal_cube_1_11_2(id, meta)
                        && (include_center || id != 7)) {
                    found = 1; best_y = py;
                    *out_x = x + dx; *out_y = py; *out_z = z + dz;
                    break;
                }
            }
        }
}

static void runtime_end_gateway_find_exit(
        GmRuntime *r, GmRuntimeEndGateway *gateway) {
    double length = sqrt((double)gateway->x * gateway->x
        + (double)gateway->z * gateway->z);
    double unit_x = length > 0.0 ? (double)gateway->x / length : 1.0;
    double unit_z = length > 0.0 ? (double)gateway->z / length : 0.0;
    double px = unit_x * 1024.0, pz = unit_z * 1024.0;
    int cx, cz, spawn_x, spawn_y, spawn_z;
    for (int i = 16; i-- > 0;) {
        cx = mc_floor(px / 16.0); cz = mc_floor(pz / 16.0);
        if (!runtime_end_chunk_nonempty(r, cx, cz)) break;
        px -= unit_x * 16.0; pz -= unit_z * 16.0;
    }
    for (int i = 16; i-- > 0;) {
        cx = mc_floor(px / 16.0); cz = mc_floor(pz / 16.0);
        if (runtime_end_chunk_nonempty(r, cx, cz)) break;
        px += unit_x * 16.0; pz += unit_z * 16.0;
    }
    cx = mc_floor(px / 16.0); cz = mc_floor(pz / 16.0);
    if (!runtime_end_find_spawnpoint(
            r, cx, cz, &spawn_x, &spawn_y, &spawn_z)) {
        spawn_x = mc_floor(px + 0.5);
        spawn_y = 75;
        spawn_z = mc_floor(pz + 0.5);
        /* The rare all-air fallback island is intentionally compact here;
         * ordinary outer terrain finds an End-stone spawnpoint. */
        for (int dx = -4; dx <= 4; ++dx)
            for (int dz = -4; dz <= 4; ++dz)
                if (dx * dx + dz * dz <= 25)
                    gm_world_set_block(
                        r->world, spawn_x + dx, spawn_y, spawn_z + dz, 121);
    }
    runtime_end_highest(
        r, spawn_x, spawn_y, spawn_z, 16, 1,
        &spawn_x, &spawn_y, &spawn_z);
    spawn_y += 10;
    (void)gm_runtime_spawn_end_gateway(
        r, spawn_x, spawn_y, spawn_z, 1,
        gateway->x, gateway->y, gateway->z, 0);
    gateway->has_exit = 1;
    gateway->exit_x = spawn_x;
    gateway->exit_y = spawn_y;
    gateway->exit_z = spawn_z;
}

void gm_runtime_tick_end_gateways(GmRuntime *r) {
    if (!r || r->dimension != 1 || r->end_gateway_count == 0)
        return;
    for (int i = 0; i < GM_RUNTIME_END_GATEWAYS; ++i) {
        GmRuntimeEndGateway *gateway = &r->end_gateways[i];
        double px, py, pz;
        if (!gateway->active || gateway->dimension != 1) continue;
        if (gm_world_block(r->world, gateway->x, gateway->y, gateway->z)
                != 209) {
            gateway->active = 0;
            --r->end_gateway_count;
            continue;
        }
        ++gateway->age;
        if (gateway->teleport_cooldown > 0) {
            --gateway->teleport_cooldown;
            continue;
        }
        px = r->player.ent.posX + (double)r->ox;
        py = r->player.ent.posY;
        pz = r->player.ent.posZ + (double)r->oz;
        if (px + 0.3 <= gateway->x || px - 0.3 >= gateway->x + 1.0
                || py + 1.8 <= gateway->y || py >= gateway->y + 1.0
                || pz + 0.3 <= gateway->z || pz - 0.3 >= gateway->z + 1.0) {
            if (gateway->age % 2400 == 0)
                gateway->teleport_cooldown = 40;
            continue;
        }
        if (!gateway->has_exit)
            runtime_end_gateway_find_exit(r, gateway);
        if (gateway->has_exit) {
            int tx = gateway->exit_x;
            int ty = gateway->exit_y;
            int tz = gateway->exit_z;
            if (!gateway->exact_teleport) {
                runtime_end_highest(r, tx, ty, tz, 5, 0, &tx, &ty, &tz);
                ++ty;
            }
            gm_runtime_set_pose(
                r, tx + 0.5, ty + 0.5, tz + 0.5,
                r->player.yaw, r->player.pitch);
        }
        gateway->teleport_cooldown = 40;
    }
}

static int runtime_end_city_facing(int rotation) {
    static const int south_rotated[4] = {3, 4, 2, 5};
    return south_rotated[rotation & 3];
}

int gm_runtime_generate_end_city(
        GmRuntime *r, int chunk_x, int chunk_z, int start_y) {
    GmEndCity city;
    int min_x = INT_MAX, min_z = INT_MAX;
    int max_x = INT_MIN, max_z = INT_MIN;
    if (!r || !r->world || r->dimension != 1
            || start_y < 60 || start_y > 240)
        return 0;
    if (r->next_entity_id <= 0)
        r->next_entity_id = 1;
    for (int i = 0; i < r->end_city_count; ++i)
        if (r->end_cities[i].chunk_x == chunk_x
                && r->end_cities[i].chunk_z == chunk_z)
            return 1;
    if (r->end_city_count >= GM_RUNTIME_END_CITIES
            || !gm_end_city_build(r->seed, chunk_x, chunk_z, start_y, &city))
        return 0;
    for (int i = 0; i < city.count; ++i) {
        const GmEndCityPiece *piece = &city.pieces[i];
        if (piece->min_x < min_x) min_x = piece->min_x;
        if (piece->min_z < min_z) min_z = piece->min_z;
        if (piece->max_x > max_x) max_x = piece->max_x;
        if (piece->max_z > max_z) max_z = piece->max_z;
    }
    int min_cx = floordiv16(min_x), min_cz = floordiv16(min_z);
    int max_cx = floordiv16(max_x), max_cz = floordiv16(max_z);
    int center_x = (min_cx + max_cx) / 2;
    int center_z = (min_cz + max_cz) / 2;
    int radius = center_x - min_cx;
    if (max_cx - center_x > radius) radius = max_cx - center_x;
    if (center_z - min_cz > radius) radius = center_z - min_cz;
    if (max_cz - center_z > radius) radius = max_cz - center_z;
    /* The live world has a 27x27 toroidal chunk store. Vanilla End Cities fit
     * comfortably within it; reject corrupt/foreign graphs before aliasing. */
    if (radius > 12) return 0;
    gm_world_ensure(r->world, center_x, center_z, radius);
    for (int i = 0; i < city.count; ++i) {
        const GmEndCityPiece *piece = &city.pieces[i];
        const GmEcTemplate *t = &GM_EC_TEMPLATES[piece->template_index];
        for (int j = 0; j < t->block_count; ++j) {
            const GmEcBlock *block = &t->blocks[j];
            int tx, ty, tz;
            if (!piece->overwrite && block->id == 0) continue;
            gm_end_city_transform(
                piece->rotation, block->x, block->y, block->z,
                &tx, &ty, &tz);
            gm_world_load_block_meta(
                r->world, piece->x + tx, piece->y + ty, piece->z + tz,
                block->id, gm_end_city_rotate_meta(
                    block->id, block->meta, piece->rotation));
        }
    }
    /* Tile/entity markers run after all templates have placed their blocks. */
    for (int i = 0; i < city.count; ++i) {
        const GmEndCityPiece *piece = &city.pieces[i];
        const GmEcTemplate *t = &GM_EC_TEMPLATES[piece->template_index];
        for (int j = 0; j < t->marker_count; ++j) {
            const GmEcMarker *marker = &t->markers[j];
            int tx, ty, tz;
            gm_end_city_transform(
                piece->rotation, marker->x, marker->y, marker->z,
                &tx, &ty, &tz);
            int x = piece->x + tx;
            int y = piece->y + ty;
            int z = piece->z + tz;
            if (marker->kind == 1) {
                int chest = runtime_chest_ensure_tile(r, x, y - 1, z);
                if (chest >= 0) {
                    /* The table itself is oracle-exact. The monolithic live
                     * loader does not reproduce ChunkProviderEnd's resident-
                     * chunk population order, so use a stable per-site seed. */
                    long long loot_seed = r->seed
                        ^ (long long)x * 3129871LL
                        ^ (long long)z * 116129781LL
                        ^ (long long)(y - 1) * 42317861LL;
                    chest_live_set_loot(&r->chests[chest].state,
                                        CHEST_LOOT_END_CITY, loot_seed);
                }
            } else if (marker->kind == 3) {
                int facing = runtime_end_city_facing(piece->rotation);
                static const int dx[6] = {0,0,0,0,-1,1};
                static const int dz[6] = {0,0,-1,1,0,0};
                (void)gm_runtime_item_frame_set(
                    r, 1, r->next_entity_id,
                    x + 0.5 + dx[facing] * 0.46875,
                    y + 0.5, z + 0.5 + dz[facing] * 0.46875,
                    x, y, z, facing, 443, 1, 0, 0);
                ++r->next_entity_id;
            }
            /* Sentry markers require EntityShulker, still tracked separately
             * from the exact template/ship/elytra acquisition slice. */
        }
    }
    gm_world_ensure(r->world, center_x, center_z, radius);
    r->end_cities[r->end_city_count].chunk_x = chunk_x;
    r->end_cities[r->end_city_count].chunk_z = chunk_z;
    ++r->end_city_count;
    r->win_gen = -1;
    return city.count;
}

static int runtime_floor_div20(int value) {
    int quotient = value / 20;
    return value % 20 < 0 ? quotient - 1 : quotient;
}

static void runtime_ensure_nearby_end_cities(GmRuntime *r) {
    if (!r || r->dimension != 1 || !r->world
            || (r->end_city_scan_x == r->ccx
                && r->end_city_scan_z == r->ccz))
        return;
    r->end_city_scan_x = r->ccx;
    r->end_city_scan_z = r->ccz;
    int min_rx = runtime_floor_div20(r->ccx - 4);
    int max_rx = runtime_floor_div20(r->ccx + 4);
    int min_rz = runtime_floor_div20(r->ccz - 4);
    int max_rz = runtime_floor_div20(r->ccz + 4);
    for (int rx = min_rx; rx <= max_rx; ++rx) {
        for (int rz = min_rz; rz <= max_rz; ++rz) {
            int cx, cz, start_y;
            gm_end_city_candidate_for_region(r->seed, rx, rz, &cx, &cz);
            if (cx < r->ccx - 4 || cx > r->ccx + 4
                    || cz < r->ccz - 4 || cz > r->ccz + 4
                    || (long long)cx * cx + (long long)cz * cz <= 4096)
                continue;
            int known = 0;
            for (int i = 0; i < r->end_city_count; ++i)
                known |= r->end_cities[i].chunk_x == cx
                    && r->end_cities[i].chunk_z == cz;
            if (known) continue;
            gm_world_ensure(r->world, cx, cz, 0);
            JavaRandom random;
            jrand_set(&random, (long long)cx + (long long)cz * 10387313LL);
            int rotation = jrand_int_bound(&random, 4);
            int off_x = rotation == 1 || rotation == 2 ? -5 : 5;
            int off_z = rotation == 2 || rotation == 3 ? -5 : 5;
            int base_x = cx * 16, base_z = cz * 16;
            int y0 = gm_world_surface_y(r->world, base_x + 7, base_z + 7) - 1;
            int y1 = gm_world_surface_y(r->world, base_x + 7,
                                        base_z + 7 + off_z) - 1;
            int y2 = gm_world_surface_y(r->world, base_x + 7 + off_x,
                                        base_z + 7) - 1;
            int y3 = gm_world_surface_y(r->world, base_x + 7 + off_x,
                                        base_z + 7 + off_z) - 1;
            start_y = y0;
            if (y1 < start_y) start_y = y1;
            if (y2 < start_y) start_y = y2;
            if (y3 < start_y) start_y = y3;
            if (start_y >= 60)
                (void)gm_runtime_generate_end_city(r, cx, cz, start_y);
        }
    }
}

static int runtime_end_population_get(void *ctx, int x, int y, int z) {
    return gm_world_block((GmWorld *)ctx, x, y, z);
}

static void runtime_end_population_set(
        void *ctx, int x, int y, int z, int id, int meta) {
    if (y < 0 || y >= 256) return;
    gm_world_load_block_meta((GmWorld *)ctx, x, y, z, id, meta);
}

static int runtime_end_population_known(
        const GmRuntime *r, int chunk_x, int chunk_z) {
    for (int i = 0; i < r->end_population_count; ++i)
        if (r->end_population_chunks[i].chunk_x == chunk_x
                && r->end_population_chunks[i].chunk_z == chunk_z)
            return 1;
    return 0;
}

static int runtime_end_population_mark(
        GmRuntime *r, int chunk_x, int chunk_z) {
    if (runtime_end_population_known(r, chunk_x, chunk_z)) return 1;
    if (r->end_population_count == r->end_population_cap) {
        int cap = r->end_population_cap ? r->end_population_cap * 2 : 64;
        GmRuntimeEndPopulationChunk *chunks =
            (GmRuntimeEndPopulationChunk *)realloc(
                r->end_population_chunks,
                (size_t)cap * sizeof *r->end_population_chunks);
        if (!chunks) return 0;
        r->end_population_chunks = chunks;
        r->end_population_cap = cap;
    }
    r->end_population_chunks[r->end_population_count++] =
        (GmRuntimeEndPopulationChunk){chunk_x, chunk_z};
    return 1;
}

int gm_runtime_populate_end_chunk(
        GmRuntime *r, int chunk_x, int chunk_z,
        unsigned long long seed48) {
    GmEndBlockAccess access;
    JavaRandom random;
    EndNoise *noise;
    long long distance;
    float island_height;
    int base_x, base_z;
    if (!r || !r->world || r->dimension != 1
            || seed48 > GM_JAVA_RANDOM_MASK)
        return 0;
    if (!r->end_population_noise) {
        noise = (EndNoise *)malloc(sizeof *noise);
        if (!noise) return 0;
        cpe_noise_init(noise, r->seed);
        r->end_population_noise = noise;
    }
    noise = (EndNoise *)r->end_population_noise;
    gm_world_ensure(r->world, chunk_x, chunk_z, 1);
    access.ctx = r->world;
    access.get = runtime_end_population_get;
    access.set = runtime_end_population_set;
    jrand_set_seed48(&random, seed48);
    base_x = chunk_x * 16;
    base_z = chunk_z * 16;
    distance = (long long)chunk_x * chunk_x
        + (long long)chunk_z * chunk_z;
    if (distance <= 4096) return 1;
    island_height = cpe_getIslandHeightValue(
        noise, chunk_x, chunk_z, 1, 1);
    if (island_height < -20.0f && jrand_int_bound(&random, 14) == 0) {
        int x = base_x + jrand_int_bound(&random, 16) + 8;
        int y = 55 + jrand_int_bound(&random, 16);
        int z = base_z + jrand_int_bound(&random, 16) + 8;
        gm_end_generate_island(&access, x, y, z, &random);
        if (jrand_int_bound(&random, 4) == 0) {
            x = base_x + jrand_int_bound(&random, 16) + 8;
            y = 55 + jrand_int_bound(&random, 16);
            z = base_z + jrand_int_bound(&random, 16) + 8;
            gm_end_generate_island(&access, x, y, z, &random);
        }
    }
    if (cpe_getIslandHeightValue(noise, chunk_x, chunk_z, 1, 1) > 40.0f) {
        int plants = jrand_int_bound(&random, 5);
        for (int i = 0; i < plants; ++i) {
            int x = base_x + jrand_int_bound(&random, 16) + 8;
            int z = base_z + jrand_int_bound(&random, 16) + 8;
            int y = gm_world_surface_y(r->world, x, z);
            if (y > 0 && gm_world_block(r->world, x, y, z) == 0
                    && gm_world_block(r->world, x, y - 1, z) == 121)
                gm_end_generate_chorus(&access, x, y, z, &random, 8);
        }
        if (jrand_int_bound(&random, 700) == 0) {
            int x = base_x + jrand_int_bound(&random, 16) + 8;
            int z = base_z + jrand_int_bound(&random, 16) + 8;
            int y = gm_world_surface_y(r->world, x, z);
            if (y > 0) {
                y += 3 + jrand_int_bound(&random, 7);
                (void)gm_runtime_spawn_end_gateway(
                    r, x, y, z, 1, 100, 50, 0, 0);
            }
        }
    }
    r->win_gen = -1;
    return 1;
}

static void runtime_ensure_nearby_end_population(GmRuntime *r) {
    if (!r || r->dimension != 1 || !r->world
            || (r->end_population_scan_x == r->ccx
                && r->end_population_scan_z == r->ccz))
        return;
    r->end_population_scan_x = r->ccx;
    r->end_population_scan_z = r->ccz;
    /* A fresh Chunk.populateChunk normally becomes eligible when its southeast
     * neighbour has just been provided; ChunkProviderEnd.rand therefore holds
     * that neighbour's provideChunk seed. This is the canonical exploration
     * order used by the standalone streamer. Explicit replay can inject any
     * observed cursor through gm_runtime_populate_end_chunk. */
    for (int cx = r->ccx - 2; cx <= r->ccx + 2; ++cx)
        for (int cz = r->ccz - 2; cz <= r->ccz + 2; ++cz) {
            JavaRandom random;
            if (runtime_end_population_known(r, cx, cz)) continue;
            jrand_set(&random,
                (long long)(cx + 1) * 341873128712LL
                + (long long)(cz + 1) * 132897987541LL);
            if (!runtime_end_population_mark(r, cx, cz)) return;
            (void)gm_runtime_populate_end_chunk(r, cx, cz, random.seed);
        }
}

static void runtime_spawn_dragon_gateway(GmRuntime *r) {
    int index, x, z;
    if (!r || r->dimension != 1 || r->end_gateway_order_count <= 0)
        return;
    index = r->end_gateway_order[--r->end_gateway_order_count];
    x = (int)(96.0 * cos(2.0 * (-MC_PI + 0.15707963267948966 * index)));
    z = (int)(96.0 * sin(2.0 * (-MC_PI + 0.15707963267948966 * index)));
    runtime_world_event_append(r, 3000, x, 75, z, 0);
    (void)gm_runtime_spawn_end_gateway(r, x, 75, z, 0, 0, 0, 0, 0);
}

int gm_runtime_weather_ice_snow_at(
        GmRuntime *r, int x, int z, int raining) {
    int changed = 0;
    int py, below;
    if (!r || !r->world || r->dimension != 0)
        return 0;
    py = gm_world_precipitation_y(r->world, x, z);
    below = py - 1;
    if (gm_world_can_freeze(r->world, x, below, z, 1)) {
        gm_world_set_block_meta(r->world, x, below, z, 79, 0);
        changed |= 1;
    }
    if (raining && gm_world_can_snow(r->world, x, py, z, 1)) {
        gm_world_set_block_meta(r->world, x, py, z, 78, 0);
        changed |= 2;
    }
    /* BlockCauldron.fillWithRain is the only vanilla 1.11.2 block override.
     * It runs after freeze/snow and consumes World.rand only in rain biomes. */
    if (raining
            && gm_world_precipitation_kind(r->world, x, below, z) == 1
            && gm_world_block(r->world, x, below, z) == 118
            && runtime_java_random_next_int(r, 20) == 1
            && gm_world_temperature(r->world, x, below, z) >= 0.15f) {
        int level = gm_world_meta(r->world, x, below, z) & 3;
        if (level < 3) {
            gm_world_set_block_meta(r->world, x, below, z, 118, level + 1);
            changed |= 4;
        }
    }
    return changed;
}

int gm_runtime_weather_chunk_tick(GmRuntime *r, int cx, int cz) {
    int changed = 0;
    int raining;
    if (!r || !r->world || !r->weather_enabled || r->dimension != 0)
        return 0;
    raining = gm_world_rain_strength(&r->clock, 1.0f) > 0.2f;
    if (raining && gm_world_thunder_strength(&r->clock, 1.0f) > 0.9f
            && runtime_java_random_next_int(r, 100000) == 0) {
        uint32_t selected = runtime_world_update_lcg_step(r) >> 2;
        int x = cx * 16 + (int)(selected & 15);
        int z = cz * 16 + (int)((selected >> 8) & 15);
        int y = gm_world_precipitation_y(r->world, x, z);
        /* adjustPosToNearbyEntity's empty-list branch is the ordinary case.
         * Loaded living-target selection and skeleton traps remain isolated
         * from this exact chunk/column weather body. */
        if (gm_world_is_raining_at(r->world, &r->clock, x, y, z))
            (void)gm_runtime_spawn_lightning(r, x, y, z, 0);
    }
    /* Ice/snow always performs its independent nextInt(16), including after
     * a successful thunder roll. */
    if (runtime_java_random_next_int(r, 16) == 0) {
        uint32_t selected = runtime_world_update_lcg_step(r) >> 2;
        int x = cx * 16 + (int)(selected & 15);
        int z = cz * 16 + (int)((selected >> 8) & 15);
        changed = gm_runtime_weather_ice_snow_at(r, x, z, raining);
    }
    return changed;
}

static void runtime_tick_weather_chunks(GmRuntime *r) {
    int radius;
    if (!r || !r->weather_enabled || !r->weather_blocks_enabled
            || r->dimension != 0)
        return;
    radius = r->view_distance;
    if (radius < 1) radius = 1;
    if (radius > 8) radius = 8;
    gm_world_ensure(r->world, r->ccx, r->ccz, radius);
    for (int cx = r->ccx - radius; cx <= r->ccx + radius; ++cx)
        for (int cz = r->ccz - radius; cz <= r->ccz + radius; ++cz)
            (void)gm_runtime_weather_chunk_tick(r, cx, cz);
}

static float runtime_crop_growth_chance(
        const GmRuntime *r, int x, int y, int z, int crop_block) {
    float chance = 1.0f;
    for (int dx = -1; dx <= 1; ++dx) {
        for (int dz = -1; dz <= 1; ++dz) {
            float soil = 0.0f;
            if (gm_world_block(r->world, x + dx, y - 1, z + dz) == 60) {
                soil = gm_world_meta(
                    r->world, x + dx, y - 1, z + dz) > 0
                    ? 3.0f : 1.0f;
            }
            if (dx != 0 || dz != 0)
                soil /= 4.0f;
            chance += soil;
        }
    }
    int east_west =
        gm_world_block(r->world, x - 1, y, z) == crop_block
        || gm_world_block(r->world, x + 1, y, z) == crop_block;
    int north_south =
        gm_world_block(r->world, x, y, z - 1) == crop_block
        || gm_world_block(r->world, x, y, z + 1) == crop_block;
    if (east_west && north_south) {
        chance /= 2.0f;
    } else if (
            gm_world_block(r->world, x - 1, y, z - 1) == crop_block
            || gm_world_block(r->world, x + 1, y, z - 1) == crop_block
            || gm_world_block(r->world, x + 1, y, z + 1) == crop_block
            || gm_world_block(r->world, x - 1, y, z + 1) == crop_block) {
        chance /= 2.0f;
    }
    return chance;
}

int gm_runtime_init(GmRuntime *r, const GmConfig *cfg, char *err, int err_cap) {
    if (!r || !cfg) { set_error(err, err_cap, "invalid runtime arguments"); return 0; }
    memset(r, 0, sizeof *r);
    r->tape_armor_points = -1;   /* no recorded armor total until a tape sets it */
    r->haste_amplifier = -1;
    r->fatigue_amplifier = -1;
    r->resistance_amplifier = -1;
    r->player_attack_speed_multiplier = 1.0;
    popmc_set_villages(cfg->villages && cfg->world == GM_WORLD_DEFAULT);
    r->world = gm_world_create_type(cfg->seed, (int)cfg->world);
    if (!r->world) { set_error(err, err_cap, "gm_world_create failed"); return 0; }
    r->window = (Chunk *)calloc(PSV_NCHUNKS, sizeof(Chunk));
    if (!r->window) {
        gm_world_destroy(r->world); r->world = NULL;
        set_error(err, err_cap, "physics window allocation failed"); return 0;
    }
    mc_sin_table_init(&r->sin_table);
    r->worlds[1]=r->world;r->dimension=0;r->world_type=cfg->world;r->seed=cfg->seed;
    r->ccx = r->ccz = 0; r->ox = r->oz = 0;
    gm_world_ensure(r->world, 0, 0, 2);
    int surface = gm_world_surface_y(r->world, 8, 8);
    psv_player_init(&r->player);
    isr_init(&r->player.inv);
    r->player.inv.current_item = 0;
    gm_player_cursor_set(ic_empty());
    r->player.ent.posX = 8.5;
    r->player.ent.posY = (double)surface + 1.0;
    r->player.ent.posZ = 8.5;
    r->player.ent.box = psv_player_box(8.5, r->player.ent.posY, 8.5);
    r->player.ent.onGround = 0;
    r->player.yaw = 180.0f; r->player.pitch = 0.0f;
    r->player_air = 300;
    r->player_fire_ticks = -20;
    r->player_position_update_ticks = 0;
    r->player_position_packet_pending = 0;
    r->world_random_seed48 =
        ((uint64_t)cfg->seed ^ GM_JAVA_RANDOM_MULT) & GM_JAVA_RANDOM_MASK;
    r->end_city_scan_x = INT_MIN;
    r->end_city_scan_z = INT_MIN;
    r->end_population_scan_x = INT_MIN;
    r->end_population_scan_z = INT_MIN;
    r->villages_enabled = cfg->villages && cfg->world == GM_WORLD_DEFAULT;
    r->village_scan_x = INT_MIN;
    r->village_scan_z = INT_MIN;
    r->village_scan_builds = -1;
    r->math_random_seed48 =
        ((uint64_t)cfg->seed ^ UINT64_C(0x9E3779B97F4A)
            ^ GM_JAVA_RANDOM_MULT) & GM_JAVA_RANDOM_MASK;
    /* A standalone Magma world has no JVM class-load clock from which to
     * derive Block.RANDOM. Keep it deterministic; exact Java replay capsules
     * replace this cursor before the first simulated tick. */
    r->block_random_seed48 =
        ((uint64_t)cfg->seed ^ UINT64_C(0xD1B54A32D192)
            ^ GM_JAVA_RANDOM_MULT) & GM_JAVA_RANDOM_MASK;
    /* Client World.rand is a distinct stream. Standalone play seeds it
     * deterministically; an oracle capsule may replace it before dispatch. */
    r->sound_random_seed48 =
        ((uint64_t)cfg->seed ^ UINT64_C(0xA24BAED4963E)
            ^ GM_JAVA_RANDOM_MULT) & GM_JAVA_RANDOM_MASK;
    {
        JavaRandom gateway_random;
        jrand_set(&gateway_random, (i64)cfg->seed);
        r->end_gateway_order_count = 20;
        for (int i = 0; i < 20; ++i)
            r->end_gateway_order[i] = i;
        /* Collections.shuffle(list, new Random(worldSeed)). */
        for (int i = 20; i > 1; --i) {
            int j = jrand_int_bound(&gateway_random, i);
            int value = r->end_gateway_order[i - 1];
            r->end_gateway_order[i - 1] = r->end_gateway_order[j];
            r->end_gateway_order[j] = value;
        }
    }
    pv_init(&r->vitals);
    r->gamerules = mc_gamerules_default();
    r->gamerules.doDaylightCycle = cfg->daylight;
    runtime_sync_server_player(r);
    gm_world_clock_init(&r->clock, cfg->seed);
    memset(&r->entities, 0, sizeof r->entities);
    gm_mobs_init(&r->mobs, cfg->seed);
    gm_fluid_init(&r->fluids);
    r->weather_enabled = cfg->weather;
    r->clock.freeze_daylight = !r->gamerules.doDaylightCycle;
    r->mobs_enabled = cfg->mobs;
    /* Live random ticks on by default; script.c clears this for tape replay. */
    r->randtick_enabled = 1;
    r->randtick_radius = cfg->view_distance > 0 ? cfg->view_distance : 2;
    r->active_furnace = -1;
    r->active_chest = -1;
    r->tape_boat_ride_id = -1;
    r->tape_boat_mount_pending = -1;
    r->weather_blocks_enabled = cfg->weather;
    r->view_distance = cfg->view_distance;
    r->brewing_enabled = cfg->brewing;
    r->enchanting_enabled = cfg->enchanting;
    r->player_xp_seed = 0;
    r->player_xp_level = -1;
    r->player_xp_frac = -1.0f;
    r->player_xp_total = 0;
    enchanting_live_init(&r->enchanting);
    r->do_fire_tick = 1;
    r->do_entity_drops = 1;
    r->do_mob_loot = 1;
    r->falling_instant = 0;
    r->mob_griefing = cfg->mob_griefing;
    r->active_static_container = -1;
    r->chests_cap = GM_RUNTIME_CHESTS_INITIAL;
    r->chests = (GmRuntimeChest *)calloc((size_t)r->chests_cap, sizeof(GmRuntimeChest));
    if (!r->chests) {
        free(r->window);
        for (int i = 0; i < 3; ++i) if (r->worlds[i]) gm_world_destroy(r->worlds[i]);
        set_error(err, err_cap, "chest TE storage allocation failed");
        return 0;
    }
    r->scheduled_ticks = (GmRuntimeScheduledTick *)calloc(
        GM_RUNTIME_SCHEDULED_TICKS, sizeof *r->scheduled_ticks);
    if (!r->scheduled_ticks) {
        free(r->chests);
        free(r->window);
        for (int i = 0; i < 3; ++i)
            if (r->worlds[i]) gm_world_destroy(r->worlds[i]);
        r->chests = NULL;
        r->window = NULL;
        set_error(err, err_cap, "scheduled-tick storage allocation failed");
        return 0;
    }
    for (int i = 0; i < 9; ++i) r->craft_grid[i] = ic_empty();
    return 1;
}

void gm_runtime_destroy(GmRuntime *r) {
    if (!r) return;
    free(r->window);
    free(r->chests);
    for (int i = 0; i < r->static_containers_cap; ++i)
        gm_nbt_blob_clear(&r->static_containers[i].item_tag);
    free(r->static_containers);
    free(r->command_blocks);
    free(r->flower_pots);
    for (int i = 0; i < r->skulls_cap; ++i)
        gm_nbt_blob_clear(&r->skulls[i].owner_profile);
    for (int i = 0; i < r->tagged_items_cap; ++i)
        gm_nbt_blob_clear(&r->tagged_items[i].tag);
    free(r->skulls);
    free(r->tagged_items);
    free(r->item_frames);
    free(r->scheduled_ticks);
    free(r->redstone_torch_toggles);
    free(r->end_population_chunks);
    free(r->end_population_noise);
    r->chests = NULL;
    r->static_containers = NULL;
    r->command_blocks = NULL;
    r->flower_pots = NULL;
    r->skulls = NULL;
    r->tagged_items = NULL;
    r->item_frames = NULL;
    r->scheduled_ticks = NULL;
    r->redstone_torch_toggles = NULL;
    r->end_population_chunks = NULL;
    r->end_population_noise = NULL;
    r->chests_cap = 0;
    r->static_containers_cap = 0;
    r->command_blocks_cap = 0;
    r->flower_pots_cap = 0;
    r->skulls_cap = 0;
    r->tagged_items_cap = 0;
    r->item_frames_cap = 0;
    for(int i=0;i<3;++i)if(r->worlds[i])gm_world_destroy(r->worlds[i]);
    memset(r, 0, sizeof *r);
}

void gm_runtime_respawn(GmRuntime *r) {
    if (!r) return;
    r->dead = 0;
    r->death_screen_ticks = 0;
    r->player_death_time = 0;
    r->quit_to_title = 0;
    r->vitals.health = 20.0f;
    r->player.health = 20.0f;
    r->player_air = 300;
    r->player_fire_ticks = -20;
    r->player_position_update_ticks = 0;
    r->player_position_packet_pending = 0;
    r->mobs.player_hurt_resistant = 0;
    r->mobs.player_hurt_time = 0;
    r->mobs.player_last_damage = 0.0f;
    runtime_sync_server_player(r);
}

static void runtime_refresh_potion_attributes(GmRuntime *r) {
    double movement = 1.0;
    int levitation_amplifier = -1;
    int jump_boost_amplifier = -1;
    int haste_amplifier = -1;
    int fatigue_amplifier = -1;
    int resistance_amplifier = -1;
    int blindness = 0;
    double attack_speed = 1.0;
    float max_health = 20.0f;
    for (int i = 0; i < r->potion_count; ++i) {
        const GmPotionEffectView *effect = &r->potions[i];
        if (effect->id == 1) {
            /* MobEffects.SPEED: amount 0.20000000298023224, operation 2.
             * Potion scales the modifier by amplifier+1 before
             * ModifiableAttributeInstance applies the multiplicative product. */
            movement *= 1.0
                + 0.20000000298023224 * (double)(effect->amplifier + 1);
        } else if (effect->id == 2) {
            /* MobEffects.SLOWNESS: amount -0.15000000596046448, operation 2. */
            movement *= 1.0
                - 0.15000000596046448 * (double)(effect->amplifier + 1);
        } else if (effect->id == 25) {
            levitation_amplifier = effect->amplifier;
        } else if (effect->id == 8) {
            jump_boost_amplifier = effect->amplifier;
        } else if (effect->id == 3) {
            haste_amplifier = effect->amplifier;
            attack_speed *= 1.0
                + 0.10000000149011612 * (double)(effect->amplifier + 1);
        } else if (effect->id == 4) {
            fatigue_amplifier = effect->amplifier;
            attack_speed *= 1.0
                - 0.10000000149011612 * (double)(effect->amplifier + 1);
        } else if (effect->id == 11) {
            resistance_amplifier = effect->amplifier;
        } else if (effect->id == 15) {
            blindness = 1;
        } else if (effect->id == 21) {
            max_health += 4.0f * (float)(effect->amplifier + 1);
        }
    }
    if (movement < 0.0) movement = 0.0;
    r->player.movement_speed_multiplier = movement;
    r->server_player.movement_speed_multiplier = movement;
    r->player.levitation_amplifier = levitation_amplifier;
    r->server_player.levitation_amplifier = levitation_amplifier;
    r->player.jump_boost_amplifier = jump_boost_amplifier;
    r->server_player.jump_boost_amplifier = jump_boost_amplifier;
    r->player.blindness = blindness;
    r->server_player.blindness = blindness;
    if (attack_speed < 0.0) attack_speed = 0.0;
    r->haste_amplifier = haste_amplifier;
    r->fatigue_amplifier = fatigue_amplifier;
    r->resistance_amplifier = resistance_amplifier;
    r->mobs.player_resistance_amplifier = resistance_amplifier;
    r->vitals.maxHealth = max_health;
    if (r->vitals.health > max_health)
        pv_set_health(&r->vitals, max_health);
    r->player.health = r->vitals.health;
    r->server_player.health = r->vitals.health;
    r->player_attack_speed_multiplier = attack_speed;
}

static void runtime_tick_potions(GmRuntime *r) {
    int out = 0;
    int before = r->potion_count;
    for (int i = 0; i < r->potion_count; ++i) {
        GmPotionEffectView effect = r->potions[i];
        /* PotionEffect.onUpdate performs its periodic action first, then
         * decrements duration and returns false at zero. The authoritative
         * list therefore omits a duration-zero effect in this tick's view.
         * The locked oracle mirrors the parked server property state before
         * the next client movement tick, so an expiring movement modifier is
         * removed before that tick's travel as well. */
        if (effect.duration > 0 && effect.id == 17) {
            /* MobEffects.HUNGER isReady is true every tick. Potion uses a
             * float multiply before EntityPlayer.addExhaustion. */
            pv_add_exhaustion(
                &r->vitals, 0.005f * (float)(effect.amplifier + 1));
        } else if (effect.duration > 0 && effect.id == 19) {
            /* MobEffects.POISON: isReady uses duration modulo
             * (25 >> amplifier), with intervals <= 0 ready every tick. It
             * cannot reduce a living entity below one health point. */
            int interval = 25 >> (effect.amplifier & 31);
            int ready = interval > 0
                ? effect.duration % interval == 0
                : 1;
            if (ready && r->vitals.health > 1.0f) {
                (void)gm_mobs_attack_player(
                    &r->mobs, (struct PvStats *)&r->vitals,
                    &r->player.inv, 1.0f, 1);
                r->player.health = r->vitals.health;
                r->server_player.health = r->vitals.health;
            }
        } else if (effect.duration > 0 && effect.id == 10) {
            /* MobEffects.REGENERATION uses duration modulo
             * (50 >> amplifier), with intervals <= 0 ready every tick. */
            int interval = 50 >> (effect.amplifier & 31);
            int ready = interval > 0
                ? effect.duration % interval == 0
                : 1;
            if (ready && r->vitals.health < r->vitals.maxHealth) {
                pv_heal(&r->vitals, 1.0f);
                r->player.health = r->vitals.health;
                r->server_player.health = r->vitals.health;
            }
        } else if (effect.duration > 0 && effect.id == 20) {
            /* MobEffects.WITHER uses duration modulo
             * (40 >> amplifier), with intervals <= 0 ready every tick. */
            int interval = 40 >> (effect.amplifier & 31);
            int ready = interval > 0
                ? effect.duration % interval == 0
                : 1;
            if (ready) {
                (void)gm_mobs_attack_player(
                    &r->mobs, (struct PvStats *)&r->vitals,
                    &r->player.inv, 1.0f, 1);
                r->player.health = r->vitals.health;
                r->server_player.health = r->vitals.health;
            }
        }
        if (effect.duration > 0) --effect.duration;
        if (effect.duration > 0) {
            r->potions[out++] = effect;
        } else if (effect.id == 22) {
            r->mobs.player_absorption -=
                4.0f * (float)(effect.amplifier + 1);
            if (r->mobs.player_absorption < 0.0f)
                r->mobs.player_absorption = 0.0f;
        }
    }
    r->potion_count = out;
    if (out != before)
        runtime_refresh_potion_attributes(r);
}

void gm_runtime_potions_clear(GmRuntime *r) {
    if (!r) return;
    for (int i = 0; i < r->potion_count; ++i)
        if (r->potions[i].id == 22)
            r->mobs.player_absorption -=
                4.0f * (float)(r->potions[i].amplifier + 1);
    if (r->mobs.player_absorption < 0.0f)
        r->mobs.player_absorption = 0.0f;
    r->potion_count = 0;
    runtime_refresh_potion_attributes(r);
}

int gm_runtime_potion_add(
        GmRuntime *r, int id, int amplifier, int duration) {
    if (!r || id < 1 || id > 255 || amplifier < 0 || amplifier > 255
            || duration <= 0 || r->potion_count >= GM_MAX_POTION_EFFECTS)
        return 0;
    GmPotionEffectView *effect = &r->potions[r->potion_count++];
    effect->id = id;
    effect->amplifier = amplifier;
    effect->duration = duration;
    if (id == 22)
        r->mobs.player_absorption += 4.0f * (float)(amplifier + 1);
    runtime_refresh_potion_attributes(r);
    return 1;
}

static void runtime_potion_combine(
        GmRuntime *r,int id,int amplifier,int duration) {
    for(int i=0;i<r->potion_count;++i) {
        GmPotionEffectView *effect=&r->potions[i];
        if(effect->id!=id)continue;
        if(amplifier>effect->amplifier) {
            effect->amplifier=amplifier;
            effect->duration=duration;
        } else if(amplifier==effect->amplifier
                && duration>effect->duration) {
            effect->duration=duration;
        }
        runtime_refresh_potion_attributes(r);
        return;
    }
    (void)gm_runtime_potion_add(r,id,amplifier,duration);
}

/* PotionType.registerPotionTypes, registry ids shared with tile_entity_brewing. */
static int runtime_potion_type_effect(
        int type, int *id, int *amplifier, int *duration) {
    *id=0;*amplifier=0;*duration=0;
    switch(type) {
    case TB_PT_NIGHT_VISION:*id=16;*duration=3600;break;
    case TB_PT_LONG_NIGHT_VISION:*id=16;*duration=9600;break;
    case TB_PT_INVISIBILITY:*id=14;*duration=3600;break;
    case TB_PT_LONG_INVISIBILITY:*id=14;*duration=9600;break;
    case TB_PT_LEAPING:*id=8;*duration=3600;break;
    case TB_PT_LONG_LEAPING:*id=8;*duration=9600;break;
    case TB_PT_STRONG_LEAPING:*id=8;*duration=1800;*amplifier=1;break;
    case TB_PT_FIRE_RESISTANCE:*id=12;*duration=3600;break;
    case TB_PT_LONG_FIRE_RESISTANCE:*id=12;*duration=9600;break;
    case TB_PT_SWIFTNESS:*id=1;*duration=3600;break;
    case TB_PT_LONG_SWIFTNESS:*id=1;*duration=9600;break;
    case TB_PT_STRONG_SWIFTNESS:*id=1;*duration=1800;*amplifier=1;break;
    case TB_PT_SLOWNESS:*id=2;*duration=1800;break;
    case TB_PT_LONG_SLOWNESS:*id=2;*duration=4800;break;
    case TB_PT_WATER_BREATHING:*id=13;*duration=3600;break;
    case TB_PT_LONG_WATER_BREATHING:*id=13;*duration=9600;break;
    case TB_PT_HEALING:*id=6;*duration=1;break;
    case TB_PT_STRONG_HEALING:*id=6;*duration=1;*amplifier=1;break;
    case TB_PT_HARMING:*id=7;*duration=1;break;
    case TB_PT_STRONG_HARMING:*id=7;*duration=1;*amplifier=1;break;
    case TB_PT_POISON:*id=19;*duration=900;break;
    case TB_PT_LONG_POISON:*id=19;*duration=1800;break;
    case TB_PT_STRONG_POISON:*id=19;*duration=432;*amplifier=1;break;
    case TB_PT_REGENERATION:*id=10;*duration=900;break;
    case TB_PT_LONG_REGENERATION:*id=10;*duration=1800;break;
    case TB_PT_STRONG_REGENERATION:*id=10;*duration=450;*amplifier=1;break;
    case TB_PT_STRENGTH:*id=5;*duration=3600;break;
    case TB_PT_LONG_STRENGTH:*id=5;*duration=9600;break;
    case TB_PT_STRONG_STRENGTH:*id=5;*duration=1800;*amplifier=1;break;
    case TB_PT_WEAKNESS:*id=18;*duration=1800;break;
    case TB_PT_LONG_WEAKNESS:*id=18;*duration=4800;break;
    case TB_PT_LUCK:*id=26;*duration=6000;break;
    default:return 0;
    }
    return 1;
}

static void runtime_apply_potion_type(
        GmRuntime *r, int type, double instant_factor,
        double duration_factor, int minimum_duration) {
    int id=0,amplifier=0,duration=0;
    if(!runtime_potion_type_effect(type,&id,&amplifier,&duration))return;
    if(id==6) {
        int amount=(int)(instant_factor*(double)(4<<amplifier)+0.5);
        if(amount>0)pv_heal(&r->vitals,(float)amount);
    } else if(id==7) {
        int amount=(int)(instant_factor*(double)(6<<amplifier)+0.5);
        if(amount>0)
        (void)gm_mobs_attack_player(
            &r->mobs,(struct PvStats *)&r->vitals,
            &r->player.inv,(float)amount,1);
    } else {
        duration=(int)(duration_factor*(double)duration+0.5);
        if(duration>=minimum_duration)
            runtime_potion_combine(r,id,amplifier,duration);
    }
    r->player.health=r->vitals.health;
    r->server_player.health=r->vitals.health;
}

/* A drink is applied after this tick's existing active-effect pass, matching
 * EntityLivingBase.onEntityUpdate -> onLivingUpdate item completion order. */
static void runtime_finish_potion_drink(GmRuntime *r,int type) {
    runtime_apply_potion_type(r,type,1.0,1.0,1);
}

static int runtime_xp_bar_cap(int level) {
    return level >= 30 ? 112 + (level - 30) * 9
        : level >= 15 ? 37 + (level - 15) * 5
        : 7 + level * 2;
}

static void runtime_apply_collected_xp(GmRuntime *r, int previous_total) {
    if (!r || r->player_xp_level < 0 || r->mobs.xp_total <= previous_total)
        return;
    int amount = r->mobs.xp_total - previous_total;
    if (amount > INT_MAX - r->player_xp_total)
        amount = INT_MAX - r->player_xp_total;
    int cap = runtime_xp_bar_cap(r->player_xp_level);
    r->player_xp_frac += (float)amount / (float)cap;
    r->player_xp_total += amount;
    while (r->player_xp_frac >= 1.0f) {
        r->player_xp_frac =
            (r->player_xp_frac - 1.0f) * (float)cap;
        ++r->player_xp_level;
        cap = runtime_xp_bar_cap(r->player_xp_level);
        r->player_xp_frac /= (float)cap;
    }
}

void gm_runtime_tick(GmRuntime *r, GmAction action) {
    int late_block_use_swing = 0;
    int queued_shear = 0, queued_shear_eid = 0, queued_shear_hand = 0;
    /* server_feed_animal_pending is a compact interaction kind:
     * 1 breeding food, 2 cow milk, 3 pig saddle, 4 pig mount. */
    int queued_feed_animal = 0;
    int queued_feed_animal_eid = 0, queued_feed_animal_hand = 0;
    int queued_animal_preempt = 0;
    int queued_pig_boost = 0, queued_pig_boost_hand = 0;
    if (!r || !r->world || r->won) return;
    r->te_x = r->player.ent.posX + (double)r->ox;
    r->te_y = r->player.ent.posY;
    r->te_z = r->player.ent.posZ + (double)r->oz;
    r->te_valid = 1;
    /* GuiGameOver is open: advance enableButtonsTimer and handle button clicks.
     * World/player physics stay frozen (vanilla doesGuiPauseGame=false but the
     * player entity is already dead; magma freezes the survival transition). */
    if (r->dead) {
        if (r->death_screen_ticks < 1000000)
            ++r->death_screen_ticks;
        if (r->player_death_time < 20)
            ++r->player_death_time;
        /* enableButtonsTimer == 20 unlocks buttons (GuiGameOver.updateScreen). */
        if (action.death_click && r->death_screen_ticks >= 20) {
            if (action.death_button == 0) {
                gm_runtime_respawn(r);
            } else if (action.death_button == 1) {
                /* Title Screen: product has no main menu - end the episode. */
                r->quit_to_title = 1;
            }
        }
        return;
    }
    recenter(r);
    /* The recorded client sees the server's mount/dismount relationship on
     * the tick after right-click/sneak. ent_view has already supplied this
     * tick's authoritative boat pose before gm_runtime_tick is called. */
    if (r->tape_boat_dismount_pending) {
        r->tape_boat_ride_id = -1;
        r->tape_boat_mount_pending = -1;
        r->tape_boat_dismount_pending = 0;
    } else if (r->tape_boat_mount_pending >= 0 &&
               r->tape_boat.valid &&
               r->tape_boat.ent_id == r->tape_boat_mount_pending) {
        r->tape_boat_ride_id = r->tape_boat_mount_pending;
        r->tape_boat_mount_pending = -1;
        r->tape_boat_mount_message_ticks = 60;
    }
    (void)gm_runtime_sync_village_residents(r);
    runtime_ensure_nearby_end_population(r);
    runtime_ensure_nearby_end_cities(r);
    ICStack held_now=isr_get_stack(&r->player.inv,r->player.inv.current_item);
    if (action.do_place && held_now.item == 346 && held_now.count > 0) {
        if (r->fish_hook.active) {
            (void)gm_runtime_retract_fishing_rod(r);
        } else {
            int lure = 0, luck = 0;
            for (int e = 0; e < held_now.n_enchants; ++e) {
                if (held_now.enchants[e].id == 62)
                    lure = held_now.enchants[e].level;
                else if (held_now.enchants[e].id == 61)
                    luck = held_now.enchants[e].level;
            }
            (void)gm_runtime_cast_fishing_rod(r, lure, luck);
        }
        gm_mobs_player_swing(&r->mobs);
        action.do_place = 0;
        action.use = 0;
    }
    if (action.do_place && held_now.item == 401 && held_now.count > 0) {
        int spawned = -1;
        int flight = ic_firework_flight(&held_now);
        int explosions = ic_firework_explosions(&held_now);
        int large = ic_firework_large(&held_now);
        int flicker = ic_firework_flicker(&held_now);
        if (r->player.elytra_flying) {
            spawned = gm_runtime_spawn_firework_payload(
                r, r->player.ent.posX + (double)r->ox,
                r->player.ent.posY,
                r->player.ent.posZ + (double)r->oz,
                flight, explosions, large, flicker, 1);
        } else {
            int hx, hy, hz, ax, ay, az;
            if (gm_raycast_sel_reach(
                    r->window, &r->sin_table, &r->player, PSV_REACH,
                    &hx, &hy, &hz, &ax, &ay, &az) >= 0) {
                double x = hx + (ax > hx ? 1.0 : ax < hx ? 0.0 : 0.5);
                double y = hy + (ay > hy ? 1.0 : ay < hy ? 0.0 : 0.5);
                double z = hz + (az > hz ? 1.0 : az < hz ? 0.0 : 0.5);
                spawned = gm_runtime_spawn_firework_payload(
                    r, x, y, z,
                    flight, explosions, large, flicker, 0);
            }
        }
        if (spawned >= 0) {
            (void)isr_decr_stack_size(
                &r->player.inv, r->player.inv.current_item, 1);
            action.do_place = 0;
            action.use = 0;
        }
    }
    if(action.do_place
            &&(held_now.item==TB_SPLASH_POTION
                ||held_now.item==TB_LINGERING_POTION)
            &&runtime_throw_potion(r,held_now)){
        action.do_place=0;action.use=0;
    }
    if(held_now.item==261&&action.use){r->bow_drawing=1;++r->bow_ticks;}
    else if(r->bow_drawing){spawn_bow_arrow(r,r->bow_ticks);r->bow_drawing=0;r->bow_ticks=0;}
    if(action.do_place&&held_now.item==381&&throw_eye_of_ender(r)){
        action.do_place=0;action.use=0;
    }
    /* ItemBoat: place on water/adjacent block (oak boat item id 333). */
    if(action.do_place&&held_now.item==333){
        int hx,hy,hz,ax,ay,az;
        if(gm_raycast_sel_reach(r->window,&r->sin_table,&r->player,PSV_REACH,
                                &hx,&hy,&hz,&ax,&ay,&az)>=0){
            double bx=ax+r->ox+0.5,by=ay+0.1,bz=az+r->oz+0.5;
            if(gm_mobs_place_boat(&r->mobs,bx,by,bz,r->player.yaw)>=0){
                (void)isr_decr_stack_size(&r->player.inv,r->player.inv.current_item,1);
                action.do_place=0;action.use=0;
            }
        }
    }
    /* Mount nearby boat on use when not already placing a block. A tape boat
     * is the client entity actually clicked; live play keeps the mob store
     * path. Defer the tape relationship by one tick for the server response. */
    if (action.use && !action.do_place && r->tape_boat.valid &&
        r->tape_boat_ride_id < 0 && r->tape_boat_mount_pending < 0) {
        double dx = r->tape_boat.x -
                    (r->player.ent.posX + (double)r->ox);
        double dy = r->tape_boat.y - r->player.ent.posY;
        double dz = r->tape_boat.z -
                    (r->player.ent.posZ + (double)r->oz);
        if (dx * dx + dy * dy + dz * dz < PSV_REACH * PSV_REACH)
            r->tape_boat_mount_pending = r->tape_boat.ent_id;
    } else if(action.use&&!action.do_place&&gm_mobs_boat_mount(&r->mobs,
               (struct PsvPlayer *)&r->player,r->ox,r->oz)){
        action.use=0;
    }
    /* Sneak dismounts boat. */
    if (action.sneak && r->tape_boat_ride_id >= 0)
        r->tape_boat_dismount_pending = 1;
    if(action.sneak&&gm_mobs_boat_riding(&r->mobs))
        gm_mobs_boat_dismount(&r->mobs,(struct PsvPlayer *)&r->player,r->ox,r->oz);
    if (action.sneak && gm_mobs_pig_riding(&r->mobs, NULL))
        gm_mobs_pig_dismount_explicit(
            &r->mobs, r->world, (const struct Chunk *)r->window,
            (struct PsvPlayer *)&r->player, r->ox, r->oz);
    if (action.do_place && gm_mobs_pig_riding(&r->mobs, NULL)) {
        ICStack main = isr_get_stack(
            &r->player.inv, r->player.inv.current_item);
        ICStack off = isr_get_stack(&r->player.inv, ISR_OFFHAND_SLOT);
        if (main.item == 398 && main.count > 0) {
            queued_pig_boost = 1;
            queued_pig_boost_hand = 0;
        } else if (off.item == 398 && off.count > 0) {
            queued_pig_boost = 1;
            queued_pig_boost_hand = 1;
        }
        if (queued_pig_boost) {
            action.do_place = 0;
            action.use = 0;
        }
    }
    /* Mounted: WASD drives the boat (EntityBoat.controlBoat); player walk is
     * suppressed so the hull is the only motion source. */
    float boat_fwd = 0.0f, boat_str = 0.0f;
    if (gm_mobs_boat_riding(&r->mobs)) {
        boat_fwd = action.forward;
        boat_str = action.strafe;
    }
    if (gm_mobs_boat_riding(&r->mobs)
            || gm_mobs_pig_riding(&r->mobs, NULL)) {
        action.forward = 0.0f;
        action.strafe = 0.0f;
    }
    if (r->container) {
        GmPlayerView cv; gm_runtime_view(r,&cv);
        double dx=(r->container_wx+0.5)-cv.x;
        double dy=(r->container_wy+0.5)-(cv.y+cv.eye_height);
        double dz=(r->container_wz+0.5)-cv.z;
        int id=gm_world_block(r->world,r->container_wx,r->container_wy,r->container_wz);
        int valid=r->container==1?id==58
                 :r->container==2?(id==61||id==62)
                 :r->container==3?runtime_is_chest_block(id)
                 :r->container==4?id==117
                 :r->container==5?id==116
                 :0;
        if (!valid || dx*dx+dy*dy+dz*dz>36.0) {
            runtime_close_container(r);
            r->container=0;r->active_furnace=-1;r->active_chest=-1;
            r->active_static_container=-1;
        }
    }
    if (action.close_container && r->container) {
        runtime_close_container(r);
        r->container = 0;
    }
    if (action.inv_click) {
        (void)gm_container_click(r, action.inv_slot, action.inv_button, action.inv_type);
        action.inv_click = 0;
    }
    /* Apply integrated-server landing packets before the client snapshots its
     * physics/raycast window for this tick. */
    gm_live_pre_player_tick(&r->entities, r->world);
    /* refill the physics window only when its contents can have changed:
     * recenter, dimension/world switch, or any block mutation since the last
     * fill. The unconditional refill dominated tape replay (94% of a
     * physics-only run in find_chunk/light_state). */
    {
        long long g = gm_world_block_gen(r->world);
        if (r->win_world != r->world || r->win_ccx != r->ccx ||
            r->win_ccz != r->ccz || r->win_gen != g) {
            gm_world_fill_window(r->world, r->ccx, r->ccz, (struct Chunk *)r->window);
            r->win_world = r->world; r->win_ccx = r->ccx; r->win_ccz = r->ccz;
            r->win_gen = g;
        }
    }
    /* Block-container interaction is authoritative only when the integrated
     * server consumes the packet emitted by the preceding client update.  The
     * packet carries the original target coordinate; re-raycasting here would
     * accidentally open a table that the same click had just placed. */
    if (r->server_block_use_pending) {
        int wx = r->server_block_use_wx;
        int wy = r->server_block_use_wy;
        int wz = r->server_block_use_wz;
        int id = gm_world_block(r->world, wx, wy, wz);
        int predicted_item = r->server_block_use_predicted_item;
        int item = r->server_block_use_item;
        int meta = r->server_block_use_meta;
        r->server_block_use_pending = 0;
        r->server_block_use_predicted_item = 0;
        if (runtime_is_server_block_use(id)
                && ((id == 140 && predicted_item == 1)
                    ? runtime_flower_pot_insert(
                        r, wx, wy, wz, item, meta, 1)
                    : (id == 84 && predicted_item == 2)
                    ? runtime_jukebox_insert_record(
                        r, wx, wy, wz, item, meta, 1)
                    : gm_runtime_use_block(r, wx, wy, wz))) {
            /* Minecraft.rightClickMouse swings after a successful block
             * activation; EntityPlayer.swingArm resets attack cooldown. */
            if ((id == 140 && predicted_item == 1)
                    || (id == 84 && predicted_item == 2)
                    || (id == 46 && isr_get_stack(
                        &r->player.inv,
                        r->player.inv.current_item).count <= 0))
                /* Emptying the last fire charge changes main-hand identity;
                 * EntityPlayer.onUpdate resets cooldown after aging it. */
                late_block_use_swing = 1;
            else
                gm_mobs_player_swing(&r->mobs);
        }
    }
    if (action.do_place || action.use) {
        PsvPlayer ray_player = r->player;
        ray_player.yaw += action.dyaw;
        ray_player.pitch += action.dpitch;
        if (ray_player.pitch > 89.0f) ray_player.pitch = 89.0f;
        if (ray_player.pitch < -89.0f) ray_player.pitch = -89.0f;
        if (action.do_place) {
            int bhx, bhy, bhz, bax, bay, baz;
            double block_distance = 1.0e30;
            double ex, ey, ez, dx, dy, dz, entity_distance;
            int target_eid, target_type;
            (void)gm_raycast_sel_reach_distance(
                r->window, &r->sin_table, &ray_player, 4.5,
                &bhx, &bhy, &bhz, &bax, &bay, &baz, &block_distance);
            gm_player_look_ray(
                &r->sin_table, &ray_player,
                &ex, &ey, &ez, &dx, &dy, &dz);
            ICStack main_hand = isr_get_stack(
                &r->player.inv, r->player.inv.current_item);
            ICStack off_hand = isr_get_stack(
                &r->player.inv, ISR_OFFHAND_SLOT);
            if (gm_mobs_raycast_entity(
                    &r->mobs, r->dimension,
                    ex + r->ox, ey, ez + r->oz, dx, dy, dz, 3.0,
                    &target_eid, &target_type, &entity_distance)
                    && entity_distance < block_distance
                    && (target_type == EW_TYPE_SHEEP
                        || target_type == EW_TYPE_COW
                        || target_type == EW_TYPE_PIG
                        || target_type == EW_TYPE_CHICKEN)) {
                int target_age = 0;
                int target_saddled = 0, riding_eid = 0;
                (void)gm_mobs_get_animal_breeding_state(
                    &r->mobs, target_eid, &target_age,
                    NULL, NULL, NULL, NULL);
                if (target_type == EW_TYPE_PIG)
                    (void)gm_mobs_get_pig_saddled(
                        &r->mobs, target_eid, &target_saddled);
                int target_ridden = gm_mobs_pig_riding(
                    &r->mobs, &riding_eid) && riding_eid == target_eid;
                /* Minecraft.rightClickMouse tries MAIN_HAND before OFF_HAND
                 * and stops at the first handled interaction. ItemShears
                 * handles every sheep; breeding food handles only a child or
                 * an adult that can enter love mode. */
                if (target_type == EW_TYPE_COW && target_age >= 0
                        && main_hand.item == 325 && main_hand.count > 0) {
                    queued_feed_animal = 2;
                    queued_feed_animal_hand = 0;
                } else if (target_type == EW_TYPE_PIG && main_hand.count > 0
                            && main_hand.item == 329) {
                    queued_feed_animal = target_saddled && !target_ridden
                        ? 4 : 3;
                    queued_feed_animal_hand = 0;
                } else if (target_type == EW_TYPE_SHEEP
                        && main_hand.item == 359 && main_hand.count > 0) {
                    queued_shear = 1;
                    queued_shear_hand = 0;
                } else if (main_hand.count > 0
                        && gm_mobs_animal_can_feed(
                            &r->mobs, target_eid, main_hand.item)) {
                    queued_feed_animal = 1;
                    queued_feed_animal_hand = 0;
                } else if (target_type == EW_TYPE_PIG
                        && main_hand.item == 421 && main_hand.count > 0) {
                    /* EntityPig handles a name tag even when the unnamed
                     * stack itself makes no naming change. */
                    queued_animal_preempt = 1;
                } else if (target_type == EW_TYPE_PIG
                        && target_saddled && !target_ridden) {
                    queued_feed_animal = 4;
                    queued_feed_animal_hand = 0;
                } else if (target_type == EW_TYPE_COW && target_age >= 0
                        && off_hand.item == 325 && off_hand.count > 0) {
                    queued_feed_animal = 2;
                    queued_feed_animal_hand = 1;
                } else if (target_type == EW_TYPE_PIG && off_hand.count > 0
                            && off_hand.item == 329) {
                    queued_feed_animal = target_saddled && !target_ridden
                        ? 4 : 3;
                    queued_feed_animal_hand = 1;
                } else if (target_type == EW_TYPE_SHEEP
                        && off_hand.item == 359 && off_hand.count > 0) {
                    queued_shear = 1;
                    queued_shear_hand = 1;
                } else if (off_hand.count > 0
                        && gm_mobs_animal_can_feed(
                            &r->mobs, target_eid, off_hand.item)) {
                    queued_feed_animal = 1;
                    queued_feed_animal_hand = 1;
                } else if (target_type == EW_TYPE_PIG
                        && off_hand.item == 421 && off_hand.count > 0) {
                    queued_animal_preempt = 1;
                } else if (target_type == EW_TYPE_PIG
                        && target_saddled && !target_ridden) {
                    queued_feed_animal = 4;
                    queued_feed_animal_hand = 1;
                }
                queued_shear_eid = target_eid;
                queued_feed_animal_eid = target_eid;
            }
            if (queued_shear || queued_feed_animal
                    || queued_animal_preempt) {
                /* objectMouseOver is ENTITY, so this click cannot also use
                 * or place against the block behind the sheep. */
                action.do_place = 0;
                action.use = 0;
            }
        }
        int hx,hy,hz,ax,ay,az;
        if((action.do_place || action.use)
                && gm_raycast_sel_reach(r->window,&r->sin_table,&ray_player,PSV_REACH,
                                &hx,&hy,&hz,&ax,&ay,&az)>=0){
            int wx=hx+r->ox,wz=hz+r->oz,id=gm_world_block(r->world,wx,hy,wz);
            int server_use = runtime_is_server_block_use(id);
            ICStack held = isr_get_stack(
                &r->player.inv, r->player.inv.current_item);
            if (id == 140)
                server_use = action.do_place
                    && held.count > 0
                    && !runtime_flower_pot_payload(
                        r, wx, hy, wz, NULL, NULL)
                    && runtime_flower_pot_item_supported(
                        held.item, held.meta);
            else if (id == 84)
                server_use = action.do_place
                    && (gm_world_meta(r->world, wx, hy, wz) != 0
                        || (held.count > 0 && held.item >= 2256
                            && held.item <= 2267 && held.meta == 0));
            else if (id == 46)
                server_use = action.do_place && held.count > 0
                    && (held.item == 259 || held.item == 385);
            if (server_use) {
                if (action.do_place) {
                    r->server_block_use_pending = 1;
                    r->server_block_use_wx = wx;
                    r->server_block_use_wy = hy;
                    r->server_block_use_wz = wz;
                    r->server_block_use_predicted_item = 0;
                    if (id == 140) {
                        r->server_block_use_item = held.item;
                        r->server_block_use_meta = held.meta;
                        r->server_block_use_predicted_item = 1;
                    } else if (id == 84
                            && gm_world_meta(
                                r->world, wx, hy, wz) == 0) {
                        r->server_block_use_item = held.item;
                        r->server_block_use_meta = held.meta;
                        r->server_block_use_predicted_item = 2;
                    }
                }
                /* The block consumes right-click; do not let the generic
                 * placement path place an item against it in this client tick. */
                action.do_place = 0;
                action.use = 0;
            }
        }
    }
    /* The integrated server consumes the movement packet emitted by the
     * preceding client update. This can change authoritative onGround,
     * motion, exhaustion, and the first Entity.move fire contact before the
     * ordinary player update ages hurt timers. */
    runtime_process_server_packet(r);
    int pig_vehicle_packets_at_entry =
        (r->pig_vehicle_packet.pending ? 1 : 0)
        + (r->pig_vehicle_packet_deferred.pending ? 1 : 0);
    for (int packet_index = 0;
            packet_index < pig_vehicle_packets_at_entry; ++packet_index)
        runtime_process_server_pig_vehicle_packet(r);
    /* Age timers already present at tick entry. Player-base hazards below can
     * create a fresh 20/10 pair during onEntityUpdate; vanilla then decrements
     * that new pair later in the same player tick, handled by hazard_reset. */
    gm_mobs_player_hurt_tick(&r->mobs);
    int hazard_reset = 0;
    /* Entity.onEntityUpdate: burning damage uses the pre-decrement counter.
     * EntityLivingBase later ages a newly-created hurt pair in this same tick. */
    if(r->player_fire_ticks>0){
        if(r->player_fire_ticks%20==0
                && !runtime_has_potion(r, 12)) {
            int before = r->mobs.player_hurt_resistant;
            (void)gm_mobs_attack_player(&r->mobs,
                (struct PvStats *)&r->vitals, &r->player.inv, 1.0f, 1);
            if (r->mobs.player_hurt_resistant > before)
                hazard_reset = 1;
        }
        --r->player_fire_ticks;
        r->player.health=r->vitals.health;
    }
    if (action.attack && attack_hits_falling_block(r))
        action.attack_entity=1;
    /* player_view lands before gm_runtime_tick for a tape row. Carry its
     * GameType into the ordinary PlayerControllerMP dig calculation; action
     * t is still consumed during row t, matching the recorder's post-tick
     * semantics. This is mode propagation, not a block-specific shortcut. */
    action.creative = r->tape_creative;
    int wet_pre = runtime_player_is_wet(r, &r->player);
    /* EntityLivingBase.onEntityUpdate: the eye-height liquid-surface test,
     * then one air decrement per submerged tick. With no Respiration or water-
     * breathing effect, -20 resets to 0 and deals 2 DROWN damage. This is one
     * existing-window block lookup per player tick; no world scan/allocation. */
    {
        double eye_y = r->player.ent.posY + psv_player_eye_height(&r->player);
        int wx = (int)floor(r->player.ent.posX + (double)r->ox);
        int wy = (int)floor(eye_y);
        int wz = (int)floor(r->player.ent.posZ + (double)r->oz);
        int id = gm_world_block(r->world,wx,wy,wz);
        /* Forge 1.11.2 Entity.isInsideOfMaterial delegates vanilla
         * BlockLiquid to ForgeHooks, whose positive-filled test is
         * eyes < blockY + 1 + getLiquidHeightPercent(meta). Since wy is
         * floor(eyes), every eye inside a water block passes; breathing
         * resumes only after the eye enters a non-water block. */
        int eye_in_water = id == 8 || id == 9;
        if (!eye_in_water) {
            r->player_air = 300;
        } else {
            if (!runtime_has_potion(r, 13)) {
                --r->player_air;
                if (r->player_air == -20) {
                    r->player_air = 0;
                    int before = r->mobs.player_hurt_resistant;
                    (void)gm_mobs_attack_player(
                        &r->mobs, (struct PvStats *)&r->vitals,
                        &r->player.inv, 2.0f, 1);
                    if (r->mobs.player_hurt_resistant > before)
                        hazard_reset = 1;
                    r->player.health = r->vitals.health;
                }
            }
        }
    }
    /* EntityLivingBase extinguishes after its air/drowning work. Entity.move
     * below converts this zero to the player's ordinary -20 immune sentinel. */
    if (wet_pre)
        r->player_fire_ticks = 0;
    if (hazard_reset)
        gm_mobs_player_hurt_tick(&r->mobs);
    runtime_tick_potions(r);
    if (r->server_shear_pending) {
        uint64_t shear_seed = r->next_shears_random_valid
            ? r->next_shears_random_seed48
            : mc_hash_seed(
                (uint64_t)r->seed, r->tick, r->server_shear_eid,
                0, 0, UINT32_C(0x53484541)) & GM_JAVA_RANDOM_MASK;
        int hand_slot = r->server_shear_hand
            ? ISR_OFFHAND_SLOT : r->player.inv.current_item;
        int result = gm_mobs_shear_sheep(
            &r->mobs, r->server_shear_eid, &r->player.inv, hand_slot,
            &shear_seed, &r->math_random_seed48,
            &r->entities, &r->next_entity_id);
        if (result == 2) {
            r->next_shears_random_seed48 = shear_seed;
            r->next_shears_random_valid = 0;
        }
    }
    if (r->server_feed_animal_pending) {
        int hand_slot = r->server_feed_animal_hand
            ? ISR_OFFHAND_SLOT : r->player.inv.current_item;
        if (r->server_feed_animal_pending == 2) {
            double eye_height = (r->server_player.elytra_flying
                    || r->server_player.elytra_pose)
                ? (double)0.4F : (double)1.62F;
            (void)gm_mobs_milk_cow(
                &r->mobs, r->server_feed_animal_eid,
                &r->player.inv, hand_slot, 0,
                r->server_player.ent.posX + (double)r->ox,
                r->server_player.ent.posY,
                r->server_player.ent.posZ + (double)r->oz,
                r->server_player.yaw, r->server_player.pitch, eye_height,
                &r->sin_table, &r->math_random_seed48,
                &r->entities, &r->next_entity_id);
        } else if (r->server_feed_animal_pending == 3) {
            (void)gm_mobs_saddle_pig(
                &r->mobs, r->server_feed_animal_eid,
                &r->player.inv, hand_slot, 0);
        } else if (r->server_feed_animal_pending == 4) {
            (void)gm_mobs_pig_mount(
                &r->mobs, r->server_feed_animal_eid);
        } else {
            /* The playable runtime is currently survival-only. */
            (void)gm_mobs_feed_animal(
                &r->mobs, r->server_feed_animal_eid,
                &r->player.inv, hand_slot, 0);
        }
    }
    if (r->server_pig_boost_pending) {
        int hand_slot = r->server_pig_boost_hand
            ? ISR_OFFHAND_SLOT : r->player.inv.current_item;
        (void)gm_mobs_pig_boost(
            &r->mobs, &r->player.inv, hand_slot, 0);
    }
    /* CPacketUseEntity is handled by the integrated server on the following
     * locked tick. Queue only the physical attack press edge (`do_break`);
     * holding the mouse over an entity does not synthesize repeat clicks. */
    if (r->server_attack_pending) {
        if (runtime_attack_item_frame(r)) {
            /* EntityItemFrame consumes the attack before block/mob reach. */
        } else if (r->dimension == 1) {
            GmDragonCrystalHit crystal_hit;
            int dragon_hit=gm_dragon_player_attack(
                &r->dragon, (const struct PsvPlayer *)&r->player,
                r->ox, r->oz, &crystal_hit);
            if(dragon_hit==2){
                runtime_explode_with_rays(
                    r,crystal_hit.x,crystal_hit.y,crystal_hit.z,6.0F,1);
                gm_dragon_crystal_destroyed(
                    &r->dragon,crystal_hit.index,1,1);
            }
        } else {
            GmMobDeathContext death_context = {
                r->do_mob_loot,
                &r->math_random_seed48,
                &r->next_entity_id
            };
            int attack_result = gm_mobs_player_attack(
                &r->mobs, (const struct PsvPlayer *)&r->player,
                r->ox, r->oz,
                (const struct McSinTable *)&r->sin_table, &r->entities,
                runtime_attack_potion_bonus(r),
                r->player_attack_speed_multiplier,
                psv_is_on_ladder(r->window, &r->player.ent),
                psv_in_liquid(r->window, &r->player.ent, 1),
                r->mobs.pig_ride >= 0 || r->mobs.boat_ride >= 0,
                &death_context);
            if (attack_result == 2)
                pv_add_exhaustion(&r->vitals, 0.1f);
        }
    }
    if (r->server_swing_pending) {
        /* EntityPlayerMP.swingArm resets ticksSinceLastSwing for both the
         * clickMouse arm packet and held progressive-dig arm packets. */
        gm_mobs_player_swing(&r->mobs);
    }
    r->server_attack_pending = action.do_break != 0;
    r->server_shear_pending = queued_shear;
    r->server_shear_eid = queued_shear_eid;
    r->server_shear_hand = queued_shear_hand;
    r->server_feed_animal_pending = queued_feed_animal;
    r->server_feed_animal_eid = queued_feed_animal_eid;
    r->server_feed_animal_hand = queued_feed_animal_hand;
    r->server_pig_boost_pending = queued_pig_boost;
    r->server_pig_boost_hand = queued_pig_boost_hand;
    GmBlockEdit edits[GM_RUNTIME_MAX_EDITS];
    int n = 0;
    gm_player_tick_network_client_effects((struct Chunk *)r->window,
                                  (const struct McSinTable *)&r->sin_table,
                                  (struct PsvPlayer *)&r->player,
                                  (struct PvStats *)&r->vitals, action,
                                  r->ox, 0, r->oz,
                                  edits, &n, GM_RUNTIME_MAX_EDITS,
                                  r->haste_amplifier, r->fatigue_amplifier);
    {
        int x, y, z, state_id, sound;
        float volume, pitch;
        if (gm_player_take_dig_sound(&x, &y, &z, &state_id)
                && gm_runtime_block_hit_sound(
                    state_id, &sound, &volume, &pitch))
            runtime_sound_event_append(
                r, sound, GM_SOUND_CATEGORY_NEUTRAL, 0, 0,
                (double)x + 0.5, (double)y + 0.5, (double)z + 0.5,
                volume, pitch);
    }
    {
        int damage, state_id, sound;
        float volume, pitch;
        if (gm_player_take_fall_sound(&damage, &state_id)) {
            double x = r->player.ent.posX + (double)r->ox;
            double y = r->player.ent.posY;
            double z = r->player.ent.posZ + (double)r->oz;
            runtime_sound_event_append(
                r, damage > 4 ? GM_SOUND_PLAYER_BIG_FALL
                              : GM_SOUND_PLAYER_SMALL_FALL,
                GM_SOUND_CATEGORY_PLAYERS, 0, 0,
                x, y, z, 1.0F, 1.0F);
            if (gm_runtime_block_fall_sound(
                    state_id, &sound, &volume, &pitch))
                runtime_sound_event_append(
                    r, sound, GM_SOUND_CATEGORY_PLAYERS, 0, 0,
                    x, y, z, volume, pitch);
        }
    }
    {
        ICStack item_use_drop=gm_player_take_item_use_drop();
        if (!isr_is_empty(&item_use_drop))
            (void)runtime_drop_player_stack(r,item_use_drop);
    }
    {
        ICStack drink=gm_player_take_finished_drink();
        if(drink.item==TB_POTION)
            runtime_finish_potion_drink(r,drink.meta);
        else if(drink.item==335)
            gm_runtime_potions_clear(r);
    }
    r->server_swing_pending =
        action.do_break != 0 || gm_player_dig_swing();
    /* NetHandlerPlayServer.update runs EntityPlayerMP travel after processing
     * the prior client packet. This is the second Entity.move fire contact and
     * the authoritative source of movement exhaustion and onGround. */
    runtime_server_player_tick(r);
    /* EntityPlayer.onLivingUpdate updates FoodStats after living travel. This
     * consumes movement and IN_FIRE exhaustion together, and may regenerate
     * or starve using the health produced by the travel contact above. */
    pv_on_update_gr(&r->vitals, &r->gamerules);
    r->player.health = r->vitals.health;
    r->player.food = (float)r->vitals.foodLevel;
    r->server_player.health = r->vitals.health;
    r->server_player.food = (float)r->vitals.foodLevel;
    /* Ghost pushers (tape replay): EntityLivingBase.collideWithNearbyEntities
     * runs right after travel, queries the UNGROWN player bb (strict
     * intersects), and each hit applies Entity.applyEntityCollision - a
     * post-friction 0.05-scaled X/Z velocity nudge away from the pusher;
     * position changes only on the NEXT tick. Verbatim vanilla math. Found at
     * tape 20260712T055346Z t1471: oracle motion gained exactly
     * (-0.014510, -0.040214) from sheep 4504 at (56.4834, 124.2278) while
     * positions still matched (mobs=off replays had no pusher at all). */
    {
        const McAABB *pb = (const McAABB *)&r->player.ent.box;
        for (int i = 0; i < r->nghosts; ++i) {
            double gx = r->ghosts[i].x - (double)r->ox;
            double gy = r->ghosts[i].y;
            double gz = r->ghosts[i].z - (double)r->oz;
            double hw = r->ghosts[i].w * 0.5;
            if (!(pb->minX < gx + hw && pb->maxX > gx - hw &&
                  pb->minY < gy + r->ghosts[i].h && pb->maxY > gy &&
                  pb->minZ < gz + hw && pb->maxZ > gz - hw))
                continue;
            double d0 = r->player.ent.posX - gx;
            double d1 = r->player.ent.posZ - gz;
            double d2 = fabs(d0) > fabs(d1) ? fabs(d0) : fabs(d1);
            if (d2 >= 0.009999999776482582) {
                d2 = (double)(float)sqrt(d2);  /* MathHelper.sqrt is float */
                d0 /= d2; d1 /= d2;
                double d3 = 1.0 / d2;
                if (d3 > 1.0) d3 = 1.0;
                d0 *= d3; d1 *= d3;
                d0 *= 0.05000000074505806; d1 *= 0.05000000074505806;
                r->player.ent.motionX += d0;
                r->player.ent.motionZ += d1;
            }
        }
        r->nghosts = 0;
    }
    runtime_queue_client_move_packet(r);
    for (int i = 0; i < n; ++i) {
        int old_id = gm_world_block(r->world, edits[i].wx, edits[i].wy, edits[i].wz);
        int old_meta =
            gm_world_meta(r->world, edits[i].wx, edits[i].wy, edits[i].wz);
        int break_meta = old_meta;
        if (old_id == 132 && edits[i].id != 132
                && edits[i].harvest_tool == 359)
            break_meta |= 8;
        int new_comparator =
            edits[i].id == 149 || edits[i].id == 150;
        if (new_comparator
                && !runtime_comparator_find(
                    r, r->dimension,
                    edits[i].wx, edits[i].wy, edits[i].wz)
                && r->comparator_count >= GM_RUNTIME_COMPARATORS)
            continue;
        if ((edits[i].id == 151 || edits[i].id == 178)
                && runtime_daylight_detector_find(
                    r, r->dimension,
                    edits[i].wx, edits[i].wy, edits[i].wz) < 0
                && r->daylight_detector_count
                    >= GM_RUNTIME_DAYLIGHT_DETECTORS)
            continue;
        if (runtime_is_chest_block(old_id) && edits[i].id != old_id)
            runtime_break_chest_te(r, edits[i].wx, edits[i].wy, edits[i].wz);
        if ((old_id == 61 || old_id == 62)
                && edits[i].id != 61 && edits[i].id != 62)
            runtime_break_furnace_te(
                r, edits[i].wx, edits[i].wy, edits[i].wz);
        if (runtime_static_container_size_for_block(old_id) > 0
                && edits[i].id != old_id)
            runtime_break_static_container_te(
                r, edits[i].wx, edits[i].wy, edits[i].wz);
        if (runtime_is_command_block(old_id)
                && edits[i].id != old_id)
            runtime_break_command_block_te(
                r, edits[i].wx, edits[i].wy, edits[i].wz);
        if (old_id != edits[i].id || old_meta != edits[i].meta)
            runtime_break_item_frames_for_block(
                r, edits[i].wx, edits[i].wy, edits[i].wz);
        if (edits[i].break_effect && old_id != 0)
            runtime_world_event_append(
                r, 2001, edits[i].wx, edits[i].wy, edits[i].wz,
                old_id | ((old_meta & 255) << 12));
        gm_world_set_block_meta(r->world, edits[i].wx, edits[i].wy, edits[i].wz,
                                edits[i].id, edits[i].meta);
        if (edits[i].place_effect && edits[i].id != 0)
            (void)runtime_block_place_audio_append(
                r, edits[i].wx, edits[i].wy, edits[i].wz,
                edits[i].id | ((edits[i].meta & 255) << 12));
        gm_live_block_changed(&r->entities, r->world,
                              edits[i].wx, edits[i].wy, edits[i].wz);
        if (old_id != 122 && edits[i].id == 122)
            (void)runtime_schedule_tick_insert(
                r, edits[i].wx, edits[i].wy, edits[i].wz, 122,
                r->clock.total_time + 5, 0,
                r->scheduled_tick_next_order);
        if (old_id != 145 && edits[i].id == 145)
            (void)runtime_schedule_tick_insert(
                r, edits[i].wx, edits[i].wy, edits[i].wz, 145,
                r->clock.total_time + 2, 0,
                r->scheduled_tick_next_order);
        if ((old_id == 151 || old_id == 178)
                && edits[i].id != 151 && edits[i].id != 178)
            runtime_daylight_detector_remove(
                r, r->dimension,
                edits[i].wx, edits[i].wy, edits[i].wz);
        if (edits[i].id == 151 || edits[i].id == 178)
            (void)runtime_daylight_detector_ensure(
                r, r->dimension,
                edits[i].wx, edits[i].wy, edits[i].wz);
        if ((old_id == 149 || old_id == 150)
                && old_id != edits[i].id)
            runtime_comparator_remove(
                r, r->dimension,
                edits[i].wx, edits[i].wy, edits[i].wz);
        if (old_id != edits[i].id
                && (old_id == 55 || old_id == 93 || old_id == 94
                    || old_id == 131 || old_id == 132
                    || old_id == 149 || old_id == 150
                    || (runtime_redstone_is_pressure_plate(old_id)
                        && old_meta > 0)))
            runtime_redstone_break_replaced_state(
                r, edits[i].wx, edits[i].wy, edits[i].wz,
                old_id, break_meta);
        if (old_id != edits[i].id
                && (edits[i].id == 93 || edits[i].id == 94))
            runtime_redstone_repeater_notify_output(
                r, edits[i].wx, edits[i].wy, edits[i].wz,
                edits[i].meta);
        if (old_id != edits[i].id && new_comparator)
            runtime_redstone_repeater_notify_output(
                r, edits[i].wx, edits[i].wy, edits[i].wz,
                edits[i].meta);
        if (new_comparator)
            (void)runtime_comparator_ensure(
                r, r->dimension,
                edits[i].wx, edits[i].wy, edits[i].wz);
        if (old_id != 132 && edits[i].id == 132)
            runtime_redstone_tripwire_notify_hook(
                r, edits[i].wx, edits[i].wy, edits[i].wz,
                edits[i].meta, 0);
        if (old_id == 218 && edits[i].id != 218
                && (old_meta & 8) != 0
                && runtime_redstone_observer_tick_pending(
                    r, edits[i].wx, edits[i].wy, edits[i].wz))
            runtime_redstone_observer_notify_output(
                r, edits[i].wx, edits[i].wy, edits[i].wz, old_meta);
        if (old_id != 218 && edits[i].id == 218)
            runtime_redstone_observer_on_added(
                r, edits[i].wx, edits[i].wy, edits[i].wz);
        gm_fluid_mark(&r->fluids, r->world, r->dimension,
                      edits[i].wx, edits[i].wy, edits[i].wz);
        break_unsupported_plants(r, edits[i].wx, edits[i].wy, edits[i].wz);
        if (edits[i].id == 55)
            runtime_redstone_update_wire_component(
                r, edits[i].wx, edits[i].wy, edits[i].wz);
        if (old_id != 55 && edits[i].id == 55)
            runtime_redstone_wire_on_added(
                r, edits[i].wx, edits[i].wy, edits[i].wz);
        if (old_id != edits[i].id
                && (edits[i].id == 123 || edits[i].id == 124))
            runtime_redstone_lamp_on_added(
                r, edits[i].wx, edits[i].wy, edits[i].wz,
                edits[i].id);
        if (old_id != edits[i].id
                && (old_id == 76 || edits[i].id == 76))
            runtime_redstone_torch_notify_adjacent_neighbors(
                r, edits[i].wx, edits[i].wy, edits[i].wz);
        runtime_redstone_notify_neighbors(
            r, edits[i].wx, edits[i].wy, edits[i].wz);
        if (old_id != edits[i].id || old_meta != edits[i].meta)
            runtime_redstone_update_observers_at(
                r, edits[i].wx, edits[i].wy, edits[i].wz);
        if (runtime_is_chest_block(old_id)
                || old_id == 61 || old_id == 62
                || runtime_static_container_size_for_block(old_id) > 0
                || old_id == 92 || old_id == 118
                || old_id == 120
                || runtime_is_command_block(old_id)
                || runtime_is_chest_block(edits[i].id)
                || edits[i].id == 61 || edits[i].id == 62
                || runtime_static_container_size_for_block(
                    edits[i].id) > 0
                || edits[i].id == 92 || edits[i].id == 118
                || edits[i].id == 120
                || runtime_is_command_block(edits[i].id))
            runtime_redstone_update_comparator_output_level(
                r, edits[i].wx, edits[i].wy, edits[i].wz);
        if(edits[i].id==8&&r->dimension==-1){
            gm_world_set_block(r->world,edits[i].wx,edits[i].wy,edits[i].wz,0);
        }else if(edits[i].id==8||edits[i].id==10){
            static const int dx[6]={1,-1,0,0,0,0},dy[6]={0,0,1,-1,0,0},dz[6]={0,0,0,0,1,-1};
            for(int q=0;q<6;++q){int x=edits[i].wx+dx[q],y=edits[i].wy+dy[q],z=edits[i].wz+dz[q];
                int id=gm_world_block(r->world,x,y,z);
                if(edits[i].id==8&&(id==10||id==11))
                    gm_world_set_block(r->world,x,y,z,gm_world_meta(r->world,x,y,z)==0?49:4);
                else if(edits[i].id==10&&(id==8||id==9))
                    gm_world_set_block(r->world,edits[i].wx,edits[i].wy,edits[i].wz,49);
            }
        }
        if(edits[i].id==51)
            runtime_fire_on_added(
                r,edits[i].wx,edits[i].wy,edits[i].wz);
        if (edits[i].drop_id > 0)
            (void)runtime_spawn_item_stack_at_block(
                r, edits[i].wx, edits[i].wy, edits[i].wz,
                edits[i].drop_id, edits[i].drop_meta, 1);
    }
    if (r->weather_enabled) {
        gm_world_clock_set_random_seed48(
            &r->clock, (unsigned long long)r->world_random_seed48);
        gm_world_tick(&r->clock);
        r->world_random_seed48 = (uint64_t)
            gm_world_clock_random_seed48(&r->clock);
    } else {
        gm_world_tick_clear(&r->clock);
    }
    runtime_tick_scheduled(r);
    runtime_tick_weather_chunks(r);
    /* World.updateEntities advances weather effects before loaded entities. */
    runtime_tick_lightning(r);
    int deferred_falling_blocks =
        runtime_controlled_tnt_precedes_falling(r);
    if (!deferred_falling_blocks)
        runtime_tick_falling_blocks(r);
    gm_fluid_tick(&r->fluids, r->world, r->dimension, r->tick);
    /* Random block ticks: LIVE/WINDOW only (r->randtick_enabled). Replay keeps
     * this off; do not approximate Java's unseedable world RNG on tapes. */
    if (r->randtick_enabled)
        gm_randtick_pass(r->world, r->seed, r->tick, r->ccx, r->ccz,
                         r->randtick_radius, &r->gamerules);
    if (r->piston_recheck_count > 0)
        runtime_redstone_piston_process_rechecks(r);
    int mob_block_collisions = 0;
    int deferred_controlled_mobs =
        runtime_controlled_tnt_precedes_mob(r);
    int early_controlled_items =
        runtime_controlled_item_precedes_tnt(r);
    int player_xp_before_entities = r->mobs.xp_total;
    if (r->mobs_enabled) {
        gm_mobs_tick(&r->mobs,r->world,(const struct Chunk *)r->window,
                     (const struct McSinTable *)&r->sin_table,
                     (struct PsvPlayer *)&r->player,(struct PvStats *)&r->vitals,
                     r->ox,r->oz,r->dimension,r->clock.world_time,
                     r->mob_griefing,&r->world_random_seed48,
                     &r->math_random_seed48,&r->next_entity_id,
                     r->do_mob_loot,&r->entities,
                     boat_fwd, boat_str);
        {
            int x, y, z, data;
            while (gm_mobs_take_sheep_world_event(
                    &r->mobs, &x, &y, &z, &data))
                runtime_world_event_append(r, 2001, x, y, z, data);
        }
        {double x,y,z;if(gm_mobs_take_explosion(&r->mobs,&x,&y,&z))runtime_explode(r,x,y,z,3.0f);}
        spawn_hostile_projectiles(r);
        mob_block_collisions = 1;
        if (r->mobs.pig_ride >= 0)
            runtime_queue_client_pig_vehicle_packet(r);
    } else if (r->controlled_mobs_enabled) {
        if (!deferred_controlled_mobs) {
            gm_mobs_tick_controlled(
                &r->mobs, r->world, (const struct Chunk *)r->window,
                (struct PsvPlayer *)&r->player,
                r->ox, r->oz, r->dimension, r->do_mob_loot,
                &r->world_random_seed48, &r->math_random_seed48,
                &r->next_entity_id);
            mob_block_collisions = 2;
        }
    } else {
        gm_mobs_tick_xp(
            &r->mobs, r->world, (struct PsvPlayer *)&r->player,
            r->ox, r->oz);
    }
    runtime_apply_collected_xp(r, player_xp_before_entities);
    runtime_sound_drain_mobs(r);
    if (mob_block_collisions)
        runtime_redstone_mob_pressure_plate_collisions(
            r, mob_block_collisions == 2);
    if (!deferred_controlled_mobs && r->mobs.xp_collision_count > 0)
        runtime_redstone_xp_pressure_plate_collisions(r);
    if (late_block_use_swing)
        gm_mobs_player_swing(&r->mobs);
    if(r->dimension==1){
        GmPlayerView dv;gm_runtime_view(r,&dv);
        if(gm_dragon_tick(&r->dragon,r->world,(const struct McSinTable *)&r->sin_table,
                          dv.x,dv.y,dv.z)) {
            gm_mobs_spawn_xp(&r->mobs,0.5,65.5,0.5,12000);
            runtime_spawn_dragon_gateway(r);
        }
    }
    gm_runtime_tick_fireworks(r);
    gm_runtime_tick_fishing(r);
    gm_runtime_tick_minecarts(r);
    int deferred_small_fireballs =
        runtime_controlled_tnt_precedes_small_fireball(r);
    tick_projectiles(r, deferred_small_fireballs ? -1 : 0);
    if (early_controlled_items)
        runtime_tick_live_items(r);
    /* Existing arrows precede TNT they ignite in the Java loaded-entity
     * iteration, and the newly spawned primed entity still updates in that
     * boundary. Redstone/player-spawned TNT also receives exactly one tick. */
    runtime_tick_primed_tnt(r);
    runtime_tick_end_crystals(r);
    if (deferred_small_fireballs)
        tick_projectiles(r, 1);
    runtime_tick_area_effect_clouds(r);
    if (deferred_falling_blocks)
        runtime_tick_falling_blocks(r);
    if (deferred_controlled_mobs) {
        gm_mobs_tick_controlled(
            &r->mobs, r->world, (const struct Chunk *)r->window,
            (struct PsvPlayer *)&r->player,
            r->ox, r->oz, r->dimension, r->do_mob_loot,
            &r->world_random_seed48, &r->math_random_seed48,
            &r->next_entity_id);
        runtime_redstone_mob_pressure_plate_collisions(r, 1);
        if (r->mobs.xp_collision_count > 0)
            runtime_redstone_xp_pressure_plate_collisions(r);
    }
    if (!early_controlled_items)
        runtime_tick_live_items(r);
    /* World.updateEntities advances ordinary entities before tickable block
     * entities. Moving-piston tiles therefore sweep the already-ticked item
     * AABBs, which is observable for a just-created destroy-reaction drop. */
    runtime_tick_pistons(r);
    runtime_tick_daylight_detectors(r);
    /* Hopper work is proportional to represented live hopper tiles. The
     * absent static-container pool keeps the base profile to one branch. */
    runtime_tick_hoppers(r);
    /* Brewing stands share the cold static-container pool, but become a
     * bounded tickable active set only when the bundle is enabled. With no
     * represented stand this remains one null-pointer branch. */
    if (r->brewing_enabled && r->static_containers) {
        for (int i = 0; i < r->static_containers_cap; ++i) {
            GmRuntimeStaticContainer *stand = &r->static_containers[i];
            int bottle_drops = 0;
            int flags;
            if (!stand->active || stand->dimension != r->dimension
                    || stand->block != 117
                    || gm_world_block(r->world, stand->wx, stand->wy,
                                      stand->wz) != 117)
                continue;
            flags = brewing_live_tick(
                stand->slots, &stand->brewing, &bottle_drops);
            if (flags & TB_TICK_BOTTLES)
                gm_world_set_block_meta(
                    r->world, stand->wx, stand->wy, stand->wz, 117,
                    brewing_live_bottle_bits(stand->slots));
            if (flags & TB_TICK_BREWED)
                runtime_world_event_append(
                    r, 1035, stand->wx, stand->wy, stand->wz, 0);
            while (bottle_drops-- > 0)
                (void)runtime_drop_stack(
                    r, stand->wx, stand->wy, stand->wz,
                    ic_mk(TB_GLASS_BOTTLE, 1, 0));
            if (flags & TB_TICK_CHANGED)
                runtime_redstone_update_comparator_output_level(
                    r, stand->wx, stand->wy, stand->wz);
        }
    }
    /* Entity.updateRidden runs the player's ordinary living update (which
     * retains the recorded motion fields), then the boat updates the passenger
     * position. The entity stream is authoritative for the client boat pose,
     * avoiding a second, packet-incomplete boat simulation in tape replay. */
    if (r->tape_boat_ride_id >= 0 && r->tape_boat.valid &&
        r->tape_boat.ent_id == r->tape_boat_ride_id) {
        int left = action.strafe < -0.01f;
        int right = action.strafe > 0.01f;
        int paddle_forward = action.forward > 0.01f;
        int paddle[2] = { (right && !left) || paddle_forward,
                          (left && !right) || paddle_forward };
        for (int p = 0; p < 2; ++p)
            r->tape_boat_paddle[p] =
                paddle[p] ? r->tape_boat_paddle[p] + 0.01f : 0.0f;
        for (int i = 0; i < r->nghost_views; ++i)
            if (r->ghost_views[i].type == EW_TYPE_BOAT &&
                r->ghost_views[i].ent_id == r->tape_boat_ride_id) {
                r->ghost_views[i].boat_paddle[0] = r->tape_boat_paddle[0];
                r->ghost_views[i].boat_paddle[1] = r->tape_boat_paddle[1];
            }
        r->player.ent.posX = r->tape_boat.x - (double)r->ox;
        r->player.ent.posY =
            r->tape_boat.y - 0.44999998807907104;
        r->player.ent.posZ = r->tape_boat.z - (double)r->oz;
        /* Entity.updateRidden zeroes passenger velocity, then
         * EntityLivingBase.onLivingUpdate performs one unobstructed air
         * travel step before EntityBoat.updatePassenger replaces position.
         * Preserve that independently observable motion state. */
        float forward = action.forward * 0.98f;
        float strafe = -action.strafe * 0.98f;
        float move = strafe * strafe + forward * forward;
        if (move >= 1.0e-4f) {
            move = sqrtf(move);
            if (move < 1.0f) move = 1.0f;
            move = 0.02f / move;
            strafe *= move;
            forward *= move;
        } else {
            strafe = forward = 0.0f;
        }
        /* Boat.updatePassenger applies deltaRotation after the passenger's
         * living update, so the row's final player yaw is one boat-yaw step
         * newer than the yaw which produced these motion fields. */
        float motion_yaw = r->tape_boat_prev_yaw_valid
            ? (float)r->tape_boat_prev_yaw : r->player.yaw;
        float yaw = motion_yaw * 0.017453292f;
        float sy = mc_sin(&r->sin_table, yaw);
        float cy = mc_cos(&r->sin_table, yaw);
        r->player.ent.motionX =
            (double)(strafe * cy - forward * sy) * 0.9100000262260437;
        r->player.ent.motionY =
            -0.08 * 0.9800000190734863;
        r->player.ent.motionZ =
            (double)(forward * cy + strafe * sy) * 0.9100000262260437;
        r->player.ent.onGround = 0;
        r->player.ent.box = psv_player_box(r->player.ent.posX,
                                           r->player.ent.posY,
                                           r->player.ent.posZ);
        r->te_x = r->tape_boat.x;
        r->te_y = r->player.ent.posY;
        r->te_z = r->tape_boat.z;
    }
    for (int i = 0; i < GM_RUNTIME_FURNACES; ++i) if (r->furnaces[i].active) {
        GmRuntimeFurnace *f = &r->furnaces[i];
        int was_lit = f->state.burn_time > 0;
        SRStack old_input = f->state.input;
        SRStack old_fuel = f->state.fuel;
        SRStack old_output = f->state.output;
        furnace_live_tick(&f->state);
        int lit = f->state.burn_time > 0;
        if (lit != was_lit) {
            int id = gm_world_block(r->world,f->wx,f->wy,f->wz);
            if (id == 61 || id == 62)
                gm_world_set_block_meta(r->world,f->wx,f->wy,f->wz,lit?62:61,
                                        gm_world_meta(r->world,f->wx,f->wy,f->wz));
        }
        if (old_input.item != f->state.input.item
                || old_input.count != f->state.input.count
                || old_input.meta != f->state.input.meta
                || old_fuel.item != f->state.fuel.item
                || old_fuel.count != f->state.fuel.count
                || old_fuel.meta != f->state.fuel.meta
                || old_output.item != f->state.output.item
                || old_output.count != f->state.output.count
                || old_output.meta != f->state.output.meta)
            runtime_redstone_update_comparator_output_level(
                r, f->wx, f->wy, f->wz);
    }
    for (int i = 0; i < r->chests_cap; ++i)
        if (r->chests && r->chests[i].active) chest_live_tick(&r->chests[i].state);
    gm_runtime_tick_end_gateways(r);
    if (r->vitals.health <= 0.0f && !r->dead) {
        r->dead = 1;
        r->deaths++;
        r->player_death_time = 1;
        r->death_screen_ticks = 0;
        r->quit_to_title = 0;
    }
    if(r->portal_cooldown>0)--r->portal_cooldown;
    int feet=gm_world_block(r->world,(int)floor(r->player.ent.posX+r->ox),
                            (int)floor(r->player.ent.posY),(int)floor(r->player.ent.posZ+r->oz));
    int head=gm_world_block(r->world,(int)floor(r->player.ent.posX+r->ox),
                            (int)floor(r->player.ent.posY+1.0),(int)floor(r->player.ent.posZ+r->oz));
    /* vanilla Entity.setPortal: while a cooldown is pending, every in-pane
     * collision REFRESHES it - standing inside the arrival portal never
     * re-arms the transit (walked-return tape 101755Z re-transited at +88). */
    if((feet==90||head==90)&&r->portal_cooldown>0)r->portal_cooldown=100;
    if((feet==119||head==119)&&r->dimension==1&&r->dragon.state.death_processed){
        r->credits=1;r->won=1;
    }else if((feet==119||head==119)&&r->dimension==0){
        if(!r->worlds[2])r->worlds[2]=gm_world_create_type(r->seed,3);
        if(r->worlds[2]){
            r->world=r->worlds[2];r->dimension=1;
            gm_world_ensure(r->world,6,0,1);
            for(int x=98;x<=102;++x)for(int z=-2;z<=2;++z){
                gm_world_set_block(r->world,x,48,z,49);
                for(int y=49;y<=51;++y)gm_world_set_block(r->world,x,y,z,0);
            }
            gm_runtime_set_pose(r,100.5,49.0,0.5,90.0f,0.0f);
            float absorption = r->mobs.player_absorption;
            gm_mobs_init(&r->mobs,r->seed^1LL);memset(&r->entities,0,sizeof r->entities);
            r->mobs.player_resistance_amplifier = r->resistance_amplifier;
            r->mobs.player_absorption = absorption;
            gm_dragon_init(&r->dragon,r->world,r->seed);
            r->portal_time=0;r->portal_cooldown=100;
        }
    }else if((feet==90||head==90)&&r->portal_cooldown==0&&(r->dimension==0||r->dimension==-1)){
        /* The integrated server transfers at 80 contacts; the client-visible
         * dimension changes on the following tick (seed-0 natural portal tape:
         * first contact t158, ramp reaches 1.0 at t237, dim changes at t238). */
        if(++r->portal_time>=82){
            GmPlayerView v;gm_runtime_view(r,&v);
            int nd=r->dimension==0?-1:0,wi=nd+1;
            if(!r->worlds[wi])r->worlds[wi]=gm_world_create_type(r->seed,nd==-1?2:0);
            if(r->worlds[wi]){
                double scale=nd==-1?0.125:8.0,tx,ty,tz;
                int nx=(int)floor(v.x*scale),nz=(int)floor(v.z*scale);
                if(gm_portal_find_or_make(r->worlds[wi],nx,nz,&tx,&ty,&tz)){
                    r->world=r->worlds[wi];r->dimension=nd;
                    gm_runtime_set_pose(r,tx,ty,tz,v.yaw,v.pitch);
                    float absorption = r->mobs.player_absorption;
                    gm_mobs_init(&r->mobs,r->seed^(long long)nd);
                    r->mobs.player_resistance_amplifier =
                        r->resistance_amplifier;
                    r->mobs.player_absorption = absorption;
                    memset(&r->entities,0,sizeof r->entities);
                    r->portal_cooldown=100;r->portal_time=0;
                }
            }
        }
    }else if(feet!=90&&head!=90)r->portal_time=0;
    if (r->tape_boat_mount_message_ticks > 0)
        r->tape_boat_mount_message_ticks--;
    r->tick++;
}

void gm_runtime_tick_entry_feet(const GmRuntime *r,
                                double *x, double *y, double *z) {
    if (!r) return;
    if (r->te_valid) { *x = r->te_x; *y = r->te_y; *z = r->te_z; return; }
    *x = r->player.ent.posX + (double)r->ox;
    *y = r->player.ent.posY;
    *z = r->player.ent.posZ + (double)r->oz;
}

void gm_runtime_view(const GmRuntime *r, GmPlayerView *out) {
    gm_player_view((const struct PsvPlayer *)&r->player, r->ox, r->oz, out);
    out->on_ground = r->server_player.ent.onGround;
    out->dead = r->dead;
    out->deaths = r->deaths;
    out->score = r->score;
    out->death_ticks = r->death_screen_ticks;
    out->air = r->player_air;
    out->fire = r->player_fire_ticks > 0;
    out->creative = 0;
    out->hurt_time = r->mobs.player_hurt_time;
    out->max_hurt_time = 10;
    out->hurt_yaw = 0.0f;
    out->max_health = r->vitals.maxHealth;
    out->absorption = r->mobs.player_absorption;
    out->attack_cooldown = gm_mobs_player_attack_strength(
        &r->mobs, (const struct PsvPlayer *)&r->player,
        r->player_attack_speed_multiplier);
    out->potion_count = r->potion_count;
    memcpy(out->potions, r->potions,
           (size_t)r->potion_count * sizeof out->potions[0]);
    if (r->mobs.player_wither_ticks > 0
            && out->potion_count < GM_MAX_POTION_EFFECTS) {
        GmPotionEffectView *wither = &out->potions[out->potion_count++];
        wither->id = 20;
        wither->amplifier = 0;
        wither->duration = r->mobs.player_wither_ticks;
    }
    out->riding_boat = r->tape_boat_ride_id >= 0 ||
                       gm_mobs_boat_riding(&r->mobs);
    out->mount_message_ticks = r->tape_boat_mount_message_ticks;
    int xp=r->mobs.xp_total, level=0;
    for (;;) {
        int cap=level>=30?9*level-158:(level>=15?5*level-38:2*level+7);
        if (xp<cap) {
            if (r->player_xp_level >= 0) {
                out->xp_level = r->player_xp_level;
                out->xp_frac = r->player_xp_frac;
            } else {
                out->xp_level = level;
                out->xp_frac = cap ? (float)xp / (float)cap : 0.0f;
            }
            break;
        }
        xp-=cap;++level;
    }
}

void gm_runtime_set_pose(GmRuntime *r, double x, double y, double z,
                         float yaw, float pitch) {
    if (!r) return;
    r->ccx = floordiv16((int)floor(x));
    r->ccz = floordiv16((int)floor(z));
    r->ox = r->ccx * 16; r->oz = r->ccz * 16;
    r->player.ent.posX = x - r->ox;
    r->player.ent.posY = y;
    r->player.ent.posZ = z - r->oz;
    r->player.ent.box = psv_player_box(r->player.ent.posX, y, r->player.ent.posZ);
    r->player.ent.motionX = r->player.ent.motionY = r->player.ent.motionZ = 0.0;
    r->player.ent.onGround = 0;
    r->player.fall_distance = 0.0f;
    r->player.yaw = yaw; r->player.pitch = pitch;
    runtime_close_container(r);
    r->container=0; r->active_furnace=-1; r->active_chest=-1;
    r->active_static_container=-1;
    gm_player_dig_reset();
    runtime_sync_server_player(r);
}

void gm_runtime_set_velocity(GmRuntime *r, double x, double y, double z) {
    if (!r) return;
    r->player.ent.motionX=x; r->player.ent.motionY=y; r->player.ent.motionZ=z;
    r->server_player.ent.motionX=x;
    r->server_player.ent.motionY=y;
    r->server_player.ent.motionZ=z;
}

void gm_runtime_set_pose_state(GmRuntime *r, double x, double y, double z,
                               float yaw, float pitch, double vx, double vy,
                               double vz, int on_ground, float fall_distance) {
    if (!r) return;
    gm_runtime_set_pose(r,x,y,z,yaw,pitch);
    gm_runtime_set_velocity(r,vx,vy,vz);
    r->player.ent.onGround=on_ground?1:0;
    r->player.fall_distance=fall_distance;
    runtime_sync_server_player(r);
}

void gm_runtime_set_packet_velocity(GmRuntime *r, double x, double y, double z) {
    if (!r) return;
    gm_player_set_packet_velocity((struct PsvPlayer *)&r->player,x,y,z);
}

void gm_runtime_add_velocity(GmRuntime *r, double x, double y, double z) {
    /* SPacketExplosion knockback: handleExplosion adds the packet motion to
     * the local player's current motion (it does not replace it). */
    if (!r) return;
    r->player.ent.motionX += x;
    r->player.ent.motionY += y;
    r->player.ent.motionZ += z;
}

void gm_runtime_set_elytra(GmRuntime *r, int equipped) {
    /* Narrow replay/test hook. Normal play derives flight from chest item 443. */
    if (!r) return;
    r->player.elytra_equipped = equipped ? 1 : 0;
}

void gm_runtime_set_elytra_flag7(GmRuntime *r, int flying) {
    if (!r) return;
    r->player.elytra_flag7_recorded = 1;
    r->player.elytra_flying = flying ? 1 : 0;
    r->player.elytra_flying_pending = 0;
}

void gm_runtime_ent_box(GmRuntime *r, double x, double y, double z,
                        double w, double h) {
    if (!r || r->nghosts >= GM_RUNTIME_GHOSTS) return;
    r->ghosts[r->nghosts].x = x;
    r->ghosts[r->nghosts].y = y;
    r->ghosts[r->nghosts].z = z;
    r->ghosts[r->nghosts].w = w;
    r->ghosts[r->nghosts].h = h;
    r->nghosts++;
}

int gm_runtime_dragon_contact(GmRuntime *r, double min_x, double min_y,
                              double min_z, double max_x, double max_y,
                              double max_z, float damage) {
    if (!r || damage <= 0.0f || min_x >= max_x || min_y >= max_y ||
        min_z >= max_z) return 0;
    const McAABB *pb=(const McAABB *)&r->player.ent.box;
    double lx0=min_x-r->ox,lx1=max_x-r->ox;
    double lz0=min_z-r->oz,lz1=max_z-r->oz;
    if (!(pb->minX < lx1 && pb->maxX > lx0 && pb->minY < max_y &&
          pb->maxY > min_y && pb->minZ < lz1 && pb->maxZ > lz0)) return 0;
    /* Dragon part contact uses causeMobDamage-style path: armor applies. */
    int hit=gm_mobs_attack_player(&r->mobs,(struct PvStats *)&r->vitals,
                                 &r->player.inv, damage, 0);
    r->player.health=r->vitals.health;
    return hit;
}

/* Per-entity continuity for tape ghost render pose (hurt flash + limb swing).
 * Keyed by tape entity id. Render-only; never touches physics. */
#define GM_ENT_ANIM_CAP 64
typedef struct {
    int   id;
    float x, y, z;
    float hp;
    float limb_swing;
    float limb_amount;
    int   hurt_time;
    int   creeper_fuse;
    int   creeper_primed;
    int   used;
} GmEntAnim;
static GmEntAnim g_ent_anim[GM_ENT_ANIM_CAP];

static GmEntAnim *ent_anim_get(int id) {
    if (id < 0) return 0;
    for (int i = 0; i < GM_ENT_ANIM_CAP; ++i)
        if (g_ent_anim[i].used && g_ent_anim[i].id == id) return &g_ent_anim[i];
    for (int i = 0; i < GM_ENT_ANIM_CAP; ++i)
        if (!g_ent_anim[i].used) {
            memset(&g_ent_anim[i], 0, sizeof g_ent_anim[i]);
            g_ent_anim[i].id = id;
            g_ent_anim[i].used = 1;
            g_ent_anim[i].hp = -1.f;
            return &g_ent_anim[i];
        }
    return 0;
}

/* Renderable ghost entities (tape replay, divergence #10): render-only pose
 * records for this tick's frame capture. Never read by gm_runtime_tick.
 * Tracks hurtTime (hp drop -> 10 tick red flash) and limbSwing from position
 * deltas (ModelQuadruped / ModelBiped setRotationAngles). */
void gm_runtime_ent_view(GmRuntime *r, const GmEntityView *view) {
    if (!r || !view || r->nghost_views >= GM_RUNTIME_GHOST_VIEWS) return;
    GmEntityView *v = &r->ghost_views[r->nghost_views++];
    *v = *view;
    GmEntAnim *a = ent_anim_get(view->ent_id);
    if (a) {
        if (!view->tape_pose) {
            if (a->hp >= 0.f && view->health >= 0.f && view->health < a->hp - 1e-4f)
                a->hurt_time = 10;  /* legacy tape inference */
            else if (a->hurt_time > 0)
                a->hurt_time--;
            v->hurt_time = a->hurt_time;
        }
        /* EntityLivingBase.onLivingUpdate limbSwing integrate */
        float dx = view->x - a->x, dz = view->z - a->z;
        float dist = sqrtf(dx * dx + dz * dz) * 4.0f;
        if (dist > 1.0f) dist = 1.0f;
        if (a->hp >= 0.f) {  /* skip first sighting (no prev pos) */
            a->limb_amount += (dist - a->limb_amount) * 0.4f;
            a->limb_swing += a->limb_amount;
        }
        a->x = view->x; a->y = view->y; a->z = view->z;
        a->hp = view->health;
        v->limb_swing = a->limb_swing;
        v->limb_swing_amount = a->limb_amount;
        if (view->type == EW_TYPE_CREEPER && view->creeper_fuse <= 0) {
            double dxp = (double)view->x -
                         (r->player.ent.posX + (double)r->ox);
            double dzp = (double)view->z -
                         (r->player.ent.posZ + (double)r->oz);
            double engage_sq = a->creeper_fuse > 0 ? 49.0 : 9.0;
            if (dxp * dxp + dzp * dzp < engage_sq) {
                /* EntityAICreeperSwell changes the synced state first; the
                 * following EntityCreeper.onUpdate advances the fuse. A tape
                 * first observes that state transition one frame before its
                 * first non-zero flash intensity. */
                if (a->creeper_primed) {
                    if (a->creeper_fuse < 30) a->creeper_fuse++;
                } else {
                    a->creeper_primed = 1;
                }
            } else if (a->creeper_fuse > 0) {
                a->creeper_fuse--;
                if (a->creeper_fuse == 0) a->creeper_primed = 0;
            } else {
                a->creeper_primed = 0;
            }
            v->creeper_fuse = a->creeper_fuse;
        }
    }
}

void gm_runtime_tape_boat_view(GmRuntime *r, int ent_id, double x, double y,
                               double z, double yaw) {
    if (!r || ent_id < 0) return;
    /* While riding, never switch to another nearby boat. On foot select the
     * closest recorded boat so right-click resolves like the client ray hit. */
    if (r->tape_boat.valid && r->tape_boat_ride_id != ent_id) {
        if (r->tape_boat_ride_id >= 0) return;
        double px = r->player.ent.posX + (double)r->ox;
        double pz = r->player.ent.posZ + (double)r->oz;
        double old_dx = r->tape_boat.x - px;
        double old_dy = r->tape_boat.y - r->player.ent.posY;
        double old_dz = r->tape_boat.z - pz;
        double new_dx = x - px;
        double new_dy = y - r->player.ent.posY;
        double new_dz = z - pz;
        if (old_dx * old_dx + old_dy * old_dy + old_dz * old_dz <=
            new_dx * new_dx + new_dy * new_dy + new_dz * new_dz)
            return;
    }
    r->tape_boat.valid = 1;
    r->tape_boat.ent_id = ent_id;
    r->tape_boat.x = x;
    r->tape_boat.y = y;
    r->tape_boat.z = z;
    r->tape_boat.yaw = yaw;
}

void gm_runtime_ent_views_clear(GmRuntime *r) {
    if (!r) return;
    if (r->tape_boat.valid) {
        r->tape_boat_prev_yaw = r->tape_boat.yaw;
        r->tape_boat_prev_yaw_valid = 1;
    }
    r->tape_boat.valid = 0;

    for (int i = 0; i < GM_RUNTIME_FIREBALL_TRACKS; ++i)
        if (r->tape_fireball_impacts[i].active &&
            ++r->tape_fireball_impacts[i].age >= 2)
            r->tape_fireball_impacts[i].active = 0;

    struct { int ent_id; float x, y, z, dx, dy, dz; }
        current[GM_RUNTIME_FIREBALL_TRACKS];
    int ncurrent = 0;
    for (int i = 0; i < r->nghost_views &&
                    ncurrent < GM_RUNTIME_FIREBALL_TRACKS; ++i) {
        const GmEntityView *v = &r->ghost_views[i];
        if (v->type != GM_VIEW_DRAGON_FIREBALL || v->item_id != 385 ||
            v->item_meta < 2)
            continue;
        current[ncurrent].ent_id = v->ent_id;
        current[ncurrent].x = v->x;
        current[ncurrent].y = v->y;
        current[ncurrent].z = v->z;
        current[ncurrent].dx = 0.0f;
        current[ncurrent].dy = 0.0f;
        current[ncurrent].dz = 0.0f;
        for (int j = 0; j < r->ntape_large_fireballs; ++j)
            if (r->tape_large_fireballs[j].ent_id == v->ent_id) {
                current[ncurrent].dx = v->x - r->tape_large_fireballs[j].x;
                current[ncurrent].dy = v->y - r->tape_large_fireballs[j].y;
                current[ncurrent].dz = v->z - r->tape_large_fireballs[j].z;
                break;
            }
        ncurrent++;
    }

    for (int i = 0; i < r->ntape_large_fireballs; ++i) {
        int present = 0;
        for (int j = 0; j < ncurrent; ++j)
            if (current[j].ent_id == r->tape_large_fireballs[i].ent_id) {
                present = 1;
                break;
            }
        if (present) continue;

        /* The recorder keeps only its nearest entity window. A distant
         * disappearance may only mean it fell outside that window; infer an
         * impact only where the last position could affect the player view. */
        double dx = (double)r->tape_large_fireballs[i].x -
                    (r->player.ent.posX + r->ox);
        double dy = (double)r->tape_large_fireballs[i].y -
                    r->player.ent.posY;
        double dz = (double)r->tape_large_fireballs[i].z -
                    (r->player.ent.posZ + r->oz);
        if (dx * dx + dy * dy + dz * dz > 64.0 ||
            r->tape_hurt_time <= 0)
            continue;
        for (int j = 0; j < GM_RUNTIME_FIREBALL_TRACKS; ++j)
            if (!r->tape_fireball_impacts[j].active) {
                r->tape_fireball_impacts[j].active = 1;
                r->tape_fireball_impacts[j].ent_id =
                    r->tape_large_fireballs[i].ent_id;
                r->tape_fireball_impacts[j].age = 1;
                /* The removal packet reaches the client after the server-side
                 * collision. Rewind the last client velocity by two samples
                 * to recover the impact-side position instead of drawing the
                 * puff behind the camera at the extrapolated removal pose. */
                r->tape_fireball_impacts[j].x =
                    r->tape_large_fireballs[i].x -
                    2.0f * r->tape_large_fireballs[i].dx;
                r->tape_fireball_impacts[j].y =
                    r->tape_large_fireballs[i].y -
                    2.0f * r->tape_large_fireballs[i].dy;
                r->tape_fireball_impacts[j].z =
                    r->tape_large_fireballs[i].z -
                    2.0f * r->tape_large_fireballs[i].dz;
                break;
            }
    }

    r->ntape_large_fireballs = ncurrent;
    for (int i = 0; i < ncurrent; ++i) {
        r->tape_large_fireballs[i].ent_id = current[i].ent_id;
        r->tape_large_fireballs[i].x = current[i].x;
        r->tape_large_fireballs[i].y = current[i].y;
        r->tape_large_fireballs[i].z = current[i].z;
        r->tape_large_fireballs[i].dx = current[i].dx;
        r->tape_large_fireballs[i].dy = current[i].dy;
        r->tape_large_fireballs[i].dz = current[i].dz;
    }
    r->nghost_views = 0;
}

int gm_runtime_ghost_views(const GmRuntime *r, GmEntityView *out, int max) {
    if (!r) return 0;
    int n = r->nghost_views < max ? r->nghost_views : max;
    for (int i = 0; i < n; ++i) out[i] = r->ghost_views[i];
    for (int i = 0; i < GM_RUNTIME_FIREBALL_TRACKS && n < max; ++i)
        if (r->tape_fireball_impacts[i].active) {
            GmEntityView *v = &out[n++];
            memset(v, 0, sizeof *v);
            v->type = GM_VIEW_EXPLOSION_LARGE;
            v->x = r->tape_fireball_impacts[i].x;
            v->y = r->tape_fireball_impacts[i].y;
            v->z = r->tape_fireball_impacts[i].z;
            v->ent_id = r->tape_fireball_impacts[i].ent_id;
            v->age = r->tape_fireball_impacts[i].age;
        }
    return n;
}

/* Open GUI screen for tape-replay frame capture (divergence #9). Render-only:
 * never touches r->container / craft grid / furnace so physics stays clean. */
void gm_runtime_gui_view(GmRuntime *r, int container, int mx, int my) {
    if (!r || container < 0 || container > 4) return;
    r->gui_view_active = 1;
    r->gui_view_container = container;
    r->gui_view_mx = mx;
    r->gui_view_my = my;
}

void gm_runtime_gui_view_clear(GmRuntime *r) {
    if (!r) return;
    r->gui_view_active = 0;
    memset(r->tape_gui_slot_active, 0, sizeof r->tape_gui_slot_active);
    r->tape_gui_cursor_active = 0;
    r->tape_furnace_active = 0;
    r->tape_brewing_active = 0;
}

int gm_runtime_gui_view_get(const GmRuntime *r, int *container, int *mx, int *my) {
    if (!r || !r->gui_view_active) return 0;
    if (container) *container = r->gui_view_container;
    if (mx) *mx = r->gui_view_mx;
    if (my) *my = r->gui_view_my;
    return 1;
}

static int tape_stack_valid(int item, int count, int meta) {
    return item >= 0 && item <= 4095 && count >= 0 && count <= 64 &&
           meta >= 0 && meta <= 32767 && ((item == 0) == (count == 0));
}

static int tape_stack_enchants_ok(const ICStack *s) {
    int n;
    if (!s) return 0;
    n = s->n_enchants;
    if (n < 0 || n > IC_MAX_ENCHANTS) return 0;
    return 1;
}

int gm_runtime_tape_gui_slot_stack(GmRuntime *r, int slot, ICStack stack) {
    if (!r || slot < 0 || slot >= GMC_SLOT_COUNT ||
        !tape_stack_valid(stack.item, stack.count, stack.meta) ||
        !tape_stack_enchants_ok(&stack)) return 0;
    if (stack.count == 0) stack = ic_empty();
    r->tape_gui_slots[slot] = stack;
    r->tape_gui_slot_active[slot] = 1;
    return 1;
}

int gm_runtime_tape_gui_cursor_stack(GmRuntime *r, ICStack stack) {
    if (!r || !tape_stack_valid(stack.item, stack.count, stack.meta) ||
        !tape_stack_enchants_ok(&stack)) return 0;
    if (stack.count == 0) stack = ic_empty();
    r->tape_gui_cursor = stack;
    r->tape_gui_cursor_active = 1;
    return 1;
}

int gm_runtime_tape_gui_slot(GmRuntime *r, int slot, int item, int count, int meta) {
    return gm_runtime_tape_gui_slot_stack(
        r, slot, count == 0 ? ic_empty() : ic_mk(item, count, meta));
}

int gm_runtime_tape_gui_cursor(GmRuntime *r, int item, int count, int meta) {
    return gm_runtime_tape_gui_cursor_stack(
        r, count == 0 ? ic_empty() : ic_mk(item, count, meta));
}

int gm_runtime_tape_gui_slot_get(const GmRuntime *r, int slot, ICStack *out) {
    if (!r || slot < 0 || slot >= GMC_SLOT_COUNT ||
        !r->tape_gui_slot_active[slot]) return 0;
    if (out) *out = r->tape_gui_slots[slot];
    return 1;
}

int gm_runtime_tape_gui_cursor_get(const GmRuntime *r, ICStack *out) {
    if (!r || !r->tape_gui_cursor_active) return 0;
    if (out) *out = r->tape_gui_cursor;
    return 1;
}

int gm_runtime_tape_furnace(GmRuntime *r, int burn, int current_burn,
                            int cook, int total_cook) {
    if (!r || burn < 0 || current_burn < 0 || cook < 0 || total_cook < 0)
        return 0;
    r->tape_furnace_active = 1;
    r->tape_furnace_burn = burn;
    r->tape_furnace_current_burn = current_burn;
    r->tape_furnace_cook = cook;
    r->tape_furnace_total_cook = total_cook;
    return 1;
}

int gm_runtime_tape_brewing(GmRuntime *r, int brew, int fuel) {
    if (!r || brew < 0 || brew > TB_BREW_TICKS
            || fuel < 0 || fuel > TB_FUEL_CHARGE)
        return 0;
    r->tape_brewing_active = 1;
    r->tape_brewing_brew = brew;
    r->tape_brewing_fuel = fuel;
    return 1;
}

int gm_runtime_tape_inventory(GmRuntime *r, int slot, int item, int count, int meta) {
    if (!r || !isr_slot_ok(slot) ||
        item < 0 || item > 4095 || count < 0 || count > 64 ||
        meta < 0 || meta > 32767 || ((item == 0) != (count == 0))) return 0;
    if (!r->tape_inv_active) {
        isr_init(&r->tape_inv);
        r->tape_inv_active = 1;
    }
    isr_set_stack(&r->tape_inv, slot,
                  count == 0 ? ic_empty() : ic_mk(item, count, meta));
    return 1;
}

void gm_runtime_tape_player_view(GmRuntime *r, int xp_level, float xp_frac, int air,
                                 float portal, int portal_frame, int portal_phase,
                                 int loading, int texture_animations_pinned,
                                 int fire, int creative, int hurt_time,
                                 int max_hurt_time, float hurt_yaw,
                                 float attack_cooldown) {
    if (!r) return;
    r->tape_xp_active = 1;
    r->tape_xp_level = xp_level;
    r->tape_xp_frac = xp_frac;
    r->tape_air = air;
    r->tape_portal = portal;
    r->tape_portal_frame = portal_frame;
    r->tape_portal_phase = portal_phase;
    r->tape_loading = loading;
    r->tape_texture_animations_pinned = texture_animations_pinned;
    r->tape_fire = fire;
    r->tape_creative = creative;
    r->tape_hurt_time = hurt_time;
    r->tape_max_hurt_time = max_hurt_time;
    r->tape_hurt_yaw = hurt_yaw;
    r->tape_attack_cooldown = attack_cooldown;
}

void gm_runtime_tape_potions_clear(GmRuntime *r) {
    if (r) r->tape_potion_count = 0;
}

int gm_runtime_tape_potion(GmRuntime *r, int id, int amplifier, int duration,
                           int show_particles) {
    if (!r || id < 1 || id > 255 || amplifier < 0 || amplifier > 255 ||
        duration < 0 || r->tape_potion_count >= GM_MAX_POTION_EFFECTS) return 0;
    GmPotionEffectView *p = &r->tape_potions[r->tape_potion_count++];
    p->id = id;
    p->amplifier = amplifier;
    p->duration = duration;
    p->hide_particles = show_particles ? 0 : 1;
    return 1;
}

void gm_runtime_tape_armor(GmRuntime *r, int points) {
    if (!r) return;
    if (points > 20) points = 20;
    r->tape_armor_points = points < 0 ? -1 : points;
}

void gm_runtime_apply_tape_view(const GmRuntime *r, GmPlayerView *view) {
    if (!r || !view) return;
    if (r->tape_inv_active) {
        for (int i = 0; i < 9; ++i) {
            ICStack s = isr_get_stack(&r->tape_inv, i);
            view->hotbar_ids[i] = s.item;
            view->hotbar_counts[i] = s.count;
            view->hotbar_meta[i] = s.meta;
        }
    }
    if (r->tape_xp_active) {
        view->xp_level = r->tape_xp_level;
        view->xp_frac = r->tape_xp_frac;
        view->air = r->tape_air;
        view->portal = r->tape_portal;
        view->portal_frame = r->tape_portal_frame;
        view->portal_phase = r->tape_portal_phase;
        view->loading = r->tape_loading;
        view->texture_animations_pinned = r->tape_texture_animations_pinned;
        view->fire = r->tape_fire;
        view->creative = r->tape_creative;
        view->hurt_time = r->tape_hurt_time;
        view->max_hurt_time = r->tape_max_hurt_time;
        view->hurt_yaw = r->tape_hurt_yaw;
        view->attack_cooldown = r->tape_attack_cooldown;
        view->potion_count = r->tape_potion_count;
        memcpy(view->potions, r->tape_potions,
               (size_t)r->tape_potion_count * sizeof view->potions[0]);
        /* AbstractClientPlayer.getFovModifier reads generic.movementSpeed.
         * SPEED and SLOWNESS are operation-2 modifiers, so each multiplier is
         * applied in sequence before the ratio is averaged with 1. Tape
         * scenarios apply their effects before recording starts, after the
         * 0.5/tick EntityRenderer easing has converged. */
        {
            float speed_ratio = 1.0f;
            int has_speed_modifier = 0;
            for (int i = 0; i < r->tape_potion_count; ++i) {
                const GmPotionEffectView *p = &r->tape_potions[i];
                if (p->id == 1) {
                    speed_ratio *= 1.0f + 0.2f * (float)(p->amplifier + 1);
                    has_speed_modifier = 1;
                } else if (p->id == 2) {
                    speed_ratio *= 1.0f - 0.15f * (float)(p->amplifier + 1);
                    has_speed_modifier = 1;
                }
            }
            if (has_speed_modifier)
                view->fov_mult = (speed_ratio + 1.0f) * 0.5f;
        }
        /* Only the recorder can know the real generic.armor total (NBT
         * AttributeModifiers replace an armor item's defaults), so a tape
         * that carries it wins over the item-id guess. */
        if (r->tape_armor_points >= 0)
            view->armor_points = r->tape_armor_points;
        /* Recorder rows are post-tick. GuiIngame rendered the same health
         * transition one updateCounter earlier (portal_phase proves 1:1). */
        view->hud_transition_lead = 1;
    }
}

/* Absolute camera rotation, position/physics untouched. Human-play tape replay
 * sets the recorded per-tick yaw/pitch directly (mouse input is not physics;
 * accumulating dyaw deltas in float would drift off the recorded values). */
void gm_runtime_set_look(GmRuntime *r, float yaw, float pitch) {
    if (!r) return;
    r->player.yaw = yaw; r->player.pitch = pitch;
}

/* Seed health/food from a recorded tape header so a replayed survival session
 * starts from the recorded vitals instead of a fresh 20/20 player. */
void gm_runtime_set_vitals(GmRuntime *r, float health, int food) {
    if (!r) return;
    if (health > 0.0f && r->dead) {
        /* SPacketRespawn + SPacketUpdateHealth: revive before the first
         * destination tick and discard the old entity's burn/hurt state. */
        r->dead = 0;
        r->death_screen_ticks = 0;
        r->player_death_time = 0;
        r->quit_to_title = 0;
        r->player_fire_ticks = -20;
        r->mobs.player_hurt_resistant = 0;
        r->mobs.player_hurt_time = 0;
        r->mobs.player_last_damage = 0.0f;
    }
    r->vitals.health = health;
    r->vitals.foodLevel = food;
    r->player.health = health;
    r->player.food = (float)food;
}

void gm_runtime_set_food_stats(GmRuntime *r, float saturation, float exhaustion) {
    if (!r) return;
    r->vitals.saturation = saturation;
    r->vitals.exhaustion = exhaustion;
}

int gm_runtime_set_food_timer(GmRuntime *r, int food_timer) {
    if (!r || food_timer < 0 || food_timer > 1000000) return 0;
    r->vitals.foodTimer = food_timer;
    return 1;
}

int gm_runtime_set_player_xp(
        GmRuntime *r, int level, float fraction, int total) {
    if (!r || level < 0 || level > 21863 || !isfinite(fraction)
            || fraction < 0.0f || fraction >= 1.0f || total < 0)
        return 0;
    r->player_xp_level = level;
    r->player_xp_frac = fraction;
    r->player_xp_total = total;
    r->mobs.xp_total = total;
    return 1;
}

int gm_runtime_set_player_combat(
        GmRuntime *r, int attack_ticks, int hurt_time,
        int hurt_resistant_time, int death_time, int dead, int deaths) {
    if (!r || attack_ticks < 0 || attack_ticks > 1000000000
            || hurt_time < 0 || hurt_time > 20
            || hurt_resistant_time < 0 || hurt_resistant_time > 20
            || death_time < 0 || death_time > 20
            || (dead != 0 && dead != 1) || deaths < 0
            || (!dead && death_time != 0))
        return 0;
    r->mobs.player_ticks_since_last_swing = attack_ticks;
    r->mobs.player_hurt_time = hurt_time;
    r->mobs.player_hurt_resistant = hurt_resistant_time;
    r->player_death_time = death_time;
    r->dead = dead;
    r->deaths = deaths;
    r->death_screen_ticks = dead && death_time > 0 ? death_time - 1 : 0;
    return 1;
}

int gm_runtime_set_player_absorption(GmRuntime *r, float absorption) {
    if (!r || !isfinite(absorption) || absorption < 0.0f
            || absorption > 1024.0f)
        return 0;
    r->mobs.player_absorption = absorption;
    return 1;
}

int gm_runtime_set_selected_slot(GmRuntime *r, int slot) {
    if (!r || slot < 0 || slot > 8) return 0;
    r->player.inv.current_item = slot;
    return 1;
}

int gm_runtime_set_air(GmRuntime *r, int air) {
    if (!r || air < -20 || air > 300) return 0;
    r->player_air = air;
    return 1;
}

int gm_runtime_set_fire(GmRuntime *r, int fire_ticks) {
    if (!r || fire_ticks < -20 || fire_ticks > 32767) return 0;
    r->player_fire_ticks = fire_ticks;
    return 1;
}

int gm_runtime_set_do_fire_tick(GmRuntime *r, int enabled) {
    if (!r || (enabled != 0 && enabled != 1)) return 0;
    r->do_fire_tick = enabled;
    return 1;
}

int gm_runtime_set_do_entity_drops(GmRuntime *r, int enabled) {
    if (!r || (enabled != 0 && enabled != 1)) return 0;
    r->do_entity_drops = enabled;
    return 1;
}

int gm_runtime_set_do_mob_loot(GmRuntime *r, int enabled) {
    if (!r || (enabled != 0 && enabled != 1)) return 0;
    r->do_mob_loot = enabled;
    return 1;
}

int gm_runtime_set_falling_instant(GmRuntime *r, int enabled) {
    if (!r || (enabled != 0 && enabled != 1)) return 0;
    r->falling_instant = enabled;
    return 1;
}

int gm_runtime_set_position_update_ticks(GmRuntime *r, int ticks, int pending) {
    if (!r || ticks < 0 || ticks > 19 || (pending != 0 && pending != 1))
        return 0;
    r->player_position_update_ticks = ticks;
    r->player_position_packet_pending = pending;
    memset(&r->player_move_packet, 0, sizeof r->player_move_packet);
    if (pending) {
        r->player_move_packet.pending = 1;
        r->player_move_packet.moving = 1;
        r->player_move_packet.on_ground = r->player.ent.onGround;
        r->player_move_packet.x = r->player.ent.posX;
        r->player_move_packet.y = r->player.ent.box.minY;
        r->player_move_packet.z = r->player.ent.posZ;
    }
    return 1;
}

int gm_runtime_spawn_xp_fixture(
        GmRuntime *r, double x, double y, double z,
        double vx, double vy, double vz, int value, int eid,
        int age, int pickup_delay, int color, int target_color) {
    if (!r) return 0;
    r->mobs.active_dimension = r->dimension;
    return gm_mobs_spawn_xp_exact(
        &r->mobs, x, y, z, vx, vy, vz, value, eid,
        age, pickup_delay, color, target_color);
}

int gm_runtime_spawn_item_fixture(
        GmRuntime *r, int eid, double x, double y, double z,
        double vx, double vy, double vz, int item, int count, int meta,
        int age, int pickup_delay, int controlled_stationary) {
    if (!r) return 0;
    return gm_live_spawn_item_exact(
        &r->entities, eid, x, y, z, vx, vy, vz, 0.0f,
        item, count, meta, age, pickup_delay, controlled_stationary);
}

int gm_runtime_spawn_falling_fixture(
        GmRuntime *r, int eid, int block, int meta, int fall_time,
        double x, double y, double z, double vx, double vy, double vz,
        int no_gravity, int no_ground) {
    if (!r || eid <= 0
            || (block != 12 && block != 13 && block != 122 && block != 145)
            || (block == 145 ? meta < 0 || meta > 11 : meta != 0)
            || fall_time < 0 || fall_time > 600
            || !isfinite(x) || !isfinite(y) || !isfinite(z)
            || !isfinite(vx) || !isfinite(vy) || !isfinite(vz)
            || (no_gravity != 0 && no_gravity != 1)
            || (no_ground != 0 && no_ground != 1)
            || r->falling_block_count >= GM_RUNTIME_FALLING_BLOCKS)
        return 0;
    for (int i = 0; i < GM_RUNTIME_FALLING_BLOCKS; ++i) {
        GmRuntimeFallingBlock *falling = &r->falling_blocks[i];
        if (falling->active)
            continue;
        memset(falling, 0, sizeof *falling);
        falling->active = 1;
        falling->eid = eid;
        falling->block = block;
        falling->meta = meta;
        falling->fall_time = fall_time;
        falling->should_drop_item = 1;
        falling->hurt_entities = block == 145;
        {
            JavaRandom random;
            jrand_set(&random, runtime_entity_constructor_seed(
                r, eid, UINT64_C(0x46414C4C494E4742)));
            falling->random_seed48 = random.seed;
        }
        falling->no_gravity = no_gravity;
        falling->no_ground = no_ground;
        falling->origin_x = (int)floor(x);
        falling->origin_y = (int)floor(y);
        falling->origin_z = (int)floor(z);
        runtime_falling_set_position(falling, x, y, z);
        falling->vx = vx;
        falling->vy = vy;
        falling->vz = vz;
        ++r->falling_block_count;
        return 1;
    }
    return 0;
}

int gm_runtime_spawn_arrow_fixture(
        GmRuntime *r, int eid, double x, double y, double z,
        double vx, double vy, double vz, int controlled_stationary,
        int fire_ticks) {
    if (!r || eid <= 0 || controlled_stationary != 1
            || vx != 0.0 || vy != 0.0 || vz != 0.0
            || fire_ticks < 0 || fire_ticks > 32767)
        return 0;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        GmRuntimeProjectile *p = &r->projectiles[i];
        if (p->active)
            continue;
        memset(p, 0, sizeof *p);
        p->active = 1;
        p->type = 1;
        p->eid = eid;
        p->controlled_stationary = 1;
        p->fire_ticks = fire_ticks;
        p->x = x;
        p->y = y;
        p->z = z;
        return 1;
    }
    return 0;
}

int gm_runtime_spawn_primed_tnt_fixture(
        GmRuntime *r, int eid, double x, double y, double z,
        double vx, double vy, double vz, int fuse) {
    if (!r || eid <= 0 || fuse <= 0 || fuse > 32767
            || !isfinite(x) || !isfinite(y) || !isfinite(z)
            || !isfinite(vx) || !isfinite(vy) || !isfinite(vz)
            || r->primed_tnt_count >= GM_RUNTIME_PRIMED_TNT)
        return 0;
    for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i) {
        GmRuntimePrimedTnt *tnt = &r->primed_tnt[i];
        if (tnt->active)
            continue;
        memset(tnt, 0, sizeof *tnt);
        tnt->active = 1;
        tnt->dimension = r->dimension;
        tnt->eid = eid;
        tnt->fuse = fuse;
        tnt->x = x;
        tnt->y = y;
        tnt->z = z;
        tnt->vx = vx;
        tnt->vy = vy;
        tnt->vz = vz;
        ++r->primed_tnt_count;
        return 1;
    }
    return 0;
}

int gm_runtime_spawn_end_crystal_fixture(
        GmRuntime *r, int eid, double x, double y, double z,
        int inner_rotation, int show_bottom, int has_beam,
        int beam_x, int beam_y, int beam_z) {
    if (!r || eid <= 0 || inner_rotation < 0
            || (show_bottom != 0 && show_bottom != 1)
            || (has_beam != 0 && has_beam != 1)
            || !isfinite(x) || !isfinite(y) || !isfinite(z)
            || r->end_crystal_count >= GM_RUNTIME_END_CRYSTALS)
        return 0;
    for (int i = 0; i < GM_RUNTIME_END_CRYSTALS; ++i) {
        GmRuntimeEndCrystal *crystal = &r->end_crystals[i];
        if (crystal->active) continue;
        memset(crystal, 0, sizeof *crystal);
        crystal->active = 1;
        crystal->dimension = r->dimension;
        crystal->eid = eid;
        crystal->inner_rotation = inner_rotation;
        crystal->show_bottom = show_bottom;
        crystal->has_beam = has_beam;
        crystal->beam_x = beam_x;
        crystal->beam_y = beam_y;
        crystal->beam_z = beam_z;
        crystal->x = x;
        crystal->y = y;
        crystal->z = z;
        ++r->end_crystal_count;
        return 1;
    }
    return 0;
}

int gm_runtime_spawn_small_fireball_fixture(
        GmRuntime *r, int eid, double x, double y, double z,
        double vx, double vy, double vz, double ax, double ay, double az) {
    if (!r || eid <= 0 || !isfinite(x) || !isfinite(y) || !isfinite(z)
            || !isfinite(vx) || !isfinite(vy) || !isfinite(vz)
            || !isfinite(ax) || !isfinite(ay) || !isfinite(az))
        return 0;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        GmRuntimeProjectile *p = &r->projectiles[i];
        if (p->active)
            continue;
        memset(p, 0, sizeof *p);
        p->active = 1;
        p->type = 3;
        p->eid = eid;
        p->x = x;
        p->y = y;
        p->z = z;
        p->vx = vx;
        p->vy = vy;
        p->vz = vz;
        p->ax = ax;
        p->ay = ay;
        p->az = az;
        return 1;
    }
    return 0;
}

int gm_runtime_spawn_potion_fixture(
        GmRuntime *r, int eid, int potion_item, int potion_type,
        double x, double y, double z, double vx, double vy, double vz,
        int age) {
    if (!r || eid <= 0
            || (potion_item != TB_SPLASH_POTION
                && potion_item != TB_LINGERING_POTION)
            || potion_type < TB_PT_EMPTY || potion_type >= TB_PT_COUNT
            || age < 0 || age >= 1200
            || !isfinite(x) || !isfinite(y) || !isfinite(z)
            || !isfinite(vx) || !isfinite(vy) || !isfinite(vz))
        return 0;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i) {
        GmRuntimeProjectile *p = &r->projectiles[i];
        if (p->active) continue;
        memset(p, 0, sizeof *p);
        p->active = 1;
        p->type = 6;
        p->age = age;
        p->eid = eid;
        p->potion_item = potion_item;
        p->potion_type = potion_type;
        p->x = x;
        p->y = y;
        p->z = z;
        p->vx = vx;
        p->vy = vy;
        p->vz = vz;
        return 1;
    }
    return 0;
}

int gm_runtime_spawn_area_effect_cloud_fixture(
        GmRuntime *r, int eid, int potion_type, double x, double y, double z,
        int age, int duration, int wait_time, int reapplication_delay,
        float radius, float radius_on_use, float radius_per_tick,
        int next_application) {
    if (!r || eid <= 0
            || potion_type < TB_PT_EMPTY || potion_type >= TB_PT_COUNT
            || age < 0 || duration <= 0 || wait_time < 0
            || reapplication_delay < 0 || next_application < 0
            || (long long)age >= (long long)wait_time + duration
            || !isfinite(x) || !isfinite(y) || !isfinite(z)
            || !isfinite(radius) || radius < 0.5F
            || !isfinite(radius_on_use) || !isfinite(radius_per_tick)
            || r->area_effect_cloud_count >= GM_RUNTIME_AREA_EFFECT_CLOUDS)
        return 0;
    for (int i = 0; i < GM_RUNTIME_AREA_EFFECT_CLOUDS; ++i) {
        GmRuntimeAreaEffectCloud *cloud = &r->area_effect_clouds[i];
        if (cloud->state.active) continue;
        memset(cloud, 0, sizeof *cloud);
        cloud->eid = eid;
        cloud->potion_type = potion_type;
        cloud->state.active = 1;
        cloud->state.age = age;
        cloud->state.duration = duration;
        cloud->state.wait_time = wait_time;
        cloud->state.reapplication_delay = reapplication_delay;
        cloud->state.next_application = next_application;
        cloud->state.radius = radius;
        cloud->state.radius_on_use = radius_on_use;
        cloud->state.radius_per_tick = radius_per_tick;
        cloud->x = x;
        cloud->y = y;
        cloud->z = z;
        ++r->area_effect_cloud_count;
        return 1;
    }
    return 0;
}

int gm_runtime_spawn_mob_fixture(
        GmRuntime *r, int type, int eid, double x, double y, double z,
        double vx, double vy, double vz, float yaw, float health, int no_ai,
        int hurt_time, int death_time, int hurt_resistant_time) {
    if (!r) return 0;
    r->mobs.active_dimension = r->dimension;
    int slot = gm_mobs_spawn_exact(
        &r->mobs, type, eid, x, y, z, vx, vy, vz, yaw, health, no_ai,
        hurt_time, death_time, hurt_resistant_time);
    if (slot < 0) return 0;
    r->controlled_mobs_enabled = 1;
    return 1;
}

int gm_runtime_spawn_villager_fixture(
        GmRuntime *r, int eid, double x, double y, double z,
        double vx, double vy, double vz, float yaw, float health,
        int hurt_time, int death_time, int hurt_resistant_time,
        int profession, int living_sound_time,
        uint64_t seed48, int have_next_gaussian,
        double next_gaussian) {
    GmRuntimeVillageResident *resident;
    int slot;
    if (!r || profession < 0 || profession > 5
            || living_sound_time < -80 || living_sound_time > 1000
            || seed48 > GM_JAVA_RANDOM_MASK
            || (have_next_gaussian != 0 && have_next_gaussian != 1)
            || r->village_resident_count >= GM_RUNTIME_VILLAGE_RESIDENTS)
        return 0;
    r->mobs.active_dimension = r->dimension;
    slot = gm_mobs_spawn_exact(
        &r->mobs, GM_MOB_VILLAGER, eid, x, y, z, vx, vy, vz, yaw,
        health, 1, hurt_time, death_time, hurt_resistant_time);
    if (slot < 0) return 0;
    r->mobs.villager_profession[slot] = (unsigned char)profession;
    r->mobs.entity_living_sound_time[slot] = living_sound_time;
    if (!gm_mobs_set_entity_random_state(
            &r->mobs, eid, seed48, have_next_gaussian, next_gaussian))
        return 0;
    resident = &r->village_residents[r->village_resident_count++];
    memset(resident, 0, sizeof *resident);
    resident->x = mc_floor(x);
    resident->y = mc_floor(y);
    resident->z = mc_floor(z);
    resident->eid = eid;
    resident->profession = (unsigned char)profession;
    r->controlled_mobs_enabled = 1;
    return 1;
}

int gm_runtime_set_mob_fire_ticks(
        GmRuntime *r, int eid, int fire_ticks) {
    if (!r) return 0;
    return gm_mobs_set_entity_fire_ticks(&r->mobs, eid, fire_ticks);
}

int gm_runtime_set_mob_air(GmRuntime *r, int eid, int air) {
    if (!r) return 0;
    return gm_mobs_set_air(&r->mobs, eid, air);
}

int gm_runtime_set_sheep_state(
        GmRuntime *r, int eid, int fleece_color, int sheared) {
    if (!r) return 0;
    return gm_mobs_set_sheep_state(
        &r->mobs, eid, fleece_color, sheared);
}

int gm_runtime_set_mob_growing_age(
        GmRuntime *r, int eid, int growing_age) {
    if (!r) return 0;
    return gm_mobs_set_growing_age(&r->mobs, eid, growing_age);
}

int gm_runtime_set_mob_recent_hit_state(
        GmRuntime *r, int eid, int recently_hit, int attacking_player) {
    if (!r) return 0;
    return gm_mobs_set_recent_hit_state(
        &r->mobs, eid, recently_hit, attacking_player);
}

int gm_runtime_spawn_boat_fixture(
        GmRuntime *r, int eid, double x, double y, double z, float yaw) {
    if (!r) return 0;
    r->mobs.active_dimension = r->dimension;
    if (gm_mobs_spawn_boat_exact(&r->mobs, eid, x, y, z, yaw) < 0)
        return 0;
    r->controlled_mobs_enabled = 1;
    return 1;
}

int gm_runtime_set_entity_id_cursor(GmRuntime *r, int next_entity_id) {
    if (!r || next_entity_id < 0)
        return 0;
    r->next_entity_id = next_entity_id;
    return 1;
}

int gm_runtime_set_world_random_seed48(GmRuntime *r, uint64_t seed48) {
    if (!r || seed48 > GM_JAVA_RANDOM_MASK)
        return 0;
    r->world_random_seed48 = seed48;
    r->world_random_have_gaussian = 0;
    r->world_random_gaussian = 0.0;
    return 1;
}

int gm_runtime_set_world_random_gaussian(
        GmRuntime *r, int have_next_gaussian, double next_gaussian) {
    if (!r || (have_next_gaussian != 0 && have_next_gaussian != 1)
            || !isfinite(next_gaussian))
        return 0;
    r->world_random_have_gaussian = have_next_gaussian;
    r->world_random_gaussian = next_gaussian;
    return 1;
}

int gm_runtime_set_math_random_seed48(GmRuntime *r, uint64_t seed48) {
    if (!r || seed48 > GM_JAVA_RANDOM_MASK)
        return 0;
    r->math_random_seed48 = seed48;
    return 1;
}

int gm_runtime_set_player_random_seed48(GmRuntime *r, uint64_t seed48) {
    if (!r || seed48 >= (UINT64_C(1) << 48)) return 0;
    jrand_set_seed48(&r->mobs.player_random, seed48);
    return 1;
}

int gm_runtime_set_next_explosion_random_seed48(
        GmRuntime *r, uint64_t seed48) {
    if (!r || seed48 > GM_JAVA_RANDOM_MASK)
        return 0;
    r->next_explosion_random_valid = 1;
    r->next_explosion_random_seed48 = seed48;
    return 1;
}

int gm_runtime_set_next_fireball_random_state(
        GmRuntime *r, uint64_t seed48, int have_next_gaussian,
        double next_gaussian) {
    if (!r || (have_next_gaussian != 0 && have_next_gaussian != 1))
        return 0;
    r->next_fireball_random_valid = 1;
    r->next_fireball_random_seed48 = seed48 & GM_JAVA_RANDOM_MASK;
    r->next_fireball_random_have_gaussian = have_next_gaussian;
    r->next_fireball_random_gaussian = next_gaussian;
    return 1;
}

int gm_runtime_set_next_potion_random_state(
        GmRuntime *r, uint64_t seed48, int have_next_gaussian,
        double next_gaussian) {
    if (!r || (have_next_gaussian != 0 && have_next_gaussian != 1))
        return 0;
    r->next_potion_random_valid = 1;
    r->next_potion_random_seed48 = seed48 & GM_JAVA_RANDOM_MASK;
    r->next_potion_random_have_gaussian = have_next_gaussian;
    r->next_potion_random_gaussian = next_gaussian;
    return 1;
}

int gm_runtime_set_next_falling_random_seed48(
        GmRuntime *r, uint64_t seed48) {
    if (!r || seed48 > GM_JAVA_RANDOM_MASK)
        return 0;
    r->next_falling_random_valid = 1;
    r->next_falling_random_seed48 = seed48;
    return 1;
}

int gm_runtime_set_next_shears_random_seed48(
        GmRuntime *r, uint64_t seed48) {
    if (!r || seed48 > GM_JAVA_RANDOM_MASK)
        return 0;
    r->next_shears_random_valid = 1;
    r->next_shears_random_seed48 = seed48;
    return 1;
}

int gm_runtime_set_block_random_seed48(GmRuntime *r, uint64_t seed48) {
    if (!r || seed48 > GM_JAVA_RANDOM_MASK)
        return 0;
    r->block_random_seed48 = seed48;
    return 1;
}

int gm_runtime_set_world_update_lcg(GmRuntime *r, int32_t update_lcg) {
    if (!r)
        return 0;
    r->world_update_lcg = update_lcg;
    return 1;
}

void gm_runtime_begin_controlled_input(GmRuntime *r) {
    if (!r)
        return;
    r->controlled_input_valid = 0;
    r->controlled_input_before_valid = 1;
    r->controlled_input_before_entity_id = r->next_entity_id;
    r->controlled_input_before_world_seed48 = r->world_random_seed48;
    r->controlled_input_before_math_seed48 = r->math_random_seed48;
    r->controlled_input_before_block_seed48 = r->block_random_seed48;
    r->controlled_input_before_update_lcg = r->world_update_lcg;
}

void gm_runtime_capture_controlled_input(GmRuntime *r) {
    if (!r)
        return;
    if (!r->controlled_input_before_valid)
        gm_runtime_begin_controlled_input(r);
    r->controlled_input_valid = 1;
    r->controlled_input_tick = r->tick + 1;
    r->controlled_input_entity_id = r->next_entity_id;
    r->controlled_input_world_seed48 = r->world_random_seed48;
    r->controlled_input_math_seed48 = r->math_random_seed48;
    r->controlled_input_block_seed48 = r->block_random_seed48;
    r->controlled_input_update_lcg = r->world_update_lcg;
    r->controlled_input_before_valid = 0;
}

static int runtime_random_tick_leaves(
        GmRuntime *r, int x, int y, int z, int leaf_id) {
    signed char surroundings[9 * 9 * 9];
    static const int neighbor_dx[6] = {-1, 1, 0, 0, 0, 0};
    static const int neighbor_dy[6] = {0, 0, -1, 1, 0, 0};
    static const int neighbor_dz[6] = {0, 0, 0, 0, -1, 1};
    int meta = gm_world_meta(r->world, x, y, z);
    if (!gm_block_meta_canonical_1_11_2(leaf_id, meta))
        return 0;
    if ((meta & 8) == 0 || (meta & 4) != 0)
        return 1;

    /* BlockLeaves fills a 9-cube around the callback and propagates six-face
     * distance from BlockLog support for exactly four rounds. A compact local
     * cube is equivalent for the center result and avoids the vanilla shared
     * 32^3 scratch allocation. This controlled callback runs in the loaded
     * player proof region, satisfying Java's surrounding 11-cube load guard. */
    for (int ox = -4; ox <= 4; ++ox)
        for (int oy = -4; oy <= 4; ++oy)
            for (int oz = -4; oz <= 4; ++oz) {
                int block = gm_world_block(
                    r->world, x + ox, y + oy, z + oz);
                int index = (ox + 4) * 81 + (oy + 4) * 9 + oz + 4;
                surroundings[index] =
                    (block == 17 || block == 162) ? 0
                    : (block == 18 || block == 161) ? -2 : -1;
            }
    for (int distance = 1; distance <= 4; ++distance)
        for (int ix = 0; ix < 9; ++ix)
            for (int iy = 0; iy < 9; ++iy)
                for (int iz = 0; iz < 9; ++iz) {
                    int index = ix * 81 + iy * 9 + iz;
                    if (surroundings[index] != distance - 1)
                        continue;
                    for (int face = 0; face < 6; ++face) {
                        int nx = ix + neighbor_dx[face];
                        int ny = iy + neighbor_dy[face];
                        int nz = iz + neighbor_dz[face];
                        int neighbor_index;
                        if (nx < 0 || nx >= 9
                                || ny < 0 || ny >= 9
                                || nz < 0 || nz >= 9)
                            continue;
                        neighbor_index = nx * 81 + ny * 9 + nz;
                        if (surroundings[neighbor_index] == -2)
                            surroundings[neighbor_index] =
                                (signed char)distance;
                    }
                }
    if (surroundings[4 * 81 + 4 * 9 + 4] >= 0) {
        gm_world_set_block_meta(r->world, x, y, z, leaf_id, meta & ~8);
        return 1;
    }
    if (!runtime_redstone_piston_apply_destroy_payload(
            r, x, y, z, leaf_id, meta))
        return 0;
    gm_world_set_block_meta(r->world, x, y, z, 0, 0);
    runtime_redstone_break_replaced_state(
        r, x, y, z, leaf_id, meta);
    return 1;
}

int gm_runtime_random_tick_block(
        GmRuntime *r, int x, int y, int z, int expected_block) {
    if (!r || !r->world || y < 0 || y > 255
            || gm_world_block(r->world, x, y, z) != expected_block)
        return 0;
    /*
     * First promoted callback: BlockCrops (wheat, id 59). This is the complete
     * vanilla 1.11.2 growth body, including light, fertile-soil weighting,
     * crop-layout penalty, float truncation, and java.util.Random.nextInt.
     * Loaded-chunk random-tick selection is intentionally a separate layer.
     */
    if (expected_block == 51) {
        if (!r->do_fire_tick)
            return 1;
        if (!runtime_fire_proof_supported(r, x, y, z))
            return 0;
        runtime_tick_fire(r, x, y, z);
        return 1;
    }
    if (expected_block == 18 || expected_block == 161)
        return runtime_random_tick_leaves(
            r, x, y, z, expected_block);
    if (expected_block != 59)
        return 0;
    int age = gm_world_meta(r->world, x, y, z);
    if (age < 0 || age > 7)
        return 0;
    int light = gm_world_sky_light(r->world, x, y + 1, z);
    int block_light = gm_world_block_light(r->world, x, y + 1, z);
    if (block_light > light)
        light = block_light;
    if (light < 9 || age >= 7)
        return 1;
    float chance = runtime_crop_growth_chance(
        r, x, y, z, expected_block);
    int bound = (int)(25.0f / chance) + 1;
    if (runtime_java_random_next_int(r, bound) == 0)
        gm_world_set_block_meta(
            r->world, x, y, z, expected_block, age + 1);
    return 1;
}

int gm_runtime_random_tick_selection(
        GmRuntime *r, int x, int y, int z, int expected_block,
        int lcg_advances_before) {
    if (!r || !r->world || lcg_advances_before < 0
            || lcg_advances_before > 1000000
            || (expected_block != 2 && expected_block != 81)
            || gm_world_block(r->world, x, y, z) != expected_block)
        return 0;
    for (int i = 0; i < lcg_advances_before; ++i)
        (void)runtime_world_update_lcg_step(r);
    uint32_t selected = runtime_world_update_lcg_step(r) >> 2;
    int selected_x = (x & ~15) + (int)(selected & 15);
    int selected_z = (z & ~15) + (int)((selected >> 8) & 15);
    int selected_y = (y & ~15) + (int)((selected >> 16) & 15);
    if (selected_x != x || selected_y != y || selected_z != z)
        return 0;
    /*
     * Light-independent selector proof: BlockCactus age 0 increments to 1
     * when the cell above is air and the column is shorter than three. This
     * is the ordinary callback's deterministic age<15 arm; the age-15 growth
     * and neighbor-validity branches remain separate slices.
     */
    if (expected_block == 81) {
        int age = gm_world_meta(r->world, x, y, z);
        if (age != 0 || y >= 255
                || gm_world_block(r->world, x, y + 1, z) != 0)
            return 0;
        int height = 1;
        while (height < 3 && y - height >= 0
                && gm_world_block(r->world, x, y - height, z) == 81)
            ++height;
        if (height >= 3)
            return 1;
        gm_world_set_block_meta(r->world, x, y, z, 81, 1);
        return 1;
    }
    /*
     * First natural-selector callback: BlockGrass's deterministic decay arm.
     * The proof fixture admits only a stone cap, whose light opacity is >2;
     * spread (light >= 9 and 12 Random samples) remains separate work.
     */
    if (y >= 255 || gm_world_block(r->world, x, y + 1, z) != 1)
        return 0;
    int light = gm_world_sky_light(r->world, x, y + 1, z);
    int block_light = gm_world_block_light(r->world, x, y + 1, z);
    if (block_light > light)
        light = block_light;
    if (light < 4)
        gm_world_set_block_meta(r->world, x, y, z, 3, 0);
    return 1;
}

int gm_runtime_schedule_tick(
        GmRuntime *r, int x, int y, int z, int block, long long time,
        int priority, long long order) {
    if (!r || !r->scheduled_ticks || y < 1 || y > 255
            || (block != 1 && block != 8 && block != 10
                && block != 12 && block != 13 && block != 51
                && block != 122
                && block != 145
                && block != 23
                && block != 158
                && block != 28
                && block != 70 && block != 72
                && block != 147 && block != 148
                && block != 75 && block != 76
                && block != 77 && block != 93 && block != 94
                && block != 124 && block != 143
                && block != 149 && block != 150
                && block != 131 && block != 132
                && block != 199 && block != 200
                && block != 218)
            || time < 0
            || priority < -128 || priority > 127 || order < 0)
        return 0;
    if (block == 8 && !runtime_water_supported(r, x, y, z))
        return 0;
    if (block == 10
            && !runtime_lava_source_flat_supported(r, x, y, z)
            && !runtime_lava_above_enclosed_water_supported(r, x, y, z))
        return 0;
    if ((block == 12 || block == 13)
            && runtime_falling_block_landing_y(
                r, x, y, z, block, NULL) < 0.0)
        return 0;
    if (block == 122
            && (gm_world_block(r->world, x, y, z) != 122
                || gm_world_meta(r->world, x, y, z) != 0))
        return 0;
    if (block == 145
            && (gm_world_block(r->world, x, y, z) != 145
                || gm_world_meta(r->world, x, y, z) < 0
                || gm_world_meta(r->world, x, y, z) > 11
                || (runtime_falling_can_fall_through(
                        gm_world_block(r->world, x, y - 1, z))
                    && (!r->falling_instant
                        || runtime_falling_block_landing_y(
                            r, x, y, z, block, NULL) < 0.0))))
        return 0;
    if (block == 51 && r->do_fire_tick
            && !runtime_fire_proof_supported(r, x, y, z))
        return 0;
    if (block == 158 && !runtime_dropper_supported(r, x, y, z))
        return 0;
    if (block == 23 && !runtime_dispenser_supported(r, x, y, z))
        return 0;
    if (block == 28
            && (gm_world_block(r->world, x, y, z) != 28
                || (gm_world_meta(r->world, x, y, z) & 8) == 0))
        return 0;
    if (runtime_redstone_is_pressure_plate(block)
            && !runtime_redstone_pressure_plate_callback_supported(
                r, x, y, z, block))
        return 0;
    if ((block == 131 || block == 132)
            && (gm_world_block(r->world, x, y, z) != block
                || !gm_block_meta_canonical_1_11_2(
                    block, gm_world_meta(r->world, x, y, z))))
        return 0;
    if ((block == 75 || block == 76)
            && !runtime_redstone_torch_callback_supported(
                r, x, y, z, block))
        return 0;
    if ((block == 77 || block == 143)
            && !runtime_redstone_button_supported(
                r, x, y, z, block, 1))
        return 0;
    if ((block == 93 || block == 94)
            && !runtime_redstone_repeater_supported(
                r, x, y, z, block))
        return 0;
    if ((block == 149 || block == 150)
            && !runtime_redstone_comparator_supported(
                r, x, y, z, block))
        return 0;
    if (block == 218
            && !runtime_redstone_observer_supported(r, x, y, z))
        return 0;
    if (block == 124
            && !runtime_redstone_lamp_off_supported(r, x, y, z))
        return 0;
    if (block == 200
            && (gm_world_block(r->world, x, y, z) != 200
                || !gm_block_meta_canonical_1_11_2(
                    200, gm_world_meta(r->world, x, y, z))))
        return 0;
    if (block == 199
            && (gm_world_block(r->world, x, y, z) != 199
                || gm_world_meta(r->world, x, y, z) != 0))
        return 0;
    return runtime_schedule_tick_insert(
        r, x, y, z, block, time, priority, order);
}

int gm_runtime_scheduled_tick_count(const GmRuntime *r) {
    return r ? r->scheduled_tick_count : 0;
}

int gm_runtime_scheduled_tick_get(
        const GmRuntime *r, int index, GmRuntimeScheduledTick *out) {
    if (!r || !out || index < 0 || index >= r->scheduled_tick_count)
        return 0;
    *out = r->scheduled_ticks[index];
    return 1;
}

int gm_runtime_moving_piston_load(
        GmRuntime *r, int dimension, int x, int y, int z,
        int moved_block, int moved_meta, int facing,
        int extending, int source, float progress, float last_progress) {
    GmWorld *world;
    if (!r || dimension < -1 || dimension > 1
            || y < 0 || y > 255
            || moved_block <= 0 || moved_block > 4095
            || moved_meta < 0 || moved_meta > 15
            || !gm_block_meta_canonical_1_11_2(moved_block, moved_meta)
            || facing < 0 || facing > 5
            || (extending != 0 && extending != 1)
            || (source != 0 && source != 1)
            || !isfinite(progress) || !isfinite(last_progress)
            || progress < 0.0f || progress > 1.0f
            || last_progress < 0.0f || last_progress > progress
            || progress - last_progress > 0.5f
            || r->piston_count >= GM_RUNTIME_PISTONS)
        return 0;
    world = r->worlds[dimension + 1];
    if (!world || gm_world_block(world, x, y, z) != 36
            || (gm_world_meta(world, x, y, z) & 7) != facing)
        return 0;
    for (int i = 0; i < r->piston_count; ++i)
        if (r->pistons[i].active
                && r->pistons[i].dimension == dimension
                && r->pistons[i].x == x && r->pistons[i].y == y
                && r->pistons[i].z == z)
            return 0;
    r->pistons[r->piston_count++] = (GmRuntimePiston){
        .active = 1,
        .dimension = dimension,
        .x = x,
        .y = y,
        .z = z,
        .moved_block = moved_block,
        .moved_meta = moved_meta,
        .facing = facing,
        .extending = extending,
        .source = source,
        .progress = progress,
        .last_progress = last_progress,
    };
    return 1;
}

int gm_runtime_moving_piston_count(const GmRuntime *r) {
    return r ? r->piston_count : 0;
}

int gm_runtime_moving_piston_get(
        const GmRuntime *r, int index, GmRuntimePiston *out) {
    if (!r || !out || index < 0 || index >= r->piston_count)
        return 0;
    *out = r->pistons[index];
    return 1;
}

int gm_runtime_comparator_count(const GmRuntime *r) {
    return r ? r->comparator_count : 0;
}

int gm_runtime_comparator_get(
        const GmRuntime *r, int index, GmRuntimeComparator *out) {
    if (!r || !out || index < 0 || index >= r->comparator_count)
        return 0;
    *out = r->comparators[index];
    return 1;
}

int gm_runtime_comparator_set_output(
        GmRuntime *r, int dimension, int x, int y, int z,
        int output_signal) {
    if (!r || dimension < -1 || dimension > 1
            || y < 0 || y > 255
            || output_signal < 0 || output_signal > 15)
        return 0;
    GmWorld *world = r->worlds[dimension + 1];
    GmRuntimeComparator *entry =
        runtime_comparator_find_mut(r, dimension, x, y, z);
    if (!world || !entry)
        return 0;
    int block = gm_world_block(world, x, y, z);
    if (block != 149 && block != 150)
        return 0;
    entry->output_signal = output_signal;
    return 1;
}

int gm_runtime_chest_count(const GmRuntime *r) {
    int count = 0;
    if (!r || !r->chests) return 0;
    for (int i = 0; i < r->chests_cap; ++i)
        if (r->chests[i].active
                && runtime_is_chest_block(gm_world_block(
                    r->world, r->chests[i].wx,
                    r->chests[i].wy, r->chests[i].wz)))
            count++;
    return count;
}

int gm_runtime_chest_get(
        const GmRuntime *r, int index, GmRuntimeChest *out) {
    int at = 0;
    if (!r || !r->chests || !out || index < 0) return 0;
    for (int i = 0; i < r->chests_cap; ++i) {
        const GmRuntimeChest *chest = &r->chests[i];
        if (!chest->active
                || !runtime_is_chest_block(gm_world_block(
                    r->world, chest->wx, chest->wy, chest->wz)))
            continue;
        if (at++ == index) {
            *out = *chest;
            return 1;
        }
    }
    return 0;
}

int gm_runtime_furnace_count(const GmRuntime *r) {
    int count = 0;
    if (!r) return 0;
    for (int i = 0; i < GM_RUNTIME_FURNACES; ++i) {
        const GmRuntimeFurnace *furnace = &r->furnaces[i];
        int id;
        if (!furnace->active) continue;
        id = gm_world_block(
            r->world, furnace->wx, furnace->wy, furnace->wz);
        if (id == 61 || id == 62) count++;
    }
    return count;
}

int gm_runtime_furnace_get(
        const GmRuntime *r, int index, GmRuntimeFurnace *out) {
    int at = 0;
    if (!r || !out || index < 0) return 0;
    for (int i = 0; i < GM_RUNTIME_FURNACES; ++i) {
        const GmRuntimeFurnace *furnace = &r->furnaces[i];
        int id;
        if (!furnace->active) continue;
        id = gm_world_block(
            r->world, furnace->wx, furnace->wy, furnace->wz);
        if (id != 61 && id != 62) continue;
        if (at++ == index) {
            *out = *furnace;
            return 1;
        }
    }
    return 0;
}

int gm_runtime_furnace_set_slot(
        GmRuntime *r, int dimension, int x, int y, int z,
        int slot, int item, int count, int meta,
        int burn_time, int current_burn_time,
        int cook_time, int total_cook_time) {
    int index = -1;
    int free_slot = -1;
    int block;
    SRStack stack;
    if (!r || !r->world || dimension != r->dimension
            || y < 0 || y > 255
            || slot < 0 || slot >= FURNACE_LIVE_SLOT_COUNT
            || item < 0 || item > 4095
            || count < 0 || count > FFT_STACK_LIMIT
            || meta < 0 || meta > 32767
            || (item == 0) != (count == 0)
            || burn_time < 0 || burn_time > 32767
            || current_burn_time < 0 || current_burn_time > 32767
            || cook_time < 0 || cook_time > 32767
            || total_cook_time < 0 || total_cook_time > 32767)
        return 0;
    block = gm_world_block(r->world, x, y, z);
    if (block != 61 && block != 62)
        return 0;
    if (item != 0 && count > tec_max_stack_size(item))
        return 0;
    for (int i = 0; i < GM_RUNTIME_FURNACES; ++i) {
        GmRuntimeFurnace *candidate = &r->furnaces[i];
        if (candidate->active
                && candidate->wx == x
                && candidate->wy == y
                && candidate->wz == z) {
            index = i;
            break;
        }
        if (!candidate->active && free_slot < 0)
            free_slot = i;
        else if (candidate->active && free_slot < 0) {
            int candidate_block = gm_world_block(
                r->world, candidate->wx, candidate->wy, candidate->wz);
            if (candidate_block != 61 && candidate_block != 62)
                free_slot = i;
        }
    }
    if (index < 0) index = free_slot;
    if (index < 0) return 0;
    GmRuntimeFurnace *furnace = &r->furnaces[index];
    if (!furnace->active
            || furnace->wx != x || furnace->wy != y || furnace->wz != z) {
        furnace->active = 1;
        furnace->wx = x;
        furnace->wy = y;
        furnace->wz = z;
        furnace_live_init(&furnace->state);
    }
    stack = item == 0 ? sr_empty() : sr_mk(item, count, meta);
    if (slot == FURNACE_LIVE_SLOT_INPUT)
        furnace->state.input = stack;
    else if (slot == FURNACE_LIVE_SLOT_FUEL)
        furnace->state.fuel = stack;
    else
        furnace->state.output = stack;
    furnace->state.burn_time = burn_time;
    furnace->state.current_burn_time = current_burn_time;
    furnace->state.cook_time = cook_time;
    furnace->state.total_cook = total_cook_time;
    return 1;
}

int gm_runtime_static_container_count(const GmRuntime *r) {
    int count = 0;
    if (!r || !r->static_containers) return 0;
    for (int i = 0; i < r->static_containers_cap; ++i) {
        const GmRuntimeStaticContainer *container =
            &r->static_containers[i];
        if (container->active
                && container->dimension == r->dimension
                && gm_world_block(
                    r->world, container->wx,
                    container->wy, container->wz) == container->block
                && container->size
                    == runtime_static_container_size_for_block(
                        container->block))
            count++;
    }
    return count;
}

int gm_runtime_static_container_get(
        const GmRuntime *r, int index,
        GmRuntimeStaticContainer *out) {
    int at = 0;
    if (!r || !r->static_containers || !out || index < 0)
        return 0;
    for (int i = 0; i < r->static_containers_cap; ++i) {
        const GmRuntimeStaticContainer *container =
            &r->static_containers[i];
        if (!container->active
                || container->dimension != r->dimension
                || gm_world_block(
                    r->world, container->wx,
                    container->wy, container->wz) != container->block
                || container->size
                    != runtime_static_container_size_for_block(
                        container->block))
            continue;
        if (at++ == index) {
            *out = *container;
            return 1;
        }
    }
    return 0;
}

static void runtime_static_container_remove(
        GmRuntime *r, int dimension, int x, int y, int z) {
    if (!r || !r->static_containers) return;
    for (int i = 0; i < r->static_containers_cap; ++i) {
        GmRuntimeStaticContainer *container =
            &r->static_containers[i];
        if (container->active && container->dimension == dimension
                && container->wx == x && container->wy == y
                && container->wz == z) {
            gm_nbt_blob_clear(&container->item_tag);
            memset(container, 0, sizeof *container);
            return;
        }
    }
}

int gm_runtime_static_container_set_slot(
        GmRuntime *r, int dimension, int x, int y, int z,
        int slot, int item, int count, int meta) {
    int block;
    int size;
    int index = -1;
    int free_slot = -1;
    if (!r || !r->world || dimension != r->dimension
            || y < 0 || y > 255
            || item < 0 || item > 4095
            || count < 0 || count > 64
            || meta < 0 || meta > 32767
            || (item == 0) != (count == 0))
        return 0;
    block = gm_world_block(r->world, x, y, z);
    size = runtime_static_container_size_for_block(block);
    if (size <= 0 || size > GM_RUNTIME_STATIC_CONTAINER_SLOTS
            || slot < 0 || slot >= size
            || (item != 0 && count > tec_max_stack_size(item)))
        return 0;
    if (block == 117 && item != 0) {
        ICStack brewing_stack = ic_mk(item, count, meta);
        /* A completed dragon-breath brew can leave its container item in the
         * ingredient slot even though players cannot insert it there. */
        if (!(slot == BREWING_LIVE_INGREDIENT
                    && item == TB_GLASS_BOTTLE && count == 1 && meta == 0)
                && !brewing_live_slot_valid(slot, &brewing_stack))
            return 0;
    }
    if (block == 84
            && ((item == 0 && gm_world_meta(r->world, x, y, z) != 0)
                || (item != 0
                    && (item < 2256 || item > 2267
                        || count != 1 || meta != 0
                        || gm_world_meta(r->world, x, y, z) != 1))))
        return 0;
    for (int i = 0; i < r->static_containers_cap; ++i) {
        GmRuntimeStaticContainer *candidate =
            &r->static_containers[i];
        if (candidate->active
                && candidate->dimension == dimension
                && candidate->wx == x && candidate->wy == y
                && candidate->wz == z) {
            index = i;
            break;
        }
        if (!candidate->active && free_slot < 0)
            free_slot = i;
    }
    if (index < 0) index = free_slot;
    if (index < 0) {
        int old_cap = r->static_containers_cap;
        int new_cap = old_cap > 0
            ? old_cap * 2 : GM_RUNTIME_STATIC_CONTAINERS_INITIAL;
        GmRuntimeStaticContainer *grown;
        if (new_cap > GM_RUNTIME_STATIC_CONTAINERS_MAX)
            new_cap = GM_RUNTIME_STATIC_CONTAINERS_MAX;
        if (new_cap <= old_cap)
            return 0;
        grown = (GmRuntimeStaticContainer *)realloc(
            r->static_containers,
            (size_t)new_cap * sizeof *grown);
        if (!grown) return 0;
        memset(grown + old_cap, 0,
               (size_t)(new_cap - old_cap) * sizeof *grown);
        r->static_containers = grown;
        r->static_containers_cap = new_cap;
        index = old_cap;
    }
    GmRuntimeStaticContainer *container =
        &r->static_containers[index];
    if (!container->active
            || container->dimension != dimension
            || container->wx != x || container->wy != y
            || container->wz != z || container->block != block
            || container->size != size) {
        gm_nbt_blob_clear(&container->item_tag);
        memset(container, 0, sizeof *container);
        container->active = 1;
        container->dimension = dimension;
        container->wx = x;
        container->wy = y;
        container->wz = z;
        container->block = block;
        container->size = size;
        if (block == 117)
            brewing_live_init(container->slots, &container->brewing);
        else {
            for (int i = 0; i < size; ++i)
                container->slots[i] = ic_empty();
            if (block == 154)
                container->transfer_cooldown = -1;
        }
    }
    container->slots[slot] =
        item == 0 ? ic_empty() : ic_mk(item, count, meta);
    return 1;
}

int gm_runtime_hopper_set_transfer_state(
        GmRuntime *r, int dimension, int x, int y, int z,
        int transfer_cooldown, long long ticked_game_time) {
    if (!r || !r->world || dimension != r->dimension
            || gm_world_block(r->world, x, y, z) != 154)
        return 0;
    if (!runtime_static_container_at(r, x, y, z)
            && !gm_runtime_static_container_set_slot(
                r, dimension, x, y, z, 0, 0, 0, 0))
        return 0;
    for (int i = 0; i < r->static_containers_cap; ++i) {
        GmRuntimeStaticContainer *hopper = &r->static_containers[i];
        if (hopper->active && hopper->dimension == dimension
                && hopper->wx == x && hopper->wy == y && hopper->wz == z
                && hopper->block == 154) {
            hopper->transfer_cooldown = transfer_cooldown;
            hopper->ticked_game_time = ticked_game_time;
            return 1;
        }
    }
    return 0;
}

int gm_runtime_brewing_set_slot(
        GmRuntime *r, int dimension, int x, int y, int z,
        int slot, int item, int count, int meta,
        int brew_time, int fuel) {
    if (!r || gm_world_block(r->world, x, y, z) != 117
            || brew_time < 0 || brew_time > TB_BREW_TICKS
            || fuel < 0 || fuel > TB_FUEL_CHARGE
            || !gm_runtime_static_container_set_slot(
                r, dimension, x, y, z, slot, item, count, meta))
        return 0;
    for (int i = 0; i < r->static_containers_cap; ++i) {
        GmRuntimeStaticContainer *stand = &r->static_containers[i];
        if (!stand->active || stand->dimension != dimension
                || stand->wx != x || stand->wy != y || stand->wz != z
                || stand->block != 117)
            continue;
        stand->brewing.brew_time = brew_time;
        stand->brewing.fuel = fuel;
        /* A pre-tick live snapshot with an active brew has already latched
         * the current ingredient. Java NBT omits this field, but oracle state
         * capture occurs after the preceding tile tick. */
        stand->brewing.ingredient_id = brew_time > 0
            ? stand->slots[BREWING_LIVE_INGREDIENT].item : 0;
        return 1;
    }
    return 0;
}

int gm_runtime_shulker_set_item_tag_nbt(
        GmRuntime *r, int dimension, int x, int y, int z,
        const void *item_tag_nbt, size_t item_tag_nbt_len) {
    if (!r || !r->world || !r->static_containers
            || dimension != r->dimension
            || !runtime_is_shulker_box(gm_world_block(r->world, x, y, z)))
        return 0;
    for (int i = 0; i < r->static_containers_cap; ++i) {
        GmRuntimeStaticContainer *container = &r->static_containers[i];
        if (container->active && container->dimension == dimension
                && container->wx == x && container->wy == y
                && container->wz == z
                && runtime_is_shulker_box(container->block))
            return gm_nbt_blob_set(
                &container->item_tag, item_tag_nbt, item_tag_nbt_len);
    }
    return 0;
}

int gm_runtime_command_block_count(const GmRuntime *r) {
    int count = 0;
    if (!r || !r->command_blocks) return 0;
    for (int i = 0; i < r->command_blocks_cap; ++i) {
        const GmRuntimeCommandBlock *command = &r->command_blocks[i];
        if (command->active
                && command->dimension == r->dimension
                && gm_world_block(
                    r->world, command->wx,
                    command->wy, command->wz) == command->block
                && runtime_is_command_block(command->block))
            count++;
    }
    return count;
}

int gm_runtime_command_block_get(
        const GmRuntime *r, int index, GmRuntimeCommandBlock *out) {
    int at = 0;
    if (!r || !r->command_blocks || !out || index < 0)
        return 0;
    for (int i = 0; i < r->command_blocks_cap; ++i) {
        const GmRuntimeCommandBlock *command = &r->command_blocks[i];
        if (!command->active
                || command->dimension != r->dimension
                || gm_world_block(
                    r->world, command->wx,
                    command->wy, command->wz) != command->block
                || !runtime_is_command_block(command->block))
            continue;
        if (at++ == index) {
            *out = *command;
            return 1;
        }
    }
    return 0;
}

int gm_runtime_command_block_set_success(
        GmRuntime *r, int dimension, int x, int y, int z,
        int success_count) {
    int block;
    int index = -1;
    int free_slot = -1;
    if (!r || !r->world || dimension != r->dimension
            || y < 0 || y > 255
            || success_count < 0 || success_count > 15)
        return 0;
    block = gm_world_block(r->world, x, y, z);
    if (!runtime_is_command_block(block))
        return 0;
    for (int i = 0; i < r->command_blocks_cap; ++i) {
        GmRuntimeCommandBlock *candidate = &r->command_blocks[i];
        if (candidate->active
                && candidate->dimension == dimension
                && candidate->wx == x && candidate->wy == y
                && candidate->wz == z) {
            index = i;
            break;
        }
        if (!candidate->active && free_slot < 0)
            free_slot = i;
    }
    if (index < 0) index = free_slot;
    if (index < 0) {
        int old_cap = r->command_blocks_cap;
        int new_cap = old_cap > 0
            ? old_cap * 2 : GM_RUNTIME_COMMAND_BLOCKS_INITIAL;
        GmRuntimeCommandBlock *grown;
        if (new_cap > GM_RUNTIME_COMMAND_BLOCKS_MAX)
            new_cap = GM_RUNTIME_COMMAND_BLOCKS_MAX;
        if (new_cap <= old_cap)
            return 0;
        grown = (GmRuntimeCommandBlock *)realloc(
            r->command_blocks, (size_t)new_cap * sizeof *grown);
        if (!grown) return 0;
        memset(grown + old_cap, 0,
               (size_t)(new_cap - old_cap) * sizeof *grown);
        r->command_blocks = grown;
        r->command_blocks_cap = new_cap;
        index = old_cap;
    }
    GmRuntimeCommandBlock *command = &r->command_blocks[index];
    if (!command->active
            || command->dimension != dimension
            || command->wx != x || command->wy != y
            || command->wz != z || command->block != block) {
        memset(command, 0, sizeof *command);
        command->active = 1;
        command->dimension = dimension;
        command->wx = x;
        command->wy = y;
        command->wz = z;
        command->block = block;
    }
    command->success_count = success_count;
    return 1;
}

int gm_runtime_flower_pot_count(const GmRuntime *r) {
    int count = 0;
    if (!r || !r->flower_pots) return 0;
    for (int i = 0; i < r->flower_pots_cap; ++i) {
        const GmRuntimeFlowerPot *pot = &r->flower_pots[i];
        if (pot->active
                && pot->dimension == r->dimension
                && gm_world_block(
                    r->world, pot->wx, pot->wy, pot->wz) == 140)
            count++;
    }
    return count;
}

int gm_runtime_flower_pot_get(
        const GmRuntime *r, int index, GmRuntimeFlowerPot *out) {
    int at = 0;
    if (!r || !r->flower_pots || !out || index < 0)
        return 0;
    for (int i = 0; i < r->flower_pots_cap; ++i) {
        const GmRuntimeFlowerPot *pot = &r->flower_pots[i];
        if (!pot->active
                || pot->dimension != r->dimension
                || gm_world_block(
                    r->world, pot->wx, pot->wy, pot->wz) != 140)
            continue;
        if (at++ == index) {
            *out = *pot;
            return 1;
        }
    }
    return 0;
}

int gm_runtime_flower_pot_set(
        GmRuntime *r, int dimension, int x, int y, int z,
        int item, int meta) {
    int index = -1;
    int free_slot = -1;
    if (!r || !r->world || dimension != r->dimension
            || y < 0 || y > 255
            || gm_world_block(r->world, x, y, z) != 140
            || item < 0 || item > 4095
            || meta < 0 || meta > 32767
            || (item == 0 && meta != 0))
        return 0;
    for (int i = 0; i < r->flower_pots_cap; ++i) {
        GmRuntimeFlowerPot *candidate = &r->flower_pots[i];
        if (candidate->active
                && candidate->dimension == dimension
                && candidate->wx == x && candidate->wy == y
                && candidate->wz == z) {
            index = i;
            break;
        }
        if (!candidate->active && free_slot < 0)
            free_slot = i;
    }
    if (index < 0) index = free_slot;
    if (index < 0) {
        int old_cap = r->flower_pots_cap;
        int new_cap = old_cap > 0
            ? old_cap * 2 : GM_RUNTIME_FLOWER_POTS_INITIAL;
        GmRuntimeFlowerPot *grown;
        if (new_cap > GM_RUNTIME_FLOWER_POTS_MAX)
            new_cap = GM_RUNTIME_FLOWER_POTS_MAX;
        if (new_cap <= old_cap)
            return 0;
        grown = (GmRuntimeFlowerPot *)realloc(
            r->flower_pots, (size_t)new_cap * sizeof *grown);
        if (!grown) return 0;
        memset(grown + old_cap, 0,
               (size_t)(new_cap - old_cap) * sizeof *grown);
        r->flower_pots = grown;
        r->flower_pots_cap = new_cap;
        index = old_cap;
    }
    GmRuntimeFlowerPot *pot = &r->flower_pots[index];
    *pot = (GmRuntimeFlowerPot){
        .active = 1,
        .dimension = dimension,
        .wx = x,
        .wy = y,
        .wz = z,
        .item = item,
        .meta = meta,
    };
    return 1;
}

static void runtime_flower_pot_remove(
        GmRuntime *r, int dimension, int x, int y, int z) {
    if (!r || !r->flower_pots) return;
    for (int i = 0; i < r->flower_pots_cap; ++i) {
        GmRuntimeFlowerPot *pot = &r->flower_pots[i];
        if (pot->active && pot->dimension == dimension
                && pot->wx == x && pot->wy == y && pot->wz == z) {
            memset(pot, 0, sizeof *pot);
            return;
        }
    }
}

int gm_runtime_skull_count(const GmRuntime *r) {
    int count = 0;
    if (!r || !r->skulls) return 0;
    for (int i = 0; i < r->skulls_cap; ++i) {
        const GmRuntimeSkull *skull = &r->skulls[i];
        if (skull->active
                && skull->dimension == r->dimension
                && gm_world_block(
                    r->world, skull->wx, skull->wy, skull->wz) == 144)
            count++;
    }
    return count;
}

int gm_runtime_skull_get(
        const GmRuntime *r, int index, GmRuntimeSkull *out) {
    int at = 0;
    if (!r || !r->skulls || !out || index < 0)
        return 0;
    for (int i = 0; i < r->skulls_cap; ++i) {
        const GmRuntimeSkull *skull = &r->skulls[i];
        if (!skull->active
                || skull->dimension != r->dimension
                || gm_world_block(
                    r->world, skull->wx, skull->wy, skull->wz) != 144)
            continue;
        if (at++ == index) {
            *out = *skull;
            return 1;
        }
    }
    return 0;
}

int gm_runtime_skull_set(
        GmRuntime *r, int dimension, int x, int y, int z,
        int type, int rotation) {
    return gm_runtime_skull_set_profile_nbt(
        r, dimension, x, y, z, type, rotation, NULL, 0);
}

int gm_runtime_skull_set_profile_nbt(
        GmRuntime *r, int dimension, int x, int y, int z,
        int type, int rotation, const void *profile_nbt,
        size_t profile_nbt_len) {
    int index = -1;
    int free_slot = -1;
    GmNbtBlob profile = {0};
    if (!r || !r->world || dimension != r->dimension
            || y < 0 || y > 255
            || gm_world_block(r->world, x, y, z) != 144
            || type < 0 || type > 5 || rotation < 0 || rotation > 15
            || (profile_nbt_len > 0 && type != 3)
            || (profile_nbt_len == 0 && profile_nbt != NULL)
            || (profile_nbt_len > 0
                && !gm_nbt_blob_set(
                    &profile, profile_nbt, profile_nbt_len)))
        return 0;
    for (int i = 0; i < r->skulls_cap; ++i) {
        GmRuntimeSkull *candidate = &r->skulls[i];
        if (candidate->active
                && candidate->dimension == dimension
                && candidate->wx == x && candidate->wy == y
                && candidate->wz == z) {
            index = i;
            break;
        }
        if (!candidate->active && free_slot < 0)
            free_slot = i;
    }
    if (index < 0) index = free_slot;
    if (index < 0) {
        int old_cap = r->skulls_cap;
        int new_cap = old_cap > 0
            ? old_cap * 2 : GM_RUNTIME_SKULLS_INITIAL;
        GmRuntimeSkull *grown;
        if (new_cap > GM_RUNTIME_SKULLS_MAX)
            new_cap = GM_RUNTIME_SKULLS_MAX;
        if (new_cap <= old_cap) {
            gm_nbt_blob_clear(&profile);
            return 0;
        }
        grown = (GmRuntimeSkull *)realloc(
            r->skulls, (size_t)new_cap * sizeof *grown);
        if (!grown) {
            gm_nbt_blob_clear(&profile);
            return 0;
        }
        memset(grown + old_cap, 0,
               (size_t)(new_cap - old_cap) * sizeof *grown);
        r->skulls = grown;
        r->skulls_cap = new_cap;
        index = old_cap;
    }
    GmRuntimeSkull *skull = &r->skulls[index];
    gm_nbt_blob_clear(&skull->owner_profile);
    *skull = (GmRuntimeSkull){
        .active = 1,
        .dimension = dimension,
        .wx = x,
        .wy = y,
        .wz = z,
        .type = type,
        .rotation = rotation,
        .owner_profile = profile,
    };
    return 1;
}

static void runtime_skull_remove(
        GmRuntime *r, int dimension, int x, int y, int z) {
    if (!r || !r->skulls) return;
    for (int i = 0; i < r->skulls_cap; ++i) {
        GmRuntimeSkull *skull = &r->skulls[i];
        if (skull->active && skull->dimension == dimension
                && skull->wx == x && skull->wy == y && skull->wz == z) {
            gm_nbt_blob_clear(&skull->owner_profile);
            memset(skull, 0, sizeof *skull);
            return;
        }
    }
}

static int runtime_item_frame_surface_valid(
        const GmRuntime *r, int x, int y, int z, int facing) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    int support_x;
    int support_z;
    if (!r || !r->world || facing < 2 || facing > 5
            || y < 0 || y > 255
            || gm_world_block(r->world, x, y, z) != 0)
        return 0;
    support_x = x - dx[facing];
    support_z = z - dz[facing];
    return gm_block_is_normal_cube_1_11_2(
        gm_world_block(r->world, support_x, y, support_z),
        gm_world_meta(r->world, support_x, y, support_z));
}

int gm_runtime_item_frame_count(const GmRuntime *r) {
    int count = 0;
    if (!r || !r->item_frames) return 0;
    for (int i = 0; i < r->item_frames_cap; ++i) {
        const GmRuntimeItemFrame *frame = &r->item_frames[i];
        if (frame->active
                && frame->dimension == r->dimension
                && runtime_item_frame_surface_valid(
                    r, frame->hanging_x, frame->hanging_y,
                    frame->hanging_z, frame->facing))
            count++;
    }
    return count;
}

int gm_runtime_item_frame_get(
        const GmRuntime *r, int index, GmRuntimeItemFrame *out) {
    int at = 0;
    if (!r || !r->item_frames || !out || index < 0)
        return 0;
    for (int i = 0; i < r->item_frames_cap; ++i) {
        const GmRuntimeItemFrame *frame = &r->item_frames[i];
        if (!frame->active
                || frame->dimension != r->dimension
                || !runtime_item_frame_surface_valid(
                    r, frame->hanging_x, frame->hanging_y,
                    frame->hanging_z, frame->facing))
            continue;
        if (at++ == index) {
            *out = *frame;
            return 1;
        }
    }
    return 0;
}

int gm_runtime_item_frame_set(
        GmRuntime *r, int dimension, int eid,
        double x, double y, double z,
        int hanging_x, int hanging_y, int hanging_z,
        int facing, int item, int count, int meta, int rotation) {
    int index = -1;
    int free_slot = -1;
    if (!r || dimension != r->dimension || eid <= 0
            || !isfinite(x) || !isfinite(y) || !isfinite(z)
            || hanging_y < 0 || hanging_y > 255
            || facing < 2 || facing > 5
            || rotation < 0 || rotation > 7
            || !((item == 0 && count == 0 && meta == 0
                    && rotation == 0)
                || (item == 1 && count == 1 && meta == 0)
                || (item == 443 && count == 1 && meta == 0))
            || !runtime_item_frame_surface_valid(
                r, hanging_x, hanging_y, hanging_z, facing))
        return 0;
    for (int i = 0; i < r->item_frames_cap; ++i) {
        GmRuntimeItemFrame *candidate = &r->item_frames[i];
        if (candidate->active && candidate->eid == eid) {
            index = i;
            break;
        }
        if (candidate->active
                && candidate->dimension == dimension
                && candidate->hanging_x == hanging_x
                && candidate->hanging_y == hanging_y
                && candidate->hanging_z == hanging_z)
            return 0;
        if (!candidate->active && free_slot < 0)
            free_slot = i;
    }
    if (index < 0) index = free_slot;
    if (index < 0) {
        int old_cap = r->item_frames_cap;
        int new_cap = old_cap > 0
            ? old_cap * 2 : GM_RUNTIME_ITEM_FRAMES_INITIAL;
        GmRuntimeItemFrame *grown;
        if (new_cap > GM_RUNTIME_ITEM_FRAMES_MAX)
            new_cap = GM_RUNTIME_ITEM_FRAMES_MAX;
        if (new_cap <= old_cap)
            return 0;
        grown = (GmRuntimeItemFrame *)realloc(
            r->item_frames, (size_t)new_cap * sizeof *grown);
        if (!grown) return 0;
        memset(grown + old_cap, 0,
               (size_t)(new_cap - old_cap) * sizeof *grown);
        r->item_frames = grown;
        r->item_frames_cap = new_cap;
        index = old_cap;
    }
    GmRuntimeItemFrame *frame = &r->item_frames[index];
    memset(frame, 0, sizeof *frame);
    frame->active = 1;
    frame->dimension = dimension;
    frame->eid = eid;
    frame->x = x;
    frame->y = y;
    frame->z = z;
    frame->hanging_x = hanging_x;
    frame->hanging_y = hanging_y;
    frame->hanging_z = hanging_z;
    frame->facing = facing;
    frame->item = item;
    frame->count = count;
    frame->meta = meta;
    frame->rotation = rotation;
    return 1;
}

int gm_runtime_break_item_frame(GmRuntime *r, int eid) {
    if (!r || !r->item_frames || eid <= 0) return 0;
    for (int i = 0; i < r->item_frames_cap; ++i) {
        GmRuntimeItemFrame *frame = &r->item_frames[i];
        if (!frame->active || frame->dimension != r->dimension
                || frame->eid != eid)
            continue;
        int item = frame->item;
        if (item == 0) item = 389; /* empty frame breaks on the second hit */
        if (!gm_live_spawn_item_exact(
                &r->entities, r->next_entity_id,
                frame->x, frame->y, frame->z,
                0.0, 0.0, 0.0, 0.0f,
                item, 1, frame->item ? frame->meta : 0,
                0, 10, 0))
            return 0;
        ++r->next_entity_id;
        if (frame->item) {
            frame->item = 0;
            frame->count = 0;
            frame->meta = 0;
            frame->rotation = 0;
        } else {
            memset(frame, 0, sizeof *frame);
        }
        return 1;
    }
    return 0;
}

static int runtime_attack_item_frame(GmRuntime *r) {
    double eye_x, eye_y, eye_z, look_x, look_y, look_z;
    GmRuntimeItemFrame *best = NULL;
    double best_t = 6.0;
    if (!r || !r->item_frames) return 0;
    gm_player_look_ray(
        &r->sin_table, &r->player,
        &eye_x, &eye_y, &eye_z, &look_x, &look_y, &look_z);
    eye_x += r->ox;
    eye_z += r->oz;
    for (int i = 0; i < r->item_frames_cap; ++i) {
        GmRuntimeItemFrame *frame = &r->item_frames[i];
        if (!frame->active || frame->dimension != r->dimension
                || !runtime_item_frame_surface_valid(
                    r, frame->hanging_x, frame->hanging_y,
                    frame->hanging_z, frame->facing))
            continue;
        double dx = frame->x - eye_x;
        double dy = frame->y - eye_y;
        double dz = frame->z - eye_z;
        double along = dx * look_x + dy * look_y + dz * look_z;
        double perpendicular = dx * dx + dy * dy + dz * dz - along * along;
        if (along >= 0.0 && along <= 5.0 && perpendicular <= 0.25
                && along < best_t) {
            best = frame;
            best_t = along;
        }
    }
    return best ? gm_runtime_break_item_frame(r, best->eid) : 0;
}

int gm_runtime_redstone_torch_toggle_add(
        GmRuntime *r, int x, int y, int z, long long time) {
    if (!r || y < 0 || y > 255 || time < 0
            || time > r->clock.total_time)
        return 0;
    if (r->redstone_torch_toggle_count > 0
            && time < r->redstone_torch_toggles[
                r->redstone_torch_toggle_count - 1].time)
        return 0;
    return runtime_redstone_torch_append_toggle(r, x, y, z, time);
}

int gm_runtime_redstone_torch_toggle_count(const GmRuntime *r) {
    return r ? r->redstone_torch_toggle_count : 0;
}

int gm_runtime_redstone_torch_toggle_get(
        const GmRuntime *r, int index,
        GmRuntimeRedstoneTorchToggle *out) {
    if (!r || !out || index < 0
            || index >= r->redstone_torch_toggle_count)
        return 0;
    *out = r->redstone_torch_toggles[index];
    return 1;
}

static GmWorld *runtime_world_for_dimension(GmRuntime *r, int dimension) {
    if (!r || dimension < -1 || dimension > 1) return NULL;
    int wi = dimension + 1;
    if (!r->worlds[wi]) {
        int world_type = dimension == -1 ? 2 : (dimension == 1 ? 3 : 0);
        r->worlds[wi] = gm_world_create_type(r->seed, world_type);
    }
    return r->worlds[wi];
}

int gm_runtime_set_dimension(GmRuntime *r, int dimension) {
    GmWorld *world = runtime_world_for_dimension(r, dimension);
    if (!world) return 0;
    r->world = world;
    r->dimension = dimension;
    r->fire_rain_context_valid = 0;
    r->fire_humidity_context_valid = 0;
    r->win_world = NULL;
    /* an authoritative transfer (tape/script) implies arrival state: the
     * player may be standing inside the paired portal, so arm the cooldown
     * and clear any in-pane contact accumulated before the switch. */
    r->portal_cooldown = 100;
    r->portal_time = 0;
    return 1;
}

void gm_runtime_set_time(GmRuntime *r, long long world_time) {
    if (!r) return;
    r->clock.world_time=world_time;
}

void gm_runtime_set_total_time(GmRuntime *r, long long total_time) {
    if (!r) return;
    gm_world_clock_set_total_time(&r->clock, total_time);
}

void gm_runtime_set_gamerules(GmRuntime *r, const McGameRules *gamerules) {
    if (!r || !gamerules) return;
    r->gamerules = *gamerules;
    r->clock.freeze_daylight = !gamerules->doDaylightCycle;
    r->clock.freeze_weather = !gamerules->doWeatherCycle;
}

int gm_runtime_harvest_block(GmRuntime *r, int x, int y, int z) {
    if (!r || !r->world || y < 0 || y > 255) return 0;
    int id = gm_world_block(r->world, x, y, z);
    int meta = gm_world_meta(r->world, x, y, z);
    ICStack held = isr_get_stack(
        &r->player.inv, r->player.inv.current_item);
    if (id != 132 || held.item != 359 || held.count != 1) return 0;

    /* PlayerInteractionManager.tryHarvestBlock calls ItemShears first, then
     * BlockTripWire.onBlockHarvested marks DISARMED before removeBlock invokes
     * breakBlock. The intermediate meta-12 state uses update flag 4 and has no
     * observable neighbor callback of its own. */
    pv_add_exhaustion(&r->vitals, 0.005f);
    {
        ITAStack tool = ita_mk(held.item, held.meta);
        ita_on_block_destroyed(&tool, id);
        if (tool.damage > ita_stack_max_damage(&tool))
            (void)isr_decr_stack_size(
                &r->player.inv, r->player.inv.current_item, 1);
        else {
            held.meta = tool.damage;
            isr_set_stack(
                &r->player.inv, r->player.inv.current_item, held);
        }
    }
    gm_world_set_block_meta(r->world, x, y, z, 0, 0);
    runtime_redstone_break_replaced_state(r, x, y, z, id, meta | 8);
    gm_fluid_mark(&r->fluids, r->world, r->dimension, x, y, z);
    break_unsupported_plants(r, x, y, z);
    runtime_redstone_notify_neighbors(r, x, y, z);
    runtime_redstone_update_observers_at(r, x, y, z);
    return runtime_spawn_item_stack_at_block(r, x, y, z, 287, 0, 1);
}

int gm_runtime_set_block(GmRuntime *r, int x, int y, int z, int id, int meta) {
    if (!r || !r->world || y < 0 || y > 255 || id < 0 || id > 4095 ||
        meta < 0 || meta > 15) return 0;
    r->fire_rain_context_valid = 0;
    r->fire_humidity_context_valid = 0;
    int old_id = gm_world_block(r->world, x, y, z);
    int old_meta = gm_world_meta(r->world, x, y, z);
    int prime_tnt_on_add = old_id != 46 && id == 46
        && runtime_redstone_is_powered(r, x, y, z);
    if (prime_tnt_on_add
            && r->primed_tnt_count >= GM_RUNTIME_PRIMED_TNT)
        return 0;
    if ((id == 151 || id == 178)
            && runtime_daylight_detector_find(
                r, r->dimension, x, y, z) < 0
            && r->daylight_detector_count
                >= GM_RUNTIME_DAYLIGHT_DETECTORS)
        return 0;
    if ((id == 149 || id == 150)
            && !runtime_comparator_find(
                r, r->dimension, x, y, z)
            && r->comparator_count >= GM_RUNTIME_COMPARATORS)
        return 0;
    if (runtime_is_chest_block(old_id) && id != old_id)
        runtime_break_chest_te(r, x, y, z);
    if ((old_id == 61 || old_id == 62) && id != 61 && id != 62)
        runtime_break_furnace_te(r, x, y, z);
    if (runtime_static_container_size_for_block(old_id) > 0
            && id != old_id)
        runtime_break_static_container_te(r, x, y, z);
    if (runtime_is_command_block(old_id) && id != old_id)
        runtime_break_command_block_te(r, x, y, z);
    if (old_id == 140 && id != 140)
        runtime_flower_pot_remove(r, r->dimension, x, y, z);
    if (old_id == 144 && id != 144)
        runtime_skull_remove(r, r->dimension, x, y, z);
    if (old_id != id || old_meta != meta)
        runtime_break_item_frames_for_block(r, x, y, z);
    gm_world_set_block_meta(r->world, x, y, z, id, meta);
    gm_live_block_changed(&r->entities, r->world, x, y, z);
    if (old_id != 122 && id == 122)
        (void)runtime_schedule_tick_insert(
            r, x, y, z, 122, r->clock.total_time + 5, 0,
            r->scheduled_tick_next_order);
    if (old_id != 145 && id == 145)
        (void)runtime_schedule_tick_insert(
            r, x, y, z, 145, r->clock.total_time + 2, 0,
            r->scheduled_tick_next_order);
    if ((old_id != id || old_meta != meta) && id == 51)
        runtime_fire_on_added(r, x, y, z);
    if ((old_id == 151 || old_id == 178)
            && id != 151 && id != 178)
        runtime_daylight_detector_remove(
            r, r->dimension, x, y, z);
    if ((id == 151 || id == 178)
            && !runtime_daylight_detector_ensure(
                r, r->dimension, x, y, z))
        return 0;
    if (old_id != id && runtime_is_chest_block(id)
            && runtime_chest_ensure_tile(r, x, y, z) < 0)
        return 0;
    if (old_id != id
            && runtime_static_container_size_for_block(id) > 0
            && !gm_runtime_static_container_set_slot(
                r, r->dimension, x, y, z, 0, 0, 0, 0))
        return 0;
    if (old_id != id && id == 140
            && !gm_runtime_flower_pot_set(
                r, r->dimension, x, y, z, 0, 0))
        return 0;
    if (old_id != id && id == 144
            && !gm_runtime_skull_set(
                r, r->dimension, x, y, z, 0, 0))
        return 0;
    if ((old_id == 149 || old_id == 150) && old_id != id)
        runtime_comparator_remove(r, r->dimension, x, y, z);
    if (old_id != id
            && (old_id == 55 || old_id == 93 || old_id == 94
                || old_id == 131 || old_id == 132
                || old_id == 149 || old_id == 150
                || (runtime_redstone_is_pressure_plate(old_id)
                    && old_meta > 0)))
        runtime_redstone_break_replaced_state(
            r, x, y, z, old_id, old_meta);
    if (old_id != id && (id == 93 || id == 94))
        runtime_redstone_repeater_notify_output(r, x, y, z, meta);
    if (old_id != id && (id == 149 || id == 150))
        runtime_redstone_repeater_notify_output(r, x, y, z, meta);
    if ((id == 149 || id == 150)
            && !runtime_comparator_ensure(
                r, r->dimension, x, y, z))
        return 0;
    if (old_id != 132 && id == 132)
        runtime_redstone_tripwire_notify_hook(r, x, y, z, meta, 0);
    if (old_id == 218 && id != 218
            && (old_meta & 8) != 0
            && runtime_redstone_observer_tick_pending(r, x, y, z))
        runtime_redstone_observer_notify_output(r, x, y, z, old_meta);
    if (old_id != 218 && id == 218)
        runtime_redstone_observer_on_added(r, x, y, z);
    gm_fluid_mark(&r->fluids, r->world, r->dimension, x, y, z);
    break_unsupported_plants(r, x, y, z);
    if (id == 55)
        runtime_redstone_update_wire_component(r, x, y, z);
    if (old_id != 55 && id == 55)
        runtime_redstone_wire_on_added(r, x, y, z);
    if (old_id != id && (id == 123 || id == 124))
        runtime_redstone_lamp_on_added(r, x, y, z, id);
    else if (id == 29 || id == 33)
        runtime_redstone_piston_check(r, x, y, z);
    if (prime_tnt_on_add) {
        /* BlockTNT.onBlockAdded primes before its nested setBlockToAir. That
         * air write has its own neighbor pass; the outer placement pass still
         * follows below even though the target has already returned to air. */
        if (!runtime_tnt_prime(r, x, y, z))
            return 0;
        gm_world_set_block_meta(r->world, x, y, z, 0, 0);
        runtime_redstone_notify_neighbors(r, x, y, z);
        runtime_redstone_update_observers_at(r, x, y, z);
    }
    if (old_id != id && (old_id == 76 || id == 76))
        runtime_redstone_torch_notify_adjacent_neighbors(r, x, y, z);
    runtime_redstone_notify_neighbors(r, x, y, z);
    if (old_id != id || old_meta != meta)
        runtime_redstone_update_observers_at(r, x, y, z);
    if (runtime_is_chest_block(old_id)
            || old_id == 61 || old_id == 62
            || runtime_static_container_size_for_block(old_id) > 0
            || old_id == 92 || old_id == 118
            || old_id == 120 || runtime_is_command_block(old_id)
            || runtime_is_chest_block(id)
            || id == 61 || id == 62
            || runtime_static_container_size_for_block(id) > 0
            || id == 92
            || id == 118 || id == 120
            || runtime_is_command_block(id))
        runtime_redstone_update_comparator_output_level(r, x, y, z);
    if (old_id != id
            && (old_id == 69 || old_id == 77 || old_id == 143)
            && (old_meta & 8) != 0) {
        static const int dx[6] = {0, 0, 0, 0, -1, 1};
        static const int dy[6] = {-1, 1, 0, 0, 0, 0};
        static const int dz[6] = {0, 0, -1, 1, 0, 0};
        int face = runtime_redstone_control_facing(old_id, old_meta);
        runtime_redstone_notify_neighbors(
            r, x - dx[face], y - dy[face], z - dz[face]);
    }
    return 1;
}

int gm_runtime_load_block(GmRuntime *r, int x, int y, int z, int id, int meta) {
    if (!r || !r->world || y < 0 || y > 255 || id < 0 || id > 4095 ||
        meta < 0 || meta > 15) return 0;
    int old_id = gm_world_block(r->world, x, y, z);
    if ((id == 151 || id == 178)
            && runtime_daylight_detector_find(
                r, r->dimension, x, y, z) < 0
            && r->daylight_detector_count
                >= GM_RUNTIME_DAYLIGHT_DETECTORS)
        return 0;
    if ((id == 149 || id == 150)
            && !runtime_comparator_ensure(
                r, r->dimension, x, y, z))
        return 0;
    gm_world_load_block_meta(r->world, x, y, z, id, meta);
    if ((old_id == 151 || old_id == 178)
            && id != 151 && id != 178)
        runtime_daylight_detector_remove(
            r, r->dimension, x, y, z);
    if ((id == 151 || id == 178)
            && !runtime_daylight_detector_ensure(
                r, r->dimension, x, y, z))
        return 0;
    if ((old_id == 149 || old_id == 150)
            && id != 149 && id != 150)
        runtime_comparator_remove(r, r->dimension, x, y, z);
    r->win_gen = -1;
    return 1;
}

int gm_runtime_snapshot_region(GmRuntime *r, int ccx, int ccz, int radius) {
    if (!r || !r->world || radius < 0 || radius > 32) return 0;
    gm_world_ensure(r->world, ccx, ccz, radius);
    return 1;
}


int gm_runtime_load_block_dim(GmRuntime *r, int dimension, int x, int y, int z,
                              int id, int meta) {
    GmWorld *world = runtime_world_for_dimension(r, dimension);
    if (!world || y < 0 || y > 255 || id < 0 || id > 4095 || meta < 0 || meta > 15)
        return 0;
    int old_id = gm_world_block(world, x, y, z);
    if ((id == 151 || id == 178)
            && runtime_daylight_detector_find(
                r, dimension, x, y, z) < 0
            && r->daylight_detector_count
                >= GM_RUNTIME_DAYLIGHT_DETECTORS)
        return 0;
    if ((id == 149 || id == 150)
            && !runtime_comparator_ensure(
                r, dimension, x, y, z))
        return 0;
    gm_world_load_block_meta(world, x, y, z, id, meta);
    if ((old_id == 151 || old_id == 178)
            && id != 151 && id != 178)
        runtime_daylight_detector_remove(r, dimension, x, y, z);
    if ((id == 151 || id == 178)
            && !runtime_daylight_detector_ensure(
                r, dimension, x, y, z))
        return 0;
    if ((old_id == 149 || old_id == 150)
            && id != 149 && id != 150)
        runtime_comparator_remove(r, dimension, x, y, z);
    if (world == r->world) r->win_gen = -1;
    return 1;
}


int gm_runtime_snapshot_region_dim(GmRuntime *r, int dimension,
                                   int ccx, int ccz, int radius) {
    GmWorld *world = runtime_world_for_dimension(r, dimension);
    if (!world || radius < 0 || radius > 32) return 0;
    gm_world_ensure(world, ccx, ccz, radius);
    return 1;
}

int gm_runtime_finalize_block_snapshot_dim(
    GmRuntime *r, int dimension, int ccx, int ccz, int radius
) {
    return gm_runtime_snapshot_region_dim(r, dimension, ccx, ccz, radius);
}

int gm_runtime_load_sky_light_dim(
    GmRuntime *r, int dimension, int x, int y, int z, int value
) {
    GmWorld *world = runtime_world_for_dimension(r, dimension);
    if (!world || y < 0 || y > 255 || value < 0 || value > 15) return 0;
    return gm_world_load_sky_light(world, x, y, z, value);
}

int gm_runtime_finalize_sky_light_snapshot_dim(
    GmRuntime *r, int dimension
) {
    GmWorld *world = runtime_world_for_dimension(r, dimension);
    if (!world) return 0;
    gm_world_finalize_sky_light_snapshot(world);
    return 1;
}

int gm_runtime_set_inventory_stack(GmRuntime *r, int slot, ICStack stack) {
    if (!r || !isr_slot_ok(slot) || stack.item < 0 || stack.item > 4095 ||
        stack.count < 0 || stack.count > 64 ||
        stack.meta < 0 || stack.meta > 32767 ||
        ((stack.item == 0) != (stack.count == 0)) ||
        stack.n_enchants < 0 || stack.n_enchants > IC_MAX_ENCHANTS)
        return 0;
    for (int i = 0; i < stack.n_enchants; ++i)
        if (stack.enchants[i].id < 0 || stack.enchants[i].level <= 0)
            return 0;
    isr_set_stack(&r->player.inv, slot,
                  stack.count == 0 ? ic_empty() : stack);
    if (slot == ISR_ARMOR_CHEST) sync_elytra_from_chest(r);
    return 1;
}

int gm_runtime_set_inventory(GmRuntime *r, int slot, int item, int count, int meta) {
    return gm_runtime_set_inventory_stack(
        r, slot, count == 0 ? ic_empty() : ic_mk(item, count, meta));
}

void gm_runtime_set_weather(GmRuntime *r, int raining, int thundering,
                            int rain_time, int thunder_time) {
    if (!r) return;
    r->weather_enabled = 1;
    gm_world_clock_set_weather(&r->clock, raining, thundering,
                               rain_time, thunder_time);
}

void gm_runtime_set_weather_full(
        GmRuntime *r, int raining, int thundering, int rain_time,
        int thunder_time, int clean_weather_time, int weather_cycle,
        float prev_rain_strength, float rain_strength,
        float prev_thunder_strength, float thunder_strength) {
    if (!r) return;
    r->weather_enabled = 1;
    gm_world_clock_set_weather_full(
        &r->clock, raining, thundering, rain_time, thunder_time,
        clean_weather_time, weather_cycle, prev_rain_strength,
        rain_strength, prev_thunder_strength, thunder_strength);
}

void gm_runtime_set_daylight_cycle(GmRuntime *r, int enabled) {
    if (!r) return;
    r->clock.freeze_daylight = enabled ? 0 : 1;
}

int gm_runtime_set_fire_rain_context(
        GmRuntime *r, int x, int y, int z, int can_die,
        int raining_at_east, int can_die_west_candidate) {
    if (!r || y < 0 || y > 255 || (can_die != 0 && can_die != 1)
            || (raining_at_east != 0 && raining_at_east != 1)
            || (can_die_west_candidate != 0
                && can_die_west_candidate != 1))
        return 0;
    r->fire_rain_context_valid = 1;
    r->fire_rain_x = x;
    r->fire_rain_y = y;
    r->fire_rain_z = z;
    r->fire_rain_can_die = can_die;
    r->fire_rain_at_east = raining_at_east;
    r->fire_rain_can_die_west_candidate = can_die_west_candidate;
    return 1;
}

int gm_runtime_set_fire_humidity_context(
        GmRuntime *r, int x, int y, int z) {
    if (!r || y < 0 || y > 255)
        return 0;
    r->fire_humidity_context_valid = 1;
    r->fire_humidity_x = x;
    r->fire_humidity_y = y;
    r->fire_humidity_z = z;
    return 1;
}

int gm_runtime_projectile_views(const GmRuntime *r, GmEntityView *out, int max) {
    if (!r || !out || max <= 0) return 0;
    int n = 0;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES && n < max; ++i) {
        const GmRuntimeProjectile *p = &r->projectiles[i];
        if (!p->active) continue;
        GmEntityView v; memset(&v, 0, sizeof v);
        v.x = (float)p->x; v.y = (float)p->y; v.z = (float)p->z;
        v.health = 1.0f;
        if (p->type == 1 || p->type == 2) {
            /* bow arrows: RenderArrow model, oriented from velocity like
             * vanilla EntityArrow (yaw atan2(vx,vz), pitch atan2(vy,horiz)) */
            v.type = 29; /* ER_TYPE_ARROW */
            double h = sqrt(p->vx * p->vx + p->vz * p->vz);
            v.yaw   = (float)(atan2(p->vx, p->vz) * 180.0 / MC_PI);
            v.pitch = (float)(atan2(p->vy, h) * 180.0 / MC_PI);
        } else if (p->type == 3 || p->type == 5) {
            /* Fireballs use the fire_charge particle icon; the large ghast
             * shot shares this billboard path until scale is carried in views.
             * EntitySmallFireball uses RenderFireball scale 0.5.
             * particle icon. gm_items_emit_billboard selects that exact path
             * from item id 385. Live shots are isFireballFiery (setFire each
             * tick) so flags bit 0 enables renderEntityOnFire layers. */
            v.type = GM_VIEW_BILLBOARD; v.item_id = 385;
            v.flags = 1; /* burn / isBurning */
            if (p->type == 5) v.item_meta = 2; /* large width 1.0 fire overlay */
        } else if (p->type == 4) {
            v.type = GM_VIEW_BILLBOARD; v.item_id = 381; /* eye of ender */
        } else if (p->type == 6) {
            v.type = GM_VIEW_BILLBOARD;
            v.item_id = p->potion_item;
            v.item_meta = p->potion_type;
        } else if (p->type >= 7 && p->type <= 9) {
            v.type = GM_VIEW_BILLBOARD;
            v.item_id = p->potion_item;
        } else {
            v.type = 20; /* unknown: legacy marker box */
        }
        out[n++] = v;
    }
    for (int i = 0; i < GM_RUNTIME_FIREWORKS && n < max; ++i) {
        const GmRuntimeFirework *rocket = &r->fireworks[i];
        GmEntityView v;
        if (!rocket->active || rocket->dimension != r->dimension
                || rocket->attached_player)
            continue;
        memset(&v, 0, sizeof v);
        v.type = GM_VIEW_BILLBOARD;
        v.item_id = 401;
        v.ent_id = rocket->eid;
        v.x = (float)rocket->x;
        v.y = (float)rocket->y;
        v.z = (float)rocket->z;
        v.yaw = rocket->yaw;
        v.pitch = rocket->pitch;
        v.health = 1.0F;
        out[n++] = v;
    }
    if (r->fish_hook.active && r->fish_hook.dimension == r->dimension
            && n < max) {
        GmEntityView v;
        memset(&v, 0, sizeof v);
        /* Synthetic atlas id 9004 is RenderFish's exact particle-atlas cell.
         * It shares the camera-facing half-block quad transform used here. */
        v.type = GM_VIEW_BILLBOARD;
        v.item_id = 9004;
        v.ent_id = r->fish_hook.eid;
        v.x = (float)r->fish_hook.x;
        v.y = (float)r->fish_hook.y;
        v.z = (float)r->fish_hook.z;
        v.health = 1.0F;
        out[n++] = v;
    }
    for (int i = 0; i < GM_RUNTIME_MINECARTS && n < max; ++i) {
        const GmRuntimeMinecart *cart = &r->minecarts[i];
        GmEntityView v;
        if (!cart->active || cart->dimension != r->dimension) continue;
        memset(&v, 0, sizeof v);
        v.type = 28; /* ER_TYPE_MINECART */
        v.ent_id = cart->eid;
        v.x = (float)cart->x;
        v.y = (float)cart->y;
        v.z = (float)cart->z;
        v.yaw = cart->yaw;
        v.pitch = cart->pitch;
        v.health = -1.0f;
        out[n++] = v;
    }
    return n;
}

int gm_runtime_end_crystal_views(
        const GmRuntime *r, GmEntityView *out, int max) {
    if (!r || !out || max <= 0 || r->end_crystal_count == 0)
        return 0;
    int n = 0;
    for (int i = 0; i < GM_RUNTIME_END_CRYSTALS && n < max; ++i) {
        const GmRuntimeEndCrystal *crystal = &r->end_crystals[i];
        if (!crystal->active || crystal->dimension != r->dimension)
            continue;
        GmEntityView view;
        memset(&view, 0, sizeof view);
        view.type = GM_ENTITY_CRYSTAL;
        view.ent_id = crystal->eid;
        view.x = (float)crystal->x;
        view.y = (float)crystal->y;
        view.z = (float)crystal->z;
        view.health = 5.0f;
        view.crystal_rot = (float)crystal->inner_rotation;
        view.show_bottom = crystal->show_bottom;
        view.has_beam = crystal->has_beam;
        view.beam_x = crystal->has_beam ? crystal->beam_x : -1;
        view.beam_y = crystal->has_beam ? crystal->beam_y : -1;
        view.beam_z = crystal->has_beam ? crystal->beam_z : -1;
        out[n++] = view;
    }
    return n;
}

int gm_runtime_falling_block_views(
        const GmRuntime *r, GmEntityView *out, int max) {
    if (!r || !out || max <= 0 || r->falling_block_count == 0)
        return 0;
    int n = 0;
    for (int i = 0; i < GM_RUNTIME_FALLING_BLOCKS && n < max; ++i) {
        const GmRuntimeFallingBlock *falling = &r->falling_blocks[i];
        if (!falling->active)
            continue;
        GmEntityView view;
        memset(&view, 0, sizeof view);
        view.type = GM_VIEW_FALLING_BLOCK;
        view.ent_id = falling->eid;
        view.x = (float)falling->x;
        view.y = (float)falling->y;
        view.z = (float)falling->z;
        view.health = -1.0f;
        view.item_id = falling->block;
        view.item_meta = falling->meta;
        view.age = falling->fall_time;
        out[n++] = view;
    }
    return n;
}

int gm_runtime_craft(GmRuntime *r, int grid_width, const int inv_slots[9]) {
    if (!r || (grid_width != 2 && grid_width != 3)) return 0;
    if (grid_width == 3 && r->container != 1) return 0;
    CRStack grid[9];
    int use[ISR_MAIN_SLOTS];
    memset(use, 0, sizeof use);
    for (int i = 0; i < 9; ++i) {
        grid[i] = crf_empty();
        int slot = inv_slots[i];
        if (slot < 0) continue;
        if (slot >= ISR_MAIN_SLOTS) return 0;
        if (grid_width == 2 && (i % 3 >= 2 || i / 3 >= 2)) return 0;
        ICStack s = isr_get_stack(&r->player.inv, slot);
        if (isr_is_empty(&s)) return 0;
        use[slot]++;
        if (use[slot] > s.count) return 0;
        grid[i] = crf_mk(s.item, 1, s.meta);
    }
    /* RecipeFireworks is stateful/NBT-producing and is intentionally absent
     * from the static generated table. Preserve its three shapeless forms in
     * the compact firework metadata representation. */
    ICStack special = ic_empty();
    {
        int paper = 0, gunpowder = 0, stars = 0, tagged_stars = 0;
        int dyes = 0, glow_or_diamond = 0, shape = 0, other = 0;
        int rocket_large = 0, rocket_flicker = 0;
        int star_large = 0, star_flicker = 0;
        int first_star_meta = 0;
        for (int i = 0; i < 9; ++i) {
            if (crf_isEmpty(grid[i])) continue;
            switch (grid[i].item) {
            case 339: ++paper; break;
            case 289: ++gunpowder; break;
            case 402:
                ++stars;
                if (((grid[i].meta >> 8) & 0x1f) > 0) {
                    ++tagged_stars;
                    rocket_large |= (grid[i].meta >> 13) & 1;
                    rocket_flicker |= (grid[i].meta >> 14) & 1;
                }
                first_star_meta = grid[i].meta;
                break;
            case 351: ++dyes; break;
            case 348:
                ++glow_or_diamond; star_flicker = 1; break;
            case 264: ++glow_or_diamond; break;
            case 385: ++shape; star_large = 1; break;
            case 288: case 371: case 397: ++shape; break;
            default: ++other; break;
            }
        }
        if (!other && paper == 1 && gunpowder >= 1 && gunpowder <= 3
                && dyes == 0 && glow_or_diamond == 0 && shape == 0) {
            special = ic_mk(401, 3, ic_firework_meta_payload(
                gunpowder, tagged_stars,
                tagged_stars >= 3 || rocket_large, rocket_flicker));
        } else if (!other && paper == 0 && gunpowder == 1 && stars == 0
                && dyes > 0 && shape <= 1) {
            special = ic_mk(402, 1, ic_firework_meta_payload(
                0, 1, star_large, star_flicker));
        } else if (!other && paper == 0 && gunpowder == 0 && stars == 1
                && dyes > 0 && glow_or_diamond == 0 && shape == 0
                && ((first_star_meta >> 8) & 0x1f) > 0) {
            special = ic_mk(402, 1, first_star_meta);
        }
    }
    CRRecipe recipes[CRF_NRECIPES];
    CRStack result = crf_empty();
    if (isr_is_empty(&special)) {
        int nr = crf_build(recipes);
        result = crf_findMatching(recipes, nr, grid);
        if (crf_isEmpty(result) || result.item == (i32)0xffffffff) return 0;
    }
    IsrInv next = r->player.inv;
    for (int slot = 0; slot < ISR_MAIN_SLOTS; ++slot)
        if (use[slot]) (void)isr_decr_stack_size(&next, slot, use[slot]);
    ICStack output = isr_is_empty(&special)
        ? ic_mk(result.item, result.count, result.meta) : special;
    (void)isr_add_item_stack_to_inventory(&next, &output);
    if (!isr_is_empty(&output)) return 0;
    r->player.inv = next;
    return 1;
}

static int runtime_chest_adjacent_index(
        const GmRuntime *r, int chest_index) {
    static const int dx[4] = {0, 1, 0, -1};
    static const int dz[4] = {-1, 0, 1, 0};
    const GmRuntimeChest *chest;
    int block_id;
    if (!r || !r->chests || chest_index < 0
            || chest_index >= r->chests_cap)
        return -1;
    chest = &r->chests[chest_index];
    if (!chest->active)
        return -1;
    block_id = gm_world_block(
        r->world, chest->wx, chest->wy, chest->wz);
    if (!runtime_is_chest_block(block_id))
        return -1;
    for (int face = 0; face < 4; ++face) {
        int x = chest->wx + dx[face];
        int z = chest->wz + dz[face];
        if (gm_world_block(r->world, x, chest->wy, z) != block_id)
            continue;
        for (int i = 0; i < r->chests_cap; ++i)
            if (i != chest_index && r->chests[i].active
                    && r->chests[i].wx == x
                    && r->chests[i].wy == chest->wy
                    && r->chests[i].wz == z)
                return i;
    }
    return -1;
}

static void runtime_chest_notify_viewer_change(
        GmRuntime *r, int chest_index) {
    const GmRuntimeChest *chest;
    if (!r || !r->chests || chest_index < 0
            || chest_index >= r->chests_cap)
        return;
    chest = &r->chests[chest_index];
    if (!chest->active
            || gm_world_block(
                r->world, chest->wx, chest->wy, chest->wz) != 146)
        return;
    /* TileEntityChest.open/closeInventory notifies at the chest and, only
     * for Type.TRAP, at pos.down() so upward strong power propagates. */
    runtime_redstone_notify_neighbors(
        r, chest->wx, chest->wy, chest->wz);
    runtime_redstone_notify_neighbors(
        r, chest->wx, chest->wy - 1, chest->wz);
}

static void runtime_chest_open_container(
        GmRuntime *r, int chest_index) {
    int pair_index = runtime_chest_adjacent_index(r, chest_index);
    chest_live_open(&r->chests[chest_index].state);
    runtime_chest_notify_viewer_change(r, chest_index);
    /* InventoryLargeChest.openInventory forwards to both 27-slot halves. */
    if (pair_index >= 0) {
        chest_live_open(&r->chests[pair_index].state);
        runtime_chest_notify_viewer_change(r, pair_index);
    }
}

static void runtime_chest_close_container(
        GmRuntime *r, int chest_index) {
    int pair_index = runtime_chest_adjacent_index(r, chest_index);
    chest_live_close(&r->chests[chest_index].state);
    runtime_chest_notify_viewer_change(r, chest_index);
    /* InventoryLargeChest.closeInventory mirrors the two-half open. */
    if (pair_index >= 0) {
        chest_live_close(&r->chests[pair_index].state);
        runtime_chest_notify_viewer_change(r, pair_index);
    }
}

/* Close any open container TE bookkeeping before switching screens. */
static void runtime_close_container(GmRuntime *r)
{
    if (!r) return;
    if (r->container == 3 && r->active_chest >= 0)
        runtime_chest_close_container(r, r->active_chest);
    gm_container_close(r);
    r->enchanting.open = 0;
    r->container = 0;
    r->active_furnace = -1;
    r->active_chest = -1;
    r->active_static_container = -1;
}

/* Drop one chest slot stack as a ground EntityItem, including StoredEnchantments.
 * Uses live overflow hold when the active table is full (not silent discard).
 * Returns 1 if held (active or overflow), 0 if both caps rejected the stack. */
static int runtime_drop_stack(GmRuntime *r, int wx, int wy, int wz, ICStack st)
{
    if (!r || st.item <= 0 || st.count <= 0) return 1;
    if (gm_live_spawn_stack(&r->entities, wx + 0.5, wy + 0.5, wz + 0.5, st, 10))
        return 1;
    /* Last resort: try player inventory so a full-chest break cannot vanish. */
    {
        ICStack rem = st;
        (void)isr_add_item_stack_to_inventory(&r->player.inv, &rem);
        if (isr_is_empty(&rem)) return 1;
    }
    return 0;
}

/* Materialize deferred structure loot (or live TE slots) and drop all stacks.
 * Vanilla: breakBlock -> InventoryHelper after fillWithLoot on first access. */
static void runtime_drop_chest_contents(GmRuntime *r, ChestLive *ch, int wx, int wy, int wz)
{
    int s;
    if (!r || !ch) return;
    chest_live_ensure_loot(ch);
    for (s = 0; s < CHEST_LIVE_SLOTS; ++s) {
        ICStack st = chest_live_get(ch, s);
        runtime_drop_stack(r, wx, wy, wz, st);
    }
}

/* TileEntityChest break: drop every slot (after deferred loot fill) and free
 * the TE so a replacement chest at the same block starts empty. Unopened
 * structure chests with no TE still materialize deferred loot on break. */
static void runtime_break_chest_te(GmRuntime *r, int wx, int wy, int wz)
{
    if (!r || !r->chests) return;
    for (int i = 0; i < r->chests_cap; ++i) {
        GmRuntimeChest *c = &r->chests[i];
        if (!c->active || c->wx != wx || c->wy != wy || c->wz != wz) continue;
        runtime_drop_chest_contents(r, &c->state, wx, wy, wz);
        if (r->container == 3 && r->active_chest == i)
            runtime_close_container(r);
        c->active = 0;
        chest_live_init(&c->state);
        return;
    }
    /* No TE yet: unopened structure chest still drops deferred loot. */
    {
        int tid = -1;
        long long lseed = 0;
        if (gm_world_block(r->world, wx, wy, wz) == 54
                && runtime_generated_chest_info(
                    r, wx, wy, wz, &tid, &lseed)) {
            ChestLive tmp;
            chest_live_init(&tmp);
            chest_live_set_loot(&tmp, tid, lseed);
            runtime_drop_chest_contents(r, &tmp, wx, wy, wz);
        }
    }
}

/* TileEntityFurnace break: drop all three inventory slots and retire the
 * fixed-pool entry. A 61<->62 lit-state transition keeps the same tile. */
static void runtime_break_furnace_te(GmRuntime *r, int wx, int wy, int wz)
{
    if (!r) return;
    for (int i = 0; i < GM_RUNTIME_FURNACES; ++i) {
        GmRuntimeFurnace *f = &r->furnaces[i];
        SRStack slots[FURNACE_LIVE_SLOT_COUNT];
        if (!f->active || f->wx != wx || f->wy != wy || f->wz != wz)
            continue;
        slots[0] = f->state.input;
        slots[1] = f->state.fuel;
        slots[2] = f->state.output;
        for (int slot = 0; slot < FURNACE_LIVE_SLOT_COUNT; ++slot) {
            if (!sr_isEmpty(slots[slot]))
                (void)runtime_drop_stack(
                    r, wx, wy, wz,
                    ic_mk(slots[slot].item, slots[slot].count,
                          slots[slot].meta));
        }
        if (r->container == 2 && r->active_furnace == i)
            runtime_close_container(r);
        f->active = 0;
        furnace_live_init(&f->state);
        return;
    }
}

/* Static inventory break mirrors InventoryHelper.dropInventoryItems. The
 * active comparator-only slice has no GUI/tick path, but replacement must
 * still retire and preserve every represented stack. */
static void runtime_break_static_container_te(
        GmRuntime *r, int wx, int wy, int wz) {
    if (!r || !r->static_containers) return;
    for (int i = 0; i < r->static_containers_cap; ++i) {
        GmRuntimeStaticContainer *container =
            &r->static_containers[i];
        if (!container->active
                || container->dimension != r->dimension
                || container->wx != wx || container->wy != wy
                || container->wz != wz)
            continue;
        if (runtime_is_shulker_box(container->block)) {
            (void)runtime_spawn_shulker_box_item(
                r, wx, wy, wz, container->block, container);
            gm_nbt_blob_clear(&container->item_tag);
            memset(container, 0, sizeof *container);
            return;
        }
        if (r->container == 4 && r->active_static_container == i)
            runtime_close_container(r);
        for (int slot = 0; slot < container->size; ++slot)
            (void)runtime_drop_stack(
                r, wx, wy, wz, container->slots[slot]);
        memset(container, 0, sizeof *container);
        return;
    }
}

/* Command blocks in the bounded comparator slice carry only the inert saved
 * success count. Replacing the block retires that represented tile. */
static void runtime_break_command_block_te(
        GmRuntime *r, int wx, int wy, int wz) {
    if (!r || !r->command_blocks) return;
    for (int i = 0; i < r->command_blocks_cap; ++i) {
        GmRuntimeCommandBlock *command = &r->command_blocks[i];
        if (!command->active
                || command->dimension != r->dimension
                || command->wx != wx || command->wy != wy
                || command->wz != wz)
            continue;
        memset(command, 0, sizeof *command);
        return;
    }
}

/* The initial comparator-source slice does not yet claim frame drops or
 * damage. A live edit that replaces either the hanging air cell or its support
 * must still retire the represented source immediately. */
static void runtime_break_item_frames_for_block(
        GmRuntime *r, int wx, int wy, int wz) {
    static const int dx[6] = {0, 0, 0, 0, -1, 1};
    static const int dz[6] = {0, 0, -1, 1, 0, 0};
    if (!r || !r->item_frames) return;
    for (int i = 0; i < r->item_frames_cap; ++i) {
        GmRuntimeItemFrame *frame = &r->item_frames[i];
        if (!frame->active || frame->dimension != r->dimension)
            continue;
        int support_x =
            frame->hanging_x - dx[frame->facing];
        int support_z =
            frame->hanging_z - dz[frame->facing];
        if ((frame->hanging_x == wx
                    && frame->hanging_y == wy
                    && frame->hanging_z == wz)
                || (support_x == wx
                    && frame->hanging_y == wy
                    && support_z == wz))
            memset(frame, 0, sizeof *frame);
    }
}

/* Free slot for a new TE. Never evicts a live chest while its block exists.
 * Reclaims slots whose block is gone; grows the table when full. */
static int runtime_chest_free_slot(GmRuntime *r, int wx, int wy, int wz)
{
    int free_slot = -1;
    if (!r || !r->chests) return -1;
    for (int i = 0; i < r->chests_cap; ++i) {
        GmRuntimeChest *c = &r->chests[i];
        if (!c->active) {
            if (free_slot < 0) free_slot = i;
            continue;
        }
        if (c->wx == wx && c->wy == wy && c->wz == wz) return i;
        /* Reclaim only when the world no longer has a chest block here. */
        if (!runtime_is_chest_block(
                gm_world_block(r->world, c->wx, c->wy, c->wz))) {
            if (r->container == 3 && r->active_chest == i)
                runtime_close_container(r);
            c->active = 0;
            chest_live_init(&c->state);
            if (free_slot < 0) free_slot = i;
        }
    }
    if (free_slot >= 0) return free_slot;
    /* Grow: never drop inventory of a live block-backed TE. */
    {
        int new_cap = r->chests_cap > 0 ? r->chests_cap * 2 : GM_RUNTIME_CHESTS_INITIAL;
        GmRuntimeChest *grown = (GmRuntimeChest *)realloc(
            r->chests, (size_t)new_cap * sizeof(GmRuntimeChest));
        if (!grown) return -1; /* fail safely: no open, no loss */
        memset(grown + r->chests_cap, 0,
               (size_t)(new_cap - r->chests_cap) * sizeof(GmRuntimeChest));
        free_slot = r->chests_cap;
        r->chests = grown;
        r->chests_cap = new_cap;
        return free_slot;
    }
}

static int runtime_chest_ensure_tile(
        GmRuntime *r, int x, int y, int z) {
    int index = runtime_chest_free_slot(r, x, y, z);
    if (index < 0) return -1;
    GmRuntimeChest *chest = &r->chests[index];
    if (!chest->active) {
        chest->active = 1;
        chest->wx = x;
        chest->wy = y;
        chest->wz = z;
        chest_live_init(&chest->state);
    }
    return index;
}

int gm_runtime_chest_set_slot(
        GmRuntime *r, int dimension, int x, int y, int z,
        int slot, int item, int count, int meta) {
    static const int dx[4] = {0, 1, 0, -1};
    static const int dz[4] = {-1, 0, 1, 0};
    int adjacent_count = 0;
    int block_id;
    if (!r || !r->world || dimension != r->dimension
            || y < 0 || y > 255 || slot < 0
            || slot >= CHEST_LIVE_SLOTS
            || item < 0 || item > 4095
            || count < 0 || count > TEC_STACK_LIMIT
            || meta < 0 || meta > 32767
            || (item == 0) != (count == 0))
        return 0;
    block_id = gm_world_block(r->world, x, y, z);
    if (!runtime_is_chest_block(block_id))
        return 0;
    for (int face = 0; face < 4; ++face) {
        if (gm_world_block(
                r->world, x + dx[face], y, z + dz[face]) == block_id)
            adjacent_count++;
    }
    if (adjacent_count > 1)
        return 0;
    if (item != 0 && count > tec_max_stack_size(item))
        return 0;
    int index = runtime_chest_ensure_tile(r, x, y, z);
    if (index < 0) return 0;
    GmRuntimeChest *chest = &r->chests[index];
    chest_live_set(
        &chest->state, slot,
        item == 0 ? ic_empty() : ic_mk(item, count, meta));
    return 1;
}

int gm_runtime_use_block(GmRuntime *r, int wx, int wy, int wz) {
    if (!r || !r->world) return 0;
    GmPlayerView v; gm_runtime_view(r, &v);
    double dx = (wx + 0.5) - v.x;
    double dy = (wy + 0.5) - (v.y + v.eye_height);
    double dz = (wz + 0.5) - v.z;
    if (dx*dx + dy*dy + dz*dz > 36.0) return 0;
    int id = gm_world_block(r->world, wx, wy, wz);
    if (id == 46) {
        ICStack held = isr_get_stack(
            &r->player.inv, r->player.inv.current_item);
        if (held.count <= 0 || (held.item != 259 && held.item != 385)
                || !runtime_tnt_prime(r, wx, wy, wz))
            return 0;
        gm_world_set_block_meta(r->world, wx, wy, wz, 0, 0);
        runtime_redstone_notify_neighbors(r, wx, wy, wz);
        runtime_redstone_update_observers_at(r, wx, wy, wz);
        if (held.item == 259) {
            ++held.meta;
            if (held.meta > 64)
                (void)isr_decr_stack_size(
                    &r->player.inv, r->player.inv.current_item, 1);
            else
                isr_set_stack(
                    &r->player.inv, r->player.inv.current_item, held);
        } else {
            (void)isr_decr_stack_size(
                &r->player.inv, r->player.inv.current_item, 1);
        }
        return 1;
    }
    if (id == 69) {
        int meta = gm_world_meta(r->world, wx, wy, wz);
        gm_world_set_block_meta(
            r->world, wx, wy, wz, id, meta ^ 8);
        runtime_redstone_update_observers_at(r, wx, wy, wz);
        runtime_redstone_button_notify(r, wx, wy, wz, id, meta);
        return 1;
    }
    if (id == 77 || id == 143) {
        int meta = gm_world_meta(r->world, wx, wy, wz);
        if ((meta & 8) != 0)
            return 1;
        if (!runtime_schedule_tick_insert(
                r, wx, wy, wz, id,
                r->clock.total_time + (id == 143 ? 30 : 20),
                0, r->scheduled_tick_next_order))
            return 0;
        gm_world_set_block_meta(
            r->world, wx, wy, wz, id, meta | 8);
        runtime_redstone_update_observers_at(r, wx, wy, wz);
        runtime_redstone_button_notify(r, wx, wy, wz, id, meta);
        return 1;
    }
    if (id == 93 || id == 94) {
        int meta = gm_world_meta(r->world, wx, wy, wz);
        int next_delay = ((meta >> 2) + 1) & 3;
        gm_world_set_block_meta(
            r->world, wx, wy, wz, id, (meta & 3) | (next_delay << 2));
        runtime_redstone_notify_neighbors(r, wx, wy, wz);
        runtime_redstone_update_observers_at(r, wx, wy, wz);
        return 1;
    }
    if (id == 149 || id == 150) {
        int meta = gm_world_meta(r->world, wx, wy, wz);
        if (!runtime_comparator_find_mut(
                r, r->dimension, wx, wy, wz))
            return 0;
        gm_world_set_block_meta(r->world, wx, wy, wz, id, meta ^ 4);
        runtime_redstone_update_observers_at(r, wx, wy, wz);
        runtime_redstone_comparator_dispatch(r, wx, wy, wz, id);
        return 1;
    }
    if (id == 151 || id == 178) {
        int next_id = id == 151 ? 178 : 151;
        int next_meta = runtime_redstone_daylight_power(
            r, wx, wy, wz, next_id == 178);
        if (!runtime_daylight_detector_ensure(
                r, r->dimension, wx, wy, wz))
            return 0;
        gm_world_set_block_meta(
            r->world, wx, wy, wz, next_id, next_meta);
        runtime_redstone_notify_neighbors(r, wx, wy, wz);
        runtime_redstone_update_observers_at(r, wx, wy, wz);
        return 1;
    }
    if (id == 64 || (id >= 193 && id <= 197)) {
        int meta = gm_world_meta(r->world, wx, wy, wz);
        int lower_y = (meta & 8) != 0 ? wy - 1 : wy;
        if (lower_y < 0
                || gm_world_block(r->world, wx, lower_y, wz) != id)
            return 0;
        int lower_meta = gm_world_meta(r->world, wx, lower_y, wz);
        if ((lower_meta & 8) != 0)
            return 0;
        gm_world_set_block_meta(
            r->world, wx, lower_y, wz, id, lower_meta ^ 4);
        runtime_redstone_update_observers_at(r, wx, lower_y, wz);
        return 1;
    }
    if (id == 96) {
        int meta = gm_world_meta(r->world, wx, wy, wz);
        gm_world_set_block_meta(r->world, wx, wy, wz, id, meta ^ 4);
        runtime_redstone_update_observers_at(r, wx, wy, wz);
        return 1;
    }
    if (runtime_is_fence_gate_id(id)) {
        int meta = gm_world_meta(r->world, wx, wy, wz);
        if ((meta & 4) != 0) {
            meta &= ~4;
        } else {
            int facing = (int)floor(
                (double)r->server_player.yaw / 90.0 + 0.5) & 3;
            if ((meta & 3) == (facing ^ 2))
                meta = (meta & ~3) | facing;
            meta |= 4;
        }
        gm_world_set_block_meta(r->world, wx, wy, wz, id, meta);
        runtime_redstone_update_observers_at(r, wx, wy, wz);
        return 1;
    }
    if (id == 92) {
        int bites = gm_world_meta(r->world, wx, wy, wz);
        if (r->vitals.foodLevel >= 20)
            return 1;
        if (bites < 0 || bites > 6)
            return 0;
        if (!gm_runtime_set_block(
                r, wx, wy, wz, bites < 6 ? 92 : 0,
                bites < 6 ? bites + 1 : 0))
            return 0;
        {
            int food = r->vitals.foodLevel + 2;
            float saturation;
            if (food > 20) food = 20;
            r->vitals.foodLevel = food;
            saturation = r->vitals.saturation
                + (float)2 * 0.1f * 2.0f;
            r->vitals.saturation = saturation < (float)food
                ? saturation : (float)food;
            r->player.food = (float)food;
            r->server_player.food = (float)food;
        }
        return 1;
    }
    if (id == 140) {
        ICStack held = isr_get_stack(
            &r->player.inv, r->player.inv.current_item);
        return held.count > 0 && runtime_flower_pot_insert(
            r, wx, wy, wz, held.item, held.meta, 1);
    }
    if (id == 84) {
        if (gm_world_meta(r->world, wx, wy, wz) != 0)
            return runtime_jukebox_eject_record(r, wx, wy, wz);
        ICStack held = isr_get_stack(
            &r->player.inv, r->player.inv.current_item);
        return held.count > 0 && runtime_jukebox_insert_record(
            r, wx, wy, wz, held.item, held.meta, 1);
    }
    if (id == 58) {
        runtime_close_container(r); /* return any live grid/cursor before switching */
        r->container=1; r->container_wx=wx; r->container_wy=wy; r->container_wz=wz;
        return 1;
    }
    if (id == 61 || id == 62) {
        runtime_close_container(r);
        int free_slot = -1;
        for (int i = 0; i < GM_RUNTIME_FURNACES; ++i) {
            GmRuntimeFurnace *f = &r->furnaces[i];
            if (f->active && f->wx==wx && f->wy==wy && f->wz==wz) {
                r->container=2; r->active_furnace=i;
                r->container_wx=wx; r->container_wy=wy; r->container_wz=wz;
                return 1;
            }
            if (!f->active && free_slot < 0) free_slot=i;
        }
        if (free_slot < 0) return 0;
        GmRuntimeFurnace *f=&r->furnaces[free_slot];
        f->active=1; f->wx=wx; f->wy=wy; f->wz=wz; furnace_live_init(&f->state);
        r->container=2; r->active_furnace=free_slot;
        r->container_wx=wx; r->container_wy=wy; r->container_wz=wz;
        return 1;
    }
    if (id == 116) {
        GmPlayerView view;
        if (!r->enchanting_enabled) return 0;
        runtime_close_container(r);
        gm_runtime_view(r, &view);
        if (r->player_xp_level < 0)
            r->player_xp_level = view.xp_level;
        enchanting_live_open(
            &r->enchanting, r->world, wx, wy, wz,
            r->player_xp_seed);
        r->container = 5;
        r->container_wx = wx;
        r->container_wy = wy;
        r->container_wz = wz;
        return 1;
    }
    if (id == 117) {
        int index = -1;
        if (!r->brewing_enabled) return 0;
        runtime_close_container(r);
        for (int i = 0; i < r->static_containers_cap; ++i) {
            GmRuntimeStaticContainer *stand = &r->static_containers[i];
            if (stand->active && stand->dimension == r->dimension
                    && stand->wx == wx && stand->wy == wy
                    && stand->wz == wz && stand->block == 117) {
                index = i;
                break;
            }
        }
        if (index < 0) {
            if (!gm_runtime_static_container_set_slot(
                    r, r->dimension, wx, wy, wz, 0, 0, 0, 0))
                return 0;
            for (int i = 0; i < r->static_containers_cap; ++i) {
                GmRuntimeStaticContainer *stand = &r->static_containers[i];
                if (stand->active && stand->dimension == r->dimension
                        && stand->wx == wx && stand->wy == wy
                        && stand->wz == wz && stand->block == 117) {
                    index = i;
                    break;
                }
            }
        }
        if (index < 0) return 0;
        r->container = 4;
        r->active_static_container = index;
        r->container_wx = wx;
        r->container_wy = wy;
        r->container_wz = wz;
        return 1;
    }
    if (runtime_is_chest_block(id)) {
        runtime_close_container(r);
        if (!r->chests) return 0;
        for (int i = 0; i < r->chests_cap; ++i) {
            GmRuntimeChest *c = &r->chests[i];
            if (c->active && c->wx==wx && c->wy==wy && c->wz==wz) {
                runtime_chest_open_container(r, i);
                r->container=3; r->active_chest=i;
                r->container_wx=wx; r->container_wy=wy; r->container_wz=wz;
                return 1;
            }
        }
        int free_slot = runtime_chest_free_slot(r, wx, wy, wz);
        if (free_slot < 0) return 0;
        GmRuntimeChest *c=&r->chests[free_slot];
        c->active=1; c->wx=wx; c->wy=wy; c->wz=wz;
        chest_live_init(&c->state);
        {
            int tid = -1; long long lseed = 0;
            if (id == 54
                    && runtime_generated_chest_info(
                        r, wx, wy, wz, &tid, &lseed))
                chest_live_set_loot(&c->state, tid, lseed);
        }
        runtime_chest_open_container(r, free_slot);
        r->container=3; r->active_chest=free_slot;
        r->container_wx=wx; r->container_wy=wy; r->container_wz=wz;
        return 1;
    }
    if(id==120){
        ICStack held=isr_get_stack(&r->player.inv,r->player.inv.current_item);
        if(held.item!=381||held.count<=0||(gm_world_meta(r->world,wx,wy,wz)&4))return 0;
        if(!gm_end_portal_insert_eye(r->world,wx,wy,wz))return 0;
        (void)isr_decr_stack_size(&r->player.inv,r->player.inv.current_item,1);
        return 1;
    }
    if(id==26){
        if(r->dimension==0){
            long long day=r->clock.world_time/24000LL;
            r->clock.world_time=(day+1)*24000LL;return 1;
        }
        for(int x=wx-1;x<=wx+1;++x)for(int z=wz-1;z<=wz+1;++z)
            if(gm_world_block(r->world,x,wy,z)==26)gm_world_set_block(r->world,x,wy,z,0);
        runtime_explode_with_flags(
            r,wx+0.5,wy+0.5,wz+0.5,5.0f,1,0,1);return 1;
    }
    return 0;
}

int gm_runtime_enchant_click(GmRuntime *r, int button)
{
    if (!r || r->container != 5 || !r->enchanting.open
            || !r->enchanting_enabled || button < 0 || button > 2)
        return 0;
    if (!enchanting_live_apply(
            &r->enchanting, r->world, button, 0,
            &r->player_xp_level, &r->mobs.player_random))
        return 0;
    r->player_xp_seed = r->enchanting.xp_seed;
    /* World.playSound pitch consumes one World.rand float after offers update. */
    (void)runtime_java_random_next_float(r);
    return 1;
}

int gm_runtime_furnace_insert(GmRuntime *r, int furnace_slot,
                              int inventory_slot, int amount) {
    if (!r || r->container!=2 || r->active_furnace<0 || amount<=0 ||
        inventory_slot<0 || inventory_slot>=ISR_MAIN_SLOTS) return 0;
    ICStack src=isr_get_stack(&r->player.inv,inventory_slot);
    if (isr_is_empty(&src)) return 0;
    if (src.count>amount) src.count=amount;
    SRStack in=sr_mk(src.item,src.count,src.meta);
    GmRuntimeFurnace *furnace = &r->furnaces[r->active_furnace];
    int moved=furnace_live_insert(&furnace->state,furnace_slot,in);
    if (moved>0) {
        (void)isr_decr_stack_size(&r->player.inv,inventory_slot,moved);
        runtime_redstone_update_comparator_output_level(
            r, furnace->wx, furnace->wy, furnace->wz);
    }
    return moved;
}

int gm_runtime_furnace_extract(GmRuntime *r, int furnace_slot, int amount) {
    if (!r || r->container!=2 || r->active_furnace<0 || amount<=0) return 0;
    GmRuntimeFurnace *furnace=&r->furnaces[r->active_furnace];
    FurnaceLive *f=&furnace->state;
    SRStack *src = furnace_slot==0?&f->input:furnace_slot==1?&f->fuel:
                   furnace_slot==2?&f->output:NULL;
    if (!src || sr_isEmpty(*src)) return 0;
    int n=src->count<amount?src->count:amount;
    IsrInv next=r->player.inv;
    ICStack out=ic_mk(src->item,n,src->meta);
    (void)isr_add_item_stack_to_inventory(&next,&out);
    int moved=n-out.count;
    if (moved<=0) return 0;
    (void)furnace_live_extract(f,furnace_slot,moved);
    r->player.inv=next;
    runtime_redstone_update_comparator_output_level(
        r, furnace->wx, furnace->wy, furnace->wz);
    return moved;
}

void gm_runtime_brewing_changed(GmRuntime *r) {
    if (!r || r->container != 4 || r->active_static_container < 0
            || r->active_static_container >= r->static_containers_cap)
        return;
    GmRuntimeStaticContainer *stand =
        &r->static_containers[r->active_static_container];
    if (!stand->active || stand->block != 117) return;
    runtime_redstone_update_comparator_output_level(
        r, stand->wx, stand->wy, stand->wz);
}
