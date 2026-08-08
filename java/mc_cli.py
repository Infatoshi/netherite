#!/usr/bin/env python
"""mc_cli.py - config-driven launcher for mc-1.11.2-env instances.

    uv run --no-project --with pyyaml python mc_cli.py [options]

Reads a profile yaml (single source of truth for one instance), then:
  1. patches Minecraft/run/options.txt with the [video]/[ui] settings,
  2. writes Minecraft/run/qrl_launch.json ([world]/[ui]/[port]/[genprobe], read by
     NetheriteMod (mod id qrl): main menu is skipped, world auto-launches, chat hidden, gamerules applied),
  3. launches runClient (headless on DISPLAY, or --vnc for the Xvfb+x11vnc stack).

No invented env vars: everything the mod reads arrives through qrl_launch.json or a
gradle property (-PmcUsername, -PqrlPort).

Options:
  --config PATH      profile yaml (default: fast.yaml next to this script).
                     fast.yaml = agent/trace stack (harness chrome stripped,
                     clock/weather frozen, gameplay intact). vanilla.yaml =
                     human play over Moonlight (sunshine_launch_mc.sh applies
                     it automatically; full vanilla rules, sound, menus).
  --set K=V          dotted override, repeatable (e.g. --set world.seed=42)
  --instances N      batch: N clients, port/display auto-incremented per instance.
                     NOTE: each instance needs a distinct world folder; NetheriteMod
                     names folders qrl_<seed>[_flat], so give each instance its own
                     seed (--set world.seed=...) or launches 2..N will fail the
                     world lock. Gradle builds are serialized before spawning.
  --vnc              launch via start_vnc_client.sh (Xvfb + x11vnc + client)
  --no-launch        write config artifacts only, do not start the game
  --dry-run          print what would be written/run, change nothing
"""
import argparse, json, os, shlex, subprocess, sys

import yaml

ROOT = os.path.dirname(os.path.abspath(__file__))
RUN = os.path.join(ROOT, "Minecraft", "run")

PARTICLES = {"all": 0, "decreased": 1, "minimal": 2}
SOUND_CATS = (
    "master", "music", "record", "weather", "block",
    "hostile", "neutral", "player", "ambient", "voice",
)


def deep_set(cfg, dotted, raw):
    keys = dotted.split(".")
    node = cfg
    for k in keys[:-1]:
        node = node.setdefault(k, {})
    node[keys[-1]] = yaml.safe_load(raw)  # yaml-parse the value: ints/bools/strings


def _bool_txt(v):
    return "true" if v else "false"


