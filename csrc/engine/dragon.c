#include "dragon.h"
#include "env.h"
#include "jmath.h"
#include "smath.h"
#include "world.h"
#include "blocks.h"
#include "seedworld.h"
#include "player.h"
#include "item_entity.h"

#include <math.h>
#include <string.h>
#include <float.h>

#define PI 3.14159265358979323846
#define PIF 3.1415927f

static float wrap_f(float a)
{
    a = fmodf(a, 360.0f);
    if (a >= 180.0f) a -= 360.0f;
    if (a < -180.0f) a += 360.0f;
    return a;
}

static double wrap_d(double a)
{
    a = fmod(a, 360.0);
    if (a >= 180.0) a -= 360.0;
    if (a < -180.0) a += 360.0;
    return a;
}

static double sqrt_float(double a) { return (double)(float)sqrt(a); }

static void offsets(const struct dragon_state *d, int n, double out[3])
{
    int i = (d->ring_index - n) & 63;
    out[0] = d->ring[i][0];
    out[1] = d->ring[i][1];
    out[2] = d->ring[i][2];
}

static void set_part(struct dragon_part_state *p, double x, double y, double z)
{
    p->x = x;
    p->y = y;
    p->z = z;
}

static int player_intersects(const struct dragon_state *d, const struct dragon_part_state *p,
                             double ex, double ey, double ez, double oy)
{
    double half = (double)(p->width / 2.0f);
    double phalf = (double)(0.6f / 2.0f);
    double pmin_x = d->player_x - phalf, pmax_x = d->player_x + phalf;
    double pmin_z = d->player_z - phalf, pmax_z = d->player_z + phalf;
    double pmax_y = d->player_min_y + (double)1.8f;
    return pmax_x > p->x-half-ex && pmin_x < p->x+half+ex &&
           pmax_y > p->y-ey+oy && d->player_min_y < p->y+(double)p->height+ey+oy &&
           pmax_z > p->z-half-ez && pmin_z < p->z+half+ez;
}

static void player_wing_collision(struct dragon_state *d)
{
    double half = (double)(d->part[1].width / 2.0f);
    double cx = ((d->part[1].x-half) + (d->part[1].x+half)) / 2.0;
    double cz = ((d->part[1].z-half) + (d->part[1].z+half)) / 2.0;
    double dx = d->player_x - cx, dz = d->player_z - cz;
    double dist = dx*dx + dz*dz;
    d->player_mx += dx / dist * 4.0;
    d->player_my += 0.20000000298023224;
    d->player_mz += dz / dist * 4.0;
}

static void enchantment_damage_roll(struct dragon_state *d)
{
    if (!d->det) return;
    struct det_split *sp=det_split_find(d->det,"./net/minecraft/enchantment/EnchantmentHelper.java:enchantmentRand");
    if (!sp) sp=det_split_random(d->det,"./net/minecraft/enchantment/EnchantmentHelper.java:enchantmentRand");
    (void)det_split_int_n(d->det,sp,1);
}

static double random_knockback_offset(det_state *det)
{
    int role=det_role(det);
    double a=det_math_random_role(det,role);
    double b=det_math_random_role(det,role);
    return (a-b)*0.01;
}

static void player_bite(struct dragon_state *d)
{
    if (d->player_health <= 0.0f || ((float)d->player_hurt_resistant_time > 10.0f && 10.0f <= d->player_last_damage)) return;
    float amount = 10.0f;
    int full_hurt=1;
    if (d->player_hurt_resistant_time > 10) {
        amount -= d->player_last_damage;
        full_hurt=0;
    } else {
        d->player_hurt_resistant_time = 20;
        d->player_hurt_time = 10;
    }
    d->player_last_damage = 10.0f;
    enchantment_damage_roll(d);
    d->player_health -= amount;
    if (d->player_health < 0.0f) d->player_health=0.0f;
    if (!full_hurt) return;
    /* EntityLivingBase.setBeenAttacked, then knockBack against the dragon. */
    (void)det_rng_double(&d->player_rand);
    (void)det_rng_double(&d->player_rand);
    double dx = d->x - d->player_x, dz = d->z - d->player_z;
    while (dx*dx+dz*dz<1.0e-4 && d->det) {
        dx=random_knockback_offset(d->det);
        dz=random_knockback_offset(d->det);
    }
    float len = (float)sqrt(dx*dx + dz*dz);
    d->player_mx /= 2.0; d->player_my /= 2.0; d->player_mz /= 2.0;
    d->player_mx -= dx / (double)len * 0.4000000059604645;
    d->player_my += 0.4000000059604645;
    d->player_mz -= dz / (double)len * 0.4000000059604645;
    if (d->player_my > 0.4000000059604645) d->player_my = 0.4000000059604645;
    (void)det_rng_float(&d->player_rand);
    (void)det_rng_float(&d->player_rand);
}

