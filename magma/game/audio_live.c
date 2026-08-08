#include "game/audio_live.h"

#ifdef MAGMA_AUDIO_OPENAL
#include "assets/sound_manifest.h"
#define AL_ALEXT_PROTOTYPES
#include <AL/al.h>
#include <AL/alc.h>
#include <AL/alext.h>
#include <vorbis/vorbisfile.h>
#endif

#include <math.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GM_AUDIO_SOURCES 33
#define GM_AUDIO_RECORD_STREAMS 4
#define GM_AUDIO_RECORD_BUFFERS 4
#define GM_AUDIO_RECORD_PCM_BYTES 65536
#define GM_AUDIO_DELAYED 256

typedef struct { int min_delay, max_delay; } GmMusicTypeDelay;

static const GmMusicTypeDelay gm_music_delays[GM_MUSIC_TYPE_COUNT] = {
    {20, 600}, {12000, 24000}, {1200, 3600},
    {INT_MAX, INT_MAX}, {1200, 3600}, {0, 0}, {6000, 24000}
};

static int music_random_between(JavaRandom *random, int low, int high) {
    if (low == high) return low;
    return low + jrand_int_bound(random, high - low + 1);
}

void gm_music_ticker_init(GmMusicTicker *ticker, long long seed) {
    if (!ticker) return;
    memset(ticker, 0, sizeof *ticker);
    jrand_set(&ticker->random, seed);
    ticker->current_type = -1;
    ticker->time_until_next_music = 100;
}

void gm_music_ticker_update(
        GmMusicTicker *ticker, int requested_type, int sound_playing,
        int *stop_type, int *play_type) {
    const GmMusicTypeDelay *type;
    if (stop_type) *stop_type = -1;
    if (play_type) *play_type = -1;
    if (!ticker || requested_type < 0
            || requested_type >= GM_MUSIC_TYPE_COUNT)
        return;
    type = &gm_music_delays[requested_type];
    if (ticker->current_type >= 0) {
        if (ticker->current_type != requested_type) {
            if (stop_type) *stop_type = ticker->current_type;
            ticker->time_until_next_music = music_random_between(
                &ticker->random, 0, type->min_delay / 2);
        }
        if (!sound_playing) {
            ticker->current_type = -1;
            int delay = music_random_between(
                &ticker->random, type->min_delay, type->max_delay);
            if (delay < ticker->time_until_next_music)
                ticker->time_until_next_music = delay;
        }
    }
    if (ticker->time_until_next_music > type->max_delay)
        ticker->time_until_next_music = type->max_delay;
    if (ticker->current_type < 0
            && ticker->time_until_next_music-- <= 0) {
        ticker->current_type = requested_type;
        ticker->time_until_next_music = INT_MAX;
        if (play_type) *play_type = requested_type;
    }
}

void gm_music_ticker_stop(GmMusicTicker *ticker, int *stop_type) {
    if (stop_type) *stop_type = -1;
    if (!ticker || ticker->current_type < 0) return;
    if (stop_type) *stop_type = ticker->current_type;
    ticker->current_type = -1;
    ticker->time_until_next_music = 0;
}

void gm_cave_ambience_init(
        GmCaveAmbience *ambience, long long seed,
        uint32_t update_lcg, int ambience_ticks) {
    if (!ambience) return;
    memset(ambience, 0, sizeof *ambience);
    jrand_set(&ambience->random, seed);
    ambience->update_lcg = update_lcg;
    ambience->ambience_ticks = ambience_ticks < 0 ? 0 : ambience_ticks;
}

void gm_cave_ambience_update(GmCaveAmbience *ambience) {
    if (ambience && ambience->ambience_ticks > 0)
        --ambience->ambience_ticks;
}

int gm_cave_ambience_probe(
        GmCaveAmbience *ambience, int chunk_x, int chunk_z,
        int air, int total_light, int sky_light,
        int player_present, double player_distance_sq,
        GmCaveSound *sound) {
    uint32_t sample;
    int x, y, z;
    if (!ambience || ambience->ambience_ticks != 0) return 0;
    ambience->update_lcg = ambience->update_lcg * 3u + 1013904223u;
    sample = ambience->update_lcg >> 2;
    x = chunk_x + (int)(sample & 15u);
    z = chunk_z + (int)((sample >> 8) & 15u);
    y = (int)((sample >> 16) & 255u);
    if (!air || total_light > jrand_int_bound(&ambience->random, 8)
            || sky_light > 0 || !player_present
            || player_distance_sq <= 4.0)
        return 0;
    if (sound) {
        sound->x = x;
        sound->y = y;
        sound->z = z;
        sound->volume = 0.7F;
        sound->pitch = 0.8F + jrand_float(&ambience->random) * 0.2F;
    } else {
        (void)jrand_float(&ambience->random);
    }
    ambience->ambience_ticks = 6000
        + jrand_int_bound(&ambience->random, 12000);
    return 1;
}

static void audio_error(char *err, int cap, const char *message) {
    if (err && cap > 0) snprintf(err, (size_t)cap, "%s", message);
}

#ifdef MAGMA_AUDIO_OPENAL
typedef struct {
    OggVorbis_File file;
    ALuint source;
    ALuint buffers[GM_AUDIO_RECORD_BUFFERS];
    uint64_t serial;
    int active, file_open, dimension;
    double x, y, z;
} GmAudioRecord;

typedef struct {
    int active;
    int64_t due_tick;
    GmRuntimeSoundEvent event;
} GmAudioDelayed;

