/* game/item_render.c - dropped-item (EntityItem) models + GUI block icons.
 *
 * Vanilla references (java/oracle-src/net/minecraft/client/renderer/entity/
 * RenderEntityItem.java + RenderItem + models/block/block.json display.ground /
 * display.gui + ItemModelGenerator):
 *
 * ENTITY DROP:
 *   bob y = sin(age/10 + hoverStart)*0.1 + 0.1
 *   spin  = age/20 + hoverStart (radians about Y)
 *   translate y += 0.25 * ground_scale.y  (block ground scale 0.25; item 0.5)
 *   block cubes: GROUND scale 0.25 of a full block
 *   flat items: GROUND scale 0.5 of a 16x16 sprite with 1/16 extrusion
 *               (ItemModelGenerator z 7.5..8.5 in 0..16 model units)
 *
 * GUI ICON (hotbar / container slots):
 *   block/block.json gui transform: rotation [30, 225, 0], scale 0.625
 *   setupGuiTransform: *16, y-flip, center of 16x16 slot; standard item lighting
 *   approximated here by the classic per-face shade factors (UP 1 / NS 0.8 / EW 0.6)
 *   on a software-rasterized isometric mini-cube (top + sides via full z-buffered cube).
 *
 * WINDING: the axis-aligned FACES template below is CCW-seen-from-outside
 * (world/mesh_mc.c convention); a pure rotation about Y preserves handedness.
 */
#include "game/item_render.h"
#include "assets/blockmodels.h"
#include "assets/atlas_gen.h"
#include "assets/item_atlas.h"
#include "assets/gui_atlas.h"
#include "assets/mob_atlas.h"
#include "game/block_registry.h"
#include "world/lightmap.h"

#include <math.h>
#include <string.h>

#define IR_CUBE_VERTS 36  /* 6 faces * 2 tris * 3 verts */
#define IR_FLAT_VERTS 36  /* extruded flat item: thin box, 6 faces */
#define IR_CUBE_HALF  0.125f     /* 0.25-scale block half-extent */
#define IR_FLAT_HALF  0.25f      /* 0.5-block sprite half-extent (GROUND scale 0.5) */
#define IR_FLAT_THICK 0.015625f  /* half of 0.5*(1/16) extrusion after GROUND scale */
#define IR_CUBE_GROUND_SY 0.25f  /* block ground scale.y */
#define IR_FLAT_GROUND_SY 0.50f  /* item  ground scale.y */

/* Fallback item sprite for stacks the atlas does not carry (index of iron
 * ingot: a neutral, recognizable slab). */
#define IR_FALLBACK_NAME "iron_ingot"

/* mesh_mc FACES template: axis-aligned unit-cube corners, CCW from outside,
 * with the MC directional face shade. Index order == BM_DOWN..BM_EAST. */
static const struct { float shade; int c[4][3]; } IR_FACES[6] = {
    { 0.5f, { {0,0,0},{1,0,0},{1,0,1},{0,0,1} } },  /* BM_DOWN  */
    { 1.0f, { {0,1,0},{0,1,1},{1,1,1},{1,1,0} } },  /* BM_UP    */
    { 0.8f, { {0,0,0},{0,1,0},{1,1,0},{1,0,0} } },  /* BM_NORTH */
    { 0.8f, { {0,0,1},{1,0,1},{1,1,1},{0,1,1} } },  /* BM_SOUTH */
    { 0.6f, { {0,0,0},{0,0,1},{0,1,1},{0,1,0} } },  /* BM_WEST  */
    { 0.6f, { {1,0,1},{1,0,0},{1,1,0},{1,1,1} } },  /* BM_EAST  */
};
static const float IR_CUV[4][2] = { {0,1}, {1,1}, {1,0}, {0,0} };
static const int   IR_TRI[6] = { 0, 1, 2, 0, 2, 3 };

/* hoverStart stand-in: EntityItem stores a per-instance random in [0, 2π). We
 * have no instance id on GmEntityView, so hash item_id/meta for a stable phase
 * (de-syncs multi-drop piles without needing per-entity state). */
static float ir_hover(int item_id, int item_meta) {
    unsigned h = (unsigned)item_id * 374761393u + (unsigned)item_meta * 668265263u;
    return (float)(h & 0xFFFFu) * (6.2831855f / 65536.0f);
}
/* vanilla: sin((age + pt)/10 + hoverStart)*0.1 + 0.1 */
static float ir_bob(int age, float hover) {
    return sinf((float)age / 10.0f + hover) * 0.1f + 0.1f;
}
/* vanilla: (age/20 + hoverStart) radians about Y */
static float ir_spin(int age, float hover) {
    return (float)age / 20.0f + hover;
}

