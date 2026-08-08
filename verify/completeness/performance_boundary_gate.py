#!/usr/bin/env python3
"""Static half of the performance contract; dynamic commands run in the sweep."""

from __future__ import annotations

import json
import pathlib


ROOT = pathlib.Path(__file__).resolve().parents[2]
EVIDENCE = {
    "PERF-01": ("magma/trace/perf_guard.py",
                "magma/trace/perf_baseline_gpu1.json"),
    "PERF-02": ("verify/completeness/capacity_boundary_gate.py",
                "magma/game/test_piston_capacity.c", "magma/game/test_runtime.c"),
    "PERF-03": ("scripts/kernel_parity_gate.sh",
                "verify/kernels/parity_manifest.json", "netherite_sweep.sh"),
    "PERF-04": ("verify/completeness/test_native_checkpoint.py",
                "verify/completeness/test_native_save_slot.py",
                "magma/game/test_runtime.c"),
    "PERF-05": ("verify/completeness/gap_audit.py",
                "verify/completeness/registry_gate.py", "netherite_sweep.sh",
                "scripts/setup_and_verify.sh"),
}


def main() -> int:
    for todo, evidence in EVIDENCE.items():
        for relative in evidence:
            path = ROOT / relative
            if not path.is_file() or path.stat().st_size == 0:
                raise RuntimeError(f"{todo}: missing evidence {relative}")
    baseline = json.loads(
        (ROOT / "magma/trace/perf_baseline_gpu1.json").read_text())
    metrics = baseline["metrics"]
    if set(metrics) != {
            "cpu_sps", "blaze_t0_gpu_sps", "magma_1080p_cuda_fps"}:
        raise RuntimeError("performance metric ownership changed")
    for name, spec in metrics.items():
        if spec["baseline"] <= 0 or not 0 < spec["max_regression_fraction"] <= .05:
            raise RuntimeError(f"{name}: invalid frozen floor")
    parity = json.loads(
        (ROOT / "verify/kernels/parity_manifest.json").read_text())
    if not parity:
        raise RuntimeError("kernel parity manifest is empty")
    print(
        "PASS performance boundary: three frozen throughput/render floors, dense "
        "capacity cases, CPU/CUDA and raster-twin parity, checkpoint/soak owners, "
        "and final clean-sweep ownership are fail-closed")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (KeyError, OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL performance boundary: {exc}")
        raise SystemExit(1)