typedef struct {
    ALCdevice *device;
    ALCcontext *context;
    int loopback;
    ALuint buffers[GM_SOUND_ASSET_VARIANT_COUNT];
    ALuint sources[GM_AUDIO_SOURCES];
    GmAudioRecord records[GM_AUDIO_RECORD_STREAMS];
    GmAudioRecord music;
    GmAudioDelayed delayed[GM_AUDIO_DELAYED];
    int delayed_count;
    unsigned int source_cursor;
    uint64_t record_serial;
    GmMusicTicker music_ticker;
    int64_t last_music_tick;
    GmCaveAmbience cave_ambience;
    int64_t last_cave_tick;
    int cave_chunk_cursor;
    float category_volume[10];
    float source_raw_gain[GM_AUDIO_SOURCES];
    float source_attenuation[GM_AUDIO_SOURCES];
    int source_category[GM_AUDIO_SOURCES];
    JavaRandom selector_random[GM_SOUND_ASSET_NODE_COUNT];
    double listener_x, listener_y, listener_z;
    char objects[512];
} GmAudioImpl;

static float clamp01(float value) {
    return value < 0.0F ? 0.0F : value > 1.0F ? 1.0F : value;
}

static float clamp_pitch(float value) {
    return value < 0.5F ? 0.5F : value > 2.0F ? 2.0F : value;
}

static int source_index(const GmAudioImpl *impl, ALuint source) {
    if (!impl) return -1;
    for (int i = 0; i < GM_AUDIO_SOURCES; ++i)
        if (impl->sources[i] == source) return i;
    return -1;
}

static void set_source_mix(
        GmAudioImpl *impl, ALuint source, int category,
        float raw_gain, float attenuation) {
    int index = source_index(impl, source);
    float category_gain = category >= 0 && category < 10
        ? impl->category_volume[category] : 1.0F;
    if (index >= 0) {
        impl->source_raw_gain[index] = raw_gain;
        impl->source_attenuation[index] = attenuation;
        impl->source_category[index] = category;
    }
    alSourcef(source, AL_GAIN,
        clamp01(raw_gain * category_gain) * attenuation);
}

static int directory_exists(const char *path) {
    struct stat value;
    return path && stat(path, &value) == 0 && S_ISDIR(value.st_mode);
}

static uint64_t selector_seed_base(void) {
    const char *text = getenv("MAGMA_AUDIO_SELECTOR_SEED");
    char *end = NULL;
    unsigned long long value;
    if (!text || !*text) return UINT64_C(0x4e65746865726974);
    value = strtoull(text, &end, 0);
    return end && *end == '\0' ? (uint64_t)value
                                : UINT64_C(0x4e65746865726974);
}

static int find_objects(char *out, int cap) {
    const char *override = getenv("MAGMA_ASSET_OBJECTS");
    static const char *const candidates[] = {
        "java/Minecraft/run/gradle/caches/minecraft/assets/objects",
        "../../java/Minecraft/run/gradle/caches/minecraft/assets/objects",
        "../java/Minecraft/run/gradle/caches/minecraft/assets/objects"
    };
    if (directory_exists(override)) {
        snprintf(out, (size_t)cap, "%s", override);
        return 1;
    }
    for (size_t i = 0; i < sizeof candidates / sizeof candidates[0]; ++i) {
        if (!directory_exists(candidates[i])) continue;
        snprintf(out, (size_t)cap, "%s", candidates[i]);
        return 1;
    }
    return 0;
}

static int decode_buffer(
        const char *objects, const char *hash, ALuint buffer) {
    char path[640];
    OggVorbis_File file;
    vorbis_info *info;
    ogg_int64_t frames;
    size_t cap, used = 0;
    unsigned char *pcm;
    int section = 0;
    snprintf(path, sizeof path, "%s/%c%c/%s",
             objects, hash[0], hash[1], hash);
    if (ov_fopen(path, &file) != 0) return 0;
    info = ov_info(&file, -1);
    frames = ov_pcm_total(&file, -1);
    if (!info || (info->channels != 1 && info->channels != 2)
            || info->rate <= 0 || frames <= 0
            || (uint64_t)frames > SIZE_MAX / (2u * (unsigned)info->channels)) {
        ov_clear(&file);
        return 0;
    }
    cap = (size_t)frames * (size_t)info->channels * 2u;
    pcm = (unsigned char *)malloc(cap);
    if (!pcm) { ov_clear(&file); return 0; }
    while (used < cap) {
        long got = ov_read(
            &file, (char *)pcm + used, (int)(cap - used),
            0, 2, 1, &section);
        if (got == 0) break;
        if (got < 0) { free(pcm); ov_clear(&file); return 0; }
        used += (size_t)got;
    }
    alBufferData(buffer,
        info->channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
        pcm, (ALsizei)used, (ALsizei)info->rate);
    free(pcm);
    ov_clear(&file);
    return alGetError() == AL_NO_ERROR;
}

static int stream_record_buffer(GmAudioRecord *record, ALuint buffer) {
    unsigned char pcm[GM_AUDIO_RECORD_PCM_BYTES];
    vorbis_info *info;
    size_t used = 0;
    int section = 0;
    if (!record || !record->file_open) return 0;
    info = ov_info(&record->file, -1);
    if (!info || (info->channels != 1 && info->channels != 2)
            || info->rate <= 0)
        return 0;
    while (used < sizeof pcm) {
        long got = ov_read(
            &record->file, (char *)pcm + used,
            (int)(sizeof pcm - used), 0, 2, 1, &section);
        if (got == 0) break;
        if (got < 0) return 0;
        used += (size_t)got;
    }
    if (used == 0) return 0;
    alBufferData(buffer,
        info->channels == 1 ? AL_FORMAT_MONO16 : AL_FORMAT_STEREO16,
        pcm, (ALsizei)used, (ALsizei)info->rate);
    return alGetError() == AL_NO_ERROR;
}

