#!/usr/bin/env python3
"""Validate and summarize the 10-chunk native precision smoke controls."""

from __future__ import annotations

import argparse
import json
import math
import re
import statistics
from pathlib import Path


def parse_chunks(path: Path, prefix: str) -> list[dict[str, float]]:
    chunks = []
    for line in path.read_text().splitlines():
        if not line.startswith(prefix + " "):
            continue
        row: dict[str, float] = {}
        for name, raw in re.findall(r"(\w+)=([^ ]+)", line):
            if "/" not in raw:
                try:
                    row[name] = float(raw)
                except ValueError:
                    pass
        chunks.append(row)
    if len(chunks) != 10:
        raise AssertionError(f"{path}: expected 10 chunks, got {len(chunks)}")
    return chunks


def require_healthy(rows: list[dict[str, float]], name: str) -> None:
    for index, row in enumerate(rows, 1):
        required = (
            "reward_mean", "loss_mean", "grad_norm_mean", "grad_norm_max",
            "update_norm", "updates", "available_cells",
            "peak_allocated_gib", "finite",
        )
        if not all(math.isfinite(row[key]) for key in required):
            raise AssertionError(f"{name} chunk {index} contains non-finite data")
        if row["finite"] != 1 or row["update_norm"] <= 0 or row["updates"] != 48:
            raise AssertionError(f"{name} chunk {index} stalled")
    steady_peaks = [row["peak_allocated_gib"] for row in rows[1:]]
    if max(steady_peaks) - min(steady_peaks) > 0.01:
        raise AssertionError(f"{name} memory peak grows across steady chunks")


def steady_benchmark(path: Path) -> dict[str, float]:
    samples = [
        float(value)
        for value in re.findall(r"NATIVE_BENCH .* wall_ms=([0-9.]+)",
                                path.read_text())
    ]
    if len(samples) != 10:
        raise AssertionError(f"{path}: expected 10 benchmark chunks")
    steady = samples[2:]
    mean_ms = statistics.mean(steady)
    return {
        "steady_samples": len(steady),
        "steady_chunk_wall_ms_mean": mean_ms,
        "steady_chunk_wall_ms_median": statistics.median(steady),
        "steady_env_ticks_per_s": 786432 / (mean_ms / 1000.0),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("smoke_dir", type=Path)
    parser.add_argument("oracle_receipt", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()

    native_fp32 = parse_chunks(
        args.smoke_dir / "native_fp32_10chunks.log", "NATIVE_SMOKE")
    native_bf16 = parse_chunks(
        args.smoke_dir / "native_10chunks.log", "NATIVE_SMOKE")
    python_fp32 = parse_chunks(
        args.smoke_dir / "python_fp32_10chunks.log", "PYTHON_SMOKE")
    for name, rows in (
        ("native_fp32", native_fp32),
        ("native_bf16", native_bf16),
        ("python_fp32", python_fp32),
    ):
        require_healthy(rows, name)

    native_sim = json.loads(
        (args.smoke_dir / "native_fp32_sim_sanity.json").read_text())
    bf16_sim = json.loads((args.smoke_dir / "sim_sanity.json").read_text())
    python_sim = json.loads(
        (args.smoke_dir / "python_sim_sanity.json").read_text())
    if not native_sim["finite_and_non_degenerate"]:
        raise AssertionError("native FP32 simulation sanity failed")
    if not python_sim["finite_and_non_degenerate"]:
        raise AssertionError("Python FP32 simulation sanity failed")
    if bf16_sim["finite_and_non_degenerate"]:
        raise AssertionError("full BF16 unexpectedly passed rejection control")
    if native_fp32[-1]["available_cells"] != python_fp32[-1]["available_cells"]:
        raise AssertionError("native FP32 curriculum did not match Python FP32")
    if native_bf16[-1]["available_cells"] >= native_fp32[-1]["available_cells"]:
        raise AssertionError("BF16 rejection control did not stall as measured")

    oracle = json.loads(args.oracle_receipt.read_text())
    update_delta = oracle["bf16_error_ceilings"]["update_delta"]["max_abs"]
    result = {
        "schema": "netherite.native-precision-smoke.v1",
        "production_recipe": {
            "n_envs": 6144,
            "chunk_length": 32,
            "repeat": 4,
            "epochs": 2,
            "minibatch": 8192,
            "chunks": 10,
            "optimizer_steps": 480,
        },
        "python_fp32": {
            "final_reward_mean": python_fp32[-1]["reward_mean"],
            "final_available_cells": python_fp32[-1]["available_cells"],
            "peak_allocated_gib": python_fp32[-1]["peak_allocated_gib"],
            "sim_value_range": [python_sim["value_min"],
                                python_sim["value_max"]],
            "all_action_categories_active": min(
                python_sim["active_categories_per_head"]) >= 2,
        },
        "native_fp32": {
            "status": "accepted",
            "final_reward_mean": native_fp32[-1]["reward_mean"],
            "final_available_cells": native_fp32[-1]["available_cells"],
            "peak_allocated_gib": native_fp32[-1]["peak_allocated_gib"],
            "sim_value_range": [native_sim["value_min"],
                                native_sim["value_max"]],
            "all_action_categories_active": min(
                native_sim["active_categories_per_head"]) >= 2,
            "all_chunks_finite_nonzero_updates": True,
            "steady_memory_growth_gib": max(
                row["peak_allocated_gib"] for row in native_fp32[1:]) - min(
                row["peak_allocated_gib"] for row in native_fp32[1:]),
            "timing": steady_benchmark(
                args.smoke_dir / "native_fp32_10chunks.log"),
        },
        "native_bf16": {
            "status": "rejected",
            "reason": (
                "finite single-step error masks sign-flipped Adam coordinates; "
                "the 10-chunk policy collapses and curriculum stalls"
            ),
            "final_reward_mean": native_bf16[-1]["reward_mean"],
            "final_available_cells": native_bf16[-1]["available_cells"],
            "sim_value_range": [bf16_sim["value_min"], bf16_sim["value_max"]],
            "active_categories_per_head":
                bf16_sim["active_categories_per_head"],
            "single_step_update_delta_max_abs": update_delta,
            "adam_learning_rate": 3e-4,
            "update_delta_to_learning_rate": update_delta / 3e-4,
            "timing": steady_benchmark(
                args.smoke_dir / "native_10chunks.log"),
        },
    }
    rendered = json.dumps(result, indent=2, sort_keys=True)
    if args.output:
        args.output.write_text(rendered + "\n")
    print(rendered)


if __name__ == "__main__":
    main()
