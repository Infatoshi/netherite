/* The pathfinder, ported from oracle/src/pathfinding/PathFinder.java, Path.java,
 * PathPoint.java, PathEntity.java and World's two entry points. See
 * pathfind.h for the layout and the vanilla quirks the port keeps. */
#include "pathfind.h"
#include "env.h"
#include "blocks.h"
#include "jmath.h"
#include "world.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* Block ids, from Blocks. */
enum {
    ID_FLOWING_WATER = 8,
    ID_WATER = 9,
    ID_FLOWING_LAVA = 10,
    ID_LAVA = 11,
    ID_WOODEN_DOOR = 64,
    ID_TRAPDOOR = 96,
    ID_FENCE_GATE = 107
};

/* ------------------------------------------------------------ the entity */

/* Entity.setPosition: the box of an entity that is never moved afterwards. */
void pf_entity_set_position(struct pf_entity *e, double x, double y, double z)
{
    e->pos_x = x;
    e->pos_y = y;
    e->pos_z = z;
    float hw = e->width / 2.0F;
    e->box.min_x = x - (double)hw;
    e->box.min_y = y - (double)e->y_offset + (double)e->y_size;
    e->box.min_z = z - (double)hw;
    e->box.max_x = x + (double)hw;
    e->box.max_y = y - (double)e->y_offset + (double)e->y_size + (double)e->height;
    e->box.max_z = z + (double)hw;
}

/* ------------------------------------------------------------ block reads */

static int block_at(struct world *w, int x, int y, int z)
{
    return world_get_block(w, x, y, z) & 4095;
}

static int meta_at(struct world *w, int x, int y, int z)
{
    return world_get_meta(w, x, y, z);
}

/* BlockDoor.getBlocksMovement: func_150012_g over the world reads the lower
 * half's metadata and the override answers its OPEN bit. So an open door
 * reports true and a closed one false, which sends a closed door (wooden or
 * iron) into getVerticalOffset's blocking branch and an open one past it. */
static int door_blocks_movement(struct world *w, int x, int y, int z)
{
    int meta = meta_at(w, x, y, z);

    if ((meta & 8) != 0) meta = meta_at(w, x, y - 1, z);

    return (meta & 4) != 0;
}

/* BlockFenceGate.getBlocksMovement: isFenceGateOpen(meta). */
static int fence_gate_blocks_movement(struct world *w, int x, int y, int z)
{
    return (meta_at(w, x, y, z) & 4) != 0;
}

/* BlockTrapDoor.getBlocksMovement: !func_150118_d(meta); a closed trapdoor
 * reports true (the walk path) and an open one takes the blocking branch. */
static int trapdoor_blocks_movement(struct world *w, int x, int y, int z)
{
    return (meta_at(w, x, y, z) & 4) == 0;
}

/* getBlocksMovement and getVerticalOffset ran a class_name strcmp chain and a
 * material-name strcmp per world block, over 600M times on a 20k-case path
 * check. These resolve each block id once into an integer class and cache the
 * air and lava material indices. */
enum { PFM_DEFAULT, PFM_DOOR, PFM_PASS, PFM_FENCE_GATE, PFM_TRAPDOOR, PFM_LIQUID, PFM_PLATE };
static signed char pf_move_class[4096];
static int pf_mat_air = -1, pf_mat_lava = -1;

/* before main: the same for every environment */
__attribute__((constructor)) static void pf_build_tables(void)
{
    for (int i = 0; i < 33; ++i)
    {
        const char *m = MATERIALS[i].name;
        if (m == NULL) continue;
        if (strcmp(m, "air") == 0) pf_mat_air = i;
        else if (strcmp(m, "lava") == 0) pf_mat_lava = i;
    }
    for (int id = 0; id < 4096; ++id)
    {
        const char *c = BLOCKS[id].class_name;
        int k = PFM_DEFAULT;
        if (c != NULL)
        {
            if (strcmp(c, "BlockDoor") == 0) k = PFM_DOOR;
            else if (strcmp(c, "BlockFence") == 0 || strcmp(c, "BlockWall") == 0) k = PFM_PASS;
            else if (strcmp(c, "BlockFenceGate") == 0) k = PFM_FENCE_GATE;
            else if (strcmp(c, "BlockTrapDoor") == 0) k = PFM_TRAPDOOR;
            else if (strcmp(c, "BlockDynamicLiquid") == 0 || strcmp(c, "BlockStaticLiquid") == 0) k = PFM_LIQUID;
            else if (strcmp(c, "BlockPressurePlate") == 0 || strcmp(c, "BlockPressurePlateWeighted") == 0 ||
                     strcmp(c, "BlockSign") == 0) k = PFM_PLATE;
        }
        pf_move_class[id] = (signed char)k;
    }
}

