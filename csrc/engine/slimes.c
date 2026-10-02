/* EntitySlime and EntityMagmaCube, ported from
 * oracle/src/entity/monster/EntitySlime.java and EntityMagmaCube.java, plus the
 * probe's own player (EntityPlayer's tick, damage pipeline and food stats).
 *
 * A slime is an EntityLiving that does not use the task AI: isAIEnabled() is
 * false, so every tick runs updateEntityActionState (the jump toward the nearest
 * player, the jump delay, moveStrafing/moveForward) instead of the AI tasks.
 * The size (1, 2 or 4) lives in the data watcher and drives the bounding box,
 * the max health (size*size), the attack strength and the split.
 *
 * The collision attack runs the other way round: the *player's* onLivingUpdate
 * sweeps its expanded box and calls each entity's onCollideWithPlayer, which is
 * where a slime damages the player. The player here is the probe's, a plain
 * EntityPlayer standing in the world: its tick is EntityPlayer.onLivingUpdate
 * (the head, EntityLivingBase.onLivingUpdate, then the movement speed, the
 * camera and the collision sweep), EntityPlayer.onUpdate's tail (the food
 * stats) and its damage pipeline (EntityPlayer.attackEntityFrom over
 * EntityLivingBase.attackEntityFrom, with EntityPlayer.damageEntity's hunger
 * cost). The parts of EntityPlayer that touch neither the recorded state nor
 * the slimes (the inventory, the item in use, sleeping, the container, the
 * play-time stat, the camera yaw and pitch smoothing) are left out; the report
 * lists them.
 *
 * Every literal is copied as printed, and every statement that reads state the
 * previous statement wrote stays in vanilla's order. */
#include "slimes.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "blocks.h"
#include "collide.h"
#include "det.h"
#include "jmath.h"
#include "nbtjson.h"
#include "player.h"
#include "raytrace.h"
#include "smath.h"
#include "trace.h"
#include "world.h"

/* The probe's world difficulty (see the manifest: NORMAL). */
#define PROBE_DIFFICULTY_NORMAL 2
#define PROBE_DIFFICULTY_PEACEFUL 0
#define PROBE_DIFFICULTY_HARD 3

enum { ITEM_SLIME_BALL = 341, ITEM_MAGMA_CREAM = 378 };

/* --------------------------------------------------------- the constructor */

/* EntitySlime.setSlimeSize. */
void slime_set_size(struct living *l, int size)
{
    l->slime_size = size;
    float s = 0.6F * (float)size;
    living_set_size(l, s, s);
    entity_set_position(&l->e, l->e.pos_x, l->e.pos_y, l->e.pos_z);
    attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], (double)(size * size));
    living_set_health(l, living_max_health(l));
    l->experience_value = size;
}

/* The EntitySlime(World) constructor after EntityLiving's, plus
 * EntityMagmaCube's own. */
void slime_construct(struct living *l, det_state *det)
{
    if (l->kind == SK_MAGMA_CUBE) l->immune_to_fire = 1;

    /* EntityLivingBase.applyEntityAttributes registers the three attributes and
     * sets movementSpeed to 0.1 when the AI is disabled (EntitySlime), then
     * EntityLiving's override registers followRange at 16 */
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.10000000149011612);

    int var2 = 1 << det_rng_int_n(&l->rand, 3);
    l->e.y_offset = 0.0F;
    l->slime_jump_delay = det_rng_int_n(&l->rand, 20) + 10;
    slime_set_size(l, var2);

    if (l->kind == SK_MAGMA_CUBE)
    {
        /* EntityMagmaCube.applyEntityAttributes */
        attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.20000000298023224);
    }

    (void)det;
}

/* ------------------------------------------------------------ the onUpdate */

/* EntitySlime.onUpdate's head, before super: at PEACEFUL a sized slime is
 * marked dead and still runs the rest of this tick. */
void slime_pre_on_update(struct living *l, det_state *det)
{
    (void)det;

    if (living_server_peaceful(l) && l->slime_size > 0)
    {
        l->is_dead = 1;
    }

    l->squish_factor += (l->squish_amount - l->squish_factor) * 0.5F;
    l->prev_squish_factor = l->squish_factor;
    l->slime_was_on_ground = l->e.on_ground;
}

