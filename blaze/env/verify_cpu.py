#!/usr/bin/env python3
"""M1 fidelity gate: real magma_game (--rl-bin --snapshot-in) vs batch-of-1
CPU blaze, stepped in lockstep with identical action streams; EVERY tick's
observation must match byte-for-byte on the gated fields.

Gated fields (byte ranges of the packed BOLR record): magic..inv_counts
(tick, pose, dead, hotbar, container, inv_counts) and coal..edge (coal list,
cam, depth, edge). blocks/logs are excluded BY DESIGN: their membership rides
the real env's scan-cache rebuild cadence (world_dirty countdown), which
blaze does not reproduce; the trainer never reads them.

Fixture envelope (2026-08-07): blaze's world IS the .bsnp region, so
oc_block outside it reads air. A camera ray reaches OC_FAR=48, so a fixture
whose player is within 48 blocks of a horizontal region edge cannot support
a byte-exact cam compare: magma's sliding camreg hits real world blocks
where blaze necessarily returns 0. That is a FIXTURE limit, not a port bug,
and it is classified BLOCKED (exit 3) - never PASS, never FAIL - and only
when the per-tick state digests prove the sim itself still matches. Bake
curriculum snapshots with snapshot_r >= 49 (make_snapshots.py CURRICULUM_R,
default 64 like T0_R) to keep the camera inside the fixture.

Usage (anvil):
  uv run --no-project --with numpy python blaze/env/verify_cpu.py \
      [--seeds 14,16,...] [--ticks 1000] [--stage 6.0] [--episodes FILE.jsonl]

Default: the 8 training seeds x 1000 seeded pseudo-random actions
(attack-heavy; same xorshift32 as blaze_verify.c). --episodes replays real
trained action streams instead (one JSON list of action dicts per line).
--chain runs the FULL spawn-to-torch gate: the committed
rl/out/chain_actions_s10.json (movement + hotbar + use/place + craft:N +
interact) replayed from the fresh-spawn tick-0 snapshot s10_t0.bsnp -
craft/interact/use are simulated in blaze, nothing is stripped.
"""
import argparse
import ctypes
import json
import os
import select
import struct
import subprocess
import sys
import time

RL = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "rl")
MAGMA = os.path.join(os.path.dirname(os.path.dirname(RL)), "magma")
BIN = os.path.join(MAGMA, "magma_game")
SO = os.path.join(os.path.dirname(os.path.abspath(__file__)), "blaze_cpu.so")
SNAPS = os.path.join(RL, "out", "snaps")

TRAIN_SEEDS = [14, 16, 20, 27, 29, 32, 44, 46]
YAWS = (-15.0, 0.0, 15.0)
PITCHES = (-10.0, 0.0, 10.0)

# BOLR record layout (packed): see game/rl_mode.c RlBinObs.
OFF = {}
o = 0
for name, sz in [("magic", 4), ("tick", 8), ("x", 8), ("y", 8), ("z", 8),
                 ("yaw", 4), ("pitch", 4), ("dead", 4),
                 ("hotbar_ids", 36), ("hotbar_counts", 36),
                 ("hotbar_sel", 4), ("container", 4), ("inv_counts", 36),
                 ("blocks", 256 * 16), ("logs", 64 * 12), ("coal", 32 * 12),
                 ("cam", 2304 * 2), ("depth", 2304), ("edge", 2304)]:
    OFF[name] = (o, o + sz)
    o += sz
BIN_SIZE = o
GATED = [(0, OFF["blocks"][0]), (OFF["coal"][0], BIN_SIZE)]

# Magma-to-Blaze subsystem parity record (packed BpParityRecord).
PARITY_MAGIC = 0x59524150  # PARY
PARITY_VERSION = 1
PARITY_NAMES = (
    "player", "dig", "inventory", "items", "world", "crafting",
    "containers", "furnaces", "fluids", "random_ticks", "falling_blocks",
    "mobs", "projectiles", "explosions", "portals", "dimensions", "dragon",
    "weather", "xp", "victory", "chests", "boats", "elytra", "observations",
)
PARITY_INDEX = {name: i for i, name in enumerate(PARITY_NAMES)}
# Default-on --chain state-digest pass. Subset of BP_IMPLEMENTED_MASK.
# BP_MOBS is implemented (snapshot v3 + living spine tick). Default chain
# fixtures have n_mobs=0; compare populated stores with --features mobs.
# Unported
# names stay out so they never spuriously BLOCK the gate.
# Furnaces is implemented but has zero evidence on the non-iron chain - digests
# still match (empty FNV seed); evidence is only required under explicit
# --port-parity --features.
# BP_CHESTS is implemented (placed TE + GUI transfers). Default chain
# fixtures have no chest TE; compare with --features chests. Worldgen loot
# tables stay a named generation gap.
PARITY_SUPPORTED = (
    "player", "dig", "inventory", "items", "world", "crafting",
    "containers", "furnaces", "fluids", "observations",
)
PARITY_UNREPRESENTED_SNAPSHOT = 1 << 63
PARITY_DEBUG_NAMES = (
    "player_x", "player_y", "player_z", "motion_x", "motion_y", "motion_z",
    "yaw", "pitch", "on_ground", "fall_distance", "sprinting",
    "sprint_timer", "health", "food", "exhaustion", "dig_progress",
    "dig_world_x", "dig_y", "dig_world_z", "dig_hitting", "dig_delay", "atk_prev",
    "left_click_counter", "rc_delay", "use_prev", "hurt_vel_reset",
    "server_motion_x", "server_motion_z", "container", "container_wx",
    "container_wy", "container_wz",
)
PARITY_DEBUG_RANGES = {
    "player": range(15),
    "dig": range(15, 28),
    "containers": range(28, 32),
}
PARITY_STRUCT = struct.Struct("<IIIIQQQq24Q24I32Q")
PARITY_SIZE = PARITY_STRUCT.size
# Packed layout offsets used by corruption selftests (world is subsystem 4).
PARITY_DIGEST_OFF = 48  # start of digest[BP_NSUBSYSTEMS]
PARITY_WORLD_DIGEST_OFF = PARITY_DIGEST_OFF + PARITY_INDEX["world"] * 8
VERIFIED = 0
FAILED = 1
BLOCKED = 3

# blaze_snapshot.h: v2 added the per-cell packed light payload; v3 added the
# per-mob trailer. A v1 bake loads with light == NULL, and blaze then runs
# with world dynamics UNREPRESENTED rather than refusing the fixture. v2
# loads with n_mobs = 0.
BSNP_MAGIC = b"BSNP"
BSNP_VERSION_LIGHT = 2
SNAP_HEAD_SIZE = 752       # sizeof(RlSnapHead), packed
SNAP_RDIMS_OFF = 728       # 6 x i32: rx0,ry0,rz0,rnx,rny,rnz
SNAP_NITEMS_OFF = 724      # u32 n_items just before rdims
SNAP_ITEM_SIZE = 76        # packed RlSnapItem
SNAP_MOB_SIZE = 572        # packed RlSnapMob
MOB_TYPE_NAMES = {
    0: "none", 1: "player", 2: "zombie", 3: "skeleton", 4: "creeper",
    5: "spider", 6: "enderman", 7: "blaze", 10: "sheep", 11: "pig",
    12: "cow", 13: "chicken", 15: "pigman", 26: "ghast", 27: "magma",
    32: "wither_skeleton", 35: "slime", 36: "silverfish", 37: "boat",
    38: "tnt_primed",
}

# obs_camera.h: rays terminate at OC_FAR; outside the blaze region -> air.
OC_FAR = 48.0
OC_W = 64
OC_H = 36

# Subsystems that must still match for a pure fixed-region camera BLOCKED
# call: observations may diverge, the sim state may not.
SIM_SUBSYSTEMS = (
    "player", "dig", "inventory", "items", "world", "crafting", "containers",
)


def snapshot_dynamics_blocker(path):
    """BLOCKED reason for a .bsnp that predates the light payload, else None.

    A stale v1 fixture is invisible to a CPU-vs-CUDA gate: both sides load the
    same lightless snapshot, both run with world dynamics off, and the gate
    passes byte-exact while silently verifying the wrong thing. It only
    surfaces much later as an unexplained cam divergence. Fail closed."""
    name = os.path.basename(path)
    try:
        with open(path, "rb") as f:
            head = f.read(8)
    except OSError as exc:
        return f"cannot read {name}: {exc}"
    if len(head) < 8 or head[:4] != BSNP_MAGIC:
        return f"{name} is not a .bsnp (magic {head[:4]!r})"
    version = struct.unpack("<I", head[4:8])[0]
    if version < BSNP_VERSION_LIGHT:
        return (f"{name} is a v{version} bake with no per-cell light payload "
                f"(need v{BSNP_VERSION_LIGHT}): world dynamics would run "
                f"unrepresented. Re-bake it -- "
                f"T0=1 SEEDS=<seed> make_snapshots.py")
    return None


