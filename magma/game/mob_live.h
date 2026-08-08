#ifndef MAGMA_GAME_MOB_LIVE_H
#define MAGMA_GAME_MOB_LIVE_H

/* Product living simulation keeps the original 95 usable hot slots and two
 * non-spawnable staging slots for interacting cold AoS rows. Blaze includes the same store
 * without these overrides and therefore retains its original 96-slot ABI. */
#ifndef EW_MAX_ENTITIES
#define EW_MAX_ENTITIES 98
#endif
#ifndef EW_SPAWN_LIMIT
#define EW_SPAWN_LIMIT (EW_MAX_ENTITIES - 2)
#endif

#include "game/game.h"
#include "game/live_sim.h"
#include "game/nbt_blob.h"
#include "entity_hostile_spine.h"
#include "entity_blaze_fireball.h"
#include "entity_xp_orb.h"
#include "inventory_stack_rules.h"
#include "potion_throwable.h"
#include "game/villager_trade.h"

typedef struct GmLivingColdSlot GmLivingColdSlot;

#define GM_SPAWNERS 64
/* Fast-page living capacity (slot 0 and two staging slots are reserved).
 * Product capacity is dynamic through GmLivingColdSlot. */
#define GM_MOB_CAPACITY (EW_SPAWN_LIMIT - 1)
#define GM_MOB_LIVING_STAGE_SLOT EW_SPAWN_LIMIT
#define GM_MOB_LIVING_STAGE_SLOT_SECONDARY (EW_SPAWN_LIMIT + 1)
#define GM_XP_ORBS GM_MOB_CAPACITY
/* Initial living + XP dispatches plus the bounded child/XP append tail. */
#define GM_MOB_UPDATE_ORDER_CAPACITY (2 * GM_MOB_CAPACITY + GM_XP_ORBS)
#define GM_MOB_LOADED_ORDER_CAPACITY \
    (2 * (GM_MOB_CAPACITY + GM_XP_ORBS))
#define GM_MOB_EFFECT_CAPACITY 27

enum {
    GM_MOB_LOADED_LIVING = 1,
    GM_MOB_LOADED_XP = 2
};

typedef struct {
    int eid;
    unsigned int generation;
    unsigned char kind;
    unsigned int slot;
} GmMobLoadedRef;

typedef struct {
    McOrb orb;
    uint64_t random_seed48;
    int64_t uuid_most, uuid_least;
    unsigned int loaded_generation;
    signed char dimension;
    unsigned char uuid_present;
} GmXpOrbCold;
#define GM_BLAZE_SHOT_QUEUE 3
#define GM_WITCH_SHOT_QUEUE 3
#define GM_MOB_EVENT_CAPACITY (GM_MOB_CAPACITY * 3)
#define GM_MOB_TERMINAL_PARTICLE_COUNT 20
#define GM_MOB_TERMINAL_PARTICLE_CAPACITY GM_MOB_CAPACITY
#define GM_MOB_PARTICLE_BATCH_MAX 7
#define GM_MOB_PARTICLE_BATCH_CAPACITY GM_MOB_EVENT_CAPACITY
#define GM_PIG_COLLISION_BOXES 512
#define GM_EVOKER_FANGS 256
#define GM_HORSE_INVENTORY_SLOTS 17
#define GM_SKELETON_TRAP_PAIRS 4
#define GM_SKELETON_TRAP_SHOTS GM_SKELETON_TRAP_PAIRS
#define GM_LLAMA_SPITS 128
#define GM_SNOWMAN_SHOTS 128

enum {
    GM_DAMAGE_SOURCE_GENERIC = 1,
    GM_DAMAGE_SOURCE_FIRE = 2,
    GM_DAMAGE_SOURCE_FALL = 4,
    GM_DAMAGE_SOURCE_EXPLOSION = 8,
    GM_DAMAGE_SOURCE_PROJECTILE = 16,
    GM_DAMAGE_SOURCE_MAGIC = 32
};

typedef struct {
    double x, y, z;
    double aim_x, aim_y, aim_z;
} GmBlazeShot;

typedef struct {
    double x, y, z;
    double aim_x, aim_y, aim_z;
    int potion;
    float sound_pitch;
} GmWitchShot;

typedef struct {
    int shooter_eid;
    double x, y, z;
    double vx, vy, vz;
    double target_x, target_y, target_z;
    int on_ground;
    int power, punch, flame;
    float sound_pitch;
} GmSkeletonTrapShot;

typedef struct {
    int slot;
    int eid;
    int type;
    double x, y, z;
    double vx, vy, vz;
    float health;
    float eye_height;
    int hurt_time;
    int hurt_resistant_time;
    McAABB box;
} GmMobExplosionTarget;

typedef struct {
    int active;
    int eid;
    int caster_eid;
    int dimension;
    int warmup;
    int life_ticks;
    int sent_spike;
    double x, y, z;
    float yaw;
} GmEvokerFang;

enum {
    GM_MOB_EVENT_ENTITY_STATUS = 1,
    GM_MOB_EVENT_SOUND = 2,
    GM_MOB_EVENT_WORLD_EVENT = 3
};

enum {
    GM_MOB_SOUND_CHICKEN_HURT = 1,
    GM_MOB_SOUND_CHICKEN_DEATH = 2,
    GM_MOB_SOUND_PIG_HURT = 3,
    GM_MOB_SOUND_PIG_DEATH = 4,
    GM_MOB_SOUND_COW_HURT = 5,
    GM_MOB_SOUND_COW_DEATH = 6,
    GM_MOB_SOUND_SHEEP_HURT = 7,
    GM_MOB_SOUND_SHEEP_DEATH = 8,
    GM_MOB_SOUND_SHEEP_SHEAR = 9,
    GM_MOB_SOUND_CHICKEN_EGG = 10,
    GM_MOB_SOUND_COW_MILK = 11,
    GM_MOB_SOUND_ITEM_ARMOR_EQUIP_GENERIC = 12,
    GM_MOB_SOUND_PIG_SADDLE = 13,
    GM_MOB_SOUND_ZOMBIE_HURT,
    GM_MOB_SOUND_ZOMBIE_DEATH,
    GM_MOB_SOUND_ZOMBIE_VILLAGER_HURT,
    GM_MOB_SOUND_ZOMBIE_VILLAGER_DEATH,
    GM_MOB_SOUND_PIGMAN_HURT,
    GM_MOB_SOUND_PIGMAN_DEATH,
    GM_MOB_SOUND_SKELETON_HURT,
    GM_MOB_SOUND_SKELETON_DEATH,
    GM_MOB_SOUND_WITHER_SKELETON_HURT,
    GM_MOB_SOUND_WITHER_SKELETON_DEATH,
    GM_MOB_SOUND_CREEPER_HURT,
    GM_MOB_SOUND_CREEPER_DEATH,
    GM_MOB_SOUND_SPIDER_HURT,
    GM_MOB_SOUND_SPIDER_DEATH,
    GM_MOB_SOUND_ENDERMAN_HURT,
    GM_MOB_SOUND_ENDERMAN_DEATH,
    GM_MOB_SOUND_ENDERMAN_TELEPORT,
    GM_MOB_SOUND_BLAZE_HURT,
    GM_MOB_SOUND_BLAZE_DEATH,
    GM_MOB_SOUND_GHAST_HURT,
    GM_MOB_SOUND_GHAST_DEATH,
    GM_MOB_SOUND_SLIME_HURT,
    GM_MOB_SOUND_SLIME_DEATH,
    GM_MOB_SOUND_SMALL_SLIME_HURT,
    GM_MOB_SOUND_SMALL_SLIME_DEATH,
    GM_MOB_SOUND_MAGMA_HURT,
    GM_MOB_SOUND_MAGMA_DEATH,
    GM_MOB_SOUND_SMALL_MAGMA_HURT,
    GM_MOB_SOUND_SMALL_MAGMA_DEATH,
    GM_MOB_SOUND_SILVERFISH_HURT,
    GM_MOB_SOUND_SILVERFISH_DEATH,
    GM_MOB_SOUND_VILLAGER_HURT,
    GM_MOB_SOUND_VILLAGER_DEATH,
    GM_MOB_SOUND_WITCH_AMBIENT,
    GM_MOB_SOUND_WITCH_THROW,
    GM_MOB_SOUND_WITCH_DRINK,
    GM_MOB_SOUND_WITCH_HURT,
    GM_MOB_SOUND_WITCH_DEATH,
    GM_MOB_SOUND_HOSTILE_SPLASH,
    GM_MOB_SOUND_HOSTILE_SMALL_FALL,
    GM_MOB_SOUND_HOSTILE_BIG_FALL,
    GM_MOB_SOUND_BLOCK_WOOD_FALL,
    GM_MOB_SOUND_BLOCK_GRAVEL_FALL,
    GM_MOB_SOUND_BLOCK_GRASS_FALL,
    GM_MOB_SOUND_BLOCK_STONE_FALL,
    GM_MOB_SOUND_BLOCK_METAL_FALL,
    GM_MOB_SOUND_BLOCK_GLASS_FALL,
    GM_MOB_SOUND_BLOCK_CLOTH_FALL,
    GM_MOB_SOUND_BLOCK_SAND_FALL,
    GM_MOB_SOUND_BLOCK_SNOW_FALL,
    GM_MOB_SOUND_BLOCK_LADDER_FALL,
    GM_MOB_SOUND_BLOCK_ANVIL_FALL,
    GM_MOB_SOUND_BLOCK_SLIME_FALL,
    GM_MOB_SOUND_BLOCK_WOOD_STEP,
    GM_MOB_SOUND_BLOCK_GRAVEL_STEP,
    GM_MOB_SOUND_BLOCK_GRASS_STEP,
    GM_MOB_SOUND_BLOCK_STONE_STEP,
    GM_MOB_SOUND_BLOCK_METAL_STEP,
    GM_MOB_SOUND_BLOCK_GLASS_STEP,
    GM_MOB_SOUND_BLOCK_CLOTH_STEP,
    GM_MOB_SOUND_BLOCK_SAND_STEP,
    GM_MOB_SOUND_BLOCK_SNOW_STEP,
    GM_MOB_SOUND_BLOCK_LADDER_STEP,
    GM_MOB_SOUND_BLOCK_ANVIL_STEP,
    GM_MOB_SOUND_BLOCK_SLIME_STEP,
    GM_MOB_SOUND_GENERIC_SMALL_FALL,
    GM_MOB_SOUND_GENERIC_BIG_FALL,
    GM_MOB_SOUND_WOLF_HURT,
    GM_MOB_SOUND_WOLF_DEATH,
    GM_MOB_SOUND_OCELOT_HURT,
    GM_MOB_SOUND_OCELOT_DEATH,
    GM_MOB_SOUND_VINDICATOR_HURT,
    GM_MOB_SOUND_VINDICATOR_DEATH,
    GM_MOB_SOUND_EVOKER_HURT,
    GM_MOB_SOUND_EVOKER_DEATH,
    GM_MOB_SOUND_EVOKER_PREPARE_ATTACK,
    GM_MOB_SOUND_EVOKER_PREPARE_SUMMON,
    GM_MOB_SOUND_EVOKER_PREPARE_WOLOLO,
    GM_MOB_SOUND_EVOKER_CAST,
    GM_MOB_SOUND_EVOKER_FANGS,
    GM_MOB_SOUND_VINDICATOR_AMBIENT,
    GM_MOB_SOUND_EVOKER_AMBIENT,
    GM_MOB_SOUND_VEX_AMBIENT,
    GM_MOB_SOUND_VEX_CHARGE,
    GM_MOB_SOUND_VEX_HURT,
    GM_MOB_SOUND_VEX_DEATH,
    GM_MOB_SOUND_GUARDIAN_AMBIENT,
    GM_MOB_SOUND_GUARDIAN_AMBIENT_LAND,
    GM_MOB_SOUND_GUARDIAN_ATTACK,
    GM_MOB_SOUND_GUARDIAN_DEATH,
    GM_MOB_SOUND_GUARDIAN_DEATH_LAND,
    GM_MOB_SOUND_GUARDIAN_FLOP,
    GM_MOB_SOUND_GUARDIAN_HURT,
    GM_MOB_SOUND_GUARDIAN_HURT_LAND,
    GM_MOB_SOUND_ELDER_GUARDIAN_AMBIENT,
    GM_MOB_SOUND_ELDER_GUARDIAN_AMBIENT_LAND,
    GM_MOB_SOUND_ELDER_GUARDIAN_CURSE,
    GM_MOB_SOUND_ELDER_GUARDIAN_DEATH,
    GM_MOB_SOUND_ELDER_GUARDIAN_DEATH_LAND,
    GM_MOB_SOUND_ELDER_GUARDIAN_FLOP,
    GM_MOB_SOUND_ELDER_GUARDIAN_HURT,
    GM_MOB_SOUND_ELDER_GUARDIAN_HURT_LAND,
    GM_MOB_SOUND_IRON_GOLEM_ATTACK,
    GM_MOB_SOUND_IRON_GOLEM_HURT,
    GM_MOB_SOUND_IRON_GOLEM_DEATH,
    GM_MOB_SOUND_IRON_GOLEM_STEP,
    GM_MOB_SOUND_HORSE_AMBIENT,
    GM_MOB_SOUND_HORSE_ANGRY,
    GM_MOB_SOUND_HORSE_ARMOR,
    GM_MOB_SOUND_HORSE_BREATHE,
    GM_MOB_SOUND_HORSE_DEATH,
    GM_MOB_SOUND_HORSE_EAT,
    GM_MOB_SOUND_HORSE_GALLOP,
    GM_MOB_SOUND_HORSE_HURT,
    GM_MOB_SOUND_HORSE_JUMP,
    GM_MOB_SOUND_HORSE_LAND,
    GM_MOB_SOUND_HORSE_SADDLE,
    GM_MOB_SOUND_HORSE_STEP,
    GM_MOB_SOUND_HORSE_STEP_WOOD,
    GM_MOB_SOUND_DONKEY_AMBIENT,
    GM_MOB_SOUND_DONKEY_ANGRY,
    GM_MOB_SOUND_DONKEY_CHEST,
    GM_MOB_SOUND_DONKEY_DEATH,
    GM_MOB_SOUND_DONKEY_HURT,
    GM_MOB_SOUND_MULE_AMBIENT,
    GM_MOB_SOUND_MULE_CHEST,
    GM_MOB_SOUND_MULE_DEATH,
    GM_MOB_SOUND_MULE_HURT,
    GM_MOB_SOUND_SKELETON_HORSE_AMBIENT,
    GM_MOB_SOUND_SKELETON_HORSE_DEATH,
    GM_MOB_SOUND_SKELETON_HORSE_HURT,
    GM_MOB_SOUND_ZOMBIE_HORSE_AMBIENT,
    GM_MOB_SOUND_ZOMBIE_HORSE_DEATH,
    GM_MOB_SOUND_ZOMBIE_HORSE_HURT,
    GM_MOB_SOUND_LLAMA_AMBIENT,
    GM_MOB_SOUND_LLAMA_ANGRY,
    GM_MOB_SOUND_LLAMA_CHEST,
    GM_MOB_SOUND_LLAMA_DEATH,
    GM_MOB_SOUND_LLAMA_EAT,
    GM_MOB_SOUND_LLAMA_HURT,
    GM_MOB_SOUND_LLAMA_SPIT,
    GM_MOB_SOUND_LLAMA_STEP,
    GM_MOB_SOUND_LLAMA_SWAG,
    GM_MOB_SOUND_BAT_AMBIENT,
    GM_MOB_SOUND_BAT_HURT,
    GM_MOB_SOUND_BAT_DEATH,
    GM_MOB_SOUND_MOOSHROOM_SHEAR,
    GM_MOB_SOUND_SNOWMAN_AMBIENT,
    GM_MOB_SOUND_SNOWMAN_HURT,
    GM_MOB_SOUND_SNOWMAN_DEATH,
    GM_MOB_SOUND_SNOWMAN_SHOOT,
    GM_MOB_SOUND_ENDERMITE_AMBIENT,
    GM_MOB_SOUND_ENDERMITE_HURT,
    GM_MOB_SOUND_ENDERMITE_DEATH,
    GM_MOB_SOUND_ENDERMITE_STEP,
    GM_MOB_SOUND_HUSK_AMBIENT,
    GM_MOB_SOUND_HUSK_HURT,
    GM_MOB_SOUND_HUSK_DEATH,
    GM_MOB_SOUND_HUSK_STEP,
    GM_MOB_SOUND_STRAY_AMBIENT,
    GM_MOB_SOUND_STRAY_HURT,
    GM_MOB_SOUND_STRAY_DEATH,
    GM_MOB_SOUND_STRAY_STEP,
    GM_MOB_SOUND_POLAR_BEAR_AMBIENT,
    GM_MOB_SOUND_POLAR_BEAR_BABY_AMBIENT,
    GM_MOB_SOUND_POLAR_BEAR_HURT,
    GM_MOB_SOUND_POLAR_BEAR_DEATH,
    GM_MOB_SOUND_POLAR_BEAR_STEP,
    GM_MOB_SOUND_POLAR_BEAR_WARNING,
    GM_MOB_SOUND_RABBIT_AMBIENT,
    GM_MOB_SOUND_RABBIT_ATTACK,
    GM_MOB_SOUND_RABBIT_DEATH,
    GM_MOB_SOUND_RABBIT_HURT,
    GM_MOB_SOUND_RABBIT_JUMP
};

