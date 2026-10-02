/* The entity runtime state a snapshot carries beside NBT: Snapshot.java's
 * "rt" key per entity (oracle/harness/netherite/oracle/EntityState.java), every
 * field of the entity's class chain, its DataWatcher and, for an
 * EntityLiving, the AI task lists and the look, move, jump and body helpers
 * and the navigator. A snapshot taken at any tick resumes mid-AI, mid-jump
 * and mid-path only with these, so every loaded entity gets them after the
 * whole list exists (the references are entity ids).
 *
 * Each table row names a Java field (EntityState's key: the MCP name) and
 * the native member it lands in. A Java field with no row is state the
 * native engine derives, never reads or does not model. */
#include "serverreplay.h"
#include "env.h"
#include "snap_runtime.h"
#include "ai.h"
#include "leash.h"
#include "villagers.h"
#include "hostiles_pigman.h"
#include "hostiles_enderman.h"
#include "hostiles_witch.h"
#include "item_entity.h"
#include "entity.h"
#include "fallhang.h"
#include "endfight.h"
#include "dragon.h"
#include "player.h"

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum { RT_F32, RT_F64, RT_I32, RT_U8, RT_LREF, RT_BB, RT_PATH };

struct rt_field {
    const char *name;
    int type;
    size_t off;
};

#define LF(n, t, m) { n, t, offsetof(struct living, m) }

/* Entity, EntityLivingBase, EntityLiving and EntityCreature, then the
 * kinds' own fields. */
static const struct rt_field living_fields[] = {
    LF("posX", RT_F64, e.pos_x), LF("posY", RT_F64, e.pos_y), LF("posZ", RT_F64, e.pos_z),
    LF("prevPosX", RT_F64, e.prev_pos_x), LF("prevPosY", RT_F64, e.prev_pos_y), LF("prevPosZ", RT_F64, e.prev_pos_z),
    LF("lastTickPosX", RT_F64, last_tick_x), LF("lastTickPosY", RT_F64, last_tick_y), LF("lastTickPosZ", RT_F64, last_tick_z),
    LF("motionX", RT_F64, e.motion_x), LF("motionY", RT_F64, e.motion_y), LF("motionZ", RT_F64, e.motion_z),
    LF("rotationYaw", RT_F32, rotation_yaw), LF("rotationPitch", RT_F32, rotation_pitch),
    LF("prevRotationYaw", RT_F32, prev_rotation_yaw), LF("prevRotationPitch", RT_F32, prev_rotation_pitch),
    LF("boundingBox", RT_BB, e.bounding_box),
    LF("onGround", RT_U8, e.on_ground),
    LF("isCollidedHorizontally", RT_U8, e.is_collided_horizontally),
    LF("isCollidedVertically", RT_U8, e.is_collided_vertically),
    LF("isCollided", RT_U8, e.is_collided),
    LF("velocityChanged", RT_U8, e.velocity_changed),
    LF("isInWeb", RT_U8, e.is_in_web),
    LF("isDead", RT_U8, is_dead),
    LF("width", RT_F32, e.width), LF("height", RT_F32, e.height),
    LF("prevDistanceWalkedModified", RT_F32, prev_distance_walked_modified),
    LF("distanceWalkedModified", RT_F32, e.distance_walked_modified),
    LF("distanceWalkedOnStepModified", RT_F32, e.distance_walked_on_step_modified),
    LF("fallDistance", RT_F32, e.fall_distance),
    LF("nextStepDistance", RT_I32, e.next_step_distance),
    LF("yOffset", RT_F32, e.y_offset), LF("ySize", RT_F32, e.y_size), LF("stepHeight", RT_F32, e.step_height),
    LF("noClip", RT_U8, e.no_clip),
    LF("ticksExisted", RT_I32, ticks_existed),
    LF("fireResistance", RT_I32, e.fire_resistance), LF("fire", RT_I32, e.fire),
    LF("inWater", RT_U8, is_in_water),
    LF("hurtResistantTime", RT_I32, hurt_resistant_time),
    LF("firstUpdate", RT_U8, e.first_update),
    LF("isImmuneToFire", RT_U8, immune_to_fire),
    LF("addedToChunk", RT_U8, added_to_chunk),
    LF("chunkCoordX", RT_I32, chunk_coord_x), LF("chunkCoordY", RT_I32, chunk_coord_y), LF("chunkCoordZ", RT_I32, chunk_coord_z),
    LF("isAirBorne", RT_U8, is_air_borne),
    LF("timeUntilPortal", RT_I32, time_until_portal), LF("inPortal", RT_U8, in_portal),
    LF("portalCounter", RT_I32, portal_counter), LF("teleportDirection", RT_I32, teleport_direction),
    LF("dimension", RT_I32, dimension),
    LF("invulnerable", RT_U8, invulnerable),
    LF("field_70135_K", RT_U8, e.field_70135_K),
    LF("preventEntitySpawning", RT_U8, e.prevent_entity_spawning),
    /* EntityLivingBase */
    LF("prevHealth", RT_F32, prev_health),
    LF("hurtTime", RT_I32, hurt_time), LF("maxHurtTime", RT_I32, max_hurt_time),
    LF("attackedAtYaw", RT_F32, attacked_at_yaw),
    LF("deathTime", RT_I32, death_time), LF("attackTime", RT_I32, attack_time),
    LF("prevSwingProgress", RT_F32, prev_swing_progress), LF("swingProgress", RT_F32, swing_progress),
    LF("prevLimbSwingAmount", RT_F32, prev_limb_swing_amount), LF("limbSwingAmount", RT_F32, limb_swing_amount),
    LF("limbSwing", RT_F32, limb_swing),
    LF("maxHurtResistantTime", RT_I32, max_hurt_resistant_time),
    LF("prevCameraPitch", RT_F32, prev_camera_pitch), LF("cameraPitch", RT_F32, camera_pitch),
    LF("renderYawOffset", RT_F32, render_yaw_offset), LF("prevRenderYawOffset", RT_F32, prev_render_yaw_offset),
    LF("rotationYawHead", RT_F32, rotation_yaw_head), LF("prevRotationYawHead", RT_F32, prev_rotation_yaw_head),
    LF("attackingPlayer", RT_LREF, attacking_player),
    LF("recentlyHit", RT_I32, recently_hit),
    LF("dead", RT_U8, dead),
    LF("entityAge", RT_I32, entity_age),
    LF("field_110154_aX", RT_F32, field_110154_aX), LF("field_70764_aw", RT_F32, field_70764_aw),
    LF("lastDamage", RT_F32, last_damage),
    LF("isJumping", RT_U8, is_jumping),
    LF("moveStrafing", RT_F32, move_strafing), LF("moveForward", RT_F32, move_forward),
    LF("randomYawVelocity", RT_F32, random_yaw_velocity),
    LF("potionsNeedUpdate", RT_U8, potions_need_update),
    LF("entityLivingToAttack", RT_LREF, entity_living_to_attack),
    LF("revengeTimer", RT_I32, revenge_timer),
    LF("lastAttacker", RT_LREF, last_attacker),
    LF("lastAttackerTime", RT_I32, last_attacker_time),
    LF("landMovementFactor", RT_F32, land_movement_factor),
    LF("jumpTicks", RT_I32, jump_ticks),
    LF("arrowHitTimer", RT_I32, arrow_hit_timer),
    /* EntityLiving */
    LF("livingSoundTime", RT_I32, living_sound_time),
    LF("experienceValue", RT_I32, experience_value),
    LF("attackTarget", RT_LREF, attack_target),
    LF("defaultPitch", RT_F32, default_pitch),
    LF("currentTarget", RT_LREF, current_target),
    LF("numTicksToChaseTarget", RT_I32, num_ticks_to_chase_target),
    LF("canPickUpLoot", RT_U8, can_pick_up_loot),
    LF("persistenceRequired", RT_U8, persistence_required),
    LF("isLeashed", RT_U8, is_leashed),
    /* EntityCreature */
    LF("pathToEntity", RT_PATH, creature_path),
    LF("entityToAttack", RT_LREF, entity_to_attack),
    LF("hasAttacked", RT_U8, has_attacked),
    LF("fleeingTick", RT_I32, fleeing_tick),
    LF("maximumHomeDistance", RT_F32, maximum_home_distance),
    /* EntityAgeable, EntityAnimal */
    LF("breeding", RT_I32, breeding),
    /* EntityZombie */
    LF("conversionTime", RT_I32, zombie_conversion_time),
    /* EntityCreeper */
    LF("lastActiveTime", RT_I32, creeper_last_active_time),
    LF("timeSinceIgnited", RT_I32, creeper_time_since_ignited),
    LF("fuseTime", RT_I32, creeper_fuse_time),
    LF("explosionRadius", RT_I32, creeper_explosion_radius),
    /* EntityEnderman */
    LF("stareTimer", RT_I32, enderman_stare_timer),
    LF("teleportDelay", RT_I32, enderman_teleport_delay),
    LF("isAggressive", RT_U8, enderman_is_aggressive),
    LF("lastEntityToAttack", RT_LREF, enderman_last_entity_to_attack),
    /* EntityPigZombie */
    LF("angerLevel", RT_I32, pigman_anger_level),
    LF("field_110191_bu", RT_LREF, pigman_last_entity_to_attack),
    LF("randomSoundDelay", RT_I32, pigman_random_sound_delay),
    /* EntitySilverfish */
    LF("allySummonCooldown", RT_I32, silverfish_ally_summon_cooldown),
    /* EntityWitch */
    LF("witchAttackTimer", RT_I32, witch_attack_timer),
    /* EntityBlaze */
    LF("heightOffset", RT_F32, blaze_height_offset),
    LF("heightOffsetUpdateTime", RT_I32, blaze_height_offset_update_time),
    LF("field_70846_g", RT_I32, blaze_attack_step),
    /* EntityGhast */
    LF("courseChangeCooldown", RT_I32, course_change_cooldown),
    LF("waypointX", RT_F64, waypoint_x), LF("waypointY", RT_F64, waypoint_y), LF("waypointZ", RT_F64, waypoint_z),
    LF("aggroCooldown", RT_I32, aggro_cooldown),
    LF("prevAttackCounter", RT_I32, prev_attack_counter),
    LF("attackCounter", RT_I32, attack_counter),
    LF("explosionStrength", RT_I32, explosion_power),
    /* EntitySlime */
    LF("squishAmount", RT_F32, squish_amount),
    LF("squishFactor", RT_F32, squish_factor),
    LF("prevSquishFactor", RT_F32, prev_squish_factor),
    LF("slimeJumpDelay", RT_I32, slime_jump_delay),
    /* EntityChicken */
    LF("field_70886_e", RT_F32, chicken_field_70886_e),
    LF("destPos", RT_F32, chicken_dest_pos),
    LF("field_70884_g", RT_F32, chicken_field_70884_g),
    LF("field_70888_h", RT_F32, chicken_field_70888_h),
    LF("field_70889_i", RT_F32, chicken_field_70889_i),
    LF("timeUntilNextEgg", RT_I32, time_until_next_egg),
    /* EntitySheep */
    LF("sheepTimer", RT_I32, sheep_timer),
    /* EntitySquid */
    LF("squidPitch", RT_F32, squid_pitch), LF("prevSquidPitch", RT_F32, prev_squid_pitch),
    LF("squidYaw", RT_F32, squid_yaw), LF("prevSquidYaw", RT_F32, prev_squid_yaw),
    LF("squidRotation", RT_F32, squid_rotation), LF("prevSquidRotation", RT_F32, prev_squid_rotation),
    LF("tentacleAngle", RT_F32, tentacle_angle), LF("lastTentacleAngle", RT_F32, last_tentacle_angle),
    LF("randomMotionSpeed", RT_F32, squid_random_motion_speed),
    LF("rotationVelocity", RT_F32, squid_rotation_velocity),
    LF("field_70871_bB", RT_F32, squid_field_70871_bB),
    LF("randomMotionVecX", RT_F32, squid_random_motion_vec_x),
    LF("randomMotionVecY", RT_F32, squid_random_motion_vec_y),
    LF("randomMotionVecZ", RT_F32, squid_random_motion_vec_z),
    /* EntityVillager */
    LF("randomTickDivider", RT_I32, random_tick_divider),
    LF("isMating", RT_U8, is_mating),
    LF("isPlaying", RT_U8, is_playing),
    LF("timeUntilReset", RT_I32, time_until_reset),
    LF("needsInitilization", RT_U8, needs_initialization),
    LF("isLookingForHome", RT_U8, is_looking_for_home),
    /* EntityIronGolem */
    LF("homeCheckTimer", RT_I32, home_check_timer),
    LF("attackTimer", RT_I32, attack_timer),
    LF("holdRoseTick", RT_I32, hold_rose_tick),
};

