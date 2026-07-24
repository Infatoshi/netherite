/* Deterministic geometry/UV/topology gates for ui-entity work.
 * These are NOT pixel gates. Build: bash raster/verify/ui_entities/run_gates.sh */
#include "core/types.h"
#include "game/game.h"          /* GmEntityView + MAGMA_GAME_H before entity_render.h */
#include "game/entity_render.h"
#include "game/item_render.h"
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

    printf("== ui_entities geom gates (not pixel gates) ==\n");

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

    /* LayerSlimeGel outer shell. */
    {
        GmEntityView s;
        memset(&s, 0, sizeof s);
        s.type = 35; s.item_meta = 2; s.health = 4;
        int n = gm_slime_gel_emit(&s, 1, out, 8192);
        CHECK(n == 36, "LayerSlimeGel emits one 8x8x8 box");
        /* Java color(1,1,1,1): translucency is slime.png texel alpha only. */
        CHECK(out[0].tint.a == 255, "gel shell vertex alpha is opaque (texel alpha)");
    }

    /* RH Rodrigues / OpenGL rotate signs for death-ray axes. */
    {
        CHECK(gm_entity_rot_rx90_maps_y_to_z(),
              "Rx(+90) maps +Y -> +Z (right-handed OpenGL)");
        CHECK(gm_entity_rot_axes_are_unit(),
              "composed +90 axis rotations keep unit columns");
    }

    /* Magma squish from real view field (not limb_swing). */
    {
        GmEntityView a, b;
        memset(&a, 0, sizeof a); memset(&b, 0, sizeof b);
        a.type = 27; a.item_meta = 2; a.squish = 0.0f;
        b.type = 27; b.item_meta = 2; b.squish = 1.0f;
        int na = gm_entities_emit(&a, 1, out, 8192);
        bounds(out, na, mn, mx);
        float ha = mx[1] - mn[1];
        int nb = gm_entities_emit(&b, 1, out, 8192);
        bounds(out, nb, mn, mx);
        float hb = mx[1] - mn[1];
        /* preRenderCallback: squish stretches Y relative to XZ. */
        CHECK(hb > ha * 1.05f, "magma squishFactor stretches height");
    }

    /* Fire overlay extents: small width 0.3125 vs large width 1.0. */
    {
        GmEntityView sm, lg;
        memset(&sm, 0, sizeof sm); memset(&lg, 0, sizeof lg);
        sm.type = 30; sm.item_id = 385; sm.item_meta = 1;
        lg.type = 30; lg.item_id = 385; lg.item_meta = 2;
        int ns = gm_small_fireball_fire_emit(&sm, 1, 0.0f, out, 256);
        bounds(out, ns, mn, mx);
        float ws = mx[0] - mn[0];
        int nl = gm_small_fireball_fire_emit(&lg, 1, 0.0f, out, 256);
        bounds(out, nl, mn, mx);
        float wl = mx[0] - mn[0];
        CHECK(ns == 12 && nl == 12, "both fireballs emit two fire layers");
        CHECK(approx(ws, 0.4375f, 1e-4f), "small fire scale = 0.3125*1.4");
        CHECK(approx(wl, 1.4f, 1e-4f), "large fire scale = 1.0*1.4");
        CHECK(approx(wl / ws, 1.0f / 0.3125f, 0.01f), "large/small fire width ratio");
    }

    /* Death rays: 9 verts/ray, blend inputs (white center, magenta rim). */
    {
        GmEntityView d;
        memset(&d, 0, sizeof d);
        d.type = 9; d.death_ticks = 100; d.health = 0;
        int n = gm_dragon_death_rays_emit(&d, 1, out, 8192);
        int rays = (int)((0.5f + 0.25f) / 2.0f * 60.0f);
        CHECK(n == rays * 9, "deathTicks=100: 3 tris/ray (5-vert fan)");
        CHECK(out[0].tint.a > 0 && out[1].tint.a == 0,
              "ray smooth shading inputs: center alpha, rim alpha 0");
        d.death_ticks = 0;
        CHECK(gm_dragon_death_rays_emit(&d, 1, out, 8192) == 0, "no rays when alive");
    }

    /* Dragon dissolve markers (per-texel path uses light/ao; not box drop). */
    {
        GmEntityView d;
        memset(&d, 0, sizeof d);
        d.type = 9; d.y = 80; d.health = 0; d.death_ticks = 100;
        int mid = gm_entities_emit(&d, 1, out, 8192);
        CHECK(mid > 200, "mid-death dragon emits full body geometry");
        int ok = 1;
        for (int i = 0; i < mid; ++i)
            if (!(out[i].light < 0.0f && fabsf(out[i].ao - 0.5f) < 1e-4f)) ok = 0;
        CHECK(ok, "dissolve verts carry light<0 and ao=deathTicks/200");
        d.death_ticks = 200;
        CHECK(gm_entities_emit(&d, 1, out, 8192) == 0, "f=1 emits nothing");
        float uo = 0, vo = 0;
        gm_entity_dissolve_mask(&uo, &vo);
        CHECK(fabsf(uo - 0.25f) < 1e-4f && fabsf(vo) < 1e-4f,
              "dissolve mask offset maps dragon -> dragon_exploding");
    }

    /* Particles: real particles.png UVs + vanilla-ish counts. */
    {
        GmEntityView e;
        memset(&e, 0, sizeof e);
        e.type = 6; e.y = 64; e.age = 3; e.ent_id = 1; e.health = 40;
        int n = gm_particles_emit(&e, 1, 90.0f, 0.0f, out, 8192);
        CHECK(n == 90 * 6, "enderman portal cloud: 90 quads");
        const CrMobSprite *sp = &CR_MOB_SPRITES[CR_MOB_PARTICLES];
        float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
        int bad = 0;
        for (int i = 0; i < n; ++i) {
            float px = out[i].uv.x * aw, py = out[i].uv.y * ah;
            if (px < sp->x0 - 1 || px > sp->x1 + 1 ||
                py < sp->y0 - 1 || py > sp->y1 + 1) bad++;
        }
        CHECK(bad == 0, "particle UVs stay inside particles.png sheet");
        CHECK(CR_MOB_PARTICLES >= 0 && CR_MOB_DRAGON_EXPLODING >= 0,
              "particles + dragon_exploding packed in mob atlas");
    }

    /* Fragment-level dissolve: shade discards when exploding alpha <= thr.
     * Not a Java pixel gate — proves the interactive shade path works. */
    {
        CrTexture ea = gm_entity_atlas();
        CrShadeCtx sh = {0};
        sh.atlas = &ea;
        sh.alpha_mask = 1;
        sh.alpha_test = 1;
        sh.layer = CR_LAYER_CUTOUT;
        gm_entity_dissolve_mask(&sh.mask_u_off, &sh.mask_v_off);
        /* Sample a known dragon UV near the body center of the atlas. */
        const CrMobSprite *d = &CR_MOB_SPRITES[CR_MOB_DRAGON];
        float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
        CrFragment frag = {0};
        frag.uv.x = ((float)d->x0 + 128.0f) / aw;
        frag.uv.y = ((float)d->y0 + 128.0f) / ah;
        frag.light = -1.0f;
        frag.ao = 0.0f; /* thr=0: only alpha==0 discards */
        frag.tint = (CrRgba){255,255,255,255};
        frag.blk = 15.0f;
        CrRgba c0 = cr_shade(&sh, &frag);
        frag.ao = 1.0f; /* thr=1: all a<=1 discard */
        CrRgba c1 = cr_shade(&sh, &frag);
        CHECK(c0.a != 0, "dissolve thr=0 keeps opaque exploding texels");
        CHECK(c1.a == 0, "dissolve thr=1 discards all fragments");
    }

    printf("\n%s\n", g_fail ? "*** GEOM GATES FAILED ***" : "ALL GEOM GATES PASSED");
    return g_fail;
}
