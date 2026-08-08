/* gamerules_wire: unit test for the P0 GameRules wire-up (PORT_MATRIX). For each sim-affecting
 * rule, toggle it and assert the divergent behavior, and confirm the default instance reproduces
 * today's behavior. Exit 0 = all pass, nonzero = a failure (printed). No golden; pure asserts.
 *
 * Rules exercised (consumer file:function):
 *   keepInventory       player_death.h   pd_check_death_gr    (EntityPlayer.onDeath drop)
 *   mobGriefing         ender_dragon_damage.h edd_destroy_blocks_gr (EntityDragon.destroyBlocksInAABB)
 *   doTileDrops         player_survival.h psv_tick_gr/psv_run_gr (Block.harvestBlock -> dropBlockAsItem)
 *   naturalRegeneration player_vitals.h  pv_on_update_gr      (FoodStats.onUpdate flag)
 *   doFireTick          block_tickers.h  bt_tick_fire_gr      (BlockFire.updateTick first line) */
#include <stdio.h>
#include <stdlib.h>
#include "../core/mc_gamerules.h"
#include "../core/player_death.h"
#include "../core/player_vitals.h"
#include "../core/ender_dragon_damage.h"
#include "../core/player_survival.h"
#include "../core/block_tickers.h"

static int g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { printf("  ok   %s\n", msg); } \
    else      { printf("  FAIL %s\n", msg); ++g_fail; } } while (0)

/* ---- keepInventory: EntityPlayer.onDeath drops inventory unless keepInventory ---- */
static void test_keep_inventory(void) {
    printf("[keepInventory] player_death.h pd_check_death_gr\n");
    McGameRules keep = mc_gamerules_default(); keep.keepInventory = 1;
    McGameRules drop = mc_gamerules_default(); drop.keepInventory = 0;

    PdState sk; pd_init(&sk); sk.pv.health = 0.0f;    /* force lethal */
    pd_check_death_gr(&sk, &keep);
    PdState sd; pd_init(&sd); sd.pv.health = 0.0f;
    pd_check_death_gr(&sd, &drop);

    CHECK(sk.dead == 1 && sd.dead == 1, "both register a death");
    CHECK(sk.deaths == 1 && sd.deaths == 1, "deaths incremented");
    CHECK(sk.inv_count == PD_INIT_INV, "keepInventory=1 -> inventory retained");
    CHECK(sd.inv_count == 0, "keepInventory=0 -> inventory dropped");

    /* default rules == drop-on-death (bit-identical to prior emitted behavior) */
    PdState sdef; pd_init(&sdef); sdef.pv.health = 0.0f; pd_check_death(&sdef);
    CHECK(sdef.inv_count == 0, "default rules drop inventory on death");
}

/* ---- mobGriefing: EntityDragon.destroyBlocksInAABB only when mobGriefing ---- */
static void test_mob_griefing(void) {
    printf("[mobGriefing] ender_dragon_damage.h edd_destroy_blocks_gr\n");
    McGameRules on  = mc_gamerules_default(); on.mobGriefing  = 1;
    McGameRules off = mc_gamerules_default(); off.mobGriefing = 0;
    const u64 seed = 12345ULL;

    EddWorld wo; edd_init_scene(&wo, seed);
    u32 broken_on = edd_destroy_blocks_gr(&wo, &on);

    EddWorld wf; edd_init_scene(&wf, seed);
    u32 broken_off = edd_destroy_blocks_gr(&wf, &off);

    CHECK(broken_on > 0, "mobGriefing=1 -> dragon breaks blocks (ON case fires)");
    CHECK(broken_off == 0, "mobGriefing=0 -> dragon breaks no blocks");
}

