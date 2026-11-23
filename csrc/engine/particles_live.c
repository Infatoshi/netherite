/* The client's particles. See particles_live.h for the contract; each
 * constructor, onUpdate and spawn site below is the named vanilla method
 * statement for statement, the float and double casts as written. */
#define _POSIX_C_SOURCE 200809L

#include "particles_live.h"
#include "jfloor.h"
#include "env.h"

#include "blocks.h"
#include "biomes.h"
#include "noise.h"
#include "potion.h"
#include "raytrace.h"
#include "collide.h"
#include "smath.h"
#include "render_blocks_int.h"
#include "item_color.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

static const char *const KIND_CLASS[PLIVE_KINDS] = {
    [PLIVE_DIGGING] = "EntityDiggingFX",
    [PLIVE_LARGE_EXPLODE] = "EntityLargeExplodeFX",
    [PLIVE_HUGE_EXPLODE] = "EntityHugeExplodeFX",
    [PLIVE_SMOKE] = "EntitySmokeFX",
    [PLIVE_FLAME] = "EntityFlameFX",
    [PLIVE_REDDUST] = "EntityReddustFX",
    [PLIVE_PORTAL] = "EntityPortalFX",
    [PLIVE_AURA] = "EntityAuraFX",
    [PLIVE_RAIN] = "EntityRainFX",
    [PLIVE_SPLASH] = "EntitySplashFX",
    [PLIVE_EXPLODE] = "EntityExplodeFX",
    [PLIVE_CRIT] = "EntityCritFX",
    [PLIVE_CRIT2] = "EntityCrit2FX",
    [PLIVE_SPELL] = "EntitySpellParticleFX",
    [PLIVE_BREAKING] = "EntityBreakingFX",
    [PLIVE_LAVA] = "EntityLavaFX",
    [PLIVE_DROP] = "EntityDropParticleFX",
    [PLIVE_SUSPEND] = "EntitySuspendFX",
    [PLIVE_ENCHANT] = "EntityEnchantmentTableParticleFX",
    [PLIVE_BUBBLE] = "EntityBubbleFX",
    [PLIVE_BLOCKDUST] = "EntityBlockDustFX",
    [PLIVE_NOTE] = "EntityNoteFX",
    [PLIVE_HEART] = "EntityHeartFX",
};

const char *particles_live_class(int kind)
{
    return kind >= 0 && kind < PLIVE_KINDS ? KIND_CLASS[kind] : "?";
}

int particles_live_kind(const char *cls)
{
    if (!cls) return -1;
    for (int k = 0; k < PLIVE_KINDS; ++k)
        if (KIND_CLASS[k] && !strcmp(KIND_CLASS[k], cls)) return k;
    return -1;
}

void particles_live_init(struct particles_live *pl, struct world *w, det_state *det)
{
    /* The mesher is the caller's live render world (the layer 1 icons read
     * its biome colors); it survives the reset, the way EffectRenderer does. */
    struct rb_mesher *keep = pl->mesher;
    int pinned = pl->pinned, fancy = pl->fancy;
    int64_t pin_t = pl->pin_t;
    det_pin pm = pl->pin_math, ps = pl->pin_seed, pe = pl->pin_effect;
    /* the FX slots past n are never read (fx_slot clears one as it takes
     * it), so they stay unwritten: the 4 x 4,000 slots are 7.6 MB, and a
     * run at the minimal particle setting uses a few */
    memset(pl, 0, offsetof(struct particles_live, fx));
    pl->n = 0;
    pl->mesher = keep;
    pl->pinned = pinned;
    pl->fancy = fancy;
    pl->pin_t = pin_t;
    pl->pin_math = pm;
    pl->pin_seed = ps;
    pl->pin_effect = pe;
    pl->world = w;
    pl->det = det;
    /* the run's particleSetting (the pinned profile's minimal, 2, unless a
     * tape's options say otherwise): at 2 only the specials and the
     * directly added FX exist */
    pl->setting = env_particle_setting();
    /* EffectRenderer.rand: one Det.newRandom at startup, never digested. The
     * live copy seeds from a constant; its three nextDouble draws place each
     * hit particle, so live hit positions are the seed's children, not the
     * oracle's. Everything else about the particles is exact. */
    det_rng_set_seed(&pl->effect_rand, 0x4e50415254494e45LL); /* "NPARINE" */
}

void particles_live_pin(struct particles_live *pl, int64_t world_seed, int64_t t)
{
    if (!pl->pinned || pl->pin_math.seed != world_seed)
    {
        det_pin_init(&pl->pin_math, DET_PIN_FX_MATH, world_seed);
        det_pin_init(&pl->pin_seed, DET_PIN_FX_SEED, world_seed);
        det_pin_init(&pl->pin_effect, DET_PIN_EFFECT, world_seed);
        pl->pinned = 1;
    }
    pl->pin_t = t;
}

/* EffectRenderer.rand */
static det_rng *effect_rng(struct particles_live *pl)
{
    return pl->pinned ? det_pin_at(&pl->pin_effect, pl->pin_t) : &pl->effect_rand;
}

/* ------------------------------------------------------------ construction */

static float clamp01(float v)
{
    if (v < 0.0F) v = 0.0F;
    if (v > 1.0F) v = 1.0F;
    return v;
}

/* Entity.setSize on the client: the box grows from its min corner (the
 * position is not re-centred until the next move). */
static void fx_set_size(struct live_fx *f, float w, float h)
{
    if (w != f->e.width || h != f->e.height)
    {
        f->e.width = w;
        f->e.height = h;
        f->e.bounding_box.max_x = f->e.bounding_box.min_x + (double)f->e.width;
        f->e.bounding_box.max_z = f->e.bounding_box.min_z + (double)f->e.width;
        f->e.bounding_box.max_y = f->e.bounding_box.min_y + (double)f->e.height;
    }
}

/* EntityFX.setParticleTextureIndex */
static void fx_set_tex(struct live_fx *f, int i)
{
    f->tix = i % 16;
    f->tiy = i / 16;
}

/* The protected EntityFX(World, x, y, z) constructor, after Entity's: the
 * entity ID, the FX's own Random (one seeder long) and the UUID (two). */
static void fx_base4(struct particles_live *pl, struct live_fx *f, double x, double y, double z)
{
    memset(f, 0, sizeof *f);
    entity_init(&f->e, pl->world);
    f->e.can_trigger_walking = 0;
    (void)det_next_entity_id_role(pl->det, DET_CLIENT);
    f->rand = det_new_random_role(pl->det, DET_CLIENT);
    if (pl->pinned) det_rng_set_seed(&f->rand, det_rng_long(det_pin_at(&pl->pin_seed, pl->pin_t)));
    {
        int64_t msb, lsb;
        det_uuid_role(pl->det, DET_CLIENT, &msb, &lsb);
    }
    entity_set_position(&f->e, 0.0, 0.0, 0.0);
    f->alpha = 1.0F;
    fx_set_size(f, 0.2F, 0.2F);
    f->e.y_offset = f->e.height / 2.0F;
    entity_set_position(&f->e, x, y, z);
    /* lastTickPos = (x, y, z); prevPos stays 0 until the first onUpdate */
    f->red = f->green = f->blue = 1.0F;
    f->jitx = det_rng_float(&f->rand) * 3.0F;
    f->jity = det_rng_float(&f->rand) * 3.0F;
    f->scale = (det_rng_float(&f->rand) * 0.5F + 0.5F) * 2.0F;
    f->maxage = (int)(4.0F / (det_rng_float(&f->rand) * 0.9F + 0.1F));
    f->age = 0;
}

static double mrand(struct particles_live *pl)
{
    double v = det_math_random_role(pl->det, DET_CLIENT);
    return pl->pinned ? det_rng_double(det_pin_at(&pl->pin_math, pl->pin_t)) : v;
}

/* EntityFX(World, x, y, z, vx, vy, vz) */
static void fx_base7(struct particles_live *pl, struct live_fx *f, double x, double y, double z,
                     double vx, double vy, double vz)
{
    fx_base4(pl, f, x, y, z);
    f->e.motion_x = vx + (double)((float)(mrand(pl) * 2.0 - 1.0) * 0.4F);
    f->e.motion_y = vy + (double)((float)(mrand(pl) * 2.0 - 1.0) * 0.4F);
    f->e.motion_z = vz + (double)((float)(mrand(pl) * 2.0 - 1.0) * 0.4F);
    double m1 = mrand(pl);
    double m2 = mrand(pl);
    float var14 = (float)(m1 + m2 + 1.0) * 0.15F;
    float var15 = (float)sqrt(f->e.motion_x * f->e.motion_x + f->e.motion_y * f->e.motion_y +
                              f->e.motion_z * f->e.motion_z);
    f->e.motion_x = f->e.motion_x / (double)var15 * (double)var14 * 0.4000000059604645;
    f->e.motion_y = f->e.motion_y / (double)var15 * (double)var14 * 0.4000000059604645 + 0.10000000149011612;
    f->e.motion_z = f->e.motion_z / (double)var15 * (double)var14 * 0.4000000059604645;
}

static void fx_multiply_velocity(struct live_fx *f, float v)
{
    f->e.motion_x *= (double)v;
    f->e.motion_y = (f->e.motion_y - 0.10000000149011612) * (double)v + 0.10000000149011612;
    f->e.motion_z *= (double)v;
}

static int kind_layer(int kind)
{
    switch (kind)
    {
    case PLIVE_DIGGING: case PLIVE_BLOCKDUST: case PLIVE_HUGE_EXPLODE: return 1;
    case PLIVE_BREAKING: return 2;
    case PLIVE_LARGE_EXPLODE: case PLIVE_CRIT2: return 3;
    default: return 0;
    }
}

/* A fresh slot at the end of the list; the FX is built in it and then
 * committed by fx_add (the constructor's own onUpdate runs before addEffect,
 * and may itself add FX, so the slot is claimed first). */
static struct live_fx *fx_slot(struct particles_live *pl)
{
    if (pl->n >= PLIVE_CAP) return NULL;
    struct live_fx *f = &pl->fx[pl->n++];
    memset(f, 0, sizeof *f);
    f->dead = 2;    /* under construction: not yet in any layer list */
    return f;
}

/* EffectRenderer.addEffect: a layer at 4000 drops its oldest FX first. */
static void fx_add(struct particles_live *pl, struct live_fx *f)
{
    int layer = kind_layer(f->kind);
    f->layer = layer;
    int count = 0, oldest = -1;
    for (int i = 0; i < pl->n; ++i)
    {
        if (&pl->fx[i] == f || pl->fx[i].dead == 2 || pl->fx[i].layer != layer) continue;
        if (oldest < 0) oldest = i;
        ++count;
    }
    if (count >= PLIVE_LAYER_CAP && oldest >= 0)
    {
        long at = f - pl->fx;
        memmove(&pl->fx[oldest], &pl->fx[oldest + 1], (size_t)(pl->n - oldest - 1) * sizeof *pl->fx);
        pl->n--;
        if (at > oldest) f = &pl->fx[at - 1];
    }
    f->dead = 0;
}

/* The slot for an FX the caller abandons (a spawn that returned null after
 * its slot was claimed never happens: every constructor completes). */

static void fx_update(struct particles_live *pl, struct live_fx *f);
static void fx_move(struct live_fx *f);
static void fx_prev(struct live_fx *f);

/* ------------------------------------------------------------ the classes */

