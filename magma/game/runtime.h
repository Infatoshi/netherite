#ifndef MAGMA_GAME_RUNTIME_H
#define MAGMA_GAME_RUNTIME_H

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
#include "game/villager_trade.h"
#include "game/dragon_live.h"
#include "game/container_live.h"
#include "mc_gamerules.h"
#include "game/nbt_blob.h"
#include "fishing.h"

#define GM_RUNTIME_FURNACES 16
/* Growable chest TE table: starts at this capacity, doubles when full.
 * Never evicts a live TE while its block 54 still exists. */
#define GM_RUNTIME_CHESTS_INITIAL 64
/* Static inventory tiles begin with dispenser/dropper. The growable cold pool
 * is reused by later brewing/hopper/shulker comparator slices; no idle tick
 * scans it. */
#define GM_RUNTIME_STATIC_CONTAINERS_INITIAL 16
#define GM_RUNTIME_STATIC_CONTAINERS_MAX 256
#define GM_RUNTIME_STATIC_CONTAINER_SLOTS 27
/* Inert command-block state is cold capsule data. Command execution remains
 * a separate slice, so this pool has no tick hook. */
#define GM_RUNTIME_COMMAND_BLOCKS_INITIAL 16
#define GM_RUNTIME_COMMAND_BLOCKS_MAX 256
#define GM_RUNTIME_FLOWER_POTS_INITIAL 16
#define GM_RUNTIME_FLOWER_POTS_MAX 256
#define GM_RUNTIME_SKULLS_INITIAL 16
#define GM_RUNTIME_SKULLS_MAX 256
#define GM_RUNTIME_TAGGED_ITEMS_INITIAL 8
#define GM_RUNTIME_TAGGED_ITEMS_MAX 256
#define GM_RUNTIME_ITEM_FRAMES_INITIAL 16
#define GM_RUNTIME_ITEM_FRAMES_MAX 256
#define GM_RUNTIME_PROJECTILES 32
#define GM_RUNTIME_AREA_EFFECT_CLOUDS 16
#define GM_RUNTIME_FALLING_BLOCKS 16
#define GM_RUNTIME_WORLD_EVENT_CAPACITY GM_RUNTIME_FALLING_BLOCKS
#define GM_RUNTIME_PRIMED_TNT 16
#define GM_RUNTIME_END_CRYSTALS 16
#define GM_RUNTIME_PISTONS 64
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
#define GM_RUNTIME_MINECARTS 32
#define GM_RUNTIME_END_GATEWAYS 32
#define GM_RUNTIME_END_CITIES 64
#define GM_RUNTIME_VILLAGE_RESIDENTS 256
#define GM_RUNTIME_SCHEDULED_TICKS 4096
/* Compat alias for tests that still reference the old fixed size. */
#define GM_RUNTIME_CHESTS GM_RUNTIME_CHESTS_INITIAL
typedef struct {
    int active, type, age;
    int eid;
    int controlled_stationary;
    int fire_ticks;
    int shooting_living;
    int shooter_eid;
    int potion_item; /* TB_SPLASH_POTION or TB_LINGERING_POTION */
    int potion_type; /* TB_PT_* for thrown splash/lingering potions */
    float yaw, pitch;
    double x, y, z, vx, vy, vz;
    double ax, ay, az; /* EntityFireball acceleration; zero for arrows/items */
} GmRuntimeProjectile;
typedef struct {
    int eid;
    int potion_type;
    PtAreaEffectCloud state;
    double x, y, z;
    int mob_eid[EW_MAX_ENTITIES];
    int mob_next_application[EW_MAX_ENTITIES];
} GmRuntimeAreaEffectCloud;
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
    int hurt_entities;
    int dont_set_block;
    uint64_t random_seed48;
    int origin_x, origin_y, origin_z;
    double x, y, z;
    double vx, vy, vz;
    int bounding_box_valid;
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
    double x, y, z;
    double vx, vy, vz;
} GmRuntimePrimedTnt;
typedef struct {
    int active;
    int dimension;
    int eid;
    int inner_rotation;
    int show_bottom;
    int has_beam;
    int beam_x, beam_y, beam_z;
    double x, y, z;
} GmRuntimeEndCrystal;
typedef struct {
    int active;
    int dimension;
    int eid;
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
    int attached_player;
    int flight, explosion_count;
    int large_blast, twinkle;
    uint64_t blast_random_seed48, twinkle_random_seed48;
    uint64_t random_seed48;
    int random_have_gaussian;
    double random_gaussian;
    float yaw, pitch;
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
    GM_SOUND_COUNT
};
typedef struct {
    uint64_t seq;
    int sound, category, eid, dimension;
    int relative, delay_ticks;
    double x, y, z;
    float volume, pitch;
} GmRuntimeSoundEvent;
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
    int reverse, rolling_amplitude, rolling_direction;
    float damage, yaw, pitch;
    double x, y, z, vx, vy, vz;
    int fuel, tnt_fuse, hopper_enabled, transfer_cooldown;
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
    FurnaceLive state;
} GmRuntimeFurnace;
typedef struct {
    int active, wx, wy, wz;
    ChestLive state;
} GmRuntimeChest;
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
} GmRuntimeStaticContainer;
typedef struct {
    int active;
    int dimension;
    int wx, wy, wz;
    int block;
    int success_count;
} GmRuntimeCommandBlock;
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
    int type, rotation;
    GmNbtBlob owner_profile;
} GmRuntimeSkull;
typedef struct {
    int active;
    int eid;
    int item;
    int size;
    ICStack slots[GM_RUNTIME_STATIC_CONTAINER_SLOTS];
    GmNbtBlob tag;
} GmRuntimeTaggedItem;

