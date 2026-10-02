/* The populate driver, see populate.h. Every world effect goes through the
 * world core's setBlock path (world_set_block), so the write listener, the
 * block callbacks, the queued light engine and the scheduled-update list see
 * exactly what vanilla's populate sees. The four overworld structure
 * generators' blocks are native (the *_blocks.c halves over
 * structure_blocks.c's sc_generate), driven here in vanilla's order out of the
 * generators' own structure maps. */
#include "populate.h"
#include "env.h"

#include "blocks.h"
#include "features.h"
#include "features_lakes.h"
#include "biomes.h"
#include "layers.h"
#include "mineshaft.h"
#include "noise.h"
#include "stronghold.h"
#include "structure.h"
#include "structure_blocks.h"
#include "temple.h"
#include "temple_blocks.h"
#include "ticks.h"
#include "village.h"


#include <stdlib.h>
#include <string.h>

/* Block ids the driver names, from Blocks. */
enum {
    P_WATER = 8, P_STILL_WATER = 9, P_LAVA = 10, P_STILL_LAVA = 11, P_SNOW_LAYER = 78,
    P_ICE = 79, P_PACKED_ICE = 174, P_LEAVES = 18,
};

/* The four overworld structure generators in ChunkProviderGenerate field
 * order: mineshaftGenerator, villageGenerator, strongholdGenerator,
 * scatteredFeatureGenerator. The whole-world state each type needs (the
 * village and temple weight tables, the stronghold's positions) initializes
 * once, at construction, through its own begin(). */
static const struct structure_type *const POP_TYPES[4] = {
    &structure_mineshaft, &structure_village, &structure_stronghold, &structure_temple,
};

static int (*const POP_BLOCKS[4])(struct sc_ctx *c, struct piece *p) = {
    mineshaft_blocks, village_blocks, stronghold_blocks, temple_blocks,
};

static void fire_stage(struct populate *p, int stage)
{
    if (p->stage.fn != NULL) p->stage.fn(p->stage.ctx, stage, (uint64_t)p->rand.seed);
}

void populate_stage_now(struct populate *p, int stage)
{
    fire_stage(p, stage);
}

/* World.canBlockFreeze and World.func_147478_e's 0.15F, in one place; the
 * negative check (run_config) moves it. */
static float snow_threshold(void)
{
    return nw_env->cfg.populate_negative_snow ? 0.25F : 0.15F;
}

/* world.c's on_chunk is (void *, int, int); the world pointer is the driver. */
static void offer_trampoline(void *ctx, int cx, int cz)
{
    populate_offer_chunk((struct populate *)ctx, cx, cz);
}

static struct chunk *chunk_at(struct populate *p, int cx, int cz)
{
    return world_load_chunk(&p->world, cx, cz);
}

void populate_init(struct populate *p, int64_t seed)
{
    memset(p, 0, sizeof *p);
    world_init(&p->world, seed);
    p->world.on_chunk = offer_trampoline;
    p->world.on_chunk_ctx = p;

    /* BiomeGenBase's two static perlins, built from their own Randoms */
    jrand r;
    jr_seed(&r, 1234L);
    perlin_init(&p->temp_noise, &r, 1);
    jr_seed(&r, 2345L);
    perlin_init(&p->flower_noise, &r, 1);

    /* MapGenBase.func_151539_a's two placement longs: one Random, reseeded
     * from the world seed at every call, so the two longs are constants of the
     * world (and the same for all four types) */
    jr_seed(&r, seed);
    p->mul_x = jr_long(&r);
    p->mul_z = jr_long(&r);

    /* each type's once-per-walk state (the village and temple weight tables,
     * the stronghold's positions) initializes exactly once, at construction */
    for (int t = 0; t < 4; ++t)
        if (POP_TYPES[t]->begin != NULL) POP_TYPES[t]->begin(seed);

    for (int t = 0; t < 4; ++t) jhm64_init(&p->maps[t].keys);
}

void populate_reseed(struct populate *p, int64_t seed)
{
    p->world.seed = seed;
    /* the overworld provider is built from the seed at its next use too
     * (world_gen, which keeps gen.owed) */
    p->world.gen_made = 0;
    /* the Nether's and the End's providers are built from the seed at their
     * next use */
    p->world.nether_ready = p->world.end_ready = 0;

    jrand r;
    jr_seed(&r, seed);
    p->mul_x = jr_long(&r);
    p->mul_z = jr_long(&r);
    if (p->world.dim == -1)
    {
        const struct structure_type *f = structure_type_by_name("Fortress");
        if (f->begin != NULL) f->begin(seed);
    }
    else if (p->world.dim == 0)
        for (int t = 0; t < 4; ++t)
            if (POP_TYPES[t]->begin != NULL) POP_TYPES[t]->begin(seed);
}

