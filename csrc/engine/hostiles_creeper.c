/* EntityCreeper (oracle/src/entity/monster/EntityCreeper.java) and
 * EntityAICreeperSwell (oracle/src/entity/ai/EntityAICreeperSwell.java), on top
 * of the hostile scaffold hostiles.c carries.
 *
 * The creeper is an EntityMob with no daylight burning, a fuse and a blast. Its
 * tick is EntityCreeper.onUpdate's head (creeper_pre_on_update: lastActiveTime,
 * the state byte, timeSinceIgnited and func_146077_cc), then EntityMob's
 * onLivingUpdate (creeper_on_living_update), the base living tick.
 *
 * The swell is a task whose shouldExecute watches the attack target's distance,
 * whose updateTask sets the state byte (1 within 7 blocks and in sight, -1
 * otherwise) and whose continueExecuting is EntityAIBase's default, which is
 * shouldExecute itself: the task drops out the moment the state clears with no
 * target in reach, and it holds mutex 1 against the wander and the ocelot
 * avoidance while it runs. creeperState then drives the fuse: timeSinceIgnited
 * += state each tick, and at fuseTime the creeper calls func_146077_cc:
 * World.createExplosion with mobGriefing as its second argument (which that
 * method passes as newExplosion's isSmoking; its isFlaming is the hardcoded
 * false) and a radius of 3, or 6 when the lightning made it powered, then
 * setDead.
 *
 * The blast is explosion.c's run over the arena's world and entity pool; this
 * file adds only what the arena brings: the blast reaches the arena's livings
 * (World.getEntitiesWithinAABBExcludingEntity walks one chunk-section list of
 * items and livings together) and the drops it spawns have to join the arena's
 * tick list. */
#include "nbtw.h"
#include "hostiles_creeper.h"
#include "env.h"

#include <stdlib.h>
#include <string.h>

#include "ai.h"
#include "blockcb.h"
#include "blocks.h"
#include "explosion.h"
#include "fire.h"
#include "hostiles.h"
#include "jmath.h"
#include "world.h"

/* Blocks.fire. */
#define ID_FIRE 51
#define ITEM_GUNPOWDER 289

static int material_is_air(int id)
{
    const char *name = MATERIALS[BLOCKS[id & 4095].material].name;

    return name != NULL && strcmp(name, "air") == 0;
}

/* ------------------------------------------------------------ the entity */

void creeper_construct(struct living *l, det_state *det)
{
    /* EntityMob's constructor: the XP a player kill drops */
    l->experience_value = 5;
    /* EntityCreeper.applyEntityAttributes: EntityMob's registration of
     * attackDamage (the SharedMonsterAttributes default of 2) and the
     * creeper's 0.25 movement speed. EntityLiving's registration put
     * followRange at 16 (attrs_instance_init's default is 32). */
    attrs_set_base(&l->attrs.a[ATTR_ATTACK_DAMAGE], 2.0);
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.25);
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);

    entity_set_size(&l->e, 0.6f, 1.8f);

    /* EntityCreeper.entityInit's three dataWatcher bytes (16 state, 17
     * powered, 18 ignited) and the field defaults. */
    l->creeper_last_active_time = 0;
    l->creeper_time_since_ignited = 0;
    l->creeper_fuse_time = 30;
    l->creeper_explosion_radius = 3;
    l->creeper_state = -1;
    l->creeper_powered = 0;
    l->creeper_ignited = 0;

    /* EntityCreeper.fall */
    l->kind_fall = creeper_fall;

    /* The creeper's navigator keeps PathNavigate's defaults: canPassOpenWoodenDoors
     * true (ai_setup_kind), canPassClosedWoodenDoors false, avoidsWater false.
     * EntityAISwimming's constructor is what sets canSwim. */
    ai_setup_kind(l, det);
}

void creeper_fall(struct living *l, float distance)
{
    /* EntityCreeper.fall: super.fall (the fall damage) first */
    living_fall(l, distance);

    l->creeper_time_since_ignited = (int)((float)l->creeper_time_since_ignited + distance * 1.5F);

    if (l->creeper_time_since_ignited > l->creeper_fuse_time - 5)
    {
        l->creeper_time_since_ignited = l->creeper_fuse_time - 5;
    }
}

/* EntityCreeper.setCreeperState. */
static void creeper_set_state(struct living *l, int state)
{
    l->creeper_state = state;
}

int creeper_max_safe_point_tries(struct living *l)
{
    return lv_get(l->attack_target) == NULL ? 3 : 3 + (int)(l->health - 1.0F);
}

