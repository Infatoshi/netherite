/* See endfight.h. */
#include "nbtw.h"
#include "endfight.h"
#include "env.h"

#include <math.h>
#include <string.h>

#include "aabb.h"
#include "entity_nbt.h"
#include "living.h"
#include "snapshot.h"
#include "tape.h"
#include "world.h"

void endfight_spawn_dragon(struct sr_end *e, struct world *w, det_state *det, float yaw)
{
    struct dragon_crystal_state *keep ENV_LOCAL = envstack_take(DRAGON_MAX_CRYSTALS * sizeof *keep);
    int n = e->d.n_crystals;

    memcpy(keep, e->d.crystals, DRAGON_MAX_CRYSTALS * sizeof *keep);
    dragon_init(&e->d, w, det, 0.0, 128.0, 0.0, yaw);
    memcpy(e->d.crystals, keep, DRAGON_MAX_CRYSTALS * sizeof *keep);
    e->d.n_crystals = n;
    endfight_dragon_chunk(&e->d, 1);
    e->dragon_spawned = 1;
    e->dragon_listed = 1;
}

int endfight_spawn_crystal(struct sr_end *e, det_state *det, double x, double y, double z, float yaw)
{
    int i = dragon_crystal_init(&e->d, det, x, y, z);

    if (i >= 0)
    {
        e->d.crystals[i].yaw = yaw;
        e->d.crystals[i].chunk_stamp = ++entity_chunk_stamp;
    }
    return i;
}

/* EntityDragon: Entity, EntityLivingBase and EntityLiving's blocks and
 * nothing of its own. The dragon moves through noClip's moveEntity, which
 * never reaches the fire and fall tails; it stays out of water. Its
 * attributes are EntityLiving's four with maxHealth at 200. */
void endfight_dragon_w(struct nbtw *w, const struct dragon_state *d)
{
    struct living *l = &nw_scratch->endfight_living;
    det_state fake;

    det_init(&fake);
    living_init(l, NULL, GK_GHAST, &fake);
    det_free(&fake);
    l->kind = 1000;              /* no kind's own tail */
    attrs_init(&l->attrs);
    attrs_set_base(&l->attrs.a[ATTR_MAX_HEALTH], 200.0);
    attrs_set_base(&l->attrs.a[ATTR_KNOCKBACK_RESISTANCE], 0.0);
    attrs_set_base(&l->attrs.a[ATTR_MOVEMENT_SPEED], 0.10000000149011612);
    attrs_set_base(&l->attrs.a[ATTR_FOLLOW_RANGE], 16.0);
    l->e.pos_x = d->x;
    l->e.pos_y = d->y;
    l->e.pos_z = d->z;
    l->e.y_size = 0.0F;
    l->e.motion_x = d->mx;
    l->e.motion_y = d->my;
    l->e.motion_z = d->mz;
    l->rotation_yaw = d->yaw;
    l->rotation_pitch = 0.0F;
    l->e.fall_distance = 0.0F;
    l->e.fire = 0;
    l->air = d->air;
    l->e.on_ground = 0;
    l->dimension = 1;
    l->uuid_msb = d->uuid_msb;
    l->uuid_lsb = d->uuid_lsb;
    l->health = d->health;
    l->hurt_time = d->hurt_time;
    l->death_time = 0;
    l->attack_time = 0;
    l->absorption = 0.0F;

    living_write_w(l, w);
}

nbt *endfight_dragon_nbt(const struct dragon_state *d)
{
    nbt *tag = nbt_new_compound();
    struct nbtw w;
    nbtw_tree(&w, tag);
    endfight_dragon_w(&w, d);
    return tag;
}

/* EntityEnderCrystal writes nothing of its own; its onUpdate never runs
 * Entity.onEntityUpdate, so the base fields stay the constructor's. */
void endfight_crystal_w(struct nbtw *w, const struct dragon_crystal_state *c)
{
    struct entity e;

    memset(&e, 0, sizeof e);
    e.pos_x = c->x;
    e.pos_y = c->y;
    e.pos_z = c->z;
    e.motion_x = c->mx;
    e.motion_y = c->my;
    e.motion_z = c->mz;
    ent_w_base(w, &e, 1, 300, 0, c->uuid_msb, c->uuid_lsb, c->yaw, 0.0F);
}

nbt *endfight_crystal_nbt(const struct dragon_crystal_state *c)
{
    nbt *tag = nbt_new_compound();
    struct nbtw w;
    nbtw_tree(&w, tag);
    endfight_crystal_w(&w, c);
    return tag;
}

int endfight_dragon_chunk(struct dragon_state *d, int force)
{
    int cx = (int)floor(d->x / 16.0), cy = (int)floor(d->y / 16.0), cz = (int)floor(d->z / 16.0);

    if (!force && cx == d->chunk_x && cy == d->chunk_y && cz == d->chunk_z) return 0;

    d->chunk_x = cx;
    d->chunk_y = cy;
    d->chunk_z = cz;
    d->chunk_stamp = ++entity_chunk_stamp;
    return 1;
}

