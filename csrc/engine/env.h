/* One environment: the state the tick mutates that no world or server replay
 * carries, in one struct, so that several environments live in one process
 * and tick in any interleaving (the GPU steps many side by side).
 *
 * nw_env names the current environment. It starts at the process's first one
 * (env.c), which every single-world tool uses without knowing it; a harness
 * that runs several makes each with env_new and points nw_env at the one it
 * is about to tick (tests/test_interleave.c). The pointer is thread-local, so
 * one thread per environment works too.
 *
 * What lives here, by kind: the modules' own state (a pending tick set, a
 * callback a phase installs, a counter), the scratch a function needs across
 * calls or too large for the stack, and the run configuration (the harness
 * switches). A world's state lives in its struct world, a dimension's in the
 * replay's per-dimension array (serverreplay.h), and tables that are the same
 * for every environment are built once before main and only read after
 * (csrc/tests/globals.sh lists every writable global left and why).
 *
 * A file reads its fields under the names its globals had: a file static
 * became a #define to its field where it was declared (ticks.c and a few
 * others use the struct directly), and the names several files share are
 * defined at the end of this header. */
#ifndef NETHERITE_ENV_H
#define NETHERITE_ENV_H

#include <stdint.h>
#include <stdio.h>

#include "ai.h"
#include "explosion.h"
#include "activate.h"
#include "arena.h"
#include "blockcb.h"
#include "blockwl.h"
#include "clientworld.h"
#include "collide.h"
#include "det.h"
#include "dig.h"
#include "envstack.h"
#include "fallhang.h"
#include "grave.h"
#include "features_nether.h"
#include "item_entity.h"
#include "itemtag.h"
#include "jrand.h"
#include "layers.h"
#include "living.h"
#include "nbtbin.h"
#include "nbtjson.h"
#include "nether.h"
#include "pick.h"
#include "phase.h"
#include "place.h"
#include "populate.h"
#include "populate_nether.h"
#include "randomtick.h"
#include "spawning.h"
#include "regionspill.h"
#include "spill.h"
#include "stronghold.h"
#include "structure.h"
#include "survival.h"
#include "terrain.h"
#include "ticks.h"
#include "tileticks.h"
#include "image.h"

struct gh_world;
struct ie_world;
struct ie_ent;
struct an_ent;
struct sc_ent;
struct serverreplay;
struct snapshot;
struct tnt_blast;

/* The harness switches: set by a test before its run, read by the tick.
 * Each negative check makes a recording diverge on purpose, naming the row. */
struct run_config {
    int sr_negative;                    /* serverreplay.h */
    int populate_negative_snow;         /* populate.h */
    int populate_negative_maporder;
    int populate_negative_village;
    int decorator_negative_count;
    int decorator_negative_muttree;
    int populate_hell_negative_anystart; /* populate_nether.h */
    int populate_negative_spike;
    int seedworld_negative_fuzz_order;  /* seedworld.h */
    int seedworld_negative_early_pop;
    int seedworld_negative_dim_block;
    int ghast_negative_attack;          /* ghasts.h */
    int ghast_negative_waypoint;
    int dragon_negative_draw;           /* dragon.h */
    int explosion_skip_attenuation;     /* explosion.h */
    int explosion_debug;
    int portal_search_radius_override;  /* portal.h */
    int portal_site_draw_order_override;
    int raytrace_negative_stairs_nearest; /* raytrace.h */
    int player_no_sprint_boost;         /* player.h */
    int player_no_ground_friction;
    /* GameSettings.particleSetting + 1, the tape's options.particles; 0 is
     * the pinned profile's minimal (2) (env_particle_setting) */
    int particle_setting;
};

/* The environment's random streams (GPU plan L12): the Randoms the game
 * keeps from one row to the next outside an entity, in one place, so a
 * kernel reads and writes them at one offset of the environment. An
 * entity's own Random stays in its pool slot; a provider's Random that
 * every use reseeds first (ChunkProviderGenerate.rand, hellRNG, endRNG,
 * MapGenBase.rand) is scratch and stays with its provider. */
