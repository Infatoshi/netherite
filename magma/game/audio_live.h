#ifndef MAGMA_GAME_AUDIO_LIVE_H
#define MAGMA_GAME_AUDIO_LIVE_H

#include "game/runtime.h"

typedef struct {
    void *impl;
    uint64_t next_seq;
    uint64_t dropped;
    int enabled;
    int active_records;
    int pending_delayed;
    int active_music_type;
    int cave_probes;
    int cave_sounds_played;
    float category_volume[10];
} GmAudioLive;

/* Interactive-only consumer. Failure leaves audio disabled and does not
 * affect simulation startup. Set MAGMA_AUDIO=0 to skip device/resource work. */
int gm_audio_live_init(GmAudioLive *audio, char *err, int err_cap);
void gm_audio_live_update(
    GmAudioLive *audio, const GmRuntime *runtime,
    double x, double y, double z, float yaw, float pitch);
/* Explicit client clock/context boundary. requested_music_type < 0 derives the
 * type from runtime; GM_MUSIC_MENU with runtime == NULL is the title client. */
void gm_audio_live_update_at(
    GmAudioLive *audio, const GmRuntime *runtime,
    int requested_music_type, int64_t client_tick,
    double x, double y, double z, float yaw, float pitch);
void gm_audio_live_set_category_volume(
    GmAudioLive *audio, int category, float volume);
/* Test/capture seam for OpenAL Soft's deterministic loopback device. Returns
 * zero unless MAGMA_AUDIO_LOOPBACK=1 selected that device at initialization. */
int gm_audio_live_render_samples(
    GmAudioLive *audio, int16_t *stereo_pcm, int frames);
void gm_audio_live_destroy(GmAudioLive *audio);

/* Deterministic accessor-selection oracle surface.  Each accessor is seeded
 * from base_seed and a stable hash of its ResourceLocation. */
int gm_audio_live_selector_fixture(
    uint64_t base_seed, int sound, int draws, int *variants, int capacity);

typedef struct {
    int variant;
    float gain;
    float pitch;
    float range;
    int stream;
} GmAudioSourceDescriptor;

enum {
    GM_MUSIC_MENU,
    GM_MUSIC_GAME,
    GM_MUSIC_CREATIVE,
    GM_MUSIC_CREDITS,
    GM_MUSIC_NETHER,
    GM_MUSIC_END_BOSS,
    GM_MUSIC_END,
    GM_MUSIC_TYPE_COUNT
};

typedef struct {
    JavaRandom random;
    int current_type;          /* -1 when no music record is retained */
    int time_until_next_music;
} GmMusicTicker;

/* Device-independent port of MusicTicker.update. sound_playing is the exact
 * SoundHandler.isSoundPlaying result for the retained record. Outputs are
 * one-tick commands for the consumer and never mutate simulation state. */
void gm_music_ticker_init(GmMusicTicker *ticker, long long seed);
void gm_music_ticker_update(
    GmMusicTicker *ticker, int requested_type, int sound_playing,
    int *stop_type, int *play_type);
void gm_music_ticker_stop(GmMusicTicker *ticker, int *stop_type);
/* Minecraft.getAmbientMusicType projected onto represented runtime state. */
int gm_audio_requested_music_type(const GmRuntime *runtime);

typedef struct {
    JavaRandom random;
    uint32_t update_lcg;
    int ambience_ticks;
} GmCaveAmbience;

typedef struct {
    int x, y, z;
    float volume, pitch;
} GmCaveSound;

/* Device-independent WorldClient cave-mood boundary. update() is called once
 * per client world tick; probe() follows it for each selected chunk. */
void gm_cave_ambience_init(
    GmCaveAmbience *ambience, long long seed,
    uint32_t update_lcg, int ambience_ticks);
void gm_cave_ambience_update(GmCaveAmbience *ambience);
int gm_cave_ambience_probe(
    GmCaveAmbience *ambience, int chunk_x, int chunk_z,
    int air, int total_light, int sky_light,
    int player_present, double player_distance_sq,
    GmCaveSound *sound);

/* Deterministic, device-independent SoundManager boundary.  This is the
 * source-level audio oracle: selected owned asset plus the exact values sent
 * to Paulscode before device mixing and distance attenuation. */
int gm_audio_live_source_fixture(
    uint64_t base_seed, int sound, float event_volume, float event_pitch,
    float category_volume, GmAudioSourceDescriptor *out);

#endif
