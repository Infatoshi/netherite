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

    /* V1 rows carry Java's constructor results and never use the pool RNG. */
    gm_particles_live_init(&live, UINT64_C(0x70636f));
    CHECK(gm_particles_live_spawn_recorded_state(
              &live, 0, 10.0, 20.0, 30.0, 10.25, 20.5, 30.75,
              -0.03125, 0.0625, 0.125, 7, 29, 1, 4.25f,
              0.8125f, 0.8125f, 0.8125f, 0, 0,
              13, 6) == 1,
          "exact recorded normal constructor state spawns");
    CHECK(gm_particles_live_spawn_recorded_state(
              &live, 1, 40.0, 50.0, 60.0, 40.0, 50.0, 60.0,
              0.0, 0.0, 0.0, 6, 9, 0, 0.625f,
              0.4375f, 0.4375f, 0.4375f, 0, 0,
              0, 0) == 1,
          "exact recorded large private state spawns");
    CHECK(live.particles[0].recorded_exact == 1 &&
          live.particles[0].age == 7 && live.particles[0].max_age == 29 &&
          live.particles[0].scale == 4.25f &&
          live.particles[0].gray == 0.8125f &&
          live.particles[0].on_ground == 1,
          "normal constructor values survive without reconstruction");
    check_close("exact normal previous x", live.particles[0].prev_x, 10.0);
    check_close("exact normal position x", live.particles[0].x, 10.25);
    check_close("exact normal motion x", live.particles[0].motion_x, -0.03125);
    CHECK(live.particles[1].recorded_exact == 1 &&
          live.particles[1].age == 6 && live.particles[1].max_age == 9 &&
          live.particles[1].scale == 0.625f &&
          live.particles[1].gray == 0.4375f,
          "large private life and size survive without reconstruction");
    {
        CrVertex verts[6];
        CHECK(gm_particles_live_emit_recorded(
                  &live, 0, 1.0f, 0.0f, 0.0f, verts, 6) == 6,
              "exact normal renders beyond legacy bounded age");
        CHECK(gm_particles_live_emit_recorded(
                  &live, 3, 1.0f, 0.0f, 0.0f, verts, 6) == 6,
              "exact large renders beyond legacy bounded age");
    }
    CHECK(gm_particles_live_spawn_recorded_state(
              &live, 0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
              0.0, 0.0, 0.0, -1, 10, 0, 1.0f,
              1.0f, 1.0f, 1.0f, 0, 0, 0, 0) == 0,
          "exact constructor state rejects negative age");
    CHECK(gm_particles_live_spawn_recorded_state(
              &live, 15, 2.0, 3.0, 4.0, 2.0, 3.0, 4.0,
              0.01, 0.02, -0.03, 0, 17, 0, 1.125f,
              0.25f, 0.5f, 0.75f, 0, 128, 15, 4) == 1,
          "exact SPELL_MOB constructor state spawns");
    CHECK(live.particles[2].kind == GM_LIVE_PARTICLE_SPELL_MOB &&
          live.particles[2].texture_index == 0 &&
          live.particles[2].texture_base == 128 &&
          live.particles[2].color_r == 0.25f &&
          live.particles[2].color_g == 0.5f &&
          live.particles[2].color_b == 0.75f,
          "SPELL_MOB color and private texture base are exact");
    CHECK(gm_particles_live_spawn_recorded_state(
              &live, 11, 3.0, 4.0, 5.0, 3.0, 4.0, 5.0,
              0.01, 0.02, -0.03, 0, 17, 0, 1.25f,
              0.2f, 0.2f, 0.2f, 0, 0, 14, 5) == 1,
          "exact SMOKE_NORMAL constructor state spawns");
    CHECK(gm_particles_live_spawn_recorded_state(
              &live, 34, 5.0, 6.0, 7.0, 5.0, 6.0, 7.0,
              0.0, 0.1, 0.0, 0, 16, 0, 2.5f,
              1.0f, 1.0f, 1.0f, 80, 0, 15, 0) == 1,
          "exact HEART constructor state spawns");
    CHECK(live.particles[3].kind == GM_LIVE_PARTICLE_SMOKE_NORMAL &&
          live.particles[3].original_scale == 1.25f &&
          live.particles[4].kind == GM_LIVE_PARTICLE_HEART &&
          live.particles[4].original_scale == 2.5f &&
          live.particles[4].texture_index == 80,
          "recorded horse particles preserve render class and original scale");

    /* ParticleSpit is ParticleExplosion plus its post-drag -0.024 Y step. */
    gm_particles_live_init(&live, UINT64_C(0x73706974));
    CHECK(gm_particles_live_spawn_recorded_state(
              &live, 48, 1.0, 2.0, 3.0, 1.0, 2.0, 3.0,
              0.1, 0.2, 0.3, 0, 20, 0, 2.0f,
              0.8f, 0.8f, 0.8f, 0, 0, 15, 6) == 1
              && live.particles[0].kind == GM_LIVE_PARTICLE_SPIT,
          "exact SPIT constructor state maps to ParticleSpit");
    gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(live.particles[0].age == 0,
          "SPIT stays at constructor age on its spawn tick");
    gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(live.particles[0].age == 1,
          "SPIT advances through ParticleExplosion on the next tick");
    check_close("SPIT move Y", live.particles[0].y, 2.204);
    check_close("SPIT post-drag gravity Y", live.particles[0].motion_y,
                0.204 * 0.8999999761581421 - 0.024);
    {
        CrVertex verts[6];
        CHECK(gm_particles_live_emit_recorded(
                  &live, 0, 1.0f, 0.0f, 0.0f, verts, 6) == 6,
              "SPIT renders through the ParticleExplosion atlas path");
    }

    /* Live llama packet arguments still receive the client constructor's
     * bounded private motion, color, scale, and lifetime entropy. */
    gm_particles_live_init(&live, UINT64_C(0x737069746c697665));
    CHECK(gm_particles_live_spawn_spit(
              &live, 1.0, 2.0, 3.0, 0.1, 0.2, 0.3, 13, 7) == 1,
          "live SPIT constructor accepts one packet particle");
    CHECK(live.particles[0].active && live.particles[0].newborn &&
              live.particles[0].kind == GM_LIVE_PARTICLE_SPIT,
          "live SPIT enters the visual pool at constructor age");
    CHECK(live.particles[0].motion_x >= 0.05 &&
              live.particles[0].motion_x <= 0.15 &&
              live.particles[0].motion_y >= 0.15 &&
              live.particles[0].motion_y <= 0.25 &&
              live.particles[0].motion_z >= 0.25 &&
              live.particles[0].motion_z <= 0.35,
          "live SPIT adds only ParticleExplosion's +/-0.05 motion entropy");
    CHECK(live.particles[0].gray >= 0.7f &&
              live.particles[0].gray <= 1.0f &&
              live.particles[0].scale >= 1.0f &&
              live.particles[0].scale <= 7.0f &&
              live.particles[0].max_age >= 18 &&
              live.particles[0].max_age <= 82,
          "live SPIT constructor attributes stay in vanilla bounds");
    {
        CrVertex verts[6];
        CHECK(gm_particles_live_emit_recorded(
                  &live, 0, 1.0f, 0.0f, 0.0f, verts, 6) == 6 &&
                  verts[0].light == 13.0f && verts[0].blk == 7.0f,
              "live SPIT renders on layer zero with packet-position light");
    }
    gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(!live.particles[0].newborn && live.particles[0].age == 0,
          "live SPIT does not double-update on its packet tick");

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

    /* Player resetHeight's bubble/splash calls become layer-0 live particles. */
    gm_particles_live_init(&live, UINT64_C(0x77617465726678));
    CHECK(gm_particles_live_spawn_water(&live, 4,
          8.25, 65.0, 8.75, 0.25, -0.5, 0.75, 14, 3) == 1,
          "water bubble constructor accepts vanilla id 4");
    CHECK(gm_particles_live_spawn_water(&live, 5,
          8.75, 65.0, 8.25, 0.25, 0.0, 0.75, 14, 3) == 1,
          "water splash constructor accepts vanilla id 5");
    CHECK(gm_particles_live_spawn_water(&live, 6,
          0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 0) == 0,
          "water constructor rejects unrelated particle ids");
    CHECK(gm_particles_live_count(&live) == 2,
          "water pool retains both resetHeight particles");
    CHECK(!gm_particles_live_suppresses_explosion(&live),
          "water particles do not suppress independent explosion rendering");
    {
        GmLiveParticle *bubble = &live.particles[0];
        GmLiveParticle *splash = &live.particles[1];
        CHECK(bubble->kind == GM_LIVE_PARTICLE_WATER_BUBBLE
              && bubble->texture_index == 32,
              "bubble uses ParticleBubble texture cell 32");
        CHECK(splash->kind == GM_LIVE_PARTICLE_WATER_SPLASH
              && splash->texture_index >= 20 && splash->texture_index <= 23,
              "splash uses ParticleSplash texture cells 20 through 23");
        check_close("splash override motion x", splash->motion_x, 0.25);
        check_close("splash override motion y", splash->motion_y, 0.1);
        check_close("splash override motion z", splash->motion_z, 0.75);
        CrVertex verts[12];
        int n = gm_particles_live_emit_water(
            &live, 0.0f, 0.0f, 0.0f, verts, 12);
        CHECK(n == 12, "water emit writes both particles.png billboards");
        CHECK(verts[0].tint.r == 255 && verts[0].tint.g == 255
              && verts[0].tint.b == 255 && verts[0].tint.a == 255,
              "water particles render with vanilla white color");
        CHECK(verts[0].light == 14.0f && verts[0].blk == 3.0f,
              "water particle light coordinates survive constructor path");
        gm_particles_live_tick(&live, NULL, 0, 0);
        CHECK(bubble->age == 0 && splash->age == 0,
              "water particles stay at constructor age on spawn tick");
        double splash_y = splash->y;
        gm_particles_live_tick(&live, NULL, 0, 0);
        CHECK(bubble->age == 1 && splash->age == 1,
              "water particles first update on the following tick");
        check_close("splash gravity/move y", splash->y,
                    splash_y + 0.1 - (double)0.04f);
    }

    /* Lingering clouds feed exact SPELL_MOB arguments into ParticleSpell's
     * deterministic visual constructor and translucent layer-0 renderer. */
    gm_particles_live_init(&live, UINT64_C(0x7370656c6c6d6f62));
    CHECK(gm_particles_live_spawn_spell(&live, 15,
          8.25, 65.0, 8.75, 0.25, 0.5, 0.75, 12, 6) == 1,
          "spell-mob constructor accepts vanilla id 15");
    CHECK(gm_particles_live_spawn_spell(&live, 20,
          0.0, 0.0, 0.0, 1.0, 1.0, 1.0, 0, 0) == 0,
          "spell-mob constructor rejects unrelated particle ids");
    {
        GmLiveParticle *spell = &live.particles[0];
        CHECK(spell->kind == GM_LIVE_PARTICLE_SPELL_MOB
                  && spell->texture_index == 128,
              "spell-mob starts in ParticleSpell texture strip");
        CHECK(spell->max_age >= 8 && spell->max_age <= 40,
              "spell-mob uses vanilla lifetime bounds");
        CHECK(spell->scale >= 0.75f && spell->scale <= 1.5f,
              "spell-mob applies the vanilla three-quarter scale");
        CHECK(spell->color_r == 0.25f && spell->color_g == 0.5f
                  && spell->color_b == 0.75f,
              "spell-mob retains cloud RGB arguments");
        gm_particles_live_tick(&live, NULL, 0, 0);
        CHECK(spell->age == 1 && spell->texture_index >= 128
                  && spell->texture_index <= 135,
              "spell-mob first ParticleManager tick advances its texture");
        CrVertex verts[6];
        int n = gm_particles_live_emit_spell(
            &live, 0.0f, 0.0f, 0.0f, verts, 6);
        CHECK(n == 6, "spell-mob emits one particles.png billboard");
        CHECK(verts[0].tint.r == 64 && verts[0].tint.g == 128
                  && verts[0].tint.b == 191,
              "spell-mob billboard packs its RGB tint");
        CHECK(verts[0].light == 12.0f && verts[0].blk == 6.0f,
              "spell-mob billboard retains light coordinates");
    }

    /* Lava mixing feeds exact SMOKE_LARGE coordinates into the vanilla
     * ParticleSmokeLarge constructor and layer-0 renderer. */
    gm_particles_live_init(&live, UINT64_C(0x6c617661736d6f6b));
    CHECK(gm_particles_live_spawn_smoke(&live, 12,
          8.25, 65.2, 8.75, 0.0, 0.0, 0.0, 13, 7) == 1,
          "large-smoke constructor accepts vanilla id 12");
    CHECK(gm_particles_live_spawn_smoke(&live, 10,
          0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0, 0) == 0,
          "smoke constructor rejects unrelated particle ids");
    {
        GmLiveParticle *smoke = &live.particles[0];
        CHECK(smoke->kind == GM_LIVE_PARTICLE_SMOKE_LARGE
                  && smoke->texture_index == 0,
              "large smoke starts at its constructor texture state");
        CHECK(smoke->max_age >= 20 && smoke->max_age <= 97,
              "large smoke applies the vanilla 2.5x lifetime");
        CHECK(smoke->original_scale >= 1.875f
                  && smoke->original_scale <= 3.75f,
              "large smoke applies the vanilla 2.5x scale");
        CHECK(smoke->gray >= 0.0f && smoke->gray <= 0.30000001192092896f,
              "large smoke keeps the vanilla dark-gray range");
        CrVertex verts[6];
        CHECK(gm_particles_live_emit_smoke(
                  &live, 1.0f, 0.0f, 0.0f, verts, 6) == 6,
              "large smoke emits one particles.png billboard");
        CHECK(verts[0].tint.r == verts[0].tint.g
                  && verts[0].tint.g == verts[0].tint.b,
              "large smoke billboard retains its grayscale tint");
        CHECK(verts[0].light == 13.0f && verts[0].blk == 7.0f,
              "large smoke billboard retains light coordinates");
        gm_particles_live_tick(&live, NULL, 0, 0);
        CHECK(smoke->age == 0,
              "large smoke stays at constructor age on spawn tick");
        double prior_y = smoke->y;
        gm_particles_live_tick(&live, NULL, 0, 0);
        CHECK(smoke->age == 1 && smoke->texture_index >= 0
                  && smoke->texture_index <= 7 && smoke->y > prior_y,
              "large smoke first update rises and advances its texture");
    }

    /* Tame failure uses the unscaled ParticleSmokeNormal factory. */
    gm_particles_live_init(&live, UINT64_C(0x74616d65736d6f6b));
    CHECK(gm_particles_live_spawn_smoke(&live, 11,
          8.25, 65.2, 8.75, 0.01, 0.02, -0.03, 13, 7) == 1,
          "normal-smoke constructor accepts vanilla id 11");
    {
        GmLiveParticle *smoke = &live.particles[0];
        CHECK(smoke->kind == GM_LIVE_PARTICLE_SMOKE_NORMAL
                  && smoke->texture_index == 0,
              "normal smoke starts at its constructor texture state");
        CHECK(smoke->max_age >= 8 && smoke->max_age <= 40,
              "normal smoke retains the unscaled vanilla lifetime");
        CHECK(smoke->original_scale >= 0.75f
                  && smoke->original_scale <= 1.5f,
              "normal smoke retains the unscaled vanilla size");
        CrVertex verts[6];
        CHECK(gm_particles_live_emit_smoke(
                  &live, 1.0f, 0.0f, 0.0f, verts, 6) == 6,
              "normal smoke emits one particles.png billboard");
        gm_particles_live_tick(&live, NULL, 0, 0);
        CHECK(smoke->age == 0,
              "normal smoke stays at constructor age on spawn tick");
        gm_particles_live_tick(&live, NULL, 0, 0);
        CHECK(smoke->age == 1 && smoke->texture_index >= 0
                  && smoke->texture_index <= 7,
              "normal smoke first update advances its texture");
    }

    /* Breeding and successful taming use ParticleHeart's fixed life/texture. */
    gm_particles_live_init(&live, UINT64_C(0x6272656564686561));
    CHECK(gm_particles_live_spawn_heart(
              &live, 34, 8.25, 65.2, 8.75, 14, 5) == 1,
          "heart constructor accepts vanilla id 34");
    CHECK(gm_particles_live_spawn_heart(
              &live, 33, 0.0, 0.0, 0.0, 0, 0) == 0,
          "heart constructor rejects unrelated particle ids");
    {
        GmLiveParticle *heart = &live.particles[0];
        CHECK(heart->kind == GM_LIVE_PARTICLE_HEART
                  && heart->texture_index == 80 && heart->max_age == 16,
              "heart installs vanilla texture 80 and fixed lifetime");
        CHECK(heart->motion_y > 0.1 && heart->motion_y < 0.103,
              "heart replaces input motion with its small upward impulse");
        CHECK(heart->original_scale >= 1.5f
                  && heart->original_scale <= 3.0f,
              "heart applies the vanilla 1.5x base-particle scale");
        CrVertex verts[6];
        CHECK(gm_particles_live_emit_heart(
                  &live, 0.5f, 0.0f, 0.0f, verts, 6) == 6,
              "heart emits one particles.png billboard");
        CHECK(verts[0].tint.r == 255 && verts[0].tint.g == 255
                  && verts[0].tint.b == 255
                  && verts[0].light == 14.0f && verts[0].blk == 5.0f,
              "heart billboard retains white tint and light coordinates");
        gm_particles_live_tick(&live, NULL, 0, 0);
        CHECK(heart->age == 0,
              "heart stays at constructor age on spawn tick");
        double prior_y = heart->y;
        gm_particles_live_tick(&live, NULL, 0, 0);
        CHECK(heart->age == 1 && heart->y > prior_y,
              "heart first update rises and advances age");
    }

    /* Note blocks feed NOTE's pitch fraction into its six-tick constructor. */
    gm_particles_live_init(&live, UINT64_C(0x6e6f7465626c6f63));
    CHECK(gm_particles_live_spawn_note(
              &live, 23, 8.5, 66.2, 7.5, 0.5, 14, 6) == 1,
          "note constructor accepts vanilla id 23");
    CHECK(gm_particles_live_spawn_note(
              &live, 22, 0.0, 0.0, 0.0, 0.0, 0, 0) == 0,
          "note constructor rejects unrelated particle ids");
    {
        GmLiveParticle *note = &live.particles[0];
        CHECK(note->kind == GM_LIVE_PARTICLE_NOTE
                  && note->texture_index == 64 && note->max_age == 6,
              "note constructor installs texture 64 and six-tick life");
        CHECK(note->motion_y > 0.19 && note->motion_y < 0.21,
              "note constructor adds its exact upward impulse");
        CrVertex verts[6];
        CHECK(gm_particles_live_emit_combat(
                  &live, 0, 0.0f, 0.0f, 0.0f, verts, 6) == 6,
              "note renders one particles.png billboard");
        CHECK(verts[0].light == 14.0f && verts[0].blk == 6.0f,
              "note billboard retains light coordinates");
        gm_particles_live_tick(&live, NULL, 0, 0);
        CHECK(note->age == 0,
              "note stays at constructor age on spawn tick");
        double prior_y = note->y;
        gm_particles_live_tick(&live, NULL, 0, 0);
        CHECK(note->age == 1 && note->y > prior_y,
              "note first update rises and advances age");
    }
    for (int i = 0; i < 6; ++i)
        gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(gm_particles_live_count(&live) == 0,
          "note expires after the vanilla six-age boundary");

    /* Combat packet constructors and attached critical emitters stay bounded. */
    gm_particles_live_init(&live, UINT64_C(0x636f6d626174));
    CHECK(gm_particles_live_spawn_combat(
              &live, 45, 0, 9.5, 65.9, 7.5,
              0.0, 0.0, 0.0, 0.0, 0.0, -1.0, 0.0,
              0.0f, 0.0f, 15, 0) == 1,
          "sweep packet spawns one custom particle");
    CHECK(live.particles[0].kind == GM_LIVE_PARTICLE_SWEEP_ATTACK
              && live.particles[0].max_age == 4
              && live.particles[0].scale == 1.0f,
          "zero-count sweep packet maps speed*offset to unit-size sweep");
    {
        CrVertex verts[12];
        CHECK(gm_particles_live_emit_combat(
                  &live, 0, 0.0f, 180.0f, 0.0f, verts, 12) == 0,
              "sweep does not enter particles.png layer");
        CHECK(gm_particles_live_emit_combat(
                  &live, 3, 0.0f, 180.0f, 0.0f, verts, 12) == 6,
              "sweep renders one sweep.png layer-3 quad");
        CHECK(verts[0].blk == 15.0f,
              "sweep render carries vanilla full-bright lightmap");
    }
    gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(live.particles[0].age == 0,
          "sweep stays at constructor life on spawn tick");
    for (int i = 0; i < 4; ++i)
        gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(gm_particles_live_count(&live) == 0,
          "sweep expires on its fourth vanilla update");

    gm_particles_live_init(&live, UINT64_C(0x64616d616765));
    CHECK(gm_particles_live_spawn_combat(
              &live, 44, 5, 8.5, 65.45, 6.5,
              0.0, 0.0, 0.0, 0.1, 0.0, 0.1, 0.2,
              0.0f, 0.0f, 13, 4) == 5,
          "damage-indicator packet expands its exact count");
    CHECK(gm_particles_live_count(&live) == 5,
          "damage indicators share the fixed particle pool");
    for (int i = 0; i < 5; ++i)
        CHECK(live.particles[i].kind == GM_LIVE_PARTICLE_DAMAGE_INDICATOR
                  && live.particles[i].texture_index == 67
                  && live.particles[i].max_age == 20
                  && live.particles[i].age == 1,
              "damage indicator applies ParticleCrit constructor overrides");
    {
        CrVertex verts[30];
        CHECK(gm_particles_live_emit_combat(
                  &live, 0, 0.0f, 0.0f, 0.0f, verts, 30) == 30,
              "damage indicators render five particles.png quads");
        CHECK(verts[0].light == 13.0f && verts[0].blk == 4.0f,
              "combat particle light coordinates survive packet expansion");
    }

    gm_particles_live_init(&live, UINT64_C(0x637269746963616c));
    int critical_spawned = gm_particles_live_spawn_combat(
        &live, 9, -1, 8.5, 65.0, 6.5,
        0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
        0.9f, 0.9f, 15, 0);
    CHECK(critical_spawned > 0 && critical_spawned <= 16,
          "critical emitter constructor performs sixteen bounded attempts");
    CHECK(live.emitter_count == 1,
          "critical packet retains one three-update emitter");
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i)
        if (live.particles[i].active)
            CHECK(live.particles[i].kind == GM_LIVE_PARTICLE_CRIT
                      && live.particles[i].texture_index == 65,
                  "critical emitter creates ParticleCrit texture 65");
    gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(live.emitter_count == 1,
          "critical emitter does not double-update on packet tick");
    gm_particles_live_tick(&live, NULL, 0, 0);
    gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(live.emitter_count == 0 && gm_particles_live_count(&live) <= 48,
          "critical emitter expires after three bounded 16-attempt batches");

    gm_particles_live_init(&live, UINT64_C(0x6d61676963637269));
    CHECK(gm_particles_live_spawn_combat(
              &live, 10, -1, 8.5, 65.0, 6.5,
              0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
              0.9f, 0.9f, 15, 0) > 0,
          "magic-critical attached emitter spawns");
    for (int i = 0; i < GM_PARTICLES_LIVE_CAP; ++i)
        if (live.particles[i].active)
            CHECK(live.particles[i].kind == GM_LIVE_PARTICLE_CRIT_MAGIC
                      && live.particles[i].texture_index == 66
                      && live.particles[i].color_r
                          < live.particles[i].color_b,
                  "magic critical applies texture and red-channel override");
    CHECK(gm_particles_live_spawn_combat(
              &live, 43, 1, 0.0, 0.0, 0.0,
              0.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
              0.0f, 0.0f, 0, 0) == 0,
          "combat constructor rejects unrelated particle ids");

    /* Ender Chest, pearl, and Eye events share ParticlePortal's curved
     * origin-relative trajectory and age-dependent scale/brightness. */
    gm_particles_live_init(&live, UINT64_C(0x706f7274616c));
    CHECK(gm_particles_live_spawn_portal(
              &live, 24, 4.25, 70.5, -2.75,
              0.5, -0.125, -0.25, 11, 3) == 1,
          "portal constructor accepts vanilla id 24");
    CHECK(gm_particles_live_spawn_portal(
              &live, 23, 0.0, 0.0, 0.0,
              0.0, 0.0, 0.0, 0, 0) == 0,
          "portal constructor rejects unrelated particle ids");
    p = &live.particles[0];
    CHECK(p->kind == GM_LIVE_PARTICLE_PORTAL
              && p->max_age >= 40 && p->max_age <= 49
              && p->texture_index >= 0 && p->texture_index <= 7
              && p->original_scale >= 0.5f
              && p->original_scale < 0.7f,
          "portal constructor installs vanilla lifetime, texel, and scale");
    CHECK(p->color_r >= 0.36f && p->color_r < 0.9f
              && p->color_g >= 0.12f && p->color_g < 0.3f
              && p->color_b >= 0.4f && p->color_b < 1.0f,
          "portal constructor installs vanilla purple color range");
    int portal_max_age = p->max_age;
    gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(p->newborn == 0 && p->age == 0,
          "portal does not double-update on its spawn tick");
    gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(p->age == 1, "portal advances age on its first client update");
    check_close("portal first X", p->x, 4.75);
    check_close("portal first Y", p->y, 71.375);
    check_close("portal first Z", p->z, -3.0);
    {
        CrVertex verts[6];
        CHECK(gm_particles_live_emit_portal(
                  &live, 0.0f, 0.0f, 0.0f, verts, 6) == 6,
              "portal renders one particles.png billboard");
        CHECK(verts[0].light == 11.0f && verts[0].blk >= 3.0f,
              "portal render preserves sky light and raises block light");
    }
    for (int tick = 1; tick <= portal_max_age; ++tick)
        gm_particles_live_tick(&live, NULL, 0, 0);
    CHECK(gm_particles_live_count(&live) == 0,
          "portal expires on ParticlePortal's max-age comparison");

    if (failures) {
        fprintf(stderr, "%d particle test(s) failed\n", failures);
        return 1;
    }
    printf("particles_live: PASS\n");
    return 0;
}
