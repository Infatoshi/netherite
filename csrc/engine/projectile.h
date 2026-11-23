#ifndef NETHERITE_PROJECTILE_H
#define NETHERITE_PROJECTILE_H

#include "item_entity.h"

struct living;

ie_ent *proj_spawn_arrow(ie_world *iew, double x, double y, double z,
                         double dir_x, double dir_y, double dir_z,
                         float speed, float inaccuracy);

ie_ent *proj_spawn_arrow_target(ie_world *iew, struct living *shooter, struct living *target,
                                float speed, float inaccuracy);

ie_ent *proj_spawn_throwable(ie_world *iew, int kind, double x, double y, double z,
                             double dir_x, double dir_y, double dir_z,
                             float speed, float inaccuracy, int potion_damage);

ie_ent *proj_spawn_fireball(ie_world *iew, int kind, double x, double y, double z,
                            double accel_x, double accel_y, double accel_z);

ie_ent *proj_spawn_arrow_shooter(ie_world *iew, struct living *shooter, int shooter_is_player,
                                 double x, double y, double z, float eye_height,
                                 float yaw, float pitch, float velocity);

ie_ent *proj_spawn_throwable_shooter(ie_world *iew, int kind, struct living *shooter, int shooter_is_player,
                                     double x, double y, double z, float eye_height,
                                     float yaw, float pitch);

void proj_arrow_heading(ie_ent *en, double dir_x, double dir_y, double dir_z,
                        float speed, float inaccuracy);

void proj_throwable_heading(ie_ent *en, double dir_x, double dir_y, double dir_z,
                            float speed, float inaccuracy);

void projectile_update(ie_world *iew, ie_ent *en);

/* EntityFireball.attackEntityFrom from a DamageSource whose getEntity() is
 * src (a living) or, when src is NULL, a non-living entity (has_entity) or
 * none. Returns attackEntityFrom's answer. */
int proj_fireball_attack_from(ie_ent *fb, struct living *src, int has_entity);

/* EntityArrow.onUpdate's entity hit on a non-living entity of its own pool
 * (a large fireball, a primed TNT): the damage roll's critical draw, the
 * burning arrow's setFire(5), attackEntityFrom, then the bowhit pitch and
 * setDead, or the turn back. */
void proj_arrow_hit_nonliving(ie_ent *arrow, ie_ent *other);

ie_ent *proj_spawn_ender_eye(ie_world *iew, double x, double y, double z,
                             double target_x, int target_y, double target_z);
/* EntityEnderEye(World) for a saved eye (EntityList.createEntityFromNBT) */
ie_ent *proj_load_ender_eye(ie_world *iew, double x, double y, double z);
ie_ent *proj_throw_ender_eye(ie_world *iew, double player_x, double player_y,
                             double player_z, float player_y_offset);
ie_ent *proj_use_ender_eye(ie_world *iew, double player_x, double player_y,
                           double player_z, float player_y_offset,
                           int creative, int *stack_count);

#endif
