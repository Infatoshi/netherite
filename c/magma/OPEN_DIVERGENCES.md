# Open divergences

Active gaps only. Resolved work is recorded in git history and
`docs/DEVLOG.md`; it does not belong in this file.

Last verified on `d7f2998`, which includes the reviewed capture, preview, and
exact-hand-gate commits (`44a7de3`, `27ec2fc`, `d198a96`).

Pixel-perfect means every owned, A/B-stable pixel is equal. Mean-error budgets,
hard-pixel floors, empty target captures, and unstable Oracle pairs are not
passes.

## Interactive C raster renderer

### First-person hand use poses

All three states have exact Java A/B captures but remain strict C residuals.
Ownership is the union of the Java and C subjects at threshold zero.

- Bow pull: `hard_px=30260`, `maxch=125`.
- Eat mid-use: `hard_px=101880`, `maxch=215`.
- Blocking shield: the old idle-tip golden was replaced by a genuine sticky
  `MAIN_HAND/BLOCK` capture; C still has `hard_px=28506`, `maxch=100`.

The former hand mean budget is gone. Synthetic exact controls pass, while
missing Java silhouette, C-extra, +1-channel, shift, and recolor mutations all
fail.

Likely work: finish ItemRenderer/model registration, edge shading, and the
remaining bow/eat sticky full-use capture provenance.

Repro:

```bash
bash c/magma/raster/verify/ui_hud/run_ui_hud_gates.sh
```

### Inventory player preview

GUI chrome is bit-exact, but the rendered player remains open at max channel 1:

- Pose 1: mean `0.011641`, `442` nonzero pixels.
- Pose 2: mean `0.009949`, `323` nonzero pixels.

The current path matches RenderHelper/Mesa unorm8 light-material packing for the
dominant face bins. Smaller fixed-function primary-light bins remain.

Repro:

```bash
bash c/magma/raster/verify/mc_capture/run_gui_verify.sh
```

### Portal and underwater

- Portal is `CAPTURE_BLOCKED`: the accepted pair has max-channel-1 A/B
  instability. Atomic same-client-turn `frame_pair`, sticky portal time, and
  pinned texture animation did not make a new pair exact, so no worse golden
  was promoted. C-vs-J composition also retains a large outdoor-underlay
  residual.
- Underwater is `CAPTURE_BLOCKED`: A/B reaches max channel 3 and C-vs-J remains
  about `4.97/ch`. Eye-at-surface fog is also visibly too weak.

Neither surface may pass until the Oracle pair is exact and the C full-frame
residual is zero.

### Entity and particle pixels

No strict entity family is pixel-perfect yet:

- 11 states are `CAPTURE_BLOCKED` by nonzero Java A/B on complete family ROIs:
  slime/magma sizes and squish, dig stone/grass, and dragon fireball.
- Five stable states are honest `RESIDUAL`: dragon death at ticks 50/100/190,
  small fireball, and XP orb.
- XP now has a genuine visible, full-frame A/B-exact Oracle capture; C remains
  `hard_px=12000`.
- Small fireball no longer draws on-fire layers unless `isBurning`, but its
  complete ROI remains open.
- Dragon death uses the correct 48-bit `java.util.Random`; remaining gaps are
  body pose/UV/dissolve, fine ray orientation, and surrounding scene pixels.
- Death dissolve is still per-box rather than vanilla per-texel.

Repro:

```bash
bash c/magma/raster/verify/ui_entities/run_gates.sh
bash c/magma/raster/verify/ui_entities/run_oracle_gate.sh
```

### Full-frame soft surfaces

These have useful capture-integrity checks but no pixel-perfect product claim:

- Death-screen world/tint composition outside the exact chrome and paired tint
  model remains about `33.17/ch`.
- Fire overlay full-frame composition remains open.
- Rain/overcast rendering is not modeled for the canonical rain window.
- High-altitude and long-distance haze are weaker than Java.

### Canonical tape residual unexplained clusters

The dominant early-tape failure (all CUTOUT geometry discarded: tallgrass,
cross plants, grass_side_overlay) was a misaligned positional `CrShadeCtx`
initializer and is **fixed** - see DEVLOG 2026-07-25. What remains on
`20260721T215812Z_fast_s0_survival_default_rd8_77b5b462`:

- Pixel gate still FAIL, but 7 failed frames (was 58), worst t=260 with
  7291 unexplained px (was t=80 / 74783). UNEXPLAINED total 122_581 px over
  63 frames (was 1_540_406 over 67).
