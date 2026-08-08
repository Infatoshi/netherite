#include "game/runtime.h"

#include <stdio.h>
#include <string.h>
#include <unistd.h>

static void tag_for_index(unsigned char tag[12], int index) {
    static const unsigned char prefix[7] = {
        10, 0, 0, 3, 0, 1, 'v'
    };
    memcpy(tag, prefix, sizeof prefix);
    tag[7] = (unsigned char)((unsigned int)index >> 24);
    tag[8] = (unsigned char)((unsigned int)index >> 16);
    tag[9] = (unsigned char)((unsigned int)index >> 8);
    tag[10] = (unsigned char)index;
    tag[11] = 0;
}

int main(void) {
    static GmRuntime runtime, restored;
    static char long_name[GM_RUNTIME_ITEM_NAME_LENGTH_MAX + 2u];
    const char *checkpoint = "../.tmp/stack_tag_capacity.bin";
    unsigned char tag[12];
    GmConfig config;
    char error[256];
    int runtime_ready = 0, restored_ready = 0;

    gm_config_defaults(&config);
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL: initialize stack-tag source: %s\n", error);
        goto fail;
    }
    runtime_ready = 1;
    if (!gm_runtime_init(&restored, &config, error, sizeof error)) {
        fprintf(stderr, "FAIL: initialize stack-tag destination: %s\n", error);
        goto fail;
    }
    restored_ready = 1;

    for (int index = 0; index <= GM_RUNTIME_ITEM_NAMES; ++index) {
        char name[64];
        snprintf(name, sizeof name, "capacity_name_%03d", index);
        if (gm_runtime_item_name_intern(&runtime, name) != index + 1) {
            fprintf(stderr, "FAIL: custom item name %d was truncated\n", index);
            goto fail;
        }
    }
    if (runtime.item_name_count != GM_RUNTIME_ITEM_NAMES + 1
            || runtime.item_names_cap <= GM_RUNTIME_ITEM_NAMES) {
        fprintf(stderr, "FAIL: item-name store did not grow past 64\n");
        goto fail;
    }
    memset(long_name, 'n', GM_RUNTIME_ITEM_NAME_LENGTH_MAX);
    long_name[GM_RUNTIME_ITEM_NAME_LENGTH_MAX] = '\0';
    if (gm_runtime_item_name_intern(&runtime, long_name)
                != GM_RUNTIME_ITEM_NAMES + 2
            || gm_runtime_item_name_intern(&runtime, long_name)
                != GM_RUNTIME_ITEM_NAMES + 2) {
        fprintf(stderr, "FAIL: long custom item name was truncated\n");
        goto fail;
    }
    long_name[GM_RUNTIME_ITEM_NAME_LENGTH_MAX] = 'n';
    long_name[GM_RUNTIME_ITEM_NAME_LENGTH_MAX + 1u] = '\0';
    if (gm_runtime_item_name_intern(&runtime, long_name) != 0) {
        fprintf(stderr, "FAIL: overlong custom item name was accepted\n");
        goto fail;
    }
    long_name[GM_RUNTIME_ITEM_NAME_LENGTH_MAX] = '\0';

    for (int index = 0; index <= GM_RUNTIME_STACK_TAGS_MAX; ++index) {
        tag_for_index(tag, index);
        if (gm_runtime_stack_tag_intern(&runtime, tag, sizeof tag)
                != index + 1) {
            fprintf(stderr, "FAIL: unique stack tag %d was truncated\n", index);
            goto fail;
        }
    }
    if (runtime.stack_tag_count != GM_RUNTIME_STACK_TAGS_MAX + 1
            || runtime.stack_tags_cap <= GM_RUNTIME_STACK_TAGS_MAX) {
        fprintf(stderr, "FAIL: stack-tag store did not grow past 8192\n");
        goto fail;
    }
    tag_for_index(tag, GM_RUNTIME_STACK_TAGS_MAX);
    if (gm_runtime_stack_tag_intern(&runtime, tag, sizeof tag)
            != GM_RUNTIME_STACK_TAGS_MAX + 1) {
        fprintf(stderr, "FAIL: stack-tag dedup changed after growth\n");
        goto fail;
    }
    if (!gm_runtime_write_checkpoint(&runtime, checkpoint)
            || !gm_runtime_load_checkpoint(&restored, checkpoint)
            || restored.stack_tag_count != GM_RUNTIME_STACK_TAGS_MAX + 1
            || restored.stack_tags_cap <= GM_RUNTIME_STACK_TAGS_MAX
            || restored.item_name_count != GM_RUNTIME_ITEM_NAMES + 2
            || restored.item_names_cap <= GM_RUNTIME_ITEM_NAMES
            || !gm_runtime_item_name(
                &restored, GM_RUNTIME_ITEM_NAMES + 1)
            || strcmp(gm_runtime_item_name(
                          &restored, GM_RUNTIME_ITEM_NAMES + 1),
                      "capacity_name_064") != 0) {
        fprintf(stderr, "FAIL: grown stack-tag store did not continue\n");
        goto fail;
    }
    if (!gm_runtime_item_name(&restored, GM_RUNTIME_ITEM_NAMES + 2)
            || strcmp(gm_runtime_item_name(
                          &restored, GM_RUNTIME_ITEM_NAMES + 2),
                      long_name) != 0
            || gm_runtime_item_name_intern(&restored, long_name)
                != GM_RUNTIME_ITEM_NAMES + 2) {
        fprintf(stderr, "FAIL: long custom item name did not continue\n");
        goto fail;
    }
    tag_for_index(tag, GM_RUNTIME_STACK_TAGS_MAX);
    if (gm_runtime_stack_tag_intern(&restored, tag, sizeof tag)
            != GM_RUNTIME_STACK_TAGS_MAX + 1) {
        fprintf(stderr, "FAIL: restored stack-tag identity changed\n");
        goto fail;
    }

    unlink(checkpoint);
    gm_runtime_destroy(&restored);
    gm_runtime_destroy(&runtime);
    puts("stack_tag_capacity: PASS");
    return 0;

fail:
    unlink(checkpoint);
    if (restored_ready) gm_runtime_destroy(&restored);
    if (runtime_ready) gm_runtime_destroy(&runtime);
    return 1;
}