/* EntitySlime.makesSoundOnLand: the magma cube always, the slime only size 4. */
static int slime_makes_sound_on_land(struct living *l)
{
    return l->kind == SK_MAGMA_CUBE || l->slime_size > 2;
}

/* EntitySlime.alterSquishAmount. */
void slime_alter_squish(struct living *l)
{
    l->squish_amount *= (l->kind == SK_MAGMA_CUBE) ? 0.9F : 0.6F;
}

/* EntitySlime.onUpdate's tail, after super. */
void slime_post_on_update(struct living *l, det_state *det)
{
    (void)det;
    int var1 = l->slime_was_on_ground;

    if (l->e.on_ground && !var1)
    {
        int var2 = l->slime_size;

        for (int var3 = 0; var3 < var2 * 8; ++var3)
        {
            float var4 = det_rng_float(&l->rand) * 3.1415927F * 2.0F;
            float var5 = det_rng_float(&l->rand) * 0.5F + 0.5F;
            float var6 = mh_sin(var4) * (float)var2 * 0.5F * var5;
            float var7 = mh_cos(var4) * (float)var2 * 0.5F * var5;
            /* World.spawnParticle("slime"/"flame", ...) is a client-side effect:
             * the draws above are the whole server-side cost */
            (void)var6;
            (void)var7;
        }

        if (slime_makes_sound_on_land(l))
        {
            /* playSound's pitch: (rand.nextFloat() - rand.nextFloat()) * 0.2F + 1.0F */
            (void)det_rng_float(&l->rand);
            (void)det_rng_float(&l->rand);
        }

        l->squish_amount = -0.5F;
    }
    else if (!l->e.on_ground && var1)
    {
        l->squish_amount = 1.0F;
    }

    slime_alter_squish(l);

    /* the client half (setSize from getSlimeSize) is dead on the server */
}

/* --------------------------------------------- updateEntityActionState */

/* EntitySlime.getJumpDelay / EntityMagmaCube.getJumpDelay. */
int slime_get_jump_delay(struct living *l, det_state *det)
{
    (void)det;
    int d = det_rng_int_n(&l->rand, 20) + 10;

    if (l->kind == SK_MAGMA_CUBE) d = d * 4;

    return d;
}

/* EntityMagmaCube.jump. */
void slime_jump_override(struct living *l)
{
    l->e.motion_y = (double)(0.42F + (float)l->slime_size * 0.1F);
    l->is_air_borne = 1;
}

/* EntitySlime.updateEntityActionState. */
void slime_action_state(struct living *l, det_state *det)
{
    living_despawn(l);
    struct living *var1 = an_closest_vulnerable_player(l->an, l->e.pos_x, l->e.pos_y, l->e.pos_z, 16.0);

    if (var1 != NULL)
    {
        living_face_entity(l, var1, 10.0F, 20.0F);
    }

    int jump = 0;

    if (l->e.on_ground)
    {
        int old = l->slime_jump_delay--;
        if (old <= 0) jump = 1;
    }

    if (jump)
    {
        l->slime_jump_delay = slime_get_jump_delay(l, det);

        if (var1 != NULL)
        {
            l->slime_jump_delay /= 3;
        }

        l->is_jumping = 1;

        /* makesSoundOnJump is size > 0 for both kinds */
        if (l->slime_size > 0)
        {
            /* playSound's pitch: (rand.nextFloat() - rand.nextFloat()) * 0.2F + 1.0F */
            (void)det_rng_float(&l->rand);
            (void)det_rng_float(&l->rand);
        }

        l->move_strafing = 1.0F - det_rng_float(&l->rand) * 2.0F;
        l->move_forward = (float)(1 * l->slime_size);
    }
    else
    {
        l->is_jumping = 0;

        if (l->e.on_ground)
        {
            l->move_strafing = 0.0F;
            l->move_forward = 0.0F;
        }
    }
}

/* ------------------------------------------------------------ the death */

