/* The Nether and End populate drivers, see populate_nether.h for the Random
 * history model. Every world effect goes through the world core's setBlock
 * path (world_set_block), so the write listener, the block callbacks, the
 * queued light engine and the scheduled-update list see exactly what vanilla's
 * populate sees in those dimensions. */
#include "populate_nether.h"
#include "env.h"

#include "blocks.h"
#include "features_nether.h"
#include "fortress.h"
#include "structure_blocks.h"

#include "trace.h"

#include <stdlib.h>
#include <string.h>

/* Block ids the drivers name, from Blocks. */
enum {
    PE_DIRT = 3, PE_GRAVEL = 13, PE_GOLD = 14, PE_IRON = 15, PE_COAL = 16,
    PE_LAPIS = 21, PE_REDSTONE = 73, PE_DIAMOND = 56, PE_END_STONE = 121,
};

/* The material indices the End's spike placement reads, by name so a
 * regenerated blocks.h cannot change what they mean. */
static int MAT_LEAVES = -1;

/* before main: the same for every environment */
__attribute__((constructor)) static void mat_init(void)
{
    for (size_t i = 0; i < sizeof MATERIALS / sizeof MATERIALS[0]; ++i)
        if (MATERIALS[i].name != NULL && strcmp(MATERIALS[i].name, "leaves") == 0) MAT_LEAVES = (int)i;
}

static void hell_stage(struct populate *p, int stage)
{
    if (p->stage.fn != NULL)
        p->stage.fn(p->stage.ctx, stage, (uint64_t)p->world.nether.rand.seed);
}

static void end_stage(struct populate *p, int stage)
{
    if (p->stage.fn != NULL) p->stage.fn(p->stage.ctx, stage, (uint64_t)p->end_rand.seed);
}

/* One row of the nether lane's feature table, generate with its own block and
 * size parameter. */
static int hell_feature(struct world *w, jrand *r, int x, int y, int z, const char *name, int config)
{
    int n;
    const struct feature *rows = features_nether_for(name, &n);

    return rows[config].generate(w, r, x, y, z, rows[config].block, rows[config].count);
}

/* ------------------------------------------------------------- the fortress map */

/* The fortress map: the same shape as the four overworld types' maps
 * (populate.h), a scratch field of the dim -1 driver. */
#define hmap (nw_env->populate_hell.hmap)

void populate_hell_init(struct populate *p, int64_t seed)
{
    const struct structure_type *t_ = structure_type_by_name("Fortress");

    if (t_->begin != NULL) t_->begin(seed);

    jhm64_init(&hmap.keys);
}

static void hell_map_free(void)
{
    for (int i = 0; i < hmap.n; ++i) start_free(&hmap.starts[i].start);

    free(hmap.starts);
    jhm64_free(&hmap.keys);
    memset(&hmap, 0, sizeof hmap);
}

/* The test calls this after its last call: the map is the driver's own, not a
 * populate field, so it does not ride populate_free. */
void populate_hell_free(void)
{
    hell_map_free();
}

void populate_hell_offer_chunk(struct populate *p, int cx, int cz)
{
    const struct structure_type *t_ = structure_type_by_name("Fortress");

    /* MapGenBase.func_151539_a: the candidates -8..8 around the chunk, x
     * outer, z inner */
    for (int dx = -8; dx <= 8; ++dx)
    {
        for (int dz = -8; dz <= 8; ++dz)
        {
            int ox = cx + dx, oz = cz + dz;

            if (jhm64_contains(&hmap.keys, chunk_key(ox, oz))) continue;   /* func_151538_a's containsKey */

            /* MapGenStructure.func_151538_a: one nextInt() per offered
             * candidate, then the spawn test on the candidate's own seed */
            jrand r;
            uint64_t kx = (uint64_t)ox * (uint64_t)p->mul_x;
            uint64_t kz = (uint64_t)oz * (uint64_t)p->mul_z;
            jr_seed(&r, (int64_t)(kx ^ kz ^ (uint64_t)p->world.seed));
            jr_int(&r);

            if (!t_->can_spawn(&r, ox, oz)) continue;

            if (hmap.n == hmap.cap)
            {
                hmap.cap = hmap.cap ? hmap.cap * 2 : 32;
                hmap.starts = realloc(hmap.starts, (size_t)hmap.cap * sizeof *hmap.starts);
            }

            struct pop_start *s = &hmap.starts[hmap.n];

            start_init(&s->start, t_->name, ox, oz);
            t_->make_start(&r, ox, oz, &s->start);
            s->sizeable = 1;   /* the fortress Start is always sizeable */

            jhm64_put(&hmap.keys, chunk_key(ox, oz), hmap.n);
            ++hmap.n;
        }
    }
}

