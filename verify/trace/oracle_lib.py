"""oracle_lib.py - shared helpers for the trace/replay verification flywheel.

Talks to the REAL Java Minecraft 1.11.2 client through the qrl bridge
(127.0.0.1:25575, java/qrl_client.py). Every helper here is additive glue over
the hard-won prior art in verify/mc_capture/ (capture.sh /
capture_at_poses.sh): socket-probe the bridge (never grep runclient.log),
freeze the scene (clear noon, no cycles), pin poses by re-asserting tp every
tick, and grab frames with the qrl "frame" command (an in-process framebuffer
read -> PNG; no x11grab window-geometry fragility, exact 854x480).

Conventions (pixel-verified, see magma/game/view.h):
  - qrl obs / MC tp use FEET coords and MC yaw/pitch (yaw 180 faces -Z,
    positive pitch looks DOWN). magma's script set_pose takes the SAME
    convention (game/script.c -> gm_runtime_set_pose), so poses pass through
    unchanged. The camera eye sits 1.62 above feet on both sides.
"""
import os
import socket
import subprocess
import sys
import time

# repo-root/java derived from this file's location (works on mac + anvil);
# override with MC_JAVA_DIR if the checkout layout ever differs.
_REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
JAVA_DIR = os.environ.get("MC_JAVA_DIR", os.path.join(_REPO, "java"))
if JAVA_DIR not in sys.path:
    sys.path.insert(0, JAVA_DIR)

import qrl_client  # noqa: E402

EYE_HEIGHT = 1.62
FOV = 70


def bridge_up(timeout=1.0):
    try:
        s = socket.create_connection(("127.0.0.1", 25575), timeout=timeout)
        s.close()
        return True
    except OSError:
        return False


def wait_bridge(max_s=420):
    """Poll the real socket until the qrl bridge accepts a connection."""
    for i in range(max_s):
        if bridge_up():
            return i
        time.sleep(1)
    raise TimeoutError(
        f"qrl bridge never accepted a connection within {max_s}s; "
        f"check {JAVA_DIR}/runclient.log")


def connect():
    return qrl_client.NetheriteEnv()


def ensure_world(env, seed=0, mode="survival"):
    """reset() auto-launches the world if none is loaded; polls until ready."""
    o = env.reset({"seed": seed, "mode": mode, "type": "default"})
    if not o.get("ok"):
        raise RuntimeError(f"reset not ok: {o}")
    return o


def runcmds(env, cmds):
    return env._cmd({"cmd": "runcmds", "action": {"cmds": list(cmds)}})


def freeze_scene(env, world_time=6000, gamemode="survival", mob_spawning=False):
    """Deterministic scene: silence feedback, freeze clear weather + clock."""
    cmds = [
        "gamerule sendCommandFeedback false",
        "gamerule logAdminCommands false",
        "gamerule doDaylightCycle false",
        "gamerule doWeatherCycle false",
        "gamerule doMobSpawning " + ("true" if mob_spawning else "false"),
        "gamerule doFireTick false",
        "gamerule randomTickSpeed 0",
        f"time set {world_time}",
        "weather clear 1000000",
        f"gamemode {gamemode} @a",
    ]
    r = runcmds(env, cmds)
    if r.get("failed"):
        raise RuntimeError(f"freeze_scene: some commands failed: {r}")
    return r


def hold_pose(env, x, y, z, yaw, pitch, settle_ticks=30, final_sleep=0.6):
    """Teleport and PIN the pose: re-assert tp each settle tick so chunks build
    while survival gravity cannot drift the player, then a final re-assert and a
    short sleep so the teleport renders before any frame grab."""
    tp = f"tp @a {x:.6f} {y:.6f} {z:.6f} {yaw:.4f} {pitch:.4f}"
    for _ in range(settle_ticks):
        env.step({})
        runcmds(env, [tp])
        time.sleep(0.02)
    runcmds(env, [tp])
    time.sleep(final_sleep)
    runcmds(env, [tp])
    time.sleep(0.3)
    return env.obs()


def grab_frame(env, path):
    """In-process framebuffer -> PNG on the game side (exact display WxH)."""
    r = env._cmd({"cmd": "frame", "action": {"file": path}})
    if not r.get("ok"):
        raise RuntimeError(f"frame grab failed: {r}")
    return r


