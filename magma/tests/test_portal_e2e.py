#!/usr/bin/env python3
"""Portal E2E gates: light nether portal, nether 360 look, end portal + look.

Requires live Minecraft + qrl on 127.0.0.1:25575 (start_vnc_client.sh).

  PORTAL_E2E_OUT=/tmp/portal_e2e \\
  uv run --no-project --with pillow --with numpy --with pytest \\
    pytest magma/tests/test_portal_e2e.py -v

If PORTAL_E2E_REUSE=1 and /tmp/portal_e2e/results.json exists, reuse the hashed
Java capture while rebuilding, rerendering, and rescoring the current C candidate.
"""
from __future__ import annotations

import json
import os
import socket
import subprocess
import sys
import uuid
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parents[2]
OUT = Path(os.environ.get("PORTAL_E2E_OUT", "/tmp/portal_e2e"))
RUNNER = Path(__file__).resolve().parent / "portal_e2e_run.py"


def _qrl_up() -> bool:
    try:
        s = socket.create_connection(("127.0.0.1", 25575), timeout=2)
        s.close()
        return True
    except OSError:
        return False


@pytest.fixture(scope="module")
def results():
    reuse = os.environ.get("PORTAL_E2E_REUSE", "0") not in ("", "0", "false")
    res_path = OUT / "results.json"
    run_id = str(uuid.uuid4())
    if not reuse and not _qrl_up():
        if os.environ.get("PORTAL_E2E_ALLOW_SKIP", "0") not in ("", "0", "false"):
            pytest.skip("qrl not listening on 25575; explicit skip enabled")
        pytest.fail("qrl not listening on 25575; start java/start_vnc_client.sh")
    if reuse and not res_path.is_file():
        pytest.fail(f"PORTAL_E2E_REUSE=1 but {res_path} is missing")
    if not reuse:
        if OUT.exists() and any(OUT.iterdir()):
            pytest.fail(f"fresh capture requires a unique empty output directory: {OUT}")
        prior_capture_id = None
    else:
        prior_capture_id = json.loads(res_path.read_text()).get("capture_run_id")
    args = [sys.executable, str(RUNNER)]
    if reuse:
        args.append("--rescore")
    r = subprocess.run(
        args,
        cwd=str(REPO),
        env={**os.environ, "PORTAL_E2E_OUT": str(OUT), "PORTAL_E2E_RUN_ID": run_id},
        capture_output=True, text=True, timeout=900,
    )
    if not res_path.is_file():
        pytest.fail(f"portal e2e produced no results.json\n{r.stdout[-2000:]}\n{r.stderr[-2000:]}")
    data = json.loads(res_path.read_text())
    if r.returncode != 0:
        pytest.fail(
            f"portal e2e exit={r.returncode} status="
            f"{data.get('capture_status')}/{data.get('score_status')} "
            f"error={data.get('capture_error') or data.get('score_error')}\n"
            f"{r.stdout[-4000:]}\n{r.stderr[-4000:]}"
        )
    assert data.get("score_run_id") == run_id, "runner reused a stale score result"
    if reuse:
        assert prior_capture_id and data.get("capture_run_id") == prior_capture_id
    else:
        assert data.get("capture_run_id") == run_id
    assert data.get("capture_manifest"), "live inputs lack a capture hash manifest"
    assert data.get("capture_status") == "complete", data
    assert data.get("score_status") == "complete", data
    return data


def test_nether_portal_lights_with_fire(results):
    s = results["steps"]["portal_light"]
    assert s["pass"], s
    assert s["portal_blocks"] >= 6


