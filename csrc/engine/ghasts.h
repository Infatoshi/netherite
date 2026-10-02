/* EntityGhast and the EntityLargeFireball it fires, over the living and item
 * pools. See ghasts.c. */
#ifndef NETHERITE_GHASTS_H
#define NETHERITE_GHASTS_H

#include "living.h"

struct expl_extra;

/* The probe player: a bare EntityPlayer the ghasts see through
 * playerEntities, the fireballs and explosions damage, and whose state the
 * records carry every tick. Never ticked, so its timers only move when a
 * damage path writes them. */
struct gh_player {
    int entity_id;
    int64_t uuid_msb, uuid_lsb;
    det_rng rand;
    double pos_x, pos_y, pos_z;
    double motion_x, motion_y, motion_z;
    float rotation_yaw, rotation_pitch;
    float health, prev_health, absorption;
    int hurt_time, max_hurt_time, death_time, hurt_resistant_time;
    float last_damage;
    int entity_age, is_dead;
    uint8_t velocity_changed;   /* Entity.velocityChanged, setBeenAttacked */
};

/* The ghast lane's world: the an_world the entities live in plus the player. */
struct gh_world {
    struct an_world *an;
    struct gh_player player;
    int has_walls;             /* the walls run: no line of sight, no damage */
    /* the explosion's entities outside the living pool (the whole-server
     * replay's item pool and its falling and hanging entities), appended to
     * the fireball blast's entity pass; NULL for none */
    int (*blast_other)(void *ctx, struct aabb box, struct expl_extra *out, int cap);
    void *blast_other_ctx;
    /* the whole-server replay's player: its living twin (the EntityPlayerMP
     * the blast damages and pushes), 0 for the probe's own player */
    lref player_livh;
};

/* EntityGhast's constructor halves living_init does not cover: the size, the
 * fire immunity and the xp value. */
void ghast_construct(struct living *l, det_state *det);

/* EntityGhast.getLook(1.0F): the exact-1.0 branch of EntityLivingBase.getLook,
 * the angles through MathHelper's sin/cos table. */
void ghast_look_vec(struct living *l, double *x, double *y, double *z);

/* EntityGhast.updateEntityActionState, the old-AI branch living.c dispatches
 * to. Includes this.despawnEntity() (the ghast's own call, no super). */
void ghast_update_entity_action_state(struct living *l);

/* EntityGhast.attackEntityFrom: the fireball-from-player branch never fires in
 * a probe (no player hits a fireball back), so this is the EntityLivingBase
 * body with the ghast's death drops. */
int ghast_attack_entity_from(struct living *l, struct living *attacker, int source, float amount, det_state *det);

/* EntityGhast.dropFewItems. */
void ghast_drop_few_items(struct living *l, int looting);

/* The collision pass extension: Java's collideWithNearbyEntities walks every
 * entity, so the ghast also pushes against the fireballs and the player. */
void ghast_collide_with_nearby_entities(struct living *l, const struct aabb *box);

/* The player's box for the fireball's entity scan (projectile.c). */
int ghast_player_bb(struct ie_world *iew, struct aabb *out);

/* World.getClosestVulnerablePlayerToEntity over the one probe player: the
 * ghast's target when it is alive, vulnerable and within range, else NULL. */
struct gh_player *gh_closest_vulnerable_player(struct an_world *an, struct living *l, double range);

/* The probe player's damage paths (EntityPlayer.attackEntityFrom and the
 * EntityLivingBase body under it). shooter is the fireball's shootingEntity
 * for the knockback, NULL for the explosion's entity-less source. */
int gh_player_attack_entity_from(struct gh_world *gw, int source, float amount,
                                 struct living *shooter, det_state *det);

/* The DamageSource unblockable set, shared with living.c's damageEntity. */
int living_dmg_unblockable(int source);

/* The negative checks (run_config, env.h; the gates leave both 0):
 * ghast_negative_attack runs the attack threshold at 15 (vanilla 20),
 * ghast_negative_waypoint the waypoint range at 12.0F (vanilla 16.0F). */

/* EntityLargeFireball.onImpact, the iew->on_fireball_impact the test wires. */
void gh_fireball_impact(struct ie_world *iew, struct ie_ent *en, void *hit, int hit_kind);

/* EntityLargeFireball.writeEntityToNBT over the Entity base, the canonical
 * tree the probe hashes. */
void ghast_fireball_write_nbt(struct ie_ent *en, struct nbt *tag);
uint64_t ghast_fireball_nbt_hash(struct ie_ent *en);
void ghast_fireball_nbt_text(struct ie_ent *en, char **out);

/* The natural-spawn construction: the record's fields, then the constructor
 * and onSpawnWithEgg. */
struct living *gh_spawn_ghast(struct an_world *an, int spawn_index, double x, double y, double z,
                              float yaw, float pitch);

/* World.updateEntities' pass over the probe's own list: an entity dead at its
 * visit is skipped, the spawns land at the end and tick the same pass, and a
 * dead entity leaves the list and its chunk. The removals come back in visit
 * order, health as the float bits for a living and -1 otherwise. */
struct gh_removal {
    int tick, si, entity_id, kind;
    float health;   /* -1.0F when the entity is not living */
    int fire;
};

void gh_tick(struct an_world *an, int tick, struct gh_removal *out, int max_out, int *n_out);

#endif