/* EntitySlime.createInstance. The child is built with new EntitySlime(worldObj):
 * the spawn list's onSpawnWithEgg is not part of that path. The reference
 * registers the entity (worldObj.spawnEntityInWorld) only after
 * setLocationAndAngles has put it where it lands, and that registration is what
 * every getEntitiesWithinAABB query in the rest of the tick reads, so the child
 * is constructed at its final position rather than at the origin. */
static struct living *slime_create_instance(struct living *l, double x, double y, double z, float yaw)
{
    /* the constructor's draws on the world's own streams (a replay's
     * server), as EntityAIMate.spawnBaby's child takes them */
    struct an_world *an = l->an;
    if (an->constructor_hook) an->constructor_hook(an, 1, an->constructor_ctx);
    struct living *child = an_spawn_slime(an, l->kind, an->n, x, y, z, yaw, 0.0F, 1, 0);
    if (an->constructor_hook) an->constructor_hook(an, 0, an->constructor_ctx);
    /* spawnEntityInWorld: loadedEntityList, which the natural spawner's
     * countEntities(IMob) reads */
    if (an->track_spawn) an->track_spawn(an, child, an->constructor_ctx);
    return child;
}

/* EntitySlime.setDead. */
void slime_set_dead(struct living *l, det_state *det)
{
    int var1 = l->slime_size;

    if (living_is_client_world(l) && var1 > 1 && l->health <= 0.0F)
    {
        int var2 = 2 + det_rng_int_n(&l->rand, 3);

        for (int var3 = 0; var3 < var2; ++var3)
        {
            float var4 = ((float)(var3 % 2) - 0.5F) * (float)var1 / 4.0F;
            float var5 = ((float)(var3 / 2) - 0.5F) * (float)var1 / 4.0F;

            /* the child's constructor draws from the child's own Random and the
             * role streams; the yaw is the parent's nextFloat. The two Random
             * streams are independent, so taking the yaw first is exact. */
            float yaw = det_rng_float(&l->rand) * 360.0F;
            struct living *child = slime_create_instance(l, l->e.pos_x + (double)var4, l->e.pos_y + 0.5,
                                                         l->e.pos_z + (double)var5, yaw);

            if (child == NULL) continue;

            slime_set_size(child, var1 / 2);
        }
    }

    (void)det;
    living_set_dead(l);
}

/* EntitySlime.func_146068_u / the drop table of EntityLiving.dropFewItems and
 * EntityMagmaCube.dropFewItems. */
void slime_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det)
{
    (void)hit_by_player;
    (void)det;

    if (l->kind == SK_MAGMA_CUBE)
    {
        /* EntityMagmaCube.dropFewItems: always magma cream, only when size > 1 */
        if (l->slime_size > 1)
        {
            int var4 = det_rng_int_n(&l->rand, 4) - 2;

            if (looting > 0) var4 += det_rng_int_n(&l->rand, looting + 1);

            for (int var5 = 0; var5 < var4; ++var5)
            {
                an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y + 0.0, l->e.pos_z, ITEM_MAGMA_CREAM, 0, 1, 10);
            }
        }

        return;
    }

    /* EntitySlime.func_146068_u is the slime ball only at size 1, null otherwise;
     * EntityLiving.dropFewItems draws nothing when the item is null */
    if (l->slime_size != 1) return;

    int var4 = det_rng_int_n(&l->rand, 3);

    if (looting > 0) var4 += det_rng_int_n(&l->rand, looting + 1);

    for (int var5 = 0; var5 < var4; ++var5)
    {
        an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y + 0.0, l->e.pos_z, ITEM_SLIME_BALL, 0, 1, 10);
    }
}

/* ------------------------------------------------------------ the attack */

int slime_can_despawn(struct living *l)
{
    return 1;
}

/* EntityLivingBase.canEntityBeSeen. */
static int slime_can_entity_be_seen(struct living *l, struct living *other)
{
    double ex = other->e.pos_x;
    double ey = other->e.pos_y + (double)living_eye_height(other);
    double ez = other->e.pos_z;
    struct rt_mop hit;
    return raytrace_trace(l->world, l->e.pos_x, l->e.pos_y + (double)living_eye_height(l), l->e.pos_z, ex, ey, ez, &hit) == 0;
}