static void set_target(struct dragon_state *d)
{
    d->force_target = 0;
    if (det_rng_int_n(&d->rand, 2) == 0 && d->has_player) {
        (void)det_rng_int_n(&d->rand, 1);
        d->has_target = 1;
        return;
    }
    do {
        d->tx = 0.0;
        d->ty = (double)(70.0f + det_rng_float(&d->rand) * 50.0f);
        d->tz = 0.0;
        d->tx += (double)(det_rng_float(&d->rand) * 120.0f - 60.0f);
        d->tz += (double)(det_rng_float(&d->rand) * 120.0f - 60.0f);
        double x = d->x - d->tx, y = d->y - d->ty, z = d->z - d->tz;
        if (x*x + y*y + z*z > 100.0) break;
    } while (1);
    d->has_target = 0;
}

static void move_flying(struct dragon_state *d, float speed)
{
    float s = mh_sin(d->yaw * PIF / 180.0f);
    float c = mh_cos(d->yaw * PIF / 180.0f);
    d->mx += (double)(speed * s);
    d->mz += (double)(-speed * c);
}

static void update_parts(struct dragon_state *d)
{
    double o5[3], o10[3], o0[3];
    offsets(d, 5, o5); offsets(d, 10, o10); offsets(d, 0, o0);
    float v2 = (float)(o5[1] - o10[1]) * 10.0f / 180.0f * PIF;
    float co = mh_cos(v2), si = -mh_sin(v2);
    float a = d->yaw * PIF / 180.0f;
    float sn = mh_sin(a), cs = mh_cos(a);
    d->part[0].width = d->part[0].height = 3.0f;
    d->part[1].width = 5.0f; d->part[1].height = 3.0f;
    for (int i = 2; i <= 4; ++i) d->part[i].width = d->part[i].height = 2.0f;
    d->part[5].width = 4.0f; d->part[5].height = 2.0f;
    d->part[6].width = 4.0f; d->part[6].height = 3.0f;
    set_part(&d->part[1], d->x + (double)(sn * 0.5f), d->y, d->z - (double)(cs * 0.5f));
    set_part(&d->part[5], d->x + (double)(cs * 4.5f), d->y + 2.0, d->z + (double)(sn * 4.5f));
    set_part(&d->part[6], d->x - (double)(cs * 4.5f), d->y + 2.0, d->z - (double)(sn * 4.5f));
    if (d->wing_push != NULL && d->hurt_time == 0) {
        /* the parts' boxes: width across, height up from their feet; the
         * wings' grown by (4, 2, 4) and lowered 2, the head's grown by 1 */
        double half = (double)(d->part[1].width / 2.0f);
        double cx = ((d->part[1].x-half) + (d->part[1].x+half)) / 2.0;
        double cz = ((d->part[1].z-half) + (d->part[1].z+half)) / 2.0;
        for (int w = 5; w <= 6; ++w) {
            const struct dragon_part_state *p = &d->part[w];
            double h = (double)(p->width / 2.0f);
            double box[6] = {p->x-h-4.0, p->y-2.0-2.0, p->z-h-4.0,
                             p->x+h+4.0, p->y+(double)p->height+2.0-2.0, p->z+h+4.0};
            d->wing_push(d->world_ctx, box, cx, cz);
        }
        const struct dragon_part_state *p = &d->part[0];
        double h = (double)(p->width / 2.0f);
        double box[6] = {p->x-h-1.0, p->y-1.0, p->z-h-1.0, p->x+h+1.0, p->y+(double)p->height+1.0, p->z+h+1.0};
        d->head_bite(d->world_ctx, box);
    }
    else if (d->has_player && d->hurt_time == 0) {
        if (player_intersects(d, &d->part[5], 4.0, 2.0, 4.0, -2.0)) player_wing_collision(d);
        if (player_intersects(d, &d->part[6], 4.0, 2.0, 4.0, -2.0)) player_wing_collision(d);
        if (player_intersects(d, &d->part[0], 1.0, 1.0, 1.0, 0.0)) player_bite(d);
    }
    float hs = mh_sin(d->yaw * PIF / 180.0f - d->yaw_velocity * 0.01f);
    float hc = mh_cos(d->yaw * PIF / 180.0f - d->yaw_velocity * 0.01f);
    set_part(&d->part[0], d->x + (double)(hs * 5.5f * co),
             d->y + (o0[1] - o5[1]) * 1.0 + (double)(si * 5.5f),
             d->z - (double)(hc * 5.5f * co));
    for (int i = 0; i < 3; ++i) {
        double o[3]; offsets(d, 12 + i * 2, o);
        float angle = d->yaw * PIF / 180.0f + (float)wrap_d(o[0] - o5[0]) * PIF / 180.0f * 1.0f;
        float s = mh_sin(angle), c = mh_cos(angle);
        float base = 1.5f, dist = (float)(i + 1) * 2.0f;
        set_part(&d->part[i+2],
                 d->x - (double)((sn * base + s * dist) * co),
                 d->y + (o[1] - o5[1]) * 1.0 - (double)((dist + base) * si) + 1.5,
                 d->z + (double)((cs * base + c * dist) * co));
    }
}

