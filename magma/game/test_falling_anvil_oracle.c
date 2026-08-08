#include "game/runtime.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Narrow scheduled-anvil fixture.  Keep its JSON shape aligned with the
 * dragon-egg fixture: trace/test_falling_anvil.py is intentionally able to
 * compare the raw EntityFallingBlock trajectory without a tape. */
typedef struct {
    int step, fall_time, dead;
    double x, y, z, vx, vy, vz;
    int on_ground, collided_horizontally, collided_vertically;
    float fall_distance;
    uint64_t random_seed48;
} FallingRow;

typedef struct { GmRuntimeScheduledTick entry; long long delay; } ScheduledRow;

static uint64_t java_lcg_steps(uint64_t seed, int steps) {
    while (steps-- > 0)
        seed = (seed * UINT64_C(0x5DEECE66D) + UINT64_C(0xB))
            & ((UINT64_C(1) << 48) - UINT64_C(1));
    return seed;
}

static int snapshot_scheduled(const GmRuntime *r, ScheduledRow *rows, int cap) {
    int n = gm_runtime_scheduled_tick_count(r);
    if (n > cap) return -1;
    for (int i = 0; i < n; ++i) {
        if (!gm_runtime_scheduled_tick_get(r, i, &rows[i].entry)) return -1;
        rows[i].delay = rows[i].entry.time - r->clock.total_time;
    }
    return n;
}

static void write_scheduled(const ScheduledRow *rows, int n) {
    putchar('[');
    for (int i = 0; i < n; ++i) {
        const GmRuntimeScheduledTick *e = &rows[i].entry;
        if (i) putchar(',');
        printf("[%d,%d,%d,%d,%lld,%d,%d]", e->x, e->y, e->z, e->block,
            rows[i].delay, e->priority, i);
    }
    putchar(']');
}

static void write_ticked_item(const GmRuntime *r) {
    const GmLiveEnt *item = NULL;
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
        if (r->entities.ents[slot].active
                && r->entities.ents[slot].type == 0) {
            if (item) {
                fputs("null", stdout);
                return;
            }
            item = &r->entities.ents[slot];
        }
    if (!item) {
        fputs("null", stdout);
        return;
    }
    printf("{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
           "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,\"yaw\":%.9g,"
           "\"item\":%d,\"count\":%d,\"meta\":%d,\"age\":%d,"
           "\"pickup_delay\":%d}",
        item->eid, item->x, item->y, item->z,
        item->mx, item->my, item->mz, (double)item->yaw,
        item->item, item->count, item->meta, item->age,
        item->pickup_delay);
}

static void write_mob_drops(const GmRuntime *r, int first_eid) {
    int first = 1;
    putchar('[');
    for (int eid = first_eid; eid < r->next_entity_id; ++eid) {
        const GmLiveEnt *item = NULL;
        for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
            if (r->entities.ents[slot].active
                    && r->entities.ents[slot].type == 0
                    && r->entities.ents[slot].eid == eid) {
                item = &r->entities.ents[slot];
                break;
            }
        if (!item) continue;
        if (!first) putchar(',');
        first = 0;
        printf("{\"eid\":%d,\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
               "\"vx\":%.17g,\"vy\":%.17g,\"vz\":%.17g,"
               "\"yaw\":%.9g,\"hover_start\":%.9g,"
               "\"item\":%d,\"count\":%d,\"meta\":%d,"
               "\"age\":%d,\"pickup_delay\":%d,\"health\":%d,"
               "\"lifespan\":%d,\"on_ground\":%s,\"is_dead\":false}",
            item->eid, item->x, item->y, item->z,
            item->mx, item->my, item->mz, (double)item->yaw,
            (double)item->hover_start,
            item->item, item->count, item->meta, item->age,
            item->pickup_delay, item->health, item->lifespan,
            item->on_ground ? "true" : "false");
    }
    putchar(']');
}

static int fixture_mob_slot(const GmRuntime *r, int eid) {
    const EwStore *s = r->mobs.current ? &r->mobs.b : &r->mobs.a;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->id[slot] == eid)
            return slot;
    return -1;
}

