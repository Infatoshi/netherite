#include "game/runtime.h"

#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static float float_from_bits(uint32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

int main(int argc, char **argv) {
    uint32_t current_bits, previous_bits;
    float current, after;
    int in_portal, nausea_duration, remove;
    if (argc != 6) return 2;
    current_bits = (uint32_t)strtoul(argv[1], NULL, 16);
    previous_bits = (uint32_t)strtoul(argv[2], NULL, 16);
    current = float_from_bits(current_bits);
    in_portal = atoi(argv[3]);
    nausea_duration = atoi(argv[4]);
    remove = !strcmp(argv[5], "remove");
    if ((in_portal != 0 && in_portal != 1)
            || nausea_duration < 0 || nausea_duration > 1000000
            || (strcmp(argv[5], "step") && !remove))
        return 2;
    after = remove ? 0.0F : gm_runtime_client_portal_step(
        current, in_portal, nausea_duration);
    printf("{\"ok\":true,\"before_bits\":\"%08" PRIx32
           "\",\"prev_before_bits\":\"%08" PRIx32
           "\",\"after_bits\":\"%08" PRIx32
           "\",\"prev_after_bits\":\"%08" PRIx32
           "\",\"in_portal_after\":%s"
           ",\"nausea_active_after\":%s}\n",
           current_bits, previous_bits, float_bits(after),
           remove ? float_bits(0.0F) : current_bits,
           !remove && in_portal ? "false"
               : in_portal ? "true" : "false",
           !remove && nausea_duration > 0 ? "true" : "false");
    return 0;
}
