/* Deterministic geometry/UV/topology gates for ui-entity work.
 * These are NOT pixel gates. Build: bash ../verify/ui_entities/run_gates.sh */
#include "core/types.h"
#include "game/game.h"          /* GmEntityView + MAGMA_GAME_H before entity_render.h */
#include "game/entity_render.h"
#include "game/item_render.h"
#include "game/sky.h"
#include "assets/mob_atlas.h"
#include "world/lightmap.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail = 0;
#define CHECK(c, m) do { \
    if (!(c)) { printf("FAIL: %s\n", m); g_fail = 1; } \
    else      { printf("ok:   %s\n", m); } \
} while (0)

static int approx(float a, float b, float e) { return fabsf(a - b) <= e; }

/* RenderXPOrb lighting (RenderHelper.java:13-14,30-48; RenderXPOrb.java:59-70).
 * GL_RESCALE_NORMAL off, NORMAL_3B (0,1,0), S(0.3) => |n|=1/0.3. */
static float xp_orb_item_factor(float yaw_deg, float pitch_deg) {
    const float deg = 0.017453292519943295f;
    float yr = (180.0f - yaw_deg) * deg;
    float pr = -pitch_deg * deg;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    int bx = (int)(0.0f * 127.0f);
    int by = (int)(1.0f * 127.0f);
    int bz = (int)(0.0f * 127.0f);
    float nx = (2.0f * (float)bx + 1.0f) * (1.0f / 255.0f);
    float ny = (2.0f * (float)by + 1.0f) * (1.0f / 255.0f);
    float nz = (2.0f * (float)bz + 1.0f) * (1.0f / 255.0f);
    float ty = ny * cp - nz * sp, tz = ny * sp + nz * cp;
    ny = ty; nz = tz;
    float tx = nx * cy + nz * sy;
    tz = -nx * sy + nz * cy;
    nx = tx; nz = tz;
    const float inv_s = 1.0f / 0.3f;
    nx *= inv_s; ny *= inv_s; nz *= inv_s;
    const double lx = 0.20000000298023224, ly = 1.0, lz = -0.699999988079071;
    const double inv = 1.0 / sqrt(lx * lx + ly * ly + lz * lz);
    float l0x = (float)(lx * inv), l0y = (float)(ly * inv), l0z = (float)(lz * inv);
    float d0 = nx * l0x + ny * l0y + nz * l0z;
    float d1 = nx * -l0x + ny * l0y + nz * -l0z;
    if (d0 < 0.0f) d0 = 0.0f;
    if (d1 < 0.0f) d1 = 0.0f;
    return 0.4f + 0.6f * d0 + 0.6f * d1;
}

