/* The player riding a saddled pig (lane/ride). See riding.h. */

#include "riding.h"
#include "env.h"

#include <math.h>
#include <string.h>

#include "blocks.h"
#include "clientworld.h"
#include "combat.h"
#include "entity.h"
#include "items.h"
#include "jmath.h"
#include "living.h"
#include "pathfind.h"
#include "place.h"
#include "player.h"
#include "serverreplay.h"
#include "survival.h"
#include "world.h"

enum { ITEM_SADDLE = 329, ITEM_CARROT_ON_A_STICK = 398, ITEM_FISHING_ROD = 346 };

/* EntityAIControlledByPlayer's maxSpeed, EntityPig's constructor argument. */
#define PIG_MAX_SPEED 0.3F

static float wrap_angle(float a)
{
    a = fmodf(a, 360.0F);
    if (a >= 180.0F) a -= 360.0F;
    if (a < -180.0F) a += 360.0F;
    return a;
}

static int ceil_float(float f)
{
    int i = (int)f;
    return f > (float)i ? i + 1 : i;
}

static float abs_float(float f)
{
    return f >= 0.0F ? f : -f;
}

/* the rider as an EntityPlayer, NULL for none or a mob rider */
static struct server_player *rider_player(struct living *l)
{
    struct living *r = lv_get(l->ridden_by_entity);
    return r != NULL && r->player_mp ? r->player_sp : NULL;
}

static struct surv_stack *held_stack(struct server_player *p)
{
    int slot = p->sv.current_item;
    if (slot < 0 || slot >= 9) return NULL;
    return p->sv.inv[slot].count > 0 ? &p->sv.inv[slot] : NULL;
}

/* EntityPig.canBeSteered: the rider holds a carrot on a stick */
static int pig_can_be_steered(struct server_player *p)
{
    const struct surv_stack *h = held_stack(p);
    return h != NULL && h->item == ITEM_CARROT_ON_A_STICK;
}

static struct ai_task *controlled_task(struct living *pig)
{
    for (int i = 0; i < lv_ai(pig)->tasks.n; ++i)
        if (lv_ai(pig)->tasks.entries[i].t.cls == AIC_CONTROLLED_BY_PLAYER) return &lv_ai(pig)->tasks.entries[i].t;
    return NULL;
}

int ride_ai_should_execute(struct living *l, struct ai_task *t)
{
    struct server_player *p = rider_player(l);
    return living_is_alive(l) && p != NULL && (t->ctl_speed_boosted || pig_can_be_steered(p));
}

/* EntityAIControlledByPlayer.func_151498_a: stairs (render type 10) or a
 * BlockSlab (the stone and wooden slabs, single and double). */
static int stair_or_slab(int id)
{
    return BLOCKS[id].render_type == 10 || id == 43 || id == 44 || id == 125 || id == 126;
}

/* The fishing rod the carrot on a stick leaves when it breaks: a new stack
 * with the old one's tag (its enchantments). */
static void carrot_to_rod(struct server_player *p, struct surv_stack *st)
{
    struct surv_stack rod = *st;
    rod.item = ITEM_FISHING_ROD;
    rod.damage = 0;
    rod.count = 1;
    rod.gen = ++p->sv.gen_counter;
    *st = rod;
}