/* EntitySlime.onCollideWithPlayer. The player's own tick calls this. */
void slime_on_collide_with_player(struct living *l, struct living *player, det_state *det)
{
    int var2;
    int damage;

    if (l->kind == SK_MAGMA_CUBE)
    {
        var2 = l->slime_size;
        damage = l->slime_size + 2;
    }
    else
    {
        if (l->slime_size <= 1) return;   /* canDamagePlayer: size > 1 */
        var2 = l->slime_size;
        damage = l->slime_size;
    }

    double dx = l->e.pos_x - player->e.pos_x;
    double dy = l->e.pos_y - player->e.pos_y;
    double dz = l->e.pos_z - player->e.pos_z;

    if (slime_can_entity_be_seen(l, player) && dx * dx + dy * dy + dz * dz < 0.6 * (double)var2 * 0.6 * (double)var2
        && player_attack_entity_from(player, l, DMG_MOB, (float)damage, 1, det))
    {
        /* playSound("mob.attack", 1.0F, (rand.nextFloat() - rand.nextFloat()) * 0.2F + 1.0F) */
        (void)det_rng_float(&l->rand);
        (void)det_rng_float(&l->rand);
    }
}

/* --------------------------------------------------------- the probe player */

/* EntityPlayer.attackEntityFrom for the probe's player. scaled is
 * DamageSource.isDifficultyScaled() (true for a mob's attack, false for the
 * probe's generic damage and the starvation tick). */
int player_attack_entity_from(struct living *l, struct living *attacker, int source, float amount, int scaled, det_state *det)
{
    /* EntityPlayer.attackEntityFrom */
    if (l->invulnerable) return 0;
    if (l->health <= 0.0F) return 0;

    l->entity_age = 0;

    if (scaled)
    {
        /* the probe pins NORMAL: PEACEFUL zeroes, EASY halves + 1, HARD * 3/2 */
        (void)PROBE_DIFFICULTY_NORMAL;
    }

    if (amount == 0.0F) return 0;

    return living_attack_entity_from_attacker(l, attacker, source, amount, det);
}

/* EntityPlayer.damageEntity. The player overrides the same pipeline
 * EntityLivingBase.damageEntity runs (armour, the resistance potion, the
 * enchantment roll, absorption) and adds the hunger cost of the hit. */
void player_damage_entity(struct living *l, int source, float amount, det_state *det)
{
    if (l->invulnerable) return;

    /* isBlocking is false (no item in use) */
    float before = l->health;
    living_damage_entity(l, source, amount, det);

    /* the block vanilla wraps addExhaustion in runs only when the damage that
     * reached the health was not zero */
    if (l->health != before)
    {
        /* addExhaustion(DamageSource.getHungerDamage()): 0.3F for the mob, fire
         * and explosion sources, zero for the ones that bypass armour */
        player_add_exhaustion(l, living_dmg_hunger(source));
    }
}

/* EntityPlayer.shouldHeal. */
static int player_should_heal(struct living *l)
{
    return l->health > 0.0F && l->health < living_max_health(l);
}

/* EntityPlayer.heal through EntityLivingBase.heal. */
static void player_heal(struct living *l, float v)
{
    float cur = l->health;

    if (cur > 0.0F) living_set_health(l, cur + v);
}

