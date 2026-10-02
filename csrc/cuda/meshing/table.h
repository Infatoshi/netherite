/* The table mesher (cuda/meshing table.c, table_host.c): the device
 * mesher's count pass with every drawn cell drawn apart, each cell's
 * vertices then laid out in rb_mesh_section's order. Plain C, read by the
 * device C (clang, nvptx64) and the host C.
 *
 * Without smooth lighting (the table's ao 0: the pipeline's scenes) a
 * cell's draw depends on the mesher's state only through the words its own
 * begin sets (the bounds; kernels.c's lanes chain the rest from cell to
 * cell), for all but a few render types, and leaves the others as it found
 * them: the cells of a section pass can be drawn independently. The host's
 * class table (table_host.c) names the (id, meta) for which that holds,
 * each drawn on its own by the C renderer in random worlds and held to
 * mtab_draw_check, and how the device draws it: a standard block, a
 * liquid, a door a face a thread; a log, a fence, stairs a box's face a
 * thread; anything else whole, a thread a cell (rb_mesh_cell). The face and
 * box paths transcribe the C's float operations in its order and the host
 * holds each of their (id, meta) against rb_mesh_cell byte for byte;
 * whatever fails is drawn whole.
 *
 * A count launch in the table mode (host.c run, every count pass):
 * mesh_tab_cache (each request's world around its section, for the
 * accessors), mesh_tab_list (each cell's kind and class, the classes'
 * lists), the classes' draw kernels on their own streams, mesh_tab_place
 * (each request's layout, count, flags, place in vbuf) and mesh_tab_copy.
 * A request with a drawn cell the table cannot draw (a piston, the few
 * others), or one whose whole-cell draw failed its checks, is counted by
 * mesh_run the exact way (MTAB_REDO); one past the table's vertex buffer
 * keeps its count and is left to the write pass (voff -1), whose mesh_run
 * draws it. */
#ifndef NETHERITE_CUDA_MESHING_TABLE_H
#define NETHERITE_CUDA_MESHING_TABLE_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "../../engine/blocks.h"
#include "../../engine/render_blocks.h"
#include "../../engine/render_blocks_int.h"

struct meshing_launch;

/* each (id, meta)'s table class (the host's table, uint8 [RB_IDS * RB_METAS]):
 * a standard block drawn a face a thread (mtab_cube_face), a liquid a face a
 * thread (mtab_fluid_face), another cell drawn whole, a draw of standard
 * blocks over boxes a box's face a thread (mtab_boxes: logs, fences,
 * stairs), a door a face a thread (mtab_door_face); each class has its list
 * of cells and its kernel */
enum { MTAB_NO = 0, MTAB_CUBE = 1, MTAB_FLUID = 2, MTAB_GEN = 3, MTAB_BOX = 4, MTAB_DOOR = 5, MTAB_CLASSES = 6 };
/* the most vertices a table cell may draw (more: its draw fails the check) */
enum { MTAB_MAXV = 128, MTAB_THREADS = 512, MTAB_DT = 64 /* a draw kernel's block */ };
/* a request's table flags (meshing_launch tflag): the table passed its
 * buffer (the write pass draws the request); a check failed (mesh_run
 * counts it again); bits 8..11 the Tessellator flags its table cells set */
enum { MTAB_FULL = 1, MTAB_REDO = 2 };

/* kernels.c's per-thread context (m->dev): the libm values (rb_cos and the
 * others look them up) and the errors; the table's draws make their own */
struct meshing_ctx {
    const struct rb_trig *t1, *t2;      /* the request's, the launch's */
    int n1, n2;
    int err;
};

/* the marks (kernels.c MARK): a table cell's draw starts with these
 * carried values and every flag on; a vertex field left at one was written
 * before the cell's own setter */
#define MTAB_MARK_UV __builtin_nan("0x4dead0000beef")
enum { MTAB_MARK_COLOR = 0x00dead01, MTAB_MARK_BRIGHT = (int32_t)0x80000000 };

