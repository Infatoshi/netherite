/* The client and server player ticks, ported from EntityClientPlayerMP,
 * EntityPlayerSP, EntityPlayer, EntityLivingBase, Entity,
 * MovementInputFromOptions and NetHandlerPlayServer. See player.h for the
 * boundary and the report for the assumptions. */
#include "player.h"
#include "env.h"
#include "riding.h"
#include "blocks.h"
#include "trace.h"
#include "collide.h"
#include "jmath.h"
#include "smath.h"
#include "nbtjson.h"
#include "tape.h"
#include "combat.h"
#include "pickobj.h"
#include "serverreplay.h"
#include "chunkload.h"
#include "sleep.h"
#include "survival.h"
#include "potion.h"
#include "particles_live.h"
#include "chunkscan.h"

#include <math.h>
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#if defined(__x86_64__) && defined(__GNUC__)
#include <immintrin.h>   /* the fog loop's AVX-512 form (cp_fog_lanes) */
#endif

#define MAT_WATER 6
#define MAT_LAVA 7

#define BLOCK_LADDER 65
#define BLOCK_VINE 106

/* ------------------------------------------------------------- key bindings */

static const char *KEY_DESCS[K_N] = {
    "key.attack", "key.use", "key.forward", "key.left", "key.back", "key.right",
    "key.jump", "key.sneak", "key.sprint", "key.drop", "key.inventory", "key.chat",
    "key.playerList", "key.pickItem", "key.command", "key.togglePerspective",
    "key.hotbar.1", "key.hotbar.2", "key.hotbar.3", "key.hotbar.4", "key.hotbar.5",
    "key.hotbar.6", "key.hotbar.7", "key.hotbar.8", "key.hotbar.9",
};

const char *key_desc(int k)
{
    return KEY_DESCS[k];
}

int key_lookup(const char *desc)
{
    for (int i = 0; i < K_N; ++i)
        if (!strcmp(KEY_DESCS[i], desc)) return i;
    return -1;
}

/* ------------------------------------------------------------------- pieces */

/* MathHelper.sqrt_float: (float)Math.sqrt((double)f). */
static float sqrt_float(float f)
{
    return (float)sqrt((double)f);
}

/* Entity.setRotation: yaw % 360, pitch % 360, Java's float remainder. */
void entity_set_rotation(float *yaw, float *pitch, float y, float p)
{
    *yaw = fmodf(y, 360.0F);
    *pitch = fmodf(p, 360.0F);
}

/* Entity.setPositionAndRotation over a player's rotation fields. */
void entity_set_pos_rot(struct entity *e, double x, double y, double z,
                               float *yaw, float *pitch, float *prev_yaw, float *prev_pitch,
                               float ny, float np)
{
    e->pos_x = e->pos_x; /* prevPos rides on the players' own fields below */
    e->pos_x = x;
    e->pos_y = y;
    e->pos_z = z;
    /* setPositionAndRotation sets prevPos = pos (Entity.java:1270-1272);
     * prevRot = rot below. The S08's C06 reads bounding_box.minY from the
     * fresh position, and the client player's next living tick's move code
     * compares against the prev the S08 left. */
    e->prev_pos_x = x;
    e->prev_pos_y = y;
    e->prev_pos_z = z;
    double var9 = (double)*prev_yaw - (double)ny;

    if (var9 < -180.0) *prev_yaw += 360.0F;
    if (var9 >= 180.0) *prev_yaw -= 360.0F;

    e->y_size = 0.0F;
    entity_set_position(e, x, y, z);
    *prev_yaw = *yaw = ny;
    *prev_pitch = *pitch = np;
    entity_set_rotation(yaw, pitch, ny, np);
}

/* World.getBlock(...).isNormalCube(), the pushOutOfBlocks test. */
static int is_normal_cube(struct world *w, int x, int y, int z)
{
    return BLOCKS[world_get_block(w, x, y, z) & 4095].normal_cube;
}

/* World.isAnyLiquid. */
static int world_is_any_liquid(struct world *w, struct aabb b)
{
    int x0 = mh_floor(b.min_x);
    int x1 = mh_floor(b.max_x + 1.0);
    int y0 = mh_floor(b.min_y);
    int y1 = mh_floor(b.max_y + 1.0);
    int z0 = mh_floor(b.min_z);
    int z1 = mh_floor(b.max_z + 1.0);

    if (b.min_x < 0.0) --x0;
    if (b.min_y < 0.0) --y0;
    if (b.min_z < 0.0) --z0;

    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
                if (MATERIALS[BLOCKS[world_get_block(w, x, y, z) & 4095].material].is_liquid) return 1;

    return 0;
}

/* World.isMaterialInBB. */
static int world_is_material_in_bb(struct world *w, struct aabb b, int material)
{
    int x0 = mh_floor(b.min_x);
    int x1 = mh_floor(b.max_x + 1.0);
    int y0 = mh_floor(b.min_y);
    int y1 = mh_floor(b.max_y + 1.0);
    int z0 = mh_floor(b.min_z);
    int z1 = mh_floor(b.max_z + 1.0);

    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
                if (BLOCKS[world_get_block(w, x, y, z) & 4095].material == material) return 1;

    return 0;
}

/* World.checkBlockCollision: a block whose material blocksMovement in the box. */
static int world_check_block_collision(struct world *w, struct aabb b)
{
    int x0 = mh_floor(b.min_x);
    int x1 = mh_floor(b.max_x + 1.0);
    int y0 = mh_floor(b.min_y);
    int y1 = mh_floor(b.max_y + 1.0);
    int z0 = mh_floor(b.min_z);
    int z1 = mh_floor(b.max_z + 1.0);

    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
                if (MATERIALS[BLOCKS[world_get_block(w, x, y, z) & 4095].material].blocks_movement) return 1;

    return 0;
}

/* BlockLiquid.func_149801_b: the liquid height fraction of a metadata. */
static float liquid_height_percent(int meta)
{
    if (meta >= 8) meta = 0;
    return (float)(meta + 1) / 9.0F;
}

/* BlockLiquid.func_149798_e: the flow weight of one cell (-1 when not this
 * liquid; falling columns count as their source). */
static int liquid_weight(struct world *w, int x, int y, int z)
{
    if (BLOCKS[world_get_block(w, x, y, z) & 4095].material != MAT_WATER) return -1;

    int meta = world_get_meta(w, x, y, z);
    if (meta >= 8) meta = 0;
    return meta;
}

/* BlockLiquid.func_149800_f: the flow direction of one water cell, into out. */
static void liquid_flow_vector(struct world *w, int x, int y, int z, double *ox, double *oy, double *oz)
{
    int self_meta = world_get_meta(w, x, y, z);
    int var6 = self_meta >= 8 ? 0 : self_meta;
    double fx = 0.0, fy = 0.0, fz = 0.0;

    static const int off[4][2] = { {-1, 0}, {0, -1}, {1, 0}, {0, 1} };

    for (int d = 0; d < 4; ++d)
    {
        int nx = x + off[d][0];
        int nz = z + off[d][1];
        int var11 = liquid_weight(w, nx, y, nz);

        if (var11 < 0)
        {
            int nid = world_get_block(w, nx, y, nz) & 4095;

            if (!MATERIALS[BLOCKS[nid].material].blocks_movement)
            {
                var11 = liquid_weight(w, nx, y - 1, nz);

                if (var11 >= 0)
                {
                    int var12 = var11 - (var6 - 8);
                    fx += (double)((nx - x) * var12);
                    fy += (double)((y - y) * var12);
                    fz += (double)((nz - z) * var12);
                }
            }
        }
        else if (var11 >= 0)
        {
            int var12 = var11 - var6;
            fx += (double)((nx - x) * var12);
            fy += (double)((y - y) * var12);
            fz += (double)((nz - z) * var12);
        }
    }

    if (self_meta >= 8)
    {
        /* the eight solid-neighbor check; BlockLiquid.isBlockSolid is
         * material == water ? false : side == 1 ? true : material == ice ?
         * false : material.isSolid() */
        int solid = 0;
        int xs[8] = { x, x, x - 1, x + 1, x, x, x - 1, x + 1 };
        int ys[8] = { y, y, y, y, y + 1, y + 1, y + 1, y + 1 };
        int zs[8] = { z - 1, z + 1, z, z, z - 1, z + 1, z, z };

        for (int i = 0; i < 8 && !solid; ++i)
        {
            int nid = world_get_block(w, xs[i], ys[i], zs[i]) & 4095;
            const struct material_def *m = &MATERIALS[BLOCKS[nid].material];

            if (m == &MATERIALS[MAT_WATER]) continue;
            if (i < 4 && !strcmp(m->name, "ice")) continue;
            if (m->is_solid) solid = 1;
        }

        if (solid)
        {
            /* Vec3.normalize: MathHelper.sqrt_double is a float */
            double len = (double)(float)sqrt(fx * fx + fy * fy + fz * fz);

            if (len >= 1.0E-4) { fx /= len; fy /= len; fz /= len; }
            else { fx = 0.0; fy = 0.0; fz = 0.0; }

            fy += -6.0;
        }
    }

    double len = (double)(float)sqrt(fx * fx + fy * fy + fz * fz);

    if (len >= 1.0E-4) { fx /= len; fy /= len; fz /= len; }
    else { fx = 0.0; fy = 0.0; fz = 0.0; }

    *ox = fx;
    *oy = fy;
    *oz = fz;
}

/* World.handleMaterialAcceleration: the liquid motion added to the entity and
 * whether any water cell counts. */
static int handle_material_acceleration(struct world *w, struct aabb box, double *mx, double *my, double *mz)
{
    int x0 = mh_floor(box.min_x);
    int x1 = mh_floor(box.max_x + 1.0);
    int y0 = mh_floor(box.min_y);
    int y1 = mh_floor(box.max_y + 1.0);
    int z0 = mh_floor(box.min_z);
    int z1 = mh_floor(box.max_z + 1.0);

    if (!world_check_chunks_exist(w, x0, y0, z0, x1, y1, z1)) return 0;

    int found = 0;
    double vx = 0.0, vy = 0.0, vz = 0.0;
    /* the cells' answer (found and the flows' sum) is the world's water memo
     * while the chunks they and their neighbours lie in are unwritten since */
    int k[6] = {x0, x1, y0, y1, z0, z1};
    double v[3];
    int hit = water_memo_get(w, &w->wmemo, k, &found, v);

#ifdef NETHERITE_MOVE_MEMO_CHECK
    int verify = hit, vfound = found;
    double vv[3] = {v[0], v[1], v[2]};
    hit = 0;
    found = 0;
#endif
    if (hit)
    {
        vx = v[0];
        vy = v[1];
        vz = v[2];
    }
    else
    {
        for (int x = x0; x < x1; ++x)
            for (int y = y0; y < y1; ++y)
                for (int z = z0; z < z1; ++z)
                {
                    int id = world_get_block(w, x, y, z) & 4095;

                    if (BLOCKS[id].material != MAT_WATER) continue;

                    double level = (double)((float)(y + 1) - liquid_height_percent(world_get_meta(w, x, y, z)));

                    if ((double)y1 >= level)
                    {
                        double fx, fy, fz;
                        found = 1;
                        liquid_flow_vector(w, x, y, z, &fx, &fy, &fz);
                        vx += fx;
                        vy += fy;
                        vz += fz;
                    }
                }
        v[0] = vx;
        v[1] = vy;
        v[2] = vz;
#ifdef NETHERITE_MOVE_MEMO_CHECK
        if (verify && (found != vfound || memcmp(v, vv, sizeof v)))
        {
            fprintf(stderr, "water memo: the players' water test differs\n");
            abort();
        }
#endif
        water_memo_put(w, &w->wmemo, k, found, v);
    }

    /* Vec3.lengthVector (a double sqrt) gates, Vec3.normalize (a float
     * sqrt, zero under 1.0E-4) scales */
    if (sqrt(vx * vx + vy * vy + vz * vz) > 0.0)
    {
        double len = (double)(float)sqrt(vx * vx + vy * vy + vz * vz);
        if (len < 1.0E-4) { vx = 0.0; vy = 0.0; vz = 0.0; }
        else { vx /= len; vy /= len; vz /= len; }
        *mx += vx * 0.014;
        *my += vy * 0.014;
        *mz += vz * 0.014;
    }

    return found;
}

/* Entity.handleWaterMovement for a player: the splash draws from the client
 * Random and writes particles, neither of which the rows read. */
int handle_water_movement(struct world *w, struct aabb bb, double *mx, double *my, double *mz,
                                 uint8_t *in_water, float *fall_distance)
{
    struct aabb box = aabb_expand(aabb_expand(bb, 0.0, -0.4000000059604645, 0.0), -0.001, -0.001, -0.001);
    int hit = handle_material_acceleration(w, box, mx, my, mz);

    if (hit)
    {
        *fall_distance = 0.0F;
        *in_water = 1;
    }
    else
    {
        *in_water = 0;
    }

    return *in_water;
}

/* Entity.handleWaterMovement for the server player: a water entry after
 * the first update plays the splash (EntityPlayer.playSound's pitch, sent to
 * no other player) and spawns the bubbles and splashes (nothing on the
 * server's WorldManager), every draw on the player's own Random. */
int sp_handle_water_movement(struct entity *e, det_rng *rand)
{
    int was = e->in_water;
    double mx = e->motion_x, my = e->motion_y, mz = e->motion_z;
    int hit = handle_water_movement(e->world, e->bounding_box, &mx, &my, &mz,
                                    &e->in_water, &e->fall_distance);
    e->motion_x = (neth_real)mx;
    e->motion_y = (neth_real)my;
    e->motion_z = (neth_real)mz;

    if (hit && !was && !e->first_update)
    {
        (void)det_rng_float(rand);   /* the pitch, 1.0F + (nextFloat() - nextFloat()) * 0.4F */
        (void)det_rng_float(rand);
        for (int i = 0; (float)i < 1.0F + e->width * 20.0F; ++i)
        {
            (void)det_rng_float(rand);   /* bubble: x, z, then motionY's */
            (void)det_rng_float(rand);
            (void)det_rng_float(rand);
        }
        for (int i = 0; (float)i < 1.0F + e->width * 20.0F; ++i)
        {
            (void)det_rng_float(rand);   /* splash: x, z */
            (void)det_rng_float(rand);
        }
    }
    return hit;
}

/* EntityLivingBase.isOnLadder. */
static int is_on_ladder(struct world *w, double pos_x, double pos_z, struct aabb bb)
{
    int id = world_get_block(w, mh_floor(pos_x), mh_floor(bb.min_y), mh_floor(pos_z)) & 4095;
    return id == BLOCK_LADDER || id == BLOCK_VINE;
}

/* Entity.moveFlying. */
static void move_flying(double yaw, neth_real *motion_x, neth_real *motion_z, float strafe, float forward, float friction)
{
    float var4 = strafe * strafe + forward * forward;

    if (var4 >= 1.0E-4F)
    {
        var4 = sqrt_float(var4);

        if (var4 < 1.0F) var4 = 1.0F;

        var4 = friction / var4;
        strafe *= var4;
        forward *= var4;
        // Java: this.rotationYaw * (float)Math.PI / 180.0F, float at every step
        float var5 = mh_sin((float)yaw * (float)M_PI / 180.0F);
        float var6 = mh_cos((float)yaw * (float)M_PI / 180.0F);
        *motion_x = (neth_real)((double)*motion_x + (double)(strafe * var6 - forward * var5));
        *motion_z = (neth_real)((double)*motion_z + (double)(forward * var6 + strafe * var5));

    }
}

/* Entity.isOffsetPositionInLiquid. */
static int is_offset_position_in_liquid(struct world *w, struct aabb bb, double dx, double dy, double dz,
                                        struct collide_list *scratch)
{
    struct aabb box = aabb_offset_box(bb, dx, dy, dz);

    collide_list_clear(scratch);
    world_get_colliding_bounding_boxes(w, box, scratch);
    if (scratch->n != 0) return 0;
    return !world_is_any_liquid(w, box);
}

/* ------------------------------------------------------- load from snapshot */

/* The player files are canonical NBT: a scalar parses into a typed node, so
 * each getter renders it back to its "t:value" text and parses that. */

static const char *scalar_text(const void *comp, const char *key, char *buf, size_t n)
{
    const nbt *v = nbt_get((const nbt *)comp, key);

    if (!v) return NULL;

    char *text = nbt_render(v);
    snprintf(buf, n, "%s", text);
    free(text);
    /* the canonical text of a scalar keeps its quotes */
    size_t len = strlen(buf);
    if (len >= 2 && buf[0] == '"' && buf[len - 1] == '"') { buf[len - 1] = 0; return buf + 1; }
    return buf;
}

static int get_f(const void *comp, const char *key, float *out)
{
    char buf[64];
    const char *s = scalar_text(comp, key, buf, sizeof buf);

    if (!s || s[0] != 'f' || s[1] != ':') return 0;

    uint32_t bits = (uint32_t)strtoul(s + 2, NULL, 16);
    memcpy(out, &bits, 4);
    return 1;
}

static int get_d(const void *comp, const char *key, double *out)
{
    char buf[64];
    const char *s = scalar_text(comp, key, buf, sizeof buf);

    if (!s || s[0] != 'd' || s[1] != ':') return 0;

    uint64_t bits = strtoull(s + 2, NULL, 16);
    memcpy(out, &bits, 8);
    return 1;
}

static int get_r(const void *comp, const char *key, neth_real *out)
{
    double v;
    if (!get_d(comp, key, &v)) return 0;
    *out = (neth_real)v;
    return 1;
}

static int get_i(const void *comp, const char *key, int *out)
{
    char buf[64];
    const char *s = scalar_text(comp, key, buf, sizeof buf);

    if (!s || s[1] != ':') return 0;

    *out = (int)strtol(s + 2, NULL, 10);
    return 1;
}

/* "bb:" then six raw doubles (EntityState's AxisAlignedBB, a canonical
 * "str:" string): Entity.boundingBox as the moves left it, which a
 * setPosition from the position does not reproduce (a collision derives
 * the position back from the box). 0 when absent. */