/* The entities that are not living: the item pool's (EntityItem,
 * EntityXPOrb) and the projectiles' (EntityArrow, EntityThrowable,
 * EntityFireball, EntityTNTPrimed, EntityEnderEye), all ie_ent. */
#define IF(n, t, m) { n, t, offsetof(ie_ent, m) }
static const struct rt_field ie_fields[] = {
    IF("posX", RT_F64, e.pos_x), IF("posY", RT_F64, e.pos_y), IF("posZ", RT_F64, e.pos_z),
    IF("prevPosX", RT_F64, prev_x), IF("prevPosY", RT_F64, prev_y), IF("prevPosZ", RT_F64, prev_z),
    IF("lastTickPosX", RT_F64, last_tick_x), IF("lastTickPosY", RT_F64, last_tick_y), IF("lastTickPosZ", RT_F64, last_tick_z),
    IF("motionX", RT_F64, e.motion_x), IF("motionY", RT_F64, e.motion_y), IF("motionZ", RT_F64, e.motion_z),
    IF("rotationYaw", RT_F32, rotation_yaw), IF("rotationPitch", RT_F32, rotation_pitch),
    IF("prevRotationYaw", RT_F32, prev_yaw), IF("prevRotationPitch", RT_F32, prev_pitch),
    IF("boundingBox", RT_BB, e.bounding_box),
    IF("onGround", RT_U8, e.on_ground),
    IF("isCollidedHorizontally", RT_U8, e.is_collided_horizontally),
    IF("isCollidedVertically", RT_U8, e.is_collided_vertically),
    IF("isCollided", RT_U8, e.is_collided),
    IF("velocityChanged", RT_U8, velocity_changed),
    IF("isInWeb", RT_U8, e.is_in_web),
    IF("isDead", RT_U8, is_dead),
    IF("width", RT_F32, e.width), IF("height", RT_F32, e.height),
    IF("distanceWalkedModified", RT_F32, e.distance_walked_modified),
    IF("distanceWalkedOnStepModified", RT_F32, e.distance_walked_on_step_modified),
    IF("fallDistance", RT_F32, e.fall_distance),
    IF("nextStepDistance", RT_I32, e.next_step_distance),
    IF("yOffset", RT_F32, e.y_offset), IF("ySize", RT_F32, e.y_size), IF("stepHeight", RT_F32, e.step_height),
    IF("noClip", RT_U8, e.no_clip),
    IF("ticksExisted", RT_I32, ticks_existed),
    IF("fireResistance", RT_I32, e.fire_resistance), IF("fire", RT_I32, e.fire),
    IF("inWater", RT_U8, in_water),
    IF("firstUpdate", RT_U8, first_update),
    IF("addedToChunk", RT_U8, added_to_chunk),
    IF("chunkCoordX", RT_I32, chunk_x), IF("chunkCoordY", RT_I32, chunk_y), IF("chunkCoordZ", RT_I32, chunk_z),
    IF("timeUntilPortal", RT_I32, time_until_portal), IF("inPortal", RT_I32, in_portal),
    IF("portalCounter", RT_I32, portal_counter), IF("teleportDirection", RT_I32, teleport_direction),
    IF("dimension", RT_I32, dimension),
    IF("preventEntitySpawning", RT_U8, e.prevent_entity_spawning),
    /* EntityItem */
    IF("age", RT_I32, age), IF("delayBeforeCanPickup", RT_I32, delay), IF("health", RT_I32, health),
    IF("hoverStart", RT_F32, hover_start),
    /* EntityXPOrb */
    IF("xpColor", RT_I32, xp_color), IF("xpOrbAge", RT_I32, age), IF("field_70532_c", RT_I32, delay),
    IF("xpOrbHealth", RT_I32, health), IF("xpValue", RT_I32, xp_value), IF("xpTargetColor", RT_I32, xp_target_color),
    /* EntityArrow */
    IF("field_145791_d", RT_I32, tile_x), IF("field_145792_e", RT_I32, tile_y), IF("field_145789_f", RT_I32, tile_z),
    IF("inData", RT_I32, in_data), IF("inGround", RT_U8, in_ground),
    IF("canBePickedUp", RT_I32, can_be_picked_up), IF("arrowShake", RT_I32, shake),
    IF("ticksInGround", RT_I32, ticks_in_ground), IF("ticksInAir", RT_I32, ticks_in_air),
    IF("damage", RT_F64, arrow_damage), IF("knockbackStrength", RT_I32, knockback_strength),
    /* EntityThrowable */
    IF("field_145788_c", RT_I32, tile_x), IF("field_145786_d", RT_I32, tile_y), IF("field_145787_e", RT_I32, tile_z),
    IF("throwableShake", RT_I32, shake),
    /* EntityFireball */
    IF("field_145795_e", RT_I32, tile_x), IF("field_145793_f", RT_I32, tile_y), IF("field_145794_g", RT_I32, tile_z),
    IF("ticksAlive", RT_I32, ticks_alive),
    IF("accelerationX", RT_F64, accel_x), IF("accelerationY", RT_F64, accel_y), IF("accelerationZ", RT_F64, accel_z),
    IF("field_92057_e", RT_I32, explosion_power),
    /* EntityTNTPrimed */
    IF("fuse", RT_I32, fuse),
    /* EntityEnderEye */
    IF("targetX", RT_F64, eye_target_x), IF("targetY", RT_F64, eye_target_y), IF("targetZ", RT_F64, eye_target_z),
    IF("despawnTimer", RT_I32, eye_timer), IF("shatterOrDrop", RT_U8, eye_drop),
};

