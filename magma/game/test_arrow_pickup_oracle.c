#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv) {
    GmConfig config;
    GmRuntime runtime;
    char error[256];
    int pickup, shake, inventory_mode, creative;
    int arrow_count = 0;
    if (argc != 5) return 2;
    pickup = atoi(argv[1]);
    shake = atoi(argv[2]);
    inventory_mode = atoi(argv[3]);
    creative = atoi(argv[4]);
    if (pickup < 0 || pickup > 2 || shake < 0 || shake > 255
            || inventory_mode < 0 || inventory_mode > 2
            || (creative != 0 && creative != 1))
        return 2;
    gm_config_defaults(&config);
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.seed = 0;
    if (!gm_runtime_init(&runtime, &config, error, sizeof error)) {
        fprintf(stderr, "%s\n", error);
        return 1;
    }
    isr_init(&runtime.player.inv);
    if (inventory_mode == 1) {
        for (int i = 0; i < ISR_MAIN_SLOTS; ++i)
            isr_set_stack(&runtime.player.inv, i, ic_mk(1, 64, 0));
    } else if (inventory_mode == 2) {
        isr_set_stack(&runtime.player.inv, 0, ic_mk(262, 63, 0));
    }
    runtime.tape_creative = creative;
    if (!gm_runtime_spawn_player_arrow_state_fixture(
            &runtime, 5100, 8.5, 4.25, 8.5,
            0.0, 0.0, 0.0, 0.0F, 0.0F,
            0, -1, 2.0, 0, 0, pickup, 1, shake, 0,
            8, 4, 8, 1, 0, UINT64_C(1), 0, 0.0)) {
        gm_runtime_destroy(&runtime);
        return 1;
    }
    (void)gm_runtime_player_arrow_pickup_now(&runtime, 0);
    for (int i = 0; i < ISR_MAIN_SLOTS; ++i) {
        ICStack stack = isr_get_stack(&runtime.player.inv, i);
        if (stack.item == 262) arrow_count += stack.count;
    }
    ICStack slot0 = isr_get_stack(&runtime.player.inv, 0);
    printf("{\"ok\":true,\"arrow_alive\":%s,"
           "\"arrow_count\":%d,\"slot0_item\":%d,"
           "\"slot0_count\":%d}\n",
           runtime.projectiles[0].active ? "true" : "false",
           arrow_count, slot0.item, slot0.count);
    gm_runtime_destroy(&runtime);
    return 0;
}