int endfight_dragon_listed_near(const struct dragon_state *d, const struct aabb *box)
{
    int x0 = (int)floor((box->min_x - 2.0) / 16.0), x1 = (int)floor((box->max_x + 2.0) / 16.0);
    int z0 = (int)floor((box->min_z - 2.0) / 16.0), z1 = (int)floor((box->max_z + 2.0) / 16.0);
    int y0 = (int)floor((box->min_y - 2.0) / 16.0), y1 = (int)floor((box->max_y + 2.0) / 16.0);
    int cy = d->chunk_y < 0 ? 0 : d->chunk_y > 15 ? 15 : d->chunk_y;

    if (y0 < 0) y0 = 0;
    if (y0 > 15) y0 = 15;
    if (y1 < 0) y1 = 0;
    if (y1 > 15) y1 = 15;

    return d->chunk_x >= x0 && d->chunk_x <= x1 && d->chunk_z >= z0 && d->chunk_z <= z1 && cy >= y0 && cy <= y1;
}

void endfight_reload_dragon(struct sr_end *e, struct world *w, det_state *det, const struct dragon_state *o)
{
    struct dragon_crystal_state *keep ENV_LOCAL = envstack_take(DRAGON_MAX_CRYSTALS * sizeof *keep);
    int n = e->d.n_crystals;

    memcpy(keep, e->d.crystals, DRAGON_MAX_CRYSTALS * sizeof *keep);
    dragon_init(&e->d, w, det, 0.0, 0.0, 0.0, 0.0F);
    memcpy(e->d.crystals, keep, DRAGON_MAX_CRYSTALS * sizeof *keep);
    e->d.n_crystals = n;

    struct dragon_state *d = &e->d;
    d->uuid_msb = o->uuid_msb;
    d->uuid_lsb = o->uuid_lsb;
    d->mx = fabs(o->mx) > 10.0 ? 0.0 : o->mx;
    d->my = fabs(o->my) > 10.0 ? 0.0 : o->my;
    d->mz = fabs(o->mz) > 10.0 ? 0.0 : o->mz;
    d->x = o->x;
    d->y = o->y;
    d->z = o->z;
    d->yaw = d->prev_yaw = fmodf(o->yaw, 360.0F);
    /* setPosition's box, 16 wide from the feet */
    d->bb_min_x = d->x - 8.0;
    d->bb_max_x = d->x + 8.0;
    d->bb_min_y = d->y;
    d->bb_min_z = d->z - 8.0;
    d->bb_max_z = d->z + 8.0;
    d->health = o->health;
    d->hurt_time = o->hurt_time;
    d->air = o->air;
    d->mob_griefing = o->mob_griefing;
    e->dragon_listed = 1;
    endfight_dragon_chunk(d, 1);
}

int endfight_count(const struct sr_end *e)
{
    int n = e->dragon_listed ? 1 : 0;

    for (int i = 0; i < e->d.n_crystals; ++i)
        if (e->d.crystals[i].in_world) ++n;

    return n;
}

static double nbt_d(const nbt *v)
{
    uint64_t bits = v ? nbt_double_bits(v) : 0;
    double d;
    memcpy(&d, &bits, sizeof d);
    return d;
}

static float nbt_f(const nbt *v)
{
    uint32_t bits = v ? nbt_float_bits(v) : 0;
    float f;
    memcpy(&f, &bits, sizeof f);
    return f;
}

