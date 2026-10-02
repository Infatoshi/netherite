#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "clientworld.h"
#include "tileticks.h"
#include "particles_live.h"
#include "arena.h"
#include "env.h"
#include "collide.h"
#include "aabb.h"
#include "blocks.h"
#include "entity.h"
#include "item_entity.h"
#include "jmath.h"
#include "living.h"

static inline double mh_wrap_angle_to_180(double value)
{
    /* fmod is exact: an angle inside (-360, 360) is its own remainder */
    if (!(value > -360.0 && value < 360.0)) value = fmod(value, 360.0);
    if (value >= 180.0) value -= 360.0;
    if (value < -180.0) value += 360.0;
    return value;
}

/* MathHelper.wrapAngleTo180_float */
static inline float mh_wrap_angle_to_180_float(float value)
{
    if (!(value > -360.0F && value < 360.0F)) value = fmodf(value, 360.0F);
    if (value >= 180.0F) value -= 360.0F;
    if (value < -180.0F) value += 360.0F;
    return value;
}

static inline void client_entity_constructor_draws(det_state *det, bool is_living)
{
    det_next_entity_id_role(det, DET_CLIENT);
    det_new_random_role(det, DET_CLIENT);
    int64_t msb, lsb;
    det_uuid_role(det, DET_CLIENT, &msb, &lsb);
    if (is_living)
    {
        det_math_random_role(det, DET_CLIENT);
        det_math_random_role(det, DET_CLIENT);
        det_math_random_role(det, DET_CLIENT);
    }
}

/* Chunk.addEntity at the entity's position: the list slice and its stamp. */
static void client_entity_chunk_add(struct client_entity *e)
{
    e->chunk_x = mh_floor(e->x / 16.0);
    e->chunk_z = mh_floor(e->z / 16.0);
    int cy = mh_floor(e->y / 16.0);
    e->chunk_y = cy < 0 ? 0 : (cy > 15 ? 15 : cy);
    e->chunk_seq = ++entity_chunk_stamp;
}

void clientworld_stamp_at_least(uint64_t v)
{
    if (entity_chunk_stamp < v) entity_chunk_stamp = v;
}

/* updateEntityWithOptionalForce's tail: a new chunk position moves the
 * entity to the end of the new slice's list (y compared unclamped). */
static void client_entity_chunk_update(struct client_entity *e)
{
    if (mh_floor(e->x / 16.0) != e->chunk_x || mh_floor(e->y / 16.0) != e->chunk_y ||
        mh_floor(e->z / 16.0) != e->chunk_z)
        client_entity_chunk_add(e);
}

/* EntityFX's base constructor: the jitter/scale/maxAge draws happen in the
 * Entity base constructor's rand (a fresh Entity.Random per instance, but
 * our det stream just records the roles in order). */
/* RenderGlobal.doSpawnParticle's gates for the kinds past the specials:
 * particleSetting 1 draws theWorld.rand.nextInt(3) (0 makes it 2), then
 * the 16-block distance from the render view entity, then the setting (only
 * 0 and 1 spawn). */
static int client_particle_spawns(struct clientworld *cw, double px, double py, double pz,
                                  double plx, double ply, double plz)
{
    int setting = env_particle_setting();
    if (setting == 1 && jr_int_n(&cw->rand, 3) == 0) setting = 2;
    double dx = plx - px;
    double dy = ply - py;
    double dz = plz - pz;
    if (dx * dx + dy * dy + dz * dz > 256.0) return 0;
    return setting <= 1;
}

/* The Entity constructor, then EntityFX(world, x, y, z, mx, my, mz)'s five
 * Math.random, then n of the kind's own. */
static void client_fx_draws(struct clientworld *cw, int n)
{
    det_next_entity_id_role(cw->det, DET_CLIENT);
    det_new_random_role(cw->det, DET_CLIENT);
    int64_t msb, lsb;
    det_uuid_role(cw->det, DET_CLIENT, &msb, &lsb);
    for (int i = 0; i < 5 + n; ++i) det_math_random_role(cw->det, DET_CLIENT);
}

/* RenderGlobal.spawnParticle("bubble", ...): EntityBubbleFX's motion and
 * age (four more Math.random). */
static void client_entity_bubble_draws(struct clientworld *cw, double px, double py, double pz,
                                       double plx, double ply, double plz)
{
    if (client_particle_spawns(cw, px, py, pz, plx, ply, plz)) client_fx_draws(cw, 4);
}

/* RenderGlobal.spawnParticle("smoke", ...): EntitySmokeFX's grey and age
 * (two more Math.random). */
static void client_entity_smoke_draws(struct clientworld *cw, double px, double py, double pz,
                                      double plx, double ply, double plz)
{
    if (client_particle_spawns(cw, px, py, pz, plx, ply, plz)) client_fx_draws(cw, 2);
}

void clientworld_init(struct clientworld *cw, struct world *w, det_state *det, uint64_t rand_state, int update_lcg, bool has_void_particles)
{
    memset(cw, 0, sizeof(*cw));
    cw->world = w;
    cw->det = det;
    cw->rand.seed = rand_state;
    cw->update_lcg = update_lcg;
    cw->has_void_particles = has_void_particles;
    cw->surface_world = has_void_particles;
    cw->nents = 0;
    cw->negative_check_skip_torch_draw = false;
}

struct client_entity *clientworld_get_entity(struct clientworld *cw, int id)
{
    for (int i = 0; i < cw->nents; ++i)
    {
        if (cw->ents[i].id == id)
        {
            return &cw->ents[i];
        }
    }
    return NULL;
}

struct client_entity *clientworld_add_entity(struct clientworld *cw, int id, bool is_living,
                                             double x, double y, double z, float yaw, float pitch)
{
    struct client_entity *e = clientworld_get_entity(cw, id);
    if (!e)
    {
        if (cw->nents >= CW_MAX_ENTITIES) list_full("entities in the client world", CW_MAX_ENTITIES);
        e = &cw->ents[cw->nents++];
    }
    if (!e) return NULL;

    memset(e, 0, sizeof(*e));
    e->id = id;
    e->is_living = is_living;
    e->x = x;
    e->y = y;
    e->z = z;
    e->yaw = yaw;
    e->pitch = pitch;
    return e;
}

/* Entity.mountEntity on the client: null sets the rider down on top of its
 * old vehicle (setLocationAndAngles at posX, boundingBox.minY + height,
 * posZ); a vehicle links both ends unless it would close a cycle. */
static void client_mount(struct clientworld *cw, struct client_entity *r, struct client_entity *v)
{
    struct client_entity *old = r->riding_id ? clientworld_get_entity(cw, r->riding_id) : NULL;

    if (v == NULL)
    {
        if (old != NULL)
        {
            r->x = old->x;
            r->y = old->y + (double)old->height;
            r->z = old->z;
            r->box_kept = false;
            old->ridden_by_id = 0;
        }
        r->riding_id = 0;
        return;
    }

    if (old != NULL) old->ridden_by_id = 0;
    for (struct client_entity *q = v->riding_id ? clientworld_get_entity(cw, v->riding_id) : NULL; q != NULL;
         q = q->riding_id ? clientworld_get_entity(cw, q->riding_id) : NULL)
        if (q == r) return;
    r->riding_id = v->id;
    v->ridden_by_id = r->id;
}

/* World.removeEntity on the copy with this id: the rider dismounts onto
 * it, and it leaves its own vehicle */
static void remove_copy(struct clientworld *cw, int id)
{
    for (int j = 0; j < cw->nents; ++j)
    {
        if (cw->ents[j].id != id) continue;
        struct client_entity *gone = &cw->ents[j];
        struct client_entity *rider = gone->ridden_by_id ? clientworld_get_entity(cw, gone->ridden_by_id) : NULL;
        if (rider && rider->riding_id == gone->id) client_mount(cw, rider, NULL);
        if (gone->riding_id) client_mount(cw, gone, NULL);
        for (int k = j; k < cw->nents - 1; ++k) cw->ents[k] = cw->ents[k + 1];
        cw->nents--;
        return;
    }
}