/* Fixed plains-ish biome tint for tinted faces of dropped blocks (the entity
 * pass has no per-position biome sampling; vanilla uses the item's own tint). */
static CrRgba ir_tint(int tint_class) {
    switch (tint_class) {
        case BM_TINT_GRASS:         return (CrRgba){145, 189,  89, 255};
        case BM_TINT_FOLIAGE:       return (CrRgba){119, 171,  47, 255};
        case BM_TINT_FOLIAGE_PINE:  return (CrRgba){ 97, 153,  97, 255};
        case BM_TINT_FOLIAGE_BIRCH: return (CrRgba){128, 167,  85, 255};
        default:                    return (CrRgba){255, 255, 255, 255};
    }
}

/* Model-key lookup for a dropped stack; NULL when the stack is not a block
 * with a usable model (renders via the item atlas instead). */
static const BmBlock *ir_block_model(int item_id, int item_meta) {
    if (item_id <= 0 || item_id > 255) return 0;      /* item-range id */
    int key = gm_state_to_model_key(gm_pack_state(item_id, item_meta));
    if (key == GM_MODEL_FALLBACK || key == 0) return 0;
    const BmBlock *m = bm_block(key);
    if (!m || m->is_air) return 0;
    return m;
}

int gm_item_drop_uses_block_atlas(int item_id, int item_meta) {
    return ir_block_model(item_id, item_meta) != 0;
}

int gm_item_sprite_index(int item_id) {
    for (int i = 0; i < CR_ITEM_SPRITE_COUNT; ++i)
        if (CR_ITEM_SPRITES[i].id == item_id) return i;
    for (int i = 0; i < CR_ITEM_SPRITE_COUNT; ++i)
        if (!strcmp(CR_ITEM_SPRITES[i].name, IR_FALLBACK_NAME)) return i;
    return 0;
}

/* Rotate (lx,lz) about Y by spin; place at feet + bob + 0.25*ground_sy + ly. */
static void ir_place(float fx, float fy, float fz, float bob, float ground_sy,
                     float spin, float lx, float ly, float lz,
                     float *ox, float *oy, float *oz) {
    float cs = cosf(spin), sn = sinf(spin);
    float cy = fy + bob + 0.25f * ground_sy;
    *ox = fx + lx * cs + lz * sn;
    *oy = cy + ly;
    *oz = fz - lx * sn + lz * cs;
}

/* one miniature block cube: GROUND scale 0.25, spun about Y, bobbing. */
static int ir_emit_cube(const BmBlock *m, float fx, float fy, float fz,
                        int age, float hover, CrVertex *out) {
    float bob = ir_bob(age, hover), spin = ir_spin(age, hover);
    int written = 0;
    for (int f = 0; f < 6; ++f) {
        float u0, v0, u1, v1;
        bm_sprite_uv(m->face[f].sprite, &u0, &v0, &u1, &v1);
        CrRgba tint = ir_tint(m->face[f].tint);
        CrVertex quad[4];
        for (int c = 0; c < 4; ++c) {
            float lx = IR_FACES[f].c[c][0] ? IR_CUBE_HALF : -IR_CUBE_HALF;
            float ly = IR_FACES[f].c[c][1] ? IR_CUBE_HALF : -IR_CUBE_HALF;
            float lz = IR_FACES[f].c[c][2] ? IR_CUBE_HALF : -IR_CUBE_HALF;
            CrVertex vtx;
            ir_place(fx, fy, fz, bob, IR_CUBE_GROUND_SY, spin, lx, ly, lz,
                     &vtx.pos.x, &vtx.pos.y, &vtx.pos.z);
            vtx.uv.x = u0 + IR_CUV[c][0] * (u1 - u0);
            vtx.uv.y = v0 + IR_CUV[c][1] * (v1 - v0);
            vtx.light = IR_FACES[f].shade;
            vtx.tint = tint;
            vtx.ao = 1.0f;
            vtx.blk = 0.0f;
            quad[c] = vtx;
        }
        for (int k = 0; k < 6; ++k) out[written++] = quad[IR_TRI[k]];
    }
    return written;
}

/* one extruded flat item (ItemModelGenerator 1/16 slab): thin box of the sprite,
 * GROUND scale 0.5, spun about Y, bobbing. Front/back carry the full sprite;
 * rim faces reuse edge texels so the drop reads as a chunky 3D token. */