void populate_hell_add_start(struct populate *p, int ox, int oz)
{
    const struct structure_type *t_ = structure_type_by_name("Fortress");

    if (jhm64_contains(&hmap.keys, chunk_key(ox, oz))) return;

    jrand r;
    uint64_t kx = (uint64_t)ox * (uint64_t)p->mul_x;
    uint64_t kz = (uint64_t)oz * (uint64_t)p->mul_z;
    jr_seed(&r, (int64_t)(kx ^ kz ^ (uint64_t)p->world.seed));
    jr_int(&r);
    (void)t_->can_spawn(&r, ox, oz);

    if (hmap.n == hmap.cap)
    {
        hmap.cap = hmap.cap ? hmap.cap * 2 : 32;
        hmap.starts = realloc(hmap.starts, (size_t)hmap.cap * sizeof *hmap.starts);
    }

    struct pop_start *s = &hmap.starts[hmap.n];

    start_init(&s->start, t_->name, ox, oz);
    t_->make_start(&r, ox, oz, &s->start);
    s->sizeable = 1;

    jhm64_put(&hmap.keys, chunk_key(ox, oz), hmap.n);
    ++hmap.n;
}

int populate_hell_count(struct populate *p, int cx, int cz)
{
    /* MapGenStructure.generateStructuresInChunk: the box is (cx*16+8,
     * cz*16+8) .. (+15, +15), and a start generates when it is sizeable and
     * its bounding box intersects it */
    (void)p;
    int cx8 = cx * 16 + 8, cz8 = cz * 16 + 8;
    int n = 0;

    for (int i = 0; i < hmap.n; ++i)
    {
        struct pop_start *s = &hmap.starts[i];

        if (s->sizeable && s->start.bb.maxX >= cx8 && s->start.bb.minX <= cx8 + 15 &&
            s->start.bb.maxZ >= cz8 && s->start.bb.minZ <= cz8 + 15)
            ++n;
    }

    return n;
}

/* ChunkProviderHell.getPossibleCreatures' fortress half for the monster type:
 * 1 when MapGenStructure.hasStructureAt holds (a sizeable start whose box
 * meets the column has a component box holding the point), 2 when
 * func_142038_b does (the FIRST sizeable start in the map's HashMap order
 * meets the column; the walk stops at it whether or not it does) and the
 * block below is nether brick, else 0. */
int populate_hell_fortress_at(struct populate *p, int x, int y, int z, int below)
{
    for (int i = 0; i < hmap.n; ++i)
    {
        const struct pop_start *s = &hmap.starts[i];

        if (!s->sizeable || !(s->start.bb.maxX >= x && s->start.bb.minX <= x &&
                              s->start.bb.maxZ >= z && s->start.bb.minZ <= z)) continue;

        for (int k = 0; k < s->start.n; ++k)
        {
            const struct bbox *b = &s->start.pieces[k]->bb;

            if (x >= b->minX && x <= b->maxX && z >= b->minZ && z <= b->maxZ &&
                y >= b->minY && y <= b->maxY) return 1;
        }
    }

    if (below != 112) return 0;   /* Blocks.nether_brick */

    if (hmap.n > p->cap_order)
    {
        p->cap_order = hmap.n;
        p->order = realloc(p->order, (size_t)p->cap_order * sizeof *p->order);
    }

    int n = jhm64_order(&hmap.keys, p->order, NULL);

    for (int i = 0; i < n; ++i)
    {
        const struct pop_start *s = &hmap.starts[p->order[i]];

        if (!s->sizeable) continue;

        int meets = s->start.bb.maxX >= x && s->start.bb.minX <= x &&
                    s->start.bb.maxZ >= z && s->start.bb.minZ <= z;

        /* the negative check: every start's box instead of the first's */
        if (nw_env->cfg.populate_hell_negative_anystart && !meets) continue;

        return meets ? 2 : 0;
    }

    return 0;
}

