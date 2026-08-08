#include "game/runtime.h"
#include "tile_entity_brewing.h"

#include <stdio.h>
#include <stdint.h>
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
    gm_runtime_set_pose(r, 8.5, 78.0, 8.5, 0.0f, 0.0f);
    return 1;
}

static int container_at(
        const GmRuntime *r, int x, int y, int z,
        GmRuntimeStaticContainer *out) {
    int count = gm_runtime_static_container_count(r);
    for (int i = 0; i < count; ++i) {
        GmRuntimeStaticContainer value;
        if (gm_runtime_static_container_get(r, i, &value)
                && value.wx == x && value.wy == y && value.wz == z) {
            if (out) *out = value;
            return 1;
        }
    }
    return 0;
}

static void tick(GmRuntime *r, int count) {
    GmAction action;
    memset(&action, 0, sizeof action);
    while (count-- > 0)
        gm_runtime_tick(r, action);
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

static void event_pair(const GmRuntime *r, int *first, int *second) {
    GmRuntimeWorldEvent event;
    *first = -1;
    *second = -1;
    if (gm_runtime_world_event_get(r, 0, &event)) *first = event.id;
    if (gm_runtime_world_event_get(r, 1, &event)) *second = event.id;
}

static int sound_id(const GmRuntime *r, int index) {
    GmRuntimeSoundEvent event;
    return gm_runtime_sound_event_get(r, index, &event) ? event.sound : -1;
}

int main(void) {
    GmRuntime r;
    GmRuntimeStaticContainer source, destination;
    const int x = 12, y = 78, z = 8;

    CHECK(init_flat(&r), "initialize hopper fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 154, 5),
          "place east-facing source hopper");
    CHECK(gm_runtime_set_block(&r, x + 1, y, z, 23, 2),
          "place destination dispenser");
    CHECK(gm_runtime_static_container_set_slot(
              &r, 0, x, y, z, 0, 1, 3, 0),
          "load source hopper");
    CHECK(gm_runtime_hopper_set_transfer_state(
              &r, 0, x, y, z, 0, 0),
          "arm source hopper");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && container_at(&r, x + 1, y, z, &destination)
              && source.slots[0].count == 2
              && destination.slots[0].item == 1
              && destination.slots[0].count == 1
              && source.transfer_cooldown == 8,
          "one item transfers and starts the exact cooldown");
    printf("A 1 %d %d %d %lld\n", source.slots[0].count,
           destination.slots[0].count, source.transfer_cooldown,
           source.ticked_game_time);
    tick(&r, 7);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].count == 2
              && source.transfer_cooldown == 1,
          "seven cooldown ticks do not transfer");
    printf("A 8 %d %d %d %lld\n", source.slots[0].count,
           destination.slots[0].count, source.transfer_cooldown,
           source.ticked_game_time);
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && container_at(&r, x + 1, y, z, &destination)
              && source.slots[0].count == 1
              && destination.slots[0].count == 2
              && source.transfer_cooldown == 8,
          "eighth cooldown tick transfers exactly once");
    printf("A 9 %d %d %d %lld\n", source.slots[0].count,
           destination.slots[0].count, source.transfer_cooldown,
           source.ticked_game_time);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize powered hopper fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 154, 13)
              && gm_runtime_set_block(&r, x + 1, y, z, 158, 2),
          "place powered hopper and dropper");
    CHECK(gm_runtime_static_container_set_slot(
              &r, 0, x, y, z, 0, 4, 1, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y, z, 0, 0),
          "load powered hopper");
    tick(&r, 2);
    CHECK(container_at(&r, x, y, z, &source)
              && container_at(&r, x + 1, y, z, &destination)
              && source.slots[0].count == 1
              && isr_is_empty(&destination.slots[0])
              && source.transfer_cooldown == 0,
          "powered hopper normalizes elapsed cooldown but stays disabled");
    printf("P 2 %d %d %d %lld\n", source.slots[0].count,
           destination.slots[0].count, source.transfer_cooldown,
           source.ticked_game_time);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize pull-chain fixture");
    CHECK(gm_runtime_set_block(&r, x, y + 1, z, 154, 0)
              && gm_runtime_set_block(&r, x, y, z, 154, 5)
              && gm_runtime_set_block(&r, x + 1, y, z, 23, 2),
          "place vertical hopper chain");
    CHECK(gm_runtime_static_container_set_slot(
              &r, 0, x, y + 1, z, 0, 5, 2, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y + 1, z, 0, 0)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y, z, 0, 0),
          "load and arm hopper chain");
    tick(&r, 1);
    CHECK(container_at(&r, x, y + 1, z, &source)
              && container_at(&r, x, y, z, &destination)
              && source.slots[0].count == 1
              && destination.slots[0].item == 5
              && destination.slots[0].count == 1,
          "lower hopper pulls one item after its outgoing phase");
    printf("C 1 %d %d %d %d %lld\n", source.slots[0].count,
           destination.slots[0].count, source.transfer_cooldown,
           destination.transfer_cooldown, destination.ticked_game_time);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize dropped-item fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 154, 5)
              && gm_runtime_hopper_set_transfer_state(
                  &r, 0, x, y, z, 0, 0),
          "place and arm capture hopper");
    CHECK(gm_runtime_spawn_item_fixture(
              &r, 7001, x + 0.5, y + 1.0, z + 0.5,
              0.0, 0.0, 0.0, 264, 3, 0, 0, 20, 1),
          "spawn stationary capture stack");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].item == 264
              && source.slots[0].count == 3
              && r.entities.n_active == 0
              && source.transfer_cooldown == 8,
          "hopper captures the complete dropped stack and retires its entity");
    printf("I 1 %d %d %d\n", source.slots[0].count,
           r.entities.n_active == 0 ? 1 : 0, source.transfer_cooldown);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize scheduled dropper fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 158, 13)
              && gm_runtime_set_block(&r, x + 1, y, z, 23, 2),
          "place triggered east dropper and destination inventory");
    CHECK(gm_runtime_static_container_set_slot(
              &r, 0, x, y, z, 0, 1, 2, 0),
          "load the one occupied dropper slot");
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 158, 1, 0, 0),
          "schedule exact dropper callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && container_at(&r, x + 1, y, z, &destination)
              && source.slots[0].count == 1
              && destination.slots[0].item == 1
              && destination.slots[0].count == 1,
          "dropper inserts exactly one item into the facing inventory");
    printf("D 1 %d %d\n", source.slots[0].count,
           destination.slots[0].count);
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize default dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13),
          "place triggered east dispenser");
    CHECK(gm_runtime_static_container_set_slot(
              &r, 0, x, y, z, 0, 1, 2, 0),
          "load the one occupied plain-stone dispenser slot");
    CHECK(gm_runtime_set_world_random_seed48(
              &r, (UINT64_C(123) ^ UINT64_C(0x5deece66d))
                  & ((UINT64_C(1) << 48) - 1))
              && gm_runtime_set_world_random_gaussian(&r, 0, 0.0),
          "seed exact dispenser World.rand state");
    r.math_random_seed48 = UINT64_C(0x123456789abc);
    r.next_entity_id = 9001;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule exact default dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].count == 1
              && r.entities.n_active == 1
              && gm_runtime_world_event_count(&r) == 2,
          "default dispenser ejects one live item and two world events");
    CHECK(gm_runtime_sound_event_count(&r) == 1
              && sound_id(&r, 0) == GM_SOUND_DISPENSER_DISPENSE,
          "default dispenser emits one resolved block sound");
    {
        const GmLiveEnt *entity = &r.entities.ents[0];
        printf(
            "E 1 %016llx %016llx %016llx %016llx %016llx %016llx "
            "%08x %08x %d %llu %d %016llx %d\n",
            dbits(entity->x), dbits(entity->y), dbits(entity->z),
            dbits(entity->mx), dbits(entity->my), dbits(entity->mz),
            fbits(entity->yaw), fbits(entity->hover_start),
            source.slots[0].count,
            (unsigned long long)r.world_random_seed48,
            r.world_random_have_gaussian,
            dbits(r.world_random_gaussian),
            gm_runtime_world_event_count(&r));
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize TNT dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 46, 2, 0),
          "load east-facing TNT dispenser");
    r.math_random_seed48 = UINT64_C(0x13579bdf2468);
    r.next_entity_id = 9101;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule TNT dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].count == 1
              && r.primed_tnt_count == 1,
          "TNT dispenser consumes one item and primes one entity");
    {
        const GmRuntimePrimedTnt *entity = NULL;
        int first, second;
        for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i)
            if (r.primed_tnt[i].active) entity = &r.primed_tnt[i];
        CHECK(entity != NULL, "find dispensed TNT entity");
        event_pair(&r, &first, &second);
        printf("X T %d %016llx %016llx %016llx %016llx %016llx "
               "%016llx %d %d %d\n",
               source.slots[0].count,
               dbits(entity->x), dbits(entity->y), dbits(entity->z),
               dbits(entity->vx), dbits(entity->vy), dbits(entity->vz),
               entity->fuse, first, second);
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize fire-charge dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 385, 2, 0)
              && gm_runtime_set_world_random_seed48(
                  &r, (UINT64_C(777) ^ UINT64_C(0x5deece66d))
                      & ((UINT64_C(1) << 48) - 1))
              && gm_runtime_set_world_random_gaussian(&r, 0, 0.0)
              && gm_runtime_set_next_fireball_random_state(
                  &r, UINT64_C(0x2468ace13579), 0, 0.0),
          "load and seed fire-charge dispenser");
    r.next_entity_id = 9201;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule fire-charge dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].count == 1,
          "fire-charge dispenser consumes one item");
    {
        const GmRuntimeProjectile *entity = NULL;
        int first, second;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (r.projectiles[i].active && r.projectiles[i].type == 3)
                entity = &r.projectiles[i];
        CHECK(entity != NULL, "find dispensed small fireball");
        event_pair(&r, &first, &second);
        printf("X C %d %016llx %016llx %016llx %llu %d %d %d\n",
               source.slots[0].count,
               dbits(entity->x), dbits(entity->y), dbits(entity->z),
               (unsigned long long)r.world_random_seed48,
               r.world_random_have_gaussian, first, second);
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize potion dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 438, 1, TB_PT_SWIFTNESS)
              && gm_runtime_set_next_potion_random_state(
                  &r, UINT64_C(0x369cf147258a), 0, 0.0),
          "load and seed splash-potion dispenser");
    r.next_entity_id = 9301;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule potion dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && isr_is_empty(&source.slots[0]),
          "potion dispenser consumes one item");
    {
        const GmRuntimeProjectile *entity = NULL;
        int first, second;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (r.projectiles[i].active && r.projectiles[i].type == 6)
                entity = &r.projectiles[i];
        CHECK(entity != NULL && entity->potion_item == 438
                  && entity->potion_type == TB_PT_SWIFTNESS,
              "dispensed potion retains item and potion payload");
        event_pair(&r, &first, &second);
        printf("X P %d %d %d %d\n", source.slots[0].count,
               entity->potion_item, first, second);
    }
    gm_runtime_destroy(&r);

    {
        static const int items[3] = {344, 332, 384};
        static const int kinds[3] = {7, 8, 9};
        for (int q = 0; q < 3; ++q) {
            CHECK(init_flat(&r), "initialize throwable dispenser fixture");
            CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0, items[q], 2, 0)
                      && gm_runtime_set_next_potion_random_state(
                          &r, UINT64_C(0x4711a5c39d27) + q, 0, 0.0),
                  "load and seed throwable dispenser");
            r.next_entity_id = 9351 + q;
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule throwable dispenser callback");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source)
                      && source.slots[0].count == 1,
                  "throwable dispenser consumes one item");
            const GmRuntimeProjectile *entity = NULL;
            int first, second;
            for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
                if (r.projectiles[i].active
                        && r.projectiles[i].type == kinds[q])
                    entity = &r.projectiles[i];
            CHECK(entity != NULL && entity->potion_item == items[q],
                  "throwable dispenser retains projectile kind and item");
            event_pair(&r, &first, &second);
            printf("X Q %d %d %d %d %d\n", items[q],
                   source.slots[0].count, kinds[q], first, second);
            gm_runtime_destroy(&r);
        }
    }

    CHECK(init_flat(&r), "initialize firework dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 401, 2,
                  ic_firework_meta(2, 0))
              && gm_runtime_set_next_firework_random_state(
                  &r, UINT64_C(0x48ace13579bd), 0, 0.0),
          "load and seed firework dispenser");
    r.next_entity_id = 9401;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule firework dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].count == 1
              && r.firework_count == 1,
          "firework dispenser consumes one item and launches one rocket");
    {
        const GmRuntimeFirework *entity = NULL;
        int first, second;
        for (int i = 0; i < GM_RUNTIME_FIREWORKS; ++i)
            if (r.fireworks[i].active) entity = &r.fireworks[i];
        CHECK(entity != NULL && entity->flight == 2 && entity->age == 1,
              "dispensed firework retains payload and advances once");
        event_pair(&r, &first, &second);
        printf("X F %d %d %d %d\n", source.slots[0].count,
               entity->age, first, second);
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize bucket dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, IC_WATER_BUCKET, 1, 0),
          "load water-bucket dispenser");
    gm_runtime_set_time(&r, 1);
    r.tick = 1;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule bucket dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].item == IC_BUCKET
              && source.slots[0].count == 1
              && gm_world_block(r.world, x + 1, y, z) == 8,
          "water bucket places flowing source and becomes empty bucket");
    {
        int first, second;
        event_pair(&r, &first, &second);
        printf("X W %d %d %d %d %d\n", source.slots[0].item,
               source.slots[0].count,
               gm_world_block(r.world, x + 1, y, z), first, second);
    }
    gm_runtime_destroy(&r);

    CHECK(init_flat(&r), "initialize boat dispenser fixture");
    CHECK(gm_runtime_set_block(&r, x + 1, y, z, 8, 0)
              && gm_runtime_set_block(&r, x, y, z, 23, 13)
              && gm_runtime_static_container_set_slot(
                  &r, 0, x, y, z, 0, 333, 2, 0),
          "load boat dispenser facing water");
    r.next_entity_id = 9501;
    CHECK(gm_runtime_schedule_tick(&r, x, y, z, 23, 1, 0, 0),
          "schedule boat dispenser callback");
    tick(&r, 1);
    CHECK(container_at(&r, x, y, z, &source)
              && source.slots[0].count == 1,
          "boat dispenser consumes one boat");
    {
        const EwStore *store = r.mobs.current ? &r.mobs.b : &r.mobs.a;
        int boat = -1, first, second;
        for (int i = 0; i < EW_MAX_ENTITIES; ++i)
            if (store->alive[i] && store->type[i] == EW_TYPE_BOAT)
                boat = i;
        CHECK(boat >= 0, "find dispensed boat");
        event_pair(&r, &first, &second);
        printf("X B %d %016llx %016llx %016llx %08x %d %d\n",
               source.slots[0].count,
               dbits(store->x[boat]), dbits(store->y[boat]),
               dbits(store->z[boat]), fbits(store->yaw[boat]),
               first, second);
    }
    gm_runtime_destroy(&r);

    {
        static const int targets[3] = {0, 1, 46};
        static const int expected[3] = {51, 1, 0};
        static const int damage[3] = {8, 7, 7};
        for (int q = 0; q < 3; ++q) {
            CHECK(init_flat(&r), "initialize flint dispenser fixture");
            CHECK(gm_runtime_set_block(&r, x + 1, y - 1, z, 1, 0)
                      && gm_runtime_set_block(
                          &r, x + 1, y, z, targets[q], 0)
                      && gm_runtime_set_block(&r, x, y, z, 23, 13)
                      && gm_runtime_static_container_set_slot(
                          &r, 0, x, y, z, 0, 259, 1, 7),
                  "load flint dispenser and target");
            CHECK(gm_runtime_schedule_tick(
                      &r, x, y, z, 23, 1, 0, 0),
                  "schedule flint dispenser callback");
            tick(&r, 1);
            CHECK(container_at(&r, x, y, z, &source)
                      && source.slots[0].count == 1
                      && source.slots[0].meta == damage[q]
                      && gm_world_block(r.world, x + 1, y, z) == expected[q],
                  "flint dispenser preserves exact target and durability");
            CHECK((q == 2 ? r.primed_tnt_count == 1
                          : r.primed_tnt_count == 0),
                  "flint TNT target primes only the TNT case");
            CHECK(gm_runtime_sound_event_count(&r) == 1
                      && sound_id(&r, 0) == (q == 1
                          ? GM_SOUND_DISPENSER_FAIL
                          : GM_SOUND_DISPENSER_DISPENSE),
                  "flint optional behavior resolves success/failure sound");
            {
                int first, second;
                event_pair(&r, &first, &second);
                printf("X L %d %d %d %d %d %d\n", q,
                       source.slots[0].count, source.slots[0].meta,
                       gm_world_block(r.world, x + 1, y, z),
                       first, second);
            }
            gm_runtime_destroy(&r);
        }
    }

    puts("hopper_live: PASS (cooldown, transfer, chain, power, item capture)");
    return 0;
}
