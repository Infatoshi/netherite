"""Portable synthetic .bsnp fixture and deterministic Blaze action tape."""
import ctypes
import os
import struct

import numpy as np


class RlSnapHead(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("magic", ctypes.c_char * 4), ("version", ctypes.c_uint),
        ("seed", ctypes.c_longlong), ("tick", ctypes.c_longlong),
        ("ox", ctypes.c_int), ("oz", ctypes.c_int),
        ("px", ctypes.c_double), ("py", ctypes.c_double),
        ("pz", ctypes.c_double), ("box", ctypes.c_double * 6),
        ("yaw", ctypes.c_float), ("pitch", ctypes.c_float),
        ("mx", ctypes.c_double), ("my", ctypes.c_double),
        ("mz", ctypes.c_double), ("on_ground", ctypes.c_int),
        ("collided_h", ctypes.c_int), ("collided_v", ctypes.c_int),
        ("is_collided", ctypes.c_int), ("fall_distance", ctypes.c_float),
        ("sprinting", ctypes.c_int),
        ("sprint_toggle_timer", ctypes.c_int),
        ("jump_factor_sprint", ctypes.c_int), ("jump_ticks", ctypes.c_int),
        ("prev_move_forward", ctypes.c_float), ("prev_sneak", ctypes.c_int),
        ("health", ctypes.c_float), ("food", ctypes.c_int),
        ("saturation", ctypes.c_float), ("exhaustion", ctypes.c_float),
        ("food_timer", ctypes.c_int), ("dig_progress", ctypes.c_float),
        ("dig_hx", ctypes.c_int), ("dig_hy", ctypes.c_int),
        ("dig_hz", ctypes.c_int), ("dig_hitting", ctypes.c_int),
        ("dig_delay", ctypes.c_int), ("atk_prev", ctypes.c_int),
        ("rc_delay", ctypes.c_int), ("use_prev", ctypes.c_int),
        ("hurt_vel_reset", ctypes.c_int),
        ("server_motion_x", ctypes.c_double),
        ("server_motion_z", ctypes.c_double),
        ("container", ctypes.c_int), ("container_wx", ctypes.c_int),
        ("container_wy", ctypes.c_int), ("container_wz", ctypes.c_int),
        ("world_dirty", ctypes.c_int), ("hotbar_sel", ctypes.c_int),
        ("inv", (ctypes.c_int * 3) * 37), ("n_items", ctypes.c_uint),
        ("rx0", ctypes.c_int), ("ry0", ctypes.c_int),
        ("rz0", ctypes.c_int), ("rnx", ctypes.c_int),
        ("rny", ctypes.c_int), ("rnz", ctypes.c_int),
    ]


assert ctypes.sizeof(RlSnapHead) == 752


class RlSnapItem(ctypes.Structure):
    _pack_ = 1
    _fields_ = [
        ("x", ctypes.c_double), ("y", ctypes.c_double),
        ("z", ctypes.c_double), ("mx", ctypes.c_double),
        ("my", ctypes.c_double), ("mz", ctypes.c_double),
        ("item", ctypes.c_int), ("count", ctypes.c_int),
        ("meta", ctypes.c_int), ("age", ctypes.c_int),
        ("pickup_delay", ctypes.c_int), ("lifespan", ctypes.c_int),
        ("on_ground", ctypes.c_int),
    ]


assert ctypes.sizeof(RlSnapItem) == 76


def _idx(x, y, z, ny, nz):
    return (x * ny + y) * nz + z


def write_synthetic_snapshot(path, nx=16, ny=32, nz=16):
    """Write a small valid scene with a stone floor and patterned front wall."""
    if nx < 16 or ny < 16 or nz < 16:
        raise ValueError("synthetic fixture needs dimensions >= 16")
    h = RlSnapHead()
    h.magic = b"BSNP"
    h.version = 1
    h.seed = 0x123456789
    h.tick = 400
    h.ox = h.oz = 0
    h.px, h.py, h.pz = 8.5, 4.0, 8.5
    h.box[:] = (8.2, 4.0, 8.2, 8.8, 5.8, 8.8)
    h.yaw, h.pitch = 0.0, 0.0
    h.on_ground = 1
    h.collided_v = 1
    h.is_collided = 1
    h.health, h.food, h.saturation = 20.0, 20, 5.0
    h.dig_hx = h.dig_hy = h.dig_hz = -(2 ** 31)
    h.container_wx = h.container_wy = h.container_wz = -(2 ** 31)
    h.hotbar_sel = 0
    h.rx0 = h.ry0 = h.rz0 = 0
    h.rnx, h.rny, h.rnz = nx, ny, nz

    cells = np.zeros(nx * ny * nz, dtype="<u2")
    for x in range(nx):
        for z in range(nz):
            cells[_idx(x, 3, z, ny, nz)] = 1 << 4
    wall_z = 13
    palette = (1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17, 20)
    coal = []
    for x in range(3, 14):
        for y in range(4, 13):
            block = palette[(x * 7 + y * 3) % len(palette)]
            if (x, y) in ((7, 5), (8, 5), (9, 6)):
                block = 16
            cells[_idx(x, y, wall_z, ny, nz)] = block << 4
            if block == 16:
                coal.append((x, y, wall_z))
    coal.sort()

    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(bytes(h))
        f.write(cells.tobytes())
        f.write(struct.pack("<I", len(coal)))
        for xyz in coal:
            f.write(struct.pack("<iii", *xyz))
    return os.fspath(path)


