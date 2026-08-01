/* game/item_render.c - dropped-item (EntityItem) models + GUI block icons.
 *
 * Vanilla references (java/oracle-src/net/minecraft/client/renderer/entity/
 * RenderEntityItem.java + RenderItem + models/block/block.json display.ground /
 * display.gui + ItemModelGenerator):
 *
 * ENTITY DROP:
 *   bob y = sin((age+partialTicks)/10 + hoverStart)*0.1 + 0.1
 *   spin  = (age+partialTicks)/20 + hoverStart (radians about Y)
 *   model ground translation: block +3/16 Y; generated item +2/16 Y
 *   translate y += 0.25 * ground_scale.y  (block ground scale 0.25; item 0.5)
 *   block cubes: GROUND scale 0.25 of a full block
 *   flat items: GROUND scale 0.5 of a 16x16 sprite with 1/16 extrusion
 *               (ItemModelGenerator z 7.5..8.5 in 0..16 model units)
 *
 * GUI ICON (hotbar / container slots):
 *   block/block.json gui transform: rotation [30, 225, 0], scale 0.625
 *   setupGuiTransform: *16, y-flip, center of 16x16 slot; standard item lighting
 *   RenderHelper's two directional lights + ambient term are resolved into
 *   fixed per-face shades for the software-rasterized, z-buffered mini-cube.
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
#include "renderkernels/rk.h"
#include "world/lightmap.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

#define IR_CUBE_VERTS 36  /* 6 faces * 2 tris * 3 verts */
#define IR_FLAT_VERTS 36  /* extruded flat item: thin box, 6 faces */
#define IR_CUBE_HALF  0.125f     /* 0.25-scale block half-extent */
#define IR_FLAT_HALF  0.25f      /* 0.5-block sprite half-extent (GROUND scale 0.5) */
#define IR_FLAT_THICK 0.015625f  /* half of 0.5*(1/16) extrusion after GROUND scale */
#define IR_CUBE_GROUND_SY 0.25f  /* block ground scale.y */
#define IR_FLAT_GROUND_SY 0.50f  /* item  ground scale.y */
#define IR_CUBE_GROUND_TY (3.0f / 16.0f) /* block/block.json ground translation.y */
#define IR_FLAT_GROUND_TY (2.0f / 16.0f) /* item/generated.json ground translation.y */

/* Fallback item sprite for stacks the atlas does not carry (index of iron
 * ingot: a neutral, recognizable slab). */
#define IR_FALLBACK_NAME "iron_ingot"

/* Vanilla's furnace BlockItem has a 3D inventory/ground model even though the
 * canonical state bridge deliberately leaves live furnace blocks unsupported.
 * Keep that exception local to EntityItem rendering so held/GUI/world paths do
 * not inherit a new model mapping. The item model's unrotated facing is north. */
static const BmBlock IR_DROP_FURNACE = {
    0, 1, CR_LAYER_SOLID, BM_KIND_CUBE, {
        { CR_SPRITE_FURNACE_TOP,       BM_TINT_NONE }, /* DOWN  */
        { CR_SPRITE_FURNACE_TOP,       BM_TINT_NONE }, /* UP    */
        { CR_SPRITE_FURNACE_FRONT_OFF, BM_TINT_NONE }, /* NORTH */
        { CR_SPRITE_FURNACE_SIDE,      BM_TINT_NONE }, /* SOUTH */
        { CR_SPRITE_FURNACE_SIDE,      BM_TINT_NONE }, /* WEST  */
        { CR_SPRITE_FURNACE_SIDE,      BM_TINT_NONE }, /* EAST  */
    }
};

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
/* FaceBakery's default auto-UV in IR_FACES corner order. UP and NORTH have a
 * different corner rotation from the other four faces; using one shared map
 * rotates their textures by 90 degrees (visible on primed TNT lettering). */
static const float IR_FACE_UV[6][4][2] = {
    { {0,1}, {1,1}, {1,0}, {0,0} }, /* DOWN  */
    { {0,0}, {0,1}, {1,1}, {1,0} }, /* UP    */
    { {1,1}, {1,0}, {0,0}, {0,1} }, /* NORTH */
    { {0,1}, {1,1}, {1,0}, {0,0} }, /* SOUTH */
    { {0,1}, {1,1}, {1,0}, {0,0} }, /* WEST  */
    { {0,1}, {1,1}, {1,0}, {0,0} }, /* EAST  */
};
static const float IR_CUV[4][2] = { {0,1}, {1,1}, {1,0}, {0,0} };
static const int   IR_TRI[6] = { 0, 1, 2, 0, 2, 3 };

static float ir_bits_float(int32_t bits) {
    float value;
    memcpy(&value, &bits, sizeof value);
    return value;
}

/* hoverStart stand-in: EntityItem stores a per-instance random in [0, 2π). We
 * have no instance id on GmEntityView, so hash item_id/meta for a stable phase
 * (de-syncs multi-drop piles without needing per-entity state). */
