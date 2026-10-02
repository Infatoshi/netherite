/* See tnt.h. */
#include "tnt.h"
#include "env.h"

#include <string.h>

#include "entityquery.h"
#include "explosion.h"
#include "fallhang.h"
#include "item_entity.h"
#include "items.h"
#include "living.h"
#include "player.h"
#include "randomtick.h"
#include "serverreplay.h"
#include "survival.h"

#define ID_TNT 46
#define IT_FLINT_AND_STEEL 259

/* The blast's context for the extras' callbacks: the replay and the TNT's
 * placer (DamageSource.setExplosionSource's attacker). */
struct tnt_blast {
    struct serverreplay *sr;
    int by_player;
    uint64_t placer;          /* a mob placer (living.h lref), 0 none */
    int dim;                  /* the blast's dimension: the player is hit only there */
    int player_hit;
    double kx, ky, kz;        /* the player's entry in Explosion.field_77288_k */
};

#define cur_blast (nw_env->tnt.cur_blast)

static void player_attack(void *ent, float amount)
{
    struct tnt_blast *b = ent;
    struct server_player *p = b->sr->player;

    double at[2] = {p->e.pos_x, p->e.pos_z};
    /* setExplosionSource: the TNT's placer, when there is one: the player,
     * or the mob (a creeper whose blast primed it) the knockback comes from */
    const struct living *mob = b->by_player ? NULL : lv_get(b->placer);
    if (mob != NULL)
    {
        at[0] = mob->e.pos_x;
        at[1] = mob->e.pos_z;
        surv_set_attacker(surv_egg_of_kind(mob->kind) >= 0 ? mob->kind : SURV_ATTACKER_NO_EGG);
        surv_set_attacker_ref(b->placer);
        surv_set_attacker_msg(DMG_EXPLOSION, mob->kind);
    }
    else surv_set_attacker(b->by_player ? SK_PLAYER : -1);
    surv_server_damage_by(p, SURV_EXPLOSION, amount, b->by_player || mob != NULL ? at : NULL);

    if (b->sr->player_livh != 0)
    {
        /* the knockback from the placer lands on the twin too */
        lv_get(b->sr->player_livh)->e.motion_x = p->e.motion_x;
        lv_get(b->sr->player_livh)->e.motion_y = p->e.motion_y;
        lv_get(b->sr->player_livh)->e.motion_z = p->e.motion_z;
        lv_get(b->sr->player_livh)->health = p->sv.health;
        lv_get(b->sr->player_livh)->is_dead = p->sv.dead;
    }
}

static void player_motion(void *ent, double mx, double my, double mz)
{
    struct tnt_blast *b = ent;
    struct server_player *p = b->sr->player;

    p->e.motion_x += mx;
    p->e.motion_y += my;
    p->e.motion_z += mz;
    /* the living twin is the same EntityPlayerMP: the pass copies its motion
     * back onto the server player after each living entity */
    if (b->sr->player_livh != 0)
    {
        struct living *tw = lv_get(b->sr->player_livh);
        tw->e.motion_x += mx;
        tw->e.motion_y += my;
        tw->e.motion_z += mz;
    }

    /* the map's vector is the push before Blast Protection */
    b->player_hit = 1;
    b->kx = nw_env->explosion.raw_push[0];
    b->ky = nw_env->explosion.raw_push[1];
    b->kz = nw_env->explosion.raw_push[2];
}

static void living_attack(void *ent, float amount)
{
    struct living *l = ent;
    struct serverreplay *sr = cur_blast->sr;
    struct living *attacker = cur_blast->by_player ? lv_get(sr->player_livh) : lv_get(cur_blast->placer);

    living_attack_entity_from_attacker(l, attacker, DMG_EXPLOSION, amount, &SR_DET(sr));
}

static void living_motion(void *ent, double mx, double my, double mz)
{
    struct living *l = ent;

    l->e.motion_x += mx;
    l->e.motion_y += my;
    l->e.motion_z += mz;
}

static void ie_attack(void *ent, float amount)
{
    ie_ent *en = ent;
    /* EntitySmallFireball.attackEntityFrom sets nothing */
    if (en->kind == IE_SMALL_FIREBALL) return;
    en->e.velocity_changed = 1;
    /* the living world's items (a creeper blast's drops) and orbs have
     * health: EntityItem.attackEntityFrom and EntityXPOrb's */
    if (en->kind != IE_ITEM && en->kind != IE_ORB) return;
    en->health = (int)((float)en->health - amount);
    if (en->health <= 0) en->is_dead = 1;
}

static void ie_motion(void *ent, double mx, double my, double mz)
{
    ie_ent *en = ent;
    en->e.motion_x += mx;
    en->e.motion_y += my;
    en->e.motion_z += mz;
}

