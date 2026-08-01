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

static u8 pack_tint(float base, float lm) {
    float v = base;
    float l = lm;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    if (l < 0.0f) l = 0.0f;
    if (l > 1.0f) l = 1.0f;
    return (u8)(0.6f * 255.0f * v * l + 0.5f);
}

int main(void) {
    GmParticlesLive live;
    gm_particles_live_init(&live, UINT64_C(0x123456789abcdef0));
    int spawned = gm_particles_live_spawn_destroy(&live, 4, 70, -3, 1,
                                                   1.0f, 1.0f, 1.0f,
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
    gm_particles_live_spawn_destroy(&live, 0, 64, 0, 1,
                                    1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
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
            burst, 64, 0, 1, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f);
    CHECK(total == GM_PARTICLES_LIVE_CAP, "spawns stop at fixed pool capacity");
    CHECK(gm_particles_live_count(&guarded.pool) == GM_PARTICLES_LIVE_CAP,
          "pool count never exceeds capacity");
    CHECK(guarded.before == UINT64_C(0x1122334455667788),
          "pool does not underflow storage");
    CHECK(guarded.after == UINT64_C(0x8877665544332211),
          "pool does not overflow storage");

    /* Tint threading: base multiplier scales the 0.6*lightmap vertex color. */
    gm_particles_live_init(&live, UINT64_C(0x71c7));
    gm_particles_live_spawn_destroy(&live, 1, 70, 1, 1,
                                    1.0f, 1.0f, 1.0f,
                                    0.5f, 0.7f, 0.9f);
    {
        CrVertex verts[6];
        int n = gm_particles_live_emit(&live, 0.0f, 0.0f, 0.0f, verts, 6);
        CHECK(n == 6, "emit writes one billboard (6 verts)");
        u8 er = pack_tint(0.5f, 1.0f);
        u8 eg = pack_tint(0.7f, 1.0f);
        u8 eb = pack_tint(0.9f, 1.0f);
        CHECK(verts[0].tint.r == er, "vertex tint R scales by base 0.5");
        CHECK(verts[0].tint.g == eg, "vertex tint G scales by base 0.7");
        CHECK(verts[0].tint.b == eb, "vertex tint B scales by base 0.9");
        CHECK(verts[0].tint.a == 255, "vertex tint A is opaque");
        for (int i = 1; i < n; ++i) {
            CHECK(verts[i].tint.r == er && verts[i].tint.g == eg &&
                  verts[i].tint.b == eb,
                  "all billboard verts share the same tint");
        }
    }

    /* White base + full lightmap stays the pre-tint 0.6 gray (byte path). */
    gm_particles_live_init(&live, UINT64_C(0x71c8));
    gm_particles_live_spawn_destroy(&live, 2, 70, 2, 1,
                                    1.0f, 1.0f, 1.0f,
                                    1.0f, 1.0f, 1.0f);
    {
        CrVertex verts[6];
        int n = gm_particles_live_emit(&live, 0.0f, 0.0f, 0.0f, verts, 6);
        CHECK(n == 6, "white-base emit writes 6 verts");
        u8 gray = pack_tint(1.0f, 1.0f);
        CHECK(gray == 153, "0.6*255 packs to 153");
        CHECK(verts[0].tint.r == gray && verts[0].tint.g == gray &&
              verts[0].tint.b == gray,
              "white base yields legacy 0.6 gray tint");
    }

    /* Recorder ids 0..2 preserve the three vanilla explosion constructors. */
    gm_particles_live_init(&live, UINT64_C(0x70636c));
    CHECK(gm_particles_live_spawn_recorded(&live, 0,
          1.0, 2.0, 3.0, 0.125, 0.25, 0.5, 15, 7) == 1,
          "recorded normal explosion spawns");
    CHECK(gm_particles_live_spawn_recorded(&live, 1,
          4.0, 5.0, 6.0, 0.5, 99.0, 99.0, 15, 15) == 1,
          "recorded large explosion spawns");
    CHECK(gm_particles_live_spawn_recorded(&live, 2,
          7.0, 8.0, 9.0, 0.0, 0.0, 0.0, 0, 0) == 1,
          "recorded huge explosion spawns");
    CHECK(gm_particles_live_spawn_recorded(&live, 3,
          0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 0) == 0,
          "recorded constructor rejects non-whitelisted ids");
    CHECK(gm_particles_live_count(&live) == 3,
          "recorded pool contains all three explosion types");
    CHECK(gm_particles_live_suppresses_explosion(&live),
          "recorded explosion suppresses RNG reconstruction");
    {
        GmLiveParticle *normal = &live.particles[0];
        GmLiveParticle *large = &live.particles[1];
        GmLiveParticle *huge = &live.particles[2];
        CHECK(normal->kind == GM_LIVE_PARTICLE_EXPLOSION_NORMAL,
              "id 0 maps to normal explosion");
        CHECK(normal->max_age >= 18 && normal->max_age <= 82,
              "normal explosion uses vanilla lifetime range");
        CHECK(normal->scale >= 1.0f && normal->scale <= 7.0f,
              "normal explosion uses vanilla scale range");
        check_close("normal velocity x", normal->motion_x, 0.125);
        check_close("normal velocity y", normal->motion_y, 0.25);
        check_close("normal velocity z", normal->motion_z, 0.5);
        CHECK(large->kind == GM_LIVE_PARTICLE_EXPLOSION_LARGE,
              "id 1 maps to large explosion");
        CHECK(large->max_age >= 6 && large->max_age <= 9,
              "large explosion uses vanilla lifetime range");
        check_close("large scale from speed x", large->scale, 0.75);
        CHECK(huge->kind == GM_LIVE_PARTICLE_EXPLOSION_HUGE,
              "id 2 maps to huge explosion");
        CHECK(huge->max_age == 8, "huge explosion uses vanilla lifetime");
    }
    {
        CrVertex verts[12];
        CHECK(gm_particles_live_emit_recorded(
                  &live, 0, 0.0f, 0.0f, 0.0f, verts, 12) == 6,
              "normal explosion renders one layer-0 billboard");
        CHECK(gm_particles_live_emit_recorded(
                  &live, 3, 0.0f, 0.0f, 0.0f, verts, 12) == 6,
              "large explosion renders one layer-3 billboard");
    }
    gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(live.particles[0].age == 0 && live.particles[1].age == 0 &&
          live.particles[2].age == 0,
          "recorded particles render at constructor age on spawn tick");
    gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(live.particles[0].age == 1 && live.particles[1].age == 1 &&
          live.particles[2].age == 1,
          "recorded particles first update on following tick");

    /* Recorded particles remain alive for suppression after render certainty. */
    gm_particles_live_init(&live, UINT64_C(0x70636d));
    gm_particles_live_spawn_recorded(&live, 0,
        0.0, 64.0, 0.0, 0.0, 0.0, 0.0, 15, 15);
    gm_particles_live_tick(&live, NULL, 0, 0); /* newborn */
    for (int tick = 0; tick < 3; ++tick)
        gm_particles_live_tick(&live, NULL, 0, 0);
    {
        CrVertex verts[6];
        CHECK(gm_particles_live_count(&live) == 1,
              "normal explosion remains alive past bounded render age");
        CHECK(gm_particles_live_emit_recorded(
                  &live, 0, 0.0f, 0.0f, 0.0f, verts, 6) == 0,
              "normal explosion stops drawing after bounded render age");
        CHECK(gm_particles_live_suppresses_explosion(&live),
              "live recorded normal still suppresses RNG reconstruction");
    }
    gm_particles_live_init(&live, UINT64_C(0x70636e));
    gm_particles_live_spawn_recorded(&live, 1,
        0.0, 64.0, 0.0, 0.5, 0.0, 0.0, 15, 15);
    live.particles[0].age = 6;
    live.particles[0].max_age = 9;
    {
        CrVertex verts[6];
        CHECK(gm_particles_live_emit_recorded(
                  &live, 3, 0.0f, 0.0f, 0.0f, verts, 6) == 0,
              "large explosion stops drawing after guaranteed lifetime");
        CHECK(gm_particles_live_suppresses_explosion(&live),
              "uncertain large lifetime still suppresses RNG reconstruction");
    }

    if (failures) {
        fprintf(stderr, "%d particle test(s) failed\n", failures);
        return 1;
    }
    printf("particles_live: PASS\n");
    return 0;
}
