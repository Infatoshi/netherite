# Open divergences vs the real game

Every entry: what diverges, the measured magnitude, and a one-command repro.
Protocol in VERIFY.md. Add at the bottom, delete when fixed (git history is
the archive). Repros assume the game is live on :0 with the qrl bridge up.

## 1. Tick 9811: oracle world has flowing water magma's pristine worldgen lacks (save-state provenance)

The 12k human tape is BIT-CLEAN at 1e-9 for 9,811 ticks after the 2026-07-10
physics-fidelity fixes (strafe sign; slipperiness solidity gate; in-water box +
vanilla's vacuous height test; water-edge hop inversion; flowing-water 0.014
push implemented). Root cause of t9811, pinned by delta fingerprinting: the
oracle received a push of 0.014*unit(sum of per-cell getFlow = unit(1,-2) +
(0,-1)) from TWO flowing-water cells at (41, 64-65, 238) that magma does not
have - MAGMA_DUMP at t9811 shows its live pond == pristine worldgen (all
meta-0 sources, nothing at y>=64 there; the fluid CA is mark-driven and was
never poked). Magma's worldgen block extent is verified against the real
game, so the extra oracle water is EVOLVED SAVE STATE: the session ran on the
persistent qrl_0 save whose spring/pond water had flowed in prior sessions or
in-session, while tape replay regenerates pristine worldgen from the seed.
The diverged trajectory later kills the magma player by fall damage
(~t 11040), which the replay reports loudly instead of aborting.

Fix direction (pick one): (a) record physics tapes only on a FRESH world
(delete the save before the session - first-session worldgen == magma
worldgen), or (b) snapshot the oracle's loaded-region block/meta state at
recstart and seed magma's world from it. (a) is free and is now the VERIFY.md
rule; (b) is what full save-replay eventually needs.

Repro: `cd raster/verify/trace && uv run --no-project --with numpy --with pillow python replay_tape.py /tmp/play_tape.jsonl` -> FIRST DIVERGENCE tick 9811 field vx. `MAGMA_DUMP="9811,38,44,62,66,236,241" ./magma_game --headless ... --script out/tape_play_tape/magma_script.jsonl` dumps magma's live cells; compare against the live game's (41,64-65,238).

## 2. CLOSED (no bug): first-tick walk acceleration was the bridge alignment artifact

