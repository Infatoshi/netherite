#!/usr/bin/env python3
"""Drive qrl capture_translucent_draw on the slime_bounce pad.

Frames and vertex dumps come only from the live Java client. Never synthesize
PNG contents or quad buffers. Output:

  verify/fixtures/slime_translucent/
    camera.json, chunks.json, model_census.json, quads.jsonl
    coverage.json          post-transform fragment visit order (this script)
    slime_translucent_a.png / _b.png   same-pose A/B context frames
    capture_reply.json
"""
from __future__ import print_function

import argparse
import json
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "java"))
import qrl_client  # noqa: E402

PLAT_Y = 3
PAD = 10
POSE = {
    "x": 0.5,
    "y": float(PLAT_Y + 1),
    "z": 0.5,
    "yaw": 0.0,
    "pitch": 0.0,
    "no_gravity": True,
}


def log(msg):
    print("[slime_translucent] " + msg, flush=True)


def runcmds(e, cmds):
    r = e._cmd({"cmd": "runcmds", "action": {"cmds": cmds}})
    if not r.get("ok") or r.get("failed"):
        raise RuntimeError("runcmds failed: %s <- %s" % (r, cmds))
    return r


def settle(e, n):
    for _ in range(n):
        e.step({})


def mul4(m, v):
    x = m[0] * v[0] + m[4] * v[1] + m[8] * v[2] + m[12] * v[3]
    y = m[1] * v[0] + m[5] * v[1] + m[9] * v[2] + m[13] * v[3]
    z = m[2] * v[0] + m[6] * v[1] + m[10] * v[2] + m[14] * v[3]
    w = m[3] * v[0] + m[7] * v[1] + m[11] * v[2] + m[15] * v[3]
    return (x, y, z, w)


def project(mv, pr, x, y, z, width, height):
    eye = mul4(mv, (x, y, z, 1.0))
    clip = mul4(pr, eye)
    if abs(clip[3]) < 1e-8:
        return None
    ndc_x = clip[0] / clip[3]
    ndc_y = clip[1] / clip[3]
    if ndc_x < -1.2 or ndc_x > 1.2 or ndc_y < -1.2 or ndc_y > 1.2:
        return None
    sx = (ndc_x * 0.5 + 0.5) * width
    sy = (1.0 - (ndc_y * 0.5 + 0.5)) * height
    return (sx, sy)


def in_tri(px, py, a, b, c):
    ax, ay = a
    v0x, v0y = c[0] - ax, c[1] - ay
    v1x, v1y = b[0] - ax, b[1] - ay
    v2x, v2y = px - ax, py - ay
    den = v0x * v1y - v1x * v0y
    if abs(den) < 1e-12:
        return False
    u = (v2x * v1y - v1x * v2y) / den
    v = (v0x * v2y - v2x * v0y) / den
    return u >= -1e-6 and v >= -1e-6 and (u + v) <= 1.0 + 1e-6


