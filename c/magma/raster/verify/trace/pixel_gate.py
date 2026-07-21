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
Tape-specific accepted recorder gaps live in an optional sibling
``<tape>.known_divergences.json``. With no sidecar, behavior is unchanged.
"""
import json
from pathlib import Path

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
VIEWMODEL_X_FRAC = 0.52
VIEWMODEL_Y_FRAC = 0.40


def load_known_divergences(tape_path):
    """Load an optional tape sibling sidecar; absent means no acceptances."""
    path = Path(tape_path).with_suffix(".known_divergences.json")
    if not path.exists():
        return []
    data = json.loads(path.read_text())
    if data.get("version") != 1 or not isinstance(data.get("divergences"), list):
        raise ValueError(f"invalid known-divergence sidecar: {path}")
    return data["divergences"]


def _active_known(known, tick):
    if tick is None:
        return []
    return [entry for entry in known or []
            if entry["ticks"][0] <= tick <= entry["ticks"][1]]


def _solid_box_mask(c16, mask):
    """Protect large, interior, flat rectangles from every acceptance.

    This is the marker-box regression invariant: even inside a globally
    accepted color shift, a new solid render primitive remains unexplained.
    """
    from scipy import ndimage

    protected = np.zeros_like(mask)
    colors, counts = np.unique(c16[mask].reshape(-1, 3), axis=0,
                               return_counts=True)
    for color in colors[counts >= FAIL_CLUSTER]:
        same = mask & np.all(c16 == color, axis=2)
        lab, n = ndimage.label(same, structure=np.ones((3, 3), dtype=int))
        sizes = np.bincount(lab.ravel())
        for i in range(1, n + 1):
            if sizes[i] < FAIL_CLUSTER:
                continue
            ys, xs = np.nonzero(lab == i)
            bbox_area = ((ys.max() - ys.min() + 1)
                         * (xs.max() - xs.min() + 1))
            interior = (ys.min() > BOSSBAR_Y and ys.max() < mask.shape[0] - 1
                        and xs.min() > 0 and xs.max() < mask.shape[1] - 1)
            if interior and sizes[i] / bbox_area >= 0.90:
                protected[lab == i] = True
    return protected


def _region_matches(entry, ys, xs):
    regions = entry.get("regions")
    if not regions:
        return False
    y0, y1, x0, x1 = ys.min(), ys.max(), xs.min(), xs.max()
    return any(y0 >= r[0] and x0 >= r[1] and y1 <= r[2] and x1 <= r[3]
               for r in regions)


def _region_mask(entry, shape):
    """Inclusive sidecar regions as a pixel mask; no regions accepts nothing."""
    out = np.zeros(shape, dtype=bool)
    h, w = shape
    for y0, x0, y1, x1 in entry.get("regions", []):
        y0, x0 = max(0, y0), max(0, x0)
        y1, x1 = min(h - 1, y1), min(w - 1, x1)
        if y0 <= y1 and x0 <= x1:
            out[y0:y1 + 1, x0:x1 + 1] = True
    return out


def _known_cluster_class(entries, ys, xs, protected):
    """Return known:N only for scoped, non-marker residual components."""
    if protected[ys, xs].any():
        return None
    for entry in entries:
        predicate = entry.get("predicate", {})
        if (predicate.get("type") == "non_solid_scene"
                and _region_matches(entry, ys, xs)):
            return f"known:{entry['open_divergence']}"
    return None


def _known_pixel_masks(entries, o16, c16, mask, protected):
    """Extract tightly scoped color/texture shifts before residual labeling."""
    masks = []
    brightness_o = o16.mean(axis=2)
    brightness_c = c16.mean(axis=2)
    saturation_o = o16.max(axis=2) - o16.min(axis=2)
    saturation_c = c16.max(axis=2) - c16.min(axis=2)
    for entry in entries:
        predicate = entry.get("predicate", {})
        ptype = predicate.get("type")
        usable = mask & ~protected & _region_mask(entry, mask.shape)
        if not usable.any():
            continue
        if ptype == "texture_luminance_modulation":
            # AO, directional face shade, and destroy-stage overlays modulate
            # an existing texture nearly achromatically. Normalize RGB by its
            # sum so geometry/sprite substitutions, including marker colors,
            # cannot enter this class merely because their luminance differs.
            osum = o16.sum(axis=2).astype(np.float64)
            csum = c16.sum(axis=2).astype(np.float64)
            onorm = o16 / np.maximum(osum[..., None], 1.0)
            cnorm = c16 / np.maximum(csum[..., None], 1.0)
            max_delta = predicate.get("max_chroma_delta", 0.08)
            min_rgb_sum = predicate.get("min_rgb_sum", 24)
            accepted = (usable & (osum >= min_rgb_sum)
                        & (csum >= min_rgb_sum)
                        & (np.abs(onorm - cnorm).max(axis=2) <= max_delta))
            if accepted.any():
                masks.append((accepted,
                              f"known:{entry['open_divergence']}"))
            continue
        if ptype != "global_oracle_darker_desaturated":
            continue
        # The entry is valid only when the frame as a whole has the filed
        # weather signature. Pixel extraction stays directional. The small
        # saturation slack admits shaded terrain under the same global dim.
        if (brightness_o[usable].mean() >= brightness_c[usable].mean()
                or saturation_o[usable].mean() >= saturation_c[usable].mean()):
            continue
        slack = predicate.get("pixel_saturation_slack", 0)
        accepted = (usable & (brightness_o < brightness_c)
                    & (saturation_o < saturation_c + slack))
        if accepted.any():
            masks.append((accepted,
                          f"known:{entry['open_divergence']}"))
    return masks


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


def _labeled_clusters(mask, o16, c16, w, h, fixed_class=None,
                      known_entries=None, protected=None):
    """Label one isolated mask and return its non-noise components."""
    from scipy import ndimage

    lab, n = ndimage.label(mask, structure=np.ones((3, 3), dtype=int))
    if n == 0:
        return []
    sizes = np.bincount(lab.ravel())
    ob = o16.sum(axis=2)
    cb = c16.sum(axis=2)
    out = []
    for i in range(1, n + 1):
        if sizes[i] < MIN_CLUSTER:
            continue
        ys, xs = np.nonzero(lab == i)
        cls = fixed_class or _classify(ob, cb, ys, xs, w, h)
        if cls == "UNEXPLAINED" and known_entries:
            cls = (_known_cluster_class(known_entries, ys, xs, protected)
                   or cls)
        out.append({"px": int(sizes[i]), "cls": cls,
                    "bbox": [int(ys.min()), int(xs.min()),
                             int(ys.max()), int(xs.max())]})
    return out


def gate_frame(o16, c16, w, h, *, tick=None, known=None):
    """Diff one frame pair (int16 (h,w,3) arrays) -> list of cluster dicts.
    Only components >= MIN_CLUSTER are returned."""
    d = np.abs(o16 - c16).max(axis=2)
    mask = d > DIFF_THRESH
    if not mask.any():
        return []

    # Positional acceptances are topology barriers, not post-label classes.
    # Remove them before labeling the remaining image so an accepted HUD,
    # bossbar, or viewmodel pixel cannot bridge otherwise separate terrain
    # components into one large unexplained blob. Label each accepted region
    # independently so its pixel totals remain visible in the baseline.
    bossbar = np.zeros_like(mask)
    bossbar[:BOSSBAR_Y + 1, :] = True
    hud = np.zeros_like(mask)
    hud[int(h * HUD_FRAC):, :] = True
    viewmodel = np.zeros_like(mask)
    viewmodel[int(h * VIEWMODEL_Y_FRAC) + 1:,
              int(w * VIEWMODEL_X_FRAC) + 1:] = True
    viewmodel &= ~hud
    protected = _solid_box_mask(c16, mask)

    out = []
    for region, cls in ((bossbar, "bossbar"), (hud, "hud"),
                        (viewmodel, "viewmodel")):
        out.extend(_labeled_clusters(mask & region, o16, c16, w, h,
                                     fixed_class=cls))
    mask &= ~(bossbar | hud | viewmodel)
    entries = _active_known(known, tick)
    for known_mask, cls in _known_pixel_masks(entries, o16, c16, mask,
                                               protected):
        out.extend(_labeled_clusters(known_mask, o16, c16, w, h,
                                     fixed_class=cls))
        mask &= ~known_mask
    out.extend(_labeled_clusters(mask, o16, c16, w, h,
                                 known_entries=entries,
                                 protected=protected))
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