static void stop_record(GmAudioRecord *record) {
    ALint queued = 0;
    if (!record) return;
    if (record->source) {
        alSourceStop(record->source);
        alGetSourcei(record->source, AL_BUFFERS_QUEUED, &queued);
        while (queued-- > 0) {
            ALuint buffer = 0;
            alSourceUnqueueBuffers(record->source, 1, &buffer);
        }
    }
    if (record->file_open) ov_clear(&record->file);
    record->active = 0;
    record->file_open = 0;
    record->serial = 0;
}

static int choose_node(GmAudioImpl *impl, int node_index) {
    const GmSoundAssetNode *node;
    int roll;
    if (!impl || node_index < 0 || node_index >= GM_SOUND_ASSET_NODE_COUNT)
        return -1;
    node = &gm_sound_asset_nodes[node_index];
    if (node->count <= 0 || node->total_weight <= 0) return -1;
    roll = jrand_int_bound(
        &impl->selector_random[node_index], node->total_weight);
    for (int i = 0; i < node->count; ++i) {
        const GmSoundAssetEdge *edge =
            &gm_sound_asset_edges[node->start + i];
        if (roll < edge->weight) {
            if (edge->target >= 0)
                return choose_node(impl, edge->target);
            return edge->variant;
        }
        roll -= edge->weight;
    }
    return -1;
}

static int choose_variant(
        GmAudioImpl *impl, const GmRuntimeSoundEvent *event) {
    if (!event || event->sound <= 0 || event->sound >= GM_SOUND_COUNT)
        return -1;
    return choose_node(impl, gm_sound_asset_roots[event->sound]);
}

int gm_audio_live_selector_fixture(
        uint64_t base_seed, int sound, int draws,
        int *variants, int capacity) {
    GmAudioImpl impl;
    GmRuntimeSoundEvent event;
    if (sound <= 0 || sound >= GM_SOUND_COUNT || draws < 0
            || draws > capacity || (draws > 0 && !variants))
        return 0;
    memset(&impl, 0, sizeof impl);
    memset(&event, 0, sizeof event);
    event.sound = sound;
    for (int i = 0; i < GM_SOUND_ASSET_NODE_COUNT; ++i)
        jrand_set(&impl.selector_random[i],
                  (i64)(base_seed ^ gm_sound_asset_seed_keys[i]));
    for (int i = 0; i < draws; ++i) {
        variants[i] = choose_variant(&impl, &event);
    }
    return 1;
}

int gm_audio_live_source_fixture(
        uint64_t base_seed, int sound, float event_volume, float event_pitch,
        float category_volume, GmAudioSourceDescriptor *out) {
    GmAudioImpl impl;
    GmRuntimeSoundEvent event;
    int variant;
    float raw_volume;
    if (!out || sound <= 0 || sound >= GM_SOUND_COUNT) return 0;
    memset(&impl, 0, sizeof impl);
    memset(&event, 0, sizeof event);
    event.sound = sound;
    for (int i = 0; i < GM_SOUND_ASSET_NODE_COUNT; ++i)
        jrand_set(&impl.selector_random[i],
                  (i64)(base_seed ^ gm_sound_asset_seed_keys[i]));
    variant = choose_variant(&impl, &event);
    memset(out, 0, sizeof *out);
    out->variant = variant;
    if (variant < 0) return 1;
    raw_volume = event_volume * gm_sound_asset_variants[variant].volume;
    out->gain = raw_volume * category_volume;
    if (out->gain < 0.0F) out->gain = 0.0F;
    if (out->gain > 1.0F) out->gain = 1.0F;
    out->pitch = event_pitch * gm_sound_asset_variants[variant].pitch;
    if (out->pitch < 0.5F) out->pitch = 0.5F;
    if (out->pitch > 2.0F) out->pitch = 2.0F;
    out->range = 16.0F * (raw_volume > 1.0F ? raw_volume : 1.0F);
    out->stream = gm_sound_asset_variants[variant].stream;
    return 1;
}

static int same_record_position(
        const GmAudioRecord *record, const GmRuntimeSoundEvent *event) {
    return record && event && record->active
        && record->dimension == event->dimension
        && record->x == event->x && record->y == event->y
        && record->z == event->z;
}

static void stop_record_at(
        GmAudioImpl *impl, const GmRuntimeSoundEvent *event) {
    for (int i = 0; i < GM_AUDIO_RECORD_STREAMS; ++i)
        if (same_record_position(&impl->records[i], event))
            stop_record(&impl->records[i]);
}

