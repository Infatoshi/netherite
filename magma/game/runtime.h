#ifndef MAGMA_GAME_RUNTIME_H
#define MAGMA_GAME_RUNTIME_H

#ifndef EW_MAX_ENTITIES
#define EW_MAX_ENTITIES 98
#endif
#ifndef EW_SPAWN_LIMIT
#define EW_SPAWN_LIMIT 96
#endif

#include "game/config.h"
#include "game/fluid_live.h"
#include "game/live_sim.h"
#include "game/player_ctl.h"
#include "potion_throwable.h"
#include "game/furnace_live.h"
#include "game/chest_live.h"
#include "game/brewing_live.h"
#include "game/enchanting_live.h"
#include "game/mob_live.h"
#include "game/village_live.h"
#include "game/villager_trade.h"
#include "game/dragon_live.h"
#include "game/container_live.h"
#include "game/portal_live.h"
#include "mc_gamerules.h"
#include "game/nbt_blob.h"
#include "fishing.h"

#define GM_RUNTIME_FURNACES 16
/* Growable chest TE table: starts at this capacity, doubles when full.
 * Never evicts a live TE while its block 54 still exists. */
#define GM_RUNTIME_CHESTS_INITIAL 64
#define GM_RUNTIME_ENDER_CHESTS_INITIAL 16
#define GM_ACTIVE_ENDER_CHEST (-2)
#define GM_ACTIVE_SHULKER_BOX (-3)
/* Static inventory tiles begin with dispenser/dropper. The growable cold pool
 * is reused by later brewing/hopper/shulker comparator slices; no idle tick
 * scans it. */
#define GM_RUNTIME_STATIC_CONTAINERS_INITIAL 16
#define GM_RUNTIME_STATIC_CONTAINERS_MAX 256
#define GM_RUNTIME_STATIC_CONTAINER_SLOTS 27
enum {
    GM_SHULKER_BOX_CLOSED = 0,
    GM_SHULKER_BOX_OPENING,
    GM_SHULKER_BOX_OPENED,
    GM_SHULKER_BOX_CLOSING
};
/* Inert command-block state is cold capsule data. Command execution remains
 * a separate slice, so this pool has no tick hook. */
#define GM_RUNTIME_COMMAND_BLOCKS_INITIAL 16
#define GM_RUNTIME_COMMAND_BLOCKS_MAX 256
#define GM_RUNTIME_STRUCTURE_BLOCKS_INITIAL 16
#define GM_RUNTIME_STRUCTURE_BLOCKS_MAX 256
#define GM_RUNTIME_STRUCTURE_TEMPLATES_INITIAL 4
#define GM_RUNTIME_STRUCTURE_TEMPLATES_MAX 64
#define GM_STRUCTURE_NAME_LENGTH 65
#define GM_STRUCTURE_AUTHOR_LENGTH 65
#define GM_STRUCTURE_METADATA_LENGTH 129
#define GM_STRUCTURE_TEMPLATE_SIDE 32
#define GM_STRUCTURE_TEMPLATE_CELLS \
    (GM_STRUCTURE_TEMPLATE_SIDE * GM_STRUCTURE_TEMPLATE_SIDE \
     * GM_STRUCTURE_TEMPLATE_SIDE)
#define GM_STRUCTURE_TEMPLATE_TILES_MAX 256
#define GM_STRUCTURE_TEMPLATE_ENTITIES_MAX 256
#define GM_RUNTIME_FLOWER_POTS_INITIAL 16
#define GM_RUNTIME_FLOWER_POTS_MAX 256
#define GM_RUNTIME_NOTE_BLOCKS_INITIAL 16
#define GM_RUNTIME_NOTE_BLOCKS_MAX 256
#define GM_RUNTIME_SKULLS_INITIAL 16
#define GM_RUNTIME_SKULLS_MAX 256
#define GM_RUNTIME_DECORATIVE_TILES_INITIAL 16
#define GM_RUNTIME_DECORATIVE_TILES_MAX 512
#define GM_RUNTIME_TAGGED_ITEMS_INITIAL 8
#define GM_RUNTIME_TAGGED_ITEMS_MAX 256
#define GM_RUNTIME_STACK_TAGS_INITIAL 16
#define GM_RUNTIME_STACK_TAGS_MAX 8192
#define GM_RUNTIME_ITEM_FRAMES_INITIAL 16
#define GM_RUNTIME_ITEM_FRAMES_MAX 256
#define GM_RUNTIME_PAINTINGS_INITIAL 16
#define GM_RUNTIME_PAINTINGS_MAX 256
#define GM_RUNTIME_LEASH_KNOTS_INITIAL 8
#define GM_RUNTIME_LEASH_KNOTS_MAX 256
#define GM_RUNTIME_ITEM_NAMES 64
#define GM_RUNTIME_ITEM_NAME_LENGTH_MAX 65535u
#define GM_RUNTIME_COLD_STORE_MAX 1048576
#define GM_RUNTIME_PROJECTILES 32
#define GM_RUNTIME_WITHERS 8
#define GM_RUNTIME_AREA_EFFECT_CLOUDS 16
#define GM_RUNTIME_FALLING_BLOCKS 16
#define GM_RUNTIME_WORLD_EVENT_CAPACITY GM_RUNTIME_FALLING_BLOCKS
#define GM_RUNTIME_PRIMED_TNT 16
#define GM_RUNTIME_END_CRYSTALS 16
#define GM_RUNTIME_PISTONS_INITIAL 64
/* Compat alias for fixtures which stage the historical boundary. */
#define GM_RUNTIME_PISTONS GM_RUNTIME_PISTONS_INITIAL
#define GM_RUNTIME_COMPARATORS 64
#define GM_RUNTIME_DAYLIGHT_DETECTORS 64
#define GM_RUNTIME_GHOSTS 16
#define GM_RUNTIME_GHOST_VIEWS 32  /* REC_ENT_MAX in the qrl recorder */
#define GM_RUNTIME_FIREBALL_TRACKS 8
#define GM_RUNTIME_LIGHTNING 8
#define GM_RUNTIME_WEATHER_EVENTS 32
#define GM_RUNTIME_FIREWORKS 16
#define GM_RUNTIME_FIREWORK_EVENTS 32
#define GM_RUNTIME_FIREWORK_TWINKLES 32
#define GM_RUNTIME_FISH_EVENTS 32
#define GM_RUNTIME_SOUND_EVENTS 256
#define GM_RUNTIME_PARTICLE_EVENTS 1024
#define GM_RUNTIME_MINECARTS 32
#define GM_RUNTIME_END_GATEWAYS 32
#define GM_RUNTIME_END_CITIES 64
#define GM_RUNTIME_MANSIONS 64
#define GM_RUNTIME_MANSION_RESIDENTS 512
#define GM_RUNTIME_MONUMENTS 64
#define GM_RUNTIME_SHULKERS_INITIAL 64
#define GM_RUNTIME_SHULKERS_MAX 2048
#define GM_RUNTIME_SHULKER_BULLETS_INITIAL 32
#define GM_RUNTIME_SHULKER_BULLETS_MAX 512
#define GM_RUNTIME_ARMOR_STANDS_INITIAL 32
#define GM_RUNTIME_ARMOR_STANDS_MAX 2048
#define GM_RUNTIME_VILLAGE_RESIDENTS 256
#define GM_RUNTIME_VILLAGE_STATES_MAX 16
#define GM_RUNTIME_VILLAGE_POSITION_QUEUE 65
#define GM_RUNTIME_IGLOO_RESIDENTS 64
#define GM_RUNTIME_SWAMP_WITCHES 64
#define GM_RUNTIME_SCHEDULED_TICKS_INITIAL 4096
#define GM_RUNTIME_STATISTICS_MAX (1u << 20)
#define GM_RUNTIME_POTION_EFFECTS 27
#define GM_RUNTIME_ARROW_EFFECTS GM_RUNTIME_POTION_EFFECTS
/* Compat alias for tests that still reference the old fixed size. */
#define GM_RUNTIME_CHESTS GM_RUNTIME_CHESTS_INITIAL
typedef struct {
    int active, type, age;
    int ticks_existed;
    int dimension;
    int eid;
    int controlled_stationary;
    int fire_ticks;
    int shooting_living;
    int shooter_eid;
    int shooter_uuid_pending;
    int shooter_uuid_present;
    long long shooter_uuid_most, shooter_uuid_least;
    int no_gravity;
    int player_thrower;
    int thrower_player_pending;
    int pearl_private_thrower;
    int ignore_player;
    int ignore_player_time;
    int portal_counter;
    int portal_cooldown;
    int in_portal;
    int last_portal_pos_valid;
    int last_portal_x, last_portal_y, last_portal_z;
    double last_portal_vec_x, last_portal_vec_y;
    int teleport_direction; /* EnumFacing horizontal index S/W/N/E = 0..3. */
    int throwable_shake;
    int throwable_in_ground;
    int throwable_ticks_in_ground;
    int throwable_ticks_in_air;
    int throwable_tile_x, throwable_tile_y, throwable_tile_z;
    int throwable_tile_block;
    int potion_item; /* TB_SPLASH_POTION or TB_LINGERING_POTION */
    int potion_type; /* TB_PT_* for thrown splash/lingering potions */
    int potion_color, potion_custom_color, potion_tag_id;
    int potion_effect_count;
    PtMobEffect potion_effects[GM_RUNTIME_POTION_EFFECTS];
    unsigned char potion_effect_flags[GM_RUNTIME_POTION_EFFECTS];
    float yaw, pitch;
    float prev_yaw, prev_pitch;
    double x, y, z, vx, vy, vz;
    double ax, ay, az; /* EntityFireball acceleration; zero for arrows/items */
    double eye_target_x, eye_target_y, eye_target_z;
    int eye_shatter_or_drop;
    double arrow_damage;
    uint64_t random_seed48;
    double random_next_gaussian;
    int random_have_gaussian;
    uint64_t client_random_seed48;
    int client_random_valid;
    int arrow_knockback;
    int arrow_critical;
    int arrow_pickup_status;
    int arrow_in_ground;
    int arrow_shake;
    int arrow_ticks_in_ground;
    /* EntityArrow.timeInGround is deliberately separate from the NBT `life`
     * counter above: vanilla does not serialize it, so save/load resets the
     * tipped-arrow custom-effect expiry clock without resetting despawn. */
    int arrow_time_in_ground;
    int arrow_tile_x, arrow_tile_y, arrow_tile_z;
    int arrow_tile_block, arrow_tile_meta;
    /* EntityArrow subclass payload. Projectile type still describes the
     * shooter path (1 player/dispenser, 2 living shooter); arrow_kind keeps
     * the registry class orthogonal to that hot collision dispatch. */
    int arrow_kind;
    int arrow_potion_type;
    int arrow_spectral_duration;
    int arrow_color, arrow_custom_color;
    int arrow_pickup_item, arrow_pickup_meta, arrow_pickup_tag_id;
    int arrow_effect_count;
    PtMobEffect arrow_effects[GM_RUNTIME_ARROW_EFFECTS];
    unsigned char arrow_effect_flags[GM_RUNTIME_ARROW_EFFECTS];
    int uuid_present;
    long long uuid_most, uuid_least;
    int wither_invulnerable;
    int wither_shooter_eid;
    int wither_ticks_in_air;
    int wither_life;
} GmRuntimeProjectile;