/* A falling block's or a hanging entity's attackEntityFrom: the falling
 * block's setBeenAttacked, a hanging entity's break (an item frame first lets
 * its item go). */
static void fh_attack(void *ent, float amount)
{
    fh_ent *en = ent;
    (void)amount;
    fh_attacked(en->fw, en);
}

static void fh_motion(void *ent, double mx, double my, double mz)
{
    fh_ent *en = ent;
    en->e.motion_x += mx;
    en->e.motion_y += my;
    en->e.motion_z += mz;
}

/* An extra's place in World.getEntitiesWithinAABBExcludingEntity's walk: the
 * chunks x then z, each chunk's sections, each section in insertion order. */
struct blast_key { int cx, cz, cy; uint64_t stamp; };

static struct blast_key extra_key(const struct expl_extra *e)
{
    if (e->attack_from == fh_attack)
    {
        const fh_ent *f = e->ent;
        return (struct blast_key){f->chunk_x, f->chunk_z, f->chunk_y, f->e.chunk_stamp};
    }
    if (e->attack_from == ie_attack)
    {
        const ie_ent *ie = e->ent;
        return (struct blast_key){ie->chunk_x, ie->chunk_z, ie->chunk_y, ie->e.chunk_stamp};
    }
    /* the player: its living twin's place in the chunk lists */
    const struct living *l = e->attack_from == player_attack ? e->armor : e->ent;
    return (struct blast_key){l->chunk_coord_x, l->chunk_coord_z, l->chunk_coord_y, l->e.chunk_stamp};
}

static int key_before(struct blast_key a, struct blast_key b)
{
    if (a.cx != b.cx) return a.cx < b.cx;
    if (a.cz != b.cz) return a.cz < b.cz;
    if (a.cy != b.cy) return a.cy < b.cy;
    return a.stamp < b.stamp;
}

/* The entity pass's entities outside the item pool: the living ones (when
 * the replay runs mobs) with the falling and hanging entities among them in
 * the walk's order, then the server player. */
static int blast_query(void *ctx, struct aabb box, struct expl_extra *out, int cap)
{
    struct tnt_blast *b = ctx;
    struct serverreplay *sr = b->sr;
    int n = 0;

    if (sr->mobs_enabled)
    {
        AN_QUERY_LIST(found);
        int k = an_entities_excluding(&sr->d->anw, an_deref(sr->player_enth), &box, found, AN_MAX_ENTITIES);

        for (int i = 0; i < k && n < cap; ++i)
        {
            if (!found[i]->is_living)
            {
                /* the living world's projectiles (an arrow stuck beside the
                 * TNT): Entity.attackEntityFrom's setBeenAttacked, then the
                 * push (func_92092_a reads no armour on them) */
                ie_ent *ie = ie_get(found[i]->ieh);
                if (ie == NULL || ie->is_dead) continue;
                struct expl_extra *e = &out[n++];
                e->ent = ie;
                e->pos = &ie->e.pos_x;
                e->eye_height = 0.0F;
                e->box = &ie->e.bounding_box;
                e->attack_from = ie_attack;
                e->add_motion = ie_motion;
                e->armor = NULL;
                continue;
            }

            struct living *l = lv_get(found[i]->livh);
            struct expl_extra *e = &out[n++];
            e->ent = l;
            e->pos = &l->e.pos_x;
            e->eye_height = living_eye_height(l);
            e->box = &l->e.bounding_box;
            e->attack_from = living_attack;
            e->add_motion = living_motion;
            e->armor = l;
        }
    }

    /* the falling blocks and hanging entities (paintings, item frames, leash
     * knots) the box reaches, merged into the livings' walk order */
    {
        FH_QUERY_LIST(found);
        int k = fh_entities_in_box(&sr->d->fhw, box, found, FH_MAX_ENTITIES);

        for (int i = 0; i < k && n < cap; ++i)
        {
            struct expl_extra e = {
                .ent = found[i],
                .pos = &found[i]->e.pos_x,
                .eye_height = 0.0F,
                .box = &found[i]->e.bounding_box,
                .attack_from = fh_attack,
                .add_motion = fh_motion,
                .armor = NULL,
            };
            struct blast_key key = extra_key(&e);
            int at = n;
            while (at > 0 && key_before(key, extra_key(&out[at - 1]))) --at;
            memmove(&out[at + 1], &out[at], (size_t)(n - at) * sizeof *out);
            out[at] = e;
            ++n;
        }
    }

    struct server_player *p = sr->player;

    /* the player is in this world's lists only in its own dimension, at
     * its twin's place in the walk (a sheep that entered the chunk section
     * after it dies after it) */
    if (p != NULL && !p->sv.removed && p->dimension == sr->here && n < cap &&
        aabb_intersects(&p->e.bounding_box, &box))
    {
        struct expl_extra e = {
            .ent = b,
            .pos = &p->e.pos_x,
            .eye_height = 1.62F,   /* EntityPlayerMP.getEyeHeight */
            .box = &p->e.bounding_box,
            .attack_from = player_attack,
            .add_motion = player_motion,
            .armor = sr->player_livh != 0 ? lv_get(sr->player_livh) : NULL,
        };
        int at = n;
        if (e.armor != NULL)
        {
            struct blast_key key = extra_key(&e);
            while (at > 0 && key_before(key, extra_key(&out[at - 1]))) --at;
        }
        memmove(&out[at + 1], &out[at], (size_t)(n - at) * sizeof *out);
        out[at] = e;
        ++n;
    }

    return n;
}

