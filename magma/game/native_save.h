#ifndef MAGMA_GAME_NATIVE_SAVE_H
#define MAGMA_GAME_NATIVE_SAVE_H

#include "game/config.h"
#include "game/runtime.h"

#include <stddef.h>

#define GM_NATIVE_SAVE_SLOT_MAX 64

typedef struct GmNativeSaveInfo {
    char slot[GM_NATIVE_SAVE_SLOT_MAX + 1];
    unsigned long long generation;
    long long seed;
    long long tick;
    unsigned world_mask;
    GmWorldType world_type;
} GmNativeSaveInfo;

/* Generation directories are immutable. `current` is replaced last, so a
 * crash exposes either the complete old generation or the complete new one. */
int gm_native_save_write(
    GmRuntime *runtime, const char *root, const char *slot,
    char *error, size_t error_cap);
int gm_native_save_load(
    GmRuntime *runtime, const GmConfig *template_config,
    const char *root, const char *slot,
    char *error, size_t error_cap);
int gm_native_save_info(
    const char *root, const char *slot, GmNativeSaveInfo *out,
    char *error, size_t error_cap);
int gm_native_save_list(
    const char *root, GmNativeSaveInfo *out, int max,
    char *error, size_t error_cap);

#endif
