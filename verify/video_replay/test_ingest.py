import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import ingest


def test_probe_and_hash(tmp_path):
    video = tmp_path / "tiny.mp4"
    import subprocess
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-f", "lavfi",
        "-i", "color=c=blue:s=64x36:r=30:d=1", "-an", "-y", str(video),
    ], check=True)
    probe = ingest.probe_video(video)
    assert probe["width"] == 64
    assert probe["height"] == 36
    assert probe["fps_num"] / probe["fps_den"] == 30
    assert 0.9 <= probe["duration_s"] <= 1.1
    assert len(ingest.sha256(video)) == 64