typedef struct {
    int active;
    int dimension;
    int eid;
    double x, y, z;
    int hanging_x, hanging_y, hanging_z;
    int facing;
    int item, count, meta;
    int rotation;
} GmRuntimeItemFrame;
typedef struct {
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
} GmRuntimeEndPopulationChunk;
typedef struct {
    int x, y, z;
    int eid;
    unsigned char profession;
    GmVillagerTrade trade;
} GmRuntimeVillageResident;

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
    McGameRules gamerules;
    GmWorldClock clock;
    GmLiveSim entities;
    GmMobLive mobs;
    GmDragonLive dragon;
    GmRuntimeProjectile projectiles[GM_RUNTIME_PROJECTILES];
    GmRuntimeAreaEffectCloud area_effect_clouds[
        GM_RUNTIME_AREA_EFFECT_CLOUDS];
    int area_effect_cloud_count;
    /* Exact BlockFalling slice. Fixed storage plus falling_block_count keeps
     * the ordinary no-falling-entity tick path to a single branch. */
    GmRuntimeFallingBlock falling_blocks[GM_RUNTIME_FALLING_BLOCKS];
    int falling_block_count;
    /* World.playEvent payloads are a transient ordered observation stream.
     * The fixed falling-block pool bounds the currently represented producer
     * to at most one terminal event per active entity and phase. */
    GmRuntimeWorldEvent world_events[GM_RUNTIME_WORLD_EVENT_CAPACITY];
    int world_event_head;
    int world_event_count;
    uint64_t world_event_next_seq;
    uint64_t world_event_dropped;
    /* BlockFalling's process-global worldgen switch. Ordinary live runtime
     * leaves this false; controlled population/oracle boundaries may enable
     * the synchronous scan-and-place path explicitly. */
    int falling_instant;
    /* Powered TNT is active-set driven. The ordinary world path pays one
     * count branch; constructor and motion cursors exist only after priming. */
    GmRuntimePrimedTnt primed_tnt[GM_RUNTIME_PRIMED_TNT];
    int primed_tnt_count;
    /* Standalone End crystals are separate from the fixed dragon-arena
     * crystals so saved fixtures can exist in any dimension. */
    GmRuntimeEndCrystal end_crystals[GM_RUNTIME_END_CRYSTALS];
    int end_crystal_count;
    GmRuntimeLightning lightning[GM_RUNTIME_LIGHTNING];
    int lightning_count;
    int last_lightning_bolt;
    GmRuntimeWeatherEvent weather_events[GM_RUNTIME_WEATHER_EVENTS];
    int weather_event_head;
    int weather_event_count;
    uint64_t weather_event_next_seq;
    uint64_t weather_event_dropped;
    GmRuntimeFirework fireworks[GM_RUNTIME_FIREWORKS];
    int firework_count;
    GmRuntimeFireworkEvent firework_events[GM_RUNTIME_FIREWORK_EVENTS];
    int firework_event_head, firework_event_count;
    uint64_t firework_event_next_seq, firework_event_dropped;
    GmRuntimeFireworkTwinkle
        firework_twinkles[GM_RUNTIME_FIREWORK_TWINKLES];
    int firework_twinkle_count;
    GmRuntimeFishHook fish_hook;
    GmRuntimeFishEvent fish_events[GM_RUNTIME_FISH_EVENTS];
    int fish_event_head, fish_event_count;
    uint64_t fish_event_next_seq, fish_event_dropped;
    /* Ordered, allocation-free client sound seam. Simulation producers append
     * resolved sound identity, category, source, volume, and pitch. Playback
     * is an optional interactive consumer and never feeds simulation state. */
    GmRuntimeSoundEvent sound_events[GM_RUNTIME_SOUND_EVENTS];
    int sound_event_head, sound_event_count;
    uint64_t sound_event_next_seq, sound_event_dropped;
    uint64_t sound_random_seed48;
    uint64_t sound_mob_next_seq;
    /* Rail entities are a fixed active set. With no minecarts the base tick
     * pays one count branch; rail path work scales only with live carts. */
    GmRuntimeMinecart minecarts[GM_RUNTIME_MINECARTS];
    int minecart_count;
    GmRuntimeEndGateway end_gateways[GM_RUNTIME_END_GATEWAYS];
    int end_gateway_count;
    int end_gateway_order[20];
    int end_gateway_order_count;
    struct { int chunk_x, chunk_z; } end_cities[GM_RUNTIME_END_CITIES];
    int end_city_count;
    int end_city_scan_x, end_city_scan_z;
    /* End decorate() is cold, chunk-discovery work. The dynamic set prevents
     * a populated chunk from consuming RNG or replacing chorus on revisit. */
    GmRuntimeEndPopulationChunk *end_population_chunks;
    int end_population_count, end_population_cap;
    int end_population_scan_x, end_population_scan_z;
    void *end_population_noise;
    /* StructureVillagePieces spawns each resident once while its piece is
     * placed. Retain claimed sites separately from the live slot so death or
     * leaving/re-entering the scan window cannot respawn that resident. */
    GmRuntimeVillageResident
        village_residents[GM_RUNTIME_VILLAGE_RESIDENTS];
    int village_resident_count;
    int villages_enabled;
    int village_scan_x, village_scan_z;
    long village_scan_builds;
    /* Moving piston tile entities are active-set driven. Exact slices cover
     * an empty normal-piston head extension and a straight line of up to 12
     * stones; broader reactions/branching and collision rules are admitted
     * by later fixtures. */
    GmRuntimePiston pistons[GM_RUNTIME_PISTONS];
    int piston_count;
    /* BlockPistonBase.onBlockAdded queues a block event when a moving source
     * settles back into a powered base. The event executes in the following
     * WorldServer tick before tile entities advance. */
    int piston_recheck_count;
    int piston_recheck_x[GM_RUNTIME_PISTONS];
    int piston_recheck_y[GM_RUNTIME_PISTONS];
    int piston_recheck_z[GM_RUNTIME_PISTONS];
    int next_entity_id;
    /* Internal 48-bit java.util.Random seed (the AtomicLong payload, not the
     * public constructor seed). Random block callbacks consume this exactly. */
    uint64_t world_random_seed48;
    int world_random_have_gaussian;
    double world_random_gaussian;
    /* java.lang.Math's process-global Random cursor. EntityItem construction
     * consumes this independently of World.rand. */
    uint64_t math_random_seed48;
    /* Block.RANDOM is another process-global java.util.Random cursor. It is
     * causal for randomized block drops and is not serialized by world NBT. */
    uint64_t block_random_seed48;
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
    int32_t controlled_input_before_update_lcg;
    int controlled_input_entity_id;
    uint64_t controlled_input_world_seed48;
    uint64_t controlled_input_math_seed48;
    uint64_t controlled_input_block_seed48;
    int32_t controlled_input_update_lcg;
    int bow_ticks,bow_drawing;
    int player_air;        /* Entity AIR data parameter, vanilla default 300 */
    int player_fire_ticks; /* Entity.fire, setFire(seconds) stores seconds*20 */
    int player_position_update_ticks; /* EntityPlayerSP stationary packet cursor */
    int player_position_packet_pending; /* queued CPacketPlayer.Position */
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
    int player_death_time;     /* EntityLivingBase.deathTime */
    int score;                 /* EntityPlayer.getScore (GuiGameOver line) */
    int death_screen_ticks;    /* GuiGameOver.enableButtonsTimer */
    int quit_to_title;         /* Title Screen confirmed / episode end */
    int dimension;
    GmWorldType world_type;       /* WorldInfo terrain type, shared by dimensions */
    int portal_time, portal_cooldown;
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
    int mob_griefing;
    int controlled_mobs_enabled;
    int server_attack_pending;
    int server_shear_pending;
    int server_shear_eid;
    int server_shear_hand; /* 0 main, 1 offhand */
    int server_feed_animal_pending;
    int server_feed_animal_eid;
    int server_feed_animal_hand; /* 0 main, 1 offhand */
    int server_pig_boost_pending;
    int server_pig_boost_hand; /* 0 main, 1 offhand */
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
    int container; /* 0 player, 1 workbench, 2 furnace, 3 chest, 4 brewing, 5 enchanting */
    int container_wx, container_wy, container_wz;
    int active_furnace;
    int active_chest;
    int active_static_container;
    ICStack craft_grid[9]; /* live craft matrix (container_live slot ids 36..44) */
    GmRuntimeFurnace furnaces[GM_RUNTIME_FURNACES];
    GmRuntimeChest *chests; /* growable; capacity in chests_cap */
    int chests_cap;
    /* Allocated only when a represented static inventory/record tile is
     * restored or created. Comparator queries reach it by exact coordinate. */
    GmRuntimeStaticContainer *static_containers;
    int static_containers_cap;
    GmRuntimeCommandBlock *command_blocks;
    int command_blocks_cap;
    GmRuntimeFlowerPot *flower_pots;
    int flower_pots_cap;
    GmRuntimeSkull *skulls;
    int skulls_cap;
    GmRuntimeTaggedItem *tagged_items;
    int tagged_items_cap;
    GmRuntimeItemFrame *item_frames;
    int item_frames_cap;
    /* Sorted, allocate-once pending block updates. The idle hot path is one
     * count check; insertion/restoration work is cold or active-set driven. */
    GmRuntimeScheduledTick *scheduled_ticks;
    int scheduled_tick_count;
    long long scheduled_tick_next_order;
    /* Vanilla keeps a per-world chronological list of redstone-torch off
     * transitions. It is cold-path, so allocate it only when a torch actually
     * toggles and prune it only from a torch callback. */
    GmRuntimeRedstoneTorchToggle *redstone_torch_toggles;
    int redstone_torch_toggle_count;
    int redstone_torch_toggle_cap;
    /* Comparator output is TileEntity state, not block metadata. This fixed
     * table is touched only by comparator load/edit/query/callback paths. */
    GmRuntimeComparator comparators[GM_RUNTIME_COMPARATORS];
    int comparator_count;
    /* Tickable daylight-detector tiles use a fixed active set. The ordinary
     * world path pays one count branch and never scans blocks. */
    GmRuntimeDaylightDetector
        daylight_detectors[GM_RUNTIME_DAYLIGHT_DETECTORS];
    int daylight_detector_count;
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
    int gui_view_container;  /* 0 player, 1 workbench, 2 furnace, 3 chest, 4 brewing */
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
    int tape_fire, tape_creative, tape_hurt_time, tape_max_hurt_time;
    float tape_hurt_yaw, tape_attack_cooldown;
    int tape_potion_count;
    GmPotionEffectView tape_potions[GM_MAX_POTION_EFFECTS];
    /* Recorded ForgeHooks.getTotalArmorValue. -1 = the tape did not carry it
     * (pre-2026-07-29 schema); the view then keeps the item-derived guess. */
    int tape_armor_points;
} GmRuntime;