void dragon_update_parts(struct dragon_state *d)
{
    update_parts(d);
}

void dragon_client_tick(struct dragon_state *d, struct dragon_interp *ip)
{
    if (ip->health <= 0.0f)
    {
        /* EntityDragon.onDeathUpdate: moveEntity(0, 0.1, 0) through its
         * noClip box, then the turn; onLivingUpdate only spawns particles */
        ++d->death_ticks;
        d->bb_min_y += 0.10000000149011612;
        d->y = d->bb_min_y;
        d->render_yaw = d->yaw += 20.0f;
        return;
    }
    d->yaw = wrap_f(d->yaw);
    if (d->ring_index < 0)
        for (int i = 0; i < 64; ++i) {
            d->ring[i][0] = (double)d->yaw;
            d->ring[i][1] = d->y;
        }
    if (++d->ring_index == 64) d->ring_index = 0;
    d->ring[d->ring_index][0] = (double)d->yaw;
    d->ring[d->ring_index][1] = d->y;
    if (ip->incr > 0)
    {
        double x = d->x + (ip->new_x - d->x) / (double)ip->incr;
        double y = d->y + (ip->new_y - d->y) / (double)ip->incr;
        double z = d->z + (ip->new_z - d->z) / (double)ip->incr;
        double dyaw = wrap_d(ip->new_yaw - (double)d->yaw);
        d->yaw = (float)((double)d->yaw + dyaw / (double)ip->incr);
        ip->pitch = (float)((double)ip->pitch + (ip->new_pitch - (double)ip->pitch) / (double)ip->incr);
        --ip->incr;
        d->x = x; d->y = y; d->z = z;
        d->bb_min_x = x - 8.0; d->bb_max_x = x + 8.0;
        d->bb_min_y = y;
        d->bb_min_z = z - 8.0; d->bb_max_z = z + 8.0;
        d->yaw = fmodf(d->yaw, 360.0f);
        ip->pitch = fmodf(ip->pitch, 360.0f);
    }
    d->render_yaw = d->yaw;
    update_parts(d);
}

static int destroy_blocks(struct dragon_state *d, const struct dragon_part_state *p)
{
    if (!d->world) return 0;
    int x0 = mh_floor(p->x - (double)(p->width / 2.0f));
    int y0 = mh_floor(p->y);
    int z0 = mh_floor(p->z - (double)(p->width / 2.0f));
    int x1 = mh_floor(p->x + (double)(p->width / 2.0f));
    int y1 = mh_floor(p->y + (double)p->height);
    int z1 = mh_floor(p->z + (double)(p->width / 2.0f));
    int blocked = 0, broke = 0;
    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z) {
                int id = world_get_block(d->world, x, y, z) & 4095;
                if (BLOCKS[id].material == 0) continue;
                if (id == 49 || id == 121 || id == 7 || !d->mob_griefing) blocked = 1;
                else if (world_set_block(d->world, x, y, z, 0, 0, 3)) broke = 1;
            }
    if (broke) {
        (void)det_rng_float(&d->rand);
        (void)det_rng_float(&d->rand);
        (void)det_rng_float(&d->rand);
    }
    return blocked;
}

