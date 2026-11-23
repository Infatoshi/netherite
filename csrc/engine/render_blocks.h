/* The 1.7.10 chunk mesher in C: RenderBlocks into a Tessellator, emitting the
 * same 8 ints per vertex in the same order as the oracle's MeshProbe, fed from
 * the same probe directory (manifest.json, sections.bin, atlas.json, chunks.bin,
 * table.bin).
 *
 * Ported render types: 0 standard blocks (ambient occlusion, its partial-bounds
 * variant, and the colour-multiplier variant), 1 crossed squares, 4 fluids,
 * 31 logs, 40 double plants. Every other render type draws nothing, which is
 * what RenderBlocks.renderBlockByRenderType returns for the types outside its
 * dispatch chain (22 chests are drawn by a tile entity renderer, for one).
 *
 * The tables are the oracle's own: table.bin holds getIcon(side, meta) for every
 * block, the render properties, the block bounds setBlockBoundsBasedOnState
 * leaves behind, the grass and foliage colormaps and the biome colour kinds;
 * chunks.bin holds the world the oracle meshed; atlas.json holds each icon's uv
 * range. Nothing here is transcribed by hand.
 */
#ifndef NETHERITE_RENDER_BLOCKS_H
#define NETHERITE_RENDER_BLOCKS_H

#include <stddef.h>
#include <stdint.h>

#include "noise.h"

enum {
    RB_IDS = 4096,
    RB_METAS = 16,
    RB_SIDES = 6,
    RB_NO_ICON = 0xffff,
    RB_BIOMES = 256,
    RB_SPECIALS = 16,
    RB2_SPECIALS = 6,
    RB3_SPECIALS = 35
};

/* the fixed special-icon slots of table.bin */
enum {
    RB_ICON_GRASS_SNOWED = 0,
    RB_ICON_GRASS_OVERLAY = 1,
    RB_ICON_DP_LOW = 2,
    RB_ICON_DP_HIGH = 8,
    RB_ICON_DP_FLOWER = 14
};

/* the biome colour kinds */
enum { RB_KIND_MAP = 0, RB_KIND_ROOFED = 1, RB_KIND_SWAMP = 2, RB_KIND_CONST = 3, RB_KIND_MUTATED = 4 };

/* chunks.bin: per chunk, int32 cx, int32 cz, Probe.java's chunk bytes, then the
 * chunk's 256 biome ids. */
enum {
    RB_CHUNK_META = 2 * 65536,       /* the ids are 2 bytes each, first */
    RB_CHUNK_SKY = 3 * 65536,
    RB_CHUNK_BLOCK = 4 * 65536,
    RB_CHUNK_HEIGHT = 5 * 65536,
    RB_CHUNK_BYTES = RB_CHUNK_HEIGHT + 2 * 256 * 4 + 4 + 2,
    RB_CHUNK_BIOMES = RB_CHUNK_BYTES,
    RB_CHUNK_RECORD = 8 + RB_CHUNK_BYTES + 256
};

struct rb_props {
    uint8_t can_block_grass, render_as_normal, neighbor, render_pass;
    uint8_t opaque_cube, is_slab, light, pad;
    float ao_light;               /* getAmbientOcclusionLightValue() */
};

struct rb_biome {
    float temperature, rainfall;
    int32_t water;                /* waterColorMultiplier */
    int32_t grass_const, foliage_const;
    uint8_t grass_kind, foliage_kind;
    int16_t base;                 /* a mutation's base slot, else -1 */
};

struct rb_table {
    int fancy, ao;                /* graphics options from this recording's manifest */
    int n_icons;
    char **icon_name;
    int n_special;
    char **special;
    uint16_t *icon_index;         /* RB_IDS * RB_METAS * RB_SIDES */
    uint32_t *grass_map;          /* 65536 */
    uint32_t *foliage_map;        /* 65536 */
    struct rb_props *props;       /* RB_IDS */
    const double *bounds;         /* RB_IDS * RB_METAS * 6 */
    struct rb_biome *biomes;      /* RB_BIOMES */
    float *sin_table;             /* 65536, MathHelper.SIN_TABLE */
};

/* an icon's uv range; the atlas lookup resolves a name to one of these */
struct rb_uv { float min_u, max_u, min_v, max_v; };

struct rb_atlas {
    int n;
    char **name;
    struct rb_uv *uv;
};

struct chunk;

/* the chunks the oracle meshed, exactly as chunks.bin lays them out; or
 * (chunks set, the playable client) a window of chunks in the engine's own
 * form, NULL where a record would be zeros */
struct world;