/* FoodStats.onUpdate. */
static void player_food_tick(struct living *l, det_state *det)
{
    l->prev_food_level = l->food_level;

    if (l->food_exhaustion > 4.0F)
    {
        l->food_exhaustion -= 4.0F;

        if (l->food_saturation > 0.0F)
        {
            l->food_saturation -= 1.0F;
            if (l->food_saturation < 0.0F) l->food_saturation = 0.0F;
        }
        else if (PROBE_DIFFICULTY_NORMAL != PROBE_DIFFICULTY_PEACEFUL)
        {
            l->food_level -= 1;
            if (l->food_level < 0) l->food_level = 0;
        }
    }

    /* naturalRegeneration is the default game rule, on */
    if (l->food_level >= 18 && player_should_heal(l))
    {
        ++l->food_timer;

        if (l->food_timer >= 80)
        {
            player_heal(l, 1.0F);
            player_add_exhaustion(l, 3.0F);
            l->food_timer = 0;
        }
    }
    else if (l->food_level <= 0)
    {
        ++l->food_timer;

        if (l->food_timer >= 80)
        {
            if (l->health > 10.0F || PROBE_DIFFICULTY_NORMAL == PROBE_DIFFICULTY_HARD
                || (l->health > 1.0F && PROBE_DIFFICULTY_NORMAL == PROBE_DIFFICULTY_NORMAL))
            {
                player_attack_entity_from(l, NULL, DMG_STARVE, 1.0F, 0, det);
            }

            l->food_timer = 0;
        }
    }
    else
    {
        l->food_timer = 0;
    }
}

/* EntityPlayer.onLivingUpdate, with EntityLivingBase.onLivingUpdate in the
 * middle. */
void player_on_living_update(struct living *l, det_state *det)
{
    /* EntityPlayer.onLivingUpdate's head: the PEACEFUL heal (the probe pins
     * NORMAL), inventory.decrementAnimations (over plain stacks it draws and
     * changes nothing the tick reads) and prevCameraYaw = cameraYaw (a render
     * field nothing here reads) */

    living_default_on_living_update(l, det);

    /* the tail */
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.10000000149011612);   /* (double)capabilities.getWalkSpeed() */
    l->land_movement_factor = (float)attrs_value(&l->attrs.a[ATTR_MOVEMENT_SPEED]);
    /* jumpMovementFactor = speedInAir (0.02F), the constant moveEntityWithHeading
     * already uses; isSprinting() is false */

    /* the camera yaw and pitch smoothing after it writes only the player's
     * render fields, which nothing here reads */
    if (l->health > 0.0F)
    {
        struct aabb var4 = aabb_expand(l->e.bounding_box, 1.0, 0.5, 1.0);
        AN_QUERY_LIST(var5);
        int n = an_entities_excluding(l->an, NULL, &var4, var5, AN_MAX_ENTITIES);

        for (int var6 = 0; var6 < n; ++var6)
        {
            if (lv_get(var5[var6]->livh) == l) continue;
            if (!var5[var6]->is_living) continue;

            /* EntityItem.onCollideWithPlayer is also called, but the probe's
             * inventory is full, so the pickup always returns without a draw */
            if (!IS_SLIME_KIND(lv_get(var5[var6]->livh)->kind)) continue;

            if (lv_get(var5[var6]->livh)->is_dead) continue;

            slime_on_collide_with_player(lv_get(var5[var6]->livh), l, det);
        }
    }
}

/* MathHelper.sqrt_double: (double)(float)Math.sqrt(x). */
static double sqrt_double(double v)
{
    return (double)(float)sqrt(v);
}

/* Math.round(float): floor(x + 0.5F) as int. */
static int round_float(float f)
{
    return (int)floor((double)(f + 0.5F));
}

/* EntityPlayer.addMovementStat: the probe's player never rides, is never in
 * water and never on a ladder, so the ground branch is the one that draws. */
void player_add_movement_stat(struct living *l, double dx, double dy, double dz)
{
    /* nothing rides in the probe */

    if (living_is_inside_of_material(l, 6))
    {
        int var7 = round_float((float)sqrt_double(dx * dx + dy * dy + dz * dz) * 100.0F);
        if (var7 > 0) player_add_exhaustion(l, 0.015F * (float)var7 * 0.01F);
    }
    else if (living_is_in_water(l))
    {
        int var7 = round_float((float)sqrt_double(dx * dx + dz * dz) * 100.0F);
        if (var7 > 0) player_add_exhaustion(l, 0.015F * (float)var7 * 0.01F);
    }
    else if (living_is_on_ladder(l))
    {
        /* StatList.distanceClimbedStat only: (int)Math.round(dy * 100.0D) */
    }
    else if (l->e.on_ground)
    {
        int var7 = round_float((float)sqrt_double(dx * dx + dz * dz) * 100.0F);

        if (var7 > 0)
        {
            /* isSprinting() is false for the probe's player */
            player_add_exhaustion(l, 0.01F * (float)var7 * 0.01F);
        }
    }
}