static int ir_emit_flat(float fx, float fy, float fz, int age, float hover,
                        float u0, float v0, float u1, float v1,
                        CrRgba tint, CrVertex *out) {
    float bob = ir_bob(age, hover), spin = ir_spin(age, hover);
    int written = 0;
    /* local half-extents: X/Y = 0.25 (0.5 block), Z = 0.015625 (half of 1/16*0.5) */
    const float hx = IR_FLAT_HALF, hy = IR_FLAT_HALF, hz = IR_FLAT_THICK;
    /* face -> (axis-aligned box corners in local space) using IR_FACES unit cube */
    for (int f = 0; f < 6; ++f) {
        CrVertex quad[4];
        for (int c = 0; c < 4; ++c) {
            float lx = IR_FACES[f].c[c][0] ? hx : -hx;
            float ly = IR_FACES[f].c[c][1] ? hy : -hy;
            float lz = IR_FACES[f].c[c][2] ? hz : -hz;
            CrVertex vtx;
            ir_place(fx, fy, fz, bob, IR_FLAT_GROUND_SY, spin, lx, ly, lz,
                     &vtx.pos.x, &vtx.pos.y, &vtx.pos.z);
            /* UV: front/back (N/S, indices 2/3) use full sprite; other faces
             * sample a strip so rims are not a stretch of the full image. */
            float uu = IR_CUV[c][0], vv = IR_CUV[c][1];
            if (f == BM_DOWN || f == BM_UP) {
                /* top/bottom: U along X, V along Z (thin) */
                uu = IR_FACES[f].c[c][0] ? 1.0f : 0.0f;
                vv = IR_FACES[f].c[c][2] ? 1.0f : 0.0f;
            } else if (f == BM_WEST || f == BM_EAST) {
                uu = IR_FACES[f].c[c][2] ? 1.0f : 0.0f;
                vv = IR_FACES[f].c[c][1] ? 0.0f : 1.0f;
            }
            vtx.uv.x = u0 + uu * (u1 - u0);
            vtx.uv.y = v0 + vv * (v1 - v0);
            /* front/back at full brightness; rims slightly darker (readable volume) */
            vtx.light = (f == BM_NORTH || f == BM_SOUTH) ? 1.0f : IR_FACES[f].shade;
            vtx.tint = tint;
            vtx.ao = 1.0f;
            vtx.blk = 0.0f;
            quad[c] = vtx;
        }
        for (int k = 0; k < 6; ++k) out[written++] = quad[IR_TRI[k]];
    }
    return written;
}

int gm_items_emit(const GmEntityView *ents, int n, CrVertex *out, int max) {
    int written = 0;
    for (int e = 0; e < n; ++e) {
        if (ents[e].type != GM_VIEW_ITEM) continue;
        const BmBlock *m = ir_block_model(ents[e].item_id, ents[e].item_meta);
        if (!m) continue;                              /* item-atlas pass */
        float hover = ents[e].has_hover_start ? ents[e].hover_start
                                               : ir_hover(ents[e].item_id, ents[e].item_meta);
        if (m->kind == BM_KIND_CROSS) {
            /* plants drop as a flat extruded sprite of their block texture
             * (vanilla flat block items). */
            if (written + IR_FLAT_VERTS > max) break;
            float u0, v0, u1, v1;
            bm_sprite_uv(m->face[BM_SOUTH].sprite, &u0, &v0, &u1, &v1);
            written += ir_emit_flat(ents[e].x, ents[e].y, ents[e].z,
                                    ents[e].age, hover, u0, v0, u1, v1,
                                    ir_tint(m->face[BM_SOUTH].tint),
                                    out + written);
        } else {
            if (written + IR_CUBE_VERTS > max) break;
            written += ir_emit_cube(m, ents[e].x, ents[e].y, ents[e].z,
                                    ents[e].age, hover, out + written);
        }
    }
    return written;
}

int gm_items_emit_flat(const GmEntityView *ents, int n, CrVertex *out, int max) {
    const float aw = (float)CR_ITEM_ATLAS_W, ah = (float)CR_ITEM_ATLAS_H;
    int written = 0;
    for (int e = 0; e < n; ++e) {
        if (ents[e].type != GM_VIEW_ITEM) continue;
        if (ir_block_model(ents[e].item_id, ents[e].item_meta)) continue;
        if (written + IR_FLAT_VERTS > max) break;
        const CrItemSprite *s = &CR_ITEM_SPRITES[gm_item_sprite_index(ents[e].item_id)];
        float hover = ents[e].has_hover_start ? ents[e].hover_start
                                               : ir_hover(ents[e].item_id, ents[e].item_meta);
        written += ir_emit_flat(ents[e].x, ents[e].y, ents[e].z, ents[e].age, hover,
                                (float)s->x0 / aw, (float)s->y0 / ah,
                                (float)s->x1 / aw, (float)s->y1 / ah,
                                (CrRgba){255, 255, 255, 255}, out + written);
    }
    return written;
}

