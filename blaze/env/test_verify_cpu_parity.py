import os
import sys

import pytest

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import verify_cpu as verify


def make_record(*, implemented=0, measured=0, active=0, tick=0,
                digests=None, evidence=None, debug=None, source="test"):
    digest_values = [0] * len(verify.PARITY_NAMES)
    evidence_values = [0] * len(verify.PARITY_NAMES)
    debug_values = [0] * len(verify.PARITY_DEBUG_NAMES)
    for name, value in (digests or {}).items():
        digest_values[verify.PARITY_INDEX[name]] = value
    for name, value in (evidence or {}).items():
        evidence_values[verify.PARITY_INDEX[name]] = value
    for name, value in (debug or {}).items():
        debug_values[verify.PARITY_DEBUG_NAMES.index(name)] = value
    raw = verify.PARITY_STRUCT.pack(
        verify.PARITY_MAGIC, verify.PARITY_VERSION, verify.PARITY_SIZE,
        len(verify.PARITY_NAMES), implemented, measured, active, tick,
        *digest_values, *evidence_values, *debug_values)
    return verify.ParityRecord(raw, source)


def feature_mask(*names):
    return sum(1 << verify.PARITY_INDEX[name] for name in names)


def test_packed_parity_record_layout_and_named_decode():
    mask = feature_mask("player", "inventory")
    rec = make_record(
        implemented=mask, measured=mask, active=feature_mask("player"),
        tick=17, digests={"player": 0x1234, "inventory": 0x5678},
        evidence={"player": 2, "inventory": 1})

    assert verify.PARITY_SIZE == 592
    assert rec.tick == 17
    assert rec.digest[verify.PARITY_INDEX["player"]] == 0x1234
    assert rec.evidence[verify.PARITY_INDEX["inventory"]] == 1


def test_comparison_is_exact_but_only_for_requested_features():
    mask = feature_mask("player", "inventory")
    real = make_record(
        implemented=mask, measured=mask, tick=4,
        digests={"player": 11, "inventory": 22})
    blaze = make_record(
        implemented=mask, measured=mask, tick=4,
        digests={"player": 11, "inventory": 99})

    assert verify.parity_pair_status(real, blaze, ["player"])[0] \
        == verify.VERIFIED
    status, detail, subsystem = verify.parity_pair_status(
        real, blaze, ["inventory"])
    assert status == verify.FAILED
    assert detail == "subsystem inventory digest differs"
    assert subsystem == "inventory"

    blaze = make_record(
        implemented=mask, measured=mask, tick=4,
        digests={"player": 11, "inventory": 22},
        evidence={"player": 1})
    status, detail, subsystem = verify.parity_pair_status(
        real, blaze, ["player"])
    assert status == verify.FAILED
    assert detail == "subsystem player evidence differs"
    assert subsystem == "player"

    real = make_record(
        implemented=mask, measured=mask, active=feature_mask("player"),
        tick=4, digests={"player": 11})
    blaze = make_record(
        implemented=mask, measured=mask, tick=4,
        digests={"player": 11})
    status, detail, subsystem = verify.parity_pair_status(
        real, blaze, ["player"])
    assert status == verify.FAILED
    assert detail == "subsystem player active state differs"
    assert subsystem == "player"

    real = make_record(debug={"container": 3})
    blaze = make_record(debug={"container": 4})
    assert verify.parity_debug_differences(
        real, blaze, "containers") == [("container", 3, 4)]


def test_unsupported_or_unmeasured_feature_is_blocked():
    random_ticks = feature_mask("random_ticks")
    real = make_record(implemented=random_ticks, measured=0)
    blaze = make_record(implemented=0, measured=0)

    status, detail, _ = verify.parity_pair_status(
        real, blaze, ["random_ticks"])
    assert status == verify.BLOCKED
    assert "not measured: random_ticks" in detail




def test_missing_magma_parity_record_fails_clearly():
    read_fd, write_fd = os.pipe()
    os.close(write_fd)
    real = verify.RealEnv.__new__(verify.RealEnv)
    real.parity_fd = read_fd
    try:
        with pytest.raises(RuntimeError, match="closed before a complete PARY"):
            real._read_parity()
    finally:
        os.close(read_fd)


def test_missing_blaze_parity_symbol_fails_clearly(monkeypatch):
    class MissingParityLib:
        def __getattr__(self, name):
            raise AttributeError(f"undefined symbol: {name}")

    monkeypatch.setattr(verify.ctypes, "CDLL", lambda _path: MissingParityLib())
    with pytest.raises(RuntimeError, match="parity ABI unavailable.*missing symbol"):
        verify.Blaze1("unused.bsnp", port_parity=True)