/* EntityCreeper.onUpdate's head, run before EntityLivingBase's body. */
void creeper_pre_on_update(struct living *l, det_state *det)
{
    if (!living_is_alive(l)) return;

    l->creeper_last_active_time = l->creeper_time_since_ignited;

    if (l->creeper_ignited != 0) creeper_set_state(l, 1);

    int var1 = l->creeper_state;

    /* playSound("creeper.primed", 1.0F, 0.5F): World.playSoundAtEntity is empty
     * on the server and the pitch here is the constant 0.5F */
    (void)var1;

    l->creeper_time_since_ignited += var1;

    if (l->creeper_time_since_ignited < 0) l->creeper_time_since_ignited = 0;

    if (l->creeper_time_since_ignited >= l->creeper_fuse_time)
    {
        l->creeper_time_since_ignited = l->creeper_fuse_time;
        creeper_explode(l, det);
    }
}

/* EntityMob.onLivingUpdate: the arm swing progress (a creeper never swings:
 * its held item is always null), then the brightness that ages it, then
 * EntityLiving.onLivingUpdate and the loot pass. No daylight burning: that is
 * EntityZombie's own override. */
void creeper_on_living_update(struct living *l, det_state *det)
{
    int x = mh_floor(l->e.pos_x);
    int z = mh_floor(l->e.pos_z);
    double v4 = (l->e.bounding_box.max_y - l->e.bounding_box.min_y) * 0.66;
    int y = mh_floor(l->e.pos_y - (double)l->e.y_offset + v4);
    float br = living_light_brightness(l->world, l->an ? l->an->skylight : 0, x, y, z);

    if (br > 0.5f) l->entity_age += 2;

    living_default_on_living_update(l, det);
    hostile_loot_update(l);
}

void creeper_attack_entity_as_mob(struct living *l, struct living *target, det_state *det)
{
    /* EntityCreeper.attackEntityAsMob returns true without touching the target:
     * the blast is the creeper's damage */
    (void)l;
    (void)target;
    (void)det;
}

void creeper_drop_few_items(struct living *l, int hit_by_player, int looting, det_state *det)
{
    /* EntityLivingBase.dropFewItems with EntityCreeper.func_146068_u's
     * Items.gunpowder */
    (void)hit_by_player;
    (void)det;

    int count = det_rng_int_n(&l->rand, 3);

    if (looting > 0) count += det_rng_int_n(&l->rand, looting + 1);

    for (int i = 0; i < count; ++i)
    {
        an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y, l->e.pos_z, ITEM_GUNPOWDER, 0, 1, 10);
    }
}

void creeper_write_kind_nbt(struct living *l, struct nbtw *w)
{
    if (l->creeper_powered == 1) nbtw_byte(w, "powered", 1);

    nbtw_short(w, "Fuse", (short)l->creeper_fuse_time);
    nbtw_byte(w, "ExplosionRadius", (int8_t)l->creeper_explosion_radius);
    nbtw_byte(w, "ignited", l->creeper_ignited != 0 ? 1 : 0);
}

/* ------------------------------------------------------ the charged creeper */

/* One drop the block callbacks spawned into the item pool joins the arena's
 * tick list, the way World.spawnEntityInWorld appends to loadedEntityList. The
 * probe's spawn index is the list position the entity takes.
 *
 * A drop the blast's own entity pass spawned (a dying mob's dropFewItems goes
 * through an_spawn_item, which wraps it already) is in the list: the probe's
 * absorption skips an entity its spawn map already holds, so this does too. */
static void creeper_absorb_items(struct an_world *an, int before)
{
    while (before < an->iew.n)
    {
        ie_ent *ie = ie_ent_at(an->iew.slot[before++]);
        int wrapped = 0;

        for (int i = 0; i < an->n; ++i)
        {
            if (!an_ent_at(an->slot[i])->is_living && ie_get(an_ent_at(an->slot[i])->ieh) == ie)
            {
                wrapped = 1;
                break;
            }
        }

        if (wrapped) continue;

        ie->spawn_index = an->n;
        ie->dimension = an->dimension;

        struct an_ent *en = an_ent_alloc();
        en->used = 1;
        en->is_living = 0;
        en->spawn_index = an->n;
        en->livh = 0;
        en->ieh = ie_ref(ie);
        an_list_push(an, en);
        an_chunk_add(an, en, mh_floor(ie->e.pos_x / 16.0), mh_floor(ie->e.pos_y / 16.0),
                     mh_floor(ie->e.pos_z / 16.0));
    }
}

/* EntityLightningBolt(World, x, y, z), the constructor the probe builds before
 * onStruckByLightning. The bolt is never spawned; what reaches the world is the
 * fire it places. Its Det draws (id, Random, UUID) are real: Det's entity id
 * counter and seeder stream advance. */
