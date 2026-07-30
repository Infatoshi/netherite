# mc_capture - live Minecraft frame (rung-4 golden)

> Verification entry point: `magma/VERIFY.md`. This dir holds the legacy
> single-frame gates (`make rung4-verify` etc); day-to-day divergence hunting
> moved to `../trace/` (checkpoints + tape replay).

Captures ONE frame of the REAL Minecraft 1.11.2 client (Forge + Malmo/qrl mods,
llvmpipe software GL on Xvfb :1), at a fixed seed and camera pose, to serve as
the coarse whole-frame golden that magma's full seed->raster path is diffed
against.

## Run
    bash capture.sh          # capture the golden (needs the live game on :1)
    make -C ../../magma rung4-verify        # single-shot magma vs golden
    make -C ../../magma hard-scene-verify   # hard leaf-canopy scene + ablations
Outputs (this dir): `mc_frame.png` (854x480 window content), `pose.json`,
`magma_frame.png` (magma's render of the same scene/pose, written by
`run_rung4.sh`), `hard_scene_magma.png` (baseline from hard-scene).

### Hard scene verifier
See `hard_scene.json` + `run_hard_scene.sh`. Full-stack integration golden on the
seed-0 elevated forest pose (the known crop~27 case). Ablates `MAGMA_SMOOTH` /
`MAGMA_FOG` / `MAGMA_GAMMA` and prints a scoreboard. Report lands in
`/tmp/hard_scene_seed0/` (override with `HARD_SCENE_OUT=`).

## Pose (MATCHED to magma's rung-3/rung-4 ChunkScene camera)
Both sides render the seed-0 3x3-chunk-around-origin scene from the SAME camera:
world eye ~(8.3, 95, 40), FOV 70, 854x480. magma uses yaw 0 / pitch -35deg
(forward toward -Z, tilted down); the MC convention that reproduces that forward
is **yaw 180 / pitch +35** (MC yaw 180 faces -Z, POSITIVE pitch looks down). The
resolved pose is PRINTED by `rung4_candidate` (its `POSE` line) and hardcoded into
`capture.sh` (`POS_X/POS_EYE_Y/POS_Z/YAW/PITCH`); they are deterministic for
seed 0 + the current mesher. `capture.sh` puts MC into **spectator** so the aerial
camera holds altitude (no gravity, and no hand/hotbar drawn - closer to magma),
and teleports feet to `eye - 1.62` so the MC eye lands at y=95.

## What it does
1. Kills any running game; wipes `saves/qrl_<SEED>` so the world regenerates from
   clean worldgen (a stale save can carry contamination from a prior run).
2. Launches the headless stack + game via `../../java/start_vnc_client.sh`.
3. Waits until the qrl bridge actually ACCEPTS a TCP connection on 127.0.0.1:25575
   (a real socket probe - do NOT grep runclient.log for "listening", a stale line
   from a previous game survives the relaunch and races ahead of the new boot).