class ParityRecord:
    def __init__(self, raw, source):
        if len(raw) != PARITY_SIZE:
            raise RuntimeError(
                f"{source} PARY record is {len(raw)} bytes, expected {PARITY_SIZE}")
        fields = PARITY_STRUCT.unpack(raw)
        magic, version, size, nsubsystems = fields[:4]
        if magic != PARITY_MAGIC:
            raise RuntimeError(
                f"{source} parity pipe returned magic 0x{magic:08x}, "
                f"expected PARY")
        if version != PARITY_VERSION:
            raise RuntimeError(
                f"{source} PARY version {version}, expected {PARITY_VERSION}")
        if size != PARITY_SIZE:
            raise RuntimeError(
                f"{source} PARY size {size}, expected {PARITY_SIZE}")
        if nsubsystems != len(PARITY_NAMES):
            raise RuntimeError(
                f"{source} PARY has {nsubsystems} subsystems, "
                f"expected {len(PARITY_NAMES)}")
        self.implemented = fields[4]
        self.measured = fields[5]
        self.active = fields[6]
        self.tick = fields[7]
        self.digest = fields[8:32]
        self.evidence = fields[32:56]
        self.debug_bits = fields[56:88]


class Rng:
    """xorshift32, kept in sync with blaze_verify.c."""
    def __init__(self, seed):
        self.s = seed & 0xFFFFFFFF

    def next(self):
        x = self.s
        x ^= (x << 13) & 0xFFFFFFFF
        x ^= x >> 17
        x ^= (x << 5) & 0xFFFFFFFF
        self.s = x
        return x


def rand_action(rng):
    return {"dyaw": YAWS[rng.next() % 3], "dpitch": PITCHES[rng.next() % 3],
            "forward": int(rng.next() % 4 != 0), "jump": int(rng.next() % 8 == 0),
            "attack": int(rng.next() % 4 != 3)}


class RealEnv:
    def __init__(self, seed, snap, port_parity=False, magma_args=None):
        self.parity_fd = None
        argv = [BIN, "--rl-bin", "--render", "off", "--pace", "unlimited",
                "--seed", str(seed), "--mobs", "off", "--snapshot-in", snap]
        if magma_args:
            argv = list(argv) + list(magma_args)
        kwargs = {
            "stdin": subprocess.PIPE,
            "stdout": subprocess.PIPE,
            "stderr": subprocess.DEVNULL,
        }
        parity_write = None
        if port_parity:
            self.parity_fd, parity_write = os.pipe()
            argv = list(argv) + ["--set", f"port_parity_fd={parity_write}"]
            kwargs["pass_fds"] = (parity_write,)
        try:
            self.proc = subprocess.Popen(argv, **kwargs)
        except Exception:
            if self.parity_fd is not None:
                os.close(self.parity_fd)
                self.parity_fd = None
            raise
        finally:
            if parity_write is not None:
                os.close(parity_write)
        try:
            self.rec = self._read()
            self.parity_rec = self._read_parity() if port_parity else None
        except Exception:
            self.close()
            raise

    def _read(self):
        win = self._exact(4)
        while win != b"BOLR":
            win = win[1:] + self._exact(1)
        return win + self._exact(BIN_SIZE - 4)

    def _exact(self, n):
        buf = b""
        while len(buf) < n:
            c = self.proc.stdout.read(n - len(buf))
            if not c:
                raise RuntimeError("real env died")
            buf += c
        return buf

    def _read_parity(self):
        deadline = time.monotonic() + 10.0
        chunks = []
        remaining = PARITY_SIZE
        while remaining:
            timeout = deadline - time.monotonic()
            if timeout <= 0:
                raise RuntimeError(
                    "timed out waiting for Magma PARY record; "
                    "port_parity_fd is not supported or did not emit")
            readable, _, _ = select.select([self.parity_fd], [], [], timeout)
            if not readable:
                raise RuntimeError(
                    "timed out waiting for Magma PARY record; "
                    "port_parity_fd is not supported or did not emit")
            chunk = os.read(self.parity_fd, remaining)
            if not chunk:
                raise RuntimeError(
                    "Magma parity pipe closed before a complete PARY record")
            chunks.append(chunk)
            remaining -= len(chunk)
        return ParityRecord(b"".join(chunks), "Magma")

    def step(self, act):
        self.proc.stdin.write((json.dumps(act) + "\n").encode())
        self.proc.stdin.flush()
        self.rec = self._read()
        if self.parity_fd is not None:
            self.parity_rec = self._read_parity()
        return self.rec

    def close(self):
        self.proc.kill()
        if self.parity_fd is not None:
            os.close(self.parity_fd)
            self.parity_fd = None


class Blaze1:
    def __init__(self, snap, port_parity=False, so_path=None, device=0):
        self.lib = ctypes.CDLL(so_path or SO)
        self.parity_enabled = port_parity
        if port_parity:
            try:
                parity_size = self.lib.blaze_parity_size
                capabilities = self.lib.blaze_capabilities
                requirements = self.lib.blaze_snapshot_requirements
                parity_state = self.lib.blaze_parity_state
            except AttributeError as exc:
                symbol = str(exc).rsplit(":", 1)[-1].strip()
                raise RuntimeError(
                    f"Blaze parity ABI unavailable; missing symbol {symbol}") from exc
            parity_size.argtypes = []
            parity_size.restype = ctypes.c_int
            capabilities.argtypes = []
            capabilities.restype = ctypes.c_ulonglong
            requirements.argtypes = [ctypes.c_void_p, ctypes.c_int]
            requirements.restype = ctypes.c_ulonglong
            parity_state.argtypes = [ctypes.c_void_p, ctypes.c_int,
                                     ctypes.c_void_p]
            parity_state.restype = ctypes.c_int
            size = parity_size()
            if size != PARITY_SIZE:
                raise RuntimeError(
                    f"Blaze PARY record is {size} bytes, expected {PARITY_SIZE}")
            self.capabilities = capabilities()
        from blaze import BlazeCreateOpts
        self.lib.blaze_create.restype = ctypes.c_void_p
        self.lib.blaze_create.argtypes = [
            ctypes.c_int, ctypes.c_int,
            ctypes.POINTER(BlazeCreateOpts)]
        self.lib.blaze_destroy.argtypes = [ctypes.c_void_p]
        opts = BlazeCreateOpts.defaults()
        self.h = self.lib.blaze_create(int(device), 1, ctypes.byref(opts))
        assert self.h
        err = ctypes.create_string_buffer(256)
        paths = (ctypes.c_char_p * 1)(snap.encode())
        r = self.lib.blaze_load_snapshots(ctypes.c_void_p(self.h), paths, 1,
                                          err, 256)
        if r < 0:
            raise RuntimeError(f"load_snapshots: {err.value.decode()}")
        self.liquid = self.lib.blaze_snapshot_has_liquid(
            ctypes.c_void_p(self.h), 0)
        if port_parity:
            self.requirements = self.lib.blaze_snapshot_requirements(
                ctypes.c_void_p(self.h), 0)
            self.parity_buf = ctypes.create_string_buffer(PARITY_SIZE)
        assign = (ctypes.c_int * 1)(0)
        assert self.lib.blaze_assign(ctypes.c_void_p(self.h), assign) == 0
        assert self.lib.blaze_reset(ctypes.c_void_p(self.h), None) == 0
        assert self.lib.blaze_obs_size() == BIN_SIZE, \
            f"CuBinObs {self.lib.blaze_obs_size()} != RlBinObs {BIN_SIZE}"
        self.buf = ctypes.create_string_buffer(BIN_SIZE)

    def emit(self, want_cam=1):
        assert self.lib.blaze_emit(ctypes.c_void_p(self.h), 0, want_cam,
                                   self.buf) == 0
        return self.buf.raw

    def parity(self):
        if not self.parity_enabled:
            raise RuntimeError("Blaze parity state requested without --port-parity")
        r = self.lib.blaze_parity_state(
            ctypes.c_void_p(self.h), 0, self.parity_buf)
        if r != 0:
            raise RuntimeError(f"blaze_parity_state failed with status {r}")
        return ParityRecord(self.parity_buf.raw, "Blaze")

    def step(self, act):
        a = (ctypes.c_double * 17)(
            act.get("forward", 0), act.get("strafe", 0), act.get("dyaw", 0),
            act.get("dpitch", 0), act.get("jump", 0), act.get("sneak", 0),
            act.get("sprint", 0), act.get("attack", 0), act.get("use", 0),
            act.get("hotbar", -1), act.get("craft", -1),
            act.get("interact", 0), act.get("smelt", 0),
            act.get("inv_click", 0), act.get("inv_slot", 0),
            act.get("inv_button", 0), act.get("inv_type", 0))
        r = self.lib.blaze_tick_raw(ctypes.c_void_p(self.h), 0, a,
                                    act.get("cam", 1), self.buf)
        assert r == 0, "blaze_tick_raw failed"
        return self.buf.raw

    def debug(self):
        out = (ctypes.c_double * 21)()
        self.lib.blaze_debug_state(ctypes.c_void_p(self.h), 0, out, 21)
        return list(out)

    def _bind_cam_inputs(self):
        if getattr(self, "_cam_bound", False):
            return
        fn = self.lib.blaze_obs_cam_inputs
        fn.argtypes = [
            ctypes.c_void_p, ctypes.c_int,
            ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
            ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_float),
            ctypes.POINTER(ctypes.c_float), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.c_int),
            ctypes.POINTER(ctypes.POINTER(ctypes.c_uint16)),
        ]
        fn.restype = ctypes.c_int
        self._cam_bound = True

    def cells_u16(self):
        """Copy of the live packed region cells. Invalidated by the next tick."""
        import numpy as np
        self._bind_cam_inputs()
        nx = ctypes.c_int()
        ny = ctypes.c_int()
        nz = ctypes.c_int()
        cells = ctypes.POINTER(ctypes.c_uint16)()
        r = self.lib.blaze_obs_cam_inputs(
            ctypes.c_void_p(self.h), 0,
            None, None, None, None, None,
            None, None, None,
            ctypes.byref(nx), ctypes.byref(ny), ctypes.byref(nz),
            ctypes.byref(cells))
        if r != 0 or not cells:
            raise RuntimeError("blaze_obs_cam_inputs failed")
        vol = nx.value * ny.value * nz.value
        if vol <= 0:
            raise RuntimeError("blaze region volume is empty")
        return np.ctypeslib.as_array(cells, shape=(vol,)).copy()

    def close(self):
        self.lib.blaze_destroy(ctypes.c_void_p(self.h))