static int get_bb(const nbt *comp, const char *key, struct aabb *out)
{
    const nbt *v = comp ? nbt_get(comp, key) : NULL;
    const char *s = v ? nbt_string_value(v) : NULL;
    if (s == NULL || strncmp(s, "bb:", 3) != 0) return 0;
    double d[6];
    const char *p = s + 3;
    for (int i = 0; i < 6; ++i)
    {
        char *end;
        uint64_t b = strtoull(p, &end, 16);
        if (end != p + 16) return 0;
        memcpy(&d[i], &b, 8);
        p = end;
        if (i < 5 && *p++ != ',') return 0;
    }
    *out = aabb_make(d[0], d[1], d[2], d[3], d[4], d[5]);
    return 1;
}

/* BlockPortal.onEntityCollidedWithBlock on the client player's own move:
 * not while riding */
static void cp_set_in_portal(void *self)
{
    struct client_player *cp = self;

    if (cp->riding_id != 0) return;
    clientstate_set_in_portal(&cp->portal);
}

double player_move_speed(int sprint, int slow, int speed)
{
    /* HashSet order of the three modifiers' UUIDs (hashCode spread over 16
     * buckets): slowness 7107DE5E (5), the sprint boost 662A6B8D (6), speed
     * 91AEAA56 (13); each multiplies by 1 + amount, the potions' amount
     * Potion.func_111183_a's modifier times amplifier + 1 */
    double r = 0.10000000149011612;
    if (slow >= 0) r *= 1.0 + -0.15000000596046448 * (double)(slow + 1);
    if (sprint && !neth_env->cfg.player_no_sprint_boost) r *= 1.0 + 0.30000001192092896;
    if (speed >= 0) r *= 1.0 + 0.20000000298023224 * (double)(speed + 1);
    /* RangedAttribute's clamp: movementSpeed's minimum is 0 */
    return r < 0.0 ? 0.0 : r;
}

/* A potion's movementSpeed modifier in an NBT tag's Attributes (the saved
 * set readEntityFromNBT applies): its amplifier, -1 when absent. */
static int nbt_speed_mod_amp(const nbt *tag, uint64_t msb, uint64_t lsb, double per)
{
    const nbt *attrs = nbt_get(tag, "Attributes");
    for (int i = 0; attrs && i < nbt_list_size(attrs); ++i)
    {
        const nbt *a = nbt_list_get(attrs, i);
        const char *name = nbt_string_value(nbt_get(a, "Name"));
        if (attrs_index_by_name(name) != ATTR_MOVEMENT_SPEED) continue;
        const nbt *mods = nbt_get(a, "Modifiers");
        for (int k = 0; mods && k < nbt_list_size(mods); ++k)
        {
            const nbt *m = nbt_list_get(mods, k);
            if ((uint64_t)nbt_int_value(nbt_get(m, "UUIDMost")) != msb ||
                (uint64_t)nbt_int_value(nbt_get(m, "UUIDLeast")) != lsb)
                continue;
            uint64_t bits = nbt_double_bits(nbt_get(m, "Amount"));
            double amount;
            memcpy(&amount, &bits, 8);
            return (int)lround(amount / per) - 1;
        }
    }
    return -1;
}

int client_player_load(struct client_player *p, const void *tree, struct world *w)
{
    const nbt *t = (const nbt *)tree;
    memset(p, 0, sizeof *p);
    p->e.world = w;

    const nbt *f = nbt_get(t, "fields");
    const nbt *in = nbt_get(t, "input");
    const nbt *ab = nbt_get(t, "abilities");
    const nbt *tag = nbt_get(t, "nbt");

    if (!f || !in || !ab || !tag) { fprintf(stderr, "cp load: missing %s %s %s %s\n", !!f?"1":"f", !!in?"1":"in", !!ab?"1":"ab", !!tag?"1":"tag"); return 0; }

    int ok = 1;
    ok &= get_f(f, "prevRotationYaw", &p->prev_rotation_yaw);
    ok &= get_f(f, "prevRotationPitch", &p->prev_rotation_pitch);
    ok &= get_f(f, "rotationYaw", &p->rotation_yaw);
    ok &= get_f(f, "rotationPitch", &p->rotation_pitch);
    ok &= get_r(f, "prevPosX", &p->e.prev_pos_x);
    ok &= get_r(f, "prevPosY", &p->e.prev_pos_y);
    ok &= get_r(f, "prevPosZ", &p->e.prev_pos_z);
    ok &= get_r(f, "posX", &p->e.pos_x);
    ok &= get_r(f, "posY", &p->e.pos_y);
    ok &= get_r(f, "posZ", &p->e.pos_z);
    ok &= get_r(f, "motionX", &p->e.motion_x);
    ok &= get_r(f, "motionY", &p->e.motion_y);
    ok &= get_r(f, "motionZ", &p->e.motion_z);
    {
        int og = 0;
        ok &= get_i(f, "onGround", &og);
        p->e.on_ground = (uint8_t)(og != 0);
    }
    ok &= get_f(f, "fallDistance", &p->e.fall_distance);
    ok &= get_f(f, "ySize", &p->e.y_size);
    ok &= get_f(f, "stepHeight", &p->e.step_height);
    ok &= get_d(f, "oldPosX", &p->old_pos_x);
    ok &= get_d(f, "oldMinY", &p->old_min_y);
    ok &= get_d(f, "oldPosY", &p->old_pos_y);
    ok &= get_d(f, "oldPosZ", &p->old_pos_z);
    ok &= get_f(f, "oldRotationYaw", &p->old_rotation_yaw);
    ok &= get_f(f, "oldRotationPitch", &p->old_rotation_pitch);
    ok &= get_i(f, "wasOnGround", &p->was_on_ground);
    ok &= get_i(f, "shouldStopSneaking", &p->should_stop_sneaking);
    ok &= get_i(f, "wasSneaking", &p->was_sneaking);
    ok &= get_i(f, "ticksSinceMovePacket", &p->ticks_since_move_packet);
    ok &= get_i(f, "sprintToggleTimer", &p->sprint_toggle_timer);
    ok &= get_i(f, "sprintingTicksLeft", &p->sprinting_ticks_left);
    ok &= get_i(f, "flyToggleTimer", &p->fly_toggle_timer);

    ok &= get_f(in, "moveStrafe", &p->in_strafe);
    ok &= get_f(in, "moveForward", &p->in_forward);
    ok &= get_i(in, "jump", &p->in_jump);
    ok &= get_i(in, "sneak", &p->in_sneak);

    ok &= get_i(ab, "allowFlying", &p->allow_flying);
    ok &= get_i(ab, "isFlying", &p->is_flying);

    int food = 20;
    ok &= get_i(tag, "foodLevel", &food);
    p->food_level = (float)food;
    if (!ok) fprintf(stderr, "cp load: some field missing\n");

    /* Entity's constructor fields the snapshot does not record. */
    p->e.width = 0.6F;
    p->e.height = 1.8F;
    p->e.y_offset = 1.62F;
    /* EntityPlayer's constructor: a player's fireResistance is 20, which is
     * the Fire field's -20 once Entity.moveEntity writes it cold */
    p->e.fire_resistance = 20;
    p->e.next_step_distance = 1;
    p->e.field_70135_K = 1;
    p->in_game_has_focus = 1;
    /* a player triggers walking: moveEntity's steps reach
     * Block.onEntityWalking (the redstone ore on the client world), from the
     * step counters the snapshot recorded */
    p->e.can_trigger_walking = 1;
    (void)get_i(f, "nextStepDistance", &p->e.next_step_distance);
    (void)get_f(f, "distanceWalkedOnStepModified", &p->e.distance_walked_on_step_modified);

    /* setSprinting is not in NBT: sprintingTicksLeft is 600 exactly while
     * sprinting (EntityPlayerSP.setSprinting) and these snapshots never fall
     * mid-decay. */
    p->sprinting = p->sprinting_ticks_left != 0;

    entity_set_position(&p->e, p->e.pos_x, p->e.pos_y, p->e.pos_z);

    /* the movementSpeed attribute: base 0.10000000149011612 with the sprint
     * boost modifier (0.30000001192092896, operation 2) on sprinting, and the
     * potions' modifiers an S20 left (saved, so in the client's NBT) */
    p->mod_slow = nbt_speed_mod_amp(tag, 0x7107de5e7ce84030ULL, 0x940e514c1f160890ULL, -0.15000000596046448);
    p->mod_speed = nbt_speed_mod_amp(tag, 0x91aeaa56376b4498ULL, 0x935b2f7f68070635ULL, 0.20000000298023224);
    p->mod_sprint = p->sprinting;
    p->move_speed = player_move_speed(p->mod_sprint, p->mod_slow, p->mod_speed);

    /* EntityLivingBase's field initializer; EntityPlayer.onLivingUpdate only
     * refreshes it after the same tick's travel, so the first tick moves with
     * the value the previous tick left */
    p->jump_movement_factor = p->sprinting ? 0.026F : 0.02F;

    /* a mid-run snapshot's runtime (EntityState.java): the sprint flag
     * itself (data watcher 0, bit 3), the movementSpeed value (an S20 can
     * strip the unsaved sprint modifier while the flag stays on) and the
     * jump counters the previous tick left */
    const nbt *rt = nbt_get(t, "rt");
    if (rt)
    {
        const nbt *rf = nbt_get(rt, "f");
        int flags = 0;
        if (get_i(nbt_get(rt, "dw"), "0", &flags)) p->sprinting = (flags & 8) != 0;
        (void)get_d(nbt_get(rt, "attrs"), "generic.movementSpeed", &p->move_speed);
        /* the sprint boost is unsaved: whether the value carries it */
        p->mod_sprint = p->move_speed == player_move_speed(1, p->mod_slow, p->mod_speed);
        (void)get_f(rf, "jumpMovementFactor", &p->jump_movement_factor);
        (void)get_i(rf, "jumpTicks", &p->jump_ticks);
        (void)get_i(rf, "isJumping", &p->is_jumping);
        (void)get_bb(rf, "boundingBox", &p->e.bounding_box);
        /* the render-only state RenderPlayer and the preview read */
        (void)get_i(rf, "ticksExisted", &p->ticks_existed);
        (void)get_f(rf, "renderYawOffset", &p->render_yaw_offset);
        (void)get_f(rf, "prevRenderYawOffset", &p->prev_render_yaw_offset);
        (void)get_f(rf, "rotationYawHead", &p->yaw_head);
        (void)get_f(rf, "prevRotationYawHead", &p->prev_yaw_head);
        (void)get_f(rf, "limbSwing", &p->limb_swing);
        (void)get_f(rf, "limbSwingAmount", &p->limb_swing_amount);
        (void)get_f(rf, "prevLimbSwingAmount", &p->prev_limb_swing_amount);
        /* the last move's collision flags: EntityPlayerSP.onLivingUpdate
         * stops a sprint on isCollidedHorizontally before this tick moves */
        {
            int v = 0;
            if (get_i(rf, "isCollidedHorizontally", &v)) p->e.is_collided_horizontally = (uint8_t)(v != 0);
            if (get_i(rf, "isCollidedVertically", &v)) p->e.is_collided_vertically = (uint8_t)(v != 0);
            if (get_i(rf, "isCollided", &v)) p->e.is_collided = (uint8_t)(v != 0);
        }
        /* Entity.ridingEntity: the client copy of the vehicle */
        const nbt *rv = rf ? nbt_get(rf, "ridingEntity") : NULL;
        const char *rs = rv ? nbt_string_value(rv) : NULL;
        if (rs && !strncmp(rs, "e:", 2) && strtol(rs + 2, NULL, 10) > 0) p->riding_id = (int)strtol(rs + 2, NULL, 10);
    }

    return ok;
}

int client_player_added_to_chunk(const void *tree)
{
    const nbt *rf = nbt_get(nbt_get((const nbt *)tree, "rt"), "f");
    int v = 0;
    if (!rf || !get_i(rf, "addedToChunk", &v)) return -1;
    return v != 0;
}

int server_player_load(struct server_player *p, const void *tree, struct world *w)
{
    const nbt *t = (const nbt *)tree;
    memset(p, 0, sizeof *p);
    p->e.world = w;

    const nbt *f = nbt_get(t, "fields");

    if (!f) return 0;

    int ok = 1;
    ok &= get_r(f, "prevPosX", &p->e.prev_pos_x);
    ok &= get_r(f, "prevPosY", &p->e.prev_pos_y);
    ok &= get_r(f, "prevPosZ", &p->e.prev_pos_z);
    ok &= get_r(f, "posX", &p->e.pos_x);
    ok &= get_r(f, "posY", &p->e.pos_y);
    ok &= get_r(f, "posZ", &p->e.pos_z);
    ok &= get_r(f, "motionX", &p->e.motion_x);
    ok &= get_r(f, "motionY", &p->e.motion_y);
    ok &= get_r(f, "motionZ", &p->e.motion_z);
    ok &= get_f(f, "rotationYaw", &p->rotation_yaw);
    ok &= get_f(f, "rotationPitch", &p->rotation_pitch);
    {
        int og = 0;
        ok &= get_i(f, "onGround", &og);
        p->e.on_ground = (uint8_t)(og != 0);
    }
    ok &= get_f(f, "fallDistance", &p->e.fall_distance);
    ok &= get_f(f, "ySize", &p->e.y_size);

    /* the tree the entity digest writes EntityPlayerMP's NBT from: its health,
     * air, hurt timers and food stats are surv_server_load's, and ent_nbt_player
     * reads them from surv_state. The "nbt" compound is what the digest's
     * constant parts (the attributes, the abilities, the UUIDs) come out of. */
    if (nbt_get(t, "nbt") == NULL) return 0;

    p->snapshot_tree = tree;
    p->dimension = (int)nbt_int_value(nbt_get(nbt_get(t, "nbt"), "Dimension"));
    portal_entity_init(&p->portal, p->dimension, 80);
    p->portal.time_until_portal = (int)nbt_int_value(nbt_get(nbt_get(t, "nbt"), "PortalCooldown"));

    /* the EntityPlayerMP constructor: stepHeight 0, yOffset 0 */
    p->e.width = 0.6F;
    p->e.height = 1.8F;
    p->e.y_offset = 0.0F;
    p->e.step_height = 0.0F;
    /* EntityPlayer's constructor, see client_player_load */
    p->e.fire_resistance = 20;
    p->e.next_step_distance = 1;
    p->e.field_70135_K = 1;
    /* Entity.canTriggerWalking is true for a player: moveEntity's step
     * reaches Block.onEntityWalking (the redstone ore). The step counters
     * the player walked up before the snapshot, when it records them */
    p->e.can_trigger_walking = 1;
    (void)get_i(f, "nextStepDistance", &p->e.next_step_distance);
    (void)get_f(f, "distanceWalkedOnStepModified", &p->e.distance_walked_on_step_modified);

    /* NetHandlerPlayServer bookkeeping: hasMoved is the handler's runtime
     * state. A dev tp in the recording's pre-snapshot ops leaves the handler
     * hasMoved-false while the snapshot's player fields still hold the pre-tp
     * position (Snapshot.dump runs on the Oracle Snapshot thread before the
     * Dev entry lands): setPlayerLocation set lastPos to the tp target, which
     * is also where setPositionAndRotation left the player, so lastPos is
     * simply the loaded position in every case. floatingTickCount phases the
     * float-kick counter. Absent (old snapshots): the join defaults. */
    const nbt *net = nbt_get(t, "net");
    int has_moved = 1, floating = 0, ntc = -1;

    if (net)
    {
        (void)get_i(net, "hasMoved", &has_moved);
        (void)get_i(net, "floatingTickCount", &floating);
        (void)get_i(net, "networkTickCount", &ntc);
    }

    p->has_moved = has_moved != 0;
    p->floating_tick_count = floating;
    p->network_tick_count = ntc;
    /* previousEquipment starts empty; the attribute map has applied nothing */
    p->held_attr_item = -1;
    p->last_pos_x = p->e.pos_x;
    p->last_pos_y = p->e.pos_y;
    p->last_pos_z = p->e.pos_z;

    /* the handler's own counter ("net" compound, Snapshot.serverPlayer): its
     * % 20 keepalive correction runs while hasMoved is false */
    {
        const nbt *net = nbt_get(t, "net");
        int ntc = 0;
        if (net) get_i(net, "networkTickCount", &ntc);
        p->network_tick_count = ntc;
    }

    entity_set_position(&p->e, p->e.pos_x, p->e.pos_y, p->e.pos_z);

    /* the potions' saved movementSpeed modifiers (the NBT's Attributes) */
    p->pot_slow = nbt_speed_mod_amp(nbt_get(t, "nbt"), 0x7107de5e7ce84030ULL, 0x940e514c1f160890ULL, -0.15000000596046448);
    p->pot_speed = nbt_speed_mod_amp(nbt_get(t, "nbt"), 0x91aeaa56376b4498ULL, 0x935b2f7f68070635ULL, 0.20000000298023224);
    p->move_speed = player_move_speed(0, p->pot_slow, p->pot_speed);
    p->jump_movement_factor = 0.02F;

    /* a mid-run snapshot's runtime (EntityState.java): the sprint and sneak
     * flags (data watcher 0, bits 3 and 1), the movementSpeed value, the
     * jump counters, and the handler's lastPos (NetHandlerPlayServer: a tp
     * moves it without moving the player's last accepted position) */
    const nbt *rt = nbt_get(t, "rt");
    if (rt)
    {
        const nbt *rf = nbt_get(rt, "f");
        int flags = 0;
        if (get_i(nbt_get(rt, "dw"), "0", &flags))
        {
            p->sprinting = (flags & 8) != 0;
            p->sneaking = (flags & 2) != 0;
        }
        (void)get_d(nbt_get(rt, "attrs"), "generic.movementSpeed", &p->move_speed);
        (void)get_f(rf, "jumpMovementFactor", &p->jump_movement_factor);
        (void)get_i(rf, "jumpTicks", &p->jump_ticks);
        (void)get_i(rf, "isJumping", &p->is_jumping);
        (void)get_bb(rf, "boundingBox", &p->e.bounding_box);
        {
            int v = 0;
            if (get_i(rf, "isCollidedHorizontally", &v)) p->e.is_collided_horizontally = (uint8_t)(v != 0);
            if (get_i(rf, "isCollidedVertically", &v)) p->e.is_collided_vertically = (uint8_t)(v != 0);
            if (get_i(rf, "isCollided", &v)) p->e.is_collided = (uint8_t)(v != 0);
        }
        /* Entity.inWater: handleWaterMovement's splash is for an entry, so a
         * player saved in water (a chained session's start) is in it already */
        {
            int v = 0;
            if (get_i(rf, "inWater", &v)) p->e.in_water = (uint8_t)(v != 0);
        }
        /* EntityPlayerMP.currentWindowId: the next window's id is it % 100 + 1 */
        (void)get_i(rf, "currentWindowId", &p->window_id);
        /* Entity's portal state: standing in a portal counts toward the
         * trip (portalCounter against getMaxInPortalTime) */
        (void)get_i(rf, "timeUntilPortal", &p->portal.time_until_portal);
        (void)get_i(rf, "portalCounter", &p->portal.portal_counter);
        (void)get_i(rf, "inPortal", &p->portal.in_portal);
        (void)get_i(rf, "teleportDirection", &p->portal.teleport_direction);
    }
    const nbt *nrt = nbt_get(t, "netrt");
    if (nrt)
    {
        (void)get_d(nrt, "lastPosX", &p->last_pos_x);
        (void)get_d(nrt, "lastPosY", &p->last_pos_y);
        (void)get_d(nrt, "lastPosZ", &p->last_pos_z);
    }

    return ok;
}

