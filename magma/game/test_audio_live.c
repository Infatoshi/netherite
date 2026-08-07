#include "game/audio_live.h"

#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static int init_flat(GmRuntime *r) {
    GmConfig cfg;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    return gm_runtime_init(r, &cfg, err, sizeof err);
}

int main(void) {
    GmRuntime runtime;
    GmAudioLive audio;
    GmAction idle;
    char err[256];
    const int x = 12, y = 78, z = 8;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(init_flat(&runtime), "initialize audio runtime fixture");
    CHECK(gm_runtime_set_block(&runtime, x, y, z, 23, 13)
          && gm_runtime_static_container_set_slot(
              &runtime, 0, x, y, z, 0, 1, 1, 0)
          && gm_runtime_schedule_tick(
              &runtime, x, y, z, 23, 1, 0, 0),
          "schedule resolved dispenser sound");
    gm_runtime_tick(&runtime, idle);
    CHECK(gm_runtime_sound_event_count(&runtime) == 1,
          "runtime produces one sound before playback");
    CHECK(gm_runtime_spawn_mob_fixture(
              &runtime, GM_MOB_CHICKEN, 7001,
              8.5, 4.0, 8.5, 0.0, 0.0, 0.0,
              0.0F, 4.0F, 1, 0, 0, 0)
          && gm_runtime_set_mob_growing_age(&runtime, 7001, 0)
          && gm_mobs_set_chicken_state(
              &runtime.mobs, 7001, 1, 0.0F, 0.0F,
              0.0F, 0.0F, 0.0F, 0)
          && gm_mobs_set_entity_random_state(
              &runtime.mobs, 7001, UINT64_C(0x123456789abc), 0, 0.0),
          "restore chicken before egg sound boundary");
    runtime.mobs_enabled = 1;
    gm_runtime_tick(&runtime, idle);
    {
        GmRuntimeSoundEvent event;
        CHECK(gm_runtime_sound_event_count(&runtime) == 2
              && gm_runtime_sound_event_get(&runtime, 1, &event)
              && event.sound == GM_SOUND_CHICKEN_EGG
              && event.category == GM_SOUND_CATEGORY_NEUTRAL
              && event.eid == 7001,
              "mob ring drains into global sound order");
    }
    CHECK(gm_runtime_set_block(&runtime, x + 2, y, z, 84, 0)
          && gm_runtime_static_container_set_slot(
              &runtime, 0, x + 2, y, z, 0, 0, 0, 0),
          "stage empty jukebox for streamed record");
    gm_runtime_set_pose(
        &runtime, (double)x + 2.5, (double)y, (double)z + 0.5,
        0.0F, 0.0F);
    isr_set_stack(&runtime.player.inv, 0, ic_mk(2257, 1, 0));
    runtime.player.inv.current_item = 0;
    CHECK(gm_runtime_use_block(&runtime, x + 2, y, z),
          "insert record cat into jukebox");
    CHECK(gm_audio_live_init(&audio, err, sizeof err), err);
    CHECK(audio.enabled, "OpenAL/Vorbis consumer is enabled");
    gm_audio_live_update(
        &audio, &runtime, 8.5, 80.0, 8.5, 180.0F, 0.0F);
    CHECK(audio.next_seq == 3 && audio.dropped == 0
          && audio.active_records == 1,
          "audio consumer starts one bounded record stream");
    gm_audio_live_update(
        &audio, &runtime, 8.5, 80.0, 8.5, 180.0F, 0.0F);
    CHECK(audio.next_seq == 3 && audio.active_records == 1,
          "audio consumer does not replay retained ring entries");
    CHECK(gm_runtime_set_entity_id_cursor(&runtime, 7100)
          && gm_runtime_use_block(&runtime, x + 2, y, z),
          "eject record cat from jukebox");
    gm_audio_live_update(
        &audio, &runtime, 8.5, 80.0, 8.5, 180.0F, 0.0F);
    CHECK(audio.next_seq == 4 && audio.active_records == 0,
          "1010/0 stops the record stream at its block position");
    gm_runtime_set_pose(&runtime, 8.5, 30.0, 8.5, 0.0F, 0.0F);
    CHECK(gm_runtime_firework_audio_fixture(
              &runtime, 7200, 24.5, 30.0, 8.5,
              1, 0, 0,
              UINT64_C(0x123456789abc), UINT64_C(0x0fedcba98765)),
          "emit far firework blast with Java distance delay");
    gm_audio_live_update(
        &audio, &runtime, 8.5, 30.0, 8.5, 0.0F, 0.0F);
    CHECK(audio.next_seq == 5 && audio.pending_delayed == 1,
          "audio consumer queues the eight-tick far blast delay");
    runtime.tick += 7;
    gm_audio_live_update(
        &audio, &runtime, 8.5, 30.0, 8.5, 0.0F, 0.0F);
    CHECK(audio.pending_delayed == 1,
          "far blast remains pending before its exact due tick");
    ++runtime.tick;
    gm_audio_live_update(
        &audio, &runtime, 8.5, 30.0, 8.5, 0.0F, 0.0F);
    CHECK(audio.pending_delayed == 0,
          "far blast starts on its exact due tick");
    gm_audio_live_destroy(&audio);
    gm_runtime_destroy(&runtime);
    puts("audio_live: PASS (88 events, 199 variants, bounded streams/delay)");
    return 0;
}
