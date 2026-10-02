/* Gate: the pick and the ray shapes (csrc/engine/raytrace.c, csrc/engine/pick.c)
 * against the oracle's RayProbe recording (out/java/raypick/<name>/, made
 * with
 *
 *   make -C oracle run SEED=1 CLASS=RayProbe NAME=rp-1 \
 *     CMD='{"out":"<abs>/out/java/raypick/rp-1"}'
 *
 * ). Block cases: the probe's 5x5x5 room is written into empty chunks at the
 * same place; the target's bounds after setBlockBoundsBasedOnState (every
 * block but the stairs and the torches, whose Block objects keep what their
 * last trace set) and, per
 * ray, Block.collisionRayTrace on the target, World.func_147447_a with the
 * pick's flags (false, false, true) and with the arrow's (false, true,
 * false) must match bit for bit: the result type, the block, the face and
 * the hit vector. Entity cases: every entity's canBeCollidedWith, border and
 * bounding box from its kind and position, the order
 * getEntitiesWithinAABBExcludingEntity lists them in (pick_order), and the
 * getMouseOver pass's chosen entity, hit vector and distance.
 *
 * Negative checks: --negative=stairs-nearest picks the stairs octant hit
 * nearest the end instead of the farthest; --negative=fireball-border gives
 * the large fireball Entity's 0.1 border. Each must fail.
 */
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

#include "../engine/blocks.h"
#include "../engine/jmath.h"
#include "../engine/env.h"
#include "../engine/fallhang.h"
#include "../engine/nbtjson.h"
#include "../engine/tape.h"
#include "../engine/pick.h"
#include "../engine/raytrace.h"
#include "../engine/world.h"

static gzFile g;
static int eof_hit;

static void rd(void *p, int n)
{
    if (gzread(g, p, n) != n) { eof_hit = 1; memset(p, 0, (size_t)n); }
}
static int u8(void) { unsigned char v; rd(&v, 1); return v; }
static int u16(void) { unsigned char b[2]; rd(b, 2); return b[0] | b[1] << 8; }
static int i32(void)
{
    unsigned char b[4];
    rd(b, 4);
    return (int)((uint32_t)b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16 | (uint32_t)b[3] << 24);
}
static double f64(void)
{
    unsigned char b[8];
    rd(b, 8);
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i) v = v << 8 | b[i];
    double d;
    memcpy(&d, &v, 8);
    return d;
}
static float f32(void)
{
    uint32_t v = (uint32_t)i32();
    float f;
    memcpy(&f, &v, 4);
    return f;
}
static int same(double a, double b) { return memcmp(&a, &b, sizeof a) == 0; }

struct mop_rec { int type, x, y, z, side; double hx, hy, hz; };

static struct mop_rec read_mop(void)
{
    struct mop_rec m;
    m.type = u8();
    m.x = i32(); m.y = i32(); m.z = i32();
    m.side = u8();
    m.hx = f64(); m.hy = f64(); m.hz = f64();
    return m;
}

static struct mop_rec from_native(int got, const struct rt_mop *mop)
{
    struct mop_rec m = {0, 0, 0, 0, 0, 0.0, 0.0, 0.0};
    if (!got) return m;
    m.type = mop->type == RT_MISS ? 2 : 1;
    m.x = mop->x; m.y = mop->y; m.z = mop->z;
    m.side = mop->face;
    m.hx = mop->hx; m.hy = mop->hy; m.hz = mop->hz;
    return m;
}

static int mop_equal(const struct mop_rec *a, const struct mop_rec *b)
{
    if (a->type != b->type) return 0;
    if (a->type == 0) return 1;
    return a->x == b->x && a->y == b->y && a->z == b->z && a->side == b->side &&
           same(a->hx, b->hx) && same(a->hy, b->hy) && same(a->hz, b->hz);
}

static void show(const char *what, const struct mop_rec *m)
{
    printf("  %s: type %d block (%d,%d,%d) side %d hit (%.17g, %.17g, %.17g)\n",
           what, m->type, m->x, m->y, m->z, m->side, m->hx, m->hy, m->hz);
}

static struct chunk *put_chunk(struct world *w, int cx, int cz)
{
    static uint16_t ids[CHUNK_CELLS];
    static uint8_t metas[CHUNK_CELLS];
    struct chunk *c = chunk_new();
    chunk_construct(c, cx, cz, ids, metas);
    world_put_chunk(w, c);
    return c;
}

