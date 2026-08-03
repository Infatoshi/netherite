"""Collect reproducible receipts for the archived Java pilot interface audit.

This is intentionally offline. It reads the pinned d55 source, t0 snapshots,
and archived Java pilot artifacts. It never starts Minecraft or a GPU env.
"""

from __future__ import annotations

import argparse
import ctypes
import gzip
import hashlib
import json
import math
import re
import struct
import zlib
from collections import Counter
from pathlib import Path

import numpy as np

DEFAULT_ARCHIVE = Path(
    "/home/infatoshi/dev/netherite-artifacts/branch-archive/"
    "ppo-native-bf16-20260802/optloop_runs/ppo-native-bf16-d55-v4/"
    "final/java_pilot"
)
DEFAULT_REFERENCE = Path("/home/infatoshi/dev/nw/paper-t3")
SEEDS = (2, 3, 10)
PLANE_NAMES = (
    "log",
    "leaves",
    "coal_ore",
    "stone_or_cobble",
    "grass_or_dirt",
    "crafting_table",
    "solid",
    "depth",
    "edge",
)
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
TIME_RE = re.compile(r"\[(\d\d):(\d\d):(\d\d)\]")
HEARTBEAT_RE = re.compile(r"server tick heartbeat (\d+)")
DEC1400_RE = re.compile(r"dec\s+1400\s+([0-9.]+) t/s")


class SnapHead(ctypes.LittleEndianStructure):
    _pack_ = 1
    _fields_ = [  # noqa: RUF012 - ctypes requires this mutable class descriptor.
        ("magic", ctypes.c_char * 4),
        ("version", ctypes.c_uint32),
        ("seed", ctypes.c_int64),
        ("tick", ctypes.c_int64),
        ("ox", ctypes.c_int32),
        ("oz", ctypes.c_int32),
        ("px", ctypes.c_double),
        ("py", ctypes.c_double),
        ("pz", ctypes.c_double),
        ("box", ctypes.c_double * 6),
        ("yaw", ctypes.c_float),
        ("pitch", ctypes.c_float),
        ("mx", ctypes.c_double),
        ("my", ctypes.c_double),
        ("mz", ctypes.c_double),
        ("on_ground", ctypes.c_int32),
        ("collided_h", ctypes.c_int32),
        ("collided_v", ctypes.c_int32),
        ("is_collided", ctypes.c_int32),
        ("fall_distance", ctypes.c_float),
        ("sprinting", ctypes.c_int32),
        ("sprint_toggle_timer", ctypes.c_int32),
        ("jump_factor_sprint", ctypes.c_int32),
        ("jump_ticks", ctypes.c_int32),
        ("prev_move_forward", ctypes.c_float),
        ("prev_sneak", ctypes.c_int32),
        ("health", ctypes.c_float),
        ("food", ctypes.c_int32),
        ("saturation", ctypes.c_float),
        ("exhaustion", ctypes.c_float),
        ("food_timer", ctypes.c_int32),
        ("dig_progress", ctypes.c_float),
        ("dig_hx", ctypes.c_int32),
        ("dig_hy", ctypes.c_int32),
        ("dig_hz", ctypes.c_int32),
        ("dig_hitting", ctypes.c_int32),
        ("dig_delay", ctypes.c_int32),
        ("atk_prev", ctypes.c_int32),
        ("rc_delay", ctypes.c_int32),
        ("use_prev", ctypes.c_int32),
        ("hurt_vel_reset", ctypes.c_int32),
        ("server_motion_x", ctypes.c_double),
        ("server_motion_z", ctypes.c_double),
        ("container", ctypes.c_int32),
        ("container_wx", ctypes.c_int32),
        ("container_wy", ctypes.c_int32),
        ("container_wz", ctypes.c_int32),
        ("world_dirty", ctypes.c_int32),
        ("hotbar_sel", ctypes.c_int32),
        ("inv", (ctypes.c_int32 * 3) * 37),
        ("n_items", ctypes.c_uint32),
        ("rx0", ctypes.c_int32),
        ("ry0", ctypes.c_int32),
        ("rz0", ctypes.c_int32),
        ("rnx", ctypes.c_int32),
        ("rny", ctypes.c_int32),
        ("rnz", ctypes.c_int32),
    ]


assert ctypes.sizeof(SnapHead) == 752
ITEM_SIZE = 76