/* EntityFallingBlock and the hanging entities (fh_ent). */
#define HF(n, t, m) { n, t, offsetof(fh_ent, m) }
static const struct rt_field fh_fields[] = {
    HF("posX", RT_F64, e.pos_x), HF("posY", RT_F64, e.pos_y), HF("posZ", RT_F64, e.pos_z),
    HF("prevPosX", RT_F64, e.prev_pos_x), HF("prevPosY", RT_F64, e.prev_pos_y), HF("prevPosZ", RT_F64, e.prev_pos_z),
    HF("lastTickPosX", RT_F64, last_tick_x), HF("lastTickPosY", RT_F64, last_tick_y), HF("lastTickPosZ", RT_F64, last_tick_z),
    HF("motionX", RT_F64, e.motion_x), HF("motionY", RT_F64, e.motion_y), HF("motionZ", RT_F64, e.motion_z),
    HF("rotationYaw", RT_F32, rotation_yaw), HF("prevRotationYaw", RT_F32, prev_rotation_yaw),
    HF("rotationPitch", RT_F32, rotation_pitch),
    HF("boundingBox", RT_BB, e.bounding_box),
    HF("onGround", RT_U8, e.on_ground),
    HF("isCollidedHorizontally", RT_U8, e.is_collided_horizontally),
    HF("isCollidedVertically", RT_U8, e.is_collided_vertically),
    HF("isCollided", RT_U8, e.is_collided),
    HF("isInWeb", RT_U8, e.is_in_web),
    HF("width", RT_F32, e.width), HF("height", RT_F32, e.height),
    HF("fallDistance", RT_F32, e.fall_distance),
    HF("yOffset", RT_F32, e.y_offset), HF("ySize", RT_F32, e.y_size),
    HF("ticksExisted", RT_I32, ticks_existed),
    HF("fire", RT_I32, e.fire),
    HF("isDead", RT_I32, is_dead),
    HF("addedToChunk", RT_I32, added_to_chunk),
    HF("chunkCoordX", RT_I32, chunk_x), HF("chunkCoordY", RT_I32, chunk_y), HF("chunkCoordZ", RT_I32, chunk_z),
    /* EntityFallingBlock */
    HF("field_145812_b", RT_I32, time), HF("field_145813_c", RT_I32, drop_item),
    HF("field_145808_f", RT_I32, broke), HF("field_145809_g", RT_I32, hurt_entities),
    HF("field_145815_h", RT_I32, hurt_max), HF("field_145816_i", RT_F32, hurt_amount),
    /* EntityHanging */
    HF("tickCounter1", RT_I32, tick_counter1),
};

/* The helpers' and the navigator's rows. */
#define LK(n, t, m) { n, t, offsetof(struct look_helper, m) }
static const struct rt_field look_fields[] = {
    LK("deltaLookYaw", RT_F32, delta_look_yaw), LK("deltaLookPitch", RT_F32, delta_look_pitch),
    LK("isLooking", RT_U8, is_looking),
    LK("posX", RT_F64, x), LK("posY", RT_F64, y), LK("posZ", RT_F64, z),
};
#define MV(n, t, m) { n, t, offsetof(struct move_helper, m) }
static const struct rt_field move_fields[] = {
    MV("posX", RT_F64, x), MV("posY", RT_F64, y), MV("posZ", RT_F64, z), MV("speed", RT_F64, speed),
    MV("update", RT_U8, update),
};
#define BD(n, t, m) { n, t, offsetof(struct body_helper, m) }
static const struct rt_field body_fields[] = {
    BD("field_75666_b", RT_I32, counter), BD("field_75667_c", RT_F32, yaw),
};
#define NV(n, t, m) { n, t, offsetof(struct path_nav, m) }
static const struct rt_field nav_fields[] = {
    NV("currentPath", RT_PATH, path), NV("speed", RT_F64, speed),
    NV("noSunPathfind", RT_U8, no_sun_pathfind),
    NV("totalTicks", RT_I32, total_ticks), NV("ticksAtLastPos", RT_I32, ticks_at_last_pos),
    NV("canPassOpenWoodenDoors", RT_U8, can_pass_open_doors),
    NV("canPassClosedWoodenDoors", RT_U8, can_pass_closed_doors),
    NV("avoidsWater", RT_U8, avoids_water), NV("canSwim", RT_U8, can_swim),
};

/* A task's rows, per Java class (the executing entries and tickCount are
 * the list's own). EntityAITarget's and EntityAIDoorInteract's rows apply to
 * their subclasses too. */
#define TK(n, t, m) { n, t, offsetof(struct ai_task, m) }
struct rt_task_class {
    const char *cls;
    const struct rt_field *f;
    int n;
};