static void write_mob_damage(const GmRuntime *r, int eid) {
    const EwStore *mobs = r->mobs.current ? &r->mobs.b : &r->mobs.a;
    int slot = fixture_mob_slot(r, eid);
    float width, height;
    int drop_entity_count = 0, xp_entity_count = 0;
    if (slot < 0) {
        fputs("null", stdout);
        return;
    }
    const char *type = mobs->type[slot] == GM_MOB_CHICKEN ? "chicken"
        : mobs->type[slot] == GM_MOB_SHEEP ? "sheep"
            : mobs->type[slot] == GM_MOB_COW ? "cow" : "pig";
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (r->entities.ents[i].active && r->entities.ents[i].type == 0)
            ++drop_entity_count;
    for (int i = 0; i < GM_XP_ORBS; ++i)
        if (!r->mobs.xp_orbs[i].dead && r->mobs.xp_orbs[i].xpValue > 0)
            ++xp_entity_count;
    ehs_size_scaled(mobs->type[slot], r->mobs.size[slot], &width, &height);
    const double half = (double)width * 0.5;
    printf("{\"eid\":%d,\"type\":\"%s\",\"health\":%.9g,"
           "\"hurt_resistant_time\":%d,\"hurt_time\":%d,"
           "\"max_hurt_time\":%d,\"last_damage\":%.9g,"
           "\"death_time\":%d,\"living_dead\":%s,"
           "\"entity_is_dead\":%s,\"drop_entity_count\":%d,"
           "\"xp_entity_count\":%d,\"entity_seed48\":%llu,"
           "\"recently_hit\":%d,\"attacking_player\":%s,"
           "\"fire_ticks\":%d,\"burning\":%s,"
           "\"fleece_color\":%d,\"sheared\":%s,"
           "\"aabb\":[%.17g,%.17g,%.17g,"
           "%.17g,%.17g,%.17g]}",
        mobs->id[slot], type, (double)mobs->health[slot],
        r->mobs.entity_hurt_resistant[slot],
        r->mobs.entity_hurt_time[slot], 10,
        (double)r->mobs.entity_last_damage[slot],
        r->mobs.entity_death_time[slot],
        r->mobs.entity_dead[slot] ? "true" : "false",
        mobs->alive[slot] ? "false" : "true",
        drop_entity_count, xp_entity_count,
        (unsigned long long)r->mobs.entity_random[slot].random.seed,
        r->mobs.entity_recently_hit[slot],
        r->mobs.entity_attacking_player[slot] ? "true" : "false",
        r->mobs.fire_ticks[slot],
        r->mobs.fire_ticks[slot] > 0 ? "true" : "false",
        r->mobs.sheep_data[slot] & 15,
        r->mobs.sheep_data[slot] & 16 ? "true" : "false",
        mobs->x[slot] - half, mobs->y[slot], mobs->z[slot] - half,
        mobs->x[slot] + half, mobs->y[slot] + (double)height,
        mobs->z[slot] + half);
}

static const char *mob_event_sound_name(int data) {
    switch (data) {
    case GM_MOB_SOUND_CHICKEN_HURT:
        return "minecraft:entity.chicken.hurt";
    case GM_MOB_SOUND_CHICKEN_DEATH:
        return "minecraft:entity.chicken.death";
    case GM_MOB_SOUND_PIG_HURT:
        return "minecraft:entity.pig.hurt";
    case GM_MOB_SOUND_PIG_DEATH:
        return "minecraft:entity.pig.death";
    case GM_MOB_SOUND_COW_HURT:
        return "minecraft:entity.cow.hurt";
    case GM_MOB_SOUND_COW_DEATH:
        return "minecraft:entity.cow.death";
    case GM_MOB_SOUND_SHEEP_HURT:
        return "minecraft:entity.sheep.hurt";
    default:
        return "minecraft:entity.sheep.death";
    }
}

static void write_mob_events(const GmRuntime *r) {
    int first = 1;
    putchar('[');
    for (int i = 0; i < gm_mobs_event_count(&r->mobs); ++i) {
        GmMobEvent event;
        if (!gm_mobs_event_get(&r->mobs, i, &event))
            continue;
        if (!first) putchar(',');
        first = 0;
        if (event.kind == GM_MOB_EVENT_ENTITY_STATUS) {
            printf("{\"kind\":\"status\",\"eid\":%d,\"status\":%d}",
                event.eid, event.data);
        } else if (event.kind == GM_MOB_EVENT_SOUND) {
            printf("{\"kind\":\"sound\",\"eid\":%d,"
                   "\"sound\":\"%s\",\"category\":\"neutral\","
                   "\"x\":%.17g,\"y\":%.17g,\"z\":%.17g,"
                   "\"volume\":%.9g,\"pitch\":%.9g}",
                event.eid, mob_event_sound_name(event.data),
                event.x, event.y, event.z,
                (double)event.volume, (double)event.pitch);
        }
    }
    putchar(']');
}

static void write_world_events(const GmRuntime *r) {
    putchar('[');
    for (int i = 0; i < gm_runtime_world_event_count(r); ++i) {
        GmRuntimeWorldEvent event;
        if (!gm_runtime_world_event_get(r, i, &event))
            continue;
        if (i) putchar(',');
        printf("{\"seq\":%llu,\"id\":%d,\"x\":%d,\"y\":%d,"
               "\"z\":%d,\"data\":%d}",
            (unsigned long long)event.seq, event.id,
            event.x, event.y, event.z, event.data);
    }
    putchar(']');
}

