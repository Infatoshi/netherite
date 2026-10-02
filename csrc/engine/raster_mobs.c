#define _POSIX_C_SOURCE 200809L
#include "raster_mobs.h"
#include "raster_obs.h"
#include "raster.h"
#include "texanim.h"
#include "jmath.h"
#include "smath.h"
#include "render_blocks.h"
#include "renderstate.h"
#include "tape.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846f
#define MAX_PARTS 16
#define MAX_BOXES 3

struct mob_box
{
    float x, y, z, grow;
    int dx, dy, dz, u, v, mirror;
};
struct mob_part
{
    float x, y, z, ax, ay, az;
    int head, nbox, parent; /* parent pivot for ModelRenderer childModels; -1 for a root */
    struct mob_box box[MAX_BOXES];
};
struct mob_model
{
    struct mob_part part[MAX_PARTS];
    int npart, quadruped;
    float child_head_y, child_head_z;
};
struct mob_texture
{
    unsigned char *rgba;
    int w, h;
};
enum { TEX_PIG, TEX_SADDLE, TEX_COW, TEX_SHEEP, TEX_FUR, TEX_CHICKEN,
       TEX_ZOMBIE, TEX_SHADOW, TEX_SKELETON, TEX_WITHER_SKELETON,
       TEX_CREEPER, TEX_CREEPER_ARMOR, TEX_SPIDER, TEX_CAVE_SPIDER,
       TEX_SPIDER_EYES, TEX_ENDERMAN, TEX_ENDERMAN_EYES, TEX_WITCH,
TEX_SLIME, TEX_SILVERFISH, TEX_PIGMAN, TEX_GHAST, TEX_GHAST_SHOOTING,
       TEX_BLAZE, TEX_MAGMACUBE, TEX_VILLAGER, TEX_FARMER, TEX_LIBRARIAN,
       TEX_PRIEST, TEX_SMITH, TEX_BUTCHER, TEX_IRON_GOLEM, TEX_SQUID, TEX_BAT,
       TEX_MOOSHROOM, TEX_MUSHROOM_RED, TEX_DRAGON, TEX_DRAGON_EYES,
       TEX_DRAGON_EXPLODING, TEX_ENDCRYSTAL, TEX_ENDCRYSTAL_BEAM, TEX_ZOMBIE_VILLAGER, TEX_LEAD_KNOT, TEX_STEVE, TEX_COUNT };
static const char *texture_names[TEX_COUNT] = {
    "pig", "pig_saddle", "cow", "sheep", "sheep_fur", "chicken", "zombie", "shadow",
    "skeleton", "wither_skeleton", "creeper", "creeper_armor", "spider", "cave_spider",
    "spider_eyes", "enderman", "enderman_eyes", "witch", "slime", "silverfish",
"pigman", "ghast", "ghast_shooting", "blaze", "magmacube", "villager", "farmer",
    "librarian", "priest", "smith", "butcher", "iron_golem", "squid", "bat",
    "mooshroom", "mushroom_red",
    "dragon", "dragon_eyes", "dragon_exploding", "endercrystal", "endercrystal_beam",
    "zombie_villager", "lead_knot", "steve"};
static const struct { int w, h; } texture_size[TEX_COUNT] = {
    {64, 32}, {64, 32}, {64, 32}, {64, 32}, {64, 32}, {64, 32}, {64, 64}, {64, 64},
    {64, 32}, {64, 32}, {64, 32}, {64, 32}, {64, 32}, {64, 32},
    {64, 32}, {64, 32}, {64, 32}, {64, 128}, {64, 32}, {64, 32},
    {64, 64}, {64, 32}, {64, 32}, {64, 32}, {64, 32}, {64, 64}, {64, 64},
    {64, 64}, {64, 64}, {64, 64}, {64, 64}, {128, 128}, {64, 32}, {64, 64},
    {64, 32}, {16, 16},
    {256, 256}, {256, 256}, {256, 256}, {128, 64}, {16, 256}, {64, 64}, {32, 32}, {64, 32}};
static struct mob_texture textures[TEX_COUNT];
static struct rb_atlas block_atlas;
static unsigned char *block_atlas_rgba;
static char texture_assets[1024];
static const float fleece[16][3] = {
    {1, 1, 1},          {0.85f, 0.5f, 0.2f}, {0.7f, 0.3f, 0.85f},  {0.4f, 0.6f, 0.85f},
    {0.9f, 0.9f, 0.2f}, {0.5f, 0.8f, 0.1f},  {0.95f, 0.5f, 0.65f}, {0.3f, 0.3f, 0.3f},
    {0.6f, 0.6f, 0.6f}, {0.3f, 0.5f, 0.6f},  {0.5f, 0.25f, 0.7f},  {0.2f, 0.3f, 0.7f},
    {0.4f, 0.3f, 0.2f}, {0.4f, 0.5f, 0.2f},  {0.6f, 0.2f, 0.2f},   {0.1f, 0.1f, 0.1f}};

static struct mob_part *part(struct mob_model *m, float x, float y, float z, int head)
{
    if (m->npart >= MAX_PARTS)
        abort();
    struct mob_part *p = &m->part[m->npart++];
    memset(p, 0, sizeof *p);
    p->x = x;
    p->y = y;
    p->z = z;
    p->head = head;
    p->parent = -1;
    return p;
}
static void box(struct mob_part *p, float x, float y, float z, int dx, int dy, int dz, int u, int v,
                float grow, int mirror)
{
    if (p->nbox >= MAX_BOXES)
        abort();
    p->box[p->nbox++] = (struct mob_box){x, y, z, grow, dx, dy, dz, u, v, mirror};
}
static void quad_legs(struct mob_model *m, int h, float grow)
{
    const float lx[4] = {-3, 3, -3, 3}, lz[4] = {7, 7, -5, -5};
    for (int i = 0; i < 4; ++i)
    {
        struct mob_part *p = part(m, lx[i], 24 - h, lz[i], 0);
        box(p, -2, 0, -2, 4, h, 4, 0, 16, grow, 0);
    }
}
static void quadruped(struct mob_model *m, int h, float grow)
{
    m->quadruped = 1;
    m->child_head_y = 8;
    m->child_head_z = 4;
    struct mob_part *p = part(m, 0, 18 - h, -6, 1);
    box(p, -4, -4, -8, 8, 8, 8, 0, 0, grow, 0);
    p = part(m, 0, 17 - h, 2, 0);
    p->ax = PI / 2;
    box(p, -5, -10, -7, 10, 16, 8, 28, 8, grow, 0);
    quad_legs(m, h, grow);
}
static void model_pig(struct mob_model *m, float grow)
{
    quadruped(m, 6, grow);
    m->child_head_y = 4;
    box(&m->part[0], -2, 0, -9, 4, 3, 1, 16, 16, grow, 0);
}
static void model_cow(struct mob_model *m)
{
    quadruped(m, 12, 0);
    struct mob_part *p = &m->part[0];
    p->y = 4;
    p->z = -8;
    p->nbox = 0;
    box(p, -4, -4, -6, 8, 8, 6, 0, 0, 0, 0);
    box(p, -5, -5, -4, 1, 3, 1, 22, 0, 0, 0);
    box(p, 4, -5, -4, 1, 3, 1, 22, 0, 0, 0);
    p = &m->part[1];
    p->y = 5;
    p->nbox = 0;
    box(p, -6, -10, -7, 12, 18, 10, 18, 4, 0, 0);
    box(p, -2, 2, -8, 4, 6, 1, 52, 0, 0, 0);
    m->part[2].x -= 1;
    m->part[3].x += 1;
    m->part[4].x -= 1;
    m->part[5].x += 1;
    m->part[4].z -= 1;
    m->part[5].z -= 1;
    m->child_head_z = 6;
}
static void model_sheep(struct mob_model *m, int wool, float eat_y, float eat_x)
{
    quadruped(m, 12, 0);
    struct mob_part *p = &m->part[0];
    p->y = 6 + eat_y * 9;
    p->z = -8;
    p->ax = eat_x;
    p->nbox = 0;
    box(p, -3, -4, wool ? -4 : -6, 6, 6, wool ? 6 : 8, 0, 0, wool ? 0.6f : 0, 0);
    p = &m->part[1];
    p->y = 5;
    p->nbox = 0;
    box(p, -4, -10, -7, 8, 16, 6, 28, 8, wool ? 1.75f : 0, 0);
    if (wool)
    {
        for (int i = 2; i < 6; ++i)
        {
            p = &m->part[i];
            p->nbox = 0;
            box(p, -2, 0, -2, 4, 6, 4, 0, 16, 0.5f, 0);
        }
    }
}
static void model_chicken(struct mob_model *m, float wing)
{
    m->child_head_y = 5;
    m->child_head_z = 2;
    struct mob_part *p = part(m, 0, 15, -4, 1);
    box(p, -2, -6, -2, 4, 6, 3, 0, 0, 0, 0);
    p = part(m, 0, 15, -4, 1);
    box(p, -2, -4, -4, 4, 2, 2, 14, 0, 0, 0);
    p = part(m, 0, 15, -4, 1);
    box(p, -1, -2, -3, 2, 2, 2, 14, 4, 0, 0);
    p = part(m, 0, 16, 0, 0);
    p->ax = PI / 2;
    box(p, -3, -4, -3, 6, 8, 6, 0, 9, 0, 0);
    p = part(m, -2, 19, 1, 0);
    box(p, -1, 0, -3, 3, 5, 3, 26, 0, 0, 0);
    p = part(m, 1, 19, 1, 0);
    box(p, -1, 0, -3, 3, 5, 3, 26, 0, 0, 0);
    p = part(m, -4, 13, 0, 0);
    p->az = wing;
    box(p, 0, 0, -3, 1, 4, 6, 24, 13, 0, 0);
    p = part(m, 4, 13, 0, 0);
    p->az = -wing;
    box(p, -1, 0, -3, 1, 4, 6, 24, 13, 0, 0);
}
static void model_zombie(struct mob_model *m, float limb, float amount, float swing, float age)
{
    m->child_head_y = 16;
    struct mob_part *p = part(m, 0, 0, 0, 1);
    box(p, -4, -8, -4, 8, 8, 8, 0, 0, 0, 0);
    p = part(m, 0, 0, 0, 0);
    float body_yaw = mh_sin(sqrtf(swing) * PI * 2.0f) * 0.2f;
    p->ay = body_yaw;
    box(p, -4, 0, -2, 8, 12, 4, 16, 16, 0, 0);
    float s = mh_sin(swing * PI);
    float smooth = mh_sin((1.0f - (1.0f - swing) * (1.0f - swing)) * PI);
    p = part(m, -mh_cos(body_yaw) * 5.0f, 2, mh_sin(body_yaw) * 5.0f, 0);
    p->ax = -PI / 2 - s * 1.2f + smooth * 0.4f + mh_sin(age * 0.067f) * 0.05f;
    p->ay = -(0.1f - s * 0.6f);
    p->az = mh_cos(age * 0.09f) * 0.05f + 0.05f;
    box(p, -3, -2, -2, 4, 12, 4, 40, 16, 0, 0);
    p = part(m, mh_cos(body_yaw) * 5.0f, 2, -mh_sin(body_yaw) * 5.0f, 0);
    p->ax = -PI / 2 - s * 1.2f + smooth * 0.4f - mh_sin(age * 0.067f) * 0.05f;
    p->ay = -m->part[2].ay;
    p->az = -m->part[2].az;
    box(p, -1, -2, -2, 4, 12, 4, 40, 16, 0, 1);
    p = part(m, -1.9f, 12, 0.1f, 0);
    p->ax = mh_cos(limb * 0.6662f) * 1.4f * amount;
    box(p, -2, 0, -2, 4, 12, 4, 0, 16, 0, 0);
    p = part(m, 1.9f, 12, 0.1f, 0);
    p->ax = mh_cos(limb * 0.6662f + PI) * 1.4f * amount;
    box(p, -2, 0, -2, 4, 12, 4, 0, 16, 0, 1);
    p = part(m, 0, 0, 0, 0);
    box(p, -4, -8, -4, 8, 8, 8, 32, 0, 0.5f, 0);
}
static void model_villager(struct mob_model *m, float limb, float amount)
{
    struct mob_part *p = part(m, 0, 0, 0, 1);
    box(p, -4, -10, -4, 8, 10, 8, 0, 0, 0, 0);
    p = part(m, 0, -2, 0, 1); p->parent = 0;
    box(p, -1, -1, -6, 2, 4, 2, 24, 0, 0, 0);
    p = part(m, 0, 0, 0, 0);
    box(p, -4, 0, -3, 8, 12, 6, 16, 20, 0, 0);
    box(p, -4, 0, -3, 8, 18, 6, 0, 38, 0.5f, 0);
    p = part(m, 0, 3, -1, 0); p->ax = -0.75f;
    box(p, -8, -2, -2, 4, 8, 4, 44, 22, 0, 0);
    box(p, 4, -2, -2, 4, 8, 4, 44, 22, 0, 0);
    box(p, -4, 2, -2, 8, 4, 4, 40, 38, 0, 0);
    p = part(m, -2, 12, 0, 0);
    p->ax = mh_cos(limb * 0.6662f) * 1.4f * amount * 0.5f;
    box(p, -2, 0, -2, 4, 12, 4, 0, 22, 0, 0);
    p = part(m, 2, 12, 0, 0);
    p->ax = mh_cos(limb * 0.6662f + PI) * 1.4f * amount * 0.5f;
    box(p, -2, 0, -2, 4, 12, 4, 0, 22, 0, 1);
}
static float golem_triangle(float v, float period)
{
    return (fabsf(fmodf(v, period) - period * 0.5f) - period * 0.25f) / (period * 0.25f);
}
static void model_iron_golem(struct mob_model *m, const struct mob_render_input *e, float limb, float amount)
{
    struct mob_part *p = part(m, 0, -7, -2, 1);
    box(p, -4, -12, -5.5f, 8, 10, 8, 0, 0, 0, 0);
    box(p, -1, -5, -7.5f, 2, 4, 2, 24, 0, 0, 0);
    p = part(m, 0, -7, 0, 0);
    box(p, -9, -2, -6, 18, 12, 11, 0, 40, 0, 0);
    box(p, -4.5f, 10, -3, 9, 5, 6, 0, 70, 0.5f, 0);
    p = part(m, 0, -7, 0, 0);
    box(p, -13, -2.5f, -3, 4, 30, 6, 60, 21, 0, 0);
    p = part(m, 0, -7, 0, 0);
    box(p, 9, -2.5f, -3, 4, 30, 6, 60, 58, 0, 0);
    p = part(m, -4, 11, 0, 0);
    p->ax = -1.5f * golem_triangle(limb, 13) * amount;
    box(p, -3.5f, -3, -3, 6, 16, 5, 37, 0, 0, 0);
    p = part(m, 5, 11, 0, 0);
    p->ax = 1.5f * golem_triangle(limb, 13) * amount;
    box(p, -3.5f, -3, -3, 6, 16, 5, 60, 0, 0, 1);
    if (e->attack_timer > 0)
        m->part[2].ax = m->part[3].ax = -2 + 1.5f * golem_triangle(e->attack_timer - e->partial_tick, 10);
    else if (e->rose_timer > 0)
        m->part[2].ax = -0.8f + 0.025f * golem_triangle(e->rose_timer, 70);
    else {
        m->part[2].ax = (-0.2f + 1.5f * golem_triangle(limb, 13)) * amount;
        m->part[3].ax = (-0.2f - 1.5f * golem_triangle(limb, 13)) * amount;
    }
}
static void model_blaze(struct mob_model *m, float age)
{
    struct mob_part *p = part(m, 0, 0, 0, 1);
    box(p, -4, -4, -4, 8, 8, 8, 0, 0, 0, 0);
    float angle = age * PI * -0.1f;
    for (int i = 0; i < 12; ++i)
    {
        if (i == 4) angle = PI / 4 + age * PI * 0.03f;
        if (i == 8) angle = 0.47123894f + age * PI * -0.05f;
        float radius = i < 4 ? 9 : i < 8 ? 7 : 5;
        float y = i < 4 ? -2 + mh_cos((i * 2.0f + age) * 0.25f) :
                  i < 8 ? 2 + mh_cos((i * 2.0f + age) * 0.25f) :
                          11 + mh_cos((i * 1.5f + age) * 0.5f);
        p = part(m, mh_cos(angle) * radius, y, mh_sin(angle) * radius, 0);
        box(p, 0, 0, 0, 2, 8, 2, 0, 16, 0, 0);
        angle += 1;
    }
}
static void model_magma_cube(struct mob_model *m, float squish)
{
    struct mob_part *p = part(m, 0, 0, 0, 0);
    box(p, -2, 18, -2, 4, 4, 4, 0, 16, 0, 0);
    for (int i = 0; i < 8; ++i)
    {
        int u = i == 2 || i == 3 ? 24 : 0;
        int v = i == 2 ? 10 : i == 3 ? 19 : i;
        p = part(m, 0, -(4 - i) * fmaxf(0, squish) * 1.7f, 0, 0);
        box(p, -4, 16 + i, -4, 8, 1, 8, u, v, 0, 0);
    }
}
static void model_squid(struct mob_model *m, float tentacle)
{
    struct mob_part *p = part(m, 0, 8, 0, 0);
    box(p, -6, -8, -6, 12, 16, 12, 0, 0, 0, 0);
    for (int i = 0; i < 8; ++i)
    {
        double a = (double)i * 3.14159265358979323846 * 2.0 / 8.0;
        p = part(m, (float)cos(a) * 5, 15, (float)sin(a) * 5, 0);
        p->ay = (float)((double)i * 3.14159265358979323846 * -2.0 / 8.0 + 3.14159265358979323846 / 2.0);
        p->ax = tentacle;
        box(p, -1, 0, -1, 2, 18, 2, 48, 0, 0, 0);
    }
}
static void model_ghast(struct mob_model *m, float age)
{
    static const int len[9] = {8, 13, 9, 11, 11, 10, 12, 9, 12};
    struct mob_part *p = part(m, 0, 8, 0, 0);
    box(p, -8, -8, -8, 16, 16, 16, 0, 0, 0, 0);
    for (int i = 0; i < 9; ++i)
    {
        float x = ((i % 3 - (i / 3 % 2) * 0.5f + 0.25f) - 1) * 5;
        float z = ((float)(i / 3) - 1) * 5;
        p = part(m, x, 15, z, 0);
        p->ax = 0.2f * mh_sin(age * 0.3f + i) + 0.4f;
        box(p, -1, 0, -1, 2, len[i], 2, 0, 0, 0, 0);
    }
}
static void model_bat(struct mob_model *m, const struct mob_render_input *e)
{
    float age = e->age + e->partial_tick;
    struct mob_part *p = part(m, 0, e->hanging ? -2 : 0, 0, 1);
    p->ax = e->pitch / (180.0f / PI);
    p->ay = e->hanging ? PI - (e->head_yaw - e->body_yaw) / (180.0f / PI) :
                         (e->head_yaw - e->body_yaw) / (180.0f / PI);
    p->az = e->hanging ? PI : 0;
    box(p, -3, -3, -3, 6, 6, 6, 0, 0, 0, 0);
    p = part(m, 0, 0, 0, 1); p->parent = 0;
    box(p, -4, -6, -2, 3, 4, 1, 24, 0, 0, 0);
    p = part(m, 0, 0, 0, 1); p->parent = 0;
    box(p, 1, -6, -2, 3, 4, 1, 24, 0, 0, 1);
    p = part(m, 0, 0, 0, 0);
    p->ax = e->hanging ? PI : PI / 4 + mh_cos(age * 0.1f) * 0.15f;
    box(p, -3, 4, -3, 6, 12, 6, 0, 16, 0, 0);
    box(p, -5, 16, 0, 10, 6, 1, 0, 34, 0, 0);
    p = part(m, e->hanging ? -3 : 0, 0, e->hanging ? 3 : 0, 0);
    p->parent = 3;
    p->ax = e->hanging ? -0.15707964f : 0;
    p->ay = e->hanging ? -PI * 2 / 5 : mh_cos(age * 1.3f) * PI * 0.25f;
    box(p, -12, 1, 1.5f, 10, 16, 1, 42, 0, 0, 0);
    p = part(m, -12, 1, 1.5f, 0);
    p->parent = 4;
    p->ay = e->hanging ? -1.7278761f : m->part[4].ay * 0.5f;
    box(p, -8, 1, 0, 8, 12, 1, 24, 16, 0, 0);
    p = part(m, e->hanging ? 3 : 0, 0, e->hanging ? 3 : 0, 0);
    p->parent = 3;
    p->ax = m->part[4].ax;
    p->ay = -m->part[4].ay;
    box(p, 2, 1, 1.5f, 10, 16, 1, 42, 0, 0, 1);
    p = part(m, 12, 1, 1.5f, 0);
    p->parent = 6;
    p->ay = -m->part[5].ay;
    box(p, 0, 1, 0, 8, 12, 1, 24, 16, 0, 1);
}
static void model_skeleton(struct mob_model *m, float limb, float amount, float swing, float age,
                           int wither)
{
    model_zombie(m, limb, amount, swing, age);
    for (int i = 2; i <= 5; ++i)
        m->part[i].nbox = 0;
    m->part[4].x = -2; m->part[5].x = 2;
    box(&m->part[2], -1, -2, -1, 2, 12, 2, 40, 16, 0, 0);
    box(&m->part[3], -1, -2, -1, 2, 12, 2, 40, 16, 0, 1);
    box(&m->part[4], -1, 0, -1, 2, 12, 2, 0, 16, 0, 0);
    box(&m->part[5], -1, 0, -1, 2, 12, 2, 0, 16, 0, 1);
    m->part[6].nbox = 0;
    if (wither)
    {
        /* RenderSkeleton scales the whole wither skeleton by 1.2. */
        m->child_head_y = 0;
    }
}
static void model_creeper(struct mob_model *m, float limb, float amount, int armor)
{
    float grow = armor ? 2.0f : 0.0f;
    struct mob_part *p = part(m, 0, 4, 0, 1);
    box(p, -4, -8, -4, 8, 8, 8, 0, 0, grow, 0);
    p = part(m, 0, 4, 0, 0);
    box(p, -4, 0, -2, 8, 12, 4, 16, 16, grow, 0);
    for (int i = 0; i < 4; ++i)
    {
        p = part(m, i & 1 ? 2 : -2, 16, i < 2 ? 4 : -4, 0);
        p->ax = mh_cos(limb * 0.6662f + ((i == 1 || i == 2) ? PI : 0)) * 1.4f * amount;
        box(p, -2, 0, -2, 4, 6, 4, 0, 16, grow, 0);
    }
}
static void model_spider(struct mob_model *m, float limb, float amount)
{
    struct mob_part *p = part(m, 0, 15, -3, 1);
    box(p, -4, -4, -8, 8, 8, 8, 32, 4, 0, 0);
    p = part(m, 0, 15, 0, 0);
    box(p, -3, -3, -3, 6, 6, 6, 0, 0, 0, 0);
    p = part(m, 0, 15, 9, 0);
    box(p, -5, -4, -6, 10, 8, 12, 0, 12, 0, 0);
    float ybase[8] = {PI/4, -PI/4, 0.3926991f, -0.3926991f,
                      -0.3926991f, 0.3926991f, -PI/4, PI/4};
    float zbase[8] = {-PI/4, PI/4, -PI/4*0.74f, PI/4*0.74f,
                      -PI/4*0.74f, PI/4*0.74f, -PI/4, PI/4};
    float phases[4] = {0, PI, PI/2, PI*1.5f};
    for (int i = 0; i < 8; ++i)
    {
        p = part(m, i & 1 ? 4 : -4, 15, 2 - i/2, 0);
        int k = i / 2;
        float dy = -mh_cos(limb * 0.6662f * 2.0f + phases[k]) * 0.4f * amount;
        float dz = fabsf(mh_sin(limb * 0.6662f + phases[k]) * 0.4f) * amount;
        p->ay = ybase[i] + (i & 1 ? -dy : dy);
        p->az = zbase[i] + (i & 1 ? -dz : dz);
        box(p, i & 1 ? -1 : -15, -1, -1, 16, 2, 2, 18, 0, 0, 0);
    }
}
static void model_slime(struct mob_model *m, int outer)
{
    struct mob_part *p = part(m, 0, 0, 0, 0);
    box(p, outer ? -4 : -3, outer ? 16 : 17, outer ? -4 : -3,
        outer ? 8 : 6, outer ? 8 : 6, outer ? 8 : 6, 0, outer ? 0 : 10, 0, 0);
    if (outer) return;
    p = part(m, 0, 0, 0, 0);
    box(p, -3.25f, 18, -3.5f, 2, 2, 2, 32, 0, 0, 0);
    p = part(m, 0, 0, 0, 0);
    box(p, 1.25f, 18, -3.5f, 2, 2, 2, 32, 4, 0, 0);
    p = part(m, 0, 0, 0, 0);
    box(p, 0, 21, -3.5f, 1, 1, 1, 32, 8, 0, 0);
}
static void model_silverfish(struct mob_model *m, float age)
{
    static const int dims[7][3] = {{3,2,2},{4,3,2},{6,4,3},{3,3,3},{2,2,3},{2,1,2},{1,1,2}};
    static const int uv[7][2] = {{0,0},{0,4},{0,9},{0,16},{0,22},{11,0},{13,4}};
    float z = -3.5f, pivots[7];
    for (int i = 0; i < 7; ++i)
    {
        pivots[i] = z;
        struct mob_part *p = part(m, 0, 24-dims[i][1], z, 0);
        p->ay = mh_cos(age * 0.9f + i * 0.15f * PI) * PI * 0.05f * (1 + abs(i-2));
        p->x = mh_sin(age * 0.9f + i * 0.15f * PI) * PI * 0.2f * abs(i-2);
        box(p, -dims[i][0]*0.5f, 0, -dims[i][2]*0.5f,
            dims[i][0], dims[i][1], dims[i][2], uv[i][0], uv[i][1], 0, 0);
        if (i < 6) z += (dims[i][2] + dims[i+1][2]) * 0.5f;
    }
    struct mob_part *p = part(m, 0, 16, pivots[2], 0);
    p->ay = m->part[2].ay;
    box(p, -5, 0, -1.5f, 10, 8, 3, 20, 0, 0, 0);
    p = part(m, m->part[4].x, 20, pivots[4], 0);
    p->ay = m->part[4].ay;
    box(p, -3, 0, -1.5f, 6, 4, 3, 20, 11, 0, 0);
    p = part(m, m->part[1].x, 19, pivots[1], 0);
    p->ay = m->part[1].ay;
    box(p, -3, 0, -1, 6, 5, 2, 20, 18, 0, 0);
}
static void model_enderman(struct mob_model *m, float limb, float amount, float age,
                          int carrying, int screaming)
{
    struct mob_part *p = part(m, 0, -13 - (screaming ? 5 : 0), 0, 1);
    box(p, -4, -8, -4, 8, 8, 8, 0, 0, 0, 0);
    p = part(m, 0, -14, 0, 0);
    box(p, -4, 0, -2, 8, 12, 4, 32, 16, 0, 0);
    for (int i = 0; i < 2; ++i)
    {
        p = part(m, i ? 5 : -5, -12, 0, 0);
        p->ax = carrying ? -0.5f : fmaxf(-0.4f, fminf(0.4f,
            (mh_cos(limb * 0.6662f + (i ? 0 : PI)) * amount +
             (i ? -1 : 1) * mh_sin(age * 0.067f) * 0.05f) * 0.5f));
        p->az = carrying ? (i ? -0.05f : 0.05f) :
                (i ? -1 : 1) * (mh_cos(age * 0.09f) * 0.05f + 0.05f);
        box(p, -1, -2, -1, 2, 30, 2, 56, 0, 0, i);
    }
    for (int i = 0; i < 2; ++i)
    {
        p = part(m, i ? 2 : -2, -5, 0, 0);
        p->ax = fmaxf(-0.4f, fminf(0.4f,
            mh_cos(limb * 0.6662f + (i ? PI : 0)) * 1.4f * amount * 0.5f));
        box(p, -1, 0, -1, 2, 30, 2, 56, 0, 0, i);
    }
    p = part(m, 0, -13, 0, 0);
    box(p, -4, -8, -4, 8, 8, 8, 0, 16, -0.5f, 0);
}
static void model_witch(struct mob_model *m, float limb, float amount, float age, int id,
                        int holding)
{
    struct mob_part *p = part(m, 0, 0, 0, 1);
    box(p, -4, -10, -4, 8, 10, 8, 0, 0, 0, 0);
    p = part(m, 0, 0, 0, 0);
    box(p, -4, 0, -3, 8, 12, 6, 16, 20, 0, 0);
    box(p, -4, 0, -3, 8, 18, 6, 0, 38, 0.5f, 0);
    p = part(m, 0, 3, -1, 0); p->ax = -0.75f;
    box(p, -8, -2, -2, 4, 8, 4, 44, 22, 0, 0);
    box(p, 4, -2, -2, 4, 8, 4, 44, 22, 0, 0);
    box(p, -4, 2, -2, 8, 4, 4, 40, 38, 0, 0);
    for (int i = 0; i < 2; ++i)
    {
        p = part(m, i ? 2 : -2, 12, 0, 0);
        p->ax = mh_cos(limb * 0.6662f + (i ? PI : 0)) * 0.7f * amount;
        box(p, -2, 0, -2, 4, 12, 4, 0, 22, 0, i);
    }
    p = part(m, 0, -2, 0, 0); p->parent = 0;
    float freq = 0.01f * (id % 10);
    p->ax = holding ? -0.9f : mh_sin(age * freq) * 4.5f * PI / 180;
    p->az = mh_cos(age * freq) * 2.5f * PI / 180;
    /* ModelRenderer.render translates by the offsets (blocks) in the head's
     * frame before the pivot */
    if (holding) { p->y += 0.1875f * 16; p->z += -0.09375f * 16; }
    box(p, -1, -1, -6, 2, 4, 2, 24, 0, 0, 0);
    p = part(m, 0, -2, 0, 0); p->parent = 5;
    box(p, 0, 3, -6.75f, 1, 1, 1, 0, 0, -0.25f, 0);
    p = part(m, -5, -10.03125f, -5, 0); p->parent = 0;
    box(p, 0, 0, 0, 10, 2, 10, 0, 64, 0, 0);
    p = part(m, 1.75f, -4, 2, 0); p->parent = 7;
    p->ax = -0.05235988f; p->az = 0.02617994f;
    box(p, 0, 0, 0, 7, 4, 7, 0, 76, 0, 0);
    p = part(m, 1.75f, -4, 2, 0); p->parent = 8;
    p->ax = -0.10471976f; p->az = 0.05235988f;
    box(p, 0, 0, 0, 4, 4, 4, 0, 87, 0, 0);
    p = part(m, 1.75f, -2, 2, 0); p->parent = 9;
    p->ax = -0.20943952f; p->az = 0.10471976f;
    box(p, 0, 0, 0, 1, 2, 1, 0, 95, 0.25f, 0);
}
/* ModelBiped(grow) posed by setRotationAngles with RenderPlayer's flags:
 * heldItemRight, the sword's block (3), aimedBow, isSneak, isRiding. The
 * parts are head, body, right and left arm, right and left leg, headwear, in
 * render()'s order. */
