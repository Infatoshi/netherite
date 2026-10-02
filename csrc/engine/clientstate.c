/* The client-side view state of work item C5; see clientstate.h. Each
 * function is the vanilla method its comment names, in its own float order. */
#include "clientstate.h"

#include <math.h>
#include <string.h>

/* ------------------------------------------------------------------ FOV */

float clientstate_fov_multiplier(const struct cs_fov_in *in)
{
    float v1 = 1.0F;

    if (in->flying) v1 *= 1.1F;

    v1 = (float)((double)v1 * ((in->move_speed / (double)in->walk_speed + 1.0) / 2.0));

    if (in->walk_speed == 0.0F || isnan(v1) || isinf(v1)) v1 = 1.0F;

    if (in->bow)
    {
        float v4 = (float)in->use_duration / 20.0F;

        if (v4 > 1.0F) v4 = 1.0F;
        else v4 *= v4;

        v1 *= 1.0F - v4 * 0.15F;
    }

    return v1;
}

void clientstate_fov_hand(float *fmh, float *fmhp, float mult)
{
    *fmhp = *fmh;
    *fmh += (mult - *fmh) * 0.5F;

    if (*fmh > 1.5F) *fmh = 1.5F;
    if (*fmh < 0.1F) *fmh = 0.1F;
}

/* ModifiableAttributeInstance.computeValue over movementSpeed's clamp
 * (RangedAttribute 0 .. Double.MAX_VALUE). */
double clientstate_attr_value(const struct cs_attr *a)
{
    double v = a->base;

    for (int i = 0; i < a->n; ++i)
        if (a->m[i].op == 0) v += a->m[i].amount;

    double r = v;

    for (int i = 0; i < a->n; ++i)
        if (a->m[i].op == 1) r += v * a->m[i].amount;

    for (int i = 0; i < a->n; ++i)
        if (a->m[i].op == 2) r *= 1.0 + a->m[i].amount;

    if (r < 0.0) r = 0.0;
    return r;
}

void clientstate_attr_apply(struct cs_attr *a, const struct cs_attr_mod *m)
{
    for (int i = 0; i < a->n; ++i)
        if (a->m[i].msb == m->msb && a->m[i].lsb == m->lsb) return;
    if (a->n < CS_ATTR_MODS) a->m[a->n++] = *m;
}

void clientstate_attr_remove(struct cs_attr *a, int64_t msb, int64_t lsb)
{
    int k = 0;

    for (int i = 0; i < a->n; ++i)
        if (!(a->m[i].msb == msb && a->m[i].lsb == lsb)) a->m[k++] = a->m[i];
    a->n = k;
}

/* EntityLivingBase.sprintingSpeedBoostModifierUUID */
#define SPRINT_MSB ((int64_t)0x662a6b8dda3e4c1cULL)
#define SPRINT_LSB ((int64_t)0x881396ea6097278dULL)

void clientstate_attr_sprint(struct cs_attr *a, int sprinting)
{
    clientstate_attr_remove(a, SPRINT_MSB, SPRINT_LSB);

    if (sprinting)
    {
        struct cs_attr_mod m = {SPRINT_MSB, SPRINT_LSB, 2, 0.30000001192092896};
        clientstate_attr_apply(a, &m);
    }
}

int clientstate_potion_speed_mod(int id, int amplifier, struct cs_attr_mod *out)
{
    /* Potion.func_111183_a: the registered amount times amplifier + 1 */
    if (id == 1)
    {
        struct cs_attr_mod m = {(int64_t)0x91aeaa56376b4498ULL, (int64_t)0x935b2f7f68070635ULL, 2,
                                0.20000000298023224 * (double)(amplifier + 1)};
        *out = m;
        return 1;
    }

    if (id == 2)
    {
        struct cs_attr_mod m = {(int64_t)0x7107de5e7ce84030ULL, (int64_t)0x940e514c1f160890ULL, 2,
                                -0.15000000596046448 * (double)(amplifier + 1)};
        *out = m;
        return 1;
    }

    return 0;
}

/* --------------------------------------------------------------- portal */

void clientstate_set_in_portal(struct cs_portal *p)
{
    if (p->time_until > 0) p->time_until = 10;
    else p->in_portal = 1;
}

int clientstate_portal_tick(struct cs_portal *p, int confusion, jrand *rand)
{
    int draws = 0;

    p->prev = p->time;

    if (p->in_portal)
    {
        if (p->time == 0.0F)
        {
            if (rand) (void)jr_float(rand);
            draws = 1;
        }

        p->time += 0.0125F;

        if (p->time >= 1.0F) p->time = 1.0F;

        p->in_portal = 0;
    }
    else if (confusion > 60)
    {
        p->time += 0.006666667F;

        if (p->time > 1.0F) p->time = 1.0F;
    }
    else
    {
        if (p->time > 0.0F) p->time -= 0.05F;
        if (p->time < 0.0F) p->time = 0.0F;
    }

    if (p->time_until > 0) --p->time_until;

    return draws;
}

/* ----------------------------------------------------------------- hurt */