/* Block.getBlocksMovement, with every override getVerticalOffset reaches. */
static int blocks_movement(struct world *w, int id, int x, int y, int z)
{
    switch (pf_move_class[id])
    {
    case PFM_DOOR:       return door_blocks_movement(w, x, y, z);
    case PFM_PASS:       return 0;
    case PFM_FENCE_GATE: return fence_gate_blocks_movement(w, x, y, z);
    case PFM_TRAPDOOR:   return trapdoor_blocks_movement(w, x, y, z);
    /* BlockLiquid.getBlocksMovement: material != lava */
    case PFM_LIQUID:     return BLOCKS[id].material != pf_mat_lava;
    case PFM_PLATE:      return 1;
    default:             return !MATERIALS[BLOCKS[id].material].blocks_movement;
    }
}

/* World.isMaterialInBB over the world (the pathfinder's entity stands in the
 * world it searches, so there is no cache to differ from). */
static int is_material_in_bb(struct world *w, const struct aabb *box, const char *material)
{
    int x0 = mh_floor(box->min_x);
    int x1 = mh_floor(box->max_x + 1.0);
    int y0 = mh_floor(box->min_y);
    int y1 = mh_floor(box->max_y + 1.0);
    int z0 = mh_floor(box->min_z);
    int z1 = mh_floor(box->max_z + 1.0);

    for (int x = x0; x < x1; ++x)
        for (int y = y0; y < y1; ++y)
            for (int z = z0; z < z1; ++z)
            {
                const char *m = MATERIALS[BLOCKS[block_at(w, x, y, z)].material].name;

                if (m != NULL && strcmp(m, material) == 0) return 1;
            }

    return 0;
}

/* ------------------------------------------------------- the point table */

/* PathPoint.makeHash. */
static int32_t make_hash(int x, int y, int z)
{
    return (int32_t)((uint32_t)(y & 255) | (uint32_t)((x & 32767) << 8) | (uint32_t)((z & 32767) << 24) |
                     (x < 0 ? 0x80000000u : 0) | (z < 0 ? 32768u : 0));
}

/* The IntHashMap lookup, addKey and clearMap over an open-addressing table of
 * (key, point) slots. Vanilla's map only ever holds one point per key: an
 * addKey for a key already present overwrites the slot, and the point pool
 * never frees, so the table is exactly one slot per live key. */
/* The finder's storage is the environment's (arena.h): one search runs at a
 * time. */
static void pf_bind(struct pf *f)
{
    struct pf_store *st = &nw_env->arena.pf;

    f->pts = st->pts;
    f->heap = st->heap;
    f->keys = st->keys;
    f->vals = st->vals;
    f->pcap = f->hcap = PF_MAX_POINTS;
}

static int pf_lookup(struct pf *f, int32_t key)
{
    if (f->mcap == 0)
    {
        if (f->pts == NULL) pf_bind(f);
        f->mcap = 1024;

        memset(f->keys, 0, (size_t)f->mcap * sizeof *f->keys);
    }

    uint32_t h = (uint32_t)key * 0x9E3779B9u;

    for (;;)
    {
        int slot = (int)(h & (uint32_t)(f->mcap - 1));

        if (f->keys[slot] == ~key) return f->vals[slot];
        if (f->keys[slot] == 0) return -1;
        ++h;
    }
}