static float ir_hover(int item_id, int item_meta) {
    unsigned h = (unsigned)item_id * 374761393u + (unsigned)item_meta * 668265263u;
    return (float)(h & 0xFFFFu) * (6.2831855f / 65536.0f);
}
/* Tape/world frame capture renders at partialTicks=1. */
static float ir_bob(int age, float hover) {
    return sinf(((float)age + 1.0f) / 10.0f + hover) * 0.1f + 0.1f;
}
static float ir_spin(int age, float hover) {
    return ((float)age + 1.0f) / 20.0f + hover;
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

/* RenderManager/OpenGlHelper.setLightmapTextureCoords: a dropped stack is lit by
 * the world light at its position, exactly like every other entity. The dropped
 * item passes bind no lightmap LUT (frame_capture.c/game_main.c ish/fsh), so
 * fold the entity's updateLightmap colour into the vertex tint the same way
 * gm_held_items_emit already does. Without this a drop renders at full daylight
 * and reads as an opaque bright cube in shade/at night. */
static CrRgba ir_lm_fold(const GmEntityView *e, CrRgba base) {
    float mr = 1.0f, mg = 1.0f, mb = 1.0f;
    if (e->lm_lit == 1) {
        if (e->lm_mul_r > 0.0f || e->lm_mul_g > 0.0f || e->lm_mul_b > 0.0f) {
            mr = e->lm_mul_r; mg = e->lm_mul_g; mb = e->lm_mul_b;
        } else {
            CrLightmapRgb c3 = cr_lightmap_rgb(0, (int)e->lm_light,
                                               (int)e->lm_blk, 1.0f, 0, 0);
            mr = c3.r; mg = c3.g; mb = c3.b;
        }
    } else if (e->lm_lit == 2) {
        mr = e->lm_mul_r; mg = e->lm_mul_g; mb = e->lm_mul_b;
    }
    base.r = (u8)((float)base.r * mr + 0.5f);
    base.g = (u8)((float)base.g * mg + 0.5f);
    base.b = (u8)((float)base.b * mb + 0.5f);
    return base;
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

static const BmBlock *ir_drop_block_model(int item_id, int item_meta) {
    const BmBlock *m = ir_block_model(item_id, item_meta);
    if (m) return m;
    return item_id == 61 ? &IR_DROP_FURNACE : 0;
}

int gm_item_drop_uses_block_atlas(int item_id, int item_meta) {
    return ir_drop_block_model(item_id, item_meta) != 0;
}

int gm_item_sprite_index(int item_id) {
    for (int i = 0; i < CR_ITEM_SPRITE_COUNT; ++i)
        if (CR_ITEM_SPRITES[i].id == item_id) return i;
    for (int i = 0; i < CR_ITEM_SPRITE_COUNT; ++i)
        if (!strcmp(CR_ITEM_SPRITES[i].name, IR_FALLBACK_NAME)) return i;
    return 0;
}

/* Rotate (lx,lz) about Y by spin; apply the JSON ground translation before
 * placing the model at feet + bob + RenderEntityItem's centering offset. */
static void ir_place(float fx, float fy, float fz, float bob, float ground_sy,
                     float ground_ty, float spin, float lx, float ly, float lz,
                     float *ox, float *oy, float *oz) {
    float cs = cosf(spin), sn = sinf(spin);
    float cy = fy + bob + 0.25f * ground_sy + ground_ty;
    *ox = fx + lx * cs + lz * sn;
    *oy = cy + ly;
    *oz = fz - lx * sn + lz * cs;
}

/* one miniature block cube: GROUND scale 0.25, spun about Y, bobbing. */
static int ir_emit_cube(const BmBlock *m, const GmEntityView *ev,
                        float fx, float fy, float fz,
                        int age, float hover, CrVertex *out) {
    float bob = ir_bob(age, hover), spin = ir_spin(age, hover);
    const float full_uv[4] = { 0.0f, 0.0f, 16.0f, 16.0f };
    int written = 0;
    for (int f = 0; f < 6; ++f) {
        float u0, v0, u1, v1;
        bm_sprite_uv(m->face[f].sprite, &u0, &v0, &u1, &v1);
        int32_t baked[28];
        rk_facebakery_make_quad(0.0f, 0.0f, 0.0f,
                                16.0f, 16.0f, 16.0f,
                                f, 0, full_uv, u0, u1, v0, v1,
                                0, 3, 0.0f, NULL, 0, baked);
        CrRgba tint = ir_lm_fold(ev, ir_tint(m->face[f].tint));
        CrVertex quad[4];
        for (int c = 0; c < 4; ++c) {
            const int o = c * 7;
            float lx = (ir_bits_float(baked[o])     - 0.5f) * 0.25f;
            float ly = (ir_bits_float(baked[o + 1]) - 0.5f) * 0.25f;
            float lz = (ir_bits_float(baked[o + 2]) - 0.5f) * 0.25f;
            CrVertex vtx;
            ir_place(fx, fy, fz, bob, IR_CUBE_GROUND_SY, IR_CUBE_GROUND_TY,
                     spin, lx, ly, lz,
                     &vtx.pos.x, &vtx.pos.y, &vtx.pos.z);
            vtx.uv.x = ir_bits_float(baked[o + 4]);
            vtx.uv.y = ir_bits_float(baked[o + 5]);
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
            ir_place(fx, fy, fz, bob, IR_FLAT_GROUND_SY, IR_FLAT_GROUND_TY,
                     spin, lx, ly, lz,
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
        const BmBlock *m = ir_drop_block_model(ents[e].item_id, ents[e].item_meta);
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
                                    ir_lm_fold(&ents[e],
                                        ir_tint(m->face[BM_SOUTH].tint)),
                                    out + written);
        } else {
            if (written + IR_CUBE_VERTS > max) break;
            written += ir_emit_cube(m, &ents[e], ents[e].x, ents[e].y, ents[e].z,
                                    ents[e].age, hover, out + written);
        }
    }
    return written;
}

/* RenderFallingBlock.doRender (oracle-src RenderFallingBlock.java:56-59):
 * the block MODEL, i.e. a UNIT cube, not the 0.25 GROUND item drop and not
 * the 0.98 entity bounding box. It renders the model at blockpos and then
 * translates by (x - blockpos.x - 0.5, y - blockpos.y, z - blockpos.z - 0.5),
 * so the cube spans [posX-0.5, posX+0.5] x [posY, posY+1] x [posZ-0.5,
 * posZ+0.5]: half-extent 0.5 in XZ, full height, base at the entity feet
 * (Entity.setPosition puts posY at the box minY). Face shades match mesh_mc /
 * item drops. */
static int ir_emit_falling_cube(const BmBlock *m, const GmEntityView *ev,
                                float scale, CrVertex *out) {
    const float half = 0.5f * scale;
    const float cy = ev->y + 0.5f;
    int written = 0;
    for (int f = 0; f < 6; ++f) {
        float u0, v0, u1, v1;
        bm_sprite_uv(m->face[f].sprite, &u0, &v0, &u1, &v1);
        CrRgba tint = ir_lm_fold(ev, ir_tint(m->face[f].tint));
        CrVertex quad[4];
        for (int c = 0; c < 4; ++c) {
            float lx = IR_FACES[f].c[c][0] ? half : -half;
            float ly = IR_FACES[f].c[c][1] ? half : -half;
            float lz = IR_FACES[f].c[c][2] ? half : -half;
            CrVertex vtx;
            vtx.pos.x = ev->x + lx;
            vtx.pos.y = cy + ly;
            vtx.pos.z = ev->z + lz;
            vtx.uv.x = u0 + IR_FACE_UV[f][c][0] * (u1 - u0);
            vtx.uv.y = v0 + IR_FACE_UV[f][c][1] * (v1 - v0);
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

/* The recorder did not carry EntityTNTPrimed.fuse in this tape generation,
 * but it does carry a monotonic ticksExisted reconstruction. Normalize that
 * counter per entity: the first client-visible row is after one onUpdate, so
 * the default fuse 80 renders as 79 there. */
static int ir_tnt_fuse(const GmEntityView *ev) {
    enum { TNT_TRACKED = 16 };
    static struct { int id, first_ticks, used; } tracked[TNT_TRACKED];
    int free_slot = -1;
    for (int i = 0; i < TNT_TRACKED; ++i) {
        if (tracked[i].used && tracked[i].id == ev->ent_id) {
            int age = ev->ticks_existed - tracked[i].first_ticks + 1;
            return 80 - (age > 0 ? age : 1);
        }
        if (!tracked[i].used && free_slot < 0) free_slot = i;
    }
    if (free_slot < 0) free_slot = (unsigned)ev->ent_id % TNT_TRACKED;
    tracked[free_slot].used = 1;
    tracked[free_slot].id = ev->ent_id;
    tracked[free_slot].first_ticks = ev->ticks_existed;
    return 79;
}

int gm_falling_blocks_emit(const GmEntityView *ents, int n, CrVertex *out, int max) {
    int written = 0;
    if (!ents || !out || max < IR_CUBE_VERTS) return 0;
    for (int e = 0; e < n; ++e) {
        int id = ents[e].item_id;
        int meta = ents[e].item_meta;
        float scale = 1.0f;
        if (ents[e].type == GM_VIEW_TNT_PRIMED) {
            id = 46;
            meta = 0;
            int fuse = ir_tnt_fuse(&ents[e]);
            if (fuse + 1 < 10) {
                float swell = 1.0f - (float)(fuse + 1) / 10.0f;
                if (swell < 0.0f) swell = 0.0f;
                if (swell > 1.0f) swell = 1.0f;
                swell *= swell;
                swell *= swell;
                scale = 1.0f + swell * 0.3f;
            }
        } else if (ents[e].type != GM_VIEW_FALLING_BLOCK) {
            continue;
        }
        const BmBlock *m = ir_block_model(id, meta);
        if (!m || m->kind == BM_KIND_CROSS) continue; /* MODEL render type only */
        if (written + IR_CUBE_VERTS > max) break;
        written += ir_emit_falling_cube(m, &ents[e], scale, out + written);
    }
    return written;
}

/* RenderMinecart display-tile transform. RenderMinecart translates the cart
 * up 0.375, scales the display model by 0.75, then applies the subtype's
 * (displayOffset - 8) / 16 vertical translation. Coordinates below are
 * vanilla block-model units, centered on the cart in XZ. */
static int ir_emit_minecart_box(const GmEntityView *ev, int display_offset,
                                const float from[3], const float to[3],
                                const int sprite[6], CrVertex *out) {
    const float scale = 0.75f;
    const float ybase = ev->y + 0.375f
                      + scale * (float)(display_offset - 8) / 16.0f;
    const float a = (180.0f - ev->yaw) * (3.14159265358979323846f / 180.0f);
    const float cs = cosf(a), sn = sinf(a);
    int written = 0;
    for (int f = 0; f < 6; ++f) {
        float u0, v0, u1, v1;
        bm_sprite_uv(sprite[f], &u0, &v0, &u1, &v1);
        CrVertex quad[4];
        for (int c = 0; c < 4; ++c) {
            float bx = IR_FACES[f].c[c][0] ? to[0] : from[0];
            float by = IR_FACES[f].c[c][1] ? to[1] : from[1];
            float bz = IR_FACES[f].c[c][2] ? to[2] : from[2];
            float lx = (bx / 16.0f - 0.5f) * scale;
            float lz = (bz / 16.0f - 0.5f) * scale;
            CrVertex vtx;
            vtx.pos.x = ev->x + cs * lx + sn * lz;
            vtx.pos.y = ybase + by * (scale / 16.0f);
            vtx.pos.z = ev->z - sn * lx + cs * lz;
            vtx.uv.x = u0 + IR_CUV[c][0] * (u1 - u0);
            vtx.uv.y = v0 + IR_CUV[c][1] * (v1 - v0);
            vtx.light = IR_FACES[f].shade;
            /* renderModelBrightness supplies entity brightness as vertex
             * colour, then fixed-function entity lighting attenuates it again.
             * The terrain pass has no GL lights, so fold their measured
             * material contribution into the display-tile tint here. */
            vtx.tint = ir_lm_fold(ev, (CrRgba){170, 170, 170, 255});
            vtx.ao = 1.0f;
            vtx.blk = 0.0f;
            quad[c] = vtx;
        }
        for (int k = 0; k < 6; ++k) out[written++] = quad[IR_TRI[k]];
    }
    return written;
}

static int ir_minecart_box(const GmEntityView *ev, int offset,
                           const float from[3], const float to[3],
                           const int sprite[6], CrVertex *out, int max) {
    if (max < IR_CUBE_VERTS) return 0;
    return ir_emit_minecart_box(ev, offset, from, to, sprite, out);
}

int gm_minecart_contents_emit(const GmEntityView *ents, int n,
                              CrVertex *out, int max) {
    int written = 0;
    if (!ents || !out) return 0;
    for (int e = 0; e < n; ++e) {
        const GmEntityView *ev = &ents[e];
        const float full0[3] = {0, 0, 0}, full1[3] = {16, 16, 16};
        int face[6], offset = 6;
        if (ev->type == GM_VIEW_MINECART_TNT) {
            const int f[6] = {
                CR_SPRITE_TNT_BOTTOM, CR_SPRITE_TNT_TOP,
                CR_SPRITE_TNT_SIDE, CR_SPRITE_TNT_SIDE,
                CR_SPRITE_TNT_SIDE, CR_SPRITE_TNT_SIDE
            };
            memcpy(face, f, sizeof face);
            written += ir_minecart_box(ev, offset, full0, full1, face,
                                       out + written, max - written);
        } else if (ev->type == GM_VIEW_MINECART_FURNACE) {
            const int f[6] = {
                CR_SPRITE_FURNACE_TOP, CR_SPRITE_FURNACE_TOP,
                CR_SPRITE_FURNACE_FRONT_OFF, CR_SPRITE_FURNACE_SIDE,
                CR_SPRITE_FURNACE_SIDE, CR_SPRITE_FURNACE_SIDE
            };
            memcpy(face, f, sizeof face);
            written += ir_minecart_box(ev, offset, full0, full1, face,
                                       out + written, max - written);
        } else if (ev->type == GM_VIEW_MINECART_CHEST) {
            const int f[6] = {
                CR_SPRITE_PLANKS_OAK, CR_SPRITE_PLANKS_OAK,
                CR_SPRITE_PLANKS_OAK, CR_SPRITE_PLANKS_OAK,
                CR_SPRITE_PLANKS_OAK, CR_SPRITE_PLANKS_OAK
            };
            const float body0[3] = {1, 0, 1}, body1[3] = {15, 10, 15};
            const float lid0[3] = {1, 10, 1}, lid1[3] = {15, 14, 15};
            const float knob0[3] = {7, 11, 0}, knob1[3] = {9, 13, 1};
            offset = 8;
            memcpy(face, f, sizeof face);
            written += ir_minecart_box(ev, offset, body0, body1, face,
                                       out + written, max - written);
            written += ir_minecart_box(ev, offset, lid0, lid1, face,
                                       out + written, max - written);
            written += ir_minecart_box(ev, offset, knob0, knob1, face,
                                       out + written, max - written);
        } else if (ev->type == GM_VIEW_MINECART_HOPPER) {
            const int side[6] = {
                CR_SPRITE_HOPPER_OUTSIDE, CR_SPRITE_HOPPER_OUTSIDE,
                CR_SPRITE_HOPPER_OUTSIDE, CR_SPRITE_HOPPER_OUTSIDE,
                CR_SPRITE_HOPPER_OUTSIDE, CR_SPRITE_HOPPER_OUTSIDE
            };
            const int rim[6] = {
                CR_SPRITE_HOPPER_OUTSIDE, CR_SPRITE_HOPPER_TOP,
                CR_SPRITE_HOPPER_OUTSIDE, CR_SPRITE_HOPPER_OUTSIDE,
                CR_SPRITE_HOPPER_OUTSIDE, CR_SPRITE_HOPPER_OUTSIDE
            };
            const int floor[6] = {
                CR_SPRITE_HOPPER_OUTSIDE, CR_SPRITE_HOPPER_INSIDE,
                CR_SPRITE_HOPPER_OUTSIDE, CR_SPRITE_HOPPER_OUTSIDE,
                CR_SPRITE_HOPPER_OUTSIDE, CR_SPRITE_HOPPER_OUTSIDE
            };
            static const float b0[7][3] = {
                {0,10,0}, {0,11,0}, {14,11,0}, {2,11,0},
                {2,11,14}, {4,4,4}, {6,0,6}
            };
            static const float b1[7][3] = {
                {16,11,16}, {2,16,16}, {16,16,16}, {14,16,2},
                {14,16,16}, {12,10,12}, {10,4,10}
            };
            offset = 1;
            for (int b = 0; b < 7; ++b) {
                const int *spr = b == 0 ? floor : (b < 5 ? rim : side);
                written += ir_minecart_box(ev, offset, b0[b], b1[b], spr,
                                           out + written, max - written);
            }
        }
    }
    return written;
}

int gm_items_emit_flat(const GmEntityView *ents, int n, CrVertex *out, int max) {
    const float aw = (float)CR_ITEM_ATLAS_W, ah = (float)CR_ITEM_ATLAS_H;
    int written = 0;
    for (int e = 0; e < n; ++e) {
        if (ents[e].type != GM_VIEW_ITEM) continue;
        if (ir_drop_block_model(ents[e].item_id, ents[e].item_meta)) continue;
        if (written + IR_FLAT_VERTS > max) break;
        const CrItemSprite *s = &CR_ITEM_SPRITES[gm_item_sprite_index(ents[e].item_id)];
        float hover = ents[e].has_hover_start ? ents[e].hover_start
                                               : ir_hover(ents[e].item_id, ents[e].item_meta);
        written += ir_emit_flat(ents[e].x, ents[e].y, ents[e].z, ents[e].age, hover,
                                (float)s->x0 / aw, (float)s->y0 / ah,
                                (float)s->x1 / aw, (float)s->y1 / ah,
                                ir_lm_fold(&ents[e],
                                    (CrRgba){255, 255, 255, 255}),
                                out + written);
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
                        float arm_rp[3], float arm_rot[3]) {
    arm_rp[0] = -5.0f; arm_rp[1] = 2.0f; arm_rp[2] = 0.0f;
    arm_rot[0] = arm_rot[1] = arm_rot[2] = 0.0f;
    if (v->tape_pose && (v->flags & 4)) return 0;       /* invisible */
    if (v->type == 15 ||
        (v->type == 2 && v->skin == CR_MOB_PIGMAN + 1)) {
        *disp = &IR_DISP_HANDHELD; arm_rot[0] = -ER_PI_F / 2.25f; /* zombie arms */
        return 283;                                     /* gold sword */
    }
    if (v->type == 3) {                                 /* ER_TYPE_SKELETON */
        *disp = &IR_DISP_BOW;                            /* arm down (idle) */
        return 261;                                     /* bow */
    }
    if (v->type == 32) {                                /* wither skeleton */
        *disp = &IR_DISP_HANDHELD;
        arm_rp[0] = -4.0f; /* ModelSkeleton.postRenderArm right-side shift */
        arm_rot[0] = cosf(v->limb_swing * 0.6662f + ER_PI_F) *
                     v->limb_swing_amount;
        if (v->swing_progress > 0.0f) {
            float sp = v->swing_progress;
            float body = sinf(sqrtf(sp) * ER_PI_F * 2.0f) * 0.2f;
            arm_rp[0] = -cosf(body) * 5.0f + 1.0f;
            arm_rp[2] = sinf(body) * 5.0f;
            arm_rot[1] = body * 3.0f;
            float q = 1.0f - sp;
            q *= q; q *= q;
            float f2 = sinf((1.0f - q) * ER_PI_F);
            float head_ax = v->pitch * IR_D2R;
            float f3 = sinf(sp * ER_PI_F) * -(head_ax - 0.7f) * 0.75f;
            arm_rot[0] -= f2 * 1.2f + f3;
            arm_rot[2] -= sinf(sp * ER_PI_F) * 0.4f;
        }
        return 272;                                     /* stone sword */
    }
    return 0;
}

int gm_held_items_emit(const GmEntityView *ents, int n, CrVertex *out, int max) {
    const float aw = (float)CR_ITEM_ATLAS_W, ah = (float)CR_ITEM_ATLAS_H;
    int written = 0;
    for (int e = 0; e < n; ++e) {
        const IrDisplay *disp; float arm_rp[3], arm_rot[3];
        int id = ir_held_item(&ents[e], &disp, arm_rp, arm_rot);
        if (!id) continue;
        if (written + 12 > max) break;

        IrMat m; ir_mat_identity(&m);
        ir_mat_translate(&m, arm_rp[0], arm_rp[1], arm_rp[2]);
        ir_mat_rot(&m, 2, arm_rot[2]);
        ir_mat_rot(&m, 1, arm_rot[1]);
        ir_mat_rot(&m, 0, arm_rot[0]);
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
            if (ents[e].lm_mul_r > 0.0f || ents[e].lm_mul_g > 0.0f ||
                ents[e].lm_mul_b > 0.0f) {
                tint.r = (u8)(255.0f * ents[e].lm_mul_r + 0.5f);
                tint.g = (u8)(255.0f * ents[e].lm_mul_g + 0.5f);
                tint.b = (u8)(255.0f * ents[e].lm_mul_b + 0.5f);
            } else {
                CrLightmapRgb c3 = cr_lightmap_rgb(0, (int)ents[e].lm_light,
                                                   (int)ents[e].lm_blk,
                                                   1.0f, 0, 0);
                tint.r = (u8)(255.0f * c3.r + 0.5f);
                tint.g = (u8)(255.0f * c3.g + 0.5f);
                tint.b = (u8)(255.0f * c3.b + 0.5f);
            }
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
                float model_scale = ents[e].type == 32 ? 1.2f : 1.0f;
                float wx = -p[0] / 16.0f * model_scale;
                float wy = (24.0f - p[1]) / 16.0f * model_scale;
                float wz = p[2] / 16.0f * model_scale;
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

/* RenderFireball / RenderDragonFireball: exact direct camera-facing quads from
 * their 1.11.2 doRender methods. Both use
 *   T(pos) S(scale) Ry(180-playerViewY) Rx(-playerViewX)
 * and vertices (-.5,-.25,0), (.5,-.25,0), (.5,.75,0), (-.5,.75,0).
 * EntitySmallFireball is registered by RenderManager at scale 0.5 and samples
 * Items.FIRE_CHARGE's particle icon (textures/items/fireball.png). The dragon
 * fireball uses scale 2.0 and textures/entity/enderdragon/dragon_fireball.png.
 * Both EntityFireball subclasses return packed brightness 0x00f000f0, so this
 * item-atlas pass deliberately leaves the vertices full-bright. */
static int emit_fireball_billboard(const GmEntityView *ent, float view_yaw,
                                   float view_pitch, CrVertex *out) {
    const float aw = (float)CR_ITEM_ATLAS_W, ah = (float)CR_ITEM_ATLAS_H;
    const CrItemSprite *s =
        &CR_ITEM_SPRITES[gm_item_sprite_index(ent->item_id)];
    float scale = ent->type == GM_VIEW_DRAGON_FIREBALL ? 2.0f : 0.5f;
    float yr = (180.0f - view_yaw) * IR_D2R;
    float pr = -view_pitch * IR_D2R;
    float cy = cosf(yr), sy = sinf(yr);
    float cp = cosf(pr), sp = sinf(pr);
    static const float CORN[4][4] = {
        { -0.5f, -0.25f, 0.0f, 1.0f },
        {  0.5f, -0.25f, 1.0f, 1.0f },
        {  0.5f,  0.75f, 1.0f, 0.0f },
        { -0.5f,  0.75f, 0.0f, 0.0f },
    };
    CrVertex quad[4];
    for (int c = 0; c < 4; ++c) {
        float px = CORN[c][0], py = CORN[c][1], pz = 0.0f;
        /* Rx(-playerViewX), then Ry(180-playerViewY), then S(scale). */
        float ty = py * cp - pz * sp, tz = py * sp + pz * cp;
        py = ty; pz = tz;
        float tx = px * cy + pz * sy;
        tz = -px * sy + pz * cy;
        px = tx; pz = tz;
        CrVertex vtx;
        vtx.pos.x = ent->x + px * scale;
        vtx.pos.y = ent->y + py * scale;
        vtx.pos.z = ent->z + pz * scale;
        vtx.uv.x = ((float)s->x0 + CORN[c][2] * 16.0f) / aw;
        vtx.uv.y = ((float)s->y0 + CORN[c][3] * 16.0f) / ah;
        vtx.light = 1.0f;
        vtx.blk = 15.0f;
        vtx.tint = (CrRgba){ 255, 255, 255, 255 };
        vtx.ao = 1.0f;
        quad[c] = vtx;
    }
    for (int k = 0; k < 6; ++k) out[k] = quad[IR_TRI[k]];
    return 6;
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
        int direct_fireball =
            (ents[e].type == GM_VIEW_BILLBOARD && ents[e].item_id == 385) ||
            ents[e].type == GM_VIEW_DRAGON_FIREBALL;
        if (ents[e].type != GM_VIEW_BILLBOARD && !direct_fireball) continue;
        if (direct_fireball) {
            if (written + 6 > max) break;
            written += emit_fireball_billboard(&ents[e], view_yaw, view_pitch,
                                                out + written);
            continue;
        }
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

/* Vanilla Entity*.setSize AABB (width, height) per LIVING view type - the box
 * Render.renderEntityOnFire scales its fire layers by. 0 for view types that
 * are not living entities (items, projectiles, crystals, boats, ...), so the
 * fire pass skips them. EW_TYPE_* / render-only ids as in entity_render.c;
 * slime_size is GmEntityView.item_meta (EntitySlime.setSlimeSize:
 * 0.51000005 * size). Blaze/player inherit the Entity default 0.6 x 1.8. */
static int ir_living_box(int type, int slime_size, float *w, float *h) {
    float bw, bh;
    switch (type) {
        case 1:  bw = 0.6f;  bh = 1.8f;  break;  /* player */
        case 2:                                  /* zombie */
        case 15: bw = 0.6f;  bh = 1.95f; break;  /* pigman */
        case 3:  bw = 0.6f;  bh = 1.99f; break;  /* skeleton */
        case 32: bw = 0.7f;  bh = 2.4f;  break;  /* wither skeleton */
        case 4:  bw = 0.6f;  bh = 1.7f;  break;  /* creeper */
        case 5:  bw = 1.4f;  bh = 0.9f;  break;  /* spider */
        case 6:  bw = 0.6f;  bh = 2.9f;  break;  /* enderman */
        case 7:  bw = 0.6f;  bh = 1.8f;  break;  /* blaze */
        case 10:                                 /* sheep */
        case 11:                                 /* pig */
        case 12: bw = 0.9f;  bh = 1.4f;  break;  /* cow */
        case 13: bw = 0.4f;  bh = 0.7f;  break;  /* chicken */
        case 14: bw = 0.8f;  bh = 0.8f;  break;  /* squid */
        case 23: bw = 0.6f;  bh = 1.95f; break;  /* witch */
        case 24: bw = 0.5f;  bh = 0.9f;  break;  /* bat */
        case 25: bw = 0.9f;  bh = 1.87f; break;  /* llama */
        case 26: bw = 4.0f;  bh = 4.0f;  break;  /* ghast */
        case 36: bw = 0.4f;  bh = 0.3f;  break;  /* silverfish */
        case 34: bw = 0.5f;  bh = 1.975f; break; /* armor stand */
        case 27:                                 /* magma cube */
        case 35: {                               /* slime */
            int sz = slime_size > 0 ? slime_size : 1;
            bw = bh = 0.51000005f * (float)sz;
            break;
        }
        default: return 0;
    }
    if (w) *w = bw;
    if (h) *h = bh;
    return 1;
}

/* Render.renderEntityOnFire (oracle-src Render.java:136-190) as camera-facing
 * quads: scale f = width*1.4, layer count from f3 = height/f, z base
 * -0.3 + (int)f3 * 0.02, then per layer f3/f4 -= 0.45, f1 *= 0.9, f5 += 0.03.
 * f4 (posY - boundingBox.minY) is 0 for every view magma carries: tape/live
 * entity y IS the AABB bottom. Unlit (GlStateManager.disableLighting) ->
 * light 1 / blk 15. (cy,sy) is the -playerViewY billboard rotation.
 * Returns verts written; stops early when out of room. */
static int ir_fire_layers(float ex, float ey, float ez,
                          float width, float height,
                          float cy, float sy,
                          CrVertex *out, int max) {
    static const int FIRE_SPRITE[2] = {
        CR_SPRITE_FIRE_LAYER_0, CR_SPRITE_FIRE_LAYER_1
    };
    float scale = width * 1.4f;
    if (scale <= 0.0f) return 0;
    float f3 = height / scale;
    float zbase = -0.3f + (float)((int)f3) * 0.02f;
    float half = 0.5f, yoff = 0.0f, zoff = 0.0f;
    int written = 0, layer = 0;
    while (f3 > 0.0f && layer < 8) {
        if (written + 6 > max) break;
        float u0, v0, u1, v1;
        bm_sprite_uv(FIRE_SPRITE[layer % 2], &u0, &v0, &u1, &v1);
        if ((layer / 2) % 2 == 0) {
            float t = u0; u0 = u1; u1 = t;
        }
        float local[4][5] = {
            {  half, 0.0f - yoff, zoff, u1, v1 },
            { -half, 0.0f - yoff, zoff, u0, v1 },
            { -half, 1.4f - yoff, zoff, u0, v0 },
            {  half, 1.4f - yoff, zoff, u1, v0 },
        };
        CrVertex quad[4];
        for (int c = 0; c < 4; ++c) {
            float px = local[c][0];
            float py = local[c][1];
            float pz = local[c][2] + zbase;
            float tx = px * cy + pz * sy;
            float tz = -px * sy + pz * cy;
            CrVertex vtx;
            vtx.pos.x = ex + tx * scale;
            vtx.pos.y = ey + py * scale;
            vtx.pos.z = ez + tz * scale;
            vtx.uv.x = local[c][3]; vtx.uv.y = local[c][4];
            vtx.light = 1.0f; vtx.blk = 15.0f;
            vtx.tint = (CrRgba){255, 255, 255, 255};
            vtx.ao = 1.0f;
            quad[c] = vtx;
        }
        for (int k = 0; k < 6; ++k)
            out[written++] = quad[IR_TRI[k]];
        half *= 0.9f;
        yoff -= 0.45f;
        zoff += 0.03f;
        f3 -= 0.45f;
        ++layer;
    }
    return written;
}

/* Render.doRenderShadowAndFire -> renderEntityOnFire for fireballs that are
 * isBurning() (GmEntityView.flags bit 0). RenderManager only calls this when
 * entityIn.isBurning(); EntityFireball.setFire(1) each tick when
 * isFireballFiery(), but a freshly pinned non-ticked fireball has fire==0 and
 * must show only the fire_charge billboard (ui_entities fireball_small golden).
 * EntitySmallFireball width=height=0.3125 (scale 0.4375); EntityLargeFireball
 * width=height=1.0 (scale 1.4). item_meta>=2 marks large after
 * gm_entity_prep_large_fireball_fire. Both heights/f yield two fire layers.
 * Dragon fireballs (item_id 9003) never enter here. */
int gm_small_fireball_fire_emit(const GmEntityView *ents, int n,
                                float view_yaw, CrVertex *out, int max) {
    float yr = -view_yaw * IR_D2R;
    float cy = cosf(yr), sy = sinf(yr);
    int written = 0;
    for (int e = 0; e < n; ++e) {
        if (ents[e].type != GM_VIEW_BILLBOARD || ents[e].item_id != 385)
            continue;
        /* flags bit 0 = burn (Entity.isBurning). Legacy projectile rows do
         * not carry Entity.fire, but EntityFireball calls setFire(1) on its
         * first update; ticksExisted distinguishes that state from a fresh,
         * not-yet-ticked fireball pin. */
        if ((ents[e].flags & 1) == 0 && ents[e].ticks_existed <= 0)
            continue;
        if (written + 12 > max) break;
        /* EntityLargeFireball width 1.0; EntitySmallFireball width 0.3125. */
        float width = (ents[e].item_meta >= 2) ? 1.0f : 0.3125f;
        written += ir_fire_layers(ents[e].x, ents[e].y, ents[e].z,
                                  width, width, cy, sy,
                                  out + written, max - written);
    }
    return written;
}

/* Render.doRenderShadowAndFire -> renderEntityOnFire for LIVING entities that
 * are isBurning() (Render.java:344-348). The tape recorder writes bit 0 of the
 * per-entity flags field straight from EntityLivingBase.isBurning()
 * (Recorder.java "flags bitfield: 1=burning"), so this is the RECORDED
 * oracle state, never an inference. EntityBlaze overrides isBurning() to
 * return isCharged() - the ON_FIRE datamanager bit its EntityAIFireballAttack
 * sets for the whole volley (EntityBlaze.java:172-186, 281-291) - which is why
 * an aggroed oracle blaze is engulfed in flames while idling ones are not.
 * Fireball billboards keep their own pass (gm_small_fireball_fire_emit); every
 * other view type with no living AABB is skipped. */
int gm_entity_fire_emit(const GmEntityView *ents, int n,
                        float view_yaw, CrVertex *out, int max) {
    float yr = -view_yaw * IR_D2R;
    float cy = cosf(yr), sy = sinf(yr);
    int written = 0;
    for (int e = 0; e < n; ++e) {
        if ((ents[e].flags & 1) == 0)
            continue;
        float w, h;
        if (!ir_living_box(ents[e].type, ents[e].item_meta, &w, &h))
            continue;
        if (written + 6 > max) break;
        written += ir_fire_layers(ents[e].x, ents[e].y, ents[e].z, w, h,
                                  cy, sy, out + written, max - written);
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
 * faces with RenderHelper's standard GUI lighting, sample terrain (or a single
 * gui-atlas tile for blocks without a model key).
 *
 * Grow-on-demand buffers rasterize at 16 * GUI scale, matching the physical
 * pixel grid used by OpenGL instead of scaling a completed 16x16 bake.
 * ========================================================================= */

#define ICON_GUI_N 16
static CrRgba *g_icon_rgba;
static float  *g_icon_z;
static int     g_icon_n;
static int     g_icon_capacity;

/* Fixed-function RenderHelper.enableGUIStandardItemLighting after its
 * Ry(-30)*Rx(165) light setup, the GUI y-flip, and block.json's
 * Rx(30)*Ry(225) model transform. Ambient is 0.4 and each positive light dot
 * contributes 0.6; full precision is unnecessary after u8 texel rounding. */
static const float IR_GUI_SHADE[6] = {
    0.400000f, 1.000000f, 0.434702f,
    0.721625f, 0.745950f, 0.636575f
};

/* FaceBakery's default full-cube BlockFaceUV ordering, expressed in the
 * IR_FACES corner order. DOWN/SOUTH/WEST/EAST already match IR_CUV; UP and
 * NORTH do not. */
static const float IR_GUI_UV[6][4][2] = {
    { {0,1}, {1,1}, {1,0}, {0,0} }, /* DOWN  */
    { {0,0}, {0,1}, {1,1}, {1,0} }, /* UP    */
    { {1,1}, {1,0}, {0,0}, {0,1} }, /* NORTH */
    { {0,1}, {1,1}, {1,0}, {0,0} }, /* SOUTH */
    { {0,1}, {1,1}, {1,0}, {0,0} }, /* WEST  */
    { {0,1}, {1,1}, {1,0}, {0,0} }, /* EAST  */
};

typedef struct {
    const unsigned char *px; /* R,G,B,A bytes */
    int x0, y0, w, h, stride; /* sprite rect; stride in texels */
} IrIconTex;

static int ir_icon_clear(int scale) {
    int n = ICON_GUI_N * scale;
    int pixels = n * n;
    if (pixels > g_icon_capacity) {
        CrRgba *rgba = realloc(g_icon_rgba, (size_t)pixels * sizeof *rgba);
        if (!rgba) return 0;
        g_icon_rgba = rgba;
        float *z = realloc(g_icon_z, (size_t)pixels * sizeof *z);
        if (!z) return 0;
        g_icon_z = z;
        g_icon_capacity = pixels;
    }
    g_icon_n = n;
    for (int i = 0; i < pixels; ++i) {
        g_icon_rgba[i] = (CrRgba){0, 0, 0, 0};
        g_icon_z[i] = -1e30f;
    }
    return 1;
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
    const float s = 0.625f * (float)g_icon_n;
    *sx = 0.5f * (float)g_icon_n + s * x2;
    *sy = 0.5f * (float)g_icon_n - s * y2; /* GUI y-flip */
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
    if (maxx > g_icon_n - 1) maxx = g_icon_n - 1;
    if (miny < 0) miny = 0;
    if (maxy > g_icon_n - 1) maxy = g_icon_n - 1;
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
            int i = py * g_icon_n + px;
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

/* Alpha-composite the framebuffer-resolution icon into fb at (dx,dy). */
static void ir_icon_blit(CrFramebuffer *fb, int dx, int dy) {
    if (!fb || !fb->color) return;
    for (int sy = 0; sy < g_icon_n; ++sy) {
        for (int sx = 0; sx < g_icon_n; ++sx) {
            CrRgba src = g_icon_rgba[sy * g_icon_n + sx];
            if (src.a == 0) continue;
            int x = dx + sx, y = dy + sy;
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
    if (item_id <= 0 || item_id > 255 || scale < 1) return 0; /* not a block id */
    const BmBlock *m = ir_block_model(item_id, item_meta);
    /* Cross / non-cube models stay flat (torch etc.). */
    if (m && m->kind != BM_KIND_CUBE && m->kind != BM_KIND_SLAB_BOTTOM &&
        m->kind != BM_KIND_SLAB_TOP && m->kind != BM_KIND_STAIRS &&
        m->kind != BM_KIND_CACTUS && m->kind != BM_KIND_SNOW_LAYER)
        return 0;

    if (!ir_icon_clear(scale)) return 0;

    if (m && m->kind == BM_KIND_CUBE) {
        for (int f = 0; f < 6; ++f) {
            IrIconTex tex = ir_tex_from_sprite(m->face[f].sprite);
            ir_icon_face(IR_FACES[f].c, IR_GUI_UV[f], &tex, IR_GUI_SHADE[f],
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
            ir_icon_face(IR_FACES[f].c, IR_GUI_UV[f], &tex,
                         IR_GUI_SHADE[f], white);
    }

    /* Did we actually draw anything? */
    int any = 0;
    for (int i = 0; i < g_icon_n * g_icon_n; ++i)
        if (g_icon_rgba[i].a) { any = 1; break; }
    if (!any) return 0;
    if (fb) ir_icon_blit(fb, dx, dy);
    return 1;
}
