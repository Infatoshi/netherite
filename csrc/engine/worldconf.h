/* config.yaml, the world's one config (the repo root's, or configs/NAME.yaml):
 * the C reader of the file oracle/harness/netherite/oracle/WorldConf.java reads.
 * The grammar is config.yaml's header (a strict YAML subset: sections at
 * column 0, entries two spaces in, plain scalars, # comments; or a run file's
 * two parts, sim and policy, at column 0 with their sections two spaces in
 * and entries four: the registry is sim's, policy's text is the trainer's,
 * checked for its grammar and skipped) and the keys
 * are WorldConf.KEYS, the same table below: an unknown key, a value its kind
 * does not take, a key twice or a line outside the grammar is refused with
 * the file and line. A key the file leaves out keeps config.yaml's value
 * (wconf_defaults). The native keys (WC_NATIVE0 on: the fidelity section,
 * fidelity.h; engine.render and engine.tick, where the renderer and the
 * tick run; the performance section, the pipeline's tuning, pipeline.h)
 * are the runtime's alone: config.yaml leaves them out (their defaults
 * are vanilla and the pipeline's own choices) and the oracle refuses a
 * file that sets one.
 *
 * The engine itself reads no config: it replays what the oracle recorded
 * (the tape header's world and options, the snapshot). The runtime reads one
 * (csrc/runtime: pool_config.config, pipe_bench and pipe_gate --config,
 * pool_gate --world) and holds every start to it (wconf_match_start).
 * No allocation, no globals but the read-only registry; builds on macOS. */
#ifndef NETHERITE_WORLDCONF_H
#define NETHERITE_WORLDCONF_H

#include <stddef.h>

struct jval;

enum {
    WC_SEED, WC_DIFFICULTY, WC_RENDER_DISTANCE, WC_DAYLIGHT_CYCLE, WC_KEEP_INVENTORY, WC_VILLAGES,
    WC_UNSUPPORTED0,                        /* WorldConf.UNSUPPORTED's 17, in its order */
    WC_GRAPHICS = WC_UNSUPPORTED0 + 17, WC_PARTICLES, WC_OBSERVATION, WC_CHANNELS, WC_TICKS_PER_STEP,
    WC_ACTION_REPEAT, WC_HUD, WC_GAMMA,
    WC_CAMERA, WC_SEMANTIC, WC_SEMANTIC_RANGE,
    WC_DEVICE_MESH, WC_DEVICE_GENERATION, WC_DEVICE_LIGHT,
    WC_KIND, WC_START, WC_WORLDS, WC_GROUPS, WC_CARDS, WC_EPISODE_SECONDS,
    WC_CLICK_INTERVAL, WC_MASK_CLOSE_GRID, WC_MASK_NOOP_CLICKS, WC_MOVE_STACK, WC_ITEM_SELECT, WC_RECIPES,
    WC_HOLD_ATTACK, WC_LOOK_TARGET, WC_LOOK_TICKS,
    /* the native keys (fidelity.h): the runtime's alone, so neither config.yaml nor the
     * oracle's WorldConf.KEYS has them, and the oracle refuses a file that sets one */
    WC_NATIVE0,
    WC_FID_SPAWNING = WC_NATIVE0, WC_FID_SPAWN_TYPES, WC_FID_MOBS, WC_FID_RANDOM_TICKS, WC_FID_WEATHER,
    WC_FID_SKIP_AMBIENT, WC_FID_DISTANT_RADIUS,
    WC_FID_RUNG,
    WC_RENDER, WC_TICK, WC_TICK_SCOPE, WC_RAYCAST, WC_RAYCAST_HOST_GROUPS, WC_CAST_PRECISION, WC_CAST_RADIUS,
    WC_WORKERS, WC_STREAMS, WC_GROUP, WC_BATCH, WC_DEPTH, WC_LAYOUT, WC_PIN, WC_PLACE, WC_TUNE, WC_REGION_SPILL,
    WC_NKEYS
};

#define WCONF_VAL 160

struct wconf {
    char val[WC_NKEYS][WCONF_VAL];
    char path[512];             /* the file read ("" none) */
};

