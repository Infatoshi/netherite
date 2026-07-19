# GROK_REPORT: pixel-diff grind (grok/grind)

Branch: `grok/grind` (worktree `/home/infatoshi/dev/minecraft/wt-grind`)
Date: 2026-07-12
Tape: `20260712T055346Z_fast_s0_survival_default_rd8_77b5b462.jsonl`
Physics: **NO divergence over 3121 ticks** (1e-9) after every class
GPU: CUDA on GPU1 (RTX 3090)

## Classes completed (4 commits)

### 1. Entity ghost fidelity — `2321c33`
Squid model (ModelSquid body + 8 tentacles, `squid.png` in mob atlas),
hurt flash (hp drop -> hurtTime=10 red tint mix ~0.3), limbSwing from
per-tick position deltas (ModelQuadruped cos(ls*0.6662)*1.4*lsa).
Tape `ent_view` now carries entity `id` for continuity.

| Frame | Baseline | After | Delta |
|------:|---------:|------:|------:|
| t1540 (sheep hp drop 8->7) | 20.44 | 16.37 | **-4.07** |
| t1520 (flock) | 24.95 | 24.97 | +0.02 |
| t1620 | 16.25 | 15.92 | -0.33 |

Residual: sheep wool still over-white vs oracle face detail; limbSwing only
helps when ents actually move between keyframes; death roll not wired
(no deathTime in tape).

### 2. Water still animation — `6d263d2`
All 32 `water_still` frames; atlas tile advanced by
`total_time / frametime(2) % 32`. Live mutable atlas +
`cr_raster_cuda_atlas_dirty` for device re-upload.

Measured impact is small vs overcast/squid/terrain classes on this tape
(surface water was never the dominant cluster once beach solids were fixed).
Phase is deterministic from tape header `total_time=717` + tick.

### 3. Selection outline + dig crack default ON; entity UW fog — `ef4963d`
Headless/replay frames now draw vanilla black selection ribbons +
`destroy_stage_N` crack (was `CRASTER_OVERLAY=1` opt-in). Opt out:
`CRASTER_NO_OVERLAY=1`. Entities get water EXP fog when eye-in-fluid so
distant squid do not punch through as bright blobs.

| Frame | Baseline | After | Notes |
|------:|---------:|------:|------|
| t1000 submerged | 7.41 | 7.42 | restored after UW fog |
| t1020 submerged | 7.14 | 7.15 | restored |
| dig t2460-2900 | ~10-21 | +0.5..1.1 | crack/sel still slightly off vs oracle style |

