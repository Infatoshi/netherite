#include "game/config.h"
#include "core/config.h"

#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

enum {
    SEEN_SEED         = 1u << 0,
    SEEN_WORLD        = 1u << 1,
    SEEN_VILLAGES     = 1u << 2,
    SEEN_ENCHANTING   = 1u << 3,
    SEEN_BREWING      = 1u << 4,
    SEEN_WEATHER      = 1u << 5,
    SEEN_RENDER       = 1u << 6,
    SEEN_BACKEND      = 1u << 7,
    SEEN_PACE         = 1u << 8,
    SEEN_VIEW         = 1u << 9,
    SEEN_WIDTH        = 1u << 10,
    SEEN_HEIGHT       = 1u << 11,
    SEEN_SENS         = 1u << 12,
    SEEN_FRAMES       = 1u << 13,
    SEEN_KILL         = 1u << 14,
    SEEN_PPM          = 1u << 15,
    SEEN_HEADLESS     = 1u << 16,
    SEEN_TICKS        = 1u << 17,
    SEEN_SCRIPT       = 1u << 18,
    SEEN_STATE_OUT    = 1u << 19,
    SEEN_FRAMES_OUT   = 1u << 20,
    SEEN_FRAME_EVERY  = 1u << 21,
    SEEN_FRAME_OFFSET = 1u << 22,
    SEEN_MOBS         = 1u << 23,
    SEEN_DAYLIGHT     = 1u << 24,
    SEEN_SNAPSHOT_IN  = 1u << 25,
    SEEN_COMPOSE      = 1u << 26,
    SEEN_STATS        = 1u << 27,
    SEEN_CONF         = 1u << 28,
    SEEN_RL           = 1u << 29,
    SEEN_RL_BIN       = 1u << 30
};

static int fail(char *err, int cap, const char *fmt, ...) {
    if (err && cap > 0) {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(err, (size_t)cap, fmt, ap);
        va_end(ap);
    }
    return 2;
}

static int mark_once(unsigned *seen, unsigned bit, const char *name,
                     char *err, int cap) {
    if (*seen & bit) return fail(err, cap, "duplicate option: %s", name);
    *seen |= bit;
    return 0;
}

static const char *need_value(int *i, int argc, char **argv, char *err, int cap) {
    if (*i + 1 >= argc) {
        fail(err, cap, "missing value for %s", argv[*i]);
        return NULL;
    }
    ++*i;
    return argv[*i];
}

static int set_value(const char *key, const char *value,
                     char *err, int err_cap) {
    int rc = cr_cfg_set(key, value);
    if (rc == -1) return fail(err, err_cap, "unknown config key: %s", key);
    if (rc == -2) return fail(err, err_cap, "bad value for config key %s: %s",
                              key, value);
    return 0;
}

static int set_kv(const char *kv, char *err, int err_cap) {
    const char *eq = strchr(kv, '=');
    char key[64];
    size_t n;
    if (!eq || eq == kv) return fail(err, err_cap, "--set expects key=value: %s", kv);
    n = (size_t)(eq - kv);
    if (n >= sizeof key) return fail(err, err_cap, "config key is too long: %s", kv);
    memcpy(key, kv, n);
    key[n] = '\0';
    return set_value(key, eq + 1, err, err_cap);
}

