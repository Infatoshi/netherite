/* EntityGhast (1.7.10) and the EntityLargeFireball it fires, over the living
 * and item pools, checked tick by tick against the oracle's GhastProbe dump
 * (oracle/harness/netherite/oracle/GhastProbe.java).
 *
 * Ported here: the ghast's updateEntityActionState (the wandering waypoint and
 * isCourseTraversable's bounding-box sweep through the world), the targeting
 * (World.getClosestVulnerablePlayerToEntity over the probe player, then
 * canEntityBeSeen's eye ray), the attack counter and the fireball spawn, the
 * fireball's EntityLargeFireball.onImpact (the 6.0F fireball damage and the
 * explosion), the ghast's damage (EntityLivingBase.attackEntityFrom) and death
 * (dropFewItems, the 20-tick death), and the probe player the ghasts shoot
 * at: its damage paths (EntityPlayer.attackEntityFrom with the difficulty
 * scale, knockBack with the shooter as the source) recorded every tick.
 *
 * The fireball's flight is projectile.c's IE_LARGE_FIREBALL branch, which
 * calls back here for the impact (iew->on_fireball_impact); the explosion is
 * explosion.c's expl_run_extras with the ghasts and the player as the outside
 * entities of the damage pass.
 */
#include "ghasts.h"
#include "env.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "explosion.h"
#include "projectile.h"
#include "collide.h"
#include "jmath.h"
#include "raytrace.h"
#include "trace.h"

/* pi as the double constant the degree trig widens */
#define GH_PI 3.141592653589793

/* MathHelper.sqrt_double: the square root rounded to float, widened back. */
static double sqrt_double(double v)
{
    return (double)(float)sqrt(v);
}

/* The aiming angle: (float)StrictMath.atan2(x, z) * 180.0F / (float)PI. The
 * aiming line negates it. */
static float aim_yaw(double dx, double dz)
{
    return (float)atan2(dx, dz) * 180.0F / (float)GH_PI;
}

/* EntityGhast.getLook(1.0F): the exact-1.0 branch of EntityLivingBase.getLook,
 * the angles through MathHelper's sin/cos table. */
void ghast_look_vec(struct living *l, double *x, double *y, double *z)
{
    float v2 = mh_cos(-l->rotation_yaw * 0.017453292F - (float)GH_PI);
    float v3 = mh_sin(-l->rotation_yaw * 0.017453292F - (float)GH_PI);
    float v4 = -mh_cos(-l->rotation_pitch * 0.017453292F);
    float v5 = mh_sin(-l->rotation_pitch * 0.017453292F);
    *x = (double)(v3 * v4);
    *y = (double)v5;
    *z = (double)(v2 * v4);
    /* Vec3's constructor turns a -0.0 component into 0.0 */
    if (*x == 0.0) *x = 0.0;
    if (*y == 0.0) *y = 0.0;
    if (*z == 0.0) *z = 0.0;
}

/* ------------------------------------------------------- the constructor */

void ghast_construct(struct living *l, det_state *det)
{
    (void)det;
    living_set_base_size(l, 4.0F, 4.0F);
    l->immune_to_fire = 1;
    l->experience_value = 5;
    l->explosion_power = 1;
}

/* ---------------------------------------------------- EntityGhast damage */

/* EntityGhast.attackEntityFrom: the fireball-from-player branch needs a player
 * attacker, which no probe source has, so the body is EntityLivingBase's,
 * carried by living_attack_entity_from_attacker (the fireball's 6.0F damage and
 * the explosion's both ride DamageSource entities: the shooting ghast). */
int ghast_attack_entity_from(struct living *l, struct living *attacker, int source, float amount, det_state *det)
{
    return living_attack_entity_from_attacker(l, attacker, source, amount, det);
}

/* EntityGhast.dropFewItems. */
void ghast_drop_few_items(struct living *l, int looting)
{
    int a = det_rng_int_n(&l->rand, 2);
    int b = det_rng_int_n(&l->rand, 1 + looting);
    int var3 = a + b;

    for (int i = 0; i < var3; ++i)
        an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 370, 0, 1, 10);

    int c = det_rng_int_n(&l->rand, 3);
    int d = det_rng_int_n(&l->rand, 1 + looting);
    var3 = c + d;

    for (int i = 0; i < var3; ++i)
        an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, 289, 0, 1, 10);
}

/* ------------------------------------------------ EntityGhast action state */

/* World.getClosestVulnerablePlayerToEntity over playerEntities: the probe
 * holds the one player, alive and vulnerable (capabilities.disableDamage
 * false), not sneaking, not invisible, so the range test is the plain one. */
/* the player a ghast targets: its world's view of it */
static struct gh_player *gh_target_player(struct living *l)
{
    return &((struct gh_world *)l->an->user_data)->player;
}

struct gh_player *gh_closest_vulnerable_player(struct an_world *an, struct living *l, double range)
{
    struct gh_world *gw = (struct gh_world *)an->user_data;

    if (!an->has_player) return NULL;

    struct gh_player *p = &gw->player;

