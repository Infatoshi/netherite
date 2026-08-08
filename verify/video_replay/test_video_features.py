import importlib.util
import pathlib

import numpy as np


HERE = pathlib.Path(__file__).resolve().parent
SPEC = importlib.util.spec_from_file_location(
    "video_features", HERE / "video_features.py")
features = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(features)


def test_phase_shift_recovers_horizontal_motion():
    frames = np.zeros((3, features.HEIGHT, features.WIDTH), dtype=np.uint8)
    frames[:, 8:27, 12:30] = 180
    frames[1] = np.roll(frames[0], 3, axis=1)
    frames[2] = np.roll(frames[1], -1, axis=0)
    edge, mask = features.structural_edges(frames)
    shifts = features.phase_shifts(frames, mask)
    assert tuple(shifts[1]) == (0, -3)
    assert tuple(shifts[2]) == (1, 0)


def test_structural_mask_excludes_hud_band():
    frames = np.zeros((1, features.HEIGHT, features.WIDTH), dtype=np.uint8)
    frames[:, 5:20, 8:35] = 200
    edge, mask = features.structural_edges(frames)
    assert edge.sum() > 0
    assert not mask[0, -1].any()
    assert mask[0, 10, 0]


def test_interaction_hint_finds_repeated_hand_motion_not_world_pan():
    frames = np.full((15, features.HEIGHT, features.WIDTH), 80,
                     dtype=np.uint8)
    frames[4:11:2, 20:34, 47:61] = 220
    third = np.zeros(15, dtype=np.uint8)
    hints = features.interaction_hints(frames, third)
    assert hints[7]
    third[7] = 1
    hints = features.interaction_hints(frames, third)
    assert not hints[7]


def test_gui_hint_requires_persistent_gray_panel_geometry():
    rgb = np.zeros((8, features.HEIGHT, features.WIDTH, 3), dtype=np.uint8)
    rgb[:] = (20, 80, 20)
    rgb[2:6, 10:22, 42:50] = 120
    gray = np.rint(rgb[..., 0] * .299 + rgb[..., 1] * .587 +
                   rgb[..., 2] * .114).astype(np.uint8)
    hint = features.gui_hints(rgb, gray)
    assert hint[2:6].all()
    assert not hint[:2].any() and not hint[6:].any()