static void start_record(
        GmAudioImpl *impl, const GmRuntimeSoundEvent *event) {
    GmAudioRecord *record = NULL;
    int variant = choose_variant(impl, event);
    char path[640];
    ALuint queued[GM_AUDIO_RECORD_BUFFERS];
    int queued_count = 0;
    if (variant < 0 || !gm_sound_asset_variants[variant].stream)
        return;
    stop_record_at(impl, event);
    for (int i = 0; i < GM_AUDIO_RECORD_STREAMS; ++i) {
        if (!impl->records[i].active) {
            record = &impl->records[i];
            break;
        }
        if (!record || impl->records[i].serial < record->serial)
            record = &impl->records[i];
    }
    if (!record) return;
    stop_record(record);
    snprintf(path, sizeof path, "%s/%c%c/%s",
        impl->objects, gm_sound_asset_variants[variant].hash[0],
        gm_sound_asset_variants[variant].hash[1],
        gm_sound_asset_variants[variant].hash);
    if (ov_fopen(path, &record->file) != 0) return;
    record->file_open = 1;
    for (int i = 0; i < GM_AUDIO_RECORD_BUFFERS; ++i) {
        if (!stream_record_buffer(record, record->buffers[i])) break;
        queued[queued_count++] = record->buffers[i];
    }
    if (queued_count == 0) {
        stop_record(record);
        return;
    }
    record->active = 1;
    record->dimension = event->dimension;
    record->x = event->x;
    record->y = event->y;
    record->z = event->z;
    record->serial = ++impl->record_serial;
    alSourceQueueBuffers(record->source, queued_count, queued);
    {
        float raw_gain = event->volume
            * gm_sound_asset_variants[variant].volume;
        float pitch = event->pitch
            * gm_sound_asset_variants[variant].pitch;
        float range = 16.0F * (raw_gain > 1.0F ? raw_gain : 1.0F);
        double dx = event->x - impl->listener_x;
        double dy = event->y - impl->listener_y;
        double dz = event->z - impl->listener_z;
        float distance = (float)sqrt(dx * dx + dy * dy + dz * dz);
        float attenuation = distance <= 0.0F ? 1.0F
            : distance >= range ? 0.0F : 1.0F - distance / range;
        if (pitch < 0.5F) pitch = 0.5F;
        if (pitch > 2.0F) pitch = 2.0F;
        set_source_mix(impl, record->source, GM_SOUND_CATEGORY_RECORDS,
            raw_gain, attenuation);
        alSourcef(record->source, AL_PITCH, pitch);
    }
    alSourcei(record->source, AL_SOURCE_RELATIVE, AL_FALSE);
    alSource3f(record->source, AL_POSITION,
        (float)event->x, (float)event->y, (float)event->z);
    alSourcef(record->source, AL_ROLLOFF_FACTOR, 0.0F);
    alSourcePlay(record->source);
}

static void update_records(GmAudioImpl *impl, int dimension) {
    for (int i = 0; i < GM_AUDIO_RECORD_STREAMS; ++i) {
        GmAudioRecord *record = &impl->records[i];
        ALint processed = 0, queued = 0, state = 0;
        if (!record->active) continue;
        if (record->dimension != dimension) {
            stop_record(record);
            continue;
        }
        alGetSourcei(record->source, AL_BUFFERS_PROCESSED, &processed);
        while (processed-- > 0) {
            ALuint buffer = 0;
            alSourceUnqueueBuffers(record->source, 1, &buffer);
            if (stream_record_buffer(record, buffer))
                alSourceQueueBuffers(record->source, 1, &buffer);
        }
        alGetSourcei(record->source, AL_BUFFERS_QUEUED, &queued);
        if (queued == 0) {
            stop_record(record);
            continue;
        }
        alGetSourcei(record->source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) alSourcePlay(record->source);
    }
    {
        GmAudioRecord *record = &impl->music;
        ALint processed = 0, queued = 0, state = 0;
        if (!record->active) return;
        alGetSourcei(record->source, AL_BUFFERS_PROCESSED, &processed);
        while (processed-- > 0) {
            ALuint buffer = 0;
            alSourceUnqueueBuffers(record->source, 1, &buffer);
            if (stream_record_buffer(record, buffer))
                alSourceQueueBuffers(record->source, 1, &buffer);
        }
        alGetSourcei(record->source, AL_BUFFERS_QUEUED, &queued);
        if (queued == 0) {
            stop_record(record);
            return;
        }
        alGetSourcei(record->source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) alSourcePlay(record->source);
    }
}

static int active_record_count(const GmAudioImpl *impl) {
    int count = 0;
    for (int i = 0; i < GM_AUDIO_RECORD_STREAMS; ++i)
        count += impl->records[i].active != 0;
    return count;
}

static ALuint acquire_source(GmAudioImpl *impl) {
    const unsigned int ordinary =
        GM_AUDIO_SOURCES - GM_AUDIO_RECORD_STREAMS - 1;
    for (unsigned int i = 0; i < ordinary; ++i) {
        unsigned int index = (impl->source_cursor + (unsigned)i)
            % ordinary;
        ALint state = 0;
        alGetSourcei(impl->sources[index], AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            impl->source_cursor = (index + 1u) % ordinary;
            return impl->sources[index];
        }
    }
    {
        unsigned int index = impl->source_cursor++ % ordinary;
        alSourceStop(impl->sources[index]);
        return impl->sources[index];
    }
}

