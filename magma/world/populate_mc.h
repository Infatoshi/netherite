/* world/populate_mc.h - decoration overlay for the magma world (VIEW-DISTANCE).
 *
 * blaze's cp_provide_chunk (used by world/light.c) is base terrain ONLY: it
 * omits populate()/decoration, so the raw chunks have no trees, foliage, or
 * plants. This module runs blaze's faithful, position-parametrized populate
 * (`owr_run` from core/overworld_region.h, verified byte-identical java==cpu==cuda)
 * and overlays the resulting PB_* decoration onto ANY chunk the renderer needs -
 * not just the 2x2 origin block.
 *
 * WHY per-window: MC populate for base chunk (bcx,bcz) writes its decoration into
 * a 2x2-chunk window whose FEATURES land at world [bcx*16+8, bcx*16+24) offset +8.
 * So a given chunk C=(cx,cz) receives decoration from the FOUR populate passes at
 * base chunks (bcx,bcz) in {cx-1,cx} x {cz-1,cz}. This module runs owr_run ONCE per
 * base chunk (cached), extracts the DECORATION cells (cells that differ from that
 * window's OWN st_run base terrain - so base-terrain deltas between st_run and the
 * cp_provide_chunk terrain light.c uses are never overlaid, only true features),
 * and applies the four contributing windows' decoration to chunk C. Deterministic
 * and seed-correct: owr_run seeds the per-chunk RNG from the REAL (bcx,bcz).
 *
 * The block ids overlaid are blaze PB_* codes (0..20 == CB_*, 21+ are feature
 * blocks), which the magma block-model table (assets/blockmodels.c) understands.
 */
#ifndef MAGMA_WORLD_POPULATE_MC_H
#define MAGMA_WORLD_POPULATE_MC_H
#ifdef __cplusplus
extern "C" {
#endif

/* Overlay MC populate() decoration onto chunk (cx,cz) for `seed`. `chunk_block` is
 * the chunk's 16x256x16 block store in light.c's CB_INDEX layout
 * (idx = (lx<<12)|(lz<<8)|y, lx/lz in [0,16), y in [0,256)); decoration cells are
 * written in place over the base terrain already present there. Idempotent and
 * cached: each base-chunk populate window is computed via owr_run at most once for
 * the process lifetime and reused by all four chunks it decorates. */
void popmc_decorate_chunk(long long seed, int cx, int cz, unsigned short *chunk_block);

/* Same overlay, also writing legacy block metadata for generated tiles whose
 * state is not encoded in PB_* (currently dungeon chest facing). `chunk_meta`
 * uses the same y-fastest 65536-cell layout as chunk_block. */
void popmc_decorate_chunk_meta(long long seed, int cx, int cz,
                               unsigned short *chunk_block,
                               unsigned char *chunk_meta);

/* Enable the optional village stage before any chunks are built. Toggling the
 * mode invalidates cached populate windows; leaving it off adds no live-tick work. */
void popmc_set_villages(int enabled);

enum {
    POPMC_DUNGEON_MOB_SKELETON = 1,
    POPMC_DUNGEON_MOB_ZOMBIE = 2,
    POPMC_DUNGEON_MOB_SPIDER = 3
};

/* Exact tile metadata captured from WorldGenDungeons' live placement stream.
 * Lookups are fixed-pool scans and do not generate chunks or allocate. */
int popmc_dungeon_chest_info(long long seed, int x, int y, int z,
                             long long *loot_seed, int *facing_meta);
int popmc_desert_chest_info(long long seed, int x, int y, int z,
                            long long *loot_seed, int *facing_meta);
int popmc_jungle_chest_info(long long seed, int x, int y, int z,
                            long long *loot_seed, int *facing_meta);
int popmc_village_chest_info(long long seed, int x, int y, int z,
                             long long *loot_seed, int *facing_meta);
typedef struct {
    int x, y, z;
    int profession;
    int zombie_infested;
} PopmcVillageResident;
/* Copy the unique generated village residents inside the inclusive X/Z bounds.
 * Returns the number copied (at most `capacity`); cached metadata only, so this
 * does not generate chunks or allocate. */
int popmc_village_residents(long long seed,
                            int min_x, int min_z, int max_x, int max_z,
                            PopmcVillageResident *out, int capacity);
int popmc_jungle_dispenser_info(long long seed, int x, int y, int z,
                                long long *loot_seed, int *facing_meta);
int popmc_swamp_witch_info(long long seed, int x, int y, int z);
int popmc_swamp_pot_info(long long seed, int x, int y, int z,
                         int *item, int *meta);
int popmc_dungeon_spawner_info(long long seed, int x, int y, int z,
                               int *mob_kind);

/* Exact deferred structure data produced by MapGenMineshaft placement. A cart
 * site is an EntityMinecartChest using the abandoned-mineshaft loot table;
 * its inventory remains unmaterialized until first access, as in vanilla. */
int popmc_mineshaft_cart_info(long long seed, int x, int y, int z,
                              long long *loot_seed);
int popmc_mineshaft_spawner_info(long long seed, int x, int y, int z);

/* CUMULATIVE region prepare: build populate windows for `nbases` base chunks (flat
 * [x0,z0, x1,z1, ...] pairs) in the given order, which MUST be vanilla populate
 * order (spawn-square raster, then later player-loaded chunks). Each window seeds
 * its world with the decoration cells of the four populate-order-earlier windows
 * already in the pool, reproducing vanilla's shared-world cascade (trees, lakes and
 * dungeons READ earlier windows' output). The toroidal pool must span the whole
 * region (owr_d_min via cr_caps_override / magma.conf) or windows get evicted and
 * rebuilt without their donors. Offline verifier path (trace/world_dump). */
void popmc_prepare(long long seed, const int *bases_xz, int nbases);

/* Worldgen RNG-cursor probe hook (see mc_rng.h MC_PROBE): fn(cx, cz, tag, type,
 * cursor) is called at every vanilla Forge terrain-gen event boundary during window
 * populate. NULL disables. Offline flywheel tools only. */
void popmc_set_probe(void (*fn)(int, int, const char *, const char *,
                                unsigned long long));

/* How many times owr_run has actually executed (a new populate window was built).
 * Steady-state play must NOT keep incrementing this - windows are compute-once. */
long popmc_window_builds(void);

/* Population-cascade RNG-clobber events (see mc_rng.h watch + DEVLOG): while base
 * chunk (bcx,bcz) populates, when its rand state reaches `before`, jump to `after`.
 * Mined from the live game's GEN log lines by trace/world_verify.py. The array must
 * outlive all window builds. NULL/0 disables. Offline verifier path only. */
#define POPMC_CASCADE_NONE (-9999999)
typedef struct {
    int bcx, bcz;                    /* parent chunk whose populate is interrupted */
    unsigned long long before, after; /* rand state at interrupt / at resume */
    int ncx, ncz;                    /* cascade-populated chunk, or POPMC_CASCADE_NONE */
} PopmcCascadeEvt;
void popmc_set_cascade(const PopmcCascadeEvt *evts, int n);

/* Light opacity / emission for a decorated (PB_*) block id, faithful to blaze. */
int  popmc_opacity(int id);
int  popmc_emission(int id);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_WORLD_POPULATE_MC_H */