int tnt_blast_other(void *ctx, struct aabb box, struct expl_extra *out, int cap)
{
    struct serverreplay *sr = ctx;
    int n = 0;

    /* the item pool: items and orbs lose health, a primed TNT is pushed */
    IE_QUERY_LIST(items);
    int k = entityquery_in_box(&sr->d->iew, box, NULL, items, IE_MAX_ENTITIES);
    for (int i = 0; i < k && n < cap; ++i)
    {
        if (items[i]->is_dead) continue;
        struct expl_extra *e = &out[n++];
        e->ent = items[i];
        e->pos = &items[i]->e.pos_x;
        e->eye_height = 0.0F;
        e->box = &items[i]->e.bounding_box;
        e->attack_from = expl_attack_ie;
        e->add_motion = ie_motion;
        e->armor = NULL;
    }

    /* the falling blocks and the hanging entities */
    fh_ent *found[64];
    k = fh_entities_in_box(&sr->d->fhw, box, found, 64);
    if (k > 64) k = 64;
    for (int i = 0; i < k && n < cap; ++i)
    {
        struct expl_extra *e = &out[n++];
        e->ent = found[i];
        e->pos = &found[i]->e.pos_x;
        e->eye_height = 0.0F;
        e->box = &found[i]->e.bounding_box;
        e->attack_from = fh_attack;
        e->add_motion = fh_motion;
        e->armor = NULL;
    }
    return n;
}

void tnt_replay_explode(void *ctx, struct ie_ent *tnt)
{
    struct serverreplay *sr = ctx;
    /* a TNT in the End's item pool: the End's lists, crystals and dragon */
    if (sr->here == 1)
    {
        serverreplay_end_tnt_explode(sr, tnt);
        return;
    }
    struct tnt_blast b;
    memset(&b, 0, sizeof b);
    b.sr = sr;
    b.by_player = tnt->tnt_by_player;
    b.placer = tnt->tnt_placer;
    b.dim = sr->here;

    struct tnt_blast *prev = cur_blast;
    cur_blast = &b;
    expl_exclude_ie = tnt;
    expl_placed_by_player = tnt->tnt_by_player;
    uint64_t prev_placer = expl_placer;
    expl_placer = tnt->tnt_placer;

    /* the TNT's own world and its World.rand: the pass's dimension (the
     * Nether's TNT explodes over the Nether) */
    struct sr_world *ws = &sr->w[0];
    for (int i = 0; i < sr->nworlds; ++i)
        if (sr->w[i].dim == sr->here) ws = &sr->w[i];

    double x = tnt->e.pos_x, y = tnt->e.pos_y, z = tnt->e.pos_z;
    /* World.rand: the pass's copy inside a living-world entity's tick */
    jrand *wr = sr->in_entity_tick ? &sr->d->anw.iew.world_rand.r : &ST_RAND(&ws->st);
    expl_run_extras(ws->st.w, &SR_DET(sr), DET_SERVER, wr, &sr->d->iew, NULL,
                    x, y, z, 4.0F, 0, 1, NULL, blast_query, &b);

    expl_exclude_ie = NULL;
    expl_placed_by_player = 0;
    expl_placer = prev_placer;
    cur_blast = prev;

    /* WorldServer.newExplosion's S27 to a player within 64 blocks, with its
     * knockback entry (zero when the pass did not reach it) */
    struct server_player *p = sr->player;

    if (p != NULL && p->dimension == sr->here)
    {
        double dx = p->e.pos_x - x, dy = p->e.pos_y - y, dz = p->e.pos_z - z;

        if (dx * dx + dy * dy + dz * dz < 4096.0)
        {
            sr->has_s27 = 1;
            sr->s27_x += b.kx;
            sr->s27_y += b.ky;
            sr->s27_z += b.kz;
        }
    }
}