void ride_ai_update(struct living *l, struct ai_task *t, det_state *det)
{
    struct server_player *var1 = rider_player(l);
    if (var1 == NULL) return;
    struct world *w = l->world;

    float var3 = wrap_angle(var1->rotation_yaw - l->rotation_yaw) * 0.5F;
    if (var3 > 5.0F) var3 = 5.0F;
    if (var3 < -5.0F) var3 = -5.0F;
    l->rotation_yaw = wrap_angle(l->rotation_yaw + var3);

    if (t->ctl_current_speed < PIG_MAX_SPEED)
        t->ctl_current_speed += (PIG_MAX_SPEED - t->ctl_current_speed) * 0.01F;
    if (t->ctl_current_speed > PIG_MAX_SPEED) t->ctl_current_speed = PIG_MAX_SPEED;

    int var4 = mh_floor(l->e.pos_x);
    int var5 = mh_floor(l->e.pos_y);
    int var6 = mh_floor(l->e.pos_z);
    float var7 = t->ctl_current_speed;

    if (t->ctl_speed_boosted)
    {
        if (t->ctl_speed_boost_time++ > t->ctl_max_speed_boost_time) t->ctl_speed_boosted = 0;
        var7 += var7 * 1.15F * mh_sin((float)t->ctl_speed_boost_time / (float)t->ctl_max_speed_boost_time *
                                      3.1415927F);
    }

    float var8 = 0.91F;
    if (l->e.on_ground)
        var8 = BLOCKS[world_get_block(w, mh_floor_float((float)var4), mh_floor_float((float)var5) - 1,
                                      mh_floor_float((float)var6)) & 4095].slipperiness * 0.91F;

    float var9 = 0.16277136F / (var8 * var8 * var8);
    float var10 = mh_sin(l->rotation_yaw * 3.1415927F / 180.0F);
    float var11 = mh_cos(l->rotation_yaw * 3.1415927F / 180.0F);
    float var12 = living_get_ai_move_speed(l) * var9;
    float var13 = var7 > 1.0F ? var7 : 1.0F;
    var13 = var12 / var13;
    float var14 = var7 * var13;
    float var15 = -(var14 * var10);
    float var16 = var14 * var11;

    if (abs_float(var15) > abs_float(var16))
    {
        if (var15 < 0.0F) var15 -= l->e.width / 2.0F;
        if (var15 > 0.0F) var15 += l->e.width / 2.0F;
        var16 = 0.0F;
    }
    else
    {
        var15 = 0.0F;
        if (var16 < 0.0F) var16 -= l->e.width / 2.0F;
        if (var16 > 0.0F) var16 += l->e.width / 2.0F;
    }

    int var17 = mh_floor(l->e.pos_x + (double)var15);
    int var18 = mh_floor(l->e.pos_z + (double)var16);
    /* the size point: the pig with the player (height 1.8F) on it */
    int sx = mh_floor_float(l->e.width + 1.0F);
    int sy = mh_floor_float(l->e.height + var1->e.height + 1.0F);
    int sz = mh_floor_float(l->e.width + 1.0F);

    if (var4 != var17 || var6 != var18)
    {
        int var20 = world_get_block(w, var4, var5, var6) & 4095;
        int var21 = !stair_or_slab(var20) &&
                    (BLOCKS[var20].material != 0 || !stair_or_slab(world_get_block(w, var4, var5 - 1, var6) & 4095));

        if (var21 &&
            pf_vertical_offset_at(w, l->e.pos_x, l->e.pos_y, l->e.pos_z, l->e.bounding_box,
                                  var17, var5, var18, sx, sy, sz, 0, 0, 1) == 0 &&
            pf_vertical_offset_at(w, l->e.pos_x, l->e.pos_y, l->e.pos_z, l->e.bounding_box,
                                  var4, var5 + 1, var6, sx, sy, sz, 0, 0, 1) == 1 &&
            pf_vertical_offset_at(w, l->e.pos_x, l->e.pos_y, l->e.pos_z, l->e.bounding_box,
                                  var17, var5 + 1, var18, sx, sy, sz, 0, 0, 1) == 1)
            l->jump.is_jumping = 1;
    }

    /* the rider is never in creative; the draw is last in the && chain */
    if (t->ctl_current_speed >= PIG_MAX_SPEED * 0.5F && det_rng_float(&l->rand) < 0.006F &&
        !t->ctl_speed_boosted)
    {
        struct surv_stack *var22 = held_stack(var1);

        if (var22 != NULL && var22->item == ITEM_CARROT_ON_A_STICK)
        {
            (void)surv_damage_stack(var1, var22, 1);
            if (var22->count == 0) carrot_to_rod(var1, var22);
        }
    }

    living_move_entity_with_heading(l, 0.0F, var7, det);
}

