/* Shared Magma <-> Blaze subsystem parity record and deterministic hashes. */
#ifndef BLAZE_PORT_PARITY_H
#define BLAZE_PORT_PARITY_H

#include <stdint.h>

#if defined(__CUDACC__)
#define BP_HD __host__ __device__
#else
#define BP_HD
#endif

#define BP_PARITY_MAGIC       UINT32_C(0x59524150) /* "PARY" little-endian */
#define BP_PARITY_VERSION     UINT32_C(1)
#define BP_NSUBSYSTEMS        24u
#define BP_NDEBUG            32u
#define BP_FNV1A_OFFSET       UINT64_C(14695981039346656037)
#define BP_FNV1A_PRIME        UINT64_C(1099511628211)

enum BpSubsystem {
    BP_PLAYER = 0,
    BP_DIG,
    BP_INVENTORY,
    BP_ITEMS,
    BP_WORLD,
    BP_CRAFTING,
    BP_CONTAINERS,
    BP_FURNACES,
    BP_FLUIDS,
    BP_RANDOM_TICKS,
    BP_FALLING_BLOCKS,
    BP_MOBS,            /* digest: blaze_snap_mobs_digest (blaze_snapshot.h) */
    BP_PROJECTILES,
    BP_EXPLOSIONS,
    BP_PORTALS,
    BP_DIMENSIONS,
    BP_DRAGON,
    BP_WEATHER,
    BP_XP,
    BP_VICTORY,
    BP_CHESTS,
    BP_BOATS,
    BP_ELYTRA,
    BP_OBSERVATIONS
};
enum BpDebugField {
    BP_DBG_PLAYER_X = 0,
    BP_DBG_PLAYER_Y,
    BP_DBG_PLAYER_Z,
    BP_DBG_MOTION_X,
    BP_DBG_MOTION_Y,
    BP_DBG_MOTION_Z,
    BP_DBG_YAW,
    BP_DBG_PITCH,
    BP_DBG_ON_GROUND,
    BP_DBG_FALL_DISTANCE,
    BP_DBG_SPRINTING,
    BP_DBG_SPRINT_TIMER,
    BP_DBG_HEALTH,
    BP_DBG_FOOD,
    BP_DBG_EXHAUSTION,
    BP_DBG_DIG_PROGRESS,
    BP_DBG_DIG_HX,
    BP_DBG_DIG_HY,
    BP_DBG_DIG_HZ,
    BP_DBG_DIG_HITTING,
    BP_DBG_DIG_DELAY,
    BP_DBG_ATK_PREV,
    BP_DBG_LEFT_CLICK_COUNTER,
    BP_DBG_RC_DELAY,
    BP_DBG_USE_PREV,
    BP_DBG_HURT_VEL_RESET,
    BP_DBG_SERVER_MOTION_X,
    BP_DBG_SERVER_MOTION_Z,
    BP_DBG_CONTAINER,
    BP_DBG_CONTAINER_WX,
    BP_DBG_CONTAINER_WY,
    BP_DBG_CONTAINER_WZ
};

#define BP_BIT(subsystem) (UINT64_C(1) << (subsystem))
#define BP_REQ_UNREPRESENTED_SNAPSHOT (UINT64_C(1) << 63)
#define BP_IMPLEMENTED_MASK \
    (BP_BIT(BP_PLAYER) | BP_BIT(BP_DIG) | BP_BIT(BP_INVENTORY) | \
     BP_BIT(BP_ITEMS) | BP_BIT(BP_WORLD) | BP_BIT(BP_CRAFTING) | \
     BP_BIT(BP_CONTAINERS) | BP_BIT(BP_FURNACES) | BP_BIT(BP_FLUIDS) | \
     BP_BIT(BP_RANDOM_TICKS) | BP_BIT(BP_FALLING_BLOCKS) | \
     BP_BIT(BP_MOBS) | BP_BIT(BP_PROJECTILES) | BP_BIT(BP_EXPLOSIONS) | \
     BP_BIT(BP_WEATHER) | \
     BP_BIT(BP_CHESTS) | BP_BIT(BP_XP) | BP_BIT(BP_BOATS) | BP_BIT(BP_ELYTRA) | \
     BP_BIT(BP_OBSERVATIONS))