enum {
    GM_PLAYER_POTION_SLOWNESS = 1,
    GM_PLAYER_POTION_POISON = 2,
    GM_PLAYER_POTION_WEAKNESS = 4
};

typedef struct {
    uint64_t seq;
    int kind;
    int eid;
    int data;
    double x, y, z;
    float volume, pitch;
} GmMobEvent;

typedef struct {
    int victim_eid;
    int village_index;
    int victim_child;
    int victim_killed;
    double x, y, z;
} GmVillagerHarmEvent;

typedef struct {
    int active;
    int dimension;
    int eid;
    int owner_eid;
    int owner_uuid_present;
    uint64_t owner_uuid_most, owner_uuid_least;
    int uuid_present;
    int64_t uuid_most, uuid_least;
    double x, y, z;
    double vx, vy, vz;
    float yaw, pitch, prev_yaw, prev_pitch;
    unsigned char no_gravity;
    unsigned char in_water;
    int ticks_existed;
    JavaGaussianRandom random;
} GmLlamaSpit;

typedef struct {
    int active;
    int dimension;
    int eid;
    int owner_eid;
    int owner_uuid_present;
    uint64_t owner_uuid_most, owner_uuid_least;
    int uuid_present;
    int64_t uuid_most, uuid_least;
    double x, y, z;
    double vx, vy, vz;
    float yaw, pitch;
    JavaGaussianRandom random;
} GmSnowmanShot;

/* Cold observation of the most recently accepted represented vehicle packet.
 * It exists to bracket the packet-before-base-tick boundary without changing
 * simulation state or adding work to an ordinary mob tick. */
typedef struct {
    uint64_t seq;
    int valid;
    int eid;
    double x, y, z;
    McAABB box;
    float fall_distance;
    int is_in_water;
    int is_in_lava;
    float health;
    int fire_ticks;
    int hurt_time;
    int hurt_resistant_time;
    int fire_resistance_ticks;
    float last_damage;
    int alive;
    uint64_t entity_seed48;
    uint64_t math_seed48;
} GmPigPacketContactCheckpoint;

enum {
    GM_PIG_VEHICLE_MOVE_ACCEPTED = 1,
    GM_PIG_VEHICLE_MOVE_CORRECTED_COLLISION = 2,
    GM_PIG_VEHICLE_MOVE_CORRECTED_SPEED = 3
};

/* Cold result for one dry authoritative CPacketVehicleMove transition.  The
 * direct oracle slice owns one server body, so both tracker triplets begin at
 * the entry pose.  The integrated dual-pose runtime retains its tracker
 * triplets across client prediction and successive packet dispatches. */
typedef struct {
    int result;
    int correction_count;
    double lowest_x, lowest_y, lowest_z;
    double lowest_x1, lowest_y1, lowest_z1;
    double correction_x, correction_y, correction_z;
    float correction_yaw, correction_pitch;
} GmPigVehicleMoveResult;

/* The integrated client and server own distinct copies of a controlled
 * vehicle. Keep the single represented server pig outside EwStore: those
 * ping-pong stores remain the client-predicted/rendered body. */
typedef struct {
    int valid;
    int eid;
    int on_ground;
    int first_update;
    double x, y, z;
    double vx, vy, vz;
    McAABB box;
    float yaw, pitch;
    float fall_distance;
    double lowest_x, lowest_y, lowest_z;
    double lowest_x1, lowest_y1, lowest_z1;
    uint64_t packet_seq;
    GmPigVehicleMoveResult last_move;
} GmPigVehicleServerState;

typedef struct {
    uint64_t seq;
    int valid;
    int eid;
    double target_x, target_y, target_z;
    float target_yaw, target_pitch;
    double x, y, z;
    double vx, vy, vz;
    McAABB box;
    float yaw, pitch;
    int on_ground;
    float fall_distance;
    int is_in_water;
    int is_in_lava;
    float health;
    int fire_ticks;
    int hurt_time;
    int hurt_resistant_time;
    int fire_resistance_ticks;
    float last_damage;
    int alive;
    uint64_t entity_seed48;
    uint64_t math_seed48;
    double client_x, client_y, client_z;
    McAABB client_box;
    float client_yaw, client_pitch;
    GmPigVehicleMoveResult move;
} GmPigVehicleMoveCheckpoint;

/* Global cursors and gamerule observed synchronously by a represented
 * EntityLivingBase.onDeath call.  The stack-owned context prevents a lethal
 * hit from retaining stale pointers across ticks. */
typedef struct {
    int do_mob_loot;
    uint64_t *math_random_seed48;
    int *next_entity_id;
} GmMobDeathContext;

/* Nullable true source for EntityDamageSourceIndirect. The potion/cloud is
 * always the immediate source; this record is the living thrower/owner used
 * for revenge, knockback, and player kill credit. */
typedef struct {
    int eid;
    int is_player;
    double x, z;
    int looting_level;
} GmMobPotionDamageOwner;

enum {
    GM_HOSTILE_LOOT_MAX = 4,
    GM_HOSTILE_LOOT_POTION_NONE = 0,
    GM_HOSTILE_LOOT_POTION_SLOWNESS = 1
};
typedef struct {
    int count;
    int item[GM_HOSTILE_LOOT_MAX];
    int quantity[GM_HOSTILE_LOOT_MAX];
    int meta[GM_HOSTILE_LOOT_MAX];
    int potion_type[GM_HOSTILE_LOOT_MAX]; /* semantic stack tag, TB_PT_* */
} GmHostileLootOutcome;

typedef struct {
    double x, y, z;
    double vx, vy, vz;
} GmTerminalParticle;

typedef struct {
    uint64_t seq;
    int eid;
    int dimension;
    int particle_id;
    int ignore_range;
    int parameter_count;
    GmTerminalParticle particles[GM_MOB_TERMINAL_PARTICLE_COUNT];
} GmMobTerminalParticles;

typedef struct {
    uint64_t seq;
    int eid;
    int dimension;
    int particle_id;
    int count;
    GmTerminalParticle particles[GM_MOB_PARTICLE_BATCH_MAX];
    int descriptor_count;
    double x, y, z;
    double offset_x, offset_y, offset_z, speed;
    int parameter_count;
    int parameters[2];
} GmMobParticleBatch;

enum {
    GM_SHEEP_MATE_NONE = 0,
    GM_SHEEP_MATE_WAITING = 1,
    GM_SHEEP_MATE_BORN = 2,
    GM_SHEEP_MATE_CANCELLED = 3,
    GM_SHEEP_MATE_NULL_CHILD = 4
};

typedef struct {
    int result;
    int delay;
    int child_eid;
    int child_slot;
    int child_type;
    int child_fleece;
    int xp_eid;
    int xp_slot;
    int xp_value;
} GmSheepMateResult;
typedef GmSheepMateResult GmAnimalMateResult;

typedef struct {
    int eid;
    int profession;
    double x, y, z;
    GmVillagerTrade trade;
} GmVillagerBirth;

typedef struct {
    int type;
    int weight;
    int nbt_tag_id;
    unsigned char default_entity_nbt;
} GmSpawnerPotential;
/* TileEntityMobSpawner live state. potential_* preserves the serialized
 * WeightedSpawnerEntity rows separately from the current SpawnData choice. */
typedef struct {
    int active;
    int dimension;
    int x, y, z;
    int entity_type;   /* EW_TYPE_* / GM_MOB_* */
    int delay;
    int min_delay, max_delay;
    int spawn_count;
    int max_nearby;
    int spawn_range;
    int entity_nbt_tag_id;
    unsigned char default_entity_nbt;
    double mob_rotation;
    double prev_mob_rotation;
    int activate_range;
    int potential_count;
    int potential_cap;
    GmSpawnerPotential *potentials;
} GmSpawnerTE;