static int resolve_config(GmConfig *cfg, const CrConfig *r,
                          char *err, int err_cap) {
    cfg->seed = r->seed;
    if (!strcmp(r->world, "default")) cfg->world = GM_WORLD_DEFAULT;
    else if (!strcmp(r->world, "superflat")) cfg->world = GM_WORLD_SUPERFLAT;
    else return fail(err, err_cap, "world must be default or superflat: %s", r->world);

    cfg->villages = r->villages;
    cfg->enchanting = r->enchanting;
    cfg->brewing = r->brewing;
    cfg->weather = r->weather;
    cfg->xp = r->xp;
    cfg->elytra = r->elytra;
    cfg->daylight = r->daylight;
    cfg->mobs = r->mobs;
    cfg->natural_spawn = r->natural_spawn;
    cfg->natural_spawn_passive = r->natural_spawn_passive;
    cfg->stats = r->stats;

    if (!strcmp(r->render, "window")) cfg->render = GM_RENDER_WINDOW;
    else if (!strcmp(r->render, "off")) cfg->render = GM_RENDER_OFF;
    else return fail(err, err_cap, "render must be window or off: %s", r->render);

    if (!strcmp(r->compose, "capture")) cfg->compose = GM_COMPOSE_CAPTURE;
    else if (!strcmp(r->compose, "window")) cfg->compose = GM_COMPOSE_WINDOW;
    else return fail(err, err_cap, "compose must be capture or window: %s", r->compose);

    if (!strcmp(r->backend, "cpu")) cfg->backend = GM_BACKEND_CPU;
    else if (!strcmp(r->backend, "cuda")) cfg->backend = GM_BACKEND_CUDA;
    else if (!strcmp(r->backend, "metal")) cfg->backend = GM_BACKEND_METAL;
    else return fail(err, err_cap, "backend must be cpu, cuda, or metal: %s", r->backend);
    cfg->device = r->device;

    if (!strcmp(r->pace, "realtime")) cfg->pace = GM_PACE_REALTIME;
    else if (!strcmp(r->pace, "unlimited")) cfg->pace = GM_PACE_UNLIMITED;
    else return fail(err, err_cap, "pace must be realtime or unlimited: %s", r->pace);

    cfg->view_distance = r->view_distance;
    cfg->width = r->width;
    cfg->height = r->height;
    cfg->headless = r->headless;
    cfg->rl = r->rl;
    cfg->rl_bin = r->rl_bin;
    if (cfg->rl_bin) cfg->rl = 1;
    if (cfg->rl) cfg->headless = 1;
    cfg->ticks = r->ticks;
    cfg->script_path = r->script[0] ? r->script : NULL;
    cfg->state_out_path = r->state_out[0] ? r->state_out : NULL;
    cfg->frames_out_dir = r->frames_out[0] ? r->frames_out : NULL;
    cfg->snapshot_in = r->snapshot_in[0] ? r->snapshot_in : NULL;
    cfg->frame_every = r->frame_every;
    cfg->frame_offset = r->frame_offset;
    cfg->sensitivity = r->sensitivity;
    cfg->frames = r->frames;
    cfg->kill_frame = r->kill_frame;
    cfg->ppm_path = r->ppm[0] ? r->ppm : NULL;

    if (cfg->view_distance < 1 || cfg->view_distance > 8)
        return fail(err, err_cap, "view_distance must be in 1..8");
    if (cfg->width <= 0 || cfg->height <= 0)
        return fail(err, err_cap, "width and height must be positive");
    if (cfg->device < 0) return fail(err, err_cap, "device must be non-negative");
    if (!isfinite(cfg->sensitivity) || cfg->sensitivity <= 0.0f)
        return fail(err, err_cap, "sensitivity must be finite and positive");
    if (cfg->ticks < -1) return fail(err, err_cap, "ticks must be -1 or non-negative");
    if (cfg->frames < -1) return fail(err, err_cap, "frames must be -1 or non-negative");
    if (cfg->kill_frame < -1)
        return fail(err, err_cap, "kill_frame must be -1 or non-negative");
    if (cfg->frame_every <= 0) return fail(err, err_cap, "frame_every must be positive");
    if (cfg->frame_offset < 0) return fail(err, err_cap, "frame_offset must be non-negative");
    if (cfg->ticks >= 0 && cfg->frames >= 0)
        return fail(err, err_cap, "ticks and legacy frames cannot both be set");
    if (cfg->headless && !cfg->rl && cfg->ticks < 0)
        return fail(err, err_cap, "headless mode requires ticks");
    return 0;
}

void gm_config_defaults(GmConfig *cfg) {
    CrConfig defaults;
    char err[128];
    memset(cfg, 0, sizeof *cfg);
    cr_cfg_defaults(&defaults);
    (void)resolve_config(cfg, &defaults, err, sizeof err);
}

static int find_conf(int argc, char **argv, const char **path,
                     int *show_help, char *err, int err_cap) {
    int seen = 0;
    *path = NULL;
    *show_help = 0;
    for (int i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--help")) *show_help = 1;
        if (strcmp(argv[i], "--conf")) continue;
        if (seen) return fail(err, err_cap, "duplicate option: --conf");
        if (i + 1 >= argc) return fail(err, err_cap, "missing value for --conf");
        if (!argv[i + 1][0]) return fail(err, err_cap, "--conf path may not be empty");
        *path = argv[++i];
        seen = 1;
    }
    return 0;
}