def write_dense_coal_snapshot(path):
    """Legacy x/y/z ore order with enough candidates to hit the 512 cap."""
    nx, ny, nz = 40, 65, 40
    h = RlSnapHead()
    h.magic = b"BSNP"; h.version = 1; h.seed = 7
    h.px, h.py, h.pz = 20.5, 24.0, 20.5
    h.box[:] = (20.2, 24.0, 20.2, 20.8, 25.8, 20.8)
    h.health, h.food, h.saturation = 20.0, 20, 5.0
    h.dig_hx = h.dig_hy = h.dig_hz = -(2 ** 31)
    h.container_wx = h.container_wy = h.container_wz = -(2 ** 31)
    h.rnx, h.rny, h.rnz = nx, ny, nz
    cells = np.full(nx * ny * nz, 16 << 4, dtype="<u2")
    coal = [(x, y, z) for x in range(nx) for y in range(ny)
            for z in range(nz)]
    with open(path, "wb") as f:
        f.write(bytes(h)); f.write(cells.tobytes())
        f.write(struct.pack("<I", len(coal)))
        for xyz in coal:                 # intentionally legacy x/y/z order
            f.write(struct.pack("<iii", *xyz))
    return os.fspath(path)


def write_overflow_snapshot(path):
    """Full active table plus a nearby coal block for overflow/capture tests."""
    base = write_synthetic_snapshot(path)
    raw = open(base, "rb").read()
    h = RlSnapHead.from_buffer_copy(raw[:ctypes.sizeof(RlSnapHead)])
    cell_count = h.rnx * h.rny * h.rnz
    cells = np.frombuffer(raw, dtype="<u2", count=cell_count,
                          offset=ctypes.sizeof(RlSnapHead)).copy()
    cells[_idx(8, 5, 10, h.rny, h.rnz)] = 16 << 4
    h.n_items = 48
    h.inv[0][:] = (278, 1, 0)       # diamond pick; break coal before expiry
    h.hotbar_sel = 0
    items = []
    for i in range(48):
        it = RlSnapItem()
        it.x, it.y, it.z = 1.5, 4.0, 1.5
        it.item, it.count = 3, 1
        it.age, it.lifespan = 0, 20
        items.append(bytes(it))
    coal = []
    ids = (cells >> 4).reshape(h.rnx, h.rny, h.rnz)
    for x, y, z in zip(*np.nonzero(ids == 16)):
        coal.append((int(x), int(y), int(z)))
    with open(path, "wb") as f:
        f.write(bytes(h)); f.write(b"".join(items)); f.write(cells.tobytes())
        f.write(struct.pack("<I", len(coal)))
        for xyz in coal:
            f.write(struct.pack("<iii", *xyz))
    return os.fspath(path)


def action_tape(n_envs, decision, full=False):
    """Deterministic, bounded actions that exercise movement and all flags."""
    i = np.arange(n_envs, dtype=np.int64)
    if not full:
        a = np.empty((n_envs, 5), dtype=np.int64)
        a[:, 0] = (i + decision) % 3
        a[:, 1] = (i * 2 + decision) % 3
        a[:, 2] = ((i + decision) % 4) != 0
        a[:, 3] = ((i + 2 * decision) % 11) == 0
        a[:, 4] = ((i * 3 + decision) % 5) == 0
        return a
    a = np.zeros((n_envs, 13), dtype=np.float64)
    a[:, 0] = (((i + decision) % 3) - 1) * 0.5
    a[:, 1] = (((i * 2 + decision) % 3) - 1) * 0.25
    a[:, 2] = (((i + decision) % 5) - 2) * 3.0
    a[:, 3] = (((i + 2 * decision) % 5) - 2) * 2.0
    a[:, 4] = ((i + decision) % 13) == 0
    a[:, 5] = ((i + decision) % 17) == 0
    a[:, 6] = ((i + decision) % 3) == 0
    a[:, 7] = ((i + decision) % 4) == 0
    a[:, 8] = ((i + decision) % 19) == 0
    a[:, 9] = np.where(((i + decision) % 6) == 0,
                       (i + decision) % 9, -1)
    a[:, 10] = np.where(((i * 3 + decision) % 7) == 0,
                        (i + decision) % 13, -1)
    a[:, 11] = ((i + decision) % 23) == 0
    a[:, 12] = ((i + decision) % 29) == 0
    return a