typedef struct {
    EwStore a, b;
    int current;
    int active_dimension;
    int natural_spawning_enabled;
    signed char entity_dimension[EW_MAX_ENTITIES];
    unsigned char entity_uuid_present[EW_MAX_ENTITIES];
    int64_t entity_uuid_most[EW_MAX_ENTITIES];
    int64_t entity_uuid_least[EW_MAX_ENTITIES];
    long long seed, tick;
    int next_id;
    int player_ticks_since_last_swing;             /* EntityPlayer cooldown cursor */
    JavaRandom player_random;                      /* EntityPlayerMP Entity.rand */
    int player_xp_cooldown;                        /* EntityPlayer.xpCooldown */
    int xp_total;
    McOrb xp_orbs[GM_XP_ORBS];
    signed char orb_dimension[GM_XP_ORBS];
    unsigned char orb_uuid_present[GM_XP_ORBS];
    int64_t orb_uuid_most[GM_XP_ORBS];
    int64_t orb_uuid_least[GM_XP_ORBS];
    uint64_t orb_random_seed48[GM_XP_ORBS];
    int next_orb_id;
    McAABB xp_collision_boxes[GM_XP_ORBS];
    int xp_collision_count;
    GmXpOrbCold *xp_orb_cold;
    int xp_orb_cold_cap;
    /* AoS overflow for living entities. The verified 95-slot SoA remains the
     * allocation-free hot tick; cold rows are staged through generated,
     * exhaustive field copies when their loaded-order turn is processed. */
    GmLivingColdSlot *living_cold;
    int living_cold_count;
    int living_cold_cap;
    int living_staged_cold;
    int living_staged_cold_secondary;
    int tick_update_order[GM_MOB_UPDATE_ORDER_CAPACITY];
    int *tick_update_order_cold;
    int tick_update_order_cold_cap;
    int tick_update_order_count;
    GmMobLoadedRef loaded_order[GM_MOB_LOADED_ORDER_CAPACITY];
    GmMobLoadedRef *loaded_order_cold;
    int loaded_order_cold_cap;
    int loaded_order_count;
    unsigned int living_loaded_generation[EW_MAX_ENTITIES];
    unsigned int xp_loaded_generation[GM_XP_ORBS];
    int creeper_fuse[EW_MAX_ENTITIES];
    unsigned char creeper_powered[EW_MAX_ENTITIES];
    unsigned char hurt_aggro[EW_MAX_ENTITIES];   /* revenge target set */
    int entity_revenge_eid[EW_MAX_ENTITIES];     /* non-player living source */
    int panic_ticks[EW_MAX_ENTITIES];            /* passive revenge target lifetime (101 ticks) */
    /* Vanilla EntityAITasks state for sheep/pig/cow/chicken. Task bits and
     * hash-RNG details stay private to mob_live.c; these are per-entity goal,
     * navigator, look-helper, and sheep eat-grass fields. */
    unsigned int passive_tasks[EW_MAX_ENTITIES];
    int passive_task_tick[EW_MAX_ENTITIES];
    int passive_watch_time[EW_MAX_ENTITIES];
    int passive_idle_time[EW_MAX_ENTITIES];
    int passive_eat_time[EW_MAX_ENTITIES];
    double passive_idle_x[EW_MAX_ENTITIES];
    double passive_idle_z[EW_MAX_ENTITIES];
    double passive_nav_speed[EW_MAX_ENTITIES];
    unsigned char llama_nav_goal_valid[EW_MAX_ENTITIES];
    unsigned char llama_nav_goal_pending[EW_MAX_ENTITIES];
    double llama_nav_goal_x[EW_MAX_ENTITIES];
    double llama_nav_goal_y[EW_MAX_ENTITIES];
    double llama_nav_goal_z[EW_MAX_ENTITIES];
    float passive_move_forward[EW_MAX_ENTITIES];
    float passive_move_strafe[EW_MAX_ENTITIES];
    float passive_head_yaw[EW_MAX_ENTITIES];
    float passive_head_pitch[EW_MAX_ENTITIES];
    int body_rotation_tick_counter[EW_MAX_ENTITIES];
    float body_prev_head_yaw[EW_MAX_ENTITIES];
    unsigned char passive_sheared[EW_MAX_ENTITIES];
    int fire_ticks[EW_MAX_ENTITIES]; /* signed Entity fire counter; ordinary
                                      * Witch ON_FIRE/lava phases are exact */
    int arrow_count[EW_MAX_ENTITIES]; /* EntityLivingBase arrows embedded */
    int despawn_ticks[EW_MAX_ENTITIES];          /* ticks spent >32 blocks from player */
    unsigned char persistence_required[EW_MAX_ENTITIES];
    int entity_portal_cooldown[EW_MAX_ENTITIES]; /* Entity.timeUntilPortal */
    unsigned char entity_left_handed[EW_MAX_ENTITIES];
    unsigned char entity_can_pick_up_loot[EW_MAX_ENTITIES];
    ICStack entity_mainhand[EW_MAX_ENTITIES];
    int entity_vehicle_eid[EW_MAX_ENTITIES];
    int entity_rider_eid[EW_MAX_ENTITIES];
    unsigned char witch_left_handed[EW_MAX_ENTITIES];
    /* EntityAISkeletonRiders-owned equipment and mount graph. Ordinary
     * skeleton equipment remains an AI-01 tail; these fields are exact for
     * the four trap riders and survive native checkpoints. */
    unsigned char skeleton_trap_left_handed[EW_MAX_ENTITIES];
    unsigned char skeleton_trap_can_pick_up_loot[EW_MAX_ENTITIES];
    unsigned char skeleton_trap_first_tick[EW_MAX_ENTITIES];
    unsigned char skeleton_trap_target_player[EW_MAX_ENTITIES];
    int skeleton_trap_target_eid[EW_MAX_ENTITIES];
    int skeleton_trap_revenge_eid[EW_MAX_ENTITIES];
    unsigned char skeleton_trap_revenge_running[EW_MAX_ENTITIES];
    unsigned char skeleton_trap_bow_running[EW_MAX_ENTITIES];
    unsigned char skeleton_trap_hand_active[EW_MAX_ENTITIES];
    unsigned char skeleton_trap_item_use_count[EW_MAX_ENTITIES];
    unsigned char skeleton_trap_move_action[EW_MAX_ENTITIES];
    unsigned char skeleton_trap_move_terminal_ticks[EW_MAX_ENTITIES];
    unsigned char skeleton_trap_move_wait_latched[EW_MAX_ENTITIES];
    float skeleton_trap_move_forward[EW_MAX_ENTITIES];
    float skeleton_trap_move_strafe[EW_MAX_ENTITIES];
    double skeleton_trap_move_x[EW_MAX_ENTITIES];
    double skeleton_trap_move_y[EW_MAX_ENTITIES];
    double skeleton_trap_move_z[EW_MAX_ENTITIES];
    double skeleton_trap_move_speed[EW_MAX_ENTITIES];
    int skeleton_trap_vehicle_eid[EW_MAX_ENTITIES];
    int skeleton_trap_rider_eid[EW_MAX_ENTITIES];
    ICStack skeleton_trap_mainhand[EW_MAX_ENTITIES];
    ICStack skeleton_trap_head[EW_MAX_ENTITIES];
    int skeleton_trap_shot_head, skeleton_trap_shot_count;
    int skeleton_trap_shots_cap;
    GmSkeletonTrapShot *skeleton_trap_shots;
    double entity_random_follow_range_bonus[EW_MAX_ENTITIES];
    unsigned char witch_drinking[EW_MAX_ENTITIES];
    unsigned char witch_potion[EW_MAX_ENTITIES];
    int witch_attack_timer[EW_MAX_ENTITIES];
    float witch_drink_pitch[EW_MAX_ENTITIES];
    int witch_see_time[EW_MAX_ENTITIES];
    unsigned char witch_shots_pending[EW_MAX_ENTITIES];
    unsigned char witch_shot_head[EW_MAX_ENTITIES];
    GmWitchShot witch_shots[EW_MAX_ENTITIES][GM_WITCH_SHOT_QUEUE];
    int anger[EW_MAX_ENTITIES];                  /* pigman angerLevel ticks */
    unsigned char blaze_attack_step[EW_MAX_ENTITIES]; /* AIFireballAttack 0..4 */
    unsigned char blaze_attacking[EW_MAX_ENTITIES];
    unsigned char blaze_charged[EW_MAX_ENTITIES];     /* EntityBlaze.ON_FIRE bit */
    unsigned char blaze_shots_pending[EW_MAX_ENTITIES];
    unsigned char blaze_shot_head[EW_MAX_ENTITIES];
    GmBlazeShot blaze_shots[EW_MAX_ENTITIES][GM_BLAZE_SHOT_QUEUE];
    int evoker_spell_ticks[EW_MAX_ENTITIES];
    int evoker_spell_id[EW_MAX_ENTITIES];
    int evoker_spell_warmup[EW_MAX_ENTITIES];
    int evoker_summon_next[EW_MAX_ENTITIES];
    int evoker_attack_next[EW_MAX_ENTITIES];
    int evoker_wololo_next[EW_MAX_ENTITIES];
    int evoker_wololo_target[EW_MAX_ENTITIES];
    int vex_owner_eid[EW_MAX_ENTITIES];
    int vex_bound_x[EW_MAX_ENTITIES];
    int vex_bound_y[EW_MAX_ENTITIES];
    int vex_bound_z[EW_MAX_ENTITIES];
    int vex_life_ticks[EW_MAX_ENTITIES];
    unsigned char vex_limited_life[EW_MAX_ENTITIES];
    unsigned char vex_charging[EW_MAX_ENTITIES];
    int guardian_attack_counter[EW_MAX_ENTITIES];
    int guardian_target_eid[EW_MAX_ENTITIES];
    unsigned char guardian_moving[EW_MAX_ENTITIES];
    float guardian_tail_animation[EW_MAX_ENTITIES];
    float guardian_tail_speed[EW_MAX_ENTITIES];
    float guardian_spikes_animation[EW_MAX_ENTITIES];
    int guardian_curse_pending_eid;
    JavaGaussianRandom entity_random[EW_MAX_ENTITIES];
    JavaGaussianRandom entity_server_random[EW_MAX_ENTITIES];
    float entity_pitch[EW_MAX_ENTITIES];          /* Entity.rotationPitch */
    int entity_ticks_existed[EW_MAX_ENTITIES];
    int endermite_lifetime[EW_MAX_ENTITIES];
    unsigned char endermite_player_spawned[EW_MAX_ENTITIES];
    unsigned char bat_hanging[EW_MAX_ENTITIES];
    unsigned char bat_spawn_position_valid[EW_MAX_ENTITIES];
    int bat_spawn_x[EW_MAX_ENTITIES];
    int bat_spawn_y[EW_MAX_ENTITIES];
    int bat_spawn_z[EW_MAX_ENTITIES];
    unsigned char snowman_pumpkin[EW_MAX_ENTITIES];
    int snowman_target_eid[EW_MAX_ENTITIES];
    int snowman_target_unseen_ticks[EW_MAX_ENTITIES];
    int snowman_ranged_attack_time[EW_MAX_ENTITIES];
    int snowman_ranged_see_time[EW_MAX_ENTITIES];
    unsigned int snowman_target_task_tick[EW_MAX_ENTITIES];
    unsigned char controlled_mob_griefing;
    unsigned char do_entity_drops;
    unsigned char player_creative;
    unsigned char player_disable_damage;
    float squid_pitch[EW_MAX_ENTITIES];
    float squid_prev_pitch[EW_MAX_ENTITIES];
    float squid_yaw[EW_MAX_ENTITIES];
    float squid_prev_yaw[EW_MAX_ENTITIES];
    float squid_rotation[EW_MAX_ENTITIES];
    float squid_prev_rotation[EW_MAX_ENTITIES];
    float squid_tentacle_angle[EW_MAX_ENTITIES];
    float squid_last_tentacle_angle[EW_MAX_ENTITIES];
    float squid_random_motion_speed[EW_MAX_ENTITIES];
    float squid_rotation_velocity[EW_MAX_ENTITIES];
    float squid_rotate_speed[EW_MAX_ENTITIES];
    float squid_random_motion_x[EW_MAX_ENTITIES];
    float squid_random_motion_y[EW_MAX_ENTITIES];
    float squid_random_motion_z[EW_MAX_ENTITIES];
    float squid_render_yaw_offset[EW_MAX_ENTITIES];
    int entity_age[EW_MAX_ENTITIES];
    int entity_living_sound_time[EW_MAX_ENTITIES];
    int entity_server_living_sound_time[EW_MAX_ENTITIES];
    double entity_last_tick_x[EW_MAX_ENTITIES];
    double entity_last_tick_y[EW_MAX_ENTITIES];
    double entity_last_tick_z[EW_MAX_ENTITIES];
    double entity_prev_x[EW_MAX_ENTITIES];
    double entity_prev_y[EW_MAX_ENTITIES];
    double entity_prev_z[EW_MAX_ENTITIES];
    double entity_box_min_x[EW_MAX_ENTITIES];
    double entity_box_min_y[EW_MAX_ENTITIES];
    double entity_box_min_z[EW_MAX_ENTITIES];
    double entity_box_max_x[EW_MAX_ENTITIES];
    double entity_box_max_y[EW_MAX_ENTITIES];
    double entity_box_max_z[EW_MAX_ENTITIES];
    unsigned char entity_box_valid[EW_MAX_ENTITIES];
    float entity_fall_distance[EW_MAX_ENTITIES];
    float entity_server_fall_distance[EW_MAX_ENTITIES];
    unsigned char entity_collided_horizontal[EW_MAX_ENTITIES];
    unsigned char entity_collided_vertical[EW_MAX_ENTITIES];
    unsigned char entity_in_water[EW_MAX_ENTITIES];
    unsigned char entity_in_lava[EW_MAX_ENTITIES];
    unsigned char entity_server_in_water[EW_MAX_ENTITIES];
    unsigned char entity_server_in_lava[EW_MAX_ENTITIES];
    int entity_server_fire_resistance_ticks[EW_MAX_ENTITIES];
    unsigned char entity_in_web[EW_MAX_ENTITIES];
    int blaze_height_offset_update_time[EW_MAX_ENTITIES];
    float blaze_height_offset[EW_MAX_ENTITIES];
    unsigned char size[EW_MAX_ENTITIES];         /* slime/magma size 1/2/4 */
    unsigned char boat_variant[EW_MAX_ENTITIES]; /* EntityBoat.Type 0..5 */
    signed char boat_status[EW_MAX_ENTITIES];    /* EntityBoat.Status; -1 before first tick */
    signed char boat_previous_status[EW_MAX_ENTITIES];
    float boat_momentum[EW_MAX_ENTITIES];
    float boat_out_of_control[EW_MAX_ENTITIES];
    float boat_delta_rotation[EW_MAX_ENTITIES];
    float boat_glide[EW_MAX_ENTITIES];
    float boat_paddle_position[EW_MAX_ENTITIES][2];
    unsigned char boat_paddle_state[EW_MAX_ENTITIES][2];
    double boat_water_level[EW_MAX_ENTITIES];
    double boat_last_yd[EW_MAX_ENTITIES];
    int boat_lerp_steps[EW_MAX_ENTITIES];
    double boat_lerp_x[EW_MAX_ENTITIES];
    double boat_lerp_y[EW_MAX_ENTITIES];
    double boat_lerp_z[EW_MAX_ENTITIES];
    double boat_lerp_yaw[EW_MAX_ENTITIES];
    double boat_lerp_pitch[EW_MAX_ENTITIES];
    unsigned char boat_client_remote[EW_MAX_ENTITIES];
    int boat_passenger_eid[EW_MAX_ENTITIES][2];
    signed char boat_player_passenger_index[EW_MAX_ENTITIES];
    int boat_portal_counter[EW_MAX_ENTITIES];
    unsigned char boat_in_portal[EW_MAX_ENTITIES];
    unsigned char boat_last_portal_valid[EW_MAX_ENTITIES];
    int boat_last_portal_x[EW_MAX_ENTITIES];
    int boat_last_portal_y[EW_MAX_ENTITIES];
    int boat_last_portal_z[EW_MAX_ENTITIES];
    double boat_last_portal_vec_x[EW_MAX_ENTITIES];
    double boat_last_portal_vec_y[EW_MAX_ENTITIES];
    signed char boat_teleport_direction[EW_MAX_ENTITIES];
    unsigned char sheep_data[EW_MAX_ENTITIES];   /* fleece 0..15 | sheared 0x10 */
    unsigned char villager_profession[EW_MAX_ENTITIES]; /* 0..5 */
    int villager_random_tick_divider[EW_MAX_ENTITIES];
    unsigned int villager_ai_tick_count[EW_MAX_ENTITIES];
    int villager_mate_eid[EW_MAX_ENTITIES];
    int villager_mating_timeout[EW_MAX_ENTITIES];
    unsigned char villager_mate_active[EW_MAX_ENTITIES];
    unsigned char villager_mating[EW_MAX_ENTITIES];
    unsigned char villager_follow_golem_active[EW_MAX_ENTITIES];
    unsigned char villager_took_golem_rose[EW_MAX_ENTITIES];
    int villager_follow_golem_eid[EW_MAX_ENTITIES];
    int villager_take_golem_rose_tick[EW_MAX_ENTITIES];
    unsigned char villager_front_door_valid[EW_MAX_ENTITIES];
    int villager_front_door_village[EW_MAX_ENTITIES];
    int villager_front_door_index[EW_MAX_ENTITIES];
    int villager_front_door_x[EW_MAX_ENTITIES];
    int villager_front_door_y[EW_MAX_ENTITIES];
    int villager_front_door_z[EW_MAX_ENTITIES];
    int villager_front_door_inside_dx[EW_MAX_ENTITIES];
    int villager_front_door_inside_dz[EW_MAX_ENTITIES];
    unsigned char villager_front_door_detached[EW_MAX_ENTITIES];
    unsigned char villager_restrict_door_active[EW_MAX_ENTITIES];
    unsigned char villager_door_restriction_pending[EW_MAX_ENTITIES];
    unsigned char villager_enter_doors[EW_MAX_ENTITIES];
    unsigned char villager_break_doors[EW_MAX_ENTITIES];
    unsigned char villager_open_door_active[EW_MAX_ENTITIES];
    unsigned char villager_stopped_door_interaction[EW_MAX_ENTITIES];
    int villager_close_door_timer[EW_MAX_ENTITIES];
    int villager_open_door_x[EW_MAX_ENTITIES];
    int villager_open_door_y[EW_MAX_ENTITIES];
    int villager_open_door_z[EW_MAX_ENTITIES];
    float villager_door_start_x[EW_MAX_ENTITIES];
    float villager_door_start_z[EW_MAX_ENTITIES];
    unsigned char villager_indoors_door_valid[EW_MAX_ENTITIES];
    int villager_indoors_door_x[EW_MAX_ENTITIES];
    int villager_indoors_door_y[EW_MAX_ENTITIES];
    int villager_indoors_door_z[EW_MAX_ENTITIES];
    int villager_indoors_inside_dx[EW_MAX_ENTITIES];
    int villager_indoors_inside_dz[EW_MAX_ENTITIES];
    unsigned char villager_move_indoors_active[EW_MAX_ENTITIES];
    int villager_last_inside_x[EW_MAX_ENTITIES];
    int villager_last_inside_z[EW_MAX_ENTITIES];
    unsigned char villager_home_valid[EW_MAX_ENTITIES];
    int villager_home_x[EW_MAX_ENTITIES];
    int villager_home_y[EW_MAX_ENTITIES];
    int villager_home_z[EW_MAX_ENTITIES];
    int villager_home_radius[EW_MAX_ENTITIES];
    unsigned char villager_move_restriction_active[EW_MAX_ENTITIES];
    unsigned char villager_avoid_active[EW_MAX_ENTITIES];
    int villager_avoid_target_eid[EW_MAX_ENTITIES];
    float villager_avoid_speed[EW_MAX_ENTITIES];
    unsigned char villager_play_active[EW_MAX_ENTITIES];
    unsigned char villager_playing[EW_MAX_ENTITIES];
    int villager_play_target_eid[EW_MAX_ENTITIES];
    int villager_play_time[EW_MAX_ENTITIES];
    unsigned char villager_harvest_active[EW_MAX_ENTITIES];
    unsigned char villager_harvest_has_item[EW_MAX_ENTITIES];
    unsigned char villager_harvest_wants_reap[EW_MAX_ENTITIES];
    int villager_harvest_current_task[EW_MAX_ENTITIES];
    int villager_harvest_run_delay[EW_MAX_ENTITIES];
    int villager_harvest_timeout[EW_MAX_ENTITIES];
    int villager_harvest_max_stay[EW_MAX_ENTITIES];
    int villager_harvest_x[EW_MAX_ENTITIES];
    int villager_harvest_y[EW_MAX_ENTITIES];
    int villager_harvest_z[EW_MAX_ENTITIES];
    unsigned char villager_harvest_above[EW_MAX_ENTITIES];
    unsigned char villager_interact_active[EW_MAX_ENTITIES];
    int villager_interact_target_eid[EW_MAX_ENTITIES];
    int villager_interact_look_time[EW_MAX_ENTITIES];
    int villager_interaction_delay[EW_MAX_ENTITIES];
    unsigned char villager_swim_active[EW_MAX_ENTITIES];
    unsigned char villager_trade_active[EW_MAX_ENTITIES];
    unsigned char villager_trade_look_active[EW_MAX_ENTITIES];
    int villager_trade_look_time[EW_MAX_ENTITIES];
    unsigned char villager_watch_player_active[EW_MAX_ENTITIES];
    int villager_watch_player_time[EW_MAX_ENTITIES];
    unsigned char villager_watch_living_active[EW_MAX_ENTITIES];
    int villager_watch_living_target_eid[EW_MAX_ENTITIES];
    int villager_watch_living_time[EW_MAX_ENTITIES];
    unsigned char villager_wander_active[EW_MAX_ENTITIES];
    int active_villager_customer_eid;
    unsigned char villager_willing[EW_MAX_ENTITIES];
    unsigned char villager_village_valid[EW_MAX_ENTITIES];
    int villager_village_index[EW_MAX_ENTITIES];
    unsigned char villager_village_mating[EW_MAX_ENTITIES];
    int villager_village_doors[EW_MAX_ENTITIES];
    int villager_village_population[EW_MAX_ENTITIES];
    ICStack villager_inventory[EW_MAX_ENTITIES][8];
    unsigned char villager_collection_pending[EW_MAX_ENTITIES];
    int villager_collection_x[EW_MAX_ENTITIES];
    int villager_collection_y[EW_MAX_ENTITIES];
    int villager_collection_z[EW_MAX_ENTITIES];
    GmVillagerBirth villager_births[EW_MAX_ENTITIES];
    int villager_birth_count;
    GmVillagerHarmEvent villager_harm_events[EW_MAX_ENTITIES];
    int villager_harm_count;
    unsigned char golem_player_created[EW_MAX_ENTITIES];
    int golem_home_check_timer[EW_MAX_ENTITIES];
    int golem_attack_timer[EW_MAX_ENTITIES];
    int golem_hold_rose_tick[EW_MAX_ENTITIES];
    int golem_target_eid[EW_MAX_ENTITIES];
    /* Borrowed for the duration of a runtime tick. Static Block.RANDOM is a
     * separate process-global cursor from World.rand. */
    uint64_t *active_block_random_seed48;
    /* Constructor globals borrowed only while the live entity pass is in
     * flight. They are cleared before checkpoints and never own storage. */
    uint64_t *active_entity_seed_generator_seed48;
    uint64_t *active_server_uuid_random_seed48;
    unsigned char golem_target_player[EW_MAX_ENTITIES];
    unsigned char golem_target_village_defense[EW_MAX_ENTITIES];
    int golem_village_index[EW_MAX_ENTITIES];
    int golem_village_aggressor_eid[EW_MAX_ENTITIES];
    unsigned char golem_village_low_reputation_player[EW_MAX_ENTITIES];
    unsigned int golem_target_task_tick[EW_MAX_ENTITIES];
    int golem_path_delay[EW_MAX_ENTITIES];
    double golem_path_target_x[EW_MAX_ENTITIES];
    double golem_path_target_y[EW_MAX_ENTITIES];
    double golem_path_target_z[EW_MAX_ENTITIES];
    float golem_limb_swing[EW_MAX_ENTITIES];
    float golem_limb_amount[EW_MAX_ENTITIES];
    float golem_step_distance[EW_MAX_ENTITIES];
    int golem_next_step_distance[EW_MAX_ENTITIES];
    int zombie_villager_conversion_time[EW_MAX_ENTITIES];
    unsigned char zombie_villager_world_event_pending[EW_MAX_ENTITIES];
    int zombie_villager_world_event_x[EW_MAX_ENTITIES];
    int zombie_villager_world_event_y[EW_MAX_ENTITIES];
    int zombie_villager_world_event_z[EW_MAX_ENTITIES];
    unsigned char pig_saddled[EW_MAX_ENTITIES];  /* EntityPig.SADDLED */
    unsigned char pig_boosting[EW_MAX_ENTITIES];
    int pig_boost_time[EW_MAX_ENTITIES];
    int pig_boost_total[EW_MAX_ENTITIES];
    float pig_pitch[EW_MAX_ENTITIES];
    float pig_prev_yaw[EW_MAX_ENTITIES];
    float pig_render_yaw[EW_MAX_ENTITIES];
    float pig_head_yaw[EW_MAX_ENTITIES];
    float pig_step_height[EW_MAX_ENTITIES];
    float pig_jump_factor[EW_MAX_ENTITIES];
    float pig_ai_speed[EW_MAX_ENTITIES];
    float pig_prev_limb_amount[EW_MAX_ENTITIES];
    float pig_limb_amount[EW_MAX_ENTITIES];
    float pig_limb_swing[EW_MAX_ENTITIES];
    /* AbstractHorse STATUS, attributes, animation, subclass, and inventory.
     * Slot 0 is saddle, slot 1 armor, 2..16 donkey/mule chest storage. */
    unsigned char horse_status[EW_MAX_ENTITIES];
    int horse_variant[EW_MAX_ENTITIES];
    unsigned char horse_armor[EW_MAX_ENTITIES];
    unsigned char horse_chested[EW_MAX_ENTITIES];
    int horse_temper[EW_MAX_ENTITIES];
    unsigned char horse_owner_present[EW_MAX_ENTITIES];
    uint64_t horse_owner_uuid_most[EW_MAX_ENTITIES];
    uint64_t horse_owner_uuid_least[EW_MAX_ENTITIES];
    unsigned char horse_crazy_active[EW_MAX_ENTITIES];
    double horse_max_health[EW_MAX_ENTITIES];
    double horse_movement_speed[EW_MAX_ENTITIES];
    double horse_jump_strength[EW_MAX_ENTITIES];
    int horse_eating_counter[EW_MAX_ENTITIES];
    int horse_open_mouth_counter[EW_MAX_ENTITIES];
    int horse_jump_rearing_counter[EW_MAX_ENTITIES];
    int horse_tail_counter[EW_MAX_ENTITIES];
    int horse_sprint_counter[EW_MAX_ENTITIES];
    int horse_gallop_time[EW_MAX_ENTITIES];
    int horse_trap_time[EW_MAX_ENTITIES];
    unsigned char horse_trap[EW_MAX_ENTITIES];
    unsigned char horse_jumping[EW_MAX_ENTITIES];
    unsigned char horse_allow_stand_sliding[EW_MAX_ENTITIES];
    float horse_jump_power[EW_MAX_ENTITIES];
    float horse_head_lean[EW_MAX_ENTITIES];
    float horse_prev_head_lean[EW_MAX_ENTITIES];
    float horse_rearing_amount[EW_MAX_ENTITIES];
    float horse_prev_rearing_amount[EW_MAX_ENTITIES];
    float horse_mouth_openness[EW_MAX_ENTITIES];
    float horse_prev_mouth_openness[EW_MAX_ENTITIES];
    float horse_prev_limb_amount[EW_MAX_ENTITIES];
    float horse_limb_amount[EW_MAX_ENTITIES];
    float horse_limb_swing[EW_MAX_ENTITIES];
    ICStack horse_inventory[EW_MAX_ENTITIES][GM_HORSE_INVENTORY_SLOTS];
    unsigned char llama_strength[EW_MAX_ENTITIES];
    signed char llama_decor[EW_MAX_ENTITIES];
    unsigned char llama_variant[EW_MAX_ENTITIES];
    unsigned char llama_did_spit[EW_MAX_ENTITIES];
    /* EntityLiving leash state is shared by every represented living slot.
     * Historical field names are retained to keep checkpoint layout stable. */
    unsigned char llama_leashed[EW_MAX_ENTITIES];
    unsigned char llama_leash_holder_kind[EW_MAX_ENTITIES];
    int llama_leash_holder_eid[EW_MAX_ENTITIES];
    double llama_leash_holder_x[EW_MAX_ENTITIES];
    double llama_leash_holder_y[EW_MAX_ENTITIES];
    double llama_leash_holder_z[EW_MAX_ENTITIES];
    unsigned char llama_leash_pending[EW_MAX_ENTITIES];
    int llama_leash_pending_x[EW_MAX_ENTITIES];
    int llama_leash_pending_y[EW_MAX_ENTITIES];
    int llama_leash_pending_z[EW_MAX_ENTITIES];
    int llama_caravan_head_eid[EW_MAX_ENTITIES];
    int llama_caravan_tail_eid[EW_MAX_ENTITIES];
    double llama_caravan_speed[EW_MAX_ENTITIES];
    int llama_caravan_dist_counter[EW_MAX_ENTITIES];
    int llama_attack_target_eid[EW_MAX_ENTITIES];
    unsigned char llama_attack_target_kind[EW_MAX_ENTITIES];
    int llama_ranged_attack_time[EW_MAX_ENTITIES];
    int llama_ranged_see_time[EW_MAX_ENTITIES];
    int llama_follow_parent_eid[EW_MAX_ENTITIES];
    int llama_follow_parent_delay[EW_MAX_ENTITIES];
    float llama_step_distance[EW_MAX_ENTITIES];
    int llama_next_step_distance[EW_MAX_ENTITIES];
    int growing_age[EW_MAX_ENTITIES];             /* EntityAgeable; child when < 0 */
    unsigned char polar_standing[EW_MAX_ENTITIES];
    float polar_stand_animation0[EW_MAX_ENTITIES];
    float polar_stand_animation[EW_MAX_ENTITIES];
    int polar_warning_sound_ticks[EW_MAX_ENTITIES];
    unsigned char polar_player_target[EW_MAX_ENTITIES];
    unsigned char rabbit_type[EW_MAX_ENTITIES];
    int rabbit_jump_ticks[EW_MAX_ENTITIES];
    int rabbit_jump_duration[EW_MAX_ENTITIES];
    unsigned char rabbit_was_on_ground[EW_MAX_ENTITIES];
    int rabbit_move_duration[EW_MAX_ENTITIES];
    int rabbit_carrot_ticks[EW_MAX_ENTITIES];
    double rabbit_move_speed[EW_MAX_ENTITIES];
    unsigned char rabbit_raid_valid[EW_MAX_ENTITIES];
    int rabbit_raid_x[EW_MAX_ENTITIES];
    int rabbit_raid_y[EW_MAX_ENTITIES];
    int rabbit_raid_z[EW_MAX_ENTITIES];
    int chicken_time_until_next_egg[EW_MAX_ENTITIES];
    float chicken_wing_rotation[EW_MAX_ENTITIES];
    float chicken_dest_pos[EW_MAX_ENTITIES];
    float chicken_old_flap_speed[EW_MAX_ENTITIES];
    float chicken_old_flap[EW_MAX_ENTITIES];
    float chicken_wing_rot_delta[EW_MAX_ENTITIES];
    unsigned char chicken_jockey[EW_MAX_ENTITIES];
    unsigned char tameable_tamed[EW_MAX_ENTITIES];
    unsigned char tameable_sitting[EW_MAX_ENTITIES];
    unsigned char tameable_sit_requested[EW_MAX_ENTITIES];
    unsigned char tameable_owner[EW_MAX_ENTITIES];
    unsigned char tameable_variant[EW_MAX_ENTITIES]; /* wolf collar dye or cat skin */
    unsigned char wolf_angry[EW_MAX_ENTITIES];
    unsigned char tameable_following[EW_MAX_ENTITIES];
    int tameable_follow_recalc[EW_MAX_ENTITIES];
    int sheep_in_love[EW_MAX_ENTITIES];           /* EntityAnimal.inLove */
    int sheep_forced_age[EW_MAX_ENTITIES];        /* EntityAgeable.forcedAge */
    int sheep_forced_age_timer[EW_MAX_ENTITIES];  /* client happy-particle timer */
    unsigned char sheep_bred_by_player[EW_MAX_ENTITIES];
    int sheep_mate_eid[EW_MAX_ENTITIES];
    int sheep_mate_delay[EW_MAX_ENTITIES];
    unsigned char sheep_mate_active[EW_MAX_ENTITIES];
    int sheep_eat_timer[EW_MAX_ENTITIES];         /* EntityAIEatGrass 40..0 */
    unsigned int sheep_ai_tick_count[EW_MAX_ENTITIES]; /* wrapping goal scheduler */
    unsigned char sheep_world_event_pending[EW_MAX_ENTITIES];
    int sheep_world_event_x[EW_MAX_ENTITIES];
    int sheep_world_event_y[EW_MAX_ENTITIES];
    int sheep_world_event_z[EW_MAX_ENTITIES];
    int sheep_world_event_data[EW_MAX_ENTITIES];
    JavaGaussianRandom animal_child_random_queue[EW_MAX_ENTITIES];
    int animal_child_chicken_egg_queue[EW_MAX_ENTITIES];
    unsigned char animal_child_state_head;
    unsigned char animal_child_state_count;
    float squish_amount[EW_MAX_ENTITIES];        /* EntitySlime.squishAmount */
    float squish_factor[EW_MAX_ENTITIES];        /* EntitySlime.squishFactor */
    float prev_squish_factor[EW_MAX_ENTITIES];   /* EntitySlime.prevSquishFactor */
    unsigned char was_on_ground[EW_MAX_ENTITIES]; /* EntitySlime.wasOnGround */
    int jump_delay[EW_MAX_ENTITIES];             /* slime/magma jump cooldown */
    int charge[EW_MAX_ENTITIES];                 /* ghast charge (-40..20); blaze AIFireballAttack.attackStep */
    unsigned char blaze_on_fire[EW_MAX_ENTITIES]; /* EntityBlaze ON_FIRE / isCharged display bit */
    float boat_damage[EW_MAX_ENTITIES];          /* EntityBoat DAMAGE_TAKEN */
    unsigned char controlled_no_ai[EW_MAX_ENTITIES]; /* locked oracle fixture */
    unsigned char controlled_block_collisions[EW_MAX_ENTITIES];
    int entity_hurt_resistant[EW_MAX_ENTITIES];
    int entity_hurt_time[EW_MAX_ENTITIES];
    int entity_max_hurt_time[EW_MAX_ENTITIES];
    float entity_attacked_yaw[EW_MAX_ENTITIES];
    float entity_limb_swing_amount[EW_MAX_ENTITIES];
    unsigned char entity_velocity_changed[EW_MAX_ENTITIES];
    int entity_death_time[EW_MAX_ENTITIES];
    unsigned char entity_dead[EW_MAX_ENTITIES]; /* EntityLivingBase.dead */
    float entity_last_damage[EW_MAX_ENTITIES];
    int entity_recently_hit[EW_MAX_ENTITIES];
    unsigned char entity_attacking_player[EW_MAX_ENTITIES];
    unsigned char entity_effect_count[EW_MAX_ENTITIES];
    unsigned char entity_fire_resistance_this_tick[EW_MAX_ENTITIES];
    float entity_absorption[EW_MAX_ENTITIES];
    int entity_air[EW_MAX_ENTITIES];
    PtMobEffect entity_effects[EW_MAX_ENTITIES][GM_MOB_EFFECT_CAPACITY];
    /* bit 0 ambient, bit 1 hidden particles. Hidden is inverted so legacy
     * zero-filled producers retain vanilla's visible, non-ambient default. */
    unsigned char entity_effect_flags[EW_MAX_ENTITIES]
        [GM_MOB_EFFECT_CAPACITY];
    int boat_ride_eid;                               /* stable EID, or -1 */
    int pig_ride_eid;                                /* stable EID, or -1 */
    int horse_ride_eid;                              /* stable EID, or -1 */
    uint64_t represented_player_uuid_most;
    uint64_t represented_player_uuid_least;
    unsigned char represented_player_primary_right;
    float horse_input_forward;                   /* current passenger input */
    float horse_input_strafe;
    GmSpawnerTE *spawners;
    int spawners_cap;
    int player_hurt_resistant;                    /* EntityLivingBase.hurtResistantTime */
    int player_hurt_time;                         /* EntityLivingBase.hurtTime */
    int player_max_hurt_time;                     /* EntityLivingBase.maxHurtTime */
    float player_last_damage;                     /* EntityLivingBase.lastDamage */
    int player_entity_age;                        /* EntityLivingBase.entityAge */
    float player_attacked_yaw;                    /* EntityLivingBase.attackedAtYaw */
    float player_limb_swing_amount;               /* EntityLivingBase.limbSwingAmount */
    unsigned char player_velocity_changed;        /* Entity.velocityChanged */
    int player_recently_hit;                       /* EntityLivingBase.recentlyHit */
    unsigned char player_attacking_player;         /* represented player attribution */
    int player_revenge_eid;                        /* nullable revenge target id */
    int player_revenge_ticks;                      /* ticks until base target expiry */
    unsigned char player_revenge_present;
    unsigned char player_revenge_is_player;
    int player_resistance_amplifier;               /* -1 inactive; MobEffects 11 */
    float player_absorption;                       /* EntityLivingBase gold hearts */
    int player_wither_ticks;                      /* PotionEffect(WITHER, 200, 0) */
    int player_hunger_ticks;                      /* Husk PotionEffect(HUNGER, 140*(int)localDifficulty, 0) */
    unsigned int player_potion_flags;             /* GM_PLAYER_POTION_* target state */
    int explosion_pending;
    double explosion_x, explosion_y, explosion_z;
    /* Pending fireball spawn consumed by runtime: 0=none, 3=small (blaze), 5=large (ghast). */
    int fireball_pending;
    double fireball_x, fireball_y, fireball_z;
    double fireball_vx, fireball_vy, fireball_vz;
    GmEvokerFang *evoker_fangs;
    int evoker_fang_count, evoker_fangs_cap;
    GmLlamaSpit *llama_spits;
    int llama_spit_count, llama_spits_cap;
    GmSnowmanShot *snowman_shots;
    int snowman_shot_count, snowman_shots_cap;
    /* Cold-growable causal streams. Worlds without a producer allocate
     * nothing; dense ticks retain every represented event in exact order. */
    GmMobEvent *events;
    int event_head, event_count, events_cap;
    uint64_t event_next_seq, event_dropped;
    /* One atomic 20-particle batch per represented terminal living slot. */
    GmMobTerminalParticles *terminal_particles;
    int terminal_particle_head, terminal_particle_count;
    int terminal_particles_cap;
    uint64_t terminal_particle_next_seq, terminal_particle_dropped;
    GmMobParticleBatch *particle_batches;
    int particle_batch_head, particle_batch_count;
    int particle_batches_cap;
    uint64_t particle_batch_next_seq, particle_batch_dropped;
    uint64_t sheep_birth_dropped, sheep_breed_xp_dropped;
    GmPigPacketContactCheckpoint pig_packet_contact_checkpoint;
    GmPigVehicleServerState pig_vehicle_server;
    GmPigVehicleMoveCheckpoint pig_vehicle_move_checkpoint;
    /* Effect-only EntityLightningBolt constructed by the trap AI. The
     * runtime drains this after the living update without advancing the
     * loaded-entity ID cursor (weather effects own a separate list). */
    int skeleton_trap_lightning_pending;
    int skeleton_trap_lightning_eid;
    int skeleton_trap_lightning_living_time;
    long long skeleton_trap_lightning_bolt_vertex;
    uint64_t skeleton_trap_lightning_random_seed48;
    double skeleton_trap_lightning_x;
    double skeleton_trap_lightning_y;
    double skeleton_trap_lightning_z;
    /* Cold ridden-pig workspaces. Collectors overwrite [0, count), so init can
     * deliberately leave this trailing storage untouched. */
    McAABB pig_collision_scratch[GM_PIG_COLLISION_BOXES];
    int pig_block_contact_scratch[GM_PIG_COLLISION_BOXES][4];
} GmMobLive;