/* ------------------------------------------------------------ shared physics */

/* EntityLivingBase.setSprinting, the attribute half. */
static void cp_set_sprinting(struct client_player *p, int v)
{
    p->sprinting = v;
    p->sprinting_ticks_left = v ? 600 : 0;
    p->mod_sprint = v;
    p->move_speed = player_move_speed(p->mod_sprint, p->mod_slow, p->mod_speed);
}

void player_client_set_sprinting(struct client_player *p, int v)
{
    cp_set_sprinting(p, v);
}

/* The potions' movementSpeed modifiers on the server player (its twin
 * holds the effects): a change re-derives the attribute, keeping the sprint
 * modifier the last setSprinting left */
static void sp_sync_speed(struct server_player *p)
{
    const struct living *tw = p->replay && p->replay->player_livh ? lv_get(p->replay->player_livh) : NULL;
    if (tw == NULL) return;
    const struct potion_effect *ps = living_get_potion_effect(tw, POT_MOVE_SLOWDOWN);
    const struct potion_effect *pf = living_get_potion_effect(tw, POT_MOVE_SPEED);
    int slow = ps ? ps->amplifier : -1, speed = pf ? pf->amplifier : -1;
    if (slow == p->pot_slow && speed == p->pot_speed) return;
    int sprint = p->move_speed == player_move_speed(1, p->pot_slow, p->pot_speed);
    p->pot_slow = slow;
    p->pot_speed = speed;
    p->move_speed = player_move_speed(sprint, slow, speed);
}

static void sp_set_sprinting(struct server_player *p, int v)
{
    /* EntityLivingBase.setSprinting: the modifier is removed when present,
     * applied when sprinting; either marks the attribute (the S20) and a
     * stop while not sprinting touches nothing */
    if (v || p->sprinting) p->attr_dirty = 1;
    p->sprinting = v;
    p->move_speed = player_move_speed(p->sprinting, p->pot_slow, p->pot_speed);
}

/* EntityPlayerSP.isSneaking: the movement input's flag. */
static int cp_is_sneaking(const struct client_player *p)
{
    return p->in_sneak && !p->sv.sleeping;
}

/* EntityPlayerSP.func_145771_j, pushOutOfBlocks. */
static void push_out_of_blocks(struct client_player *p, double px, double py, double pz)
{
    struct world *w = p->e.world;
    int var7 = mh_floor(px);
    int var8 = mh_floor(py);
    int var9 = mh_floor(pz);
    double var10 = px - (double)var7;
    double var12 = pz - (double)var9;

    if (is_normal_cube(w, var7, var8, var9) || is_normal_cube(w, var7, var8 + 1, var9))
    {
        int var14 = !is_normal_cube(w, var7 - 1, var8, var9) && !is_normal_cube(w, var7 - 1, var8 + 1, var9);
        int var15 = !is_normal_cube(w, var7 + 1, var8, var9) && !is_normal_cube(w, var7 + 1, var8 + 1, var9);
        int var16 = !is_normal_cube(w, var7, var8, var9 - 1) && !is_normal_cube(w, var7, var8 + 1, var9 - 1);
        int var17 = !is_normal_cube(w, var7, var8, var9 + 1) && !is_normal_cube(w, var7, var8 + 1, var9 + 1);
        signed char var18 = -1;
        double var19 = 9999.0;

        if (var14 && var10 < var19) { var19 = var10; var18 = 0; }
        if (var15 && 1.0 - var10 < var19) { var19 = 1.0 - var10; var18 = 1; }
        if (var16 && var12 < var19) { var19 = var12; var18 = 4; }
        if (var17 && 1.0 - var12 < var19) { var19 = 1.0 - var12; var18 = 5; }

        float var21 = 0.1F;

        if (var18 == 0) p->e.motion_x = (double)(-var21);
        if (var18 == 1) p->e.motion_x = (double)var21;
        if (var18 == 4) p->e.motion_z = (double)(-var21);
        if (var18 == 5) p->e.motion_z = (double)var21;
    }
}


/* Entity.handleWaterMovement for the client player: a water entry after
 * the first update is func_71061_d_'s splash, its sound and particle places
 * on the player's own Random, the bubbles and splashes built at the
 * particle setting */
static void cp_handle_water(struct client_player *p)
{
    struct entity *e = &p->e;
    int was = e->in_water;
    double mx = e->motion_x, my = e->motion_y, mz = e->motion_z;
    int hit = handle_water_movement(e->world, e->bounding_box, &mx, &my, &mz,
                                    &e->in_water, &e->fall_distance);
    e->motion_x = (neth_real)mx;
    e->motion_y = (neth_real)my;
    e->motion_z = (neth_real)mz;
    if (!hit || was || e->first_update || !surv.det) return;
    det_rng *r = &p->sv.erand;
    (void)det_rng_float(r);   /* the pitch, 1.0F + (nextFloat() - nextFloat()) * 0.4F */
    (void)det_rng_float(r);
    float var2 = (float)floor(e->bounding_box.min_y);
    for (int i = 0; (float)i < 1.0F + e->width * 20.0F; ++i)
    {
        float var4 = (det_rng_float(r) * 2.0F - 1.0F) * e->width;
        float var5 = (det_rng_float(r) * 2.0F - 1.0F) * e->width;
        double vy = e->motion_y - (double)(det_rng_float(r) * 0.2F);
        surv_client_fx_spawn(p, "bubble", e->pos_x + (double)var4, (double)(var2 + 1.0F), e->pos_z + (double)var5,
                             e->motion_x, vy, e->motion_z);
    }
    for (int i = 0; (float)i < 1.0F + e->width * 20.0F; ++i)
    {
        float var4 = (det_rng_float(r) * 2.0F - 1.0F) * e->width;
        float var5 = (det_rng_float(r) * 2.0F - 1.0F) * e->width;
        surv_client_fx_spawn(p, "splash", e->pos_x + (double)var4, (double)(var2 + 1.0F), e->pos_z + (double)var5,
                             e->motion_x, e->motion_y, e->motion_z);
    }
}

/* EntityLivingBase.updateFallState, the client player's override: re-check
 * water, land on blocks (a no-op body on every block these tapes touch), then
 * Entity's bookkeeping. fall()'s damage is a client-side no-op
 * (EntityClientPlayerMP.attackEntityFrom returns false). */
static void cp_fall_state(struct entity *e, double dy, int on_ground)
{
    struct client_player *p = (struct client_player *)((char *)e - offsetof(struct client_player, e));

    if (!e->in_water) cp_handle_water(p);

    if (on_ground && e->fall_distance > 0.0F)
    {
        /* Block.onFallenUpon: farmland and crops only; not reached here */
    }

    if (on_ground)
    {
        if (e->fall_distance > 0.0F) e->fall_distance = 0.0F;
    }
    else if (dy < 0.0)
    {
        e->fall_distance = (float)((double)e->fall_distance - dy);
    }

    (void)p;
}

/* EntityLivingBase.moveEntityWithHeading for a not-flying player. */
static void move_entity_with_heading(struct client_player *p, float strafe, float forward)
{
    struct world *w = p->e.world;
    struct entity *e = &p->e;
    struct collide_list *scratch COLLIDE_SCRATCH = collide_scratch_begin();
    double var8;

    if (e->in_water)
    {
        var8 = e->pos_y;
        move_flying((double)p->rotation_yaw, &e->motion_x, &e->motion_z, strafe, forward, 0.02F);
        entity_move_ex(e, e->motion_x, e->motion_y, e->motion_z, cp_is_sneaking(p), cp_fall_state);
        e->motion_x *= 0.800000011920929;
        e->motion_y *= 0.800000011920929;
        e->motion_z *= 0.800000011920929;
        e->motion_y -= 0.02;

        if (e->is_collided_horizontally &&
            is_offset_position_in_liquid(w, e->bounding_box, e->motion_x,
                                         e->motion_y + 0.6000000238418579 - e->pos_y + var8, e->motion_z,
                                         scratch))
        {
            e->motion_y = 0.30000001192092896;
        }
    }
    else if (world_is_material_in_bb(w, aabb_expand(e->bounding_box, -0.10000000149011612,
                                                    -0.4000000059604645, -0.10000000149011612), MAT_LAVA))
    {
        var8 = e->pos_y;
        move_flying((double)p->rotation_yaw, &e->motion_x, &e->motion_z, strafe, forward, 0.02F);
        entity_move_ex(e, e->motion_x, e->motion_y, e->motion_z, cp_is_sneaking(p), cp_fall_state);
        e->motion_x *= 0.5;
        e->motion_y *= 0.5;
        e->motion_z *= 0.5;
        e->motion_y -= 0.02;

        if (e->is_collided_horizontally &&
            is_offset_position_in_liquid(w, e->bounding_box, e->motion_x,
                                         e->motion_y + 0.6000000238418579 - e->pos_y + var8, e->motion_z,
                                         scratch))
        {
            e->motion_y = 0.30000001192092896;
        }
    }
    else
    {
        float var3 = 0.91F;

        if (e->on_ground)
        {
            float slip = neth_env->cfg.player_no_ground_friction ? 1.0F
                : BLOCKS[world_get_block(w, mh_floor(e->pos_x), mh_floor(e->bounding_box.min_y) - 1,
                                         mh_floor(e->pos_z)) & 4095].slipperiness;
            var3 = slip * 0.91F;
        }

        float var4 = 0.16277136F / (var3 * var3 * var3);
        float var5;

        if (e->on_ground) var5 = (float)p->move_speed * var4;
        else var5 = p->jump_movement_factor;

        move_flying((double)p->rotation_yaw, &e->motion_x, &e->motion_z, strafe, forward, var5);
        var3 = 0.91F;

        if (e->on_ground)
        {
            float slip = neth_env->cfg.player_no_ground_friction ? 1.0F
                : BLOCKS[world_get_block(w, mh_floor(e->pos_x), mh_floor(e->bounding_box.min_y) - 1,
                                         mh_floor(e->pos_z)) & 4095].slipperiness;
            var3 = slip * 0.91F;
        }

        if (is_on_ladder(w, e->pos_x, e->pos_z, e->bounding_box))
        {
            float var6 = 0.15F;

            if (e->motion_x < (double)(-var6)) e->motion_x = (double)(-var6);
            if (e->motion_x > (double)var6) e->motion_x = (double)var6;
            if (e->motion_z < (double)(-var6)) e->motion_z = (double)(-var6);
            if (e->motion_z > (double)var6) e->motion_z = (double)var6;

            e->fall_distance = 0.0F;

            if (e->motion_y < -0.15) e->motion_y = -0.15;

            if (cp_is_sneaking(p) && e->motion_y < 0.0) e->motion_y = 0.0;
        }

        entity_move_ex(e, e->motion_x, e->motion_y, e->motion_z, cp_is_sneaking(p), cp_fall_state);

        if (e->is_collided_horizontally && is_on_ladder(w, e->pos_x, e->pos_z, e->bounding_box))
            e->motion_y = 0.2;

        /* the client half: an unloaded chunk freezes the fall */
        int cx = (int)e->pos_x >> 4;
        int cz = (int)e->pos_z >> 4;

        if (!world_chunk_loaded(w, cx, cz))
        {
            if (e->pos_y > 0.0) e->motion_y = -0.1;
            else e->motion_y = 0.0;
        }
        else
        {
            e->motion_y -= 0.08;
        }

        e->motion_y *= 0.9800000190734863;
        e->motion_x *= (double)var3;
        e->motion_z *= (double)var3;
    }

    /* moveEntityWithHeading's tail: the limbs over the tick's step (render
     * only) */
    p->prev_limb_swing_amount = p->limb_swing_amount;
    {
        double lx = p->e.pos_x - p->e.prev_pos_x, lz = p->e.pos_z - p->e.prev_pos_z;
        float step = (float)sqrt(lx * lx + lz * lz) * 4.0F;
        if (step > 1.0F) step = 1.0F;
        p->limb_swing_amount += (step - p->limb_swing_amount) * 0.4F;
        p->limb_swing += p->limb_swing_amount;
    }
}

/* EntityPlayer.jump on top of EntityLivingBase.jump; the exhaustion half rides
 * on food stats nothing compared reads. */
static void jump_common(struct entity *e, int sprinting, float rotation_yaw)
{
    e->motion_y = 0.41999998688697815;

    if (sprinting)
    {
        float var1 = rotation_yaw * 0.017453292F;
        e->motion_x -= (double)(mh_sin(var1) * 0.2F);
        e->motion_z += (double)(mh_cos(var1) * 0.2F);
    }

}

/* EntityLivingBase.onLivingUpdate from updateEntityActionState down, for the
 * client player. */
static void cp_arm_update(struct client_player *p);

static void cp_living_core(struct client_player *p)
{
    struct entity *e = &p->e;

    if (p->jump_ticks > 0) --p->jump_ticks;

    /* isClientWorld() is true for EntityPlayerSP: no interpolation, no motion
     * damp */

    if (fabs(e->motion_x) < 0.005) e->motion_x = 0.0;
    if (fabs(e->motion_y) < 0.005) e->motion_y = 0.0;
    if (fabs(e->motion_z) < 0.005) e->motion_z = 0.0;

    /* updateEntityActionState (EntityPlayerSP) */
    float move_strafing = p->in_strafe;
    float move_forward = p->in_forward;
    p->is_jumping = p->in_jump;

    /* EntityLivingBase.isMovementBlocked: the dead player neither jumps nor
     * walks (the blocked branch zeroes the inputs) */
    if (p->sv.health <= 0.0F || p->sv.sleeping)
    {
        p->is_jumping = 0;
        move_strafing = 0.0F;
        move_forward = 0.0F;
    }
    else
    {
        cp_arm_update(p);
        /* the oldAi branch: rotationYawHead follows the look */
        p->yaw_head = p->rotation_yaw;
    }

    int lava = world_is_material_in_bb(e->world, aabb_expand(e->bounding_box, -0.10000000149011612,
                                                             -0.4000000059604645, -0.10000000149011612),
                                       MAT_LAVA);

    if (p->is_jumping)
    {
        if (!e->in_water && !lava)
        {
            if (e->on_ground && p->jump_ticks == 0)
            {
                jump_common(e, p->sprinting, p->rotation_yaw);
                p->jump_ticks = 10;
            }
        }
        else
        {
            e->motion_y += 0.03999999910593033;
        }
    }
    else
    {
        p->jump_ticks = 0;
    }

    move_strafing *= 0.98F;
    move_forward *= 0.98F;
    p->move_strafing = move_strafing;
    p->move_forward = move_forward;
    move_entity_with_heading(p, move_strafing, move_forward);
}

void client_swing_item(struct client_player *p)
{
    if (!p->swing_in_progress || p->swing_int >= 6 / 2 || p->swing_int < 0)
    {
        p->swing_int = -1;
        p->swing_in_progress = 1;
    }
}

/* EntityPlayerSP.updateEntityActionState's arm lag, then
 * EntityPlayer.updateEntityActionState's updateArmSwingProgress. */
static void cp_arm_update(struct client_player *p)
{
    p->prev_arm_yaw = p->arm_yaw;
    p->prev_arm_pitch = p->arm_pitch;
    p->arm_pitch = (float)((double)p->arm_pitch + (double)(p->rotation_pitch - p->arm_pitch) * 0.5);
    p->arm_yaw = (float)((double)p->arm_yaw + (double)(p->rotation_yaw - p->arm_yaw) * 0.5);
    if (p->swing_in_progress)
    {
        ++p->swing_int;
        if (p->swing_int >= 6)
        {
            p->swing_int = 0;
            p->swing_in_progress = 0;
        }
    }
    else
        p->swing_int = 0;
    p->swing_progress = (float)p->swing_int / 6.0F;
}

/* EntityPlayerSP.onLivingUpdate and the EntityPlayer/EntityLivingBase chain
 * around it, minus everything that only renders or only runs server-side. */