static const struct rt_field t_target[] = {
    TK("shouldCheckSight", RT_U8, should_check_sight), TK("nearbyOnly", RT_U8, nearby_only),
    TK("targetSearchStatus", RT_I32, target_search_status), TK("targetSearchDelay", RT_I32, target_search_delay),
    TK("field_75298_g", RT_I32, target_unseen_ticks),
};
static const struct rt_field t_door[] = {
    TK("entityPosX", RT_I32, door_x), TK("entityPosY", RT_I32, door_y), TK("entityPosZ", RT_I32, door_z),
    TK("hasStoppedDoorInteraction", RT_U8, has_stopped_door_interaction),
    TK("entityPositionX", RT_F32, door_pos_x), TK("entityPositionZ", RT_F32, door_pos_z),
};
static const struct rt_field t_wander[] = {
    TK("xPosition", RT_F64, x), TK("yPosition", RT_F64, y), TK("zPosition", RT_F64, z), TK("speed", RT_F64, speed),
};
static const struct rt_field t_panic[] = {
    TK("randPosX", RT_F64, x), TK("randPosY", RT_F64, y), TK("randPosZ", RT_F64, z), TK("speed", RT_F64, speed),
};
static const struct rt_field t_watch[] = {
    TK("closestEntity", RT_LREF, target), TK("maxDistanceForPlayer", RT_F32, max_watch_dist),
    TK("lookTime", RT_I32, look_time), TK("field_75331_e", RT_F32, watch_chance),
};
static const struct rt_field t_look_idle[] = {
    TK("lookX", RT_F64, x), TK("lookZ", RT_F64, z), TK("idleTime", RT_I32, idle_time),
};
static const struct rt_field t_collide[] = {
    TK("attackTick", RT_I32, attack_tick), TK("speedTowardsTarget", RT_F64, speed),
    TK("longMemory", RT_U8, long_memory), TK("entityPathEntity", RT_PATH, path),
    TK("field_75445_i", RT_I32, collide_cooldown),
    TK("field_151497_i", RT_F64, collide_px), TK("field_151495_j", RT_F64, collide_py),
    TK("field_151496_k", RT_F64, collide_pz),
};
static const struct rt_field t_nearest[] = {
    TK("targetEntity", RT_LREF, target),
};
static const struct rt_field t_hurt_by[] = {
    TK("entityCallsForHelp", RT_U8, calls_for_help), TK("field_142052_b", RT_I32, revenge_timer_last),
};
static const struct rt_field t_move_restrict[] = {
    TK("movePosX", RT_F64, x), TK("movePosY", RT_F64, y), TK("movePosZ", RT_F64, z),
    TK("movementSpeed", RT_F64, speed),
};
static const struct rt_field t_move_target[] = {
    TK("targetEntity", RT_LREF, target),
    TK("movePosX", RT_F64, x), TK("movePosY", RT_F64, y), TK("movePosZ", RT_F64, z),
    TK("speed", RT_F64, speed), TK("maxTargetDistance", RT_F32, max_watch_dist),
};
static const struct rt_field t_break_door[] = {
    TK("breakingTime", RT_I32, breaking_time), TK("field_75358_j", RT_I32, field_75358_j),
};
static const struct rt_field t_open_door[] = {
    TK("field_75360_j", RT_I32, door_counter),
};
static const struct rt_field t_flee_sun[] = {
    TK("shelterX", RT_F64, shelter_x), TK("shelterY", RT_F64, shelter_y), TK("shelterZ", RT_F64, shelter_z),
    TK("movementSpeed", RT_F64, speed),
};
static const struct rt_field t_arrow[] = {
    TK("attackTarget", RT_LREF, target), TK("rangedAttackTime", RT_I32, ranged_attack_time),
    TK("entityMoveSpeed", RT_F64, speed), TK("field_75318_f", RT_I32, field_75318_f),
    TK("field_96561_g", RT_I32, field_96561_g), TK("maxRangedAttackTime", RT_I32, max_ranged_attack_time),
    TK("field_96562_i", RT_F32, field_96562_i), TK("field_82642_h", RT_F32, field_82642_h),
};
static const struct rt_field t_swell[] = {
    TK("creeperAttackTarget", RT_LREF, target),
};
static const struct rt_field t_mate[] = {
    TK("targetMate", RT_LREF, target), TK("spawnBabyDelay", RT_I32, spawn_baby_delay), TK("moveSpeed", RT_F64, speed),
};
static const struct rt_field t_tempt[] = {
    TK("field_75282_b", RT_F64, speed), TK("field_75278_f", RT_F64, tempt_pitch), TK("field_75279_g", RT_F64, tempt_yaw),
    TK("delayTemptCounter", RT_I32, delay_tempt_counter), TK("isRunning", RT_U8, tempt_running),
    TK("scaredByPlayerMovement", RT_U8, scared_by_movement), TK("field_75286_m", RT_U8, tempt_old_avoid),
};
static const struct rt_field t_follow_parent[] = {
    TK("parentAnimal", RT_LREF, target), TK("field_75347_c", RT_F64, speed), TK("field_75345_d", RT_I32, follow_parent_delay),
};
static const struct rt_field t_eat_grass[] = {
    TK("field_151502_a", RT_I32, eat_grass_timer),
};
static const struct rt_field t_avoid[] = {
    TK("farSpeed", RT_F64, speed), TK("nearSpeed", RT_F64, avoid_near_speed),
    TK("closestLivingEntity", RT_LREF, target), TK("distanceFromEntity", RT_F32, max_watch_dist),
    TK("entityPathEntity", RT_PATH, path),
};
static const struct rt_field t_move_indoors[] = {
    TK("insidePosX", RT_I32, inside_pos_x), TK("insidePosZ", RT_I32, inside_pos_z),
};
static const struct rt_field t_mtv[] = {
    TK("movementSpeed", RT_F64, speed), TK("entityPathNavigate", RT_PATH, mtv_path), TK("isNocturnal", RT_U8, nocturnal),
};
static const struct rt_field t_villager_mate[] = {
    TK("mate", RT_LREF, target_mate), TK("matingTimeout", RT_I32, mating_timeout),
};
static const struct rt_field t_follow_golem[] = {
    TK("theGolem", RT_LREF, target_golem), TK("takeGolemRoseTick", RT_I32, take_golem_rose_tick),
    TK("tookGolemRose", RT_U8, took_golem_rose),
};
static const struct rt_field t_play[] = {
    TK("targetVillager", RT_LREF, target_child), TK("field_75261_c", RT_F64, speed), TK("playTime", RT_I32, play_time),
};
static const struct rt_field t_look_villager[] = {
    TK("theVillager", RT_LREF, target), TK("lookTime", RT_I32, look_time),
};
static const struct rt_field t_ctl[] = {
    TK("currentSpeed", RT_F32, ctl_current_speed), TK("speedBoosted", RT_U8, ctl_speed_boosted),
    TK("speedBoostTime", RT_I32, ctl_speed_boost_time), TK("maxSpeedBoostTime", RT_I32, ctl_max_speed_boost_time),
};

#define TC(c, a) { c, a, (int)(sizeof a / sizeof a[0]) }
static const struct rt_task_class task_classes[] = {
    TC("EntityAIWander", t_wander), TC("EntityAIPanic", t_panic),
    TC("EntityAIWatchClosest", t_watch), TC("EntityAIWatchClosest2", t_watch), TC("EntityAILookAtTradePlayer", t_watch),
    TC("EntityAILookIdle", t_look_idle), TC("EntityAIAttackOnCollide", t_collide),
    TC("EntityAINearestAttackableTarget", t_nearest), TC("EntityAIHurtByTarget", t_hurt_by),
    TC("EntityAIMoveTowardsRestriction", t_move_restrict), TC("EntityAIMoveTowardsTarget", t_move_target),
    TC("EntityAIBreakDoor", t_break_door), TC("EntityAIOpenDoor", t_open_door),
    TC("EntityAIFleeSun", t_flee_sun), TC("EntityAIArrowAttack", t_arrow),
    TC("EntityAICreeperSwell", t_swell), TC("EntityAIMate", t_mate), TC("EntityAITempt", t_tempt),
    TC("EntityAIFollowParent", t_follow_parent), TC("EntityAIEatGrass", t_eat_grass),
    TC("EntityAIAvoidEntity", t_avoid), TC("EntityAIMoveIndoors", t_move_indoors),
    TC("EntityAIMoveThroughVillage", t_mtv), TC("EntityAIVillagerMate", t_villager_mate),
    TC("EntityAIFollowGolem", t_follow_golem), TC("EntityAIPlay", t_play),
    TC("EntityAILookAtVillager", t_look_villager), TC("EntityAIControlledByPlayer", t_ctl),
};

/* ------------------------------------------------------------- values */

#define g_sr (nw_env->snap_runtime.sr)
#define g_player_id (nw_env->snap_runtime.player_id)