static struct live_fx *new_smoke(struct particles_live *pl, double x, double y, double z,
                                 double vx, double vy, double vz, float s)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_SMOKE;
    f->e.motion_x *= 0.10000000149011612;
    f->e.motion_y *= 0.10000000149011612;
    f->e.motion_z *= 0.10000000149011612;
    f->e.motion_x += vx;
    f->e.motion_y += vy;
    f->e.motion_z += vz;
    f->red = f->green = f->blue = (float)(mrand(pl) * 0.30000001192092896);
    f->scale *= 0.75F;
    f->scale *= s;
    f->base_scale = f->scale;
    f->maxage = (int)(8.0 / (mrand(pl) * 0.8 + 0.2));
    f->maxage = (int)((float)f->maxage * s);
    f->e.no_clip = 0;
    return f;
}

static struct live_fx *new_flame(struct particles_live *pl, double x, double y, double z,
                                 double vx, double vy, double vz)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, vx, vy, vz);
    f->kind = PLIVE_FLAME;
    f->e.motion_x = f->e.motion_x * 0.009999999776482582 + vx;
    f->e.motion_y = f->e.motion_y * 0.009999999776482582 + vy;
    f->e.motion_z = f->e.motion_z * 0.009999999776482582 + vz;
    for (int i = 0; i < 3; ++i)
    {
        /* var10000 = p + (rand.nextFloat() - rand.nextFloat()) * 0.05F, discarded */
        (void)det_rng_float(&f->rand);
        (void)det_rng_float(&f->rand);
    }
    f->base_scale = f->scale;
    f->red = f->green = f->blue = 1.0F;
    f->maxage = (int)(8.0 / (mrand(pl) * 0.8 + 0.2)) + 4;
    f->e.no_clip = 1;
    fx_set_tex(f, 48);
    return f;
}

static struct live_fx *new_reddust(struct particles_live *pl, double x, double y, double z,
                                   float s, float r, float g, float b)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_REDDUST;
    f->e.motion_x *= 0.10000000149011612;
    f->e.motion_y *= 0.10000000149011612;
    f->e.motion_z *= 0.10000000149011612;
    if (r == 0.0F) r = 1.0F;
    float var12 = (float)mrand(pl) * 0.4F + 0.6F;
    f->red = ((float)(mrand(pl) * 0.20000000298023224) + 0.8F) * r * var12;
    f->green = ((float)(mrand(pl) * 0.20000000298023224) + 0.8F) * g * var12;
    f->blue = ((float)(mrand(pl) * 0.20000000298023224) + 0.8F) * b * var12;
    f->scale *= 0.75F;
    f->scale *= s;
    f->base_scale = f->scale;
    f->maxage = (int)(8.0 / (mrand(pl) * 0.8 + 0.2));
    f->maxage = (int)((float)f->maxage * s);
    f->e.no_clip = 0;
    return f;
}

/* EntityEnchantmentTableParticleFX's constructor: the portal's shape with its
 * own tint, scale (field_70565_a), age and one of the 26 glyphs. */
static struct live_fx *new_enchant(struct particles_live *pl, double x, double y, double z,
                                   double vx, double vy, double vz)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, vx, vy, vz);
    f->kind = PLIVE_ENCHANT;
    f->e.motion_x = vx;
    f->e.motion_y = vy;
    f->e.motion_z = vz;
    f->home_x = f->e.pos_x = x;
    f->home_y = f->e.pos_y = y;
    f->home_z = f->e.pos_z = z;
    float var14 = det_rng_float(&f->rand) * 0.6F + 0.4F;
    f->base_scale = f->scale = det_rng_float(&f->rand) * 0.5F + 0.2F;
    f->red = f->green = f->blue = 1.0F * var14;
    f->green *= 0.9F;
    f->red *= 0.9F;
    f->maxage = (int)(mrand(pl) * 10.0) + 30;
    f->e.no_clip = 1;
    fx_set_tex(f, (int)(mrand(pl) * 26.0 + 1.0 + 224.0));
    return f;
}

static struct live_fx *new_portal(struct particles_live *pl, double x, double y, double z,
                                  double vx, double vy, double vz)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, vx, vy, vz);
    f->kind = PLIVE_PORTAL;
    f->e.motion_x = vx;
    f->e.motion_y = vy;
    f->e.motion_z = vz;
    f->home_x = f->e.pos_x = x;
    f->home_y = f->e.pos_y = y;
    f->home_z = f->e.pos_z = z;
    float var14 = det_rng_float(&f->rand) * 0.6F + 0.4F;
    f->base_scale = f->scale = det_rng_float(&f->rand) * 0.2F + 0.5F;
    f->red = f->green = f->blue = 1.0F * var14;
    f->green *= 0.3F;
    f->red *= 0.9F;
    f->maxage = (int)(mrand(pl) * 10.0) + 40;
    f->e.no_clip = 1;
    fx_set_tex(f, (int)(mrand(pl) * 8.0));
    return f;
}

static struct live_fx *new_aura(struct particles_live *pl, double x, double y, double z,
                                double vx, double vy, double vz)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, vx, vy, vz);
    f->kind = PLIVE_AURA;
    float var14 = det_rng_float(&f->rand) * 0.1F + 0.2F;
    f->red = var14;
    f->green = var14;
    f->blue = var14;
    fx_set_tex(f, 0);
    fx_set_size(f, 0.02F, 0.02F);
    f->scale *= det_rng_float(&f->rand) * 0.6F + 0.5F;
    f->e.motion_x *= 0.019999999552965164;
    f->e.motion_y *= 0.019999999552965164;
    f->e.motion_z *= 0.019999999552965164;
    f->maxage = (int)(20.0 / (mrand(pl) * 0.8 + 0.2));
    f->e.no_clip = 1;
    return f;
}

static struct live_fx *new_rain(struct particles_live *pl, double x, double y, double z)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_RAIN;
    f->e.motion_x *= 0.30000001192092896;
    f->e.motion_y = (double)((float)mrand(pl) * 0.2F + 0.1F);
    f->e.motion_z *= 0.30000001192092896;
    f->red = 1.0F;
    f->green = 1.0F;
    f->blue = 1.0F;
    fx_set_tex(f, 19 + det_rng_int_n(&f->rand, 4));
    fx_set_size(f, 0.01F, 0.01F);
    f->gravity = 0.06F;
    f->maxage = (int)(8.0 / (mrand(pl) * 0.8 + 0.2));
    return f;
}

static struct live_fx *new_splash(struct particles_live *pl, double x, double y, double z,
                                  double vx, double vy, double vz)
{
    struct live_fx *f = new_rain(pl, x, y, z);
    if (!f) return NULL;
    f->kind = PLIVE_SPLASH;
    f->gravity = 0.04F;
    ++f->tix;
    if (vy == 0.0 && (vx != 0.0 || vz != 0.0))
    {
        f->e.motion_x = vx;
        f->e.motion_y = vy + 0.1;
        f->e.motion_z = vz;
    }
    return f;
}

static struct live_fx *new_explode(struct particles_live *pl, double x, double y, double z,
                                   double vx, double vy, double vz)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, vx, vy, vz);
    f->kind = PLIVE_EXPLODE;
    f->e.motion_x = vx + (double)((float)(mrand(pl) * 2.0 - 1.0) * 0.05F);
    f->e.motion_y = vy + (double)((float)(mrand(pl) * 2.0 - 1.0) * 0.05F);
    f->e.motion_z = vz + (double)((float)(mrand(pl) * 2.0 - 1.0) * 0.05F);
    f->red = f->green = f->blue = det_rng_float(&f->rand) * 0.3F + 0.7F;
    {
        float a = det_rng_float(&f->rand);
        float b = det_rng_float(&f->rand);
        f->scale = a * b * 6.0F + 1.0F;
    }
    f->maxage = (int)(16.0 / ((double)det_rng_float(&f->rand) * 0.8 + 0.2)) + 2;
    return f;
}

/* EntityCritFX.onUpdate, which its constructor also runs once (new_crit
 * calls it directly, not through fx_update's dispatch). */
static void crit_update(struct live_fx *f)
{
    fx_prev(f);
    if (f->age++ >= f->maxage) f->dead = 1;
    fx_move(f);
    f->green = (float)((double)f->green * 0.96);
    f->blue = (float)((double)f->blue * 0.9);
    f->e.motion_x *= 0.699999988079071;
    f->e.motion_y *= 0.699999988079071;
    f->e.motion_z *= 0.699999988079071;
    f->e.motion_y -= 0.019999999552965164;
    if (f->e.on_ground)
    {
        f->e.motion_x *= 0.699999988079071;
        f->e.motion_z *= 0.699999988079071;
    }
}

static struct live_fx *new_crit(struct particles_live *pl, double x, double y, double z,
                                double vx, double vy, double vz, float s)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_CRIT;
    f->e.motion_x *= 0.10000000149011612;
    f->e.motion_y *= 0.10000000149011612;
    f->e.motion_z *= 0.10000000149011612;
    f->e.motion_x += vx * 0.4;
    f->e.motion_y += vy * 0.4;
    f->e.motion_z += vz * 0.4;
    f->red = f->green = f->blue = (float)(mrand(pl) * 0.30000001192092896 + 0.6000000238418579);
    f->scale *= 0.75F;
    f->scale *= s;
    f->base_scale = f->scale;
    f->maxage = (int)(6.0 / (mrand(pl) * 0.8 + 0.6));
    f->maxage = (int)((float)f->maxage * s);
    f->e.no_clip = 0;
    fx_set_tex(f, 65);
    crit_update(f);
    return f;
}

static struct live_fx *new_spell(struct particles_live *pl, double x, double y, double z,
                                 double vx, double vy, double vz)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, vx, vy, vz);
    f->kind = PLIVE_SPELL;
    f->spell_base = 128;
    f->e.motion_y *= 0.20000000298023224;
    if (vx == 0.0 && vz == 0.0)
    {
        f->e.motion_x *= 0.10000000149011612;
        f->e.motion_z *= 0.10000000149011612;
    }
    f->scale *= 0.75F;
    f->maxage = (int)(8.0 / (mrand(pl) * 0.8 + 0.2));
    f->e.no_clip = 0;
    return f;
}

static struct live_fx *new_breaking(struct particles_live *pl, double x, double y, double z,
                                    double vx, double vy, double vz, int item, int meta)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_BREAKING;
    f->has_icon = 1;
    f->item_id = item;
    f->item_meta = meta;
    f->red = f->green = f->blue = 1.0F;
    f->gravity = BLOCKS[80].particle_gravity;    /* Blocks.snow */
    f->scale /= 2.0F;
    if (vx != vx) return f;    /* the (world, x, y, z, item[, meta]) constructors: no motion arguments */
    f->e.motion_x *= 0.10000000149011612;
    f->e.motion_y *= 0.10000000149011612;
    f->e.motion_z *= 0.10000000149011612;
    f->e.motion_x += vx;
    f->e.motion_y += vy;
    f->e.motion_z += vz;
    return f;
}

/* EntityDiggingFX(world, x, y, z, vx, vy, vz, block, meta).applyRenderColor
 * (meta), "blockcrack_ID_META"; EntityBlockDustFX ("blockdust_") then sets
 * the motion to the arguments. Block.getRenderColor is white but for the
 * foliage-coloured blocks (item_color.c), tall grass's from the grass
 * colour map when the live mesher is there. BlockStem's is not kept: a stem
 * has no collision box, so no entity stands or lands on one. */
