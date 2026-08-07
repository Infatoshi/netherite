#include "game/audio_live.h"

#ifdef MAGMA_AUDIO_OPENAL
#include "assets/sound_manifest.h"
#include <AL/al.h>
#include <AL/alc.h>
#include <vorbis/vorbisfile.h>
#endif

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GM_AUDIO_SOURCES 32
#define GM_AUDIO_RECORD_STREAMS 4
#define GM_AUDIO_RECORD_BUFFERS 4
#define GM_AUDIO_RECORD_PCM_BYTES 65536
#define GM_AUDIO_DELAYED 256

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
    ALuint buffers[GM_SOUND_ASSET_VARIANT_COUNT];
    ALuint sources[GM_AUDIO_SOURCES];
    GmAudioRecord records[GM_AUDIO_RECORD_STREAMS];
    GmAudioDelayed delayed[GM_AUDIO_DELAYED];
    int delayed_count;
    unsigned int source_cursor;
    uint64_t record_serial;
    char objects[512];
} GmAudioImpl;

static int directory_exists(const char *path) {
    struct stat value;
    return path && stat(path, &value) == 0 && S_ISDIR(value.st_mode);
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

static int choose_variant(const GmRuntimeSoundEvent *event) {
    const GmSoundAssetSpan *span = &gm_sound_asset_spans[event->sound];
    uint64_t value = event->seq + UINT64_C(0x9e3779b97f4a7c15)
        + (uint64_t)(unsigned)event->sound * UINT64_C(0xbf58476d1ce4e5b9);
    int roll;
    value ^= value >> 30; value *= UINT64_C(0xbf58476d1ce4e5b9);
    value ^= value >> 27; value *= UINT64_C(0x94d049bb133111eb);
    value ^= value >> 31;
    if (span->count <= 0 || span->total_weight <= 0) return -1;
    roll = (int)(value % (uint64_t)span->total_weight);
    for (int i = 0; i < span->count; ++i) {
        int variant = span->start + i;
        if (roll < gm_sound_asset_variants[variant].weight) return variant;
        roll -= gm_sound_asset_variants[variant].weight;
    }
    return span->start;
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
    int variant = choose_variant(event);
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
    alSourcef(record->source, AL_GAIN,
        event->volume * gm_sound_asset_variants[variant].volume);
    alSourcef(record->source, AL_PITCH,
        event->pitch * gm_sound_asset_variants[variant].pitch);
    alSourcei(record->source, AL_SOURCE_RELATIVE, AL_FALSE);
    alSource3f(record->source, AL_POSITION,
        (float)event->x, (float)event->y, (float)event->z);
    alSourcef(record->source, AL_REFERENCE_DISTANCE, 16.0F);
    alSourcef(record->source, AL_MAX_DISTANCE, 64.0F);
    alSourcef(record->source, AL_ROLLOFF_FACTOR, 1.0F);
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
}

static int active_record_count(const GmAudioImpl *impl) {
    int count = 0;
    for (int i = 0; i < GM_AUDIO_RECORD_STREAMS; ++i)
        count += impl->records[i].active != 0;
    return count;
}

static ALuint acquire_source(GmAudioImpl *impl) {
    const unsigned int ordinary =
        GM_AUDIO_SOURCES - GM_AUDIO_RECORD_STREAMS;
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
    int variant = choose_variant(event);
    ALuint source;
    float gain, pitch;
    if (variant < 0 || !impl->buffers[variant]) return;
    source = acquire_source(impl);
    gain = event->volume * gm_sound_asset_variants[variant].volume;
    pitch = event->pitch * gm_sound_asset_variants[variant].pitch;
    if (gain < 0.0F) gain = 0.0F;
    if (pitch < 0.01F) pitch = 0.01F;
    if (pitch > 4.0F) pitch = 4.0F;
    alSourcei(source, AL_BUFFER, (ALint)impl->buffers[variant]);
    alSourcef(source, AL_GAIN, gain);
    alSourcef(source, AL_PITCH, pitch);
    alSourcei(source, AL_SOURCE_RELATIVE, event->relative ? AL_TRUE : AL_FALSE);
    if (event->relative) {
        alSource3f(source, AL_POSITION, 0.0F, 0.0F, 0.0F);
        alSourcef(source, AL_ROLLOFF_FACTOR, 0.0F);
    } else {
        alSource3f(source, AL_POSITION,
                   (float)event->x, (float)event->y, (float)event->z);
        alSourcef(source, AL_REFERENCE_DISTANCE, 16.0F);
        alSourcef(source, AL_MAX_DISTANCE,
                  16.0F * (event->volume > 1.0F ? event->volume : 1.0F));
        alSourcef(source, AL_ROLLOFF_FACTOR, 1.0F);
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
    if (!find_objects(impl->objects, (int)sizeof impl->objects)) {
        audio_error(err, err_cap, "Minecraft asset objects not found");
        free(impl); return 0;
    }
    impl->device = alcOpenDevice(NULL);
    if (!impl->device) {
        audio_error(err, err_cap, "OpenAL device unavailable");
        free(impl); return 0;
    }
    impl->context = alcCreateContext(impl->device, NULL);
    if (!impl->context || !alcMakeContextCurrent(impl->context)) {
        audio_error(err, err_cap, "OpenAL context unavailable");
        if (impl->context) alcDestroyContext(impl->context);
        alcCloseDevice(impl->device); free(impl); return 0;
    }
    alGenBuffers(GM_SOUND_ASSET_VARIANT_COUNT, impl->buffers);
    alGenSources(GM_AUDIO_SOURCES, impl->sources);
    for (int i = 0; i < GM_AUDIO_RECORD_STREAMS; ++i) {
        impl->records[i].source =
            impl->sources[GM_AUDIO_SOURCES - GM_AUDIO_RECORD_STREAMS + i];
        alGenBuffers(
            GM_AUDIO_RECORD_BUFFERS, impl->records[i].buffers);
    }
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
    alDistanceModel(AL_INVERSE_DISTANCE_CLAMPED);
    audio->impl = impl;
    audio->enabled = 1;
    return 1;
#endif
}

void gm_audio_live_update(
        GmAudioLive *audio, const GmRuntime *runtime,
        double x, double y, double z, float yaw, float pitch) {
    if (!audio || !audio->enabled || !audio->impl || !runtime) return;
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
    update_records(impl, runtime->dimension);
    play_due_delayed(impl, runtime->tick, runtime->dimension);
    for (int i = 0; i < gm_runtime_sound_event_count(runtime); ++i) {
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
#else
    (void)x; (void)y; (void)z; (void)yaw; (void)pitch;
#endif
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