static void model_player(struct mob_model *m, const struct mob_render_input *e, float grow)
{
    float limb = e->limb, amount = fminf(e->limb_amount, 1);
    float age = (float)e->age + e->partial_tick;
    float swing = e->swing;
    struct mob_part *head = part(m, 0, 0, 0, 1);
    box(head, -4, -8, -4, 8, 8, 8, 0, 0, grow, 0);
    struct mob_part *body = part(m, 0, 0, 0, 0);
    box(body, -4, 0, -2, 8, 12, 4, 16, 16, grow, 0);
    struct mob_part *rarm = part(m, -5, 2, 0, 0);
    box(rarm, -3, -2, -2, 4, 12, 4, 40, 16, grow, 0);
    struct mob_part *larm = part(m, 5, 2, 0, 0);
    box(larm, -1, -2, -2, 4, 12, 4, 40, 16, grow, 1);
    struct mob_part *rleg = part(m, -1.9f, 12, 0, 0);
    box(rleg, -2, 0, -2, 4, 12, 4, 0, 16, grow, 0);
    struct mob_part *lleg = part(m, 1.9f, 12, 0, 0);
    box(lleg, -2, 0, -2, 4, 12, 4, 0, 16, grow, 1);
    struct mob_part *hw = part(m, 0, 0, 0, 1);
    box(hw, -4, -8, -4, 8, 8, 8, 32, 0, grow + 0.5f, 0);
    float hy = (e->head_yaw - e->body_yaw) / (180.0f / PI), hx = e->pitch / (180.0f / PI);
    head->ay = hw->ay = hy;
    head->ax = hw->ax = hx;
    rarm->ax = mh_cos(limb * 0.6662f + PI) * 2.0f * amount * 0.5f;
    larm->ax = mh_cos(limb * 0.6662f) * 2.0f * amount * 0.5f;
    rarm->az = larm->az = 0.0f;
    rleg->ax = mh_cos(limb * 0.6662f) * 1.4f * amount;
    lleg->ax = mh_cos(limb * 0.6662f + PI) * 1.4f * amount;
    rleg->ay = lleg->ay = 0.0f;
    if (e->riding)
    {
        rarm->ax += -(PI / 5.0f);
        larm->ax += -(PI / 5.0f);
        rleg->ax = -(PI * 2.0f / 5.0f);
        lleg->ax = -(PI * 2.0f / 5.0f);
        rleg->ay = PI / 10.0f;
        lleg->ay = -(PI / 10.0f);
    }
    if (e->held_right) rarm->ax = rarm->ax * 0.5f - (PI / 10.0f) * (float)e->held_right;
    rarm->ay = larm->ay = 0.0f;
    body->ay = mh_sin(sqrtf(swing) * PI * 2.0f) * 0.2f;
    rarm->z = mh_sin(body->ay) * 5.0f;
    rarm->x = -mh_cos(body->ay) * 5.0f;
    larm->z = -mh_sin(body->ay) * 5.0f;
    larm->x = mh_cos(body->ay) * 5.0f;
    rarm->ay += body->ay;
    larm->ay += body->ay;
    larm->ax += body->ay;
    float v8 = 1.0f - swing;
    v8 *= v8;
    v8 *= v8;
    v8 = 1.0f - v8;
    float v9 = mh_sin(v8 * PI);
    float v10 = mh_sin(swing * PI) * -(head->ax - 0.7f) * 0.75f;
    rarm->ax = (float)((double)rarm->ax - ((double)v9 * 1.2 + (double)v10));
    rarm->ay += body->ay * 2.0f;
    rarm->az = mh_sin(swing * PI) * -0.4f;
    if (e->sneak)
    {
        body->ax = 0.5f;
        rarm->ax += 0.4f;
        larm->ax += 0.4f;
        rleg->z = lleg->z = 4.0f;
        rleg->y = lleg->y = 9.0f;
        head->y = hw->y = 1.0f;
    }
    else
    {
        body->ax = 0.0f;
        rleg->z = lleg->z = 0.1f;
        rleg->y = lleg->y = 12.0f;
        head->y = hw->y = 0.0f;
    }
    rarm->az += mh_cos(age * 0.09f) * 0.05f + 0.05f;
    larm->az -= mh_cos(age * 0.09f) * 0.05f + 0.05f;
    rarm->ax += mh_sin(age * 0.067f) * 0.05f;
    larm->ax -= mh_sin(age * 0.067f) * 0.05f;
    if (e->aimed_bow)
    {
        rarm->az = larm->az = 0.0f;
        rarm->ay = -(0.1f - 0.0f * 0.6f) + head->ay;
        larm->ay = 0.1f - 0.0f * 0.6f + head->ay + 0.4f;
        rarm->ax = -(PI / 2.0f) + head->ax;
        larm->ax = -(PI / 2.0f) + head->ax;
        rarm->ax -= 0.0f * 1.2f - 0.0f * 0.4f;
        larm->ax -= 0.0f * 1.2f - 0.0f * 0.4f;
        rarm->az += mh_cos(age * 0.09f) * 0.05f + 0.05f;
        larm->az -= mh_cos(age * 0.09f) * 0.05f + 0.05f;
        rarm->ax += mh_sin(age * 0.067f) * 0.05f;
        larm->ax -= mh_sin(age * 0.067f) * 0.05f;
    }
}

static void pose_model(struct mob_model *m, const struct mob_render_input *e, int wool)
{
    memset(m, 0, sizeof *m);
    float limb = e->limb * (e->child ? 3 : 1), amount = fminf(e->limb_amount, 1);
    switch (e->kind)
    {
    case MOB_PIG:
        model_pig(m, 0);
        break;
    case MOB_COW:
        model_cow(m);
        break;
    case MOB_SHEEP:
        model_sheep(m, wool, e->eat_head_y, e->eat_head_x);
        break;
    case MOB_CHICKEN:
        model_chicken(m, e->wing);
        break;
    case MOB_ZOMBIE:
        model_zombie(m, limb, amount, e->swing, e->age + e->partial_tick);
        if (e->zombie_villager)
        {
            m->part[0].nbox = 0;
            box(&m->part[0], -4, -10, -4, 8, 10, 8, 0, 32, 0, 0);
            box(&m->part[0], -1, -3, -6, 2, 4, 2, 24, 32, 0, 0);
        }
        break;
    case MOB_PIGMAN:
        model_zombie(m, limb, amount, e->swing, e->age + e->partial_tick);
        break;
    case MOB_MOOSHROOM:
        model_cow(m);
        break;
    case MOB_VILLAGER:
        model_villager(m, limb, amount);
        break;
    case MOB_IRON_GOLEM:
        model_iron_golem(m, e, limb, amount);
        break;
    case MOB_BLAZE:
        model_blaze(m, e->age + e->partial_tick);
        break;
    case MOB_MAGMA_CUBE:
        model_magma_cube(m, e->squish);
        break;
    case MOB_SQUID:
        model_squid(m, e->tentacle);
        break;
    case MOB_GHAST:
        model_ghast(m, e->age + e->partial_tick);
        break;
    case MOB_BAT:
        model_bat(m, e);
        break;
    case MOB_SKELETON:
    case MOB_WITHER_SKELETON:
        model_skeleton(m, limb, amount, e->swing, e->age + e->partial_tick,
                       e->kind == MOB_WITHER_SKELETON);
        break;
    case MOB_CREEPER: model_creeper(m, limb, amount, wool); break;
    case MOB_SPIDER:
    case MOB_CAVE_SPIDER: model_spider(m, limb, amount); break;
    case MOB_ENDERMAN: model_enderman(m, limb, amount, e->age + e->partial_tick,
                                      e->carried_id, e->screaming); break;
    case MOB_WITCH: model_witch(m, limb, amount, e->age, e->id, e->equip[0].id != 0); break;
    case MOB_SLIME: model_slime(m, wool); break;
    case MOB_SILVERFISH: model_silverfish(m, e->age + e->partial_tick); break;
    case MOB_PLAYER: model_player(m, e, 0.0f); break;
    case MOB_DRAGON:
    case MOB_ENDER_CRYSTAL:
    case MOB_LEASH_KNOT:
        break; /* drawn by dragon_draw / crystal_draw / knot_draw, not the shared path */
    }
    float head_x = e->pitch / (180.0f / PI), head_y = (e->head_yaw - e->body_yaw) / (180.0f / PI);
    if (e->kind == MOB_SHEEP)
        head_x = e->eat_head_x;
    if (e->kind == MOB_CHICKEN)
        for (int i = 0; i < 3; ++i)
        {
            m->part[i].ax = head_x;
            m->part[i].ay = head_y;
        }
    else if (e->kind != MOB_SLIME && e->kind != MOB_SILVERFISH &&
             e->kind != MOB_MAGMA_CUBE && e->kind != MOB_SQUID &&
             e->kind != MOB_GHAST && e->kind != MOB_BAT)
    {
        m->part[0].ax = head_x;
        m->part[0].ay = head_y;
    }
    if (e->kind == MOB_ZOMBIE || e->kind == MOB_PIGMAN || e->kind == MOB_SKELETON ||
        e->kind == MOB_WITHER_SKELETON || e->kind == MOB_ENDERMAN || e->kind == MOB_PLAYER)
    {
        m->part[6].ax = head_x;
        m->part[6].ay = head_y;
    }
    if (m->quadruped)
    {
        float x = mh_cos(limb * 0.6662f) * 1.4f * amount;
        float y = mh_cos(limb * 0.6662f + PI) * 1.4f * amount;
        m->part[2].ax = x;
        m->part[3].ax = y;
        m->part[4].ax = y;
        m->part[5].ax = x;
    }
    if (e->kind == MOB_CHICKEN)
    {
        float x = mh_cos(limb * 0.6662f) * 1.4f * amount;
        float y = mh_cos(limb * 0.6662f + PI) * 1.4f * amount;
        m->part[4].ax = x;
        m->part[5].ax = y;
    }
}

static float diffuse(float nx, float ny, float nz)
{
    return raster_ebox_diffuse(nx, ny, nz);
}
static void rotate_x(float v[3], float a)
{
    float c = cosf(a), s = sinf(a), y = v[1], z = v[2];
    v[1] = y * c - z * s;
    v[2] = y * s + z * c;
}
static void rotate_y(float v[3], float a)
{
    float c = cosf(a), s = sinf(a), x = v[0], z = v[2];
    v[0] = x * c + z * s;
    v[2] = -x * s + z * c;
}
/* getDeathMaxRotation: RenderSpider (and RenderCaveSpider) and
 * RenderSilverfish turn the corpse 180 degrees, every other renderer 90. */
