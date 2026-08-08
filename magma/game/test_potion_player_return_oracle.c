#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "game/runtime.h"
#include "tile_entity_brewing.h"

static unsigned float_bits(float value) {
    unsigned bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static const unsigned char custom_drink_tag[] = {
    10, 0, 0,
    9, 0, 19,
        'C','u','s','t','o','m','P','o','t','i','o','n','E','f','f','e','c','t','s',
        10, 0, 0, 0, 2,
        1, 0, 2, 'I','d', 5,
        1, 0, 9, 'A','m','p','l','i','f','i','e','r', 1,
        3, 0, 8, 'D','u','r','a','t','i','o','n', 0, 0, 1, 44,
        1, 0, 7, 'A','m','b','i','e','n','t', 1,
        1, 0, 13,
            'S','h','o','w','P','a','r','t','i','c','l','e','s', 0,
        0,
        1, 0, 2, 'I','d', 23,
        1, 0, 9, 'A','m','p','l','i','f','i','e','r', 2,
        3, 0, 8, 'D','u','r','a','t','i','o','n', 0, 0, 0, 1,
        1, 0, 7, 'A','m','b','i','e','n','t', 0,
        1, 0, 13,
            'S','h','o','w','P','a','r','t','i','c','l','e','s', 1,
        0,
    0
};

static int top_block_y(const GmRuntime *r, int x, int z) {
    for (int y = 20; y >= 0; --y)
        if (gm_world_block(r->world, x, y, z) != 0)
            return y;
    return -1;
}

int main(void) {
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    GmRuntimeProjectile *potion = NULL;
    PtMobEffect custom[2] = {{12, 101, 0}, {6, 1, 0}};
    unsigned char flags[2] = {1, 2};
    char err[256];
    int top;
    memset(&idle, 0, sizeof idle);
    gm_config_defaults(&cfg);
    cfg.world = GM_WORLD_SUPERFLAT;
    cfg.brewing = 1;
    cfg.mobs = 1;
    if (!gm_runtime_init(&r, &cfg, err, sizeof err)) return 2;
    top = top_block_y(&r, 8, 8);
    if (top < 0) return 3;
    gm_runtime_set_pose_state(
        &r, 8.5, (double)top + 1.0, 8.5, 0.0F, 0.0F,
        0.0, 0.0, 0.0, 1, 0.0F);
    gm_runtime_set_vitals(&r, 20.0F, 20);
    if (!gm_runtime_spawn_potion_fixture(
            &r, 810, TB_SPLASH_POTION, TB_PT_STRONG_HARMING,
            8.5, (double)top + 1.8, 7.5, 0.0, 0.0, 2.0, 0))
        return 4;
    if (!gm_runtime_set_potion_payload(
            &r, 810, 0x315A7D, 1, custom, 2, flags, 0))
        return 5;
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (r.projectiles[i].active && r.projectiles[i].eid == 810)
            potion = &r.projectiles[i];
    if (!potion) return 6;
    for (int tick = 0; tick < 5; ++tick) {
        if (tick == 1) {
            potion->x = 8.5;
            potion->y = (double)top + 1.8;
            potion->z = 20.5;
            potion->vx = potion->vy = potion->vz = 0.0;
        } else if (tick == 4) {
            potion->x = 8.5;
            potion->y = (double)top + 1.8;
            potion->z = 7.5;
            potion->vx = potion->vy = 0.0;
            potion->vz = 2.0;
        }
        gm_runtime_tick(&r, idle);
        printf("R %d %d %d %d %08x\n",
               potion->age, !potion->active, potion->ignore_player,
               potion->ignore_player_time, float_bits(r.vitals.health));
    }
    printf("H %d %d\n", r.mobs.player_hurt_time,
           r.mobs.player_hurt_resistant);
    for (int index = 0; index < r.potion_count; ++index)
        printf("P %d %d %d %d %d\n",
               r.potions[index].id, r.potions[index].amplifier,
               r.potions[index].duration, r.potions[index].ambient,
               !r.potions[index].hide_particles);
    gm_runtime_potions_clear(&r);
    {
        GmAction drink_action;
        ICStack drink = ic_mk(TB_POTION, 1, TB_PT_STRONG_SWIFTNESS);
        drink.tag_id = gm_runtime_stack_tag_intern(
            &r, custom_drink_tag, sizeof custom_drink_tag);
        if (drink.tag_id <= 0) return 7;
        memset(&drink_action, 0, sizeof drink_action);
        drink_action.use = 1;
        r.player.inv.current_item = 0;
        isr_set_stack(&r.player.inv, 0, drink);
        for (int tick = 0; tick < 32; ++tick)
            gm_runtime_tick(&r, drink_action);
        drink = isr_get_stack(&r.player.inv, 0);
        printf("D %d %d %d\n", drink.item, drink.count, drink.meta);
        for (int index = 0; index < r.potion_count; ++index)
            printf("Q %d %d %d %d %d\n",
                   r.potions[index].id, r.potions[index].amplifier,
                   r.potions[index].duration, r.potions[index].ambient,
                   !r.potions[index].hide_particles);
    }
    gm_runtime_potions_clear(&r);
    gm_runtime_set_vitals(&r, r.vitals.health, 11);
    gm_runtime_set_food_stats(&r, 2.5F, 0.0F);
    if (!gm_runtime_potion_add(&r, 23, 2, 1)
            || !gm_runtime_potion_add(&r, 26, 1, 2)
            || !gm_runtime_potion_add(&r, 27, 0, 2))
        return 8;
    gm_runtime_tick(&r, idle);
    {
        int saturation_alive = 0;
        for (int index = 0; index < r.potion_count; ++index)
            if (r.potions[index].id == 23) saturation_alive = 1;
        printf("S %d %08x %08x %d\n",
               r.vitals.foodLevel, float_bits(r.vitals.saturation),
               float_bits(r.player_luck), saturation_alive);
    }
    gm_runtime_destroy(&r);
    return 0;
}
