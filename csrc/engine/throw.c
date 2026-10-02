/* See throw.h. */
#include "throw.h"
#include "env.h"
#include "riding.h"

#include <string.h>

#include "det.h"
#include "items.h"
#include "itemuse.h"
#include "living.h"
#include "player.h"
#include "projectile.h"
#include "raytrace.h"
#include "serverreplay.h"
#include "survival.h"

enum {
    IT_BOW = 261, IT_ARROW = 262, IT_SNOWBALL = 332, IT_EGG = 344,
    IT_ENDER_PEARL = 368, IT_ENDER_EYE = 381, IT_POTION = 373, IT_EXP_BOTTLE = 384,
    ENCH_UNBREAKING = 34, ENCH_INFINITY = 51,
    BOW_USE_DURATION = 72000,
    END_PORTAL_FRAME = 120,
};

/* The replay whose living world the pearl and eye hooks reach. */
#define throw_sr (nw_env->throw.sr)

/* Item.itemRand, the static per-role stream every item's sound pitch draws. */
static void item_rand_float(det_state *det, int role)
{
    const char *name = "./net/minecraft/item/Item.java:itemRand";
    det_split *s = det_split_find(det, name);
    if (!s) s = det_split_random(det, name);
    (void)det_split_float_role(det, s, role);
}

static void empty_slot(struct surv_state *sv, struct surv_stack *st)
{
    st->item = 0;
    st->damage = 0;
    st->count = 0;
    st->tag = 0;
    st->gen = ++sv->gen_counter;
}

/* InventoryPlayer.hasItem over mainInventory. */
static int has_arrow(const struct surv_state *sv)
{
    for (int i = 0; i < 36; ++i)
        if (sv->inv[i].count > 0 && sv->inv[i].item == IT_ARROW) return 1;
    return 0;
}

/* InventoryPlayer.consumeInventoryItem: the first stack of the item. */
static void consume_arrow(struct surv_state *sv)
{
    for (int i = 0; i < 36; ++i)
    {
        struct surv_stack *st = &sv->inv[i];
        if (st->count <= 0 || st->item != IT_ARROW) continue;
        if (--st->count <= 0) empty_slot(sv, st);
        return;
    }
}

/* EntityPlayer.setItemInUse: a new stack only. */
static void set_item_in_use(struct surv_state *sv, int slot)
{
    struct surv_stack *cur = &sv->inv[slot];
    if (sv->using_slot == slot && sv->using_gen == cur->gen && sv->using_count > 0) return;
    sv->using_count = BOW_USE_DURATION;
    sv->using_slot = slot;
    sv->using_gen = cur->gen;
}

/* ItemBow.onPlayerStoppedUsing's charge; 0 when below the 0.1 floor. */
static int bow_charge(int count_left, float *out)
{
    int var6 = BOW_USE_DURATION - count_left;
    float var7 = (float)var6 / 20.0F;
    var7 = (var7 * var7 + var7 * 2.0F) / 3.0F;
    if ((double)var7 < 0.1) return 0;
    if (var7 > 1.0F) var7 = 1.0F;
    *out = var7;
    return 1;
}

/* ItemStack.damageItem(1, player) on the client: attemptDamageItem's
 * Unbreaking roll (EnchantmentDurability.negateDamage, not armor) draws the
 * client player's own Random, and the broken bow's renderBrokenItemStack
 * its own and Math.random (surv_client_render_broken). */
static void damage_bow_client(struct client_player *p, struct surv_stack *bow)
{
    struct surv_state *sv = &p->sv;
    int level = surv_ench_level(bow, ENCH_UNBREAKING);
    if (level > 0 && det_rng_int_n(&sv->erand, level + 1) > 0) return;
    if (++bow->damage > ITEMS[IT_BOW].max_damage)
    {
        surv_client_render_broken(p, bow);
        --bow->count;
        if (bow->count < 0) bow->count = 0;
        bow->damage = 0;
        if (bow->count == 0) empty_slot(sv, bow);
    }
}

/* ItemBow's Infinity: the release needs no arrow, the arrow is pickup 2
 * (creative only) and none is consumed. The draw still needs one
 * (onItemRightClick tests hasItem). */
static int bow_infinite(const struct surv_stack *bow)
{
    return surv_ench_level(bow, ENCH_INFINITY) > 0;
}

/* The slot's stack still the one setItemInUse took (EntityPlayer.onUpdate
 * clears the use when the held stack changes). */
static struct surv_stack *using_stack(struct surv_state *sv, int current)
{
    if (sv->using_slot < 0 || sv->using_slot != current) return NULL;
    struct surv_stack *cur = &sv->inv[sv->using_slot];
    if (cur->count <= 0 || cur->gen != sv->using_gen) return NULL;
    return cur;
}

