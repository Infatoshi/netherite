#include "game/runtime.h"

#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", M); fail = 1; } \
} while (0)

static uint64_t java_lcg_steps(uint64_t seed, int steps) {
    while (steps-- > 0)
        seed = (seed * UINT64_C(0x5DEECE66D) + UINT64_C(0xB))
            & ((UINT64_C(1) << 48) - UINT64_C(1));
    return seed;
}

static int mob_slot(const GmRuntime *r, int eid) {
    const EwStore *s = r->mobs.current ? &r->mobs.b : &r->mobs.a;
    for (int slot = 1; slot < EW_MAX_ENTITIES; ++slot)
        if (s->alive[slot] && s->id[slot] == eid)
            return slot;
    return -1;
}

static void component_fixture(
        GmMobLive *mobs, GmLiveSim *drops, IsrInv *inventory,
        int sheared, int child, int held_item, int unbreaking) {
    memset(drops, 0, sizeof *drops);
    isr_init(inventory);
    gm_mobs_init(mobs, 0);
    mobs->active_dimension = 0;
    ICStack tool = ic_mk(held_item, 1, 0);
    if (unbreaking) {
        tool.n_enchants = 1;
        tool.enchants[0].id = 34;
        tool.enchants[0].level = 3;
    }
    isr_set_stack(inventory, 0, tool);
    CHECK(gm_mobs_spawn_exact(
              mobs, EW_TYPE_SHEEP, 640000,
              10.0, 64.0, 10.0, 0.0, 0.0, 0.0,
              0.0F, 8.0F, 1, 0, 0, 0) >= 0
              && gm_mobs_set_entity_random_state(
                  mobs, 640000, 1, 0, 0.0)
              && gm_mobs_set_sheep_state(mobs, 640000, 14, sheared)
              && gm_mobs_set_growing_age(
                  mobs, 640000, child ? -100 : 0),
          "component sheep fixture initializes");
}

static void test_component_controls(void) {
    GmMobLive mobs;
    GmLiveSim drops;
    IsrInv inventory;
    uint64_t shear_seed, math_seed;
    int next_id;

    component_fixture(&mobs, &drops, &inventory, 1, 0, 359, 0);
    shear_seed = UINT64_C(0x3456789ABCDE);
    math_seed = UINT64_C(0x123456789ABC);
    next_id = 640001;
    CHECK(gm_mobs_shear_sheep(
              &mobs, 640000, &inventory, 0,
              &shear_seed, &math_seed, &drops, &next_id) == 1
              && drops.n_active == 0 && next_id == 640001
              && shear_seed == UINT64_C(0x3456789ABCDE)
              && math_seed == UINT64_C(0x123456789ABC)
              && isr_get_stack(&inventory, 0).meta == 0,
          "already-sheared adult is a handled exact no-op");

    component_fixture(&mobs, &drops, &inventory, 0, 1, 359, 0);
    shear_seed = UINT64_C(0x3456789ABCDE);
    math_seed = UINT64_C(0x123456789ABC);
    next_id = 640001;
    GmEntityView view;
    CHECK(gm_mobs_shear_sheep(
              &mobs, 640000, &inventory, 0,
              &shear_seed, &math_seed, &drops, &next_id) == 1
              && gm_mobs_fill_views(&mobs, &view, 1) == 1
              && (view.flags & 8) && !view.sheared
              && drops.n_active == 0 && next_id == 640001,
          "child sheep is ineligible and exposes the child view flag");

    component_fixture(&mobs, &drops, &inventory, 0, 0, 280, 0);
    shear_seed = UINT64_C(0x3456789ABCDE);
    math_seed = UINT64_C(0x123456789ABC);
    next_id = 640001;
    CHECK(gm_mobs_shear_sheep(
              &mobs, 640000, &inventory, 0,
              &shear_seed, &math_seed, &drops, &next_id) == 0
              && !(mobs.sheep_data[1] & 16) && drops.n_active == 0,
          "non-shears item cannot shear");

    component_fixture(&mobs, &drops, &inventory, 0, 0, 359, 3);
    shear_seed = UINT64_C(0x3456789ABCDE);
    math_seed = UINT64_C(0x123456789ABC);
    next_id = 640001;
    CHECK(gm_mobs_shear_sheep(
              &mobs, 640000, &inventory, 0,
              &shear_seed, &math_seed, &drops, &next_id) == 2
              && isr_get_stack(&inventory, 0).meta == 0
              && mobs.entity_random[1].random.seed == java_lcg_steps(1, 2),
          "Unbreaking uses the sheep cursor and can negate durability");

    component_fixture(&mobs, &drops, &inventory, 0, 0, 359, 0);
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        CHECK(gm_live_spawn_item_exact(
                  &drops, 700000 + i, 10.0, 64.0, 10.0,
                  0.0, 0.0, 0.0, 0.0F,
                  1, 1, 0, 0, 10, 1),
              "capacity prefill succeeds");
    shear_seed = UINT64_C(0x3456789ABCDE);
    math_seed = UINT64_C(0x123456789ABC);
    next_id = 640001;
    CHECK(gm_mobs_shear_sheep(
              &mobs, 640000, &inventory, 0,
              &shear_seed, &math_seed, &drops, &next_id) == -1
              && !(mobs.sheep_data[1] & 16)
              && mobs.entity_random[1].random.seed == 1
              && next_id == 640001
              && shear_seed == UINT64_C(0x3456789ABCDE)
              && math_seed == UINT64_C(0x123456789ABC)
              && isr_get_stack(&inventory, 0).meta == 0
              && gm_mobs_event_count(&mobs) == 0,
          "full exact item store rejects shearing atomically");
}

