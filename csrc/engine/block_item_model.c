#include "block_item_model.h"
#include "render_blocks_int.h"
#include "blocks.h"
#include "rbshare.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* The scene's block table and atlas, the process's (rbshare.h), only read; a
 * call meshes with a mesher and tessellator of its own, taken from a pool
 * (lane/guiact: GUI draws on several threads mesh block items at once; a
 * mesher's icon table is 6 MB, so they are kept, not made each call) */
static const struct rb_table *table;
static const struct rb_atlas *atlas;
static char loaded[1024];
static int generation;
static pthread_mutex_t load_lock = PTHREAD_MUTEX_INITIALIZER;

struct bim {
    struct rb_mesher mesher;
    struct rb_tess tess;
    int generation;
};
enum { BIM_POOL = 64 };
static struct bim *bim_pool[BIM_POOL];
static int bim_free_n;

static float bits_float(int32_t b)
{
    float f;
    memcpy(&f, &b, 4);
    return f;
}

static void bim_destroy(struct bim *b)
{
    rb_mesher_free(&b->mesher);
    rb_tess_free(&b->tess);
    free(b);
}

/* the scene's tables (load_lock held) */
static int load_locked(const char *scene)
{
    if (!strcmp(loaded, scene)) return 0;
    if (loaded[0])
        while (bim_free_n > 0) bim_destroy(bim_pool[--bim_free_n]);
    loaded[0] = 0;
    ++generation;
    table = rb_table_shared(scene);
    atlas = rb_atlas_shared(scene);
    if (table == NULL || atlas == NULL) return -1;
    snprintf(loaded, sizeof loaded, "%s", scene);
    return 0;
}

static int load(const char *scene)
{
    pthread_mutex_lock(&load_lock);
    int rc = load_locked(scene);
    pthread_mutex_unlock(&load_lock);
    return rc;
}

static struct bim *bim_get(const char *scene)
{
    pthread_mutex_lock(&load_lock);
    struct bim *b = NULL;
    int gen = generation;
    if (load_locked(scene) == 0) {
        gen = generation;
        if (bim_free_n > 0) b = bim_pool[--bim_free_n];
        else {
            b = calloc(1, sizeof *b);
            if (b && (rb_tess_init(&b->tess, 8192) ||
                      rb_mesher_init(&b->mesher, table, atlas, NULL, &b->tess, 0, 0, 0))) {
                bim_destroy(b);
                b = NULL;
            }
            if (b) b->generation = gen;
        }
    }
    pthread_mutex_unlock(&load_lock);
    return b;
}

static void bim_put(struct bim *b)
{
    pthread_mutex_lock(&load_lock);
    if (b->generation == generation && bim_free_n < BIM_POOL) bim_pool[bim_free_n++] = b;
    else bim_destroy(b);
    pthread_mutex_unlock(&load_lock);
}

static void add_face(struct bim *bm, struct block_item_model *out, int side, int grass)
{
    if (out->n >= (int)(sizeof out->quad / sizeof out->quad[0])) return;
    struct block_item_quad *q = &out->quad[out->n++];
    static const float normal[6][3] = {
        {0,-1,0}, {0,1,0}, {0,0,-1}, {0,0,1}, {-1,0,0}, {1,0,0}
    };
    memcpy(q->normal, normal[side], sizeof q->normal);
    q->tint_top = grass && side == 1;
    for (int i = 0; i < 4; ++i) {
        const int32_t *r = bm->tess.raw + i * 8;
        for (int j = 0; j < 3; ++j) q->p[i][j] = bits_float(r[j]);
        for (int j = 0; j < 2; ++j) q->uv[i][j] = bits_float(r[3 + j]);
    }
}

static void box(struct bim *bm, struct block_item_model *out, int id, int meta,
                double x0, double y0, double z0, double x1, double y1, double z1)
{
    struct rb_mesher *m = &bm->mesher;
    m->min_x = x0; m->min_y = y0; m->min_z = z0;
    m->max_x = x1; m->max_y = y1; m->max_z = z1;
    m->enable_ao = m->render_from_inside = 0;
    m->flip_texture = m->uv_east = m->uv_west = m->uv_south = 0;
    m->uv_north = m->uv_top = m->uv_bottom = 0;
    static void (*const face[6])(struct rb_mesher *, double, double, double,
                                  const struct rb_uv *) = {
        render_face_y_neg, render_face_y_pos, render_face_z_neg,
        render_face_z_pos, render_face_x_neg, render_face_x_pos
    };
    for (int side = 0; side < 6; ++side) {
        rb_tess_start_quads(&bm->tess);
        const struct rb_uv *ic = icon_from_side_meta(m, id, side, meta);
        face[side](m, -0.5, -0.5, -0.5, ic);
        if (bm->tess.vertex_count == 4) add_face(bm, out, side, id == 2);
    }
}

/* ModelBox's unfolded 64x64 chest texture, after the tile renderer's
 * translate/flip and RenderBlocks' 90 degree Y turn. Coordinates are sixteenths
 * of a block, as ModelChest.addBox records them. */
static void chest_face(struct block_item_model *out, const float p[4][3],
                       float u0, float v0, float u1, float v1,
                       float nx, float ny, float nz)
{
    struct block_item_quad *q = &out->quad[out->n++];
    q->normal[0] = -nz; q->normal[1] = -ny; q->normal[2] = -nx;
    const float uv[4][2] = {{u0,v1},{u0,v0},{u1,v0},{u1,v1}};
    for (int i = 0; i < 4; ++i) {
        q->p[i][0] = .5f - p[i][2] / 16.0f;
        q->p[i][1] = .5f - p[i][1] / 16.0f;
        q->p[i][2] = .5f - p[i][0] / 16.0f;
        q->uv[i][0] = uv[i][0] / 64.0f;
        q->uv[i][1] = uv[i][1] / 64.0f;
    }
}

static void chest_box(struct block_item_model *out, float x0, float y0, float z0,
                      float dx, float dy, float dz, int tx, int ty)
{
    float x1=x0+dx, y1=y0+dy, z1=z0+dz;
    float a=tx, b=a+dz, c=b+dx, d=c+dz, e=d+dx, f=e+dz;
    float g=ty, h=g+dz, i=h+dy;
    /* ModelBox's six TexturedQuad arrays and their UV rectangles. */
    const float east[4][3]={{x1,y1,z0},{x1,y0,z0},{x1,y0,z1},{x1,y1,z1}};
    const float west[4][3]={{x0,y1,z1},{x0,y0,z1},{x0,y0,z0},{x0,y1,z0}};
    const float top[4][3]={{x0,y0,z1},{x0,y0,z0},{x1,y0,z0},{x1,y0,z1}};
    const float bottom[4][3]={{x1,y1,z1},{x1,y1,z0},{x0,y1,z0},{x0,y1,z1}};
    const float south[4][3]={{x1,y1,z1},{x1,y0,z1},{x0,y0,z1},{x0,y1,z1}};
    const float north[4][3]={{x0,y1,z0},{x0,y0,z0},{x1,y0,z0},{x1,y1,z0}};
    chest_face(out,east,d,h,e,i,1,0,0);
    chest_face(out,west,a,h,b,i,-1,0,0);
    chest_face(out,top,b,g,c,h,0,-1,0);
    chest_face(out,bottom,c,g,d,h,0,1,0);
    chest_face(out,south,d,h,e,i,0,0,1);
    chest_face(out,north,b,h,c,i,0,0,-1);
    (void)f;
}

static int build(struct bim *bm, int id, int meta, struct block_item_model *out);

int block_item_model_build(const char *scene, int id, int meta,
                           struct block_item_model *out)
{
    memset(out, 0, sizeof *out);
    if (id <= 0 || id >= 4096 || !BLOCKS[id].exists) return -1;
    struct bim *bm = bim_get(scene);
    if (bm == NULL) return -1;
    int rc = build(bm, id, meta, out);
    bim_put(bm);
    return rc;
}

static int build(struct bim *bm, int id, int meta, struct block_item_model *out)
{
    int rt = BLOCKS[id].render_type;
    out->render_pass = table->props[id].render_pass;
    if (id == 61 || id == 23 || id == 158) meta = 3; /* furnace/dispenser/dropper */
    if (rt == 16) meta = 1;
    if (rt == 0 || rt == 31 || rt == 39 || rt == 16 || rt == 26) {
        out->inner_rotate_y90 = 1;
        /* renderBlockAsItem calls setBlockBoundsForItemRender first: the
         * overrides of the render-type-0 blocks (BlockSlab, BlockSnow,
         * BlockCarpet, BlockTrapDoor, BlockButton, BlockBasePressurePlate) */
        double b[6] = {0, 0, 0, 1, 1, 1};
        if (id == 44 || id == 126) b[4] = 0.5;
        else if (id == 78) b[4] = 0.125;
        else if (id == 171) b[4] = 0.0625;
        else if (id == 96) { b[1] = 0.40625; b[4] = 0.59375; }
        else if (id == 77 || id == 143) {
            b[0] = 0.3125; b[1] = 0.375; b[2] = 0.375; b[3] = 0.6875; b[4] = 0.625; b[5] = 0.625;
        }
        else if (id == 70 || id == 72 || id == 147 || id == 148) { b[1] = 0.375; b[4] = 0.625; }
        box(bm, out, id, meta, b[0], b[1], b[2], b[3], b[4], b[5]);
    } else if (rt == 13) {
        /* the cactus: a full box whose four sides the Tessellator's
         * addTranslation moves a sixteenth in */
        box(bm, out, id, 0, 0, 0, 0, 1, 1, 1);
        for (int i = 0; i < out->n; ++i) {
            struct block_item_quad *q = &out->quad[i];
            int axis = q->normal[0] != 0 ? 0 : q->normal[2] != 0 ? 2 : -1;
            if (axis < 0) continue;
            float shift = q->normal[axis] < 0 ? 0.0625f : -0.0625f;
            for (int k = 0; k < 4; ++k) q->p[k][axis] += shift;
        }
    } else if (rt == 10) {
        box(bm, out, id, meta, 0, 0, 0, 1, 1, 0.5);
        box(bm, out, id, meta, 0, 0, 0.5, 1, 0.5, 1);
    } else if (rt == 11) {
        box(bm, out, id, meta, .375, 0, 0, .625, 1, .25);
        box(bm, out, id, meta, .375, 0, .75, .625, 1, 1);
        box(bm, out, id, meta, .4375, .8125, -.125, .5625, .9375, 1.125);
        box(bm, out, id, meta, .4375, .3125, -.125, .5625, .4375, 1.125);
    } else if (rt == 32) {
        box(bm, out, id, meta, 0, 0, .3125, 1, .8125, .6875);
        box(bm, out, id, meta, .25, 0, .25, .75, 1, .75);
    } else if (rt == 22) {
        /* TileEntityRendererChestHelper's three chests: 1 normal, 2 ender, 3 trapped */
        out->chest_texture = id == 130 ? 2 : id == 146 ? 3 : 1;
        chest_box(out, 1, 2, 1, 14, 5, 14, 0, 0);
        chest_box(out, 7, 5, 0, 2, 4, 1, 0, 0);
        chest_box(out, 1, 6, 1, 14, 10, 14, 0, 19);
    } else {
        return -1;
    }
    return out->n ? 0 : -1;
}

int block_item_model_grass_color(const char *scene)
{
    if (load(scene) || !table->grass_map) return -1;
    return (int)(table->grass_map[127 << 8 | 127] & 0xffffff);
}

int block_item_model_override_box(const char *scene, const char *icon,
                                  double x0, double y0, double z0,
                                  double x1, double y1, double z1,
                                  struct block_item_model *out)
{
    if (load(scene)) return -1;
    const struct rb_uv *ic = NULL;
    for (int i = 0; i < atlas->n; ++i)
        if (!strcmp(atlas->name[i], icon)) { ic = &atlas->uv[i]; break; }
    if (!ic) return -1;
    struct bim *b = bim_get(scene);
    if (b == NULL) return -1;
    struct rb_mesher *m = &b->mesher;
    m->min_x = x0; m->min_y = y0; m->min_z = z0;
    m->max_x = x1; m->max_y = y1; m->max_z = z1;
    m->enable_ao = m->render_from_inside = 0;
    m->flip_texture = m->uv_east = m->uv_west = m->uv_south = 0;
    m->uv_north = m->uv_top = m->uv_bottom = 0;
    static void (*const face[6])(struct rb_mesher *, double, double, double,
                                  const struct rb_uv *) = {
        render_face_y_neg, render_face_y_pos, render_face_z_neg,
        render_face_z_pos, render_face_x_neg, render_face_x_pos
    };
    for (int side = 0; side < 6; ++side) {
        rb_tess_start_quads(&b->tess);
        face[side](m, 0.0, 0.0, 0.0, ic);
        if (b->tess.vertex_count == 4) add_face(b, out, side, 0);
    }
    bim_put(b);
    return 0;
}
