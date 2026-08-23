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
    unsigned xp_pickups;
    McOrb xp_orbs[GM_XP_ORBS];
    signed char orb_dimension[GM_XP_ORBS];
    int next_orb_id;
    int creeper_fuse[EW_MAX_ENTITIES];
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
    float passive_render_yaw[EW_MAX_ENTITIES]; /* EntityLivingBase.renderYawOffset */
    float passive_prev_head_yaw[EW_MAX_ENTITIES]; /* EntityBodyHelper.prevRenderYawHead */
    int passive_body_ticks[EW_MAX_ENTITIES];      /* EntityBodyHelper.rotationTickCounter */
    unsigned char passive_sheared[EW_MAX_ENTITIES];
    int fire_ticks[EW_MAX_ENTITIES];             /* daylight burn */
    int despawn_ticks[EW_MAX_ENTITIES];          /* ticks spent >32 blocks from player */
    int anger[EW_MAX_ENTITIES];                  /* pigman angerLevel ticks */
    unsigned char size[EW_MAX_ENTITIES];         /* slime/magma size 1/2/4 */
    float squish_amount[EW_MAX_ENTITIES];        /* EntitySlime.squishAmount */
    float squish_factor[EW_MAX_ENTITIES];        /* EntitySlime.squishFactor */
    unsigned char was_on_ground[EW_MAX_ENTITIES]; /* EntitySlime.wasOnGround */
    int jump_delay[EW_MAX_ENTITIES];             /* slime/magma jump cooldown */
    int charge[EW_MAX_ENTITIES];                 /* ghast charge (-40..20); blaze AIFireballAttack.attackStep */
    unsigned char blaze_on_fire[EW_MAX_ENTITIES]; /* EntityBlaze ON_FIRE / isCharged display bit */
    int boat_damage[EW_MAX_ENTITIES];            /* boat hit-to-break counter */
    int boat_ride;                               /* slot player rides, or -1 */
    GmSpawnerTE spawners[GM_SPAWNERS];
    int player_hurt_resistant;                    /* EntityLivingBase.hurtResistantTime */
    float player_last_damage;                     /* EntityLivingBase.lastDamage */
    int player_wither_ticks;                      /* PotionEffect(WITHER, 200, 0) */
    int explosion_pending;
    double explosion_x, explosion_y, explosion_z;
    /* Pending fireball spawn consumed by runtime: 0=none, 3=small (blaze), 5=large (ghast). */
    int fireball_pending;
    double fireball_x, fireball_y, fireball_z;
    double fireball_vx, fireball_vy, fireball_vz;
    /* det_entity_rng: live java.util.Random cursor (internal seed48) + AI hydrate.
     * Unused when the knob is off; hash streams stay on the default path. */
    unsigned long long ent_jr_seed[EW_MAX_ENTITIES];
    int living_sound_time[EW_MAX_ENTITIES];
    int entity_age[EW_MAX_ENTITIES];
    int chicken_egg[EW_MAX_ENTITIES];
    /* det_entity_rng PathNavigateGround: world-coord PathPoints from PathFinder. */
    short det_nav_x[EW_MAX_ENTITIES][48];
    short det_nav_y[EW_MAX_ENTITIES][48];
    short det_nav_z[EW_MAX_ENTITIES][48];
    unsigned char det_nav_n[EW_MAX_ENTITIES];
    unsigned char det_nav_i[EW_MAX_ENTITIES];
    /* PathNavigate.totalTicks / ticksAtLastPos / lastPosCheck (checkForStuck). */
    int det_nav_ticks[EW_MAX_ENTITIES];
    int det_nav_stuck_at[EW_MAX_ENTITIES];
    double det_nav_stuck_x[EW_MAX_ENTITIES];
    double det_nav_stuck_y[EW_MAX_ENTITIES];
    double det_nav_stuck_z[EW_MAX_ENTITIES];
    /* Previous tape pl. Tape pl is client pose after ServerTick END (includes
     * this tick's knockback). LookHelper and tryMoveToEntityLiving share it. */
    double look_px, look_py, look_pz;
    unsigned char look_have;
    /* PathNavigate.getPathSearchRange: FOLLOW_RANGE attribute base.
     * Summoned mobs skip onInitialSpawn, so no gaussian spawn bonus. */
    float det_follow[EW_MAX_ENTITIES];
    /* det_entity_rng hostile hydrate (zombie/skeleton/creeper). Unused off-knob. */
    int det_target_tick[EW_MAX_ENTITIES];
    unsigned int det_target_tasks[EW_MAX_ENTITIES];
    unsigned char det_has_target[EW_MAX_ENTITIES];
    int det_melee_delay[EW_MAX_ENTITIES];
    double det_melee_tx[EW_MAX_ENTITIES];
    double det_melee_ty[EW_MAX_ENTITIES];
    double det_melee_tz[EW_MAX_ENTITIES];
    int det_see_time[EW_MAX_ENTITIES];
    int det_strafe_time[EW_MAX_ENTITIES];
    int det_bow_attack_time[EW_MAX_ENTITIES];
    unsigned char det_strafe_cw[EW_MAX_ENTITIES];
    unsigned char det_strafe_back[EW_MAX_ENTITIES];
    signed char det_cstate[EW_MAX_ENTITIES];
    int det_raise_arm[EW_MAX_ENTITIES];
    /* AbstractSkeleton ctor setCombatTask: empty hand -> melee. /summon NBT
     * skips onInitialSpawn (no bow). Unused off-knob. */
    unsigned char det_skel_melee[EW_MAX_ENTITIES];
    /* Entity.move carries AABB; rebuilding from pos ± width/2 each tick is 1 ULP. */
    unsigned char det_box_on[EW_MAX_ENTITIES];
    McAABB det_box[EW_MAX_ENTITIES];
    /* det_entity_rng extras for DIM-1 hostiles (unused when the knob is off). */
    unsigned char det_persist[EW_MAX_ENTITIES];
    unsigned char ent_jr_have_gauss[EW_MAX_ENTITIES];
    double ent_jr_gauss[EW_MAX_ENTITIES];
    int blaze_hot[EW_MAX_ENTITIES];     /* EntityBlaze.heightOffsetUpdateTime */
    float blaze_hof[EW_MAX_ENTITIES];   /* EntityBlaze.heightOffset */
    /* EntityLivingBase.hurtTime / deathTime. Live tick does not yet age
     * these; snapshot v3 still carries them so a later spine port can restore. */
    int hurt_time[EW_MAX_ENTITIES];
    int death_time[EW_MAX_ENTITIES];
    /* EntityBoat.deltaRotation / boatGlide. Not in EwStore. */
    float boat_delta_rot[EW_MAX_ENTITIES];
    float boat_glide[EW_MAX_ENTITIES];
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
/* Place a tape-hydrated passive with a live Entity.rand cursor. det_entity_rng. */
int gm_mobs_det_place(GmMobLive *m, int eid, int type,
                      double x, double y, double z, float yaw, float pitch, float head_yaw,
                      unsigned long long seed48, int living_sound, int entity_age, int task_tick,
                      unsigned tasks, int watch, int idle, double idle_x, double idle_z,
                      int eat, int egg, int on_ground, float render_yaw, float prev_head_yaw,
                      int body_ticks, unsigned long long seed48_init);