static void reset_case(
        GmRuntime *r, double y, int eid, int offhand, int blocked) {
    gm_mobs_init(&r->mobs, 0);
    memset(&r->entities, 0, sizeof r->entities);
    r->controlled_mobs_enabled = 0;
    r->mobs_enabled = 0;
    r->server_shear_pending = 0;
    r->next_shears_random_valid = 0;
    isr_init(&r->player.inv);
    r->player.inv.current_item = 0;
    isr_set_stack(
        &r->player.inv, offhand ? ISR_OFFHAND_SLOT : 0,
        ic_mk(359, 1, 0));
    gm_runtime_set_pose(r, 8.5, y, 8.5, 0.0F, 24.0F);
    gm_world_set_block_meta(r->world, 8, (int)y + 1, 9,
                            blocked ? 1 : 0, 0);
    CHECK(gm_runtime_spawn_mob_fixture(
              r, GM_MOB_SHEEP, eid, 8.5, y, 10.5,
              0.0, 0.0, 0.0, 0.0F, 8.0F, 1, 0, 0, 0)
              && gm_mobs_set_entity_random_state(
                  &r->mobs, eid, 0, 0, 0.0)
              && gm_runtime_set_sheep_state(r, eid, 14, 0)
              && gm_runtime_set_next_shears_random_seed48(
                  r, UINT64_C(0x3456789ABCDE))
              && gm_runtime_set_math_random_seed48(
                  r, UINT64_C(0x123456789ABC))
              && gm_runtime_set_entity_id_cursor(r, eid + 1),
          "runtime shearing fixture initializes");
}

static void run_hand_case(GmRuntime *r, double y, int eid, int offhand) {
    GmAction use, idle;
    memset(&use, 0, sizeof use);
    memset(&idle, 0, sizeof idle);
    use.hotbar_sel = idle.hotbar_sel = -1;
    use.use = 1;
    use.do_place = 1;
    reset_case(r, y, eid, offhand, 0);
    gm_runtime_tick(r, use);
    int slot = mob_slot(r, eid);
    CHECK(slot >= 0 && !(r->mobs.sheep_data[slot] & 16)
              && r->server_shear_pending
              && r->server_shear_eid == eid
              && r->server_shear_hand == offhand
              && r->entities.n_active == 0,
          "entity use queues exactly one delayed shearing packet");
    gm_runtime_tick(r, idle);
    slot = mob_slot(r, eid);
    ICStack tool = isr_get_stack(
        &r->player.inv, offhand ? ISR_OFFHAND_SLOT : 0);
    int drops_exact = r->entities.n_active == 3;
    for (int i = 0; i < 3 && drops_exact; ++i) {
        const GmLiveEnt *drop = &r->entities.ents[i];
        drops_exact = drop->item == 35 && drop->count == 1
            && drop->meta == 14 && drop->eid == eid + 1 + i;
    }
    int exact = slot >= 0 && (r->mobs.sheep_data[slot] & 16)
              && (r->mobs.sheep_data[slot] & 15) == 14
              && !r->server_shear_pending
              && tool.item == 359 && tool.count == 1 && tool.meta == 1
              && drops_exact
              && r->next_entity_id == eid + 4
              && r->mobs.entity_random[slot].random.seed
                    == java_lcg_steps(0, 3)
              && r->math_random_seed48
                    == java_lcg_steps(UINT64_C(0x123456789ABC), 24)
              && !r->next_shears_random_valid
              && r->next_shears_random_seed48
                    == java_lcg_steps(UINT64_C(0x3456789ABCDE), 15);
    if (!exact) {
        fprintf(stderr,
            "shear diagnostic hand=%d slot=%d data=%d pending=%d "
            "tool=%d/%d/%d drops=%d drop=%d/%d/%d/eid%d next=%d "
            "entity_rng=%llu math_rng=%llu shear_valid=%d shear_rng=%llu\n",
            offhand, slot, slot >= 0 ? r->mobs.sheep_data[slot] : -1,
            r->server_shear_pending, tool.item, tool.count, tool.meta,
            r->entities.n_active, r->entities.ents[0].item,
            r->entities.ents[0].count, r->entities.ents[0].meta,
            r->entities.ents[0].eid, r->next_entity_id,
            (unsigned long long)(slot >= 0
                ? r->mobs.entity_random[slot].random.seed : 0),
            (unsigned long long)r->math_random_seed48,
            r->next_shears_random_valid,
            (unsigned long long)r->next_shears_random_seed48);
    }
    CHECK(exact,
          offhand ? "offhand shearing executes exact server transition"
                  : "main-hand shearing executes exact server transition");
    CHECK(gm_mobs_event_count(&r->mobs) == 1,
          "runtime shearing emits one causal sound event");
}

int main(void) {
    GmConfig cfg;
    GmRuntime r;
    char err[256];
    test_component_controls();
    gm_config_defaults(&cfg);
    cfg.view_distance = 1;
    cfg.mobs = 0;
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err),
          "shearing runtime initializes");
    if (fail) return 1;
    double y = (double)gm_world_surface_y(r.world, 8, 8) + 1.0;

    run_hand_case(&r, y, 630000, 0);
    run_hand_case(&r, y, 630100, 1);

    GmAction use;
    memset(&use, 0, sizeof use);
    use.hotbar_sel = -1;
    use.use = 1;
    use.do_place = 1;
    reset_case(&r, y, 630200, 0, 1);
    gm_runtime_tick(&r, use);
    CHECK(!r.server_shear_pending
              && !(r.mobs.sheep_data[mob_slot(&r, 630200)] & 16)
              && r.entities.n_active == 0,
          "nearer solid selection box occludes sheep interaction");

    gm_runtime_destroy(&r);
    if (fail) return 1;
    puts("PASS shearing runtime: delayed main/offhand use, exact RNG, occlusion");
    return 0;
}
