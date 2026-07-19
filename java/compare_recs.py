"""Compare two recordings of the same tape: bitwise on state/dumps, per-channel on pixels."""
import json
import sys
from pathlib import Path

import numpy as np
from PIL import Image

a, b = Path(sys.argv[1]), Path(sys.argv[2])

def keyed(d, pat):
    return sorted(d.glob(pat), key=lambda p: int("".join(c for c in p.stem if c.isdigit())))

# state: field-level compare; total_time is wall-clock server ticks (prep length varies,
# server free-runs between steps) and is excluded from the graded set by contract
state_diffs = []
for fa in keyed(a, "state_*.json"):
    fb = b / fa.name
    sa, sb = json.loads(fa.read_text()), json.loads(fb.read_text())
    for s in (sa, sb):
        if isinstance(s.get("time"), dict):
            s["time"].pop("total_time", None)
    bad = {k: (sa[k], sb.get(k)) for k in sa if sa[k] != sb.get(k)}
    if bad:
        state_diffs.append((fa.name, bad))
print(f"state: {len(list(a.glob('state_*.json')))} files, {len(state_diffs)} differ")
for name, bad in state_diffs[:5]:
    print(f"  {name}: {bad}")

# world dumps: bitwise after canonicalization - the check-decay bit (meta 0x8) on leaves
# (ids 18, 161) is transient tick-scheduler state, masked by contract
def canon(raw):
    v = np.frombuffer(raw, dtype=np.uint16, offset=40).copy()
    leaves = ((v >> 4) == 18) | ((v >> 4) == 161)
    v[leaves] &= ~np.uint16(0x8)  # meta bit 3 in the low nibble of (id<<4)|meta
    return raw[:40], v

dump_bad = []
for fa in keyed(a, "world_*.mcbd"):
    ha, va = canon(fa.read_bytes())
    hb, vb = canon((b / fa.name).read_bytes())
    if ha != hb or not np.array_equal(va, vb):
        dump_bad.append(fa.name)
print(f"mcbd: {len(list(a.glob('world_*.mcbd')))} files, {len(dump_bad)} differ {dump_bad[:5]}")

def pixcmp(files_a, root_b, label):
    worst = (0.0, None)
    total_mean = 0.0
    ndiff_files = 0
    for fa in files_a:
        fb = root_b / fa.relative_to(a)
        ia = np.asarray(Image.open(fa).convert("RGB"), dtype=np.int16)
        ib = np.asarray(Image.open(fb).convert("RGB"), dtype=np.int16)
        d = np.abs(ia - ib)
        m = float(d.mean())
        total_mean += m
        if d.max() > 0:
            ndiff_files += 1
        if m > worst[0]:
            worst = (m, fa.name)
    n = len(files_a)
    print(f"{label}: {n} frames, {ndiff_files} with any pixel diff, "
          f"mean/channel avg {total_mean/n:.4f}, worst {worst[0]:.4f} ({worst[1]})")

pixcmp(keyed(a, "frame_*.png"), b, "keyframe frames")
pixcmp(sorted((a / "video").glob("v_*.png")), b, "video frames")