struct rb_world {
    uint8_t *data;
    struct chunk **chunks;        /* rows * rows, [dx * rows + dz] */
    size_t stride;                /* RB_CHUNK_RECORD */
    int margin;                   /* radius + 1 */
    int rows;                     /* 2 * margin + 1 */
    int origin_cx, origin_cz;     /* the camera chunk */

    /* tile entities the mesher's renderers read (ChunkCache.getTileEntity);
     * a scene's tileentities.bin, absent for the plain MeshProbe runs */
    struct rb_te { int32_t x, y, z, block, data; } *te;
    int te_n;
    /* WorldProvider.hasNoSky (the Nether, the End): ChunkCache's sky
     * brightness answers 0 before it reads a chunk */
    int no_sky;
    /* the live client's world, whose tile entities ChunkCache.getTileEntity
     * reads when there is no te list (a flower pot's plant) */
    struct world *live;
    /* the device mesher's band table (cuda/meshing), NULL elsewhere: for the
     * 3x3 chunks around the origin and sections band_s0 .. band_s0 + 2,
     * each band and what its chunk holds (render_blocks.c rb_band) */
    const struct rb_band *bands;
    int band_s0;
#if defined(__NVPTX__)
    /* the device mesher's table mode (cuda/meshing table.c), NULL
     * elsewhere: the accessors' answers for the RB_CACHE cube from
     * cache_x0, cache_y0, cache_z0 (id | meta << 12 | sky << 16 | block
     * light << 20, x then z then y), and rb_world_biome's for its columns
     * (x then z), made once a section from the accessors themselves */
    const uint32_t *cache;
    const uint8_t *bcache;
    int cache_x0, cache_y0, cache_z0;
    /* the device mesher's spread pass (cuda/meshing kernels.c mesh_class),
     * NULL elsewhere: rb_column_noise's answers for the cache's columns
     * (x then z), perlin_a's then perlin_b's */
    const double *ncache;
    /* the band table's first block in x, y and z (band_of: (origin_cx - 1)
     * * 16, band_s0 * 16, (origin_cz - 1) * 16), set with bands */
    int band_x0, band_y0, band_z0;
#endif
};
enum { RB_CACHE = 20 };
/* a band of the device mesher's table: kind 0 no chunk, 1 a chunk whose
 * mask holds the section, 2 one whose mask does not; band its storage, NULL
 * for none */
struct chunk_sec;
struct rb_band {
    const struct chunk_sec *band;
    int32_t kind, pad;
};

/* the Tessellator: 8 ints per vertex, x, y, z, u, v, color, normal, brightness */
struct rb_tess {
    int32_t *raw;
    int cap, n;
    int vertex_count;
    double u, v, xoff, yoff, zoff;
    int32_t color, brightness, normal;
    int has_texture, has_color, has_brightness, has_normals;
    int32_t *dbg;                 /* 4 ints per vertex: block id, meta, render type, 0 */
    int cur_id, cur_meta, cur_rt; /* the block the next vertices come from */
    int cur_x, cur_y, cur_z;
    int dropped;                  /* vertices past the buffer, for a loud failure */
};

struct rb_mesher {
    const struct rb_table *tab;
    const struct rb_atlas *atlas;
    const struct rb_world *w;
    struct rb_tess *t;
    /* BiomeGenBase.field_150605_ac / field_150606_ad, rb_mesher_init's own
     * (behind pointers: a device mesher copies the rest per section) */
    const struct perlin *perlin_a, *perlin_b;
    int px, py, pz;                     /* the camera block, for the inside-the-block draw */

    /* setRenderBoundsFromBlock / setBlockBoundsBasedOnState */
    double min_x, max_x, min_y, max_y, min_z, max_z;
    int partial;
    int enable_ao, render_all_faces, render_from_inside;
    int flip_texture, f152631f;
    int uv_east, uv_west, uv_south, uv_north, uv_top, uv_bottom;
    int anvil_part;                     /* BlockAnvil.field_149833_b while its parts draw */
    double piston_bounds[2][6];         /* BlockPistonBase's stored bounds (id 33, 29), which
                                         * setBlockBoundsBasedOnState and func_150070_b move */

    /* the face colour and brightness the last face computed */
    int32_t bright_tl, bright_bl, bright_br, bright_tr;
    float r_tl, g_tl, b_tl, r_bl, g_bl, b_bl, r_br, g_br, b_br, r_tr, g_tr, b_tr;