def liquid_cells_key(cells_u16):
    """Fingerprint of liquid cells (ids 8-11): (count, xor of index^state).

    Equal keys mean the same (position, packed state) multiset. Used by the
    chain-stage emit pass to prove quiescence; a key change is liquid-active.
    """
    import numpy as np
    cells = np.asarray(cells_u16, dtype=np.uint16)
    ids = cells >> 4
    mask = (ids >= 8) & (ids <= 11)
    n = int(np.count_nonzero(mask))
    if n == 0:
        return (0, 0)
    idx = np.flatnonzero(mask).astype(np.uint64)
    tok = (idx << 16) ^ cells[idx].astype(np.uint64)
    return (n, int(np.bitwise_xor.reduce(tok)))


def first_diff_field(a, b):
    for name, (lo, hi) in OFF.items():
        if name in ("blocks", "logs"):
            continue
        if a[lo:hi] != b[lo:hi]:
            return name
    return None


def fmt_field(rec, name):
    lo, hi = OFF[name]
    raw = rec[lo:hi]
    if name in ("x", "y", "z", "tick"):
        return struct.unpack("<d" if name != "tick" else "<q", raw)[0]
    if name in ("yaw", "pitch"):
        return struct.unpack("<f", raw)[0]
    if name in ("dead", "hotbar_sel", "container"):
        return struct.unpack("<i", raw)[0]
    if name == "coal":
        v = struct.unpack(f"<{(hi-lo)//4}i", raw)
        return [v[i:i+3] for i in range(0, 24, 3)]
    if name in ("hotbar_ids", "hotbar_counts", "inv_counts"):
        return struct.unpack(f"<{(hi-lo)//4}i", raw)
    if name in ("cam", "depth", "edge"):
        n = sum(1 for _ in raw)
        return f"<{name}: {n} bytes, first diff elsewhere>"
    return raw.hex()


def gated_equal(a, b):
    return all(a[lo:hi] == b[lo:hi] for lo, hi in GATED)


def gated_equal_except_obs(a, b):
    """Gated equality ignoring cam/depth/edge (the observation channels)."""
    for name, (lo, hi) in OFF.items():
        if name in ("blocks", "logs", "cam", "depth", "edge"):
            continue
        if a[lo:hi] != b[lo:hi]:
            return False
    return True


def read_snap_region(path):
    """Return (rx0, ry0, rz0, rnx, rny, rnz) from a .bsnp head."""
    with open(path, "rb") as f:
        head = f.read(SNAP_HEAD_SIZE)
    if len(head) < SNAP_HEAD_SIZE:
        raise RuntimeError(f"short .bsnp head: {path}")
    return struct.unpack_from("<6i", head, SNAP_RDIMS_OFF)


def parse_bsnp_mobs(path):
    """Read the packed living-slot trailer from a .bsnp (harness dump)."""
    with open(path, "rb") as f:
        head = f.read(SNAP_HEAD_SIZE)
        if len(head) < SNAP_HEAD_SIZE:
            raise RuntimeError(f"short .bsnp head: {path}")
        n_items = struct.unpack_from("<I", head, SNAP_NITEMS_OFF)[0]
        _rx0, _ry0, _rz0, rnx, rny, rnz = struct.unpack_from(
            "<6i", head, SNAP_RDIMS_OFF)
        f.read(n_items * SNAP_ITEM_SIZE)
        vol = rnx * rny * rnz
        f.read(vol * 2)
        ncoal = struct.unpack("<I", f.read(4))[0]
        f.read(ncoal * 12)
        f.read(vol)
        n_mobs = struct.unpack("<I", f.read(4))[0]
        rows = []
        for _ in range(n_mobs):
            raw = f.read(SNAP_MOB_SIZE)
            if len(raw) != SNAP_MOB_SIZE:
                raise RuntimeError(f"truncated .bsnp mobs: {path}")
            slot, _id, typ, alive = struct.unpack_from("<iiii", raw, 0)
            x, y, z = struct.unpack_from("<ddd", raw, 20)
            rows.append({
                "slot": slot, "type": typ, "alive": alive,
                "x": x, "y": y, "z": z,
            })
        return rows


def fmt_mob_row(row):
    name = MOB_TYPE_NAMES.get(row["type"], f"type{row['type']}")
    return (f"slot={row['slot']} type={name}({row['type']}) "
            f"alive={row['alive']} "
            f"xyz=({row['x']:.5f},{row['y']:.5f},{row['z']:.5f})")


def blaze_live_mobs(cu):
    """Live blaze table via blaze_mobs_count/get (harness, not sim)."""
    lib = cu.lib
    lib.blaze_mobs_count.argtypes = [ctypes.c_void_p, ctypes.c_int]
    lib.blaze_mobs_count.restype = ctypes.c_int
    lib.blaze_mobs_get.argtypes = [
        ctypes.c_void_p, ctypes.c_int, ctypes.c_int,
        ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_int),
        ctypes.POINTER(ctypes.c_double), ctypes.POINTER(ctypes.c_double),
        ctypes.POINTER(ctypes.c_double)]
    lib.blaze_mobs_get.restype = ctypes.c_int
    n = lib.blaze_mobs_count(ctypes.c_void_p(cu.h), 0)
    if n < 0:
        raise RuntimeError("blaze_mobs_count failed")
    rows = []
    for i in range(n):
        slot = ctypes.c_int()
        typ = ctypes.c_int()
        alive = ctypes.c_int()
        x = ctypes.c_double()
        y = ctypes.c_double()
        z = ctypes.c_double()
        r = lib.blaze_mobs_get(
            ctypes.c_void_p(cu.h), 0, i,
            ctypes.byref(slot), ctypes.byref(typ), ctypes.byref(alive),
            ctypes.byref(x), ctypes.byref(y), ctypes.byref(z))
        if r != 0:
            raise RuntimeError(f"blaze_mobs_get({i}) failed")
        rows.append({
            "slot": slot.value, "type": typ.value, "alive": alive.value,
            "x": x.value, "y": y.value, "z": z.value,
        })
    return rows