/* a reference to the player, which serverreplay_bind_player fills in */
static void rt_player_ref(lref *ref)
{
    g_sr->rt_player_refs = slab_table_room(g_sr->rt_player_refs, g_sr->nrt_player_refs, &g_sr->cap_rt_player_refs,
                                           sizeof *g_sr->rt_player_refs);
    g_sr->rt_player_refs[g_sr->nrt_player_refs++] = ref;
}

/* The living with this entity id in any world, or the player's twin (which
 * serverreplay_bind_player makes after the load: the reference at ref is
 * filled in then). */
static struct living *rt_living(int id, lref *ref)
{
    if (id < 0) return NULL;
    if (g_sr->player_livh && lv_get(g_sr->player_livh)->entity_id == id) return lv_get(g_sr->player_livh);
    if (id == g_player_id)
    {
        if (ref) rt_player_ref(ref);
        return NULL;
    }
    struct living *found = NULL;
    int here = g_sr->here;
    for (int w = 0; w < g_sr->nworlds && !found; ++w)
    {
        serverreplay_enter(g_sr, g_sr->w[w].dim);
        for (int i = 0; i < g_sr->d->anw.n; ++i)
        {
            struct an_ent *en = an_ent_at(g_sr->d->anw.slot[i]);
            if (en->used && en->is_living && en->livh && lv_get(en->livh)->entity_id == id) { found = lv_get(en->livh); break; }
        }
    }
    serverreplay_enter(g_sr, here);
    /* the EntityDragon as the livings see it (a mob it hit holds it as its
     * attacker): the End's shadow living, in step with the dragon */
    if (!found && g_sr->end && g_sr->end->shadowh && lv_get(g_sr->end->shadowh)->entity_id == id)
        found = lv_get(g_sr->end->shadowh);
    for (int i = 0; i < g_sr->nghosts && !found; ++i)
        if (lv_get(g_sr->ghosts[i])->entity_id == id) found = lv_get(g_sr->ghosts[i]);
    return found;
}

/* "p:<index>,<length>;x,y,z;..." or "p:" for none */
static pathref rt_path(const char *s)
{
    if (!s || strncmp(s, "p:", 2) != 0 || !s[2]) return 0;
    int npts = 0;
    for (const char *q = s; *q; ++q) npts += *q == ';';
    pathref h = path_alloc(npts);
    struct path_ent *p = path_at(h);
    char *end;
    p->index = (int)strtol(s + 2, &end, 10);
    if (*end == ',') p->length = (int)strtol(end + 1, &end, 10);
    int n = 0;
    while (*end == ';' && n < npts)
    {
        p->pts[n][0] = (int)strtol(end + 1, &end, 10);
        p->pts[n][1] = (int)strtol(end + 1, &end, 10);
        p->pts[n][2] = (int)strtol(end + 1, &end, 10);
        ++n;
    }
    p->n_points = n;
    return h;
}

static int rt_bits(const char *s, int digits, uint64_t *out)
{
    if (!s || (int)strlen(s) < digits) return 0;
    char *end;
    *out = strtoull(s, &end, 16);
    return end == s + digits;
}

/* One value into base + f->off. Returns 0 when the text is not the row's
 * type (left unapplied). */
static int rt_apply(char *base, const struct rt_field *f, const char *v)
{
    void *dst = base + f->off;
    uint64_t b;
    if (!strncmp(v, "str:", 4)) v += 4; /* the non-scalar forms */
    switch (f->type)
    {
        case RT_F32:
            if (strncmp(v, "f:", 2) || !rt_bits(v + 2, 8, &b)) return 0;
            { uint32_t u = (uint32_t)b; memcpy(dst, &u, 4); }
            return 1;
        case RT_F64:
            if (strncmp(v, "d:", 2) || !rt_bits(v + 2, 16, &b)) return 0;
            memcpy(dst, &b, 8);
            return 1;
        case RT_I32:
            if (v[1] != ':') return 0;
            *(int *)dst = (int)strtol(v + 2, NULL, 10);
            return 1;
        case RT_U8:
            if (v[1] != ':') return 0;
            *(uint8_t *)dst = (uint8_t)strtol(v + 2, NULL, 10);
            return 1;
        case RT_LREF:
            if (strncmp(v, "e:", 2)) return 0;
            *(lref *)dst = lv_ref(rt_living((int)strtol(v + 2, NULL, 10), (lref *)dst));
            return 1;
        case RT_BB:
        {
            if (strncmp(v, "bb:", 3) || !v[3]) return 0;
            double d[6];
            const char *p = v + 3;
            for (int i = 0; i < 6; ++i)
            {
                if (!rt_bits(p, 16, &b)) return 0;
                memcpy(&d[i], &b, 8);
                p += 16;
                if (i < 5 && *p++ != ',') return 0;
            }
            memcpy(dst, d, sizeof d);
            return 1;
        }
        case RT_PATH:
        {
            pathref *pp = dst;
            path_drop(*pp);
            *pp = rt_path(v);
            return 1;
        }
    }
    return 0;
}

static void rt_apply_all(char *base, const struct rt_field *t, int n, const struct jval *obj)
{
    if (!obj) return;
    for (int i = 0; i < n; ++i)
    {
        const char *v = json_str(json_get(obj, t[i].name));
        if (v) rt_apply(base, &t[i], v);
    }
}

/* ------------------------------------------------------------- the AI */

static const struct rt_task_class *rt_task_class(const char *cls)
{
    for (size_t i = 0; i < sizeof task_classes / sizeof task_classes[0]; ++i)
        if (strcmp(task_classes[i].cls, cls) == 0) return &task_classes[i];
    return NULL;
}

static int rt_is_target_task(const char *cls)
{
    return strcmp(cls, "EntityAINearestAttackableTarget") == 0 || strcmp(cls, "EntityAIHurtByTarget") == 0 ||
           strcmp(cls, "EntityAIDefendVillage") == 0;
}

/* A VillageDoorInfo ("vd:x,y,z") as the village collection's own object:
 * the village door lists, then the doors found but not yet in a village. */
static struct village_door_info *rt_door(struct living *l, const char *v)
{
    if (v && !strncmp(v, "str:", 4)) v += 4;
    int x, y, z;
    if (!v || strncmp(v, "vd:", 3) || sscanf(v + 3, "%d,%d,%d", &x, &y, &z) != 3) return NULL;
    struct village_collection *vc = l->an ? (struct village_collection *)l->an->village_collection : NULL;
    if (!vc) return NULL;
    for (int i = 0; i < vc->num_villages; ++i)
        for (int d = 0; d < vc_list_at(vc, i)->num_doors; ++d)
        {
            struct village_door_info *di = door_at(vc_list_at(vc, i)->doors[d]);
            if (di && di->pos_x == x && di->pos_y == y && di->pos_z == z) return di;
        }
    for (int d = 0; d < vc->num_new_doors; ++d)
        if (vc->new_doors[d].pos_x == x && vc->new_doors[d].pos_y == y && vc->new_doors[d].pos_z == z)
            return &vc->new_doors[d];
    return NULL;
}

/* A Village ("vi:" and its center) as the collection's own. */
static struct village *rt_village(struct living *l, const char *v)
{
    if (v && !strncmp(v, "str:", 4)) v += 4;
    int x, y, z;
    if (!v || strncmp(v, "vi:", 3) || sscanf(v + 3, "%d,%d,%d", &x, &y, &z) != 3) return NULL;
    struct village_collection *vc = l->an ? (struct village_collection *)l->an->village_collection : NULL;
    for (int i = 0; vc && i < vc->num_villages; ++i)
        if (vc_list_at(vc, i)->center_x == x && vc_list_at(vc, i)->center_y == y && vc_list_at(vc, i)->center_z == z)
            return vc_list_at(vc, i);
    return NULL;
}

/* The village references a task holds (EntityAIMoveIndoors.doorInfo,
 * EntityAIRestrictOpenDoor.frontDoor, EntityAIMoveThroughVillage's door
 * and visited list, EntityAIVillagerMate.villageObj). */
