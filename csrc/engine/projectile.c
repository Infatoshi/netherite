#include "projectile.h"
#include "env.h"
#include "blocks.h"
#include "collide.h"
#include "envstack.h"
#include "ghasts.h"
#include "jmath.h"
#include "living.h"
#include "raytrace.h"
#include "trace.h"
#include "entityquery.h"
#include "smath.h"
#include "world.h"
#include "stronghold.h"

#include <math.h>
#include <stdlib.h>
#include <stdio.h>



static int aabb_calculate_intercept(const struct aabb *box,
                                    double x0, double y0, double z0,
                                    double x1, double y1, double z1,
                                    double *hx, double *hy, double *hz)
{
    double dx = x1 - x0;
    double dy = y1 - y0;
    double dz = z1 - z0;

    int have_v[6] = {0};
    double vx[6], vy[6], vz[6];

    if (dx * dx >= 1.0000000116860974E-7) {
        double t = (box->min_x - x0) / dx;
        if (t >= 0.0 && t <= 1.0) {
            double rx = x0 + dx * t, ry = y0 + dy * t, rz = z0 + dz * t;
            if (ry >= box->min_y && ry <= box->max_y && rz >= box->min_z && rz <= box->max_z) {
                have_v[0] = 1; vx[0] = rx; vy[0] = ry; vz[0] = rz;
            }
        }
        t = (box->max_x - x0) / dx;
        if (t >= 0.0 && t <= 1.0) {
            double rx = x0 + dx * t, ry = y0 + dy * t, rz = z0 + dz * t;
            if (ry >= box->min_y && ry <= box->max_y && rz >= box->min_z && rz <= box->max_z) {
                have_v[1] = 1; vx[1] = rx; vy[1] = ry; vz[1] = rz;
            }
        }
    }
    if (dy * dy >= 1.0000000116860974E-7) {
        double t = (box->min_y - y0) / dy;
        if (t >= 0.0 && t <= 1.0) {
            double rx = x0 + dx * t, ry = y0 + dy * t, rz = z0 + dz * t;
            if (rx >= box->min_x && rx <= box->max_x && rz >= box->min_z && rz <= box->max_z) {
                have_v[2] = 1; vx[2] = rx; vy[2] = ry; vz[2] = rz;
            }
        }
        t = (box->max_y - y0) / dy;
        if (t >= 0.0 && t <= 1.0) {
            double rx = x0 + dx * t, ry = y0 + dy * t, rz = z0 + dz * t;
            if (rx >= box->min_x && rx <= box->max_x && rz >= box->min_z && rz <= box->max_z) {
                have_v[3] = 1; vx[3] = rx; vy[3] = ry; vz[3] = rz;
            }
        }
    }
    if (dz * dz >= 1.0000000116860974E-7) {
        double t = (box->min_z - z0) / dz;
        if (t >= 0.0 && t <= 1.0) {
            double rx = x0 + dx * t, ry = y0 + dy * t, rz = z0 + dz * t;
            if (rx >= box->min_x && rx <= box->max_x && ry >= box->min_y && ry <= box->max_y) {
                have_v[4] = 1; vx[4] = rx; vy[4] = ry; vz[4] = rz;
            }
        }
        t = (box->max_z - z0) / dz;
        if (t >= 0.0 && t <= 1.0) {
            double rx = x0 + dx * t, ry = y0 + dy * t, rz = z0 + dz * t;
            if (rx >= box->min_x && rx <= box->max_x && ry >= box->min_y && ry <= box->max_y) {
                have_v[5] = 1; vx[5] = rx; vy[5] = ry; vz[5] = rz;
            }
        }
    }

    int best_idx = -1;
    double min_d = 0.0;
    for (int i = 0; i < 6; ++i) {
        if (!have_v[i]) continue;
        double d = (vx[i] - x0) * (vx[i] - x0) + (vy[i] - y0) * (vy[i] - y0) + (vz[i] - z0) * (vz[i] - z0);
        if (best_idx < 0 || d < min_d) {
            best_idx = i;
            min_d = d;
        }
    }
    if (best_idx < 0) return 0;
    *hx = vx[best_idx];
    *hy = vy[best_idx];
    *hz = vz[best_idx];
    return 1;
}

static int get_xp_split(int p)
{
    return p >= 2477 ? 2477 : (p >= 1237 ? 1237 : (p >= 617 ? 617 : (p >= 307 ? 307 : (p >= 149 ? 149 : (p >= 73 ? 73 : (p >= 37 ? 37 : (p >= 17 ? 17 : (p >= 7 ? 7 : (p >= 3 ? 3 : 1)))))))));
}

static void update_rotations(ie_ent *en, double mx, double my, double mz, int is_fireball)
{
    float var16, yaw, pitch;
    if (is_fireball)
    {
        var16 = (float)sqrt(mx * mx + mz * mz);
        yaw = (float)(atan2(mz, mx) * 180.0 / 3.141592653589793) + 90.0F;
        pitch = (float)(atan2((double)var16, my) * 180.0 / 3.141592653589793) - 90.0F;
    }
    else
    {
        var16 = (float)sqrt(mx * mx + mz * mz);
        yaw = (float)(atan2(mx, mz) * 180.0 / 3.141592653589793);
        pitch = (float)(atan2(my, (double)var16) * 180.0 / 3.141592653589793);
    }

    while (pitch - en->prev_pitch < -180.0F) en->prev_pitch -= 360.0F;
    while (pitch - en->prev_pitch >= 180.0F) en->prev_pitch += 360.0F;
    while (yaw - en->prev_yaw < -180.0F) en->prev_yaw -= 360.0F;
    while (yaw - en->prev_yaw >= 180.0F) en->prev_yaw += 360.0F;

    en->rotation_pitch = en->prev_pitch + (pitch - en->prev_pitch) * 0.2F;
    en->rotation_yaw = en->prev_yaw + (yaw - en->prev_yaw) * 0.2F;
}

