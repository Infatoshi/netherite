#include "game/runtime.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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

static int item_total(const GmRuntime *r, int item) {
    int total = 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        const GmLiveEnt *entity = &r->entities.ents[i];
        if (entity->active && entity->type == 0 && entity->item == item)
            total += entity->count;
    }
    return total;
}

int main(void) {
    GmRuntime r;
    GmRuntimeMinecart cart;

    {
        const char *checkpoint = "minecart-capacity-checkpoint.bin";
        GmEntityView views[GM_RUNTIME_MINECARTS + 1];
        CHECK(init_flat(&r), "minecart capacity fixture initializes");
        for (int index = 0; index <= GM_RUNTIME_MINECARTS; ++index)
            CHECK(gm_runtime_spawn_minecart_fixture(
                      &r, GM_MINECART_RIDEABLE, 6800 + index,
                      100.5 + index * 2.0, 240.0, 100.5,
                      0.0, 0.0, 0.0, 0.0F),
                  "minecart store grows beyond hot capacity");
        CHECK(r.minecart_count == GM_RUNTIME_MINECARTS + 1
                  && r.minecarts_cap > GM_RUNTIME_MINECARTS
                  && gm_runtime_projectile_views(
                      &r, views, GM_RUNTIME_MINECARTS + 1)
                      == GM_RUNTIME_MINECARTS + 1
                  && views[GM_RUNTIME_MINECARTS].ent_id
                      == 6800 + GM_RUNTIME_MINECARTS,
              "grown minecart store preserves payload and render order");
        CHECK(gm_runtime_write_checkpoint(&r, checkpoint),
              "grown minecart store writes checkpoint");
        CHECK(gm_runtime_load_checkpoint(&r, checkpoint),
              "grown minecart store reloads checkpoint");
        CHECK(r.minecarts_cap > GM_RUNTIME_MINECARTS
                  && r.minecart_count == GM_RUNTIME_MINECARTS + 1
                  && r.minecarts[GM_RUNTIME_MINECARTS].active
                  && r.minecarts[GM_RUNTIME_MINECARTS].eid
                      == 6800 + GM_RUNTIME_MINECARTS,
              "grown minecart store survives checkpoint reload");
        (void)remove(checkpoint);
        gm_runtime_destroy(&r);
    }

    /* Live rendering must retain the concrete cart class and display payload;
     * treating every cart as EntityMinecartEmpty hides six default blocks. */
    {
        static const int kinds[7] = {
            GM_MINECART_RIDEABLE, GM_MINECART_CHEST,
            GM_MINECART_FURNACE, GM_MINECART_TNT,
            GM_MINECART_SPAWNER, GM_MINECART_HOPPER,
            GM_MINECART_COMMAND
        };
        static const int view_types[7] = {
            GM_VIEW_MINECART_EMPTY, GM_VIEW_MINECART_CHEST,
            GM_VIEW_MINECART_FURNACE, GM_VIEW_MINECART_TNT,
            GM_VIEW_MINECART_SPAWNER, GM_VIEW_MINECART_HOPPER,
            GM_VIEW_MINECART_COMMAND
        };
        GmEntityView views[7];
        CHECK(init_flat(&r) && track(&r, 66, 1),
              "minecart render-view fixture");
        for (int k = 0; k < 7; ++k)
            CHECK(gm_runtime_spawn_minecart_fixture(
                      &r, kinds[k], 6900 + k,
                      9.0 + k, 78.0625, 8.5,
                      0.0, 0.0, 0.0, 0.0f),
                  "spawn render-view minecart variant");
        r.minecarts[2].fuel = 2;
        r.minecarts[3].tnt_fuse = 9;
        r.minecarts[6].custom_display = 1;
        r.minecarts[6].display_block = 1;
        r.minecarts[6].display_meta = 4;
        r.minecarts[6].display_offset = 9;
        CHECK(gm_runtime_projectile_views(&r, views, 7) == 7,
              "all minecart variants produce render views");
        for (int k = 0; k < 7; ++k)
            CHECK(views[k].type == view_types[k],
                  "minecart render view preserves subtype");
        CHECK(views[2].minecart_powered,
              "fueled furnace cart view is powered");
        CHECK(views[3].minecart_tnt_fuse == 9,
              "TNT cart view carries fuse");
        CHECK(views[6].minecart_custom_display
                  && views[6].minecart_display_block == 1
                  && views[6].minecart_display_meta == 4
                  && views[6].minecart_display_offset == 9,
              "minecart view carries custom display state");
        gm_runtime_destroy(&r);
    }

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

    CHECK(init_flat(&r) && track(&r, 66, 1), "minecart damage fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_RIDEABLE, 7270,
              12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f)
              && gm_runtime_minecart_attack(
                  &r, 7270, 1.0f, 0, 0, 0),
          "damage minecart nonlethally");
    CHECK(gm_runtime_minecart_get(&r, 0, &cart),
          "read damaged minecart");
    printf("N %d %d %08x %d\n", cart.rolling_direction,
           cart.rolling_amplitude, fbits(cart.damage),
           cart.active ? 0 : 1);
    gm_runtime_destroy(&r);

    {
        static const int kinds[] = {
            GM_MINECART_RIDEABLE, GM_MINECART_CHEST,
            GM_MINECART_FURNACE, GM_MINECART_TNT,
            GM_MINECART_HOPPER
        };
        static const int items[] = {46, 54, 61, 154, 264, 328};
        for (unsigned k = 0; k < sizeof kinds / sizeof kinds[0]; ++k) {
            CHECK(init_flat(&r) && track(&r, 66, 1),
                  "minecart lethal fixture");
            CHECK(gm_runtime_spawn_minecart_fixture(
                      &r, kinds[k], 7280 + (int)k,
                      12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f),
                  "spawn lethal minecart");
            if (kinds[k] == GM_MINECART_CHEST
                    || kinds[k] == GM_MINECART_HOPPER)
                CHECK(gm_runtime_minecart_set_slot(
                          &r, 7280 + (int)k, 0, 264, 3, 0),
                      "fill container minecart");
            CHECK(gm_runtime_minecart_attack(
                      &r, 7280 + (int)k, 5.0f, 0, 0, 0) == 2,
                  "destroy minecart");
            cart = r.minecarts[0];
            printf("K %d %d %08x %d %d\n", kinds[k],
                   cart.active ? 0 : 1, fbits(cart.damage),
                   cart.rolling_direction, cart.rolling_amplitude);
            for (unsigned q = 0; q < sizeof items / sizeof items[0]; ++q) {
                int total = item_total(&r, items[q]);
                if (total > 0)
                    printf("KD %d %d %d\n", kinds[k], items[q], total);
            }
            gm_runtime_destroy(&r);
        }
    }

    CHECK(init_flat(&r) && track(&r, 66, 1),
          "creative minecart destruction fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_RIDEABLE, 7290,
              12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f)
              && gm_runtime_minecart_attack(
                  &r, 7290, 1.0f, 1, 0, 0) == 2,
          "creative minecart destruction");
    printf("KC %d %d\n", r.minecarts[0].active ? 0 : 1,
           r.entities.n_active);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 66, 1),
          "disabled minecart drops fixture");
    CHECK(gm_runtime_set_do_entity_drops(&r, 0)
              && gm_runtime_spawn_minecart_fixture(
                  &r, GM_MINECART_CHEST, 7291,
                  12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f)
              && gm_runtime_minecart_set_slot(
                  &r, 7291, 0, 264, 3, 0)
              && gm_runtime_minecart_attack(
                  &r, 7291, 5.0f, 0, 0, 0) == 2,
          "destroy chest cart with entity drops disabled");
    printf("KG %d %d %d %d\n", r.minecarts[0].active ? 0 : 1,
           item_total(&r, 264), item_total(&r, 328), item_total(&r, 54));
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

    for (int meta = 0; meta < 10; ++meta) {
        char name[8];
        CHECK(init_flat(&r) && track(&r, 66, meta),
              "rail direction fixture");
        CHECK(gm_runtime_spawn_minecart_fixture(
                  &r, GM_MINECART_RIDEABLE, 7100 + meta,
                  12.5, 78.0625, 8.5, 0.2, 0.0, 0.1, 0.0f),
              "spawn rail direction cart");
        gm_runtime_tick_minecarts(&r);
        CHECK(gm_runtime_minecart_get(&r, 0, &cart),
              "read rail direction cart");
        snprintf(name, sizeof name, "M%d", meta);
        print_cart(name, &cart);
        gm_runtime_destroy(&r);
    }

    CHECK(init_flat(&r), "derailed fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_RIDEABLE, 7200,
              12.5, 90.0, 8.5, 0.6, 0.2, -0.5, 0.0f),
          "spawn derailed cart");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart), "read derailed cart");
    print_cart("X", &cart);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 66, 1),
          "furnace speed-cap fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_FURNACE, 7250,
              12.5, 78.0625, 8.5, 0.5, 0.0, 0.0, 0.0f),
          "spawn speed-capped furnace cart");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart),
          "read speed-capped furnace cart");
    print_cart("Q0", &cart);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 66, 1),
          "furnace push fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_FURNACE, 7251,
              12.5, 78.0625, 8.5, 0.1, 0.0, 0.0, 0.0f)
              && gm_runtime_minecart_set_state(
                  &r, 7251, 2, 0.2, 0.0, -1, 1, -1),
          "spawn pushed furnace cart");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart),
          "read pushed furnace cart");
    print_cart("Q1", &cart);
    printf("QP %016llx %016llx %d\n",
           dbits(cart.push_x), dbits(cart.push_z), cart.fuel);
    gm_runtime_destroy(&r);

    for (int seed = 0; seed <= 1; ++seed) {
        GmRuntimeParticleEvent event;
        int count, kind = -1;
        double x = 0.0, y = 0.0, z = 0.0;
        CHECK(init_flat(&r) && track(&r, 66, 1),
              "furnace smoke fixture");
        CHECK(gm_runtime_spawn_minecart_fixture(
                  &r, GM_MINECART_FURNACE, 7252 + seed,
                  12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f)
                  && gm_runtime_minecart_set_state(
                      &r, 7252 + seed, 2, 0.0, 0.0, -1, 1, -1)
                  && gm_runtime_minecart_set_random_state(
                      &r, 7252 + seed, (uint64_t)seed, 0, 0.0),
              "spawn seeded furnace cart");
        gm_runtime_tick_minecarts(&r);
        CHECK(gm_runtime_minecart_get(&r, 0, &cart),
              "read seeded furnace cart");
        count = gm_runtime_particle_event_count(&r);
        if (count > 0) {
            CHECK(gm_runtime_particle_event_get(&r, 0, &event),
                  "read furnace smoke event");
            kind = event.kind;
            x = event.x; y = event.y; z = event.z;
        }
        printf("QF%d %d %d %016llx %016llx %016llx %012llx\n",
               seed, count, kind, dbits(x), dbits(y), dbits(z),
               (unsigned long long)cart.random_seed48);
        gm_runtime_destroy(&r);
    }

    CHECK(init_flat(&r) && track(&r, 66, 1), "ridden cart fixture");
    gm_runtime_set_pose(&r, 11.5, 78.0, 8.5, -90.0f, 0.0f);
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_RIDEABLE, 7260,
              12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f)
              && gm_runtime_minecart_mount(&r, 7260)
              && gm_runtime_minecart_set_rider_input(
                  &r, 1.0f, -90.0f),
          "mount ridden cart");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart), "read ridden cart");
    print_cart("V", &cart);
    printf("VP %016llx %016llx %016llx %d\n",
           dbits(r.player.ent.posX + r.ox), dbits(r.player.ent.posY),
           dbits(r.player.ent.posZ + r.oz),
           gm_runtime_minecart_riding(&r) == 7260 ? 1 : 0);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 66, 1),
          "playable furnace interaction fixture");
    gm_runtime_set_pose(&r, 11.5, 78.0, 8.5, -90.0f, 60.0f);
    CHECK(gm_runtime_set_inventory(&r, 0, 263, 2, 0)
              && gm_runtime_spawn_minecart_fixture(
                  &r, GM_MINECART_FURNACE, 7261,
                  12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f),
          "prepare playable furnace interaction");
    {
        GmAction use;
        memset(&use, 0, sizeof use);
        use.hotbar_sel = -1;
        use.use = 1;
        gm_runtime_tick(&r, use);
    }
    CHECK(gm_runtime_minecart_get(&r, 0, &cart),
          "read fueled furnace cart");
    print_cart("I", &cart);
    printf("IF %d %016llx %016llx %d\n", cart.fuel,
           dbits(cart.push_x), dbits(cart.push_z),
           isr_get_stack(&r.player.inv, 0).count);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 66, 1),
          "playable minecart attack fixture");
    gm_runtime_set_pose(&r, 11.5, 78.0, 8.5, -90.0f, 60.0f);
    CHECK(gm_runtime_set_player_combat(&r, 20, 0, 0, 0, 0, 0)
              && gm_runtime_spawn_minecart_fixture(
                  &r, GM_MINECART_RIDEABLE, 7262,
                  12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f),
          "prepare playable minecart attack");
    {
        GmAction attack, idle;
        memset(&attack, 0, sizeof attack);
        memset(&idle, 0, sizeof idle);
        attack.hotbar_sel = idle.hotbar_sel = -1;
        attack.do_break = 1;
        gm_runtime_tick(&r, attack);
        gm_runtime_tick(&r, idle);
    }
    CHECK(gm_runtime_minecart_get(&r, 0, &cart),
          "read playably attacked minecart");
    CHECK(cart.damage == 9.0F
              && cart.rolling_direction == -1
              && cart.rolling_amplitude == 9,
          "playable attack reaches exact minecart damage path");
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 66, 1),
          "playable minecart mount fixture");
    gm_runtime_set_pose(&r, 11.5, 78.0, 8.5, -90.0f, 60.0f);
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_RIDEABLE, 7263,
              12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f),
          "prepare playable minecart mount");
    {
        GmAction use, sneak;
        memset(&use, 0, sizeof use);
        memset(&sneak, 0, sizeof sneak);
        use.hotbar_sel = sneak.hotbar_sel = -1;
        use.use = 1;
        gm_runtime_tick(&r, use);
        CHECK(gm_runtime_minecart_riding(&r) == 7263,
              "playable use mounts rideable minecart");
        sneak.sneak = 1;
        gm_runtime_tick(&r, sneak);
        CHECK(gm_runtime_minecart_riding(&r) < 0,
              "playable sneak dismounts rideable minecart");
    }
    gm_runtime_destroy(&r);

    for (int furnace = 0; furnace <= 1; ++furnace) {
        GmRuntimeMinecart left, right;
        CHECK(init_flat(&r) && track(&r, 66, 1),
              "minecart collision fixture");
        CHECK(gm_runtime_spawn_minecart_fixture(
                  &r, GM_MINECART_RIDEABLE, 7300,
                  12.2, 78.0625, 8.5, 0.2, 0.0, 0.0, 0.0f)
                  && gm_runtime_spawn_minecart_fixture(
                      &r, furnace ? GM_MINECART_FURNACE
                                  : GM_MINECART_RIDEABLE,
                      7301, 12.8, 78.0625, 8.5,
                      -0.1, 0.0, 0.0, 0.0f),
              "spawn colliding minecarts");
        gm_runtime_tick_minecarts(&r);
        CHECK(gm_runtime_minecart_get(&r, 0, &left)
                  && gm_runtime_minecart_get(&r, 1, &right),
              "read colliding minecarts");
        print_cart(furnace ? "F0" : "C0", &left);
        print_cart(furnace ? "F1" : "C1", &right);
        gm_runtime_destroy(&r);
    }

    CHECK(init_flat(&r) && track(&r, 28, 1), "detector fixture");
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_RIDEABLE, 7005,
              12.5, 78.0625, 8.5, 0.1, 0.0, 0.0, 0.0f),
          "spawn detector cart");
    gm_runtime_tick_minecarts(&r);
    printf("D %d %d\n", gm_world_meta(r.world, 12, 78, 8),
           gm_runtime_scheduled_tick_count(&r));
    r.minecarts[0].active = 0;
    r.minecart_count = 0;
    r.scheduled_tick_count = 0;
    CHECK(gm_runtime_schedule_tick(
              &r, 12, 78, 8, 28, r.clock.total_time, 0, 0),
          "schedule detector update callback");
    {
        GmAction idle;
        memset(&idle, 0, sizeof idle);
        idle.hotbar_sel = -1;
        gm_runtime_tick(&r, idle);
    }
    printf("D2 %d %d\n", gm_world_meta(r.world, 12, 78, 8),
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
        gm_runtime_destroy(&r);
        CHECK(init_flat(&r) && track(&r, 157, 9),
              "rideable activator fixture");
        CHECK(gm_runtime_spawn_minecart_fixture(
                  &r, GM_MINECART_RIDEABLE, 7009,
                  12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f)
                  && gm_runtime_minecart_mount(&r, 7009),
              "spawn rideable activator cart");
        gm_runtime_tick_minecarts(&r);
        CHECK(gm_runtime_minecart_get(&r, 0, &cart),
              "read rideable activator cart");
        printf("AR %d %d %08x\n", cart.rolling_direction,
               cart.rolling_amplitude, fbits(cart.damage));
        printf("AE %d\n", gm_runtime_minecart_riding(&r) < 0 ? 1 : 0);
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

    CHECK(init_flat(&r) && track(&r, 66, 1),
          "inactive spawner minecart fixture");
    r.player.ent.posX = 0.0; r.player.ent.posY = 4.0; r.player.ent.posZ = 0.0;
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_SPAWNER, 7010,
              12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f)
              && gm_runtime_set_world_random_seed48(&r, 0),
          "spawn inactive spawner cart");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart),
          "read inactive spawner cart");
    printf("Z0 %d %012llx\n", cart.spawner_delay,
           (unsigned long long)r.world_random_seed48);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 66, 1),
          "active spawner minecart fixture");
    r.player.ent.posX = 12.5 - r.ox;
    r.player.ent.posY = 78.0;
    r.player.ent.posZ = 8.5 - r.oz;
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_SPAWNER, 7011,
              12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f)
              && gm_runtime_set_world_random_seed48(&r, 0),
          "spawn active spawner cart");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart),
          "read active spawner cart");
    printf("Z1 %d %012llx\n", cart.spawner_delay,
           (unsigned long long)r.world_random_seed48);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 66, 1),
          "spawner minecart reset fixture");
    r.player.ent.posX = 12.5 - r.ox;
    r.player.ent.posY = 78.0;
    r.player.ent.posZ = 8.5 - r.oz;
    CHECK(gm_runtime_spawn_minecart_fixture(
              &r, GM_MINECART_SPAWNER, 7012,
              12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f)
              && gm_runtime_minecart_set_spawner_state(
                  &r, 7012, GM_MOB_PIG, 0, 7, 11, 1, 0, 4, 16)
              && gm_runtime_set_world_random_seed48(&r, 0),
          "spawn reset-boundary spawner cart");
    gm_runtime_tick_minecarts(&r);
    CHECK(gm_runtime_minecart_get(&r, 0, &cart),
          "read reset-boundary spawner cart");
    printf("ZR %d %012llx\n", cart.spawner_delay,
           (unsigned long long)r.world_random_seed48);
    {
        static const unsigned char empty_nbt[4] = {10, 0, 0, 0};
        for (int index = 0; index < 17; ++index)
            CHECK(gm_runtime_minecart_add_spawner_potential(
                      &r, 7012,
                      index & 1 ? GM_MOB_SKELETON : GM_MOB_PIG,
                      index + 2, empty_nbt, sizeof empty_nbt, 1),
                  "minecart spawner potentials grow beyond former 16-row limit");
    }
    {
        char checkpoint[] = "game/.minecart-spawner-XXXXXX";
        int fd = mkstemp(checkpoint);
        CHECK(fd >= 0 && close(fd) == 0
                  && gm_runtime_write_checkpoint(&r, checkpoint)
                  && gm_runtime_load_checkpoint(&r, checkpoint),
              "spawner minecart checkpoint round trip");
        CHECK(unlink(checkpoint) == 0
                  && gm_runtime_minecart_get(&r, 0, &cart)
                  && cart.kind == GM_MINECART_SPAWNER
                  && cart.spawner_entity_type == GM_MOB_PIG
                  && cart.spawner_delay == 8
                  && cart.spawner_min_delay == 7
                  && cart.spawner_max_delay == 11
                  && cart.spawner_spawn_count == 1
                  && cart.spawner_max_nearby == 0
                  && cart.spawner_spawn_range == 4
                  && cart.spawner_activate_range == 16
                  && cart.spawner_potential_count == 18
                  && cart.spawner_potential_cap > 16
                  && cart.spawner_potentials[0].type == GM_MOB_PIG
                  && cart.spawner_potentials[0].weight == 1
                  && cart.spawner_potentials[17].type == GM_MOB_PIG
                  && cart.spawner_potentials[17].weight == 18
                  && r.world_random_seed48 == UINT64_C(0x3bb194f24a25),
              "spawner minecart checkpoint retains exact scalar/RNG state");
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r) && track(&r, 157, 9)
              && gm_runtime_spawn_minecart_fixture(
                  &r, GM_MINECART_COMMAND, 7020,
                  12.5, 78.0625, 8.5, 0.0, 0.0, 0.0, 0.0f)
              && gm_runtime_minecart_set_command_state(
                  &r, 7020, "Searge", "cart-oracle", 0, 1,
                  "{\"text\":\"seed\"}"),
          "command minecart activator fixture");
    for (int tick = 1; tick <= 9; ++tick) {
        const unsigned char *command, *output;
        size_t command_len, output_len;
        gm_runtime_tick_minecarts(&r);
        CHECK(gm_runtime_minecart_get(&r, 0, &cart)
                  && gm_runtime_armor_stand_string(
                      &r, cart.command_tag_id, &command, &command_len)
                  && gm_runtime_armor_stand_string(
                      &r, cart.command_last_output_tag_id,
                      &output, &output_len),
              "read command minecart payload");
        printf("J%d %d %d %d %.*s %.*s\n",
               tick, cart.ticks_existed,
               cart.command_activator_cooldown,
               cart.command_success_count,
               (int)command_len, (const char *)command,
               (int)output_len, (const char *)output);
    }
    {
        char checkpoint[] = "game/.minecart-command-XXXXXX";
        int fd = mkstemp(checkpoint);
        CHECK(fd >= 0 && close(fd) == 0
                  && gm_runtime_write_checkpoint(&r, checkpoint)
                  && gm_runtime_load_checkpoint(&r, checkpoint),
              "command minecart checkpoint round trip");
        CHECK(unlink(checkpoint) == 0
                  && gm_runtime_minecart_get(&r, 0, &cart)
                  && cart.ticks_existed == 9
                  && cart.command_activator_cooldown == 8
                  && cart.command_success_count == 1
                  && cart.command_track_output == 1,
              "command minecart checkpoint retains clocks and payload");
    }
    gm_runtime_destroy(&r);

    puts("minecart_live: PASS (rails, derailment, collision, callbacks)");
    return 0;
}
