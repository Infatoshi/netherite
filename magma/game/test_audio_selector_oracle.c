#include "game/audio_live.h"
#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>

int main(void) {
    const uint64_t seed = UINT64_C(0x4e65746865726974);
    int variants[16];
    for (int sound = 1; sound < GM_SOUND_COUNT; ++sound) {
        if (!gm_audio_live_selector_fixture(
                seed, sound, 16, variants, 16)) {
            fprintf(stderr, "selector failed for sound %d\n", sound);
            return 1;
        }
        for (int draw = 0; draw < 16; ++draw)
            printf("%d %d %d\n", sound, draw, variants[draw]);
    }
    return 0;
}