static void rt_task_village(struct living *l, struct ai_task *task, const char *cls, const struct jval *tj)
{
    if (!strcmp(cls, "EntityAIMoveIndoors") || !strcmp(cls, "EntityAIMoveThroughVillage"))
        task->front_door = door_ref(rt_door(l, json_str(json_get(tj, "doorInfo"))));
    if (!strcmp(cls, "EntityAIRestrictOpenDoor"))
        task->front_door = door_ref(rt_door(l, json_str(json_get(tj, "frontDoor"))));
    if (!strcmp(cls, "EntityAIVillagerMate"))
        task->village_ptr = vc_village_ref(l->an->village_collection, rt_village(l, json_str(json_get(tj, "villageObj"))));
    if (!strcmp(cls, "EntityAIMoveThroughVillage"))
    {
        const char *v = json_str(json_get(tj, "doorList"));
        if (v && !strncmp(v, "str:", 4)) v += 4;
        lv_ai(l)->mtv_nvisited_doors = 0;
        if (v && !strncmp(v, "vdl:", 4))
        {
            const char *p = v + 4;
            while (*p && lv_ai(l)->mtv_nvisited_doors < 16)
            {
                int *d = lv_ai(l)->mtv_visited_doors[lv_ai(l)->mtv_nvisited_doors];
                if (sscanf(p, "%d,%d,%d", &d[0], &d[1], &d[2]) != 3) break;
                ++lv_ai(l)->mtv_nvisited_doors;
                p = strchr(p, ';');
                if (!p) break;
                ++p;
            }
        }
    }
}

static int rt_tasks(struct living *l, struct ai_tasks *t, const struct jval *o, char *err, size_t errn)
{
    if (!o) return 1;
    int64_t v;
    const struct jval *e = json_get(o, "e");
    int n = json_len(e);
    if (n != t->n)
    {
        snprintf(err, errn, "entity %d: %d AI tasks, the snapshot has %d", l->entity_id, t->n, n);
        return 0;
    }
    if (json_int(json_get(o, "tc"), &v)) t->tick_count = (int)v;
    for (int i = 0; i < n; ++i)
    {
        const struct jval *tj = json_at(e, i);
        const char *cls = json_str(json_get(tj, "cls"));
        if (!cls) continue;
        if (!strncmp(cls, "str:", 4)) cls += 4;
        struct ai_task *task = &t->entries[i].t;
        if (rt_is_target_task(cls))
            rt_apply_all((char *)task, t_target, (int)(sizeof t_target / sizeof t_target[0]), tj);
        if (!strcmp(cls, "EntityAIBreakDoor") || !strcmp(cls, "EntityAIOpenDoor"))
            rt_apply_all((char *)task, t_door, (int)(sizeof t_door / sizeof t_door[0]), tj);
        const struct rt_task_class *tc = rt_task_class(cls);
        if (tc) rt_apply_all((char *)task, tc->f, tc->n, tj);
        rt_task_village(l, task, cls, tj);
    }
    /* the executing entries in executingTaskEntries order */
    const char *x = json_str(json_get(o, "exec"));
    t->nexec = 0;
    if (x && !strncmp(x, "ia:", 3))
    {
        const char *p = x + 3;
        while (*p && t->nexec < AI_MAX_ENTRIES)
        {
            char *end;
            long k = strtol(p, &end, 10);
            if (end == p) break;
            t->executing[t->nexec++] = (int)k;
            p = *end == ',' ? end + 1 : end;
        }
    }
    return 1;
}

/* ------------------------------------------------------------- data watcher */

static void rt_watcher(struct living *l, const struct jval *dw)
{
    if (!dw) return;
    int64_t v;
    if (json_int(json_get(dw, "0"), &v)) l->flags0 = (uint8_t)v;
    if (json_int(json_get(dw, "1"), &v)) l->air = (int)v;
    if (json_int(json_get(dw, "9"), &v)) l->arrow_count_in_entity = (int)v;
    /* the potion colour and ambient flag updatePotionEffects draws on */
    if (json_int(json_get(dw, "7"), &v)) l->potion_liquid_color = (int)v;
    if (json_int(json_get(dw, "8"), &v)) l->potion_is_ambient = (uint8_t)v;
    /* the kinds' byte 16: the spider's climb flag, the bat's hanging bit,
     * the ghast's charge, the blaze's fire flag, the pig's saddle */
    if (l->kind == HK_SPIDER || l->kind == HK_CAVE_SPIDER || l->kind == AK_BAT || l->kind == GK_GHAST ||
        l->kind == HK_BLAZE || l->kind == AK_PIG)
        if (json_int(json_get(dw, "16"), &v)) l->data_watcher_16 = (int)v;
    if (l->kind == HK_CREEPER && json_int(json_get(dw, "16"), &v)) l->creeper_state = (int)v;
    if (l->kind == HK_ENDERMAN && json_int(json_get(dw, "18"), &v)) l->enderman_screaming = (int)v;
    if (l->kind == HK_WITCH && json_int(json_get(dw, "21"), &v)) l->witch_aggressive = (int)v;
}

/* ------------------------------------------------------------- entry */

static fh_ent *rt_find_fh(struct serverreplay *sr, int id);