int clientstate_health_update(struct cs_hurt *h, int status, jrand *rand)
{
    if (status == 2)
    {
        h->limb = 1.5F;
        h->res = h->max_res;
        h->hurt = h->max_hurt = 10;
        h->attacked_yaw = 0.0F;
    }
    else if (status == 3)
    {
        h->health = 0.0F;
    }
    else
    {
        return 0;
    }

    if (rand)
    {
        (void)jr_float(rand);
        (void)jr_float(rand);
    }

    return 2;
}

void clientstate_hurt_animation(struct cs_hurt *h)
{
    h->hurt = h->max_hurt = 10;
    h->attacked_yaw = 0.0F;
}

/* EntityPlayer.damageEntity(DamageSource.generic, amount) on the client */
static void damage_entity(struct cs_hurt *h, float amount, struct cs_damage_ctx *ctx)
{
    struct cs_damage_ctx none = {0, 0, -1, 0.0F, 0, 20.0F};

    if (!ctx) ctx = &none;

    /* generic is blockable, not absolute */
    if (ctx->blocking && amount > 0.0F) amount = (1.0F + amount) * 0.5F;

    /* applyArmorCalculations */
    {
        int v3 = 25 - ctx->armor;
        float v4 = amount * (float)v3;
        amount = v4 / 25.0F;
    }

    /* applyPotionDamageCalculations */
    if (ctx->resistance >= 0)
    {
        int v3 = (ctx->resistance + 1) * 5;
        int v4 = 25 - v3;
        float v5 = amount * (float)v4;
        amount = v5 / 25.0F;
    }

    if (amount <= 0.0F)
    {
        amount = 0.0F;
    }
    else
    {
        int v3 = ctx->ench_mod > 20 ? 20 : ctx->ench_mod;

        if (v3 > 0)
        {
            int v4 = 25 - v3;
            float v5 = amount * (float)v4;
            amount = v5 / 25.0F;
        }
    }

    float v3 = amount;
    amount = fmaxf(amount - ctx->absorption, 0.0F);
    float abs = ctx->absorption - (v3 - amount);
    ctx->absorption = abs < 0.0F ? 0.0F : abs;

    if (amount != 0.0F)
    {
        /* setHealth clamps to 0 .. getMaxHealth() */
        float hp = h->health - amount;
        if (hp < 0.0F) hp = 0.0F;
        if (hp > ctx->max_health) hp = ctx->max_health;
        h->health = hp;
    }
}

void clientstate_set_sp_health(struct cs_hurt *h, float health, int *has_set, struct cs_damage_ctx *ctx)
{
    if (has_set && !*has_set)
    {
        h->health = health;
        *has_set = 1;
        return;
    }

    float v2 = h->health - health;

    if (v2 <= 0.0F)
    {
        h->health = health;

        if (v2 < 0.0F) h->res = h->max_res / 2;
    }
    else
    {
        h->last_damage = v2;
        h->res = h->max_res;
        damage_entity(h, v2, ctx);
        h->hurt = h->max_hurt = 10;
    }
}

void clientstate_hurt_tick(struct cs_hurt *h)
{
    if (h->hurt > 0) --h->hurt;
    if (h->res > 0) --h->res;
}

void clientstate_limb_step(struct cs_hurt *h, double dx, double dz)
{
    float v7 = (float)sqrt(dx * dx + dz * dz) * 4.0F;

    if (v7 > 1.0F) v7 = 1.0F;

    h->limb += (v7 - h->limb) * 0.4F;
}

/* ------------------------------------------------------------ lightning */

void clientstate_bolt_spawn(struct cs_weather *w, int id, det_state *det)
{
    if (w->n >= CS_BOLTS) return;

    struct cs_bolt *b = &w->b[w->n++];
    memset(b, 0, sizeof *b);
    /* Entity(World): nextEntityId, Det.newRandom, Det.uuid; the id is then
     * the packet's */
    (void)det_next_entity_id_role(det, DET_CLIENT);
    det_rng r = det_new_random_role(det, DET_CLIENT);
    int64_t msb, lsb;
    det_uuid_role(det, DET_CLIENT, &msb, &lsb);
    b->rand = r.r;
    b->id = id;
    b->state = 2;
    b->vertex = jr_long(&b->rand);
    b->living = jr_int_n(&b->rand, 3) + 1;
}

/* EntityLightningBolt.onUpdate on the client world */
static void bolt_update(struct cs_weather *w, struct cs_bolt *b, int *dead)
{
    if (b->state == 2)
    {
        /* the thunder and explode sounds' pitches */
        (void)jr_float(&b->rand);
        (void)jr_float(&b->rand);
    }

    --b->state;

    if (b->state < 0)
    {
        if (b->living == 0)
        {
            *dead = 1;
        }
        else if (b->state < -jr_int_n(&b->rand, 10))
        {
            --b->living;
            b->state = 1;
            b->vertex = jr_long(&b->rand);
        }
    }

    if (b->state >= 0) w->last_bolt = 2;
}

void clientstate_weather_tick(struct cs_weather *w)
{
    if (w->last_bolt > 0) --w->last_bolt;

    for (int i = 0; i < w->n; ++i)
    {
        struct cs_bolt *b = &w->b[i];
        int dead = 0;

        ++b->age;
        bolt_update(w, b, &dead);

        if (dead)
        {
            memmove(&w->b[i], &w->b[i + 1], (size_t)(w->n - i - 1) * sizeof w->b[0]);
            --w->n;
            --i;
        }
    }
}