The 12k HUMAN tape contains 5 walk-from-rest events inside its 1e-9-clean
region (player on ground, |v|<1e-6, no movement input, then a movement key:
t=3870 s, t=3890 f=-1, t=9600 s, t=9620 s, t=9714 f=+1). All five replay
bit-clean at 1e-9 (first divergence is t9811, a different class - see #1).
Oracle post-tick first speed at these events is 0.0535 blocks/tick and
magma matches it exactly, so the old "oracle 0.098 vs magma 0.0196"
bridge-stepped measurement was purely the one-tick input-alignment artifact
documented in VERIFY.md. No magma change needed.

Evidence repro: `cd raster/verify/trace && uv run --no-project --with numpy
--with pillow python replay_tape.py /tmp/play_tape.jsonl` (clean through
t9810) + scan the tape for the rest-to-move ticks above.

## 3. FIXED (af19a36): night lightmap darkening (was 48.6/ch)

Terrain baked the NOON lightmap scalar into vertices at mesh time, so time of
day never touched terrain brightness. Fixed by the shade-time lightmap:
vertices carry raw lightmap coords (sky/blk levels), and the frame's 16x16
updateLightmap texture - rebuilt per frame from getSunBrightnessBody(wt) - is
sampled bilinearly per fragment (GL semantics; fluids use the
BlockLiquid.getPackedLightmapCoords max-of-cell-and-above rule).
night_spawn terrain 48.61 -> 3.76/ch; day scenes unchanged-or-better.

Same commit closed the 12k-replay "pixel rows climb 3.42 -> 25.79/ch" lead:
the trace profile records with doDaylightCycle=false (oracle frozen at the
header's world_time) while magma's clock free-ran into dusk. magma now
honours `--daylight off` (replay_tape.py passes it); rows are flat over the
whole tape.

Residual: hand/entity emitters still bake noon light (per-frame emitters,
cheap to fix by tracking L->sun_brightness); revisit if the HUD/hand region
shows up in night tapes.

## 4. MOSTLY FIXED (fd847f3 + 713f664 + bd02472 + e222c91): the "oblique texel phase" at the 12k-tape poses was missing HUD + missing grass_side_overlay

Re-diagnosis (2026-07-12): decomposing the two static 12k-tape poses
(A t0-3880 3.42/ch, B t3900-9600 9.01/ch) into connected diff clusters showed
the big "texel phase" clusters were NOT a rasterization phase problem:

- Missing HUD (largest class, both poses): gm_script_run never called
  gm_hud_init, so every headless frame was HUD-less vs an oracle frame with
  hearts/hunger/hotbar/crosshair. Fixed + HUD layout pinned to vanilla
  GuiIngame/GuiIngameForge coords (hotbar sh-22, stats row sh-39, XP sh-29,
  hunger x -9-i*8, crosshair (w/2-7,h/2-7) with the vanilla
  ONE_MINUS_DST_COLOR/ONE_MINUS_SRC_COLOR invert blend).
- Missing grass_side_overlay (the "phase" clusters on grass walls):
  vanilla block/grass.json draws a SECOND biome-tinted overlay quad on every
  grass side face; magma drew only the untinted grass_side base (baked-in
  bright yellow-green strip). Per-texel arithmetic pinned it: predicted mc/cr
  ratio overlay_texel*EH_tint/base_texel = .690/.587/1.074 vs observed
  .725/.585/.979; dirt texels bit-exact. Fixed: overlay quads emitted into
  CUTOUT_MIPPED, coplanar z resolved by the new CrShadeCtx.depth_lequal
  (GL_LEQUAL, MC's depth func) in both rasterizers; CPU==CUDA bit-exact.
- Latent: CUTOUT_MIPPED sampled magma's mip chain while both oracle
  profiles pin mipmapLevels:0 (plain GL_NEAREST). Now mips-off. Measured
  no-op at these poses and at the canopy pose (leaves there magnify, LOD<=0);
  it only diverged on minified/distant foliage.

After (12k tape): pose A 3.42 -> 1.13/ch, pose B 9.01 -> 2.54/ch
(terrain 4.61 -> 2.34). Remaining clusters >=50px at the poses, ranked:

1. FIRST-PERSON ARM (A: all 5 remaining clusters; B: ~1.0/ch of the 2.54):
   magma's gm_hand_draw arm has visibly different silhouette/pose vs
   vanilla ItemRenderer.renderArmFirstPerson. Biggest single residual now.
2. Selection box: the oracle draws the targeted-block wireframe; magma's
   MAGMA_OVERLAY box does NOT match it (enabling it made pose B WORSE,
   2.54 -> 3.05/ch) - the box needs a vanilla-fidelity port
   (drawSelectionBox: black 40% alpha lines, grow 0.002) before it can
   default on in replays.
3. Thin (1-2 px) silhouette-edge lines at grazing block boundaries and
   distant-water speckle - per-pixel class, not texel-block flips.

forest_canopy could NOT be re-measured at full res this session: the oracle
checkpoint captures (out/checkpoints/mc_*.png + oracle_obs.json) were wiped,
and re-capturing requires teleport pose-pinning on the live bridge (a human
may be playing). Indicative only: vs the committed DOWNSCALED sbs evidence at
the settled pose (-102.8,73,83.0), magma went 20.2 -> 19.3/ch, i.e. the
canopy residual (leaf undersides/trunk at extreme grazing) is still open as
the true remaining texel-phase question. Next checkpoints re-run (when the
bridge is free) re-pins it; the row-240 scanline probe in git history is the
tight loop.

Secondary, filed (unchanged): under-canopy saved SKYLIGHT genuinely diverges -
2935/14007 cells (qrl `sample_light` vs MAGMA_DUMP_LIGHT over
(-112..-92, 60..88, 72..94)), java DARKER by up to 14 at y 60-72, and java
equals magma's pure column ladder in 2899 of them. Vanilla's end state is
stale by construction: generateSkylightMap ladder + relightBlock stomps,
repaired only where a checkLightFor BFS was seeded (updateSkylightNeighborHeight
gated isAreaLoaded(16), Chunk.recheckGaps gated 33x33); magma floods
everywhere (compute_skylight_spread). But this is PIXEL-NEUTRAL: with the
in-binary A/B gate verified live (MAGMA_NO_SKYSPREAD=1 changes 17k cells),
checkpoint terrain diffs move only 20.98->21.19 (canopy) and <0.3 elsewhere.
Fix (if ever needed for bit-parity): port the literal vanilla stale pipeline
behind a mode flag, verbatim or not at all (worldgen context-divergence
playbook). Low priority given pixel neutrality.

Repro: replay_tape.py on /tmp/play_tape.jsonl (poses A/B print at t1000/t6200);
cluster decomposition = scipy.ndimage.label on |mc-magma|>16 per pose frame.

## 5. FIXED: seed 20260710 decoration divergence was Swampland M (id 134) using default decorator config

Root cause (genprobe flywheel, 2026-07-11 night): fresh structures-off ground
truth + java genprobe log showed 19/590 chunks first-diverging at DEC CLAY, all
in one cluster - a Mutated Swampland (biome id 134) region. Vanilla registers
134 as another `new BiomeSwamp(...)` (same decorator counts, swamp tree, blue
orchid, fossil gate), but magma's decorate dispatch only matched id 6, so 134
fell through to the base-Biome default config (sandPerChunk2 3 vs swamp's 0,
wrong tree/reed/lily counts) and the default oak tree picker. Fixed by treating
134 as swamp at every decorate site (pls_bd_cfg2, pls_gen_tree,
pls_biome_decorate_full fossil gate, flower pickers, bd_genTree).
After: regression_suite 20260710 = 590/590 chunks RNG-clean, block match
99.909% -> 99.9732% (residual is the known CA-timing fluid class).

Audit note (same bug class, unfixed/unverified): id 156 (Mutated Birch Forest
Hills = BiomeForestMutated) is missing from pls_bd_cfg2 (only 27/28/155 listed)
and pls_gen_tree (case 155 only); no verified seed contains 156 yet - add it
alongside 155 when one does.

Repro: `bash trace/regression_suite.sh 20260710` (needs
trace/out/java_genprobe_seed20260710.log + structures-off save
java/Minecraft/run/saves/qrl_20260710; both regenerated this session via the
qrl_genprobe.txt sidecar + sit-at-spawn protocol). NOTE: rebuild
trace/world_dump by hand after header edits (no make target; mirror the
magma_game link line) - a stale binary reproduces the OLD divergence.

## 6. Bridge-DRIVEN tapes: oracle motion zeroed on the tick after landing (harness artifact)

Tapes recorded while a qrl bridge script drives the player (not a human)
diverge deterministically on the first onGround tick after a fall: the oracle
enters that tick with motionX/Z == 0 while magma (and vanilla formulas)
carry the landing motion. Fingerprint on the fresh seed-0 tape
(20260711T050916Z, t148): oracle displacement exactly 0.098000 = fresh accel
from zero (post vz 0.053508 = 0.098*0.546); magma displacement 0.261807 =
0.163807 carry + 0.098 accel. Same |d|=0.1638 signature on both driven tapes
at the same spot. The 12k HUMAN tape validated thousands of landings
bit-exact, so the zeroing is specific to bridge-stepped play - consistent
with a server position sync (SPacketPlayerPosLook zeroes motion components;
"Player0 moved wrongly!" logged 04:46Z/04:52Z during driven sessions).
Physics ground truth stays HUMAN tapes; driven tapes are pipeline smoke only.

Repro: the driven tapes (20260711T050916Z and its twin) were deleted in the
2026-07-11 tape cleanup - only the canonical 12k HUMAN tape survives (see
VERIFY.md "canonical tape"). To reproduce, record a fresh bridge-driven tape
with a fall in it and replay it: first onGround tick after the fall diverges
on x/z with the 0.1638 carry signature.

## 7. FIXED: live-play frame captures are now tick-boundary (was +-1 tick while moving)

recordTick now re-renders the world into the framebuffer with
renderWorld(partialTicks=1.0F) right before the screenshot grab (Phase.END
client tick, GL context live, works under llvmpipe headless), so the captured
frame is the exact post-tick camera - no interpolation. Frames carry a
"tb":1 marker in the tape JSONL; if the render throws, it falls back to the
old interpolated grab (no marker).

Smoke (fresh seed-0 driven tape, 275 ticks, physics bit-clean at 1e-9 the
whole way, frames_every=5): walking frames now diff 5.8-6.5/ch vs 5.7/ch
static at the same scene - the old ~17/ch moving-camera inflation is gone.
Cross-check: magma frames rendered at every tick around a moving capture
show no better-matching neighbor tick (the pairing is exact, not off-by-one).

Follow-up lead (separate class, not motion): the smoke's END pose
(37.5,67,182.7 - a low hollow below spawn) diffs ~16/ch static, worst blocks
are horizon cells where the oracle shows sky (136,176,250) and magma draws
distant terrain/foliage (66,86,55) - distant-terrain/fog cut at low-y
vantages. File properly if a tape gate ever keys on such a scene.

Repro: record any driven tape (rec frames grab tb automatically), replay via
replay_tape.py; per-frame whole/terrain lines print alongside the physics
diff.

## 8. Populate-order-sensitive decoration: live-session worldgen can differ per tree (worldpatch re-anchor)

Fresh-world tape 20260712T055346Z, t1798: the oracle world has a BIG oak
(WorldGenBigTree, branch logs 17/4|17/8, trunk to y=80) at (25,~70..80,128),
chunk (1,8); magma's replay worldgen grows a height-4 small oak whose
eye-level canopy at x=27 clamps the player the oracle walks past (x wall at
28.3). Ground truth pinned from the session save
(java/Minecraft/run/saves/qrl_0 region NBT) and the t1800 golden frame (bare
tall trunk in view). A world_dump of an island 5x5 box grows a THIRD shape
(height-5 oak), confirming the site is population-context/cascade-sensitive:
per-chunk decoration RNG is deterministic, but vanilla's cross-chunk cascade
corruption depends on chunk generation ORDER, and the live session's order
(spawn spiral + human path) is neither recorded in the tape nor reproducible
by magma's lazy player-path chunk gen. Same family as #1 (world provenance),
but present even on a FRESH world.

Re-anchor (implemented): replay_tape.py splices an optional sidecar
`<tape>.worldpatch.jsonl` (set_block events, tick 1 - tick 0 is overwritten by
worldgen) into the script. The sidecar for this tape carries the 716-cell
log/leaves diff of the tree box x[20,32] y[68,84] z[121,135], values from the
session save with the two logs the tape itself chops (25,70..71,128) restored
to 17/0. Non-tree plant diffs in the box (in-tape instant tallgrass punches,
one dplant meta nibble) are left alone. With the patch the replay runs
t1798 -> t2291.

Real fix direction: record the oracle's chunk-populate order (or a recstart
world snapshot, #1 fix (b)) into the tape and drive magma's worldgen with
it; until then each such site is one sidecar entry, built by diffing
MAGMA_DUMP against the session save region NBT.

Repro: remove the sidecar
raster/verify/tapes/20260712T055346Z_..._77b5b462.jsonl.worldpatch.jsonl and
replay -> FIRST DIVERGENCE t1798 x (magma clamps at 28.3 on phantom leaves);
restore it -> t2291.

## 9. Unrecorded GUI interactions: inventory provenance (worldpatch set_inventory re-anchor)

Fresh-world tape 20260712T055346Z, t2291/t2495: the qrl tape records only the
discrete action set (move/look/atk/use/hotbar); inventory-GUI and
container-GUI *clicks* are NOT taped. In the session the human crafted (2x2
grid: logs -> planks -> crafting table) and later used the placed table -
none of it visible to the replay, so magma's slot 6 stayed empty while the
oracle's held a crafting table (placed at (14,65,126) on the t2275 use press;
block id 58 confirmed in the session save region NBT) and then a wooden
shovel (visible in the t2480/t2500 golden frames; the shovel's 2.0 dig speed
on dirt is what let the oracle out-dig magma's bare hand, clamping magma
one block behind in the tunnel at t2495).

Re-anchor (implemented): the worldpatch sidecar now splices entries at their
own "tick" field (min 1), and accepts set_inventory as well as set_block.
This tape's sidecar seeds slot 6 = crafting table x1 at t2274 (one tick
before the use press, so magma's OWN rightClickMouse/place path runs) and
slot 6 = wooden shovel x1 at t2320 (after the oracle's table-GUI session, so
magma's own dig-speed/wear model runs). With both, plus the shovel
dig-speed fix in items_tools_armor.h, the tape replays clean end-to-end.

**Recorder half done:** per-tick `"gui"` (GuiScreen simple name) + `"gmx"`/`"gmy"`
(ScaledResolution mouse) when a screen is open; goldens include the open GUI.

**Replay wiring done:** `replay_tape.py` emits render-only `gui_view` events;
`script.c` maps GuiInventory/GuiCrafting/GuiFurnace -> container 0/1/2 (others
logged once + skipped); `frame_capture.c` draws `gm_screen_draw` after the HUD
with scaled-res mouse converted by `gm_screen_mouse_to_fb`. Canonical tape
20260712T055346Z has no gui fields (pre-recorder) so is byte-identical with
or without this wiring. Inventory *truth* still comes from worldpatch
`set_inventory` - gui_view only paints the panel from current runtime slots.

**Remaining:** GUI *clicks* still unrecorded (no Container.slotClick stream);
craft/furnace slot mutations during a GUI session still need set_inventory
re-anchors (or a future click/inventory-snapshot tape field).

Real fix direction: tape the container/GUI click stream (qrl recstart hook on
Container.slotClick) or snapshot inventory per tick; until then each crafted
item is one sidecar set_inventory entry, inferred from save NBT
(placed-block ids), golden frames (held-item sprite), and material budget.

Repro: drop the two set_inventory lines from
raster/verify/tapes/20260712T055346Z_..._77b5b462.jsonl.worldpatch.jsonl ->
FIRST DIVERGENCE t2291 x (magma misses the placed table wall); restore ->
clean. Headless smoke: `gui_view` + GuiCrafting in test_script.sh dumps a
crafting panel PPM (see docs/archive/GROK_REPORT.md).

## 10-16: frame-fidelity sweep of the fresh canonical tape (2026-07-12, human review + 157-pair triage)

Physics replays bit-clean on 20260712T055346Z_..._77b5b462.jsonl; every entry
below is RENDER-side. Repro for all: `replay_tape.py <tape> --report`, then
compare oracle keyframe `<tape>_frames/f_%06d.png` (tick i*20) against magma
frame i in magma_frames.npy. Frame indices below are that pairing.

## 10. MOSTLY FIXED: entities now render in tape replay (renderable ghost views)
Was: the renderer only drew live-sim entities; tape replay fed recorded
oracle entities in as physics-only ghost boxes (ent_box), so NOTHING
rendered. Now every recorded ent also becomes a render-only `ent_view`
script event (replay_tape.py): script.c maps the tape type string to a
model id (gm_entity_type_for_name, entity_render.c), stores it as a
per-tick ghost view on GmRuntime, and frame capture draws it through the
existing gm_entities_emit mob-model path. ent_box pusher semantics
untouched; physics still bit-clean over all 3121 ticks.
Sheep-flock frames (whole-frame mean/ch): f76 38.92 -> 24.95,
f77 24.17 -> 20.44, f78 64.16 -> 16.09, f81 17.66 -> 16.25,
f83 15.87 -> 13.75, f84 18.04 -> 17.20; others in 76-88 unchanged
(recorded ents off-screen there). "Zombie at frame 38" was wrong: that
zombie is at y=23 underground and not visible in the oracle frame either.

Grind follow-up (`2321c33` + round-2 lightmap): (a) limbSwing from per-tick
position deltas; (b) hurtTime red flash; (c) ModelSquid; (d) ent_view id;
(e) entity pass samples the frame lightmap (sky=15/blk=0, face shade on ao)
so outdoor wool is not fullbright. t1520 24.97->24.81/ch. Residuals: head
pitch while grazing is neither in tape nor model; face/leg bare-skin detail
vs fur still soft; head yaw = body yaw; witch/bat no model; sheared/dyed
wool not recorded.

Mob-texture audit vs jar (2026-07-13): every skin the renderer binds is now
byte-identical to the jar (tests/check_mob_atlas.py, wired into
`make test-game`). Skin-variant types no longer wear their base mob's skin:
pigman, husk, stray, cave spider, mooshroom get their own sprite via
GmEntityView.skin (gm_entity_skin_for_name). Entities are no longer
fullbright: frame capture samples world sky/block light at the vanilla eye
height (gm_entity_eye_y) and either feeds the lightmap LUT (Overworld) or
folds the exact updateLightmap color into the tint (Nether/End, lm==NULL) -
this is what made pigmen glow white/cyan in the dark Nether. Evidence-tape
presentation-window mean 8.30 -> 8.05/ch.

Coverage pass 2 (2026-07-13): bipedHeadwear overlay box added to ModelZombie
(u32,v0 delta+0.5, copies head rotation) - the pigman's pink face lives ONLY
on that hat layer; without it the head showed the base decayed-skull texels
on the whole face. Held items via the vanilla LayerHeldItem chain
(postRenderArm -> rot -90X/180Y -> hand offset -> item json
thirdperson_righthand -> recentre): pigman gold sword, skeleton/stray bow
(gm_held_items_emit, item atlas pass). Own models transcribed from the
oracle for witch (14 boxes, scale .9375), bat (9, scale .35, flying pose +
age-driven flap), llama (9, creamy), ghast (10, scale 4.5, Random(1660)
tentacle lengths, age-driven wave), magma cube (9, size 2), minecart (6,
+0.375 block cart offset). Emit clamps face UVs to the sprite rect (vanilla
GL_REPEAT wrap on oversized nets would bleed across the packed atlas).
Residuals: zombie villager wears the plain zombie skin (villager-head
texture layout, needs its own model); llama variant + magma cube size not
recorded in tapes (creamy / size 2 assumed); witch head/hat kept static
(nested pivots flattened); entity light sampled at one block vs vanilla's
interpolated brightness, so mobs can sit one brightness step off at light
borders.

## 11. MOSTLY FIXED: underwater view rendered as air (no fog/tint/overlay)
Fully submerged keyframes 50-54 (ticks 1000-1080) dropped 41.9-46.6/ch ->
7.1-7.9/ch; every non-submerged frame byte-identical; physics replay stays
bit-clean (render-only). What was built (game/underwater.{c,h} + hooks in
frame_capture/game_main/sky/shade, vanilla constants verbatim):
- Eye-in-fluid test: ActiveRenderInfo.getBlockStateAtEntityViewpoint (liquid
  surface at y+1 - (getLiquidHeightPercent(level) - 1/9), else block above).
- Terrain fog: CrShadeCtx.fog_exp_density - GL_EXP exp(-0.1*dist) (setupFog
  water branch; lava 2.0) replacing the linear ramp on all 4 layers; color =
  updateFogColor water branch (0.02, 0.02, 0.2) * fogColor1, where fogColor1
  is updateRenderer's light-at-feet smoother (0.1/tick, RD8 floor 0.25),
  stepped every tick in GmFrameCapture and seeded at its steady state.
- Sky pass: GmSkyCtx.uw EXP-fogs the sky plane (16/dir.y) toward the water
  fog and drops sunset/stars/sun/moon (dist >= 100 -> e^-10); clearColor =
  fog color. CPU + CUDA (host-built ctx).
- FOV: getFOVModifier eye-in-water 60/70 on the frame camera.
- Overlay: ItemRenderer.renderWaterOverlayTexture - misc/underwater.png
  (assets/underwater_tex.h) full-screen quad at view z=-0.5, UV 4x4 tiles
  shifted (-yaw/64, pitch/64), color(getBrightness x3, 0.5), src-over,
  gated on ForgeHooks.isInsideOfMaterial (eyes < y+1+filled).
The from-inside water surface was already emitted (reversed top quad).

Grind follow-up (`6d263d2` + `ef4963d` + round-2 fogColor1): water_still
animates from total_time; entity EXP water fog; **fogColor1 (updateFogColor
f13) multiplies sky/terrain fog** so post-dive surface-swim sky matches
(f055 31.5->18.0, f057 28.1->15.8, f049 21.9->8.1/ch; both_sky ~3/ch). Not
#12 rain. Residuals on ocean frames: horizon silhouette / distant terrain
worldpatch gaps + HUD floor; first-person arm not water-fogged.

## 12. Rain/overcast event not modeled (tape rain window t1800-2100 only)
Oracle ticks ~1800-2100 (frames 90-105): sky/fog go gray, light dims (rain at
wt~2500-2800 midday, despite doWeatherCycle false in fast.yaml - rain state
predates the freeze or is server-side). Magma stays bright blue. Recorder
gap: older tapes have no raining/rainStrength field (recorder now has a
follow-up commit for per-tick rain/thunder). **Not** the f055-f061 ocean
class: those are clear weather (rainingStrength ramp 0.01/tick would need
~t1700 for full rain at t1800; oracle sky at t0/t1620 is full blue and
matches magma; t1140 both_sky after fogColor1 fix is 3/ch).

**Round-3 quantitative gate for pre-rain frames:** oracle sky brightness
t1620=189, t1740=201, t1760=203, t1780=200, t1800=183, t1820=142, t1880=133.
f088/f089 stay full blue (both_sky |d|~2/ch) - file under terrain silhouette
(#19), not this weather class. Rain work still needs a weather-taped recording.

## 13. FIXED: first-person held item (was always bare arm, no swing)
Ported ItemRenderer non-empty path into game/hand.c (transformSideFirstPerson +
transformFirstPerson + firstperson_righthand block/generated transforms +
generated 1/16 plate). Empty hand keeps the vanilla arm path. Canonical tape
physics still clean; held-tool frames 123/140/146/156 whole means
31.37/47.83/36.55/38.51 -> 11.41/14.06/6.05/6.18 /ch. Residual: plate edges
are full-border not per-texel extrusion; equip bob skipped; HUD-vs-no-HUD
capture class. Evidence: docs/archive/GROK_REPORT.md + out_helditem_f146.png.

## 14. MOSTLY FIXED (`ef4963d` + round-2 multiply): crack + selection default ON; particles still missing
(a)+(b) headless/replay frames emit selection ribbons + destroy_stage_N crack
by default (opt out MAGMA_NO_OVERLAY / MAGMA_NO_CRACK). Selection is
blend=1 SRC_ALPHA. Crack is blend=2 DST_COLOR/SRC_COLOR (2*src*dst) with white
vertex colour and cutout alpha on destroy_stage strokes; hit-face only when
dig target matches raycast. Dig t2860 22.24->21.60/ch. (c) block hit/break
particles still absent (oracle 123, 129) - dominate residual dig look.
(d) FIXED (tape 20260721T215812Z dig window): frame_capture mapped the crack
face by comparing the raycast adjacent cell's sign (`ax<0`) instead of the
cell delta `ax-hx`, pinning every crack to the +x face - top-face digs never
showed cracks at all. Now face = delta of (a-h) cell coords.

## 15. FIXED: Block appearance mapping bugs: crafting table = stone, flower species wrong
(a) placed crafting table (id 58) fell through `gm_state_to_model_key` to
`GM_MODEL_FALLBACK` -> stone; fixed with model key 223 + multi-face sprites.
(b) red-flower metas 2..8 collapsed to poppy in `bm_block()`; now each
meta maps to its EnumFlowerType sprite. Pixel means (tape 20260712...77b5b462):
frame 114 t2280 18.57->10.46/ch, frame 115 t2300 29.19->8.87/ch; daisy frames
slight whole-mean drop + whiteish pixel count up (oxeye). Physics clean.

## 16. FIXED (grok/icons): item icon fidelity - hotbar iso cubes + extruded drops
(a) HUD block items now software-rasterize vanilla GUI isometric mini-cubes
(gui display rot 30/225/0, scale 0.625, per-face shade UP/NS/EW); flat 2D
items keep gui_atlas sprites. (b) dropped items: GROUND scale 0.25/0.5,
bob sin(age/10+hover)*0.1+0.1, spin age/20+hover, flat items get ItemModelGenerator
1/16 extrusion (thin box, 36 verts). Evidence: docs/archive/GROK_REPORT.md + out_icons_div16/.

Note (capture artifact, FIXED in recorder): older tapes' oracle frames have NO
GUI; post-fix tapes mark "hud":1.

## 17. FIXED (`edf4cc8`): beach hillside solid provenance on tape 77b5b462
Swim-path frames t840/860/880 showed oracle grass/dirt steps vs magma sand
(oracle-green & magma-sand tens of thousands of pixels). Session save has
grass/dirt/stone at y>=63 where pristine worldgen places sand/gravel
(populate-order / cascade family of #8). Worldpatch adds 601 solid-only
set_block cells from the save (tree + inventory re-anchors preserved).
Physics bit-clean 3121 ticks. Means: t840 28.31->13.52, t860 33.24->10.70,
t880 30.02->10.72 /ch. Residual: full terrain heightmaps still diverge in
places worldpatch does not cover; true fix is recstart world snapshot (#1b).

## 18. f068 t1360 terrain silhouette (NOT solid worldgen; re-examined round 3)

Looking into hillside foliage at pitch 15: oracle has ~5k sky-blue pixels in
the top band; magma has zero - solid foliage silhouettes. both_sky empty;
worst blocks at y=0. **Round-3 skeptic check vs qrl_0 save + world_dump:**
solid-only (beach method) diffs at y>=63 in the f068 box = **0**; leaf/plant
occupancy diffs = **0**; sand-ish solid diffs at y60-95 = **6** only (below
the silhouette band). fancyGraphics=false => leaves are opaque SOLID (no
alpha sky holes). So this is **not** beach-style solid provenance and not
missing leaves in the save-vs-dump sense. Residual is render-side skyline
(tint/cull/fog/edge) or tape-time vs end-save cascade outside the dump path.
Non-hud ~24.5/ch of 29.2 whole. True fix still recstart world snapshot (#1b)
or a targeted render-path diff. Repro: grind triptych f068_t1360.

## 19. f040 t800 / f088 t1760 terrain silhouette (NOT pre-rain)

**f088 t1760 weather claim REFUTED (round 3):** both_sky n=88k oracle mean
[163,196,255] vs magma [166,197,253] (|d|~2-3/ch) - full day blue. Rain
ramp model: if full rain by t1800, rainingStrength at t1760 could be ~0.4;
oracle sky brightness at t1760 is **203** vs clear t1620 **189** and rainy
t1820 **142** - not dimmed. Rain onset is ~t1800 (both_sky starts diverging
gray) and is still BLOCKED under #12. Residual is oracle_only_sky ~18k px
(terrain/tree silhouette holes). Big-oak tree worldpatch already covers the
x20-30 canopy; +29 extra leaf cells measured **0** mean change.

**f040 t800:** oracle_only_sky ~15k (sky vs dirt wall); both_sky n=10k with
magma brighter R/G by ~13-15 (not geometry). Solid-only save-derived
air-clears (y>=61 phantom solids) and place-solids: physics clean when
path-gated, **0** pixel mean change on f040 (cells not the visible sky
notch, or live mesh already matches). Same family as #17/#1b.

## 20. MOSTLY FIXED (round 3): sheep face / grazing head

Tape has no sheepTimer. Idle sheep (limbSwingAmount < 0.08) use mid-graze
head constants from EntitySheep.getHeadRotation* at sheepTimer=20 (ry=15,
ax=PI/5 + graze sine) on skin+fur heads; fur body emits before skin head so
face snout wins near-coplanar depth. f076 t1520 **24.81->19.19/ch**; face
white-vs-dark residual 8387->2106 px. Residual: true sheepTimer still untaped;
wool over-white vs oracle checkered face at some angles.

## 21. FIXED: sprint FOV modifier (verified vs oracle, sprint_fov_probe tape)

EntityRenderer.updateFovModifierHand ported: fovModifierHand +=
(target - fovModifierHand)*0.5 per tick, clamp [0.1,1.5], target 1.15
sprinting / 1.0 (getFovModifier speed-attr ratio); camera fov = 70*mult.
Vanilla order quirk matched for free: against a wall, EntityPlayerSP re-sets
sprint (line 994) then the collide check cancels it the SAME tick (line 999),
so isSprinting() samples false and there is no wall pump - the visible zoom
is the ease during approach/intermittent contact, which magma's sprint
machine already reproduces (physics bit-exact on the probe tape). Verified
pixel-exact: planks-wall span identical oracle vs magma at every sampled
approach tick (110->402->854 px). Probe: trace/sprint_fov_probe.py (scripted
arena + paced bridge stepping; unpaced steps tape stale duplicate frames).
Residual on the probe tape: cobble side-wall face shading (~6.5/ch during
motion, 1.1/ch static). Bow-draw FOV zoom (f *= 1-min(1,t/20)^2*0.15) still
unmodeled - needed for dragon-fight segments.

A/B on the clean probe tape 20260713T073441Z (chat-free): with the fix,
overall 1.46/ch, sprint window 3.5-4.5/ch; with target forced to 1.0 the
same window reads 20-24/ch over ~90% of pixels. Known transient: 15.5/ch for
1-2 frames at sprint ONSET (t=42) - vanilla renders FOV lerped by
partialTicks between prev/current fovModifierHand while magma steps
per-tick, so the steepest ramp tick leads by up to one tick. Decays by t=48;
do not chase on bridge-stepped tapes (one-tick artifact class).

## 22. planks rendered as fallback stone (FIXED for oak sprite)

gm_state_to_model_key had no case for id 5: command-built/village planks fell
to GM_MODEL_FALLBACK gray stone (probe wall 26/ch -> 1.1/ch after fix). New
CBX_PLANKS=224 cube; ALL six species currently share the oak sprite (species
meta preserved in state, visual-only collapse; add per-species sprites when a
tape shows them).

## 23. FIXED: air sprint accel one-tick lag + missing jumpTicks cooldown

Two hold-jump-sprint divergences found by the progression-bot overworld
segment tape (20260713T074351Z, replays PHYSICS-CLEAN after both fixes):

- t435 vx: magma applied the 1.3x sprint air accel the tick sprint turned
  on. Vanilla folds sprint into EntityPlayer.jumpMovementFactor AFTER
  super.onLivingUpdate() has already moved the entity, so airborne accel
  lags the sprint flag by exactly one tick (ground speed reads the attribute
  live, no lag). psv now mirrors it: `jump_factor_sprint` updated
  post-movement at both travel exits.
- t451 y: with jump held, magma re-jumped the tick it landed; the oracle
  waits (jump t442 -> land t450 -> re-jump t452). Vanilla
  EntityLivingBase.jumpTicks: decremented each onLivingUpdate, ground jump
  gated on ==0, set to 10 after jump(); the swim-up branch is NOT gated.
  psv now carries `jump_ticks` with identical ordering.

Regressions: sprint-FOV probe tape 073441Z and fresh-world baseline
20260712T055346Z still physics-clean; 12k human tape unchanged (t9811 is
save-provenance, #1).

## 24. FIXED: viewmodel lighting/water-fov + HUD 1px ScaledResolution shift

Three residual classes killed on the progression tapes (all render-only,
physics stayed clean):

- First-person arm/item lighting: hand.c drew fixed scalars (0.86/0.90 +
  world-style face shades). Vanilla lights the viewmodel with
  RenderHelper.enableStandardItemLighting - ambient 0.4 plus two 0.6-diffuse
  lights at normalize(+-0.2,1,-+0.7) anchored under Rx(pitch)*Ry(yaw)
  (rotateArroundXAndY) - times the lightmap at the EYE block
  (ItemRenderer.setLightmap getCombinedLight). gm_hand_set_env now feeds the
  frame lightmap + eye levels + rotation; per-quad diffuse from transformed
  normals rides in vtx.ao.
- Hand camera fov: renderHand projects with getFOVModifier(pt,false) - the
  eye-in-water 60/70 squeeze applies (never the sprint ease). Magma
  hardcoded 70: underwater the oracle's arm sits mostly off-screen while
  magma's stayed centered (whole bottom-right quadrant diffing 17-25/ch).
- HUD anchors: vanilla lays the GUI out in ScaledResolution coords
  (scaledWidth = ceil(w/scale)); at 854x480/scale2 the scaled center 213*2 =
  426 != fb->w/2 = 427, so every element sat 1px right - edge outlines on
  hearts/hunger/bubbles/XP/hotbar in every diff. hud.c now computes anchors
  in scaled coords (incl. crosshair).

After (overworld segment tape 074351Z): settled 4.75 -> 0.68/ch whole,
terrain 3.56 -> 0.47. Sprint-FOV probe idle: 1.46 -> 0.09/ch. replay_tape.py
also drops byte-identical stale leading oracle frames LOUDLY (recstart
handoff renderer lag taped frame-0 duplicates for ~40 ticks).

## 25. FIXED: position-random block model variants (stone/dirt/grass/sand/...)

The nether-segment tape (portal pad on a /fill stone platform) showed a
static 2.23/ch plateau while standing still: the floor texture matched in
palette and detail energy but the per-block arrangement was scrambled.
Vanilla 1.11.2 has EIGHT blockstates with position-random WeightedBakedModel
variants (from the jar): stone + bedrock (base/mirrored x y0/y180), dirt,
grass (snowy=false), sand, red_sand (y 0/90/180/270), netherrack (all 16
x*y quarter combos), waterlily (y quarters). magma only randomized fire.

mesh_mc.c now applies the variant to every cube emit: selection is the same
MathHelper.getPositionRandom -> WeightedBakedModel path already used for
fire (weighted_variant, ex fire_variant); geometry rotates about the cube
centre with vanilla's -angle ModelRotation convention (matrix Y(-y)*X(-x),
X first; face remap = EnumFacing.rotateX/rotateY), UVs glued (uvlock
false); mirrored variants bake cube_mirrored's explicit uv [16,0,0,16].
Cull, light, AO and shade all key on the FINAL world face. Grass side
overlays and the lily plane get the same treatment.

After: nether-pad tape 083431Z plateau 2.23 -> 0.18/ch, look-around peak
3.75 -> 0.57; overworld tape 074351Z improved at every sampled row (t42
15.42 -> 13.90, t100 6.26 -> 4.31, settled 0.68 -> 0.55/ch) with the old
mesher A/B-verified on identical frames; 12k tape physics unchanged.

## 26. OPEN (recorder-class): walking/turning partial-tick camera offset

While the player moves or turns, oracle frames render at an interpolated
partial-tick pose; magma renders the exact tick pose. At walk speed the
camera differs by up to 0.2 blocks (nearby geometry swims), during 7.5
deg/tick look sweeps by several degrees - whole-frame misregistration
peaking 13-22/ch on close terrain, decaying to the sub-1/ch floor the
moment motion stops. Every moving window in every tape carries this class;
do NOT chase texture/lighting bugs inside them. Candidate fix: tape the
render partialTicks per frame and have replay interpolate the camera pose.

## 27. OPEN (render, degenerate pose): camera inside an opaque block

Vanilla renders the near-black zoomed block texture when the eye is inside
an opaque block (suffocation view); magma draws the scene as if the
camera were in air. Surfaced by tape 095648Z after the bot teleported the
player into solid netherrack (stale arrival obs, bot since fixed): oracle
frames near-black, magma showed the cave - 7.5-8.4/ch over 96% of the
frame for the whole standing window. Physics (incl. suffocation hp loss)
stayed bit-exact; this is render-only and unreachable on a sane route.
Fix when it matters: sample the block at the eye and composite the
in-block face texture like EntityRenderer.orientCamera/renderBlockOverlay.

## 28. OPEN (render, one-frame transient): oracle draws the loading sky one tick past a dimension transfer

Tape 101755Z (walked nether roundtrip, physics clean at 1e-9): the return
transit frames t574-580 are bit-exact 0.00/ch (both sides hold the
placeholder), then t582 alone spikes to 28.5/ch - the oracle client still
renders its empty just-loaded world (darker sky+fog, no terrain) while
magma has already switched to the fully-lit overworld palette; by t584
the diff is back to 1.6/ch. Flat color offset over ~90% of the frame, one
frame long, only at authoritative arrivals. Oracle-side chunk/sky warm-up
ordering, not a magma scene bug; deprioritized. Fix if ever needed:
have replay skip (or fog-neutralize) the first rendered frame after each
snapshot arrival event.

## 29. OPEN (render, viewmodel): drawn-bow registration offset

Dragon tape 104827Z: with the ItemRenderer BOW using-branch transform and
bow_pulling_0/1/2 sprites implemented, the drawn bow lands close but the
oracle's bow reads slightly larger/left of magma's (whole-frame 3.9/ch
over ~14-17% during full draws, floor 1.0/ch elsewhere). Candidates: the
draw-tremble phase (sin((f5-0.1)*1.3) sampled at partial ticks), a
partial-tick term in f5 itself, or equip-progress differences. Tune with
a still-draw A/B harness before chasing texture detail.

## 30. PARTIAL (render, entities): arrow ghosts modeled; old tapes lack pitch

Model landed 2026-07-13: arrow.png in the mob atlas, ER_TYPE_ARROW with an
exact RenderArrow emit (T(pos) Ry(yaw-90) Rz(pitch) Rx(45) S(0.05625)
T(-4,0,0); butt-square fin quads both windings at x=-7 + 4 shaft quads at
Rx 135/225/315/405; tex fins (0,.15625)-(.15625,.3125), shaft
(0,0)-(.5,.15625) of the 32px texture), mapped for
EntityArrow/EntityTippedArrow/EntitySpectralArrow. Verified standalone and
in-frame (isolated ent_view renders the correct horizontal arrow).

Remaining residual on EXISTING tapes: 7-field ent rows carry no pitch, so
non-flat stuck arrows render flat - on the dragon tape (arrows shot ~35deg
upward into the wall) the ghost's tail sits ~0.4 blocks high and the whole
shaft shows where vanilla buries it obliquely (diff heatmap: oracle
X-cross at (427,258), magma fins at (427,190), t120). Recorder now
appends rotationPitch for EntityArrow rows (QuantizedRL.java, source only -
client rebuild pending, same as the skin header field) and replay_tape.py
maps field 7 -> ent_view pitch. Re-record after the client rebuild to close.

## 31. OPEN (hud, minor): hotbar arrow count drifts as the oracle shoots

The oracle consumes an arrow per shot (63,62,...); magma's replay
inventory only consumes via its own projectile sim on use-release, which
tracks but can desync with vanilla's inventory packets (count text pixels
in the hotbar slot). Revisit once arrows are modeled end-to-end.

## 32. RESOLVED (sim + replay + render): mine segment - axe speed, arrival clobber, tool sprites

Mine tape 120328Z (2026-07-13) surfaced three stacked divergences:
- ita_tool_dig_speed had no axe branch (hand speed on logs; oracle chopped in
  15 ticks, magma never finished). Fixed: ita_is_axe + ItemAxe
  effective-on table (material WOOD union EFFECTIVE_ON).
- snapshot arrival re-application fired on EVERY ppos (aim-pin tps), re-
  placing already-dug blocks from the tick-0 snapshot: the t116 pin restored
  the log dug at t57 and the replay walked into the invisible-to-raycast
  ghost block at t190 (window refilled from the patched store; raycast saw
  the post-dig window). Fixed: arrivals only on dim change or >8-chunk jumps.
- held iron axe fell back to the iron_ingot sprite (no tool sprites in the
  item atlas) - 94% of the persistent whole-frame diff at t0 (6.33 -> 1.67/ch
  after adding iron/stone/wood/diamond tool + arrow sprites).

## 33. OPEN (sim, minor): one-tick break lag on aim-pin target change

Mine tape 120328Z t390: oracle walks through the wall's feet block one tick
before magma (x 49.798 vs 49.7196, |d|=0.078, heals at the next pin; end
euclid 0). The dig state machine's acquire/damage cadence differs by one
tick when the target changes via a tp aim-pin mid-hold. Candidates: press
vs held-tick classification around the set_pose tick, blockHitDelay swallow
alignment. Cosmetic on tapes with pins; matters for pin-free play.

## 34. OPEN (render, viewmodel): held-item registration + edge shading

With correct sprites (see 32) the first-person axe still sits ~10px
up-right of the oracle's and lacks the darker extruded-edge shading (oracle
shows a dark right-edge silhouette; magma's plate is flat-lit). Same
registration family as 29 (drawn bow). Candidates: view bob at rest pose,
equip-progress partial tick, per-face diffuse on the plate rim.

## 35. RESOLVED (2026-07-13): sneak edge clamp overwrote motionX/Z (build tape fell off pillar)

Build tape 124101Z first diverged t426 `vz` during the sneak-back bridge and
ended 22 blocks apart (magma stayed on the pillar, oracle inched to the lip
and later fell): `psv` copied the sneak-clamped move back into
`e->motionX/motionZ` (player_survival.h). Vanilla Entity.move clamps only the
move() ARGUMENTS and keeps d2/d4 in sync inside the loop, so motion survives
the post-move zeroing and the player keeps inching 0.05-quantized toward the
ledge lip on later ticks. Fix: clamp into locals `mvx/mvz`, feed those to
mc_entity_move_step, never touch e->motion*. After: euclid 0.0000, end pixels
1.37/ch (was 49.98/ch, 92%).

## 36. ARTIFACT (2026-07-13): hp regen jitter vs set_vitals_post re-anchor

Post-fall regen (FoodStats saturated heal, min(sat,6)/6 every 10 food-ticks)
compares with transient |d| up to 0.83 hp: the tape's client-side hp packets
jitter 1-2 ticks around the true cadence (oracle heal intervals observed
11,10,9,8,12), and replay_tape's `set_vitals_post` re-anchor lands on top of
magma's own correctly-timed heal (e.g. re-anchor 1.83 at t630, own heal
+0.83 at t631 -> 2.67 never seen in oracle). Converges to equality within ~60
ticks; pv_on_update is line-exact vs FoodStats.java. Measurement artifact of
client hp packets, not a sim bug. Accept hp transients during regen windows
that follow a matched damage event and converge.

## 37. RESOLVED (2026-07-13): dragon-fight boss fog missing (End terrain too bright)

First End-dim tape (crystal segment 134124Z): oracle end stone faded to
near-black with distance; magma kept the overworld linear fog (start 96).
The dragon fight's BossInfoServer sets createFog(true) and
EntityRenderer.setupFog pulls the linear ramp to [far*0.05, min(far,192)*0.5]
= [6.4, 64] at rd8 whenever GuiBossOverlay.shouldCreateFog() (same range the
Nether always uses via doesXZShowFog - note WorldProviderEnd.doesXZShowFog is
FALSE in 1.11.2, the End's dense fog exists only during the fight). Fix:
terrain_shades gains boss_fog (frame_capture boss_latch, now updated BEFORE
the terrain pass). t0 whole diff 4.42 -> 0.46/ch.

## 38. RESOLVED (2026-07-13): End clear/fog color used the overworld day color

updateFogColor for the End: WorldProviderEnd.getFogColor is constant
(0.627451, 0.5019608, 0.627451)*0.15 (its celestial-angle term clamps to 0 at
the fixed angle 0.5), blended f = 1 - pow(0.25 + 0.75*rd/32, 0.25) toward the
sky color - which is BLACK in the End (getSkyColor's cos term clamps to 0) -
then scaled by fogColor1 (light-at-feet smoother, ~0.25 with feet at light 0).
Predicts the sampled oracle fog [5,4,5] exactly. frame_capture now has a
dimension==1 clear-color branch mirroring the Nether one.

## 39. RESOLVED (2026-07-13): crystal/mob UV scale vs ModelBase textureWidth

ModelEnderCrystal keeps the ModelBase default 64x32 texture size while
endercrystal.png is 128x64: vanilla UVs are normalized by the MODEL size, so
image pixels = model texels * 2. magma sampled the raw texel coords ->
glass cage read the top-left quarter (pale blue), purple core invisible.
er_aff_box gains uvscale (crystal 2, dragon 1 - ModelDragon sets 256x256
matching dragon.png). Any future model whose PNG size differs from its
ModelBase textureWidth/Height needs the same factor.

## 40. OPEN: particles not rendered (explosion burst, enderman portal specks)

Crystal destruction (t276+ of 134124Z) shows oracle-only explosion particles
plus constant purple portal specks around endermen; whole-frame diff 4.8-5.7/ch
at 80-88% coverage in those windows while structural content matches. Class:
magma has no particle system. Accept scattered speck diffs; revisit if a
milestone needs the explosion visual.

## 41. OPEN: HUD heart flash blink not modeled

After damage the oracle blinks the heart row white/pale for ~20 ticks
(GuiIngame heart flash); magma draws the plain post-damage hearts. Small
fixed-position HUD diff during damage windows.

## 42. OPEN: entity-driven world edits do not replay (crystal explosion fire)

The tape carries no block-change records; magma re-simulates PLAYER dig and
place from inputs, but world edits caused by ENTITIES (the crystal explosion
removing the pillar-top fire block) never happen in the replay world - the
fire keeps burning in magma after t276. Residual ~30px orange at the
destroyed pillar. General fix would be recorder block-diff records (BlockEvent
hook); the dragon fight needs it only for crystal fires, so filed as open.

## 43. OPEN: dragon ring-buffer cold start (and unload resets)

Vanilla EntityDragon keeps a 64-entry (yaw, posY) ring on the server entity;
magma rebuilds it from tape rows, seeding flat on first sight. On a tape
where the dragon stays loaded this only straightens the tail/neck for the
first ~64 ticks. On 140709Z (shot from the End spawn platform, x=100, rd8)
the dragon unloaded 786/2827 ticks and every reappearance re-seeded the ring:
close-pass frames (t1840, 13.7/ch) show the oracle diving mid-charge (body
pitch = ring y-slope * 10 deg) while magma renders it level. Fix in data,
not code: record from island center so the dragon never unloads (bot fixed
2026-07-13); accept the one-time 64-tick warmup.

## 44. OPEN: enchantment glint not rendered

Power V bow shows the purple enchant glint in the oracle (hotbar icon AND
first-person model); magma draws the plain item. Visible as a persistent
purple-vs-brown diff in the hotbar slot plus a shifting overlay on the held
bow. Class: no glint pass (EnchantmentEffect two-pass scrolling texture) in
item render. ACCEPTED: the glint scroll phase is driven by wall-clock
Minecraft.getSystemTime(), not ticks, so it cannot bit-match a replay even if
implemented; an approximation would only shrink, never zero, the diff.

## 45. OPEN: stale duplicate boss bar (oracle client-session artifact)

The oracle client shows TWO "Ender Dragon" boss bars on 140709Z: GuiBossOverlay
keeps a bar per boss UUID until a REMOVE packet, and an earlier End visit in
the same client session left a stale entry. Only one EntityDragon (eid 2463)
exists in the tape. magma draws one bar from recorded state - arguably more
correct than the oracle's leaked HUD. Fixed-position ~10px band diff; accept.

## 46. OPEN: dragon death effects (light rays, XP orbs, exploding smoke)

The kill tape (144207Z, deathTicks 1-196 at t32129+) shows three oracle-only
effects: LayerEnderDragonDeath's expanding white light rays, the XP orb swarm,
and the final smoke burst. magma renders none (no beam/orb/particle
systems). The death window whole-frame diff runs 7-18/ch at 50-90% coverage,
almost all from these. The dragon body itself matches (per-box dissolve, #47).

## 47. OPEN: death dissolve is per-box, vanilla is per-texel

Vanilla masks the dragon skin per-texel to dragon_exploding.png alpha >
deathTicks/200 (alpha-test pass + depth-EQUAL repaint). magma drops whole
model boxes once f passes each box's precomputed q85 exploding alpha
(entity_render.c er_dragon_expl_q85): parts vanish in steps between f=0.50
and 0.74 instead of sparkling out continuously to f=1.0. A per-texel match
needs a per-quad alpha threshold in both raster cores; accepted for now.

## 48. NOTE: dragon-fight pixel floor is particle-bound

With physics clean at 1e-9 over 32651 End ticks, the steady-state whole-frame
diff (2.5-4.5/ch at 12-25%) is dominated by class #40 particles: enderman
portal specks, dragon's-breath lingering clouds (AreaEffectCloud), arrow crit
trails. Terrain/fog/lightmap verified by region sampling at t31500: near
ground oracle [50,42,37] vs magma [54,50,38], far ground [18,14,25] vs
[11,9,15], divergent regions are all particle clouds ([80,19,70] magenta vs
dark sky).

## 49. FIXED (2026-07-13): dragon flight pose stale under sparse frame capture

The e2e tape (175629Z) rendered the flying dragon with the wrong body
orientation whenever it turned fast or buzzed the player (t16700 spiked to
11.7/ch: the oracle wing filled the screen while magma's dragon had swung
off-frame). Vanilla pushes (rotationYaw, posY) into the 64-entry
getMovementOffsets ring every onLivingUpdate; magma pushed only inside
emit_dragon, which runs only on RENDERED ticks, so --frame-every 2 halved the
ring rate and every lookback (body yaw mo7, pitch roll (mo5.y-mo10.y)*10,
neck/tail trail) landed twice as far back. Fix: gm_dragon_pose_tick advances
the ring on skipped-render ticks from gm_frame_capture_write; rendered ticks
keep pushing inside the emit. Fight worst-frame dropped 11.74 -> 2.97/ch
(remaining floor is #48 particles + #29 drawn bow + #50 bar).

## 50. OPEN: e2e oracle lost the dragon boss bar (End-entry registration bug)

The 175629Z e2e goldens show NO boss bar for the entire fight (0 magenta px
in 320 sampled End frames) while the staged-entry m8 tape (144207Z) shows it
(2148 px/frame): the organic End entry hit the vanilla transfer-registration
bug, and the qrl watchdog's re-registration restores world lists but the
client's GuiBossOverlay map never gets a working ADD again (DragonFightManager
did run - "Scanning for legacy world dragon fight" fired post-entry). magma
draws the bar from recorded dragon state, which is vanilla-correct; the
constant ~10px top-band diff on this tape is an oracle-side artifact. Fix
would be recorder/mod-side (force bossInfo remove+add after watchdog
re-registration), then re-record; accept for the existing tape.

## 51. OPEN (render, water): eye-at-surface water fog far too weak

Found by the pixel gate on the e2e tape (175629Z t140-166, swimming at spawn
bay): with the eye at or just under the water surface the oracle's view is
near-opaque deep blue (dense water fog) while magma renders clear water with
underwater terrain silhouettes visible tens of blocks out. 60k-280k px
clusters per frame. Matches the 2026-07 playtest "water looks wrong" note.
Class: underwater fog density/onset in game/underwater.c (uw.fog01/density vs
vanilla's fogDensity 0.1 + waterVisionTime ramp); also check the surface-crossing
eye-height threshold. Repro: gate a replay of the e2e tape, see gatefail_sbs_t000150.png.
Checked-and-dead lead (2026-07-17): Malmo CAN install a BlankSkyRenderer via
IRenderHandler, but only from ColourMapProducerImplementation during colourmap
missions; human play + qrl tape runs never start one, so the oracle's sky/fog
in all tapes is plain vanilla. The divergence is real, reference math stays
vanilla-only.

## 52. OPEN (world-state, staging): mid-tape arena state window in the mine segment

Gate fail t1054-1056 (e2e 175629Z): the oracle wall is mid-dig with break
holes while magma shows a cleanly formed window in the same wall - the
world snapshot patch applies entirely at t0, so any ORACLE-side staged edit
or divergently-timed break inside the tape leaves a bounded window where
block state differs until organic edits reconverge (~t1100 here). Needs a
tick-stamped block-edit channel in the recorder (record setblock/fill and
organic break/place server events with their tick) applied by replay at the
recorded tick. Bounded, self-healing; the gate baseline carries it.

## 53. OPEN (render, fog): high-altitude / distance haze much weaker than oracle

Found by the pixel gate on the e2e tape (175629Z; worst run t1510-2198, ~90k
diff px per frame, also t406-468): standing high up looking down over ocean,
the oracle's distant terrain and water sit under a strong bluish distance
haze (fog toward the horizon/lower view rows) while magma renders the same
chunks nearly crisp - whole-quadrant clusters (15-20k px each) over distant
water and shoreline. Not the underwater case (#51): the eye is in air.
Class: terrain distance-fog curve/onset vs vanilla setupFog (EXP2 density and
the farPlaneDistance ramp), possibly missing the render-distance fog start
scaling. Repro: probe_sbs_t1800.png from a gated replay of the e2e tape.

## 54. OPEN (recorder-class): legacy EntityItem rows omit render state

The canonical 20260712T055346Z tape predates the expanded entity schema.
Its seven-field `EntityItem` rows omit item id/count and age/hover state, so
the dig/drop window t2460-2900 cannot reconstruct the oracle's dropped-item
sprite, stack, bob, or spin. Physics remains bit-clean over all 3121 ticks;
the mismatch is confined to recorded pixel frames in that window. New tapes
record the expanded fields. The canonical tape's known-divergence sidecar
scopes this legacy evidence to that tick window and the non-positional scene
region; a new solid marker remains gate-failing.

Repro: CPU replay the canonical tape with `--report --cpu`; inspect the
`known:54` gate class and the t2460-2900 side-by-side frames.

## 55. FIXED (codex, tape 20260721T215812Z): torch placement - support, refire, and hit face

Three bugs found via the bot-recorded canonical tape's torch windows
(t3419-24 pit torch, t3447-52 ground torch):
(a) held-use refire placed a SECOND torch in an unsupported cell; magma now
validates BlockTorch support (vanilla canPlaceBlockAt order) and a rejected
placement does not consume (`torch_placement_meta` in player_ctl.c).
(b) the held-torch viewmodel rendered a 3D stick; 1.11.2 models/item/torch.json
inherits item/generated (flat extruded sprite) - hand.c now routes it there
(test: tests/test_hand_torch.c).
(c) the refire after a successful wall-torch placement re-raycast to a STALE
face: gm_raycast_sel_reach returned the DDA voxel-entry cell as the place
spot, but for recessed AABBs (wall torch) the struck face differs - a
descending ray entered from above yet hit the torch's WEST face, so magma
read an UP click and stacked an extra torch on the supported cell above.
Vanilla derives sideHit from AxisAlignedBB.calculateIntercept and rejects
the occupied cell without consuming. sel_box.c ray_box_hit now returns the
actual AABB face (mirrored in blaze_core.h cu_ray_box_hit); regression:
test_play_compose.c descending-ray case.