static void write_double_bits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    printf("\"%016llx\"", (unsigned long long)bits);
}

static void write_float_bits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    printf("\"%08x\"", (unsigned)bits);
}

static void write_terminal_particles(const GmRuntime *r) {
    putchar('[');
    for (int batch_index = 0;
            batch_index < gm_mobs_terminal_particle_count(&r->mobs);
            ++batch_index) {
        GmMobTerminalParticles batch;
        if (!gm_mobs_terminal_particle_get(
                &r->mobs, batch_index, &batch))
            continue;
        if (batch_index) putchar(',');
        printf("{\"seq\":%llu,\"eid\":%d,\"dimension\":%d,"
               "\"particle_id\":%d,\"ignore_range\":%s,"
               "\"parameters\":[],\"particles\":[",
            (unsigned long long)batch.seq, batch.eid, batch.dimension,
            batch.particle_id, batch.ignore_range ? "true" : "false");
        for (int particle = 0;
                particle < GM_MOB_TERMINAL_PARTICLE_COUNT; ++particle) {
            const GmTerminalParticle *p = &batch.particles[particle];
            if (particle) putchar(',');
            putchar('[');
            write_double_bits(p->x); putchar(',');
            write_double_bits(p->y); putchar(',');
            write_double_bits(p->z); putchar(',');
            write_double_bits(p->vx); putchar(',');
            write_double_bits(p->vy); putchar(',');
            write_double_bits(p->vz); putchar(']');
        }
        fputs("]}", stdout);
    }
    putchar(']');
}

static void write_terminal_xp_orbs(const GmRuntime *r) {
    int first = 1;
    putchar('[');
    for (int slot = 0; slot < GM_XP_ORBS; ++slot) {
        const McOrb *orb = &r->mobs.xp_orbs[slot];
        if (orb->dead || orb->xpValue <= 0)
            continue;
        if (!first) putchar(',');
        first = 0;
        printf("{\"eid\":%d,\"dimension\":%d,\"value\":%d,"
               "\"health\":%d,\"age\":%d,\"pickup_delay\":%d,"
               "\"color\":%d,\"target_color\":%d,\"dead\":%s,"
               "\"yaw_bits\":",
            orb->eid, r->mobs.orb_dimension[slot], orb->xpValue,
            orb->health, orb->xpOrbAge, orb->delayBeforeCanPickup,
            orb->xpColor, orb->xpTargetColor,
            orb->dead ? "true" : "false");
        write_float_bits(orb->yaw);
        fputs(",\"payload_bits\":[", stdout);
        write_double_bits(orb->posX); putchar(',');
        write_double_bits(orb->posY); putchar(',');
        write_double_bits(orb->posZ); putchar(',');
        write_double_bits(orb->motionX); putchar(',');
        write_double_bits(orb->motionY); putchar(',');
        write_double_bits(orb->motionZ); fputs("]}", stdout);
    }
    putchar(']');
}

static const GmLiveEnt *fixture_item(const GmRuntime *r, int eid) {
    for (int slot = 0; slot < GM_LIVE_MAX; ++slot)
        if (r->entities.ents[slot].active
                && r->entities.ents[slot].type == 0
                && r->entities.ents[slot].eid == eid)
            return &r->entities.ents[slot];
    return NULL;
}

