/* Leads (lane/ride). See leash.h. */

#include "nbtw.h"
#include "leash.h"
#include "env.h"

#include <math.h>
#include <string.h>

#include "ai.h"
#include "animals.h"
#include "blocks.h"
#include "entity_nbt.h"
#include "fallhang.h"
#include "living.h"
#include "player.h"
#include "raytrace.h"
#include "serverreplay.h"
#include "survival.h"
#include "world.h"

enum { ITEM_LEAD = 420 };

#define leash_sr (nw_env->leash.sr)

void leash_bind(struct serverreplay *sr)
{
    leash_sr = sr;
}

/* the player twin the living world holds (the EntityPlayerMP the leash
 * names) */
static struct living *leash_player(void)
{
    return leash_sr != NULL ? lv_get(leash_sr->player_livh) : NULL;
}

/* EntityLiving.setLeashedToEntity; the S1B goes to the watchers' copies */
static void leash_set(struct living *l, int holder, struct fh_ent *knot)
{
    l->is_leashed = 1;
    l->leash_holder = holder;
    l->leash_knot = fh_ref(knot);
    /* the knot's block, kept past the knot's release (the NBT of an animal
     * whose knot left the world this tick still names it) */
    if (knot != NULL)
    {
        l->leash_x = knot->tile_x;
        l->leash_y = knot->tile_y;
        l->leash_z = knot->tile_z;
    }
    ++l->leash_sends;
}

/* EntityLiving.clearLeashed(send, drop): the S1B with no holder when SEND,
 * and the lead drops as entityDropItem(new ItemStack(Items.lead, 1), 0.0F) */
static void leash_clear(struct living *l, int send, int drop)
{
    if (!l->is_leashed) return;
    l->is_leashed = 0;
    l->leash_holder = LEASH_NONE;
    l->leash_knot = 0;
    if (send) ++l->leash_sends;
    if (drop && l->an != NULL)
        an_spawn_item(l->an, l->an->n, l->e.pos_x, l->e.pos_y + (double)0.0F, l->e.pos_z, ITEM_LEAD, 0, 1, 10);
}

/* EntityLiving.allowLeashing: not leashed and not an IMob; EntityVillager's
 * and EntityAmbientCreature's (the bat's) overrides answer false */
static int allow_leashing(const struct living *l)
{
    if (l->kind == VK_VILLAGER || l->kind == AK_BAT) return 0;
    return !l->is_leashed && !surv_is_imob(l->kind);
}

int leash_interact_first(struct surv_stack *held, struct living *l, int server)
{
    if (l == NULL) return 0;

    /* leashed to this player: the lead comes off (and drops, not creative) */
    if (l->is_leashed && l->leash_holder == LEASH_PLAYER)
    {
        if (server) leash_clear(l, 1, 1);
        return 1;
    }

    /* the tamed branch needs a wolf or an ocelot, both switched off */
    if (held != NULL && held->count > 0 && held->item == ITEM_LEAD && allow_leashing(l))
    {
        if (server) leash_set(l, LEASH_PLAYER, NULL);
        --held->count;
        return 1;
    }
    return 0;
}

/* World.getEntitiesWithinAABB(EntityLiving.class, box): the living world's
 * chunk walk, the player twin left out (an EntityPlayer is not an
 * EntityLiving) */
static int livings_in(const struct aabb *box, struct living **out, int cap)
{
    struct serverreplay *sr = leash_sr;
    AN_QUERY_LIST(found);
    int n = an_entities_excluding(&sr->d->anw, an_deref(sr->player_enth), box, found, AN_MAX_ENTITIES);
    int k = 0;
    for (int i = 0; i < n && k < cap; ++i)
        if (found[i]->is_living && lv_get(found[i]->livh) != lv_get(sr->player_livh)) out[k++] = lv_get(found[i]->livh);
    return k;
}

/* EntityLeashKnot.getKnotForBlock: the knots in the box one block around,
 * the first on this tile */