    /* EntityPlayer.isEntityAlive: !isDead && health > 0 */
    if (p->is_dead || p->health <= 0.0F) return NULL;

    double dx = p->pos_x - l->e.pos_x;
    double dy = p->pos_y - l->e.pos_y;
    double dz = p->pos_z - l->e.pos_z;

    if (dx * dx + dy * dy + dz * dz >= range * range) return NULL;

    return p;
}

/* EntityLivingBase.canEntityBeSeen: the eye ray through the block raytrace.
 * The target's eye height is EntityPlayerMP's 1.62F. */
static int gh_can_entity_be_seen(struct living *l, struct gh_player *p)
{
    struct rt_mop mop;
    return !raytrace_blocks(l->world,
                            l->e.pos_x, l->e.pos_y + (double)living_eye_height(l), l->e.pos_z,
                            p->pos_x, p->pos_y + (double)1.62F, p->pos_z, 0, 0, 0, &mop);
}

/* EntityGhast.isCourseTraversable: the bounding box swept one course step at a
 * time, the collision pool empty every step. World.getCollidingBoundingBoxes'
 * entity half adds nothing here: Entity.getBoundingBox() is null and the
 * ghast's getCollisionBox is Entity's null, so only the blocks can block. */
static int ghast_course_traversable(struct living *l, double wx, double wy, double wz, double dist)
{
    double dx = (wx - l->e.pos_x) / dist;
    double dy = (wy - l->e.pos_y) / dist;
    double dz = (wz - l->e.pos_z) / dist;
    struct aabb box = aabb_copy(l->e.bounding_box);

    for (int i = 1; (double)i < dist; ++i)
    {
        box.min_x += dx;
        box.min_y += dy;
        box.min_z += dz;
        box.max_x += dx;
        box.max_y += dy;
        box.max_z += dz;

        if (!world_colliding_boxes_empty(l->world, box)) return 0;
    }

    return 1;
}

/* the fireball: EntityFireball's (world, shooter, ax, ay, az) constructor, the
 * getLook(1.0F) offset, and spawnEntityInWorld */
static void ghast_fire_fireball(struct living *l, double tx, double ty, double tz)
{
    struct an_world *an = l->an;
    struct ie_world *iew = &an->iew;

    /* the constructor's nextGaussian spread, on the fireball's own Random -
     * which is created inside proj_spawn_fireball, so the draws run through
     * the spawn's seed state below */
    (void)tx; (void)ty; (void)tz;

    ie_ent *fb = proj_spawn_fireball(iew, IE_LARGE_FIREBALL,
                                     l->e.pos_x, l->e.pos_y, l->e.pos_z,
                                     0.0, 0.0, 0.0);
    if (!fb) return;

    fb->shooter = lv_ref(l);
    fb->shooter_is_player = 0;

    /* the three nextGaussian*0.4 draws, then the normalisation the
     * constructor does */
    double ax = tx + det_rng_gaussian(&fb->rand) * 0.4;
    double ay = ty + det_rng_gaussian(&fb->rand) * 0.4;
    double az = tz + det_rng_gaussian(&fb->rand) * 0.4;
    double len = sqrt_double(ax * ax + ay * ay + az * az);
    fb->accel_x = ax / len * 0.1;
    fb->accel_y = ay / len * 0.1;
    fb->accel_z = az / len * 0.1;

    /* the constructor's setLocationAndAngles(shooter...): the shooter's yaw
     * and pitch, motion 0 */
    fb->prev_yaw = fb->rotation_yaw = l->rotation_yaw;
    fb->prev_pitch = fb->rotation_pitch = l->rotation_pitch;
    fb->e.motion_x = fb->e.motion_y = fb->e.motion_z = 0.0;

    /* the spawn's getLook(1.0F) offset, written to posX, posY and posZ
     * alone: the bounding box stays where the constructor's setPosition put
     * it, on the ghast, until the fireball's first move (its first
     * handleLavaMovement and entity sweep read that box) */
    double lx, ly, lz;
    ghast_look_vec(l, &lx, &ly, &lz);
    fb->e.pos_x = l->e.pos_x + lx * 4.0;
    fb->e.pos_y = l->e.pos_y + (double)(l->e.height / 2.0F) + 0.5;
    fb->e.pos_z = l->e.pos_z + lz * 4.0;
    ie_added_to_world(iew, fb);   /* spawnEntityInWorld's chunk add, at that position */


    /* the probe's list takes the fireball at the next spawn index */
    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 0;
    en->spawn_index = an->n;
    en->livh = 0;
    en->ieh = ie_ref(fb);
    an_list_push(an, en);
}