static int start_music(GmAudioImpl *impl, int music_type) {
    GmAudioRecord *record;
    ALuint queued[GM_AUDIO_RECORD_BUFFERS];
    char path[640];
    int root, variant, queued_count = 0;
    if (!impl || music_type < 0 || music_type >= GM_MUSIC_TYPE_COUNT)
        return 0;
    root = gm_special_sound_asset_roots[1 + music_type];
    variant = choose_node(impl, root);
    if (variant < 0 || !gm_sound_asset_variants[variant].stream)
        return 0;
    record = &impl->music;
    stop_record(record);
    snprintf(path, sizeof path, "%s/%c%c/%s",
        impl->objects, gm_sound_asset_variants[variant].hash[0],
        gm_sound_asset_variants[variant].hash[1],
        gm_sound_asset_variants[variant].hash);
    if (ov_fopen(path, &record->file) != 0) return 0;
    record->file_open = 1;
    for (int i = 0; i < GM_AUDIO_RECORD_BUFFERS; ++i) {
        if (!stream_record_buffer(record, record->buffers[i])) break;
        queued[queued_count++] = record->buffers[i];
    }
    if (queued_count == 0) {
        stop_record(record);
        return 0;
    }
    record->active = 1;
    record->dimension = music_type;
    record->serial = ++impl->record_serial;
    alSourceQueueBuffers(record->source, queued_count, queued);
    set_source_mix(impl, record->source, GM_SOUND_CATEGORY_MUSIC, 1.0F, 1.0F);
    alSourcef(record->source, AL_PITCH, 1.0F);
    alSourcei(record->source, AL_SOURCE_RELATIVE, AL_TRUE);
    alSource3f(record->source, AL_POSITION, 0.0F, 0.0F, 0.0F);
    alSourcef(record->source, AL_ROLLOFF_FACTOR, 0.0F);
    alSourcePlay(record->source);
    return 1;
}

int gm_audio_requested_music_type(const GmRuntime *runtime) {
    if (!runtime) return GM_MUSIC_MENU;
    if (runtime->credits) return GM_MUSIC_CREDITS;
    if (runtime->dimension == -1) return GM_MUSIC_NETHER;
    if (runtime->dimension == 1)
        return runtime->dragon.initialized
            && !runtime->dragon.state.death_processed
            ? GM_MUSIC_END_BOSS : GM_MUSIC_END;
    return runtime->tape_game_mode == GM_MODE_CREATIVE
        ? GM_MUSIC_CREATIVE : GM_MUSIC_GAME;
}

static void update_music(
        GmAudioImpl *impl, int requested, int64_t client_tick) {
    if (impl->last_music_tick == INT64_MIN)
        impl->last_music_tick = client_tick - 1;
    while (impl->last_music_tick < client_tick) {
        int stop_type, play_type;
        ++impl->last_music_tick;
        gm_music_ticker_update(
            &impl->music_ticker, requested, impl->music.active,
            &stop_type, &play_type);
        if (stop_type >= 0)
            stop_record(&impl->music);
        if (play_type >= 0)
            (void)start_music(impl, play_type);
    }
}

static int cave_sample_position(
        GmCaveAmbience *ambience, int chunk_x, int chunk_z,
        int *x, int *y, int *z) {
    uint32_t sample;
    if (!ambience || ambience->ambience_ticks != 0) return 0;
    ambience->update_lcg = ambience->update_lcg * 3u + 1013904223u;
    sample = ambience->update_lcg >> 2;
    *x = chunk_x + (int)(sample & 15u);
    *z = chunk_z + (int)((sample >> 8) & 15u);
    *y = (int)((sample >> 16) & 255u);
    return 1;
}

static int cave_accept(
        GmCaveAmbience *ambience, int x, int y, int z,
        int air, int total_light, int sky_light,
        int player_present, double player_distance_sq,
        GmCaveSound *sound) {
    if (!ambience || !air
            || total_light > jrand_int_bound(&ambience->random, 8)
            || sky_light > 0 || !player_present
            || player_distance_sq <= 4.0)
        return 0;
    if (sound) {
        sound->x = x; sound->y = y; sound->z = z;
        sound->volume = 0.7F;
        sound->pitch = 0.8F + jrand_float(&ambience->random) * 0.2F;
    } else {
        (void)jrand_float(&ambience->random);
    }
    ambience->ambience_ticks = 6000
        + jrand_int_bound(&ambience->random, 12000);
    return 1;
}

static void play_cave_sound(GmAudioImpl *impl, const GmCaveSound *cave) {
    int variant = choose_node(impl, gm_special_sound_asset_roots[0]);
    ALuint source;
    float dx, dy, dz, distance, attenuation;
    if (!impl || !cave || variant < 0 || !impl->buffers[variant]) return;
    source = acquire_source(impl);
    dx = (float)((double)cave->x + 0.5 - impl->listener_x);
    dy = (float)((double)cave->y + 0.5 - impl->listener_y);
    dz = (float)((double)cave->z + 0.5 - impl->listener_z);
    distance = sqrtf(dx * dx + dy * dy + dz * dz);
    attenuation = distance >= 16.0F ? 0.0F : 1.0F - distance / 16.0F;
    alSourcei(source, AL_BUFFER, (ALint)impl->buffers[variant]);
    set_source_mix(impl, source, GM_SOUND_CATEGORY_AMBIENT,
        cave->volume * gm_sound_asset_variants[variant].volume, attenuation);
    alSourcef(source, AL_PITCH, clamp_pitch(
        cave->pitch * gm_sound_asset_variants[variant].pitch));
    alSourcei(source, AL_SOURCE_RELATIVE, AL_FALSE);
    alSource3f(source, AL_POSITION,
        cave->x + 0.5F, cave->y + 0.5F, cave->z + 0.5F);
    alSourcef(source, AL_ROLLOFF_FACTOR, 0.0F);
    alSourcePlay(source);
}

