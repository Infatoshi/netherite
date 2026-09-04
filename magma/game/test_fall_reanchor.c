/* game/test_fall_reanchor.c - regression: authoritative set_block_post
 * reanchors restore sand/gravel finals without scheduling a second fall;
 * ordinary set_block retains vanilla gravity side effects.
 *
 * Coverage:
 *   A/B unsupported sand via set_block -> schedule + entity + landing
 *   C   reanchor of landing final schedules nothing
 *   C2  reanchor of unsupported sand final invents no fall
 *   D   repeated same-value reanchors stay inert for gravity
 *   E/F ordinary set_block still schedules sand and gravel falls
 *   G   dual-path mid-flight reanchor ends with one sand cell
 *   H   held creative prefers a closer block over a farther falling AABB
 *   I   creative clickBlock blockHitDelay=5 holds a re-landed cell
 *
 * Build+run: bash game/test_fall_reanchor.sh
 */
#include "game/runtime.h"
#include "mc_blocks.h"

#include <stdio.h>
#include <string.h>

static int fail;
#define CHECK(C, M) do { \
    if (!(C)) { fprintf(stderr, "FAIL: %s\n", (M)); fail = 1; } \
} while (0)

static void idle_ticks(GmRuntime *r, int n)
{
    GmAction a;
    memset(&a, 0, sizeof a);
    a.hotbar_sel = -1;
    for (int i = 0; i < n; ++i)
        gm_runtime_tick(r, a);
}

static int count_fall_ents(const GmLiveSim *s)
{
    int n = 0;
    for (int i = 0; i < GM_LIVE_MAX; ++i)
        if (s->ents[i].active && s->ents[i].type == 2)
            ++n;
    return n;
}

static int count_fall_updates(const GmLiveSim *s)
{
    int n = 0;
    for (int i = 0; i < GM_LIVE_FALL_UPDATES; ++i)
        if (s->fall_updates[i].active)
            ++n;
    return n;
}

static int has_fall_update_at(const GmLiveSim *s, int x, int y, int z, int id)
{
    for (int i = 0; i < GM_LIVE_FALL_UPDATES; ++i) {
        const GmLiveFallUpdate *u = &s->fall_updates[i];
        if (u->active && u->x == x && u->y == y && u->z == z &&
            u->block_id == id)
            return 1;
    }
    return 0;
}

static void clear_gravity(GmLiveSim *s)
{
    for (int i = 0; i < GM_LIVE_MAX; ++i) {
        if (s->ents[i].active && s->ents[i].type == 2) {
            s->ents[i].active = 0;
            if (s->n_active > 0) s->n_active--;
        }
        s->fall_landings[i].active = 0;
    }
    for (int i = 0; i < GM_LIVE_FALL_UPDATES; ++i)
        s->fall_updates[i].active = 0;
}

static void clear_column(GmRuntime *r, int x, int y0, int y1, int z)
{
    for (int y = y0; y <= y1; ++y)
        gm_runtime_set_block(r, x, y, z, 0, 0);
}