#define BP_MEASURED_MASK \
    (BP_BIT(BP_PLAYER) | BP_BIT(BP_DIG) | BP_BIT(BP_INVENTORY) | \
     BP_BIT(BP_ITEMS) | BP_BIT(BP_WORLD) | BP_BIT(BP_CRAFTING) | \
     BP_BIT(BP_CONTAINERS) | BP_BIT(BP_FURNACES) | BP_BIT(BP_FLUIDS) | \
     BP_BIT(BP_RANDOM_TICKS) | BP_BIT(BP_FALLING_BLOCKS) | \
     BP_BIT(BP_MOBS) | BP_BIT(BP_PROJECTILES) | BP_BIT(BP_EXPLOSIONS) | \
     BP_BIT(BP_WEATHER) | \
     BP_BIT(BP_CHESTS) | BP_BIT(BP_XP) | BP_BIT(BP_BOATS) | BP_BIT(BP_ELYTRA) | \
     BP_BIT(BP_OBSERVATIONS))

#define BP_SUBSYSTEM_NAMES \
    "player", "dig", "inventory", "items", "world", "crafting", \
    "containers", "furnaces", "fluids", "random_ticks", \
    "falling_blocks", "mobs", "projectiles", "explosions", "portals", \
    "dimensions", "dragon", "weather", "xp", "victory", "chests", \
    "boats", "elytra", "observations"

static const char *const bp_subsystem_names[BP_NSUBSYSTEMS] = {
    BP_SUBSYSTEM_NAMES
};

#pragma pack(push, 1)
typedef struct BpParityRecord {
    uint32_t magic;
    uint32_t version;
    uint32_t size;
    uint32_t nsubsystems;
    uint64_t implemented_mask;
    uint64_t measured_mask;
    uint64_t active_mask;
    int64_t tick;
    uint64_t digest[BP_NSUBSYSTEMS];
    uint32_t evidence[BP_NSUBSYSTEMS];
    uint64_t debug_bits[BP_NDEBUG];
} BpParityRecord;
#pragma pack(pop)

typedef char BpParityRecord_must_be_592_bytes
    [(sizeof(BpParityRecord) == 592) ? 1 : -1];

BP_HD static inline uint64_t bp_hash_begin(void) { return BP_FNV1A_OFFSET; }

BP_HD static inline uint64_t bp_hash_u8(uint64_t h, uint8_t v) {
    return (h ^ (uint64_t)v) * BP_FNV1A_PRIME;
}

BP_HD static inline uint64_t bp_hash_u16(uint64_t h, uint16_t v) {
    h = bp_hash_u8(h, (uint8_t)v);
    return bp_hash_u8(h, (uint8_t)(v >> 8));
}

BP_HD static inline uint64_t bp_hash_u32(uint64_t h, uint32_t v) {
    h = bp_hash_u16(h, (uint16_t)v);
    return bp_hash_u16(h, (uint16_t)(v >> 16));
}

BP_HD static inline uint64_t bp_hash_u64(uint64_t h, uint64_t v) {
    h = bp_hash_u32(h, (uint32_t)v);
    return bp_hash_u32(h, (uint32_t)(v >> 32));
}

BP_HD static inline uint64_t bp_hash_i32(uint64_t h, int32_t v) {
    return bp_hash_u32(h, (uint32_t)v);
}

BP_HD static inline uint64_t bp_hash_i64(uint64_t h, int64_t v) {
    return bp_hash_u64(h, (uint64_t)v);
}

BP_HD static inline uint32_t bp_float_bits(float v) {
    union { float f; uint32_t u; } bits;
    bits.f = v;
    return bits.u;
}

BP_HD static inline uint64_t bp_double_bits(double v) {
    union { double f; uint64_t u; } bits;
    bits.f = v;
    return bits.u;
}

BP_HD static inline uint64_t bp_hash_float(uint64_t h, float v) {
    return bp_hash_u32(h, bp_float_bits(v));
}

BP_HD static inline uint64_t bp_hash_double(uint64_t h, double v) {
    return bp_hash_u64(h, bp_double_bits(v));
}

/* Canonical subsystem components. Both Magma and the shared CPU/CUDA Blaze
 * core feed their authoritative scalar state through these exact functions. */
BP_HD static inline uint64_t bp_hash_stack3(
    uint64_t h, int32_t item, int32_t count, int32_t meta) {
    h = bp_hash_i32(h, item);
    h = bp_hash_i32(h, count);
    return bp_hash_i32(h, meta);
}

