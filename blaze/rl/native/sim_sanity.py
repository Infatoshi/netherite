#!/usr/bin/env python3
"""Fixed-seed batched blaze sanity evaluation for a converted checkpoint."""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import sys
from pathlib import Path

import numpy as np
import torch

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "blaze" / "env"))
from ppo_chain_cu import (
    HEADS,
    NCH,
    NHEAD,
    NPLANES,
    NSCAL,
    REPEAT,
    ChainPolicy,
    acts_to_rows,
    build_frame,
    build_scal,
    obs_float,
    stage_of_best,
)

from blaze import CUDA_SO, VecBlaze


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1 << 20), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("checkpoint", type=Path)
    parser.add_argument("--receipt", type=Path)
    parser.add_argument("--lanes", type=int, default=128)
    parser.add_argument("--decisions", type=int, default=128)
    parser.add_argument("--rng-seed", type=int, default=20260801)
    parser.add_argument("--seeds", default="2,3,10,14")
    args = parser.parse_args()
    seeds = [int(item) for item in args.seeds.split(",")]
    if args.lanes < len(seeds):
        raise ValueError("lanes must cover every requested seed")

    torch.manual_seed(args.rng_seed)
    torch.cuda.manual_seed_all(args.rng_seed)
    device = torch.device("cuda:0")
    state = torch.load(args.checkpoint, map_location="cpu", weights_only=True)
    model = ChainPolicy().to(device)
    model.load_state_dict(state, strict=True)
    model.eval()

    paths = [ROOT / "blaze" / "rl" / "out" / "snaps" /
             f"s{seed}_t0.bsnp" for seed in seeds]
    assignments = np.arange(args.lanes, dtype=np.int32) % len(seeds)
    env = VecBlaze(args.lanes, device=0, so_path=CUDA_SO)
    try:
        env.set_success_item(50)
        env.load_snapshots([str(path) for path in paths])
        env.assign(assignments)
        env.reset()
        stack = torch.zeros(
            (args.lanes, NCH, 36, 64), dtype=torch.uint8, device=device)
        scalars = torch.zeros((args.lanes, NSCAL), device=device)
        episode_decisions = torch.zeros(
            args.lanes, dtype=torch.int32, device=device)
        best = torch.zeros((args.lanes, 9), dtype=torch.int32, device=device)
        deepest = torch.zeros(args.lanes, dtype=torch.int64, device=device)
        action_counts = [torch.zeros(size, dtype=torch.int64, device=device)
                         for size in HEADS]
        episodes = 0
        successes = 0
        value_min = float("inf")
        value_max = float("-inf")
        finite = True
        noop = torch.zeros(NHEAD, dtype=torch.int64, device=device)
        noop[:3] = 1

        for decision in range(args.decisions):
            with torch.no_grad():
                logits, values = model(obs_float(stack), scalars)
                finite &= bool(torch.isfinite(values).all())
                finite &= all(bool(torch.isfinite(head).all()) for head in logits)
                value_min = min(value_min, float(values.min()))
                value_max = max(value_max, float(values.max()))
                actions = torch.stack([
                    torch.distributions.Categorical(logits=head).sample()
                    for head in logits
                ], dim=1)
                if decision == 0:
                    actions[:] = noop
                for head, size in enumerate(HEADS):
                    action_counts[head] += torch.bincount(
                        actions[:, head], minlength=size)

            cam, depth, edge, env_scalars, _, done, pose = env.step(
                acts_to_rows(actions, device), repeat=REPEAT)
            status = env.status
            best = torch.maximum(best, status[:, :9])
            deepest = torch.maximum(deepest, stage_of_best(best))
            episode_decisions += 1
            ended = (done > 0) | (episode_decisions >= 1500)

            frame = build_frame(cam, depth, edge)
            stack[:, :-NPLANES] = stack[:, NPLANES:].clone()
            stack[:, -NPLANES:] = frame
            scalars = build_scal(
                env_scalars, status, pose,
                episode_decisions.float() / 1500.0)

            if ended.any():
                indices = ended.nonzero(as_tuple=True)[0]
                episodes += int(indices.numel())
                successes += int((done[indices] == 1).sum())
                mask = ended.cpu().numpy().astype(np.uint8)
                env.reset(mask)
                stack[indices] = 0
                scalars[indices] = 0
                episode_decisions[indices] = 0
                best[indices] = 0

        histogram = torch.bincount(deepest, minlength=5).cpu().tolist()
        counts = [values.cpu().tolist() for values in action_counts]
        head_active = [sum(value > 0 for value in head) for head in counts]
        # Multi-category heads (size>2) should not fully collapse after training.
        # Binary heads may legitimately pick one action after a short smoke.
        multi_ok = all(
            active >= 2
            for active, size in zip(head_active, HEADS)
            if size > 2
        )
        values_sane = (
            math.isfinite(value_min)
            and math.isfinite(value_max)
            and abs(value_min) < 50.0
            and abs(value_max) < 50.0
        )
        finite &= multi_ok and values_sane
        result = {
            "schema": "netherite.native-sim-sanity.v1",
            "checkpoint": str(args.checkpoint),
            "checkpoint_sha256": sha256(args.checkpoint),
            "backend": str(CUDA_SO),
            "world_seeds": seeds,
            "lanes": args.lanes,
            "decisions": args.decisions,
            "repeat": REPEAT,
            "policy_rng_seed": args.rng_seed,
            "sampling": "torch.distributions.Categorical",
            "episodes_ended": episodes,
            "torch_successes": successes,
            "deepest_stage_histogram": histogram,
            "action_counts": counts,
            "active_categories_per_head": head_active,
            "multi_category_heads_diverse": multi_ok,
            "value_min": value_min,
            "value_max": value_max,
            "values_within_sane_range": values_sane,
            "finite_and_non_degenerate": finite,
        }
        rendered = json.dumps(result, indent=2, sort_keys=True)
        if args.receipt:
            args.receipt.parent.mkdir(parents=True, exist_ok=True)
            args.receipt.write_text(rendered + "\n")
        print(rendered)
        if not finite:
            raise SystemExit(2)
    finally:
        env.close()


if __name__ == "__main__":
    main()