#include "game/living_cold_slot.generated.h"

/* Product type aliases matching EW_TYPE_* / entity_render ER_TYPE_*. */
enum {
    GM_MOB_ZOMBIE = EW_TYPE_ZOMBIE,
    GM_MOB_SKELETON = EW_TYPE_SKELETON,
    GM_MOB_CREEPER = EW_TYPE_CREEPER,
    GM_MOB_SPIDER = EW_TYPE_SPIDER,
    GM_MOB_ENDERMAN = EW_TYPE_ENDERMAN,
    GM_MOB_BLAZE = EW_TYPE_BLAZE,
    GM_MOB_SHEEP = EW_TYPE_SHEEP,
    GM_MOB_PIG = EW_TYPE_PIG,
    GM_MOB_COW = EW_TYPE_COW,
    GM_MOB_CHICKEN = EW_TYPE_CHICKEN,
    GM_MOB_PIGMAN = EW_TYPE_PIGMAN,
    GM_MOB_WOLF = EW_TYPE_WOLF,
    GM_MOB_OCELOT = EW_TYPE_OCELOT,
    GM_MOB_WITCH = EW_TYPE_WITCH,
    GM_MOB_GHAST = EW_TYPE_GHAST,
    GM_MOB_MAGMA = EW_TYPE_MAGMA,
    GM_MOB_WITHER_SKELETON = EW_TYPE_WITHER_SKELETON,
    GM_MOB_SLIME = EW_TYPE_SLIME,
    GM_MOB_SILVERFISH = EW_TYPE_SILVERFISH,
    GM_MOB_CAVE_SPIDER = EW_TYPE_CAVE_SPIDER,
    GM_MOB_VILLAGER = EW_TYPE_VILLAGER,
    GM_MOB_ZOMBIE_VILLAGER = EW_TYPE_ZOMBIE_VILLAGER,
    GM_MOB_VINDICATOR = EW_TYPE_VINDICATOR,
    GM_MOB_EVOKER = EW_TYPE_EVOKER,
    GM_MOB_VEX = EW_TYPE_VEX,
    GM_MOB_GUARDIAN = EW_TYPE_GUARDIAN,
    GM_MOB_ELDER_GUARDIAN = EW_TYPE_ELDER_GUARDIAN,
    GM_MOB_IRON_GOLEM = EW_TYPE_IRON_GOLEM,
    GM_MOB_STRAY = EW_TYPE_STRAY,
    GM_MOB_HUSK = EW_TYPE_HUSK,
    GM_MOB_MOOSHROOM = EW_TYPE_MOOSHROOM,
    GM_MOB_RABBIT = EW_TYPE_RABBIT,
    GM_MOB_POLAR_BEAR = EW_TYPE_POLAR_BEAR,
    GM_MOB_SQUID = EW_TYPE_SQUID,
    GM_MOB_BAT = EW_TYPE_BAT,
    GM_MOB_ENDERMITE = EW_TYPE_ENDERMITE,
    GM_MOB_SNOWMAN = EW_TYPE_SNOWMAN,
    GM_MOB_GIANT = EW_TYPE_GIANT,
    GM_MOB_LLAMA = EW_TYPE_LLAMA,
    GM_MOB_HORSE = EW_TYPE_HORSE,
    GM_MOB_DONKEY = EW_TYPE_DONKEY,
    GM_MOB_MULE = EW_TYPE_MULE,
    GM_MOB_SKELETON_HORSE = EW_TYPE_SKELETON_HORSE,
    GM_MOB_ZOMBIE_HORSE = EW_TYPE_ZOMBIE_HORSE,
    GM_ENTITY_BOAT = EW_TYPE_BOAT,
    GM_ENTITY_XP_ORB = 21
};