def raster_coverage(outdir, camera, quads, width, height):
    mv = camera.get("modelview")
    pr = camera.get("projection")
    if not mv or not pr or len(mv) != 16 or len(pr) != 16:
        return {"ok": False, "error": "missing MVP"}
    # Count overlapping fragments on the slime-top ROI (lower half, full width).
    # Full-frame barycentric over 5k quads is fine at 854x480 if we clip AABBs.
    hits = {}
    n_proj = 0
    n_slime = 0
    for q in quads:
        verts = q.get("verts") or []
        if len(verts) != 4:
            continue
        pts = []
        ok = True
        for v in verts:
            p = project(mv, pr, v["x"], v["y"], v["z"], width, height)
            if p is None:
                ok = False
                break
            pts.append(p)
        if not ok:
            continue
        n_proj += 1
        if int(q.get("block_id") or 0) == 165:
            n_slime += 1
        xs = [p[0] for p in pts]
        ys = [p[1] for p in pts]
        x0 = max(0, int(min(xs)) - 1)
        x1 = min(width - 1, int(max(xs)) + 1)
        y0 = max(0, int(min(ys)) - 1)
        y1 = min(height - 1, int(max(ys)) + 1)
        if x1 < x0 or y1 < y0:
            continue
        t0 = (pts[0], pts[1], pts[2])
        t1 = (pts[0], pts[2], pts[3])
        draw_i = int(q.get("draw_i", -1))
        for y in range(y0, y1 + 1):
            py = y + 0.5
            for x in range(x0, x1 + 1):
                px = x + 0.5
                if in_tri(px, py, *t0) or in_tri(px, py, *t1):
                    hits.setdefault((x, y), []).append(draw_i)

    multi = []
    n_single = 0
    n_multi = 0
    n_dual_plus = 0
    for (x, y), seq in hits.items():
        if len(seq) == 1:
            n_single += 1
            continue
        n_multi += 1
        if len(seq) >= 2:
            n_dual_plus += 1
        if len(multi) < 200:
            multi.append({"x": x, "y": y, "n": len(seq), "draw_i": seq[:8]})

    # Rim question: slime top-face pixels whose first two covering quads come
    # from the same block (shell + core) vs a single quad.
    return {
        "ok": True,
        "n_quads": len(quads),
        "n_projected": n_proj,
        "n_slime_projected": n_slime,
        "n_covered_px": len(hits),
        "n_single": n_single,
        "n_multi": n_multi,
        "n_dual_plus": n_dual_plus,
        "multi_samples": multi,
        "width": width,
        "height": height,
    }


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--outdir", required=True)
    ap.add_argument("--seed", type=int, default=0)
    args = ap.parse_args()
    outdir = os.path.abspath(args.outdir)
    os.makedirs(outdir, exist_ok=True)

    e = qrl_client.NetheriteEnv()
    e.s.settimeout(180)
    log("reset flat seed=%d" % args.seed)
    o = e.reset(
        {
            "seed": args.seed,
            "mode": "survival",
            "type": "flat",
            "structures": False,
            "fresh": True,
        },
        timeout=300.0,
    )
    if not o.get("ok"):
        raise SystemExit("reset failed: %s" % o)

    last, still = None, 0
    for _ in range(200):
        obs = e.step({})
        pos = (obs.get("x"), obs.get("y"), obs.get("z"))
        still = still + 1 if pos == last else 0
        last = pos
        if still >= 10:
            break

    runcmds(e, [
        "/gamerule doDaylightCycle false",
        "/gamerule doWeatherCycle false",
        "/gamerule doMobSpawning false",
        "/gamerule randomTickSpeed 0",
        "/time set 6000",
        "/weather clear 1000000",
        "/difficulty peaceful",
        "/gamemode survival @p",
        "/tp @p 0.5 16 0.5 0 0",
    ])
    settle(e, 12)
    runcmds(e, [
        "/fill -%d %d -%d %d %d %d minecraft:slime" % (
            PAD, PLAT_Y, PAD, PAD, PLAT_Y, PAD),
        "/tp @p 0.5 %d 0.5 0 0" % (PLAT_Y + 1),
    ])
    settle(e, 16)
    pose = e._cmd({"cmd": "set_pose", "action": POSE})
    if not pose.get("ok"):
        raise SystemExit("set_pose failed: %s" % pose)
    settle(e, 8)
    rr = e._cmd({"cmd": "reload_renderers", "action": {}})
    log("reload_renderers: %s" % rr)
    settle(e, 8)

    dump = e._cmd({
        "cmd": "capture_translucent_draw",
        "action": {
            "dir": outdir,
            "x0": -PAD, "y0": PLAT_Y, "z0": -PAD,
            "x1": PAD, "y1": PLAT_Y, "z1": PAD,
        },
    }, read_deadline=180.0)
    log("capture_translucent_draw: %s" % dump)
    with open(os.path.join(outdir, "capture_reply.json"), "w") as f:
        json.dump(dump, f, indent=2)
    if not dump.get("ok"):
        raise SystemExit("capture_translucent_draw failed: %s" % dump)

    path_a = os.path.join(outdir, "slime_translucent_a.png")
    path_b = os.path.join(outdir, "slime_translucent_b.png")
    pair = e._cmd({
        "cmd": "frame_pair",
        "action": {"file_a": path_a, "file_b": path_b, "rerender": True},
    })
    log("frame_pair: ok=%s fog_restored=%s" % (
        pair.get("ok"), pair.get("fog_restored")))
    with open(os.path.join(outdir, "frame_pair.json"), "w") as f:
        json.dump(pair, f, indent=2)

    cam_path = os.path.join(outdir, "camera.json")
    quad_path = os.path.join(outdir, "quads.jsonl")
    camera = json.loads(open(cam_path).read())
    quads = []
    with open(quad_path) as f:
        for line in f:
            line = line.strip()
            if line:
                quads.append(json.loads(line))
    width = int(camera.get("display_w") or pair.get("w") or 854)
    height = int(camera.get("display_h") or pair.get("h") or 480)
    cov = raster_coverage(outdir, camera, quads, width, height)
    census_path = os.path.join(outdir, "model_census.json")
    census = json.loads(open(census_path).read()) if os.path.isfile(census_path) else {}
    cov["n_slime_blocks"] = census.get("n_slime")
    cov["n_general_quads"] = census.get("n_general_quads")
    cov["n_face_quads"] = census.get("n_face_quads")
    cov["n_translucent_quads"] = dump.get("n_translucent_quads")
    with open(os.path.join(outdir, "coverage.json"), "w") as f:
        json.dump(cov, f, indent=2)
    log("coverage n_multi=%s n_single=%s general=%s face=%s trans_quads=%s" % (
        cov.get("n_multi"), cov.get("n_single"),
        cov.get("n_general_quads"), cov.get("n_face_quads"),
        cov.get("n_translucent_quads")))
    e.close()
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as ex:
        log("FAIL: %s" % ex)
        raise
