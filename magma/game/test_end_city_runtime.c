#include "game/runtime.h"

#include <stdio.h>

#define CHECK(c,m) do { if (!(c)) { fprintf(stderr,"FAIL: %s\n",m); return 1; } } while (0)

int main(void) {
    GmRuntime r;
    GmConfig cfg;
    GmRuntimeItemFrame frame;
    GmRuntimeChest chest;
    char err[256];
    int purpur = 0, end_bricks = 0, found_elytra = 0;
    gm_config_defaults(&cfg);
    cfg.seed = 1;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), err);
    CHECK(gm_runtime_set_dimension(&r, 1), "enter End");
    CHECK(gm_runtime_generate_end_city(&r, -4, 7, 70) == 110,
          "place Java-locked 110-piece city graph");
    CHECK(r.end_city_count == 1, "city start retained once");
    for (int x = -132; x <= -24; ++x)
        for (int y = 65; y <= 210; ++y)
            for (int z = 80; z <= 163; ++z) {
                int id = gm_world_block(r.world, x, y, z);
                purpur += id >= 201 && id <= 205;
                end_bricks += id == 206;
            }
    CHECK(purpur > 1000 && end_bricks > 100,
          "real template purpur/end-brick volume is present");
    CHECK(gm_runtime_chest_count(&r) > 0
          && gm_runtime_chest_get(&r, 0, &chest)
          && chest.state.loot_table == CHEST_LOOT_END_CITY
          && !chest.state.loot_filled,
          "city chest carries deferred End City treasure table");
    chest_live_ensure_loot(&chest.state);
    CHECK(chest_live_total_items(&chest.state) > 0,
          "End City treasure materializes on first access");
    CHECK(gm_runtime_item_frame_count(&r) == 1
          && gm_runtime_item_frame_get(&r, 0, &frame)
          && frame.item == 443 && frame.count == 1,
          "ship marker creates elytra item frame");
    CHECK(gm_runtime_break_item_frame(&r, frame.eid),
          "first frame hit releases displayed elytra");
    CHECK(gm_runtime_item_frame_get(&r, 0, &frame) && frame.item == 0,
          "frame remains empty after displayed-item hit");
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (r.entities.ents[i].active && r.entities.ents[i].item == 443)
            found_elytra = 1;
    CHECK(found_elytra, "elytra is a live collectible item entity");
    gm_runtime_destroy(&r);
    printf("end_city_runtime: PASS (%d purpur-family, %d end-brick cells, elytra collectible)\n",
           purpur, end_bricks);
    return 0;
}