/* ---------------------------------------------------------------------------
 * Held items (LayerHeldItem): mobs that always carry a vanilla loadout render
 * the item sprite in the right hand. Pigman -> gold sword, skeleton/stray ->
 * bow. The full GL chain is reproduced in model-texel space:
 *   ModelBiped.postRenderArm (arm rp + pose rotation)
 *   rotate(-90, X), rotate(180, Y), translate(1/16, 0.125, -0.625) blocks
 *   item json display thirdperson_righthand (T, then Rx Ry Rz, then S)
 *   translate(-0.5,-0.5,-0.5) blocks (RenderItem model recentre)
 * then the same model->world map emit_box uses. The item is the generated
 * 16x16 sprite as front+back quads (1-texel extrusion rims omitted). */

#define ER_PI_F 3.14159265358979323846f
#define IR_D2R  0.017453292519943295f

typedef struct { float r[3][3]; float t[3]; } IrMat;

static void ir_mat_identity(IrMat *m) {
    memset(m, 0, sizeof *m);
    m->r[0][0] = m->r[1][1] = m->r[2][2] = 1.0f;
}
/* M = M * Op (GL post-multiply: Op runs first on vertices). */
static void ir_mat_mul(IrMat *m, const IrMat *op) {
    IrMat o = *m;
    for (int i = 0; i < 3; ++i) {
        m->t[i] = o.t[i];
        for (int j = 0; j < 3; ++j) {
            m->t[i] += o.r[i][j] * op->t[j];
            float s = 0;
            for (int k = 0; k < 3; ++k) s += o.r[i][k] * op->r[k][j];
            m->r[i][j] = s;
        }
    }
}
static void ir_mat_translate(IrMat *m, float x, float y, float z) {
    IrMat op; ir_mat_identity(&op);
    op.t[0] = x; op.t[1] = y; op.t[2] = z;
    ir_mat_mul(m, &op);
}
static void ir_mat_scale(IrMat *m, float s) {
    IrMat op; ir_mat_identity(&op);
    op.r[0][0] = op.r[1][1] = op.r[2][2] = s;
    ir_mat_mul(m, &op);
}
static void ir_mat_rot(IrMat *m, int axis, float rad) {
    IrMat op; ir_mat_identity(&op);
    float c = cosf(rad), s = sinf(rad);
    int a = (axis + 1) % 3, b = (axis + 2) % 3;
    op.r[a][a] = c; op.r[a][b] = -s;
    op.r[b][a] = s; op.r[b][b] = c;
    ir_mat_mul(m, &op);
}
static void ir_mat_apply(const IrMat *m, float x, float y, float z, float *o) {
    for (int i = 0; i < 3; ++i)
        o[i] = m->r[i][0] * x + m->r[i][1] * y + m->r[i][2] * z + m->t[i];
}

/* item json display.thirdperson_righthand (1.11.2 jar models/item/...). */
typedef struct { float rot[3], trans[3], scale; } IrDisplay;
static const IrDisplay IR_DISP_HANDHELD = {   /* item/handheld.json (sword) */
    { 0, -90, 55 }, { 0, 4.0f, 0.5f }, 0.85f
};
static const IrDisplay IR_DISP_BOW = {        /* item/bow.json */
    { -80, 260, -40 }, { -1.0f, -2.0f, 2.5f }, 0.9f
};

/* Vanilla always-held loadout per view (EntityPigZombie ctor gold sword,
 * AbstractSkeleton setCombatTask bow). Returns item id or 0. */
static int ir_held_item(const GmEntityView *v, const IrDisplay **disp,
                        float *arm_ax) {
    if (v->tape_pose && (v->flags & 4)) return 0;       /* invisible */
    if (v->type == 2 && v->skin == CR_MOB_PIGMAN + 1) { /* ER_TYPE_ZOMBIE */
        *disp = &IR_DISP_HANDHELD; *arm_ax = -ER_PI_F / 2.25f;  /* zombie arms */
        return 283;                                     /* gold sword */
    }
    if (v->type == 3) {                                 /* ER_TYPE_SKELETON */
        *disp = &IR_DISP_BOW; *arm_ax = 0.0f;           /* arm down (idle) */
        return 261;                                     /* bow */
    }
    return 0;
}