def print_mob_dump(tag, rows):
    print(f"    {tag} mobs n={len(rows)}")
    for row in rows:
        print(f"      {fmt_mob_row(row)}")


def region_margin(x, z, rx0, rz0, rnx, rnz):
    """Min horizontal distance from (x,z) to the region AABB edge (blocks)."""
    return min(x - rx0, rx0 + rnx - x, z - rz0, rz0 + rnz - z)


def pose_xyz_yaw_pitch(rec):
    x, y, z = struct.unpack_from("<ddd", rec, OFF["x"][0])
    yaw, pitch = struct.unpack_from("<ff", rec, OFF["yaw"][0])
    return x, y, z, yaw, pitch


def cam_pixel_diffs(a, b):
    """[(index, magma_id, blaze_id)] for every differing cam pixel."""
    lo, hi = OFF["cam"]
    ma = struct.unpack(f"<{OC_W * OC_H}H", a[lo:hi])
    ba = struct.unpack(f"<{OC_W * OC_H}H", b[lo:hi])
    return [(i, mv, bv) for i, (mv, bv) in enumerate(zip(ma, ba)) if mv != bv]


def first_cam_pixel_diff(a, b):
    """(pix_index, magma_id, blaze_id) for the first differing cam pixel."""
    diffs = cam_pixel_diffs(a, b)
    return diffs[0] if diffs else (None, None, None)


def sim_digests_match(real_par, blaze_par):
    """True/False over SIM_SUBSYSTEMS, or None when digests are unavailable."""
    if real_par is None or blaze_par is None:
        return None
    return all(real_par.digest[PARITY_INDEX[n]] ==
               blaze_par.digest[PARITY_INDEX[n]] for n in SIM_SUBSYSTEMS)


def classify_camera_envelope(snap, real_rec, blaze_rec, real_par, blaze_par):
    """Classify a gated mismatch as FAILED or BLOCKED (fixture camera OOR).

    BLOCKED needs ALL of: only cam/depth/edge diverge, every differing cam
    pixel is magma-block vs blaze-air-at-max-depth (the OOR signature - a
    ray that left the fixture), the eye is within OC_FAR of a horizontal
    region edge (so a ray CAN leave), and the per-tick sim digests still
    match. Missing digests are fail-closed: without them a real world/dig
    divergence could hide behind the same cam symptom."""
    if not gated_equal_except_obs(real_rec, blaze_rec):
        return FAILED, "non-observation gated field diverges"
    diffs = cam_pixel_diffs(real_rec, blaze_rec)
    if not diffs:
        return FAILED, "depth/edge diverge without a cam pixel diff"
    lo = OFF["depth"][0]
    not_oor = [(i, mid, bid) for i, mid, bid in diffs
               if not (mid != 0 and bid == 0 and blaze_rec[lo + i] == 255)]
    if not_oor:
        i, mid, bid = not_oor[0]
        return FAILED, (
            f"{len(not_oor)}/{len(diffs)} differing cam pixels are not the "
            f"out-of-region signature (first: pixel {i} magma id={mid} "
            f"blaze id={bid} blaze depth={blaze_rec[lo + i]}; OOR requires "
            f"magma block vs blaze air at depth 255)")
    sim_match = sim_digests_match(real_par, blaze_par)
    if sim_match is None:
        return FAILED, ("cam-only divergence but state digests are off "
                        "(--no-state-digest): cannot certify fixture OOR")
    if not sim_match:
        return FAILED, "sim subsystem digest mismatch; not pure camera OOR"
    x, y, z, yaw, pitch = pose_xyz_yaw_pitch(real_rec)
    rx0, _ry0, rz0, rnx, rny, rnz = read_snap_region(snap)
    margin = region_margin(x, z, rx0, rz0, rnx, rnz)
    if margin >= OC_FAR:
        return FAILED, (
            f"cam-only divergence with horizontal margin {margin:.3f} >= "
            f"OC_FAR={OC_FAR}: no ray can leave the fixture, so this is a "
            f"real camera divergence")
    i, mid, bid = diffs[0]
    return BLOCKED, (
        f"fixed-region camera out-of-region (OOR): {len(diffs)} cam pixels\n"
        f"    first pixel[{i}] row={i // OC_W} col={i % OC_W}: "
        f"magma id={mid} vs blaze air 0 (blaze depth 255 = ray hit nothing)\n"
        f"    pose world=({x:.4f},{y:.4f},{z:.4f}) yaw={yaw:.2f} "
        f"pitch={pitch:.2f}\n"
        f"    fixture region x=[{rx0},{rx0 + rnx}) z=[{rz0},{rz0 + rnz}) "
        f"dims={rnx}x{rny}x{rnz}\n"
        f"    horizontal margin to region edge={margin:.3f} < OC_FAR="
        f"{OC_FAR}: rays leave the fixture, blaze reads air, magma's "
        f"camreg reads the live world\n"
        f"    sim digests ({','.join(SIM_SUBSYSTEMS)}) all MATCH: the port "
        f"is fine, the fixture envelope is too small\n"
        f"    remedy: re-bake this stage with snapshot_r >= 49 "
        f"(make_snapshots.py CURRICULUM_R, default {64}) - t0 fixtures are "
        f"128x128x128 and gate byte-exact")


def report_state_digest_fail(seed, label, when, real_rec, blaze_rec, features):
    """Print first tick + feature + digests for a state-digest divergence.

    Returns False always (caller sets ok = False). `when` is 'INITIAL' or
    f'tick {t}'. This is the early-warning signal that catches world
    divergence hundreds of ticks before it reaches the camera planes.
    """
    status, detail, subsystem = parity_pair_status(real_rec, blaze_rec, features)
    kind = "BLOCKED" if status == BLOCKED else "FAILED"
    print(f"  seed {seed} [{label}] STATE DIGEST {kind} at {when} "
          f"(Magma tick={real_rec.tick}, Blaze tick={blaze_rec.tick}): "
          f"{detail}")
    if subsystem is not None:
        idx = PARITY_INDEX[subsystem]
        print(f"    feature: {subsystem}")
        print(f"    Magma: digest=0x{real_rec.digest[idx]:016x} "
              f"evidence={real_rec.evidence[idx]} "
              f"active={int(bool(real_rec.active & (1 << idx)))}")
        print(f"    Blaze: digest=0x{blaze_rec.digest[idx]:016x} "
              f"evidence={blaze_rec.evidence[idx]} "
              f"active={int(bool(blaze_rec.active & (1 << idx)))}")
        for field, magma_value, blaze_value in \
                parity_debug_differences(real_rec, blaze_rec, subsystem)[:8]:
            print(f"    differing scalar {field}: "
                  f"Magma={magma_value!r}, Blaze={blaze_value!r}")
    return False


