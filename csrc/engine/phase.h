/* The tick as a sequence of phases (GPU audit L6, out/plan section 1): each
 * phase is a function of the environment with declared inputs and outputs,
 * and a digest of what it wrote. A GPU kernel for phase P is checked against
 * C one phase at a time: run C to P's start, run the kernel, and compare P's
 * digest (or the state behind it) with C's after P.
 *
 * The phases, in the order one row runs them (session.c, serverreplay.c,
 * servertick.c). The per-world ones run for the overworld, the Nether and
 * the End in worldServers order, their labels prefixed with the dimension
 * ("0.S4", "-1.S11"); a world with no chunks runs SE alone.
 *
 *   R0   the header's setup entries due at the row (MobMove)
 *   C1   client chunk and block sync (the chunk packets and the last tick's writes)
 *   C2   client player tick (Minecraft.runTick: input, onUpdate, sendMotionUpdates)
 *   TP   the paused server's save (its first paused row), then nothing ticks
 *   DV   the Dev entries at the head of the server tick
 *   S1   World.tick's updateWeather, areAllPlayersAsleep and wakeAllPlayers
 *   S2   SpawnerAnimals.findChunksForSpawning
 *   S1T  calculateSkylightSubtracted, the total and the day time
 *   S3   ChunkProviderServer.unloadQueuedChunks
 *   S4   tickUpdates: the scheduled block ticks
 *   S5   func_147456_g's head: the ambient countdown and the player's light check
 *   S6   func_147456_g's chunk loop (audit S6 to S8, interleaved per chunk on
 *        World.rand): mood sound, light flags, lightning, ice and snow, the
 *        random block ticks
 *   SG   villageSiegeObj.tick (the overworld's after its villages)
 *   S9   the tick's tail: updatePlayerInstances, the unload marks of a
 *        playerless Nether, removeStalePortalLocations, the village collection
 *   S10  World.updateEntities' weatherEffects (the bolts)
 *   S11  loadedEntityList: the player, the dragon and crystals, the livings,
 *        the items and orbs, the falling blocks and hangings
 *   S12  the tile entity walk
 *   NT   EntityTracker.updateTrackedEntities (the player's world)
 *   S13  the world's writes folded into Rows.blkHash (the harness's d.blk)
 *   SE   WorldServer.tick for a world with no chunks
 *   N1   the network tick: processPlayer and the row's actions
 *   N2   PlayerManager.updateMountedMovingPlayer (the C03's chunk loads)
 *   N1E  the action's end (its new entities join the pass order)
 *   N3   the tracker's packets queued for the client
 *   T    the t % 900 save's unload marks
 *
 * The state classes below partition what a phase can write; the table in
 * phase.c gives each phase its reads and writes. A phase's digest hashes the
 * classes it writes, after it ran:
 *
 *   SCALARS  every world's servertick scalars (weather, clocks, skylight,
 *            updateLCG, the ambient countdown, the siege's), its
 *            updateEntityTick and player manager mark, nextTickEntryID
 *   WRAND    every world's World.rand
 *   DET      the Det streams (seeders, Math, splits, the id counters) and
 *            the shuffle Random
 *   BLOCKS   every server world's chunk map (count, load order, requests), its
 *            provider's unload queue and player manager (cl_digest), and
 *            the chunks the phase wrote (a block, a metadata or a light
 *            write, or the load), whole: cells, light, heights, flags, in
 *            (cx, cz) order; so the digest is the state written, not the
 *            order it was written in
 *   PENDING  every world's pending block ticks, as a set
 *   ENTITIES every world's pass order, each entry's id and NBT (the player's
 *            place only), the bolts and the pool counts
 *   TILES    every world's tile entity walk list, each entity's NBT
 *   RECORD   the tick's write and spawn records and Rows.blkHash
 *   VILLAGE  the village collection, the siege and the portal caches
 *   PLAYER   the server player's NBT, its packet queue and chunk send queue
 *   TRACKER  the EntityTracker's entries and the tracker packets waiting for
 *            the client
 *   CLIENT   the client player, its entity mirrors, the chunks the phase
 *            wrote in the client's world, and the row's client packets
 *            (client_out)
 *
 * Tracing (test_snapshots --phase-trace FILE, --phase-digest, --phase-check)
 * is off by default; off, a phase boundary is one predictable branch. With
 * check on, every class is digested at both ends of every phase and a class
 * that changed outside the phase's writes is reported.
 *
 * Profiling (test_snapshots --phase-profile, make -C csrc phase-profile;
 * phaseprof.c) times the same marks: each phase run's cycles and heap
 * allocations (the latter when tests/allocount.c is preloaded), per world,
 * per row, plus the nested sub-marks below (inclusive, outermost entry only),
 * which split what the phases share: chunk provision, raw generation,
 * population, the light engine, the chunk loop's light flags and its random
 * block ticks, the pathfinder. Off, a sub-mark is one predictable branch. */