/* EntityPlayer.addExhaustion. */
void player_add_exhaustion(struct living *l, float v)
{
    /* capabilities.disableDamage is false and the world is the server's */
    l->food_exhaustion += v;
    if (l->food_exhaustion > 40.0F) l->food_exhaustion = 40.0F;
}

/* EntityPlayer.onUpdate's tail. */
void player_after_update(struct living *l, det_state *det)
{
    /* the open-container check, the burning-creative extinguish and the
     * field_7109x camera trail draw and change nothing this lane reads */
    player_food_tick(l, det);
}

/* ----------------------------------------------------------- the spawn path */

/* The probe's player starts at its full 2000 health with vanilla food stats. */
void player_spawn_setup(struct living *l)
{
    attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 2000.0);
    /* EntityLivingBase.applyEntityAttributes on a non-AI entity */
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.10000000149011612);
    living_set_health(l, 2000.0F);
    l->food_level = 20;
    l->food_saturation = 5.0F;
    l->food_exhaustion = 0.0F;
    l->food_timer = 0;
    l->prev_food_level = 20;
    l->on_living_update = player_on_living_update;
    l->e.y_offset = 1.62F;
    living_set_size(l, 0.6F, 1.8F);
}

struct living *an_spawn_slime(struct an_world *an, int kind, int spawn_index, double x, double y, double z,
                              float yaw, float pitch, int size, int with_egg)
{
    struct living *l = living_alloc();

    living_init(l, an->w, kind, an->det);
    l->an = an;
    l->dimension = an->dimension;
    slime_construct(l, an->det);
    living_set_location_and_angles(l, x, y, z, yaw, pitch);

    /* the probe's forced size (setSlimeSize, no draw) */
    slime_set_size(l, size);

    /* EntityLiving.onSpawnWithEgg(null): the spawn list's random follow-range
     * bonus, from the entity's own Random. Only the spawn-list path runs it;
     * EntitySlime.setDead's children do not (see an_spawn_living). */
    if (with_egg) living_on_spawn_with_egg(l, an->det);

    an_add_living(an, l, spawn_index);
    return l;
}

struct living *an_spawn_player(struct an_world *an, int spawn_index, double x, double y, double z,
                               float yaw, float pitch)
{
    struct living *l = living_alloc();

    living_init(l, an->w, SK_PLAYER, an->det);
    l->an = an;
    l->dimension = an->dimension;
    player_spawn_setup(l);

    /* the record carries the entity's posY; setLocationAndAngles takes the
     * feet and adds yOffset (1.62 for the player) */
    living_set_location_and_angles(l, x, y - (double)l->e.y_offset, z, yaw, pitch);

    an_add_living(an, l, spawn_index);
    an->playerh = lv_ref(l);
    an->has_player = 1;
    return l;
}

/* --------------------------------------------- World.getClosestVulnerable... */

struct living *an_closest_player(struct an_world *an, double x, double y, double z, double range)
{
    /* World.getClosestPlayerToEntity: no vulnerable/health/isDead gate, just
     * the range. The probe's player is the only entry of playerEntities in
     * range; the harness player sits tens of thousands of blocks away. */
    if (an->playerh == 0) return NULL;

    struct living *p = lv_get(an->playerh);

    double dx = p->e.pos_x - x;
    double dy = p->e.pos_y - y;
    double dz = p->e.pos_z - z;
    double d = dx * dx + dy * dy + dz * dz;

    if (range < 0.0 || d < range * range) return p;

    return NULL;
}

struct living *an_closest_vulnerable_player(struct an_world *an, double x, double y, double z, double range)
{
    /* entity != null, the player's max health is 2000 and it never dies
     * (World.getClosestVulnerablePlayer) */
    if (an->playerh == 0) return NULL;

    struct living *p = lv_get(an->playerh);