static int rt_living_apply(struct living *l, const struct jval *rt, char *err, size_t errn)
{
    const struct jval *f = json_get(rt, "f");
    rt_apply_all((char *)l, living_fields, (int)(sizeof living_fields / sizeof living_fields[0]), f);
    /* EntityCreature.homePosition (ChunkCoordinates) */
    const char *home = json_str(json_get(f, "homePosition"));
    if (home && !strncmp(home, "str:", 4)) home += 4;
    if (home && !strncmp(home, "c:", 2)) sscanf(home + 2, "%d,%d,%d", &l->home_x, &l->home_y, &l->home_z);
    /* EntityBat.spawnPosition (its flight target; null is "c:") */
    const char *bat = json_str(json_get(f, "spawnPosition"));
    if (bat && !strncmp(bat, "str:", 4)) bat += 4;
    if (bat && !strncmp(bat, "c:", 2))
        l->bat_has_spawn_pos = sscanf(bat + 2, "%d,%d,%d", &l->bat_spawn_x, &l->bat_spawn_y, &l->bat_spawn_z) == 3;
    rt_watcher(l, json_get(rt, "dw"));
    /* the unsaved speed boosts an angry pigman or enderman carries */
    {
        const char *last = json_str(json_get(f, l->kind == HK_PIGMAN ? "field_110191_bu" : "lastEntityToAttack"));
        if (last && !strncmp(last, "str:", 4)) last += 4;
        int angry = last && !strncmp(last, "e:", 2) && strtol(last + 2, NULL, 10) >= 0;
        if (angry && l->kind == HK_PIGMAN) pigman_restore_speed_boost(l);
        if (angry && l->kind == HK_ENDERMAN) enderman_restore_speed_boost(l);
        if (l->kind == HK_WITCH) witch_restore_drinking(l);
    }
    /* EntityLiving.leashedToEntity, the holder a mid-run leash already
     * resolved (field_110170_bx, the NBT a load resolves later, is null
     * then): the knot in this world, or the player */
    {
        const char *lh = json_str(json_get(f, "leashedToEntity"));
        if (lh && !strncmp(lh, "str:", 4)) lh += 4;
        int hid = lh && !strncmp(lh, "e:", 2) ? (int)strtol(lh + 2, NULL, 10) : -1;
        fh_ent *knot = hid >= 0 ? rt_find_fh(g_sr, hid) : NULL;
        if (l->is_leashed && knot && knot->kind == FH_KNOT)
        {
            l->leash_holder = LEASH_KNOT;
            l->leash_knot = fh_ref(knot);
            l->leash_x = knot->tile_x;
            l->leash_y = knot->tile_y;
            l->leash_z = knot->tile_z;
            l->leash_pending = 0;
        }
        else if (l->is_leashed && hid >= 0 && hid == g_player_id)
        {
            l->leash_holder = LEASH_PLAYER;
            l->leash_pending = 0;
        }
        /* field_110170_bx itself, a load's Leash tag the entity has not
         * updated since (a rejoin's first tick: pfc-leashjoin-s2), which
         * the saved NBT no longer carries: recreateLeash's knot or UUID */
        const char *bx = json_str(json_get(f, "field_110170_bx"));
        if (bx && !strncmp(bx, "str:", 4)) bx += 4;
        if (l->is_leashed && hid < 0 && bx && !strncmp(bx, "lh:", 3))
        {
            long long a, b;
            int x, y, z;
            if (sscanf(bx + 3, "u,%lld,%lld", &a, &b) == 2)
            {
                l->leash_pending = 1;
                l->leash_msb = (int64_t)a;
                l->leash_lsb = (int64_t)b;
            }
            else if (sscanf(bx + 3, "k,%d,%d,%d", &x, &y, &z) == 3)
            {
                l->leash_pending = 2;
                l->leash_x = x;
                l->leash_y = y;
                l->leash_z = z;
            }
            else
                l->leash_pending = 3;
        }
    }
    /* EntityVillager.buyingPlayer: a trade window is open (the player's
     * restored ContainerMerchant points back at this villager) */
    {
        const char *bp = json_str(json_get(f, "buyingPlayer"));
        if (bp && !strncmp(bp, "str:", 4)) bp += 4;
        if (l->kind == VK_VILLAGER && bp && !strncmp(bp, "e:", 2) && strtol(bp + 2, NULL, 10) >= 0)
        {
            l->buying_player = 1;
            snprintf(lv_villager(l)->buying_player_name, sizeof lv_villager(l)->buying_player_name, "%s", "Player");
        }
        const char *lb = json_str(json_get(f, "lastBuyingPlayer"));
        if (lb && !strncmp(lb, "str:", 4)) lb += 4;
        if (l->kind == VK_VILLAGER && lb)
        {
            snprintf(lv_villager(l)->last_buying_player, sizeof lv_villager(l)->last_buying_player, "%s", lb);
            l->has_last_buying_player = 1;
        }
    }
    const struct jval *ai = json_get(rt, "ai");
    if (!ai) return 1;
    /* EntityCreature.field_110180_bt: a leashed creature's
     * EntityAIMoveTowardsRestriction, added at priority 2 by the
     * updateLeashedState that first saw the holder */
    {
        int64_t bt = 0;
        if (json_int(json_get(f, "field_110180_bt"), &bt) && bt && !l->leash_ai)
        {
            ai_add_leash_restriction(l);
            l->leash_ai = 1;
        }
    }
    if (!rt_tasks(l, &lv_ai(l)->tasks, json_get(ai, "tasks"), err, errn)) return 0;
    if (!rt_tasks(l, &lv_ai(l)->target_tasks, json_get(ai, "target"), err, errn)) return 0;
    rt_apply_all((char *)&l->look, look_fields, (int)(sizeof look_fields / sizeof look_fields[0]), json_get(ai, "look"));
    rt_apply_all((char *)&l->move, move_fields, (int)(sizeof move_fields / sizeof move_fields[0]), json_get(ai, "move"));
    rt_apply_all((char *)&l->body, body_fields, (int)(sizeof body_fields / sizeof body_fields[0]), json_get(ai, "body"));
    {
        int64_t v;
        if (json_int(json_get(json_get(ai, "jump"), "isJumping"), &v)) l->jump.is_jumping = (int)v;
    }
    const struct jval *nav = json_get(ai, "nav");
    rt_apply_all((char *)&l->nav, nav_fields, (int)(sizeof nav_fields / sizeof nav_fields[0]), nav);
    /* PathNavigate.lastPosCheck (a Vec3) */
    const char *lp = json_str(json_get(nav, "lastPosCheck"));
    uint64_t b;
    if (lp && !strncmp(lp, "str:", 4)) lp += 4;
    if (lp && !strncmp(lp, "v:", 2) && strlen(lp) >= 2 + 16 * 3 + 2)
    {
        if (rt_bits(lp + 2, 16, &b)) memcpy(&l->nav.lx, &b, 8);
        if (rt_bits(lp + 19, 16, &b)) memcpy(&l->nav.ly, &b, 8);
        if (rt_bits(lp + 36, 16, &b)) memcpy(&l->nav.lz, &b, 8);
    }
    return 1;
}

/* An ie_ent or fh_ent of this dimension by entity id (the item pool, the
 * living list's projectiles, the falling and hanging list). */
static ie_ent *rt_find_ie(struct serverreplay *sr, int id)
{
    for (int k = 0; k < sr->d->iew.n; ++k)
        if (ie_ent_at(sr->d->iew.slot[k]) && ie_ent_at(sr->d->iew.slot[k])->entity_id == id) return ie_ent_at(sr->d->iew.slot[k]);
    for (int k = 0; k < sr->d->anw.n; ++k)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[k]);
        if (en->used && !en->is_living && en->ieh && ie_get(en->ieh)->entity_id == id) return ie_get(en->ieh);
    }
    return NULL;
}

static fh_ent *rt_find_fh(struct serverreplay *sr, int id)
{
    for (int k = 0; k < sr->d->fhw.n; ++k)
        if (fh_ent_at(sr->d->fhw.slot[k]) && fh_ent_at(sr->d->fhw.slot[k])->entity_id == id) return fh_ent_at(sr->d->fhw.slot[k]);
    return NULL;
}

/* An ie_ent's rows, its data watcher's crit flag (the arrow's byte 16) and
 * its shooter or thrower. */
static void rt_ie_apply(ie_ent *ie, const struct jval *rt, const char *cls)
{
    const struct jval *f = json_get(rt, "f");
    rt_apply_all((char *)ie, ie_fields, (int)(sizeof ie_fields / sizeof ie_fields[0]), f);
    int64_t v;
    if (!strcmp(cls, "EntityArrow") && json_int(json_get(json_get(rt, "dw"), "16"), &v)) ie->is_critical = (int)(v & 1);
    const char *who = json_str(json_get(f, !strcmp(cls, "EntityArrow") || !strncmp(cls, "EntityLargeFireball", 19) ||
                                            !strcmp(cls, "EntitySmallFireball") ? "shootingEntity" : "thrower"));
    if (who && !strncmp(who, "str:", 4)) who += 4;
    if (who && !strncmp(who, "e:", 2))
    {
        int id = (int)strtol(who + 2, NULL, 10);
        if (id >= 0 && id == g_player_id)
        {
            /* the player's twin exists after the bind, which fills the
             * pointers in */
            ie->shooter_is_player = 1;
            rt_player_ref(&ie->shooting_entity);
            rt_player_ref(&ie->shooter);
        }
        else if (id >= 0)
        {
            struct living *l = rt_living(id, NULL);
            if (l) { ie->shooting_entity = lv_ref(l); ie->shooter = lv_ref(l); }
        }
    }
    const char *orb = json_str(json_get(f, "closestPlayer"));
    if (orb && !strncmp(orb, "str:", 4)) orb += 4;
    if (orb && !strncmp(orb, "e:", 2)) ie->xp_has_target = strtol(orb + 2, NULL, 10) >= 0;
}

/* ------------------------------------------------ the chunk slice order */

#define g_snap (nw_env->snap_runtime.snap)
#define g_stamp_base (nw_env->snap_runtime.stamp_base)

/* The chunk stamp that puts an entity at its recorded place in its slice
 * (Chunk.entityLists; stamps are only ever compared within one slice): the
 * base plus its index there, or after every placed one in its own old
 * order when the snapshot does not place it. */
static uint64_t rt_slice_stamp(int id, uint64_t old)
{
    for (int i = 0; i < g_snap->nents; ++i)
        if (g_snap->ents[i].id == id && g_snap->ents[i].cidx >= 0) return g_stamp_base + (uint64_t)g_snap->ents[i].cidx;
    return g_stamp_base + 1000000u + old;
}