/* ------------------------------------------------------------ the server */

static void spawn_throwable(struct server_player *p, int kind)
{
    struct serverreplay *sr = p->replay;
    if (!sr) return;
    ie_ent *en = proj_spawn_throwable_shooter(&sr->d->anw.iew, kind, lv_get(sr->player_livh), 1,
                                              p->e.pos_x, p->e.pos_y, p->e.pos_z, 1.62F,
                                              p->rotation_yaw, p->rotation_pitch);
    if (!en) return;
    en->shooter_life = p->life;
    /* EntityPotion(world, player, stack): the stack's damage is the potion */
    if (kind == IE_POTION)
    {
        en->potion_damage = p->sv.inv[p->sv.current_item].damage;
        en->potion_spent = 1 - p->sv.inv[p->sv.current_item].count;
    }
    en->owner_name = "Player";
    an_adopt_projectile(&sr->d->anw, en);
}

/* Item.getMovingObjectPositionFromPlayer(world, player, false) lands on an
 * end portal frame. */
static int looks_at_end_frame(struct server_player *p)
{
    struct iu_player ip = {p->e.pos_x, p->e.pos_y, p->e.pos_z, p->e.y_offset,
                           p->rotation_yaw, p->rotation_pitch, 1, 0};
    int x, y, z;
    if (!itemuse_look_block(p->e.world, &ip, &x, &y, &z)) return 0;
    return (world_get_block(p->e.world, x, y, z) & 4095) == END_PORTAL_FRAME;
}

int throw_server_use(struct server_player *p)
{
    struct surv_state *sv = &p->sv;
    struct surv_stack *cur = &sv->inv[sv->current_item];
    struct serverreplay *sr = p->replay;

    switch (cur->item)
    {
    case IT_BOW:
        if (has_arrow(sv)) set_item_in_use(sv, sv->current_item);
        return 1;

    case IT_POTION:
        /* ItemPotion.onItemRightClick's splash branch (the drink is
         * survival.c's) */
        if (!(cur->damage & 16384)) return 0;
        /* fall through */
    case IT_SNOWBALL:
    case IT_EGG:
    case IT_ENDER_PEARL:
    case IT_EXP_BOTTLE:
        --cur->count;
        item_rand_float(surv.det, DET_SERVER);
        spawn_throwable(p, cur->item == IT_SNOWBALL ? IE_SNOWBALL
                           : cur->item == IT_EGG ? IE_EGG
                           : cur->item == IT_POTION ? IE_POTION
                           : cur->item == IT_EXP_BOTTLE ? IE_EXP_BOTTLE : IE_ENDER_PEARL);
        if (cur->count <= 0) empty_slot(sv, cur);
        return 1;

    case IT_ENDER_EYE:
        if (looks_at_end_frame(p) || !sr) return 1;
        /* findClosestStructure("Stronghold") is null in the Nether and the
         * End (ChunkProviderHell's and ChunkProviderEnd's func_147416_a):
         * no eye, no sound, the stack stays */
        if (p->e.world != NULL && p->e.world->dim != 0) return 1;
        {
            /* findClosestStructure, EntityEnderEye, moveTowards, the spawn,
             * then the itemRand pitch (projectile.c keeps that order) */
            ie_ent *en = proj_throw_ender_eye(&sr->d->anw.iew, p->e.pos_x, p->e.pos_y, p->e.pos_z,
                                              p->e.y_offset);
            if (!en) return 1;
            an_adopt_projectile(&sr->d->anw, en);
            if (--cur->count <= 0) empty_slot(sv, cur);
        }
        return 1;
    }
    return 0;
}