static inline uint32_t mtab_fbits(float f)
{
    uint32_t w;
    __builtin_memcpy(&w, &f, 4);
    return w;
}

static inline uint32_t mtab_wget(const void *p, int k)
{
    uint32_t w;
    __builtin_memcpy(&w, (const char *)p + 4 * k, 4);
    return w;
}

/* the mesher's words a draw may leave changed: all but its pointers to the
 * world, the Tessellator and the context, the camera block, and the bounds,
 * which each cell's draw sets first (render_block_begin, hidden_cube_state) */
enum { MTAB_MW = (int)(sizeof(struct rb_mesher) / 4) };
static inline int mtab_word_kept(int k)
{
#define MTAB_IN(f, n) (k * 4 >= (int)offsetof(struct rb_mesher, f) && k * 4 < (int)offsetof(struct rb_mesher, f) + (n))
    return !(MTAB_IN(w, 8) || MTAB_IN(t, 8) || MTAB_IN(dev, 8) || MTAB_IN(px, 12) ||
             (k * 4 >= (int)offsetof(struct rb_mesher, min_x) && k * 4 < (int)offsetof(struct rb_mesher, enable_ao)));
#undef MTAB_IN
}
_Static_assert(offsetof(struct rb_mesher, partial) + 4 == offsetof(struct rb_mesher, enable_ao) &&
               offsetof(struct rb_mesher, max_z) + 8 == offsetof(struct rb_mesher, partial) && sizeof(struct rb_mesher) % 4 == 0,
               "the bounds, then partial, then enable_ao");

/* M's words (mtab_word_kept) against the template's (0: equal; unrolled:
 * the loads go out together, the kept words are constants) */
static inline uint32_t mtab_words_off(const struct rb_mesher *m, const struct rb_mesher *tp)
{
    uint32_t d = 0;
#if defined(__clang__)
#pragma clang loop unroll(full)
#endif
    for (int k = 0; k < MTAB_MW; ++k)
        if (mtab_word_kept(k)) d |= mtab_wget(m, k) ^ mtab_wget(tp, k);
    return d;
}

/* The table cell (ID, META) at (X, Y, Z) drawn by the C renderer into V
 * (MTAB_MAXV vertices): M a copy of the template TP whose world, context
 * and Tessellator T (zeroed but for its translation: the section begun)
 * are set. Its vertex count; *FLAGS the Tessellator flags the draw set, and
 * MTAB_REDO << 4 when a check failed: more vertices than V holds, a vertex
 * field no setter of the cell wrote (its carried values start at the
 * marks, every flag on; MARKS 0: the caller checks the vertices as it
 * copies them), the words past the bounds not the template's, the
 * translation or normal changed. From the template's words (a lane holds
 * the chain's so) the draw from begin is the draw in rb_mesh_section's
 * chain. CLS MTAB_CUBE: the draw is rb_mesh_cell_standard's on the device
 * (a kernel of it reaches no other render type's code). */