enum {
    GM_ARROW_NORMAL = 0,
    GM_ARROW_TIPPED,
    GM_ARROW_SPECTRAL
};
typedef struct {
    int active;
    int dimension;
    int eid;
    int uuid_present;
    long long uuid_most, uuid_least;
    double x, y, z, vx, vy, vz;
    float yaw, pitch, render_yaw_offset;
    float prev_render_yaw_offset;
    float rotation_yaw_head, prev_rotation_yaw_head;
    int body_rotation_tick_counter;
    float body_prev_render_yaw_head;
    float health;
    int invul_time;
    int ticks_existed;
    int hurt_time, death_time, hurt_resistant_time;
    int air, fire;
    int no_ai, no_gravity;
    int on_ground;
    float fall_distance;
    int in_water;
    int living_sound_time;
    float last_damage;
    int recently_hit;
    int attacking_player;
    uint64_t random_seed48;
    int random_have_gaussian;
    double random_gaussian;
    int watched_target[3];
    int watched_target_is_player[3];
    int next_head_update[2];
    int idle_head_updates[2];
    int block_break_counter;
    int target_eid;
    int target_is_player;
    int revenge_eid;
    int revenge_is_player;
    int revenge_timer;
    int hurt_target_task_active;
    int hurt_target_eid;
    int hurt_target_is_player;
    int hurt_revenge_timer_old;
    int hurt_target_unseen_ticks;
    int nearest_target_task_active;
    int target_task_tick;
    int goal_task_tick;
    int invul_task_active;
    int ranged_task_active;
    int ranged_attack_time;
    int ranged_see_time;
    float head_x_rotation[2], head_y_rotation[2];
    float head_x_rotation_prev[2], head_y_rotation_prev[2];
    int nether_star_dropped;
    int xp_dropped;
} GmRuntimeWither;
typedef struct {
    int eid;
    int potion_type;
    int potion_color, potion_custom_color;
    int potion_effect_count;
    PtMobEffect potion_effects[GM_RUNTIME_POTION_EFFECTS];
    unsigned char potion_effect_flags[GM_RUNTIME_POTION_EFFECTS];
    int player_owner;
    int uuid_present;
    long long uuid_most, uuid_least;
    int owner_present, owner_eid;
    long long owner_uuid_most, owner_uuid_least;
    int ignore_radius;
    int particle, particle_param1, particle_param2;
    int dimension;
    int air, fire_ticks, portal_cooldown;
    int on_ground, no_gravity, invulnerable, silent, glowing;
    int update_blocked, in_water, first_update;
    float fall_distance;
    double prev_x, prev_y, prev_z;
    double last_tick_x, last_tick_y, last_tick_z;
    uint64_t server_random_seed48;
    int server_random_have_gaussian;
    double server_random_gaussian;
    /* Client mirror cursor used only by the particle branch. */
    uint64_t random_seed48;
    PtAreaEffectCloud state;
    double x, y, z, vx, vy, vz;
    float yaw, pitch, prev_yaw, prev_pitch;
} GmRuntimeAreaEffectCloud;
typedef struct {
    int cloud_eid;
    int target_eid;
    int deadline;
} GmRuntimeAreaEffectCooldown;
typedef struct {
    int active;
    int eid;
    int block;
    int meta;
    int fall_time;
    double landing_y;
    int drop_on_land;
    int should_drop_item;
    int no_gravity;
    int no_ground;
    int on_ground;
    int collided_horizontally;
    int collided_vertically;
    float fall_distance;
    float impact_fall_distance;
    float yaw, pitch;
    int air, fire_ticks, portal_cooldown;
    int hurt_entities;
    float fall_hurt_amount;
    int fall_hurt_max;
    int dont_set_block;
    int tile_entity_tag_id;
    uint64_t random_seed48;
    int origin_x, origin_y, origin_z;
    double x, y, z;
    double vx, vy, vz;
    int bounding_box_valid;
    int uuid_present;
    int64_t uuid_most, uuid_least;
    double bb_min_x, bb_min_y, bb_min_z;
    double bb_max_x, bb_max_y, bb_max_z;
} GmRuntimeFallingBlock;
typedef struct {
    uint64_t seq;
    int id;
    int dimension;
    int x, y, z;
    int data;
} GmRuntimeWorldEvent;
typedef struct {
    int active;
    int dimension;
    int eid;
    int fuse;
    int uuid_present;
    int64_t uuid_most, uuid_least;
    double x, y, z;
    double vx, vy, vz;
    float yaw, pitch, fall_distance;
    int on_ground, no_gravity, air, fire_ticks, portal_cooldown;
    uint64_t random_seed48;
} GmRuntimePrimedTnt;
typedef struct {
    int active;
    int dimension;
    int eid;
    int uuid_present;
    int64_t uuid_most, uuid_least;
    int inner_rotation;
    int show_bottom;
    int has_beam;
    int beam_x, beam_y, beam_z;
    double x, y, z;
    double vx, vy, vz;
    float yaw, pitch, fall_distance;
    int on_ground, no_gravity, air, fire_ticks, portal_cooldown;
    uint64_t random_seed48;
} GmRuntimeEndCrystal;
enum {
    GM_DRAGON_RESPAWN_NONE = 0,
    GM_DRAGON_RESPAWN_START,
    GM_DRAGON_RESPAWN_PREPARING,
    GM_DRAGON_RESPAWN_PILLARS,
    GM_DRAGON_RESPAWN_DRAGON
};
typedef struct {
    int active;
    int dimension;
    int eid;
    int ticks_existed;
    int lightning_state;
    int living_time;
    int effect_only;
    long long bolt_vertex;
    uint64_t random_seed48;
    double x, y, z;
} GmRuntimeLightning;
enum {
    GM_WEATHER_EVENT_THUNDER = 1,
    GM_WEATHER_EVENT_IMPACT = 2
};
typedef struct {
    uint64_t seq;
    int kind;
    int eid;
    double x, y, z;
    float volume, pitch;
} GmRuntimeWeatherEvent;
typedef struct {
    int active;
    int dimension;
    int eid;
    int age, lifetime;
    int ticks_existed;
    int attached_player;
    int flight, explosion_count;
    int large_blast, twinkle;
    int firework_item_present;
    int firework_item, firework_count, firework_meta;
    int stack_tag_id;
    int state_exact;
    int uuid_present;
    int64_t uuid_most, uuid_least;
    uint64_t blast_random_seed48, twinkle_random_seed48;
    uint64_t random_seed48;
    int random_have_gaussian;
    double random_gaussian;
    float yaw, pitch, prev_yaw, prev_pitch;
    double x, y, z, vx, vy, vz;
} GmRuntimeFirework;
enum {
    GM_FIREWORK_EVENT_LAUNCH = 1,
    GM_FIREWORK_EVENT_EXPLODE = 2
};
typedef struct {
    uint64_t seq;
    int kind, eid, explosion_count;
    double x, y, z;
    float volume, pitch;
} GmRuntimeFireworkEvent;
typedef struct {
    int active, dimension, eid, ticks_left;
    uint64_t random_seed48;
    double x, y, z;
} GmRuntimeFireworkTwinkle;
enum {
    GM_FISH_STATE_FLYING = 0,
    GM_FISH_STATE_BOBBING = 1,
    GM_FISH_STATE_HOOKED = 2
};
enum {
    GM_FISH_EVENT_THROW = 1,
    GM_FISH_EVENT_SPLASH = 2,
    GM_FISH_EVENT_CATCH = 3,
    GM_FISH_EVENT_RETRACT = 4
};
typedef struct {
    int active, dimension, eid, state, in_ground;
    int ticks_in_ground, ticks_in_air, caught_eid, caught_kind, caught_slot;
    FishCatchState catch_state;
    JavaGaussianRandom random;
    float yaw, pitch;
    double x, y, z, vx, vy, vz;
} GmRuntimeFishHook;
typedef struct {
    uint64_t seq;
    int kind, eid, item, count, meta, xp, rod_damage;
    double x, y, z;
} GmRuntimeFishEvent;
#define GM_RUNTIME_SMELT_EVENTS 64
#define GM_RUNTIME_BREW_EVENTS 64
#define GM_RUNTIME_ITEM_STAT_LIMIT 512
enum {
    GM_SMELT_ACHIEVEMENT_NONE = 0,
    GM_SMELT_ACHIEVEMENT_ACQUIRE_IRON = 1,
    GM_SMELT_ACHIEVEMENT_COOK_FISH = 2
};
typedef struct {
    uint64_t seq;
    ICStack stack;
    int xp;
    int achievement;
    int achievement_awarded;
} GmRuntimeSmeltEvent;
typedef struct {
    uint64_t seq;
    ICStack stack;
    int achievement_awarded;
} GmRuntimeBrewEvent;
enum {
    GM_SOUND_CATEGORY_MASTER = 0,
    GM_SOUND_CATEGORY_MUSIC,
    GM_SOUND_CATEGORY_RECORDS,
    GM_SOUND_CATEGORY_WEATHER,
    GM_SOUND_CATEGORY_BLOCKS,
    GM_SOUND_CATEGORY_HOSTILE,
    GM_SOUND_CATEGORY_NEUTRAL,
    GM_SOUND_CATEGORY_PLAYERS,
    GM_SOUND_CATEGORY_AMBIENT,
    GM_SOUND_CATEGORY_VOICE
};
enum {
    GM_SOUND_CHICKEN_HURT = 1,
    GM_SOUND_CHICKEN_DEATH,
    GM_SOUND_PIG_HURT,
    GM_SOUND_PIG_DEATH,
    GM_SOUND_COW_HURT,
    GM_SOUND_COW_DEATH,
    GM_SOUND_SHEEP_HURT,
    GM_SOUND_SHEEP_DEATH,
    GM_SOUND_SHEEP_SHEAR,
    GM_SOUND_CHICKEN_EGG,
    GM_SOUND_ITEM_BUCKET_FILL,
    GM_SOUND_ITEM_ARMOR_EQUIP_GENERIC,
    GM_SOUND_PIG_SADDLE,
    GM_SOUND_LIGHTNING_THUNDER,
    GM_SOUND_LIGHTNING_IMPACT,
    GM_SOUND_FIREWORK_LAUNCH,
    GM_SOUND_FIREWORK_BLAST,
    GM_SOUND_FIREWORK_BLAST_FAR,
    GM_SOUND_FIREWORK_LARGE_BLAST,
    GM_SOUND_FIREWORK_LARGE_BLAST_FAR,
    GM_SOUND_FIREWORK_TWINKLE,
    GM_SOUND_FIREWORK_TWINKLE_FAR,
    GM_SOUND_BLOCK_WOOD_BREAK,
    GM_SOUND_BLOCK_GRAVEL_BREAK,
    GM_SOUND_BLOCK_GRASS_BREAK,
    GM_SOUND_BLOCK_STONE_BREAK,
    GM_SOUND_BLOCK_METAL_BREAK,
    GM_SOUND_BLOCK_GLASS_BREAK,
    GM_SOUND_BLOCK_CLOTH_BREAK,
    GM_SOUND_BLOCK_SAND_BREAK,
    GM_SOUND_BLOCK_SNOW_BREAK,
    GM_SOUND_BLOCK_LADDER_BREAK,
    GM_SOUND_BLOCK_ANVIL_BREAK,
    GM_SOUND_BLOCK_SLIME_BREAK,
    GM_SOUND_BLOCK_WOOD_PLACE,
    GM_SOUND_BLOCK_GRAVEL_PLACE,
    GM_SOUND_BLOCK_GRASS_PLACE,
    GM_SOUND_BLOCK_STONE_PLACE,
    GM_SOUND_BLOCK_METAL_PLACE,
    GM_SOUND_BLOCK_GLASS_PLACE,
    GM_SOUND_BLOCK_CLOTH_PLACE,
    GM_SOUND_BLOCK_SAND_PLACE,
    GM_SOUND_BLOCK_SNOW_PLACE,
    GM_SOUND_BLOCK_LADDER_PLACE,
    GM_SOUND_BLOCK_ANVIL_PLACE,
    GM_SOUND_BLOCK_SLIME_PLACE,
    GM_SOUND_BLOCK_WOOD_HIT,
    GM_SOUND_BLOCK_GRAVEL_HIT,
    GM_SOUND_BLOCK_GRASS_HIT,
    GM_SOUND_BLOCK_STONE_HIT,
    GM_SOUND_BLOCK_METAL_HIT,
    GM_SOUND_BLOCK_GLASS_HIT,
    GM_SOUND_BLOCK_CLOTH_HIT,
    GM_SOUND_BLOCK_SAND_HIT,
    GM_SOUND_BLOCK_SNOW_HIT,
    GM_SOUND_BLOCK_LADDER_HIT,
    GM_SOUND_BLOCK_ANVIL_HIT,
    GM_SOUND_BLOCK_SLIME_HIT,
    GM_SOUND_PLAYER_SMALL_FALL,
    GM_SOUND_PLAYER_BIG_FALL,
    GM_SOUND_BLOCK_WOOD_FALL,
    GM_SOUND_BLOCK_GRAVEL_FALL,
    GM_SOUND_BLOCK_GRASS_FALL,
    GM_SOUND_BLOCK_STONE_FALL,
    GM_SOUND_BLOCK_METAL_FALL,
    GM_SOUND_BLOCK_GLASS_FALL,
    GM_SOUND_BLOCK_CLOTH_FALL,
    GM_SOUND_BLOCK_SAND_FALL,
    GM_SOUND_BLOCK_SNOW_FALL,
    GM_SOUND_BLOCK_LADDER_FALL,
    GM_SOUND_BLOCK_ANVIL_FALL,
    GM_SOUND_BLOCK_SLIME_FALL,
    GM_SOUND_BLOCK_WOOD_STEP,
    GM_SOUND_BLOCK_GRAVEL_STEP,
    GM_SOUND_BLOCK_GRASS_STEP,
    GM_SOUND_BLOCK_STONE_STEP,
    GM_SOUND_BLOCK_METAL_STEP,
    GM_SOUND_BLOCK_GLASS_STEP,
    GM_SOUND_BLOCK_CLOTH_STEP,
    GM_SOUND_BLOCK_SAND_STEP,
    GM_SOUND_BLOCK_SNOW_STEP,
    GM_SOUND_BLOCK_LADDER_STEP,
    GM_SOUND_BLOCK_ANVIL_STEP,
    GM_SOUND_BLOCK_SLIME_STEP,
    GM_SOUND_PLAYER_SWIM,
    GM_SOUND_PLAYER_SPLASH,
    GM_SOUND_BOBBER_SPLASH,
    GM_SOUND_DISPENSER_DISPENSE,
    GM_SOUND_DISPENSER_FAIL,
    GM_SOUND_DISPENSER_LAUNCH,
    GM_SOUND_ENDEREYE_LAUNCH,
    GM_SOUND_FIREWORK_SHOOT,
    GM_SOUND_IRON_DOOR_OPEN,
    GM_SOUND_WOODEN_DOOR_OPEN,
    GM_SOUND_WOODEN_TRAPDOOR_OPEN,
    GM_SOUND_FENCE_GATE_OPEN,
    GM_SOUND_FIRE_EXTINGUISH,
    GM_SOUND_IRON_DOOR_CLOSE,
    GM_SOUND_WOODEN_DOOR_CLOSE,
    GM_SOUND_WOODEN_TRAPDOOR_CLOSE,
    GM_SOUND_FENCE_GATE_CLOSE,
    GM_SOUND_GHAST_WARN,
    GM_SOUND_GHAST_SHOOT,
    GM_SOUND_ENDERDRAGON_SHOOT,
    GM_SOUND_BLAZE_SHOOT,
    GM_SOUND_ZOMBIE_ATTACK_DOOR_WOOD,
    GM_SOUND_ZOMBIE_ATTACK_IRON_DOOR,
    GM_SOUND_ZOMBIE_BREAK_DOOR_WOOD,
    GM_SOUND_WITHER_BREAK_BLOCK,
    GM_SOUND_WITHER_SHOOT,
    GM_SOUND_BAT_TAKEOFF,
    GM_SOUND_ZOMBIE_INFECT,
    GM_SOUND_ZOMBIE_VILLAGER_CONVERTED,
    GM_SOUND_ZOMBIE_VILLAGER_CURE,
    GM_SOUND_ANVIL_DESTROY,
    GM_SOUND_ANVIL_USE,
    GM_SOUND_ANVIL_LAND,
    GM_SOUND_PORTAL_TRAVEL,
    GM_SOUND_CHORUS_FLOWER_GROW,
    GM_SOUND_CHORUS_FLOWER_DEATH,
    GM_SOUND_BREWING_STAND_BREW,
    GM_SOUND_IRON_TRAPDOOR_CLOSE,
    GM_SOUND_IRON_TRAPDOOR_OPEN,
    GM_SOUND_SPLASH_POTION_BREAK,
    GM_SOUND_ENDERDRAGON_FIREBALL_EXPLODE,
    GM_SOUND_END_GATEWAY_SPAWN,
    GM_SOUND_ENDERDRAGON_GROWL,
    GM_SOUND_VILLAGER_YES,
    GM_SOUND_VILLAGER_NO,
    GM_SOUND_RECORD_STOP,
    GM_SOUND_RECORD_13,
    GM_SOUND_RECORD_CAT,
    GM_SOUND_RECORD_BLOCKS,
    GM_SOUND_RECORD_CHIRP,
    GM_SOUND_RECORD_FAR,
    GM_SOUND_RECORD_MALL,
    GM_SOUND_RECORD_MELLOHI,
    GM_SOUND_RECORD_STAL,
    GM_SOUND_RECORD_STRAD,
    GM_SOUND_RECORD_WARD,
    GM_SOUND_RECORD_11,
    GM_SOUND_RECORD_WAIT,
    GM_SOUND_PLAYER_ATTACK_KNOCKBACK,
    GM_SOUND_PLAYER_ATTACK_SWEEP,
    GM_SOUND_PLAYER_ATTACK_CRIT,
    GM_SOUND_PLAYER_ATTACK_STRONG,
    GM_SOUND_PLAYER_ATTACK_WEAK,
    GM_SOUND_PLAYER_ATTACK_NODAMAGE,
    GM_SOUND_ZOMBIE_HURT,
    GM_SOUND_ZOMBIE_DEATH,
    GM_SOUND_ZOMBIE_VILLAGER_HURT,
    GM_SOUND_ZOMBIE_VILLAGER_DEATH,
    GM_SOUND_PIGMAN_HURT,
    GM_SOUND_PIGMAN_DEATH,
    GM_SOUND_SKELETON_HURT,
    GM_SOUND_SKELETON_DEATH,
    GM_SOUND_WITHER_SKELETON_HURT,
    GM_SOUND_WITHER_SKELETON_DEATH,
    GM_SOUND_CREEPER_HURT,
    GM_SOUND_CREEPER_DEATH,
    GM_SOUND_SPIDER_HURT,
    GM_SOUND_SPIDER_DEATH,
    GM_SOUND_ENDERMAN_HURT,
    GM_SOUND_ENDERMAN_DEATH,
    GM_SOUND_ENDERMAN_TELEPORT,
    GM_SOUND_BLAZE_HURT,
    GM_SOUND_BLAZE_DEATH,
    GM_SOUND_GHAST_HURT,
    GM_SOUND_GHAST_DEATH,
    GM_SOUND_SLIME_HURT,
    GM_SOUND_SLIME_DEATH,
    GM_SOUND_SMALL_SLIME_HURT,
    GM_SOUND_SMALL_SLIME_DEATH,
    GM_SOUND_MAGMA_HURT,
    GM_SOUND_MAGMA_DEATH,
    GM_SOUND_SMALL_MAGMA_HURT,
    GM_SOUND_SMALL_MAGMA_DEATH,
    GM_SOUND_SILVERFISH_HURT,
    GM_SOUND_SILVERFISH_DEATH,
    GM_SOUND_VILLAGER_HURT,
    GM_SOUND_VILLAGER_DEATH,
    GM_SOUND_LAVA_EXTINGUISH,
    GM_SOUND_NOTE_HARP,
    GM_SOUND_NOTE_BASEDRUM,
    GM_SOUND_NOTE_SNARE,
    GM_SOUND_NOTE_HAT,
    GM_SOUND_NOTE_BASS,
    GM_SOUND_WITCH_AMBIENT,
    GM_SOUND_WITCH_THROW,
    GM_SOUND_WITCH_DRINK,
    GM_SOUND_WITCH_HURT,
    GM_SOUND_WITCH_DEATH,
    GM_SOUND_HOSTILE_SPLASH,
    GM_SOUND_HOSTILE_SMALL_FALL,
    GM_SOUND_HOSTILE_BIG_FALL,
    GM_SOUND_GENERIC_SMALL_FALL,
    GM_SOUND_GENERIC_BIG_FALL,
    GM_SOUND_ARROW_SHOOT,
    GM_SOUND_ARROW_HIT,
    GM_SOUND_WOLF_HURT,
    GM_SOUND_WOLF_DEATH,
    GM_SOUND_OCELOT_HURT,
    GM_SOUND_OCELOT_DEATH,
    GM_SOUND_SHULKER_AMBIENT,
    GM_SOUND_SHULKER_CLOSE,
    GM_SOUND_SHULKER_DEATH,
    GM_SOUND_SHULKER_HURT,
    GM_SOUND_SHULKER_HURT_CLOSED,
    GM_SOUND_SHULKER_OPEN,
    GM_SOUND_SHULKER_SHOOT,
    GM_SOUND_SHULKER_TELEPORT,
    GM_SOUND_SHULKER_BULLET_HIT,
    GM_SOUND_SHULKER_BULLET_HURT,
    GM_SOUND_VINDICATOR_HURT,
    GM_SOUND_VINDICATOR_DEATH,
    GM_SOUND_EVOKER_HURT,
    GM_SOUND_EVOKER_DEATH,
    GM_SOUND_EVOKER_PREPARE_ATTACK,
    GM_SOUND_EVOKER_PREPARE_SUMMON,
    GM_SOUND_EVOKER_PREPARE_WOLOLO,
    GM_SOUND_EVOKER_CAST,
    GM_SOUND_EVOKER_FANGS,
    GM_SOUND_VINDICATOR_AMBIENT,
    GM_SOUND_EVOKER_AMBIENT,
    GM_SOUND_VEX_AMBIENT,
    GM_SOUND_VEX_CHARGE,
    GM_SOUND_VEX_HURT,
    GM_SOUND_VEX_DEATH,
    GM_SOUND_GUARDIAN_AMBIENT,
    GM_SOUND_GUARDIAN_AMBIENT_LAND,
    GM_SOUND_GUARDIAN_ATTACK,
    GM_SOUND_GUARDIAN_DEATH,
    GM_SOUND_GUARDIAN_DEATH_LAND,
    GM_SOUND_GUARDIAN_FLOP,
    GM_SOUND_GUARDIAN_HURT,
    GM_SOUND_GUARDIAN_HURT_LAND,
    GM_SOUND_ELDER_GUARDIAN_AMBIENT,
    GM_SOUND_ELDER_GUARDIAN_AMBIENT_LAND,
    GM_SOUND_ELDER_GUARDIAN_CURSE,
    GM_SOUND_ELDER_GUARDIAN_DEATH,
    GM_SOUND_ELDER_GUARDIAN_DEATH_LAND,
    GM_SOUND_ELDER_GUARDIAN_FLOP,
    GM_SOUND_ELDER_GUARDIAN_HURT,
    GM_SOUND_ELDER_GUARDIAN_HURT_LAND,
    GM_SOUND_IRON_GOLEM_ATTACK,
    GM_SOUND_IRON_GOLEM_HURT,
    GM_SOUND_IRON_GOLEM_DEATH,
    GM_SOUND_IRON_GOLEM_STEP,
    GM_SOUND_WITHER_AMBIENT,
    GM_SOUND_WITHER_HURT,
    GM_SOUND_WITHER_DEATH,
    GM_SOUND_WITHER_SPAWN,
    GM_SOUND_HORSE_AMBIENT,
    GM_SOUND_HORSE_ANGRY,
    GM_SOUND_HORSE_ARMOR,
    GM_SOUND_HORSE_BREATHE,
    GM_SOUND_HORSE_DEATH,
    GM_SOUND_HORSE_EAT,
    GM_SOUND_HORSE_GALLOP,
    GM_SOUND_HORSE_HURT,
    GM_SOUND_HORSE_JUMP,
    GM_SOUND_HORSE_LAND,
    GM_SOUND_HORSE_SADDLE,
    GM_SOUND_HORSE_STEP,
    GM_SOUND_HORSE_STEP_WOOD,
    GM_SOUND_DONKEY_AMBIENT,
    GM_SOUND_DONKEY_ANGRY,
    GM_SOUND_DONKEY_CHEST,
    GM_SOUND_DONKEY_DEATH,
    GM_SOUND_DONKEY_HURT,
    GM_SOUND_MULE_AMBIENT,
    GM_SOUND_MULE_CHEST,
    GM_SOUND_MULE_DEATH,
    GM_SOUND_MULE_HURT,
    GM_SOUND_SKELETON_HORSE_AMBIENT,
    GM_SOUND_SKELETON_HORSE_DEATH,
    GM_SOUND_SKELETON_HORSE_HURT,
    GM_SOUND_ZOMBIE_HORSE_AMBIENT,
    GM_SOUND_ZOMBIE_HORSE_DEATH,
    GM_SOUND_ZOMBIE_HORSE_HURT,
    GM_SOUND_LLAMA_AMBIENT,
    GM_SOUND_LLAMA_ANGRY,
    GM_SOUND_LLAMA_CHEST,
    GM_SOUND_LLAMA_DEATH,
    GM_SOUND_LLAMA_EAT,
    GM_SOUND_LLAMA_HURT,
    GM_SOUND_LLAMA_SPIT,
    GM_SOUND_LLAMA_STEP,
    GM_SOUND_LLAMA_SWAG,
    GM_SOUND_ARMOR_STAND_BREAK,
    GM_SOUND_ARMOR_STAND_FALL,
    GM_SOUND_ARMOR_STAND_HIT,
    GM_SOUND_ARMOR_STAND_PLACE,
    GM_SOUND_ITEM_ARMOR_EQUIP_LEATHER,
    GM_SOUND_ITEM_ARMOR_EQUIP_CHAIN,
    GM_SOUND_ITEM_ARMOR_EQUIP_IRON,
    GM_SOUND_ITEM_ARMOR_EQUIP_GOLD,
    GM_SOUND_ITEM_ARMOR_EQUIP_DIAMOND,
    GM_SOUND_ITEM_ARMOR_EQUIP_ELYTRA,
    GM_SOUND_ENTITY_GENERIC_EXTINGUISH_FIRE,
    GM_SOUND_PAINTING_BREAK,
    GM_SOUND_PAINTING_PLACE,
    GM_SOUND_LEASH_KNOT_BREAK,
    GM_SOUND_LEASH_KNOT_PLACE,
    GM_SOUND_ITEM_FRAME_ADD_ITEM,
    GM_SOUND_ITEM_FRAME_BREAK,
    GM_SOUND_ITEM_FRAME_PLACE,
    GM_SOUND_ITEM_FRAME_REMOVE_ITEM,
    GM_SOUND_ITEM_FRAME_ROTATE_ITEM,
    GM_SOUND_EGG_THROW,
    GM_SOUND_SNOWBALL_THROW,
    GM_SOUND_EXPERIENCE_BOTTLE_THROW,
    GM_SOUND_ENDER_PEARL_THROW,
    GM_SOUND_SPLASH_POTION_THROW,
    GM_SOUND_LINGERING_POTION_THROW,
    GM_SOUND_ENDER_CHEST_OPEN,
    GM_SOUND_ENDER_CHEST_CLOSE,
    GM_SOUND_SHULKER_BOX_OPEN,
    GM_SOUND_SHULKER_BOX_CLOSE,
    GM_SOUND_CHEST_OPEN,
    GM_SOUND_CHEST_CLOSE,
    GM_SOUND_BAT_AMBIENT,
    GM_SOUND_BAT_HURT,
    GM_SOUND_BAT_DEATH,
    GM_SOUND_MOOSHROOM_SHEAR,
    GM_SOUND_SNOWMAN_AMBIENT,
    GM_SOUND_SNOWMAN_HURT,
    GM_SOUND_SNOWMAN_DEATH,
    GM_SOUND_SNOWMAN_SHOOT,
    GM_SOUND_ENDERMITE_AMBIENT,
    GM_SOUND_ENDERMITE_HURT,
    GM_SOUND_ENDERMITE_DEATH,
    GM_SOUND_ENDERMITE_STEP,
    GM_SOUND_HUSK_AMBIENT,
    GM_SOUND_HUSK_HURT,
    GM_SOUND_HUSK_DEATH,
    GM_SOUND_HUSK_STEP,
    GM_SOUND_STRAY_AMBIENT,
    GM_SOUND_STRAY_HURT,
    GM_SOUND_STRAY_DEATH,
    GM_SOUND_STRAY_STEP,
    GM_SOUND_POLAR_BEAR_AMBIENT,
    GM_SOUND_POLAR_BEAR_BABY_AMBIENT,
    GM_SOUND_POLAR_BEAR_HURT,
    GM_SOUND_POLAR_BEAR_DEATH,
    GM_SOUND_POLAR_BEAR_STEP,
    GM_SOUND_POLAR_BEAR_WARNING,
    GM_SOUND_RABBIT_AMBIENT,
    GM_SOUND_RABBIT_ATTACK,
    GM_SOUND_RABBIT_DEATH,
    GM_SOUND_RABBIT_HURT,
    GM_SOUND_RABBIT_JUMP,
    GM_SOUND_PLAYER_BURP,
    GM_SOUND_CHORUS_FRUIT_TELEPORT,
    GM_SOUND_COUNT
};
typedef struct {
    uint64_t seq;
    int sound, category, eid, dimension;
    int relative, delay_ticks;
    double x, y, z;
    float volume, pitch;
} GmRuntimeSoundEvent;
typedef struct {
    int kind, dimension;
    /* count >= 0 is an SPacketParticles descriptor. count == -1 is an
     * entity-attached ParticleEmitter using entity_width/entity_height. */
    int count, entity_eid;
    double x, y, z;
    double motion_x, motion_y, motion_z;
    double offset_x, offset_y, offset_z, speed;
    float entity_width, entity_height;
    int parameter_count;
    int parameters[2];
} GmRuntimeParticleEvent;
enum {
    GM_PARTICLE_EXPLOSION_NORMAL = 0,
    GM_PARTICLE_EXPLOSION_LARGE = 1,
    GM_PARTICLE_WATER_BUBBLE = 4,
    GM_PARTICLE_WATER_SPLASH = 5,
    GM_PARTICLE_CRIT = 9,
    GM_PARTICLE_CRIT_MAGIC = 10,
    GM_PARTICLE_SMOKE_NORMAL = 11,
    GM_PARTICLE_SPELL_MOB = 15,
    GM_PARTICLE_SMOKE_LARGE = 12,
    GM_PARTICLE_NOTE = 23,
    GM_PARTICLE_PORTAL = 24,
    GM_PARTICLE_SNOWBALL = 31,
    GM_PARTICLE_HEART = 34,
    GM_PARTICLE_ITEM_CRACK = 36,
    GM_PARTICLE_BLOCK_CRACK = 37,
    GM_PARTICLE_BLOCK_DUST = 38,
    GM_PARTICLE_END_ROD = 43,
    GM_PARTICLE_DAMAGE_INDICATOR = 44,
    GM_PARTICLE_SWEEP_ATTACK = 45,
    GM_PARTICLE_SPIT = 48
};
enum {
    GM_MINECART_RIDEABLE = 0,
    GM_MINECART_CHEST = 1,
    GM_MINECART_FURNACE = 2,
    GM_MINECART_TNT = 3,
    GM_MINECART_SPAWNER = 4,
    GM_MINECART_HOPPER = 5,
    GM_MINECART_COMMAND = 6
};
typedef struct {
    int active, dimension, eid, kind;
    int ticks_existed;
    int uuid_present;
    int64_t uuid_most, uuid_least;
    int reverse, rolling_amplitude, rolling_direction;
    float damage, yaw, pitch, fall_distance;
    double x, y, z, vx, vy, vz;
    int on_ground, air, fire_ticks, portal_cooldown, no_gravity;
    int custom_display, display_block, display_meta, display_offset;
    int fuel, tnt_fuse, hopper_enabled, transfer_cooldown;
    int spawner_entity_type, spawner_delay;
    int spawner_min_delay, spawner_max_delay, spawner_spawn_count;
    int spawner_max_nearby, spawner_spawn_range, spawner_activate_range;
    int spawner_nbt_tag_id, spawner_default_entity_nbt;
    int spawner_potential_count;
    int spawner_potential_cap;
    GmSpawnerPotential *spawner_potentials;
    double spawner_mob_rotation, spawner_prev_mob_rotation;
    int command_tag_id, command_name_tag_id, command_last_output_tag_id;
    int command_success_count, command_track_output;
    int command_activator_cooldown;
    double push_x, push_z;
    uint64_t random_seed48;
    int random_have_gaussian;
    double random_gaussian;
    ICStack slots[GM_RUNTIME_STATIC_CONTAINER_SLOTS];
} GmRuntimeMinecart;
typedef struct {
    int active, dimension;
    int x, y, z;
    long long age;
    int teleport_cooldown;
    int has_exit, exact_teleport;
    int exit_x, exit_y, exit_z;
} GmRuntimeEndGateway;
typedef struct {
    int active, dimension, eid;
    int x, y, z;               /* ATTACHED_BLOCK_POS */
    int face;                  /* EnumFacing D,U,N,S,W,E */
    int no_ai;
    int peek_tick, peek_time;
    int attack_time, has_player_target;
    int watch_time, idle_look_time, living_sound_time;
    int ticks_existed, hurt_time, hurt_resistant_time, death_time;
    float health, last_damage;
    float prev_peek_amount, peek_amount;
    float head_yaw, head_pitch;
    uint64_t random_seed48;
} GmRuntimeShulker;
typedef struct {
    int active, dimension, eid, owner_eid;
    int direction, steps, ticks_existed;
    double x, y, z, vx, vy, vz;
    double target_dx, target_dy, target_dz;
    float yaw, pitch;
    uint64_t random_seed48;
} GmRuntimeShulkerBullet;
typedef struct {
    float x, y, z;
} GmRuntimeRotation;
enum {
    GM_ARMOR_STAND_MAINHAND = 0,
    GM_ARMOR_STAND_OFFHAND = 1,
    GM_ARMOR_STAND_FEET = 2,
    GM_ARMOR_STAND_LEGS = 3,
    GM_ARMOR_STAND_CHEST = 4,
    GM_ARMOR_STAND_HEAD = 5,
    GM_ARMOR_STAND_SLOTS = 6
};
enum {
    GM_ARMOR_STAND_HEAD_POSE = 0,
    GM_ARMOR_STAND_BODY_POSE = 1,
    GM_ARMOR_STAND_LEFT_ARM_POSE = 2,
    GM_ARMOR_STAND_RIGHT_ARM_POSE = 3,
    GM_ARMOR_STAND_LEFT_LEG_POSE = 4,
    GM_ARMOR_STAND_RIGHT_LEG_POSE = 5,
    GM_ARMOR_STAND_POSE_PARTS = 6
};
enum {
    GM_ARMOR_STAND_SMALL = 1,
    GM_ARMOR_STAND_SHOW_ARMS = 4,
    GM_ARMOR_STAND_NO_BASE_PLATE = 8,
    GM_ARMOR_STAND_MARKER = 16
};
enum {
    GM_ARMOR_STAND_DAMAGE_OTHER = 0,
    GM_ARMOR_STAND_DAMAGE_OUT_OF_WORLD,
    GM_ARMOR_STAND_DAMAGE_EXPLOSION,
    GM_ARMOR_STAND_DAMAGE_IN_FIRE,
    GM_ARMOR_STAND_DAMAGE_ON_FIRE,
    GM_ARMOR_STAND_DAMAGE_PLAYER,
    GM_ARMOR_STAND_DAMAGE_ARROW
};
enum {
    GM_ARMOR_STAND_EFFECTS_MAX = 32,
    GM_ARMOR_STAND_TAGS_MAX = 1024
};
typedef struct {
    int id, amplifier, duration;
    int ambient, show_particles;
} GmRuntimeArmorStandEffect;
typedef struct {
    int active, dimension, eid;
    int uuid_present;
    int64_t uuid_most, uuid_least;
    double x, y, z, vx, vy, vz;
    float yaw, pitch, health, fall_distance;
    int on_ground, no_gravity, invisible, status, disabled_slots;
    int air, in_water, ticks_existed, fire_ticks;
    int hurt_time, death_time, hurt_resistant_time;
    float last_damage;
    float absorption, max_health, max_health_base;
    int revenge_timer, portal_cooldown;
    int custom_name_tag_id, custom_name_visible;
    int silent, glowing, invulnerable, update_blocked, fall_flying;
    int vehicle_eid;
    int effect_count;
    GmRuntimeArmorStandEffect effects[GM_ARMOR_STAND_EFFECTS_MAX];
    int tag_count;
    int tag_ids[GM_ARMOR_STAND_TAGS_MAX];
    long long punch_cooldown;
    uint64_t random_seed48;
    int random_have_gaussian;
    double random_gaussian;
    ICStack equipment[GM_ARMOR_STAND_SLOTS];
    GmRuntimeRotation pose[GM_ARMOR_STAND_POSE_PARTS];
} GmRuntimeArmorStand;
typedef struct {
    int active;
    int dimension;
    int x, y, z;
    int moved_block;
    int moved_meta;
    int facing;
    int extending;
    int source;
    float progress;
    float last_progress;
} GmRuntimePiston;
typedef struct {
    int x, y, z;
} GmRuntimePistonRecheck;
typedef struct {
    int pending;
    int moving;
    int rotating;
    int on_ground;
    double x, y, z;
    float yaw, pitch;
} GmRuntimeMovePacket;
typedef struct {
    int pending;
    int eid;
    uint64_t seq;
    double x, y, z;
    float yaw, pitch;
} GmRuntimePigVehiclePacket;
typedef struct {
    int valid;
    int eid;
    uint64_t source_seq;
    uint64_t ack_seq;
    double x, y, z;
    float yaw, pitch;
} GmRuntimePigVehicleCorrection;
typedef struct {
    int active, wx, wy, wz;
    /* TileEntityLockable.customName. Zero means the vanilla translated
     * "Furnace" name; nonzero indexes the runtime's cold name registry. */
    int custom_name;
    FurnaceLive state;
} GmRuntimeFurnace;
typedef struct {
    int active, wx, wy, wz;
    ChestLive state;
} GmRuntimeChest;
typedef struct {
    int active, dimension, wx, wy, wz;
    float lid_angle, prev_lid_angle;
    int num_players_using;
    int ticks_since_sync;
} GmRuntimeEnderChest;
enum { GM_BEACON_SEGMENTS_MAX = 256 };
typedef struct {
    float red, green, blue;
    int height;
} GmRuntimeBeaconSegment;
typedef struct {
    int active;
    int dimension;
    int wx, wy, wz;
    int block;
    int size;
    ICStack slots[GM_RUNTIME_STATIC_CONTAINER_SLOTS];
    /* Used only by block 117. NBT persists BrewTime and Fuel; ingredient_id
     * is deliberately runtime-only, matching TileEntityBrewingStand. */
    BrewingLiveState brewing;
    /* Used only by block 154. TileEntityHopper decrements this before each
     * tile tick; ticked_game_time resolves same-boundary hopper chains. */
    int transfer_cooldown;
    long long ticked_game_time;
    /* Complete dropped ItemStack tag for shulker boxes. This is cold save
     * state: BlockEntityTag plus the duplicated display.Name when present. */
    GmNbtBlob item_tag;
    /* TileEntityShulkerBox transient state. NBT deliberately omits these;
     * native checkpoints retain them so a fork can continue mid-animation. */
    int shulker_open_count;
    int shulker_animation_status;
    float shulker_progress;
    float shulker_progress_old;
    /* TileEntityBeacon state. Payment is slots[0], which is deliberately
     * transient in Java NBT but retained by native checkpoints. The beam
     * segment list is client state rebuilt by updateBeacon every 80 ticks. */
    int beacon_complete;
    int beacon_levels;
    int beacon_primary;
    int beacon_secondary;
    long long beacon_render_counter;
    float beacon_render_scale;
    int beacon_segment_count;
    GmRuntimeBeaconSegment beacon_segments[GM_BEACON_SEGMENTS_MAX];
} GmRuntimeStaticContainer;
typedef struct {
    int active;
    int dimension;
    int wx, wy, wz;
    int block;
    int success_count;
    int command_tag_id;
    int last_output_tag_id;
    int powered;
    int automatic;
    int condition_met;
} GmRuntimeCommandBlock;
enum {
    GM_STRUCTURE_MODE_SAVE = 0,
    GM_STRUCTURE_MODE_LOAD = 1,
    GM_STRUCTURE_MODE_CORNER = 2,
    GM_STRUCTURE_MODE_DATA = 3
};
enum {
    GM_STRUCTURE_MIRROR_NONE = 0,
    GM_STRUCTURE_MIRROR_LEFT_RIGHT = 1,
    GM_STRUCTURE_MIRROR_FRONT_BACK = 2
};
enum {
    GM_STRUCTURE_ROTATION_NONE = 0,
    GM_STRUCTURE_ROTATION_CW90 = 1,
    GM_STRUCTURE_ROTATION_CW180 = 2,
    GM_STRUCTURE_ROTATION_CCW90 = 3
};
typedef struct GmRuntimeStructureBlock {
    int active;
    int dimension;
    int wx, wy, wz;
    char name[GM_STRUCTURE_NAME_LENGTH];
    char author[GM_STRUCTURE_AUTHOR_LENGTH];
    char metadata[GM_STRUCTURE_METADATA_LENGTH];
    int pos_x, pos_y, pos_z;
    int size_x, size_y, size_z;
    int mirror, rotation, mode;
    int ignore_entities, powered, show_air, show_bounding_box;
    float integrity;
    long long seed;
} GmRuntimeStructureBlock;
enum {
    GM_STRUCTURE_GUI_NAME = 0,
    GM_STRUCTURE_GUI_POS_X,
    GM_STRUCTURE_GUI_POS_Y,
    GM_STRUCTURE_GUI_POS_Z,
    GM_STRUCTURE_GUI_SIZE_X,
    GM_STRUCTURE_GUI_SIZE_Y,
    GM_STRUCTURE_GUI_SIZE_Z,
    GM_STRUCTURE_GUI_INTEGRITY,
    GM_STRUCTURE_GUI_SEED,
    GM_STRUCTURE_GUI_METADATA,
    GM_STRUCTURE_GUI_FIELD_COUNT
};
/* Client-side GuiEditStructure form state. Java edits text locally and sends
 * one MC|Struct payload only on Done/Save/Load/Detect, so keeping the strings
 * separate from the authoritative tile prevents an unsubmitted field from
 * changing a redstone-triggered save. This is pointer-free and checkpointed,
 * allowing a native fork to continue while the non-pausing screen is open. */
