/* The device mesher's interface (cuda/meshing): what the host sends the
 * kernel in kernels.c and what comes back. Plain C, read by the device C
 * (clang, nvptx64), the host C (meshfeed users) and the renderer (nvcc).
 *
 * The device holds a copy of each chunk record a request reads, laid out
 * as the engine's own struct chunk and struct chunk_sec (world.h: a band
 * named by its offset from its chunk), so render_blocks*.c's accessors run
 * on it unchanged; the host writes those copies byte for byte
 * (mesh_copy). A request is one section pass: the section, the camera's
 * block (RenderBlocks' double draw of the block the camera is in), the
 * 3x3 chunks around it (all a section's mesh reads: at most two blocks past
 * its edges), the flower pots' plants its cells hold, and the libm values
 * its double plants need (render_blocks.h rb_trig). */
#ifndef NETHERITE_MESHING_KERNELS_H
#define NETHERITE_MESHING_KERNELS_H

#include <stdint.h>

/* the warps that mesh one request together (a block) */
enum { MESHING_WARPS = 4, MESHING_PK = 512 };
/* the spread count pass's lanes an SM (host.c lcap) */
enum { MESHING_LANES_SM = 256 };
/* mesh_fix's warps a block (its registers are held to 128: 512 threads) */
enum { MESHING_FIX_WARPS = 16 };
/* the spread count pass (kernels.c mesh_class and the kernels after it):
 * each lane's record, in 32-bit words */
enum { MESHING_LANE_WORDS = 320, MESHING_NKEY = 256, MESHING_CLASS_THREADS = 256, MESHING_MASK_WORDS = 640 };

struct meshing_req {
    int32_t cx, s, cz, pass;
    int32_t px, py, pz, no_sky;
    uint64_t chunk[9];          /* device struct chunk of (cx - 1 + i / 3, cz - 1 + i % 3); 0: none */
    uint64_t te;                /* device struct rb_te [te_n] */
    uint64_t trig;              /* device struct rb_trig [ntrig], sorted (render_blocks.h rb_trig_cmp) */
    uint64_t out;               /* the write pass: int32 [cap * 8]; 0: the count pass */
    int32_t te_n, ntrig, cap;
    int32_t key;                /* the request's entry in the launch's voff (both passes) */
};

/* count: vertices; flags: the Tessellator's (texture 1, color 2,
 * brightness 4, normals 8); err: MESHING_ERR_*; pad: the lanes' redraws,
 * and MESHING_RES_REDRAW when the count pass left the request's vertices
 * out of vbuf (the write pass draws it: mesh_run) */
struct meshing_res { int32_t count, flags, err, pad; };
enum { MESHING_RES_REDRAW = 1 << 30 };
/* MESHING_ERR_STATE: a draw changed a mesher word outside the ones the lanes
 * hand on (kernels.c REG0..REG1, override): the runs are not the host's */
/* MESHING_ERR_MARK: a setter wrote kernels.c's mark (a vertex copied from a
 * lane's own buffer would be wrong) */
enum { MESHING_ERR_TRIG = 1, MESHING_ERR_FULL = 2, MESHING_ERR_STATE = 4, MESHING_ERR_MARK = 8 };

