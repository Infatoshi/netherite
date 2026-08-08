#include "game/runtime.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static uint64_t dbits(double value) {
    uint64_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static uint32_t fbits(float value) {
    uint32_t bits;
    memcpy(&bits, &value, sizeof bits);
    return bits;
}

static EwStore *store(GmMobLive *m) {
    return m->current ? &m->b : &m->a;
}

static int init(GmRuntime *r) {
    GmConfig config;
    char error[256];
    gm_config_defaults(&config);
    config.seed = 0;
    config.world = GM_WORLD_SUPERFLAT;
    config.view_distance = 1;
    config.mobs = 0;
    config.weather = 0;
    if (!gm_runtime_init(r, &config, error, sizeof error)) {
        fprintf(stderr, "init: %s\n", error);
        return 0;
    }
    for (int x = -4; x <= 32; ++x)
        for (int z = -8; z <= 5; ++z) {
            gm_world_set_block(r->world, x, 0, z, 1);
            for (int y = 1; y <= 8; ++y)
                gm_world_set_block(r->world, x, y, z, 0);
        }
    return 1;
}

static void tick(GmRuntime *r) {
    gm_mobs_tick(
        &r->mobs, r->world, NULL,
        (const struct McSinTable *)&r->sin_table,
        (struct PsvPlayer *)&r->player,
        (struct PvStats *)&r->vitals,
        r->ox, r->oz, r->dimension, r->clock.world_time,
        &r->clock, r->mob_griefing, &r->world_random_seed48,
        &r->math_random_seed48, &r->next_entity_id,
        r->do_mob_loot, &r->entities, 0.0F, 0.0F);
}

static int attack(void) {
    GmRuntime r;
    if (!init(&r)) return 0;
    int slot = gm_mobs_spawn(&r.mobs, EW_TYPE_EVOKER, 0.5, 1.0, 0.5);
    EwStore *s = store(&r.mobs);
    int eid = s->id[slot];
    if (slot <= 0 || gm_mobs_evoker_cast_attack(
            &r.mobs, r.world,
            (const struct McSinTable *)&r.sin_table,
            eid, 20.5, 1.0, 0.5) != 16) {
        gm_runtime_destroy(&r);
        return 0;
    }
    printf("A %d", gm_mobs_evoker_fang_count(&r.mobs));
    for (int i = 0; i < 16; ++i) {
        GmEvokerFang fang;
        if (!gm_mobs_evoker_fang_get(&r.mobs, i, &fang)) return 0;
        printf(" %016" PRIx64 " %016" PRIx64 " %016" PRIx64
               " %08" PRIx32 " %d",
            dbits(fang.x), dbits(fang.y), dbits(fang.z),
            fbits(fang.yaw), fang.warmup);
    }
    putchar('\n');

    GmEvokerFang first;
    if (!gm_mobs_evoker_fang_get(&r.mobs, 0, &first)) return 0;
    gm_runtime_set_pose(&r, first.x, first.y, first.z, 0.0F, 0.0F);
    r.vitals.health = 20.0F;
    r.player.health = 20.0F;
    printf("F");
    for (int step = 1; step <= 32; ++step) {
        tick(&r);
        int dead = gm_mobs_evoker_fang_count(&r.mobs) < 16;
        printf(" %d:%08" PRIx32 ":%d",
            step, fbits(r.vitals.health), dead);
        if (dead) break;
    }
    putchar('\n');
    gm_runtime_destroy(&r);
    return 1;
}

static int summon(void) {
    GmRuntime r;
    if (!init(&r)) return 0;
    int evoker = gm_mobs_spawn(
        &r.mobs, EW_TYPE_EVOKER, 10.5, 1.0, -3.5);
    EwStore *s = store(&r.mobs);
    int eid = s->id[evoker];
    r.mobs.entity_random[evoker].random.seed = UINT64_C(0x123456789ab);
    if (gm_mobs_evoker_cast_summon(&r.mobs, eid) != 3) return 0;
    s = store(&r.mobs);
    printf("S 3");
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot) {
        if (!s->alive[slot] || s->type[slot] != EW_TYPE_VEX) continue;
        printf(" %016" PRIx64 " %016" PRIx64 " %016" PRIx64
               " %d %d %d %d",
            dbits(s->x[slot]), dbits(s->y[slot]), dbits(s->z[slot]),
            r.mobs.vex_bound_x[slot], r.mobs.vex_bound_y[slot],
            r.mobs.vex_bound_z[slot], r.mobs.vex_life_ticks[slot]);
    }
    putchar('\n');
    gm_runtime_destroy(&r);
    return 1;
}

