/* The entity AI for the farm animals: EntityAITasks and the tasks, the
 * look/move/jump/body helpers, EntitySenses, PathNavigate and
 * RandomPositionGenerator. See ai.c. */
#ifndef NETHERITE_AI_H
#define NETHERITE_AI_H

#include "living.h"
#include "pathfind.h"

/* EntityAINavigate's path search scratch (ai.c): the finder and its output,
 * one shared by every search that is not nested in another. */
struct nav_scratch {
    struct pf f;
    int *out;   /* the environment's (arena.h pf_store.out): PF_MAX_POINTS points */
};

void ai_setup_kind(struct living *l, det_state *det);
void ai_add_break_door(struct living *l);
void ai_add_leash_restriction(struct living *l);
void ai_tasks_update(struct living *l, struct ai_tasks *t);
void ai_remove_task(struct living *l, int cls);
void ai_add_task_arrow_attack(struct living *l, int priority, double speed, int min_interval, int max_interval, float max_dist);
void ai_add_task_attack_on_collide(struct living *l, int priority, double speed, int long_memory);

/* EntityLookHelper. */
void look_helper_update(struct living *l);
void look_set_position_with_entity(struct living *l, struct living *other, float dyaw, float dpitch);
void look_set_position(struct living *l, double x, double y, double z, float dyaw, float dpitch);

/* EntityMoveHelper. */
void move_helper_set_move_to(struct living *l, double x, double y, double z, double speed);
void move_helper_update(struct living *l);

/* EntityJumpHelper. */
void jump_helper_do(struct living *l);

/* EntityBodyHelper. */
void body_helper_update(struct living *l);
float body_helper_a(float a, float b, float limit);

/* EntitySenses. */
void senses_clear(struct living *l);
int senses_can_see(struct living *l, struct living *other);

/* EntityCreature's own pathToEntity, which World.getPathEntityToEntity and
 * getEntityPathToXYZ return: the same pathfinder the navigator uses, without
 * the navigator's flags (the four booleans are PathFinder's constructor
 * arguments in order). */
pathref ai_creature_path_to_entity(struct living *l, struct living *other, float max_dist,
                                            int door_open, int door_closed, int avoid_water, int can_swim);
pathref ai_creature_path_to_xyz(struct living *l, int x, int y, int z, float max_dist,
                                         int door_open, int door_closed, int avoid_water, int can_swim);

/* PathNavigate. */
void nav_update(struct living *l);
int nav_try_move_to_xyz(struct living *l, double x, double y, double z, double speed);
int nav_try_move_to_entity(struct living *l, struct living *other, double speed);
/* tryMoveToEntityLiving over a target given as posX, boundingBox.minY, posZ
 * (a leash knot). */
int nav_try_move_to_point(struct living *l, int other_id, double x, double min_y, double z, double speed);
int nav_no_path(struct living *l);
void nav_clear_path(struct living *l);

#endif