int ride_carrot_right_click(struct server_player *p)
{
    struct surv_stack *cur = held_stack(p);
    if (cur == NULL || cur->item != ITEM_CARROT_ON_A_STICK) return 0;

    struct living *pig = lv_get(p->ridingh);
    if (pig == NULL || pig->kind != AK_PIG) return 1;
    struct ai_task *t = controlled_task(pig);
    if (t == NULL) return 1;

    /* isControlledByPlayer, and 7 uses left */
    if (!t->ctl_speed_boosted && t->ctl_current_speed > PIG_MAX_SPEED * 0.3F &&
        ITEMS[cur->item].max_damage - cur->damage >= 7)
    {
        /* boostSpeed */
        t->ctl_speed_boosted = 1;
        t->ctl_speed_boost_time = 0;
        t->ctl_max_speed_boost_time = det_rng_int_n(&pig->rand, 841) + 140;
        (void)surv_damage_stack(p, cur, 7);

        /* tryUseItem stores the returned stack: the fishing rod for a broken
         * one */
        if (cur->count == 0) carrot_to_rod(p, cur);
    }
    return 1;
}

int ride_saddle_interact(struct surv_stack *held, struct living *pig, int mutate)
{
    if (held == NULL || held->count <= 0 || held->item != ITEM_SADDLE) return 0;
    if (pig == NULL || pig->kind != AK_PIG) return 0;

    if (!(pig->data_watcher_16 & 1) && pig->growing_age >= 0)
    {
        /* setSaddled(true); the sound draws nothing */
        if (mutate) pig->data_watcher_16 = 1;
        --held->count;
    }
    return 1;
}

/* The player's packets leave through playerNetServerHandler at once; the
 * harness's queue is this tick's once the world pass has opened it. */
static void open_s2c(struct server_player *p)
{
    if (!p->dev_prequeued && !p->net_phase)
    {
        s2c_clear();
        p->dev_prequeued = 1;
    }
}

static void push_s1b(struct server_player *p, int vehicle_id)
{
    struct s2c_pkt *pkt = s2c_add(s2c_out());
    pkt->kind = PK_S1B;
    pkt->i0 = vehicle_id;
}

int ride_pig_interact(struct server_player *p, struct living *pig)
{
    struct living *me = p->replay != NULL ? serverreplay_player_living(p->replay) : NULL;
    if (me == NULL || pig->kind != AK_PIG || !(pig->data_watcher_16 & 1)) return 0;
    if (lv_get(pig->ridden_by_entity) != NULL && lv_get(pig->ridden_by_entity) != me) return 0;

    /* EntityPlayerMP.mountEntity(pig): Entity.mountEntity's links (the
     * cycle test cannot fire: a pig rides nothing), the S1B, and
     * setPlayerLocation at the unchanged position */
    if (p->ridingh != 0) lv_get(p->ridingh)->ridden_by_entity = 0;
    p->ridingh = lv_ref(pig);
    me->riding_entity = lv_ref(pig);
    pig->ridden_by_entity = lv_ref(me);
    open_s2c(p);
    push_s1b(p, pig->entity_id);
    serverreplay_set_player_location(p, p->e.pos_x, p->e.pos_y, p->e.pos_z, p->rotation_yaw, p->rotation_pitch);
    return 1;
}

static void unlink(struct server_player *p)
{
    struct living *me = p->replay != NULL ? serverreplay_player_living(p->replay) : NULL;
    if (p->ridingh != 0 && lv_get(lv_get(p->ridingh)->ridden_by_entity) == me) lv_get(p->ridingh)->ridden_by_entity = 0;
    if (me != NULL) me->riding_entity = 0;
    p->ridingh = 0;
}