static int wololo(void) {
    GmRuntime r;
    if (!init(&r)) return 0;
    int evoker = gm_mobs_spawn(
        &r.mobs, EW_TYPE_EVOKER, 0.5, 1.0, 0.5);
    int sheep = gm_mobs_spawn(&r.mobs, EW_TYPE_SHEEP, 2.5, 1.0, 0.5);
    EwStore *s = store(&r.mobs);
    r.mobs.sheep_data[sheep] = 11;
    if (!gm_mobs_evoker_cast_wololo(&r.mobs, s->id[evoker])) return 0;
    printf("W %d\n", r.mobs.sheep_data[sheep] & 15);
    gm_runtime_destroy(&r);
    return 1;
}

static int automatic(void) {
    GmRuntime r;
    if (!init(&r)) return 0;
    /* EntityEvoker FOLLOW_RANGE is 12. Keep the target inside that exact
     * acquisition radius while leaving enough room for both fang patterns. */
    gm_runtime_set_pose(&r, 10.5, 1.0, 0.5, 0.0F, 0.0F);
    int evoker = gm_mobs_spawn(
        &r.mobs, EW_TYPE_EVOKER, 0.5, 1.0, 0.5);
    if (evoker <= 0) return 0;
    r.mobs.persistence_required[evoker] = 1;
    int saw_fangs = 0, saw_fang_view = 0;
    int saw_cast_pose = 0, saw_vex_view = 0;
    int schedule_ok = 1;
    for (int step = 0; step < 5; ++step) {
        tick(&r);
        /* Exercise both scheduler branches without making every regression
         * run simulate the otherwise idle 100-tick casting interval. The
         * direct spell effects and their real warmups are oracle-checked
         * above; here we check the automatic priority/state transitions. */
        if (step == 0) {
            schedule_ok &= r.mobs.evoker_spell_id[evoker] == 1
                && r.mobs.evoker_spell_warmup[evoker] == 19
                && r.mobs.evoker_spell_ticks[evoker] == 100;
            r.mobs.evoker_spell_warmup[evoker] = 1;
        } else if (step == 1) {
            schedule_ok &= r.mobs.evoker_spell_id[evoker] == 1
                && r.mobs.evoker_spell_warmup[evoker] == 0;
            r.mobs.evoker_spell_id[evoker] = 0;
            r.mobs.evoker_spell_ticks[evoker] = 0;
            r.mobs.evoker_attack_next[evoker] = 0;
        } else if (step == 2) {
            schedule_ok &= r.mobs.evoker_spell_id[evoker] == 2
                && r.mobs.evoker_spell_warmup[evoker] == 19
                && r.mobs.evoker_spell_ticks[evoker] == 40;
            r.mobs.evoker_spell_warmup[evoker] = 1;
        }
        GmEntityView views[512];
        int n = gm_mobs_fill_views(&r.mobs, views, 512);
        saw_fangs |= gm_mobs_evoker_fang_count(&r.mobs) > 0;
        for (int i = 0; i < n; ++i) {
            saw_cast_pose |= views[i].type == EW_TYPE_EVOKER
                && (views[i].flags & 512) != 0;
            saw_vex_view |= views[i].type == EW_TYPE_VEX;
            saw_fang_view |= views[i].type == GM_VIEW_EVOKER_FANGS;
        }
    }
    EwStore *s = store(&r.mobs);
    int vexes = 0;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        vexes += s->alive[slot] && s->type[slot] == EW_TYPE_VEX;
    int okay = schedule_ok && vexes >= 3 && saw_fangs && saw_fang_view
        && saw_cast_pose && saw_vex_view;
    if (!okay)
        fprintf(stderr,
            "automatic spell loop failed: schedule=%d vex=%d fangs=%d "
            "fang_view=%d cast=%d vex_view=%d\n",
            schedule_ok, vexes, saw_fangs, saw_fang_view,
            saw_cast_pose, saw_vex_view);
    gm_runtime_destroy(&r);
    return okay;
}

int main(void) {
    return attack() && summon() && wololo() && automatic() ? 0 : 1;
}