void populate_free(struct populate *p)
{
    world_free(&p->world);

    for (int t = 0; t < 4; ++t)
    {
        for (int i = 0; i < p->maps[t].n; ++i) start_free(&p->maps[t].starts[i].start);

        free(p->maps[t].starts);
        jhm64_free(&p->maps[t].keys);
    }

    free(p->order);
}

/* ------------------------------------------------------------- the structure maps */

/* StructureStart.isSizeableStructure, latched when the start was built: the
 * village's hasMoreThanTwoComponents, every other type's constant true. */
static struct bbox step_box(int cx, int cz);

/* A mineshaft start's pieces as bytes: the count, then each piece's struct
 * and a room's linked boxes. The ids are the type's string constants. 0 for
 * a start with another kind of piece. */
static int start_pieces_out(const struct start *s, struct rspill_out *o)
{
    uint32_t n = (uint32_t)s->n;
    rspill_out_put(o, &n, sizeof n);
    for (int i = 0; i < s->n; ++i)
    {
        const struct piece *q = s->pieces[i];
        if (q->owned != NULL || q->kind > PIECE_STAIRS) return 0;
        rspill_out_put(o, q, sizeof *q);
        if (q->kind == PIECE_ROOM)
        {
            n = (uint32_t)q->u.room.n;
            rspill_out_put(o, &n, sizeof n);
            rspill_out_put(o, q->u.room.v, (size_t)q->u.room.n * sizeof *q->u.room.v);
        }
    }
    return 1;
}

static void start_reload(struct pop_start *s)
{
    uint32_t len;
    const uint8_t *p = rspill_blob_get(s->blob, &len), *end = p + len;
    s->blob = 0;
    uint32_t n = rspill_in_u32(&p, end);
    for (uint32_t i = 0; i < n; ++i)
    {
        struct piece *q = malloc(sizeof *q);
        if (q == NULL) abort();
        rspill_in(&p, end, q, sizeof *q);
        if (q->kind == PIECE_ROOM)
        {
            q->u.room.n = (int)rspill_in_u32(&p, end);
            q->u.room.v = q->u.room.cap ? malloc((size_t)q->u.room.cap * sizeof *q->u.room.v) : NULL;
            rspill_in(&p, end, q->u.room.v, (size_t)q->u.room.n * sizeof *q->u.room.v);
        }
        start_add(&s->start, q);
    }
}

void populate_spill_far(struct populate *p, int type, double x, double z, int reach)
{
    struct pop_map *m = &p->maps[type];
    for (int i = 0; i < m->n; ++i)
    {
        struct pop_start *s = &m->starts[i];
        const struct bbox *b = &s->start.bb;
        if (s->blob != 0 || s->start.n == 0) continue;
        double dx = x < b->minX ? b->minX - x : x > b->maxX + 1 ? x - b->maxX - 1 : 0.0;
        double dz = z < b->minZ ? b->minZ - z : z > b->maxZ + 1 ? z - b->maxZ - 1 : 0.0;
        if (dx <= reach && dz <= reach) continue;
        struct rspill_out o = {0};
        if (start_pieces_out(&s->start, &o) && (s->blob = rspill_blob_put(o.p, (uint32_t)o.n)) != 0)
            start_free(&s->start);
        free(o.p);
    }
}

/* the chunks whose step box meets the start's box (pop_start.pop_left) */
static int start_pop_chunks(const struct start *s)
{
    const struct bbox *b = &s->bb;
    if (b->maxY < 1 || b->minY > 512) return 0;
    /* cx * 16 + 8 <= maxX and cx * 16 + 23 >= minX, likewise z */
    int x0 = (b->minX - 23) >> 4, x1 = (b->maxX - 8) >> 4;
    int z0 = (b->minZ - 23) >> 4, z1 = (b->maxZ - 8) >> 4;
    int n = 0;
    for (int cx = x0; cx <= x1; ++cx)
        for (int cz = z0; cz <= z1; ++cz)
        {
            struct bbox box = step_box(cx, cz);
            n += bbox_intersects(b, &box);
        }
    return n;
}