def grab_stable_frame(env, path, pin_cmds=None, max_wait_s=90, stable_thresh=0.2,
                      consecutive=3, min_elapsed_s=8.0, interval_s=1.5):
    """Grab a frame, but WAIT OUT chunk streaming: llvmpipe + a fresh teleport can
    take tens of seconds to build/render surrounding chunk meshes, builds can
    STALL for over a second and then resume, and a too-early grab silently
    returns a frame full of sky where terrain belongs. Grab repeatedly
    (re-asserting the pose via pin_cmds between grabs, stepping a tick so the
    client keeps ticking) until `consecutive` consecutive grab pairs differ by
    < stable_thresh mean abs/channel AND at least min_elapsed_s passed."""
    import numpy as np
    from PIL import Image
    tmp = path + ".probe.png"
    prev = None
    stable = 0
    t0 = time.time()
    while True:
        if pin_cmds:
            runcmds(env, pin_cmds)
        env.step({})
        grab_frame(env, tmp)
        cur = np.asarray(Image.open(tmp).convert("RGB")).astype(np.int16)
        if prev is not None:
            d = float(np.abs(cur - prev).mean())
            stable = stable + 1 if d < stable_thresh else 0
            if stable >= consecutive and time.time() - t0 >= min_elapsed_s:
                os.replace(tmp, path)
                return d
        prev = cur
        if time.time() - t0 > max_wait_s:
            os.replace(tmp, path)
            sys.stderr.write(f"[oracle_lib] WARN: frame never stabilized within "
                             f"{max_wait_s}s ({path}); keeping the last grab\n")
            return -1.0
        time.sleep(interval_s)


def get_entities(env):
    """Full loaded-entity list (server thread, atomically with num_ticks).
    Doubles are transported as raw bit patterns; decode here."""
    import struct
    r = env._cmd({"cmd": "getentities", "action": {}})
    if not r.get("ok"):
        raise RuntimeError(f"getentities failed: {r}")

    def d(bits):
        return struct.unpack("<d", struct.pack("<q", int(bits)))[0]

    def f(bits):
        return struct.unpack("<f", struct.pack("<i", int(bits)))[0]

    ents = []
    for e in r.get("ents", []):
        ents.append({
            "eid": e["eid"], "type": e["type"],
            "x": d(e["x"]), "y": d(e["y"]), "z": d(e["z"]),
            "health": f(e["health"]),
        })
    out = {"num_ticks": r.get("num_ticks"), "ents": ents}
    if "player" in r:
        p = r["player"]
        out["player"] = {"x": d(p["x"]), "y": d(p["y"]), "z": d(p["z"])}
    return out


def magma_root():
    """verify/trace/oracle_lib.py -> magma/ (works in any worktree)."""
    return os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", "magma"))


def repo_root():
    return os.path.abspath(os.path.join(magma_root(), ".."))


def diff_frame_py():
    return os.path.join(repo_root(), "java", "render-opt", "wholeframe", "diff_frame.py")