class NbtReader:
    """Small read-only NBT decoder sufficient for archived level.dat files."""

    def __init__(self, data: bytes):
        self.data = data
        self.pos = 0

    def take(self, count: int) -> bytes:
        out = self.data[self.pos : self.pos + count]
        if len(out) != count:
            raise ValueError("truncated NBT")
        self.pos += count
        return out

    def num(self, fmt: str):
        size = struct.calcsize(fmt)
        return struct.unpack(fmt, self.take(size))[0]

    def string(self) -> str:
        length = self.num(">H")
        start = self.pos
        raw = self.take(length)
        try:
            return raw.decode("utf-8")
        except UnicodeDecodeError as exc:
            raise ValueError(
                f"invalid NBT string at offset {start}, length {length}, "
                f"prefix={raw[:24].hex()}"
            ) from exc

    def payload(self, tag: int):
        if tag == 1:
            return self.num(">b")
        if tag == 2:
            return self.num(">h")
        if tag == 3:
            return self.num(">i")
        if tag == 4:
            return self.num(">q")
        if tag == 5:
            return self.num(">f")
        if tag == 6:
            return self.num(">d")
        if tag == 7:
            return self.take(self.num(">i"))
        if tag == 8:
            return self.string()
        if tag == 9:
            subtype, count = self.num(">b"), self.num(">i")
            return [self.payload(subtype) for _ in range(count)]
        if tag == 10:
            out = {}
            while True:
                subtype = self.num(">b")
                if subtype == 0:
                    return out
                name = self.string()
                out[name] = self.payload(subtype)
        if tag == 11:
            return [self.num(">i") for _ in range(self.num(">i"))]
        if tag == 12:
            return [self.num(">q") for _ in range(self.num(">i"))]
        raise ValueError(f"unsupported NBT tag {tag}")

    def root(self):
        tag = self.num(">b")
        if tag != 10:
            raise ValueError(f"NBT root is {tag}, expected compound")
        self.string()
        return self.payload(tag)


def load_level(path: Path) -> dict:
    reader = NbtReader(gzip.decompress(path.read_bytes()))
    return reader.root()["Data"]


def load_mca_chunk(region_dir: Path, chunk_x: int, chunk_z: int) -> np.ndarray:
    region_path = region_dir / f"r.{chunk_x >> 5}.{chunk_z >> 5}.mca"
    raw = region_path.read_bytes()
    location_index = (chunk_x & 31) + (chunk_z & 31) * 32
    location = raw[location_index * 4 : location_index * 4 + 4]
    sector = int.from_bytes(location[:3], "big")
    if sector == 0:
        raise ValueError(f"missing chunk {chunk_x},{chunk_z} in {region_path}")
    offset = sector * 4096
    length = int.from_bytes(raw[offset : offset + 4], "big")
    compression = raw[offset + 4]
    payload = raw[offset + 5 : offset + 4 + length]
    if compression == 1:
        decoded = gzip.decompress(payload)
    elif compression == 2:
        decoded = zlib.decompress(payload)
    else:
        raise ValueError(f"unsupported MCA compression {compression}")
    root = NbtReader(decoded).root()
    sections = root["Level"].get("Sections", [])
    blocks = np.zeros((16, 256, 16), dtype=np.uint16)
    for section in sections:
        section_y = int(section["Y"])
        if not 0 <= section_y < 16:
            continue
        base = np.frombuffer(section["Blocks"], dtype=np.uint8).astype(np.uint16)
        add_raw = section.get("Add")
        if add_raw:
            packed = np.frombuffer(add_raw, dtype=np.uint8)
            add = np.empty(4096, dtype=np.uint16)
            add[0::2] = packed & 0x0F
            add[1::2] = packed >> 4
            base |= add << 8
        blocks[:, section_y * 16 : section_y * 16 + 16, :] = base.reshape(
            16, 16, 16
        ).transpose(2, 0, 1)
    return blocks


def compare_relative_block_window(
    head: SnapHead, ids: np.ndarray, region_dir: Path, java_pose: list[float]
) -> dict:
    snapshot_pose = [head.px + head.ox, head.py, head.pz + head.oz]
    snapshot_floor = [math.floor(value) for value in snapshot_pose]
    java_floor = [math.floor(value) for value in java_pose]
    chunks: dict[tuple[int, int], np.ndarray | None] = {}
    snapshot_values = []
    java_values = []
    intended_cells = 33 * 65 * 33
    unavailable_cells = 0
    for dx in range(-16, 17):
        for dy in range(-24, 41):
            for dz in range(-16, 17):
                wx, wy, wz = (
                    java_floor[0] + dx,
                    java_floor[1] + dy,
                    java_floor[2] + dz,
                )
                chunk_key = (wx >> 4, wz >> 4)
                if chunk_key not in chunks:
                    try:
                        chunks[chunk_key] = load_mca_chunk(region_dir, *chunk_key)
                    except ValueError:
                        chunks[chunk_key] = None
                if chunks[chunk_key] is None:
                    unavailable_cells += 1
                    continue
                snapshot_values.append(
                    block(
                        ids,
                        head,
                        snapshot_floor[0] + dx,
                        snapshot_floor[1] + dy,
                        snapshot_floor[2] + dz,
                    )
                )
                chunk = chunks[chunk_key]
                assert chunk is not None
                java_values.append(
                    int(chunk[wx & 15, wy, wz & 15])
                    if 0 <= wy < 256
                    else 0
                )
    snapshot_array = np.asarray(snapshot_values, dtype="<u2")
    java_array = np.asarray(java_values, dtype="<u2")
    matching = int(np.count_nonzero(snapshot_array == java_array))
    categories = (0, 1, 2, 3, 4, 16, 17, 18, 58)
    return {
        "scope": (
            "Relative dx[-16,16], dy[-24,40], dz[-16,16] block-ID window. "
            "Java source is the final archived fifth-attempt save, not an initial-state dump."
        ),
        "intended_cells": intended_cells,
        "compared_cells": int(snapshot_array.size),
        "unavailable_final_save_cells": unavailable_cells,
        "matching_block_id_cells": matching,
        "differing_block_id_cells": int(snapshot_array.size - matching),
        "matching_fraction": matching / snapshot_array.size,
        "snapshot_sha256": hashlib.sha256(snapshot_array.tobytes()).hexdigest(),
        "archived_java_final_save_sha256": hashlib.sha256(
            java_array.tobytes()
        ).hexdigest(),
        "selected_id_counts": {
            str(block_id): {
                "snapshot": int(np.count_nonzero(snapshot_array == block_id)),
                "archived_java_final_save": int(
                    np.count_nonzero(java_array == block_id)
                ),
            }
            for block_id in categories
        },
    }