static int xp_orb_lit_u8(int c, float factor) {
    float v = ((float)c * (1.0f / 255.0f)) * factor;
    if (v < 0.0f) v = 0.0f;
    if (v > 1.0f) v = 1.0f;
    return (int)(v * 255.0f + 0.5f);
}

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

    /* LayerSlimeGel outer shell + living alphaFunc(GL_GREATER, 0.1). */
    {
        GmEntityView s;
        memset(&s, 0, sizeof s);
        s.type = 35; s.item_meta = 2; s.health = 4;
        int n = gm_slime_gel_emit(&s, 1, out, 8192);
        CHECK(n == 36, "LayerSlimeGel emits one 8x8x8 box");
        /* Java color(1,1,1,1): translucency is slime.png texel alpha only. */
        CHECK(out[0].tint.a == 255, "gel shell vertex alpha is opaque (texel alpha)");
        /* Living threshold: discard a/255 <= 0.1 (a <= 25). slime.png has
         * a∈{0,8,199,201,255}; a=8 must discard, a=199 must pass. */
        CrTexture ea = gm_entity_atlas();
        CrShadeCtx gel = {0};
        gel.atlas = &ea;
        gel.alpha_test = 1;
        gel.alpha_ref = 0.1f;
        gel.layer = CR_LAYER_TRANSLUCENT;
        gel.blend = 4;
        const CrMobSprite *sl = &CR_MOB_SPRITES[CR_MOB_SLIME];
        float aw = (float)CR_MOB_ATLAS_W, ah = (float)CR_MOB_ATLAS_H;
        /* Scan atlas rect for a low-alpha texel and a gel-alpha texel. */
        int found_lo = 0, found_hi = 0;
        CrRgba clo = {0}, chi = {0};
        for (int y = sl->y0; y < sl->y1 && !(found_lo && found_hi); ++y) {
            for (int x = sl->x0; x < sl->x1; ++x) {
                CrFragment f = {0};
                f.uv.x = ((float)x + 0.5f) / aw;
                f.uv.y = ((float)y + 0.5f) / ah;
                f.tint = (CrRgba){255,255,255,255};
                f.light = 1.0f; f.ao = 1.0f; f.blk = 15.0f;
                CrRgba raw = cr_atlas_sample(&ea, f.uv.x, f.uv.y);
                if (!found_lo && raw.a > 0 && raw.a <= 25) {
                    clo = cr_shade(&gel, &f); found_lo = 1;
                }
                if (!found_hi && raw.a >= 199) {
                    chi = cr_shade(&gel, &f); found_hi = 1;
                }
            }
        }
        CHECK(found_hi, "slime atlas has high-alpha gel texels");
        CHECK(chi.a != 0, "living alpha_ref=0.1 keeps gel a>=199");
        if (found_lo)
            CHECK(clo.a == 0, "living alpha_ref=0.1 discards a<=25");
        else
            CHECK(1, "no a in (0,25] in slime.png (hist may be only 0/8/199+)");
        /* a=8 if present must discard under 0.1; under cutout 0.5 both discard. */
        {
            CrFragment f = {0};
            f.tint = (CrRgba){255,255,255,255};
            f.light = 1.0f; f.ao = 1.0f; f.blk = 15.0f;
            /* Force-sample: find a==8 or synthesize via known discard path. */
            int saw8 = 0;
            for (int y = sl->y0; y < sl->y1 && !saw8; ++y)
                for (int x = sl->x0; x < sl->x1; ++x) {
                    f.uv.x = ((float)x + 0.5f) / aw;
                    f.uv.y = ((float)y + 0.5f) / ah;
                    CrRgba raw = cr_atlas_sample(&ea, f.uv.x, f.uv.y);
                    if (raw.a == 8) {
                        CrRgba c = cr_shade(&gel, &f);
                        CHECK(c.a == 0, "a=8 discards under living 0.1");
                        saw8 = 1;
                        break;
                    }
                }
            if (!saw8) CHECK(1, "slime.png has no a==8 in this atlas packing");
        }
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

    /* Fire overlay extents: small width 0.3125 vs large width 1.0.
     * flags bit 0 = isBurning (RenderManager gate); clear => no layers. */
    {
        GmEntityView sm, lg;
        memset(&sm, 0, sizeof sm); memset(&lg, 0, sizeof lg);
        sm.type = 30; sm.item_id = 385; sm.item_meta = 1;
        lg.type = 30; lg.item_id = 385; lg.item_meta = 2;
        CHECK(gm_small_fireball_fire_emit(&sm, 1, 0.0f, out, 256) == 0,
              "non-burning fireball emits no fire layers");
        sm.flags = 1; lg.flags = 1;
        int ns = gm_small_fireball_fire_emit(&sm, 1, 0.0f, out, 256);
        bounds(out, ns, mn, mx);
        float ws = mx[0] - mn[0];
        int nl = gm_small_fireball_fire_emit(&lg, 1, 0.0f, out, 256);
        bounds(out, nl, mn, mx);
        float wl = mx[0] - mn[0];
        CHECK(ns == 12 && nl == 12, "both burning fireballs emit two fire layers");
        CHECK(approx(ws, 0.4375f, 1e-4f), "small fire scale = 0.3125*1.4");
        CHECK(approx(wl, 1.4f, 1e-4f), "large fire scale = 1.0*1.4");
        CHECK(approx(wl / ws, 1.0f / 0.3125f, 0.01f), "large/small fire width ratio");
    }

    /* RenderFireball item lighting (RenderHelper.java:30-48) + lightmap 15/15
     * (EntityFireball.java:272-274). GL clamps colour*factor per channel;
     * er_shade_item clamps the factor. White vertices (glColor 1,1,1) so the
     * product clamp of the primary is clamp(shade). Capture pitch 25 still
     * has shade>1; the 15/15 texel is the remaining modulate. */
    {
        float s_up = gm_fireball_item_shade(0.0f, 1.0f, 0.0f);
        CHECK(s_up > 1.0f, "unclamped +Y item lighting exceeds 1 (RenderHelper.java:30-48)");
        int r188 = (int)(188.0f * s_up + 0.5f);
        if (r188 > 255) r188 = 255;
        CHECK(r188 == 255, "GL product clamp: 188 * unclamped factor saturates");
        int r_clamped = (int)(188.0f * (s_up > 1.0f ? 1.0f : s_up) + 0.5f);
        CHECK(r_clamped == 188, "factor-first clamp would leave 188 unsaturated");

        CrRgba lm8 = cr_lightmap_rgba8(cr_lightmap_rgb(0, 15, 15, 1.0f, 0.0f, 0.0f));
        CHECK(lm8.r == 252 && lm8.g == 252 && lm8.b == 252,
              "overworld lightmap[15,15] truncates 0.99*255 (DynamicTexture)");

        GmEntityView sm;
        memset(&sm, 0, sizeof sm);
        sm.type = 30; sm.item_id = 385; sm.x = 8.5f; sm.y = 6.0f; sm.z = 12.5f;
        int n0 = gm_items_emit_billboard(&sm, 1, 0.0f, 0.0f, out, 256);
        CHECK(n0 == 6, "RenderFireball pitch 0 emits 6 verts");
        CHECK(out[0].light == 1.0f && out[0].blk == 15.0f,
              "item-atlas pass keeps light=1; 15/15 is in tint");
        CHECK(out[0].tint.r == lm8.r && out[0].tint.g == lm8.g &&
              out[0].tint.b == lm8.b,
              "pitch 0 white*shade clamps then * lightmap[15,15]");

        int n25 = gm_items_emit_billboard(&sm, 1, 0.0f, 25.0f, out, 256);
        CHECK(n25 == 6 && out[0].tint.r == lm8.r,
              "capture pitch 25 still clamps white*shade to 1");

        int n90 = gm_items_emit_billboard(&sm, 1, 0.0f, 90.0f, out, 256);
        CHECK(n90 == 6 && out[0].tint.r < lm8.r && out[0].tint.r > 0,
              "pitch 90 item lighting darkens the white primary");
    }

    /* Death rays: 9 verts/ray, blend inputs (white center, magenta rim).
     * Also pin Random(432) 48-bit LCG: f2 in [5,25] when f1=0 (Java nextFloat).
     * A 52-bit mask bug made nextFloat >> 1 and stretched rays ~10x. */
    {
        GmEntityView d;
        memset(&d, 0, sizeof d);
        d.type = 9; d.death_ticks = 100; d.health = 0;
        d.x = 0.0f; d.y = 0.0f; d.z = 0.0f;
        int n = gm_dragon_death_rays_emit(&d, 1, out, 8192);
        float f = 101.0f / 200.0f;
        float bound = (f + f * f) / 2.0f * 60.0f;
        int rays = 0;
        while ((float)rays < bound) ++rays;
        CHECK(n == rays * 9, "deathTicks=100+pt1: 3 tris/ray (5-vert fan)");
        CHECK(out[0].tint.a > 0 && out[1].tint.a == 0,
              "ray smooth shading inputs: center alpha, rim alpha 0");
        /* First ray: center at origin (after base stack), rim verts at distance
         * ~f2 in [5,25] for f1=0. Measure max |pos| of first fan's 9 verts. */
        float maxr = 0.0f;
        for (int i = 0; i < 9 && i < n; ++i) {
            float px = out[i].pos.x, py = out[i].pos.y, pz = out[i].pos.z;
            float r = sqrtf(px * px + py * py + pz * pz);
            if (r > maxr) maxr = r;
        }
        /* Base stack translate(0,-1.501)+Layer(0,-1,-2) shifts origin; allow
         * generous headroom but reject the old 100+ block stretch. */
        CHECK(maxr > 4.0f && maxr < 40.0f,
              "ray length from Random.nextFloat in [0,1) (not 52-bit overflow)");
        d.death_ticks = 0;
        CHECK(gm_dragon_death_rays_emit(&d, 1, out, 8192) == 0, "no rays when alive");
    }

    /* Dragon dissolve markers (per-texel path uses light/blk; not box drop).
     * RenderDragon.java:59-63: f = deathTicks/200 into alphaFunc GL_GREATER.
     * ao is RenderHelper item lighting (RenderHelper.java:30-48), not
     * block-face 1/0.8/0.6/0.5. Axis-aligned: 1.0 up / ~0.4 down. */
    {
        GmEntityView d;
        memset(&d, 0, sizeof d);
        d.type = 9; d.y = 80; d.health = 0; d.death_ticks = 100;
        int mid = gm_entities_emit(&d, 1, out, 8192);
        CHECK(mid > 200, "mid-death dragon emits full body geometry");
        int ok = 1, buckets = 0;
        float amin = 9.0f, amax = -9.0f;
        for (int i = 0; i < mid; ++i) {
            float a = out[i].ao;
            if (!(out[i].light < 0.0f && fabsf(out[i].blk - 0.5f) < 1e-4f
                  && a > 0.3f && a <= 1.0001f)) ok = 0;
            if (a < amin) amin = a;
            if (a > amax) amax = a;
            if (fabsf(a - 0.5f) < 1e-3f || fabsf(a - 0.6f) < 1e-3f ||
                fabsf(a - 0.8f) < 1e-3f) buckets++;
        }
        CHECK(ok, "dissolve verts carry light<0, blk=deathTicks/200, item ao");
        CHECK(amin < 0.45f && amax > 0.99f,
              "item lighting spans ~0.4 down to 1.0 up (RenderHelper.java:30-48)");
        CHECK(buckets < mid / 4, "dragon ao is not block-face 0.5/0.6/0.8");
        d.death_ticks = 200;
        CHECK(gm_entities_emit(&d, 1, out, 8192) == 0, "f=1 emits nothing");
        float uo = 0, vo = 0;
        gm_entity_dissolve_mask(&uo, &vo);
        CHECK(fabsf(uo - 0.25f) < 1e-4f && fabsf(vo) < 1e-4f,
              "dissolve mask offset maps dragon -> dragon_exploding");
    }

    /* Capture-pin pose (ui_entities dragon_death_*). Distinct ent_id so the
     * static trail ring re-inits (gm_dragon_pose_tick). */
    {
        GmEntityView d0, d1;
        memset(&d0, 0, sizeof d0); memset(&d1, 0, sizeof d1);
        d0.type = d1.type = 9;
        d0.y = d1.y = 80.0f;
        d0.health = d1.health = 200.0f;
        d0.death_ticks = d1.death_ticks = 50;
        /* ModelDragon.java:185 wing.rotateAngleX = 0.125 - cos(animTime*2PI)*0.2
         * EntityDragon.java:221 isAIDisabled => animTime=0.5F
         * (0 -> -0.075 rad, 0.5 -> +0.325 rad). */
        d0.ent_id = 110; d0.anim_time = 0.0f;
        d1.ent_id = 111; d1.anim_time = 0.5f;
        int n0 = gm_entities_emit(&d0, 1, out, 8192);
        bounds(out, n0, mn, mx);
        float h0 = mx[1] - mn[1];
        int n1 = gm_entities_emit(&d1, 1, out, 8192);
        bounds(out, n1, mn, mx);
        float h1 = mx[1] - mn[1];
        CHECK(n0 == n1 && n0 > 200, "animTime 0 vs 0.5 keeps box count");
        CHECK(fabsf(h1 - h0) > 0.3f,
              "animTime 0.5 vs 0 changes wing Y span (ModelDragon.java:185)");

        /* PhaseHover.getIsStationary:28-30; getHeadPartYOffset:1064-1066
         * returns idx (neck 0..4, head 6) instead of 0. */
        memset(&d0, 0, sizeof d0); memset(&d1, 0, sizeof d1);
        d0.type = d1.type = 9;
        d0.y = d1.y = 80.0f;
        d0.health = d1.health = 200.0f;
        d0.anim_time = d1.anim_time = 0.5f;
        d0.ent_id = 112; d0.stationary = 0;
        d1.ent_id = 113; d1.stationary = 1; d1.phase_id = 10;
        n0 = gm_entities_emit(&d0, 1, out, 8192);
        bounds(out, n0, mn, mx);
        float z0 = mx[2] - mn[2], y0s = mx[1] - mn[1];
        n1 = gm_entities_emit(&d1, 1, out, 8192);
        bounds(out, n1, mn, mx);
        float z1 = mx[2] - mn[2], y1s = mx[1] - mn[1];
        CHECK(n0 == n1 && n0 > 200, "stationary 0 vs 1 keeps box count");
        CHECK(fabsf(z1 - z0) > 0.2f || fabsf(y1s - y0s) > 0.2f,
              "HOVER stationary tucks neck/head (EntityDragon.java:1064-1066)");

        /* NoAI skips ring fill (EntityDragon.java:219-240). applyRotations
         * (RenderDragon.java:33) reads getMovementOffsets(7)[0]=0, not
         * rotationYaw=180. Cold-start ring copies view yaw, so yaw=180
         * flips the body about Y. */
        memset(&d0, 0, sizeof d0); memset(&d1, 0, sizeof d1);
        d0.type = d1.type = 9;
        d0.y = d1.y = 80.0f;
        d0.health = d1.health = 200.0f;
        d0.anim_time = d1.anim_time = 0.5f;
        d0.stationary = d1.stationary = 1;
        d0.ent_id = 114; d0.yaw = 0.0f;
        d1.ent_id = 115; d1.yaw = 180.0f;
        n0 = gm_entities_emit(&d0, 1, out, 8192);
        bounds(out, n0, mn, mx);
        float zc0 = 0.5f * (mn[2] + mx[2]);
        n1 = gm_entities_emit(&d1, 1, out, 8192);
        bounds(out, n1, mn, mx);
        float zc1 = 0.5f * (mn[2] + mx[2]);
        CHECK(n0 == n1 && n0 > 200, "ring yaw 0 vs 180 keeps box count");
        CHECK(fabsf(zc0 - zc1) > 2.0f,
              "ring yaw 180 vs 0 flips body along Z (RenderDragon.java:33-35)");
    }

    /* EntityRenderer.java:2044-2047 dense ramp. createFog is End-only
     * (DragonFightManager.java:54). Overworld capture pins must not densify. */
    {
        CHECK(gm_fog_dense_ramp(0, 1) == 0,
              "overworld ignores createFog (no DragonFightManager)");
        CHECK(gm_fog_dense_ramp(1, 1) == 1,
              "End createFog pulls [far*0.05, far*0.5]");
        CHECK(gm_fog_dense_ramp(1, 0) == 0,
              "End without BossInfo uses normal fog");
        CHECK(gm_fog_dense_ramp(-1, 0) == 1,
              "Nether doesXZShowFog always dense");
    }

    /* Portal: particles.png. EXPLOSION_LARGE: explosion.png (not particles). */
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
        CHECK(bad == 0, "portal UVs stay inside particles.png sheet");
        CHECK(CR_MOB_PARTICLES >= 0 && CR_MOB_DRAGON_EXPLODING >= 0 &&
              CR_MOB_EXPLOSION >= 0,
              "particles + dragon_exploding + explosion packed in mob atlas");
        GmEntityView d;
        memset(&d, 0, sizeof d);
        d.type = 9; d.death_ticks = 20; d.health = 0; d.ent_id = 3;
        int en = gm_particles_emit(&d, 1, 0.0f, 0.0f, out, 8192);
        CHECK(en > 0 && en % 6 == 0, "dragon death emits EXPLOSION_LARGE quads");
        const CrMobSprite *ex = &CR_MOB_SPRITES[CR_MOB_EXPLOSION];
        bad = 0;
        for (int i = 0; i < en; ++i) {
            float px = out[i].uv.x * aw, py = out[i].uv.y * ah;
            if (px < ex->x0 - 1 || px > ex->x1 + 1 ||
                py < ex->y0 - 1 || py > ex->y1 + 1) bad++;
        }
        CHECK(bad == 0, "EXPLOSION_LARGE UVs stay inside explosion.png");
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
        frag.ao = 1.0f; /* ModelBox up-face shade */
        frag.tint = (CrRgba){255,255,255,255};
        frag.blk = 0.0f; /* thr=0: only alpha==0 discards */
        CrRgba c0 = cr_shade(&sh, &frag);
        frag.blk = 1.0f; /* thr=1: all a<=1 discard */
        CrRgba c1 = cr_shade(&sh, &frag);
        CHECK(c0.a != 0, "dissolve thr=0 keeps opaque exploding texels");
        CHECK(c1.a == 0, "dissolve thr=1 discards all fragments");
        /* Skin pass keeps face shade (RenderDragon.java:70-71 bind skin after
         * exploding mask). ao_mul must not be forced to 1. */
        frag.blk = 0.0f;
        frag.ao = 1.0f;
        CrRgba chi = cr_shade(&sh, &frag);
        frag.ao = 0.5f;
        CrRgba clo = cr_shade(&sh, &frag);
        CHECK(chi.a != 0 && clo.a != 0 && clo.r * 2 >= chi.r - 2 &&
              clo.r * 2 <= chi.r + 2,
              "dissolve down-face ao=0.5 is half of up-face ao=1");
    }

    /* Two-pass exploding-vs-skin (RenderDragon.java:57-71). Synthetic 2x1
     * atlas: texel 0 = skin, texel 1 = exploding. mask_u_off maps 0 -> 1. */
    {
        CrRgba px[2];
        px[0].r = 10; px[0].g = 20; px[0].b = 30; px[0].a = 0;     /* skin a=0 */
        px[1].r = 240; px[1].g = 240; px[1].b = 240; px[1].a = 255; /* exploding */
        CrTexture syn;
        memset(&syn, 0, sizeof syn);
        syn.w = 2; syn.h = 1; syn.texels = px;
        CrShadeCtx sh = {0};
        sh.atlas = &syn;
        sh.alpha_mask = 1;
        sh.alpha_test = 1;
        sh.layer = CR_LAYER_CUTOUT;
        sh.mask_u_off = 0.75f; /* u=0 -> exploding at u=0.75, ix=1 */
        sh.mask_v_off = 0.0f;
        CrFragment frag = {0};
        frag.uv.x = 0.0f; frag.uv.y = 0.0f;
        frag.light = -1.0f;
        frag.ao = 1.0f;
        frag.tint = (CrRgba){255,255,255,255};
        frag.blk = 0.25f; /* deathTicks=50 */
        CrRgba hole = cr_shade(&sh, &frag);
        CHECK(hole.a != 0 && hole.r > 200 && hole.g > 200 && hole.b > 200,
              "skin a=0 keeps pass-1 exploding RGB (RenderDragon.java:66-71)");

        px[0].a = 25; /* GL_GREATER 0.1 discards a<=25 */
        CrRgba still = cr_shade(&sh, &frag);
        CHECK(still.r > 200,
              "skin a=25 fails alphaFunc 0.1; exploding RGB stays");

        px[0].a = 26; /* 26/255 > 0.1 */
        CrRgba sk = cr_shade(&sh, &frag);
        CHECK(sk.r < 40 && sk.g < 40 && sk.b < 50,
              "skin a=26 passes 0.1 and overwrites exploding RGB");

        px[0].a = 50; /* would FAIL default CUTOUT 0.5 (thr=127) */
        CrRgba mid = cr_shade(&sh, &frag);
        CHECK(mid.r < 40,
              "pass-2 uses 0.1 not CUTOUT 0.5 (RenderDragon.java:66)");

        px[1].a = 0; /* exploding fails pass 1 */
        px[0].a = 255;
        CrRgba gone = cr_shade(&sh, &frag);
        CHECK(gone.a == 0,
              "exploding a=0 discards (pass 1 GL_GREATER f fails)");
        px[1].a = 255;
        px[1].r = 240; px[1].g = 240; px[1].b = 240;
    }

    /* RenderXPOrb: texture cell, hue pulse, billboard transform. */
    {
        static float sin_tab[65536];
        static int sin_ok;
        if (!sin_ok) {
            const double two_pi = 6.283185307179586476925286766559;
            for (int i = 0; i < 65536; ++i)
                sin_tab[i] = (float)sin((double)i * two_pi / 65536.0);
            sin_ok = 1;
        }
        /* MathHelper.sin (MathHelper.java:29-31). */
        #define XP_MCSIN(v) (sin_tab[((int)((v) * 10430.378f)) & 65535])

        /* getTextureByXP thresholds (EntityXPOrb.java:290-293). */
        {
            static const int edges[][2] = {
                {1, 0}, {2, 0}, {3, 1}, {6, 1}, {7, 2}, {16, 2}, {17, 3},
                {36, 3}, {37, 4}, {72, 4}, {73, 5}, {148, 5}, {149, 6},
                {306, 6}, {307, 7}, {616, 7}, {617, 8}, {1236, 8}, {1237, 9},
                {2476, 9}, {2477, 10},
            };
            int bad = 0;
            const CrMobSprite *spr = &CR_MOB_SPRITES[CR_MOB_EXPERIENCE_ORB];
            float aw = (float)CR_MOB_ATLAS_W;
            for (unsigned i = 0; i < sizeof edges / sizeof edges[0]; ++i) {
                GmEntityView e;
                memset(&e, 0, sizeof e);
                e.type = 21; e.y = 64; e.item_id = edges[i][0];
                CrVertex v[6];
                if (gm_xp_orbs_emit(&e, 1, 0.0f, 0.0f, 1.0f, v, 6) != 6) {
                    bad = 1; break;
                }
                int tier = edges[i][1];
                float want_u0 = ((float)spr->x0 + (float)(tier % 4 * 16)) / aw;
                if (fabsf(v[0].uv.x - want_u0) > 1e-5f) bad = 1;
            }
            CHECK(bad == 0, "getTextureByXP 4x4 cell U matches EntityXPOrb.java:290-293");
            /* value 17 is the capture (tier 3, column 3 of row 0). */
            GmEntityView cap;
            memset(&cap, 0, sizeof cap);
            cap.type = 21; cap.y = 64; cap.item_id = 17;
            CrVertex v[6];
            gm_xp_orbs_emit(&cap, 1, 0.0f, 0.0f, 1.0f, v, 6);
            float u0 = ((float)CR_MOB_SPRITES[CR_MOB_EXPERIENCE_ORB].x0 + 48.0f)
                       / (float)CR_MOB_ATLAS_W;
            float u1 = ((float)CR_MOB_SPRITES[CR_MOB_EXPERIENCE_ORB].x0 + 64.0f)
                       / (float)CR_MOB_ATLAS_W;
            CHECK(approx(v[0].uv.x, u0, 1e-5f) && approx(v[1].uv.x, u1, 1e-5f),
                  "xpValue 17 selects sheet cell i=3 (RenderXPOrb.java:37-42)");
        }

        /* Hue pulse: f9=(xpColor+partialTicks)/2 (RenderXPOrb.java:52-55).
         * Emit bakes GL item lighting into tint; compare against pulse * factor. */
        {
            GmEntityView e;
            memset(&e, 0, sizeof e);
            e.type = 21; e.y = 64; e.item_id = 17; e.item_meta = 0;
            CrVertex v0[6], v1[6];
            gm_xp_orbs_emit(&e, 1, 0.0f, 0.0f, 0.0f, v0, 6);
            gm_xp_orbs_emit(&e, 1, 0.0f, 0.0f, 1.0f, v1, 6);
            float f9_0 = (0.0f + 0.0f) / 2.0f;
            float f9_1 = (0.0f + 1.0f) / 2.0f;
            int r0 = (int)((XP_MCSIN(f9_0 + 0.0f) + 1.0f) * 0.5f * 255.0f);
            int b0 = (int)((XP_MCSIN(f9_0 + 4.1887903f) + 1.0f) * 0.1f * 255.0f);
            int r1 = (int)((XP_MCSIN(f9_1 + 0.0f) + 1.0f) * 0.5f * 255.0f);
            int b1 = (int)((XP_MCSIN(f9_1 + 4.1887903f) + 1.0f) * 0.1f * 255.0f);
            float fac00 = xp_orb_item_factor(0.0f, 0.0f);
            int lr0 = xp_orb_lit_u8(r0, fac00), lg0 = xp_orb_lit_u8(255, fac00);
            int lb0 = xp_orb_lit_u8(b0, fac00);
            int lr1 = xp_orb_lit_u8(r1, fac00), lg1 = xp_orb_lit_u8(255, fac00);
            int lb1 = xp_orb_lit_u8(b1, fac00);
            CHECK(r0 != r1,
                  "partialTicks is part of the hue argument, not dropped");
            CHECK(v0[0].tint.r == (u8)lr0 && v0[0].tint.g == (u8)lg0 &&
                  v0[0].tint.b == (u8)lb0 && v0[0].tint.a == 128,
                  "xpColor=0 partialTicks=0 pulse then GL item lighting");
            CHECK(v1[0].tint.r == (u8)lr1 && v1[0].tint.g == (u8)lg1 &&
                  v1[0].tint.b == (u8)lb1 && v1[0].tint.a == 128,
                  "xpColor=0 partialTicks=1 pulse then GL item lighting");
        }

        /* Billboard: T(pos) T(0,0.1,0) Ry(180-yaw) Rx(-pitch) S(0.3)
         * verts (-0.5,-0.25),(0.5,-0.25),(0.5,0.75),(-0.5,0.75)
         * RenderXPOrb.java:56-67. Capture pose yaw=0 pitch=25. */
        {
            const float deg = 0.017453292519943295f;
            const float yaw = 0.0f, pitch = 25.0f;
            const float ex = 8.5f, ey = 6.0f, ez = 10.5f;
            float yr = (180.0f - yaw) * deg;
            float pr = -pitch * deg;
            float cy = cosf(yr), sy = sinf(yr);
            float cp = cosf(pr), sp = sinf(pr);
            static const float CORN[4][2] = {
                { -0.5f, -0.25f }, {  0.5f, -0.25f },
                {  0.5f,  0.75f }, { -0.5f,  0.75f },
            };
            GmEntityView e;
            memset(&e, 0, sizeof e);
            e.type = 21; e.x = ex; e.y = ey; e.z = ez; e.item_id = 17;
            CrVertex v[6];
            CHECK(gm_xp_orbs_emit(&e, 1, yaw, pitch, 1.0f, v, 6) == 6,
                  "capture-pose orb emits 6 verts");
            int bad = 0;
            /* tri 0,1,2,0,2,3 so verts 0,1,2 of the output are corners 0,1,2 */
            for (int c = 0; c < 3; ++c) {
                float px = CORN[c][0], py = CORN[c][1], pz = 0.0f;
                float ty = py * cp - pz * sp, tz = py * sp + pz * cp;
                py = ty; pz = tz;
                float tx = px * cy + pz * sy;
                tz = -px * sy + pz * cy;
                px = tx; pz = tz;
                float wx = ex + px * 0.3f;
                float wy = ey + 0.1f + py * 0.3f;
                float wz = ez + pz * 0.3f;
                if (!approx(v[c].pos.x, wx, 1e-5f) ||
                    !approx(v[c].pos.y, wy, 1e-5f) ||
                    !approx(v[c].pos.z, wz, 1e-5f))
                    bad = 1;
            }
            CHECK(bad == 0, "billboard T+Ry(180-yaw)+Rx(-pitch)+S(0.3) (RenderXPOrb.java:56-60)");
            /* Capture-pose lit colour. Pulse at color=0 partialTicks=1 is
             * (188,255,b). GL_RESCALE_NORMAL off + S(0.3) makes |n|=1/0.3 so
             * the unclamped factor exceeds 255/188; COLOR_MATERIAL saturates
             * R and G to 255. Not a fitted vertex colour. */
            {
                float f9 = (0.0f + 1.0f) / 2.0f;
                int pr = (int)((XP_MCSIN(f9 + 0.0f) + 1.0f) * 0.5f * 255.0f);
                int pb = (int)((XP_MCSIN(f9 + 4.1887903f) + 1.0f) * 0.1f * 255.0f);
                float fac = xp_orb_item_factor(yaw, pitch);
                int lr = xp_orb_lit_u8(pr, fac);
                int lg = xp_orb_lit_u8(255, fac);
                int lb = xp_orb_lit_u8(pb, fac);
                CHECK(pr == 188 && pr != 255,
                      "capture pulse R is 188 (RenderXPOrb.java:52-55), not 255");
                CHECK(fac > 255.0f / 188.0f,
                      "capture |n|=1/0.3 factor exceeds 255/188 (rescale off)");
                CHECK(lr == 255 && lg == 255,
                      "unclamped factor * pulse saturates R and G to 255");
                CHECK(v[0].tint.r == 255 && v[0].tint.g == 255 &&
                      v[0].tint.b == (u8)lb && v[0].tint.a == 128,
                      "capture-pose lit colour (RenderHelper.java:30-48)");
                CHECK(v[0].tint.r == v[1].tint.r && v[0].tint.g == v[1].tint.g &&
                      v[0].tint.b == v[2].tint.b,
                      "all orb verts share the same lit colour");
            }
        }

        /* EntityXPOrb.getBrightnessForRender (EntityXPOrb.java:67-81):
         * j = (packed block & 255) + (int)(0.5F * 15.0F * 16.0F), cap 240.
         * Magma stores 0..15; packed block is level*16. Sky is unchanged. */
        {
            GmEntityView e;
            memset(&e, 0, sizeof e);
            e.type = 21; e.y = 64; e.item_id = 17;
            e.lm_lit = 1; e.lm_light = 15.0f; e.lm_blk = 0.0f;
            CrVertex v[6];
            gm_xp_orbs_emit(&e, 1, 0.0f, 0.0f, 1.0f, v, 6);
            CHECK(approx(v[0].light, 15.0f, 1e-5f) &&
                  approx(v[0].blk, 7.5f, 1e-5f),
                  "getBrightnessForRender +120 block coord (EntityXPOrb.java:67-81)");
            e.lm_blk = 15.0f;
            gm_xp_orbs_emit(&e, 1, 0.0f, 0.0f, 1.0f, v, 6);
            CHECK(approx(v[0].blk, 15.0f, 1e-5f),
                  "getBrightnessForRender block coord caps at 240");
        }
        #undef XP_MCSIN
    }

    printf("\n%s\n", g_fail ? "*** GEOM GATES FAILED ***" : "ALL GEOM GATES PASSED");
    return g_fail;
}