void ghast_update_entity_action_state(struct living *l)
{
    struct an_world *an = l->an;

    /* setDead at PEACEFUL; the rest of the action state still runs */
    if (living_server_peaceful(l)) l->is_dead = 1;

    /* EntityLiving.despawnEntity, the ghast's own call: the player sits within
     * 1024 blocks so entityAge resets, and the age>600 branch short-circuits
     * before its draw */
    living_despawn_entity_pub(l);

    l->prev_attack_counter = l->attack_counter;
    double var1 = l->waypoint_x - l->e.pos_x;
    double var3 = l->waypoint_y - l->e.pos_y;
    double var5 = l->waypoint_z - l->e.pos_z;
    double var7 = var1 * var1 + var3 * var3 + var5 * var5;

    if (var7 < 1.0 || var7 > 3600.0)
    {
        float range = nw_env->cfg.ghast_negative_waypoint ? 12.0F : 16.0F;
        l->waypoint_x = l->e.pos_x + (double)((det_rng_float(&l->rand) * 2.0F - 1.0F) * range);
        l->waypoint_y = l->e.pos_y + (double)((det_rng_float(&l->rand) * 2.0F - 1.0F) * range);
        l->waypoint_z = l->e.pos_z + (double)((det_rng_float(&l->rand) * 2.0F - 1.0F) * range);
    }

    if (l->course_change_cooldown-- <= 0)
    {
        l->course_change_cooldown += det_rng_int_n(&l->rand, 5) + 2;
        var7 = sqrt_double(var7);

        if (ghast_course_traversable(l, l->waypoint_x, l->waypoint_y, l->waypoint_z, var7))
        {
            l->e.motion_x += var1 / var7 * 0.1;
            l->e.motion_y += var3 / var7 * 0.1;
            l->e.motion_z += var5 / var7 * 0.1;
        }
        else
        {
            l->waypoint_x = l->e.pos_x;
            l->waypoint_y = l->e.pos_y;
            l->waypoint_z = l->e.pos_z;
        }
    }

    /* the target: a dead target leaves the field null before the re-acquire */
    if (l->ghast_target)
    {
        struct gh_player *tp = l->ghast_target_is_player ? gh_target_player(l) : NULL;

        if (tp != NULL && tp->is_dead) l->ghast_target = 0;
    }

    if (!l->ghast_target || l->aggro_cooldown-- <= 0)
    {
        struct gh_player *p = gh_closest_vulnerable_player(an, l, 100.0);

        if (p != NULL)
        {
            l->ghast_target = 1;
            l->ghast_target_is_player = 1;
            l->aggro_cooldown = 20;
        }
        else if (!l->ghast_target)
        {
            /* keep the null */
        }
        else
        {
            /* aggroCooldown ran out with the target still alive and in range:
             * Java re-assigns whatever the query returns, null included */
            l->ghast_target = 0;
            l->ghast_target_is_player = 0;
        }
    }

    struct gh_player *target = l->ghast_target_is_player && l->ghast_target ? gh_target_player(l) : NULL;

    double dx = target ? target->pos_x - l->e.pos_x : 0.0;
    double dy = target ? target->pos_y - l->e.pos_y : 0.0;
    double dz = target ? target->pos_z - l->e.pos_z : 0.0;
    double dsq = dx * dx + dy * dy + dz * dz;

    if (target != NULL && dsq < 64.0 * 64.0)
    {
        double tx = target->pos_x - l->e.pos_x;
        double tz = target->pos_z - l->e.pos_z;
        double ty = (target->pos_y + (double)(1.8F / 2.0F)) - (l->e.pos_y + (double)(l->e.height / 2.0F));
        l->render_yaw_offset = l->rotation_yaw = -aim_yaw(tx, tz);

        if (gh_can_entity_be_seen(l, target))
        {
            if (l->attack_counter == 10)
            {
                /* playAuxSFXAtEntity 1007: the charge sound */
                env_aux_sfx(l->world, 1007, (int)l->e.pos_x, (int)l->e.pos_y, (int)l->e.pos_z, 0);
            }

            ++l->attack_counter;

            if (l->attack_counter == (nw_env->cfg.ghast_negative_attack ? 15 : 20))
            {
                /* playAuxSFXAtEntity 1008: the fire sound */
                env_aux_sfx(l->world, 1008, (int)l->e.pos_x, (int)l->e.pos_y, (int)l->e.pos_z, 0);
                ghast_fire_fireball(l, tx, ty, tz);
                l->attack_counter = -40;
            }
        }
        else if (l->attack_counter > 0)
        {
            --l->attack_counter;
        }
    }
    else
    {
        l->render_yaw_offset = l->rotation_yaw = -aim_yaw(l->e.motion_x, l->e.motion_z);

        if (l->attack_counter > 0) --l->attack_counter;
    }

    /* dataWatcher 16: the charge the client renders */
    int want16 = l->attack_counter > 10 ? 1 : 0;
    if (l->data_watcher_16 != want16) l->data_watcher_16 = want16;
}

/* ------------------------------------------- the fireball impact callback */

/* the damage pass's outside entities: the living ghasts and the player, in
 * Java's merged chunk-section order. The order within one explosion matters
 * only through the Math.random draws each living hit spends, and the probe's
 * entities sit in disjoint sections except where an explosion's box spans the
 * whole cavern, so the walk is the an_world's list in spawn order with the
 * player first. */
/* The gh_world the running explosion's damage pass uses: set by gh_expl_query
 * for the pass's duration (one explosion at a time, as in the oracle). */
