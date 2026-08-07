#include "game/runtime.h"

#include <stdio.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

int main(void) {
    GmRuntime r;
    GmConfig cfg;
    GmRuntimeEndGateway gateway_state;
    char err[256];
    int index, x, z, bedrock = 0, gateway = 0, air = 0;
    gm_config_defaults(&cfg);
    cfg.seed = 0;
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), err);
    index = r.end_gateway_order[r.end_gateway_order_count - 1];
    x = (int)(96.0 * cos(2.0 * (-MC_PI + 0.15707963267948966 * index)));
    z = (int)(96.0 * sin(2.0 * (-MC_PI + 0.15707963267948966 * index)));
    CHECK(gm_runtime_set_dimension(&r, 1), "create End world");
    CHECK(gm_runtime_spawn_end_gateway(
              &r, 32, 75, 32, 1, 0, 80, 0, 1),
          "generate exact-position gateway");
    for (int dx = -1; dx <= 1; ++dx)
        for (int dy = -2; dy <= 2; ++dy)
            for (int dz = -1; dz <= 1; ++dz) {
                int block = gm_world_block(
                    r.world, 32 + dx, 75 + dy, 32 + dz);
                if (block == 7) ++bedrock;
                else if (block == 209) ++gateway;
                else if (block == 0) ++air;
            }
    printf("G %d %d %d %d %d %d\n",
           index, x, z, bedrock, gateway, air);
    CHECK(bedrock == 12 && gateway == 1 && air == 32,
          "WorldGenEndGateway exact 3x5x3 volume");
    gm_runtime_set_pose(&r, 32.5, 75.0, 32.5, 0.0F, 0.0F);
    gm_runtime_tick_end_gateways(&r);
    CHECK(gm_runtime_end_gateway_get(&r, 0, &gateway_state),
          "gateway tile remains active");
    printf("T %lld %d %.1f %.1f %.1f\n",
           gateway_state.age, gateway_state.teleport_cooldown,
           r.player.ent.posX + r.ox, r.player.ent.posY,
           r.player.ent.posZ + r.oz);
    CHECK(gateway_state.age == 1 && gateway_state.teleport_cooldown == 40,
          "teleport triggers forty-tick cooldown");
    gm_runtime_tick_end_gateways(&r);
    CHECK(gm_runtime_end_gateway_get(&r, 0, &gateway_state)
          && gateway_state.teleport_cooldown == 39,
          "cooldown decrements before entity query");
    gateway_state.age = 2399;
    gateway_state.teleport_cooldown = 0;
    r.end_gateways[0] = gateway_state;
    gm_runtime_set_pose(&r, 40.5, 75.0, 40.5, 0.0F, 0.0F);
    gm_runtime_tick_end_gateways(&r);
    CHECK(gm_runtime_end_gateway_get(&r, 0, &gateway_state)
          && gateway_state.age == 2400
          && gateway_state.teleport_cooldown == 40,
          "periodic 2400-tick cooldown pulse");
    gm_runtime_destroy(&r);
    puts("end_gateway: PASS (shuffle, volume, lifecycle, exact travel)");
    return 0;
}
