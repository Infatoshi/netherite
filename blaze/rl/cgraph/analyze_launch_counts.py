#!/usr/bin/env python3
"""Create the launch-count receipt from the two Nsight SQLite exports."""

import argparse
import json
import sqlite3
from pathlib import Path


def launch_counts(database, bounds=None):
    query = """
        SELECT strings.value, count(*)
        FROM CUPTI_ACTIVITY_KIND_RUNTIME AS runtime
        JOIN StringIds AS strings ON strings.id = runtime.nameId
    """
    parameters = ()
    if bounds is not None:
        query += " WHERE runtime.start < ? AND runtime.end > ?"
        parameters = (bounds[1], bounds[0])
    query += " GROUP BY strings.value"
    with sqlite3.connect(database) as connection:
        rows = connection.execute(query, parameters)
        return {
            name: count for name, count in rows
            if "Launch" in name
        }


def nvtx_bounds(database, name):
    with sqlite3.connect(database) as connection:
        row = connection.execute(
            "SELECT start, end FROM NVTX_EVENTS WHERE text = ?", (name,)
        ).fetchone()
    if row is None:
        raise RuntimeError(f"NVTX range not found: {name}")
    return row


def graph_launches_by_phase(database):
    query = """
        SELECT ranges.text, count(*)
        FROM CUPTI_ACTIVITY_KIND_RUNTIME AS runtime
        JOIN StringIds AS strings ON strings.id = runtime.nameId
        JOIN NVTX_EVENTS AS ranges
          ON runtime.start < ranges.end AND runtime.end > ranges.start
        WHERE strings.value LIKE 'cudaGraphLaunch%'
          AND ranges.text IN ('cgraph/rollout_policy',
                              'cgraph/ppo_forward_loss')
        GROUP BY ranges.text
    """
    with sqlite3.connect(database) as connection:
        return dict(connection.execute(query))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("baseline", type=Path)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--baseline-range", default="cgraph_ceiling/chunk/2")
    args = parser.parse_args()

    baseline = launch_counts(
        args.baseline, nvtx_bounds(args.baseline, args.baseline_range)
    )
    candidate = launch_counts(args.candidate)
    phases = graph_launches_by_phase(args.candidate)
    baseline_graph = sum(
        count for name, count in baseline.items() if "GraphLaunch" in name
    )
    candidate_graph = sum(
        count for name, count in candidate.items() if "GraphLaunch" in name
    )
    receipt = {
        "schema": "netherite.cgraph-launch-proof.v1",
        "baseline_nvtx_range": args.baseline_range,
        "baseline_launch_api_by_symbol": baseline,
        "baseline_direct_launch_api_calls": sum(baseline.values())
        - baseline_graph,
        "candidate_launch_api_by_symbol": candidate,
        "candidate_direct_launch_api_calls": sum(candidate.values())
        - candidate_graph,
        "candidate_graph_launch_api_calls": candidate_graph,
        "candidate_rollout_graph_launches": phases.get(
            "cgraph/rollout_policy", 0
        ),
        "candidate_update_graph_launches": phases.get(
            "cgraph/ppo_forward_loss", 0
        ),
    }
    args.output.write_text(json.dumps(receipt, indent=2, sort_keys=True) + "\n")
    print(json.dumps(receipt, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