void clientworld_handle_packet(struct clientworld *cw, const struct tracker_packet *p)
{
    if (p->kind == TRACKER_PKT_S0D)
    {
        det_next_entity_id_role(cw->det, DET_CLIENT);
        det_new_random_role(cw->det, DET_CLIENT);
        int64_t msb, lsb;
        det_uuid_role(cw->det, DET_CLIENT, &msb, &lsb);
        for (int m = 0; m < 5; ++m)
        {
            det_math_random_role(cw->det, DET_CLIENT);
        }
        for (int j = 0; j < cw->nents; ++j)
        {
            if (cw->ents[j].id == p->id)
            {
                for (int k = j; k < cw->nents - 1; ++k) cw->ents[k] = cw->ents[k + 1];
                cw->nents--;
                break;
            }
        }
    }
    else if (p->kind == TRACKER_PKT_S0F)
    {
        /* WorldClient.addEntityToWorld: a copy with the id already there
         * (an entry that forgot the dead player spawns again) is removed
         * for the new one */
        remove_copy(cw, p->id);
        struct client_entity *e;
        {
            if (cw->nents >= CW_MAX_ENTITIES) list_full("entities in the client world", CW_MAX_ENTITIES);
            e = &cw->ents[cw->nents++];
            memset(e, 0, sizeof(*e));
            e->id = p->id;
            e->is_living = true;
            e->kind = -1;
            e->next_step = 1;
            e->x = (double)p->x / 32.0;
            e->y = (double)p->y / 32.0;
            e->z = (double)p->z / 32.0;
            e->server_pos_x = p->x;
            e->server_pos_y = p->y;
            e->server_pos_z = p->z;
            e->dw16 = p->data;
            e->flags0 = p->flags0;
            e->kindw = p->kindw;
            e->yaw = (float)(p->yaw * 360) / 256.0f;
            e->pitch = (float)(p->pitch * 360) / 256.0f;
            e->head_yaw = (float)(p->head_yaw * 360) / 256.0f;
            /* handleSpawnMob divides in float, unlike S0E and S12 */
            e->motion_x = (double)((float)p->mx / 8000.0F);
            e->motion_y = (double)((float)p->my / 8000.0F);
            e->motion_z = (double)((float)p->mz / 8000.0F);
            client_entity_chunk_add(e);
            client_entity_constructor_draws(cw->det, true);
        }
    }
    else if (p->kind == TRACKER_PKT_S0E)
    {
        remove_copy(cw, p->id);
        struct client_entity *e;
        {
            if (cw->nents >= CW_MAX_ENTITIES) list_full("entities in the client world", CW_MAX_ENTITIES);
            e = &cw->ents[cw->nents++];
            memset(e, 0, sizeof(*e));
            e->id = p->id;
            e->is_living = false;
            e->x = (double)p->x / 32.0;
            e->y = (double)p->y / 32.0;
            e->z = (double)p->z / 32.0;
            e->server_pos_x = p->x;
            e->server_pos_y = p->y;
            e->server_pos_z = p->z;
            e->yaw = (float)(p->yaw * 360) / 256.0f;
            e->pitch = (float)(p->pitch * 360) / 256.0f;
            e->motion_x = (double)p->mx / 8000.0;
            e->motion_y = (double)p->my / 8000.0;
            e->motion_z = (double)p->mz / 8000.0;
            client_entity_constructor_draws(cw->det, false);
            if (p->type == 2)
            {
                det_math_random_role(cw->det, DET_CLIENT);
                det_math_random_role(cw->det, DET_CLIENT);
                det_math_random_role(cw->det, DET_CLIENT);
                det_math_random_role(cw->det, DET_CLIENT);
            }
            /* handleSpawnObject's fireball branches: the EntityLargeFireball
             * (world, x/32, y/32, z/32, mx/8000, my/8000, mz/8000)
             * constructor normalises the packet's motion fields into the
             * acceleration (ax / len * 0.1); the motion stays at rest
             * (below) until an S12 on velocityChanged sets it. */
            if (p->type == 63 || p->type == 64)
            {
                double ax = (double)p->mx / 8000.0;
                double ay = (double)p->my / 8000.0;
                double az = (double)p->mz / 8000.0;
                /* MathHelper.sqrt_double: a float */
                double len = (double)(float)sqrt(ax * ax + ay * ay + az * az);
                e->is_fireball = true;
                e->is_large_fireball = p->type == 63;
                /* the constructor's setLocationAndAngles: lastTickPos is the
                 * spawn's position until the copy's first update */
                e->prev_x = e->x;
                e->prev_y = e->y;
                e->prev_z = e->z;
                e->width = p->type == 63 ? 1.0F : 0.3125F;
                e->height = e->width;
                e->accel_x = ax / len * 0.1;
                e->accel_y = ay / len * 0.1;
                e->accel_z = az / len * 0.1;
                /* handleSpawnObject zeroes a fireball packet's data
                 * (func_149002_g(0), NetHandlerPlayClient.java:345, 350):
                 * no setVelocity, and the constructor leaves the motion at
                 * rest, so the copy's first update does not move it */
                e->motion_x = e->motion_y = e->motion_z = 0.0;
            }
            client_entity_chunk_add(e);
        }
    }
    else if (p->kind == TRACKER_PKT_S15)
    {
        struct client_entity *e = clientworld_get_entity(cw, p->id);
        if (e)
        {
            e->server_pos_x += p->x;
            e->server_pos_y += p->y;
            e->server_pos_z += p->z;
            double tx = (double)e->server_pos_x / 32.0;
            double ty = (double)e->server_pos_y / 32.0;
            double tz = (double)e->server_pos_z / 32.0;

            if (e->is_living)
            {
                e->new_pos_x = tx;
                e->new_pos_y = ty;
                e->new_pos_z = tz;
                /* no rotation in an S15: the target is the current one */
                e->new_rotation_yaw = (double)e->yaw;
                e->new_rotation_pitch = (double)e->pitch;
                e->new_pos_rotation_increments = 3;
            }
            else
            {
                e->x = tx;
                e->y = ty;
                e->z = tz;
            }
        }
    }
    else if (p->kind == TRACKER_PKT_S16)
    {
        struct client_entity *e = clientworld_get_entity(cw, p->id);
        if (e)
        {
            float tyaw = (float)(p->yaw * 360) / 256.0f;
            float tpitch = (float)(p->pitch * 360) / 256.0f;

            if (e->is_living)
            {
                /* handleEntityMovement's setPositionAndRotation2 also aims
                 * the interpolation at the unchanged server position */
                e->new_pos_x = (double)e->server_pos_x / 32.0;
                e->new_pos_y = (double)e->server_pos_y / 32.0;
                e->new_pos_z = (double)e->server_pos_z / 32.0;
                e->new_rotation_yaw = (double)tyaw;
                e->new_rotation_pitch = (double)tpitch;
                e->new_pos_rotation_increments = 3;
            }
            else
            {
                e->yaw = tyaw;
                e->pitch = tpitch;
            }
        }
    }
    else if (p->kind == TRACKER_PKT_S17)
    {
        struct client_entity *e = clientworld_get_entity(cw, p->id);
        if (e)
        {
            e->server_pos_x += p->x;
            e->server_pos_y += p->y;
            e->server_pos_z += p->z;
            double tx = (double)e->server_pos_x / 32.0;
            double ty = (double)e->server_pos_y / 32.0;
            double tz = (double)e->server_pos_z / 32.0;
            float tyaw = (float)(p->yaw * 360) / 256.0f;
            float tpitch = (float)(p->pitch * 360) / 256.0f;

            if (e->is_living)
            {
                e->new_pos_x = tx;
                e->new_pos_y = ty;
                e->new_pos_z = tz;
                e->new_rotation_yaw = (double)tyaw;
                e->new_rotation_pitch = (double)tpitch;
                e->new_pos_rotation_increments = 3;
            }
            else
            {
                e->x = tx;
                e->y = ty;
                e->z = tz;
                e->yaw = tyaw;
                e->pitch = tpitch;
            }
        }
    }
    else if (p->kind == TRACKER_PKT_S18)
    {
        struct client_entity *e = clientworld_get_entity(cw, p->id);
        if (e)
        {
            e->server_pos_x = p->x;
            e->server_pos_y = p->y;
            e->server_pos_z = p->z;
            double tx = (double)e->server_pos_x / 32.0;
            double ty = (double)e->server_pos_y / 32.0;
            double tz = (double)e->server_pos_z / 32.0;
            float tyaw = (float)(p->yaw * 360) / 256.0f;
            float tpitch = (float)(p->pitch * 360) / 256.0f;

            if (e->is_living)
            {
                /* handleEntityTeleport: 1/64 above the server position, then
                 * EntityLivingBase.setPositionAndRotation2's three steps */
                e->new_pos_x = tx;
                e->new_pos_y = ty + 0.015625;
                e->new_pos_z = tz;
                e->new_rotation_yaw = (double)tyaw;
                e->new_rotation_pitch = (double)tpitch;
                e->new_pos_rotation_increments = 3;
            }
            else
            {
                /* handleEntityTeleport's 1/64 lift for every entity, then
                 * Entity.setPositionAndRotation2: the position at once (the
                 * push out of a block it overlaps is not modelled: the
                 * copies here are fireballs in the air) */
                e->x = tx;
                e->y = ty + 0.015625;
                e->z = tz;
                e->yaw = tyaw;
                e->pitch = tpitch;
                e->new_pos_rotation_increments = 0;
            }
        }
    }
    else if (p->kind == TRACKER_PKT_S12)
    {
        struct client_entity *e = clientworld_get_entity(cw, p->id);
        if (e)
        {
            e->motion_x = (double)p->mx / 8000.0;
            e->motion_y = (double)p->my / 8000.0;
            e->motion_z = (double)p->mz / 8000.0;
        }
    }
    else if (p->kind == TRACKER_PKT_S19)
    {
        struct client_entity *e = clientworld_get_entity(cw, p->id);
        if (e)
        {
            e->head_yaw = (float)(p->head_yaw * 360) / 256.0f;
        }
    }
    else if (p->kind == TRACKER_PKT_S1C)
    {
        struct client_entity *e = clientworld_get_entity(cw, p->id);
        if (e)
        {
            e->dw16 = p->data;
            e->flags0 = p->flags0;
            e->kindw = p->kindw;
        }
    }
    else if (p->kind == TRACKER_PKT_S1A)
    {
        /* handleHealthUpdate(3): setHealth(0) and onDeath */
        struct client_entity *e = clientworld_get_entity(cw, p->id);
        if (e && e->is_living) e->dying = true;
    }
    else if (p->kind == TRACKER_PKT_S1B)
    {
        /* NetHandlerPlayClient.handleEntityAttach: a vehicle the client does
         * not have reads as null */
        struct client_entity *r = clientworld_get_entity(cw, p->id);
        struct client_entity *v = p->vehicle_id > 0 ? clientworld_get_entity(cw, p->vehicle_id) : NULL;
        if (r && p->leash == 0) client_mount(cw, r, v);
    }
    else if (p->kind == TRACKER_PKT_S13)
    {
        for (int i = 0; i < p->num_ids; ++i) remove_copy(cw, p->ids[i]);
    }
    else if (p->kind == TRACKER_PKT_S23)
    {
        world_set_block(cw->world, p->x, p->y, p->z, p->block, p->meta, 3);
    }
    else if (p->kind == TRACKER_PKT_S22)
    {
        for (int i = 0; i < p->num_records; ++i)
        {
            world_set_block(cw->world, p->rx[i], p->ry[i], p->rz[i], p->rblock[i], p->rmeta[i], 3);
        }
    }
    else if (p->kind == TRACKER_PKT_S28)
    {
        if (p->effect == 1004 || (p->effect >= 1007 && p->effect <= 1012) || (p->effect >= 1014 && p->effect <= 1017))
        {
            jr_float(&cw->rand);
            jr_float(&cw->rand);
        }
        else if (p->effect >= 1020 && p->effect <= 1022)
        {
            jr_float(&cw->rand);
        }
    }
}