int gm_held_items_emit(const GmEntityView *ents, int n, CrVertex *out, int max) {
    const float aw = (float)CR_ITEM_ATLAS_W, ah = (float)CR_ITEM_ATLAS_H;
    int written = 0;
    for (int e = 0; e < n; ++e) {
        const IrDisplay *disp; float arm_ax;
        int id = ir_held_item(&ents[e], &disp, &arm_ax);
        if (!id) continue;
        if (written + 12 > max) break;

        IrMat m; ir_mat_identity(&m);
        ir_mat_translate(&m, -5.0f, 2.0f, 0.0f);        /* right arm rp */
        ir_mat_rot(&m, 0, arm_ax);                      /* arm pose (X only) */
        ir_mat_rot(&m, 0, -90.0f * IR_D2R);
        ir_mat_rot(&m, 1, 180.0f * IR_D2R);
        ir_mat_translate(&m, 1.0f, 2.0f, -10.0f);       /* (1/16,.125,-.625)*16 */
        ir_mat_translate(&m, disp->trans[0], disp->trans[1], disp->trans[2]);
        ir_mat_rot(&m, 0, disp->rot[0] * IR_D2R);
        ir_mat_rot(&m, 1, disp->rot[1] * IR_D2R);
        ir_mat_rot(&m, 2, disp->rot[2] * IR_D2R);
        ir_mat_scale(&m, disp->scale);
        ir_mat_translate(&m, -8.0f, -8.0f, -8.0f);      /* recentre [0,16] */

        /* lighting: this pass has no lightmap LUT; fold the exact
         * updateLightmap color into the tint like the legacy entity path. */
        CrRgba tint = { 255, 255, 255, 255 };
        if (ents[e].lm_lit == 1) {
            CrLightmapRgb c3 = cr_lightmap_rgb(0, (int)ents[e].lm_light,
                                               (int)ents[e].lm_blk, 1.0f, 0, 0);
            tint.r = (u8)(255.0f * c3.r + 0.5f);
            tint.g = (u8)(255.0f * c3.g + 0.5f);
            tint.b = (u8)(255.0f * c3.b + 0.5f);
        } else if (ents[e].lm_lit == 2) {
            tint.r = (u8)(255.0f * ents[e].lm_mul_r + 0.5f);
            tint.g = (u8)(255.0f * ents[e].lm_mul_g + 0.5f);
            tint.b = (u8)(255.0f * ents[e].lm_mul_b + 0.5f);
        }

        const CrItemSprite *s = &CR_ITEM_SPRITES[gm_item_sprite_index(id)];
        float yr = (180.0f - ents[e].yaw) * IR_D2R;     /* applyRotations */
        float cs = cosf(yr), sn = sinf(yr);

        /* front (z=8.5) then back (z=7.5, reversed winding). */
        static const float CORN[4][2] = { {0,0},{16,0},{16,16},{0,16} };
        for (int face = 0; face < 2; ++face) {
            float z = face ? 7.5f : 8.5f;
            CrVertex quad[4];
            for (int c = 0; c < 4; ++c) {
                int ci = face ? 3 - c : c;
                float p[3];
                ir_mat_apply(&m, CORN[ci][0], CORN[ci][1], z, p);
                float wx = -p[0] / 16.0f;
                float wy = (24.0f - p[1]) / 16.0f;
                float wz = p[2] / 16.0f;
                CrVertex vtx;
                vtx.pos.x = ents[e].x + wx * cs + wz * sn;
                vtx.pos.y = ents[e].y + wy;
                vtx.pos.z = ents[e].z - wx * sn + wz * cs;
                vtx.uv.x = ((float)s->x0 + CORN[ci][0]) / aw;
                vtx.uv.y = ((float)s->y0 + (16.0f - CORN[ci][1])) / ah;
                vtx.light = 1.0f;
                vtx.blk = 0.0f;
                vtx.tint = tint;
                vtx.ao = 1.0f;
                quad[c] = vtx;
            }
            for (int k = 0; k < 6; ++k) out[written++] = quad[IR_TRI[k]];
        }
    }
    return written;
}

/* RenderSnowball (thrown pearls / eyes of ender / snowballs / eggs): the item
 * sprite as a camera-facing quad. GL chain (1.11 has NO outer 0.5 scale):
 * T(pos) Ry(-playerViewY) Rx(playerViewX) Ry(180), then RenderItem GROUND:
 * T(0, 2/16, 0) S(0.5) T(-0.5,-0.5,-0.5) with the 16x16 sprite spanning
 * [0,1]^2 at z=0.5 +- 0.5/16. Front+back quads only (the 1-texel extrusion
 * rims are omitted, same simplification as gm_held_items_emit). view_yaw /
 * view_pitch are the camera's rotationYaw/rotationPitch in degrees. */