def json_dump(path: Path, value) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, sort_keys=True) + "\n")


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def seconds(match: re.Match[str]) -> int:
    return int(match[1]) * 3600 + int(match[2]) * 60 + int(match[3])


def pilot_runtime(archive: Path) -> dict:
    java = json.loads((archive / "java.json").read_text())
    receipt = json.loads((archive / "receipt.json").read_text())
    sim_stub = json.loads((archive / "sim_stub.json").read_text())
    attempts = java["attempts"]
    by_seed = {}
    for seed in SEEDS:
        rows = [row for row in attempts if row["seed"] == seed]
        by_seed[str(seed)] = {
            "attempts": len(rows),
            "actions_sent": [row["actions_sent"] for row in rows],
            "non_noop_steps": [row["non_noop_steps"] for row in rows],
            "deaths_inferred_from_short_episode": sum(
                row["actions_sent"] < java["ep_ticks"] for row in rows
            ),
            "best_inventory_max": [
                max(row["best_inv_counts"][index] for row in rows)
                for index in range(9)
            ],
        }

    terminal_rates = []
    for path in sorted((archive / "logs").glob("seed_*.log")):
        terminal_rates.extend(
            float(match.group(1))
            for match in map(DEC1400_RE.search, path.read_text().splitlines())
            if match
        )

    raw = (archive / "client0.log").read_text(errors="replace")
    clean = ANSI_RE.sub("", raw)
    heartbeats = []
    for line in clean.splitlines():
        tm = TIME_RE.search(line)
        hb = HEARTBEAT_RE.search(line)
        if tm and hb:
            heartbeats.append((seconds(tm), int(hb.group(1))))
    segments: list[list[tuple[int, int]]] = []
    for point in heartbeats:
        if not segments:
            segments.append([point])
            continue
        last = segments[-1][-1]
        if point[1] - last[1] != 1200 or point[0] - last[0] > 5:
            segments.append([point])
        else:
            segments[-1].append(point)
    heartbeat_rates = []
    for segment in segments:
        dt = segment[-1][0] - segment[0][0]
        if len(segment) >= 10 and dt > 0:
            heartbeat_rates.append((segment[-1][1] - segment[0][1]) / dt)

    fatal_by_time = Counter(
        match.group(1)
        for match in re.finditer(
            r"\[(\d\d:\d\d:\d\d)\] \[Client thread/FATAL\]: Error executing task",
            clean,
        )
    )
    rate_summary = None
    ratio_summary = None
    if heartbeat_rates:
        rate_summary = {
            "n_contiguous_segments": len(heartbeat_rates),
            "min": min(heartbeat_rates),
            "median": float(np.median(heartbeat_rates)),
            "max": max(heartbeat_rates),
        }
    if terminal_rates and rate_summary:
        server_rate = rate_summary["median"]
        ratio_summary = {
            "server_ticks_per_bridge_action_at_fastest_full_episode": server_rate
            / max(terminal_rates),
            "server_ticks_per_bridge_action_at_slowest_full_episode": server_rate
            / min(terminal_rates),
        }

    return {
        "artifact": {
            "commit": java["commit"],
            "tracked_clean": java["tracked_clean"],
            "checkpoint_sha256": java["checkpoint_sha256"],
            "n_attempts": len(attempts),
            "actions_total": sum(row["actions_sent"] for row in attempts),
            "non_noop_total": sum(row["non_noop_steps"] for row in attempts),
            "successes": sum(bool(row["success"]) for row in attempts),
            "attempts_action_sequence_exact": sum(
                row["bridge_action_seq"] == row["actions_sent"] for row in attempts
            ),
            "attempts_action_digest_exact": sum(
                row["bridge_action_fnv64"] == row["local_action_fnv64"]
                for row in attempts
            ),
            "attempts_world_seed_exact": sum(
                row["world_seed"] == row["seed"] for row in attempts
            ),
            "per_head_action_histograms_available": False,
            "receipt_merge_tool_error": receipt.get("merge_tool_error"),
            "sim_stub_note": sim_stub.get("pilot_note"),
        },
        "by_seed": by_seed,
        "bridge_action_rate_full_episodes_per_second": {
            "samples_dec1400": terminal_rates,
            "min": min(terminal_rates) if terminal_rates else None,
            "median": float(np.median(terminal_rates)) if terminal_rates else None,
            "max": max(terminal_rates) if terminal_rates else None,
        },
        "server_heartbeat_ticks_per_second": rate_summary,
        "server_ticks_per_bridge_action": ratio_summary,
        "client_log": {
            "heartbeat_lines": len(heartbeats),
            "fresh_world_launches": clean.count("[qrl] launching world qrl_"),
            "launch_settings_applied": clean.count("[qrl] launch settings applied"),
            "moved_wrongly_warnings": clean.count("moved wrongly!"),
            "fatal_error_executing_task": clean.count(
                "[Client thread/FATAL]: Error executing task"
            ),
            "fatal_npe_handle_statistics": clean.count(
                "NetHandlerPlayClient.handleStatistics"
            ),
            "fatal_clusters": dict(sorted(fatal_by_time.items())),
        },
    }