int main(void)
{
    GmConfig cfg;
    gm_config_defaults(&cfg);
    cfg.view_distance = 2;
    GmRuntime r;
    char err[256];
    CHECK(gm_runtime_init(&r, &cfg, err, sizeof err), "runtime initializes");
    if (fail) return 1;

    const int X = 20, Z = 20, YF = 100;
    const int YS = 104;

    for (int i = 0; i < GM_LIVE_MAX; ++i)
        r.entities.ents[i].active = 0;
    r.entities.n_active = 0;
    r.entities.plant_active = 0;

    /* ---- A/B: ordinary set_block unsupported sand falls and lands ---- */
    fprintf(stderr, "== A/B ordinary unsupported sand fall ==\n");
    clear_column(&r, X, YF, YS + 2, Z);
    CHECK(gm_runtime_set_block(&r, X, YF, Z, BLK_STONE, 0), "floor stone");
    CHECK(gm_runtime_set_block(&r, X, YS, Z, BLK_SAND, 0), "place unsupported sand");
    CHECK(has_fall_update_at(&r.entities, X, YS, Z, BLK_SAND),
          "set_block schedules fall update for unsupported sand");
    CHECK(count_fall_ents(&r.entities) == 0,
          "no falling entity until scheduled update fires");

    int saw_ent = 0;
    int land_y = -1;
    for (int t = 0; t < 40; ++t) {
        idle_ticks(&r, 1);
        int nfb = count_fall_ents(&r.entities);
        if (nfb > 0) saw_ent = 1;
        if (nfb == 0 && saw_ent) {
            if (gm_world_block(r.world, X, YF + 1, Z) == BLK_SAND) {
                land_y = YF + 1;
                break;
            }
            idle_ticks(&r, 1);
            if (gm_world_block(r.world, X, YF + 1, Z) == BLK_SAND) {
                land_y = YF + 1;
                break;
            }
        }
    }
    CHECK(saw_ent, "falling sand entity spawned");
    CHECK(land_y == YF + 1, "sand lands on floor (YF+1)");
    CHECK(gm_world_block(r.world, X, YS, Z) == 0, "source cell is air after fall");
    CHECK(gm_world_block(r.world, X, land_y, Z) == BLK_SAND, "landing cell is sand");
    CHECK(count_fall_ents(&r.entities) == 0, "no active falling entity after land");
    fprintf(stderr, "   landed sand at y=%d after ordinary set_block fall\n", land_y);

    idle_ticks(&r, 6);
    CHECK(count_fall_ents(&r.entities) == 0,
          "supported landed sand does not re-spawn falling entity");
    CHECK(gm_world_block(r.world, X, land_y, Z) == BLK_SAND,
          "landed sand remains after cascade drain");

    clear_gravity(&r.entities);
    CHECK(count_fall_updates(&r.entities) == 0, "gravity bookkeeping cleared");

    /* ---- C: reanchor landing final must not schedule fall ---- */
    fprintf(stderr, "== C authoritative reanchor of landing final ==\n");
    CHECK(gm_world_block(r.world, X, land_y, Z) == BLK_SAND, "pre: landing is sand");
    CHECK(gm_runtime_reanchor_block(&r, X, land_y, Z, BLK_SAND, 0),
          "reanchor landing sand final");
    CHECK(!has_fall_update_at(&r.entities, X, land_y, Z, BLK_SAND),
          "reanchor of landing sand does not schedule fall_update");
    CHECK(count_fall_updates(&r.entities) == 0,
          "reanchor landing leaves fall_updates empty");
    CHECK(gm_runtime_reanchor_block(&r, X, YS, Z, 0, 0),
          "reanchor source air final");
    CHECK(count_fall_updates(&r.entities) == 0,
          "reanchor source air does not schedule fall_update");

    int second_ent = 0;
    for (int t = 0; t < 10; ++t) {
        idle_ticks(&r, 1);
        if (count_fall_ents(&r.entities) > 0) {
            second_ent = 1;
            break;
        }
    }
    CHECK(!second_ent,
          "reanchor of supported landing does not spawn a second falling entity");
    CHECK(gm_world_block(r.world, X, land_y, Z) == BLK_SAND,
          "landing sand stable after reanchor ticks");

    /* ---- C2: reanchor unsupported sand final invents no gravity work ---- */
    fprintf(stderr, "== C2 reanchor unsupported sand final ==\n");
    const int X2 = 22, Z2 = 20;
    clear_gravity(&r.entities);
    clear_column(&r, X2, YF, YS + 2, Z2);
    clear_gravity(&r.entities);
    CHECK(gm_runtime_set_block(&r, X2, YF, Z2, BLK_STONE, 0), "C2 floor");
    clear_gravity(&r.entities);
    CHECK(gm_runtime_reanchor_block(&r, X2, YS, Z2, BLK_SAND, 0),
          "reanchor unsupported sand final");
    CHECK(!has_fall_update_at(&r.entities, X2, YS, Z2, BLK_SAND),
          "reanchor unsupported sand does not schedule fall");
    CHECK(gm_world_block(r.world, X2, YS, Z2) == BLK_SAND,
          "reanchor wrote unsupported sand truth");
    int c2_ent = 0;
    for (int t = 0; t < 20; ++t) {
        idle_ticks(&r, 1);
        if (count_fall_ents(&r.entities) > 0) {
            c2_ent = 1;
            break;
        }
    }
    CHECK(!c2_ent, "reanchor unsupported sand does not spawn falling entity");
    CHECK(gm_world_block(r.world, X2, YS, Z2) == BLK_SAND,
          "unsupported sand final remains (no invented fall cleared it)");

    /* ---- D: repeated same-value reanchors ---- */
    fprintf(stderr, "== D repeated same-value reanchors ==\n");
    clear_gravity(&r.entities);
    CHECK(gm_world_block(r.world, X, land_y, Z) == BLK_SAND, "D pre sand present");
    for (int i = 0; i < 5; ++i)
        CHECK(gm_runtime_reanchor_block(&r, X, land_y, Z, BLK_SAND, 0),
              "same-value reanchor");
    CHECK(count_fall_updates(&r.entities) == 0,
          "same-value reanchors arm no fall_updates");
    for (int t = 0; t < 8; ++t) idle_ticks(&r, 1);
    CHECK(count_fall_ents(&r.entities) == 0,
          "repeated same-value reanchors do not spawn falling entities");
    CHECK(gm_world_block(r.world, X, land_y, Z) == BLK_SAND,
          "sand remains after repeated reanchors");

    /* ---- E: ordinary set_block still schedules ---- */
    fprintf(stderr, "== E ordinary set_block retains fall side effects ==\n");
    clear_gravity(&r.entities);
    clear_column(&r, X, YF + 1, YS + 2, Z);
    clear_gravity(&r.entities);
    CHECK(gm_runtime_set_block(&r, X, YS, Z, BLK_SAND, 0), "E place sand");
    CHECK(has_fall_update_at(&r.entities, X, YS, Z, BLK_SAND),
          "ordinary set_block still schedules fall");
    int e_ent = 0;
    for (int t = 0; t < 30; ++t) {
        idle_ticks(&r, 1);
        if (count_fall_ents(&r.entities) > 0) {
            e_ent = 1;
            break;
        }
    }
    CHECK(e_ent, "ordinary set_block still spawns falling sand entity");

    /* ---- F: gravel ordinary path ---- */
    fprintf(stderr, "== F ordinary gravel fall ==\n");
    clear_gravity(&r.entities);
    const int X3 = 24;
    clear_column(&r, X3, YF, YS + 2, Z);
    CHECK(gm_runtime_set_block(&r, X3, YF, Z, BLK_STONE, 0), "F floor");
    clear_gravity(&r.entities);
    CHECK(gm_runtime_set_block(&r, X3, YS, Z, BLK_GRAVEL, 0), "F place gravel");
    CHECK(has_fall_update_at(&r.entities, X3, YS, Z, BLK_GRAVEL),
          "ordinary set_block schedules gravel fall");
    int f_ent = 0;
    for (int t = 0; t < 30; ++t) {
        idle_ticks(&r, 1);
        if (count_fall_ents(&r.entities) > 0) {
            f_ent = 1;
            break;
        }
    }
    CHECK(f_ent, "ordinary gravel falls as entity");

    /* ---- G: mid-flight reanchor must not arm a second scheduled fall.
     * Concurrent live EntityFallingBlock + early landing reanchor can still
     * stack a cell from entity placement (dual ownership of the block);
     * that is outside this reanchor-schedule fix. Pin only that reanchor
     * does not invent a second fall_update / second entity. ---- */
    fprintf(stderr, "== G dual-path: local fall + mid-flight reanchor ==\n");
    clear_gravity(&r.entities);
    const int X4 = 26;
    clear_column(&r, X4, YF, YS + 2, Z);
    CHECK(gm_runtime_set_block(&r, X4, YF, Z, BLK_STONE, 0), "G floor");
    clear_gravity(&r.entities);
    CHECK(gm_runtime_set_block(&r, X4, YS, Z, BLK_SAND, 0), "G place sand");
    int g_ent = 0;
    for (int t = 0; t < 20; ++t) {
        idle_ticks(&r, 1);
        if (count_fall_ents(&r.entities) > 0 &&
            gm_world_block(r.world, X4, YS, Z) == 0) {
            g_ent = 1;
            break;
        }
    }
    CHECK(g_ent, "G entity in flight with source cleared");
    int ents_mid = count_fall_ents(&r.entities);
    CHECK(gm_runtime_reanchor_block(&r, X4, YF + 1, Z, BLK_SAND, 0),
          "G reanchor early landing final");
    CHECK(!has_fall_update_at(&r.entities, X4, YF + 1, Z, BLK_SAND),
          "mid-flight reanchor does not schedule fall at landing");
    int peak_ents = ents_mid;
    for (int t = 0; t < 40; ++t) {
        idle_ticks(&r, 1);
        int n = count_fall_ents(&r.entities);
        if (n > peak_ents) peak_ents = n;
    }
    fprintf(stderr, "   G peak_ents=%d (mid=%d)\n", peak_ents, ents_mid);
    CHECK(peak_ents == ents_mid,
          "mid-flight reanchor does not spawn an additional falling entity");
    CHECK(gm_world_block(r.world, X4, YF + 1, Z) == BLK_SAND,
          "reanchored landing sand remains written");

    /* ---- H: getMouseOver entity-vs-block distance. A falling AABB further
     * along the look ray must not steal a held creative attack from a closer
     * solid. Vanilla: entity wins only if intercept < block hit. */
    fprintf(stderr, "== H creative attack prefers closer block over far falling ==\n");
    clear_gravity(&r.entities);
    {
        const int XH = 28, ZH_NEAR = 21, ZH_FAR = 23;
        clear_column(&r, XH, YF, YS + 8, ZH_NEAR);
        clear_column(&r, XH, YF, YS + 8, ZH_FAR);
        CHECK(gm_runtime_set_block(&r, XH, YF, ZH_FAR, BLK_STONE, 0), "H far floor");
        clear_gravity(&r.entities);
        CHECK(gm_runtime_set_block(&r, XH, 102, ZH_FAR, BLK_SAND, 0), "H far sand");
        int h_ent = 0;
        for (int t = 0; t < 40; ++t) {
            idle_ticks(&r, 1);
            if (count_fall_ents(&r.entities) > 0 &&
                gm_world_block(r.world, XH, 102, ZH_FAR) == 0) {
                h_ent = 1;
                break;
            }
        }
        CHECK(h_ent, "H far sand is a falling entity");
        CHECK(gm_runtime_set_block(&r, XH, 102, ZH_NEAR, BLK_STONE, 0),
              "H closer stone on the look ray");
        gm_runtime_set_pose(&r, XH + 0.5, 101.0, 18.5, 0.0f, 0.0f);
        r.tape_creative = 1;
        CHECK(count_fall_ents(&r.entities) > 0,
              "falling entity is present when the held attack starts");
        {
            GmAction atk;
            memset(&atk, 0, sizeof atk);
            atk.attack = 1;
            atk.hotbar_sel = -1;
            for (int t = 0; t < 4; ++t)
                gm_runtime_tick(&r, atk);
        }
        CHECK(gm_world_block(r.world, XH, 102, ZH_NEAR) == 0,
              "closer stone is removed; far falling AABB does not steal the attack");
    }

    /* ---- I: clickBlock creative writes blockHitDelay=5
     * (PlayerControllerMP.java:237-242). sendClickBlockToController then
     * sees air and does not decrement (Minecraft.java:1500-1508).
     * onPlayerDamageBlock delay>0 decrements and returns
     * (PlayerControllerMP.java:301-305). A re-placed cell under a held
     * creative attack must survive the five countdown ticks. */
    fprintf(stderr, "== I creative blockHitDelay holds a re-landed cell ==\n");
    clear_gravity(&r.entities);
    {
        const int XI = 30, ZI = 21;
        GmPlayerCtlSnap snap;
        GmAction atk;
        clear_column(&r, XI, YF, YS + 8, ZI);
        CHECK(gm_runtime_set_block(&r, XI, 100, 18, BLK_STONE, 0),
              "I floor under player");
        CHECK(gm_runtime_set_block(&r, XI, 102, ZI, BLK_STONE, 0), "I stone");
        gm_runtime_set_pose(&r, XI + 0.5, 101.0, 18.5, 0.0f, 0.0f);
        r.tape_creative = 1;
        memset(&atk, 0, sizeof atk);
        atk.attack = 1;
        atk.hotbar_sel = -1;
        gm_runtime_tick(&r, atk);
        CHECK(gm_world_block(r.world, XI, 102, ZI) == 0,
              "I press tick destroys the stone");
        gm_player_ctl_dig_export(&r.ctl, &snap);
        CHECK(snap.dig_delay == 5,
              "I clickBlock creative writes blockHitDelay=5");
        CHECK(gm_runtime_set_block(&r, XI, 102, ZI, BLK_STONE, 0),
              "I re-place stone (re-land)");
        for (int t = 0; t < 5; ++t) {
            gm_runtime_tick(&r, atk);
            CHECK(gm_world_block(r.world, XI, 102, ZI) == BLK_STONE,
                  "I re-landed stone survives delay countdown");
            gm_player_ctl_dig_export(&r.ctl, &snap);
            CHECK(snap.dig_delay == 4 - t,
                  "I onPlayerDamageBlock decrements delay without breaking");
        }
        CHECK(snap.dig_delay == 0, "I delay is 0 after five held ticks");
        gm_runtime_tick(&r, atk);
        CHECK(gm_world_block(r.world, XI, 102, ZI) == 0,
              "I sixth held tick destroys after delay expires");
        gm_player_ctl_dig_export(&r.ctl, &snap);
        CHECK(snap.dig_delay == 5,
              "I creative onPlayerDamageBlock re-arms blockHitDelay=5");
    }

    gm_runtime_destroy(&r);

    if (fail) {
        fprintf(stderr, "fall_reanchor: FAIL\n");
        return 1;
    }
    fprintf(stderr, "fall_reanchor: PASS\n");
    return 0;
}