static void write_mob_post_rows(
        GmRuntime *r, int mob_eid, int first_item_eid,
        int item_count, GmAction idle) {
    int mob_slot = fixture_mob_slot(r, mob_eid);
    putchar('[');
    if (mob_slot < 0) {
        putchar(']');
        return;
    }
    for (int tick = 1; tick <= 20; ++tick) {
        int xp_count = 0;
        int particle_batches_before =
            gm_mobs_terminal_particle_count(&r->mobs);
        gm_runtime_tick(r, idle);
        int terminal_particle_count =
            (gm_mobs_terminal_particle_count(&r->mobs)
                - particle_batches_before)
            * GM_MOB_TERMINAL_PARTICLE_COUNT;
        const EwStore *mobs =
            r->mobs.current ? &r->mobs.b : &r->mobs.a;
        for (int slot = 0; slot < GM_XP_ORBS; ++slot)
            if (!r->mobs.xp_orbs[slot].dead
                    && r->mobs.xp_orbs[slot].xpValue > 0)
                ++xp_count;
        if (tick > 1) putchar(',');
        printf("{\"tick\":%d,\"death_time\":%d,"
               "\"hurt_resistant_time\":%d,\"hurt_time\":%d,"
               "\"living_dead\":%s,\"entity_is_dead\":%s,"
               "\"loaded\":%s,\"fire_ticks\":%d,\"burning\":%s,"
               "\"entity_seed48\":%llu,\"recently_hit\":%d,"
               "\"attacking_player\":%s,\"world_seed48\":%llu,"
               "\"math_seed48\":%llu,\"xp_entity_count\":%d,"
               "\"terminal_particle_count\":%d,"
               "\"items\":[",
            tick, r->mobs.entity_death_time[mob_slot],
            r->mobs.entity_hurt_resistant[mob_slot],
            r->mobs.entity_hurt_time[mob_slot],
            r->mobs.entity_dead[mob_slot] ? "true" : "false",
            mobs->alive[mob_slot] ? "false" : "true",
            mobs->alive[mob_slot] ? "true" : "false",
            r->mobs.fire_ticks[mob_slot],
            r->mobs.fire_ticks[mob_slot] > 0 ? "true" : "false",
            (unsigned long long)
                r->mobs.entity_random[mob_slot].random.seed,
            r->mobs.entity_recently_hit[mob_slot],
            r->mobs.entity_attacking_player[mob_slot] ? "true" : "false",
            (unsigned long long)r->world_random_seed48,
            (unsigned long long)r->math_random_seed48,
            xp_count, terminal_particle_count);
        for (int i = 0; i < item_count; ++i) {
            int eid = first_item_eid + i;
            const GmLiveEnt *item = fixture_item(r, eid);
            if (i) putchar(',');
            if (item)
                printf("[%d,%d,%d,false,true]",
                    eid, item->age, item->pickup_delay);
            else
                printf("[%d,-1,-1,true,false]", eid);
        }
        fputs("]}", stdout);
    }
    putchar(']');
}

