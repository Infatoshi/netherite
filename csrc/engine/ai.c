/* The entity AI of 1.7.10 for the farm animals: EntityAITasks with its tick rate
 * and mutex bits, the ten task classes the animals register, EntityLookHelper,
 * EntityMoveHelper, EntityJumpHelper, EntityBodyHelper, EntitySenses,
 * PathNavigate on top of the pathfinder (csrc/engine/pathfind.c) and
 * RandomPositionGenerator. Ported from oracle/src/entity/ai and
 * oracle/src/pathfinding/PathNavigate.java.
 *
 * Dead by construction in the probe's world, kept as their Java shape so the
 * record matches: EntityAITempt, EntityAIWatchClosest and
 * EntityAIControlledByPlayer never find a player, so their shouldExecute draws
 * happen but nothing starts, and EntitySenses.canSee is never reached (no
 * targeting task is registered).
 *
 * PathNavigate's World.getPathEntityToEntity / getEntityPathToXYZ go through
 * pathfind.c; the entity's width, height and bounding box are the pathfinder's
 * view of it. */
#include "ai.h"
#include "env.h"
#include "hostiles_creeper.h"
#include "hostiles_witch.h"
#include "hostiles.h"
#include "hostiles_skeleton.h"
#include "hostiles_spider.h"
#include "hostiles_enderman.h"
#include "hostiles_silverfish.h"
#include "hostiles_pigman.h"
#include "hostiles_blaze.h"
#include "slimes.h"
#include "animals.h"
#include "ghasts.h"
#include "villagers.h"
#include "raytrace.h"
#include "riding.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "blocks.h"
#include "jmath.h"
#include "living.h"
#include "pathfind.h"
#include "smath.h"
#include "trace.h"
#include "world.h"
#include "biomes.h"

struct living;
static void ai_block_break_event(struct living *l, int x, int y, int z, int stage);

/* MathHelper.sqrt_double: the square root, rounded to float and widened back. */
static double sqrt_double(double v)
{
    return (double)(float)sqrt(v);
}

static float wrap_angle_float(float a)
{
    /* fmodf is exact: an angle inside (-360, 360) is its own remainder */
    if (!(a > -360.0F && a < 360.0F)) a = fmodf(a, 360.0F);
    if (a >= 180.0F) a -= 360.0F;
    if (a < -180.0F) a += 360.0F;
    return a;
}

static int ceil_float_int(float f)
{
    int i = (int)f;
    return f > (float)i ? i + 1 : i;
}


/* Block.getBlocksMovement: the NEGATION of the material's flag, with the
 * overrides the pathfinder depends on: the liquids (water blocks, lava does
 * not), the fence and wall classes (never), the pressure plates and signs
 * (always), and the door, gate and trapdoor open bits. */
static int blocks_movement(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;
    const struct block_def *b = &BLOCKS[id];
    const char *cn = b->class_name;
    int meta = world_get_meta(w, x, y, z);

    /* BlockLiquid.getBlocksMovement, inherited by the static and dynamic
     * forms: passable unless lava */
    if (strcmp(cn, "BlockLiquid") == 0 || strcmp(cn, "BlockStaticLiquid") == 0 ||
        strcmp(cn, "BlockDynamicLiquid") == 0) return b->material != 7;

    if (strcmp(cn, "BlockFence") == 0 || strcmp(cn, "BlockWall") == 0) return 0;

    /* BlockBasePressurePlate's override, inherited by both plate classes */
    if (strcmp(cn, "BlockPressurePlate") == 0 || strcmp(cn, "BlockPressurePlateWeighted") == 0 ||
        strcmp(cn, "BlockSign") == 0) return 1;

    if (strcmp(cn, "BlockFenceGate") == 0) return (meta & 4) != 0;

    if (strcmp(cn, "BlockTrapDoor") == 0) return (meta & 4) == 0;

    if (strcmp(cn, "BlockDoor") == 0)
    {
        /* BlockDoor.func_150012_g: the halves combine, the open bit is 4 */
        int lower = (meta & 8) != 0 ? world_get_block(w, x, y - 1, z) & 4095 : id;
        int lower_meta = (meta & 8) != 0 ? world_get_meta(w, x, y - 1, z) : meta;
        int upper_meta = (meta & 8) != 0 ? meta : world_get_meta(w, x, y + 1, z);
        int combined = (lower_meta & 7) | (upper_meta & 8);

        if (lower == id && (upper_meta & 8) != 0) return (combined & 4) != 0;
    }

    return !MATERIALS[b->material].blocks_movement;
}

static double dist_sq_to(struct living *a, struct living *b)
{
    double dx = a->e.pos_x - b->e.pos_x;
    double dy = a->e.pos_y - b->e.pos_y;
    double dz = a->e.pos_z - b->e.pos_z;
    return dx * dx + dy * dy + dz * dz;
}

/* ------------------------------------------------------------- the helpers */

void senses_clear(struct living *l)
{
    nw_env->senses.owner = lv_ref(l);
    nw_env->senses.s.nseen = 0;
    nw_env->senses.s.nunseen = 0;
}

int senses_can_see(struct living *l, struct living *other)
{
    struct senses *se = &nw_env->senses.s;
    lref ro = lv_ref(other);

    /* only l's own AI tasks read its senses, after its updateAITasks
     * cleared them (living.c living_update_ai_tasks) */
    if (nw_env->senses.owner != lv_ref(l))
    {
        fprintf(stderr, "ai: living %d reads senses it did not clear\n", l->entity_id);
        abort();
    }

    for (int i = 0; i < se->nseen; ++i)
        if (se->seen[i] == ro) return 1;

    for (int i = 0; i < se->nunseen; ++i)
        if (se->unseen[i] == ro) return 0;

    double sx = l->e.pos_x;
    double sy = l->e.pos_y + (double)living_eye_height(l);
    double sz = l->e.pos_z;

    double ex = other->e.pos_x;
    double ey = other->e.pos_y + (double)living_eye_height(other);
    double ez = other->e.pos_z;

    struct rt_mop mop;
    int hit = raytrace_trace(l->world, sx, sy, sz, ex, ey, ez, &mop);
    int can_see = (hit == 0);

    if (can_see)
    {
        if (se->nseen < 64) se->seen[se->nseen++] = ro;
    }
    else
    {
        if (se->nunseen < 64) se->unseen[se->nunseen++] = ro;
    }

    return can_see;
}

void look_helper_update(struct living *l)
{
    l->rotation_pitch = 0.0F;

    if (l->look.is_looking)
    {
        l->look.is_looking = 0;
        double v1 = l->look.x - l->e.pos_x;
        double v3 = l->look.y - (l->e.pos_y + (double)living_eye_height(l));
        double v5 = l->look.z - l->e.pos_z;
        double v7 = sqrt_double(v1 * v1 + v5 * v5);
        float v9 = (float)(fd_atan2(v5, v1) * 180.0 / 3.141592653589793) - 90.0F;
        float v10 = (float)(-(fd_atan2(v3, v7) * 180.0 / 3.141592653589793));
        l->rotation_pitch = living_update_rotation(l->rotation_pitch, v10, l->look.delta_look_pitch);
        l->rotation_yaw_head = living_update_rotation(l->rotation_yaw_head, v9, l->look.delta_look_yaw);
    }
    else
    {
        l->rotation_yaw_head = living_update_rotation(l->rotation_yaw_head, l->render_yaw_offset, 10.0F);
    }

    float v11 = wrap_angle_float(l->rotation_yaw_head - l->render_yaw_offset);

    if (!nav_no_path(l))
    {
        if (v11 < -75.0F) l->rotation_yaw_head = l->render_yaw_offset - 75.0F;
        if (v11 > 75.0F) l->rotation_yaw_head = l->render_yaw_offset + 75.0F;
    }
}

void look_set_position_with_entity(struct living *l, struct living *other, float dyaw, float dpitch)
{
    l->look.x = other->e.pos_x;
    l->look.y = other->e.pos_y + (double)living_eye_height(other);
    l->look.z = other->e.pos_z;
    l->look.delta_look_yaw = dyaw;
    l->look.delta_look_pitch = dpitch;
    l->look.is_looking = 1;
}

void look_set_position(struct living *l, double x, double y, double z, float dyaw, float dpitch)
{
    l->look.x = x;
    l->look.y = y;
    l->look.z = z;
    l->look.delta_look_yaw = dyaw;
    l->look.delta_look_pitch = dpitch;
    l->look.is_looking = 1;
}

void move_helper_set_move_to(struct living *l, double x, double y, double z, double speed)
{
    l->move.x = x;
    l->move.y = y;
    l->move.z = z;
    l->move.speed = speed;
    l->move.update = 1;
}

void move_helper_update(struct living *l)
{
    /* setMoveForward(0.0F): landMovementFactor (getAIMoveSpeed) keeps the
     * last speed set, which EntityAIControlledByPlayer's move reads */
    l->move_forward = 0.0F;

    if (!l->move.update) return;

    l->move.update = 0;
    int v1 = mh_floor(l->e.bounding_box.min_y + 0.5);
    double v2 = l->move.x - l->e.pos_x;
    double v4 = l->move.z - l->e.pos_z;
    double v6 = l->move.y - (double)v1;
    double v8 = v2 * v2 + v6 * v6 + v4 * v4;

    if (v8 >= 2.500000277905201E-7)
    {
        float v10 = (float)(fd_atan2(v4, v2) * 180.0 / 3.141592653589793) - 90.0F;
        l->rotation_yaw = living_update_rotation(l->rotation_yaw, v10, 30.0F);
        living_set_ai_move_speed(l, (float)(l->move.speed * attrs_value(&l->attrs.a[ATTR_MOVEMENT_SPEED])));

        if (v6 > 0.0 && v2 * v2 + v4 * v4 < 1.0) l->jump.is_jumping = 1;
    }
}

void jump_helper_do(struct living *l)
{
    living_set_jumping(l, l->jump.is_jumping);
    l->jump.is_jumping = 0;
}

void body_helper_update(struct living *l)
{
    double v1 = l->e.pos_x - l->e.prev_pos_x;
    double v3 = l->e.pos_z - l->e.prev_pos_z;

    if (v1 * v1 + v3 * v3 > 2.500000277905201E-7)
    {
        l->render_yaw_offset = l->rotation_yaw;
        l->rotation_yaw_head = body_helper_a(l->render_yaw_offset, l->rotation_yaw_head, 75.0F);
        l->body.yaw = l->rotation_yaw_head;
        l->body.counter = 0;
    }
    else
    {
        float v5 = 75.0F;

        if (fabsf(l->rotation_yaw_head - l->body.yaw) > 15.0F)
        {
            l->body.counter = 0;
            l->body.yaw = l->rotation_yaw_head;
        }
        else
        {
            ++l->body.counter;

            if (l->body.counter > 10)
            {
                float v6 = 1.0F - (float)(l->body.counter - 10) / 10.0F;
                v5 = (v6 > 0.0F ? v6 : 0.0F) * 75.0F;
            }
        }

        l->render_yaw_offset = body_helper_a(l->rotation_yaw_head, l->render_yaw_offset, v5);
    }
}

float body_helper_a(float a, float b, float limit)
{
    float v4 = wrap_angle_float(a - b);

    if (v4 < -limit) v4 = -limit;
    if (v4 >= limit) v4 = limit;

    return a - v4;
}

/* ------------------------------------------------------------------- nav */

/* The path search's scratch: Java builds a new PathFinder per search, so
 * one finder and output buffer serve every living in turn (a search never
 * starts inside another; the finder's arrays are the environment's). The
 * finder reads the world the living's navigator first searched in
 * (PathNavigate keeps the worldObj it was made with). */
#define nav_shared (nw_env->ai.nav_shared)
#define nav_shared_busy (nw_env->ai.nav_shared_busy)

static struct nav_scratch *nav_scratch_take_world(struct world *w, struct world *entity_w)
{
    if (nav_shared_busy) abort();
    nav_shared_busy = 1;
    nav_shared.f.w = w;
    nav_shared.f.entity_w = entity_w;
    nav_shared.out = nw_arena()->pf.out;
    return &nav_shared;
}

static struct nav_scratch *nav_scratch_take(struct living *l)
{
    if (l->nav.finder_world == NULL) l->nav.finder_world = l->world;
    /* the navigator's world backs the ChunkCache; func_82565_a reads the
     * living's own (a traveller's old instance, whose navigator stayed in the
     * world it left, paths over the destination's blocks) */
    return nav_scratch_take_world(l->nav.finder_world, l->world);
}

static void nav_scratch_give(struct nav_scratch *s)
{
    (void)s;
    nav_shared_busy = 0;
}

/* The search's points as a new path, or 0 for no path (n < 0). */
static pathref nav_path_of(const int *out, int n, int set_points)
{
    if (n < 0) return 0;

    pathref h = path_alloc(n);
    struct path_ent *p = path_at(h);
    p->length = n;
    if (set_points) p->n_points = n;

    for (int i = 0; i < n; ++i)
    {
        p->pts[i][0] = out[i * 3 + 0];
        p->pts[i][1] = out[i * 3 + 1];
        p->pts[i][2] = out[i * 3 + 2];
    }

    return h;
}

static void nav_fill_entity(struct living *l, struct pf_entity *e)
{
    memset(e, 0, sizeof *e);
    e->pos_x = l->e.pos_x;
    e->pos_y = l->e.pos_y;
    e->pos_z = l->e.pos_z;
    e->width = l->e.width;
    e->height = l->e.height;
    e->box = l->e.bounding_box;
    e->in_water = l->is_in_water;
    e->max_safe_point_tries = living_max_safe_point_tries(l);
    e->y_offset = l->e.y_offset;
    e->y_size = l->e.y_size;
}

static int nav_pathable_y(struct living *l)
{
    if (l->is_in_water && l->nav.can_swim)
    {
        int y = (int)l->e.bounding_box.min_y;
        int id = world_get_block(l->world, mh_floor(l->e.pos_x), y, mh_floor(l->e.pos_z)) & 4095;
        int n = 0;

        do
        {
            if (id != 8 && id != 9) return y;

            ++y;
            id = world_get_block(l->world, mh_floor(l->e.pos_x), y, mh_floor(l->e.pos_z)) & 4095;
            ++n;
        }
        while (n <= 16);

        return (int)l->e.bounding_box.min_y;
    }

    return (int)(l->e.bounding_box.min_y + 0.5);
}

static int nav_can_navigate(struct living *l)
{
    if (l->e.on_ground) return 1;

    if (l->nav.can_swim && (l->is_in_water || living_handle_lava_movement(l))) return 1;

    /* the third clause: isRiding() && this is an EntityZombie (the pigman
     * too) && ridingEntity is an EntityChicken */
    if (lv_get(l->riding_entity) != NULL && (l->kind == HK_ZOMBIE || l->kind == HK_PIGMAN) &&
        lv_get(l->riding_entity)->kind == AK_CHICKEN) return 1;

    return 0;
}

static double nav_pos_x(struct living *l)
{
    return l->e.pos_x;
}

static void nav_entity_position(struct living *l, double *x, double *y, double *z)
{
    *x = nav_pos_x(l);
    *y = (double)nav_pathable_y(l);
    *z = l->e.pos_z;
}

static int nav_is_position_clear(struct living *l, int x0, int y0, int z0, int sx, int sy, int sz,
                                 double ox, double oz, double vx, double vz)
{
    for (int x = x0; x < x0 + sx; ++x)
    {
        for (int y = y0; y < y0 + sy; ++y)
        {
            for (int z = z0; z < z0 + sz; ++z)
            {
                double dx = (double)x + 0.5 - ox;
                double dz = (double)z + 0.5 - oz;

                if (dx * vx + dz * vz >= 0.0)
                {
                    if (!blocks_movement(l->world, x, y, z)) return 0;
                }
            }
        }
    }

    return 1;
}

static int nav_is_safe_to_stand_at(struct living *l, int px, int py, int pz, int sx, int sy, int sz,
                                   double ox, double oy, double oz, double vx, double vz)
{
    int x0 = px - sx / 2;
    int z0 = pz - sz / 2;

    if (!nav_is_position_clear(l, x0, py, z0, sx, sy, sz, ox, oz, vx, vz)) return 0;

    for (int x = x0; x < x0 + sx; ++x)
    {
        for (int z = z0; z < z0 + sz; ++z)
        {
            double dx = (double)x + 0.5 - ox;
            double dz = (double)z + 0.5 - oz;

            if (dx * vx + dz * vz >= 0.0)
            {
                int id = world_get_block(l->world, x, py - 1, z) & 4095;
                int mat = BLOCKS[id].material;

                if (mat == 0 || (mat == 6 && !l->is_in_water) || mat == 7) return 0;
            }
        }
    }

    (void)oy;
    return 1;
}