def load_snapshot(path: Path):
    data = path.read_bytes()
    head = SnapHead.from_buffer_copy(data[: ctypes.sizeof(SnapHead)])
    if bytes(head.magic) != b"BSNP" or head.version != 1:
        raise ValueError(f"bad snapshot header: {path}")
    count = head.rnx * head.rny * head.rnz
    offset = ctypes.sizeof(SnapHead) + ITEM_SIZE * head.n_items
    packed = np.frombuffer(data, dtype="<u2", count=count, offset=offset)
    ids = (packed >> 4).reshape(head.rnx, head.rny, head.rnz)
    return head, ids


def sin_table() -> np.ndarray:
    return np.asarray(
        [np.float32(math.sin(index * math.pi * 2.0 / 65536.0)) for index in range(65536)],
        dtype=np.float32,
    )


def lut_sin(table: np.ndarray, value: np.float32) -> np.float32:
    index = int(np.float32(value * np.float32(10430.378))) & 65535
    return table[index]


def lut_cos(table: np.ndarray, value: np.float32) -> np.float32:
    index = int(
        np.float32(np.float32(value * np.float32(10430.378)) + np.float32(16384.0))
    ) & 65535
    return table[index]


def block(ids: np.ndarray, head: SnapHead, x: int, y: int, z: int) -> int:
    ix, iy, iz = x - head.rx0, y - head.ry0, z - head.rz0
    if ix < 0 or iy < 0 or iz < 0:
        return 0
    if ix >= head.rnx or iy >= head.rny or iz >= head.rnz:
        return 0
    return int(ids[ix, iy, iz])