static struct live_fx *new_block_fx(struct particles_live *pl, double x, double y, double z,
                                    double vx, double vy, double vz, int id, int meta, int dust)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    id &= 4095;
    fx_base7(pl, f, x, y, z, vx, vy, vz);
    f->kind = PLIVE_DIGGING;
    f->has_icon = 1;
    f->tix = 0; f->tiy = 0;
    f->block_id = id;
    f->block_meta = meta;
    f->gravity = BLOCKS[id].particle_gravity;
    f->red = f->green = f->blue = 0.6F;
    f->scale /= 2.0F;
    if (id != 2)
    {
        /* BlockTallGrass's getRenderColor: ColorizerGrass at (0.5, 1.0) */
        int grass = pl->mesher && pl->mesher->tab && pl->mesher->tab->grass_map ?
                    (int)(pl->mesher->tab->grass_map[127 << 8 | 127] & 0xffffff) : -1;
        int col = item_block_color(id, meta, grass);
        if (col < 0) col = 16777215;
        f->red *= (float)(col >> 16 & 255) / 255.0F;
        f->green *= (float)(col >> 8 & 255) / 255.0F;
        f->blue *= (float)(col & 255) / 255.0F;
    }
    if (dust)
    {
        f->kind = PLIVE_BLOCKDUST;
        f->e.motion_x = vx;
        f->e.motion_y = vy;
        f->e.motion_z = vz;
    }
    return f;
}

/* EntityBubbleFX */
static struct live_fx *new_bubble(struct particles_live *pl, double x, double y, double z,
                                  double vx, double vy, double vz)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, vx, vy, vz);
    f->kind = PLIVE_BUBBLE;
    f->red = f->green = f->blue = 1.0F;
    fx_set_tex(f, 32);
    fx_set_size(f, 0.02F, 0.02F);
    f->scale *= det_rng_float(&f->rand) * 0.6F + 0.2F;
    f->e.motion_x = vx * 0.20000000298023224 + (double)((float)(mrand(pl) * 2.0 - 1.0) * 0.02F);
    f->e.motion_y = vy * 0.20000000298023224 + (double)((float)(mrand(pl) * 2.0 - 1.0) * 0.02F);
    f->e.motion_z = vz * 0.20000000298023224 + (double)((float)(mrand(pl) * 2.0 - 1.0) * 0.02F);
    f->maxage = (int)(8.0 / (mrand(pl) * 0.8 + 0.2));
    return f;
}

static float pl_mh_sin(float f);

/* EntityNoteFX(world, x, y, z, note / 24, 0, 0), scale 2: the colour from
 * the note's pitch, texture 64, six ticks */
static struct live_fx *new_note(struct particles_live *pl, double x, double y, double z, double vx)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_NOTE;
    f->e.motion_x *= 0.009999999776482582;
    f->e.motion_y *= 0.009999999776482582;
    f->e.motion_z *= 0.009999999776482582;
    f->e.motion_y += 0.2;
    f->red = pl_mh_sin(((float)vx + 0.0F) * (float)M_PI * 2.0F) * 0.65F + 0.35F;
    f->green = pl_mh_sin(((float)vx + 0.33333334F) * (float)M_PI * 2.0F) * 0.65F + 0.35F;
    f->blue = pl_mh_sin(((float)vx + 0.6666667F) * (float)M_PI * 2.0F) * 0.65F + 0.35F;
    f->scale *= 0.75F;
    f->scale *= 2.0F;
    f->base_scale = f->scale;
    f->maxage = 6;
    f->e.no_clip = 0;
    fx_set_tex(f, 64);
    return f;
}

/* EntityHeartFX(world, x, y, z, vx, vy, vz), scale 2: texture 80, sixteen
 * ticks; its motion arguments are dropped */
static struct live_fx *new_heart(struct particles_live *pl, double x, double y, double z)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_HEART;
    f->e.motion_x *= 0.009999999776482582;
    f->e.motion_y *= 0.009999999776482582;
    f->e.motion_z *= 0.009999999776482582;
    f->e.motion_y += 0.1;
    f->scale *= 0.75F;
    f->scale *= 2.0F;
    f->base_scale = f->scale;
    f->maxage = 16;
    f->e.no_clip = 0;
    fx_set_tex(f, 80);
    return f;
}

static struct live_fx *new_lava(struct particles_live *pl, double x, double y, double z)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_LAVA;
    f->e.motion_x *= 0.800000011920929;
    f->e.motion_y *= 0.800000011920929;
    f->e.motion_z *= 0.800000011920929;
    f->e.motion_y = (double)(det_rng_float(&f->rand) * 0.4F + 0.05F);
    f->red = f->green = f->blue = 1.0F;
    f->scale *= det_rng_float(&f->rand) * 2.0F + 0.2F;
    f->base_scale = f->scale;
    f->maxage = (int)(16.0 / (mrand(pl) * 0.8 + 0.2));
    f->e.no_clip = 0;
    fx_set_tex(f, 49);
    return f;
}

static struct live_fx *new_drop(struct particles_live *pl, double x, double y, double z, int lava)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_DROP;
    f->e.motion_x = f->e.motion_y = f->e.motion_z = 0.0;
    if (!lava) { f->red = 0.0F; f->green = 0.0F; f->blue = 1.0F; }
    else { f->red = 1.0F; f->green = 0.0F; f->blue = 0.0F; }
    fx_set_tex(f, 113);
    fx_set_size(f, 0.01F, 0.01F);
    f->gravity = 0.06F;
    f->lava = lava;
    f->bob = 40;
    f->maxage = (int)(64.0 / (mrand(pl) * 0.8 + 0.2));
    f->e.motion_x = f->e.motion_y = f->e.motion_z = 0.0;
    return f;
}

static struct live_fx *new_suspend(struct particles_live *pl, double x, double y, double z,
                                   double vx, double vy, double vz)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y - 0.125, z, vx, vy, vz);
    f->kind = PLIVE_SUSPEND;
    f->red = 0.4F;
    f->green = 0.4F;
    f->blue = 0.7F;
    fx_set_tex(f, 0);
    fx_set_size(f, 0.01F, 0.01F);
    f->scale *= det_rng_float(&f->rand) * 0.6F + 0.2F;
    f->e.motion_x = vx * 0.0;
    f->e.motion_y = vy * 0.0;
    f->e.motion_z = vz * 0.0;
    f->maxage = (int)(16.0 / (mrand(pl) * 0.8 + 0.2));
    return f;
}

static struct live_fx *new_huge(struct particles_live *pl, double x, double y, double z)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_HUGE_EXPLODE;
    f->tstart = 0;
    f->tmax = 8;
    return f;
}