def test_enter_nether(results):
    s = results["steps"]["portal_enter"]
    assert s["pass"] and s["dim"] == -1, s
    assert not s.get("forced"), f"nether entry must use BlockPortal: {s}"
    assert s.get("method") in {
        "automatic_block_collision", "nether_collision_handler",
    }, s
    assert s.get("assisted") is (s.get("method") == "nether_collision_handler"), s
    if s.get("assisted"):
        evidence = s.get("evidence", {})
        assert evidence.get("ok") is True and evidence.get("portal_touch") is True
        assert evidence.get("kind") == "nether_collision_handler"
        assert evidence.get("intersects") is True and evidence.get("pre_dim") == 0


def test_nether_lookaround_eight_views(results):
    s = results["steps"]["nether_look"]
    assert s["pass"] and s["n_views"] == 8
    for i in range(8):
        p = OUT / f"oracle_nether_look_{i:02d}.png"
        assert p.is_file() and p.stat().st_size > 1000, p


def test_nether_scene_has_no_nonplayer_entities(results):
    s = results["steps"]["nether_scene"]
    assert s["pass"] and s["remaining_nonplayers"] == 0, s


def test_nether_pixel_vs_c_mesh(results):
    p = results["pixel"]["nether"]
    t = p["thresholds"]
    assert p["n_views"] == 8
    assert p["max_mae"] <= t["max_per_view_mae"], p
    informative = [v for v in p["per_view"]
                   if v["world"]["oracle_edge_energy"]
                   >= t["min_oracle_edge_energy_for_corr"]]
    assert len(informative) >= t["min_informative_views"], informative
    assert min(v["world"]["coarse_gcorr"] for v in informative) \
        >= t["min_per_view_coarse_gcorr"], p
    assert max(v["visible_nonfog_silhouette"]["symmetric_difference_fraction"]
               for v in p["per_view"]) \
        <= t["max_visible_nonfog_symmetric_difference_fraction"], p
    assert max(v["hot_emitter"]["false_positive_pixels"]
               for v in p["per_view"]) \
        <= t["max_hot_false_positive_pixels"], p
    assert p["coverage_pass"] and p["texture_animations_pinned"], p
    backgrounds = [v["background"]["mae"] for v in p["per_view"] if v["background"]]
    assert max(backgrounds) <= t["max_background_mae"], p
    assert all(v["fallback_nonair"] == 0 for v in p["per_view"])
    assert all(v["unsupported_nonzero_meta"] == 0 for v in p["per_view"])
    assert all(v["supported_nonzero_meta"] == p["source_nonzero_meta"]
               for v in p["per_view"])
    anchors = [a for v in p["per_view"] for a in v.get("fire_anchors", [])]
    assert len(anchors) == 4
    assert all(a["roi_pixels"] > 0 and a["oracle_pixels"] > 0
               and a["candidate_pixels"] > 0 for a in anchors), anchors
    assert min(a["iou"] for a in anchors) >= t["min_fire_anchor_iou"], anchors
    assert min(a["recall"] for a in anchors) >= t["min_fire_anchor_recall"], anchors
    assert min(a["precision"] for a in anchors) >= t["min_fire_anchor_precision"], anchors
    assert all(a["no_fire_ablation"]["iou"] < t["min_fire_anchor_iou"]
               for a in anchors), anchors


def test_nether_flat_clear_ablation_fails(results):
    p = results["pixel"]["nether"]
    t = p["thresholds"]
    threshold = t["min_per_view_coarse_gcorr"]
    views = [v for v in p["per_view"]
             if v["world"]["oracle_edge_energy"]
             >= t["min_oracle_edge_energy_for_corr"]]
    assert len(views) >= t["min_informative_views"]
    assert min(v["world"]["coarse_gcorr"] for v in views) >= threshold
    assert all(v["flat_clear_ablation"]["coarse_gcorr"] < threshold
               for v in views)


def test_end_portal_frames_and_interior(results):
    s = results["steps"]["end_portal_build"]
    assert s["pass"], s
    assert s["frames"] == 12
    assert s["portal_blocks"] == 9
    assert s["portal_center_exact"] is True
    assert s["eye_meta"] == sorted([4] * 3 + [5] * 3 + [6] * 3 + [7] * 3)
    activation = s["activation"]
    assert activation["result"] == "SUCCESS", activation
    assert activation["before_eye"] is False and activation["after_eye"] is True, activation