- t=260 and t=460 hold the two big residual clusters (3380 px and 2989 px at
  t=260, on the near canopy and a distant tree). Re-measured 2026-07-25 with
  `pxdiff.py`: t=260 is 103 clusters, the canopy one at y[83,178] x[526,611]
  is 2989 px, `texel-selection`, exact-match 0.55 / tol4 0.83 at shift (1,-1).
  It is NOT a cutout-coverage bug, and the oracle's own capture settings say
  why it cannot be: the tape ran `fancyGraphics=false, mipmapLevels=0`, where
  vanilla `BlockLeaves.getBlockLayer` returns SOLID, not CUTOUT_MIPPED, and
  magma already meshes leaves as `CR_LAYER_SOLID` with alpha forced opaque.
  Direct count of the canopy bbox: 118 of 3991 differing pixels (3.0%) are
  true sky-holes, the other 96% are both-leaf texel flips. An earlier pxdiff
  build called this `cutout-sky+` from the mean-delta direction alone; the
  discriminator now requires a measured hole fraction, because on a minified
  canopy sigma is 50-70 and a mean of +4 along the sky axis gives an
  alignment of 0.997 at 3% coverage error.
  The leaf INTERIOR, separately, is nearest-neighbour texel selection on
  minified faces, not shading or geometry: the per-channel delta there is
  zero-mean with a large spread (near canopy mean +0.1/+0.2/+0.2, sigma 19/28/8; distant tree
  -0.9/-1.1/-0.4, sigma 32/46/14), i.e. individual texels flip between
  neighbouring values rather than the surface being uniformly off. Ruled out:
  a global sub-pixel camera offset (best whole-frame alignment is dx=dy=0,
  6.23 mean/ch, vs 7.45 at dx=-1) and the fog distance mode - the oracle's own
  GL query records `fog_distance_mode_nv = 34139` (GL_EYE_RADIAL_NV), which is
  what `CrFragment.eye_dist` already implements, and forcing planar |z| fog
  makes the tape worse (particles 181k -> 436k px, viewmodel 256k -> 411k,
  failed frames 7 -> 10).
- t=3180/3200/3220 clusters soak from `viewmodel` (hand/item residual), a
  separate open item under "First-person hand use poses".
- Residual whole-frame mean at t=80 is 3.76/ch, all outdoor terrain: grass
  tint / AO / luminance (the `known:4` class), not geometry.

Repro:

```bash
uv run --no-project --with numpy --with scipy --with pillow --with nbt \
  python c/magma/raster/verify/trace/replay_tape.py \
  c/magma/raster/verify/tapes/20260721T215812Z_fast_s0_survival_default_rd8_77b5b462.jsonl \
  --cpu --report
```

### Scenario tape pixel-gate failures (triaged, unfixed)

Diagnosis only, from a delegated triage pass; the code claims below were
spot-checked but the pixel measurements are the triage agent's, not
independently re-measured here.

- `scenario_soulsand_ice_20260723T001810Z`: **closed** (2026-07-25). The 44
  mild-shift failures were the sky-plane fog: `orientCamera` puts the 64-tile
  sky plane at `16 - eyeHeight` and vanilla's fixed-function fog on it is
  per-vertex Gouraud, not per-pixel (`ac47c2b`). That left one frame, t=60, a
  77%-of-frame 5.37 shift on the step down onto soul sand: the `fogColor1`
  light smoother was sampling the post-tick feet, one tick ahead of vanilla's
  pre-movement `updateRenderer` (`5c4cf6e`). The tape is rc=0.
  Still open on this tape: ~1226 px of UNEXPLAINED at t=50, in 7 clusters of
  50-370 px (plus viewmodel-masked siblings), all mid-frame on the receding
  soul sand path with the grass either side clean. `pxdiff.py` on the
  texel-selection bands (e.g. y[320,331] x[407,444]): zero_shift 26.26/ch,
  best_shift dy=-1 at 1.52/ch; remeasured as C[y]≈G[y+1] at 0.4-1.5/ch
  across those rows. At yaw=-90 pitch=0 the screen-Y axis maps to texture U
  (world X / path depth), not V.
  **Sampling-rule dead end (2026-07-25, wt/texel):** magma already uses
  GL_NEAREST `floor(u*w)` (default is `floor(u*w - 1e-4)`; pure floor via
  MAGMA_SAMPLE_MODE=1 is bit-identical on this residual). Vanilla 1.11.2 with
  `mipmapLevels:0` binds the terrain atlas as plain GL_NEAREST (EntityRenderer
  only forces non-mip for CUTOUT; SOLID uses the upload filter, still nearest
  with no mips). Sweeps: round / UV+0.5 drop UNEXPLAINED to 165 but raise
  whole-frame mean 3.51→5.47 and shred HUD/viewmodel; U+0.2 yields unex 864
  mean 3.43 (partial, no principle); every V bias worsens both metrics. No
  sampling-operator change gets unex→0 without a nightly regression - do not
  fudge MAGMA_TEXEL_BIAS. Residual is UV phase at pixel centres (wrong side of
  texel boundaries on the oblique top faces), not floor-vs-round. Checked
  separately: `raster_cpu.c` already samples at `(px+0.5f, py+0.5f)`, so the
  other cheap phase suspect - rasterising at the pixel corner - is ruled out
  too. What is left to test is the quad's world position / perspective-correct
  UV precision on grazing top faces, which is a different investigation.