BP_HD static inline uint64_t bp_hash_item_entity(
    uint64_t h, double x, double y, double z,
    double mx, double my, double mz, int32_t on_ground, int32_t age,
    int32_t item, int32_t count, int32_t meta, int32_t pickup_delay,
    int32_t lifespan) {
    h = bp_hash_double(h, x);
    h = bp_hash_double(h, y);
    h = bp_hash_double(h, z);
    h = bp_hash_double(h, mx);
    h = bp_hash_double(h, my);
    h = bp_hash_double(h, mz);
    h = bp_hash_i32(h, on_ground);
    h = bp_hash_i32(h, age);
    h = bp_hash_stack3(h, item, count, meta);
    h = bp_hash_i32(h, pickup_delay);
    return bp_hash_i32(h, lifespan);
}

BP_HD static inline uint64_t bp_hash_furnace_state(
    uint64_t h, int32_t wx, int32_t wy, int32_t wz,
    int32_t input_item, int32_t input_count, int32_t input_meta,
    int32_t fuel_item, int32_t fuel_count, int32_t fuel_meta,
    int32_t output_item, int32_t output_count, int32_t output_meta,
    int32_t burn_time, int32_t current_burn_time,
    int32_t cook_time, int32_t total_cook) {
    h = bp_hash_i32(h, wx);
    h = bp_hash_i32(h, wy);
    h = bp_hash_i32(h, wz);
    h = bp_hash_stack3(h, input_item, input_count, input_meta);
    h = bp_hash_stack3(h, fuel_item, fuel_count, fuel_meta);
    h = bp_hash_stack3(h, output_item, output_count, output_meta);
    h = bp_hash_i32(h, burn_time);
    h = bp_hash_i32(h, current_burn_time);
    h = bp_hash_i32(h, cook_time);
    return bp_hash_i32(h, total_cook);
}

BP_HD static inline uint64_t bp_debug_pair_i32(int32_t low, int32_t high) {
    return (uint64_t)(uint32_t)low | ((uint64_t)(uint32_t)high << 32);
}

/* Canonical bounded-world digest. The origin is deliberately absent: index
 * zero is the region's (x0,y0,z0), and cells use snapshot order
 * ((x * ny + y) * nz + z). Every represented packed id<<4|meta state
 * participates. XOR of index-bound tokens makes an authoritative cell replace
 * O(1) while retaining dimensions and normalized relative coordinates. */
BP_HD static inline uint64_t bp_world_digest_cell_token(
    uint64_t index, uint16_t state) {
    uint64_t h = bp_hash_begin();
    h = bp_hash_u32(h, UINT32_C(0x4c4c4543)); /* "CELL" */
    h = bp_hash_u64(h, index);
    return bp_hash_u16(h, state);
}

BP_HD static inline uint64_t bp_world_digest_begin(
    int32_t nx, int32_t ny, int32_t nz) {
    uint64_t h = bp_hash_begin();
    h = bp_hash_u32(h, UINT32_C(0x31444c57)); /* "WLD1" */
    h = bp_hash_i32(h, nx);
    h = bp_hash_i32(h, ny);
    return bp_hash_i32(h, nz);
}

BP_HD static inline uint64_t bp_world_digest_add(
    uint64_t digest, uint64_t index, uint16_t state) {
    return digest ^ bp_world_digest_cell_token(index, state);
}

BP_HD static inline uint64_t bp_world_digest_replace(
    uint64_t digest, uint64_t index, uint16_t old_state,
    uint16_t new_state) {
    digest ^= bp_world_digest_cell_token(index, old_state);
    return digest ^ bp_world_digest_cell_token(index, new_state);
}

BP_HD static inline uint64_t bp_world_digest_cells(
    const uint16_t *cells, int32_t nx, int32_t ny, int32_t nz) {
    uint64_t digest = bp_world_digest_begin(nx, ny, nz);
    uint64_t count = (uint64_t)(uint32_t)nx * (uint64_t)(uint32_t)ny *
                     (uint64_t)(uint32_t)nz;
    uint64_t i;
    for (i = 0; i < count; ++i)
        digest = bp_world_digest_add(digest, i, cells[i]);
    return digest;
}