static int start_sizeable(const struct start *s)
{
    return s->n > 0 && s->pieces[0]->kind == PIECE_VILLAGE ? s->pieces[0]->u.village.valid : 1;
}

void populate_offer_chunk(struct populate *p, int cx, int cz)
{
    for (int t = 0; t < 4; ++t)
    {
        const struct structure_type *t_ = POP_TYPES[t];
        struct pop_map *m = &p->maps[t];

        /* MapGenBase.func_151539_a: the candidates -8..8 around the chunk,
         * x outer, z inner */
        for (int dx = -8; dx <= 8; ++dx)
        {
            for (int dz = -8; dz <= 8; ++dz)
            {
                int ox = cx + dx, oz = cz + dz;

                if (jhm64_contains(&m->keys, chunk_key(ox, oz))) continue;   /* func_151538_a's containsKey */

                /* MapGenStructure.func_151538_a: one nextInt() per offered
                 * candidate, then the spawn test on the candidate's own seed */
                jrand r;
                uint64_t kx = (uint64_t)ox * (uint64_t)p->mul_x;
                uint64_t kz = (uint64_t)oz * (uint64_t)p->mul_z;
                jr_seed(&r, (int64_t)(kx ^ kz ^ (uint64_t)p->world.seed));
                jr_int(&r);

                if (!t_->can_spawn(&r, ox, oz)) continue;

                if (m->n == m->cap)
                {
                    m->cap = m->cap ? m->cap * 2 : 32;
                    m->starts = realloc(m->starts, (size_t)m->cap * sizeof *m->starts);
                }

                struct pop_start *s = &m->starts[m->n];

                start_init(&s->start, t_->name, ox, oz);
                t_->make_start(&r, ox, oz, &s->start);
                s->sizeable = start_sizeable(&s->start);
                s->pop_left = start_pop_chunks(&s->start);
                s->blob = 0;

                jhm64_put(&m->keys, chunk_key(ox, oz), m->n);
                ++m->n;
            }
        }
    }
}

/* A start the world's structureMap already holds (a snapshot's
 * structures.json), rebuilt as func_151538_a built it: the candidate's seeded
 * Random, its nextInt, the spawn test's draws on it, then the start. The spawn
 * tests that reseed World.rand run on a private one here: this is a rebuild,
 * not a tick. */
void populate_add_start(struct populate *p, int type, int ox, int oz)
{
    const struct structure_type *t_ = POP_TYPES[type];
    struct pop_map *m = &p->maps[type];

    if (jhm64_contains(&m->keys, chunk_key(ox, oz))) return;

    jrand *world_rand = populate_world_rand;
    populate_world_rand = NULL;

    jrand r;
    uint64_t kx = (uint64_t)ox * (uint64_t)p->mul_x;
    uint64_t kz = (uint64_t)oz * (uint64_t)p->mul_z;
    jr_seed(&r, (int64_t)(kx ^ kz ^ (uint64_t)p->world.seed));
    jr_int(&r);
    (void)t_->can_spawn(&r, ox, oz);

    if (m->n == m->cap)
    {
        m->cap = m->cap ? m->cap * 2 : 32;
        m->starts = realloc(m->starts, (size_t)m->cap * sizeof *m->starts);
    }

    struct pop_start *s = &m->starts[m->n];

    start_init(&s->start, t_->name, ox, oz);
    t_->make_start(&r, ox, oz, &s->start);
    s->sizeable = start_sizeable(&s->start);
    /* the snapshot's start: chunks it met may have populated before, and
     * would never count down */
    s->pop_left = -1;
    s->blob = 0;

    jhm64_put(&m->keys, chunk_key(ox, oz), m->n);
    ++m->n;
    populate_world_rand = world_rand;
}