static int nav_direct_path(struct living *l, double fx, double fy, double fz, double tx, double ty, double tz,
                           int sx, int sy, int sz)
{
    int x0 = mh_floor(fx);
    int z0 = mh_floor(fz);
    double dx = tx - fx;
    double dz = tz - fz;
    double v12 = dx * dx + dz * dz;

    if (v12 < 1.0E-8) return 0;

    double v14 = 1.0 / sqrt(v12);
    dx *= v14;
    dz *= v14;
    sx += 2;
    sz += 2;

    if (!nav_is_safe_to_stand_at(l, x0, (int)fy, z0, sx, sy, sz, fx, fy, fz, dx, dz)) return 0;

    sx -= 2;
    sz -= 2;
    double v16 = 1.0 / fabs(dx);
    double v18 = 1.0 / fabs(dz);
    double v20 = (double)(x0 * 1) - fx;
    double v22 = (double)(z0 * 1) - fz;

    if (dx >= 0.0) ++v20;
    if (dz >= 0.0) ++v22;

    v20 /= dx;
    v22 /= dz;
    int v24 = dx < 0.0 ? -1 : 1;
    int v25 = dz < 0.0 ? -1 : 1;
    int v26 = mh_floor(tx);
    int v27 = mh_floor(tz);
    int v28 = v26 - x0;
    int v29 = v27 - z0;

    while (1)
    {
        if (v28 * v24 <= 0 && v29 * v25 <= 0) return 1;

        if (v20 < v22)
        {
            v20 += v16;
            x0 += v24;
            v28 = v26 - x0;
        }
        else
        {
            v22 += v18;
            z0 += v25;
            v29 = v27 - z0;
        }

        if (!nav_is_safe_to_stand_at(l, x0, (int)fy, z0, sx, sy, sz, fx, fy, fz, dx, dz)) return 0;
    }
}

static void nav_path_follow(struct living *l)
{
    double ex, ey, ez;
    nav_entity_position(l, &ex, &ey, &ez);
    int v2 = path_at(l->nav.path)->length;
    trace("pfollow.in", "iiidddff", l->entity_id, path_at(l->nav.path)->index, v2, ex, ey, ez, (double)l->e.width, (double)l->e.height);

    for (int i = path_at(l->nav.path)->index; i < path_at(l->nav.path)->length; ++i)
    {
        if (path_at(l->nav.path)->pts[i][1] != (int)ey)
        {
            v2 = i;
            break;
        }
    }

    float v8 = l->e.width * l->e.width;
    int v4;

    for (v4 = path_at(l->nav.path)->index; v4 < v2; ++v4)
    {
        double px = (double)path_at(l->nav.path)->pts[v4][0] + (double)((int)(l->e.width + 1.0F)) * 0.5;
        double pz = (double)path_at(l->nav.path)->pts[v4][2] + (double)((int)(l->e.width + 1.0F)) * 0.5;
        double sx = ex - px;
        double sy = ey - (double)path_at(l->nav.path)->pts[v4][1];
        double sz = ez - pz;

        trace("pf.loop", "iidfddd", l->entity_id, v4, sx * sx + sy * sy + sz * sz, (double)v8, px, pz, (double)path_at(l->nav.path)->pts[v4][1]);

        if (sx * sx + sy * sy + sz * sz < (double)v8) path_at(l->nav.path)->index = v4 + 1;
    }

    v4 = ceil_float_int(l->e.width);
    int v5 = (int)l->e.height + 1;
    int v6 = v4;

    for (int i = v2 - 1; i >= path_at(l->nav.path)->index; --i)
    {
        double px = (double)path_at(l->nav.path)->pts[i][0] + (double)((int)(l->e.width + 1.0F)) * 0.5;
        double pz = (double)path_at(l->nav.path)->pts[i][2] + (double)((int)(l->e.width + 1.0F)) * 0.5;

        int direct = nav_direct_path(l, ex, ey, ez, px, (double)path_at(l->nav.path)->pts[i][1], pz, v4, v5, v6);
        trace("pf.direct", "iii", l->entity_id, i, direct);


        if (direct)
        {
            path_at(l->nav.path)->index = i;
            break;
        }
    }

    trace("pfollow.out", "iii", l->entity_id, path_at(l->nav.path)->index, v2);
    if (l->nav.total_ticks - l->nav.ticks_at_last_pos > 100)
    {
        double dx = ex - l->nav.lx;
        double dy = ey - l->nav.ly;
        double dz = ez - l->nav.lz;

        if (dx * dx + dy * dy + dz * dz < 2.25) nav_clear_path(l);

        l->nav.ticks_at_last_pos = l->nav.total_ticks;
        l->nav.lx = ex;
        l->nav.ly = ey;
        l->nav.lz = ez;
    }
}

int nav_no_path(struct living *l)
{
    return l->nav.path == 0 || path_at(l->nav.path)->index >= path_at(l->nav.path)->length;
}

void nav_clear_path(struct living *l)
{
    trace("clearpath", "i", l->entity_id);
    path_drop(l->nav.path);
    l->nav.path = 0;
}

/* PathNavigate.setPath. */
static int nav_set_path(struct living *l, pathref ph, double speed)
{
    struct path_ent *p = path_at(ph);

    trace("setpath", "ii", l->entity_id, p ? p->length : -1);

    if (!p)
    {
        /* PathNavigate.setPath writes the field directly on a null path; it does
         * not go through clearPathEntity */
        path_drop(l->nav.path);
        l->nav.path = 0;
        return 0;
    }

    /* isSamePath: same point count and every point equal */
    int same = 0;


    int prev_n = l->nav.path ? (path_at(l->nav.path)->n_points > 0 ? path_at(l->nav.path)->n_points : path_at(l->nav.path)->length) : -1;
    int new_n = p->n_points > 0 ? p->n_points : p->length;
    if (l->nav.path && prev_n == new_n)
    {
        same = 1;

        for (int i = 0; i < new_n; ++i)
        {
            if (path_at(l->nav.path)->pts[i][0] != p->pts[i][0] || path_at(l->nav.path)->pts[i][1] != p->pts[i][1]
                || path_at(l->nav.path)->pts[i][2] != p->pts[i][2])
            {
                same = 0;
                break;
            }
        }

        trace("samepath", "iiiiiiiiiii", l->entity_id, same, l->nav.path ? path_at(l->nav.path)->length : -1, p->length,
              l->nav.path ? path_at(l->nav.path)->index : -1, l->nav.path ? path_at(l->nav.path)->pts[0][0] : -1,
              l->nav.path ? path_at(l->nav.path)->pts[0][1] : -1, l->nav.path ? path_at(l->nav.path)->pts[0][2] : -1,
              p->pts[0][0], p->pts[0][1], p->pts[0][2]);

        if (same)
        {
            path_drop(ph);
        }
        else
        {
            path_drop(l->nav.path);
            l->nav.path = ph;
        }
    }
    else
    {
        trace("samepath", "iiiiiiiiiii", l->entity_id, 0, l->nav.path ? path_at(l->nav.path)->length : -1, p->length,
              l->nav.path ? path_at(l->nav.path)->index : -1, l->nav.path ? path_at(l->nav.path)->pts[0][0] : -1,
              l->nav.path ? path_at(l->nav.path)->pts[0][1] : -1, l->nav.path ? path_at(l->nav.path)->pts[0][2] : -1,
              p->pts[0][0], p->pts[0][1], p->pts[0][2]);

        path_drop(l->nav.path);
        l->nav.path = ph;
    }


    if (l->nav.no_sun_pathfind && l->nav.path)
    {
        int ex = mh_floor(l->e.pos_x);
        int ey = (int)(l->e.bounding_box.min_y + 0.5);
        int ez = mh_floor(l->e.pos_z);
        if (!world_can_block_see_the_sky(l->world, ex, ey, ez))
        {
            for (int var1 = 0; var1 < path_at(l->nav.path)->length; ++var1)
            {
                if (world_can_block_see_the_sky(l->world, path_at(l->nav.path)->pts[var1][0], path_at(l->nav.path)->pts[var1][1], path_at(l->nav.path)->pts[var1][2]))
                {
                    path_at(l->nav.path)->length = var1 - 1;

                    break;
                }
            }
        }
    }

    if (path_at(l->nav.path)->length == 0)
    {
        return 0;
    }

    l->nav.speed = speed;
    double ex, ey, ez;
    nav_entity_position(l, &ex, &ey, &ez);
    l->nav.ticks_at_last_pos = l->nav.total_ticks;
    l->nav.lx = ex;
    l->nav.ly = ey;
    l->nav.lz = ez;
    return 1;
}

int nav_set_path_shared(struct living *l, pathref p, double speed)
{
    path_hold(p);
    return nav_set_path(l, p, speed);
}

void nav_update(struct living *l)
{
    ++l->nav.total_ticks;

    if (nav_no_path(l)) return;

    if (nav_can_navigate(l)) nav_path_follow(l);

    if (!nav_no_path(l))
    {
        if (path_at(l->nav.path)->index < path_at(l->nav.path)->length)
        {
            int i = path_at(l->nav.path)->index;
            double px = (double)path_at(l->nav.path)->pts[i][0] + (double)((int)(l->e.width + 1.0F)) * 0.5;
            double pz = (double)path_at(l->nav.path)->pts[i][2] + (double)((int)(l->e.width + 1.0F)) * 0.5;
            move_helper_set_move_to(l, px, (double)path_at(l->nav.path)->pts[i][1], pz, l->nav.speed);
        }
    }
}

/* World.getEntityPathToXYZ through the navigator's flags. */
static pathref nav_get_path_to_xyz(struct living *l, double x, double y, double z)
{
    if (!nav_can_navigate(l)) return 0;

    struct nav_scratch *s = nav_scratch_take(l);
    const int *out = s->out;
    struct pf_entity e;
    nav_fill_entity(l, &e);
    int n = pf_get_entity_path_to_xyz(&s->f, &e, mh_floor(x), (int)y, mh_floor(z),
                                      (float)attrs_value(&l->attrs.a[ATTR_FOLLOW_RANGE]),
                                      l->nav.can_pass_open_doors, l->nav.can_pass_closed_doors,
                                      l->nav.avoids_water, l->nav.can_swim, s->out, PF_MAX_POINTS);

    trace("path.xyz", "iiiidddiiiiiii", l->entity_id, mh_floor(x), (int)y, mh_floor(z),
          l->e.pos_x, l->e.bounding_box.min_y, l->e.pos_z, n, n >= 1 ? out[0] : -1, n >= 1 ? out[1] : -1,
          n >= 1 ? out[2] : -1, n >= 2 ? out[(n - 1) * 3] : -1, n >= 2 ? out[(n - 1) * 3 + 1] : -1,
          n >= 2 ? out[(n - 1) * 3 + 2] : -1);

    pathref p = nav_path_of(out, n, 1);
    nav_scratch_give(s);
    return p;
}

/* PathNavigate.getPathToEntityLiving over the target's posX, boundingBox.minY
 * and posZ (the server player is not a struct living, so it comes as a point;
 * its box starts at its feet). */
static pathref nav_get_path_to_point(struct living *l, int other_id, double ox, double omin_y, double oz)
{
    if (!nav_can_navigate(l)) return 0;

    struct nav_scratch *s = nav_scratch_take(l);
    struct pf_entity e;
    nav_fill_entity(l, &e);
    int n = pf_get_path_entity_to_entity(&s->f, &e, ox, omin_y, oz,
                                         (float)attrs_value(&l->attrs.a[ATTR_FOLLOW_RANGE]),
                                         l->nav.can_pass_open_doors, l->nav.can_pass_closed_doors,
                                         l->nav.avoids_water, l->nav.can_swim, s->out, PF_MAX_POINTS);

    trace("path.ent", "iidddfiii", l->entity_id, other_id, l->e.pos_x, l->e.bounding_box.min_y, l->e.pos_z,
          (double)(float)attrs_value(&l->attrs.a[ATTR_FOLLOW_RANGE]), l->nav.can_pass_open_doors, l->nav.can_swim, n);

    pathref p = nav_path_of(s->out, n, 1);
    nav_scratch_give(s);
    return p;
}

static pathref nav_get_path_to_entity(struct living *l, struct living *other)
{
    return nav_get_path_to_point(l, other->entity_id, other->e.pos_x, other->e.bounding_box.min_y, other->e.pos_z);
}

int nav_try_move_to_xyz(struct living *l, double x, double y, double z, double speed)
{
    return nav_set_path(l, nav_get_path_to_xyz(l, x, y, z), speed);
}

/* -------------------------------------------- EntityCreature's own path */

/* World.getPathEntityToEntity for EntityCreature.updateEntityActionState: the
 * four booleans are PathFinder's own (the open wooden doors, the movement
 * block, the water pathing, canEntityDrown), not the navigator's flags, and the
 * caller owns the returned path. It searches this.worldObj, the world the
 * creature is in now (a pigman that went through a portal this tick searches
 * the destination), not the navigator's. */
pathref ai_creature_path_to_entity(struct living *l, struct living *other, float max_dist,
                                            int door_open, int door_closed, int avoid_water, int can_swim)
{
    struct nav_scratch *s = nav_scratch_take_world(l->world, l->world);
    struct pf_entity e;
    nav_fill_entity(l, &e);
    int n = pf_get_path_entity_to_entity(&s->f, &e, other->e.pos_x, other->e.bounding_box.min_y, other->e.pos_z,
                                         max_dist, door_open, door_closed, avoid_water, can_swim,
                                         s->out, PF_MAX_POINTS);

    pathref p = nav_path_of(s->out, n, 0);
    nav_scratch_give(s);
    return p;
}

/* World.getEntityPathToXYZ for EntityCreature.updateWanderPath. */
pathref ai_creature_path_to_xyz(struct living *l, int x, int y, int z, float max_dist,
                                         int door_open, int door_closed, int avoid_water, int can_swim)
{
    struct nav_scratch *s = nav_scratch_take_world(l->world, l->world);
    struct pf_entity e;
    nav_fill_entity(l, &e);
    int n = pf_get_entity_path_to_xyz(&s->f, &e, x, y, z, max_dist, door_open, door_closed, avoid_water, can_swim,
                                      s->out, PF_MAX_POINTS);

    pathref p = nav_path_of(s->out, n, 0);
    nav_scratch_give(s);
    return p;
}

int nav_try_move_to_point(struct living *l, int other_id, double x, double min_y, double z, double speed)
{
    pathref p = nav_get_path_to_point(l, other_id, x, min_y, z);

    if (!p) return 0;

    return nav_set_path(l, p, speed);
}

int nav_try_move_to_entity(struct living *l, struct living *other, double speed)
{
    /* PathNavigate.tryMoveToEntityLiving returns false on a null path without
     * touching the current one */
    pathref p = nav_get_path_to_entity(l, other);

    if (!p) return 0;

    return nav_set_path(l, p, speed);
}

/* ------------------------------------------- RandomPositionGenerator */

/* ChunkCoordinates.getDistanceSquared. */
static float chunk_dist_sq(int hx, int hy, int hz, int x, int y, int z)
{
    int dx = hx - x, dy = hy - y, dz = hz - z;
    return (float)(dx * dx + dy * dy + dz * dz);
}

static struct living *find_nearest_entity_in_aabb(struct an_world *an, struct living *exclude, int kind, const struct aabb *box)
{
    AN_QUERY_LIST(ents);
    int n = an_entities_within_aabb(an, kind, box, ents, AN_MAX_ENTITIES);
    struct living *best = NULL;
    double best_dist = 1e30;
    for (int i = 0; i < n; ++i)
    {
        if (!ents[i]->is_living || !ents[i]->livh) continue;
        struct living *other = lv_get(ents[i]->livh);
        /* World.findNearestEntityWithinAABB takes a dead entity still in
         * a chunk's list (killed earlier in the pass, a traveller's ghost) */
        if (other == exclude) continue;
        /* the any-kind search is findNearestEntityWithinAABB(EntityLiving):
         * the player (an EntityLivingBase only) is never a candidate */
        if (kind < 0 && (other->kind == SK_PLAYER || other->kind == HK_PLAYER)) continue;
        double dx = exclude->e.pos_x - other->e.pos_x;
        double dy = exclude->e.pos_y - other->e.pos_y;
        double dz = exclude->e.pos_z - other->e.pos_z;
        double d = dx * dx + dy * dy + dz * dz;
        if (d <= best_dist)
        {
            best_dist = d;
            best = other;
        }
    }
    return best;
}