Residual (#14): crack uses gray tint not true DST_COLOR/SRC_COLOR multiply;
selection ribbons approximate glLineWidth. Particles not ported.

### 4. Beach surface worldpatch — `edf4cc8`
601 solid-only cells at y>=63 from session save (grass/dirt/stone/gravel
where pristine worldgen has sand etc.). Family of OPEN #8 populate-order
provenance. Tree patch + inventory re-anchors preserved.

| Frame | Baseline | After | Delta |
|------:|---------:|------:|------:|
| t840 | 28.31 | 13.52 | **-14.79** |
| t860 | 33.24 | 10.70 | **-22.54** |
| t880 | 30.02 | 10.72 | **-19.30** |
| t820 | 18.55 | 13.52 | **-5.03** |
| t900 | 13.35 | 8.84 | **-4.51** |

## Aggregate (non-rain ticks only)

| Metric | Baseline | After |
|--------|---------:|------:|
| mean whole/ch | 12.53 | **12.02** |
| median whole/ch | 10.90 | **10.88** |
| frames <15/ch | 109 | **112** |
| regressions >0.5/ch | — | 9 (max +2.19 at t940; mostly HUD/overlay) |

Rain window t1800-2100 **BLOCKED** (OPEN #12): not counted, not faked.

## Residual ranked list (non-rain, whole mean)

| Rank | Frame | Tick | Mean/ch | Hypothesis |
|-----:|------:|-----:|--------:|------------|
| 1 | f055 | 1100 | 31.5 | Overcast/sky gray vs craster blue (weather not taped; #12 bleed) + water surface + distant squid |
| 2 | f068 | 1360 | 29.2 | Terrain silhouette (sky vs foliage) + HUD floor |
| 3 | f059 | 1180 | 28.3 | Open-water overcast + surface swimming |
| 4 | f057 | 1140 | 28.1 | Same overcast/surface class |
| 5 | f056 | 1120 | 27.6 | Same |
| 6 | f076 | 1520 | 25.0 | Sheep model texture/pose (wool over-white; head/face detail) |
| 7 | f089 | 1780 | 24.7 | Pre-rain sky/terrain edge (#12 onset) |
| 8 | f049 | 980 | 21.9 | Surface swim; residual overcast + water pattern |
| 9 | f143 | 2860 | 22.2 | Dig crack style + shovel arm + selection fidelity |
| 10 | f133 | 2660 | 17.5 | Dig tunnel lighting/crack |

HUD floor (~2-4/ch): this tape's oracle goldens have no GUI; craster draws HUD.
Do not remove HUD; judge non-HUD crop (y < ~400).

## Stop criterion

Not met for all non-rain non-HUD under ~4/ch. Six class commits of work done
(entity/hurt/limb, squid, water anim, overlay, entity UW fog, beach patch).
Dominant remaining: **unrecorded weather (#12)** on ocean frames, then sheep
texture detail and dig particle/blend fidelity.

## Commands used

```bash
nvidia-smi
cd c/craster && rm -f assets/*.o game/*.o core/*.o && make game -j && make game-cuda -j
make test-game && make test-raster-parity
cd raster/verify/trace
uv run --no-project --with numpy --with pillow python replay_tape.py \
  ../tapes/20260712T055346Z_fast_s0_survival_default_rd8_77b5b462.jsonl --report
uv run --no-project --with numpy --with pillow python grind.py
```

## Round 2 (2026-07-12)

Physics: **NO divergence over 3121 ticks** after every class. GPU1.

### Classes completed (4 commits)

#### 1. Dig crack DST_COLOR/SRC_COLOR — `fe02fb8` + `eebfaa6` + frame_capture
`CrShadeCtx.blend=2` = vanilla `blendFunc(DST_COLOR, SRC_COLOR)` → `out=2*src*dst`
in CPU + CUDA (parity bit-exact). Crack draws white destroy_stage with cutout
alpha (bg a≈1 discarded); selection stays blend=1. Hit-face only when dig
target matches raycast (avoids multiply-darkening hidden faces).

| Frame | Baseline | After | Delta |
|------:|---------:|------:|------:|
| t2860 dig | 22.24 | 21.60 | **-0.64** |
| t2660 dig | 17.53 | 17.53 | 0 |
| t2460 dig | 11.36 | 11.36 | 0 |

Residual: block break particles still missing; dominate dig look vs oracle.

#### 2. fogColor1 on sky/terrain fog — `6c85446` + frame_capture
**Root cause of f055-f061 was NOT rain/overcast (#12).** Evidence:
- Vanilla rainingStrength ramps 0.01/tick; full rain at t1800 ⇒ onset ~t1700.
  t1100-1220 is clear.
- Oracle sky at t0/t1620 is full blue and matches craster (~1/ch both_sky).
- After underwater (t1000-1080), fogColor1 (light-at-feet smoother) is low;
  updateFogColor multiplies the fog *target* by f13. Low-elevation sky on
  surface-swim frames then darkens via the sky-plane fog mix.
- Craster previously left sky fog unscaled → pure bright blue (132,175,255)
  vs oracle (120,155,233). After fix, both_sky t1140 **25.6 → 3.1/ch**.

| Frame | Baseline | After | Delta |
|------:|---------:|------:|------:|
| t980 surface | 21.91 | 8.07 | **-13.84** |
| t1100 | 31.50 | 18.00 | **-13.50** |
| t1120 | 27.61 | 13.41 | **-14.20** |
| t1140 | 28.12 | 15.83 | **-12.29** |
| t1160 | 27.27 | 18.11 | **-9.16** |
| t1180 | 28.28 | 19.07 | **-9.21** |
| t1200 | 27.37 | 19.32 | **-8.05** |
| t1220 | 26.43 | 18.61 | **-7.82** |

#### 3. Entity lightmap — `a1c8c96`
Entity pass samples the frame lightmap (sky=15, blk=0; face shade on ao).
Outdoor sheep wool no longer fullbright white scalar. t1520 24.97→24.81.

Residual: head pitch while grazing not in tape/model; fur-over-face
silhouette still soft; sheared/dye not recorded.

### Aggregate (non-rain)

| Metric | Round1 end | Round2 |
|--------|---------:|------:|
| mean whole/ch | 12.02 | **11.37** |
| median whole/ch | 10.88 | **10.88** |
| regressions >0.5 vs R1 | — | **0** |

Rain window t1800-2100 still BLOCKED (#12).

### Residual ranked (non-rain)

| Rank | Frame | Tick | Mean/ch | Hypothesis |
|-----:|------:|-----:|--------:|------------|
| 1 | f068 | 1360 | 29.2 | Terrain silhouette: oracle sky holes vs craster solid foliage (leaves/worldpatch) |
| 2 | f076 | 1520 | 24.8 | Sheep head pitch + face/leg bare skin vs fur |
| 3 | f089 | 1780 | 24.7 | Pre-rain / terrain edge (#12 onset) |
| 4 | f040 | 800 | 24.2 | Terrain/sky silhouette at low pitch |
| 5 | f143 | 2860 | 21.6 | Dig particles + hand; crack style fixed |
| 6 | f060 | 1200 | 19.3 | Ocean residual horizon/terrain |
| 7 | f133 | 2660 | 17.5 | Dig tunnel lighting |

### Stop criterion
Not fully met (~4/ch non-HUD non-rain). Four class commits this round;
ocean class closed as fogColor1 (not #12). Dominant remaining: f068 foliage
silhouette, sheep pose/face, dig particles, #12 rain window.

## Round 3 (2026-07-12)

Physics: **NO divergence over 3121 ticks** after every class. GPU1 only.

### Localization (no hypothesis without triptych + numbers)

| Frame | Tick | Mean | Finding |
|------:|-----:|------:|---------|
| f040 | 800 | 24.19 | oracle_only_sky 15k (dirt wall vs sky notch); both_sky R/G +13..15 (haze). Solid-only save patch path-safe: 0 px change |
| f088 | 1760 | 23.51 | **NOT rain**: both_sky |d|~2/ch full blue; sky bright 203 vs t1820 rain 142. Tree worldpatch already covers big oak; silhouette residual |
| f068 | 1360 | 29.18 | **NOT solid provenance**: 0 solid/leaf diffs vs qrl_0 at y>=63 in view box. fancy=false opaque leaves. OPEN #18 updated |
| f076 | 1520 | 24.81->**19.19** | Sheep face covered by idle head pose + fur order |

Weather gate (oracle sky mean brightness): t1620=189, t1760=203, t1780=200,
t1800=183, t1820=142. Pre-rain frames stay clear; #12 window only t1800-2100.

### Classes completed (1 commit)

#### 1. Sheep mid-graze head + skin-last face order — `588e0a4`
Idle sheep (limbSwingAmount < 0.08) use EntitySheep mid-graze constants
(sheepTimer=20: head ry=15, ax=PI/5+sine). Fur body before skin head so face
snout wins depth vs expanded fur head. Tape has no sheepTimer.

| Frame | Baseline | After | Delta |
|------:|---------:|------:|------:|
| t1520 flock | 24.81 | **19.19** | **-5.62** |
| t1540 hurt | 16.41 | 16.33 | -0.08 |

Face white-vs-dark residual px: 8387 -> 2106. Other non-rain frames byte-stable
at keyframe means (0 regressions >0.5).

### Attempted, not shipped
- Solid-only worldpatch extension (f040 air-clears / place-solids, y>=55-61):
  physics broke when corridor-wide; path-safe subset **0** pixel gain.
- +29 tree leaf cells beyond #8 box: physics clean, **0** pixel gain (canopy
  already re-anchored).

### Aggregate (non-rain)

| Metric | Round2 | Round3 |
|--------|---------:|------:|
| mean whole/ch | 11.37 | **11.33** |
| median whole/ch | 10.88 | **10.88** |
| regressions >0.5 vs R2 | — | **0** |

### Residual ranked (non-rain)

| Rank | Frame | Tick | Mean/ch | Notes |
|-----:|------:|-----:|--------:|-------|
| 1 | f068 | 1360 | 29.2 | #18 silhouette (not solid/leaf save diff) |
| 2 | f089 | 1780 | 24.7 | Terrain edge (clear sky; not #12) |
| 3 | f040 | 800 | 24.2 | #19 sky notch + mild sky haze |
| 4 | f088 | 1760 | 23.5 | #19 silhouette (clear sky) |
| 5 | f143 | 2860 | 21.6 | Dig particles still missing (#14) |
| 6 | f076 | 1520 | **19.2** | Sheep residual after graze pose |
| 7 | f060 | 1200 | 19.3 | Ocean horizon residual |

### Stop criterion
Partial: one class landed; non-HUD non-rain still >>4/ch. Remaining tops are
silhouette / dig-particles / #12 weather-tape / HUD floor - not further
solid-only worldpatch without recstart snapshot or weather-taped recording.

## Round 4 (2026-07-12): GUI-screen replay wiring (OPEN #9)

Physics: **NO divergence over 3121 ticks**. Pixel means on the canonical tape
**identical to Round 3 to 2 decimals** (non-rain mean **11.33**, keyframes
t840/860/880/1000/1520/2280/2300/2860 exact). Expected: tape has **no** `gui`
fields (predates recorder), so `gui_view` never fires.

### Class: wire gui_view chain (recorder half already on master)

Java recorder (prior commit `326c064`) already emits per open-screen tick:
`"gui":"<GuiScreen simple name>"`, `"gmx"`/`"gmy"` (ScaledResolution mouse).

Replay chain (this commit), mirroring `ent_view`:

1. `replay_tape.py` — rows with `"gui"` emit
   `{tick, type:"gui_view", gui, mx, my}` (mx/my from gmx/gmy; default center
   213,120 for 427x240 gui space).
2. `script.c` — parse `gui_view`; `gm_screen_kind_for_gui` maps
   GuiInventory→0 / GuiCrafting→1 / GuiFurnace→2; GuiIngameMenu/GuiChat/other
   logged once + skipped. Per-tick clear like `ent_views`.
3. `runtime` — render-only `gui_view_*` fields (does **not** mutate
   `r->container`, so physics/close-distance stay clean).
4. `frame_capture.c` — after HUD, if active: convert gmx/gmy via
   `gm_screen_mouse_to_fb` (scale = fb_h/240), temporarily set container for
   `gm_screen_draw`, restore. Forces sync hand/hud path when GUI open so
   inventory matches this tick (no deferred readback).

### Verification

| Check | Result |
|-------|--------|
| `test_screen` mapping + mouse scale | PASS |
| `test_script` GuiCrafting headless PPM | PASS (panel_px gray 139; PNG `c/craster/out_gui_view_crafting.png`) |
| `make test-game` | PASS |
| `make test-raster-parity` | PASS (all layers CPU==CUDA) |
| Canonical tape physics | NO divergence 3121 ticks |
| Canonical tape pixels vs R3 | means match to 2 decimals (0 regressions) |
| Script from tape | 0 `gui_view` events |

Remaining under #9: GUI **clicks** still unrecorded; inventory truth still
via worldpatch `set_inventory`. A future tape with `gui` fields will paint
panels automatically; craft results still need click stream or inventory
snapshots.