#define RNG_WORLDS 4
struct rng_table {
    /* World.rand and World.updateLCG, by slot: a replay's worlds at
     * sr_dim_index (0 the overworld, 1 the Nether, 2 the End), a tool's
     * single world at 0 (servertick's rng_slot) */
    struct { jrand rand; int32_t update_lcg; } world[RNG_WORLDS];
    /* the whole-server replay's Det: the seeders, Math streams, entity-ID
     * counters and split streams (SR_DET) */
    det_state det;
    /* Collections.shuffle's shared Random, det.nbt's "shuf" (SR_SHUF) */
    jrand shuffle;
    /* each WorldServer's worldTeleporter.random, by the world's
     * sr_dim_index (portal.h TP_RAND) */
    jrand teleport[3];
    /* the static Randoms of vanilla's block singletons (spill.h, block_rand)
     * and BlockFire's Random(1) that a scheduled fire tick draws from once
     * it has no world stream (blockcb.c) */
    det_rng block[BR_N];
    jrand fire;
    int fire_ready;
};

/* What a function fills and reads within one call, too large for the
 * stack: nothing in it lives from one call to the next (between rows it is
 * empty: the stack's top 0, the depths 0), and its memos are kept under
 * stamps of their own, so any environment's call may use any scratch. A
 * pool's worker keeps one of its own for every environment it steps
 * (nw_scratch): one warm copy a worker instead of a cold one an
 * environment (lane/coldaudit). */
struct env_scratch {
    uint16_t chunkgen_ids[CHUNK_CELLS];
    /* World.lightUpdateBlockList (world.c lp_pass and lt_flood): the light
     * pass's queue, filled and drained within one call */
    int32_t light_list[32768];
    /* world.c lp_pass: the queue turns of cells of chunks the world
     * lacks, within one pass while no light is written, under memo_stamp */
    uint64_t lp_memo[1 << 14];
    uint32_t memo_stamp;
    /* the lt_stamp values of the last flood (lt_flood) */
    uint32_t flood_seq;
    /* world.c lt_flood: each cell's last evaluation and last write in
     * the flood (flood_seq values), the 37-cube around its origin */
    uint64_t lt_stamp[37 * 37 * 37];
    /* pathfind.c vertical_offset: the columns one search has tested */
    struct { int32_t x, y, z; uint32_t stamp, map_ver; int32_t v; const void *w; } pf_vmemo[4096];
    uint32_t pf_vstamp;   /* the search pf_vmemo's entries belong to */
    uint8_t chunkgen_metas[CHUNK_CELLS];
    int chunkgen_biomes[256];
    uint16_t dim_blocks[DIM_CELLS];
    double nether_field[425], nether_slowsand[256], nether_gravel[256], nether_exclusivity[256];
    double end_field[297];
    struct pick_entry pick_pe[PICK_MAX_CANDIDATES(CW_MAX_ENTITIES)];
    struct pick_cand pick_pc[PICK_MAX_CANDIDATES(CW_MAX_ENTITIES)], pick_list[PICK_MAX_CANDIDATES(CW_MAX_ENTITIES)];
    int pick_order[PICK_MAX_CANDIDATES(CW_MAX_ENTITIES)];
    fh_world pickobj_fw;
    struct an_ent *fp_found[AN_MAX_ENTITIES];
    /* collide.h: the collision lists a move takes (collide_scratch_begin) */
    struct collide_list collide[COLLIDE_SCRATCH_DEPTH];
    int collide_depth;
    /* activate.c's live paths' act_env (act_scratch_begin) */
    act_env act[ACT_SCRATCH_DEPTH];
    int act_depth;
    /* explosion.c: one frame per explosion in progress (a crystal's
     * blast inside another's) */
    struct expl_frame expl[EXPL_SCRATCH_DEPTH];
    int expl_depth;
    /* a chunk unload's entity lists (serverreplay.c sr_cl_event) */
    struct ie_ent *unload_ies[IE_MAX_ENTITIES + 1], *unload_mob_items[IE_MAX_ENTITIES + 1];
    fh_ent *unload_fhs[FH_MAX_ENTITIES + 1];
    struct living endfight_living;
    struct s2c_pkt s2c_discard;   /* s2c_add past a full queue */
    _Alignas(8) unsigned char s2c_side_discard[S2C_SIDE_MAX_PKT];   /* its out-of-line part */
    /* envstack.h: the big locals, last in first out */
    size_t stack_top;
    _Alignas(64) unsigned char stack[ENVSTACK_CAP];
};

