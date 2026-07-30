# NAMES.md - what the names mean

Product name: **netherite**. One sentence per tree: java is the truth, blaze
is the simulation, magma is blaze plus eyes, verify is how we prove it.

| Name | What it is |
|------|------------|
| **java/** | The oracle: real Minecraft 1.11.2 (Forge client + decompiled reference source, private-only) plus the qrl recorder mod that captures tapes, and the closed render-opt kernel lab (`java/render-opt/`). Everything else in the repo is measured against what this tree outputs. |
| **blaze/** | The simulation. Reference CPU implementation and production CUDA implementation of the game tick (worldgen, physics, mobs, containers), single-source `MC_HD` headers so CPU == CUDA bitwise. `blaze/env/` is the GPU-resident batched RL env over that core; `blaze/rl/` is the trainers/eval. No renderer. |
| **magma/** | The playable fidelity tier: blaze's verified tick composed with a from-scratch software rasterizer (CPU and CUDA), HUD, audio-less client loop. This is what replays tapes frame-by-frame against Java goldens. |
| **verify/** | The cross-stack harness: tapes, goldens, scenarios, pixel gate, nightly sweep, mc_capture / ui_hud / ui_entities one-shot gates. Sits beside the trees because it judges all of them. |

Supporting vocabulary:

- **oracle** - the real Java game, treated as ground truth. Never patched to
  make C look better.
- **tape** - a recorded oracle session (inputs + state + golden frames) that
  magma replays deterministically.
- **golden** - a frame or dump captured from the oracle; the thing a gate
  diffs against.
- **gate** - a pass/fail check with frozen thresholds (pixel gate, physics
  first-divergence, unit batteries). rc 0 pass, rc 3 pixel fail, rc 4
  physics fail.
- **pin** - a tape in `scripts/regression_pins.txt` every merge must keep
  green.
- **qrl** - the recorder mod inside `java/` (main class `Recorder`, modid
  `qrl`; historical class name "QuantizedRL").

Name collision to know about: **blaze is also a vanilla mob** (nether
fortress ranged mob, blaze rods). Both meanings live in this codebase -
`verify/scenarios/portal_fortress_blaze.yaml` is about the mob; everything
under `blaze/` is the simulation. Context disambiguates; docs say "blaze
mob" when the mob is meant.

Historical names you may hit in DEVLOG/archive: **mc-sim** (the old name for
what is now blaze's core), `c/magma`, `c/mc-sim`, `c/render-opt` (pre-2026-07-30
layout, restructured into the trees above).