#define gh_pass_world (nw_env->ghasts.pass_world)

static void gh_ghast_expl_attack(void *ent, float amount)
{
    struct living *l = (struct living *)ent;
    ghast_attack_entity_from(l, NULL, DMG_EXPLOSION, amount, l->an->det);
}

static void gh_ghast_expl_motion(void *ent, double mx, double my, double mz)
{
    struct living *l = (struct living *)ent;
    l->e.motion_x += mx;
    l->e.motion_y += my;
    l->e.motion_z += mz;
}

/* The gh_world the running explosion's damage pass reaches the player
 * through: set by gh_expl_query for the pass's duration (one explosion at a
 * time, as in the oracle). */

static void gh_player_expl_attack(void *ent, float amount)
{
    struct gh_player *p = (struct gh_player *)ent;
    (void)p;
    gh_player_attack_entity_from(gh_pass_world, DMG_EXPLOSION, amount, NULL, gh_pass_world->an->det);
}

static void gh_player_expl_motion(void *ent, double mx, double my, double mz)
{
    struct gh_player *p = (struct gh_player *)ent;
    p->motion_x += mx;
    p->motion_y += my;
    p->motion_z += mz;
}

static int gh_expl_query(void *ctx, struct aabb box, struct expl_extra *out, int cap)
{
    struct an_world *an = (struct an_world *)ctx;
    struct gh_world *gw = (struct gh_world *)an->user_data;
    gh_pass_world = gw;
    int n = 0;

    /* the player first: Java's list holds it at its chunk-section place, and
     * the probe's player spawns before every ghast. The replay's player is
     * its living twin: the damage goes through the twin's attack (the
     * server player's own), the push onto the twin's motion (the pass
     * copies it back, the S27 carries it) */
    struct living *twin = an->has_player && gw->player_livh != 0 ? lv_get(gw->player_livh) : NULL;
    if (twin != NULL && n < cap)
    {
        if (!twin->is_dead && aabb_intersects(&twin->e.bounding_box, &box))
        {
            struct expl_extra *e = &out[n++];
            e->ent = twin;
            e->pos = &twin->e.pos_x;
            e->eye_height = 1.62F;
            e->box = &twin->e.bounding_box;
            e->attack_from = gh_ghast_expl_attack;
            e->add_motion = gh_ghast_expl_motion;
            e->armor = twin;
        }
    }
    else if (an->has_player && n < cap)
    {
        struct aabb *pbb = &nw_env->ghasts_blast.pbb;
        struct gh_player *p = &gw->player;
        *pbb = aabb_make(p->pos_x - 0.3, p->pos_y, p->pos_z - 0.3,
                                p->pos_x + 0.3, p->pos_y + 1.8, p->pos_z + 0.3);

        if (aabb_intersects(pbb, &box))
        {
            struct expl_extra *e = &out[n++];
            e->ent = p;
            e->pos = &p->pos_x;
            e->eye_height = 1.62F;
            e->box = pbb;
            e->attack_from = gh_player_expl_attack;
            e->add_motion = gh_player_expl_motion;
            e->armor = NULL;
        }
    }

    for (int i = 0; i < an->n && n < cap; ++i)
    {
        struct an_ent *en = an_ent_at(an->slot[i]);

        if (!en->is_living) continue;

        struct living *l = lv_get(en->livh);

        if (l->is_dead || l == twin) continue;

        if (!aabb_intersects(&l->e.bounding_box, &box)) continue;

        struct expl_extra *e = &out[n++];
        e->ent = l;
        e->pos = &l->e.pos_x;
        e->eye_height = living_eye_height(l);
        e->box = &l->e.bounding_box;
        e->attack_from = gh_ghast_expl_attack;
        e->add_motion = gh_ghast_expl_motion;
        e->armor = l;
    }

    /* the owner's other pools (the replay's items, falling and hanging
     * entities), after the livings */
    if (gw != NULL && gw->blast_other != NULL && n < cap)
        n += gw->blast_other(gw->blast_other_ctx, box, out + n, cap - n);

    return n;
}

/* EntityLargeFireball.onImpact: the 6.0F fireball damage to the hit entity,
 * then the explosion at the fireball's own position, then setDead (projectile.c
 * sets the flag). hit kinds: 0 another ie_ent, 1 a living, 2 the player.
 * The test installs this as iew->on_fireball_impact. */
