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
 * (wconf_defaults).
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
    WC_GRAPHICS = WC_UNSUPPORTED0 + 17, WC_PARTICLES, WC_OBSERVATION, WC_TICKS_PER_STEP, WC_HUD, WC_GAMMA,
    WC_CAMERA, WC_SEMANTIC, WC_SEMANTIC_RANGE,
    WC_DEVICE_MESH, WC_DEVICE_GENERATION, WC_DEVICE_LIGHT,
    WC_KIND, WC_START, WC_WORLDS, WC_GROUPS, WC_CARDS, WC_EPISODE_SECONDS,
    WC_CLICK_INTERVAL, WC_MASK_CLOSE_GRID, WC_MASK_NOOP_CLICKS, WC_MOVE_STACK, WC_ITEM_SELECT, WC_RECIPES,
    WC_HOLD_ATTACK, WC_LOOK_TARGET, WC_LOOK_TICKS,
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
/* client.camera (0 pixels: the frames alone, 1 semantic: the semantic
 * camera alone, no renderer, 2 both), client.semantic's W and H and
 * client.semantic_range (blocks): csrc/runtime/semcam.h */
void wconf_semantic(const struct wconf *c, int *camera, int *w, int *h, int *range);
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
