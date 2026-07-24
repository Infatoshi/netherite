#ifndef MAGMA_GAME_MOB_LIVE_H
#define MAGMA_GAME_MOB_LIVE_H

#include "game/game.h"
#include "game/live_sim.h"
#include "entity_hostile_spine.h"
#include "entity_xp_orb.h"
#include "inventory_stack_rules.h"

#define GM_XP_ORBS 64
#define GM_SPAWNERS 64
/* Living-slot product capacity (slot 0 reserved). Matches EW_MAX_ENTITIES-1. */
#define GM_MOB_CAPACITY (EW_MAX_ENTITIES - 1)

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
    int player_attack_cooldown;
    int xp_total;
    McOrb xp_orbs[GM_XP_ORBS];
    signed char orb_dimension[GM_XP_ORBS];
    int next_orb_id;
    int creeper_fuse[EW_MAX_ENTITIES];
    unsigned char hurt_aggro[EW_MAX_ENTITIES];   /* revenge target set */
    int panic_ticks[EW_MAX_ENTITIES];            /* passive flee timer after damage */
    int fire_ticks[EW_MAX_ENTITIES];             /* daylight burn */
    int despawn_ticks[EW_MAX_ENTITIES];          /* ticks spent >32 blocks from player */
    int anger[EW_MAX_ENTITIES];                  /* pigman angerLevel ticks */
    unsigned char size[EW_MAX_ENTITIES];         /* slime/magma size 1/2/4 */
    float squish_amount[EW_MAX_ENTITIES];        /* EntitySlime.squishAmount */
    float squish_factor[EW_MAX_ENTITIES];        /* EntitySlime.squishFactor */
    int jump_delay[EW_MAX_ENTITIES];             /* slime/magma jump cooldown */
    int charge[EW_MAX_ENTITIES];                 /* ghast fireball charge (-40..20) */
    int boat_damage[EW_MAX_ENTITIES];            /* boat hit-to-break counter */
    int boat_ride;                               /* slot player rides, or -1 */
    GmSpawnerTE spawners[GM_SPAWNERS];
    int player_hurt_resistant;                    /* EntityLivingBase.hurtResistantTime */
    float player_last_damage;                     /* EntityLivingBase.lastDamage */
    int player_wither_ticks;                      /* PotionEffect(WITHER, 200, 0) */
    int explosion_pending;
    double explosion_x, explosion_y, explosion_z;
    /* Pending large fireball spawn (ghast) consumed by runtime. */
    int fireball_pending;
    double fireball_x, fireball_y, fireball_z;
    double fireball_vx, fireball_vy, fireball_vz;
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
    GM_ENTITY_BOAT = EW_TYPE_BOAT,
    GM_ENTITY_XP_ORB = 21
};

void gm_mobs_init(GmMobLive *m, long long seed);
/* Component/test hook. Runtime progression never calls this directly. */
int gm_mobs_spawn(GmMobLive *m, int type, double x, double y, double z);
/* Spawn with slime/magma size (1,2,4). Other types ignore size. */
int gm_mobs_spawn_sized(GmMobLive *m, int type, double x, double y, double z, int size);
/* Returns nonzero when attack is aimed at a mob, including cooldown ticks. */
int gm_mobs_player_attack(GmMobLive *m, const struct PsvPlayer *player,
                          int ox, int oz, GmLiveSim *drops);
/* Shared EntityLivingBase.attackEntityFrom hurt-resistance path. Dragon
 * contact and tape-replay authoritative mob contacts use the same gate as
 * live hostile melee. When player_inv is non-NULL and bypass_armor is 0,
 * CombatRules armor absorb + InventoryPlayer.damageArmor run first. */
int gm_mobs_attack_player(GmMobLive *m, struct PvStats *vitals,
                          struct IsrInv *player_inv, float amount,
                          int bypass_armor);
void gm_mobs_player_hurt_tick(GmMobLive *m);
/* boat_forward/boat_strafe: player WASD while mounted (GmAction.forward/strafe).
 * Zero when not riding; runtime passes the action and suppresses player walk. */
void gm_mobs_tick(GmMobLive *m, GmWorld *world, const struct McSinTable *sin_table,
                  struct PsvPlayer *player, struct PvStats *vitals,
                  int ox, int oz, int dimension, long long world_time, GmLiveSim *drops,
                  float boat_forward, float boat_strafe);
int gm_mobs_fill_views(const GmMobLive *m, GmEntityView *out, int max);
int gm_mobs_alive(const GmMobLive *m);
int gm_mobs_living_count(const GmMobLive *m);
int gm_mobs_damage_near(GmMobLive *m,double x,double y,double z,double radius,
                        float damage,GmLiveSim *drops);
int gm_mobs_take_explosion(GmMobLive *m,double *x,double *y,double *z);
int gm_mobs_take_fireball(GmMobLive *m,double *x,double *y,double *z,
                          double *vx,double *vy,double *vz);
void gm_mobs_spawn_xp(GmMobLive *m,double x,double y,double z,int value);
/* Register/update a TileEntityMobSpawner. entity_type is EW_TYPE_*. */
int gm_mobs_register_spawner(GmMobLive *m,int x,int y,int z,int entity_type);
/* Place a boat at world coords (oak boat item). Returns slot or -1. */
int gm_mobs_place_boat(GmMobLive *m,double x,double y,double z,float yaw);
/* Player use on nearby boat: mount. Returns 1 if mounted. */
int gm_mobs_boat_mount(GmMobLive *m,struct PsvPlayer *player,int ox,int oz);
/* Dismount if riding. */
void gm_mobs_boat_dismount(GmMobLive *m,struct PsvPlayer *player,int ox,int oz);
int gm_mobs_boat_riding(const GmMobLive *m);

#endif