void throw_server_release(struct server_player *p)
{
    struct surv_state *sv = &p->sv;
    /* EntityPlayer.stopUsingItem fires itemInUse: the bow, even when a C09
     * just before the release moved the held slot off it */
    struct surv_stack *bow = using_stack(sv, sv->using_slot);
    struct serverreplay *sr = p->replay;
    float var7;

    if (!bow || bow->item != IT_BOW) return;
    int infinite = bow_infinite(bow);
    if (!infinite && !has_arrow(sv)) return;
    if (!bow_charge(sv->using_count, &var7)) return;

    ie_ent *arrow = NULL;
    if (sr)
        arrow = proj_spawn_arrow_shooter(&sr->d->anw.iew, lv_get(sr->player_livh), 1,
                                         p->e.pos_x, p->e.pos_y, p->e.pos_z, 1.62F,
                                         p->rotation_yaw, p->rotation_pitch, var7 * 2.0F);
    if (arrow && var7 == 1.0F) arrow->is_critical = 1;
    /* the bow's Power, Punch and Flame (EnchantmentHelper.getEnchantmentLevel
     * over the bow's own ench list); Flame's setFire(100) is 2000 ticks, the
     * arrow's getLastActiveItems being null (no fire protection) */
    int power = surv_ench_level(bow, 48), punch = surv_ench_level(bow, 49);
    if (arrow && power > 0) arrow->arrow_damage = arrow->arrow_damage + (double)power * 0.5 + 0.5;
    if (arrow && punch > 0) arrow->knockback_strength = punch;
    if (arrow && surv_ench_level(bow, 50) > 0 && arrow->e.fire < 2000) arrow->e.fire = 2000;
    /* ItemStack.damageItem(1, player): the Unbreaking roll on the player's
     * Random, the break effects and stat */
    if (surv_damage_stack(p, bow, 1) && bow->count <= 0) empty_slot(sv, bow);
    item_rand_float(surv.det, DET_SERVER);
    if (infinite)
    {
        if (arrow) arrow->can_be_picked_up = 2;
    }
    else
        consume_arrow(sv);
    if (arrow) an_adopt_projectile(&sr->d->anw, arrow);
}

/* ------------------------------------------------------------ the client */

int throw_client_use(struct client_player *p)
{
    struct surv_state *sv = &p->sv;
    struct surv_stack *cur = &sv->inv[p->hotbar];

    switch (cur->item)
    {
    case IT_BOW:
        if (has_arrow(sv)) set_item_in_use(sv, p->hotbar);
        return 1;

    case IT_POTION:
        if (!(cur->damage & 16384)) return 0;
        /* fall through */
    case IT_SNOWBALL:
    case IT_EGG:
    case IT_ENDER_PEARL:
    case IT_EXP_BOTTLE:
        --cur->count;
        item_rand_float(surv.det, DET_CLIENT);
        if (cur->count <= 0) empty_slot(sv, cur);
        return 1;

    case IT_ENDER_EYE:
        /* the client world is remote: the frame ray only */
        return 1;
    }
    return 0;
}

void throw_client_release(struct client_player *p)
{
    struct surv_state *sv = &p->sv;
    /* itemInUse, whatever the hotbar key did earlier in this tick */
    struct surv_stack *bow = using_stack(sv, sv->using_slot);
    float var7;

    if (!bow || bow->item != IT_BOW) return;
    int infinite = bow_infinite(bow);
    if (!infinite && !has_arrow(sv)) return;
    if (!bow_charge(sv->using_count, &var7)) return;

    /* new EntityArrow on the client world: the constructor's draws (its
     * heading draws on the arrow's own Random, which is then dropped) */
    int64_t msb, lsb;
    (void)det_next_entity_id_role(surv.det, DET_CLIENT);
    (void)det_new_random_role(surv.det, DET_CLIENT);
    det_uuid_role(surv.det, DET_CLIENT, &msb, &lsb);
    damage_bow_client(p, bow);
    item_rand_float(surv.det, DET_CLIENT);
    if (!infinite) consume_arrow(sv);
}

/* ------------------------------------------------------------ the hooks */

/* EntityEnderPearl.onImpact's server half: setPositionAndUpdate (the
 * handler's setPlayerLocation and its S08), fallDistance 0, then
 * attackEntityFrom(DamageSource.fall, 5.0F), all only while the thrower's
 * worldObj is the pearl's (the hook runs with the pearl's world entered). */
/* A landing ahead of the network tick opens the row's packet queue (as the
 * entity pass's hits do), so what it sends is this row's alone. */
static void pearl_open_queue(struct server_player *sp)
{
    if (!sp->dev_prequeued && !sp->net_phase)
    {
        s2c_clear();
        sp->dev_prequeued = 1;
    }
}