static int update_cave(
        GmAudioImpl *impl, const GmRuntime *runtime, int64_t client_tick,
        int *probes) {
    int played = 0;
    if (!impl || !runtime || !runtime->world) return 0;
    if (impl->last_cave_tick == INT64_MIN)
        impl->last_cave_tick = client_tick - 1;
    while (impl->last_cave_tick < client_tick) {
        int count = runtime->loaded_chunks_authoritative
            ? runtime->loaded_chunks_cap : 1;
        ++impl->last_cave_tick;
        gm_cave_ambience_update(&impl->cave_ambience);
        for (int checked = 0; checked < 10 && checked < count; ++checked) {
            int index = count > 0 ? impl->cave_chunk_cursor++ % count : 0;
            int cx, cz, sx, sy, sz;
            if (runtime->loaded_chunks_authoritative) {
                const GmRuntimeLoadedChunk *chunk = &runtime->loaded_chunks[index];
                if (!chunk->present) continue;
                cx = chunk->chunk_x * 16;
                cz = chunk->chunk_z * 16;
            } else {
                cx = ((int)floor(runtime->server_player.ent.posX
                    + (double)runtime->ox) >> 4) * 16;
                cz = ((int)floor(runtime->server_player.ent.posZ
                    + (double)runtime->oz) >> 4) * 16;
            }
            if (cave_sample_position(
                    &impl->cave_ambience, cx, cz, &sx, &sy, &sz)) {
                if (probes) ++*probes;
                double dx = (double)sx + 0.5
                    - (runtime->server_player.ent.posX + (double)runtime->ox);
                double dy = (double)sy + 0.5
                    - runtime->server_player.ent.posY;
                double dz = (double)sz + 0.5
                    - (runtime->server_player.ent.posZ + (double)runtime->oz);
                GmCaveSound cave;
                int block = gm_world_block(runtime->world, sx, sy, sz);
                int sky = gm_world_sky_light(runtime->world, sx, sy, sz);
                int total = sky;
                int block_light = gm_world_block_light(
                    runtime->world, sx, sy, sz);
                if (block_light > total) total = block_light;
                if (cave_accept(&impl->cave_ambience, sx, sy, sz,
                        block == 0, total, sky, 1,
                        dx * dx + dy * dy + dz * dz, &cave)) {
                    play_cave_sound(impl, &cave);
                    ++played;
                }
            }
        }
    }
    return played;
}

static void play_event(
        GmAudioImpl *impl, const GmRuntimeSoundEvent *event) {
    if (event->sound == GM_SOUND_RECORD_STOP) {
        stop_record_at(impl, event);
        return;
    }
    if (event->sound >= GM_SOUND_RECORD_13
            && event->sound <= GM_SOUND_RECORD_WAIT) {
        start_record(impl, event);
        return;
    }
    int variant = choose_variant(impl, event);
    ALuint source;
    float gain, pitch;
    if (variant < 0 || !impl->buffers[variant]) return;
    source = acquire_source(impl);
    gain = event->volume * gm_sound_asset_variants[variant].volume;
    pitch = event->pitch * gm_sound_asset_variants[variant].pitch;
    /* SoundManager clamps the category-scaled PositionedSound values before
     * Paulscode applies its linear 1-distance/range attenuation.  All category
     * sliders are currently one in the supported canonical profile. */
    if (gain < 0.0F) gain = 0.0F;
    if (gain > 1.0F) gain = 1.0F;
    if (pitch < 0.5F) pitch = 0.5F;
    if (pitch > 2.0F) pitch = 2.0F;
    alSourcei(source, AL_BUFFER, (ALint)impl->buffers[variant]);
    set_source_mix(impl, source, event->category, gain, 1.0F);
    alSourcef(source, AL_PITCH, pitch);
    alSourcei(source, AL_SOURCE_RELATIVE, event->relative ? AL_TRUE : AL_FALSE);
    if (event->relative) {
        alSource3f(source, AL_POSITION, 0.0F, 0.0F, 0.0F);
        alSourcef(source, AL_ROLLOFF_FACTOR, 0.0F);
    } else {
        alSource3f(source, AL_POSITION,
                   (float)event->x, (float)event->y, (float)event->z);
        {
            float raw_volume = event->volume
                * gm_sound_asset_variants[variant].volume;
            float range = 16.0F * (raw_volume > 1.0F ? raw_volume : 1.0F);
            double dx = event->x - impl->listener_x;
            double dy = event->y - impl->listener_y;
            double dz = event->z - impl->listener_z;
            float distance = (float)sqrt(dx * dx + dy * dy + dz * dz);
            float attenuation = distance <= 0.0F ? 1.0F
                : distance >= range ? 0.0F : 1.0F - distance / range;
            set_source_mix(impl, source, event->category, gain, attenuation);
        }
        alSourcef(source, AL_ROLLOFF_FACTOR, 0.0F);
    }
    alSourcePlay(source);
}

static int queue_delayed(
        GmAudioImpl *impl, const GmRuntimeSoundEvent *event,
        int64_t tick) {
    for (int i = 0; i < GM_AUDIO_DELAYED; ++i) {
        GmAudioDelayed *delayed = &impl->delayed[i];
        if (delayed->active) continue;
        delayed->active = 1;
        delayed->due_tick = tick + event->delay_ticks;
        delayed->event = *event;
        ++impl->delayed_count;
        return 1;
    }
    return 0;
}

static void play_due_delayed(
        GmAudioImpl *impl, int64_t tick, int dimension) {
    if (impl->delayed_count == 0) return;
    for (int i = 0; i < GM_AUDIO_DELAYED; ++i) {
        GmAudioDelayed *delayed = &impl->delayed[i];
        if (!delayed->active || delayed->due_tick > tick) continue;
        if (delayed->event.dimension == dimension)
            play_event(impl, &delayed->event);
        delayed->active = 0;
        --impl->delayed_count;
    }
}
#endif

