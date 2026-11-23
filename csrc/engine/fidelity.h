/* The native sim's fidelity: what of vanilla 1.7.10 a run leaves out for
 * speed (the config's fidelity section, worldconf.h; native only: the
 * oracle runs vanilla and refuses the section, so a run at anything but
 * vanilla has no recording to check against and the gates refuse it).
 * The zero struct is vanilla. A start takes it once, after its load
 * (fidelity_apply), and every env copied from the start keeps it: each
 * world's tick holds it (servertick.h fid, read by the spawner too) and
 * each dimension's living list (living.h an_world fid_keep, which sets a
 * living of a kind not kept dead as it joins). The daylight cycle is
 * world.daylight_cycle, a vanilla game rule the oracle records, so it is
 * not here. */
#ifndef NETHERITE_FIDELITY_H
#define NETHERITE_FIDELITY_H

#include <stddef.h>
#include <stdint.h>

struct wconf;
struct serverreplay;

/* servertick.h struct servertick fid's bits */
enum {
    FID_NO_RANDOM_TICKS = 1,   /* func_147456_g's random block ticks (crops, grass, leaves, fire, ice melt) */
    FID_NO_WEATHER = 2,        /* updateWeather, the chunk loop's lightning, ice and snow: the start's weather stays */
    FID_SKIP_AMBIENT = 4,      /* opt-in training: skip zero-gameplay ambient mobs (bats, squids) during spawn & tick */
    FID_DISTANT_TICKS = 8,     /* opt-in training: skip distant chunk ticks & sparsely tick distant non-boss mobs */
};

struct fidelity {
    int no_spawning;           /* SpawnerAnimals.findChunksForSpawning off (the doMobSpawning rule false,
                                * which also stops BlockPortal's pigmen) */
    unsigned skip_types;       /* the spawner's categories left out: 1 << CT_MONSTER, CT_CREATURE, CT_AMBIENT, CT_WATER */
    int keep_on;               /* mobs is not all: only the kinds in keep live */
    uint32_t keep;             /* 1 << living kind (living.h AK_, VK_, SK_, HK_, GK_) */
    int bits;                  /* FID_NO_RANDOM_TICKS | FID_NO_WEATHER | FID_SKIP_AMBIENT | FID_DISTANT_TICKS */
    int distant_tick_radius;   /* blocks from player beyond which chunk ticks skip & non-boss mobs tick 1/4 (0: off) */
};

/* the config's fidelity keys; 0, or -1 with err (an unknown mob kind) */
int fidelity_from_conf(struct fidelity *f, const struct wconf *c, char *err, size_t n);
/* 1 when f is vanilla */
int fidelity_vanilla(const struct fidelity *f);
/* "vanilla", or the keys that are not, "spawning=off,random_ticks=off" */
void fidelity_describe(const struct fidelity *f, char *out, size_t n);
/* f on a loaded start: the spawner's rule, and every living whose kind is
 * not kept set dead (World.updateEntities removes it on the first tick, the
 * tracker tells the client). */
void fidelity_apply(const struct fidelity *f, struct serverreplay *sr);

#endif