static void pearl_impact(ie_world *iew, ie_ent *pearl)
{
    struct serverreplay *sr = throw_sr;
    if (!sr || !sr->player || !pearl->shooter_is_player || pearl->shooter != lv_ref(lv_get(sr->player_livh))) return;
    if (iew != &sr->d->anw.iew) return;
    struct server_player *sp = sr->player;
    double x = pearl->e.pos_x, y = pearl->e.pos_y, z = pearl->e.pos_z;

    if (pearl->shooter_life != sp->life)
    {
        /* an EntityPlayerMP a respawn replaced threw it: that object stays
         * in the world it died in, and its setPositionAndUpdate goes through
         * the NetHandlerPlayServer it shares with the new player, so the new
         * one lands on the pearl with the old object's rotation. The
         * respawn's own setPlayerLocation, sent while the handler still named
         * the old object, left that at the fresh player's (0, 0). The fall
         * damage hits the old, dead object. */
        if (sp->life - pearl->shooter_life > 8 || sp->life_dim[pearl->shooter_life & 7] != sr->here) return;
        pearl_open_queue(sp);
        if (s2c_out()->n < S2C_MAX)
        {
            sp->has_moved = 0;
            sp->last_pos_x = x;
            sp->last_pos_y = y;
            sp->last_pos_z = z;
            struct s2c_pkt *pkt = s2c_add(s2c_out());
            pkt->kind = PK_S08;
            pkt->f0 = x;
            pkt->f1 = y + 1.6200000047683716;
            pkt->f2 = z;
            pkt->f3 = 0.0F;
            pkt->f4 = 0.0F;
            sp->dev_prequeued = 1;
            entity_set_pos_rot(&sp->e, x, y, z, &sp->rotation_yaw, &sp->rotation_pitch,
                               &sp->prev_rotation_yaw, &sp->prev_rotation_pitch, 0.0F, 0.0F);
            serverreplay_player_moved(sr);
        }
        return;
    }
    if (sp->dimension != sr->here) return;
    pearl_open_queue(sp);

    /* the riding thrower steps off first (EntityPlayerMP.mountEntity(null)) */
    if (sp->ridingh != 0) ride_server_dismount(sp);
    float yaw = sp->rotation_yaw, pitch = sp->rotation_pitch;

    entity_set_position(&sp->e, x, y, z);
    sp->prev_rotation_yaw = yaw;
    sp->prev_rotation_pitch = pitch;
    sp->has_moved = 0;
    sp->last_pos_x = x;
    sp->last_pos_y = y;
    sp->last_pos_z = z;
    if (s2c_out()->n < S2C_MAX)
    {
        struct s2c_pkt *pkt = s2c_add(s2c_out());
        pkt->kind = PK_S08;
        pkt->f0 = x;
        pkt->f1 = y + 1.6200000047683716;
        pkt->f2 = z;
        pkt->f3 = yaw;
        pkt->f4 = pitch;
        /* the queue survives the player's own tick part, which runs after
         * the entity pass here */
        sp->dev_prequeued = 1;
    }
    sp->e.fall_distance = 0.0F;
    float before = sp->sv.health;
    /* the twin's rand is the player's Entity.rand in the entity pass: the
     * hit's draws (setBeenAttacked, the hurt sound) continue it */
    if (sr->player_livh) sp->sv.erand = lv_get(sr->player_livh)->rand;
    surv_server_damage(sp, SURV_FALL, 5.0F);
    if (sr->player_livh) lv_get(sr->player_livh)->rand = sp->sv.erand;
    /* the world's tracker pass after updateEntities: the player's changed
     * health (data watcher 6) reaches its own client as an S1C this tick */
    if (sp->sv.health != before && s2c_out()->n < S2C_MAX)
    {
        struct s2c_pkt *pkt = s2c_add(s2c_out());
        pkt->kind = PK_S1C;
        pkt->f0 = sp->sv.health;
        sp->dev_prequeued = 1;
    }
    if (sr->player_livh)
    {
        lv_get(sr->player_livh)->health = sp->sv.health;
        lv_get(sr->player_livh)->is_dead = sp->sv.dead;
    }
    serverreplay_player_moved(sr);
}

/* The eye's drop is an EntityItem of the living world's lists. */
static ie_ent *eye_drop(ie_world *iew, double x, double y, double z, int item, int damage, int count)
{
    struct serverreplay *sr = throw_sr;
    if (!sr || iew != &sr->d->anw.iew) return NULL;
    return an_spawn_item(&sr->d->anw, sr->d->anw.n, x, y, z, item, damage, count, 0);
}

/* EntityThrowable.getThrower for a throwable loaded from NBT. */
static void resolve_thrower(ie_world *iew, ie_ent *en)
{
    if (throw_sr && iew == &throw_sr->d->anw.iew) sr_throwable_resolve(throw_sr, en);
}

void throw_bind(struct serverreplay *sr)
{
    throw_sr = sr;
    /* every world's pool: a pearl lands, and a loaded one looks its thrower
     * up, in whichever world it flies */
    for (int i = 0; i < 3; ++i)
    {
        sr->dims[i].anw.iew.on_pearl_impact = pearl_impact;
        sr->dims[i].anw.iew.resolve_thrower = resolve_thrower;
        sr->dims[i].anw.iew.spawn_item = eye_drop;
    }
}