static void dragon_setup(struct dragon_state *d, struct world *w, det_state *det,
                         double x, double y, double z, float yaw)
{
    memset(d,0,sizeof *d);
    d->world=w; d->det=det;
    d->x=x; d->y=y; d->z=z; d->yaw=yaw;
    d->bb_min_x=x-8.0; d->bb_max_x=x+8.0;
    d->bb_min_y=y; d->bb_min_z=z-8.0; d->bb_max_z=z+8.0;
    d->health=200.0f; d->ty=100.0; d->ring_index=-1;
    d->mob_griefing=1;
    d->healing_crystal_index=-1;
    d->first_update=1;
    d->air=300;
    d->part[0].width=d->part[0].height=6.0f;
    d->part[1].width=d->part[1].height=8.0f;
    for (int i=2;i<7;++i) d->part[i].width=d->part[i].height=4.0f;
}

void dragon_init(struct dragon_state *d, struct world *w, det_state *det,
                 double x, double y, double z, float yaw)
{
    dragon_setup(d,w,det,x,y,z,yaw);
    int role=det_role(det);
    d->entity_id=det_next_entity_id_role(det,role);
    d->rand=det_new_random_role(det,role);
    det_uuid_role(det,role,&d->uuid_msb,&d->uuid_lsb);
    /* EntityLivingBase's three Math.random initializers. */
    for (int i=0;i<3;++i) (void)det_math_random_role(det,role);
    /* EntityDragonPart's seven Entity constructors, in array order. */
    for (int i=0;i<7;++i) {
        d->part[i].entity_id=det_next_entity_id_role(det,role);
        (void)det_new_random_role(det,role);
        int64_t msb,lsb;
        det_uuid_role(det,role,&msb,&lsb);
    }
}

void dragon_adopt(struct dragon_state *d, struct world *w, det_state *det,
                  const struct sw_entity *entity)
{
    dragon_setup(d,w,det,entity->x,entity->y,entity->z,entity->yaw);
    d->entity_id=entity->id;
    d->uuid_msb=entity->uuid_msb; d->uuid_lsb=entity->uuid_lsb;
    d->rand=entity->rand;
    for (int i=0;i<7;++i) d->part[i].entity_id=(int32_t)((uint32_t)entity->id+(uint32_t)(i+1));
}

int dragon_crystal_init(struct dragon_state *d, det_state *det,
                        double x, double y, double z)
{
    if (d->n_crystals >= DRAGON_MAX_CRYSTALS) return -1;
    int index=d->n_crystals++;
    struct dragon_crystal_state *c=&d->crystals[index];
    memset(c,0,sizeof *c);
    int role=det_role(det);
    c->entity_id=det_next_entity_id_role(det,role);
    c->rand=det_new_random_role(det,role);
    det_uuid_role(det,role,&c->uuid_msb,&c->uuid_lsb);
    c->inner_rotation=det_rng_int_n(&c->rand,100000);
    c->x=x; c->y=y; c->z=z; c->health=5; c->in_world=1;
    return index;
}

int dragon_crystal_adopt(struct dragon_state *d, const struct sw_entity *entity)
{
    if (d->n_crystals>=DRAGON_MAX_CRYSTALS) return -1;
    int index=d->n_crystals++;
    struct dragon_crystal_state *c=&d->crystals[index];
    memset(c,0,sizeof *c);
    c->entity_id=entity->id;
    c->uuid_msb=entity->uuid_msb; c->uuid_lsb=entity->uuid_lsb;
    c->x=entity->x; c->y=entity->y; c->z=entity->z;
    c->rand=entity->rand;
    c->inner_rotation=entity->crystal_rotation;
    c->health=5; c->in_world=1;
    return index;
}