static inline int mtab_draw_check(struct rb_mesher *m, struct rb_tess *t, const struct rb_mesher *tp, int id, int meta, int x, int y,
                                  int z, int32_t *v, int *flags, int cls, int marks)
{
    const double xo = t->xoff, yo = t->yoff, zo = t->zoff;
    t->u = t->v = MTAB_MARK_UV;
    t->color = MTAB_MARK_COLOR;
    t->brightness = MTAB_MARK_BRIGHT;
    t->has_texture = t->has_color = t->has_brightness = 1;
    t->raw = v;
    t->cap = MTAB_MAXV * 8;
#if defined(__NVPTX__)
    if (cls == MTAB_CUBE) rb_mesh_cell_standard(m, 2, id, meta, x, y, z);
    else
#endif
        rb_mesh_cell(m, 2, id, meta, x, y, z);
    const uint32_t mu = mtab_fbits((float)MTAB_MARK_UV);
    int n = t->vertex_count, bad = t->dropped;
    for (int j = 0; marks && j < n; ++j)
        bad |= (uint32_t)v[j * 8 + 3] == mu || (uint32_t)v[j * 8 + 4] == mu || v[j * 8 + 5] == MTAB_MARK_COLOR ||
               v[j * 8 + 7] == MTAB_MARK_BRIGHT;
    bad |= mtab_words_off(m, tp) != 0;
    bad |= t->xoff != xo || t->yoff != yo || t->zoff != zo || t->normal != 0;
    /* a setter replaced the mark (no setter writes one: a colour's alpha
     * is 255, a brightness is not negative, a texture coordinate is not
     * that NaN) */
    uint32_t u, vv;
    float fu = (float)t->u, fv = (float)t->v;
    __builtin_memcpy(&u, &fu, 4);
    __builtin_memcpy(&vv, &fv, 4);
    *flags = (u != mu || vv != mu ? 1 : 0) | (t->color != MTAB_MARK_COLOR ? 2 : 0) | (t->brightness != MTAB_MARK_BRIGHT ? 4 : 0) |
             (t->has_normals ? 8 : 0) | (bad ? MTAB_REDO << 4 : 0);
    return n;
}

/* A standard block's face F (0..5: y-, y+, z-, z+, x-, x+) as
 * render_standard_block_color draws it (the table's ao 0), on its own: the
 * mesher M at the cell's begin (render_block_begin's words, the template's
 * others). mtab_cube_face_shown: the face is drawn; mtab_cube_face: its
 * brightness, colour (the block's colour multiplier P5..P7, as
 * render_standard_block makes them) and quad. The same float operations in
 * the same order as the C; the host's class table holds every standard
 * block drawn this way against rb_mesh_cell byte for byte (table_host.c) */
static inline int mtab_cube_face_shown(struct rb_mesher *m, int id, int x, int y, int z, int f)
{
    int dx = f == 4 ? -1 : f == 5 ? 1 : 0, dy = f == 0 ? -1 : f == 1 ? 1 : 0, dz = f == 2 ? -1 : f == 3 ? 1 : 0;
    return m->render_all_faces || should_side_be_rendered(m, id, x + dx, y + dy, z + dz, f);
}

/* the icons baked at setup (table_host.c): (id, meta, face)'s
 * block_icon_world_index code, or MTAB_ICON_WORLD where the world decides it
 * (a grass side, the anvil's and the piston's) */
enum { MTAB_ICON_WORLD = (int32_t)0x80000000 };
static inline const struct rb_uv *mtab_icon(struct rb_mesher *m, const int32_t *icons, int id, int meta, int x, int y, int z, int f)
{
    int32_t k = icons ? icons[((id * RB_METAS) + (meta & 15)) * RB_SIDES + f] : MTAB_ICON_WORLD;
    return k != MTAB_ICON_WORLD ? &m->icon[k] : block_icon_world(m, id, x, y, z, f);
}