def run_seed(seed, snap, actions, label, show_final_inv=False,
             state_digest=False):
    """Lockstep one action stream. Returns VERIFIED / FAILED / BLOCKED."""
    features = list(PARITY_SUPPORTED) if state_digest else []
    real = RealEnv(seed, snap, port_parity=state_digest)
    cu = Blaze1(snap, port_parity=state_digest)
    if cu.liquid:
        print(f"  note: seed {seed} snapshot region contains liquid")
    if state_digest:
        print(f"  state-digest: comparing {','.join(features)} every tick")
    status = VERIFIED
    ok = True
    exact_ticks = 0          # ticks byte-exact (+ digest-exact) before a stop

    def classify(when, a_rec, b_rec, act=None):
        """Print a divergence and return its FAILED/BLOCKED classification."""
        f = first_diff_field(a_rec, b_rec)
        head = (f"  seed {seed} [{label}] "
                + ("INITIAL obs differs" if when == "INITIAL"
                   else f"FIRST DIVERGENCE {when} "
                        f"(env tick {struct.unpack('<q', a_rec[4:12])[0]})")
                + f": field {f}")
        print(head)
        if act is not None:
            print(f"    action: {act}")
        print(f"    real:  {fmt_field(a_rec, f)}")
        print(f"    blaze: {fmt_field(b_rec, f)}")
        blaze_par = cu.parity() if state_digest else None
        real_par = real.parity_rec if state_digest else None
        st, detail = classify_camera_envelope(
            snap, a_rec, b_rec, real_par, blaze_par)
        print(f"    class: {'BLOCKED' if st == BLOCKED else 'FAILED'}")
        print(f"    {detail}")
        return st

    try:
        a_rec, b_rec = real.rec, cu.emit(1)
        rx0, _, rz0, rnx, _, rnz = read_snap_region(snap)
        px, _, pz, _, _ = pose_xyz_yaw_pitch(a_rec)
        margin0 = region_margin(px, pz, rx0, rz0, rnx, rnz)
        print(f"  fixture {os.path.basename(snap)}: region {rnx}x{rnz} "
              f"horizontal, eye margin {margin0:.2f} blocks "
              f"({'>=' if margin0 >= OC_FAR else '<'} OC_FAR={OC_FAR}"
              f"{'' if margin0 >= OC_FAR else ' - camera can leave the fixture'})")
        if not gated_equal(a_rec, b_rec):
            status = classify("INITIAL", a_rec, b_rec)
            ok = False
        if ok and state_digest:
            blaze_p = cu.parity()
            pstatus, _, _ = parity_pair_status(
                real.parity_rec, blaze_p, features)
            if pstatus != VERIFIED:
                report_state_digest_fail(
                    seed, label, "INITIAL", real.parity_rec, blaze_p, features)
                status = FAILED
                ok = False
        for t, act in enumerate(actions):
            if not ok:
                break
            a_rec = real.step(act)
            b_rec = cu.step(act)
            if not gated_equal(a_rec, b_rec):
                status = classify(f"tick {t}", a_rec, b_rec, act)
                d = cu.debug()
                print(f"    blaze state: pos=({d[0]:.17g},{d[1]:.17g},"
                      f"{d[2]:.17g}) mot=({d[3]:.17g},{d[4]:.17g},{d[5]:.17g})"
                      f" og={d[8]:.0f} dig=({d[12]:.4f},{d[13]:.0f},"
                      f"{d[14]:.0f})")
                ok = False
                break
            if state_digest:
                blaze_p = cu.parity()
                pstatus, _, _ = parity_pair_status(
                    real.parity_rec, blaze_p, features)
                if pstatus != VERIFIED:
                    report_state_digest_fail(
                        seed, label, f"tick {t}", real.parity_rec, blaze_p,
                        features)
                    print(f"    action: {act}")
                    status = FAILED
                    ok = False
                    break
            exact_ticks = t + 1
    finally:
        if ok and show_final_inv:
            names = ["log", "planks", "stick", "cobble", "table", "wpick",
                     "spick", "coal", "torch"]
            inv = fmt_field(a_rec, "inv_counts")
            print("  final inv_counts: "
                  + " ".join(f"{n}={c}" for n, c in zip(names, inv) if c))
        real.close()
        cu.close()
    digest_note = (" + state digests "
                   f"({','.join(features)})" if state_digest else "")
    if ok:
        print(f"  seed {seed} [{label}]: {len(actions)} ticks, ZERO diffs"
              f"{digest_note}")
    else:
        print(f"  seed {seed} [{label}]: "
              f"{'BLOCKED' if status == BLOCKED else 'FAILED'} after "
              f"{exact_ticks}/{len(actions)} ticks zero-diff{digest_note}")
    return status


def parity_mask_names(mask):
    names = [name for i, name in enumerate(PARITY_NAMES) if mask & (1 << i)]
    if mask & PARITY_UNREPRESENTED_SNAPSHOT:
        names.append("unrepresented_snapshot_state")
        mask &= ~PARITY_UNREPRESENTED_SNAPSHOT
    unknown = mask & ~((1 << len(PARITY_NAMES)) - 1)
    if unknown:
        names.append(f"unknown(0x{unknown:x})")
    return ",".join(names) if names else "none"




def parity_pair_status(real_rec, blaze_rec, features):
    requested = sum(1 << PARITY_INDEX[name] for name in features)
    for source, rec in (("Magma", real_rec), ("Blaze", blaze_rec)):
        missing_implemented = requested & ~rec.implemented
        missing_measured = requested & ~rec.measured
        if missing_implemented or missing_measured:
            parts = []
            if missing_implemented:
                parts.append(
                    "not implemented: "
                    + parity_mask_names(missing_implemented))
            if missing_measured:
                parts.append(
                    "not measured: "
                    + parity_mask_names(missing_measured))
            return BLOCKED, f"{source} " + "; ".join(parts), None
    if real_rec.tick != blaze_rec.tick:
        return (FAILED,
                f"tick differs: Magma={real_rec.tick}, Blaze={blaze_rec.tick}",
                None)
    for name in features:
        idx = PARITY_INDEX[name]
        if real_rec.digest[idx] != blaze_rec.digest[idx]:
            return FAILED, f"subsystem {name} digest differs", name
        if real_rec.evidence[idx] != blaze_rec.evidence[idx]:
            return FAILED, f"subsystem {name} evidence differs", name
        real_active = bool(real_rec.active & (1 << idx))
        blaze_active = bool(blaze_rec.active & (1 << idx))
        if real_active != blaze_active:
            return FAILED, f"subsystem {name} active state differs", name
    return VERIFIED, None, None

def parity_debug_value(index, bits):
    if index in (0, 1, 2, 3, 4, 5, 26, 27):
        return struct.unpack("<d", struct.pack("<Q", bits))[0]
    if index in (6, 7, 9, 12, 14, 15):
        return struct.unpack("<f", struct.pack("<I", bits & 0xFFFFFFFF))[0]
    value = bits & 0xFFFFFFFF
    return value - (1 << 32) if value & (1 << 31) else value


def parity_debug_differences(real_rec, blaze_rec, subsystem):
    idx = PARITY_INDEX[subsystem]
    differences = []

    def note(name, magma_value, blaze_value):
        if magma_value != blaze_value:
            differences.append((name, magma_value, blaze_value))

    note("evidence", real_rec.evidence[idx], blaze_rec.evidence[idx])
    note("active",
         int(bool(real_rec.active & (1 << idx))),
         int(bool(blaze_rec.active & (1 << idx))))

    if subsystem == "crafting":
        note("craft_attempts",
             parity_debug_value(29, real_rec.debug_bits[29] >> 32),
             parity_debug_value(29, blaze_rec.debug_bits[29] >> 32))
        return differences

    if subsystem == "containers":
        for i in range(28, 32):
            note(PARITY_DEBUG_NAMES[i],
                 parity_debug_value(i, real_rec.debug_bits[i]),
                 parity_debug_value(i, blaze_rec.debug_bits[i]))
        note("container_opens",
             parity_debug_value(28, real_rec.debug_bits[28] >> 32),
             parity_debug_value(28, blaze_rec.debug_bits[28] >> 32))
        note("cursor_item",
             parity_debug_value(30, real_rec.debug_bits[30] >> 32),
             parity_debug_value(30, blaze_rec.debug_bits[30] >> 32))
        real_cursor = real_rec.debug_bits[31] >> 32
        blaze_cursor = blaze_rec.debug_bits[31] >> 32
        note("cursor_count", real_cursor & 0xffff, blaze_cursor & 0xffff)
        real_meta = (real_cursor >> 16) & 0xffff
        blaze_meta = (blaze_cursor >> 16) & 0xffff
        note("cursor_meta",
             real_meta - (1 << 16) if real_meta & (1 << 15) else real_meta,
             blaze_meta - (1 << 16) if blaze_meta & (1 << 15) else blaze_meta)
        return differences

    indices = PARITY_DEBUG_RANGES.get(
        subsystem, range(len(PARITY_DEBUG_NAMES)))
    for i in indices:
        note(PARITY_DEBUG_NAMES[i],
             parity_debug_value(i, real_rec.debug_bits[i]),
             parity_debug_value(i, blaze_rec.debug_bits[i]))
    return differences


def print_initial_parity(seed, label, real_rec, blaze_rec, features):
    print(f"  seed {seed} [{label}] PARY observation 0: "
          f"Magma tick={real_rec.tick}, Blaze tick={blaze_rec.tick}")
    for name in features:
        idx = PARITY_INDEX[name]
        match = "MATCH" if real_rec.digest[idx] == blaze_rec.digest[idx] \
            else "MISMATCH"
        print(f"    {name}: Magma digest=0x{real_rec.digest[idx]:016x} "
              f"evidence={real_rec.evidence[idx]} "
              f"active={int(bool(real_rec.active & (1 << idx)))}; "
              f"Blaze digest=0x{blaze_rec.digest[idx]:016x} "
              f"evidence={blaze_rec.evidence[idx]} "
              f"active={int(bool(blaze_rec.active & (1 << idx)))} [{match}]")


