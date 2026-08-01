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

    if (failures) {
        fprintf(stderr, "%d particle test(s) failed\n", failures);
        return 1;
    }
    printf("particles_live: PASS\n");
    return 0;
}