def test_enter_end(results):
    s = results["steps"]["end_enter"]
    assert s["pass"] and s["dim"] == 1, s
    assert not s.get("forced"), f"end enter must be natural portal_touch path: {s}"
    assert s.get("method") in {
        "automatic_block_collision", "end_collision_handler",
    }, f"end entry must exercise BlockEndPortal collision: {s}"
    assert s.get("assisted") is (s.get("method") == "end_collision_handler"), s
    if s.get("assisted"):
        evidence = s.get("evidence", {})
        assert evidence.get("ok") is True and evidence.get("portal_touch") is True
        assert evidence.get("kind") == "end_collision_handler"
        assert evidence.get("intersects") is True and evidence.get("pre_dim") == 0


def test_end_scene_has_no_nonplayer_entities(results):
    s = results["steps"]["end_scene"]
    assert s["pass"] and s["remaining_nonplayers"] == 0, s


def test_end_lookaround_eight_views(results):
    s = results["steps"]["end_look"]
    assert s["pass"] and s["n_views"] == 8
    for i in range(8):
        p = OUT / f"oracle_end_look_{i:02d}.png"
        assert p.is_file() and p.stat().st_size > 500, p


def test_end_pixel_vs_c_mesh(results):
    p = results["pixel"]["end"]
    t = p["thresholds"]
    assert p["n_views"] == 8
    assert p["max_mae"] <= t["max_per_view_mae"], p
    assert p["mean_gcorr"] >= t["min_mean_gcorr"], p
    assert p["coverage_pass"] and p["texture_animations_pinned"], p
    nonbackground_views = [
        v for v in p["per_view"] if v["oracle_nonbackground_fraction"] >= 0.01
    ]
    assert len(nonbackground_views) >= t["min_nonbackground_views"]
    assert max(v["oracle_nonbackground"]["mae"] for v in nonbackground_views) \
        <= t["max_per_view_nonbackground_mae"], nonbackground_views
    assert min(v["nonbackground_silhouette"]["iou"]
               for v in nonbackground_views) \
        >= t["min_per_view_nonbackground_silhouette_iou"], nonbackground_views
    assert min(v["oracle_nonbackground"]["edge_energy_ratio"]
               for v in nonbackground_views) \
        >= t["min_per_view_nonbackground_edge_ratio"], nonbackground_views
    sky = p["sky_anchor"]
    assert sky["terrain_pixels"] == 0, sky
    assert sky["mae"] <= t["max_sky_anchor_mae"], sky
    assert sky["exact_fraction"] >= t["min_sky_anchor_exact_fraction"], sky


def test_end_flat_sky_ablation_fails(results):
    p = results["pixel"]["end"]
    t = p["thresholds"]
    flat = p["sky_anchor"]["flat_clear_ablation"]
    assert flat["mae"] > t["max_sky_anchor_mae"], flat
    assert flat["exact_fraction"] < t["min_sky_anchor_exact_fraction"], flat


def test_end_missing_nonbackground_ablation_fails(results):
    p = results["pixel"]["end"]
    t = p["thresholds"]
    views = [
        v for v in p["per_view"] if v["oracle_nonbackground_fraction"] >= 0.01
    ]
    assert views
    assert all(v["missing_nonbackground_silhouette"]["iou"]
               < t["min_per_view_nonbackground_silhouette_iou"] for v in views)
    assert all(v["missing_nonbackground_ablation"]["mae"]
               > t["max_per_view_nonbackground_mae"] for v in views)


def test_overall_ok(results):
    assert results.get("ok") is True, results


if __name__ == "__main__":
    raise SystemExit(pytest.main([__file__, "-v", "--tb=short"]))