static inline void mtab_cube_face(struct rb_mesher *m, int id, int meta, int x, int y, int z, int f, float p5, float p6, float p7,
                                  const int32_t *icons)
{
    struct rb_tess *t = m->t;
    float v10 = 0.5F, v11 = 1.0F, v12 = 0.8F, v13 = 0.6F;
    float v14 = v11 * p5, v15 = v11 * p6, v16 = v11 * p7;
    float v17 = v10, v18 = v12, v19 = v13;
    float v20 = v10, v21 = v12, v22 = v13;
    float v23 = v10, v24 = v12, v25 = v13;
    int is_grass = id == 2 && strcmp(BLOCKS[2].class_name, "BlockGrass") == 0;
    if (!is_grass)
    {
        v17 = v10 * p5; v18 = v12 * p5; v19 = v13 * p5;
        v20 = v10 * p6; v21 = v12 * p6; v22 = v13 * p6;
        v23 = v10 * p7; v24 = v12 * p7; v25 = v13 * p7;
    }
    switch (f)
    {
        case 0:
            tess_set_brightness(t, m->min_y > 0.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x, y - 1, z));
            tess_set_color_opaque_f(t, v17, v20, v23);
            render_face_y_neg(m, (double)x, (double)y, (double)z, mtab_icon(m, icons, id, meta, x, y, z, 0));
            break;
        case 1:
            tess_set_brightness(t, m->max_y < 1.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x, y + 1, z));
            tess_set_color_opaque_f(t, v14, v15, v16);
            render_face_y_pos(m, (double)x, (double)y, (double)z, mtab_icon(m, icons, id, meta, x, y, z, 1));
            break;
        case 2:
            tess_set_brightness(t, m->min_z > 0.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x, y, z - 1));
            tess_set_color_opaque_f(t, v18, v21, v24);
            render_face_z_neg(m, (double)x, (double)y, (double)z, mtab_icon(m, icons, id, meta, x, y, z, 2));
            break;
        case 3:
            tess_set_brightness(t, m->max_z < 1.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x, y, z + 1));
            tess_set_color_opaque_f(t, v18, v21, v24);
            render_face_z_pos(m, (double)x, (double)y, (double)z, mtab_icon(m, icons, id, meta, x, y, z, 3));
            break;
        case 4:
            tess_set_brightness(t, m->min_x > 0.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x - 1, y, z));
            tess_set_color_opaque_f(t, v19, v22, v25);
            render_face_x_neg(m, (double)x, (double)y, (double)z, mtab_icon(m, icons, id, meta, x, y, z, 4));
            break;
        default:
            tess_set_brightness(t, m->max_x < 1.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x + 1, y, z));
            tess_set_color_opaque_f(t, v19, v22, v25);
            render_face_x_pos(m, (double)x, (double)y, (double)z, mtab_icon(m, icons, id, meta, x, y, z, 5));
            break;
    }
}

/* render_standard_block's colour multiplier as floats */
static inline void mtab_cube_color(struct rb_mesher *m, int id, int x, int y, int z, float *r, float *g, float *b)
{
    int32_t c = color_multiplier(m, id, x, y, z);
    *r = (float)((c >> 16) & 255) / 255.0F;
    *g = (float)((c >> 8) & 255) / 255.0F;
    *b = (float)(c & 255) / 255.0F;
}

/* A cell whose draw is render_standard_block over boxes: each box's
 * bounds, field_152631_f and uv rotations (bits: east, west, south, north,
 * top, bottom), as render_block_log (render type 31), rb2_render_fence
 * (11) and rb2_render_stairs (10) set them, from the mesher at the cell's
 * begin; how many (0: not such a cell). The host's class table holds every such cell drawn this way
 * against rb_mesh_cell byte for byte (table_host.c) */
enum { MTAB_BOXES = 5 };
struct mtab_box { double b[6]; int f152631f, uv; };

