"""Shared helpers for frame-exact action recording and replay."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path

from netherite_env import (
    WORLD_SAMPLE_MAX_DY,
    WORLD_SAMPLE_MIN_DY,
    WORLD_SAMPLE_RADIUS_XZ,
    WORLD_SAMPLE_STEP_XZ,
)


ZERO_ENV_ACTION = {
    "forward": 0,
    "back": 0,
    "left": 0,
    "right": 0,
    "jump": 0,
    "sneak": 0,
    "sprint": 0,
    "attack": 0,
    "use": 0,
    "camera": [0, 0],
}
FULL_CHUNK_SAMPLE_COUNT = 25
PHYSICS_FLOAT_FIELDS = (
    "yaw",
    "pitch",
    "health",
    "max_health",
    "saturation",
)
PHYSICS_INT_FIELDS = (
    "food",
    "on_ground",
    "in_water",
    "world_fingerprint",
    "loaded_chunks",
    "chunk_mask",
    "actual_world_seed",
    "state_tick",
)


def frame_digest(frame) -> str:
    return hashlib.blake2b(frame.tobytes(), digest_size=16).hexdigest()


def sample_step_path(root_dir: Path, phase: str, step: int) -> Path:
    return root_dir / phase / f"step_{step:06d}.png"


def sample_state_path(root_dir: Path, phase: str, step: int) -> Path:
    return root_dir / phase / f"step_{step:06d}.json"


def sample_debug_state(env) -> dict[str, object]:
    return env.get_debug_state()


def physics_debug_state(state: dict[str, object]) -> dict[str, object]:
    position = state.get("position", [0.0, 0.0, 0.0])
    return {
        "position": [float(position[0]), float(position[1]), float(position[2])],
        "yaw": float(state.get("yaw", 0.0)),
        "pitch": float(state.get("pitch", 0.0)),
        "health": float(state.get("health", 0.0)),
        "max_health": float(state.get("max_health", 0.0)),
        "food": int(state.get("food", 0)),
        "saturation": float(state.get("saturation", 0.0)),
        "on_ground": int(state.get("on_ground", 0)),
        "in_water": int(state.get("in_water", 0)),
        "world_fingerprint": int(state.get("world_fingerprint", 0)),
        "loaded_chunks": int(state.get("loaded_chunks", 0)),
        "chunk_mask": int(state.get("chunk_mask", 0)),
        "actual_world_seed": int(state.get("actual_world_seed", 0)),
        "state_tick": int(state.get("state_tick", -1)),
        "world_sample": [int(value) for value in state.get("world_sample", [])],
        "server_world_fingerprint": int(state.get("server_world_fingerprint", 0)),
        "server_world_sample": [int(value) for value in state.get("server_world_sample", [])],
    }


def compare_physics_states(
    expected: dict[str, object],
    actual: dict[str, object],
    *,
    pos_atol: float = 1e-6,
    float_atol: float = 1e-6,
) -> dict[str, tuple[object, object]]:
    expected_norm = physics_debug_state(expected)
    actual_norm = physics_debug_state(actual)
    diffs: dict[str, tuple[object, object]] = {}

    expected_pos = expected_norm["position"]
    actual_pos = actual_norm["position"]
    if any(abs(float(exp) - float(act)) > pos_atol for exp, act in zip(expected_pos, actual_pos)):
        diffs["position"] = (expected_pos, actual_pos)

    for key in PHYSICS_FLOAT_FIELDS:
        if abs(float(expected_norm[key]) - float(actual_norm[key])) > float_atol:
            diffs[key] = (expected_norm[key], actual_norm[key])

    for key in PHYSICS_INT_FIELDS:
        if int(expected_norm[key]) != int(actual_norm[key]):
            diffs[key] = (expected_norm[key], actual_norm[key])

    return diffs


def world_sample_offsets() -> list[tuple[int, int, int]]:
    offsets: list[tuple[int, int, int]] = []
    for dy in range(WORLD_SAMPLE_MIN_DY, WORLD_SAMPLE_MAX_DY + 1):
        for dz in range(-WORLD_SAMPLE_RADIUS_XZ, WORLD_SAMPLE_RADIUS_XZ + 1, WORLD_SAMPLE_STEP_XZ):
            for dx in range(-WORLD_SAMPLE_RADIUS_XZ, WORLD_SAMPLE_RADIUS_XZ + 1, WORLD_SAMPLE_STEP_XZ):
                offsets.append((dx, dy, dz))
    return offsets


def compare_world_samples(
    expected: dict[str, object],
    actual: dict[str, object],
    *,
    max_diffs: int = 12,
) -> list[dict[str, int]]:
    expected_sample = physics_debug_state(expected).get("world_sample", [])
    actual_sample = physics_debug_state(actual).get("world_sample", [])
    count = min(len(expected_sample), len(actual_sample))
    offsets = world_sample_offsets()
    diffs: list[dict[str, int]] = []

    for index in range(min(count, len(offsets))):
        expected_value = int(expected_sample[index])
        actual_value = int(actual_sample[index])
        if expected_value == actual_value:
            continue
        dx, dy, dz = offsets[index]
        diffs.append(
            {
                "index": index,
                "dx": dx,
                "dy": dy,
                "dz": dz,
                "expected": expected_value,
                "actual": actual_value,
            }
        )
        if len(diffs) >= max_diffs:
            break

    if len(expected_sample) != len(actual_sample):
        diffs.append(
            {
                "index": -1,
                "dx": 0,
                "dy": 0,
                "dz": 0,
                "expected": len(expected_sample),
                "actual": len(actual_sample),
            }
        )
    return diffs


def compare_server_world_samples(
    expected: dict[str, object],
    actual: dict[str, object],
    *,
    max_diffs: int = 12,
) -> list[dict[str, int]]:
    expected_sample = physics_debug_state(expected).get("server_world_sample", [])
    actual_sample = physics_debug_state(actual).get("server_world_sample", [])
    count = min(len(expected_sample), len(actual_sample))
    offsets = world_sample_offsets()
    diffs: list[dict[str, int]] = []

    for index in range(min(count, len(offsets))):
        expected_value = int(expected_sample[index])
        actual_value = int(actual_sample[index])
        if expected_value == actual_value:
            continue
        dx, dy, dz = offsets[index]
        diffs.append(
            {
                "index": index,
                "dx": dx,
                "dy": dy,
                "dz": dz,
                "expected": expected_value,
                "actual": actual_value,
            }
        )
        if len(diffs) >= max_diffs:
            break

    if len(expected_sample) != len(actual_sample):
        diffs.append(
            {
                "index": -1,
                "dx": 0,
                "dy": 0,
                "dz": 0,
                "expected": len(expected_sample),
                "actual": len(actual_sample),
            }
        )
    return diffs


def save_state_sample(
    *,
    root_dir: Path | None,
    phase: str,
    step: int,
    state: dict[str, object],
    every: int,
) -> Path | None:
    if root_dir is None or every <= 0 or step % every != 0:
        return None

    output_path = sample_state_path(root_dir, phase, step)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    output_path.write_text(json.dumps(state, sort_keys=True) + "\n", encoding="utf-8")
    return output_path


def save_frame_sample(
    *,
    root_dir: Path | None,
    phase: str,
    step: int,
    frame,
    every: int,
) -> Path | None:
    if root_dir is None or every <= 0 or step % every != 0:
        return None

    import os

    os.environ.setdefault("PYGAME_HIDE_SUPPORT_PROMPT", "hide")
    import pygame

    output_path = sample_step_path(root_dir, phase, step)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    surface = pygame.surfarray.make_surface(frame.transpose((1, 0, 2)))
    pygame.image.save(surface, str(output_path))
    return output_path


def _load_sample_state(path: Path) -> dict[str, object] | None:
    if not path.exists():
        return None
    return json.loads(path.read_text(encoding="utf-8"))


def _state_summary_html(state: dict[str, object] | None) -> str:
    if state is None:
        return "<div class=\"missing\">state missing</div>"

    position = state.get("position", [0.0, 0.0, 0.0])
    control = state.get("control") or {}
    return (
        "<pre>"
        f"pos=({position[0]:.2f}, {position[1]:.2f}, {position[2]:.2f})\n"
        f"yaw={state.get('yaw', 0.0):.2f} pitch={state.get('pitch', 0.0):.2f}\n"
        f"hp={state.get('health', 0.0):.1f}/{state.get('max_health', 0.0):.1f} "
        f"food={state.get('food', 0)} sat={state.get('saturation', 0.0):.1f}\n"
        f"ground={state.get('on_ground', 0)} water={state.get('in_water', 0)}\n"
        f"world={state.get('world_fingerprint', 0):016x} "
        f"chunks={state.get('loaded_chunks', 0)} mask={state.get('chunk_mask', 0):08x}\n"
        f"render={state.get('completed_render_chunks', 0)}/{state.get('total_render_chunks', 0)}\n"
        f"actual_seed={state.get('actual_world_seed', 0)}\n"
        f"tick={state.get('state_tick', -1)} episode={control.get('episode_id', -1)} "
        f"seed={control.get('active_seed', 'n/a')}"
        "</pre>"
    )


def write_sample_index(root_dir: Path | None) -> Path | None:
    if root_dir is None:
        return None

    record_dir = root_dir / "record"
    replay_dir = root_dir / "replay"
    samples: dict[int, dict[str, Path]] = {}

    for phase, phase_dir in (("record", record_dir), ("replay", replay_dir)):
        if not phase_dir.exists():
            continue
        for path in sorted(phase_dir.glob("step_*.png")):
            step = int(path.stem.split("_", maxsplit=1)[1])
            entry = samples.setdefault(step, {})
            entry[phase] = path

    index_path = root_dir / "index.html"
    lines = [
        "<!doctype html>",
        "<html lang=\"en\">",
        "<head>",
        "<meta charset=\"utf-8\">",
        "<title>Netherite Frame Samples</title>",
        "<style>",
        "body { font-family: sans-serif; margin: 24px; }",
        "table { border-collapse: collapse; }",
        "th, td { border: 1px solid #ccc; padding: 8px; vertical-align: top; }",
        "img { image-rendering: pixelated; width: 480px; border: 1px solid #222; }",
        "pre { background: #f7f7f7; padding: 8px; margin: 8px 0 0; }",
        ".missing { color: #777; font-style: italic; }",
        "</style>",
        "</head>",
        "<body>",
        "<h1>Netherite Frame Samples</h1>",
        "<table>",
        "<tr><th>Step</th><th>Record</th><th>Replay</th></tr>",
    ]

    for step in sorted(samples):
        entry = samples[step]
        lines.append(f"<tr><td>{step}</td>")
        for phase in ("record", "replay"):
            image_path = entry.get(phase)
            state_path = sample_state_path(root_dir, phase, step)
            state = _load_sample_state(state_path)
            if image_path is None:
                lines.append("<td><span class=\"missing\">missing</span></td>")
                continue
            rel_path = image_path.relative_to(root_dir).as_posix()
            rel_state_path = state_path.relative_to(root_dir).as_posix()
            lines.append(
                f"<td><div>{rel_path}</div><div>{rel_state_path}</div>"
                f"<img src=\"{rel_path}\" alt=\"{phase} step {step}\">"
                f"{_state_summary_html(state)}</td>"
            )
        lines.append("</tr>")

    lines.extend(
        [
            "</table>",
            "</body>",
            "</html>",
        ]
    )
    index_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
    return index_path


def clone_action(action: dict[str, int | list[int]]) -> dict[str, int | list[int]]:
    return {
        key: list(value) if isinstance(value, list) else int(value)
        for key, value in action.items()
    }


def zero_action() -> dict[str, int | list[int]]:
    return {
        "ESC": 0,
        "attack": 0,
        "back": 0,
        "camera": [0, 0],
        "drop": 0,
        "forward": 0,
        "hotbar.1": 0,
        "hotbar.2": 0,
        "hotbar.3": 0,
        "hotbar.4": 0,
        "hotbar.5": 0,
        "hotbar.6": 0,
        "hotbar.7": 0,
        "hotbar.8": 0,
        "hotbar.9": 0,
        "inventory": 0,
        "jump": 0,
        "left": 0,
        "pickItem": 0,
        "right": 0,
        "sneak": 0,
        "sprint": 0,
        "swapHands": 0,
        "use": 0,
    }


def pose_matches(
    current_pose: dict[str, float],
    target_pose: dict[str, float],
    *,
    pos_tol: float = 0.01,
    ang_tol: float = 0.01,
) -> bool:
    return (
        abs(current_pose["x"] - target_pose["x"]) < pos_tol
        and abs(current_pose["y"] - target_pose["y"]) < pos_tol
        and abs(current_pose["z"] - target_pose["z"]) < pos_tol
        and abs(current_pose["yaw"] - target_pose["yaw"]) < ang_tol
        and abs(current_pose["pitch"] - target_pose["pitch"]) < ang_tol
    )


def world_signature_matches(
    state: dict[str, object],
    *,
    target_world_fingerprint: int | None = None,
    target_chunk_mask: int | None = None,
    min_loaded_chunks: int | None = None,
) -> bool:
    if target_world_fingerprint is not None and int(state.get("world_fingerprint", 0)) != target_world_fingerprint:
        return False
    if target_chunk_mask is not None and int(state.get("chunk_mask", 0)) != target_chunk_mask:
        return False
    if min_loaded_chunks is not None and int(state.get("loaded_chunks", 0)) < min_loaded_chunks:
        return False
    return True


def pose_from_debug_state(state: dict[str, object]) -> dict[str, float]:
    position = state["position"]
    return {
        "x": float(position[0]),
        "y": float(position[1]),
        "z": float(position[2]),
        "yaw": float(state["yaw"]),
        "pitch": float(state["pitch"]),
    }


def canonicalize_initial_frame(
    env,
    *,
    target_pose: dict[str, float] | None = None,
    target_world_fingerprint: int | None = None,
    target_chunk_mask: int | None = None,
    min_loaded_chunks: int | None = None,
    stable_frames: int = 8,
    max_steps: int = 512,
) -> tuple[dict, dict[str, float], int]:
    step_fn = getattr(env, "step_sync", env.step)

    debug_state = env.get_debug_state()
    pose = pose_from_debug_state(debug_state)

    if target_pose is None:
        obs = env._get_obs(wait_for_new_state=True, wait_for_new_frame=True)
    elif pose_matches(pose, target_pose):
        obs = env._get_obs(wait_for_new_state=True, wait_for_new_frame=True)
    else:
        obs = env.align_to_pose(target_pose)
        debug_state = env.get_debug_state()
        pose = pose_from_debug_state(debug_state)

    last_hash = frame_digest(obs["pov"])
    stable_count = 1

    for warmup_steps in range(max_steps):
        if target_pose is not None and not pose_matches(pose, target_pose):
            obs = env.align_to_pose(target_pose)
            debug_state = env.get_debug_state()
            pose = pose_from_debug_state(debug_state)
            last_hash = frame_digest(obs["pov"])
            stable_count = 1
            continue

        obs, _, _, _, _ = step_fn(ZERO_ENV_ACTION)
        debug_state = env.get_debug_state()
        pose = pose_from_debug_state(debug_state)
        frame_hash = frame_digest(obs["pov"])

        signature_ok = world_signature_matches(
            debug_state,
            target_world_fingerprint=target_world_fingerprint,
            target_chunk_mask=target_chunk_mask,
            min_loaded_chunks=min_loaded_chunks,
        )

        if frame_hash == last_hash and signature_ok and (target_pose is None or pose_matches(pose, target_pose)):
            stable_count += 1
            if stable_count >= stable_frames:
                return obs, pose, warmup_steps + 1
        else:
            last_hash = frame_hash
            stable_count = 1

    raise RuntimeError("Timed out waiting for a stable canonical start frame")