def run_seed_parity(seed, snap, actions, label, features,
                    strict_capabilities=False, require_evidence=True,
                    track_liquid=False, result=None, mobs_on=False,
                    natural_spawn=False, natural_spawn_passive=False,
                    dump_mobs=False):
    """Lockstep Magma vs Blaze CPU on PARY digests. Existing callers unchanged.

    require_evidence: default True is the --port-parity gate (zero evidence
    on a requested feature is BLOCKED). False keeps digest equality as the
    only predicate, so a quiescent fluids fixture can still VERIFY.
    track_liquid: fingerprint blaze region liquid cells (ids 8-11) at every
    observation. A change vs INITIAL is recorded; it does not alter status.
    result: optional dict filled with exact_ticks, first_div, liquid_*.
    """
    real = None
    cu = None
    real_evidence = {name: False for name in features}
    blaze_evidence = {name: False for name in features}
    exact_ticks = 0
    first_div = None
    liquid0 = None
    liquid_changed_at = None
    liquid_n = None

    def out(status):
        if result is not None:
            result["status"] = status
            result["exact_ticks"] = exact_ticks
            result["first_div"] = first_div
            result["n_actions"] = len(actions)
            result["liquid_n"] = liquid_n
            result["liquid_changed_at"] = liquid_changed_at
        return status

    def note_evidence(real_rec, blaze_rec):
        for name in features:
            idx = PARITY_INDEX[name]
            real_evidence[name] |= bool(real_rec.evidence[idx])
            blaze_evidence[name] |= bool(blaze_rec.evidence[idx])

    def note_liquid(when):
        nonlocal liquid0, liquid_changed_at, liquid_n
        if not track_liquid or liquid_changed_at is not None:
            return
        key = liquid_cells_key(cu.cells_u16())
        if liquid0 is None:
            liquid0 = key
            liquid_n = key[0]
            print(f"  seed {seed} [{label}] liquid fingerprint n={key[0]} "
                  f"xor=0x{key[1]:016x}")
            return
        if key != liquid0:
            liquid_changed_at = when
            print(f"  seed {seed} [{label}] liquid-active at {when}: "
                  f"n {liquid0[0]}->{key[0]} "
                  f"xor 0x{liquid0[1]:016x}->0x{key[1]:016x}")

    try:
        cu = Blaze1(snap, port_parity=True)
        requested = sum(1 << PARITY_INDEX[name] for name in features)
        if strict_capabilities:
            unsupported = requested & ~cu.capabilities
            missing_requirements = cu.requirements & ~cu.capabilities
            if unsupported:
                print(f"  seed {seed} [{label}] BLOCKED before stepping: "
                      "Blaze lacks requested capabilities "
                      f"{parity_mask_names(unsupported)}")
                first_div = "INITIAL"
                return out(BLOCKED)
            if missing_requirements:
                print(f"  seed {seed} [{label}] BLOCKED before stepping: "
                      "snapshot requires unsupported capabilities "
                      f"{parity_mask_names(missing_requirements)}")
                first_div = "INITIAL"
                return out(BLOCKED)

        extra = []
        if "weather" in features:
            extra.extend(["--weather", "on"])
        if "elytra" in features:
            extra.extend(["--set", "elytra=1"])
            cu.lib.blaze_set_elytra_enabled.argtypes = [
                ctypes.c_void_p, ctypes.c_int]
            cu.lib.blaze_set_elytra_enabled.restype = ctypes.c_int
            if cu.lib.blaze_set_elytra_enabled(ctypes.c_void_p(cu.h), 1) != 0:
                raise RuntimeError("blaze_set_elytra_enabled failed")
        if mobs_on:
            extra.extend(["--set", "mobs=1"])
            cu.lib.blaze_set_mobs_enabled.argtypes = [
                ctypes.c_void_p, ctypes.c_int]
            cu.lib.blaze_set_mobs_enabled.restype = ctypes.c_int
            if cu.lib.blaze_set_mobs_enabled(ctypes.c_void_p(cu.h), 1) != 0:
                raise RuntimeError("blaze_set_mobs_enabled failed")
        if natural_spawn:
            extra.extend(["--set", "natural_spawn=1", "--set", "set_time=18000"])
            cu.lib.blaze_set_natural_spawn.argtypes = [
                ctypes.c_void_p, ctypes.c_int]
            cu.lib.blaze_set_natural_spawn.restype = ctypes.c_int
            if cu.lib.blaze_set_natural_spawn(ctypes.c_void_p(cu.h), 1) != 0:
                raise RuntimeError("blaze_set_natural_spawn failed")
            cu.lib.blaze_set_world_time.argtypes = [
                ctypes.c_void_p, ctypes.c_longlong]
            cu.lib.blaze_set_world_time.restype = ctypes.c_int
            if cu.lib.blaze_set_world_time(ctypes.c_void_p(cu.h), 18000) != 0:
                raise RuntimeError("blaze_set_world_time failed")
        if natural_spawn_passive:
            extra.extend(["--set", "natural_spawn_passive=1",
                          "--set", "set_time=6000"])
            cu.lib.blaze_set_natural_spawn_passive.argtypes = [
                ctypes.c_void_p, ctypes.c_int]
            cu.lib.blaze_set_natural_spawn_passive.restype = ctypes.c_int
            if cu.lib.blaze_set_natural_spawn_passive(
                    ctypes.c_void_p(cu.h), 1) != 0:
                raise RuntimeError("blaze_set_natural_spawn_passive failed")
            cu.lib.blaze_set_world_time.argtypes = [
                ctypes.c_void_p, ctypes.c_longlong]
            cu.lib.blaze_set_world_time.restype = ctypes.c_int
            if cu.lib.blaze_set_world_time(ctypes.c_void_p(cu.h), 6000) != 0:
                raise RuntimeError("blaze_set_world_time failed")
        real = RealEnv(seed, snap, port_parity=True, magma_args=extra)
        cu.emit(1)
        real_parity = real.parity_rec
        blaze_parity = cu.parity()
        print_initial_parity(
            seed, label, real_parity, blaze_parity, features)
        if dump_mobs:
            print_mob_dump("Magma fixture", parse_bsnp_mobs(snap))
            print_mob_dump("Blaze live", blaze_live_mobs(cu))
        note_evidence(real_parity, blaze_parity)
        note_liquid("INITIAL")
        status, detail, _ = parity_pair_status(
            real_parity, blaze_parity, features)
        if status != VERIFIED:
            first_div = "INITIAL"
            print(f"  seed {seed} [{label}] "
                  f"{'BLOCKED' if status == BLOCKED else 'FAILED'} "
                  f"at observation 0: {detail}")
            return out(status)

        for observation, act in enumerate(actions, 1):
            real_obs = real.step(act)
            blaze_obs = cu.step(act)
            real_parity = real.parity_rec
            blaze_parity = cu.parity()
            note_evidence(real_parity, blaze_parity)
            note_liquid(observation - 1)
            if dump_mobs and "mobs" in features:
                mi = PARITY_INDEX["mobs"]
                print(f"    t={real_parity.tick} mobs evidence "
                      f"Magma={real_parity.evidence[mi]} "
                      f"Blaze={blaze_parity.evidence[mi]}")
            status, detail, subsystem = parity_pair_status(
                real_parity, blaze_parity, features)
            if status != VERIFIED:
                first_div = observation - 1
                kind = "BLOCKED" if status == BLOCKED else "FAILED"
                print(f"  seed {seed} [{label}] {kind} at observation "
                      f"{observation} (Magma tick={real_parity.tick}, "
                      f"Blaze tick={blaze_parity.tick}): {detail}")
                print(f"    action: {act}")
                if subsystem is not None:
                    idx = PARITY_INDEX[subsystem]
                    print(f"    Magma {subsystem}: "
                          f"digest=0x{real_parity.digest[idx]:016x} "
                          f"evidence={real_parity.evidence[idx]} "
                          f"active={int(bool(real_parity.active & (1 << idx)))}")
                    print(f"    Blaze {subsystem}: "
                          f"digest=0x{blaze_parity.digest[idx]:016x} "
                          f"evidence={blaze_parity.evidence[idx]} "
                          f"active={int(bool(blaze_parity.active & (1 << idx)))}")
                    for field, magma_value, blaze_value in \
                            parity_debug_differences(
                                real_parity, blaze_parity, subsystem)[:8]:
                        print(f"    differing scalar {field}: "
                              f"Magma={magma_value!r}, Blaze={blaze_value!r}")
                    if dump_mobs and subsystem == "mobs":
                        dump_path = os.path.join(
                            os.environ.get("TMPDIR", "/tmp"),
                            f"natspawn2_magma_mobs_t{real_parity.tick}.bsnp")
                        try:
                            real.step({
                                "snapshot": dump_path,
                                "snapshot_bounds": "inherit",
                            })
                            print_mob_dump("Magma live",
                                           parse_bsnp_mobs(dump_path))
                        except Exception as exc:  # noqa: BLE001
                            print(f"    Magma mob dump failed: {exc}")
                        try:
                            print_mob_dump("Blaze live",
                                           blaze_live_mobs(cu))
                        except Exception as exc:  # noqa: BLE001
                            print(f"    Blaze mob dump failed: {exc}")
                    if subsystem == "observations":
                        for field in ("cam", "depth", "edge"):
                            lo, hi = OFF[field]
                            if field == "cam":
                                magma_values = struct.unpack(
                                    "<2304H", real_obs[lo:hi])
                                blaze_values = struct.unpack(
                                    "<2304H", blaze_obs[lo:hi])
                            else:
                                magma_values = real_obs[lo:hi]
                                blaze_values = blaze_obs[lo:hi]
                            differing = [
                                i for i, (magma_value, blaze_value)
                                in enumerate(zip(magma_values, blaze_values))
                                if magma_value != blaze_value
                            ]
                            if differing:
                                print(f"    {field}: {len(differing)} "
                                      "elements differ; first index "
                                      f"{differing[0]}")
                return out(status)
            exact_ticks = observation

        no_evidence = [
            name for name in features
            if not real_evidence[name] or not blaze_evidence[name]
        ]
        if require_evidence and no_evidence:
            for name in no_evidence:
                print(f"  seed {seed} [{label}] BLOCKED: subsystem {name} "
                      "has zero fixture evidence "
                      f"(Magma={int(real_evidence[name])}, "
                      f"Blaze={int(blaze_evidence[name])})")
            return out(BLOCKED)
        print(f"  seed {seed} [{label}]: VERIFIED {len(actions)} ticks "
              f"for {','.join(features)}"
              + ("" if require_evidence else " (evidence not required)"))
        return out(VERIFIED)
    # The fail-closed gate must classify any ABI/runtime error as FAILED.
    except Exception as exc:  # noqa: BLE001
        print(f"  seed {seed} [{label}] FAILED: port parity unavailable: {exc}")
        if first_div is None:
            first_div = "INITIAL"
        if result is not None:
            result["error"] = str(exc)
        return out(FAILED)
    finally:
        if real is not None:
            real.close()
        if cu is not None:
            cu.close()


