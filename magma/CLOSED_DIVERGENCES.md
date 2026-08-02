# Closed divergences

Resolved, retracted, or superseded entries moved out of OPEN_DIVERGENCES.md
so the open file stays an actionable list. Entries are preserved verbatim
(full forensics) because they document why a question is settled; read them
before re-investigating anything that smells similar. Newest at top.

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

### slime_bounce horizon band: fog-blend decomposition (wt/horizonfog, 2026-07-27)

Measured on t=80 of `scenario_slime_bounce_20260723T001527Z` (flat world, pose
feet `(0.5,4,0.5)` yaw/pitch 0, RD8, fog linear 96→128, GL_EYE_RADIAL_NV).
Replay via `replay_tape.py --cpu`; goldens from the tape frames dir. Silhouette
= first row from y=235 with blue < 200. Baseline gate: 15 failed frames,
UNEXPLAINED 6709.

**Blend model.** Horizon sky / fog colour F = (179, 207, 255) (matches
`updateFogColor` clear at this noon flat take). Unfogged terrain T recovered
from a `MAGMA_FOG=0` replay of the same tape (same geometry, no fog lerp). Then
for each edge pixel:

```
c = (1 - t) * T + t * F    →    t = fog factor in [0,1]
```

G−M colour delta at the first gold-visible row is **parallel to (F−T) with
cos ≈ −0.999** (orth residual ~2/ch): same albedo T, almost pure fog-factor
difference. Not a texel flip, not a sky-gradient bug (rows above the band are
bit-identical).

**Per-run numbers (t=80, mean over run columns at first gold-visible row):**

| run cols | mid-angle | gold sil y | magma sil y | t_gold | t_magma | Δt (m−g) | r_eff gold | r_eff magma |
|----------|-----------|------------|-------------|--------|---------|----------|------------|-------------|
| 58–90 (33) | −45.8° | 246 | 247 | 0.647 | 0.831 | **+0.185** | ~117 m | ~123 m |
| 174–210 (37) | −34.4° | 245 | 246 | 0.626 | 0.825 | **+0.199** | ~116 m | ~122 m |
| 644–676 (33) | +34.3° | 245 | 246 | 0.626 | 0.820 | **+0.194** | ~116 m | ~122 m |
| 762–796 (35) | +45.8° | 246 | 247 | 0.639 | 0.831 | **+0.192** | ~116 m | ~123 m |

Full-width mean Δt at `min(sil_g, sil_m)` is **+0.178** (std 0.057) — the
same ~0.18 over-fog is present on the 711 agreeing columns; the 143 flips are
only where gold's t pushes blue under 200 one row earlier than magma.

Re-fogging magma's nofog T with `t_magma − 0.18` cuts edge-row error from
~25/ch to ~4/ch on every run (residual then matches gold_as_fog(T) ~2/ch). So
the band **is** the fog-factor gap, not a separate coverage hole.

**Geometric coverage (`MAGMA_FOG=0`).** Magma has terrain (blue≪200) from y=244
on the run columns; with fog on, y=244–245 are sky-exact (t=1, fully fogged to
F). Gold's visible sil is y=245/246. Magma is not missing far geometry — it
draws it, fully fogged, then shows a more-fogged transition row.

**Analytic flat-plane check** (ground y=4, eye y=5.62, vFOV 70, pitch 0):

- Magma's measured t matches radial fog on the plane hit: mean
  `|t_magma − t_radial(r_hit)| ≈ 0.002` for |angle| > 10°.
- Gold is systematically under-fogged vs the same hit:
  `|t_gold − t_radial| ≈ 0.19`. Planar |z| is worse for gold
  (`|t_gold − t_planar| ≈ 0.30`).
- Magma vs documented ramp 96/128: RMSE **0.0015**. Gold vs 96/128: RMSE
  **0.15**. Best unconstrained fit for gold is roughly start≈102 end≈134
  (not a constant present in oracle-src).

**Hypothesis results:**