/* MapGenStructure.generateStructuresInChunk over the fortress map: every
 * sizeable start whose box meets the populate box generates with hellRNG
 * itself (ChunkProviderHell.populate passes this.hellRNG), in the map's
 * HashMap order, then the stage marker fires. */
void populate_hell_structures(struct populate *p, int cx, int cz)
{
    int cx8 = cx * 16 + 8, cz8 = cz * 16 + 8;
    struct bbox box = bbox_make(cx8, 1, cz8, cx8 + 15, 512, cz8 + 15);

    /* the map's values() order, over the shared scratch the overworld types
     * grow (grown on demand here too) */
    if (hmap.n > p->cap_order)
    {
        p->cap_order = hmap.n ? hmap.n : 1;
        p->order = realloc(p->order, (size_t)p->cap_order * sizeof *p->order);
    }

    int n = jhm64_order(&hmap.keys, p->order, NULL);

    /* the negative check: the walk's own order instead of the map's */
    if (nw_env->cfg.populate_negative_maporder)
    {
        for (int i = 0; i < hmap.n; ++i) p->order[i] = i;
        n = hmap.n;
    }

    /* the environment a structure block half runs in: hellRNG is the
     * population Random (the pieces' own draws are the stream the stage
     * markers carry); no entities are constructed and no drop is modeled */
    struct sc_ctx c;

    memset(&c, 0, sizeof c);
    c.w = &p->world;
    c.box = box;
    c.rand = &p->world.nether.rand;
    c.piece_fn = fortress_blocks;

    for (int i = 0; i < n; ++i)
    {
        struct pop_start *s = &hmap.starts[p->order[i]];

        if (!s->sizeable || !bbox_intersects(&s->start.bb, &box)) continue;

        sc_generate(&c, &s->start);
    }

    sc_free_ents(&c);

    hell_stage(p, HELL_FORTRESS);
}

/* ------------------------------------------------------- ChunkProviderHell.populate */

/* The eight open lava springs: x and z at (chunk + nextInt(16) + 8), y at
 * nextInt(120) + 4. */
void populate_hell_lava(struct populate *p, int cx, int cz)
{
    jrand *r = &p->world.nether.rand;
    int bx = cx * 16, bz = cz * 16;

    for (int i = 0; i < 8; ++i)
    {
        int x = bx + jr_int_n(r, 16) + 8;
        int y = jr_int_n(r, 120) + 4;
        int z = bz + jr_int_n(r, 16) + 8;
        hell_feature(&p->world, r, x, y, z, "helllava", 0);
    }

    hell_stage(p, HELL_HELLLAVA);
}

/* The fire patches: nextInt(nextInt(10) + 1) + 1 tries, same band. */
void populate_hell_fire(struct populate *p, int cx, int cz)
{
    jrand *r = &p->world.nether.rand;
    int bx = cx * 16, bz = cz * 16;
    int n = jr_int_n(r, jr_int_n(r, 10) + 1) + 1;

    for (int i = 0; i < n; ++i)
    {
        int x = bx + jr_int_n(r, 16) + 8;
        int y = jr_int_n(r, 120) + 4;
        int z = bz + jr_int_n(r, 16) + 8;
        hell_feature(&p->world, r, x, y, z, "fire", 0);
    }

    hell_stage(p, HELL_FIRE);
}

