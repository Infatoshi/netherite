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
  t=260, on the near canopy and a distant tree). Attributed to
  nearest-neighbour texel selection on minified leaf faces, not to shading or
  geometry: on leaf interiors the per-channel delta is zero-mean with a large
  spread (near canopy mean +0.1/+0.2/+0.2, sigma 19/28/8; distant tree
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

- `scenario_soulsand_ice_20260723T001810Z`: 45 of 51 frames fail, but only one
  frame has any UNEXPLAINED cluster (1226 px at t=50, and that is hand
  silhouette just outside the viewmodel mask, so gate topology). The other 44
  fail mild-shift alone (`mean_abs` 3.57-5.29 over the 3.32 budget). The
  residual is fogged lower sky / horizon, not worldgen or the mesher: at t=100
  the oracle horizon is `[60,74,101]` against magma's `[53,64,84]`, while the
  zenith nearly matches. Suspect `game/sky.c` fog/sky blend and the
  `terrain_shades` fog setup.
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

### Inventory state gate is shallow

`collect_state_assertions` samples every 20 ticks and only checks ticks that
carry an `inv` row. On every current scenario tape that is tick 0 alone
(`inv_checked=1`), and replay now seeds live inventory at tick 0 from
`ticks[0]["inv"]`, so the single checked tick largely verifies the seeding path
rather than inventory evolution. The divergence this closed was real (empty
inventory where the tape had a bow and 64 arrows), but hardening requires
re-recording tapes with periodic `inv` rows.

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
