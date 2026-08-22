# Open divergences

Active gaps only. Resolved work is recorded in git history and
`docs/DEVLOG.md`. Closed, retracted, or superseded entries keep their full
forensics in `CLOSED_DIVERGENCES.md` (a stub with the close date and one-line
resolution stays here in place); read there before re-investigating anything
that smells like a settled question.

Last verified on `d7f2998`, which includes the reviewed capture, preview, and
exact-hand-gate commits (`44a7de3`, `27ec2fc`, `d198a96`). Oracle evidence
for rain / slime DRAW / portal A/B: lane/unblock 2026-08-21.

Pixel-perfect means every owned, A/B-stable pixel is equal. Mean-error budgets,
hard-pixel floors, empty target captures, and unstable Oracle pairs are not
passes.

Stop-asking rank and GPU-port sequence live in `docs/GATES.md` "Remaining
to stop asking". This file keeps forensics. Survey 2026-08-21 (Fable rank):

Grindable here: hand use poses; inventory preview; auto-campaign hand /
HUD; slime rim (oracle translucent DRAW dump exists); rain lightmap
(rain tape exists); portal/underwater C residual (Oracle A/B now maxch=0).

Do not grind here: explosion puff `Particle.rand`; fog retune; texel bias;
Magma GPU tick; particle blend=3 (vanilla ParticleManager is SRC_ALPHA,
magma already matches); canonical t=260 texel-selection (retracted,
CLOSED); entities over water (item 13 CLOSED).

## Interactive C raster renderer

### First-person hand use poses

All three states have exact Java A/B captures (`noise_max=0`) and remain
strict C residuals. Ownership is the Java∪C subject at `HAND_SUBJECT_THR=8`.
Bow/eat recaptured 2026-08-22 (`lane/handgold`) with sticky USE pose
(`use_branch=bow|eat`, bow remaining `use_count=71980`, eat remaining 16/32).
A/B sha256 identical. Idle-tip goldens retired. Shield A/B not recaptured.
Candidate now stages the capture pad through `ui_hud_scene` (superflat seed 0,
stone pad+wall, pose 8.5/5/8.5 yaw 0 pitch 0, time 6000) and composes
world then `gm_hand_draw` then HUD. `n_only_j` 16309/50268/12533 -> 0/41/17
(wall is no longer Java-only). Do not retune `hand.c` transforms.
Numerical/compose/live and HUD chrome still PASS; overlay rows byte-stable;
synthetic exact/mutation controls still PASS.

- Bow pull: `hard_px=20745`, `maxch=108`, `c_vs_j=7.007`, `n_only_j=0`
  (was isolation 20830 / 100 / 29.266 / 16309).
- Eat mid-use: `hard_px=73440`, `maxch=215`, `c_vs_j=1.317`, `n_only_j=41`
  (was 74218 / 215 / 32.764 / 50268).
- Blocking shield: `hard_px=28564`, `maxch=61`, `c_vs_j=0.911`, `n_only_j=17`
  (was 28506 / 100 / 23.613 / 12533).

`hard_px==0` is not reached. Eat/shield owned leftover is mostly 1 L8 wall
texels plus painted-face LSB (same class as inventory preview / portal pad).
Bow `c_vs_j=7` is occupancy in the lower-right ROI: gray bow metal vs C
stone, not a missing wall. Stone has no random model rotation
(`BlockModelShapes` maps `Blocks.STONE` by `VARIANT` only). PASS still
requires `hard_px==0`. Diff triptychs (gitignored):
`out/verify/ui_hud/handscene/<id>_tri.png`.

Repro:

```bash
bash verify/ui_hud/run_ui_hud_gates.sh
```

### Inventory player preview

GUI chrome is bit-exact (table/furnace/chest/inventory non-preview `bit== PASS`,
A/B noise 0). The rendered player remains open at max channel 1:

- Pose 1: mean `0.002448`, `62` nonzero pixels, `hard_px=0`.
- Pose 2: mean `0.003316`, `140` nonzero pixels, `hard_px=0`.

Re-measured 2026-08-22 (`lane/preview`). Packing is Mesa FLOAT_TO_UBYTE
then unorm8 modulate (`L8=round(primary*255)`, `out=(tex*L8+127)/255`).
Primary is RenderHelper 0.4+0.6 on GL 2.1 BYTE normals
(`VertexBuffer.normal` `(int)(c*127)`, unpack `(2c+1)/255`) after
`prepareScale` RESCALE_NORMAL only (no `GL_NORMALIZE`). Remaining 1 L8
sits on face bins whose `C*255` is just above `n+0.5` while sibling bins
need round the other way (`test_preview_color_formula.py`). Do not invent
a PASS-FLOOR.

Repro:

```bash
bash verify/mc_capture/run_gui_verify.sh
```

### Portal and underwater

Oracle A/B is now exact. Recapture 2026-08-21 (`aa43667` frame_pair fog
freeze + 10s `finishTimeNano` deadline), command:

```bash
ONLY=overlay_portal_050,overlay_underwater bash verify/ui_hud/capture_ui_hud.sh
```

Measured on the twins (full 854x480, 409920 px):

| id | maxch | n_ab_maxch_ge1 | mean | fog_restored | pass_a==pass_b |
|----|-------|----------------|------|--------------|----------------|
| overlay_portal_050 | 0 | 0 | 0.0 | 1 | yes |
| overlay_underwater | 0 | 0 | 0.0 | 1 | yes |

Root cause of the old `CAPTURE_BLOCKED` was the capture harness, not the
C overlay: `frame_pair` stepped `EntityRenderer.fogColor1/2` (0.1F smoother)
twice on one turn, and `renderWorld(..., pairNano)` used a deadline already
in the past so `updateChunks` skipped remaining uploads. The accepted noisy
pair (~865 portal maxch=1; underwater n_ab_maxch_ge1=183372, maxch=3) is
superseded. PNGs: `verify/ui_hud/goldens/overlay_{portal_050,underwater}_{a,b}.png`
(a sha256==b). Meta records `noise_max=0`.

C-vs-J is still OPEN against this exact pair. Same-scene underlay is wired
(window_compose on the capture pad; warp WORLD-only before renderHand).
PASS still requires `hard_px==0`. Do not revive a noisier pair.

Measured 2026-08-22 (`lane/portalpix`), full A/B-stable ROI, `hard_thr=0`:

| id | c_vs_j | hard_px | maxch | note |
|----|--------|---------|-------|------|
| overlay_portal_050 | 1.465 | 363305 | 144 | was 47.191 / 391116 / 181 gray isolation. Interior ~1 LSB; leftover edges/hand. |
| overlay_underwater | 26.763 | 390096 | 112 | was 6.083 / 388620 / 55 fitted constant (74,75,79). Overlay formula matches ItemRenderer; Magma water/glass/fog underlay is too blue vs Java. |

Underwater close path is terrain/water raster fidelity (possibly kernel
twins), not overlay alpha or brightness constants.

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
- The death burst (deathTicks 180-217) now reconstructs the full vanilla
  timeline - every one of a `ParticleExplosionHuge`'s 8 batches (not just the
  newest), and the ~17 ticks of cloud that outlive the entity - and the boss
  fog now ends at `processDragonDeath` as vanilla does, which is what makes
  the post-death cloud read white instead of fog grey. What stays open is
  placement: the offsets come from `EntityDragon.rand` / `Particle.rand`,
  neither recorded in a tape and both seeded from system time, so magma's
  cloud matches the oracle's extent, brightness, and decay but not puff for
  puff (~0.6 IoU on the bright mask). Exact match needs the recorder to log
  `spawnParticle` calls.
- Death dissolve is still per-box rather than vanilla per-texel.

Repro:

```bash
bash verify/ui_entities/run_gates.sh
bash verify/ui_entities/run_oracle_gate.sh
```

### Blaze on-fire flag in the LIVE simulator: CLOSED

Moved to CLOSED_DIVERGENCES.md. Live AIFireballAttack 78-on/100-off gated
by `bash magma/game/test_mob_live.sh` and `--blaze-receipt`.

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
initializer and is **fixed** - see DEVLOG 2026-07-25.