/* RandomPositionGenerator.findRandomTarget with no direction vector. */
static int random_target_block(struct living *l, int dxz, int dy, double vec_x, double vec_y, double vec_z, int has_vec, int *ox, int *oy, int *oz)
{
    int v6 = 0, v7 = 0, v8 = 0, found = 0;
    float best = -99999.0F;
    int has_home = (l->maximum_home_distance != -1.0F);
    int var10 = 0;

    if (has_home)
    {
        int ex = mh_floor(l->e.pos_x);
        int ey = mh_floor(l->e.pos_y);
        int ez = mh_floor(l->e.pos_z);
        int hdx = l->home_x - ex;
        int hdy = l->home_y - ey;
        int hdz = l->home_z - ez;
        double var11 = (double)(hdx * hdx + hdy * hdy + hdz * hdz + 4.0F);
        double var13 = (double)(l->maximum_home_distance + (float)dxz);
        var10 = (var11 < var13 * var13);
    }

    for (int i = 0; i < 10; ++i)
    {
        int v12 = det_rng_int_n(&l->rand, 2 * dxz) - dxz;
        int v17 = det_rng_int_n(&l->rand, 2 * dy) - dy;
        int v14 = det_rng_int_n(&l->rand, 2 * dxz) - dxz;
        trace("randtarget", "iiiii", l->entity_id, i, v12, v17, v14);

        if (!has_vec || (double)v12 * vec_x + (double)v14 * vec_z >= 0.0)
        {
            v12 += mh_floor(l->e.pos_x);
            v17 += mh_floor(l->e.pos_y);
            v14 += mh_floor(l->e.pos_z);

            int in_home = 1;
            if (var10 && l->maximum_home_distance != -1.0F)
            {
                int hdx = l->home_x - v12;
                int hdy = l->home_y - v17;
                int hdz = l->home_z - v14;
                in_home = ((float)(hdx * hdx + hdy * hdy + hdz * hdz) < l->maximum_home_distance * l->maximum_home_distance);
            }

            if (in_home)
            {
                float w = 0.0F;
                if (l->kind < AK_KINDS)
                {
                    w = animal_get_block_path_weight(l, v12, v17, v14);
                }
                else if ((l->kind >= HK_ZOMBIE && l->kind <= HK_SPIDER) || l->kind == HK_WITCH || l->kind == HK_PIGMAN)
                {
                    w = 0.5F - living_light_brightness(l->world, l->an ? l->an->skylight : 0, v12, v17, v14);
                }

                if (w > best)
                {
                    best = w;
                    v6 = v12;
                    v7 = v17;
                    v8 = v14;
                    found = 1;
                }
            }
        }
    }

    (void)chunk_dist_sq;

    trace("randtarget.pick", "iiiiif", l->entity_id, v6, v7, v8, found, (double)best);

    if (!found) return 0;

    *ox = v6;
    *oy = v7;
    *oz = v8;
    return 1;
}

static int random_target(struct living *l, int dxz, int dy, int *ox, int *oy, int *oz)
{
    return random_target_block(l, dxz, dy, 0.0, 0.0, 0.0, 0, ox, oy, oz);
}

static int random_target_towards(struct living *l, int dxz, int dy, double tx, double ty, double tz, int *ox, int *oy, int *oz)
{
    return random_target_block(l, dxz, dy, tx - l->e.pos_x, ty - l->e.pos_y, tz - l->e.pos_z, 1, ox, oy, oz);
}

/* RandomPositionGenerator.findRandomTargetBlockAwayFrom. */
static int random_target_away_from(struct living *l, int dxz, int dy, double fx, double fy, double fz, int *ox, int *oy, int *oz)
{
    return random_target_block(l, dxz, dy, l->e.pos_x - fx, l->e.pos_y - fy, l->e.pos_z - fz, 1, ox, oy, oz);
}

/* ---------------------------------------------------------- the task work */

static int task_index_of(struct ai_tasks *t, int entry)
{
    for (int i = 0; i < t->nexec; ++i)
        if (t->executing[i] == entry) return i;

    return -1;
}

static int tasks_compatible(int mutex_a, int mutex_b)
{
    return (mutex_a & mutex_b) == 0;
}

/* can_use over the executing tasks as a mask (bit j: entries[j] is
 * executing): the same answer (isInterruptible is true for every task, so
 * only an executing task of no greater priority and an overlapping mutex
 * refuses), reading only the executing entries */
static int can_use_mask(const struct ai_tasks *t, int entry, uint32_t exec)
{
    int prio = t->entries[entry].priority, mutex = t->entries[entry].t.mutex;

    for (uint32_t m = exec & ~(1u << entry); m != 0; m &= m - 1)
    {
        int j = __builtin_ctz(m);
        if (prio >= t->entries[j].priority && !tasks_compatible(mutex, t->entries[j].t.mutex)) return 0;
    }
    return 1;
}

static void ai_reset(struct living *l, struct ai_task *task);

/* EntityAIMate.getNearbyMate: the closest same-kind animal in the 8-block box. */
static struct living *nearby_mate(struct living *l)
{
    struct aabb box = aabb_expand(l->e.bounding_box, 8.0, 8.0, 8.0);
    AN_QUERY_LIST(found);
    int n = an_entities_within_aabb(l->an, l->kind, &box, found, AN_MAX_ENTITIES);
    double best = 1.7976931348623157e308;
    struct living *mate = NULL;

    for (int i = 0; i < n; ++i)
    {
        struct living *o = lv_get(found[i]->livh);

        if (o == l) continue;

        /* canMateWith: the STRICT class comparison, so a cow never mates a
         * mooshroom even though the query above matched it, and both must be in
         * love */
        if (o->kind != l->kind) continue;
        if (!(l->in_love > 0 && o->in_love > 0)) continue;

        double d = dist_sq_to(l, o);

        if (d < best)
        {
            mate = o;
            best = d;
        }
    }

    return mate;
}

/* EntityAIFollowParent.shouldExecute's search. */
static void fp_dump(struct living *l, struct an_ent **found, int n)
{
    trace("fp.cands", "ii", l->entity_id, n);

    for (int i = 0; i < n; ++i) trace("fp.cand", "ii", l->entity_id, lv_get(found[i]->livh)->entity_id);
}

#define fp_found (nw_scratch->fp_found)


static struct living *follow_parent_target(struct living *l, double *out_dist, int *out_n)
{
    struct aabb box = aabb_expand(l->e.bounding_box, 8.0, 4.0, 8.0);
    struct an_ent **found = fp_found;
    int n = an_entities_within_aabb(l->an, l->kind, &box, found, AN_MAX_ENTITIES);
    *out_n = n;
    struct living *parent = NULL;
    double best = 1.7976931348623157e308;

    for (int i = 0; i < n; ++i)
    {
        struct living *o = lv_get(found[i]->livh);

        if (o == l) continue;
        if (o->growing_age < 0) continue;

        double d = dist_sq_to(l, o);

        if (d <= best)
        {
            best = d;
            parent = o;
        }
    }

    *out_dist = best;
    return parent;
}

static void spawn_baby(struct living *l, struct living *mate, det_state *det);

static int living_is_within_home_distance(struct living *l, int x, int y, int z)
{
    if (l->maximum_home_distance == -1.0F) return 1;
    int dx = l->home_x - x, dy = l->home_y - y, dz = l->home_z - z;
    return (float)(dx * dx + dy * dy + dz * dz) < l->maximum_home_distance * l->maximum_home_distance;
}

static int ai_is_suitable_target(struct living *l, struct living *other, int can_target_invuln, int check_sight)
{
    if (!other || other == l || !living_is_alive(other)) return 0;
    /* EntityLiving.canAttackClass: never a creeper or a ghast; the iron
     * golem's override spares players when the golem is player-built */
    if (other->kind == HK_CREEPER || other->kind == GK_GHAST) return 0;
    if (l->kind == VK_IRON_GOLEM && l->is_player_created &&
        (other->kind == HK_PLAYER || other->kind == SK_PLAYER)) return 0;
    if (!can_target_invuln && other->invulnerable) return 0;
    if (!living_is_within_home_distance(l, mh_floor(other->e.pos_x),
                                        mh_floor(other->e.pos_y),
                                        mh_floor(other->e.pos_z)))
    {
        return 0;
    }
    if (check_sight && !senses_can_see(l, other)) return 0;
    return 1;
}

/* World.getClosestPlayerToEntity(entity, 10.0) over the one player: its
 * position (feet) when it is inside the range. */
static int tempt_player(struct living *l, double *x, double *y, double *z)
{
    if (!l->an) return 0;
    double px, py, pz;
    if (l->an->playerh) { px = lv_get(l->an->playerh)->e.pos_x; py = lv_get(l->an->playerh)->e.pos_y; pz = lv_get(l->an->playerh)->e.pos_z; }
    else if (l->an->has_player) { px = l->an->player_x; py = l->an->player_y; pz = l->an->player_z; }
    else return 0;
    if (x) { *x = px; *y = py; *z = pz; }
    double dx = px - l->e.pos_x, dy = py - l->e.pos_y, dz = pz - l->e.pos_z;
    return dx * dx + dy * dy + dz * dz < 100.0;
}

/* IMob: the monsters IMob.mobSelector accepts */
static int is_imob_kind(int kind)
{
    return kind == HK_ZOMBIE || kind == HK_SKELETON || kind == HK_CREEPER || kind == HK_SPIDER ||
           kind == HK_CAVE_SPIDER || kind == HK_ENDERMAN || kind == HK_WITCH || kind == HK_SILVERFISH ||
           kind == HK_PIGMAN || kind == HK_BLAZE || kind == GK_GHAST || kind == SK_SLIME ||
           kind == SK_MAGMA_CUBE;
}

int ai_is_imob_kind(int kind)
{
    return is_imob_kind(kind);
}

/* EntityAITarget.isSuitableTarget's nearbyOnly tail: the cached reach test,
 * redone when the delay runs out (canEasilyReach: the delay's draw, then the
 * path to the target, whose end must be within 1.5 blocks of it) */
static int target_nearby_ok(struct living *l, struct ai_task *t, struct living *other)
{
    if (--t->target_search_delay <= 0) t->target_search_status = 0;
    if (t->target_search_status == 0)
    {
        t->target_search_delay = 10 + det_rng_int_n(&l->rand, 5);
        pathref ph = nav_get_path_to_entity(l, other);
        const struct path_ent *p = path_at(ph);
        int ok = 0;
        if (p != NULL && p->n_points > 0)
        {
            int dx = p->pts[p->n_points - 1][0] - mh_floor(other->e.pos_x);
            int dz = p->pts[p->n_points - 1][2] - mh_floor(other->e.pos_z);
            ok = (double)(dx * dx + dz * dz) <= 2.25;
        }
        path_drop(ph);
        t->target_search_status = ok ? 1 : 2;
    }
    return t->target_search_status != 2;
}

