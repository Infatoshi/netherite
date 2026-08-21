#!/usr/bin/env python3
"""Unit tests for chain-stage remainder padding and emit-status mapping."""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import make_snapshots as ms


def test_remainder_actions_pads_to_min_ticks():
    played = [{"forward": 1}, {"jump": 1}, {"attack": 1}]
    rest, n_chain = ms.remainder_actions(played, 2, min_ticks=5)
    assert n_chain == 1
    assert rest[0] == {"attack": 1}
    assert rest[1:] == [{"cam": 0}] * 4
    assert len(rest) == 5


def test_remainder_actions_keeps_long_tail():
    played = [{"forward": i} for i in range(10)]
    rest, n_chain = ms.remainder_actions(played, 3, min_ticks=4)
    assert n_chain == 7
    assert len(rest) == 7
    assert rest[0] == {"forward": 3}
    assert rest[-1] == {"forward": 9}


def test_remainder_actions_strips_snapshot_keys():
    played = [{"forward": 1, "snapshot": "x.bsnp", "snapshot_r": 64},
              {"cam": 0}]
    rest, n_chain = ms.remainder_actions(played, 0, min_ticks=2)
    assert n_chain == 2
    assert "snapshot" not in rest[0]
    assert rest[0]["forward"] == 1


def test_remainder_actions_empty_played_pads_idle():
    rest, n_chain = ms.remainder_actions([], 0, min_ticks=600)
    assert n_chain == 0
    assert len(rest) == 600
    assert rest[0] == {"cam": 0}


def test_stage_meta_roundtrip(tmp_path):
    path = str(tmp_path / "s10_stg1.bsnp")
    with open(path, "wb") as f:
        f.write(b"BSNP" + (b"\x00" * 20))
    ms.write_stage_meta(path, 10, 1, 424, 431, 2058)
    meta = ms.load_stage_meta(path)
    assert meta["first_action_idx"] == 424
    assert meta["dump_tick"] == 431
    assert meta["remainder_start"] == 425
    assert meta["n_quiesce"] == ms.QUIESCE
