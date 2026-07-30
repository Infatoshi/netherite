/* game/test_hand.c - first-person held-item emit path (divergence #13).
 *
 * (A) EMPTY: item_id 0 emits 0 verts (bare-arm path is separate).
 * (B) STICK (280): emits front/back + per-texel rim verts; all UV inside stick sprite;
 *     geometry spans a non-zero AABB in eye space; swing moves verts.
 * (C) WOOD SHOVEL (269): emits front/back + rim verts; UV inside sprite.
 * (D) DIRT block (3): emits 36 verts (unit cube); UV inside dirt sprite;
 *     extents after transform are finite and non-degenerate.
 * (E) CAP: max < 36 returns 0 and never overruns out (canary intact).
 * (F) FLINT AND STEEL: generated rim lighting uses ItemLayerModel's opposite
 *     vertex normal at the TNT tape's camera pitch.
 *
 * Build/run: bash game/test_hand.sh
 */
#include "core/types.h"
#include "game/hand.h"
#include "game/item_render.h"
#include "game/block_registry.h"
#include "assets/blockmodels.h"
#include "assets/item_atlas.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int g_fail;
#define TEST_MAX 6156
#define CHECK(cond, msg) do { \
    if (!(cond)) { fprintf(stderr, "FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); g_fail = 1; } \
} while (0)

int main(void) {
    static CrVertex out[TEST_MAX], out2[TEST_MAX];
    const float eps = 1e-4f;

    /* ---- (A) empty ---- */
    {
        int n = gm_hand_emit_held(0, 0, 0.0f, 0.0f, out, TEST_MAX);
        CHECK(n == 0, "empty item emits 0 verts");
    }

    /* ---- (F) flint-and-steel generated-rim normal ---- */
    {
        gm_hand_set_env(NULL, 15.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                        1.0f, 0.0f, 15.0f);
        int n = gm_hand_emit_held(259, 0, 0.0f, 0.0f, out, TEST_MAX);
        CHECK(n > 12, "flint and steel emits generated-item rims");
        CHECK(fabsf(out[12].ao - 1.0f) < eps,
              "first flint-and-steel rim uses ItemLayerModel opposite normal");
        gm_hand_set_env(NULL, 15.0f, 0.0f, 1.0f, 1.0f, 1.0f,
                        1.0f, 0.0f, 0.0f);
    }

    /* ---- (B) stick ---- */
    {
        CHECK(!gm_item_drop_uses_block_atlas(280, 0), "stick is flat item");
        int n = gm_hand_emit_held(280, 0, 0.0f, 0.0f, out, TEST_MAX);
        CHECK(n > 12 && n % 6 == 0, "stick emits front/back and rim verts");
        int si = gm_item_sprite_index(280);
        const CrItemSprite *s = &CR_ITEM_SPRITES[si];
        float u0 = (float)s->x0 / (float)CR_ITEM_ATLAS_W;
        float v0 = (float)s->y0 / (float)CR_ITEM_ATLAS_H;
        float u1 = (float)s->x1 / (float)CR_ITEM_ATLAS_W;
        float v1 = (float)s->y1 / (float)CR_ITEM_ATLAS_H;
        float minx = 1e9f, maxx = -1e9f, miny = 1e9f, maxy = -1e9f, minz = 1e9f, maxz = -1e9f;
        for (int i = 0; i < n; ++i) {
            CHECK(out[i].uv.x >= u0 - eps && out[i].uv.x <= u1 + eps, "stick u in sprite");
            CHECK(out[i].uv.y >= v0 - eps && out[i].uv.y <= v1 + eps, "stick v in sprite");
            if (out[i].pos.x < minx) minx = out[i].pos.x;
            if (out[i].pos.x > maxx) maxx = out[i].pos.x;
            if (out[i].pos.y < miny) miny = out[i].pos.y;
            if (out[i].pos.y > maxy) maxy = out[i].pos.y;
            if (out[i].pos.z < minz) minz = out[i].pos.z;
            if (out[i].pos.z > maxz) maxz = out[i].pos.z;
        }
        /* firstperson_righthand Ry=-90 leaves the plate nearly edge-on in X
         * (thickness ~1/16 * scale); major extent is Y/Z. */
        CHECK(maxx - minx > 0.01f, "stick has x thickness");
        CHECK(maxy - miny > 0.2f, "stick has y extent");
        CHECK(maxz - minz > 0.2f, "stick has z extent");
        /* rest pose sits in the lower-right viewmodel region (x>0, z<0) */
        CHECK(minx > 0.0f, "stick rest x > 0 (right hand)");
        CHECK(maxz < 0.0f, "stick rest in front of eye (neg z)");

        int n2 = gm_hand_emit_held(280, 0, 0.5f, 0.0f, out2, TEST_MAX);
        CHECK(n2 == n, "stick swing preserves topology");
        float d = 0.0f;
        for (int i = 0; i < n; ++i) {
            float dx = out2[i].pos.x - out[i].pos.x;
            float dy = out2[i].pos.y - out[i].pos.y;
            float dz = out2[i].pos.z - out[i].pos.z;
            d += dx*dx + dy*dy + dz*dz;
        }
        CHECK(d > 1e-4f, "swing moves held-item verts");
    }

    /* ---- (C) wood shovel ---- */
    {
        int n = gm_hand_emit_held(269, 0, 0.0f, 0.0f, out, TEST_MAX);
        CHECK(n > 12 && n % 6 == 0, "wood shovel emits front/back and rim verts");
        int si = gm_item_sprite_index(269);
        CHECK(CR_ITEM_SPRITES[si].id == 269, "wood shovel has atlas entry");
        const CrItemSprite *s = &CR_ITEM_SPRITES[si];
        float u0 = (float)s->x0 / (float)CR_ITEM_ATLAS_W;
        float v0 = (float)s->y0 / (float)CR_ITEM_ATLAS_H;
        float u1 = (float)s->x1 / (float)CR_ITEM_ATLAS_W;
        float v1 = (float)s->y1 / (float)CR_ITEM_ATLAS_H;
        for (int i = 0; i < n; ++i) {
            CHECK(out[i].uv.x >= u0 - eps && out[i].uv.x <= u1 + eps, "shovel u in sprite");
            CHECK(out[i].uv.y >= v0 - eps && out[i].uv.y <= v1 + eps, "shovel v in sprite");
        }
    }

    /* ---- (D) dirt block ---- */
    {
        CHECK(gm_item_drop_uses_block_atlas(3, 0), "dirt is block");
        int n = gm_hand_emit_held(3, 0, 0.0f, 0.0f, out, TEST_MAX);
        CHECK(n == 36, "dirt cube emits 36 verts");
        int key = gm_state_to_model_key(gm_pack_state(3, 0));
        const BmBlock *m = bm_block(key);
        float u0, v0, u1, v1;
        bm_sprite_uv(m->face[0].sprite, &u0, &v0, &u1, &v1);
        float minx = 1e9f, maxx = -1e9f;
        for (int i = 0; i < n; ++i) {
            CHECK(out[i].uv.x >= u0 - eps && out[i].uv.x <= u1 + eps, "dirt u in sprite");
            CHECK(out[i].uv.y >= v0 - eps && out[i].uv.y <= v1 + eps, "dirt v in sprite");
            if (out[i].pos.x < minx) minx = out[i].pos.x;
            if (out[i].pos.x > maxx) maxx = out[i].pos.x;
        }
        CHECK(maxx - minx > 0.05f, "dirt has non-degenerate extent");
        CHECK(minx > 0.0f, "dirt rest x > 0 (right hand)");
    }

    /* ---- (E) cap ---- */
    {
        CrVertex canary;
        memset(&canary, 0xAB, sizeof canary);
        out[0] = canary;
        int n = gm_hand_emit_held(280, 0, 0.0f, 0.0f, out, 12);
        CHECK(n == 0, "max<36 emits nothing");
        CHECK(memcmp(&out[0], &canary, sizeof canary) == 0, "cap does not overwrite out");
    }

    /* ---- (F) equip lower shifts held item down (y decreases in eye space) ---- */
    {
        int n0 = gm_hand_emit_held(280, 0, 0.0f, 0.0f, out, TEST_MAX);
        int n1 = gm_hand_emit_held(280, 0, 0.0f, 1.0f, out2, TEST_MAX);
        CHECK(n0 == n1 && n0 > 0, "equip preserves topology");
        float y0 = 0.0f, y1 = 0.0f;
        for (int i = 0; i < n0; ++i) { y0 += out[i].pos.y; y1 += out2[i].pos.y; }
        y0 /= (float)n0; y1 /= (float)n1;
        CHECK(y1 < y0 - 0.2f, "equip=1 lowers held item");
    }

    /* ---- (G) drawn-bow offset differs from idle bow ---- */
    {
        gm_hand_set_bow_pull(0);
        int n0 = gm_hand_emit_held(261, 0, 0.0f, 0.0f, out, TEST_MAX);
        gm_hand_set_bow_pull(20);
        int n1 = gm_hand_emit_held(261, 0, 0.0f, 0.0f, out2, TEST_MAX);
        gm_hand_set_bow_pull(0);
        CHECK(n0 > 12 && n1 > 12, "bow emits verts idle and drawn");
        float d = 0.0f;
        int n = n0 < n1 ? n0 : n1;
        for (int i = 0; i < n; ++i) {
            float dx = out2[i].pos.x - out[i].pos.x;
            float dy = out2[i].pos.y - out[i].pos.y;
            float dz = out2[i].pos.z - out[i].pos.z;
            d += dx*dx + dy*dy + dz*dz;
        }
        CHECK(d > 0.01f, "drawn bow pose moves verts vs idle");
    }

    /* ---- (H) eat use pose lifts/offsets vs idle bread ---- */
    {
        gm_hand_set_use(0, 0, 0);
        int n0 = gm_hand_emit_held(297, 0, 0.0f, 0.0f, out, TEST_MAX); /* bread */
        gm_hand_set_use(1, 16, 32); /* mid-eat, max 32 ticks for bread */
        int n1 = gm_hand_emit_held(297, 0, 0.0f, 0.0f, out2, TEST_MAX);
        gm_hand_set_use(0, 0, 0);
        CHECK(n0 > 0 && n1 == n0, "eat preserves topology");
        float d = 0.0f;
        for (int i = 0; i < n0; ++i) {
            float dx = out2[i].pos.x - out[i].pos.x;
            float dy = out2[i].pos.y - out[i].pos.y;
            float dz = out2[i].pos.z - out[i].pos.z;
            d += dx*dx + dy*dy + dz*dz;
        }
        CHECK(d > 0.01f, "eat pose offsets held item");
    }

    /* ---- (I) block-use ignores swing (vs mid-swing idle path) ---- */
    {
        /* Vanilla BLOCK use (shield EnumAction.BLOCK): transformSideFirstPerson
         * only. Force use_action=2 here to test the pose; live item resolution
         * only arms BLOCK for shield 442 (swords are NONE in 1.11.2). With
         * swing=0 idle transformFirstPerson is net identity, so block == idle.
         * Compare against a mid-swing pose instead. */
        gm_hand_set_use(0, 0, 0);
        int n_swing = gm_hand_emit_held(267, 0, 0.5f, 0.0f, out, TEST_MAX);
        gm_hand_set_use(2, 0, 0);
        int n_block = gm_hand_emit_held(267, 0, 0.5f, 0.0f, out2, TEST_MAX);
        gm_hand_set_use(0, 0, 0);
        CHECK(n_swing > 0 && n_block == n_swing, "block-use topology");
        float d = 0.0f;
        for (int i = 0; i < n_swing; ++i) {
            float dx = out2[i].pos.x - out[i].pos.x;
            float dy = out2[i].pos.y - out[i].pos.y;
            float dz = out2[i].pos.z - out[i].pos.z;
            d += dx*dx + dy*dy + dz*dz;
        }
        CHECK(d > 1e-4f, "block-use ignores mid-swing transform");
    }

    /* ---- (J) edge/rim verts: stick has many more than 12 (front+back) ---- */
    {
        int n = gm_hand_emit_held(280, 0, 0.0f, 0.0f, out, TEST_MAX);
        CHECK(n >= 12 + 6 * 4, "stick has rim quads beyond front/back");
    }

    /* ---- (K) shield 442: ModelShield plate+handle (72 verts), block pose ---- */
    {
        gm_hand_set_use(0, 0, 0);
        int n_idle = gm_hand_emit_held(442, 0, 0.0f, 0.0f, out, TEST_MAX);
        gm_hand_set_use(2, 72000, 72000);
        int n_block = gm_hand_emit_held(442, 0, 0.0f, 0.0f, out2, TEST_MAX);
        gm_hand_set_use(0, 0, 0);
        CHECK(n_idle == 72 && n_block == 72, "shield emits 72 verts (2 boxes x 6 faces)");
        float d = 0.0f;
        for (int i = 0; i < n_idle; ++i) {
            float dx = out2[i].pos.x - out[i].pos.x;
            float dy = out2[i].pos.y - out[i].pos.y;
            float dz = out2[i].pos.z - out[i].pos.z;
            d += dx*dx + dy*dy + dz*dz;
        }
        CHECK(d > 0.01f, "shield blocking display differs from idle");
        /* blocking firstperson sits in lower-right viewmodel region */
        float mx = 0.0f, my = 0.0f, mz = 0.0f;
        for (int i = 0; i < n_block; ++i) {
            mx += out2[i].pos.x; my += out2[i].pos.y; mz += out2[i].pos.z;
        }
        mx /= (float)n_block; my /= (float)n_block; mz /= (float)n_block;
        CHECK(mx > 0.0f, "shield block mean x > 0 (right hand)");
        CHECK(mz < 0.0f, "shield block mean z < 0 (in front of eye)");
    }

    if (g_fail) {
        fprintf(stderr, "test_hand: FAILED\n");
        return 1;
    }
    printf("test_hand: PASS\n");
    return 0;
}