/* A living copy's move body's self: the world, and the copy whose cell memo
 * the fall state's water test reads */
struct cw_body_self {
    struct clientworld *cw;
    struct client_entity *ce;
};

static void client_living_fall_state(struct entity *e, double dy, int on_ground)
{
    /* EntityLivingBase.updateFallState: out of water after the move, the
     * water check again at the new box (its flow pushes before the friction) */
    if (!e->in_water && ie_water_accelerate_memo(e->world, e, &((struct cw_body_self *)e->self)->ce->cmemo))
    {
        e->in_water = 1;
        e->fall_distance = 0.0F;
    }
    if (on_ground) e->fall_distance = 0.0F;
    else if (dy < 0.0) e->fall_distance = (float)((double)e->fall_distance - dy);
}

/* EntityBodyHelper.func_75664_a on the client: an AI mob's body yaw follows
 * its (interpolated) yaw while it moves and eases toward its head yaw at
 * rest. The chicken's is what places its rider. */
static float client_body_limit(float a, float b, float limit)
{
    float v4 = mh_wrap_angle_to_180_float(a - b);

    if (v4 < -limit) v4 = -limit;
    if (v4 >= limit) v4 = limit;

    return a - v4;
}

static void client_body_helper(struct client_entity *e)
{
    double v1 = e->x - e->prev_x;
    double v3 = e->z - e->prev_z;

    if (v1 * v1 + v3 * v3 > 2.500000277905201E-7)
    {
        e->render_yaw_offset = e->yaw;
        e->head_yaw = client_body_limit(e->render_yaw_offset, e->head_yaw, 75.0F);
        e->body_yaw = e->head_yaw;
        e->body_counter = 0;
    }
    else
    {
        float v5 = 75.0F;

        if (fabsf(e->head_yaw - e->body_yaw) > 15.0F)
        {
            e->body_counter = 0;
            e->body_yaw = e->head_yaw;
        }
        else
        {
            ++e->body_counter;

            if (e->body_counter > 10)
            {
                float v6 = 1.0F - (float)(e->body_counter - 10) / 10.0F;
                v5 = (v6 > 0.0F ? v6 : 0.0F) * 75.0F;
            }
        }

        e->render_yaw_offset = client_body_limit(e->head_yaw, e->render_yaw_offset, v5);
    }
}

/* Block.onEntityWalking under a copy's step: the redstone ore's
 * func_150185_e on the client world (the lit ore's class too), its particle
 * rows on WorldClient.rand, and the unlit ore lights in the client's copy */
static void client_walking_block(void *self, int x, int y, int z, int id)
{
    struct clientworld *cw = ((struct cw_body_self *)self)->cw;
    if ((id != 73 && id != 74) || cw->walk_fx == NULL) return;
    particles_live_redstone_sparkle(cw->walk_fx, x, y, z);
    if (id == 73) world_set_block(cw->world, x, y, z, 74, 0, 3);
}

struct aabb client_entity_box(const struct client_entity *e)
{
    if (e->box_kept) return e->box;
    float half = e->width / 2.0F;
    return aabb_make(e->x - (double)half, e->y, e->z - (double)half,
                     e->x + (double)half, e->y + (double)e->height, e->z + (double)half);
}

/* One client entity's onUpdate: the interpolation, the motion without AI
 * input, and the chicken's body yaw. */