static int ai_should_execute(struct living *l, struct ai_task *t, det_state *det)
{
    switch (t->cls)
    {
        case AIC_SWIMMING:
            return l->is_in_water || living_handle_lava_movement(l);

        case AIC_PANIC:
            /* the panic runs when there is a revenge target or the animal burns */
            if (lv_get(l->entity_living_to_attack) == NULL && !living_is_burning(l)) return 0;
            {
                int x, y, z;
                if (!random_target(l, 5, 4, &x, &y, &z)) return 0;
                t->x = (double)x;
                t->y = (double)y;
                t->z = (double)z;
                return 1;
            }

        case AIC_CONTROLLED_BY_PLAYER:
            return ride_ai_should_execute(l, t);

        case AIC_MATE:
            if (!(l->in_love > 0)) return 0;
            t->target = lv_ref(nearby_mate(l));
            return lv_get(t->target) != NULL;

        case AIC_TEMPT:
            if (t->delay_tempt_counter > 0)
            {
                --t->delay_tempt_counter;
                return 0;
            }

            /* World.getClosestPlayerToEntity(entity, 10.0), then the held item */
            if (!tempt_player(l, NULL, NULL, NULL)) return 0;
            return l->an->player_held_item != 0 && l->an->player_held_item == t->item_id;

        case AIC_FOLLOW_PARENT:
        {
            trace("fp.enter", "iiiidddddd", l->entity_id, l->growing_age, t->follow_parent_delay,
                  lv_get(t->target) ? lv_get(t->target)->entity_id : -1, l->e.bounding_box.min_x, l->e.bounding_box.max_x,
                  l->e.bounding_box.min_y, l->e.bounding_box.max_y, l->e.bounding_box.min_z, l->e.bounding_box.max_z);
            if (l->growing_age >= 0) return 0;

            double d;
            int nfound;
            struct living *parent = follow_parent_target(l, &d, &nfound);
            fp_dump(l, fp_found, nfound);
            trace("followparent", "iidi", l->entity_id, parent ? parent->entity_id : -1, d, nfound);
            if (parent == NULL) return 0;
            if (d < 9.0) return 0;

            t->target = lv_ref(parent);
            return 1;
        }

        case AIC_WANDER:
        {
            if (l->entity_age >= 100) return 0;
            if (det_rng_int_n(&l->rand, 120) != 0) return 0;

            int x, y, z;

            if (!random_target(l, 10, 7, &x, &y, &z)) return 0;

            t->x = (double)x;
            t->y = (double)y;
            t->z = (double)z;
            return 1;
        }

        case AIC_AVOID_ENTITY:
        {
            /* EntityAIAvoidEntity.shouldExecute over a class (the villager's
             * EntityZombie, which takes in the pigman; the creeper's ocelot,
             * target_class 0, is switched off in every world: its query is
             * empty and draws nothing) */
            if (t->target_class == 0) return 0;
            float d = t->max_watch_dist;
            struct aabb box = aabb_expand(l->e.bounding_box, (double)d, 3.0, (double)d);
            AN_QUERY_LIST(ents);
            int n = an_entities_within_aabb(l->an, t->target_class, &box, ents, AN_MAX_ENTITIES);
            struct living *closest = NULL;
            for (int i = 0; i < n; ++i)
            {
                if (!ents[i]->is_living || !ents[i]->livh) continue;
                struct living *other = lv_get(ents[i]->livh);
                /* field_98218_a: isEntityAlive && canSee, for every candidate */
                if (!living_is_alive(other) || !senses_can_see(l, other)) continue;
                if (!closest) closest = other;
            }
            if (!closest) return 0;
            t->target = lv_ref(closest);
            int vx, vy, vz;
            if (!random_target_away_from(l, 16, 7, closest->e.pos_x, closest->e.pos_y, closest->e.pos_z, &vx, &vy, &vz))
                return 0;
            double ax = closest->e.pos_x - (double)vx, ay = closest->e.pos_y - (double)vy, az = closest->e.pos_z - (double)vz;
            double bx = closest->e.pos_x - l->e.pos_x, by = closest->e.pos_y - l->e.pos_y, bz = closest->e.pos_z - l->e.pos_z;
            if (ax * ax + ay * ay + az * az < bx * bx + by * by + bz * bz) return 0;
            path_drop(t->path);
            t->path = nav_get_path_to_xyz(l, (double)vx, (double)vy, (double)vz);
            /* PathEntity.isDestinationSame: the final point's x and z */
            if (!t->path) return 0;
            if (path_at(t->path)->length <= 0) return 0;
            return path_at(t->path)->pts[path_at(t->path)->length - 1][0] == vx && path_at(t->path)->pts[path_at(t->path)->length - 1][2] == vz;
        }

        case AIC_TRADE_PLAYER:
        {
            /* EntityAITradePlayer.shouldExecute */
            if (!living_is_alive(l)) return 0;
            if (living_is_in_water(l)) return 0;
            if (!l->e.on_ground) return 0;
            if (l->e.velocity_changed) return 0;
            if (!l->buying_player) return 0;
            double dx = l->an->player_x - l->e.pos_x;
            double dy = l->an->player_y - l->e.pos_y;
            double dz = l->an->player_z - l->e.pos_z;
            if (dx * dx + dy * dy + dz * dz > 16.0) return 0;
            /* the customer's openContainer is always a Container here */
            return 1;
        }

        case AIC_LOOK_AT_TRADE_PLAYER:
        {
            /* EntityAILookAtTradePlayer.shouldExecute: isTrading, no draw */
            return l->buying_player != 0;
        }

        case AIC_MOVE_TOWARDS_TARGET:
        {
            /* EntityAIMoveTowardsTarget.shouldExecute */
            struct living *target = lv_get(l->attack_target);
            t->target = lv_ref(target);
            if (!target) return 0;
            double dx = target->e.pos_x - l->e.pos_x, dy = target->e.pos_y - l->e.pos_y, dz = target->e.pos_z - l->e.pos_z;
            if (dx * dx + dy * dy + dz * dz > (double)(t->max_watch_dist * t->max_watch_dist)) return 0;
            int x, y, z;
            if (!random_target_towards(l, 16, 7, target->e.pos_x, target->e.pos_y, target->e.pos_z, &x, &y, &z)) return 0;
            t->x = (double)x;
            t->y = (double)y;
            t->z = (double)z;
            return 1;
        }

        case AIC_BREAK_DOOR:
        {
            if (!l->e.is_collided_horizontally) return 0;
            const struct path_ent *path = path_at(l->nav.path);
            if (!path || path->index >= path->length || !l->nav.can_pass_closed_doors) return 0;
            int found = 0;
            int max_idx = path->index + 2 < path->length ? path->index + 2 : path->length;
            for (int i = 0; i < max_idx; ++i)
            {
                int px = path->pts[i][0];
                int py = path->pts[i][1] + 1;
                int pz = path->pts[i][2];
                double dx = (double)px - l->e.pos_x;
                double dz = (double)pz - l->e.pos_z;
                if (dx * dx + dz * dz <= 2.25)
                {
                    int id = world_get_block(l->world, px, py, pz) & 4095;
                    if (id == 64)
                    {
                        t->door_x = px;
                        t->door_y = py;
                        t->door_z = pz;
                        found = 1;
                        break;
                    }
                }
            }
            if (!found)
            {
                int ex = mh_floor(l->e.pos_x);
                int ey = mh_floor(l->e.pos_y + 1.0);
                int ez = mh_floor(l->e.pos_z);
                int id = world_get_block(l->world, ex, ey, ez) & 4095;
                if (id == 64)
                {
                    t->door_x = ex;
                    t->door_y = ey;
                    t->door_z = ez;
                    found = 1;
                }
            }
            if (!found) return 0;
            int meta = world_get_meta(l->world, t->door_x, t->door_y, t->door_z);
            if (meta & 8) meta = world_get_meta(l->world, t->door_x, t->door_y - 1, t->door_z);
            if ((meta & 4) != 0) return 0;
            return 1;
        }

        case AIC_ATTACK_ON_COLLIDE:
        {
            struct living *target = lv_get(l->attack_target);
            if (!target || !living_is_alive(target)) return 0;
            if (t->target_class != 0 && target->kind != t->target_class) return 0;
            pathref path = nav_get_path_to_entity(l, target);
            if (!path) return 0;
            t->path = path;
            return 1;
        }

        case AIC_HURT_BY_TARGET:
        {
            int rev = l->revenge_timer;
            if (rev != t->revenge_timer_last && lv_get(l->entity_living_to_attack) != NULL &&
                ai_is_suitable_target(l, lv_get(l->entity_living_to_attack), 0, t->should_check_sight))
            {
                return 1;
            }
            return 0;
        }

        case AIC_CREEPER_SWELL:
            /* EntityAICreeperSwell.shouldExecute: the state it set last tick or
             * a target within 3 blocks */
            return creeper_swell_should_execute(l, t);

        case AIC_NEAREST_ATTACKABLE_TARGET:
        {
            if (t->watch_chance > 0.0f && det_rng_int_n(&l->rand, (int)t->watch_chance) != 0)
            {
                return 0;
            }
            double range = attrs_value(&l->attrs.a[ATTR_FOLLOW_RANGE]);
            struct aabb box = {
                .min_x = l->e.bounding_box.min_x - range,
                .min_y = l->e.bounding_box.min_y - 4.0,
                .min_z = l->e.bounding_box.min_z - range,
                .max_x = l->e.bounding_box.max_x + range,
                .max_y = l->e.bounding_box.max_y + 4.0,
                .max_z = l->e.bounding_box.max_z + range
            };
            AN_QUERY_LIST(ents);
            /* target_class -2: EntityLiving under IMob.mobSelector (the golem) */
            int imob = t->target_class == -2;
            int n = an_entities_within_aabb(l->an, imob ? -1 : t->target_class, &box, ents, AN_MAX_ENTITIES);
            struct living *best = NULL;
            double best_dist = 1e30;
            for (int i = 0; i < n; ++i)
            {
                if (!ents[i]->is_living || !ents[i]->livh) continue;
                struct living *other = lv_get(ents[i]->livh);
                if (imob && !is_imob_kind(other->kind)) continue;
                if (other == l || !living_is_alive(other)) continue;
                /* EntityLiving.canAttackClass: never a creeper or a ghast */
                if (other->kind == GK_GHAST) continue;
                if (!ai_is_suitable_target(l, other, 0, t->should_check_sight)) continue;
                if (t->nearby_only && !target_nearby_ok(l, t, other)) continue;
                double dx = l->e.pos_x - other->e.pos_x;
                double dy = l->e.pos_y - other->e.pos_y;
                double dz = l->e.pos_z - other->e.pos_z;
                double d = dx * dx + dy * dy + dz * dz;
                if (d < best_dist)
                {
                    best_dist = d;
                    best = other;
                }
            }
            if (!best) return 0;
            t->target = lv_ref(best);
            return 1;
        }

        case AIC_DEFEND_VILLAGE:
        {
            /* EntityAIDefendVillage.shouldExecute: the village's nearest
             * aggressor, else one roll in 20 for a player of too low a
             * reputation; isSuitableTarget(target, false) with the task's
             * own nearbyOnly state */
            struct village *v = lv_village(l);
            if (!v) return 0;
            struct living *target = v_find_nearest_aggressor(v, l);
            if (!target || !ai_is_suitable_target(l, target, 0, 0) || !target_nearby_ok(l, t, target))
            {
                if (det_rng_int_n(&l->rand, 20) != 0) return 0;
                target = v_find_low_reputation_player(v, l);
                t->target = lv_ref(target);
                return target && ai_is_suitable_target(l, target, 0, 0) && target_nearby_ok(l, t, target);
            }
            t->target = lv_ref(target);
            return 1;
        }

        case AIC_MOVE_INDOORS:
        {
            int ex = mh_floor(l->e.pos_x);
            int ey = mh_floor(l->e.pos_y);
            int ez = mh_floor(l->e.pos_z);

            int is_daytime = (l->an->skylight < 4);
            int can_lightning = 1;
            if (l->world)
            {
                int b = world_get_biome(l->world, ex, ez);
                if (b >= 0 && b < 256) can_lightning = BIOMES[b].lightning;
            }
            /* World.isRaining: getRainStrength(1.0F) > 0.2; a world whose
             * provider.hasNoSky (the Nether, the End) never draws */
            if (is_daytime && !l->an->raining && can_lightning) return 0;
            if (l->world != NULL && l->world->dim != 0) return 0;

            if (det_rng_int_n(&l->rand, 50) != 0) return 0;
            if (t->inside_pos_x != -1)
            {
                double dx = (double)t->inside_pos_x - l->e.pos_x;
                double dz = (double)t->inside_pos_z - l->e.pos_z;
                if (dx * dx + dz * dz < 4.0) return 0;
            }
            struct village_collection *vc = (struct village_collection *)l->an->village_collection;
            struct village *v = vc_find_nearest_village(vc, ex, ey, ez, 14);
            if (!v) return 0;
            struct village_door_info *door = v_find_nearest_door_unrestricted(v, ex, ey, ez);
            if (!door) return 0;
            t->front_door = door_ref(door);
            return 1;
        }

        case AIC_RESTRICT_OPEN_DOOR:
        {
            if (l->an->skylight < 4) return 0;
            struct village_collection *vc = (struct village_collection *)l->an->village_collection;
            if (!vc) return 0;
            int ex = mh_floor(l->e.pos_x);
            int ey = mh_floor(l->e.pos_y);
            int ez = mh_floor(l->e.pos_z);
            struct village *v = vc_find_nearest_village(vc, ex, ey, ez, 16);
            if (!v) return 0;
            struct village_door_info *door = v_find_nearest_door(v, ex, ey, ez);
            if (!door) return 0;
            if ((double)vdi_inside_dist_sq(door, ex, ey, ez) < 2.25)
            {
                t->front_door = door_ref(door);
                return 1;
            }
            return 0;
        }

        case AIC_OPEN_DOOR:
        {
            if (!l->e.is_collided_horizontally) return 0;
            if (!l->nav.path || path_at(l->nav.path)->index >= path_at(l->nav.path)->length || !l->nav.can_pass_closed_doors) return 0;

            int p_idx = path_at(l->nav.path)->index;
            int p_len = path_at(l->nav.path)->length;
            int limit = (p_idx + 2 < p_len) ? p_idx + 2 : p_len;
            for (int k = 0; k < limit; ++k)
            {
                int px = path_at(l->nav.path)->pts[k][0];
                int py = path_at(l->nav.path)->pts[k][1] + 1;
                int pz = path_at(l->nav.path)->pts[k][2];
                double dx = (double)px - l->e.pos_x;
                double dz = (double)pz - l->e.pos_z;
                if (dx * dx + dz * dz <= 2.25)
                {
                    int blk = world_get_block(l->world, px, py, pz) & 4095;
                    if (blk == 64)
                    {
                        t->door_x = px;
                        t->door_y = py;
                        t->door_z = pz;
                        return 1;
                    }
                }
            }

            int ex = mh_floor(l->e.pos_x);
            int ey = mh_floor(l->e.pos_y + 1.0);
            int ez = mh_floor(l->e.pos_z);
            int blk = world_get_block(l->world, ex, ey, ez) & 4095;
            if (blk == 64)
            {
                t->door_x = ex;
                t->door_y = ey;
                t->door_z = ez;
                return 1;
            }
            return 0;
        }

        case AIC_MOVE_TOWARDS_RESTRICTION:
        {
            if (l->maximum_home_distance == -1.0F) return 0;
            int ex = mh_floor(l->e.pos_x);
            int ey = mh_floor(l->e.pos_y);
            int ez = mh_floor(l->e.pos_z);
            int dx = l->home_x - ex;
            int dy = l->home_y - ey;
            int dz = l->home_z - ez;
            if ((float)(dx * dx + dy * dy + dz * dz) < l->maximum_home_distance * l->maximum_home_distance) return 0;

            int rx, ry, rz;
            if (!random_target_towards(l, 16, 7, (double)l->home_x, (double)l->home_y, (double)l->home_z, &rx, &ry, &rz)) return 0;
            t->x = (double)rx;
            t->y = (double)ry;
            t->z = (double)rz;
            return 1;
        }

        case AIC_VILLAGER_MATE:
        {
            if (l->growing_age != 0) return 0;
            if (det_rng_int_n(&l->rand, 500) != 0) return 0;
            struct village_collection *vc = (struct village_collection *)l->an->village_collection;
            if (!vc) return 0;
            int ex = mh_floor(l->e.pos_x);
            int ey = mh_floor(l->e.pos_y);
            int ez = mh_floor(l->e.pos_z);
            struct village *v = vc_find_nearest_village(vc, ex, ey, ez, 0);
            if (!v) return 0;
            if (!v_is_mating_season(v)) return 0;
            int max_v = (int)((double)((float)v->num_doors) * 0.35);
            if (v->num_villagers >= max_v) return 0;

            struct aabb box = l->e.bounding_box;
            box = aabb_expand(box, 8.0, 3.0, 8.0);
            struct living *mate = find_nearest_entity_in_aabb(l->an, l, VK_VILLAGER, &box);
            if (!mate || mate->growing_age != 0) return 0;

            t->village_ptr = vc_village_ref(l->an->village_collection, v);
            t->target_mate = lv_ref(mate);
            return 1;
        }

        case AIC_FOLLOW_GOLEM:
        {
            if (l->growing_age >= 0) return 0;
            if (l->an->skylight >= 4) return 0;
            struct aabb box = l->e.bounding_box;
            box = aabb_expand(box, 6.0, 2.0, 6.0);
            AN_QUERY_LIST(ents);
            int n = an_entities_within_aabb(l->an, VK_IRON_GOLEM, &box, ents, AN_MAX_ENTITIES);
            for (int i = 0; i < n; ++i)
            {
                if (ents[i]->is_living && ents[i]->livh && lv_get(ents[i]->livh)->hold_rose_tick > 0)
                {
                    t->target_golem = lv_ref(lv_get(ents[i]->livh));
                    return 1;
                }
            }
            return 0;
        }

        case AIC_PLAY:
        {
            if (l->growing_age >= 0) return 0;
            if (det_rng_int_n(&l->rand, 400) != 0) return 0;

            struct aabb box = l->e.bounding_box;
            box = aabb_expand(box, 6.0, 3.0, 6.0);
            AN_QUERY_LIST(ents);
            int n = an_entities_within_aabb(l->an, VK_VILLAGER, &box, ents, AN_MAX_ENTITIES);
            double best_dist = 1e30;
            struct living *target = NULL;
            for (int i = 0; i < n; ++i)
            {
                if (!ents[i]->is_living || !ents[i]->livh) continue;
                struct living *v = lv_get(ents[i]->livh);
                if (v != l && !v->is_playing && v->growing_age < 0)
                {
                    double dx = v->e.pos_x - l->e.pos_x;
                    double dy = v->e.pos_y - l->e.pos_y;
                    double dz = v->e.pos_z - l->e.pos_z;
                    double dsq = dx * dx + dy * dy + dz * dz;
                    if (dsq <= best_dist)
                    {
                        best_dist = dsq;
                        target = v;
                    }
                }
            }
            if (!target)
            {
                int rx, ry, rz;
                if (!random_target(l, 16, 3, &rx, &ry, &rz)) return 0;
            }
            t->target_child = lv_ref(target);
            return 1;
        }

        case AIC_WATCH_CLOSEST:
        case AIC_WATCH_CLOSEST2:
        {
            float ch = t->watch_chance > 0.0F ? t->watch_chance : 0.02F;
            if (det_rng_float(&l->rand) >= ch) return 0;

            if (t->watch_class == 0)
            {
                if (!l->an->has_player) return 0;
                double dx = l->an->player_x - l->e.pos_x;
                double dy = l->an->player_y - l->e.pos_y;
                double dz = l->an->player_z - l->e.pos_z;
                float md = t->max_watch_dist > 0.0F ? t->max_watch_dist : 8.0F;
                /* World.getClosestPlayer: strictly inside the distance */
                if (dx * dx + dy * dy + dz * dz >= (double)md * (double)md) return 0;
                t->is_watching_player = 1;
                t->target = 0;
                return 1;
            }
            else if (t->watch_class == 1)
            {
                float md = t->max_watch_dist > 0.0F ? t->max_watch_dist : 5.0F;
                struct aabb box = l->e.bounding_box;
                box = aabb_expand(box, (double)md, 3.0, (double)md);
                struct living *v = find_nearest_entity_in_aabb(l->an, l, VK_VILLAGER, &box);
                if (!v) return 0;
                t->is_watching_player = 0;
                t->target = lv_ref(v);
                return 1;
            }
            else
            {
                float md = t->max_watch_dist > 0.0F ? t->max_watch_dist : 8.0F;
                struct aabb box = l->e.bounding_box;
                box = aabb_expand(box, (double)md, 3.0, (double)md);
                struct living *v = find_nearest_entity_in_aabb(l->an, l, -1, &box);
                if (!v) return 0;
                t->is_watching_player = 0;
                t->target = lv_ref(v);
                return 1;
            }
        }

        case AIC_LOOK_AT_VILLAGER:
        {
            if (l->an->skylight >= 4) return 0;
            if (det_rng_int_n(&l->rand, 8000) != 0) return 0;
            struct aabb box = l->e.bounding_box;
            box = aabb_expand(box, 6.0, 2.0, 6.0);
            struct living *v = find_nearest_entity_in_aabb(l->an, l, VK_VILLAGER, &box);
            if (!v) return 0;
            t->target = lv_ref(v);
            return 1;
        }

        case AIC_MOVE_THROUGH_VILLAGE:
        {
            if (lv_ai(l)->mtv_nvisited_doors > 15)
            {
                memmove(&lv_ai(l)->mtv_visited_doors[0], &lv_ai(l)->mtv_visited_doors[1],
                        (size_t)(lv_ai(l)->mtv_nvisited_doors - 1) * sizeof(lv_ai(l)->mtv_visited_doors[0]));
                --lv_ai(l)->mtv_nvisited_doors;
            }
            if (t->nocturnal && l->an->skylight < 4) return 0;

            struct village_collection *vc = (struct village_collection *)l->an->village_collection;
            int ex = mh_floor(l->e.pos_x);
            int ey = mh_floor(l->e.pos_y);
            int ez = mh_floor(l->e.pos_z);
            struct village *v = vc_find_nearest_village(vc, ex, ey, ez, 0);
            if (!v) return 0;

            struct village_door_info *best_door = NULL;
            int best_dist = 0x7fffffff;
            for (int di = 0; di < v->num_doors; ++di)
            {
                struct village_door_info *door = door_at(v->doors[di]);
                int visited = 0;
                for (int vi = 0; vi < lv_ai(l)->mtv_nvisited_doors; ++vi)
                {
                    if (door->pos_x == lv_ai(l)->mtv_visited_doors[vi][0] &&
                        door->pos_y == lv_ai(l)->mtv_visited_doors[vi][1] &&
                        door->pos_z == lv_ai(l)->mtv_visited_doors[vi][2])
                    {
                        visited = 1;
                        break;
                    }
                }
                if (visited) continue;

                int dx = door->pos_x - ex;
                int dy = door->pos_y - ey;
                int dz = door->pos_z - ez;
                int d = dx * dx + dy * dy + dz * dz;
                if (d < best_dist)
                {
                    best_dist = d;
                    best_door = door;
                }
            }
            if (!best_door) return 0;

            t->front_door = door_ref(best_door);
            int old_break = l->nav.can_pass_closed_doors;
            l->nav.can_pass_closed_doors = 0;
            pathref path = nav_get_path_to_xyz(l, (double)best_door->pos_x, (double)best_door->pos_y, (double)best_door->pos_z);
            l->nav.can_pass_closed_doors = old_break;
            if (path)
            {
                t->mtv_path = path;
                return 1;
            }

            int rx, ry, rz;
            if (random_target_towards(l, 10, 7, (double)best_door->pos_x, (double)best_door->pos_y, (double)best_door->pos_z, &rx, &ry, &rz))
            {
                old_break = l->nav.can_pass_closed_doors;
                l->nav.can_pass_closed_doors = 0;
                path = nav_get_path_to_xyz(l, (double)rx, (double)ry, (double)rz);
                l->nav.can_pass_closed_doors = old_break;
                if (path)
                {
                    t->mtv_path = path;
                    return 1;
                }
            }
            return 0;
        }

        case AIC_LOOK_IDLE:
            return det_rng_float(&l->rand) < 0.02F;

        case AIC_EAT_GRASS:
        {
            if (det_rng_int_n(&l->rand, l->growing_age < 0 ? 50 : 1000) != 0) return 0;

            int x = mh_floor(l->e.pos_x);
            int y = mh_floor(l->e.pos_y);
            int z = mh_floor(l->e.pos_z);
            int here = world_get_block(l->world, x, y, z) & 4095;

            if (here == 31 && world_get_meta(l->world, x, y, z) == 1) return 1;

            return (world_get_block(l->world, x, y - 1, z) & 4095) == 2;
        }

        case AIC_RESTRICT_SUN:
            return l->an ? l->an->skylight < 4 : 0;

        case AIC_FLEE_SUN:
        {
            if (!(l->an ? l->an->skylight < 4 : 0)) return 0;
            if (!living_is_burning(l)) return 0;
            int ex = mh_floor(l->e.pos_x);
            int ey = (int)l->e.bounding_box.min_y;
            int ez = mh_floor(l->e.pos_z);
            if (!world_can_block_see_the_sky(l->world, ex, ey, ez)) return 0;

            for (int k = 0; k < 10; ++k)
            {
                int sx = mh_floor(l->e.pos_x + (double)det_rng_int_n(&l->rand, 20) - 10.0);
                int sy = mh_floor(l->e.bounding_box.min_y + (double)det_rng_int_n(&l->rand, 6) - 3.0);
                int sz = mh_floor(l->e.pos_z + (double)det_rng_int_n(&l->rand, 20) - 10.0);
                if (!world_can_block_see_the_sky(l->world, sx, sy, sz) &&
                    (0.5f - living_light_brightness(l->world, l->an ? l->an->skylight : 0, sx, sy, sz) < 0.0f))
                {
                    t->shelter_x = (double)sx;
                    t->shelter_y = (double)sy;
                    t->shelter_z = (double)sz;
                    return 1;
                }
            }
            return 0;
        }

        case AIC_ARROW_ATTACK:
        {
            /* EntityAIArrowAttack.shouldExecute: any attack target, alive or
             * not (the target tasks drop a dead one on their own run) */
            struct living *target = lv_get(l->attack_target);
            if (!target) return 0;
            t->target = lv_ref(target);
            return 1;
        }

        default:
            return 0;
    }

    (void)det;
}