int populate_restore_pieces(struct populate *p, int type, int ox, int oz, const int *v, int n, const int *flags)
{
    struct pop_map *m = &p->maps[type];
    int idx = -1;
    for (int i = 0; i < m->n && idx < 0; ++i)
        if (m->starts[i].start.chunk_x == ox && m->starts[i].start.chunk_z == oz) idx = i;
    if (idx < 0 || n % 8 != 0) return 0;
    struct start *s = &m->starts[idx].start;
    int nsaved = n / 8;
    struct piece **out = malloc((size_t)(nsaved > 0 ? nsaved : 1) * sizeof *out);
    char *used = calloc((size_t)(s->n > 0 ? s->n : 1), 1);
    if (!out || !used) abort();
    for (int k = 0; k < nsaved; ++k)
    {
        const int *c = v + 8 * k;
        struct piece *hit = NULL;
        for (int i = 0; i < s->n && hit == NULL; ++i)
        {
            const struct bbox *b = &s->pieces[i]->bb;
            if (!used[i] && b->minX == c[0] && b->minZ == c[2] && b->maxX == c[3] && b->maxZ == c[5] &&
                b->maxY - b->minY == c[4] - c[1])
            {
                used[i] = 1;
                hit = s->pieces[i];
            }
        }
        if (hit == NULL)
        {
            free(out);
            free(used);
            return 0;
        }
        hit->bb = bbox_make(c[0], c[1], c[2], c[3], c[4], c[5]);
        if (hit->kind == PIECE_VILLAGE)
        {
            hit->u.village.hpos = c[6];
            hit->u.village.vcount = c[7];
        }
        else if (hit->kind == PIECE_TEMPLE)
            hit->u.temple.hpos = c[6];
        /* the once-set generation flags (structures.json <Type>.flags) */
        if (flags != NULL)
        {
            int f = flags[k];
            if (hit->kind == PIECE_CORRIDOR) hit->u.corridor.spawner_placed = f & 1;
            else if (hit->kind == PIECE_VILLAGE && hit->u.village.kind == V_HOUSE2) hit->u.village.chest = (f >> 1) & 1;
            else if (hit->kind == PIECE_STRONGHOLD) stronghold_piece_set_flags(hit, (f >> 1) & 1, (f >> 2) & 1);
            else if (hit->kind == PIECE_TEMPLE)
            {
                if (hit->u.temple.kind == TEMPLE_DESERT_PYRAMID)
                    for (int i = 0; i < 4; ++i) hit->u.temple.v.desert.has_placed_chest[i] = (f >> (3 + i)) & 1;
                else if (hit->u.temple.kind == TEMPLE_JUNGLE_PYRAMID)
                {
                    hit->u.temple.v.jungle.placed_main_chest = (f >> 7) & 1;
                    hit->u.temple.v.jungle.placed_hidden_chest = (f >> 8) & 1;
                    hit->u.temple.v.jungle.placed_trap1 = (f >> 9) & 1;
                    hit->u.temple.v.jungle.placed_trap2 = (f >> 10) & 1;
                }
                else hit->u.temple.v.swamp.has_witch = (f >> 11) & 1;
            }
        }
        out[k] = hit;
    }
    /* the pieces population dropped (addComponentParts false) are gone from
     * the saved list; they stay allocated, as sc_generate's drop leaves them */
    for (int k = 0; k < nsaved; ++k) s->pieces[k] = out[k];
    s->n = nsaved;
    free(out);
    free(used);
    return 1;
}

/* WorldServer.createSpawnPosition: the biome search a fresh overworld runs to
 * pick the world spawn, WorldChunkManager.func_150795_a over the generator's
 * 1/4-resolution biome layer, a 129x129 grid of 4-block cells 256 blocks
 * around (0, 0). The `new Random(seed)` the search was handed is returned,
 * because createSpawnPosition's fuzz loop keeps drawing from it. */
void populate_biome_spawn_search(int64_t seed, int *sx, int *sz, uint64_t *rand_state)
{
    /* WorldChunkManager's biomesToSpawnIn, as the registry ids of the biome
     * objects it lists: forest, plains, taiga, taigaHills, forestHills,
     * jungle, jungleHills. */
    static const int spawn_biomes[] = {4, 1, 5, 19, 18, 21, 22};

    struct layers l;
    int w = 129, h = 129;
    int *grid = malloc((size_t)w * (size_t)h * sizeof *grid);

    if (grid == NULL) abort();

    layers_init(&l, seed);
    biomes_gen(&l, -64, -64, w, h, grid);

    /* func_150795_a's reservoir sample: the first matching cell wins, and
     * every later one replaces it with probability 1/(matches so far + 1) */
    jrand r;
    jr_seed(&r, seed);

    int bx = 0, bz = 0, n = 0;

    for (int i = 0; i < w * h; ++i)
    {
        int x = (-64 + i % w) << 2, z = (-64 + i / w) << 2;
        int match = 0;

        for (size_t k = 0; k < sizeof spawn_biomes / sizeof spawn_biomes[0]; ++k)
            if (grid[i] == spawn_biomes[k]) match = 1;

        if (match && (n == 0 || jr_int_n(&r, n + 1) == 0))
        {
            bx = x;
            bz = z;
            ++n;
        }
    }

    free(grid);
    *sx = bx;
    *sz = bz;
    if (rand_state != NULL) *rand_state = r.seed;
}