1. **Per-vertex vs per-pixel fog — REFUTED as the 0.18 gap.** Magma already
   does perspective-correct per-fragment radial fog (`raster_cpu.c` interpolates
   `eye_dist_w`, `shade.c` applies the linear ramp). Vanilla 1.11.2 sets no
   `glHint(GL_FOG_HINT, …)` (default DONT_CARE). On 1×1 block faces (both
   mesher and vanilla FaceBakery), max |t_true − t_affine_vertex| and
   |t_true − t_persp_lerp_r| are **~7e−5** at the horizon — three orders below
   0.18. The sky-plane Gouraud fix (`ac47c2b`, 64×64 tiles) does not transfer:
   terrain quads are 1 m, not 64 m.

2. **Planar |z| vs radial — REFUTED as the fix direction.** Oracle capture
   queries `fog_distance_mode_nv = 34139` (GL_EYE_RADIAL_NV); magma matches that
   and the analytic plane. Forcing planar was already shown to regress the
   canonical tape (OPEN_DIVERGENCES "Canonical tape residual"). Gold is closer
   to radial than planar but still Δt≈−0.18 vs true radial.

3. **Projected far-edge / half-pixel — open but not sufficient alone.** Far
   ground tops foreshorten to ~0.04 px of height; the visible rim is extremely
   pitch-sensitive (Δr ≈ 6 m for ~0.04° ≈ 0.3 px). A pure integer row-shift of
   magma vs gold is a *worse* match than same-row fog adjust (best dy=0). The
   data prefer "same pixel, same T, different t" over "magma is one row late."
   A sub-pixel registration gap could still contribute at the threshold, but it
   does not explain the global Δt≈0.18 on agreeing columns.

**What magma implements (oracle-aligned):**
`EntityRenderer.setupFog(0)` linear start=`far*0.75` end=`far` with
`far=RD*16=128` (`EntityRenderer.java:2025–2036`), plus
`glFogi(34138, 34139)` when NV_fog_distance is present (`:2039–2041`). Magma:
`GM_TERRAIN_FOG_START/END` in `sky.h`, radial `eye_dist` in `transform.c` /
`shade.c`. No `glHint` for fog in oracle-src.

**Not a safe code fix yet.** Dropping magma fog by ~0.18 (or widening fog end /
narrowing start toward the empirical 102/134 fit) would paper over gold and
break the documented vanilla ramp that magma already matches to 0.0015 RMSE on
this geometry. Next leads if revisited: (a) capture-side fog evaluation on the
recording GL stack (does the golden's driver honour EYE_RADIAL the same way the
seed7 probe claims?), (b) any remaining view/projection registration at the
0.3 px level that would put gold on a nearer isosurface while sharing T, (c)
confirm with a depth/eye_dist dump from the live Java capture at these columns.

Do **not** widen RD cull, fudge `GM_TERRAIN_FOG_*`, or retune CLASS_PIXEL_BUDGETS
for this band.

**Addendum (2026-07-27, independent re-measure): the fit degeneracy is
resolved - same fog color, real fog-factor gap - and lead (b) is the live
one.** On t=80 agreeing columns, both sides converge to the identical
sky/fog color (179,207,255) in the row above the silhouette and to the same
grass color a few rows below; magma's last terrain rows are consistently
~+37 blue foggier (x=100: gold (128,154,152) vs magma (149,176,191); x=220
and x=530 alike, and magma's sil+1 row is still fog-tinted where gold's is
already clean grass). So it is genuinely Δt with shared F, not a fog-color
difference. Converting: Δt 0.18 x 32-block ramp ≈ 5.8 blocks of effective
distance, and at the horizon's ~6 m per pixel row that is ~0.3 px of
vertical registration - exactly the sensitivity the H3 note computed, and
invisible to the integer-shift test that "refuted" it. A single sub-pixel
vertical projection offset (eye height, pitch, gluPerspective cotangent, or
viewport pixel-center convention) explains a global horizon-only Δt with
zero near-field effect. Discriminating probe: measure the sub-pixel screen
position of a tall NEAR vertical edge (slime block silhouette) golden vs
magma on the same frame - a registration offset shows there too; a pure fog
difference does not.

