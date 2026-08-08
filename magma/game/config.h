#ifndef MAGMA_GAME_CONFIG_H
#define MAGMA_GAME_CONFIG_H

#include <stdio.h>

typedef enum { GM_WORLD_DEFAULT = 0, GM_WORLD_SUPERFLAT = 1 } GmWorldType;
typedef enum { GM_RENDER_WINDOW = 0, GM_RENDER_OFF = 1 } GmRenderMode;
typedef enum { GM_COMPOSE_CAPTURE = 0, GM_COMPOSE_WINDOW = 1 } GmComposeMode;
typedef enum { GM_BACKEND_CPU = 0, GM_BACKEND_CUDA = 1, GM_BACKEND_METAL = 2 } GmBackend;
typedef enum { GM_PACE_REALTIME = 0, GM_PACE_UNLIMITED = 1 } GmPace;

/* Max repeats of --set key=value on one command line (fixed storage, no heap). */
#define GM_CONFIG_MAX_SET 32

typedef struct {
    long long seed;
    GmWorldType world;
    int villages;
    int enchanting;
    int brewing;
    int weather;
    /* doDaylightCycle: 1 = worldTime advances per tick (vanilla default);
     * 0 = frozen clock (the trace profile pins world_time for pixel
     * determinism - the oracle session magma replays against never
     * leaves its start time-of-day). */
    int daylight;
    /* Mob simulation (spawning + AI). Default on. Tape replay passes off when
     * the recorded oracle session ran with doMobSpawning=false: magma's spawn
     * RNG cannot match the oracle's, and phantom mobs corrupt the replay. */
    int mobs;
    /* Live per-second frame timing stats on stderr (--stats on). Window loop
     * only; piggybacks on the bench stamp slots, no sim state touched. */
    int stats;
    /* mobGriefing: gates world mutation by living-mob projectiles. */
    int mob_griefing;
    GmRenderMode render;
    GmComposeMode compose;
    GmBackend backend;
    GmPace pace;
    int view_distance;
    int width, height;

    /* Shared deterministic harness controls. */
    int headless;
    /* RL step mode (game/rl_mode.c): implies headless; JSON action per stdin
     * line, JSON obs per stdout line. rl_bin (--rl-bin): packed binary obs
     * records on stdout instead of JSON (actions stay JSON - they are tiny);
     * record layout in rl_mode.c, magic 0x524c4f42. */
    int rl;
    int rl_bin;
    /* .bsnp state snapshot to load after runtime init (rl_mode.c format;
     * written by the "snapshot" action key). Seed inside must match --seed. */
    const char *snapshot_in;
    int ticks;
    const char *script_path;
    const char *state_out_path;
    const char *frames_out_dir;
    /* Sparse frame capture: render only ticks where (tick - frame_offset) is a
     * non-negative multiple of frame_every. Skipped ticks still advance the
     * per-tick animation state (hand bob/swing), so the rendered frames are
     * pixel-identical to an every-tick run. Frame files keep tick numbering. */
    int frame_every;
    int frame_offset;

    /* Existing developer/capture controls retained while the shared headless
     * event-script runner is built. They are not gameplay feature settings. */
    float sensitivity;
    int frames;
    int kill_frame;
    const char *ppm_path;

    int show_help;
    int print_config;

    /* ---- config registry front end (core/config.h) ----
     * conf_path: --conf PATH, the file the registry loads (NULL -> magma.conf
     * in the cwd). This replaced the MAGMA_CONF env var.
     * set_kv / n_set: --set key=value, repeatable, applied AFTER the file in
     * argv order so the last one wins. Fixed array, no heap; the strings point
     * straight into argv. */
    const char *conf_path;
    const char *set_kv[GM_CONFIG_MAX_SET];
    int n_set;
    int dump_config;
} GmConfig;

void gm_config_defaults(GmConfig *cfg);
/* 0 success, 2 command-line error. */
int gm_config_parse(GmConfig *cfg, int argc, char **argv, char *err, int err_cap);
/* Reject accepted target settings that this build cannot execute yet. */
int gm_config_validate_runtime(const GmConfig *cfg, int cuda_compiled,
                               int metal_compiled, char *err, int err_cap);
void gm_config_print_usage(FILE *out, const char *argv0);
void gm_config_print(FILE *out, const GmConfig *cfg);

#endif