static void set_raw(struct world *w, int x, int y, int z, int id, int meta)
{
    struct chunk *c = world_chunk(w, x >> 4, z >> 4);
    int cell = (x & 15) << 12 | (z & 15) << 8 | y;
    chunk_set_id(c, cell, id);
    chunk_set_meta_cell(c, cell, meta);
    if (id != 0) c->mask |= (uint16_t)(1 << (y >> 4));
}

int main(int argc, char **argv)
{
    if (argc < 2)
    {
        fprintf(stderr, "usage: test_raypick [--negative=stairs-nearest|fireball-border] DIR\n");
        return 2;
    }
    const char *dir = argv[argc - 1];
    int neg_stairs = 0, neg_border = 0;
    for (int i = 1; i < argc - 1; ++i)
    {
        if (!strcmp(argv[i], "--negative=stairs-nearest")) neg_stairs = 1;
        else if (!strcmp(argv[i], "--negative=fireball-border")) neg_border = 1;
    }
    nw_env->cfg.raytrace_negative_stairs_nearest = neg_stairs;

    char path[4096];
    snprintf(path, sizeof path, "%s/manifest.json", dir);
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return 2; }
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *text = malloc((size_t)len + 1);
    if (fread(text, 1, (size_t)len, f) != (size_t)len) { fclose(f); return 2; }
    text[len] = 0;
    fclose(f);
    struct jval *m = json_parse(text);
    const char *kind = m ? json_str(json_get(m, "kind")) : NULL;
    if (!kind || strcmp(kind, "raypick")) { printf("SKIP %s: not a raypick probe\n", dir); return 0; }
    int ox = 0, oy = 0, oz = 0;
    const char *origin = json_str(json_get(m, "origin"));
    if (!origin || sscanf(origin, "%d,%d,%d", &ox, &oy, &oz) != 3) { printf("FAIL %s: no origin\n", dir); return 1; }
    int64_t want_rays = 0, want_cases = 0;
    json_int(json_get(m, "rays"), &want_rays);
    json_int(json_get(m, "entity_cases"), &want_cases);

    static struct world w;
    world_init(&w, 0);
    w.no_generate = 1;
    /* RayProbe traces on the WorldClient: BlockPortal's metadata write is
     * the server's only */
    w.is_remote = 1;
    for (int cx = (ox - 24) >> 4; cx <= (ox + 24) >> 4; ++cx)
        for (int cz = (oz - 24) >> 4; cz <= (oz + 24) >> 4; ++cz)
            put_chunk(&w, cx, cz);

    snprintf(path, sizeof path, "%s/cases.bin.gz", dir);
    g = gzopen(path, "rb");
    if (!g) { perror(path); return 2; }

    long rays = 0, blocks = 0, ecases = 0, picks = 0, listed = 0, fails = 0;
    long hits[3] = {0, 0, 0};
    for (;;)
    {
        int tag = u8();
        if (eof_hit) { printf("FAIL %s: truncated cases.bin.gz\n", dir); return 1; }
        if (tag == 0) break;

        if (tag == 1)
        {
            int id = i32(), meta = i32();
            for (int a = 0; a < 5; ++a)
                for (int b = 0; b < 5; ++b)
                    for (int c = 0; c < 5; ++c)
                    {
                        int cid = u16(), cmeta = u8();
                        set_raw(&w, ox + a - 2, oy + b - 2, oz + c - 2, cid, cmeta);
                    }
            double want_b[6], got_b[6];
            for (int q = 0; q < 6; ++q) want_b[q] = f64();
            raytrace_block_bounds(&w, ox, oy, oz, got_b);
            /* the stairs' and the torches' Block objects keep what their
             * last collisionRayTrace set; setBlockBoundsBasedOnState does not
             * reset them */
            int stale = !strcmp(BLOCKS[id].class_name, "BlockStairs") || id == 50 || id == 75 || id == 76;
            if (!stale)
                for (int q = 0; q < 6; ++q)
                    if (!same(want_b[q], got_b[q]))
                    {
                        if (fails++ < 10)
                            printf("FAIL %s: block %d meta %d bounds[%d] want %.17g got %.17g\n",
                                   dir, id, meta, q, want_b[q], got_b[q]);
                        break;
                    }
            int n = i32();
            for (int k = 0; k < n; ++k)
            {
                double se[6];
                for (int q = 0; q < 6; ++q) se[q] = f64();
                struct mop_rec want[3];
                for (int q = 0; q < 3; ++q) want[q] = read_mop();
                struct rt_mop mop[3];
                int got[3];
                got[0] = raytrace_collision(&w, ox, oy, oz, se[0], se[1], se[2], se[3], se[4], se[5], &mop[0]);
                got[1] = raytrace_blocks(&w, se[0], se[1], se[2], se[3], se[4], se[5], 0, 0, 1, &mop[1]);
                got[2] = raytrace_blocks(&w, se[0], se[1], se[2], se[3], se[4], se[5], 0, 1, 0, &mop[2]);
                static const char *names[3] = {"collisionRayTrace", "pick walk", "arrow walk"};
                for (int q = 0; q < 3; ++q)
                {
                    struct mop_rec have = from_native(got[q], &mop[q]);
                    if (want[q].type == 1) ++hits[q];
                    if (mop_equal(&want[q], &have)) continue;
                    if (fails++ < 10)
                    {
                        printf("FAIL %s: block %d meta %d ray %ld %s, start (%.17g, %.17g, %.17g) end (%.17g, %.17g, %.17g)\n",
                               dir, id, meta, rays, names[q], se[0], se[1], se[2], se[3], se[4], se[5]);
                        show("oracle", &want[q]);
                        show("native", &have);
                        printf("  ids: oracle block %d:%d native block %d:%d\n",
                               world_get_block(&w, want[q].x, want[q].y, want[q].z), world_get_meta(&w, want[q].x, want[q].y, want[q].z),
                               world_get_block(&w, have.x, have.y, have.z), world_get_meta(&w, have.x, have.y, have.z));
                    }
                }
                ++rays;
            }
            ++blocks;
            continue;
        }

        if (tag != 2) { printf("FAIL %s: unknown record %d\n", dir, tag); return 1; }

        /* an entity case */
        int n = i32();
        struct { int kind, a, b, c, d, collidable; double x, y, z; float border; struct aabb box; } ent[64];
        if (n > 64) { printf("FAIL %s: %d entities in one case\n", dir, n); return 1; }
        struct pick_entry pe[64];
        int owner = -1;
        for (int i = 0; i < n; ++i)
        {
            ent[i].kind = u8();
            ent[i].a = i32(); ent[i].b = i32(); ent[i].c = i32(); ent[i].d = i32();
            ent[i].x = f64(); ent[i].y = f64(); ent[i].z = f64();
            ent[i].collidable = u8();
            ent[i].border = f32();
            ent[i].box.min_x = f64(); ent[i].box.min_y = f64(); ent[i].box.min_z = f64();
            ent[i].box.max_x = f64(); ent[i].box.max_y = f64(); ent[i].box.max_z = f64();

            struct aabb nb;
            if (ent[i].kind == PK_PAINTING || ent[i].kind == PK_FRAME)
            {
                fh_ent h;
                memset(&h, 0, sizeof h);
                h.kind = ent[i].kind == PK_PAINTING ? FH_PAINTING : FH_FRAME;
                h.art = ent[i].d >> 8;
                h.tile_x = ent[i].a; h.tile_y = ent[i].b; h.tile_z = ent[i].c;
                fh_set_direction(&h, ent[i].d & 255);
                nb = h.e.bounding_box;
            }
            else nb = pick_kind_box(ent[i].kind, ent[i].a, ent[i].x, ent[i].y, ent[i].z);
            int col = pick_kind_collidable(ent[i].kind);
            float border = pick_kind_border(ent[i].kind);
            if (neg_border && ent[i].kind == PK_LARGE_FIREBALL) border = 0.1F;
            const double *wb = &ent[i].box.min_x, *gb = &nb.min_x;
            int box_ok = 1;
            for (int q = 0; q < 6; ++q) box_ok &= same(wb[q], gb[q]);
            if (!box_ok || col != ent[i].collidable || border != ent[i].border)
            {
                if (fails++ < 10)
                    printf("FAIL %s: entity case %ld entity %d kind %d: collidable %d/%d border %g/%g box (%.17g %.17g %.17g %.17g %.17g %.17g) native (%.17g %.17g %.17g %.17g %.17g %.17g)\n",
                           dir, ecases, i, ent[i].kind, ent[i].collidable, col, (double)ent[i].border, (double)border,
                           wb[0], wb[1], wb[2], wb[3], wb[4], wb[5], gb[0], gb[1], gb[2], gb[3], gb[4], gb[5]);
            }
            ent[i].collidable = col;
            ent[i].border = border;

            if (ent[i].kind == PK_DRAGON) owner = i;
            pe[i].cx = mh_floor(ent[i].x / 16.0);
            pe[i].cz = mh_floor(ent[i].z / 16.0);
            int cy = mh_floor(ent[i].y / 16.0);
            pe[i].cy = cy < 0 ? 0 : (cy > 15 ? 15 : cy);
            pe[i].seq = i;
            pe[i].owner = ent[i].kind == PK_PART ? owner : -1;
            pe[i].box = nb;
        }
        int nl = i32();
        int list[64];
        for (int i = 0; i < nl; ++i) list[i] = i32();
        double ex = f64(), ey = f64(), ez = f64();
        double lx = f64(), ly = f64(), lz = f64();
        struct aabb viewer;
        viewer.min_x = f64(); viewer.min_y = f64(); viewer.min_z = f64();
        viewer.max_x = f64(); viewer.max_y = f64(); viewer.max_z = f64();
        double var2 = f64(), var4 = f64();
        int have_mo = u8();
        int want_chosen = i32();
        double whx = f64(), why = f64(), whz = f64(), wdist = f64();
        int want_replaced = u8();

        struct aabb search = aabb_expand(aabb_add_coord(viewer, lx * var2, ly * var2, lz * var2), 1.0, 1.0, 1.0);
        int got_list[64];
        int ngot = pick_order(pe, n, &search, got_list, 64);
        int order_ok = ngot == nl;
        for (int i = 0; order_ok && i < nl; ++i) order_ok = got_list[i] == list[i];
        if (!order_ok && fails++ < 10)
        {
            printf("FAIL %s: entity case %ld list order:", dir, ecases);
            for (int i = 0; i < nl; ++i) printf(" %d", list[i]);
            printf(" native:");
            for (int i = 0; i < ngot && i < 64; ++i) printf(" %d", got_list[i]);
            printf("\n");
        }

        struct pick_cand cand[64];
        for (int i = 0; i < nl; ++i)
        {
            int k = list[i];
            cand[i].box = pe[k].box;
            cand[i].border = ent[k].border;
            cand[i].collidable = ent[k].collidable;
            cand[i].riding = 0;
            cand[i].id = k;
        }
        struct pick_result r = pick_entity_pass(cand, nl, ex, ey, ez, lx, ly, lz, var2, var4, have_mo);
        int got_chosen = r.chosen >= 0 ? cand[r.chosen].id : -1;
        int ok = got_chosen == want_chosen && same(r.dist, wdist) && r.replaced == want_replaced;
        if (ok && want_chosen >= 0) ok = same(r.hx, whx) && same(r.hy, why) && same(r.hz, whz);
        if (!ok && fails++ < 10)
            printf("FAIL %s: entity case %ld pick: oracle entity %d hit (%.17g, %.17g, %.17g) dist %.17g replaced %d, native entity %d hit (%.17g, %.17g, %.17g) dist %.17g replaced %d\n",
                   dir, ecases, want_chosen, whx, why, whz, wdist, want_replaced,
                   got_chosen, r.hx, r.hy, r.hz, r.dist, r.replaced);
        if (want_chosen >= 0 && want_replaced) ++picks;
        listed += nl;
        ++ecases;
    }
    gzclose(g);

    if (rays != want_rays || ecases != want_cases)
    {
        printf("FAIL %s: %ld rays and %ld entity cases read, the manifest says %lld and %lld\n",
               dir, rays, ecases, (long long)want_rays, (long long)want_cases);
        return 1;
    }
    if (fails)
    {
        printf("FAIL %s: %ld mismatches (%ld block cases, %ld rays, %ld entity cases)\n", dir, fails, blocks, rays, ecases);
        return 1;
    }
    printf("PASS %s: %ld block cases, %ld rays (%ld, %ld, %ld block hits: trace, pick walk, arrow walk), "
           "%ld entity cases (%ld listed, %ld picks), bit for bit\n",
           dir, blocks, rays, hits[0], hits[1], hits[2], ecases, listed, picks);
    return 0;
}
