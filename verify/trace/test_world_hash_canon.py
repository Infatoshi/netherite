"""World-hash packing: BlockDoublePlant upper FACING must be round-trip stable.

Evidence from tape 20260803T055113Z_vanilla_s80302... at anchor [122,70,255]:
  - Java wfnv (live getMetaFromState) = b0a2c27af21ef1d9
  - MCA / raw magma nearby_hash       = d2679c1f94f72c6a
  - Single-cell cause: (125,71,255) id=175 meta 11 (Anvil) vs meta 10 (live
    UPPER+NORTH; EnumFacing.NORTH.horizontalIndex == 2, meta = 8|2).

BlockDoublePlant.getStateFromMeta drops FACING, so Anvil facing nibbles are not
part of the digest domain both sides can share.
"""
from __future__ import annotations

import snapshot_patch

FNV_OFFSET = 1469598103934665603
FNV_PRIME = 0x100000001B3
MASK64 = (1 << 64) - 1

# Measured on the 80302 recstart MCA at anchor [122,70,255] (official
# snapshot_patch._read_mca_states, pre-canon). Only the double-plant upper at
# (125,71,255) differs from the Java digest domain.
JAVA_WFA = (122, 70, 255)
JAVA_WFNV = "b0a2c27af21ef1d9"
MCA_RAW_FNV = "d2679c1f94f72c6a"
DPLANT_CELL = (125, 71, 255)
DPLANT_MCA_META = 11
DPLANT_LIVE_META = 10  # UPPER + NORTH


def _fnv_volume(states_zyx_iter):
    """FNV-1a over packed states in z-outer, y, x-inner order (script.c)."""
    h = FNV_OFFSET
    for s in states_zyx_iter:
        h ^= s & 0xFFFFFFFF
        h = (h * FNV_PRIME) & MASK64
    return f"{h:016x}"


def test_canon_anvil_state_double_plant_upper_to_north():
    for facing_meta in range(8, 16):
        raw = (175 << 4) | facing_meta
        got = snapshot_patch._canon_anvil_state(raw)
        assert got == (175 << 4) | (8 | 2), facing_meta
    # Lower half variants are untouched.
    for variant in range(6):
        raw = (175 << 4) | variant
        assert snapshot_patch._canon_anvil_state(raw) == raw
    # Unrelated blocks untouched.
    assert snapshot_patch._canon_anvil_state((2 << 4) | 0) == (2 << 4)
    assert snapshot_patch._canon_anvil_state((18 << 4) | 8) == (18 << 4) | 8


def test_synthetic_double_plant_hash_packing_equivalence():
    """Canonical meta 11 hashes exactly like live-client meta 10."""
    ax, ay, az = JAVA_WFA
    # Direct check of the packing constants the gate cares about:
    assert DPLANT_LIVE_META == 8 | 2
    assert snapshot_patch._canon_anvil_state(
        (175 << 4) | DPLANT_MCA_META) == (175 << 4) | DPLANT_LIVE_META

    # Synthetic volume proves that canon produces the same packed input as
    # meta 10 at the measured cell. The full measured digest identity is
    # covered by the MCA-backed test below when the recorded world is present.
    body = []
    plant_index = None
    i = 0
    for z in range(az - 4, az + 5):
        for y in range(ay - 4, ay + 5):
            for x in range(ax - 4, ax + 5):
                if (x, y, z) == DPLANT_CELL:
                    plant_index = i
                    body.append((175 << 4) | DPLANT_MCA_META)
                else:
                    # Placeholder: this synthetic case checks packing
                    # equivalence, not the measured absolute digest.
                    body.append(0)
                i += 1
    assert plant_index is not None

    # The synthetic stream differs only at the measured plant cell.
    body = [0] * 729
    body[plant_index] = (175 << 4) | DPLANT_MCA_META
    h11 = _fnv_volume(body)
    body[plant_index] = (175 << 4) | DPLANT_LIVE_META
    h10 = _fnv_volume(body)
    assert h11 != h10
    assert h11 != JAVA_WFNV  # empty body is not the real volume
    # Canon equivalence: packing 11 through _canon_anvil_state equals packing 10.
    body[plant_index] = snapshot_patch._canon_anvil_state(
        (175 << 4) | DPLANT_MCA_META)
    assert _fnv_volume(body) == h10


def test_measured_80302_t0_hashes_if_mca_readable():
    """When the recorded tape MCA is present, prove the full-volume identity."""
    from pathlib import Path

    region = (
        Path(__file__).resolve().parents[1]
        / "tapes"
        / "20260803T055113Z_vanilla_s80302_survival_default_rd8_837eae74_world"
        / "region"
    )
    if not region.is_dir():
        import pytest
        pytest.skip("main-tree 80302 MCA not mounted")

    # Temporarily read raw (pre-canon) by using nbt directly for the one cell
    # and snapshot_patch for the full volume (now canons upper dplants).
    ax, ay, az = JAVA_WFA
    cache = {}

    def get_raw(x, y, z):
        # Bypass module canon: re-read via internal path then undo is hard;
        # instead build from _read_mca_states (canons) and re-apply MCA meta 11
        # only at the known plant cell for the raw baseline.
        cx, cz = x >> 4, z >> 4
        if (cx, cz) not in cache:
            cache[(cx, cz)] = snapshot_patch._read_mca_states(region, cx, cz)
        return int(cache[(cx, cz)][x & 15, y, z & 15])

    def volume(override=None):
        for z in range(az - 4, az + 5):
            for y in range(ay - 4, ay + 5):
                for x in range(ax - 4, ax + 5):
                    if override and (x, y, z) in override:
                        yield override[(x, y, z)]
                    else:
                        yield get_raw(x, y, z)

    # After _read_mca_states canon, the plant cell is already 175:10.
    assert get_raw(*DPLANT_CELL) == (175 << 4) | DPLANT_LIVE_META
    assert _fnv_volume(volume()) == JAVA_WFNV

    # Restoring the raw Anvil nibble reproduces the pre-fix magma digest.
    raw_override = {DPLANT_CELL: (175 << 4) | DPLANT_MCA_META}
    assert _fnv_volume(volume(raw_override)) == MCA_RAW_FNV


def test_world_gate_mismatch_records_anchor_and_both_digests():
    """collect_state_assertions surfaces java vs magma digests at the anchor."""
    import replay_tape

    ticks = [{
        "t": 0, "x": 122.7, "y": 70.0, "z": 255.3,
        "vx": 0.0, "vy": 0.0, "vz": 0.0, "og": 1, "hp": 20.0, "food": 20,
        "wfnv": JAVA_WFNV, "wfa": list(JAVA_WFA),
    }]
    c_rows = [{
        "tick": 0, "x": 122.7, "y": 70.0, "z": 255.3,
        "vx": 0.0, "vy": 0.0, "vz": 0.0, "on_ground": 1,
        "health": 20.0, "food": 20.0, "inventory": [], "entities": [],
        "nearby_hash": MCA_RAW_FNV, "nearby_anchor": list(JAVA_WFA),
    }]
    state = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    w = state["world"]
    assert w["mode"] == "java"
    assert w["compared"] == 1
    assert w["pass"] is False
    assert w["mismatches"][0] == {
        "tick": 0, "java": JAVA_WFNV, "magma": MCA_RAW_FNV,
        "anchor": list(JAVA_WFA),
    }

    # Same digests after canon domain -> pass.
    c_rows[0]["nearby_hash"] = JAVA_WFNV
    state2 = replay_tape.collect_state_assertions(ticks, c_rows, sample_every=1)
    assert state2["world"]["pass"] is True
    assert state2["world"]["mismatches"] == []