- `20260712T055346Z_fast_s0_survival_default_rd8_77b5b462`: the goldens have
  no HUD (Malmo forces `hideGUI` for the mission), magma used to draw one, and
  the gate's positional `hud` accept swallowed the whole mismatch - the bottom
  96 rows, 20 percent of the frame, had no pixel verification at all on this
  tape. `capture.hide_gui` + `MAGMA_HIDE_GUI` fixed that (`3dc2d19`) and the
  tape. `capture.hide_gui` + `MAGMA_HIDE_GUI` stops magma drawing it
  (`3dc2d19`), and the gate stops accepting those rows positionally when the
  tape sets that flag - `_positional_accept_masks(hide_gui=True)` returns an
  empty HUD barrier while carving the same strip out of `viewmodel`, so the
  region behind `CLASS_PIXEL_BUDGETS["viewmodel"]` does not silently grow by
  half. The legacy sidecar regions, all recorder gaps and all scene-global,
  were widened from y1=383 to y1=479 for the same reason: their old limit was
  an artifact of the mask, not of the gap.
  Failed frames go 14 -> 25. That is the price of measuring a fifth of the
  frame that was never measured before, on all 157 frames; the tape was
  already failing. The rain window t=1800..2100 now resolves cleanly into
  `known:12` where before it leaked (t=1800/1940/1960 were failing, now 0
  unexplained), and the new failures are at t=360..520 and t=1140..1220.
  **A wrong reading to not repeat:** the t=1800..1960 window first looked like
  a missing first-person held item - the oracle shows a dark held log and
  magma appeared to show none. It is not. Golden/candidate over that window is
  a uniform ~0.45 ratio across the whole frame including the sky (t=1900 whole
  0.726, sky 0.809, ground 0.696), i.e. the already-filed oracle rain
  darkening; magma's log is simply drawn at full brightness against grass and
  reads as absent at a glance. Zoom before concluding.
  The other canonical tape, `20260721T215812Z`, has a HUD on all 181 goldens
  and is unaffected; do not set `capture.hide_gui` on it.
- `scenario_elytra_dip_20260723T001355Z`: passes in flight (t=100 mild_abs
  1.19) and starts failing at landing (t>=140, ~6-7/ch). Near-field grass is
  rendered at the wrong spatial frequency - coarse block-scale flats where the
  oracle has fine noise. UNEXPLAINED clusters stay small (max 520, under
  FAIL_CLUSTER), so it fails via mild-shift. Likely the same texel-selection
  family as the canonical-tape leaf residual above.
- `scenario_ender_dragon_20260722T093713Z` (stale, superseded by `094040Z`):
  magma draws large extra bright geometry the oracle does not have (45216 px
  cluster at t=420, magma mean `[118,124,89]` where the oracle is
  `[34,45,30]`), so this is added content rather than a gate misclass. One
  contributing cause is confirmed in code: `gm_runtime_set_dimension`
  (`game/runtime.c:1022`) never calls `gm_dragon_init`, which only the portal
  path (`game/runtime.c:609`) does, so an authoritative tape dimension switch
  arrives in the End without the fight initialised. Prefer `094040Z` as the
  dragon gate tape.
  Do **not** "fix" this by calling `gm_dragon_init` from `set_dimension`:
  `replay_tape.py` already turns every recorded entity into a render-only
  `ent_view` ghost, and `frame_capture.c:712` fills live-dragon views before
  appending ghost views, so a live dragon would be drawn *on top of* the tape
  one. The symptom here is too much bright geometry, not too little, so the
  likelier cause is End island worldgen / snapshot coverage at x~100. The
  portal path additionally carves a platform and sets the pose, neither of
  which an authoritative tape transfer should do.