typedef struct {
    int active;
    int focus;
    GmRuntimeStructureBlock value;
    char pos_x[16], pos_y[16], pos_z[16];
    char size_x[16], size_y[16], size_z[16];
    char integrity[16];
    char seed[32];
} GmRuntimeStructureGui;
enum {
    GM_STRUCTURE_TILE_NONE = 0,
    GM_STRUCTURE_TILE_CHEST,
    GM_STRUCTURE_TILE_FURNACE,
    GM_STRUCTURE_TILE_ENDER_CHEST,
    GM_STRUCTURE_TILE_STATIC_CONTAINER,
    GM_STRUCTURE_TILE_COMMAND_BLOCK,
    GM_STRUCTURE_TILE_STRUCTURE_BLOCK,
    GM_STRUCTURE_TILE_FLOWER_POT,
    GM_STRUCTURE_TILE_NOTE_BLOCK,
    GM_STRUCTURE_TILE_SKULL
};
/* Compact Template BlockInfo.tileentityData payload. Java NBT does not retain
 * chest/ender/shulker animation, beacon beam segments, or hopper tick time,
 * so those transient fields are intentionally absent. blob_tag_id reuses the
 * runtime's content-interned NBT table for player-skull profiles. */
typedef struct {
    uint16_t cell;
    unsigned char kind;
    unsigned char slot_count;
    ICStack slots[GM_RUNTIME_STATIC_CONTAINER_SLOTS];
    int values[16];
    long long longs[2];
    int blob_tag_id;
    char name[GM_STRUCTURE_NAME_LENGTH];
    char author[GM_STRUCTURE_AUTHOR_LENGTH];
    char metadata[GM_STRUCTURE_METADATA_LENGTH];
} GmRuntimeStructureTile;
/* TemplateManager state is deliberately cold. The dense 32^3 state grid
 * keeps lookup and native-checkpoint serialization bounded; present[] is
 * separate because packed state 0xffff is itself a valid runtime state. */
typedef struct {
    int active;
    char name[GM_STRUCTURE_NAME_LENGTH];
    char author[GM_STRUCTURE_AUTHOR_LENGTH];
    int size_x, size_y, size_z;
    uint16_t states[GM_STRUCTURE_TEMPLATE_CELLS];
    unsigned char present[GM_STRUCTURE_TEMPLATE_CELLS];
    int tile_count, tiles_cap;
    GmRuntimeStructureTile *tiles;
    int entity_count, entities_cap;
    GmMobTemplateEntity *entities;
} GmRuntimeStructureTemplate;
typedef struct {
    int active;
    int dimension;
    int wx, wy, wz;
    int item, meta;
} GmRuntimeFlowerPot;
typedef struct {
    int active;
    int dimension;
    int wx, wy, wz;
    int note, powered;
} GmRuntimeNoteBlock;
typedef struct {
    int active;
    int dimension;
    int wx, wy, wz;
    int type, rotation;
    GmNbtBlob owner_profile;
} GmRuntimeSkull;
/* Arbitrary semantic tile NBT for signs and banners plus the exact ItemStack
 * tag emitted by banner teardown. Both ids address the runtime's validated,
 * content-interned NBT table and therefore remain pointer-free checkpoints. */
typedef struct {
    int active;
    int dimension;
    int wx, wy, wz;
    int block;
    int tile_tag_id;
    int drop_item, drop_meta, drop_tag_id;
} GmRuntimeDecorativeTile;
typedef struct {
    int active;
    int eid;
    int item;
    int size;
    ICStack slots[GM_RUNTIME_STATIC_CONTAINER_SLOTS];
    GmNbtBlob tag;
} GmRuntimeTaggedItem;
typedef struct {
    GmNbtBlob tag;
} GmRuntimeStackTag;
/* WorldSavedData for filled maps is keyed by the ItemStack damage value and
 * is not part of the stack NBT. Keep the recipe-visible portion in a cold,
 * growable table so map scaling makes the same world-data decision as Java. */
typedef struct {
    int active;
    int map_id;
    int scale;
    int has_exploration_marker;
    int dimension;
    int x_center, z_center;
    int tracking_position, unlimited_tracking;
    int update_step;
    unsigned char colors[128 * 128];
} GmRuntimeMapData;
typedef struct {
    int active;
    size_t len;
    char *text;
} GmRuntimeItemName;

typedef struct {
    int active;
    int dimension;
    int eid;
    int uuid_present;
    long long uuid_most, uuid_least;
    double x, y, z;
    int hanging_x, hanging_y, hanging_z;
    int facing;
    int item, count, meta;
    int rotation;
    int tick_counter;
    float item_drop_chance;
    unsigned long long random_seed48;
    int random_have_gaussian;
    double random_gaussian;
    int repair_cost, custom_name, tag_id;
    int n_enchants;
    IcEnch enchants[IC_MAX_ENCHANTS];
    /* EntityTrackerEntry owns the transient frame marker outside frame NBT.
     * MapData itself is cold state; keeping its exact 128x128 byte plane here
     * makes arbitrary filled maps renderable and checkpoint-safe without
     * touching the hot entity SoA. */
    int tracker_update_counter;
    int map_data_present;
    int map_dimension, map_x_center, map_z_center, map_scale;
    int map_tracking_position, map_unlimited_tracking;
    int map_decoration_present;
    int map_decoration_type;
    int map_decoration_x, map_decoration_z, map_decoration_rotation;
    int map_colors_present;
    unsigned char map_colors[128 * 128];
} GmRuntimeItemFrame;
typedef struct {
    int active;
    int dimension;
    int eid;
    int uuid_present;
    long long uuid_most, uuid_least;
    double x, y, z;
    int hanging_x, hanging_y, hanging_z;
    int facing;
    int art;
    int tick_counter;
} GmRuntimePainting;
typedef struct {
    int active;
    int dimension;
    int eid;
    int uuid_present;
    long long uuid_most, uuid_least;
    double x, y, z;
    int hanging_x, hanging_y, hanging_z;
    int tick_counter;
} GmRuntimeLeashKnot;
typedef struct {
    int dimension;
    int x, y, z;
    int block;
    long long time;
    int priority;
    long long order;
} GmRuntimeScheduledTick;
typedef struct {
    int x, y, z;
    long long time;
} GmRuntimeRedstoneTorchToggle;
typedef struct {
    int active;
    int dimension;
    int x, y, z;
    int output_signal;
} GmRuntimeComparator;
typedef struct {
    int dimension;
    int x, y, z;
} GmRuntimeDaylightDetector;

typedef struct {
    int chunk_x, chunk_z;
} GmRuntimeChunkRef;
typedef GmRuntimeChunkRef GmRuntimeEndPopulationChunk;
typedef struct {
    int x, y, z;
    int eid;
    unsigned char profession;
    GmVillagerTrade trade;
} GmRuntimeVillageResident;
typedef struct {
    double x, y, z, vx, vy, vz;
    float health, yaw, pitch;
    int conversion_time, fire, air;
    int eid;
    signed char profession;
    unsigned char kind, persistence, on_ground;
} GmRuntimeIglooResident;
typedef struct {
    double x, y, z;
    int eid;
} GmRuntimeSwampWitch;
typedef struct {
    int x, y, z;
    int eid;
    unsigned char kind; /* 1 evoker, 2 vindicator */
} GmRuntimeMansionResident;
typedef struct {
    int x, y, z;
} GmRuntimeBlockPos;

typedef struct {
    int chunk_x, chunk_z;
    unsigned random_tick_mask;
    int present;
} GmRuntimeTickingChunk;

typedef struct {
    int chunk_x, chunk_z;
    int present;
} GmRuntimeLoadedChunk;