**t=260 texel-selection CLOSED 2026-08-21.** The old 7291-px / 6252-px
"texel-selection" clusters were the FOV-before-sprint ordering bug
(already landed; `player_ctl.c`). Re-measured on this tree: t=260 is 2
`known:4` shading-offset clusters (65+53 px, sel=0.00), not UNEXPLAINED.
t=460 has 0 clusters >=50 px. Full forensics in CLOSED_DIVERGENCES.md.

What remains on
`20260721T215812Z_fast_s0_survival_default_rd8_77b5b462` (CPU replay
2026-08-21, 181 frames):

- Pixel gate still FAIL, 2 frames, both mild_shift with 0 UNEXPLAINED px:
  t=3080 mean=10.67, t=3540 mean=6.96. Those are the dig window
  (sidecar known:14), not t=260. UNEXPLAINED 11441 px over 22 frames,
  max_cluster 1435 (under FAIL_CLUSTER 4000).
- t=260 residual 118 px is sidecar-accepted `known:4`
  `texture_luminance_modulation` (oak log darker in magma: golden
  (90,89,84) vs magma (59,44,34)). Tape and magma both `ao=0`. No exact
  shade fix without an oracle fragment lightmap capture; texel bias / fog
  retune stay forbidden. Repro: `cd verify/trace && uv run --no-project
  --with numpy,scipy,pillow python pxdiff.py clusters --tape
  20260721T215812Z_fast_s0_survival_default_rd8_77b5b462 --tick 260`.
- t=3180/3200/3220 clusters soak from `viewmodel` (hand/item residual), a
  separate open item under "First-person hand use poses".
- Residual whole-frame mean at t=80 is 3.76/ch, all outdoor terrain: grass
  tint / AO / luminance (the `known:4` class), not geometry.

Repro:

```bash
uv run --no-project --with numpy --with scipy --with pillow --with nbt \
  python verify/trace/replay_tape.py \
  verify/tapes/20260721T215812Z_fast_s0_survival_default_rd8_77b5b462.jsonl \
  --cpu --report
```

### Double-height plants render as solid tinted slabs

RESOLVED 2026-07-29. Two separate defects wore the same face. The model
mapping (upper-half meta quirk, per-type tint) was the first and is fixed
(90 model-oracle contracts). The residual "FLAT biome-tint rectangles"
was **not a renderer bug at all** - nothing in the plant texture path is
wrong:

- The mesh UVs are correct. Dumping every cross quad the scenic_walk
  replay emits gives tallgrass `uv=(0.191,0.500)..(0.247,0.562)`, i.e.
  the full 14x16-texel span of the sprite's atlas rect (48,128)-(64,144).
- The sampling is correct. Rendering the CUTOUT layer's interpolated UV
  showed a smooth gradient, and each rendered pixel equals
  `texel * tint * light` exactly (px(300,280): magma (70,109,52),
  sampled texel.r 149, tint (122,191,91) -> implied texel 146).
- The quads only LOOKED flat because they were point-blank: the biggest
  "solid" blob (11937 px, 142x118) spans **1.8 texels** of UV. It is one
  tallgrass card 0.81 blocks from the camera, magnified ~74 px/texel.

That card should not have existed. The recorded save has AIR at
(-171,70,247); the replay grew a plant there. `snapshot_patch.py` diffs
the save against `world_dump`'s generation and emits an event only where
they disagree - but the replay renders the GAME's generation, and the two
do not agree on decoration. magma's populate windows seed each other with
their neighbours' out-of-bounds spill (`world/populate_mc.c`
build_window donor seeding), so a window's cell list depends on which
windows were resident when it was built: `world_dump` builds them in one
fixed sweep, the game builds them around a walking player. Measured on
chunk (-11,15), seed 3: `world_dump` 24 tallgrass cells, the game 42,
agreeing on only 12. Every cell where the save and `world_dump` happened
to agree emitted no event and kept whatever the game grew there.

Fix: `snapshot_patch.py` now also states the save's value for the whole
vegetation band (4 blocks above each column's ground top), making the
patch authoritative exactly where generation drifts instead of trusting
`world_dump` to match. scenic_walk t=80 whole-frame 10.33 -> 3.39 /ch
(terrain 12.55 -> 3.13), tape mean 4.86 -> 3.34, unexplained gate pixels
4.03M -> 2.38M, failing frames 92 -> 39. Triptych:
~/dev/nw/.tmp/dp/scenic_t80_golden_before_after.png.

**Closed 2026-07-29.** The band was a workaround for diffing against the
wrong world; the patch now diffs against the right one. Census of the 169
chunks around the scenic_walk start (save vs `world_dump` vs the game's own
generation, `MAGMA_WORLDDUMP` in `game/script.c`): 4664 cells where the game
disagrees with the save while `world_dump` agrees with it, so no event was
emitted - and 4488 of them (96%) are logs and leaves, up to 30 blocks above
the ground the band is anchored to. A band cannot reach those. Ores are 1
cell and lakes 2: `WorldGenMinable` only reads base terrain, so ore placement
is effectively order-independent and never needed covering.

Fix: `snapshot_patch.py` runs a PROBE pass before it diffs - the tape's own
replay script with the patch reduced to its `snapshot_region` ensures and no
`snapshot_block` - and reads the world back out of the running game. That
probe builds populate windows in the real replay's order, because the order
is fixed by the ensure sequence and the simulated walk and neither depends on
the patch's block contents (`snapshot_region` is a plain `gm_world_ensure`,
and its tile list is derived from the tape). Verified: the per-tick player
chunk is identical across all 274 scenic_walk ticks with and without the
block events applied.

The patch is now exact rather than approximate, and much smaller for it.
Diffing the PATCHED replay's live world against the save, over the 289
chunks visible from the tape:

| tape | patch cells before | after | world cells still wrong |
|---|---|---|---|
| scenic_walk | 350646 | 66051 | 7046 -> 0 |
| slime_bounce | 302080 | 1465 | 0 -> 0 |
| hold_dig_dense | 295936 | 3 | 0 -> 0 |

Only scenic_walk was actually WRONG before; the other two were merely paying
~300k events to restate cells `world_dump` had guessed wrong but that the
game would have generated correctly anyway. scenic_walk's 7046 were 4517 tree
blocks, 2477 air where the save has a tree, 48 terrain, 1 ore, 1 plant. It is
now bit-identical to the save at
t=60/120/200/239/244/260/273, and the canonical 215812Z tape ends 3616 ticks
in with 5 differing cells, all of them blocks the session itself placed or
broke. Chunks the probe cannot observe (outside the resident 19x19 pool at
every dump tick) still fall back to `world_dump` plus the vegetation band;
that is 68 of 357 chunks on the canonical tape, 0 on the other three.

Consequence for the pixel gate: scenic_walk's remaining 39 failing frames
are NOT decoration. The world behind them is provably the save's, so the
residual is renderer-side (the failing frames' clusters are 40% particle
soak, the rest viewmodel and thin-line).

### Nether arrival: fire/lava content missing from the replayed world

Found 2026-07-29 on the first dense portal tape
(`scenario_portal_roundtrip_20260729T075228Z`); **root-caused and fixed the
same day**. Neither suspect in the original note was right: no filter dropped
fire (51) or lava, and the patch never covered DIM-1 because **there was no
DIM-1 to cover**.

- The recorder snapshots the save at `recstart` (`Recorder.java`, recstart
  handler). A dimension the player first enters DURING the recording has no
  region files on disk at that moment, so it can never be in that copy:
  `075228Z_world/DIM-1/` held only `data/` and `forcedchunks.dat`, and
  `snapshot_patch.py` emitted 0 dim -1 events (its cache is 29 events, all
  dim 0). The replayed Nether was therefore 100% magma's own generation.
- magma generates Nether TERRAIN (`nether_full.h` `nf_run` =
  ChunkProviderHell prepareHeights/buildSurfaces/MapGenCavesHell + fortress),
  which is why the cave geometry matched. It does not run
  `ChunkProviderHell.populate` - fire, lava springs, glowstone, quartz, magma
  blocks, mushrooms - and it cannot: that method's `Random` is reseeded only
  in `provideChunk` (`ChunkProviderHell.java:267`), so `populate` consumes
  whatever RNG state the previously generated chunk left behind. Nether
  decoration is chunk-load-order dependent, not seed-derivable. The saved
  world snapshot is the only sound mechanism for it.
