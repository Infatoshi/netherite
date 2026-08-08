#include "game/audio_live.h"
#include "assets/sound_manifest.h"

#include <limits.h>
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

static int captured_nonzero(const int16_t *pcm, int samples) {
    int count = 0;
    for (int i = 0; i < samples; ++i) count += pcm[i] != 0;
    return count;
}

int main(void) {
    {
        GmRuntime state;
        memset(&state, 0, sizeof state);
        CHECK(gm_audio_requested_music_type(NULL) == GM_MUSIC_MENU,
              "no world selects menu music");
        CHECK(gm_audio_requested_music_type(&state) == GM_MUSIC_GAME,
              "survival Overworld selects game music");
        state.tape_game_mode = GM_MODE_CREATIVE;
        CHECK(gm_audio_requested_music_type(&state) == GM_MUSIC_CREATIVE,
              "creative Overworld selects creative music");
        state.dimension = -1;
        CHECK(gm_audio_requested_music_type(&state) == GM_MUSIC_NETHER,
              "Nether selection precedes game-mode music");
        state.dimension = 0;
        state.credits = 1;
        CHECK(gm_audio_requested_music_type(&state) == GM_MUSIC_CREDITS,
              "credits screen selects credits music");
    }
    static GmRuntime runtime;
    GmAudioLive audio;
    GmAction idle;
    char err[256];
    const int x = 12, y = 78, z = 8;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    {
        GmMusicTicker ticker;
        int stop, play;
        gm_music_ticker_init(&ticker, 0);
        CHECK(ticker.current_type == -1
              && ticker.time_until_next_music == 100,
              "MusicTicker starts with Java's 100-tick delay");
        gm_music_ticker_update(
            &ticker, GM_MUSIC_END_BOSS, 0, &stop, &play);
        CHECK(stop == -1 && play == GM_MUSIC_END_BOSS
              && ticker.current_type == GM_MUSIC_END_BOSS
              && ticker.time_until_next_music == INT_MAX,
              "zero-delay End boss music starts on the first update");
        gm_music_ticker_update(
            &ticker, GM_MUSIC_GAME, 0, &stop, &play);
        CHECK(stop == GM_MUSIC_END_BOSS && play == -1
              && ticker.current_type == -1
              && ticker.time_until_next_music == 5780
              && ticker.random.seed == UINT64_C(0xd4d95138ab6f),
              "music switch consumes both exact Random draws and countdown");
        for (int i = 0; i < 5780; ++i)
            gm_music_ticker_update(
                &ticker, GM_MUSIC_GAME, 0, &stop, &play);
        CHECK(ticker.time_until_next_music == 0 && play == -1,
              "music countdown preserves post-decrement boundary");
        gm_music_ticker_update(
            &ticker, GM_MUSIC_GAME, 0, &stop, &play);
        CHECK(play == GM_MUSIC_GAME && ticker.current_type == GM_MUSIC_GAME
              && ticker.time_until_next_music == INT_MAX,
              "game music starts on the exact countdown boundary");
        gm_music_ticker_update(
            &ticker, GM_MUSIC_GAME, 0, &stop, &play);
        CHECK(stop == -1 && play == -1 && ticker.current_type == -1
              && ticker.time_until_next_music == 12986,
              "finished music samples the exact game delay and decrements");
        ticker.current_type = GM_MUSIC_NETHER;
        ticker.time_until_next_music = INT_MAX;
        gm_music_ticker_stop(&ticker, &stop);
        CHECK(stop == GM_MUSIC_NETHER && ticker.current_type == -1
              && ticker.time_until_next_music == 0,
              "explicit music stop resets the Java ticker boundary");
        gm_music_ticker_update(
            &ticker, GM_MUSIC_NETHER, 0, &stop, &play);
        CHECK(play == GM_MUSIC_NETHER,
              "explicitly stopped music can restart on the next update");
        gm_music_ticker_init(&ticker, 0);
        for (int i = 0; i < 100; ++i)
            gm_music_ticker_update(
                &ticker, GM_MUSIC_CREDITS, 0, &stop, &play);
        CHECK(play == -1 && ticker.time_until_next_music == 0,
              "credits delay reaches zero without integer overflow");
        gm_music_ticker_update(
            &ticker, GM_MUSIC_CREDITS, 0, &stop, &play);
        CHECK(play == GM_MUSIC_CREDITS
              && ticker.time_until_next_music == INT_MAX,
              "credits music starts at the exact initial boundary");
    }
    {
        GmCaveAmbience ambience;
        GmCaveSound cave;
        gm_cave_ambience_init(&ambience, 0, 0, 1);
        CHECK(!gm_cave_ambience_probe(
                  &ambience, 16, 32, 1, 0, 0, 1, 5.0, &cave)
              && ambience.update_lcg == 0,
              "cave probes are disabled while the countdown is active");
        gm_cave_ambience_update(&ambience);
        CHECK(gm_cave_ambience_probe(
                  &ambience, 16, 32, 1, 0, 0, 1, 5.0, &cave)
              && cave.x == 23 && cave.y == 27 && cave.z == 44
              && cave.volume == 0.7F
              && memcmp(&cave.pitch,
                        &(float){0.9662882089614868F}, sizeof cave.pitch) == 0
              && ambience.ambience_ticks == 14029
              && ambience.update_lcg == UINT32_C(0x3c6ef35f)
              && ambience.random.seed == UINT64_C(0x3d93cb7ab84e),
              "cave mood matches exact LCG position, Random draws, and delay");
        gm_cave_ambience_init(&ambience, 0, 0, 0);
        CHECK(!gm_cave_ambience_probe(
                  &ambience, 0, 0, 0, 0, 0, 1, 5.0, &cave)
              && ambience.random.seed == UINT64_C(0x5deece66d),
              "non-air cave candidate consumes no client Random draw");
        CHECK(!gm_cave_ambience_probe(
                  &ambience, 0, 0, 1, 6, 0, 1, 5.0, &cave)
              && ambience.random.seed == UINT64_C(0xbb20b4600a74),
              "lit cave candidate consumes only the light-threshold draw");
    }
    CHECK(init_flat(&runtime), "initialize audio runtime fixture");
    CHECK(gm_runtime_loaded_chunks_begin(&runtime, 1)
          && gm_runtime_loaded_chunk_set(&runtime, 0, 0, 0)
          && gm_runtime_loaded_chunks_finalize(&runtime)
          && gm_runtime_load_sky_light_dim(&runtime, 0, 7, 27, 12, 0)
          && gm_runtime_finalize_sky_light_snapshot_dim(&runtime, 0)
          && gm_runtime_load_block_light_dim(&runtime, 0, 7, 27, 12, 0)
          && gm_runtime_finalize_block_light_snapshot_dim(&runtime, 0),
          "stage the first exact WorldClient cave sample in loaded-chunk order");
    CHECK(gm_sound_asset_spans[GM_SOUND_WITCH_AMBIENT].count > 0
          && gm_sound_asset_spans[GM_SOUND_WITCH_DRINK].count > 0
          && gm_sound_asset_spans[GM_SOUND_WITCH_HURT].count > 0
          && gm_sound_asset_spans[GM_SOUND_WITCH_DEATH].count > 0,
          "Witch lifecycle sounds resolve to owned assets");
    CHECK(gm_sound_asset_spans[GM_SOUND_ENDERMITE_AMBIENT].count > 0
          && gm_sound_asset_spans[GM_SOUND_ENDERMITE_HURT].count > 0
          && gm_sound_asset_spans[GM_SOUND_ENDERMITE_DEATH].count > 0
          && gm_sound_asset_spans[GM_SOUND_ENDERMITE_STEP].count > 0,
          "Endermite lifecycle sounds resolve to owned assets");
    CHECK(gm_sound_asset_spans[GM_SOUND_HUSK_AMBIENT].count > 0
          && gm_sound_asset_spans[GM_SOUND_HUSK_HURT].count > 0
          && gm_sound_asset_spans[GM_SOUND_HUSK_DEATH].count > 0
          && gm_sound_asset_spans[GM_SOUND_HUSK_STEP].count > 0,
          "Husk lifecycle sounds resolve to owned assets");
    CHECK(gm_sound_asset_spans[GM_SOUND_STRAY_AMBIENT].count > 0
          && gm_sound_asset_spans[GM_SOUND_STRAY_HURT].count > 0
          && gm_sound_asset_spans[GM_SOUND_STRAY_DEATH].count > 0
          && gm_sound_asset_spans[GM_SOUND_STRAY_STEP].count > 0,
          "Stray lifecycle sounds resolve to owned assets");
    CHECK(gm_sound_asset_spans[GM_SOUND_POLAR_BEAR_AMBIENT].count > 0
          && gm_sound_asset_spans[GM_SOUND_POLAR_BEAR_BABY_AMBIENT].count > 0
          && gm_sound_asset_spans[GM_SOUND_POLAR_BEAR_HURT].count > 0
          && gm_sound_asset_spans[GM_SOUND_POLAR_BEAR_DEATH].count > 0
          && gm_sound_asset_spans[GM_SOUND_POLAR_BEAR_STEP].count > 0
          && gm_sound_asset_spans[GM_SOUND_POLAR_BEAR_WARNING].count > 0,
          "Polar Bear lifecycle sounds resolve to owned assets");
    CHECK(gm_sound_asset_spans[GM_SOUND_RABBIT_AMBIENT].count > 0
          && gm_sound_asset_spans[GM_SOUND_RABBIT_ATTACK].count > 0
          && gm_sound_asset_spans[GM_SOUND_RABBIT_DEATH].count > 0
          && gm_sound_asset_spans[GM_SOUND_RABBIT_HURT].count > 0
          && gm_sound_asset_spans[GM_SOUND_RABBIT_JUMP].count > 0,
          "Rabbit lifecycle sounds resolve to owned assets");
    CHECK(gm_sound_asset_spans[GM_SOUND_GUARDIAN_AMBIENT].count > 0
          && gm_sound_asset_spans[GM_SOUND_GUARDIAN_ATTACK].count > 0
          && gm_sound_asset_spans[GM_SOUND_GUARDIAN_FLOP].count > 0
          && gm_sound_asset_spans[GM_SOUND_ELDER_GUARDIAN_CURSE].count > 0
          && gm_sound_asset_spans[GM_SOUND_ELDER_GUARDIAN_DEATH].count > 0,
          "Guardian and Elder Guardian sounds resolve to owned assets");
    {
        static GmRuntime curse_runtime;
        CHECK(init_flat(&curse_runtime),
              "initialize Elder Guardian curse fixture");
        int eid = 7701;
        int slot = gm_mobs_spawn_exact(
            &curse_runtime.mobs, GM_MOB_ELDER_GUARDIAN, eid,
            8.5, 4.0, 8.5, 0.0, 0.0, 0.0, 0.0F, 80.0F,
            0, 0, 0, 0);
        CHECK(slot > 0, "spawn Elder Guardian curse fixture");
        curse_runtime.mobs.entity_ticks_existed[slot] =
            (1200 - eid % 1200 - 1 + 1200) % 1200;
        curse_runtime.mobs_enabled = 1;
        gm_runtime_tick(&curse_runtime, idle);
        GmRuntimeSoundEvent curse;
        CHECK(curse_runtime.potion_count == 1
              && curse_runtime.potions[0].id == 4
              && curse_runtime.potions[0].amplifier == 2
              && curse_runtime.potions[0].duration == 6000,
              "Elder cadence applies mining fatigue III for 6000 ticks");
        CHECK(gm_runtime_sound_event_count(&curse_runtime) == 1
              && gm_runtime_sound_event_get(&curse_runtime, 0, &curse)
              && curse.sound == GM_SOUND_ELDER_GUARDIAN_CURSE
              && curse.category == GM_SOUND_CATEGORY_MASTER
              && curse.relative == 1 && curse.eid == eid,
              "Elder cadence emits the relative master curse sound");
        gm_runtime_destroy(&curse_runtime);
    }
    CHECK(gm_sound_asset_spans[GM_SOUND_HOSTILE_SMALL_FALL].count > 0
          && gm_sound_asset_spans[GM_SOUND_HOSTILE_BIG_FALL].count > 0,
          "hostile landing sounds resolve to owned assets");
    CHECK(gm_sound_asset_spans[GM_SOUND_ZOMBIE_VILLAGER_HURT].count > 0
          && gm_sound_asset_spans[
              GM_SOUND_ZOMBIE_VILLAGER_DEATH].count > 0,
          "zombie-villager feedback resolves to distinct owned assets");
    CHECK(gm_sound_asset_spans[GM_SOUND_GENERIC_SMALL_FALL].count > 0
          && gm_sound_asset_spans[GM_SOUND_GENERIC_BIG_FALL].count > 0,
          "generic living landing sounds resolve to owned assets");
    {
        static GmRuntime landing_runtime;
        CHECK(init_flat(&landing_runtime),
              "initialize generic landing audio fixture");
        int slot = gm_mobs_spawn(
            &landing_runtime.mobs, EW_TYPE_SHEEP, 8.5, 220.0, 24.5);
        CHECK(slot > 0, "spawn generic landing audio fixture");
        landing_runtime.mobs_enabled = 1;
        landing_runtime.mobs.persistence_required[slot] = 1;
        landing_runtime.mobs.a.vy[slot] = -0.1;
        landing_runtime.mobs.b.vy[slot] = -0.1;
        landing_runtime.mobs.a.on_ground[slot] = 0;
        landing_runtime.mobs.b.on_ground[slot] = 0;
        landing_runtime.mobs.entity_fall_distance[slot] = 5.0F;
        gm_world_set_block(landing_runtime.world, 8, 219, 24, 1);
        for (int by = 220; by <= 222; ++by)
            gm_world_set_block(landing_runtime.world, 8, by, 24, 0);
        gm_runtime_tick(&landing_runtime, idle);
        GmRuntimeSoundEvent event[3];
        int exact = gm_runtime_sound_event_count(&landing_runtime) == 3;
        for (int i = 0; i < 3; ++i)
            exact = exact && gm_runtime_sound_event_get(
                &landing_runtime, i, &event[i]);
        CHECK(exact
              && event[0].sound == GM_SOUND_GENERIC_SMALL_FALL
              && event[1].sound == GM_SOUND_SHEEP_HURT
              && event[2].sound == GM_SOUND_BLOCK_STONE_FALL
              && event[0].category == GM_SOUND_CATEGORY_NEUTRAL
              && event[1].category == GM_SOUND_CATEGORY_NEUTRAL
              && event[2].category == GM_SOUND_CATEGORY_NEUTRAL,
              "passive landing drains exact generic/feedback/support audio");
        gm_runtime_destroy(&landing_runtime);
    }
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
    CHECK(audio.category_volume[GM_SOUND_CATEGORY_MASTER] == 1.0F
          && audio.category_volume[GM_SOUND_CATEGORY_AMBIENT] == 1.0F,
          "live category sliders initialize at the Java full-volume profile");
    gm_audio_live_set_category_volume(
        &audio, GM_SOUND_CATEGORY_AMBIENT, 0.25F);
    gm_audio_live_set_category_volume(
        &audio, GM_SOUND_CATEGORY_MASTER, -1.0F);
    CHECK(audio.category_volume[GM_SOUND_CATEGORY_AMBIENT] == 0.25F
          && audio.category_volume[GM_SOUND_CATEGORY_MASTER] == 0.0F,
          "live category and master sliders clamp and apply immediately");
    gm_audio_live_set_category_volume(
        &audio, GM_SOUND_CATEGORY_AMBIENT, 1.0F);
    gm_audio_live_set_category_volume(
        &audio, GM_SOUND_CATEGORY_MASTER, 1.0F);
    gm_audio_live_update(
        &audio, &runtime, 8.5, 80.0, 8.5, 180.0F, 0.0F);
    CHECK(audio.next_seq == 3 && audio.dropped == 0
          && audio.active_records == 1 && audio.cave_probes == 1
          && audio.cave_sounds_played == 1,
          "audio consumer starts a record and plays the live ordered cave sample");
    {
        static int16_t pcm[48000 * 2];
        long long nonzero = 0;
        for (int second = 0; second < 30; ++second) {
            CHECK(gm_audio_live_render_samples(&audio, pcm, 48000),
                  "capture 48 kHz stereo loopback second");
            nonzero += captured_nonzero(pcm, 48000 * 2);
            gm_audio_live_update(
                &audio, &runtime, 8.5, 80.0, 8.5, 180.0F, 0.0F);
        }
        CHECK(nonzero > 100000,
              "thirty-second record capture contains sustained PCM output");
    }
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
    CHECK(gm_runtime_load_block(&runtime, x, y, z + 4, 11, 0)
          && gm_runtime_load_block(&runtime, x, y, z + 3, 8, 0)
          && gm_runtime_set_block(&runtime, x + 1, y, z + 4, 1, 0),
          "mix a static lava source beside flowing water");
    {
        GmRuntimeSoundEvent event;
        CHECK(gm_runtime_sound_event_count(&runtime) == 6
              && gm_runtime_sound_event_get(&runtime, 5, &event)
              && event.sound == GM_SOUND_LAVA_EXTINGUISH
              && event.category == GM_SOUND_CATEGORY_BLOCKS,
              "lava mixing emits its represented block sound");
    }
    gm_audio_live_update(
        &audio, &runtime, 8.5, 80.0, 8.5, 180.0F, 0.0F);
    CHECK(audio.next_seq == 6,
          "audio consumer resolves the lava-extinguish manifest entry");
    runtime.tick += 91;
    gm_audio_live_update(
        &audio, &runtime, 8.5, 80.0, 8.5, 180.0F, 0.0F);
    CHECK(audio.active_music_type == -1,
          "live game music honors the initial post-decrement countdown");
    ++runtime.tick;
    gm_audio_live_update(
        &audio, &runtime, 8.5, 80.0, 8.5, 180.0F, 0.0F);
    CHECK(audio.active_music_type == GM_MUSIC_GAME,
          "live consumer starts the selected game music stream");
    gm_audio_live_destroy(&audio);
    CHECK(gm_audio_live_init(&audio, err, sizeof err), err);
    gm_audio_live_update_at(
        &audio, NULL, GM_MUSIC_MENU, 0,
        0.0, 0.0, 0.0, 0.0F, 0.0F);
    gm_audio_live_update_at(
        &audio, NULL, GM_MUSIC_MENU, 100,
        0.0, 0.0, 0.0, 0.0F, 0.0F);
    CHECK(audio.active_music_type == GM_MUSIC_MENU,
          "title client clock starts menu music on the exact boundary");
    {
        static int16_t pcm[48000 * 2];
        long long nonzero = 0;
        for (int second = 0; second < 30; ++second) {
            CHECK(gm_audio_live_render_samples(&audio, pcm, 48000),
                  "capture title-music loopback second");
            nonzero += captured_nonzero(pcm, 48000 * 2);
            gm_audio_live_update_at(
                &audio, NULL, GM_MUSIC_MENU, 101 + second,
                0.0, 0.0, 0.0, 0.0F, 0.0F);
        }
        CHECK(nonzero > 100000,
              "thirty-second title capture contains sustained PCM output");
    }
    gm_audio_live_update_at(
        &audio, &runtime, GM_MUSIC_GAME, 131,
        8.5, 80.0, 8.5, 180.0F, 0.0F);
    CHECK(audio.active_music_type == -1,
          "title-to-world transition stops old music immediately, without a fake crossfade");
    gm_audio_live_destroy(&audio);
    gm_runtime_destroy(&runtime);
    puts("audio_live: PASS (live cave/menu/category lifecycle and bounded streams)");
    return 0;
}