### CPU/CUDA replay parity: closed, keep sweeping

Parity had only ever been measured on one tape. The canonical
`20260721T215812Z` replay is bit identical CPU vs CUDA, but a full 23-tape
CUDA sweep on GPU0 (`sm_120`, `nightly_20260725T062525Z`) was **FAIL** with
baseline regressions on six tapes where the CPU sweep was PASS.

The terrain half of that is **fixed**: `cuda/raster_cuda.cu` built its MVP with
`cr_look_yaw_pitch_dev`, which is look-only, while the host path uses
`cr_camera_view` - so CUDA silently dropped
`EntityRenderer.hurtCameraEffect` (hurt roll/yaw). Every tick the player took
damage, the CPU rendered a rolled horizon and CUDA a flat one. Both MVP sites
now call `cr_camera_view_dev`.

On `scenario_blaze_bow_demo_20260722T104234Z` (407 frames, serial runs):
whole-tape diff 12_212_050 px before, 9_344_718 after the hurt fix, and the two
hurt bursts collapse (fi=43: 167_824 px -> 36; fi=231: 156_540 -> 23). With
`MAGMA_NO_DEFER=1` on top, 12_875 px total, 0 frames over 1000, max 46 - sky
stars only. The remainder is the deferred-frame-end issue above.

Ruled out along the way: GPU contention (serial re-runs reproduce
byte-for-byte); a chunk/mesh upload budget (`wl_ensure_mesh` is dirty-driven,
there is no per-frame budget); and the early player deaths, which happen
identically on the CPU and are a separate matter.

Re-run of the 23-tape CUDA sweep on GPU0 after the fix
(`nightly_20260725T071901Z`): 15 rc=0 / 8 rc=3, the same tally as the CPU
sweep, with baseline regressions on **two** tapes instead of five.
`scenario_ender_dragon_20260722T094040Z`,
`scenario_ender_dragon_demo_20260722T104500Z` and
`scenario_lava_walk_20260722T234940Z` are now byte-identical to their CPU
baselines on every class.

The two that still regressed were both the deferred frame end, and both are
now **fixed** - the DEFERRED path reproduces the CPU baseline byte-for-byte on
every class, `failed_frames` and the state block:

- `finish_pending` re-derived the fire overlay's fov scale as
  `cam.fov_deg / 70`, which folds in `getFovModifier`'s bow-pull / sprint
  term; the sync path passes `uw.fov_scale`. Divergence on exactly the
  fire+bow ticks (`blaze_bow_demo`: 57 failed frames -> 1).
- `finish_pending` also re-ran `gm_overlay_block_in_hand_live` against
  `c->pend_world`, which is just the live world pointer, so the eye-block
  sample happened one rendered frame (20 ticks) after the frame it drew. On
  the canonical tape t=660 that resolved to dirt and painted the whole frame
  with the suffocation overlay. The overlay is now split into pick/draw and
  the deferred path resolves at arm time.

The bisect that found the second one: `MAGMA_NO_HAND=1`, `MAGMA_NO_OVERLAY=1`,
a full `cudaStreamSynchronize` inside `frame_end_async`, and resetting the
shade-ctx ring at `frame_begin` each left the frame bit-identically wrong
(83_341_540 px, 3/3 runs), while the raw deferred readback with all host
retire draws skipped was normal (mean 81.4). That ruled out GPU asynchrony
entirely and pointed at the host draws in `finish_pending`.

A deferred-path CUDA replay is parity evidence again. Baselines remain
CPU-authoritative.

### Late-tape item acquisition on the canonical tape

Widening the inventory gate to every `inv`-bearing tick (rather than only the
every-20 sample grid) exposed two real divergences on
`20260721T215812Z`, which the old gate could not see:

- t=3257 slot 1: tape has item 270 (wooden pickaxe), magma has nothing.
- t=3267 slot 2: tape has item 50 (torch, count 8), magma has nothing.

