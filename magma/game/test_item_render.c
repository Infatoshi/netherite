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
 * (G) GUI ISO: dirt/cobble/crafting-table block icons draw into a 16x16 slot
 *     (non-empty pixels); a stick id does not claim the block-icon path.
 *
 * Build/run: bash game/test_item_render.sh
 */
#include "core/types.h"
#include "game/item_render.h"
#include "game/block_registry.h"
#include "assets/atlas_gen.h"
#include "assets/blockmodels.h"
#include "assets/item_atlas.h"

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

    /* ---- (G) GUI isometric block icons ---- */
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

        CHECK(!gm_item_draw_block_icon(&fb, 280, 0, 0, 0, 1), "stick is not a block icon");
        free(fb.color); free(fb.depth);
    }

    /* ---- (H) EntityFallingBlock full-size cube (RenderFallingBlock) ---- */
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

    if (g_fail) { fprintf(stderr, "test_item_render: FAILED\n"); return 1; }
    printf("test_item_render: OK\n");
    return 0;
}