static void cp_on_living_update(struct client_player *p)
{
    struct entity *e = &p->e;

    if (p->sprinting_ticks_left > 0)
    {
        --p->sprinting_ticks_left;

        if (p->sprinting_ticks_left == 0) cp_set_sprinting(p, 0);
    }

    if (p->sprint_toggle_timer > 0) --p->sprint_toggle_timer;

    /* in a portal any open screen closes first (no C0D), its container's
     * client-side spills drawing the same Random */
    if (p->portal.in_portal) surv_client_portal_close(p);

    /* the portal timer: entering plays portal.trigger at a pitch drawn from
     * the player's own Random; a long confusion effect fills it too */
    {
        /* in a portal, a screen that is up closes (displayGuiScreen(null)) */
        if (p->portal.in_portal) surv_client_portal_close(p);
        const struct potion_effect *conf = potion_map_get(&p->sv.potions, 9);
        clientstate_portal_tick(&p->portal, conf ? conf->duration : -1, &p->sv.erand.r);
    }

    int var1 = p->in_jump;
    float var2 = 0.8F;
    int var3 = p->in_forward >= var2;

    /* MovementInputFromOptions.updatePlayerMoveState */
    float strafe = 0.0F, forward = 0.0F;

    if (p->keys.held[K_FORWARD]) ++forward;
    if (p->keys.held[K_BACK]) --forward;
    if (p->keys.held[K_LEFT]) ++strafe;
    if (p->keys.held[K_RIGHT]) --strafe;

    p->in_jump = p->keys.held[K_JUMP];
    p->in_sneak = p->keys.held[K_SNEAK];

    if (p->in_sneak)
    {
        strafe = (float)((double)strafe * 0.3);
        forward = (float)((double)forward * 0.3);
    }

    p->in_strafe = strafe;
    p->in_forward = forward;

    /* EntityPlayerSP.onLivingUpdate's using block: the itemInUse slowdown,
     * not while riding */
    if (p->sv.using_slot >= 0 && p->riding_id == 0)
    {
        p->in_strafe = (float)((double)p->in_strafe * 0.2);
        p->in_forward = (float)((double)p->in_forward * 0.2);
        p->sprint_toggle_timer = 0;
    }

    if (p->in_sneak && e->y_size < 0.2F) e->y_size = 0.2F;

    double w = (double)e->width * 0.35;
    push_out_of_blocks(p, e->pos_x - w, e->bounding_box.min_y + 0.5, e->pos_z + w);
    push_out_of_blocks(p, e->pos_x - w, e->bounding_box.min_y + 0.5, e->pos_z - w);
    push_out_of_blocks(p, e->pos_x + w, e->bounding_box.min_y + 0.5, e->pos_z - w);
    push_out_of_blocks(p, e->pos_x + w, e->bounding_box.min_y + 0.5, e->pos_z + w);

    int var4 = (float)p->sv.food.level > 6.0F || p->allow_flying;

    if (e->on_ground && !var3 && p->in_forward >= var2 && !p->sprinting && var4 &&
        p->sv.using_slot < 0)
    {
        if (p->sprint_toggle_timer <= 0 && !p->keys.held[K_SPRINT])
            p->sprint_toggle_timer = 7;
        else
            cp_set_sprinting(p, 1);
    }

    if (!p->sprinting && p->in_forward >= var2 && var4 && p->sv.using_slot < 0 &&
        p->keys.held[K_SPRINT])
        cp_set_sprinting(p, 1);

    if (p->sprinting && (p->in_forward < var2 || e->is_collided_horizontally || !var4))
        cp_set_sprinting(p, 0);

    /* allowFlying and the horse block never apply on these tapes */

    /* EntityPlayer.onLivingUpdate: flyToggleTimer, the peaceful heal and
     * the camera fields move nothing; decrementAnimations counts the
     * hotbar pops down */
    if (p->fly_toggle_timer > 0) --p->fly_toggle_timer;
    for (int i = 0; i < 36; ++i)
        if (p->inv_anim[i] > 0) --p->inv_anim[i];

    cp_living_core(p);

    p->jump_movement_factor = 0.02F;

    if (p->sprinting)
        p->jump_movement_factor = (float)((double)p->jump_movement_factor + (double)0.02F * 0.3);

    /* setAIMoveSpeed lands on landMovementFactor, which a player's
     * getAIMoveSpeed does not read: the attribute value is used directly */
    (void)var1;
}

/* Entity.onEntityUpdate and EntityLivingBase.onEntityUpdate for the client
 * player: the base tick the living update runs under. */
static void cp_on_entity_update(struct client_player *p)
{
    struct entity *e = &p->e;
    struct world *w = e->world;

    e->prev_pos_x = e->pos_x;
    e->prev_pos_y = e->pos_y;
    e->prev_pos_z = e->pos_z;
    p->prev_rotation_pitch = p->rotation_pitch;
    p->prev_rotation_yaw = p->rotation_yaw;
    p->prev_swing_progress = p->swing_progress;   /* EntityLivingBase.onEntityUpdate */

    /* the sprint's blockcrack under a dry sprinter: two draws on the
     * player's own Random place it, the FX builds at the particle setting */
    if (p->sprinting && !e->in_water && surv.det)
    {
        int bx = (int)floor(e->pos_x);
        int by = (int)floor(e->pos_y - 0.20000000298023224 - (double)e->y_offset);
        int bz = (int)floor(e->pos_z);
        int id = by >= 0 && by < 256 ? world_get_block(w, bx, by, bz) & 4095 : 0;
        if (BLOCKS[id].exists && BLOCKS[id].material != 0)
        {
            char name[48];
            snprintf(name, sizeof name, "blockcrack_%d_%d", id, world_get_meta(w, bx, by, bz));
            double fx = e->pos_x + ((double)det_rng_float(&p->sv.erand) - 0.5) * (double)e->width;
            double fz = e->pos_z + ((double)det_rng_float(&p->sv.erand) - 0.5) * (double)e->width;
            surv_client_fx_spawn(p, name, fx, e->bounding_box.min_y + 0.1, fz, -e->motion_x * 4.0, 1.5,
                                 -e->motion_z * 4.0);
        }
    }

    cp_handle_water(p);

    /* the client half of the fire block */
    e->fire = 0;

    if (world_is_material_in_bb(w, aabb_expand(e->bounding_box, -0.10000000149011612, -0.4000000059604645,
                                               -0.10000000149011612), MAT_LAVA))
    {
        /* setOnFireFromLava: attackEntityFrom is false on the client player */
        e->fall_distance *= 0.5F;
    }

    /* posY < -64: EntityLivingBase.kill is attackEntityFrom(outOfWorld, 4),
     * which EntityClientPlayerMP answers false: the client player falls on
     * (the server's hits bring the health down) */

    e->first_update = 0;

    /* EntityLivingBase.onEntityUpdate's survival half: the hurt and death
     * timers that open (and close) the game-over screen */
    surv_client_base_tick(p);

    /* its tail's previous body and head yaw (render only) */
    p->prev_render_yaw_offset = p->render_yaw_offset;
    p->prev_yaw_head = p->yaw_head;
}

static float cp_wrap180(float a)
{
    a = fmodf(a, 360.0F);
    if (a >= 180.0F) a -= 360.0F;
    if (a < -180.0F) a += 360.0F;
    return a;
}

/* EntityLivingBase.onUpdate after onLivingUpdate: the body turns toward the
 * walk (func_110146_f), and the previous body and head yaw are brought within
 * 180 degrees (render only; the rows read none of it). */
void cp_living_tail(struct client_player *p)
{
    double dx = p->e.pos_x - p->e.prev_pos_x, dz = p->e.pos_z - p->e.prev_pos_z;
    float moved = (float)(dx * dx + dz * dz);
    float target = p->render_yaw_offset;

    if (moved > 0.0025000002F)
        target = (float)fd_atan2(dz, dx) * 180.0F / (float)M_PI - 90.0F;
    if (p->swing_progress > 0.0F) target = p->rotation_yaw;

    float d = cp_wrap180(target - p->render_yaw_offset);
    p->render_yaw_offset += d * 0.3F;
    float off = cp_wrap180(p->rotation_yaw - p->render_yaw_offset);
    if (off < -75.0F) off = -75.0F;
    if (off >= 75.0F) off = 75.0F;
    p->render_yaw_offset = p->rotation_yaw - off;
    if (off * off > 2500.0F) p->render_yaw_offset += off * 0.2F;

    while (p->render_yaw_offset - p->prev_render_yaw_offset < -180.0F) p->prev_render_yaw_offset -= 360.0F;
    while (p->render_yaw_offset - p->prev_render_yaw_offset >= 180.0F) p->prev_render_yaw_offset += 360.0F;
    while (p->yaw_head - p->prev_yaw_head < -180.0F) p->prev_yaw_head -= 360.0F;
    while (p->yaw_head - p->prev_yaw_head >= 180.0F) p->prev_yaw_head += 360.0F;
}

/* sendMotionUpdates: the C0B actions and the C03 packet this tick's state
 * produce, in the order the client sends them. */
static void cp_send_motion_updates(struct client_player *p, struct c03 *out, int *c0b, int *nc0b)
{
    struct entity *e = &p->e;
    int var1 = p->sprinting;

    *nc0b = 0;

    if (var1 != p->was_sneaking)
    {
        c0b[(*nc0b)++] = var1 ? C0B_SPRINT_ON : C0B_SPRINT_OFF;
        p->was_sneaking = var1;
    }

    int var2 = cp_is_sneaking(p);

    if (var2 != p->should_stop_sneaking)
    {
        c0b[(*nc0b)++] = var2 ? C0B_SNEAK_ON : C0B_SNEAK_OFF;
        p->should_stop_sneaking = var2;
    }

    double var3 = e->pos_x - p->old_pos_x;
    double var5 = e->bounding_box.min_y - p->old_min_y;
    double var7 = e->pos_z - p->old_pos_z;
    double var9 = (double)(p->rotation_yaw - p->old_rotation_yaw);
    double var11 = (double)(p->rotation_pitch - p->old_rotation_pitch);
    int var13 = var3 * var3 + var5 * var5 + var7 * var7 > 9.0E-4 || p->ticks_since_move_packet >= 20;
    int var14 = var9 != 0.0 || var11 != 0.0;

    memset(out, 0, sizeof *out);
    out->on_ground = e->on_ground;
    out->yaw = p->rotation_yaw;
    out->pitch = p->rotation_pitch;

    if (var13 && var14)
    {
        out->kind = C03_POSLOOK;
        out->c = e->pos_x;
        out->d = e->bounding_box.min_y;
        out->f = e->pos_y;
        out->e = e->pos_z;
    }
    else if (var13)
    {
        out->kind = C03_POS;
        out->c = e->pos_x;
        out->d = e->bounding_box.min_y;
        out->f = e->pos_y;
        out->e = e->pos_z;
    }
    else if (var14)
    {
        out->kind = C03_LOOK;
    }
    else
    {
        out->kind = C03_STOP;
    }

    ++p->ticks_since_move_packet;
    p->was_on_ground = e->on_ground;

    if (var13)
    {
        p->old_pos_x = e->pos_x;
        p->old_min_y = e->bounding_box.min_y;
        p->old_pos_y = e->pos_y;
        p->old_pos_z = e->pos_z;
        p->ticks_since_move_packet = 0;
    }

    if (var14)
    {
        p->old_rotation_yaw = p->rotation_yaw;
        p->old_rotation_pitch = p->rotation_pitch;
    }
}

/* ------------------------------------------------------------ the client tick */

/* The client's World.updateEntity(rider): EntityPlayer.updateRidden (the
 * sneak dismount is the server's), Entity.updateRidden's zeroed motion and
 * onUpdate (EntityClientPlayerMP's riding branch: the C05 and the C0C in
 * place of sendMotionUpdates), updateRiderPosition, the fall reset, and the
 * pig's rider keeps its look. */
void ride_client_update_ridden(struct client_player *p, struct client_out *out)
{
    float yaw0 = p->rotation_yaw, pitch0 = p->rotation_pitch;

    /* Entity.updateRidden on a vehicle that set itself dead in its own
     * update (its client deathTime reached 20): the rider only lets go, no
     * onUpdate and so no C05 and C0C this tick; EntityLivingBase.updateRidden
     * still clears the fall */
    if (!ride_client_riding(p))
    {
        p->e.fall_distance = 0.0F;
        return;
    }

    p->e.motion_x = p->e.motion_y = p->e.motion_z = 0.0;
    surv_client_eat_tick(p);
    sleep_client_update(p);
    cp_on_entity_update(p);
    cp_on_living_update(p);
    cp_living_tail(p);

    memset(&out->c03, 0, sizeof out->c03);
    out->c03.kind = C03_LOOK;
    out->c03.yaw = p->rotation_yaw;
    out->c03.pitch = p->rotation_pitch;
    out->c03.on_ground = p->e.on_ground;
    out->has_c03 = 1;
    out->has_c0c = 1;
    out->c0c_strafe = p->move_strafing;
    out->c0c_forward = p->move_forward;
    out->c0c_jump = p->in_jump;
    out->c0c_sneak = p->in_sneak;

    ride_client_rider_position(p);
    p->e.fall_distance = 0.0F;
    p->rotation_pitch = pitch0;
    p->rotation_yaw = yaw0;
}

void client_player_pick(struct client_player *p, const struct act *a)
{
    if (a->has_look)
    {
        /* Act.applyLook, the absolute form */
        p->rotation_yaw = a->look[0];
        p->rotation_pitch = a->look[1];
        p->prev_rotation_yaw = a->look[2];
        p->prev_rotation_pitch = a->look[3];
    }

    if (!p->picked && p->e.world != NULL) surv_client_mouseover(p);
    p->picked = 1;
}

/* The shared client draws of runTick's TextureManager.tick (TextureClock's
 * and TextureCompass's targets, a Math.random each off the surface world)
 * and EntityRenderer.updateRenderer (updateTorchFlicker's eight), spent
 * without their values where no update_renderer (the playable client's,
 * which spends them itself) draws the frame */
static void cp_renderer_draws(struct client_player *p)
{
    if (!surv.det) return;
    if (p->e.world && p->e.world->dim != 0)
    {
        (void)det_math_random_role(surv.det, DET_CLIENT);
        (void)det_math_random_role(surv.det, DET_CLIENT);
    }
    for (int i = 0; i < 8; ++i) (void)det_math_random_role(surv.det, DET_CLIENT);
}

void client_player_set_world_rand(struct client_player *p, uint64_t seed, int32_t lcg)
{
    p->cw_rand.r.seed = seed;
    p->cw_rand.have_next_next_gaussian = 0;
    p->cw_lcg = lcg;
    p->cw_rand_known = 1;
    /* the fog loop's slots start again (a start's world may sit where
     * another was) */
    p->fog_world = NULL;
}

void client_player_new_world_rand(struct client_player *p)
{
    /* World's field initializers: updateLCG from a Det.newRandom's nextInt,
     * then rand */
    det_rng lcg = det_new_random_role(surv.det, DET_CLIENT);
    p->cw_lcg = det_rng_int(&lcg);
    p->cw_rand = det_new_random_role(surv.det, DET_CLIENT);
    /* the constructor's ambientTickCountdown */
    (void)det_rng_int_n(&p->cw_rand, 12000);
    p->cw_rand_known = 1;
    /* a new WorldClient: its light starts from the chunks it is sent, and
     * the fog loop's slots name none of its chunks */
    p->cw_check_missed = 0;
    p->fog_world = NULL;
}

/* the LCG's k steps as one affine map, s * FOG_AC[k][0] + FOG_AC[k][1]
 * (mod 2^48), so a cell's six coordinate draws come from its first seed at
 * once rather than one after another */
static const uint64_t FOG_AC[25][2] = {
    {0x000000000001ULL, 0x000000000000ULL}, {0x0005deece66dULL, 0x00000000000bULL},
    {0xbb20b4600a69ULL, 0x0040942de6baULL}, {0xd498bd0ac4b5ULL, 0x0aa8544e593dULL},
    {0x32eb772c5f11ULL, 0x2d3873c4cd04ULL}, {0x6e00fcf9c03dULL, 0x5d5692ace2bfULL},
    {0x45d73749a7f9ULL, 0x17617168255eULL}, {0x37bc7ed23b05ULL, 0x17a0d1925d11ULL},
    {0x75489f259f21ULL, 0x7cba449ae648ULL}, {0xa139e068670dULL, 0x86d91b38bcb3ULL},
    {0x121f9f058e89ULL, 0x7455bfb52a42ULL}, {0x53bce7b8c655ULL, 0x3bb194f24a25ULL},
    {0x199c3838d031ULL, 0xd4cf89e2cfccULL}, {0x6be66668aaddULL, 0xee85f453c1e7ULL},
    {0x7415cdcf4e19ULL, 0x916c5ad31966ULL}, {0x0b838e7ab6a5ULL, 0x8e5c32bb7479ULL},
    {0x6dc260740241ULL, 0x0d0352014d90ULL}, {0xee96bd575badULL, 0xc45d76fd665bULL},
    {0x1cb4a40b76a9ULL, 0x04d799be56caULL}, {0x011117495bf5ULL, 0x409aa63c700dULL},
    {0xb05b0eb64551ULL, 0x4c56a6636394ULL}, {0xe0b62a8e497dULL, 0xe04048385e0fULL},
    {0x662319d79839ULL, 0x88141c55866eULL}, {0xc808761b0645ULL, 0xeb96389910e1ULL},
    {0x35692ebfa961ULL, 0x83f34bc255d8ULL},
};
#define FOG_AT(k) ((s * FOG_AC[k][0] + FOG_AC[k][1]) & JR_MASK)

/* steps 0 to 14 for a seed held as s << 16 (mod 2^64): the multiplier as
 * it is, the addend shifted */
#define FOG_C16(c) ((uint64_t)(c) << 16)
static const uint64_t FOG_AC16[15][2] = {
    {0x000000000001ULL, FOG_C16(0x000000000000ULL)}, {0x0005deece66dULL, FOG_C16(0x00000000000bULL)},
    {0xbb20b4600a69ULL, FOG_C16(0x0040942de6baULL)}, {0xd498bd0ac4b5ULL, FOG_C16(0x0aa8544e593dULL)},
    {0x32eb772c5f11ULL, FOG_C16(0x2d3873c4cd04ULL)}, {0x6e00fcf9c03dULL, FOG_C16(0x5d5692ace2bfULL)},
    {0x45d73749a7f9ULL, FOG_C16(0x17617168255eULL)}, {0x37bc7ed23b05ULL, FOG_C16(0x17a0d1925d11ULL)},
    {0x75489f259f21ULL, FOG_C16(0x7cba449ae648ULL)}, {0xa139e068670dULL, FOG_C16(0x86d91b38bcb3ULL)},
    {0x121f9f058e89ULL, FOG_C16(0x7455bfb52a42ULL)}, {0x53bce7b8c655ULL, FOG_C16(0x3bb194f24a25ULL)},
    {0x199c3838d031ULL, FOG_C16(0xd4cf89e2cfccULL)}, {0x6be66668aaddULL, FOG_C16(0xee85f453c1e7ULL)},
    {0x7415cdcf4e19ULL, FOG_C16(0x916c5ad31966ULL)},
};
#undef FOG_C16

/* the bits of a band with no section: every cell reads air */
static const uint8_t FOG_NONE[SEC_CELLS / 8];

/* The verify build (#define CP_FOG_VERIFY before player.c's includes) runs
 * the loop as it was before the slots (cp_void_fog_ref) beside it every
 * tick on a copy of the seed and prints at exit the ticks, the seeds that
 * differ, cp_fog_slot's hits and misses, the ticks on the window, on the
 * two-cell steps and on a window kept from the tick before. */
