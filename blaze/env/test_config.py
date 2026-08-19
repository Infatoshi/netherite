from pathlib import Path

import pytest
from config import DEFAULT_PATH, BlazeConfig, load_config

from blaze import VecBlaze


def test_defaults_use_cpu():
    cfg = load_config()
    assert cfg.backend == "cpu"
    assert cfg.n_envs == 1
    assert cfg.warp_tick == 1


def test_default_file_names_every_setting():
    keys = {
        line.split("=", 1)[0].strip()
        for line in DEFAULT_PATH.read_text().splitlines()
        if "=" in line.split("#", 1)[0]
    }
    assert keys == set(BlazeConfig.__dataclass_fields__)


def test_backend_and_library_cannot_conflict():
    with pytest.raises(ValueError, match="different backends"):
        VecBlaze(backend="cpu", so_path="/tmp/blaze_cuda.so")


def test_file_and_override(tmp_path: Path):
    path = tmp_path / "blaze.conf"
    path.write_text("backend = metal\nn_envs = 8\nno_emit_all = 1\n")
    cfg = load_config(path, device=2)
    assert cfg.backend == "metal"
    assert cfg.n_envs == 8
    assert cfg.no_emit_all is True
    assert cfg.device == 2


@pytest.mark.parametrize("text", [
    "unknown = 1\n",
    "ktime = true\n",
    "backend = other\n",
    "n_envs = 0\n",
    "device = -1\n",
])
def test_bad_config_fails(tmp_path: Path, text: str):
    path = tmp_path / "bad.conf"
    path.write_text(text)
    with pytest.raises(ValueError):
        load_config(path)