Probe results (same day): the near-edge measurement over 122 high-contrast
edge pairs at t=80 gives magma-minus-gold dy median 0.000 px (mean 0.21,
std 0.41 - outlier-driven), and a lower-frame band agrees (median 0.000).
A uniform screen shift, eye-height offset, or FOV-scale error would all
have moved those near edges by the same ~0.3 px, so every screen-space form
of H3 is now refuted alongside H1/H2. Separately, the oracle's LIVE GL fog
state is on record: `mc_capture/camera_seed7.json` captures
`fog_start 96.0, fog_end 128.0, fog_mode 9729 (LINEAR),
fog_distance_mode_nv 34139` from the running client, so the empirical
"102/134" fit is NOT the oracle's fog config either. What survives: either
the capture GL stack's fog EVALUATION deviates from t=(d-96)/32 at large d,
or golden's row-to-distance mapping at grazing incidence differs in a way
near edges cannot see. Next probe that separates them: compute golden's
empirical t(d) across the whole 96..128 band on the mc_capture pose/seed7
scenes (exact camera + fog state recorded per capture) against analytic
ground distances - a fog-curve deviation shows as t(d) bending off the
ramp everywhere; a mapping difference shows t(d) on-ramp but with d
shifted only on grazing ground, not on vertical faces at the same
distance.

**Addendum (2026-07-27, wt/fogcurve): t(d) probe — hypothesis A survives,
B refuted.** Repro:
`cd verify/trace && uv run --no-project --with numpy,scipy,pillow
python fogcurve_probe.py --scene all --out ~/dev/nw/.tmp/fogcurve`
(uses existing slime_bounce fog/nofog magma frames under `.tmp/hfog_{out,nofog}`
if present; seed7 re-rendered via `game_candidate --seed 7 --fov 77` with
`--depth` dump).

Method: recover `t = median_ch (P − T)/(F − T)` with `T` from `MAGMA_FOG=0`
and recorded `F`. Analytic eye-radial `d` on slime flat ground via ray/plane
at y=4; seed7 `d` from magma depth buffer. Magma's own `t_magma` tracks
`t_ramp = clamp((d−96)/32)` to RMSE 0.002 on clean grass (control).

*slime_bounce t=80, clean grass tops, plane d (n=991 HC; bulk 100..122 n=599):*

| d     | n   | t_gold | t_magma | t_ramp | t_gold−ramp | t_magma−ramp |
|-------|-----|--------|---------|--------|-------------|--------------|
| 100–102 | 90 | 0.005 | 0.162 | 0.161 | **−0.155** | +0.002 |
| 104–106 | 51 | 0.120 | 0.281 | 0.279 | **−0.159** | +0.001 |
| 108–110 | 52 | 0.241 | 0.408 | 0.406 | **−0.165** | +0.002 |
| 112–114 | 48 | 0.344 | 0.539 | 0.537 | **−0.193** | +0.001 |
| 116–118 | 58 | 0.474 | 0.656 | 0.655 | **−0.181** | +0.001 |
| 120–122 | 39 | 0.600 | 0.783 | 0.781 | **−0.181** | +0.002 |

Bulk mean `t_gold − t_ramp` = **−0.169** (flat across the band, not a
growing bend). Implied constant distance shift
`δ = d − (96 + 32·t_gold)`: median **5.26 blocks** (mean 5.33, std 1.52);
`0.18 × 32 = 5.76` matches the horizon-band Δt. Free linear-ramp fit for
gold: start≈**101**, end≈**134.5** (RMSE 0.045 vs 0.173 on vanilla 96/128).
Magma vs vanilla ramp RMSE **0.0023**. Flat world has no far vertical faces
in the fog band (entities empty at t=80), so orientation needs seed7.

*seed7 (camera_seed7.json: eye (16.5, 89, 268.5), pitch −40°, FOV 77,
F=(179,206,255), fog 96/128 LINEAR EYE_RADIAL), mid-band d∈[104,120],
material classes from nofog albedo + orth-to-fog filter:*

| class | n   | t_gold−ramp | t_magma−ramp | t_gold−t_magma |
|-------|-----|-------------|--------------|----------------|
| trunk (vertical) | 666 | +0.212 | +0.002 | +0.210 |
| grass (ground)   | 975 | +0.174 | +0.002 | +0.172 |