#ifdef CP_FOG_VERIFY
static _Atomic uint64_t fog_ticks, fog_bad, fog_hit, fog_miss, fog_wint, fog_look, fog_same, fog_lanes;
static _Atomic uint64_t fog_slot_lanes, fog_slot_bad, fog_win_lanes, fog_win_bad;
#define FOG_COUNT(n) (++(n))
#else
#define FOG_COUNT(n) ((void)0)
#endif

#if defined(__x86_64__) && defined(__GNUC__)
static int cp_fog_bits_ok(void)
{
    return __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512bw") &&
           __builtin_cpu_supports("avx512bitalg");
}

/* cp_fog_slot's bits in AVX-512 lanes: 64 ids a step, cell i's bit lut[id]
 * taken from a 256-bit table (lut's bytes as four 64-bit masks) by the id's
 * low six bits in the quarter its top two name (BITALG's bit shuffle), the
 * step's 64-bit mask the 8 bytes of bits its cells own; returns whether an
 * id is 74 (lit redstone ore) */
__attribute__((target("avx512f,avx512bw,avx512bitalg"), noinline)) static int
cp_fog_bits_lanes(const uint8_t *ids, const uint8_t *lut, uint8_t *bits)
{
    __m512i t[4];
    for (int q = 0; q < 4; ++q)
    {
        __m512i l = _mm512_loadu_si512(lut + 64 * q);
        t[q] = _mm512_set1_epi64((long long)_mm512_test_epi8_mask(l, l));
    }
    const __m512i c64 = _mm512_set1_epi8(64), c128 = _mm512_set1_epi8((char)128), c192 = _mm512_set1_epi8((char)192);
    const __m512i ore = _mm512_set1_epi8(74);
    __mmask64 any = 0;
    for (int i = 0; i < SEC_CELLS; i += 64)
    {
        __m512i v = _mm512_loadu_si512(ids + i);
        __mmask64 lt64 = _mm512_cmplt_epu8_mask(v, c64), lt128 = _mm512_cmplt_epu8_mask(v, c128);
        __mmask64 lt192 = _mm512_cmplt_epu8_mask(v, c192);
        uint64_t m = (_mm512_bitshuffle_epi64_mask(t[0], v) & lt64) |
                     (_mm512_bitshuffle_epi64_mask(t[1], v) & lt128 & ~lt64) |
                     (_mm512_bitshuffle_epi64_mask(t[2], v) & lt192 & ~lt128) |
                     (_mm512_bitshuffle_epi64_mask(t[3], v) & ~lt192);
        memcpy(bits + (i >> 3), &m, 8);
        any |= _mm512_cmpeq_epi8_mask(v, ore);
    }
    return any != 0;
}
#endif

/* p's slot for band sec of chunk c (the client world's, no ids past 255),
 * its bits made from the band's ids when the slot held another band or the
 * chunk was written since; lut[id] is the air test's failure by id, made
 * on the tick's first miss (*lut_made) */
static int cp_fog_slot(struct client_player *p, const struct chunk *c, int sec, uint8_t *lut, int *lut_made)
{
    int k = (c->cx & 3) << 4 | (c->cz & 3) << 2 | (sec & 3);
    struct cp_fog_key *key = &p->fog_key[k];

    if (key->cx == c->cx && key->cz == c->cz && key->sec == sec && key->wseq == c->wseq)
    {
        for (int l = 0; l < SEC_CELLS / 8; l += 64) __builtin_prefetch(p->fog_bits[k] + l);
        FOG_COUNT(fog_hit);
        return k;
    }
    FOG_COUNT(fog_miss);
    if (!*lut_made)
    {
        for (int id = 0; id < 256; ++id) lut[id] = id != 0 && BLOCKS[id].exists && BLOCKS[id].material != 0;
        *lut_made = 1;
    }
    const uint8_t *ids = chunk_sec_at(c, sec)->ids;
    uint8_t *bits = p->fog_bits[k];
    int ore = 0;
#if defined(__x86_64__) && defined(__GNUC__)
    if (cp_fog_bits_ok())
    {
        ore = cp_fog_bits_lanes(ids, lut, bits);
#ifdef CP_FOG_VERIFY
        /* the loop below on a copy, which must agree */
        uint8_t ref[SEC_CELLS / 8];
        int ref_ore = 0;
        for (int i = 0; i < SEC_CELLS; i += 8)
        {
            unsigned v = 0;
            for (int j = 0; j < 8; ++j)
            {
                v |= (unsigned)lut[ids[i + j]] << j;
                ref_ore |= ids[i + j] == 74;
            }
            ref[i >> 3] = (uint8_t)v;
        }
        FOG_COUNT(fog_slot_lanes);
        if (ref_ore != ore || memcmp(ref, bits, sizeof ref)) FOG_COUNT(fog_slot_bad);
#endif
    }
    else
#endif
    for (int i = 0; i < SEC_CELLS; i += 8)
    {
        unsigned v = 0;
        for (int j = 0; j < 8; ++j)
        {
            v |= (unsigned)lut[ids[i + j]] << j;
            ore |= ids[i + j] == 74;
        }
        bits[i >> 3] = (uint8_t)v;
    }
    key->cx = c->cx;
    key->cz = c->cz;
    key->sec = sec;
    key->ore = ore;
    key->wseq = c->wseq;
    return k;
}

#if defined(__x86_64__) && defined(__GNUC__)
/* The look-ahead loop below in AVX-512 lanes. A cell's air test depends only
 * on how many draws came before it (its offset k: its draws are k + 1 to
 * k + 6), not on the cells before, so every offset's cell is made in lanes,
 * 32 at a time: the draws' four bits from four lanes of LCG seeds 32 steps
 * apart (the seed as s << 16, as there), then each offset's cell as its bit
 * in the window, (((dx + 15) * 32 + dz + 15) * 32 + dy + 15). The chain then
 * walks four cells a step through those words: the ten offsets the four can
 * sit at (6j + 0 to j) tested at once, selected as masks, so a step waits on
 * one word load and one window load. The lanes run 256 to 448 offsets ahead
 * of the chain, interleaved with it (it is latency bound and leaves them
 * room), in two small rings on the stack: a tick's buffers stay out of the
 * env's cached lines. The chain ends k draws on (6000 to 7000), the seed then
 * from k's bits, 2^i steps each. Exact to the loop on every seed (a bench of
 * 2,000 seeds a density, and CP_FOG_VERIFY), in about half its cycles. */
/* steps 0 to 31 (mod 2^48) */
static const uint64_t FOG_LA[32] = {
    0x000000000001ULL, 0x0005deece66dULL, 0xbb20b4600a69ULL, 0xd498bd0ac4b5ULL,
    0x32eb772c5f11ULL, 0x6e00fcf9c03dULL, 0x45d73749a7f9ULL, 0x37bc7ed23b05ULL,
    0x75489f259f21ULL, 0xa139e068670dULL, 0x121f9f058e89ULL, 0x53bce7b8c655ULL,
    0x199c3838d031ULL, 0x6be66668aaddULL, 0x7415cdcf4e19ULL, 0x0b838e7ab6a5ULL,
    0x6dc260740241ULL, 0xee96bd575badULL, 0x1cb4a40b76a9ULL, 0x011117495bf5ULL,
    0xb05b0eb64551ULL, 0xe0b62a8e497dULL, 0x662319d79839ULL, 0xc808761b0645ULL,
    0x35692ebfa961ULL, 0xda405d34444dULL, 0x560f619a42c9ULL, 0x844a05fb0595ULL,
    0x230d6e413e71ULL, 0x6e471e0d1c1dULL, 0x784fa3930659ULL, 0x31a6c559a9e5ULL,
};
/* their addends, shifted for the s << 16 form */
static const uint64_t FOG_LC[32] = {
    0x0000000000000000ULL, 0x00000000000b0000ULL, 0x0040942de6ba0000ULL, 0x0aa8544e593d0000ULL,
    0x2d3873c4cd040000ULL, 0x5d5692ace2bf0000ULL, 0x17617168255e0000ULL, 0x17a0d1925d110000ULL,
    0x7cba449ae6480000ULL, 0x86d91b38bcb30000ULL, 0x7455bfb52a420000ULL, 0x3bb194f24a250000ULL,
    0xd4cf89e2cfcc0000ULL, 0xee85f453c1e70000ULL, 0x916c5ad319660000ULL, 0x8e5c32bb74790000ULL,
    0x0d0352014d900000ULL, 0xc45d76fd665b0000ULL, 0x04d799be56ca0000ULL, 0x409aa63c700d0000ULL,
    0x4c56a66363940000ULL, 0xe04048385e0f0000ULL, 0x88141c55866e0000ULL, 0xeb96389910e10000ULL,
    0x83f34bc255d80000ULL, 0xcf784dfe9d030000ULL, 0x303c4f3d8c520000ULL, 0xe2e580de6af50000ULL,
    0x9213c2a7a85c0000ULL, 0x13a77f7557370000ULL, 0xd0b5ca058c760000ULL, 0xfc21d156d2490000ULL,
};
/* 32 steps, the lanes' stride */
#define FOG_L32A 0xfac6caed1481ULL
#define FOG_L32C 0x1e4c4c311f200000ULL
/* 2^i steps, i = 0 to 12 (mod 2^48) */
static const uint64_t FOG_JUMP[13][2] = {
    {0x0005deece66dULL, 0x00000000000bULL}, {0xbb20b4600a69ULL, 0x0040942de6baULL},
    {0x32eb772c5f11ULL, 0x2d3873c4cd04ULL}, {0x75489f259f21ULL, 0x7cba449ae648ULL},
    {0x6dc260740241ULL, 0x0d0352014d90ULL}, {0xfac6caed1481ULL, 0x1e4c4c311f20ULL},
    {0xab768c7e6901ULL, 0xf77b98004e40ULL}, {0xa430a00dd201ULL, 0x6cc0d398dc80ULL},
    {0x4fa0405fa401ULL, 0xa7a83e92b900ULL}, {0x94fba1cf4801ULL, 0x9e1922a97200ULL},
    {0xba5cc7de9001ULL, 0xaba71b62e400ULL}, {0xba17a0bd2001ULL, 0xe5b38f05c800ULL},
    {0x27e7857a4001ULL, 0x698c7f0b9000ULL},
};
/* the offsets made: 1000 cells end before 7000 + a step's 22, so 7104 (32 a
 * round), each round's 32 cells only while fewer than FOG_LEAD lie ahead of
 * the chain; the draws in a ring of 256 (its first 64 mirrored past its end,
 * so a round's six loads never wrap), the cells in one of 512 (its first 32
 * mirrored: a step reads 22 on) */
#define FOG_LNC 7104
#define FOG_LEAD 448

/* c ? a : b as a cmov: the air tests are coin flips */
static inline __attribute__((always_inline)) uint32_t fog_sel(uint32_t c, uint32_t a, uint32_t b)
{
    __asm__("test %1, %1\n\tcmovne %2, %0" : "+r"(b) : "r"(c), "r"(a) : "cc");
    return b;
}

static int cp_fog_lanes_ok(void)
{
    return __builtin_cpu_supports("avx512f") && __builtin_cpu_supports("avx512dq") &&
           __builtin_cpu_supports("avx512bw") && __builtin_cpu_supports("avx512vl") && __builtin_cpu_supports("bmi2");
}

/* the seed after 1000 cells from seed s over window win (fog_win: word
 * (dx + 15) * 32 + dz + 15, bit dy + 15 set where the cell is not air) */
__attribute__((target("avx512f,avx512dq,avx512bw,avx512vl,bmi2"), noinline)) static uint64_t
cp_fog_lanes(uint64_t s, const uint32_t *win)
{
    _Alignas(64) uint8_t v[256 + 64];
    _Alignas(64) uint16_t cell[512 + 32];
    const __m512i t = _mm512_set1_epi64((long long)(s << 16));
    __m512i x0 = _mm512_add_epi64(_mm512_mullo_epi64(t, _mm512_loadu_si512(FOG_LA)), _mm512_loadu_si512(FOG_LC));
    __m512i x1 = _mm512_add_epi64(_mm512_mullo_epi64(t, _mm512_loadu_si512(FOG_LA + 8)), _mm512_loadu_si512(FOG_LC + 8));
    __m512i x2 = _mm512_add_epi64(_mm512_mullo_epi64(t, _mm512_loadu_si512(FOG_LA + 16)), _mm512_loadu_si512(FOG_LC + 16));
    __m512i x3 = _mm512_add_epi64(_mm512_mullo_epi64(t, _mm512_loadu_si512(FOG_LA + 24)), _mm512_loadu_si512(FOG_LC + 24));
    const __m512i a32 = _mm512_set1_epi64((long long)FOG_L32A), c32 = _mm512_set1_epi64((long long)FOG_L32C);
    const __m512i base = _mm512_set1_epi16((15 * 32 + 15) * 32 + 15);
    unsigned nd = 0, nc = 0;
    /* draws nd to nd + 31: their top four bits, the lanes 32 steps on */
#define FOG_DRAWS()                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        __m256i lo = _mm256_set_m128i(_mm512_cvtepi64_epi8(_mm512_srli_epi64(x1, 60)),                                \
                                      _mm512_cvtepi64_epi8(_mm512_srli_epi64(x0, 60)));                               \
        __m256i hi = _mm256_set_m128i(_mm512_cvtepi64_epi8(_mm512_srli_epi64(x3, 60)),                                \
                                      _mm512_cvtepi64_epi8(_mm512_srli_epi64(x2, 60)));                               \
        __m256i d = _mm256_permute4x64_epi64(_mm256_unpacklo_epi64(lo, hi), 0xd8);                                     \
        _mm256_store_si256((__m256i *)(v + (nd & 255)), d);                                                            \
        if ((nd & 255) < 64) _mm256_store_si256((__m256i *)(v + 256 + (nd & 255)), d);                                \
        x0 = _mm512_add_epi64(_mm512_mullo_epi64(x0, a32), c32);                                                       \
        x1 = _mm512_add_epi64(_mm512_mullo_epi64(x1, a32), c32);                                                       \
        x2 = _mm512_add_epi64(_mm512_mullo_epi64(x2, a32), c32);                                                       \
        x3 = _mm512_add_epi64(_mm512_mullo_epi64(x3, a32), c32);                                                       \
        nd += 32;                                                                                                      \
    } while (0)
    /* the cells of offsets nc to nc + 31, offset k's from draws k + 1 to k + 6 */
#define FOG_CELLS()                                                                                                    \
    do                                                                                                                 \
    {                                                                                                                  \
        const uint8_t *q = v + (nc & 255);                                                                             \
        __m512i d1 = _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *)(q + 1)));                               \
        __m512i d2 = _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *)(q + 2)));                               \
        __m512i d3 = _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *)(q + 3)));                               \
        __m512i d4 = _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *)(q + 4)));                               \
        __m512i d5 = _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *)(q + 5)));                               \
        __m512i d6 = _mm512_cvtepu8_epi16(_mm256_loadu_si256((const __m256i *)(q + 6)));                               \
        __m512i w = _mm512_add_epi16(_mm512_slli_epi16(_mm512_sub_epi16(d1, d2), 5), _mm512_sub_epi16(d5, d6));        \
        w = _mm512_add_epi16(_mm512_add_epi16(_mm512_slli_epi16(w, 5), _mm512_sub_epi16(d3, d4)), base);               \
        _mm512_store_si512(cell + (nc & 511), w);                                                                      \
        if ((nc & 511) == 0) _mm512_store_si512(cell + 512, w);                                                        \
        nc += 32;                                                                                                      \
    } while (0)
    /* two rounds of draws ahead of the cells, 256 cells ahead of the chain */
    FOG_DRAWS();
    FOG_DRAWS();
    for (int i = 0; i < 8; ++i)
    {
        FOG_DRAWS();
        FOG_CELLS();
    }
    unsigned k = 0;
#define FOG_LAIR(j) (~(win[c[j] >> 5] >> (c[j] & 31)) & 1u)
    for (int i = 0; i < 1000; i += 4)
    {
        if (nc < FOG_LNC && nc - k < FOG_LEAD)
        {
            FOG_DRAWS();
            FOG_CELLS();
        }
        const uint16_t *c = cell + (k & 511);
        uint32_t a0 = FOG_LAIR(0), a6 = FOG_LAIR(6), a7 = FOG_LAIR(7);
        uint32_t a12 = FOG_LAIR(12), a13 = FOG_LAIR(13), a14 = FOG_LAIR(14);
        uint32_t a18 = FOG_LAIR(18), a19 = FOG_LAIR(19), a20 = FOG_LAIR(20), a21 = FOG_LAIR(21);
        /* the second cell's air by the first's, the third's and fourth's
         * candidates by the first two, then the last two choices */
        uint32_t a1 = fog_sel(a0, a7, a6);
        uint32_t lo = fog_sel(a0, a13, a12), hi = fog_sel(a0, a14, a13);
        uint32_t b18 = fog_sel(a0, a19, a18), b19 = fog_sel(a0, a20, a19), b20 = fog_sel(a0, a21, a20);
        uint32_t c_lo = fog_sel(a1, b19, b18), c_hi = fog_sel(a1, b20, b19);
        uint32_t a2 = fog_sel(a1, hi, lo);
        uint32_t a3 = fog_sel(a2, c_hi, c_lo);
        k += 24 + a0 + a1 + a2 + a3;
    }
#undef FOG_LAIR
#undef FOG_CELLS
#undef FOG_DRAWS
    for (int i = 0; i < 13; ++i)
        if (k >> i & 1) s = (s * FOG_JUMP[i][0] + FOG_JUMP[i][1]) & JR_MASK;
    return s;
}

/* cp_void_fog_minimal's window from the bands' bits in lanes: for one x the
 * columns along z within a chunk are consecutive 16-bit words of each band,
 * so up to 16 columns are one masked load a band, their 48-bit columns
 * shifted by sh (rows below the world come in as 0) into 32-bit words */