int gm_config_parse(GmConfig *cfg, int argc, char **argv, char *err, int err_cap) {
    const char *conf_path;
    unsigned seen = 0;
    int show_help = 0;

    gm_config_defaults(cfg);
    if (err && err_cap > 0) err[0] = '\0';
    if (find_conf(argc, argv, &conf_path, &show_help, err, err_cap)) return 2;
    if (show_help) {
        cfg->show_help = 1;
        return 0;
    }

    cr_cfg_load(conf_path);
    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i];
        const char *v;
        const char *key = NULL;
        unsigned bit = 0;

        if (!strcmp(a, "--print-config")) { cfg->print_config = 1; continue; }
        if (!strcmp(a, "--dump-config")) { cfg->dump_config = 1; continue; }
        if (!strcmp(a, "--conf")) { ++i; continue; }
        if (!strcmp(a, "--set")) {
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (set_kv(v, err, err_cap)) return 2;
            continue;
        }
        if (!strcmp(a, "--headless")) {
            if (mark_once(&seen, SEEN_HEADLESS, a, err, err_cap)) return 2;
            if (set_value("headless", "1", err, err_cap)) return 2;
            continue;
        }
        if (!strcmp(a, "--rl")) {
            if (mark_once(&seen, SEEN_RL, a, err, err_cap)) return 2;
            if (set_value("rl", "1", err, err_cap)) return 2;
            continue;
        }
        if (!strcmp(a, "--rl-bin")) {
            if (mark_once(&seen, SEEN_RL_BIN, a, err, err_cap)) return 2;
            if (set_value("rl_bin", "1", err, err_cap)) return 2;
            continue;
        }
        if (!strcmp(a, "--cuda")) {
            if (mark_once(&seen, SEEN_BACKEND, a, err, err_cap)) return 2;
            if (set_value("backend", "cuda", err, err_cap)) return 2;
            continue;
        }

#define ARG(opt, cfg_key, seen_bit) \
        if (!strcmp(a, opt)) { key = cfg_key; bit = seen_bit; }
        ARG("--seed", "seed", SEEN_SEED)
        else ARG("--world", "world", SEEN_WORLD)
        else ARG("--villages", "villages", SEEN_VILLAGES)
        else ARG("--enchanting", "enchanting", SEEN_ENCHANTING)
        else ARG("--brewing", "brewing", SEEN_BREWING)
        else ARG("--weather", "weather", SEEN_WEATHER)
        else ARG("--daylight", "daylight", SEEN_DAYLIGHT)
        else ARG("--mobs", "mobs", SEEN_MOBS)
        else ARG("--stats", "stats", SEEN_STATS)
        else ARG("--render", "render", SEEN_RENDER)
        else ARG("--compose", "compose", SEEN_COMPOSE)
        else ARG("--backend", "backend", SEEN_BACKEND)
        else ARG("--pace", "pace", SEEN_PACE)
        else ARG("--view-distance", "view_distance", SEEN_VIEW)
        else ARG("--width", "width", SEEN_WIDTH)
        else ARG("--w", "width", SEEN_WIDTH)
        else ARG("--height", "height", SEEN_HEIGHT)
        else ARG("--h", "height", SEEN_HEIGHT)
        else ARG("--ticks", "ticks", SEEN_TICKS)
        else ARG("--script", "script", SEEN_SCRIPT)
        else ARG("--state-out", "state_out", SEEN_STATE_OUT)
        else ARG("--frames-out", "frames_out", SEEN_FRAMES_OUT)
        else ARG("--snapshot-in", "snapshot_in", SEEN_SNAPSHOT_IN)
        else ARG("--frame-every", "frame_every", SEEN_FRAME_EVERY)
        else ARG("--frame-offset", "frame_offset", SEEN_FRAME_OFFSET)
        else ARG("--sens", "sensitivity", SEEN_SENS)
        else ARG("--frames", "frames", SEEN_FRAMES)
        else ARG("--kill-frame", "kill_frame", SEEN_KILL)
        else ARG("--ppm", "ppm", SEEN_PPM)