static struct fh_ent *knot_for_block(int x, int y, int z)
{
    struct serverreplay *sr = leash_sr;
    struct aabb box = aabb_make((double)x - 1.0, (double)y - 1.0, (double)z - 1.0,
                                (double)x + 1.0, (double)y + 1.0, (double)z + 1.0);
    FH_QUERY_LIST(out);
    int n = fh_entities_in_box(&sr->d->fhw, box, out, FH_MAX_ENTITIES);
    for (int i = 0; i < n; ++i)
        if (out[i]->kind == FH_KNOT && out[i]->tile_x == x && out[i]->tile_y == y && out[i]->tile_z == z)
            return out[i];
    return NULL;
}

/* EntityLeashKnot.func_110129_a: the knot, forceSpawn, spawnEntityInWorld
 * (the pass order's tail and the tracker) */
static struct fh_ent *spawn_knot(int x, int y, int z)
{
    struct serverreplay *sr = leash_sr;
    sr->d->fhw.role = DET_SERVER;
    fh_ent *knot = fh_spawn_knot(&sr->d->fhw, x, y, z);
    if (knot == NULL) return NULL;
    fh_added_to_world(&sr->d->fhw, knot);
    sr_order_push(sr, 1, knot);
    return knot;
}

int leash_tie_to_fence(struct server_player *p, int x, int y, int z)
{
    (void)p;
    if (leash_sr == NULL) return 0;

    struct fh_ent *knot = knot_for_block(x, y, z);
    int tied = 0;
    double r = 7.0;
    struct aabb box = aabb_make((double)x - r, (double)y - r, (double)z - r,
                                (double)x + r, (double)y + r, (double)z + r);
    struct living *ls[256];
    int n = livings_in(&box, ls, 256);

    for (int i = 0; i < n; ++i)
    {
        if (!ls[i]->is_leashed || ls[i]->leash_holder != LEASH_PLAYER) continue;
        if (knot == NULL) knot = spawn_knot(x, y, z);
        leash_set(ls[i], LEASH_KNOT, knot);
        tied = 1;
    }
    return tied;
}

int leash_knot_interact(struct server_player *p, int entity_id)
{
    struct serverreplay *sr = leash_sr;
    if (sr == NULL) return 0;

    struct fh_ent *knot = NULL;
    for (int i = 0; i < sr->d->fhw.n; ++i)
        if (fh_ent_at(sr->d->fhw.slot[i])->kind == FH_KNOT && fh_ent_at(sr->d->fhw.slot[i])->entity_id == entity_id) knot = fh_ent_at(sr->d->fhw.slot[i]);
    if (knot == NULL) return 0;

    /* processUseEntity's reach: 6 blocks, 3 when canEntityBeSeen's ray
     * meets a block */
    struct rt_mop mop;
    int blocked = raytrace_blocks(p->e.world, p->e.pos_x, p->e.pos_y + 1.62F, p->e.pos_z,
                                  knot->e.pos_x, knot->e.pos_y, knot->e.pos_z, 0, 0, 0, &mop);
    double dx = p->e.pos_x - knot->e.pos_x, dy = p->e.pos_y - knot->e.pos_y, dz = p->e.pos_z - knot->e.pos_z;
    if (dx * dx + dy * dy + dz * dz >= (blocked ? 9.0 : 36.0)) return 1;

    struct surv_stack *held = &p->sv.inv[p->sv.current_item];
    int tied = 0;

    if (held->count > 0 && held->item == ITEM_LEAD)
    {
        double r = 7.0;
        struct aabb box = aabb_make(knot->e.pos_x - r, knot->e.pos_y - r, knot->e.pos_z - r,
                                    knot->e.pos_x + r, knot->e.pos_y + r, knot->e.pos_z + r);
        struct living *ls[256];
        int n = livings_in(&box, ls, 256);
        for (int i = 0; i < n; ++i)
        {
            if (!ls[i]->is_leashed || ls[i]->leash_holder != LEASH_PLAYER) continue;
            leash_set(ls[i], LEASH_KNOT, knot);
            tied = 1;
        }
    }

    /* the survival player unties: the knot dies, its mobs let go on their
     * next updateLeashedState */
    if (!tied) knot->is_dead = 1;

    /* interactWith: interactFirst answered true; the held stack is untouched */
    return 1;
}