- Second, independent defect on the replay side: `snapshot_arrival_events`
  detected arrivals only from position packets, and a portal transit has none
  (the server moves the player inside `changeDimension`). On this tape the
  row `dim` flips at t=133 and the first `ppos` is t=168, so even with DIM-1
  data the patch would only have been applied at tick 0, to a world the
  player was not in yet.

Fixes: `snapshotSaveDir(mc, snapRoot, addOnly)` in `Recorder.java` - the
recstart pass is unchanged, and `recstop` runs a second ADD-ONLY pass that
copies only paths the snapshot does not already have, so dimensions born
during the recording are added while recstart truth for the start dimension,
`level.dat` and `playerdata` is never overwritten with end-of-session state.
Plus the dim-flip arrival in `replay_tape.py::snapshot_arrival_events`.

The 075228Z tape is NOT repairable - its Nether was never written to any
disk that still exists - so the scenario was re-recorded with the fixed
recorder as `scenario_portal_roundtrip_20260729T083543Z` (dims {0:134,
-1:352}, `recstop` reported `snapshot_added: 4`, one per Nether region file).
Same-tape A/B, CPU replay (DIM-1 region hidden vs present, patch cache
dropped both times): 387 failed frames / 75.1M UNEXPLAINED px over 368
frames, worst t=292 at 266k -> 170 failed frames / 11.2M px over 143 frames,
worst t=281 at 175k. Fire and the arrival lava pool are present and the
chamber is lit in both panes (t=216, t=280 SBS).

Still open on that tape (170 frames): fire/lava ANIMATION phase and the
lightmap around them, plus the pre-existing viewmodel/HUD classes.

### Magma's generated nether lava sea is FLOWING lava: FIXED (2026-07-29)

Moved to CLOSED_DIVERGENCES.md.

### Eye-in-fluid overlay timing: CLOSED (root-caused 2026-07-29)

Moved to CLOSED_DIVERGENCES.md.

### Waterfall ENTRY window on the dense elytra tape (t=58..65) - CLOSED

Moved to CLOSED_DIVERGENCES.md.

### Fortress-hunt tape finds (2026-07-29, scenario_portal_fortress_blaze)

Three divergences from the staged fortress-melee recording
(`tapes/retired/scenario_portal_fortress_blaze_20260729T090129Z`):
- **Fortress placement**: CLOSED 2026-08-21. Piece tree now matches 1.11.2
  (`pendingChildren` ArrayList shift-remove, `HORIZONTAL.random` N/E/S/W,
  `setRandomHeight(48,70)`). Seed-0 `nether_full` spawners are
  (-325,56,-102) and (-325,56,-215), equal to the oracle DIM-1 MCA.
  Forensics in CLOSED_DIVERGENCES.md.
- **Blaze death animation**: FIXED 2026-07-29. `gm_entities_emit`
  computed the `RenderLivingBase.applyRotations` keel and then threw it
  away (`(void)death_roll;  /* z-roll needs entity-level aff */`), and it
  tinted only on `hurtTime`, not `hurtTime || deathTime`. The tape has
  always carried both: entity row fields 10/11 are `hurtTime`/`deathTime`
  (t=274 is the first row with hp 0, hurtTime 9, deathTime 1; deathTime
  runs to 19 at t=292, then the entity despawns), `tape_to_script` writes
  them as ent_view `hurt`/`death` and `script.c` loads them into
  `GmEntityView`. Nothing needed inventing. `emit_box` now takes the keel
  cos/sin and applies it between the flipped model vector and the body
  yaw, matching the vanilla stack
  `translate(pos) . rotateY(180-yaw) . rotateZ(keel) . prepareScale`.
  Two conventions were settled by measurement, not assumption:
  partialTicks is 1.0 (partial=0 costs 155k unexplained px), and the red
  tint persists for the whole death (dropping it costs 44.7k px). Result
  on the repro tape: mean abs error over the death body falls at every
  death tick (t=281 42.6 -> 22.2/ch, t=283 27.3 -> 8.5/ch), tape
  UNEXPLAINED 5238675 -> 5124969 px. Note the keel saturates at 90 deg
  from deathTime 13, not 20 - `sqrt(deathTime*0.08)` hits 1 at 12.5.
- **Spawner cage miniature**: CLOSED (data path). Moved to
  CLOSED_DIVERGENCES.md. TileEntities -> `set_tile_entity` ->
  `GmRuntime.spawners` -> `gm_frame_spawners_emit`. `discover_spawners`
  still does not drive the TESR.
Harness notes that cost takes (now in the yaml): `structures: false`
disables fortresses entirely; melee attacks fire on the mouse-DOWN edge
so held button-1 lands exactly one swing (this is why the older
blaze_melee/blaze_bow tapes never damage their blaze); the target is
NoAI-pinned because a live blaze kites to fireball range.

### Dragon-kill tape finds (2026-07-29, scenario_dragon_kill)