def stream_result(statuses, noun, state_digest):
    """Print the BOLR gate summary and return the process exit status.

    PASS only when every stream verified: a BLOCKED stream (fixture camera
    envelope, or a missing .bsnp) exits 3 so it can never be read as green,
    and any FAILED stream still exits 1."""
    verified = sum(s == VERIFIED for s in statuses)
    blocked = sum(s == BLOCKED for s in statuses)
    failed = sum(s == FAILED for s in statuses)
    if failed or not statuses:
        name, status = "FAIL", FAILED
    elif blocked:
        name, status = "BLOCKED", BLOCKED
    else:
        name, status = "PASS", VERIFIED
    digest_note = " + state digests" if state_digest else ""
    extra = ""
    if blocked:
        extra += (f", {blocked} BLOCKED (fixture camera envelope / missing "
                  f"fixture)")
    if failed:
        extra += f", {failed} FAILED"
    print(f"\n{name}: {verified}/{len(statuses)} {noun} "
          f"zero-diff{digest_note}{extra}")
    return status


def port_parity_result(statuses, noun="fixtures"):
    if FAILED in statuses or not statuses:
        status = FAILED
        name = "FAILED"
    elif BLOCKED in statuses:
        status = BLOCKED
        name = "BLOCKED"
    else:
        status = VERIFIED
        name = "VERIFIED"
    verified = sum(value == VERIFIED for value in statuses)
    print(f"\n{name}: {verified}/{len(statuses)} {noun} verified")
    return status