static void creeper_lightning_bolt(struct living *l, det_state *det, double x, double y, double z)
{
    struct world *w = l->world;
    struct an_world *an = l->an;

    (void)det_next_entity_id_role(det, DET_OTHER);
    det_rng bolt_rand = det_new_random_role(det, DET_OTHER);
    int64_t msb, lsb;
    det_uuid_role(det, DET_OTHER, &msb, &lsb);
    (void)msb;
    (void)lsb;

    /* boltVertex, then boltLivingTime */
    (void)det_rng_long(&bolt_rand);
    (void)det_rng_int_n(&bolt_rand, 3);

    /* doFireTick is the default rule; the difficulty is pinned NORMAL and the
     * arena's chunks all exist, so the fire placement runs. */
    if (an != NULL)
    {
        struct blockcb_env saved = nw_env->blockcb.env;
        nw_env->blockcb.env.world_rand = &an->iew.world_rand.r;
        nw_env->blockcb.env.det = an->det;
        nw_env->blockcb.env.item_drop = expl_env_item_drop;
        nw_env->blockcb.env.ctx = &an->iew;
        nw_env->blockcb.env.item_spill = NULL;

        int before = an->iew.n;

        int bx = mh_floor(x), by = mh_floor(y), bz = mh_floor(z);

        if (material_is_air(world_get_block(w, bx, by, bz)) && fire_can_place(w, bx, by, bz))
        {
            world_set_block(w, bx, by, bz, ID_FIRE, 0, 3);
        }

        for (int i = 0; i < 4; ++i)
        {
            int fx = bx + det_rng_int_n(&bolt_rand, 3) - 1;
            int fy = by + det_rng_int_n(&bolt_rand, 3) - 1;
            int fz = bz + det_rng_int_n(&bolt_rand, 3) - 1;

            if (material_is_air(world_get_block(w, fx, fy, fz)) && fire_can_place(w, fx, fy, fz))
            {
                world_set_block(w, fx, fy, fz, ID_FIRE, 0, 3);
            }
        }

        creeper_absorb_items(an, before);
        nw_env->blockcb.env = saved;
    }
}

void creeper_struck_by_lightning(struct living *l, det_state *det, double x, double y, double z)
{
    creeper_lightning_bolt(l, det, x, y, z);

    /* Entity.onStruckByLightning: dealFireDamage(5) then ++fire, with setFire(8)
     * only when the increment lands on zero. */
    if (!l->immune_to_fire) living_attack_entity_from(l, DMG_IN_FIRE, 5.0F, det);

    ++l->e.fire;

    if (l->e.fire == 0) living_set_fire(l, 8);

    /* EntityCreeper's own tail */
    l->creeper_powered = 1;
}

/* ------------------------------------------------------------- the blast */

void creeper_explode(struct living *l, det_state *det)
{
    /* World.getGameRules().getGameRuleBooleanValue("mobGriefing") is
     * World.createExplosion's second argument, which that method passes as
     * newExplosion's isSmoking; its isFlaming is the hardcoded false a creeper
     * blast always gets (EntityLargeFireball is the caller that passes true). */
    int mob_griefing = 1;
    float size = l->creeper_powered ? (float)(l->creeper_explosion_radius * 2)
                                    : (float)l->creeper_explosion_radius;

    if (l->an != NULL)
    {
        int before = l->an->iew.n;

        expl_run(l->world, det, det_role(det), &l->an->iew.world_rand.r, &l->an->iew, l,
                 l->e.pos_x, l->e.pos_y, l->e.pos_z, size, 0, mob_griefing, NULL);

        creeper_absorb_items(l->an, before);
    }

    living_set_dead(l);
}

/* ------------------------------------------------------- the swell task */

/* EntityAICreeperSwell.shouldExecute. */
int creeper_swell_should_execute(struct living *l, struct ai_task *t)
{
    struct living *var1 = lv_get(l->attack_target);

    (void)t;

    if (l->creeper_state > 0) return 1;
    if (var1 == NULL) return 0;

    double dx = l->e.pos_x - var1->e.pos_x;
    double dy = l->e.pos_y - var1->e.pos_y;
    double dz = l->e.pos_z - var1->e.pos_z;

    return dx * dx + dy * dy + dz * dz < 9.0;
}

/* EntityAICreeperSwell.updateTask. */
void creeper_swell_update(struct living *l, struct ai_task *t)
{
    struct living *target = lv_get(t->target);

    if (target == NULL)
    {
        creeper_set_state(l, -1);
        return;
    }

    double dx = l->e.pos_x - target->e.pos_x;
    double dy = l->e.pos_y - target->e.pos_y;
    double dz = l->e.pos_z - target->e.pos_z;

    int too_far = dx * dx + dy * dy + dz * dz > 49.0;

    if (too_far)
    {
        creeper_set_state(l, -1);
    }
    else if (!senses_can_see(l, target))
    {
        creeper_set_state(l, -1);
    }
    else
    {
        creeper_set_state(l, 1);
    }
}