struct meshing_launch {
    uint64_t req, res;          /* device struct meshing_req [n], struct meshing_res [n] */
    uint64_t mesher;            /* device struct rb_mesher: the template each request starts from */
    uint64_t trig;              /* device struct rb_trig [ntrig]: the liquids' and brewing stands', sorted */
    uint64_t scratch;           /* device struct rb_mesher (mesh_setup's) */
    /* the count pass writes each request's vertices into vbuf when they fit
     * (vcur counts the vertices taken) and where into voff[key] (-1: not
     * written); the write pass then copies them */
    uint64_t vbuf;              /* device int32 [vcap * 8]; 0: none */
    uint64_t vcur;              /* device uint64 */
    uint64_t voff;              /* device int64 [keys] */
    int64_t vcap;
    /* the count pass draws each lane's run into a buffer of MESHING_PK
     * vertices of the pool, one slot a resident block (nslot of them, taken
     * and given back through the bitmap slots) */
    uint64_t pool;              /* device int32 [nslot * 32 * MESHING_WARPS * MESHING_PK * 8]; 0: none */
    uint64_t slots;             /* device uint32 [(nslot + 31) / 32], zero between launches */
    int32_t nslot, pad;
    int32_t n, ntrig;
    /* the spread count pass: the requests' cells shared out over lcap lanes
     * of the whole launch, each request's lanes in proportion to its work;
     * lane k draws into the pool's buffer k */
    uint64_t masks;             /* device uint32 [n][MESHING_MASK_WORDS]: each request's cells to visit, those that draw, those of
                                 * a render type other than 0, the hidden cubes that set the brightness (kernels.c mesh_class),
                                 * then each word's weight before it (mesh_wsum) */
    uint64_t plan;              /* device int32 [3 * n + 1]: weights, cells, then each request's first lane (n + 1) */
    uint64_t lanes;             /* device uint32 [lcap][MESHING_LANE_WORDS] */
    uint64_t ckey;              /* device uint8 [n][4096]: each cell's draw kind (kernels.c cell_key) */
    uint64_t perm;              /* device int32 [lcap] the lanes in key order, then int32 [MESHING_NKEY] the keys' counts */
    uint64_t bands;             /* device struct rb_band [n][27]: each request's band table (render_blocks.h) */
    uint64_t spec0;             /* device struct rb_uv [all the specials]: the mesher's special icons as they were made */
    int32_t lcap, pad2;
    uint64_t cut;               /* device uint32 [3][lcap]: each planned lane's request and run, then each mesh_cut block's first
                                 * place among the kept lanes (kernels.c mesh_cut, mesh_pack) */
    /* a count launch of mesh_run blocks takes its requests in the order
     * mesh_order wrote (the plan's third part), heaviest first */
    int32_t ordered, pad3;
    /* the table mode (table.h), in a count pass: tab 0 off */
    uint64_t tab;               /* device uint8 [RB_IDS * RB_METAS]: each (id, meta)'s table class; then struct rb_uv [RB_IDS]: a door's upper icon;
                                 * then int32 [RB_IDS * RB_METAS * RB_SIDES]: the icons (table.h mtab_icon) */
    uint64_t tidm;              /* device uint16 [n][4096]: a table cell's id | meta << 12, 0 for another (mesh_tab_list) */
    uint64_t tcell;             /* device uint64 [n][4096]: a table cell's first vertex in tvbuf | count << 40 */
    uint64_t tvbuf;             /* device int32 [tvcap * 8]: the table cells' vertices */
    uint64_t tvcur;             /* device uint64: the vertices taken */
    int64_t tvcap;
    uint64_t tflag;             /* device int32 [n]: MTAB_FULL, MTAB_REDO, the table's Tessellator flags << 8 */
    uint64_t ttot;              /* device int32 [n]: the table cells' vertices */
    uint64_t tlist;             /* device int32 [MTAB_CLASSES][n * 4096]: each class's table cells, request << 12 | cell */
    uint64_t tlcnt;             /* device int32 [MTAB_CLASSES]: their counts */
    uint64_t tcache;            /* device uint32 [n][RB_CACHE^3]: each request's world around its section (rb_world cache) */
    uint64_t tbcache;           /* device uint8 [n][RB_CACHE^2]: and its biomes */
    uint64_t tbands;            /* device struct rb_band [n][27]: each request's band table (mesh_tab_cache's) */
    uint64_t toff;              /* device int32 [n][4096]: each cell's first vertex in its request (mesh_tab_place's) */
    /* the spread count pass: each request's world around its section as the
     * table mode caches it (rb_world cache, bcache) and its columns' noise
     * (ncache), made by mesh_class for the drawing lanes; 0 none */
    uint64_t wcache;            /* device char [n][MESHING_WC_BYTES] */
};
/* a request's wcache: uint32 [RB_CACHE^3] (render_blocks.h), uint8 [RB_CACHE^2], double [2][RB_CACHE^2] */
enum { MESHING_WC_BIOME = 32000, MESHING_WC_NOISE = 32400, MESHING_WC_BYTES = 38912 };

/* One upload: LEN bytes of the staged block at SRC to device address DST. */
struct meshing_copy { uint64_t dst, src, len; };

#endif
