#!/usr/bin/env python3
"""Run and stamp the fixed paper PPO recipe.

Invoke this through overnight-compute after acquiring the machine lock.
Generated logs/manifests go under RUN_DIR (default paper/results/train).
"""

import hashlib
import json
import os
import subprocess
import sys
from datetime import datetime, timezone

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DEFAULT_RECIPE = os.path.join(ROOT, "paper", "recipes", "ppo_chain_1p92b.json")
TRAINER = os.path.join(ROOT, "blaze", "env", "ppo_chain_cu.py")


def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def output(args):
    return subprocess.check_output(args, cwd=ROOT, text=True).strip()


def main():
    commit = output(["git", "rev-parse", "HEAD"])
    expected_commit = os.environ.get("PAPER_PIN")
    if not expected_commit:
        raise RuntimeError("PAPER_PIN is required; set it to the confirmed full commit")
    if commit != expected_commit:
        raise RuntimeError(f"checkout {commit} does not match PAPER_PIN {expected_commit}")
    if subprocess.run(
            ["git", "diff-index", "--quiet", "HEAD", "--"], cwd=ROOT,
            check=False).returncode != 0:
        raise RuntimeError("tracked checkout is dirty; refusing paper measurement")

    recipe_path = os.environ.get("PPO_RECIPE", DEFAULT_RECIPE)
    with open(recipe_path) as f:
        recipe = json.load(f)
    env = os.environ.copy()
    env.update(recipe["env"])
    run_dir = os.environ.get("RUN_DIR", os.path.join(ROOT, "paper", "results", "train"))
    os.makedirs(run_dir, exist_ok=True)

    seeds = [int(s) for s in recipe["env"]["TRAIN_SEEDS"].split(",")]
    snapshots = [os.path.join(ROOT, "blaze", "rl", "out", "snaps",
                              f"s{seed}_t0.bsnp") for seed in seeds]
    missing = [path for path in snapshots if not os.path.isfile(path)]
    if missing:
        raise FileNotFoundError(
            "missing t0 snapshots; build magma and run T0=1 "
            "blaze/env/make_snapshots.py first: " + ", ".join(missing))

    command = [sys.executable, TRAINER]
    manifest = {
        "schema": "netherite.ppo_run.v1",
        "started_utc": datetime.now(timezone.utc).isoformat(),
        "commit": commit,
        "recipe_path": os.path.relpath(recipe_path, ROOT),
        "recipe_sha256": sha256_file(recipe_path),
        "recipe": recipe,
        "command": command,
        "python": sys.version,
        "nvidia_smi": output(["nvidia-smi"]),
        "snapshots": {
            os.path.relpath(path, ROOT): sha256_file(path) for path in snapshots
        },
    }
    manifest_path = os.path.join(run_dir, "train_manifest.json")
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
        f.write("\n")

    log_path = os.path.join(run_dir, "train.log")
    with open(log_path, "w") as log:
        process = subprocess.Popen(command, cwd=ROOT, env=env,
                                   stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, text=True,
                                   bufsize=1)
        for line in process.stdout:
            sys.stdout.write(line)
            sys.stdout.flush()
            log.write(line)
            log.flush()
        returncode = process.wait()

    manifest["finished_utc"] = datetime.now(timezone.utc).isoformat()
    manifest["returncode"] = returncode
    manifest["log_sha256"] = sha256_file(log_path)
    out_dir = os.path.join(ROOT, "blaze", "rl", "out")
    artifacts = {}
    for suffix in (".pt", "_last.pt", "_reward.json"):
        path = os.path.join(out_dir, recipe["env"]["OUT_NAME"] + suffix)
        if os.path.isfile(path):
            artifacts[os.path.relpath(path, ROOT)] = sha256_file(path)
    manifest["artifacts"] = artifacts
    with open(manifest_path, "w") as f:
        json.dump(manifest, f, indent=2, sort_keys=True)
        f.write("\n")
    return returncode


if __name__ == "__main__":
    raise SystemExit(main())