def run_magma_script(script_path, ticks, frames_dir, state_out, w=854, h=480,
                       seed=0, extra_env=None, timeout=600,
                       frame_every=1, frame_offset=0, mobs=True,
                       backend="cpu", daylight=True, world="default",
                       compose="capture", set_kv=None):
    """Run the magma game headlessly on a JSONL event script.

    backend="cuda" uses the magma_game_cuda binary (raster stage on the GPU,
    bit-exact vs CPU per make test-raster-parity; sim stays host-side). It runs
    on GPU1 (the 3090) unless CUDA_VISIBLE_DEVICES is already set - GPU0 is the
    shared big card and stays free per the repo GPU policy.

    backend="metal" uses the magma_game_metal binary (macOS / Apple silicon,
    make -C magma game-metal). Same plumbing as cuda; parity status lives in
    magma/VERIFY.md "Metal backend (macOS)" - UNVERIFIED until
    scripts/mac_metal_verify.sh passes on the MacBook.

    set_kv: list of "key=value" strings appended as repeated --set flags
    (config registry overrides; applied after magma.conf). Prefer this over
    extra_env for knobs that have left getenv.
    """
    croot = magma_root()
    binary = {"cuda": "magma_game_cuda",
              "metal": "magma_game_metal"}.get(backend, "magma_game")
    game = os.path.join(croot, binary)
    if not os.path.exists(game):
        target = {"cuda": "game-cuda", "metal": "game-metal"}.get(backend, "game")
        r = subprocess.run(["make", "-C", croot, target], capture_output=True)
        if r.returncode != 0:
            sys.stderr.write(r.stderr.decode(errors="replace"))
            raise RuntimeError(f"{binary} build failed")
    env = dict(os.environ)
    if backend == "cuda":
        env.setdefault("CUDA_VISIBLE_DEVICES", "1")
    if extra_env:
        env.update(extra_env)
    cmd = [game, "--headless", "--world", world, "--seed", str(seed),
           "--ticks", str(ticks), "--width", str(w), "--height", str(h),
           "--script", script_path, "--state-out", state_out]
    if compose != "capture":
        cmd += ["--compose", compose]
    if backend in ("cuda", "metal"):
        cmd += ["--backend", backend]
    if not mobs:
        cmd += ["--mobs", "off"]
    if not daylight:
        cmd += ["--daylight", "off"]
    if frames_dir:
        cmd += ["--frames-out", frames_dir]
        if frame_every > 1:
            cmd += ["--frame-every", str(frame_every),
                    "--frame-offset", str(frame_offset)]
    if set_kv:
        for kv in set_kv:
            cmd += ["--set", kv]
    r = subprocess.run(cmd, cwd=croot, env=env, capture_output=True, timeout=timeout)
    if r.returncode != 0:
        sys.stderr.write(r.stdout.decode(errors="replace"))
        sys.stderr.write(r.stderr.decode(errors="replace"))
        raise RuntimeError(f"magma_game failed (rc={r.returncode})")
    return r


def _diff_stats(a, b, thr=0):
    """Same math as render-opt/wholeframe/diff_frame.py stats() at --thr 0."""
    import numpy as np
    d = np.abs(a - b)
    per_pixel = d.max(axis=2)
    total = int(per_pixel.size)
    differ = int((per_pixel > thr).sum())
    return {
        "max_abs_per_channel": int(d.max()),
        "mean_abs": float(d.mean()),
        "rmse": float(np.sqrt((d.astype(np.float64) ** 2).mean())),
        "differing_pixels": differ,
        "total_pixels": total,
        "pct_differing": 100.0 * differ / total,
    }


def diff_regions_arrays(a, b, w, h):
    """whole | terrain crop | hud strip stats over two (h,w,3) int16 arrays."""
    r0, r1, c0, c1 = int(h * 0.14), int(h * 0.86), int(w * 0.09), int(w * 0.91)
    return {
        "whole": _diff_stats(a, b),
        "terrain": _diff_stats(a[r0:r1, c0:c1], b[r0:r1, c0:c1]),
        "hud": _diff_stats(a[int(h * 0.90):h], b[int(h * 0.90):h]),
    }


def diff_regions(golden_png, cand_png, w, h):
    """whole | terrain crop | hud strip (frame_oracle.py's region split),
    bit-identical to diff_frame.py's stats but in-process: ONE decode per
    image instead of 3 subprocess launches x 2 decodes per frame pair
    (that was ~95% of replay_tape's pixel-diff wall time).
    Returns {region: {mean_abs, max_abs_per_channel, rmse, pct_differing, ...}}."""
    import numpy as np
    from PIL import Image
    a = np.asarray(Image.open(golden_png).convert("RGB")).astype(np.int16)
    b = np.asarray(Image.open(cand_png).convert("RGB")).astype(np.int16)
    return diff_regions_arrays(a, b, w, h)


def read_ppm(path):
    """Binary P6 PPM -> (h,w,3) uint8. magma's --frames-out format; reading
    it directly skips a PNG encode (C) + decode (PIL) per frame."""
    import numpy as np
    import re
    with open(path, "rb") as f:
        raw = f.read()
    # exactly ONE whitespace char after maxval, then binary pixels (which may
    # themselves be whitespace-valued bytes - never str.split the body)
    m = re.match(rb"P6\s+(\d+)\s+(\d+)\s+(\d+)\s", raw)
    if not m:
        raise ValueError(f"{path}: not a binary P6 PPM")
    w, h, maxv = int(m.group(1)), int(m.group(2)), int(m.group(3))
    if maxv != 255:
        raise ValueError(f"{path}: maxval {maxv} unsupported")
    px = np.frombuffer(raw, dtype=np.uint8,
                       count=w * h * 3, offset=m.end())
    return px.reshape(h, w, 3)