struct env {
    struct arena_env arena;     /* first: nw_arena() (arena.h) reads it as the env */
    /* an image's own layout, heap and directory (image.h); zero for the
     * process's first environment, which is not an image */
    struct env_image img;
    struct run_config cfg;
    struct rng_table rng;
    struct ticks_env ticks;

    /* The values a phase installs for the code it calls (a world's Random,
     * a Det stream, a callback and its context), by the file that reads
     * them. Each is set before use by the environment that runs. */
    struct { struct ie_world *live_iew; } activate;
    /* end_portal_kept: BlockEndPortal.field_149948_a, set while the dragon
     * builds its exit portal (the End keeps those portal blocks) */
    struct { struct blockcb_env env; int end_portal_kept; } blockcb;
    /* the block callbacks' worklist: the frames of the calls in progress */
    struct bwl bwl;
    struct { int raining; uint64_t chunk_stamp; } entity;
    /* raw_push: the running hit's field_77288_k vector (var35, before
     * Blast Protection); player_gap: what the armour took off the player
     * twin's push, for the living pass's S27 */
    struct {
        const struct ie_ent *exclude_ie;
        int placed_by_player;
        uint64_t placer;          /* the blast's getExplosivePlacedBy when a mob (living.h lref) */
        double raw_push[3], player_gap[3];
        /* WorldServer.newExplosion's S27 fan-out, after doExplosionB(false):
         * the replay's watcher (NULL elsewhere) takes each finished explosion
         * with its affected count (0 for a non-smoking one: the list is
         * cleared before the packet) */
        void (*on_done)(void *ctx, struct world *w, double x, double y, double z, float size, int naffected,
                        const struct s27_xp *xp, int nxp);
        void *on_done_ctx;
        /* doExplosionB's blocks whose drop draws on the client too (the
         * S27's list as the client world has it: the blocks before the
         * blast) */
        struct s27_xp client_xp[S27_XP_MAX];
        /* the client's doExplosionB clears the S27's listed cells; its world
         * runs no neighbour change, so a write elsewhere that the clears set
         * off waits for the flush */
        const int *listed;   /* doExplosionB's affected cells (x, y, z) while its loop runs, else NULL */
        int nlisted;
    } explosion;
    struct { int fall_instantly; struct fh_env env; } fallhang;
    struct { int tree_notify; } features;
    struct { struct spike_crystal crystal_last; } features_nether;
    struct { int bigtree_height_limit, bigtree_scaled; } features_trees;
    struct {
        int raining, difficulty;
        void (*tnt_prime)(void *ctx, int x, int y, int z);
        void *tnt_prime_ctx;
    } fire;
    struct { int64_t seed; } fortress;
    struct { struct gh_world *pass_world; } ghasts;
    struct { int32_t pending_rider_ent; } hostiles_spider;   /* an_ref */
    struct { det_state *live_det; int live_role; } itemuse;
    struct { struct serverreplay *sr; } leash;
    struct { jrand *walking_world; } living;
    /* World.destroyBlockInWorldPartially's calls this row (a zombie's door
     * crack, EntityAIBreakDoor), what WorldManager sends the players as S25:
     * the breaker's id, the block and the stage (-1 the end); the playable
     * client draws them (session.c empties it each row) */
    struct { int n; struct { int id, x, y, z, stage; } e[16]; } block_break;
    struct {
        jrand case_rand;
        det_state *case_det;
        det_rng *case_math;
        det_split *case_item;
        int case_role;      /* the Det role the case's constructors draw on */
        place_entity_hook entity_hook;
        void *entity_hook_ctx;
    } place;
    struct {
        jrand *world_rand;
        det_state *det;
        void (*on_ent)(void *ctx, const struct sc_ent *e);
        void *on_ent_ctx;
    } populate;
    struct { struct end_dragon_spawn dragon_last; } populate_nether;
    struct { struct randomtick_env env; int skylight; } randomtick;
    struct { int neg_early_armed; } seedworld;
    /* the region cache (regioncache.h, regioncache_flag): mode 0 on, 1 off,
     * 2 verify; dir NULL for the default under $HOME */
    struct { int mode; const char *dir; } regioncache;
    struct { struct serverreplay *end_ctx, *pick_ctx; int fh_drop_unordered; } serverreplay;
    struct { const struct snapshot *snap; uint64_t stamp_base; struct serverreplay *sr; int player_id; } snap_runtime;
    struct { void (*dispenser_random)(void *ctx); void *dispenser_random_ctx; } tileentity;
    /* World.playAuxSFX on a server world (the null-player form): the replay's
     * S28 to its player (serverreplay.c), unset elsewhere */
    struct { void (*fn)(void *ctx, struct world *w, int type, int x, int y, int z, int data); void *ctx; } aux_sfx;
    /* WorldServer.addBlockEvent's S24 to the players near it (the replay's) */
    struct { void (*fn)(void *ctx, struct world *w, int x, int y, int z, int block, int event, int param); void *ctx; } block_event;
    struct {
        int64_t world_time, total_time;
        int skylight_subtracted;
        const float *cos_table;
        struct tt_env env;
        jrand *world_rand;
        int furnace_swapping;
    } tileticks;
    struct { struct tnt_blast *cur_blast; } tnt;
    struct { struct serverreplay *sr; } throw;
    struct { FILE *out; } trace;
    /* the light engine (world.c): a capture of every call (lightcap.h), off
     * unless a harness sets it; the last call's queue length and whether it
     * dropped an entry at the 32,768 cap; the client world's calls deferred
     * (lightdefer.h), off unless a harness sets it */
    struct { struct lightcap *cap; int32_t tail; uint8_t overflow; struct lightdefer *defer; } light;
    /* the phase tracer (phase.h): off unless a harness turns it on */
    struct phase_env phase;
    /* the dead livings this environment has buried (grave.h) */
    struct grave_env grave;
    /* EntityLiving.senses of the living whose updateAITasks runs: Java
     * clears the cache at that method's head (clearSensingCache) and only
     * the AI tasks it then runs read it, so one cache per environment,
     * owned by the living being updated, holds every read Java's would */
    struct {
        lref owner;
        struct senses s;
    } senses;
    /* the attribute modifier names past living.h's fixed ones, in the order
     * the environment first met them (an id's only meaning is its name) */
    struct {
        int n;
        char s[ATTR_NAMES_ENV][ATTR_NAME_LEN];
    } attr_names;