#ifndef NETHERITE_PHASE_H
#define NETHERITE_PHASE_H

#include <stdint.h>
#include <stdio.h>

enum phase_id {
    PH_R0, PH_C1, PH_C2, PH_TP, PH_DV,
    PH_S1, PH_S2, PH_S1T, PH_S3, PH_S4, PH_S5, PH_S6, PH_SG, PH_S9, PH_BE,
    PH_S10, PH_S11, PH_S12, PH_NT, PH_S13, PH_SE,
    PH_N1, PH_N2, PH_N1E, PH_N3, PH_T, PH_VD,
    PH_COUNT
};

/* the first and last per-world phase */
#define PH_WORLD_FIRST PH_S1
#define PH_WORLD_LAST PH_SE

enum phase_class {
    PHK_SCALARS, PHK_WRAND, PHK_DET, PHK_BLOCKS, PHK_PENDING, PHK_ENTITIES,
    PHK_TILES, PHK_RECORD, PHK_VILLAGE, PHK_PLAYER, PHK_TRACKER, PHK_CLIENT,
    PHK_COUNT
};

/* the profiler's nested sub-marks */
enum phase_sub {
    PS_LOAD,   /* ChunkProviderServer.loadChunk: the region store or generation, and the populate rule */
    PS_GEN,    /* raw generation, the chunk backend (world_chunk_request) */
    PS_POP,    /* ChunkProviderServer.populate (sr_populate, the Nether's and the End's on_chunk hooks) */
    PS_LIGHT,  /* the light engine, update_light_by_type (func_147463_c) */
    PS_NEWLIGHT, /* a populated chunk's func_150809_p and neighbour passes */
    PS_LFLAGS, /* S6's per-chunk light flags (Chunk.func_150804_b) */
    PS_RELIGHT, /* S6's Chunk.enqueueRelightChecks (func_147467_a's checkLight) */
    PS_RTICK,  /* S6's random block ticks, three per ticking section */
    PS_PATH,   /* the pathfinder's search (PathFinder.addToPath) */
    PS_SEEDW,  /* the region store's first build (sr_ensure_saved: the seed world's spawn area, or the checkpoint's regions) */
    /* the row's work outside every phase mark */
    PS_GRAVE,  /* grave_collect, between rows */
    PS_WBEGIN, /* sr_world_begin: the player's scene, the active chunk set, the spawner's player */
    PS_PARK,   /* serverreplay_park: packing the chunks the row's unloads parked, between rows */
    PS_S2CCOPY, /* a copy of the server's s2c queue: only when a tick appends to it while it is still the sent one (s2c_out) */
    PS_COUNT
};

/* phase_env.on's bits */
#define PHASE_TRACE 1
#define PHASE_PROF 2
#define PHASE_KERNEL 4   /* the kernel harness's hook alone, without the tracer's digests */

/* profile columns: a phase per world (dim + 1: the Nether, the overworld,
 * the End; a phase outside the worlds counts as the overworld's), then the
 * sub-marks */
#define PHP_SLOTS (PH_COUNT * 3)
#define PHP_COLS (PHP_SLOTS + PS_COUNT)

/* what a mark reads: the TSC, user-space instructions and cycles (the
 * process's own hardware counters), heap allocations */
enum phase_metric { PHX_TSC, PHX_INS, PHX_CYC, PHX_ALLOCS, PHX_COUNT };

struct phase_prof_row {
    int64_t t;
    uint64_t total[PHX_COUNT];  /* session_tick's */
    uint64_t v[PHX_COUNT][PHP_COLS];
};

/* The profiler's state (phaseprof.c), in the tracer's */
struct phase_prof {
    struct phase_prof_row *rows; /* one per profiled row, a mapping committed as written */
    size_t cap, n;
    long skipped;             /* rows without the server (not profiled) */
    int in_row;
    struct phase_prof_row cur;
    uint64_t ph0[PHX_COUNT], row0[PHX_COUNT], sub0[PS_COUNT][PHX_COUNT];
    int sub_depth[PS_COUNT];
    uint64_t runs[PHP_COLS];
    const volatile uint64_t *allocs; /* allocount.c's counter; NULL when not preloaded */
    int pmu_fd[2];            /* perf_event_open: user instructions, user cycles; -1 none */
    void *pmu_page[2];        /* their mmap pages, read with rdpmc when the kernel allows */
    int pmu_mode;             /* 0 none, 1 read(), 2 rdpmc */
    uint64_t t0_cyc, t0_ns;   /* the clock pair at the start, for TSC counts to ns */
    uint64_t first_row_cyc;   /* the clock at the first profiled row: setup is before it */
};