int gm_items_emit_billboard(const GmEntityView *ents, int n, float view_yaw,
                            float view_pitch, CrVertex *out, int max) {
    const float aw = (float)CR_ITEM_ATLAS_W, ah = (float)CR_ITEM_ATLAS_H;
    float yr = -view_yaw * IR_D2R, pr = view_pitch * IR_D2R;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    static const float CORN[4][2] = { {0,0},{16,0},{16,16},{0,16} };
    int written = 0;
    for (int e = 0; e < n; ++e) {
        if (ents[e].type != GM_VIEW_BILLBOARD) continue;
        if (written + 12 > max) break;
        const CrItemSprite *s =
            &CR_ITEM_SPRITES[gm_item_sprite_index(ents[e].item_id)];
        CrRgba tint = { 255, 255, 255, 255 };
        if (ents[e].lm_lit == 2) {
            tint.r = (u8)(255.0f * ents[e].lm_mul_r + 0.5f);
            tint.g = (u8)(255.0f * ents[e].lm_mul_g + 0.5f);
            tint.b = (u8)(255.0f * ents[e].lm_mul_b + 0.5f);
        }
        for (int face = 0; face < 2; ++face) {
            float zq = face ? 7.5f : 8.5f;
            CrVertex quad[4];
            for (int c = 0; c < 4; ++c) {
                int ci = face ? 3 - c : c;
                /* GROUND: T(0,2/16,0) S(0.5) T(-0.5,-0.5,-0.5), model px/16 */
                float px = 0.5f * (CORN[ci][0] / 16.0f - 0.5f);
                float py = 0.125f + 0.5f * (CORN[ci][1] / 16.0f - 0.5f);
                float pz = 0.5f * (zq / 16.0f - 0.5f);
                /* Ry(180) */
                px = -px; pz = -pz;
                /* Rx(playerViewX) */
                float ty = py * cp - pz * sp, tz = py * sp + pz * cp;
                py = ty; pz = tz;
                /* Ry(-playerViewY) */
                float tx = px * cy + pz * sy;
                tz = -px * sy + pz * cy;
                px = tx; pz = tz;
                CrVertex vtx;
                vtx.pos.x = ents[e].x + px;
                vtx.pos.y = ents[e].y + py;
                vtx.pos.z = ents[e].z + pz;
                vtx.uv.x = ((float)s->x0 + CORN[ci][0]) / aw;
                vtx.uv.y = ((float)s->y0 + (16.0f - CORN[ci][1])) / ah;
                vtx.light = 1.0f;
                vtx.blk = 0.0f;
                vtx.tint = tint;
                vtx.ao = 1.0f;
                quad[c] = vtx;
            }
            for (int k = 0; k < 6; ++k) out[written++] = quad[IR_TRI[k]];
        }
    }
    return written;
}

CrTexture gm_item_atlas(void) {
    CrTexture t;
    t.w = CR_ITEM_ATLAS_W;
    t.h = CR_ITEM_ATLAS_H;
    t.texels = (const CrRgba *)CR_ITEM_ATLAS_RGBA;
    t.tile = CR_ITEM_ATLAS_TILE;
    t.mip_levels = 0;
    for (int i = 0; i < 15; ++i) { t.mip[i] = 0; t.mipw[i] = 0; t.miph[i] = 0; }
    return t;
}

/* =========================================================================
 * GUI isometric block icons (hotbar / container).
 *
 * Vanilla: RenderItem.renderItemModelIntoGUI + block/block.json gui display
 *   rotation [30, 225, 0], scale 0.625, then *16 with y-flip into the 16x16 slot.
 * Software path: orthographic project the unit cube, z-buffer rasterize all 6
 * faces with per-face shade (UP 1 / NS 0.8 / EW 0.6 / DOWN 0.5), sample terrain
 * (or a single gui-atlas tile for blocks without a model key).
 *
 * Allocate-once 16x16 buffers; nearest-scale blit into the framebuffer.
 * ========================================================================= */

#define ICON_N 16
static CrRgba g_icon_rgba[ICON_N * ICON_N];
static float  g_icon_z[ICON_N * ICON_N];

typedef struct {
    const unsigned char *px; /* R,G,B,A bytes */
    int x0, y0, w, h, stride; /* sprite rect; stride in texels */
} IrIconTex;

static void ir_icon_clear(void) {
    for (int i = 0; i < ICON_N * ICON_N; ++i) {
        g_icon_rgba[i] = (CrRgba){0, 0, 0, 0};
        g_icon_z[i] = -1e30f;
    }
}

/* GUI display: Ry(225) then Rx(30) (GL call order: Rx then Ry => v' = Rx*Ry*v),
 * scale 0.625, *16, y-flip, origin at slot center (8,8). */
static void ir_icon_xform(float x, float y, float z,
                          float *sx, float *sy, float *sz) {
    x -= 0.5f; y -= 0.5f; z -= 0.5f;
    const float cy = -0.70710678f, syy = -0.70710678f; /* cos/sin 225° */
    float x1 = x * cy + z * syy;
    float z1 = -x * syy + z * cy;
    float y1 = y;
    const float cx = 0.86602540f, sxr = 0.5f;          /* cos/sin 30° */
    float y2 = y1 * cx - z1 * sxr;
    float z2 = y1 * sxr + z1 * cx;
    float x2 = x1;
    const float s = 10.0f; /* 0.625 * 16 */
    *sx = 8.0f + s * x2;
    *sy = 8.0f - s * y2;   /* GUI y-flip */
    *sz = z2;
}