static void client_update_entity(struct clientworld *cw, struct client_entity *e)
{
    /* EntitySlime.onUpdate ahead of super's: the squish eases toward its
     * amount, and the copy notes whether it stood on the ground */
    int slime = e->is_living && IS_SLIME_KIND(e->kind);
    int was_on_ground = e->on_ground;
    if (slime) e->squish_factor += (e->squish_amount - e->squish_factor) * 0.5F;
    e->prev_x = e->x;
    e->prev_y = e->y;
    e->prev_z = e->z;

    /* Entity.onEntityUpdate's handleWaterMovement, before onLivingUpdate:
     * inWater over the box expanded 0.4 down and contracted 0.001, the flow's
     * push on the motion, and fallDistance cleared in water. The squid
     * (its own isInWater) and the ghast (EntityFlying) keep their own
     * movement below. */
    int own_move = e->kind == AK_SQUID || e->kind == GK_GHAST;
    if (cw->simulate_living_physics && e->is_living && e->width > 0.0F && !own_move)
    {
        struct entity we;
        memset(&we, 0, sizeof we);
        we.bounding_box = client_entity_box(e);
        we.motion_x = e->motion_x;
        we.motion_y = e->motion_y;
        we.motion_z = e->motion_z;
        e->in_water = ie_water_accelerate_memo(cw->world, &we, &e->cmemo);
        e->motion_x = we.motion_x;
        e->motion_y = we.motion_y;
        e->motion_z = we.motion_z;
        if (e->in_water) e->fall_distance = 0.0F;
    }

    /* EntityLivingBase.onEntityUpdate's onDeathUpdate: at deathTime 20 the
     * copy sets itself dead (its update goes on; the world drops it after) */
    if (e->is_living && e->dying && ++e->death_time == 20) e->is_dead = true;

    int interpolated = e->is_living && e->new_pos_rotation_increments > 0;
    if (e->is_living && e->new_pos_rotation_increments > 0)
    {
        double dx = e->x + (e->new_pos_x - e->x) / (double)e->new_pos_rotation_increments;
        double dy = e->y + (e->new_pos_y - e->y) / (double)e->new_pos_rotation_increments;
        double dz = e->z + (e->new_pos_z - e->z) / (double)e->new_pos_rotation_increments;
        double dyaw = mh_wrap_angle_to_180(e->new_rotation_yaw - (double)e->yaw);
        e->yaw = (float)((double)e->yaw + dyaw / (double)e->new_pos_rotation_increments);
        e->pitch = (float)((double)e->pitch + (e->new_rotation_pitch - (double)e->pitch) / (double)e->new_pos_rotation_increments);
        --e->new_pos_rotation_increments;
        e->x = dx;
        e->y = dy;
        e->z = dz;
        e->box_kept = false;
    }

    if (cw->simulate_living_physics && e->is_living && e->width > 0.0F)
    {
        /* EntityLivingBase.onLivingUpdate's client branch has no AI input.
         * It still moves under the last S0F/S12 velocity between S14
         * position packets; that motion changes the hitbox under the
         * crosshair even when the server position packet is unchanged. */
        /* EntityBlaze.onLivingUpdate ahead of super's: a falling blaze
         * keeps 0.6 of its fall, on the client too */
        if (e->kind == HK_BLAZE && !e->on_ground && e->motion_y < 0.0) e->motion_y *= 0.6;
        if (!interpolated)
        {
            e->motion_x *= 0.98;
            e->motion_y *= 0.98;
            e->motion_z *= 0.98;
        }
        if (fabs(e->motion_x) < 0.005) e->motion_x = 0.0;
        if (fabs(e->motion_y) < 0.005) e->motion_y = 0.0;
        if (fabs(e->motion_z) < 0.005) e->motion_z = 0.0;

        struct entity body;
        entity_init(&body, cw->world);
        entity_set_size(&body, e->width, e->height);
        /* EntityLivingBase's 0.5F; EntityEnderman's constructor makes it
         * 1.0F (the client copy steps a block up with it; seed-1 S13 row
         * 6556) */
        body.step_height = e->kind == HK_ENDERMAN ? 1.0F : 0.5F;
        body.y_size = e->y_size;
        entity_set_position(&body, e->x, e->y, e->z);
        /* a box setScaleForAge shrank from its min corner is where the move
         * starts: moveEntity then puts posX and posZ at its centre (a
         * child's copy steps half the size change toward the min corner) */
        if (e->box_kept) body.bounding_box = e->box;
        e->box_kept = false;
        body.motion_x = e->motion_x;
        body.motion_y = e->motion_y;
        body.motion_z = e->motion_z;
        body.on_ground = e->on_ground;
        body.fall_distance = e->fall_distance;
        /* the ghast and the bat keep an empty updateFallState, the squid its
         * own isInWater: none of them looks again */
        body.in_water = e->in_water || own_move || e->kind == AK_BAT;
        body.distance_walked_modified = e->dist_walked;
        body.distance_walked_on_step_modified = e->dist_walked_step;
        body.next_step_distance = e->next_step;
        /* canTriggerWalking: false for the squid, the bat and the
         * silverfish (and no step for a copy of unknown kind) */
        body.can_trigger_walking = e->kind >= 0 && e->kind != AK_SQUID && e->kind != AK_BAT && e->kind != HK_SILVERFISH;
        struct cw_body_self body_self = {cw, e};
        body.self = &body_self;
        body.walking_block = client_walking_block;
        /* EntityLivingBase.moveEntityWithHeading with no strafe or forward
         * input (moveFlying does nothing): the water branch, the lava branch
         * (Entity.handleLavaMovement over the box shrunk 0.1 sideways and 0.4
         * at the bottom), else the ground branch */
        int in_lava = !own_move && !e->in_water && ie_lava_memo(cw->world, &body.bounding_box, &e->cmemo);
        if (!own_move && (e->in_water || in_lava))
        {
            double v8 = body.pos_y;
            double k = e->in_water ? 0.800000011920929 : 0.5;
            float fell = body.fall_distance;
            entity_move_memo(&body, body.motion_x, body.motion_y, body.motion_z, 0, client_living_fall_state, &e->cmemo);
            e->landed_fall = body.on_ground && fell > 0.0F ? fell : 0.0F;
            body.motion_x *= k;
            body.motion_y *= k;
            body.motion_z *= k;
            body.motion_y -= 0.02;
            if (body.is_collided_horizontally)
            {
                /* Entity.isOffsetPositionInLiquid */
                struct aabb ob = aabb_offset(body.bounding_box, body.motion_x,
                                             body.motion_y + 0.6000000238418579 - body.pos_y + v8, body.motion_z);
                struct collide_list *list COLLIDE_SCRATCH = collide_scratch_begin();
                collide_list_clear(list);
                world_get_colliding_bounding_boxes(cw->world, ob, list);
                if (list->n == 0 && !living_world_is_any_liquid(cw->world, ob)) body.motion_y = 0.30000001192092896;
            }
        }
        else if (e->kind == AK_SQUID)
        {
            /* EntitySquid.moveEntityWithHeading: the move alone (its motion
             * the server's, set by the S12s; no gravity on the client) */
            float fell = body.fall_distance;
            entity_move_memo(&body, body.motion_x, body.motion_y, body.motion_z, 0, client_living_fall_state, &e->cmemo);
            e->landed_fall = body.on_ground && fell > 0.0F ? fell : 0.0F;
        }
        else if (e->kind == GK_GHAST)
        {
            /* EntityFlying.moveEntityWithHeading's air branch: no input, the
             * move, then the friction on every axis (no gravity) */
            float friction = 0.91F;
            if (body.on_ground)
                friction = BLOCKS[world_get_block(cw->world, mh_floor(body.pos_x), mh_floor(body.bounding_box.min_y) - 1,
                                                  mh_floor(body.pos_z)) & 4095].slipperiness * 0.91F;
            float fell = body.fall_distance;
            entity_move_memo(&body, body.motion_x, body.motion_y, body.motion_z, 0, client_living_fall_state, &e->cmemo);
            e->landed_fall = body.on_ground && fell > 0.0F ? fell : 0.0F;
            body.motion_x *= (double)friction;
            body.motion_y *= (double)friction;
            body.motion_z *= (double)friction;
        }
        else
        {
        float friction = 0.91F;
        if (body.on_ground)
        {
            int bx = mh_floor(body.pos_x);
            int by = mh_floor(body.bounding_box.min_y) - 1;
            int bz = mh_floor(body.pos_z);
            friction = BLOCKS[world_get_block(cw->world, bx, by, bz) & 4095].slipperiness * 0.91F;
        }
        /* EntitySpider.isOnLadder: the climb flag the server watches */
        int on_ladder = (e->kind == HK_SPIDER || e->kind == HK_CAVE_SPIDER) && (e->dw16 & 1);
        if (on_ladder)
        {
            const double lim = (double)0.15F;
            if (body.motion_x < -lim) body.motion_x = -lim;
            if (body.motion_x > lim) body.motion_x = lim;
            if (body.motion_z < -lim) body.motion_z = -lim;
            if (body.motion_z > lim) body.motion_z = lim;
            body.fall_distance = 0.0F;
            if (body.motion_y < -0.15) body.motion_y = -0.15;
        }
        float fell = body.fall_distance;
        entity_move_memo(&body, body.motion_x, body.motion_y, body.motion_z,
                         0, client_living_fall_state, &e->cmemo);
        e->landed_fall = body.on_ground && fell > 0.0F ? fell : 0.0F;
        if (body.is_collided_horizontally && on_ladder) body.motion_y = 0.2;
        body.motion_y -= 0.08;
        body.motion_y *= 0.9800000190734863;
        body.motion_x *= (double)friction;
        body.motion_z *= (double)friction;
        }
        e->x = body.pos_x;
        e->y = body.pos_y;
        e->z = body.pos_z;
        e->motion_x = body.motion_x;
        e->motion_y = body.motion_y;
        e->motion_z = body.motion_z;
        e->on_ground = body.on_ground;
        e->fall_distance = body.fall_distance;
        e->y_size = body.y_size;
        if (!own_move && e->kind != AK_BAT) e->in_water = body.in_water;
        e->dist_walked = body.distance_walked_modified;
        e->dist_walked_step = body.distance_walked_on_step_modified;
        e->next_step = body.next_step_distance;
        /* EntityChicken.onLivingUpdate after super's: the flapping slow
         * fall, on the client's copy too */
        if (e->kind == AK_CHICKEN && !e->on_ground && e->motion_y < 0.0) e->motion_y *= 0.6;
        /* EntityBat.onUpdate after super's: a hanging bat (DataWatcher 16
         * bit 1) stops on its block's underside, a flying one damps its
         * fall, on the client's copy too */
        if (e->kind == AK_BAT)
        {
            if (e->dw16 & 1)
            {
                e->motion_x = e->motion_y = e->motion_z = 0.0;
                e->y = (double)mh_floor(e->y) + 1.0 - (double)e->height;
            }
            else e->motion_y *= 0.6000000238418579;
        }
    }

    /* EntityAgeable.onLivingUpdate after super's: setScaleForAge on the
     * client, Entity.setSize keeping the box's min corner (the spawn's
     * constructor size shrinks to a child's off centre) */
    if (e->age_width > 0.0F && (e->age_width != e->width || e->age_height != e->height))
    {
        e->box = client_entity_box(e);
        e->box_kept = true;
        e->width = e->age_width;
        e->height = e->age_height;
        e->box.max_x = e->box.min_x + (double)e->width;
        e->box.max_z = e->box.min_z + (double)e->width;
        e->box.max_y = e->box.min_y + (double)e->height;
    }

    if (e->is_living && e->kind == AK_CHICKEN) client_body_helper(e);
    /* EntitySlime.onUpdate after super's: a landing squashes, a take-off
     * stretches, then alterSquishAmount (EntityMagmaCube keeps 0.9, the
     * slime 0.6); the landing's particle and sound draws are the copy's own
     * Random, which nothing drawn reads */
    if (slime)
    {
        if (e->on_ground && !was_on_ground) e->squish_amount = -0.5F;
        else if (!e->on_ground && was_on_ground) e->squish_amount = 1.0F;
        e->squish_amount *= e->kind == SK_MAGMA_CUBE ? 0.9F : 0.6F;
    }
}