enum {
    GM_HORSE_TAME = 2,
    GM_HORSE_SADDLED = 4,
    GM_HORSE_BRED = 8,
    GM_HORSE_EATING = 16,
    GM_HORSE_REARING = 32,
    GM_HORSE_MOUTH_OPEN = 64
};

enum {
    GM_HORSE_TAME_NO_TRIGGER = 0,
    GM_HORSE_TAME_FAILED = 1,
    GM_HORSE_TAME_SUCCEEDED = 2
};

typedef struct {
    int type, eid, growing_age;
    int status, variant, armor, temper;
    int owner_present;
    uint64_t owner_uuid_most, owner_uuid_least;
    int chested, trap, trap_time, ridden, horse_jumping;
    int eating_counter, open_mouth_counter, jump_rearing_counter;
    int tail_counter, sprint_counter, gallop_time;
    float health, jump_power;
    double max_health, movement_speed, jump_strength;
    float head_lean, prev_head_lean;
    float rearing_amount, prev_rearing_amount;
    float mouth_openness, prev_mouth_openness;
    ICStack inventory[GM_HORSE_INVENTORY_SLOTS];
} GmHorseState;

typedef struct {
    GmHorseState horse;
    int strength, decor, variant, did_spit, leashed;
    int leash_holder_kind, leash_holder_eid;
    int caravan_head_eid, caravan_tail_eid;
    double caravan_speed;
    int caravan_dist_counter;
} GmLlamaState;

/* Persistent represented entity payload used by Structure Template. This is
 * intentionally not a live-slot memcpy: Java serializes NBT, reconstructs a
 * fresh entity (and fresh AI task objects), assigns a new UUID, then restores
 * only persistent fields. Coordinates are absolute here; the Structure
 * runtime converts them to/from template-relative doubles. */
enum {
    GM_TEMPLATE_ENTITY_LIVING = 1,
    GM_TEMPLATE_ENTITY_XP = 2,
    GM_TEMPLATE_ENTITY_BOAT = 3,
    GM_TEMPLATE_ENTITY_ITEM = 4,
    GM_TEMPLATE_ENTITY_PRIMED_TNT = 5,
    GM_TEMPLATE_ENTITY_FALLING_BLOCK = 6,
    GM_TEMPLATE_ENTITY_END_CRYSTAL = 7,
    GM_TEMPLATE_ENTITY_MINECART = 8
};
enum { GM_TEMPLATE_MINECART_SLOTS = 27 };
typedef struct {
    int kind, type;
    double x, y, z, vx, vy, vz;
    double box_min_x, box_min_y, box_min_z;
    double box_max_x, box_max_y, box_max_z;
    float yaw, pitch, health, fall_distance, absorption;
    int on_ground, air, fire_ticks, portal_cooldown;
    int no_ai, persistence_required, left_handed, can_pick_up_loot;
    int size, growing_age, forced_age, forced_age_timer, in_love;
    int sheep_data, pig_saddled;
    int chicken_time_until_next_egg, chicken_jockey;
    int villager_profession, villager_willing;
    int creeper_fuse, creeper_powered, anger;
    int bat_hanging, snowman_pumpkin;
    int endermite_lifetime, endermite_player_spawned;
    int tameable_tamed, tameable_sitting, tameable_owner;
    int tameable_variant, wolf_angry;
    int golem_player_created;
    int zombie_villager_conversion_time;
    int vex_owner_eid, vex_bound_x, vex_bound_y, vex_bound_z;
    int vex_life_ticks, vex_limited_life;
    int effect_count;
    PtMobEffect effects[GM_MOB_EFFECT_CAPACITY];
    unsigned char effect_flags[GM_MOB_EFFECT_CAPACITY];
    ICStack mainhand;
    ICStack villager_inventory[8];
    GmHorseState horse;
    int llama_strength, llama_decor, llama_variant, llama_did_spit;
    int boat_variant;
    int xp_value, xp_health, xp_age;
    int xp_pickup_delay, xp_color, xp_target_color;
    ICStack item_stack;
    int item_health, item_age, item_pickup_delay, item_lifespan;
    int item_no_gravity;
    int tnt_fuse, tnt_no_gravity;
    int falling_block, falling_meta, falling_time;
    int falling_should_drop_item, falling_hurt_entities;
    int falling_no_gravity;
    int falling_hurt_max, falling_tile_tag_id;
    float falling_hurt_amount;
    int crystal_show_bottom, crystal_has_beam, crystal_no_gravity;
    int crystal_beam_x, crystal_beam_y, crystal_beam_z;
    int minecart_kind, minecart_no_gravity;
    int minecart_custom_display, minecart_display_block;
    int minecart_display_meta, minecart_display_offset;
    int minecart_fuel, minecart_tnt_fuse;
    int minecart_hopper_enabled, minecart_transfer_cooldown;
    int minecart_spawner_entity_type, minecart_spawner_delay;
    int minecart_spawner_min_delay, minecart_spawner_max_delay;
    int minecart_spawner_spawn_count, minecart_spawner_max_nearby;
    int minecart_spawner_spawn_range, minecart_spawner_activate_range;
    int minecart_spawner_nbt_tag_id;
    int minecart_spawner_default_entity_nbt;
    int minecart_spawner_potential_count;
    int minecart_spawner_potential_cap;
    GmSpawnerPotential *minecart_spawner_potentials;
    double minecart_spawner_mob_rotation;
    double minecart_spawner_prev_mob_rotation;
    int minecart_command_tag_id, minecart_command_name_tag_id;
    int minecart_command_last_output_tag_id;
    int minecart_command_success_count, minecart_command_track_output;
    double minecart_push_x, minecart_push_z;
    ICStack minecart_slots[GM_TEMPLATE_MINECART_SLOTS];
} GmMobTemplateEntity;

typedef struct {
    int type, eid, leashed;
    int holder_kind, holder_eid;
    double holder_x, holder_y, holder_z;
    int pending, pending_x, pending_y, pending_z;
} GmLivingLeashState;

typedef struct {
    int eid, type;
    int persistent, hurt_resistant_time;
    int vehicle_eid, rider_eid;
    int left_handed, can_pick_up_loot;
    int uuid_present;
    int64_t uuid_most, uuid_least;
    double x, y, z, vx, vy, vz;
    uint64_t entity_seed48;
    int entity_have_gaussian;
    double entity_gaussian;
    ICStack mainhand, head;
} GmSkeletonTrapEntityState;

typedef struct {
    int eid, living_time;
    long long bolt_vertex;
    uint64_t random_seed48;
    double x, y, z;
} GmSkeletonTrapLightning;

void gm_mobs_init(GmMobLive *m, long long seed);
void gm_mobs_destroy(GmMobLive *m);
int gm_mobs_living_cold_reserve(GmMobLive *m, int need);
int gm_mobs_living_spawn_reserve(GmMobLive *m, int additional);
int gm_mobs_living_cold_append_hot(GmMobLive *m, int hot_slot);
int gm_mobs_living_cold_park_hot(GmMobLive *m, int hot_slot);
const GmLivingColdSlot *gm_mobs_living_cold_ref(
    const GmMobLive *m, int cold_slot);
void gm_mobs_living_cold_flush(GmMobLive *m);
/* Iterate every active living entity without exposing whether it is in the
 * fixed hot page or a cold record. Initialize *cursor to zero. The returned
 * slot remains valid until the next iterator/staging call; mutations are
 * committed when iteration advances or gm_mobs_living_cold_flush is called. */
int gm_mobs_living_next_slot(GmMobLive *m, int *cursor);
int gm_mobs_xp_slot_count(const GmMobLive *m);
McOrb *gm_mobs_xp_orb_mut(GmMobLive *m, int slot);
const McOrb *gm_mobs_xp_orb_ref(const GmMobLive *m, int slot);
int gm_mobs_xp_slot_dimension(const GmMobLive *m, int slot);
int gm_mobs_xp_slot_uuid(
    const GmMobLive *m, int slot, int64_t *most, int64_t *least);
void gm_mobs_set_natural_spawning(GmMobLive *m, int enabled);
/* Component/test hook. Runtime progression never calls this directly. */
int gm_mobs_spawn(GmMobLive *m, int type, double x, double y, double z);
/* Generated-village resident spawn. Profession is the 1.11.2 career skin
 * selector: 0 farmer, 1 librarian, 2 priest, 3 smith, 4 butcher, 5 nitwit. */
int gm_mobs_spawn_villager(GmMobLive *m, double x, double y, double z,
                           int profession);
int gm_mobs_set_villager_village_state(
    GmMobLive *m, int eid, int valid, int door_count,
    int population, int mating_season);
int gm_mobs_set_villager_village_index(
    GmMobLive *m, int eid, int village_index);
int gm_mobs_set_villager_inventory_slot(
    GmMobLive *m, int eid, int slot, ICStack stack);
int gm_mobs_get_villager_mating_state(
    const GmMobLive *m, int eid, int *willing, int *mating,
    int *mate_eid, int *timeout);
int gm_mobs_take_villager_collection_position(
    GmMobLive *m, int *x, int *y, int *z);
int gm_mobs_take_villager_birth(GmMobLive *m, GmVillagerBirth *birth);
int gm_mobs_take_villager_harm(
    GmMobLive *m, GmVillagerHarmEvent *event);
int gm_mobs_villager_mate_start(GmMobLive *m, int eid);
int gm_mobs_villager_mate_update(
    GmMobLive *m, int eid, uint64_t *world_random_seed48,
    uint64_t *math_random_seed48, int *next_entity_id);
/* Direct EntityAIFollowGolem component boundary. Runtime calls the same
 * helpers through the priority-7 villager scheduler. */
int gm_mobs_villager_follow_golem_task(
    GmMobLive *m, int eid, int day, int setup_tick);
int gm_mobs_set_villager_front_door(
    GmMobLive *m, int eid, int valid, int village_index, int door_index,
    int x, int y, int z, int inside_dx, int inside_dz, int detached);
int gm_mobs_villager_restrict_door_task(
    GmMobLive *m, int eid, int day, int setup_tick);
int gm_mobs_villager_open_door_task(
    GmMobLive *m, GmWorld *world, int eid, int setup_tick);
int gm_mobs_set_villager_indoors_context(
    GmMobLive *m, int eid, int door_valid,
    int door_x, int door_y, int door_z, int inside_dx, int inside_dz,
    int home_valid, int home_x, int home_y, int home_z, int home_radius);
int gm_mobs_villager_move_indoors_task(
    GmMobLive *m, GmWorld *world, int eid,
    int nighttime_or_snow_shelter, int setup_tick);
int gm_mobs_villager_move_restriction_task(
    GmMobLive *m, GmWorld *world, int eid, int setup_tick);
int gm_mobs_villager_avoid_task(
    GmMobLive *m, GmWorld *world, int eid, int setup_tick);
int gm_mobs_villager_play_task(
    GmMobLive *m, GmWorld *world, int eid, int setup_tick);
int gm_mobs_villager_harvest_task(
    GmMobLive *m, GmWorld *world, int eid,
    int setup_tick, int mob_griefing);
/* Full EntityAIHarvestFarmland boundary, including World.rand,
 * Block.RANDOM, Math.random, entity ids, and spawned EntityItems. */
int gm_mobs_villager_harvest_task_exact(
    GmMobLive *m, GmWorld *world, int eid,
    int setup_tick, int mob_griefing,
    uint64_t *world_random_seed48, uint64_t *block_random_seed48,
    uint64_t *math_random_seed48, int *next_entity_id,
    GmLiveSim *drops);
int gm_mobs_villager_interact_task_exact(
    GmMobLive *m, int eid, int setup_tick,
    const McSinTable *sin_table, uint64_t *math_random_seed48,
    int *next_entity_id, GmLiveSim *drops);
/* Runtime's one local merchant customer. Pass -1 when no villager screen is
 * open; EntityAITradePlayer can clear it when its continuation fails. */
void gm_mobs_set_active_villager_customer(GmMobLive *m, int eid);
int gm_mobs_active_villager_customer(const GmMobLive *m);
int gm_mobs_spawn_iron_golem(GmMobLive *m, double x, double y, double z,
                             int player_created);
int gm_mobs_set_iron_golem_state(GmMobLive *m, int eid,
    int player_created, int home_check_timer,
    int attack_timer, int hold_rose_tick);
int gm_mobs_get_iron_golem_state(const GmMobLive *m, int eid,
    int *player_created, int *home_check_timer,
    int *attack_timer, int *hold_rose_tick);
int gm_mobs_set_iron_golem_village_context(
    GmMobLive *m, int eid, int village_index,
    int aggressor_eid, int low_reputation_player);
int gm_mobs_iron_golem_attack(GmMobLive *m, int golem_eid,
    int target_eid, float *rolled_damage);
int gm_mobs_iron_golem_status(GmMobLive *m, int eid, int status);
int gm_mobs_iron_golem_state_tick(GmMobLive *m, int eid);
int gm_mobs_spawn_zombie_villager(GmMobLive *m,
                                  double x, double y, double z,
                                  int profession);
int gm_mobs_spawn_witch(GmMobLive *m, double x, double y, double z);
int gm_mobs_evoker_cast_attack(
    GmMobLive *m, GmWorld *world, const struct McSinTable *sin_table,
    int evoker_eid, double target_x, double target_y, double target_z);
int gm_mobs_evoker_cast_summon(GmMobLive *m, int evoker_eid);
int gm_mobs_evoker_cast_wololo(GmMobLive *m, int evoker_eid);
int gm_mobs_evoker_fang_count(const GmMobLive *m);
int gm_mobs_evoker_fang_get(
    const GmMobLive *m, int index, GmEvokerFang *out);
/* Spawn with slime/magma size (1,2,4). Other types ignore size. */
int gm_mobs_spawn_sized(GmMobLive *m, int type, double x, double y, double z, int size);
/* Ordinary live spawn with a preconstructed Entity id. This preserves Java's
 * constructor-before-spawn ordering when an onInitialSpawn hook appends a
 * passenger or mount before the original entity enters World.loadedEntityList. */
int gm_mobs_spawn_natural_eid(
    GmMobLive *m, int type, int eid, double x, double y, double z);
int gm_mobs_set_rotation(
    GmMobLive *m, int eid, float yaw, float pitch);
/* EntitySheep.getRandomSheepColor and the low-nibble onInitialSpawn write.
 * The caller supplies World.rand; constructor/entity RNG is deliberately
 * separate in vanilla. */
int gm_mobs_random_sheep_color(JavaRandom *world_random);
/* EntitySheep.createChild two-dye crafting lookup. Recipe matches consume no
 * World.rand; every fallback consumes exactly one nextBoolean. */
int gm_mobs_sheep_child_color(
    JavaRandom *world_random, int first_fleece, int second_fleece);
int gm_mobs_sheep_on_initial_spawn(
    GmMobLive *m, int eid, JavaRandom *world_random);
/* Cold oracle hook: exact stationary represented living fixture with Java's
 * id. Slime and magma fixtures use size 1; use the sized variant for 2 or 4.
 * no_ai=false represents the taskless/gravity-free collision oracle variant. */
int gm_mobs_spawn_exact(GmMobLive *m, int type, int eid,
                        double x, double y, double z,
                        double vx, double vy, double vz,
                        float yaw, float health, int no_ai,
                        int hurt_time, int death_time,
                        int hurt_resistant_time);
int gm_mobs_spawn_exact_sized(GmMobLive *m, int type, int eid,
                        double x, double y, double z,
                        double vx, double vy, double vz,
                        float yaw, float health, int size, int no_ai,
                        int hurt_time, int death_time,
                        int hurt_resistant_time);
typedef struct {
    int targeted;
    int attempted;
    int accepted;
    int knockback;
    int critical;
    int sweep;
    int strong;
    int weak;
    int no_damage;
    int enchantment_critical;
    int sweep_hits;
    int target_eid;
    int target_dimension;
    double target_x, target_y, target_z;
    float target_width, target_height;
    float damage_dealt;
    float thorns_damage;
} GmPlayerAttackOutcome;

#define GM_PLAYER_THORNS_MAX_HITS 6
typedef struct {
    int callbacks;
    int hit_count;
    int damage[GM_PLAYER_THORNS_MAX_HITS];
} GmPlayerThornsOutcome;

/* Exact EnchantmentHelper.applyThornEnchantments random/equipment boundary
 * for a player victim. Mutates selected armor durability and player Entity.rand
 * and reports the raw retaliation calls in order. */
int gm_mobs_player_thorns_roll(
    GmMobLive *m, struct IsrInv *player_inv,
    GmPlayerThornsOutcome *outcome);