void ride_server_dismount(struct server_player *p)
{
    struct world *w = p->e.world;
    open_s2c(p);

    if (p->ridingh != 0)
    {
        /* EntityLivingBase.dismountEntity */
        struct living *v = lv_get(p->ridingh);
        double var3 = v->e.pos_x;
        double var5 = v->e.bounding_box.min_y + (double)v->e.height;
        double var7 = v->e.pos_z;
        int done = 0;

        for (int a = -1; a <= 1 && !done; ++a)
        {
            for (int b = -1; b < 1; ++b)
            {
                if (a == 0 && b == 0) continue;

                int var12 = (int)(p->e.pos_x + (double)a);
                int var13 = (int)(p->e.pos_z + (double)b);
                struct aabb box = aabb_offset_box(p->e.bounding_box, (double)a, 1.0, (double)b);

                if (!world_colliding_boxes_empty(w, box)) continue;

                if (place_solid_top_surface(w, var12, (int)p->e.pos_y, var13))
                {
                    serverreplay_set_player_location(p, p->e.pos_x + (double)a, p->e.pos_y + 1.0,
                                                     p->e.pos_z + (double)b, p->rotation_yaw, p->rotation_pitch);
                    done = 1;
                    break;
                }

                int below = world_get_block(w, var12, (int)p->e.pos_y - 1, var13) & 4095;
                if (place_solid_top_surface(w, var12, (int)p->e.pos_y - 1, var13) || below == 8 || below == 9)
                {
                    var3 = p->e.pos_x + (double)a;
                    var5 = p->e.pos_y + 1.0;
                    var7 = p->e.pos_z + (double)b;
                }
            }
        }

        if (!done) serverreplay_set_player_location(p, var3, var5, var7, p->rotation_yaw, p->rotation_pitch);
        unlink(p);
    }

    push_s1b(p, -1);
    serverreplay_set_player_location(p, p->e.pos_x, p->e.pos_y, p->e.pos_z, p->rotation_yaw, p->rotation_pitch);
}

void ride_server_rider_position(struct server_player *p)
{
    struct living *v = lv_get(p->ridingh);
    if (v == NULL) return;
    entity_set_position(&p->e, v->e.pos_x,
                        v->e.pos_y + (double)v->e.height * 0.75 + (double)(p->e.y_offset - 0.5F),
                        v->e.pos_z);
}

void ride_server_update_entity(struct server_player *p)
{
    struct serverreplay *sr = p->replay;
    if (sr == NULL || p->sv.removed) return;

    /* updateEntityWithOptionalForce's head */
    p->prev_rotation_yaw = p->rotation_yaw;
    p->prev_rotation_pitch = p->rotation_pitch;

    if (p->ridingh == 0)
    {
        serverreplay_player_on_update(sr, serverreplay_player_world(sr));
        return;
    }

    /* EntityPlayer.updateRidden: the sneaking rider steps off */
    if (p->sneaking)
    {
        ride_server_dismount(p);
        p->sneaking = 0;
        return;
    }

    double x0 = p->e.pos_x, y0 = p->e.pos_y, z0 = p->e.pos_z;
    float yaw0 = p->rotation_yaw, pitch0 = p->rotation_pitch;

    /* Entity.updateRidden */
    if (lv_get(p->ridingh)->is_dead) unlink(p);
    else
    {
        p->e.motion_x = p->e.motion_y = p->e.motion_z = 0.0;
        /* the twin the entity pass pushes and copies back is the same
         * object: its motion zeroes too */
        if (sr->player_livh != 0)
        {
            struct living *tw = lv_get(sr->player_livh);
            tw->e.motion_x = tw->e.motion_y = tw->e.motion_z = 0.0;
        }
        serverreplay_player_on_update(sr, serverreplay_player_world(sr));
        if (p->ridingh != 0) ride_server_rider_position(p);
    }

    /* EntityLivingBase.updateRidden */
    p->e.fall_distance = 0.0F;

    /* EntityPlayer.updateRidden: addMountedMovementStat, the pig keeps the
     * rider's look */
    if (p->ridingh != 0)
    {
        double dx = p->e.pos_x - x0, dy = p->e.pos_y - y0, dz = p->e.pos_z - z0;
        int var7 = (int)lroundf((float)sqrt(dx * dx + dy * dy + dz * dz) * 100.0F);
        if (var7 > 0 && lv_get(p->ridingh)->kind == AK_PIG) surv_add_stat(p, STAT_DISTANCE_PIG, var7);
        if (lv_get(p->ridingh)->kind == AK_PIG)
        {
            p->rotation_pitch = pitch0;
            p->rotation_yaw = yaw0;
        }
    }
}

