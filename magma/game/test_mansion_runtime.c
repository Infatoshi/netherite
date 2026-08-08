#include "game/runtime.h"
#include "game/mansion_live.h"

#include <limits.h>
#include <stdio.h>

#include "assets/mansion_templates.h"

#define CHECK(c,m) do { if (!(c)) { fprintf(stderr,"FAIL: %s\n",m); return 1; } } while (0)

int main(void) {
    GmRuntime runtime;
    GmConfig cfg;
    GmMansion expected;
    char err[256];
    int min_x = INT_MAX, min_z = INT_MAX, max_x = INT_MIN, max_z = INT_MIN;
    int chest_markers = 0, resident_markers = 0;
    int dark_oak = 0, planks = 0, cobble = 0;
    gm_config_defaults(&cfg);
    cfg.seed = 1;
    cfg.world = GM_WORLD_DEFAULT;
    cfg.view_distance = 1;
    cfg.villages = 0;
    cfg.mobs = 1;
    cfg.weather = 0;
    CHECK(gm_runtime_init(&runtime, &cfg, err, sizeof err), err);
    CHECK(gm_mansion_build_at_chunk(cfg.seed, 0, 0, 70, &expected),
          "build expected real mansion graph");
    for (int i = 0; i < expected.count; ++i) {
        const GmMansionPiece *piece = &expected.pieces[i];
        const GmMansionTemplate *t =
            &GM_MANSION_TEMPLATES[piece->template_index];
        if (piece->min_x < min_x) min_x = piece->min_x;
        if (piece->min_z < min_z) min_z = piece->min_z;
        if (piece->max_x > max_x) max_x = piece->max_x;
        if (piece->max_z > max_z) max_z = piece->max_z;
        for (int j = 0; j < t->marker_count; ++j) {
            int kind = t->markers[j].kind;
            chest_markers += kind >= 1 && kind <= 4;
            resident_markers += kind == 5 || kind == 6;
        }
    }
    CHECK(gm_runtime_generate_mansion(&runtime, 0, 0, 70) == expected.count,
          "place complete Java-derived mansion graph");
    CHECK(runtime.mansion_count == 1, "mansion start retained once");
    CHECK(gm_runtime_chest_count(&runtime) == chest_markers,
          "every generated chest marker creates one live chest tile");
    CHECK(gm_runtime_mansion_resident_count(&runtime) == resident_markers,
          "every Mage/Warrior marker is retained as a resident site");
    {
        const EwStore *store = runtime.mobs.current
            ? &runtime.mobs.b : &runtime.mobs.a;
        int evokers = 0, vindicators = 0;
        for (int i = 0; i < resident_markers; ++i) {
            GmRuntimeMansionResident resident;
            CHECK(gm_runtime_mansion_resident_get(&runtime, i, &resident)
                      && resident.eid > 0,
                  "mansion marker materializes one live resident");
            int slot = gm_mobs_find_slot_by_eid(
                &runtime.mobs, resident.eid);
            CHECK(slot > 0 && store->health[slot] == 24.0F
                      && runtime.mobs.persistence_required[slot],
                  "mansion resident keeps exact health and persistence");
            evokers += store->type[slot] == EW_TYPE_EVOKER;
            vindicators += store->type[slot] == EW_TYPE_VINDICATOR;
        }
        CHECK(evokers > 0 && vindicators > 0
                  && evokers + vindicators == resident_markers,
              "Mage and Warrior markers retain distinct live types");
    }
    for (int i = 0; i < gm_runtime_chest_count(&runtime); ++i) {
        GmRuntimeChest chest;
        CHECK(gm_runtime_chest_get(&runtime, i, &chest),
              "generated mansion chest is readable");
        CHECK(chest.state.loot_table == CHEST_LOOT_WOODLAND_MANSION
                  && !chest.state.loot_filled,
              "mansion chest retains deferred woodland loot table");
    }
    for (int z = min_z; z <= max_z; ++z)
        for (int x = min_x; x <= max_x; ++x)
            for (int y = 2; y <= 110; ++y) {
                int id = gm_world_block(runtime.world, x, y, z);
                dark_oak += id == 162;
                planks += id == 5;
                cobble += id == 4;
            }
    CHECK(dark_oak > 1000 && planks > 5000 && cobble > 100,
          "full mansion shell, rooms, and foundations are materialized");
    int residents = runtime.mansion_resident_count;
    int chests = gm_runtime_chest_count(&runtime);
    CHECK(gm_runtime_generate_mansion(&runtime, 0, 0, 70) == 1,
          "repeat generation recognizes retained start");
    CHECK(runtime.mansion_count == 1
              && runtime.mansion_resident_count == residents
              && gm_runtime_chest_count(&runtime) == chests,
          "repeat generation does not duplicate blocks, tiles, or residents");
    gm_runtime_destroy(&runtime);
    printf("mansion_runtime: PASS (%d pieces, %d chests, %d residents, "
           "%d dark-oak, %d planks, %d cobble)\n",
           expected.count, chests, residents, dark_oak, planks, cobble);
    return 0;
}