static int ai_continue_executing(struct living *l, struct ai_task *t)
{
    switch (t->cls)
    {
        case AIC_RESTRICT_SUN:
            return l->an ? l->an->skylight < 4 : 0;

        case AIC_FLEE_SUN:
            return !nav_no_path(l);

        case AIC_ARROW_ATTACK:
            /* shouldExecute() || !noPath(): with no attack target left the
             * task runs on at its last target (dead or not) while a path
             * remains; the grave keeps that living while t->target names it */
            if (lv_get(l->attack_target) != NULL)
            {
                t->target = l->attack_target;
                return 1;
            }
            return !nav_no_path(l);

        case AIC_SWIMMING:
            return l->is_in_water || living_handle_lava_movement(l);

        case AIC_PANIC:
        case AIC_WANDER:
            return !nav_no_path(l);

        case AIC_CONTROLLED_BY_PLAYER:
            return ride_ai_should_execute(l, t);

        case AIC_MATE:
            return lv_get(t->target) != NULL && living_is_alive(lv_get(t->target)) && lv_get(t->target)->in_love > 0
                   && t->spawn_baby_delay < 60;

        case AIC_TEMPT:
            /* every tempting animal is built with scaredByPlayerMovement
             * false, so continueExecuting is shouldExecute */
            return ai_should_execute(l, t, l->an ? l->an->det : NULL);

        case AIC_FOLLOW_PARENT:
        {
            if (lv_get(t->target) == NULL || !living_is_alive(lv_get(t->target))) return 0;

            double d = dist_sq_to(l, lv_get(t->target));
            return d >= 9.0 && d <= 256.0;
        }

        case AIC_MOVE_INDOORS:
        case AIC_MOVE_TOWARDS_RESTRICTION:
            return !nav_no_path(l);

        case AIC_RESTRICT_OPEN_DOOR:
        {
            if (l->an->skylight < 4) return 0;
            struct village_door_info *door = door_deref(t->front_door);
            if (!door || door->is_detached_from_village_flag) return 0;
            int ex = mh_floor(l->e.pos_x);
            int ez = mh_floor(l->e.pos_z);
            return vdi_is_inside(door, ex, ez);
        }

        case AIC_OPEN_DOOR:
            /* EntityAIOpenDoor(this, true).continueExecuting: the counter and
             * EntityAIDoorInteract's hasStoppedDoorInteraction */
            return t->door_counter > 0 && !t->has_stopped_door_interaction;

        case AIC_MOVE_TOWARDS_TARGET:
        {
            if (nav_no_path(l) || !lv_get(t->target) || !living_is_alive(lv_get(t->target))) return 0;
            double dx = lv_get(t->target)->e.pos_x - l->e.pos_x, dy = lv_get(t->target)->e.pos_y - l->e.pos_y, dz = lv_get(t->target)->e.pos_z - l->e.pos_z;
            return dx * dx + dy * dy + dz * dz < (double)(t->max_watch_dist * t->max_watch_dist);
        }

        case AIC_VILLAGER_MATE:
        {
            if (t->mating_timeout < 0 || l->growing_age != 0) return 0;
            struct village *v = vc_village(l->an->village_collection, t->village_ptr);
            if (!v || !v_is_mating_season(v)) return 0;
            int max_v = (int)((double)((float)v->num_doors) * 0.35);
            return v->num_villagers < max_v;
        }

        case AIC_FOLLOW_GOLEM:
            return lv_get(t->target_golem) && lv_get(t->target_golem)->hold_rose_tick > 0;

        case AIC_PLAY:
            return t->play_time > 0;

        case AIC_WATCH_CLOSEST:
        case AIC_WATCH_CLOSEST2:
        {
            if (t->look_time <= 0) return 0;
            float md = t->max_watch_dist > 0.0F ? t->max_watch_dist : 8.0F;
            if (t->is_watching_player)
            {
                /* EntityAIWatchClosest.continueExecuting: !closestEntity.isEntityAlive()
                 * ends the watch: the dead player (still in playerEntities)
                 * stops being watched the tick after its first face */
                if (l->an->playerh != 0 && !living_is_alive(lv_get(l->an->playerh))) return 0;
                double dx = l->an->player_x - l->e.pos_x;
                double dy = l->an->player_y - l->e.pos_y;
                double dz = l->an->player_z - l->e.pos_z;
                if (dx * dx + dy * dy + dz * dz > (double)(md * md)) return 0;
                return 1;
            }
            else
            {
                if (!lv_get(t->target) || !living_is_alive(lv_get(t->target))) return 0;
                double dx = lv_get(t->target)->e.pos_x - l->e.pos_x;
                double dy = lv_get(t->target)->e.pos_y - l->e.pos_y;
                double dz = lv_get(t->target)->e.pos_z - l->e.pos_z;
                if (dx * dx + dy * dy + dz * dz > (double)(md * md)) return 0;
                return 1;
            }
        }

        case AIC_LOOK_AT_VILLAGER:
            return t->look_time > 0;

        case AIC_MOVE_THROUGH_VILLAGE:
        {
            if (nav_no_path(l)) return 0;
            struct village_door_info *door = door_deref(t->front_door);
            if (!door) return 0;
            float r = l->e.width + 4.0F;
            double dx = (double)door->pos_x - l->e.pos_x;
            double dy = (double)door->pos_y - l->e.pos_y;
            double dz = (double)door->pos_z - l->e.pos_z;
            return (dx * dx + dy * dy + dz * dz > (double)(r * r));
        }

        case AIC_LOOK_IDLE:
            return t->idle_time >= 0;

        case AIC_EAT_GRASS:
            return t->eat_grass_timer > 0;

        case AIC_BREAK_DOOR:
        {
            double dx = (double)t->door_x - l->e.pos_x;
            double dy = (double)t->door_y - l->e.pos_y;
            double dz = (double)t->door_z - l->e.pos_z;
            double dsq = dx * dx + dy * dy + dz * dz;
            int meta = world_get_meta(l->world, t->door_x, t->door_y, t->door_z);
            if (meta & 8) meta = world_get_meta(l->world, t->door_x, t->door_y - 1, t->door_z);
            int is_open = (meta & 4) != 0;
            return t->breaking_time <= 240 && !is_open && dsq < 4.0;
        }

        case AIC_ATTACK_ON_COLLIDE:
        {
            struct living *target = lv_get(l->attack_target);
            if (!target || !living_is_alive(target)) return 0;
            if (!t->long_memory)
            {
                return !nav_no_path(l);
            }
            else
            {
                return living_is_within_home_distance(l, mh_floor(target->e.pos_x),
                                                      mh_floor(target->e.pos_y),
                                                      mh_floor(target->e.pos_z));
            }
        }

        case AIC_HURT_BY_TARGET:
        case AIC_NEAREST_ATTACKABLE_TARGET:
        case AIC_DEFEND_VILLAGE:
        {
            struct living *target = lv_get(l->attack_target);
            if (!target || !living_is_alive(target)) return 0;
            double range = attrs_value(&l->attrs.a[ATTR_FOLLOW_RANGE]);
            double dx = l->e.pos_x - target->e.pos_x;
            double dy = l->e.pos_y - target->e.pos_y;
            double dz = l->e.pos_z - target->e.pos_z;
            if (dx * dx + dy * dy + dz * dz > range * range) return 0;
            if (t->should_check_sight)
            {
                if (senses_can_see(l, target))
                {
                    t->target_unseen_ticks = 0;
                }
                else if (++t->target_unseen_ticks > 60)
                {
                    return 0;
                }
            }
            return 1;
        }

        case AIC_CREEPER_SWELL:
            /* EntityAICreeperSwell has no continueExecuting of its own: the
             * EntityAIBase default is shouldExecute, so the task drops out the
             * moment the creeper's state clears and its target is gone
             * (and re-starts when either comes back) */
            return creeper_swell_should_execute(l, t);

        case AIC_AVOID_ENTITY:
            /* EntityAIAvoidEntity.continueExecuting */
            return !nav_no_path(l);

        case AIC_TRADE_PLAYER:
            /* EntityAIBase's default continueExecuting: shouldExecute again */
            return ai_should_execute(l, t, NULL);

        case AIC_LOOK_AT_TRADE_PLAYER:
        {
            /* EntityAIWatchClosest.continueExecuting over the customer: a
             * dead customer ends it (!isEntityAlive) */
            if (l->an->playerh != 0 && !living_is_alive(lv_get(l->an->playerh))) return 0;
            if (t->look_time <= 0) return 0;
            double dx = l->an->player_x - l->e.pos_x;
            double dy = l->an->player_y - l->e.pos_y;
            double dz = l->an->player_z - l->e.pos_z;
            return dx * dx + dy * dy + dz * dz <= 64.0;
        }

        default:
            return 0;
    }
}