void ride_server_process_input(struct server_player *p, float strafe, float forward, int jump, int sneak)
{
    if (p->ridingh == 0) return;
    if (strafe >= -1.0F && strafe <= 1.0F) p->move_strafing = strafe;
    if (forward >= -1.0F && forward <= 1.0F) p->move_forward = forward;
    p->is_jumping = jump;
    p->sneaking = sneak;
}

struct aabb ride_server_pickup_box(const struct server_player *p)
{
    if (p->ridingh != 0 && !lv_get(p->ridingh)->is_dead)
    {
        const struct aabb *a = &p->e.bounding_box, *b = &lv_get(p->ridingh)->e.bounding_box;
        struct aabb u = aabb_make(fmin(a->min_x, b->min_x), fmin(a->min_y, b->min_y), fmin(a->min_z, b->min_z),
                                  fmax(a->max_x, b->max_x), fmax(a->max_y, b->max_y), fmax(a->max_z, b->max_z));
        return aabb_expand(u, 1.0, 0.0, 1.0);
    }
    return aabb_expand(p->e.bounding_box, 1.0, 0.5, 1.0);
}

void ride_player_fall(struct living *vehicle, float distance)
{
    struct server_player *p = rider_player(vehicle);
    if (p == NULL) return;

    /* EntityPlayer.fall: the distanceFallen stat, then EntityLivingBase.fall's
     * damage */
    if (distance >= 2.0F)
    {
        open_s2c(p);
        surv_add_stat(p, STAT_DISTANCE_FALLEN, (int)(long long)floor((double)distance * 100.0 + 0.5));
    }
    int var4 = ceil_float(distance - 3.0F);
    if (var4 <= 0) return;

    float before = p->sv.health;
    open_s2c(p);
    surv_server_damage(p, SURV_FALL, (float)var4);
    struct living *me = p->replay != NULL ? serverreplay_player_living(p->replay) : NULL;
    if (me != NULL)
    {
        me->health = p->sv.health;
        me->is_dead = p->sv.dead;
    }
    /* the tracker pass's S1C of the changed health (data watcher 6) */
    if (p->sv.health != before)
    {
        struct s2c_pkt *pkt = s2c_add(s2c_out());
        pkt->kind = PK_S1C;
        pkt->f0 = p->sv.health;
    }
}

void ride_pig_fly(struct living *pig)
{
    struct server_player *p = rider_player(pig);
    if (p == NULL) return;
    /* triggerAchievement -> EntityPlayerMP.addStat (surv_add_stat flushes) */
    open_s2c(p);
    surv_add_stat(p, ACH_FLY_PIG, 1);
}

/* ------------------------------------------------------------- the client */

static struct client_entity *client_vehicle(struct client_player *p, int id)
{
    if (id <= 0 || p->combat == NULL) return NULL;
    return clientworld_get_entity(&p->combat->client, id);
}

void ride_client_s1b(struct client_player *p, int vehicle_id)
{
    /* NetHandlerPlayClient.handleEntityAttach for the player: mountEntity
     * with the client's copy, null when the id names none (the client's
     * EntityPlayer.mountEntity(null) only drops the links) */
    struct client_entity *v = client_vehicle(p, vehicle_id);
    p->riding_id = v != NULL ? vehicle_id : 0;
}

int ride_client_riding(struct client_player *p)
{
    if (p->riding_id == 0) return 0;
    struct client_entity *v = client_vehicle(p, p->riding_id);
    /* World.updateEntities: a dead vehicle lets go of its rider, which then
     * updates at its own place */
    if (v == NULL || v->is_dead)
    {
        p->riding_id = 0;
        return 0;
    }
    return 1;
}

int ride_client_vehicle_id(const struct client_player *p)
{
    return p->riding_id;
}

void ride_client_rider_position(struct client_player *p)
{
    struct client_entity *v = client_vehicle(p, p->riding_id);
    if (v == NULL) return;
    entity_set_position(&p->e, v->x, v->y + (double)v->height * 0.75 + (double)(p->e.y_offset - 0.5F), v->z);
}