int main(int argc, char **argv) {
    const char *mode = argc >= 2 ? argv[1] : "fall";
    int meta = argc >= 3 ? atoi(argv[2]) : 0;
    const int supported = !strcmp(mode, "supported");
    const int fall = !strcmp(mode, "fall");
    const int drop = !strcmp(mode, "drop");
    const int damage_fresh = !strcmp(mode, "damage");
    const int damage_reject = !strcmp(mode, "damage_reject");
    const int damage_delta = !strcmp(mode, "damage_delta");
    const int damage_absorption = !strcmp(mode, "damage_absorption");
    const int damage_resistance = !strcmp(mode, "damage_resistance");
    const int damage_armor = !strcmp(mode, "damage_armor_chest");
    const int damage_helmet = !strcmp(mode, "damage_armor_helmet");
    const int damage_pig = !strcmp(mode, "damage_pig");
    const int damage_pig_xp = !strcmp(mode, "damage_pig_loot_xp");
    const int damage_pig_any = damage_pig || damage_pig_xp;
    const int damage_pigs = !strcmp(mode, "damage_pigs");
    const int damage_cow = !strcmp(mode, "damage_cow");
    const int damage_cow_xp = !strcmp(mode, "damage_cow_loot_xp");
    const int damage_cow_any = damage_cow || damage_cow_xp;
    const int damage_sheep = !strcmp(mode, "damage_sheep");
    const int damage_sheep_red_xp =
        !strcmp(mode, "damage_sheep_red_loot_xp");
    const int damage_sheep_sheared_xp =
        !strcmp(mode, "damage_sheep_sheared_loot_xp");
    const int damage_sheep_xp = !strcmp(mode, "damage_sheep_loot_xp")
        || damage_sheep_red_xp || damage_sheep_sheared_xp;
    const int damage_sheep_any = damage_sheep || damage_sheep_xp;
    const int damage_chicken = !strcmp(mode, "damage_chicken");
    const int damage_chicken_loot =
        !strcmp(mode, "damage_chicken_loot");
    const int damage_chicken_loot_zero =
        !strcmp(mode, "damage_chicken_loot_zero");
    const int damage_chicken_loot_one =
        !strcmp(mode, "damage_chicken_loot_one");
    const int damage_chicken_xp =
        !strcmp(mode, "damage_chicken_loot_cooked_xp");
    const int damage_chicken_xp_expired =
        !strcmp(mode, "damage_chicken_loot_cooked_xp_expired");
    const int damage_chicken_loot_cooked =
        !strcmp(mode, "damage_chicken_loot_cooked")
        || damage_chicken_xp || damage_chicken_xp_expired;
    const int damage_chicken_loot_any = damage_chicken_loot
        || damage_chicken_loot_zero || damage_chicken_loot_one
        || damage_chicken_loot_cooked;
    const int damage_chicken_any =
        damage_chicken || damage_chicken_loot_any;
    const int damage = damage_fresh || damage_reject || damage_delta
        || damage_absorption || damage_resistance || damage_armor
        || damage_helmet;
    const int moving = fall || drop || damage || damage_pig_any || damage_pigs
        || damage_cow_any || damage_sheep_any || damage_chicken_any;
    const int instant = !strcmp(mode, "instant");
    const int unsupported = moving || instant;
    const int capacity = !strcmp(mode, "capacity");
    const int ox = argc >= 4 ? atoi(argv[3]) : 26;
    const int oz = argc >= 5 ? atoi(argv[4]) : 8;
    const int by = 220, min_y = 215, max_y = 221;
    const uint64_t math_seed = UINT64_C(0x123456789ABC);
    const uint64_t world_seed = UINT64_C(0x23456789ABCD);
    const uint64_t player_seed = UINT64_C(0x6789ABCDEF01);
    const uint64_t entity_seed = argc >= 6
        ? (uint64_t)strtoull(argv[5], NULL, 0)
        : UINT64_C(0x23456789ABCD);
    const int next_entity_id = 520000;
    GmConfig cfg; GmRuntime r; GmAction idle; char err[256];
    FallingRow rows[20]; ScheduledRow on_added[4], after_loss[4], final[4];
    int row_count = 0, after_loss_count = 0;

    if ((!supported && !unsupported && !capacity)
            || (meta != 0 && meta != 1 && meta != 4 && meta != 8)) {
        fprintf(stderr,
            "usage: %s supported|fall|drop|damage|damage_reject|damage_delta|"
            "damage_absorption|damage_resistance|damage_armor_chest|"
            "damage_armor_helmet|"
            "damage_pig|damage_pig_loot_xp|damage_pigs|damage_cow|"
            "damage_cow_loot_xp|"
            "damage_sheep|damage_sheep_loot_xp|"
            "damage_sheep_red_loot_xp|damage_sheep_sheared_loot_xp|"
            "damage_chicken|"
            "damage_chicken_loot|damage_chicken_loot_zero|"
            "damage_chicken_loot_one|damage_chicken_loot_cooked|"
            "damage_chicken_loot_cooked_xp|"
            "damage_chicken_loot_cooked_xp_expired|"
            "instant|capacity meta "
            "[origin_x origin_z entity_seed48]\n", argv[0]);
        return 2;
    }
    gm_config_defaults(&cfg);
    cfg.view_distance = 1; cfg.mobs = cfg.daylight = cfg.weather = 0;
    cfg.render = GM_RENDER_OFF;
    memset(&idle, 0, sizeof idle); idle.hotbar_sel = -1;
    if (!gm_runtime_init(&r, &cfg, err, sizeof err)) {
        fprintf(stderr, "runtime init: %s\n", err); return 1;
    }
    gm_runtime_set_total_time(&r, 100);
    memset(&r.entities, 0, sizeof r.entities);
    memset(r.falling_blocks, 0, sizeof r.falling_blocks);
    r.falling_block_count = r.scheduled_tick_count = r.scheduled_tick_next_order = 0;
    for (int y = min_y; y <= max_y; ++y)
        for (int z = oz - 2; z <= oz + 2; ++z)
            for (int x = ox - 2; x <= ox + 2; ++x)
                gm_world_set_block_meta(r.world, x, y, z, 0, 0);
    for (int z = oz - 2; z <= oz + 2; ++z)
        for (int x = ox - 2; x <= ox + 2; ++x)
            gm_world_set_block_meta(r.world, x, by - 4, z, 1, 0);
    if (drop)
        gm_world_set_block_meta(r.world, ox, by - 4, oz, 44, 0);
    if (damage) {
        gm_runtime_set_pose_state(&r, ox + 0.5, by - 3.0, oz + 0.5,
            0.0f, 0.0f, 0.0, 0.0, 0.0, 1, 0.0f);
        gm_runtime_set_vitals(&r, 20.0f, 20);
        gm_runtime_set_food_stats(&r, 5.0f, 0.0f);
        r.dead = 0;
        r.mobs.player_hurt_resistant = 0;
        r.mobs.player_hurt_time = 0;
        r.mobs.player_last_damage = 0.0f;
        r.mobs.player_absorption = damage_absorption ? 4.0f : 0.0f;
        r.mobs.player_resistance_amplifier = damage_resistance ? 0 : -1;
        if (damage_armor)
            isr_set_stack(
                &r.player.inv, ISR_ARMOR0 + 2, ic_mk(311, 1, 0));
        if (damage_helmet)
            isr_set_stack(
                &r.player.inv, ISR_ARMOR0 + 3, ic_mk(310, 1, 0));
        r.player.health = r.server_player.health = 20.0f;
    }
    gm_world_set_block_meta(r.world, ox, by - 1, oz, 1, 0);
    if (!gm_runtime_set_block(&r, ox, by, oz, 145, meta)) {
        fprintf(stderr, "anvil placement failed\n");
        gm_runtime_destroy(&r);
        return 1;
    }
    int on_added_count = snapshot_scheduled(&r, on_added, 4);
    if (on_added_count < 0) { gm_runtime_destroy(&r); return 1; }
    if (unsupported || capacity) {
        if (!gm_runtime_set_block(&r, ox, by - 1, oz, 0, 0)) {
            fprintf(stderr, "anvil support removal failed\n");
            gm_runtime_destroy(&r);
            return 1;
        }
        after_loss_count = snapshot_scheduled(&r, after_loss, 4);
        if (after_loss_count < 0) { gm_runtime_destroy(&r); return 1; }
    }
    if (!gm_runtime_set_math_random_seed48(&r, math_seed)
            || !gm_runtime_set_world_random_seed48(&r, world_seed)
            || !gm_runtime_set_entity_id_cursor(&r, next_entity_id)
            || (damage && !gm_runtime_set_player_random_seed48(
                &r, player_seed))
            || !gm_runtime_set_falling_instant(&r, instant)
            || !gm_runtime_set_next_falling_random_seed48(
                &r, entity_seed)) {
        fprintf(stderr, "cursor restore failed\n");
        gm_runtime_destroy(&r);
        return 1;
    }
    if (instant) {
        r.scheduled_tick_count = 0;
        r.scheduled_tick_next_order = 0;
        if (!gm_runtime_schedule_tick(
                &r, ox, by, oz, 145,
                r.clock.total_time + 2, 0, 0)) {
            fprintf(stderr, "instant anvil callback restore failed\n");
            gm_runtime_destroy(&r);
            return 1;
        }
    }
    if (capacity) for (int slot = 0; slot < GM_RUNTIME_FALLING_BLOCKS; ++slot) {
        int x = ox - 8 + slot, z = oz + 8;
        gm_world_set_block_meta(r.world, x, 200, z, 145, meta);
        if (!gm_runtime_spawn_falling_fixture(&r, 600000 + slot, 145, meta, 0,
                x + .5, 200.0, z + .5, 0, 0, 0, 1, 1)) {
            fprintf(stderr, "falling pool setup failed\n");
            gm_runtime_destroy(&r);
            return 1;
        }
    }
    for (int tick = 0; tick < 2; ++tick) gm_runtime_tick(&r, idle);
    if (damage_pig_any || damage_pigs || damage_cow_any || damage_sheep_any
            || damage_chicken_any) {
        /* Match Java's controlled construction order: falling entity first,
         * then one NoAI pig before falling update two. The explicit saved
         * cursor records both allocations even though magma stores controlled
         * mob and runtime fixture cursors separately. */
        if (!gm_runtime_spawn_mob_fixture(
                &r, damage_chicken_any ? GM_MOB_CHICKEN
                    : damage_sheep_any ? GM_MOB_SHEEP
                    : damage_cow_any ? GM_MOB_COW : GM_MOB_PIG,
                next_entity_id + 1,
                ox + 0.5, by - 3.0, oz + 0.5,
                0.0, 0.0, 0.0, 0.0f,
                damage_chicken_any || damage_pig_xp || damage_cow_xp
                    || damage_sheep_xp ? 4.0f
                    : damage_sheep ? 8.0f : 10.0f, 1, 0, 0, 0)
                || !gm_mobs_set_entity_random_state(
                    &r.mobs, next_entity_id + 1,
                    damage_chicken_loot_zero ? UINT64_C(2)
                        : damage_chicken_loot_one ? UINT64_C(3)
                        : damage_chicken_loot_any
                            ? UINT64_C(0x23456789ABCD)
                        : damage_chicken ? UINT64_C(0x89ABCDEF0123)
                        : damage_sheep_any ? UINT64_C(0x789ABCDEF012)
                        : damage_cow_any ? UINT64_C(0x56789ABCDEF0)
                            : UINT64_C(0x3456789ABCDE), 0, 0.0)
                || (damage_sheep_any && !gm_runtime_set_sheep_state(
                    &r, next_entity_id + 1,
                    damage_sheep_red_xp || damage_sheep_sheared_xp ? 14 : 0,
                    damage_sheep_sheared_xp))
                || ((damage_chicken_any || damage_pig_xp || damage_cow_xp
                        || damage_sheep_xp)
                    && !gm_runtime_set_mob_fire_ticks(
                        &r, next_entity_id + 1,
                        damage_chicken_loot_cooked ? 100 : -1))
                || (damage_pigs && (!gm_runtime_spawn_mob_fixture(
                    &r, GM_MOB_PIG, next_entity_id + 2,
                    ox + 0.5, by - 3.0, oz + 0.5,
                    0.0, 0.0, 0.0, 0.0f, 10.0f, 1, 0, 0, 0)
                    || !gm_mobs_set_entity_random_state(
                        &r.mobs, next_entity_id + 2,
                        UINT64_C(0x456789ABCDEF), 0, 0.0)))
                || !gm_runtime_set_entity_id_cursor(&r,
                    next_entity_id + (damage_pigs ? 3 : 2))
                /* EntityLivingBase's constructor consumes three global
                 * Math.random doubles before the pinned impact boundary. */
                || !gm_runtime_set_math_random_seed48(
                    &r, java_lcg_steps(math_seed,
                        damage_pigs ? 12 : 6))
                || !gm_runtime_set_do_mob_loot(
                    &r, damage_chicken_loot_any || damage_pig_xp
                        || damage_cow_xp || damage_sheep_xp ? 1 : 0)) {
            fprintf(stderr, "passive fixture setup failed\n");
            gm_runtime_destroy(&r);
            return 1;
        }
    }
    if (moving) for (;;) {
        GmRuntimeFallingBlock *f = &r.falling_blocks[0];
        if (f->fall_time <= 0 || row_count >= 20) {
            fprintf(stderr, "falling anvil row boundary failed\n");
            gm_runtime_destroy(&r);
            return 1;
        }
        rows[row_count++] = (FallingRow){f->fall_time, f->fall_time, !f->active,
            f->x, f->y, f->z, f->vx, f->vy, f->vz, f->on_ground,
            f->collided_horizontally, f->collided_vertically,
            f->fall_distance, f->random_seed48};
        if (!f->active) break;
        if ((damage_reject || damage_delta) && f->fall_time == 12) {
            /* gm_runtime_tick ages living hurt timers before falling entities.
             * Seed the tick-entry values so impact observes Java's 20/10. */
            r.vitals.health = 20.0f;
            r.player.health = r.server_player.health = 20.0f;
            r.vitals.exhaustion = 0.0f;
            r.mobs.player_hurt_resistant = 21;
            r.mobs.player_hurt_time = 11;
            r.mobs.player_last_damage = damage_reject ? 4.0f : 2.0f;
        }
        if ((damage_pig_any || damage_pigs || damage_cow_any
                    || damage_sheep_any
                    || damage_chicken_any)
                && f->fall_time == 12)
            gm_runtime_tick_falling_fixture_phase(&r);
        else
            gm_runtime_tick(&r, idle);
    }
    /* Java's isolated fixture advances only the falling entity before the
     * impact boundary.  Seed the saved combat-credit state here so magma's
     * ordinary world ticks used to move the anvil cannot age it early. */
    if ((damage_pig_xp || damage_cow_xp || damage_sheep_xp
            || damage_chicken_xp || damage_chicken_xp_expired)
            && !gm_runtime_set_mob_recent_hit_state(
                &r, next_entity_id + 1,
                damage_pig_xp || damage_cow_xp || damage_sheep_xp
                    || damage_chicken_xp
                    ? 20 : 19, 1)) {
        fprintf(stderr, "passive combat-credit restore failed\n");
        gm_runtime_destroy(&r);
        return 1;
    }
    int final_count = snapshot_scheduled(&r, final, 4);
    if (final_count < 0) { gm_runtime_destroy(&r); return 1; }
    printf("{\"mode\":\"%s\",\"meta\":%d,\"origin_x\":%d,"
        "\"origin_z\":%d,\"base_y\":%d,\"on_added_scheduled\":",
        mode, meta, ox, oz, by);
    write_scheduled(on_added, on_added_count);
    printf(",\"after_support_loss_scheduled\":");
    write_scheduled(after_loss, after_loss_count);
    printf(",\"rows\":[");
    for (int i = 0; i < row_count; ++i) { FallingRow *v = &rows[i]; if (i) putchar(',');
        printf("[%d,%d,%s,%.17g,%.17g,%.17g,%.17g,%.17g,%.17g,"
            "%s,%s,%s,%.9g,%llu]", v->step, v->fall_time,
            v->dead ? "true" : "false", v->x, v->y, v->z,
            v->vx, v->vy, v->vz, v->on_ground ? "true" : "false",
            v->collided_horizontally ? "true" : "false",
            v->collided_vertically ? "true" : "false",
            (double)v->fall_distance,
            (unsigned long long)v->random_seed48); }
    printf("],\"final_blocks\":["); int first = 1;
    for (int y = min_y; y <= max_y; ++y) for (int z = oz - 2; z <= oz + 2; ++z) for (int x = ox - 2; x <= ox + 2; ++x) { int id = gm_world_block(r.world, x, y, z); if (!id) continue; if (!first) putchar(','); first = 0; printf("[%d,%d,%d,%d,%d]", x, y, z, id, gm_world_meta(r.world, x, y, z)); }
    printf("],\"scheduled\":"); write_scheduled(final, final_count);
    printf(",\"fixture_entities\":");
    if (moving) printf("[[%d,true,%.17g,%.17g,%.17g]]", next_entity_id, r.falling_blocks[0].x, r.falling_blocks[0].y, r.falling_blocks[0].z); else printf("[]");
    printf(",\"ticked_item\":");
    if (drop) write_ticked_item(&r);
    else fputs("null", stdout);
    printf(",\"player_damage\":");
    if (damage) {
        printf("{\"health\":%.9g,\"absorption\":%.9g,"
               "\"hurt_resistant_time\":%d,\"hurt_time\":%d,"
               "\"max_hurt_time\":%d,\"last_damage\":%.9g,"
               "\"food_exhaustion\":%.9g,\"entity_seed48\":%llu,"
               "\"armor\":[",
            (double)r.vitals.health,
            (double)r.mobs.player_absorption,
            r.mobs.player_hurt_resistant, r.mobs.player_hurt_time,
            r.mobs.player_hurt_time, (double)r.mobs.player_last_damage,
            (double)r.vitals.exhaustion,
            (unsigned long long)r.mobs.player_random.seed);
        {
            int armor_first = 1;
            for (int slot = ISR_ARMOR0;
                    slot < ISR_ARMOR0 + ISR_ARMOR_SLOTS; ++slot) {
                ICStack stack = isr_get_stack(&r.player.inv, slot);
                if (stack.item <= 0 || stack.count <= 0) continue;
                if (!armor_first) putchar(',');
                armor_first = 0;
                printf("{\"slot\":%d,\"id\":%d,\"count\":%d,"
                       "\"meta\":%d}",
                    slot, stack.item, stack.count, stack.meta);
            }
        }
        printf("]}");
    } else {
        fputs("null", stdout);
    }
    printf(",\"mob_damage\":");
    if (damage_pig_any || damage_cow_any || damage_sheep_any
            || damage_chicken_any) {
        write_mob_damage(&r, next_entity_id + 1);
    } else {
        fputs("null", stdout);
    }
    printf(",\"mob_damages\":[");
    if (damage_pigs) {
        write_mob_damage(&r, next_entity_id + 1);
        putchar(',');
        write_mob_damage(&r, next_entity_id + 2);
    }
    printf("],\"impact_order\":[");
    if (damage_pigs)
        printf("%d,%d", next_entity_id + 1, next_entity_id + 2);
    else if (damage_pig_xp || damage_cow_any || damage_sheep_any
            || damage_chicken_any)
        printf("%d", next_entity_id + 1);
    printf("],\"mob_events\":");
    write_mob_events(&r);
    printf(",\"world_events\":");
    write_world_events(&r);
    printf(",\"mob_drops\":");
    write_mob_drops(&r, next_entity_id + 2);
    printf(",\"mob_post_rows\":");
    if (damage_pig_xp || damage_cow_xp || damage_sheep_xp
            || damage_chicken_loot_cooked)
        write_mob_post_rows(
            &r, next_entity_id + 1, next_entity_id + 2,
            r.next_entity_id - (next_entity_id + 2), idle);
    else
        fputs("[]", stdout);
    printf(",\"terminal_particles\":");
    write_terminal_particles(&r);
    printf(",\"xp_orbs\":");
    write_terminal_xp_orbs(&r);
    printf(",\"mob_damage_post_runtime_tick\":false");
    printf(",\"source_block\":%d,\"source_meta\":%d,"
        "\"math_seed48\":%llu,\"world_seed48\":%llu,"
        "\"next_entity_id\":%d,\"entity_seed48\":%llu,"
        "\"impact_fall_distance\":%.9g}\n",
        gm_world_block(r.world, ox, by, oz),
        gm_world_meta(r.world, ox, by, oz),
        (unsigned long long)r.math_random_seed48,
        (unsigned long long)r.world_random_seed48, r.next_entity_id,
        (unsigned long long)(moving
            ? r.falling_blocks[0].random_seed48 : entity_seed),
        (double)(moving
            ? r.falling_blocks[0].impact_fall_distance : 0.0f));
    gm_runtime_destroy(&r); return 0;
}