def options_patch(video, ui):
    """options.txt keys <- config (GameSettings.saveOptions encodings).

    Perf-optimized defaults are enforced here even if omitted from yaml, so a
    partial config cannot re-enable fancy leaves / AO / shadows by accident.
    """
    kv = {}
    if "fov" in video:
        # GameSettings stores fov as (degrees-70)/40  → 70deg == 0.0
        kv["fov"] = repr((float(video["fov"]) - 70.0) / 40.0)
    if "max_fps" in video:
        kv["maxFps"] = str(int(video["max_fps"]))
    if "render_distance" in video:
        kv["renderDistance"] = str(int(video["render_distance"]))
    if "gui_scale" in video:
        kv["guiScale"] = str(int(video["gui_scale"]))
    if "particles" in video:
        kv["particles"] = str(PARTICLES[str(video["particles"])])
    if "smooth_lighting" in video:
        kv["ao"] = str(int(video["smooth_lighting"]))
    if "mipmap_levels" in video:
        kv["mipmapLevels"] = str(int(video["mipmap_levels"]))
    if "vsync" in video:
        kv["enableVsync"] = _bool_txt(video["vsync"])
    if "clouds" in video:
        c = video["clouds"]
        kv["renderClouds"] = "fast" if c == "fast" else ("true" if c is True else "false")
    if "gamma" in video:
        # stored raw 0..1 (not the 0..1 slider mapped oddly — gammaSetting is the value)
        kv["gamma"] = repr(float(video["gamma"]))
    if "fancy_graphics" in video:
        kv["fancyGraphics"] = _bool_txt(video["fancy_graphics"])
    if "entity_shadows" in video:
        kv["entityShadows"] = _bool_txt(video["entity_shadows"])
    if "bob_view" in video:
        kv["bobView"] = _bool_txt(video["bob_view"])
    if "anaglyph" in video:
        kv["anaglyph3d"] = _bool_txt(video["anaglyph"])
    if "use_vbo" in video:
        kv["useVbo"] = _bool_txt(video["use_vbo"])
    if "fbo_enable" in video:
        kv["fboEnable"] = _bool_txt(video["fbo_enable"])
    if "sound" in video:
        kv["soundCategory_master"] = repr(float(video["sound"]))
    if "mute_all_sounds" in video:
        # explicit both ways: profiles share one options.txt, so an unmute must
        # overwrite the 0.0s a muted profile left behind
        vol = "0.0" if video["mute_all_sounds"] else "1.0"
        for cat in SOUND_CATS:
            kv[f"soundCategory_{cat}"] = vol

    # --- hard pins (perf + determinism; always written) ---
    kv.setdefault("fancyGraphics", "false")
    kv.setdefault("entityShadows", "false")
    kv.setdefault("ao", "0")
    kv.setdefault("mipmapLevels", "0")
    kv.setdefault("particles", "2")          # minimal
    kv.setdefault("renderClouds", "false")
    kv.setdefault("enableVsync", "false")
    kv.setdefault("bobView", "false")
    kv.setdefault("anaglyph3d", "false")
    kv.setdefault("useVbo", "true")
    kv.setdefault("gamma", "0.0")
    kv.setdefault("soundCategory_master", "0.0")

    # chat visibility lives in options.txt too; the mod also forces it at runtime
    kv["chatVisibility"] = "0" if ui.get("chat", True) else "2"
    kv["pauseOnLostFocus"] = "false"
    # always off for a headless/controlled instance: Realms poller + telemetry
    kv["realmsNotifications"] = "false"
    kv["snooperEnabled"] = "false"
    kv["heldItemTooltips"] = "false"
    kv["showInventoryAchievementHint"] = "false"
    kv["autoJump"] = "false"
    kv["attackIndicator"] = "0"
    return kv


def patch_options_txt(kv, dry):
    path = os.path.join(RUN, "options.txt")
    lines = []
    if os.path.exists(path):
        with open(path) as f:
            lines = f.read().splitlines()
    seen = set()
    out = []
    for line in lines:
        key = line.split(":", 1)[0]
        if key in kv:
            out.append(f"{key}:{kv[key]}")
            seen.add(key)
        else:
            out.append(line)
    for key, val in kv.items():
        if key not in seen:
            out.append(f"{key}:{val}")
    text = "\n".join(out) + "\n"
    if dry:
        print(f"-- would write {path}:\n{text}")
    else:
        os.makedirs(RUN, exist_ok=True)
        with open(path, "w") as f:
            f.write(text)
        print(f"wrote {path} ({len(kv)} keys)")


def write_launch_json(cfg, port, dry, profile):
    j = {
        "port": port,
        "profile": profile,  # provenance for tape naming (tape.py); mod ignores it
        "chat": bool(cfg.get("ui", {}).get("chat", True)),
        "hide_gui": bool(cfg.get("ui", {}).get("hide_gui", False)),
        "strip": {k: bool(v) for k, v in cfg.get("strip", {}).items()},
        "determinism": {k: bool(v) for k, v in cfg.get("determinism", {}).items()},
        "world": cfg.get("world", {}),
    }
    # worldgen RNG-cursor probe (netheritemod.WorldGenProbe): absent key = probe off. The
    # run/qrl_genprobe.txt sidecar remains the no-config-edit way to turn it on.
    if cfg.get("genprobe"):
        j["genprobe"] = str(cfg["genprobe"])
    # gamerule values must be strings for the command line
    gr = j["world"].get("gamerules")
    if gr:
        j["world"]["gamerules"] = {k: str(v).lower() for k, v in gr.items()}
    path = os.path.join(RUN, "qrl_launch.json")
    text = json.dumps(j, indent=2)
    if dry:
        print(f"-- would write {path}:\n{text}")
    else:
        os.makedirs(RUN, exist_ok=True)
        with open(path, "w") as f:
            f.write(text + "\n")
        print(f"wrote {path}")
    return path