void dragon_crystal_tick(struct dragon_state *d, int index)
{
    if (index<0 || index>=d->n_crystals) return;
    struct dragon_crystal_state *c=&d->crystals[index];
    if (!c->in_world) return;
    if (c->dead) { c->in_world=0; return; }
    ++c->inner_rotation;
    if (!d->world) return;
    int x=mh_floor(c->x), y=mh_floor(c->y), z=mh_floor(c->z);
    if ((world_get_block(d->world,x,y,z)&4095)!=51)
        world_set_block(d->world,x,y,z,51,0,3);
}

int dragon_attack_part(struct dragon_state *d, int part, float amount, int player_source)
{
    if (part != 0) amount = amount / 4.0f + 1.0f;
    float angle = d->yaw * PIF / 180.0f;
    float sn = mh_sin(angle), cs = mh_cos(angle);
    d->tx = d->x + (double)(sn * 5.0f) + (double)((det_rng_float(&d->rand) - 0.5f) * 2.0f);
    d->ty = d->y + (double)(det_rng_float(&d->rand) * 3.0f) + 1.0;
    d->tz = d->z - (double)(cs * 5.0f) + (double)((det_rng_float(&d->rand) - 0.5f) * 2.0f);
    d->has_target = 0;
    if (player_source == 0 || d->health <= 0.0f) return 0;
    int full_hurt = 1;
    if (d->hurt_resistant_time > 10) {
        if (amount <= d->last_damage) return 0;
        enchantment_damage_roll(d);
        d->health -= amount - d->last_damage;
        if (d->health < 0.0f) d->health=0.0f;
        d->last_damage = amount;
        full_hurt = 0;
    } else {
        d->last_damage = amount;
        d->prev_health = d->health;
        d->hurt_resistant_time = 20;
        enchantment_damage_roll(d);
        d->health -= amount;
        if (d->health < 0.0f) d->health=0.0f;
        d->hurt_time = 10;
    }
    if (full_hurt) {
        /* setBeenAttacked, knockBack, hurt/death sound pitch. */
        (void)det_rng_double(&d->rand);
        if (player_source == 2 && d->det) (void)det_math_random_role(d->det,det_role(d->det));
        if (player_source == 1) {
            (void)det_rng_double(&d->rand);
            double dx = d->player_x - d->x, dz = d->player_z - d->z;
            while (dx*dx+dz*dz<1.0e-4 && d->det) {
                dx=random_knockback_offset(d->det);
                dz=random_knockback_offset(d->det);
            }
            float len = (float)sqrt(dx*dx + dz*dz);
            d->mx /= 2.0; d->my /= 2.0; d->mz /= 2.0;
            d->mx -= dx / (double)len * 0.4000000059604645;
            d->my += 0.4000000059604645;
            d->mz -= dz / (double)len * 0.4000000059604645;
            if (d->my > 0.4000000059604645) d->my = 0.4000000059604645;
            d->is_air_borne = 1;
        }
        (void)det_rng_float(&d->rand);
        (void)det_rng_float(&d->rand);
    }
    if (d->health <= 0.0f) d->dead_flag = 1;
    return d->health <= 0.0f;
}

void dragon_crystal_attack(struct dragon_state *d, int index)
{
    if (index<0 || index>=d->n_crystals) return;
    struct dragon_crystal_state *c=&d->crystals[index];
    if (c->dead) return;
    c->health=0; c->dead=1;
    if (d->crystal_explode) d->crystal_explode(d->crystal_explode_ctx,c->x,c->y,c->z);
}

