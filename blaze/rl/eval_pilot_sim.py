#!/usr/bin/env python3
"""BLAZE-sim eval of the Java-pilot-failing native BF16 checkpoint.

Answers: does native_1p92b.pt fail on pilot seeds 2/3/10 in the BLAZE env
too (generalization/quality), or only in Java (transfer gap)?

Mirrors the pilot protocol as closely as the sim allows:
  ep_ticks=6000, tries=5, sampling=categorical,
  rng_protocol=torch.manual_seed(seed*100+attempt),
  success = inv_counts[torch] >= 1 (status col IX_TORCH).

Usage (GPU0 under flock):
  flock /home/infatoshi/dev/nw/.tmp/gpu0.lock env CUDA_VISIBLE_DEVICES=0 \\
    UV_CACHE_DIR=/home/infatoshi/.cache/uv TMPDIR=/home/infatoshi/dev/nw/.tmp \\
    uv run --no-project --with numpy==2.5.1 --with torch==2.13.0 \\
    python blaze/rl/eval_pilot_sim.py
"""
from __future__ import annotations

import hashlib
import json
import os
import subprocess
import sys
import time
from pathlib import Path

import torch

ROOT = Path(__file__).resolve().parents[2]
ENV_DIR = ROOT / "blaze" / "env"
RL_DIR = ROOT / "blaze" / "rl"
sys.path.insert(0, str(ENV_DIR))
sys.path.insert(0, str(RL_DIR))

from blaze import CUDA_SO, VecBlaze  # noqa: E402
from ppo_chain_cu import (  # noqa: E402
    HEADS,
    IX_TORCH,
    MILE_NAMES,
    N_STAGES,
    NHEAD,
    NPLANES,
    REPEAT,
    STACK,
    TRAIN_SEEDS,
    ChainPolicy,
    acts_to_rows,
    build_frame,
    build_scal,
    obs_float,
    stage_of_best,
)

