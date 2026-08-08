#include "game/runtime.h"

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static uint64_t java_lcg_steps(uint64_t seed, int steps)
{
    for (int i = 0; i < steps; ++i)
        seed = (seed * UINT64_C(0x5DEECE66D) + UINT64_C(0xB))
            & ((UINT64_C(1) << 48) - UINT64_C(1));
    return seed;
}

static void test_defense_math(int blast_protection, int resistance,
                              float expected_health, const char *label)
{
    GmMobLive mobs;
    PvStats vitals;
    IsrInv inventory;
    ICStack chest = ic_mk(311, 1, 0);

    gm_mobs_init(&mobs, 0);
    pv_init(&vitals);
    isr_init(&inventory);
    if (blast_protection) {
        chest.n_enchants = 1;
        chest.enchants[0].id = 3;
        chest.enchants[0].level = 4;
    }
    isr_set_stack(&inventory, ISR_ARMOR_CHEST, chest);
    if (resistance)
        mobs.player_resistance_amplifier = 0;
    CHECK(gm_mobs_attack_player_source(
              &mobs, (struct PvStats *)&vitals, &inventory,
              3.0f, 0, GM_DAMAGE_SOURCE_EXPLOSION) == 2,
          label);
    chest = isr_get_stack(&inventory, ISR_ARMOR_CHEST);
    CHECK(fabsf(vitals.health - expected_health) < 1.0e-6f
              && mobs.player_hurt_time == 10,
          label);
    CHECK(chest.item == 311 && chest.count == 1 && chest.meta == 1
              && chest.n_enchants == (blast_protection ? 1 : 0)
              && (!blast_protection
                  || (chest.enchants[0].id == 3
                      && chest.enchants[0].level == 4)),
          label);
}

static void test_item_damage_exceptions(void)
{
    GmLiveSim items;
    GmLiveExplosionTarget targets[1];
    memset(&items, 0, sizeof items);
    CHECK(gm_live_spawn_item_exact(
              &items, 1, 10.5, 83.0, 8.5,
              0.0, 0.0, 0.0, 0.0F,
              1, 1, 0, 0, 32767, 1),
          "lethal item fixture spawns");
    CHECK(gm_live_explosion_targets(&items, targets, 1) == 1
              && targets[0].eid == 1
              && targets[0].box.minX == 10.375
              && targets[0].box.maxY == 83.25,
          "item explosion target retains exact quarter-cube AABB");
    CHECK(!gm_live_apply_explosion(
              &items, targets[0].slot, 9.0F, -0.2, 0.1, 0.0)
              && items.n_active == 0,
          "lethal item explosion retires the entity");
    CHECK(gm_live_spawn_item_exact(
              &items, 2, 9.5, 83.0, 8.5,
              0.0, 0.0, 0.0, 0.0F,
              399, 1, 0, 0, 32767, 1),
          "Nether Star item fixture spawns");
    CHECK(gm_live_explosion_targets(&items, targets, 1) == 1
              && gm_live_apply_explosion(
                  &items, targets[0].slot, 9.0F, -0.2, 0.1, 0.0)
              && items.ents[targets[0].slot].health == 5
              && items.ents[targets[0].slot].mx == -0.2
              && items.ents[targets[0].slot].my == 0.1,
          "Nether Star rejects damage but retains explosion impulse");
}