/* The same search for the callers that only want the position. */
static void initial_spawn(int64_t seed, int *sx, int *sz)
{
    populate_biome_spawn_search(seed, sx, sz, NULL);
}

int populate_initial_chunks(struct populate *p)
{
    int sx, sz;

    initial_spawn(p->world.seed, &sx, &sz);

    /* MinecraftServer.initialWorldChunkLoad: var11 = -192..192 step 16 outer,
     * var12 inner; the chunk the load lands in */
    int n = 0;

    for (int a = -192; a <= 192; a += 16)
        for (int b = -192; b <= 192; b += 16)
        {
            populate_offer_chunk(p, (sx + a) >> 4, (sz + b) >> 4);
            ++n;
        }

    return n;
}

int populate_map_size(struct populate *p, int type)
{
    return p->maps[type].n;
}

/* MapGenStructure.generateStructuresInChunk's filter, and the order its
 * values() iterator hands the starts over in. The order comes back through
 * p->order (grown on demand); it is a snapshot, as Java's iterator walks the
 * map as it stood when the call began. */
static int map_order(struct populate *p, int type)
{
    struct pop_map *m = &p->maps[type];

    if (m->n > p->cap_order)
    {
        p->cap_order = m->n ? m->n : 1;
        p->order = realloc(p->order, (size_t)p->cap_order * sizeof *p->order);
    }

    /* the negative check: the walk's own order instead of the map's */
    if (nw_env->cfg.populate_negative_maporder)
    {
        for (int i = 0; i < m->n; ++i) p->order[i] = i;
        return m->n;
    }

    return jhm64_order(&m->keys, p->order, NULL);
}

/* MapGenStructure.generateStructuresInChunk: the box is (cx*16+8, cz*16+8)
 * .. (+15, +15); StructureBoundingBox's four-argument constructor makes it y
 * 1..512. A start generates when it is sizeable and its box meets it. */
static struct bbox step_box(int cx, int cz)
{
    int cx8 = cx * 16 + 8, cz8 = cz * 16 + 8;

    return bbox_make(cx8, 1, cz8, cx8 + 15, 512, cz8 + 15);
}

int populate_structures_count(struct populate *p, int type, int cx, int cz)
{
    struct pop_map *m = &p->maps[type];
    struct bbox box = step_box(cx, cz);
    int n = map_order(p, type);
    int c = 0;

    for (int i = 0; i < n; ++i)
    {
        struct pop_start *s = &m->starts[p->order[i]];

        if (s->sizeable && bbox_intersects(&s->start.bb, &box)) ++c;
    }

    return c;
}

int populate_structures(struct populate *p, int type, int cx, int cz)
{
    struct pop_map *m = &p->maps[type];
    struct bbox box = step_box(cx, cz);
    int n = map_order(p, type);
    int any = 0;

    /* the environment a structure block half runs in: the world, the step
     * chunk's box, the shared population Random. Det's streams and World.rand
     * are the probe's own (the entities a piece constructs are not part of
     * this record); with none installed a drop draws nothing, as the seam
     * note in structure_blocks.c says. */
    struct sc_ctx c;

    memset(&c, 0, sizeof c);
    c.w = &p->world;
    c.box = box;
    c.rand = &p->rand;
    c.piece_fn = POP_BLOCKS[type];
    /* the pieces' entity constructions (a corridor's chest cart, a village's
     * villagers) and their drops: only a caller that models Det's streams and
     * World.rand installs them, and then it owns their role (populate.h) */
    c.det = populate_det;
    c.world_rand = populate_world_rand;

    for (int i = 0; i < n; ++i)
    {
        struct pop_start *s = &m->starts[p->order[i]];

        if (!bbox_intersects(&s->start.bb, &box)) continue;
        if (s->blob != 0) start_reload(s);
        if (s->sizeable)
        {
            sc_generate(&c, &s->start);
            any = 1;
        }
        /* this chunk populates once: one fewer that can reach the start */
        if (s->pop_left > 0 && --s->pop_left == 0 && type <= 1) start_free(&s->start);
    }

    sc_free_ents(&c);

    fire_stage(p, POP_MINESHAFT + type);

    /* ChunkProviderGenerate.populate keeps this return for the village
     * generator alone, and gates the two lakes on it; the negative check
     * drops that gate. */
    return nw_env->cfg.populate_negative_village ? 0 : any;
}

