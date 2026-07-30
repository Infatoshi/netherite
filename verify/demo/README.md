# Pixel-fidelity demo pack

Canonical human tape (seed 0, 3121 ticks, 157 oracle frames @ 854x480) used to
prove magma matches the real Java client: physics 1e-9, pixel gate PASS,
side-by-side MP4.

**Source of truth:** recorded from live Forge+qrl on anvil (2026-07-12).
Shipped so a cold box can produce the demo without re-recording.

```bash
# after bootstrap + build:
bash scripts/demo_pixel_sbs.sh
# -> demos/pixel_match_sbs.mp4 + demos/pixel_match_report.md
```

See `magma/VERIFY.md` for the full flywheel. Regenerating this pack needs a
live client + `tape.py start/stop` (not required for the shipped demo).