static inline int mtab_boxes(struct rb_mesher *m, int id, int meta, int x, int y, int z, struct mtab_box *o)
{
    int rt = BLOCKS[id].render_type;
    if (rt == 31)
    {
        int axis = meta & 12;
        o[0] = (struct mtab_box){{m->min_x, m->min_y, m->min_z, m->max_x, m->max_y, m->max_z}, m->f152631f,
                                 axis == 4 ? 1 | 2 | 16 | 32 : axis == 8 ? 4 | 8 : 0};
        return 1;
    }
    if (rt == 10)
    {
        double b[6];
        o[0] = (struct mtab_box){{0.0, (meta & 4) != 0 ? 0.5 : 0.0, 0.0, 1.0, (meta & 4) != 0 ? 1.0 : 0.5, 1.0}, 0, 0};
        int top = stairs_top_bounds(m, x, y, z, b);
        o[1] = (struct mtab_box){{b[0], b[1], b[2], b[3], b[4], b[5]}, 1, 0};
        if (!(top && stairs_corner_bounds(m, x, y, z, b))) return 2;
        o[2] = (struct mtab_box){{b[0], b[1], b[2], b[3], b[4], b[5]}, 1, 0};
        return 3;
    }
    if (rt != 11) return 0;
    int n = 0;
    float var6 = 0.375F, var7 = 0.625F;
    o[n++] = (struct mtab_box){{var6, 0.0, var6, var7, 1.0, var7}, m->f152631f, 0};
    int var10 = fence_connects(m, id, x - 1, y, z);
    int var11 = fence_connects(m, id, x + 1, y, z);
    int var12 = fence_connects(m, id, x, y, z - 1);
    int var13 = fence_connects(m, id, x, y, z + 1);
    int var8 = (var10 || var11) ? 1 : 0;
    int var9 = (var12 || var13) ? 1 : 0;
    if (!var8 && !var9) var8 = 1;
    var6 = 0.4375F;
    var7 = 0.5625F;
    float var14 = 0.75F, var15 = 0.9375F;
    float var16 = var10 ? 0.0F : var6, var17 = var11 ? 1.0F : var7;
    float var18 = var12 ? 0.0F : var6, var19 = var13 ? 1.0F : var7;
    if (var8) o[n++] = (struct mtab_box){{var16, var14, var6, var17, var15, var7}, 1, 0};
    if (var9) o[n++] = (struct mtab_box){{var6, var14, var18, var7, var15, var19}, 1, 0};
    var14 = 0.375F;
    var15 = 0.5625F;
    if (var8) o[n++] = (struct mtab_box){{var16, var14, var6, var17, var15, var7}, 1, 0};
    if (var9) o[n++] = (struct mtab_box){{var6, var14, var18, var7, var15, var19}, 1, 0};
    return n;
}

/* box B's words in M, as the draw sets them before its render_standard_block */
static inline void mtab_box_set(struct rb_mesher *m, const struct mtab_box *b)
{
    rb2_set_render_bounds(m, b->b[0], b->b[1], b->b[2], b->b[3], b->b[4], b->b[5]);
    m->f152631f = b->f152631f;
    m->uv_east = b->uv & 1;
    m->uv_west = b->uv >> 1 & 1;
    m->uv_south = b->uv >> 2 & 1;
    m->uv_north = b->uv >> 3 & 1;
    m->uv_top = b->uv >> 4 & 1;
    m->uv_bottom = b->uv >> 5 & 1;
}

/* A door's faces as rb2_render_door draws them: whether it draws (the
 * other half is there), and face F (its bounds, brightness, colour, icon
 * and quad; door_icon flips its icon's copy, not the mesher, and the draw
 * clears flip_texture before each side: a face's draw needs no other).
 * mtab_door_icon is door_icon with the upper half's icon from the table
 * (UP[id]: door_upper_uv's, which searches the atlas by name, made once) */
static inline void mtab_door_icon(struct rb_mesher *m, int id, int side, int state, const struct rb_uv *up, struct rb_uv *out)
{
    const struct rb_uv *lower = icon_from_side_meta(m, id, 0, 0);
    if (side == 1 || side == 0)
    {
        *out = *lower;
        return;
    }
    int var3 = state & 3;
    int var4 = (state & 4) != 0;
    int var9 = 0;
    int upper = (state & 8) != 0;
    if (var4)
    {
        if ((var3 == 0 && side == 2) || (var3 == 1 && side == 5) || (var3 == 2 && side == 3) || (var3 == 3 && side == 4)) var9 = 1;
    }
    else
    {
        if ((var3 == 0 && side == 5) || (var3 == 1 && side == 3) || (var3 == 2 && side == 4) || (var3 == 3 && side == 2)) var9 = 1;
        if ((state & 16) != 0) var9 = !var9;
    }
    *out = upper ? up[id] : *lower;
    if (var9)
    {
        float s = out->min_u;
        out->min_u = out->max_u;
        out->max_u = s;
    }
}

