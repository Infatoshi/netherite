#include <stdio.h>
#include <string.h>
#include "game/config.h"

static int fails;
#define CHECK(C, M) do { if (!(C)) { fprintf(stderr, "FAIL: %s\n", (M)); ++fails; } } while (0)

static int parse(GmConfig *c, int n, char **v, char *err) {
    return gm_config_parse(c, n, v, err, 256);
}

static void read_print(void (*print_fn)(FILE *, const GmConfig *),
                       const GmConfig *c, char *buf, size_t cap) {
    FILE *f = tmpfile();
    CHECK(f != NULL, "create temporary print stream");
    if (!f) { if (cap) buf[0] = 0; return; }
    print_fn(f, c);
    rewind(f);
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = 0;
    fclose(f);
}

static void read_usage(char *buf, size_t cap) {
    FILE *f = tmpfile();
    CHECK(f != NULL, "create temporary usage stream");
    if (!f) { if (cap) buf[0] = 0; return; }
    gm_config_print_usage(f, "game");
    rewind(f);
    size_t n = fread(buf, 1, cap - 1, f);
    buf[n] = 0;
    fclose(f);
}

int main(void) {
    GmConfig c;
    char err[256];
    char *defaults[] = {"game"};
    CHECK(parse(&c, 1, defaults, err) == 0, "defaults parse");
    CHECK(c.seed == 0 && c.world == GM_WORLD_DEFAULT, "default seed/world");
    CHECK(!c.villages && !c.enchanting && !c.brewing && !c.weather,
          "optional bundles default off");
    CHECK(c.render == GM_RENDER_WINDOW && c.compose == GM_COMPOSE_CAPTURE &&
          c.backend == GM_BACKEND_CPU &&
          c.pace == GM_PACE_REALTIME, "default execution profile");
    CHECK(c.mob_griefing, "mobGriefing defaults on");
    CHECK(c.view_distance == 8 && c.width == 854 && c.height == 480,
          "default render dimensions");
    CHECK(!c.headless && c.ticks == -1 && !c.script_path &&
          !c.state_out_path && !c.frames_out_dir, "default harness controls");
    CHECK(gm_config_validate_runtime(&c, 0, 0, err, sizeof err) == 0,
          "current default is runnable");

    char *full[] = {
        "game", "--seed", "-7", "--world", "superflat",
        "--villages", "on", "--enchanting", "on", "--brewing", "on",
        "--weather", "on", "--render", "off", "--compose", "window",
        "--mob-griefing", "off", "--backend", "cuda",
        "--pace", "unlimited", "--view-distance", "4", "--width", "640",
        "--height", "360", "--headless", "--ticks", "42",
        "--script", "events.jsonl", "--state-out", "state.jsonl",
        "--frames-out", "frames", "--print-config"
    };
    CHECK(parse(&c, (int)(sizeof full / sizeof full[0]), full, err) == 0,
          "target product settings parse");
    CHECK(c.seed == -7 && c.world == GM_WORLD_SUPERFLAT && c.villages &&
          c.enchanting && c.brewing && c.weather, "target bundle values retained");
    CHECK(c.render == GM_RENDER_OFF && c.compose == GM_COMPOSE_WINDOW &&
          c.backend == GM_BACKEND_CUDA &&
          c.pace == GM_PACE_UNLIMITED && c.view_distance == 4,
          "target execution values retained");
    CHECK(!c.mob_griefing, "mobGriefing value retained");
    CHECK(c.headless && c.ticks == 42 && !strcmp(c.script_path, "events.jsonl") &&
          !strcmp(c.state_out_path, "state.jsonl") &&
          !strcmp(c.frames_out_dir, "frames"), "canonical harness values retained");

    char printed[4096];
    read_print(gm_config_print, &c, printed, sizeof printed);
    CHECK(strstr(printed, "headless=on") && strstr(printed, "ticks=42") &&
          strstr(printed, "compose=window") &&
          strstr(printed, "mob_griefing=off") &&
          strstr(printed, "script=events.jsonl") &&
          strstr(printed, "state_out=state.jsonl") &&
          strstr(printed, "frames_out=frames"), "canonical print includes harness values");
    read_usage(printed, sizeof printed);
    CHECK(strstr(printed, "--mob-griefing on|off") &&
          strstr(printed, "--headless") && strstr(printed, "--ticks N") &&
          strstr(printed, "--script PATH") && strstr(printed, "--state-out PATH") &&
          strstr(printed, "--frames-out DIR") && strstr(printed, "--compose"),
          "usage includes harness options");
    CHECK(gm_config_validate_runtime(&c, 1, 0, err, sizeof err) == 2 &&
          strstr(err, "villages"), "villages reject the unsupported superflat provider");

    {
        char *villages_only[] = {"game", "--villages", "on"};
        CHECK(parse(&c, 3, villages_only, err) == 0 &&
              gm_config_validate_runtime(&c, 0, 0, err, sizeof err) == 0,
              "village bundle is runnable in the default world");
    }

    {
        char *brewing_only[] = {"game", "--brewing", "on"};
        CHECK(parse(&c, 3, brewing_only, err) == 0 &&
              gm_config_validate_runtime(&c, 0, 0, err, sizeof err) == 0,
              "brewing bundle is runnable");
    }
    {
        char *enchanting_only[] = {"game", "--enchanting", "on"};
        CHECK(parse(&c, 3, enchanting_only, err) == 0 &&
              gm_config_validate_runtime(&c, 0, 0, err, sizeof err) == 0,
              "enchanting bundle is runnable");
    }
    {
        char *weather_only[] = {"game", "--weather", "on"};
        CHECK(parse(&c, 3, weather_only, err) == 0 &&
              gm_config_validate_runtime(&c, 0, 0, err, sizeof err) == 0,
              "weather bundle is runnable");
    }

    char *headless_ok[] = {"game", "--headless", "--ticks", "1",
                           "--render", "off", "--pace", "unlimited"};
    CHECK(parse(&c, (int)(sizeof headless_ok / sizeof headless_ok[0]), headless_ok, err) == 0 &&
          gm_config_validate_runtime(&c, 0, 0, err, sizeof err) == 0,
          "headless render-off unlimited profile is runnable");
    char *frames_wired[]={"game","--headless","--ticks","1","--frames-out","frames"};
    CHECK(parse(&c,6,frames_wired,err)==0&&gm_config_validate_runtime(&c,0,0,err,sizeof err)==0,
          "scripted CPU frame capture is wired");

    c.villages = c.enchanting = c.brewing = c.weather = 0;
    c.frames_out_dir = NULL;
    c.render = GM_RENDER_WINDOW;
    c.backend = GM_BACKEND_CPU;
    c.pace = GM_PACE_REALTIME;
    CHECK(gm_config_validate_runtime(&c, 0, 0, err, sizeof err) == 0,
          "superflat arena is runnable with optional bundles off");

    char *dup[] = {"game", "--width", "640", "--w", "800"};
    CHECK(parse(&c, 5, dup, err) == 2 && strstr(err, "duplicate"),
          "alias duplicate rejected");
    char *dup_headless[] = {"game", "--headless", "--headless", "--ticks", "1"};
    CHECK(parse(&c, 5, dup_headless, err) == 2 && strstr(err, "duplicate"),
          "duplicate headless rejected");
    char *dup_ticks[] = {"game", "--ticks", "1", "--ticks", "2"};
    CHECK(parse(&c, 5, dup_ticks, err) == 2 && strstr(err, "duplicate"),
          "duplicate ticks rejected");
    char *dup_script[] = {"game", "--script", "a", "--script", "b"};
    CHECK(parse(&c, 5, dup_script, err) == 2 && strstr(err, "duplicate"),
          "duplicate script rejected");
    char *dup_state[] = {"game", "--state-out", "a", "--state-out", "b"};
    CHECK(parse(&c, 5, dup_state, err) == 2 && strstr(err, "duplicate"),
          "duplicate state output rejected");
    char *dup_frames_out[] = {"game", "--frames-out", "a", "--frames-out", "b"};
    CHECK(parse(&c, 5, dup_frames_out, err) == 2 && strstr(err, "duplicate"),
          "duplicate frames output rejected");
    char *dup_compose[] = {"game", "--compose", "capture", "--compose", "window"};
    CHECK(parse(&c, 5, dup_compose, err) == 2 && strstr(err, "duplicate"),
          "duplicate compose mode rejected");
    char *bad_compose[] = {"game", "--compose", "shared"};
    CHECK(parse(&c, 3, bad_compose, err) == 2 && strstr(err, "invalid"),
          "invalid compose mode rejected");
    char *bad_bool[] = {"game", "--weather", "yes"};
    CHECK(parse(&c, 3, bad_bool, err) == 2, "noncanonical boolean rejected");
    char *missing[] = {"game", "--seed"};
    CHECK(parse(&c, 2, missing, err) == 2 && strstr(err, "missing"),
          "missing value rejected");
    char *missing_ticks[] = {"game", "--ticks"};
    CHECK(parse(&c, 2, missing_ticks, err) == 2 && strstr(err, "missing"),
          "missing ticks value rejected");
    char *missing_script[] = {"game", "--script"};
    CHECK(parse(&c, 2, missing_script, err) == 2 && strstr(err, "missing"),
          "missing script value rejected");
    char *missing_state[] = {"game", "--state-out"};
    CHECK(parse(&c, 2, missing_state, err) == 2 && strstr(err, "missing"),
          "missing state output value rejected");
    char *missing_frames_out[] = {"game", "--frames-out"};
    CHECK(parse(&c, 2, missing_frames_out, err) == 2 && strstr(err, "missing"),
          "missing frames output value rejected");
    char *empty_script[] = {"game", "--script", ""};
    CHECK(parse(&c, 3, empty_script, err) == 2 && strstr(err, "empty"),
          "empty script path rejected");
    char *empty_state[] = {"game", "--state-out", ""};
    CHECK(parse(&c, 3, empty_state, err) == 2 && strstr(err, "empty"),
          "empty state output path rejected");
    char *empty_frames_out[] = {"game", "--frames-out", ""};
    CHECK(parse(&c, 3, empty_frames_out, err) == 2 && strstr(err, "empty"),
          "empty frames output path rejected");
    char *empty_ticks[] = {"game", "--ticks", ""};
    CHECK(parse(&c, 3, empty_ticks, err) == 2, "empty ticks value rejected");
    char *bad_ticks[] = {"game", "--ticks", "-1"};
    CHECK(parse(&c, 3, bad_ticks, err) == 2, "negative ticks rejected");
    char *headless_no_ticks[] = {"game", "--headless"};
    CHECK(parse(&c, 2, headless_no_ticks, err) == 2 && strstr(err, "requires"),
          "headless without ticks rejected");
    char *ticks_and_frames[] = {"game", "--ticks", "1", "--frames", "1"};
    CHECK(parse(&c, 5, ticks_and_frames, err) == 2 && strstr(err, "--frames"),
          "ticks with legacy frames rejected");
    char *creative[] = {"game", "--creative"};
    CHECK(parse(&c, 2, creative, err) == 2 && strstr(err, "unknown"),
          "creative is absent rather than disabled");
    char *view[] = {"game", "--view-distance", "9"};
    CHECK(parse(&c, 3, view, err) == 2, "view distance outside compiled cap rejected");

    if (fails) return 1;
    puts("config: PASS");
    return 0;
}