static void pf_add_key(struct pf *f, int32_t key, int point)
{
    if (f->mcap == 0) (void)pf_lookup(f, key); /* makes the table */

    if ((f->npts + 1) * 2 > f->mcap)
    {
        /* the table doubles; every key in it is one point's hash (openPoint
         * adds a point only for a new key), so it refills from the points */
        f->mcap *= 2;
        if (f->mcap > 2 * PF_MAX_POINTS) abort();

        memset(f->keys, 0, (size_t)f->mcap * sizeof *f->keys);

        for (int i = 0; i < f->npts; ++i)
        {
            uint32_t h = (uint32_t)f->pts[i].hash * 0x9E3779B9u;

            for (;;)
            {
                int slot = (int)(h & (uint32_t)(f->mcap - 1));

                if (f->keys[slot] == 0)
                {
                    f->keys[slot] = ~f->pts[i].hash;
                    f->vals[slot] = i;
                    f->pts[i].slot = slot;
                    break;
                }

                ++h;
            }
        }
    }

    uint32_t h = (uint32_t)key * 0x9E3779B9u;

    for (;;)
    {
        int slot = (int)(h & (uint32_t)(f->mcap - 1));

        if (f->keys[slot] == ~key)
        {
            f->vals[slot] = point;
            f->pts[point].slot = slot;
            return;
        }

        if (f->keys[slot] == 0)
        {
            f->keys[slot] = ~key;
            f->vals[slot] = point;
            f->pts[point].slot = slot;
            return;
        }

        ++h;
    }
}

/* IntHashMap.clearMap: every key the last search added is one of its
 * points' (openPoint adds a key only with a new point, and each insertion
 * notes its slot in the point), so a map far larger than that search (it
 * keeps the size the largest search grew it to) is emptied slot by slot.
 * A slot holds ~key and 0 is empty: the arrays are one search's scratch that
 * an image move does not copy (image.c image_extents), and a table that reads
 * zero there is an empty one, whatever slots its points name */
static void pf_clear_map(struct pf *f)
{
    if (8 * f->npts < f->mcap)
        for (int i = 0; i < f->npts; ++i) f->keys[f->pts[i].slot] = 0;
    else
        memset(f->keys, 0, (size_t)f->mcap * sizeof *f->keys);
}

/* PathFinder.openPoint: the mapped point for the coordinates, or a new one.
 * The key is makeHash alone, so two coordinates that hash the same share a
 * point, as in vanilla. */
static int open_point(struct pf *f, int x, int y, int z)
{
    int32_t key = make_hash(x, y, z);
    int p = pf_lookup(f, key);

    if (p >= 0) return p;

    if (f->npts == PF_MAX_POINTS) abort();

    p = f->npts++;
    f->pts[p].x = x;
    f->pts[p].y = y;
    f->pts[p].z = z;
    f->pts[p].hash = key;
    f->pts[p].index = -1;
    f->pts[p].total = 0.0F;
    f->pts[p].next = 0.0F;
    f->pts[p].target = 0.0F;
    f->pts[p].prev = -1;
    f->pts[p].is_first = 0;
    pf_add_key(f, key, p);
    return p;
}

/* ------------------------------------------------------------ the heap */

/* Path.addPoint's sortBack: one 0-based binary heap on distanceToTarget.
 * Vanilla keeps a child on a tie: only a strictly smaller parent key stops it
 * (var3 >= var5 breaks). */
static void heap_sort_back(struct pf *f, int i)
{
    int pt = f->heap[i];
    float key = f->pts[pt].target;

    while (i > 0)
    {
        int parent = (i - 1) >> 1;
        int pp = f->heap[parent];

        if (key >= f->pts[pp].target) break;

        f->heap[i] = pp;
        f->pts[pp].index = i;
        i = parent;
    }

    f->heap[i] = pt;
    f->pts[pt].index = i;
}

/* Path.sortForward, tie behavior included: when the two children tie, the
 * first (left) wins; a child equal to the parent does not rise. */
static void heap_sort_forward(struct pf *f, int i)
{
    int pt = f->heap[i];
    float key = f->pts[pt].target;

    for (;;)
    {
        int l = 1 + (i << 1);
        int r = l + 1;

        if (l >= f->nheap) break;

        int lp = f->heap[l];
        float lk = f->pts[lp].target;
        int rp = -1;
        float rk = (float)INFINITY;

        if (r < f->nheap)
        {
            rp = f->heap[r];
            rk = f->pts[rp].target;
        }

        if (lk < rk)
        {
            if (lk >= key) break;

            f->heap[i] = lp;
            f->pts[lp].index = i;
            i = l;
        }
        else
        {
            if (rk >= key) break;

            f->heap[i] = rp;
            f->pts[rp].index = i;
            i = r;
        }
    }

    f->heap[i] = pt;
    f->pts[pt].index = i;
}