static struct live_fx *new_large(struct particles_live *pl, double x, double y, double z, double vx)
{
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    fx_base7(pl, f, x, y, z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_LARGE_EXPLODE;
    f->vq = 6 + det_rng_int_n(&f->rand, 4);
    f->red = f->green = f->blue = det_rng_float(&f->rand) * 0.6F + 0.4F;
    f->vs = 1.0F - (float)vx * 0.5F;
    f->vx = 0;
    f->brf = 61680;
    return f;
}

/* EntityCrit2FX(world, entity, name): the 7-argument constructor at the
 * entity's middle with its motion, then one onUpdate. */
static struct live_fx *new_crit2(struct particles_live *pl, int id, int magic)
{
    struct plive_target t;
    if (!pl->target || !pl->target(pl->target_ctx, id, &t)) return NULL;
    struct live_fx *f = fx_slot(pl);
    if (!f) return NULL;
    /* the entity's motion only offsets the base constructor's motion, which
     * the emitter never reads; the draws are the same for any value */
    fx_base7(pl, f, t.x, t.min_y + (double)(t.height / 2.0F), t.z, 0.0, 0.0, 0.0);
    f->kind = PLIVE_CRIT2;
    f->ent_id = id;
    f->mlife = 3;
    f->magic = magic;
    fx_update(pl, f);
    return f;
}

/* -------------------------------------------------------- doSpawnParticle */

struct live_fx *particles_live_spawn(struct particles_live *pl, const char *name,
                                     double x, double y, double z, double vx, double vy, double vz)
{
    if (!pl->det || !name) return NULL;
    int setting = pl->setting;
    if (setting == 1 && pl->world_rand && det_rng_int_n(pl->world_rand, 3) == 0) setting = 2;

    double var15 = pl->view_x - x;
    double var17 = pl->view_y - y;
    double var19 = pl->view_z - z;
    struct live_fx *f = NULL;

    if (!strcmp(name, "hugeexplosion")) f = new_huge(pl, x, y, z);
    else if (!strcmp(name, "largeexplode")) f = new_large(pl, x, y, z, vx);
    if (f)
    {
        fx_add(pl, f);
        return f;
    }

    if (var15 * var15 + var17 * var17 + var19 * var19 > 16.0 * 16.0) return NULL;
    if (setting > 1) return NULL;

    if (!strcmp(name, "bubble")) f = new_bubble(pl, x, y, z, vx, vy, vz);
    else if (!strcmp(name, "suspended")) f = new_suspend(pl, x, y, z, vx, vy, vz);
    else if (!strcmp(name, "depthsuspend") || !strcmp(name, "townaura")) f = new_aura(pl, x, y, z, vx, vy, vz);
    else if (!strcmp(name, "happyVillager"))
    {
        f = new_aura(pl, x, y, z, vx, vy, vz);
        if (f)
        {
            fx_set_tex(f, 82);
            f->red = f->green = f->blue = 1.0F;
        }
    }
    else if (!strcmp(name, "crit")) f = new_crit(pl, x, y, z, vx, vy, vz, 1.0F);
    else if (!strcmp(name, "magicCrit"))
    {
        f = new_crit(pl, x, y, z, vx, vy, vz, 1.0F);
        if (f)
        {
            f->red *= 0.3F;
            f->green *= 0.8F;
            ++f->tix;
        }
    }
    else if (!strcmp(name, "smoke")) f = new_smoke(pl, x, y, z, vx, vy, vz, 1.0F);
    else if (!strcmp(name, "spell")) f = new_spell(pl, x, y, z, vx, vy, vz);
    else if (!strcmp(name, "mobSpell") || !strcmp(name, "mobSpellAmbient"))
    {
        /* EntitySpellParticleFX(x, y, z, 0, 0, 0), the colour from the
         * arguments (setRBGColorF), the ambient one at alpha 0.15 */
        f = new_spell(pl, x, y, z, 0.0, 0.0, 0.0);
        if (f)
        {
            if (name[8] == 'A') f->alpha = 0.15F;
            f->red = (float)vx;
            f->green = (float)vy;
            f->blue = (float)vz;
        }
    }
    else if (!strcmp(name, "witchMagic"))
    {
        f = new_spell(pl, x, y, z, vx, vy, vz);
        if (f)
        {
            f->spell_base = 144;
            float v = (pl->world_rand ? det_rng_float(pl->world_rand) : 0.0F) * 0.5F + 0.35F;
            f->red = 1.0F * v;
            f->green = 0.0F * v;
            f->blue = 1.0F * v;
        }
    }
    else if (!strcmp(name, "snowballpoof")) f = new_breaking(pl, x, y, z, NAN, 0.0, 0.0, 332, 0);
    else if (!strcmp(name, "slime")) f = new_breaking(pl, x, y, z, NAN, 0.0, 0.0, 341, 0);
    else if (!strcmp(name, "instantSpell"))
    {
        f = new_spell(pl, x, y, z, vx, vy, vz);
        if (f) f->spell_base = 144;
    }
    else if (!strcmp(name, "portal")) f = new_portal(pl, x, y, z, vx, vy, vz);
    else if (!strcmp(name, "enchantmenttable")) f = new_enchant(pl, x, y, z, vx, vy, vz);
    else if (!strcmp(name, "note")) f = new_note(pl, x, y, z, vx);
    else if (!strcmp(name, "heart")) f = new_heart(pl, x, y, z);
    else if (!strcmp(name, "angryVillager"))
    {
        f = new_heart(pl, x, y + 0.5, z);
        if (f)
        {
            fx_set_tex(f, 81);
            f->red = f->green = f->blue = 1.0F;
        }
    }
    else if (!strcmp(name, "explode")) f = new_explode(pl, x, y, z, vx, vy, vz);
    else if (!strcmp(name, "flame")) f = new_flame(pl, x, y, z, vx, vy, vz);
    else if (!strcmp(name, "lava")) f = new_lava(pl, x, y, z);
    else if (!strcmp(name, "splash")) f = new_splash(pl, x, y, z, vx, vy, vz);
    else if (!strcmp(name, "largesmoke")) f = new_smoke(pl, x, y, z, vx, vy, vz, 2.5F);
    else if (!strcmp(name, "reddust")) f = new_reddust(pl, x, y, z, 1.0F, (float)vx, (float)vy, (float)vz);
    else if (!strcmp(name, "dripWater")) f = new_drop(pl, x, y, z, 0);
    else if (!strcmp(name, "dripLava")) f = new_drop(pl, x, y, z, 1);
    else if (!strncmp(name, "iconcrack_", 10))
    {
        int item = 0, meta = 0;
        if (sscanf(name + 10, "%d_%d", &item, &meta) < 1) return NULL;
        f = new_breaking(pl, x, y, z, vx, vy, vz, item, meta);
    }
    else if (!strncmp(name, "blockcrack_", 11) || !strncmp(name, "blockdust_", 10))
    {
        int dust = name[5] == 'd', id = 0, meta = 0;
        if (sscanf(name + (dust ? 10 : 11), "%d_%d", &id, &meta) != 2) return NULL;
        f = new_block_fx(pl, x, y, z, vx, vy, vz, id, meta, dust);
    }
    else
    {
        /* a name this port has no class for: nothing is constructed, which
         * leaves the client streams short of the oracle's */
        return NULL;
    }
    if (f) fx_add(pl, f);
    return f;
}

/* ---------------------------------------------------------- the dig flow */

/* EntityDiggingFX's constructor tail: the block icon, gravity, the 0.6 tint,
 * half scale, then applyColourMultiplier. The icon's uv comes from the block
 * atlas at draw time (the mesher's icon table by (id, meta, side 0)). */
static void fx_digging_setup(struct particles_live *pl, struct live_fx *f,
                             int id, int meta, int cx, int cy, int cz)
{
    f->kind = PLIVE_DIGGING;
    f->has_icon = 1;
    f->tix = 0; f->tiy = 0;
    f->block_id = id;
    f->block_meta = meta;
    f->gravity = BLOCKS[id].particle_gravity;
    f->red = f->green = f->blue = 0.6F;
    f->scale /= 2.0F;

    /* applyColourMultiplier: Blocks.grass returns this unchanged; the other
     * overrides carry the 3x3 biome average (leaves: the foliage map). */
    if (id != 2)
    {
        int col = 16777215;
        if (id == 18 && pl->mesher)
            col = (int)rb_foliage_color_multiplier(pl->mesher, cx, cy, cz);
        f->red *= (float)(col >> 16 & 255) / 255.0F;
        f->green *= (float)(col >> 8 & 255) / 255.0F;
        f->blue *= (float)(col & 255) / 255.0F;
    }
}

void particles_live_hit(struct particles_live *pl, int x, int y, int z, int side)
{
    int id = world_get_block(pl->world, x, y, z) & 4095;
    if (id == 0) return;   /* getMaterial() != Material.air */

    /* EffectRenderer.rand's three nextDouble, inside the block's bounds: the
     * shared Block's, which this tick's mouse-over ray trace left at
     * setBlockBoundsBasedOnState's for this block (a fence's arms, a door's
     * combined halves); a stair's trace leaves octant 7's */
    double b[6];
    raytrace_block_bounds(pl->world, x, y, z, b);
    if (collide_stairs_octant(id)) { b[0] = b[1] = b[2] = 0.5; b[3] = b[4] = b[5] = 1.0; }
    /* Block's bounds are doubles (the float setBlockBounds widened) and
     * var6 is 0.1F: the sums are in double with (double)0.1F, whose extra
     * 1.5e-9 keeps a side-5 particle's box off the next cell (its collision
     * query then leaves a stair's shared bounds alone) */
    double minx = (double)(float)b[0], maxx = (double)(float)b[3];
    double miny = (double)(float)b[1], maxy = (double)(float)b[4];
    double minz = (double)(float)b[2], maxz = (double)(float)b[5];
    const double f6 = (double)0.1F, f2 = (double)(0.1F * 2.0F);
    double px = (double)x + det_rng_double(effect_rng(pl)) * (maxx - minx - f2) + f6 + minx;
    double py = (double)y + det_rng_double(effect_rng(pl)) * (maxy - miny - f2) + f6 + miny;
    double pz = (double)z + det_rng_double(effect_rng(pl)) * (maxz - minz - f2) + f6 + minz;
    if (side == 0) py = (double)y + miny - f6;
    if (side == 1) py = (double)y + maxy + f6;
    if (side == 2) pz = (double)z + minz - f6;
    if (side == 3) pz = (double)z + maxz + f6;
    if (side == 4) px = (double)x + minx - f6;
    if (side == 5) px = (double)x + maxx + f6;

    struct live_fx *f = fx_slot(pl);
    if (!f) return;
    fx_base7(pl, f, px, py, pz, 0.0, 0.0, 0.0);
    int meta = world_get_meta(pl->world, x, y, z) & 15;
    fx_digging_setup(pl, f, id, meta, x, y, z);

    /* multiplyVelocity(0.2F).multipleParticleScaleBy(0.6F) */
    fx_multiply_velocity(f, 0.2F);
    fx_set_size(f, 0.2F * 0.6F, 0.2F * 0.6F);
    f->scale *= 0.6F;
    fx_add(pl, f);
}

void particles_live_destroy(struct particles_live *pl, int x, int y, int z, int id, int meta)
{
    if (id == 0) return;

    for (int var7 = 0; var7 < 4; ++var7)
        for (int var8 = 0; var8 < 4; ++var8)
            for (int var9 = 0; var9 < 4; ++var9)
            {
                double dx = (double)x + ((double)var7 + 0.5) / 4.0;
                double dy = (double)y + ((double)var8 + 0.5) / 4.0;
                double dz = (double)z + ((double)var9 + 0.5) / 4.0;
                struct live_fx *f = fx_slot(pl);
                if (!f) return;
                fx_base7(pl, f, dx, dy, dz, dx - (double)x - 0.5, dy - (double)y - 0.5, dz - (double)z - 0.5);
                fx_digging_setup(pl, f, id, meta, x, y, z);
                fx_add(pl, f);
            }
}

void particles_live_explosion(struct particles_live *pl, const char *kind,
                              double x, double y, double z, double mx, double my, double mz)
{
    (void)particles_live_spawn(pl, kind, x, y, z, mx, my, mz);
}

/* ----------------------------------------------------- the display ticks */

/* World.doesBlockHaveSolidTopSurface */
static int solid_top(struct world *w, int x, int y, int z)
{
    int id = world_get_block(w, x, y, z) & 4095;
    int meta = world_get_meta(w, x, y, z);
    const char *c = BLOCKS[id].class_name;

    if (MATERIALS[BLOCKS[id].material].is_opaque && BLOCKS[id].normal_block) return 1;
    if (c && !strcmp(c, "BlockStairs")) return (meta & 4) == 4;
    if (c && (!strcmp(c, "BlockSlab") || !strcmp(c, "BlockStoneSlab") || !strcmp(c, "BlockWoodSlab"))) return (meta & 8) == 8;
    if (c && !strcmp(c, "BlockHopper")) return 1;
    if (c && !strcmp(c, "BlockSnow")) return (meta & 7) == 7;
    return 0;
}

static int block_at(struct particles_live *pl, int x, int y, int z)
{
    return world_get_block(pl->world, x, y, z) & 4095;
}

/* BlockFire.func_149844_e: the block at (x, y, z) catches fire */
static int fire_can_catch(struct particles_live *pl, int x, int y, int z)
{
    return FIRE_FLAM[block_at(pl, x, y, z)] > 0;
}

/* World.spawnParticle: the IWorldAccess hook, then RenderGlobal.spawnParticle */
static void spawn(struct particles_live *pl, const char *name, double x, double y, double z,
                  double vx, double vy, double vz)
{
    if (pl->on_spawn)
    {
        const double a[6] = {x, y, z, vx, vy, vz};
        pl->on_spawn(pl->spawn_ctx, name, a);
    }
    (void)particles_live_spawn(pl, name, x, y, z, vx, vy, vz);
}

/* RenderGlobal.spawnParticle straight from playAuxSFX: no World in between */
static void spawn_rg(struct particles_live *pl, const char *name, double x, double y, double z,
                     double vx, double vy, double vz)
{
    (void)particles_live_spawn(pl, name, x, y, z, vx, vy, vz);
}

/* BlockRedstoneOre.func_150186_m: six particle rows on the world's Random,
 * each a reddust just outside a face that no opaque block covers */
void particles_live_redstone_sparkle(struct particles_live *pl, int x, int y, int z)
{
    det_rng *var5 = pl->world_rand;
    if (!var5) return;
    double var6 = 0.0625;
    for (int var8 = 0; var8 < 6; ++var8)
    {
        double var9 = (double)((float)x + det_rng_float(var5));
        double var11 = (double)((float)y + det_rng_float(var5));
        double var13 = (double)((float)z + det_rng_float(var5));
        if (var8 == 0 && !BLOCKS[block_at(pl, x, y + 1, z)].opaque_cube) var11 = (double)(y + 1) + var6;
        if (var8 == 1 && !BLOCKS[block_at(pl, x, y - 1, z)].opaque_cube) var11 = (double)(y + 0) - var6;
        if (var8 == 2 && !BLOCKS[block_at(pl, x, y, z + 1)].opaque_cube) var13 = (double)(z + 1) + var6;
        if (var8 == 3 && !BLOCKS[block_at(pl, x, y, z - 1)].opaque_cube) var13 = (double)(z + 0) - var6;
        if (var8 == 4 && !BLOCKS[block_at(pl, x + 1, y, z)].opaque_cube) var9 = (double)(x + 1) + var6;
        if (var8 == 5 && !BLOCKS[block_at(pl, x - 1, y, z)].opaque_cube) var9 = (double)(x + 0) - var6;
        if (var9 < (double)x || var9 > (double)(x + 1) || var11 < 0.0 || var11 > (double)(y + 1) ||
            var13 < (double)z || var13 > (double)(z + 1))
            spawn(pl, "reddust", var9, var11, var13, 0.0, 0.0, 0.0);
    }
}

void particles_live_display_tick(struct particles_live *pl, int id, int x, int y, int z, det_rng *r)
{
    struct world *w = pl->world;

    if (id == 50 || id == 76)
    {
        /* BlockTorch, BlockRedstoneTorch (lit) */
        int var6 = world_get_meta(w, x, y, z);
        double var7, var9, var11;
        if (id == 50)
        {
            var7 = (double)((float)x + 0.5F);
            var9 = (double)((float)y + 0.7F);
            var11 = (double)((float)z + 0.5F);
        }
        else
        {
            var7 = (double)((float)x + 0.5F) + (double)(det_rng_float(r) - 0.5F) * 0.2;
            var9 = (double)((float)y + 0.7F) + (double)(det_rng_float(r) - 0.5F) * 0.2;
            var11 = (double)((float)z + 0.5F) + (double)(det_rng_float(r) - 0.5F) * 0.2;
        }
        double var13 = 0.2199999988079071;
        double var15 = 0.27000001072883606;
        double px = var7, py = var9, pz = var11;
        if (var6 == 1) { px = var7 - var15; py = var9 + var13; }
        else if (var6 == 2) { px = var7 + var15; py = var9 + var13; }
        else if (var6 == 3) { py = var9 + var13; pz = var11 - var15; }
        else if (var6 == 4) { py = var9 + var13; pz = var11 + var15; }
        if (id == 50)
        {
            spawn(pl, "smoke", px, py, pz, 0.0, 0.0, 0.0);
            spawn(pl, "flame", px, py, pz, 0.0, 0.0, 0.0);
        }
        else
        {
            spawn(pl, "reddust", px, py, pz, 0.0, 0.0, 0.0);
        }
    }
    else if (id == 117)
    {
        /* BlockBrewingStand: the smoke above the rod */
        double var6 = (double)((float)x + 0.4F + det_rng_float(r) * 0.2F);
        double var8 = (double)((float)y + 0.7F + det_rng_float(r) * 0.3F);
        double var10 = (double)((float)z + 0.4F + det_rng_float(r) * 0.2F);
        spawn(pl, "smoke", var6, var8, var10, 0.0, 0.0, 0.0);
    }
    else if (id == 116)
    {
        /* BlockEnchantmentTable: each bookshelf two out, on the table's row
         * or the one above, with air between, sends a glyph (one roll in 16
         * per ring cell; the inner ring's middle is skipped) */
        for (int var6 = x - 2; var6 <= x + 2; ++var6)
        {
            for (int var7 = z - 2; var7 <= z + 2; ++var7)
            {
                if (var6 > x - 2 && var6 < x + 2 && var7 == z - 1) var7 = z + 2;
                if (det_rng_int_n(r, 16) != 0) continue;

                for (int var8 = y; var8 <= y + 1; ++var8)
                {
                    if ((world_get_block(w, var6, var8, var7) & 4095) != 47) continue;
                    if ((world_get_block(w, (var6 - x) / 2 + x, var8, (var7 - z) / 2 + z) & 4095) != 0) break;

                    double vx = (double)((float)(var6 - x) + det_rng_float(r)) - 0.5;
                    double vy = (double)((float)(var8 - y) - det_rng_float(r) - 1.0F);
                    double vz = (double)((float)(var7 - z) + det_rng_float(r)) - 0.5;
                    spawn(pl, "enchantmenttable", (double)x + 0.5, (double)y + 2.0, (double)z + 0.5, vx, vy, vz);
                }
            }
        }
    }
    else if (id == 62)
    {
        /* BlockFurnace, lit */
        int var6 = world_get_meta(w, x, y, z);
        float var7 = (float)x + 0.5F;
        float var8 = (float)y + 0.0F + det_rng_float(r) * 6.0F / 16.0F;
        float var9 = (float)z + 0.5F;
        float var10 = 0.52F;
        float var11 = det_rng_float(r) * 0.6F - 0.3F;
        double px, py = (double)var8, pz;
        if (var6 == 4) { px = (double)(var7 - var10); pz = (double)(var9 + var11); }
        else if (var6 == 5) { px = (double)(var7 + var10); pz = (double)(var9 + var11); }
        else if (var6 == 2) { px = (double)(var7 + var11); pz = (double)(var9 - var10); }
        else if (var6 == 3) { px = (double)(var7 + var11); pz = (double)(var9 + var10); }
        else return;
        spawn(pl, "smoke", px, py, pz, 0.0, 0.0, 0.0);
        spawn(pl, "flame", px, py, pz, 0.0, 0.0, 0.0);
    }
    else if (id == 74)
    {
        /* BlockRedstoneOre, lit: func_150186_m over the world's own Random */
        particles_live_redstone_sparkle(pl, x, y, z);
    }
    else if (id == 90)
    {
        /* BlockPortal */
        if (det_rng_int_n(r, 100) == 0) (void)det_rng_float(r);    /* the portal sound's pitch */
        for (int var6 = 0; var6 < 4; ++var6)
        {
            double var7 = (double)((float)x + det_rng_float(r));
            double var9 = (double)((float)y + det_rng_float(r));
            double var11 = (double)((float)z + det_rng_float(r));
            int var19 = det_rng_int_n(r, 2) * 2 - 1;
            double var13 = ((double)det_rng_float(r) - 0.5) * 0.5;
            double var15 = ((double)det_rng_float(r) - 0.5) * 0.5;
            double var17 = ((double)det_rng_float(r) - 0.5) * 0.5;
            if (block_at(pl, x - 1, y, z) != 90 && block_at(pl, x + 1, y, z) != 90)
            {
                var7 = (double)x + 0.5 + 0.25 * (double)var19;
                var13 = (double)(det_rng_float(r) * 2.0F * (float)var19);
            }
            else
            {
                var11 = (double)z + 0.5 + 0.25 * (double)var19;
                var17 = (double)(det_rng_float(r) * 2.0F * (float)var19);
            }
            spawn(pl, "portal", var7, var9, var11, var13, var15, var17);
        }
    }
    else if (id == 119)
    {
        /* BlockEndPortal */
        double var6 = (double)((float)x + det_rng_float(r));
        double var8 = (double)((float)y + 0.8F);
        double var10 = (double)((float)z + det_rng_float(r));
        spawn(pl, "smoke", var6, var8, var10, 0.0, 0.0, 0.0);
    }
    else if (id == 130)
    {
        /* BlockEnderChest */
        for (int var6 = 0; var6 < 3; ++var6)
        {
            (void)det_rng_float(r);
            double var9 = (double)((float)y + det_rng_float(r));
            (void)det_rng_float(r);
            int var19 = det_rng_int_n(r, 2) * 2 - 1;
            int var20 = det_rng_int_n(r, 2) * 2 - 1;
            (void)det_rng_float(r);
            double var15 = ((double)det_rng_float(r) - 0.5) * 0.125;
            (void)det_rng_float(r);
            double var11 = (double)z + 0.5 + 0.25 * (double)var20;
            double var17 = (double)(det_rng_float(r) * 1.0F * (float)var20);
            double var7 = (double)x + 0.5 + 0.25 * (double)var19;
            double var13 = (double)(det_rng_float(r) * 1.0F * (float)var19);
            spawn(pl, "portal", var7, var9, var11, var13, var15, var17);
        }
    }
    else if (id == 110)
    {
        /* BlockMycelium */
        if (det_rng_int_n(r, 10) == 0)
        {
            float a = det_rng_float(r);
            double px = (double)((float)x + a);
            double py = (double)((float)y + 1.1F);
            float b = det_rng_float(r);
            double pz = (double)((float)z + b);
            spawn(pl, "townaura", px, py, pz, 0.0, 0.0, 0.0);
        }
    }
    else if (id == 8 || id == 9 || id == 10 || id == 11)
    {
        /* BlockLiquid */
        int water = id == 8 || id == 9;
        if (water && det_rng_int_n(r, 10) == 0)
        {
            int var6 = world_get_meta(w, x, y, z);
            if (var6 <= 0 || var6 >= 8)
            {
                float a = det_rng_float(r), b = det_rng_float(r), c = det_rng_float(r);
                spawn(pl, "suspended", (double)((float)x + a), (double)((float)y + b), (double)((float)z + c), 0.0, 0.0, 0.0);
            }
        }
        if (water && det_rng_int_n(r, 64) == 0)
        {
            int var6 = world_get_meta(w, x, y, z);
            if (var6 > 0 && var6 < 8)
            {
                (void)det_rng_float(r);
                (void)det_rng_float(r);
            }
        }
        if (!water)
        {
            int above = block_at(pl, x, y + 1, z);
            if (BLOCKS[above].material == BLOCKS[0].material && !BLOCKS[above].opaque_cube)
            {
                if (det_rng_int_n(r, 100) == 0)
                {
                    float a = det_rng_float(r);
                    double var21 = (double)((float)x + a);
                    double var22 = (double)y + BLOCKS[id].max_y;
                    float b = det_rng_float(r);
                    double var23 = (double)((float)z + b);
                    spawn(pl, "lava", var21, var22, var23, 0.0, 0.0, 0.0);
                    (void)det_rng_float(r);
                    (void)det_rng_float(r);
                }
                if (det_rng_int_n(r, 200) == 0)
                {
                    (void)det_rng_float(r);
                    (void)det_rng_float(r);
                }
            }
        }
        if (det_rng_int_n(r, 10) == 0 && solid_top(w, x, y - 1, z) &&
            !MATERIALS[BLOCKS[block_at(pl, x, y - 2, z)].material].blocks_movement)
        {
            float a = det_rng_float(r);
            double var21 = (double)((float)x + a);
            double var22 = (double)y - 1.05;
            float b = det_rng_float(r);
            double var23 = (double)((float)z + b);
            spawn(pl, water ? "dripWater" : "dripLava", var21, var22, var23, 0.0, 0.0, 0.0);
        }
    }
    else if (id == 51)
    {
        /* BlockFire */
        if (det_rng_int_n(r, 24) == 0)
        {
            (void)det_rng_float(r);
            (void)det_rng_float(r);
        }
        if (!solid_top(w, x, y - 1, z) && !fire_can_catch(pl, x, y - 1, z))
        {
            static const int side[5][3] = {{-1, 0, 0}, {1, 0, 0}, {0, 0, -1}, {0, 0, 1}, {0, 1, 0}};
            for (int s = 0; s < 5; ++s)
            {
                if (!fire_can_catch(pl, x + side[s][0], y + side[s][1], z + side[s][2])) continue;
                for (int var6 = 0; var6 < 2; ++var6)
                {
                    float var7, var8, var9;
                    if (s == 0) { var7 = (float)x + det_rng_float(r) * 0.1F; var8 = (float)y + det_rng_float(r); var9 = (float)z + det_rng_float(r); }
                    else if (s == 1) { var7 = (float)(x + 1) - det_rng_float(r) * 0.1F; var8 = (float)y + det_rng_float(r); var9 = (float)z + det_rng_float(r); }
                    else if (s == 2) { var7 = (float)x + det_rng_float(r); var8 = (float)y + det_rng_float(r); var9 = (float)z + det_rng_float(r) * 0.1F; }
                    else if (s == 3) { var7 = (float)x + det_rng_float(r); var8 = (float)y + det_rng_float(r); var9 = (float)(z + 1) - det_rng_float(r) * 0.1F; }
                    else { var7 = (float)x + det_rng_float(r); var8 = (float)(y + 1) - det_rng_float(r) * 0.1F; var9 = (float)z + det_rng_float(r); }
                    spawn(pl, "largesmoke", (double)var7, (double)var8, (double)var9, 0.0, 0.0, 0.0);
                }
            }
        }
        else
        {
            for (int var6 = 0; var6 < 3; ++var6)
            {
                float var7 = (float)x + det_rng_float(r);
                float var8 = (float)y + det_rng_float(r) * 0.5F + 0.5F;
                float var9 = (float)z + det_rng_float(r);
                spawn(pl, "largesmoke", (double)var7, (double)var8, (double)var9, 0.0, 0.0, 0.0);
            }
        }
    }
    else if (id == 18 || id == 161)
    {
        /* BlockLeaves: a drip under rain where lightning could strike the
         * cell above (World.canLightningStrikeAt: raining, the sky seen, at
         * the precipitation height, a rainy biome where no snow falls), over
         * no solid top, one roll in 15 */
        int y1 = y + 1;
        if (!(pl->rain > 0.2F) || !world_can_block_see_the_sky(w, x, y1, z) ||
            world_get_precipitation_height(w, x, z) > y1)
            return;
        int biome = world_get_biome(w, x, z);
        if (BIOMES[biome].snow || world_can_snow_at(w, x, y1, z, 0) || !BIOMES[biome].lightning) return;
        if (solid_top(w, x, y - 1, z) || det_rng_int_n(r, 15) != 1) return;
        double var6 = (double)((float)x + det_rng_float(r));
        double var8 = (double)y - 0.05;
        double var10 = (double)((float)z + det_rng_float(r));
        spawn(pl, "dripWater", var6, var8, var10, 0.0, 0.0, 0.0);
    }
}

/* ---------------------------------------------------------- crits, aux */

void particles_live_crit(struct particles_live *pl, int entity_id, int magic)
{
    struct live_fx *f = new_crit2(pl, entity_id, magic);
    if (f) fx_add(pl, f);
}

/* PotionHelper.func_77915_a(damage, false) over getPotionEffects' list order,
 * and ItemPotion.isEffectInstant */
static int potion_color(int damage, int *instant)
{
    struct potion_effect eff[32];
    int n = potion_get_effects_for_damage(damage, eff, 32);
    *instant = 0;
    if (n <= 0) return 3694022;
    float r = 0.0F, g = 0.0F, b = 0.0F, c = 0.0F;
    for (int i = 0; i < n; ++i)
    {
        int col = potion_liquid_color(eff[i].id);
        if (potion_is_instant(eff[i].id)) *instant = 1;
        for (int a = 0; a <= eff[i].amplifier; ++a)
        {
            r += (float)(col >> 16 & 255) / 255.0F;
            g += (float)(col >> 8 & 255) / 255.0F;
            b += (float)(col >> 0 & 255) / 255.0F;
            ++c;
        }
    }
    r = r / c * 255.0F;
    g = g / c * 255.0F;
    b = b / c * 255.0F;
    return (int)r << 16 | (int)g << 8 | (int)b;
}

void particles_live_aux(struct particles_live *pl, int id, int x, int y, int z, int data)
{
    det_rng *var7 = pl->world_rand;
    if (!var7) return;
    char name[48];

    if (id == 2002)
    {
        double var9 = (double)x, var11 = (double)y, var13 = (double)z;
        snprintf(name, sizeof name, "iconcrack_%d_%d", 373, data);
        for (int i = 0; i < 8; ++i)
        {
            double a = det_rng_gaussian(var7) * 0.15;
            double b = det_rng_double(var7) * 0.2;
            double c = det_rng_gaussian(var7) * 0.15;
            spawn_rg(pl, name, var9, var11, var13, a, b, c);
        }
        int instant = 0;
        int var16 = potion_color(data, &instant);
        float var17 = (float)(var16 >> 16 & 255) / 255.0F;
        float var18 = (float)(var16 >> 8 & 255) / 255.0F;
        float var19 = (float)(var16 >> 0 & 255) / 255.0F;
        const char *var20 = instant ? "instantSpell" : "spell";
        for (int i = 0; i < 100; ++i)
        {
            double var22 = det_rng_double(var7) * 4.0;
            double var41 = det_rng_double(var7) * M_PI * 2.0;
            double var26 = fd_cos(var41) * var22;
            double var28 = 0.01 + det_rng_double(var7) * 0.5;
            double var30 = fd_sin(var41) * var22;
            struct live_fx *f = particles_live_spawn(pl, var20, var9 + var26 * 0.1, var11 + 0.3, var13 + var30 * 0.1,
                                                     var26, var28, var30);
            if (f)
            {
                float var33 = 0.75F + det_rng_float(var7) * 0.25F;
                f->red = var17 * var33;
                f->green = var18 * var33;
                f->blue = var19 * var33;
                fx_multiply_velocity(f, (float)var22);
            }
        }
        (void)det_rng_float(var7);   /* the smash sound's pitch */
    }
    else if (id == 2003)
    {
        double var9 = (double)x + 0.5, var11 = (double)y, var13 = (double)z + 0.5;
        snprintf(name, sizeof name, "iconcrack_%d", 381);
        for (int i = 0; i < 8; ++i)
        {
            double a = det_rng_gaussian(var7) * 0.15;
            double b = det_rng_double(var7) * 0.2;
            double c = det_rng_gaussian(var7) * 0.15;
            spawn_rg(pl, name, var9, var11, var13, a, b, c);
        }
        for (double var36 = 0.0; var36 < M_PI * 2.0; var36 += 0.15707963267948966)
        {
            spawn_rg(pl, "portal", var9 + fd_cos(var36) * 5.0, var11 - 0.4, var13 + fd_sin(var36) * 5.0,
                  fd_cos(var36) * -5.0, 0.0, fd_sin(var36) * -5.0);
            spawn_rg(pl, "portal", var9 + fd_cos(var36) * 5.0, var11 - 0.4, var13 + fd_sin(var36) * 5.0,
                  fd_cos(var36) * -7.0, 0.0, fd_sin(var36) * -7.0);
        }
    }
}

/* MathHelper.sin and cos (jmath.h's, whose names render_blocks_int.h
 * takes here) */
extern float MH_SIN[65536];
static int32_t pl_f2i(float f)
{
    if (f != f) return 0;
    if (f >= 2147483647.0f) return INT32_MAX;
    if (f <= -2147483648.0f) return INT32_MIN;
    return (int32_t)f;
}
static float pl_mh_sin(float f) { return MH_SIN[pl_f2i(f * 10430.378f) & 65535]; }
static float pl_mh_cos(float f) { return MH_SIN[pl_f2i(f * 10430.378f + 16384.0f) & 65535]; }

void particles_live_aux_sfx(struct particles_live *pl, int id, int x, int y, int z, int data)
{
    det_rng *var7 = pl->world_rand;

    if (id == 2001)
    {
        /* the break sound, then EffectRenderer.func_147215_a */
        particles_live_destroy(pl, x, y, z, data & 4095, data >> 12 & 255);
        return;
    }
    if (id == 1003)
    {
        /* the door's open or close sound: a Math.random pick, then the
         * pitch off the world's Random either way */
        (void)det_math_random_role(pl->det, DET_CLIENT);
        if (var7) (void)det_rng_float(var7);
        return;
    }
    if (id == 2005)
    {
        /* ItemDye.func_150918_a: per happy villager particle three
         * nextGaussian and three nextFloat of Item.itemRand, over a block
         * that is not air, as high as its bounds */
        int bid = world_get_block(pl->world, x, y, z) & 4095;
        if (!BLOCKS[bid].exists || BLOCKS[bid].material == 0) return;
        double b[6];
        raytrace_block_bounds(pl->world, x, y, z, b);
        det_split *ir = det_split_find(pl->det, "./net/minecraft/item/Item.java:itemRand");
        if (!ir) ir = det_split_random(pl->det, "./net/minecraft/item/Item.java:itemRand");
        for (int i = 0; i < (data == 0 ? 15 : data); ++i)
        {
            double vx = det_split_gaussian_role(pl->det, ir, DET_CLIENT) * 0.02;
            double vy = det_split_gaussian_role(pl->det, ir, DET_CLIENT) * 0.02;
            double vz = det_split_gaussian_role(pl->det, ir, DET_CLIENT) * 0.02;
            double px = (double)((float)x + det_split_float_role(pl->det, ir, DET_CLIENT));
            double py = (double)y + (double)det_split_float_role(pl->det, ir, DET_CLIENT) * b[4];
            double pz = (double)((float)z + det_split_float_role(pl->det, ir, DET_CLIENT));
            spawn(pl, "happyVillager", px, py, pz, vx, vy, vz);
        }
        return;
    }
    if (!var7) return;

    switch (id)
    {
    case 1004: case 1007: case 1008: case 1009: case 1010: case 1011: case 1012:
    case 1014: case 1015: case 1016: case 1017:
        /* (nextFloat - nextFloat): the sound's pitch */
        (void)det_rng_float(var7);
        (void)det_rng_float(var7);
        return;
    case 1020: case 1021: case 1022:
        (void)det_rng_float(var7);
        return;
    case 2000:
    {
        /* the dispenser's smoke, out of the face data points at */
        int var34 = data % 3 - 1, var10 = data / 3 % 3 - 1;
        double var11 = (double)x + (double)var34 * 0.6 + 0.5;
        double var13 = (double)y + 0.5;
        double var35 = (double)z + (double)var10 * 0.6 + 0.5;
        for (int i = 0; i < 10; ++i)
        {
            double var38 = det_rng_double(var7) * 0.2 + 0.01;
            double var39 = var11 + (double)var34 * 0.01 + (det_rng_double(var7) - 0.5) * (double)var10 * 0.5;
            double var22 = var13 + (det_rng_double(var7) - 0.5) * 0.5;
            double var41 = var35 + (double)var10 * 0.01 + (det_rng_double(var7) - 0.5) * (double)var34 * 0.5;
            double var26 = (double)var34 * var38 + det_rng_gaussian(var7) * 0.01;
            double var28 = -0.03 + det_rng_gaussian(var7) * 0.01;
            double var30 = (double)var10 * var38 + det_rng_gaussian(var7) * 0.01;
            spawn_rg(pl, "smoke", var39, var22, var41, var26, var28, var30);
        }
        return;
    }
    case 2002:
    case 2003:
        particles_live_aux(pl, id, x, y, z, data);
        return;
    case 2004:
        /* the spawner's burst */
        for (int i = 0; i < 20; ++i)
        {
            double var22 = (double)x + 0.5 + ((double)det_rng_float(var7) - 0.5) * 2.0;
            double var41 = (double)y + 0.5 + ((double)det_rng_float(var7) - 0.5) * 2.0;
            double var26 = (double)z + 0.5 + ((double)det_rng_float(var7) - 0.5) * 2.0;
            spawn(pl, "smoke", var22, var41, var26, 0.0, 0.0, 0.0);
            spawn(pl, "flame", var22, var41, var26, 0.0, 0.0, 0.0);
        }
        return;
    case 2006:
    {
        /* a fall's dust, 150 per 2.5 blocks of fall over the landing block */
        int bid = world_get_block(pl->world, x, y, z) & 4095;
        if (!BLOCKS[bid].exists || BLOCKS[bid].material == 0) return;
        float lim = 0.2F + (float)data / 15.0F;
        double var21 = (double)(lim < 10.0F ? lim : 10.0F);
        if (var21 > 2.5) var21 = 2.5;
        int var23 = (int)(150.0 * var21);
        char name[48];
        snprintf(name, sizeof name, "blockdust_%d_%d", bid, world_get_meta(pl->world, x, y, z));
        for (int i = 0; i < var23; ++i)
        {
            /* MathHelper.randomFloatClamp */
            float var25 = det_rng_float(var7) * ((float)M_PI * 2.0F - 0.0F) + 0.0F;
            double var26 = (double)(det_rng_float(var7) * (1.0F - 0.75F) + 0.75F);
            double var28 = 0.20000000298023224 + var21 / 100.0;
            double var30 = (double)(pl_mh_cos(var25) * 0.2F) * var26 * var26 * (var21 + 0.2);
            double var32 = (double)(pl_mh_sin(var25) * 0.2F) * var26 * var26 * (var21 + 0.2);
            spawn(pl, name, (double)((float)x + 0.5F), (double)((float)y + 1.0F), (double)((float)z + 0.5F),
                  var30, var28, var32);
        }
        return;
    }
    default:
        return;
    }
}

void particles_live_explosion_packet(struct particles_live *pl, double x, double y, double z, float size,
                                     int naffected, const struct s27_xp *xp, int nxp)
{
    det_rng *wr = pl->world_rand;
    /* new Explosion: its explosionRNG, one seeder long */
    (void)det_seeder_next_long(pl->det, DET_CLIENT);
    if (wr)
    {
        (void)det_rng_float(wr);
        (void)det_rng_float(wr);
    }
    particles_live_spawn(pl, size >= 2.0F ? "hugeexplosion" : "largeexplode", x, y, z, 1.0, 0.0, 0.0);
    for (int i = 0, j = 0; wr && i < naffected; ++i)
    {
        for (int k = 0; k < 5; ++k) (void)det_rng_float(wr);
        /* dropBlockAsItemWithChance on the client world: Block's own body
         * is !isRemote only, but BlockOre's xp roll
         * (MathHelper.getRandomIntegerInRange), BlockRedstoneOre's and
         * BlockMobSpawner's run on either side */
        for (; j < nxp && xp[j].index == i; ++j)
        {
            switch (xp[j].block)
            {
            case 16: (void)det_rng_int_n(wr, 3); break;                 /* coal: 0..2 */
            case 56: case 129: (void)det_rng_int_n(wr, 5); break;       /* diamond, emerald: 3..7 */
            case 21: case 153: (void)det_rng_int_n(wr, 4); break;       /* lapis, quartz: 2..5 */
            case 73: case 74: (void)det_rng_int_n(wr, 5); break;        /* redstone: 1 + nextInt(5) */
            case 52: (void)det_rng_int_n(wr, 15); (void)det_rng_int_n(wr, 15); break;
            }
        }
    }
}

void particles_live_explosion_b(struct particles_live *pl, double x, double y, double z, float size,
                                const int (*blocks)[3], int nblocks)
{
    det_rng *wr = pl->world_rand;
    if (!wr) return;
    /* the explosion sound's pitch */
    (void)det_rng_float(wr);
    (void)det_rng_float(wr);
    spawn(pl, size >= 2.0F ? "hugeexplosion" : "largeexplode", x, y, z, 1.0, 0.0, 0.0);
    for (int i = 0; i < nblocks; ++i)
    {
        int bx = blocks[i][0], by = blocks[i][1], bz = blocks[i][2];
        double var8 = (double)((float)bx + det_rng_float(wr));
        double var10 = (double)((float)by + det_rng_float(wr));
        double var12 = (double)((float)bz + det_rng_float(wr));
        double var14 = var8 - x;
        double var16 = var10 - y;
        double var18 = var12 - z;
        double var20 = (double)(float)sqrt(var14 * var14 + var16 * var16 + var18 * var18);
        var14 /= var20;
        var16 /= var20;
        var18 /= var20;
        double var22 = 0.5 / (var20 / (double)size + 0.1);
        {
            float a = det_rng_float(wr);
            float b = det_rng_float(wr);
            var22 *= (double)(a * b + 0.3F);
        }
        var14 *= var22;
        var16 *= var22;
        var18 *= var22;
        spawn(pl, "explode", (var8 + x * 1.0) / 2.0, (var10 + y * 1.0) / 2.0, (var12 + z * 1.0) / 2.0,
              var14, var16, var18);
        spawn(pl, "smoke", var8, var10, var12, var14, var16, var18);
    }
}

/* ------------------------------------------------------------------ rain */

static struct perlin temp_noise;

/* before main: the same for every environment */
__attribute__((constructor)) static void temp_noise_init(void)
{
    jrand r;
    jr_seed(&r, 1234);
    perlin_init(&temp_noise, &r, 1);
}

/* BiomeGenBase.getFloatTemperature */
static float biome_temp(int biome, int x, int y, int z)
{
    if (y > 64)
    {
        float var4 = (float)perlin_point(&temp_noise, (double)x * 1.0 / 8.0, (double)z * 1.0 / 8.0) * 4.0F;
        return BIOMES[biome].temperature - (var4 + (float)y - 64.0F) * 0.05F / 30.0F;
    }
    return BIOMES[biome].temperature;
}

void particles_live_rain(struct particles_live *pl, float rain, int fancy, int renderer_update_count,
                         int *rain_sound_counter)
{
    float var1 = rain;
    if (!fancy) var1 /= 2.0F;
    if (var1 == 0.0F) return;

    jrand random;
    jr_seed(&random, (int64_t)renderer_update_count * 312987231LL);
    int var4 = mh_floor(pl->view_x);
    int var5 = mh_floor(pl->view_y);
    int var6 = mh_floor(pl->view_z);
    int var7 = 10;
    double var10 = 0.0;
    int var14 = 0;
    int var15 = (int)(100.0F * var1 * var1);
    if (pl->setting == 1) var15 >>= 1;
    else if (pl->setting == 2) var15 = 0;

    for (int var16 = 0; var16 < var15; ++var16)
    {
        int var17 = var4 + jr_int_n(&random, var7) - jr_int_n(&random, var7);
        int var18 = var6 + jr_int_n(&random, var7) - jr_int_n(&random, var7);
        int var19 = world_get_precipitation_height(pl->world, var17, var18);
        int var20 = block_at(pl, var17, var19 - 1, var18);
        int var21 = world_get_biome(pl->world, var17, var18);

        if (var19 <= var5 + var7 && var19 >= var5 - var7 && BIOMES[var21].lightning &&
            biome_temp(var21, var17, var19, var18) >= 0.15F)
        {
            float var22 = jr_float(&random);
            float var23 = jr_float(&random);
            double min_y = BLOCKS[var20].min_y;

            if (BLOCKS[var20].material == BLOCKS[10].material)
            {
                struct live_fx *f = new_smoke(pl, (double)((float)var17 + var22), (double)((float)var19 + 0.1F) - min_y,
                                              (double)((float)var18 + var23), 0.0, 0.0, 0.0, 1.0F);
                if (f) fx_add(pl, f);
            }
            else if (BLOCKS[var20].material != BLOCKS[0].material)
            {
                ++var14;
                if (jr_int_n(&random, var14) == 0)
                    var10 = (double)((float)var19 + 0.1F) - min_y;
                struct live_fx *f = new_rain(pl, (double)((float)var17 + var22), (double)((float)var19 + 0.1F) - min_y,
                                             (double)((float)var18 + var23));
                if (f) fx_add(pl, f);
            }
        }
    }

    if (var14 > 0 && jr_int_n(&random, 3) < (*rain_sound_counter)++)
    {
        *rain_sound_counter = 0;
        /* the sound's volume and pitch come from the renderer's Random too;
         * nothing after this reads it this tick */
        (void)var10;
    }
}

/* -------------------------------------------------------------- onUpdate */

static void fx_move(struct live_fx *f)
{
    entity_move(&f->e, f->e.motion_x, f->e.motion_y, f->e.motion_z);
}

static void fx_prev(struct live_fx *f)
{
    f->e.prev_pos_x = f->e.pos_x;
    f->e.prev_pos_y = f->e.pos_y;
    f->e.prev_pos_z = f->e.pos_z;
}

/* BlockLiquid.func_149801_b */
static float liquid_height(int meta)
{
    if (meta >= 8) meta = 0;
    return (float)(meta + 1) / 9.0F;
}

/* EntityRainFX's and EntityDropParticleFX's tail: dead inside a liquid or
 * solid block below its surface. */
static void fx_liquid_kill(struct live_fx *f)
{
    int bx = mh_floor(f->e.pos_x), by = mh_floor(f->e.pos_y), bz = mh_floor(f->e.pos_z);
    int id = world_get_block(f->e.world, bx, by, bz) & 4095;
    const struct material_def *m = &MATERIALS[BLOCKS[id].material];
    if (m->is_liquid || m->is_solid)
    {
        double var2 = (double)((float)(by + 1) - liquid_height(world_get_meta(f->e.world, bx, by, bz)));
        if (f->e.pos_y < var2) f->dead = 1;
    }
}

static void fx_update(struct particles_live *pl, struct live_fx *f)
{
    switch (f->kind)
    {
    case PLIVE_HUGE_EXPLODE:
        /* EntityHugeExplodeFX.onUpdate: six largeexplode children per tick
         * at +-4 blocks */
        for (int k = 0; k < 6; ++k)
        {
            double a = det_rng_double(&f->rand), b = det_rng_double(&f->rand);
            double cx = f->e.pos_x + (a - b) * 4.0;
            a = det_rng_double(&f->rand); b = det_rng_double(&f->rand);
            double cy = f->e.pos_y + (a - b) * 4.0;
            a = det_rng_double(&f->rand); b = det_rng_double(&f->rand);
            double cz = f->e.pos_z + (a - b) * 4.0;
            spawn(pl, "largeexplode", cx, cy, cz, (double)((float)f->tstart / (float)f->tmax), 0.0, 0.0);
            /* the list may have moved: f stays valid, spawns only append */
        }
        ++f->tstart;
        if (f->tstart == f->tmax) f->dead = 1;
        return;

    case PLIVE_LARGE_EXPLODE:
        fx_prev(f);
        ++f->vx;
        if (f->vx == f->vq) f->dead = 1;
        return;

    case PLIVE_CRIT2:
    {
        struct plive_target t;
        int have = pl->target && pl->target(pl->target_ctx, f->ent_id, &t);
        for (int var1 = 0; var1 < 16; ++var1)
        {
            double var2 = (double)(det_rng_float(&f->rand) * 2.0F - 1.0F);
            double var4 = (double)(det_rng_float(&f->rand) * 2.0F - 1.0F);
            double var6 = (double)(det_rng_float(&f->rand) * 2.0F - 1.0F);
            if (have && var2 * var2 + var4 * var4 + var6 * var6 <= 1.0)
            {
                double var8 = t.x + var2 * (double)t.width / 4.0;
                double var10 = t.min_y + (double)(t.height / 2.0F) + var4 * (double)t.height / 4.0;
                double var12 = t.z + var6 * (double)t.width / 4.0;
                spawn(pl, f->magic ? "magicCrit" : "crit", var8, var10, var12, var2, var4 + 0.2, var6);
            }
        }
        ++f->life;
        if (f->life >= f->mlife) f->dead = 1;
        return;
    }

    case PLIVE_SMOKE:
    case PLIVE_REDDUST:
    case PLIVE_SPELL:
        fx_prev(f);
        if (f->age++ >= f->maxage) f->dead = 1;
        if (f->kind == PLIVE_SPELL) { int i = f->spell_base + (7 - f->age * 8 / f->maxage); f->tix = i % 16; f->tiy = i / 16; }
        else { int i = 7 - f->age * 8 / f->maxage; f->tix = i % 16; f->tiy = i / 16; }
        if (f->kind != PLIVE_REDDUST) f->e.motion_y += 0.004;
        fx_move(f);
        if (f->e.pos_y == f->e.prev_pos_y)
        {
            f->e.motion_x *= 1.1;
            f->e.motion_z *= 1.1;
        }
        f->e.motion_x *= 0.9599999785423279;
        f->e.motion_y *= 0.9599999785423279;
        f->e.motion_z *= 0.9599999785423279;
        if (f->e.on_ground)
        {
            f->e.motion_x *= 0.699999988079071;
            f->e.motion_z *= 0.699999988079071;
        }
        return;

    case PLIVE_FLAME:
        fx_prev(f);
        if (f->age++ >= f->maxage) f->dead = 1;
        fx_move(f);
        f->e.motion_x *= 0.9599999785423279;
        f->e.motion_y *= 0.9599999785423279;
        f->e.motion_z *= 0.9599999785423279;
        if (f->e.on_ground)
        {
            f->e.motion_x *= 0.699999988079071;
            f->e.motion_z *= 0.699999988079071;
        }
        return;

    case PLIVE_PORTAL:
    {
        fx_prev(f);
        float var1 = (float)f->age / (float)f->maxage;
        float var2 = var1;
        var1 = -var1 + var1 * var1 * 2.0F;
        var1 = 1.0F - var1;
        f->e.pos_x = f->home_x + f->e.motion_x * (double)var1;
        f->e.pos_y = f->home_y + f->e.motion_y * (double)var1 + (double)(1.0F - var2);
        f->e.pos_z = f->home_z + f->e.motion_z * (double)var1;
        if (f->age++ >= f->maxage) f->dead = 1;
        return;
    }

    case PLIVE_ENCHANT:
    {
        /* EntityEnchantmentTableParticleFX.onUpdate: from the glyph's start
         * toward the table, dropping at the end; no collision */
        fx_prev(f);
        float var1 = (float)f->age / (float)f->maxage;
        var1 = 1.0F - var1;
        float var2 = 1.0F - var1;
        var2 *= var2;
        var2 *= var2;
        f->e.pos_x = f->home_x + f->e.motion_x * (double)var1;
        f->e.pos_y = f->home_y + f->e.motion_y * (double)var1 - (double)(var2 * 1.2F);
        f->e.pos_z = f->home_z + f->e.motion_z * (double)var1;
        if (f->age++ >= f->maxage) f->dead = 1;
        return;
    }

    case PLIVE_AURA:
        fx_prev(f);
        fx_move(f);
        f->e.motion_x *= 0.99;
        f->e.motion_y *= 0.99;
        f->e.motion_z *= 0.99;
        if (f->maxage-- <= 0) f->dead = 1;
        return;

    case PLIVE_RAIN:
    case PLIVE_SPLASH:
        fx_prev(f);
        f->e.motion_y -= (double)f->gravity;
        fx_move(f);
        f->e.motion_x *= 0.9800000190734863;
        f->e.motion_y *= 0.9800000190734863;
        f->e.motion_z *= 0.9800000190734863;
        if (f->maxage-- <= 0) f->dead = 1;
        if (f->e.on_ground)
        {
            if (mrand(pl) < 0.5) f->dead = 1;
            f->e.motion_x *= 0.699999988079071;
            f->e.motion_z *= 0.699999988079071;
        }
        fx_liquid_kill(f);
        return;

    case PLIVE_EXPLODE:
        fx_prev(f);
        if (f->age++ >= f->maxage) f->dead = 1;
        { int i = 7 - f->age * 8 / f->maxage; f->tix = i % 16; f->tiy = i / 16; }
        f->e.motion_y += 0.004;
        fx_move(f);
        f->e.motion_x *= 0.8999999761581421;
        f->e.motion_y *= 0.8999999761581421;
        f->e.motion_z *= 0.8999999761581421;
        if (f->e.on_ground)
        {
            f->e.motion_x *= 0.699999988079071;
            f->e.motion_z *= 0.699999988079071;
        }
        return;

    case PLIVE_CRIT:
        crit_update(f);
        return;

    case PLIVE_LAVA:
    {
        fx_prev(f);
        if (f->age++ >= f->maxage) f->dead = 1;
        float var1 = (float)f->age / (float)f->maxage;
        if (det_rng_float(&f->rand) > var1)
            spawn(pl, "smoke", f->e.pos_x, f->e.pos_y, f->e.pos_z, f->e.motion_x, f->e.motion_y, f->e.motion_z);
        f->e.motion_y -= 0.03;
        fx_move(f);
        f->e.motion_x *= 0.9990000128746033;
        f->e.motion_y *= 0.9990000128746033;
        f->e.motion_z *= 0.9990000128746033;
        if (f->e.on_ground)
        {
            f->e.motion_x *= 0.699999988079071;
            f->e.motion_z *= 0.699999988079071;
        }
        return;
    }

    case PLIVE_DROP:
        fx_prev(f);
        if (!f->lava)
        {
            f->red = 0.2F;
            f->green = 0.3F;
            f->blue = 1.0F;
        }
        else
        {
            f->red = 1.0F;
            f->green = 16.0F / (float)(40 - f->bob + 16);
            f->blue = 4.0F / (float)(40 - f->bob + 8);
        }
        f->e.motion_y -= (double)f->gravity;
        if (f->bob-- > 0)
        {
            f->e.motion_x *= 0.02;
            f->e.motion_y *= 0.02;
            f->e.motion_z *= 0.02;
            fx_set_tex(f, 113);
        }
        else
        {
            fx_set_tex(f, 112);
        }
        fx_move(f);
        f->e.motion_x *= 0.9800000190734863;
        f->e.motion_y *= 0.9800000190734863;
        f->e.motion_z *= 0.9800000190734863;
        if (f->maxage-- <= 0) f->dead = 1;
        if (f->e.on_ground)
        {
            if (!f->lava)
            {
                f->dead = 1;
                spawn(pl, "splash", f->e.pos_x, f->e.pos_y, f->e.pos_z, 0.0, 0.0, 0.0);
            }
            else
            {
                fx_set_tex(f, 114);
            }
            f->e.motion_x *= 0.699999988079071;
            f->e.motion_z *= 0.699999988079071;
        }
        fx_liquid_kill(f);
        return;

    case PLIVE_NOTE:
    case PLIVE_HEART:
    {
        /* EntityNoteFX.onUpdate and EntityHeartFX.onUpdate: 0.66 and 0.86
         * a tick */
        double k = f->kind == PLIVE_NOTE ? 0.6600000262260437 : 0.8600000143051147;
        fx_prev(f);
        if (f->age++ >= f->maxage) f->dead = 1;
        fx_move(f);
        if (f->e.pos_y == f->e.prev_pos_y)
        {
            f->e.motion_x *= 1.1;
            f->e.motion_z *= 1.1;
        }
        f->e.motion_x *= k;
        f->e.motion_y *= k;
        f->e.motion_z *= k;
        if (f->e.on_ground)
        {
            f->e.motion_x *= 0.699999988079071;
            f->e.motion_z *= 0.699999988079071;
        }
        return;
    }

    case PLIVE_BUBBLE:
    {
        fx_prev(f);
        f->e.motion_y += 0.002;
        fx_move(f);
        f->e.motion_x *= 0.8500000238418579;
        f->e.motion_y *= 0.8500000238418579;
        f->e.motion_z *= 0.8500000238418579;
        int id = world_get_block(f->e.world, mh_floor(f->e.pos_x), mh_floor(f->e.pos_y), mh_floor(f->e.pos_z)) & 4095;
        if (BLOCKS[id].material != BLOCKS[9].material) f->dead = 1;
        if (f->maxage-- <= 0) f->dead = 1;
        return;
    }

    case PLIVE_SUSPEND:
    {
        fx_prev(f);
        fx_move(f);
        int id = world_get_block(f->e.world, mh_floor(f->e.pos_x), mh_floor(f->e.pos_y), mh_floor(f->e.pos_z)) & 4095;
        if (BLOCKS[id].material != BLOCKS[9].material) f->dead = 1;
        if (f->maxage-- <= 0) f->dead = 1;
        return;
    }

    default:
        /* EntityFX.onUpdate: the digging and breaking particles */
        fx_prev(f);
        if (f->age++ >= f->maxage) f->dead = 1;
        f->e.motion_y -= 0.04 * (double)f->gravity;
        fx_move(f);
        f->e.motion_x *= 0.9800000190734863;
        f->e.motion_y *= 0.9800000190734863;
        f->e.motion_z *= 0.9800000190734863;
        if (f->e.on_ground)
        {
            f->e.motion_x *= 0.699999988079071;
            f->e.motion_z *= 0.699999988079071;
        }
        return;
    }
}

void particles_live_tick(struct particles_live *pl)
{
    /* updateEffects: each layer's list in order, FX added during the pass
     * included; a dead FX leaves its list at once */
    for (int layer = 0; layer < 4; ++layer)
        for (int i = 0; i < pl->n; ++i)
        {
            if (pl->fx[i].layer != layer || pl->fx[i].dead) continue;
            fx_update(pl, &pl->fx[i]);
        }

    int out = 0;
    for (int i = 0; i < pl->n; ++i)
        if (!pl->fx[i].dead) pl->fx[out++] = pl->fx[i];
    pl->n = out;
}

int particles_live_count(const struct particles_live *pl)
{
    return pl->n;
}

const struct live_fx *particles_live_get(const struct particles_live *pl, int i)
{
    return &pl->fx[i];
}

/* ------------------------------------------------------------ brightness */

/* Block.func_149710_n (useNeighborBrightness), set by Block.registerBlocks'
 * loop: a non-air block with render type 10 (stairs), a BlockSlab (the
 * double slabs too), farmland, a block whose material does not block grass
 * (MaterialLogic, MaterialTransparent, MaterialPortal), or light opacity 0. */
static int use_neighbor_brightness(int id)
{
    const struct block_def *b = &BLOCKS[id & 4095];
    int m = b->material;
    const char *c = b->class_name;

    if (m == 0) return 0;
    if (b->opacity == 0 || b->render_type == 10 || id == 60) return 1;
    if (c && (!strcmp(c, "BlockStoneSlab") || !strcmp(c, "BlockWoodSlab"))) return 1;
    return m == 5 || m == 14 || m == 17 || m == 19 || m == 20 || m == 26 || m == 31;
}

/* World.getSkyBlockTypeBrightness: the saved light, or for a block that uses
 * its neighbours' brightness the brightest of the five above and around. */
static int sky_block_type_brightness(struct particles_live *pl, int type, int x, int y, int z)
{
    struct world *w = pl->light_world ? pl->light_world : pl->world;

    if (w->dim != 0 && type == LIGHT_SKY) return 0;   /* hasNoSky: the Nether and the End */
    if (y < 0) y = 0;
    if (y >= 256) return type == LIGHT_SKY ? 15 : 0;

    if (!world_chunk_loaded(w, x >> 4, z >> 4))
        return type == LIGHT_SKY ? 15 : 0;

    int id = world_get_block(w, x, y, z) & 4095;
    if (use_neighbor_brightness(id))
    {
        static const int off[5][3] = {{0,1,0},{1,0,0},{-1,0,0},{0,0,1},{0,0,-1}};
        int best = world_get_light(w, type, x, y + 1, z);
        for (int i = 1; i < 5; ++i)
        {
            int v = world_get_light(w, type, x + off[i][0], y + off[i][1], z + off[i][2]);
            if (v > best) best = v;
        }
        return best;
    }
    return world_get_light(w, type, x, y, z);
}

/* Entity.getBrightnessForRender */
static int entity_brf(struct particles_live *pl, const struct live_fx *f)
{
    int x = mh_floor(f->e.pos_x);
    int z = mh_floor(f->e.pos_z);

    if (!world_chunk_loaded(pl->light_world ? pl->light_world : pl->world, x >> 4, z >> 4)) return 0;

    double var4 = (f->e.bounding_box.max_y - f->e.bounding_box.min_y) * 0.66;
    int y = mh_floor(f->e.pos_y - (double)f->e.y_offset + var4);

    int sky = sky_block_type_brightness(pl, LIGHT_SKY, x, y, z);
    int block = sky_block_type_brightness(pl, LIGHT_BLOCK, x, y, z);
    return sky << 20 | block << 4;
}

static int fx_brf(struct particles_live *pl, const struct live_fx *f, float pt)
{
    switch (f->kind)
    {
    case PLIVE_LARGE_EXPLODE:
        return 61680;
    case PLIVE_FLAME:
    {
        float var2 = clamp01(((float)f->age + pt) / (float)f->maxage);
        int var3 = entity_brf(pl, f);
        int var4 = var3 & 255;
        int var5 = var3 >> 16 & 255;
        var4 += (int)(var2 * 15.0F * 16.0F);
        if (var4 > 240) var4 = 240;
        return var4 | var5 << 16;
    }
    case PLIVE_PORTAL:
    case PLIVE_ENCHANT:  /* the same getBrightnessForRender */
    {
        int var2 = entity_brf(pl, f);
        float var3 = (float)f->age / (float)f->maxage;
        var3 *= var3;
        var3 *= var3;
        int var4 = var2 & 255;
        int var5 = var2 >> 16 & 255;
        var5 += (int)(var3 * 15.0F * 16.0F);
        if (var5 > 240) var5 = 240;
        return var4 | var5 << 16;
    }
    case PLIVE_LAVA:
    {
        int var3 = entity_brf(pl, f);
        return 240 | (var3 >> 16 & 255) << 16;
    }
    case PLIVE_DROP:
        return f->lava ? 257 : entity_brf(pl, f);
    default:
        return entity_brf(pl, f);
    }
}

void particles_live_light_pt(struct particles_live *pl, float pt)
{
    for (int i = 0; i < pl->n; ++i)
        pl->fx[i].brf = fx_brf(pl, &pl->fx[i], pt);
}

void particles_live_light(struct particles_live *pl)
{
    particles_live_light_pt(pl, 1.0F);
}