def launch(cfg, args, port, display, idx):
    env = os.environ.copy()
    username = str(cfg.get("instance", {}).get("username", "Player0")) + (
        str(idx) if idx else "")
    # Config reaches the JVM as gradle properties, never env vars: -PmcUsername pins the
    # player name (build.gradle -> --username), -PqrlPort becomes -Dqrl.port for batch
    # instances 1..N-1 that share instance 0's run/qrl_launch.json "port".
    props = [f"-PmcUsername={username}"]
    if idx:
        props.append(f"-PqrlPort={port}")
    if args.vnc:
        # start_vnc_client.sh forwards its extra args verbatim to ./gradlew runClient
        cmd = ["bash", os.path.join(ROOT, "start_vnc_client.sh")] + props
        cwd = ROOT
    else:
        env["DISPLAY"] = display
        env.setdefault("LIBGL_ALWAYS_SOFTWARE", "1")
        env.setdefault("MESA_GL_VERSION_OVERRIDE", "2.1")
        cmd = ["./gradlew", "runClient"] + props
        cwd = os.path.join(ROOT, "Minecraft")
    if args.dry_run:
        print(f"-- would run (instance {idx}): NetheriteMod port {port} DISPLAY={display} "
              f"{shlex.join(cmd)}")
        return None
    # --vnc: start_vnc_client.sh writes runclient.log itself; keep our wrapper log separate
    name = "vnc_launch.log" if args.vnc else f"runclient{'' if idx == 0 else '_' + str(idx)}.log"
    log = os.path.join(ROOT, name)
    lf = open(log, "ab")
    p = subprocess.Popen(cmd, cwd=cwd, env=env, stdout=lf, stderr=lf,
                         start_new_session=True)
    print(f"instance {idx}: pid {p.pid}, NetheriteMod port {port}, display {display}, log {log}")
    return p


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--config", default=os.path.join(ROOT, "fast.yaml"))
    ap.add_argument("--set", action="append", default=[], metavar="K=V")
    ap.add_argument("--instances", type=int, default=1)
    ap.add_argument("--vnc", action="store_true")
    ap.add_argument("--no-launch", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    args = ap.parse_args()

    with open(args.config) as f:
        cfg = yaml.safe_load(f)
    for kv in args.set:
        k, _, v = kv.partition("=")
        deep_set(cfg, k, v)

    inst = cfg.get("instance", {})
    base_port = int(inst.get("port", 25575))
    base_disp = str(inst.get("display", ":1"))

    patch_options_txt(options_patch(cfg.get("video", {}), cfg.get("ui", {})), args.dry_run)
    write_launch_json(cfg, base_port, args.dry_run,
                      os.path.splitext(os.path.basename(args.config))[0])

    if args.no_launch:
        print("config written; not launching (--no-launch)")
        return

    if args.instances > 1 and args.vnc:
        sys.exit("--vnc supports a single instance (one Xvfb/x11vnc stack)")
    disp_num = int(base_disp.lstrip(":"))
    for i in range(args.instances):
        if args.instances > 1:
            # per-instance port/display; qrl_launch.json holds instance 0's port,
            # -PqrlPort (-> -Dqrl.port) overrides it for the rest (property wins in the mod)
            launch(cfg, args, base_port + i, f":{disp_num + i}", i)
        else:
            launch(cfg, args, base_port, base_disp, 0)


if __name__ == "__main__":
    main()
