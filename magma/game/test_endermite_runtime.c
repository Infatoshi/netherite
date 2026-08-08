#include "game/native_save.h"
#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static int mob_slot(const GmMobLive *m, int eid) {
    const EwStore *s = m->current ? &m->b : &m->a;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->id[slot] == eid)
            return slot;
    return -1;
}

static void clean_save(const char *root) {
    char path[512];
    static const char *files[] = {
        "runtime.bin", "player_statistics.json", "manifest.bin",
        "world_dim-1.bin", "world_dim0.bin", "world_dim1.bin",
    };
    for (size_t i = 0; i < sizeof files / sizeof files[0]; ++i) {
        snprintf(path, sizeof path,
                 "%s/endermite/generation-0000000000000001/%s",
                 root, files[i]);
        (void)remove(path);
    }
    snprintf(path, sizeof path,
             "%s/endermite/generation-0000000000000001", root);
    (void)rmdir(path);
    snprintf(path, sizeof path, "%s/endermite/current", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/endermite/write.lock", root);
    (void)remove(path);
    snprintf(path, sizeof path, "%s/endermite", root);
    (void)rmdir(path);
    (void)rmdir(root);
}

static GmRuntime *new_runtime(GmConfig *cfg) {
    char error[256];
    GmRuntime *r = calloc(1, sizeof *r);
    CHECK(r != NULL, "Endermite runtime allocation succeeds");
    if (!r) return NULL;
    gm_config_defaults(cfg);
    cfg->view_distance = 1;
    cfg->mobs = 0;
    CHECK(gm_runtime_init(r, cfg, error, sizeof error),
          "Endermite runtime initializes");
    if (fail) {
        free(r);
        return NULL;
    }
    return r;
}

static int spawn_fixture(
        GmRuntime *r, int eid, int lifetime, int player_spawned,
        int persistence_required) {
    double y = (double)gm_world_surface_y(r->world, 8, 8) + 1.0;
    gm_runtime_set_pose(r, 8.5, y, 8.5, 0.0F, 20.0F);
    return gm_runtime_spawn_mob_fixture(
               r, EW_TYPE_ENDERMITE, eid, 8.5, y, 10.5,
               0.0, 0.0, 0.0, 0.0F, 8.0F, 1, 0, 0, 0)
        && gm_runtime_restore_endermite_state(
               r, eid, lifetime, player_spawned, persistence_required);
}

static void test_expiry_and_save(void) {
    GmConfig cfg;
    GmRuntime *r = new_runtime(&cfg);
    if (!r) return;
    const int eid = 687000;
    CHECK(spawn_fixture(r, eid, 2398, 1, 0),
          "near-expiry Endermite fixture initializes");
    GmAction idle = {.hotbar_sel = -1};
    gm_runtime_tick(r, idle);
    int slot = mob_slot(&r->mobs, eid);
    CHECK(slot > 0 && r->mobs.endermite_lifetime[slot] == 2399
              && r->mobs.endermite_player_spawned[slot],
          "Endermite lifetime reaches 2399 and preserves player origin");

    char save_root[256], error[256];
    snprintf(save_root, sizeof save_root,
             ".tmp/endermite-runtime-%ld", (long)getpid());
    clean_save(save_root);
    CHECK(gm_native_save_write(
              r, save_root, "endermite", error, sizeof error),
          "native save records the tick-2399 Endermite boundary");
    gm_runtime_tick(r, idle);
    CHECK(mob_slot(&r->mobs, eid) < 0,
          "nonpersistent Endermite retires exactly at lifetime 2400");
    CHECK(gm_native_save_load(
              r, &cfg, save_root, "endermite", error, sizeof error),
          "native save restores the tick-2399 Endermite boundary");
    gm_runtime_tick(r, idle);
    CHECK(mob_slot(&r->mobs, eid) < 0,
          "save/reload preserves exact lifetime-2400 retirement");
    clean_save(save_root);
    gm_runtime_destroy(r);
    free(r);
}

static void test_persistence_and_view(void) {
    GmConfig cfg;
    GmRuntime *r = new_runtime(&cfg);
    if (!r) return;
    const int eid = 687100;
    CHECK(spawn_fixture(r, eid, 2399, 0, 1),
          "persistent Endermite fixture initializes");
    GmAction idle = {.hotbar_sel = -1};
    for (int tick = 0; tick < 5; ++tick)
        gm_runtime_tick(r, idle);
    int slot = mob_slot(&r->mobs, eid);
    CHECK(slot > 0 && r->mobs.endermite_lifetime[slot] == 2399
              && r->mobs.persistence_required[slot],
          "persistence freezes lifetime and prevents retirement");
    GmEntityView view;
    CHECK(gm_mobs_fill_views(&r->mobs, &view, 1) == 1
              && view.type == EW_TYPE_ENDERMITE
              && view.ent_id == eid
              && view.ticks_existed == 5,
          "live Endermite state reaches the animated render view");
    gm_runtime_destroy(r);
    free(r);
}

int main(void) {
    test_expiry_and_save();
    test_persistence_and_view();
    if (fail) return 1;
    puts("PASS Endermite runtime: lifetime, persistence, save continuation and render handoff");
    return 0;
}