static inline int mtab_door_drawn(struct rb_mesher *m, int id, int x, int y, int z)
{
    int meta = rb_world_meta(m->w, x, y, z);
    return (meta & 8) != 0 ? rb_world_block(m->w, x, y - 1, z) == id : rb_world_block(m->w, x, y + 1, z) == id;
}

static inline void mtab_door_face(struct rb_mesher *m, int id, int x, int y, int z, int f, const struct rb_uv *up)
{
    struct rb_tess *t = m->t;
    int state = door_state(m, x, y, z);
    double b[6];
    door_bounds(state, b);
    rb2_set_render_bounds(m, b[0], b[1], b[2], b[3], b[4], b[5]);
    float var8 = 0.5F, var9 = 1.0F, var10 = 0.8F, var11 = 0.6F;
    struct rb_uv ic;
    switch (f)
    {
        case 0:
            tess_set_brightness(t, m->min_y > 0.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x, y - 1, z));
            tess_set_color_opaque_f(t, var8, var8, var8);
            mtab_door_icon(m, id, 0, state, up, &ic);
            render_face_y_neg(m, (double)x, (double)y, (double)z, &ic);
            break;
        case 1:
            tess_set_brightness(t, m->max_y < 1.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x, y + 1, z));
            tess_set_color_opaque_f(t, var9, var9, var9);
            mtab_door_icon(m, id, 1, state, up, &ic);
            render_face_y_pos(m, (double)x, (double)y, (double)z, &ic);
            break;
        case 2:
            tess_set_brightness(t, m->min_z > 0.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x, y, z - 1));
            tess_set_color_opaque_f(t, var10, var10, var10);
            mtab_door_icon(m, id, 2, state, up, &ic);
            m->flip_texture = 0;
            render_face_z_neg(m, (double)x, (double)y, (double)z, &ic);
            break;
        case 3:
            tess_set_brightness(t, m->max_z < 1.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x, y, z + 1));
            tess_set_color_opaque_f(t, var10, var10, var10);
            mtab_door_icon(m, id, 3, state, up, &ic);
            m->flip_texture = 0;
            render_face_z_pos(m, (double)x, (double)y, (double)z, &ic);
            break;
        case 4:
            tess_set_brightness(t, m->min_x > 0.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x - 1, y, z));
            tess_set_color_opaque_f(t, var11, var11, var11);
            mtab_door_icon(m, id, 4, state, up, &ic);
            m->flip_texture = 0;
            render_face_x_neg(m, (double)x, (double)y, (double)z, &ic);
            break;
        default:
            tess_set_brightness(t, m->max_x < 1.0 ? block_brightness(m, id, x, y, z) : block_brightness(m, id, x + 1, y, z));
            tess_set_color_opaque_f(t, var11, var11, var11);
            mtab_door_icon(m, id, 5, state, up, &ic);
            m->flip_texture = 0;
            render_face_x_pos(m, (double)x, (double)y, (double)z, &ic);
            break;
    }
}

/* A liquid's faces as render_block_fluids draws them: face F (0 the top,
 * 1 the bottom, 2..5 the sides i = F - 2), whether it is drawn, and its
 * quads (8 vertices a top and a side, 4 the bottom), with the C's float
 * operations in its order. The sides read the corner heights less the top's
 * lift when the top is drawn, as the C lowers them in its top block before
 * its sides */
static inline int mtab_fluid_shown(struct rb_mesher *m, int id, int x, int y, int z, int f)
{
    int dx = f == 4 ? -1 : f == 5 ? 1 : 0, dy = f == 0 ? 1 : f == 1 ? -1 : 0, dz = f == 2 ? -1 : f == 3 ? 1 : 0;
    int side = f == 0 ? 1 : f == 1 ? 0 : f;
    return m->render_all_faces || should_side_be_rendered(m, id, x + dx, y + dy, z + dz, side);
}