EXPECTED_SHA = "ecd7aa73709fa9485364fda768559a7cbc45e43352ed703f5c2ead8a373266f0"
CKPT_DEFAULT = ROOT / "blaze" / "rl" / "out" / "native_1p92b.pt"
SNAPS_DEFAULT = ROOT / "blaze" / "rl" / "out" / "snaps"
OUT_DEFAULT = ROOT / "optloop_runs" / "simgen-v1" / "PRESERVED"
PILOT_SEEDS = [2, 3, 10]
EP_TICKS = int(os.environ.get("EP_TICKS", "6000"))
TRIES = int(os.environ.get("TRIES", "5"))
# decisions = ticks / REPEAT (trainer default EP_DEC=1500 for 6000 ticks)
EP_DECISIONS = EP_TICKS // REPEAT
DEVICE = int(os.environ.get("BLAZE_DEV", "0"))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def git_provenance():
    commit = subprocess.check_output(
        ["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip()
    clean = subprocess.run(
        ["git", "diff-index", "--quiet", "HEAD", "--"], cwd=ROOT,
        check=False).returncode == 0
    return commit, clean


def snap_path(snaps: Path, seed: int) -> Path:
    return snaps / f"s{seed}_t0.bsnp"


def available_seeds(candidates, snaps: Path):
    ok, missing = [], []
    for seed in candidates:
        if snap_path(snaps, seed).is_file():
            ok.append(int(seed))
        else:
            missing.append(int(seed))
    return ok, missing


def strict_load(ckpt: Path, device: torch.device):
    """Load checkpoint into ChainPolicy with strict=True. No renames needed
    for the native conversion (keys already match master ChainPolicy)."""
    raw = torch.load(ckpt, map_location="cpu", weights_only=True)
    model = ChainPolicy()
    expected = model.state_dict()
    ckpt_keys = list(raw.keys())
    model_keys = list(expected.keys())
    missing = [k for k in model_keys if k not in raw]
    unexpected = [k for k in ckpt_keys if k not in expected]
    shape_mismatch = []
    for key in model_keys:
        if key in raw and tuple(raw[key].shape) != tuple(expected[key].shape):
            shape_mismatch.append({
                "key": key,
                "ckpt": list(raw[key].shape),
                "model": list(expected[key].shape),
            })
    mapping = {
        "schema": "netherite.simgen.strict-load.v1",
        "checkpoint": str(ckpt),
        "checkpoint_sha256": sha256_file(ckpt),
        "parameter_keys_ckpt": len(ckpt_keys),
        "parameter_keys_model": len(model_keys),
        "keys_identical_order": ckpt_keys == model_keys,
        "key_rename_map": {},
        "missing_keys": missing,
        "unexpected_keys": unexpected,
        "shape_mismatch": shape_mismatch,
        "strict": True,
    }
    if missing or unexpected or shape_mismatch:
        mapping["status"] = "fail"
        return None, mapping
    incompatible = model.load_state_dict(raw, strict=True)
    if incompatible.missing_keys or incompatible.unexpected_keys:
        mapping["status"] = "fail"
        mapping["incompatible"] = {
            "missing": list(incompatible.missing_keys),
            "unexpected": list(incompatible.unexpected_keys),
        }
        return None, mapping
    model.to(device)
    model.eval()
    mapping["status"] = "ok"
    mapping["parameter_values"] = sum(p.numel() for p in model.parameters())
    mapping["ckpt_keys"] = ckpt_keys
    mapping["model_keys"] = model_keys
    return model, mapping


def is_noop_row(acts_1d: torch.Tensor) -> bool:
    """True if discrete head sample is the env noop (look 0, forward 0, rest 0).
    Matches eval_chain_batched / trainer burn-in noop: heads 0/1/2 == 1."""
    a = acts_1d.tolist()
    return (a[0] == 1 and a[1] == 1 and a[2] == 1
            and all(int(x) == 0 for x in a[3:]))


def run_episode(env, net, dev, seed: int, attempt: int, snap_idx: int):
    """One full episode: 6000 ticks / 1500 decisions, categorical sampling."""
    rng_seed = seed * 100 + attempt
    torch.manual_seed(rng_seed)

    env.assign([snap_idx])
    env.reset()

    # burn-in / first frame via noop step (same as eval_chain_batched)
    noop = torch.zeros((1, NHEAD), dtype=torch.int64, device=dev)
    noop[:, 0] = noop[:, 1] = noop[:, 2] = 1
    cam, depth, edge, scal, rew, done, pose = env.step(
        acts_to_rows(noop, dev), repeat=REPEAT)
    frame = build_frame(cam, depth, edge)
    stack = frame.repeat(1, STACK, 1, 1)
    best = env.status[:, :9].clone()
    non_noop_decisions = 0
    actions_sent = 0
    success = False
    finished_early = False

    for dec in range(EP_DECISIONS):
        tfrac = torch.tensor([dec / EP_DECISIONS], dtype=torch.float32,
                             device=dev)
        scal_full = build_scal(scal, env.status, pose, tfrac)
        with torch.no_grad():
            logits, _ = net(obs_float(stack), scal_full)
            a = torch.stack(
                [torch.distributions.Categorical(logits=lg).sample()
                 for lg in logits], dim=1)
        if not is_noop_row(a[0]):
            non_noop_decisions += 1
        cam, depth, edge, scal, rew, done, pose = env.step(
            acts_to_rows(a, dev), repeat=REPEAT)
        actions_sent += REPEAT
        best = torch.maximum(best, env.status[:, :9])
        if int(done[0].item()) == 1:
            success = True
            finished_early = True
            break
        if int(done[0].item()) > 0:  # death / out
            finished_early = True
            break
        frame = build_frame(cam, depth, edge)
        stack = torch.cat([stack[:, NPLANES:], frame], dim=1)

    inv = [int(v) for v in best[0].tolist()]
    observed_torches = inv[IX_TORCH]
    # status torch count is the ground truth success signal (matches pilot)
    success_from_inv = observed_torches >= 1
    if success and not success_from_inv:
        # done==1 should mean torch; keep inv as authority for the receipt
        success = success_from_inv
    if success_from_inv:
        success = True
    reach = int(stage_of_best(best)[0].item())
    if success:
        reach = N_STAGES
    # non_noop_steps: tick-comparable (each decision is REPEAT env ticks)
    non_noop_steps = non_noop_decisions * REPEAT

    return {
        "seed": seed,
        "attempt": attempt,
        "policy_rng_seed": rng_seed,
        "world_seed": seed,
        "reached": reach,
        "success": bool(success),
        "success_source": "blaze_status.inv_counts[torch]",
        "observed_torches": observed_torches,
        "torches_gt0": observed_torches > 0,
        "waypoints_gt0": reach > 0,
        "best_inv_counts": inv,
        "actions_sent": actions_sent,
        "non_noop_decisions": non_noop_decisions,
        "non_noop_steps": non_noop_steps,
        "finished_early": finished_early,
        "milestone_name": (
            "TORCH" if reach >= N_STAGES
            else MILE_NAMES[min(reach, len(MILE_NAMES) - 1)]),
    }


def summarize(attempts):
    non_noop = [a["non_noop_steps"] for a in attempts]
    per_seed_reached = {}
    per_seed_torches = {}
    per_seed_successes = {}
    per_seed_non_noop = {}
    for a in attempts:
        s = str(a["seed"])
        per_seed_reached[s] = max(per_seed_reached.get(s, 0), a["reached"])
        per_seed_torches[s] = per_seed_torches.get(s, 0) + int(a["torches_gt0"])
        per_seed_successes[s] = per_seed_successes.get(s, 0) + int(a["success"])
        per_seed_non_noop.setdefault(s, []).append(a["non_noop_steps"])
    return {
        "n_attempts": len(attempts),
        "successes": sum(1 for a in attempts if a["success"]),
        "attempts_with_torches_gt0": sum(1 for a in attempts if a["torches_gt0"]),
        "attempts_with_reached_gt0": sum(1 for a in attempts if a["reached"] > 0),
        "non_noop_steps_mean": (
            float(sum(non_noop) / len(non_noop)) if non_noop else 0.0),
        "non_noop_steps_min": min(non_noop) if non_noop else 0,
        "non_noop_steps_max": max(non_noop) if non_noop else 0,
        "per_seed_reached": per_seed_reached,
        "per_seed_torches_gt0": per_seed_torches,
        "per_seed_successes": per_seed_successes,
        "per_seed_non_noop_mean": {
            k: float(sum(v) / len(v)) for k, v in per_seed_non_noop.items()
        },
    }


def write_verdict(path: Path, results: dict, java_summary: dict):
    pilot_seeds = results["pilot_seeds"]
    control_seeds = results["control_seeds"]
    sim_sum = results["summary"]
    lines = []
    lines.append("# VERDICT: simgen-v1 native_1p92b sim vs Java pilot")
    lines.append("")
    lines.append("## Question")
    lines.append("")
    lines.append(
        "Does the 1.92B-tick native BF16 checkpoint that went 0/15 in the "
        "real Java client also fail in the BLAZE sim on the same seeds?"
    )
    lines.append("")
    lines.append("- Sim FAIL + Java FAIL => generalization / checkpoint quality")
    lines.append("- Sim SUCCESS + Java FAIL => sim-to-Java transfer gap")
    lines.append("")
    lines.append("## Setup")
    lines.append("")
    lines.append(f"- checkpoint: `{results['checkpoint']}`")
    lines.append(f"- sha256: `{results['checkpoint_sha256']}`")
    lines.append(f"- strict load: `{results['load']['status']}` "
                 f"(renames: {results['load']['key_rename_map']})")
    lines.append(f"- environment: blaze CUDA (`{results['backend']}`)")
    lines.append(f"- ep_ticks={results['ep_ticks']} tries={results['tries']} "
                 f"sampling={results['sampling']} rng={results['rng_protocol']}")
    lines.append(f"- pilot seeds evaluated: {pilot_seeds}")
    lines.append(f"- control (TRAIN_SEEDS) seeds evaluated: {control_seeds}")
    if results.get("missing_seeds"):
        lines.append(f"- missing snapshots (skipped): {results['missing_seeds']}")
    lines.append("")
    lines.append("## Per-seed 2x2 (sim x java)")
    lines.append("")
    lines.append("| seed | role | sim reached (best) | sim torches>0 | sim success | "
                 "java reached | java success | cell |")
    lines.append("|------|------|--------------------|---------------|-------------| "
                 "--------------|--------------|------|")
    java_reached = java_summary.get("per_seed_reached", {})
    for seed in pilot_seeds:
        s = str(seed)
        sim_r = sim_sum["per_seed_reached"].get(s, 0)
        sim_t = sim_sum["per_seed_torches_gt0"].get(s, 0)
        sim_ok = sim_sum["per_seed_successes"].get(s, 0)
        # "success" for the transfer question: any torch OR any waypoint progress
        # Primary ship metric is torch success; waypoint is secondary signal.
        sim_torch = sim_ok > 0
        java_r = int(java_reached.get(s, 0))
        java_ok = False  # pilot receipt: 0 successes overall
        if sim_torch and not java_ok:
            cell = "SIM_OK / JAVA_FAIL => transfer gap"
        elif (not sim_torch) and (not java_ok):
            cell = "SIM_FAIL / JAVA_FAIL => generalization / quality"
        elif sim_torch and java_ok:
            cell = "SIM_OK / JAVA_OK => both work"
        else:
            cell = "SIM_FAIL / JAVA_OK => unexpected"
        role = "pilot"
        if seed in control_seeds:
            role = "pilot+control"
        lines.append(
            f"| {seed} | {role} | {sim_r} | {sim_t}/{results['tries']} | "
            f"{sim_ok}/{results['tries']} | {java_r} | 0/5 | {cell} |"
        )
    for seed in control_seeds:
        if seed in pilot_seeds:
            continue
        s = str(seed)
        sim_r = sim_sum["per_seed_reached"].get(s, 0)
        sim_t = sim_sum["per_seed_torches_gt0"].get(s, 0)
        sim_ok = sim_sum["per_seed_successes"].get(s, 0)
        lines.append(
            f"| {seed} | control | {sim_r} | {sim_t}/{results['tries']} | "
            f"{sim_ok}/{results['tries']} | n/a | n/a | train-seed baseline |"
        )
    lines.append("")
    lines.append("## Aggregate")
    lines.append("")
    lines.append(f"- sim successes (torch): {sim_sum['successes']}/{sim_sum['n_attempts']}")
    lines.append(f"- sim attempts with torches>0: {sim_sum['attempts_with_torches_gt0']}")
    lines.append(f"- sim attempts with reached>0: {sim_sum['attempts_with_reached_gt0']}")
    lines.append(
        f"- sim non_noop_steps mean/min/max: "
        f"{sim_sum['non_noop_steps_mean']:.1f} / "
        f"{sim_sum['non_noop_steps_min']} / {sim_sum['non_noop_steps_max']}"
    )
    lines.append(
        f"- java pilot: successes={java_summary.get('successes', 0)}, "
        f"torches_gt0={java_summary.get('attempts_with_torches_gt0', 0)}, "
        f"reached_gt0={java_summary.get('attempts_with_reached_gt0', 0)}, "
        f"non_noop mean={java_summary.get('non_noop_steps_mean', 'n/a')}"
    )
    lines.append("")
    lines.append("## Control-seed comparison")
    lines.append("")
    lines.append(
        f"TRAIN_SEEDS default = {results['train_seeds']}. "
        f"First two with snapshots used as controls: {control_seeds}."
    )
    if set(pilot_seeds).issubset(set(results["train_seeds"])):
        lines.append(
            "Note: pilot seeds 2/3/10 are themselves members of TRAIN_SEEDS, "
            "so a sim failure on them is not pure OOD-seed generalization; "
            "it is in-distribution checkpoint quality / incomplete skill."
        )
    for seed in control_seeds:
        s = str(seed)
        lines.append(
            f"- control seed {seed}: best_reached="
            f"{sim_sum['per_seed_reached'].get(s, 0)}, "
            f"torches_gt0={sim_sum['per_seed_torches_gt0'].get(s, 0)}/"
            f"{results['tries']}, "
            f"successes={sim_sum['per_seed_successes'].get(s, 0)}/"
            f"{results['tries']}, "
            f"non_noop_mean={sim_sum['per_seed_non_noop_mean'].get(s, 0):.1f}"
        )
    lines.append("")
    lines.append("## Verdict")
    lines.append("")
    pilot_sim_success = any(
        sim_sum["per_seed_successes"].get(str(s), 0) > 0 for s in pilot_seeds
    )
    pilot_sim_waypoint = any(
        sim_sum["per_seed_reached"].get(str(s), 0) > 0 for s in pilot_seeds
    )
    if pilot_sim_success:
        lines.append(
            "**TRANSFER GAP**: checkpoint places torches in BLAZE sim on at "
            "least one pilot seed, but Java pilot was 0/15 torches and 0 "
            "waypoints. Prefer interface/semantics audit (sibling lane)."
        )
        overall = "transfer_gap"
    elif pilot_sim_waypoint:
        lines.append(
            "**PARTIAL / QUALITY + POSSIBLE TRANSFER**: checkpoint reaches "
            "waypoints (stage>0) in sim but never crafts a torch; Java also "
            "got 0 torches and 0 waypoints. Torch failure is shared; waypoint "
            "delta may still hide a transfer or observation gap."
        )
        overall = "partial_quality_with_waypoint_delta"
    else:
        lines.append(
            "**GENERALIZATION / CHECKPOINT QUALITY**: checkpoint fails in "
            "BLAZE sim on the same pilot seeds (0 torches, 0 waypoints) just "
            "as it failed in Java. The discriminating failure is not "
            "sim-to-real transfer; the policy does not complete the chain in "
            "the training environment either."
        )
        overall = "generalization_quality"
    lines.append("")
    lines.append(f"overall_label: `{overall}`")
    lines.append("")
    path.write_text("\n".join(lines) + "\n")
    return overall


def main() -> int:
    ckpt = Path(os.environ.get("CHAIN_NET", str(CKPT_DEFAULT)))
    snaps = Path(os.environ.get("SNAPS", str(SNAPS_DEFAULT)))
    out_dir = Path(os.environ.get("OUT_DIR", str(OUT_DEFAULT)))
    out_dir.mkdir(parents=True, exist_ok=True)
    log_path = out_dir / "eval.log"

    class Tee:
        def __init__(self, *streams):
            self.streams = streams

        def write(self, data):
            for s in self.streams:
                s.write(data)
                s.flush()

        def flush(self):
            for s in self.streams:
                s.flush()

    # Append so flock-wait lines written by the launcher are preserved.
    log_f = open(log_path, "a")
    sys.stdout = Tee(sys.__stdout__, log_f)
    sys.stderr = Tee(sys.__stderr__, log_f)

    t0 = time.time()
    print(f"eval_pilot_sim start ts={time.strftime('%Y-%m-%dT%H:%M:%SZ', time.gmtime())}")
    print(f"ckpt={ckpt}")
    print(f"snaps={snaps}")
    print(f"out={out_dir}")
    print(f"CUDA_VISIBLE_DEVICES={os.environ.get('CUDA_VISIBLE_DEVICES')}")
    print(f"HEADS={HEADS} N_STAGES={N_STAGES} EP_TICKS={EP_TICKS} "
          f"EP_DECISIONS={EP_DECISIONS} REPEAT={REPEAT} TRIES={TRIES}")

    digest = sha256_file(ckpt)
    print(f"checkpoint sha256={digest}")
    if digest != EXPECTED_SHA:
        print(f"ABORT: sha mismatch expected={EXPECTED_SHA}")
        return 2

    if not torch.cuda.is_available():
        print("ABORT: CUDA not available")
        return 3
    # With CUDA_VISIBLE_DEVICES=0, physical GPU0 is logical cuda:0
    dev = torch.device("cuda:0")
    print(f"device={dev} name={torch.cuda.get_device_name(0)}")

    net, load_receipt = strict_load(ckpt, dev)
    load_path = out_dir / "load_receipt.json"
    load_path.write_text(json.dumps(load_receipt, indent=2, sort_keys=True) + "\n")
    print(f"strict load status={load_receipt['status']} -> {load_path}")
    if net is None:
        print("ABORT: strict load failed")
        print(json.dumps(load_receipt, indent=2))
        return 4

    pilot_ok, pilot_missing = available_seeds(PILOT_SEEDS, snaps)
    # control: first two TRAIN_SEEDS with snapshots
    control_candidates = list(TRAIN_SEEDS)
    control_ok, control_missing = available_seeds(control_candidates, snaps)
    control_ok = control_ok[:2]
    eval_seeds = []
    for s in pilot_ok + control_ok:
        if s not in eval_seeds:
            eval_seeds.append(s)
    missing = sorted(set(pilot_missing + control_missing))
    print(f"TRAIN_SEEDS={TRAIN_SEEDS}")
    print(f"pilot_ok={pilot_ok} pilot_missing={pilot_missing}")
    print(f"control_ok={control_ok} (first two TRAIN_SEEDS with snaps)")
    print(f"eval_seeds={eval_seeds} missing={missing}")
    if not eval_seeds:
        print("ABORT: no seeds with snapshots")
        return 5
    if 10 not in eval_seeds and not control_ok:
        print("ABORT: need at least seed 10 or one control")
        return 5

    paths = [str(snap_path(snaps, s)) for s in eval_seeds]
    # VecBlaze device index is relative to CUDA_VISIBLE_DEVICES
    env = VecBlaze(1, device=0, so_path=CUDA_SO)
    env.set_success_item(50)  # torch
    n_loaded = env.load_snapshots(paths)
    print(f"loaded {n_loaded} snapshots: {paths}")
    seed_to_snap = {s: i for i, s in enumerate(eval_seeds)}

    attempts = []
    for seed in eval_seeds:
        for att in range(TRIES):
            t_ep = time.time()
            result = run_episode(
                env, net, dev, seed, att, seed_to_snap[seed])
            result["wall_s"] = round(time.time() - t_ep, 3)
            attempts.append(result)
            print(
                f"seed={seed} att={att} rng={result['policy_rng_seed']} "
                f"reached={result['reached']}({result['milestone_name']}) "
                f"torch={result['observed_torches']} "
                f"success={result['success']} "
                f"non_noop={result['non_noop_steps']} "
                f"inv={result['best_inv_counts']} "
                f"wall={result['wall_s']}s",
                flush=True,
            )

    env.close()
    summary = summarize(attempts)
    commit, tracked_clean = git_provenance()

    # Java pilot summary (from archived receipt; embedded for self-contained verdict)
    java_summary = {
        "attempts_with_reached_gt0": 0,
        "attempts_with_torches_gt0": 0,
        "n_attempts": 15,
        "non_noop_steps_max": 6000,
        "non_noop_steps_mean": 4464.533333333334,
        "non_noop_steps_min": 188,
        "per_seed_reached": {"10": 0, "2": 0, "3": 0},
        "successes": 0,
        "source": (
            "/home/infatoshi/dev/netherite-artifacts/branch-archive/"
            "ppo-native-bf16-20260802/optloop_runs/ppo-native-bf16-d55-v4/"
            "final/java_pilot/receipt.json"
        ),
    }

    results = {
        "schema": "netherite.simgen.pilot-sim.v1",
        "environment": "blaze",
        "backend": CUDA_SO,
        "commit": commit,
        "tracked_clean": tracked_clean,
        "checkpoint": str(ckpt.relative_to(ROOT)) if ckpt.is_relative_to(ROOT)
        else str(ckpt),
        "checkpoint_sha256": digest,
        "seeds": eval_seeds,
        "pilot_seeds": pilot_ok,
        "control_seeds": control_ok,
        "missing_seeds": missing,
        "train_seeds": list(TRAIN_SEEDS),
        "tries": TRIES,
        "ep_ticks": EP_TICKS,
        "ep_decisions": EP_DECISIONS,
        "repeat": REPEAT,
        "sampling": "categorical",
        "rng_protocol": "torch.manual_seed(seed*100+attempt)",
        "success_source": "blaze_status.inv_counts[torch]",
        "success_item": 50,
        "load": load_receipt,
        "attempts": attempts,
        "summary": summary,
        "per_seed_reached": summary["per_seed_reached"],
        "java_pilot_summary": java_summary,
        "wall_s": round(time.time() - t0, 3),
    }

    results_path = out_dir / "results.json"
    results_path.write_text(
        json.dumps(results, indent=2, sort_keys=True) + "\n")
    print(f"wrote {results_path}")

    verdict_path = out_dir / "VERDICT.md"
    overall = write_verdict(verdict_path, results, java_summary)
    print(f"wrote {verdict_path} overall={overall}")
    print(f"summary: {json.dumps(summary, sort_keys=True)}")
    print(f"done wall_s={results['wall_s']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