/* Entity.updateRidden on the client: no motion, the rider's own onUpdate,
 * then the vehicle's updateRiderPosition. The chicken carries its rider 0.1
 * ahead along its body yaw at half its height; any other vehicle at 0.75 of
 * its height. The skeleton's getYOffset is 0.5 lower. */
static void client_update_ridden(struct clientworld *cw, struct client_entity *r, const struct client_entity *v)
{
    r->motion_x = r->motion_y = r->motion_z = 0.0;
    client_update_entity(cw, r);

    double yoff = r->kind == HK_SKELETON ? -0.5 : 0.0;
    if (v->kind == AK_CHICKEN)
    {
        float s1 = mh_sin(v->render_yaw_offset * 3.1415927F / 180.0F);
        float c1 = mh_cos(v->render_yaw_offset * 3.1415927F / 180.0F);
        r->x = v->x + (double)(0.1F * s1);
        r->y = v->y + (double)(v->height * 0.5F) + yoff + (double)0.0F;
        r->z = v->z - (double)(0.1F * c1);
        r->render_yaw_offset = v->render_yaw_offset;
    }
    else
    {
        r->x = v->x;
        r->y = v->y + (double)v->height * 0.75 + yoff;
        r->z = v->z;
    }
    r->box_kept = false;
    r->fall_distance = 0.0F;
}

/* World.updateEntities on the client: a rider on a live vehicle that still
 * carries it waits for that vehicle, which updates it right after itself. */
void clientworld_tick_entities(struct clientworld *cw, double player_x, double player_y, double player_z)
{
    for (int i = 0; i < cw->nents; ++i)
    {
        struct client_entity *e = &cw->ents[i];
        if (e->is_dead) continue;

        if (e->no_chunk && !e->riding_id)
        {
            /* updateEntityWithOptionalForce on an entity not addedToChunk:
             * no update, the chunk check adds it (the client's chunkExists
             * is always true), and a rider still updates after it */
            e->prev_yaw = e->yaw;
            e->prev_pitch = e->pitch;
            client_entity_chunk_add(e);
            e->no_chunk = false;
            goto rider;
        }

        if (e->is_fireball)
        {
            /* EntityFireball.onUpdate's client branch: Entity.onUpdate runs
             * onEntityUpdate only (prevPos taken; the onFire block is
             * server-only), then setFire, the inGround branch skipped, the
             * position += motion, the rotation from atan2 with
             * wrapAngleTo180 and the 0.2 prev blend, motion += accel and
             * the *0.95F damping (0.8F in water, 4 bubble particles), and
             * setPosition. The block raytrace and the entity scan exist
             * only on the server: no onImpact on the client, so a client
             * fireball flies until an S13 removes it. The vanilla order
             * puts the movement before the accel blend; mirror it. */
            e->prev_x = e->x;
            e->prev_y = e->y;
            e->prev_z = e->z;
            /* Entity.onEntityUpdate's kill below y -64 */
            if (e->y < -64.0) { e->is_dead = true; continue; }
            /* Entity.onEntityUpdate's handleWaterMovement: inWater over the
             * box expanded 0.4 down and contracted 0.001, with the flow's
             * push on the motion (World.handleMaterialAcceleration) */
            {
                struct entity we;
                memset(&we, 0, sizeof we);
                double half = (double)(e->width / 2.0F);
                we.bounding_box = aabb_make(e->x - half, e->y, e->z - half,
                                            e->x + half, e->y + (double)e->height, e->z + half);
                we.motion_x = e->motion_x;
                we.motion_y = e->motion_y;
                we.motion_z = e->motion_z;
                e->in_water = ie_water_accelerate(cw->world, &we, 0);
                e->motion_x = we.motion_x;
                e->motion_y = we.motion_y;
                e->motion_z = we.motion_z;
            }
            e->x += e->motion_x;
            e->y += e->motion_y;
            e->z += e->motion_z;
            float f = (float)sqrt(e->motion_x * e->motion_x + e->motion_z * e->motion_z);
            e->prev_yaw = e->yaw;
            e->prev_pitch = e->pitch;
            /* EntityFireball.onUpdate's rotation block: atan2 of the motion,
             * the wrap into the prev window, then the 0.2 prev blend (the
             * same math projectile.c's update_rotations carries). */
            float yaw = (float)(atan2(e->motion_z, e->motion_x) * 180.0 / 3.141592653589793) + 90.0F;
            float pitch = (float)(atan2((double)f, e->motion_y) * 180.0 / 3.141592653589793) - 90.0F;
            while (pitch - e->prev_pitch < -180.0F) e->prev_pitch -= 360.0F;
            while (pitch - e->prev_pitch >= 180.0F) e->prev_pitch += 360.0F;
            while (yaw - e->prev_yaw < -180.0F) e->prev_yaw -= 360.0F;
            while (yaw - e->prev_yaw >= 180.0F) e->prev_yaw += 360.0F;
            e->pitch = e->prev_pitch + (pitch - e->prev_pitch) * 0.2F;
            e->yaw = e->prev_yaw + (yaw - e->prev_yaw) * 0.2F;
            /* isInWater(): 4 bubbles and 0.8F, else 0.95F; motion += accel,
             * then the (double) float damping */
            float var17 = 0.95F;
            if (e->in_water)
            {
                for (int k = 0; k < 4; ++k)
                    client_entity_bubble_draws(cw, e->x - e->motion_x * 0.25, e->y - e->motion_y * 0.25,
                                               e->z - e->motion_z * 0.25, player_x, player_y, player_z);
                var17 = 0.8F;
            }
            e->motion_x += e->accel_x;
            e->motion_y += e->accel_y;
            e->motion_z += e->accel_z;
            e->fireball_burning = true;   /* the setFire(1) above */
            e->motion_x *= (double)var17;
            e->motion_y *= (double)var17;
            e->motion_z *= (double)var17;
            /* RenderGlobal.spawnParticle("smoke", posX, posY + 0.5, posZ,
             * 0, 0, 0) every tick: the player distance gate, then the
             * EntityFX smoke constructor draws */
            client_entity_smoke_draws(cw, e->x, e->y + 0.5, e->z, player_x, player_y, player_z);
            client_entity_chunk_update(e);
            continue;
        }

        if (e->riding_id)
        {
            struct client_entity *v = clientworld_get_entity(cw, e->riding_id);
            if (v && !v->is_dead && v->ridden_by_id == e->id) continue;
            if (v) v->ridden_by_id = 0;
            e->riding_id = 0;
        }

        client_update_entity(cw, e);
        /* updateEntityWithOptionalForce's chunk move comes before the
         * rider's own update, which makes its own */
        client_entity_chunk_update(e);

    rider:
        if (e->ridden_by_id)
        {
            struct client_entity *r = clientworld_get_entity(cw, e->ridden_by_id);
            if (r && !r->is_dead && r->riding_id == e->id && e->is_dead)
            {
                /* Entity.updateRidden on a vehicle that set itself dead in
                 * its update: the rider lets go, no update this tick
                 * (EntityLivingBase.updateRidden's fall reset) */
                r->riding_id = 0;
                r->fall_distance = 0.0F;
            }
            else if (r && !r->is_dead && r->riding_id == e->id && r->no_chunk)
            {
                r->prev_yaw = r->yaw;
                r->prev_pitch = r->pitch;
                client_entity_chunk_add(r);
                r->no_chunk = false;
            }
            else if (r && !r->is_dead && r->riding_id == e->id)
            {
                client_update_ridden(cw, r, e);
                client_entity_chunk_update(r);
            }
            else
            {
                if (r) r->riding_id = 0;
                e->ridden_by_id = 0;
            }
        }
    }
}

