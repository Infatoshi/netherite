#ifndef MAGMA_GAME_MOB_LIVE_H
#define MAGMA_GAME_MOB_LIVE_H

#include "game/game.h"
#include "game/live_sim.h"
#include "entity_hostile_spine.h"
#include "entity_blaze_fireball.h"
#include "entity_xp_orb.h"
#include "inventory_stack_rules.h"
#include "potion_throwable.h"

#define GM_SPAWNERS 64
/* Living-slot product capacity (slot 0 reserved). Matches EW_MAX_ENTITIES-1. */
#define GM_MOB_CAPACITY (EW_MAX_ENTITIES - 1)
#define GM_XP_ORBS GM_MOB_CAPACITY
/* Initial living + XP dispatches plus the bounded child/XP append tail. */
#define GM_MOB_UPDATE_ORDER_CAPACITY (2 * GM_MOB_CAPACITY + GM_XP_ORBS)
#define GM_MOB_LOADED_ORDER_CAPACITY \
    (2 * (GM_MOB_CAPACITY + GM_XP_ORBS))
#define GM_MOB_EFFECT_CAPACITY 16

enum {
    GM_MOB_LOADED_LIVING = 1,
    GM_MOB_LOADED_XP = 2
};

typedef struct {
    int eid;
    unsigned int generation;
    unsigned char kind;
    unsigned char slot;
} GmMobLoadedRef;
#define GM_BLAZE_SHOT_QUEUE 3
#define GM_MOB_EVENT_CAPACITY (GM_MOB_CAPACITY * 3)
#define GM_MOB_TERMINAL_PARTICLE_COUNT 20
#define GM_MOB_TERMINAL_PARTICLE_CAPACITY GM_MOB_CAPACITY
#define GM_MOB_PARTICLE_BATCH_MAX 7
#define GM_MOB_PARTICLE_BATCH_CAPACITY GM_MOB_EVENT_CAPACITY
#define GM_PIG_COLLISION_BOXES 512

enum {
    GM_DAMAGE_SOURCE_GENERIC = 1,
    GM_DAMAGE_SOURCE_FIRE = 2,
    GM_DAMAGE_SOURCE_FALL = 4,
    GM_DAMAGE_SOURCE_EXPLOSION = 8,
    GM_DAMAGE_SOURCE_PROJECTILE = 16
};

typedef struct {
    double x, y, z;
    double aim_x, aim_y, aim_z;
} GmBlazeShot;

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

enum {
    GM_MOB_EVENT_ENTITY_STATUS = 1,
    GM_MOB_EVENT_SOUND = 2
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
    GM_MOB_SOUND_PIG_SADDLE = 13
};

typedef struct {
    uint64_t seq;
    int kind;
    int eid;
    int data;
    double x, y, z;
    float volume, pitch;
} GmMobEvent;

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

/* TileEntityMobSpawner live state (entity id + delay countdown). */
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
    int activate_range;
} GmSpawnerTE;