`grass − trunk` residual = **−0.038** (B predicted **−0.18** if only
grazing ground were distance-shifted; A predicted ~0). Absolute seed7
t_gold is *positive* (gold looks more fogged) because magma nofog `T` is not
bit-aligned to the golden's albedo (lighting/smooth residuals on the
mc_capture path); that biases both classes equally and is why the
**relative** residual is the discriminator, not the absolute sign.

**Verdict: A (orientation-independent fog-curve gap).** Gold is under-fogged
by ~0.17 vs the documented linear EYE_RADIAL ramp on clean same-geometry
ground; vertical faces do **not** sit on the ramp while ground is offset, so
B (grazing-only distance mapping) is out. Equivalent descriptions of A: a
constant Δt ≈ −0.17, a constant δd ≈ 5.3 blocks, or an effective
start/end ≈ 101/134.5 — all the same linear warp. Live GL state still
reports 96/128, so this is evaluation / post-fog, not the configured
params.

**No magma code change.** Magma already matches the oracle formula
(`EntityRenderer.setupFog(0)` start=`far*0.75` end=`far`,
`glFogi(34138, 34139)` EYE_RADIAL) to 0.002 RMSE; fudging
`GM_TERRAIN_FOG_*` toward 101/134 would paper over the golden and break the
documented ramp. Next leads: (1) llvmpipe / capture GL fog evaluation vs
spec at large eye-radial d (does the driver honour LINEAR EYE_RADIAL as
`(d−start)/(end−start)`?), (2) any post-fog colour path on the recording
client that pulls toward terrain, (3) a live depth/fog-factor dump from the
Java capture at the same columns. Do not retune CLASS_PIXEL_BUDGETS for the
band.

Two sharpening facts (2026-07-27 review): the fitted endpoints are BOTH the
configured ones scaled by the same factor - 101/96 = 1.052 and
134.5/128 = 1.051 - so the warp is exactly "the capture stack's fog
distance reads as d/1.05", a multiplicative radial-distance underestimate,
not an additive offset or a start/end reconfiguration. And the recording
renderer is on record as llvmpipe (Mesa 26.0.3, the `glxinfo` preamble in
every `start_vnc_client.sh` log), so lead (1) concretely means: how does
Mesa/llvmpipe evaluate GL_NV_fog_distance EYE_RADIAL - per-vertex fog
coord with screen-linear interpolation across the quad would systematically
underestimate the radial distance of interior pixels on large ground quads
(chord-vs-arc), which has the right sign and is orientation-independent at
these view angles. Reproducing THAT (vertex-evaluated radial fog,
interpolated) in magma would be a mechanism port, not a fudge - but measure
it against a llvmpipe minimal repro first.

### slime_bounce: horizon band CLOSED, shell contradiction isolated (2026-07-30)

The horizon-band family is SOLVED: the camera sat 0.08F too high because
magma skipped EntityPlayerSP's sneaking eye height (1.62 -> 1.54); fix merged
65ea82a, and three delegates independently removed a second, duplicate
application of the same offset in frame_capture. Re-recorded
scenario_slime_bounce_20260730T095754Z: t=0 is clean (the fogColor1 recorder
fix confirmed), and EVERY remaining failed frame has 0 unexplained px,
failing only the global check. That residual is the slime-shell
inset-vs-full-element contradiction documented below: honest geometry
attempts render worse (fix_slimebounce findings); a fake double-shell was
rejected. Old tape 20260723T001527Z is superseded and retired. The 2026-07-27
fog-blend decomposition below remains for reference; its baseline numbers
predate the eye-height fix.

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