void gh_fireball_impact(struct ie_world *iew, struct ie_ent *en, void *hit, int hit_kind)
{
    /* a blaze's EntitySmallFireball in the same world: its own onImpact */
    if (en->kind == IE_SMALL_FIREBALL)
    {
        an_fireball_impact(iew, en, hit, hit_kind);
        return;
    }

    if (hit_kind == 1)
    {
        /* query_living hands out the an_world's list entries; the living rides
         * inside */
        struct an_ent *ae = (struct an_ent *)hit;
        struct living *victim = lv_get(ae->livh);
        /* EntityGhast.attackEntityFrom: a "fireball" source from a player
         * kills the ghast outright (super with 1000.0F); the shooter is the
         * deflecting player after a sword hit. Every other living (or a
         * fireball from a non-player shooter) takes the plain 6.0F. */
        struct living *shooter = lv_get(en->shooter);
        if (victim && victim->kind == GK_GHAST && en->kind == IE_LARGE_FIREBALL &&
            shooter && shooter->kind == HK_PLAYER)
        {
            living_attack_entity_from_attacker(victim, shooter, DMG_FIREBALL, 1000.0F, iew->det);
            /* then the shooter's triggerAchievement(ghast) */
            if (victim->an != NULL && victim->an->achievement_hook != NULL)
                victim->an->achievement_hook(victim->an, ACH_GHAST, victim->an->bred_ctx);
        }
        else if (victim)
        {
            /* causeFireballDamage(this, null) for a fireball read back from
             * a save (its shooter is not saved): the fireball is its own
             * source entity, the knockback's origin (the shooterless arrow's
             * path) */
            victim->hit_src_arrow = shooter == NULL;
            victim->hit_src_x = en->e.pos_x;
            victim->hit_src_z = en->e.pos_z;
            ghast_attack_entity_from(victim, shooter, DMG_FIREBALL, 6.0F, iew->det);
            victim->hit_src_arrow = 0;
        }
    }
    else if (hit_kind == 2)
    {
        struct gh_world *gw = (struct gh_world *)((struct an_world *)iew->user_data)->user_data;
        gh_player_attack_entity_from(gw, DMG_FIREBALL, 6.0F, lv_get(en->shooter), iew->det);
    }
    else if (hit_kind == 0 && hit != NULL)
    {
        /* EntityFireball.attackEntityFrom on the other fireball: setBeenAttacked,
         * then the motion and acceleration from the shooter's look vector */
        ie_ent *other = (ie_ent *)hit;
        other->e.velocity_changed = 1;

        if (en->shooter != 0)
        {
            struct living *shooter = lv_get(en->shooter);
            double lx, ly, lz;
            ghast_look_vec(shooter, &lx, &ly, &lz);
            other->e.motion_x = lx;
            other->e.motion_y = ly;
            other->e.motion_z = lz;
            other->accel_x = lx * 0.1;
            other->accel_y = ly * 0.1;
            other->accel_z = lz * 0.1;
        }
    }

    /* newExplosion(null, posX/Y/Z, power, flaming, mobGriefing): the exploder
     * is the shootingEntity, and the explosion's DamageSource carries it, so
     * every damaged entity takes the knockback branch from its position */
    struct an_world *an = (struct an_world *)iew->user_data;
    int iew_before = iew->n;
    expl_run_extras(an->w, iew->det, iew->role, &iew->world_rand.r, iew, NULL,
                    en->e.pos_x, en->e.pos_y, en->e.pos_z, (float)en->explosion_power,
                    1, 1, NULL, gh_expl_query, an);
    for (int k = iew_before; k < iew->n; ++k)
    {
        ie_ent *new_ie = ie_ent_at(iew->slot[k]);
        int already = 0;
        for (int j = 0; j < an->n; ++j)
        {
            if (!an_ent_at(an->slot[j])->is_living && ie_get(an_ent_at(an->slot[j])->ieh) == new_ie)
            {
                already = 1;
                break;
            }
        }
        if (already) continue;

        new_ie->dimension = an->dimension;
        new_ie->spawn_index = an->n;

        struct an_ent *new_en = an_ent_alloc();
        new_en->used = 1;
        new_en->is_living = 0;
        new_en->spawn_index = an->n;
        new_en->livh = 0;
        new_en->ieh = ie_ref(new_ie);
        an_list_push(an, new_en);
        an_chunk_add(an, new_en, mh_floor(new_ie->e.pos_x / 16.0),
                     mh_floor(new_ie->e.pos_y / 16.0),
                     mh_floor(new_ie->e.pos_z / 16.0));
    }
}

/* ------------------------------------------------ the probe player damage */

/* EntityPlayer.damageEntity: the blocking branch is dead (no shield use), the
 * armour empty, no potions, absorption 0. */
static void gh_player_damage_entity(struct gh_world *gw, int source, float amount)
{
    struct gh_player *p = &gw->player;

    /* applyArmorCalculations: not unblockable, 25 - 0 armor */
    if (!living_dmg_unblockable(source))
    {
        int v3 = 25;
        float v4 = amount * (float)v3;
        amount = v4 / 25.0F;
    }

    /* applyPotionDamageCalculations: no resistance; the enchantmentRand draw
     * the shared body runs whenever the amount survives to it */
    if (amount > 0.0F)
    {
        struct det_split *sp = det_split_find(gw->an->det,
            "./net/minecraft/enchantment/EnchantmentHelper.java:enchantmentRand");
        if (sp) (void)det_split_int_n(gw->an->det, sp, 1);
    }
    else
    {
        return;
    }

    /* absorption 0: the damage lands whole */
    p->health -= amount;
}