typedef struct {
    EwStore a, b;
    int current;
    int active_dimension;
    signed char entity_dimension[EW_MAX_ENTITIES];
    long long seed, tick;
    int next_id;
    int player_ticks_since_last_swing;             /* EntityPlayer cooldown cursor */
    JavaRandom player_random;                      /* EntityPlayerMP Entity.rand */
    int xp_total;
    McOrb xp_orbs[GM_XP_ORBS];
    signed char orb_dimension[GM_XP_ORBS];
    int next_orb_id;
    McAABB xp_collision_boxes[GM_XP_ORBS];
    int xp_collision_count;
    int tick_update_order[GM_MOB_UPDATE_ORDER_CAPACITY];
    int tick_update_order_count;
    GmMobLoadedRef loaded_order[GM_MOB_LOADED_ORDER_CAPACITY];
    int loaded_order_count;
    unsigned int living_loaded_generation[EW_MAX_ENTITIES];
    unsigned int xp_loaded_generation[GM_XP_ORBS];
    int creeper_fuse[EW_MAX_ENTITIES];
    unsigned char creeper_powered[EW_MAX_ENTITIES];
    unsigned char hurt_aggro[EW_MAX_ENTITIES];   /* revenge target set */
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
    float passive_head_yaw[EW_MAX_ENTITIES];
    float passive_head_pitch[EW_MAX_ENTITIES];
    unsigned char passive_sheared[EW_MAX_ENTITIES];
    int fire_ticks[EW_MAX_ENTITIES];             /* daylight burn */
    int despawn_ticks[EW_MAX_ENTITIES];          /* ticks spent >32 blocks from player */
    int anger[EW_MAX_ENTITIES];                  /* pigman angerLevel ticks */
    unsigned char blaze_attack_step[EW_MAX_ENTITIES]; /* AIFireballAttack 0..4 */
    unsigned char blaze_attacking[EW_MAX_ENTITIES];
    unsigned char blaze_charged[EW_MAX_ENTITIES];     /* EntityBlaze.ON_FIRE bit */
    unsigned char blaze_shots_pending[EW_MAX_ENTITIES];
    unsigned char blaze_shot_head[EW_MAX_ENTITIES];
    GmBlazeShot blaze_shots[EW_MAX_ENTITIES][GM_BLAZE_SHOT_QUEUE];
    JavaGaussianRandom entity_random[EW_MAX_ENTITIES];
    JavaGaussianRandom entity_server_random[EW_MAX_ENTITIES];
    int entity_ticks_existed[EW_MAX_ENTITIES];
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
    unsigned char sheep_data[EW_MAX_ENTITIES];   /* fleece 0..15 | sheared 0x10 */
    unsigned char villager_profession[EW_MAX_ENTITIES]; /* 0..5 */
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
    int growing_age[EW_MAX_ENTITIES];             /* EntityAgeable; child when < 0 */
    int chicken_time_until_next_egg[EW_MAX_ENTITIES];
    float chicken_wing_rotation[EW_MAX_ENTITIES];
    float chicken_dest_pos[EW_MAX_ENTITIES];
    float chicken_old_flap_speed[EW_MAX_ENTITIES];
    float chicken_old_flap[EW_MAX_ENTITIES];
    float chicken_wing_rot_delta[EW_MAX_ENTITIES];
    unsigned char chicken_jockey[EW_MAX_ENTITIES];
    int sheep_in_love[EW_MAX_ENTITIES];           /* EntityAnimal.inLove */
    int sheep_forced_age[EW_MAX_ENTITIES];        /* EntityAgeable.forcedAge */
    int sheep_forced_age_timer[EW_MAX_ENTITIES];  /* client happy-particle timer */
    unsigned char sheep_bred_by_player[EW_MAX_ENTITIES];
    int sheep_mate_slot[EW_MAX_ENTITIES];
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
    unsigned char was_on_ground[EW_MAX_ENTITIES]; /* EntitySlime.wasOnGround */
    int jump_delay[EW_MAX_ENTITIES];             /* slime/magma jump cooldown */
    int charge[EW_MAX_ENTITIES];                 /* ghast charge (-40..20); blaze AIFireballAttack.attackStep */
    unsigned char blaze_on_fire[EW_MAX_ENTITIES]; /* EntityBlaze ON_FIRE / isCharged display bit */
    float boat_damage[EW_MAX_ENTITIES];          /* EntityBoat DAMAGE_TAKEN */
    unsigned char controlled_no_ai[EW_MAX_ENTITIES]; /* locked oracle fixture */
    unsigned char controlled_block_collisions[EW_MAX_ENTITIES];
    int entity_hurt_resistant[EW_MAX_ENTITIES];
    int entity_hurt_time[EW_MAX_ENTITIES];
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
    int boat_ride;                               /* slot player rides, or -1 */
    int pig_ride;                                /* saddled pig slot, or -1 */
    GmSpawnerTE spawners[GM_SPAWNERS];
    int player_hurt_resistant;                    /* EntityLivingBase.hurtResistantTime */
    int player_hurt_time;                         /* EntityLivingBase.hurtTime */
    float player_last_damage;                     /* EntityLivingBase.lastDamage */
    int player_resistance_amplifier;               /* -1 inactive; MobEffects 11 */
    float player_absorption;                       /* EntityLivingBase gold hearts */
    int player_wither_ticks;                      /* PotionEffect(WITHER, 200, 0) */
    int explosion_pending;
    double explosion_x, explosion_y, explosion_z;
    /* Pending fireball spawn consumed by runtime: 0=none, 3=small (blaze), 5=large (ghast). */
    int fireball_pending;
    double fireball_x, fireball_y, fireball_z;
    double fireball_vx, fireball_vy, fireball_vz;
    /* Allocation-free causal event ring. Producers append only when an event
     * occurs; consumers retain seq and can detect overwritten records. */
    GmMobEvent events[GM_MOB_EVENT_CAPACITY];
    int event_head, event_count;
    uint64_t event_next_seq, event_dropped;
    /* One atomic 20-particle batch per represented terminal living slot. */
    GmMobTerminalParticles
        terminal_particles[GM_MOB_TERMINAL_PARTICLE_CAPACITY];
    int terminal_particle_head, terminal_particle_count;
    uint64_t terminal_particle_next_seq, terminal_particle_dropped;
    GmMobParticleBatch particle_batches[GM_MOB_PARTICLE_BATCH_CAPACITY];
    int particle_batch_head, particle_batch_count;
    uint64_t particle_batch_next_seq, particle_batch_dropped;
    uint64_t sheep_birth_dropped, sheep_breed_xp_dropped;
    GmPigPacketContactCheckpoint pig_packet_contact_checkpoint;
    GmPigVehicleServerState pig_vehicle_server;
    GmPigVehicleMoveCheckpoint pig_vehicle_move_checkpoint;
    /* Cold ridden-pig workspaces. Collectors overwrite [0, count), so init can
     * deliberately leave this trailing storage untouched. */
    McAABB pig_collision_scratch[GM_PIG_COLLISION_BOXES];
    int pig_block_contact_scratch[GM_PIG_COLLISION_BOXES][4];
} GmMobLive;

