/* The native side of netherite.oracle.Det; see det.h for the mapping from the
 * Java roles to the engine's one thread and for what each entry point replaces. */
#include "det.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The raw bits of a double, the way fdlibm's __HI/__LO pair reads them. */
static uint64_t det_bits(double d)
{
    uint64_t b;

    memcpy(&b, &d, sizeof b);
    return b;
}

static double det_from_bits(uint64_t b)
{
    double d;

    memcpy(&d, &b, sizeof d);
    return d;
}

void det_init(det_state *s)
{
    memset(s, 0, sizeof *s);
    s->role = DET_OTHER;
}

void det_free(det_state *s)
{
    det_split *sp = s->splits;

    while (sp)
    {
        det_split *next = sp->next;
        free(sp);
        sp = next;
    }

    s->splits = NULL;
}

void det_set_role(det_state *s, int role)
{
    s->role = role;
}

int det_role(const det_state *s)
{
    return s->role;
}

uint64_t det_mix(uint64_t s, uint64_t k)
{
    uint64_t z = s + 0x9e3779b97f4a7c15ULL * (k + 1);

    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

int64_t det_pin_seed(int64_t world_seed, int k, int64_t t)
{
    return (int64_t)det_mix(det_mix((uint64_t)world_seed, (uint64_t)(int64_t)(40 + k)), (uint64_t)t);
}

void det_pin_init(det_pin *p, int k, int64_t world_seed)
{
    p->k = k;
    p->seed = world_seed;
    p->at = 0;
    p->fresh = 1;
}

det_rng *det_pin_at(det_pin *p, int64_t t)
{
    if (p->fresh || p->at != t)
    {
        p->fresh = 0;
        p->at = t;
        det_rng_set_seed(&p->r, det_pin_seed(p->seed, p->k, t));
    }
    return &p->r;
}

float det_spawner_display_yaw(int64_t world_seed)
{
    det_rng r;
    det_rng_set_seed(&r, det_pin_seed(world_seed, DET_PIN_SPAWNER, 0));
    (void)det_rng_double(&r);   /* field_70770_ap */
    (void)det_rng_double(&r);   /* field_70769_ao */
    return (float)(det_rng_double(&r) * 3.141592653589793 * 2.0);   /* Math.PI */
}

/* Java's String.hashCode over the UTF-16 code units of an ASCII name. */
int32_t det_name_hash(const char *name)
{
    uint32_t h = 0;

    for (const unsigned char *p = (const unsigned char *)name; *p; ++p) h = 31 * h + *p;
    return (int32_t)h;
}

void det_rng_set_seed(det_rng *r, int64_t seed)
{
    jr_seed(&r->r, seed);
    r->have_next_next_gaussian = 0;
    r->next_next_gaussian = 0.0;
}

/* Puts a stream into a state read by det_rng_state, which java.util.Random's
 * setSeed cannot: setSeed scrambles its argument, (seed ^ 0x5DEECE66D) & mask,
 * so setSeed(state) is a different state. */
static void rng_load(det_rng *r, uint64_t state)
{
    r->r.seed = state & JR_MASK;
    r->have_next_next_gaussian = 0;
    r->next_next_gaussian = 0.0;
}

uint64_t det_rng_state(const det_rng *r)
{
    return r->r.seed;
}

double det_rng_gaussian(det_rng *r)
{
    if (r->have_next_next_gaussian)
    {
        r->have_next_next_gaussian = 0;
        return r->next_next_gaussian;
    }

    double v1, v2, s;

    do
    {
        v1 = 2.0 * det_rng_double(r) - 1.0;
        v2 = 2.0 * det_rng_double(r) - 1.0;
        s = v1 * v1 + v2 * v2;
    } while (s >= 1.0 || s == 0.0);

    double multiplier = det_sqrt(-2.0 * det_log(s) / s);
    r->next_next_gaussian = v2 * multiplier;
    r->have_next_next_gaussian = 1;
    return v1 * multiplier;
}

double det_sqrt(double x)
{
    return sqrt(x);
}

/* fdlibm's __ieee754_log, which is StrictMath.log; see det.h for why. */
double det_log(double x)
{
    /* The literals are the fdlibm ones; the comments are their bit patterns. */
    static const double ln2_hi = 6.93147180369123816490e-01; /* 3fe62e42 fee00000 */
    static const double ln2_lo = 1.90821492927058770002e-10; /* 3dea39ef 35793c76 */
    static const double two54 = 1.80143985094819840000e+16;  /* 43500000 00000000 */
    static const double Lg1 = 6.666666666666735130e-01;      /* 3FE55555 55555593 */
    static const double Lg2 = 3.999999999940941908e-01;      /* 3FD99999 9997FA04 */
    static const double Lg3 = 2.857142874366239149e-01;      /* 3FD24924 94229359 */
    static const double Lg4 = 2.222219843214978396e-01;      /* 3FCC71C5 1D8E78AF */
    static const double Lg5 = 1.818357216161805012e-01;      /* 3FC74664 96CB03DE */
    static const double Lg6 = 1.531383769920937332e-01;      /* 3FC39A09 D078C69F */
    static const double Lg7 = 1.479819860511658591e-01;      /* 3FC2F112 DF3E5244 */
    const double zero = 0.0;

    uint64_t xb = det_bits(x);
    int32_t hx = (int32_t)(xb >> 32);
    uint32_t lx = (uint32_t)xb;
    int k = 0;

    if (hx < 0x00100000) /* x < 2**-1022 */
    {
        if (((hx & 0x7fffffff) | (int32_t)lx) == 0) return -two54 / zero; /* log(+-0) = -inf */
        if (hx < 0) return (x - x) / zero;                                /* log(-#) = NaN */
        k -= 54;
        x *= two54; /* subnormal, scale up */
        hx = (int32_t)(det_bits(x) >> 32);
    }

    if (hx >= 0x7ff00000) return x + x;
    k += (hx >> 20) - 1023;
    hx &= 0x000fffff;

    int i = (hx + 0x95f64) & 0x100000;

    xb = (det_bits(x) & 0x00000000ffffffffULL) | ((uint64_t)(uint32_t)(hx | (i ^ 0x3ff00000)) << 32);
    x = det_from_bits(xb);
    k += (i >> 20);

    double f = x - 1.0;

    if ((0x000fffff & (2 + hx)) < 3) /* |f| < 2**-20 */
    {
        if (f == zero)
        {
            if (k == 0) return zero;
            double dk = (double)k;
            return dk * ln2_hi + dk * ln2_lo;
        }

        double R = f * f * (0.5 - 0.33333333333333333 * f);

        if (k == 0) return f - R;
        double dk = (double)k;
        return dk * ln2_hi - ((R - dk * ln2_lo) - f);
    }

    double s = f / (2.0 + f);
    double dk = (double)k;
    double z = s * s;

    i = hx - 0x6147a;
    double w = z * z;
    int j = 0x6b851 - hx;
    double t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    double t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    i |= j;
    double R = t2 + t1;

    if (i > 0)
    {
        double hfsq = 0.5 * f * f;

        if (k == 0) return f - (hfsq - s * (hfsq + R));
        return dk * ln2_hi - ((hfsq - (s * (hfsq + R) + dk * ln2_lo)) - f);
    }

    if (k == 0) return f - s * (f - R);
    return dk * ln2_hi - ((s * (f - R) - dk * ln2_lo) - f);
}

void det_reset(det_state *s, int64_t seed)
{
    s->world_seed = seed;
    for (int r = 0; r < DET_ROLES; ++r)
    {
        det_rng_set_seed(&s->seeder[r], (int64_t)det_mix((uint64_t)seed, (uint64_t)(10 + r)));
        det_rng_set_seed(&s->math[r], (int64_t)det_mix((uint64_t)seed, (uint64_t)(20 + r)));
    }

    s->next_id[DET_CLIENT] = 1 << 24;
    s->next_id[DET_SERVER] = 0;
    s->next_id[DET_OTHER] = 3 << 24;
    s->next_id[DET_RENDER] = 2 << 24;

    for (det_split *sp = s->splits; sp; sp = sp->next) det_split_reseed(s, sp);
}

int64_t det_seeder_next_long(det_state *s, int role)
{
    return det_rng_long(&s->seeder[role]);
}

det_rng det_new_random_role(det_state *s, int role)
{
    det_rng nr;

    det_rng_set_seed(&nr, det_seeder_next_long(s, role));
    return nr;
}

det_rng det_new_random(det_state *s)
{
    return det_new_random_role(s, s->role);
}

double det_math_random_role(det_state *s, int role)
{
    return det_rng_double(&s->math[role]);
}

double det_math_random(det_state *s)
{
    return det_math_random_role(s, s->role);
}

void det_uuid_role(det_state *s, int role, int64_t *msb, int64_t *lsb)
{
    uint64_t a = (uint64_t)det_seeder_next_long(s, role);
    uint64_t b = (uint64_t)det_seeder_next_long(s, role);

    a = (a & ~0xF000ULL) | 0x4000ULL;
    b = (b & 0x3FFFFFFFFFFFFFFFULL) | 0x8000000000000000ULL;
    *msb = (int64_t)a;
    *lsb = (int64_t)b;
}

void det_uuid(det_state *s, int64_t *msb, int64_t *lsb)
{
    det_uuid_role(s, s->role, msb, lsb);
}

int32_t det_next_entity_id_role(det_state *s, int role)
{
    int32_t v = s->next_id[role];

    s->next_id[role] = (int32_t)((uint32_t)v + 1);
    return v;
}

int32_t det_next_entity_id(det_state *s)
{
    return det_next_entity_id_role(s, s->role);
}

det_split *det_split_random(det_state *s, const char *name)
{
    det_split *sp = calloc(1, sizeof *sp);

    if (sp == NULL) return NULL;
    snprintf(sp->name, sizeof sp->name, "%s", name);

    det_split **tail = &s->splits;

    while (*tail) tail = &(*tail)->next;
    *tail = sp;
    det_split_reseed(s, sp);
    return sp;
}

det_split *det_split_find(const det_state *s, const char *name)
{
    for (det_split *sp = s->splits; sp; sp = sp->next)
        if (strcmp(sp->name, name) == 0) return sp;
    return NULL;
}

int det_split_index(const det_state *s, const det_split *sp)
{
    int i = 0;

    for (const det_split *p = s->splits; p; p = p->next, ++i)
        if (p == sp) return i;
    return -1;
}

void det_split_reseed(det_state *s, det_split *sp)
{
    uint64_t k = (uint64_t)(int64_t)det_name_hash(sp->name) & 0xffffffffULL;

    for (int r = 0; r < DET_ROLES; ++r)
    {
        det_rng_set_seed(&sp->d[r], (int64_t)det_mix((uint64_t)s->world_seed ^ (k << 16), (uint64_t)(30 + r)));
        sp->used[r] = 0;
    }
}

static det_rng *split_pick(det_split *sp, int role)
{
    sp->used[role] = 1;
    return &sp->d[role];
}

int32_t det_split_next_role(det_state *s, det_split *sp, int role, int bits)
{
    return det_rng_next(split_pick(sp, role), bits);
}

int32_t det_split_int_role(det_state *s, det_split *sp, int role)
{
    return det_rng_int(split_pick(sp, role));
}

int32_t det_split_int_n_role(det_state *s, det_split *sp, int role, int32_t n)
{
    return det_rng_int_n(split_pick(sp, role), n);
}

int64_t det_split_long_role(det_state *s, det_split *sp, int role)
{
    return det_rng_long(split_pick(sp, role));
}

double det_split_double_role(det_state *s, det_split *sp, int role)
{
    return det_rng_double(split_pick(sp, role));
}

float det_split_float_role(det_state *s, det_split *sp, int role)
{
    return det_rng_float(split_pick(sp, role));
}

int det_split_bool_role(det_state *s, det_split *sp, int role)
{
    return det_rng_bool(split_pick(sp, role));
}

double det_split_gaussian_role(det_state *s, det_split *sp, int role)
{
    return det_rng_gaussian(split_pick(sp, role));
}

void det_split_set_seed_role(det_state *s, det_split *sp, int role, int64_t seed)
{
    det_rng_set_seed(split_pick(sp, role), seed);
}

int32_t det_split_next(det_state *s, det_split *sp, int bits)
{
    return det_split_next_role(s, sp, s->role, bits);
}

int32_t det_split_int(det_state *s, det_split *sp)
{
    return det_split_int_role(s, sp, s->role);
}

int32_t det_split_int_n(det_state *s, det_split *sp, int32_t n)
{
    return det_split_int_n_role(s, sp, s->role, n);
}

int64_t det_split_long(det_state *s, det_split *sp)
{
    return det_split_long_role(s, sp, s->role);
}

double det_split_double(det_state *s, det_split *sp)
{
    return det_split_double_role(s, sp, s->role);
}

float det_split_float(det_state *s, det_split *sp)
{
    return det_split_float_role(s, sp, s->role);
}

int det_split_bool(det_state *s, det_split *sp)
{
    return det_split_bool_role(s, sp, s->role);
}

double det_split_gaussian(det_state *s, det_split *sp)
{
    return det_split_gaussian_role(s, sp, s->role);
}

void det_split_set_seed(det_state *s, det_split *sp, int64_t seed)
{
    det_split_set_seed_role(s, sp, s->role, seed);
}

uint64_t det_seeder_state(const det_state *s, int role)
{
    return det_rng_state(&s->seeder[role]);
}

uint64_t det_math_state(const det_state *s, int role)
{
    return det_rng_state(&s->math[role]);
}

uint64_t det_split_state(const det_state *s, int role)
{
    uint64_t h = 0;

    for (const det_split *sp = s->splits; sp; sp = sp->next)
        if (sp->used[role]) h += det_mix(det_rng_state(&sp->d[role]), (uint64_t)(int64_t)det_name_hash(sp->name));
    return h;
}

void det_load(det_state *s, int64_t world_seed, const uint64_t seeder[DET_ROLES], const uint64_t math[DET_ROLES],
              const int32_t next_id[DET_ROLES])
{
    s->world_seed = world_seed;
    for (int r = 0; r < DET_ROLES; ++r)
    {
        rng_load(&s->seeder[r], seeder[r]);
        rng_load(&s->math[r], math[r]);
        s->next_id[r] = next_id[r];
    }
}

det_split *det_split_add(det_state *s, const char *name, const uint64_t state[DET_ROLES], const uint8_t used[DET_ROLES])
{
    det_split *sp = det_split_random(s, name);

    if (sp == NULL) return NULL;
    for (int r = 0; r < DET_ROLES; ++r)
    {
        rng_load(&sp->d[r], state[r]);
        sp->used[r] = used[r];
    }
    return sp;
}