/* Canonical liquid-evolution digest. Scheduler fields (dimension, active
 * region AABB / cadence / quiet steps) are sequential FNV. Liquid cells in
 * the snapshot region use the same index space as bp_world_digest_* and an
 * XOR of (index, packed state) tokens so a write is O(1). Presence of ids
 * 8..11 is not enough: Magma and Blaze must both hash the live CA scheduler
 * plus every represented liquid cell. */
BP_HD static inline int bp_is_liquid_id(int32_t id) {
    return id >= 8 && id <= 11;
}

BP_HD static inline int bp_is_liquid_state(uint16_t state) {
    return bp_is_liquid_id((int32_t)(state >> 4));
}

BP_HD static inline uint64_t bp_fluid_cell_token(uint64_t index, uint16_t state) {
    uint64_t h = bp_hash_begin();
    h = bp_hash_u32(h, UINT32_C(0x4451494c)); /* "LIQD" */
    h = bp_hash_u64(h, index);
    return bp_hash_u16(h, state);
}

BP_HD static inline uint64_t bp_fluid_cells_add(
    uint64_t digest, uint32_t *ncells, uint64_t index, uint16_t state) {
    if (!bp_is_liquid_state(state)) return digest;
    if (ncells) ++*ncells;
    return digest ^ bp_fluid_cell_token(index, state);
}

BP_HD static inline uint64_t bp_fluid_cells_replace(
    uint64_t digest, uint32_t *ncells, uint64_t index,
    uint16_t old_state, uint16_t new_state) {
    int old_l = bp_is_liquid_state(old_state);
    int new_l = bp_is_liquid_state(new_state);
    if (old_l == new_l && old_state == new_state) return digest;
    if (old_l) {
        digest ^= bp_fluid_cell_token(index, old_state);
        if (ncells) --*ncells;
    }
    if (new_l) {
        digest ^= bp_fluid_cell_token(index, new_state);
        if (ncells) ++*ncells;
    }
    return digest;
}

BP_HD static inline uint64_t bp_hash_fluid_region(
    uint64_t h, int32_t active, int32_t x0, int32_t y0, int32_t z0,
    int32_t x1, int32_t y1, int32_t z1, int32_t has_water,
    int32_t quiet_steps) {
    h = bp_hash_i32(h, active);
    if (!active) return h;
    h = bp_hash_i32(h, x0);
    h = bp_hash_i32(h, y0);
    h = bp_hash_i32(h, z0);
    h = bp_hash_i32(h, x1);
    h = bp_hash_i32(h, y1);
    h = bp_hash_i32(h, z1);
    h = bp_hash_i32(h, has_water);
    return bp_hash_i32(h, quiet_steps);
}

BP_HD static inline uint64_t bp_fluid_digest_begin(int32_t dim, int32_t nregions) {
    uint64_t h = bp_hash_begin();
    h = bp_hash_u32(h, UINT32_C(0x31444c46)); /* "FLD1" */
    h = bp_hash_i32(h, dim);
    return bp_hash_i32(h, nregions);
}

BP_HD static inline uint64_t bp_fluid_digest_finish(
    uint64_t h, uint64_t cells_xor, uint32_t ncells, uint32_t mutations) {
    h = bp_hash_u64(h, cells_xor);
    h = bp_hash_u32(h, ncells);
    return bp_hash_u32(h, mutations);
}

/* Magma randtick.c dispatch: grass, leaves/leaves2, fire, wheat/carrot/potato. */
BP_HD static inline int bp_is_randtick_id(int32_t id) {
    return id == 2 || id == 18 || id == 51 || id == 59 ||
           id == 141 || id == 142 || id == 161;
}

BP_HD static inline int bp_is_randtick_state(uint16_t state) {
    return bp_is_randtick_id((int32_t)(state >> 4));
}

BP_HD static inline uint64_t bp_randtick_cell_token(uint64_t index, uint16_t state) {
    uint64_t h = bp_hash_begin();
    h = bp_hash_u32(h, UINT32_C(0x4c435452)); /* "RTCL" */
    h = bp_hash_u64(h, index);
    return bp_hash_u16(h, state);
}

BP_HD static inline uint64_t bp_randtick_cells_add(
    uint64_t digest, uint32_t *ncells, uint64_t index, uint16_t state) {
    if (!bp_is_randtick_state(state)) return digest;
    if (ncells) ++*ncells;
    return digest ^ bp_randtick_cell_token(index, state);
}