static void create_portal(struct dragon_state *d)
{
    if (!d->world) return;
    /* BlockEndPortal.field_149948_a = true for the whole build */
    nw_env->blockcb.end_portal_kept = 1;
    int cx = mh_floor(d->x), cz = mh_floor(d->z);
    for (int y = 63; y <= 96; ++y)
        for (int x = cx - 4; x <= cx + 4; ++x)
            for (int z = cz - 4; z <= cz + 4; ++z) {
                double dx = (double)(x-cx), dz = (double)(z-cz), r = dx*dx+dz*dz;
                if (r > 12.25) continue;
                if (y < 64) {
                    if (r <= 6.25) world_set_block(d->world,x,y,z,7,0,3);
                } else if (y > 64) world_set_block(d->world,x,y,z,0,0,3);
                else if (r > 6.25) world_set_block(d->world,x,y,z,7,0,3);
                else world_set_block(d->world,x,y,z,119,0,3);
            }
    world_set_block(d->world,cx,64,cz,7,0,3);
    world_set_block(d->world,cx,65,cz,7,0,3);
    world_set_block(d->world,cx,66,cz,7,0,3);
    world_set_block(d->world,cx-1,66,cz,50,0,3);
    world_set_block(d->world,cx+1,66,cz,50,0,3);
    world_set_block(d->world,cx,66,cz-1,50,0,3);
    world_set_block(d->world,cx,66,cz+1,50,0,3);
    world_set_block(d->world,cx,67,cz,7,0,3);
    world_set_block(d->world,cx,68,cz,122,0,3);
    nw_env->blockcb.end_portal_kept = 0;
}

