#!/usr/bin/env python3
"""Per-region animated-texture and underwater-overlay gate."""

from __future__ import annotations

import argparse
import io
import json
import sys
import zipfile
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


def temporal_mean(
    oracle: list[np.ndarray] | np.ndarray,
    magma: list[np.ndarray] | np.ndarray,
    mask: np.ndarray | None = None,
) -> float:
    if len(oracle) < 2:
        return 0.0
    o0 = oracle[0].astype(np.int16)
    m0 = magma[0].astype(np.int16)
    vals = []
    for o, m in zip(oracle[1:], magma[1:]):
        diff = np.abs((o.astype(np.int16) - o0) - (m.astype(np.int16) - m0))
        if mask is not None:
            diff = diff[mask]
        vals.append(float(diff.mean()))
    return float(np.mean(vals))


def motion_mask(frames: list[np.ndarray], minimum: float) -> np.ndarray:
    stacked = np.asarray(frames, dtype=np.int16)
    motion = np.abs(np.diff(stacked, axis=0)).mean(axis=(0, 3))
    return motion > minimum


def vanilla_atlas_frames(tape: Path, count: int) -> tuple[np.ndarray, list[str]]:
    """Independently decode TextureAtlasSprite metadata from the vanilla jar."""
    assets = Path(__file__).resolve().parents[2] / "magma" / "assets"
    sys.path.insert(0, str(assets))
    from mc_jar import find_jar

    names = [
        "water_still",
        "water_flow",
        "lava_still",
        "lava_flow",
        "fire_layer_0",
        "fire_layer_1",
    ]
    header, rows = load_tape(tape)
    portal = next(row for row in rows if "portal_frame" in row)
    total = int(header["total_time"])
    client_tick = total - ((total % 32 - int(portal["portal_frame"])) & 31)
    result = np.empty((count, len(names), 16, 16, 4), dtype=np.uint8)
    with zipfile.ZipFile(find_jar()) as jar:
        for sprite, name in enumerate(names):
            base = f"assets/minecraft/textures/blocks/{name}.png"
            image = Image.open(io.BytesIO(jar.read(base))).convert("RGBA")
            physical_count = image.height // image.width
            metadata = json.loads(jar.read(base + ".mcmeta"))["animation"]
            frametime = int(metadata.get("frametime", 1))
            sequence = [int(v) for v in metadata.get("frames", range(physical_count))]
            physical = [
                np.asarray(
                    image.crop(
                        (0, frame * image.width, image.width, (frame + 1) * image.width)
                    ).resize((16, 16), Image.Resampling.NEAREST)
                )
                for frame in range(physical_count)
            ]
            for tick in range(count):
                logical = ((client_tick + tick) // frametime) % len(sequence)
                result[tick, sprite] = physical[sequence[logical]]
    return result, names


def save_evidence(path: Path, oracle: np.ndarray, magma: np.ndarray) -> None:
    diff = np.abs(oracle.astype(np.int16) - magma.astype(np.int16))
    shown = np.minimum(diff * 4, 255).astype(np.uint8)
    Image.fromarray(np.concatenate((oracle, magma, shown), axis=1), "RGB").save(path)


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--tape", type=Path, required=True)
    ap.add_argument("--magma", type=Path, required=True)
    ap.add_argument("--ticks", type=Path, required=True)
    ap.add_argument("--atlas", type=Path, required=True)
    ap.add_argument("--scene", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    args = ap.parse_args()

    scene = json.loads(args.scene.read_text())
    header, rows = load_tape(args.tape)
    magma_all = np.load(args.magma, mmap_mode="r")
    magma_ticks = np.load(args.ticks)
    magma_by_tick = {int(tick): i for i, tick in enumerate(magma_ticks)}
    args.out.mkdir(parents=True, exist_ok=True)

    samples = {}
    for pose_name, pose in scene["poses"].items():
        selected = []
        for row in rows:
            tick = int(row["t"])
            frame = row.get("frame")
            if frame and tick in magma_by_tick and at_pose(row, pose) and Path(frame).is_file():
                selected.append(
                    (
                        tick,
                        row,
                        np.asarray(Image.open(frame).convert("RGB")),
                        np.asarray(magma_all[magma_by_tick[tick]]),
                    )
                )
        samples[pose_name] = selected

    width = 122
    print("=" * width)
    print("ANIM VERIFY  (absolute and temporal mean absolute error per channel)")
    print("=" * width)
    print(
        f"{'region':<20} {'frames':>6} {'abs/ch':>9} {'max/ch':>9} {'gate':>8} "
        f"{'temporal':>10} {'gate':>8} {'shifted':>9} {'pixels':>8}  verdict"
    )
    print("-" * width)
    failed = False
    results = {}
    for name, cfg in scene["regions"].items():
        seq = samples[cfg["pose"]]
        x0, y0, x1, y1 = (int(value) for value in cfg["box"])
        oracle = [sample[2][y0:y1, x0:x1] for sample in seq]
        magma = [sample[3][y0:y1, x0:x1] for sample in seq]
        if not oracle or any(a.shape != b.shape for a, b in zip(oracle, magma)):
            print(
                f"{name:<20} {len(oracle):>6} {'-':>9} {'-':>9} {'-':>8} "
                f"{'-':>10} {'-':>8} {'-':>9} {'-':>8}  FAIL(no samples)"
            )
            failed = True
            continue
        abs_values = [mean_abs(a, b) for a, b in zip(oracle, magma)]
        abs_mean = float(np.mean(abs_values))
        abs_max = float(np.max(abs_values))
        abs_tol = float(cfg["abs_tol"])
        max_abs_tol = float(cfg.get("max_abs_tol", float("inf")))
        mask = None
        if "motion_min" in cfg:
            mask = motion_mask(oracle, float(cfg["motion_min"]))
            if not np.any(mask):
                raise RuntimeError(f"{name}: motion mask selected no pixels")
        temporal = temporal_mean(oracle, magma, mask)
        temporal_tol = cfg.get("temporal_tol")
        shifted = None
        sensitive = True
        if temporal_tol is not None:
            shift = int(cfg["phase_shift_ticks"])
            shifted = temporal_mean(oracle[:-shift], magma[shift:], mask)
            sensitive = shifted > float(temporal_tol)
        ok = abs_mean <= abs_tol and abs_max <= max_abs_tol
        if temporal_tol is not None:
            ok = ok and temporal <= float(temporal_tol) and sensitive
        verdict = "PASS" if ok else "FAIL"
        failed |= not ok
        print(
            f"{name:<20} {len(oracle):>6} {abs_mean:>9.3f} {abs_max:>9.3f} {abs_tol:>8.3f} "
            f"{temporal:>10.3f} "
            f"{('-' if temporal_tol is None else f'{float(temporal_tol):.3f}'):>8} "
            f"{('-' if shifted is None else f'{shifted:.3f}'):>9} "
            f"{('-' if mask is None else str(int(mask.sum()))):>8}  {verdict}"
        )
        worst = int(np.argmax(abs_values))
        save_evidence(args.out / f"{name}_diff.png", oracle[worst], magma[worst])
        if name == "underwater_overlay":
            save_evidence(args.out / "underwater_transition_t219.png", oracle[0], magma[0])
        results[name] = {
            "frames": len(oracle),
            "absolute_mean_ch": abs_mean,
            "absolute_max_ch": abs_max,
            "absolute_tol": abs_tol,
            "max_absolute_tol": cfg.get("max_abs_tol"),
            "temporal_mean_ch": temporal,
            "temporal_tol": temporal_tol,
            "shifted_temporal_mean_ch": shifted,
            "motion_pixels": None if mask is None else int(mask.sum()),
            "phase_sensitive": sensitive,
            "verdict": verdict,
            "worst_tick": seq[worst][0],
        }

    atlas_expected, atlas_names = vanilla_atlas_frames(args.tape, len(rows))
    atlas_actual = np.fromfile(args.atlas, dtype=np.uint8)
    atlas_actual = atlas_actual.reshape(len(rows), len(atlas_names), 16, 16, 4)
    for name, cfg in scene.get("atlas_regions", {}).items():
        indices = [atlas_names.index(sprite) for sprite in cfg["sprites"]]
        oracle = atlas_expected[:, indices]
        magma = atlas_actual[:, indices]
        abs_mean = mean_abs(oracle, magma)
        abs_max = max(mean_abs(a, b) for a, b in zip(oracle, magma))
        temporal = temporal_mean(oracle, magma)
        shift = int(cfg["phase_shift_ticks"])
        shifted = temporal_mean(oracle[:-shift], magma[shift:])
        tol = float(cfg["temporal_tol"])
        ok = abs_mean == 0.0 and temporal <= tol and shifted > tol
        failed |= not ok
        verdict = "PASS" if ok else "FAIL"
        print(
            f"{name:<20} {len(rows):>6} {abs_mean:>9.3f} {abs_max:>9.3f} {0.0:>8.3f} "
            f"{temporal:>10.3f} {tol:>8.3f} {shifted:>9.3f} "
            f"{len(indices) * 256:>8}  {verdict}"
        )
        results[name] = {
            "source": "compiled_atlas",
            "frames": len(rows),
            "absolute_mean_ch": abs_mean,
            "absolute_max_ch": abs_max,
            "temporal_mean_ch": temporal,
            "temporal_tol": tol,
            "shifted_temporal_mean_ch": shifted,
            "phase_sensitive": shifted > tol,
            "verdict": verdict,
        }
    print("-" * width)
    print("=" * width)

    portal = [row for row in rows if "portal_frame" in row]
    portal_anchor = (int(portal[0]["t"]), int(portal[0]["portal_frame"])) if portal else None
    print(
        "animation mapping: TextureAtlasSprite.updateAnimation metadata; "
        f"tape total_time={header['total_time']}"
    )
    print(f"portal mapping: recorded TextureAtlasSprite.frameCounter, anchor={portal_anchor}")
    print("alignment: tape tick boundary; one global replay clock, no per-region best-match")
    print("negative control: shifted candidate must exceed each animated region's temporal gate")
    (args.out / "results.json").write_text(
        json.dumps(
            {
                "tape": str(args.tape),
                "header_total_time": header["total_time"],
                "portal_anchor": portal_anchor,
                "results": results,
            },
            indent=2,
        )
        + "\n"
    )
    return 1 if failed else 0


if __name__ == "__main__":
    raise SystemExit(main())