static CrRgba ir_icon_sample(const IrIconTex *t, float u, float v,
                             float shade, CrRgba tint) {
    if (u < 0.f) u = 0.f;
    if (u > 1.f) u = 1.f;
    if (v < 0.f) v = 0.f;
    if (v > 1.f) v = 1.f;
    int px = t->x0 + (int)(u * (float)t->w);
    int py = t->y0 + (int)(v * (float)t->h);
    if (px >= t->x0 + t->w) px = t->x0 + t->w - 1;
    if (py >= t->y0 + t->h) py = t->y0 + t->h - 1;
    if (px < t->x0) px = t->x0;
    if (py < t->y0) py = t->y0;
    const unsigned char *p = t->px + ((py * t->stride + px) * 4);
    float r = (p[0] * (tint.r * (1.f / 255.f))) * shade;
    float g = (p[1] * (tint.g * (1.f / 255.f))) * shade;
    float b = (p[2] * (tint.b * (1.f / 255.f))) * shade;
    if (r > 255.f) r = 255.f;
    if (g > 255.f) g = 255.f;
    if (b > 255.f) b = 255.f;
    CrRgba o = { (u8)(r + 0.5f), (u8)(g + 0.5f), (u8)(b + 0.5f), p[3] };
    return o;
}

/* Barycentric triangle fill into the 16x16 icon buffer (z-tested). */
static void ir_icon_tri(float x0, float y0, float z0, float u0, float v0,
                        float x1, float y1, float z1, float u1, float v1,
                        float x2, float y2, float z2, float u2, float v2,
                        const IrIconTex *tex, float shade, CrRgba tint) {
    int minx = (int)floorf(fminf(x0, fminf(x1, x2)));
    int maxx = (int)ceilf (fmaxf(x0, fmaxf(x1, x2)));
    int miny = (int)floorf(fminf(y0, fminf(y1, y2)));
    int maxy = (int)ceilf (fmaxf(y0, fmaxf(y1, y2)));
    if (minx < 0) minx = 0;
    if (maxx > ICON_N - 1) maxx = ICON_N - 1;
    if (miny < 0) miny = 0;
    if (maxy > ICON_N - 1) maxy = ICON_N - 1;
    float area = (x1 - x0) * (y2 - y0) - (x2 - x0) * (y1 - y0);
    if (fabsf(area) < 1e-8f) return;
    float inv = 1.0f / area;
    for (int py = miny; py <= maxy; ++py) {
        for (int px = minx; px <= maxx; ++px) {
            float cx = (float)px + 0.5f, cy = (float)py + 0.5f;
            float w0 = ((x1 - cx) * (y2 - cy) - (x2 - cx) * (y1 - cy)) * inv;
            float w1 = ((x2 - cx) * (y0 - cy) - (x0 - cx) * (y2 - cy)) * inv;
            float w2 = ((x0 - cx) * (y1 - cy) - (x1 - cx) * (y0 - cy)) * inv;
            if (w0 < 0.f || w1 < 0.f || w2 < 0.f) continue;
            float z = w0 * z0 + w1 * z1 + w2 * z2;
            int i = py * ICON_N + px;
            if (z < g_icon_z[i]) continue;   /* farther from camera */
            float u = w0 * u0 + w1 * u1 + w2 * u2;
            float v = w0 * v0 + w1 * v1 + w2 * v2;
            CrRgba c = ir_icon_sample(tex, u, v, shade, tint);
            if (c.a < 16) continue;
            g_icon_z[i] = z;
            g_icon_rgba[i] = c;
        }
    }
}

static void ir_icon_face(const int corners[4][3], const float cuv[4][2],
                         const IrIconTex *tex, float shade, CrRgba tint) {
    float sx[4], sy[4], sz[4];
    for (int c = 0; c < 4; ++c)
        ir_icon_xform((float)corners[c][0], (float)corners[c][1],
                      (float)corners[c][2], &sx[c], &sy[c], &sz[c]);
    /* two tris: 0-1-2 and 0-2-3 */
    ir_icon_tri(sx[0], sy[0], sz[0], cuv[0][0], cuv[0][1],
                sx[1], sy[1], sz[1], cuv[1][0], cuv[1][1],
                sx[2], sy[2], sz[2], cuv[2][0], cuv[2][1],
                tex, shade, tint);
    ir_icon_tri(sx[0], sy[0], sz[0], cuv[0][0], cuv[0][1],
                sx[2], sy[2], sz[2], cuv[2][0], cuv[2][1],
                sx[3], sy[3], sz[3], cuv[3][0], cuv[3][1],
                tex, shade, tint);
}