/* EntityPlayer.knockBack (EntityLivingBase's): the resistance roll is a
 * nextDouble against the attribute's 0, drawn by the caller. */
static void gh_player_knock_back(struct gh_world *gw, double dx, double dz)
{
    struct gh_player *p = &gw->player;
    float var7 = (float)sqrt(dx * dx + dz * dz);
    float var8 = 0.4F;
    p->motion_x /= 2.0;
    p->motion_y /= 2.0;
    p->motion_z /= 2.0;
    p->motion_x -= dx / (double)var7 * (double)var8;
    p->motion_y += (double)var8;
    p->motion_z -= dz / (double)var7 * (double)var8;

    if (p->motion_y > 0.4000000059604645) p->motion_y = 0.4000000059604645;
}

/* EntityPlayer.attackEntityFrom: the difficulty scale (normal: none), the stat
 * (no draw), then EntityLivingBase's body. source is DMG_FIREBALL or
 * DMG_EXPLOSION; shooter, when not NULL, is the fireball's shootingEntity and
 * takes the knockback branch. */
int gh_player_attack_entity_from(struct gh_world *gw, int source, float amount,
                                 struct living *shooter, det_state *det)
{
    struct gh_player *p = &gw->player;

    if (p->is_dead) return 0;   /* isEntityInvulnerable false */

    p->entity_age = 0;

    if (p->health <= 0.0F) return 0;

    /* the fire-resistance branch: the player holds no effects */

    /* EntityPlayer's addStat(damageTakenStat): no draw */

    int var3 = 1;

    if ((float)p->hurt_resistant_time > (float)20 / 2.0F)
    {
        if (amount <= p->last_damage) return 0;

        gh_player_damage_entity(gw, source, amount - p->last_damage);
        p->last_damage = amount;
        var3 = 0;
    }
    else
    {
        p->last_damage = amount;
        p->prev_health = p->health;
        p->hurt_resistant_time = 20;
        gh_player_damage_entity(gw, source, amount);
        p->max_hurt_time = 10;
        p->hurt_time = 10;
    }

    /* attackedAtYaw and the knockback */
    if (var3)
    {
        /* attackedAtYaw is recorded on nothing for the player */

        if (source != DMG_DROWN)
        {
            /* setBeenAttacked: the roll against the knockback resistance
             * attribute's 0 always leaves the flag on */
            p->velocity_changed = det_rng_double(&p->rand) >= 0.0F;
        }

        if (shooter != NULL)
        {
            double var9 = shooter->e.pos_x - p->pos_x;
            double var7 = shooter->e.pos_z - p->pos_z;

            while (var9 * var9 + var7 * var7 < 1.0e-4)
            {
                var9 = (det_math_random_role(det, DET_OTHER) - det_math_random_role(det, DET_OTHER)) * 0.01;
                var7 = (det_math_random_role(det, DET_OTHER) - det_math_random_role(det, DET_OTHER)) * 0.01;
            }

            /* knockBack: the rand.nextDouble() >= 0.0 resistance roll always
             * passes for the player's 0 */
            (void)det_rng_double(&p->rand);
            gh_player_knock_back(gw, var9, var7);
        }
        else
        {
            /* no attacker entity: the yaw is the entity-less one */
            (void)det_math_random_role(det, DET_OTHER);
        }
    }

    /* the sounds: the hurt and death pitches draw two floats each */
    if (p->health <= 0.0F)
    {
        if (var3)
        {
            (void)det_rng_float(&p->rand);
            (void)det_rng_float(&p->rand);
        }

        /* EntityLivingBase.onDeath for the player: nothing spawns (the drops
         * need recentlyHit, the score an attacker) */
        p->is_dead = 1;
    }
    else if (var3)
    {
        (void)det_rng_float(&p->rand);
        (void)det_rng_float(&p->rand);
    }

    return 1;
}

/* the same for the player on the receiving end. */
static void gh_player_apply_entity_collision(struct gh_world *gw, struct living *l)
{
    struct gh_player *p = &gw->player;
    double v2 = l->e.pos_x - p->pos_x;
    double v4 = l->e.pos_z - p->pos_z;
    double v6 = fabs(v2) > fabs(v4) ? fabs(v2) : fabs(v4);

    if (v6 >= 0.009999999776482582)
    {
        v6 = sqrt_double(v6);
        v2 /= v6;
        v4 /= v6;
        double v8 = 1.0 / v6;

        if (v8 > 1.0) v8 = 1.0;

        v2 *= v8;
        v4 *= v8;
        v2 *= 0.05000000074505806;
        v4 *= 0.05000000074505806;
        p->motion_x += -v2;
        p->motion_z += -v4;
        l->e.motion_x += v2;
        l->e.motion_z += v4;
        l->is_air_borne = 1;
    }
}

