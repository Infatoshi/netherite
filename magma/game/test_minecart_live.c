#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define CHECK(c, m) do { if (!(c)) { \
    fprintf(stderr, "FAIL: %s\n", (m)); return 1; } } while (0)

static int init_flat(GmRuntime *r) {
    GmConfig cfg;
    char err[256];
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.view_distance = 1;
    cfg.mobs = 0;
    cfg.weather = 0;
    if (!gm_runtime_init(r, &cfg, err, sizeof err)) {
        fprintf(stderr, "FAIL: %s\n", err);
        return 0;
    }
    return 1;
}

static int track(GmRuntime *r, int block, int meta) {
    for (int x = 8; x <= 16; ++x) {
        if (!gm_runtime_load_block(r, x, 77, 8, 1, 0)
                || !gm_runtime_load_block(r, x, 78, 8, block, meta))
            return 0;
    }
    return 1;
}

static unsigned long long dbits(double value) {
    union { double d; uint64_t u; } bits;
    bits.d = value;
    return (unsigned long long)bits.u;
}

static unsigned fbits(float value) {
    union { float f; uint32_t u; } bits;
    bits.f = value;
    return bits.u;
}

static void print_cart(const char *name, const GmRuntimeMinecart *cart) {
    printf("%s %016llx %016llx %016llx %016llx %016llx %016llx "
           "%08x %08x\n", name,
           dbits(cart->x), dbits(cart->y), dbits(cart->z),
           dbits(cart->vx), dbits(cart->vy), dbits(cart->vz),
           fbits(cart->yaw), fbits(cart->pitch));
}

int main(void) {
    GmRuntime r;
    GmRuntimeMinecart cart;

    CHECK(init_flat(&r) && track(&r, 66, 1), "straight rail fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_RIDEABLE, 7001,
              12.5, 78.0625, 8.5, 0.2, 0.0, 0.0, 0.0f),
          "spawn straight cart");
    gm_runtime_tick_minecarts(&r);
    gm_runtime_tick_minecarts(&r);
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart), "read straight cart");
    print_cart("S", &cart);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 27, 1), "braking rail fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_RIDEABLE, 7002,
              12.5, 78.0625, 8.5, 0.2, 0.0, 0.0, 0.0f),
          "spawn braking cart");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart), "read braking cart");
    print_cart("B", &cart);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 27, 9), "powered rail fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_RIDEABLE, 7003,
              12.5, 78.0625, 8.5, 0.2, 0.0, 0.0, 0.0f),
          "spawn powered cart");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart), "read powered cart");
    print_cart("P", &cart);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "slope fixture");
    for (int x = 10; x <= 14; ++x)
        CHECK(gm_runtime_load_block(
                  &r, x, 77 + (x >= 13 ? 1 : 0), 8, 1, 0),
              "slope support");
    CHECK(gm_runtime_load_block(&r, 11, 78, 8, 66, 1)
              && gm_runtime_load_block(&r, 12, 78, 8, 66, 2)
              && gm_runtime_load_block(&r, 13, 79, 8, 66, 1),
          "slope rails");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_RIDEABLE, 7004,
              12.25, 78.0625, 8.5, 0.2, 0.0, 0.0, 0.0f),
          "spawn slope cart");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart), "read slope cart");
    print_cart("U", &cart);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 28, 1), "detector fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_RIDEABLE, 7005,
              12.5, 78.0625, 8.5, 0.1, 0.0, 0.0, 0.0f),
          "spawn detector cart");
    gm_runtime_tick_minecarts(&r);
    printf("D %d %d\n", gm_world_meta(r.world, 12, 78, 8),
           gm_runtime_scheduled_tick_count(&r));
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 157, 9), "activator fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_TNT, 7006,
              12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f),
          "spawn TNT cart");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart), "read ignited cart");
    {
        int ignited = cart.tnt_fuse >= 0;
        gm_runtime_destroy(&r);
        CHECK(init_flat(&r) && track(&r, 157, 9),
              "hopper activator fixture");
        CHECK(gm_runtime_spawn_minecart_fixture(
                  &r, GM_MINECART_HOPPER, 7007,
                  12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f),
              "spawn hopper cart");
        gm_runtime_tick_minecarts(&r);
        CHECK(gm_runtime_minecart_get(&r, 0, &cart),
              "read disabled hopper cart");
        printf("A %d %d\n", ignited, cart.hopper_enabled);
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 66, 1), "hopper capture fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_HOPPER, 7008,
              12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f)
              && gm_runtime_spawn_item_fixture(
                  &r, 8001, 12.5, 78.2, 8.5,
                  0.0, 0.0, 0.0, 264, 3, 0, 0, 0, 1),
          "spawn hopper cart and item");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart),
          "read hopper capture cart");
    printf("H %d %d\n", cart.slots[0].count,
           r.entities.n_active == 0 ? 1 : 0);
    gm_runtime_destroy(&r);

    puts("minecart_live: PASS (rails, power, slope, detector, activator)");
    return 0;
}