static int class_is(int id, const char *name)
{
    const char *c = BLOCKS[id & 4095].class_name;
    return c != NULL && strcmp(c, name) == 0;
}

static int solid_top_surface(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;
    int meta = world_get_meta(w, x, y, z);

    if (MATERIALS[BLOCKS[id].material].is_opaque && BLOCKS[id].normal_block) return 1;
    if (class_is(id, "BlockStairs")) return (meta & 4) == 4;
    if (class_is(id, "BlockSlab")) return (meta & 8) == 8;
    if (class_is(id, "BlockHopper")) return 1;
    if (class_is(id, "BlockSnow")) return (meta & 7) == 7;

    return 0;
}

static void spawn_particle_draws(struct clientworld *cw, double px, double py, double pz,
                                 double part_x, double part_y, double part_z, int math_draws)
{
    double dx = px - part_x;
    double dy = py - part_y;
    double dz = pz - part_z;
    if (dx * dx + dy * dy + dz * dz <= 256.0)
    {

        det_next_entity_id_role(cw->det, DET_CLIENT);
        det_new_random_role(cw->det, DET_CLIENT);
        int64_t msb, lsb;
        det_uuid_role(cw->det, DET_CLIENT, &msb, &lsb);
        for (int i = 0; i < math_draws; ++i)
        {
            det_math_random_role(cw->det, DET_CLIENT);
        }
    }
}