static void test_bed_explosion_fire(void)
{
    const uint64_t world_seed48 = UINT64_C(135120319782334);
    const uint64_t random_zero_seed48 = UINT64_C(0x5DEECE66D);
    GmConfig cfg;
    GmRuntime r;
    char err[256];

    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "bed-fire runtime initializes");
    if (fail) return;
    gm_world_ensure(r.world, 0, 0, 0);
    for (int x = 0; x <= 16; ++x)
        for (int y = 92; y <= 108; ++y)
            for (int z = 0; z <= 16; ++z)
                gm_world_set_block_meta(r.world, x, y, z, 0, 0);
    gm_world_set_block_meta(r.world, 8, 99, 8, 49, 0);
    gm_world_set_block_meta(r.world, 8, 100, 8, 26, 0);
    r.dimension = -1;
    gm_runtime_set_pose(&r, 8.5, 100.0, 14.0, 180.0F, 0.0F);
    r.vitals.health = r.vitals.maxHealth = 200.0F;
    r.player.health = r.server_player.health = 200.0F;
    CHECK(gm_runtime_set_world_random_seed48(&r, world_seed48)
              && gm_runtime_set_next_explosion_random_seed48(
                  &r, random_zero_seed48),
          "bed-fire fixture restores both independent random cursors");
    CHECK(gm_runtime_use_block(&r, 8, 100, 8),
          "non-Overworld bed creates a flaming size-five explosion");
    CHECK(gm_world_block(r.world, 8, 99, 8) == 49
              && gm_world_block(r.world, 8, 100, 8) == 51,
          "seed-zero explosion RNG ignites the sole eligible air cell");
    CHECK(!r.next_explosion_random_valid,
          "flaming explosion consumes the injected constructor cursor");
    CHECK(r.world_random_seed48 == java_lcg_steps(world_seed48, 1355),
          "bed rays, sound pitch, and fire scheduling consume exact World.rand");
    gm_runtime_destroy(&r);
}