#ifndef MAGMA_AUDIO_OPENAL
int gm_audio_live_selector_fixture(
        uint64_t base_seed, int sound, int draws,
        int *variants, int capacity) {
    (void)base_seed; (void)sound; (void)draws;
    (void)variants; (void)capacity;
    return 0;
}

int gm_audio_live_source_fixture(
        uint64_t base_seed, int sound, float event_volume, float event_pitch,
        float category_volume, GmAudioSourceDescriptor *out) {
    (void)base_seed; (void)sound; (void)event_volume; (void)event_pitch;
    (void)category_volume; (void)out;
    return 0;
}
#endif

int gm_audio_live_init(GmAudioLive *audio, char *err, int err_cap) {
    const char *setting;
    if (!audio) return 0;
    memset(audio, 0, sizeof *audio);
    setting = getenv("MAGMA_AUDIO");
    if (setting && !strcmp(setting, "0")) return 1;
#ifndef MAGMA_AUDIO_OPENAL
    audio_error(err, err_cap, "OpenAL/Vorbis support was not available at build time");
    return 0;
#else
    GmAudioImpl *impl = (GmAudioImpl *)calloc(1, sizeof *impl);
    if (!impl) { audio_error(err, err_cap, "audio allocation failed"); return 0; }
    {
        uint64_t seed = selector_seed_base();
        for (int category = 0; category < 10; ++category) {
            impl->category_volume[category] = 1.0F;
            audio->category_volume[category] = 1.0F;
        }
        for (int source = 0; source < GM_AUDIO_SOURCES; ++source) {
            impl->source_category[source] = -1;
            impl->source_attenuation[source] = 1.0F;
        }
        for (int i = 0; i < GM_SOUND_ASSET_NODE_COUNT; ++i)
            jrand_set(&impl->selector_random[i],
                      (i64)(seed ^ gm_sound_asset_seed_keys[i]));
    }
    if (!find_objects(impl->objects, (int)sizeof impl->objects)) {
        audio_error(err, err_cap, "Minecraft asset objects not found");
        free(impl); return 0;
    }
    impl->loopback = getenv("MAGMA_AUDIO_LOOPBACK")
        && !strcmp(getenv("MAGMA_AUDIO_LOOPBACK"), "1");
    impl->device = impl->loopback
        ? alcLoopbackOpenDeviceSOFT(NULL) : alcOpenDevice(NULL);
    if (!impl->device) {
        audio_error(err, err_cap, "OpenAL device unavailable");
        free(impl); return 0;
    }
    {
        const ALCint loopback_attributes[] = {
            ALC_FORMAT_CHANNELS_SOFT, ALC_STEREO_SOFT,
            ALC_FORMAT_TYPE_SOFT, ALC_SHORT_SOFT,
            ALC_FREQUENCY, 48000, 0
        };
        impl->context = alcCreateContext(
            impl->device, impl->loopback ? loopback_attributes : NULL);
    }
    if (!impl->context || !alcMakeContextCurrent(impl->context)) {
        audio_error(err, err_cap, "OpenAL context unavailable");
        if (impl->context) alcDestroyContext(impl->context);
        alcCloseDevice(impl->device); free(impl); return 0;
    }
    alGenBuffers(GM_SOUND_ASSET_VARIANT_COUNT, impl->buffers);
    alGenSources(GM_AUDIO_SOURCES, impl->sources);
    for (int i = 0; i < GM_AUDIO_RECORD_STREAMS; ++i) {
        impl->records[i].source =
            impl->sources[
                GM_AUDIO_SOURCES - GM_AUDIO_RECORD_STREAMS - 1 + i];
        alGenBuffers(
            GM_AUDIO_RECORD_BUFFERS, impl->records[i].buffers);
    }
    impl->music.source = impl->sources[GM_AUDIO_SOURCES - 1];
    alGenBuffers(GM_AUDIO_RECORD_BUFFERS, impl->music.buffers);
    gm_music_ticker_init(&impl->music_ticker,
        (long long)(selector_seed_base() ^ UINT64_C(0x4d55534943544943)));
    impl->last_music_tick = INT64_MIN;
    gm_cave_ambience_init(&impl->cave_ambience,
        (long long)(selector_seed_base() ^ UINT64_C(0x434156454d4f4f44)),
        0, 0);
    impl->last_cave_tick = INT64_MIN;
    if (alGetError() != AL_NO_ERROR) {
        audio_error(err, err_cap, "OpenAL buffer/source allocation failed");
        gm_audio_live_destroy(&(GmAudioLive){ .impl = impl, .enabled = 1 });
        return 0;
    }
    for (int i = 0; i < GM_SOUND_ASSET_VARIANT_COUNT; ++i) {
        if (gm_sound_asset_variants[i].stream) continue;
        if (decode_buffer(
                impl->objects, gm_sound_asset_variants[i].hash,
                impl->buffers[i])) continue;
        audio_error(err, err_cap, "failed to decode a Minecraft sound asset");
        gm_audio_live_destroy(&(GmAudioLive){ .impl = impl, .enabled = 1 });
        return 0;
    }
    /* Paulscode's LINEAR model computes gain on the CPU and disables OpenAL
     * rolloff; retain that contract instead of applying a second curve. */
    alDistanceModel(AL_NONE);
    audio->impl = impl;
    audio->enabled = 1;
    return 1;
#endif
}

void gm_audio_live_update(
        GmAudioLive *audio, const GmRuntime *runtime,
        double x, double y, double z, float yaw, float pitch) {
    gm_audio_live_update_at(
        audio, runtime, -1, runtime ? runtime->tick : 0,
        x, y, z, yaw, pitch);
}