    /* survival.c: the survival module's world view, the client's particles,
     * mouseover and dig controller, the server's dig machine, the S37 ring */
    struct {
        struct surv_world w;
        struct particles_live fx;
        struct surv_mouseover mo;
        struct surv_dig_state dig;
        dig_env digenv;
        int digenv_ready, dig_active, dig_ticked;
        int64_t dig_fork;
        struct surv_s37 s37_ring[SURV_S37_RING];
        struct s2c_ring s2c;            /* the row's three packet queues */
        unsigned s37_next;
        int attacker;
        uint64_t attacker_ref;          /* the attacker's living (living.h lref), 0 unknown */
        int attacker_msg_set, attacker_dmg, attacker_name;   /* surv_set_attacker_msg's */
    } survival;

    /* game state vanilla keeps on shared objects (a block singleton's bounds,
     * a biome's tree generator, a static Random) or a structure walk keeps */
    struct { struct nav_scratch nav_shared; int nav_shared_busy; } ai;
    struct { struct { int x, y, z; } wire_changed[256]; int wire_nchanged; } blockcb_wire;
    /* item_bounds: the block's shared bounds were last left at its
     * setBlockBoundsForItemRender by addCollisionBoxesToList (the portal
     * frame's 13/16), or for the End portal at the 1/16 a ray trace's
     * setBlockBoundsBasedOnState set; brewing_base: the brewing stand's base;
     * pane_bounds: iron bars, the glass pane and the stained pane (101, 102,
     * 160) as their last setBlockBounds left them, once pane_set */
    struct { float fence_bounds[2][6]; float chest_bounds[2][6]; float ladder_bounds[6]; unsigned char stairs_octant[4096]; unsigned char brewing_base; unsigned char item_bounds[4096]; float pane_bounds[3][6]; unsigned char pane_set[3]; } collide;
    /* pkt_seq: the order the trackers' packets left the server in, across
     * the living and the object trackers (the client applies them so) */
    struct { int live_ids[AN_MAX_ENTITIES]; uint64_t pkt_seq; } combat;
    struct { int bigtree_state[256]; } decorator;
    /* BlockLeaves.field_150128_a, one per leaf block object (Blocks.leaves
     * 18, Blocks.leaves2 161): its centre cell, the only cell a later
     * updateTick reads without rewriting it first (randomtick.c rt_leaves) */
    struct { int32_t decay_centre[2]; } leaves;
    struct { int side_sources; int flow_dir[2][4]; int cost_array[2][4]; } features_springs;
    struct { struct aabb pbb; } ghasts_blast;
    struct { struct pop_map hmap; } populate_hell;
    /* MapGenStronghold's ring and biome box caches and the piece walk's
     * state (stronghold.c) */
    struct {
        struct sh_pos coords[SH_COUNT];
        int coords_ready;
        struct layers layers;
        int layers_ready;
        int64_t layers_seed;
        int *box;
        int box_x, box_z, box_w, box_h, box_valid;
        int spawned[SH_WEIGHT_N];
        int total_weight;
        int active[SH_WEIGHT_N];
        int active_n;
        int strong_component, current_weight;
        struct bbox start_bb;
        int portal_found;
        struct piece **pending;
        int pending_n, pending_cap;
    } stronghold;
    struct { int64_t seed; struct layers layers; } temple;
    struct { int64_t seed; struct layers layers; int in_desert; } village;
    struct { uint8_t *pack_scratch; } world;
    /* the region stores' spill file (regionspill.h) */
    struct region_spill rspill;