/* Path.addPoint: push, then sortBack. */
static void heap_add(struct pf *f, int pt)
{
    /* PathPoint.index >= 0 ("OW KNOWS!") can never happen here: an option is
     * only added when it is not in the heap. */
    if (f->nheap == PF_MAX_POINTS) abort();

    f->heap[f->nheap] = pt;
    f->pts[pt].index = f->nheap;
    heap_sort_back(f, f->nheap++);
}

/* Path.dequeue: the root, the last point into its slot, sortForward. */
static int heap_dequeue(struct pf *f)
{
    int pt = f->heap[0];
    f->heap[0] = f->heap[--f->nheap];

    if (f->nheap > 0) heap_sort_forward(f, 0);

    f->pts[pt].index = -1;
    return pt;
}

/* Path.changeDistance: the key moves and the point re-sorts, up when it fell,
 * down when it rose. */
static void heap_change_distance(struct pf *f, int pt, float key)
{
    float old = f->pts[pt].target;
    f->pts[pt].target = key;

    if (key < old) heap_sort_back(f, f->pts[pt].index);
    else heap_sort_forward(f, f->pts[pt].index);
}

/* --------------------------------------------------------- the distances */

/* PathPoint.distanceToSquared. */
static float dist2(const struct pf_point *a, const struct pf_point *b)
{
    float dx = (float)(b->x - a->x);
    float dy = (float)(b->y - a->y);
    float dz = (float)(b->z - a->z);
    return dx * dx + dy * dy + dz * dz;
}

/* PathPoint.distanceTo: MathHelper.sqrt_float, which is
 * (float)Math.sqrt((double) of the float sum). */
static float dist(const struct pf_point *a, const struct pf_point *b)
{
    return (float)sqrt((double)dist2(a, b));
}

/* ------------------------------------------------------ getVerticalOffset */

/* PathFinder.func_82565_a: the block rules of one candidate column box, over
 * the size point's three fields. */
static int vertical_offset_read(struct pf *f, struct world *w, int x, int y, int z);

/* One search asks the same column again and again (a neighbor of several
 * points, the fall loop): the answer depends on the search's own fields and
 * the world, which the search only reads, so it is kept for the search
 * (f->vstamp) while the chunk map is unchanged (a read may load a chunk,
 * whose population writes). */
static int vertical_offset(struct pf *f, int x, int y, int z)
{
    struct world *w = f->entity_w != NULL ? f->entity_w : f->w;
    uint32_t h = ((uint32_t)x * 0x9E3779B1u ^ (uint32_t)y * 0x85EBCA77u ^ (uint32_t)z * 0xC2B2AE3Du) >> 20;
    __typeof__(&nw_scratch->pf_vmemo[0]) m = &nw_scratch->pf_vmemo[h];

    if (f->vstamp != 0 && m->stamp == f->vstamp && m->map_ver == w->map_ver && m->w == w && m->x == x && m->y == y && m->z == z)
        return m->v;

    int v = vertical_offset_read(f, w, x, y, z);

    m->x = x;
    m->y = y;
    m->z = z;
    m->stamp = f->vstamp;
    m->map_ver = w->map_ver;
    m->w = w;
    m->v = v;
    return v;
}

static int vertical_offset_read(struct pf *f, struct world *w, int x, int y, int z)
{
    int trapdoor = 0;

    for (int bx = x; bx < x + f->size_x; ++bx)
        for (int by = y; by < y + f->size_y; ++by)
            for (int bz = z; bz < z + f->size_z; ++bz)
            {
                int id = block_at(w, bx, by, bz);
                const struct block_def *b = &BLOCKS[id];

                if (b->material == pf_mat_air) continue;

                int is_water = id == ID_FLOWING_WATER || id == ID_WATER;

                if (id == ID_TRAPDOOR) trapdoor = 1;
                else if (is_water)
                {
                    if (f->pathing_in_water) return -1;
                    trapdoor = 1; /* var8, the "2" return below */
                }
                else if (!f->wooden_door_allowed && id == ID_WOODEN_DOOR) return 0;

                int rt = b->render_type;

                if (rt == 9)
                {
                    int ex = mh_floor(f->pos_x);
                    int ey = mh_floor(f->pos_y);
                    int ez = mh_floor(f->pos_z);

                    if (BLOCKS[block_at(w, ex, ey, ez)].render_type != 9 &&
                        BLOCKS[block_at(w, ex, ey - 1, ez)].render_type != 9)
                        return -3;
                }
                else if (!blocks_movement(w, id, bx, by, bz) && (!f->movement_block_allowed || id != ID_WOODEN_DOOR))
                {
                    if (rt == 11 || id == ID_FENCE_GATE || rt == 32) return -3;
                    if (id == ID_TRAPDOOR) return -4;

                    if (b->material != pf_mat_lava) return 0;
                    /* Entity.handleLavaMovement: the box shrinks by 0.1 x, 0.4 y, 0.1 z
                     * before the material check */
                    struct aabb lava_box = aabb_expand(f->box, -0.10000000149011612, -0.4000000059604645,
                                                       -0.10000000149011612);
                    if (!is_material_in_bb(w, &lava_box, "lava")) return -2;
                }
            }

    return trapdoor ? 2 : 1;
}