From `tapes/retired/scenario_dragon_kill_20260729T094414Z` (pitch-armed
command-block kill so the real onDeathUpdate plays; see the yaml):
- **Death-ray intensity curve**: **root-caused and FIXED** (2026-07-29,
  `wt/dragonfx`). Three independent bugs in one symptom, all in the
  `LayerEnderDragonDeath` port:
  1. *Late onset*: `gm_dragon_death_rays_emit` skipped the entity while
     `(f+f*f)/2*60 < 1`. Vanilla's loop is `for (i = 0; (float)i < bound;
     ++i)`, so ANY bound > 0 draws one ray - deathTicks 1 already has a
     beam (`LayerEnderDragonDeath.doRenderLayer`). Cost: ~5 death ticks.
  2. *Too bright mid-death*: the ray pass ran with no fog and no
     lightmap. `EntityRenderer.setupFog` is scene state (entities are
     drawn under it), and the End dragon fight's `BossInfo.createFog`
     (`DragonFightManager.bossInfo` ... `setCreateFog(true)`,
     `GuiBossOverlay.shouldCreateFog`) pulls the linear ramp to
     `[far*0.05, min(far,192)*0.5]` = [6.4, 64] - the dragon sits 39-54
     blocks out, i.e. 57-83% fogged. Separately, the layer's
     `disableTexture2D()` only disables the ACTIVE unit, so the lightmap
     on `OpenGlHelper.lightmapTexUnit` keeps MODULATing the fans with the
     dragon's brightness (0.24 in the End, not 1.0). MixinStripBossBar
     hides only the HUD bar, never the BossInfo, so the fog is live for
     the whole animation even though no bar is in frame.
  3. *No final starburst*: `(int)(255.0F * (1.0F - f1))` goes NEGATIVE
     once deathTicks+partial > 200, and `VertexBuffer.color(int,int,int,
     int)` stores it through a Java `(byte)` narrowing cast (UBYTE
     branch) - it WRAPS to ~250. That wrap IS vanilla's t=458 starburst;
     magma clamped to 0 and the rays vanished at the oracle's peak. The
     `bound > 60` clamp went too: the CLIENT keeps ticking deathTicks
     past 200 (`setDead` is inside `!world.isRemote`).
  Measured on the tape, mean-abs/ch vs golden: t=260 0.683 -> 0.572,
  t=340 3.354 -> 0.932, t=458 8.484 -> 7.355; whole tape 3.307 -> 2.235,
  death window (t=258..470) 3.917 -> 1.066. Ray energy now tracks the
  oracle within 1-9% for every frame from deathTicks 100 to 198.
  Residual at t=458 only (oracle ~2x brighter in the far field): the
  EXPLOSION_HUGE cloud `onDeathUpdate` spawns at deathTicks 180-200, and
  magma stops drawing the dragon entirely after t=458 because the tape's
  `ents` rows stop while the oracle client renders it to deathTicks ~204.
- **Dragon boss bar**: **not a boss bar** (2026-07-29). Neither side
  draws one on this tape - the recorder's `strip.overlays` cancels
  `GuiBossOverlay.renderBossHealth` (MixinStripBossBar) and magma gates
  its own top-center bar on `MAGMA_STRIP_OVERLAYS`. The "small strip
  above the hotbar" is magma's ARMOR row: the scenario equips a leather
  chestplate whose only job is knockback resistance, and an ItemStack
  with an `AttributeModifiers` tag REPLACES the item's default modifiers
  (`ItemStack.getAttributeModifiers`), so vanilla's generic.armor total
  is 0 and no icons are drawn. magma derived 3 from the item id, which
  is all the tape carries. **Fixed in code, needs a re-record**: the
  recorder now writes the real total (`Recorder.recordTick`,
  `"armor":p.getTotalArmorValue()`), the replay emits `armor_view`, and
  `gm_runtime_tape_armor` overrides the item-derived guess.
- **Phantom HUD icon**: **root-caused, fixed in code, needs a re-record**
  (2026-07-29). It is the Resistance status icon (a shield in
  inventory.png), drawn because the scenario runs `effect @p resistance
  1000000 4 true` - the trailing `true` is hideParticles, and
  `GuiIngame.renderPotionEffects` wraps the whole icon blit in
  `if (potioneffect.doesShowParticles())`. The recorder's `pots` triples
  never carried that bit, so magma could only assume "visible". `pots`
  entries now carry `doesShowParticles` as a 4th field, `potion_view`
  takes `show_particles`, and `GmPotionEffectView.hide_particles` gates
  the blit (inverted so legacy rows keep vanilla's shown default - the
  wither_skeleton tape's icon must not disappear).
  Verified by replaying this tape's generated script with both fields
  injected (what a re-record would produce): armor row and icon go to 0
  px, matching the golden, and mean-abs drops further to t=260 0.121,
  t=340 0.482, t=458 6.901, whole tape 1.817. THIS RETIRED TAPE STILL
  SHOWS BOTH until it is re-recorded with the new recorder.
- **Entity interpolation / mirrored death pose**: **root-caused and
  FIXED** (2026-07-29, `wt/dragonfx`). Not view lag and not
  interpolation - two bugs in the `getMovementOffsets` trail ring, both
  in `gm_dragon_pose_tick` (`game/entity_render.c`). Nothing reads the
  entity's `rotationYaw` directly: `RenderDragon.applyRotations` and the
  whole `ModelDragon` neck/head/tail chain are driven ONLY by the ring,
  so a ring-phase error rotates and re-poses the entire dragon while
  leaving its translation correct - which is why the symptom read as a
  positional offset with no fixable `(dx,dy)`.
  1. *Ring phase one tick early*: vanilla's push
     (`EntityDragon.onLivingUpdate:239-240`) runs BEFORE the tick's own
     motion - the client interpolation block is at `:242-255` and the
     phase movement below it - so `ringBuffer[idx]` is the pose at the
     END of tick T-1 while the render at `partialTicks=1.0` draws the
     body at the end of tick T. The tape's `ents` row is post-tick
     state, so magma was pushing tick T. Fix: hold the current row in
     `pend_*` and push the PREVIOUS one, which makes `ring[]` a literal
     `ringBuffer[]` and keeps `er_dragon_mo` a literal
     `getMovementOffsets`.
  2. *Ring kept advancing after death*: `health <= 0` takes
     `onLivingUpdate`'s `:191-197` branch (explosion particles) and
     never reaches the push, so vanilla's ring - and with it the ENTIRE
     model pose, `animTime` included - freezes at death. Meanwhile
     `onDeathUpdate` spins `rotationYaw += 20` per tick
     (`EntityDragon.java:701`) and the recorder faithfully writes that
     spin into the tape (`yaw` runs 157.5 -> 797.5 over t=258..290,
     unwrapped, because the `wrapDegrees` at `:217` is in the alive
     branch). magma fed the spin to the ring, so its dying dragon
     rotated ~20 deg/tick and read as MIRRORED within ~9 death ticks.
  Measured with the geometry oracle (`<tape>.geom.jsonl` vs
  `MAGMA_GEOM_DUMP`, `geom_diff.py --offset 0`, ticks 180-458 - below
  180 the recorder's golden re-render is still stale, `jaw` is pinned at
  its t=0 value there). 1668 part comparisons: 255 mismatches, worst
  164.6 texels / 3.109 rad -> **0 mismatches, max 0.000 texel /
  0.0000 rad**. Dead ticks alone went 198/1188 bad -> 0. Pixel residual
  in the dragon window (x[300,620] y[100,370], per-pixel max-channel
  delta > 16, best-shift scan +-16 px): t=230 542 -> 183, t=244
  482 -> 195, t=260 617 -> 436, t=270 4957 -> 1954, t=280 5284 -> 2277.
  Best shift is (0,0) before AND after - there was never a translation
  to recover, confirming the "~16px right" reading was pose error.
  Residual after t=270 is the death-ray/dissolve pass, tracked above.

### Geared dragon-kill tape: the death clock desyncs (2026-07-29)

`scenario_dragon_kill_geared_20260730T025316Z` is the same bow kill with a
geared player, and after the burst rebuild above it failed the gate on two
frames (t=454 4855 px, t=456 4258 px, both magma-brighter UNEXPLAINED). It is
not a magma timing bug: **that recording's client and server death clocks are
6 ticks apart, and the tape records only the client's.**

Evidence, all from the tape's own frames:

- The death rays agree exactly. `LayerEnderDragonDeath` is driven by
  `deathTicks` through a fixed `Random(432L)`, so the spoke pattern is a
  fingerprint of the render clock. Magma's t=454 rays vs the oracle's:
  **IoU 0.900 at t=454 and 0.000 at 448, 450, 452, 456, 458, 460**. The
  recorded `deathTicks` IS the client's render clock, including the
  `(byte)(255*(1-f1))` alpha wrap that makes the dt-201 starburst.
- The cloud does not. The oracle's explosion cloud drops the dense
  `BossInfo` fog ramp (p90 33.7 grey -> 180.0 white, px>150 68 -> 3692)
  between t=446 and t=448, i.e. at recorded `deathTicks` 195 - six ticks
  before that same clock reaches 200. Particle brightness has no other
  input: `ParticleExplosionLarge` hardcodes `lightmap(0, 240)` and
  `explosion.png` is pure white with binary alpha, so only fog can move it.
- Fog is server state (`DragonFightManager.processDragonDeath` ->
  `setVisible(false)` at server `deathTicks` 200), so the server led the
  client by 6 ticks here. On the synced recording
  (`scenario_dragon_kill_20260729T110941Z`) both clocks coincide: white
  cloud at t=462, recorded dt 201, one tick after the entity leaves the
  tape.
- Ruled out with measurements: `VALID_PLAYER` scoping (player stationary at
  67.2 blocks, hp constant, never leaves the 192 sphere), launch profile
  (both `fast`, `strip.overlays`), `MixinStripBossBar` (cancels
  `renderBossHealth` only, BossInfo and its fog stay live), a row/frame skew
  (geared t=448-452 match no original frame at any dt, MAE 16-19 vs ~2), and
  a latch tick offset (magma's population crosses the oracle's between t=456
  and t=460 rather than sitting at a constant sign).

Nothing in the tape exposes the server clock: `processDragonDeath`'s only
other observables are XP orbs and the gateway, and the recorder's entity
whitelist logs neither. Magma therefore keeps snapping at the one server
event the tape does expose (`deathTicks` 200 / entity removal), which is
correct wherever the recording is synced. On this tape that puts magma's
white-cloud window 6 ticks late, which flips the residual bright-puff
placement mismatch - the unrecoverable `EntityDragon.rand` / `Particle.rand`
offsets of divergence 40 - onto the magma-brighter side at t=454-458. Extent
and decay still track (bright px 10571 vs 10535 at t=454, 4531 vs 4233 at
456, 3804 vs 3876 at 460), so those three frames are filed in the tape's
`known_divergences.json` sidecar as divergence 40, scoped to
`[170, 280, 300, 460]` and ticks 454-458.

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
  and is unaffected; do not set `capture.hide_gui` on it. The t=260 / t=460
  texel-selection reading is retracted (CLOSED_DIVERGENCES.md, 2026-08-21):
  t=260 is 118 px of sidecar `known:4` shading-offset, t=460 has 0 clusters
  >=50 px. The tape still FAILs mild_shift at t=3080 and t=3540 (0
  UNEXPLAINED px each).
- `scenario_slime_bounce_20260723T001527Z` (tape retired 2026-07-30,
  superseded by `20260730T095754Z`; this shell contradiction is the ONLY
  remaining slime residual and every failed frame on the new tape is
  0-unexplained global-check, i.e. exactly this): the slime platform renders
  too dark. Baseline on the old tape: **15 failed frames, 6709 UNEXPLAINED px**.
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
  **Source and ordering closure (2026-07-27, wt/slimerim):**
  the adjacent-slime cull hypothesis is ruled out. `ModelBakery.java:685-698`
  puts every face whose JSON `cullface` is null into `generalQuads`; therefore
  slime's two six-face elements produce 12 general quads and zero per-facing
  quads. `BlockModelRenderer.java:93-110` calls `shouldSideBeRendered` only for
  per-facing lists and renders the null-facing list unconditionally.
  `BlockBreakable.java:37-55` does suppress a face against the same block type,
  but that method is never consulted for these 12 quads. Shared faces must
  therefore be present in the vanilla buffer.

  **Oracle TRANSLUCENT DRAW capture (2026-08-21, anvil, git `856c3ee`):**

  ```bash
  bash verify/mc_capture/capture_slime_translucent.sh
  ```

  Writes `verify/fixtures/slime_translucent/` (live Java, not synthesized):
  441 slime on `/fill -10 3 -10 10 3 10`, pose (0.5, 4.0, 0.5) yaw0 pitch0.
  `model_census.json`: every block `general_quads=12`, `face_quads=0`,
  `should_side` UP-only, layer Translucent (5292 = 441*12). `quads.jsonl`
  is TRANSLUCENT after `sortVertexData` (line order = draw order;
  sha256 `49fec3327691dbe9dcb896d77ae1747650355ed0e7e5ffd6f18bb01674fa3929`).
  `coverage.json` (MVP with chunk origin restored): n_single=0,
  n_multi=331012 of 854x480. Same-pose `frame_pair` A/B identical
  (maxch=0, fog_restored=1). Rim grind is no longer blocked on missing
  oracle draw evidence.

  The other two state hypotheses are also source-closed. Vanilla sorts
  TRANSLUCENT quads by descending squared centroid distance within each
  16-high `RenderChunk` (`RenderChunk.java:339-344`,
  `VertexBuffer.java:69-92`) and renders the layer with depth writes disabled
  (`EntityRenderer.java:1448-1466`). Magma already uses the same SRC_ALPHA
  blend without a translucent depth write (`cpu/raster_cpu.c:209-219`), so a
  first translucent face cannot selectively occlude the inset core through
  the depth buffer.

  A new combination experiment tested the two faithful source consequences
  together rather than repeating either rejected lever alone: emit all 12
  general quads for every slime and partition the C column mesh into vanilla
  16-high sections, sorting each section's six-vertex quads by the same
  descending centroid-distance key. It regressed **15 -> 17 failed frames**;
  UNEXPLAINED stayed **6709 px**, and t=50 whole/terrain error increased from
  **7.21/8.00 to 7.93/8.83 per channel**. Reversing both section and quad order
  was a direction diagnostic, not a proposed fix: it failed much harder at
  **19 frames, 73321 UNEXPLAINED px**, with t=50 whole/terrain
  **34.80/35.74**. Neither change is landed.

  **Remeasure 2026-08-22 (lane/slimerim, tape `20260730T095754Z`, CPU):**
  Isolated slime already FaceBakery-matches 12 generalQuads (72 verts).
  Interior 3x3 on stone: magma emits 6 inner verts (UP) and 0 outer DOWN;
  dump wants 36 and 6. Skipping neighbor cull to emit the 12 matched
  `quads.jsonl` (inner 3/16 inset 10.1875-10.8125 at block x=10, outer 0-1)
  but darkened the pad: t=50 whole/terrain **4.53/4.62 -> 23.88/23.36 /ch**;
  failed frames **13 -> 17**; t=140 UNEXPLAINED **0 -> 40447 px**. CPU
  raster drew a 3D inner-cube grid (both sides of inner/outer walls) vs
  Java's flat dual-top. Not landed. Closing the rim needs the hash-paired
  raster (cpu/cuda/metal), not different inset constants.

  The t=50 arithmetic explains why ordering looked plausible but also refutes
  it as the missing implementation lever. On 30205 selected dark-rim pixels,
  golden/candidate medians are `[113,185,95]` / `[91,148,76]`; on 2061
  already-dual pixels both medians are `[114,185,96]`. With
  `a=188/255`, a dual top has coefficient
  `1-(1-a)^2 = 0.930965`. A top plus two 0.8-shaded internal N/S faces behind
  has coefficient `a + (1-a)*0.8*(1-(1-a)^2) = 0.932940`, only
  `[0.24,0.40,0.20]` RGB above dual at the texture mean. That numerical match
  does not survive the actual source-defined quad population and order.

  **Gate-accounting correction:** in the current baseline, `pxdiff clusters`
  assigns the large dark platform clusters at t=50 to the semantic
  `particles`, `viewmodel`, and `hud` masks. The reported 6709 UNEXPLAINED
  pixels are the separate horizon-edge family, so a rim-only correction cannot
  make that counter approach its alleged ~750 floor without changing
  classification. The next non-fudged experiment needs a live oracle
  translucent draw capture (quad buffer plus post-transform fragment order),
  not another inferred cull/sort/depth variant.
  Open gap: how vanilla keeps the rim as bright as dual-top without the
  overshoot magma hits when fed full generalQuads. Blend equation itself is
  not the bug on dual-covered pixels.
- scenario_elytra_dip triage bullet: CLOSED 2026-07-30: re-recorded tape 20260727T214459Z is rc 0 with pixels clean after the native-resolution flow-texture fix (d5ce4fe); old-tape forensics preserved. Full entry in CLOSED_DIVERGENCES.md.

- scenario_ender_dragon_20260722T093713Z triage bullet (stale tape): CLOSED: tape superseded by 094040Z (and 20260730T093740Z, both rc 0). Keeps the do-not-fix warning: never call gm_dragon_init from set_dimension; replay ghosts already render the recorded dragon. Full entry in CLOSED_DIVERGENCES.md.


### CPU/CUDA replay parity: closed, keep sweeping

CLOSED: CPU and CUDA replay outputs match; keep sweeping both backends after renderer merges (rebuild magma_game_cuda too, or the gate scores stale frames). Full entry in CLOSED_DIVERGENCES.md.

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

CLOSED 2026-07-30 (negative result retained): the band was never a cull mismatch; root cause was camera eye height. Full entry in CLOSED_DIVERGENCES.md.

### slime_bounce: horizon band CLOSED, shell contradiction isolated (2026-07-30)

CLOSED 2026-07-30: sneak eye height 0.08F (65ea82a) + duplicate offset removed in frame_capture. Residual = the shell inset contradiction, still OPEN in the triage section. Full entry in CLOSED_DIVERGENCES.md.

### slime_bounce horizon band: fog-blend decomposition (wt/horizonfog, 2026-07-27)

CLOSED 2026-07-30 with the horizon family (sneak eye height); full decomposition preserved for reference. Full entry in CLOSED_DIVERGENCES.md.

### fogColor1 non-convergence at recstart

CLOSED 2026-07-30: recorder writes fog_color1 into the tape header, replay seeds MAGMA_FOG_C1_INIT from it; all 2026-07-30 re-records carry the field (slime t=0 clean, elytra_dip header 0.99999976). Legacy tapes keep the steady-state seed. Full entry in CLOSED_DIVERGENCES.md.

### Rain sky, particles, and lightning remain (lightmap sun is wired)

Lightmap `f` now follows `World.getSunBrightnessBody` (`World.java:1572-1580`),
including the rain/thunder factors at 1578-1579. Tape `rain`/`thunder` are
`getRainStrength(1.0F)` / `getThunderStrength(1.0F)` (`Recorder.java:8213-8214`;
thunder is already rain-weighted). `cr_lightmap_rgb`'s last two args are torch
flicker and gamma, not weather; `updateLightmap` (`EntityRenderer.java:892`)
takes sun brightness as `f` after those factors. Live play keeps rain=thunder=0
(no rainingStrength fade).

`scenario_rain_thunder_20260821T093435Z` `--cpu`, 21 frames:

| | whole mean/ch | terrain | UNEXPLAINED px | failed frames |
|---|---|---|---|---|
| before | ~75.7 | ~70.5 | 7217585 | 21 |
| after | ~50.9 | ~41.5 | 2140760 | 21 |

Arm R/G on this tape 0.46 -> 0.99 (matches golden except t=180 lightning, 1.57).
Still FAIL: rain particles, `getSkyColorBody` rain mix (sky stays clear),
`lastLightningBolt` lightmap override. Tape header declares
`container_identity`/`gui_clicks` with no events so replay is fail-closed rc=2
regardless of pixels. Do not treat the canonical tape's `known:12` rain class
as the evidence.

Oracle tape (2026-08-21, anvil, git `aa43667`):

```bash
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
bash verify/scenarios/run_scenario.sh verify/scenarios/rain_thunder.yaml
```

`verify/tapes/scenario_rain_thunder_20260821T093435Z.jsonl` (sha256
`37a7d62ee9cca0daa418fcc859101b1e94cd2c4538b6cdf2a0ac29951a30d972`):
header `rain_strength=1.0` `thunder_strength=1.0`; all 209 tick rows have
`rain=1.0` `thunder=1.0`. World snapshot (anvil, 6448975 bytes):
`verify/tapes/scenario_rain_thunder_20260821T093435Z_world/` (sorted-file
sha256 `bf6bb57d4accf15cf07357a4a4ab759295397d5c8dd39097e6e5391e50fc8673`).
Recipe: `verify/scenarios/rain_thunder.yaml` (`/weather thunder 1000000`,
`doWeatherCycle false`, 160-tick strength ramp).

### Remaining isolated render features

- One-frame loading sky after a dimension transfer.
- Enchantment glint.
- Chest model rendering and world-layout seed parity.
- Arrow ghost pitch on legacy tapes.
- General held-item registration outside the pinned use poses. (The rim
  edge-shading half closed 2026-07-30: generated-item rim quads now invert
  WEST/EAST normals per Forge ItemLayerModel.)
- Sheep grazing/head pose is only partially matched
  (scenario_sheep_grazing_20260730T092648Z replays rc 0, so the residual sits
  under gate thresholds; not re-measured pose-by-pose).
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

### Recorder gaps proven 2026-07-30 (overnight flywheel)

1. Explosion particle clouds are unreproducible from any tape:
   `doExplosionB` particle spawns consume client `world.rand`, and the tape
   records neither RNG state nor particle instances. A substitute-seed
   reconstruction visibly differs and fails the gate (creeperpix delegate,
   fix_creeperpix findings). Physics through explosions IS exact via the new
   `expl` packet capture (MixinRecordExplosion, additive knockback + block
   clears in replay). Next recorder step: whitelist particle-instance capture
   in `ParticleManager.addEffect` for explosion classes; this also makes the
   dragon-death white puffs deterministic. Affected today: tnt_explosion
   (t=30 blast cloud, partly classed UNEXPLAINED because the particles
   brightness heuristic misses cloud edges), creeper_encounter.
2. FIXED 2026-08-02 (recorder opt-in): elytra flag-7 arming round-trip varies
   per recording - a 2-tick `elytra_flying_pending` model makes
   scenario_nether_elytra_20260729T110024Z physics-exact (351/351) but breaks
   scenario_elytra_dip_20260727T214459Z at tick 59, and the 1-tick model does
   the reverse, because the CPacketEntityAction -> metadata round trip on the
   integrated server is not a constant. Fix: MixinRecordPlayerMetadata records
   the observed SPacketEntityMetadata flag-7 arrivals (`flag7_metadata`/`f7`),
   replay forwards them as `set_elytra_flag7`, and the same recording round
   also captures the pre-travel rotation (`look_phase`/`ry`/`rp` at
   ClientTickEvent.START -> `set_look_pre`) since WHICH side of travel a look
   change lands on also varies per recording (nether_elytra armed with the
   new pitch; the fresh-world t371 walking turn used the old yaw).
   scenario_elytra_dip_20260803T032614Z: physics-exact 520/520 at 1e-9, all
   state gates pass, pixel rc=0. scenario_nether_elytra_20260803T033526Z:
   physics fully clean (incl. hp) through terminal death at t=110, java-mode
   world hash 0 mismatches; its pixel FAIL is items 5/9 debt, and takes that
   wall-crash earlier instead hit item 17. Legacy tapes byte-identical
   (sha-proof on both old elytra tapes).
3. Tape headers record no gamerule state. silverfish_encounter runs
   `naturalRegeneration false`, replay simulates the vanilla default, and hp
   diverges 0.4 at t49 (= 1.0 silverfish damage x Resistance III 40%
   residual: the damage amount itself is exact), then food/exhaustion drifts
   at t361. Suppressing only regeneration in an instrumented replay removes
   both. Recorder fix: serialize gamerules at recstart; replay consumes them.
   Affects any scenario relying on non-default gamerules for survival stats.
4. falling_blocks records sky-only goldens deterministically (4 takes,
   including phased tp-first staging and a 400-tick settle): selection
   wireframe and hotbar render, so the client HAS block data, but chunk
   meshes never build during this specific scenario while same-session
   neighbors record fine. Needs live oracle debugging; all four takes retired
   from the sweep.
5. The nether_elytra world snapshot lacks transient lavafall cells
   (x=-123, z=-86, y=32..46); a counterfactual fill reproduces the golden.
   Needs dynamic-fluid snapshot capture; re-recording alone will not fix it.
6. Magma has no gravity-block cascade: sand/gravel never convert to
   FallingBlock entities on support removal, so the world blocks diverge
   from the first conversion tick while render stays plausible (tape ghost
   views draw Java's falling entities over magma's still-floating column).
   Caught 2026-08-01 by the Java/C world digest gate on the first VALID
   falling_blocks take (scenario_falling_blocks_20260801T151855Z): ticks
   0-19 digest-identical, dig applies one tick late (Java t20, magma t21,
   same digest value - separate small skew), first cascade divergence t22,
   magma world frozen from t30 while Java evolves through t59. Fix needs a
   real falling-block sim (convert on support removal + neighbor updates +
   landing re-block); the 1-tick dig skew deserves its own look at the
   attack-input replay alignment.

Status updates 2026-08-01: item 3's recorder half is DONE (recstart now
serializes all gamerules into the tape header); replay-side consumption is
still open. Item 4 no longer reproduces: the 2026-08-01 falling_blocks
takes record real terrain goldens (chunk meshes present), so the scenario
is recordable again - its gate now fails honestly on item 6 instead.

Status updates 2026-08-01 (evening wave): item 3 is CLOSED end-to-end -
replay consumes header gamerules (naturalRegeneration/doDaylightCycle/
doWeatherCycle honored, rest deliberately inert), and the recorder now
reads the INTEGRATED SERVER's rules instead of the join-time client copy
(only doDaylightCycle ever synced client-side, via SPacketTimeUpdate's
negated worldTime - which is why it alone recorded correctly). Verified:
silverfish_encounter 175112Z records naturalRegeneration=false truthfully
and replays physics-clean where 172741Z diverged at t101. Item 6 cascade
is LANDED (BlockFalling delay-2, EntityFallingBlock motion/landing,
neighbor schedule, creative GameType into the dig controller). Re-ran
151855Z on 2026-08-22 (lane/fallblock, Mac CPU): world_hash 309/310,
first mismatch t46 java=f63a2e55f4417889 magma=8d22d846ed0c2a49,
reconverge t47. Documented t22 cascade break and t30 freeze do not
reproduce. Dig Java-t20 / magma-t21 skew is gone under the gate's
tape[t] vs magma row[t] compare. t46 is OPEN: held creative
blockHitDelay is 1 on the re-landed cell (PlayerControllerMP
onPlayerDamageBlock delay>0 decrements and returns, no break). Strict
Entity.getCollisionBorderSize 0.0F regresses t25 (285 hash mismatches).
`t_ent < t_block` (test H) is required and landed; it is not enough.
Native: `bash magma/game/test_fall_reanchor.sh` PASS (A-H). Mac CPU
frames on this tape are sky + selection box without a terrain mesh
(86%/ch); that is render, not the digest contract.

Pixel triage 2026-08-01 (full report:
~/dev/nw/pxtriage_reports/pxtriage_20260801.md, covering the
three state-clean rc=3 takes) yields four new tracked items:

7. Dragon fireball billboard renders too bright/saturated vs the oracle's
   muted purple (silhouette also differs). Path: entity_render.c
   billboard for EntityDragonFireball -> item 9003 ->
   emit_fireball_billboard() forcing light=1/blk=15/white tint. Confirmed
   by eyeball on dragon_kill_geared 175614Z t=181..224 (17 frames, 1897
   px). Needs an atlas-vs-lighting A/B to pick the exact fix.
8. Recorder gap: entity rows lack prevPosX/Y/Z and limbSwing state, so
   Java's interpolated render pose cannot be reconstructed for close-range
   articulated mobs (silverfish 175112Z t=260, 15704 px across 10 frames;
   ghost STATE matches exactly, only render pose diverges).
9. Recorder gap: one-tick death-screen transition state (death timer,
   HUD/hurt/game-over in-frame ordering) is not captured; water_dive
   173755Z t=990 trips the global mild_shift detector with zero
   unexplained clusters. Cosmetic-only.
10. Oracle capture-loop artifact: dragon_kill_geared 175614Z golden frame
    hashes REPEAT (t=14==t=32, t=16==t=34, one frame spans t=22..30 and
    again t=40..50, recovering at t=52). Take-level recording artifact,
    not a magma defect; re-record if this take is ever promoted.

The dragon death-cloud unexplained px (17175 across t=266..474) were
filed as a pcl-consumption gap. pcl consume is CLOSED (`spawn_particle`
from tape `pcl` rows; `make -C magma test-particles-live` PASS). Remaining
death-cloud placement noise is the unrecorded `Particle.rand` item, not
a missing consume path. Full forensics in CLOSED_DIVERGENCES.md.

Interactive-play sweep 2026-08-01 (first human session on the Mac Metal
windowed build; every item below is from the interactive path that no
pixel gate renders - the windowed-path blindspot class):

11. FIXED: cutout draw buffer overflow abort at seed-0 plains spawn, vd8
    (262,932 verts > 262,144 cap; tall grass). Cap doubled to 524,288;
    measured peak documented in magma.conf. Tape peaks never passed 33K
    because no pinned tape renders a plains spawn at rd8.
12. FIXED: live sheep/pig/cow/chicken now run their vanilla 1.11.2
    priority/mutex task lists for swim, panic, eat-grass (sheep),
    wander-avoid-water, watch-closest player, and look-idle. Movement and
    look helpers carry the task outputs into motion and rendered head pose;
    sheep panic uses 1.25 * the 0.23 movement attribute. Mate, tempt, and
    follow-parent remain dormant because live play has no breeding state.
13. CLOSED 2026-08-21: entities no longer x-ray through translucent
    water. `window_compose.c` and `frame_capture.c` both draw SOLID..CUTOUT
    exclusive, then entities/particles, then TRANSLUCENT.
    WR-ENTITY-WATER-OCCLUSION ALL PASS (behind 0/812, front 5016/5016,
    half 170/905). Forensics in CLOSED_DIVERGENCES.md.
14. NOT A DIVERGENCE (source-verified): sand placed directly above tall
    grass stays put in vanilla 1.11.2. BlockFalling.canFallThrough is
    fire/air/water/lava materials only; tall grass is Material.VINE
    (BlockTallGrass.java:34), so updateTick's fall condition fails. The
    fall-through-replaceable-plants rule is a later-version behavior.
    Open sub-case to verify: a falling sand ENTITY landing INTO a tall
    grass cell must replace the grass (EntityFallingBlock landing
    setBlockState); qrl-record if touched.
15. FIXED 2026-08-01: dropped block mini-cubes used one `IR_CUV` map for all
    faces, bypassing the placed-block FaceBakery corner order and 0.999/0.001
    inset. The EntityItem cube path now consumes `rk_facebakery_make_quad`
    output directly. The registry census covers 49 cube item ids / 769 states /
    4,614 faces; every dropped UV is bit-identical to a fresh placed-face bake.

16. CLOSED 2026-08-21: `WorldServer.createSpawnPosition` is ported
    (`magma/game/world_spawn.c`). Seed 1000 world spawn is (168, 64, 252)
    and seed 0 is (44, 64, 176), equal to oracle `qrl_<seed>/level.dat`
    SpawnX/Y/Z. Magma interactive default uses that xz + 0.5 and
    `getTopSolidOrLiquidBlock` Y, not (8.5, 70, 0.3). Oracle player NBT
    offsets are Forge spawnRadius fuzz (Malmo reseeds it); tapes use
    set_pose. Pin: `make -C magma test-world-spawn`.
    Forensics in CLOSED_DIVERGENCES.md.

17. OPEN 2026-08-02: elytra fly-into-wall kinetic damage is server-
    authoritative in both tick and amount, so magma's locally computed hit
    diverges. scenario_nether_elytra_20260803T032911Z: client collision at
    tick 70 (vx -1.2091 -> 0), magma applies speed*10-3 = 9.091 immediately;
    the oracle's recorded hp drop lands at row 72 (SPacketUpdateHealth round
    trip) and is 10.21 - implying the SERVER's own tracked speed (~1.321),
    not the client's. The 1.12-hp gap decided a death: magma hit 0 at t=101
    while the oracle died a tick earlier at t=100 with a different total.
    Fix shape is the flag-7 pattern again: record health-update packet
    arrivals (tick + value) and let replay apply recorded server damage
    instead of local kinetic computation - but hp is a gated physics field,
    so the design must keep the hp gate meaningful for locally simulated
    damage classes (fire, fall) while pinning only server-computed events.
    Filed, not implemented.

### Human tape campaign 2026-08-03

The 10,973-tick seed-80302 survival recording
`20260803T055113Z_vanilla_s80302_survival_default_rd8_837eae74` is the first
long human tape with a recstart world snapshot, per-tick inventory keyframes,
and Java nearby-world hashes. Its current full state replay still first exceeds
the physics tolerance at t1475 (`x`, 8.50e-5 blocks), ends 43.4306 blocks away,
and trips the inventory and world gates. The inventory gate returns its
20-entry mismatch cap, beginning with GUI-created/moved items at t668.

Landed infrastructure and simulation corrections from the campaign:

18. FIXED: Recorder nearby-world FNV canonicalizes the upper half of
    `BlockDoublePlant` through the same lossy metadata domain as magma.
    Live metadata 10 previously hashed as 11 after
    `getStateFromMeta(getMetaFromState(state))`, causing a false t0 world-gate
    mismatch despite byte-identical snapshot terrain. The synthetic hash test
    pins the measured plant cell and both packed identities.
19. FIXED: survival digging now mirrors Minecraft's `leftClickCounter=10`
    press-miss cooldown and 4.5-block survival reach instead of the creative
    5.0-block reach. The focused controller matrix, full scripted survival
    route, real-vs-batched CPU gate, and CPU-vs-CUDA gate pass. The route's
    wooden-pick damage moves deterministically from 17 to 14 because three
    formerly out-of-reach blocks are no longer mined.
20. FIXED FOR NEW TAPES: every successful client `World.setBlockState` final
    is recorded in exact call order as `bc`, then replayed as same-tick
    post-action `set_block_post` re-anchors. This covers client mutation paths
    that route through `World.setBlockState`, including ordinary packet
    updates, local prediction, neighbor cascades, and fluid edits, without
    inventing edits for legacy tapes. Partial bulk
    `SPacketChunkData -> Chunk.fillChunk` (Forge `clumpingThreshold`, default
    64) is now captured by `MixinRecordChunkFill` onto the same `bc` channel
    (changed cells only, deterministic section/index order). Full `loadChunk`
    terrain streams and recstart/dim-download handoffs are intentionally not
    recorded (MCA snapshot + existing forcedLoading clear). The setBlockState
    mixin was runtime-smoked in the real client and the C next-tick collision
    regression passes.
21. FIXED FOR NEW TAPES (player / workbench / furnace / single chest): ordered
    `GuiContainer.handleMouseClick` calls are recorded as `gclk` with
    `windowId`. Open/close edges carry `gopen`/`gclose` with gui class,
    window id, ctype (`player`/`workbench`/`furnace`/`chest`), world pos
    (from TE or last right-click block), container-side slot seed, cursor,
    and furnace burn/cook prop. Replay emits `container_open` /
    `container_slot` / `container_cursor` / `container_click` /
    `container_close` and applies them only through magma
    `container_live` + TE tables. Fail closed on missing/mismatched
    identity, unsupported ClickType, double chest, legacy non-inventory
    5-field `gclk`, or malformed payloads. Still blocked: double chest,
    enchant/brewing/dispenser/hopper/beacon/shulker/horse/merchant,
    SWAP/CLONE/QUICK_CRAFT/PICKUP_ALL. `gui_view`/`gslots`/`gcur` remain
    render truth and never invent clicks.
22. FIXED IN A FRESH ORACLE FIXTURE: `survival_campaign_auto` now records
    `dig_trace` and exercises the human tape's sustained mining, miss cooldown,
    tool changes, GUI open/close, chest/furnace, pickup/throw, and flowing-water
    paths on seed 917351. The 4,810-tick replay is exact for all six pose and
    velocity fields at `1e-9`; inventory (252 independent ticks), entities
    (6,687 rows), Java world hash (4,810 ticks), and dig trace (4,809 paired
    ticks) all pass.
23. FIXED: the player controller now preserves vanilla's just-finished
    `currentBlock` until `onPlayerDestroyBlock` clears it, translates active
    controller targets across floating-origin recentering, and pins
    `leftClickCounter=10000` while a block container is open. Closing the GUI
    clears the sentinel. Recorder and replay expose the controller/raycast
    dig-state fields through an opt-in, phase-aware fail-closed comparator;
    render-sampled ray observations are matched to the measured adjacent
    controller phase instead of being treated as same-tick state.
24. FIXED: shared survival semantics now include lone/double chest collision,
    furnace ROCK pickaxe effectiveness and harvest level, empty-to-filled
    furnace cook-time initialization, outside-GUI THROW as vanilla's accepted
    no-op, post-move water-entry current application, and Java's deliberate
    float `MathHelper.sqrt` rounding during liquid-flow normalization.
25. FIXED: `GuiInventory` now follows `InventoryEffectRenderer`: any active
    potion moves the main panel and hit boxes to the vanilla shifted origin and
    draws the status panel, icon, English vanilla name, amplifier, and duration.
    On the campaign's Resistance IV inventory frame t380 this reduced
    whole-frame error from 35.48/ch to 1.30/ch; the post-fix `pxdiff survey`
    reports no inventory-panel cluster.


Still open on this recording:

- The old tape predates `bc` and `gclk`, so its GUI-created inventory and world
  mutations cannot be retroactively recovered. Post-tick inventory comparison
  is now aligned to the replay's t+1 re-anchor, exposing real missing items
  rather than one-tick false failures.
- The original human recording has no opt-in dig-state trace. Its first
  same-anchor mutation split remains Java t1232/t1233 at grass `(116,65,324)`
  plus tall grass above it: Java exposes an intermediate plant-only removal
  hash for one tick while magma removes both in one neighbor-cascade tick, then
  both reconverge immediately. Later streams separate and leave grass
  `(113,65,326)` solid at the t1475 collision. The fresh 4,810-tick campaign
  above is exact with the corrected controller and proves the general paths,
  but cannot retroactively identify which legacy client/server event was
  missing from this old tape; a coordinate special case remains forbidden.
- The recording's goldens were produced by the Mac OpenGL path, while current
  magma replay frames use the Linux renderer. That invalidates absolute
  HUD/icon and mean-per-channel gate claims. The frames still prove large
  content/pose failures after the worlds separate, but renderer work needs a
  same-platform calibrated baseline or a re-record.

- **Auto-campaign pixel causes (2026-08-03, seed 917351, Linux/GL, hide_gui=off):
  physics+state verified clean, so every pixel diff is pure rendering.** Measured
  via pxdiff survey (t80/180/240/1140/2000) + probe/pixels; camera-move and
  hideGUI-golden families ruled out (pose byte-equal, in.sneak=0 always).
  Enumerated:
    1. World/luminance fog-fidelity wash (UNEXPLAINED, largest): giant horizon
       clusters, candidate brighter+smoother vs golden's darker gradient.
       NOT a new bug - this is the documented fog family (CLOSED_DIVERGENCES
       slime_bounce horizon-band): magma already matches the vanilla radial fog
       ramp to 0.0015 RMSE and retuning fog_start/end papers over a sub-pixel
       view/projection registration lead. Do not retune GM_TERRAIN_FOG_*.
    2. Viewmodel (hand) near-black (viewmodel class, 2nd): lower-right quadrants
       render flat-dark (candidate lum_std ~1.7, golden shovel/hand browns).
       Isolation ui_hud hands are lit (`gm_hand_set_env(NULL, 15, 15, 1,1,1)`).
       Tape/window path binds the frame lightmap LUT and samples
       `light_sky`/`light_blk` at `(floor(x), floor(y+eye_height), floor(z))`
       (`frame_capture.c` / `window_compose.c`). Combined-light 0,0 would make
       the LUT texel near-black while terrain can still look lit from baked
       mesh light. Opposite extreme of the documented "arm far too bright"
       on another tape. Not re-replayed this lane (4810 ticks). Repro: replay
       `scenario_survival_campaign_auto_*` t=80, print those eye-block levels
       next to the viewmodel pixels.
    3. HUD hotbar darker + slight placement delta (hud class, small but
       persistent). Isolated HUD chrome is bit-exact (`hud_durability_half`
       and the other CORE_HARD rows `hard_px=0` vs ui_hud goldens). The
       campaign residual is hotbar-over-world (widgets.png a=186 via
       `hud_blend_px_tex`), not the isolated icon blit. Not re-replayed
       this lane. Do not retune HUD blend to paper over world underlay.
    4. Particles: RESIDUAL 2026-08-21, not an additive-blend miss. The
       2026-08-03 "missing additive glow" read is pixel_gate's `particles`
       class (oracle brighter by >12), which is a brightness heuristic,
       not a GL blend probe. Vanilla 1.11.2
       `ParticleManager.renderParticles` (oracle-src line 283) is
       `blendFunc(SRC_ALPHA, ONE_MINUS_SRC_ALPHA)` +
       `alphaFunc(GL_GREATER, 0.003921569F)` for FX layers 0..2. No
       Particle*.java in that dir switches DestFactor.ONE for the pass
       (ParticleFlame only overrides brightness). Magma layer 0 already
       matches: `blend=1`, `alpha_ref=0.003921569f` in frame_capture.c
       and window_compose.c. Do not set blend=3; that would miss vanilla
       and likely regress dragon-death explosion tapes.
       Magma reconstructs only BLOCK + EXPLOSION_NORMAL/LARGE/HUGE
       (`MixinRecordParticles` whitelist). Campaign digging is
       ParticleDigging (FX layer 1, terrain atlas, same src-over). Crit /
       spell / heart / sweep from the campaign zombie are not
       implemented. Particle.rand placement stays a separate forbidden
       item. Tape on anvil:
       `verify/tapes/scenario_survival_campaign_auto_20260803T082651Z.jsonl`
       (4810 ticks, 241 frames, seed 917351). Repro of the blend fact:
       `ParticleManager.java:282-284` vs magma `ps.blend = 1`. A 4810-tick
       pixel re-gate is the verify after a particle-type port, not after
       a blend flip.
    5. Minor cutout-sky+/thinline edge (noise level) - mesh_mc.c cutout layer.
  Priority for a renderer grind: 2 then 3 (each needs the full
  4810-tick auto-campaign pixel re-gate to verify; 1 is forbidden above;
  4 is not a blend knob).

Micro-regression priced 2026-07-30: nether_elytra t=63 gained 2409
unexplained px (7 clusters, largest 1463) relative to its 2026-07-29
baseline after the night's renderer merges; physics still 351/351. Baseline
intentionally left old so the delta stays visible until attributed.

## Verification commands

```bash
make -C magma test-game
bash verify/ui_hud/run_ui_hud_gates.sh
bash verify/ui_entities/run_oracle_gate.sh
bash verify/mc_capture/run_gui_actions_verify.sh
bash verify/mc_capture/run_gui_verify.sh
```