static void ai_start(struct living *l, struct ai_task *t, det_state *det)
{
    switch (t->cls)
    {
        case AIC_CONTROLLED_BY_PLAYER:
            t->ctl_current_speed = 0.0F;
            break;

        case AIC_RESTRICT_SUN:
            l->nav.no_sun_pathfind = 1;
            break;

        case AIC_FLEE_SUN:
            nav_try_move_to_xyz(l, t->shelter_x, t->shelter_y, t->shelter_z, t->speed);
            break;

        case AIC_TEMPT:
            t->tempt_running = 1;
            t->tempt_old_avoid = l->nav.avoids_water;
            l->nav.avoids_water = 0;
            break;

        case AIC_ARROW_ATTACK:
            break;

        case AIC_PANIC:
            nav_try_move_to_xyz(l, t->x, t->y, t->z, t->speed);
            break;

        case AIC_FOLLOW_PARENT:
            t->follow_parent_delay = 0;
            break;

        case AIC_WANDER:
            nav_try_move_to_xyz(l, t->x, t->y, t->z, t->speed);
            break;

        case AIC_MOVE_INDOORS:
        {
            t->inside_pos_x = -1;
            t->inside_pos_z = -1;
            struct village_door_info *door = door_deref(t->front_door);
            if (door)
            {
                int ix = vdi_inside_x(door);
                int iy = vdi_inside_y(door);
                int iz = vdi_inside_z(door);
                double dx = (double)ix - l->e.pos_x;
                double dy = (double)iy - l->e.pos_y;
                double dz = (double)iz - l->e.pos_z;
                if (dx * dx + dy * dy + dz * dz > 256.0)
                {
                    int rx, ry, rz;
                    if (random_target_towards(l, 14, 3, (double)ix + 0.5, (double)iy, (double)iz + 0.5, &rx, &ry, &rz))
                    {
                        nav_try_move_to_xyz(l, (double)rx, (double)ry, (double)rz, 1.0);
                    }
                }
                else
                {
                    nav_try_move_to_xyz(l, (double)ix + 0.5, (double)iy, (double)iz + 0.5, 1.0);
                }
            }
            break;
        }

        case AIC_RESTRICT_OPEN_DOOR:
            l->nav.can_pass_closed_doors = 0;
            l->nav.can_pass_open_doors = 0;
            break;

        case AIC_OPEN_DOOR:
            /* EntityAIOpenDoor.startExecuting does not call
             * EntityAIDoorInteract.startExecuting: hasStoppedDoorInteraction
             * and entityPositionX/Z keep their initial false and 0.0F, so
             * updateTask's dot product is always 0 and the flag never sets */
            t->door_counter = 20;
            door_set_open(l->world, t->door_x, t->door_y, t->door_z, 1, l->an->on_write_cb, l->an->on_write_ctx);
            break;

        case AIC_MOVE_TOWARDS_RESTRICTION:
            nav_try_move_to_xyz(l, t->x, t->y, t->z, t->speed);
            break;

        case AIC_VILLAGER_MATE:
            t->mating_timeout = 300;
            l->is_mating = 1;
            break;

        case AIC_FOLLOW_GOLEM:
            t->take_golem_rose_tick = det_rng_int_n(&l->rand, 320);
            t->took_golem_rose = 0;
            if (lv_get(t->target_golem)) nav_clear_path(lv_get(t->target_golem));
            break;

        case AIC_PLAY:
            if (lv_get(t->target_child)) l->is_playing = 1;
            t->play_time = 1000;
            break;

        case AIC_WATCH_CLOSEST:
        case AIC_WATCH_CLOSEST2:
            t->look_time = 40 + det_rng_int_n(&l->rand, 40);
            break;

        case AIC_LOOK_AT_VILLAGER:
            t->look_time = 400;
            iron_golem_set_holding_rose(l, 1);
            break;

        case AIC_MOVE_THROUGH_VILLAGE:
            if (t->mtv_path)
            {
                nav_set_path(l, t->mtv_path, t->speed);
                t->mtv_path = 0;
            }
            break;

        case AIC_LOOK_IDLE:
        {
            double v1 = (3.141592653589793 * 2.0) * det_rng_double(&l->rand);
            t->x = fd_cos(v1);
            t->z = fd_sin(v1);
            t->idle_time = 20 + det_rng_int_n(&l->rand, 20);
            trace("lookidle", "iddd", l->entity_id, v1, t->x, t->z);
            break;
        }

        case AIC_EAT_GRASS:
            t->eat_grass_timer = 40;
            /* setEntityState(this, (byte)10): World.setEntityState is empty */
            nav_clear_path(l);
            break;

        case AIC_BREAK_DOOR:
            t->breaking_time = 0;
            /* field_75358_j keeps the stage the last run sent */
            break;

        case AIC_ATTACK_ON_COLLIDE:
            if (t->path)
            {
                nav_set_path(l, t->path, t->speed);
                t->path = 0;
            }
            t->collide_cooldown = 0;
            break;

        case AIC_CREEPER_SWELL:
            /* EntityAICreeperSwell.startExecuting */
            nav_clear_path(l);
            t->target = l->attack_target;
            break;

        case AIC_AVOID_ENTITY:
            /* EntityAIAvoidEntity.startExecuting: setPath(entityPathEntity, farSpeed) */
            nav_set_path(l, t->path, t->speed);
            t->path = 0;
            break;

        case AIC_MOVE_TOWARDS_TARGET:
            nav_try_move_to_xyz(l, t->x, t->y, t->z, t->speed);
            break;

        case AIC_DEFEND_VILLAGE:
            l->attack_target = t->target;
            t->target_search_status = 0;
            t->target_search_delay = 0;
            t->target_unseen_ticks = 0;
            break;

        case AIC_TRADE_PLAYER:
            /* EntityAITradePlayer.startExecuting */
            nav_clear_path(l);
            break;

        case AIC_LOOK_AT_TRADE_PLAYER:
            /* EntityAIWatchClosest.startExecuting over the customer */
            t->look_time = 40 + det_rng_int_n(&l->rand, 40);
            break;

        case AIC_HURT_BY_TARGET:
        {
            l->attack_target = l->entity_living_to_attack;
            t->revenge_timer_last = l->revenge_timer;
            t->target_unseen_ticks = 0;
            if (t->calls_for_help)
            {
                double range = attrs_value(&l->attrs.a[ATTR_FOLLOW_RANGE]);
                struct aabb box = {
                    .min_x = l->e.pos_x - range,
                    .min_y = l->e.pos_y - 10.0,
                    .min_z = l->e.pos_z - range,
                    .max_x = l->e.pos_x + range + 1.0,
                    .max_y = l->e.pos_y + 10.0 + 1.0,
                    .max_z = l->e.pos_z + range + 1.0
                };
                AN_QUERY_LIST(ents);
                int n = an_entities_within_aabb(l->an, l->kind, &box, ents, AN_MAX_ENTITIES);
                for (int i = 0; i < n; ++i)
                {
                    if (!ents[i]->is_living || !ents[i]->livh) continue;
                    struct living *other = lv_get(ents[i]->livh);
                    if (other != l && lv_get(other->attack_target) == NULL)
                    {
                        other->attack_target = l->entity_living_to_attack;
                    }
                }
            }
            break;
        }

        case AIC_NEAREST_ATTACKABLE_TARGET:
            l->attack_target = t->target;
            /* EntityAITarget.startExecuting */
            t->target_search_status = 0;
            t->target_search_delay = 0;
            t->target_unseen_ticks = 0;
            break;

        default:
            break;
    }

    (void)det;
}

static void ai_reset(struct living *l, struct ai_task *t)
{
    switch (t->cls)
    {
        case AIC_CONTROLLED_BY_PLAYER:
            t->ctl_speed_boosted = 0;
            t->ctl_current_speed = 0.0F;
            break;

        case AIC_RESTRICT_SUN:
            l->nav.no_sun_pathfind = 0;
            break;

        case AIC_FLEE_SUN:
            break;

        case AIC_ARROW_ATTACK:
            t->target = 0;
            t->field_75318_f = 0;
            t->ranged_attack_time = -1;
            break;

        case AIC_MATE:
            t->target = 0;
            t->spawn_baby_delay = 0;
            break;

        case AIC_FOLLOW_PARENT:
            t->target = 0;
            break;

        case AIC_TEMPT:
            t->target = 0;
            nav_clear_path(l);
            t->delay_tempt_counter = 100;
            t->tempt_running = 0;
            l->nav.avoids_water = t->tempt_old_avoid;
            break;

        case AIC_MOVE_INDOORS:
        {
            struct village_door_info *door = door_deref(t->front_door);
            if (door)
            {
                t->inside_pos_x = vdi_inside_x(door);
                t->inside_pos_z = vdi_inside_z(door);
            }
            t->front_door = 0;
            break;
        }

        case AIC_RESTRICT_OPEN_DOOR:
            l->nav.can_pass_closed_doors = 1;
            l->nav.can_pass_open_doors = 1;
            t->front_door = 0;
            break;

        case AIC_OPEN_DOOR:
            door_set_open(l->world, t->door_x, t->door_y, t->door_z, 0, l->an->on_write_cb, l->an->on_write_ctx);
            break;

        case AIC_VILLAGER_MATE:
            t->village_ptr = 0;
            t->target_mate = 0;
            l->is_mating = 0;
            break;

        case AIC_FOLLOW_GOLEM:
            t->target_golem = 0;
            nav_clear_path(l);
            break;

        case AIC_PLAY:
            l->is_playing = 0;
            t->target_child = 0;
            break;

        case AIC_WATCH_CLOSEST:
        case AIC_WATCH_CLOSEST2:
            t->target = 0;
            t->is_watching_player = 0;
            break;

        case AIC_LOOK_AT_VILLAGER:
            iron_golem_set_holding_rose(l, 0);
            t->target = 0;
            break;

        case AIC_MOVE_THROUGH_VILLAGE:
        {
            struct village_door_info *door = door_deref(t->front_door);
            if (door)
            {
                double dx = (double)door->pos_x - l->e.pos_x;
                double dy = (double)door->pos_y - l->e.pos_y;
                double dz = (double)door->pos_z - l->e.pos_z;
                if (nav_no_path(l) || dx * dx + dy * dy + dz * dz < 16.0)
                {
                    if (lv_ai(l)->mtv_nvisited_doors < 16)
                    {
                        lv_ai(l)->mtv_visited_doors[lv_ai(l)->mtv_nvisited_doors][0] = door->pos_x;
                        lv_ai(l)->mtv_visited_doors[lv_ai(l)->mtv_nvisited_doors][1] = door->pos_y;
                        lv_ai(l)->mtv_visited_doors[lv_ai(l)->mtv_nvisited_doors][2] = door->pos_z;
                        ++lv_ai(l)->mtv_nvisited_doors;
                    }
                }
            }
            t->front_door = 0;
            break;
        }

        case AIC_EAT_GRASS:
            t->eat_grass_timer = 0;
            break;

        case AIC_BREAK_DOOR:
            t->breaking_time = 0;
            /* resetTask's destroyBlockInWorldPartially(-1); the stage it
             * last sent stays, as startExecuting leaves it */
            ai_block_break_event(l, t->door_x, t->door_y, t->door_z, -1);
            break;

        case AIC_ATTACK_ON_COLLIDE:
            nav_clear_path(l);
            break;

        case AIC_HURT_BY_TARGET:
        case AIC_NEAREST_ATTACKABLE_TARGET:
        case AIC_DEFEND_VILLAGE:
            l->attack_target = 0;
            break;

        case AIC_MOVE_TOWARDS_TARGET:
            t->target = 0;
            break;

        case AIC_CREEPER_SWELL:
            /* EntityAICreeperSwell.resetTask */
            t->target = 0;
            break;

        case AIC_AVOID_ENTITY:
            /* EntityAIAvoidEntity.resetTask */
            t->target = 0;
            break;

        case AIC_TRADE_PLAYER:
            /* EntityAITradePlayer.resetTask */
            l->buying_player = 0;
            break;

        default:
            break;
    }
}