void gm_audio_live_update_at(
        GmAudioLive *audio, const GmRuntime *runtime,
        int requested_music_type, int64_t client_tick,
        double x, double y, double z, float yaw, float pitch) {
    if (!audio || !audio->enabled || !audio->impl) return;
#ifdef MAGMA_AUDIO_OPENAL
    GmAudioImpl *impl = (GmAudioImpl *)audio->impl;
    float yr = yaw * 0.01745329251994329577F;
    float pr = pitch * 0.01745329251994329577F;
    float forward[3] = {
        -sinf(yr) * cosf(pr), -sinf(pr), cosf(yr) * cosf(pr)
    };
    float right[3] = { -forward[2], 0.0F, forward[0] };
    float right_len = sqrtf(right[0] * right[0] + right[2] * right[2]);
    float orientation[6];
    if (right_len > 0.0001F) {
        right[0] /= right_len; right[2] /= right_len;
    } else { right[0] = -1.0F; right[2] = 0.0F; }
    orientation[0] = forward[0];
    orientation[1] = forward[1];
    orientation[2] = forward[2];
    orientation[3] = -right[2] * forward[1];
    orientation[4] = right[2] * forward[0] - right[0] * forward[2];
    orientation[5] = right[0] * forward[1];
    alListener3f(AL_POSITION, (float)x, (float)y, (float)z);
    alListenerfv(AL_ORIENTATION, orientation);
    impl->listener_x = x;
    impl->listener_y = y;
    impl->listener_z = z;
    update_records(impl, runtime ? runtime->dimension : INT_MIN);
    if (requested_music_type < 0)
        requested_music_type = gm_audio_requested_music_type(runtime);
    update_music(impl, requested_music_type, client_tick);
    audio->cave_sounds_played += update_cave(
        impl, runtime, client_tick, &audio->cave_probes);
    if (runtime)
        play_due_delayed(impl, runtime->tick, runtime->dimension);
    for (int i = 0; runtime && i < gm_runtime_sound_event_count(runtime); ++i) {
        GmRuntimeSoundEvent event;
        if (!gm_runtime_sound_event_get(runtime, i, &event)
                || event.seq < audio->next_seq) continue;
        if (event.seq > audio->next_seq)
            audio->dropped += event.seq - audio->next_seq;
        audio->next_seq = event.seq + 1;
        if (event.dimension != runtime->dimension) continue;
        if (event.delay_ticks > 0) {
            if (!queue_delayed(impl, &event, runtime->tick))
                ++audio->dropped;
        } else {
            play_event(impl, &event);
        }
    }
    audio->active_records = active_record_count(impl);
    audio->pending_delayed = impl->delayed_count;
    audio->active_music_type = impl->music.active
        ? impl->music.dimension : -1;
#else
    (void)requested_music_type; (void)client_tick;
    (void)x; (void)y; (void)z; (void)yaw; (void)pitch;
#endif
}

void gm_audio_live_set_category_volume(
        GmAudioLive *audio, int category, float volume) {
    if (!audio || category < 0 || category >= 10) return;
    volume = clamp01(volume);
    audio->category_volume[category] = volume;
#ifdef MAGMA_AUDIO_OPENAL
    if (audio->enabled && audio->impl) {
        GmAudioImpl *impl = (GmAudioImpl *)audio->impl;
        impl->category_volume[category] = volume;
        if (category == GM_SOUND_CATEGORY_MASTER) {
            alListenerf(AL_GAIN, volume);
        } else {
            for (int index = 0; index < GM_AUDIO_SOURCES; ++index) {
                if (impl->source_category[index] != category) continue;
                alSourcef(impl->sources[index], AL_GAIN,
                    clamp01(impl->source_raw_gain[index] * volume)
                    * impl->source_attenuation[index]);
            }
        }
    }
#endif
}

int gm_audio_live_render_samples(
        GmAudioLive *audio, int16_t *stereo_pcm, int frames) {
#ifdef MAGMA_AUDIO_OPENAL
    if (audio && audio->enabled && audio->impl && stereo_pcm && frames > 0) {
        GmAudioImpl *impl = (GmAudioImpl *)audio->impl;
        if (impl->loopback) {
            alcRenderSamplesSOFT(impl->device, stereo_pcm, frames);
            return 1;
        }
    }
#else
    (void)audio; (void)stereo_pcm; (void)frames;
#endif
    return 0;
}

void gm_audio_live_destroy(GmAudioLive *audio) {
    if (!audio) return;
#ifdef MAGMA_AUDIO_OPENAL
    if (audio->impl) {
        GmAudioImpl *impl = (GmAudioImpl *)audio->impl;
        if (impl->context) alcMakeContextCurrent(impl->context);
        for (int i = 0; i < GM_AUDIO_RECORD_STREAMS; ++i) {
            stop_record(&impl->records[i]);
            alDeleteBuffers(
                GM_AUDIO_RECORD_BUFFERS, impl->records[i].buffers);
        }
        stop_record(&impl->music);
        alDeleteBuffers(GM_AUDIO_RECORD_BUFFERS, impl->music.buffers);
        alDeleteSources(GM_AUDIO_SOURCES, impl->sources);
        alDeleteBuffers(GM_SOUND_ASSET_VARIANT_COUNT, impl->buffers);
        if (impl->context) {
            alcMakeContextCurrent(NULL);
            alcDestroyContext(impl->context);
        }
        if (impl->device) alcCloseDevice(impl->device);
        free(impl);
    }
#endif
    memset(audio, 0, sizeof *audio);
}
