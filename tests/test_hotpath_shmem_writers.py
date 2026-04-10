from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def test_hotpath_shmem_writers_use_runtime_force_gate():
    shmem_buffer = (ROOT / "src/main/java/com/netherite/mod/ShmemBuffer.java").read_text()
    assert 'Boolean.getBoolean("netherite.force_shmem")' in shmem_buffer

    files = {
        "FrameGrabber.java": ROOT / "src/main/java/com/netherite/mod/FrameGrabber.java",
        "StateExporter.java": ROOT / "src/main/java/com/netherite/mod/StateExporter.java",
    }
    for name, path in files.items():
        text = path.read_text()
        assert "ShmemBuffer.forceIfEnabled" in text, f"missing force gate in {name}"
