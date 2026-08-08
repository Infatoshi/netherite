import copy

import pytest

from paper.tools.pair_sim2real import pair_results

SEEDS = [2, 3, 10, 11, 14, 16, 20, 27, 29, 32, 33, 44, 46]


def artifact(environment):
    attempts = []
    for seed in SEEDS:
        for attempt in range(5 if seed == 16 else 1):
            row = {
                "seed": seed,
                "attempt": attempt,
                "policy_rng_seed": seed * 100 + attempt,
                "reached": 5 if seed != 16 else 4,
                "success": seed != 16,
                "success_source": ("live_java_obs.inv_counts[torch]"
                                   if environment == "java-1.11.2"
                                   else "magma_bolr.inv_counts[torch]"),
                "observed_torches": 1 if seed != 16 else 0,
                "actions_sent": 40,
                "non_noop_steps": 30,
                "world_seed": seed,
            }
            if environment == "java-1.11.2":
                row["bridge_action_seq"] = 40
                row["bridge_action_fnv64"] = "abc"
                row["local_action_fnv64"] = "abc"
            attempts.append(row)
    return {
        "schema": "netherite.sim2real.v1",
        "environment": environment,
        "commit": "a" * 40,
        "tracked_clean": True,
        "checkpoint_sha256": "b" * 64,
        "seeds": SEEDS,
        "tries": 5,
        "ep_ticks": 6000,
        "repeat": 4,
        "sampling": "categorical",
        "rng_protocol": "torch.manual_seed(seed*100+attempt)",
        "attempts": attempts,
    }


def test_pairs_thirteen_seeds_and_counts_success():
    paired = pair_results(artifact("magma"), artifact("java-1.11.2"))
    assert paired["n_seeds"] == 13
    assert paired["sim_successes"] == 12
    assert paired["java_successes"] == 12
    assert paired["rows"][-4]["seed"] == 32


def test_rejects_checkpoint_asymmetry():
    sim = artifact("magma")
    real = artifact("java-1.11.2")
    real["checkpoint_sha256"] = "c" * 64
    with pytest.raises(ValueError, match="checkpoint_sha256"):
        pair_results(sim, real)


def test_rejects_missing_java_action_ack():
    sim = artifact("magma")
    real = artifact("java-1.11.2")
    real = copy.deepcopy(real)
    real["attempts"][0]["bridge_action_seq"] = 39
    with pytest.raises(ValueError, match="acknowledgements"):
        pair_results(sim, real)
