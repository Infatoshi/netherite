#!/usr/bin/env python3
"""Per-region animated-texture and underwater-overlay gate."""

from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
from PIL import Image


def load_tape(path: Path):
    rows = [json.loads(line) for line in path.read_text().splitlines() if line.strip()]
    return rows[0], rows[1:]


def at_pose(row: dict, pose: dict) -> bool:
    eps = float(pose["epsilon"])
    return all(abs(float(row[k]) - float(pose[k])) <= eps for k in ("x", "y", "z"))


def mean_abs(a: np.ndarray, b: np.ndarray) -> float:
    return float(np.abs(a.astype(np.int16) - b.astype(np.int16)).mean())


def temporal_mean(oracle: list[np.ndarray], magma: list[np.ndarray]) -> float:
    if len(oracle) < 2:
        return 0.0
    o0 = oracle[0].astype(np.int16)
    m0 = magma[0].astype(np.int16)
    vals = []
    for o, m in zip(oracle[1:], magma[1:]):
        od = o.astype(np.int16) - o0
        md = m.astype(np.int16) - m0
        vals.append(float(np.abs(od - md).mean()))
    return float(np.mean(vals))


def save_evidence(path: Path, oracle: np.ndarray, magma: np.ndarray) -> None:
    diff = np.abs(oracle.astype(np.int16) - magma.astype(np.int16))
    shown = np.minimum(diff * 4, 255).astype(np.uint8)
    Image.fromarray(np.concatenate((oracle, magma, shown), axis=1), "RGB").save(path)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tape", type=Path, required=True)
    ap.add_argument("--magma", type=Path, required=True)
    ap.add_argument("--ticks", type=Path, required=True)
    ap.add_argument("--scene", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    scene = json.loads(args.scene.read_text())
    header, rows = load_tape(args.tape)
    magma_all = np.load(args.magma, mmap_mode="r")
    magma_ticks = np.load(args.ticks)
    magma_by_tick = {int(t): i for i, t in enumerate(magma_ticks)}
    args.out.mkdir(parents=True, exist_ok=True)

    samples = {}
    for pose_name, pose in scene["poses"].items():
        selected = []
        for row in rows:
            tick = int(row["t"])
            frame = row.get("frame")
            if frame and tick in magma_by_tick and at_pose(row, pose) and Path(frame).is_file():
                selected.append((tick, row, np.asarray(Image.open(frame).convert("RGB")),
                                 np.asarray(magma_all[magma_by_tick[tick]])))
        samples[pose_name] = selected

    print("=" * 108)
    print("ANIM VERIFY  (absolute and temporal mean absolute error per channel)")
    print("=" * 108)
    print(f"{'region':<20} {'frames':>6} {'abs/ch':>9} {'gate':>8} "
          f"{'temporal':>10} {'gate':>8} {'shifted':>9}  verdict")
    print("-" * 108)
    failed = False
    results = {}
    for name, cfg in scene["regions"].items():
        seq = samples[cfg["pose"]]
        x0, y0, x1, y1 = (int(v) for v in cfg["box"])
        oracle = [s[2][y0:y1, x0:x1] for s in seq]
        magma = [s[3][y0:y1, x0:x1] for s in seq]
        if not oracle or any(a.shape != b.shape for a, b in zip(oracle, magma)):
            print(f"{name:<20} {len(oracle):>6} {'-':>9} {'-':>8} "
                  f"{'-':>10} {'-':>8} {'-':>9}  FAIL(no samples)")
            failed = True
            continue
        abs_values = [mean_abs(a, b) for a, b in zip(oracle, magma)]
        abs_mean = float(np.mean(abs_values))
        abs_tol = float(cfg["abs_tol"])
        temporal = temporal_mean(oracle, magma)
        temporal_tol = cfg.get("temporal_tol")
        shifted = None
        sensitive = True
        if temporal_tol is not None:
            shift = int(cfg["phase_shift_ticks"])
            shifted_magma = magma[shift:] + magma[:shift]
            shifted = temporal_mean(oracle, shifted_magma)
            sensitive = shifted > float(temporal_tol)
        ok = abs_mean <= abs_tol
        if temporal_tol is not None:
            ok = ok and temporal <= float(temporal_tol) and sensitive
        verdict = "PASS" if ok else "FAIL"
        failed |= not ok
        print(f"{name:<20} {len(oracle):>6} {abs_mean:>9.3f} {abs_tol:>8.3f} "
              f"{temporal:>10.3f} "
              f"{('-' if temporal_tol is None else f'{float(temporal_tol):.3f}'):>8} "
              f"{('-' if shifted is None else f'{shifted:.3f}'):>9}  {verdict}")
        worst = int(np.argmax(abs_values))
        save_evidence(args.out / f"{name}_diff.png", oracle[worst], magma[worst])
        results[name] = {
            "frames": len(oracle), "absolute_mean_ch": abs_mean,
            "absolute_tol": abs_tol, "temporal_mean_ch": temporal,
            "temporal_tol": temporal_tol, "shifted_temporal_mean_ch": shifted,
            "phase_sensitive": sensitive, "verdict": verdict,
            "worst_tick": seq[worst][0],
        }
    print("-" * 108)
    for name, reason in scene["gaps"].items():
        print(f"{name:<20} {'GAP':>6}  {reason}")
    print("=" * 108)

    portal = [r for r in rows if "portal_frame" in r]
    portal_anchor = (int(portal[0]["t"]), int(portal[0]["portal_frame"])) if portal else None
    print(f"water mapping: frame=(total_time/2)%32; tape total_time={header['total_time']}")
    print(f"portal mapping: recorded TextureAtlasSprite.frameCounter, anchor={portal_anchor}")
    print("alignment: tape tick boundary; one global replay clock, no per-region best-match")
    print("negative control: shifted candidate must exceed each animated region's temporal gate")
    (args.out / "results.json").write_text(json.dumps({
        "tape": str(args.tape), "header_total_time": header["total_time"],
        "portal_anchor": portal_anchor, "results": results,
    }, indent=2) + "\n")
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