/* Product type aliases matching EW_TYPE_* / entity_render ER_TYPE_*. */
enum {
    GM_MOB_BLAZE = EW_TYPE_BLAZE,
    GM_MOB_SHEEP = EW_TYPE_SHEEP,
    GM_MOB_PIG = EW_TYPE_PIG,
    GM_MOB_COW = EW_TYPE_COW,
    GM_MOB_CHICKEN = EW_TYPE_CHICKEN,
    GM_MOB_PIGMAN = EW_TYPE_PIGMAN,
    GM_MOB_GHAST = EW_TYPE_GHAST,
    GM_MOB_MAGMA = EW_TYPE_MAGMA,
    GM_MOB_WITHER_SKELETON = EW_TYPE_WITHER_SKELETON,
    GM_MOB_SLIME = EW_TYPE_SLIME,
    GM_MOB_SILVERFISH = EW_TYPE_SILVERFISH,
    GM_MOB_CAVE_SPIDER = EW_TYPE_CAVE_SPIDER,
    GM_MOB_VILLAGER = EW_TYPE_VILLAGER,
    GM_ENTITY_BOAT = EW_TYPE_BOAT,
    GM_ENTITY_XP_ORB = 21
};

void gm_mobs_init(GmMobLive *m, long long seed);
/* Component/test hook. Runtime progression never calls this directly. */
int gm_mobs_spawn(GmMobLive *m, int type, double x, double y, double z);
/* Generated-village resident spawn. Profession is the 1.11.2 career skin
 * selector: 0 farmer, 1 librarian, 2 priest, 3 smith, 4 butcher, 5 nitwit. */
int gm_mobs_spawn_villager(GmMobLive *m, double x, double y, double z,
                           int profession);
/* Spawn with slime/magma size (1,2,4). Other types ignore size. */
int gm_mobs_spawn_sized(GmMobLive *m, int type, double x, double y, double z, int size);
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
/* Cold oracle hook: exact stationary sheep/pig/cow/chicken/villager fixture
 * with Java's id.
 * no_ai=false represents the taskless/gravity-free collision oracle variant. */
