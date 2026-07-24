/* Deterministic geometry/UV gates for ui-entity divergences.
 * Build: bash raster/verify/ui_entities/run_gates.sh */
#include "core/types.h"
#include "game/entity_render.h"
#include "assets/mob_atlas.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c, m) do { \
    if (!(c)) { printf("FAIL: %s\n", m); g_fail = 1; } \
    else      { printf("ok:   %s\n", m); } \
} while (0)

static int approx(float a, float b, float e) { return fabsf(a - b) <= e; }

static void bounds(const CrVertex *v, int n, float *mn, float *mx) {
    mn[0] = mn[1] = mn[2] = 1e30f;
    mx[0] = mx[1] = mx[2] = -1e30f;
    for (int i = 0; i < n; ++i) {
        float p[3] = { v[i].pos.x, v[i].pos.y, v[i].pos.z };
        for (int k = 0; k < 3; ++k) {
            if (p[k] < mn[k]) mn[k] = p[k];
            if (p[k] > mx[k]) mx[k] = p[k];
        }
    }
}

int main(void) {
    CrVertex out[8192];
    float mn[3], mx[3];

    printf("== ui_entities geom gates ==\n");

    /* Slime size scale ratios (RenderSlime.preRenderCallback). */
    {
        GmEntityView a, b;
        memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
        a.type = 35; a.y = 0; a.health = 4; a.item_meta = 1;
        b.type = 35; b.y = 0; b.health = 4; b.item_meta = 4;
        int na = gm_entities_emit(&a, 1, out, 8192);
        bounds(out, na, mn, mx);
        float ha = mx[1] - mn[1], wa = mx[0] - mn[0];
        int nb = gm_entities_emit(&b, 1, out, 8192);
        bounds(out, nb, mn, mx);
        float hb = mx[1] - mn[1], wb = mx[0] - mn[0];
        CHECK(approx(hb / ha, 4.0f, 0.05f), "slime height scales with size");
        CHECK(approx(wb / wa, 4.0f, 0.05f), "slime width scales with size");
    }

    /* Magma size 1 vs 2. */
    {
        GmEntityView a, b;
        memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
        a.type = 27; a.y = 0; a.health = 4; a.item_meta = 1;
        b.type = 27; b.y = 0; b.health = 4; b.item_meta = 2;
        int na = gm_entities_emit(&a, 1, out, 8192);
        bounds(out, na, mn, mx);
        float ha = mx[1] - mn[1];
        int nb = gm_entities_emit(&b, 1, out, 8192);
        bounds(out, nb, mn, mx);
        float hb = mx[1] - mn[1];
        CHECK(approx(hb / ha, 2.0f, 0.05f), "magma size doubles height");
        CHECK(na == 9 * 36 && nb == 9 * 36, "magma always 9 ModelMagmaCube boxes");
    }

    /* Fireball scale patch: type morph + fire prep. */
    {
        GmEntityView v[2];
        memset(v, 0, sizeof v);
        v[0].type = 30; v[0].item_id = 385;
        v[1].type = 30; v[1].item_id = 385;
        int pt[2] = { 3, 5 };
        gm_entity_patch_large_fireballs(pt, 2, v, 2);
        CHECK(v[0].type == 30 && v[0].item_meta == 1, "small fireball scale path");
        CHECK(v[1].type == 33 && v[1].item_id == 385 && v[1].item_meta == 2,
              "large fireball scale-2 path (type 33 + fire_charge UV)");
        /* Measured billboard half-extent would require item_render; type is the
         * contract item_render reads for scale 2.0 vs 0.5. */
        CHECK(gm_entity_billboard_item("EntityLargeFireball") == 385,
              "large fireball particle icon is fire_charge");
        CHECK(gm_entity_billboard_item("EntitySmallFireball") == 385,
              "small fireball particle icon is fire_charge");
    }

    /* Dragon death rays at deathTicks=100. */
    {
        GmEntityView d;
        memset(&d, 0, sizeof d);
        d.type = 9; d.death_ticks = 100; d.health = 0;
        int n = gm_dragon_death_rays_emit(&d, 1, out, 8192);
        int rays = (int)((0.5f + 0.25f) / 2.0f * 60.0f);
        CHECK(n == rays * 6, "deathTicks=100 emits LayerEnderDragonDeath ray count");
        d.death_ticks = 200;
        int n2 = gm_dragon_death_rays_emit(&d, 1, out, 8192);
        CHECK(n2 > n, "more death ticks => more rays");
        d.death_ticks = 0;
        CHECK(gm_dragon_death_rays_emit(&d, 1, out, 8192) == 0, "no rays when alive");
    }

    /* Dragon dissolve still emits body at mid-death, none at f=1. */
    {
        GmEntityView d;
        memset(&d, 0, sizeof d);
        d.type = 9; d.y = 80; d.health = 0; d.death_ticks = 100;
        int mid = gm_entities_emit(&d, 1, out, 8192);
        d.death_ticks = 200;
        int end = gm_entities_emit(&d, 1, out, 8192);
        CHECK(mid > 0, "mid-death dragon still has some boxes");
        CHECK(end == 0, "deathTicks=200 dissolves all dragon boxes");
    }

    /* Enderman portal particles: 8 quads, camera-facing. */
    {
        GmEntityView e;
        memset(&e, 0, sizeof e);
        e.type = 6; e.y = 64; e.age = 3; e.ent_id = 1; e.health = 40;
        int n = gm_particles_emit(&e, 1, 90.0f, 0.0f, out, 8192);
        CHECK(n == 48, "enderman: 8 portal particle quads (48 verts)");
        /* UVs inside enderman skin rect (stand-in until particles.png atlas). */
        const CrMobSprite *sp = &CR_MOB_SPRITES[CR_MOB_ENDERMAN];
        float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
        int bad = 0;
        for (int i = 0; i < n; ++i) {
            float px = out[i].uv.x * aw, py = out[i].uv.y * ah;
            if (px < sp->x0 - 1 || px > sp->x1 + 1 ||
                py < sp->y0 - 1 || py > sp->y1 + 1) bad++;
        }
        CHECK(bad == 0, "particle UVs stay inside stand-in sprite rect");
    }

    printf("\n%s\n", g_fail ? "*** GEOM GATES FAILED ***" : "ALL GEOM GATES PASSED");
    return g_fail;
}
