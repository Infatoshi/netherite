#include "game/config.h"

#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

enum {
    SEEN_SEED       = 1u << 0,
    SEEN_WORLD      = 1u << 1,
    SEEN_VILLAGES   = 1u << 2,
    SEEN_ENCHANTING = 1u << 3,
    SEEN_BREWING    = 1u << 4,
    SEEN_WEATHER    = 1u << 5,
    SEEN_RENDER     = 1u << 6,
    SEEN_BACKEND    = 1u << 7,
    SEEN_PACE       = 1u << 8,
    SEEN_VIEW       = 1u << 9,
    SEEN_WIDTH      = 1u << 10,
    SEEN_HEIGHT     = 1u << 11,
    SEEN_SENS       = 1u << 12,
    SEEN_FRAMES     = 1u << 13,
    SEEN_KILL       = 1u << 14,
    SEEN_PPM        = 1u << 15,
    SEEN_HEADLESS   = 1u << 16,
    SEEN_TICKS      = 1u << 17,
    SEEN_SCRIPT     = 1u << 18,
    SEEN_STATE_OUT  = 1u << 19,
    SEEN_FRAMES_OUT = 1u << 20,
    SEEN_FRAME_EVERY  = 1u << 21,
    SEEN_FRAME_OFFSET = 1u << 22,
    SEEN_MOBS         = 1u << 23,
    SEEN_DAYLIGHT     = 1u << 24,
    SEEN_SNAPSHOT_IN  = 1u << 25,
    SEEN_COMPOSE      = 1u << 26,
    SEEN_STATS        = 1u << 27,
    SEEN_CONF         = 1u << 28,
    SEEN_MOB_GRIEFING = 1u << 29
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

static int parse_ll(const char *s, long long *out) {
    char *end = NULL;
    errno = 0;
    long long v = strtoll(s, &end, 10);
    if (errno || !end || end == s || *end) return 0;
    *out = v;
    return 1;
}

static int parse_pos_int(const char *s, int *out) {
    long long v;
    if (!parse_ll(s, &v) || v <= 0 || v > INT_MAX) return 0;
    *out = (int)v;
    return 1;
}

static int parse_nonneg_int(const char *s, int *out) {
    long long v;
    if (!parse_ll(s, &v) || v < 0 || v > INT_MAX) return 0;
    *out = (int)v;
    return 1;
}

static int parse_on_off(const char *s, int *out) {
    if (!strcmp(s, "on")) { *out = 1; return 1; }
    if (!strcmp(s, "off")) { *out = 0; return 1; }
    return 0;
}

void gm_config_defaults(GmConfig *cfg) {
    memset(cfg, 0, sizeof *cfg);
    cfg->seed = 0;
    cfg->world = GM_WORLD_DEFAULT;
    cfg->render = GM_RENDER_WINDOW;
    cfg->compose = GM_COMPOSE_CAPTURE;
    cfg->backend = GM_BACKEND_CPU;
    cfg->pace = GM_PACE_REALTIME;
    cfg->view_distance = 8;
    cfg->width = 854;
    cfg->height = 480;
    cfg->ticks = -1;
    cfg->sensitivity = 0.15f;
    cfg->frames = -1;
    cfg->kill_frame = -1;
    cfg->frame_every = 1;
    cfg->frame_offset = 0;
    cfg->mobs = 1;
    cfg->daylight = 1;
    cfg->stats = 0;
    cfg->conf_path = NULL;
    cfg->n_set = 0;
    cfg->dump_config = 0;
    cfg->mob_griefing = 1;
}

int gm_config_parse(GmConfig *cfg, int argc, char **argv, char *err, int err_cap) {
    unsigned seen = 0;
    gm_config_defaults(cfg);
    if (err && err_cap > 0) err[0] = 0;

    for (int i = 1; i < argc; ++i) {
        const char *a = argv[i], *v;
        if (!strcmp(a, "--help")) { cfg->show_help = 1; continue; }
        if (!strcmp(a, "--print-config")) { cfg->print_config = 1; continue; }
        if (!strcmp(a, "--dump-config")) { cfg->dump_config = 1; continue; }

        if (!strcmp(a, "--headless")) {
            if (mark_once(&seen, SEEN_HEADLESS, a, err, err_cap)) return 2;
            cfg->headless = 1;
        } else if (!strcmp(a, "--rl")) {
            cfg->rl = 1;
            cfg->headless = 1; /* rl mode IS a headless harness mode */
        } else if (!strcmp(a, "--rl-bin")) {
            cfg->rl = 1;
            cfg->rl_bin = 1;
            cfg->headless = 1;
        } else if (!strcmp(a, "--ticks")) {
            if (mark_once(&seen, SEEN_TICKS, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!parse_nonneg_int(v, &cfg->ticks))
                return fail(err, err_cap, "ticks must be a non-negative integer");
        } else if (!strcmp(a, "--script") || !strcmp(a, "--state-out") ||
                   !strcmp(a, "--frames-out")) {
            unsigned bit = !strcmp(a, "--script") ? SEEN_SCRIPT :
                           !strcmp(a, "--state-out") ? SEEN_STATE_OUT : SEEN_FRAMES_OUT;
            const char **dst = !strcmp(a, "--script") ? &cfg->script_path :
                               !strcmp(a, "--state-out") ? &cfg->state_out_path :
                               &cfg->frames_out_dir;
            if (mark_once(&seen, bit, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!*v) return fail(err, err_cap, "%s path may not be empty", a);
            *dst = v;
        } else if (!strcmp(a, "--snapshot-in")) {
            if (mark_once(&seen, SEEN_SNAPSHOT_IN, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!*v) return fail(err, err_cap, "--snapshot-in path may not be empty");
            cfg->snapshot_in = v;
        } else if (!strcmp(a, "--frame-every")) {
            if (mark_once(&seen, SEEN_FRAME_EVERY, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!parse_pos_int(v, &cfg->frame_every))
                return fail(err, err_cap, "frame-every must be a positive integer");
        } else if (!strcmp(a, "--frame-offset")) {
            if (mark_once(&seen, SEEN_FRAME_OFFSET, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!parse_nonneg_int(v, &cfg->frame_offset))
                return fail(err, err_cap, "frame-offset must be a non-negative integer");
        } else if (!strcmp(a, "--seed")) {
            if (mark_once(&seen, SEEN_SEED, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!parse_ll(v, &cfg->seed)) return fail(err, err_cap, "invalid seed: %s", v);
        } else if (!strcmp(a, "--world")) {
            if (mark_once(&seen, SEEN_WORLD, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!strcmp(v, "default")) cfg->world = GM_WORLD_DEFAULT;
            else if (!strcmp(v, "superflat")) cfg->world = GM_WORLD_SUPERFLAT;
            else return fail(err, err_cap, "invalid world: %s", v);
        } else if (!strcmp(a, "--villages") || !strcmp(a, "--enchanting") ||
                   !strcmp(a, "--brewing") || !strcmp(a, "--weather") ||
                   !strcmp(a, "--mobs") || !strcmp(a, "--daylight") ||
                   !strcmp(a, "--stats") || !strcmp(a, "--mob-griefing")) {
            unsigned bit = !strcmp(a, "--villages") ? SEEN_VILLAGES :
                           !strcmp(a, "--enchanting") ? SEEN_ENCHANTING :
                           !strcmp(a, "--brewing") ? SEEN_BREWING :
                           !strcmp(a, "--weather") ? SEEN_WEATHER :
                           !strcmp(a, "--daylight") ? SEEN_DAYLIGHT :
                           !strcmp(a, "--stats") ? SEEN_STATS :
                           !strcmp(a, "--mob-griefing") ? SEEN_MOB_GRIEFING :
                           SEEN_MOBS;
            int *dst = !strcmp(a, "--villages") ? &cfg->villages :
                       !strcmp(a, "--enchanting") ? &cfg->enchanting :
                       !strcmp(a, "--brewing") ? &cfg->brewing :
                       !strcmp(a, "--weather") ? &cfg->weather :
                       !strcmp(a, "--daylight") ? &cfg->daylight :
                       !strcmp(a, "--stats") ? &cfg->stats :
                       !strcmp(a, "--mob-griefing") ? &cfg->mob_griefing :
                       &cfg->mobs;
            if (mark_once(&seen, bit, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!parse_on_off(v, dst)) return fail(err, err_cap, "%s expects on|off", a);
        } else if (!strcmp(a, "--render")) {
            if (mark_once(&seen, SEEN_RENDER, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!strcmp(v, "window")) cfg->render = GM_RENDER_WINDOW;
            else if (!strcmp(v, "off")) cfg->render = GM_RENDER_OFF;
            else return fail(err, err_cap, "invalid render mode: %s", v);
        } else if (!strcmp(a, "--compose")) {
            if (mark_once(&seen, SEEN_COMPOSE, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!strcmp(v, "capture")) cfg->compose = GM_COMPOSE_CAPTURE;
            else if (!strcmp(v, "window")) cfg->compose = GM_COMPOSE_WINDOW;
            else return fail(err, err_cap, "invalid compose mode: %s", v);
        } else if (!strcmp(a, "--backend") || !strcmp(a, "--cuda")) {
            if (mark_once(&seen, SEEN_BACKEND, a, err, err_cap)) return 2;
            if (!strcmp(a, "--cuda")) cfg->backend = GM_BACKEND_CUDA;
            else {
                if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
                if (!strcmp(v, "cpu")) cfg->backend = GM_BACKEND_CPU;
                else if (!strcmp(v, "cuda")) cfg->backend = GM_BACKEND_CUDA;
                else if (!strcmp(v, "metal")) cfg->backend = GM_BACKEND_METAL;
                else return fail(err, err_cap, "invalid backend: %s", v);
            }
        } else if (!strcmp(a, "--pace")) {
            if (mark_once(&seen, SEEN_PACE, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!strcmp(v, "realtime")) cfg->pace = GM_PACE_REALTIME;
            else if (!strcmp(v, "unlimited")) cfg->pace = GM_PACE_UNLIMITED;
            else return fail(err, err_cap, "invalid pace: %s", v);
        } else if (!strcmp(a, "--view-distance")) {
            if (mark_once(&seen, SEEN_VIEW, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!parse_pos_int(v, &cfg->view_distance) || cfg->view_distance > 8)
                return fail(err, err_cap, "view distance must be in 1..8");
        } else if (!strcmp(a, "--width") || !strcmp(a, "--w")) {
            if (mark_once(&seen, SEEN_WIDTH, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap)) || !parse_pos_int(v, &cfg->width))
                return fail(err, err_cap, "width must be a positive integer");
        } else if (!strcmp(a, "--height") || !strcmp(a, "--h")) {
            if (mark_once(&seen, SEEN_HEIGHT, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap)) || !parse_pos_int(v, &cfg->height))
                return fail(err, err_cap, "height must be a positive integer");
        } else if (!strcmp(a, "--sens")) {
            char *end = NULL;
            if (mark_once(&seen, SEEN_SENS, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            errno = 0;
            cfg->sensitivity = strtof(v, &end);
            if (errno || !end || *end || !isfinite(cfg->sensitivity) || cfg->sensitivity <= 0.0f)
                return fail(err, err_cap, "sensitivity must be finite and positive");
        } else if (!strcmp(a, "--frames")) {
            if (mark_once(&seen, SEEN_FRAMES, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap)) ||
                !parse_nonneg_int(v, &cfg->frames))
                return fail(err, err_cap, "frames must be a non-negative integer");
        } else if (!strcmp(a, "--kill-frame")) {
            if (mark_once(&seen, SEEN_KILL, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap)) ||
                !parse_nonneg_int(v, &cfg->kill_frame))
                return fail(err, err_cap, "kill-frame must be a non-negative integer");
        } else if (!strcmp(a, "--conf")) {
            if (mark_once(&seen, SEEN_CONF, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!*v) return fail(err, err_cap, "--conf path may not be empty");
            cfg->conf_path = v;
        } else if (!strcmp(a, "--set")) {
            /* Repeatable on purpose (last wins), so no mark_once here. */
            if (!(v = need_value(&i, argc, argv, err, err_cap))) return 2;
            if (!strchr(v, '='))
                return fail(err, err_cap, "--set expects key=value, got: %s", v);
            if (cfg->n_set >= GM_CONFIG_MAX_SET)
                return fail(err, err_cap, "too many --set overrides (max %d)",
                            GM_CONFIG_MAX_SET);
            cfg->set_kv[cfg->n_set++] = v;
        } else if (!strcmp(a, "--ppm")) {
            if (mark_once(&seen, SEEN_PPM, a, err, err_cap)) return 2;
            if (!(v = need_value(&i, argc, argv, err, err_cap)) || !*v)
                return fail(err, err_cap, "ppm path may not be empty");
            cfg->ppm_path = v;
        } else {
            return fail(err, err_cap, "unknown option: %s", a);
        }
    }
    if ((seen & SEEN_TICKS) && (seen & SEEN_FRAMES))
        return fail(err, err_cap, "--ticks may not be combined with legacy --frames");
    if (cfg->headless && !cfg->rl && !(seen & SEEN_TICKS))
        return fail(err, err_cap, "--headless requires --ticks");
    return 0;
}

int gm_config_validate_runtime(const GmConfig *cfg, int cuda_compiled,
                               int metal_compiled, char *err, int err_cap) {
    if (cfg->villages && cfg->world != GM_WORLD_DEFAULT)
        return fail(err, err_cap, "--villages on currently requires --world default");
    if (cfg->render == GM_RENDER_OFF && !cfg->headless)
        return fail(err, err_cap, "--render off requires the shared headless runner, not wired yet");
    if (cfg->pace == GM_PACE_UNLIMITED && !cfg->headless)
        return fail(err, err_cap, "--pace unlimited requires the shared headless runner, not wired yet");
    if (!cfg->headless && (cfg->ticks >= 0 || cfg->script_path || cfg->state_out_path || cfg->frames_out_dir))
        return fail(err, err_cap, "harness controls require --headless");
    if (cfg->snapshot_in && !cfg->rl)
        return fail(err, err_cap, "--snapshot-in requires --rl/--rl-bin");
    if (cfg->backend == GM_BACKEND_CUDA && !cuda_compiled)
        return fail(err, err_cap, "CUDA backend unavailable; rebuild with `make game-cuda`");
    if (cfg->backend == GM_BACKEND_METAL && !metal_compiled)
        return fail(err, err_cap, "Metal backend unavailable; rebuild with `make game-metal`");
    return 0;
}

void gm_config_print(FILE *out, const GmConfig *c) {
    fprintf(out,
        "seed=%lld world=%s villages=%s enchanting=%s brewing=%s weather=%s "
        "mobs=%s daylight=%s stats=%s mob_griefing=%s "
        "render=%s compose=%s backend=%s pace=%s view_distance=%d width=%d height=%d "
        "headless=%s ticks=%d script=%s state_out=%s frames_out=%s\n",
        c->seed, c->world == GM_WORLD_DEFAULT ? "default" : "superflat",
        c->villages ? "on" : "off", c->enchanting ? "on" : "off",
        c->brewing ? "on" : "off", c->weather ? "on" : "off",
        c->mobs ? "on" : "off", c->daylight ? "on" : "off",
        c->stats ? "on" : "off",
        c->mob_griefing ? "on" : "off",
        c->render == GM_RENDER_WINDOW ? "window" : "off",
        c->compose == GM_COMPOSE_CAPTURE ? "capture" : "window",
        c->backend == GM_BACKEND_CPU ? "cpu" :
        c->backend == GM_BACKEND_CUDA ? "cuda" : "metal",
        c->pace == GM_PACE_REALTIME ? "realtime" : "unlimited",
        c->view_distance, c->width, c->height,
        c->headless ? "on" : "off", c->ticks,
        c->script_path ? c->script_path : "(none)",
        c->state_out_path ? c->state_out_path : "(none)",
        c->frames_out_dir ? c->frames_out_dir : "(none)");
}

void gm_config_print_usage(FILE *out, const char *argv0) {
    fprintf(out,
        "usage: %s [settings] [developer capture controls]\n"
        "  --seed N                     world seed (default 0)\n"
        "  --world default|superflat    world provider\n"
        "  --villages on|off            optional village bundle\n"
        "  --enchanting on|off          optional enchanting bundle\n"
        "  --brewing on|off             optional brewing bundle\n"
        "  --weather on|off             optional weather bundle\n"
        "  --daylight on|off            doDaylightCycle (off = frozen clock)\n"
        "  --mobs on|off                mob spawning + AI (default on)\n"
        "  --stats on|off               per-second frame timing stats to stderr\n"
        "  --mob-griefing on|off        mobGriefing gamerule (default on)\n"
        "  --render window|off          presentation mode\n"
        "  --compose capture|window     headless frame compositor (default capture)\n"
        "  --backend cpu|cuda|metal     raster backend\n"
        "  --pace realtime|unlimited    simulation pacing\n"
        "  --view-distance N            supported range 1..8\n"
        "  --width W --height H         framebuffer size\n"
        "  --headless                   run the shared deterministic harness\n"
        "  --rl                         RL step mode: action JSON per stdin line,\n"
        "                               obs JSON per stdout line (implies --headless)\n"
        "  --rl-bin                     RL step mode with packed binary obs records\n"
        "  --ticks N                    non-negative harness tick count\n"
        "  --script PATH                harness event script\n"
        "  --state-out PATH             write harness state output\n"
        "  --frames-out DIR             write harness frames to directory\n"
        "  --frame-every N              render 1 in N ticks to frames-out (default 1)\n"
        "  --frame-offset K             first rendered tick for sparse capture (default 0)\n"
        "  --snapshot-in PATH           load a .bsnp state snapshot after init (rl mode)\n"
        "  --conf PATH                  config registry file (default ./magma.conf)\n"
        "  --set key=value              registry override, repeatable, applied after --conf\n"
        "  --dump-config                print the effective config registry and exit\n"
        "  --print-config               print canonical effective settings and exit\n"
        "  --help                       show this help\n"
        "developer capture controls: --sens S --frames N --kill-frame N --ppm PATH\n",
        argv0 ? argv0 : "magma_game");
}