static void ai_update(struct living *l, struct ai_task *t, det_state *det)
{
    switch (t->cls)
    {
        case AIC_CONTROLLED_BY_PLAYER:
            ride_ai_update(l, t, det);
            break;

        case AIC_SWIMMING:
            if (det_rng_float(&l->rand) < 0.8F) l->jump.is_jumping = 1;
            break;

        case AIC_TEMPT:
        {
            /* setLookPositionWithEntity(player, 30, verticalFaceSpeed), then
             * the stop inside 2.5 blocks or tryMoveToEntityLiving */
            double px, py, pz;
            tempt_player(l, &px, &py, &pz);
            look_set_position(l, px, py + (double)1.62F, pz, 30.0F, 40.0F);
            double dx = l->e.pos_x - px, dy = l->e.pos_y - py, dz = l->e.pos_z - pz;
            if (dx * dx + dy * dy + dz * dz < 6.25) nav_clear_path(l);
            else
            {
                pathref p = nav_get_path_to_point(l, -1, px, py, pz);
                if (p) nav_set_path(l, p, t->speed);
            }
            break;
        }

        case AIC_MATE:
            look_set_position_with_entity(l, lv_get(t->target), 10.0F, 40.0F);
            nav_try_move_to_entity(l, lv_get(t->target), t->speed);
            ++t->spawn_baby_delay;

            if (t->spawn_baby_delay >= 60 && dist_sq_to(l, lv_get(t->target)) < 9.0) spawn_baby(l, lv_get(t->target), det);

            break;

        case AIC_FOLLOW_PARENT:
            if (--t->follow_parent_delay <= 0)
            {
                t->follow_parent_delay = 10;
                nav_try_move_to_entity(l, lv_get(t->target), t->speed);
            }

            break;

        case AIC_RESTRICT_OPEN_DOOR:
            if (t->front_door)
            {
                struct village_door_info *door = door_deref(t->front_door);
                door->door_opening_restriction_counter++;
            }
            break;

        case AIC_OPEN_DOOR:
        {
            --t->door_counter;
            /* EntityAIDoorInteract.updateTask */
            float v1 = (float)((double)((float)t->door_x + 0.5F) - l->e.pos_x);
            float v2 = (float)((double)((float)t->door_z + 0.5F) - l->e.pos_z);
            float v3 = t->door_pos_x * v1 + t->door_pos_z * v2;
            if (v3 < 0.0F) t->has_stopped_door_interaction = 1;
            break;
        }

        case AIC_AVOID_ENTITY:
            /* EntityAIAvoidEntity.updateTask: the near and far speeds (the
             * villager's are both 0.6) */
            if (lv_get(t->target))
            {
                double dx = l->e.pos_x - lv_get(t->target)->e.pos_x, dy = l->e.pos_y - lv_get(t->target)->e.pos_y, dz = l->e.pos_z - lv_get(t->target)->e.pos_z;
                l->nav.speed = dx * dx + dy * dy + dz * dz < 49.0 ? t->avoid_near_speed : t->speed;
            }
            break;

        case AIC_VILLAGER_MATE:
        {
            --t->mating_timeout;
            if (lv_get(t->target_mate))
            {
                look_set_position_with_entity(l, lv_get(t->target_mate), 10.0F, 30.0F);
                double dx = l->e.pos_x - lv_get(t->target_mate)->e.pos_x;
                double dy = l->e.pos_y - lv_get(t->target_mate)->e.pos_y;
                double dz = l->e.pos_z - lv_get(t->target_mate)->e.pos_z;
                double dsq = dx * dx + dy * dy + dz * dz;
                if (dsq > 2.25)
                {
                    nav_try_move_to_entity(l, lv_get(t->target_mate), 0.25);
                }
                else if (t->mating_timeout == 0 && lv_get(t->target_mate)->is_mating)
                {
                    /* giveBirth */
                    struct living *child = villager_create_child(l, lv_get(t->target_mate), det);
                    living_set_growing_age(lv_get(t->target_mate), 6000);
                    living_set_growing_age(l, 6000);
                    if (child)
                    {
                        living_set_growing_age(child, -24000);
                        living_set_location_and_angles(child, l->e.pos_x, l->e.pos_y, l->e.pos_z, 0.0F, 0.0F);
                    }
                }
            }
            if (det_rng_int_n(&l->rand, 35) == 0)
            {
                /* heart particle */
            }
            break;
        }

        case AIC_FOLLOW_GOLEM:
        {
            if (lv_get(t->target_golem))
            {
                look_set_position_with_entity(l, lv_get(t->target_golem), 30.0F, 30.0F);
                if (lv_get(t->target_golem)->hold_rose_tick == t->take_golem_rose_tick)
                {
                    nav_try_move_to_entity(l, lv_get(t->target_golem), 0.5);
                    t->took_golem_rose = 1;
                }
                if (t->took_golem_rose)
                {
                    double dx = l->e.pos_x - lv_get(t->target_golem)->e.pos_x;
                    double dy = l->e.pos_y - lv_get(t->target_golem)->e.pos_y;
                    double dz = l->e.pos_z - lv_get(t->target_golem)->e.pos_z;
                    if (dx * dx + dy * dy + dz * dz < 4.0)
                    {
                        iron_golem_set_holding_rose(lv_get(t->target_golem), 0);
                        nav_clear_path(l);
                    }
                }
            }
            break;
        }

        case AIC_PLAY:
        {
            --t->play_time;
            if (lv_get(t->target_child))
            {
                double dx = l->e.pos_x - lv_get(t->target_child)->e.pos_x;
                double dy = l->e.pos_y - lv_get(t->target_child)->e.pos_y;
                double dz = l->e.pos_z - lv_get(t->target_child)->e.pos_z;
                if (dx * dx + dy * dy + dz * dz > 4.0)
                {
                    nav_try_move_to_entity(l, lv_get(t->target_child), t->speed);
                }
            }
            else if (nav_no_path(l))
            {
                int rx, ry, rz;
                if (random_target(l, 16, 3, &rx, &ry, &rz))
                {
                    nav_try_move_to_xyz(l, (double)rx, (double)ry, (double)rz, t->speed);
                }
            }
            break;
        }

        case AIC_WATCH_CLOSEST:
        case AIC_WATCH_CLOSEST2:
        {
            if (t->is_watching_player)
            {
                look_set_position(l, l->an->player_x, l->an->player_y + (double)1.62f, l->an->player_z, 10.0F, 40.0F);
            }
            else if (lv_get(t->target))
            {
                look_set_position(l, lv_get(t->target)->e.pos_x, lv_get(t->target)->e.pos_y + (double)living_eye_height(lv_get(t->target)), lv_get(t->target)->e.pos_z, 10.0F, 40.0F);
            }
            --t->look_time;
            break;
        }

        case AIC_LOOK_AT_TRADE_PLAYER:
            /* EntityAIWatchClosest.updateTask over the customer (the player's
             * eye height, EntityPlayer's 1.62) */
            look_set_position(l, l->an->player_x, l->an->player_y + (double)1.62f,
                              l->an->player_z, 10.0F, 40.0F);
            --t->look_time;
            break;

        case AIC_LOOK_AT_VILLAGER:
            if (lv_get(t->target)) look_set_position_with_entity(l, lv_get(t->target), 30.0F, 30.0F);
            --t->look_time;
            break;

        case AIC_LOOK_IDLE:
            --t->idle_time;
            look_set_position(l, l->e.pos_x + t->x, l->e.pos_y + (double)living_eye_height(l), l->e.pos_z + t->z,
                              10.0F, 40.0F);
            break;

        case AIC_EAT_GRASS:
        {
            t->eat_grass_timer = t->eat_grass_timer - 1;
            if (t->eat_grass_timer < 0) t->eat_grass_timer = 0;

            if (t->eat_grass_timer == 4)
            {
                int x = mh_floor(l->e.pos_x);
                int y = mh_floor(l->e.pos_y);
                int z = mh_floor(l->e.pos_z);

                if ((world_get_block(l->world, x, y, z) & 4095) == 31)
                {
                    if (living_mob_loot(l->world))
                    {
                        /* World.func_147480_a(x, y, z, false) */
                        env_aux_sfx(l->world, 2001, x, y, z, 31 + (world_get_meta(l->world, x, y, z) << 12));
                        world_set_block(l->world, x, y, z, 0, 0, 3);
                    }

                    animal_eat_grass_bonus(l);
                }
                else if ((world_get_block(l->world, x, y - 1, z) & 4095) == 2)
                {
                    if (living_mob_loot(l->world))
                    {
                        /* World.playAuxSFX(2001): the grass block */
                        env_aux_sfx(l->world, 2001, x, y - 1, z, 2);
                        world_set_block(l->world, x, y - 1, z, 3, 0, 2);
                    }

                    animal_eat_grass_bonus(l);
                }
            }

            break;
        }

        case AIC_BREAK_DOOR:
        {
            if (det_rng_int_n(&l->rand, 20) == 0)
            {
                env_aux_sfx(l->world, 1010, t->door_x, t->door_y, t->door_z, 0);
            }
            ++t->breaking_time;
            int bstage = (int)((float)t->breaking_time / 240.0f * 10.0f);
            if (bstage != t->field_75358_j)
            {
                /* destroyBlockInWorldPartially is the clients' S25 */
                ai_block_break_event(l, t->door_x, t->door_y, t->door_z, bstage);
                t->field_75358_j = bstage;
            }
            /* on HARD the 240th tick breaks the door: World.setBlockToAir
             * (the other half and its drop follow from the door's own
             * neighbour update); the 1012 and 2001 effects are the clients' */
            if (t->breaking_time == 240 && l->an != NULL && l->an->difficulty == 3)
            {
                world_set_block(l->world, t->door_x, t->door_y, t->door_z, 0, 0, 3);
                env_aux_sfx(l->world, 1012, t->door_x, t->door_y, t->door_z, 0);
                env_aux_sfx(l->world, 2001, t->door_x, t->door_y, t->door_z, 64);
            }
            break;
        }

        case AIC_ATTACK_ON_COLLIDE:
        {
            struct living *target = lv_get(l->attack_target);
            if (!target) break;
            look_set_position_with_entity(l, target, 30.0f, 30.0f);
            double dx = l->e.pos_x - target->e.pos_x;
            double dy = l->e.pos_y - target->e.bounding_box.min_y;
            double dz = l->e.pos_z - target->e.pos_z;
            double dsq = dx * dx + dy * dy + dz * dz;
            double reach = (double)(l->e.width * 2.0f * l->e.width * 2.0f + target->e.width);

            --t->collide_cooldown;
            if ((t->long_memory || senses_can_see(l, target)) && t->collide_cooldown <= 0 &&
                ((t->collide_px == 0.0 && t->collide_py == 0.0 && t->collide_pz == 0.0) ||
                 (target->e.pos_x - t->collide_px)*(target->e.pos_x - t->collide_px) +
                 (target->e.pos_y - t->collide_py)*(target->e.pos_y - t->collide_py) +
                 (target->e.pos_z - t->collide_pz)*(target->e.pos_z - t->collide_pz) >= 1.0 ||
                 det_rng_float(&l->rand) < 0.05f))
            {
                t->collide_px = target->e.pos_x;
                t->collide_py = target->e.bounding_box.min_y;
                t->collide_pz = target->e.pos_z;
                t->collide_cooldown = 4 + det_rng_int_n(&l->rand, 7);

                if (dsq > 1024.0) t->collide_cooldown += 10;
                else if (dsq > 256.0) t->collide_cooldown += 5;

                int dbg_ok = nav_try_move_to_entity(l, target, t->speed);
                if (!dbg_ok)
                {
                    t->collide_cooldown += 15;
                }
            }

            if (t->attack_tick > 0) --t->attack_tick;
            if (dsq <= reach && t->attack_tick <= 20)
            {
                t->attack_tick = 20;
                /* EntityCreature.attackEntityAsMob per kind: the creeper's
                 * override returns true without damaging (its blast is the
                 * damage), every other mob runs EntityMob's */
                if (l->kind == HK_CREEPER) creeper_attack_entity_as_mob(l, target, det);
                else if (l->kind == HK_SKELETON) skeleton_attack_entity_as_mob(l, target, det);
                else if (l->kind == VK_IRON_GOLEM) iron_golem_attack_entity_as_mob(l, target, det);
                else zombie_attack_entity_as_mob(l, target, det);
            }
            break;
        }

        case AIC_ARROW_ATTACK:
        {
            /* updateTask reads attackTarget as it is, dead or alive */
            struct living *target = lv_get(t->target);
            if (!target) break;

            double dx = l->e.pos_x - target->e.pos_x;
            double dy = l->e.pos_y - target->e.bounding_box.min_y;
            double dz = l->e.pos_z - target->e.pos_z;
            double var1 = dx * dx + dy * dy + dz * dz;

            int can_see = senses_can_see(l, target);
            if (can_see)
            {
                ++t->field_75318_f;
            }
            else
            {
                t->field_75318_f = 0;
            }

            if (var1 <= (double)t->field_82642_h && t->field_75318_f >= 20)
            {
                nav_clear_path(l);
            }
            else
            {
                nav_try_move_to_entity(l, target, t->speed);
            }

            look_set_position_with_entity(l, target, 30.0f, 30.0f);

            float var4;
            if (--t->ranged_attack_time == 0)
            {
                if (var1 > (double)t->field_82642_h || !can_see)
                {
                    break;
                }

                var4 = (float)sqrt(var1) / t->field_96562_i;
                float var5 = var4;
                if (var4 < 0.1f) var5 = 0.1f;
                if (var5 > 1.0f) var5 = 1.0f;

                if (l->kind == HK_WITCH) witch_attack_entity_with_ranged_attack(l, target, var5);
                else skeleton_attack_entity_with_ranged_attack(l, target, var5);
                float interval = var4 * (float)(t->max_ranged_attack_time - t->field_96561_g) + (float)t->field_96561_g;
                t->ranged_attack_time = mh_floor_float(interval);
            }
            else if (t->ranged_attack_time < 0)
            {
                var4 = (float)sqrt(var1) / t->field_96562_i;
                float interval = var4 * (float)(t->max_ranged_attack_time - t->field_96561_g) + (float)t->field_96561_g;
                t->ranged_attack_time = mh_floor_float(interval);
            }
            break;
        }

        case AIC_CREEPER_SWELL:
            /* EntityAICreeperSwell.updateTask */
            creeper_swell_update(l, t);
            break;

        default:
            break;
    }
}

/* EntityAIMate.spawnBaby. */
static void spawn_baby(struct living *l, struct living *mate, det_state *det)
{
    if (l->an && l->an->constructor_hook) l->an->constructor_hook(l->an, 1, l->an->constructor_ctx);
    struct living *child = animal_create_child(l, mate, l->an ? l->an->det : det);
    if (l->an && l->an->constructor_hook) l->an->constructor_hook(l->an, 0, l->an->constructor_ctx);

    trace("baby", "iiil", l->entity_id, mate->entity_id, child ? child->entity_id : -1,
          (int64_t)(child ? (uint64_t)child->rand.r.seed : (uint64_t)-1));

    if (!child) return;

    /* func_146083_cb of this animal, else of the mate: the player who fed
     * it gets animalsBred (and breedCow for a cow) */
    if ((l->love_player || mate->love_player) && l->an && l->an->bred_hook)
        l->an->bred_hook(l->an, l->kind, l->an->bred_ctx);

    living_set_growing_age(l, 6000);
    living_set_growing_age(mate, 6000);
    l->in_love = 0;
    mate->in_love = 0;

    living_set_growing_age(child, -24000);
    living_set_location_and_angles(child, l->e.pos_x, l->e.pos_y, l->e.pos_z, 0.0F, 0.0F);

    for (int i = 0; i < 7; ++i)
    {
        (void)det_rng_gaussian(&l->rand);
        (void)det_rng_gaussian(&l->rand);
        (void)det_rng_gaussian(&l->rand);
        (void)det_rng_float(&l->rand);
        (void)det_rng_float(&l->rand);
        (void)det_rng_float(&l->rand);
    }

    if (living_mob_loot(l->world))
    {
        int xp = det_rng_int_n(&l->rand, 7) + 1;
        an_spawn_orb(l->an, xp, l->e.pos_x, l->e.pos_y, l->e.pos_z);
    }
}

void ai_tasks_update(struct living *l, struct ai_tasks *t)
{
    int spawned[AI_MAX_ENTRIES];
    int nspawned = 0;

    if (t->tick_count++ % 3 == 0)
    {
        /* the executing set as a mask, kept with the list below */
        uint32_t exec = 0;
        for (int e = 0; e < t->nexec; ++e) exec |= 1u << t->executing[e];

        for (int i = 0; i < t->n; ++i)
        {
            int at = exec >> i & 1 ? task_index_of(t, i) : -1;

            if (at >= 0)
            {
                if (can_use_mask(t, i, exec) && ai_continue_executing(l, &t->entries[i].t)) continue;

                ai_reset(l, &t->entries[i].t);
                memmove(&t->executing[at], &t->executing[at + 1],
                        (size_t)(t->nexec - at - 1) * sizeof t->executing[0]);
                --t->nexec;
                exec &= ~(1u << i);
            }

            if (can_use_mask(t, i, exec) && ai_should_execute(l, &t->entries[i].t, l->an ? l->an->det : NULL))
            {
                spawned[nspawned++] = i;
                t->executing[t->nexec++] = i;
                exec |= 1u << i;
            }
        }
    }
    else
    {
        for (int e = 0; e < t->nexec; ++e)
        {
            if (!ai_continue_executing(l, &t->entries[t->executing[e]].t))
            {
                ai_reset(l, &t->entries[t->executing[e]].t);
                memmove(&t->executing[e], &t->executing[e + 1], (size_t)(t->nexec - e - 1) * sizeof t->executing[0]);
                --t->nexec;
                --e;
            }
        }
    }

    for (int i = 0; i < nspawned; ++i) ai_start(l, &t->entries[spawned[i]].t, l->an ? l->an->det : NULL);

    for (int e = 0; e < t->nexec; ++e) ai_update(l, &t->entries[t->executing[e]].t, l->an ? l->an->det : NULL);
}

/* --------------------------------------------------------- the kind lists */

/* The next free entry of a task list, or the program stops: a list never
 * holds more than AI_MAX_ENTRIES (living.h). */
static struct ai_entry *task_slot(struct ai_tasks *t)
{
    if (t->n >= AI_MAX_ENTRIES)
    {
        fprintf(stderr, "ai: more than %d tasks in one list\n", AI_MAX_ENTRIES);
        abort();
    }
    return &t->entries[t->n++];
}

static void add_task_full(struct living *l, int priority, int cls, int mutex, double speed, int item,
                          int watch_class, float watch_chance, float max_watch_dist, int nocturnal)
{
    struct ai_entry *e = task_slot(&lv_ai(l)->tasks);
    memset(e, 0, sizeof *e);
    e->priority = priority;
    e->t.cls = cls;
    e->t.mutex = mutex;
    e->t.speed = speed;
    e->t.watch_chance = watch_chance;
    e->t.max_watch_dist = max_watch_dist;
    /* the class's own parameters: the union's other members share their
     * bytes (living.h struct ai_task) */
    switch (cls)
    {
        case AIC_TEMPT: e->t.item_id = item; break;
        case AIC_WATCH_CLOSEST: case AIC_WATCH_CLOSEST2: case AIC_LOOK_AT_TRADE_PLAYER: case AIC_LOOK_AT_VILLAGER:
            e->t.watch_class = watch_class;
            break;
        case AIC_MOVE_THROUGH_VILLAGE: e->t.nocturnal = nocturnal; break;
        case AIC_MOVE_INDOORS:
            e->t.inside_pos_x = -1;
            e->t.inside_pos_z = -1;
            break;
        /* EntityAIBreakDoor.field_75358_j starts at -1 */
        case AIC_BREAK_DOOR: e->t.field_75358_j = -1; break;
        default: break;
    }
    if ((item && cls != AIC_TEMPT) || (nocturnal && cls != AIC_MOVE_THROUGH_VILLAGE)
        || (watch_class && cls != AIC_WATCH_CLOSEST && cls != AIC_WATCH_CLOSEST2))
    {
        fprintf(stderr, "ai: task class %d takes no item, nocturnal or watch class\n", cls);
        abort();
    }
}

static void add_task(struct living *l, int priority, int cls, int mutex, double speed, int item)
{
    add_task_full(l, priority, cls, mutex, speed, item, 0, 0.0f, 0.0f, 0);
}

static void add_target_task(struct living *l, int priority, int cls, int mutex)
{
    struct ai_entry *e = task_slot(&lv_ai(l)->target_tasks);
    memset(e, 0, sizeof *e);
    e->priority = priority;
    e->t.cls = cls;
    e->t.mutex = mutex;
}

static void add_target_task_full(struct living *l, int priority, int cls, int mutex, int target_class,
                                float watch_chance, int should_check_sight, int nearby_only, int calls_for_help)
{
    struct ai_entry *e = task_slot(&lv_ai(l)->target_tasks);
    memset(e, 0, sizeof *e);
    e->priority = priority;
    e->t.cls = cls;
    e->t.mutex = mutex;
    e->t.target_class = target_class;
    e->t.watch_chance = watch_chance;
    e->t.should_check_sight = should_check_sight;
    e->t.nearby_only = nearby_only;
    e->t.calls_for_help = calls_for_help;
}

/* EntityCreature.updateLeashedState's field_110178_bs:
 * tasks.addTask(2, new EntityAIMoveTowardsRestriction(this, 1.0D)). */
void ai_add_leash_restriction(struct living *l)
{
    add_task_full(l, 2, AIC_MOVE_TOWARDS_RESTRICTION, 1, 1.0, 0, 0, 0.0f, 0.0f, 0);
}

/* World.destroyBlockInWorldPartially: the S25 the players get (env.h's
 * block_break) */
static void ai_block_break_event(struct living *l, int x, int y, int z, int stage)
{
    if (nw_env->block_break.n >= (int)(sizeof nw_env->block_break.e / sizeof nw_env->block_break.e[0])) return;
    int k = nw_env->block_break.n++;
    nw_env->block_break.e[k].id = l->entity_id;
    nw_env->block_break.e[k].x = x;
    nw_env->block_break.e[k].y = y;
    nw_env->block_break.e[k].z = z;
    nw_env->block_break.e[k].stage = stage;
}

void ai_add_break_door(struct living *l)
{
    add_task_full(l, 1, AIC_BREAK_DOOR, 0, 0.0, 0, 0, 0.0f, 0.0f, 0);
}