/* Returns 2 for accepted damage, 1 for a targeted rejected hit, or 0 for miss.
 * distance_walked_delta is Entity.distanceWalkedModified minus its value at
 * the latest base tick, used only by the full-cooldown sword sweep predicate.
 * outcome may be NULL for callers that do not consume client side effects. */
int gm_mobs_player_attack(GmMobLive *m, const struct PsvPlayer *player,
                          int ox, int oz,
                          const struct McSinTable *sin_table,
                          GmLiveSim *drops,
                          float attack_damage_bonus,
                          double attack_speed_multiplier,
                          int on_ladder, int in_water, int riding,
                          const GmMobDeathContext *death_context,
    float distance_walked_delta,
    GmPlayerAttackOutcome *outcome);
/* Read-only companion to gm_mobs_player_attack's target-selection pass.
 * Returns the exact candidate EID and along-ray distance used by that pass. */
int gm_mobs_player_attack_target(
    const GmMobLive *m, const struct PsvPlayer *player,
    int ox, int oz, int *eid, double *distance);
int gm_mobs_generate_hostile_loot(
    int type, int size, uint64_t *entity_seed48,
    int looting_level, int killed_by_player,
    GmHostileLootOutcome *outcome);
/* Exact inner EntityPig.attackEntityFrom(player-source) boundary.  This
 * bypasses player cooldown/weapon logic so a locked fixture can distinguish
 * damage semantics from EntityPlayer.attackTargetEntityWithCurrentItem. */
int gm_mobs_player_damage_pig_exact(
    GmMobLive *m, int eid, double attacker_x, double attacker_z,
    float damage, GmLiveSim *drops,
    const GmMobDeathContext *death_context);
/* Exact inner EntityWitch player-source damage/death boundary. Lethal accepted
 * damage runs entities/witch with the held Looting level and appends exact
 * EntityItem constructor state before returning to EntityPlayer's tail. */
int gm_mobs_player_damage_witch_exact(
    GmMobLive *m, int eid, double attacker_x, double attacker_z,
    float damage, int looting_level, GmLiveSim *drops,
    const GmMobDeathContext *death_context);
/* EntityPlayerMP.swingArm resets the server cooldown on every arm packet. */
void gm_mobs_player_swing(GmMobLive *m);
/* Shared EntityLivingBase.attackEntityFrom hurt-resistance path. Dragon
 * contact and tape-replay authoritative mob contacts use the same gate as
 * live hostile melee. CombatRules armor absorb + InventoryPlayer.damageArmor
 * run unless bypass_armor is set; enchantment protection still applies to
 * unblockable, non-absolute sources such as fire and fall. */
int gm_mobs_attack_player(GmMobLive *m, struct PvStats *vitals,
                          struct IsrInv *player_inv, float amount,
                          int bypass_armor);
int gm_mobs_attack_player_source(GmMobLive *m, struct PvStats *vitals,
                                 struct IsrInv *player_inv, float amount,
                                 int bypass_armor, int source_flags);
int gm_mobs_last_player_damage_source(void);
/* EntityLivingBase's ANVIL/FALLING_BLOCK head-slot hook. Runs before the hurt
 * gate, damages any nonempty head stack, and returns the 0.75-scaled amount. */
float gm_mobs_anvil_helmet_pre_damage(GmMobLive *m,
                                      struct IsrInv *player_inv,
                                      float amount);
float gm_mobs_player_resistance_damage(const GmMobLive *m, float amount);
float gm_mobs_player_absorb_damage(GmMobLive *m, float amount);
void gm_mobs_player_hurt_tick(GmMobLive *m);
/* boat_forward/boat_strafe: player WASD while mounted (GmAction.forward/strafe).
 * Zero when not riding; runtime passes the action and suppresses player walk. */
typedef struct {
    int chunk_x, chunk_z;
} GmNaturalSpawnChunk;

typedef struct {
    const GmNaturalSpawnChunk *loaded_chunks;
    int loaded_chunk_count;
    int world_spawn_x, world_spawn_y, world_spawn_z;
    uint64_t *collections_random_seed48;
} GmNaturalSpawnContext;

/* Direct Java-8 HashSet iteration + Collections.shuffle seam. Returns the
 * complete eligible count, writing up to capacity entries. */
int gm_mobs_natural_chunk_order(
    int player_chunk_x, int player_chunk_z,
    const GmNaturalSpawnContext *context, uint64_t *collections_seed48,
    GmNaturalSpawnChunk *out, int capacity);

void gm_mobs_tick(GmMobLive *m, GmWorld *world, const struct Chunk *window,
                  const struct McSinTable *sin_table,
                  struct PsvPlayer *player, struct PvStats *vitals,
                  int ox, int oz, int dimension, long long world_time,
                  const GmWorldClock *clock,
                  int mob_griefing, uint64_t *world_random_seed48,
                  uint64_t *math_random_seed48, int *next_entity_id,
                  int do_mob_loot,
                  GmLiveSim *drops,
                  float boat_forward, float boat_strafe);
void gm_mobs_tick_spawn_context(
                  GmMobLive *m, GmWorld *world, const struct Chunk *window,
                  const struct McSinTable *sin_table,
                  struct PsvPlayer *player, struct PvStats *vitals,
                  int ox, int oz, int dimension, long long world_time,
                  const GmWorldClock *clock,
                  int mob_griefing, uint64_t *world_random_seed48,
                  uint64_t *math_random_seed48, int *next_entity_id,
                  const GmNaturalSpawnContext *spawn_context,
                  int do_mob_loot, GmLiveSim *drops,
                  float boat_forward, float boat_strafe);
/* Clear disposable PathNavigate state after initialization or NBT-like load. */
void gm_mobs_reset_navigation_cache(GmMobLive *m);
/* EntityPlayerMP's post-travel living-entity query. Used by parked server
 * ticks after the ordinary living list has advanced. */
void gm_mobs_player_collision_query(
    GmMobLive *m, struct PsvPlayer *player,
    int ox, int oz, int dimension);

/* Direct exact Guardian attack-task boundary. Counter starts at -10, publishes
 * TARGET_ENTITY/status 21 at zero, and applies the two ordered damage sources
 * at attack duration (80 ordinary, 60 elder). Returns 1 while active. */
int gm_mobs_guardian_attack_step(GmMobLive *m, int guardian_eid,
                                 struct PvStats *vitals,
                                 struct IsrInv *player_inv);

/* EntityGuardian's stationary, non-magic, non-explosion retaliation. */
float gm_mobs_guardian_thorns(GmMobLive *m, int guardian_eid,
                              float attacker_health, float damage,
                              int source_flags);
/* Consume one EntityElderGuardian 1200-tick mining-fatigue broadcast. */
int gm_mobs_take_guardian_curse(GmMobLive *m, int *elder_eid);

int gm_mobs_handle_water_slot(
    GmMobLive *m, const GmWorld *world, const struct Chunk *window,
    int ox, int oz, EwStore *store, int slot);
int gm_mobs_fill_views(const GmMobLive *m, GmEntityView *out, int max);
int gm_mobs_alive(const GmMobLive *m);
int gm_mobs_living_count(const GmMobLive *m);
/* Enumerate represented EntityLivingBase collision boxes in one dimension.
 * Returns the number written, bounded by capacity. Boats and XP orbs are not
 * EntityLivingBase. */
int gm_mobs_living_boxes(
    const GmMobLive *m, int dimension, McAABB *out, int capacity);
/* Bounded EntityFallingBlock living-target leg. Visits active, controlled
 * NoAI sheep, pigs, cows, and chickens plus ordinary Witches in slot order;
 * returns accepted target count. A fresh accepted source-less hit advances
 * Math once and the target Entity.rand four LCG steps before any loot draws.
 * When do_mob_loot is true, lethal chicken, pig, cow, adult sheep, and Witch
 * targets run their exact 1.11.2 loot tables synchronously and append causal
 * EntityItems through next_entity_id. Fresh controlled passive hits append
 * status 2 and their exact hurt/death sound; ordinary Witches use the same
 * source-less feedback with no held-equipment drop or combat credit. Lethal
 * hits append status 3 after loot. XP remains a later death update concern.
 * Fixed item capacity rejects that one target atomically, including events. */
int gm_mobs_falling_anvil_damage_controlled_passives(
    GmMobLive *m, int dimension, const McAABB *falling_box, float damage,
    uint64_t *math_random_seed48, GmLiveSim *drops,
    int *next_entity_id, int do_mob_loot);
/* Oldest-first bounded causal event view. Sequence numbers remain monotonic;
 * event_dropped reports records overwritten before a consumer read them. */
int gm_mobs_event_count(const GmMobLive *m);
int gm_mobs_event_get(const GmMobLive *m, int index, GmMobEvent *out);
int gm_mobs_type_by_eid(const GmMobLive *m, int eid);
/* Oldest-first terminal EntityLivingBase particle batches. */
int gm_mobs_terminal_particle_count(const GmMobLive *m);
int gm_mobs_terminal_particle_get(
    const GmMobLive *m, int index, GmMobTerminalParticles *out);
int gm_mobs_particle_batch_count(const GmMobLive *m);
int gm_mobs_particle_batch_get(
    const GmMobLive *m, int index, GmMobParticleBatch *out);
/* Cold Explosion.doExplosionA represented mob/boat leg. Non-living entities
 * are a separate parity surface. The target snapshot makes ray exposure
 * immutable while accepted damage mutates the live store. */
int gm_mobs_explosion_targets(
    const GmMobLive *m, int dimension,
    GmMobExplosionTarget *out, int capacity);
float gm_mobs_eye_height_at(const GmMobLive *m, int slot);
int gm_mobs_apply_explosion(
    GmMobLive *m, int slot, float damage,
    double impulse_x, double impulse_y, double impulse_z,
    GmLiveSim *drops);
/* Entity.onStruckByLightning over the represented living AABB. Pigs convert
 * to a new pigman entity; creepers retain the powered flag after generic
 * lightning damage. Returns the number of struck snapshot entities. */
int gm_mobs_lightning_strike(
    GmMobLive *m, int dimension, const McAABB *box,
    GmLiveSim *drops, int *next_entity_id);
int gm_mobs_creeper_is_powered(
    const GmMobLive *m, int eid, int *powered);
/* EntityPotion living-target effects. Instant health/damage performs the
 * undead reversal; water damages only blaze/enderman. */
int gm_mobs_apply_instant_potion(
    GmMobLive *m, int slot, int potion_id, int amplifier,
    double factor, GmLiveSim *drops);
int gm_mobs_apply_instant_potion_indirect(
    GmMobLive *m, GmWorld *world, int slot,
    int potion_id, int amplifier, double factor,
    const GmMobPotionDamageOwner *owner,
    GmLiveSim *drops, const GmMobDeathContext *death_context);
int gm_mobs_apply_potion_effect(
    GmMobLive *m, int slot, int potion_id, int amplifier, int duration);
/* EntityLivingBase.addPotionEffect, including instant-effect instances from
 * tipped arrows. Throwable potions use apply_instant_potion instead. */
int gm_mobs_add_potion_effect(
    GmMobLive *m, int slot, int potion_id, int amplifier, int duration);
int gm_mobs_add_potion_effect_flags(
    GmMobLive *m, int slot, int potion_id, int amplifier, int duration,
    int ambient, int show_particles);
int gm_mobs_potion_effect_count(const GmMobLive *m, int slot);
int gm_mobs_potion_effect_get(
    const GmMobLive *m, int slot, int index, PtMobEffect *out);
int gm_mobs_potion_effect_flags(
    const GmMobLive *m, int slot, int index,
    int *ambient, int *show_particles);
float gm_mobs_max_health(const GmMobLive *m, int slot);
float gm_mobs_absorption(const GmMobLive *m, int slot);
int gm_mobs_air(const GmMobLive *m, int slot);
int gm_mobs_set_air(GmMobLive *m, int eid, int air);
int gm_mobs_apply_water_potion(
    GmMobLive *m, GmWorld *w, int slot, GmLiveSim *drops,
    uint64_t *math_random_seed48);
/* EntityZombieVillager cure lifecycle. The interaction consumes a normal
 * golden apple only when Weakness is active, replaces Weakness with Strength,
 * chooses the exact 3600..6000 timer, and emits status 16. */
int gm_mobs_zombie_villager_can_cure(
    const GmMobLive *m, int eid, const ICStack *held);
int gm_mobs_cure_zombie_villager(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot, int creative);
int gm_mobs_zombie_villager_cure_audio(
    GmMobLive *m, int eid, double *x, double *y, double *z,
    float *volume, float *pitch);
int gm_mobs_zombie_villager_conversion_progress(
    GmMobLive *m, GmWorld *w, int eid);
int gm_mobs_zombie_villager_conversion_state(
    const GmMobLive *m, int eid, int *conversion_time);
int gm_mobs_zombie_villager_finish_conversion(
    GmMobLive *m, int eid, int *next_entity_id);
int gm_mobs_take_zombie_villager_world_event(
    GmMobLive *m, int *x, int *y, int *z);
/* Boxes whose ordinary Entity.move path ran this tick. In controlled_only
 * mode, excludes true NoAI fixtures while retaining the taskless/gravity-free
 * collision oracle variant. */
int gm_mobs_collision_boxes(
    const GmMobLive *m, int dimension, int controlled_only,
    McAABB *out, int capacity);
/* Ordinary doBlockCollisions boxes for pressure-plate/tripwire callbacks.
 * Includes boats, which are entities but not EntityLivingBase. */
int gm_mobs_trigger_collision_boxes(
    const GmMobLive *m, int dimension, int controlled_only,
    McAABB *out, int capacity);
/* BlockBasePressurePlate.Sensitivity.MOBS query over represented living
 * entities in one dimension. Boats and XP orbs are not EntityLivingBase. */
int gm_mobs_living_intersects_aabb(
    const GmMobLive *m, int dimension, const McAABB *box);
/* Count represented EntityLivingBase instances intersecting a trigger box. */
int gm_mobs_living_count_intersects_aabb(
    const GmMobLive *m, int dimension, const McAABB *box);
int gm_mobs_boat_count_intersects_aabb(
    const GmMobLive *m, int dimension, const McAABB *box);
/* EntityXPOrb.move boxes captured during the current ordinary entity pass. */
int gm_mobs_xp_collision_boxes(
    const GmMobLive *m, McAABB *out, int capacity);
/* Count currently live XP orbs intersecting an EVERYTHING trigger query. */
int gm_mobs_xp_count_intersects_aabb(
    const GmMobLive *m, int dimension, const McAABB *box);
/* World.getEntitiesWithinAABBExcludingEntity query over every entity class
 * owned by GmMobLive: living mobs, boats, XP orbs, and evoker fangs. */
int gm_mobs_any_intersects_aabb(
    const GmMobLive *m, int dimension, const McAABB *box);
/* BlockFarmland.turnToDirt entity query. Move every represented living mob,
 * boat, and XP orb whose current box intersects the new dirt collision box to
 * its top. Entity.setPosition leaves motion and history fields untouched. */
int gm_mobs_farmland_lift_intersecting(
    GmMobLive *m, int dimension, const McAABB *box, double top_y);
/* ProjectileHelper entity leg: nearest collidable represented mob/boat AABB,
 * expanded by vanilla's exact 0.30000001192092896. */
int gm_mobs_projectile_intercept(
    const GmMobLive *m, int dimension, int shooter_eid, int include_shooter,
    double sx, double sy, double sz, double ex, double ey, double ez,
    int *slot, double *distance_sq);
/* EntityArrow.onHit against a represented living target fired by the player.
 * Returns 0 when the slot is no longer targetable, 1 when attackEntityFrom
 * rejects the hit, 2 when ordinary damage is accepted, and 3 when an
 * Enderman accepts the indirect hit by teleporting without taking damage. */
int gm_mobs_player_arrow_hit(
    GmMobLive *m, GmWorld *world, int slot,
    double shooter_x, double shooter_z, float damage,
    int knockback, double arrow_vx, double arrow_vz, int burning,
    int looting_level, GmLiveSim *drops,
    const GmMobDeathContext *death_context);
/* EntityEgg/EntitySnowball player-owned entity impact. Unlike the arrow
 * boundary, a zero-damage thrown hit is accepted on a fresh living target:
 * it sets hurt/revenge state and consumes the ordinary feedback RNG without
 * changing health. Endermen retain their indirect-source teleport path. */
int gm_mobs_player_throwable_hit(
    GmMobLive *m, GmWorld *world, int slot,
    double thrower_x, double thrower_z, float damage,
    GmLiveSim *drops, const GmMobDeathContext *death_context);
/* EntitySnowball impact with a represented non-player living thrower. */
int gm_mobs_entity_throwable_hit(
    GmMobLive *m, GmWorld *world, int slot, int thrower_eid,
    double thrower_x, double thrower_z, float damage,
    GmLiveSim *drops, const GmMobDeathContext *death_context);
/* EntityArrow.onHit with no shootingEntity, as used by dispenser arrows.
 * Preserves source-less hurt feedback and never credits or angers a player. */
int gm_mobs_source_arrow_hit(
    GmMobLive *m, GmWorld *world, int slot,
    double arrow_x, double arrow_z, float damage,
    GmLiveSim *drops, const GmMobDeathContext *death_context);
