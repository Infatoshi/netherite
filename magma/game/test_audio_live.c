/* Sound-seam consumer contract. Runs headlessly and WITHOUT an audio device:
 * every assertion here is about the ring and the consumer's bookkeeping, which
 * is the part that has to be right for the simulation to stay untouched. The
 * OpenAL/Vorbis playback path is behind MAGMA_AUDIO_OPENAL and is exercised by
 * actually playing the game, not by this test. */
#include "game/audio_live.h"
#include "core/config.h"   /* cr_cfg_set(audio) for the disable-path contract */

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
    GmRuntimeSoundEvent event;
    char err[256];

    CHECK(init_flat(&runtime), "initialize audio runtime fixture");
    CHECK(gm_runtime_sound_event_count(&runtime) == 0,
          "a fresh runtime holds no sound events");

    /* Ring ordering and identity. The fixtures are the same appenders the
     * player tick uses, so this is the real producer path. */
    CHECK(gm_runtime_block_break_audio_fixture(&runtime, 10, 64, 20, 1)
          && gm_runtime_block_break_audio_fixture(&runtime, 11, 64, 20, 41)
          && gm_runtime_block_place_audio_fixture(&runtime, 12, 65, 21, 165),
          "block break/place fixtures resolve represented sounds");
    CHECK(!gm_runtime_block_break_audio_fixture(&runtime, 0, 64, 0, 235),
          "unregistered block id emits no fabricated sound");
    CHECK(gm_runtime_sound_event_count(&runtime) == 3,
          "ring retains exactly the three represented events");
    CHECK(gm_runtime_sound_event_get(&runtime, 0, &event)
          && event.seq == 0
          && event.sound == GM_SOUND_BLOCK_STONE_BREAK
          && event.category == GM_SOUND_CATEGORY_BLOCKS
          && event.x == 10.5 && event.y == 64.5 && event.z == 20.5
          && event.delay_ticks == 0 && event.relative == 0,
          "first event keeps family, category, block centre, and delay");
    CHECK(gm_runtime_sound_event_get(&runtime, 1, &event)
          && event.seq == 1 && event.sound == GM_SOUND_BLOCK_METAL_BREAK,
          "second event keeps its own material family and sequence");
    CHECK(gm_runtime_sound_event_get(&runtime, 2, &event)
          && event.seq == 2 && event.sound == GM_SOUND_BLOCK_SLIME_PLACE,
          "placement appends after the breaks in emission order");
    CHECK(!gm_runtime_sound_event_get(&runtime, 3, &event),
          "reading past the retained count fails instead of inventing an event");

    /* End to end through the real tick: a held attack on a placed stone block
     * must produce the interleaved hit sounds and then exactly one break. This
     * is what proves the emit sites in player_ctl/runtime are wired, as opposed
     * to only the fixtures above. */
    gm_runtime_sound_events_clear(&runtime);
    {
        GmAction a;
        int breaks = 0, hits = 0, other = 0;
        const int bx = 8, by = 5, bz = 11;
        CHECK(gm_runtime_set_block(&runtime, bx, by, bz, 1, 0),
              "stage a stone block in front of the player");
        /* Stand one block south, eye-level with the target. */
        gm_runtime_set_pose(&runtime, 8.5, 5.0, 9.5, 0.0F, 0.0F);
        memset(&a, 0, sizeof a);
        a.attack = 1;
        a.hotbar_sel = -1;
        for (int t = 0; t < 400 &&
                gm_world_block(runtime.world, bx, by, bz) == 1; ++t)
            gm_runtime_tick(&runtime, a);
        CHECK(gm_world_block(runtime.world, bx, by, bz) == 0,
              "held attack breaks the staged stone block");
        for (int i = 0; i < gm_runtime_sound_event_count(&runtime); ++i) {
            CHECK(gm_runtime_sound_event_get(&runtime, i, &event), "read event");
            if (event.sound == GM_SOUND_BLOCK_STONE_BREAK) ++breaks;
            else if (event.sound == GM_SOUND_BLOCK_STONE_HIT) ++hits;
            else ++other;
        }
        CHECK(breaks == 1, "a single break emits exactly one break sound");
        CHECK(hits >= 1, "progressive mining emits at least one hit sound");
        CHECK(other == 0, "mining stone emits no sound from another material");
    }
    gm_runtime_sound_events_clear(&runtime);

    /* audio=0 is the documented "do no device or resource work" switch.
     * The consumer must stay disabled AND must not report an error, so a
     * headless run is a clean skip rather than a warning. */
    {
        GmAudioLive audio;
        CHECK(cr_cfg_set("audio", "0") == 0, "set audio=0");
        err[0] = '\0';
        CHECK(gm_audio_live_init(&audio, err, sizeof err),
              "audio=0 initializes successfully");
        CHECK(!audio.enabled && !audio.impl && err[0] == '\0',
              "audio=0 leaves the consumer disabled and silent");
        gm_audio_live_update(&audio, &runtime, 8.5, 80.0, 8.5, 0.0F, 0.0F);
        CHECK(audio.next_seq == 0 && audio.dropped == 0,
              "a disabled consumer never advances its watermark");
        gm_audio_live_destroy(&audio);
        CHECK(cr_cfg_set("audio", "1") == 0, "restore audio=1");
    }

    /* Producing sound must never disturb the ring's owner. The events above
     * were appended by the same code the tick runs; the runtime is still
     * usable and a clear returns it to the pre-audio state exactly. */
    gm_runtime_sound_events_clear(&runtime);
    CHECK(gm_runtime_sound_event_count(&runtime) == 0
          && !gm_runtime_sound_event_get(&runtime, 0, &event),
          "clearing the ring drops every retained event");

    /* Overflow accounting. GM_RUNTIME_SOUND_EVENTS+8 appends must retain the
     * NEWEST window and count the loss, never silently swallow it: a consumer
     * detects the gap from the jump in seq. */
    {
        uint64_t first_seq = runtime.sound_event_next_seq;
        for (int i = 0; i < GM_RUNTIME_SOUND_EVENTS + 8; ++i)
            CHECK(gm_runtime_block_break_audio_fixture(&runtime, i, 64, 0, 1),
                  "overflow fixture appends");
        CHECK(gm_runtime_sound_event_count(&runtime) == GM_RUNTIME_SOUND_EVENTS,
              "ring saturates at its declared capacity");
        CHECK(runtime.sound_event_dropped == 8,
              "every overwritten event is accounted for");
        CHECK(gm_runtime_sound_event_get(&runtime, 0, &event)
              && event.seq == first_seq + 8,
              "the retained window starts after the dropped events");
    }

#ifdef MAGMA_AUDIO_OPENAL
    puts("audio_live: PASS (ring contract; built WITH OpenAL/Vorbis)");
#else
    /* Without the flag the consumer must refuse with a named reason rather
     * than pretending to be enabled. */
    {
        GmAudioLive audio;
        err[0] = '\0';
        CHECK(!gm_audio_live_init(&audio, err, sizeof err),
              "a build without OpenAL refuses to enable audio");
        CHECK(err[0] != '\0' && !audio.enabled,
              "the refusal carries a reason and leaves audio disabled");
        gm_audio_live_destroy(&audio);
    }
    puts("audio_live: PASS (ring contract; built WITHOUT OpenAL/Vorbis)");
#endif
    gm_runtime_destroy(&runtime);
    return 0;
}
