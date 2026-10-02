/* The CUDA chunk generator: ChunkProviderGenerate.provideChunk for the
 * overworld (the biome GenLayers, the density noise, the surface pass, caves,
 * ravines, then the Chunk constructor's copy) as a pipeline of batched kernels
 * over (seed, cx, cz) requests. Its oracle is the C engine's provide_chunk
 * (csrc/engine/chunkgen.c); worldgen_check (check.c) compares the two
 * byte for byte, stage by stage.
 *
 * Every stage is a pure function of (seed, cx, cz), so a batch may mix seeds
 * and chunks in any order. The kernels are built with --fmad=false and IEEE
 * division and square root, and follow the C engine's operation order. */
#ifndef NETHERITE_WORLDGEN_H
#define NETHERITE_WORLDGEN_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WORLDGEN_CELLS 65536

/* One row of csrc/engine/biomes.h in a plain layout both compilers read (the
 * header's designated initializers are C only). tables.c fills it. */
struct worldgen_biome {
    float root, variation, temperature;
    uint16_t top, top_meta, filler;
    int16_t base, object_id;
    uint8_t exists, cls, temp_cat, snow, mesa, surface, p1, p2;
};

void worldgen_biome_rows(struct worldgen_biome rows[256]);
/* each block id's light opacity and random-tick flag (csrc/engine/blocks.h),
 * for ids below 256: the built stage's tables. tables.c fills them. */
void worldgen_block_rows(uint8_t opacity[256], uint8_t tick_randomly[256]);

struct worldgen_req { int32_t seed_index, cx, cz; };

/* The kernels in pipeline order. worldgen_run(g, s) runs one of them over the
 * submitted batch, so a check can read every stage's output in between. */
enum worldgen_stage {
    WORLDGEN_LAYERS,     /* getBiomesForGeneration 10x10 and the voronoi 16x16 */
    WORLDGEN_DENSITY,    /* func_147423_a field, func_147424_a ids, the surface noise */
    WORLDGEN_SURFACE,    /* func_147422_a with the provider rand */
    WORLDGEN_CAVES,      /* MapGenCaves */
    WORLDGEN_RAVINES,    /* MapGenRavine */
    WORLDGEN_CONSTRUCT,  /* Chunk's constructor: metadata kept only under blocks */
    WORLDGEN_STAGES
};

/* Negative controls: each perturbs one constant in one stage, so a check can
 * show that the comparison fails at that stage and no earlier one. */
enum worldgen_negative {
    WORLDGEN_NEG_NONE,
    WORLDGEN_NEG_LAYERS,   /* voronoi jitter 3.6 -> 3.5 */
    WORLDGEN_NEG_FIELD,    /* main noise scale 8.555150000000001 -> 8.55515 */
    WORLDGEN_NEG_SURFACE,  /* surface depth noise / 3.0 + 3.0 -> + 3.1 */
    WORLDGEN_NEG_CAVES,    /* cave yaw drift * 4.0f -> * 4.01f */
    WORLDGEN_NEG_RAVINES,  /* ravine yaw drift * 0.05f -> * 0.06f */
    WORLDGEN_NEG_BUILT,    /* the sky fill's start 15 -> 14 (built.cu) */
};

/* Device error bits (worldgen_errors): a layer area larger than the capacity the
 * host sized, a cave branch where the port assumes none, a batch whose
 * carvers' tunnels or spheres overflowed the carvers' work area (carve.cuh),
 * or a metadata value over 15 under a block (the built stage; the C
 * constructor stops on one).
 * All are 0 on every chunk; nonzero means the output is not to be trusted. */
enum { WORLDGEN_ERR_LAYER_BOX = 1, WORLDGEN_ERR_BRANCH = 2, WORLDGEN_ERR_CARVE_CAP = 4, WORLDGEN_ERR_BUILT_META = 8 };

struct worldgen;

/* The host thread sleeps while it waits for the device instead of spinning
 * (cudaDeviceScheduleBlockingSync): call before anything else touches the
 * device. 0 on success. */
int worldgen_blocking_sync(void);

/* Allocates for batches up to max_batch chunks over up to max_seeds seeds on
 * the current CUDA device; NULL (with a message on stderr) on failure. */