void ghast_collide_with_nearby_entities(struct living *l, const struct aabb *box)
{
    struct an_world *an = l->an;
    struct gh_world *gw = (struct gh_world *)an->user_data;

    /* World.getEntitiesWithinAABBExcludingEntity's order: the chunks from
     * (minX - 2)/16 to (maxX + 2)/16 in cx-major, cz order, then the clamped y
     * sections, each in insertion order. Only living entities and the player
     * canBePushed; the items and fireballs the query reaches answer false. The
     * player spawned before everything else, so inside its chunk and section it
     * comes first, and the sums must take it there (double adds are not
     * associative). */
    int cx0 = mh_floor((box->min_x - 2.0) / 16.0);
    int cx1 = mh_floor((box->max_x + 2.0) / 16.0);
    int cz0 = mh_floor((box->min_z - 2.0) / 16.0);
    int cz1 = mh_floor((box->max_z + 2.0) / 16.0);
    int y0 = mh_floor((box->min_y - 2.0) / 16.0);
    int y1 = mh_floor((box->max_y + 2.0) / 16.0);

    if (y0 < 0) y0 = 0;
    if (y0 > 15) y0 = 15;
    if (y1 < 0) y1 = 0;
    if (y1 > 15) y1 = 15;

    int pcx = 0, pcz = 0, psec = 0;

    if (an->has_player)
    {
        struct gh_player *p = &gw->player;
        pcx = mh_floor(p->pos_x / 16.0);
        pcz = mh_floor(p->pos_z / 16.0);
        psec = mh_floor(p->pos_y / 16.0);

        if (psec < 0) psec = 0;
        if (psec > 15) psec = 15;
    }

    for (int cx = cx0; cx <= cx1; ++cx)
    {
        for (int cz = cz0; cz <= cz1; ++cz)
        {
            if (!world_chunk_loaded(an->w, cx, cz)) continue;

            struct an_chunk *c = an_chunk_find(an, cx, cz);
            struct aabb pbb;

            int player_here = an->has_player && cx == pcx && cz == pcz;

            if (player_here) ghast_player_bb(&an->iew, &pbb);

            for (int y = y0; y <= y1; ++y)
            {
                if (player_here && y == psec && an->has_player)
                {
                    if (aabb_intersects(&pbb, box)) gh_player_apply_entity_collision(gw, l);
                }

                if (!c) continue;

                for (int i = 0; i < c->sec[y].n; ++i)
                {
                    struct an_ent *e = an_ent_at(sec_items(&c->sec[y])[i]);

                    if (!e->used) continue;
                    if (!e->is_living) continue;
                    if (lv_get(e->livh) == l) continue;
                    if (!living_can_be_pushed(lv_get(e->livh))) continue;

                    if (!aabb_intersects(&lv_get(e->livh)->e.bounding_box, box)) continue;

                    living_apply_entity_collision_pub(lv_get(e->livh), l);
                }
            }
        }
    }
}

/* ------------------------------------------------ the fireball scan's pieces */

/* the player's box for projectile.c's entity scan. */
int ghast_player_bb(struct ie_world *iew, struct aabb *out)
{
    struct an_world *an = (struct an_world *)iew->user_data;

    if (!an || !an->has_player) return 0;
    if (an->playerh != 0)
    {
        *out = lv_get(an->playerh)->e.bounding_box;
        return 1;
    }
    if (an->user_data == NULL) return 0;

    struct gh_player *p = &((struct gh_world *)an->user_data)->player;
    *out = aabb_make(p->pos_x - 0.3, p->pos_y, p->pos_z - 0.3,
                     p->pos_x + 0.3, p->pos_y + 1.8, p->pos_z + 0.3);
    return 1;
}

/* ------------------------------------------------ the fireball's NBT */

/* Entity.writeToNBT over the fireball's record plus EntityFireball's and
 * EntityLargeFireball's keys. */
void ghast_fireball_write_nbt(struct ie_ent *en, struct nbt *tag)
{
    struct entity *e = &en->e;
    nbt_put(tag, "Pos", nbt_double_list3(e->pos_x, e->pos_y + (double)e->y_size, e->pos_z));
    nbt_put(tag, "Motion", nbt_double_list3(e->motion_x, e->motion_y, e->motion_z));
    nbt_put(tag, "Rotation", nbt_float_list2(en->rotation_yaw, en->rotation_pitch));
    nbt_put(tag, "FallDistance", nbt_new_float(e->fall_distance));
    nbt_put(tag, "Fire", nbt_new_short(e->fire));
    nbt_put(tag, "Air", nbt_new_short(300));
    nbt_put(tag, "OnGround", nbt_new_byte(e->on_ground ? 1 : 0));
    nbt_put(tag, "Dimension", nbt_new_int(-1));
    nbt_put(tag, "Invulnerable", nbt_new_byte(0));
    nbt_put(tag, "PortalCooldown", nbt_new_int(0));
    nbt_put(tag, "UUIDMost", nbt_new_long(en->uuid_msb));
    nbt_put(tag, "UUIDLeast", nbt_new_long(en->uuid_lsb));

    /* EntityFireball.writeEntityToNBT */
    nbt_put(tag, "xTile", nbt_new_short(en->tile_x));
    nbt_put(tag, "yTile", nbt_new_short(en->tile_y));
    nbt_put(tag, "zTile", nbt_new_short(en->tile_z));
    nbt_put(tag, "inTile", nbt_new_byte(en->in_tile));
    nbt_put(tag, "inGround", nbt_new_byte(en->in_ground ? 1 : 0));
    nbt_put(tag, "direction", nbt_double_list3(e->motion_x, e->motion_y, e->motion_z));

    /* EntityLargeFireball.writeEntityToNBT */
    nbt_put(tag, "ExplosionPower", nbt_new_int(en->explosion_power));
}