/* Entity.getDistanceToEntity: float differences, MathHelper.sqrt_float */
static float distance_to(const struct living *l, double x, double y, double z)
{
    float a = (float)(l->e.pos_x - x);
    float b = (float)(l->e.pos_y - y);
    float c = (float)(l->e.pos_z - z);
    return (float)sqrt((double)(a * a + b * b + c * c));
}

/* EntityCreature: every leashable kind here but the bat, the slimes and the
 * ghast (EntityLiving directly) */
static int is_creature(int kind)
{
    return kind != AK_BAT && !IS_SLIME_KIND(kind) && kind != GK_GHAST;
}

/* EntityLiving.recreateLeash */
static void recreate_leash(struct living *l)
{
    if (l->is_leashed && l->leash_pending)
    {
        if (l->leash_pending == 1)
        {
            /* the EntityLivingBase in the box grown by 10 whose UUID matches:
             * the player (a leash only ever names one) */
            struct living *pl = leash_player();
            struct server_player *sp = leash_sr != NULL ? leash_sr->player : NULL;
            if (pl != NULL && sp != NULL)
            {
                int64_t msb = 0, lsb = 0;
                ent_nbt_player_uuid(sp, &msb, &lsb);
                struct aabb box = aabb_expand(l->e.bounding_box, 10.0, 10.0, 10.0);
                if (msb == l->leash_msb && lsb == l->leash_lsb && aabb_intersects(&box, &pl->e.bounding_box))
                    l->leash_holder = LEASH_PLAYER;
            }
        }
        else if (l->leash_pending == 2)
        {
            struct fh_ent *knot = knot_for_block(l->leash_x, l->leash_y, l->leash_z);
            if (knot == NULL) knot = spawn_knot(l->leash_x, l->leash_y, l->leash_z);
            l->leash_holder = knot != NULL ? LEASH_KNOT : LEASH_NONE;
            l->leash_knot = fh_ref(knot);
        }
        else
        {
            leash_clear(l, 0, 1);
        }
    }
    l->leash_pending = 0;
}

