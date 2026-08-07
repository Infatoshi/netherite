#include "game/end_city_live.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include "assets/end_city_templates.h"
#include "mc_rng.h"

static uint64_t add(uint64_t hash, int value) {
    hash ^= (uint32_t)value;
    return hash * UINT64_C(0x100000001b3);
}

static int one(long long seed, int cx, int cz) {
    if (getenv("ENDCITY_VERBOSE")) {
        JavaRandom base,probe;
        jrand_set(&base,seed);
        long long mx=jrand_long(&base),mz=jrand_long(&base);
        uint64_t mixed=(uint64_t)(int64_t)cx*(uint64_t)mx
            ^ (uint64_t)(int64_t)cz*(uint64_t)mz ^ (uint64_t)seed;
        jrand_set(&probe,(int64_t)mixed);
        int a=jrand_int_bound(&probe,2),b=jrand_int_bound(&probe,2);
        int c=jrand_int_bound(&probe,3),d=jrand_int(&probe);
        printf("R %016llx %d %d %d %d\n",(unsigned long long)mixed,
            a,b,c,d);
    }
    GmEndCity city;
    if (!gm_end_city_build(seed, cx, cz, 70, &city)) return 0;
    uint64_t hash = UINT64_C(0xcbf29ce484222325);
    for (int i = 0; i < city.count; ++i) {
        const GmEndCityPiece *p = &city.pieces[i];
        hash=add(hash,p->template_index); hash=add(hash,p->x);
        hash=add(hash,p->y); hash=add(hash,p->z);
        hash=add(hash,p->component_type);
        hash=add(hash,p->min_x); hash=add(hash,p->min_y); hash=add(hash,p->min_z);
        hash=add(hash,p->max_x); hash=add(hash,p->max_y); hash=add(hash,p->max_z);
        if (getenv("ENDCITY_VERBOSE"))
            printf("P %s %d %d %d %d %d %d %d %d %d %d\n",
                   GM_EC_TEMPLATES[p->template_index].name,
                   p->x,p->y,p->z,p->component_type,
                   p->min_x,p->min_y,p->min_z,p->max_x,p->max_y,p->max_z);
    }
    printf("%lld %d %d %d %d %016llx\n", seed, cx, cz, city.count,
           city.ship_created, (unsigned long long)hash);
    return 1;
}

int main(void) {
    if (!one(0,2,3) || !one(1,-4,7)
            || !one(123456789,60,-41) || !one(-99887766,-90,-72))
        return 1;
    return 0;
}