__attribute__((target("avx512f,avx512bw,avx512vl"), noinline)) static void
cp_fog_window_lanes(uint32_t *win, const uint8_t *const *bits, int cx0, int cz0, int px, int pz, int sh)
{
    const __m128i c0 = _mm_cvtsi32_si128(sh >= 0 ? sh : -sh), c1 = _mm_cvtsi32_si128(16 - sh);
    const __m128i c2 = _mm_cvtsi32_si128(32 - sh);
    for (int dx = 0; dx < 31; ++dx)
    {
        int x = px - 15 + dx;
        const uint8_t *const *bi = bits + ((x >> 4) - cx0) * 9;
        for (int dz = 0; dz < 31;)
        {
            int z = pz - 15 + dz, n = 16 - (z & 15);
            if (n > 31 - dz) n = 31 - dz;
            const uint8_t *const *bb = bi + ((z >> 4) - cz0) * 3;
            int off = (x & 15) << 5 | (z & 15) << 1;
            __mmask16 m = (__mmask16)((1u << n) - 1);
            __m512i r0 = _mm512_cvtepu16_epi32(_mm256_maskz_loadu_epi16(m, bb[0] + off));
            __m512i r1 = _mm512_cvtepu16_epi32(_mm256_maskz_loadu_epi16(m, bb[1] + off));
            __m512i r2 = _mm512_cvtepu16_epi32(_mm256_maskz_loadu_epi16(m, bb[2] + off));
            __m512i a = sh >= 0 ? _mm512_srl_epi32(r0, c0) : _mm512_sll_epi32(r0, c0);
            __m512i w = _mm512_or_si512(a, _mm512_or_si512(_mm512_sll_epi32(r1, c1), _mm512_sll_epi32(r2, c2)));
            _mm512_mask_storeu_epi32(win + (dx << 5 | dz), m, w);
            dz += n;
        }
    }
}
#endif

/* doVoidFogParticles at GameSettings.particleSetting 2 (minimal, the
 * pinned profile's and every training start's), its draws alone: there
 * RenderGlobal.doSpawnParticle returns before it builds an FX or draws
 * (its setting-1 roll), so no cell's work reaches anything but the Randoms.
 * An air cell's roll and its depthsuspend place stay on the world's Random;
 * every randomDisplayTick (particles_live_display_tick) draws only var5,
 * a fresh Random dropped at the tick's end, and its block reads change
 * nothing, except lit redstone ore's func_150186_m, which places its six
 * reddust rows on the world's Random (three nextFloat each). The cells'
 * block reads are kept, the same reads in the same order: their air test
 * decides the draws, and so the next tick's light-check cell. Every draw
 * here is a power-of-two nextInt or a nextFloat, one LCG step each, so the
 * seed stays in a register: nextInt(16) is the step's top four bits,
 * nextInt(8) its top three.
 *
 * A cell's air test reads a bit, not its id and BLOCKS: each band's bits
 * are kept in a slot of the player (cp_fog_slot) while its chunk is not
 * written, where the 27 bands' ids were 108 KB a tick of likely misses (an
 * env's bands leave the cache between its steps; lane/voidfog). Where no
 * band within reach holds lit ore or needs the exact read, the cells within
 * 15 are one 4 KB window of those bits, kept while the player's cell and
 * the nine chunks hold, and the loop steps two cells at a time without a
 * branch on the air test. Otherwise a band holding lit ore reads the id of
 * a cell whose bit is set, and a band the bits cannot answer reads every
 * cell through chunkscan_block, whose reads are World.getBlock's (a chunk
 * not loaded, a band with ids past 255, the world's edge near). */
static void cp_void_fog_minimal(struct client_player *p, det_rng *wr, struct chunkscan *cs, int px, int py, int pz,
                                int void_particles)
{
    /* the cells' bands, looked up once: (x >> 4) - cx0 and (z >> 4) - cz0
     * are 0 to 2 (the cells lie within 15 of the player), the bands from
     * y0's; mode 0 the bits alone, 1 the ids of set bits (lit ore), 2 the
     * exact read of every cell */
    const uint8_t *bits[27], *ids[27];
    uint8_t mode[27];
    uint8_t lut[256];
    int lut_made = 0, special = 0;
    int y0 = (py - 15 < 0 ? 0 : py - 15) >> 4;
    int inside = px - 15 >= -30000000 && px + 15 < 30000000 && pz - 15 >= -30000000 && pz + 15 < 30000000;

    if (p->fog_world != cs->w)
    {
        for (int k = 0; k < 64; ++k) p->fog_key[k].sec = -1;
        p->fog_win_ok = 0;
        p->fog_world = cs->w;
    }
    /* the window holds while the player's cell and the nine chunks' wseq
     * do (a chunk not loaded is 0: an inserted chunk's wseq is past it) */
    const struct chunk *ch[9];
    int same = p->fog_win_ok && p->fog_win_at[0] == px && p->fog_win_at[1] == py && p->fog_win_at[2] == pz;
    for (int i = 0; i < 9; ++i)
    {
        ch[i] = inside ? world_chunk(cs->w, cs->cx0 + i / 3, cs->cz0 + i % 3) : NULL;
        same &= (ch[i] ? ch[i]->wseq : 0) == p->fog_win_seq[i];
    }
    if (same) FOG_COUNT(fog_same);
    for (int i = 0; !same && i < 3; ++i)
        for (int j = 0; j < 3; ++j)
        {
            const struct chunk *c = ch[i * 3 + j];
            for (int k = 0; k < 3; ++k)
            {
                int b = i * 9 + j * 3 + k, sec = y0 + k;
                const struct chunk_sec *sb = c && sec < 16 && (c->mask >> sec & 1) ? chunk_sec_at(c, sec) : NULL;
                ids[b] = NULL;
                if (c == NULL || sb == NULL || sb->ids_hi)
                {
                    bits[b] = FOG_NONE;
                    mode[b] = c == NULL || sb != NULL ? 2 : 0;
                }
                else
                {
                    int slot = cp_fog_slot(p, c, sec, lut, &lut_made);
                    bits[b] = p->fog_bits[slot];
                    mode[b] = p->fog_key[slot].ore != 0;
                    ids[b] = sb->ids;
                }
                special |= mode[b];
            }
        }
    uint32_t *win = p->fog_win;
    if (!same)
    {
        p->fog_win_ok = !special;
        p->fog_win_at[0] = px;
        p->fog_win_at[1] = py;
        p->fog_win_at[2] = pz;
        for (int i = 0; i < 9; ++i) p->fog_win_seq[i] = ch[i] ? ch[i]->wseq : 0;
    }
    uint64_t s = wr->r.seed;
    if (p->fog_win_ok)
    {
        /* the bits alone answer every cell: the window's words from the
         * bands' 16-bit runs of a column (bit y & 15 of the two bytes at
         * (x & 15) << 5 | (z & 15) << 1), rows outside the world 0, as its
         * reads there are air */
        int sh = py - 15 - y0 * 16;
        FOG_COUNT(fog_wint);
#if defined(__x86_64__) && defined(__GNUC__)
        if (!same && cp_fog_lanes_ok())
        {
            cp_fog_window_lanes(win, bits, cs->cx0, cs->cz0, px, pz, sh);
#ifdef CP_FOG_VERIFY
            /* every word against the loop below's */
            int wbad = 0;
            for (int dx = 0; dx < 31; ++dx)
                for (int dz = 0; dz < 31; ++dz)
                {
                    int x = px - 15 + dx, z = pz - 15 + dz, off = (x & 15) << 5 | (z & 15) << 1;
                    const uint8_t *const *bb = bits + ((x >> 4) - cs->cx0) * 9 + ((z >> 4) - cs->cz0) * 3;
                    uint16_t r0, r1, r2;
                    memcpy(&r0, bb[0] + off, 2);
                    memcpy(&r1, bb[1] + off, 2);
                    memcpy(&r2, bb[2] + off, 2);
                    uint64_t col = (uint64_t)r0 | (uint64_t)r1 << 16 | (uint64_t)r2 << 32;
                    wbad |= win[dx << 5 | dz] != (uint32_t)(sh >= 0 ? col >> sh : col << -sh);
                }
            FOG_COUNT(fog_win_lanes);
            if (wbad) FOG_COUNT(fog_win_bad);
#endif
            same = 1;   /* the loop below has nothing left to make */
        }
#endif
        for (int dx = 0; !same && dx < 31; ++dx)
        {
            int x = px - 15 + dx;
            const uint8_t *const *bi = bits + ((x >> 4) - cs->cx0) * 9;
            for (int dz = 0; dz < 31; ++dz)
            {
                int z = pz - 15 + dz, off = (x & 15) << 5 | (z & 15) << 1;
                const uint8_t *const *bb = bi + ((z >> 4) - cs->cz0) * 3;
                uint16_t r0, r1, r2;
                memcpy(&r0, bb[0] + off, 2);
                memcpy(&r1, bb[1] + off, 2);
                memcpy(&r2, bb[2] + off, 2);
                uint64_t col = (uint64_t)r0 | (uint64_t)r1 << 16 | (uint64_t)r2 << 32;
                win[dx << 5 | dz] = (uint32_t)(sh >= 0 ? col >> sh : col << -sh);
            }
        }
        const uint32_t *wc = win + (15 << 5 | 15);
        if (!void_particles || py - 15 >= 8)
        {
            /* no cell can roll (y >= 8: nextInt(8) is never past it), so
             * a cell at draw k takes 6 or 7 draws and the next cell starts
             * at k + 6 or k + 7, whatever came before: draws 1 to 13 hold
             * this cell and both cells that can follow it, and two cells
             * on, the seed is FOG_AT(12 + the two cells' air) */
            FOG_COUNT(fog_look);
#if defined(__x86_64__) && defined(__GNUC__)
            if (cp_fog_lanes_ok())
            {
                FOG_COUNT(fog_lanes);
                wr->r.seed = cp_fog_lanes(s, win);
                return;
            }
#endif
            /* the seed as t = s << 16 (mod 2^64): the LCG's steps hold in
             * that form with C[k] << 16 (FOG_AC16), and a draw's four bits
             * are the top four, so no step is masked. The table is read
             * through a pointer the compiler cannot see into, so its 28
             * constants are memory operands, not 10-byte immediates (the
             * loop is bound by its instruction count) */
            uint64_t t = s << 16;
            const uint64_t(*ac)[2] = FOG_AC16;
            __asm__("" : "+r"(ac));
#define FOG_T(k) (t * ac[k][0] + ac[k][1])
#define FOG_V(k) ((int)(FOG_T(k) >> 60))
#define FOG_AIR(a, b, c, d, e, f) (~(wc[(FOG_V(a) - FOG_V(b)) * 32 + (FOG_V(e) - FOG_V(f))] >> (FOG_V(c) - FOG_V(d) + 15)) & 1u)
            /* the selects as masks (the air test is a coin flip a branch
             * would mispredict half the time), the two cells that can come
             * second each choosing its seed before the first cell chooses
             * between them: the loop is bound by this chain */
            for (int i = 0; i < 1000; i += 2)
            {
                uint32_t a0 = FOG_AIR(1, 2, 3, 4, 5, 6);
                uint32_t a6 = FOG_AIR(7, 8, 9, 10, 11, 12);
                uint32_t a7 = FOG_AIR(8, 9, 10, 11, 12, 13);
                uint64_t t12 = FOG_T(12), t13 = FOG_T(13), t14 = FOG_T(14);
                uint64_t after6 = a6 ? t13 : t12, after7 = a7 ? t14 : t13;
                t = a0 ? after7 : after6;
            }
#undef FOG_AIR
#undef FOG_V
#undef FOG_T
            wr->r.seed = t >> 16;
            return;
        }
        for (int i = 0; i < 1000; ++i)
        {
            int dx = (int)(FOG_AT(1) >> 44) - (int)(FOG_AT(2) >> 44);
            int dy = (int)(FOG_AT(3) >> 44) - (int)(FOG_AT(4) >> 44);
            int dz = (int)(FOG_AT(5) >> 44) - (int)(FOG_AT(6) >> 44);
            if (!(wc[dx * 32 + dz] >> (dy + 15) & 1))
                s = (int)(FOG_AT(7) >> 45) > py + dy && void_particles ? FOG_AT(10) : FOG_AT(7);
            else
                s = FOG_AT(6);
        }
        wr->r.seed = s;
        return;
    }
    const int bx = cs->cx0 * 9, bz = cs->cz0 * 3;
    for (int i = 0; i < 1000; ++i)
    {
        int x = px + (int)(FOG_AT(1) >> 44) - (int)(FOG_AT(2) >> 44);
        int y = py + (int)(FOG_AT(3) >> 44) - (int)(FOG_AT(4) >> 44);
        int z = pz + (int)(FOG_AT(5) >> 44) - (int)(FOG_AT(6) >> 44);
        int solid = 0, ore = 0;
        if ((unsigned)y < 256u)
        {
            int b = (x >> 4) * 9 - bx + (z >> 4) * 3 - bz + (y >> 4) - y0;
            int ci = SEC_XYZ(x & 15, y, z & 15);
            solid = bits[b][ci >> 3] >> (ci & 7) & 1;
            if (__builtin_expect(mode[b] != 0, 0) && (solid || mode[b] == 2))
            {
                if (mode[b] == 1) ore = ids[b][ci] == 74;
                else
                {
                    int id = chunkscan_block(cs, x, y, z) & 4095;
                    solid = id != 0 && BLOCKS[id].exists && BLOCKS[id].material != 0;
                    ore = id == 74;
                }
            }
        }

        if (!solid)
            /* the roll, nextInt(8), and past y the three nextFloat */
            s = (int)(FOG_AT(7) >> 45) > y && void_particles ? FOG_AT(10) : FOG_AT(7);
        else
            s = ore ? FOG_AT(24) : FOG_AT(6);
    }
    wr->r.seed = s;
}
#undef FOG_AT

#ifdef CP_FOG_VERIFY
static void cp_fog_verify_note(int bad)
{
    ++fog_ticks;
    if (bad) ++fog_bad;
}
__attribute__((destructor)) static void cp_fog_verify_print(void)
{
    fprintf(stderr, "CP_FOG_VERIFY ticks %llu mismatched %llu slot hits %llu misses %llu window %llu lookahead %llu window kept %llu lanes %llu slot lanes %llu differ %llu window lanes %llu differ %llu\n",
            (unsigned long long)fog_ticks, (unsigned long long)fog_bad, (unsigned long long)fog_hit,
            (unsigned long long)fog_miss, (unsigned long long)fog_wint, (unsigned long long)fog_look,
            (unsigned long long)fog_same, (unsigned long long)fog_lanes, (unsigned long long)fog_slot_lanes,
            (unsigned long long)fog_slot_bad, (unsigned long long)fog_win_lanes, (unsigned long long)fog_win_bad);
}
/* the loop before the slots, run beside it on a copy of the seed */
static void cp_void_fog_ref(det_rng *wr, struct chunkscan *cs, int px, int py, int pz, int void_particles)
{
    /* the LCG k steps on as one affine map, s * A[k] + C[k] (mod 2^48), so a
     * cell's six coordinate draws come from its first seed at once rather
     * than one after another */
    uint64_t A[25], C[25];
    A[0] = 1;
    C[0] = 0;
    for (int k = 1; k < 25; ++k)
    {
        A[k] = A[k - 1] * 0x5DEECE66DULL;
        C[k] = C[k - 1] * 0x5DEECE66DULL + 0xBULL;
    }
    /* the cells' bands, looked up once: (x >> 4) - cx0 and (z >> 4) - cz0
     * are 0 to 2 (the cells lie within 15 of the player), the bands from
     * y0's. An entry is a band's ids, 0 for a band that reads air, 1 for a
     * read left to chunkscan_block, whose reads are World.getBlock's (a
     * chunk not loaded, a band with ids past 255, the world's edge near). */
    uintptr_t band[3][3][3];
    int y0 = (py - 15 < 0 ? 0 : py - 15) >> 4;
    int inside = px - 15 >= -30000000 && px + 15 < 30000000 && pz - 15 >= -30000000 && pz + 15 < 30000000;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
        {
            const struct chunk *c = inside ? world_chunk(cs->w, cs->cx0 + i, cs->cz0 + j) : NULL;
            for (int k = 0; k < 3; ++k)
            {
                int sec = y0 + k;
                const struct chunk_sec *b = c && sec < 16 && (c->mask >> sec & 1) ? chunk_sec_at(c, sec) : NULL;
                band[i][j][k] = c == NULL || (b && b->ids_hi) ? 1 : (uintptr_t)(b ? b->ids : NULL);
            }
        }
    uint64_t s = wr->r.seed;
#define FOG_AT(k) ((s * A[k] + C[k]) & JR_MASK)
    for (int i = 0; i < 1000; ++i)
    {
        int x = px + (int)(FOG_AT(1) >> 44) - (int)(FOG_AT(2) >> 44);
        int y = py + (int)(FOG_AT(3) >> 44) - (int)(FOG_AT(4) >> 44);
        int z = pz + (int)(FOG_AT(5) >> 44) - (int)(FOG_AT(6) >> 44);
        int id = 0;
        if ((unsigned)y < 256u)
        {
            uintptr_t b = band[(x >> 4) - cs->cx0][(z >> 4) - cs->cz0][(y >> 4) - y0];
            id = b > 1 ? ((const uint8_t *)b)[SEC_XYZ(x & 15, y, z & 15)] : b ? chunkscan_block(cs, x, y, z) & 4095 : 0;
        }

        if (id == 0 || !BLOCKS[id].exists || BLOCKS[id].material == 0)
            /* the roll, nextInt(8), and past y the three nextFloat */
            s = (int)(FOG_AT(7) >> 45) > y && void_particles ? FOG_AT(10) : FOG_AT(7);
        else
            s = id == 74 ? FOG_AT(24) : FOG_AT(6);
    }
#undef FOG_AT
    wr->r.seed = s;
}
#endif

/* WorldClient.tick's setActivePlayerChunksAndCheckLight (the one player's
 * pick and its light-check cell) and Minecraft.runTick's
 * doVoidFogParticles around the moved player: a thousand cells within 15,
 * each air cell's depthsuspend roll (below y 8 in a world with void
 * particles, the overworld, its place on the world's Random and an
 * EntityAuraFX within 16 at particles 0), each other cell's
 * randomDisplayTick on a fresh Random (particles_live_display_tick). The
 * world's Random unknown, only the fresh Random's seeder long is spent. */