- `scenario_elytra_dip`: **re-recorded 2026-07-27 as `20260727T214459Z`**
  (old `20260723T001355Z` moved to `tapes/retired/`, baseline swapped). The
  new tape has settled liquids (200 settle ticks per setup command), a
  converged recorded `fog_color1` (0.99999976 in the header), and the lava
  sea trimmed to x<=36 - the first settled recording (`213715Z`, also in
  retired/) landed at x=40.7 in the last lava column, burned to death
  standing there, and respawned at world spawn, which replay cannot follow.
  Current state: **1 failed frame, t=60, 3010 px** - narrow ~12px vertical
  strips inside the curtain where the golden renders darker falling-water
  streaks and magma is flat brighter blue (cluster means g [45,65,160] vs
  m [48,69,182]). Texture animations ARE pinned on this tape, so it is not
  animation phase; it is the flow-texture selection/orientation on falling
  cells viewed from inside the curtain, the same family as the rejected
  native `water_flow` quadrant experiment. Whole-frame at t=60 is
  mean_abs 3.57 (threshold 3.32), ratio g/m ~0.98/ch.
  The remainder of this entry documents the RETIRED `20260723T001355Z`
  tape's failures for the record; its mechanisms (fogColor1 warmup at t=0,
  mid-growth waterfall at t=60-80) are closed by construction on the new
  tape.
  Old `scenario_elytra_dip_20260723T001355Z`: 4 failed frames.
  **RETRACTED (2026-07-27): the t=70/t=80 "neighbour brightness for water"
  mechanism above was wrong.** Registry finalization (`Block.java`
  `registerBlocks` tail) sets `useNeighborBrightness` only for stairs, slabs,
  farmland/grass path, translucent, or `lightOpacity == 0` blocks. Water has
  opacity 3 and `MaterialLiquid.blocksLight()` keeps it non-translucent, so
  vanilla samples the water cell's OWN light - exactly what magma already
  did. Forcing the neighbour lookup for water fails `water_dive` 93 frames.
  Lava DOES qualify (registered without `setLightOpacity`, so opacity 0 -
  magma's 255 was the real light bug, fixed with the exact
  `getLightBrightness` port through rk_14 in `game/underwater.c`; all four
  water tapes now diff clean against the oracle's saved SkyLight, see
  `trace/skylight_diff.py`). t=70/t=80's decay improved by neither, which
  fits the mid-growth waterfall below: the oracle's feet crossed water cells
  whose growth state magma's frozen approximation does not carry.
  The other failures are t=0 (4.39/ch plus
  an 85 px one-row registration cluster) and t=60 (10.62/ch water-colour wash,
  463 unexplained px). A separate native `water_flow` quadrant experiment
  removed that cluster locally but caused broad `water_dive`/`water_flow`
  regressions and was also rejected.
  Re-confirmed from the frames (2026-07-26): only t=60 is underwater (a
  one-frame dip); golden's underwater frame is brighter with per-channel
  ratios R 1.084 / G 1.072 / B 1.135, and after resurfacing golden carries a
  decaying brightness excess (1.026 at t=70, 1.016 at t=80, 1.004 at t=90,
  gone by t=110) - a `fogColor1` that dropped less during the dip than
  magma's. With the neighbour-brightness reading retracted, the remaining
  driver is the water cells themselves: the oracle's dip crossed a
  partially-grown curtain whose cell contents (and thus feet light) differ
  from magma's frozen approximation.
  **The t=60 463px cluster is a DEVELOPING waterfall the replay cannot
  represent (2026-07-26).** The scenario fills a single water wall at x=10
  (`/fill 10 4 -3 10 22 3 water`) and starts recording immediately; the x=9
  and x=11 curtain columns are that wall's live sideways spread, still
  growing through the first ~seconds of the tape. The world save is
  post-capture (fully grown, all three columns, oracle skylight 12/9/12 at
  z=0), so `tape_to_script`'s elytra post-capture-spread heuristic freezes an
  approximation: x9+x10 falls patched in at t0, x11's dropped, everything
  cleared at t=65 before player contact. Both directions of "fix" were
  measured and are wrong: keeping x11's falls (sustained-under-source
  exemption in `post_capture_spread`) takes t=60 from 463 to 14047
  UNEXPLAINED px because the golden still sees past the curtain's right edge
  at t=60; the committed drop leaves the 463px top-of-screen sliver where the
  golden's partially-grown x11 fall has water and magma has none. Magma's
  fluid CA does not grow it either: snapshot water is deliberately not
  fluid-marked (re-simulating patched water was the rejected native
  water_flow experiment). The clean fix is in the scenario, not the replay:
  add a settle wait between the water fill and recstart and re-record -
  already on the re-record decision list.

### Waterfall ENTRY window on the dense elytra tape (t=58..65) - CLOSED