/* ---------------------------------------------------------------- Chunk bits */

int populate_top_filled_segment(const struct chunk *c)
{
    /* the highest section present, by its bit in the mask */
    return c->mask != 0 ? (31 - __builtin_clz((unsigned)c->mask)) << 4 : 0;
}

static const struct chunk_sec *column_band(const struct chunk *c, int y);

/* BiomeGenBase.getFloatTemperature: the plain temperature at or below y 64,
 * above it the biome's temperature minus the shared perlin's drop. */
float populate_float_temperature(struct populate *p, int biome, int x, int y, int z)
{
    if (y > 64)
    {
        double noise = perlin_point(&p->temp_noise, (double)x * 1.0 / 8.0, (double)z * 1.0 / 8.0);
        float var4 = (float)noise * 4.0F;
        return BIOMES[biome].temperature - (var4 + (float)y - 64.0F) * 0.05F / 30.0F;
    }

    return BIOMES[biome].temperature;
}

/* PRECIP_STOP[id]: the block's material blocks movement or is a liquid
 * (where getPrecipitationHeight's walk stops). Built before main from BLOCKS
 * and MATERIALS, the same for every environment. */
static uint8_t PRECIP_STOP[sizeof BLOCKS / sizeof BLOCKS[0]];

__attribute__((constructor)) static void precip_stop_init(void)
{
    for (size_t id = 0; id < sizeof BLOCKS / sizeof BLOCKS[0]; ++id)
    {
        const struct material_def *m = &MATERIALS[BLOCKS[id].material];

        PRECIP_STOP[id] = m->blocks_movement || m->is_liquid;
    }
}

int populate_precipitation_height(struct populate *p, int x, int z)
{
    struct chunk *c = chunk_at(p, x >> 4, z >> 4);
    int idx = (x & 15) | (z & 15) << 4;

    if (c->precip[idx] != -999) return c->precip[idx];

    /* the walk down from the top filled segment, band by band (a band with
     * no storage is air all through); it reads nothing below y 1 */
    int y = populate_top_filled_segment(c) + 15;
    int h = -1;

    while (y > 0 && h == -1)
    {
        int y0 = y & ~15;
        const struct chunk_sec *s = column_band(c, y);

        if (y0 < 1) y0 = 1;
        if (s == NULL)
        {
            y = y0 - 1;
            continue;
        }

        int base = SEC_XYZ(x & 15, 0, z & 15);

        for (; y >= y0; --y)
            if (PRECIP_STOP[chunk_sec_id(s, base | (y & 15))])
            {
                h = y + 1;
                break;
            }
    }

    c->precip[idx] = h;
    return h;
}

/* --------------------------------------------------------------- func_150809_p */

/* A band's cells, NULL for air: a section the mask leaves out or one with
 * no storage reads air. */
static const struct chunk_sec *column_band(const struct chunk *c, int y)
{
    return (c->mask >> (y >> 4)) & 1 ? chunk_sec_at(c, y >> 4) : NULL;
}

/* Chunk.func_150811_f(x, z): walk the column from the top filled segment down,
 * relighting through the queued light engine; false when one relight reports
 * no change (world.c's walk, the same as the server world's) */
static int sky_column_check(struct populate *p, struct chunk *c, int x, int z)
{
    return world_light_column_check(&p->world, c, x, z);
}

/* Chunk.func_150801_a: the 16 edge columns of a populated neighbour learn the
 * chunk beside them arrived. dir 3 west edge (x 15), 1 east edge (x 0),
 * 0 south edge (z 15), 2 north edge (z 0), as func_150809_p calls them. */
static void neighbour_edge_check(struct populate *p, struct chunk *n, int dir)
{
    if (!n->terrain_populated) return;

    for (int i = 0; i < 16; ++i)
    {
        int x, z;

        if (dir == 3) { x = 15; z = i; }
        else if (dir == 1) { x = 0; z = i; }
        else if (dir == 0) { x = i; z = 15; }
        else { x = i; z = 0; }

        sky_column_check(p, n, x, z);
    }
}