/* The EntityItem(World, x, y, z) a block popped inside the blast drops
 * (dropBlockAsItem_do's sink): the Entity draws, the four Math.random
 * values, the pickup delay; into the item pool, which the network phase's
 * tail orders. */
static void blast_drop_sink(void *ctx, double x, double y, double z, int item, int damage, int count)
{
    struct serverreplay *sr = ctx;
    int eid = det_next_entity_id_role(&SR_DET(sr), DET_SERVER);
    det_rng r = det_new_random_role(&SR_DET(sr), DET_SERVER);
    int64_t msb, lsb;
    det_uuid_role(&SR_DET(sr), DET_SERVER, &msb, &lsb);
    float hover = (float)(det_math_random_role(&SR_DET(sr), DET_SERVER) * 3.141592653589793 * 2.0);
    float yaw = (float)(det_math_random_role(&SR_DET(sr), DET_SERVER) * 360.0);
    double mx = (double)(float)(det_math_random_role(&SR_DET(sr), DET_SERVER) * 0.20000000298023224 - 0.10000000149011612);
    double mz = (double)(float)(det_math_random_role(&SR_DET(sr), DET_SERVER) * 0.20000000298023224 - 0.10000000149011612);
    ie_ent *en = ie_adopt_item(&sr->d->iew, eid, msb, lsb, det_rng_state(&r), x, y, z, mx, 0.20000000298023224, mz,
                               yaw, hover, item, damage, count, 0);

    if (en == NULL) return;
    en->delay = 10;
    ie_added_to_world(&sr->d->iew, en);
}

void tnt_replay_blast(struct serverreplay *sr, double x, double y, double z, float power,
                      int flaming, int smoking)
{
    struct tnt_blast b;
    memset(&b, 0, sizeof b);
    b.sr = sr;

    struct tnt_blast *prev = cur_blast;
    cur_blast = &b;

    int dim = serverreplay_player_dim(sr);
    b.dim = dim;
    struct world *w = sr->player != NULL ? sr->player->e.world : sr->w[0].st.w;
    jrand *wr = serverreplay_world_rand(sr, dim);

    /* outside the world tick no random-tick stream is published: the blast's
     * own World.rand and sink take the pops its fire and holes cause (a
     * mushroom lit past 13 by the fire drops itself) */
    struct randomtick_env saved, env = {wr, &SR_DET(sr), DET_SERVER, blast_drop_sink, sr};
    randomtick_tick_save(&saved);
    randomtick_tick_load(&env);
    /* in the End the blast reaches the crystals and the dragon's parts */
    if (dim == 1)
    {
        double k[3];
        serverreplay_end_run_blast(sr, wr, x, y, z, power, flaming, smoking, 0, k);
        b.kx = k[0];
        b.ky = k[1];
        b.kz = k[2];
    }
    else
        expl_run_extras(w, &SR_DET(sr), DET_SERVER, wr, &sr->d->iew, NULL,
                        x, y, z, power, flaming, smoking, NULL, blast_query, &b);
    randomtick_tick_load(&saved);

    cur_blast = prev;

    /* WorldServer.newExplosion's S27 to the player within 64 blocks */
    struct server_player *p = sr->player;

    if (p != NULL)
    {
        double dx = p->e.pos_x - x, dy = p->e.pos_y - y, dz = p->e.pos_z - z;

        if (dx * dx + dy * dy + dz * dz < 4096.0)
        {
            sr->has_s27 = 1;
            sr->s27_x += b.kx;
            sr->s27_y += b.ky;
            sr->s27_z += b.kz;
        }
    }
}

int tnt_server_activate(struct server_player *p, int x, int y, int z)
{
    struct surv_stack *cur = &p->sv.inv[p->sv.current_item];

    if (cur->count <= 0 || cur->item != IT_FLINT_AND_STEEL) return 0;

    /* func_150114_a(world, x, y, z, 1, player): the primed entity */
    struct serverreplay *sr = p->replay;

    if (sr != NULL)
    {
        ie_ent *tnt = ie_spawn_tnt(&sr->d->iew, (double)((float)x + 0.5F), (double)((float)y + 0.5F),
                                   (double)((float)z + 0.5F), 1);
        if (tnt != NULL) ie_added_to_world(&sr->d->iew, tnt);
    }

    world_set_block(p->e.world, x, y, z, 0, 0, 3);

    /* ItemStack.damageItem(1, player); processPlayerBlockPlacement's tail
     * then drops a worn-out stack */
    if (surv_damage_stack(p, cur, 1) && cur->count <= 0)
    {
        *cur = (struct surv_stack){0};
        cur->gen = ++p->sv.gen_counter;
    }

    return 1;
}
