"""Structural pixel gate: connected-component classification of the
oracle-vs-magma frame diff.

Policy (VERIFY.md / sky-tolerance): per-pixel noise is tolerated, CLUSTERS are
not. A whole-frame mean hides a 10k-px marker box behind sky dither; this
module labels each diff cluster, classifies it against the accepted divergence
classes in OPEN_DIVERGENCES.md, and fails the run on anything unexplained.

Classes (conservative predicates, keyed to OPEN_DIVERGENCES numbers):
  bossbar   #45/#50  cluster entirely inside the top boss-bar band
  hud       #44/...  cluster entirely inside the bottom HUD strip
  thinline  #4       wireframe / silhouette-edge class: large bbox, tiny fill
  particles #40/#48  oracle-only brightness (additive particles magma
                     doesn't draw); magma-brighter clusters NEVER match
                     this class - that's how the marker-box bug type is caught
  viewmodel #29      held-item region: lower-right, touching a frame edge
  UNEXPLAINED        everything else -> gate failure when big enough

Verdict: a frame FAILS on any UNEXPLAINED cluster >= FAIL_CLUSTER px (or an
unexplained total >= FAIL_TOTAL). The tape PASSES when no frame fails.
Per-class totals are written to a .gate.json baseline for suite diffing.
"""
import numpy as np

DIFF_THRESH = 25      # max-channel abs diff for a pixel to enter the mask
MIN_CLUSTER = 50      # px; smaller components are per-pixel noise, dropped
# Fail thresholds are calibrated to "egregious": the ambient fidelity level
# (entity silhouettes offset a pixel, cloud edges) runs ~1k unexplained px per
# frame with clusters to ~2k; the finding-class bugs this gate exists to catch
# (marker box ~10k, water fog ~60k, arena window ~147k) sit well above.
FAIL_CLUSTER = 4000   # px; an UNEXPLAINED component this big fails the frame
FAIL_TOTAL = 8000     # px; total UNEXPLAINED in one frame that fails it
BOSSBAR_Y = 44        # top band (boss bar + name text) at 480p scale 2
HUD_FRAC = 0.80       # bottom HUD strip starts here (hotbar top ~y=436/480)


def _classify(o, c, ys, xs, w, h):
    """One labeled component (pixel coords ys/xs) -> class string."""
    y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    area = len(ys)
    bbox_area = (y1 - y0 + 1) * (x1 - x0 + 1)
    if y1 <= BOSSBAR_Y:
        return "bossbar"
    if y0 >= int(h * HUD_FRAC):
        return "hud"
    if bbox_area >= 400 and area / bbox_area < 0.12:
        return "thinline"
    ob = o[ys, xs].mean()
    cb = c[ys, xs].mean()
    if ob > cb + 12.0:
        return "particles"          # oracle-only additive glow
    if (x0 > w * 0.52 and y0 > h * 0.40
            and (x1 >= w - 3 or y1 >= h - 3)):
        return "viewmodel"
    return "UNEXPLAINED"


def gate_frame(o16, c16, w, h):
    """Diff one frame pair (int16 (h,w,3) arrays) -> list of cluster dicts.
    Only components >= MIN_CLUSTER are returned."""
    from scipy import ndimage
    d = np.abs(o16 - c16).max(axis=2)
    mask = d > DIFF_THRESH
    if not mask.any():
        return []
    lab, n = ndimage.label(mask, structure=np.ones((3, 3), dtype=int))
    if n == 0:
        return []
    sizes = np.bincount(lab.ravel())
    out = []
    ob = o16.sum(axis=2)   # brightness proxies for the classifier
    cb = c16.sum(axis=2)
    for i in range(1, n + 1):
        if sizes[i] < MIN_CLUSTER:
            continue
        ys, xs = np.nonzero(lab == i)
        cls = _classify(ob, cb, ys, xs, w, h)
        out.append({"px": int(sizes[i]), "cls": cls,
                    "bbox": [int(ys.min()), int(xs.min()),
                             int(ys.max()), int(xs.max())]})
    return out


def frame_verdict(clusters):
    """(fails, unexplained_px) for one frame's cluster list."""
    unex = [cl for cl in clusters if cl["cls"] == "UNEXPLAINED"]
    total = sum(cl["px"] for cl in unex)
    big = max((cl["px"] for cl in unex), default=0)
    return (big >= FAIL_CLUSTER or total >= FAIL_TOTAL), total


def transit_ticks(rows, pad=40):
    """Ticks within `pad` of a dimension change or a loading-screen row.
    Around a transfer the oracle renders GuiDownloadTerrain / the destination
    a few frames apart from magma (chunk-stream timing is not simulated);
    whole-frame diffs there are a filed artifact class, not render bugs."""
    marked = set()
    prev_dim = None
    prev_pos = None
    for r in rows:
        t = r.get("t")
        if t is None:
            continue
        dim = r.get("dim")
        if r.get("loading") or (prev_dim is not None and dim != prev_dim):
            marked.update(range(t - pad, t + pad + 1))
        # teleports (pearl / tp staging): the recorded one-tick position jump
        # replays with a self-healing sub-block offset (VERIFY.md artifact);
        # the parallax shift diffs every high-contrast pixel for a few frames.
        pos = (r.get("x"), r.get("y"), r.get("z"))
        if (prev_pos is not None and prev_dim == dim
                and None not in pos and None not in prev_pos):
            d2 = sum((a - b) ** 2 for a, b in zip(pos, prev_pos))
            if d2 > 9.0:
                marked.update(range(t - pad, t + pad + 1))
        prev_pos = pos
        prev_dim = dim
    return marked


def summarize(per_tick, transit=None):
    """per_tick: {tick: [cluster,...]} -> gate summary dict (json-able).
    Clusters on ticks in `transit` are reclassified to the transit class."""
    if transit:
        for t, cls_list in per_tick.items():
            if t in transit:
                for cl in cls_list:
                    cl["cls"] = "transit"
    classes = {}
    failed = []
    for t, cls_list in sorted(per_tick.items()):
        for cl in cls_list:
            s = classes.setdefault(cl["cls"], {"frames": 0, "px": 0,
                                               "max_cluster": 0, "ticks": set()})
            if t not in s["ticks"]:
                s["frames"] += 1
                s["ticks"].add(t)
            s["px"] += cl["px"]
            s["max_cluster"] = max(s["max_cluster"], cl["px"])
        fails, total = frame_verdict(cls_list)
        if fails:
            failed.append({"tick": t, "unexplained_px": total,
                           "clusters": [cl for cl in cls_list
                                        if cl["cls"] == "UNEXPLAINED"]})
    for s in classes.values():
        s.pop("ticks")
    failed.sort(key=lambda r: -r["unexplained_px"])
    return {"thresholds": {"diff": DIFF_THRESH, "min_cluster": MIN_CLUSTER,
                           "fail_cluster": FAIL_CLUSTER,
                           "fail_total": FAIL_TOTAL},
            "frames_checked": len(per_tick),
            "classes": classes,
            "failed_frames": failed,
            "pass": not failed}