/* The glowstone stalactites: glowstone 1's nextInt(nextInt(10) + 1) tries at
 * nextInt(120) + 4, glowstone 2's fixed 10 at nextInt(128). */
void populate_hell_glow1(struct populate *p, int cx, int cz)
{
    jrand *r = &p->world.nether.rand;
    int bx = cx * 16, bz = cz * 16;
    int n = jr_int_n(r, jr_int_n(r, 10) + 1);

    for (int i = 0; i < n; ++i)
    {
        int x = bx + jr_int_n(r, 16) + 8;
        int y = jr_int_n(r, 120) + 4;
        int z = bz + jr_int_n(r, 16) + 8;
        hell_feature(&p->world, r, x, y, z, "glowstone1", 0);
    }

    hell_stage(p, HELL_GLOW1);
}

void populate_hell_glow2(struct populate *p, int cx, int cz)
{
    jrand *r = &p->world.nether.rand;
    int bx = cx * 16, bz = cz * 16;

    for (int i = 0; i < 10; ++i)
    {
        int x = bx + jr_int_n(r, 16) + 8;
        int y = jr_int_n(r, 128);
        int z = bz + jr_int_n(r, 16) + 8;
        hell_feature(&p->world, r, x, y, z, "glowstone2", 0);
    }

    hell_stage(p, HELL_GLOW2);
}

/* The two mushroom patches: nextInt(1) == 0 always holds, so one patch each at
 * nextInt(128), the brown first. */
void populate_hell_brown(struct populate *p, int cx, int cz)
{
    jrand *r = &p->world.nether.rand;
    int bx = cx * 16, bz = cz * 16;

    if (jr_int_n(r, 1) == 0)
    {
        int x = bx + jr_int_n(r, 16) + 8;
        int y = jr_int_n(r, 128);
        int z = bz + jr_int_n(r, 16) + 8;
        hell_feature(&p->world, r, x, y, z, "nethermushrooms", 0);
    }

    hell_stage(p, HELL_BROWN);
}

void populate_hell_red(struct populate *p, int cx, int cz)
{
    jrand *r = &p->world.nether.rand;
    int bx = cx * 16, bz = cz * 16;

    if (jr_int_n(r, 1) == 0)
    {
        int x = bx + jr_int_n(r, 16) + 8;
        int y = jr_int_n(r, 128);
        int z = bz + jr_int_n(r, 16) + 8;
        hell_feature(&p->world, r, x, y, z, "nethermushrooms", 1);
    }

    hell_stage(p, HELL_RED);
}

/* The quartz veins and the hidden lava springs: 16 tries each, x and z at
 * chunk + nextInt(16) (no +8), y at nextInt(108) + 10. */
void populate_hell_quartz(struct populate *p, int cx, int cz)
{
    jrand *r = &p->world.nether.rand;
    int bx = cx * 16, bz = cz * 16;

    for (int i = 0; i < 16; ++i)
    {
        int x = bx + jr_int_n(r, 16);
        int y = jr_int_n(r, 108) + 10;
        int z = bz + jr_int_n(r, 16);
        hell_feature(&p->world, r, x, y, z, "quartz", 0);
    }

    hell_stage(p, HELL_QUARTZ);
}

void populate_hell_hidden(struct populate *p, int cx, int cz)
{
    jrand *r = &p->world.nether.rand;
    int bx = cx * 16, bz = cz * 16;

    for (int i = 0; i < 16; ++i)
    {
        int x = bx + jr_int_n(r, 16);
        int y = jr_int_n(r, 108) + 10;
        int z = bz + jr_int_n(r, 16);
        hell_feature(&p->world, r, x, y, z, "helllavahidden", 0);
    }

    hell_stage(p, HELL_HIDDENLAVA);
}

/* --------------------------------------------------------------- the End */