/* Alpha-composite the finished 16x16 icon into fb at (dx,dy) * scale. */
static void ir_icon_blit(CrFramebuffer *fb, int dx, int dy, int scale) {
    if (!fb || !fb->color || scale < 1) return;
    for (int sy = 0; sy < ICON_N; ++sy) {
        for (int sx = 0; sx < ICON_N; ++sx) {
            CrRgba src = g_icon_rgba[sy * ICON_N + sx];
            if (src.a == 0) continue;
            int px0 = dx + sx * scale, py0 = dy + sy * scale;
            for (int yy = 0; yy < scale; ++yy) {
                for (int xx = 0; xx < scale; ++xx) {
                    int x = px0 + xx, y = py0 + yy;
                    if (x < 0 || y < 0 || x >= fb->w || y >= fb->h) continue;
                    CrRgba *d = &fb->color[y * fb->w + x];
                    if (src.a == 255) { *d = src; continue; }
                    int a = src.a, ia = 255 - a;
                    d->r = (u8)((src.r * a + d->r * ia + 127) / 255);
                    d->g = (u8)((src.g * a + d->g * ia + 127) / 255);
                    d->b = (u8)((src.b * a + d->b * ia + 127) / 255);
                    d->a = (u8)(a + (d->a * ia + 127) / 255);
                }
            }
        }
    }
}

static IrIconTex ir_tex_from_sprite(int sprite) {
    IrIconTex t;
    if (sprite < 0 || sprite >= CR_ATLAS_SPRITE_COUNT) sprite = CR_SPRITE_STONE;
    CrAtlasSprite s = CR_ATLAS_SPRITES[sprite];
    t.px = CR_ATLAS_RGBA;
    t.x0 = s.x0; t.y0 = s.y0;
    t.w = s.x1 - s.x0; t.h = s.y1 - s.y0;
    t.stride = CR_ATLAS_W;
    return t;
}

static int ir_gui_icon_sprite(int item_id) {
    int lo = 0, hi = GUI_ITEM_ICON_COUNT - 1;
    while (lo <= hi) {
        int mid = (lo + hi) / 2;
        if (GUI_ITEM_ICONS[mid].item == item_id) return GUI_ITEM_ICONS[mid].sprite;
        if (GUI_ITEM_ICONS[mid].item < item_id) lo = mid + 1; else hi = mid - 1;
    }
    return -1;
}

int gm_item_draw_block_icon(CrFramebuffer *fb, int item_id, int item_meta,
                            int dx, int dy, int scale) {
    if (item_id <= 0 || item_id > 255) return 0; /* not a block id */
    const BmBlock *m = ir_block_model(item_id, item_meta);
    /* Cross / non-cube models stay flat (torch etc.). */
    if (m && m->kind != BM_KIND_CUBE && m->kind != BM_KIND_SLAB_BOTTOM &&
        m->kind != BM_KIND_SLAB_TOP && m->kind != BM_KIND_STAIRS &&
        m->kind != BM_KIND_CACTUS && m->kind != BM_KIND_SNOW_LAYER)
        return 0;

    ir_icon_clear();

    if (m && m->kind == BM_KIND_CUBE) {
        for (int f = 0; f < 6; ++f) {
            IrIconTex tex = ir_tex_from_sprite(m->face[f].sprite);
            ir_icon_face(IR_FACES[f].c, IR_CUV, &tex, IR_FACES[f].shade,
                         ir_tint(m->face[f].tint));
        }
    } else {
        /* Block id without a cube model (e.g. crafting table 58, furnace 61):
         * single-texture mini-cube from the gui_atlas flat tile so the slot
         * still reads as an isometric block rather than a flat square. */
        int gsi = ir_gui_icon_sprite(item_id);
        if (gsi < 0) return 0;
        const GuiSprite *gs = &GUI_SPRITES[gsi];
        IrIconTex tex;
        tex.px = GUI_RGBA + gs->off;
        tex.x0 = 0; tex.y0 = 0;
        tex.w = gs->w; tex.h = gs->h;
        tex.stride = gs->w;
        CrRgba white = {255, 255, 255, 255};
        for (int f = 0; f < 6; ++f)
            ir_icon_face(IR_FACES[f].c, IR_CUV, &tex, IR_FACES[f].shade, white);
    }

    /* Did we actually draw anything? */
    int any = 0;
    for (int i = 0; i < ICON_N * ICON_N; ++i)
        if (g_icon_rgba[i].a) { any = 1; break; }
    if (!any) return 0;
    if (fb) ir_icon_blit(fb, dx, dy, scale);
    return 1;
}