    float l_xyz_nnn, l_xy_nn, l_xyz_nnp, l_yz_nn, l_yz_np, l_xyz_pnn, l_xy_pn, l_xyz_pnp;
    float l_xyz_npn, l_xy_np, l_xyz_npp, l_yz_pn, l_xyz_ppn, l_xy_pp, l_yz_pp, l_xyz_ppp;
    float l_xz_nn, l_xz_pn, l_xz_np, l_xz_pp;
    int32_t b_xyz_nnn, b_xy_nn, b_xyz_nnp, b_yz_nn, b_yz_np, b_xyz_pnn, b_xy_pn, b_xyz_pnp;
    int32_t b_xyz_npn, b_xy_np, b_xyz_npp, b_yz_pn, b_xyz_ppn, b_xy_pp, b_yz_pp, b_xyz_ppp;
    int32_t b_xz_nn, b_xz_pn, b_xz_np, b_xz_pp;

    int mat_snow, mat_crafted_snow;     /* Material.field_151597_y, Material.craftedSnow */
    int grass_top_slot, grass_side_uv;  /* resolved for the fancyGrass overlay */
    /* table.bin's special icons (RB_SPECIALS), resolved; special2 and
     * special3 follow them in the same block (rb_mesher_init): read-only
     * tables behind pointers, so a copy of the mesher (the device mesher's,
     * one a lane) holds only the state a draw changes */
    struct rb_uv *special;
    struct rb_uv *icon;                 /* RB_IDS * RB_METAS * RB_SIDES, resolved from tab + atlas */
    struct rb_uv missingno;             /* the atlas's missing sprite; has_missingno when found */
    int has_missingno;

    /* render_blocks2.c: the fire and redstone dust icon names, resolved from
     * atlas.json by name (table.bin only holds getIcon(side, meta) icons), and
     * the override texture setOverrideBlockTexture replaces face icons with */
    struct rb_uv *special2;             /* RB2_SPECIALS */
    const struct rb_uv *override;

    /* the device mesher's context (cuda/meshing: rb_cos and the others read
     * the host's libm values there); NULL on the host */
    const void *dev;

    /* render_blocks3.c: the icons table.bin cannot hold because getIcon never
     * returns them or reads mutable state (piston and anvil parts, pane tops,
     * cauldron inner, brewing base, end frame eye, hopper, stem connected) */
    struct rb_uv *special3;             /* RB3_SPECIALS */
};

/* Load the probe directory's four files into t, a, w (t and a own their memory).
 * Returns 0 on success, -1 with a message on stderr otherwise. */
int rb_table_load(struct rb_table *t, const char *dir);
void rb_table_free(struct rb_table *t);
int rb_atlas_load(struct rb_atlas *a, const char *dir);
void rb_atlas_free(struct rb_atlas *a);
int rb_world_load(struct rb_world *w, const char *dir);
void rb_world_free(struct rb_world *w);

/* manifest.json: the camera chunk, the camera block, the radius, and the
 * graphics settings the meshes were built under (ao 2 and fancy, checked). */
int rb_manifest_load(const char *dir, int *radius, int *cx, int *cz, int *px, int *py, int *pz);

int rb_mesher_init(struct rb_mesher *m, const struct rb_table *tab, const struct rb_atlas *a,
                   const struct rb_world *w, struct rb_tess *t, int px, int py, int pz);
void rb_mesher_free(struct rb_mesher *m);

/* One chunk section and one pass, the way WorldRenderer.updateRenderer builds
 * it: the pass test and the y/z/x block order, plus the double draw of the block
 * the camera sits inside. t holds the vertices afterwards. */
void rb_mesh_section(struct rb_mesher *m, int cx, int cz, int section, int pass);
/* The pass rb_mesh_section draws block ID in (rb_mesh_cell_kind's test), -1
 * for air, which no pass draws. */
int rb_block_pass(const struct rb_table *tab, int id);
/* rb_mesh_section's parts, for a mesher that walks the cells itself (the
 * device's, cuda/meshing): the Tessellator started; a cell's kind (0 nothing
 * to do: air or the other pass; 1 a hidden cube, which only leaves its
 * state; 2 a draw; its block into *id and *meta), which reads the mesher's
 * state only through render_all_faces (0 outside a block's draw); and the
 * cell's work for its kind. rb_mesh_section is begin, then kind and cell
 * for every cell in order. */
void rb_mesh_section_begin(struct rb_mesher *m, int cx, int cz, int section);
int rb_mesh_cell_kind(struct rb_mesher *m, int x, int y, int z, int pass, int *id, int *meta);
void rb_mesh_cell(struct rb_mesher *m, int kind, int id, int meta, int x, int y, int z);
/* rb_mesh_cell for kind 1 and the blocks of render type 0 alone (the
 * device's cube kernel reaches no other render type's code) */
void rb_mesh_cell_standard(struct rb_mesher *m, int kind, int id, int meta, int x, int y, int z);
#if defined(__NVPTX__)
/* the device mesher's guess of the state a cell's draw leaves
 * (render_blocks.c) */
