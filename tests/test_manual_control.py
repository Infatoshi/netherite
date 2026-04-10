"""Tests for the Python-side manual controller."""

# ruff: noqa: E402

import sys
from pathlib import Path

import numpy as np


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

from manual_control import ManualController, _format_status


def test_manual_controller_maps_pressed_keys_to_action():
    controller = ManualController(camera_speed=6.0)

    for key in ("w", "d", " ", "shift", "ctrl", "f", "e", "left", "down"):
        controller.on_key_press(key)

    action = controller.build_action()

    assert action["forward"] == 1
    assert action["back"] == 0
    assert action["left"] == 0
    assert action["right"] == 1
    assert action["jump"] == 1
    assert action["sneak"] == 1
    assert action["sprint"] == 1
    assert action["attack"] == 1
    assert action["use"] == 1
    np.testing.assert_array_equal(
        action["camera"],
        np.array([-6.0, 6.0], dtype=np.float32),
    )


def test_manual_controller_releases_keys_and_quit_stops_loop():
    controller = ManualController(camera_speed=3.0)
    controller.on_key_press("A")
    controller.on_key_press("I")

    before_release = controller.build_action()
    np.testing.assert_array_equal(
        before_release["camera"],
        np.array([0.0, -3.0], dtype=np.float32),
    )
    assert before_release["left"] == 1

    controller.on_key_release("a")
    controller.on_key_release("i")
    after_release = controller.build_action()

    assert after_release["left"] == 0
    np.testing.assert_array_equal(
        after_release["camera"],
        np.array([0.0, 0.0], dtype=np.float32),
    )

    controller.on_key_press("q")
    assert controller.running is False


def test_manual_controller_controls_text_mentions_core_bindings():
    text = ManualController().controls_text()

    assert "WASD" in text
    assert "Arrows/IJKL" in text
    assert "Q quit" in text


def test_format_status_reports_sim_rate_and_display_rate_separately():
    text = _format_status(
        seed=123,
        step=640,
        elapsed=2.0,
        display_frames=60,
        position=np.array([10.0, 64.0, -3.0], dtype=np.float64),
        pitch=45.0,
        controls_text="Q quit",
    )

    assert "seed 123" in text
    assert "sim 320 steps/s" in text
    assert "display 30.0 fps" in text
    assert "pitch 45" in text