int  gm_runtime_init(GmRuntime *r, const GmConfig *cfg, char *err, int err_cap);
void gm_runtime_destroy(GmRuntime *r);
/* The only authoritative survival transition used by interactive and harness play. */
void gm_runtime_tick(GmRuntime *r, GmAction action);
/* Cold generated-entity synchronization. Production calls this after a
 * recenter/new population window; tests may call it after gm_world_ensure. */
int gm_runtime_sync_village_residents(GmRuntime *r);
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
 * (container 0/1/2/3/4; mx/my = vanilla ScaledResolution coords). Render-only. */
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
int gm_runtime_spawn_item_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, int item, int count, int meta,
    int age, int pickup_delay, int controlled_stationary);
int gm_runtime_spawn_falling_fixture(
    GmRuntime *r, int eid, int block, int meta, int fall_time,
    double x, double y, double z, double vx, double vy, double vz,
    int no_gravity, int no_ground);
/* Cold oracle hook: advance only the same falling-entity phase used by the
 * public tick. This permits an immediate EntityFallingBlock.onUpdate boundary
 * without also aging later controlled living entities. */
void gm_runtime_tick_falling_fixture_phase(GmRuntime *r);
int gm_runtime_spawn_arrow_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, int controlled_stationary,
    int fire_ticks);