int gm_mobs_spawn_exact(GmMobLive *m, int type, int eid,
                        double x, double y, double z,
                        double vx, double vy, double vz,
                        float yaw, float health, int no_ai,
                        int hurt_time, int death_time,
                        int hurt_resistant_time);
/* Returns 2 for accepted damage, 1 for a targeted rejected hit, or 0 for miss. */
int gm_mobs_player_attack(GmMobLive *m, const struct PsvPlayer *player,
                          int ox, int oz,
                          const struct McSinTable *sin_table,
                          GmLiveSim *drops,
                          float attack_damage_bonus,
                          double attack_speed_multiplier,
                          int on_ladder, int in_water, int riding,
                          const GmMobDeathContext *death_context);
/* Exact inner EntityPig.attackEntityFrom(player-source) boundary.  This
 * bypasses player cooldown/weapon logic so a locked fixture can distinguish
 * damage semantics from EntityPlayer.attackTargetEntityWithCurrentItem. */
int gm_mobs_player_damage_pig_exact(
    GmMobLive *m, int eid, double attacker_x, double attacker_z,
    float damage, GmLiveSim *drops,
    const GmMobDeathContext *death_context);
/* EntityPlayerMP.swingArm resets the server cooldown on every arm packet. */
void gm_mobs_player_swing(GmMobLive *m);
/* Shared EntityLivingBase.attackEntityFrom hurt-resistance path. Dragon
 * contact and tape-replay authoritative mob contacts use the same gate as
 * live hostile melee. When player_inv is non-NULL and bypass_armor is 0,
 * CombatRules armor absorb + InventoryPlayer.damageArmor run first. */
int gm_mobs_attack_player(GmMobLive *m, struct PvStats *vitals,
                          struct IsrInv *player_inv, float amount,
                          int bypass_armor);
int gm_mobs_attack_player_source(GmMobLive *m, struct PvStats *vitals,
                                 struct IsrInv *player_inv, float amount,
                                 int bypass_armor, int source_flags);
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
void gm_mobs_tick(GmMobLive *m, GmWorld *world, const struct Chunk *window,
                  const struct McSinTable *sin_table,
                  struct PsvPlayer *player, struct PvStats *vitals,
                  int ox, int oz, int dimension, long long world_time,
                  int mob_griefing, uint64_t *world_random_seed48,
                  uint64_t *math_random_seed48, int *next_entity_id,
                  int do_mob_loot,
                  GmLiveSim *drops,
                  float boat_forward, float boat_strafe);
int gm_mobs_fill_views(const GmMobLive *m, GmEntityView *out, int max);
int gm_mobs_alive(const GmMobLive *m);
int gm_mobs_living_count(const GmMobLive *m);
/* Enumerate represented EntityLivingBase collision boxes in one dimension.
 * Returns the number written, bounded by capacity. Boats and XP orbs are not
 * EntityLivingBase. */
int gm_mobs_living_boxes(
    const GmMobLive *m, int dimension, McAABB *out, int capacity);
/* Bounded EntityFallingBlock living-target leg. Visits active, controlled
 * NoAI sheep, pigs, cows, and chickens in slot order only; returns accepted
 * target count. A fresh accepted source-less hit advances Math once and the
 * target Entity.rand four LCG steps before any loot draws. When do_mob_loot
 * is true, lethal chicken, pig, cow, and adult sheep run their exact 1.11.2
 * loot tables synchronously and append causal
 * EntityItems through next_entity_id. Fresh controlled passive hits append
 * status 2 and their exact hurt/death sound; lethal hits append status 3 after
 * loot. XP remains a later death update concern. Fixed item capacity rejects
 * that one target atomically, including its event records. */
int gm_mobs_falling_anvil_damage_controlled_passives(
    GmMobLive *m, int dimension, const McAABB *falling_box, float damage,
    uint64_t *math_random_seed48, GmLiveSim *drops,
    int *next_entity_id, int do_mob_loot);
/* Oldest-first bounded causal event view. Sequence numbers remain monotonic;
 * event_dropped reports records overwritten before a consumer read them. */