    /* the trees' memory (nbtjson.c): the size-class free lists and the
     * scratch region of a scratch build */
    struct {
        uint64_t *pool_free_list[NBT_POOL_CLASSES];
        unsigned char *pool_slab;
        size_t pool_left;
        int scratch_on, scratch_outside;
        unsigned char **scratch_slabs;
        int scratch_nslabs, scratch_cur;
        size_t scratch_used, scratch_dirty;
        void **scratch_big;
        int scratch_nbig, scratch_capbig;
    } nbtjson;
    /* the entity digest's NBT hashing (nbtbin.c): its buffer and memo, and
     * nbtw.h's binary output's field records and the bytes of a compound
     * being put in order */
    struct {
        nbtbin long_buf, memo_scratch;
        struct nbtbin_memo_slot memo[NBTBIN_MEMO_SLOTS];
        struct nbtw_field *wf;
        int wfcap;
        uint8_t *wtmp;
        size_t wtmpcap;
        struct nbtw_order *worder;   /* NBTW_ORDER_SLOTS, made on first use */
        struct nbtw_field *wc;       /* the closed compounds' field records */
        int wccap;
        struct nbtw_closed *wd;      /* the closed compounds */
        int wdcap;
    } nbtbin;
    /* the item tag store (itemtag.h): every stack tag the environment interned */
    struct {
        struct itag entries[ITAG_CAP];
        int nentries;
        int heads[2 * ITAG_CAP];
        char arena[ITAG_TEXT_CAP];
        uint32_t arena_used;
        /* while a region-cache capture runs (regioncache.h): every index a
         * lookup or intern returned, in call order */
        int *journal;
        int njournal, capjournal, journal_on;
    } itemtag;