BP_HD static inline uint64_t bp_randtick_cells_replace(
    uint64_t digest, uint32_t *ncells, uint64_t index,
    uint16_t old_state, uint16_t new_state) {
    int old_r = bp_is_randtick_state(old_state);
    int new_r = bp_is_randtick_state(new_state);
    if (old_r == new_r && old_state == new_state) return digest;
    if (old_r) {
        digest ^= bp_randtick_cell_token(index, old_state);
        if (ncells) --*ncells;
    }
    if (new_r) {
        digest ^= bp_randtick_cell_token(index, new_state);
        if (ncells) ++*ncells;
    }
    return digest;
}

BP_HD static inline uint64_t bp_randtick_digest_begin(void) {
    uint64_t h = bp_hash_begin();
    return bp_hash_u32(h, UINT32_C(0x314b5452)); /* "RTK1" */
}

BP_HD static inline uint64_t bp_randtick_digest_finish(
    uint64_t h, uint64_t cells_xor, uint32_t ncells, uint32_t mutations) {
    h = bp_hash_u64(h, cells_xor);
    h = bp_hash_u32(h, ncells);
    return bp_hash_u32(h, mutations);
}

/* Magma live_sim.c: sand 12, gravel 13. */
BP_HD static inline int bp_is_falling_id(int32_t id) {
    return id == 12 || id == 13;
}

BP_HD static inline int bp_is_falling_state(uint16_t state) {
    return bp_is_falling_id((int32_t)(state >> 4));
}

BP_HD static inline uint64_t bp_falling_cell_token(uint64_t index, uint16_t state) {
    uint64_t h = bp_hash_begin();
    h = bp_hash_u32(h, UINT32_C(0x4c434c46)); /* "FLCL" */
    h = bp_hash_u64(h, index);
    return bp_hash_u16(h, state);
}

BP_HD static inline uint64_t bp_falling_cells_add(
    uint64_t digest, uint32_t *ncells, uint64_t index, uint16_t state) {
    if (!bp_is_falling_state(state)) return digest;
    if (ncells) ++*ncells;
    return digest ^ bp_falling_cell_token(index, state);
}

BP_HD static inline uint64_t bp_falling_cells_replace(
    uint64_t digest, uint32_t *ncells, uint64_t index,
    uint16_t old_state, uint16_t new_state) {
    int old_f = bp_is_falling_state(old_state);
    int new_f = bp_is_falling_state(new_state);
    if (old_f == new_f && old_state == new_state) return digest;
    if (old_f) {
        digest ^= bp_falling_cell_token(index, old_state);
        if (ncells) --*ncells;
    }
    if (new_f) {
        digest ^= bp_falling_cell_token(index, new_state);
        if (ncells) ++*ncells;
    }
    return digest;
}

BP_HD static inline uint64_t bp_hash_falling_entity(
    uint64_t h, double x, double y, double z,
    double mx, double my, double mz, int32_t on_ground, int32_t fall_time,
    int32_t block_id, int32_t meta) {
    h = bp_hash_double(h, x);
    h = bp_hash_double(h, y);
    h = bp_hash_double(h, z);
    h = bp_hash_double(h, mx);
    h = bp_hash_double(h, my);
    h = bp_hash_double(h, mz);
    h = bp_hash_i32(h, on_ground);
    h = bp_hash_i32(h, fall_time);
    h = bp_hash_i32(h, block_id);
    return bp_hash_i32(h, meta);
}

BP_HD static inline uint64_t bp_falling_digest_begin(void) {
    uint64_t h = bp_hash_begin();
    return bp_hash_u32(h, UINT32_C(0x314c4146)); /* "FAL1" */
}

BP_HD static inline uint64_t bp_falling_digest_finish(
    uint64_t h, uint64_t cells_xor, uint32_t ncells, uint32_t mutations) {
    h = bp_hash_u64(h, cells_xor);
    h = bp_hash_u32(h, ncells);
    return bp_hash_u32(h, mutations);
}

/* Magma runtime.c live arrows (type 1/2). Callers pass 0 for tile;
 * inGround/shake/ticksInGround ride sidecars. nents counts stuck arrows. */
