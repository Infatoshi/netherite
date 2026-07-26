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
  no HUD, and magma used to draw one. **`capture.hide_gui` is a misnomer** and
  the original diagnosis in this entry was wrong about the mechanism: the tape's
  own `qrl_launch` records `hide_gui: false` with `strip.overlays: true`, so the
  recorder skipped the overlay pass rather than Malmo forcing
  `gameSettings.hideGUI`. The goldens draw a first-person viewmodel on nearly
  every frame - a bare arm on most of them, a held item on a few - which
  settles it. That matters because vanilla's real `hideGUI` would take
  the hand and the portal wash with it; "overlays stripped" takes only the
  overlay, so the portal wash stays tied to this flag (it is drawn inside
  `renderGameOverlay`) while suppressing magma's HAND is a workaround for the
  arm's over-bright shading, not fidelity. `options.bobView` is also false in
  this recording, so the oracle's viewmodel never bobs - it sits on identical
  screen pixels frame to frame, which is what made the yaw-sweep test decisive.
  The original (wrong) framing follows, kept because the pixel numbers in it
  are real: the goldens have no HUD and no first-person hand (Malmo forces
  `hideGUI` for the mission),
  magma used to draw both, and the gate's positional `hud` and `viewmodel`
  accepts swallowed the whole mismatch - the bottom 96 rows plus the lower
  right quadrant had no pixel verification at all on this tape.
  `capture.hide_gui` + `MAGMA_HIDE_GUI` stops magma drawing them (`3dc2d19`,
  `81ea54e`), and the gate stops accepting those regions positionally when the
  tape sets that flag: `_positional_accept_masks(hide_gui=True)` returns empty
  HUD and viewmodel barriers. The legacy sidecar regions, all recorder gaps and
  all scene-global, were widened from y1=383 to y1=479 for the same reason:
  their old limit was an artifact of the mask, not of the gap.
  `hideGUI` covers more than the overlay. `EntityRenderer.renderHand` gates
  `renderItemInFirstPerson` on `(thirdPersonView == 0 && !sleeping &&
  !hideGUI && !spectator)` (oracle-src `EntityRenderer.java:824`), and
  `GuiIngame.renderPortal` is called from inside `renderGameOverlay`
  (`GuiIngame.java:156`). `itemRenderer.renderOverlays` (block-in-hand, water,
  fire) and `hurtCameraEffect` sit outside that branch at line 833 and stay on.
  Failed frames go 14 -> 25 -> 19. That is the price of measuring a quarter of
  the frame that was never measured before, on all 157 frames; the tape was
  already failing. The rain window t=1800..2100 now resolves cleanly into
  `known:12` where before it leaked, the four pure-arm frames
  (t=360/440/480/500) are gone, and what is left in the newly measured region
  is t=520 (near-field pit floor, same dirt palette in a different projection -
  the golden's floor texels streak, magma's stay square, everything outside the
  pit matches to 0.1 percent) and t=2440 / t=2860.
  t=2440 IS the hand, and an earlier revision of this entry said it was not.
  The transient brown object in the bottom-right corner is the oracle's held
  wooden shovel. `capture.hand_from_tick` (2440 here, carried to
  `frame_capture.c` as `MAGMA_HAND_FROM_TICK`) turns magma's hand back on from
  that tick: t=2440 goes 3.167 -> 0.969 per channel whole-frame and stops
  failing (`c4b7f00`), and at t=2860 both sides now draw the same shovel.
  **What that tick actually means** is not what `c4b7f00` claimed. It is not
  the tick where the oracle's viewmodel returns - the oracle draws a viewmodel
  through the whole tape. It is the first golden at which MAGMA's held item is
  finally known to be right. The tape rows carry no `inv` field at all; the
  only inventory magma ever receives is two `set_inventory` rows in
  `<tape>.worldpatch.jsonl`, at t=2274 (item 58, crafting table) and t=2320
  (item 269, wooden shovel, slot 6). Before t=2320 magma holds nothing and can
  only draw a bare arm, which is wrong wherever the oracle holds something; the
  first golden after the re-anchor is t=2440. So the hand window is a property
  of the sidecar, not of the oracle, and it moves if the sidecar is extended.
  **t=1140..1220 is a BARE ARM, not a held block, and this entry said otherwise
  twice.** The yaw-sweep test was right that the corner object is a screen-fixed
  viewmodel; the identification of it as a held block was wrong. Opening the
  lower-right crop of the goldens at t=0, 900, 1140, 1800, 2400, 2800 and 3100
  shows the same skin-toned tapered wedge in the same screen position over
  seven completely different backgrounds (water, grass, stone, planks). That is
  `ItemRenderer.renderArmFirstPerson` with an empty main hand, and magma has
  had that path since the HAND agent landed it.
  **A discriminator that reads backwards, so do not reuse it:** the fraction of
  that box holding wood/skin colour is constant to three decimals (0.747 /
  0.748 / 0.747 / 0.747 / 0.747) over t=1140..1220 and swings wildly (0.089 /
  0.000 / 0.000 / 0.436 / 0.005) over the confirmed hand window t=2440..2520.
  The constancy is real and the reading taken from it was not: it says
  "screen-fixed", which a bare arm and a held block satisfy equally. Compare
  the two frames at 2x and look, rather than scoring a colour fraction.
  **A gate hole this exposed:** `hide_gui`/`hide_hand` drop the POSITIONAL
  viewmodel barrier, but `pixel_gate` also has a post-hoc semantic `viewmodel`
  class ("held-item region: lower-right, touching a frame edge") with a 40000 px
  budget, and the same `hud` heuristic (`y0 >= h*HUD_FRAC`) shadowed the bottom
  rows, so un-masking those regions did not actually put them under measurement
  on any frame where a heuristic fired - the 17969 px at t=1180 were classed
  `viewmodel` and never counted. Both semantic classes now honour
  `hide_gui`/`hide_hand`. On this tape `hud` disappears entirely (107 frames /
  244695 px of it were never an explanation), `viewmodel` drops to the ticks
  from 2440 where there really is a hand (63 frames / 461603 px -> 35 / 94657),
  and failed frames go **18 -> 28**. That is the price of measuring the last
  unmeasured quarter of the frame, and every one of the new failures is in it.
  No other tape sets `capture.hide_gui`, so nothing else moves.
  With the gate honest, the viewmodel is the single largest remaining
  divergence on this tape: **11 of the 20 cluster-failing frames** are
  dominated by one cluster in that corner, in two silhouettes - 30064-30503 px
  at y[349,479] x[559,853] over t=600..660, and 17076-18406 px at y[335,479]
  x[601,771] over t=700 and t=1200..1340.
  **It is NOT recorder-blocked, and an earlier revision of this entry filed it
  that way.** The oracle is empty-handed for most of the tape, so there is no
  inventory to miss; magma's arm geometry is already right. What is wrong is
  the arm's SHADING. Forcing the hand on for the whole tape
  (`MAGMA_HAND_FROM_TICK=0`, which now overrides the sidecar) and comparing the
  gate-independent per-tick `whole mean/ch` against the suppressed run: **110
  of 157 frames get worse, 10 better, 37 unchanged** (mean 5.91 -> 6.92). The
  arm is drawn far too bright. On the two cleanest frames, where terrain and
  sky agree to within 1/255:

  | tick | probe | golden | magma | ratio |
  |---|---|---|---|---|
  | 900  | arm (639,429)     | (139,103,84) | (192,173,148) | 0.72 / 0.60 / 0.58 |
  | 900  | terrain (639,299) | (87,114,69)  | (87,114,70)   | exact |
  | 900  | sky (299,59)      | (155,188,255)| (154,190,255) | exact |
  | 1140 | arm (639,429)     | (146,107,88) | (201,180,154) | 0.73 / 0.60 / 0.58 |
  | 1140 | terrain (640,301) | (152,147,103)| (152,147,104) | exact |

  **Half of that was a SKIN MISMATCH, and reading the ratio as "per-channel, so
  a lightmap colour" was wrong.** The tape header has no `skin` field, so
  `replay_tape.py` fell back to slim and magma drew ALEX against the oracle's
  Steve. The tape's own `qrl_launch.determinism.pin_skin` is true, and
  `MixinRandomSkinTexture` forces the classic model whenever it is set;
  `tape_skin()` now honours it (`db5ac63`). Against the Steve texel (150,111,91)
  the golden is a clean scalar 0.660/0.658/0.659 - it was never a coloured
  multiplier, it was a paler texture. With Steve drawn, forcing the hand on for
  the whole tape flips the A/B: **91 of 157 frames better, 29 worse, 37
  unchanged, mean whole/ch 5.91 -> 5.07** (t=900 7.44 -> 6.17, t=1140 4.69 ->
  1.38, t=2300 2.50 -> 1.15). The suppression is still on because the residual
  is unfixed, not because the arm is a net loss; flipping the sidecar is a
  release-time call since it re-baselines the tape.
  **There is no "scalar ~1.57x over-bright arm" residual. That entry was wrong
  and is retracted (2026-07-25).** It came from dividing the golden by a raw
  atlas texel, which prices in the shading the oracle also applies. Measured
  against the actual magma render on the actual arm pixels, the arm is already
  right. Method: replay the tape twice, once with `MAGMA_HAND_FROM_TICK=0` and
  once with it past the end, and take the arm mask as the pixels where the two
  differ by more than 3 (eroded 2x to drop the silhouette); then read
  golden/magma over that mask. Every rain-free frame from t=0 to t=1780 comes
  back **0.997 / 0.995 / 0.995**, on 16796 arm pixels, across seven distinct
  yaw/pitch poses (yaw 0 to -390, pitch -45 to +90). `hand_diffuse` and
  `build_arm_matrix` need no further work; do not go looking at the eye-space
  normals, which is what the retracted entry sent the last agent to do.
  What is actually left on the arm is two things, neither a hand bug:
  - **The rain window t=1800..1980**, where the arm ratio is a flat achromatic
    **0.670 / 0.668 / 0.672** while sky (0.787/0.805/0.827) and terrain
    (0.758/0.774/0.694) are chromatic and milder. The arm is the pure lightmap
    readout - it takes no fog blend - so it shows the whole error. See "The
    lightmap ignores rain and thunder" below; the arm is just the cleanest
    place to measure it.
  - t=2020..2200 (0.836/0.904/0.920) and the wild ratios at t=2280/2360/2680/
    2760, which are frames where magma holds a different item, or none - the
    `worldpatch.jsonl` inventory re-anchor gap already filed above.
  Because the arm is exact outside those two windows, **flipping the sidecar's
  `hand_from_tick` to 0 is now the better default** and no longer trades a
  known-wrong brightness for a position win. It still re-baselines the tape, so
  it stays a release-time call.
  **Pickup inference cannot rescue the held-item intervals, so do not try it.**
  The idea was to derive `set_inventory` rows from `EntityItem`s that vanish
  near the player. The tape does carry 8673 EntityItem rows, 530 of them within
  three blocks of the player, but every one is **7 fields**
  (`id, name, x, y, z, yaw, pitch`) with no item id. The worldpatch is no help
  either - all 1317 of its `set_block` rows are at tick 1, an initial-world
  snapshot rather than an edit log. Recovering WHICH item needs the tape
  re-recorded with the every-20-tick inventory keyframes the recorder now
  emits; recovering the ARM does not.
  **t=540..660 is a held DIRT BLOCK, and it is inferable from the tape.** These
  are the only 10 ticks the forced-hand A/B improves, because magma's pale arm
  is closer to a brown block than the grass it draws with no hand at all. The
  player is looking straight down (`pitch` exactly 90.0 across the window) and
  the corner is filled by a dirt-textured object that is pixel-identical at
  t=600 / 620 / 660 while the grass at its edges shifts. A whole-corner region
  test says "terrain" here and is wrong - region `[350:480, 560:854]` tracks the
  background (mean |d| 0.85 / 10.98 / 11.93 against a control of 0.96 / 15.22 /
  14.13) only because the viewmodel is a small part of it. The 12x14 patch at
  the arm's own location is identical to 0.1 across all four ticks. Take the
  measurement on the object, not on a region that mostly is not the object, and
  then look at it at 3x.
  The inputs say the same thing: `atk` at t=560, `use` at t=680, i.e. mine,
  hold, place. That is the one held-item interval on this tape whose identity is
  recoverable without re-recording - not from `EntityItem` (no item id) but from
  the block that was mined, by ray-casting the recorded eye/yaw/pitch at t=480
  (the mine runs t=480..562 at `pitch` 15, the place t=611..680 at `pitch` 90
  with `y` climbing 71 -> 73, i.e. pillaring up with what was just dug). Doing
  it needs magma's generated world, not just the sidecar: the worldpatch is a
  sparse PATCH of 1317 cells, not a full snapshot, and a raycast against it
  alone hits nothing. Nobody has tried it. The
  goldens over that window also draw the block selection outline, which is
  worth checking against magma separately.
  **A wrong reading to not repeat:** the t=1800..1960 window first looked like
  a missing first-person held item - the oracle shows a dark held log and
  magma appeared to show none. It is not. Golden/candidate over that window is
  a uniform ~0.45 ratio across the whole frame including the sky (t=1900 whole
  0.726, sky 0.809, ground 0.696), i.e. the already-filed oracle rain
  darkening; magma's log is simply drawn at full brightness against grass and
  reads as absent at a glance. Zoom before concluding.
  The other canonical tape, `20260721T215812Z`, has a HUD on all 181 goldens
  and is unaffected; do not set `capture.hide_gui` on it. Its own 7 failed
  frames are all ONE family and it is the texel-selection residual above, not
  anything tape-specific: t=260 (7291 px) and t=460 (6252 px) fail on
  `texel-selection` clusters at sel 0.55 / 0.50 on a distant canopy and a
  grazing leaf underside, where both sides draw the same leaves from the same
  palette in a shuffled arrangement plus a 1-2 px sky/leaf silhouette edge.
  t=300 / t=320 / t=700 have no UNEXPLAINED cluster over 300 px at all and fail
  purely on mild-shift, i.e. the same wash spread thin. Closing the texel
  residual closes this tape.