void dragon_tick(struct dragon_state *d)
{
    if (d->dead) return;
    ++d->ticks;
    d->prev_x = d->x; d->prev_y = d->y; d->prev_z = d->z;
    d->prev_yaw = d->yaw;
    /* Entity.onEntityUpdate's handleWaterMovement: flowing water pushes the
     * dragon's box like any entity's, and an entry after the first update
     * plays the splash (the sound's pitch, the bubbles and the splashes,
     * nothing on the server, every draw on the dragon's Random) */
    if (d->world) {
        struct aabb bb = aabb_make(d->bb_min_x, d->bb_min_y, d->bb_min_z, d->bb_max_x, d->bb_min_y + 8.0, d->bb_max_z);
        int was = d->in_water;
        uint8_t in = 0;
        float fall = 0.0f;
        int hit = handle_water_movement(d->world, bb, &d->mx, &d->my, &d->mz, &in, &fall);
        d->in_water = in;
        if (hit && !was && !d->first_update) {
            (void)det_rng_float(&d->rand);
            (void)det_rng_float(&d->rand);
            for (int i = 0; (float)i < 1.0F + 16.0F * 20.0F; ++i) {
                (void)det_rng_float(&d->rand);
                (void)det_rng_float(&d->rand);
                (void)det_rng_float(&d->rand);
            }
            for (int i = 0; (float)i < 1.0F + 16.0F * 20.0F; ++i) {
                (void)det_rng_float(&d->rand);
                (void)det_rng_float(&d->rand);
            }
        }
    }
    d->first_update = 0;
    /* EntityLivingBase.onEntityUpdate's air: the eye (height * 0.85) in
     * water spends it (no respiration, no draw), drowning at -20 (the eight
     * bubbles' draws; the dragon's attackEntityFrom refuses the damage) */
    if (d->world && d->health > 0.0f) {
        double ey = d->y + (double)(8.0F * 0.85F);
        int bx = mh_floor(d->x), by = j_f2i((float)mh_floor(ey)), bz = mh_floor(d->z);
        int id = world_get_block(d->world, bx, by, bz) & 4095;
        int under = 0;
        if (BLOCKS[id].material == 6) {   /* Material.water */
            float h = ie_liquid_height_meta(world_get_meta(d->world, bx, by, bz)) - 0.11111111F;
            under = ey < (double)((float)(by + 1) - h);
        }
        if (under) {
            if (--d->air == -20) {
                d->air = 0;
                for (int i = 0; i < 8 * 6; ++i) (void)det_rng_float(&d->rand);
            }
        } else d->air = 300;
    } else if (d->world) d->air = 300;
    /* EntityLiving.onEntityUpdate's ambient sound check. */
    if (d->health > 0.0f && det_rng_int_n(&d->rand, 1000) < d->living_sound_time++) {
        d->living_sound_time = -80;
        (void)det_rng_float(&d->rand);
        (void)det_rng_float(&d->rand);
    }
    if (nw_env->cfg.dragon_negative_draw && d->ticks==1) (void)det_rng_float(&d->rand);
    if (d->hurt_time > 0) --d->hurt_time;
    if (d->hurt_resistant_time > 0) --d->hurt_resistant_time;
    d->prev_anim = d->anim;
    if (d->health <= 0.0f) {
        ++d->death_ticks;
        if (d->death_ticks > 150 && d->death_ticks % 5 == 0 && d->spawn_xp)
            d->spawn_xp(d->spawn_xp_ctx, d->x, d->y, d->z, 1000);
        if (d->death_ticks >= 180 && d->death_ticks <= 200) {
            (void)det_rng_float(&d->rand); (void)det_rng_float(&d->rand); (void)det_rng_float(&d->rand);
        }
        d->bb_min_y += 0.10000000149011612;
        d->y = d->bb_min_y;
        d->render_yaw = d->yaw += 20.0f;
        d->prev_yaw = d->yaw;
        if (d->death_ticks == 200) {
            if (d->spawn_xp) d->spawn_xp(d->spawn_xp_ctx, d->x, d->y, d->z, 2000);
            create_portal(d); d->dead = 1;
        }
        (void)det_rng_float(&d->rand); (void)det_rng_float(&d->rand); (void)det_rng_float(&d->rand);
        return;
    }
    if (d->healing_crystal_index>=0) {
        struct dragon_crystal_state *c=&d->crystals[d->healing_crystal_index];
        if (c->dead) {
            d->healing_crystal_index=-1;
            dragon_attack_part(d,0,10.0f,2);
        } else if (d->ticks % 10 == 0 && d->health < 200.0f) {
            /* setHealth clamps to the maximum: 199.5 heals to 200 */
            d->health += 1.0f;
            if (d->health > 200.0f) d->health = 200.0f;
        }
    }
    if (det_rng_int_n(&d->rand, 10) == 0) {
        int nearest=-1;
        double best=DBL_MAX;
        for (int i=0;i<d->n_crystals;++i) {
            const struct dragon_crystal_state *c=&d->crystals[i];
            if (!c->in_world) continue;
            if (!(c->x+1.0>d->bb_min_x-32.0 && c->x-1.0<d->bb_max_x+32.0 &&
                  c->y+1.0>d->bb_min_y-32.0 && c->y-1.0<d->bb_min_y+40.0 &&
                  c->z+1.0>d->bb_min_z-32.0 && c->z-1.0<d->bb_max_z+32.0)) continue;
            double dx=c->x-d->x, dy=c->y-d->y, dz=c->z-d->z;
            double dist=dx*dx+dy*dy+dz*dz;
            if (dist<best) { best=dist; nearest=i; }
        }
        d->healing_crystal_index=nearest;
    }
    float a = 0.2f / ((float)sqrt_float(d->mx*d->mx + d->mz*d->mz) * 10.0f + 1.0f);
    a *= (float)fd_pow(2.0, d->my);
    d->anim += d->slowed ? a * 0.5f : a;
    d->yaw = wrap_f(d->yaw);
    if (d->ring_index < 0)
        for (int i = 0; i < 64; ++i) { d->ring[i][0] = (double)d->yaw; d->ring[i][1] = d->y; }
    if (++d->ring_index == 64) d->ring_index = 0;
    d->ring[d->ring_index][0] = (double)d->yaw;
    d->ring[d->ring_index][1] = d->y;
    double dx = d->tx - d->x, dy = d->ty - d->y, dz = d->tz - d->z;
    double dist = dx*dx + dy*dy + dz*dz;
    if (d->has_target) {
        d->tx = d->player_x;
        d->tz = d->player_z;
        double px = d->tx - d->x, pz = d->tz - d->z;
        double horizontal = sqrt(px*px + pz*pz);
        double rise = 0.4000000059604645 + horizontal / 80.0 - 1.0;
        if (rise > 10.0) rise = 10.0;
        d->ty = d->player_min_y + rise;
    } else {
        d->tx += det_rng_gaussian(&d->rand) * 2.0;
        d->tz += det_rng_gaussian(&d->rand) * 2.0;
    }
    if (d->force_target || dist < 100.0 || dist > 22500.0) set_target(d);
    dy /= sqrt_float(dx*dx + dz*dz);
    if (dy < -0.6000000238418579) dy = -0.6000000238418579;
    if (dy > 0.6000000238418579) dy = 0.6000000238418579;
    d->my += dy * 0.10000000149011612;
    d->yaw = wrap_f(d->yaw);
    double desired = 180.0 - fd_atan2(dx, dz) * 180.0 / PI;
    double turn = wrap_d(desired - (double)d->yaw);
    if (turn > 50.0) turn = 50.0;
    if (turn < -50.0) turn = -50.0;
    /* Vec3.normalize: zero-length vectors remain unchanged. */
    double vx = d->tx-d->x, vy = d->ty-d->y, vz = d->tz-d->z;
    double vl = sqrt_float(vx*vx + vy*vy + vz*vz);
    if (vl >= 1.0E-4) { vx /= vl; vy /= vl; vz /= vl; }
    float sn = mh_sin(d->yaw * PIF / 180.0f), cs = mh_cos(d->yaw * PIF / 180.0f);
    double fx = (double)sn, fy = d->my, fz = (double)-cs;
    double fl = sqrt_float(fx*fx + fy*fy + fz*fz);
    if (fl >= 1.0E-4) { fx /= fl; fy /= fl; fz /= fl; }
    float align = (float)(fx*vx + fy*vy + fz*vz + 0.5) / 1.5f;
    if (align < 0.0f) align = 0.0f;
    d->yaw_velocity *= 0.8f;
    float drag_f = (float)sqrt_float(d->mx*d->mx + d->mz*d->mz) * 1.0f + 1.0f;
    double drag = sqrt(d->mx*d->mx + d->mz*d->mz) * 1.0 + 1.0;
    if (drag > 40.0) drag = 40.0;
    d->yaw_velocity = (float)((double)d->yaw_velocity + turn * (0.699999988079071 / drag / (double)drag_f));
    d->yaw += d->yaw_velocity * 0.1f;
    float accel = (float)(2.0 / (drag + 1.0));
    move_flying(d, 0.06f * (align * accel + (1.0f - accel)));
    double slow = d->slowed ? 0.800000011920929 : 1.0;
    d->bb_min_x += d->mx * slow; d->bb_max_x += d->mx * slow;
    d->bb_min_y += d->my * slow;
    d->bb_min_z += d->mz * slow; d->bb_max_z += d->mz * slow;
    d->x = (d->bb_min_x + d->bb_max_x) / 2.0;
    d->y = d->bb_min_y;
    d->z = (d->bb_min_z + d->bb_max_z) / 2.0;
    double ml = sqrt_float(d->mx*d->mx + d->my*d->my + d->mz*d->mz);
    double ux = d->mx, uy = d->my, uz = d->mz;
    if (ml >= 1.0E-4) { ux /= ml; uy /= ml; uz /= ml; }
    float damp = (float)(ux*fx + uy*fy + uz*fz + 1.0) / 2.0f;
    damp = 0.8f + 0.15f * damp;
    d->mx *= (double)damp; d->mz *= (double)damp; d->my *= 0.9100000262260437;
    d->render_yaw = d->yaw;
    update_parts(d);
    int head_blocked = destroy_blocks(d, &d->part[0]);
    int body_blocked = destroy_blocks(d, &d->part[1]);
    d->slowed = head_blocked | body_blocked;
    /* EntityLivingBase.onUpdate's turn of the rendered body after
     * onLivingUpdate. EntityLiving.func_110146_f calls this base body. */
    double px = d->x - d->prev_x, pz = d->z - d->prev_z;
    float distance_sq = (float)(px * px + pz * pz);
    float body = d->render_yaw;
    if (distance_sq > 0.0025000002f)
        body = (float)fd_atan2(pz, px) * 180.0f / PIF - 90.0f;
    float delta = wrap_f(body - d->render_yaw);
    d->render_yaw += delta * 0.3f;
    float relative = wrap_f(d->yaw - d->render_yaw);
    if (relative < -75.0f) relative = -75.0f;
    if (relative >= 75.0f) relative = 75.0f;
    d->render_yaw = d->yaw - relative;
    if (relative * relative > 2500.0f) d->render_yaw += relative * 0.2f;
    /* The free-flight probe has no blocks at flight altitude. */
    while (d->yaw - d->prev_yaw < -180.0f) d->prev_yaw -= 360.0f;
    while (d->yaw - d->prev_yaw >= 180.0f) d->prev_yaw += 360.0f;
}