/* EntityArrow.onHit with a represented non-player shootingEntity. The true
 * source supplies primary knockback and a living victim's revenge target. */
int gm_mobs_entity_arrow_hit(
    GmMobLive *m, GmWorld *world, int slot, int shooter_eid,
    double shooter_x, double shooter_z, float damage,
    GmLiveSim *drops, const GmMobDeathContext *death_context);
/* EntityFishHook caught-entity lifecycle. Hook position follows 80% of the
 * living target's height; retraction adds the vanilla pull impulse. */
int gm_mobs_fishing_target_position(
        const GmMobLive *m, int slot, int dimension,
        double *x, double *y, double *z);
int gm_mobs_find_slot_by_eid(const GmMobLive *m, int eid);
int gm_mobs_fishing_reel(
    GmMobLive *m, int slot, int dimension,
    double angler_x, double angler_y, double angler_z);
/* EntitySmallFireball entity impact. Returns whether the target still existed;
 * fire-immune entities consume the projectile without taking damage. */
int gm_mobs_small_fireball_hit(
    GmMobLive *m, int slot, float damage, GmLiveSim *drops);
/* EntityWitherSkull entity impact. Returns 2 when this accepted hit killed
 * the target, 1 for a represented living impact, and 0 for no target. */
int gm_mobs_wither_skull_hit(
    GmMobLive *m, int slot, float damage, GmLiveSim *drops);