    if (p->is_dead || p->health <= 0.0F) return NULL;

    double dx = p->e.pos_x - x;
    double dy = p->e.pos_y - y;
    double dz = p->e.pos_z - z;
    double d = dx * dx + dy * dy + dz * dz;
    double r = range;

    /* isSneaking(): the server player's flag from its C0B packets shrinks
     * the range to 0.8 of it (isInvisible's armour cut is not ported);
     * the probe's player has no server player and never sneaks */
    if (p->player_sp != NULL && p->player_sp->sneaking) r = range * 0.800000011920929;

    if ((range < 0.0 || d < r * r))
    {
        return p;
    }

    return NULL;
}

/* -------------------------------------------------------------- the record */

static uint64_t fnv64(const char *s)
{
    uint64_t h = 0xcbf29ce484222325ULL;

    for (const unsigned char *p = (const unsigned char *)s; *p; ++p)
    {
        h = (h ^ (uint64_t)*p) * 0x100000001b3ULL;
    }

    return h;
}

static uint32_t fbits(float f)
{
    uint32_t u;
    memcpy(&u, &f, sizeof u);
    return u;
}

static void put_i32(unsigned char *b, int o, int32_t v)
{
    for (int i = 0; i < 4; ++i) b[o + i] = (unsigned char)(v >> (8 * i));
}

static void put_u64(unsigned char *b, int o, uint64_t v)
{
    for (int i = 0; i < 8; ++i) b[o + i] = (unsigned char)(v >> (8 * i));
}

void slime_write_state(struct an_world *an, const struct an_ent *en, unsigned char *rec)
{
    (void)an;
    memset(rec, 0, SLIME_REC_BYTES);

    int kind = 0;
    uint64_t hash;
    int ticks_existed;
    int entity_age = 0;
    int jump_delay = 0;
    float move_forward = 0.0F, move_strafing = 0.0F;
    float rotation_yaw_head = 0.0F, render_yaw_offset = 0.0F;
    float squish_amount = 0.0F, squish_factor = 0.0F, prev_squish_factor = 0.0F;
    int living_sound_time = 0, jump_ticks = 0, hurt_resistant_time = 0, max_hurt_resistant_time = 0;
    int recently_hit = 0;
    float last_damage = 0.0F;
    int flags = 0;
    uint64_t rand_state;
    int slime_size = 0, hurt_time = 0, max_hurt_time = 0;

    if (en->is_living)
    {
        struct living *l = lv_get(en->livh);
        kind = l->kind == SK_SLIME ? 0 : (l->kind == SK_MAGMA_CUBE ? 1 : 2);

        char *text;
        living_nbt_text(l, &text);
        hash = fnv64(text);
        free(text);

        ticks_existed = l->ticks_existed;
        entity_age = l->entity_age;
        jump_delay = l->slime_jump_delay;
        move_forward = l->move_forward;
        move_strafing = l->move_strafing;
        rotation_yaw_head = l->rotation_yaw_head;
        render_yaw_offset = l->render_yaw_offset;
        squish_amount = l->squish_amount;
        squish_factor = l->squish_factor;
        prev_squish_factor = l->prev_squish_factor;
        living_sound_time = l->living_sound_time;
        jump_ticks = l->jump_ticks;
        hurt_resistant_time = l->hurt_resistant_time;
        max_hurt_resistant_time = l->max_hurt_resistant_time;
        recently_hit = l->recently_hit;
        last_damage = l->last_damage;
        flags = (l->added_to_chunk ? 1 : 0) | (l->e.on_ground ? 2 : 0) | (l->is_jumping ? 4 : 0)
            | (l->is_air_borne ? 8 : 0) | (l->is_in_water ? 16 : 0) | (l->e.is_collided_horizontally ? 32 : 0);
        rand_state = det_rng_state(&l->rand);
        slime_size = l->slime_size;
        hurt_time = l->hurt_time;
        max_hurt_time = l->max_hurt_time;
    }
    else
    {
        ie_ent *ie = ie_get(en->ieh);
        kind = -1;

        char *text;
        item_entity_nbt_text(ie, &text);
        hash = fnv64(text);
        free(text);

        ticks_existed = ie->ticks_existed;
        /* Entity.isAirBorne stays false for an item: nothing jumps or knocks
         * one back, so bit 3 is always 0 */
        flags = (ie->added_to_chunk ? 1 : 0) | (ie->e.on_ground ? 2 : 0)
            | (ie->e.in_water ? 16 : 0) | (ie->e.is_collided_horizontally ? 32 : 0);
        rand_state = det_rng_state(&ie->rand);
    }

    put_i32(rec, 0, en->spawn_index);
    put_i32(rec, 4, en->is_living ? lv_get(en->livh)->entity_id : ie_get(en->ieh)->entity_id);
    put_i32(rec, 8, kind);
    put_u64(rec, 12, hash);
    put_i32(rec, 20, ticks_existed);
    put_i32(rec, 24, entity_age);
    put_i32(rec, 28, jump_delay);
    put_i32(rec, 32, (int32_t)fbits(move_forward));
    put_i32(rec, 36, (int32_t)fbits(move_strafing));
    put_i32(rec, 40, (int32_t)fbits(rotation_yaw_head));
    put_i32(rec, 44, (int32_t)fbits(render_yaw_offset));
    put_i32(rec, 48, (int32_t)fbits(squish_amount));
    put_i32(rec, 52, (int32_t)fbits(squish_factor));
    put_i32(rec, 56, (int32_t)fbits(prev_squish_factor));
    put_i32(rec, 60, living_sound_time);
    put_i32(rec, 64, jump_ticks);
    put_i32(rec, 68, hurt_resistant_time);
    put_i32(rec, 72, max_hurt_resistant_time);
    put_i32(rec, 76, recently_hit);
    put_i32(rec, 80, (int32_t)fbits(last_damage));
    put_i32(rec, 84, flags);
    put_u64(rec, 88, rand_state);
    put_i32(rec, 96, slime_size);
    put_i32(rec, 100, hurt_time);
    put_i32(rec, 104, max_hurt_time);
}