BP_HD static inline uint64_t bp_hash_projectile(
    uint64_t h, int32_t type,
    double x, double y, double z,
    double mx, double my, double mz,
    int32_t in_ground, int32_t tile_x, int32_t tile_y, int32_t tile_z,
    int32_t shake, int32_t ticks_in_air, int32_t ticks_in_ground) {
    h = bp_hash_i32(h, type);
    h = bp_hash_double(h, x);
    h = bp_hash_double(h, y);
    h = bp_hash_double(h, z);
    h = bp_hash_double(h, mx);
    h = bp_hash_double(h, my);
    h = bp_hash_double(h, mz);
    h = bp_hash_i32(h, in_ground);
    h = bp_hash_i32(h, tile_x);
    h = bp_hash_i32(h, tile_y);
    h = bp_hash_i32(h, tile_z);
    h = bp_hash_i32(h, shake);
    h = bp_hash_i32(h, ticks_in_air);
    return bp_hash_i32(h, ticks_in_ground);
}

BP_HD static inline uint64_t bp_projectiles_digest_begin(void) {
    uint64_t h = bp_hash_begin();
    return bp_hash_u32(h, UINT32_C(0x314A5250)); /* "PRJ1" */
}

BP_HD static inline uint64_t bp_projectiles_digest_finish(
    uint64_t h, int32_t nents, uint32_t hits) {
    h = bp_hash_i32(h, nents);
    return bp_hash_u32(h, hits);
}

/* Magma runtime_explode + creeper fuse (EntityCreeper onUpdate ignited). */
BP_HD static inline uint64_t bp_explosions_digest_begin(void) {
    uint64_t h = bp_hash_begin();
    return bp_hash_u32(h, UINT32_C(0x31585045)); /* "EXP1" */
}

BP_HD static inline uint64_t bp_hash_explosion_pending(
    uint64_t h, int32_t pending,
    double x, double y, double z, float size) {
    h = bp_hash_i32(h, pending);
    if (!pending) return h;
    h = bp_hash_double(h, x);
    h = bp_hash_double(h, y);
    h = bp_hash_double(h, z);
    return bp_hash_float(h, size);
}

BP_HD static inline uint64_t bp_hash_explosion_blast(
    uint64_t h, uint64_t rays, uint32_t ndestroyed, uint32_t blasts,
    float damage, double kbx, double kby, double kbz) {
    h = bp_hash_u64(h, rays);
    h = bp_hash_u32(h, ndestroyed);
    h = bp_hash_u32(h, blasts);
    h = bp_hash_float(h, damage);
    h = bp_hash_double(h, kbx);
    h = bp_hash_double(h, kby);
    return bp_hash_double(h, kbz);
}

BP_HD static inline uint64_t bp_hash_creeper_fuse(
    uint64_t h, int32_t slot, int32_t fuse, int32_t ignited, int32_t alive) {
    h = bp_hash_i32(h, slot);
    h = bp_hash_i32(h, fuse);
    h = bp_hash_i32(h, ignited);
    return bp_hash_i32(h, alive);
}

/* World clock / weather. Magma live hashes rain_strength=0 (no fade). */
BP_HD static inline uint64_t bp_weather_digest(
    int64_t world_time, int64_t total_time,
    int32_t raining, int32_t thundering,
    int32_t rain_time, int32_t thunder_time,
    float rain_strength, float thunder_strength) {
    uint64_t h = bp_hash_begin();
    h = bp_hash_u32(h, UINT32_C(0x31545757)); /* "WWT1" */
    h = bp_hash_i64(h, world_time);
    h = bp_hash_i64(h, total_time);
    h = bp_hash_i32(h, raining);
    h = bp_hash_i32(h, thundering);
    h = bp_hash_i32(h, rain_time);
    h = bp_hash_i32(h, thunder_time);
    h = bp_hash_float(h, rain_strength);
    return bp_hash_float(h, thunder_strength);
}

/* Magma chest_live.c + container_live.c: every runtime chest TE in the
 * initial 64-slot table (pos, 27 id/count/meta slots, numPlayersUsing)
 * plus the player's 36 main inventory slots and cursor. Loot-table
 * identity is a named generation gap and is not hashed. */
#define BP_CHEST_SLOTS 27
#define BP_CHEST_TABLE 64
BP_HD static inline uint64_t bp_chests_digest_begin(void) {
    uint64_t h = bp_hash_begin();
    return bp_hash_u32(h, UINT32_C(0x31534843)); /* "CHS1" */
}