void leash_update(struct living *l)
{
    if (leash_sr == NULL) return;
    if (!l->is_leashed && !l->leash_pending && !l->leash_ai) return;

    /* EntityLiving.updateLeashedState */
    if (l->leash_pending) recreate_leash(l);

    if (l->is_leashed)
    {
        int gone = l->leash_holder == LEASH_NONE ||
                   (l->leash_holder == LEASH_KNOT && (l->leash_knot == 0 || fh_ref_released(l->leash_knot) || fh_get(l->leash_knot)->is_dead)) ||
                   (l->leash_holder == LEASH_PLAYER && (leash_player() == NULL || leash_player()->is_dead));
        if (gone) leash_clear(l, 1, 1);
    }

    if (!is_creature(l->kind)) return;

    /* EntityCreature.updateLeashedState: the holder in this world */
    if (l->is_leashed && l->leash_holder != LEASH_NONE)
    {
        double hx, hy, hz, hmin_y;
        int hid;

        if (l->leash_holder == LEASH_PLAYER)
        {
            struct living *pl = leash_player();
            hx = pl->e.pos_x; hy = pl->e.pos_y; hz = pl->e.pos_z;
            hmin_y = pl->e.bounding_box.min_y;
            hid = pl->entity_id;
            if (leash_sr->player == NULL || serverreplay_player_dim(leash_sr) != leash_sr->here) return;
        }
        else
        {
            /* a knot of another world (the leashed mob went through a
             * portal: the rest of the old instance's update) */
            if (fh_get(l->leash_knot)->e.world != l->e.world) return;
            hx = fh_get(l->leash_knot)->e.pos_x; hy = fh_get(l->leash_knot)->e.pos_y; hz = fh_get(l->leash_knot)->e.pos_z;
            hmin_y = fh_get(l->leash_knot)->e.bounding_box.min_y;
            hid = fh_get(l->leash_knot)->entity_id;
        }

        /* setHomeArea((int)posX, (int)posY, (int)posZ, 5) */
        l->home_x = (int)hx;
        l->home_y = (int)hy;
        l->home_z = (int)hz;
        l->maximum_home_distance = 5.0F;
        float var2 = distance_to(l, hx, hy, hz);

        if (!l->leash_ai)
        {
            ai_add_leash_restriction(l);
            l->nav.avoids_water = 0;
            l->leash_ai = 1;
        }

        /* func_142017_o: empty for every kind here */

        if (var2 > 4.0F) nav_try_move_to_point(l, hid, hx, hmin_y, hz, 1.0);

        if (var2 > 6.0F)
        {
            double var3 = (hx - l->e.pos_x) / (double)var2;
            double var5 = (hy - l->e.pos_y) / (double)var2;
            double var7 = (hz - l->e.pos_z) / (double)var2;
            l->e.motion_x += var3 * fabs(var3) * 0.4;
            l->e.motion_y += var5 * fabs(var5) * 0.4;
            l->e.motion_z += var7 * fabs(var7) * 0.4;
        }

        if (var2 > 10.0F) leash_clear(l, 1, 1);
    }
    else if (!l->is_leashed && l->leash_ai)
    {
        l->leash_ai = 0;
        ai_remove_task(l, AIC_MOVE_TOWARDS_RESTRICTION);
        l->nav.avoids_water = 1;
        /* detachHome */
        l->maximum_home_distance = -1.0F;
    }
}

void leash_write_nbt(const struct living *l, struct nbtw *w)
{
    if (!l->is_leashed || l->leash_holder == LEASH_NONE) return;

    nbtw_comp(w, "Leash");
    if (l->leash_holder == LEASH_PLAYER && leash_sr != NULL && leash_sr->player != NULL)
    {
        int64_t msb = 0, lsb = 0;
        ent_nbt_player_uuid(leash_sr->player, &msb, &lsb);
        nbtw_long(w, "UUIDMost", msb);
        nbtw_long(w, "UUIDLeast", lsb);
    }
    else if (l->leash_holder == LEASH_KNOT && l->leash_knot != 0)
    {
        nbtw_int(w, "X", l->leash_x);
        nbtw_int(w, "Y", l->leash_y);
        nbtw_int(w, "Z", l->leash_z);
    }
    nbtw_end(w);
}

void leash_read_nbt(struct living *l, const nbt *tag)
{
    const nbt *leashed = nbt_get(tag, "Leashed");
    l->is_leashed = leashed != NULL && nbt_int_value(leashed) != 0;
    l->leash_holder = LEASH_NONE;
    l->leash_knot = 0;
    l->leash_pending = 0;

    const nbt *leash = nbt_get(tag, "Leash");
    if (!l->is_leashed || leash == NULL || nbt_kind(leash) != NBT_COMPOUND) return;

    const nbt *um = nbt_get(leash, "UUIDMost"), *ul = nbt_get(leash, "UUIDLeast");
    const nbt *x = nbt_get(leash, "X"), *y = nbt_get(leash, "Y"), *z = nbt_get(leash, "Z");
    if (um != NULL && ul != NULL && nbt_kind(um) == NBT_LONG && nbt_kind(ul) == NBT_LONG)
    {
        l->leash_pending = 1;
        l->leash_msb = (int64_t)nbt_int_value(um);
        l->leash_lsb = (int64_t)nbt_int_value(ul);
    }
    else if (x != NULL && y != NULL && z != NULL)
    {
        l->leash_pending = 2;
        l->leash_x = (int)nbt_int_value(x);
        l->leash_y = (int)nbt_int_value(y);
        l->leash_z = (int)nbt_int_value(z);
    }
    else
    {
        l->leash_pending = 3;
    }
}
