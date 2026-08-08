/* game/test_item_render.c - standalone verification for game/item_render.c.
 *
 * (A) BLOCK DROP: a dirt drop emits 36 verts in the terrain-atlas pass, 0 in
 *     the item-atlas pass; every UV lies inside the dirt sprite rect; geometry
 *     is a 0.25-scale cube (extents) and every face normal points away from
 *     the cube center (CCW-seen-from-outside winding).
 * (B) ITEM DROP: a flint drop emits 36 verts in the item-atlas pass, 0 in the
 *     terrain pass; UVs lie inside the flint atlas rect; the thin box has
 *     thickness matching vanilla 1/16 extrusion after GROUND scale 0.5.
 * (C) BOB/SPIN: age changes the emitted y (bob) and rotates x/z (spin).
 * (D) CAP: max below a model's vert count emits nothing for it and never
 *     overruns `out` (canary vertex intact); non-item entity types skipped.
 * (E) FALLBACK: an unknown item id still resolves to a valid sprite index.
 * (F) FIREBALLS: exact RenderFireball / RenderDragonFireball scale, offset,
 *     full-bright state, atlas sprites, and renderEntityOnFire UV order.
 * (G) GUI ISO: dirt/cobble/crafting-table and metadata-colored carpet icons
 *     draw into a 16x16 slot; a stick id does not claim the block-icon path.
 *
 * Build/run: bash game/test_item_render.sh
 */
#include "core/types.h"
#include "game/item_render.h"
#include "game/block_registry.h"
#include "assets/atlas_gen.h"
#include "assets/blockmodels.h"
#include "assets/item_atlas.h"
#include "assets/mob_atlas.h"
#include "renderkernels/rk.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); g_fail = 1; } \
} while (0)

static GmEntityView mk_item(int id, int meta, int age) {
    GmEntityView v;
    memset(&v, 0, sizeof v);
    v.type = GM_VIEW_ITEM;
    v.x = 10.0f; v.y = 64.0f; v.z = -3.0f;
    v.item_id = id; v.item_meta = meta; v.age = age;
    return v;
}

static void tri_normal(const CrVertex *t, float n[3]) {
    float e1[3] = { t[1].pos.x - t[0].pos.x, t[1].pos.y - t[0].pos.y, t[1].pos.z - t[0].pos.z };
    float e2[3] = { t[2].pos.x - t[0].pos.x, t[2].pos.y - t[0].pos.y, t[2].pos.z - t[0].pos.z };
    n[0] = e1[1]*e2[2] - e1[2]*e2[1];
    n[1] = e1[2]*e2[0] - e1[0]*e2[2];
    n[2] = e1[0]*e2[1] - e1[1]*e2[0];
}

static int verts_use_sprite(const CrVertex *v, int n, int sprite) {
    float u0, v0, u1, v1;
    const float eps = 1e-6f;
    bm_sprite_uv(sprite, &u0, &v0, &u1, &v1);
    for (int i = 0; i < n; ++i)
        if (v[i].uv.x < u0 - eps || v[i].uv.x > u1 + eps
                || v[i].uv.y < v0 - eps || v[i].uv.y > v1 + eps)
            return 0;
    return 1;
}