struct worldgen *worldgen_create(int max_batch, int max_seeds, int negative);
void worldgen_free(struct worldgen *g);

/* Runs the generator on stream (a cudaStream_t; NULL, the default, the
 * legacy default stream): its kernels and copies, and worldgen_generate's and
 * the fetches' waits are the stream's, not the device's. */
void worldgen_set_stream(struct worldgen *g, void *stream);
/* page-locked host memory (cudaHostAlloc), NULL on failure, and its free */
void *worldgen_host_alloc(size_t bytes);
void worldgen_host_free(void *p);
/* a new non-blocking stream on the current device, at its highest
 * priority (low: its lowest; NULL on failure) */
void *worldgen_stream_new(int low);
/* the calling thread's current device, and making dev current (0 on success):
 * a generator made on one thread and run on another */
int worldgen_device_current(void);
int worldgen_device_use(int dev);

/* Builds each seed's generator state on the device (the noise permutations,
 * the layer seeds, the Mesa bands, the carver longs): what chunkgen_init does. */
int worldgen_set_seeds(struct worldgen *g, const int64_t *seeds, int n);

/* Uploads a batch; the stages then run over it. */
int worldgen_submit(struct worldgen *g, const struct worldgen_req *req, int n);
int worldgen_run(struct worldgen *g, int stage);
/* Every stage in order, then waits: provide_chunk over the whole batch. */
int worldgen_generate(struct worldgen *g);

/* Copies back (after a worldgen_run or worldgen_generate). Arrays are per chunk in
 * request order: ids and metas WORLDGEN_CELLS each (index x << 12 | z << 8 | y;
 * every id the raw pipeline makes is below 256), biomes 100 (generation, raw
 * object ids) and 256 (index z << 4 | x), field 825 doubles. */
int worldgen_fetch_ids(struct worldgen *g, uint8_t *ids);
int worldgen_fetch_metas(struct worldgen *g, uint8_t *metas);
int worldgen_fetch_biomes(struct worldgen *g, uint8_t *gen100, uint8_t *full256);
int worldgen_fetch_field(struct worldgen *g, double *field);
/* the surface pass's topBlock table after each chunk (256 per chunk, by biome
 * id) and the provider Random's internal state after it (one per chunk):
 * the provider state the generation leaves */
int worldgen_fetch_tops(struct worldgen *g, uint8_t *tops, uint64_t *rands);
int worldgen_fetch_sin(struct worldgen *g, float *table);  /* MathHelper's SIN_TABLE, 65536 */
unsigned worldgen_errors(struct worldgen *g);

/* The built stage (built.cu): each chunk of the batch as the Chunk
 * constructor and Chunk.generateSkylightMap leave it (chunkgen_construct: the
 * bands a block not air gives storage, their random-tick counts, the height
 * map, heightMapMinimum and the sky light), so a host that takes a generated
 * chunk copies bands instead of building them. Per chunk a header, and band k
 * at bands + (i * 16 + k) * WORLDGEN_BAND_BYTES: its ids (4096, index x << 8
 * | z << 4 | y), metadata (2048 nibbles, the constructor's rule: 0 under
 * air) and sky light (2048 nibbles, 0 where the fill wrote none), nibble i
 * at byte i >> 1, the low half for even i; a band not in the mask is zeros.
 * worldgen_fetch_built runs the stage over the generated batch (after
 * worldgen_generate) and copies the headers, then bands 0 up to the batch's
 * highest band in the mask (the rest of each chunk's 16 are left alone);
 * *top_band (NULL: not wanted) is that band, -1 when no chunk has one. */
#define WORLDGEN_BAND_BYTES 8192
struct worldgen_built {
    uint16_t mask;              /* bands with storage */
    uint16_t metas_any;         /* bands whose metadata is not all 0 */
    uint16_t ticking[16];       /* each band's random-tick count (every band: an empty one's is air's) */
    int32_t height_min;         /* heightMapMinimum (INT_MAX: no column has an opaque block) */
    int32_t height[256];        /* heightMap, index z << 4 | x (0 where the column has none) */
};
int worldgen_fetch_built(struct worldgen *g, struct worldgen_built *hdr, uint8_t *bands, int *top_band);
/* bytes of shared memory the layer kernel uses per chunk */
int worldgen_layer_shared_bytes(struct worldgen *g);