/* Magma tick_xp_orbs + EntityPlayer addExperience. */
BP_HD static inline uint64_t bp_hash_xp_orb(
    uint64_t h, double x, double y, double z,
    double mx, double my, double mz,
    int32_t on_ground, int32_t age, int32_t delay,
    int32_t value, int32_t eid, int32_t dead) {
    h = bp_hash_double(h, x);
    h = bp_hash_double(h, y);
    h = bp_hash_double(h, z);
    h = bp_hash_double(h, mx);
    h = bp_hash_double(h, my);
    h = bp_hash_double(h, mz);
    h = bp_hash_i32(h, on_ground);
    h = bp_hash_i32(h, age);
    h = bp_hash_i32(h, delay);
    h = bp_hash_i32(h, value);
    h = bp_hash_i32(h, eid);
    return bp_hash_i32(h, dead);
}

BP_HD static inline uint64_t bp_xp_digest_begin(void) {
    uint64_t h = bp_hash_begin();
    return bp_hash_u32(h, UINT32_C(0x314F5058)); /* "XPO1" */
}

BP_HD static inline uint64_t bp_xp_digest_finish(
    uint64_t h, int32_t nents, int32_t pickups,
    int32_t level, float experience, int32_t total, int32_t cooldown) {
    h = bp_hash_i32(h, nents);
    h = bp_hash_i32(h, pickups);
    h = bp_hash_i32(h, level);
    h = bp_hash_float(h, experience);
    h = bp_hash_i32(h, total);
    return bp_hash_i32(h, cooldown);
}

/* Magma tick_boat. */
BP_HD static inline uint64_t bp_boats_digest_begin(void) {
    uint64_t h = bp_hash_begin();
    return bp_hash_u32(h, UINT32_C(0x31544F42)); /* "BOT1" */
}

BP_HD static inline uint64_t bp_hash_boat(
    uint64_t h, int32_t slot, int32_t alive,
    double x, double y, double z,
    double mx, double my, double mz,
    float yaw, int32_t on_ground, int32_t status,
    float delta_rot, float glide, int32_t riding) {
    h = bp_hash_i32(h, slot);
    h = bp_hash_i32(h, alive);
    h = bp_hash_double(h, x);
    h = bp_hash_double(h, y);
    h = bp_hash_double(h, z);
    h = bp_hash_double(h, mx);
    h = bp_hash_double(h, my);
    h = bp_hash_double(h, mz);
    h = bp_hash_float(h, yaw);
    h = bp_hash_i32(h, on_ground);
    h = bp_hash_i32(h, status);
    h = bp_hash_float(h, delta_rot);
    h = bp_hash_float(h, glide);
    return bp_hash_i32(h, riding);
}

/* EntityPlayerSP START_FALL_FLYING + psv_elytra_travel. */
BP_HD static inline uint64_t bp_elytra_digest(
    int32_t equipped, int32_t flying, int32_t pending, int32_t pose,
    int32_t ticks, float wall_damage,
    double mx, double my, double mz, int32_t on_ground) {
    uint64_t h = bp_hash_begin();
    h = bp_hash_u32(h, UINT32_C(0x31594C45)); /* "ELY1" */
    h = bp_hash_i32(h, equipped);
    h = bp_hash_i32(h, flying);
    h = bp_hash_i32(h, pending);
    h = bp_hash_i32(h, pose);
    h = bp_hash_i32(h, ticks);
    h = bp_hash_float(h, wall_damage);
    h = bp_hash_double(h, mx);
    h = bp_hash_double(h, my);
    h = bp_hash_double(h, mz);
    return bp_hash_i32(h, on_ground);
}

BP_HD static inline void bp_record_init(BpParityRecord *r, int64_t tick) {
    unsigned i;
    r->magic = BP_PARITY_MAGIC;
    r->version = BP_PARITY_VERSION;
    r->size = (uint32_t)sizeof(*r);
    r->nsubsystems = BP_NSUBSYSTEMS;
    r->implemented_mask = BP_IMPLEMENTED_MASK;
    r->measured_mask = BP_MEASURED_MASK;
    r->active_mask = 0;
    r->tick = tick;
    for (i = 0; i < BP_NSUBSYSTEMS; ++i) {
        r->digest[i] = bp_hash_begin();
        r->evidence[i] = 0;
    }
    for (i = 0; i < BP_NDEBUG; ++i) r->debug_bits[i] = 0;
}

#undef BP_HD

#endif /* BLAZE_PORT_PARITY_H */