#define RT_SORT_SLICE(arr, n, stampof)                                                  \
    do {                                                                                \
        for (int i_ = 1; i_ < (n); ++i_)                                                \
        {                                                                               \
            __typeof__((arr)[0]) v_ = (arr)[i_];                                        \
            int j_ = i_ - 1;                                                            \
            while (j_ >= 0 && stampof((arr)[j_]) > stampof(v_)) { (arr)[j_ + 1] = (arr)[j_]; --j_; } \
            (arr)[j_ + 1] = v_;                                                         \
        }                                                                               \
    } while (0)

#define AN_STAMP(en) (*((en)->is_living ? &lv_get((en)->livh)->e.chunk_stamp : &ie_get((en)->ieh)->e.chunk_stamp))
#define AN_ID(en) ((en)->is_living ? lv_get((en)->livh)->entity_id : ie_get((en)->ieh)->entity_id)
#define IE_STAMP(en) ((en)->e.chunk_stamp)
/* the same over a section list's slot indices */
#define AN_STAMP_AT(i) AN_STAMP(an_ent_at(i))
#define IE_STAMP_AT(i) (ie_ent_at(i)->e.chunk_stamp)
#define FH_STAMP_AT(i) (fh_ent_at(i)->e.chunk_stamp)

/* Chunk.entityLists' order for the loaded entities: the chunk's queries
 * (collision pushes, pickups, item merging, targets, explosions) and a
 * chunk's save visit a slice in its list order, which after moves between
 * chunks is not the load order. The stamps first, then each native slice
 * list sorted by them. */
static uint64_t rt_slice_order(struct serverreplay *sr)
{
    uint64_t top = 0;
#define RESTAMP(st, id) do { (st) = rt_slice_stamp((id), (st)); if ((st) > top) top = (st); } while (0)
    for (int k = 0; k < sr->d->anw.n; ++k)
    {
        struct an_ent *en = an_ent_at(sr->d->anw.slot[k]);
        if (en->used) RESTAMP(AN_STAMP(en), AN_ID(en));
    }
    for (int k = 0; k < sr->d->iew.n; ++k)
        if (ie_ent_at(sr->d->iew.slot[k])) RESTAMP(ie_ent_at(sr->d->iew.slot[k])->e.chunk_stamp, ie_ent_at(sr->d->iew.slot[k])->entity_id);
    for (int k = 0; k < sr->d->fhw.n; ++k)
        if (fh_ent_at(sr->d->fhw.slot[k])) RESTAMP(fh_ent_at(sr->d->fhw.slot[k])->e.chunk_stamp, fh_ent_at(sr->d->fhw.slot[k])->entity_id);
    if (sr->end && sr->here == 1)
    {
        struct dragon_state *d = &sr->end->d;
        for (int i = 0; i < d->n_crystals; ++i) RESTAMP(d->crystals[i].chunk_stamp, d->crystals[i].entity_id);
        if (sr->end->dragon_listed) RESTAMP(d->chunk_stamp, d->entity_id);
    }
#undef RESTAMP
    for (int k = 0; k < sr->d->anw.nchunks; ++k)
        for (int y = 0; y < 16; ++y)
            RT_SORT_SLICE(sec_items(&sr->d->anw.chunks[k].sec[y]), sr->d->anw.chunks[k].sec[y].n, AN_STAMP_AT);
    for (int k = 0; k < sr->d->iew.nchunks; ++k)
        for (int y = 0; y < IE_SECTIONS; ++y)
            RT_SORT_SLICE(sec_items(&sr->d->iew.chunks[k].sec[y]), sr->d->iew.chunks[k].sec[y].n, IE_STAMP_AT);
    for (int k = 0; k < sr->d->fhw.nchunks; ++k)
        for (int y = 0; y < FH_SECTIONS; ++y)
            RT_SORT_SLICE(sec_items(&sr->d->fhw.chunks[k].sec[y]), sr->d->fhw.chunks[k].sec[y].n, FH_STAMP_AT);
    return top;
}

/* The player's own place in its slice, once serverreplay_bind_player has
 * made its living twin: it joins its section now, where the snapshot had it
 * (not on its first tick, behind whatever that tick's head added there, a
 * kit's summon) */
void sr_runtime_player_stamp(struct serverreplay *sr)
{
    if (!sr->snap || !sr->player_livh) return;
    g_snap = sr->snap;
    for (int i = 0; i < g_snap->nents; ++i)
        if (g_snap->ents[i].player && g_snap->ents[i].cidx >= 0 && sr->rt_stamp_base)
        {
            struct living *l = lv_get(sr->player_livh);
            if (!l->added_to_chunk && sr->player != NULL && g_snap->ents[i].dim == sr->here)
            {
                int cx = (int)floor(sr->player->e.pos_x / 16.0);
                int cy = (int)floor(sr->player->e.pos_y / 16.0);
                int cz = (int)floor(sr->player->e.pos_z / 16.0);
                an_chunk_add(&sr->d->anw, an_deref(sr->player_enth), cx, cy, cz);
                l->e.chunk_stamp = sr->rt_stamp_base + (uint64_t)g_snap->ents[i].cidx;
                for (int k = 0; k < sr->d->anw.nchunks; ++k)
                    if (sr->d->anw.chunks[k].cx == cx && sr->d->anw.chunks[k].cz == cz)
                    {
                        struct sec_list *sl = &sr->d->anw.chunks[k].sec[l->chunk_coord_y];
                        RT_SORT_SLICE(sec_items(sl), sl->n, AN_STAMP_AT);
                    }
            }
            l->e.chunk_stamp = sr->rt_stamp_base + (uint64_t)g_snap->ents[i].cidx;
        }
}

int sr_apply_runtime(struct serverreplay *sr, const struct snapshot *snap)
{
    g_snap = snap;
    g_sr = sr;
    g_player_id = -1;
    for (int i = 0; i < snap->nents; ++i)
        if (snap->ents[i].player) g_player_id = snap->ents[i].id;
    int here = sr->here;
    for (int i = 0; i < snap->nents; ++i)
    {
        const struct snap_entity *se = &snap->ents[i];
        if (!se->rt || se->player) continue;
        struct living *l = NULL;
        serverreplay_enter(sr, se->dim);
        for (int k = 0; k < sr->d->anw.n && !l; ++k)
        {
            struct an_ent *en = an_ent_at(sr->d->anw.slot[k]);
            if (en->used && en->is_living && en->livh && lv_get(en->livh)->entity_id == se->id) l = lv_get(en->livh);
        }
        if (!l)
        {
            ie_ent *ie = rt_find_ie(sr, se->id);
            if (ie) { rt_ie_apply(ie, se->rt, se->cls); continue; }
            fh_ent *fh = rt_find_fh(sr, se->id);
            if (fh) rt_apply_all((char *)fh, fh_fields, (int)(sizeof fh_fields / sizeof fh_fields[0]), json_get(se->rt, "f"));
            continue;
        }
        if (!rt_living_apply(l, se->rt, sr->err, sizeof sr->err))
        {
            serverreplay_enter(sr, here);
            return 0;
        }
    }
    /* the ghosts' own state; out of every list, so out of every chunk */
    for (int i = 0; i < snap->nghosts && i < sr->nghosts; ++i)
    {
        struct living *l = lv_get(sr->ghosts[i]);
        const struct snap_entity *se = NULL;
        for (int k = 0; k < snap->nghosts && !se; ++k)
            if (snap->ghosts[k].id == l->entity_id) se = &snap->ghosts[k];
        if (!se || !se->rt) continue;
        serverreplay_enter(sr, se->dim);
        if (!rt_living_apply(l, se->rt, sr->err, sizeof sr->err))
        {
            serverreplay_enter(sr, here);
            return 0;
        }
        l->added_to_chunk = 0;
    }
    g_stamp_base = entity_chunk_stamp + 1;
    sr->rt_stamp_base = g_stamp_base;
    uint64_t top = g_stamp_base;
    for (int w = 0; w < sr->nworlds; ++w)
    {
        serverreplay_enter(sr, sr->w[w].dim);
        uint64_t t = rt_slice_order(sr);
        if (t > top) top = t;
    }
    entity_chunk_stamp = top + 1;
    serverreplay_enter(sr, here);
    return 1;
}