4. `reset(seed)`, silences command feedback, forces frozen clear noon
   (`time 6000`, `weather clear`, daylight/weather cycles off), teleports to the
   fixed pose, and ticks chunks in (no `fluid()` flood - that would add water not
   present in magma's world).
5. Grabs the MC window content region with `ffmpeg x11grab -draw_mouse 0` (no
   title bar, no cursor), writes `mc_frame.png` + `pose.json`, stops the game.

## Pose (in capture.sh)
SEED=0, YAW=-135, PITCH=20, FOV=70 (MC "Normal" default). Position = seed-0 spawn
column, +2y. See `pose.json` for the ACTUAL read-back pose.

## Caveats for matching magma
- This is a COARSE whole-frame tolerance target, NOT the tight OSMesa noise floor
  of `../verify/chunk_*` (rung 3). Irreducible differences: MC draws a real sky
  gradient + clouds (magma fills a flat sky-blue), full smooth per-vertex lighting
  (magma folds MC's directional per-FACE shade, flatter), a runtime-stitched atlas
  (magma uses its own stitched 128x128 atlas), and the FULL view-distance terrain
  where magma only meshes the 3x3-chunk island around origin (sky-void beyond ~48
  blocks). So expect a large whole-frame mean; the terrain crop is the honest number.
- Pose framing: RESOLVED. The camera is the elevated/angled rung-3 ChunkScene camera
  over open terrain around origin, not the buried seed-0 spawn column. See the Pose
  section above.

## Container-GUI pixel gate (capture_gui.sh + run_gui_verify.sh)

`capture_gui.sh` captures every container screen magma implements: crafting table,
furnace, and player inventory. The audit source is `gm_screen_kind_for_gui` in
`game/screen.c`; there are no additional magma screen kinds. Chest, dispenser /
dropper, hopper, enchanting, brewing, anvil, villager, creative, beacon, horse, and
shulker screens are not implemented and are intentionally not captured. The live
goldens use a fresh seed-0 world, EMPTY survival
inventory, frozen clear noon, a pinned stone platform with a crafting table and
furnace, GUI scale 2 at 854x480. Each screen is grabbed twice (`mc_gui_<name>_{a,b}.png`)
so the verifier measures the Java-vs-Java repeat noise floor (0.000 - the screens are
fully static). Headless input gotcha: synthetic/XTEST MOUSE clicks and the qrl
use-keybind do NOT reach `rightClickMouse` under Xvfb, but keyboard XTEST does; the
script temporarily rebinds `key.use` to R in options.txt (restored on exit) and opens
the block GUIs with `xdotool key r`, the player screen with E.

`run_gui_verify.sh` renders inventory, table, furnace, and chest through the REAL
`gm_screen_draw` path (`gui_candidate.c`, zeroed runtime = empty everything) and
pixel-diffs the panel region (inset 4px per side: translucent rounded corners over
the live 3D scene are dropped). Table / furnace / chest / inventory non-preview
chrome are **bit-exact** gates (near-zero A/B noise prerequisite; no margin).
Inventory player-preview ROI is a hard **open** gate under `pin_preview_anim`:
PASS only if bit-exact; any residual is FAIL (no PASS-FLOOR budget). Pose2 goldens
come only from `capture_gui.sh` (held-out); `capture_gui_actions.sh` must not
overwrite them.

Panel origin gotcha: vanilla GuiContainer centers in GUI units with INTEGER division
- at 854x480/scale2 that is floor((427-176)/2)=125 gui -> fb x 250. Naive framebuffer
centering lands at 251 and shows up as vertical 1px lines on every slot border.

### Inventory action sequence

`capture_gui_actions.sh` acquires `/tmp/qrl_25575.lock`, reuses or boots the Xvfb
`:1` oracle, installs a deterministic loadout, and drives GUI input through
`java/mcwindow_script.py`. The script protocol now exposes the relay's existing
absolute `ma` cursor event as `"cursor":[x,y]`; no Java/mod protocol addition was
needed. The fixed sequence is: pick A, place B, right-click split B, right-click
deposit one in C, shift-click B to hotbar 0, swap hotbar 0/1 with three vanilla
PICKUP clicks, press Q over hotbar 0, then close. It writes
`mc_gui_action_00_initial.png` through `mc_gui_action_08_close.png` and
`gui_actions_scene.json`.

`run_gui_actions_verify.sh` applies the same logical operations through
`gm_container_click`, renders `gm_screen_draw` after every visible step, and prints
a per-step table. The pixel gate is a hard owned-pixel contract (no mean+margin
budget):

- **Owned pixels**: full inventory panel (inset 4 gui px) including every slot
  cell (armor, craft 2x2, result, offhand, main 27, hotbar 9), hover chrome,
  tooltips, and carried stack. The live 3D player-preview viewport is the only
  panel hole (preview is gated in `run_gui_verify.sh`).
- **Noise floor**: Java A/B from `mc_gui_inventory_{a,b}.png` on the same owned
  mask. When A/B noise is zero, one wrong owned pixel fails (`max` channel +
  `hard_px` with thr 10). Residuals are reopened rather than absorbed.
- **OS cursor non-claim**: neither the Java FBO golden nor `gm_screen_draw`
  includes the OS pointer. No 12x12 hole is punched over game pixels.
- **Mutation self-tests** (non-vacuous): each case builds a perfect passing base
  (genuine PASS magma step or Java oracle copy), asserts unmutated control
  PASSes, applies the corruption, asserts mutated FAILs, and requires
  meaningful painted counts. Covers one pixel, hundreds outside the old five
  ROIs, blank armor/offhand, missing held stack, cursor-center corruption.
- **`08_close`**: state-only (`focusdiag` `screen=None` + capture present). After
  close there is no inventory panel to pixel-claim.