static inline void mtab_fluid_face(struct rb_mesher *m, int id, int x, int y, int z, int f)
{
    struct rb_tess *t = m->t;
    int32_t c = color_multiplier(m, id, x, y, z);
    float r = (float)((c >> 16) & 255) / 255.0F;
    float g = (float)((c >> 8) & 255) / 255.0F;
    float b = (float)(c & 255) / 255.0F;
    int mat = BLOCKS[id].material;
    float f14 = 0.5F, f15 = 1.0F, f16 = 0.8F, f17 = 0.6F;
    double d32 = 0.0010000000474974513;
    if (f == 1)
    {
        tess_set_brightness(t, block_brightness(m, id, x, y - 1, z));
        tess_set_color_opaque_f(t, f14, f14, f14);
        render_face_y_neg(m, (double)x, (double)y + d32, (double)z, icon_from_side(m, id, 0));
        return;
    }
    int meta = rb_world_meta(m->w, x, y, z);
    double h24 = (double)fluid_height(m, x, y, z, mat);
    double h26 = (double)fluid_height(m, x, y, z + 1, mat);
    double h28 = (double)fluid_height(m, x + 1, y, z + 1, mat);
    double h30 = (double)fluid_height(m, x + 1, y, z, mat);
    if (mtab_fluid_shown(m, id, x, y, z, 0))
    {
        h24 -= d32;
        h26 -= d32;
        h28 -= d32;
        h30 -= d32;
    }
    if (f == 0)
    {
        const struct rb_uv *ic = icon_from_side_meta(m, id, 1, meta);
        float angle = liquid_angle(m, x, y, z, mat);
        if ((double)angle > -999.0) ic = icon_from_side_meta(m, id, 2, meta);
        double u0, v0, u1, v1, u2, v2, u3, v3;
        if ((double)angle < -999.0)
        {
            u0 = (double)interp_u(ic, 0.0);
            v0 = (double)interp_v(ic, 0.0);
            u1 = u0;
            v1 = (double)interp_v(ic, 16.0);
            u2 = (double)interp_u(ic, 16.0);
            v2 = v1;
            u3 = u2;
            v3 = v0;
        }
        else
        {
            float s = mh_sin(m, angle) * 0.25F;
            float co = mh_cos(m, angle) * 0.25F;
            u0 = (double)interp_u(ic, (double)(8.0F + (-co - s) * 16.0F));
            v0 = (double)interp_v(ic, (double)(8.0F + (-co + s) * 16.0F));
            u1 = (double)interp_u(ic, (double)(8.0F + (-co + s) * 16.0F));
            v1 = (double)interp_v(ic, (double)(8.0F + (co + s) * 16.0F));
            u2 = (double)interp_u(ic, (double)(8.0F + (co + s) * 16.0F));
            v2 = (double)interp_v(ic, (double)(8.0F + (co - s) * 16.0F));
            u3 = (double)interp_u(ic, (double)(8.0F + (co - s) * 16.0F));
            v3 = (double)interp_v(ic, (double)(8.0F + (-co - s) * 16.0F));
        }
        tess_set_brightness(t, block_brightness(m, id, x, y, z));
        tess_set_color_opaque_f(t, f15 * r, f15 * g, f15 * b);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)y + h24, (double)(z + 0), u0, v0);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)y + h26, (double)(z + 1), u1, v1);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + h28, (double)(z + 1), u2, v2);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + h30, (double)(z + 0), u3, v3);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)y + h24, (double)(z + 0), u0, v0);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + h30, (double)(z + 0), u3, v3);
        tess_add_vertex_with_uv(t, (double)(x + 1), (double)y + h28, (double)(z + 1), u2, v2);
        tess_add_vertex_with_uv(t, (double)(x + 0), (double)y + h26, (double)(z + 1), u1, v1);
        return;
    }
    int i = f - 2, ax = x, az = z;
    if (i == 0) az = z - 1;
    if (i == 1) ++az;
    if (i == 2) ax = x - 1;
    if (i == 3) ++ax;
    const struct rb_uv *ic = icon_from_side_meta(m, id, i + 2, meta);
    double e, ff, g2, h2, i2, j2;
    if (i == 0) { e = h24; ff = h30; g2 = (double)x; i2 = (double)(x + 1); h2 = (double)z + d32; j2 = (double)z + d32; }
    else if (i == 1) { e = h28; ff = h26; g2 = (double)(x + 1); i2 = (double)x; h2 = (double)(z + 1) - d32; j2 = (double)(z + 1) - d32; }
    else if (i == 2) { e = h26; ff = h24; g2 = (double)x + d32; i2 = (double)x + d32; h2 = (double)(z + 1); j2 = (double)z; }
    else { e = h30; ff = h28; g2 = (double)(x + 1) - d32; i2 = (double)(x + 1) - d32; h2 = (double)z; j2 = (double)(z + 1); }
    float u0 = interp_u(ic, 0.0);
    float u1 = interp_u(ic, 8.0);
    float v0 = interp_v(ic, (1.0 - e) * 16.0 * 0.5);
    float v1 = interp_v(ic, (1.0 - ff) * 16.0 * 0.5);
    float v2 = interp_v(ic, 8.0);
    tess_set_brightness(t, block_brightness(m, id, ax, y, az));
    float f56 = 1.0F;
    f56 *= i < 2 ? f16 : f17;
    tess_set_color_opaque_f(t, f15 * f56 * r, f15 * f56 * g, f15 * f56 * b);
    tess_add_vertex_with_uv(t, g2, (double)y + e, h2, (double)u0, (double)v0);
    tess_add_vertex_with_uv(t, i2, (double)y + ff, j2, (double)u1, (double)v1);
    tess_add_vertex_with_uv(t, i2, (double)(y + 0), j2, (double)u1, (double)v2);
    tess_add_vertex_with_uv(t, g2, (double)(y + 0), h2, (double)u0, (double)v2);
    tess_add_vertex_with_uv(t, g2, (double)(y + 0), h2, (double)u0, (double)v2);
    tess_add_vertex_with_uv(t, i2, (double)(y + 0), j2, (double)u1, (double)v2);
    tess_add_vertex_with_uv(t, i2, (double)y + ff, j2, (double)u1, (double)v1);
    tess_add_vertex_with_uv(t, g2, (double)y + e, h2, (double)u0, (double)v0);
}

