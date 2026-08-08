#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

#define CHECK(condition, message) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL: %s\n", message); \
        return 1; \
    } \
} while (0)

int main(void) {
    GmConfig cfg;
    GmRuntime runtime;
    GmRuntimeSoundEvent splash, swim;
    GmAction idle;
    JavaRandom expected_random;
    GmPlayerSplashParticle expected_particles[GM_PLAYER_SPLASH_PARTICLE_CAP];
    char err[256];
    const uint64_t seed48 = UINT64_C(0x0fedcba98765);

    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err), err);

    for (int x = 7; x <= 9; ++x)
        for (int z = 7; z <= 24; ++z)
            gm_world_set_block_meta(runtime.world, x, 6, z, 1, 0);
    gm_runtime_set_pose(&runtime, 8.5, 7.0, 8.5, 0.0F, 0.0F);
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_sound_event_count(&runtime) == 0,
          "first dry player update emits no movement sound");

    for (int x = 7; x <= 9; ++x)
        for (int z = 7; z <= 24; ++z)
            for (int y = 7; y <= 8; ++y)
                gm_world_set_block_meta(runtime.world, x, y, z, 9, 0);
    jrand_set_seed48(&expected_random, seed48);
    float splash_volume = gm_player_movement_audio_volume(
        GM_PLAYER_MOVEMENT_AUDIO_SPLASH, 0.0, 0.0, 7.0);
    float splash_pitch = gm_player_movement_audio_pitch(
        GM_PLAYER_MOVEMENT_AUDIO_SPLASH, &expected_random);
    int expected_particle_count = gm_player_splash_particles(
        &expected_random, 8.5, 7.0, 8.5, 0.6F, 0.0, 0.0, 7.0,
        expected_particles, GM_PLAYER_SPLASH_PARTICLE_CAP);
    float swim_volume = gm_player_movement_audio_volume(
        GM_PLAYER_MOVEMENT_AUDIO_SWIM, 0.0, 0.0, 7.0);
    float swim_pitch = gm_player_movement_audio_pitch(
        GM_PLAYER_MOVEMENT_AUDIO_SWIM, &expected_random);
    CHECK(gm_runtime_set_client_player_random_seed48(&runtime, seed48),
          "set exact client-player Random cursor");
    gm_runtime_set_velocity(&runtime, 0.0, 0.0, 7.0);
    gm_runtime_tick(&runtime, idle);

    CHECK(gm_runtime_sound_event_count(&runtime) == 2
          && gm_runtime_sound_event_get(&runtime, 0, &splash)
          && gm_runtime_sound_event_get(&runtime, 1, &swim),
          "water entry emits exactly two ordered movement events");
    CHECK(splash.sound == GM_SOUND_PLAYER_SPLASH
          && swim.sound == GM_SOUND_PLAYER_SWIM
          && splash.category == GM_SOUND_CATEGORY_PLAYERS
          && swim.category == GM_SOUND_CATEGORY_PLAYERS,
          "movement events use exact identities and player category");
    CHECK(splash.x == 8.5 && splash.y == 7.0 && splash.z == 8.5
          && swim.x == 8.5 && swim.y == 7.0 && swim.z == 15.5,
          "splash uses pre-move and swim uses post-move player position");
    CHECK(splash.volume == splash_volume && splash.pitch == splash_pitch
          && swim.volume == swim_volume && swim.pitch == swim_pitch
          && runtime.client_player_random_seed48 == expected_random.seed,
          "movement scalars and full particle-coupled RNG cursor are exact");
    CHECK(gm_runtime_particle_event_count(&runtime) == expected_particle_count,
          "water entry exports all 26 current-tick particle calls");
    for (int i = 0; i < expected_particle_count; ++i) {
        GmRuntimeParticleEvent actual;
        GmPlayerSplashParticle *expected = &expected_particles[i];
        CHECK(gm_runtime_particle_event_get(&runtime, i, &actual)
              && actual.kind == expected->kind
              && actual.dimension == 0
              && actual.x == expected->x && actual.y == expected->y
              && actual.z == expected->z
              && actual.motion_x == expected->motion_x
              && actual.motion_y == expected->motion_y
              && actual.motion_z == expected->motion_z,
              "runtime particle call matches exact Java-order arguments");
    }

    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_particle_event_count(&runtime) == 0,
          "current-tick particle stream clears without a new water entry");

    gm_runtime_destroy(&runtime);
    puts("player movement audio runtime: PASS (sound + 26 particle calls + RNG)");
    return 0;
}