typedef struct {
    int chunk_x, chunk_z;
    int eligible;
} GmRuntimePendingChunkUnload;

/* Java's loadedEntityList and loadedTileEntityList are causal ordered stores,
 * not sets. Keep their imported order independently of the type-specific hot
 * pools so checkpoint/reload never reconstructs it from EIDs or positions. */
#define GM_RUNTIME_LOADED_ENTITY_ORDER 4096
#define GM_RUNTIME_LOADED_TILE_ORDER 4096
typedef struct {
    int x, y, z;
} GmRuntimeLoadedTile;

/* Ordinary EntityItem/falling state, Java loaded-list order, and server chunk
 * membership are active-dimension stores. The specialized mob/tile pools
 * already carry explicit dimensions; retain these remaining current-world
 * lists here while the player is away. Chunk arrays transfer ownership when
 * the active dimension changes. */
typedef struct {
    int valid;
    GmLiveSim entities;
    int *loaded_entity_order;
    int loaded_entity_order_count, loaded_entity_order_cap;
    GmRuntimeLoadedTile *loaded_tile_order;
    int loaded_tile_order_count, loaded_tile_order_cap;
    GmRuntimeLoadedTile *tickable_tile_order;
    int tickable_tile_order_count, tickable_tile_order_cap;
    GmRuntimeTickingChunk *ticking_chunks;
    int ticking_chunk_count, ticking_chunks_cap;
    int ticking_chunks_authoritative;
    GmRuntimeLoadedChunk *loaded_chunks;
    int loaded_chunk_count, loaded_chunks_cap;
    int loaded_chunks_authoritative;
    GmRuntimePendingChunkUnload *pending_chunk_unloads;
    int pending_chunk_unload_count, pending_chunk_unloads_cap;
    int pending_chunk_unload_cursor;
} GmRuntimeDimensionLists;

typedef struct GmRuntime {
    GmWorld *world;
    GmWorld *worlds[3]; /* index dimension+1: Nether, Overworld, End */
    Chunk *window;
    McSinTable sin_table;
    PsvPlayer player;
    /* Integrated-server EntityPlayerMP movement shadow. Client pose/motion
     * advances immediately; survival fields consume the prior client movement
     * packet on the following server tick. */
    PsvPlayer server_player;
    PvStats vitals;
    uint64_t player_uuid_most, player_uuid_least;
    char player_name[17];
    int player_spawn_present;
    int player_spawn_x, player_spawn_y, player_spawn_z;
    int player_spawn_forced;
    int trigger_qrl_present;
    int trigger_qrl_score;
    int trigger_qrl_locked;
    /* The complete vanilla statistics JSON is cold, opaque save state.  Hot
     * counters needed every player tick stay scalar; unknown and achievement
     * values round-trip byte-for-byte instead of being discarded. */
    unsigned char *player_statistics_json;
    size_t player_statistics_len;
    uint64_t player_statistics_fnv64;
    int player_statistics_present;
    long long stat_play_one_minute;
    long long stat_time_since_death;
    /* StatisticsManager values touched by SlotFurnaceOutput. Item ids index
     * the complete 1.11.2 registry; presence distinguishes absent JSON keys
     * from keys whose Java int value is zero. */
    int stat_craft_item[GM_RUNTIME_ITEM_STAT_LIMIT];
    unsigned char stat_craft_item_present[GM_RUNTIME_ITEM_STAT_LIMIT];
    int stat_achievement_build_furnace;
    int stat_achievement_open_inventory;
    int stat_achievement_acquire_iron;
    int stat_achievement_cook_fish;
    int stat_achievement_blaze_rod;
    int stat_achievement_potion;
    unsigned char stat_achievement_build_furnace_present;
    unsigned char stat_achievement_open_inventory_present;
    unsigned char stat_achievement_acquire_iron_present;
    unsigned char stat_achievement_cook_fish_present;
    unsigned char stat_achievement_blaze_rod_present;
    unsigned char stat_achievement_potion_present;
    McGameRules gamerules;
    GmWorldClock clock;
    int world_spawn_x, world_spawn_y, world_spawn_z;
    int default_game_mode;
    int difficulty;
    double border_center_x, border_center_z;
    double border_diameter, border_target_diameter;
    long long border_time_until_target;
    double border_damage_amount, border_damage_buffer;
    int border_warning_time, border_warning_distance;
    GmLiveSim entities;
    GmRuntimeDimensionLists dimension_lists[3];
    GmMobLive mobs;
    GmDragonLive dragon;
    GmRuntimeProjectile *projectiles;
    int projectiles_cap;
    GmRuntimeWither *withers;
    int wither_count, withers_cap;
    GmRuntimeAreaEffectCloud *area_effect_clouds;
    int area_effect_cloud_count, area_effect_clouds_cap;
    GmRuntimeAreaEffectCooldown *area_effect_cooldowns;
    int area_effect_cooldown_count, area_effect_cooldowns_cap;
    /* Exact BlockFalling slice. Dynamic cold storage plus the active count
     * keeps the ordinary no-falling-entity tick path to a single branch. */
    GmRuntimeFallingBlock *falling_blocks;
    int falling_block_count, falling_blocks_cap;
    /* World.playEvent payloads are a transient ordered observation stream.
     * The fixed falling-block pool bounds the currently represented producer
     * to at most one terminal event per active entity and phase. */
    GmRuntimeWorldEvent *world_events;
    int world_event_head;
    int world_event_count, world_events_cap;
    uint64_t world_event_next_seq;
    uint64_t world_event_dropped;
    /* BlockFalling's process-global worldgen switch. Ordinary live runtime
     * leaves this false; controlled population/oracle boundaries may enable
     * the synchronous scan-and-place path explicitly. */
    int falling_instant;
    /* Powered TNT is active-set driven. The ordinary world path pays one
     * count branch; constructor and motion cursors exist only after priming. */
    GmRuntimePrimedTnt *primed_tnt;
    int primed_tnt_count, primed_tnt_cap;
    /* Standalone End crystals are separate from the fixed dragon-arena
     * crystals so saved fixtures can exist in any dimension. */
    GmRuntimeEndCrystal *end_crystals;
    int end_crystal_count, end_crystals_cap;
    uint64_t dragon_respawn_crystal_mask;
    int dragon_respawn_state;
    int dragon_respawn_ticks;
    GmRuntimeLightning *lightning;
    int lightning_count, lightning_cap;
    int last_lightning_bolt;
    GmRuntimeWeatherEvent *weather_events;
    int weather_event_head;
    int weather_event_count, weather_events_cap;
    uint64_t weather_event_next_seq;
    uint64_t weather_event_dropped;
    GmRuntimeFirework *fireworks;
    int firework_count, fireworks_cap;
    GmRuntimeFireworkEvent *firework_events;
    int firework_event_head, firework_event_count, firework_events_cap;
    uint64_t firework_event_next_seq, firework_event_dropped;
    GmRuntimeFireworkTwinkle *firework_twinkles;
    int firework_twinkle_count, firework_twinkles_cap;
    GmRuntimeFishHook fish_hook;
    GmRuntimeFishEvent *fish_events;
    int fish_event_head, fish_event_count, fish_events_cap;
    uint64_t fish_event_next_seq, fish_event_dropped;
    GmRuntimeSmeltEvent *smelt_events;
    int smelt_event_head, smelt_event_count, smelt_events_cap;
    uint64_t smelt_event_next_seq, smelt_event_dropped;
    GmRuntimeBrewEvent *brew_events;
    int brew_event_head, brew_event_count, brew_events_cap;
    uint64_t brew_event_next_seq, brew_event_dropped;
    /* Ordered, allocation-free client sound seam. Simulation producers append
     * resolved sound identity, category, source, volume, and pitch. Playback
     * is an optional interactive consumer and never feeds simulation state. */
    GmRuntimeSoundEvent *sound_events;
    int sound_event_head, sound_event_count, sound_events_cap;
    uint64_t sound_event_next_seq, sound_event_dropped;
    uint64_t sound_random_seed48;
    /* TileEntityDispenser.RNG is process-global in Java and unsaved by NBT.
     * Magma scopes its exact 48-bit cursor to the runtime for independent,
     * deterministic environments; oracle fixtures can inject the cursor. */
    uint64_t dispenser_random_seed48;
    uint64_t world_mob_next_seq;
    uint64_t sound_mob_next_seq;
    uint64_t particle_mob_next_seq;
    /* EntityPlayerSP's client-side Entity.rand cursor. Swimming pitch and
     * splash particles consume it independently of EntityPlayerMP. */
    uint64_t client_player_random_seed48;
    /* Current-tick allocation-free World.spawnParticle calls. The visual
     * consumer drains these after simulation and before ParticleManager tick. */
    GmRuntimeParticleEvent *particle_events;
    int particle_event_count, particle_events_cap;
    /* Rail entities are a fixed active set. With no minecarts the base tick
     * pays one count branch; rail path work scales only with live carts. */
    GmRuntimeMinecart *minecarts;
    int minecart_count, minecarts_cap;
    int minecart_ride_eid;
    float minecart_rider_forward, minecart_rider_yaw;
    GmRuntimeEndGateway *end_gateways;
    int end_gateway_count, end_gateways_cap;
    int end_gateway_order[20];
    int end_gateway_order_count;
    GmRuntimeChunkRef *end_cities;
    int end_city_count, end_cities_cap;
    int end_city_scan_x, end_city_scan_z;
    GmRuntimeChunkRef *mansions;
    int mansion_count, mansions_cap;
    int mansion_scan_x, mansion_scan_z;
    GmRuntimeMansionResident *mansion_residents;
    int mansion_resident_count, mansion_residents_cap;
    GmRuntimeChunkRef *monuments;
    int monument_count, monuments_cap;
    int monument_scan_x, monument_scan_z;
    /* End-city sentries and their guided projectiles are cold growable sets.
     * A world without either pays one count branch in the entity phase. */
    GmRuntimeShulker *shulkers;
    int shulker_count, shulkers_cap;
    GmRuntimeShulkerBullet *shulker_bullets;
    int shulker_bullet_count, shulker_bullets_cap;
    /* Armor stands are cold growable entities. Worlds without one pay one
     * count branch; complete equipment and pose state stays out of the hot
     * GmRuntime allocation. */
    GmRuntimeArmorStand *armor_stands;
    int armor_stand_count, armor_stands_cap;
    /* End decorate() is cold, chunk-discovery work. The dynamic set prevents
     * a populated chunk from consuming RNG or replacing chorus on revisit. */
    GmRuntimeEndPopulationChunk *end_population_chunks;
    int end_population_count, end_population_cap;
    int end_population_scan_x, end_population_scan_z;
    void *end_population_noise;
    /* StructureVillagePieces spawns each resident once while its piece is
     * placed. Retain claimed sites separately from the live slot so death or
     * leaving/re-entering the scan window cannot respawn that resident. */
    GmRuntimeVillageResident *village_residents;
    int village_resident_count, village_residents_cap;
    /* WorldSavedData villages are a cold growable set. State-capsule restore
     * allocates it only when a checkpoint actually contains a village. */
    GmVillageState *village_states;
    int village_state_count, village_states_cap;
    int village_collection_tick;
    GmRuntimeBlockPos *village_position_queue;
    int village_position_count, village_position_queue_cap;
    int village_trade_reset_active;
    int villages_enabled;
    int village_scan_x, village_scan_z;
    /* Shared population-window cursor. Village metadata and initial-animal
     * batches are both products of the same monotonically ordered builds. */
    long village_scan_builds;
    /* WorldServer's transient VillageSiege controller. It is intentionally
     * absent from save data, matching Java's per-WorldServer lifecycle. */
    int village_siege_has_setup;
    int village_siege_state;
    int village_siege_count;
    int village_siege_next_spawn;
    int village_siege_village;
    int village_siege_x, village_siege_y, village_siege_z;
    GmRuntimeIglooResident *igloo_residents;
    int igloo_resident_count, igloo_residents_cap;
    int igloo_scan_x, igloo_scan_z;
    long igloo_scan_builds;
    GmRuntimeSwampWitch *swamp_witches;
    int swamp_witch_count, swamp_witches_cap;
    int swamp_witch_scan_x, swamp_witch_scan_z;
    long swamp_witch_scan_builds;
    /* Moving piston tile entities are active-set driven. Exact slices cover
     * an empty normal-piston head extension and a straight line of up to 12
     * stones; broader reactions/branching and collision rules are admitted
     * by later fixtures. */
    GmRuntimePiston *pistons;
    int piston_count, pistons_cap;
    /* Direct native callbacks may notify the piston base while its Java
     * block event is still being represented synchronously. Suppress only a
     * recursive check of that same base; callbacks for other pistons retain
     * vanilla neighbor order. This transient guard is never serialized. */
    int piston_check_depth;
    int piston_check_x, piston_check_y, piston_check_z;
    /* BlockPistonBase.onBlockAdded queues a block event when a moving source
     * settles back into a powered base. The event executes in the following
     * WorldServer tick before tile entities advance. */
    GmRuntimePistonRecheck *piston_rechecks;
    int piston_recheck_count, piston_rechecks_cap;
    int next_entity_id;
    int player_entity_id;
    /* Internal 48-bit java.util.Random seed (the AtomicLong payload, not the
     * public constructor seed). Random block callbacks consume this exactly. */
    uint64_t world_random_seed48;
    int world_random_have_gaussian;
    double world_random_gaussian;
    /* One Teleporter instance per dimension. Its private Random is seeded
     * from the world seed and advances only when a new Nether portal is
     * constructed, independently of World.rand. */
    uint64_t portal_random_seed48[3];
    /* WorldServer's three default Teleporters retain successful search
     * results by source BlockPos key and evict them on the 100-tick cadence. */
    GmPortalCache portal_caches[3];
    /* java.lang.Math's process-global Random cursor. EntityItem construction
     * consumes this independently of World.rand. */
    uint64_t math_random_seed48;
    /* Server-thread MathHelper UUID stream, isolated by the Java oracle.
     * Every new entity consumes two nextLong calls from this cursor. */
    uint64_t server_uuid_random_seed48;
    /* Malmo SeedHelper's "entity" generator. Each Entity constructor takes
     * one nextLong from this stream to seed its private java.util.Random. */
    uint64_t entity_seed_generator_seed48;
    /* Block.RANDOM is another process-global java.util.Random cursor. It is
     * causal for randomized block drops and is not serialized by world NBT. */
    uint64_t block_random_seed48;
    /* InventoryHelper.RANDOM owns container-break offsets, stack splitting,
     * and Gaussian motion. Its cached Gaussian is part of continuation. */
    uint64_t inventory_helper_random_seed48;
    int inventory_helper_random_have_gaussian;
    double inventory_helper_random_gaussian;
    /* Explosion owns a clock-seeded Random separate from World.rand. Exact
     * replay can supply the next constructor cursor; standalone play uses a
     * deterministic event-local fallback because the JVM clock is not saved. */
    int next_explosion_random_valid;
    uint64_t next_explosion_random_seed48;
    /* Controlled constructor cursor for the next shooter-owned fireball.
     * Vanilla new Random() is clock-seeded, so exact replay supplies the
     * post-UUID cursor; standalone play falls back to a deterministic seed. */
    int next_fireball_random_valid;
    uint64_t next_fireball_random_seed48;
    int next_fireball_random_have_gaussian;
    double next_fireball_random_gaussian;
    /* EntityArrow's private Random drives its three-Gaussian launch spread
     * and later critical-hit and impact cursors. */
    int next_arrow_random_valid;
    uint64_t next_arrow_random_seed48;
    int next_arrow_random_have_gaussian;
    double next_arrow_random_gaussian;
    /* EntityPotion uses the same clock-seeded Entity.rand construction and
     * three-Gaussian throwable heading. Exact fixtures may inject its
     * post-UUID state; standalone play uses a deterministic event seed. */
    int next_potion_random_valid;
    uint64_t next_potion_random_seed48;
    int next_potion_random_have_gaussian;
    double next_potion_random_gaussian;
    /* EntityFallingBlock owns another clock-seeded Entity.rand. Exact replay
     * supplies its post-constructor cursor before the next falling spawn. */
    int next_falling_random_valid;
    uint64_t next_falling_random_seed48;
    /* EntityLightningBolt's post-UUID Entity.rand cursor. */
    int next_lightning_random_valid;
    uint64_t next_lightning_random_seed48;
    int next_firework_random_valid;
    uint64_t next_firework_random_seed48;
    int next_firework_random_have_gaussian;
    double next_firework_random_gaussian;
    int next_firework_audio_random_valid;
    uint64_t next_firework_blast_seed48;
    uint64_t next_firework_twinkle_seed48;
    int next_fishing_random_valid;
    uint64_t next_fishing_random_seed48;
    int next_fishing_random_have_gaussian;
    double next_fishing_random_gaussian;
    /* Forge ItemShears creates this clock-seeded stream only after an
     * eligible target. Exact replay supplies the next raw 48-bit cursor. */
    int next_shears_random_valid;
    uint64_t next_shears_random_seed48;
    int32_t world_update_lcg;
    /* Cold trace-only checkpoints bracketing a controlled input. */
    int controlled_input_valid;
    long long controlled_input_tick;
    int controlled_input_before_valid;
    int controlled_input_before_entity_id;
    uint64_t controlled_input_before_world_seed48;
    uint64_t controlled_input_before_math_seed48;
    uint64_t controlled_input_before_block_seed48;
    uint64_t controlled_input_before_inventory_helper_seed48;
    int controlled_input_before_inventory_helper_have_gaussian;
    double controlled_input_before_inventory_helper_gaussian;
    int32_t controlled_input_before_update_lcg;
    int controlled_input_entity_id;
    uint64_t controlled_input_world_seed48;
    uint64_t controlled_input_math_seed48;
    uint64_t controlled_input_block_seed48;
    uint64_t controlled_input_inventory_helper_seed48;
    int controlled_input_inventory_helper_have_gaussian;
    double controlled_input_inventory_helper_gaussian;
    int32_t controlled_input_update_lcg;
    int bow_ticks,bow_drawing;
    int player_air;        /* Entity AIR data parameter, vanilla default 300 */
    /* EntityLivingBase.prevBlockpos on the authoritative player. Frost Walker
     * runs only when this integer block position changes. */
    int frost_prev_block_valid;
    int frost_prev_block_x, frost_prev_block_y, frost_prev_block_z;
    int player_fire_ticks; /* Entity.fire, setFire(seconds) stores seconds*20 */
    int player_position_update_ticks; /* EntityPlayerSP stationary packet cursor */
    int player_position_packet_pending; /* queued CPacketPlayer.Position */
    int command_position_correction_ticks;
    double command_position_correction_x;
    double command_position_correction_y;
    double command_position_correction_z;
    float command_position_correction_yaw;
    float command_position_correction_pitch;
    int command_position_correction_rotation;
    int command_position_correction_pre_tick;
    int command_game_mode_client_delay;
    double player_last_reported_x, player_last_reported_y;
    double player_last_reported_z;
    float player_last_reported_yaw, player_last_reported_pitch;
    int player_prev_on_ground;
    GmRuntimeMovePacket player_move_packet;
    /* EntityPlayerSP emits this separate packet after controlled pig travel.
     * Payload is the client-predicted vehicle pose; processing owns a distinct
     * authoritative pig body in GmMobLive. */
    GmRuntimePigVehiclePacket pig_vehicle_packet;
    GmRuntimePigVehiclePacket pig_vehicle_packet_deferred;
    uint64_t pig_vehicle_packet_seq;
    GmRuntimePigVehicleCorrection pig_vehicle_last_correction;
    int player_sprint_sent;
    int server_sprinting;
    int server_sprint_pending;
    int server_sprint_pending_value;
    int ccx, ccz;
    int ox, oz;
    /* Feet position (world coords) as of the ENTRY of the last gm_runtime_tick:
     * server pose packets have landed, this tick's own movement has not. That
     * is the position EntityRenderer.updateRenderer samples for the fogColor1
     * light term - see gm_runtime_tick_entry_feet. */
    double te_x, te_y, te_z;
    int te_valid;
    /* physics-window fill memo: refill only on recenter / world switch / block
     * mutation (gm_world_block_gen). The unconditional per-tick refill was 94%
     * of a physics-only tape replay (find_chunk+light_state, perf 2026-07-10). */
    const GmWorld *win_world;
    int win_ccx, win_ccz;
    long long win_gen;
    int dead, deaths, won, credits;
    /* EntityLivingBase.dead / GuiGameOver state. Entity.isDead (`dead`
     * above) remains false until deathTime reaches 20. */
    int player_dying;
    int player_death_time;     /* EntityLivingBase.deathTime */
    int score;                 /* EntityPlayer.getScore (GuiGameOver line) */
    int death_screen_ticks;    /* GuiGameOver.enableButtonsTimer */
    int quit_to_title;         /* Title Screen confirmed / episode end */
    int dimension;
    GmWorldType world_type;       /* WorldInfo terrain type, shared by dimensions */
    int portal_time, portal_cooldown;
    float player_time_in_portal, player_prev_time_in_portal;
    int player_in_portal;       /* EntityPlayerSP.inPortal for its next tick */
    int player_last_portal_pos_valid;
    int player_last_portal_x, player_last_portal_y, player_last_portal_z;
    double player_last_portal_vec_x, player_last_portal_vec_y;
    int player_teleport_direction;
    int ender_pearl_cooldown;
    int chorus_fruit_cooldown;
    long long seed;
    long long tick;
    int weather_enabled;
    int weather_blocks_enabled;
    int view_distance;
    int brewing_enabled;
    int enchanting_enabled;
    /* EntityPlayer's table-specific seed and post-enchant level. A negative
     * level means the ordinary XP-orb total remains the source of truth. */
    int player_xp_seed;
    int player_xp_level;
    float player_xp_frac;
    int player_xp_total;
    GmEnchantingLive enchanting;
    GmAnvilLive anvil;
    GmRuntimeItemName *item_names;
    int item_name_count, item_names_cap;
    int fire_rain_context_valid;
    int fire_rain_x, fire_rain_y, fire_rain_z;
    int fire_rain_can_die;
    int fire_rain_at_east;
    int fire_rain_can_die_west_candidate;
    int fire_humidity_context_valid;
    int fire_humidity_x, fire_humidity_y, fire_humidity_z;
    int do_fire_tick;
    int do_entity_drops;
    int do_mob_loot;
    int mobs_enabled; /* --mobs off skips gm_mobs_tick (tape-replay parity) */
    /* Live/window random block ticks (game/randtick.c). Default ON for interactive
     * play and unit tests; script/tape replay sets 0 so the unseedable oracle
     * world RNG is not approximated here. */
    int randtick_enabled;
    int randtick_radius; /* Chebyshev chunk radius around player for the pass */
    GmRuntimeTickingChunk *ticking_chunks;
    int ticking_chunk_count, ticking_chunks_cap;
    int ticking_chunks_authoritative;
    /* Exact ChunkProviderServer membership at an imported save boundary.
     * This differs from the PlayerChunkMap tick iterator: block callbacks use
     * World.isBlockLoaded and must not page a persisted cold chunk into that
     * answer merely because its Anvil payload exists. */
    GmRuntimeLoadedChunk *loaded_chunks;
    int loaded_chunk_count, loaded_chunks_cap;
    int loaded_chunks_authoritative;
    GmRuntimePendingChunkUnload *pending_chunk_unloads;
    int pending_chunk_unload_count, pending_chunk_unloads_cap;
    int pending_chunk_unload_cursor;
    int *loaded_entity_order;
    int loaded_entity_order_count, loaded_entity_order_cap;
    GmRuntimeLoadedTile *loaded_tile_order;
    int loaded_tile_order_count, loaded_tile_order_cap;
    /* World.tickableTileEntities is a distinct list from
     * loadedTileEntityList.  Java normally builds it as a filtered view of
     * insertion order, but reload and deferred additions can make the two
     * identities independently observable. */
    GmRuntimeLoadedTile *tickable_tile_order;
    int tickable_tile_order_count, tickable_tile_order_cap;
    int mob_griefing;
    int controlled_mobs_enabled;
    int restored_active_mobs_enabled;
    int server_attack_pending;
    int server_note_click_pending;
    int server_note_click_wx, server_note_click_wy, server_note_click_wz;
    float server_distance_walked_modified;
    float server_prev_distance_walked_modified;
    int server_shear_pending;
    int server_shear_eid;
    int server_shear_hand; /* 0 main, 1 offhand */
    int server_feed_animal_pending;
    int server_feed_animal_eid;
    int server_feed_animal_hand; /* 0 main, 1 offhand */
    int server_feed_animal_sneak;
    int server_pig_boost_pending;
    int server_pig_boost_hand; /* 0 main, 1 offhand */
    int horse_jump_power_counter;
    float horse_client_jump_power;
    int server_swing_pending;
    /* CPacketPlayerTryUseItemOnBlock shadow.  The client creates this packet
     * from the use edge during its next update; the integrated server consumes
     * it on the following locked tick, just like server_attack_pending. */
    int server_block_use_pending;
    int server_block_use_wx, server_block_use_wy, server_block_use_wz;
    int server_block_use_item, server_block_use_meta;
    int server_block_use_predicted_item;
    int potion_count;
    GmPotionEffectView potions[GM_MAX_POTION_EFFECTS];
    int haste_amplifier;
    int fatigue_amplifier;
    int resistance_amplifier;
    double player_attack_speed_multiplier;
    float player_luck;
    int container; /* 0 player, 1 workbench, 2 furnace, 3 chest, 4 brewing,
                    * 5 enchanting, 6 anvil, 7 merchant, 8 horse, 9 shulker,
                    * 10 54-slot large chest, 11 beacon, 12 structure,
                    * 13 dispenser/dropper, 14 hopper */
    int container_wx, container_wy, container_wz;
    int container_drag_event, container_drag_mode;
    unsigned char container_drag_slots[GMC_SLOT_COUNT];
    /* Checkpoint shadow of InventoryPlayer.itemStack. Live clicks use the
     * player_ctl cursor; checkpoint write snapshots it here atomically. */
    ICStack container_cursor;
    int active_furnace;
    int active_chest;
    int active_chest_pair; /* canonical second half for container 10, else -1 */
    int active_static_container;
    int active_villager_eid;
    int active_horse_eid;
    int merchant_selected;
    int merchant_offer_index;
    ICStack merchant_slots[3];
    ICStack craft_grid[9]; /* live craft matrix (container_live slot ids 36..44) */
    GmRuntimeFurnace *furnaces;
    int furnaces_cap;
    GmRuntimeChest *chests; /* growable; capacity in chests_cap */
    int chests_cap;
    /* InventoryEnderChest belongs to the player, not to any block tile. Each
     * placed tile owns only its animation/viewer state. */
    ChestLive ender_chest_inventory;
    GmRuntimeEnderChest *ender_chests;
    int ender_chests_cap;
    int active_ender_chest;
    /* Allocated only when a represented static inventory/record tile is
     * restored or created. Comparator queries reach it by exact coordinate. */
    GmRuntimeStaticContainer *static_containers;
    int static_containers_cap;
    GmRuntimeCommandBlock *command_blocks;
    int command_blocks_cap;
    GmRuntimeStructureBlock *structure_blocks;
    int structure_blocks_cap;
    GmRuntimeStructureTemplate *structure_templates;
    int structure_templates_cap;
    GmRuntimeStructureGui structure_gui;
    GmRuntimeFlowerPot *flower_pots;
    int flower_pots_cap;
    GmRuntimeNoteBlock *note_blocks;
    int note_blocks_cap;
    GmRuntimeSkull *skulls;
    int skulls_cap;
    GmRuntimeDecorativeTile *decorative_tiles;
    int decorative_tiles_cap;
    GmRuntimeTaggedItem *tagged_items;
    int tagged_items_cap;
    /* Content-interned complete ItemStack tag compounds. ICStack.tag_id is a
     * stable one-based index into this cold table. Entries are append-only so
     * copied stacks never acquire dangling handles. */
    GmRuntimeStackTag *stack_tags;
    int stack_tag_count, stack_tags_cap;
    GmRuntimeMapData *map_data;
    int map_data_cap;
    int map_next_id;
    GmRuntimeItemFrame *item_frames;
    int item_frames_cap;
    GmRuntimePainting *paintings;
    int paintings_cap;
    int painting_count;
    GmRuntimeLeashKnot *leash_knots;
    int leash_knots_cap;
    int leash_knot_count;
    int leash_knot_pending_count;
    /* Sorted pending block updates. The idle hot path is one count check;
     * insertion/restoration work is cold or active-set driven. */
    GmRuntimeScheduledTick *scheduled_ticks;
    int scheduled_tick_count, scheduled_ticks_cap;
    long long scheduled_tick_next_order;
    /* Vanilla keeps a per-world chronological list of redstone-torch off
     * transitions. It is cold-path, so allocate it only when a torch actually
     * toggles and prune it only from a torch callback. */
    GmRuntimeRedstoneTorchToggle *redstone_torch_toggles;
    int redstone_torch_toggle_count;
    int redstone_torch_toggle_cap;
    /* Comparator output is TileEntity state, not block metadata. This fixed
     * table is touched only by comparator load/edit/query/callback paths. */
    GmRuntimeComparator *comparators;
    int comparator_count, comparators_cap;
    /* Tickable daylight-detector tiles use a fixed active set. The ordinary
     * world path pays one count branch and never scans blocks. */
    GmRuntimeDaylightDetector *daylight_detectors;
    int daylight_detector_count, daylight_detectors_cap;
    GmFluidLive fluids;    /* live water/lava flow region (game/fluid_live.c) */
    /* Tape-replay ghost pushers: recorded oracle entity boxes (world coords,
     * feet y, full width/height) injected per tick; gm_runtime_tick applies
     * the vanilla applyEntityCollision player push from them after the player
     * update and clears the list. Live mob pushes are NOT routed here. */
    struct { double x, y, z, w, h; } ghosts[GM_RUNTIME_GHOSTS];
    int nghosts;
    /* Tape-replay RENDERABLE ghost entities (divergence #10): recorded oracle
     * entities (mapped type id + pose) held for the frame captured after this
     * tick. Render-only - never touches physics/progression; the pusher list
     * above keeps its own separate, physics-verified semantics. The script
     * loop clears this at the top of every tick. */
    GmEntityView ghost_views[GM_RUNTIME_GHOST_VIEWS];
    int nghost_views;
    /* Exact-double EntityBoat pose from the current tape row. Unlike the
     * render ghost above, this drives the recorded local player's riding
     * relationship: mount/dismount packets take effect one client tick after
     * the input, then EntityBoat.updatePassenger pins the player's feet at
     * boat y + getMountedYOffset() + EntityPlayer.getYOffset(), evaluated
     * through EntityBoat's float local, is y - 0.44999998807907104. */
    struct { int valid, ent_id; double x, y, z, yaw; } tape_boat;
    int tape_boat_ride_id;       /* -1 while the recorded player is on foot */
    int tape_boat_mount_pending; /* entity id; activates next client tick */
    int tape_boat_dismount_pending;
    int tape_boat_mount_message_ticks;
    float tape_boat_paddle[2];
    double tape_boat_prev_yaw;
    int tape_boat_prev_yaw_valid;
    /* Tape rows carry a nearby EntityLargeFireball removal but not its
     * SPacketExplosion. Retain enough trajectory to reconstruct the first
     * renderable ParticleExplosionLarge puff on a player-hit removal. */
    struct { int ent_id; float x, y, z, dx, dy, dz; }
        tape_large_fireballs[GM_RUNTIME_FIREBALL_TRACKS];
    int ntape_large_fireballs;
    struct { int active, ent_id, age; float x, y, z; }
        tape_fireball_impacts[GM_RUNTIME_FIREBALL_TRACKS];
    /* Tape-replay open GUI screen (divergence #9): render-only. When set,
     * frame capture draws gm_screen_draw after the HUD using this container
     * kind + ScaledResolution mouse coords. Cleared each tick like ghost_views.
     * Does NOT mutate r->container (physics/close distance stay untouched). */
    int gui_view_active;     /* 1 if a mapped gui_view event landed this tick */
    int gui_view_container;  /* gm_screen_kind_for_gui result, excluding 12 */
    int gui_view_mx, gui_view_my; /* vanilla ScaledResolution mouse coords */
    /* Exact post-tick container render truth. Unlike the live container, these
     * slots/cursor/progress never participate in click or furnace simulation.
     * They are cleared with gui_view at the start of every replay tick. */
    ICStack tape_gui_slots[GMC_SLOT_COUNT];
    unsigned char tape_gui_slot_active[GMC_SLOT_COUNT];
    ICStack tape_gui_cursor;
    int tape_gui_cursor_active;
    int tape_furnace_active;
    int tape_furnace_burn, tape_furnace_current_burn;
    int tape_furnace_cook, tape_furnace_total_cook;
    int tape_brewing_active;
    int tape_brewing_brew, tape_brewing_fuel;
    int tape_merchant_active;
    int tape_merchant_selected, tape_merchant_offer_count;
    int tape_merchant_disabled;
    ICStack tape_merchant_offer[3];
    /* Post-tick oracle inventory used only for this tick's hand/HUD/GUI. The
     * replay separately re-anchors the live inventory before the next tick so
     * current-tick actions still consume their true pre-tick stacks. */
    IsrInv tape_inv;
    int tape_inv_active;
    int tape_xp_active, tape_xp_level;
    float tape_xp_frac;
    int tape_air;
    float tape_portal;
    int tape_portal_frame, tape_portal_phase, tape_loading;
    int tape_texture_animations_pinned;
    int tape_fire, tape_creative, tape_game_mode;
    int tape_hurt_time, tape_max_hurt_time;
    float tape_hurt_yaw, tape_attack_cooldown;
    int tape_potion_count;
    GmPotionEffectView tape_potions[GM_MAX_POTION_EFFECTS];
    /* Recorded ForgeHooks.getTotalArmorValue. -1 = the tape did not carry it
     * (pre-2026-07-29 schema); the view then keeps the item-derived guess. */
    int tape_armor_points;
    /* java.util.Collections' lazily initialized process-global Random. The
     * oracle isolates this cursor for server spawning; append it so legacy
     * native checkpoint bytes remain a valid prefix during migration. */
    uint64_t collections_random_seed48;
} GmRuntime;