void slime_write_player(struct an_world *an, unsigned char *rec)
{
    (void)an;
    memset(rec, 0, SLIME_PLAYER_BYTES);
    struct living *l = lv_get(an->playerh);

    if (l == NULL) return;

    put_i32(rec, 0, l->entity_id);
    put_i32(rec, 4, (int32_t)fbits(l->health));
    put_i32(rec, 8, (int32_t)fbits(l->prev_health));
    put_i32(rec, 12, (int32_t)fbits(l->absorption));
    put_i32(rec, 16, (int32_t)fbits(l->last_damage));
    put_i32(rec, 20, l->entity_age);
    put_i32(rec, 24, l->death_time);
    put_i32(rec, 28, l->hurt_time);
    put_i32(rec, 32, l->max_hurt_time);
    put_i32(rec, 36, l->hurt_resistant_time);
    put_i32(rec, 40, l->max_hurt_resistant_time);
    put_i32(rec, 44, l->recently_hit);
    put_i32(rec, 48, l->food_level);
    put_i32(rec, 52, (int32_t)fbits(l->food_saturation));
    put_i32(rec, 56, (int32_t)fbits(l->food_exhaustion));
    put_i32(rec, 60, l->food_timer);
    put_i32(rec, 64, l->prev_food_level);
    double pos[3] = { l->e.pos_x, l->e.pos_y, l->e.pos_z };
    double mot[3] = { l->e.motion_x, l->e.motion_y, l->e.motion_z };
    memcpy(rec + 68, pos, sizeof pos);
    memcpy(rec + 92, mot, sizeof mot);
    put_i32(rec, 116, (int32_t)fbits(l->rotation_yaw));
    put_i32(rec, 120, (int32_t)fbits(l->rotation_pitch));
    put_i32(rec, 124, (l->e.on_ground ? 1 : 0) | (l->added_to_chunk ? 2 : 0) | (l->is_air_borne ? 4 : 0)
        | (l->is_in_water ? 8 : 0));
    put_u64(rec, 128, det_rng_state(&l->rand));
}