def run_port_parity(args, seeds, features):
    if args.iron:
        seed = args.chain_seed
        snap = os.path.join(SNAPS, f"s{seed}_t0_iron.bsnp")
        acts_path = os.path.join(RL, "out", f"iron_actions_s{seed}.json")
        if not os.path.exists(snap) or not os.path.exists(acts_path):
            print(f"BLOCKED: iron gate missing {snap} or {acts_path}")
            return port_parity_result([BLOCKED], "iron fixture")
        with open(acts_path) as f:
            acts = json.load(f)
        status = run_seed_parity(
            seed, snap, acts, f"iron stage x{len(acts)}", features,
            args.strict_capabilities, mobs_on=getattr(args, "mobs_on", False),
            natural_spawn=getattr(args, "natural_spawn", False),
            natural_spawn_passive=getattr(args, "natural_spawn_passive", False),
            dump_mobs=getattr(args, "dump_mobs", False))
        return port_parity_result([status], "iron fixture")

    if args.chain:
        seed = args.chain_seed
        snap = args.snapshot or os.path.join(SNAPS, f"s{seed}_t0.bsnp")
        acts_path = (args.tape if args.tape is not None
                     else os.path.join(RL, "out", f"chain_actions_s{seed}.json"))
        if not os.path.exists(snap) or not os.path.exists(acts_path):
            print(f"BLOCKED: chain gate missing {snap} or {acts_path}")
            return port_parity_result([BLOCKED], "chain fixture")
        with open(acts_path) as f:
            acts = json.load(f)
        acts = [{k: v for k, v in act.items() if k != "snapshot"}
                for act in acts]
        if (args.expected_chain_actions is not None and \
                len(acts) != args.expected_chain_actions):
            print("BLOCKED: full chain fixture has "
                  f"{len(acts)} actions, expected "
                  f"{args.expected_chain_actions}")
            return port_parity_result([BLOCKED], "chain fixture")
        status = run_seed_parity(
            seed, snap, acts, f"full chain x{len(acts)}", features,
            args.strict_capabilities, mobs_on=getattr(args, "mobs_on", False),
            natural_spawn=getattr(args, "natural_spawn", False),
            natural_spawn_passive=getattr(args, "natural_spawn_passive", False),
            dump_mobs=getattr(args, "dump_mobs", False))
        return port_parity_result([status], "chain fixture")

    statuses = []
    for seed in seeds:
        snap = args.snapshot or os.path.join(
            SNAPS, f"s{seed}_d{args.stage}.bsnp")
        if not os.path.exists(snap):
            print(f"  seed {seed}: BLOCKED missing {snap} "
                  "(run make_snapshots.py)")
            statuses.append(BLOCKED)
            continue
        rng = Rng(0xC0A1 ^ (seed * 2654435761 & 0xFFFFFFFF))
        actions = [rand_action(rng) for _ in range(args.ticks)]
        statuses.append(run_seed_parity(
            seed, snap, actions, f"random x{args.ticks}", features,
            args.strict_capabilities))

    if args.episodes:
        if not os.path.exists(args.episodes):
            print(f"  episodes: BLOCKED missing {args.episodes}")
            statuses.append(BLOCKED)
        else:
            with open(args.episodes) as f:
                for li, line in enumerate(f):
                    line = line.strip()
                    if not line:
                        continue
                    acts = json.loads(line)
                    if isinstance(acts, dict):
                        acts = acts.get("actions", [])
                    acts = [
                        {k: v for k, v in act.items() if k != "snapshot"}
                        for act in acts
                    ][:2000]
                    seed = seeds[0]
                    snap = os.path.join(
                        SNAPS, f"s{seed}_d{args.stage}.bsnp")
                    if not os.path.exists(snap):
                        print(f"  episode {li}: BLOCKED missing {snap}")
                        statuses.append(BLOCKED)
                    else:
                        statuses.append(run_seed_parity(
                            seed, snap, acts, f"episode {li}", features,
                            args.strict_capabilities))
                    if li >= 2:
                        break
    return port_parity_result(statuses)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--seeds", default=",".join(map(str, TRAIN_SEEDS)))
    ap.add_argument("--ticks", type=int, default=1000)
    ap.add_argument("--stage", default="6.0")
    ap.add_argument(
        "--snapshot",
        help="explicit .bsnp for a parity fixture")
    ap.add_argument("--episodes", default=None,
                    help="jsonl of real trained action lists (one per line)")
    ap.add_argument("--chain", action="store_true",
                    help="full spawn-to-torch chain gate: replay "
                         "rl/out/chain_actions_s10.json (craft/interact/use "
                         "INCLUDED) from the fresh-spawn tick-0 snapshot "
                         "rl/out/snaps/s10_t0.bsnp")
    ap.add_argument(
        "--tape",
        help="with --chain: override the action-stream JSON path "
             "(default: rl/out/chain_actions_s{seed}.json)")
    ap.add_argument("--chain-seed", type=int, default=10)
    ap.add_argument(
        "--expected-chain-actions", type=int,
        help="fail closed unless --chain loads exactly this many actions")
    ap.add_argument("--iron", action="store_true",
                    help="iron-stage gate: replay "
                         "rl/out/iron_actions_s10.json (craft:6/7 + smelt:1 "
                         "+ furnace place/tick INCLUDED) from the inventory-"
                         "injected snapshot rl/out/snaps/s10_t0_iron.bsnp "
                         "(regenerate both with make_iron_actions.py)")
    ap.add_argument(
        "--port-parity", action="store_true",
        help="compare opt-in PARY subsystem records instead of legacy BOLR "
             "gated fields")
    ap.add_argument(
        "--features",
        help="comma-separated PARY subsystem names to verify (requires "
             "--port-parity)")
    ap.add_argument(
        "--strict-capabilities", action="store_true",
        help="fail closed before stepping if requested or snapshot-required "
             "capabilities are unavailable (requires --port-parity)")
    ap.add_argument(
        "--mobs-on", action="store_true",
        help="enable magma --set mobs=1 and blaze hostile AI (mobs row)")
    ap.add_argument(
        "--natural-spawn", action="store_true",
        help="enable WorldEntitySpawner (magma --set natural_spawn=1 set_time=18000)")
    ap.add_argument(
        "--natural-spawn-passive", action="store_true",
        help="enable CREATURE WorldEntitySpawner (natural_spawn_passive=1 set_time=6000)")
    ap.add_argument(
        "--dump-mobs", action="store_true",
        help="print Magma/Blaze living-slot tables at observation 0 and "
             "at the first mobs PARY mismatch (harness only)")
    ap.add_argument(
        "--no-state-digest", action="store_true",
        help="opt out of the default-on per-tick PARY state-digest pass "
             "on --chain/--iron (BOLR-only)")
    args = ap.parse_args()
    seeds = [int(s) for s in args.seeds.split(",")]
    t0 = time.perf_counter()
    if args.expected_chain_actions is not None and not args.chain:
        ap.error("--expected-chain-actions requires --chain")
    if args.tape is not None and not args.chain:
        ap.error("--tape requires --chain")
    if args.snapshot and (not args.port_parity or args.iron):
        ap.error("--snapshot requires --port-parity and cannot be combined "
                 "with --iron")
    if args.snapshot and not args.chain and len(seeds) != 1:
        ap.error("--snapshot requires exactly one --seeds value unless "
                 "--chain selects its own seed")
    if not args.port_parity:
        if args.features is not None or args.strict_capabilities:
            ap.error("--features and --strict-capabilities require --port-parity")
    else:
        if args.features is None:
            ap.error("--port-parity requires an explicit --features list")
        requested_features = [
            name.strip() for name in args.features.split(",") if name.strip()
        ]
        if not requested_features:
            ap.error("--features must name at least one subsystem")
        unknown = sorted(set(requested_features) - set(PARITY_NAMES))
        if unknown:
            ap.error("unknown parity feature(s): " + ",".join(unknown))
        requested_features = list(dict.fromkeys(requested_features))
        status = run_port_parity(args, seeds, requested_features)
        print(f"runtime: {time.perf_counter() - t0:.2f}s")
        sys.exit(status)

    statuses = []
    # Default-on state digests: every tick compares Magma PARY vs Blaze CPU
    # for all BP_IMPLEMENTED subsystems, in addition to the BOLR gated-field
    # byte compare. --no-state-digest reverts to BOLR-only (pre-state-gate
    # behavior). The seeds path needs them too: a cam divergence may only be
    # classified as a fixture-envelope BLOCK when the digests prove the sim
    # still matches, so without them classify_camera_envelope fails closed.
    state_digest = not args.no_state_digest

    if args.iron:
        seed = args.chain_seed
        snap = os.path.join(SNAPS, f"s{seed}_t0_iron.bsnp")
        acts_path = os.path.join(RL, "out", f"iron_actions_s{seed}.json")
        if not os.path.exists(snap) or not os.path.exists(acts_path):
            print(f"iron gate: missing {snap} or {acts_path} "
                  f"(run make_iron_actions.py)")
            sys.exit(1)
        blocker = snapshot_dynamics_blocker(snap)
        if blocker:
            print(f"BLOCKED before stepping: {blocker}")
            sys.exit(BLOCKED)
        acts = json.load(open(acts_path))
        statuses.append(run_seed(seed, snap, acts, f"iron stage x{len(acts)}",
                                 show_final_inv=True,
                                 state_digest=state_digest))
        status = stream_result(statuses, "iron stream", state_digest)
        print(f"runtime: {time.perf_counter() - t0:.2f}s")
        sys.exit(status)
    if args.chain:
        seed = args.chain_seed
        snap = os.path.join(SNAPS, f"s{seed}_t0.bsnp")
        acts_path = (args.tape if args.tape is not None
                     else os.path.join(RL, "out", f"chain_actions_s{seed}.json"))
        if not os.path.exists(snap) or not os.path.exists(acts_path):
            print(f"chain gate: missing {snap} or {acts_path}")
            sys.exit(1)
        blocker = snapshot_dynamics_blocker(snap)
        if blocker:
            print(f"BLOCKED before stepping: {blocker}")
            sys.exit(BLOCKED)
        acts = json.load(open(acts_path))
        # replay the chain EXACTLY as recorded - craft/interact/use are now
        # simulated in blaze; only a stray "snapshot" key would be protocol-
        # level (none in the committed chains).
        acts = [{k: v for k, v in a.items() if k != "snapshot"} for a in acts]
        statuses.append(run_seed(seed, snap, acts, f"full chain x{len(acts)}",
                                 show_final_inv=True,
                                 state_digest=state_digest))
        status = stream_result(statuses, "chain stream", state_digest)
        print(f"runtime: {time.perf_counter() - t0:.2f}s")
        sys.exit(status)
    for seed in seeds:
        snap = os.path.join(SNAPS, f"s{seed}_d{args.stage}.bsnp")
        if not os.path.exists(snap):
            # Fail closed: a vanished fixture must not silently shrink the
            # denominator into a green 4/4 (bake attrition looks like a pass).
            print(f"  seed {seed}: BLOCKED missing {snap} "
                  f"(run make_snapshots.py)")
            statuses.append(BLOCKED)
            continue
        rng = Rng(0xC0A1 ^ (seed * 2654435761 & 0xFFFFFFFF))
        actions = [rand_action(rng) for _ in range(args.ticks)]
        statuses.append(run_seed(seed, snap, actions, f"random x{args.ticks}",
                                 state_digest=state_digest))

    if args.episodes and os.path.exists(args.episodes):
        with open(args.episodes) as f:
            for li, line in enumerate(f):
                line = line.strip()
                if not line:
                    continue
                acts = json.loads(line)
                if isinstance(acts, dict):
                    acts = acts.get("actions", [])
                # strip protocol primitives outside the learned action space
                # (craft/interact/snapshot): blaze does not implement them, so
                # BOTH envs replay the identical filtered stream.
                acts = [{k: v for k, v in a.items()
                         if k not in ("craft", "interact", "snapshot", "use")}
                        for a in acts][:2000]
                # trained streams may carry cam:0 repeats; keep them - the
                # real env and blaze share the want_cam semantics.
                seed = seeds[0]
                snap = os.path.join(SNAPS, f"s{seed}_d{args.stage}.bsnp")
                statuses.append(run_seed(seed, snap, acts, f"episode {li}",
                                         state_digest=state_digest))
                if li >= 2:
                    break

    sys.exit(stream_result(statuses, "streams", state_digest))


if __name__ == "__main__":
    main()