int  gm_runtime_init(GmRuntime *r, const GmConfig *cfg, char *err, int err_cap);
void gm_runtime_destroy(GmRuntime *r);
/* Version/ABI-fenced native checkpoint. World columns are written separately
 * through gm_runtime_write_chunk_store_dim; this file owns all represented
 * runtime, entity, tile, queue, RNG, inventory, and clock state. */
int gm_runtime_write_checkpoint(const GmRuntime *r, const char *path);
int gm_runtime_load_checkpoint(GmRuntime *r, const char *path);
int gm_runtime_restore_player_statistics(
    GmRuntime *r, const void *json, size_t len,
    long long play_one_minute, long long time_since_death);
int gm_runtime_write_player_statistics(const GmRuntime *r, const char *path);
/* The only authoritative survival transition used by interactive and harness play. */
void gm_runtime_tick(GmRuntime *r, GmAction action);
/* Cold generated-entity synchronization. Production calls this after a
 * recenter/new population window; tests may call it after gm_world_ensure. */
int gm_runtime_sync_village_residents(GmRuntime *r);
int gm_runtime_village_position_enqueue(GmRuntime *r, int x, int y, int z);
int gm_runtime_village_collection_begin(
    GmRuntime *r, int collection_tick, int village_count);
/* Focused WorldServer village phase for exact state-machine/oracle gates. */
void gm_runtime_tick_village_siege_fixture(GmRuntime *r);
int gm_runtime_village_state_restore(
    GmRuntime *r, int index,
    int num_villagers, int radius, int num_golems,
    int last_add_door_timestamp, int tick_counter, int no_breed_ticks,
    int center_x, int center_y, int center_z,
    int helper_x, int helper_y, int helper_z);
int gm_runtime_village_door_restore(
    GmRuntime *r, int index, int x, int y, int z,
    int inside_dx, int inside_dz, int timestamp);
int gm_runtime_village_reputation_restore(
    GmRuntime *r, int index, uint64_t uuid_most, uint64_t uuid_least,
    int score);
int gm_runtime_player_uuid_restore(
    GmRuntime *r, uint64_t uuid_most, uint64_t uuid_least);
int gm_runtime_player_name_restore(GmRuntime *r, const char *name);
int gm_runtime_player_primary_hand_restore(
    GmRuntime *r, int primary_right);
int gm_runtime_sync_igloo_residents(GmRuntime *r);
int gm_runtime_sync_swamp_witches(GmRuntime *r);
void gm_runtime_view(const GmRuntime *r, GmPlayerView *out);
/* Test-hook pose mutation. It changes travel state only, never progression state. */
void gm_runtime_set_pose(GmRuntime *r, double x, double y, double z,
                         float yaw, float pitch);
void gm_runtime_set_pose_state(GmRuntime *r, double x, double y, double z,
                               float yaw, float pitch, double vx, double vy,
                               double vz, int on_ground, float fall_distance);
void gm_runtime_set_velocity(GmRuntime *r, double x, double y, double z);
void gm_runtime_set_packet_velocity(GmRuntime *r, double x, double y, double z);
void gm_runtime_add_velocity(GmRuntime *r, double x, double y, double z);
/* Cold exact-step fixture state. Effects age in gm_runtime_tick; supported
 * gameplay modifiers are applied from this bounded active list. */
void gm_runtime_potions_clear(GmRuntime *r);
int gm_runtime_potion_add(GmRuntime *r, int id, int amplifier, int duration);
int gm_runtime_potion_add_flags(
        GmRuntime *r, int id, int amplifier, int duration,
        int ambient, int show_particles);
/* EntityPlayerSP.onLivingUpdate's exact float portal/Nausea ramp. The effect
 * duration is the post-PotionEffect.onUpdate value seen by onLivingUpdate. */
float gm_runtime_client_portal_step(
        float current, int in_portal, int nausea_duration);
/* Tape/live equipment bridge for EntityEquipmentSlot.CHEST == Items.ELYTRA. */
void gm_runtime_set_elytra(GmRuntime *r, int equipped);
/* Enable tape-authoritative flag-7 metadata timing and apply an observed
 * player metadata value before the current tick. */
void gm_runtime_set_elytra_flag7(GmRuntime *r, int flying);
/* Absolute camera rotation only (tape replay of recorded mouse look). */
void gm_runtime_set_look(GmRuntime *r, float yaw, float pitch);
/* Tape replay: register a recorded oracle entity box (world coords, feet y,
 * width w, height h) as a ghost pusher for the NEXT gm_runtime_tick. */
void gm_runtime_ent_box(GmRuntime *r, double x, double y, double z,
                        double w, double h);
/* Tape replay: apply EntityDragon.collideWithEntities / attackEntitiesInList
 * damage only when the recorded part query box overlaps the live player. */
int gm_runtime_dragon_contact(GmRuntime *r, double min_x, double min_y,
                              double min_z, double max_x, double max_y,
                              double max_z, float damage);
/* Tape replay: register a recorded oracle entity for RENDERING at this tick's
 * frame capture (type = EW_TYPE_* model id). Render-only; no physics effect.
 * ent_id is the tape entity id (for hurtTime/limbSwing continuity); pass -1
 * if unknown. */
void gm_runtime_ent_view(GmRuntime *r, const GmEntityView *view);
/* Preserve an EntityBoat tape row at JSON double precision for passenger
 * position following. Call before gm_runtime_ent_view for the same entity. */
void gm_runtime_tape_boat_view(GmRuntime *r, int ent_id, double x, double y,
                               double z, double yaw);
void gm_runtime_ent_views_clear(GmRuntime *r);
/* Fill `out` with this tick's renderable ghost entities; returns count. */
int gm_runtime_ghost_views(const GmRuntime *r, GmEntityView *out, int max);
/* Tape replay: register an open container GUI for this tick's frame capture
 * (container 0..7; mx/my = vanilla ScaledResolution coords). Render-only. */
void gm_runtime_gui_view(GmRuntime *r, int container, int mx, int my);
void gm_runtime_gui_view_clear(GmRuntime *r);
/* Returns 1 if a gui_view is active this tick; writes container/mx/my. */
int gm_runtime_gui_view_get(const GmRuntime *r, int *container, int *mx, int *my);
int gm_runtime_tape_gui_slot(GmRuntime *r, int slot, int item, int count, int meta);
int gm_runtime_tape_gui_cursor(GmRuntime *r, int item, int count, int meta);
/* Same as above but retain StoredEnchantments subset (optional tape extension). */
int gm_runtime_tape_gui_slot_stack(GmRuntime *r, int slot, ICStack stack);
int gm_runtime_tape_gui_cursor_stack(GmRuntime *r, ICStack stack);
int gm_runtime_tape_gui_slot_get(const GmRuntime *r, int slot, ICStack *out);
int gm_runtime_tape_gui_cursor_get(const GmRuntime *r, ICStack *out);
int gm_runtime_tape_furnace(GmRuntime *r, int burn, int current_burn,
                            int cook, int total_cook);
int gm_runtime_tape_brewing(GmRuntime *r, int brew, int fuel);
int gm_runtime_tape_merchant(
    GmRuntime *r, int selected, int offer_count, int disabled,
    ICStack buy_a, ICStack buy_b, ICStack sell);