int gm_runtime_spawn_primed_tnt_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, int fuse);
int gm_runtime_spawn_end_crystal_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    int inner_rotation, int show_bottom, int has_beam,
    int beam_x, int beam_y, int beam_z);
int gm_runtime_spawn_small_fireball_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, double ax, double ay, double az);
int gm_runtime_spawn_potion_fixture(
    GmRuntime *r, int eid, int potion_item, int potion_type,
    double x, double y, double z, double vx, double vy, double vz,
    int age);
int gm_runtime_spawn_area_effect_cloud_fixture(
    GmRuntime *r, int eid, int potion_type, double x, double y, double z,
    int age, int duration, int wait_time, int reapplication_delay,
    float radius, float radius_on_use, float radius_per_tick,
    int next_application);
int gm_runtime_spawn_mob_fixture(
    GmRuntime *r, int type, int eid, double x, double y, double z,
    double vx, double vy, double vz, float yaw, float health, int no_ai,
    int hurt_time, int death_time, int hurt_resistant_time);
int gm_runtime_spawn_villager_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    double vx, double vy, double vz, float yaw, float health,
    int hurt_time, int death_time, int hurt_resistant_time,
    int profession, int living_sound_time,
    uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
int gm_runtime_set_mob_fire_ticks(
    GmRuntime *r, int eid, int fire_ticks);