def render_camera(head: SnapHead, ids: np.ndarray, table: np.ndarray, mode: str):
    """Emulate d55 Blaze float DDA or Java SemanticCamera double DDA."""
    cam = np.zeros((36, 64), dtype=np.uint16)
    depth = np.full((36, 64), 255, dtype=np.uint8)
    edge = np.zeros((36, 64), dtype=np.uint8)
    ex0, ey0, ez0 = head.px + head.ox, head.py + 1.62, head.pz + head.oz
    yaw = np.float32(np.float32(head.yaw) * np.float32(math.pi / 180.0))
    pitch = np.float32(np.float32(head.pitch) * np.float32(math.pi / 180.0))
    sy, cy = lut_sin(table, yaw), lut_cos(table, yaw)
    sp, cp = lut_sin(table, pitch), lut_cos(table, pitch)

    if mode == "float32_blaze":
        f = np.float32
        ex, ey, ez = f(ex0), f(ey0), f(ez0)
        lx, ly, lz = f(f(-sy) * cp), f(-sp), f(cy * cp)
        hn = f(np.sqrt(f(f(lx * lx) + f(lz * lz) + f(1e-12))))
        rx, rz = f(-lz / hn), f(lx / hn)
        ux, uy, uz = f(-rz * ly), f(f(rz * lx) - f(rx * lz)), f(rx * ly)
        tany = f(0.7002075382097097)
        tanx = f(tany * f(64.0 / 36.0))
        far, edge_w = f(48.0), f(0.05)
    elif mode == "double_java":
        ex, ey, ez = float(ex0), float(ey0), float(ez0)
        lx, ly, lz = float(-sy) * float(cp), float(-sp), float(cy) * float(cp)
        hn = math.sqrt(lx * lx + lz * lz + 1e-12)
        rx, rz = -lz / hn, lx / hn
        ux, uy, uz = -rz * ly, rz * lx - rx * lz, rx * ly
        tany = 0.7002075382097097
        tanx = tany * (64.0 / 36.0)
        far, edge_w = 48.0, 0.05
    else:
        raise ValueError(mode)

    for py in range(36):
        for px in range(64):
            if mode == "float32_blaze":
                ny = f(f(1.0) - f(f(2.0) * f(f(py) + f(0.5)) / f(36.0)))
                nx = f(f(2.0) * f(f(px) + f(0.5)) / f(64.0) - f(1.0))
                ddx = f(lx + f(f(nx * tanx) * rx) + f(f(ny * tany) * ux))
                ddy = f(ly + f(f(ny * tany) * uy))
                ddz = f(lz + f(f(nx * tanx) * rz) + f(f(ny * tany) * uz))
                norm = f(np.sqrt(f(f(ddx * ddx) + f(ddy * ddy) + f(ddz * ddz))))
                dx, dy, dz = f(ddx / norm), f(ddy / norm), f(ddz / norm)
                one, huge, zero = f(1.0), f(1e30), f(0.0)
                abs_fn, floor_fn = np.abs, lambda value: math.floor(float(value))
            else:
                ny = 1.0 - 2.0 * (py + 0.5) / 36.0
                nx = 2.0 * (px + 0.5) / 64.0 - 1.0
                ddx = lx + nx * tanx * rx + ny * tany * ux
                ddy = ly + ny * tany * uy
                ddz = lz + nx * tanx * rz + ny * tany * uz
                norm = math.sqrt(ddx * ddx + ddy * ddy + ddz * ddz)
                dx, dy, dz = ddx / norm, ddy / norm, ddz / norm
                one, huge, zero = 1.0, 1e30, 0.0
                abs_fn, floor_fn = abs, math.floor

            vx, vy, vz = floor_fn(ex), floor_fn(ey), floor_fn(ez)
            sx, syi, sz = (1 if dx > 0 else -1), (1 if dy > 0 else -1), (1 if dz > 0 else -1)
            tdx = abs_fn(one / dx) if dx != 0 else huge
            tdy = abs_fn(one / dy) if dy != 0 else huge
            tdz = abs_fn(one / dz) if dz != 0 else huge
            tmx = ((vx + 1 - ex) if dx > 0 else (ex - vx)) * tdx if dx != 0 else huge
            tmy = ((vy + 1 - ey) if dy > 0 else (ey - vy)) * tdy if dy != 0 else huge
            tmz = ((vz + 1 - ez) if dz > 0 else (ez - vz)) * tdz if dz != 0 else huge
            if mode == "float32_blaze":
                tdx, tdy, tdz = f(tdx), f(tdy), f(tdz)
                tmx, tmy, tmz = f(tmx), f(tmy), f(tmz)
            t, axis, found = zero, -1, 0
            while t < far:
                found = block(ids, head, vx, vy, vz)
                if found:
                    break
                if tmx < tmy and tmx < tmz:
                    t, vx, tmx, axis = tmx, vx + sx, tmx + tdx, 0
                elif tmy < tmz:
                    t, vy, tmy, axis = tmy, vy + syi, tmy + tdy, 1
                else:
                    t, vz, tmz, axis = tmz, vz + sz, tmz + tdz, 2
                if mode == "float32_blaze":
                    t, tmx, tmy, tmz = f(t), f(tmx), f(tmy), f(tmz)
            if t >= far:
                found = 0
            if not found:
                continue
            cam[py, px] = found
            depth[py, px] = min(255, int(t * (f(4.0) if mode == "float32_blaze" else 4.0)))
            if axis >= 0:
                for tangent in range(3):
                    if tangent == axis:
                        continue
                    coord = (ex + t * dx, ey + t * dy, ez + t * dz)[tangent]
                    frac = coord - floor_fn(coord)
                    if frac < edge_w or frac > one - edge_w:
                        edge[py, px] = 1
                        break
    return cam, depth, edge


def histogram(array: np.ndarray) -> dict[str, int]:
    values, counts = np.unique(array, return_counts=True)
    return {str(int(value)): int(count) for value, count in zip(values, counts)}


def make_planes(cam: np.ndarray, depth: np.ndarray, edge: np.ndarray) -> np.ndarray:
    return np.stack(
        [
            cam == 17,
            cam == 18,
            cam == 16,
            (cam == 1) | (cam == 4),
            (cam == 2) | (cam == 3),
            cam == 58,
            cam != 0,
            depth,
            edge,
        ]
    ).astype(np.uint8)