void clientworld_random_display_tick(struct clientworld *cw, int id, int bx, int by, int bz, det_rng *var5,
                                           double px, double py, double pz)
{
    if (id == 50) /* BlockTorch */
    {
        if (!cw->negative_check_skip_torch_draw)
        {
            int meta = world_get_meta(cw->world, bx, by, bz);
            double var7 = (double)((float)bx + 0.5f);
            double var9 = (double)((float)by + 0.7f);
            double var11 = (double)((float)bz + 0.5f);
            double var13 = 0.2199999988079071;
            double var15 = 0.27000001072883606;

            double sx = var7, sy = var9, sz = var11;
            if (meta == 1) { sx = var7 - var15; sy = var9 + var13; }
            else if (meta == 2) { sx = var7 + var15; sy = var9 + var13; }
            else if (meta == 3) { sz = var11 - var15; sy = var9 + var13; }
            else if (meta == 4) { sz = var11 + var15; sy = var9 + var13; }

            /* smoke: 7 math draws */
            spawn_particle_draws(cw, px, py, pz, sx, sy, sz, 7);
            /* flame: 6 math draws */
            spawn_particle_draws(cw, px, py, pz, sx, sy, sz, 6);
        }
    }
    else if (id == 51) /* BlockFire */
    {
        if (det_rng_int_n(var5, 24) == 0)
        {
            det_rng_float(var5);
            det_rng_float(var5);
        }
    }
    else if (id == 8 || id == 9) /* BlockWater */
    {
        if (det_rng_int_n(var5, 10) == 0)
        {
            int meta = world_get_meta(cw->world, bx, by, bz);
            if (meta <= 0 || meta >= 8)
            {
                float r1 = det_rng_float(var5);
                float r2 = det_rng_float(var5);
                float r3 = det_rng_float(var5);
                double part_x = (double)((float)bx + r1);
                double part_y = (double)((float)by + r2);
                double part_z = (double)((float)bz + r3);
                /* suspended: 6 math draws */
                spawn_particle_draws(cw, px, py, pz, part_x, part_y, part_z, 6);
            }
        }
        if (det_rng_int_n(var5, 64) == 0)
        {
            int meta = world_get_meta(cw->world, bx, by, bz);
            if (meta > 0 && meta < 8)
            {
                det_rng_float(var5);
                det_rng_float(var5);
            }
        }
        if (det_rng_int_n(var5, 10) == 0 && solid_top_surface(cw->world, bx, by - 1, bz))
        {
            int b2 = world_get_block(cw->world, bx, by - 2, bz) & 4095;
            if (!MATERIALS[BLOCKS[b2].material].blocks_movement)
            {
                float r1 = det_rng_float(var5);
                float r2 = det_rng_float(var5);
                double part_x = (double)((float)bx + r1);
                double part_y = (double)by - 1.05;
                double part_z = (double)((float)bz + r2);
                /* dripWater: 1 math draw */
                spawn_particle_draws(cw, px, py, pz, part_x, part_y, part_z, 1);
            }
        }
    }
    else if (id == 10 || id == 11) /* BlockLava */
    {
        int above = world_get_block(cw->world, bx, by + 1, bz) & 4095;
        if (above == 0)
        {
            if (det_rng_int_n(var5, 100) == 0)
            {
                float r1 = det_rng_float(var5);
                float r2 = det_rng_float(var5);
                double part_x = (double)((float)bx + r1);
                double part_y = (double)by + 1.0;
                double part_z = (double)((float)bz + r2);
                /* lava: 1 math draw */
                spawn_particle_draws(cw, px, py, pz, part_x, part_y, part_z, 1);
                det_rng_float(var5);
                det_rng_float(var5);
            }
            if (det_rng_int_n(var5, 200) == 0)
            {
                det_rng_float(var5);
                det_rng_float(var5);
            }
        }
        if (det_rng_int_n(var5, 10) == 0 && solid_top_surface(cw->world, bx, by - 1, bz))
        {
            int b2 = world_get_block(cw->world, bx, by - 2, bz) & 4095;
            if (!MATERIALS[BLOCKS[b2].material].blocks_movement)
            {
                float r1 = det_rng_float(var5);
                float r2 = det_rng_float(var5);
                double part_x = (double)((float)bx + r1);
                double part_y = (double)by - 1.05;
                double part_z = (double)((float)bz + r2);
                /* dripLava: 1 math draw */
                spawn_particle_draws(cw, px, py, pz, part_x, part_y, part_z, 1);
            }
        }
    }
    else if (id == 18 || id == 161) /* BlockLeaves */
    {
        /* In clear weather, leaves do no draws */
    }
    else if (id == 62) /* BlockFurnace (lit) */
    {
        int meta = world_get_meta(cw->world, bx, by, bz);
        float var7 = (float)bx + 0.5f;
        float r1 = det_rng_float(var5);
        float var8 = (float)by + 0.0f + r1 * 6.0f / 16.0f;
        float var9 = (float)bz + 0.5f;
        float var10 = 0.52f;
        float r2 = det_rng_float(var5);
        float var11 = r2 * 0.6f - 0.3f;

        double sx = var7, sy = var8, sz = var9;
        if (meta == 4) { sx = (double)(var7 - var10); sz = (double)(var9 + var11); }
        else if (meta == 5) { sx = (double)(var7 + var10); sz = (double)(var9 + var11); }
        else if (meta == 2) { sx = (double)(var7 + var11); sz = (double)(var9 - var10); }
        else if (meta == 3) { sx = (double)(var7 + var11); sz = (double)(var9 + var10); }

        /* smoke: 7 math draws */
        spawn_particle_draws(cw, px, py, pz, sx, sy, sz, 7);
        /* flame: 6 math draws */
        spawn_particle_draws(cw, px, py, pz, sx, sy, sz, 6);
    }
    else if (id == 90) /* BlockPortal */
    {
        if (det_rng_int_n(var5, 100) == 0)
        {
            det_rng_float(var5);
        }
        for (int var6 = 0; var6 < 4; ++var6)
        {
            float r1 = det_rng_float(var5);
            float r2 = det_rng_float(var5);
            float r3 = det_rng_float(var5);
            double part_x = (double)((float)bx + r1);
            double part_y = (double)((float)by + r2);
            double part_z = (double)((float)bz + r3);
            int var19 = det_rng_int_n(var5, 2) * 2 - 1;
            det_rng_float(var5);
            det_rng_float(var5);
            det_rng_float(var5);
            int b_neg_x = world_get_block(cw->world, bx - 1, by, bz);
            int b_pos_x = world_get_block(cw->world, bx + 1, by, bz);
            if (b_neg_x != 90 && b_pos_x != 90)
            {
                part_x = (double)bx + 0.5 + 0.25 * (double)var19;
                det_rng_float(var5);
            }
            else
            {
                part_z = (double)bz + 0.5 + 0.25 * (double)var19;
                det_rng_float(var5);
            }
            /* portal: 7 math draws */
            spawn_particle_draws(cw, px, py, pz, part_x, part_y, part_z, 7);
        }
    }
    else if (id == 117) /* BlockBrewingStand */
    {
        float r1 = det_rng_float(var5);
        float r2 = det_rng_float(var5);
        float r3 = det_rng_float(var5);
        double sx = (double)((float)bx + 0.4F + r1 * 0.2F);
        double sy = (double)((float)by + 0.7F + r2 * 0.3F);
        double sz = (double)((float)bz + 0.4F + r3 * 0.2F);
        /* smoke: 7 math draws */
        spawn_particle_draws(cw, px, py, pz, sx, sy, sz, 7);
    }
    else if (id == 116) /* BlockEnchantmentTable: the bookshelf glyphs */
    {
        for (int var6 = bx - 2; var6 <= bx + 2; ++var6)
        {
            for (int var7 = bz - 2; var7 <= bz + 2; ++var7)
            {
                if (var6 > bx - 2 && var6 < bx + 2 && var7 == bz - 1) var7 = bz + 2;
                if (det_rng_int_n(var5, 16) != 0) continue;

                for (int var8 = by; var8 <= by + 1; ++var8)
                {
                    if ((world_get_block(cw->world, var6, var8, var7) & 4095) != 47) continue;
                    if ((world_get_block(cw->world, (var6 - bx) / 2 + bx, var8, (var7 - bz) / 2 + bz) & 4095) != 0) break;

                    det_rng_float(var5);
                    det_rng_float(var5);
                    det_rng_float(var5);
                    /* enchantmenttable: 7 math draws, as the portal's */
                    spawn_particle_draws(cw, px, py, pz, (double)bx + 0.5, (double)by + 2.0, (double)bz + 0.5, 7);
                }
            }
        }
    }
    else if (id == 76) /* BlockRedstoneTorch (active) */
    {
        int meta = world_get_meta(cw->world, bx, by, bz);
        float r1 = det_rng_float(var5);
        float r2 = det_rng_float(var5);
        float r3 = det_rng_float(var5);
        double var7 = (double)((float)bx + 0.5f) + (double)(r1 - 0.5f) * 0.2;
        double var9 = (double)((float)by + 0.7f) + (double)(r2 - 0.5f) * 0.2;
        double var11 = (double)((float)bz + 0.5f) + (double)(r3 - 0.5f) * 0.2;
        double var13 = 0.2199999988079071;
        double var15 = 0.27000001072883606;

        double sx = var7, sy = var9, sz = var11;
        if (meta == 1) { sx = var7 - var15; sy = var9 + var13; }
        else if (meta == 2) { sx = var7 + var15; sy = var9 + var13; }
        else if (meta == 3) { sz = var11 - var15; sy = var9 + var13; }
        else if (meta == 4) { sz = var11 + var15; sy = var9 + var13; }

        /* reddust: 10 math draws */
        spawn_particle_draws(cw, px, py, pz, sx, sy, sz, 10);
    }
}

static bool player_touches_water(struct world *w, double px, double py, double pz)
{
    double minX = px - 0.3 + 0.001;
    double maxX = px + 0.3 - 0.001;
    double minY = (py - 1.62) + 0.4 + 0.001;
    double maxY = (py + 0.18) - 0.4 - 0.001;
    double minZ = pz - 0.3 + 0.001;
    double maxZ = pz + 0.3 - 0.001;

    int x0 = mh_floor(minX);
    int x1 = mh_floor(maxX + 1.0);
    int y0 = mh_floor(minY);
    int y1 = mh_floor(maxY + 1.0);
    int z0 = mh_floor(minZ);
    int z1 = mh_floor(maxZ + 1.0);

    for (int x = x0; x < x1; ++x)
    {
        for (int y = y0; y < y1; ++y)
        {
            for (int z = z0; z < z1; ++z)
            {
                int id = world_get_block(w, x, y, z) & 4095;
                if (id == 8 || id == 9)
                {
                    int meta = world_get_meta(w, x, y, z);
                    float h_meta = (meta >= 8 ? 0 : meta) + 1;
                    double h = (double)(y + 1) - (double)h_meta / 9.0;
                    if ((double)y1 >= h) return true;
                }
            }
        }
    }
    return false;
}

void clientworld_tick_input(struct clientworld *cw, int dig_count)
{
    struct texanim_world w = {.world = 1, .surface = cw->surface_world};
    texanim_dials_tick(&cw->dials, 64, 32, &w, cw->det);
    /* the dig particles sit at the player, always inside the 16-block gate */
    for (int i = 0; i < dig_count; ++i)
        spawn_particle_draws(cw, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 5);
    renderstate_torch_flicker(&cw->flicker, cw->det);
}