/* Render-only post-tick tape state. Inventory persists until the next delta. */
int gm_runtime_tape_inventory(GmRuntime *r, int slot, int item, int count, int meta);
void gm_runtime_tape_player_view(GmRuntime *r, int xp_level, float xp_frac, int air,
                                 float portal, int portal_frame, int portal_phase,
                                 int loading, int texture_animations_pinned,
                                 int fire, int creative, int hurt_time,
                                 int max_hurt_time, float hurt_yaw,
                                 float attack_cooldown);
void gm_runtime_tape_potions_clear(GmRuntime *r);
int gm_runtime_tape_potion(GmRuntime *r, int id, int amplifier, int duration,
                           int show_particles);
/* points < 0 clears the override (fall back to the item-derived value). */
void gm_runtime_tape_armor(GmRuntime *r, int points);
void gm_runtime_apply_tape_view(const GmRuntime *r, GmPlayerView *view);

/* Feet position at the entry of the last gm_runtime_tick, in world coords.
 * EntityRenderer.updateRenderer runs before the local player's movement update
 * within Minecraft.runTick, so its fogColor1 light sample sees the pre-move
 * position - but AFTER the network phase, so a server pose packet (the tape's
 * pre-tick set_pose) is already applied. Falls back to the live view before
 * the first tick. */
void gm_runtime_tick_entry_feet(const GmRuntime *r,
                                double *x, double *y, double *z);
/* Seed recorded vitals at tape-replay start. */
void gm_runtime_set_vitals(GmRuntime *r, float health, int food);
/* State-capsule setup hooks. These mutate only cold pre-tick state and add no
 * work to the simulation loop. */
void gm_runtime_set_food_stats(GmRuntime *r, float saturation, float exhaustion);
int gm_runtime_set_food_timer(GmRuntime *r, int food_timer);
int gm_runtime_set_player_xp(
    GmRuntime *r, int level, float fraction, int total);
int gm_runtime_set_player_combat(
    GmRuntime *r, int attack_ticks, int hurt_time,
    int hurt_resistant_time, int death_time, int dead, int deaths);
int gm_runtime_set_player_absorption(GmRuntime *r, float absorption);
int gm_runtime_set_selected_slot(GmRuntime *r, int slot);
int gm_runtime_set_air(GmRuntime *r, int air);
int gm_runtime_set_fire(GmRuntime *r, int fire_ticks);
int gm_runtime_set_do_fire_tick(GmRuntime *r, int enabled);
int gm_runtime_set_do_entity_drops(GmRuntime *r, int enabled);
int gm_runtime_set_do_mob_loot(GmRuntime *r, int enabled);
int gm_runtime_set_falling_instant(GmRuntime *r, int enabled);
int gm_runtime_set_position_update_ticks(GmRuntime *r, int ticks, int pending);
int gm_runtime_spawn_xp_fixture(
    GmRuntime *r, double x, double y, double z,
    double vx, double vy, double vz, int value, int eid,
    int age, int pickup_delay, int color, int target_color);
int gm_runtime_restore_xp_orb_box(
    GmRuntime *r, int eid, double min_x, double min_y, double min_z,
    double max_x, double max_y, double max_z);
int gm_runtime_spawn_item_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, int item, int count, int meta,
    int age, int pickup_delay, int controlled_stationary);
int gm_runtime_spawn_item_state_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, float yaw, float hover_start,
    int item, int count, int meta, int age, int pickup_delay,
    int health, int lifespan, int on_ground, int no_gravity,
    int ticks_existed, int fire, int in_water, int first_update,
    unsigned long long entity_seed48);
int gm_runtime_set_entity_item_stack_tag(
    GmRuntime *r, int eid, int tag_id);
int gm_runtime_spawn_falling_fixture(
    GmRuntime *r, int eid, int block, int meta, int fall_time,
    double x, double y, double z, double vx, double vy, double vz,
    int no_gravity, int no_ground);
int gm_runtime_set_falling_origin(
    GmRuntime *r, int eid, int x, int y, int z);
/* Cold oracle hook: advance only the same falling-entity phase used by the
 * public tick. This permits an immediate EntityFallingBlock.onUpdate boundary
 * without also aging later controlled living entities. */
void gm_runtime_tick_falling_fixture_phase(GmRuntime *r);
int gm_runtime_spawn_arrow_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, int controlled_stationary,
    int fire_ticks);
int gm_runtime_spawn_player_arrow_state_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, float yaw, float pitch,
    int ticks_in_air, int fire_ticks, double damage, int knockback,
    int critical, int pickup_status, int in_ground, int shake,
    int ticks_in_ground, int tile_x, int tile_y, int tile_z,
    int tile_block, int tile_meta, uint64_t random_seed48,
    int random_have_gaussian, double random_next_gaussian);
int gm_runtime_set_arrow_payload(
    GmRuntime *r, int eid, int arrow_kind, int potion_type,
    int spectral_duration, int color, int custom_color,
    const PtMobEffect *effects, int effect_count,
    const unsigned char *effect_flags,
    int pickup_item, int pickup_meta, int pickup_tag_id);
int gm_runtime_set_arrow_time_in_ground(
    GmRuntime *r, int eid, int time_in_ground);
int gm_runtime_player_arrow_pickup_now(
    GmRuntime *r, int projectile_slot);
int gm_runtime_spawn_primed_tnt_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, int fuse);
int gm_runtime_set_transient_entity_uuid(
    GmRuntime *r, int eid, int64_t most, int64_t least);
int gm_runtime_spawn_end_crystal_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    int inner_rotation, int show_bottom, int has_beam,
    int beam_x, int beam_y, int beam_z);
int gm_runtime_place_end_crystal(
    GmRuntime *r, int support_x, int support_y, int support_z,
    int consume_slot);
/* Narrow test phase for DragonFightManager's pre-entity world tick. */
void gm_runtime_tick_dragon_respawn_fixture(GmRuntime *r);
int gm_runtime_spawn_small_fireball_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, double ax, double ay, double az);
int gm_runtime_spawn_wither_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, float yaw, float pitch,
    float render_yaw_offset, float health, int invul_time,
    int ticks_existed, int hurt_time, int death_time,
    int hurt_resistant_time, int block_break_counter,
    uint64_t random_seed48, int random_have_gaussian,
    double random_gaussian);
int gm_runtime_set_wither_uuid(
    GmRuntime *r, int eid, long long most, long long least);
int gm_runtime_restore_wither_base_state(
    GmRuntime *r, int eid, int no_ai, int no_gravity,
    int air, int fire, int on_ground, float fall_distance, int in_water,
    int living_sound_time, float last_damage, int recently_hit,
    int attacking_player);
int gm_runtime_set_wither_head_state(
    GmRuntime *r, int eid, int head, int target_eid, int target_is_player,
    int next_update, int idle_updates,
    float yaw, float pitch, float prev_yaw, float prev_pitch);
int gm_runtime_restore_wither_ai_state(
    GmRuntime *r, int eid, int target_eid, int target_is_player,
    int revenge_eid, int revenge_is_player, int revenge_timer,
    int hurt_task_active, int hurt_target_eid, int hurt_target_is_player,
    int hurt_revenge_timer_old, int hurt_target_unseen_ticks,
    int nearest_task_active,
    int target_task_tick, int goal_task_tick, int invul_task_active,
    int ranged_task_active,
    int ranged_attack_time, int ranged_see_time);
int gm_runtime_restore_wither_rotation_state(
    GmRuntime *r, int eid,
    float rotation_yaw_head, float prev_rotation_yaw_head,
    float prev_render_yaw_offset,
    int body_rotation_tick_counter, float body_prev_render_yaw_head);
int gm_runtime_wither_damage_fixture(
    GmRuntime *r, int eid, float amount, int source_kind);
int gm_runtime_wither_count(const GmRuntime *r);
int gm_runtime_wither_get(
    const GmRuntime *r, int index, GmRuntimeWither *out);
int gm_runtime_spawn_wither_skull_fixture(
    GmRuntime *r, int eid, int shooter_eid,
    double x, double y, double z, double vx, double vy, double vz,
    double ax, double ay, double az, float yaw, float pitch,
    int invulnerable, int ticks_in_air, int life);
int gm_runtime_spawn_potion_fixture(
    GmRuntime *r, int eid, int potion_item, int potion_type,
    double x, double y, double z, double vx, double vy, double vz,
    int age);
int gm_runtime_apply_player_instant_potion_indirect(
    GmRuntime *r, int potion_id, int amplifier, double factor,
    const GmMobPotionDamageOwner *owner);
int gm_runtime_spawn_potion_state_fixture(
    GmRuntime *r, int eid, int potion_item, int potion_type,
    double x, double y, double z, double vx, double vy, double vz,
    int age, int player_thrower, int ignore_player,
    int ignore_player_time);
int gm_runtime_set_potion_payload(
    GmRuntime *r, int eid, int color, int custom_color,
    const PtMobEffect *effects, int effect_count,
    const unsigned char *effect_flags, int tag_id);
int gm_runtime_spawn_throwable_state_fixture(
    GmRuntime *r, int eid, int type, int potion_item, int potion_type,
    double x, double y, double z, double vx, double vy, double vz,
    float yaw, float pitch, float prev_yaw, float prev_pitch,
    int age, int ticks_in_air, int player_thrower,
    int thrower_player_pending, int ignore_player,
    int ignore_player_time, int pearl_private_thrower,
    int throwable_shake, int throwable_in_ground,
    int throwable_ticks_in_ground,
    int throwable_tile_x, int throwable_tile_y, int throwable_tile_z,
    int throwable_tile_block, int portal_counter, int in_portal,
    int portal_cooldown,
    int last_portal_pos_valid,
    int last_portal_x, int last_portal_y, int last_portal_z,
    double last_portal_vec_x, double last_portal_vec_y,
    int teleport_direction,
    int client_random_valid, uint64_t client_random_seed48,
    uint64_t random_seed48, int random_have_gaussian,
    double random_next_gaussian);
int gm_runtime_egg_client_status_now(
    GmRuntime *r, int projectile_slot, uint64_t client_seed48);
int gm_runtime_spawn_llama_spit_fixture(
    GmRuntime *r, int eid, int owner_eid,
    int owner_uuid_present, long long owner_uuid_most,
    long long owner_uuid_least,
    double x, double y, double z, double vx, double vy, double vz,
    float yaw, float pitch, int ticks_existed, int no_gravity);
int gm_runtime_spawn_area_effect_cloud_fixture(
    GmRuntime *r, int eid, int potion_type, double x, double y, double z,
    int age, int duration, int wait_time, int reapplication_delay,
    float radius, float radius_on_use, float radius_per_tick,
    int next_application);
int gm_runtime_spawn_area_effect_cloud_state_fixture(
    GmRuntime *r, int eid, int potion_type, double x, double y, double z,
    int age, int duration, int wait_time, int reapplication_delay,
    float radius, float radius_on_use, float radius_per_tick,
    int next_application, int player_owner);
int gm_runtime_set_area_effect_cloud_payload(
    GmRuntime *r, int eid, int color, int custom_color,
    const PtMobEffect *effects, int effect_count,
    const unsigned char *effect_flags);
int gm_runtime_set_area_effect_cloud_extended_state(
    GmRuntime *r, int eid, int duration_on_use, int ignore_radius,
    int particle, int particle_param1, int particle_param2);
int gm_runtime_set_area_effect_cloud_kinematics(
    GmRuntime *r, int eid, double vx, double vy, double vz,
    float yaw, float pitch, float prev_yaw, float prev_pitch);
int gm_runtime_set_area_effect_cloud_deadline(
    GmRuntime *r, int cloud_eid, int target_eid, int deadline);
int gm_runtime_area_effect_cloud_deadline(
    const GmRuntime *r, int cloud_eid, int target_eid);
int gm_runtime_set_area_effect_cloud_identity(
    GmRuntime *r, int cloud_eid, long long uuid_most, long long uuid_least,
    int owner_present, int owner_eid,
    long long owner_uuid_most, long long owner_uuid_least);
int gm_runtime_set_area_effect_cloud_random_seed48(
    GmRuntime *r, int eid, uint64_t seed48);
int gm_runtime_set_area_effect_cloud_common_state(
    GmRuntime *r, int eid,
    int dimension, int air, int fire_ticks, int portal_cooldown,
    int on_ground, int no_gravity, int invulnerable, int silent,
    int glowing, int update_blocked, int in_water, int first_update,
    float fall_distance,
    double prev_x, double prev_y, double prev_z,
    double last_tick_x, double last_tick_y, double last_tick_z,
    uint64_t server_random_seed48, int server_random_have_gaussian,
    double server_random_gaussian);
int gm_runtime_spawn_mob_fixture(
    GmRuntime *r, int type, int eid, double x, double y, double z,
    double vx, double vy, double vz, float yaw, float health, int no_ai,
    int hurt_time, int death_time, int hurt_resistant_time);
int gm_runtime_spawn_horse_fixture(
    GmRuntime *r, int type, int eid,
    double x, double y, double z, double vx, double vy, double vz,
    float yaw, float health, int no_ai,
    double max_health, double movement_speed, double jump_strength,
    int growing_age, int status, int temper, int variant,
    int armor, int chested, int trap, int trap_time,
    int hurt_time, int death_time, int hurt_resistant_time);
int gm_runtime_spawn_llama_fixture(
    GmRuntime *r, int eid,
    double x, double y, double z, double vx, double vy, double vz,
    float yaw, float health, int no_ai,
    double max_health, double movement_speed, double jump_strength,
    int growing_age, int status, int temper, int variant,
    int strength, int decor, int chested, int did_spit, int leashed,
    int hurt_time, int death_time, int hurt_resistant_time);
int gm_runtime_restore_llama_links(
    GmRuntime *r, int eid, int leash_holder_kind, int leash_holder_eid,
    int caravan_head_eid, int caravan_tail_eid,
    double caravan_speed, int caravan_dist_counter);
int gm_runtime_restore_horse_lifecycle(
    GmRuntime *r, int eid, int in_love, int forced_age,
    int forced_age_timer, int eating_counter, int open_mouth_counter,
    int jump_rearing_counter, int tail_counter, int sprint_counter,
    int gallop_time, int horse_jumping, int allow_stand_sliding,
    float jump_power, float head_lean, float prev_head_lean,
    float rearing_amount, float prev_rearing_amount,
    float mouth_openness, float prev_mouth_openness,
    float prev_limb_amount, float limb_amount, float limb_swing);
int gm_runtime_restore_horse_owner(
    GmRuntime *r, int eid, int present,
    uint64_t uuid_most, uint64_t uuid_least);
int gm_runtime_set_horse_inventory(
    GmRuntime *r, int eid, int inventory_slot, ICStack stack);
int gm_runtime_spawn_armor_stand_fixture(
    GmRuntime *r, int eid,
    double x, double y, double z, double vx, double vy, double vz,
    float yaw, float pitch, float health,
    int on_ground, int no_gravity, int invisible,
    int status, int disabled_slots, int ticks_existed, int fire_ticks,
    long long punch_cooldown);
int gm_runtime_armor_stand_set_uuid(
    GmRuntime *r, int eid, int64_t most, int64_t least);
int gm_runtime_armor_stand_set_random_state(
    GmRuntime *r, int eid, uint64_t seed48,
    int have_next_gaussian, double next_gaussian);
int gm_runtime_armor_stand_set_living_state(
    GmRuntime *r, int eid, int air, int in_water, float fall_distance,
    int hurt_time, int death_time, int hurt_resistant_time,
    float last_damage);
int gm_runtime_armor_stand_set_generic_state(
    GmRuntime *r, int eid, float absorption, float max_health,
    float max_health_base,
    int revenge_timer, int portal_cooldown, int custom_name_visible,
    int silent, int glowing, int invulnerable, int update_blocked,
    int fall_flying, int vehicle_eid);
int gm_runtime_armor_stand_set_custom_name(
    GmRuntime *r, int eid, const char *name);
int gm_runtime_armor_stand_add_tag(
    GmRuntime *r, int eid, const char *tag);
int gm_runtime_armor_stand_add_effect(
    GmRuntime *r, int eid, int id, int amplifier, int duration,
    int ambient, int show_particles);
/* Apply the authoritative server-side consequence of a completed ItemFood
 * use. Public for the direct Java/native registry oracle. Nutrition and stack
 * transformation are performed by player_ctl at the 32-tick boundary. */
void gm_runtime_apply_finished_food(GmRuntime *r, const ICStack *stack);
int gm_runtime_armor_stand_string(
    const GmRuntime *r, int id, const unsigned char **data, size_t *length);
int gm_runtime_armor_stand_set_equipment(
    GmRuntime *r, int eid, int slot, ICStack stack);
int gm_runtime_armor_stand_set_pose(
    GmRuntime *r, int eid, int part, float x, float y, float z);
int gm_runtime_place_armor_stand(
    GmRuntime *r, int x, int y, int z, float player_yaw,
    int hand_slot, int creative);
int gm_runtime_armor_stand_interact(
    GmRuntime *r, int eid, double local_y, int hand_slot, int creative);
int gm_runtime_armor_stand_damage_fixture(
    GmRuntime *r, int eid, int source, float amount,
    int allow_edit, int creative);
int gm_runtime_armor_stand_get(
    const GmRuntime *r, int index, GmRuntimeArmorStand *out);
int gm_runtime_armor_stand_views(
    const GmRuntime *r, GmEntityView *out, int max);
void gm_runtime_tick_armor_stands(GmRuntime *r);
int gm_runtime_spawn_sized_mob_fixture(
    GmRuntime *r, int type, int eid, double x, double y, double z,
    double vx, double vy, double vz, float yaw, float health, int size,
    int no_ai, int hurt_time, int death_time, int hurt_resistant_time);
int gm_runtime_restore_no_ai_mob_state(
    GmRuntime *r, int eid, int air, int fire_ticks, int on_ground,
    float fall_distance, int in_water, int ticks_existed,
    int living_sound_time, float last_damage, uint64_t seed48,
    int have_next_gaussian, double next_gaussian);
int gm_runtime_restore_bat_state(GmRuntime *r, int eid, int hanging);
int gm_runtime_set_bat_ai_state(
    GmRuntime *r, int eid, int hanging, int spawn_position_valid,
    int spawn_x, int spawn_y, int spawn_z,
    float head_yaw, float render_yaw_offset,
    int body_rotation_tick_counter, float body_prev_head_yaw,
    int entity_age, int persistence_required);
int gm_runtime_restore_snowman_state(GmRuntime *r, int eid, int pumpkin);
int gm_runtime_restore_endermite_state(
    GmRuntime *r, int eid, int lifetime, int player_spawned,
    int persistence_required);
int gm_runtime_restore_squid_state(
    GmRuntime *r, int eid, float squid_pitch, float prev_squid_pitch,
    float squid_yaw, float prev_squid_yaw, float squid_rotation,
    float prev_squid_rotation, float tentacle_angle,
    float last_tentacle_angle, float random_motion_speed,
    float rotation_velocity, float rotate_speed, float random_motion_x,
    float random_motion_y, float random_motion_z,
    float render_yaw_offset, float head_yaw,
    int body_rotation_tick_counter, float body_prev_head_yaw);
int gm_runtime_set_squid_ai_state(
    GmRuntime *r, int eid, int entity_age, int persistence_required);
int gm_runtime_restore_no_ai_mob_box(
    GmRuntime *r, int eid, double min_x, double min_y, double min_z,
    double max_x, double max_y, double max_z);
int gm_runtime_restore_mob_effect(
    GmRuntime *r, int eid, int id, int amplifier, int duration,
    int ambient, int show_particles);
int gm_runtime_restore_mob_health_absorption(
    GmRuntime *r, int eid, float health, float absorption);
int gm_runtime_restore_iron_golem_state(
    GmRuntime *r, int eid, int player_created, int home_timer,
    int attack_timer, int rose_timer);
int gm_runtime_spawn_villager_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, float yaw, float health,
    int hurt_time, int death_time, int hurt_resistant_time,
    int profession, int living_sound_time,
    uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
int gm_runtime_set_mob_uuid(
    GmRuntime *r, int eid, int64_t uuid_most, int64_t uuid_least);
int gm_runtime_set_mob_fire_ticks(
    GmRuntime *r, int eid, int fire_ticks);
int gm_runtime_set_mob_air(GmRuntime *r, int eid, int air);
int gm_runtime_set_mob_no_ai(GmRuntime *r, int eid, int no_ai);
int gm_runtime_set_sheep_state(
    GmRuntime *r, int eid, int fleece_color, int sheared);
int gm_runtime_set_mob_growing_age(
    GmRuntime *r, int eid, int growing_age);
int gm_runtime_set_mob_recent_hit_state(
    GmRuntime *r, int eid, int recently_hit, int attacking_player);