int main(void) {
    CrVertex out[256];
    const float eps = 1e-4f;

    /* ---- (A) block drop: dirt (id 3) ---- */
    {
        GmEntityView e = mk_item(3, 0, 0);
        e.has_hover_start = 1;
        e.hover_start = 0.0f;
        CHECK(gm_item_drop_uses_block_atlas(3, 0), "dirt classifies as block");
        int nb = gm_items_emit(&e, 1, out, 256);
        CHECK(nb == 36, "dirt cube emits 36 verts");
        int nf = gm_items_emit_flat(&e, 1, out + nb, 256 - nb);
        CHECK(nf == 0, "dirt not emitted in the item-atlas pass");

        /* UV rect of the dirt sprite (all 6 faces share it) */
        int key = gm_state_to_model_key(gm_pack_state(3, 0));
        const BmBlock *m = bm_block(key);
        float u0, v0, u1, v1;
        bm_sprite_uv(m->face[0].sprite, &u0, &v0, &u1, &v1);
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, minz = 1e9f, maxz = -1e9f;
        for (int i = 0; i < nb; ++i) {
            CHECK(out[i].uv.x >= u0 - eps && out[i].uv.x <= u1 + eps, "cube u inside dirt sprite");
            CHECK(out[i].uv.y >= v0 - eps && out[i].uv.y <= v1 + eps, "cube v inside dirt sprite");
            if (out[i].pos.x < minx) minx = out[i].pos.x;
            if (out[i].pos.x > maxx) maxx = out[i].pos.x;
            if (out[i].pos.y < miny) miny = out[i].pos.y;
            if (out[i].pos.y > maxy) maxy = out[i].pos.y;
            if (out[i].pos.z < minz) minz = out[i].pos.z;
            if (out[i].pos.z > maxz) maxz = out[i].pos.z;
        }
        /* Y is invariant under spin; XZ AABB grows up to 0.25*sqrt(2) when spun. */
        CHECK(fabsf((maxy - miny) - 0.25f) < eps, "cube 0.25 tall (y)");
        float hx = maxx - minx, hz = maxz - minz;
        CHECK(hx > 0.24f && hx < 0.36f, "cube x extent in [0.25, 0.25√2]");
        CHECK(hz > 0.24f && hz < 0.36f, "cube z extent in [0.25, 0.25√2]");
        /* At partialTicks=1: bob + RenderEntityItem centering + block.json's
         * ground translation, less the cube half-height. */
        float want_min_y = 64.0f + sinf(0.1f) * 0.1f + 0.1f
                         + 0.25f * 0.25f + 3.0f / 16.0f - 0.125f;
        CHECK(fabsf(miny - want_min_y) < eps,
              "cube applies partial tick and block ground translation");

        /* winding: every face normal points away from the cube center */
        float cx = (minx + maxx) * 0.5f, cy = (miny + maxy) * 0.5f, cz = (minz + maxz) * 0.5f;
        for (int t = 0; t < nb / 3; ++t) {
            float n[3]; tri_normal(out + t * 3, n);
            float tx = (out[t*3].pos.x + out[t*3+1].pos.x + out[t*3+2].pos.x) / 3.0f - cx;
            float ty = (out[t*3].pos.y + out[t*3+1].pos.y + out[t*3+2].pos.y) / 3.0f - cy;
            float tz = (out[t*3].pos.z + out[t*3+1].pos.z + out[t*3+2].pos.z) / 3.0f - cz;
            CHECK(n[0]*tx + n[1]*ty + n[2]*tz > 0.0f, "cube face normal points outward");
        }
    }

    /* ---- (A2) drop lightmap: a dropped stack is lit by the world light at its
     * position (RenderManager -> setLightmapTextureCoords), like every other
     * entity. Emitting it at full daylight made a dirt drop read as an opaque
     * bright cube against shaded terrain. Both drop passes must fold lm_mul. */
    {
        GmEntityView lit = mk_item(3, 0, 0), dark = mk_item(3, 0, 0);
        dark.lm_lit = 2;
        dark.lm_mul_r = 0.25f; dark.lm_mul_g = 0.25f; dark.lm_mul_b = 0.30f;
        CrVertex a[64], b[64];
        int na = gm_items_emit(&lit, 1, a, 64);
        int nb2 = gm_items_emit(&dark, 1, b, 64);
        CHECK(na == 36 && nb2 == 36, "lit/dark dirt drops emit the same geometry");
        int darker = 1;
        for (int i = 0; i < nb2; ++i)
            if (b[i].tint.r >= a[i].tint.r || b[i].tint.b >= a[i].tint.b)
                darker = 0;
        CHECK(darker, "dark-lit block drop is dimmer than the same drop in daylight");
        /* item-atlas drops (flat sprites) go through the same fold. */
        GmEntityView flit = mk_item(318, 0, 0), fdark = mk_item(318, 0, 0);
        fdark.lm_lit = 2;
        fdark.lm_mul_r = 0.25f; fdark.lm_mul_g = 0.25f; fdark.lm_mul_b = 0.30f;
        int fa = gm_items_emit_flat(&flit, 1, a, 64);
        int fb = gm_items_emit_flat(&fdark, 1, b, 64);
        CHECK(fa == 36 && fb == 36, "lit/dark flint drops emit the same geometry");
        CHECK(b[0].tint.r < a[0].tint.r,
              "dark-lit item drop is dimmer than the same drop in daylight");
    }

    /* ---- (B) item drop: flint (id 318) - extruded thin box ---- */
    {
        GmEntityView e = mk_item(318, 0, 0);
        e.has_hover_start = 1;
        e.hover_start = 0.2f;
        CHECK(!gm_item_drop_uses_block_atlas(318, 0), "flint classifies as item");
        int nb = gm_items_emit(&e, 1, out, 256);
        CHECK(nb == 0, "flint not emitted in the terrain pass");
        int nf = gm_items_emit_flat(&e, 1, out, 256);
        CHECK(nf == 36, "flint extruded box emits 36 verts");

        int si = gm_item_sprite_index(318);
        CHECK(CR_ITEM_SPRITES[si].id == 318, "flint sprite index resolves to flint");
        float u0 = (float)CR_ITEM_SPRITES[si].x0 / CR_ITEM_ATLAS_W;
        float u1 = (float)CR_ITEM_SPRITES[si].x1 / CR_ITEM_ATLAS_W;
        float v0 = (float)CR_ITEM_SPRITES[si].y0 / CR_ITEM_ATLAS_H;
        float v1 = (float)CR_ITEM_SPRITES[si].y1 / CR_ITEM_ATLAS_H;
        for (int i = 0; i < nf; ++i) {
            CHECK(out[i].uv.x >= u0 - eps && out[i].uv.x <= u1 + eps, "box u inside flint rect");
            CHECK(out[i].uv.y >= v0 - eps && out[i].uv.y <= v1 + eps, "box v inside flint rect");
        }
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, minz = 1e9f, maxz = -1e9f;
        for (int i = 0; i < nf; ++i) {
            if (out[i].pos.x < minx) minx = out[i].pos.x;
            if (out[i].pos.x > maxx) maxx = out[i].pos.x;
            if (out[i].pos.y < miny) miny = out[i].pos.y;
            if (out[i].pos.y > maxy) maxy = out[i].pos.y;
            if (out[i].pos.z < minz) minz = out[i].pos.z;
            if (out[i].pos.z > maxz) maxz = out[i].pos.z;
        }
        /* GROUND scale 0.5: face 0.5x0.5, thickness 0.5/16=0.03125. Spin about Y
         * mixes X/Z so the thin axis is not axis-aligned; Y stays 0.5. AABB
         * volume is far below a 0.5 cube (0.125) because of the extrusion. */
        float dx = maxx - minx, dy = maxy - miny, dz = maxz - minz;
        CHECK(fabsf(dy - 0.5f) < 1e-3f, "flat item 0.5 tall (y)");
        float want_min_y = 64.0f + sinf(0.3f) * 0.1f + 0.1f
                         + 0.25f * 0.5f + 2.0f / 16.0f - 0.25f;
        CHECK(fabsf(miny - want_min_y) < eps,
              "flat item applies partial tick and generated ground translation");
        CHECK((dx > 0.45f || dz > 0.45f), "flat item has ~0.5 face extent");
        float vol = dx * dy * dz;
        CHECK(vol > 0.02f && vol < 0.08f, "extruded volume thinner than a 0.5 cube");
    }

    /* ---- (C) bob + spin change with age ---- */
    {
        GmEntityView e0 = mk_item(3, 0, 0), e1 = mk_item(3, 0, 7);
        CrVertex a[64], b[64];
        int na = gm_items_emit(&e0, 1, a, 64);
        int nbv = gm_items_emit(&e1, 1, b, 64);
        CHECK(na == 36 && nbv == 36, "both ages emit full cubes");
        float miny_a = 1e9f, miny_b = 1e9f;
        for (int i = 0; i < 36; ++i) {
            if (a[i].pos.y < miny_a) miny_a = a[i].pos.y;
            if (b[i].pos.y < miny_b) miny_b = b[i].pos.y;
        }
        CHECK(fabsf(miny_a - miny_b) > 1e-3f, "bob moves the cube with age");
        int moved = 0;
        for (int i = 0; i < 36; ++i)
            if (fabsf(a[i].pos.x - b[i].pos.x) > 1e-3f) { moved = 1; break; }
        CHECK(moved, "spin rotates the cube with age");
        /* same for the extruded flat */
        GmEntityView f0 = mk_item(318, 0, 0), f1 = mk_item(318, 0, 7);
        int nf0 = gm_items_emit_flat(&f0, 1, a, 64);
        int nf1 = gm_items_emit_flat(&f1, 1, b, 64);
        CHECK(nf0 == 36 && nf1 == 36, "both ages emit full extruded boxes");
        moved = 0;
        for (int i = 0; i < 36; ++i)
            if (fabsf(a[i].pos.x - b[i].pos.x) > 1e-3f ||
                fabsf(a[i].pos.y - b[i].pos.y) > 1e-3f) { moved = 1; break; }
        CHECK(moved, "box bob/spin change with age");
    }

    /* ---- (D) cap respected + non-item types skipped ---- */
    {
        GmEntityView list[3] = { mk_item(3, 0, 0), mk_item(3, 0, 0), mk_item(318, 0, 0) };
        list[1].type = 2; /* zombie: not an item, must be skipped */
        CrVertex buf[80];
        memset(buf, 0xAB, sizeof buf);
        int n = gm_items_emit(list, 3, buf, 35);      /* below one cube */
        CHECK(n == 0, "cap below a cube emits nothing");
        n = gm_items_emit(list, 3, buf, 40);          /* one cube fits */
        CHECK(n == 36, "one cube fits in 40, second entity is a zombie/item");
        unsigned char *raw = (unsigned char *)&buf[36];
        int canary_ok = 1;
        for (size_t i = 0; i < sizeof(CrVertex); ++i)
            if (raw[i] != 0xAB) { canary_ok = 0; break; }
        CHECK(canary_ok, "no write past the cap");
        n = gm_items_emit_flat(list, 3, buf, 35);     /* below one extruded box */
        CHECK(n == 0, "flat cap below a box emits nothing");
    }

    /* Recorded EntityItem.hoverStart must replace the deterministic fallback. */
    {
        GmEntityView a = mk_item(3,0,0), b = a;
        a.has_hover_start=1;a.hover_start=0.0f;
        b.has_hover_start=1;b.hover_start=1.5f;
        CrVertex va[36],vb[36];
        CHECK(gm_items_emit(&a,1,va,36)==36&&gm_items_emit(&b,1,vb,36)==36,
              "recorded item hover phases both emit");
        CHECK(fabsf(va[0].pos.y-vb[0].pos.y)>1e-4f ||
              fabsf(va[0].pos.x-vb[0].pos.x)>1e-4f,
              "recorded hoverStart controls item bob/spin");
    }

    /* ---- (E) unknown item falls back to a valid sprite ---- */
    {
        int si = gm_item_sprite_index(4000);
        CHECK(si >= 0 && si < CR_ITEM_SPRITE_COUNT, "fallback sprite index valid");
        GmEntityView e = mk_item(999, 0, 0);   /* item-range id, not in atlas */
        int nf = gm_items_emit_flat(&e, 1, out, 256);
        CHECK(nf == 36, "unknown item still renders an extruded box");
    }

    /* ---- (F) exact vanilla direct fireball billboards ---- */
    {
        GmEntityView small = mk_item(385, 0, 0);
        small.type = GM_VIEW_BILLBOARD;
        int n = gm_items_emit_billboard(&small, 1, 0.0f, 0.0f, out, 256);
        CHECK(n == 6, "RenderFireball direct quad emits 6 verts");
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f;
        for (int i = 0; i < n; ++i) {
            if (out[i].pos.x < minx) minx = out[i].pos.x;
            if (out[i].pos.x > maxx) maxx = out[i].pos.x;
            if (out[i].pos.y < miny) miny = out[i].pos.y;
            if (out[i].pos.y > maxy) maxy = out[i].pos.y;
            CHECK(fabsf(out[i].pos.z - small.z) < eps,
                  "zero-pitch RenderFireball quad lies at entity z");
            CHECK(out[i].light == 1.0f && out[i].blk == 15.0f,
                  "RenderFireball is full-bright");
            CHECK(out[i].tint.r == 255 && out[i].tint.g == 255 &&
                  out[i].tint.b == 255, "RenderFireball has white tint");
        }
        CHECK(fabsf((maxx - minx) - 0.5f) < eps,
              "RenderManager small-fireball scale is 0.5");
        CHECK(fabsf(miny - (small.y - 0.125f)) < eps &&
              fabsf(maxy - (small.y + 0.375f)) < eps,
              "RenderFireball uses vanilla -0.25..0.75 y quad");
        int si = gm_item_sprite_index(385);
        CHECK(CR_ITEM_SPRITES[si].id == 385 &&
              !strcmp(CR_ITEM_SPRITES[si].name, "fireball"),
              "small fireball samples fire_charge model particle icon");

        GmEntityView dragon = small;
        dragon.type = GM_VIEW_DRAGON_FIREBALL;
        dragon.item_id = 9003;
        n = gm_items_emit_billboard(&dragon, 1, 0.0f, 0.0f, out, 256);
        CHECK(n == 6, "RenderDragonFireball direct quad emits 6 verts");
        minx = miny = 1e9f; maxx = maxy = -1e9f;
        for (int i = 0; i < n; ++i) {
            if (out[i].pos.x < minx) minx = out[i].pos.x;
            if (out[i].pos.x > maxx) maxx = out[i].pos.x;
            if (out[i].pos.y < miny) miny = out[i].pos.y;
            if (out[i].pos.y > maxy) maxy = out[i].pos.y;
        }
        CHECK(fabsf((maxx - minx) - 2.0f) < eps,
              "RenderDragonFireball scale is 2.0");
        CHECK(fabsf(miny - (dragon.y - 0.5f)) < eps &&
              fabsf(maxy - (dragon.y + 1.5f)) < eps,
              "RenderDragonFireball uses vanilla -0.25..0.75 y quad");
        si = gm_item_sprite_index(9003);
        CHECK(CR_ITEM_SPRITES[si].id == 9003 &&
              !strcmp(CR_ITEM_SPRITES[si].name, "dragon_fireball"),
              "dragon fireball samples dedicated entity texture");
        CHECK(gm_items_emit_billboard(&dragon, 1, 0.0f, 0.0f, out, 5) == 0,
              "direct fireball respects vertex cap");

        /* Non-burning billboard (flags bit 0 clear): no fire overlay — matches
         * RenderManager gating on isBurning() and the ui_entities pin golden. */
        CHECK(gm_small_fireball_fire_emit(&small, 1, 0.0f, out, 256) == 0,
              "non-burning small fireball emits no fire layers");
        small.ticks_existed = 1;
        CHECK(gm_small_fireball_fire_emit(&small, 1, 0.0f, out, 256) == 12,
              "updated legacy small fireball infers vanilla setFire state");
        small.ticks_existed = 0;
        small.flags = 1; /* isBurning */
        n = gm_small_fireball_fire_emit(&small, 1, 0.0f, out, 256);
        CHECK(n == 12, "fiery small fireball emits two stacked fire quads");
        /* Render.renderEntityOnFire: f6=minU, f8=maxU, swap f6/f8 when
         * i/2%2==0, then emit corners (f8,f9), (f6,f9), (f6,f7), (f8,f7).
         * Triangle-list corner indices are 0,1,2,5 for IR_TRI={0,1,2,0,2,3}. */
        for (int layer = 0; layer < 2; ++layer) {
            int sprite = layer == 0 ? CR_SPRITE_FIRE_LAYER_0
                                    : CR_SPRITE_FIRE_LAYER_1;
            float f6, f7, f8, f9;
            bm_sprite_uv(sprite, &f6, &f7, &f8, &f9);
            if ((layer / 2) % 2 == 0) {
                float t = f8; f8 = f6; f6 = t;
            }
            const int base = layer * 6;
            CHECK(fabsf(out[base + 0].uv.x - f8) < eps &&
                  fabsf(out[base + 0].uv.y - f9) < eps &&
                  fabsf(out[base + 1].uv.x - f6) < eps &&
                  fabsf(out[base + 1].uv.y - f9) < eps &&
                  fabsf(out[base + 2].uv.x - f6) < eps &&
                  fabsf(out[base + 2].uv.y - f7) < eps &&
                  fabsf(out[base + 5].uv.x - f8) < eps &&
                  fabsf(out[base + 5].uv.y - f7) < eps,
                  "renderEntityOnFire UV corners match vanilla f8/f6 order");
        }
        minx = miny = 1e9f; maxx = maxy = -1e9f;
        for (int i = 0; i < n; ++i) {
            if (out[i].pos.x < minx) minx = out[i].pos.x;
            if (out[i].pos.x > maxx) maxx = out[i].pos.x;
            if (out[i].pos.y < miny) miny = out[i].pos.y;
            if (out[i].pos.y > maxy) maxy = out[i].pos.y;
        }
        CHECK(fabsf((maxx - minx) - 0.4375f) < eps,
              "renderEntityOnFire scales width by 0.3125*1.4");
        CHECK(fabsf(miny - small.y) < eps &&
              fabsf(maxy - (small.y + 0.809375f)) < eps,
              "renderEntityOnFire exact two-layer y extent");
        /* Large fireball: EntityFireball width=1.0 -> scale 1.4. */
        GmEntityView large = small;
        large.item_meta = 2;
        large.flags = 1;
        n = gm_small_fireball_fire_emit(&large, 1, 0.0f, out, 256);
        CHECK(n == 12, "fiery large fireball also emits two fire layers");
        minx = miny = 1e9f; maxx = maxy = -1e9f;
        for (int i = 0; i < n; ++i) {
            if (out[i].pos.x < minx) minx = out[i].pos.x;
            if (out[i].pos.x > maxx) maxx = out[i].pos.x;
            if (out[i].pos.y < miny) miny = out[i].pos.y;
            if (out[i].pos.y > maxy) maxy = out[i].pos.y;
        }
        CHECK(fabsf((maxx - minx) - 1.4f) < eps,
              "large fireball fire overlay width*1.4 = 1.4");
        CHECK(fabsf((maxx - minx) / 0.4375f - (1.4f / 0.4375f)) < 0.01f,
              "large/small fire extent ratio is width ratio 1.0/0.3125");
        CHECK(gm_small_fireball_fire_emit(&dragon, 1, 0.0f, out, 256) == 0,
              "non-fiery dragon fireball has no fire overlay");

        /* Living entities: EntityBlaze.isBurning() is its charged/aggro flag,
         * recorded as flags bit 0. Blaze inherits the Entity default AABB
         * 0.6 x 1.8 -> scale 0.84, f3 = 1.8/0.84 = 2.142857 -> five layers
         * (2.142, 1.692, 1.242, 0.792, 0.342). */
        GmEntityView blaze;
        memset(&blaze, 0, sizeof blaze);
        blaze.type = 7; /* EW_TYPE_BLAZE */
        blaze.x = 3.0f; blaze.y = 5.0f; blaze.z = -2.0f;
        CHECK(gm_entity_fire_emit(&blaze, 1, 0.0f, out, 256) == 0,
              "idle (uncharged) blaze emits no fire layers");
        blaze.flags = 1; /* isBurning -> isCharged */
        n = gm_entity_fire_emit(&blaze, 1, 0.0f, out, 256);
        CHECK(n == 30, "charged blaze emits five stacked fire quads");
        blaze.flags = 1 | 4;
        CHECK(gm_entity_fire_emit(&blaze, 1, 0.0f, out, 256) == 30,
              "invisible burning entity retains its separate fire overlay");
        blaze.flags = 1;
        minx = miny = 1e9f; maxx = maxy = -1e9f;
        for (int i = 0; i < n; ++i) {
            if (out[i].pos.x < minx) minx = out[i].pos.x;
            if (out[i].pos.x > maxx) maxx = out[i].pos.x;
            if (out[i].pos.y < miny) miny = out[i].pos.y;
            if (out[i].pos.y > maxy) maxy = out[i].pos.y;
        }
        CHECK(fabsf((maxx - minx) - 0.84f) < eps,
              "blaze fire overlay width = 0.6 * 1.4");
        CHECK(fabsf(miny - blaze.y) < eps && maxy > blaze.y + 1.8f,
              "blaze fire starts at the feet and overshoots the model top");
        CHECK(out[0].light > 0.99f && out[0].blk > 14.99f,
              "fire layers are unlit (disableLighting -> lightmap max)");
        /* Not a living entity: dropped items/projectiles keep their own pass. */
        GmEntityView burning_item = blaze;
        burning_item.type = GM_VIEW_ITEM;
        CHECK(gm_entity_fire_emit(&burning_item, 1, 0.0f, out, 256) == 0,
              "non-living views are skipped by the living fire pass");
    }

    /* ---- (G) Illager/Vex held-item layers ---- */
    {
        enum { HELD_CAP = 8192 };
        static CrVertex held_a[HELD_CAP], held_b[HELD_CAP];
        GmEntityView vindicator;
        memset(&vindicator, 0, sizeof vindicator);
        vindicator.type = 51;
        vindicator.x = 2.0f; vindicator.y = 64.0f; vindicator.z = 3.0f;
        CHECK(gm_held_items_emit(&vindicator, 1, out, 256) == 0,
              "idle Vindicator keeps its axe hidden");
        vindicator.flags = 256;
        vindicator.ticks_existed = 17;
        int nv = gm_held_items_emit(&vindicator, 1, held_a, HELD_CAP);
        CHECK(nv > 12 && nv % 6 == 0,
              "aggressive Vindicator emits an extruded iron-axe model");

        GmEntityView vex;
        memset(&vex, 0, sizeof vex);
        vex.type = 53;
        vex.x = 2.0f; vex.y = 64.0f; vex.z = 3.0f;
        vex.ticks_existed = 17;
        int ni = gm_held_items_emit(&vex, 1, held_a, HELD_CAP);
        vex.flags = 1024;
        int nc = gm_held_items_emit(&vex, 1, held_b, HELD_CAP);
        CHECK(ni > 12 && ni == nc && ni % 6 == 0,
              "idle and charging Vex emit one extruded iron-sword model");
        int pose_changed = 0;
        for (int i = 0; i < ni; ++i)
            pose_changed |= fabsf(held_a[i].pos.x - held_b[i].pos.x)
                    > eps
                || fabsf(held_a[i].pos.y - held_b[i].pos.y) > eps
                || fabsf(held_a[i].pos.z - held_b[i].pos.z) > eps;
        CHECK(pose_changed, "Vex charge state changes the sword-arm pose");

        GmEntityView skeleton;
        memset(&skeleton, 0, sizeof skeleton);
        skeleton.type = 3;
        skeleton.x = 2.0f; skeleton.y = 64.0f; skeleton.z = 3.0f;
        skeleton.ticks_existed = 17;
        int nsi = gm_held_items_emit(&skeleton, 1, held_a, HELD_CAP);
        skeleton.flags = 131072; /* AbstractSkeleton swinging-arms bow pose */
        skeleton.pitch = 25.0f;
        int nsc = gm_held_items_emit(&skeleton, 1, held_b, HELD_CAP);
        CHECK(nsi > 12 && nsi == nsc && nsi % 6 == 0,
              "skeleton emits the complete generated bow including rims");
        CHECK(gm_held_items_emit(&skeleton, 1, held_b, nsc - 1) == 0,
              "generated bow capacity remains atomic");
        pose_changed = 0;
        for (int i = 0; i < nsi; ++i)
            pose_changed |= fabsf(held_a[i].pos.x - held_b[i].pos.x) > eps
                || fabsf(held_a[i].pos.y - held_b[i].pos.y) > eps
                || fabsf(held_a[i].pos.z - held_b[i].pos.z) > eps;
        CHECK(pose_changed,
              "AbstractSkeleton swinging-arms state selects bow-and-arrow pose");

        GmEntityView stand;
        memset(&stand, 0, sizeof stand);
        stand.type = 34;
        stand.x = 2.0f; stand.y = 64.0f; stand.z = 3.0f;
        stand.stand_mainhand = 276;
        stand.stand_offhand = 261;
        stand.stand_pose_valid = 1;
        stand.stand_pose[2][0] = -10.0f;
        stand.stand_pose[2][2] = -10.0f;
        stand.stand_pose[3][0] = -15.0f;
        stand.stand_pose[3][2] = 10.0f;
        int nas = gm_held_items_emit(&stand, 1, held_a, HELD_CAP);
        GmEntityView one_hand = stand;
        one_hand.stand_offhand = 0;
        int nmain = gm_held_items_emit(&one_hand, 1, held_b, HELD_CAP);
        one_hand = stand;
        one_hand.stand_mainhand = 0;
        int noff = gm_held_items_emit(&one_hand, 1, held_b, HELD_CAP);
        CHECK(nmain > 12 && noff > 12 && nas == nmain + noff,
              "armor stand emits independent extruded mainhand and offhand items");
        CHECK(gm_held_items_emit(&stand, 1, held_b, nmain - 1) == 0
                  && gm_held_items_emit(&stand, 1, held_b, nmain) == nmain,
              "armor-stand held-item capacity remains atomic per hand");
        stand.stand_flags = 4;
        int nss = gm_held_items_emit(&stand, 1, held_b, HELD_CAP);
        int small_exact = nas == nss;
        for (int i = 0; i < nas && small_exact; ++i) {
            small_exact = fabsf((held_b[i].pos.x - stand.x)
                    - (held_a[i].pos.x - stand.x) * 0.5f) < eps
                && fabsf((held_b[i].pos.y - stand.y)
                    - (held_a[i].pos.y - stand.y) * 0.5f) < eps
                && fabsf((held_b[i].pos.z - stand.z)
                    - (held_a[i].pos.z - stand.z) * 0.5f) < eps;
        }
        CHECK(small_exact,
              "Small armor stand applies LayerHeldItem child scale exactly");
        stand.flags = 4;
        CHECK(gm_held_items_emit(&stand, 1, out, 24) == 0,
              "invisible armor stand hides both held-item layers");

        memset(&stand, 0, sizeof stand);
        stand.type = 34;
        stand.x = 2.0f; stand.y = 64.0f; stand.z = 3.0f;
        stand.stand_mainhand = 3; /* dirt block model */
        stand.stand_mainhand_meta = 0;
        stand.stand_pose_valid = 1;
        stand.stand_pose[3][0] = -15.0f;
        stand.stand_pose[3][2] = 10.0f;
        CrVertex adult_block[36], small_block[36];
        int nab = gm_held_blocks_emit(&stand, 1, adult_block, 36);
        CHECK(nab == 36,
              "armor stand held block uses one terrain-atlas cube model");
        CHECK(gm_held_items_emit(&stand, 1, out, 256) == 0,
              "held block is excluded from the flat item-atlas pass");
        CHECK(gm_held_blocks_emit(&stand, 1, out, 35) == 0,
              "held-block capacity remains atomic per hand");
        stand.stand_mainhand = 0;
        stand.armor_head = 86; /* pumpkin block through LayerCustomHead */
        stand.armor_head_meta = 0;
        CHECK(gm_held_blocks_emit(&stand, 1, out, 36) == 36,
              "armor-stand head block uses LayerCustomHead terrain model");
        CHECK(gm_held_blocks_emit(&stand, 1, out, 35) == 0,
              "head-block capacity remains atomic");
        stand.armor_head = 280; /* generated stick item */
        CHECK(gm_held_blocks_emit(&stand, 1, out, 36) == 0
                  && gm_held_items_emit(&stand, 1, held_a, HELD_CAP) > 12,
              "arbitrary non-armor head item uses its HEAD item transform");
        stand.armor_head = 310;
        CHECK(gm_held_items_emit(&stand, 1, held_a, HELD_CAP) == 0,
              "helmet head stack remains owned by the armor-model pass");
        stand.armor_head = 397;
        CHECK(gm_held_items_emit(&stand, 1, held_a, HELD_CAP) == 0,
              "skull head stack remains owned by the custom-skull pass");
        stand.armor_head = 0;
        stand.stand_mainhand = 3;
        stand.stand_flags = 4;
        int nsb = gm_held_blocks_emit(&stand, 1, small_block, 36);
        int small_block_exact = nab == 36 && nsb == 36;
        for (int i = 0; i < 36 && small_block_exact; ++i) {
            small_block_exact = fabsf((small_block[i].pos.x - stand.x)
                    - (adult_block[i].pos.x - stand.x) * 0.5f) < eps
                && fabsf((small_block[i].pos.y - stand.y)
                    - (adult_block[i].pos.y - stand.y) * 0.5f) < eps
                && fabsf((small_block[i].pos.z - stand.z)
                    - (adult_block[i].pos.z - stand.z) * 0.5f) < eps;
        }
        CHECK(small_block_exact,
              "Small armor stand scales the held block layer exactly");
        stand.flags = 4;
        CHECK(gm_held_blocks_emit(&stand, 1, out, 36) == 0,
              "invisible armor stand hides held block layers");
    }

    /* ---- (G) LayerMooshroomMushroom attached terrain models ---- */
    {
        {
            static const float from[4][3] = {
                {0.8f,0.0f,8.0f}, {0.8f,0.0f,8.0f},
                {8.0f,0.0f,0.8f}, {8.0f,0.0f,0.8f},
            };
            static const float to[4][3] = {
                {15.2f,16.0f,8.0f}, {15.2f,16.0f,8.0f},
                {8.0f,16.0f,15.2f}, {8.0f,16.0f,15.2f},
            };
            static const int input_face[4] = {
                BM_NORTH, BM_SOUTH, BM_WEST, BM_EAST,
            };
            static const int oracle_face[4] = {
                BM_NORTH, BM_SOUTH, BM_SOUTH, BM_NORTH,
            };
            static const float uv[4] = {0,0,16,16};
            static const float origin[3] = {.5f,.5f,.5f};
            for (int i = 0; i < 4; ++i) {
                int32_t baked[28];
                int got = rk_facebakery_make_quad(
                    from[i][0], from[i][1], from[i][2],
                    to[i][0], to[i][1], to[i][2], input_face[i], 0, uv,
                    0.5f, 0.55f, 0.2f, 0.25f, 1, 1, 45.0f,
                    origin, 1, baked);
                CHECK(got == oracle_face[i],
                      "red-mushroom FaceBakery post-rotation face matches Java");
            }
        }
        GmEntityView mooshroom;
        memset(&mooshroom, 0, sizeof mooshroom);
        mooshroom.type = 12; /* shared cow model */
        mooshroom.skin = CR_MOB_MOOSHROOM + 1;
        mooshroom.x = 2.0f; mooshroom.y = 64.0f; mooshroom.z = -3.0f;
        mooshroom.yaw = 25.0f;
        mooshroom.head_yaw = 38.0f;
        mooshroom.pitch = -12.0f;
        CrVertex mushrooms_a[72], mushrooms_b[72];
        int nm = gm_mooshroom_mushrooms_emit(
            &mooshroom, 1, mushrooms_a, 72);
        CHECK(nm == 72,
              "adult Mooshroom emits three four-quad mushroom models");
        CHECK(verts_use_sprite(
                  mushrooms_a, nm, CR_SPRITE_MUSHROOM_RED),
              "Mooshroom layer uses only the red-mushroom block sprite");
        CHECK(gm_mooshroom_mushrooms_emit(
                  &mooshroom, 1, out, 71) == 0,
              "Mooshroom mushroom layer has atomic entity capacity");

        mooshroom.head_yaw += 31.0f;
        CHECK(gm_mooshroom_mushrooms_emit(
                  &mooshroom, 1, mushrooms_b, 72) == 72,
              "Mooshroom head-pose variant emits the complete layer");
        int back_same = 1, head_changed = 0;
        for (int i = 0; i < 48; ++i)
            if (memcmp(&mushrooms_a[i].pos, &mushrooms_b[i].pos,
                       sizeof mushrooms_a[i].pos) != 0)
                back_same = 0;
        for (int i = 48; i < 72; ++i)
            if (memcmp(&mushrooms_a[i].pos, &mushrooms_b[i].pos,
                       sizeof mushrooms_a[i].pos) != 0)
                head_changed = 1;
        CHECK(back_same && head_changed,
              "head yaw moves only the head-attached mushroom");

        mooshroom.flags = 8;
        CHECK(gm_mooshroom_mushrooms_emit(
                  &mooshroom, 1, out, 72) == 0,
              "child Mooshroom suppresses all attached mushrooms");
        mooshroom.flags = 4;
        CHECK(gm_mooshroom_mushrooms_emit(
                  &mooshroom, 1, out, 72) == 0,
              "invisible Mooshroom suppresses all attached mushrooms");
        mooshroom.flags = 0;
        mooshroom.skin = 0;
        CHECK(gm_mooshroom_mushrooms_emit(
                  &mooshroom, 1, out, 72) == 0,
              "ordinary cow does not receive the Mooshroom layer");
    }

    /* ---- (H) RenderItemFrame baked models and FIXED displayed items ---- */
    {
        GmItemFrameRenderView f;
        memset(&f, 0, sizeof f);
        f.x = 10.5f; f.y = 64.5f; f.z = 20.5f;
        f.facing = 2; f.lm_light = 12.0f; f.lm_blk = 3.0f;
        CrVertex frame[132];
        CHECK(gm_item_frames_emit(&f, 1, frame, 131) == 0,
              "item-frame baked model has atomic capacity");
        int nf = gm_item_frames_emit(&f, 1, frame, 132);
        CHECK(nf == 132, "empty item frame emits its exact 22 baked quads");
        float minx = 1e9f, miny = 1e9f, minz = 1e9f;
        float maxx = -1e9f, maxy = -1e9f, maxz = -1e9f;
        int frame_light = 1;
        for (int i = 0; i < nf; ++i) {
            if (frame[i].pos.x < minx) minx = frame[i].pos.x;
            if (frame[i].pos.x > maxx) maxx = frame[i].pos.x;
            if (frame[i].pos.y < miny) miny = frame[i].pos.y;
            if (frame[i].pos.y > maxy) maxy = frame[i].pos.y;
            if (frame[i].pos.z < minz) minz = frame[i].pos.z;
            if (frame[i].pos.z > maxz) maxz = frame[i].pos.z;
            if (frame[i].light != 12.0f || frame[i].blk != 3.0f)
                frame_light = 0;
        }
        CHECK(fabsf((maxx - minx) - .75f) < eps
                  && fabsf((maxy - miny) - .75f) < eps
                  && fabsf((maxz - minz) - .0625f) < eps,
              "ordinary item-frame model has exact 12x12x1 outer bounds");
        CHECK(frame_light, "item-frame model retains entity lightmap levels");

        f.item = 358;
        nf = gm_item_frames_emit(&f, 1, frame, 132);
        minx = miny = 1e9f; maxx = maxy = -1e9f;
        for (int i = 0; i < nf; ++i) {
            if (frame[i].pos.x < minx) minx = frame[i].pos.x;
            if (frame[i].pos.x > maxx) maxx = frame[i].pos.x;
            if (frame[i].pos.y < miny) miny = frame[i].pos.y;
            if (frame[i].pos.y > maxy) maxy = frame[i].pos.y;
        }
        CHECK(nf == 132 && fabsf((maxx - minx) - 1.0f) < eps
                  && fabsf((maxy - miny) - 1.0f) < eps,
              "filled map selects the exact full-width frame model");
        CHECK(gm_item_frame_block_items_emit(&f, 1, out, 256) == 0
                  && gm_item_frame_flat_items_emit(&f, 1, out, 256) == 0,
              "filled-map content is reserved for the dynamic map renderer");
        unsigned char map_colors[128 * 128];
        CrRgba map_rgba[128 * 128];
        memset(map_colors, 0, sizeof map_colors);
        map_colors[2] = 4;
        map_colors[3] = 7;
        CHECK(gm_item_frame_map_rgba(map_colors, map_rgba),
              "valid MapData color bytes build a dynamic RGBA plane");
        CHECK(map_rgba[0].r == 0 && map_rgba[0].g == 0
                  && map_rgba[0].b == 0 && map_rgba[0].a == 16
                  && map_rgba[1].a == 24 && map_rgba[128].a == 24
                  && map_rgba[129].a == 16,
              "MapColor AIR uses Java's exact checkerboard alpha");
        CHECK(map_rgba[2].r == 89 && map_rgba[2].g == 125
                  && map_rgba[2].b == 39 && map_rgba[2].a == 255
                  && map_rgba[3].r == 67 && map_rgba[3].g == 94
                  && map_rgba[3].b == 29 && map_rgba[3].a == 255,
              "MapColor palette and four-level shade arithmetic are exact");
        map_colors[4] = 144;
        CHECK(!gm_item_frame_map_rgba(map_colors, map_rgba),
              "undefined 1.11.2 MapColor indices are rejected");
        map_colors[4] = 4;
        f.map_colors = map_colors;
        CrVertex map_plane[6];
        CHECK(gm_item_frame_map_plane_emit(&f, map_plane, 5) == 0,
              "filled-map plane capacity is atomic");
        CHECK(gm_item_frame_map_plane_emit(&f, map_plane, 6) == 6,
              "filled-map plane emits one exact quad");
        minx = miny = 1e9f; maxx = maxy = -1e9f;
        for (int i = 0; i < 6; ++i) {
            if (map_plane[i].pos.x < minx) minx = map_plane[i].pos.x;
            if (map_plane[i].pos.x > maxx) maxx = map_plane[i].pos.x;
            if (map_plane[i].pos.y < miny) miny = map_plane[i].pos.y;
            if (map_plane[i].pos.y > maxy) maxy = map_plane[i].pos.y;
        }
        CHECK(fabsf((maxx - minx) - 1.0f) < eps
                  && fabsf((maxy - miny) - 1.0f) < eps,
              "filled-map plane composes Java's exact 1/128 transform");
        f.map_decoration_present = 1;
        f.map_decoration_type = 5;
        f.map_decoration_x = -32;
        f.map_decoration_z = 48;
        f.map_decoration_rotation = 7;
        CHECK(gm_item_frame_map_icon_emit(&f, map_plane, 5) == 0
                  && gm_item_frame_map_icon_emit(&f, map_plane, 6) == 6,
              "frame-visible map marker emits atomically from map_icons");
        int icon_uv_ok = 1;
        const CrMobSprite *map_icons = &CR_MOB_SPRITES[CR_MOB_MAP_ICONS];
        float icon_u0 = (map_icons->x0 + map_icons->w * .25f)
            / CR_MOB_ATLAS_W;
        float icon_u1 = (map_icons->x0 + map_icons->w * .5f)
            / CR_MOB_ATLAS_W;
        float icon_v0 = (map_icons->y0 + map_icons->h * .25f)
            / CR_MOB_ATLAS_H;
        float icon_v1 = (map_icons->y0 + map_icons->h * .5f)
            / CR_MOB_ATLAS_H;
        for (int i = 0; i < 6; ++i)
            if (map_plane[i].uv.x < icon_u0 - eps
                    || map_plane[i].uv.x > icon_u1 + eps
                    || map_plane[i].uv.y < icon_v0 - eps
                    || map_plane[i].uv.y > icon_v1 + eps)
                icon_uv_ok = 0;
        CHECK(icon_uv_ok,
              "map marker type selects the exact 4x4 map_icons cell");
        f.map_colors = NULL;
        CHECK(gm_item_frame_map_plane_emit(&f, map_plane, 6) == 0
                  && gm_item_frame_map_icon_emit(&f, map_plane, 6) == 0,
              "absent MapData does not fabricate map content or marker");

        f.item = 3; f.meta = 0; f.rotation = 0;
        CrVertex cube0[36], cube1[36];
        int nc0 = gm_item_frame_block_items_emit(&f, 1, cube0, 36);
        CHECK(gm_item_frame_block_items_emit(&f, 1, cube1, 35) == 0,
              "displayed block item capacity is atomic");
        CHECK(nc0 == 36, "displayed dirt uses its six-face FIXED block model");
        minx = miny = minz = 1e9f;
        maxx = maxy = maxz = -1e9f;
        for (int i = 0; i < nc0; ++i) {
            if (cube0[i].pos.x < minx) minx = cube0[i].pos.x;
            if (cube0[i].pos.x > maxx) maxx = cube0[i].pos.x;
            if (cube0[i].pos.y < miny) miny = cube0[i].pos.y;
            if (cube0[i].pos.y > maxy) maxy = cube0[i].pos.y;
            if (cube0[i].pos.z < minz) minz = cube0[i].pos.z;
            if (cube0[i].pos.z > maxz) maxz = cube0[i].pos.z;
        }
        CHECK(fabsf((maxx - minx) - .25f) < eps
                  && fabsf((maxy - miny) - .25f) < eps
                  && fabsf((maxz - minz) - .25f) < eps,
              "displayed block composes outer 0.5 and FIXED 0.5 scales");
        f.rotation = 1;
        CHECK(gm_item_frame_block_items_emit(&f, 1, cube1, 36) == 36,
              "rotated displayed block still emits completely");
        int rotated = 0;
        for (int i = 0; i < 36; ++i)
            if (fabsf(cube0[i].pos.x - cube1[i].pos.x) > eps
                    || fabsf(cube0[i].pos.y - cube1[i].pos.y) > eps)
                rotated = 1;
        CHECK(rotated, "item-frame rotation applies exact 45-degree Z steps");

        f.item = 280; f.rotation = 3;
        CrVertex flat[8192];
        int ng = gm_item_frame_flat_items_emit(&f, 1, flat, 8192);
        CHECK(ng > 12 && ng <= 8192,
              "generated stick emits front, back and alpha-boundary extrusion");
        CHECK(gm_item_frame_flat_items_emit(&f, 1, flat, ng - 1) == 0,
              "generated displayed item capacity is atomic");
        CHECK(gm_item_frame_block_items_emit(&f, 1, out, 256) == 0,
              "generated displayed item does not claim terrain-atlas pass");
        int flat_light = 1;
        for (int i = 0; i < ng; ++i)
            if (flat[i].light != 12.0f || flat[i].blk != 3.0f)
                flat_light = 0;
        CHECK(flat_light, "displayed generated item retains frame lightmap levels");

        f.facing = 4; f.item = 0; f.rotation = 0;
        CHECK(gm_item_frames_emit(&f, 1, frame, 132) == 132,
              "west-facing item frame emits completely");
        minx = maxx = frame[0].pos.x;
        minz = maxz = frame[0].pos.z;
        for (int i = 1; i < 132; ++i) {
            if (frame[i].pos.x < minx) minx = frame[i].pos.x;
            if (frame[i].pos.x > maxx) maxx = frame[i].pos.x;
            if (frame[i].pos.z < minz) minz = frame[i].pos.z;
            if (frame[i].pos.z > maxz) maxz = frame[i].pos.z;
        }
        CHECK(fabsf((maxx - minx) - .0625f) < eps
                  && fabsf((maxz - minz) - .75f) < eps,
              "item-frame facing rotates model thickness from Z onto X");
    }

    /* ---- (H) GUI isometric block icons ---- */
    {
        const int W = 32, H = 32;
        CrFramebuffer fb;
        fb.w = W; fb.h = H;
        fb.color = calloc((size_t)W * H, sizeof(CrRgba));
        fb.depth = calloc((size_t)W * H, sizeof(float));
        CHECK(fb.color && fb.depth, "icon test fb alloc");
        /* dirt (3), cobble (4), crafting table (58) must draw; stick (280) must not */
        CHECK(gm_item_draw_block_icon(&fb, 3, 0, 0, 0, 1), "dirt iso icon draws");
        int dirt_px = 0;
        for (int i = 0; i < W * H; ++i) if (fb.color[i].a) dirt_px++;
        CHECK(dirt_px > 20, "dirt iso fills many pixels");

        /* RenderItem rasterizes after the scaled-GUI matrix reaches the real
         * framebuffer. A scale-2 icon must therefore not be a nearest-neighbor
         * enlargement made of uniform 2x2 cells. */
        memset(fb.color, 0, (size_t)W * H * sizeof(CrRgba));
        CHECK(gm_item_draw_block_icon(&fb, 3, 0, 0, 0, 2),
              "scale-2 dirt iso icon draws");
        int physical_raster = 0;
        for (int y = 0; y < H; y += 2) {
            for (int x = 0; x < W; x += 2) {
                CrRgba a = fb.color[y * W + x];
                for (int yy = 0; yy < 2; ++yy)
                    for (int xx = 0; xx < 2; ++xx) {
                        CrRgba b = fb.color[(y + yy) * W + x + xx];
                        physical_raster |= a.r != b.r || a.g != b.g ||
                                           a.b != b.b || a.a != b.a;
                    }
            }
        }
        CHECK(physical_raster, "scale-2 icon rasterizes on physical pixel grid");

        memset(fb.color, 0, (size_t)W * H * sizeof(CrRgba));
        CHECK(gm_item_draw_block_icon(&fb, 4, 0, 0, 0, 1), "cobble iso icon draws");
        int cobble_px = 0;
        for (int i = 0; i < W * H; ++i) if (fb.color[i].a) cobble_px++;
        CHECK(cobble_px > 20, "cobble iso fills many pixels");

        memset(fb.color, 0, (size_t)W * H * sizeof(CrRgba));
        CHECK(gm_item_draw_block_icon(&fb, 58, 0, 0, 0, 1), "crafting-table iso icon draws");
        int ct_px = 0;
        for (int i = 0; i < W * H; ++i) if (fb.color[i].a) ct_px++;
        CHECK(ct_px > 20, "crafting-table iso fills many pixels");

        memset(fb.color, 0, (size_t)W * H * sizeof(CrRgba));
        CHECK(gm_item_draw_block_icon(&fb, 171, 4, 0, 0, 1),
              "yellow carpet uses the thin metadata-colored block icon");
        int carpet_px = 0, carpet_yellow = 0;
        for (int i = 0; i < W * H; ++i) {
            if (fb.color[i].a) ++carpet_px;
            if (fb.color[i].r > fb.color[i].b + 40
                    && fb.color[i].g > fb.color[i].b + 40)
                ++carpet_yellow;
        }
        CHECK(carpet_px > 10 && carpet_px < dirt_px,
              "carpet icon is visibly thinner than a full block");
        CHECK(carpet_yellow > 10,
              "carpet metadata selects the yellow wool texture");

        CHECK(!gm_item_draw_block_icon(&fb, 280, 0, 0, 0, 1), "stick is not a block icon");
        free(fb.color); free(fb.depth);
    }

    /* ---- (I) EntityFallingBlock full-size cube (RenderFallingBlock) ---- */
    {
        CrVertex out[64];
        GmEntityView fb;
        memset(&fb, 0, sizeof fb);
        fb.type = GM_VIEW_FALLING_BLOCK;
        fb.x = 5.0f; fb.y = 70.0f; fb.z = 8.0f;
        fb.item_id = 12; /* sand */
        fb.item_meta = 0;
        int n = gm_falling_blocks_emit(&fb, 1, out, 64);
        CHECK(n == 36, "falling sand emits 36 verts (full cube)");
        float minx = 1e9f, miny = 1e9f, minz = 1e9f;
        float maxx = -1e9f, maxy = -1e9f, maxz = -1e9f;
        for (int i = 0; i < n; ++i) {
            if (out[i].pos.x < minx) minx = out[i].pos.x;
            if (out[i].pos.x > maxx) maxx = out[i].pos.x;
            if (out[i].pos.y < miny) miny = out[i].pos.y;
            if (out[i].pos.y > maxy) maxy = out[i].pos.y;
            if (out[i].pos.z < minz) minz = out[i].pos.z;
            if (out[i].pos.z > maxz) maxz = out[i].pos.z;
        }
        const float eps = 1e-4f;
        CHECK(fabsf(miny - 70.0f) < eps && fabsf(maxy - 71.0f) < eps,
              "falling block y spans feet..feet+1 (block model, not the box)");
        CHECK(fabsf((maxx - minx) - 1.0f) < eps &&
              fabsf((maxz - minz) - 1.0f) < eps,
              "falling block xz is a unit cube centred on the entity");
        /* item drop stays miniature; falling is full-size. */
        GmEntityView drop = mk_item(12, 0, 0);
        int nd = gm_items_emit(&drop, 1, out, 64);
        CHECK(nd == 36, "sand drop still emits");
        float dmaxx = -1e9f, dminx = 1e9f;
        for (int i = 0; i < nd; ++i) {
            if (out[i].pos.x < dminx) dminx = out[i].pos.x;
            if (out[i].pos.x > dmaxx) dmaxx = out[i].pos.x;
        }
        CHECK((dmaxx - dminx) < 0.5f, "item drop cube is sub-block scale");
        fb.item_id = 0;
        CHECK(gm_falling_blocks_emit(&fb, 1, out, 64) == 0,
              "falling block with no block id emits nothing");

        memset(&fb, 0, sizeof fb);
        fb.type = GM_VIEW_TNT_PRIMED;
        fb.ent_id = 9708;
        fb.ticks_existed = 35;
        fb.x = 0.5f; fb.y = 4.16f; fb.z = 4.5f;
        n = gm_falling_blocks_emit(&fb, 1, out, 64);
        CHECK(n == 36, "primed TNT emits the TNT block model");
        CHECK(out[0].pos.y >= fb.y && out[0].pos.y <= fb.y + 1.0f,
              "primed TNT block is lifted from entity feet");
        CHECK(fabsf(out[12].uv.x - out[13].uv.x) < eps &&
              out[12].uv.y > out[13].uv.y &&
              fabsf(out[13].uv.y - out[14].uv.y) < eps &&
              out[13].uv.x > out[14].uv.x,
              "primed TNT north face uses FaceBakery UV orientation");
    }

    /* ---- (J) all minecart subtype display blocks and live overrides ---- */
    {
        GmEntityView cart;
        CrVertex v[256];
        memset(&cart, 0, sizeof cart);
        cart.x = 2.5f; cart.y = 70.0625f; cart.z = 3.5f;

        cart.type = GM_VIEW_MINECART_SPAWNER;
        int n = gm_minecart_contents_emit(&cart, 1, 1.0f, v, 256);
        CHECK(n == 36, "spawner minecart emits its default display block");
        CHECK(verts_use_sprite(v, n, CR_SPRITE_MOB_SPAWNER),
              "spawner minecart uses mob-spawner texture on every face");

        cart.type = GM_VIEW_MINECART_COMMAND;
        n = gm_minecart_contents_emit(&cart, 1, 1.0f, v, 256);
        CHECK(n == 36, "command minecart emits its default display block");
        CHECK(verts_use_sprite(v + 12, 6, CR_SPRITE_COMMAND_BLOCK_FRONT)
                  && verts_use_sprite(v + 18, 6, CR_SPRITE_COMMAND_BLOCK_BACK)
                  && verts_use_sprite(v, 6, CR_SPRITE_COMMAND_BLOCK_SIDE),
              "command minecart uses north-facing command-block model");

        cart.type = GM_VIEW_MINECART_FURNACE;
        cart.minecart_powered = 1;
        n = gm_minecart_contents_emit(&cart, 1, 1.0f, v, 256);
        CHECK(n == 36, "powered furnace minecart emits its display block");
        CHECK(verts_use_sprite(v + 12, 6, CR_SPRITE_FURNACE_FRONT_ON),
              "powered furnace minecart uses lit front");

        cart.type = GM_VIEW_MINECART_COMMAND;
        cart.minecart_custom_display = 1;
        cart.minecart_display_block = 1;
        cart.minecart_display_meta = 4;
        cart.minecart_display_offset = 9;
        n = gm_minecart_contents_emit(&cart, 1, 1.0f, v, 256);
        CHECK(n == 36, "custom minecart cube replaces subtype default");
        {
            const BmBlock *stone = bm_block(gm_state_to_model_key(
                gm_pack_state(1, 4)));
            CHECK(stone && verts_use_sprite(v, 6, stone->face[0].sprite),
                  "custom minecart display preserves block metadata");
        }

        cart.minecart_display_block = 0;
        CHECK(gm_minecart_contents_emit(&cart, 1, 1.0f, v, 256) == 0,
              "custom air display hides subtype default");

        memset(&cart, 0, sizeof cart);
        cart.x = 2.5f; cart.y = 70.0625f; cart.z = 3.5f;
        cart.type = GM_VIEW_MINECART_TNT;
        cart.minecart_tnt_fuse = 4;
        cart.minecart_tnt_fuse_valid = 1;
        n = gm_minecart_contents_emit(&cart, 1, 1.0f, v, 256);
        CHECK(n == 36, "fused TNT minecart emits scaled display block");
        CHECK(fabsf(v[12].uv.x - v[13].uv.x) < eps &&
                  v[12].uv.y > v[13].uv.y &&
                  fabsf(v[13].uv.y - v[14].uv.y) < eps &&
                  v[13].uv.x > v[14].uv.x,
              "TNT minecart display uses FaceBakery UV orientation");
        {
            /* Render partial=1 makes (fuse-partial+1) exactly fuse. */
            float swell = 0.6f * 0.6f * 0.6f * 0.6f;
            float expected = 0.75f * (1.0f + 0.3f * swell);
            float min_x = v[0].pos.x, max_x = v[0].pos.x;
            for (int index = 1; index < n; ++index) {
                if (v[index].pos.x < min_x) min_x = v[index].pos.x;
                if (v[index].pos.x > max_x) max_x = v[index].pos.x;
            }
            CHECK(fabsf((max_x - min_x) - expected) < eps,
                  "TNT minecart fuse applies Java quartic swell scale");
        }
        n = gm_minecart_tnt_flash_emit(&cart, 1, 1.0f, v, 256);
        CHECK(n == 36, "TNT minecart even five-tick phase emits flash pass");
        CHECK(v[0].tint.r == 255 && v[0].tint.g == 255
                  && v[0].tint.b == 255 && v[0].tint.a == 255
                  && v[0].light == 1.0f && v[0].ao == 1.0f,
              "TNT minecart flash preserves baked-quad opaque alpha");
        cart.minecart_tnt_fuse = 80;
        n = gm_minecart_tnt_flash_emit(&cart, 1, 1.0f, v, 256);
        CHECK(n == 36 && v[0].tint.a == 255,
              "early TNT minecart flash is also opaque like Java 1.11.2");
        cart.minecart_tnt_fuse = 5;
        CHECK(gm_minecart_tnt_flash_emit(&cart, 1, 1.0f, v, 256) == 0,
              "TNT minecart odd five-tick phase suppresses flash pass");
        cart.minecart_tnt_fuse = -1;
        CHECK(gm_minecart_tnt_flash_emit(&cart, 1, 1.0f, v, 256) == 0,
              "unprimed TNT minecart suppresses flash pass");
    }

    if (g_fail) { fprintf(stderr, "test_item_render: FAILED\n"); return 1; }
    printf("test_item_render: OK\n");
    return 0;
}