/* ---- doTileDrops: broken blocks yield item drops only when doTileDrops ---- */
static void test_tile_drops(void) {
    printf("[doTileDrops] player_survival.h psv_run_gr\n");
    McGameRules on  = mc_gamerules_default(); on.doTileDrops  = 1;
    McGameRules off = mc_gamerules_default(); off.doTileDrops = 0;
    const i64 seed = 12345LL;
    const int nticks = PSV_NTICKS;

    McSinTable *st = (McSinTable *)malloc(sizeof(McSinTable));
    mc_sin_table_init(st);
    ChunkPrimer *primer = (ChunkPrimer *)malloc(sizeof(ChunkPrimer));
    CpScratch *sc = (CpScratch *)malloc(sizeof(CpScratch));
    Chunk *a = (Chunk *)malloc(sizeof(Chunk) * PSV_NCHUNKS);
    Chunk *b = (Chunk *)malloc(sizeof(Chunk) * PSV_NCHUNKS);

    PsvPlayer pon, poff;
    psv_run_gr(a, b, primer, sc, st, seed, nticks, &on,  &pon,  0);
    psv_run_gr(a, b, primer, sc, st, seed, nticks, &off, &poff, 0);

    int total_on  = isr_hotbar_total(&pon.inv)  + isr_main_total(&pon.inv);
    int total_off = isr_hotbar_total(&poff.inv) + isr_main_total(&poff.inv);

    CHECK(pon.break_events > 0, "blocks actually broke (ON case fires)");
    CHECK(pon.break_events == poff.break_events, "breaks happen regardless of doTileDrops");
    CHECK(total_on > total_off, "doTileDrops=1 -> more items than doTileDrops=0");

    free(b); free(a); free(sc); free(primer); free(st);
}

/* ---- naturalRegeneration: FoodStats.onUpdate regen branches gated by the flag ---- */
static void test_natural_regen(void) {
    printf("[naturalRegeneration] player_vitals.h pv_on_update_gr\n");
    McGameRules on  = mc_gamerules_default(); on.naturalRegeneration  = 1;
    McGameRules off = mc_gamerules_default(); off.naturalRegeneration = 0;

    /* health<max, food=20, saturation=6, foodTimer=9 -> flag=1 heals on this tick. */
    PvStats son  = { 20, 6.0f, 0.0f, 9, 10.0f, 20.0f };
    PvStats soff = { 20, 6.0f, 0.0f, 9, 10.0f, 20.0f };
    pv_on_update_gr(&son,  &on);
    pv_on_update_gr(&soff, &off);

    CHECK(son.health > 10.0f, "naturalRegeneration=1 -> heals (ON case fires)");
    CHECK(soff.health == 10.0f, "naturalRegeneration=0 -> no heal");
}

/* ---- doFireTick: BlockFire.updateTick returns immediately when doFireTick=0 ----
 * bt_tick_fire's spread sets fire into a neighbor position only when that slot is AIR in `next`,
 * so a static copy-forward scene never converts (verified: 0/2000 seeds). To expose the guard we
 * prime the target slot to AIR: with doFireTick=1 the spread code runs and lights it (subject to
 * the ~15% hash roll); with doFireTick=0 bt_tick_fire_gr returns immediately and never touches it.
 * Count over many ticks so the ON case is guaranteed to fire at least once. */
static int fire_spread_trials(const McGameRules *gr, u64 seed, int trials) {
    u16 air = mc_state(BLK_AIR, 0), fire = mc_state(51, 0), planks = mc_state(BLK_PLANKS, 0);
    int lit = 0;
    for (int t = 0; t < trials; ++t) {
        BtWorld w; w.seed = seed; w.tick = t; w.cur = 0;
        u16 *now = w.blocks_a, *next = w.blocks_b;
        for (int i = 0; i < BT_VOL; ++i) now[i] = air;
        bt_set(now, 2, 10, 2, fire);      /* fire source */
        bt_set(now, 2, 10, 3, planks);    /* flammable neighbor (+z) */
        bt_copy(next, now);
        bt_set(next, 2, 10, 3, air);      /* prime target slot to AIR so spread is observable */
        bt_tick_fire_gr(&w, now, next, gr);
        if (mc_state_id(bt_get(next, 2, 10, 3)) == 51) ++lit;
    }
    return lit;
}
static void test_fire_tick(void) {
    printf("[doFireTick] block_tickers.h bt_tick_fire_gr\n");
    McGameRules on  = mc_gamerules_default(); on.doFireTick  = 1;
    McGameRules off = mc_gamerules_default(); off.doFireTick = 0;
    const u64 seed = 12345ULL;

    int lit_on  = fire_spread_trials(&on,  seed, 256);
    int lit_off = fire_spread_trials(&off, seed, 256);

    CHECK(lit_on > 0, "doFireTick=1 -> fire spreads (ON case fires)");
    CHECK(lit_off == 0, "doFireTick=0 -> updateTick no-op, fire never spreads");
}

int main(void) {
    test_keep_inventory();
    test_mob_griefing();
    test_tile_drops();
    test_natural_regen();
    test_fire_tick();
    if (g_fail) { printf("\nFAILED: %d assertion(s)\n", g_fail); return 1; }
    printf("\nALL GAMERULE WIRE TESTS PASSED\n");
    return 0;
}
