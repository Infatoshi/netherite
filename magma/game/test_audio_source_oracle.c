#include "game/audio_live.h"
#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static uint32_t bits(float value) {
    uint32_t out;
    memcpy(&out, &value, sizeof out);
    return out;
}

int main(void) {
    static const float volume[] = {-0.5F, 0.25F, 1.0F, 4.0F};
    static const float pitch[] = {0.1F, 1.0F, 3.0F, 0.75F};
    static const float category[] = {1.0F, 0.25F, 1.0F, 0.0F};
    const uint64_t seed = UINT64_C(0x4e65746865726974);
    for (int sound = 1; sound < GM_SOUND_COUNT; ++sound) {
        for (int row = 0; row < 4; ++row) {
            GmAudioSourceDescriptor out;
            if (!gm_audio_live_source_fixture(
                    seed, sound, volume[row], pitch[row], category[row],
                    &out)) return 1;
            printf("%d %d %d %08x %08x %08x %d\n",
                   sound, row, out.variant, bits(out.gain), bits(out.pitch),
                   bits(out.range), out.stream);
        }
    }
    return 0;
}
