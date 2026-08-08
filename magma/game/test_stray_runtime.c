#include "game/native_save.h"
#include "game/runtime.h"
#include "tile_entity_brewing.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static void clean_save(const char *root) {
    char path[512];
    static const char *files[] = {
        "runtime.bin", "player_statistics.json", "manifest.bin",
        "world_dim-1.bin", "world_dim0.bin", "world_dim1.bin",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
        snprintf(path, sizeof path,
                 "%s/stray/generation-0000000000000001/%s", root, files[i]);
        (void)remove(path);
    }
    snprintf(path, sizeof path, "%s/stray/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/stray/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/stray/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/stray", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

static int active_arrow(const GmRuntime *r) {
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (r->projectiles[i].active && r->projectiles[i].type == 2)
            return i;
    return -1;
}

static int slowness_duration(const GmRuntime *r) {
    for (int i = 0; i < r->potion_count; ++i)
        if (r->potions[i].id == 2) return r->potions[i].duration;
    return 0;
}

static GmLiveEnt *find_item(GmRuntime *r, int eid) {
    for (int index = 0; index < GM_LIVE_MAX; ++index)
        if (r->entities.ents[index].active
                && r->entities.ents[index].eid == eid)
            return &r->entities.ents[index];
    return NULL;
}

static int has_stray_loot_tag(const GmRuntime *r, const GmLiveEnt *item) {
    static const unsigned char expected[] = {
        10, 0, 0, 8, 0, 6, 'P', 'o', 't', 'i', 'o', 'n',
        0, 18, 'm', 'i', 'n', 'e', 'c', 'r', 'a', 'f', 't', ':',
        's', 'l', 'o', 'w', 'n', 'e', 's', 's', 0
    };
    const GmNbtBlob *tag = item ? gm_runtime_stack_tag(r, item->tag_id) : NULL;
    return tag && tag->len == sizeof expected
        && memcmp(tag->data, expected, sizeof expected) == 0;
}

typedef struct {
    int active, age, kind, effect_count, effect_id, effect_duration;
    int player_health, slowness;
    double x, y, z, vx, vy, vz;
} StrayState;

static StrayState snapshot(const GmRuntime *r, int arrow_slot) {
    StrayState out = {0};
    if (arrow_slot >= 0 && r->projectiles[arrow_slot].active) {
        const GmRuntimeProjectile *p = &r->projectiles[arrow_slot];
        out.active = 1;
        out.age = p->age;
        out.kind = p->arrow_kind;
        out.effect_count = p->arrow_effect_count;
        if (p->arrow_effect_count > 0) {
            out.effect_id = p->arrow_effects[0].id;
            out.effect_duration = p->arrow_effects[0].duration;
        }
        out.x = p->x; out.y = p->y; out.z = p->z;
        out.vx = p->vx; out.vy = p->vy; out.vz = p->vz;
    }
    out.player_health = r->vitals.health;
    out.slowness = slowness_duration(r);
    return out;
}

int main(void) {
    static GmRuntime runtime;
    GmConfig config;
    char error[256] = {0};
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    CHECK(gm_runtime_init(&runtime, &config, error, sizeof error), error);
    if (fail) return 1;
    gm_runtime_set_pose(&runtime, 8.5, 4.0, 8.5, 0.0F, 0.0F);
    runtime.vitals.health = runtime.player.health = 20;
    int slot = gm_mobs_spawn(&runtime.mobs, EW_TYPE_STRAY, 8.5, 4.0, 16.5);
    CHECK(slot > 0, "spawn active Stray");
    runtime.mobs_enabled = 1;
    runtime.gamerules.doMobSpawning = 0;
    GmAction idle = {.hotbar_sel = -1};
    gm_runtime_tick(&runtime, idle);
    int arrow = active_arrow(&runtime);
    CHECK(arrow >= 0, "active Stray emits a living-shooter arrow");
    if (arrow >= 0) {
        const GmRuntimeProjectile *p = &runtime.projectiles[arrow];
        CHECK(p->arrow_kind == GM_ARROW_TIPPED
                  && p->arrow_potion_type == TB_PT_EMPTY
                  && p->arrow_effect_count == 1
                  && p->arrow_effects[0].id == 2
                  && p->arrow_effects[0].amplifier == 0
                  && p->arrow_effects[0].duration == 600
                  && p->arrow_effect_flags[0] == 2
                  && p->arrow_pickup_item == 440,
              "Stray arrow carries exact visible Slowness-I 600 payload");
    }

    int loot_eid = runtime.next_entity_id++;
    CHECK(gm_live_spawn_item_exact(&runtime.entities, loot_eid,
              24.5, 4.0, 24.5, 0.0, 0.0, 0.0, 0.0F,
              440, 1, 17, 0, 32767, 1),
          "stage Stray's semantic tipped-arrow loot");
    GmLiveEnt *loot = find_item(&runtime, loot_eid);
    CHECK(loot != NULL, "find staged Stray tipped-arrow loot");
    if (loot) loot->semantic_potion_type = GM_HOSTILE_LOOT_POTION_SLOWNESS;
    gm_runtime_tick(&runtime, idle);
    loot = find_item(&runtime, loot_eid);
    CHECK(has_stray_loot_tag(&runtime, loot),
          "runtime interns Stray loot as canonical slowness potion NBT");

    char save_root[256];
    snprintf(save_root, sizeof save_root, ".tmp/stray-runtime-%ld", (long)getpid());
    clean_save(save_root);
    CHECK(gm_native_save_write(&runtime, save_root, "stray", error, sizeof error),
          "native save records flying Stray arrow");
    for (int i = 0; i < 2; ++i) gm_runtime_tick(&runtime, idle);
    StrayState expected = snapshot(&runtime, arrow);
    CHECK(gm_native_save_load(&runtime, &config, save_root, "stray",
                              error, sizeof error),
          "native save restores flying Stray arrow");
    loot = find_item(&runtime, loot_eid);
    CHECK(has_stray_loot_tag(&runtime, loot),
          "native save restores Stray tipped-arrow loot NBT");
    for (int i = 0; i < 2; ++i) gm_runtime_tick(&runtime, idle);
    StrayState actual = snapshot(&runtime, arrow);
    CHECK(memcmp(&actual, &expected, sizeof actual) == 0,
          "flying Stray arrow continuation is byte-exact across save/reload");

    for (int i = 0; i < 20 && slowness_duration(&runtime) == 0; ++i)
        gm_runtime_tick(&runtime, idle);
    CHECK(runtime.vitals.health < 20 && slowness_duration(&runtime) > 0
              && slowness_duration(&runtime) <= 600,
          "Stray arrow damages player and applies its full custom Slowness payload");

    clean_save(save_root);
    gm_runtime_destroy(&runtime);
    if (fail) return 1;
    puts("PASS Stray runtime: tipped-arrow payload, impact, and native continuation");
    return 0;
}