/* ----------------------------------------------------------- getSafePoint */

/* PathFinder.getSafePoint. */
static int safe_point(struct pf *f, int x, int y, int z, int up)
{
    int v = vertical_offset(f, x, y, z);
    int p = -1;

    if (v == 2) return open_point(f, x, y, z);

    if (v == 1) p = open_point(f, x, y, z);

    if (p < 0 && up > 0 && v != -3 && v != -4 && vertical_offset(f, x, y + up, z) == 1)
    {
        p = open_point(f, x, y + up, z);
        y += up;
    }

    if (p >= 0)
    {
        int tries = 0;
        int v2 = 0;

        while (y > 0)
        {
            v2 = vertical_offset(f, x, y - 1, z);

            if (f->pathing_in_water && v2 == -1) return -1;
            if (v2 != 1) break;
            if (tries++ >= f->max_safe_tries) return -1;

            --y;

            if (y > 0) p = open_point(f, x, y, z);
        }

        if (v2 == -2) return -1;
    }

    return p;
}

/* ------------------------------------------------------- findPathOptions */

static int find_path_options(struct pf *f, int cur, int end)
{
    int n = 0;
    int up = 0;

    if (vertical_offset(f, f->pts[cur].x, f->pts[cur].y + 1, f->pts[cur].z) == 1) up = 1;

    int p = safe_point(f, f->pts[cur].x, f->pts[cur].y, f->pts[cur].z + 1, up);

    if (p >= 0 && !f->pts[p].is_first && dist(&f->pts[p], &f->pts[end]) < f->max_dist) f->options[n++] = p;

    p = safe_point(f, f->pts[cur].x - 1, f->pts[cur].y, f->pts[cur].z, up);

    if (p >= 0 && !f->pts[p].is_first && dist(&f->pts[p], &f->pts[end]) < f->max_dist) f->options[n++] = p;

    p = safe_point(f, f->pts[cur].x + 1, f->pts[cur].y, f->pts[cur].z, up);

    if (p >= 0 && !f->pts[p].is_first && dist(&f->pts[p], &f->pts[end]) < f->max_dist) f->options[n++] = p;

    p = safe_point(f, f->pts[cur].x, f->pts[cur].y, f->pts[cur].z - 1, up);

    if (p >= 0 && !f->pts[p].is_first && dist(&f->pts[p], &f->pts[end]) < f->max_dist) f->options[n++] = p;

    return n;
}

/* ------------------------------------------------------------ the search */

void pf_init(struct pf *f, struct world *w)
{
    memset(f, 0, sizeof *f);
    f->w = w;
}

void pf_free(struct pf *f)
{
    memset(f, 0, sizeof *f);
}

/* PathFinder.createEntityPath: the chain of predecessors, end first. */
static int build_path(struct pf *f, int start, int end, int *out, int out_cap)
{
    /* the chain runs end..start through prev: count it, then walk it again
     * writing each point at its place from the start (no chain array) */
    int n = 0;

    for (int p = end; p >= 0; p = f->pts[p].prev)
    {
        if (n > f->npts) return -1; /* a chain longer than the points opened: none */
        ++n;
        if (p == start) break;
    }

    int m = n < out_cap ? n : out_cap > 0 ? out_cap : 0;

    for (int p = end, k = n - 1; k >= 0; p = f->pts[p].prev, --k)
        if (k < m)
        {
            out[k * 3 + 0] = f->pts[p].x;
            out[k * 3 + 1] = f->pts[p].y;
            out[k * 3 + 2] = f->pts[p].z;
        }

    return m;
}