#define PHM(k) (1u << PHK_##k)
#define PHM_ALL ((1u << PHK_COUNT) - 1)

struct phase_desc {
    const char *label;
    const char *vanilla;      /* the Java it ports */
    const char *native;       /* the C function it is */
    unsigned reads, writes;   /* PHM masks */
};

extern const struct phase_desc PHASES[PH_COUNT];
extern const char *const PHASE_CLASS_NAMES[PHK_COUNT];

struct session;
struct client_out;
struct chunk;

/* The tracer, one per environment (env.h). */
struct phase_env {
    int on;                   /* PHASE_TRACE: every boundary digests; PHASE_PROF: every boundary is timed */
    int check;                /* and every class at both ends */
    FILE *out;                /* the trace lines; NULL keeps the hash only */
    const struct session *ss; /* the row's session */
    const struct client_out *co; /* the row's client packets */
    int world_dim;            /* the world the per-world phases run in */
    int open;                 /* the phase running, -1 none */
    uint64_t epoch[4];        /* the server worlds' and the client's write epochs at its start */
    uint64_t before[PHK_COUNT];
    const struct chunk **written; /* the digest's list of written chunks */
    size_t written_cap;
    int64_t row;
    uint64_t trace_hash;      /* every line so far */
    long rows, phases;
    long violations;
    int undeclared[PH_COUNT][PHK_COUNT]; /* runs of each phase that wrote a class it does not declare */
    int observed[PH_COUNT][PHK_COUNT];   /* and that wrote each class it declares */
    long first[PH_COUNT][PHK_COUNT];     /* the traced row (0 the first) of each one's first write */
    char first_violation[256];
    char line[16384];
    int linen;
    struct phase_prof prof;
    /* the per-phase kernel harness (test_snapshots --phase-kernel,
     * tests/phasekernel.h): kernel(id, 0) after the tracer's begin of each
     * phase whose bit is set, kernel(id, 1) at its end before the digests;
     * a nonzero return from the begin means the kernel ran the phase (a
     * device did), and the C phase is skipped */
    uint32_t kernel_mask;
    int (*kernel)(int id, int end);
};

/* nonzero: a kernel ran the phase in C's place */
int phase_begin_(int id);
void phase_end_(int id);
void phase_row_begin_(const struct session *ss, int64_t t);
void phase_row_end_(void);

/* Tracing on for the current environment: lines to out (or NULL), check as
 * above. The summary is the lines' hash. */
void phase_trace_start(FILE *out, int check);
void phase_trace_summary(FILE *to);

/* The profiler: on for the current environment (the row storage is mapped
 * here, before the first row); the summary per phase and sub-mark (runs,
 * total, share, per-row p50, p99, max, allocations), the worst rows and what
 * they spent their time in, the environment's state sizes; rows_out (or
 * NULL) gets one line per row. */
void phase_profile_start(void);
/* the process's user-space instructions so far, 0 unless the profiler is on
 * and has a counter (work outside the tick, measured alike) */
uint64_t phase_prof_instructions(void);
void phase_profile_summary(FILE *to, FILE *rows_out);
/* the profiled rows' user instructions so far, summed: the whole rows into
 * *rows_ins and each sub-mark's (outermost entries) into sub_ins[PS_COUNT];
 * returns the rows counted (0 without a counter) */
size_t phase_profile_totals(uint64_t *rows_ins, uint64_t *sub_ins);
void phase_prof_begin_(int id);
void phase_prof_end_(int id);
void phase_prof_row_begin_(int server_rows);
void phase_prof_row_end_(void);
void phase_sub_begin_(int sub);
void phase_sub_end_(int sub);

/* PHASE_RUN(id, call): the phase's function call between its marks. */
#define PHASE_ON() __builtin_expect(nw_env->phase.on, 0)
#define PHASE_RUN(id, call)                         \
    do {                                            \
        if (!PHASE_ON() || !phase_begin_(id)) call; \
        if (PHASE_ON()) phase_end_(id);             \
    } while (0)
#define PHASE_PROF_ON() __builtin_expect(nw_env->phase.on & PHASE_PROF, 0)
/* a sub-mark around call */
#define PHASE_SUB(sub, call)                        \
    do {                                            \
        if (PHASE_PROF_ON()) phase_sub_begin_(sub); \
        call;                                       \
        if (PHASE_PROF_ON()) phase_sub_end_(sub);   \
    } while (0)
#define PHASE_WORLD(dim)                              \
    do {                                              \
        if (PHASE_ON()) nw_env->phase.world_dim = (dim); \
    } while (0)

#endif
