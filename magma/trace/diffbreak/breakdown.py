# diffbreak/breakdown.py - decompose the remaining magma-vs-real-MC terrain-crop
# error into measurable causes. Reads the checked-in goldens and the HEAD magma
# renders, writes heat_pose_<i>.png + prints per-pose tables and a hypothesis ranking.
# Run: uv run --no-project --with numpy --with pillow python3 trace/diffbreak/breakdown.py
import numpy as np
from PIL import Image
import os

CAP = "../verify/mc_capture"
R0, R1, C0, C1 = 180, 479, 180, 674  # terrain crop (rows, cols)

def load(p):
    return np.asarray(Image.open(p).convert("RGB"), dtype=np.float64)

def crop(a):
    return a[R0:R1, C0:C1]

def luma(a):
    return 0.299 * a[..., 0] + 0.587 * a[..., 1] + 0.114 * a[..., 2]

def region_stats(mask, g, c, top=5):
    # connected components via simple BFS on the boolean mask
    from collections import deque
    h, w = mask.shape
    seen = np.zeros_like(mask, dtype=bool)
    regions = []
    for y in range(h):
        for x in range(w):
            if mask[y, x] and not seen[y, x]:
                q = deque([(y, x)]); seen[y, x] = True
                pix = []
                while q:
                    cy, cx = q.popleft(); pix.append((cy, cx))
                    for dy, dx in ((1,0),(-1,0),(0,1),(0,-1)):
                        ny, nx = cy+dy, cx+dx
                        if 0 <= ny < h and 0 <= nx < w and mask[ny, nx] and not seen[ny, nx]:
                            seen[ny, nx] = True; q.append((ny, nx))
                regions.append(pix)
    regions.sort(key=len, reverse=True)
    out = []
    for pix in regions[:top]:
        ys = [p[0] for p in pix]; xs = [p[1] for p in pix]
        sel = (np.array(ys), np.array(xs))
        out.append({
            "area": len(pix),
            "bbox": (min(ys)+R0, min(xs)+C0, max(ys)+R0, max(xs)+C0),
            "golden_rgb": tuple(np.round(g[sel].mean(axis=0)).astype(int)),
            "magma_rgb": tuple(np.round(c[sel].mean(axis=0)).astype(int)),
        })
    return out

def shift_diff(g, c, dy, dx):
    H, W = g.shape[:2]
    gs = g[max(dy,0):H+min(dy,0), max(dx,0):W+min(dx,0)]
    cs = c[max(-dy,0):H+min(-dy,0), max(-dx,0):W+min(-dx,0)]
    return np.abs(gs - cs).mean()

print(f"crop rows {R0}:{R1} cols {C0}:{C1}")
agg = {"total": [], "luma": [], "chroma": [], "shift_gain": []}
for i in range(4):
    g = crop(load(f"{CAP}/mc_pose_{i}.png"))
    c = crop(load(f"{CAP}/magma_pose_{i}.png"))
    d = np.abs(g - c)
    dmean = d.mean()

    # heatmap
    hm = d.mean(axis=2)
    hm8 = np.clip(hm * (255.0 / max(hm.max(), 1e-9)), 0, 255).astype(np.uint8)
    Image.fromarray(hm8).save(f"trace/diffbreak/heat_pose_{i}.png")

    # luma vs chroma: chroma = residual after scaling magma pixel to golden luma
    lg, lc = luma(g), luma(c)
    dl = np.abs(lg - lc).mean()
    scale = (lg + 1e-6) / (lc + 1e-6)
    c_lumamatched = np.clip(c * scale[..., None], 0, 255)
    dchroma = np.abs(g - c_lumamatched).mean()

    # histogram (per-channel abs diff)
    lt8 = (d < 8).mean(); mid = ((d >= 8) & (d <= 32)).mean(); gt32 = (d > 32).mean()

    # registration: does +-1px shift reduce the whole-crop diff?
    base = dmean
    best = min(shift_diff(g, c, dy, dx) for dy in (-1, 0, 1) for dx in (-1, 0, 1))
    shift_gain = base - best

    print(f"\npose {i}: crop mean {dmean:.2f}")
    print(f"  luma-only error      {dl:.2f}   chroma (luma-matched) {dchroma:.2f}")
    print(f"  histogram <8: {lt8*100:.1f}%   8-32: {mid*100:.1f}%   >32: {gt32*100:.1f}%")
    print(f"  best +-1px shift reduces mean by {shift_gain:.2f} ({100*shift_gain/base:.1f}%)")
    mask = hm > 32
    print(f"  >32 hot area: {mask.mean()*100:.1f}% of crop; top regions:")
    for r in region_stats(mask, g, c):
        print(f"    area {r['area']:6d}  bbox {r['bbox']}  golden {r['golden_rgb']}  magma {r['magma_rgb']}")
    agg["total"].append(dmean); agg["luma"].append(dl)
    agg["chroma"].append(dchroma); agg["shift_gain"].append(shift_gain)

print("\nAGGREGATE (mean over poses):")
print(f"  total {np.mean(agg['total']):.2f}  luma {np.mean(agg['luma']):.2f}  "
      f"chroma-after-luma-match {np.mean(agg['chroma']):.2f}  shift-gain {np.mean(agg['shift_gain']):.2f}")
