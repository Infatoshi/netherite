#include "game/particles_live.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int failures;

#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s\n", msg); failures++; } \
} while (0)

static void check_close(const char *name, double got, double want) {
    if (fabs(got - want) > 1e-12) {
        fprintf(stderr, "FAIL: %s got %.17g want %.17g\n", name, got, want);
        failures++;
    }
}

int main(void) {
    GmParticlesLive live;
    gm_particles_live_init(&live, UINT64_C(0x123456789abcdef0));
    int spawned = gm_particles_live_spawn_destroy(&live, 4, 70, -3, 1,
                                                   1.0f, 1.0f, 1.0f);
    CHECK(spawned == 64, "one destroy burst spawns 64 particles");
    CHECK(gm_particles_live_count(&live) == 64, "pool count is 64");

    GmLiveParticle *p = &live.particles[0];
    double x = p->x, y = p->y, z = p->z;
    double mx = p->motion_x, my = p->motion_y, mz = p->motion_z;
    for (int tick = 0; tick < 3; ++tick) {
        my -= 0.04 * (double)1.0f;
        x += mx; y += my; z += mz;
        mx *= 0.9800000190734863;
        my *= 0.9800000190734863;
        mz *= 0.9800000190734863;
        gm_particles_live_tick(&live, NULL, 0, 0);
        check_close("position x", p->x, x);
        check_close("position y", p->y, y);
        check_close("position z", p->z, z);
        check_close("motion x", p->motion_x, mx);
        check_close("motion y", p->motion_y, my);
        check_close("motion z", p->motion_z, mz);
    }

    gm_particles_live_init(&live, UINT64_C(7));
    gm_particles_live_spawn_destroy(&live, 0, 64, 0, 1, 1.0f, 1.0f, 1.0f);
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i) {
        if (!live.particles[i].active) continue;
        CHECK(live.particles[i].max_age >= 4, "particle lifetime lower bound");
        CHECK(live.particles[i].max_age <= 40, "particle lifetime upper bound");
    }
    for (int tick = 0; tick < 41; ++tick)
        gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(gm_particles_live_count(&live) == 0,
          "all particles expire by the vanilla maximum age check");

    struct GuardedPool {
        uint64_t before;
        GmParticlesLive pool;
        uint64_t after;
    } guarded;
    memset(&guarded, 0, sizeof guarded);
    guarded.before = UINT64_C(0x1122334455667788);
    guarded.after = UINT64_C(0x8877665544332211);
    gm_particles_live_init(&guarded.pool, UINT64_C(9));
    int total = 0;
    for (int burst = 0; burst < 17; ++burst)
        total += gm_particles_live_spawn_destroy(&guarded.pool,
            burst, 64, 0, 1, 1.0f, 1.0f, 1.0f);
    CHECK(total == GM_PARTICLES_LIVE_CAP, "spawns stop at fixed pool capacity");
    CHECK(gm_particles_live_count(&guarded.pool) == GM_PARTICLES_LIVE_CAP,
          "pool count never exceeds capacity");
    CHECK(guarded.before == UINT64_C(0x1122334455667788),
          "pool does not underflow storage");
    CHECK(guarded.after == UINT64_C(0x8877665544332211),
          "pool does not overflow storage");

    if (failures) {
        fprintf(stderr, "%d particle test(s) failed\n", failures);
        return 1;
    }
    printf("particles_live: PASS\n");
    return 0;
}