def plane_stats(planes: np.ndarray) -> list[dict]:
    out = []
    for index, name in enumerate(PLANE_NAMES):
        values = planes[index]
        network = values.astype(np.float32)
        scale = 1.0 / 255.0 if name == "depth" else 1.0
        network *= scale
        out.append(
            {
                "index_in_frame": index,
                "name": name,
                "storage_dtype": "uint8",
                "network_dtype": "float32",
                "network_scale": scale,
                "min_storage": int(values.min()),
                "max_storage": int(values.max()),
                "mean_storage": float(values.mean()),
                "mean_network": float(network.mean()),
                "nonzero_pixels": int(np.count_nonzero(values)),
                "histogram": histogram(values),
            }
        )
    return out


def t0_scalars(head: SnapHead, ids: np.ndarray) -> tuple[list[float], int]:
    x, y, z = head.px + head.ox, head.py, head.pz + head.oz
    pwx, pwy, pwz = math.floor(x), math.floor(y), math.floor(z)
    coal = []
    for wx in range(pwx - 16, pwx + 17):
        for wz in range(pwz - 16, pwz + 17):
            for wy in range(max(0, pwy - 24), min(255, pwy + 40) + 1):
                if block(ids, head, wx, wy, wz) != 16:
                    continue
                distance_from_feet_sq = (
                    (wx + 0.5 - x) ** 2
                    + (wy + 0.5 - y) ** 2
                    + (wz + 0.5 - z) ** 2
                )
                coal.append((distance_from_feet_sq, wx, wy, wz))
    coal.sort()
    coal = coal[:32]
    pitch_rad = math.radians(float(head.pitch))
    scalar6 = [0.0, 0.0, 0.0, 1.0, math.sin(pitch_rad), math.cos(pitch_rad)]
    if coal:
        nearest = min(
            coal,
            key=lambda row: (
                (row[1] + 0.5 - x) ** 2
                + (row[2] + 0.5 - (y + 1.62)) ** 2
                + (row[3] + 0.5 - z) ** 2
            ),
        )
        dx = nearest[1] + 0.5 - x
        dy = nearest[2] + 0.5 - (y + 1.62)
        dz = nearest[3] + 0.5 - z
        distance = math.sqrt(dx * dx + dy * dy + dz * dz)
        relative_yaw = (
            math.degrees(math.atan2(-dx, dz)) - float(head.yaw) + 180.0
        ) % 360.0 - 180.0
        relative_pitch = (
            math.degrees(-math.asin(dy / max(distance, 1e-9)))
            - float(head.pitch)
        )
        scalar6 = [
            math.sin(math.radians(relative_yaw)),
            math.cos(math.radians(relative_yaw)),
            relative_pitch / 90.0,
            min(distance, 24.0) / 24.0,
            math.sin(pitch_rad),
            math.cos(pitch_rad),
        ]
    all_scalars = scalar6 + [0.0] * 9 + [0.0] + [0.0] * 9 + [y / 64.0, 0.0]
    return np.asarray(all_scalars, dtype=np.float32).astype(float).tolist(), len(coal)


