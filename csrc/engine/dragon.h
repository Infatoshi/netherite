/* Ender Dragon's server-side flight state and the seven independently placed
 * hit boxes. The owning world tick calls dragon_tick once per server tick. */
#ifndef NETHERITE_DRAGON_H
#define NETHERITE_DRAGON_H

#include "det.h"
struct world;
struct sw_entity;

struct dragon_part_state {
    int entity_id;
    double x, y, z;
    float width, height;
};

#define DRAGON_MAX_CRYSTALS 64
struct dragon_crystal_state {
    int entity_id;
    int64_t uuid_msb, uuid_lsb;
    double x, y, z;
    float yaw;              /* setLocationAndAngles' yaw, the NBT's rotation */
    double mx, my, mz;      /* motion: only an explosion's push ever sets it */
    det_rng rand;
    int inner_rotation, health, dead, in_world;
    uint64_t chunk_stamp;   /* its place in its chunk section's entity list */
    int order_key;          /* a spawn check's candidate id it joined ahead of (serverreplay.c) */
};

struct dragon_state {
    int entity_id;
    int64_t uuid_msb, uuid_lsb;
    double x, y, z, prev_x, prev_y, prev_z;
    double bb_min_x, bb_max_x, bb_min_y, bb_min_z, bb_max_z;
    double mx, my, mz;
    float yaw, prev_yaw, anim, prev_anim, yaw_velocity, render_yaw;
    double tx, ty, tz;
    int force_target, slowed, ring_index, ticks, hurt_time, death_time, death_ticks;
    float health;
    int hurt_resistant_time, dead_flag;
    float last_damage, prev_health;
    det_rng rand;
    int has_target, living_sound_time, dead;
    int is_air_borne;       /* Entity.isAirBorne: knockBack sets it, the tracker's update block clears it */
    int in_water, first_update; /* Entity.inWater, firstUpdate: handleWaterMovement's splash */
    int air;                /* EntityLivingBase's DataWatcher air (1) */
    int has_player;
    double player_x, player_y, player_z, player_min_y;
    double player_mx, player_my, player_mz;
    float player_health, player_last_damage;
    int player_hurt_time, player_hurt_resistant_time;
    det_rng player_rand;
    struct dragon_crystal_state crystals[DRAGON_MAX_CRYSTALS];
    int n_crystals, healing_crystal_index;
    int mob_griefing;
    struct world *world;
    det_state *det;
    void (*spawn_xp)(void *ctx, double x, double y, double z, int amount);
    void *spawn_xp_ctx;
    void (*crystal_explode)(void *ctx, double x, double y, double z);
    void *crystal_explode_ctx;
    /* A world of entities (the replay): collideWithEntities over the living
     * entities a wing's box (x0..z1) reaches, pushed away from the body's
     * centre (cx, cz), and attackEntitiesInList over the ones the head's box
     * reaches. NULL: the probe's one player above. */
    void (*wing_push)(void *ctx, const double box[6], double cx, double cz);
    void (*head_bite)(void *ctx, const double box[6]);
    void *world_ctx;
    double ring[64][3];
    struct dragon_part_state part[7]; /* head, body, tail 1-3, wings 1-2 */
    /* the replay's chunk bookkeeping: the chunk it was last added to */
    int chunk_x, chunk_y, chunk_z;
    uint64_t chunk_stamp;
};

void dragon_tick(struct dragon_state *d);
/* nw_env->cfg.dragon_negative_draw (env.h): one extra draw at tick 1 */
void dragon_init(struct dragon_state *d, struct world *w, det_state *det,
                 double x, double y, double z, float yaw);
void dragon_adopt(struct dragon_state *d, struct world *w, det_state *det,
                  const struct sw_entity *entity);
int dragon_crystal_init(struct dragon_state *d, det_state *det,
                        double x, double y, double z);
int dragon_crystal_adopt(struct dragon_state *d, const struct sw_entity *entity);
void dragon_crystal_tick(struct dragon_state *d, int index);
/* EntityDragon.attackEntityFromPart; 1 when this hit took the last health
 * (EntityLivingBase.onDeath runs inside it: the caller credits the killer) */
int dragon_attack_part(struct dragon_state *d, int part, float amount, int player_source);
/* The seven parts placed from the position, the yaw and the ring buffer
 * (the tail of onLivingUpdate; no bite or push without a world_ctx). */
void dragon_update_parts(struct dragon_state *d);

/* The client's copy (WorldClient): setPositionAndRotation2's target, the
 * pitch the server never turns, the DataWatcher health. */
struct dragon_interp {
    double new_x, new_y, new_z, new_yaw, new_pitch;
    int incr;
    float pitch, health;
};
/* EntityDragon's update on the client world: onDeathUpdate's rise and turn
 * while the health is 0, else onLivingUpdate's client branch (the ring
 * buffer, the three-step interpolation, the parts). */
void dragon_client_tick(struct dragon_state *d, struct dragon_interp *ip);
void dragon_crystal_attack(struct dragon_state *d, int index);

#endif
