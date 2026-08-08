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
} GmAudioLive;

/* Interactive-only consumer. Failure leaves audio disabled and does not
 * affect simulation startup. Set MAGMA_AUDIO=0 to skip device/resource work. */
int gm_audio_live_init(GmAudioLive *audio, char *err, int err_cap);
void gm_audio_live_update(
    GmAudioLive *audio, const GmRuntime *runtime,
    double x, double y, double z, float yaw, float pitch);
void gm_audio_live_destroy(GmAudioLive *audio);

#endif