static float death_max_rotation(const struct mob_render_input *e)
{
    return e->kind == MOB_SPIDER || e->kind == MOB_CAVE_SPIDER || e->kind == MOB_SILVERFISH
               ? 180.0f : 90.0f;
}
/* rotateCorpse's yaw: RenderZombie shakes a converting zombie villager by
 * cos(ticksExisted * 3.25) * PI * 0.25 degrees. */
static float corpse_yaw(const struct mob_render_input *e)
{
    if (e->converting && (e->kind == MOB_ZOMBIE))
        return e->body_yaw + (float)(fd_cos((double)e->age * 3.25) * 3.141592653589793 * 0.25);
    return e->body_yaw;
}
/* A rotation's cosine and sine, taken once for every vertex it turns (the
 * same cosf and sinf of the same angle rotate_x/y/z take per call). */
struct rot { float c, s; };
static struct rot rot_of(float a)
{
    struct rot r = {cosf(a), sinf(a)};
    return r;
}
static void rot_x(float v[3], struct rot r)
{
    float y = v[1], z = v[2];
    v[1] = y * r.c - z * r.s;
    v[2] = y * r.s + z * r.c;
}
static void rot_y(float v[3], struct rot r)
{
    float x = v[0], z = v[2];
    v[0] = x * r.c + z * r.s;
    v[2] = -x * r.s + z * r.c;
}
static void rot_z(float v[3], struct rot r)
{
    float x = v[0], y = v[1];
    v[0] = x * r.c - y * r.s;
    v[1] = x * r.s + y * r.c;
}
/* model_vertices' per-vertex stages after the part chain (the rotations
 * taken by the caller) */
static void model_vertex_tail(const struct mob_render_input *e, const struct mob_model *m, const struct mob_part *p,
                              float v[3], int golem_on, struct rot golem, int bed, struct rot bed_a,
                              struct rot bed_b, struct rot bed_c, struct rot death, struct rot sq_y,
                              struct rot sq_x, struct rot yaw)
{
    if (e->child)
    {
        if (m->quadruped || e->kind == MOB_CHICKEN)
        {
            if (p->head)
            {
                v[1] += m->child_head_y;
                v[2] += m->child_head_z;
            }
            else
            {
                v[1] = (v[1] + 24) * 0.5f;
                v[0] *= 0.5f;
                v[2] *= 0.5f;
            }
        }
        else if (e->kind == MOB_ZOMBIE || e->kind == MOB_PIGMAN)
        {
            if (p->head)
            {
                v[0] *= 0.75f;
                v[1] = (v[1] + 16) * 0.75f;
                v[2] *= 0.75f;
            }
            else
            {
                v[0] *= 0.5f;
                v[1] = (v[1] + 24) * 0.5f;
                v[2] *= 0.5f;
            }
        }
    }
    v[0] = -v[0] * 0.0625f;
    v[1] = 1.5078125f - v[1] * 0.0625f;
    v[2] = v[2] * 0.0625f;
    float sx = 1, sy = 1;
    if (e->kind == MOB_WITHER_SKELETON) sx = sy = 1.2f;
    else if (e->kind == MOB_CAVE_SPIDER) sx = sy = 0.7f;
    else if (e->kind == MOB_WITCH || e->kind == MOB_PLAYER) sx = sy = 0.9375f;
    else if (e->kind == MOB_VILLAGER) sx = sy = e->child ? 0.46875f : 0.9375f;
    else if (e->kind == MOB_SLIME || e->kind == MOB_MAGMA_CUBE)
    {
        float size = e->slime_size > 0 ? e->slime_size : 1;
        float squish = e->squish / (size * 0.5f + 1.0f);
        sx = size / (squish + 1.0f);
        sy = size * (squish + 1.0f);
    }
    else if (e->kind == MOB_CREEPER)
    {
        float flash = fmaxf(0, fminf(1, e->flash));
        float pulse = 1.0f + mh_sin(e->flash * 100.0f) * e->flash * 0.01f;
        float fourth = flash * flash; fourth *= fourth;
        sx = (1.0f + fourth * 0.4f) * pulse;
        sy = (1.0f + fourth * 0.1f) / pulse;
    }
    v[0] *= sx; v[1] *= sy; v[2] *= sx;
    if (e->kind == MOB_GHAST) {
        float t = fmaxf(0, e->attack_progress);
        float s = 1.0f / (t*t*t*t*t*2.0f + 1.0f);
        float horizontal = (8.0f + 1.0f/s) * 0.5f;
        float vertical = (8.0f + s) * 0.5f;
        v[0] *= horizontal;
        v[1] = (v[1] - 0.6f) * vertical;
        v[2] *= horizontal;
    }
    if (e->kind == MOB_BAT) {
        v[0] *= 0.35f; v[1] *= 0.35f; v[2] *= 0.35f;
        v[1] += e->hanging ? -0.1f : mh_cos((e->age + e->partial_tick) * 0.3f) * 0.1f;
    }
    if (golem_on)
        rot_z(v, golem);
    if (bed)
    {
        /* RenderPlayer.rotateCorpse in bed: the bed's turn, 90 about z,
         * 270 about y */
        rot_y(v, bed_a);
        rot_z(v, bed_b);
        rot_y(v, bed_c);
        return;
    }
    if (e->death > 0)
        rot_z(v, death);
    if (e->kind == MOB_SQUID)
    {
        v[1] -= 1.2f;
        rot_y(v, sq_y);
        rot_x(v, sq_x);
        rot_y(v, yaw);
        v[1] += 0.5f;
    }
    else
        rot_y(v, yaw);
}
/* The model's vertices v[0..n) through the part chain and the renderer's
 * transforms, each vertex exactly as alone: the stages run over all of
 * them, each stage's rotations taken once. */
static void model_vertices(const struct mob_part *p, const struct mob_model *m,
                           const struct mob_render_input *e, float (*v)[3], int n)
{
    const struct mob_part *current = p;
    for (int depth = 0; current; ++depth)
    {
        if (depth >= MAX_PARTS)
            abort();
        struct rot rx = rot_of(current->ax), ry = rot_of(current->ay), rz = rot_of(current->az);
        for (int i = 0; i < n; ++i)
        {
            rot_x(v[i], rx);
            rot_y(v[i], ry);
            rot_z(v[i], rz);
            v[i][0] += current->x;
            v[i][1] += current->y;
            v[i][2] += current->z;
        }
        if (current->parent >= m->npart)
            abort();
        current = current->parent < 0 ? NULL : &m->part[current->parent];
    }
    struct rot golem = {1, 0}, bed_a = {1, 0}, bed_b = {1, 0}, bed_c = {1, 0}, death = {1, 0};
    struct rot sq_y = {1, 0}, sq_x = {1, 0}, yaw = {1, 0};
    int golem_on = e->kind == MOB_IRON_GOLEM && e->limb_amount >= 0.01f;
    int bed = e->kind == MOB_PLAYER && e->sleeping;
    if (golem_on) golem = rot_of(6.5f * golem_triangle(e->limb + 6.0f, 13.0f) * (PI / 180.0f));
    if (bed)
    {
        bed_a = rot_of(270.0f * (PI / 180.0f));
        bed_b = rot_of(90.0f * (PI / 180.0f));
        bed_c = rot_of(e->bed_deg * (PI / 180.0f));
    }
    else
    {
        if (e->death > 0)
        {
            float d = ((float)e->death + e->partial_tick - 1.0f) / 20.0f * 1.6f;
            d = sqrtf(fmaxf(0, d));
            if (d > 1)
                d = 1;
            death = rot_of(d * death_max_rotation(e) * (PI / 180.0f));
        }
        if (e->kind == MOB_SQUID)
        {
            sq_y = rot_of(e->squid_yaw * (PI / 180.0f));
            sq_x = rot_of(e->squid_pitch * (PI / 180.0f));
            yaw = rot_of((180.0f - e->body_yaw) * (PI / 180.0f));
        }
        else
            yaw = rot_of((180.0f - corpse_yaw(e)) * (PI / 180.0f));
    }
    for (int i = 0; i < n; ++i)
        model_vertex_tail(e, m, p, v[i], golem_on, golem, bed, bed_a, bed_b, bed_c, death, sq_y, sq_x, yaw);
}
static void model_vertex(const struct mob_part *p, const struct mob_model *m,
                         const struct mob_render_input *e, float v[3])
{
    model_vertices(p, m, e, (float (*)[3])v, 1);
}
static struct entity_clip_vertex clip_vertex(const struct entity_raster_target *t,
                                             const struct mob_render_input *e, const float v[3],
                                             float u, float tv, float diff)
{
    struct entity_clip_vertex out = {0};
    out.u = u;
    out.v = tv;
    out.diffuse = diff;
    out.color[0] = out.color[1] = out.color[2] = out.color[3] = 1.0f;
    out.light[0] = (float)(e->brightness & 65535);
    out.light[1] = (float)((e->brightness >> 16) & 65535);
    float w[4] = {(float)(e->x - t->cam[0]) + v[0], (float)(e->y - t->cam[1]) + v[1],
                  (float)(e->z - t->cam[2]) + v[2], 1};
    if (e->outer_set)
        w[0] = v[0], w[1] = v[1], w[2] = v[2];
    float eye[4];
    for (int r = 0; r < 4; ++r)
        eye[r] = t->mv[r] * w[0] + t->mv[4 + r] * w[1] + t->mv[8 + r] * w[2] + t->mv[12 + r];
    for (int r = 0; r < 4; ++r)
        out.clip[r] = t->proj[r] * eye[0] + t->proj[4 + r] * eye[1] + t->proj[8 + r] * eye[2] +
                      t->proj[12 + r] * eye[3];
    out.fogcoord = sqrtf(eye[0] * eye[0] + eye[1] * eye[1] + eye[2] * eye[2]);
    return out;
}
/* One ModelBox's six quads from 8 already-transformed corners (ModelBox's
 * vertex order). flip_normal negates the shading normal for a pass GL draws
 * with glScalef(-1,1,1), whose normal matrix (unlike the geometry) keeps it.
 * The quads are raster_ebox_quads' (raster_obs.c); a recorder takes the box
 * as one record (raster_rec_ebox: the device makes its quads) and the C
 * passes draw them unrecorded. */
static void emit_box_faces(const struct entity_raster_target *t, const struct mob_render_input *e,
                           const float v[8][3], int u0, int vv0, int dx, int dy, int dz,
                           int mirror, int flip_normal, const struct mob_texture *tex)
{
    struct raster_obs_ebox b;
    memset(&b, 0, sizeof b);
    memcpy(b.v, v, sizeof b.v);
    b.outer = e->outer_set != 0;
    if (!b.outer)
    {
        b.base[0] = (float)(e->x - t->cam[0]);
        b.base[1] = (float)(e->y - t->cam[1]);
        b.base[2] = (float)(e->z - t->cam[2]);
    }
    b.light[0] = (float)(e->brightness & 65535);
    b.light[1] = (float)((e->brightness >> 16) & 65535);
    b.u = u0; b.vv = vv0; b.dx = dx; b.dy = dy; b.dz = dz;
    b.mirror = mirror; b.flip_normal = flip_normal;
    b.tex_w = tex->w; b.tex_h = tex->h;
    b.tex_u = t->tex_u; b.tex_v = t->tex_v;
    b.tex_matrix = t->tex_matrix;
    memcpy(b.tm, t->tm, sizeof b.tm);
    const unsigned char *rgba = t->sample_tex ? t->sample_tex : tex->rgba;
    int w = t->sample_tex ? t->sample_w : tex->w, h = t->sample_tex ? t->sample_h : tex->h;
    int rec = raster_rec_on();
    if (rec)
    {
        struct raster_obs_estate st;
        const uint32_t *lm;
        raster_entity_estate(t, &st, &lm);
        raster_rec_ebox(&st, lm, rgba, w, h, &b, t->mv, t->proj);
        if (raster_rec_only()) return;
    }
    struct entity_clip_vertex q[6][4];
    raster_ebox_quads(&b, t->mv, t->proj, q);
    struct raster_rec *held = rec ? raster_rec_hold() : NULL;
    for (int f = 0; f < 6; ++f) raster_entity_quad(t, rgba, w, h, q[f]);
    if (rec) raster_rec_restore(held);
}

/* ------------------------------------------------------------------ xforms
 * A 3x4 affine matrix in GL's column-vector convention: p' = M p, built by
 * post-multiplying in the order the calls are issued, as glmTranslatef and
 * glRotatef do. rotatef carries Mesa's float conversion (degrees are rounded
 * to float after multiplying by PI/180 in double) so the angles match the
 * oracle's software GL. */
struct xf
{
    float m[12]; /* rows: out = m[0]*x + m[1]*y + m[2]*z + m[3], then m[4..7], m[8..11] */
};

static void xf_mul(struct xf *dst, const struct xf *a, const struct xf *b)
{
    struct xf r;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
            r.m[i * 4 + j] = a->m[i * 4 + 0] * b->m[0 * 4 + j] +
                             a->m[i * 4 + 1] * b->m[1 * 4 + j] +
                             a->m[i * 4 + 2] * b->m[2 * 4 + j];
        r.m[i * 4 + 3] = a->m[i * 4 + 0] * b->m[3] + a->m[i * 4 + 1] * b->m[7] +
                         a->m[i * 4 + 2] * b->m[11] + a->m[i * 4 + 3];
    }
    *dst = r;
}

static void xf_ident(struct xf *x)
{
    memset(x, 0, sizeof *x);
    x->m[0] = x->m[5] = x->m[10] = 1.0f;
}

static void xf_post(struct xf *x, const struct xf *b)
{
    struct xf r;
    xf_mul(&r, x, b);
    *x = r;
}

static void xf_translate(struct xf *x, float tx, float ty, float tz)
{
    struct xf t;
    xf_ident(&t);
    t.m[3] = tx;
    t.m[7] = ty;
    t.m[11] = tz;
    xf_post(x, &t);
}

static void xf_rot_x(struct xf *x, float rad)
{
    float c = cosf(rad), s = sinf(rad);
    struct xf r;
    xf_ident(&r);
    r.m[5] = c;  r.m[6] = -s;
    r.m[9] = s;  r.m[10] = c;
    xf_post(x, &r);
}

static void xf_rot_y(struct xf *x, float rad)
{
    float c = cosf(rad), s = sinf(rad);
    struct xf r;
    xf_ident(&r);
    r.m[0] = c;  r.m[2] = s;
    r.m[8] = -s; r.m[10] = c;
    xf_post(x, &r);
}

static void xf_rot_z(struct xf *x, float rad)
{
    float c = cosf(rad), s = sinf(rad);
    struct xf r;
    xf_ident(&r);
    r.m[0] = c;  r.m[1] = -s;
    r.m[4] = s;  r.m[5] = c;
    xf_post(x, &r);
}

/* Mesa's non-axis-aligned glRotatef: normalize the axis, then the standard
 * formula, all in float. */
static void xf_rot_axis(struct xf *x, float deg, float ax, float ay, float az)
{
    float rad = (float)((double)deg * (3.14159265358979323846 / 180.0));
    float s = sinf(rad), c = cosf(rad);
    float mag = sqrtf(ax * ax + ay * ay + az * az);
    struct xf r;
    if (mag <= 0.0f)
        return;
    ax /= mag;
    ay /= mag;
    az /= mag;
    float xx = ax * ax, yy = ay * ay, zz = az * az;
    float xy = ax * ay, yz = ay * az, zx = az * ax;
    float xs = ax * s, ys = ay * s, zs = az * s;
    float one_c = 1.0f - c;
    xf_ident(&r);
    r.m[0] = one_c * xx + c;       r.m[1] = one_c * xy - zs;      r.m[2] = one_c * zx + ys;
    r.m[4] = one_c * xy + zs;      r.m[5] = one_c * yy + c;       r.m[6] = one_c * yz - xs;
    r.m[8] = one_c * zx - ys;      r.m[9] = one_c * yz + xs;      r.m[10] = one_c * zz + c;
    xf_post(x, &r);
}

static void xf_point(const struct xf *x, const float v[3], float out[3])
{
    out[0] = x->m[0] * v[0] + x->m[1] * v[1] + x->m[2] * v[2] + x->m[3];
    out[1] = x->m[4] * v[0] + x->m[5] * v[1] + x->m[6] * v[2] + x->m[7];
    out[2] = x->m[8] * v[0] + x->m[9] * v[1] + x->m[10] * v[2] + x->m[11];
}

/* The float degrees Java hands glRotatef: rotateAngle * (180F / (float)PI),
 * then Mesa's angle *= PI/180 in double rounded back to float. */
static float gl_rad_from_model_radians(float a)
{
    const float DEG_PER_RAD = 180.0f / 3.14159274101257324219f;
    float deg = a * DEG_PER_RAD;
    return (float)((double)deg * (3.14159265358979323846 / 180.0));
}

/* ModelRenderer.render's part transform: T(rotationPoint) then Rz, Ry, Rx. */
static void xf_part(struct xf *x, float px, float py, float pz, float ax, float ay, float az)
{
    xf_translate(x, px, py, pz);
    xf_rot_z(x, gl_rad_from_model_radians(az));
    xf_rot_y(x, gl_rad_from_model_radians(ay));
    xf_rot_x(x, gl_rad_from_model_radians(ax));
}

static void xf_part_at(struct xf *out, const struct xf *parent, float px, float py, float pz,
                       float ax, float ay, float az)
{
    *out = *parent;
    xf_part(out, px, py, pz, ax, ay, az);
}

/* glScalef(-1,1,1) folded into a part matrix: only the x output row flips. */
static void xf_mirror_x(struct xf *x)
{
    for (int i = 0; i < 4; ++i)
        x->m[i] = -x->m[i];
}