void populate_150809_p(struct populate *p, struct chunk *c)
{
    struct world *w = &p->world;

    c->terrain_populated = 1;
    c->light_populated = 1;

    /* Java's func_150809_p: the sky column check and the neighbours' edge
     * relights are the !hasNoSky branch, so the Nether and the End only set
     * the two flags */
    if (w->dim != 0) return;

    if (!world_check_chunks_exist(w, c->cx * 16 - 1, 0, c->cz * 16 - 1, c->cx * 16 + 1, 63, c->cz * 16 + 1))
    {
        c->light_populated = 0;
        return;
    }

    for (int x = 0; x < 16; ++x)
    {
        for (int z = 0; z < 16; ++z)
        {
            /* the vanilla break leaves the inner loop only; the outer
             * loop walks the next column even after a failed check */
            if (!sky_column_check(p, c, x, z))
            {
                c->light_populated = 0;
                break;
            }
        }
    }

    if (c->light_populated)
    {
        /* the four neighbours relight their facing edge; each
         * getChunkFromBlockCoords generates the chunk when it is missing */
        neighbour_edge_check(p, chunk_at(p, c->cx - 1, c->cz), 3);
        neighbour_edge_check(p, chunk_at(p, c->cx + 1, c->cz), 1);
        neighbour_edge_check(p, chunk_at(p, c->cx, c->cz - 1), 0);
        neighbour_edge_check(p, chunk_at(p, c->cx, c->cz + 1), 2);
    }
}

/* ------------------------------------------------------------ the call parts */

void populate_seed(struct populate *p, int cx, int cz)
{
    /* this.rand.setSeed(worldSeed); var7 = nextLong() / 2L * 2L + 1L; var9 the
     * same; this.rand.setSeed(cx * var7 + cz * var9 ^ worldSeed) */
    jr_seed(&p->rand, p->world.seed);
    int64_t var7 = jr_long(&p->rand);
    int64_t var9 = jr_long(&p->rand);
    var7 = var7 / 2 * 2 + 1;
    var9 = var9 / 2 * 2 + 1;

    uint64_t key = (uint64_t)cx * (uint64_t)var7 + (uint64_t)cz * (uint64_t)var9;
    jr_seed(&p->rand, (int64_t)(key ^ (uint64_t)p->world.seed));

    fire_stage(p, POP_SEED);
}

void populate_lakes(struct populate *p, int cx, int cz, int village)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;
    int bx = cx * 16, bz = cz * 16;
    int biome = world_get_biome(w, bx + 16, bz + 16);
    int n;

    if (biome != 2 && biome != 17 && !village && jr_int_n(r, 4) == 0)
    {
        int x = bx + jr_int_n(r, 16) + 8;
        int y = jr_int_n(r, 256);
        int z = bz + jr_int_n(r, 16) + 8;
        const struct feature *rows = features_lakes_for("waterlake", &n);
        rows->generate(w, r, x, y, z, rows->block, rows->count);
    }

    fire_stage(p, POP_WATERLAKE);

    if (!village && jr_int_n(r, 8) == 0)
    {
        int x = bx + jr_int_n(r, 16) + 8;
        int y = jr_int_n(r, jr_int_n(r, 248) + 8);
        int z = bz + jr_int_n(r, 16) + 8;

        if (y < 63 || jr_int_n(r, 10) == 0)
        {
            const struct feature *rows = features_lakes_for("lavalake", &n);
            rows->generate(w, r, x, y, z, rows->block, rows->count);
        }
    }

    fire_stage(p, POP_LAVALAKE);
}

void populate_dungeons(struct populate *p, int cx, int cz)
{
    struct world *w = &p->world;
    jrand *r = &p->rand;
    int bx = cx * 16, bz = cz * 16;

    for (int i = 0; i < 8; ++i)
    {
        int x = bx + jr_int_n(r, 16) + 8;
        int y = jr_int_n(r, 256);
        int z = bz + jr_int_n(r, 16) + 8;
        feature_dungeons(w, r, x, y, z, 0, 0);
    }

    fire_stage(p, POP_DUNGEONS);
}

/* One column's getFloatTemperature: the biome and the perlin drop depend on
 * x and z alone, so the two tests a column makes share them (the drop is
 * computed when a y above 64 first needs it). */