def relocate_golden(png, tape_path):
    """Resolve a tape's baked golden path after the tape dir moved.

    The recorder bakes an ABSOLUTE frame path into every tick row, so moving a
    tape (e.g. tapes/X.jsonl -> tapes/retired/X.jsonl) orphans every golden.
    Fall back to <dir of the tape file>/<frames dir name>/<png name>, which is
    the layout the recorder always writes. Returns None when neither exists."""
    if os.path.exists(png):
        return png
    if not tape_path:
        return None
    alt = os.path.join(os.path.dirname(os.path.abspath(tape_path)),
                       os.path.basename(os.path.dirname(png)),
                       os.path.basename(png))
    return alt if os.path.exists(alt) else None


def oracle_frames_cache(frame_ticks, w, h, tape_path=None):
    """Decode the tape's oracle PNG frames ONCE into a sidecar npy stack.

    frame_ticks: [(tick, png_path)] from the tape. Returns (ticks, frames,
    skipped, missing) where frames is a (n,h,w,3) uint8 memmap-backed array
    aligned with ticks, skipped counts frames not at (w,h) (mid-session window
    resize), and missing counts goldens that exist at neither the baked path
    nor the tape-relative one. Tapes are immutable, so the cache lives next to
    the frames dir (<dir>/frames_<w>x<h>.npy + .ticks.npy) and is rebuilt only
    if the tick set changed."""
    import numpy as np
    if not frame_ticks:
        return [], None, 0, 0
    paths = [(t, relocate_golden(p, tape_path)) for t, p in frame_ticks]
    missing = sum(1 for _, p in paths if p is None)
    found = [(t, p) for t, p in paths if p is not None]
    if not found:
        return [], None, 0, missing
    d = os.path.dirname(found[0][1])
    base = os.path.join(d, f"frames_{w}x{h}")
    npy, tnpy = base + ".npy", base + ".ticks.npy"
    want = [t for t, _ in frame_ticks]
    if os.path.exists(npy) and os.path.exists(tnpy):
        ticks = np.load(tnpy).tolist()
        if set(ticks) <= set(want):
            return (ticks, np.load(npy, mmap_mode="r"),
                    len(want) - len(ticks) - missing, missing)
    from PIL import Image
    ticks, imgs, skipped = [], [], 0
    for t, p in found:
        im = Image.open(p)
        if im.size != (w, h):
            skipped += 1
            continue
        ticks.append(t)
        imgs.append(np.asarray(im.convert("RGB"), dtype=np.uint8))
    frames = np.stack(imgs) if imgs else None
    if frames is not None:
        np.save(npy, frames)
        np.save(tnpy, np.asarray(ticks))
    return ticks, frames, skipped, missing


def rgb_to_png(arr, png):
    from PIL import Image
    Image.fromarray(arr, "RGB").save(png)


def ppm_to_png(ppm, png):
    from PIL import Image
    Image.open(ppm).convert("RGB").save(png)


def side_by_side(mc_png, magma_png, out_png, scale=0.5):
    """Small side-by-side (MC | magma) for the committed report."""
    from PIL import Image
    a = Image.open(mc_png).convert("RGB")
    b = Image.open(magma_png).convert("RGB")
    w, h = a.size
    sw, sh = int(w * scale), int(h * scale)
    a = a.resize((sw, sh), Image.LANCZOS)
    b = b.resize((sw, sh), Image.LANCZOS)
    canvas = Image.new("RGB", (sw * 2 + 4, sh), (24, 24, 24))
    canvas.paste(a, (0, 0))
    canvas.paste(b, (sw + 4, 0))
    canvas.save(out_png, optimize=True)


def heatmap(mc_png, magma_png, out_png, scale=0.5):
    import numpy as np
    from PIL import Image
    a = np.asarray(Image.open(mc_png).convert("RGB")).astype(np.int16)
    b = np.asarray(Image.open(magma_png).convert("RGB")).astype(np.int16)
    d = np.abs(a - b).max(axis=2).astype(np.float64)
    mx = d.max()
    scaled = (d / mx * 255.0).astype(np.uint8) if mx > 0 else d.astype(np.uint8)
    im = Image.fromarray(scaled)
    if scale != 1.0:
        im = im.resize((int(im.width * scale), int(im.height * scale)), Image.LANCZOS)
    im.save(out_png, optimize=True)
