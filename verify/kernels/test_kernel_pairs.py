"""pytest wiring: the kernel-pair manifest check runs in the default suite on
any machine (pure file hashing, no GPU needed), so a one-sided kernel edit
fails CI-style everywhere, not just when someone remembers the gate script."""

import subprocess
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]


def test_kernel_pairs_match_manifest():
    r = subprocess.run(
        [sys.executable, str(REPO / "verify/kernels/kernel_pairs.py")],
        capture_output=True,
        text=True,
    )
    assert r.returncode == 0, f"kernel pair drift:\n{r.stdout}\n{r.stderr}"