uint64_t ghast_fireball_nbt_hash(struct ie_ent *en)
{
    struct nbt *tag = nbt_new_compound();
    ghast_fireball_write_nbt(en, tag);
    char *text = nbt_render(tag);
    uint64_t h = fnv_text(text);
    free(text);
    nbt_free(tag);
    return h;
}

void ghast_fireball_nbt_text(struct ie_ent *en, char **out)
{
    struct nbt *tag = nbt_new_compound();
    ghast_fireball_write_nbt(en, tag);
    *out = nbt_render(tag);
    nbt_free(tag);
}

/* ------------------------------------------------------ the spawn path */

struct living *gh_spawn_ghast(struct an_world *an, int spawn_index, double x, double y, double z,
                              float yaw, float pitch)
{
    struct living *l = living_alloc();
    living_init(l, an->w, GK_GHAST, an->det);
    l->an = an;
    l->dimension = -1;   /* the probe world is the Nether */
    ghast_construct(l, an->det);
    living_set_location_and_angles(l, x, y, z, yaw, pitch);
    living_on_spawn_with_egg(l, an->det);

    struct an_ent *en = an_ent_alloc();
    en->used = 1;
    en->is_living = 1;
    en->spawn_index = spawn_index;
    en->livh = lv_ref(l);
    en->ieh = 0;
    an_list_push(an, en);
    an_chunk_add(an, en, mh_floor(l->e.pos_x / 16.0), mh_floor(l->e.pos_y / 16.0),
                 mh_floor(l->e.pos_z / 16.0));
    return l;
}

/* ------------------------------------------------------- the tick loop */

/* World.updateEntities' pass over the probe's own list. */
static int gh_chunks_exist(struct world *w, double px, double pz)
{
    int x = mh_floor(px);
    int z = mh_floor(pz);
    int cx0 = (x - 32) >> 4, cx1 = (x + 32) >> 4;
    int cz0 = (z - 32) >> 4, cz1 = (z + 32) >> 4;

    for (int cx = cx0; cx <= cx1; ++cx)
    {
        for (int cz = cz0; cz <= cz1; ++cz)
        {
            if (!world_chunk_loaded(w, cx, cz)) return 0;
        }
    }

    return 1;
}

void gh_tick(struct an_world *an, int tick, struct gh_removal *out, int max_out, int *n_out)
{
    if (n_out) *n_out = 0;

    for (int i = 0; i < an->n; ++i)
    {
        struct an_ent *en = an_ent_at(an->slot[i]);
        int dead = 0, ie_released = 0;
        ie_removal ir;
        struct gh_removal r;
        memset(&r, 0, sizeof r);
        r.tick = tick;
        r.si = en->spawn_index;
        /* the oracle's kind codes: 0 ghast, 2 EntityLargeFireball, 3 item, 4 orb */
        if (en->is_living) r.kind = 0;
        else if (ie_get(en->ieh)->kind == IE_LARGE_FIREBALL) r.kind = 2;
        else if (ie_get(en->ieh)->kind == IE_ITEM) r.kind = 3;
        else if (ie_get(en->ieh)->kind == IE_ORB) r.kind = 4;
        else r.kind = 2;
        r.health = -1.0F;

        if (en->is_living)
        {
            struct living *l = lv_get(en->livh);

            if (l->is_dead) continue;

            r.entity_id = l->entity_id;

            if (!gh_chunks_exist(an->w, l->e.pos_x, l->e.pos_z)) continue;

            living_update_entity(l, an->det);
            an_chunk_membership(an, en);
            dead = l->is_dead;
            r.health = l->health;
            r.fire = l->e.fire;
        }
        else
        {
            ie_ent *ie = ie_get(en->ieh);

            r.entity_id = ie->entity_id;
            r.health = -1.0F;

            if (!ie->is_dead && !gh_chunks_exist(an->w, ie->e.pos_x, ie->e.pos_z)) continue;

            int n_irem = 0;
            if (ie_tick_one(&an->iew, ie, tick, &ir, 1, &n_irem))
            {
                dead = 1;
                ie_released = 1;
                r.fire = ir.fire;
            }
        }

        if (!dead) continue;

        if (out && *n_out < max_out) out[(*n_out)++] = r;

        /* an item the tick released leaves by the membership it had then */
        if (ie_released) an_remove_released(an, en, &ir);
        else an_remove(an, en);
        en->used = 0;
        memmove(&an->slot[i], &an->slot[i + 1], (size_t)(an->n - i - 1) * sizeof an->slot[0]);
        --an->n;
        --i;
    }


}