void proj_arrow_heading(ie_ent *en, double dir_x, double dir_y, double dir_z,
                        float speed, float inaccuracy)
{
    float var9 = (float)sqrt(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
    dir_x /= (double)var9;
    dir_y /= (double)var9;
    dir_z /= (double)var9;

    double g1 = det_rng_gaussian(&en->rand);
    double b1 = det_rng_bool(&en->rand) ? -1.0 : 1.0;
    dir_x += g1 * b1 * 0.007499999832361937 * (double)inaccuracy;

    double g2 = det_rng_gaussian(&en->rand);
    double b2 = det_rng_bool(&en->rand) ? -1.0 : 1.0;
    dir_y += g2 * b2 * 0.007499999832361937 * (double)inaccuracy;

    double g3 = det_rng_gaussian(&en->rand);
    double b3 = det_rng_bool(&en->rand) ? -1.0 : 1.0;
    dir_z += g3 * b3 * 0.007499999832361937 * (double)inaccuracy;

    dir_x *= (double)speed;
    dir_y *= (double)speed;
    dir_z *= (double)speed;

    en->e.motion_x = dir_x;
    en->e.motion_y = dir_y;
    en->e.motion_z = dir_z;

    float var10 = (float)sqrt(dir_x * dir_x + dir_z * dir_z);
    en->prev_yaw = en->rotation_yaw = (float)(atan2(dir_x, dir_z) * 180.0 / 3.141592653589793);
    en->prev_pitch = en->rotation_pitch = (float)(atan2(dir_y, (double)var10) * 180.0 / 3.141592653589793);
    en->ticks_in_ground = 0;
}

void proj_throwable_heading(ie_ent *en, double dir_x, double dir_y, double dir_z,
                            float speed, float inaccuracy)
{
    float var9 = (float)sqrt(dir_x * dir_x + dir_y * dir_y + dir_z * dir_z);
    dir_x /= (double)var9;
    dir_y /= (double)var9;
    dir_z /= (double)var9;

    double g1 = det_rng_gaussian(&en->rand);
    dir_x += g1 * 0.007499999832361937 * (double)inaccuracy;

    double g2 = det_rng_gaussian(&en->rand);
    dir_y += g2 * 0.007499999832361937 * (double)inaccuracy;

    double g3 = det_rng_gaussian(&en->rand);
    dir_z += g3 * 0.007499999832361937 * (double)inaccuracy;

    dir_x *= (double)speed;
    dir_y *= (double)speed;
    dir_z *= (double)speed;

    en->e.motion_x = dir_x;
    en->e.motion_y = dir_y;
    en->e.motion_z = dir_z;

    float var10 = (float)sqrt(dir_x * dir_x + dir_z * dir_z);
    en->prev_yaw = en->rotation_yaw = (float)(atan2(dir_x, dir_z) * 180.0 / 3.141592653589793);
    en->prev_pitch = en->rotation_pitch = (float)(atan2(dir_y, (double)var10) * 180.0 / 3.141592653589793);
    en->ticks_in_ground = 0;
}

ie_ent *proj_spawn_arrow(ie_world *iew, double x, double y, double z,
                         double dir_x, double dir_y, double dir_z,
                         float speed, float inaccuracy)
{
    ie_ent *en = ie_ent_alloc();
    entity_init(&en->e, iew->w);
    en->e.can_trigger_walking = 0;
    en->e.self = en;
    en->e.attack_from = NULL;
    en->e.first_update = 1; en->first_update = 1;
    en->entity_id = det_next_entity_id_role(iew->det, iew->role);
    en->rand = det_new_random_role(iew->det, iew->role);
    int64_t msb, lsb;
    det_uuid_role(iew->det, iew->role, &msb, &lsb);

    en->kind = IE_ARROW;
    en->iew = iew;
    entity_set_size(&en->e, 0.5F, 0.5F);
    en->e.y_offset = 0.0F;
    entity_set_position(&en->e, x, y, z);
    en->arrow_damage = 2.0;
    en->can_be_picked_up = 0;
    en->tile_x = en->tile_y = en->tile_z = -1;
    en->in_tile = -1;

    proj_arrow_heading(en, dir_x, dir_y, dir_z, speed, inaccuracy);

    ie_list_push(iew, en);
    return en;
}


ie_ent *proj_spawn_arrow_target(ie_world *iew, struct living *shooter, struct living *target,
                                float speed, float inaccuracy)
{
    ie_ent *en = ie_ent_alloc();
    entity_init(&en->e, iew->w);
    en->e.can_trigger_walking = 0;
    en->e.self = en;
    en->e.attack_from = NULL;
    en->e.first_update = 1; en->first_update = 1;
    en->entity_id = det_next_entity_id_role(iew->det, iew->role);
    en->rand = det_new_random_role(iew->det, iew->role);
    int64_t msb, lsb;
    det_uuid_role(iew->det, iew->role, &msb, &lsb);
    en->uuid_msb = msb;
    en->uuid_lsb = lsb;

    en->kind = IE_ARROW;
    en->iew = iew;
    entity_set_size(&en->e, 0.6F, 1.8F);
    en->arrow_damage = 2.0;
    en->can_be_picked_up = (shooter && (shooter->kind == HK_PLAYER || shooter->kind == SK_PLAYER)) ? 1 : 0;
    en->tile_x = en->tile_y = en->tile_z = -1;
    en->in_tile = -1;
    en->shooting_entity = lv_ref(shooter);

    double eye_y = shooter ? (shooter->e.pos_y + (double)living_eye_height(shooter)) : 0.0;
    en->e.pos_y = eye_y - 0.10000000149011612;

    double var6 = target ? (target->e.pos_x - shooter->e.pos_x) : 0.0;
    double var8 = target ? (target->e.bounding_box.min_y + (double)(target->e.height / 3.0f) - en->e.pos_y) : 0.0;
    double var10 = target ? (target->e.pos_z - shooter->e.pos_z) : 0.0;
    double var12 = (double)(float)sqrt(var6 * var6 + var10 * var10);

    if (var12 >= 1.0E-7)
    {
        float var14 = (float)(fd_atan2(var10, var6) * 180.0 / 3.141592653589793) - 90.0F;
        float var15 = (float)(-(fd_atan2(var8, var12) * 180.0 / 3.141592653589793));
        double var16 = var6 / var12;
        double var18 = var10 / var12;
        en->e.pos_x = shooter->e.pos_x + var16;
        en->e.pos_z = shooter->e.pos_z + var18;
        en->rotation_yaw = var14;
        en->rotation_pitch = var15;
        en->prev_yaw = var14;
        en->prev_pitch = var15;
        entity_set_position(&en->e, en->e.pos_x, en->e.pos_y, en->e.pos_z);
        en->e.y_offset = 0.0F;
        float var20 = (float)var12 * 0.2F;
        proj_arrow_heading(en, var6, var8 + (double)var20, var10, speed, inaccuracy);
    }
    else
    {
        entity_set_position(&en->e, shooter ? shooter->e.pos_x : 0.0, en->e.pos_y, shooter ? shooter->e.pos_z : 0.0);
    }

    ie_list_push(iew, en);
    return en;
}


/* The Entity constructor's draws and Entity.setLocationAndAngles from a
 * living shooter's eye, then the 0.16 side step EntityArrow's and
 * EntityThrowable's shooter constructors both make. */
static ie_ent *proj_from_shooter(ie_world *iew, int kind, float width, double x, double y, double z,
                                 float eye_height, float yaw, float pitch)
{
    ie_ent *en = ie_ent_alloc();
    entity_init(&en->e, iew->w);
    en->e.self = en;
    en->e.attack_from = NULL;
    en->e.first_update = 1; en->first_update = 1;
    en->entity_id = det_next_entity_id_role(iew->det, iew->role);
    en->rand = det_new_random_role(iew->det, iew->role);
    det_uuid_role(iew->det, iew->role, &en->uuid_msb, &en->uuid_lsb);
    en->kind = kind;
    en->iew = iew;
    if (kind == IE_ARROW) en->e.can_trigger_walking = 0;
    entity_set_size(&en->e, width, width);
    /* yOffset is still the constructor's 0 */
    en->e.pos_x = x;
    en->e.pos_y = y + (double)eye_height;
    en->e.pos_z = z;
    en->rotation_yaw = yaw;
    en->rotation_pitch = pitch;
    entity_set_position(&en->e, en->e.pos_x, en->e.pos_y, en->e.pos_z);
    float rad = en->rotation_yaw / 180.0F * (float)3.141592653589793;
    en->e.pos_x -= (double)(mh_cos(rad) * 0.16F);
    en->e.pos_y -= 0.10000000149011612;
    en->e.pos_z -= (double)(mh_sin(rad) * 0.16F);
    entity_set_position(&en->e, en->e.pos_x, en->e.pos_y, en->e.pos_z);
    en->e.y_offset = 0.0F;
    en->tile_x = en->tile_y = en->tile_z = -1;
    en->in_tile = -1;   /* no block: getIdFromBlock(null) */
    return en;
}

/* EntityArrow(World, EntityLivingBase, float): a player shooter makes it
 * pickup 1. The caller adds it to its lists. */
ie_ent *proj_spawn_arrow_shooter(ie_world *iew, struct living *shooter, int shooter_is_player,
                                 double x, double y, double z, float eye_height,
                                 float yaw, float pitch, float velocity)
{
    ie_ent *en = proj_from_shooter(iew, IE_ARROW, 0.5F, x, y, z, eye_height, yaw, pitch);
    if (!en) return NULL;
    en->arrow_damage = 2.0;
    en->in_tile = -1;
    en->shooting_entity = lv_ref(shooter);
    en->shooter = lv_ref(shooter);
    en->shooter_is_player = shooter_is_player;
    if (shooter_is_player) en->can_be_picked_up = 1;
    float ry = en->rotation_yaw / 180.0F * (float)3.141592653589793;
    float rp = en->rotation_pitch / 180.0F * (float)3.141592653589793;
    double mx = (double)(-mh_sin(ry) * mh_cos(rp));
    double mz = (double)(mh_cos(ry) * mh_cos(rp));
    double my = (double)(-mh_sin(rp));
    en->e.motion_x = mx;
    en->e.motion_y = my;
    en->e.motion_z = mz;
    proj_arrow_heading(en, mx, my, mz, velocity * 1.5F, 1.0F);
    ie_list_push(iew, en);
    return en;
}

/* EntityThrowable(World, EntityLivingBase) for the snowball, the egg and the
 * pearl (func_70182_d 1.5, func_70183_g 0). The caller adds it to its lists. */
ie_ent *proj_spawn_throwable_shooter(ie_world *iew, int kind, struct living *shooter, int shooter_is_player,
                                     double x, double y, double z, float eye_height,
                                     float yaw, float pitch)
{
    ie_ent *en = proj_from_shooter(iew, kind, 0.25F, x, y, z, eye_height, yaw, pitch);
    if (!en) return NULL;
    en->shooting_entity = lv_ref(shooter);
    en->shooter = lv_ref(shooter);
    en->shooter_is_player = shooter_is_player;
    /* func_70182_d and func_70183_g: EntityPotion 0.5 and -20,
     * EntityExpBottle 0.7 and -20, the rest EntityThrowable's 1.5 and 0 */
    float speed = kind == IE_POTION ? 0.5F : kind == IE_EXP_BOTTLE ? 0.7F : 1.5F;
    float pitch_offset = (kind == IE_POTION || kind == IE_EXP_BOTTLE) ? -20.0F : 0.0F;
    float var3 = 0.4F;
    float ry = en->rotation_yaw / 180.0F * (float)3.141592653589793;
    float rp = en->rotation_pitch / 180.0F * (float)3.141592653589793;
    double mx = (double)(-mh_sin(ry) * mh_cos(rp) * var3);
    double mz = (double)(mh_cos(ry) * mh_cos(rp) * var3);
    double my = (double)(-mh_sin((en->rotation_pitch + pitch_offset) / 180.0F * (float)3.141592653589793) * var3);
    en->e.motion_x = mx;
    en->e.motion_y = my;
    en->e.motion_z = mz;
    proj_throwable_heading(en, mx, my, mz, speed, 1.0F);
    ie_list_push(iew, en);
    return en;
}

ie_ent *proj_spawn_throwable(ie_world *iew, int kind, double x, double y, double z,
                             double dir_x, double dir_y, double dir_z,
                             float speed, float inaccuracy, int potion_damage)
{
    ie_ent *en = ie_ent_alloc();
    entity_init(&en->e, iew->w);
    en->e.self = en;
    en->e.attack_from = NULL;
    en->e.first_update = 1; en->first_update = 1;
    en->entity_id = det_next_entity_id_role(iew->det, iew->role);
    en->rand = det_new_random_role(iew->det, iew->role);
    int64_t msb, lsb;
    det_uuid_role(iew->det, iew->role, &msb, &lsb);

    en->kind = kind;
    entity_set_size(&en->e, 0.25F, 0.25F);
    en->e.y_offset = 0.0F;
    entity_set_position(&en->e, x, y, z);
    en->tile_x = en->tile_y = en->tile_z = -1;
    en->potion_damage = potion_damage;

    proj_throwable_heading(en, dir_x, dir_y, dir_z, speed, inaccuracy);

    ie_list_push(iew, en);
    return en;
}

ie_ent *proj_spawn_fireball(ie_world *iew, int kind, double x, double y, double z,
                            double accel_x, double accel_y, double accel_z)
{
    ie_ent *en = ie_ent_alloc();
    entity_init(&en->e, iew->w);
    en->e.self = en;
    en->e.attack_from = NULL;
    en->e.first_update = 1; en->first_update = 1;
    en->entity_id = det_next_entity_id_role(iew->det, iew->role);
    en->rand = det_new_random_role(iew->det, iew->role);
    int64_t msb, lsb;
    det_uuid_role(iew->det, iew->role, &msb, &lsb);
    en->uuid_msb = msb;
    en->uuid_lsb = lsb;

    en->kind = kind;
    if (kind == IE_LARGE_FIREBALL) {
        entity_set_size(&en->e, 1.0F, 1.0F);
        en->explosion_power = 1;
    } else {
        entity_set_size(&en->e, 0.3125F, 0.3125F);
    }
    entity_set_position(&en->e, x, y, z);

    double len = sqrt(accel_x * accel_x + accel_y * accel_y + accel_z * accel_z);
    en->accel_x = accel_x / len * 0.1;
    en->accel_y = accel_y / len * 0.1;
    en->accel_z = accel_z / len * 0.1;
    en->tile_x = en->tile_y = en->tile_z = -1;
    en->in_tile = -1;   /* field_145796_h: no block, NBT inTile -1 */

    ie_list_push(iew, en);
    return en;
}

ie_ent *proj_spawn_ender_eye(ie_world *iew, double x, double y, double z,
                             double target_x, int target_y, double target_z)
{
    ie_ent *en = ie_ent_alloc();
    entity_init(&en->e, iew->w);
    en->e.self = en;
    en->e.first_update = en->first_update = 1;
    en->entity_id = det_next_entity_id_role(iew->det, iew->role);
    en->rand = det_new_random_role(iew->det, iew->role);
    det_uuid_role(iew->det, iew->role, &en->uuid_msb, &en->uuid_lsb);
    en->kind = IE_ENDER_EYE;
    en->iew = iew;
    entity_set_size(&en->e, 0.25F, 0.25F);
    entity_set_position(&en->e, x, y, z);
    en->e.y_offset = 0.0F;

    double dx = target_x - x, dz = target_z - z;
    float distance = (float)sqrt(dx * dx + dz * dz);
    if (distance > 12.0F)
    {
        en->eye_target_x = x + dx / (double)distance * 12.0;
        en->eye_target_y = y + 8.0;
        en->eye_target_z = z + dz / (double)distance * 12.0;
    }
    else
    {
        en->eye_target_x = target_x;
        en->eye_target_y = (double)target_y;
        en->eye_target_z = target_z;
    }
    en->eye_drop = det_rng_int_n(&en->rand, 5) > 0;
    ie_list_push(iew, en);
    ie_added_to_world(iew, en);
    return en;
}

/* EntityEnderEye(World), EntityList.createEntityFromNBT's: Entity's
 * constructor (the id, the Random, the UUID) and setSize(0.25, 0.25). Its
 * target, despawn timer and drop flag are not saved (readEntityFromNBT is
 * empty): a loaded eye flies at (0, 0, 0) and shatters, unless a snapshot's
 * runtime block brings them back. The caller sets the position and adopts
 * it. */
ie_ent *proj_load_ender_eye(ie_world *iew, double x, double y, double z)
{
    ie_ent *en = ie_ent_alloc();
    entity_init(&en->e, iew->w);
    en->e.self = en;
    en->e.first_update = en->first_update = 1;
    en->entity_id = det_next_entity_id_role(iew->det, iew->role);
    en->rand = det_new_random_role(iew->det, iew->role);
    det_uuid_role(iew->det, iew->role, &en->uuid_msb, &en->uuid_lsb);
    en->kind = IE_ENDER_EYE;
    en->iew = iew;
    entity_set_size(&en->e, 0.25F, 0.25F);
    en->e.y_offset = 0.0F;
    entity_set_position(&en->e, x, y, z);
    ie_list_push(iew, en);
    return en;
}

ie_ent *proj_throw_ender_eye(ie_world *iew, double player_x, double player_y,
                             double player_z, float player_y_offset)
{
    int tx = 0, ty = 0, tz = 0;
    stronghold_nearest_loaded(iew->w, (int)player_x, (int)player_y, (int)player_z,
                              &tx, &ty, &tz);
    ie_ent *en = proj_spawn_ender_eye(iew, player_x,
                                      player_y + 1.62 - (double)player_y_offset,
                                      player_z, (double)tx, ty, (double)tz);
    if (en)
    {
        const char *name = "./net/minecraft/item/Item.java:itemRand";
        det_split *item_rand = det_split_find(iew->det, name);
        if (!item_rand) item_rand = det_split_random(iew->det, name);
        int previous_role = det_role(iew->det);
        det_set_role(iew->det, iew->role);
        (void)det_split_float(iew->det, item_rand); /* random.bow pitch */
        det_set_role(iew->det, previous_role);
    }
    return en;
}

ie_ent *proj_use_ender_eye(ie_world *iew, double player_x, double player_y,
                           double player_z, float player_y_offset,
                           int creative, int *stack_count)
{
    ie_ent *en = proj_throw_ender_eye(iew, player_x, player_y, player_z,
                                      player_y_offset);
    if (en && !creative && stack_count) --*stack_count;
    return en;
}

/* (int)Math.round(v) */
static int round_int(double v)
{
    return (int)(int64_t)floor(v + 0.5);
}

static void ender_eye_update(ie_world *iew, ie_ent *en)
{
    struct entity *e = &en->e;
    en->last_tick_x = e->pos_x;
    en->last_tick_y = e->pos_y;
    en->last_tick_z = e->pos_z;
    e->pos_x += e->motion_x;
    e->pos_y += e->motion_y;
    e->pos_z += e->motion_z;
    float speed = (float)sqrt(e->motion_x * e->motion_x + e->motion_z * e->motion_z);
    en->rotation_yaw = (float)(fd_atan2(e->motion_x, e->motion_z) * 180.0 / 3.141592653589793);
    en->rotation_pitch = (float)(fd_atan2(e->motion_y, (double)speed) * 180.0 / 3.141592653589793);
    while (en->rotation_pitch - en->prev_pitch < -180.0F) en->prev_pitch -= 360.0F;
    while (en->rotation_pitch - en->prev_pitch >= 180.0F) en->prev_pitch += 360.0F;
    while (en->rotation_yaw - en->prev_yaw < -180.0F) en->prev_yaw -= 360.0F;
    while (en->rotation_yaw - en->prev_yaw >= 180.0F) en->prev_yaw += 360.0F;
    en->rotation_pitch = en->prev_pitch + (en->rotation_pitch - en->prev_pitch) * 0.2F;
    en->rotation_yaw = en->prev_yaw + (en->rotation_yaw - en->prev_yaw) * 0.2F;

    double dx = en->eye_target_x - e->pos_x;
    double dz = en->eye_target_z - e->pos_z;
    float remaining = (float)sqrt(dx * dx + dz * dz);
    float angle = (float)fd_atan2(dz, dx);
    double next_speed = (double)speed + (double)(remaining - speed) * 0.0025;
    if (remaining < 1.0F)
    {
        next_speed *= 0.8;
        e->motion_y *= 0.8;
    }
    e->motion_x = fd_cos((double)angle) * next_speed;
    e->motion_z = fd_sin((double)angle) * next_speed;
    if (e->pos_y < en->eye_target_y)
        e->motion_y += (1.0 - e->motion_y) * 0.014999999664723873;
    else
        e->motion_y += (-1.0 - e->motion_y) * 0.014999999664723873;

    if (!en->in_water)
    {
        (void)det_rng_double(&en->rand);
        (void)det_rng_double(&en->rand);
    }
    entity_set_position(e, e->pos_x, e->pos_y, e->pos_z);
    if (++en->eye_timer > 80)
    {
        en->is_dead = 1;
        if (en->eye_drop)
        {
            if (iew->spawn_item)
                (void)iew->spawn_item(iew, e->pos_x, e->pos_y, e->pos_z, 381, 0, 1);
            else
            {
                ie_ent *drop = ie_spawn_item(iew, e->pos_x, e->pos_y, e->pos_z, 381, 0, 1);
                if (drop) ie_added_to_world(iew, drop);
            }
        }
        /* the shatter: playAuxSFX(2003) at Math.round of the position */
        else env_aux_sfx(iew->w, 2003, round_int(e->pos_x), round_int(e->pos_y), round_int(e->pos_z), 0);
    }
}

static void egg_on_impact(ie_world *iew, ie_ent *en)
{
    int r8 = det_rng_int_n(&en->rand, 8);
    if (r8 == 0)
    {
        int count = 1;
        if (det_rng_int_n(&en->rand, 32) == 0) count = 4;
        for (int k = 0; k < count; ++k)
        {
            if (iew->on_egg_chicken) iew->on_egg_chicken(iew, en);
            else
            {
                /* Existing projectile recordings deliberately discard the
                 * hatchlings after their constructors. */
                det_next_entity_id_role(iew->det, iew->role);
                det_rng ch_rand = det_new_random_role(iew->det, iew->role);
                int64_t msb, lsb;
                det_uuid_role(iew->det, iew->role, &msb, &lsb);
                det_math_random_role(iew->det, iew->role);
                det_math_random_role(iew->det, iew->role);
                det_math_random_role(iew->det, iew->role);
                (void)det_rng_int_n(&ch_rand, 6000);
            }
        }
    } en->is_dead = 1;
}

/* EntityEnderPearl.onImpact after the hit entity's attack: the 32 portal
 * particles' arguments (posY + nextDouble * 2, nextGaussian, nextGaussian,
 * left to right), then the thrower's teleport and fall damage on the server. */
static void pearl_on_impact(ie_world *iew, ie_ent *en)
{
    for (int k = 0; k < 32; ++k)
    {
        (void)det_rng_double(&en->rand);
        (void)det_rng_gaussian(&en->rand);
        (void)det_rng_gaussian(&en->rand);
    }
    if (iew->on_pearl_impact) iew->on_pearl_impact(iew, en);
    en->is_dead = 1;
}

/* EntityPotion.onImpact on the server: the splash's effects, then
 * playAuxSFX(2002) at the rounded position with the potion's damage */
static void potion_splash(ie_world *iew, ie_ent *en, void *hit)
{
    if (iew->on_splash) iew->on_splash(iew, en, hit);
    env_aux_sfx(iew->w, 2002, round_int(en->e.pos_x), round_int(en->e.pos_y), round_int(en->e.pos_z),
                en->potion_damage);
}

static void exp_bottle_on_impact(ie_world *iew, ie_ent *en, double px, double py, double pz)
{
    env_aux_sfx(iew->w, 2002, round_int(en->e.pos_x), round_int(en->e.pos_y), round_int(en->e.pos_z), 0);
    int r1 = det_rng_int_n(&iew->world_rand, 5);
    int r2 = det_rng_int_n(&iew->world_rand, 5);
    int var2 = 3 + r1 + r2;
    while (var2 > 0)
    {
        int var3 = get_xp_split(var2);
        var2 -= var3;
        if (iew->spawn_orb)
        {
            (void)iew->spawn_orb(iew, px, py, pz, var3);
            continue;
        }
        ie_ent *orb = ie_spawn_orb(iew, px, py, pz, var3);
        if (orb)
        {
            orb->spawn_index = iew->next_spawn_index++;
            ie_added_to_world(iew, orb);
        }
    } en->is_dead = 1;
}

int proj_fireball_attack_from(ie_ent *fb, struct living *src, int has_entity)
{
    /* setBeenAttacked */
    fb->velocity_changed = 1;
    fb->e.velocity_changed = 1;
    if (src == NULL) return has_entity;

    /* the living's getLookVec (getLook(1.0F)) as the motion, a tenth of it as
     * the acceleration, and it becomes the shootingEntity */
    double lx, ly, lz;
    ghast_look_vec(src, &lx, &ly, &lz);
    fb->e.motion_x = lx;
    fb->e.motion_y = ly;
    fb->e.motion_z = lz;
    fb->accel_x = lx * 0.1;
    fb->accel_y = ly * 0.1;
    fb->accel_z = lz * 0.1;
    fb->shooter = lv_ref(src);
    fb->shooter_is_player = src->kind == HK_PLAYER;
    return 1;
}

void proj_arrow_hit_nonliving(ie_ent *arrow, ie_ent *other)
{
    float speed = (float)sqrt(arrow->e.motion_x * arrow->e.motion_x + arrow->e.motion_y * arrow->e.motion_y +
                              arrow->e.motion_z * arrow->e.motion_z);
    double dmg = (double)speed * arrow->arrow_damage;
    int v = (int)dmg;
    if (dmg > (double)v) ++v;
    if (arrow->is_critical) (void)det_rng_int_n(&arrow->rand, v / 2 + 2);
    if (arrow->e.fire > 0 && other->e.fire < 100) other->e.fire = 100;

    /* causeArrowDamage(this, shootingEntity), or (this, this) without one:
     * the source always has an entity */
    int landed;
    if (other->kind == IE_LARGE_FIREBALL)
        landed = proj_fireball_attack_from(other, lv_get(arrow->shooting_entity), 1);
    else
    {
        /* Entity.attackEntityFrom: setBeenAttacked, false */
        other->velocity_changed = 1;
        other->e.velocity_changed = 1;
        landed = 0;
    }

    if (landed)
    {
        (void)det_rng_float(&arrow->rand); /* the bowhit pitch */
        arrow->is_dead = 1;
    }
    else
    {
        arrow->e.motion_x *= -0.10000000149011612;
        arrow->e.motion_y *= -0.10000000149011612;
        arrow->e.motion_z *= -0.10000000149011612;
        arrow->rotation_yaw += 180.0F;
        arrow->prev_yaw += 180.0F;
        arrow->ticks_in_air = 0;
    }
}

static void apply_entity_hit(ie_world *iew, ie_ent *en, ie_ent *other)
{
    if (en->kind == IE_ARROW)
    {
        proj_arrow_hit_nonliving(en, other);
        return;
    }

    /* the snowball's, the egg's and the pearl's onImpact open with
     * entityHit.attackEntityFrom(causeThrownDamage(this, getThrower())), whose
     * entity is the thrower (none without one) */
    if ((en->kind == IE_SNOWBALL || en->kind == IE_EGG || en->kind == IE_ENDER_PEARL) &&
        other->kind == IE_LARGE_FIREBALL)
    {
        struct living *thrower = lv_get(en->shooter);
        (void)proj_fireball_attack_from(other, thrower, thrower != NULL);
    }
    else
    {
        other->velocity_changed = 1;
        other->e.velocity_changed = 1;
    }

    if (en->kind == IE_EGG)
    {
        egg_on_impact(iew, en);
    }
    else if (en->kind == IE_EXP_BOTTLE)
    {
        exp_bottle_on_impact(iew, en, en->e.pos_x, en->e.pos_y, en->e.pos_z);
    }
    else if (en->kind == IE_SMALL_FIREBALL)
    {
        if (other->e.fire < 100) other->e.fire = 100;
        en->is_dead = 1;
    }
    else if (en->kind == IE_POTION)
    {
        potion_splash(iew, en, other);
        en->is_dead = 1;
    }
    else if (en->kind == IE_ENDER_PEARL)
    {
        pearl_on_impact(iew, en);
    }
    else
    {
        /* Snowball, large fireball. The large fireball's onImpact runs
         * the victim's EntityFireball.attackEntityFrom with
         * causeFireballDamage(this, shootingEntity), whose entity is the
         * shooter, or the fireball itself without one */
        en->is_dead = 1;

        if (en->kind == IE_LARGE_FIREBALL && other->kind == IE_LARGE_FIREBALL)
            (void)proj_fireball_attack_from(other, lv_get(en->shooter), 1);
    }
}

static void apply_block_hit(ie_world *iew, ie_ent *en, const struct rt_mop *m)
{
    struct entity *e = &en->e;
    struct world *w = e->world;

    if (en->kind == IE_ARROW)
    {
        en->tile_x = m->x;
        en->tile_y = m->y;
        en->tile_z = m->z;
        en->in_tile = world_get_block(w, en->tile_x, en->tile_y, en->tile_z) & 4095;
        en->in_data = world_get_meta(w, en->tile_x, en->tile_y, en->tile_z);

        e->motion_x = (double)((float)(m->hx - e->pos_x));
        e->motion_y = (double)((float)(m->hy - e->pos_y));
        e->motion_z = (double)((float)(m->hz - e->pos_z));

        float len = (float)sqrt(e->motion_x * e->motion_x + e->motion_y * e->motion_y + e->motion_z * e->motion_z);
        e->pos_x -= e->motion_x / (double)len * 0.05000000074505806;
        e->pos_y -= e->motion_y / (double)len * 0.05000000074505806;
        e->pos_z -= e->motion_z / (double)len * 0.05000000074505806;

        det_rng_float(&en->rand); /* bowhit sound pitch */
        en->in_ground = 1;
        en->shake = 7;
        en->is_critical = 0;
    }
    else if (en->kind == IE_EGG)
    {
        egg_on_impact(iew, en);
    }
    else if (en->kind == IE_EXP_BOTTLE)
    {
        exp_bottle_on_impact(iew, en, e->pos_x, e->pos_y, e->pos_z);
    }
    else if (en->kind == IE_SMALL_FIREBALL)
    {
        int bx = m->x;
        int by = m->y;
        int bz = m->z;
        switch (m->type)
        {
        case 0: --by; break;
        case 1: ++by; break;
        case 2: --bz; break;
        case 3: ++bz; break;
        case 4: --bx; break;
        case 5: ++bx; break;
        }
        if ((world_get_block(w, bx, by, bz) & 4095) == 0)
        {
            world_set_block(w, bx, by, bz, 51 /* fire */, 0, 3);
        }
        en->is_dead = 1;
    }
    else if (en->kind == IE_POTION)
    {
        potion_splash(iew, en, NULL);
        en->is_dead = 1;
    }
    else if (en->kind == IE_ENDER_PEARL)
    {
        pearl_on_impact(iew, en);
    }
    else
    {
        /* Snowball, large fireball */
        en->is_dead = 1;
    }
}

/* Entity.func_145775_I at the end of EntityArrow.onUpdate's flight branch:
 * every cell the box shrunk by 0.001 touches, onEntityCollidedWithBlock with
 * the arrow. The overrides an arrow reaches: BlockTNT (a burning arrow primes
 * it), the wooden button (func_150046_n through the owner's hook), the
 * plates and the tripwire (world state through the world's hook),
 * soul sand's damping, cactus (Entity.attackEntityFrom's setBeenAttacked) and
 * the web's isInWeb, which nothing on an arrow reads, the portal's
 * setInPortal (the arrow's portal counter, see ie_set_in_portal) and the End
 * portal's travelToDimension(1). */
static void arrow_block_collisions(ie_world *iew, ie_ent *en)
{
    struct entity *e = &en->e;
    struct world *w = e->world;
    int x0 = mh_floor(e->bounding_box.min_x + 0.001);
    int y0 = mh_floor(e->bounding_box.min_y + 0.001);
    int z0 = mh_floor(e->bounding_box.min_z + 0.001);
    int x1 = mh_floor(e->bounding_box.max_x - 0.001);
    int y1 = mh_floor(e->bounding_box.max_y - 0.001);
    int z1 = mh_floor(e->bounding_box.max_z - 0.001);

    if (!world_check_chunks_exist(w, x0, y0, z0, x1, y1, z1)) return;

    for (int x = x0; x <= x1; ++x)
        for (int y = y0; y <= y1; ++y)
            for (int z = z0; z <= z1; ++z)
            {
                int id = world_get_block(w, x, y, z) & 4095;

                if (id == 46)
                {
                    if (e->fire > 0 && iew->on_arrow_tnt) iew->on_arrow_tnt(iew, en, x, y, z);
                }
                else if (id == 143)
                {
                    if (iew->on_arrow_button) iew->on_arrow_button(iew, en, x, y, z);
                }
                else if (id == 70 || id == 72 || id == 132)
                {
                    if (w->on_plate) w->on_plate(w->on_plate_ctx, x, y, z, id);
                }
                else if (id == 88)
                {
                    e->motion_x *= 0.4;
                    e->motion_z *= 0.4;
                }
                else if (id == 81)
                {
                    e->velocity_changed = 1;
                }
                else if (id == 90)
                {
                    ie_set_in_portal(en);
                }
                else if (id == 119)
                {
                    /* BlockEndPortal: travelToDimension(1) at once (an arrow
                     * neither rides nor is ridden) */
                    ie_end_portal(en);
                }
            }
}

void projectile_update(ie_world *iew, ie_ent *en)
{

    if (en->kind == IE_ENDER_EYE) { ender_eye_update(iew, en); return; }

    struct entity *e = &en->e;
    struct world *w = e->world;

    if (en->kind == IE_ARROW)
    {
        if (en->prev_pitch == 0.0F && en->prev_yaw == 0.0F)
        {
            float d = (float)sqrt(e->motion_x * e->motion_x + e->motion_z * e->motion_z);
            en->prev_yaw = en->rotation_yaw = (float)(atan2(e->motion_x, e->motion_z) * 180.0 / 3.141592653589793);
            en->prev_pitch = en->rotation_pitch = (float)(atan2(e->motion_y, (double)d) * 180.0 / 3.141592653589793);
        }

        int cur_in_tile = world_get_block(w, en->tile_x, en->tile_y, en->tile_z) & 4095;
        if (cur_in_tile != 0)
        {
            struct aabb pool_box;
            if (collide_pool_box(w, en->tile_x, en->tile_y, en->tile_z, &pool_box))
            {
                if (e->pos_x > pool_box.min_x && e->pos_x < pool_box.max_x &&
                    e->pos_y > pool_box.min_y && e->pos_y < pool_box.max_y &&
                    e->pos_z > pool_box.min_z && e->pos_z < pool_box.max_z)
                {
                    en->in_ground = 1;
                }
            }
        }

        if (en->shake > 0) --en->shake;

        if (en->in_ground)
        {
            int meta = world_get_meta(w, en->tile_x, en->tile_y, en->tile_z);
            if (cur_in_tile == en->in_tile && meta == en->in_data)
            {
                ++en->ticks_in_ground;
                if (en->ticks_in_ground == 1200)
                { en->is_dead = 1;
                }
            }
            else
            {
                en->in_ground = 0;
                e->motion_x *= (double)(det_rng_float(&en->rand) * 0.2F);
                e->motion_y *= (double)(det_rng_float(&en->rand) * 0.2F);
                e->motion_z *= (double)(det_rng_float(&en->rand) * 0.2F);
                en->ticks_in_ground = 0;
                en->ticks_in_air = 0;
            }
            return;
        }

        ++en->ticks_in_air;
        double cur_x = e->pos_x, cur_y = e->pos_y, cur_z = e->pos_z;
        double dest_x = cur_x + e->motion_x;
        double dest_y = cur_y + e->motion_y;
        double dest_z = cur_z + e->motion_z;

        struct rt_mop mop_result;
        int has_hit = raytrace_blocks(w, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, 0, 1, 0, &mop_result);
        if (has_hit)
        {
            dest_x = mop_result.hx;
            dest_y = mop_result.hy;
            dest_z = mop_result.hz;
        }

        struct aabb broad_box = aabb_add_coord(e->bounding_box, e->motion_x, e->motion_y, e->motion_z);
        broad_box = aabb_expand(broad_box, 1.0, 1.0, 1.0);

        IE_QUERY_LIST(candidates);
        int n_cand = entityquery_in_box(iew, broad_box, en, candidates, IE_MAX_ENTITIES);

        void *entity_hit = NULL;
        int hit_is_living = 0;
        double min_ent_dist = 0.0;

        for (int i = 0; i < n_cand; ++i)
        {
            ie_ent *other = candidates[i];
            if (!ie_can_be_collided_with(other)) continue;

            if (en->kind == IE_ARROW && (void *)other == (void *)lv_get(en->shooting_entity) && en->ticks_in_air < 5) continue;

            float border = 0.3F;
            struct aabb target_box = aabb_expand(other->e.bounding_box, (double)border, (double)border, (double)border);
            double ihx, ihy, ihz;
            if (aabb_calculate_intercept(&target_box, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, &ihx, &ihy, &ihz))
            {
                double d = sqrt((ihx - cur_x) * (ihx - cur_x) +
                                (ihy - cur_y) * (ihy - cur_y) +
                                (ihz - cur_z) * (ihz - cur_z));
                if (entity_hit == NULL || d < min_ent_dist)
                {
                    entity_hit = other;
                    hit_is_living = 0;
                    min_ent_dist = d;
                }
            }
        }

        if (iew->query_living)
        {
            void **living_cands ENV_LOCAL = envstack_take(AN_MAX_ENTITIES * sizeof *living_cands);
            int n_living = iew->query_living(iew, broad_box, en, living_cands, AN_MAX_ENTITIES);
            for (int i = 0; i < n_living; ++i)
            {
                struct aabb lbb;
                if (!iew->get_living_bb || !iew->get_living_bb(iew, living_cands[i], &lbb)) continue;
                struct an_ent *ae = (struct an_ent *)living_cands[i];
                if (en->kind == IE_ARROW && ae->is_living && ae->livh == en->shooting_entity && en->ticks_in_air < 5) continue;

                float border = 0.3F;
                struct aabb target_box = aabb_expand(lbb, (double)border, (double)border, (double)border);
                double ihx, ihy, ihz;
                if (aabb_calculate_intercept(&target_box, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, &ihx, &ihy, &ihz))
                {
                    double d = sqrt((ihx - cur_x) * (ihx - cur_x) +
                                    (ihy - cur_y) * (ihy - cur_y) +
                                    (ihz - cur_z) * (ihz - cur_z));
                    if (entity_hit == NULL || d < min_ent_dist)
                    {
                        entity_hit = living_cands[i];
                        hit_is_living = 1;
                        min_ent_dist = d;
                    }
                }
            }
        }

        if (iew->query_other)
        {
            void **other_cands ENV_LOCAL = envstack_take(IE_OTHER_MAX * sizeof *other_cands);
            struct aabb *other_boxes ENV_LOCAL = envstack_take(IE_OTHER_MAX * sizeof *other_boxes);
            int n_other = iew->query_other(iew, broad_box, other_cands, other_boxes, IE_OTHER_MAX);
            for (int i = 0; i < n_other; ++i)
            {
                struct aabb target_box = aabb_expand(other_boxes[i], (double)0.3F, (double)0.3F, (double)0.3F);
                double ihx, ihy, ihz;
                if (aabb_calculate_intercept(&target_box, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, &ihx, &ihy, &ihz))
                {
                    double d = sqrt((ihx - cur_x) * (ihx - cur_x) +
                                    (ihy - cur_y) * (ihy - cur_y) +
                                    (ihz - cur_z) * (ihz - cur_z));
                    if (entity_hit == NULL || d < min_ent_dist)
                    {
                        entity_hit = other_cands[i];
                        hit_is_living = 2;
                        min_ent_dist = d;
                    }
                }
            }
        }


        if (entity_hit != NULL && hit_is_living == 2)
        {
            if (iew->on_other_hit) iew->on_other_hit(iew, en, entity_hit);
            else en->is_dead = 1;
        }
        else if (entity_hit != NULL)
        {
            if (hit_is_living)
            {
                if (en->kind == IE_POTION)
                {
                    potion_splash(iew, en, entity_hit);
                    en->is_dead = 1;
                }
                else if (en->kind == IE_ARROW)
                {
                    if (iew->on_arrow_hit) iew->on_arrow_hit(iew, en, entity_hit, 1);
                    else en->is_dead = 1;
                }
                else
                {
                    en->is_dead = 1;
                }
            }
            else
            {
                if (en->kind == IE_ARROW && iew->on_arrow_hit)
                {
                    iew->on_arrow_hit(iew, en, entity_hit, 0);
                }
                else
                {
                    apply_entity_hit(iew, en, (ie_ent *)entity_hit);
                }
            }
        }
        else if (has_hit)
        {
            apply_block_hit(iew, en, &mop_result);
        }

        e->pos_x += e->motion_x;
        e->pos_y += e->motion_y;
        e->pos_z += e->motion_z;

        update_rotations(en, e->motion_x, e->motion_y, e->motion_z, 0);

        float drag = en->in_water ? 0.8F : 0.99F;
        float gravity = 0.05F;

        if (en->in_water) e->fire = 0;

        e->motion_x *= (double)drag;
        e->motion_y *= (double)drag;
        e->motion_z *= (double)drag;
        e->motion_y -= (double)gravity;
        entity_set_position(e, e->pos_x, e->pos_y, e->pos_z);
        arrow_block_collisions(iew, en);
    }
    else if (en->kind >= IE_SNOWBALL && en->kind <= IE_POTION)
    {
        if (en->shake > 0) --en->shake;

        if (en->in_ground)
        {
            int cur_in_tile = world_get_block(w, en->tile_x, en->tile_y, en->tile_z) & 4095;
            if (cur_in_tile == en->in_tile)
            {
                ++en->ticks_in_ground; en->is_dead = 1;
                return;
            }
            en->in_ground = 0;
            e->motion_x *= (double)(det_rng_float(&en->rand) * 0.2F);
            e->motion_y *= (double)(det_rng_float(&en->rand) * 0.2F);
            e->motion_z *= (double)(det_rng_float(&en->rand) * 0.2F);
            en->ticks_in_ground = 0;
            en->ticks_in_air = 0;
        }
        else
        {
            ++en->ticks_in_air;
        }

        double cur_x = e->pos_x, cur_y = e->pos_y, cur_z = e->pos_z;
        double dest_x = cur_x + e->motion_x;
        double dest_y = cur_y + e->motion_y;
        double dest_z = cur_z + e->motion_z;

        struct rt_mop mop_result;
        int has_hit = raytrace_blocks(w, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, 0, 0, 0, &mop_result);

        if (has_hit)
        {
            dest_x = mop_result.hx;
            dest_y = mop_result.hy;
            dest_z = mop_result.hz;
        }

        /* getThrower, every tick until a thrower known by name is found */
        if (en->owner_pending && iew->resolve_thrower) iew->resolve_thrower(iew, en);

        struct aabb broad_box = aabb_add_coord(e->bounding_box, e->motion_x, e->motion_y, e->motion_z);
        broad_box = aabb_expand(broad_box, 1.0, 1.0, 1.0);

        IE_QUERY_LIST(candidates);
        int n_cand = entityquery_in_box(iew, broad_box, en, candidates, IE_MAX_ENTITIES);

        void *entity_hit = NULL;
        int hit_is_living = 0;
        double min_ent_dist = 0.0;

        for (int i = 0; i < n_cand; ++i)
        {
            ie_ent *other = candidates[i];
            if (!ie_can_be_collided_with(other)) continue;
            if ((void *)other == (void *)lv_get(en->shooting_entity) && en->ticks_in_air < 5) continue;

            float border = 0.3F;
            struct aabb target_box = aabb_expand(other->e.bounding_box, (double)border, (double)border, (double)border);
            double ihx, ihy, ihz;
            if (aabb_calculate_intercept(&target_box, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, &ihx, &ihy, &ihz))
            {
                double d = sqrt((ihx - cur_x) * (ihx - cur_x) +
                                (ihy - cur_y) * (ihy - cur_y) +
                                (ihz - cur_z) * (ihz - cur_z));
                if (entity_hit == NULL || d < min_ent_dist)
                {
                    entity_hit = other;
                    hit_is_living = 0;
                    min_ent_dist = d;
                }
            }
        }

        if (iew->query_living)
        {
            void **living_cands ENV_LOCAL = envstack_take(AN_MAX_ENTITIES * sizeof *living_cands);
            int n_living = iew->query_living(iew, broad_box, en, living_cands, AN_MAX_ENTITIES);
            for (int i = 0; i < n_living; ++i)
            {
                struct aabb lbb;
                if (!iew->get_living_bb || !iew->get_living_bb(iew, living_cands[i], &lbb)) continue;
                struct an_ent *ae = (struct an_ent *)living_cands[i];
                if (ae->is_living && ae->livh == en->shooting_entity && en->ticks_in_air < 5) continue;

                float border = 0.3F;
                struct aabb target_box = aabb_expand(lbb, (double)border, (double)border, (double)border);
                double ihx, ihy, ihz;
                if (aabb_calculate_intercept(&target_box, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, &ihx, &ihy, &ihz))
                {
                    double d = sqrt((ihx - cur_x) * (ihx - cur_x) +
                                    (ihy - cur_y) * (ihy - cur_y) +
                                    (ihz - cur_z) * (ihz - cur_z));
                    if (entity_hit == NULL || d < min_ent_dist)
                    {
                        entity_hit = living_cands[i];
                        hit_is_living = 1;
                        min_ent_dist = d;
                    }
                }
            }
        }

        if (iew->query_other)
        {
            void **other_cands ENV_LOCAL = envstack_take(IE_OTHER_MAX * sizeof *other_cands);
            struct aabb *other_boxes ENV_LOCAL = envstack_take(IE_OTHER_MAX * sizeof *other_boxes);
            int n_other = iew->query_other(iew, broad_box, other_cands, other_boxes, IE_OTHER_MAX);
            for (int i = 0; i < n_other; ++i)
            {
                struct aabb target_box = aabb_expand(other_boxes[i], (double)0.3F, (double)0.3F, (double)0.3F);
                double ihx, ihy, ihz;
                if (aabb_calculate_intercept(&target_box, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, &ihx, &ihy, &ihz))
                {
                    double d = sqrt((ihx - cur_x) * (ihx - cur_x) +
                                    (ihy - cur_y) * (ihy - cur_y) +
                                    (ihz - cur_z) * (ihz - cur_z));
                    if (entity_hit == NULL || d < min_ent_dist)
                    {
                        entity_hit = other_cands[i];
                        hit_is_living = 2;
                        min_ent_dist = d;
                    }
                }
            }
        }

        if (entity_hit != NULL && hit_is_living == 2)
        {
            /* EntityThrowable.onImpact's entity half on the crystal or the
             * part (attackEntityFrom), then the kind's own impact */
            if (iew->on_other_hit) iew->on_other_hit(iew, en, entity_hit);
            if (en->kind == IE_EGG) egg_on_impact(iew, en);
            else if (en->kind == IE_ENDER_PEARL) pearl_on_impact(iew, en);
            else if (en->kind == IE_EXP_BOTTLE) exp_bottle_on_impact(iew, en, e->pos_x, e->pos_y, e->pos_z);
            else if (en->kind == IE_POTION) { potion_splash(iew, en, NULL); en->is_dead = 1; }
            else en->is_dead = 1;
        }
        else if (entity_hit != NULL)
        {
            if (hit_is_living)
            {
                if (en->kind == IE_POTION)
                {
                    potion_splash(iew, en, entity_hit);
                    en->is_dead = 1;
                }
                else if (en->kind == IE_SNOWBALL)
                {
                    struct an_ent *ae = (struct an_ent *)entity_hit;
                    if (ae->is_living && ae->livh != 0)
                    {
                        float dmg = (lv_get(ae->livh)->kind == HK_BLAZE) ? 3.0f : 0.0f;
                        living_attack_entity_from_attacker(lv_get(ae->livh), lv_get(en->shooter), DMG_THROWN, dmg, iew->det);
                    }
                    en->is_dead = 1;
                }
                else if (en->kind == IE_EGG || en->kind == IE_ENDER_PEARL)
                {
                    /* entityHit.attackEntityFrom(causeThrownDamage, 0.0F),
                     * then the rest of onImpact */
                    struct an_ent *ae = (struct an_ent *)entity_hit;
                    if (ae->is_living && ae->livh != 0)
                        living_attack_entity_from_attacker(lv_get(ae->livh), lv_get(en->shooter), DMG_THROWN, 0.0f, iew->det);
                    if (en->kind == IE_EGG) egg_on_impact(iew, en);
                    else pearl_on_impact(iew, en);
                }
                else if (en->kind == IE_EXP_BOTTLE)
                {
                    /* EntityExpBottle.onImpact ignores what it hit: the
                     * orbs spill on a living too */
                    exp_bottle_on_impact(iew, en, e->pos_x, e->pos_y, e->pos_z);
                }
                else
                {
                    en->is_dead = 1;
                }
            }
            else
            {
                apply_entity_hit(iew, en, (ie_ent *)entity_hit);
            }
        }
        else if (has_hit)
        {
            /* EntityThrowable: a block hit on a portal is setInPortal, not
             * onImpact */
            if ((world_get_block(w, mop_result.x, mop_result.y, mop_result.z) & 4095) == 90)
                ie_set_in_portal(en);
            else
                apply_block_hit(iew, en, &mop_result);
        }

        e->pos_x += e->motion_x;
        e->pos_y += e->motion_y;
        e->pos_z += e->motion_z;

        update_rotations(en, e->motion_x, e->motion_y, e->motion_z, 0);

        float drag = en->in_water ? 0.8F : 0.99F;
        float gravity = (en->kind == IE_EXP_BOTTLE) ? 0.07F : (en->kind == IE_POTION ? 0.05F : 0.03F);

        e->motion_x *= (double)drag;
        e->motion_y *= (double)drag;
        e->motion_z *= (double)drag;
        e->motion_y -= (double)gravity;
        entity_set_position(e, e->pos_x, e->pos_y, e->pos_z);
    }
    else if (en->kind == IE_LARGE_FIREBALL || en->kind == IE_SMALL_FIREBALL)
    {
        if (e->fire < 20) e->fire = 20;

        if (en->in_ground)
        {
            int cur_in_tile = world_get_block(w, en->tile_x, en->tile_y, en->tile_z) & 4095;
            if (cur_in_tile == en->in_tile)
            {
                ++en->ticks_alive; en->is_dead = 1;
                return;
            }
            en->in_ground = 0;
            e->motion_x *= (double)(det_rng_float(&en->rand) * 0.2F);
            e->motion_y *= (double)(det_rng_float(&en->rand) * 0.2F);
            e->motion_z *= (double)(det_rng_float(&en->rand) * 0.2F);
            en->ticks_alive = 0;
            en->ticks_in_air = 0;
        }
        else
        {
            ++en->ticks_in_air;
        }

        double cur_x = e->pos_x, cur_y = e->pos_y, cur_z = e->pos_z;
        double dest_x = cur_x + e->motion_x;
        double dest_y = cur_y + e->motion_y;
        double dest_z = cur_z + e->motion_z;

        struct rt_mop mop_result;
        int has_hit = raytrace_blocks(w, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, 0, 0, 0, &mop_result);
        if (has_hit)
        {
            dest_x = mop_result.hx;
            dest_y = mop_result.hy;
            dest_z = mop_result.hz;
        }

        struct aabb broad_box = aabb_add_coord(e->bounding_box, e->motion_x, e->motion_y, e->motion_z);
        broad_box = aabb_expand(broad_box, 1.0, 1.0, 1.0);

        IE_QUERY_LIST(candidates);
        int n_cand = entityquery_in_box(iew, broad_box, en, candidates, IE_MAX_ENTITIES);

        ie_ent *entity_hit = NULL;
        double min_ent_dist = 0.0;

        for (int i = 0; i < n_cand; ++i)
        {
            ie_ent *other = candidates[i];
            if (!ie_can_be_collided_with(other)) continue;

            float border = 0.3F;
            struct aabb target_box = aabb_expand(other->e.bounding_box, (double)border, (double)border, (double)border);
            double ihx, ihy, ihz;
            if (aabb_calculate_intercept(&target_box, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, &ihx, &ihy, &ihz))
            {
                double d = sqrt((ihx - cur_x) * (ihx - cur_x) +
                                (ihy - cur_y) * (ihy - cur_y) +
                                (ihz - cur_z) * (ihz - cur_z));
                if (entity_hit == NULL || d < min_ent_dist)
                {
                    entity_hit = other;
                    min_ent_dist = d;
                }
            }
        }

        /* the living entities and the player: Java's scan is one pass over
         * every entity, so the same border-0.3 intercept and the same
         * nearest-hit test decide; the pools are walked after the ie_world's
         * and a strictly smaller distance replaces a candidate from either */
        void *living_hit = NULL;
        int living_kind_hit = 0;   /* 1 living, 2 player */

        if (iew->query_living)
        {
            void **living_cands ENV_LOCAL = envstack_take(AN_MAX_ENTITIES * sizeof *living_cands);
            int n_living = iew->query_living(iew, broad_box, en, living_cands, AN_MAX_ENTITIES);
            for (int i = 0; i < n_living; ++i)
            {
                struct aabb lbb;
                if (!iew->get_living_bb || !iew->get_living_bb(iew, living_cands[i], &lbb)) continue;

                /* the shooter is skipped until 25 ticks in air, like the
                 * ie_world pass */
                struct an_ent *ae = (struct an_ent *)living_cands[i];

                if (ae->livh == en->shooter && en->ticks_in_air < 25) continue;

                float border = 0.3F;
                struct aabb target_box = aabb_expand(lbb, (double)border, (double)border, (double)border);
                double ihx, ihy, ihz;
                if (aabb_calculate_intercept(&target_box, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, &ihx, &ihy, &ihz))
                {
                    double d = sqrt((ihx - cur_x) * (ihx - cur_x) +
                                    (ihy - cur_y) * (ihy - cur_y) +
                                    (ihz - cur_z) * (ihz - cur_z));
                    if (entity_hit == NULL && living_hit == NULL)
                    {
                        living_hit = living_cands[i];
                        living_kind_hit = 1;
                        min_ent_dist = d;
                    }
                    else if (d < min_ent_dist)
                    {
                        living_hit = living_cands[i];
                        living_kind_hit = 1;
                        entity_hit = NULL;
                        min_ent_dist = d;
                    }
                }
            }
        }

        if (iew->user_data)
        {
            /* the player: the caller exposes its box through the ghast lane's
             * callback; the test is the same border-0.3 intercept */
            struct aabb pbb;

            if (ghast_player_bb(iew, &pbb)
                && !(en->shooter_is_player && en->ticks_in_air < 25))
            {
                float border = 0.3F;
                struct aabb target_box = aabb_expand(pbb, (double)border, (double)border, (double)border);
                double ihx, ihy, ihz;
                if (aabb_calculate_intercept(&target_box, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, &ihx, &ihy, &ihz))
                {
                    double d = sqrt((ihx - cur_x) * (ihx - cur_x) +
                                    (ihy - cur_y) * (ihy - cur_y) +
                                    (ihz - cur_z) * (ihz - cur_z));
                    if ((entity_hit == NULL && living_hit == NULL) || d < min_ent_dist)
                    {
                        living_hit = NULL;
                        entity_hit = NULL;
                        living_kind_hit = 2;
                        min_ent_dist = d;
                    }
                }
            }
        }

        /* the collidable entities outside the pools (a primed TNT, a falling
         * block, a painting or an item frame, the End's crystals and the
         * dragon's parts): the same intercept and nearest-hit test */
        void *other_hit = NULL;

        if (iew->query_other)
        {
            void *other_cands[64];
            struct aabb *other_boxes ENV_LOCAL = envstack_take(64 * sizeof *other_boxes);
            int n_other = iew->query_other(iew, broad_box, other_cands, other_boxes, 64);
            for (int i = 0; i < n_other; ++i)
            {
                struct aabb target_box = aabb_expand(other_boxes[i], (double)0.3F, (double)0.3F, (double)0.3F);
                double ihx, ihy, ihz;
                if (aabb_calculate_intercept(&target_box, cur_x, cur_y, cur_z, dest_x, dest_y, dest_z, &ihx, &ihy, &ihz))
                {
                    double d = sqrt((ihx - cur_x) * (ihx - cur_x) +
                                    (ihy - cur_y) * (ihy - cur_y) +
                                    (ihz - cur_z) * (ihz - cur_z));
                    if ((entity_hit == NULL && living_kind_hit == 0 && other_hit == NULL) || d < min_ent_dist)
                    {
                        other_hit = other_cands[i];
                        entity_hit = NULL;
                        living_hit = NULL;
                        living_kind_hit = 0;
                        min_ent_dist = d;
                    }
                }
            }
        }

        if (other_hit != NULL)
        {
            /* onImpact: the hit's attackEntityFrom, then the large fireball's
             * explosion */
            if (iew->on_other_hit) iew->on_other_hit(iew, en, other_hit);
            if (en->kind == IE_LARGE_FIREBALL && iew->on_fireball_impact)
                iew->on_fireball_impact(iew, en, NULL, 0);
            en->is_dead = 1;
        }
        else if (living_kind_hit != 0)
        {
            trace("fbhit", "iiii", en->entity_id, living_kind_hit,
                  living_kind_hit == 1 ? lv_get(((struct an_ent *)living_hit)->livh)->entity_id : -1,
                  en->ticks_in_air);
            if (iew->on_fireball_impact) iew->on_fireball_impact(iew, en, living_hit, living_kind_hit);
            en->is_dead = 1;
        }
        else if (entity_hit != NULL)
        {
            trace("fbhit", "iiii", en->entity_id, 0, entity_hit->entity_id, en->ticks_in_air);
            apply_entity_hit(iew, en, entity_hit);

            /* EntityLargeFireball.onImpact: the explosion follows the hit */
            if (en->kind == IE_LARGE_FIREBALL && iew->on_fireball_impact)
            {
                iew->on_fireball_impact(iew, en, NULL, 0);
            }

            en->is_dead = 1;
        }
        else if (has_hit)
        {
            trace("fbhit", "iiii", en->entity_id, 3, -1, en->ticks_in_air);
            apply_block_hit(iew, en, &mop_result);

            if (en->kind == IE_LARGE_FIREBALL && iew->on_fireball_impact)
            {
                iew->on_fireball_impact(iew, en, NULL, 0);
            }

            en->is_dead = 1;
        }

        e->pos_x += e->motion_x;
        e->pos_y += e->motion_y;
        e->pos_z += e->motion_z;

        update_rotations(en, e->motion_x, e->motion_y, e->motion_z, 1);

        float factor = en->in_water ? 0.8F : 0.95F;
        e->motion_x += en->accel_x;
        e->motion_y += en->accel_y;
        e->motion_z += en->accel_z;
        e->motion_x *= (double)factor;
        e->motion_y *= (double)factor;
        e->motion_z *= (double)factor;
        entity_set_position(e, e->pos_x, e->pos_y, e->pos_z);
    }
}