Root cause was NOT the water at all: it was the elytra ARMING tick's camera
height, and t=58 is simply the tick where a 1.22-block camera error points at
a waterfall. Measured per-tick before the fix, magma's t=58 frame showed water
where the oracle still showed sky over rows 0..75 (oracle (137,179,255) vs
magma (55,80,223) at x=427), while t=57 and t=59 agreed - a one-tick-early
`getEyeHeight` 1.62 -> 0.4 drop, not a fog/overlay/liquid-boundary difference
(magma's own `gm_uw_eval` reports `fluid=0` across the whole window; the eye
does not enter water until t=67).

Vanilla: the client only SENDS `CPacketEntityAction(START_FALL_FLYING)`
(`EntityPlayerSP.onLivingUpdate`:1028-1036). Entity flag 7 is set on the
SERVER (`NetHandlerPlayServer.processEntityAction` case START_FALL_FLYING:1019
-> `EntityPlayerMP.setElytraFlying`:1441) and reaches the client one tick later
as entity metadata (`EntityTrackerEntry.sendMetadataToAllAssociatedPlayers`).
So on the arming tick the client's `isElytraFlying()` is still FALSE, and
everything the client derives from it is still standing-pose:
`EntityPlayer.updateSize`:372 keeps the 1.8F box and `getEyeHeight`:2486
(`isElytraFlying() || height == 0.6F`) keeps 1.62. magma set `elytra_flying`
inline right after `psv_physics_tick` and then ran `psv_update_elytra_size` in
the SAME tick, so the arming tick rendered from eye 0.4. The flag is now staged
in `PsvPlayer.elytra_flying_pending` and applied at the top of the next
`gm_player_tick`, which leaves the already-correct travel timing untouched
(first elytra travel is still the tick after the jump edge) and additionally
makes that first travel tick move with the 1.8F box, as vanilla does.

Result on scenario_elytra_dense_20260729T082313Z: t=58 19.85 -> 3.83/ch, and
the whole t=58..65 window is now 3.3-4.3/ch (was 19.85/3.4/3.3/3.6/3.7/3.8/
4.0/4.3). Physics still byte-clean over 310 ticks; the best whole-frame
row-shift at t=58 is now 0 rows, i.e. the camera is aligned. Unexplained gate
px 84183 -> 67249, worst cluster 42219 -> 15852. No regressions: elytra_dip 1
failed (t=60), water_dive 0, lava_walk 0, suffocate_camera 1 (t=0, 0 px).

Residual, NOT the entry: the same 10 frames still fail the cluster gate. What
is left in t=58..65 is waterfall SURFACE content - at t=58 magma paints the
lit top face of the y=22 water plateau (cells x=9..11, z=-4..4, meta 1/0/1
over meta 9 falling columns) across rows ~78..165 where the oracle has only
~78..95, i.e. magma's rendered surface sits lower/extends further at a grazing
view; at t=59..65 it is flow-texture streak placement inside the curtain. Both
are the same class as the never-failing 12-15/ch bands at t=50..57, present
before this fix, and belong to the mid-growth/flowing-water surface family
already filed for elytra_dip t=60 - not to the eye-in-fluid family.

### Eye-in-fluid overlay timing: CLOSED (root-caused 2026-07-29)

Found 2026-07-29 on the dense elytra tape
(`scenario_elytra_dense_20260729T082313Z`, frames every tick) via a
per-tick L/R mean-abs scan - the 10-tick gate summary never showed it:
- t=142..151: magma draws the full-screen lava submersion overlay/fog
  (~75/255 mean abs) while the oracle eye is still ABOVE the lava
  surface during the skim. Ten ticks of solid red on magma only.
- t=78: magma still applies underwater fog one tick after the oracle
  eye exits the water curtain (single-tick flicker, ~70/255).

Both filed suspects were wrong. Two independent causes, neither in the
`liquid_height_percent` boundary and neither a tick-phase problem:

**1. The lava band is PHYSICS, not overlay timing.** The tape's first
divergence is tick 141 `vy`: oracle `0.30000001192092896`, magma
`-0.10051` (`= -0.16102 * 0.5 - 0.02`, i.e. magma ran the lava branch
correctly but skipped the climb-out kick). `EntityLivingBase`
`moveEntityWithHeading`:2119 sets `motionY = 0.3` when
`isCollidedHorizontally && isOffsetPositionInLiquid(...)`;
`Entity.isOffsetPositionInLiquid`:651 is TRUE when the offset box is
FREE, and its collision half is `World.getCollisionBoxes`, which keeps a
candidate only if `Block.addCollisionBoxToList`:548 passes
`AxisAlignedBB.intersectsWith` (strict `<`, `AxisAlignedBB.java:341`).
`psv_offset_in_liquid` was calling `psv_collect_blocks` - a broadphase
CELL scan, inclusive on `floor(max)` and reaching one cell below
`floor(minY)` - with no intersects re-filter. The elytra pilot is pressed
against a wall at `x = 37`, so his box maxX is exactly `37.0`; the
broadphase returned the wall cell, the kick never fired, and instead of
popping out of the pool he sank and stayed eye-deep in lava for ten
ticks. Fixed by re-filtering with `mc_aabb_intersects`, exactly as
`psv_update_elytra_size` already documents having to do. Removing that
one line of slack also removes every downstream residual on the tape
(t>=152 went 4.4-5.0/ch to 0.7-1.2/ch) and the physics gate is clean.

**2. The viewpoint is not the eye.** `ActiveRenderInfo.projectViewFromEntity`
adds the static `position` vector, which `updateRenderInfo`:50 gets by
gluUnProject-ing the viewport centre at winZ 0 - the NEAR PLANE - through
the finished modelview. First person that modelview carries
`orientCamera`'s `translate(0,0,0.05)` (EntityRenderer:681) and the
projection is `gluPerspective(..., zNear = 0.05F, ...)` (EntityRenderer:730),
so the camera sits 0.05 ahead of the eye and the sampled point another
0.05 ahead of the camera:
`viewpoint = (x, y + eyeHeight, z) + 0.1 * getVectorForRotation(pitch, yaw)`.
At t=78 the eye is at x 11.98790 - still cell 11, water - but the oracle
viewpoint is 11.98790 + 0.09903 = cell 12, air. The remaining `position`
terms (view bobbing, hurt camera) are zero on these tapes:
`EntityPlayer.onLivingUpdate` zeroes `cameraYaw`'s target whenever
`!onGround`, and no tape frame is inside `hurtTime` at a fluid boundary.
They are NOT modelled; a ground-level tape that crosses a fluid surface
while walking would need them.
Consequence worth remembering: `ItemRenderer.renderOverlays` is gated on
`isInsideOfMaterial(WATER)` alone, off the entity's own eye with no
look-ahead, so the overlay texture and the fog/FOV can legitimately
disagree for a tick at a surface crossing. `gm_uw_eval` no longer nests
the overlay test inside `fluid == 1`.

Result on the dense tape: t=78 70.35 -> 0.80/ch, t=142..151 ~75 -> 0.7-1.3/ch,
no physics divergence at all, unexplained gate frames 178 -> 10 (the
survivors are the pre-existing t=58..65 waterfall-entry cluster and t=77,
unchanged by this work; the t=58..65 cluster was closed later - see the next
section).

### Magma's generated nether lava sea is FLOWING lava: FIXED (2026-07-29)

Was: `CPN_LAVA=10` / `CPN_FLOWING_LAVA=11` in `chunk_provider_nether.h`, and
`nf_to_vanilla` / `npm_cpn_to_vanilla` identity-mapped those wrong numbers.
Vanilla 1.11.2 is 10=`flowing_lava`, 11=`lava` still (`Block.java:2414-2415`);
ChunkProviderHell prepareHeights/buildSurfaces place `Blocks.LAVA` (still).
Sea cells were therefore id-10 flowing; a portal tape's DIM-1 snapshot patch
held 123,556 corrections of exactly this. nether_full golden was a C self-
capture so the gate never saw it.

Fix: enum + remappers to vanilla order (`CPN_FLOWING_LAVA=10`, `CPN_LAVA=11`);
verbatim Java golden constants for chunk_provider_nether; nether_full golden
regenerated (seed 7: 2523 cells 10->11 only; seed 49: 1132). Fortress
`FT_LAVA=10` stays (StructureNetherBridgePieces uses `Blocks.FLOWING_LAVA`).