void clientworld_tick(struct clientworld *cw, double player_x, double player_y, double player_z,
                      double prev_px, double prev_py, double prev_pz, bool is_sprinting,
                      bool (*has_chunk)(int cx, int cz))
{
    /* 2. Player sprinting particle (Entity.onEntityUpdate uses pre-tick pos) */
    if (is_sprinting && !cw->player_in_water)
    {
        int fpx = mh_floor(prev_px);
        int fpy = mh_floor(prev_py - 0.20000000298023224 - 1.6200000047683716);
        int fpz = mh_floor(prev_pz);
        int under_id = world_get_block(cw->world, fpx, fpy, fpz);
        if (under_id != 0)
        {
            spawn_particle_draws(cw, prev_px, prev_py, prev_pz, prev_px, prev_py, prev_pz, 5);
        }
    }

    /* 3. Player handleWaterMovement */
    bool touches_water = player_touches_water(cw->world, player_x, player_y, player_z);

    if (touches_water)
    {
        if (!cw->player_in_water)
        {
            for (int i = 0; i < 13; ++i)
            {
                spawn_particle_draws(cw, player_x, player_y, player_z, player_x, player_y, player_z, 9);
            }
            for (int i = 0; i < 13; ++i)
            {
                spawn_particle_draws(cw, player_x, player_y, player_z, player_x, player_y, player_z, 7);
            }
        }
        cw->player_in_water = true;
    }
    else
    {
        cw->player_in_water = false;
    }

    /* 2. setActivePlayerChunksAndCheckLight (4 draws) */
    jr_int_n(&cw->rand, 1);
    jr_int_n(&cw->rand, 11);
    jr_int_n(&cw->rand, 11);
    jr_int_n(&cw->rand, 11);

    /* 3. doVoidFogParticles */
    det_rng var5 = det_new_random_role(cw->det, DET_CLIENT);
    int px = mh_floor(player_x);
    int py = mh_floor(player_y);
    int pz = mh_floor(player_z);

    for (int i = 0; i < 1000; ++i)
    {
        int rx1 = jr_int_n(&cw->rand, 16);
        int rx2 = jr_int_n(&cw->rand, 16);
        int ry1 = jr_int_n(&cw->rand, 16);
        int ry2 = jr_int_n(&cw->rand, 16);
        int rz1 = jr_int_n(&cw->rand, 16);
        int rz2 = jr_int_n(&cw->rand, 16);
        int bx = px + rx1 - rx2;
        int by = py + ry1 - ry2;
        int bz = pz + rz1 - rz2;
        bool has_ch = has_chunk(bx >> 4, bz >> 4);
        int raw_id = world_get_block(cw->world, bx, by, bz);
        int id = has_ch ? raw_id : 0;
        if (id == 0)
        {
            int r8 = jr_int_n(&cw->rand, 8);
            if (r8 > by && cw->has_void_particles)
            {
                jr_float(&cw->rand);
                jr_float(&cw->rand);
                jr_float(&cw->rand);
            }
        }
        else
        {
            clientworld_random_display_tick(cw, id, bx, by, bz, &var5, player_x, player_y, player_z);
        }
    }
}

uint64_t clientworld_cw(const struct clientworld *cw)
{
    return (uint64_t)((int64_t)cw->rand.seed * 31LL + (int64_t)cw->update_lcg);
}

/* ------------------------------------------------ the client tile entities */

static int client_te_kind(int id)
{
    return id == 23 || id == 158 || id == 116 || id == 52 || id == 54 || id == 146 || id == 130;
}

/* the entity at x, y, z, -1 for none */
static int client_te_at(const struct client_tes *t, int x, int y, int z)
{
    for (int i = 0; i < t->n; ++i)
        if (t->v[i].x == x && t->v[i].y == y && t->v[i].z == z) return i;
    return -1;
}

/* loadedTileEntityList's removal keeps the others' order */
static void client_te_drop(struct client_tes *t, int at)
{
    memmove(&t->v[at], &t->v[at + 1], (size_t)(t->n - at - 1) * sizeof t->v[0]);
    --t->n;
}

static struct client_te *client_te_make(struct client_tes *t, det_state *det, int x, int y, int z, int id)
{
    if (t->n == CLIENT_TE_MAX) return NULL;
    struct client_te *e = &t->v[t->n++];
    memset(e, 0, sizeof *e);
    e->x = x;
    e->y = y;
    e->z = z;
    e->id = id;
    /* TileEntityDispenser's field_146021_j = new Random() */
    if ((id == 23 || id == 158) && det != NULL) (void)det_new_random_role(det, DET_CLIENT);
    return e;
}

void client_te_cell(struct client_tes *t, det_state *det, int x, int y, int z, int old_id, int old_meta, int id,
                    int meta)
{
    old_id &= 4095;
    id &= 4095;
    /* func_150807_a returns at once when nothing changes */
    if (old_id == id && old_meta == meta) return;
    int at = client_te_at(t, x, y, z);
    /* the old block's entity leaves when the block changes */
    if (at >= 0 && old_id != id)
    {
        client_te_drop(t, at);
        at = -1;
    }
    /* func_150806_e makes the new block's when the map has none */
    if (!client_te_kind(id) || at >= 0) return;
    (void)client_te_make(t, det, x, y, z, id);
}

struct client_te *client_te_get(struct client_tes *t, det_state *det, int x, int y, int z, int id)
{
    int at = client_te_at(t, x, y, z);
    if (at >= 0) return &t->v[at];
    if (!client_te_kind(id & 4095)) return NULL;
    return client_te_make(t, det, x, y, z, id & 4095);
}

void client_te_chunk_gone(struct client_tes *t, int cx, int cz)
{
    for (int i = 0; i < t->n; )
        if ((t->v[i].x >> 4) == cx && (t->v[i].z >> 4) == cz) client_te_drop(t, i);
        else ++i;
}

/* TileEntityChest.updateEntity on the client: the lid follows
 * numPlayersUsing, and the half with no chest of its kind at z-1 or x-1
 * plays the open and close sounds */
static void client_chest_tick(struct client_te *e, struct world *w, det_rng *wr)
{
    int id = world_get_block(w, e->x, e->y, e->z) & 4095;
    int lead = (world_get_block(w, e->x, e->y, e->z - 1) & 4095) != id &&
               (world_get_block(w, e->x - 1, e->y, e->z) & 4095) != id;
    e->prev_lid = e->lid;
    if (e->players > 0 && e->lid == 0.0F && lead && wr) (void)det_rng_float(wr);   /* random.chestopen */
    if ((e->players == 0 && e->lid > 0.0F) || (e->players > 0 && e->lid < 1.0F))
    {
        float var8 = e->lid;
        if (e->players > 0) e->lid += 0.1F;
        else e->lid -= 0.1F;
        if (e->lid > 1.0F) e->lid = 1.0F;
        if (e->lid < 0.5F && var8 >= 0.5F && lead && wr) (void)det_rng_float(wr);   /* random.chestclosed */
        if (e->lid < 0.0F) e->lid = 0.0F;
    }
}

/* TileEntityEnderChest.updateEntity on the client (its 20-tick block event
 * is the client's own and changes nothing): the lid and both sounds, with
 * no lead check */
static void client_ender_tick(struct client_te *e, det_rng *wr)
{
    e->prev_lid = e->lid;
    if (e->players > 0 && e->lid == 0.0F && wr) (void)det_rng_float(wr);   /* random.chestopen */
    if ((e->players == 0 && e->lid > 0.0F) || (e->players > 0 && e->lid < 1.0F))
    {
        float var8 = e->lid;
        if (e->players > 0) e->lid += 0.1F;
        else e->lid -= 0.1F;
        if (e->lid > 1.0F) e->lid = 1.0F;
        if (e->lid < 0.5F && var8 >= 0.5F && wr) (void)det_rng_float(wr);   /* random.chestclosed */
        if (e->lid < 0.0F) e->lid = 0.0F;
    }
}

void client_te_tick(struct client_tes *t, det_state *det, struct world *w, det_rng *wr, struct particles_live *pl,
                    double px, double py, double pz)
{
    for (int i = 0; i < t->n; ++i)
    {
        struct client_te *e = &t->v[i];
        if (e->id == 54 || e->id == 146)
        {
            if (w) client_chest_tick(e, w, wr);
            continue;
        }
        if (e->id == 130)
        {
            client_ender_tick(e, wr);
            continue;
        }
        /* World.getClosestPlayer(x + 0.5, y + 0.5, z + 0.5, range): the
         * client player is the world's only player */
        double dx = px - (double)((float)e->x + 0.5F);
        double dy = py - (double)((float)e->y + 0.5F);
        double dz = pz - (double)((float)e->z + 0.5F);
        double d2 = dx * dx + dy * dy + dz * dz;
        if (e->id == 52)
        {
            /* MobSpawnerBaseLogic.updateSpawner's client half, canRun within
             * 16 (the delay and the spin draw nothing) */
            if (!(d2 < 256.0) || wr == NULL) continue;
            double x = (double)((float)e->x + det_rng_float(wr));
            double y = (double)((float)e->y + det_rng_float(wr));
            double z = (double)((float)e->z + det_rng_float(wr));
            if (pl)
            {
                (void)particles_live_spawn(pl, "smoke", x, y, z, 0.0, 0.0, 0.0);
                (void)particles_live_spawn(pl, "flame", x, y, z, 0.0, 0.0, 0.0);
            }
            continue;
        }
        if (e->id != 116) continue;
        int near = d2 < 9.0;
        tileticks_enchant_step(&e->ench, e->x, e->z, near, px, pz, det, DET_CLIENT);
    }
}