- `scenario_slime_bounce_20260723T001527Z`: the slime platform renders too
  dark. Baseline on current master: **15 failed frames, 6709 UNEXPLAINED px**.
  `models/block/slime.json` (1.11.2 jar) has TWO elements - inset core
  `[3,3,3]..[13,13,13]` and full cube - both without cullface.
  `BlockSlime.getBlockLayer` is TRANSLUCENT. Emitting the real inset core
  (`5da6b29`) took 19 -> 16 failed frames.
  **Per-pixel arithmetic (t=50, face ROI y[300,360], a=188/255 from slime.png):**
  solve `g = C·(1-(1-a)^2) + B·(1-a)^2`, `c = C·a + B·(1-a)` on dark residual
  pixels gives C≈tex mean [120.7,200.0,101.1] and B≈0. Golden matches dual-layer
  over black; magma matches single-layer. On dual-covered pixels (block
  centers) magma already equals golden, so `raster_cpu` SRC_ALPHA
  (`c·a + d·(1-a)`, blend=1, no depth write) is correct when both layers hit.
  **Coverage map:** residual is a block-scale checkerboard - bright dual centers
  (core XY [3,13]^2) vs dark single rims (the 3/16 XZ frame where only the
  outer top draws). Golden is uniform dual brightness across the whole face.
  Rim fraction of a top face is `1-(10/16)^2 ≈ 61%`, which matches the bulk of
  the dark residual.
  **Levers tried (2026-07-25, wt/slime2), all rejected or insufficient:**
  - Full generalQuads (no neighbor cull on both elements): 15 -> 17 failed,
    darker (confirms prior -93/ch overshoot). Vanilla draws those quads
    (`BlockModelRenderer.java:105-110`) with GL cull + `sortVertexData`, but
    magma still overshoots when given the same layer count.
  - Translucent B2F sort alone (painter's order on 6-vert quads): 15 -> 14
    failed on this tape, but nightly REGRESSION on `elytra_dip` UNEXPLAINED
    784 -> 16495. Not landed.
  - Always-emit all 6 core faces (outer still culled): no further gain; core
    sides do not fill the rim to dual-top brightness (side shade 0.8 stacks to
    ~0.78·C, dual top is ~0.93·C).
  - Coplanar outer re-emit: 12 failed / 4.03 (prior), fakes uniform dual
    coverage; not the model; not landed.
  Open gap: how vanilla keeps the rim as bright as dual-top without the
  overshoot magma hits when fed full generalQuads. Blend equation itself is
  not the bug on dual-covered pixels.
- `scenario_elytra_dip_20260723T001355Z`: 4 failed frames. The t=70/t=80
  whole-frame dark decay is `fogColor1`: `Block` marks liquids translucent and
  therefore `useNeighborBrightness`, while `World.getLightBrightness` calls
  `getLightFromNeighbors`. Magma uses the water cell's own skylight 11 instead
  of the adjacent 14. Vanilla's five-neighbour maximum reduces t=70 5.71 ->
  2.13/ch and t=80 3.14 -> 1.97/ch, but the same exact lookup regresses
  `water_dive` from 0 to 14 failed frames because magma's simulated neighbour
  skylight is not yet oracle-exact there. It is therefore not landed; fixing
  the light field is prerequisite. The other failures are t=0 (4.39/ch plus
  an 85 px one-row registration cluster) and t=60 (10.62/ch water-colour wash,
  463 unexplained px). A separate native `water_flow` quadrant experiment
  removed that cluster locally but caused broad `water_dive`/`water_flow`
  regressions and was also rejected.
  Re-confirmed from the frames (2026-07-26): only t=60 is underwater (a
  one-frame dip); golden's underwater frame is brighter with per-channel
  ratios R 1.084 / G 1.072 / B 1.135, and after resurfacing golden carries a
  decaying brightness excess (1.026 at t=70, 1.016 at t=80, 1.004 at t=90,
  gone by t=110) - a `fogColor1` that dropped less during the dip than
  magma's, exactly the neighbour-brightness gap `game/underwater.c:40` states.
  Fixing the simulated skylight field is the prerequisite; nothing else in
  this tape's window is a separate bug.
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

### The world diverges from the oracle's on the canonical tape

`20260712T055346Z` reports `world nearby_hash` deltas on **96 of its 157**
sampled ticks. The hashes agree through t=300 and separate from t=400 onward,
which is where the mission starts digging. Block state is not compared by the
replay gate (only physics, inventory and pixels), so this has been sitting
under the pixel numbers rather than being reported as itself.

It accounts for the tape's worst remaining frames, t=2840..2900: 47846-48202
px, `mean_delta [-41.69, -39.60, -38.63]`, an achromatic ~40-level darkening
over a third of the frame. Side by side at 2x it is block content, not shading:
both sides are in the same mined tunnel and draw the same held shovel, but
magma has a large flat near face filling the left of the view where the golden
sees a lit tunnel receding. One side has a block the other does not. The player
holds `atk=1` continuously through that stretch. The gate classes both frames
as `particles` and only flags them because they blow the 40000 px class budget;
an achromatic darkening over a third of the frame is not particles, so the
class is wrong even though the failure is real.

The cause is the recorder, not magma's simulation: dig progress depends on the
held tool and on GUI/inventory interactions that tapes do not record (see the
recorder blocker and the `worldpatch.jsonl` re-anchor above), so magma's dig
timing drifts from the oracle's and the two worlds part company. Chasing these
frames as rendering bugs is wasted effort until either the world is re-anchored
or the tape is re-recorded with block edits taped.

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

### slime_bounce horizon band: NOT a render-distance cull mismatch

All 15 of `slime_bounce`'s failed frames are the same static artifact: a band at
the horizon (y 244..253) spanning the full width, identical from t=60 on.
Per-column silhouette at t=80 (first row from y=235 with blue < 200): **711 of
854 columns agree exactly, 143 have magma's edge exactly one row lower**, in 4
runs of 33/37/33/35 cols (plus 1-2px stragglers).

**Hypothesis tested and refuted (wt/chunkcull, 2026-07-26):** magma's
render-distance cull does **not** use a different metric or off-by-one vs
vanilla.

| Side | Cull test | Metric |
|------|-----------|--------|
| magma | `game/world_live.c:381-388` (`gm_world_mesh_view`; twin at 459-466) | Chebyshev square `cx,cz ∈ [ccx-R, ccx+R]` with R=8, then `cr_aabb_in_frustum` |
| vanilla | `RenderGlobal.getRenderChunkOffset` (oracle-src ~1027) + `ViewFrustum` `(2*RD+1)^2` | `abs(playerChunkOrigin - neighborOrigin) > RD*16` → reject (keeps `\|d\| ≤ RD`); same inclusive Chebyshev |

No Euclidean chunk test in either path. Magma's `<= R` matches vanilla's `>`
(equality kept). Frustum port is the verified ClippingHelper path
(`core/frustum.h`); full-column AABBs for outer-ring ground sections at this
pose are **kept** for the front diagonal chunks `(±8,8)`.

**Diagonals check (tape yaw=0, vFOV 70 → hFOV ~102.5°):** run mid-angles are
**-45.8°, -34.4°, +34.3°, +45.8°**. Only two of four sit on the square diagonals;
a pure cheby-vs-euclid mismatch would be two large side sectors (~400 cols
each), not four ~35-col runs. So the angular pattern does **not** diagnose a
distance-metric bug.

**Vanilla ViewFrustum centering note (not the fix direction):** at player
(0.5,0.5), `updateChunkPositions` uses `floor(x)-8` and covers chunk origins
**-9..7**, then the BFS distance filter keeps **-8..7**. Magma's symmetric
**-8..8** is one chunk *longer* on the + side, so matching that quirk would not
raise magma's horizon.

**Cause remains open** (elsewhere than the RD cull): on the 143 run columns the
sky rows above the band are bit-identical, but the first non-sky row is a
fog/edge blend where gold crosses blue<200 one row earlier; terrain rows below
the band also still differ. `sky.h` `GM_TERRAIN_ZFAR = RD*16*sqrt2` already
matches `EntityRenderer.setupCameraTransform`. Do not widen R or fudge fog end
to paper over this.

### The oracle's fogColor1 had not converged when recording started

Every scenario tape is worse at t=0 than at t=10, by 2-6x, on the whole-frame
mean. It is the same shape on all of them and it had never been filed because
each tape's t=0 sat under its own gate class. It is the whole reason
`suffocate_camera` and half the reason `elytra_dip` fail their gate: both have
t=0 failures with **zero** unexplained pixels, i.e. the frame is uniformly off
rather than structurally wrong.

The direction settles it: **magma is flat from t=0 and the ORACLE ramps.**
On `suffocate_camera`, golden sky goes 135.1 -> 140.9 and golden grass
117.9 -> 124.9 over the first 40 ticks while magma sits at 141.1 / 125.0 the
whole time. The error decays by 0.35 per 10 ticks, and 0.9^10 = 0.3487 - that
is exactly `EntityRenderer.updateRenderer`'s
`this.fogColor1 += (f2 - this.fogColor1) * 0.1F` (`EntityRenderer.java:327`),
which starts at 0 on a fresh EntityRenderer and had not finished converging by
recstart. magma implements the smoother correctly but seeds it converged
(`gm_uw_fog_c1_seed`, and `underwater.h` states the assumption out loud: "the
oracle client has been running long before recstart, so its c1 has converged").

Mechanism check, on `suffocate_camera` (whole mean/ch, tape floor 0.75):

| c1 seed | t=0 | t=10 | t=20 | t=30 | t=40 |
|---|---|---|---|---|---|
| steady (current) | 7.69 | 2.41 | 1.16 | 0.85 | 0.75 |
| 0.88 | 3.70 | 1.19 | 0.89 | 0.77 | 0.76 |
| **0.90** | **2.44** | **0.82** | **0.80** | **0.73** | **0.72** |
| 0.93 | 3.17 | 0.98 | 0.77 | 0.74 | 0.72 |

One parameter, a single clean optimum, and fitting it on t=0 alone drags t=10,
t=20 and t=30 to the tape's floor as a side effect - it is the mechanism, not a
per-frame fit. With the seed supplied, the tape's gate goes **FAIL -> PASS**.

**Do not hardcode 0.90.** The starting value is a property of the recording
session and is not derivable from the tape: `cobweb_fall` and `water_dive` have
near-identical `total_time` (112 and 113) yet start at 0.9946 and 0.9612,
because what matters is the light the client saw while the world loaded. The
per-tape t=0 ratios measured on the sky band are suffocate 0.9632, water_dive
0.9612, lava_walk 0.9846, elytra_dip 0.9903, soulsand_ice 0.9941, cobweb_fall
0.9946, fence_collide 0.9996, flow_convert 1.0068 (the last two have no ramp).

Fixed the only honest way: the recorder now writes `fog_color1` into the tape
header (`QuantizedRL.recFogColor1`, reflected off `EntityRenderer`, -1 when
unreadable), and `replay_tape.py` seeds magma from it when present via
`MAGMA_FOG_C1_INIT`. Tapes recorded before the field existed return None and
keep the steady-state seed, so nothing re-baselines. **This is inert until the
tapes are re-recorded** - the same re-record that would close the inventory
keyframe and rain gaps.

`MAGMA_FOG_C1_INIT` also works standalone, for sweeping the value on tapes that
predate the header field.

### The lightmap ignores rain and thunder

`build_lightmap_lut` (`frame_capture.c`) calls
`cr_lightmap_rgb(0, sl, bl, sun, 0.0f, 0.0f)` - rain and thunder hardcoded to
zero - and its `sun` comes from `fc_sun_brightness(sin_table, world_time)`,
which takes no weather at all. Vanilla's `updateLightmap` uses
`world.getSunBrightness(1.0F)` (`EntityRenderer.java:892`), and
`getSunBrightnessFactor` (`World.java:1551`) scales it by
`(1 - rainStrength * 5/16)` and again by `(1 - thunderStrength * 5/16)`. So
magma lights every rainy frame as if the sky were clear.

Cleanest measurement is the first-person arm on the canonical tape, because it
reads the lightmap directly with no fog blend on top: through t=1800..1980 the
arm is a flat achromatic **0.670** golden/magma while sky (0.787/0.805/0.827)
and terrain (0.758/0.774/0.694) are chromatic and milder - they pick up part of
the darkening through the fog colour, which magma does model. Outside that
window the same arm measures 0.997.

Blocked on the recorder, not on the renderer: **no tape has ever recorded rain**
- there is no rain field in any header or row, so magma cannot know it is
raining. The recorder now writes `rain_strength` and `thunder_strength` into the
header (both public on `World`); wiring them through `build_lightmap_lut` is
pointless until a tape carries them, and the values are per-tick anyway, so a
window that starts mid-tape needs them on rows rather than the header. This is
what the canonical tape's `known:12` rain class has been standing in for.

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
- Legacy tape headers omit `EntityRenderer.fogColor1` and its pre-capture
  brightness history. On `scenario_suffocate_camera_20260723T001923Z`, the
  actual in-block overlay frames pass, but t=0 is a frame-wide shading offset
  that decays from 7.69 mean/ch at t=0 to 2.41 at t=10 and 1.16 at t=20.
  Vanilla carries this 0.1/tick smoother in
  `EntityRenderer.updateRenderer`; reconstructing its initial value from the
  golden would be fitting an unrecorded constant.
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