/* World.getTopSolidOrLiquidBlock: the chunk's own columns, from the top filled
 * segment down, the first position whose block blocks movement and is not the
 * leaves material, one past it; -1 when the column has none. No cache: unlike
 * Chunk.getPrecipitationHeight vanilla writes nothing down. */
static int end_top_solid_or_liquid(struct populate *p, int x, int z)
{
    struct chunk *c = world_load_chunk(&p->world, x >> 4, z >> 4);
    int y = populate_top_filled_segment(c) + 15;

    x &= 15;
    z &= 15;

    for (; y > 0; --y)
    {
        int id = ((c->mask >> (y >> 4)) & 1) ? chunk_cell_id(c, x << 12 | z << 8 | y) : 0;
        const struct material_def *m = &MATERIALS[BLOCKS[id].material];

        if (m->blocks_movement && BLOCKS[id].material != MAT_LEAVES) return y + 1;
    }

    return -1;
}

/* generateOres: the eight standard ore passes over Blocks.stone, which the End
 * does not hold: every vein draws and places nothing. */
static void end_ore_band(struct populate *p, int cx, int cz, int count, int block, int count_size, int y0, int y1)
{
    jrand *r = &p->end_rand;

    for (int i = 0; i < count; ++i)
    {
        int x = cx * 16 + jr_int_n(r, 16);
        int y = jr_int_n(r, y1 - y0) + y0;
        int z = cz * 16 + jr_int_n(r, 16);
        feature_minable(&p->world, r, x, y, z, block, count_size);
    }
}

void populate_end_ores(struct populate *p, int cx, int cz)
{
    /* dirt, gravel, coal, iron, gold, redstone, diamond, then genStandardOre2's
     * two draws of 16 for the lapis */
    end_ore_band(p, cx, cz, 20, PE_DIRT, 32, 0, 256);
    end_ore_band(p, cx, cz, 10, PE_GRAVEL, 32, 0, 256);
    end_ore_band(p, cx, cz, 20, PE_COAL, 16, 0, 128);
    end_ore_band(p, cx, cz, 20, PE_IRON, 8, 0, 64);
    end_ore_band(p, cx, cz, 2, PE_GOLD, 8, 0, 32);
    end_ore_band(p, cx, cz, 8, PE_REDSTONE, 7, 0, 16);
    end_ore_band(p, cx, cz, 1, PE_DIAMOND, 7, 0, 16);

    {
        jrand *r = &p->end_rand;
        int x = cx * 16 + jr_int_n(r, 16);
        int y = jr_int_n(r, 16) + jr_int_n(r, 16);
        int z = cz * 16 + jr_int_n(r, 16);
        feature_minable(&p->world, r, x, y, z, PE_LAPIS, 6);
    }

    end_stage(p, END_ORES);
}

/* The spike chance: one in five calls, 4 with the negative check. */

/* The spike attempt: one in five calls, at chunk + nextInt(16) + 8 in both
 * axes, y at the top solid or liquid block. The crystal the generate would
 * spawn is the feature's own report. */
void populate_end_spike(struct populate *p, int cx, int cz)
{
    jrand *r = &p->end_rand;

    if (jr_int_n(r, nw_env->cfg.populate_negative_spike ? 4 : 5) == 0)
    {
        int x = cx * 16 + jr_int_n(r, 16) + 8;
        int z = cz * 16 + jr_int_n(r, 16) + 8;
        int y = end_top_solid_or_liquid(p, x, z);
        int n;
        const struct feature *rows = features_nether_for("spikes", &n);

        (void)rows[0].generate(&p->world, r, x, y, z, rows[0].block, rows[0].count);
    }

    end_stage(p, END_SPIKE);
}

/* The dragon at chunk 0,0: the yaw it draws from the decorate Random. */
void populate_end_dragon(struct populate *p, int cx, int cz)
{
    end_dragon_last.spawned = (cx == 0 && cz == 0);

    if (end_dragon_last.spawned) end_dragon_last.yaw = jr_float(&p->end_rand) * 360.0F;
}