/* The registry: section.key, config.yaml's value, the kind ("onoff", "long",
 * "int:LO:HI", "size", "name", "path", "cards" (CUDA ordinals, a comma
 * list), "enum:A|B|..."). */
struct wconf_key {
    const char *key, *def, *kind;
};
extern const struct wconf_key WCONF_KEYS[WC_NKEYS];

void wconf_defaults(struct wconf *c);
/* defaults, then the file; 0, or -1 with err */
int wconf_load(struct wconf *c, const char *path, char *err, size_t n);
/* text over the values in c (name for the messages); 0 or -1 */
int wconf_parse(struct wconf *c, const char *text, const char *name, char *err, size_t n);
/* one "key=value": [sim.]section.key, or a bare world key; 0 or -1 */
int wconf_set(struct wconf *c, const char *kv, char *err, size_t n);

/* The first sim.actions key at a value the runtime does not implement yet
 * (none since lanes guiact and moveact landed: click_interval .. recipes
 * are csrc/runtime/rlact.h's, hold_attack .. look_ticks moveact.h's): 0,
 * or -1 with "sim.actions.KEY: not implemented yet" in err */
int wconf_unimplemented(const struct wconf *c, char *err, size_t n);

/* the registry index of section.key, or -1 */
int wconf_index(const char *key);
const char *wconf_get(const struct wconf *c, int k);
int wconf_int(const struct wconf *c, int k);
int wconf_on(const struct wconf *c, int k);
/* world.difficulty as EnumDifficulty's id, 0 peaceful to 3 hard */
int wconf_difficulty(const struct wconf *c);
/* client.observation's W and H */
void wconf_size(const struct wconf *c, int *w, int *h);
/* client.channels: the observation's bytes a pixel, 3 (RGB) or 1 (its luma) */
int wconf_channels(const struct wconf *c);
/* client.camera (0 pixels: the frames alone, 1 semantic: the semantic
 * camera alone, no renderer, 2 both), client.semantic's W and H and
 * client.semantic_range (blocks): csrc/runtime/semcam.h */
void wconf_semantic(const struct wconf *c, int *camera, int *w, int *h, int *range);
/* client.camera raycast: the textured ray cast alone at client.observation's
 * size, no renderer (csrc/runtime/raycast.h); wconf_semantic then says 0 */
int wconf_raycast(const struct wconf *c);
/* engine.raycast device (lane/rcdev): the ray cast's rays on the GPU (cuda/raycast), its frames on the device, every
 * byte the host cast's; host (the default) casts on each env's worker */
int wconf_raycast_device(const struct wconf *c);
/* engine.raycast_host_groups (lane/rcsplit): under engine.raycast device, how many of the vector env's groups still
 * cast on their workers (spread over the groups; their frames copied to the device's), so the CPU and the GPU share
 * the cast; 0 under host */
int wconf_raycast_host_groups(const struct wconf *c);
/* client.cast_precision (a native key): the textured cast's instance, 64 (the reference) or 32 (raycast.h) */
int wconf_cast_precision(const struct wconf *c);
/* client.cast_radius (native opt-in, default 0): raycast far-plane cap in blocks (0: render_distance * 16, vanilla) */
int wconf_cast_radius(const struct wconf *c);
/* engine.device_mesh (0, 1, 2) */
int wconf_device_mesh(const struct wconf *c);

/* pipeline.start as a path: an absolute one as is, else against ROOT (the
 * directory of config.yaml, configs/'s parent for a named file) */
void wconf_start_path(const struct wconf *c, char *out, size_t n);

/* Whether a start the oracle recorded is this world: its manifest's (or
 * tape header's) world object (seed, the switches, the game rules that are
 * not vanilla's) and its tape header's options (difficulty and rd: a tape
 * without them ran at normal and 4). An unsupported switch the file turns on
 * runs off, as in the oracle. check_seed 0 skips the seed. 1 when it is,
 * else 0 with the first difference in err. */
int wconf_match_start(const struct wconf *c, const struct jval *world, const struct jval *options, int check_seed,
                      char *err, size_t n);

#endif
