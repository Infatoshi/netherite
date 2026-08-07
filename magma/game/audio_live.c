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

static void audio_error(char *err, int cap, const char *message) {
    if (err && cap > 0) snprintf(err, (size_t)cap, "%s", message);
}

#ifdef MAGMA_AUDIO_OPENAL
typedef struct {
    ALCdevice *device;
    ALCcontext *context;
    ALuint buffers[GM_SOUND_ASSET_VARIANT_COUNT];
    ALuint sources[GM_AUDIO_SOURCES];
    unsigned int source_cursor;
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

static ALuint acquire_source(GmAudioImpl *impl) {
    for (int i = 0; i < GM_AUDIO_SOURCES; ++i) {
        unsigned int index = (impl->source_cursor + (unsigned)i)
            % GM_AUDIO_SOURCES;
        ALint state = 0;
        alGetSourcei(impl->sources[index], AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            impl->source_cursor = (index + 1u) % GM_AUDIO_SOURCES;
            return impl->sources[index];
        }
    }
    {
        unsigned int index = impl->source_cursor++ % GM_AUDIO_SOURCES;
        alSourceStop(impl->sources[index]);
        return impl->sources[index];
    }
}

static void play_event(
        GmAudioImpl *impl, const GmRuntimeSoundEvent *event) {
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
    if (alGetError() != AL_NO_ERROR) {
        audio_error(err, err_cap, "OpenAL buffer/source allocation failed");
        gm_audio_live_destroy(&(GmAudioLive){ .impl = impl, .enabled = 1 });
        return 0;
    }
    for (int i = 0; i < GM_SOUND_ASSET_VARIANT_COUNT; ++i) {
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
    for (int i = 0; i < gm_runtime_sound_event_count(runtime); ++i) {
        GmRuntimeSoundEvent event;
        if (!gm_runtime_sound_event_get(runtime, i, &event)
                || event.seq < audio->next_seq) continue;
        if (event.seq > audio->next_seq)
            audio->dropped += event.seq - audio->next_seq;
        audio->next_seq = event.seq + 1;
        if (event.dimension == runtime->dimension) play_event(impl, &event);
    }
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