int main(void)
{
    const uint64_t blast_seed = UINT64_C(135120319782334);
    GmConfig cfg;
    GmRuntime r;
    GmAction idle;
    char err[256];

    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    cfg.mobs = 0;
    memset(&idle, 0, sizeof idle);
    idle.hotbar_sel = -1;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "TNT explosion runtime initializes");
    if (fail) return 1;

    for (int z = -8; z <= 24; ++z)
        for (int x = -8; x <= 24; ++x) {
            gm_world_set_block_meta(r.world, x, 77, z, 1, 0);
            for (int y = 78; y <= 91; ++y)
                gm_world_set_block_meta(r.world, x, y, z, 0, 0);
        }
    gm_world_set_block_meta(r.world, 9, 78, 8, 20, 0);
    r.vitals.health = 20.0f;
    r.vitals.maxHealth = 20.0f;
    r.vitals.foodLevel = 20;
    r.vitals.saturation = 5.0f;
    r.vitals.exhaustion = 0.05f;
    r.vitals.foodTimer = 0;
    r.mobs.player_hurt_time = 0;
    r.mobs.player_hurt_resistant = 0;
    r.mobs.player_last_damage = 0.0f;
    r.mobs.player_absorption = 0.0f;
    r.player.health = r.server_player.health = 20.0f;
    gm_runtime_set_pose_state(
        &r, 8.5, 78.0, 8.5, -180.0f, 0.0f,
        0.0, -0.0784000015258789, 0.0, 1, 0.0f);
    CHECK(gm_runtime_set_entity_id_cursor(&r, 9961)
              && gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9960, 13.5, 82.0, 8.5,
                  0.0, 0.0, 0.0, 1),
          "obstructed player-blast fixture spawns");

    gm_runtime_tick(&r, idle);
    CHECK(r.primed_tnt_count == 0
              && r.world_random_seed48
                  == java_lcg_steps(blast_seed, 1354),
          "obstructed blast retires with exact cursor");
    CHECK(gm_world_block(r.world, 9, 78, 8) == 20,
          "obstructed blast retains its same-seed glass occluder");
    CHECK(gm_world_block(r.world, 12, 77, 8) == 0
              && gm_world_block(r.world, 13, 77, 8) == 1
              && gm_world_block(r.world, 14, 77, 8) == 0,
          "obstructed blast matches its same-seed floor crater");
    CHECK(r.vitals.health == 16.0f
              && r.vitals.foodTimer == 1
              && fabsf(r.vitals.exhaustion - 0.15f) < 1.0e-7f
              && r.mobs.player_hurt_time == 9,
          "obstructed blast matches the damage lifecycle");
    CHECK(fabs(r.player.ent.posX - 8.404875) < 1.0e-15
              && fabs(r.player.ent.motionX
                  - (-0.05193825603276491)) < 1.0e-15
              && fabs(r.server_player.ent.motionX
                  - (-0.051960797397907814)) < 1.0e-15,
          "obstructed blast matches client and server knockback");

    gm_world_set_block_meta(r.world, 13, 82, 8, 49, 0);
    gm_world_set_block_meta(r.world, 13, 83, 8, 50, 5);
    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_item_fixture(
                  &r, 9961, 23.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1, 1, 0, 0, 32767, 1)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9962, 16.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1)
              && gm_runtime_spawn_mob_fixture(
                  &r, GM_MOB_PIG, 9963, 10.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 0.0F, 10.0F, 1, 0, 0, 0),
          "TNT-first open-air pig-blast fixture spawns");
    gm_runtime_tick(&r, idle);
    {
        GmMobExplosionTarget target[1];
        int count = gm_mobs_explosion_targets(
            &r.mobs, r.dimension, target, 1);
        CHECK(count == 1 && target[0].eid == 9963
                  && target[0].health == 4.0F
                  && target[0].hurt_time == 9
                  && target[0].hurt_resistant_time == 19,
              "outline-occluded TNT-first pig blast applies exact lifecycle");
        CHECK(count == 1
                  && fabs(target[0].vx
                      - (-0.16208969949315842)) < 1.0e-15
                  && fabs(target[0].vy
                      - 0.02009236855686323) < 1.0e-15
                  && target[0].vz == 0.0,
              "outline-occluded TNT-first pig blast applies exact damping");
        CHECK(gm_world_block(r.world, 13, 82, 8) == 49
                  && gm_world_block(r.world, 13, 83, 8) == 0,
              "outline occluder keeps support and loses exact torch");
        if (count == 1) {
            int slot = target[0].slot;
            r.mobs.a.alive[slot] = r.mobs.b.alive[slot] = 0;
            r.mobs.a.type[slot] = r.mobs.b.type[slot] = EW_TYPE_NONE;
            r.mobs.controlled_no_ai[slot] = 0;
            r.mobs.controlled_block_collisions[slot] = 0;
        }
        {
            const GmLiveEnt *item = NULL;
            for (int i = 0; i < GM_LIVE_MAX; ++i)
                if (r.entities.ents[i].active
                        && r.entities.ents[i].eid == 9961)
                    item = &r.entities.ents[i];
            CHECK(item && item->health == 1 && item->age == 1
                      && item->x == 23.5 && item->y == 83.0
                      && item->z == 8.5,
                  "item-first blast applies damage after its stationary tick");
            CHECK(item
                      && fabs(item->mx
                          - 0.12494932090351177) < 1.0e-15
                      && fabs(item->my
                          - 0.003413794015269717) < 1.0e-15
                      && item->mz == 0.0,
                  "item-first blast retains exact undamped impulse");
        }
    }

    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (r.entities.ents[i].active
                && r.entities.ents[i].eid == 9961) {
            r.entities.ents[i].active = 0;
            --r.entities.n_active;
        }
    gm_runtime_set_pose_state(
        &r, 0.5, 78.0, 8.5, -180.0f, 0.0f,
        0.0, -0.0784000015258789, 0.0, 1, 0.0f);
    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_arrow_fixture(
                  &r, 9969, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1, 0)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9970, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1)
              && gm_runtime_spawn_item_fixture(
                  &r, 9971, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1, 1, 0, 0, 32767, 1)
              && gm_runtime_spawn_boat_fixture(
                  &r, 9972, 1.5, 83.0, 8.5, 0.0F),
          "TNT-first surviving item/boat fixtures spawn");
    gm_runtime_tick(&r, idle);
    {
        const GmLiveEnt *item = NULL;
        for (int i = 0; i < GM_LIVE_MAX; ++i)
            if (r.entities.ents[i].active
                    && r.entities.ents[i].eid == 9971)
                item = &r.entities.ents[i];
        CHECK(item && item->health == 1 && item->age == 1
                  && fabs(item->x
                      - 1.3750506790964874) < 1.0e-15
                  && fabs(item->y
                      - 83.00341379401527) < 1.0e-15
                  && item->z == 8.5,
              "TNT-first item moves by the fresh impulse");
        CHECK(item
                  && fabs(item->mx
                      - (-0.12245033686866069)) < 1.0e-15
                  && fabs(item->my
                      - 0.003345518200077276) < 1.0e-15
                  && item->mz == 0.0,
              "TNT-first item applies exact same-tick damping");
    }
    {
        const GmRuntimeProjectile *arrow = NULL;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (r.projectiles[i].active
                    && r.projectiles[i].eid == 9969)
                arrow = &r.projectiles[i];
        CHECK(arrow && arrow->age == 1
                  && arrow->x == 1.5 && arrow->y == 83.0
                  && arrow->z == 8.5,
              "arrow-first blast retains its pre-explosion position");
        CHECK(arrow
                  && fabs(arrow->vx
                      - (-0.12499536788906258)) < 1.0e-15
                  && fabs(arrow->vy
                      - (-0.0003794502612004625)) < 1.0e-15
                  && arrow->vz == 0.0,
              "arrow-first blast applies exact raw impulse");
    }
    {
        GmMobExplosionTarget target[1];
        int count = gm_mobs_explosion_targets(
            &r.mobs, r.dimension, target, 1);
        CHECK(count == 1 && target[0].eid == 9972
                  && r.mobs.boat_damage[target[0].slot] == 39,
              "TNT-first boat survives exact damage lifecycle");
        CHECK(count == 1
                  && fabs(target[0].x
                      - 1.387742502803361) < 1.0e-15
                  && fabs(target[0].y
                      - 83.00814089616274) < 1.0e-15
                  && target[0].z == 8.5,
              "TNT-first boat moves by its damped impulse");
        CHECK(count == 1
                  && fabs(target[0].vx
                      - (-0.11225749719663904)) < 1.0e-15
                  && fabs(target[0].vy
                      - 0.008140896162744845) < 1.0e-15
                  && target[0].vz == 0.0,
              "TNT-first boat retains exact same-tick motion");
    }

    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_xp_fixture(
                  &r, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1, 9980, 0, 0, 0, -100)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9981, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1),
          "XP-first blast fixtures spawn");
    gm_runtime_tick(&r, idle);
    {
        const McOrb *orb = NULL;
        for (int i = 0; i < GM_XP_ORBS; ++i)
            if (!r.mobs.xp_orbs[i].dead
                    && r.mobs.xp_orbs[i].eid == 9980)
                orb = &r.mobs.xp_orbs[i];
        CHECK(orb && orb->health == 1
                  && orb->xpValue == 1 && orb->xpOrbAge == 1
                  && orb->delayBeforeCanPickup == 0
                  && orb->xpColor == 1 && orb->xpTargetColor == 0
                  && fabs(orb->posX
                      - 1.4950548948488454) < 1.0e-15
                  && fabs(orb->posY
                      - 82.949280010099) < 1.0e-15
                  && orb->posZ == 8.5,
              "XP-first blast retains exact pre-explosion physics state");
        CHECK(orb
                  && fabs(orb->motionX
                      - (-0.12902424922385383)) < 1.0e-15
                  && fabs(orb->motionY
                      - (-0.0434473581470098)) < 1.0e-15
                  && orb->motionZ == 0.0,
              "XP-first blast applies exact damage and raw impulse");
    }

    gm_runtime_set_total_time(&r, 140);
    gm_world_set_block_meta(r.world, 1, 83, 8, 12, 0);
    CHECK(gm_runtime_set_entity_id_cursor(&r, 9983)
              && gm_runtime_schedule_tick(
              &r, 1, 83, 8, 12, 142, 0,
              r.scheduled_tick_next_order)
              && gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9982, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 3),
          "TNT-first falling-sand fixtures spawn");
    gm_runtime_tick(&r, idle);
    CHECK(r.falling_block_count == 0 && r.primed_tnt_count == 1,
          "falling-sand fixture retains its first pre-dispatch boundary");
    gm_runtime_tick(&r, idle);
    CHECK(r.falling_block_count == 1 && r.primed_tnt_count == 1
              && r.falling_blocks[0].fall_time == 1
              && r.falling_blocks[0].x == 1.5
              && fabs(r.falling_blocks[0].y
                  - 82.96999999135733) < 1.0e-15
              && r.falling_blocks[0].vx == 0.0
              && fabs(r.falling_blocks[0].vy
                  - (-0.03919999988675116)) < 1.0e-15,
          "falling sand dispatches after TNT's non-exploding update");
    gm_runtime_tick(&r, idle);
    CHECK(r.falling_block_count == 1 && r.primed_tnt_count == 0
              && r.falling_blocks[0].fall_time == 2
              && fabs(r.falling_blocks[0].x
                  - 1.3763911663466868) < 1.0e-15
              && fabs(r.falling_blocks[0].y
                  - 82.90807990783813) < 1.0e-15
              && r.falling_blocks[0].z == 8.5
              && fabs(r.falling_blocks[0].vx
                  - (-0.12113665933789819)) < 1.0e-15
              && fabs(r.falling_blocks[0].vy
                  - (-0.06068168302984159)) < 1.0e-15
              && r.falling_blocks[0].vz == 0.0,
          "TNT-first falling sand moves and damps the exact fresh impulse");

    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9990, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1)
              && gm_runtime_spawn_small_fireball_fixture(
                  &r, 9991, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 0.0, 0.0, 0.0),
          "TNT-first small-fireball fixtures spawn");
    gm_runtime_tick(&r, idle);
    {
        const GmRuntimeProjectile *fireball = NULL;
        for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
            if (r.projectiles[i].active
                    && r.projectiles[i].eid == 9991)
                fireball = &r.projectiles[i];
        CHECK(r.primed_tnt_count == 0 && fireball && fireball->age == 1
                  && fabs(fireball->x
                      - 1.3750801534780397) < 1.0e-15
                  && fabs(fireball->y
                      - 83.00436104103332) < 1.0e-15
                  && fireball->z == 8.5,
              "TNT-first small fireball moves by the fresh impulse");
        CHECK(fireball
                  && fabs(fireball->vx
                      - (-0.11867385270670164)) < 1.0e-15
                  && fabs(fireball->vy
                      - 0.004142988929661038) < 1.0e-15
                  && fireball->vz == 0.0
                  && fireball->ax == 0.0
                  && fireball->ay == 0.0
                  && fireball->az == 0.0,
              "TNT-first small fireball applies exact motion damping");
        CHECK(fireball && fireball->yaw == 342.0F
                  && fireball->pitch == -0.39989015F,
              "TNT-first small fireball uses exact MathHelper rotation");
    }

    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9992, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9993, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 80),
          "source-first two-TNT fixtures spawn");
    gm_runtime_tick(&r, idle);
    {
        const GmRuntimePrimedTnt *target = NULL;
        for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i)
            if (r.primed_tnt[i].active && r.primed_tnt[i].eid == 9993)
                target = &r.primed_tnt[i];
        CHECK(r.primed_tnt_count == 1 && target && target->fuse == 79
                  && fabs(target->x
                      - 1.3750046321109366) < 1.0e-15
                  && fabs(target->y
                      - 82.95962055063286) < 1.0e-15
                  && target->z == 8.5,
              "source-first target TNT moves by the fresh impulse");
        CHECK(target
                  && fabs(target->vx
                      - (-0.12249546291537877)) < 1.0e-15
                  && fabs(target->vy
                      - (-0.03957186114996505)) < 1.0e-15
                  && target->vz == 0.0,
              "source-first target TNT applies exact gravity and damping");
    }

    for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i)
        if (r.primed_tnt[i].active && r.primed_tnt[i].eid == 9993) {
            r.primed_tnt[i].active = 0;
            --r.primed_tnt_count;
        }
    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9994, 1.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 80)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9995, 8.5, 83.0, 8.5,
                  0.0, 0.0, 0.0, 1),
          "target-first two-TNT fixtures spawn");
    gm_runtime_tick(&r, idle);
    {
        const GmRuntimePrimedTnt *target = NULL;
        for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i)
            if (r.primed_tnt[i].active && r.primed_tnt[i].eid == 9994)
                target = &r.primed_tnt[i];
        CHECK(r.primed_tnt_count == 1 && target && target->fuse == 79
                  && target->x == 1.5
                  && fabs(target->y
                      - 82.96000000089407) < 1.0e-15
                  && target->z == 8.5,
              "target-first target TNT retains its pre-blast position");
        CHECK(target
                  && fabs(target->vx
                      - (-0.1249617182537039)) < 1.0e-15
                  && fabs(target->vy
                      - (-0.04029341494275192)) < 1.0e-15
                  && target->vz == 0.0,
              "target-first target TNT retains the exact raw impulse");
    }

    for (int i = 0; i < GM_RUNTIME_PRIMED_TNT; ++i)
        if (r.primed_tnt[i].active) {
            r.primed_tnt[i].active = 0;
            --r.primed_tnt_count;
        }
    r.vitals.health = 20.0F;
    r.mobs.player_hurt_time = 0;
    r.mobs.player_hurt_resistant = 0;
    r.mobs.player_last_damage = 0.0F;
    r.player.health = r.server_player.health = 20.0F;
    gm_runtime_set_pose_state(
        &r, 0.5, 78.0, 8.5, -180.0F, 0.0F,
        0.0, -0.0784000015258789, 0.0, 1, 0.0F);
    CHECK(gm_runtime_set_world_random_seed48(&r, blast_seed)
              && gm_runtime_spawn_primed_tnt_fixture(
                  &r, 9996, 16.5, 88.0, 8.5,
                  0.0, 0.0, 0.0, 1)
              && gm_runtime_spawn_end_crystal_fixture(
                  &r, 9997, 9.5, 88.0, 8.5, 0, 1, 0, 0, 0, 0),
          "TNT-first End-crystal fixtures spawn");
    gm_runtime_tick(&r, idle);
    CHECK(r.primed_tnt_count == 0 && r.end_crystal_count == 0,
          "TNT-first hit retires source and End crystal");
    CHECK(r.world_random_seed48 == java_lcg_steps(blast_seed, 2708),
          "nested End-crystal blast consumes two exact explosion cursors");
    CHECK(r.vitals.health == 20.0F && r.mobs.player_hurt_time == 0
              && r.player.ent.posX == 0.5
              && r.player.ent.posY == 78.0
              && r.player.ent.posZ == 8.5,
          "distant nested End-crystal blast leaves the player unchanged");
    CHECK(gm_runtime_spawn_end_crystal_fixture(
              &r, 9998, 1.5, 83.0, 8.5, 0, 1, 0, 0, 0, 0),
          "standalone End-crystal fixture spawns");
    gm_runtime_tick(&r, idle);
    {
        GmRuntimeEndCrystal *crystal = NULL;
        for (int i = 0; i < GM_RUNTIME_END_CRYSTALS; ++i)
            if (r.end_crystals[i].active
                    && r.end_crystals[i].eid == 9998)
                crystal = &r.end_crystals[i];
        CHECK(r.end_crystal_count == 1 && crystal
                  && crystal->inner_rotation == 1
                  && crystal->show_bottom == 1
                  && crystal->x == 1.5 && crystal->y == 83.0
                  && crystal->z == 8.5,
              "ordinary standalone End-crystal tick advances render state");
    }
    CHECK(gm_runtime_set_dimension(&r, 1),
          "runtime enters the End for crystal-fire fixture");
    gm_world_ensure(r.world, 0, 0, 0);
    gm_world_set_block_meta(r.world, 9, 82, 8, 49, 0);
    gm_world_set_block_meta(r.world, 9, 83, 8, 0, 0);
    CHECK(gm_runtime_spawn_end_crystal_fixture(
              &r, 9999, 9.5, 83.0, 8.5, 0, 1, 1, 20, 99, -7),
          "End crystal-fire fixture spawns");
    gm_runtime_tick(&r, idle);
    CHECK(gm_world_block(r.world, 9, 82, 8) == 49
              && gm_world_block(r.world, 9, 83, 8) == 51,
          "End crystal creates fire above its inert support");
    {
        GmRuntimeEndCrystal *crystal = NULL;
        for (int i = 0; i < GM_RUNTIME_END_CRYSTALS; ++i)
            if (r.end_crystals[i].active
                    && r.end_crystals[i].eid == 9999)
                crystal = &r.end_crystals[i];
        CHECK(crystal && crystal->dimension == 1
                  && crystal->inner_rotation == 1
                  && crystal->has_beam == 1
                  && crystal->beam_x == 20 && crystal->beam_y == 99
                  && crystal->beam_z == -7,
              "End crystal advances while preserving its saved beam target");
    }
    {
        GmEntityView views[GM_RUNTIME_END_CRYSTALS];
        int count = gm_runtime_end_crystal_views(
            &r, views, GM_RUNTIME_END_CRYSTALS);
        CHECK(count == 1 && views[0].type == GM_ENTITY_CRYSTAL
                  && views[0].type == 31
                  && views[0].ent_id == 9999
                  && views[0].crystal_rot == 1.0F
                  && views[0].show_bottom == 1
                  && views[0].has_beam == 1
                  && views[0].beam_x == 20 && views[0].beam_y == 99
                  && views[0].beam_z == -7,
              "saved End crystal enters the live render-view stream");
    }

    /* A TNT blast marks an arena crystal, completes the crystal's nested
     * explosion, and only then notifies the dragon fight. Keep dragon and
     * player outside both blast diameters so the exact additional ten health
     * is isolated from ordinary explosion damage. */
    for (int i = 0; i < ED_NUM_CRYSTALS; ++i)
        r.dragon.state.arena.crystals[i].alive = 0;
    r.dragon.initialized = 1;
    r.dragon.state.arena.crystals[0].alive = 1;
    r.dragon.state.arena.crystals[0].x = 42.5;
    r.dragon.state.arena.crystals[0].y = 88.0;
    r.dragon.state.arena.crystals[0].z = 8.5;
    r.dragon.state.arena.dragon.alive = 1;
    r.dragon.state.arena.dragon.death_ticks = 0;
    r.dragon.state.arena.dragon.health = 100.0F;
    r.dragon.state.arena.dragon.max_health = 200.0F;
    r.dragon.state.arena.dragon.x = 11.5;
    r.dragon.state.arena.dragon.y = 88.0;
    r.dragon.state.arena.dragon.z = 8.5;
    r.dragon.state.arena.dragon.target_x = 11.5;
    r.dragon.state.arena.dragon.target_y = 88.0;
    r.dragon.state.arena.dragon.target_z = 8.5;
    r.dragon.state.arena.dragon.vx = 0.0;
    r.dragon.state.arena.dragon.vy = 0.0;
    r.dragon.state.arena.dragon.vz = 0.0;
    r.dragon.state.arena.dragon.phase = ED_PHASE_HOVER;
    r.dragon.state.arena.dragon.phase_ticks = 0;
    r.dragon.state.arena.dragon.heal_crystal_idx = 0;
    r.dragon.state.arena.player.x = r.player.ent.posX;
    r.dragon.state.arena.player.y = r.player.ent.posY;
    r.dragon.state.arena.player.z = r.player.ent.posZ;
    gm_world_ensure(r.world, 2, 0, 0);
    gm_world_ensure(r.world, 3, 0, 0);
    CHECK(gm_runtime_spawn_primed_tnt_fixture(
              &r, 10000, 42.5, 88.0, 8.5,
              0.0, 0.0, 0.0, 1),
          "arena healing-crystal TNT fixture spawns");
    gm_runtime_tick(&r, idle);
    if (r.dragon.state.arena.crystals[0].alive
            || r.dragon.state.arena.dragon.health != 90.0F)
        fprintf(stderr,
            "arena crystal result: alive=%d xyz=(%g,%g,%g) health=%g heal=%d phase=%d tnt=%d dim=%d init=%d\n",
            r.dragon.state.arena.crystals[0].alive,
            r.dragon.state.arena.crystals[0].x,
            r.dragon.state.arena.crystals[0].y,
            r.dragon.state.arena.crystals[0].z,
            r.dragon.state.arena.dragon.health,
            r.dragon.state.arena.dragon.heal_crystal_idx,
            r.dragon.state.arena.dragon.phase,r.primed_tnt_count,
            r.dimension,r.dragon.initialized);
    CHECK(!r.dragon.state.arena.crystals[0].alive
              && r.dragon.state.arena.dragon.health == 90.0F,
          "arena healing-crystal chain applies exact post-blast ten damage");

    /* Preserve arrow block-before-entity ordering while threading crystal
     * notification through the old short-circuit. */
    memset(r.end_crystals, 0, sizeof r.end_crystals);
    r.end_crystal_count = 0;
    r.dragon.state.arena.crystals[0].alive = 1;
    r.dragon.state.arena.crystals[0].x = 10.5;
    r.dragon.state.arena.crystals[0].y = 88.0;
    r.dragon.state.arena.crystals[0].z = 8.5;
    r.dragon.state.arena.dragon.health = 100.0F;
    r.dragon.state.arena.dragon.x = 41.5;
    r.dragon.state.arena.dragon.y = 88.0;
    r.dragon.state.arena.dragon.z = 8.5;
    r.dragon.state.arena.dragon.target_x = 41.5;
    r.dragon.state.arena.dragon.target_y = 88.0;
    r.dragon.state.arena.dragon.target_z = 8.5;
    r.dragon.state.arena.dragon.vx = 0.0;
    r.dragon.state.arena.dragon.vy = 0.0;
    r.dragon.state.arena.dragon.vz = 0.0;
    r.dragon.state.arena.dragon.phase = ED_PHASE_HOVER;
    r.dragon.state.arena.dragon.phase_ticks = 0;
    r.dragon.state.arena.dragon.heal_crystal_idx = 0;
    gm_world_set_block_meta(r.world, 9, 88, 8, 1, 0);
    CHECK(gm_runtime_spawn_arrow_fixture(
              &r, 10001, 8.5, 88.0, 8.5,
              0.0, 0.0, 0.0, 1, 0),
          "blocked arena-crystal arrow fixture spawns");
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (r.projectiles[i].active && r.projectiles[i].eid == 10001) {
            r.projectiles[i].controlled_stationary = 0;
            r.projectiles[i].vx = 2.0;
        }
    gm_runtime_tick(&r, idle);
    CHECK(r.dragon.state.arena.crystals[0].alive
              && r.dragon.state.arena.dragon.health == 100.0F,
          "solid block shields arena crystal from player arrow");
    gm_world_set_block_meta(r.world, 9, 88, 8, 0, 0);
    CHECK(gm_runtime_spawn_arrow_fixture(
              &r, 10002, 8.5, 88.0, 8.5,
              0.0, 0.0, 0.0, 1, 0),
          "unblocked arena-crystal arrow fixture spawns");
    for (int i = 0; i < GM_RUNTIME_PROJECTILES; ++i)
        if (r.projectiles[i].active && r.projectiles[i].eid == 10002) {
            r.projectiles[i].controlled_stationary = 0;
            r.projectiles[i].vx = 2.0;
        }
    gm_runtime_tick(&r, idle);
    CHECK(!r.dragon.state.arena.crystals[0].alive
              && r.dragon.state.arena.dragon.health == 90.0F,
          "unblocked player arrow explodes and notifies healing crystal");

    gm_runtime_destroy(&r);
    test_defense_math(
        0, 0, 17.816f, "diamond chestplate blast defense");
    test_defense_math(
        1, 0, 18.5148792f, "Blast Protection IV blast defense");
    test_defense_math(
        1, 1, 18.8119049f,
        "armor then Resistance then Blast Protection defense order");
    test_item_damage_exceptions();
    test_bed_explosion_fire();
    if (fail) return 1;
    puts("tnt_explosion: PASS");
    return 0;
}