int gm_runtime_spawn_boat_fixture(
    GmRuntime *r, int eid, double x, double y, double z, float yaw);
int gm_runtime_set_entity_id_cursor(GmRuntime *r, int next_entity_id);
int gm_runtime_set_player_entity_id(GmRuntime *r, int player_entity_id);
int gm_runtime_set_world_random_seed48(GmRuntime *r, uint64_t seed48);
int gm_runtime_set_world_random_gaussian(
    GmRuntime *r, int have_next_gaussian, double next_gaussian);
int gm_runtime_set_math_random_seed48(GmRuntime *r, uint64_t seed48);
int gm_runtime_set_collections_random_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_set_server_uuid_random_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_set_entity_seed_generator_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_set_player_random_seed48(GmRuntime *r, uint64_t seed48);
int gm_runtime_set_client_player_random_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_set_next_explosion_random_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_set_next_fireball_random_state(
    GmRuntime *r, uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
int gm_runtime_set_next_arrow_random_state(
    GmRuntime *r, uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
/* Testable ItemBow.onPlayerStoppedUsing boundary. The playable input path
 * calls the same body after the held-use release edge. */
int gm_runtime_release_bow_now(GmRuntime *r, int draw_ticks);
int gm_runtime_throw_player_item_now(GmRuntime *r, int item, int meta);
/* Direct EntityArrow.onHit seam used by the real-Java comparator. */
int gm_runtime_player_arrow_hit_now(
    GmRuntime *r, int projectile_slot, int mob_slot);
int gm_runtime_player_arrow_block_hit_now(
    GmRuntime *r, int projectile_slot,
    int block_x, int block_y, int block_z,
    double hit_x, double hit_y, double hit_z);
/* Direct EntityExpBottle.onImpact seam. Returns the exact number of spawned
 * split orbs, or zero without changing state when the fixed pools cannot fit
 * the complete Java boundary. */
int gm_runtime_xp_bottle_impact_now(
    GmRuntime *r, int projectile_slot);
/* Direct EntityEgg.onImpact seam. Returns the exact number of spawned chicks
 * (0, 1, or 4), or -1 for an invalid/unrepresentable boundary. */
int gm_runtime_egg_impact_now(
    GmRuntime *r, int projectile_slot);
/* Direct ordinary EntityEnderPearl.onImpact seam: portal particles,
 * Endermite roll/spawn, player teleport, and FALL damage. */
int gm_runtime_ender_pearl_impact_now(
    GmRuntime *r, int projectile_slot);
int gm_runtime_ender_pearl_gateway_impact_now(
    GmRuntime *r, int projectile_slot,
    int block_x, int block_y, int block_z);
/* Exact EntityEnderEye.moveTowards + onUpdate seam. The target arguments are
 * the BlockPos returned by MapGenStronghold, before its 12-block clamp. */
int gm_runtime_spawn_ender_eye_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    int target_x, int target_y, int target_z, uint64_t random_seed48);
int gm_runtime_tick_ender_eye_now(GmRuntime *r, int projectile_slot);
int gm_runtime_tick_projectile_now(GmRuntime *r, int projectile_slot);
/* Focused real-path seam for a player-owned egg/snowball returning after the
 * EntityThrowable two-tick thrower exclusion. */
int gm_runtime_player_throwable_self_hit_now(GmRuntime *r);
double gm_runtime_mathhelper_atan2(double y, double x);
/* DamageSource.FLY_INTO_WALL attack boundary. Armor is bypassed while generic
 * Protection, Resistance, absorption, and hurt resistance remain active. */
int gm_runtime_elytra_wall_damage_now(GmRuntime *r, float amount);
int gm_runtime_set_next_potion_random_state(
    GmRuntime *r, uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
int gm_runtime_set_next_falling_random_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_set_next_shears_random_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_set_block_random_seed48(GmRuntime *r, uint64_t seed48);
int gm_runtime_set_inventory_helper_random_state(
    GmRuntime *r, uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
/* Focused oracle seam for InventoryHelper.spawnItemStack. */
int gm_runtime_inventory_helper_drop_fixture(
    GmRuntime *r, int x, int y, int z, ICStack stack);
int gm_runtime_set_dispenser_random_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_set_world_update_lcg(GmRuntime *r, int32_t update_lcg);
void gm_runtime_capture_controlled_input(GmRuntime *r);
void gm_runtime_begin_controlled_input(GmRuntime *r);
int gm_runtime_random_tick_block(
    GmRuntime *r, int x, int y, int z, int expected_block);
int gm_runtime_frosted_ice_light(
    const GmRuntime *r, int x, int y, int z);
/* Exact EnchantmentFrostWalker.freezeNearby boundary. Returns the number of
 * water sources replaced and advances EntityPlayerMP.rand for each schedule. */
int gm_runtime_frost_walker_freeze(
    GmRuntime *r, double x, double y, double z, int on_ground, int level);
int gm_runtime_random_tick_selection(
    GmRuntime *r, int x, int y, int z, int expected_block,
    int lcg_advances_before);
int gm_runtime_ticking_chunks_begin(GmRuntime *r, int count);
int gm_runtime_ticking_chunk_set(
    GmRuntime *r, int order, int chunk_x, int chunk_z,
    unsigned random_tick_mask);
int gm_runtime_ticking_chunks_finalize(GmRuntime *r);
int gm_runtime_loaded_chunks_begin(GmRuntime *r, int count);
int gm_runtime_loaded_chunk_set(
    GmRuntime *r, int index, int chunk_x, int chunk_z);
int gm_runtime_loaded_chunks_finalize(GmRuntime *r);
int gm_runtime_pending_chunk_unloads_begin(GmRuntime *r, int count);
int gm_runtime_pending_chunk_unload_set(
    GmRuntime *r, int index, int chunk_x, int chunk_z, int eligible);
int gm_runtime_pending_chunk_unloads_finalize(GmRuntime *r);
int gm_runtime_restore_loaded_entity_order(
    GmRuntime *r, int order, int eid);
int gm_runtime_loaded_entity_order_get(
    const GmRuntime *r, int order, int *eid);
int gm_runtime_set_item_entity_uuid(
    GmRuntime *r, int eid, long long most, long long least);
int gm_runtime_restore_loaded_tile_order(
    GmRuntime *r, int order, int x, int y, int z);
int gm_runtime_loaded_tile_order_get(
    const GmRuntime *r, int order, GmRuntimeLoadedTile *tile);
int gm_runtime_loaded_tile_rank(
    const GmRuntime *r, int x, int y, int z);
int gm_runtime_restore_tickable_tile_order(
    GmRuntime *r, int order, int x, int y, int z);
int gm_runtime_tickable_tile_order_get(
    const GmRuntime *r, int order, GmRuntimeLoadedTile *tile);
int gm_runtime_tickable_tile_rank(
    const GmRuntime *r, int x, int y, int z);
/* Exact pending-update subset currently accepted by the capsule. Absolute
 * time/order are captured from NextTickListEntry. */
int gm_runtime_schedule_tick(
    GmRuntime *r, int x, int y, int z, int block, long long time,
    int priority, long long order);
/* Restore an opaque persisted callback from an inactive dimension. It is
 * retained exactly across dimension switches/checkpoints; callback execution
 * is still governed by the ordinary supported-block path once active. */
int gm_runtime_restore_scheduled_tick(
    GmRuntime *r, int dimension, int x, int y, int z, int block,
    long long time, int priority, long long order);
int gm_runtime_scheduled_tick_count(const GmRuntime *r);
int gm_runtime_scheduled_tick_get(
    const GmRuntime *r, int index, GmRuntimeScheduledTick *out);
int gm_runtime_moving_piston_load(
    GmRuntime *r, int dimension, int x, int y, int z,
    int moved_block, int moved_meta, int facing,
    int extending, int source, float progress, float last_progress);
int gm_runtime_moving_piston_count(const GmRuntime *r);
int gm_runtime_moving_piston_get(
    const GmRuntime *r, int index, GmRuntimePiston *out);
int gm_runtime_comparator_count(const GmRuntime *r);
int gm_runtime_comparator_get(
    const GmRuntime *r, int index, GmRuntimeComparator *out);
int gm_runtime_comparator_set_output(
    GmRuntime *r, int dimension, int x, int y, int z, int output_signal);
int gm_runtime_chest_set_slot(
    GmRuntime *r, int dimension, int x, int y, int z,
    int slot, int item, int count, int meta);
int gm_runtime_chest_set_transient(
    GmRuntime *r, int dimension, int x, int y, int z,
    int num_players_using, uint32_t lid_angle_bits,
    uint32_t prev_lid_angle_bits, int ticks_since_sync);
int gm_runtime_container_set_stack_tag(
    GmRuntime *r, int dimension, int x, int y, int z, int slot,
    const void *tag_nbt, size_t tag_nbt_len);
int gm_runtime_chest_count(const GmRuntime *r);
int gm_runtime_chest_get(
    const GmRuntime *r, int index, GmRuntimeChest *out);
int gm_runtime_ender_chest_set_slot(
    GmRuntime *r, int slot, ICStack stack);
ICStack gm_runtime_ender_chest_get_slot(
    const GmRuntime *r, int slot);
int gm_runtime_ender_chest_tile_get(
    const GmRuntime *r, int dimension, int x, int y, int z,
    GmRuntimeEnderChest *out);
int gm_runtime_ender_chest_random_display_tick(
    GmRuntime *r, int x, int y, int z, uint64_t *random_seed48);
int gm_runtime_furnace_set_slot(
    GmRuntime *r, int dimension, int x, int y, int z,
    int slot, int item, int count, int meta,
    int burn_time, int current_burn_time,
        int cook_time, int total_cook_time);
int gm_runtime_furnace_set_custom_name(
    GmRuntime *r, int dimension, int x, int y, int z,
    const char *custom_name);
int gm_runtime_brewing_set_slot(
    GmRuntime *r, int dimension, int x, int y, int z,
    int slot, int item, int count, int meta,
    int brew_time, int fuel);
int gm_runtime_brewing_set_ingredient(
    GmRuntime *r, int dimension, int x, int y, int z, int ingredient_id);
int gm_runtime_beacon_set_state(
    GmRuntime *r, int dimension, int x, int y, int z,
    int levels, int primary, int secondary, int complete);
int gm_runtime_beacon_get(
    const GmRuntime *r, int dimension, int x, int y, int z,
    GmRuntimeStaticContainer *out);
int gm_runtime_beacon_update(
    GmRuntime *r, int dimension, int x, int y, int z);
/* TileEntityBeacon.shouldBeamRender, called by the TESR once per rendered
 * frame. Mutates only the client render counter/scale fields. */
float gm_runtime_beacon_should_render(
    GmRuntimeStaticContainer *beacon, long long total_world_time);
int gm_runtime_beacon_confirm(GmRuntime *r, int primary, int secondary);
void gm_runtime_close_open_container(GmRuntime *r);
int gm_runtime_beacon_valid_effect(int effect);
int gm_runtime_beacon_payment_item(int item);
void gm_runtime_brewing_changed(GmRuntime *r);
void gm_runtime_shulker_box_changed(GmRuntime *r);
void gm_runtime_static_container_changed(GmRuntime *r);
int gm_runtime_furnace_count(const GmRuntime *r);
int gm_runtime_furnace_get(
    const GmRuntime *r, int index, GmRuntimeFurnace *out);
int gm_runtime_static_container_set_slot(
    GmRuntime *r, int dimension, int x, int y, int z,
    int slot, int item, int count, int meta);
int gm_runtime_hopper_set_transfer_state(
    GmRuntime *r, int dimension, int x, int y, int z,
    int transfer_cooldown, long long ticked_game_time);
int gm_runtime_shulker_set_item_tag_nbt(
    GmRuntime *r, int dimension, int x, int y, int z,
    const void *item_tag_nbt, size_t item_tag_nbt_len);
int gm_runtime_shulker_set_transient(
    GmRuntime *r, int dimension, int x, int y, int z,
    int open_count, int animation_status,
    uint32_t progress_bits, uint32_t progress_old_bits);
int gm_runtime_static_container_count(const GmRuntime *r);
int gm_runtime_static_container_get(
    const GmRuntime *r, int index, GmRuntimeStaticContainer *out);
int gm_runtime_command_block_set_success(
    GmRuntime *r, int dimension, int x, int y, int z, int success_count);
int gm_runtime_command_block_set_state(
    GmRuntime *r, int dimension, int x, int y, int z,
    const char *command, const char *last_output, int success_count);
int gm_runtime_command_block_set_execution_state(
    GmRuntime *r, int dimension, int x, int y, int z,
    int powered, int automatic, int condition_met);
int gm_runtime_command_block_trigger(
    GmRuntime *r, int dimension, int x, int y, int z);
int gm_runtime_command_block_trigger_at_clock(
    GmRuntime *r, int dimension, int x, int y, int z,
    int hour, int minute, int second);
int gm_runtime_command_block_trigger_at_context(
    GmRuntime *r, int dimension, int x, int y, int z,
    int hour, int minute, int second, int weather_duration_ticks);
int gm_runtime_command_block_count(const GmRuntime *r);
int gm_runtime_command_block_get(
    const GmRuntime *r, int index, GmRuntimeCommandBlock *out);
int gm_runtime_structure_block_set(
    GmRuntime *r, int dimension, int x, int y, int z,
    const GmRuntimeStructureBlock *value);
int gm_runtime_structure_block_count(const GmRuntime *r);
int gm_runtime_structure_block_get(
    const GmRuntime *r, int index, GmRuntimeStructureBlock *out);
/* GuiEditStructure's local form and MC|Struct actions. Button ids are the
 * vanilla GuiEditStructure ids (0/1/9..14/18..23). Text input is filtered and
 * capped with the same per-field limits as the Java GuiTextFields. */
int gm_runtime_structure_gui_open(
    GmRuntime *r, int dimension, int x, int y, int z);
int gm_runtime_structure_gui_button(GmRuntime *r, int button_id);
int gm_runtime_structure_gui_focus(GmRuntime *r, int field);
int gm_runtime_structure_gui_text(
    GmRuntime *r, const char *utf8, int utf8_len, int backspace, int tab);
int gm_runtime_structure_detect_size(
    GmRuntime *r, int dimension, int x, int y, int z);
/* require_matching_size matches TileEntityStructure.load(true). Pass zero
 * for the redstone trigger path, which updates SIZE and places immediately. */
int gm_runtime_structure_save(
    GmRuntime *r, int dimension, int x, int y, int z);
int gm_runtime_structure_load(
    GmRuntime *r, int dimension, int x, int y, int z,
    int require_matching_size);
int gm_runtime_structure_unload(
    GmRuntime *r, int dimension, int x, int y, int z);
int gm_runtime_structure_template_count(const GmRuntime *r);
int gm_runtime_flower_pot_set(
    GmRuntime *r, int dimension, int x, int y, int z, int item, int meta);
int gm_runtime_flower_pot_count(const GmRuntime *r);
int gm_runtime_flower_pot_get(
    const GmRuntime *r, int index, GmRuntimeFlowerPot *out);
int gm_runtime_note_block_set(
    GmRuntime *r, int dimension, int x, int y, int z,
    int note, int powered);
int gm_runtime_note_block_count(const GmRuntime *r);
int gm_runtime_note_block_get(
    const GmRuntime *r, int index, GmRuntimeNoteBlock *out);
int gm_runtime_note_block_play(
    GmRuntime *r, int x, int y, int z);
int gm_runtime_skull_set(
    GmRuntime *r, int dimension, int x, int y, int z,
    int type, int rotation);
int gm_runtime_skull_set_profile_nbt(
    GmRuntime *r, int dimension, int x, int y, int z,
    int type, int rotation, const void *profile_nbt, size_t profile_nbt_len);
/* BlockSkull.checkWitherSpawn after a live type-1 skull placement. The
 * supplied position must name the just-placed skull tile. Returns one only
 * when a complete structure is consumed and an ignited Wither is spawned. */
int gm_runtime_check_wither_spawn(
    GmRuntime *r, int x, int y, int z);
int gm_runtime_skull_count(const GmRuntime *r);
int gm_runtime_skull_get(
    const GmRuntime *r, int index, GmRuntimeSkull *out);
int gm_runtime_decorative_tile_set_nbt(
    GmRuntime *r, int dimension, int x, int y, int z,
    const void *tile_nbt, size_t tile_nbt_len,
    int drop_item, int drop_meta,
    const void *drop_tag_nbt, size_t drop_tag_nbt_len);
int gm_runtime_decorative_tile_count(const GmRuntime *r);
int gm_runtime_decorative_tile_get(
    const GmRuntime *r, int index, GmRuntimeDecorativeTile *out);
int gm_runtime_tagged_item_get_by_eid(
    const GmRuntime *r, int eid, GmRuntimeTaggedItem *out);
int gm_runtime_item_frame_set(
    GmRuntime *r, int dimension, int eid,
    double x, double y, double z,
    int hanging_x, int hanging_y, int hanging_z,
    int facing, int item, int count, int meta, int rotation);
int gm_runtime_item_frame_set_full(
    GmRuntime *r, int dimension, int eid,
    int hanging_x, int hanging_y, int hanging_z, int facing,
    ICStack stack, int rotation, int tick_counter, float item_drop_chance,
    uint64_t random_seed48, int random_have_gaussian,
    double random_gaussian, int64_t uuid_most, int64_t uuid_least);
int gm_runtime_item_frame_count(const GmRuntime *r);
int gm_runtime_item_frame_get(
    const GmRuntime *r, int index, GmRuntimeItemFrame *out);
const GmRuntimeItemFrame *gm_runtime_item_frame_ref(
    const GmRuntime *r, int index);
int gm_runtime_map_data_set(
    GmRuntime *r, int map_id, int scale, int has_exploration_marker);
const GmRuntimeMapData *gm_runtime_map_data_ref(
    const GmRuntime *r, int map_id);
int gm_runtime_update_filled_map_now(GmRuntime *r, int map_id);
const unsigned char *gm_runtime_item_frame_map_colors(
    const GmRuntime *r, int index);
int gm_runtime_item_frame_aabb(
    const GmRuntimeItemFrame *frame, McAABB *out);
int gm_runtime_item_frame_set_uuid(
    GmRuntime *r, int eid, int64_t most, int64_t least);
int gm_runtime_item_frame_set_map_state(
    GmRuntime *r, int eid, int tracker_update_counter,
    int map_data_present, int map_dimension,
    int map_x_center, int map_z_center, int map_scale,
    int map_tracking_position, int map_unlimited_tracking,
    int map_decoration_present, int map_decoration_type,
    int map_decoration_x, int map_decoration_z,
    int map_decoration_rotation);
int gm_runtime_item_frame_set_map_colors(
    GmRuntime *r, int eid, const unsigned char colors[128 * 128]);
int gm_runtime_item_frame_tracker_tick(GmRuntime *r, int eid);
int gm_runtime_item_frame_interact(
    GmRuntime *r, int eid, int hand_slot, int creative);
int gm_runtime_place_item_frame(
    GmRuntime *r, int hanging_x, int hanging_y, int hanging_z,
    int facing, int hand_slot, int creative);
int gm_runtime_damage_item_frame(
    GmRuntime *r, int eid, int creative, int explosion);
int gm_runtime_painting_set(
    GmRuntime *r, int dimension, int eid,
    int hanging_x, int hanging_y, int hanging_z,
    int facing, int art, int tick_counter);
int gm_runtime_painting_count(const GmRuntime *r);
int gm_runtime_painting_get(
    const GmRuntime *r, int index, GmRuntimePainting *out);
int gm_runtime_painting_aabb(
    const GmRuntimePainting *painting, McAABB *out);
int gm_runtime_painting_set_uuid(
    GmRuntime *r, int eid, int64_t most, int64_t least);
int gm_runtime_place_painting(
    GmRuntime *r, int hanging_x, int hanging_y, int hanging_z,
    int facing, int hand_slot, int creative);
int gm_runtime_break_painting(
    GmRuntime *r, int eid, int creative);
int gm_runtime_leash_knot_set(
    GmRuntime *r, int dimension, int eid,
    int x, int y, int z, int tick_counter);
int gm_runtime_leash_knot_count(const GmRuntime *r);
int gm_runtime_leash_knot_get(
    const GmRuntime *r, int index, GmRuntimeLeashKnot *out);
int gm_runtime_leash_knot_aabb(
    const GmRuntimeLeashKnot *knot, McAABB *out);
int gm_runtime_leash_knot_set_uuid(
    GmRuntime *r, int eid, int64_t most, int64_t least);
int gm_runtime_attach_llamas_to_fence(
    GmRuntime *r, int x, int y, int z);
int gm_runtime_attach_living_to_fence(
    GmRuntime *r, int x, int y, int z);
int gm_runtime_living_leash_interact(
    GmRuntime *r, int living_eid, int hand_slot);
int gm_runtime_leash_knot_interact(
    GmRuntime *r, int eid, int creative);
int gm_runtime_damage_leash_knot(
    GmRuntime *r, int eid, int creative);
int gm_runtime_restore_llama_leash_knot(
    GmRuntime *r, int llama_eid, int knot_eid);
int gm_runtime_restore_llama_leash_pending(
    GmRuntime *r, int llama_eid, int x, int y, int z);
int gm_runtime_restore_living_leash_knot(
    GmRuntime *r, int living_eid, int knot_eid);
int gm_runtime_restore_living_leash_pending(
    GmRuntime *r, int living_eid, int x, int y, int z);
/* EntityHanging.onUpdate pass, exposed for parked-oracle callback probes. */
void gm_runtime_tick_hanging_entities(GmRuntime *r);
int gm_runtime_redstone_torch_toggle_add(
    GmRuntime *r, int x, int y, int z, long long time);
int gm_runtime_redstone_torch_toggle_count(const GmRuntime *r);
int gm_runtime_redstone_torch_toggle_get(
    const GmRuntime *r, int index, GmRuntimeRedstoneTorchToggle *out);
/* Interactive / harness respawn (GuiGameOver Respawn button / SPacketRespawn).
 * Restores health to 20, clears dead + fire/hurt, resets death_screen_ticks. */
void gm_runtime_respawn(GmRuntime *r);
/* EntityPlayerMP.onDeath inventory boundary. Vanishing-cursed stacks are
 * destroyed; remaining main, armor, then offhand stacks become exact drops
 * unless keepInventory is enabled. Returns the number of spawned drops. */
int gm_runtime_player_death_inventory(GmRuntime *r);
/* Forge's two-argument EntityPlayer.dropItem path used by containers: exact
 * forward toss with constructor/player RNG, UUID/EID, order, and full stack. */
int gm_runtime_drop_player_stack(GmRuntime *r, ICStack stack);
int gm_runtime_set_dimension(GmRuntime *r, int dimension);
void gm_runtime_set_time(GmRuntime *r, long long world_time);
void gm_runtime_set_total_time(GmRuntime *r, long long total_time);
/* Tape/live GameRules. Runtime mechanics currently honor naturalRegeneration,
 * doDaylightCycle, and doWeatherCycle; script.c consumes other header entries
 * without changing today's simulation. */
void gm_runtime_set_gamerules(GmRuntime *r, const McGameRules *gamerules);
int gm_runtime_set_block(GmRuntime *r, int x, int y, int z, int id, int meta);
int gm_runtime_world_event_count(const GmRuntime *r);
int gm_runtime_world_event_get(
    const GmRuntime *r, int index, GmRuntimeWorldEvent *out);
/* Cold display-name registry used by represented ItemStack display.Name.
 * IDs are runtime-local and copied with stacks; zero means no custom name. */
int gm_runtime_item_name_intern(GmRuntime *r, const char *name);
const char *gm_runtime_item_name(const GmRuntime *r, int id);
int gm_runtime_stack_tag_intern(
    GmRuntime *r, const void *data, size_t len);
const GmNbtBlob *gm_runtime_stack_tag(const GmRuntime *r, int id);
int gm_runtime_spawner_set_state(
    GmRuntime *r, int x, int y, int z, int entity_type,
    int delay, int min_delay, int max_delay, int spawn_count,
    int max_nearby, int activate_range, int spawn_range,
    const void *spawn_data, size_t spawn_data_len, int default_entity_nbt);
int gm_runtime_spawner_add_potential(
    GmRuntime *r, int x, int y, int z, int entity_type, int weight,
    const void *entity_nbt, size_t entity_nbt_len, int default_entity_nbt);
int gm_runtime_anvil_set_name(GmRuntime *r, const char *name);
/* Called after a successful output take: degrades/breaks the live anvil and
 * appends vanilla world event 1030/1029 using EntityPlayer.rand. */
void gm_runtime_anvil_finish(GmRuntime *r, int creative);
int gm_runtime_harvest_block(GmRuntime *r, int x, int y, int z);
/* Exact same-item drop list for the represented 1.11.2 harvest roster.
 * Mutates World.rand exactly through Block.getDrops; spawning consumes the
 * later chance/offset draws. Returns zero only for an unsupported block. */
int gm_runtime_harvest_drop_result(
    GmRuntime *r, int block_id, int block_meta, int tool_id,
    int silk_touch, int fortune, int *item, int *count, int *item_meta);
int gm_runtime_harvest_xp_result(
    GmRuntime *r, int block_id, int tool_id,
    int silk_touch, int fortune, int *xp);
/* Snapshot initialization: canonical cell replacement with no fluid/plant
 * mutation side effects. Must run before the first replay tick. */
int gm_runtime_load_block(GmRuntime *r, int x, int y, int z, int id, int meta);
int gm_runtime_snapshot_region(GmRuntime *r, int ccx, int ccz, int radius);
int gm_runtime_load_block_dim(GmRuntime *r, int dimension, int x, int y, int z,
                              int id, int meta);
/* Bulk Anvil restore path. It replaces canonical cells without constructing
 * tile/runtime side tables; those are restored from their ordered NBT stream. */
int gm_runtime_load_raw_block_dim(
    GmRuntime *r, int dimension, int x, int y, int z, int id, int meta);
int gm_runtime_snapshot_region_dim(GmRuntime *r, int dimension,
                                   int ccx, int ccz, int radius);
int gm_runtime_attach_chunk_store_dim(
    GmRuntime *r, int dimension, const char *path);
int gm_runtime_write_chunk_store_dim(
    GmRuntime *r, int dimension, const char *path);
int gm_runtime_finalize_block_snapshot_dim(
    GmRuntime *r, int dimension, int ccx, int ccz, int radius);
int gm_runtime_load_sky_light_dim(
    GmRuntime *r, int dimension, int x, int y, int z, int value);
int gm_runtime_load_height_dim(
    GmRuntime *r, int dimension, int x, int z, int value);
int gm_runtime_finalize_sky_light_snapshot_dim(
    GmRuntime *r, int dimension);
int gm_runtime_load_block_light_dim(
    GmRuntime *r, int dimension, int x, int y, int z, int value);
int gm_runtime_finalize_block_light_snapshot_dim(
    GmRuntime *r, int dimension);
int gm_runtime_set_inventory(GmRuntime *r, int slot, int item, int count, int meta);
int gm_runtime_set_inventory_stack(GmRuntime *r, int slot, ICStack stack);
void gm_runtime_set_weather(GmRuntime *r, int raining, int thundering,
                            int rain_time, int thunder_time);
void gm_runtime_set_weather_full(
    GmRuntime *r, int raining, int thundering, int rain_time,
    int thunder_time, int clean_weather_time, int weather_cycle,
    float prev_rain_strength, float rain_strength,
    float prev_thunder_strength, float thunder_strength);
void gm_runtime_set_daylight_cycle(GmRuntime *r, int enabled);
/* Exact WorldServer iceandsnow column body. The direct entry is a narrow
 * oracle/test seam; ordinary play reaches it through loaded weather chunks. */
int gm_runtime_weather_ice_snow_at(
    GmRuntime *r, int x, int z, int raining);
int gm_runtime_weather_chunk_tick(GmRuntime *r, int cx, int cz);
int gm_runtime_spawn_lightning(
    GmRuntime *r, double x, double y, double z, int effect_only);
int gm_runtime_restore_lightning(
    GmRuntime *r, int dimension, int eid, int ticks_existed,
    int lightning_state, int living_time, int effect_only,
    long long bolt_vertex, uint64_t random_seed48,
    double x, double y, double z);
int gm_runtime_set_next_lightning_random_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_weather_event_count(const GmRuntime *r);
int gm_runtime_weather_event_get(
    const GmRuntime *r, int index, GmRuntimeWeatherEvent *out);
int gm_runtime_lightning_views(
    const GmRuntime *r, GmLightningView *out, int max);
int gm_runtime_set_next_firework_random_state(
    GmRuntime *r, uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
int gm_runtime_set_next_firework_audio_random_seeds(
    GmRuntime *r, uint64_t blast_seed48, uint64_t twinkle_seed48);
int gm_runtime_spawn_firework_payload(
    GmRuntime *r, double x, double y, double z,
    int flight, int explosion_count, int large_blast, int twinkle,
    int attached_player);
int gm_runtime_spawn_firework_state_fixture(
    GmRuntime *r, int eid,
    double x, double y, double z, double vx, double vy, double vz,
    float yaw, float pitch, float prev_yaw, float prev_pitch,
    int age, int lifetime, int ticks_existed, int attached_player,
    int flight, int explosion_count, int large_blast, int twinkle,
    int firework_item_present, int firework_item, int firework_count,
    int firework_meta, int stack_tag_id, uint64_t random_seed48,
    int random_have_gaussian, double random_gaussian);
int gm_runtime_firework_audio_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    int explosion_count, int large_blast, int twinkle,
    uint64_t blast_seed48, uint64_t twinkle_seed48);
int gm_runtime_spawn_firework(
    GmRuntime *r, double x, double y, double z,
    int flight, int explosion_count, int attached_player);
int gm_runtime_firework_item_payload(
    const GmRuntime *r, const ICStack *stack,
    int *flight, int *explosion_count, int *large_blast, int *twinkle);
void gm_runtime_tick_fireworks(GmRuntime *r);
int gm_runtime_firework_event_count(const GmRuntime *r);
int gm_runtime_firework_event_get(
    const GmRuntime *r, int index, GmRuntimeFireworkEvent *out);
int gm_runtime_set_next_fishing_random_state(
        GmRuntime *r, uint64_t seed48, int have_next_gaussian,
        double next_gaussian);
int gm_runtime_spawn_fish_hook_fixture(
        GmRuntime *r, int eid,
        double x, double y, double z, double vx, double vy, double vz,
        float yaw, float pitch, int state, int in_ground,
        int ticks_in_ground, int ticks_in_air, int ticks_catchable,
        int ticks_caught_delay, int ticks_catchable_delay,
        float approach_angle, int lure, int luck, int caught_eid,
        uint64_t seed48, int have_next_gaussian, double next_gaussian);
int gm_runtime_cast_fishing_rod(GmRuntime *r, int lure, int luck);
int gm_runtime_retract_fishing_rod(GmRuntime *r);
void gm_runtime_tick_fishing(GmRuntime *r);
int gm_runtime_fish_event_count(const GmRuntime *r);
int gm_runtime_fish_event_get(
    const GmRuntime *r, int index, GmRuntimeFishEvent *out);
int gm_runtime_smelt_event_count(const GmRuntime *r);
int gm_runtime_smelt_event_get(
    const GmRuntime *r, int index, GmRuntimeSmeltEvent *out);
int gm_runtime_brewed_potion_taken(GmRuntime *r, ICStack stack);
int gm_runtime_brew_event_count(const GmRuntime *r);
int gm_runtime_brew_event_get(
    const GmRuntime *r, int index, GmRuntimeBrewEvent *out);
int gm_runtime_sound_event_count(const GmRuntime *r);
int gm_runtime_sound_event_get(
    const GmRuntime *r, int index, GmRuntimeSoundEvent *out);
int gm_runtime_particle_event_count(const GmRuntime *r);
int gm_runtime_particle_event_get(
    const GmRuntime *r, int index, GmRuntimeParticleEvent *out);
int gm_runtime_set_sound_random_seed48(GmRuntime *r, uint64_t seed48);
int gm_runtime_block_break_sound(
    int state_id, int *sound, float *volume, float *pitch);
int gm_runtime_block_break_audio_fixture(
    GmRuntime *r, int x, int y, int z, int state_id);
int gm_runtime_block_place_sound(
    int state_id, int *sound, float *volume, float *pitch);
int gm_runtime_block_place_audio_fixture(
    GmRuntime *r, int x, int y, int z, int state_id);
int gm_runtime_block_hit_sound(
    int state_id, int *sound, float *volume, float *pitch);
int gm_runtime_block_fall_sound(
    int state_id, int *sound, float *volume, float *pitch);
int gm_runtime_block_step_sound(
    int state_id, int *sound, float *volume, float *pitch);
int gm_runtime_villager_offer_count(GmRuntime *r, int eid);
int gm_runtime_villager_offer_get(
    GmRuntime *r, int eid, int index, GmVillagerOffer *out);
int gm_runtime_villager_trade_execute(
    GmRuntime *r, int eid, int offer_index,
    ICStack *first, ICStack *second, ICStack *output,
    int *xp_value);
int gm_runtime_open_villager(GmRuntime *r, int eid);
int gm_runtime_open_horse_inventory(GmRuntime *r, int eid);
int gm_runtime_merchant_select(GmRuntime *r, int index);
void gm_runtime_merchant_refresh(GmRuntime *r);
int gm_runtime_restore_villager_trade(
    GmRuntime *r, int eid, int career, int career_level,
    int wealth, int willing_to_mate, int offer_count);
int gm_runtime_restore_villager_offer(
    GmRuntime *r, int eid, int index,
    int uses, int max_uses, int rewards_exp);
int gm_runtime_restore_villager_offer_stack(
    GmRuntime *r, int eid, int index, int part, ICStack stack);
int gm_runtime_spawn_minecart_fixture(
    GmRuntime *r, int kind, int eid,
    double x, double y, double z, double vx, double vy, double vz,
    float yaw);
int gm_runtime_minecart_get(
    const GmRuntime *r, int index, GmRuntimeMinecart *out);
int gm_runtime_minecart_set_slot(
    GmRuntime *r, int eid, int slot, int item, int count, int meta);
int gm_runtime_minecart_set_slot_stack(
    GmRuntime *r, int eid, int slot, ICStack stack);
int gm_runtime_minecart_set_state(
    GmRuntime *r, int eid, int fuel, double push_x, double push_z,
    int tnt_fuse, int hopper_enabled, int transfer_cooldown);
int gm_runtime_minecart_set_spawner_state(
    GmRuntime *r, int eid, int entity_type, int delay,
    int min_delay, int max_delay, int spawn_count,
    int max_nearby, int spawn_range, int activate_range);
int gm_runtime_minecart_set_spawner_nbt_state(
    GmRuntime *r, int eid, int entity_type, int delay,
    int min_delay, int max_delay, int spawn_count,
    int max_nearby, int spawn_range, int activate_range,
    const void *spawn_data, size_t spawn_data_len,
    int default_entity_nbt);
int gm_runtime_minecart_add_spawner_potential(
    GmRuntime *r, int eid, int entity_type, int weight,
    const void *entity_nbt, size_t entity_nbt_len,
    int default_entity_nbt);
int gm_runtime_minecart_set_command_state(
    GmRuntime *r, int eid, const char *command, const char *custom_name,
    int success_count, int track_output, const char *last_output);
int gm_runtime_minecart_set_base_state(
    GmRuntime *r, int eid, int reverse, int rolling_amplitude,
    int rolling_direction, float damage, float pitch);
int gm_runtime_minecart_set_random_state(
    GmRuntime *r, int eid, uint64_t seed48,
    int have_next_gaussian, double next_gaussian);
int gm_runtime_minecart_mount(GmRuntime *r, int eid);
int gm_runtime_minecart_dismount(GmRuntime *r);
int gm_runtime_minecart_riding(const GmRuntime *r);
int gm_runtime_minecart_set_uuid(
    GmRuntime *r, int eid, int64_t most, int64_t least);
int gm_runtime_minecart_set_rider_input(
    GmRuntime *r, float forward, float yaw);
int gm_runtime_minecart_attack(
    GmRuntime *r, int eid, float amount,
    int creative, int explosion, int fire_damage);
void gm_runtime_tick_minecarts(GmRuntime *r);
int gm_runtime_spawn_end_gateway(
    GmRuntime *r, int x, int y, int z,
    int has_exit, int exit_x, int exit_y, int exit_z,
    int exact_teleport);
void gm_runtime_tick_end_gateways(GmRuntime *r);
/* Execute ChunkProviderEnd.populate's natural feature body from an observed
 * internal java.util.Random cursor. Automatic streaming supplies the normal
 * southeast-neighbour cursor; oracle replay may inject a captured cursor. */
int gm_runtime_populate_end_chunk(
    GmRuntime *r, int chunk_x, int chunk_z,
    unsigned long long seed48);
int gm_runtime_end_gateway_count(const GmRuntime *r);
int gm_runtime_end_gateway_get(
    const GmRuntime *r, int index, GmRuntimeEndGateway *out);
int gm_runtime_generate_end_city(
    GmRuntime *r, int chunk_x, int chunk_z, int start_y);
int gm_runtime_generate_mansion(
    GmRuntime *r, int chunk_x, int chunk_z, int start_y);
int gm_runtime_generate_monument(
    GmRuntime *r, int chunk_x, int chunk_z);
int gm_runtime_monument_candidate(
    const GmRuntime *r, int chunk_x, int chunk_z);
int gm_runtime_mansion_resident_count(const GmRuntime *r);
int gm_runtime_mansion_resident_get(
    const GmRuntime *r, int index, GmRuntimeMansionResident *out);
int gm_runtime_spawn_shulker_fixture(
    GmRuntime *r, int eid, int x, int y, int z, int face,
    uint64_t random_seed48);
int gm_runtime_spawn_shulker_state_fixture(
    GmRuntime *r, int eid, int x, int y, int z, int face,
    int no_ai, int peek_tick, int peek_time, int attack_time,
    int has_player_target,
    int watch_time, int idle_look_time, int living_sound_time,
    int ticks_existed, int hurt_time, int hurt_resistant_time,
    int death_time, float health, float last_damage,
    float prev_peek_amount, float peek_amount,
    float head_yaw, float head_pitch, uint64_t random_seed48);
int gm_runtime_shulker_count(const GmRuntime *r);
int gm_runtime_shulker_get(
    const GmRuntime *r, int index, GmRuntimeShulker *out);
int gm_runtime_shulker_bullet_count(const GmRuntime *r);
int gm_runtime_shulker_bullet_get(
    const GmRuntime *r, int index, GmRuntimeShulkerBullet *out);
int gm_runtime_spawn_shulker_bullet_fixture(
    GmRuntime *r, int eid, int owner_eid, uint64_t random_seed48);
int gm_runtime_spawn_shulker_bullet_state_fixture(
    GmRuntime *r, int eid, int owner_eid, int direction, int steps,
    int ticks_existed, double x, double y, double z,
    double vx, double vy, double vz,
    double target_dx, double target_dy, double target_dz,
    float yaw, float pitch, uint64_t random_seed48);
int gm_runtime_shulker_damage_fixture(
    GmRuntime *r, int eid, float amount, int arrow);
void gm_runtime_tick_shulkers_fixture(GmRuntime *r);
int gm_runtime_shulker_views(
    const GmRuntime *r, GmEntityView *out, int max);
int gm_runtime_wither_views(
    const GmRuntime *r, GmEntityView *out, int max);
int gm_runtime_break_item_frame(GmRuntime *r, int eid);
int gm_runtime_set_fire_rain_context(
    GmRuntime *r, int x, int y, int z, int can_die, int raining_at_east,
    int can_die_west_candidate);
int gm_runtime_set_fire_humidity_context(
    GmRuntime *r, int x, int y, int z);
int gm_runtime_projectile_views(const GmRuntime *r, GmEntityView *out, int max);
int gm_runtime_end_crystal_views(
    const GmRuntime *r, GmEntityView *out, int max);
int gm_runtime_falling_block_views(
    const GmRuntime *r, GmEntityView *out, int max);
/* Execute one survival crafting take from inventory-backed grid slots. Empty
 * cells are -1. Returns 1 only if a recipe matched and the output fit. */
int gm_runtime_craft(GmRuntime *r, int grid_width, const int inv_slots[9]);
/* Survival use at a world block. Verifies reach and block identity before opening. */
int gm_runtime_use_block(GmRuntime *r, int wx, int wy, int wz);
/* Survival/creative ItemDye.applyBonemeal at a represented player inventory
 * slot. Returns one only for Java's successful IGrowable/canGrow boundary. */
int gm_runtime_player_apply_bonemeal(
    GmRuntime *r, int wx, int wy, int wz, int inventory_slot, int creative);
/* ContainerEnchantment.enchantItem for one of the three offer rows. */
int gm_runtime_enchant_click(GmRuntime *r, int button);
int gm_runtime_furnace_insert(GmRuntime *r, int furnace_slot,
                              int inventory_slot, int amount);
int gm_runtime_furnace_extract(GmRuntime *r, int furnace_slot, int amount);
/* SlotFurnaceOutput.onCrafting: round recipe XP and spawn split orbs. */
int gm_runtime_furnace_output_taken(
    GmRuntime *r, ICStack output, int removed_count);

#endif