static void cp_world_tick(struct client_player *p)
{
    if (!p->cw_rand_known || p->e.world == NULL)
    {
        (void)det_seeder_next_long(surv.det, DET_CLIENT);
        /* a light check at a cell nobody knows: the client world's light
         * is no longer the oracle's for certain */
        if (p->e.world != NULL) p->cw_check_missed = 1;
        return;
    }
    det_rng *wr = &p->cw_rand;
    /* playerCheckLight's cell: the one player, then 11 by 11 by 11 around
     * it; func_147451_t runs at it in session.c's client world tick, after
     * the chunk cache's recheckGaps, as WorldClient.tick orders them */
    (void)det_rng_int_n(wr, 1);
    p->cw_check_x = mh_floor(p->e.pos_x) + det_rng_int_n(wr, 11) - 5;
    p->cw_check_y = mh_floor(p->e.pos_y) + det_rng_int_n(wr, 11) - 5;
    p->cw_check_z = mh_floor(p->e.pos_z) + det_rng_int_n(wr, 11) - 5;
    p->cw_check_pending = 1;

    struct particles_live *pl = surv_client_fx(p);
    pl->rain = p->cw_rain;
    struct world *w = p->e.world;
    int void_particles = w->dim == 0;
    det_rng var5 = det_new_random_role(surv.det, DET_CLIENT);
    int px = (int)floor(p->e.pos_x), py = (int)floor(p->e.pos_y), pz = (int)floor(p->e.pos_z);
    /* the cells lie within 15 of the player: the chunks from
     * ((px - 15) >> 4, (pz - 15) >> 4) */
    struct chunkscan cs;
    chunkscan_init(&cs, w, (px - 15) >> 4, (pz - 15) >> 4);

    if (pl->setting >= 2 && pl->on_spawn == NULL)
    {
#ifdef CP_FOG_VERIFY
        det_rng ref = *wr;
        struct chunkscan cs_ref = cs;
        cp_void_fog_ref(&ref, &cs_ref, px, py, pz, void_particles);
        cp_void_fog_minimal(p, wr, &cs, px, py, pz, void_particles);
        cp_fog_verify_note(ref.r.seed != wr->r.seed);
#else
        cp_void_fog_minimal(p, wr, &cs, px, py, pz, void_particles);
#endif
        return;
    }
    for (int i = 0; i < 1000; ++i)
    {
        int x = px + det_rng_int_n(wr, 16);
        x -= det_rng_int_n(wr, 16);
        int y = py + det_rng_int_n(wr, 16);
        y -= det_rng_int_n(wr, 16);
        int z = pz + det_rng_int_n(wr, 16);
        z -= det_rng_int_n(wr, 16);
        int id = y >= 0 && y < 256 ? chunkscan_block(&cs, x, y, z) & 4095 : 0;

        if (!BLOCKS[id].exists || BLOCKS[id].material == 0)
        {
            if (det_rng_int_n(wr, 8) > y && void_particles)
            {
                double sx = (double)((float)x + det_rng_float(wr));
                double sy = (double)((float)y + det_rng_float(wr));
                double sz = (double)((float)z + det_rng_float(wr));
                (void)particles_live_spawn(pl, "depthsuspend", sx, sy, sz, 0.0, 0.0, 0.0);
            }
        }
        else if (particles_live_display_acts(id)) particles_live_display_tick(pl, id, x, y, z, &var5);
    }
}

void client_player_tick(struct client_player *p, const struct act *a, struct client_out *out)
{
    int ridden = 0;
    p->client_tick_index++;
    /* the move's portal cells reach the player through its entity (bound
     * each tick: a copied player keeps its own) */
    p->e.self = p;
    p->e.set_in_portal = cp_set_in_portal;
    surv_client_bind_walking(p);

    memset(out, 0, offsetof(struct client_out, npkt));
    out->npkt = 0;
    out->c09_slot = -1;

    if (p->pending_s20 && !p->game_paused)
    {
        /* NetHandlerPlayClient.handleEntityProperties: the packet's base and
         * its modifiers, the server's own set (the sprint boost as the
         * server player sprints, the potions') */
        p->mod_sprint = p->pending_s20_mod;
        p->mod_slow = p->pending_s20_slow;
        p->mod_speed = p->pending_s20_speed;
        p->move_speed = player_move_speed(p->mod_sprint, p->mod_slow, p->mod_speed);
        p->pending_s20 = 0;
    }

    client_player_pick(p, a);

    /* --- Minecraft.runTick, the parts that touch the player ---
     *
     * updateController (syncCurrentPlayItem and the s2c pump) runs first, then
     * the screens, the gui ops and the input consumers. The using slowdown and
     * the sprint gates hook into the living update below. */
    surv_client_tick_start(p, a, out);
    if (p->update_renderer && !p->game_paused) p->update_renderer(p);
    else if (!p->game_paused && !env_tick_fast()) cp_renderer_draws(p);
    /* updateRenderer's rendererUpdateCount and addRainParticles, the
     * render view (the player) where the last tick left it */
    if (!p->game_paused && surv.det && p->e.world != NULL)
    {
        ++p->renderer_update_count;
        struct particles_live *pl = surv_client_fx(p);
        int rain_sound = 0;
        if (!env_tick_fast())
            particles_live_rain(pl, p->cw_rain, pl->fancy, (int)p->renderer_update_count, &rain_sound);
    }

    /* --- WorldClient.updateEntities -> the client player ---
     *
     * EntityClientPlayerMP.onUpdate gates the whole tick on
     * worldObj.blockExists(floor(posX), 0, floor(posZ)). The client world is
     * empty at every snapshot (clientworld.nbt "chunks" i:0: the terrain
     * packets the server sent in this very pair land on the next tick), so
     * the player's first replayed tick does not run; from the second tick on
     * the spawn area is client-loaded and stays so on these tapes. The
     * client's blockExists is always true (ChunkProviderClient.chunkExists),
     * so a player over a chunk not sent yet still updates, falling through
     * the blank chunk's air at the fixed -0.1 of an unloaded chunk. */
    if (p->game_paused)
    {
        /* isGamePaused: no world update, the player included */
    }
    else if (!p->is_dead && p->first_client_tick == 2)
    {
        /* setDimensionAndSendPlayer's fresh player: the first tick sends no
         * living update (cp.my stays 0, the position unchanged) and no C03:
         * the S08's C06 alone flips the server's hasMoved (a bed respawn
         * above the floor shows it: the server's motion takes one fall
         * step on this row, not two). The fresh old_pos stays 0, so the
         * next tick's C03 is a position packet. */
        p->first_client_tick = 1;
    }
    else if (!p->is_dead && p->first_client_tick == 0)
    {
        p->first_client_tick = 1;
    }
    else if (!p->is_dead)
    {
        ++p->ticks_existed;

        /* the rider is skipped here: its vehicle's updateEntity runs it,
         * after the entities (ride_client_update_ridden below); a vehicle
         * this tick's packets destroy has let go of it already */
        if (!p->game_paused) combat_client_vehicle_gone(p, ride_client_vehicle_id(p));
        if (ride_client_riding(p)) ridden = 1;
        else
        {
            surv_client_eat_tick(p);
            sleep_client_update(p);
            cp_on_entity_update(p);
            cp_on_living_update(p);
            cp_living_tail(p);

            cp_send_motion_updates(p, &out->c03, out->c0b, &out->nc0b);
            out->has_c03 = 1;
        }
    }
    if (!p->game_paused) combat_client_tick(p);
    if (ridden) ride_client_update_ridden(p, out);
    if (!p->game_paused && !env_tick_fast()) pickobj_client_te_tick(p);

    /* WorldClient.tick's and doVoidFogParticles' draws */
    if (!p->game_paused && surv.det && !env_tick_fast()) cp_world_tick(p);

    /* runTick's "particles" section: EffectRenderer.updateEffects, after
     * updateEntities (the player's tick above). A world switch (the
     * respawn's loadWorld) cleared the list: clearEffects */
    if (surv_fx.world != p->e.world)
    {
        surv_fx.world = p->e.world;
        surv_fx.n = 0;
    }
    /* fast: nothing in the pipeline draws the list (play does), so it is emptied rather than left untimed: a
     * list never ticked filled to its layer's cap and fx_add scanned and shifted all of it on every spawn
     * (memmove 10.7% of the process late in an episode, fast 13% slower than full there; lane/vecticks) */
    if (!env_tick_fast()) particles_live_tick(&surv_fx);
    else surv_fx.n = 0;

    /* the frame's end: isGamePaused for the next runTick and the server's
     * next tick */
    p->game_paused = p->screen_credits;
    p->picked = 0;   /* the next row picks anew */
}

/* ----------------------------------------------------------- the server tick */

/* EntityLivingBase.updateFallState is empty on EntityPlayerMP; the handler
 * calls handleFalling instead, after the move. */
static void sp_tick_empty_fall(struct entity *e, double dy, int on_ground)
{
    (void)e;
    (void)dy;
    (void)on_ground;
}

/* EntityPlayerMP.onUpdateEntity's living half, as processPlayer runs it. */
static void sp_on_update_entity(struct server_player *p)
{
    struct world *w = p->e.world;
    struct entity *e = &p->e;
    struct collide_list *scratch COLLIDE_SCRATCH = collide_scratch_begin();

    /* Entity.ridingEntity for moveEntity's walking block */
    e->riding = p->ridingh != 0;

    /* this.playerEntity.onUpdateEntity(): the full living tick at the old
     * position, with no input (the server player's moveStrafing, moveForward
     * and isJumping stay 0: C0C only arrives while riding). EntityPlayer.onUpdate
     * runs first: the itemInUse countdown and the cooldown ticks, then the
     * Entity.onUpdate base tick's survival half. */
    surv_server_eat_tick(p);
    sleep_server_update(p);
    /* Entity.onEntityUpdate's head: prevPos, then the portal countdown,
     * which can carry the player into another world; the rest of the update
     * runs there */
    if (p->on_entity_update)
    {
        p->on_entity_update(p->on_entity_ctx);
        w = e->world;
    }
    surv_server_base_tick(p);

    /* EntityLivingBase.onUpdate's equipment check: the held stack's attribute
     * modifier follows the current item */
    {
        int held = p->sv.current_item;
        p->held_attr_item = held >= 0 && held < 9 && p->sv.inv[held].count > 0 ? p->sv.inv[held].item : -1;
    }
    surv_server_combat_tick(p);
    /* EntityPlayer.onLivingUpdate's head */
    surv_server_peaceful_heal(p);
    {
        /* EntityLivingBase.onLivingUpdate */

        if (p->jump_ticks > 0) --p->jump_ticks;

        if (fabs(e->motion_x) < 0.005) e->motion_x = 0.0;
        if (fabs(e->motion_y) < 0.005) e->motion_y = 0.0;
        if (fabs(e->motion_z) < 0.005) e->motion_z = 0.0;

        /* updateEntityActionState (EntityPlayer): nothing to do. The server
         * player's moveStrafing, moveForward and isJumping are the C0C's
         * (setEntityActionState, only while riding; 0 otherwise). The dead
         * or sleeping player's isMovementBlocked zeroes them. */
        if (p->sv.health <= 0.0F || p->sv.sleeping)
        {
            p->is_jumping = 0;
            p->move_strafing = 0.0F;
            p->move_forward = 0.0F;
        }

        if (p->is_jumping)
        {
            if (e->in_water ||
                world_is_material_in_bb(w, aabb_expand(e->bounding_box, -0.10000000149011612,
                                                       -0.4000000059604645, -0.10000000149011612), MAT_LAVA))
                e->motion_y += 0.03999999910593033;
            else if (e->on_ground && p->jump_ticks == 0)
            {
                jump_common(e, p->sprinting, p->rotation_yaw);
                surv_server_jump(p);
                p->jump_ticks = 10;
            }
        }
        else
        {
            p->jump_ticks = 0;
        }

        p->move_strafing *= 0.98F;
        p->move_forward *= 0.98F;
        float strafe = p->move_strafing, forward = p->move_forward;
        double trav_x = e->pos_x, trav_y = e->pos_y, trav_z = e->pos_z;

        if (e->in_water)
        {
            double var8 = e->pos_y;
            move_flying((double)p->rotation_yaw, &e->motion_x, &e->motion_z, strafe, forward, 0.02F);
            entity_move_ex(e, e->motion_x, e->motion_y, e->motion_z, p->sneaking, sp_tick_empty_fall);
            /* an End portal inside the move took the player to another world:
             * the rest reads this.worldObj, the new one */
            w = e->world;
            e->motion_x *= 0.800000011920929;
            e->motion_y *= 0.800000011920929;
            e->motion_z *= 0.800000011920929;
            e->motion_y -= 0.02;

            if (e->is_collided_horizontally &&
                is_offset_position_in_liquid(w, e->bounding_box, e->motion_x,
                                             e->motion_y + 0.6000000238418579 - e->pos_y + var8, e->motion_z,
                                             scratch))
            {
                e->motion_y = 0.30000001192092896;
            }
        }
        else if (world_is_material_in_bb(w, aabb_expand(e->bounding_box, -0.10000000149011612,
                                                        -0.4000000059604645, -0.10000000149011612), MAT_LAVA))
        {
            double var8 = e->pos_y;
            move_flying((double)p->rotation_yaw, &e->motion_x, &e->motion_z, strafe, forward, 0.02F);
            entity_move_ex(e, e->motion_x, e->motion_y, e->motion_z, p->sneaking, sp_tick_empty_fall);
            w = e->world;
            e->motion_x *= 0.5;
            e->motion_y *= 0.5;
            e->motion_z *= 0.5;
            e->motion_y -= 0.02;

            if (e->is_collided_horizontally &&
                is_offset_position_in_liquid(w, e->bounding_box, e->motion_x,
                                             e->motion_y + 0.6000000238418579 - e->pos_y + var8, e->motion_z,
                                             scratch))
            {
                e->motion_y = 0.30000001192092896;
            }
        }
        else
        {
            float var3f = 0.91F;

            if (e->on_ground)
            {
                float slip = neth_env->cfg.player_no_ground_friction ? 1.0F
                    : BLOCKS[world_get_block(w, mh_floor(e->pos_x), mh_floor(e->bounding_box.min_y) - 1,
                                             mh_floor(e->pos_z)) & 4095].slipperiness;
                var3f = slip * 0.91F;
            }

            float var4 = 0.16277136F / (var3f * var3f * var3f);
            float var5f;

            /* getAIMoveSpeed: the attribute with the modifiers it holds
             * now (a potion drunk earlier in this update) */
            sp_sync_speed(p);
            if (e->on_ground) var5f = (float)p->move_speed * var4;
            else var5f = p->jump_movement_factor;

            move_flying((double)p->rotation_yaw, &e->motion_x, &e->motion_z, strafe, forward, var5f);
            var3f = 0.91F;

            if (e->on_ground)
            {
                float slip = neth_env->cfg.player_no_ground_friction ? 1.0F
                    : BLOCKS[world_get_block(w, mh_floor(e->pos_x), mh_floor(e->bounding_box.min_y) - 1,
                                             mh_floor(e->pos_z)) & 4095].slipperiness;
                var3f = slip * 0.91F;
            }

            if (is_on_ladder(w, e->pos_x, e->pos_z, e->bounding_box))
            {
                float var6 = 0.15F;

                if (e->motion_x < (double)(-var6)) e->motion_x = (double)(-var6);
                if (e->motion_x > (double)var6) e->motion_x = (double)var6;
                if (e->motion_z < (double)(-var6)) e->motion_z = (double)(-var6);
                if (e->motion_z > (double)var6) e->motion_z = (double)var6;

                e->fall_distance = 0.0F;

                if (e->motion_y < -0.15) e->motion_y = -0.15;

                if (p->sneaking && e->motion_y < 0.0) e->motion_y = 0.0;
            }

            entity_move_ex(e, e->motion_x, e->motion_y, e->motion_z, p->sneaking, sp_tick_empty_fall);
            w = e->world;

            if (e->is_collided_horizontally && is_on_ladder(w, e->pos_x, e->pos_z, e->bounding_box))
                e->motion_y = 0.2;

            /* the server half of the unloaded-chunk check: blockExists and the
             * chunk load both run; near spawn the chunk is loaded, so the
             * gravity path runs */
            e->motion_y -= 0.08;
            e->motion_y *= 0.9800000190734863;
            e->motion_x *= (double)var3f;
            e->motion_z *= (double)var3f;
        }

        p->jump_movement_factor = 0.02F;

        if (p->sprinting)
            p->jump_movement_factor = (float)((double)p->jump_movement_factor + (double)0.02F * 0.3);

        /* EntityPlayer.moveEntityWithHeading's tail: the movement stat over the
         * distance the travel actually moved (addMovementStat: not while riding) */
        if (p->ridingh == 0)
            surv_server_move_stat(p, e->pos_x - trav_x, e->pos_y - trav_y, e->pos_z - trav_z);
    }

    serverreplay_player_collide(p->replay);

    /* EntityPlayer.onLivingUpdate's tail: the pickup query, then the food
     * tick and the S06/S1F gates */
    surv_server_living_tail(p);
    /* EntityPlayer.onUpdate after super.onUpdate: the open window's
     * canInteractWith again (a villager whose trade ended in the entity
     * pass after the player's own update closes the window now) */
    surv_server_container_check(p);
    surv_server_food_and_gate(p);
}

/* processPlayer's hasMoved-false tail: the % 20 keepalive correction. On the
 * counter's multiples the handler re-sends the last position
 * (setPlayerLocation: hasMoved stays false, the player is set there, an S08
 * rides the s2c). The counter's phase is a snapshot constant, so it fires at
 * tape rows (snapshot tick + 1 + k) % 20 == 0. */
static void sp_stale_resend(struct server_player *p)
{
    struct entity *e = &p->e;

    if (p->network_tick_count < 0 || p->network_tick_count % 20 != 0) return;

    entity_set_pos_rot(e, p->last_pos_x, p->last_pos_y, p->last_pos_z,
                       &p->rotation_yaw, &p->rotation_pitch,
                       &p->prev_rotation_yaw, &p->prev_rotation_pitch,
                       p->rotation_yaw, p->rotation_pitch);

    struct s2c_pkt *pkt8 = s2c_add(s2c_out());
    pkt8->kind = PK_S08;
    pkt8->f0 = p->last_pos_x;
    pkt8->f1 = (double)p->last_pos_y + 1.6200000047683716;
    pkt8->f2 = p->last_pos_z;
    pkt8->f3 = p->rotation_yaw;
    pkt8->f4 = p->rotation_pitch;
}