/* Additive hostile hydrate after gm_mobs_det_place. No-op for passives. */
void gm_mobs_det_hydrate_hostile(GmMobLive *m, int slot,
                                int ttt, unsigned ttasks, int tgt, int fuse, int mdelay,
                                int see, int stime, int atime, int scw, int sback, int cstate);
/* Optional tape extras: gaussian cache, blaze hover timer, PersistenceRequired, anger. */
void gm_mobs_det_rng_extra(GmMobLive *m, int slot, int have_gauss, double gauss,
                           int height_off_time, float height_off, int persist, int anger);
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
                  float boat_forward, float boat_strafe, int mob_griefing);
/* Entity.move / travel only. Zero AI intents. Used when --mobs is off so
 * loaded snapshot living slots still fall, collide, and damp. */
void gm_mobs_tick_spine(GmMobLive *m, GmWorld *world,
                        const struct McSinTable *sin_table);
/* --mobs off: EntityCreeper.onUpdate ignited fuse (explosion_live.h). */
void gm_mobs_tick_creeper_fuse(GmMobLive *m);
int gm_mobs_fill_views(const GmMobLive *m, GmEntityView *out, int max);
int gm_mobs_alive(const GmMobLive *m);
int gm_mobs_living_count(const GmMobLive *m);
int gm_mobs_damage_near(GmMobLive *m,double x,double y,double z,double radius,
                        float damage,GmLiveSim *drops);
int gm_mobs_take_explosion(GmMobLive *m,double *x,double *y,double *z);
/* Consume pending fireball. Returns kind 3 (small/blaze) or 5 (large/ghast), else 0. */
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
int gm_mobs_boat_status(const GmMobLive *m, struct GmWorld *w, int slot);
void gm_mobs_tick_orbs(GmMobLive *m, struct GmWorld *w, struct PsvPlayer *p,
                       int ox, int oz);
void gm_mobs_tick_boats(GmMobLive *m, struct GmWorld *w, struct PsvPlayer *p,
                        int ox, int oz, float forward, float strafe);

/* Packed .bsnp v3 mob trailer (RlSnapMob in blaze/env/blaze_snapshot.h).
 * Export walks occupied slots 1..EW_MAX_ENTITIES-1 in slot order. */
struct RlSnapMob;
unsigned gm_mobs_export_snap(const GmMobLive *m, struct RlSnapMob *out,
                             unsigned cap);
void gm_mobs_import_snap(GmMobLive *m, const struct RlSnapMob *in, unsigned n);
struct RlSnapOrb;
unsigned gm_mobs_export_orbs(const GmMobLive *m, struct RlSnapOrb *out,
                             unsigned cap);
void gm_mobs_import_orbs(GmMobLive *m, const struct RlSnapOrb *in, unsigned n);

#endif