static void emit_box_xf(const struct entity_raster_target *t, const struct mob_render_input *e,
                        const struct xf *outer, const struct xf *pm, const struct mob_box *b,
                        const struct mob_texture *tex, int flip_normal)
{
    float x0 = b->x - b->grow, x1 = b->x + b->dx + b->grow;
    float y0 = b->y - b->grow, y1 = b->y + b->dy + b->grow;
    float z0 = b->z - b->grow, z1 = b->z + b->dz + b->grow;
    if (b->mirror)
    {
        float tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    float raw[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                       {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    float v[8][3];
    for (int i = 0; i < 8; ++i)
    {
        float p[3];
        xf_point(pm, raw[i], p);
        p[0] *= 0.0625f;
        p[1] *= 0.0625f;
        p[2] *= 0.0625f;
        xf_point(outer, p, v[i]);
    }
    emit_box_faces(t, e, v, b->u, b->v, b->dx, b->dy, b->dz, b->mirror, flip_normal, tex);
}

static void draw_box(const struct entity_raster_target *t, const struct mob_render_input *e,
                     const struct mob_model *m, const struct mob_part *p, const struct mob_box *b,
                     const struct mob_texture *tex)
{
    float x0 = b->x - b->grow, x1 = b->x + b->dx + b->grow;
    float y0 = b->y - b->grow, y1 = b->y + b->dy + b->grow;
    float z0 = b->z - b->grow, z1 = b->z + b->dz + b->grow;
    if (b->mirror)
    {
        float tmp = x0;
        x0 = x1;
        x1 = tmp;
    }
    float v[8][3] = {{x0, y0, z0}, {x1, y0, z0}, {x1, y1, z0}, {x0, y1, z0},
                     {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}};
    model_vertices(p, m, e, v, 8);
    if (e->outer_set)
        for (int i = 0; i < 8; ++i)
        {
            const float *o = e->outer;
            float x = v[i][0], y = v[i][1], z = v[i][2];
            for (int r = 0; r < 3; ++r)
                v[i][r] = o[r] * x + o[4 + r] * y + o[8 + r] * z + o[12 + r];
        }
emit_box_faces(t, e, v, b->u, b->v, b->dx, b->dy, b->dz, b->mirror, 0, tex);
}
static int read_texture(const char *assets, int idx)
{
    if (textures[idx].rgba)
        return 1;
int w = texture_size[idx].w, h = texture_size[idx].h;
    char path[1200];
    snprintf(path, sizeof path, "%s/state/%s.rgba", assets, texture_names[idx]);
    FILE *f = fopen(path, "rb");
    if (!f)
    {
        snprintf(path, sizeof path, "out/assets/mobs/%s.rgba", texture_names[idx]);
        f = fopen(path, "rb");
    }
    if (!f)
    {
        snprintf(path, sizeof path, "../out/assets/mobs/%s.rgba", texture_names[idx]);
        f = fopen(path, "rb");
    }
    if (!f)
        return 0;
    unsigned char *px = malloc((size_t)w * h * 4);
    if (!px)
        abort();
    int ok = fread(px, 1, (size_t)w * h * 4, f) == (size_t)w * h * 4;
    fclose(f);
    if (!ok)
    {
        free(px);
        return 0;
    }
    textures[idx] = (struct mob_texture){px, w, h};
    return 1;
}
static void ensure_assets(const char *assets)
{
    if (!assets)
        assets = "";
    if (strcmp(texture_assets, assets))
    {
        if (block_atlas_rgba) rb_atlas_free(&block_atlas);
        free(block_atlas_rgba);
        block_atlas_rgba = NULL;
        for (int i = 0; i < TEX_COUNT; ++i)
        {
            free(textures[i].rgba);
            textures[i] = (struct mob_texture){0};
        }
        snprintf(texture_assets, sizeof texture_assets, "%s", assets);
    }
}
static int ensure_block_atlas(const char *assets)
{
    if (block_atlas_rgba) return 1;
    if (rb_atlas_load(&block_atlas, assets) != 0) return 0;
    char path[1200];
    snprintf(path, sizeof path, "%s/atlas.rgba", assets);
    FILE *f = fopen(path, "rb");
    if (!f) { rb_atlas_free(&block_atlas); return 0; }
    block_atlas_rgba = malloc(512 * 256 * 4);
    if (!block_atlas_rgba) abort();
    int ok = fread(block_atlas_rgba, 1, 512 * 256 * 4, f) == 512 * 256 * 4;
    fclose(f);
    if (ok) texanim_patch_scene(assets, TEXANIM_BLOCKS, block_atlas_rgba, 512, 256);
    if (!ok) { free(block_atlas_rgba); block_atlas_rgba = NULL; rb_atlas_free(&block_atlas); }
    return ok;
}
static const struct rb_uv *block_icon(const struct rb_table *table, int id, int meta, int side)
{
    if (!table || id <= 0 || id >= RB_IDS) return NULL;
    int idx = table->icon_index[(id * RB_METAS + (meta & 15)) * RB_SIDES + side];
    if (idx == RB_NO_ICON || idx >= table->n_icons) return NULL;
    const char *name = table->icon_name[idx];
    for (int i = 0; i < block_atlas.n; ++i)
        if (!strcmp(name, block_atlas.name[i])) return &block_atlas.uv[i];
    return NULL;
}
static void draw_carried_block(const char *assets, const struct entity_raster_target *t,
                               const struct mob_render_input *e, const struct rb_table *table)
{
    if (!e->carried_id || !ensure_block_atlas(assets)) return;
    /* RenderEnderman.renderEquippedItems, followed by RenderBlocks.renderBlockAsItem. */
    float v[8][3];
    for (int i = 0; i < 8; ++i)
    {
        float q[3] = {(i & 1 ? 1.0f : 0.0f) - 0.5f,
                      (i & 2 ? 1.0f : 0.0f) - 0.5f,
                      (i & 4 ? 1.0f : 0.0f) - 0.5f};
        rotate_y(q, PI / 2);
        q[0] *= -0.5f; q[1] *= -0.5f; q[2] *= 0.5f;
        rotate_y(q, PI / 4);
        rotate_x(q, 20.0f * PI / 180.0f);
        q[1] += 0.6875f; q[2] -= 0.75f;
        q[0] = -q[0]; q[1] = 1.5078125f - q[1];
        rotate_y(q, (180.0f - e->body_yaw) * PI / 180.0f);
        memcpy(v[i], q, sizeof q);
    }
    static const int faces[6][4] = {
        {0,1,5,4}, {2,6,7,3}, {0,2,3,1},
        {4,5,7,6}, {0,4,6,2}, {1,3,7,5}
    };
    for (int side = 0; side < 6; ++side)
    {
        const struct rb_uv *uv = block_icon(table, e->carried_id, e->carried_meta, side);
        if (!uv) continue;
        struct entity_clip_vertex q[4];
        float a[3], b[3], n[3];
        for (int j = 0; j < 3; ++j)
        {
            a[j] = v[faces[side][1]][j] - v[faces[side][0]][j];
            b[j] = v[faces[side][2]][j] - v[faces[side][1]][j];
        }
        n[0] = a[1]*b[2] - a[2]*b[1];
        n[1] = a[2]*b[0] - a[0]*b[2];
        n[2] = a[0]*b[1] - a[1]*b[0];
        float len = sqrtf(n[0]*n[0]+n[1]*n[1]+n[2]*n[2]);
        float d = len ? diffuse(n[0]/len,n[1]/len,n[2]/len) : 1;
        for (int j = 0; j < 4; ++j)
            q[j] = clip_vertex(t, e, v[faces[side][3-j]],
                               j == 0 || j == 3 ? uv->min_u : uv->max_u,
                               j < 2 ? uv->min_v : uv->max_v, d);
        raster_entity_quad(t, block_atlas_rgba, 512, 256, q);
    }
}
static void draw_model(const struct entity_raster_target *t, const struct mob_render_input *e,
                       const struct mob_texture *tex, int wool)
{
    struct mob_model m;
    pose_model(&m, e, wool);
    for (int i = 0; i < m.npart; ++i)
        for (int j = 0; j < m.part[i].nbox; ++j)
            draw_box(t, e, &m, &m.part[i], &m.part[i].box[j], tex);
}

/* ------------------------------------------------------------ ModelDragon
 * ModelDragon's boxes in raw model units, in the order addBox() calls them.
 * The head's first "scale" and "nostril" are added while mirror is set. */
#define DRB(x, y, z, dx, dy, dz, u, v, mir) {(x), (y), (z), 0, (dx), (dy), (dz), (u), (v), (mir)}
struct dr_boxdef
{
    int nbox;
    struct mob_box box[6];
};
static const struct dr_boxdef DR_HEAD = {6, {
    DRB(-6,-1,-24, 12,5,16, 176,44, 0), DRB(-8,-8,-10, 16,16,16, 112,30, 0),
    DRB(-5,-12,-4, 2,4,6, 0,0, 1),     DRB(-5,-3,-22, 2,2,4, 112,0, 1),
    DRB(3,-12,-4, 2,4,6, 0,0, 0),      DRB(3,-3,-22, 2,2,4, 112,0, 0)}};
static const struct dr_boxdef DR_JAW = {1, {DRB(-6,0,-16, 12,4,16, 176,65, 0)}};
static const struct dr_boxdef DR_SPINE = {2, {
    DRB(-5,-5,-5, 10,10,10, 192,104, 0), DRB(-1,-9,-3, 2,4,6, 48,0, 0)}};
static const struct dr_boxdef DR_BODY = {4, {
    DRB(-12,0,-16, 24,24,64, 0,0, 0), DRB(-1,-6,-10, 2,6,12, 220,53, 0),
    DRB(-1,-6,10, 2,6,12, 220,53, 0), DRB(-1,-6,30, 2,6,12, 220,53, 0)}};
static const struct dr_boxdef DR_WING = {2, {
    DRB(-56,-4,-4, 56,8,8, 112,88, 0), DRB(-56,0,2, 56,0,56, -56,88, 0)}};
static const struct dr_boxdef DR_WINGTIP = {2, {
    DRB(-56,-2,-2, 56,4,4, 112,136, 0), DRB(-56,0,2, 56,0,56, -56,144, 0)}};
static const struct dr_boxdef DR_FRONTLEG = {1, {DRB(-4,-4,-4, 8,24,8, 112,104, 0)}};
static const struct dr_boxdef DR_FRONTLEGTIP = {1, {DRB(-3,-1,-3, 6,24,6, 226,138, 0)}};
static const struct dr_boxdef DR_FRONTFOOT = {1, {DRB(-4,0,-12, 8,4,16, 144,104, 0)}};
static const struct dr_boxdef DR_REARLEG = {1, {DRB(-8,-4,-8, 16,32,16, 0,0, 0)}};
static const struct dr_boxdef DR_REARLEGTIP = {1, {DRB(-6,-2,0, 12,32,12, 196,0, 0)}};
static const struct dr_boxdef DR_REARFOOT = {1, {DRB(-9,0,-20, 18,6,24, 112,0, 0)}};

static void xf_rot_x_deg(struct xf *x, float deg)
{
    xf_rot_x(x, (float)((double)deg * (3.14159265358979323846 / 180.0)));
}
static void xf_rot_y_deg(struct xf *x, float deg)
{
    xf_rot_y(x, (float)((double)deg * (3.14159265358979323846 / 180.0)));
}
static void xf_rot_z_deg(struct xf *x, float deg)
{
    xf_rot_z(x, (float)((double)deg * (3.14159265358979323846 / 180.0)));
}
/* glScalef post-multiplies: the columns of the linear part scale, the
 * translation column does not. */
static void xf_scale(struct xf *x, float sx, float sy, float sz)
{
    for (int r = 0; r < 3; ++r)
    {
        x->m[r * 4 + 0] *= sx;
        x->m[r * 4 + 1] *= sy;
        x->m[r * 4 + 2] *= sz;
    }
}

/* ModelDragon.updateRotations. */
static float update_rotations(double a)
{
    while (a >= 180.0) a -= 360.0;
    while (a < -180.0) a += 360.0;
    return (float)a;
}

static void dr_emit(const struct entity_raster_target *t, const struct mob_render_input *e,
                    const struct xf *outer, const struct xf *pm, const struct dr_boxdef *d,
                    const struct mob_texture *tex, int flip)
{
    for (int i = 0; i < d->nbox; ++i)
        emit_box_xf(t, e, outer, pm, &d->box[i], tex, flip);
}

/* The matrix the model hangs off: RendererLivingEntity.doRender's
 * scale(-1,-1,1) and translate(0,-1.5078125,0), inside RenderDragon's
 * rotateCorpse (Ry(-offsets(7)[0]), Rx(pitch*10), translate(0,0,1), the death
 * rotation) and inside RenderManager's translate to the entity position. */
static void dr_base_matrix(struct xf *out, const struct mob_render_input *e)
{
    xf_ident(out);
    xf_rot_y_deg(out, -(float)e->off[7][0]);
    xf_rot_x_deg(out, (float)(e->off[5][1] - e->off[10][1]) * 10.0f);
    xf_translate(out, 0, 0, 1);
    if (e->death > 0)
    {
        float v = ((float)e->death + e->partial_tick - 1.0f) / 20.0f * 1.6f;
        v = sqrtf(v > 0 ? v : 0);
        if (v > 1.0f) v = 1.0f;
        xf_rot_z_deg(out, v * 90.0f);
    }
    xf_scale(out, -1.0f, -1.0f, 1.0f);
    xf_translate(out, 0, -1.5078125f, 0);
}

/* ModelDragon.render(scale). `mirror` negates x for the second wing pass. */
static void dragon_draw_model(const struct entity_raster_target *t,
                              const struct mob_render_input *e, const struct mob_texture *tex)
{
    const float PIF = 3.1415927f;
    float a9 = e->anim;
    float jaw_x = (float)(fd_sin((double)(a9 * PIF * 2.0f)) + 1.0) * 0.2f;
    float a10 = (float)(fd_sin((double)(a9 * PIF * 2.0f - 1.0f)) + 1.0);
    a10 = (a10 * a10 * 1.0f + a10 * 2.0f) * 0.05f;
    const float var14 = 1.5f;

    struct xf base;
    dr_base_matrix(&base, e);
    struct xf outer = base;
    xf_translate(&outer, 0, a10 - 2.0f, -3.0f);
    xf_rot_x_deg(&outer, a10 * 2.0f);
    /* The part chains run from the model root (identity): the outer matrix is
     * applied once, in emit_box_xf, after the 1/16 model scale. */
    struct xf root;
    xf_ident(&root);

    const double *var15 = e->off[6];
    float var16 = update_rotations(e->off[5][0] - e->off[10][0]);
    float var17 = update_rotations(e->off[5][0] + (double)(var16 / 2.0f));
    float var18 = a9 * PIF * 2.0f;
    float var11 = 20.0f, var12 = -12.0f, var13 = 0.0f;

    for (int i = 0; i < 5; ++i)
    {
        const double *o = e->off[5 - i];
        float var21 = (float)fd_cos((double)((float)i * 0.45f + var18)) * 0.15f;
        float ay = update_rotations(o[0] - var15[0]) * PIF / 180.0f * var14;
        float ax = var21 + (float)(o[1] - var15[1]) * PIF / 180.0f * var14 * 5.0f;
        float az = -update_rotations(o[0] - (double)var17) * PIF / 180.0f * var14;
        struct xf pm;
        xf_part_at(&pm, &root, var13, var11, var12, ax, ay, az);
        dr_emit(t, e, &outer, &pm, &DR_SPINE, tex, 0);
        var11 = (float)((double)var11 + fd_sin((double)ax) * 10.0);
        var12 = (float)((double)var12 - fd_cos((double)ay) * fd_cos((double)ax) * 10.0);
        var13 = (float)((double)var13 - fd_sin((double)ay) * fd_cos((double)ax) * 10.0);
    }

    {
        const double *o = e->off[0];
        float ay = update_rotations(o[0] - var15[0]) * PIF / 180.0f * 1.0f;
        float az = -update_rotations(o[0] - (double)var17) * PIF / 180.0f * 1.0f;
        struct xf hpm;
        xf_part_at(&hpm, &root, var13, var11, var12, 0, ay, az);
        dr_emit(t, e, &outer, &hpm, &DR_HEAD, tex, 0);
        struct xf jpm = hpm;
        xf_part(&jpm, 0, 4, -8, jaw_x, 0, 0);
        dr_emit(t, e, &outer, &jpm, &DR_JAW, tex, 0);
    }

    /* The body and the two wings share the wing matrix and its mirror. */
    struct xf wing_outer = outer;
    xf_translate(&wing_outer, 0, 1, 0);
    xf_rot_z_deg(&wing_outer, -var16 * var14 * 1.0f);
    xf_translate(&wing_outer, 0, -1, 0);
    {
        struct xf pm;
        xf_part_at(&pm, &root, 0, 4, 8, 0, 0, 0);
        dr_emit(t, e, &wing_outer, &pm, &DR_BODY, tex, 0);
    }
    for (int k = 0; k < 2; ++k)
    {
        float var21 = a9 * PIF * 2.0f;
        float wax = 0.125f - (float)fd_cos((double)var21) * 0.2f;
        float waz = (float)(fd_sin((double)var21) + 0.125) * 0.8f;
        float wtaz = -((float)(fd_sin((double)(var21 + 2.0f)) + 0.5)) * 0.75f;
        float rlax = 1.0f + a10 * 0.1f;
        float rltax = 0.5f + a10 * 0.1f;
        float rfax = 0.75f + a10 * 0.1f;
        float flax = 1.3f + a10 * 0.1f;
        float fltax = -0.5f - a10 * 0.1f;
        float ffax = 0.75f + a10 * 0.1f;
        int mir = k == 1;
        struct xf wpm, w2;
        xf_part_at(&wpm, &root, -12, 5, 2, wax, 0.25f, waz);
        if (mir) xf_mirror_x(&wpm);
        xf_part_at(&w2, &wpm, -56, 0, 0, 0, 0, wtaz);
        /* the loop enables GL_CULL_FACE (the rest of the model draws with it
         * off): the wing membrane is a box of height 0, whose back face
         * would tie the front one in depth. GL11.glCullFace(GL_FRONT) for the
         * mirrored pass: the reflection flips the winding, so the front
         * faces are the ones to keep. */
        struct entity_raster_target mt = *t;
        mt.two_sided = 0;
        if (mir) mt.cull_front = 1;
        const struct entity_raster_target *tk = &mt;
        dr_emit(tk, e, &wing_outer, &wpm, &DR_WING, tex, mir);
        dr_emit(tk, e, &wing_outer, &w2, &DR_WINGTIP, tex, mir);
        struct xf fl, flt, flf, rl, rlt, rlf;
        xf_part_at(&fl, &root, -12, 20, 2, flax, 0, 0);
        xf_part_at(&flt, &fl, 0, 20, -1, fltax, 0, 0);
        xf_part_at(&flf, &flt, 0, 23, 0, ffax, 0, 0);
        xf_part_at(&rl, &root, -16, 16, 42, rlax, 0, 0);
        xf_part_at(&rlt, &rl, 0, 32, -4, rltax, 0, 0);
        xf_part_at(&rlf, &rlt, 0, 31, 4, rfax, 0, 0);
        if (mir)
        {
            xf_mirror_x(&fl); xf_mirror_x(&flt); xf_mirror_x(&flf);
            xf_mirror_x(&rl); xf_mirror_x(&rlt); xf_mirror_x(&rlf);
        }
        dr_emit(tk, e, &wing_outer, &fl, &DR_FRONTLEG, tex, mir);
        dr_emit(tk, e, &wing_outer, &flt, &DR_FRONTLEGTIP, tex, mir);
        dr_emit(tk, e, &wing_outer, &flf, &DR_FRONTFOOT, tex, mir);
        dr_emit(tk, e, &wing_outer, &rl, &DR_REARLEG, tex, mir);
        dr_emit(tk, e, &wing_outer, &rlt, &DR_REARLEGTIP, tex, mir);
        dr_emit(tk, e, &wing_outer, &rlf, &DR_REARFOOT, tex, mir);
    }

    {
        float var24 = -(float)fd_sin((double)(a9 * PIF * 2.0f)) * 0.0f;
        float v24 = var24;
        var18 = a9 * PIF * 2.0f;
        var11 = 10.0f;
        var12 = 60.0f;
        var13 = 0.0f;
        const double *o11 = e->off[11];
        for (int i = 0; i < 12; ++i)
        {
            const double *o = e->off[12 + i];
            v24 = (float)((double)v24 + fd_sin((double)((float)i * 0.45f + var18)) * 0.05000000074505806);
            float ay = (update_rotations(o[0] - o11[0]) * var14 + 180.0f) * PIF / 180.0f;
            float ax = v24 + (float)(o[1] - o11[1]) * PIF / 180.0f * var14 * 5.0f;
            float az = update_rotations(o[0] - (double)var17) * PIF / 180.0f * var14;
            struct xf pm;
            xf_part_at(&pm, &root, var13, var11, var12, ax, ay, az);
            dr_emit(t, e, &outer, &pm, &DR_SPINE, tex, 0);
            var11 = (float)((double)var11 + fd_sin((double)ax) * 10.0);
            var12 = (float)((double)var12 - fd_cos((double)ay) * fd_cos((double)ax) * 10.0);
            var13 = (float)((double)var13 - fd_sin((double)ay) * fd_cos((double)ax) * 10.0);
        }
    }
}

/* RenderDragon.renderEquippedItems: the 200-tick explosion, new Random(432L). */
static void dragon_death_beams(const struct entity_raster_target *t,
                               const struct mob_render_input *e)
{
    static const unsigned char white[4] = {255, 255, 255, 255};
    float var4 = ((float)e->death_ticks + e->partial_tick) / 200.0f;
    float var5 = var4 > 0.8f ? (var4 - 0.8f) / 0.2f : 0.0f;
    jrand r;
    jr_seed(&r, 432L);
    struct xf base;
    dr_base_matrix(&base, e);
    struct xf m = base;
    xf_translate(&m, 0, -1, -2);
    int count = (int)ceilf((var4 + var4 * var4) / 2.0f * 60.0f);
    for (int i = 0; i < count; ++i)
    {
        xf_rot_x_deg(&m, jr_float(&r) * 360.0f);
        xf_rot_y_deg(&m, jr_float(&r) * 360.0f);
        xf_rot_z_deg(&m, jr_float(&r) * 360.0f);
        xf_rot_x_deg(&m, jr_float(&r) * 360.0f);
        xf_rot_y_deg(&m, jr_float(&r) * 360.0f);
        xf_rot_z_deg(&m, jr_float(&r) * 360.0f + var4 * 90.0f);
        float v8 = jr_float(&r) * 20.0f + 5.0f + var5 * 10.0f;
        float v9 = jr_float(&r) * 2.0f + 1.0f + var5 * 2.0f;
        float alpha = (float)(int)(255.0f * (1.0f - var5));
        struct entity_clip_vertex q[5];
        float pos[5][3] = {{0, 0, 0},
                           {-0.866f * v9, v8, -0.5f * v9},
                           {0.866f * v9, v8, -0.5f * v9},
                           {0, v8, 1.0f * v9},
                           {-0.866f * v9, v8, -0.5f * v9}};
        float col[5][4] = {{1, 1, 1, alpha / 255.0f},
                           {1, 0, 1, 0}, {1, 0, 1, 0}, {1, 0, 1, 0}, {1, 0, 1, 0}};
        for (int k = 0; k < 5; ++k)
        {
            float w[3];
            xf_point(&m, pos[k], w);
            struct entity_raster_target tt = *t;
            tt.vertex_color = 1;
            tt.no_alpha = 1;
            tt.blend = 4;
            tt.two_sided = 0; /* GL_CULL_FACE is on for the explosion */
            tt.linear_depth = 0;
            q[k] = clip_vertex(&tt, e, w, 0.5f, 0.5f, 1.0f);
            memcpy(q[k].color, col[k], sizeof col[k]);
        }
        /* GL_TRIANGLE_FAN of five vertices. */
        for (int k = 1; k + 1 < 5; ++k)
        {
            struct entity_raster_target tt = *t;
            tt.vertex_color = 1;
            tt.no_alpha = 1;
            tt.blend = 4;
            tt.two_sided = 0;
            struct entity_clip_vertex tri[4] = {q[0], q[k], q[k + 1], q[k + 1]};
            raster_entity_quad(&tt, white, 1, 1, tri);
        }
    }
}

/* ----------------------------------------------------- ModelEnderCrystal
 * RenderEnderCrystal.doRender and ModelEnderCrystal.render. */
static void crystal_draw(const char *assets, const struct entity_raster_target *t,
                         const struct mob_render_input *e)
{
    if (!read_texture(assets, TEX_ENDCRYSTAL)) return;
    /* RenderEnderCrystal is not a RendererLivingEntity: GL_CULL_FACE stays on. */
    struct entity_raster_target ct = *t;
    ct.two_sided = 0;
    t = &ct;
    struct mob_box glass = DRB(-4, -4, -4, 8, 8, 8, 0, 0, 0);
    struct mob_box cube = DRB(-4, -4, -4, 8, 8, 8, 32, 0, 0);
    struct mob_box base = DRB(-6, 0, -6, 12, 4, 12, 0, 16, 0);
    struct mob_texture *tex = &textures[TEX_ENDCRYSTAL];
    float var10 = e->crystal_rot;
    float var11 = mh_sin(var10 * 0.2f) / 2.0f + 0.5f;
    var11 += var11 * var11;
    struct xf outer, pm;
    xf_ident(&outer);
    xf_scale(&outer, 2.0f, 2.0f, 2.0f);
    xf_translate(&outer, 0, -0.5f, 0);
    xf_ident(&pm);
    emit_box_xf(t, e, &outer, &pm, &base, tex, 0);
    xf_rot_y_deg(&outer, var10 * 3.0f);
    xf_translate(&outer, 0, 0.8f + var11 * 0.2f, 0);
    xf_rot_axis(&outer, 60.0f, 0.7071f, 0.0f, 0.7071f);
    emit_box_xf(t, e, &outer, &pm, &glass, tex, 0);
    xf_scale(&outer, 0.875f, 0.875f, 0.875f);
    xf_rot_axis(&outer, 60.0f, 0.7071f, 0.0f, 0.7071f);
    xf_rot_y_deg(&outer, var10 * 3.0f);
    emit_box_xf(t, e, &outer, &pm, &glass, tex, 0);
    xf_scale(&outer, 0.875f, 0.875f, 0.875f);
    xf_rot_axis(&outer, 60.0f, 0.7071f, 0.0f, 0.7071f);
    xf_rot_y_deg(&outer, var10 * 3.0f);
    emit_box_xf(t, e, &outer, &pm, &cube, tex, 0);
}

/* RenderLeashKnot.doRender: ModelLeashKnot's one box (6 x 8 x 6 at -3, -6,
 * -3 of a 32 x 32 sheet) under glScalef(-1, -1, 1), GL_CULL_FACE off. */
static void knot_draw(const char *assets, const struct entity_raster_target *t,
                      const struct mob_render_input *e)
{
    if (!read_texture(assets, TEX_LEAD_KNOT)) return;
    struct entity_raster_target kt = *t;
    kt.two_sided = 1;
    struct mob_box knot = DRB(-3, -6, -3, 6, 8, 6, 0, 0, 0);
    struct xf outer, pm;
    xf_ident(&outer);
    xf_scale(&outer, -1.0f, -1.0f, 1.0f);
    xf_ident(&pm);
    emit_box_xf(&kt, e, &outer, &pm, &knot, &textures[TEX_LEAD_KNOT], 0);
}

/* RenderLiving.func_110827_b: two triangle strips of 25 pairs from the body
 * to the holder, sagging (f * f + f) / 2 of the rise and (24 - i) / 18 +
 * 0.125 up, the pairs alternating two browns; texture and lighting off (the
 * lightmap stays), GL_CULL_FACE off. */
static void leash_draw(const struct entity_raster_target *t, const struct mob_render_input *e)
{
    if (!e->leash) return;
    static const unsigned char white[4] = {255, 255, 255, 255};
    struct entity_raster_target lt = *t;
    lt.two_sided = 1;
    lt.unlit = 1;
    lt.vertex_color = 1;
    const float *s0 = e->leash_start, *r = e->leash_run;
    for (int strip = 0; strip < 2; ++strip)
    {
        struct entity_clip_vertex pv[2];
        for (int i = 0; i <= 24; ++i)
        {
            float c[3] = {0.5f, 0.4f, 0.3f};
            if (i % 2) c[0] = 0.35f, c[1] = 0.28f, c[2] = 0.21000001f;
            float f = (float)i / 24.0f;
            double x = (double)s0[0] + (double)r[0] * (double)f;
            double y = (double)s0[1] + (double)r[1] * (double)(f * f + f) * 0.5 + (double)((24.0f - (float)i) / 18.0f + 0.125f);
            double z = (double)s0[2] + (double)r[2] * (double)f;
            float a[3] = {(float)x, (float)(strip ? y + 0.025 : y), (float)z};
            float b[3] = {(float)(x + 0.025), (float)(strip ? y : y + 0.025), (float)(strip ? z + 0.025 : z)};
            struct entity_clip_vertex v[2] = {clip_vertex(&lt, e, a, 0, 0, 1), clip_vertex(&lt, e, b, 0, 0, 1)};
            for (int k = 0; k < 2; ++k)
                for (int q = 0; q < 3; ++q) v[k].color[q] = c[q];
            if (i > 0)
            {
                struct entity_clip_vertex quad[4] = {pv[0], pv[1], v[1], v[0]};
                raster_entity_quad(&lt, white, 1, 1, quad);
            }
            pv[0] = v[0];
            pv[1] = v[1];
        }
    }
}

/* RenderDragon.doRender's healing beam: a triangle strip of 9 pairs. */
static void dragon_beam(const char *assets, const struct entity_raster_target *t,
                        const struct mob_render_input *e)
{
    if (!e->has_beam || !read_texture(assets, TEX_ENDCRYSTAL_BEAM)) return;
    const float PIF = 3.1415927f;
    float dx = e->beam_dx, dy = e->beam_dy, dz = e->beam_dz;
    float var15 = (float)sqrt((double)(dx * dx + dz * dz));
    float var16 = e->beam_len;
    struct xf m;
    xf_ident(&m);
    xf_rot_y_deg(&m, (float)(-fd_atan2((double)dz, (double)dx)) * 180.0f / PIF - 90.0f);
    xf_rot_x_deg(&m, (float)(-fd_atan2((double)var15, (double)dy)) * 180.0f / PIF - 90.0f);
    float ticks = (float)e->beam_ticks + e->partial_tick;
    float var18 = 0.0f - ticks * 0.01f;
    float var19 = var16 / 32.0f - ticks * 0.01f;
    struct entity_raster_target bt = *t;
    bt.two_sided = 1; /* RenderDragon.doRender disables GL_CULL_FACE */
    struct entity_clip_vertex v[18];
    for (int i = 0; i <= 8; ++i)
    {
        float ang = (float)(i % 8) * PIF * 2.0f / 8.0f;
        float var22 = mh_sin(ang) * 0.75f;
        float var23 = mh_cos(ang) * 0.75f;
        float var24 = (float)(i % 8) * 1.0f / 8.0f;
        /* RenderDragon.doRender translates to the entity position plus 2 in y;
         * clip_vertex adds the entity position, so only the +2 is local. */
        float pi[3] = {var22 * 0.2f, var23 * 0.2f, 0.0f};
        float po[3] = {var22, var23, var16};
        float wi[3], wo[3];
        xf_point(&m, pi, wi);
        xf_point(&m, po, wo);
        wi[1] += 2.0f;
        wo[1] += 2.0f;
        v[i * 2] = clip_vertex(&bt, e, wi, var24, var19, 0.0f);
        v[i * 2 + 1] = clip_vertex(&bt, e, wo, var24, var18, 1.0f);
    }
    struct mob_texture *tex = &textures[TEX_ENDCRYSTAL_BEAM];
    for (int i = 0; i + 3 < 18; i += 2)
    {
        struct entity_clip_vertex q[4] = {v[i], v[i + 1], v[i + 2], v[i + 3]};
        raster_entity_quad(&bt, tex->rgba, tex->w, tex->h, q);
    }
}

static void draw_mooshroom_mushrooms(const struct entity_raster_target *t,
                                    const struct mob_render_input *e, const struct mob_texture *tex)
{
    if (e->child) return;
    struct mob_model m;
    pose_model(&m, e, 0);
    struct mob_part root = {0};
    root.parent = -1;
    static const float coords[4][4][3] = {
        {{-.45f,.5f,-.45f},{-.45f,-.5f,-.45f},{.45f,-.5f,.45f},{.45f,.5f,.45f}},
        {{.45f,.5f,.45f},{.45f,-.5f,.45f},{-.45f,-.5f,-.45f},{-.45f,.5f,-.45f}},
        {{-.45f,.5f,.45f},{-.45f,-.5f,.45f},{.45f,-.5f,-.45f},{.45f,.5f,-.45f}},
        {{.45f,.5f,-.45f},{.45f,-.5f,-.45f},{-.45f,-.5f,.45f},{-.45f,.5f,.45f}}
    };
    for (int shroom = 0; shroom < 3; ++shroom)
    {
        for (int face = 0; face < 4; ++face)
        {
            struct entity_clip_vertex q[4];
            for (int k = 0; k < 4; ++k)
            {
                float p[3] = {coords[face][k][0], coords[face][k][1], coords[face][k][2]};
                rotate_y(p, (shroom == 2 ? 12.0f : 42.0f) * PI / 180.0f);
                if (shroom == 1)
                {
                    p[0] += 0.1f; p[2] -= 0.6f;
                    rotate_y(p, 42.0f * PI / 180.0f);
                }
                if (shroom == 2)
                {
                    p[1] += 0.75f; p[2] -= 0.2f;
                }
                else
                {
                    p[0] += 0.2f; p[1] += 0.4f; p[2] += 0.5f;
                }
                p[1] = -p[1];
                for (int j = 0; j < 3; ++j) p[j] *= 16.0f;
                model_vertex(shroom == 2 ? &m.part[0] : &root, &m, e, p);
                float u = (k == 2 || k == 3) ? 1.0f : 0.0f;
                if (face & 1) u = 1.0f - u;
                q[k] = clip_vertex(t, e, p, u, (k == 1 || k == 2) ? 1.0f : 0.0f, 0.4f);
            }
            raster_entity_quad(t, tex->rgba, 16, 16, q);
        }
    }
}
static void draw_shadow(const char *assets, const struct entity_raster_target *t,
                        const struct mob_render_input *e, const struct rb_world *world,
                        const struct rb_table *table, const float light_brightness[16])
{
    if (!world || !table || !light_brightness || !read_texture(assets, 7))
        return;
    float radius = e->kind == MOB_CHICKEN ? 0.3f :
                   e->kind == MOB_SPIDER || e->kind == MOB_CAVE_SPIDER ? 1.0f :
                   e->kind == MOB_SILVERFISH ? 0.3f :
                   e->kind == MOB_MAGMA_CUBE || e->kind == MOB_BAT ? 0.25f :
                   e->kind == MOB_PIG || e->kind == MOB_COW || e->kind == MOB_SHEEP ||
                   e->kind == MOB_MOOSHROOM || e->kind == MOB_SQUID ? 0.7f : 0.5f;
    if (e->child)
        radius *= 0.5f;
    /* Render.renderShadow reads posY + getShadowSize (height / 2); the
     * player's posY is its eye, yOffset over the feet the model stands on */
    double shadow_y = e->y + (e->kind == MOB_PLAYER ? (double)e->y_offset : 0.0) +
                      (e->kind == MOB_ENDER_CRYSTAL ? 0.0f : e->height * 0.5f);
    double dx = e->x - t->cam[0], dy = e->y - t->cam[1], dz = e->z - t->cam[2];
    float opacity = 1.0f - (float)(sqrt(dx * dx + dy * dy + dz * dz) / 256.0);
    if (opacity <= 0)
        return;
    for (int bx = (int)floor(e->x - radius); bx <= (int)floor(e->x + radius); ++bx)
        for (int by = (int)floor(shadow_y - radius); by <= (int)floor(shadow_y); ++by)
            for (int bz = (int)floor(e->z - radius); bz <= (int)floor(e->z + radius); ++bz)
            {
                int id = rb_world_block(world, bx, by - 1, bz);
                if (id <= 0 || id >= RB_IDS || !table->props[id].render_as_normal)
                    continue;
                int light = rb_world_sky(world, bx, by, bz);
                int block = rb_world_blocklight(world, bx, by, bz);
                if (block > light)
                    light = block;
                if (light <= 3)
                    continue;
                if (light > 15)
                    light = 15;
                float alpha =
                    (opacity - (float)(shadow_y - by) * 0.5f) * 0.5f * light_brightness[light];
                if (alpha <= 0)
                    continue;
                if (alpha > 1)
                    alpha = 1;
                struct entity_raster_target target = *t;
                target.blend = 1;
                target.alpha = alpha;
                target.clamp_texture = 1;
                float u0 = (float)((e->x - bx) / (2.0 * radius) + 0.5);
                float u1 = (float)((e->x - bx - 1.0) / (2.0 * radius) + 0.5);
                float v0 = (float)((e->z - bz) / (2.0 * radius) + 0.5);
                float v1 = (float)((e->z - bz - 1.0) / (2.0 * radius) + 0.5);
                float verts[4][3] = {
                    {(float)(bx - e->x), (float)(by + 0.015625 - e->y), (float)(bz - e->z)},
                    {(float)(bx - e->x), (float)(by + 0.015625 - e->y), (float)(bz + 1 - e->z)},
                    {(float)(bx + 1 - e->x), (float)(by + 0.015625 - e->y), (float)(bz + 1 - e->z)},
                    {(float)(bx + 1 - e->x), (float)(by + 0.015625 - e->y), (float)(bz - e->z)}};
                float uv[4][2] = {{u0, v0}, {u0, v1}, {u1, v1}, {u1, v0}};
                struct entity_clip_vertex q[4];
                for (int k = 0; k < 4; ++k)
                    q[k] = clip_vertex(t, e, verts[k], uv[k][0], uv[k][1], 1);
                raster_entity_quad(&target, textures[7].rgba, 64, 64, q);
            }
}
/* RenderDragon.renderModel, RendererLivingEntity's pass loop and
 * RenderDragon.doRender, in vanilla's order. */
static void dragon_draw(const char *assets, const struct entity_raster_target *t,
                        const struct mob_render_input *e)
{
    if (!read_texture(assets, TEX_DRAGON)) return;
    if (e->death_ticks > 0 && read_texture(assets, TEX_DRAGON_EXPLODING))
    {
        struct entity_raster_target a = *t;
        a.alpha_ref = (float)e->death_ticks / 200.0f;
        dragon_draw_model(&a, e, &textures[TEX_DRAGON_EXPLODING]);
        struct entity_raster_target b = *t;
        b.depth_equal = 1;
        dragon_draw_model(&b, e, &textures[TEX_DRAGON]);
    }
    else
    {
        dragon_draw_model(t, e, &textures[TEX_DRAGON]);
    }
    if (e->hurt > 0)
    {
        /* RenderDragon.renderModel's own hurt pass, GL_EQUAL at 50% red:
         * only GL_TEXTURE_2D on unit 0 is off, so the lighting and the
         * lightmap still shade the red */
        struct entity_raster_target h = *t;
        h.overlay = 1;
        h.overlay_alpha = 0.5f;
        h.overlay_brightness = 1.0f;
        h.overlay_lightmap = 1;
        h.depth_equal = 1;
        h.two_sided = 0;
        dragon_draw_model(&h, e, &textures[TEX_DRAGON]);
    }
    if (read_texture(assets, TEX_DRAGON_EYES))
    {
        /* shouldRenderPass(0): additive eyes, GL_EQUAL, full block light. */
        struct entity_raster_target ey = *t;
        ey.blend = 2;
        ey.depth_equal = 1;
        ey.two_sided = 0;
        struct mob_render_input lit = *e;
        lit.brightness = 61680;
        dragon_draw_model(&ey, &lit, &textures[TEX_DRAGON_EYES]);
    }
    if (e->death_ticks > 0)
        dragon_death_beams(t, e);
    if (e->hurt > 0 || e->death > 0)
    {
        /* RendererLivingEntity's hurt flash: GL_EQUAL at 40%, drawn once for
         * the model and again per inheritRenderPass. shouldRenderPass(0) is
         * the dragon's one pass, and it leaves glBlendFunc(GL_ONE, GL_ONE):
         * the second draw of the same colour adds, its alpha unused. */
        struct entity_raster_target hb = *t;
        hb.overlay = 1;
        hb.overlay_alpha = 0.4f;
        hb.overlay_brightness = e->brightness_scalar;
        hb.depth_equal = 1;
        hb.two_sided = 0;
        dragon_draw_model(&hb, e, &textures[TEX_DRAGON]);
        hb.overlay_alpha = 1.0f;
        hb.blend = 2;
        dragon_draw_model(&hb, e, &textures[TEX_DRAGON]);
    }
    dragon_beam(assets, t, e);
}

/* ------------------------------------------------------------ equipment
 * lane/entrender: RenderBiped's armour passes and held item, RenderWitch's
 * item, RenderIronGolem's rose and RenderBiped's helmet block. The armour
 * models pose through the same model_vertex path as the body; the held items
 * draw through raster_glx with the GL matrix RendererLivingEntity.doRender
 * builds, so ItemRenderer.renderItem runs as it does for the hand. */

static int is_biped(const struct mob_render_input *e)
{
    return e->kind == MOB_ZOMBIE || e->kind == MOB_PIGMAN || e->kind == MOB_SKELETON ||
           e->kind == MOB_WITHER_SKELETON || e->kind == MOB_PLAYER;
}

/* ModelBiped.setRotationAngles for the armour models, then ModelZombie's arm
 * override when ZOMBIE_ARMS (RenderZombie's armour models are ModelZombie and
 * ModelZombieVillager; RenderSkeleton's are plain ModelBiped). */
static void pose_armor(struct mob_model *m, const struct mob_render_input *e, float grow,
                       int zombie_arms, int zvil, int held)
{
    memset(m, 0, sizeof *m);
    float limb = e->limb * (e->child ? 3 : 1), amount = fminf(e->limb_amount, 1);
    float age = (float)e->age + e->partial_tick;
    float swing = e->swing;
    struct mob_part *head = part(m, 0, 0, 0, 1);
    if (zvil)
        box(head, -4, -10, -4, 8, 6, 8, 0, 0, grow, 0);
    else
        box(head, -4, -8, -4, 8, 8, 8, 0, 0, grow, 0);
    struct mob_part *body = part(m, 0, 0, 0, 0);
    box(body, -4, 0, -2, 8, 12, 4, 16, 16, grow, 0);
    struct mob_part *rarm = part(m, -5, 2, 0, 0);
    box(rarm, -3, -2, -2, 4, 12, 4, 40, 16, grow, 0);
    struct mob_part *larm = part(m, 5, 2, 0, 0);
    box(larm, -1, -2, -2, 4, 12, 4, 40, 16, grow, 1);
    struct mob_part *rleg = part(m, -1.9f, 12, 0.1f, 0);
    box(rleg, -2, 0, -2, 4, 12, 4, 0, 16, grow, 0);
    struct mob_part *lleg = part(m, 1.9f, 12, 0.1f, 0);
    box(lleg, -2, 0, -2, 4, 12, 4, 0, 16, grow, 1);
    struct mob_part *hw = part(m, 0, 0, 0, 1);
    box(hw, -4, -8, -4, 8, 8, 8, 32, 0, grow + 0.5f, 0);
    float hx = e->pitch / (180.0f / PI), hy = (e->head_yaw - e->body_yaw) / (180.0f / PI);
    head->ay = hw->ay = hy;
    head->ax = hw->ax = hx;
    rarm->ax = mh_cos(limb * 0.6662f + PI) * 2.0f * amount * 0.5f;
    larm->ax = mh_cos(limb * 0.6662f) * 2.0f * amount * 0.5f;
    rarm->az = larm->az = 0;
    rleg->ax = mh_cos(limb * 0.6662f) * 1.4f * amount;
    lleg->ax = mh_cos(limb * 0.6662f + PI) * 1.4f * amount;
    if (held) rarm->ax = rarm->ax * 0.5f - (PI / 10.0f) * 1.0f;
    rarm->ay = larm->ay = 0;
    body->ay = mh_sin(sqrtf(swing) * PI * 2.0f) * 0.2f;
    rarm->z = mh_sin(body->ay) * 5.0f;
    rarm->x = -mh_cos(body->ay) * 5.0f;
    larm->z = -mh_sin(body->ay) * 5.0f;
    larm->x = mh_cos(body->ay) * 5.0f;
    rarm->ay += body->ay;
    larm->ay += body->ay;
    larm->ax += body->ay;
    float v8 = 1.0f - swing;
    v8 *= v8;
    v8 *= v8;
    v8 = 1.0f - v8;
    float v9 = mh_sin(v8 * PI);
    float v10 = mh_sin(swing * PI) * -(head->ax - 0.7f) * 0.75f;
    rarm->ax = (float)((double)rarm->ax - ((double)v9 * 1.2 + (double)v10));
    rarm->ay += body->ay * 2.0f;
    rarm->az = mh_sin(swing * PI) * -0.4f;
    rarm->az += mh_cos(age * 0.09f) * 0.05f + 0.05f;
    larm->az -= mh_cos(age * 0.09f) * 0.05f + 0.05f;
    rarm->ax += mh_sin(age * 0.067f) * 0.05f;
    larm->ax -= mh_sin(age * 0.067f) * 0.05f;
    if (zombie_arms)
    {
        float s8 = mh_sin(swing * PI);
        float s9 = mh_sin((1.0f - (1.0f - swing) * (1.0f - swing)) * PI);
        rarm->az = larm->az = 0.0f;
        rarm->ay = -(0.1f - s8 * 0.6f);
        larm->ay = 0.1f - s8 * 0.6f;
        rarm->ax = larm->ax = -(PI / 2.0f);
        rarm->ax -= s8 * 1.2f - s9 * 0.4f;
        larm->ax -= s8 * 1.2f - s9 * 0.4f;
        rarm->az += mh_cos(age * 0.09f) * 0.05f + 0.05f;
        larm->az -= mh_cos(age * 0.09f) * 0.05f + 0.05f;
        rarm->ax += mh_sin(age * 0.067f) * 0.05f;
        larm->ax -= mh_sin(age * 0.067f) * 0.05f;
    }
}

static const char *const armor_names[5] = {"leather", "chainmail", "iron", "diamond", "gold"};

/* One armour pass's model at 64x32 (RenderBiped.shouldRenderPass): pass 0 the
 * helmet (head and headwear), 1 the chestplate (body and arms), 2 the
 * leggings on the 0.5 model (body and legs), 3 the boots (legs). */
static void draw_armor_model(const struct entity_raster_target *t, const struct mob_render_input *e,
                             int pass, const struct mob_texture *tex)
{
    struct mob_model m;
    int zombie_arms = e->kind == MOB_ZOMBIE || e->kind == MOB_PIGMAN;
    if (e->kind == MOB_PLAYER)
    {
        /* RenderPlayer's modelArmorChestplate (1.0) and modelArmor (0.5),
         * posed as the main model */
        memset(&m, 0, sizeof m);
        model_player(&m, e, pass == 2 ? 0.5f : 1.0f);
    }
    else
        pose_armor(&m, e, pass == 2 ? 0.5f : 1.0f, zombie_arms, e->kind == MOB_ZOMBIE && e->zombie_villager,
                   e->equip[0].id != 0);
    static const int show[4][7] = {{1, 0, 0, 0, 0, 0, 1}, {0, 1, 1, 1, 0, 0, 0},
                                   {0, 1, 0, 0, 1, 1, 0}, {0, 0, 0, 0, 1, 1, 0}};
    for (int i = 0; i < m.npart; ++i)
        if (show[pass][i])
            for (int j = 0; j < m.part[i].nbox; ++j)
                draw_box(t, e, &m, &m.part[i], &m.part[i].box[j], tex);
}

/* The armour loop of RendererLivingEntity.doRender over RenderBiped's
 * shouldRenderPass: the tinted leather pass and its overlay, the glint's two
 * texture-matrix passes on an enchanted piece. */
static void draw_armor(const char *assets, const struct entity_raster_target *t,
                       const struct mob_render_input *e)
{
    for (int pass = 0; pass < 4; ++pass)
    {
        const struct thing_item *it = &e->equip[4 - pass];
        if (!it->id || !it->armor || it->armor_index < 0 || it->armor_index > 4) continue;
        char name[64];
        snprintf(name, sizeof name, "armor_%s_%d", armor_names[it->armor_index], pass == 2 ? 2 : 1);
        const unsigned char *px = thing_texture(assets, name, 64, 32);
        if (!px) continue;
        struct mob_texture tex = {(unsigned char *)px, 64, 32};
        struct entity_raster_target a = *t;
        if (it->cloth)
        {
            a.material[0] = (float)(it->color >> 16 & 255) / 255.0f;
            a.material[1] = (float)(it->color >> 8 & 255) / 255.0f;
            a.material[2] = (float)(it->color & 255) / 255.0f;
        }
        draw_armor_model(&a, e, pass, &tex);
        if (it->cloth)
        {
            snprintf(name, sizeof name, "armor_leather_%d_overlay", pass == 2 ? 2 : 1);
            const unsigned char *ov = thing_texture(assets, name, 64, 32);
            if (ov)
            {
                struct mob_texture otex = {(unsigned char *)ov, 64, 32};
                struct entity_raster_target o = *t;
                draw_armor_model(&o, e, pass, &otex);
            }
        }
        if (it->eff)
        {
            const unsigned char *glint = thing_texture(assets, "gui_glint", 64, 64);
            if (!glint) continue;
            float v19 = (float)e->age + e->partial_tick;
            for (int i = 0; i < 2; ++i)
            {
                struct entity_raster_target gl = *t;
                gl.unlit = 1;
                gl.blend = 5;
                gl.depth_equal = 1;
                float v22 = 0.76f;
                gl.material[0] = 0.5f * v22;
                gl.material[1] = 0.25f * v22;
                gl.material[2] = 0.8f * v22;
                /* glScalef(1/3), glRotatef(30 - 60 i, z), glTranslatef(0, v23, 0) */
                float tm[16];
                rs_gl_identity(tm);
                float v23 = v19 * (0.001f + (float)i * 0.003f) * 20.0f;
                float v24 = 0.33333334f;
                rs_gl_scale(tm, v24, v24, v24);
                rs_gl_rotate(tm, 30.0f - (float)i * 60.0f, 0.0f, 0.0f, 1.0f);
                rs_gl_translate(tm, 0.0f, v23, 0.0f);
                gl.tex_matrix = 1;
                gl.tm[0] = tm[0]; gl.tm[1] = tm[1];
                gl.tm[2] = tm[4]; gl.tm[3] = tm[5];
                gl.tm[4] = tm[12]; gl.tm[5] = tm[13];
                gl.sample_tex = glint;
                gl.sample_w = gl.sample_h = 64;
                draw_armor_model(&gl, e, pass, &tex);
            }
        }
    }
}

/* The hurt flash over the armour: inheritRenderPass re-renders each pass the
 * red overlay reaches. */
static void draw_armor_overlay(const char *assets, const struct entity_raster_target *t,
                               const struct mob_render_input *e)
{
    for (int pass = 0; pass < 4; ++pass)
    {
        const struct thing_item *it = &e->equip[4 - pass];
        if (!it->id || !it->armor || it->armor_index < 0 || it->armor_index > 4) continue;
        char name[64];
        snprintf(name, sizeof name, "armor_%s_%d", armor_names[it->armor_index], pass == 2 ? 2 : 1);
        const unsigned char *px = thing_texture(assets, name, 64, 32);
        if (!px) continue;
        struct mob_texture tex = {(unsigned char *)px, 64, 32};
        draw_armor_model(t, e, pass, &tex);
    }
}

#define JAVA_DEG (180.0f / 3.1415927f)

/* RendererLivingEntity.doRender's matrix at the model: renderLivingAt,
 * rotateCorpse, glScalef(-1, -1, 1), preRenderCallback and the -1.5078125
 * translate, over the camera modelview. */
static void living_gl_base(const struct entity_raster_target *t, const struct mob_render_input *e,
                           float mv[16])
{
    memcpy(mv, t->mv, 16 * sizeof(float));
    if (e->outer_set) rs_gl_mult(mv, e->outer);
    else
        rs_gl_translate(mv, (float)(e->x - t->cam[0]), (float)(e->y - t->cam[1]),
                        (float)(e->z - t->cam[2]));
    if (e->kind == MOB_PLAYER && e->sleeping)
    {
        rs_gl_rotate(mv, e->bed_deg, 0.0f, 1.0f, 0.0f);
        rs_gl_rotate(mv, 90.0f, 0.0f, 0.0f, 1.0f);
        rs_gl_rotate(mv, 270.0f, 0.0f, 1.0f, 0.0f);
    }
    else
        rs_gl_rotate(mv, 180.0f - corpse_yaw(e), 0.0f, 1.0f, 0.0f);
    if (e->death > 0 && !(e->kind == MOB_PLAYER && e->sleeping))
    {
        float d = ((float)e->death + e->partial_tick - 1.0f) / 20.0f * 1.6f;
        d = sqrtf(d);
        if (d > 1.0f) d = 1.0f;
        rs_gl_rotate(mv, d * death_max_rotation(e), 0.0f, 0.0f, 1.0f);
    }
    if (e->kind == MOB_IRON_GOLEM && e->limb_amount >= 0.01f)
        rs_gl_rotate(mv, 6.5f * golem_triangle(e->limb + 6.0f, 13.0f), 0.0f, 0.0f, 1.0f);
    rs_gl_scale(mv, -1.0f, -1.0f, 1.0f);
    if (e->kind == MOB_WITHER_SKELETON) rs_gl_scale(mv, 1.2f, 1.2f, 1.2f);
    if (e->kind == MOB_WITCH || e->kind == MOB_PLAYER) rs_gl_scale(mv, 0.9375f, 0.9375f, 0.9375f);
    rs_gl_translate(mv, 0.0f, -24.0f * 0.0625f - 0.0078125f, 0.0f);
}

/* ModelRenderer.postRender(0.0625). */
static void gl_post_render(float mv[16], const struct mob_part *p)
{
    if (p->ax == 0.0f && p->ay == 0.0f && p->az == 0.0f)
    {
        if (p->x != 0.0f || p->y != 0.0f || p->z != 0.0f)
            rs_gl_translate(mv, p->x * 0.0625f, p->y * 0.0625f, p->z * 0.0625f);
        return;
    }
    rs_gl_translate(mv, p->x * 0.0625f, p->y * 0.0625f, p->z * 0.0625f);
    if (p->az != 0.0f) rs_gl_rotate(mv, p->az * JAVA_DEG, 0.0f, 0.0f, 1.0f);
    if (p->ay != 0.0f) rs_gl_rotate(mv, p->ay * JAVA_DEG, 0.0f, 1.0f, 0.0f);
    if (p->ax != 0.0f) rs_gl_rotate(mv, p->ax * JAVA_DEG, 1.0f, 0.0f, 0.0f);
}

static void set_tint(struct glx *g, int c)
{
    g->color[0] = (float)(c >> 16 & 255) / 255.0f;
    g->color[1] = (float)(c >> 8 & 255) / 255.0f;
    g->color[2] = (float)(c & 255) / 255.0f;
    g->color[3] = 1.0f;
}

/* RenderBiped.renderEquippedItems (RenderZombie's and RenderSkeleton's), the
 * witch's and the iron golem's. GL_CULL_FACE is off inside doRender. */
/* RendererLivingEntity.renderArrowsStuckInEntity, as RenderPlayer calls it
 * first in renderEquippedItems: a Random seeded with the entity id picks a
 * ModelRenderer of ModelBiped's boxList (the cloak, the ears, the head, the
 * headwear, the body, the arms, the legs, in construction order), its box
 * and a point in it; the EntityArrow drawn there points back out of it,
 * lighting off. */
static void draw_stuck_arrows(const char *assets, const struct entity_raster_target *t,
                              const struct mob_render_input *e, const float base[16],
                              const struct mob_model *m)
{
    static const int part_of[9] = {-1, -2, 0, 6, 1, 2, 3, 4, 5};
    jrand r;
    jr_seed(&r, (int64_t)e->entity_id);
    struct glx g;
    glx_init(&g, t, e->brightness);
    g.lighting = 0;
    for (int i = 0; i < e->arrows; ++i)
    {
        int k = jr_int_n(&r, 9);
        struct mob_part p = {0};
        float x0, y0, z0, dx, dy, dz;
        if (part_of[k] >= 0)
        {
            p = m->part[part_of[k]];
            x0 = p.box[0].x; y0 = p.box[0].y; z0 = p.box[0].z;
            dx = (float)p.box[0].dx; dy = (float)p.box[0].dy; dz = (float)p.box[0].dz;
        }
        else if (part_of[k] == -1) { x0 = -5.0f; y0 = 0.0f; z0 = -1.0f; dx = 10.0f; dy = 16.0f; dz = 1.0f; }
        else { x0 = -3.0f; y0 = -6.0f; z0 = -1.0f; dx = 6.0f; dy = 6.0f; dz = 1.0f; }
        (void)jr_int_n(&r, 1);   /* cubeList.get(nextInt(1)) */
        memcpy(g.mv, base, sizeof g.mv);
        gl_post_render(g.mv, &p);
        float v9 = jr_float(&r), v10 = jr_float(&r), v11 = jr_float(&r);
        rs_gl_translate(g.mv, (x0 + (x0 + dx - x0) * v9) / 16.0f, (y0 + (y0 + dy - y0) * v10) / 16.0f,
                        (z0 + (z0 + dz - z0) * v11) / 16.0f);
        v9 = -(v9 * 2.0f - 1.0f);
        v10 = -(v10 * 2.0f - 1.0f);
        v11 = -(v11 * 2.0f - 1.0f);
        float v15 = (float)sqrt((double)(v9 * v9 + v11 * v11));
        float yaw = (float)(atan2((double)v9, (double)v11) * 180.0 / 3.141592653589793);
        float pitch = (float)(atan2((double)v10, (double)v15) * 180.0 / 3.141592653589793);
        thing_arrow(&g, assets, yaw, pitch, -e->partial_tick);
    }
}

static void draw_equipped(const char *assets, const struct entity_raster_target *t,
                          const struct mob_render_input *e)
{
    struct glx g;
    glx_init(&g, t, e->brightness);
    g.t.two_sided = 1;
    float base[16];
    living_gl_base(t, e, base);
    struct mob_model m;
    pose_model(&m, e, 0);
    if (e->kind == MOB_PLAYER && e->arrows > 0) draw_stuck_arrows(assets, t, e, base, &m);
    if (is_biped(e))
    {
        const struct thing_item *helm = &e->equip[4];
        if (helm->id && helm->ib3d)
        {
            memcpy(g.mv, base, sizeof base);
            gl_post_render(g.mv, &m.part[0]);
            float v6 = 0.625f;
            rs_gl_translate(g.mv, 0.0f, -0.25f, 0.0f);
            rs_gl_rotate(g.mv, 90.0f, 0.0f, 1.0f, 0.0f);
            rs_gl_scale(g.mv, v6, -v6, -v6);
            set_tint(&g, 0xffffff);
            thing_render_item(&g, assets, helm, 0, e->now);
        }
        else if (helm->id == 397)
        {
            /* the Items.skull branch: 1.0625 flipped, then
             * TileEntitySkullRenderer.func_152674_a(-0.5, 0, -0.5, 1, 180,
             * damage): culling off, the (-1, -1, 1) flip, ModelSkeletonHead
             * turned 180 (the zombie's 64x64) */
            int type = helm->meta;
            int ti = type == 1 ? TEX_WITHER_SKELETON : type == 2 ? TEX_ZOMBIE : type == 3 ? TEX_STEVE :
                     type == 4 ? TEX_CREEPER : TEX_SKELETON;
            if (read_texture(assets, ti))
            {
                memcpy(g.mv, base, sizeof base);
                gl_post_render(g.mv, &m.part[0]);
                float v6 = 1.0625f;
                rs_gl_scale(g.mv, v6, -v6, -v6);
                rs_gl_scale(g.mv, -1.0f, -1.0f, 1.0f);
                struct mob_part head = {0};
                head.ay = 180.0f / (180.0f / PI);
                gl_post_render(g.mv, &head);
                set_tint(&g, 0xffffff);
                glx_model_box(&g, -4.0f, -8.0f, -4.0f, 8, 8, 8, 0.0f, 0, 0, 0, 0.0625f, textures[ti].rgba,
                              64, type == 2 ? 64 : 32);
            }
        }
        const struct thing_item *it = &e->equip[0];
        if (!it->id) return;
        memcpy(g.mv, base, sizeof base);
        if (e->child)
        {
            float v6 = 0.5f;
            rs_gl_translate(g.mv, 0.0f, 0.625f, 0.0f);
            rs_gl_rotate(g.mv, -20.0f, -1.0f, 0.0f, 0.0f);
            rs_gl_scale(g.mv, v6, v6, v6);
        }
        gl_post_render(g.mv, &m.part[2]);
        rs_gl_translate(g.mv, -0.0625f, 0.4375f, 0.0625f);
        if (it->ib3d)
        {
            float v6 = 0.5f;
            rs_gl_translate(g.mv, 0.0f, 0.1875f, -0.3125f);
            v6 *= 0.75f;
            rs_gl_rotate(g.mv, 20.0f, 1.0f, 0.0f, 0.0f);
            rs_gl_rotate(g.mv, 45.0f, 0.0f, 1.0f, 0.0f);
            rs_gl_scale(g.mv, -v6, -v6, v6);
        }
        else if (it->bow)
        {
            float v6 = 0.625f;
            rs_gl_translate(g.mv, 0.0f, 0.125f, 0.3125f);
            rs_gl_rotate(g.mv, -20.0f, 0.0f, 1.0f, 0.0f);
            rs_gl_scale(g.mv, v6, -v6, v6);
            rs_gl_rotate(g.mv, -100.0f, 1.0f, 0.0f, 0.0f);
            rs_gl_rotate(g.mv, 45.0f, 0.0f, 1.0f, 0.0f);
        }
        else if (it->full3d)
        {
            float v6 = 0.625f;
            if (it->rot)
            {
                rs_gl_rotate(g.mv, 180.0f, 0.0f, 0.0f, 1.0f);
                rs_gl_translate(g.mv, 0.0f, -0.125f, 0.0f);
            }
            /* RenderPlayer: the sword held up to block */
            if (e->kind == MOB_PLAYER && e->held_right == 3)
            {
                rs_gl_translate(g.mv, 0.05f, 0.0f, -0.1f);
                rs_gl_rotate(g.mv, -50.0f, 0.0f, 1.0f, 0.0f);
                rs_gl_rotate(g.mv, -10.0f, 1.0f, 0.0f, 0.0f);
                rs_gl_rotate(g.mv, -60.0f, 0.0f, 0.0f, 1.0f);
            }
            /* func_82422_c: RenderSkeleton's own offset, RenderBiped's otherwise */
            if (e->kind == MOB_SKELETON || e->kind == MOB_WITHER_SKELETON)
                rs_gl_translate(g.mv, 0.09375f, 0.1875f, 0.0f);
            else
                rs_gl_translate(g.mv, 0.0f, 0.1875f, 0.0f);
            rs_gl_scale(g.mv, v6, -v6, v6);
            rs_gl_rotate(g.mv, -100.0f, 1.0f, 0.0f, 0.0f);
            rs_gl_rotate(g.mv, 45.0f, 0.0f, 1.0f, 0.0f);
        }
        else
        {
            float v6 = 0.375f;
            rs_gl_translate(g.mv, 0.25f, 0.1875f, -0.1875f);
            rs_gl_scale(g.mv, v6, v6, v6);
            rs_gl_rotate(g.mv, 60.0f, 0.0f, 0.0f, 1.0f);
            rs_gl_rotate(g.mv, -90.0f, 1.0f, 0.0f, 0.0f);
            rs_gl_rotate(g.mv, 20.0f, 0.0f, 0.0f, 1.0f);
        }
        if (it->multi)
            for (int pass = 0; pass <= 1; ++pass)
            {
                set_tint(&g, it->pass[pass].tint);
                thing_render_item(&g, assets, it, pass, e->now);
            }
        else
        {
            set_tint(&g, it->pass[0].tint);
            thing_render_item(&g, assets, it, 0, e->now);
        }
    }
    else if (e->kind == MOB_WITCH)
    {
        const struct thing_item *it = &e->equip[0];
        if (!it->id) return;
        memcpy(g.mv, base, sizeof base);
        if (e->child)
        {
            float v4 = 0.5f;
            rs_gl_translate(g.mv, 0.0f, 0.625f, 0.0f);
            rs_gl_rotate(g.mv, -20.0f, -1.0f, 0.0f, 0.0f);
            rs_gl_scale(g.mv, v4, v4, v4);
        }
        /* villagerNose.postRender: its own pivot and angles, not the head's */
        struct mob_part nose = m.part[5];
        nose.x = 0; nose.y = -2; nose.z = 0;
        gl_post_render(g.mv, &nose);
        rs_gl_translate(g.mv, -0.0625f, 0.53125f, 0.21875f);
        if (it->ib3d)
        {
            float v4 = 0.5f;
            rs_gl_translate(g.mv, 0.0f, 0.1875f, -0.3125f);
            v4 *= 0.75f;
            rs_gl_rotate(g.mv, 20.0f, 1.0f, 0.0f, 0.0f);
            rs_gl_rotate(g.mv, 45.0f, 0.0f, 1.0f, 0.0f);
            rs_gl_scale(g.mv, v4, -v4, v4);
        }
        else if (it->bow)
        {
            float v4 = 0.625f;
            rs_gl_translate(g.mv, 0.0f, 0.125f, 0.3125f);
            rs_gl_rotate(g.mv, -20.0f, 0.0f, 1.0f, 0.0f);
            rs_gl_scale(g.mv, v4, -v4, v4);
            rs_gl_rotate(g.mv, -100.0f, 1.0f, 0.0f, 0.0f);
            rs_gl_rotate(g.mv, 45.0f, 0.0f, 1.0f, 0.0f);
        }
        else if (it->full3d)
        {
            float v4 = 0.625f;
            if (it->rot)
            {
                rs_gl_rotate(g.mv, 180.0f, 0.0f, 0.0f, 1.0f);
                rs_gl_translate(g.mv, 0.0f, -0.125f, 0.0f);
            }
            rs_gl_translate(g.mv, 0.0f, 0.1875f, 0.0f);
            rs_gl_scale(g.mv, v4, -v4, v4);
            rs_gl_rotate(g.mv, -100.0f, 1.0f, 0.0f, 0.0f);
            rs_gl_rotate(g.mv, 45.0f, 0.0f, 1.0f, 0.0f);
        }
        else
        {
            float v4 = 0.375f;
            rs_gl_translate(g.mv, 0.25f, 0.1875f, -0.1875f);
            rs_gl_scale(g.mv, v4, v4, v4);
            rs_gl_rotate(g.mv, 60.0f, 0.0f, 0.0f, 1.0f);
            rs_gl_rotate(g.mv, -90.0f, 1.0f, 0.0f, 0.0f);
            rs_gl_rotate(g.mv, 20.0f, 0.0f, 0.0f, 1.0f);
        }
        rs_gl_rotate(g.mv, -15.0f, 1.0f, 0.0f, 0.0f);
        rs_gl_rotate(g.mv, 40.0f, 0.0f, 0.0f, 1.0f);
        /* RenderWitch sets glColor3f(1, 1, 1) once and never tints a pass */
        set_tint(&g, 0xffffff);
        thing_render_item(&g, assets, it, 0, e->now);
        if (it->multi) thing_render_item(&g, assets, it, 1, e->now);
    }
    else if (e->kind == MOB_IRON_GOLEM && e->rose_timer != 0)
    {
        memcpy(g.mv, base, sizeof base);
        rs_gl_rotate(g.mv, 5.0f + 180.0f * m.part[2].ax / 3.1415927f, 1.0f, 0.0f, 0.0f);
        rs_gl_translate(g.mv, -0.6875f, 1.25f, -0.9375f);
        rs_gl_rotate(g.mv, 90.0f, 1.0f, 0.0f, 0.0f);
        float v3 = 0.8f;
        rs_gl_scale(g.mv, v3, -v3, v3);
        set_tint(&g, 0xffffff);
        /* renderBlockAsItem(red_flower, 0, 1): render type 1, one normal
         * (0, -1, 0) and drawCrossedSquares at -0.5 */
        const struct rb_uv *ic = NULL;
        if (ensure_block_atlas(assets))
            for (int i = 0; i < block_atlas.n; ++i)
                if (!strcmp(block_atlas.name[i], "flower_rose")) { ic = &block_atlas.uv[i]; break; }
        if (!ic) return;
        double x = -0.5, y = -0.5, z = -0.5, sc = 1.0;
        double v18 = 0.45 * sc;
        float x1 = (float)(x + 0.5 - v18), x2 = (float)(x + 0.5 + v18);
        float z1 = (float)(z + 0.5 - v18), z2 = (float)(z + 0.5 + v18);
        float y0 = (float)(y + 0.0), y1 = (float)(y + sc);
        const float n[3] = {0.0f, -1.0f, 0.0f};
        const float quads[4][4][3] = {
            {{x1, y1, z1}, {x1, y0, z1}, {x2, y0, z2}, {x2, y1, z2}},
            {{x2, y1, z2}, {x2, y0, z2}, {x1, y0, z1}, {x1, y1, z1}},
            {{x1, y1, z2}, {x1, y0, z2}, {x2, y0, z1}, {x2, y1, z1}},
            {{x2, y1, z1}, {x2, y0, z1}, {x1, y0, z2}, {x1, y1, z2}}};
        const float uv[4][2] = {{ic->min_u, ic->min_v}, {ic->min_u, ic->max_v},
                                {ic->max_u, ic->max_v}, {ic->max_u, ic->min_v}};
        for (int q = 0; q < 4; ++q) glx_quad(&g, quads[q], uv, n, block_atlas_rgba, 512, 256);
    }
}

/* RendererLivingEntity.interpolateRotation */
static float interpolate_rotation(float prev, float cur, float pt)
{
    float d;
    for (d = cur - prev; d < -180.0f; d += 360.0f) ;
    while (d >= 180.0f) d -= 360.0f;
    return prev + pt * d;
}

void raster_mobs_player_preview(unsigned char *rgb, int w, int h, double sw_d, double sh_d,
                                int x, int y, int size, const unsigned char *skin,
                                const struct player_preview *pp)
{
    if (!skin) return;
    /* func_147046_a's rotations, as floats the way Java computes them */
    /* StrictMath.atan: fdlibm's atan2 over 1.0 returns its atan exactly */
    float atan_x = (float)fd_atan2((double)(pp->dx / 40.0f), 1.0), atan_y = (float)fd_atan2((double)(pp->dy / 40.0f), 1.0);
    float tilt = -atan_y * 20.0f;
    float body = atan_x * 20.0f, yaw = atan_x * 40.0f, pitch = -atan_y * 20.0f;
    /* RendererLivingEntity.doRender at partial tick 1 */
    float body_yaw = interpolate_rotation(pp->prev_body_own ? body : pp->prev_body_yaw, body, 1.0f);
    float head_yaw = interpolate_rotation(yaw, yaw, 1.0f);
    float head_pitch = pp->prev_pitch + (pitch - pp->prev_pitch) * 1.0f;
    float limb_amount = pp->prev_limb_amount + (pp->limb_amount - pp->prev_limb_amount) * 1.0f;
    if (limb_amount > 1.0f) limb_amount = 1.0f;
    float limb = pp->limb - pp->limb_amount * (1.0f - 1.0f);
    float age = (float)pp->age + 1.0f;
    float net_yaw = head_yaw - body_yaw;

    /* ModelBiped.setRotationAngles */
    const float R = 180.0f / 3.14159274101257324219f;
    float head_ax = head_pitch / R, head_ay = net_yaw / R;
    float ra_x = mh_cos(limb * 0.6662f + PI) * 2.0f * limb_amount * 0.5f;
    float la_x = mh_cos(limb * 0.6662f) * 2.0f * limb_amount * 0.5f;
    float ra_z = 0.0f, la_z = 0.0f;
    float rl_x = mh_cos(limb * 0.6662f) * 1.4f * limb_amount;
    float ll_x = mh_cos(limb * 0.6662f + PI) * 1.4f * limb_amount;
    float rl_y = 0.0f, ll_y = 0.0f;
    if (pp->riding)
    {
        ra_x += -(PI / 5.0f);
        la_x += -(PI / 5.0f);
        rl_x = -(PI * 2.0f / 5.0f);
        ll_x = -(PI * 2.0f / 5.0f);
        rl_y = PI / 10.0f;
        ll_y = -(PI / 10.0f);
    }
    if (pp->held) ra_x = ra_x * 0.5f - (PI / 10.0f) * (float)pp->held;
    float ra_y = 0.0f, la_y = 0.0f;
    /* the swing block runs for any onGround over -9990, so always */
    float swing = pp->swing;
    float body_ay = mh_sin(sqrtf(swing) * PI * 2.0f) * 0.2f;
    float ra_px = -mh_cos(body_ay) * 5.0f, ra_pz = mh_sin(body_ay) * 5.0f;
    float la_px = mh_cos(body_ay) * 5.0f, la_pz = -mh_sin(body_ay) * 5.0f;
    ra_y += body_ay;
    la_y += body_ay;
    la_x += body_ay;
    float s8 = 1.0f - swing;
    s8 *= s8;
    s8 *= s8;
    s8 = 1.0f - s8;
    float s9 = mh_sin(s8 * PI);
    float s10 = mh_sin(swing * PI) * -(head_ax - 0.7f) * 0.75f;
    ra_x = (float)((double)ra_x - ((double)s9 * 1.2 + (double)s10));
    ra_y += body_ay * 2.0f;
    ra_z = mh_sin(swing * PI) * -0.4f;
    float body_ax = 0.0f, rl_pz = 0.1f, ll_pz = 0.1f, rl_py = 12.0f, ll_py = 12.0f, head_py = 0.0f;
    if (pp->sneak)
    {
        body_ax = 0.5f;
        ra_x += 0.4f;
        la_x += 0.4f;
        rl_pz = ll_pz = 4.0f;
        rl_py = ll_py = 9.0f;
        head_py = 1.0f;
    }
    ra_z += mh_cos(age * 0.09f) * 0.05f + 0.05f;
    la_z -= mh_cos(age * 0.09f) * 0.05f + 0.05f;
    ra_x += mh_sin(age * 0.067f) * 0.05f;
    la_x -= mh_sin(age * 0.067f) * 0.05f;

    /* The GUI matrix up to enableStandardItemLighting (the lights live in
     * its frame, so the model below is lit in that frame): setupOverlayRendering's
     * translate(0,0,-2000), translate(x, y, 50), scale(-size, size, size),
     * Rz 180, Ry 135. */
    struct xf gui;
    xf_ident(&gui);
    xf_translate(&gui, 0.0f, 0.0f, -2000.0f);
    xf_translate(&gui, (float)x, (float)y, 50.0f);
    xf_scale(&gui, (float)-size, (float)size, (float)size);
    xf_rot_axis(&gui, 180.0f, 0.0f, 0.0f, 1.0f);
    xf_rot_axis(&gui, 135.0f, 0.0f, 1.0f, 0.0f);
    float mv[16] = {gui.m[0], gui.m[4], gui.m[8], 0, gui.m[1], gui.m[5], gui.m[9], 0,
                    gui.m[2], gui.m[6], gui.m[10], 0, gui.m[3], gui.m[7], gui.m[11], 1};
    float proj[16] = {0};
    proj[0] = (float)(2.0 / sw_d);
    proj[5] = (float)(2.0 / -sh_d);
    proj[10] = (float)(-2.0 / (3000.0 - 1000.0));
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[14] = (float)(-(3000.0 + 1000.0) / (3000.0 - 1000.0));
    proj[15] = 1.0f;
    /* The rest: Ry -135, the tilt, the yOffset translate and RenderPlayer's
     * y - yOffset (they cancel), then doRender's rotateCorpse, scale(-1,-1,1),
     * the player's 0.9375 and the model's translate. */
    struct xf outer;
    xf_ident(&outer);
    xf_rot_axis(&outer, -135.0f, 0.0f, 1.0f, 0.0f);
    xf_rot_axis(&outer, tilt, 1.0f, 0.0f, 0.0f);
    xf_translate(&outer, 0.0f, pp->y_offset, 0.0f);
    xf_translate(&outer, 0.0f, (float)(0.0 - (double)pp->y_offset), 0.0f);
    xf_rot_axis(&outer, 180.0f - body_yaw, 0.0f, 1.0f, 0.0f);
    xf_scale(&outer, -1.0f, -1.0f, 1.0f);
    xf_scale(&outer, 0.9375f, 0.9375f, 0.9375f);
    xf_translate(&outer, 0.0f, -24.0f * 0.0625f - 0.0078125f, 0.0f);

    float *depth = malloc((size_t)w * h * sizeof *depth);
    if (!depth) return;
    for (size_t i = 0; i < (size_t)w * h; ++i) depth[i] = 1.0f;
    const double cam[3] = {0, 0, 0};
    const float nofog[3] = {0, 0, 0};
    struct entity_raster_target t = {.w = w, .h = h, .proj = proj, .mv = mv, .cam = cam,
        .fog = nofog, .fogm = 0, .depth = depth, .rgb = rgb, .material = {1, 1, 1},
        .two_sided = 1, .no_light = 1};
    struct mob_render_input e = {0};
    e.kind = MOB_ZOMBIE;
    e.brightness = 15728880;
    struct mob_texture tex = {(unsigned char *)skin, 64, 32};
    /* ModelBiped.render: head, body, right arm, left arm, right leg, left
     * leg, headwear */
    struct { float px, py, pz, ax, ay, az; struct mob_box b; } parts[7] = {
        {0, head_py, 0, head_ax, head_ay, 0, {-4, -8, -4, 0, 8, 8, 8, 0, 0, 0}},
        {0, 0, 0, body_ax, body_ay, 0, {-4, 0, -2, 0, 8, 12, 4, 16, 16, 0}},
        {ra_px, 2, ra_pz, ra_x, ra_y, ra_z, {-3, -2, -2, 0, 4, 12, 4, 40, 16, 0}},
        {la_px, 2, la_pz, la_x, la_y, la_z, {-1, -2, -2, 0, 4, 12, 4, 40, 16, 1}},
        {-1.9f, rl_py, rl_pz, rl_x, rl_y, 0, {-2, 0, -2, 0, 4, 12, 4, 0, 16, 0}},
        {1.9f, ll_py, ll_pz, ll_x, ll_y, 0, {-2, 0, -2, 0, 4, 12, 4, 0, 16, 1}},
        {0, head_py, 0, head_ax, head_ay, 0, {-4, -8, -4, 0.5f, 8, 8, 8, 32, 0, 0}}};
    for (int i = 0; i < 7; ++i)
    {
        struct xf pm;
        xf_ident(&pm);
        xf_part(&pm, parts[i].px, parts[i].py, parts[i].pz, parts[i].ax, parts[i].ay, parts[i].az);
        emit_box_xf(&t, &e, &outer, &pm, &parts[i].b, &tex, 0);
    }
    free(depth);
}

void raster_mobs_player_preview_full(const char *assets, unsigned char *rgb, int w, int h, double sw_d,
                                     double sh_d, int x, int y, int size, const struct player_preview *pp,
                                     const struct mob_render_input *base, const uint32_t *lm)
{
    /* func_147046_a's rotations, as the plain preview computes them */
    float atan_x = (float)fd_atan2((double)(pp->dx / 40.0f), 1.0), atan_y = (float)fd_atan2((double)(pp->dy / 40.0f), 1.0);
    float tilt = -atan_y * 20.0f;
    float body = atan_x * 20.0f, yaw = atan_x * 40.0f, pitch = -atan_y * 20.0f;
    struct mob_render_input m = *base;
    m.body_yaw = interpolate_rotation(pp->prev_body_own ? body : pp->prev_body_yaw, body, 1.0f);
    m.head_yaw = interpolate_rotation(yaw, yaw, 1.0f);
    m.pitch = pp->prev_pitch + (pitch - pp->prev_pitch) * 1.0f;
    m.partial_tick = 1.0f;
    /* RenderManager.playerViewY = 180 for the fire's turn; no lightmap */
    m.view_yaw = 180.0f;
    /* no lightmap on the model; the hurt tint's getBrightness is the
     * player's own in the world (BASE's) */
    m.brightness = 15728880;
    m.x = m.y = m.z = 0.0;
    /* the GUI matrix up to enableStandardItemLighting, then the rest up to
     * RendererLivingEntity (Ry -135, the tilt, the yOffset translate and
     * RenderPlayer's y - yOffset) as the entity's own modelview */
    struct xf gui;
    xf_ident(&gui);
    xf_translate(&gui, 0.0f, 0.0f, -2000.0f);
    xf_translate(&gui, (float)x, (float)y, 50.0f);
    xf_scale(&gui, (float)-size, (float)size, (float)size);
    xf_rot_axis(&gui, 180.0f, 0.0f, 0.0f, 1.0f);
    xf_rot_axis(&gui, 135.0f, 0.0f, 1.0f, 0.0f);
    float mv[16] = {gui.m[0], gui.m[4], gui.m[8], 0, gui.m[1], gui.m[5], gui.m[9], 0,
                    gui.m[2], gui.m[6], gui.m[10], 0, gui.m[3], gui.m[7], gui.m[11], 1};
    float proj[16] = {0};
    proj[0] = (float)(2.0 / sw_d);
    proj[5] = (float)(2.0 / -sh_d);
    proj[10] = (float)(-2.0 / (3000.0 - 1000.0));
    proj[12] = -1.0f;
    proj[13] = 1.0f;
    proj[14] = (float)(-(3000.0 + 1000.0) / (3000.0 - 1000.0));
    proj[15] = 1.0f;
    rs_gl_identity(m.outer);
    rs_gl_rotate(m.outer, -135.0f, 0.0f, 1.0f, 0.0f);
    rs_gl_rotate(m.outer, tilt, 1.0f, 0.0f, 0.0f);
    rs_gl_translate(m.outer, 0.0f, pp->y_offset, 0.0f);
    rs_gl_translate(m.outer, 0.0f, (float)(0.0 - (double)pp->y_offset), 0.0f);
    m.outer_set = 1;
    m.fire_up = pp->y_offset;

    float *depth = malloc((size_t)w * h * sizeof *depth);
    if (!depth) return;
    for (size_t i = 0; i < (size_t)w * h; ++i) depth[i] = 1.0f;
    const double cam[3] = {0, 0, 0};
    const float nofog[3] = {0, 0, 0};
    unsigned long triangles = 0, samples = 0;
    struct entity_raster_target t = {.w = w, .h = h, .proj = proj, .mv = mv, .cam = cam,
        .fog = nofog, .fogm = 0, .depth = depth, .rgb = rgb, .material = {1, 1, 1},
        .two_sided = 1, .no_light = 1, .triangles = &triangles, .samples = &samples, .lm = lm,
        .linear_depth = 1};
    raster_mobs_draw(assets, &t, &m, 1, NULL, NULL, 0, NULL);
    free(depth);
}

/* A model wholly outside one clip plane draws nothing: every quad its
 * passes below make (the model, its layers, armour, held and carried
 * items, the overlays, fire and shadow) is dropped whole by the clippers
 * (raster_entities.c clip_and_draw and the device's geom_rec.cu keep a
 * vertex with w - sign * c >= 0 against each of the six planes), so a
 * sphere about the entity's middle of radius 1.5 (width + height) + 3
 * blocks outside a plane skips them all (RenderGlobal.renderEntities'
 * frustum test is of the box alone and not ported: the models poking out
 * of an unseen box are drawn). make -C csrc mob-cull-check draws each
 * culled model as a probe and aborts on a quad not wholly outside a
 * plane. The leash runs to its holder: never culled. */
static int mob_off_view(const struct entity_raster_target *t, const struct mob_render_input *e, float width)
{
    if (e->outer_set || e->leash) return 0;
    double c[3] = {e->x - t->cam[0], e->y + (double)e->height * 0.5 - t->cam[1], e->z - t->cam[2]};
    double r = 1.5 * ((double)width + (double)e->height) + 3.0;
    /* clip = proj mv p (column-major): row i of the product */
    double m[4][4];
    for (int i = 0; i < 4; ++i)
        for (int j = 0; j < 4; ++j)
        {
            double s = 0;
            for (int k = 0; k < 4; ++k) s += (double)t->proj[4 * k + i] * (double)t->mv[4 * j + k];
            m[i][j] = s;
        }
    for (int plane = 0; plane < 6; ++plane)
    {
        double sign = plane & 1 ? -1.0 : 1.0;
        const double *a = m[plane / 2];
        double n[4];
        for (int j = 0; j < 4; ++j) n[j] = m[3][j] - sign * a[j];
        double len = sqrt(n[0] * n[0] + n[1] * n[1] + n[2] * n[2]);
        if (n[0] * c[0] + n[1] * c[1] + n[2] * c[2] + n[3] < -r * len) return 1;
    }
    return 0;
}

int raster_mobs_draw(const char *assets, const struct entity_raster_target *target,
                     const struct mob_render_input *mobs, int count, const struct rb_world *world,
                     const struct rb_table *table, int fancy, const float light_brightness[16])
{
    /* RendererLivingEntity.doRender draws the model with GL_CULL_FACE off;
     * RenderMooshroom turns it back on for the mushrooms, and the shadow is
     * drawn after doRender has re-enabled it. The additive layers (eyes,
     * creeper armor) stay one-sided: they write no depth here, so their back
     * faces would add twice where GL's depth writes hide them. */
    struct entity_raster_target living = *target;
    living.two_sided = 1;
    const struct entity_raster_target *t = &living;
    ensure_assets(assets);
    int drawn = 0;
    for (int i = 0; i < count; ++i)
    {
        struct mob_render_input entry = mobs[i];
        const struct mob_render_input *e = &entry;
        if (e->invisible)
            continue;
        float width = e->width > 0             ? e->width
                      : e->kind == MOB_CHICKEN ? 0.4f
                      : e->kind == MOB_ZOMBIE  ? 0.6f
                                               : 0.9f;
        double range = ((double)width * 2.0 + e->height) / 3.0 * 64.0;
        double range_x = e->x - t->cam[0], range_y = e->y - t->cam[1], range_z = e->z - t->cam[2];
        if (!e->outer_set && range_x * range_x + range_y * range_y + range_z * range_z >= range * range)
            continue;
        if (e->burning)
            entry.brightness = 15728880;
        if (e->kind == MOB_DRAGON)
        {
            /* RenderDragon.doRender: BossStatus.setBossStatus(dragon, false),
             * the name EntityDragon.func_145748_c_().getFormattedText() */
            if (e->max_health > 0)
                raster_hud_boss_set(e->health / e->max_health, "Ender Dragon\302\247r");
            dragon_draw(assets, t, e);
            ++drawn;
            if (fancy)
                draw_shadow(assets, t, e, world, table, light_brightness);
            continue;
        }
        if (e->kind == MOB_LEASH_KNOT)
        {
            knot_draw(assets, t, e);
            ++drawn;
            continue;
        }
        if (e->kind == MOB_ENDER_CRYSTAL)
        {
            crystal_draw(assets, t, e);
            ++drawn;
            if (fancy)
                draw_shadow(assets, t, e, world, table, light_brightness);
            continue;
        }
        int ti = e->kind == MOB_PIG ? TEX_PIG : e->kind == MOB_COW ? TEX_COW :
                 e->kind == MOB_SHEEP ? TEX_SHEEP : e->kind == MOB_CHICKEN ? TEX_CHICKEN :
                 e->kind == MOB_ZOMBIE ? (e->zombie_villager ? TEX_ZOMBIE_VILLAGER : TEX_ZOMBIE) :
                 e->kind == MOB_SKELETON ? TEX_SKELETON :
                 e->kind == MOB_WITHER_SKELETON ? TEX_WITHER_SKELETON :
                 e->kind == MOB_CREEPER ? TEX_CREEPER :
                 e->kind == MOB_SPIDER ? TEX_SPIDER :
                 e->kind == MOB_CAVE_SPIDER ? TEX_CAVE_SPIDER :
                 e->kind == MOB_ENDERMAN ? TEX_ENDERMAN :
                 e->kind == MOB_WITCH ? TEX_WITCH :
                 e->kind == MOB_SLIME ? TEX_SLIME :
                 e->kind == MOB_SILVERFISH ? TEX_SILVERFISH :
                 e->kind == MOB_PIGMAN ? TEX_PIGMAN :
                 e->kind == MOB_GHAST ? (e->ghast_shooting ? TEX_GHAST_SHOOTING : TEX_GHAST) :
                 e->kind == MOB_BLAZE ? TEX_BLAZE :
                 e->kind == MOB_MAGMA_CUBE ? TEX_MAGMACUBE :
                 e->kind == MOB_VILLAGER ? (e->profession >= 0 && e->profession <= 4 ?
                                            TEX_FARMER + e->profession : TEX_VILLAGER) :
                 e->kind == MOB_IRON_GOLEM ? TEX_IRON_GOLEM :
                 e->kind == MOB_SQUID ? TEX_SQUID :
                 e->kind == MOB_BAT ? TEX_BAT : e->kind == MOB_PLAYER ? TEX_STEVE : TEX_MOOSHROOM;
        if (!read_texture(assets, ti))
            continue;
        int culled = mob_off_view(t, e, width);
#ifdef NETHERITE_MOB_CULL_CHECK
        struct raster_rec *probe_held = NULL;
        if (culled)
        {
            probe_held = raster_rec_hold();
            raster_entity_cull_probe(1, e->kind);
        }
#else
        if (culled)
        {
            ++drawn;
            continue;
        }
#endif
        draw_model(t, e, &textures[ti], 0);
        leash_draw(t, e);
        ++drawn;
        if (e->kind == MOB_PIG && e->saddle && read_texture(assets, TEX_SADDLE))
        {
            /* The saddle model is ModelPig(0.5), expanded in its own pass. */
            struct mob_model m;
            pose_model(&m, e, 0);
            for (int p = 0; p < m.npart; ++p)
                for (int b = 0; b < m.part[p].nbox; ++b)
                {
                    m.part[p].box[b].grow = 0.5f;
                    draw_box(t, e, &m, &m.part[p], &m.part[p].box[b], &textures[TEX_SADDLE]);
                }
        }
        if (e->kind == MOB_SHEEP && !e->sheared && read_texture(assets, TEX_FUR))
        {
            int c = e->color & 15;
            struct entity_raster_target dyed = *t;
            for (int ch = 0; ch < 3; ++ch)
                dyed.material[ch] = fleece[c][ch];
            draw_model(&dyed, e, &textures[TEX_FUR], 1);
        }
        if (e->kind == MOB_SLIME)
        {
            struct entity_raster_target outer = *t;
            /* RenderSlime's outer pass: GL_BLEND with the depth mask still on and
             * culling off, like the model under it */
            outer.blend = 3;
            outer.alpha = 1;
            draw_model(&outer, e, &textures[ti], 1);
        }
        if ((e->kind == MOB_SPIDER || e->kind == MOB_CAVE_SPIDER ||
             e->kind == MOB_ENDERMAN) &&
            read_texture(assets, e->kind == MOB_ENDERMAN ? TEX_ENDERMAN_EYES : TEX_SPIDER_EYES))
        {
            struct entity_raster_target eyes = *t;
            eyes.blend = 2;
            eyes.two_sided = 0;
            struct mob_render_input lit = *e;
            lit.brightness = 61680;
            draw_model(&eyes, &lit, &textures[e->kind == MOB_ENDERMAN ?
                       TEX_ENDERMAN_EYES : TEX_SPIDER_EYES], 0);
        }
        if (e->kind == MOB_CREEPER && e->charged && read_texture(assets, TEX_CREEPER_ARMOR))
        {
            /* RenderCreeper's pass 1: GL_ONE, GL_ONE with the depth mask on and
             * culling off (doRender's), so a back face drawn before the front
             * one in front of it adds too */
            struct entity_raster_target armor = *t;
            armor.blend = 7;
            armor.two_sided = 1;
            armor.unlit = 1;
            armor.tex_u = armor.tex_v = (e->age + e->partial_tick) * 0.01f;
            armor.material[0] = armor.material[1] = armor.material[2] = 0.5f;
            draw_model(&armor, e, &textures[TEX_CREEPER_ARMOR], 1);
        }
        if (is_biped(e))
            draw_armor(assets, t, e);
        if (e->kind == MOB_ENDERMAN && e->carried_id)
            draw_carried_block(assets, t, e, table);
        if (is_biped(e) || e->kind == MOB_WITCH || e->kind == MOB_IRON_GOLEM)
            draw_equipped(assets, t, e);
        if (e->hurt > 0 || e->death > 0)
        {
            struct entity_raster_target overlay = *t;
            overlay.overlay = 1;
            overlay.overlay_brightness = e->brightness_scalar;
            draw_model(&overlay, e, &textures[ti], 0);
            if (e->kind == MOB_SHEEP && !e->sheared && textures[TEX_FUR].rgba)
                draw_model(&overlay, e, &textures[TEX_FUR], 1);
            if (is_biped(e))
                draw_armor_overlay(assets, &overlay, e);
            if (e->kind == MOB_SPIDER || e->kind == MOB_CAVE_SPIDER || e->kind == MOB_ENDERMAN)
            {
                /* inheritRenderPass(0) re-runs the eyes pass's shouldRenderPass,
                 * which leaves glBlendFunc(GL_ONE, GL_ONE): the pass model's red
                 * adds on top, its alpha unused */
                struct entity_raster_target add = overlay;
                add.overlay_alpha = 1.0f;
                add.blend = 2;
                draw_model(&add, e, &textures[ti], 0);
            }
        }
        if (e->kind == MOB_CREEPER)
        {
            /* RenderCreeper.getColorMultiplier: a white pass at the flash's
             * fifth, on the odd tenths of getCreeperFlashIntensity, under the
             * same GL_EQUAL as the hurt's */
            float fi = e->flash;
            if ((int)(fi * 10.0f) % 2 != 0)
            {
                int a = (int)(fi * 0.2f * 255.0f);
                if (a < 0) a = 0;
                if (a > 255) a = 255;
                if (a > 0)
                {
                    struct entity_raster_target wf = *t;
                    wf.overlay = 1;
                    wf.overlay_has_rgb = 1;
                    wf.overlay_rgb[0] = wf.overlay_rgb[1] = wf.overlay_rgb[2] = 1.0f;
                    wf.overlay_alpha = (float)a / 255.0f;
                    draw_model(&wf, e, &textures[ti], 0);
                }
            }
        }
        if (e->kind == MOB_MOOSHROOM && read_texture(assets, TEX_MUSHROOM_RED))
            draw_mooshroom_mushrooms(target, e, &textures[TEX_MUSHROOM_RED]);
        if (fancy && !e->outer_set)
            draw_shadow(assets, target, e, world, table, light_brightness);
        struct entity_raster_target fire_t = *target;
        float fire_mv[16];
        if (e->outer_set)
        {
            /* an entity drawn under its own modelview (the inventory's
             * preview at 0, 0, 0) */
            memcpy(fire_mv, target->mv, sizeof fire_mv);
            rs_gl_mult(fire_mv, e->outer);
            rs_gl_translate(fire_mv, 0.0f, e->fire_up, 0.0f);
            fire_t.mv = fire_mv;
            /* RendererLivingEntity.doRender re-enabled the lightmap unit */
            if (e->fire_brightness_set) fire_t.no_light = 0;
        }
        if (e->burning)
            thing_fire(&fire_t, assets, e->outer_set ? 0.0f : (float)(e->x - target->cam[0]),
                       e->outer_set ? 0.0f : (float)(e->y - target->cam[1]),
                       e->outer_set ? 0.0f : (float)(e->z - target->cam[2]), e->width, e->height, e->fire_dy, e->view_yaw,
                       /* the lightmap coordinates the last pass left: the eyes
                        * passes of RenderSpider and RenderEnderman set 61680 */
                       e->fire_brightness_set ? e->fire_brightness :
                       e->kind == MOB_SPIDER || e->kind == MOB_CAVE_SPIDER || e->kind == MOB_ENDERMAN
                           ? 61680 : entry.brightness);
#ifdef NETHERITE_MOB_CULL_CHECK
        if (culled)
        {
            raster_entity_cull_probe(0, 0);
            raster_rec_restore(probe_held);
            --drawn;
        }
#endif
    }
    return drawn;
}
static float getf(const struct jval *v)
{
    uint32_t bits = 0;
    json_float(v, &bits);
    float out;
    memcpy(&out, &bits, 4);
    return out;
}
static double getd(const struct jval *v)
{
    uint64_t bits = 0;
    json_double(v, &bits);
    double out;
    memcpy(&out, &bits, 8);
    return out;
}
static int geti(const struct jval *v)
{
    int64_t n = 0;
    json_int(v, &n);
    return (int)n;
}
static char *lastline(const char *path)
{
    FILE *f = fopen(path, "rb");
    if (!f)
        return NULL;
    char *line = NULL, *last = NULL;
    size_t cap = 0;
    while (getline(&line, &cap, f) >= 0)
    {
        free(last);
        last = strdup(line);
    }
    free(line);
    fclose(f);
    return last;
}
void raster_mobs_scene(const char *scene, const struct entity_raster_target *t)
{
    char path[1200];
    snprintf(path, sizeof path, "%s/state/frames.jsonl", scene);
    char *line = lastline(path);
    if (!line)
        return;
    struct jval *root = json_parse(line);
    if (!root)
    {
        free(line);
        return;
    }
    const struct jval *a = json_get(root, "mobs");
    const struct jval *crystals = json_get(root, "crystals");
    int n = json_len(a);
    struct mob_render_input *mob = calloc((size_t)(n + json_len(crystals)), sizeof *mob);
    if (!mob)
    {
        json_free(root);
        return;
    }
    int count = 0;
    const struct jval *pl = json_get(root, "pl");
    float vpt = json_get(root, "pt") ? getf(json_get(root, "pt")) : 1.0f;
    float view_yaw = getf(json_get(pl, "pyaw")) + (getf(json_get(pl, "yaw")) - getf(json_get(pl, "pyaw"))) * vpt;
    int64_t now = 0;
    json_int(json_get(json_get(json_get(root, "hud"), "toast"), "now"), &now);
    for (int i = 0; i < n; ++i)
    {
        const struct jval *v = json_at(a, i);
        const char *cls = json_str(json_get(v, "class"));
        if (!cls)
            continue;
        enum mob_model_kind kind;
        if (!strcmp(cls, "EntityPig"))
            kind = MOB_PIG;
        else if (!strcmp(cls, "EntityCow"))
            kind = MOB_COW;
        else if (!strcmp(cls, "EntitySheep"))
            kind = MOB_SHEEP;
        else if (!strcmp(cls, "EntityChicken"))
            kind = MOB_CHICKEN;
        else if (!strcmp(cls, "EntityZombie"))
            kind = MOB_ZOMBIE;
        else if (!strcmp(cls, "EntitySkeleton"))
            kind = geti(json_get(v, "skeletonType")) == 1 ? MOB_WITHER_SKELETON : MOB_SKELETON;
        else if (!strcmp(cls, "EntityCreeper")) kind = MOB_CREEPER;
        else if (!strcmp(cls, "EntitySpider")) kind = MOB_SPIDER;
        else if (!strcmp(cls, "EntityCaveSpider")) kind = MOB_CAVE_SPIDER;
        else if (!strcmp(cls, "EntityEnderman")) kind = MOB_ENDERMAN;
        else if (!strcmp(cls, "EntityWitch")) kind = MOB_WITCH;
        else if (!strcmp(cls, "EntitySlime")) kind = MOB_SLIME;
        else if (!strcmp(cls, "EntitySilverfish")) kind = MOB_SILVERFISH;
else if (!strcmp(cls, "EntityPigZombie")) kind = MOB_PIGMAN;
        else if (!strcmp(cls, "EntityGhast")) kind = MOB_GHAST;
        else if (!strcmp(cls, "EntityBlaze")) kind = MOB_BLAZE;
        else if (!strcmp(cls, "EntityMagmaCube")) kind = MOB_MAGMA_CUBE;
        else if (!strcmp(cls, "EntityVillager")) kind = MOB_VILLAGER;
        else if (!strcmp(cls, "EntityIronGolem")) kind = MOB_IRON_GOLEM;
        else if (!strcmp(cls, "EntitySquid")) kind = MOB_SQUID;
        else if (!strcmp(cls, "EntityBat")) kind = MOB_BAT;
        else if (!strcmp(cls, "EntityMooshroom")) kind = MOB_MOOSHROOM;
        else if (!strcmp(cls, "EntityDragon")) kind = MOB_DRAGON;
        else
            continue;
        struct mob_render_input *e = &mob[count++];
        e->kind = kind;
        e->id = geti(json_get(v, "id"));
        e->x = getd(json_get(v, "x"));
        e->y = getd(json_get(v, "y"));
        e->z = getd(json_get(v, "z"));
        e->height = getf(json_get(v, "height"));
        e->width = getf(json_get(v, "width"));
        if (e->height <= 0)
            e->height = kind == MOB_PIG       ? 0.9f
                        : kind == MOB_COW     ? 1.4f
                        : kind == MOB_SHEEP   ? 1.3f
                        : kind == MOB_CHICKEN ? 0.7f
                                              : 1.8f;
        e->body_yaw = getf(json_get(v, "byaw"));
        e->head_yaw = getf(json_get(v, "hyaw"));
        e->pitch = getf(json_get(v, "pitch"));
        e->limb = getf(json_get(v, "limb"));
        e->limb_amount = getf(json_get(v, "limba"));
        e->swing = getf(json_get(v, "swing"));
        e->wing = getf(json_get(v, "wing"));
        e->eat_head_y = getf(json_get(v, "eatHeadY"));
        e->eat_head_x = getf(json_get(v, "eatHeadX"));
        e->partial_tick = getf(json_get(root, "pt"));
        e->age = geti(json_get(v, "age"));
        e->hurt = geti(json_get(v, "hurt"));
        e->death = geti(json_get(v, "death"));
        e->child = geti(json_get(v, "child"));
        e->sheared = geti(json_get(v, "sheared"));
        e->color = geti(json_get(v, "color"));
        e->saddle = geti(json_get(v, "saddle"));
        e->brightness = geti(json_get(v, "brf"));
        e->brightness_scalar = getf(json_get(v, "bright"));
        e->invisible = geti(json_get(v, "invisible"));
        e->burning = geti(json_get(v, "burning"));
        e->charged = geti(json_get(v, "charged"));
        e->flash = getf(json_get(v, "flash"));
        e->carried_id = geti(json_get(v, "carried"));
        e->carried_meta = geti(json_get(v, "carriedMeta"));
        e->screaming = geti(json_get(v, "screaming"));
        /* "slimeSize" is how the mobs2b magma cube scenes recorded it. */
        e->slime_size = geti(json_get(v, json_get(v, "size") ? "size" : "slimeSize"));
        e->squish = getf(json_get(v, "squish"));
e->profession = geti(json_get(v, "profession"));
        e->ghast_shooting = geti(json_get(v, "shooting"));
        e->hanging = geti(json_get(v, "hanging"));
        e->attack_timer = geti(json_get(v, "attackTimer"));
        e->rose_timer = geti(json_get(v, "roseTimer"));
        e->tentacle = getf(json_get(v, "tentacle"));
        e->squid_pitch = getf(json_get(v, "squidPitch"));
        e->squid_yaw = getf(json_get(v, "squidYaw"));
        e->attack_progress = getf(json_get(v, "attackProgress"));
        e->fire_dy = getf(json_get(v, "fdy"));
        e->view_yaw = view_yaw;
        e->zombie_villager = geti(json_get(v, "zvil"));
        e->converting = geti(json_get(v, "conv"));
        e->now = now;
        const struct jval *equip = json_get(v, "equip");
        for (int k = 0; k < 5 && k < json_len(equip); ++k)
            thing_item_parse(json_at(equip, k), &e->equip[k]);
        if (kind == MOB_DRAGON)
        {
            e->anim = getf(json_get(v, "anim"));
            e->health = getf(json_get(v, "hp"));
            e->max_health = getf(json_get(v, "mhp"));
            e->death_ticks = geti(json_get(v, "deathTicks"));
            const struct jval *offs = json_get(v, "offsets");
            for (int k = 0; k < 24; ++k)
            {
                const struct jval *o = json_at(offs, k);
                if (json_len(o) != 3)
                    continue;
                for (int j = 0; j < 3; ++j)
                    e->off[k][j] = getd(json_at(o, j));
            }
            const struct jval *beam = json_get(v, "beam");
            if (beam)
            {
                e->has_beam = 1;
                e->beam_rot = getf(json_get(beam, "rot"));
                e->beam_dx = getf(json_get(beam, "dx"));
                e->beam_dy = getf(json_get(beam, "dy"));
                e->beam_dz = getf(json_get(beam, "dz"));
                e->beam_len = getf(json_get(beam, "len"));
                e->beam_ticks = geti(json_get(beam, "ticks"));
            }
        }
    }
    for (int i = 0; i < json_len(crystals); ++i)
    {
        const struct jval *v = json_at(crystals, i);
        struct mob_render_input *e = &mob[count++];
        e->kind = MOB_ENDER_CRYSTAL;
        e->id = geti(json_get(v, "id"));
        e->x = getd(json_get(v, "x"));
        e->y = getd(json_get(v, "y"));
        e->z = getd(json_get(v, "z"));
        e->height = 2.0f;
        e->width = 2.0f;
        e->brightness = geti(json_get(v, "brf"));
        e->crystal_rot = getf(json_get(v, "rot"));
        e->partial_tick = getf(json_get(root, "pt"));
    }
    int64_t iv = 0;
    const struct jval *fancy_v = json_get(json_get(root, "opt"), "fancy");
    if (!fancy_v)
        fancy_v = json_get(json_get(root, "hud"), "fancy");
    int fancy = json_int(fancy_v, &iv) ? (int)iv : 1;
    float lbt[16];
    const struct jval *brightness = json_get(json_get(root, "wo"), "lbt");
    for (int i = 0; i < 16; ++i)
        lbt[i] = brightness ? getf(json_at(brightness, i)) : 1;
    struct rb_world world = {0};
    struct rb_table table = {0};
    int has_world = 0;
    int has_table = rb_table_load(&table, scene) == 0;
    if (fancy && has_table && rb_world_load(&world, scene) == 0)
    {
        int radius, cx, cz, px, py, pz;
        if (rb_manifest_load(scene, &radius, &cx, &cz, &px, &py, &pz) == 0)
        {
            world.origin_cx = cx;
            world.origin_cz = cz;
            has_world = 1;
        }
    }
    raster_mobs_draw(scene, t, mob, count, has_world ? &world : NULL, has_table ? &table : NULL,
                     fancy, lbt);
    if (world.data)
        rb_world_free(&world);
    if (has_table)
        rb_table_free(&table);
    free(mob);
    json_free(root);
}