Both are **crafted** items, and crafting/container clicks are not recorded in
tapes at all. The documented re-anchor for that is a `<tape>.worldpatch.jsonl`
sidecar carrying `set_inventory` events; `20260712T055346Z` has one, the
canonical `20260721T215812Z` does not. So this is an instance of the known
recorder blocker ("Legacy GUI interactions and inventory contents were not
fully recorded"), not a magma simulation bug - but it was invisible until the
gate started checking off-grid inv ticks. Fix is to author the sidecar from the
oracle session save, or to re-record with GUI interactions taped.

The tape's exit code is still 3 (its long-standing pixel FAIL short-circuits
before the state check), so the run now prints an explicit
`[gate] NOTE: inventory state ALSO failed` and `gate_baseline_diff.py`
compares the state block.

### Inventory gate coverage is still uneven

The gate now reports `ticks_independent` and flags `seeded_only`, but 13 of 23
tapes still carry only the tick-0 `inv` row and so verify nothing beyond the
seed. The recorder emits an inventory keyframe every 20 ticks as of this
change; the tapes have to be **re-recorded** before that takes effect. Count
and metadata are also still not compared - only item identity per slot - so
arrow-count drift and durability ticking remain ungated.

### Truncated tapes verify only a prefix

**Seven** tapes stop at a terminal death and only ever replay part of their
length. Four of them exit rc=0, so nightly counted them fully green while
verifying less than half:

| tape | replayed | of | % |
|---|---|---|---|
| `smoke_zombie` | 358 | 803 | 45 |
| `ender_dragon_demo` | 596 | 1614 | 37 |
| `ender_dragon_094040` | 607 | 1610 | 38 |
| `wither_skeleton` | 610 | 1202 | 51 |
| `enderman_fight` | 666 | 1402 | 48 |
| `blaze_bow_demo` | 814 | 1407 | 58 |
| `blaze_melee` | 999 | 1203 | 83 |

The deaths are **correct** - the oracle dies
at those ticks too and respawns (canonical check: tape tick 813 `hp=0.0`, tick
814 `hp=20.0`) - and `continue_after_death` is deliberately emitted only for
fluid episodes (`replay_tape.py`, `game/script.c` script loop), with a test
pinning that contract.

The problem was that nothing recorded the truncation, so a tape verifying 38%
of itself reported clean. The state gate now carries a `coverage` block and the
replay prints `[tape] COVERAGE: only N of M tape ticks were replayed`. Whether
to extend the contract past respawn (emit `continue_after_death` for any
`tape_has_respawn`, teach `first_divergence` to resume, update the pinning
tests) is an open product decision, not a bug.

### Remaining isolated render features

- One-frame loading sky after a dimension transfer.
- Enchantment glint.
- Chest model rendering and world-layout seed parity.
- Arrow ghost pitch on legacy tapes.
- General held-item registration and edge shading outside the pinned use poses.
- Sheep grazing/head pose is only partially matched.
- Dig particles are reconstructed but not pixel-perfect.

## Simulation and replay

- Entity-driven world edits such as crystal-explosion fire do not replay.
- Dragon ring-buffer cold start and unload reset differ.
- Some end-to-end Oracle runs lose the dragon boss-bar registration.
- Mob roster, AI/spawn details, and boat `UNDER_WATER` state remain incomplete.
- Aim-pin target changes can add a one-tick block-break lag.
- Hotbar arrow count can drift while the Oracle shoots.
- HUD heart-flash blinking is not modeled.

## Oracle, recorder, and world-state blockers

These are not established C product bugs, but they block direct parity claims:

- Tick 9811 Oracle save-state contains flowing water absent from pristine
  worldgen.
- Live-session population order can change individual decorations.
- Legacy GUI interactions and inventory contents were not fully recorded.
- Walking/turning tapes retain partial-tick camera registration uncertainty.
- Legacy `EntityItem` rows omit required render state.
- The mine segment contains a mid-tape staged arena-state window.

When one of these is encountered, improve the recorder/pin or classify the
capture as blocked. Do not fit C output to an unproven Oracle state.

## Verification commands

```bash
make -C c/magma test-game
bash c/magma/raster/verify/ui_hud/run_ui_hud_gates.sh
bash c/magma/raster/verify/ui_entities/run_oracle_gate.sh
bash c/magma/raster/verify/mc_capture/run_gui_actions_verify.sh
bash c/magma/raster/verify/mc_capture/run_gui_verify.sh
```