int gm_runtime_set_mob_air(GmRuntime *r, int eid, int air);
int gm_runtime_set_sheep_state(
    GmRuntime *r, int eid, int fleece_color, int sheared);
int gm_runtime_set_mob_growing_age(
    GmRuntime *r, int eid, int growing_age);
int gm_runtime_set_mob_recent_hit_state(
    GmRuntime *r, int eid, int recently_hit, int attacking_player);
int gm_runtime_spawn_boat_fixture(
    GmRuntime *r, int eid, double x, double y, double z, float yaw);
int gm_runtime_set_entity_id_cursor(GmRuntime *r, int next_entity_id);
int gm_runtime_set_world_random_seed48(GmRuntime *r, uint64_t seed48);
int gm_runtime_set_world_random_gaussian(
    GmRuntime *r, int have_next_gaussian, double next_gaussian);
int gm_runtime_set_math_random_seed48(GmRuntime *r, uint64_t seed48);
int gm_runtime_set_player_random_seed48(GmRuntime *r, uint64_t seed48);
int gm_runtime_set_next_explosion_random_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_set_next_fireball_random_state(
    GmRuntime *r, uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
int gm_runtime_set_next_potion_random_state(
    GmRuntime *r, uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
int gm_runtime_set_next_falling_random_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_set_next_shears_random_seed48(
    GmRuntime *r, uint64_t seed48);
int gm_runtime_set_block_random_seed48(GmRuntime *r, uint64_t seed48);
int gm_runtime_set_world_update_lcg(GmRuntime *r, int32_t update_lcg);
void gm_runtime_capture_controlled_input(GmRuntime *r);
void gm_runtime_begin_controlled_input(GmRuntime *r);
int gm_runtime_random_tick_block(
    GmRuntime *r, int x, int y, int z, int expected_block);
int gm_runtime_random_tick_selection(
    GmRuntime *r, int x, int y, int z, int expected_block,
    int lcg_advances_before);
/* Exact pending-update subset currently accepted by the capsule. Absolute
 * time/order are captured from NextTickListEntry. */
int gm_runtime_schedule_tick(
    GmRuntime *r, int x, int y, int z, int block, long long time,
    int priority, long long order);
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
int gm_runtime_chest_count(const GmRuntime *r);
int gm_runtime_chest_get(
    const GmRuntime *r, int index, GmRuntimeChest *out);
int gm_runtime_furnace_set_slot(
    GmRuntime *r, int dimension, int x, int y, int z,
    int slot, int item, int count, int meta,
    int burn_time, int current_burn_time,
        int cook_time, int total_cook_time);
int gm_runtime_brewing_set_slot(
        GmRuntime *r, int dimension, int x, int y, int z,
        int slot, int item, int count, int meta,
        int brew_time, int fuel);
void gm_runtime_brewing_changed(GmRuntime *r);
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
int gm_runtime_static_container_count(const GmRuntime *r);
int gm_runtime_static_container_get(
    const GmRuntime *r, int index, GmRuntimeStaticContainer *out);
int gm_runtime_command_block_set_success(
    GmRuntime *r, int dimension, int x, int y, int z, int success_count);
int gm_runtime_command_block_count(const GmRuntime *r);
int gm_runtime_command_block_get(
    const GmRuntime *r, int index, GmRuntimeCommandBlock *out);
int gm_runtime_flower_pot_set(
    GmRuntime *r, int dimension, int x, int y, int z, int item, int meta);
int gm_runtime_flower_pot_count(const GmRuntime *r);
int gm_runtime_flower_pot_get(
    const GmRuntime *r, int index, GmRuntimeFlowerPot *out);
int gm_runtime_skull_set(
    GmRuntime *r, int dimension, int x, int y, int z,
    int type, int rotation);
int gm_runtime_skull_set_profile_nbt(
    GmRuntime *r, int dimension, int x, int y, int z,
    int type, int rotation, const void *profile_nbt, size_t profile_nbt_len);
int gm_runtime_skull_count(const GmRuntime *r);
int gm_runtime_skull_get(
    const GmRuntime *r, int index, GmRuntimeSkull *out);
int gm_runtime_tagged_item_get_by_eid(
    const GmRuntime *r, int eid, GmRuntimeTaggedItem *out);
int gm_runtime_item_frame_set(
    GmRuntime *r, int dimension, int eid,
    double x, double y, double z,
    int hanging_x, int hanging_y, int hanging_z,
    int facing, int item, int count, int meta, int rotation);
int gm_runtime_item_frame_count(const GmRuntime *r);
int gm_runtime_item_frame_get(
    const GmRuntime *r, int index, GmRuntimeItemFrame *out);
int gm_runtime_redstone_torch_toggle_add(
    GmRuntime *r, int x, int y, int z, long long time);
int gm_runtime_redstone_torch_toggle_count(const GmRuntime *r);
int gm_runtime_redstone_torch_toggle_get(
    const GmRuntime *r, int index, GmRuntimeRedstoneTorchToggle *out);
/* Interactive / harness respawn (GuiGameOver Respawn button / SPacketRespawn).
 * Restores health to 20, clears dead + fire/hurt, resets death_screen_ticks. */
void gm_runtime_respawn(GmRuntime *r);
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
int gm_runtime_harvest_block(GmRuntime *r, int x, int y, int z);
/* Snapshot initialization: canonical cell replacement with no fluid/plant
 * mutation side effects. Must run before the first replay tick. */
int gm_runtime_load_block(GmRuntime *r, int x, int y, int z, int id, int meta);
int gm_runtime_snapshot_region(GmRuntime *r, int ccx, int ccz, int radius);
int gm_runtime_load_block_dim(GmRuntime *r, int dimension, int x, int y, int z,
                              int id, int meta);
int gm_runtime_snapshot_region_dim(GmRuntime *r, int dimension,
                                   int ccx, int ccz, int radius);
int gm_runtime_finalize_block_snapshot_dim(
    GmRuntime *r, int dimension, int ccx, int ccz, int radius);
int gm_runtime_load_sky_light_dim(
    GmRuntime *r, int dimension, int x, int y, int z, int value);
int gm_runtime_finalize_sky_light_snapshot_dim(
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
int gm_runtime_firework_audio_fixture(
    GmRuntime *r, int eid, double x, double y, double z,
    int explosion_count, int large_blast, int twinkle,
    uint64_t blast_seed48, uint64_t twinkle_seed48);
int gm_runtime_spawn_firework(
    GmRuntime *r, double x, double y, double z,
    int flight, int explosion_count, int attached_player);
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
int gm_runtime_sound_event_count(const GmRuntime *r);
int gm_runtime_sound_event_get(
    const GmRuntime *r, int index, GmRuntimeSoundEvent *out);
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
int gm_runtime_spawn_minecart_fixture(
    GmRuntime *r, int kind, int eid,
    double x, double y, double z, double vx, double vy, double vz,
    float yaw);
int gm_runtime_minecart_get(
    const GmRuntime *r, int index, GmRuntimeMinecart *out);
int gm_runtime_minecart_set_slot(
    GmRuntime *r, int eid, int slot, int item, int count, int meta);
int gm_runtime_minecart_set_state(
    GmRuntime *r, int eid, int fuel, double push_x, double push_z,
    int tnt_fuse, int hopper_enabled, int transfer_cooldown);
int gm_runtime_minecart_set_base_state(
    GmRuntime *r, int eid, int reverse, int rolling_amplitude,
    int rolling_direction, float damage, float pitch);
int gm_runtime_minecart_set_random_state(
    GmRuntime *r, int eid, uint64_t seed48,
    int have_next_gaussian, double next_gaussian);
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
/* ContainerEnchantment.enchantItem for one of the three offer rows. */
int gm_runtime_enchant_click(GmRuntime *r, int button);
int gm_runtime_furnace_insert(GmRuntime *r, int furnace_slot,
                              int inventory_slot, int amount);
int gm_runtime_furnace_extract(GmRuntime *r, int furnace_slot, int amount);

#endif