def snapshot_receipts(reference: Path, archive: Path) -> dict:
    table = sin_table()
    result = {
        "scope": (
            "Controlled t0 snapshot comparison. Blaze and Java algorithms are emulated on "
            "the same d55 snapshot cells and pose; these are not archived live Java observations."
        ),
        "resolution": [36, 64],
        "stack": 2,
        "channels": 18,
        "seeds": {},
    }
    for seed in SEEDS:
        path = reference / "blaze" / "rl" / "out" / "snaps" / f"s{seed}_t0.bsnp"
        head, ids = load_snapshot(path)
        blaze = render_camera(head, ids, table, "float32_blaze")
        java = render_camera(head, ids, table, "double_java")
        blaze_planes = make_planes(*blaze)
        java_planes = make_planes(*java)
        controlled_scalars, coal_count = t0_scalars(head, ids)
        live_level = load_level(archive / "run0" / "saves" / f"qrl_{seed}" / "level.dat")
        player = live_level.get("Player", {})
        spawn = [
            live_level.get("SpawnX"),
            live_level.get("SpawnY"),
            live_level.get("SpawnZ"),
        ]
        snapshot_world_pose = [head.px + head.ox, head.py, head.pz + head.oz]
        java_reset_pose = [spawn[0] + 0.5, spawn[1], spawn[2] + 0.5]
        spawn_delta = [
            java_reset_pose[index] - snapshot_world_pose[index] for index in range(3)
        ]
        result["seeds"][str(seed)] = {
            "snapshot": {
                "seed": int(head.seed),
                "tick": int(head.tick),
                "world_pose": snapshot_world_pose,
                "yaw": float(head.yaw),
                "pitch": float(head.pitch),
                "inventory_nonempty_slots": sum(int(row[1] > 0) for row in head.inv),
                "region_dims": [head.rnx, head.rny, head.rnz],
            },
            "archived_java_final_save": {
                "random_seed": live_level.get("RandomSeed"),
                "spawn": spawn,
                "reset_pose_from_code": java_reset_pose,
                "player_pos_final_attempt": player.get("Pos"),
                "time": live_level.get("Time"),
                "day_time": live_level.get("DayTime"),
                "raining": bool(live_level.get("raining")),
                "thundering": bool(live_level.get("thundering")),
                "game_rules": live_level.get("GameRules"),
            },
            "spawn_delta_java_minus_snapshot": spawn_delta,
            "spawn_distance_java_to_snapshot": math.sqrt(
                sum(delta * delta for delta in spawn_delta)
            ),
            "spawn_horizontal_distance_java_to_snapshot": math.hypot(
                spawn_delta[0], spawn_delta[2]
            ),
            "relative_block_window_final_save_vs_snapshot": compare_relative_block_window(
                head,
                ids,
                archive / "run0" / "saves" / f"qrl_{seed}" / "region",
                java_reset_pose,
            ),
            "blaze_t0_plane_stats": plane_stats(blaze_planes),
            "blaze_t0_policy_scalars": controlled_scalars,
            "blaze_t0_coal_scan_count_capped_32": coal_count,
            "controlled_java_double_minus_blaze_float": {
                "cam_id_differing_pixels": int(np.count_nonzero(java[0] != blaze[0])),
                "depth_differing_pixels": int(np.count_nonzero(java[1] != blaze[1])),
                "edge_differing_pixels": int(np.count_nonzero(java[2] != blaze[2])),
                "any_raw_field_differing_pixels": int(
                    np.count_nonzero(
                        (java[0] != blaze[0])
                        | (java[1] != blaze[1])
                        | (java[2] != blaze[2])
                    )
                ),
                "per_plane_differing_pixels": {
                    PLANE_NAMES[index]: int(
                        np.count_nonzero(java_planes[index] != blaze_planes[index])
                    )
                    for index in range(9)
                },
            },
        }
    return result


def interface_schema() -> dict:
    scalars = [
        ("coal_rel_yaw_sin", "float32", "sin(relative yaw radians)", "none"),
        ("coal_rel_yaw_cos", "float32", "cos(relative yaw radians)", "none"),
        ("coal_rel_pitch", "float32", "relative pitch degrees / 90", "divide 90"),
        ("coal_distance", "float32", "min(distance,24) / 24; 1 if none", "clip 24, divide 24"),
        ("pitch_sin", "float32", "sin(player pitch radians)", "none"),
        ("pitch_cos", "float32", "cos(player pitch radians)", "none"),
    ]
    inv_names = ("log", "planks", "stick", "cobble", "table", "wood_pick", "stone_pick", "coal", "torch")
    scalars.extend((f"inv_{name}", "float32", f"inventory count for {name}", "clip 10, divide 10") for name in inv_names)
    scalars.append(("container_open", "float32", "container > 0", "boolean 0/1"))
    scalars.extend((f"held_{name}", "float32", f"held item is {name}", "one-hot 0/1") for name in inv_names)
    scalars.extend(
        [
            ("player_y", "float32", "feet y / 64", "divide 64, no clip"),
            ("episode_fraction", "float32", "decision index / 1500", "divide 1500"),
        ]
    )
    return {
        "frame": {
            "resolution": [36, 64],
            "planes_per_frame": 9,
            "stack": 2,
            "channels": [
                {
                    "channel": stack * 9 + index,
                    "stack_position": "previous" if stack == 0 else "current",
                    "plane": name,
                    "storage_dtype": "uint8",
                    "network_dtype": "float32",
                    "network_scale": "1/255" if name == "depth" else "1",
                    "resize_or_crop": "none",
                }
                for stack in range(2)
                for index, name in enumerate(PLANE_NAMES)
            ],
        },
        "scalars": [
            {"index": index, "name": name, "dtype": dtype, "source": source, "scaling": scaling}
            for index, (name, dtype, source, scaling) in enumerate(scalars)
        ],
    }