void rb_mesh_guess_cell(struct rb_mesher *m, int kind, int id, int meta, int x, int y, int z);
#endif
#if defined(__NVPTX__)
/* the column noise a biome's temperature (WHICH 0: perlin_a) or a swamp's
 * colour (1: perlin_b) reads at (x, z), from the world's ncache when it
 * holds the column */
double rb_column_noise(const struct rb_mesher *m, int which, int x, int z);
#endif
/* 0: rb_mesh_section draws every hidden cube too (the shortcut's check) */
extern int rb_mesh_hidden_fast;
/* RenderBlocks.renderBlockUsingTexture for the destroy progress pass. */
void rb_mesh_override_block(struct rb_mesher *m, int x, int y, int z,
                            const struct rb_uv *icon);

/* RenderFallingBlock's RenderBlocks half for a falling anvil (145, with the
 * entity's metadata) or dragon egg (122) at cell (x, y, z): the quads into t,
 * relative to the cell's centre. */
void rb_mesh_falling_block(struct rb_mesher *m, struct rb_tess *t, int id, int meta, int x, int y, int z);

/* The libm calls the mesher makes (cos, sin, atan2), each with its
 * argument and glibc's value: a device mesher (cuda/meshing) cannot reproduce
 * glibc's roundings, so it looks every value up among these. The arguments
 * are few: the liquids' flow angle takes atan2 of a normalized sum of level
 * differences (rb_trig_liquid, every sum -15..15 in x and z, solid
 * neighbours or not), a sunflower's head the cos and sin of its column's
 * hash (rb_trig_double_plant), a brewing stand's arms three constant
 * angles (rb_trig_brewing). Each returns how many it wrote. */
enum { RB_TRIG_COS = 0, RB_TRIG_SIN = 1, RB_TRIG_ATAN2 = 2 };
struct rb_trig { int32_t fn, pad; double a, b, v; };   /* fn(a) or atan2(a, b) = v */
/* the order a table of them is sorted in: fn, then a's bits, then b's */
static inline int rb_trig_cmp(const struct rb_trig *x, const struct rb_trig *y)
{
    union { double d; uint64_t u; } xa = {x->a}, xb = {x->b}, ya = {y->a}, yb = {y->b};
    if (x->fn != y->fn) return x->fn < y->fn ? -1 : 1;
    if (xa.u != ya.u) return xa.u < ya.u ? -1 : 1;
    if (xb.u != yb.u) return xb.u < yb.u ? -1 : 1;
    return 0;
}
int rb_trig_liquid(int vx, int vz, int solid, struct rb_trig *out);
int rb_trig_double_plant(int x, int z, struct rb_trig out[3]);
int rb_trig_brewing(struct rb_trig out[6]);

/* BiomeGenBase.getFloatTemperature at (x, y, z), with the biome the world holds
 * at (x, z): the value World.getSkyColor passes to getSkyColorByTemp. */
float rb_biome_temperature(struct rb_mesher *m, int x, int y, int z);

/* Tessellator */
/* Tessellator.instance's buffer: 2097152 ints, 262,144 vertices. One section
 * pass meshes into one buffer; vanilla draws a full buffer and goes on
 * (addVertex at rawBufferIndex >= bufferSize - 32), which the mesher does
 * not do: a pass past it drops, and the caller fails loudly. A smaller
 * buffer failed on passes vanilla draws in one (tnt-cannon-s1: 3,739
 * cauldrons in one section, about 164,000 vertices). */
#define RB_TESS_CAP 2097152
int rb_tess_init(struct rb_tess *t, int cap);
void rb_tess_free(struct rb_tess *t);
void rb_tess_start_quads(struct rb_tess *t);
void rb_tess_set_translation(struct rb_tess *t, double x, double y, double z);

/* The world accessors, ChunkCache's semantics: air outside the loaded chunks. */
int rb_world_block(const struct rb_world *w, int x, int y, int z);
int rb_world_meta(const struct rb_world *w, int x, int y, int z);
int rb_world_sky(const struct rb_world *w, int x, int y, int z);
int rb_world_blocklight(const struct rb_world *w, int x, int y, int z);
int rb_world_biome(const struct rb_world *w, int x, int z);

/* World.getPrecipitationHeight at (x, z): the chunk's cached column, computed
 * from the blocks when the cache is still -999. -1 outside the loaded chunks. */
int rb_world_precipitation_height(const struct rb_world *w, int x, int z);

/* The tile entity at (x, y, z): 1 and the ItemBlock's block id and data for
 * the flower pot renderer, 0 when there is none. */
int rb_world_tile_entity(const struct rb_world *w, int x, int y, int z, int *block, int *data);

#endif