struct column_temp {
    int biome, have_noise;
    float drop;
};

static float column_temperature(struct populate *p, struct column_temp *t, int x, int y, int z)
{
    if (y > 64)
    {
        if (!t->have_noise)
        {
            double noise = perlin_point(&p->temp_noise, (double)x * 1.0 / 8.0, (double)z * 1.0 / 8.0);
            t->drop = (float)noise * 4.0F;
            t->have_noise = 1;
        }
        return BIOMES[t->biome].temperature - (t->drop + (float)y - 64.0F) * 0.05F / 30.0F;
    }

    return BIOMES[t->biome].temperature;
}

/* The perlin drop's reach: NoiseGeneratorSimplex.func_151605_a is 70 times a
 * sum of three corners, each (0.5 - r^2)^4 times a gradient's dot with an
 * offset of length r, at most sqrt(2) r; (0.5 - r^2)^4 r peaks at r^2 = 1/18,
 * so a corner is under 0.0131 and the point under 2.74, the drop (4 times it)
 * under 11. At y 256 the temperature falls by at most (11 + 192) * 0.05 / 30,
 * under 0.34: a biome warmer than the threshold by more stays above it at any
 * noise, and its test needs no noise. */
#define COLUMN_DROP_MAX 0.35F

/* column_temperature(...) > threshold, without the noise when the biome's
 * own temperature decides it */
static int column_above(struct populate *p, struct column_temp *t, int x, int y, int z, float threshold)
{
    if (y > 64 && !t->have_noise && BIOMES[t->biome].temperature > threshold + COLUMN_DROP_MAX) return 1;
    return column_temperature(p, t, x, y, z) > threshold;
}

/* World.canBlockFreeze(x, y, z, false): water at or below the 0.15 temperature,
 * block light under 10, still water. */
static int can_freeze(struct populate *p, struct column_temp *ct, int x, int y, int z)
{
    struct world *w = &p->world;

    if (column_above(p, ct, x, y, z, snow_threshold())) return 0;
    if (y < 0 || y >= 256) return 0;
    if (world_get_light(w, LIGHT_BLOCK, x, y, z) >= 10) return 0;

    int id = world_get_block(w, x, y, z) & 4095;

    if ((id == P_WATER || id == P_STILL_WATER) && world_get_meta(w, x, y, z) == 0) return 1;

    return 0;
}

/* World.func_147478_e(x, y, z, true): air at or below the 0.15 temperature,
 * block light under 10, where a snow layer can be placed. */
static int snow_here(struct populate *p, struct column_temp *ct, int x, int y, int z)
{
    struct world *w = &p->world;

    if (column_above(p, ct, x, y, z, snow_threshold())) return 0;
    if (y < 0 || y >= 256) return 0;
    if (world_get_light(w, LIGHT_BLOCK, x, y, z) >= 10) return 0;
    if (world_get_block(w, x, y, z) != 0) return 0;

    /* BlockSnow.canPlaceBlockAt on the block below */
    int below = world_get_block(w, x, y - 1, z) & 4095;

    if (below == P_ICE || below == P_PACKED_ICE) return 0;
    if (BLOCKS[below].material == P_LEAVES) return 1;
    if (below == P_SNOW_LAYER && (world_get_meta(w, x, y - 1, z) & 7) == 7) return 1;

    return BLOCKS[below].opaque_cube && MATERIALS[BLOCKS[below].material].blocks_movement;
}

void populate_freeze(struct populate *p, int cx, int cz)
{
    struct world *w = &p->world;
    int bx = cx * 16 + 8, bz = cz * 16 + 8;

    for (int i = 0; i < 16; ++i)
    {
        for (int j = 0; j < 16; ++j)
        {
            int x = bx + i, z = bz + j;
            int ph = populate_precipitation_height(p, x, z);
            /* the biome under the column: reading it has no effect, and the
             * ice write between the two tests leaves it */
            struct column_temp ct = {world_get_biome(w, x, z), 0, 0.0F};

            if (can_freeze(p, &ct, x, ph - 1, z)) world_set_block(w, x, ph - 1, z, P_ICE, 0, 2);

            if (snow_here(p, &ct, x, ph, z)) world_set_block(w, x, ph, z, P_SNOW_LAYER, 0, 2);
        }
    }

    fire_stage(p, POP_SNOW);
}

