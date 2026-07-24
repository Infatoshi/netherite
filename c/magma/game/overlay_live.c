/* game/overlay_live.c - world-touching screen overlays (suffocation).
 *
 * Kept separate from overlay.c so unit tests can link selection/crack/block
 * modulate without the live world stack. Linked only into magma_game /
 * frame_capture (OBJ_GAME). */
#include "game/overlay.h"
#include "game/block_registry.h"
#include "assets/blockmodels.h"

#include <math.h>

/* Block.causesSuffocation ~ material.blocksMovement && isFullCube. Approximate
 * with the model table: full solid cube, not air/liquid/translucent/leaves. */
static int overlay_causes_suffocation(int id, int meta) {
    if (id <= 0) return 0;
    if (id == 8 || id == 9 || id == 10 || id == 11) return 0; /* fluids */
    if (id == 18 || id == 161) return 0;                     /* leaves */
    if (id == 20 || id == 95 || id == 102 || id == 160) return 0; /* glass */
    if (id == 6 || id == 30 || id == 31 || id == 32 || id == 37 || id == 38 ||
        id == 39 || id == 40 || id == 50 || id == 51 || id == 59 || id == 83 ||
        id == 106 || id == 111 || id == 175 || id == 90)
        return 0; /* plants / torch / fire / portal / vine / lily / reed */
    int key = gm_state_to_model_key(gm_pack_state(id, meta & 15));
    if (key == GM_MODEL_FALLBACK) {
        /* Unknown solid: treat opaque-ish full blocks as suffocating. */
        return id > 0 && id < 256;
    }
    const BmBlock *bm = bm_block(key);
    if (!bm || bm->is_air || !bm->is_full_cube) return 0;
    if (bm->layer == CR_LAYER_TRANSLUCENT) return 0;
    return 1;
}

void gm_overlay_block_in_hand_live(CrFramebuffer *fb, const CrTexture *atlas,
                                   const struct GmWorld *w,
                                   const GmPlayerView *pv) {
    /* ItemRenderer.renderOverlays isEntityInsideOpaqueBlock path: sample the
     * 8 eye-box corners; use the last suffocating block's particle/UP face. */
    if (!fb || !fb->color || !atlas || !atlas->texels || !w || !pv) return;
    if (pv->dead) return;
    const float width = 0.6f;
    int found = 0, bid = 0, bmeta = 0;
    for (int i = 0; i < 8; ++i) {
        double d0 = (double)pv->x +
            (double)(((float)((i >> 0) % 2) - 0.5f) * width * 0.8f);
        double d1 = (double)pv->y +
            (double)(((float)((i >> 1) % 2) - 0.5f) * 0.1f);
        double d2 = (double)pv->z +
            (double)(((float)((i >> 2) % 2) - 0.5f) * width * 0.8f);
        int bx = (int)floor(d0);
        int by = (int)floor(d1 + (double)pv->eye_height);
        int bz = (int)floor(d2);
        int id = gm_world_block(w, bx, by, bz);
        int meta = gm_world_meta(w, bx, by, bz);
        if (overlay_causes_suffocation(id, meta)) {
            found = 1;
            bid = id;
            bmeta = meta;
        }
    }
    if (!found) return;
    int key = gm_state_to_model_key(gm_pack_state(bid, bmeta & 15));
    if (key == GM_MODEL_FALLBACK) key = 1; /* stone particle fallback */
    const BmBlock *bm = bm_block(key);
    if (!bm || bm->is_air) return;
    /* BlockModelShapes.getTexture: particle ~= UP face sprite. */
    int sprite = bm->face[BM_UP].sprite;
    float u0, v0, u1, v1;
    bm_sprite_uv(sprite, &u0, &v0, &u1, &v1);
    gm_overlay_block_in_hand(fb, atlas, u0, v0, u1, v1);
}