void ai_setup_kind(struct living *l, det_state *det)
{
    (void)det;

    /* the squid and the bat add no tasks: the squid's is the old AI, the
     * bat's updateAITicks body carries its own */
    if (l->kind == AK_SQUID || l->kind == AK_BAT) return;

    l->nav.can_swim = 1;   /* EntityAISwimming's constructor, which every kind adds */
    l->nav.can_pass_open_doors = 1;
    /* avoidsWater is the kind constructor's (animal_construct): EntityAnimal's
     * kinds set it before they add their tasks, the chicken never does */

    switch (l->kind)
    {
        case AK_PIG:
            add_task(l, 0, AIC_SWIMMING, 4, 0.0, 0);
            add_task(l, 1, AIC_PANIC, 1, 1.25, 0);
            add_task(l, 2, AIC_CONTROLLED_BY_PLAYER, 7, 0.0, 0);
            add_task(l, 3, AIC_MATE, 3, 1.0, 0);
            add_task(l, 4, AIC_TEMPT, 3, 1.2, 398);   /* carrot on a stick */
            add_task(l, 4, AIC_TEMPT, 3, 1.2, 391);   /* carrot */
            add_task(l, 5, AIC_FOLLOW_PARENT, 0, 1.1, 0);
            add_task(l, 6, AIC_WANDER, 1, 1.0, 0);
            add_task_full(l, 7, AIC_WATCH_CLOSEST, 2, 0.0, 0, 0, 0.02f, 6.0f, 0);
            add_task(l, 8, AIC_LOOK_IDLE, 3, 0.0, 0);
            break;

        case AK_COW:
        case AK_MOOSHROOM:
            add_task(l, 0, AIC_SWIMMING, 4, 0.0, 0);
            add_task(l, 1, AIC_PANIC, 1, 2.0, 0);
            add_task(l, 2, AIC_MATE, 3, 1.0, 0);
            add_task(l, 3, AIC_TEMPT, 3, 1.25, 296);  /* wheat */
            add_task(l, 4, AIC_FOLLOW_PARENT, 0, 1.25, 0);
            add_task(l, 5, AIC_WANDER, 1, 1.0, 0);
            add_task_full(l, 6, AIC_WATCH_CLOSEST, 2, 0.0, 0, 0, 0.02f, 6.0f, 0);
            add_task(l, 7, AIC_LOOK_IDLE, 3, 0.0, 0);
            break;

        case AK_CHICKEN:
            add_task(l, 0, AIC_SWIMMING, 4, 0.0, 0);
            add_task(l, 1, AIC_PANIC, 1, 1.4, 0);
            add_task(l, 2, AIC_MATE, 3, 1.0, 0);
            add_task(l, 3, AIC_TEMPT, 3, 1.0, 295);   /* wheat seeds */
            add_task(l, 4, AIC_FOLLOW_PARENT, 0, 1.1, 0);
            add_task(l, 5, AIC_WANDER, 1, 1.0, 0);
            add_task_full(l, 6, AIC_WATCH_CLOSEST, 2, 0.0, 0, 0, 0.02f, 6.0f, 0);
            add_task(l, 7, AIC_LOOK_IDLE, 3, 0.0, 0);
            break;

        case AK_SHEEP:
            add_task(l, 0, AIC_SWIMMING, 4, 0.0, 0);
            add_task(l, 1, AIC_PANIC, 1, 1.25, 0);
            add_task(l, 2, AIC_MATE, 3, 1.0, 0);
            add_task(l, 3, AIC_TEMPT, 3, 1.1, 296);   /* wheat */
            add_task(l, 4, AIC_FOLLOW_PARENT, 0, 1.1, 0);
            add_task(l, 5, AIC_EAT_GRASS, 7, 0.0, 0);
            add_task(l, 6, AIC_WANDER, 1, 1.0, 0);
            add_task_full(l, 7, AIC_WATCH_CLOSEST, 2, 0.0, 0, 0, 0.02f, 6.0f, 0);
            add_task(l, 8, AIC_LOOK_IDLE, 3, 0.0, 0);
            break;

        case VK_VILLAGER:
            add_task_full(l, 0, AIC_SWIMMING, 4, 0.0, 0, 0, 0.0f, 0.0f, 0);
            /* EntityAIAvoidEntity(this, EntityZombie.class, 8.0F, 0.6D, 0.6D) */
            add_task_full(l, 1, AIC_AVOID_ENTITY, 1, 0.6, 0, 0, 0.0f, 8.0f, 0);
            lv_ai(l)->tasks.entries[lv_ai(l)->tasks.n - 1].t.target_class = HK_ZOMBIE;
            lv_ai(l)->tasks.entries[lv_ai(l)->tasks.n - 1].t.avoid_near_speed = 0.6;
            add_task_full(l, 1, AIC_TRADE_PLAYER, 5, 0.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 1, AIC_LOOK_AT_TRADE_PLAYER, 2, 0.0, 0, 0, 0.0f, 8.0f, 0);
            add_task_full(l, 2, AIC_MOVE_INDOORS, 1, 1.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 3, AIC_RESTRICT_OPEN_DOOR, 0, 0.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 4, AIC_OPEN_DOOR, 0, 0.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 5, AIC_MOVE_TOWARDS_RESTRICTION, 1, 0.6, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 6, AIC_VILLAGER_MATE, 3, 0.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 7, AIC_FOLLOW_GOLEM, 3, 0.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 8, AIC_PLAY, 1, 0.32, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 9, AIC_WATCH_CLOSEST2, 3, 0.0, 0, 0, 1.0f, 3.0f, 0);
            add_task_full(l, 9, AIC_WATCH_CLOSEST2, 3, 0.0, 0, 1, 0.02f, 5.0f, 0);
            add_task_full(l, 9, AIC_WANDER, 1, 0.6, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 10, AIC_WATCH_CLOSEST, 2, 0.0, 0, 2, 0.02f, 8.0f, 0);
            break;

        case VK_IRON_GOLEM:
            /* the golem adds no EntityAISwimming: its navigator never swims
             * (a path from under water starts at its feet, not the surface) */
            l->nav.can_swim = 0;
            /* EntityAIAttackOnCollide(this, 1.0D, true): any class, longMemory */
            add_task_full(l, 1, AIC_ATTACK_ON_COLLIDE, 3, 1.0, 0, 0, 0.0f, 0.0f, 0);
            lv_ai(l)->tasks.entries[lv_ai(l)->tasks.n - 1].t.long_memory = 1;
            add_task_full(l, 2, AIC_MOVE_TOWARDS_TARGET, 1, 0.9, 0, 0, 0.0f, 32.0f, 0);
            add_task_full(l, 3, AIC_MOVE_THROUGH_VILLAGE, 1, 0.6, 0, 0, 0.0f, 0.0f, 1);
            add_task_full(l, 4, AIC_MOVE_TOWARDS_RESTRICTION, 1, 1.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 5, AIC_LOOK_AT_VILLAGER, 3, 0.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 6, AIC_WANDER, 1, 0.6, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 7, AIC_WATCH_CLOSEST, 2, 0.0, 0, 0, 0.02f, 6.0f, 0);
            add_task_full(l, 8, AIC_LOOK_IDLE, 3, 0.0, 0, 0, 0.0f, 0.0f, 0);

            /* EntityAIDefendVillage: EntityAITarget(golem, false, true) */
            add_target_task_full(l, 1, AIC_DEFEND_VILLAGE, 1, 0, 0.0f, 0, 1, 0);
            add_target_task(l, 2, AIC_HURT_BY_TARGET, 1);
            /* EntityAINearestAttackableTarget(this, EntityLiving.class, 0,
             * false, true, IMob.mobSelector) */
            add_target_task_full(l, 3, AIC_NEAREST_ATTACKABLE_TARGET, 1, -2, 0.0f, 0, 1, 0);
            break;

        case HK_PIGMAN:
        case HK_ZOMBIE:
            add_task_full(l, 0, AIC_SWIMMING, 4, 0.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 2, AIC_ATTACK_ON_COLLIDE, 3, 1.0, 0, 0, 0.0f, 0.0f, 0);
            lv_ai(l)->tasks.entries[lv_ai(l)->tasks.n - 1].t.target_class = HK_PLAYER;
            lv_ai(l)->tasks.entries[lv_ai(l)->tasks.n - 1].t.long_memory = 0;

            add_task_full(l, 4, AIC_ATTACK_ON_COLLIDE, 3, 1.0, 0, 0, 0.0f, 0.0f, 0);
            lv_ai(l)->tasks.entries[lv_ai(l)->tasks.n - 1].t.target_class = VK_VILLAGER;
            lv_ai(l)->tasks.entries[lv_ai(l)->tasks.n - 1].t.long_memory = 1;

            add_task_full(l, 5, AIC_MOVE_TOWARDS_RESTRICTION, 1, 1.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 6, AIC_MOVE_THROUGH_VILLAGE, 1, 1.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 7, AIC_WANDER, 1, 1.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 8, AIC_WATCH_CLOSEST, 2, 0.0, 0, 0, 0.02f, 8.0f, 0);
            add_task_full(l, 8, AIC_LOOK_IDLE, 3, 0.0, 0, 0, 0.0f, 0.0f, 0);

            add_target_task_full(l, 1, AIC_HURT_BY_TARGET, 1, 0, 0.0f, 0, 0, 1);
            add_target_task_full(l, 2, AIC_NEAREST_ATTACKABLE_TARGET, 1, HK_PLAYER, 0.0f, 1, 0, 0);
            add_target_task_full(l, 2, AIC_NEAREST_ATTACKABLE_TARGET, 1, VK_VILLAGER, 0.0f, 0, 0, 0);
            break;

        case HK_SKELETON:
            add_task(l, 1, AIC_SWIMMING, 4, 0.0, 0);
            add_task(l, 2, AIC_RESTRICT_SUN, 0, 0.0, 0);
            add_task(l, 3, AIC_FLEE_SUN, 1, 1.0, 0);
            add_task(l, 5, AIC_WANDER, 1, 1.0, 0);
            add_task_full(l, 6, AIC_WATCH_CLOSEST, 2, 0.0, 0, 0, 0.02f, 8.0f, 0);
            add_task_full(l, 6, AIC_LOOK_IDLE, 3, 0.0, 0, 0, 0.0f, 0.0f, 0);

            add_target_task_full(l, 1, AIC_HURT_BY_TARGET, 1, 0, 0.0f, 0, 0, 0);
            add_target_task_full(l, 2, AIC_NEAREST_ATTACKABLE_TARGET, 1, HK_PLAYER, 0.0f, 1, 0, 0);
            break;

        case HK_CREEPER:
            /* EntityCreeper's constructor, in its order */
            add_task_full(l, 1, AIC_SWIMMING, 4, 0.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 2, AIC_CREEPER_SWELL, 1, 0.0, 0, 0, 0.0f, 0.0f, 0);

            /* EntityAIAvoidEntity(this, EntityOcelot.class, 6.0F, 1.0D, 1.2D):
             * the arena holds no ocelot, so shouldExecute's class query is empty
             * and the task never starts (and draws nothing) */
            add_task_full(l, 3, AIC_AVOID_ENTITY, 1, 1.0, 0, 0, 0.0f, 6.0f, 0);
            lv_ai(l)->tasks.entries[lv_ai(l)->tasks.n - 1].t.avoid_near_speed = 1.2;

            add_task_full(l, 4, AIC_ATTACK_ON_COLLIDE, 3, 1.0, 0, 0, 0.0f, 0.0f, 0);
            lv_ai(l)->tasks.entries[lv_ai(l)->tasks.n - 1].t.long_memory = 0;

            add_task_full(l, 5, AIC_WANDER, 1, 0.8, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 6, AIC_WATCH_CLOSEST, 2, 0.0, 0, 0, 0.02f, 8.0f, 0);
            add_task_full(l, 6, AIC_LOOK_IDLE, 3, 0.0, 0, 0, 0.0f, 0.0f, 0);

            add_target_task_full(l, 1, AIC_NEAREST_ATTACKABLE_TARGET, 1, HK_PLAYER, 0.0f, 1, 0, 0);
            add_target_task_full(l, 2, AIC_HURT_BY_TARGET, 1, 0, 0.0f, 0, 0, 0);
            break;

        case HK_WITCH:
            add_task_full(l, 1, AIC_SWIMMING, 4, 0.0, 0, 0, 0.0f, 0.0f, 0);
            ai_add_task_arrow_attack(l, 2, 1.0, 60, 60, 10.0f);
            add_task_full(l, 2, AIC_WANDER, 1, 1.0, 0, 0, 0.0f, 0.0f, 0);
            add_task_full(l, 3, AIC_WATCH_CLOSEST, 2, 0.0, 0, 0, 0.02f, 8.0f, 0);
            add_task_full(l, 3, AIC_LOOK_IDLE, 3, 0.0, 0, 0, 0.0f, 0.0f, 0);

            add_target_task_full(l, 1, AIC_HURT_BY_TARGET, 1, 0, 0.0f, 0, 0, 0);
            add_target_task_full(l, 2, AIC_NEAREST_ATTACKABLE_TARGET, 1, HK_PLAYER, 0.0f, 1, 0, 0);
            break;

        default:
            break;
    }
}
void ai_remove_task(struct living *l, int cls)
{
    struct ai_tasks *t = &lv_ai(l)->tasks;
    for (int i = 0; i < t->n; ++i)
    {
        if (t->entries[i].t.cls == cls)
        {
            for (int e = 0; e < t->nexec; ++e)
            {
                if (t->executing[e] == i)
                {
                    ai_reset(l, &t->entries[i].t);
                    memmove(&t->executing[e], &t->executing[e + 1],
                            (size_t)(t->nexec - e - 1) * sizeof t->executing[0]);
                    --t->nexec;
                    break;
                }
            }
            for (int e = 0; e < t->nexec; ++e)
            {
                if (t->executing[e] > i)
                {
                    --t->executing[e];
                }
            }
            memmove(&t->entries[i], &t->entries[i + 1],
                    (size_t)(t->n - i - 1) * sizeof t->entries[0]);
            --t->n;
            --i;
        }
    }
}

void ai_add_task_arrow_attack(struct living *l, int priority, double speed, int min_interval, int max_interval, float max_dist)
{
    struct ai_entry *e = task_slot(&lv_ai(l)->tasks);
    memset(e, 0, sizeof *e);
    e->priority = priority;
    e->t.cls = AIC_ARROW_ATTACK;
    e->t.mutex = 3;
    e->t.speed = speed;

    e->t.field_96561_g = min_interval;
    e->t.max_ranged_attack_time = max_interval;
    e->t.field_96562_i = max_dist;
    e->t.field_82642_h = max_dist * max_dist;
    e->t.ranged_attack_time = -1;
    e->t.field_75318_f = 0;
}

void ai_add_task_attack_on_collide(struct living *l, int priority, double speed, int long_memory)
{
    struct ai_entry *e = task_slot(&lv_ai(l)->tasks);
    memset(e, 0, sizeof *e);
    e->priority = priority;
    e->t.cls = AIC_ATTACK_ON_COLLIDE;
    e->t.mutex = 3;
    e->t.speed = speed;
    e->t.long_memory = long_memory;
    e->t.target_class = HK_PLAYER;
}

/* EntityList.createEntityFromNBT's constructor half: the kind's own
 * applyEntityAttributes/size/AI setup, without the natural-spawn extras
 * (the persistence flag, the spider jockey list order). The summon path
 * (dev_summon through an_spawn_living) needs exactly this. */
void an_construct_kind(struct living *l, det_state *det)
{
    switch (l->kind)
    {
        case AK_PIG: case AK_COW: case AK_MOOSHROOM: case AK_CHICKEN:
        case AK_SHEEP: case AK_SQUID: case AK_BAT:
            animal_construct(l, det); break;
        case VK_VILLAGER: villager_construct(l, det); break;
        case VK_IRON_GOLEM: iron_golem_construct(l, det); break;
        case SK_SLIME: case SK_MAGMA_CUBE: slime_construct(l, det); break;
        case HK_ZOMBIE: zombie_construct(l, det); break;
        case HK_SKELETON: skeleton_construct(l, det); break;
        case HK_CREEPER: creeper_construct(l, det); break;
        case HK_SPIDER: case HK_CAVE_SPIDER: spider_construct(l, det); break;
        case HK_ENDERMAN: enderman_construct(l, det); break;
        case HK_WITCH: witch_construct(l, det); break;
        case HK_SILVERFISH: silverfish_construct(l, det); break;
        case HK_PIGMAN: pigman_construct(l, det); break;
        case HK_BLAZE: blaze_construct(l, det); break;
        case GK_GHAST: ghast_construct(l, det); break;
    }
}