void endfight_snapshot_dragon(struct sr_end *e, struct world *w, det_state *det, const struct snap_entity *se)
{
    struct dragon_crystal_state keep[DRAGON_MAX_CRYSTALS];
    int n = e->d.n_crystals;
    struct dragon_state *d = &e->d;
    const struct snap_dragon *x = se->dragon;
    const nbt *tag = se->tag;

    memcpy(keep, d->crystals, sizeof keep);
    memset(d, 0, sizeof *d);
    memcpy(d->crystals, keep, sizeof keep);
    d->n_crystals = n;
    d->healing_crystal_index = -1;
    d->world = w;
    d->det = det;
    d->mob_griefing = 1;

    d->entity_id = se->id;
    d->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
    d->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
    d->rand.r.seed = se->rand_state;
    d->rand.have_next_next_gaussian = se->has_gauss;
    d->rand.next_next_gaussian = se->has_gauss ? se->gauss : 0.0;
    d->x = nbt_d(nbt_list_get(nbt_get(tag, "Pos"), 0));
    d->y = nbt_d(nbt_list_get(nbt_get(tag, "Pos"), 1));
    d->z = nbt_d(nbt_list_get(nbt_get(tag, "Pos"), 2));
    d->mx = nbt_d(nbt_list_get(nbt_get(tag, "Motion"), 0));
    d->my = nbt_d(nbt_list_get(nbt_get(tag, "Motion"), 1));
    d->mz = nbt_d(nbt_list_get(nbt_get(tag, "Motion"), 2));
    d->yaw = nbt_f(nbt_list_get(nbt_get(tag, "Rotation"), 0));
    d->health = nbt_f(nbt_get(tag, "HealF"));
    d->hurt_time = (int)nbt_int_value(nbt_get(tag, "HurtTime"));
    d->death_time = (int)nbt_int_value(nbt_get(tag, "DeathTime"));
    d->living_sound_time = se->lsf;
    d->air = (int)nbt_int_value(nbt_get(tag, "Air"));
    {
        /* Entity.inWater and firstUpdate (a dragon read back from its chunk
         * this tick has not updated yet) */
        const struct jval *f = se->rt ? json_get(se->rt, "f") : NULL;
        const char *iw = f ? json_str(json_get(f, "inWater")) : NULL;
        const char *fu = f ? json_str(json_get(f, "firstUpdate")) : NULL;
        d->in_water = iw != NULL && !strcmp(iw, "b:1");
        d->first_update = fu != NULL && !strcmp(fu, "b:1");
    }

    d->prev_x = x->prev[0];
    d->prev_y = x->prev[1];
    d->prev_z = x->prev[2];
    d->prev_yaw = x->prev_yaw;
    d->bb_min_x = x->bb[0];
    d->bb_min_y = x->bb[1];
    d->bb_min_z = x->bb[2];
    d->bb_max_x = x->bb[3];
    d->bb_max_z = x->bb[4];
    d->tx = x->target[0];
    d->ty = x->target[1];
    d->tz = x->target[2];
    d->anim = x->anim;
    d->prev_anim = x->prev_anim;
    d->yaw_velocity = x->yaw_velocity;
    d->render_yaw = x->render_yaw;
    d->force_target = x->force;
    d->slowed = x->slowed;
    d->has_target = x->hunt >= 0;
    d->death_ticks = x->death_ticks;
    d->ticks = x->ticks_existed;
    d->hurt_resistant_time = x->hurt_resistant_time;
    d->last_damage = x->last_damage;
    d->prev_health = x->prev_health;
    d->dead_flag = d->health <= 0.0F;
    d->ring_index = x->ring_index;
    if (x->has_ring)
        for (int i = 0; i < 64; ++i)
        {
            d->ring[i][0] = x->ring[i][0];
            d->ring[i][1] = x->ring[i][1];
        }
    for (int i = 0; i < 7; ++i)
    {
        d->part[i].entity_id = x->part_id[i];
        d->part[i].x = x->part[i][0];
        d->part[i].y = x->part[i][1];
        d->part[i].z = x->part[i][2];
        d->part[i].width = x->part_w[i];
        d->part[i].height = x->part_h[i];
    }

    e->dragon_spawned = 1;
    e->dragon_listed = 1;
    endfight_dragon_chunk(d, 1);
}

int endfight_snapshot_crystal(struct sr_end *e, const struct snap_entity *se)
{
    struct dragon_state *d = &e->d;
    const nbt *tag = se->tag;

    if (d->n_crystals >= DRAGON_MAX_CRYSTALS) return -1;

    int i = d->n_crystals++;
    struct dragon_crystal_state *c = &d->crystals[i];

    memset(c, 0, sizeof *c);
    c->entity_id = se->id;
    c->uuid_msb = nbt_int_value(nbt_get(tag, "UUIDMost"));
    c->uuid_lsb = nbt_int_value(nbt_get(tag, "UUIDLeast"));
    c->rand.r.seed = se->rand_state;
    c->rand.have_next_next_gaussian = se->has_gauss;
    c->rand.next_next_gaussian = se->has_gauss ? se->gauss : 0.0;
    c->x = nbt_d(nbt_list_get(nbt_get(tag, "Pos"), 0));
    c->y = nbt_d(nbt_list_get(nbt_get(tag, "Pos"), 1));
    c->z = nbt_d(nbt_list_get(nbt_get(tag, "Pos"), 2));
    c->mx = nbt_d(nbt_list_get(nbt_get(tag, "Motion"), 0));
    c->my = nbt_d(nbt_list_get(nbt_get(tag, "Motion"), 1));
    c->mz = nbt_d(nbt_list_get(nbt_get(tag, "Motion"), 2));
    c->yaw = nbt_f(nbt_list_get(nbt_get(tag, "Rotation"), 0));
    c->inner_rotation = se->has_crystal ? se->crystal_rotation : 0;
    c->health = se->has_crystal ? se->crystal_health : 5;
    c->in_world = 1;
    /* a crystal setDead this tick (destroyed by a hit or a blast) is still
     * in loadedEntityList until the next entity pass takes it out */
    {
        int64_t dead = 0;
        if (se->rt) json_int(json_get(json_get(se->rt, "f"), "isDead"), &dead);
        c->dead = dead != 0;
    }
    c->chunk_stamp = ++entity_chunk_stamp;
    return i;
}

void endfight_resolve_heal(struct sr_end *e, int crystal_id)
{
    e->d.healing_crystal_index = -1;
    for (int i = 0; crystal_id >= 0 && i < e->d.n_crystals; ++i)
        if (e->d.crystals[i].entity_id == crystal_id && e->d.crystals[i].in_world) e->d.healing_crystal_index = i;
}