    /* scratch: what a function fills and reads within one call, too large
     * for the stack (struct env_scratch, above): the device build's (each
     * thread block's environment), and the host's where the thread has no
     * scratch of its own (nw_scratch) */
    struct env_scratch scratch;
};

/* The names several files share for the fields above. */
#define block_rand (nw_env->rng.block)
#define surv (nw_env->survival.w)
#define surv_fx (nw_env->survival.fx)

/* GameSettings.particleSetting of the run: 0 all, 1 decreased, 2 minimal */
static inline int env_particle_setting(void)
{
    return nw_env->cfg.particle_setting ? nw_env->cfg.particle_setting - 1 : 2;
}
#define surv_mo (nw_env->survival.mo)
#define entity_chunk_stamp (nw_env->entity.chunk_stamp)
#define expl_exclude_ie (nw_env->explosion.exclude_ie)
#define expl_placed_by_player (nw_env->explosion.placed_by_player)
#define expl_placer (nw_env->explosion.placer)
#define spike_crystal_last (nw_env->features_nether.crystal_last)
#define fire_tnt_prime (nw_env->fire.tnt_prime)
#define fire_tnt_prime_ctx (nw_env->fire.tnt_prime_ctx)
#define populate_world_rand (nw_env->populate.world_rand)
#define populate_det (nw_env->populate.det)
#define populate_on_ent (nw_env->populate.on_ent)
#define populate_on_ent_ctx (nw_env->populate.on_ent_ctx)
#define end_dragon_last (nw_env->populate_nether.dragon_last)
#define te_dispenser_random (nw_env->tileentity.dispenser_random)
#define te_dispenser_random_ctx (nw_env->tileentity.dispenser_random_ctx)

/* local-exec: every program links the environment into its executable, so
 * the pointer is one %fs-relative load (NW_ENV_TLS_MODEL: a library
 * loaded into such a program, csrc/runtime's play view, takes
 * initial-exec) */
#if !defined(__NVPTX__)
#ifndef NW_ENV_TLS_MODEL
#define NW_ENV_TLS_MODEL "local-exec"
#endif
extern _Thread_local struct env *nw_env __attribute__((tls_model(NW_ENV_TLS_MODEL)));
/* the thread's own scratch (env_scratch_new), NULL for none */
extern _Thread_local struct env_scratch *nw_thread_scratch __attribute__((tls_model(NW_ENV_TLS_MODEL)));
#endif

/* The scratch a call uses: the thread's own when it has one (a pool's
 * worker), else the current environment's. The device build has no thread
 * scratch: each thread block uses its environment's. */
#if defined(__NVPTX__)
#define nw_scratch (&nw_env->scratch)
#else
static inline struct env_scratch *nw_scratch_get(void)
{
    struct env_scratch *s = nw_thread_scratch;
    return __builtin_expect(s != NULL, 1) ? s : &nw_env->scratch;
}
#define nw_scratch (nw_scratch_get())
#endif

/* A thread's own scratch, zeroed (pages committed as written), and its
 * release: set nw_thread_scratch to it before the thread's first call and
 * to NULL before freeing it. */
struct env_scratch *env_scratch_new(void);
void env_scratch_free(struct env_scratch *s);

/* World.playAuxSFX(type, x, y, z, data) on server world w: WorldManager's
 * S28 to the players within 64 blocks (the replay's hook) */
static inline void env_aux_sfx(struct world *w, int type, int x, int y, int z, int data)
{
    if (nw_env->aux_sfx.fn) nw_env->aux_sfx.fn(nw_env->aux_sfx.ctx, w, type, x, y, z, data);
}

/* WorldServer.addBlockEvent on server world w: func_147488_Z's S24 to the
 * players within 64 blocks (the replay's hook) */
static inline void env_block_event(struct world *w, int x, int y, int z, int block, int event, int param)
{
    if (nw_env->block_event.fn) nw_env->block_event.fn(nw_env->block_event.ctx, w, x, y, z, block, event, param);
}

/* A fresh environment, ready to tick; env_free releases what it owns. */
struct env *env_new(void);
void env_free(struct env *e);

#endif
