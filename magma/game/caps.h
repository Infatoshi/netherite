/* game/caps.h - THE SINGLE SOURCE OF TRUTH for every allocate-once maximum.
 *
 * magma_game is ALLOCATE-ONCE: after the first frame it performs ZERO heap
 * allocations, ever. All working memory is pre-allocated at init from the fixed
 * maxima below. Every maximum is documented with the MEASURED value it derives
 * from (via the `debug_caps` registry key's instrumentation) and a headroom factor.
 *
 * The defaults are compile-time constants (CR_DEF_*), referenced by the config
 * registry (core/config.def) which owns all eleven cap KEYS. At startup the
 * registry is loaded from an optional `magma.conf` (plus --conf / --set) and
 * these caps are derived from it BEFORE any pool is allocated, so the whole
 * pre-allocation is a pure function of the effective CrCaps computed before the
 * window opens. Every pool size in world_live.c / light.c / populate_mc.c /
 * app/game_main.c derives from cr_caps().
 *
 * Toroidal pools (see world_live.c / light.c / populate_mc.c): a chunk at (cx,cz)
 * maps to a UNIQUE slot in a fixed D x D region pool via
 *     slot = ((cx%D)+D)%D * D + ((cz%D)+D)%D
 * where D is that pool's region diameter. Because the needed region around the
 * player is exactly D x D chunks, each in-region chunk owns a distinct slot; a
 * chunk scrolling out (D away) frees its slot for the incoming chunk at the same
 * modulo, which recycles the slot (re-mesh / re-light / re-decorate). No malloc,
 * realloc, or free in steady state OR while streaming.
 *
 * Diameters (from the max Chebyshev radius each stage must hold resident at once):
 *   mesh : the view region itself           -> D = 2R+1   (mesh output is copied
 *          out per-chunk immediately, so a smaller pool only thrashes, never
 *          corrupts; sized exactly for R with no collisions).
 *   light: the view region + 1-chunk apron   -> D = 2R+3   (mesher reads each kept
 *          chunk's 8 neighbours; all must stay resident during the pass).
 *   owr  : populate base chunks over the lit region (each chunk decorated by base
 *          chunks {cx-1,cx}x{cz-1,cz}) -> D = 2R+4.
 */
#ifndef MAGMA_GAME_CAPS_H
#define MAGMA_GAME_CAPS_H

#ifdef __cplusplus
extern "C" {
#endif

/* ---- compile-time DEFAULTS (the registry's defaults for these keys) ---- */

/* Max streaming view radius (Chebyshev, chunks). DECISION: 8. gm_world_mesh_view
 * clamps the runtime radius to this, so every pool below is sized for it. */
#define CR_DEF_VIEW_RADIUS          8

/* Per-chunk vertex cap (across all 4 layers, packed in one slab). Measured densest
 * single chunk = 55710 verts (seed 19 forest; per-layer S/CM/C/T max 32256/43686/
 * 1536/3210). 76000 == 1.36x of that. A chunk exceeding this asserts (loud cap-too-
 * small beats silent corruption). sizeof(CrVertex)=32 -> 76000*32 = 2.43 MB/slot. */
#define CR_DEF_MAX_VERTS_PER_CHUNK  76000

/* Decoration cells produced by one owr_run populate window. Measured densest = 5444
 * (seed 19). 16384 == ~3x. sizeof(DecCell)=8 -> 128 KB/slot. */
#define CR_DEF_OWR_CELLS_MAX        49152

/* Per-layer concatenated draw-buffer caps (verts summed over all kept chunks in a
 * frame). Measured per-frame peaks S/CM/C/T = 3.00M/1.32M/33K/526K (solid
 * 3,001,488 on the seed-20260710 tape). Rounded up with headroom to 4M/3.25M/256K/1M. Overflow asserts (bounded memcpy).
 * CUTOUT re-measured at 131112 (seed 0, scripted yaw pan into dense forest), so the
 * old 128K cap was under real peaks -> 256K. */
#define CR_DEF_DRAW_SOLID           4000000
#define CR_DEF_DRAW_CUTMIP          3250000
#define CR_DEF_DRAW_CUTOUT          524288
#define CR_DEF_DRAW_TRANS           1048576

/* Screen-space triangle scratch. Measured peak 806120 screen-tris. 2.0M == 2.48x
 * (trimmed from the old 4.0M). sizeof(CrScreenTri)=108 -> 2.0M*108 = 216 MB. */
#define CR_DEF_MAX_TRIS             2000000

/* Live mobs (none wired into the streaming world yet; reserved). */
#define CR_DEF_MAX_MOBS             256
/* Entity vertex scratch (one raster pass of all visible mob models). */
#define CR_DEF_ENT_MAX_VERTS        (64 * 1024)

/* ---- effective, runtime-resolved caps ---- */
typedef struct {
    int view_radius;          /* R */
    int max_verts_per_chunk;  /* per mesh slot slab */
    int owr_cells_max;        /* per owr slot */
    int draw_max[4];          /* SOLID, CUTOUT_MIPPED, CUTOUT, TRANSLUCENT */
    int max_tris;
    int max_mobs;
    int ent_max_verts;
    int owr_D_min;            /* floor for owr_D (offline tools sweeping big regions) */

    /* derived toroidal pool geometry (computed from view_radius) */
    int mesh_D,  mesh_slots;   /* D = 2R+1 */
    int light_D, light_slots;  /* D = 2R+3 */
    int owr_D,   owr_slots;    /* D = 2R+4 */
} CrCaps;

/* Singleton effective caps, derived from the config registry (core/config.h).
 * On first use it lazily loads "magma.conf" from the cwd (defaults if absent),
 * and it re-derives itself whenever the registry changes. Never NULL. */
const CrCaps *cr_caps(void);

/* Explicitly (re)load the registry from `path` (NULL -> "magma.conf") and
 * re-derive. Must be called BEFORE any pool is allocated (i.e. before
 * gm_world_create) for overrides to take effect. Missing/partial file ->
 * defaults for the unset keys; an unknown key in the file is fatal.
 * NB: this RESETS every registry key, so a binary that also takes --set must
 * load first and set second (app/game_main.c does exactly that and never calls
 * cr_caps_load itself). */
void cr_caps_load(const char *path);

/* Programmatic single-key override (a registry key) + derived recompute.
 * Must run BEFORE any pool is allocated. For offline tools (world_dump).
 * An unknown key or unparseable value is fatal (exit 2). */
void cr_caps_override(const char *key, long value);

#ifdef __cplusplus
}
#endif
#endif /* MAGMA_GAME_CAPS_H */