/* ---------------------------------------------------------------- the Nether and the End
 *
 * ChunkProviderHell.provideChunk (the density pass with its lava ocean, the
 * soul sand, gravel and bedrock pass, MapGenCavesHell) and
 * ChunkProviderEnd.provideChunk (the island density, its literal no-op
 * surface pass) as one batched pipeline over (seed, dim, cx, cz) requests; a
 * batch may mix both dimensions and any seeds. Its oracle is the C engine's
 * nether.c and end.c, and world.c's load_dim_chunk for the finished chunk;
 * worldgen_dim_check (dim_check.c) compares them stage by stage. Both dimensions
 * are 128 high: the stages before the constructor keep the providers' raw
 * array, WORLDGEN_DIM_RAW cells, index x << 11 | z << 7 | y. Neither provider writes
 * metadata, so a chunk's metas are all 0 and there is no meta array. */

#define WORLDGEN_DIM_RAW 32768

struct worldgen_dim_req { int32_t seed_index, dim, cx, cz; };   /* dim -1 the Nether, 1 the End */

enum worldgen_dim_stage {
    WORLDGEN_DIM_DENSITY,    /* the density field and its fill (func_147419_a, func_147420_a), the Nether's surface noise */
    WORLDGEN_DIM_SURFACE,    /* func_147418_b with the provider rand, func_147421_b */
    WORLDGEN_DIM_CAVES,      /* MapGenCavesHell (the End has none) */
    WORLDGEN_DIM_CONSTRUCT,  /* Chunk(World, Block[], cx, cz) and the provider's biome copy */
    WORLDGEN_DIM_STAGES
};

/* Negative controls, as for the overworld: one constant in one stage. */
enum worldgen_dim_negative {
    WORLDGEN_DIM_NEG_NONE,
    WORLDGEN_DIM_NEG_FIELD,      /* Nether main noise y scale 2053.236 / 60.0 -> / 60.1; End edge * 8.0f -> * 8.01f */
    WORLDGEN_DIM_NEG_TERRAIN,    /* Nether lava ocean below y 32 -> 31; End y step * 0.25 -> * 0.26 */
    WORLDGEN_DIM_NEG_SURFACE,    /* Nether depth noise / 3.0 + 3.0 -> + 3.1 (the End's pass is a no-op) */
    WORLDGEN_DIM_NEG_CAVES,      /* Nether cave yaw drift * 4.0f -> * 4.01f */
    WORLDGEN_DIM_NEG_CONSTRUCT,  /* the two dimensions' biome ids swapped */
};

struct worldgen_dim;

struct worldgen_dim *worldgen_dim_create(int max_batch, int max_seeds, int negative);
void worldgen_dim_free(struct worldgen_dim *g);
/* as worldgen_set_stream */
void worldgen_dim_set_stream(struct worldgen_dim *g, void *stream);
/* each seed's Nether and End noise generators and cave longs, on the device */
int worldgen_dim_set_seeds(struct worldgen_dim *g, const int64_t *seeds, int n);
int worldgen_dim_submit(struct worldgen_dim *g, const struct worldgen_dim_req *req, int n);
int worldgen_dim_run(struct worldgen_dim *g, int stage);
int worldgen_dim_generate(struct worldgen_dim *g);

/* Per chunk in request order: the raw array (WORLDGEN_DIM_RAW ids), the density
 * field (425 doubles: the Nether's 5x17x5, index (i * 5 + j) * 17 + y; the End
 * uses the first 297, its 3x33x3), and the constructed chunk's ids
 * (WORLDGEN_CELLS, index x << 12 | z << 8 | y) and biome array (256, z << 4 | x). */
int worldgen_dim_fetch_raw(struct worldgen_dim *g, uint8_t *raw);
int worldgen_dim_fetch_field(struct worldgen_dim *g, double *field);
int worldgen_dim_fetch_chunk(struct worldgen_dim *g, uint8_t *ids, uint8_t *biomes);
/* the provider Random's internal state after each chunk */
int worldgen_dim_fetch_rand(struct worldgen_dim *g, uint64_t *rands);
unsigned worldgen_dim_errors(struct worldgen_dim *g);

#ifdef __cplusplus
}
#endif

#endif