/* PathFinder.addToPath. The dequeue branch compares PathPoint.equals, which
 * is the hash plus all three coordinates; the hash alone decides the table
 * key, but the coordinates decide the equality. */
static int same_point(const struct pf_point *a, const struct pf_point *b)
{
    return a->hash == b->hash && a->x == b->x && a->y == b->y && a->z == b->z;
}

static int search(struct pf *f, int start, int end)
{
    f->pts[start].total = 0.0F;
    f->pts[start].next = dist2(&f->pts[start], &f->pts[end]);
    f->pts[start].target = f->pts[start].next;

    int best = start;
    heap_add(f, start);
    int nout = 0;

    while (f->nheap > 0)
    {
        int cur = heap_dequeue(f);

        if (same_point(&f->pts[cur], &f->pts[end]))
        {
            nout = build_path(f, start, cur, f->out, f->out_cap);
            return nout;
        }

        if (dist2(&f->pts[cur], &f->pts[end]) < dist2(&f->pts[best], &f->pts[end])) best = cur;

        f->pts[cur].is_first = 1;
        int nopts = find_path_options(f, cur, end);

        for (int i = 0; i < nopts; ++i)
        {
            int p = f->options[i];
            float t = f->pts[cur].total + dist2(&f->pts[cur], &f->pts[p]);

            /* PathPoint.isAssigned: index >= 0, i.e. the point is in the heap
             * right now. A dequeued point re-enters unconditionally. */
            if (f->pts[p].index < 0 || t < f->pts[p].total)
            {
                f->pts[p].prev = cur;
                f->pts[p].total = t;
                f->pts[p].next = dist2(&f->pts[p], &f->pts[end]);

                if (f->pts[p].index >= 0) heap_change_distance(f, p, f->pts[p].total + f->pts[p].next);
                else
                {
                    f->pts[p].target = f->pts[p].total + f->pts[p].next;
                    heap_add(f, p);
                }
            }
        }
    }

    if (best == start) return -1;

    return build_path(f, start, best, f->out, f->out_cap);
}

/* ------------------------------------------------- the two World methods */

/* The shared setup of both entry points: the PathFinder fields from the
 * entity, the flags and the maxDistance. canEntityDrown is false for every
 * mob that walks (only EntityLiving's constructor sets it through
 * setPathPriority's water modifier in later versions; no 1.7.10 walker sets
 * it), so the drown branch is dead and the ChunkCache window is not carried
 * here; see the report. */
static void setup(struct pf *f, const struct pf_entity *e, float max_dist, int door_open, int door_closed,
                  int avoid_water, int can_swim, int *out, int out_cap)
{
    /* the four flags in PathFinder order: isWoddenDoorAllowed, isMovementBlockAllowed,
     * isPathingInWater, canEntityDrown. PathNavigate passes (openDoors, closedDoors, avoidsWater,
     * canSwim), so isPathingInWater is avoidsWater. */
    f->wooden_door_allowed = door_open;
    f->movement_block_allowed = door_closed;
    f->pathing_in_water = avoid_water;
    f->can_entity_drown = can_swim;
    f->max_dist = max_dist;
    f->pos_x = e->pos_x;
    f->pos_y = e->pos_y;
    f->pos_z = e->pos_z;
    f->width = e->width;
    f->height = e->height;
    f->box = e->box;
    f->in_water = e->in_water;
    f->max_safe_tries = e->max_safe_point_tries;
    f->size_x = mh_floor((double)(e->width + 1.0F));
    f->size_y = mh_floor((double)(e->height + 1.0F));
    f->size_z = mh_floor((double)(e->width + 1.0F));
    f->out = out;
    f->out_cap = out_cap;

    pf_clear_map(f);
    f->npts = 0;
    f->nheap = 0;
    if (++nw_scratch->pf_vstamp == 0)
    {
        memset(nw_scratch->pf_vmemo, 0, sizeof nw_scratch->pf_vmemo);
        nw_scratch->pf_vstamp = 1;
    }
    f->vstamp = nw_scratch->pf_vstamp;
}