/* Pop one exact AIFireballAttack event captured before the blaze moves. */
int gm_mobs_take_blaze_shot(GmMobLive *m, int slot, GmBlazeShot *shot);
int gm_mobs_take_witch_shot(GmMobLive *m, int slot, GmWitchShot *shot);
void gm_mobs_set_player_potion_flags(GmMobLive *m, unsigned int flags);
/* Cold capsule hooks for Entity.rand and EntityBlaze's private float state. */
int gm_mobs_set_entity_random_state(
    GmMobLive *m, int eid, uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
int gm_mobs_set_entity_uuid(
    GmMobLive *m, int eid, int64_t uuid_most, int64_t uuid_least);
/* Exact common Entity/EntityLivingBase/EntityLiving checkpoint for an
 * existing NoAI mob. Type-specific state is deliberately owned by separate
 * restore hooks; this transports the hidden base cursor shared by them. */
int gm_mobs_restore_no_ai_base_state(
    GmMobLive *m, int eid, int air, int fire_ticks, int on_ground,
    float fall_distance, int in_water, int ticks_existed,
    int living_sound_time, float last_damage, uint64_t seed48,
    int have_next_gaussian, double next_gaussian);
int gm_mobs_restore_bat_state(GmMobLive *m, int eid, int hanging);
int gm_mobs_set_bat_ai_state(
    GmMobLive *m, int eid, int hanging, int spawn_position_valid,
    int spawn_x, int spawn_y, int spawn_z,
    float head_yaw, float render_yaw_offset,
    int body_rotation_tick_counter, float body_prev_head_yaw,
    int entity_age, int persistence_required);
void gm_mobs_set_player_creative(GmMobLive *m, int creative);
void gm_mobs_set_player_disable_damage(GmMobLive *m, int disabled);
int gm_mobs_restore_snowman_state(GmMobLive *m, int eid, int pumpkin);
void gm_mobs_set_controlled_mob_griefing(GmMobLive *m, int enabled);
int gm_mobs_restore_endermite_state(
    GmMobLive *m, int eid, int lifetime, int player_spawned,
    int persistence_required);
int gm_mobs_restore_squid_state(
    GmMobLive *m, int eid, float squid_pitch, float prev_squid_pitch,
    float squid_yaw, float prev_squid_yaw, float squid_rotation,
    float prev_squid_rotation, float tentacle_angle,
    float last_tentacle_angle, float random_motion_speed,
    float rotation_velocity, float rotate_speed, float random_motion_x,
    float random_motion_y, float random_motion_z,
    float render_yaw_offset, float head_yaw,
    int body_rotation_tick_counter, float body_prev_head_yaw);
int gm_mobs_set_squid_ai_state(
    GmMobLive *m, int eid, int entity_age, int persistence_required);
int gm_mobs_restore_no_ai_box(
    GmMobLive *m, int eid, double min_x, double min_y, double min_z,
    double max_x, double max_y, double max_z);
/* Change the temporary NoAI fixture into a normal living entity. The
 * no_ai=false transition is used after restoring a fresh villager NBT
 * boundary; its non-persistent task state is reset to constructor defaults. */
int gm_mobs_set_no_ai(GmMobLive *m, int eid, int no_ai);
/* Cold fixture entry into the same self-potion transition used by the live
 * Witch tick. Conditions correspond to EntityWitch.onLivingUpdate inputs. */
int gm_mobs_witch_self_potion_step(
    GmMobLive *m, int eid, int in_water, int burning,
    int has_target, double target_distance_sq);
int gm_mobs_witch_self_potion_state(
    const GmMobLive *m, int eid, int *drinking, int *timer,
    int *potion, float *drink_pitch);
/* Cold save/oracle hook for EntityChicken private state. */
int gm_mobs_set_chicken_state(
    GmMobLive *m, int eid, int time_until_next_egg,
    float wing_rotation, float dest_pos, float old_flap_speed,
    float old_flap, float wing_rot_delta, int chicken_jockey);
/* Exact NoAI Slime/Magma transient animation checkpoint. The persistent size
 * is supplied to gm_mobs_spawn_exact_sized; was_on_ground is NBT-backed. */
int gm_mobs_set_slime_state(
    GmMobLive *m, int eid, float squish_amount, float squish_factor,
    float prev_squish_factor, int was_on_ground);
int gm_mobs_set_next_animal_child_state(
    GmMobLive *m, uint64_t seed48, int have_next_gaussian,
    double next_gaussian, int chicken_time_until_next_egg);
int gm_mobs_queue_animal_child_state(
    GmMobLive *m, uint64_t seed48, int have_next_gaussian,
    double next_gaussian, int chicken_time_until_next_egg);
/* Compatibility hooks retained for the established sheep fixtures. */
int gm_mobs_set_next_sheep_child_random_state(
    GmMobLive *m, uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
int gm_mobs_queue_sheep_child_random_state(
    GmMobLive *m, uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
int gm_mobs_set_sheep_state(
    GmMobLive *m, int eid, int fleece_color, int sheared);
int gm_mobs_set_growing_age(GmMobLive *m, int eid, int growing_age);
/* Exact EntityAnimal breeding-item interaction for represented sheep, cows,
 * pigs, chickens, and ocelots. Returns 1 when the selected hand is handled and 0 when
 * vanilla would pass. The live product is survival-only; creative is retained
 * for strict oracle fixtures. */
int gm_mobs_feed_animal(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot, int creative);
/* Exact EntityCow bucket interaction. Event eid 0 is the represented player.
 * Returns 1 when handled, 0 when vanilla passes, and -1 when an otherwise
 * valid dropped-milk transition cannot fit the bounded exact item store. */
int gm_mobs_milk_cow(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot, int creative,
    double player_x, double player_y, double player_z,
    float player_yaw, float player_pitch, double player_eye_height,
    const McSinTable *sin_table, uint64_t *math_random_seed48,
    GmLiveSim *drops, int *next_entity_id);
/* EntityMooshroom's two unique interaction branches. Bowl returns 1 when
 * handled, 0 for vanilla pass, and -1 on a bounded drop-capacity failure.
 * Shear returns 2 after conversion to a cow and five red-mushroom drops, 1
 * for handled-but-ineligible shears, 0 for pass, and -1 when the atomic
 * conversion cannot fit. */
int gm_mobs_bowl_mooshroom(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot, int creative,
    double player_x, double player_y, double player_z,
    float player_yaw, float player_pitch, double player_eye_height,
    const McSinTable *sin_table, uint64_t *math_random_seed48,
    GmLiveSim *drops, int *next_entity_id);
int gm_mobs_shear_mooshroom(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot,
    uint64_t *shear_random_seed48, uint64_t *math_random_seed48,
    GmLiveSim *drops, int *next_entity_id);
/* Forge Snow Golem IShearable route. An equipped pumpkin is removed and the
 * tool takes one entity-RNG durability attempt. ItemShears constructs its
 * private Forge Random after onSheared, but consumes no draws because the
 * returned drop list is empty; there is no sound or Math RNG consumption. */
int gm_mobs_shear_snowman(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot);
/* Exact unsaddled EntityPig ItemSaddle boundary. Every saddle-on-pig request
 * is handled; only an adult unsaddled pig mutates, sounds, and consumes. */
int gm_mobs_saddle_pig(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot, int creative);
int gm_mobs_set_pig_saddled(GmMobLive *m, int eid, int saddled);
int gm_mobs_get_pig_saddled(const GmMobLive *m, int eid, int *saddled);
/* Exact immediate startRiding association for the represented player. Pig
 * travel, passenger pose, steering, and dismount geometry are separate. */
int gm_mobs_pig_mount(GmMobLive *m, int eid);
/* Explicit EntityLivingBase.dismountEntity placement for the represented
 * player riding a pig. Terminal pig retirement only clears pig_ride_eid. */
void gm_mobs_pig_dismount_explicit(
    GmMobLive *m, GmWorld *world, const struct Chunk *window,
    struct PsvPlayer *player, int ox, int oz);
void gm_mobs_pig_dismount(GmMobLive *m);
int gm_mobs_pig_riding(const GmMobLive *m, int *eid);
/* AbstractHorse family exact-state boundary. Attribute values are the saved
 * base attributes, not derived defaults, so NBT continuation does not reroll
 * constructor RNG. The ordinary spawn helper performs the 1.11.2 constructor
 * rolls when attributes are not supplied by a save. */
int gm_mobs_horse_type(int type);
int gm_mobs_spawn_horse_exact(
    GmMobLive *m, int type, int eid,
    double x, double y, double z, double vx, double vy, double vz,
    float yaw, float health, int no_ai,
    double max_health, double movement_speed, double jump_strength,
    int growing_age, int status, int temper, int variant,
    int armor, int chested, int trap, int trap_time);
int gm_mobs_spawn_llama_exact(
    GmMobLive *m, int eid,
    double x, double y, double z, double vx, double vy, double vz,
    float yaw, float health, int no_ai,
    double max_health, double movement_speed, double jump_strength,
    int growing_age, int status, int temper, int variant,
    int strength, int decor, int chested, int did_spit, int leashed);
int gm_mobs_get_llama_state(
    const GmMobLive *m, int eid, GmLlamaState *out);
int gm_mobs_llama_set_leashed(GmMobLive *m, int eid, int leashed);
int gm_mobs_restore_llama_links(
    GmMobLive *m, int eid, int leash_holder_kind, int leash_holder_eid,
    int caravan_head_eid, int caravan_tail_eid,
    double caravan_speed, int caravan_dist_counter);
/* Leash-knot anchors are non-living EntityHanging instances, represented as
 * kind 3 with their fixed center position. A negative replacement EID marks
 * a removed knot so EntityCreature's next leash update drops the lead. */
int gm_mobs_llama_set_leash_knot(
    GmMobLive *m, int eid, int knot_eid,
    double x, double y, double z);
int gm_mobs_llama_set_leash_pending(
    GmMobLive *m, int eid, int x, int y, int z);
int gm_mobs_llama_invalidate_leash_knot(
    GmMobLive *m, int knot_eid, int creative);
int gm_mobs_vanilla_leashable_type(int type);
int gm_mobs_living_can_be_leashed(const GmMobLive *m, int eid);
int gm_mobs_set_wolf_angry(GmMobLive *m, int eid, int angry);
int gm_mobs_get_living_leash_state(
    const GmMobLive *m, int eid, GmLivingLeashState *out);
int gm_mobs_living_set_leash_player(
    GmMobLive *m, int eid, int player_eid);
int gm_mobs_living_set_leash_living(
    GmMobLive *m, int eid, int holder_eid);
int gm_mobs_living_set_leash_knot(
    GmMobLive *m, int eid, int knot_eid,
    double x, double y, double z);
int gm_mobs_living_set_leash_pending(
    GmMobLive *m, int eid, int x, int y, int z);
int gm_mobs_living_clear_leash(GmMobLive *m, int eid);
int gm_mobs_living_invalidate_leash_knot(
    GmMobLive *m, int knot_eid, int creative);
int gm_mobs_entity_aabb(
    const GmMobLive *m, int eid, McAABB *out);
int gm_mobs_living_leash_step(
    GmMobLive *m, int eid, int dimension,
    double player_x, double player_y, double player_z,
    float player_health, uint64_t *math_random_seed48,
    int *next_entity_id, GmLiveSim *drops);
int gm_mobs_llama_caravan_join(
    GmMobLive *m, int eid, int head_eid);
int gm_mobs_llama_caravan_leave(GmMobLive *m, int eid);
/* EntityAILlamaFollowCaravan shouldExecute and one continue/update boundary.
 * The step target is PathNavigate.tryMoveToXYZ's requested destination; path
 * construction remains the navigator's responsibility. */
int gm_mobs_llama_caravan_try_join(GmMobLive *m, int eid);
int gm_mobs_llama_caravan_step(
        GmMobLive *m, int eid,
        double *target_x, double *target_y, double *target_z,
        double *speed);

#define GM_LLAMA_NAVIGATION_POINTS 200
typedef struct {
    int target_x, target_y, target_z;
    int path_len, path_index;
    int points[GM_LLAMA_NAVIGATION_POINTS * 3];
} GmLlamaNavigationState;

enum {
    GM_LLAMA_TASK_SWIM = 1u << 0,
    GM_LLAMA_TASK_RUN_CRAZY = 1u << 1,
    GM_LLAMA_TASK_CARAVAN = 1u << 2,
    GM_LLAMA_TASK_RANGED = 1u << 3,
    GM_LLAMA_TASK_PANIC = 1u << 4,
    GM_LLAMA_TASK_MATE = 1u << 5,
    GM_LLAMA_TASK_FOLLOW_PARENT = 1u << 6,
    GM_LLAMA_TASK_WANDER = 1u << 7,
    GM_LLAMA_TASK_WATCH = 1u << 8,
    GM_LLAMA_TASK_IDLE = 1u << 9
};

/* Read the disposable in-memory PathNavigate state used by a live llama. */
int gm_mobs_llama_navigation_state(
    const GmMobLive *m, int eid, GmLlamaNavigationState *out);
/* Read EntityAITasks-style running bits for task-conflict oracle fixtures. */
unsigned int gm_mobs_llama_task_mask(const GmMobLive *m, int eid);
int gm_mobs_llama_spit_attack_exact(
    GmMobLive *m, int owner_eid, int target_eid, int spit_eid,
    uint64_t random_seed48, int random_have_gaussian,
    double random_next_gaussian, const McSinTable *sin_table,
    GmLlamaSpit *out);
int gm_mobs_take_llama_spit(GmMobLive *m, GmLlamaSpit *out);
int gm_mobs_take_snowman_shot(GmMobLive *m, GmSnowmanShot *out);
int gm_mobs_snowman_attack_exact(
    GmMobLive *m, int owner_eid, int target_eid,
    uint64_t *entity_seed_generator_seed48,
    uint64_t *server_uuid_random_seed48,
    int *next_entity_id, GmSnowmanShot *out);
int gm_mobs_llama_spit_hit(
    GmMobLive *m, int slot, int owner_eid,
    GmLiveSim *drops, const GmMobDeathContext *death_context);
int gm_mobs_get_horse_state(
    const GmMobLive *m, int eid, GmHorseState *out);
int gm_mobs_set_horse_inventory(
    GmMobLive *m, int eid, int inventory_slot, ICStack stack);
int gm_mobs_horse_equip_chest(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot, int creative);
int gm_mobs_restore_horse_lifecycle(
    GmMobLive *m, int eid, int in_love, int forced_age,
    int forced_age_timer, int eating_counter, int open_mouth_counter,
    int jump_rearing_counter, int tail_counter, int sprint_counter,
    int gallop_time, int horse_jumping, int allow_stand_sliding,
    float jump_power, float head_lean, float prev_head_lean,
    float rearing_amount, float prev_rearing_amount,
    float mouth_openness, float prev_mouth_openness,
    float prev_limb_amount, float limb_amount, float limb_swing);
int gm_mobs_horse_feed(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot, int creative);
int gm_mobs_horse_mount(GmMobLive *m, int eid);
void gm_mobs_set_represented_player_uuid(
    GmMobLive *m, uint64_t uuid_most, uint64_t uuid_least);
void gm_mobs_set_represented_player_primary_hand(
    GmMobLive *m, int primary_right);
int gm_mobs_restore_horse_owner(
    GmMobLive *m, int eid, int present,
    uint64_t uuid_most, uint64_t uuid_least);
/* One measured EntityAIRunAroundLikeCrazy.updateTask boundary. */
int gm_mobs_horse_crazy_attempt(GmMobLive *m, int eid);
/* Explicit EntityLivingBase.dismountEntity placement for the represented
 * player riding an AbstractHorse family entity. */
void gm_mobs_horse_dismount_explicit(
    GmMobLive *m, GmWorld *world, const struct Chunk *window,
    const struct McSinTable *sin_table, struct PsvPlayer *player,
    int ox, int oz);
void gm_mobs_horse_dismount_explicit_side(
    GmMobLive *m, GmWorld *world, const struct Chunk *window,
    const struct McSinTable *sin_table, struct PsvPlayer *player,
    int primary_right, int ox, int oz);
void gm_mobs_horse_dismount(GmMobLive *m);
int gm_mobs_horse_riding(const GmMobLive *m, int *eid);
int gm_mobs_horse_set_jump_power(GmMobLive *m, int charge);
int gm_mobs_horse_set_trap(GmMobLive *m, int eid, int trap, int trap_time);
/* Exact EntityAISkeletonRiders.updateTask construction boundary. The
 * clamped difficulty input is DifficultyInstance.getClampedAdditionalDifficulty;
 * the checked live fixture uses 0.0F. Returns seven loaded living spawns. */
int gm_mobs_skeleton_trap_activate(
    GmMobLive *m, int horse_eid, float clamped_difficulty,
    uint64_t *entity_seed_generator_seed48,
    uint64_t *server_uuid_random_seed48,
    uint64_t *math_random_seed48, int *next_entity_id);
int gm_mobs_skeleton_trap_entity_state(
    const GmMobLive *m, int eid, GmSkeletonTrapEntityState *out);
int gm_mobs_take_skeleton_trap_lightning(
    GmMobLive *m, GmSkeletonTrapLightning *out);
int gm_mobs_take_skeleton_trap_shot(
    GmMobLive *m, GmSkeletonTrapShot *out);
int gm_mobs_skeleton_trap_shoot_exact(
    GmMobLive *m, int shooter_eid,
    double target_x, double target_y, double target_z,
    float target_height);
int gm_mobs_horse_create_child(
    GmMobLive *m, int first_eid, int second_eid, int child_eid,
    GmHorseState *child_state);
int gm_mobs_llama_create_child(
    GmMobLive *m, int first_eid, int second_eid, int child_eid,
    GmLlamaState *child_state);
/* Server ItemCarrotOnAStick.onItemRightClick. Returns SUCCESS as 1, PASS as
 * 0. Steering/travel consumes the state later in the client-authoritative
 * ridden pig tick. */
int gm_mobs_pig_boost(
    GmMobLive *m, IsrInv *inventory, int hand_slot, int creative);
int gm_mobs_set_pig_boost_state(
    GmMobLive *m, int eid, int boosting, int boost_time, int boost_total);
int gm_mobs_get_pig_boost_state(
    const GmMobLive *m, int eid,
    int *boosting, int *boost_time, int *boost_total);
int gm_mobs_animal_can_feed(const GmMobLive *m, int eid, int item);
int gm_mobs_set_animal_breeding_state(
    GmMobLive *m, int eid, int in_love, int forced_age,
    int forced_age_timer, int bred_by_player);
int gm_mobs_get_animal_breeding_state(
    const GmMobLive *m, int eid, int *growing_age, int *in_love,
    int *forced_age, int *forced_age_timer, int *bred_by_player);
/* Compatibility names retained for the strict sheep fixtures. */
int gm_mobs_feed_sheep(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot, int creative);
int gm_mobs_sheep_can_feed(const GmMobLive *m, int eid);
int gm_mobs_set_sheep_breeding_state(
    GmMobLive *m, int eid, int in_love, int forced_age,
    int forced_age_timer, int bred_by_player);
int gm_mobs_get_sheep_breeding_state(
    const GmMobLive *m, int eid, int *growing_age, int *in_love,
    int *forced_age, int *forced_age_timer, int *bred_by_player);
int gm_mobs_animal_mate_update(
    GmMobLive *m, int initiator_eid, int mate_eid, int *delay,
    int event_cancelled, int event_child_present,
    uint64_t *world_random_seed48, uint64_t *math_random_seed48,
    int *next_entity_id, int do_mob_loot, GmAnimalMateResult *out);
/* One exact EntityAIMate updateTask boundary for a preselected pair. The
 * caller owns task selection/reset and passes the current spawnBabyDelay. */
int gm_mobs_sheep_mate_update(
    GmMobLive *m, int initiator_eid, int mate_eid, int *delay,
    int event_cancelled, int event_child_present,
    uint64_t *world_random_seed48, uint64_t *math_random_seed48,
    int *next_entity_id, int do_mob_loot, GmSheepMateResult *out);
/* Exact EntityAIEatGrass task boundary. Begin consumes one Entity.rand draw,
 * starts at timer 40 on acceptance, and emits entity status 10. Each update
 * decrements first; update 36 applies the block/bonus transaction at timer 4.
 * mob_griefing gates only the block mutation and world event, never regrowth. */
int gm_mobs_sheep_graze_begin(GmMobLive *m, GmWorld *w, int eid);
int gm_mobs_sheep_graze_update(
    GmMobLive *m, GmWorld *w, int eid, int mob_griefing);
int gm_mobs_sheep_eat_timer(const GmMobLive *m, int eid);
int gm_mobs_take_sheep_world_event(
    GmMobLive *m, int *x, int *y, int *z, int *data);
int gm_mobs_set_recent_hit_state(
    GmMobLive *m, int eid, int recently_hit, int attacking_player);
/* Cold exact-state hook. Entity.isBurning is fire_ticks > 0 for represented
 * non-fire-immune living entities. */
int gm_mobs_set_entity_fire_ticks(
    GmMobLive *m, int eid, int fire_ticks);
/* Cold exact-state hook for the represented authoritative ridden-pig effect.
 * Duration is decremented after that tick's fire/lava damage phase. */
int gm_mobs_set_pig_server_fire_resistance(
    GmMobLive *m, int eid, int duration);
/* Exact authoritative NetHandlerPlayServer vehicle-move contact boundary.
 * The packet's -1e-6 grounded move resets server fall distance, then cactus
 * callback damage precedes generic flammable-contact damage/counter handling
 * for fire or lava, followed by
 * the wet burning cleanup. The wet sound consumes two server entity floats.
 * Call immediately before gm_mobs_tick for the matching server base tick. */
int gm_mobs_pig_packet_contact_exact(
    GmMobLive *m, int eid, int cactus_contact, int flammable_contact,
    int wet_contact,
    uint64_t *math_random_seed48);
/* Same represented stationary packet boundary with block contacts derived
 * from the mounted pig's current shared client/server pose. Moving packets use
 * the independent runtime mover below. */
int gm_mobs_pig_packet_contact_world_exact(
    GmMobLive *m, const struct Chunk *window, int ox, int oz, int eid,
    uint64_t *math_random_seed48);
/* Bounded dry processVehicleMove transition. It covers finite horizontal and
 * vertical accepted movement, the >100 speed rejection, solid-collision
 * rollback, and Entity.move contact side effects at the temporary AABB. */
int gm_mobs_pig_packet_move_dry_exact(
    GmMobLive *m, const struct Chunk *window, int ox, int oz, int eid,
    double target_x, double target_y, double target_z,
    float target_yaw, float target_pitch,
    GmPigVehicleMoveResult *out);
/* Runtime form of the bounded dry transition. It mutates only the independent
 * authoritative pig body and never the client EwStore/AABB. */
int gm_mobs_pig_packet_move_runtime_dry_exact(
    GmMobLive *m, const struct Chunk *window, int ox, int oz, int eid,
    double target_x, double target_y, double target_z,
    float target_yaw, float target_pitch,
    uint64_t *math_random_seed48);
int gm_mobs_get_pig_client_packet_pose(
    const GmMobLive *m, int *eid, double *x, double *y, double *z,
    float *yaw, float *pitch);
/* Client NetHandlerPlayClient.handleMoveVehicle pose application. It updates
 * only the mounted client vehicle pose/AABB and preserves its motion and
 * collision state. */
int gm_mobs_pig_apply_client_vehicle_correction(
    GmMobLive *m, int eid, double x, double y, double z,
    float yaw, float pitch);
int gm_mobs_get_pig_vehicle_server_state(
    const GmMobLive *m, GmPigVehicleServerState *out);
int gm_mobs_get_pig_vehicle_move_checkpoint(
    const GmMobLive *m, GmPigVehicleMoveCheckpoint *out);
int gm_mobs_get_pig_packet_contact_checkpoint(
    const GmMobLive *m, GmPigPacketContactCheckpoint *out);
int gm_mobs_set_blaze_height_state(
    GmMobLive *m, int eid, int update_time, float height_offset);
int gm_mobs_damage_near(GmMobLive *m,double x,double y,double z,double radius,
                        float damage,GmLiveSim *drops);
int gm_mobs_take_explosion(GmMobLive *m,double *x,double *y,double *z);
/* Consume pending fireball. Returns kind 3 (small/blaze) or 5 (large/ghast), else 0. */
int gm_mobs_take_fireball(GmMobLive *m,double *x,double *y,double *z,
                          double *vx,double *vy,double *vz);
void gm_mobs_spawn_xp(GmMobLive *m,double x,double y,double z,int value);
int gm_mobs_spawn_xp_exact(GmMobLive *m, double x, double y, double z,
                           double vx, double vy, double vz, int value,
                           int eid, int age, int pickup_delay, int color,
                           int target_color);
int gm_mobs_spawn_xp_split_exact(
    GmMobLive *m, double x, double y, double z, int dimension,
    int total, uint64_t *math_random_seed48, int *next_entity_id);
int gm_mobs_spawn_xp_split_constructed_exact(
    GmMobLive *m, double x, double y, double z, int dimension,
    int total, uint64_t *math_random_seed48,
    uint64_t *entity_seed_generator_seed48,
    uint64_t *server_uuid_random_seed48, int *next_entity_id);
int gm_mobs_loaded_order_count(const GmMobLive *m);
int gm_mobs_loaded_order_get(
    const GmMobLive *m, int index, int *eid, int *kind);
int gm_mobs_tick_update_order_get(
    const GmMobLive *m, int index, int *eid);
/* Structure Template's Entity NBT boundary. Capture resolves a live EID from
 * either the living or XP store. Spawn returns the new entity EID, or -1. */
int gm_mobs_template_entity_capture(
    const GmMobLive *m, int eid, GmMobTemplateEntity *out);
int gm_mobs_template_entity_valid(const GmMobTemplateEntity *entity);
int gm_mobs_template_entity_spawn(
    GmMobLive *m, const GmMobTemplateEntity *entity, int new_eid);
/* Keep non-living XP entities active when hostile/passive mob AI is disabled. */
void gm_mobs_tick_xp(GmMobLive *m, GmWorld *w, struct PsvPlayer *p,
                     int ox, int oz);
void gm_mobs_tick_xp_from_eid(
    GmMobLive *m, GmWorld *w, struct PsvPlayer *p,
    int ox, int oz, int first_eid);
/* Tick locked NoAI living fixtures and XP without natural spawn/AI work. */
void gm_mobs_tick_controlled(GmMobLive *m, GmWorld *w,
                             const struct Chunk *window,
                             struct PsvPlayer *p, int ox, int oz,
                             int dimension, const GmWorldClock *clock,
                             int do_mob_loot, GmLiveSim *drops,
                             uint64_t *world_random_seed48,
                             uint64_t *math_random_seed48,
                             int *next_entity_id);
float gm_mobs_player_attack_strength(
        const GmMobLive *m, const struct PsvPlayer *player,
        double attack_speed_multiplier);
float gm_mobs_player_attack_amount_undefined(
        GmMobLive *m, const struct PsvPlayer *player,
        float attack_damage_bonus, double attack_speed_multiplier,
        int *full_strength, int *sprint_knockback);
/* Register/update a TileEntityMobSpawner. entity_type is EW_TYPE_*. */
int gm_spawner_potentials_reserve(
    GmSpawnerPotential **potentials, int *capacity, int need);
int gm_mobs_register_spawner(GmMobLive *m,int x,int y,int z,int entity_type);
int gm_mobs_spawner_set_state(
    GmMobLive *m, int x, int y, int z, int entity_type,
    int delay, int min_delay, int max_delay, int spawn_count,
    int max_nearby, int activate_range, int spawn_range);
int gm_mobs_spawner_add_potential(
    GmMobLive *m, int x, int y, int z, int entity_type, int weight);
/* Tick one registered block spawner. Returns -1 when no tile is registered,
 * otherwise the number of entities admitted by World.spawnEntity. */
int gm_mobs_tick_spawner_at(
    GmMobLive *m, GmWorld *world, int x, int y, int z,
    double player_x, double player_y, double player_z,
    uint64_t *world_random_seed48, uint64_t *math_random_seed48,
    uint64_t *entity_seed_generator_seed48,
    uint64_t *server_uuid_random_seed48, int *next_entity_id,
    const GmNbtBlob *spawn_data);
/* Shared MobSpawnerBaseLogic candidate path used by both block and minecart
 * spawners. Returns -1 when the class-aware nearby cap resets the spawner,
 * 0 when the candidate is rejected, and 1 when it joins the world. */
int gm_mobs_spawn_spawner_candidate(
    GmMobLive *m, GmWorld *world, int entity_type,
    int origin_x, int origin_y, int origin_z,
    int spawn_range, int max_nearby,
    double x, double y, double z,
    double player_x, double player_y, double player_z,
    uint64_t *world_random_seed48, uint64_t *math_random_seed48,
    uint64_t *entity_seed_generator_seed48,
    uint64_t *server_uuid_random_seed48, int *next_entity_id,
    const GmNbtBlob *entity_nbt, int default_entity_nbt);
/* Place a boat at world coords (oak boat item). Returns slot or -1. */
int gm_mobs_place_boat(GmMobLive *m,double x,double y,double z,float yaw);
int gm_mobs_place_boat_type(
    GmMobLive *m,double x,double y,double z,float yaw,int boat_variant);
/* Exact gravity-free boat used by parked Java-vs-magma fixtures. */
int gm_mobs_spawn_boat_exact(GmMobLive *m,int eid,
                             double x,double y,double z,float yaw);
int gm_mobs_spawn_boat_type_exact(GmMobLive *m,int eid,
                                  double x,double y,double z,float yaw,
                                  int boat_variant);
/* Player use on nearby boat: mount. Returns 1 if mounted. */
int gm_mobs_boat_mount(GmMobLive *m,struct PsvPlayer *player,int ox,int oz);
int gm_mobs_boat_set_position_rotation_direct(
    GmMobLive *m, int eid, double x, double y, double z,
    float yaw, float pitch);
int gm_mobs_boat_reconstruct_portal(
    GmMobLive *m, int slot, int new_eid, int target_dimension,
    double x, double y, double z, double vx, double vy, double vz,
    float yaw, uint64_t constructor_seed48);
int gm_mobs_boat_mount_living(GmMobLive *m, int boat_eid, int living_eid);
int gm_mobs_boat_collide_nearby(GmMobLive *m, int boat_eid);
int gm_mobs_boat_update_passengers(
    GmMobLive *m, int boat_eid, const struct McSinTable *sin_table);
/* Dismount if riding. */
void gm_mobs_boat_dismount(GmMobLive *m,struct PsvPlayer *player,int ox,int oz);
int gm_mobs_boat_riding(const GmMobLive *m);

/* EntityRenderer.getMouseOver entity leg. The ray is already in mob/world
 * coordinates and max_distance is 3.0 for survival entity interaction. */
int gm_mobs_raycast_entity(
    const GmMobLive *m, int dimension,
    double ex, double ey, double ez, double dx, double dy, double dz,
    double max_distance, int *eid, int *type, double *distance);

/* Forge ItemShears.itemInteractionForEntity on one represented sheep. Returns
 * 2 when wool was emitted, 1 when shears handled an ineligible sheep, 0 for a
 * non-shears/non-sheep request, and -1 when the bounded exact-drop store
 * cannot represent the otherwise-valid transition atomically. */
int gm_mobs_shear_sheep(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot,
    uint64_t *shear_random_seed48, uint64_t *math_random_seed48,
    GmLiveSim *drops, int *next_entity_id);

/* EntityWolf/EntityOcelot owner interactions. The represented player is the
 * owner when owner is nonzero. Ocelot tame success consumes world_random for
 * its skin; wolf paths leave that cursor untouched. */
int gm_mobs_tameable_interact(
    GmMobLive *m, int eid, IsrInv *inventory, int hand_slot,
    int creative, int owner, JavaRandom *world_random);
int gm_mobs_set_tameable_state(
    GmMobLive *m, int eid, int tamed, int sitting, int owner,
    int variant, float health);
int gm_mobs_restore_tameable_state(
        GmMobLive *m, int eid, int tamed, int sitting, int owner,
        int variant, int growing_age, int living_sound_time,
        uint64_t seed48, int have_next_gaussian, double next_gaussian);
int gm_mobs_set_rabbit_state(
        GmMobLive *m, int eid, int rabbit_type, int carrot_ticks);
int gm_mobs_get_rabbit_state(
        const GmMobLive *m, int eid, int *rabbit_type, int *jump_ticks,
        int *jump_duration, int *move_duration, int *carrot_ticks,
        double *move_speed);
int gm_mobs_get_tameable_state(
    const GmMobLive *m, int eid, int *tamed, int *sitting, int *owner,
    int *variant, float *health);
int gm_mobs_sitting_ocelot_over(
    const GmMobLive *m, int dimension, int x, int y, int z);

#endif
