/* game/overlay_live.c - world-touching screen overlays (suffocation).
 *
 * Kept separate from overlay.c so unit tests can link selection/crack/block
 * modulate without the live world stack. Linked only into magma_game /
 * frame_capture (OBJ_GAME). */
#include "game/overlay.h"
#include "game/block_registry.h"
#include "game/block_normal_cube_1_11_2.h"
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"

#include <math.h>

/* EnumBlockRenderType.INVISIBLE: ItemRenderer.renderOverlays skips the
 * block-in-hand draw even when causesSuffocation is true (barrier). */
static int overlay_render_invisible(int id) {
    if (id <= 0) return 1;          /* air / BlockAir */
    if (id == 166) return 1;        /* barrier */
    if (id == 217) return 1;        /* structure_void */
    if (id == 119) return 1;        /* end_portal (BlockContainer) */
    if (id == 36) return 1;         /* piston_extension (moving) */
    return 0;
}

/* BlockModelShapes.getTexture: missing-model specials, else particle sprite.
 * Particle for CUBE6 equals any face; for TBS (grass) particle is dirt = DOWN. */
static int overlay_particle_sprite(int id, int meta) {
    /* Missing / builtin model fallbacks from BlockModelShapes.getTexture. */
    if (id == 54 || id == 146) return CR_SPRITE_PLANKS_OAK; /* chest / trapped */
    if (id == 63 || id == 68) return CR_SPRITE_PLANKS_OAK;  /* signs */
    if (id == 176 || id == 177) return CR_SPRITE_PLANKS_OAK; /* banners */
    if (id == 130) return CR_SPRITE_OBSIDIAN;               /* ender chest */
    if (id == 10 || id == 11) return CR_SPRITE_LAVA_STILL;
    if (id == 8 || id == 9) return CR_SPRITE_WATER_STILL;
    if (id == 144) return CR_SPRITE_SOUL_SAND;              /* skull */
    /* barrier / structure_void use item sprites (not block atlas); callers
     * skip via INVISIBLE before texture fetch. */

    int key = gm_state_to_model_key(gm_pack_state(id, meta & 15));
    if (key == GM_MODEL_FALLBACK) return CR_SPRITE_STONE; /* missing model particle */
    const BmBlock *bm = bm_block(key);
    if (!bm || bm->is_air) return CR_SPRITE_STONE;
    if (bm->kind == BM_KIND_CHEST) return CR_SPRITE_PLANKS_OAK;
    /* getParticleTexture: when top/bottom differ (grass/mycelium), particle is
     * the dirt/bottom face; otherwise UP equals the uniform cube particle. */
    int up = bm->face[BM_UP].sprite;
    int dn = bm->face[BM_DOWN].sprite;
    if (dn >= 0 && up >= 0 && dn != up) return dn;
    if (up >= 0) return up;
    for (int f = 0; f < 6; ++f)
        if (bm->face[f].sprite >= 0) return bm->face[f].sprite;
    return CR_SPRITE_STONE;
}

int gm_overlay_block_in_hand_pick(const struct GmWorld *w,
                                  const GmPlayerView *pv,
                                  int *out_id, int *out_meta) {
    /* ItemRenderer.renderOverlays isEntityInsideOpaqueBlock path: sample the
     * 8 eye-box corners; last causesSuffocation block wins; skip INVISIBLE. */
    if (!w || !pv || pv->dead) return 0;
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
        if (gm_block_causes_suffocation_1_11_2(id, meta)) {
            found = 1;
            bid = id;
            bmeta = meta;
        }
    }
    if (!found) return 0;
    if (overlay_render_invisible(bid)) return 0;
    *out_id = bid;
    *out_meta = bmeta;
    return 1;
}

void gm_overlay_block_in_hand_draw(CrFramebuffer *fb, const CrTexture *atlas,
                                   int bid, int bmeta) {
    if (!fb || !fb->color || !atlas || !atlas->texels) return;
    int sprite = overlay_particle_sprite(bid, bmeta);
    float u0, v0, u1, v1;
    bm_sprite_uv(sprite, &u0, &v0, &u1, &v1);
    /* Hand FOV base 70 (getFOVModifier useFOVSetting=false); water scale
     * only applies when eye is in water, which is not causesSuffocation. */
    gm_overlay_block_in_hand(fb, atlas, u0, v0, u1, v1, 70.0f);
}

void gm_overlay_block_in_hand_live(CrFramebuffer *fb, const CrTexture *atlas,
                                   const struct GmWorld *w,
                                   const GmPlayerView *pv) {
    int bid, bmeta;
    if (gm_overlay_block_in_hand_pick(w, pv, &bid, &bmeta))
        gm_overlay_block_in_hand_draw(fb, atlas, bid, bmeta);
}