#undef ARG
        else return fail(err, err_cap, "unknown option: %s", a);

        if (mark_once(&seen, bit, a, err, err_cap)) return 2;
        if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
        if ((!strcmp(key, "script") || !strcmp(key, "state_out") ||
             !strcmp(key, "frames_out") || !strcmp(key, "snapshot_in") ||
             !strcmp(key, "ppm")) && !v[0])
            return fail(err, err_cap, "%s path may not be empty", a);
        if ((!strcmp(key, "ticks") || !strcmp(key, "frames") ||
             !strcmp(key, "kill_frame") || !strcmp(key, "frame_offset")) &&
            v[0] == '-')
            return fail(err, err_cap, "%s must be non-negative", a);
        if (!strcmp(key, "villages") || !strcmp(key, "enchanting") ||
            !strcmp(key, "brewing") || !strcmp(key, "weather") ||
            !strcmp(key, "xp") || !strcmp(key, "elytra") ||
            !strcmp(key, "daylight") || !strcmp(key, "mobs") ||
            !strcmp(key, "stats")) {
            if (!strcmp(v, "on")) v = "1";
            else if (!strcmp(v, "off")) v = "0";
            else return fail(err, err_cap, "%s expects on or off", a);
        }
        if (set_value(key, v, err, err_cap)) return 2;
    }

    if (resolve_config(cfg, cr_cfg(), err, err_cap)) return 2;
    return 0;
}

int gm_config_validate_runtime(const GmConfig *cfg, int cuda_compiled,
                               int metal_compiled, char *err, int err_cap) {
    if (cfg->villages)
        return fail(err, err_cap, "villages=1 is not wired");
    if (cfg->enchanting)
        return fail(err, err_cap, "enchanting=1 is not wired");
    if (cfg->brewing)
        return fail(err, err_cap, "brewing=1 is not wired");
    /* weather=1 runs gm_world_tick (WorldInfo rain/thunder timers +
     * worldTime). Sky/rain fade is still strength=0 on the live path. */
    if (cfg->render == GM_RENDER_OFF && !cfg->headless)
        return fail(err, err_cap, "render=off requires headless=1");
    if (cfg->pace == GM_PACE_UNLIMITED && !cfg->headless)
        return fail(err, err_cap, "pace=unlimited requires headless=1");
    if (!cfg->headless && (cfg->ticks >= 0 || cfg->script_path ||
                           cfg->state_out_path || cfg->frames_out_dir))
        return fail(err, err_cap, "harness settings require headless=1");
    if (cfg->snapshot_in && !cfg->rl)
        return fail(err, err_cap, "snapshot_in requires rl=1 or rl_bin=1");
    if (cfg->backend == GM_BACKEND_CUDA && !cuda_compiled)
        return fail(err, err_cap, "CUDA backend is not in this build; use make game-cuda");
    if (cfg->backend == GM_BACKEND_METAL && !metal_compiled)
        return fail(err, err_cap, "Metal backend is not in this build; use make game-metal");
    return 0;
}

void gm_config_print(FILE *out, const GmConfig *c) {
    fprintf(out,
        "seed=%lld world=%s villages=%d enchanting=%d brewing=%d weather=%d "
        "mobs=%d daylight=%d stats=%d render=%s compose=%s backend=%s device=%d pace=%s "
        "view_distance=%d width=%d height=%d headless=%d ticks=%d "
        "script=%s state_out=%s frames_out=%s\n",
        c->seed, c->world == GM_WORLD_DEFAULT ? "default" : "superflat",
        c->villages, c->enchanting, c->brewing, c->weather, c->mobs,
        c->daylight, c->stats,
        c->render == GM_RENDER_WINDOW ? "window" : "off",
        c->compose == GM_COMPOSE_CAPTURE ? "capture" : "window",
        c->backend == GM_BACKEND_CPU ? "cpu" :
        c->backend == GM_BACKEND_CUDA ? "cuda" : "metal",
        c->device,
        c->pace == GM_PACE_REALTIME ? "realtime" : "unlimited",
        c->view_distance, c->width, c->height, c->headless, c->ticks,
        c->script_path ? c->script_path : "",
        c->state_out_path ? c->state_out_path : "",
        c->frames_out_dir ? c->frames_out_dir : "");
}

void gm_config_print_usage(FILE *out, const char *argv0) {
    fprintf(out,
        "usage: %s --conf FILE [--set key=value]...\n"
        "  --conf FILE        load the complete game config\n"
        "  --set key=value    override one config value\n"
        "  --dump-config      print all effective values\n"
        "  --print-config     print the resolved game settings\n"
        "  --help             show this help\n"
        "Legacy long options remain aliases for config keys.\n",
        argv0 ? argv0 : "magma_game");
}