def action_schema() -> dict:
    return {
        "heads": [
            {
                "index": 0,
                "name": "dyaw",
                "categories": [-15.0, 0.0, 15.0],
                "blaze": "degree delta on repeat 0; float32 player yaw update",
                "java": "degree delta on repeat 0; EntityPlayerSP.rotationYaw += float",
                "status": "identical values and sign",
            },
            {
                "index": 1,
                "name": "dpitch",
                "categories": [-10.0, 0.0, 10.0],
                "blaze": "degree delta on repeat 0; positive is down; clamp [-89,89]",
                "java": "degree delta on repeat 0; positive is down; clamp [-90,90]",
                "status": "1 degree endpoint clamp gap",
            },
            {
                "index": 2,
                "name": "forward",
                "categories": [-1.0, 0.0, 1.0],
                "blaze": "signed forward input on all four sub-ticks",
                "java": "back/forward keybind held on all four socket steps",
                "status": "identical decode",
            },
            {
                "index": 3,
                "name": "jump",
                "categories": [0, 1],
                "blaze": "boolean held on all four sub-ticks",
                "java": "jump keybind held on all four socket steps",
                "status": "identical decode",
            },
            {
                "index": 4,
                "name": "attack",
                "categories": [0, 1],
                "blaze": "boolean held on all four sub-ticks",
                "java": "attack keybind held on all four socket steps",
                "status": "identical decode",
            },
            {
                "index": 5,
                "name": "use",
                "categories": [0, 1],
                "blaze": "boolean held on all four sub-ticks",
                "java": "use keybind held on all four socket steps",
                "status": "identical decode",
            },
            {
                "index": 6,
                "name": "craft",
                "categories": ["none", 0, 1, 2, 3, 4, 5],
                "blaze": "semantic primitive once before tick; 3x3 recipes need table",
                "java": "same semantic primitive once on repeat 0; no vanilla GUI",
                "status": "recipes/gating match; ordering differs when interact co-occurs",
            },
            {
                "index": 7,
                "name": "interact",
                "categories": [0, 1],
                "blaze": "semantic nearest table/furnace within six blocks once before tick",
                "java": "same mod-state primitive once on repeat 0; no vanilla GUI",
                "status": "feasible; candidate tie/scan numeric parity is unmeasured",
            },
            {
                "index": 8,
                "name": "hotbar",
                "categories": ["none", 0, 1, 2, 3, 4, 5, 6, 7, 8],
                "blaze": "selection present on all four sub-ticks",
                "java": "selection sent on repeat 0 only",
                "status": "normally equivalent because selection is persistent",
            },
        ],
        "repeat": {
            "value": 4,
            "blaze": "one decision executes exactly four simulation ticks",
            "java": "one decision sends four acknowledged socket actions",
            "special_action_repeat": "dyaw, dpitch, craft, interact, hotbar only on repeat 0",
            "persistent_action_repeat": "forward/back, jump, attack, use on repeats 0..3",
            "camera_repeat": 3,
        },
        "pre_tick_order": {
            "blaze": ["craft", "interact"],
            "java": ["interact", "craft"],
            "effect": (
                "A simultaneous interact+3x3 craft can succeed in Java but fails in Blaze "
                "when the table was not already open."
            ),
        },
    }


def source_manifest(reference: Path, archive: Path) -> dict:
    inputs = {
        "archived_java": archive / "java.json",
        "archived_receipt": archive / "receipt.json",
        "archived_sim_stub": archive / "sim_stub.json",
        "archived_client_log": archive / "client0.log",
        "driver": Path("/home/infatoshi/dev/nw/.tmp/parallel_java_eval.py"),
        "pair_validator": reference / "paper" / "tools" / "pair_sim2real.py",
        "ppo_chain": reference / "blaze" / "env" / "ppo_chain_cu.py",
        "blaze_core": reference / "blaze" / "env" / "blaze_core.h",
        "java_evaluator": reference / "java" / "qrl_chain_demo.py",
        "java_client": reference / "java" / "qrl_client.py",
        "recorder": (
            reference
            / "java"
            / "Minecraft"
            / "src"
            / "main"
            / "java"
            / "qrl"
            / "Recorder.java"
        ),
        "semantic_camera": (
            reference
            / "java"
            / "Minecraft"
            / "src"
            / "main"
            / "java"
            / "qrl"
            / "SemanticCamera.java"
        ),
        "snapshot_baker": reference / "blaze" / "env" / "make_snapshots.py",
        "magma_runtime": reference / "magma" / "game" / "runtime.c",
    }
    return {
        "pinned_commit": "d55c7f01c1139299be3f7fa0b98ef11b82c3b473",
        "inputs": {
            name: {
                "path": str(path),
                "bytes": path.stat().st_size,
                "sha256": sha256_file(path),
            }
            for name, path in inputs.items()
        },
        "archive_observation_capture": {
            "frame_every": 0,
            "raw_policy_observation_files_found": 0,
            "consequence": (
                "Live Java per-plane and scalar distributions cannot be reconstructed "
                "from this archive."
            ),
        },
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--archive", type=Path, default=DEFAULT_ARCHIVE)
    parser.add_argument("--reference", type=Path, default=DEFAULT_REFERENCE)
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    json_dump(args.out / "pilot_runtime.json", pilot_runtime(args.archive))
    json_dump(args.out / "observation_schema.json", interface_schema())
    json_dump(args.out / "action_schema.json", action_schema())
    json_dump(args.out / "source_manifest.json", source_manifest(args.reference, args.archive))
    json_dump(
        args.out / "t0_snapshot_observation_stats.json",
        snapshot_receipts(args.reference, args.archive),
    )
    print(f"wrote receipts to {args.out}")


if __name__ == "__main__":
    main()