/* render_block_begin's words (the Tessellator's block, the bounds) */
static inline void mtab_begin(struct rb_mesher *m, int id, int meta, int x, int y, int z)
{
    struct rb_tess *t = m->t;
    t->cur_id = id;
    t->cur_meta = meta;
    t->cur_rt = BLOCKS[id].render_type;
    t->cur_x = x;
    t->cur_y = y;
    t->cur_z = z;
    set_bounds(m, id, meta);
}

#if !defined(__NVPTX__)
/* the host's half (table_host.c), for host.c: the table mode of one
 * mesher, on its module MOD (a CUmodule) and stream ST */
struct mtab_host;
struct mtab_host *meshing_tab_new(void *mod, void *st);
void meshing_tab_free(struct mtab_host *T);
/* the class table from the C renderer (every (id, meta) drawn on its own
 * in random worlds, each draw held to mtab_draw_check), made once from the
 * mesher's template T; without smooth lighting only (ao 0: no table) */
int meshing_tab_context(struct mtab_host *T, const struct rb_mesher *t);
/* a count pass of N requests: the launch's table fields (1: the
 * table mode on; 0: off, L's fields 0) */
int meshing_tab_prepare(struct mtab_host *T, struct meshing_launch *L, size_t n);
/* the table's count pass of the launch's N requests on ST (a
 * cudaStream_t), mesh_run's RUN and RUN_CUBE (CUfunctions) counting the
 * requests the table cannot mesh */
int meshing_tab_count(struct mtab_host *T, void *st, void **args, size_t n, void *run, void *run_cube);
/* the (id, meta) pairs of each class in the table (MTAB_CUBE, MTAB_GEN) */
void meshing_tab_counts(const struct mtab_host *T, int *cube, int *gen);
#endif

#endif