static void sp_process_player_body(struct server_player *p, const struct c03 *pkt);

/* processPlayer. A mob in another world that still targets the player reads
 * the one EntityPlayerMP where the handler left it (a transfer's
 * onUpdateEntity moves it in the new world, then the handler puts it back at
 * the portal), so the replay's copy follows once the handler is done. */
static void sp_process_player(struct server_player *p, const struct c03 *pkt)
{
    sp_process_player_body(p, pkt);
    if (p->replay != NULL) serverreplay_player_moved(p->replay);
}

/* processPlayer, the moved case. */
static void sp_process_player_body(struct server_player *p, const struct c03 *pkt)
{
    struct world *w = p->e.world;
    struct entity *e = &p->e;

    /* playerConqueredTheEnd: the handler ignores every move until the
     * respawn */
    if (p->conquered_end) return;

    if (!p->has_moved)
    {
        double var3 = pkt->d - p->last_pos_y;

        if (pkt->c == p->last_pos_x && var3 * var3 < 0.01 && pkt->e == p->last_pos_z)
            p->has_moved = 1;
    }

    if (!p->has_moved)
    {
        sp_stale_resend(p);
        return;
    }

    /* the riding player: its vehicle places it; the packet brings the look
     * and onGround, the living update runs where the vehicle put it, then
     * World.updateEntity runs its updateRidden (riding.c) */
    if (p->ridingh != 0)
    {
        float var34 = p->rotation_yaw;
        float var4 = p->rotation_pitch;
        ride_server_rider_position(p);
        double rx = e->pos_x, ry = e->pos_y, rz = e->pos_z;

        if (pkt->kind == C03_LOOK || pkt->kind == C03_POSLOOK)
        {
            var34 = pkt->yaw;
            var4 = pkt->pitch;
        }

        e->on_ground = pkt->on_ground;
        sp_on_update_entity(p);
        e->y_size = 0.0F;
        entity_set_pos_rot(e, rx, ry, rz, &p->rotation_yaw, &p->rotation_pitch,
                           &p->prev_rotation_yaw, &p->prev_rotation_pitch, var34, var4);
        if (p->ridingh != 0) ride_server_rider_position(p);
        /* serverUpdateMountedMovingPlayer, here: the updateEntity below
         * sends the chunks it queues (and starts watching what stands in
         * them) in this same network tick */
        p->pertinent_update = 1;
        if (p->replay != NULL) cl_c03(serverreplay_player_manager(p->replay), p->server_tick, e->pos_x, e->pos_z);
        if (p->has_moved)
        {
            p->last_pos_x = e->pos_x;
            p->last_pos_y = e->pos_y;
            p->last_pos_z = e->pos_z;
        }
        /* var2.updateEntity (the network tick's queue stays open) */
        ride_server_update_entity(p);
        return;
    }

    /* the sleeping player: the update at the bed, then back to the handler's
     * last position (the packet's own position is ignored), then
     * worldserver.updateEntity(player): a second EntityPlayerMP.onUpdate this
     * tick (the respawn and hurt timers count down twice while asleep) */
    if (p->sv.sleeping)
    {
        sp_on_update_entity(p);
        entity_set_pos_rot(e, p->last_pos_x, p->last_pos_y, p->last_pos_z, &p->rotation_yaw,
                           &p->rotation_pitch, &p->prev_rotation_yaw, &p->prev_rotation_pitch,
                           p->rotation_yaw, p->rotation_pitch);
        ride_server_update_entity(p);
        return;
    }

    double var3 = e->pos_y;
    p->last_pos_x = e->pos_x;
    p->last_pos_y = e->pos_y;
    p->last_pos_z = e->pos_z;
    double var5 = e->pos_x;
    double var7 = e->pos_y;
    double var9 = e->pos_z;
    float var11 = p->rotation_yaw;
    float var12 = p->rotation_pitch;

    int has_pos = pkt->kind == C03_POS || pkt->kind == C03_POSLOOK;
    int has_look = pkt->kind == C03_LOOK || pkt->kind == C03_POSLOOK;

    if (has_pos && pkt->d == -999.0 && pkt->f == -999.0) has_pos = 0;

    double var13;

    if (has_pos)
    {
        var5 = pkt->c;
        var7 = pkt->d;
        var9 = pkt->e;
        var13 = pkt->f - pkt->d;

        if (var13 > 1.65 || var13 < 0.1)
        {
            /* illegal stance: a kick these tapes never produce (the C04 and
             * C06 stances are posY - minY = the eye offset, about 1.62) */
        }
    }

    if (has_look)
    {
        var11 = pkt->yaw;
        var12 = pkt->pitch;
    }

    sp_on_update_entity(p);

    e->y_size = 0.0F;
    entity_set_pos_rot(e, p->last_pos_x, p->last_pos_y, p->last_pos_z, &p->rotation_yaw, &p->rotation_pitch,
                       &p->prev_rotation_yaw, &p->prev_rotation_pitch, var11, var12);

    /* a transfer's setPlayerLocation cleared hasMoved */
    if (!p->has_moved) return;

    var13 = var5 - e->pos_x;
    double var15 = var7 - e->pos_y;
    double var17 = var9 - e->pos_z;
    double var19 = fmin(fabs(var13), fabs(e->motion_x));
    double var21 = fmin(fabs(var15), fabs(e->motion_y));
    double var23 = fmin(fabs(var17), fabs(e->motion_z));
    double var25 = var19 * var19 + var21 * var21 + var23 * var23;

    /* var25 > 100 kicks out of single player only: getServerOwner() is the
     * single player's own name, so the branch never fires */

    float var27 = 0.0625F;
    int var28 = world_colliding_boxes_empty(w, aabb_expand(e->bounding_box, -var27, -var27, -var27));

    if (e->on_ground && !pkt->on_ground && var15 > 0.0)
    {
        jump_common(e, p->sprinting, p->rotation_yaw);
        surv_server_jump(p);
    }

    e->riding = p->ridingh != 0;
    entity_move_ex(e, var13, var15, var17, p->sneaking, sp_tick_empty_fall);
    e->on_ground = pkt->on_ground;
    /* EntityPlayer.addMovementStat: the walking, sprinting and swimming
     * exhaustion */
    surv_server_move_stat(p, var13, var15, var17);

    double var29 = var15;
    var13 = var5 - e->pos_x;
    var15 = var7 - e->pos_y;

    /* vanilla's always-true test (NetHandlerPlayServer:353): only NaN skips it */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wtautological-overlap-compare"
#endif
    if (var15 > -0.5 || var15 < 0.5) var15 = 0.0;
#ifdef __clang__
#pragma clang diagnostic pop
#endif

    var17 = var9 - e->pos_z;
    var25 = var13 * var13 + var15 * var15 + var17 * var17;
    int var31 = 0;

    if (var25 > 0.0625) var31 = 1;

    entity_set_pos_rot(e, var5, var7, var9, &p->rotation_yaw, &p->rotation_pitch,
                       &p->prev_rotation_yaw, &p->prev_rotation_pitch, var11, var12);
    int var32 = world_colliding_boxes_empty(w, aabb_expand(e->bounding_box, -var27, -var27, -var27));

    if (var28 && (var31 || !var32))
    {
        /* setPlayerLocation: the S08 correction */
        p->has_moved = 0;
        p->last_pos_x = p->last_pos_x;
        p->last_pos_y = p->last_pos_y;
        p->last_pos_z = p->last_pos_z;
        entity_set_pos_rot(e, p->last_pos_x, p->last_pos_y, p->last_pos_z, &p->rotation_yaw,
                           &p->rotation_pitch, &p->prev_rotation_yaw, &p->prev_rotation_pitch, var11, var12);
        p->sent_s08 = 1;
        {
            struct s2c_pkt *pkt8 = s2c_add(s2c_out());
            pkt8->kind = PK_S08;
            pkt8->f0 = p->last_pos_x;
            pkt8->f1 = (double)p->last_pos_y + 1.6200000047683716;
            pkt8->f2 = p->last_pos_z;
            pkt8->f3 = var11;
            pkt8->f4 = var12;
        }
        return;
    }

    struct aabb var33 = aabb_add_coord(aabb_expand(e->bounding_box, var27, var27, var27), 0.0, -0.55, 0.0);

    if (!world_check_block_collision(w, var33))
    {
        if (var29 >= -0.03125)
        {
            ++p->floating_tick_count;

            if (p->floating_tick_count > 80)
            {
                /* kicked for floating: never reached on these tapes */
            }
        }
    }
    else
    {
        p->floating_tick_count = 0;
    }

    e->on_ground = pkt->on_ground;
    /* serverUpdateMountedMovingPlayer: the caller moves the player manager
     * (cl_c03) when a row's processPlayer got this far */
    p->pertinent_update = 1;
    /* handleFalling(posY - var3, packet onGround) */
    {
        double dy = e->pos_y - var3;
        int og = pkt->on_ground;

        if (!e->in_water && sp_handle_water_movement(e, &p->sv.erand))
            e->fire = 0; /* Entity.handleWaterMovement's water entry */

        if (og)
        {
            if (e->fall_distance > 0.0F)
            {
                /* EntityLivingBase.updateFallState's onFallenUpon: farmland
                 * and crops only, then EntityPlayer.fall. */
                surv_server_fall(p, e->fall_distance);
                e->fall_distance = 0.0F;
            }
        }
        else if (dy < 0.0)
        {
            /* Entity.updateFallState's else-if binds to onGround, so a grounded
             * player never accumulates: the flat form accumulated here. */
            e->fall_distance = (float)((double)e->fall_distance - dy);
        }
    }
}

/* The player's data watcher values the tracker's hasChanges sees: the
 * flags (0: burning, sneaking, sprinting, eating), air (1), health (6), the
 * potion colour and ambience (7, 8), the arrows (9), absorption (17) and
 * the score (18). */
static void sp_watched(const struct server_player *p, const struct living *tw, uint32_t w[8])
{
    const struct surv_state *sv = &p->sv;
    w[0] = (uint32_t)((sv->fire > 0) | (p->sneaking != 0) << 1 | (p->sprinting != 0) << 3 | (sv->using_count > 0) << 4);
    w[1] = (uint32_t)sv->air;
    memcpy(&w[2], &sv->health, 4);
    w[3] = tw ? (uint32_t)tw->potion_liquid_color : 0;
    w[4] = tw ? tw->potion_is_ambient : 0;
    w[5] = tw ? (uint32_t)tw->arrow_count_in_entity : 0;
    memcpy(&w[6], &sv->absorption, 4);
    w[7] = (uint32_t)sv->score;
}

/* The integrated server hands its packets to the client as objects, and an
 * S1C holds the player's live WatchableObjects: the client's pump reads the
 * health, the flag byte and the absorption as the whole server tick left
 * them (a fall processPlayer applies after the tracker's pass reaches the
 * client through the pass's S1C, a tick before its S06). */
void server_player_watch_live(struct server_player *p)
{
    const struct s2c_queue *peek = s2c_out_peek();
    int any = 0;
    for (int i = 0; i < peek->n; ++i)
        any |= peek->q[i].kind == PK_S1C || peek->q[i].kind == PK_S1C_FLAGS || peek->q[i].kind == PK_S1C_ABS;
    if (!any) return;

    uint32_t w[8];
    sp_watched(p, NULL, w);
    struct s2c_queue *q = s2c_out();
    for (int i = 0; i < q->n; ++i)
    {
        if (q->q[i].kind == PK_S1C) q->q[i].f0 = p->sv.health;
        else if (q->q[i].kind == PK_S1C_FLAGS) q->q[i].i0 = (int)w[0];
        else if (q->q[i].kind == PK_S1C_ABS) q->q[i].f0 = p->sv.absorption;
    }
}

/* A confirming packet's serverUpdateMountedMovingPlayer, run between the
 * row's packets: processPlayer ends in it for every packet that got past
 * hasMoved, so the S08's confirming C06 moves the player manager (and loads
 * the chunks under a teleport) before the row's own C03 runs onUpdateEntity
 * there. The last C03's move is the caller's (N2); cl_c03 does nothing for
 * a position under 8 blocks from the last. */
static void sp_manager_move(struct server_player *p)
{
    if (p->pertinent_update && p->replay != NULL)
        cl_c03(serverreplay_player_manager(p->replay), p->server_tick, p->e.pos_x, p->e.pos_z);
}

void server_player_process_confirms(struct server_player *p, const struct client_out *co)
{
    for (int i = 0; i < co->has_confirm_c03; ++i)
    {
        sp_process_player(p, &co->confirm_c03[i]);
        sp_manager_move(p);
    }
    if (co->has_confirm2_c03)
    {
        sp_process_player(p, &co->confirm2_c03);
        sp_manager_move(p);
    }
}

void server_player_tick(struct server_player *p, const struct client_out *co, const struct act *act,
                        int server_tick)
{
    p->server_tick = server_tick;
    p->ns0d = 0;
    p->sent_s08 = 0;
    p->pertinent_update = 0;
    /* the tracker's pass below belongs to the world tick: it reads the sprint
     * flag and the attribute dirt as the previous row's packets left them
     * (a sprint attack's setSprinting(false) in this row's C02 waits for the
     * next pass) */
    int pass_dirty = p->attr_dirty, pass_sprinting = p->sprinting;
    p->attr_dirty = 0;
    surv_server_tick_start(p, co, act);
    int net_dirty = p->attr_dirty, net_sprinting = p->sprinting;
    p->attr_dirty = pass_dirty;
    p->sprinting = pass_sprinting;

    /* the tracker's per-tick pass runs during the world tick, before the
     * network tick processes this row's packets, so it flushes the dirt the
     * previous row's C0B left and reads the flag as of now. The player's own
     * entry (updateFrequency 2) runs func_111190_b on an even tick or when
     * its data watcher changed since the last pass; the S20 there carries
     * every dirty watched instance with all its modifiers. A C0B's
     * setSprinting also flips data watcher 0, so its dirt always leaves; a
     * potion's (the twin's addPotionEffect or its end) waits for the pass. */
    {
        struct living *tw = p->replay && p->replay->player_livh ? lv_get(p->replay->player_livh) : NULL;
        if (tw)
        {
            sp_sync_speed(p);
        }
        uint32_t w[8];
        sp_watched(p, tw, w);
        int changed = !p->trk_w_valid || memcmp(w, p->trk_w, sizeof w) != 0;
        int pass = p->trk_ticks % 2 == 0 || changed;
        /* a dead player out of the world has no entry: its watcher's dirt
         * waits (the respawn's new player starts its own) */
        if (p->sv.removed) pass = 0;
        /* a player in no world's tracker (surv_player_untracked): no
         * pass, and the dirt waits for its next entry */
        if (surv_player_untracked(p))
        {
            p->s20_queued = 0;
            pass = 0;
        }
        else if (p->attr_dirty || (tw && tw->attr_watch_dirty && pass))
        {
            p->s20_queued = 1;
            p->s20_mod = p->sprinting;
            p->s20_slow = p->pot_slow;
            p->s20_speed = p->pot_speed;
            p->attr_dirty = 0;
            if (tw) tw->attr_watch_dirty = 0;
        }
        else
        {
            p->s20_queued = 0;
        }
        if (pass)
        {
            /* func_151261_b's S1C to the player itself: the flag byte when
             * it changed (what the client's isBurning reads) */
            if (p->trk_w_valid && w[0] != p->trk_w[0])
            {
                struct s2c_pkt *pkt = s2c_add(s2c_out());
                pkt->kind = PK_S1C_FLAGS;
                pkt->i0 = (int)w[0];
            }
            /* and the health (6), unless the entity pass's own damage
             * already queued its S1C this tick (the fire, the pearl, the
             * mount's fall, a dev op) */
            if (p->trk_w_valid && w[2] != p->trk_w[2])
            {
                const struct s2c_queue *q = s2c_out();
                int queued = 0;
                for (int i = 0; i < q->n; ++i) queued |= q->q[i].kind == PK_S1C;
                if (!queued)
                {
                    struct s2c_pkt *pkt = s2c_add(s2c_out());
                    pkt->kind = PK_S1C;
                    pkt->f0 = p->sv.health;
                }
            }
            /* and the potion colour and ambience (7, 8), which the client's
             * updatePotionEffects reads for its mobSpell particles */
            if (p->trk_w_valid && (w[3] != p->trk_w[3] || w[4] != p->trk_w[4]))
            {
                struct s2c_pkt *pkt = s2c_add(s2c_out());
                pkt->kind = PK_S1C_POT;
                pkt->i0 = (int)w[3];
                pkt->i1 = (int)w[4];
            }
            /* and the absorption (17), which the client's own HUD draws */
            if (p->trk_w_valid && w[6] != p->trk_w[6])
            {
                struct s2c_pkt *pkt = s2c_add(s2c_out());
                pkt->kind = PK_S1C_ABS;
                pkt->f0 = p->sv.absorption;
            }
            memcpy(p->trk_w, w, sizeof w);
            p->trk_w_valid = 1;
        }
        ++p->trk_ticks;
    }
    p->attr_dirty |= net_dirty;
    p->sprinting = net_sprinting;

    /* processEntityAction 3: wakeUpPlayer(false, true, true), then
     * hasMoved false (setPlayerLocation's own) */
    if (co->c0b_wake) sleep_server_wake(p, 0, 1, 1);
    for (int i = 0; i < co->nc0b; ++i)
    {
        if (co->c0b[i] == C0B_SNEAK_ON) p->sneaking = 1;
        else if (co->c0b[i] == C0B_SNEAK_OFF) p->sneaking = 0;
        else if (co->c0b[i] == C0B_SPRINT_ON) sp_set_sprinting(p, 1);
        else if (co->c0b[i] == C0B_SPRINT_OFF) sp_set_sprinting(p, 0);
    }

    if (co->has_c03 && co->c03.kind) sp_process_player(p, &co->c03);
    /* processInput: the riding client's C0C follows its C05 */
    if (co->has_c0c) ride_server_process_input(p, co->c0c_strafe, co->c0c_forward, co->c0c_jump, co->c0c_sneak);

    if (p->network_tick_count >= 0) ++p->network_tick_count;
    p->net_phase = 0;
}
