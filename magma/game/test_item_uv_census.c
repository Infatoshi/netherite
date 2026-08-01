/* Census every canonical block state routed through the dropped mini-cube.
 * The expected UVs are baked independently with the same FaceBakery kernel
 * used by placed full cubes in world/mesh_mc.c. */
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"
#include "core/types.h"
#include "game/block_registry.h"
#include "game/item_render.h"
#include "renderkernels/rk.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

static const int tri[6] = { 0, 1, 2, 0, 2, 3 };
static const float old_uv[4][2] = { {0,1}, {1,1}, {1,0}, {0,0} };

static const BmBlock drop_furnace = {
    0, 1, CR_LAYER_SOLID, BM_KIND_CUBE, {
        { CR_SPRITE_FURNACE_TOP,       BM_TINT_NONE },
        { CR_SPRITE_FURNACE_TOP,       BM_TINT_NONE },
        { CR_SPRITE_FURNACE_FRONT_OFF, BM_TINT_NONE },
        { CR_SPRITE_FURNACE_SIDE,      BM_TINT_NONE },
        { CR_SPRITE_FURNACE_SIDE,      BM_TINT_NONE },
        { CR_SPRITE_FURNACE_SIDE,      BM_TINT_NONE },
    }
};

static float bits_float(int32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

static int same_float(float a, float b) {
    uint32_t aa, bb;
    memcpy(&aa, &a, sizeof aa);
    memcpy(&bb, &b, sizeof bb);
    return aa == bb;
}

int main(void) {
    const float full_uv[4] = { 0.0f, 0.0f, 16.0f, 16.0f };
    int cube_ids = 0, cube_states = 0, faces = 0, failures = 0;
    int changed_ids[256] = {0};

    for (int id = 1; id <= 255; ++id) {
        int id_states = 0, id_changed = 0;
        for (int meta = 0; meta < 16; ++meta) {
            int key = gm_state_to_model_key(gm_pack_state(id, meta));
            const BmBlock *model = NULL;
            if (key != 0 && key != GM_MODEL_FALLBACK) model = bm_block(key);
            if (!model && id == 61) model = &drop_furnace;
            if (!model || model->is_air || model->kind != BM_KIND_CUBE) continue;

            GmEntityView item;
            memset(&item, 0, sizeof item);
            item.type = GM_VIEW_ITEM;
            item.item_id = id;
            item.item_meta = meta;
            item.has_hover_start = 1;

            CrVertex dropped[36];
            int n = gm_items_emit(&item, 1, dropped, 36);
            if (n != 36) {
                fprintf(stderr, "FAIL id=%d meta=%d: emitted %d vertices\n",
                        id, meta, n);
                ++failures;
                continue;
            }

            ++id_states;
            ++cube_states;
            for (int face = 0; face < 6; ++face) {
                float u0, v0, u1, v1;
                int32_t baked[28];
                bm_sprite_uv(model->face[face].sprite, &u0, &v0, &u1, &v1);
                rk_facebakery_make_quad(0.0f, 0.0f, 0.0f,
                                        16.0f, 16.0f, 16.0f,
                                        face, 0, full_uv, u0, u1, v0, v1,
                                        0, 3, 0.0f, NULL, 0, baked);
                ++faces;
                for (int k = 0; k < 6; ++k) {
                    int corner = tri[k];
                    int out_i = face * 6 + k;
                    float want_u = bits_float(baked[corner * 7 + 4]);
                    float want_v = bits_float(baked[corner * 7 + 5]);
                    if (!same_float(dropped[out_i].uv.x, want_u) ||
                        !same_float(dropped[out_i].uv.y, want_v)) {
                        fprintf(stderr,
                                "FAIL id=%d meta=%d face=%d vertex=%d "
                                "drop=(%.9g,%.9g) baked=(%.9g,%.9g)\n",
                                id, meta, face, k,
                                dropped[out_i].uv.x, dropped[out_i].uv.y,
                                want_u, want_v);
                        ++failures;
                    }
                    float was_u = u0 + old_uv[corner][0] * (u1 - u0);
                    float was_v = v0 + old_uv[corner][1] * (v1 - v0);
                    if (!same_float(was_u, want_u) || !same_float(was_v, want_v))
                        id_changed = 1;
                }
            }
        }
        if (id_states) {
            ++cube_ids;
            changed_ids[id] = id_changed;
            printf("PASS id=%3d cube_states=%2d changed_by_fix=%s\n",
                   id, id_states, id_changed ? "yes" : "no");
        }
    }

    printf("UV census: %s cube_ids=%d cube_states=%d faces=%d\n",
           failures ? "FAIL" : "PASS", cube_ids, cube_states, faces);
    printf("changed ids:");
    for (int id = 1; id <= 255; ++id)
        if (changed_ids[id]) printf(" %d", id);
    putchar('\n');
    return failures ? 1 : 0;
}