/* PathFinder.createEntityPathTo's drowning branch: an entity that can drown and
 * is in water starts at its own cell lifted to the water surface, and the
 * search treats water as pathable while it runs. The path probe's entities had
 * canSwim false, so this branch never ran there. */
static int start_y(struct pf *f, const struct pf_entity *e)
{
    int y = mh_floor(e->box.min_y + 0.5);

    if (f->can_entity_drown && e->in_water)
    {
        int x = mh_floor(e->pos_x);
        int z = mh_floor(e->pos_z);
        y = (int)e->box.min_y;

        int id = world_get_block(f->w, x, y, z) & 4095;

        while (id == 8 || id == 9)
        {
            ++y;
            id = world_get_block(f->w, x, y, z) & 4095;
        }

        f->pathing_in_water = 0;
    }

    return y;
}

/* The ChunkCache constructor's getChunkFromChunkCoords over the window around
 * the entity, x outer, z inner: a chunk that is not loaded is loaded there.
 * Only a world with a provider (the whole-server replay) loads; a probe world
 * already holds every chunk the window reaches. */
static void chunk_cache_window(struct world *w, const struct pf_entity *e, int range)
{
    if (w->provide == NULL || w->no_generate) return;

    int x = mh_floor(e->pos_x), z = mh_floor(e->pos_z);

    for (int cx = (x - range) >> 4; cx <= (x + range) >> 4; ++cx)
        for (int cz = (z - range) >> 4; cz <= (z + range) >> 4; ++cz)
            world_load_chunk(w, cx, cz);
}

/* World.getEntityPathToXYZ. */
int pf_get_entity_path_to_xyz(struct pf *f, const struct pf_entity *e, int x, int y, int z, float max_dist,
                              int door_open, int door_closed, int avoid_water, int can_swim, int *out, int out_cap)
{
    if (PHASE_PROF_ON()) phase_sub_begin_(PS_PATH);
    chunk_cache_window(f->w, e, (int)(max_dist + 8.0F));
    setup(f, e, max_dist, door_open, door_closed, avoid_water, can_swim, out, out_cap);

    int sy = start_y(f, e);
    int start = open_point(f, mh_floor(e->box.min_x), sy, mh_floor(e->box.min_z));
    int end = open_point(f, mh_floor((double)((float)x + 0.5F) - (double)(e->width / 2.0F)), mh_floor((double)((float)y + 0.5F)),
                         mh_floor((double)((float)z + 0.5F) - (double)(e->width / 2.0F)));
    int r = search(f, start, end);
    if (PHASE_PROF_ON()) phase_sub_end_(PS_PATH);
    return r;
}

/* World.getPathEntityToEntity. */
int pf_get_path_entity_to_entity(struct pf *f, const struct pf_entity *e, double tx, double ty, double tz,
                                 float max_dist, int door_open, int door_closed, int avoid_water, int can_swim,
                                 int *out, int out_cap)
{
    if (PHASE_PROF_ON()) phase_sub_begin_(PS_PATH);
    chunk_cache_window(f->w, e, (int)(max_dist + 16.0F));
    setup(f, e, max_dist, door_open, door_closed, avoid_water, can_swim, out, out_cap);

    int sy = start_y(f, e);
    int start = open_point(f, mh_floor(e->box.min_x), sy, mh_floor(e->box.min_z));
    int end = open_point(f, mh_floor(tx - (double)(e->width / 2.0F)), mh_floor(ty), mh_floor(tz - (double)(e->width / 2.0F)));
    int r = search(f, start, end);
    if (PHASE_PROF_ON()) phase_sub_end_(PS_PATH);
    return r;
}

int pf_vertical_offset_at(struct world *w, double pos_x, double pos_y, double pos_z, struct aabb box,
                          int x, int y, int z, int sx, int sy, int sz,
                          int pathing_in_water, int movement_block_allowed, int wooden_door_allowed)
{
    struct pf f;
    memset(&f, 0, sizeof f);
    f.w = w;
    f.pos_x = pos_x;
    f.pos_y = pos_y;
    f.pos_z = pos_z;
    f.box = box;
    f.size_x = sx;
    f.size_y = sy;
    f.size_z = sz;
    f.pathing_in_water = pathing_in_water;
    f.movement_block_allowed = movement_block_allowed;
    f.wooden_door_allowed = wooden_door_allowed;
    /* no search: no memo */
    return vertical_offset_read(&f, w, x, y, z);
}
