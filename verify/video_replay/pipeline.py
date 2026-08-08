#!/usr/bin/env python3
"""One-command, resumable video-to-Netherite tape extraction."""
import argparse
import hashlib
import json
import pathlib
import types

from auto_search import write_json
from beam_search import run as run_beam
from video_features import extract as extract_features


SCHEMA = 3


def file_sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def main():
    parser = argparse.ArgumentParser(
        description="Recover a continuous input tape from video by native "
                    "simulation search; safe to interrupt and resume.")
    parser.add_argument("video", type=pathlib.Path)
    parser.add_argument("--seed", type=int, required=True)
    parser.add_argument("--workspace", type=pathlib.Path, required=True)
    parser.add_argument("--game", type=pathlib.Path,
                        default=pathlib.Path("magma/magma_game"))
    parser.add_argument("--fps", type=float, default=10.0)
    parser.add_argument("--workers", type=int, default=32)
    parser.add_argument("--candidates", type=int, default=192)
    parser.add_argument("--beam-width", type=int, default=12)
    parser.add_argument("--horizon-ticks", type=int, default=10)
    parser.add_argument("--checkpoint-history", type=int, default=3)
    parser.add_argument("--max-generations", type=int, default=0)
    parser.add_argument("--restart-search", action="store_true")
    args = parser.parse_args()

    video = args.video.resolve()
    game = args.game.resolve()
    workspace = args.workspace.resolve()
    if not video.is_file():
        parser.error(f"video does not exist: {video}")
    if not game.is_file():
        parser.error(f"engine does not exist: {game}; run make -C magma game")
    if not (0 < args.fps <= 20) or args.workers < 1:
        parser.error("fps must be in (0,20] and workers must be positive")
    workspace.mkdir(parents=True, exist_ok=True)
    manifest_path = workspace / "manifest.json"
    manifest = {
        "schema": SCHEMA, "kind": "video_search_manifest",
        "video": str(video), "video_sha256": file_sha256(video),
        "seed": args.seed, "fps": args.fps,
        "engine": str(game), "algorithm": "checkpoint_beam_v3",
        "horizon_ticks": args.horizon_ticks,
    }
    if manifest_path.exists():
        previous = json.loads(manifest_path.read_text())
        if previous != manifest:
            raise SystemExit(
                "workspace manifest differs from this source/configuration; "
                "use a new workspace")
    else:
        write_json(manifest_path, manifest)

    features = workspace / "video_features.npz"
    if not features.exists():
        print(json.dumps({"stage": "features", "status": "starting"}),
              flush=True)
        report = extract_features(video, features, args.fps)
        print(json.dumps({"stage": "features", "status": "complete",
                          "report": report}, sort_keys=True), flush=True)

    beam_args = types.SimpleNamespace(
        features=features, workspace=workspace / "beam",
        checkpoints=workspace / "beam" / "checkpoints", game=game,
        seed=args.seed, spawn_offset_x=-7, spawn_offset_z=-10,
        view_distance=2, fovs=[70.0, 90.0, 110.0], workers=args.workers,
        candidates=args.candidates, beam_width=args.beam_width,
        horizon_ticks=args.horizon_ticks, bootstrap_candidates=0,
        bootstrap_yaw_step=30, max_generations=args.max_generations,
        rewind_generation=None, restart=args.restart_search,
        checkpoint_history=args.checkpoint_history,
    )
    state = run_beam(beam_args)
    print(json.dumps({"stage": "search", "state": state}, sort_keys=True))


if __name__ == "__main__":
    main()
