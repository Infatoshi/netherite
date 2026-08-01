#!/usr/bin/env python3
"""Validate and pair magma/Java policy-transfer result artifacts."""

import argparse
import json
import os

MILESTONES = ("t0", "logs3", "pick", "cobble3", "coal", "TORCH")
MATCH_FIELDS = (
    "schema", "commit", "tracked_clean", "checkpoint_sha256", "seeds",
    "tries", "ep_ticks", "repeat", "sampling", "rng_protocol",
)


def load_result(path):
    with open(path) as f:
        result = json.load(f)
    if result.get("schema") != "netherite.sim2real.v1":
        raise ValueError(f"{path}: unsupported schema {result.get('schema')!r}")
    return result


def validate_pair(sim, real):
    if sim.get("environment") != "magma":
        raise ValueError("sim artifact environment must be magma")
    if real.get("environment") != "java-1.11.2":
        raise ValueError("real artifact environment must be java-1.11.2")
    for field in MATCH_FIELDS:
        if sim.get(field) != real.get(field):
            raise ValueError(
                f"comparison asymmetry in {field}: "
                f"{sim.get(field)!r} != {real.get(field)!r}")
    if not sim["tracked_clean"]:
        raise ValueError("measurements must come from a clean tracked checkout")
    seeds = sim["seeds"]
    if len(seeds) < 13 or len(set(seeds)) != len(seeds):
        raise ValueError("paired transfer requires at least 13 distinct seeds")
    for artifact in (sim, real):
        by_seed = {seed: [] for seed in seeds}
        for attempt in artifact.get("attempts", []):
            seed = attempt["seed"]
            if seed not in by_seed:
                raise ValueError(f"attempt contains undeclared seed {seed}")
            if attempt["policy_rng_seed"] != seed * 100 + attempt["attempt"]:
                raise ValueError(f"seed {seed}: policy RNG protocol mismatch")
            if attempt["world_seed"] != seed:
                raise ValueError(f"seed {seed}: observed world seed mismatch")
            if attempt["actions_sent"] <= 0 or attempt["non_noop_steps"] <= 0:
                raise ValueError(f"seed {seed}: policy did not send non-noop actions")
            if artifact["environment"] == "java-1.11.2":
                if attempt["bridge_action_seq"] != attempt["actions_sent"]:
                    raise ValueError(f"seed {seed}: Java action acknowledgements mismatch")
                if not attempt.get("bridge_action_fnv64"):
                    raise ValueError(f"seed {seed}: missing Java action digest")
                if attempt["success_source"] != "live_java_obs.inv_counts[torch]":
                    raise ValueError(f"seed {seed}: Java success is not live-state-derived")
            by_seed[seed].append(attempt)
        for seed, attempts in by_seed.items():
            if not attempts:
                raise ValueError(f"missing attempts for seed {seed}")
            if len(attempts) > artifact["tries"]:
                raise ValueError(f"too many attempts for seed {seed}")
            attempt_ids = [attempt["attempt"] for attempt in attempts]
            if attempt_ids != list(range(len(attempts))):
                raise ValueError(f"seed {seed}: attempts are not contiguous from zero")
            successes = [attempt for attempt in attempts if attempt["success"]]
            if successes and successes != [attempts[-1]]:
                raise ValueError(f"seed {seed}: evaluator did not stop at first success")
            if not successes and len(attempts) != artifact["tries"]:
                raise ValueError(f"seed {seed}: failed seed did not run all attempts")
            for attempt in attempts:
                observed_success = attempt.get("observed_torches", 0) >= 1
                if attempt["success"] != observed_success:
                    raise ValueError(f"seed {seed}: success contradicts observed inventory")
                if not 0 <= attempt["reached"] < len(MILESTONES):
                    raise ValueError(f"seed {seed}: invalid milestone {attempt['reached']}")
                if attempt["success"] != (attempt["reached"] == len(MILESTONES) - 1):
                    raise ValueError(f"seed {seed}: success contradicts milestone")
                if (artifact["environment"] == "java-1.11.2"
                        and attempt["bridge_action_fnv64"] != attempt.get(
                            "local_action_fnv64")):
                    raise ValueError(f"seed {seed}: Java action digest mismatch")


def summarize(result):
    out = {}
    for seed in result["seeds"]:
        attempts = [a for a in result["attempts"] if a["seed"] == seed]
        best = max(a["reached"] for a in attempts)
        successes = [a for a in attempts if a["success"]]
        out[seed] = {
            "success": bool(successes),
            "best_reached": best,
            "attempts_run": len(attempts),
            "first_success_attempt": successes[0]["attempt"] if successes else None,
        }
    return out


def pair_results(sim, real):
    validate_pair(sim, real)
    sim_summary = summarize(sim)
    real_summary = summarize(real)
    rows = []
    for seed in sim["seeds"]:
        s = sim_summary[seed]
        r = real_summary[seed]
        if s["success"] and r["success"]:
            note = "matched success"
        elif not s["success"] and not r["success"]:
            if s["best_reached"] == r["best_reached"]:
                note = f"matched failure at {MILESTONES[s['best_reached']]}"
            else:
                note = (f"both failed; sim {MILESTONES[s['best_reached']]}, "
                        f"Java {MILESTONES[r['best_reached']]}")
        elif s["success"]:
            note = f"sim-only success; Java stopped at {MILESTONES[r['best_reached']]}"
        else:
            note = f"Java-only success; sim stopped at {MILESTONES[s['best_reached']]}"
        rows.append({"seed": seed, "sim": s, "java": r, "note": note})
    return {
        "schema": "netherite.sim2real.paired.v1",
        "commit": sim["commit"],
        "checkpoint_sha256": sim["checkpoint_sha256"],
        "protocol": {field: sim[field] for field in MATCH_FIELDS[3:]},
        "sim_successes": sum(row["sim"]["success"] for row in rows),
        "java_successes": sum(row["java"]["success"] for row in rows),
        "n_seeds": len(rows),
        "rows": rows,
    }


def markdown(paired):
    lines = [
        "# Paired policy transfer: magma vs Java 1.11.2",
        "",
        f"Commit: `{paired['commit']}`  ",
        f"Checkpoint SHA-256: `{paired['checkpoint_sha256']}`  ",
        (f"Aggregate: magma {paired['sim_successes']}/{paired['n_seeds']}; "
         f"Java {paired['java_successes']}/{paired['n_seeds']}."),
        "",
        "| Seed | Magma | Java 1.11.2 | Magma best | Java best | Divergence note |",
        "|---:|:---:|:---:|:---|:---|:---|",
    ]
    for row in paired["rows"]:
        sim = row["sim"]
        real = row["java"]
        lines.append(
            f"| {row['seed']} | {'PASS' if sim['success'] else 'FAIL'} | "
            f"{'PASS' if real['success'] else 'FAIL'} | "
            f"{MILESTONES[sim['best_reached']]} | "
            f"{MILESTONES[real['best_reached']]} | {row['note']} |")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sim", required=True)
    parser.add_argument("--java", required=True)
    parser.add_argument("--out-json", required=True)
    parser.add_argument("--out-md", required=True)
    args = parser.parse_args()
    paired = pair_results(load_result(args.sim), load_result(args.java))
    for path in (args.out_json, args.out_md):
        os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(args.out_json, "w") as f:
        json.dump(paired, f, indent=2, sort_keys=True)
        f.write("\n")
    with open(args.out_md, "w") as f:
        f.write(markdown(paired))


if __name__ == "__main__":
    main()
