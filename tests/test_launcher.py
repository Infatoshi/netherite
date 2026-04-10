"""Tests for Minecraft launcher command construction."""

# ruff: noqa: E402

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "env"))

from config import NetheriteConfig
from launcher import MCInstance


def test_mcinstance_launch_command_passes_vanilla_window_size_args():
    cfg = NetheriteConfig(width=160, height=90, seed=4242)
    inst = MCInstance(cfg, ROOT)

    cmd = inst._build_launch_command()

    assert cmd[0] == str(ROOT / "gradlew")
    assert cmd[1] == "runClient"
    assert "-Dnetherite.width=160" in cmd
    assert "-Dnetherite.height=90" in cmd
    assert "--args=--width 160 --height 90 --username netherite_0" in cmd