int gm_mobs_event_count(const GmMobLive *m);
int gm_mobs_event_get(const GmMobLive *m, int index, GmMobEvent *out);
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
int gm_mobs_apply_potion_effect(
    GmMobLive *m, int slot, int potion_id, int amplifier, int duration);
int gm_mobs_potion_effect_count(const GmMobLive *m, int slot);
int gm_mobs_potion_effect_get(
    const GmMobLive *m, int slot, int index, PtMobEffect *out);
float gm_mobs_max_health(const GmMobLive *m, int slot);
float gm_mobs_absorption(const GmMobLive *m, int slot);
int gm_mobs_air(const GmMobLive *m, int slot);
int gm_mobs_set_air(GmMobLive *m, int eid, int air);
int gm_mobs_apply_water_potion(
    GmMobLive *m, int slot, GmLiveSim *drops);
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
/* ProjectileHelper entity leg: nearest collidable represented mob/boat AABB,
 * expanded by vanilla's exact 0.30000001192092896. */
int gm_mobs_projectile_intercept(
    const GmMobLive *m, int dimension, int shooter_eid, int include_shooter,
    double sx, double sy, double sz, double ex, double ey, double ez,
    int *slot, double *distance_sq);
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
/* Pop one exact AIFireballAttack event captured before the blaze moves. */
int gm_mobs_take_blaze_shot(GmMobLive *m, int slot, GmBlazeShot *shot);
/* Cold capsule hooks for Entity.rand and EntityBlaze's private float state. */
int gm_mobs_set_entity_random_state(
    GmMobLive *m, int eid, uint64_t seed48, int have_next_gaussian,
    double next_gaussian);
/* Cold save/oracle hook for EntityChicken private state. */
int gm_mobs_set_chicken_state(
    GmMobLive *m, int eid, int time_until_next_egg,
    float wing_rotation, float dest_pos, float old_flap_speed,
    float old_flap, float wing_rot_delta, int chicken_jockey);
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
 * pigs, and chickens. Returns 1 when the selected hand is handled and 0 when
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
 * player riding a pig. Terminal pig retirement only clears pig_ride. */
void gm_mobs_pig_dismount_explicit(
    GmMobLive *m, GmWorld *world, const struct Chunk *window,
    struct PsvPlayer *player, int ox, int oz);
void gm_mobs_pig_dismount(GmMobLive *m);
int gm_mobs_pig_riding(const GmMobLive *m, int *eid);
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
int gm_mobs_loaded_order_count(const GmMobLive *m);
int gm_mobs_loaded_order_get(
    const GmMobLive *m, int index, int *eid, int *kind);
/* Keep non-living XP entities active when hostile/passive mob AI is disabled. */
void gm_mobs_tick_xp(GmMobLive *m, GmWorld *w, struct PsvPlayer *p,
                     int ox, int oz);
/* Tick locked NoAI living fixtures and XP without natural spawn/AI work. */
void gm_mobs_tick_controlled(GmMobLive *m, GmWorld *w,
                             const struct Chunk *window,
                             struct PsvPlayer *p, int ox, int oz,
                             int dimension, int do_mob_loot,
                             uint64_t *world_random_seed48,
                             uint64_t *math_random_seed48,
                             int *next_entity_id);
float gm_mobs_player_attack_strength(
        const GmMobLive *m, const struct PsvPlayer *player,
        double attack_speed_multiplier);
/* Register/update a TileEntityMobSpawner. entity_type is EW_TYPE_*. */
int gm_mobs_register_spawner(GmMobLive *m,int x,int y,int z,int entity_type);
/* Place a boat at world coords (oak boat item). Returns slot or -1. */
int gm_mobs_place_boat(GmMobLive *m,double x,double y,double z,float yaw);
/* Exact gravity-free boat used by parked Java-vs-magma fixtures. */
int gm_mobs_spawn_boat_exact(GmMobLive *m,int eid,
                             double x,double y,double z,float yaw);
/* Player use on nearby boat: mount. Returns 1 if mounted. */
int gm_mobs_boat_mount(GmMobLive *m,struct PsvPlayer *player,int ox,int oz);
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

#endif
