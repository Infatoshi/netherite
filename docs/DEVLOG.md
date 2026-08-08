# DEVLOG (compressed)

Code and goldens are ground truth. Short history only; full agent map is root
`AGENTS.md`. Git has the long form. Old one-shot reports: `docs/archive/`.

## What shipped (by tree)

### java/
- Playable 1.11.2 Forge+Malmo on anvil only (Mac native GL dead).
- qrl bridge: reset/step/obs, overclock, recstart/recstop tapes, tick-boundary frames.
- Drop-in JNI: sin, lightmap, biome tint, AO (`c/render-opt/dropin`).
- Play: mcwindow (framebuffer stream) or Moonlight; agent stack Xvfb :1 + VNC.
- WorldGenProbe / coverage hooks for worldgen and render path attribution.

### c/render-opt/ (effectively complete)
- 39/40 kernels bitwise vs real MC; k26 closed-by-integration (atlas UVs non-deterministic).
- Whole-frame stripcheck: native drop-ins == vanilla Java at 0 px on pinned course
  (chat/name nondeterminism pinned out).
- Intentional non-ports: GL raster, atlas stitch. Optional: full rebuildChunk VBO drop-in.

### c/mc-sim/ (kernel farm done; product wiring continues in craster)
- Dual-compile core: CPU oracle + CUDA (mostly one-thread-per-env for CPU==CUDA gates;
  real batch RL needs stage-split GPU, not serial device threads).
- Waves 0–14 verified: worldgen (OW/nether/end/flat), structures (stronghold/fortress/mineshaft),
  populate, fluids/light CA, physics, combat, crafting/smelting, portals, dragon subsets,
  tick compose, cuda_batch_tick, py_gym smoke, SPS bench.
- Fidelity rule: runtime internal consistency (CPU==CUDA); worldgen vanilla LCG seed-faithful.
- Live-game worldgen: multi-seed ~99.97% cell match via genprobe flywheel (stale-skylight,
  big-tree carry, Forge extraTreeChance, ice/snow, biome transpose bugs found and fixed).
- Open kernel-side notes (not full queues):
  - `populate` Golden.java lags stale-skylight + leaf-soil mushrooms (real oracle = world_diff).
  - GPU worldgen K1 noise: GO for many-env RL (~4.6x one CPU core); K2–K6 not built.
  - Many units are CPU==CUDA only until wired through live Java tick traces.

### c/craster/ (product binary)
- Software raster CPU+CUDA; game loop uses mc-sim headers for worldgen/sim slices.
- Macro: `test_route_e2e` seed 0 empty inv -> legally `won` (travel injects only).
- Human 12k tape: physics 1e-9 clean through t9810; t9811 = evolved-save water (fresh-world rule).
- 12k frame replay perf: ~29s -> ~8.4s (device meshes, GPU sky, pipeline, hi-z, npy frames).
- GUI table/furnace pixel-gated vs Java; product gaps listed in PRODUCT.md.
- Nightqueue 2026-07-11 items closed (lightmap, canopy root-cause, swamp M 134, etc.).

## Hard lessons worth keeping

- Goldens = real MC only (verbatim Java or live capture). Never port-vs-port.
- C needs ordered temps for multi-RNG expressions; `-ffp-contract=off` / `--fmad=false`.
- Kill game: `pkill -9 -f '[G]radleStart'`; launch game in a standalone setsid call.
- Physics tapes: human + fresh world + worldgen-verified seed. Driven tapes zero motion after land.
- CUDA twin per serial unit is a verification tax, not a throughput claim.
- Doc sprawl (kernel READMEs, WORKQUEUE, dual DEVLOGs) was agent-flywheel cost; purged 2026-07-11.

## Full-parity expansion (2026-07-29)

The user promoted the former redstone/audio/side-structure/ecology cuts and the
optional villages/enchanting/brewing/weather bundles into an ordered full
Minecraft 1.11.2 parity backlog. They remain disabled in the fast base profile
until complete. The execution order, Java fixtures, and performance budgets are
in `c/magma/PARITY_PROJECT.md`.

Multiplayer remains outside the current single-player boundary. Save/reload is
now in scope through the versioned state-capsule work needed by the Java-vs-C
oracle and eventual player-facing persistence. Render-opt reopens only for a
specific gated residual.

## Full-parity 40% checkpoint (2026-08-07)

The effort-weighted board reached approximately 40%. Live hopper/dropper/
dispenser automation, rideable/TNT/hopper minecarts and rail controls, complete
fishing cold-state/loot behavior, and exact live desert pyramids, jungle
temples, and swamp huts were promoted with real-Java focused gates. The quick
sweep passes 14/14, the runtime aggregate passes, and the performance guard
passes at 5,014 scalar steps/s, 2.87M CUDA environment ticks/s, and 30.77
1080p CUDA frames/s. The full sweep passes all 25 available steps, including
CUDA and raster parity; only the undistributed, locally recorded canonical tape
is skipped. Its GPU busy check now retries briefly after the sweep's own Blaze
benchmark so a stale NVML utilization sample cannot hide raster gates. Exact
open edges and evidence paths remain on `c/magma/PARITY_PROJECT.md`.

## Full-parity 50% checkpoint (2026-08-07)

The effort-weighted board reached approximately 50%. Exact recursive village
graphs now produce live roads, wells, farms/crops, biome-specific houses,
doors, blacksmith chest state/loot, and bounded generated resident
sites/professions. Dispensers gained Java-locked default, arrow, potion, fire
charge, firework, oak-boat, bucket, and TNT behavior. Fishing gained the exact
17-point first-person line centerline in interactive and capture rendering.
Runtime villagers, AI, golems/trading, broad automation edges, and final
fishing-line pixels remain explicitly open.

The focused Java/native gates pass, the quick sweep passes 14/14, and the full
sweep passes all 25 available CPU/CUDA/raster/state/RL gates with the one
documented undistributed canonical-tape skip. The native runtime aggregate
passes in 5:20.13 at 441,612 KiB peak RSS with no swap. The performance guard
passes at 4,869 scalar steps/s, 2.87M CUDA environment ticks/s, and 30.48
1080p CUDA frames/s. Evidence and the 15 `DONE`, 15 `ACTIVE`, two `QUEUED`
portfolio are recorded in `c/magma/PARITY_PROJECT.md`.

## Full-parity 60% checkpoint (2026-08-07)

The effort-weighted board reached approximately 60%. Interactive play gained
an ordered represented sound stream and optional fixed-pool OpenAL playback for
58 events/134 owned OGG variants. Generated village residents now materialize,
render profession models, and carry a Java-locked initial trade slice covering
11 career selections and 22 ordinary offers. Dispensers now cover twelve
behavior classes, adding eggs, snowballs, experience bottles, and
flint-and-steel air/failure/TNT paths.

The focused six-family promotion gate and quick sweep pass, and the full sweep
passes all 26 available CPU/CUDA/raster/state/RL steps with one documented
undistributed canonical-tape skip. The native runtime aggregate passes in
5:10.52 at 441,932 KiB peak RSS with no swap. The performance guard passes at
5,135 scalar steps/s, 2.87M CUDA environment ticks/s, and 31.36 1080p CUDA
frames/s. Exact closed scope, remaining gaps, and evidence paths are recorded
in `c/magma/PARITY_PROJECT.md`.

## Full-parity 70% checkpoint (2026-08-07)

The effort-weighted board reached approximately 70%, with high uncertainty.
This was a fidelity checkpoint rather than a breadth-only promotion. An
unopened NoAI villager now round-trips from the real Java game through the
shared state capsule and continues for 20 exact ticks, including its hidden
ambient-sound RNG consumption and first lazy trade after reload. The direct
comparison matches 26 observed state categories and all 10,625 block cells;
the unrelated player `death_time` remains explicitly unrepresented.

Atomic same-client entity captures make Java A/B exact for all 16 hard states.
The resulting C gate is honestly zero passes and 16 residuals, not hidden
behind capture noise. A raster quantization candidate that improved the XP
crop but regressed the whole scene was rejected. The CUDA weather/sky symbol
integration was repaired and CPU/CUDA raster output remains bit-exact across
all five gate cases.

The quick sweep passes 15/15. The full sweep passes all 26 available
CPU/CUDA/raster/state/RL gates with the one documented undistributed
canonical-tape skip. The performance guard passes at 5,094 CPU scalar steps/s,
2.87M CUDA environment ticks/s, and 31.05 1080p CUDA frames/s. Exact scope and
remaining gaps are recorded in `c/magma/PARITY_PROJECT.md` and
`c/magma/OPEN_DIVERGENCES.md`.

Removed 2026-07-11 (unused routes): `java/build_mac.sh`, `play_mac.sh` (Mac GL dead),
`render_poc.py` + `setup_python_env.sh` (MineRL venv path; qrl is the RL bridge).

## Where next work lives

- Fidelity: `OPEN_DIVERGENCES.md` + fresh human tapes (`VERIFY.md`).
- Product surface: `PRODUCT.md` remaining gaps (models, encounters, HUD coverage, bundles).
- Sim/RL: gym beyond smoke, live-Java entity traces, optional GPU worldgen K2–K6.
- Code: `c/mc-sim/core/`, `c/craster/game/`, `java/.../qrl/`.

## 2026-07-12

- Moved consolidated `~/games/minecraft` first-party markdown into `docs/legacy-games-minecraft-learnings.md` so the old tree can be deleted without losing rationale.

## 2026-07-12 (legacy cold pack)

- Packed `~/games/minecraft` keep-set into `~/dev/minecraft/legacy-cold/` (flashmine bundle, netherite csrc, mc-oracle tar, archive).
- Retargeted `c/mc-sim/ref/netherite-csrc` to `legacy-cold/netherite-csrc`.
- Deleted hot checkouts under `~/games/minecraft` (flashmine, netherite*, mc-oracle, backups).

## 2026-07-17 (iron pickaxe across the whole RL stack)

- Extended the chain to an iron pickaxe end-to-end: craft:6 (furnace, 8 cobble, table-gated),
  craft:7 (iron pick, 3 ingot + 2 stick, table-gated) and a `smelt` primitive (extract furnace
  output, insert iron ore, insert 1 coal fuel) in all five layers: `rl_mode.c`, `cuenv_core.h`
  (CPU+CUDA from one source), `cuenv.py`, the qrl JVM bridge, and reward/trainer.
- BOLR binary obs stays FROZEN; iron visibility rides a JSON-only `inv_iron` obs field
  ([furnace, iron ore, ingot, iron pick] full-inventory counts, mirrored on the qrl bridge)
  and the trainer status vector (CU_STATUS_K 13 -> 17). Root cause of a day of "no iron
  pickup" false alarms: the hotbar is full by the iron stages, pickups overflow to the
  backpack, and hotbar-only detection can never see them.
- chain_probe iron stages (IRON=1): cobble bank, underground kit (furnace crafted BEFORE
  picks so it lands in-hotbar), snapshot-oracle iron hunt (.bsnp region parse; camera
  ghost-target pruning since earlier churn destroys oracle cells - wooden pick breaks iron
  ore with no drop), face-adjacent walk-in collection, smelt, final table + craft:7.
  FULL CHAIN seed 16: spawn -> iron pickaxe in 4114 ticks, scripted.
- Verified: replay of the 4114-tick chain on cuenv CPU vs the real env = ZERO diffs
  (verify_cpu --chain --chain-seed 16); 64 CUDA lanes vs CPU byte-exact (verify_cuda);
  live JVM smoke over the qrl socket (craft:6 -> smelt 3 ingots -> craft:7) PASS.
- reward_chain grows 5 default-0 iron milestones (exact backward compat, tested);
  ppo_chain_cu widens craft head 7 -> 9 + smelt head under IRON_CHAIN=1 only, so
  chain_net_cu_v2.pt stays loadable.

## Iron overnight train 2026-07-17
- Verified CPU chain s16 zero-diff (4114 ticks).
- CUDA chain verify in flight then IRON_CHAIN=1 PPO on GPU0 (tmux iron-overnight).
- Recipe: SUCCESS_ITEM=257, reward_iron_abuse.json, N=3072, EP_DEC=2500, wall 8h, 9-stage curriculum.
- CUENV_MAX_SNAPS 64->128 for iron capture slots.
- Codex seed-harden 29/3 in tmux iron-seed-harden.
- Morning: bash c/craster/rl/out/morning_iron_status.sh

## 2026-07-20 (docs consolidation)

- Single agent entry: root `AGENTS.md`. How-tos/history under `docs/`
  (`RUNBOOK`, `BOOTSTRAP`, `GATES`, `DEVLOG`). Archaeology in `docs/archive/`.
- Living contracts stay next to code (`c/*/SPEC.md`, craster PRODUCT/VERIFY/OPEN_DIVERGENCES).
- Root no longer holds DEVLOG or product/report markdown.

## 2026-07-21 (bot-recorded canonical tape + torch placement chase)

- New canonical tape 20260721T215812Z: recorded end-to-end by progression_bot
  (no human input), 3,617 ticks, physics-exact at 1e-9, pixel gate PASS.
  CANON_TAPE swapped in netherite_sweep.sh; VERIFY.md/GATES.md updated.
- Renderer/sim fixes it surfaced: crack decal face mapping (frame_capture),
  torch viewmodel item/generated routing (hand.c), torch placement support
  validation + refire hit-face from AABB calculateIntercept semantics
  (player_ctl, sel_box ray_box_hit, blaze_core cu_ray_box_hit; OPEN_DIVERGENCES
  #55), cross-plant vanilla random offsets (mesh_mc), CUDA overlay pinned-buffer
  race (frame_capture).
- KNOWN ISSUE (RESOLVED same day, see below): blaze-cuda-chain appeared to hang
  on GPU1 (sm_86) at ~tick 185 of the s10 chain.

## 2026-07-21 (later: sweep fully green + fidelity round via agent fan-out)

- blaze-cuda-chain "hang" root-caused (codex): NOT a spin. The verify-helper
  k_emit rendered all 2,304 camera rays on a SINGLE CUDA thread; a camera
  change near tick 185 raised traversal cost enough that sm_86 looked wedged.
  Fix: k_emit_cam renders one thread per pixel, then the single-thread record
  assembly runs on the same stream (blaze_cuda.cu). Chain gate byte-exact on
  GPU1/sm_86 in ~120 s (was: killed at 1200 s), semantics unchanged.
- magma-test-config re-enabled: test_config.c capture buffer 1024 -> 4096
  (usage text had outgrown it).
- Viewmodel fidelity (codex): vanilla ItemRenderer equip lower/raise + retained
  stack, swing restart at half animation, ItemLayerModel per-texel rim quads.
  Canonical tape: viewmodel 518,463 -> 470,440 px, hud 471,103 -> 397,903.
  tests/test_hand_torch.c updated to assert the exact per-texel topology.
- HUD fidelity (codex): hotbar durability strip (renderItemOverlayIntoGUI),
  meta in GmPlayerView, exact GUI mini-cube lighting/UVs. hud -5,332 px more.
- Particles finding (codex, honest negative result): the 'particles' gate class
  is misnamed - pixel_gate classifies any oracle-brighter cluster as particles;
  the dominant 87k cluster is a broad terrain-LIGHTING divergence during the
  dig windows (t3080-3160, t3320-3400). Real debris is minor and RNG-unmatchable;
  implementing it made the class worse, so it was rejected. Follow-up agent is
  chasing the lighting root cause (world/light.c incremental relight suspect).

## 2026-07-22 (brute-force triptych round: crack/stone/foliage/cave-light)

- Method change per operator: rank keyframe diffs (grind.py), eyeball the
  triptychs directly, characterize the artifact class visually, then hand the
  numerics to a codex round. Four root causes landed this way.
- Crack overlay (codex): block-damage overlay now renders full model faces
  with per-face crack UVs (vertical faces were mirrored, bottom rotated 90);
  stage = floor(progress*10)-1 per PlayerControllerMP.
- Polished stone (codex): granite/diorite/andesite metas 2/4/6 no longer fall
  through to plain stone; model-oracle coverage added.
- Foliage lighting: compute_skylight_spread read the renderer block id
  instead of the packed vanilla state for sky opacity; cube faces now use
  vanilla useNeighborBrightness (mc_light_for_ext) and cross-plants take
  neighbor combined light. Sidecar #40 filed: oracle-only ParticleDigging
  burst at t3160, surfaced once foliage skylight matched.
- Cave/canopy brightness (my measurement, codex root cause): ~64 mid-tape
  frames sat at a flat 9.06 mean/ch with magma/oracle = 1.2286 exactly,
  channel-uniform. Cause: light_set_state seeded EVERY state load with sky 15,
  so metadata-only leaf loads clobbered stored skylight under the canopy
  (Moody lightmap bytes 197/160 = 1.23125 - the measured scalar). Fix follows
  World.checkLightFor: metadata-only loads keep stored skylight; opacity
  changes re-derive via canSeeSky + flood. Tape median 8.69 -> 0.16 mean/ch;
  plateau frames 9.1 -> 0.01. Gate PASS both repos; quick sweep green.
- Worktree gotcha recorded: tracked sidecar json is NOT symlinked into
  worktrees - editing the main-tree copy silently leaves the worktree stale
  (cost one full replay to spot: active entries [] at the failing tick).

## 2026-07-22 (scenario harness: scripted combat/GUI/anim environments, six-round codex fan-out)

- New transferable scenario harness (raster/verify/scenarios/): YAML spec ->
  oracle boot -> phased setup (one runcmds batch per command + settle ticks;
  a single batch executes in ONE server tick, so tp-dependent fills/summons
  fail; vanilla also reports no-op clear/fill-air as failure) -> mcwindow
  input script -> tape -> replay gate. setup_qrl passes raw bridge steps
  (dim {id:1} for End entry).
- Eight tapes all rc=0 with gate PASS, each divergence root-caused by a codex
  round and re-verified here: canonical, smoke zombie, blaze melee, blaze
  bow, pigmen aggro, wither skeleton, enderman fight, ender dragon.
- Smoke zombie harvest: zombie melee was 4.0 (vanilla 3.0) and replay
  double-applied saturation regen across packet timing (13/6 vs 4/3 hp).
- Night scene ~2x dark: bulk snapshot loads bypassed Chunk.generateSkylightMap
  semantics (light_load_state now re-derives column skylight per batch);
  7.51 -> 1.47/ch. Pink hotbar icon = missing diamond_sword GUI atlas entry.
- GUI actions (8-step scripted inventory sequence, cursor-driven): paper doll
  (drawEntityOnScreen), hover highlight/tooltip (GuiContainer/GuiUtils),
  armor/offhand placeholder sprites, achievement-toast harness contamination
  (pre-grant achievement.openInventory). Panels 1.2-4.2 -> 0.15-0.26/ch.
- Texture animation: replay never seeded total_time, so every animated tile
  ran from clock zero (portal masked it via recorded frameCounter). Lava
  (20-frame fwd/rev) + fire (custom sequences) implemented; underwater
  overlay burst was sparse ppos anchoring, now per-row anchors. Negative
  control (time-shifted candidate) now genuinely separates per region.
- Combat physics: replay dropped ent_box collision impulses whenever velocity
  packets existed (Entity.applyEntityCollision); wither skeleton = skeleton
  model 1.2x + stone sword (8.0 raw) + wither DoT (%40 pulses through hurt
  resistance); hurt-resistance/lastDamage gating implemented; nether brick
  had no block model (rendered as stone); held items ignored the night
  lightmap. Dragon: contact damage from recorded envelopes via causeMobDamage
  + hurt-resistance ledger reproduces death at t606 exactly; dragon-breath
  AoE cloud is RNG-unmatchable -> scoped sidecar known:40.
- End entry scenario note: seed-0 obsidian platform is embedded in the island
  (tape one was 1600 ticks of inside-wall view; also surfaced that magma and
  the oracle disagree on camera-inside-block near-plane rendering - open).
- All mirrored to mono with drift vocabulary preserved (10 commits, mono
  canonical rc=0); mirror of the combat/dragon rounds pending this batch.
- Projectile blind spot (operator eyeball catch): blaze small fireballs and
  dragon fireballs were recorded in the tapes but invisible in magma - the
  pixel gate soaked them into the particles class. RenderFireball.doRender
  (fire charge billboard, scale 0.5, renderEntityOnFire overlay) and
  RenderDragonFireball.doRender (scale 2.0 quad) implemented full-bright;
  particles class dropped 74k px (blaze demo) / 23k px (dragon demo), both
  demo tapes rc=0. Gate hardened: recorded entity types with no magma model
  now fail the scenario gate as missing_model (>4 rows, allowlist only
  EntityAreaEffectCloud per sidecar note) so invisible entities can never
  pass silently again. Lesson: cluster classes that absorb "small moving
  stuff" (particles) can hide whole missing renderers; the demo eyeball
  pass is a real gate, not a formality.
- Player-state visual audit (follow-up to the fireball catch; operator asked
  "what about player on fire from blaze"): tape fields fire/hurt/pots/cd all
  drive visuals the entity-model gate never covered. Implemented:
  ItemRenderer.renderFireInFirstPerson (screen fire overlay, driven from
  recorded fire + live sim), GuiIngame.renderPlayerStats heart flash
  (ceil(health), healthUpdateCounter white-flash phase) and poison/wither
  heart rows, renderPotionEffects HUD icons, hurtCameraEffect from recorded
  hurtTime/attackedAtYaw, updateEquippedItem cd^3 target + press-edge swing.
  Fire overlay silhouette mismatch root cause was orientCamera's +0.05 Z
  nudge leaking past renderHand identity. Attack indicator: verified
  oracle-equal (setting !=1, footprint 0/256 px diff at canonical t3400).
  Gate hardening round 2: per-frame class pixel budgets (hud 55k /
  particles 40k / viewmodel 40k) reclassify oversized soaks as UNEXPLAINED;
  proven on the actual saved pre-fix burn frame (fails as soak_from:hud).
  Blaze demo class soak dropped 12.85M -> 4.02M px; burn frames t88/t120
  now ~2-4k residual px. Old-tape swingProgressInt unrecoverable -
  documented in TAPE_COMPLETENESS.md.

## 2026-07-23 (pre-reset handoff: fluids/edge-blocks green, mob-AI + glitch research recovered, GPU0 wedged)

System reset imminent; this entry is the full state capture. Everything below
is committed and pushed on master unless marked otherwise.

### Landed this session (dffab82 -> 4151434, all pushed)
- dffab82 RenderFireball/RenderDragonFireball + missing_model gate.
- aac4353 devlog: fireball blind spot.
- 2658b7c renderFireInFirstPerson + renderPlayerStats + per-frame class
  pixel budgets (hud 55k / particles 40k / viewmodel 40k).
- 215b1d9 renderPotionEffects + updateEquippedItem + hurtCameraEffect +
  orientCamera +0.05 Z-nudge fix.
- eb25c4b devlog: player-state visual audit.
- fb001ae fluid scenario specs (water_dive, lava_walk, water_flow).
- cacd114 decreaseAirSupply air ledger + bubble HUD + flowing lava
  animation + transit class budget 40k.
- 63d95ed seven 1.11.2 edge-case scenario specs (elytra, slime, web,
  soul sand, suffocation, fence, fluid conversion).
- 4151434 edge block mechanics/models: slime bounce (+1.106 vy), web
  multipliers, soul sand 0.875 box / 0.4 XZ, fence+wall 1.5 collision
  boxes, armor stand model.

### Gate state: 19/19 tapes rc=0 at 4151434 (CPU replay)
Canonical 20260721T215812Z_fast_s0_survival_default_rd8_77b5b462 plus:
scenario_smoke_zombie 081735Z, blaze_melee 092705Z, blaze_bow 092838Z,
blaze_bow_demo 104234Z, pigmen_aggro 093154Z, wither_skeleton 093020Z,
enderman_fight 093335Z, ender_dragon 094040Z, ender_dragon_demo 104500Z,
water_dive 234816Z, lava_walk 234940Z, water_flow 235050Z,
suffocate_camera 001923Z, flow_convert 002122Z, slime_bounce 001527Z,
cobweb_fall 001656Z, soulsand_ice 001810Z, fence_collide 002017Z.
(ender_dragon 093713Z is superseded/stale, known rc=3, not in gate set.)
Replay cmd: `cd c/magma/raster/verify/trace && uv run --no-project --with
numpy,scipy,pillow,nbt python replay_tape.py ../tapes/<stem>.jsonl --cpu
--report`. zsh gotcha: rc through a pipe needs `${pipestatus[1]}`.

### Research docs recovered into the repo (this commit)
/tmp scratchpad was wiped by the reset; both codex research reports were
recovered from codex rollouts (~/.codex/sessions/2026/07/22/) by decoding
their apply-patch payloads:
- docs/research/glitch_research_1112.md - 36 ranked deterministic 1.11.2
  edge cases, do-not-bother list, recommended first ten (ranks 1,2,3,4,5,
  6,8,11,12,13). Ranks covered so far: the edge-block/fluid rounds above.
  Elytra (rank 1) is in flight, see below.
- docs/research/mob_ai_audit.md - verdict: mob AI NOT solved for the live
  simulator. Ordinary mobs share one direct-steering loop; no EntityAITasks
  scheduler, pf12/PathNavigateGround not wired, pig zombie has no live
  type. Top-10 fix program is in the doc (start: per-entity Java RNG +
  trajectory-parity gate, then task scheduler, then pathfinding).
Source rollouts if re-extraction is ever needed:
rollout-2026-07-22T19-06-15-019f8c82... (glitch), rollout-2026-07-22T17-46-25-019f8c39... (mob audit).

### In flight, interrupted by the reset
- Elytra physics: branch wip/elytra (c9b18fc, pushed) holds the killed
  codex round's UNVERIFIED travel() port (player_survival.h +129 and game
  wiring). Divergence target: elytra_glide tape tick 56, oracle x=5.8095
  vs magma 5.7460. Resume by having codex continue from the branch or
  restart the round; do NOT merge unverified. All 19 tapes must stay rc=0.
- Crafting/furnace GUI capture: 40-capture plan (capture_crafting.sh,
  crafting_harness.py, run_crafting_verify.sh, manifest with recipes cited
  to CraftingManager.java) was in /tmp and is lost; regenerate from the
  codex rollout of that round or just re-prompt. Oracle-side capture never
  completed (blocked by the GPU hang below).

### Anvil GPU0 driver hang - reboot required
nvidia-modeset "Error while waiting for GPU progress" loop; nvidia-smi
cannot open GPU0 (Blackwell). Any process opening an nvidia DRM node
D-states in drm_open (kill -9 immune; D-state PIDs 1288651 Xvfb :1,
1613897). Mitigations in place, to revert after reboot:
- chmod 000 /dev/dri/{card0,renderD128,card2,renderD130} + setfacl -b
  (restore normal modes post-reboot).
- Oracle stack moved to MC_DISPLAY=:3 - java/start_vnc_client.sh in the
  mono repo is parameterized (MC_DISPLAY/MC_VNC_PORT env). Display :1
  usable again after reboot.
- CUDA replay unavailable; CPU replay unaffected. After reboot, rebuild
  magma_game_cuda before trusting CUDA-scored gates (stale-binary trap).

### Continuation queue (in order)
1. Reboot anvil; revert /dev/dri modes; verify nvidia-smi sees both GPUs;
   restart oracle stack (either display); rebuild CUDA binary.
2. Finish elytra from wip/elytra; gate all 19 tapes + new elytra tape.
3. Regenerate + run the crafting capture and run_crafting_verify.sh
   bitwise GUI diff pass.
4. Mob-AI program per docs/research/mob_ai_audit.md top-10 if launch scope
   includes live sim (trajectory-parity gate first, then pig zombie/blaze/
   enderman/skeleton).
5. Mirror batch to mono (fb001ae, cacd114, 63d95ed, 4151434 + this docs
   batch); mono has an uncommitted start_vnc_client.sh MC_DISPLAY edit to
   commit first.
6. Re-encode combat_sbs.mp4 after elytra lands; scp to macbook:~/Downloads.
7. Remaining glitch-research ranks beyond the first ten, if desired.

## 2026-07-23 (post-reset: elytra physics landed, 20/20 CPU tapes green)

- Resumed `wip/elytra` with three isolated Grok rounds: core physics, direct
  Java-oracle audit, and replay/runtime gating. All agreed the interrupted
  `EntityLivingBase.moveEntityWithHeading` branch already resolved the main
  divergence; the remaining fixes were activation timing and numeric edges.
- Elytra travel now follows 1.11.2 float/double boundaries, including
  `Vec3d.lengthVector` widening `MathHelper.sqrt`'s float result. A jump edge
  samples airborne/descending state before travel (MC-111444), arms flag 7
  after that tick, and takes the elytra branch on the following tick. The
  0.6-high pose and 0.4 eye height persist until a collision-safe resize.
- Tape equipment replay seeds chest-slot item 443 before tick 0 and applies
  later inventory changes on the following tick. Falling liquid created after
  capture is no longer backdated into the recorded glide.
- Exact regression fixtures cover the old freefall path (`x=5.7460`), the
  corrected t54->t56 chain (`x=5.809549093722865`), dive/climb binary64
  motion, activation edge, landing, eye height, runtime equipment bridge, and
  tape conversion.
- `scenario_elytra_dip_20260723T001355Z`: 505/505 ticks physics-exact at
  1e-9; 51-frame pixel gate PASS. All prior 19 CPU tape replays also rc=0:
  gate state is now 20/20.
- Clean rebuild exposed two stale-object-hidden defects and both were fixed:
  selection-box APIs now use the real `Chunk`/`McSinTable`/`PsvPlayer`
  typedefs, and `gm_world_clock_init` initializes `freeze_daylight`.
- Verification: `make test-game` PASS; non-live pytest 40 passed; elytra
  replay tests 4 passed; scoped ruff PASS. Full pytest still requires a live
  qrl client and populated DIM-1 save, so those explicit integration tests
  were excluded from the non-live run.
- Remaining elytra cuts: chest armor durability is not owned by `IsrInv`, and
  creative free-flight (`capabilities.isFlying`) is outside the simulator
  surface. Neither affects the recorded survival tape.
- Next queue item is crafting/furnace GUI capture. CUDA was not re-gated in
  this round; GPU0 is healthy after reboot but occupied by a 92 GB vLLM job.

## 2026-07-24 (Grok fan-out: route roster, armor, chests, renderer, gates)

- Four isolated Grok implementation branches covered renderer gaps, armor and
  elytra inventory ownership, single chests/stronghold loot, and the missing
  route encounter roster. A fifth branch hardened the route, pixel/state
  gates, nightly failure handling, and quick sweep coverage. Each branch was
  reviewed and tested independently before integration.
- Live encounters now include pigmen, ghasts, magma cubes, slimes,
  silverfish, wither skeletons, blazes, boats, typed spawners, XP orbs, and
  dimension ownership. The entity store is 96 entries with separate hostile
  and passive natural caps, so ambient spawning cannot starve scripted or
  route-critical encounters.
- The independent mob review found and closed five integration defects:
  boat damage decayed faster than legal cooldown hits could accumulate,
  magma cubes used slime damage instead of `size + 2`, pigmen lost their
  held gold sword, ghast fireballs used marker geometry, and a saturated
  projectile pool discarded pending ghast shots. Live-tick regressions cover
  all five.
- Armor slots compose at 49..52 and chest slots at 53..79. Crafted armor
  absorbs damage and loses durability; an equipped chest elytra owns flight
  state. Single chests support open/click/shift/throw/close/reopen persistence,
  the real `generic_54` GUI, deferred stronghold corridor/library loot, and a
  facing-aware inset mesh.
- Renderer coverage added the End portal surface, XP-orb billboard/animation,
  new mob/boat atlas entries, pigman biped walk/held equipment, and live
  ghast-fireball views. Tape inventory/state coverage now includes all 41
  main/armor/offhand slots.
- The macro route no longer clears the mob store or injects post-bed health.
  It legally harvests food and wool, crafts/equips iron boots, regenerates
  through food/vitals, uses bow and bed paths, survives the End bed blast at
  the measured interaction boundary, kills the dragon, and reaches credits.
- Verification: `make test-game` PASS, route fresh-spawn-to-credits PASS,
  mob live suite PASS, entity renderer PASS, and verifier/scenario pytest
  48 passed. `netherite_sweep.sh --quick` is green with no skips; its first
  run exposed a stale five-box fence golden after the earlier two-rail model
  landed, and the corrected nine-box/324-vertex golden now passes. The
  repository-wide Ruff command remains red on 515 legacy findings; its 199
  unrelated auto-fixes were reverted, and no Ruff edits outside the touched
  verifier files were retained.

## 2026-07-25 (CrShadeCtx positional-init misalignment: all CUTOUT geometry was discarded)

- Root cause: `terrain_shades()` (`c/magma/game/frame_capture.c`) and
  `render_world()` (`c/magma/app/game_main.c`) built their four per-layer
  `CrShadeCtx` values with positional initializers written before
  `CrShadeCtx.alpha_ref` was inserted (commit `3819bcf`). Every value from
  slot 6 on shifted by one: the fog-enable flag landed in `alpha_ref`, the
  layer enum in `enable_fog`, and `blend` in `layer`. With fog on (the
  default) `alpha_ref=1.0` gives an alpha threshold of 255, so
  `cr_shade` discarded *every* CUTOUT and CUTOUT_MIPPED texel - all cross
  plants, tallgrass and grass_side_overlay - and translucent water rendered
  with blend=0 (opaque, depth-writing). `-Wextra` only flagged the missing
  trailing `mip_bias`, never the misalignment.
- Fix: both sites now use designated initializers, so a future field insert
  cannot repeat this. No other change.
- Effect on the canonical tape
  `20260721T215812Z_fast_s0_survival_default_rd8_77b5b462` (CPU replay,
  181 frames), before -> after: UNEXPLAINED 1_540_406 px / 67 frames ->
  122_581 px / 63 frames; failed frames 58 -> 7; worst frame t=80 with
  74_783 px -> t=260 with 7_291 px; viewmodel 1_199_958 -> 256_366;
  particles 580_964 -> 181_116; hud 313_882 -> 163_264; bossbar 159_110 ->
  57_319; known:14 109_693 -> 98_557. Whole-frame mean at t=80 3.76/ch, and
  the oracle/magma side-by-side now shows the same tallgrass field.
  Physics stayed clean; `make test-game` PASS (27 suites).
- Still open: t=260 / t=460 residual clusters, the viewmodel soak at
  t=3180-3220, and the outdoor `known:4` tint/AO residual. Same misaligned
  pattern survives in `app/trace_main.c` and the `raster/verify/*_candidate.c`
  fixtures; those run with fog off so alpha is unaffected, but their layer
  and blend slots are equally shifted and their goldens are pinned to it.
- Pre-existing, unrelated: `make game-cuda` fails to link
  (`cr_camera_view` defined in both `core/math.o` and
  `cuda/raster_cuda_sm86.o`), so this round was measured on the CPU path.

## 2026-07-25 (post-fix sweep: every tape re-baselined, CUDA game link repaired)

- `magma_game_cuda` had not linked since `cr_camera_view` was added to
  `core/math.c`: `cuda/raster_cuda.cu` #includes math.c under `_dev` private
  names, and the new symbol was not in that rename list, so it collided with
  the gcc-built `core/math.o`. Added `cr_camera_view` to the rename block.
- `raster/verify/nightly_verify.sh` gained `NIGHTLY_BACKEND=cpu`, which
  replays on the CPU instead of GPU1. Default behaviour is unchanged (GPU1,
  `--cuda`, self-defer when GPU1 is busy); the override exists because a
  correctness sweep is not timed, so a 12 GB co-tenant on GPU1 is a reason to
  fall back rather than skip. GPU0 stays reserved.
- First full sweep: 23 tapes on the CPU, all post-CUTOUT-fix. 6 clean (rc=0),
  8 pixel-gate FAIL (rc=3), 9 non-player state divergence (rc=5, pixel gate
  itself PASS). All 23 gate.json results are now committed under
  `trace/baselines/`, which is what nightly diffs against - previously only
  one tape had a baseline and the other 22 failed as "missing required
  baseline", so the sweep had never produced a usable signal.
- Nightly will still report RESULT: FAIL until the nine rc=5 state divergences
  are closed; baselines can absorb pixel-gate failures, not those.
- CPU/CUDA parity re-confirmed after the shade fix: the canonical tape
  replayed with `magma_game_cuda` built `-arch=sm_120` on GPU0 is **bit
  identical** to the CPU replay - 0 differing pixels over all 181 frames,
  every gate class and max_cluster equal. (GPU1 had a 12 GB co-tenant, so the
  parity run used the idle GPU0 and a matching sm_120 object; the tree is
  built back to the default sm_86.)
- Residual on the canonical tape (t=260, t=460) characterised, not fixed:
  it is not geometry, not a camera offset (best whole-frame alignment is
  dx=dy=0), and not the fog distance mode. The oracle's own GL query records
  `fog_distance_mode_nv = 34139` (GL_EYE_RADIAL_NV), which is what magma
  already does; forcing planar |z| fog as an experiment made the tape worse
  (particles 181k -> 436k px, viewmodel 256k -> 411k, failed frames 7 -> 10)
  and the experiment was reverted. On leaf interiors the delta is zero-mean
  with a large spread (near canopy mean +0.1/+0.2/+0.2 per channel, sigma
  19/28/8), i.e. individual texels flipping between neighbours rather than a
  shading offset - nearest-neighbour texel selection on minified noisy leaf
  faces.

## 2026-07-25 (all nine rc=5 state divergences closed; rung4 pose corrected)

Grok fan-out of three: tick-0 inventory, `rung4-verify`, and triage of the
three worst pixel-gate tapes. Every diff reviewed and every acceptance test
re-run here before landing.

- **All nine rc=5 tapes are now rc=0.** Root cause was one line in
  `replay_tape.py`: tape `inv` rows are post-tick truth, re-anchored with a
  `set_inventory` on tick *t+1* so action *t* still sees the pre-tick stack,
  and `inv_view` is render-only. Nothing ever seeded live `player.inv` at
  tick 0, so the first state dump saw empty slots while the tape had recstart
  gear. `tape_to_script` now emits `set_inventory` for slots 0..40 from
  `ticks[0]["inv"]` before look/action, the same post-tick approximation
  `set_elytra` already used. No item-id special cases, gate logic unchanged.
  Verified: blaze_bow, blaze_melee, elytra_dip, both ender_dragon tapes,
  ender_dragon_demo, enderman_fight, fence_collide, pigmen_aggro,
  smoke_zombie, wither_skeleton all report
  `state: inventory PASS (1 ticks, 0 mismatches)`; the seven previously-rc=5
  tapes not otherwise pixel-failing now exit 0.
- **Caveat, recorded honestly:** `collect_state_assertions` samples every 20
  ticks and only checks ticks that carry an `inv` row, and on these tapes that
  is tick 0 alone (`inv_checked=1`). Seeding tick 0 from `ticks[0]["inv"]`
  therefore makes the only checked tick near-tautological - it now verifies the
  seeding path, not inventory evolution. The divergence it closed was real (an
  empty inventory where the tape had a bow and 64 arrows), but real hardening
  needs tapes re-recorded with periodic `inv` rows.
- `rung4_candidate.c` was rendering a default pose against a golden captured at
  a different one. Switched to `pose_scene.h`, froze the golden's real pose
  (eye 8.3/95.0/40.5, yaw 0, pitch -35, fov 77, zfar 181.01933), added
  `gm_sky_draw` + terrain fog, and made dual winding opt-in
  (`MAGMA_DUAL_WIND=1`; measured 1.47/0.85 with it vs 1.13/0.67 without, so
  single winding is the correct default). `rung4-verify` goes 44.94/36.91 ->
  1.13/0.67 PASS. `hard-scene-verify` (1.13/0.67/0.70) and `multi-verify`
  (seed0 1.13/0.67, seed7 4.27/2.37, pass=2) are unchanged.
  With the pose corrected rung4 is now the **lean twin** of hard-scene-verify -
  same scene, same golden, same numbers, through a standalone fixed-pose binary
  instead of `game_candidate`'s arbitrary-pose path. That is a second entry
  point over mesh/light/populate/shade/raster, *not* independent scene
  coverage; a COVERAGE NOTE in `run_rung4.sh` says so. A `/tmp`-scanning
  prep-list branch was proposed with it and deleted after testing showed the
  gate still passes with `prep_list=derived` when those paths are absent.
- Ratcheted the rung4 tolerances with the pose fix: 38.0/33.0 were sized for
  the stale-pose numbers and at 1.13/0.67 could no longer fail anything, so
  the gate was passing vacuously. Now 1.5/1.0, about 0.4 above measured.
- Triage of the three worst pixel-gate tapes produced no landed C fix (nothing
  unambiguous and small enough); findings are in OPEN_DIVERGENCES.
- **Nightly is green for the first time**: `nightly_20260725T052804Z`,
  `RESULT: PASS`, 23/23 tapes actually replayed (not skipped) on the CPU.
  15 rc=0 (was 6), 0 rc=5 (was 9), 8 rc=3 all matching their committed
  baselines, so no pixel regression. The 8 rc=3 are the honest open pixel
  work, not a green wash: baselines absorb them by design, and any increase
  fails the run.
- Then ran the same sweep on CUDA (GPU0 handed over explicitly; GPU1's 12 GB
  co-tenant never cleared across two watches totalling ~7h). `nightly_verify.sh`
  gained `NIGHTLY_GPU` for this, and derives `-arch=sm_XY` from the card's
  compute capability instead of assuming the Makefile's GPU1 `sm_86`, which
  would have produced a no-kernel-image failure on Blackwell.
- **The CUDA sweep is FAIL where CPU is PASS** - six tapes regress. Parity had
  only ever been measured on the canonical tape (bit identical), and it does
  not generalise. Details and measurements in OPEN_DIVERGENCES. Two wrong
  first guesses, both killed by measurement: GPU contention (serial re-runs
  reproduce byte-for-byte at identical ticks) and the three early
  `hp=0 dead=1` exits (they happen identically on the CPU, so they are a
  pre-existing sim issue, not a backend divergence).

## 2026-07-25 (four-way Grok fan-out: hurt camera, inventory gate, coverage)

Four agents in isolated worktrees. Every diff reviewed and every acceptance
re-run here.

- **CUDA dropped the hurt camera.** `cuda/raster_cuda.cu` built its MVP with
  `cr_look_yaw_pitch_dev` (look-only) at both sites while the host path uses
  `cr_camera_view`, so CUDA silently skipped
  `EntityRenderer.hurtCameraEffect`. Every damage tick the CPU drew a rolled
  horizon and CUDA a flat one - which is exactly the "terrain wedge at the
  left horizon" I had measured and mis-attributed to terrain extent/lighting.
  Both sites now call `cr_camera_view_dev`. On `blaze_bow_demo`: 12_212_050
  differing px -> 9_344_718, and the two hurt bursts collapse from 167_824 and
  156_540 px to 36 and 23. This also closes a loop: I added `cr_camera_view`
  to the CUDA `_dev` rename block earlier today to fix the link break, but the
  call sites were never switched over.
- Remaining CUDA gap is the deferred frame end on bow-pull + fire frames;
  `MAGMA_NO_DEFER=1` gives 12_875 px over 407 frames (sky-star noise only).
  Recorded in OPEN_DIVERGENCES - a deferred-path CUDA replay is not parity
  evidence until that is closed. The 23-tape CUDA sweep has not been re-run
  since the fix.
- **Inventory gate now checks every `inv`-bearing tick**, not just the
  every-20 sample grid. `blaze_bow` already carried change dumps at t=77/78
  that the grid never landed on, so this bought 10 independent checked ticks
  with no re-capture. Tick 0 no longer counts as independent (replay seeds
  from it) and seed-only tapes report `seeded_only`. The Java recorder also
  emits an inventory keyframe every 20 ticks, which needs a re-record to take
  effect. 48 unit tests, including a mutation test that makes the gate fail.
- That immediately caught two real misses on the canonical tape (t=3257 slot 1
  item 270, t=3267 slot 2 item 50). Chased to cause: both are **crafted**, and
  crafting clicks are not taped - the fix is a `.worldpatch.jsonl` sidecar,
  which `20260712` has and the canonical tape does not. Known recorder
  blocker, previously invisible.
- **No more silent coverage caps.** The state gate carries a `coverage` block,
  the replay prints `[tape] COVERAGE: only N of M tape ticks were replayed`,
  and `gate_baseline_diff.py` now compares the state block as well as pixel
  classes (it previously compared neither inventory nor coverage, so a state
  regression could never turn nightly red). A state failure under a pixel
  failure is called out instead of being swallowed by `return 3`.
- That surfaced that **seven** tapes truncate at a terminal death, not the
  three I had found - `smoke_zombie` verifies 45% of itself, `ender_dragon_*`
  37-38%, and four of the seven were exiting rc=0. The deaths themselves are
  oracle-correct (tape tick 813 `hp=0.0`, 814 `hp=20.0`); the delegated agent
  correctly refused to "fix" them and I confirmed it from the tape directly.
  My briefing premise - that magma killed a player the real game kept alive -
  was wrong.
- All 23 baselines re-committed to carry the state block. Nightly is PASS
  again (15 rc=0, 8 rc=3) with the known inventory failure absorbed the same
  way pixel failures are.
- Setup note for future fan-outs: `raster/verify/tapes/` mixes tracked sidecars
  with gitignored bulk, and a worktree that links only `*.jsonl` and `*_frames`
  silently omits the `*_world` snapshot dirs. Replay then skips snapshot
  patching without erroring, and the sweep fails for reasons that look like
  real bugs. Link every entry; assert the count matches the main tree.

### 2026-07-25 - CUDA sweep after the hurt fix: deferred frame end is the only gap left

- Re-ran the full 23-tape CUDA sweep on GPU0 (`sm_120`,
  `nightly_20260725T071901Z`). 15 rc=0 / 8 rc=3, the same tally as the CPU
  sweep, with baseline regressions on two tapes rather than the five the
  pre-fix sweep had (`nightly_20260725T062525Z`, 13/10).
  `ender_dragon_094040Z`, `ender_dragon_demo` and `lava_walk` are now
  byte-identical to their CPU baselines on every class.
- Both survivors are the same bug. Replaying the canonical
  `20260712T055346Z` and `blaze_bow_demo` with `MAGMA_NO_DEFER=1` reproduces
  the CPU baseline exactly - every pixel class, `failed_frames`, and the
  state block unchanged (canonical 3121/3121 ticks, UNEXPLAINED 538_620 on
  both sides; bow demo UNEXPLAINED 168_484). So the deferred frame end is now
  the sole CPU/CUDA divergence, and it is not bow-specific.
- Removed a build landmine while there: `cuda/raster_cuda_sm86.o` held
  whichever arch was built last (`NVFLAGS_GAME` is overridable, GPU0 needs
  `-arch=sm_120`) and nothing rebuilt it on an arch change, so a GPU switch
  could silently link the wrong arch. Renamed to `raster_cuda_game.o`;
  `scripts/demo_pixel_sbs.sh` now cleans `cuda/*.o`.

### 2026-07-25 - Deferred frame end closed: CUDA equals the CPU on every tape

Two bugs, both in `finish_pending`, both invisible to the CPU path because it
draws the hand/HUD/overlays at the frame's own tick.

- **Fire overlay fov.** The sync path passes `uw.fov_scale`; the deferred path
  re-derived it as `pend_uwfov / 70`, and `pend_uwfov` is
  `cam.fov_deg = 70 * fov_mult * fov_scale`. That folded `getFovModifier`'s
  bow-pull / sprint term into the overlay projection, which is why the
  divergence needed `fire=1` AND `use=1`. `blaze_bow_demo` went from 57 failed
  frames to 1, matching its CPU baseline byte-for-byte.
- **Suffocate overlay world.** `finish_pending` re-ran
  `gm_overlay_block_in_hand_live` against `c->pend_world` - the live world
  pointer - so the eye-block sample ran one rendered frame (20 ticks) after the
  frame being drawn. On the canonical tape t=660 that resolved to dirt and
  painted the entire frame with the suffocation overlay: 371_279 unexplained
  px, 100% of pixels differing at mean_abs 70.5. Split into
  `gm_overlay_block_in_hand_pick` / `_draw`; the deferred path resolves at arm
  time and snapshots the block.

The second one took a bisect worth recording, because every plausible GPU
explanation was wrong: `MAGMA_NO_HAND=1`, `MAGMA_NO_OVERLAY=1`, a full
`cudaStreamSynchronize` inside `frame_end_async`, and resetting the shade-ctx
ring at `frame_begin` all left the frame **bit-identically** wrong (83_341_540
px on 3/3 runs), while dumping the raw deferred readback with the host retire
draws skipped showed a perfectly normal frame (mean 81.4 vs 80.1/82.8 at the
neighbouring goldens). Deterministic-to-the-byte corruption is evidence
*against* an async race, not for one.

Also removed a build landmine: `cuda/raster_cuda_sm86.o` held whichever arch
was built last (`NVFLAGS_GAME` is overridable, GPU0 needs `-arch=sm_120`) and
nothing rebuilt it on an arch change. Renamed to `raster_cuda_game.o`;
`scripts/demo_pixel_sbs.sh` cleans `cuda/*.o`.

## 2026-07-25 - fogColor1 samples the tick-entry feet, and soulsand_ice closes

The sky-plane fog fix (`ac47c2b`) took `scenario_soulsand_ice` from 45 failed
frames to 1. The survivor, t=60, was the step down onto soul sand: 77% of the
frame differing at mean_abs 5.37, the shape of a global brightness term.

That term is `EntityRenderer.updateRenderer`'s `fogColor1` smoother, a
0.1-per-tick lerp toward the light brightness at the player's FEET block. Soul
sand is `useNeighborBrightness = false` and opaque, so standing on it puts the
feet block at light 0 and starts a long ramp from 1.0 down to 0.25 - a step the
gate only catches at its largest sampled point.

Rather than guess the phase, we solved the goldens for the `c1` they were drawn
with: render the frame at two known `c1` values, fit the local slope of frame
mean vs `c1`, invert. soulsand_ice t=60 implies 0.8548 - two smoother steps -
against our three. But a blanket one-tick lag then broke `water_dive` t=1000,
whose golden implies 0.5771, exactly four steps from the t=997 teleport.

Both are explained by where `updateRenderer` sits in `Minecraft.runTick`: after
the network phase, before the local player's movement update. A teleport
arrives as a pose packet and is visible to the same tick's smoother; ordinary
movement is not. Capture now samples `gm_runtime_tick_entry_feet` - the feet
position snapshotted at the top of `gm_runtime_tick` - instead of the post-tick
view. Same class of off-by-one as the deferred `set_look` already documented in
`game/script.c`.

CPU sweep `nightly_20260725T081035Z`: PASS, soulsand_ice rc=0, no regressions.

## 2026-07-25 - the gate was measuring 80 percent of the frame on the canonical tape

Four fixes landed and one was rejected, but the finding that reframes the rest
is that two of our measurements were not measuring what we thought.

**Concurrent replays corrupted each other.** Agent worktrees symlink `tapes/`
to one shared directory, and `.snapshot_patch.jsonl`'s staleness check keys off
`snapshot_patch.py`'s mtime, which differs per worktree. So every parallel
replay decided to regenerate the same cache at once, through two shared
fixed-name files: the `world_dump` scratch, where processes read each other's
tiles and emit a silently WRONG patch, and the cache itself, written in place so
a reader gets a TRUNCATED one and replays an unpatched world. `blaze_bow`
measured 3.63/ch terrain against a 0.94/ch baseline and reproduced 0.94 exactly
once the machine was idle. Nothing was wrong with the renderer. Both names now
carry the pid and the cache is published with `os.replace` (`b9fe039`). A
delegated agent reported the same phantom regression independently; that report
was correct about the symptom and wrong about the cause, as was I at first.

**The canonical tape's goldens have no HUD.** Malmo's `ClientStateMachine`
forces `hideGUI=true` for the whole mission, so all 157 goldens of
`20260712T055346Z` have no hearts, hotbar or crosshair, while all 181 of
`20260721T215812Z` (recorded after QuantizedRL started clearing the flag) do.
`qrl_launch.hide_gui` reads false on both, so it could not be the source;
`capture.hide_gui` is the measured value now. Magma was drawing a HUD over
goldens that have none, and the gate's positional `hud` accept swallowed it -
the bottom 96 rows, a fifth of the frame, had no pixel verification at all on
that tape. Suppressing the HUD was not enough on its own: `gate_frame_ex`
removes positional accepts as topology barriers *before* known-divergence
matching, so those rows never reached the filed rain entry and instead tripped
the `hud` class budget. The accept is now dropped entirely on `hide_gui` tapes,
while the same strip is still carved out of `viewmodel` so its budget is not
silently re-scaled (`3dc2d19`, `b3922ac`). 14 -> 25 failed frames, which is what
measuring 20 percent more of every frame costs. The rain window t=1800..2100
now resolves cleanly into `known:12` instead of leaking.

**Elytra pose never cleared after landing.** `psv_update_elytra_size` treated
`psv_collect_blocks(...) == 0` as "no collision", but that is a cell broadphase
and always returns the floor under the feet; vanilla's `collidesWithAnyBlock`
uses strict `AxisAlignedBB.intersects`, so a floor touching `minY` does not
block the expand. The 0.6F pose and 0.4F eye height stuck forever, putting the
camera 1.22 blocks low - the full-width horizon band on `elytra_dip` was sky vs
grass from the wrong eye height, not fog. 41 -> 4 failed frames (`9b165bf`).

**Dig dust skipped the lightmap.** `ParticleDigging` sets a flat 0.6 gray and
`Particle.renderParticle` multiplies it by the lightmap at the particle; magma
kept the gray. Mining unlit End stone therefore painted a 45216 px near-full
brightness patch across the wall. `ender_dragon_093713Z` 57 -> 55 failed frames,
worst mild_shift 25.94 -> 12.55 (`8ae0a38`).

**Rejected:** a slime fix that re-emitted the outer cube face coplanar to double
its translucent opacity. The diagnosis was right (the dark field is the slime
platform, `slime.json` has two translucent elements, one layer renders 0.74x too
dark) and it measured well, 19 -> 12 failed frames. But the second element is an
inset cube, not a duplicate shell, and geometry the model does not contain will
be wrong somewhere the golden does not happen to look. Sent back for the real
inset element.

Also: `pxdiff.py` grew a `cutout-sky` discriminator that requires measured
background coverage instead of a delta direction, after a delegated agent proved
my own tool's verdict on the canopy was a false positive; and
`scripts/agent_worktree.sh` builds worktrees that can actually build and replay.

## 2026-07-25 (overnight: sprint-FOV ordering, and the viewmodel story was wrong)

**The FOV eased on tick N sees tick N-1's sprint flag.** `Minecraft.runTick`
calls `entityRenderer.updateRenderer()` at `Minecraft.java:1862`
(-> `updateFovModifierHand`, `EntityRenderer.java:296`, easing
`fovModifierHand += (f - fovModifierHand) * 0.5F`) BEFORE `world.updateEntities()`
at `Minecraft.java:1881`. magma ran the ease after its sprint state machine and
was one tick ahead: at t=260 of `20260721T215812Z` it projected at 1.1453125
(80.171875 deg) against vanilla's 1.140625 (79.84375 deg). A third of a degree
of FOV is invisible as shading and decisive as sampling, and it is what had been
filed for weeks as a "texel-selection" residual - not a sampling rule, not UV
interpolation, not FaceBakery baking, not attribute precision. Moving the block
above the state machine took `20260721T215812Z` 7 -> 3 failed frames (worst 7291
px at t=260 -> 865 px at t=700; the t=460 cluster 6252 -> 72), slime 16 -> 15,
the canonical tape 28 -> 27, and one tape's UNEXPLAINED 1226 -> 0 (`130d0bd`).

**EntityFallingBlock renders the block MODEL.** `RenderFallingBlock.doRender`
draws the model at the blockpos then translates by
`(x - blockpos.x - 0.5, y - blockpos.y, z - blockpos.z - 0.5)`, so the cube is
`[posX-0.5, posX+0.5] x [posY, posY+1] x [posZ-0.5, posZ+0.5]`: a unit cube, not
the 0.98 collision box a delegate sized it from and not the 0.25 ground item
drop (`e03ac5f`).

**The gate had a hole in the same quarter of the frame it had just un-masked.**
`hide_gui`/`hide_hand` dropped the POSITIONAL hud and viewmodel barriers, but
`pixel_gate` also has post-hoc SEMANTIC classes with the same names and a 40000
px budget each, so un-masking put nothing new under measurement on any frame
where a heuristic fired. Both now honour the flags. On the canonical tape `hud`
disappears entirely (107 frames / 244695 px that were never an explanation) and
failed frames go 18 -> 28, all of them in the newly measured region (`b251384`).

**The canonical tape's viewmodel family was filed wrong, twice.** It had been
recorded as a missing HELD ITEM, recorder-blocked because the tape carries no
`inv` field. Opening the lower-right crops at t=0, 900, 1140, 1800, 2400, 2800
and 3100 shows the same skin wedge in the same screen position over seven
different backgrounds: the oracle is EMPTY HANDED for most of the tape and what
it draws is `renderArmFirstPerson`. magma has that path and lands it on the
right pixels - it draws it far too bright. On rain-free frames whose terrain and
sky agree to within 1/255, the arm is off by a per-channel 0.72/0.60/0.58
(t=900: golden (139,103,84), magma (192,173,148); terrain (87,114,69) vs
(87,114,70)). Per-channel and warm-biased, so a lightmap colour and not a
brightness scalar. Forced on for the whole tape it moves 110 of 157 frames the
wrong way on the gate-independent whole mean/ch, which is why the suppression is
still there; under investigation on `wt/armlight` (`2819f9a`).
Two methodology notes came out of it. The yaw-sweep test that produced the
original reading is sound about "screen-fixed viewmodel" and was over-read into
"held block", which it cannot support. And `MAGMA_HAND_FROM_TICK` in the
environment now overrides the sidecar (`de54029`), because the only previous way
to A/B the hand was editing `demo/`, which is symlinked into every agent
worktree - a probe in one worktree silently changed what every other running
agent was measuring.

**Pickup inference cannot rescue the held-item intervals.** The tape carries
8673 `EntityItem` rows, 530 of them within three blocks of the player, and every
one is 7 fields (`id, name, x, y, z, yaw, pitch`) with no item id. All 1317
`set_block` rows in the worldpatch are at tick 1, an initial snapshot rather than
an edit log. Recovering WHICH item needs the tape re-recorded; recovering the
ARM does not.

**Still open and newly visible:** over t=540..660 the oracle draws no viewmodel
at all, at `pitch` exactly 90.0. The corner object there is terrain, measured
rather than assumed - it tracks the background across the window (mean |d|
0.85/10.98/11.93 at t=620/640/660 against a control of 0.96/15.22/14.13).
Vanilla's `renderHand` has no pitch gate.

**Release-path GPU verified.** CUDA on GPU0 (sm_120) is identical to the
CPU baselines on all 164 base-vs-now quantities across 23 tapes (`8058633`).
`nightly_verify.sh` now says so in its SKIP message: `NIGHTLY_GPU=<n>` is safe
across GPU generations because the `sm_` target is read from the chosen card.

**From the fan-out.** Boss fog now latches on ender crystals as well as a nearby
dragon - `DragonFightManager` constructs its `bossInfo` with
`setCreateFog(true)` and clients keep it for the whole fight, while magma's
nearest-8 tape window loses a far dragon (`e7b274d`). It is citation-backed and
moves no pixel on any tape we have, including the three other End tapes, which
all still pass. The slime platform residual was measured to a conclusion without
a fix: at a=188/255 the dual-covered block centres already equal the golden, so
`raster_cpu`'s SRC_ALPHA is right, and the dark residual is the single-layer rim
of the 3/16 inset (61% of a top face). Four levers were tried and all rejected,
including a back-to-front translucent sort that takes slime 15 -> 14 and
regresses `elytra_dip` UNEXPLAINED 784 -> 16495 (`8945ac4`).

### 2026-07-25 addendum (the arm was Alex, and one delegate fix did not hold)

**Half the arm's over-brightness was a skin mismatch, and my "per-channel, so a
lightmap colour" reading was wrong.** The tape header has no `skin` field, so
`replay_tape.py` fell back to slim and magma drew ALEX against the oracle's
Steve. The tape's own `qrl_launch.determinism.pin_skin` is true and
`MixinRandomSkinTexture` forces the classic model whenever it is set;
`tape_skin()` honours it now (`db5ac63`). Against the Steve texel (150,111,91)
the golden is a clean scalar 0.660/0.658/0.659 - never a coloured multiplier,
just a paler texture. With Steve drawn, forcing the hand on flips the A/B from
110-worse/10-better to **91 better, 29 worse, 37 unchanged, mean whole/ch 5.91
-> 5.07**, which I reproduced independently. What is left is a scalar ~1.57x:
magma applies diffuse x lightmap ~0.98 where the oracle applies ~0.66, i.e. the
arm is essentially unattenuated. Light levels and the LUT path measure correct,
so the next place to look is eye-space face normals out of `build_arm_matrix`
against `hand_diffuse` under `rotateArroundXAndY`.
Whether to flip the sidecar's `hand_from_tick` to 0 is a release-time call: the
metric now favours the arm being on, but turning it on re-baselines the tape and
bakes in a known-wrong 1.57x, so it stays off until the residual lands.

**Reverted a delegate fix whose acceptance did not reproduce.** "render legacy
fiery fireballs" takes `scenario_blaze_bow_demo` from 1 to 3 failed frames
against its committed baseline (new ticks t=454 and t=460), even though
UNEXPLAINED drops 168484 -> 155626 and particles 154028 -> 83755. The mechanism
is why: inferring `Entity.fire=1` for every `EntitySmallFireball` after its
first observed tick puts on-fire layers back, and this repo already established
the opposite. The UV half of that commit IS correct - vanilla's
`Render.renderEntityOnFire` gives the first vertex `maxU` and the second `minU`
(`Render.java:174-177`) and magma had them mirrored - but it was bundled, and it
is inert once nothing burns, so it went back with the rest and should be
re-landed with a test. After the revert the tape matches its baseline on every
quantity.

Also from the fan-out, both rejected by their own authors rather than shipped: a
five-neighbour-maximum skylight for `fogColor1` that takes `elytra_dip` 4 -> 2
failed frames and regresses `water_dive` 0 -> 14, and a back-to-front translucent
sort that takes slime 15 -> 14 and regresses `elytra_dip` 784 -> 16495 px.

Tree state at hand-off: full nightly on GPU0, 23 tapes, RESULT PASS, zero
REGRESSION lines; `make -C c/magma test-game` PASS; `demos/pixel_match_sbs.mp4`
regenerated and inspected.

## 2026-07-27 (launch prep: M3 throughput measured, tapes/assets refreshed)

- **M3 throughput gate: PASS.** GPU0 (RTX PRO 6000 Blackwell, sm_120),
  exclusive card (nvidia-smi clean before every run), `verify_cuda.py
  --bench`, t0 snapshots (full action decode), repeat 4, camera per
  decision, 1000 timed decisions with pre-generated on-device random
  actions and periodic masked resets:

  | N | env-ticks/s | decisions/s |
  |---|---|---|
  | 1024 | 0.79M | 198k |
  | 4096 | **2.22M** | 554k |
  | 8192 | 3.02M | 756k |
  | 16384 | OOM: region pool 128^3 x N = 137.4 GB > 96 GB |

  Gate was >=1M aggregate at N=4096: cleared at 2.22M (repeatable to 3
  digits across two runs; a shorter 250-decision run reads ~5% higher, so
  report the 1000-decision figure). Curriculum snapshots at N=4096: 2.61M /
  653k (cheaper worlds). CPU reference (9950X3D, 32 threads,
  `blaze_cpu.so` OMP, same loop/actions/snapshots via a mirror script):
  0.29M env-ticks/s at N=256, 0.25M at N=1024 - best-vs-best the GPU is
  ~10-12x the whole CPU.
- elytra_dip re-recorded and adopted (`20260727T214459Z`): settled liquids,
  converged fog_color1 header, 1 failed frame (flow-texture streaks) vs 4.
  Old tape retired. Recorder now writes fog_color1 + rain/thunder strength.
- Fidelity state for launch copy: 23-tape suite, 16 rc=0, 7 rc=3 with every
  residual diagnosed in OPEN_DIVERGENCES.md; CPU==CUDA raster parity
  bit-exact; CPU and CUDA nightly sweeps both RESULT: PASS.
- Demos re-encoded from today's binaries: `demos/pixel_match_sbs.mp4`
  (gates re-verified PASS during encode) and `demos/combat_sbs.mp4`
  (blaze_bow_demo + ender_dragon_demo, title copy updated to the 23-tape
  claim).

## 2026-07-29 (LAUNCHED)

- Public launch: https://x.com/elliotarledge/status/2082366172222439879
  (8-tweet thread, zoom video lead). Public repo:
  https://github.com/Infatoshi/netherite - clean tree via
  export_public_tree.sh (1704 files, 549 Mojang-derived excluded), FRESH
  history. The full private repo was renamed to Infatoshi/netherite-dev;
  this checkout's origin now points there. Never push this repo's history
  to the public remote.
- Launch video pipeline documented in docs/DEMO_VIDEO.md; thread archives
  on the macbook in ~/Downloads/netherite_thread{,_v2}/.

## 2026-07-29 (blaze glow + on-fire engulfment, wt/blazeglow)

Operator catch: the oracle's blaze is full-bright with flames wrapped around
it while magma drew a dull brown mob. Two vanilla mechanisms were missing, both
render-only; the tapes already carried everything needed.

- `EntityBlaze.getBrightnessForRender` returns `15728880` (sky 15 / block 15),
  so the model ignores world light. `frame_capture.c` now pins the sampled
  sky/block levels for types where `gm_entity_fullbright` is true, which keeps
  both the LUT and the folded Nether/End paths exact.
- `Render.doRenderShadowAndFire` draws `renderEntityOnFire` for any entity with
  `isBurning()`, and `EntityBlaze.isBurning()` is `isCharged()` (the `ON_FIRE`
  datamanager bit its fireball AI holds for the 78-tick volley). magma only had
  a fireball-billboard fire pass; `gm_entity_fire_emit` now runs the same
  vanilla layer loop for living views whose recorded `flags` bit 0 is set,
  sized by the entity AABB. The layer math is shared with the fireball pass
  (`ir_fire_layers`).

The burning bit is RECORDED, not inferred (the `60f4076` trap): the qrl
recorder has written `isBurning|isSneaking|isInvisible|isChild` per living row
since 2026-07-12, and the three blaze tapes carry 388-603 burning blaze rows
each with the exact vanilla 78-on/100-off cycle. No recorder change was needed.

Gates (CPU replay, sequential): `failed_frames` unchanged on all three -
`blaze_bow_demo` keeps its single known t=812 fight-state frame (167_724 ->
167_712 px), `blaze_melee` and `blaze_bow` stay rc=0. Whole-tape diff pixels
drop (demo 664_810 -> 616_752 over the golden frames; melee blaze-ROI at t=200
3_495 -> 1_609). Gate CLASS counts churn: the big bright "blaze missing"
clusters used to soak into `particles`, and the small residual left over
(blaze rod pose, fire animation phase) lands in `UNEXPLAINED` instead - demo
particles 154_028 -> 83_313 px while UNEXPLAINED 168_484 -> 191_133 px over
3 -> 173 frames, every new cluster far under the 4000 px fail threshold.
Baselines refreshed. Live-sim gap (no `attackStep` port, so an interactive
blaze never reports burning) documented in OPEN_DIVERGENCES.md.
## 2026-07-29 (nether arrival: dimensions born mid-recording were never snapshotted)

- The portal tape's Nether had no fire, no lava pools and no block light
  because `<tape>_world/DIM-1/` had no `region/` at all: the recorder
  snapshots the save at `recstart`, and a dimension the player first enters
  DURING the recording does not exist on disk yet. `snapshot_patch.py`
  emitted 0 dim -1 events and the replay ran on magma's own generation.
- magma has Nether TERRAIN (`nf_run`) but not `ChunkProviderHell.populate`,
  and cannot have it: that `Random` is reseeded only in `provideChunk`
  (`ChunkProviderHell.java:267`), so Nether decoration is chunk-load-order
  dependent, not seed-derivable. Saved-world snapshot is the only mechanism.
- `QuantizedRL.snapshotSaveDir(mc, snapRoot, addOnly)`: recstart pass
  unchanged; `recstop` adds an ADD-ONLY pass that copies only paths the
  snapshot lacks, so new dimensions land in the tape while recstart truth for
  the start dimension / level.dat / playerdata is never overwritten.
- `snapshot_arrival_events` also only knew position packets, and a portal
  transit has none (dim flips at t=134, first ppos t=168). Added the dim-flip
  arrival at pool radius; arrivals on one tick now accumulate instead of the
  dict-comprehension silently dropping all but the last.
- Re-recorded `scenario_portal_roundtrip_20260729T083543Z` with the fixed
  recorder (`snapshot_added: 4`). Same-tape A/B: 387 failed frames / 75.1M
  UNEXPLAINED px -> 170 / 11.2M; fire and the arrival lava pool now render in
  both panes. `demos/portal_sbs.mp4` re-encoded from those frames.
- Found while measuring, NOT fixed: `nf_to_vanilla` swaps the lava ids -
  magma's generated Nether sea is `flowing_lava` (10) where vanilla is still
  `lava` (11), 123k cells of the patch. The nether_full "golden" is a
  self-capture of the C kernel, so no gate ever saw it.

## 2026-07-29 overnight (divergence grind + nether clips)

Seven renderer/sim/pipeline fixes landed, each agent-produced, personally
re-verified, and regression-gated:
- ab3e853 nether lava sea id swap (goldens upgraded to real Java oracles)
- 5efc186 elytra flag-7 one-tick latency (eye height on the arming tick)
  + retired-tape gate repair (absolute golden paths -> re-anchor or fatal;
  a retired tape used to "PASS over 0 frames" - see AGENTS.md)
- 0e73841 double_plant cross models / upper-half type / tint
- 83784f0 snapshot patch authoritative over the vegetation band (phantom
  plants from populate-order-dependent decoration; scenic 92->39 frames)
- 5224721 vanilla death keel + hurt tint; spawner miniature renderer
  built + unit-tested (data plumbing filed, 4 layers)
- af5fbd8 dragon death-ray curve (onset, boss fog, lightmap unit, the
  byte-wrap starburst) + recorder now captures armor row/hidden-particle
  HUD gates
Headline: re-recorded dragon_kill passes the FULL pixel gate (rc 0, 201
frames) - first entity-death scene to do so; adopted into the suite with
nether_elytra (115-block lava-cavern glide, wall-slam landing).
Clips shipped to the mac: nether_elytra_sbs, dragon_kill_sbs (gate-clean),
blaze_melee_sbs (death keel).
Still open (agents/wave-2 or filed): entity-interp pose mirroring,
populate-order decoration beyond the vegetation band, fortress placement
y/z, spawner data plumbing, waterfall-entry flow-texture family, fire
animation phase, live-sim blaze aggro.

Late additions to the overnight batch:
- 63f26bd dragon trail-ring phase + freeze-on-death: the "interp lag /
  mirrored corpse" was ring pollution from the unwrapped death spin;
  dragon geometry now byte-exact vs the recorder's geom oracle (1668
  parts, 0 bad).
- 9296165 snapshot patch diffs against the replay's OWN worldgen (probe
  pass): replayed worlds now bit-identical to the save (scenic 7046
  wrong cells -> 0; patches shrink up to 300k -> 3 events). The scenic
  tape's remaining 39 failing frames are measured to be particle/
  viewmodel residuals, not decoration.
Nine landed fixes total; suite RESULT: PASS; agent worktrees cleaned.

Dragon death burst (wt/dragonparticles, 2026-07-29): the death explosion was
filed as a ~3 px "shading-offset" but pxdiff's shift always sat on the span-3
search boundary; a wide-span search says zero shift is best by 3x at every
burst tick, so it was never a registration error. Three real causes, all in
the reconstruction: (1) one `ParticleExplosionHuge` spawns 6 LARGE on each of
its 8 onUpdate ticks, and magma emitted only the newest batch (~48 puffs where
vanilla has ~360); (2) vanilla removes the dragon at deathTicks 200 but its
ParticleManager cloud lives ~17 more ticks, which an entity-derived emitter
pops off - the oracle's brightest 7 frames had no magma cloud at all; (3) the
GuiBossOverlay fog latch never cleared, and `processDragonDeath` does
`bossInfo.setVisible(false)`, so the oracle's fog ramp snaps back at death and
the same cloud jumps ~4x in brightness (grey 35-60 -> white 180-245).
Fixed all three; the burst now matches the oracle in extent, brightness and
decay, tape mean 0.2266 -> 0.1753/ch (55 frames better, 2 worse), particles
class 51057 -> 26761 px, gate rc 0. Placement stays stochastic: the offsets
come from `EntityDragon.rand`/`Particle.rand`, which no tape records.

Geared dragon-kill tape (master, 2026-07-29): the follow-up tape
`scenario_dragon_kill_geared_20260730T025316Z` failed the gate on two frames
(t=454 4855 px, t=456 4258 px, magma-brighter) and read as "magma's burst runs
a few ticks late". It is not a magma clock error. The oracle's own death rays
are a fingerprint of the client render clock (fixed `Random(432L)`, count and
length from deathTicks) and magma's spokes match the oracle's at IoU 0.900 at
t=454, 0.000 at every neighbouring tick - so the recorded deathTicks IS the
client's. Yet the oracle's cloud loses the BossInfo fog ramp at t=448, six
ticks before that clock reaches 200, and particle brightness has no other
input (`lightmap(0,240)` hardcoded, explosion.png pure white). The fog is
server state (`processDragonDeath`), so the server led the client by 6 ticks
in that recording; the synced tape has both clocks together. No tape field
exposes the server clock (no XP orbs or gateway in the recorder whitelist), so
the three affected frames are filed as divergence 40 in a per-tape
`known_divergences.json`, with no code change: gate rc 0 on both dragon-kill
tapes, original particles 26842 px and max unexplained cluster 3793.

## 2026-07-30 (overnight flywheel: 12-scenario wave 2, 13 merges, 3 recorder gaps proven)

Autonomous overnight run (GOAL.md): census -> scenario synthesis -> serial qrl
recording -> parallel worktree fix delegates -> serial merge+gate on master.
Codex delegates authored fixes in isolated worktrees; every merge, gate rerun,
and divergence filing stayed with the shepherd. 42 of 48 surviving suite tapes
replay rc 0 on the CPU backend this morning.

Landed (each merged with delegate_gate ACCEPT: target tape + 6-pin regression
set, both binaries rebuilt, test-game green):
- Blocks: stone slabs (16 rows, half-box collision, side UV halves), connected
  glass panes, straight stairs (facing/half, two-box collision), trapdoors
  (3/16 poses by metadata), ladders (climb clamp, sneak hold, 0.2 kick, cutout
  plane), cactus (1/16 inset box + neighbor brightness), stonebrick id-98
  model bridge, rails + primed TNT + TNT block.
- Entities: minecart variants, armor stands (NBT pose/equipment, mob atlas),
  cave spider 0.7 scale, creeper fuse swell + white flash, silverfish replay
  mirror, dropped-item ground transforms, boat riding (seat-offset mount,
  passenger physics, paddle model, camera follow).
- HUD/camera: potion HUD order + speed/slowness FOV, creative HUD suppression,
  ItemLayerModel rim normal inversion, duplicate sneak eye-height removed
  (found independently by three delegates), slime horizon closed via sneak eye
  height 0.08F.
- Recorder (new capability): SPacketExplosion capture ("expl" tick field) with
  replay-side additive knockback + block clears; creeper/TNT physics now
  bit-exact through explosions.

Recorder gaps proven and filed (OPEN_DIVERGENCES, dated today):
1. Explosion particle clouds consume client world.rand which no tape records;
   substitute seeds visibly fail. Next step: whitelist particle-instance
   capture in ParticleManager.addEffect (also fixes dragon-death white puffs).
2. Elytra flag-7 arming round-trip varies per recording: 2-tick model makes
   nether_elytra physics-exact (351/351) but breaks elytra_dip at t=59 and
   vice versa. No constant satisfies both; needs recorded flag-7 metadata
   arrival. Candidate diff preserved on wt/netherelytra; revert ce6aa39.
3. Tape header records no gamerules: silverfish_encounter runs
   naturalRegeneration false, replay regens, hp drifts 0.4 at t49 (damage
   amount itself exact). Needs gamerule serialization at recstart.
Also filed: falling_blocks records sky-only goldens (4 deterministic repros;
client has block data, chunk meshes never build) - live oracle debugging
needed; netherelytra world snapshot lacks transient lavafall cells.

Suite hygiene: fresh gate baselines committed for all accepted tapes;
superseded stale-prefix recordings and the four defective falling_blocks
takes retired out of the sweep. One priced regression stands: nether_elytra
t=63 gained 2409 unexplained px (7 small clusters) from tonight's renderer
merges - filed, baseline left old so it stays visible.

Final sweep (nightly_20260730T122129Z, CPU): 48 tapes, 42 rc 0; the six rc 3
all replay physics-clean and pass their committed baselines (2 legacy
full-course tapes improved, tnt + creeper priced at the filed particle gap,
slime priced at the isolated shell contradiction) except nether_elytra, whose
single-line "baseline regression" (t=63, 2409 px) is the one open item and is
deliberately not absorbed. Suite RESULT: FAIL on that line alone.

## 2026-08-01: Java tape state oracle upgrade (world digest + entity truth)

The replay "state gate" stopped being decorative. Recorder.recordTick now
emits a per-tick world digest (`wfnv`: FNV-1a 64 over the 9x9x9 id<<4|meta
volume at floor(feet pos), bit-equal mirror of script.c nearby_hash - note
that constant is a historic non-standard basis, last digit dropped, and both
sides carry a comment saying bit-equality is the only requirement) plus its
anchor (`wfa`), and serializes all gamerules into the recstart header
(OPEN item 3's recorder half). script.c re-anchored nearby_hash at the
double-precision feet position (the float view floor could flip at block
boundaries), and write_state emits `nearby_anchor` + the ingested
`ghost_views`. collect_state_assertions now fails: every modeled tape entity
must reappear in magma's ghost views at its taped position (float32 tol),
and Java-vs-C digests must match on every anchor-agreeing tick; verified
failures exit rc=5. Legacy tapes keep informational verdicts - full pin set
stays ACCEPT (dragon tape: 6150 entity rows matched, 0 mismatches).

Proofs: smoke_zombie re-record = clean pass (373/373 digests, 374 ents,
rc 0); wfnv flipped at t150 -> rc 5, exact tick; ghost shift/drop and
anchor-only disagreement all behave (mutation harness). First valid
falling_blocks take promptly caught a real divergence: magma has no
gravity-block cascade (OPEN item 6 - digests identical t0-19, dig applies
one tick late with the same digest value, cascade diverges from t22, magma
world frozen from t30). OPEN item 4 (sky-only falling_blocks goldens) no
longer reproduces.

## 2026-08-02: blaze k_tick profile + optimization null result (grok lane)

First look inside the 99% "decision subtick" bucket (GPU0 Blackwell, sm_120,
N=4096 curriculum, repeat 4, exclusive flock): k_tick is ~89% of kernel wall
(5.01 ms/decision; k_obs 0.20, k_final 0.50), and inside it lane-0 serial
phys (`blaze_subtick_phys`: dig raycast + psv_physics_tick + items +
furnaces) is 51.4% of cycles, warp-cooperative coal sweep 45.8% (~381
candidates/subtick), post/reward 1.7%. Baseline 2.83M env-ticks/s with a
0.35% run-to-run spread over 3 runs.

Six candidates, all reverted under a 3% keep bar (bitwise CPU==CUDA gate run
per candidate): flat 1-env-per-thread tick loses 35% (warp tick stays ON);
slimmer coal locator -4.6%; 2-envs-per-warp (16-lane coal) -6%; n_items
early-out +0.7%; noinline+launch_bounds +1%; chunk-pointer cache in
psv_collect_blocks +0%. Conclusion: phys is a true dependent chain on lane 0
and coal is already warp-wide; nothing cheap moves it. The one lever left
with real upside is a warp-cooperative psv_collect_blocks with emission
order preserved (prefix-sum emit) - larger change, untried. ncu was blocked
(ERR_NVGPUCTRPERM); enable GPU counters before the next pass. Do not chase
obs/transfer/recenter; they are already <10% combined.

## 2026-08-02: RL flywheel policy path +8% (flywheelopt lane)

The trainer's bottleneck was never arithmetic. At the pinned M config
(N_ENVS=6144, T_CHUNK=32, REPEAT=4, EPOCHS=2, MB=8192, GPU0 exclusive) the
GPU was only 83.2% busy across a chunk: 50,032 kernel launches, 1540 ms busy,
310 ms idle. The idle was host synchronisation from
`torch.distributions.Categorical`, whose argument validation ends in a
`.all()` read back to the host - nine per forward across 81 forwards per
chunk (32 rollout + 1 GAE + 48 minibatches), 729 pipeline drains.

Replacing the nine per-head Categoricals with one padded Gumbel-argmax plus a
single `log_softmax`/gather (`FUSED_SAMPLE`, now the default) takes the chunk
from 1722.93 to 1585.17 ms paired in one lock hold: +8.00%, 456k -> 496k
env-ticks/s. Kernel launches drop to 22,736 and busy fraction rises to 94.3%
while GPU busy TIME is unchanged (1540 -> 1534 ms) - the whole gain is
recovered idle, not saved work. `rollout/sample` goes 40.28 -> 1.39 ms/chunk
and the PPO update drops 111 ms because it built nine Categoricals per
minibatch too.

CUDA Graphs were the lane's top-ranked hypothesis and are a measured
negative. Both are implemented and tested (`GRAPH_ROLLOUT`, `GRAPH_UPDATE`,
off by default): on top of the fused sampler the rollout graph returns
+1.35 ms and the update graph -5.88 ms. Once the syncs are gone there are
only ~93 ms of idle left in a 1585 ms chunk, so there is nothing for a graph
to reclaim. Their apparent +6% in a naive vs-baseline column is entirely the
validation-off that graph capture forces on. Also reverted, reproduced in two
independent pairs: caching the action-decode constant tensors measured
7.9-11.4 ms SLOWER.

Largest remaining item, measured but NOT tested (channels-last is on the
ppo-native-bf16 lane's rejected list): cuDNN NCHW<->NHWC transposes cost
158 ms/chunk, 10% of the wall, bigger than everything this lane banked. That
rejection was established in the native C++ BF16 trainer; whether it carries
to the eager fp32 torch path is one flag and one A/B.

Method note worth keeping: the correctness gate must judge GRADIENTS, not
parameters after K Adam steps. Adam divides by sqrt(v), so a near-zero-
gradient parameter turns 1e-7 of fp32 noise into an O(lr) position change;
eager-vs-eager controls on identical inputs swung 3x run to run. On gradients
the same comparison is stable to three digits (graph vs eager 5.7e-8 against
a 5.7e-8 control). Receipts: `optloop_runs/flywheelopt-v1/PRESERVED/`.

## 2026-08-02/03: nether divergence campaign (pxdiff validation + three lanes)

pxdiff hardened then validated cold: 4 blinded mutation pairs from real
goldens, codex and grok each 4/4 from docs alone, both independently reached
the elytra pose story; their friction became the survey/refinement/pose-note
round (2500c77, 185f68b). Nether tape census: 7/8 rc=0; only nether_elytra
fails, decomposed exactly.

- nethertick (d3efae3): Nether+End terrain zero-diff across blaze CPU, blaze
  CUDA, magma at 7 seeds (origin, per-seed fortress, End island box);
  injected-divergence harness proof; live nether tick probe evidenced-skip
  (blaze env is overworld-only by design).
- flag7rec (1e09743): the arming round trip AND the look-application phase
  both vary per recording - recorder now captures observed flag-7 metadata
  (f7) and pre-travel rotation (ry/rp at ClientTickEvent.START); replay
  forwards set_elytra_flag7/set_look_pre on header opt-in; legacy scripts
  sha-identical. elytra_dip re-record: physics-exact 520/520 at 1e-9, rc=0.
  nether_elytra re-record: physics clean incl. hp through terminal death.
  Vacuous-pass hole closed (FATAL when magma replays 0 ticks).
  OPEN_DIVERGENCES 2 fixed; NEW item 17: elytra fly-into-wall damage is
  server-authoritative in tick and amount (SPacketUpdateHealth round trip;
  client-speed formula gives 9.09 where the server charged 10.21).
- blazefire (4b4d5cc): Blaze AIFireballAttack attackTime is task-owned (pauses
  while the AI task is inactive); live burning flags now feed the recorder
  bit; 78-on/100-off duty cycle receipt + eyeballed frames in
  artifacts/blazefire/.
- Scenario truth: every nether_elytra take dies (fire landing or wall crash
  by arm-tick luck); rc=0 for it is gated on items 5 (lavafall snapshot) and
  17, not on more takes.

- Found while measuring: `nf_to_vanilla` swapped the lava ids - magma's
  generated Nether sea was `flowing_lava` (10) where vanilla is still
  `lava` (11), 123k cells of the patch. Fixed in the follow-up below.

## 2026-07-29 (full-parity harness baseline + Nether lava registry fix)

- Added isolated Java oracle pooling, verified raw pre/post block cuboids,
  strict transition/negative controls, a multi-case matrix, and machine-local
  performance floors. The qrl bridge now has an oracle-only bounded exact-step
  mode; every tape row asserts `player_ticks_existed += 1`. Teleport correction
  and unobserved client ticks had created false `vy`, movement, sprint, fall,
  vitals, and death divergences.
- The grounded fixture now stages stone plus three clear headroom layers by
  numeric block state before teleport, reads the support cell back from the
  server, rejects setup deaths, and waits for vanilla's grounded
  `vy=-0.0784000015258789` tail.
- Strict matrix result: two 60-tick random tapes and one 180-tick non-vacuous
  stone break all have zero divergence across the nine currently observed C
  state features and exact shared-baseline block transitions.
- Fixed `nether_full.h::nf_to_vanilla`: Java 1.11.2 `Block.registerBlocks`
  assigns 10=`flowing_lava`, 11=`lava`, and `ChunkProviderHell` terrain uses
  `Blocks.LAVA`. `CPN_LAVA` now maps to 11 and `CPN_FLOWING_LAVA` to 10.
  Both drivers assert the registry mapping. All four golden seed volumes
  (12345, 0, 7, 49) pass CPU; seed 12345 is bit-exact CPU==CUDA on sm_120.
- Baseline medians on this machine/GPU 1: mc-sim scalar 4,062 SPS, blaze t0
  N=8192 2.94M env-ticks/s, magma CUDA 1080p/vd8 25.57 fps. The new guard
  rejects a regression larger than 5%.

## 2026-07-29 (full shared runtime promoted to the state oracle)

- The prior `trace_game` state vector only exercised the player kernel and
  blanket-null'd systems already present in `GmRuntime`. It now remains as
  `c_state_small.jsonl`; `trace_runtime.py` drives the same shared runtime used
  by interactive/headless play and emits the authoritative `c_state.jsonl`.
- The settled spawn sidecar grew compatibly from five pose fields to ten full
  travel-state fields. Java consumes pose and performs a real settle; the C
  runtime injects exact position, motion, on-ground, and fall distance before
  tick zero. Full runtime and narrow tracer match on every previously observed
  field over the 60-tick probe.
- Runtime state output now carries saturation, exact inactive/player fire
  sentinel, XP, fall/flags/held state, hurt timer, wither potion duration, rich
  item/mob/orb/projectile entity motion, and total time. Air, attack cooldown,
  and death timer remain explicit per-field unsupported values.
- Controlled base traces disable mob spawning and Java random block ticks.
  Without the latter, unrelated leaf decay created mid-run item entities in
  the mining fixture despite exact player and shared-baseline block outcomes.
  Entity comparison is player-relative within 16 blocks; dedicated entity
  fixtures will provide causal non-empty coverage.
- Strict parallel matrix:
  `trace/out/matrix_full_runtime_final/summary.md` — both random seeds and the
  non-vacuous mining case pass with 14 matching feature groups, zero
  divergences, and three explicit unsupported groups. `game/test_script.sh`,
  `game/test_player_ctl.sh`, comparator self-tests, and the quick repository
  sweep all pass.
- Performance guard after promotion: scalar CPU 4,153 SPS, blaze GPU 2.93M
  env-ticks/s, magma CUDA 1080p/vd8 25.54 fps; all above the 95% floors.
  Observation code runs only in the headless state writer, not the gameplay
  hot loop.

## 2026-07-29 (server-step lock + drowning/surfacing parity)

- The client-only exact-step gate still let the integrated server advance on
  its own clock. qrl now parks the server at `ServerTickEvent.START`, grants
  one permit per action, publishes an authoritative snapshot at `END`, and
  waits until the next parked boundary. Raw block dumps use the same locked
  server world. A bounded response mailbox also removed an intermittent
  dropped-response timeout caused by `SynchronousQueue.offer`.
- Added a version-1 checksummed neutral state capsule with deterministic event
  order. Both sides cold-load player pose/motion/vitals/air, inventory,
  dimension, time/weather, and a raw block cuboid before tick zero. Round-trip,
  incomplete-state, order, and checksum-negative tests pass. Entities,
  scheduled/tile work, and RNG cursors remain open.
- Implemented live player air and separated hurt time from damage resistance.
  Drowning damage now follows Java's same-tick 20-to-19 resistance and 10-to-9
  hurt timer order. A 340-tick sealed-column fixture matches air down to -19,
  two 2.5-health hits, and timer decay exactly.
- The surfacing fixture exposed two earlier causal differences. Forge 1.11.2
  treats an eye sample in any water block as submerged for positive fill
  levels, and `EntityLivingBase.updateFallState` performs a second post-move
  water probe. Porting both made the 80-tick jump-out sequence exact: minimum
  air 252, reset to 300 on tick 38, no health loss, and exact fall distance.
- Relative-Y block capsules replaced a fixed y=72..88 cuboid that missed the
  seed-1 staging platform. The five-case aggregate result is
  `c/magma/trace/out/matrix_survival_final/summary.md`: both random walks,
  mining, drowning, and surfacing pass with 15 matching feature groups, zero
  divergences, two explicit unsupported groups, and exact raw block outcomes.
- Narrow script/player/capsule tests and all 13 quick-sweep steps pass.
  Freshly rebuilt performance artifacts also pass: scalar 4,080 SPS, Blaze
  2.91M env-ticks/s, and CUDA game 25.71 fps, above floors 3,858.9, 2.793M,
  and 24.2915.

## 2026-07-29 (fire parity + integrated movement-packet shadow)

- Promoted three fire fixtures into the exact Java-vs-magma matrix. A 45-tick
  raw counter case gates the signed inactive sentinel and 20-tick `ON_FIRE`
  damage cadence; a five-tick submerged case gates immediate extinguishing
  without damage; and a late-staged fire-block case gates both
  `Entity.move` contacts, armorable `IN_FIRE` damage, 0.1 exhaustion, hurt
  immunity, and ignition when the raw counter crosses zero.
- Captured and restored the hidden `FoodStats` exhaustion/timer fields and the
  client movement-packet cursor. Java diagnostics showed that the remaining
  surface/random failures were not fire-order regressions: the integrated
  server consumes the preceding client packet, can retain a different
  `onGround`, and charges jump/movement exhaustion on that delayed path.
- Added a bounded `EntityPlayerMP` movement shadow to `GmRuntime`. The current
  client pose/motion still drives rendering and controls, while one queued
  `CPacketPlayer` updates server position, motion, collision state, fire
  contact, and movement exhaustion before the server player tick. No heap
  allocation or loaded-world scan was added.
- The focused 25-tick surface packet gate matches Java's `onGround` sequence
  `1,1,1,0` across ticks 19-22 and the exact 0.0548 exhaustion increase caused
  by the delayed server jump plus water travel.
- Final aggregate:
  `c/magma/trace/out/matrix_packet_shadow_final/summary.md`. Both random
  walks, mining, drowning, surface reset, the packet boundary, and all three
  fire cases pass with 15 matching feature groups, zero divergences, two
  explicit unsupported groups, and exact shared-baseline block outcomes.
  Script/player component suites also pass.
- Performance remains above every floor after the extra bounded player pass:
  scalar 4,033 SPS, Blaze 2.91M env-ticks/s, and CUDA game 25.02 fps versus
  floors 3,858.9, 2.793M, and 24.2915. Results are in
  `c/magma/trace/out/perf_guard_packet_shadow.json`.

## 2026-07-29 (locked XP entity fixture + exact pickup boundary)

- Extended the parked Java oracle with a deterministic `summon_locked` path
  and authoritative server-side non-player entity snapshots. The trace
  fixture records Java's real XP-orb entity ID plus exact pose, motion, value,
  age, pickup delay, and color, then cold-loads that state into `GmRuntime`.
  The entity diff now checks those fields in addition to the existing pose,
  motion, type, and health state.
- The first 120-tick probe matched orb physics bit-for-bit but awarded XP five
  ticks late in magma. The earliest cause was pickup reach:
  `EntityPlayer.onLivingUpdate` queries
  `playerBox.expand(1.0, 0.5, 1.0)`, whose positive arguments directionally
  extend the maximum faces in 1.11.2. Magma had queried only the raw player
  body box. Porting that exact volume aligns removal and XP award on tick 8.
- Added `xp_pickup_seed_0` to the mandatory matrix. Its behavior gate is
  non-vacuous: one value-5 orb must move toward the player with ages 1 through
  8, disappear on tick 8, and simultaneously award a 5/7 level fraction.
  `c/magma/trace/out/matrix_xp_exact_final/summary.md` has all ten cases
  passing with 15 matching feature groups, zero modeled divergences, two
  explicit unsupported groups, and exact shared-baseline block transitions.
- The native mob, script, player-control, and runtime suites pass. Performance
  also remains above all floors: scalar 4,232 SPS, Blaze 2.91M env-ticks/s,
  and CUDA game 26.49 fps. Results are in
  `c/magma/trace/out/perf_guard_xp_exact.json`.
- Oracle startup now accepts isolated `ORACLE_GRADLE_CACHE` and
  `ORACLE_PROJECT_CACHE` paths, allowing a test client to rebuild/start
  without killing unrelated Minecraft clients that own the default Gradle
  transform cache.

## 2026-07-29 (melee parity + deterministic client/server packet order)

- Added a locked NoAI five-health melee fixture and real physical attack press
  edges to qrl. Authoritative observations now include attack cooldown and
  living hurt/death/resistance timers. The exact 70-tick gate covers the weak
  hit rejection, four accepted one-damage hits, 0.1 exhaustion per accepted
  attack, death-time 1..19, and removal at 20.
- Ported Java's 1.0 player base attack attribute, cooldown scaling, immunity
  delta rule, delayed use-entity/arm packets, and persistent death animation.
  The shared combat-math golden passes Java==CPU==CUDA on GPU 1.
- XP attraction no longer depends on the allocated entity ID modulo 100: the
  fixture captures and restores `xpTargetColor=-100`. Its trajectory, ages
  1..8, removal, and 5/7 award remain exact.
- The broad live-mob suite exposed a separate production bug: ordinary mobs
  never decremented `hurtTime` or `hurtResistantTime`, making the first
  successful hit's immunity permanent. Both timers now age once per live-mob
  tick; component attacks use physical click/release cadence and the delayed
  server packet path.
- Repeated long-lived-client matrices found a qrl scheduling race. The server
  permit was notified at client-tick END but not joined, so the next movement
  or arm packet could land on either side of the permitted tick. qrl now
  finishes that server tick before the client can process the newly installed
  action. It also clears queued KeyBinding press counts, partial block damage,
  and unsaved movement toggles at each parked fixture boundary. Five repeated
  alternating random-walk runs plus the final reused-client matrix are exact.
- Final evidence:
  `c/magma/trace/out/matrix_combat_xp_packet_order_final/summary.md` has all
  11 cases passing with 16 matching feature groups, zero divergences, one
  explicit unsupported player-death timer, and exact shared-baseline block
  transitions. Native mob/script/player/runtime suites, combat CPU/CUDA, and
  all 13 quick-sweep steps pass.
- Performance remains inside the existing 5% budgets: scalar 4,017 SPS, Blaze
  2.91M env-ticks/s, and CUDA game 25.85 fps on GPU 1. Results:
  `c/magma/trace/out/perf_guard_combat_packet_order_final.json`.

## 2026-07-29 (Speed II duration, attribute, and expiry parity)

- Added a locked active-potion fixture plus authoritative server potion and
  movement-speed-attribute observations. The fixture clears and drains old
  effect packets before seeding its exact pre-tick duration.
- Directly adding a potion to a remote Java player updates its effect list but
  intentionally skips attribute modifiers; those normally arrive in
  `SPacketEntityProperties`. That made early movement and expiry depend on
  Netty delivery timing in a reused client. The exact-step oracle now mirrors
  the parked server's potion attribute state on the client before its next
  movement tick, and exposes both attributes for diagnosis.
- Magma now carries a bounded active-effect list, ages it once per tick, and
  applies the exact 1.11.2 operation-2 Speed/Slowness movement constants to
  both movement copies and first-person FOV. The zero-effect hot path performs
  no attribute recomputation or allocation.
- `potion_speed_expiry_seed_0` starts with Speed II duration 6, observes
  durations 5 through 1, removes the authoritative effect on tick 5, and
  exactly matches the faster active movement and settled normal-speed tail.
  Two focused runs on the same long-lived client are exact.
- Final evidence:
  `c/magma/trace/out/matrix_potion_speed_final/summary.md` has all 12 cases
  passing, each with 16 matching feature groups, zero modeled divergences, one
  explicit unsupported player-death timer, and exact shared-baseline block
  transitions. Mob/script/player/runtime component suites, 2,432-line
  player-survival CPU/CUDA parity, and all 13 quick-sweep steps pass.
- Performance remains above every floor on GPU 1: scalar 4,127 SPS, Blaze
  2.91M env-ticks/s, and CUDA game 25.66 fps. Results:
  `c/magma/trace/out/perf_guard_potion_speed_final.json`.

## 2026-07-29 (exact NoAI-pig and XP-orb state capsules)

- Extended the version-1 neutral capsule with two explicit exact entity
  capabilities. A living NoAI pig now restores its authoritative entity ID,
  pose, motion, health, and hurt/resistance/death timers. An XP orb restores
  its ID, pose, motion, value, age, pickup delay, color, and reflected
  `xpTargetColor`.
- Capsule validation rejects duplicate entity IDs and incomplete exact
  payloads. The self-test covers deterministic entity event ordering, missing
  hidden XP state, corruption, and incomplete-capability rejection. It is now
  a normal quick-sweep step.
- The runtime trace suppresses legacy fixture injection whenever the capsule
  emitted the corresponding entity. The final melee and XP scripts each
  contain exactly one spawn event, proving the behavior gates exercise cold
  capsule restoration instead of a second side channel.
- Final evidence:
  `c/magma/trace/out/matrix_capsule_entities_final/summary.md` has all 12 cases
  passing with 16 matching feature groups, zero modeled divergences, one
  explicit unsupported player-death timer, exact shared-baseline blocks, and
  passing non-vacuous behavior gates. Capsule, runtime, and script component
  tests pass.
- This slice adds no hot-loop work. GPU-1 medians remain above every floor:
  scalar 4,156 SPS, Blaze 2.91M env-ticks/s, and CUDA game 25.30 fps. Results:
  `c/magma/trace/out/perf_guard_capsule_entities_final.json`. General entity
  payloads, tile entities, scheduled ticks, and RNG cursors remain O-02 work.

## 2026-07-29 (scheduled-update capsule and first active water dispatch)

- Added scoped authoritative Java capture for pending block updates, including
  absolute due time, priority, and relative TreeSet tie-break order. The
  capture uses the public `WorldServer.getPendingBlockUpdates` API over the
  capsule-aligned 65x65 area and rejects truncated snapshots.
- The version-1 capsule now restores a proof-safe exact subset: inert stone and
  a level-0 dynamic-water source on a stone floor with four open horizontal
  neighbors. Other scheduled block states remain captured-only; the runtime
  rejects them instead of claiming broad fluid or redstone support.
- Added a bounded allocate-once 4,096-entry pending queue to `GmRuntime`.
  Empty worlds pay one count check. Inert stone drains at its exact due
  boundary. The active water fixture dispatches on tick 2, creates four
  level-1 neighbors, and schedules NORTH/source/SOUTH/WEST/EAST five ticks
  later, matching Java on every observed tick.
- Raw block comparison now exports the shipped full runtime after replay.
  Both sides start from the same capsule cuboid, replacing the old active-world
  comparison against the narrow physics tracer. The water fixture has four
  required mutations and finishes byte-exact across all 10,625 packed cells.
- Final evidence:
  `c/magma/trace/out/matrix_water_source_dispatch_final/summary.md` has all 14
  cases passing with 17 matching feature groups, zero modeled divergences, one
  explicit unsupported player-death timer, and passing state, behavior, and
  block gates. Runtime/script/capsule component tests and all 14 quick-sweep
  steps pass.
- GPU-1 performance remains above every floor: scalar 4,160 SPS, Blaze 2.91M
  env-ticks/s, and CUDA game 25.79 fps. Results:
  `c/magma/trace/out/perf_guard_water_source_dispatch_final.json`. Descendant
  water updates, downward flow, lava, reactions, and general scheduled work
  remain active backlog rather than implied coverage.

## 2026-07-29 (water descendants and one-block downward flow)

- Ported the air/water/stone subset of 1.11.2
  `BlockDynamicLiquid.updateTick` into the shared runtime. It preserves
  in-place updates, source-to-static transitions, slope-direction order,
  static-water neighbor wakeups, queue deduplication, and the five-tick water
  cadence without scanning the world or allocating in the tick loop.
- Extended the capsule's proof-safe scheduled-water shape to a bounded
  two-air-layer basin over a flat stone floor. A pending water block may occupy
  either layer, which admits ordinary horizontal propagation and one-block
  downward flow while rejecting unknown materials and terrain shapes.
- Added two non-vacuous matrix cases. Flat propagation dispatches a source on
  tick 2 and its five children on tick 7, finishing with a static source,
  level-1/2 rings, 12 ordered descendants, and 13 exact mutations. Downward
  propagation creates metadata-8 falling water, queues the falling cell before
  its woken source, then creates two level-1 rings with 10 descendants and 9
  exact mutations.
- Final evidence:
  `c/magma/trace/out/matrix_water_downward_two_dispatches_final/summary.md`
  has all 16 cases passing with 17 matching feature groups, zero modeled
  divergences, one explicit unsupported player-death timer, and passing state,
  behavior, and raw-block gates. Runtime, script, capsule, and all 14
  quick-sweep steps pass.
- GPU-1 performance remains above every floor: scalar 4,118 SPS, Blaze 2.90M
  env-ticks/s, and CUDA game 25.20 fps. Results:
  `c/magma/trace/out/perf_guard_water_downward_two_dispatches_final.json`.
  Lava is the next W-01 slice; complex water materials and arbitrary terrain
  remain explicit backlog rather than implied coverage.

## 2026-07-29 (lava generations, reaction, and stable platform boundary)

- Added exact pending block-10 capture and a proof-safe capsule capability for
  level-0 lava on a flat stone plane. The shared runtime ports the deterministic
  flat trajectory with lava's decay increment 2, slope radius 2, and 30-tick
  Overworld cadence. It continues to reject externally loaded non-source lava;
  arbitrary states that need Java's random 30-vs-120-tick branch still require
  a world-RNG cursor.
- The 31-tick source case dispatches at tick 27 after its natural setup
  schedule, creates four metadata-2 neighbors, and leaves the exact
  NORTH/source/SOUTH/WEST/EAST child queue. The 61-tick case dispatches those
  five children at tick 57, settles the source to block 11, creates a
  metadata-4 outer ring, and leaves 12 exact descendants. Its 13 mutations and
  all 10,625 post-state cells are byte-exact.
- Added an enclosed reaction fixture with pending water and lava entries.
  Water settles without spreading at tick 2; raised lava flows down on tick 27,
  replaces that water source with stone, and is woken/requeued for +30. The
  non-vacuous raw gate requires exactly that one water-to-stone mutation.
- An aggregate rerun exposed seed-1 harness contamination: platform staging
  could wake 525 natural water blocks, and alignment latency decided whether
  they settled before the capsule snapshot. Java always reached the same final
  state. The oracle now drains 40 controlled setup ticks after platform
  staging and before any fixture, covering the 30-tick lava cadence. A focused
  seed-1 replay and the full matrix prove the boundary is stable.
- Final evidence:
  `c/magma/trace/out/matrix_lava_water_reaction_stable_final/summary.md`
  has all 19 cases passing with 17 matching feature groups, zero modeled
  divergences, one explicit unsupported player-death timer, and exact
  full-runtime raw outcomes. Runtime/script/capsule tests and all 14
  quick-sweep steps pass.
- GPU-1 performance remains above every floor: scalar 4,307 SPS, Blaze 2.91M
  env-ticks/s, and CUDA game 25.39 fps. Results:
  `c/magma/trace/out/perf_guard_lava_water_reaction_final.json`. The setup
  drain is Java-oracle cold-path work and adds no magma tick-loop cost.

## 2026-07-29 (exact scheduled falling sand and landing)

- Added a final-stage oracle fixture boundary: movement-packet normalization
  completes first, then the block is staged while the server remains parked.
  This preserves a newly scheduled two-tick sand update in the pre-tick
  capsule instead of consuming it during setup.
- Promoted a proof-safe BlockFalling slice: metadata-0 sand, a clear vertical
  air column, and stone support fully represented in the capsule. The shared
  runtime uses a fixed 16-entry falling-block pool with an empty-count fast
  path. It matches Java's float-derived initial Y offset, gravity, move/drag
  order, stone collision, landing placement, and the landed block's +2
  stability update.
- Extended authoritative entity snapshots with falling block id/meta,
  `fallTime`, and origin. The focused gate matches all nine airborne
  positions and velocities, removes the entity on tick 10, keeps the stability
  update pending through tick 11, and drains it on tick 12. The final raw
  cuboid is byte-exact: only `(12,80,8)` sand-to-air and `(12,78,8)`
  air-to-sand change.
- Capturing `Entity.nextEntityID` exposed an important boundary: integrated
  client and server construction share the process-global counter, and
  unrelated entities outside the bounded capsule consumed 48 IDs before the
  target spawn. The cursor is therefore captured-only, not falsely restored
  as exact. Emergent falling blocks compare by stable origin plus block state;
  raw engine EIDs remain available as diagnostics.
- Final evidence:
  `c/magma/trace/out/matrix_falling_sand_stable_1/summary.md` has all 20 cases
  passing with 17 modeled feature matches, zero divergences, one explicit
  unsupported player-death timer, and exact full-runtime raw outcomes. The
  capsule/runtime narrow tests and all 14 quick-sweep steps pass.
- GPU-1 performance remains above every floor: scalar 4,243 SPS, Blaze 2.91M
  env-ticks/s, and CUDA game 25.65 fps. Results:
  `c/magma/trace/out/perf_guard_falling_sand_final.json`. Random ticks, block
  fire spread, and light propagation are the next W-01 slices.

## 2026-07-29 (exact RNG transfer, wheat callback, and natural selector)

- Added authoritative capture of the private 48-bit `World.rand`
  `java.util.Random` seed and exact capsule restore into `GmRuntime`. The C
  implementation follows Java's specified LCG and bounded `nextInt` behavior;
  narrow tests verify `Random(seed=9)` advances from internal cursor
  `0x5deece664` to `0xbaebde0bf09f`. The signed 32-bit
  `World.updateLCG` cursor is now captured and restored too.
- Added a controlled oracle input that queues one real
  `Block.randomTick` call while the server is parked, then invokes it on the
  next `ServerTickEvent.START` thread before the ordinary server tick. This
  isolates actual callback behavior without claiming that loaded-chunk
  iteration or `World.updateLCG` selection is implemented.
- Ported the complete wheat growth callback slice: neighbor light, hydrated
  farmland fertility, 3x3 float-weighted growth chance, adjacent-crop penalty,
  truncation to the RNG bound, and age metadata update. The focused proof at
  `c/magma/trace/out/crop_random_tick_exact_1` changes exactly one cell,
  `(12,78,8)` from wheat age 0 to age 1; all 10,625 final packed states match.
- Added a separate natural `WorldServer.updateBlocks` fixture. It sanitizes
  random-tick membership in the actual loaded chunks, proves one eligible
  section/block, records target chunk rank, and accounts for ice/snow
  `updateLCG` advances before the selector. The focused run iterated 187
  chunks, removed 182,081 unrelated random-tick blocks, observed seven
  pre-selection advances, and let vanilla naturally select capped grass.
  `c/magma/trace/out/grass_random_selection_exact_1` has the same sole
  grass-to-dirt mutation and byte-exact 10,625-cell final state on magma.
- Aggregate evidence:
  `c/magma/trace/out/matrix_random_selection_stable_1/summary.md` has all 22
  cases passing with 17 modeled feature matches, zero divergences, one
  explicit unsupported player-death timer, and passing behavior/raw-block
  gates. The 14-step quick sweep is green with no skips.
- Performance remains above every floor with no random-tick work in the
  ordinary idle path: scalar 4,216 SPS, Blaze 2.91M env-ticks/s, and CUDA game
  25.74 fps. Results:
  `c/magma/trace/out/perf_guard_random_selection_final.json`. General
  multi-section loaded-world selection/active-set persistence, grass spread,
  leaves, fire spread, and light propagation remain active W-01 work.

## 2026-07-30 (live block-light addition and raw light oracle)

- Added an uncontaminated tick-boundary world mutation to the Java oracle. The
  socket thread queues a numeric block state while the integrated server is
  parked; the real `WorldServer.setBlockState(..., 3)` runs on the server
  thread immediately after tape tick 0 is permitted. Pre-state blocks and
  light are therefore sampled before the mutation.
- Added parked raw `EnumSkyBlock.BLOCK` cuboid dumps and the matching cold
  magma exporter. Both use one byte per cell in the existing y/z/x block-box
  order. `trace/light_diff.py` validates size/range, reports the first
  coordinate and value confusions, writes every mismatch to CSV, exits 4 on a
  parity error, and has exact-identity plus changed-value negative controls.
- The focused proof at
  `c/magma/trace/out/block_light_add_exact_1` places glowstone at
  `(12,79,8)` on tick 0. The pre-state source is air with block light 0; the
  post-state source is light 15 with an adjacent light 14. Java and magma are
  exact for all 10,625 packed block states and all 10,625 raw block-light
  cells, with exactly one air-to-glowstone mutation and zero modeled state
  divergences.
- Aggregate evidence:
  `c/magma/trace/out/matrix_block_light_chunk_edge_stable_1/summary.md` has all
  27 cases passing, including the new non-vacuous behavior/light gates. A
  complementary focused proof at
  `c/magma/trace/out/block_light_remove_exact_1` starts with the exact
  glowstone field, removes the source at tick 0, and matches all 10,625 cells
  as the entire field drains to zero. The opacity proof at
  `c/magma/trace/out/block_light_opacity_exact_1` inserts stone beside the
  source; the blocker changes 14-to-0 and the cell behind it reroutes 13-to-11
  with all 10,625 light cells exact. The overlap proof at
  `c/magma/trace/out/block_light_overlap_exact_1` inserts a second glowstone
  four blocks away; its cell changes 11-to-15, the overlap edge changes
  12-to-14, and all 1,304 changed light cells match. The dedicated boundary
  proof at `c/magma/trace/out/block_light_chunk_edge_exact_1` places its
  source at local x=15; the immediate next-chunk x=16 cell becomes light 14
  and all 2,342 changed cells match. The
  14-step quick sweep is green with no skips.
- That aggregate work caught an older harness flaw instead of hiding it:
  capped-grass selection sometimes produced no Java mutation because the
  callback was light-dependent, even though the coordinate derivation was
  intact. The natural-selector gate now uses age-zero cactus, whose callback
  is light-independent. Seed 75,682 has no ice/snow `updateLCG` success in its
  first 180 chunk samples; the gate rejects a target rank outside that prefix
  or any pre-selection advance. Focused runs
  `cactus_random_selection_exact_3` and `_4` plus the full matrix all produce
  the exact age-0-to-age-1 mutation.
- The added export and mutation controls are harness-only cold paths. GPU-1
  performance remains above every floor: scalar 4,044 SPS, Blaze 2.91M
  env-ticks/s, and CUDA game 25.57 fps. Results:
  `c/magma/trace/out/perf_guard_block_light_opacity_final.json`. Block-light
  overlapping sources, chunk edges, skylight, and fire spread remain active
  W-01 work.

## 2026-07-30 (exact saved skylight and bounded live updates)

- Added parked raw `EnumSkyBlock.SKY` dumps on Java and magma, using the same
  inclusive cuboid and y/z/x one-byte-per-cell order as block light. The
  generic strict light comparator now labels block and sky results separately
  and retains its exact-identity and changed-value negative controls.
- The first roof-placement probe exposed 841 mismatches, but most were not a
  tick bug: the capsule transferred exact blocks while magma reconstructed a
  converged skylight field instead of loading Minecraft's saved `SkyLight`
  arrays. Version-1 capsules now optionally carry `sky_light.u8`, validate its
  length, 0..15 range, and SHA-256, resolve the cold block batch once, then
  install the saved nibbles as the exact pre-tick boundary. The capability is
  `exact` only when the payload is present and `captured_only` otherwise.
- Replaced live opacity edits' whole-dirty-chunk monotonic spread with the
  bounded two-phase `World.checkLightFor(SKY)` darken/brighten queue and
  `Chunk.relightBlock`-style height/column update. A roof added at
  `(12,86,8)` changes exactly nine Java cells: its own stored light 15-to-0 and
  eight cells below 15-to-14. Roof removal reverses them, and a local-x15
  placement proves the same transition at a chunk boundary. All three focused
  runs match every one of 10,625 block, block-light, and skylight cells.
- The first aggregate run caught a separate natural-selector harness race.
  `PlayerChunkMap.getChunkIterator()` is lazy, so transient membership could
  change while earlier chunks tick after a previewed target rank was recorded.
  The isolated fixture now promotes the already-loaded target entry to rank
  zero and records `target_promoted=1`; general loaded-world ordering remains
  explicitly unclaimed. The focused cactus proof again has exactly one
  age-0-to-age-1 mutation.
- Final evidence:
  `c/magma/trace/out/matrix_skylight_live_exact_2/summary.md` has all 30 cases
  passing with 17 modeled feature matches, zero divergences, one explicit
  unsupported player-death timer, and strict behavior/raw-block/raw-light
  gates. The 14-step quick sweep is green with no skips.
- The new payload/export paths are cold-only, and unchanged frames do no new
  skylight work. GPU-1 performance remains above every floor: scalar 4,196
  SPS, Blaze 2.90M env-ticks/s, and CUDA game 25.27 fps. Results:
  `c/magma/trace/out/perf_guard_skylight_live_final.json`. Broader skylight
  shapes and fire spread remain active W-01 work.

## 2026-07-30 (exact dry fire callback and scheduled spread)

- Added a minimal Java fire proof: age-zero fire at `(12,78,8)` on the shared
  stone platform and one east oak plank. `Random(seed=36)` changes exactly that
  plank to age-zero fire. Magma now ports the 1.11.2 dry/non-humid
  NORMAL-difficulty `BlockFire.updateTick` body, including immutable source-age
  semantics, the six direct faces in Java order, air-pocket spread, queue
  deduplication, and unconditional RNG draws for nonflammable targets.
- Pending fire entries now carry authoritative humidity, difficulty,
  `doFireTick`, and rain context in the neutral capsule. Only the bounded
  air/stone/planks/fire proof region is promoted. The capsule selftest accepts
  that context and excludes a humid entry from the exact scheduled subset.
- Added a controlled scheduled-callback hook. It replaces the automatic
  placement update with a requested due time and reseeds immediately before
  that world tick. The +3 case remains pending on ticks 0-1, dispatches on tick
  2, burns the same sole plank, and leaves source and child work ordered at
  +35. Direct-callback and scheduled cases match all 10,625 blocks and the
  complete represented pending list.
- The aggregate block-light boundary gate no longer hardcodes the total count
  of far-edge lit cells, which can vary with natural leaf state outside the
  cleared platform. It still requires the local-x15 source 0-to-15,
  next-chunk x16 neighbor 0-to-14, more than 2,000 non-vacuous changes, and
  byte identity across the full cuboid.
- Final aggregate evidence is
  `c/magma/trace/out/matrix_fire_exact_full_2/summary.md`: all 32 cases pass
  with 17 modeled feature matches, zero divergences, one explicit unsupported
  player-death timer, and strict state/block/light/behavior gates. The focused
  five-case fire matrix is
  `c/magma/trace/out/matrix_fire_exact_1/summary.md`.
- Fire work stays behind direct/pending-fire branches; idle ticks gain no world
  scan. GPU-1 guards remain green at 3,964 scalar steps/s, 2.91M Blaze
  env-ticks/s, and 25.92 CUDA fps:
  `c/magma/trace/out/perf_guard_fire_exact.json`.

## 2026-07-30 (redstone scheduling foundation and lamp parity)

- Added the first edit-driven redstone substrate. Live block changes notify
  WEST, EAST, DOWN, UP, NORTH, and SOUTH in Java order; the shared power query
  probes DOWN, UP, NORTH, SOUTH, WEST, and EAST. Redstone block 152 is the
  first exact producer, and lamps 123/124 are the first consumer.
- A tick-0 redstone-block placement beside an unlit lamp now changes both
  source and lamp in the same tick. Lit lamps emit block light 15 even though
  the legacy block-property table omits the cut redstone ID range. The focused
  proof at `c/magma/trace/out/redstone_lamp_add_exact_2` matches both packed
  mutations and all 10,625 raw block-light cells.
- Removing the source schedules the lit lamp exactly four total-time ticks
  later. Java and magma retain one identical block-124 queue entry on
  observations 0-2, dispatch on observation 3, change 124 to 123, and drain
  the light field exactly. The focused proof is
  `c/magma/trace/out/redstone_lamp_off_exact_2`.
- Extended scheduled-state capture with an explicit block-ID filter and
  promoted lit-lamp callbacks through a bounded capsule proof. Direct
  redstone-block power is exact; the unpowered proof checks adjacent air/stone
  plus each stone's possible strong-power inputs and permits only inert
  air/stone/log/leaves/lamp states. An unimplemented redstone-torch negative
  is excluded. The
  capsule-loaded powered callback at
  `c/magma/trace/out/redstone_lamp_saved_powered_exact_2` restores absolute
  due time, priority, and order, drains on tick 2, and correctly leaves the
  powered lamp lit.
- The first aggregate run exposed an over-conservative proof guard: the saved
  terrain contains an inert oak log beneath the fixture's platform, so magma
  drained the queue without executing the lamp callback. Logs and leaves were
  added only to the zero-power whitelist; unknown power producers remain
  rejected. The same case then passed on the previously failing oracle.
- Final aggregate evidence:
  `c/magma/trace/out/matrix_redstone_foundation_full_2/summary.md` has all 35
  cases passing with strict state, behavior, packed-block, and raw-light
  gates. The repository-wide 14-step quick sweep is green with no skips.
- Redstone work adds no idle scan: six constant-time neighbor probes run only
  for block edits, and delayed lamps use the existing bounded pending queue.
  GPU-1 guards remain above every floor at 4,169 scalar steps/s, 2.92M Blaze
  env-ticks/s, and 26.73 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_foundation.json`.

## 2026-07-30 (floor lever power and lamp integration)

- Added the first R-02 producer on top of the redstone scheduler foundation.
  A supported floor-mounted lever uses metadata 5 when off and 13 when
  powered. The shared weak-power query now recognizes the powered bit without
  teaching the lamp about individual source types.
- Focused Java probes first exposed the expected sole divergence: lever
  metadata changed identically, but magma left the adjacent lamp unlit.
  After adding lever weak power,
  `c/magma/trace/out/redstone_lever_lamp_on_exact_2` matches the 5-to-13 lever
  edit and 123-to-124 lamp transition in the same tick.
- The initial raw-light rerun caught a separate cut-ID fallback: lever 69 was
  treated as an opaque full block, so magma stopped the lit lamp's westward
  field at the lever while Java propagated light 14 through it. Lever opacity
  is now exactly zero.
- The complementary
  `c/magma/trace/out/redstone_lever_lamp_off_exact_1` changes metadata 13-to-5,
  retains one lamp callback through observations 0-2, dispatches at absolute
  total-time +4 on observation 3, changes lamp 124-to-123, and matches all
  10,625 block-light cells. The capsule-loaded proof at
  `redstone_lever_lamp_saved_powered_exact_1` recognizes direct powered-lever
  state, restores due/priority/order, and drains the callback without changing
  the still-powered lamp.
- A late aggregate fire behavior gate was corrected after exact Java/C state
  showed its assumption was invalid: a placement-created source callback can
  sort before or after the new +35 child depending on its incidental due time.
  The gate now identifies source and child by position/block, preserves the
  source's recorded due time, requires the child at exactly +35, and still
  requires complete ordered queue equality.
- Final aggregate evidence:
  `c/magma/trace/out/matrix_redstone_lever_full_2/summary.md` has all 38 cases
  passing. The 14-step quick sweep is green with no skips.
- Lever support remains edit/pending-driven. GPU-1 medians remain above all
  floors at 4,154 scalar steps/s, 2.91M Blaze env-ticks/s, and 25.21 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_lever.json`.

## 2026-07-30 (saved stone-button release and queue handoff)

- Added the first timed control callback. The exact proof uses a floor-mounted
  powered stone button 77:13, an east lit lamp, stone support, and otherwise
  clear direct neighbors. The capsule restores one button update at absolute
  total-time +20 and rejects the same entry when the button is already
  released.
- Button weak power uses the same metadata bit as the lever, and button
  opacity is zero. On the due callback magma changes 77:13 to 77:5 and invokes
  the existing neighbor path; this creates the lamp's independent +4 callback
  rather than folding both transitions into one timer.
- The focused proof at
  `c/magma/trace/out/redstone_button_lamp_release_exact_1` retains the button
  queue for observations 0-18, releases it on 19, retains the lamp queue on
  19-22, and turns the lamp 124-to-123 on 23. Button and lamp are the only raw
  mutations, and all 10,625 final block-light bytes match Java.
- Player-originated block edits now enter the same six-neighbor notification
  path as scripted live edits, with no idle-world work.
- Final aggregate evidence:
  `c/magma/trace/out/matrix_redstone_button_full_1/summary.md` has all 39 cases
  passing. The repository-wide 14-step quick sweep is green with no skips.
- The callback runs only while present in the existing pending queue.
  GPU-1 medians remain above every floor at 4,020 scalar steps/s, 2.91M Blaze
  env-ticks/s, and 26.45 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_button.json`.

## 2026-07-30 (bounded flat redstone-wire topology)

- Added the first exact dust component on a flat, air-covered, stone-supported
  layer. An edit that reaches wire gathers only its connected component into a
  fixed 256-entry work set, seeds direct power producers at 15, and performs a
  bounded breadth-first attenuation. Vertical wire adjacency and component
  overflow are rejected before mutation rather than partially simulated.
- The first focused probe at
  `c/magma/trace/out/redstone_wire_lamp_on_probe_1` exposed exactly two missing
  outcomes: dust stayed metadata 0 and the lamp stayed off. After the fix,
  `redstone_wire_lamp_on_exact_1` matches the sole source addition, dust
  0-to-15, lamp 123-to-124, and all 10,625 block-light bytes.
- The removal proof at `redstone_wire_lamp_off_exact_1` drains dust 15-to-0 in
  the source-removal tick, retains the lamp's independent callback through
  observations 0-2, and changes lamp 124-to-123 on observation 3. Raw blocks,
  the ordered queue, and all sampled light bytes are exact.
- The attenuation boundary is non-vacuous. A 15-wire line settles to metadata
  15 through 1 and lights its endpoint, producing 17 exact mutations. With a
  16th wire, the last state remains zero and the endpoint lamp remains off,
  producing the expected 16 mutations. Focused artifacts are
  `redstone_wire_length_15_exact_1` and
  `redstone_wire_length_16_exact_1`.
- A T-branch converges to center 15 and three leaves at 14. An eight-wire
  powered loop drains every stale value to zero in the source-removal tick,
  proving the implementation does not preserve cyclic self-power. Focused
  artifacts are `redstone_wire_branch_exact_1` and
  `redstone_wire_loop_off_exact_1`.
- Final aggregate evidence:
  `c/magma/trace/out/matrix_redstone_wire_topology_full_1/summary.md` has all
  45 cases passing. The 14-step quick sweep is green with no skips.
- Dust has no idle-tick path and allocates no heap memory. GPU-1 medians remain
  above every floor at 4,063 scalar steps/s, 2.91M Blaze env-ticks/s, and
  25.50 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_wire.json`.

## 2026-07-30 (floor redstone-torch inverter and saved callbacks)

- Added the metadata-5 floor-torch inverter as the next R-02 dependency.
  Replacing its stone support with a redstone block schedules one block-76
  callback at +2; removing support power schedules the complementary block-75
  callback at +2. Both remain pending on observation 0 and dispatch on
  observation 1.
- The initial probe at
  `c/magma/trace/out/redstone_torch_floor_off_probe_1` isolated the missing
  queue and 76:5-to-75:5 transition. The focused live proofs
  `redstone_torch_floor_off_exact_1` and
  `redstone_torch_floor_on_exact_1` now match exactly two support/torch
  mutations, the complete ordered queue, and every one of 10,625 light bytes.
  Lit torch opacity is zero and its emitted block light is 7.
- Added a bounded capsule proof for block 75/76 callbacks. It requires a
  metadata-5 floor torch, the exact stone or redstone-block support required
  for that edge, and clear non-support neighbors. The selftest promotes the
  powered block-76 context and rejects its wrong-support negative.
- Saved-edge proofs at
  `redstone_torch_floor_saved_off_exact_2` and
  `redstone_torch_floor_saved_on_exact_2` restore absolute due time, priority,
  and order, then produce only the torch mutation with exact raw light.
- One aggregate attempt was discarded after oracle instance 1 stopped
  answering reset requests. Recycling that isolated client made the same
  saved-off focused case pass there. The clean aggregate at
  `c/magma/trace/out/matrix_redstone_torch_saved_full_2/summary.md` has all 49
  cases passing, and the 14-step quick sweep is green with no skips.
- Torch work is neighbor-edit and pending-queue driven. GPU-1 medians remain
  above every floor at 4,176 scalar steps/s, 2.91M Blaze env-ticks/s, and
  26.19 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_torch_inverter.json`.

## 2026-07-30 (multi-tick oracle inputs and redstone-torch burnout)

- Added a shared lockstep block-edit sequence format:
  `TICK DX DY DZ BLOCK META`. Java queues each edit while the server is parked
  immediately before admitting that tape tick; magma applies the same
  player-relative edit before the same tick. Empty files, malformed values,
  out-of-tape ticks, and duplicate edits in one tick are rejected. A stable
  runtime-script sort preserves mutation-before-action order.
- The backward-compatibility proof
  `c/magma/trace/out/redstone_torch_floor_off_sequence_exact_1` expresses the
  previously accepted one-edit floor-torch case through the new harness and
  still matches the +2 queue, both block mutations, and all 10,625 light cells.
- The unfixed 36-tick probe at
  `c/magma/trace/out/redstone_torch_floor_burnout_probe_1` remained exact
  through seven off/on cycles, then isolated the first divergence at
  observation 29: Java retained block 75 and one +160 callback while magma
  relit at observation 31. The final mismatch was exactly the torch plus the
  231 light cells influenced by its level-7 emission.
- Added a chronological per-world toggle list that is touched only by a torch
  callback. Each callback prunes entries strictly older than 60 ticks; each
  lit-to-unlit edge appends its position/time; the eighth matching entry burns
  out. The list allocates only on its first real toggle and grows with active
  history, so the idle tick path remains unchanged.
- Burnout advances `World.rand` exactly as Java 1.11.2 does: two
  `nextFloat()` calls for sound pitch and fifteen `nextDouble()` calls for
  smoke coordinates, totaling 32 LCG advances. It then queues block 75 at
  +160. A support release cannot replace that same-block/same-position entry
  with +2 work; at +160, stale history prunes and the torch relights.
- The focused two-case matrix at
  `c/magma/trace/out/matrix_redstone_torch_burnout_focused_1/summary.md`
  proves both the still-burned-out state and recovery at observation 189.
  Both have exact pending queues, raw blocks, and all 10,625 light cells.
- The clean aggregate at
  `c/magma/trace/out/matrix_redstone_torch_burnout_full_1/summary.md` has all
  51 cases passing. The performance guard is also green at 4,163 scalar
  steps/s, 2.92M Blaze env-ticks/s, and 25.86 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_torch_burnout.json`.

## 2026-07-30 (wall torches and hidden-history checkpoint continuation)

- Generalized the torch support direction and callback path from metadata 5
  floor torches to all four wall orientations. Metadata 1/2/3/4 now resolve
  their west/east/north/south supports, retain their orientation when changing
  block 75/76, and notify from the same bounded edit/scheduled-work path.
- The focused off/on matrix at
  `c/magma/trace/out/matrix_redstone_torch_wall_focused_1/summary.md` staggers
  four support edits across ticks 0-3. Each orientation creates its exact +2
  callback and all eight support/torch mutations plus all 10,625 block-light
  cells match Java.
- Promoted the torch toggle list from private implementation detail to
  authoritative checkpoint state. The QRL bridge reflects the per-world
  chronological `BlockRedstoneTorch.toggles` list, canonical traces compare it
  as the nineteenth feature, and the capsule validates and restores up to
  4,096 complete entries. Malformed, incomplete, out-of-order, and
  out-of-range histories are rejected.
- Added a parked post-observation checkpoint export and a trace-suffix slicer.
  At observation 27 of the burnout fixture, Java's blocks and block-light
  bytes are identical to the original visible fixture while its checkpoint
  contains seven toggle records. Loading that capsule in magma, applying only
  the final power/release edits, and running eight ticks reproduces Java's
  eighth-toggle burnout, +160 queue, eight-entry history, final blocks, and
  all 10,625 light cells. The reusable entry point is
  `c/magma/trace/run_torch_checkpoint_regression.sh`.
- The post-schema aggregate at
  `c/magma/trace/out/matrix_redstone_torch_checkpoint_full_1/summary.md` has
  all 53 cases passing with 18 matching features, zero divergences, and only
  `death_time` explicitly unsupported. The checkpoint performance guard is
  green at 4,281 scalar steps/s, 2.91M Blaze env-ticks/s, and 25.40 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_torch_checkpoint.json`.

## 2026-07-30 (directional lever strong power through stone)

- Added the first bounded `World.getStrongPower` path. A stone support checks
  the directional strong output of an attached powered lever or stone button;
  weak power remains the direct-neighbor path. Lamp placement now evaluates
  its own on-add power state, matching `BlockRedstoneLight.onBlockAdded`.
- The unfixed probe at
  `c/magma/trace/out/redstone_lever_strong_power_probe_1` isolated one causal
  block mismatch: Java placed lit lamp 124 while magma retained unlit lamp
  123. The 2,204 light mismatches were entirely downstream of that block.
- The focused matrix at
  `c/magma/trace/out/matrix_redstone_lever_strong_power_focused_3/summary.md`
  proves same-tick lamp-on through stone, powered-lever removal followed by
  the exact +4 lamp-off delay, and all six DOWN/UP/NORTH/SOUTH/WEST/EAST
  lever outputs. The orientation case produces six exact air-to-124 mutations
  and all 10,625 light cells match.
- Powered-control removal now mirrors the block-specific second notification
  around the attached support. This is edit-driven only; no loaded-world scan
  was added.
- The aggregate at
  `c/magma/trace/out/matrix_redstone_lever_strong_power_full_1/summary.md` has
  all 56 cases passing. GPU-1 medians remain above floor at 4,186 scalar
  steps/s, 2.91M Blaze env-ticks/s, and 25.36 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_lever_strong_power.json`.

## 2026-07-30 (bounded vertical redstone-wire graph)

- Replaced the flat-only dust work set with a fixed-capacity 3D component
  graph. Air-covered wire on stone supports now follows vanilla's same-level
  edge, one-block climb when the lower wire has clear headroom, and one-block
  descent when the adjacent same-level cell is not a represented normal cube.
  The cap remains 256 wires and the relaxation queue is statically bounded;
  there is no heap allocation or idle tick work.
- The unfixed climb probe at
  `c/magma/trace/out/redstone_wire_climb_up_probe_1` isolated four causal
  outcomes: Java settled dust to 15/14/13 over the step and lit the endpoint
  lamp, while magma changed only the source. The 2,329 light differences were
  downstream of that lamp.
- Focused Java proofs now cover climb power-on, climb source removal, and
  descent. The removal case drains all three dust cells immediately, retains
  the endpoint lamp for observations 0-2, dispatches its +4 callback on
  observation 3, and drains all 10,625 sampled light cells exactly.
- `c/magma/trace/out/matrix_redstone_wire_3d_focused_1/summary.md` reruns all
  nine flat and vertical wire cases together, protecting the prior 15/16
  cutoff, T-branch, and closed-loop behavior.
- The aggregate at
  `c/magma/trace/out/matrix_redstone_wire_3d_full_1/summary.md` has all 59
  cases passing. GPU-1 medians remain above floor at 4,073 scalar steps/s,
  2.91M Blaze env-ticks/s, and 25.80 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_wire_3d.json`.

## 2026-07-30 (wire and torch strong-power producers)

- Promoted directional strong output from powered dust and lit redstone
  torches through stone. Wire weak output is reused only for ordinary consumer
  queries; dust attenuation calls the same solid query with wire output
  disabled, matching vanilla's temporary `canProvidePower=false` guard and
  preventing self-power through the support.
- The unfixed probes at
  `c/magma/trace/out/redstone_wire_strong_power_probe_1` and
  `c/magma/trace/out/redstone_torch_strong_power_probe_1` each isolated one
  causal block mismatch: Java placed lit lamp 124 while magma retained unlit
  lamp 123. Their 2,201 and 2,444 light mismatches were downstream of that
  sole block state.
- Dust metadata changes now issue the bounded second notification ring around
  adjacent stone. Removing the source from a wire above stone drains metadata
  15 to 0 immediately, creates the indirectly powered lamp's exact +4
  callback, and turns the lamp off after three retained observations. No
  loaded-world scan or hot-loop allocation was added.
- The focused matrix at
  `c/magma/trace/out/matrix_redstone_strong_power_producers_focused_1/summary.md`
  proves wire power-on, wire power loss, and torch power-on. Raw blocks and all
  10,625 block-light cells are exact in every case.
- The first aggregate exposed two independent harness contaminants. The
  overlap-light behavior gate hard-coded a 1,304-cell total even though setup
  edge light can vary; it now requires the causal 11-to-15 and 12-to-14 cells,
  at least 1,000 non-vacuous changes, and exact full-cuboid Java/C equality.
  The fire-contact fixture could also receive a late movement packet after the
  server parked at START, producing two `Entity.move` contacts in one world
  tick. It now resets the client cursor and drains two packet-silent ticks
  before seeding counters. Three consecutive focused reruns were green.
- The corrected aggregate at
  `c/magma/trace/out/matrix_redstone_strong_power_producers_full_2/summary.md`
  has all 62 cases passing. GPU-1 medians remain above floor at 4,070 scalar
  steps/s, 2.91M Blaze env-ticks/s, and 25.55 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_strong_power_producers.json`.

## 2026-07-30 (torch strong-power notification lifecycle)

- Added the missing second notification ring for lit-redstone-torch
  add/remove and scheduled 75/76 transitions. The traversal is a fixed
  six outer directions by six neighbor directions and runs only on an edit or
  a due torch callback.
- The unfixed direct-removal probe at
  `c/magma/trace/out/redstone_torch_strong_power_off_probe_1` isolated one
  causal mismatch: Java scheduled the indirectly powered lamp at +4 and
  magma left it lit with an empty queue. After the fix,
  `redstone_torch_strong_power_off_exact_1` matches the complete queue,
  torch removal, lamp 124-to-123 transition, and all 10,625 light cells.
- A stricter seven-tick proof drives the torch off through its own scheduled
  +2 callback. The first run showed that the queue and torch history already
  matched Java, while magma's conservative saved-lamp proof rejected the
  adjacent now-unlit torch. Treating block 75 as an inert power neighbor lets
  the independently ordered +4 lamp callback dispatch normally.
  `matrix_redstone_torch_strong_power_scheduled_off_focused_2` matches the
  support, torch, and lamp transitions plus every sampled light cell.
- Added a read-only `blockstate_props` bridge command and
  `trace/capture_blockstate_props.py`. The captured
  `trace/blockstate_props_1_11_2.json` accounts for all 256 legacy IDs and
  all 4,096 raw metadata slots, preserving 151 invalid decodes as `-1`.
  `game/block_normal_cube_1_11_2.h` is generated from that artifact and
  embeds its SHA-256 provenance.
- The first 64-case aggregate rejected the old three-air-layer platform:
  natural leaves at `(13,81-82,10)` survived inside the scheduled fire
  callback's proof neighborhood. Java and C platform staging now clear six
  layers, the minimum that includes every neighbor of fire's `y-1..y+4`
  search. Three consecutive focused scheduled-fire runs passed.
- The replacement aggregate at
  `c/magma/trace/out/matrix_redstone_torch_notification_handoff_full_2/summary.md`
  has all 64 cases passing. GPU-1 guards remain above floor at 4,091 scalar
  steps/s, 2.91M Blaze env-ticks/s, and 25.21 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_torch_notification_handoff.json`.

## 2026-07-30 (registry-backed normal-cube redstone)

- Replaced the stone-ID special case in indirect strong power and bounded dust
  topology with `isNormalCube` masks captured from the initialized Java
  1.11.2 registry. The generated header also carries `canProvidePower` masks;
  both tables are tied to `trace/blockstate_props_1_11_2.json` by SHA-256.
- The unfixed plank probe at
  `c/magma/trace/out/redstone_lever_strong_power_planks_probe_1` had one
  causal mismatch: Java placed lamp 124 through the oak-plank support while
  magma placed 123. After the registry substitution, both the plank case and
  the original stone case are exact; glass is a unit-tested non-normal
  negative.
- The off-edge probe showed exact +4 queue state but a final lit lamp in
  magma. The callback proof still admitted stone by ID and rejected planks.
  It now allows arbitrary captured non-producers, evaluates represented
  wire/control/torch/redstone-block providers, and rejects unknown providers.
  Direct removal and callback dispatch through planks are exact.
- The capsule uses the same provenance-locked predicates. A saved powered
  callback through planks now restores absolute due time, priority, and order,
  then drains as the same powered no-op. Its self-test admits the plank/lever
  context and rejects an unimplemented repeater provider.
- A Java negative corrected an inferred rule: a redstone block does not
  strongly power through a neighboring plank. The dedicated negative case
  now requires an unlit 123 placement on both engines. A separate dust case
  proves the 15/14/13 one-block climb over plank supports.
- The focused 11-case registry suite and the expanded aggregate are green.
  `c/magma/trace/out/matrix_redstone_normal_cube_registry_full_1/summary.md`
  has all 69 cases passing. GPU-1 guards remain above floor at 4,045 scalar
  steps/s, 2.91M Blaze env-ticks/s, and 25.19 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_normal_cube_registry.json`.

## 2026-07-30 (wall-button release orientation)

- Added a saved pressed stone button on the east face of a normal-cube
  support. The unfixed proof at
  `c/magma/trace/out/redstone_button_wall_release_probe_1` showed Java
  retaining the button queue for observations 0-1, releasing 77:9 to 77:1 on
  observation 2/+3, retaining the lamp queue for observations 2-5, and
  turning 124 to 123 on observation 6. Magma imported no callback because its
  proof was floor-only.
- Button callback validation now resolves the support direction from the
  orientation bits, requires a registry-normal support and two bounded safe
  notification rings, preserves `meta & 7` on release, and notifies both the
  button and its attached support.
- `c/magma/trace/out/matrix_redstone_button_orientations_focused_1/summary.md`
  proves the original floor pulse and the wall pulse together. Both exact
  queues, raw button/lamp transitions, and all 10,625 block-light cells match.
- The expanded aggregate at
  `c/magma/trace/out/matrix_redstone_button_orientation_full_1/summary.md`
  has all 70 cases passing. GPU-1 guards remain above floor at 4,071 scalar
  steps/s, 2.91M Blaze env-ticks/s, and 25.41 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_button_orientation.json`.

## 2026-07-30 (saved stone-pressure-plate release)

- Added a saved powered stone pressure plate beside a lit lamp. The unfixed
  proof at
  `c/magma/trace/out/redstone_stone_pressure_plate_release_probe_1` showed
  Java retaining the block-70 callback for observations 0-1, releasing
  metadata 1 to 0 on observation 2/+3, retaining the lamp callback through
  observation 5, and turning block 124 back to 123 on observation 6. Magma
  imported no plate callback and retained both powered states.
- Block 70 now participates in the represented weak/strong-power and provider
  queries. Its due callback evaluates Java's inset, quarter-block-high
  `EntityLivingBase` trigger AABB against the authoritative server player,
  represented living active set, and End dragon. An unoccupied transition
  notifies both the plate and its normal-cube support; an occupied callback
  remains powered and schedules the next check at +20. The entity scan is
  cold-path work only when a plate callback is due.
- Capsule v1 promotes the bounded unoccupied +3 callback only when the plate
  is metadata 1 on a registry-normal support, both notification rings contain
  inert or represented blocks, and no unrepresented living entity begins
  within the conservative exclusion prism. It preserves due time, priority,
  and order rather than inferring a release from the final block state.
- The focused proof at
  `c/magma/trace/out/matrix_redstone_stone_pressure_plate_release_focused_1/summary.md`
  matches all seven queue observations, the exact plate/lamp mutation pair,
  and every sampled block/light cell.
- The expanded aggregate at
  `c/magma/trace/out/matrix_redstone_stone_pressure_plate_release_full_1/summary.md`
  has all 71 cases passing. GPU-1 guards remain above floor at 4,087 scalar
  steps/s, 2.91M Blaze env-ticks/s, and 25.08 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_stone_pressure_plate.json`.

## 2026-07-30 (live stone-pressure-plate lifecycle)

- Added two non-saved Java fixtures: a forward walk across an unpowered stone
  pressure plate and a player parked inside its trigger AABB through the first
  scheduled callback. The first unfixed proof at
  `c/magma/trace/out/redstone_stone_pressure_plate_walkover_probe_1` found the
  earliest causal divergence before redstone: magma treated block 70 as a
  full collision cube and stopped at z=8.3, while Java walked through it.
- Java 1.11.2 primary source confirms pressure plates return `NULL_AABB` for
  collision, use a 1/8-inset and 1/4-block-high entity trigger, and have zero
  light opacity. Blocks 70 and 72 now carry those geometry/material properties
  in the shared table and its Java golden. After that change the complete
  movement trace matched and only the missing redstone activation remained.
- Server-player block collisions now visit only the few cells touched by the
  contracted player box. An intersected metadata-0 stone plate evaluates the
  exact living-entity trigger, changes to metadata 1, notifies itself and the
  support below, and schedules its 20-tick callback. Packet movement uses the
  direct `current+20` due time. Java advances `totalWorldTime` before its
  ordinary entity pass while magma advances its mirrored clock later, so a
  stationary overlap found in that pass uses the equivalent absolute
  `current+21` time.
- The first occupied probe also demonstrated that the callback proof was
  correctly conservative: natural leaves one block below the support survived
  outside the staged platform and were an unrepresented notification
  consumer. The fixture now places explicit stone there. With an uncontaminated
  proof region, Java and magma retain the first callback through observations
  0-19, dispatch on observation 20, keep the plate and lamp powered, and
  schedule the replacement callback exactly 20 ticks later.
- `BLOCK_LIGHT_COMPARE` can now request a raw post-tick block-light comparison
  independently of a tick-0 edit. All three pressure-plate lifecycle fixtures
  therefore gate every one of the 10,625 sampled light cells as well as queue
  state and packed blocks. C unit tests separately protect packet-phase +20,
  ordinary-pass absolute +21, unoccupied release, and occupied rescheduling.
- The focused lifecycle matrix at
  `c/magma/trace/out/matrix_redstone_stone_pressure_plate_lifecycle_focused_1/summary.md`
  has all three cases passing. The aggregate at
  `c/magma/trace/out/matrix_redstone_stone_pressure_plate_lifecycle_full_1/summary.md`
  has all 73 cases passing. GPU-1 performance remains above every floor at
  4,036 scalar steps/s, 2.91M Blaze env-ticks/s, and 25.56 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_stone_pressure_plate_lifecycle.json`.

## 2026-07-30 (all stone-button support orientations)

- Added the four missing saved-callback fixtures for a pressed stone button
  attached DOWN, WEST, SOUTH, and NORTH of its normal-cube support. Together
  with the existing floor/UP and east-wall cases, the gate now covers powered
  metadata 8..13 and all six support directions.
- The generic runtime and capsule paths were already correct, so no simulation
  code changed. Every new fixture retained its block-77 callback through
  observations 0-1, released at observation 2/+3 while preserving metadata
  0/2/3/4, notified the correct support, retained the outward lamp's
  independent callback through observation 5, and turned block 124 to 123 on
  observation 6. This closes an evidence gap rather than hiding it as an
  inferred implementation claim.
- The behavior checker is now data-driven by button/lamp coordinates,
  powered/released metadata, due delay, and trace length. The complete
  six-case focused gate at
  `c/magma/trace/out/matrix_redstone_button_all_orientations_focused_1/summary.md`
  matches every queue observation, packed mutation, and all 10,625 sampled
  block-light cells.
- The expanded aggregate at
  `c/magma/trace/out/matrix_redstone_button_all_orientations_full_1/summary.md`
  has all 77 cases passing. The test-only expansion leaves performance above
  every floor at 3,986 scalar steps/s, 2.91M Blaze env-ticks/s, and 24.80 CUDA
  fps: `c/magma/trace/out/perf_guard_redstone_button_all_orientations.json`.

## 2026-07-30 (living-mob stone-pressure-plate activation)

- Added a deterministic stationary pig fixture for ordinary living-entity
  block collisions. The first NoAI version was correctly vacuous on both
  engines: vanilla `EntityLiving.isServerWorld()` is false when AI is disabled,
  so `move` and `doBlockCollisions` never run. The replacement keeps AI
  enabled but clears both task sets, disables gravity, and sets movement speed
  to zero. It remains stationary without directly calling the plate while
  vanilla still executes its normal collision path.
- The non-vacuous unfixed proof at
  `c/magma/trace/out/redstone_stone_pressure_plate_mob_occupied_probe_2`
  isolated the first divergence at observation 0. Java changed plate 70:0 to
  70:1, lamp 123 to 124, and created the block-70 callback due at absolute
  time 116; magma changed neither block and had an empty queue. Player,
  inventory, time, and the pig's exact pose/motion/health all matched.
- `GmMobLive` now exposes bounded exact living collision boxes. Controlled
  fixtures retain whether their Java counterpart executes ordinary block
  collisions, so a true NoAI pig remains excluded while the taskless mover is
  included. After the mob pass, runtime visits the contracted cells of at most
  95 represented movers, activates an intersected metadata-0 stone plate,
  notifies the plate/support, and schedules `total_time+20`. The existing due
  callback uses the full living set for MOBS sensitivity and reschedules at
  +20 while occupied.
- Native tests cover exact pig AABBs, bounded enumeration, the NoAI negative,
  observation-0 plate/lamp activation at Java's absolute +21 boundary, and
  occupied recurrence at +41. The four-case focused lifecycle gate at
  `c/magma/trace/out/matrix_redstone_stone_pressure_plate_mob_focused_1/summary.md`
  passes saved release, player walkover, stationary player, and stationary
  mob cases with exact queues, blocks, entities, and all 10,625 block-light
  cells.
- The first aggregate exposed an unrelated latent fixture dependency: natural
  leaves at y=76 sat below the old floor-button support, so capsule validation
  correctly rejected its saved callback. The fixture now pins that proof
  neighbor to stone; its isolated rerun passes. The clean promotable aggregate
  at
  `c/magma/trace/out/matrix_redstone_stone_pressure_plate_mob_full_2/summary.md`
  has all 78 cases passing.
- GPU-1 performance remains above every floor after the bounded per-mob pass:
  4,058 scalar steps/s, 2.90M Blaze env-ticks/s, and 25.01 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_stone_pressure_plate_mob.json`.

## 2026-07-30 (wooden pressure-plate dropped-item sensitivity)

- Added a deterministic real `EntityItem` oracle fixture. The parked Java
  bridge spawns a gravity-free zero-motion item with pickup delay 32767 and
  returns its authoritative entity ID. The sidecar carries exact EID, stack,
  pose/motion, age, and pickup delay into magma; entity diffing now gates item
  ID and count in addition to the existing metadata and kinematic fields.
- The non-vacuous unfixed proof at
  `c/magma/trace/out/redstone_wooden_pressure_plate_item_probe_1` isolated one
  causal divergence at observation 0. Java changed wooden plate 72:0 to 72:1,
  lamp 123 to 124, and created a block-72 callback due at absolute base+21;
  magma changed no blocks and had an empty queue. The item itself matched for
  all 22 observations. Java retained that callback through observation 19 and
  replaced it at observation 20 with the next callback at base+41.
- `GmLiveSim` now exposes exact 0.25-wide, 0.25-high EntityItem AABBs. Wooden
  plates use the represented EVERYTHING query (player, living entities,
  dragon, and active items), while stone deliberately excludes items. After
  item movement/aging, runtime visits only each item's contracted collision
  cells, activates an intersected metadata-0 wooden plate, notifies the
  plate/support, and schedules `total_time+20`. The fixed 48-slot pool is not
  scanned when no item is active.
- Native tests cover the exact item AABB, preserved authoritative EID/stack/
  age/infinite pickup delay, wooden activation at base+21, recurrence at
  base+41, and the defining negative: an identical item over stone leaves
  block 70 and its lamp unpowered. The script parser has a separate exact
  `spawn_item_fixture` route test.
- The focused differential at
  `c/magma/trace/out/redstone_wooden_pressure_plate_item_focused_1/summary.md`
  is exact. The five-case pressure family at
  `c/magma/trace/out/matrix_redstone_pressure_plate_item_focused_1/summary.md`
  also preserves saved stone release, player walkover, stationary player, and
  collision-enabled pig behavior.
- The promotable aggregate at
  `c/magma/trace/out/matrix_redstone_wooden_pressure_plate_item_full_1/summary.md`
  has all 79 cases passing: 79 state gates, 76 required behavior gates plus
  three not-required rows, and 79 raw block gates. GPU-1 performance remains
  above every floor at 4,046 scalar steps/s, 2.90M Blaze env-ticks/s, and
  24.91 CUDA fps:
  `c/magma/trace/out/perf_guard_redstone_wooden_pressure_plate_item.json`.
- The next ordered R-02 slice is gold/iron weighted pressure plates: exact
  entity-count-to-analog-strength mapping, dust output, +10 recurrence, and
  release. Wooden-button arrow sensitivity follows before R-03 repeaters.

## 2026-07-30 (weighted pressure-plate analog strength and release)

- The first Java probe exposed a fixture contaminant before runtime work:
  gold/iron plates 147/148 still used the cut-ID full-cube fallback, so the
  player and item could not occupy vanilla's plate geometry. The block-property
  table now records the exact half-height collision/selection shape and
  zero-opacity/light values. Its 644-line Java registry golden is exact; the
  same audit repaired omitted IDs 139/175 and corrected the golden opacity of
  lava and fences to the bundled 1.11.2 source truth.
- A clean unfixed probe then isolated the real rule. With player plus one
  dropped-item entity, Java set gold plate 147 and adjacent dust to strength 2
  and iron plate 148/dust to strength 1, powered both lamps, and retained a
  callback with a ten-tick cadence. Magma initially left both circuits at
  zero. The implementation now uses
  `ceil(min(entity_count,max_weight)/max_weight*15)` with max weights 15 and
  150, exact analog weak/upward-strong output, and +10 recurrence.
- Occupancy is counted over represented player, living-mob, item, and dragon
  sets. It remains bounded to fixed pools and skips the 48-slot item scan when
  no item is active. Native positives cover gold/iron strength and callback
  timing; a defining negative proves one EntityItem stack of count 64 produces
  strength 1 rather than being mistaken for 64 entities.
- The saved-release fixture begins with gold plate/dust strength 2 and a
  callback due at base+3. Both engines release plate and dust to zero on the
  same observation, retain the lamp's independent +4 callback, then turn
  124-to-123 with a fully drained light field. Capsule promotion is limited to
  a supported proof neighborhood and rejects a nearby unrepresented
  all-entity trigger; the positive and negative capsule self-tests pass.
- The three-case weighted gate at
  `c/magma/trace/out/matrix_redstone_weighted_pressure_plate_release_focused_2/summary.md`
  and eight-case pressure family at
  `c/magma/trace/out/matrix_redstone_pressure_plate_family_2/summary.md` pass
  every state, behavior, queue, raw-block, and raw-light check. The promotable
  aggregate at
  `c/magma/trace/out/matrix_redstone_weighted_pressure_plate_release_full_1/summary.md`
  is 82/82 overall/state/block, with 79 required behavior passes and three
  not-required rows.
- During the first aggregate, the Speed II fixture exposed a Java-client
  property-packet race after server-side expiry, not a magma divergence.
  The oracle now removes all potion-owned attribute UUIDs and rebuilds the
  authoritative effect snapshot at client-tick START after queued packet tasks
  drain. Its isolated expiry proof and the clean 82-case aggregate both pass.
- GPU-1 performance remains above every floor after the complete weighted
  runtime path: 3,976 scalar steps/s, 2.90M Blaze env-ticks/s, and 24.72 CUDA
  fps in
  `c/magma/trace/out/perf_guard_redstone_weighted_pressure_plate.json`.
  The next ordered R-02 slice is wooden-button arrow sensitivity and retained
  pulse behavior.

## 2026-07-30 (wooden-button arrow sensitivity and retained pulse)

- Bundled Minecraft 1.11.2 primary source establishes the defining difference
  from stone: `BlockButtonWood` queries `EntityArrow` in its orientation-
  specific selection AABB, activates only while unpowered, and rechecks every
  30 ticks while an arrow remains. The six DOWN/UP/NORTH/SOUTH/WEST/EAST
  shapes contract from 1/8 depth to 1/16 while pressed. Weak output is 15 on
  every face and strong output is 15 only toward the attached support.
- The qrl oracle now has a locked `EntityTippedArrow` sidecar with an
  authoritative EID, exact pose, zero motion, and no gravity. The unfixed
  differential at
  `c/magma/trace/out/redstone_wooden_button_arrow_probe_1/summary.md`
  was clean: Java changed button 143:5 to 143:13, lamp 123 to 124, retained
  the first callback through observation 29, and replaced it at observation
  30 with another +30 callback; magma initially made no changes. The arrow
  itself was exact on all 32 observations.
- Magma now uses the exact six selection AABBs, arrow-only collision path,
  direct/directional power, support notification, and +30 callback recurrence.
  Occupancy scans the existing fixed 32-projectile pool only on arrow/button
  collision or a due button callback. A native lifecycle test covers
  activation, recurrence, arrow removal, release, and the lamp's independent
  +4 drain. The defining negative proves the same arrow never activates stone.
  The strict script route separately validates the arrow sidecar parser and
  canonical entity output.
- The first fixed run exposed a second, independent property gap: button and
  queue state were exact, but the cut-ID fallback treated wooden button 143 as
  opaque and blocked 14 light cells. Its exact registry row is now hardness
  0.5, opacity 0, emission 0, and the vanilla random-tick flag. The expanded
  648-line block-property golden matches Java exactly. The focused occupied
  case at
  `c/magma/trace/out/matrix_redstone_wooden_button_arrow_focused_2/summary.md`
  now passes state, semantic behavior, queue, raw blocks, and all 10,625
  block-light cells.
- Save/reload coverage begins with powered 143:13, a lit east lamp, no arrow,
  and an imminent callback due at +3. Both engines release to 143:5, preserve
  the lamp until its separate +4 callback, then turn 124 to 123 with exact
  light. Capsule promotion admits only that bounded imminent arrow-free proof
  and rejects captured `EntityArrow`, `EntitySpectralArrow`, or
  `EntityTippedArrow` state. The focused release gate is
  `c/magma/trace/out/matrix_redstone_wooden_button_release_focused_1/summary.md`.
- The complete eight-case button family at
  `c/magma/trace/out/matrix_redstone_button_family_1/summary.md` passes all
  six stone orientations plus wooden occupied/release. The aggregate at
  `c/magma/trace/out/matrix_redstone_wooden_button_full_1/summary.md` is
  84/84 overall, state, and raw-block gates, with all 81 required behavior
  gates passing and three deliberately not-required rows.
- GPU-1 performance remains above every frozen floor: 3,962 scalar steps/s,
  2.90M Blaze env-ticks/s, and 24.54 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_wooden_button_arrow.json`. The
  implementation adds no world scan, heap allocation, or inactive-button
  per-tick work. The next ordered redstone slice is R-03 repeaters.

## 2026-07-30 (repeater timing, locking, priority, and saved callbacks)

- Bundled Minecraft Java 1.11.2 source fixed the contract before
  implementation: IDs 93/94 retain horizontal metadata in the low two bits,
  delay 1..4 in the high two bits, schedule at +2/+4/+6/+8, use priority -1
  on a rising edge and -2 on a falling edge, and select -3 when another diode
  occupies the output topology. A perpendicular powered repeater locks the
  transition. An unpowered due callback always turns on; if its input has
  already vanished, it queues powered ID 94 for the same delay to enforce the
  minimum pulse.
- The clean unfixed power-on probe at
  `c/magma/trace/out/redstone_repeater_delay_1_probe_1/summary.md` isolated
  the first divergence at the queue boundary. Java scheduled block 93 at
  absolute time 98 with priority -1 on observation 0, dispatched on
  observation 1, changed 93:1 to 94:1, and powered the lamp. Magma initially
  matched only the tick-0 source placement.
- Runtime now implements metadata-derived facing/delay, directional weak and
  strong output, exact input queries, support validation, lock detection,
  pending-update deduplication, priorities -1/-2/-3, minimum-pulse reschedule,
  output notification, and lamp handoff. Work is reached only by block edits
  or scheduled callbacks and uses the existing fixed-capacity queue; there is
  no repeater scan or hot-loop allocation.
- Narrow Java-vs-magma fixtures cover all four delays and all four horizontal
  facings, power-off, a one-observation input, strong power through ordinary
  stone, static locking, unlock notification, and a two-repeater chain. Every
  semantic gate compares the exact queue at every observation as well as the
  exact packed source/repeater/lamp mutations and all 10,625 sampled
  block-light cells. The focused family at
  `c/magma/trace/out/matrix_redstone_repeater_family_1/summary.md` is 16/16.
- Save/reload initially failed independently of live behavior. The clean
  evidence at
  `c/magma/trace/out/redstone_repeater_saved_callbacks_probe_1/summary.md`
  showed valid Java block-93/94 callbacks in the pre-tick state while the
  capsule emitted an empty magma queue, producing two shared-cell
  divergences in each case. Capsule v1 now promotes repeaters only with
  registry-backed support, air above, represented input/output states, and
  air/repeater side inputs. Positive self-tests cover saved 93 and 94; an
  unknown comparator side neighbor remains rejected. Saved power-on,
  power-off, and minimum-pulse lifecycles pass in
  `c/magma/trace/out/matrix_redstone_repeater_saved_callbacks_family_2/summary.md`.
- Native runtime tests independently cover delay-1 and delay-4 boundaries,
  falling-edge priority and lamp handoff, lock/unlock, minimum pulse, chain
  priority -3, and direct saved-callback dispatch. The capsule round-trip and
  negative suite also passes.
- The promotable aggregate at
  `c/magma/trace/out/matrix_redstone_repeater_full_1/summary.md` has 100/100
  overall, state, and raw-block gates, with 97 required semantic behavior
  passes and three deliberately not-required legacy rows. Runtime was
  416.405 seconds across four independent Java oracles.
- GPU-1 performance remains above every frozen floor: 3,926 scalar steps/s,
  2.90M Blaze env-ticks/s, and 24.67 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_repeater.json`. R-03 remains active
  for comparators and observers; repeaters are complete within the promoted
  bounded proof contract.

## 2026-07-30 (comparator analog modes, tile state, and saved callbacks)

- Bundled Minecraft Java 1.11.2 source established the comparator contract
  before implementation: IDs 149/150 use horizontal facing plus subtract-mode
  metadata, persist a separate `TileEntityComparator.outputSignal`, schedule
  after two ticks, and use priority -1 only when another diode occupies the
  output topology. Compare and subtract modes intentionally differ in both
  powered-state and unchanged-output notification behavior.
- The clean unfixed power-on evidence at
  `c/magma/trace/out/redstone_comparator_compare_power_on_probe_1/summary.md`
  isolated the first missing queue entry. Runtime now implements all four
  facings, directional rear power, raw side-dust strength, compare/subtract
  output, directional weak/strong analog power, exact notifications, and
  scheduled IDs 149/150. The tile payload uses a fixed 64-entry array touched
  only by load/edit/query/callback paths, with no inactive scan or hot-loop
  allocation.
- Oracle state now captures the complete sorted nearby
  `TileEntityComparator` list and its output signal. The state schema therefore
  has 20 features: 19 must match and only the existing player `death_time`
  remains explicitly unsupported. The Java registry golden also promotes
  block IDs 149/150 and passes all 664 property rows.
- Narrow gates cover rear strength 7, every orientation, compare side
  strengths 5/7/8, subtract side strengths 5/7/8, and a comparator-to-repeater
  chain. They retain two counterintuitive source-truth edges: compare rear
  7/side 8 stores tile output 7 while the block remains unpowered, and subtract
  rear 7/side 7 accepts its callback while output remains zero. The focused
  analog family is 8/8 at
  `c/magma/trace/out/redstone_comparator_analog_family_1/summary.md`.
- Capsule validation now requires a complete, bounded comparator tile list,
  restores output signals exactly, and promotes scheduled comparator work only
  inside a represented support/input/side/output proof region. Saved power-on
  restores output 0; saved power-off restores output 15. Both callbacks,
  block states, tile state, lamp handoff, raw blocks, and light are exact.
  Capsule and script self-tests include positive and malformed-state coverage.
- The promotable aggregate at
  `c/magma/trace/out/matrix_redstone_comparator_full_1/summary.md` passes all
  114 overall, state, and behavior gates, with 111 required raw-block passes
  and three deliberately not-required rows. Runtime was 480.109 seconds over
  four independent Java oracle instances.
- GPU-1 performance remains above every frozen floor: 4,044 scalar steps/s,
  2.91M Blaze env-ticks/s, and 24.88 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_comparator.json`. The next ordered
  R-03 slice is comparator input-override producers (static analog sources,
  then containers/item frames), followed by observer pulses.

## 2026-07-30 (static comparator overrides and one-solid look-through)

- Three primary-source block overrides are now exact: cake emits
  `(7-bites)*2`, cauldron emits its level 0..3, and an End portal frame emits
  15 only when its eye bit is set. Clean unfixed evidence for all three and
  the one-solid look-through rule is retained at
  `c/magma/trace/out/redstone_comparator_static_overrides_probe_1/summary.md`.
- Vanilla first checks the adjacent block override, then—when ordinary input
  is below 15 and that block is a normal cube—checks exactly one block farther.
  Override edits also perform the four-direction
  `World.updateComparatorOutputLevel` scan for a comparator directly adjacent
  or one normal cube away. Magma implements both paths only on edits/callbacks;
  no loaded-world or inactive per-tick scan was added.
- The first fixed cauldron and cake runs exposed independent strict-light
  regressions after all queue, tile, and raw-block state already matched.
  Their old cut-ID property rows marked both non-opaque blocks as opacity 255.
  The table and Java golden now use opacity zero; all 664 registry-golden lines
  pass and both 10,625-cell light fields are exact. The End frame correctly
  retains its existing opacity.
- A saved mid-transition fixture places a level-3 cauldron behind one stone
  block, restores comparator tile output zero and a callback due at +3, then
  commits output 3 and powers the lamp exactly. Capsule proof now admits valid
  cake/cauldron/End-frame states directly or as the represented second block,
  while rejecting the still-unrepresented item-frame ambiguity.
- The 19-case comparator regression family at
  `c/magma/trace/out/matrix_redstone_comparator_static_family_1/summary.md`
  passes every state, behavior, queue, raw-block, and raw-light gate. The full
  aggregate at
  `c/magma/trace/out/matrix_redstone_comparator_static_full_1/summary.md`
  passes 119/119 overall and state gates, 116 required behavior gates plus
  three not-required rows, and 119/119 raw-block gates in 493.444 seconds.
- Two CUDA guard attempts made during unrelated host-wide CPU contention were
  retained as failures rather than waived. After stopping only the disposable
  oracle clients and letting the contention clear, the full guard passed at
  4,315 scalar steps/s, 2.92M Blaze env-ticks/s, and 27.14 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_comparator_static_overrides_pass.json`.
  Inventory-backed comparator producers and item frames are next.

## 2026-07-30 (single-chest comparator state and saved dispatch)

- Bundled Minecraft Java 1.11.2 source established the inventory formula:
  sum each occupied stack's fraction of its effective slot limit, divide by
  inventory size, then emit `floor(fullness*14)+1`. Native controls cover an
  empty chest (0), one full stack in 27 slots (1), all slots full (15), and a
  single non-stackable item (1).
- The Java oracle now captures a complete sorted list of nearby ordinary
  single-chest inventories, including all 27 sparse slots, and provides a
  locked pre-tick slot setter. Capsule validation rejects incomplete,
  malformed, adjacent/double, trapped, or otherwise unsupported chests and
  restores accepted slots before comparator tile output and scheduled work.
  The trace schema now has 21 features: 20 must match and only player
  `death_time` remains explicitly unsupported.
- Clean unfixed evidence at
  `c/magma/trace/out/redstone_comparator_saved_single_chest_probe_1/summary.md`
  preserved exact container and queue state but isolated the causal behavior:
  Java committed comparator output 1 while Magma left output 0, producing the
  expected comparator/lamp block divergence.
- The behavioral fix first passed all state and raw-block checks but exposed a
  strict 13-cell light mismatch at
  `c/magma/trace/out/redstone_comparator_saved_single_chest_fix_1/summary.md`.
  Java block construction derives opacity from `BlockChest.isOpaqueCube()`,
  which is false. Block 54's opacity is now zero, the 664-row Java/CPU registry
  golden passes, and the focused final result is
  `c/magma/trace/out/redstone_comparator_saved_single_chest_fix_2/summary.md`.
- The 20-case comparator family at
  `c/magma/trace/out/matrix_redstone_comparator_chest_family_1/summary.md`
  passes every state, behavior, queue, raw-block, and raw-light gate. The full
  aggregate at
  `c/magma/trace/out/matrix_redstone_comparator_single_chest_full_1/summary.md`
  passes 120/120 overall and state gates, 117 required behavior gates plus
  three not-required rows, and 120/120 raw-block gates in 496.524 seconds.
- The full performance guard passes at 4,236 scalar steps/s, 2.92M Blaze
  env-ticks/s, and 27.27 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_comparator_single_chest.json`.
  Chest state uses the existing fixed tile pool and fullness is queried only
  by a comparator callback or explicit edit; no loaded-world scan,
  per-tick allocation, or inactive-chest pass was added. Double/trapped
  chests, other inventory producers, item frames, and observers remain.

## 2026-07-30 (furnace comparator state and saved dispatch)

- `BlockFurnace` is both a normal cube and an inventory comparator override.
  Its three slots use the same vanilla per-item stack-limit formula: empty is
  0, one full slot is 5, all three full slots are 15, and one non-stackable
  item in one slot is 5. Native controls lock all four boundaries.
- The authoritative container trace now includes nearby furnace IDs 61/62,
  all three sparse slots, burn time, current burn time, cook time, and total
  cook time. The locked container setter accepts the furnace's real slot
  bound. Capsule validation requires the complete timer schema and a matching
  represented furnace block, limits restore to the existing 16-tile runtime
  pool, and emits furnace state before comparator output and scheduled work.
- The deliberate omission control at
  `c/magma/trace/out/redstone_comparator_saved_furnace_probe_1/summary.md`
  matches the exact furnace inventory/timers and pending queue, then first
  diverges on comparator tile output at observation 1. Java changes only
  comparator 149:1-to-149:9 and lamp 123-to-124; Magma changes neither.
  The fixed result at
  `c/magma/trace/out/redstone_comparator_saved_furnace_fix_1/summary.md`
  commits output 5 and matches both cells and the full light field.
- The source also exposed a proof-gate edge: a furnace must not be treated as
  an ordinary look-through solid merely because it is a normal cube; its
  direct override takes precedence. The runtime proof now distinguishes a
  direct normal-cube override from a normal cube whose second block supplies
  the override.
- The 21-case comparator family at
  `c/magma/trace/out/matrix_redstone_comparator_furnace_family_1/summary.md`
  passes every state, behavior, queue, raw-block, and raw-light gate. The full
  aggregate at
  `c/magma/trace/out/matrix_redstone_comparator_furnace_full_1/summary.md`
  passes 121/121 overall and state gates, 118 required behavior gates plus
  three not-required rows, and 121/121 raw-block gates in 502.407 seconds.
- The GPU-1 performance guard passes at 4,231 scalar steps/s, 2.92M Blaze
  env-ticks/s, and 27.00 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_comparator_furnace.json`. Furnace
  state reuses the existing fixed 16-entry pool and active-only tick loop;
  comparator queries and notification scans remain callback/edit driven.
  Other inventory producers, item frames, and observers remain.

## 2026-07-30 (ordinary double-chest comparator state)

- `BlockChest.getLockableContainer` joins one horizontal ordinary neighbor as
  an `InventoryLargeChest`; `Container.calcRedstoneFromInventory` therefore
  divides fullness over 54 slots. The distinguishing fixture places four full
  stone stacks in one half, which emits 2 across the pair rather than the
  single-half value 3.
- Oracle capture now classifies both halves with reciprocal pair coordinates,
  while rejecting trapped/deferred-loot chests, triples, blocked containers,
  and unsupported inventory tiles. The reusable locked fill hook seeds slots
  0..3 through real tile setters. Capsule validation requires both 27-slot
  halves, exact reciprocal horizontal adjacency, no third chest, matching
  block IDs, and represented air above each half before restoring either
  inventory.
- The deliberate single-half control at
  `c/magma/trace/out/redstone_comparator_saved_double_chest_probe_1/summary.md`
  first diverges at observation 1: Java stores output 2 and Magma stores 3.
  Both analog values leave the comparator powered and the lamp lit, so the raw
  block gate remains exact while the tile-state and semantic gates fail. This
  demonstrates why analog tile state is a mandatory oracle feature.
- The corrected focused result at
  `c/magma/trace/out/redstone_comparator_saved_double_chest_fix_1/summary.md`
  matches both half inventories, the queue, output 2, raw blocks, and lighting.
  The 22-case comparator family at
  `c/magma/trace/out/matrix_redstone_comparator_double_chest_family_1/summary.md`
  is fully green. The aggregate at
  `c/magma/trace/out/matrix_redstone_comparator_double_chest_full_1/summary.md`
  passes 122/122 overall/state/raw-block gates, 119 required behavior gates,
  and three not-required rows in 502.648 seconds.
- The performance guard passes at 4,261 scalar steps/s, 2.92M Blaze
  env-ticks/s, and 26.79 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_comparator_double_chest.json`.
  Pair resolution performs four neighbor checks only when a comparator queries
  a chest and reuses the existing growable tile table; no per-tick scan or
  allocation was added. Trapped chests and other inventory producers remain.

## 2026-07-30 (closed trapped-chest comparator state and fixture-boundary hardening)

- Vanilla `BlockChest` uses the same inventory comparator calculation for
  ordinary and trapped chests, pairs only equal chest types, and gives block
  ID 146 opacity zero. Its separate trapped-chest redstone source is the live
  viewer count; the saved-state slice therefore admits only a closed single
  chest with `numPlayersUsing=0`, `lidAngle=0`, and `prevLidAngle=0`.
- Oracle container capture and the locked setter now preserve the distinct
  `single_trapped_chest` schema and reject wrong block IDs, adjacent trapped
  halves, blocked lids, or open/lid-transient state. The capsule enforces those
  same proof boundaries before emitting the existing slot setter. Runtime
  inventory lookup, mutation, break/replacement handling, comparator queries,
  script export, and block properties now recognize ID 146 while pairing only
  identical IDs and never applying ordinary stronghold loot to trapped chests.
- The deliberate omission at
  `c/magma/trace/out/redstone_comparator_saved_single_trapped_chest_probe_1/summary.md`
  matches the exact 27-slot container and callback queue, then first diverges
  at observation 1: Java stores comparator output 1 while Magma remains 0.
  The corrected run at
  `c/magma/trace/out/redstone_comparator_saved_single_trapped_chest_fix_1/summary.md`
  matches comparator/lamp blocks, all raw light, and the container state. The
  23-case family at
  `c/magma/trace/out/matrix_redstone_comparator_trapped_chest_family_1/summary.md`
  is fully green.
- The first 123-case aggregate,
  `c/magma/trace/out/matrix_redstone_comparator_trapped_chest_full_1/summary.md`,
  is intentionally retained as rejected setup evidence. Early static-water
  staging could turn fixture water from ID 9 to ID 8 and leave an in-flight
  callback outside the captured pending `TreeSet`, so Java alone later settled
  two cells during the five-tick wet-fire case. Repetition showed the boundary
  was nondeterministic rather than a new runtime fire divergence.
- Final-fixture alignment now restores exact player velocity, `onGround`, and
  `fallDistance` after locked block staging. The wet-fire case was narrowed to
  its uncontaminated two-tick causal claim: immediate extinguish, no damage,
  and exact air consumption. It passed independently on all four long-lived
  oracle instances; longer fluid evolution remains covered by the dedicated
  water-dispatch cases.
- The promoted full result at
  `c/magma/trace/out/matrix_redstone_comparator_trapped_chest_full_2/summary.md`
  passes 123/123 overall, state, and raw-block gates, 120 required behavior
  gates plus three not-required rows, in 521.861 seconds. Every row has 20
  matching features, zero divergences, exact raw block/light state, and only
  the explicit unsupported `death_time` subfield.
- GPU-1 performance remains above every floor at 4,313 scalar steps/s, 2.92M
  Blaze env-ticks/s, and 26.94 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_comparator_trapped_chest.json`.
  ID 146 reuses the bounded chest tile/query path and adds no world scan,
  allocation, or inactive per-tick work. Double trapped halves and live viewer
  power are the next trapped-chest slices.

## 2026-07-30 (closed double trapped-chest comparator state)

- Java source confirms trapped chests pair only with the same chest type and
  expose the pair through `InventoryLargeChest`, so comparator fullness is
  divided over all 54 slots just like an ordinary double chest. The
  distinguishing fixture places four full stone stacks in one half: the pair
  emits 2, while incorrectly reading only that half emits 3.
- Oracle capture and the locked container setter now admit one exact adjacent
  trapped half. Capture emits reciprocal `double_trapped_chest_half` schemas
  rather than conflating ID 146 with ordinary `double_chest_half` rows.
  Capsule validation requires two 27-slot entries, matching ID-146 blocks,
  represented air above both, reciprocal Manhattan-distance-one coordinates,
  and no third adjacent trapped chest. Runtime script export emits the same
  schema and both halves materialize before comparator callbacks.
- The deliberate single-half control at
  `c/magma/trace/out/redstone_comparator_saved_double_trapped_chest_probe_1/summary.md`
  first diverges at observation 1: Java stores output 2 and Magma stores 3.
  Both values power the comparator and lamp, so all 10,625 raw block and light
  cells remain exact while the analog tile-state and semantic gates fail.
  This is the intended non-vacuous proof of 54-slot composition.
- The corrected result at
  `c/magma/trace/out/redstone_comparator_saved_double_trapped_chest_fix_1/summary.md`
  matches both half inventories, output 2, the callback queue, blocks, and
  lighting. The 24-case comparator family at
  `c/magma/trace/out/matrix_redstone_comparator_double_trapped_chest_family_1/summary.md`
  is fully green.
- The complete aggregate at
  `c/magma/trace/out/matrix_redstone_comparator_double_trapped_chest_full_1/summary.md`
  passes 124/124 overall, state, and raw-block gates, 121 required behavior
  gates plus three not-required rows, in 513.008 seconds. Every row has 20
  matching state features, zero divergences, exact raw block/light outcomes,
  and only the explicit unsupported `death_time` subfield.
- GPU-1 performance remains above every floor at 4,255 scalar steps/s, 2.92M
  Blaze env-ticks/s, and 27.10 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_comparator_double_trapped_chest.json`.
  Pair lookup remains four neighbor checks on comparator query only; the slice
  adds no scan, allocation, or inactive per-tick work. Live viewer-count
  weak/upward-strong power is next.

## 2026-07-30 (live trapped-chest viewer-count redstone)

- Added an input-driven Java fixture for block ID 146. Use is pressed at tape
  tick 2 and consumed by the integrated server at observation 3; close is
  pressed at tick 7. The oracle close path retains the real client packet/GUI
  operation and adds an idempotent authoritative scheduled close before the
  next lockstep grant, removing the prior observation-7/8 loopback race.
  Successful block use swings the hand on both sides and therefore resets the
  attack cooldown at the same observation.
- Magma now carries block-container use to the next server tick with the
  original raycast coordinate. Re-raycasting after the delay could otherwise
  activate a block placed by the same input edge. Crafting tables, furnaces,
  ordinary/trapped chests, End frames, and beds use this packet shadow; generic
  placement is suppressed when the clicked block consumes use.
- Trapped chest weak power is its represented tile's
  `numPlayersUsing`, clamped to 0..15. Strong power exposes that same value only
  upward. Open/close is centralized, increments or decrements represented
  reciprocal halves, and notifies both the chest position and `pos.down()` for
  trapped chests. The lookup is reached only after a redstone query has found
  ID 146; open/close is input-driven, so no loaded-world scan or idle
  allocation was added.
- The clean deliberate omission at
  `c/magma/trace/out/redstone_trapped_chest_viewer_power_probe_6/summary.md`
  matches viewer count, exact lid floats, container contents, player swing,
  and final raw world state. It first diverges at close because Java retains
  two ordered block-124 callbacks—one direct weak consumer and one
  upward-strong-through-stone consumer—while Magma retains none.
- Native runtime tests cover the positive open/power/close/+4-off lifecycle
  and two negative controls: ordinary chest viewers produce neither weak nor
  strong power, and a trapped chest cannot strongly power horizontally through
  a normal cube. Two independent corrected oracle resets pass at
  `c/magma/trace/out/redstone_trapped_chest_viewer_power_fix_2/summary.md` and
  `c/magma/trace/out/redstone_trapped_chest_viewer_power_fix_3/summary.md`.
  The semantic gate asserts viewer `0→1→0`, lid `0→0.4→0`, successful-use
  cooldown, both ordered callbacks through observations 7-9, their drain
  before observation 10, and an exact return to the shared block baseline.
- The 25-case affected family at
  `c/magma/trace/out/matrix_redstone_trapped_chest_viewer_power_family_1/summary.md`
  passes 25/25. The complete aggregate at
  `c/magma/trace/out/matrix_redstone_trapped_chest_viewer_power_full_1/summary.md`
  passes 125/125 overall, state, and raw-block gates, 122 required behavior
  gates plus three not-required rows, in 517.526 seconds. Every case has
  20 matching state features, zero divergences, exact raw block/light outcomes,
  and only the explicit unsupported `death_time` subfield.
- GPU-1 performance remains above every floor at 4,174 scalar steps/s, 2.91M
  Blaze env-ticks/s, and 26.30 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_trapped_chest_viewer_power.json`.

## 2026-07-30 (saved dispenser/dropper comparator state)

- Enumerated every vanilla 1.11.2 comparator override before choosing the next
  uncontaminated R-03 source. Dispenser/dropper IDs 23/158 came first because
  their static nine-slot state establishes a reusable bounded inventory-tile
  pool without adding an active tick system.
- The Java bridge now captures exact non-loot `TileEntityDispenser` and
  `TileEntityDropper` inventories and can seed a real slot while the integrated
  server is parked. The neutral capsule validates the distinct schema/block
  pair, exact size 9, sparse item rows, and a 256-tile bound, then restores all
  slots before comparator output and scheduled work. Magma exports the same
  state through a cold dynamically grown pool that has no tick hook.
- The deliberate omission at
  `c/magma/trace/out/redstone_comparator_saved_dispenser_probe_1/summary.md`
  preserved the exact slot-0 stone stack and block-149 callback on both sides.
  At observation 1 Java committed output 2 and powered the lamp while Magma
  remained at output 0, isolating the missing fullness calculation from state
  transport, scheduling, and fixture setup.
- Magma now reproduces `Container.calcRedstoneFromInventory` with Java-float
  arithmetic over the represented item stack limit. Native tests cover one
  full dispenser slot -> 2, empty dispenser -> 0, out-of-range slot 9
  rejection, and one non-stackable dropper item -> 2. Independent corrected
  oracle fixtures pass for both ID 23 and inherited dropper ID 158 at
  `c/magma/trace/out/redstone_comparator_saved_dispenser_fix_1/summary.md` and
  `c/magma/trace/out/redstone_comparator_saved_dropper_fix_1/summary.md`.
- The 27-case affected family at
  `c/magma/trace/out/matrix_redstone_comparator_dispenser_dropper_family_1/summary.md`
  passes 27/27. The complete aggregate at
  `c/magma/trace/out/matrix_redstone_comparator_dispenser_dropper_full_1/summary.md`
  passes 127/127 overall, state, and raw-block gates, 124 required behavior
  gates plus three not-required rows, in 528.687 seconds. Every case has
  20 matching state features, zero divergences, exact raw block/light outcomes,
  and only the explicit unsupported `death_time` subfield.
- GPU-1 performance remains above every floor at 4,207 scalar steps/s, 2.91M
  Blaze env-ticks/s, and 26.12 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_comparator_dispenser_dropper.json`.
  Static-container memory allocates only when a tile is restored; comparator
  lookup is coordinate-bound and the fullness loop examines at most nine
  slots, so the inactive base profile gains no world scan or per-tick work.

## 2026-07-30 (saved jukebox comparator state)

- Vanilla `BlockJukebox` returns zero for an empty record tile and otherwise
  `Item.getIdFromItem(record) + 1 - RECORD_13`, mapping record IDs 2256..2267
  to comparator strengths 1..12. Playback and audio are independent of this
  static override and remain in A-01.
- Oracle capture now includes `TileEntityJukebox` in the complete bounded tile
  list. It admits only metadata-consistent empty state or one untagged vanilla
  record with count 1/meta 0. The parked setter updates the real tile,
  `HAS_RECORD` property, and comparator neighborhood. The neutral capsule
  enforces the same schema, block-ID, metadata, item-range, and 256-tile bound
  before emitting the cold static-tile restore.
- The deliberate omission at
  `c/magma/trace/out/redstone_comparator_saved_jukebox_probe_1/summary.md`
  retains the same record-13 item and block-149 callback on both engines, then
  first diverges at observation 1: Java stores output 1 and powers the lamp
  while Magma stays at zero.
- Magma now evaluates the record index from the exact one-slot tile. Native
  tests prove record 13 -> 1, record wait -> 12, empty -> 0, and non-record
  rejection. Independent oracle results pass at
  `c/magma/trace/out/redstone_comparator_saved_jukebox_fix_1/summary.md` and
  `c/magma/trace/out/redstone_comparator_saved_jukebox_record_wait_fix_1/summary.md`.
- The 29-case affected family at
  `c/magma/trace/out/matrix_redstone_comparator_jukebox_family_1/summary.md`
  passes 29/29. The complete aggregate at
  `c/magma/trace/out/matrix_redstone_comparator_jukebox_full_1/summary.md`
  passes 129/129 overall, state, and raw-block gates, 126 required behavior
  gates plus three not-required rows, in 540.150 seconds. Every case has
  20 matching state features, zero divergences, exact raw block/light outcomes,
  and only the explicit unsupported `death_time` subfield.
- GPU-1 performance remains above every floor at 4,161 scalar steps/s, 2.91M
  Blaze env-ticks/s, and 25.77 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_comparator_jukebox.json`. Jukebox
  state reuses the cold coordinate-indexed pool and reads one slot only when a
  comparator queries it; there is no tick hook, audio work, or idle scan.

## 2026-07-30 (saved command-block comparator success count)

- The next R-03 source covers command-block IDs 137/210/211 without claiming
  command execution. Java capture accepts only exact inert defaults: empty
  command, name `@`, output tracking enabled, no last output or command-result
  stats, unpowered/unmet condition state, the vanilla per-ID `auto` value, no
  pending command callback, and `successCount` in 0..15.
- The parked Java setter changes only the real `CommandBlockBaseLogic`
  success count and notifies comparators. The neutral capsule validates a
  distinct inventory-free schema, matching block ID, no-same-cell command
  callback, and a 256-tile bound. Magma restores the same state into a cold
  dynamically grown pool with no tick or command-execution hook.
- The deliberate omission at
  `c/magma/trace/out/redstone_comparator_saved_command_block_probe_2/summary.md`
  has exact pre-tick blocks, command/comparator tiles, and pending work. Its
  first and only state divergence is observation-1 comparator output: Java
  stores 7 and powers the lamp while Magma remains at zero, producing exactly
  the two expected block mismatches.
- Magma now returns the represented success count as the comparator override.
  Native tests cover all three IDs at output 7, zero output, count-16
  rejection, exact get/count state, and tile retirement on block replacement.
  The three corrected oracle cases pass at
  `c/magma/trace/out/redstone_comparator_saved_command_blocks_fix_1/summary.md`.
- The 32-case affected family at
  `c/magma/trace/out/matrix_redstone_comparator_command_block_family_1/summary.md`
  passes 32/32 in 125.181 seconds. The complete aggregate at
  `c/magma/trace/out/matrix_redstone_comparator_command_block_full_1/summary.md`
  passes 132/132 overall, state, and raw-block gates, 129 required behavior
  gates plus three not-required rows, in 542.279 seconds. Every row has 20
  matching state features, zero divergences, exact raw block/light outcomes,
  and only the explicit unsupported `death_time` subfield.
- GPU-1 performance remains above every floor at 3,997 scalar steps/s, 2.90M
  Blaze env-ticks/s, and 24.38 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_comparator_command_block.json`.
  Command tiles allocate only when exact saved state materializes and are
  queried only at a comparator-reached coordinate, so no idle scan was added.

## 2026-07-30 (saved item-frame comparator source)

- Vanilla consults an item frame only when a comparator's immediate rear
  input is a normal cube, the direct signal is below 15, the second block is
  air, and exactly one frame in that cell faces the comparator direction. An
  empty frame emits zero; a displayed item emits `rotation % 8 + 1`.
- Java now captures a dedicated complete item-frame list with exact entity ID,
  pose, hanging coordinate, facing, item/count/metadata, and rotation. The
  parked setter and neutral capsule admit only the bounded empty/plain-stone
  subset, validate the air cell and normal-cube support, reject ambiguity, and
  preserve the real comparator notification/callback boundary.
- The deliberate omission at
  `c/magma/trace/out/redstone_comparator_saved_item_frame_probe_1/summary.md`
  has exact pre-tick blocks, frame, comparator tile, and pending work. Its
  first divergence is observation-1 comparator output: Java stores 7 and
  powers the lamp while Magma stays at zero, producing exactly the expected
  comparator and lamp block mismatches.
- Magma restores represented frames into a cold pool capped at 256 and applies
  the same rear-cell, direction, and exact-one-frame query. Native tests prove
  rotation 6 -> 7, rotation 7 -> 8, empty -> 0, invalid rotation and item
  rejection, and retirement when the hanging cell or support changes. The
  corrected oracle fixture passes at
  `c/magma/trace/out/redstone_comparator_saved_item_frame_fix_1/summary.md`.
- The 32-case affected family at
  `c/magma/trace/out/matrix_redstone_comparator_item_frame_family_1/summary.md`
  passes 32/32 in 135.584 seconds. The complete aggregate at
  `c/magma/trace/out/matrix_redstone_comparator_item_frame_full_1/summary.md`
  passes 133/133 overall, state, and raw-block gates, 130 required behavior
  gates plus three not-required rows, in 576.490 seconds. Every row has 21
  matching state features, zero divergences, exact raw block/light outcomes,
  and only the explicit unsupported `death_time` subfield.
- A four-oracle-pool-loaded performance sample was intentionally preserved at
  `c/magma/trace/out/perf_guard_redstone_comparator_item_frame_oracle_pool_loaded.json`
  but rejected as promotion evidence because concurrent Minecraft clients
  depressed CPU and CUDA results. After stopping the three extra workers, the
  clean guard passed every floor at 4,196 scalar steps/s, 2.92M Blaze
  env-ticks/s, and 25.04 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_comparator_item_frame.json`. The
  frame pool has no tick hook or idle scan; it is queried only after a
  comparator reaches the exact represented rear cell.
- This promotion does not claim item-frame damage, drops, maps/tags, rendering,
  or general lifecycle. Observer pulses are the next ordered R-03 slice.

## 2026-07-30 (observer pulse lifecycle)

- Derived block 218 directly from the 1.11.2 `BlockObserver` source: metadata
  bits 0..2 encode all six facings, bit 3 is powered, only the watched face
  calls `observedNeighborChange`, and an unpowered observer with no pending
  work schedules +2/priority 0. That callback powers the observer and
  schedules release +2; output strength 15 is weak and strong only opposite
  the watched face. Live placement starts the same pulse.
- The first directional omission at
  `c/magma/trace/out/redstone_observer_west_probe_1/summary.md` retained
  Java's exact +2/+4 queue while Magma had none, with only the observer and
  lamp raw states diverging. The corrected case passes at
  `c/magma/trace/out/redstone_observer_west_fix_1/summary.md`.
- Early fixture placement exposed a setup-phase hazard rather than a runtime
  defect: observation zero could begin while the vanilla placement pulse's
  lamp was still lit. The rejected result remains at
  `c/magma/trace/out/matrix_redstone_observer_six_faces_1/summary.md`.
  A generic validated `fixture_drain_ticks` control now advances early Java
  fixtures before the shared snapshot; observer fixtures drain 8 or 12 ticks.
  The deterministic six-face matrix passes 6/6 at
  `c/magma/trace/out/matrix_redstone_observer_six_faces_2/summary.md`.
- Added exact runtime handling for watched-face notification, duplicate and
  powered suppression, +2 activation/release dispatch, directional output,
  placement pulses, observer-to-observer notification ordering, and output
  neighborhood notification on powered pending removal. There is no observer
  scan: edits and callbacks check only the six adjacent cells. Native tests
  cover the complete lifecycle plus non-watched edits and invalid-facing
  negatives.
- Separate omission/fix pairs isolate the nontrivial boundaries:
  placement at
  `c/magma/trace/out/redstone_observer_placement_probe_1/summary.md` and
  `c/magma/trace/out/redstone_observer_placement_fix_1/summary.md`; saved
  pending activation at
  `c/magma/trace/out/redstone_observer_saved_probe_1/summary.md` and
  `c/magma/trace/out/redstone_observer_saved_fix_1/summary.md`; and powered
  pending removal through one normal cube at
  `c/magma/trace/out/redstone_observer_powered_break_probe_1/summary.md` and
  `c/magma/trace/out/redstone_observer_powered_break_fix_1/summary.md`.
  The observer-chain fixture passes at
  `c/magma/trace/out/redstone_observer_chain_1/summary.md`.
- The capsule now admits exact proof-safe observer callbacks with facing
  validation and fully represented inert/observer/lamp notification regions.
  Load remains notification-free; due/priority/order are restored before
  ticking. Capsule round-trip and malformed-state negatives pass.
- The focused family at
  `c/magma/trace/out/matrix_redstone_observer_family_1/summary.md` passes
  12/12. The complete aggregate at
  `c/magma/trace/out/matrix_redstone_observer_full_1/summary.md` passes
  145/145 overall, state, and raw-block gates, with 142 required behavior
  gates plus three not-required rows, in 607.942 seconds. Every row has 21
  matching state features, zero divergences, exact raw block/light outcomes,
  and only the explicit unsupported `death_time` subfield.
- The clean GPU-1 guard at
  `c/magma/trace/out/perf_guard_redstone_observer.json` passes every floor at
  4,135 scalar steps/s, 2.91M Blaze env-ticks/s, and 25.90 CUDA fps.

## 2026-07-30 (normal-piston empty extension lifecycle)

- Started R-04 from the earliest observable piston transition. A normal
  piston ID 33 facing EAST begins unpowered with air in front; a tick-zero
  redstone block on its south side supplies power. Vanilla queues a block
  event rather than a scheduled tick, drains it in the same server tick,
  changes base 33:5 to 33:13, and creates moving head 36:5 before observation
  zero.
- The deliberate omission at
  `c/magma/trace/out/redstone_piston_east_empty_extension_probe_1/summary.md`
  has an exact shared prestate and 21/21 observed-state features matching. Its
  first raw divergence is the base metadata, followed only by the absent
  moving head: Java has three mutations while Magma has only the shared
  redstone source. The corrected one-observation case passes at
  `c/magma/trace/out/redstone_piston_east_empty_extension_fix_1/summary.md`.
- Independent two- and three-observation Java probes establish the tile
  lifecycle: block 36:5 remains through the second observation and becomes
  settled head 34:5 on the third. Magma now uses a fixed 64-entry active set
  carrying moved block/meta, facing, extending/source flags, progress, and
  last progress. The tile advances `0→0.5→1`, then retires and materializes
  the head on the following tick. The inactive path is one count check; no
  world scan or allocation was added.
- Native tests cover the immediate base/moving-head transition, exact
  `lastProgress/progress` values after each tile tick, third-tick settlement,
  and the negative rule that powered normal pistons cannot move obsidian.
  The focused family at
  `c/magma/trace/out/matrix_redstone_piston_empty_extension_family_1/summary.md`
  passes 4/4 across start, retained motion, settlement, and obsidian cases.
- The complete aggregate at
  `c/magma/trace/out/matrix_redstone_piston_empty_extension_full_1/summary.md`
  passes 149/149 overall, state, and raw-block gates, with 146 required
  behavior gates plus three not-required rows, in 619.687 seconds. Every row
  has 21 matching state features, zero divergences, exact raw block/light
  outcomes, and only the explicit unsupported `death_time` subfield.
- The first post-matrix guard at
  `c/magma/trace/out/perf_guard_redstone_piston_empty_extension.json` retained
  passing CPU/Blaze results but failed its CUDA median at 23.90 fps. That
  failure was preserved rather than promoted. An isolated CUDA rerun passed
  at 25.79 fps in
  `c/magma/trace/out/perf_guard_redstone_piston_empty_extension_cuda_rerun_1.json`;
  the subsequent full clean guard at
  `c/magma/trace/out/perf_guard_redstone_piston_empty_extension_clean_1.json`
  passes every floor at 4,130 scalar steps/s, 2.92M Blaze env-ticks/s, and
  25.68 CUDA fps.
- This first proof does not claim the remaining facings/power inputs, pushed
  blocks, retraction, sticky pistons, quasi-connectivity, slime attachment,
  moving-tile save/reload, moving geometry/rendering, or entity collision.

## 2026-07-30 (six-facing empty-piston coverage)

- Added DOWN, UP, NORTH, SOUTH, and WEST fixtures to the promoted EAST
  normal-piston case. Each starts with air at the output, applies a tick-zero
  redstone block on a non-output side, and asserts the facing-specific
  extended base plus moving-head metadata at the first observation. The five
  new Java-vs-Magma probes pass 5/5 at
  `c/magma/trace/out/matrix_redstone_piston_empty_extension_new_faces_1/summary.md`.
- No simulation fix was needed: the direction-table implementation already
  generalized correctly. Native runtime tests now loop over all six facings
  and check exact base, moving block, tile facing, progress, and third-tick
  settlement. The expanded focused family at
  `c/magma/trace/out/matrix_redstone_piston_empty_extension_six_faces_family_1/summary.md`
  passes 9/9 across six starts, EAST progress/settlement, and the obsidian
  negative.
- The complete aggregate at
  `c/magma/trace/out/matrix_redstone_piston_empty_extension_six_faces_full_1/summary.md`
  passes 154/154 overall, state, and raw-block gates, with 151 required
  behavior gates plus three not-required rows, in 654.059 seconds. Every row
  has 21 matching state features, zero divergences, exact raw block/light
  outcomes, and only the explicit unsupported `death_time` subfield.
- The first performance sample under heavy unrelated host work is preserved
  at
  `c/magma/trace/out/perf_guard_redstone_piston_empty_extension_six_faces.json`;
  CPU (3,677 steps/s) and CUDA (22.96 fps) failed while Blaze passed. A
  one-core diagnostic at
  `c/magma/trace/out/perf_guard_redstone_piston_empty_extension_six_faces_clean_1.json`
  passed CPU and Blaze but artificially held the renderer to 24.20 fps. The
  comparable normal-affinity full guard at
  `c/magma/trace/out/perf_guard_redstone_piston_empty_extension_six_faces_clean_2.json`
  passes every frozen floor at 4,087 scalar steps/s, 2.92M Blaze env-ticks/s,
  and 25.07 CUDA fps. This coverage-only expansion adds no runtime work.
- R-04 remains active. The next ordered slice is additional piston power
  inputs, followed by pushable blocks, retraction/sticky behavior,
  quasi-connectivity, slime, moving-state persistence/rendering, and entity
  collision.

## 2026-07-30 (direct powered-lever piston input)

- Added the first non-redstone-block piston input as an isolated tick-zero
  edge. A floor lever south of an EAST-facing normal piston begins at
  metadata 5 and flips to powered metadata 13. Java drains the piston block
  event in that same server tick, producing the lever mutation, base
  33:5-to-33:13, and moving head 36:5 before observation zero.
- The deliberate omission at
  `c/magma/trace/out/redstone_piston_east_lever_empty_extension_probe_1/summary.md`
  retains an exact shared prestate, empty scheduled queue, and 21/21 supported
  state features. Magma changes only the lever; its first raw divergence is
  the base metadata followed by exactly the absent moving head. No later
  symptom was used to diagnose the fix.
- The bounded piston side-power helper now accepts a directly adjacent powered
  lever as well as a redstone block, while still excluding the output face.
  It does not broaden retraction, quasi-connectivity, or other source claims.
  Native tests cover powered-lever start and settlement plus an unpowered
  metadata-5 negative. The corrected oracle case passes at
  `c/magma/trace/out/redstone_piston_east_lever_empty_extension_fix_1/summary.md`.
- The ten-case affected family at
  `c/magma/trace/out/matrix_redstone_piston_powered_lever_family_1/summary.md`
  passes 10/10. The complete aggregate at
  `c/magma/trace/out/matrix_redstone_piston_powered_lever_full_1/summary.md`
  passes 155/155 overall, state, and raw-block gates, with 152 required
  behavior gates plus three not-required rows, in 674.465 seconds. Every row
  has 21 matching state features, zero divergences, exact raw block/light
  outcomes, and only the explicit unsupported `death_time` subfield.
- The GPU-1 performance guard at
  `c/magma/trace/out/perf_guard_redstone_piston_powered_lever.json` passes
  every frozen floor at 4,280 scalar steps/s, 2.91M Blaze env-ticks/s, and
  25.96 CUDA fps. This adds six bounded neighbor metadata reads only when a
  represented piston receives a notification; there is no new idle scan,
  allocation, or active-tick work.
- R-04 remains active with remaining direct/indirect power inputs next,
  followed by pushable blocks and the retraction/sticky/quasi-connectivity
  bundles.

## 2026-07-30 (direct powered-button piston inputs)

- Added independent setter-edge fixtures for stone button ID 77 and wooden
  button ID 143 beside an EAST-facing empty normal piston. Each changes from
  metadata 5 to powered metadata 13 at tick zero and isolates direct weak
  power from the separate interaction-created release callback.
- Both deliberate omissions retain exact shared prestate, source mutation,
  empty scheduled work, and all 21 supported state features. Each first
  diverges at base 33:5-to-33:13 and has exactly one additional absent moving
  head 36:5:
  `c/magma/trace/out/redstone_piston_east_button_empty_extension_probe_1/summary.md`
  and
  `c/magma/trace/out/redstone_piston_east_wooden_button_empty_extension_probe_1/summary.md`.
- The bounded side-power helper now recognizes powered IDs 77/143 behind the
  same metadata-bit check used by the existing exact redstone controls.
  Native tests cover powered start, three-tick settlement, and unpowered
  negatives for both IDs. Corrected oracle cases pass in the corresponding
  `redstone_piston_east_button_empty_extension_fix_1` and
  `redstone_piston_east_wooden_button_empty_extension_fix_1` directories.
- The direct-control family at
  `c/magma/trace/out/matrix_redstone_piston_direct_controls_family_1/summary.md`
  passes 12/12. The complete aggregate at
  `c/magma/trace/out/matrix_redstone_piston_direct_controls_full_1/summary.md`
  passes 157/157 overall, state, and raw-block gates, with 154 required
  behavior gates plus three not-required rows, in 649.536 seconds.
- Concurrent 64-thread TAK workloads contaminated three renderer samples:
  the full sample at
  `c/magma/trace/out/perf_guard_redstone_piston_direct_controls.json`
  retained passing CPU/Blaze but CUDA at 21.38 fps; a 40-core affinity
  diagnostic fell to 20.33 fps; and an ordinary isolated retry remained at
  20.80 fps. None was promoted. After the saturating job retired, isolated
  CUDA passed at 25.53 fps and the clean full guard at
  `c/magma/trace/out/perf_guard_redstone_piston_direct_controls_clean_1.json`
  passed every floor at 3,960 scalar steps/s, 2.92M Blaze env-ticks/s, and
  25.43 CUDA fps.
- The next stone-pressure-plate strength-0-to-1 omission is already captured
  at
  `c/magma/trace/out/redstone_piston_east_stone_pressure_plate_empty_extension_probe_1/summary.md`.
  It has the same exact two-cell piston divergence and remains active rather
  than being counted in the 157-case promoted baseline.

## 2026-07-30 (direct pressure-plate piston inputs)

- Expanded the active plate probe to all 1.11.2 pressure-plate IDs: stone 70,
  wooden 72, light weighted 147, and heavy weighted 148. Tick-zero setter
  edges use strengths 1, 1, 7, and 1, respectively, beside the same
  EAST-facing empty normal piston. These cases isolate boolean direct power
  from entity collision and scheduled occupancy callbacks.
- Before implementation, all four rows at
  `c/magma/trace/out/matrix_redstone_piston_pressure_plates_probe_1/summary.md`
  have exact shared prestate/source mutation, empty scheduled work, 21/21
  supported state features, and exactly two raw mismatches: base
  33:5-to-33:13 and moving head 36:5.
- The piston side-power helper now reuses the existing exact pressure-plate
  predicate and accepts any nonzero represented strength. Native tests loop
  all four IDs, their promoted strengths, exact three-tick settlement, and
  zero-strength negatives. The corrected four-case oracle matrix at
  `c/magma/trace/out/matrix_redstone_piston_pressure_plates_fix_1/summary.md`
  passes 4/4.
- The 16-case affected family at
  `c/magma/trace/out/matrix_redstone_piston_direct_power_sources_family_1/summary.md`
  passes 16/16. The complete aggregate at
  `c/magma/trace/out/matrix_redstone_piston_direct_power_sources_full_1/summary.md`
  passes 161/161 overall, state, and raw-block gates, with 158 required
  behavior gates plus three not-required rows, in 659.149 seconds. Existing
  plate collision, reschedule, release, weighted analog, and entity-state
  regressions all remain exact.
- The clean GPU-1 performance guard at
  `c/magma/trace/out/perf_guard_redstone_piston_direct_power_sources.json`
  passes every frozen floor at 4,138 scalar steps/s, 2.91M Blaze env-ticks/s,
  and 25.82 CUDA fps. Plate recognition adds only bounded comparisons in the
  already-notified piston query; no scan, allocation, or inactive tick path
  was added.
- R-04 now moves to directional producers and indirect power through normal
  cubes before pushable-block structure traversal.

## 2026-07-30 (directional torch piston input and piston opacity)

- Added complementary EAST-piston fixtures for lit-redstone-torch weak-power
  directionality. A floor torch south of the piston powers its north neighbor
  and must start empty extension; a floor torch directly above the piston is
  queried on its attachment face and must leave the piston retracted.
- The deliberate two-case probe at
  `c/magma/trace/out/matrix_redstone_piston_torch_direction_probe_1/summary.md`
  retains exact shared prestates, empty scheduled queues, and all 21 supported
  state features. The positive case first diverges at only the unextended base
  and absent moving head. The negative behavior and raw blocks are already
  exact, but its block-light gate exposes an independent earlier defect:
  Java's non-opaque piston base carries block light 6 while magma had zero.
- Piston blocks 29/33, head 34, and moving block 36 now use vanilla opacity
  zero. The isolated wrong-face fixture then matches all 10,625 block-light
  cells at
  `c/magma/trace/out/redstone_piston_east_torch_attached_face_light_fix_1/summary.md`.
  The bounded piston side-power query now reuses the existing
  metadata-derived lit-torch face predicate. The corrected positive/negative
  pair passes 2/2 at
  `c/magma/trace/out/matrix_redstone_piston_torch_direction_fix_1/summary.md`.
  Native regressions cover powered start, three-tick settlement, wrong-face
  rejection, and exact block-light 6 through the retracted base.
- The complete affected piston family at
  `c/magma/trace/out/matrix_redstone_piston_directional_torch_family_1/summary.md`
  passes 18/18. The aggregate at
  `c/magma/trace/out/matrix_redstone_piston_directional_torch_full_1/summary.md`
  passes 163/163 overall, state, and raw-block gates, with 160 required
  behavior gates plus three not-required rows, in 687.414 seconds. Every case
  has 21 matching state features, zero divergences, and only the explicit
  unsupported `death_time` subfield.
- The GPU-1 performance guard at
  `c/magma/trace/out/perf_guard_redstone_piston_directional_torch.json`
  passes every frozen floor at 4,253 scalar steps/s, 2.91M Blaze env-ticks/s,
  and 26.49 CUDA fps. Torch direction is six bounded metadata checks only when
  an already-represented piston is notified; the disabled path remains one
  `piston_count == 0` branch with no scan or allocation.
- R-04 remains active. Remaining directional producers and indirect
  normal-cube power are next, followed by pushable-block traversal.

## 2026-07-30 (directional powered-repeater piston input)

- Added a powered-repeater positive and rotated negative beside the same
  EAST-facing empty normal piston. Stable redstone blocks behind each source
  make the tick-zero states valid: repeater 94:0 outputs north into the
  piston, while repeater 94:1 outputs east and must leave it retracted.
- The deliberate matrix at
  `c/magma/trace/out/matrix_redstone_piston_repeater_direction_probe_1/summary.md`
  retains the prior torch wrong-face case as a regression control. The torch
  and rotated repeater pass. Only the oriented repeater fails, with exact
  shared prestate, source placement, empty scheduled queue, block lighting,
  and 21/21 supported state features; its two raw mismatches are precisely
  base 33:5-to-33:13 and the absent moving head 36:5.
- The bounded piston side-power query now accepts block 94 only when
  `BlockHorizontal` metadata points its powered output at the queried face.
  Native regressions cover start, three-tick settlement, and rotated
  rejection. The corrected positive, negative, and torch control pass 3/3 at
  `c/magma/trace/out/matrix_redstone_piston_repeater_direction_fix_1/summary.md`.
- The expanded affected family at
  `c/magma/trace/out/matrix_redstone_piston_directional_repeater_family_1/summary.md`
  passes 20/20. The complete aggregate at
  `c/magma/trace/out/matrix_redstone_piston_directional_repeater_full_1/summary.md`
  passes 165/165 overall, state, and raw-block gates, with 162 required
  behavior gates plus three not-required rows, in 691.317 seconds.
- The first performance sample under concurrent unrelated CPU work is
  preserved at
  `c/magma/trace/out/perf_guard_redstone_piston_directional_repeater.json`:
  Blaze and CUDA pass at 2.90M env-ticks/s and 25.37 fps, but scalar CPU
  measures 3,827 steps/s against the 3,858.9 floor. An immediate
  ordinary-affinity full rerun with no code change passes every floor at
  4,278 scalar steps/s, 2.91M Blaze env-ticks/s, and 25.96 CUDA fps in
  `c/magma/trace/out/perf_guard_redstone_piston_directional_repeater_rerun_1.json`.
  The new path is one metadata comparison only when a represented piston is
  notified; no scan, allocation, or inactive work was added.
- R-04 now moves to powered-comparator directionality, then observers, wire,
  and indirect normal-cube/quasi-connectivity inputs.

## 2026-07-30 (directional powered-comparator piston input)

- Added a saved-tile comparator pair at a stricter on-add boundary than the
  repeater fixtures. Each world restores powered comparator 150 with exact
  tile output 15 and a stable rear redstone block; tick zero then places the
  EAST-facing piston itself. Comparator 150:0 outputs north and must extend,
  while rotated 150:1 outputs east and must leave the piston retracted.
- The first probe at
  `c/magma/trace/out/matrix_redstone_piston_comparator_direction_probe_1/summary.md`
  isolates two independent earliest differences. The oriented case has
  exactly the absent extended-base/moving-head pair. The rotated case has
  exact behavior, raw blocks, saved tile output, empty queue, and all 21
  supported state features, but 487 block-light mismatches centered on the
  comparator.
- Java 1.11.2 registers powered comparator 150 with light level 0.625, which
  becomes integer emission 9; the legacy C table had zero. After correcting
  this source property, the rotated negative matches all 10,625 block-light
  cells at
  `c/magma/trace/out/redstone_piston_east_comparator_wrong_direction_light_fix_1/summary.md`.
  One intervening two-case rerun is preserved at
  `c/magma/trace/out/matrix_redstone_piston_comparator_light_fix_1`;
  its second row is infrastructure-fail because the pool bridge timed out
  during reset before fixture staging. Restarting only pool instance 0
  produced the clean proof above.
- The piston query now accepts a comparator only when its block state is
  powered, its restored tile output is positive, and its metadata-derived
  output matches the queried face. Native regressions cover oriented start,
  three-tick settlement, rotated rejection, and output-zero rejection. The
  corrected comparator/repeater-control matrix passes 3/3 at
  `c/magma/trace/out/matrix_redstone_piston_comparator_direction_fix_1/summary.md`.
- The expanded piston family at
  `c/magma/trace/out/matrix_redstone_piston_directional_comparator_family_1/summary.md`
  passes 22/22. The aggregate at
  `c/magma/trace/out/matrix_redstone_piston_directional_comparator_full_1/summary.md`
  passes 167/167 overall, state, and raw-block gates, with 164 required
  behavior gates plus three not-required rows, in 733.320 seconds.
- The GPU-1 performance guard at
  `c/magma/trace/out/perf_guard_redstone_piston_directional_comparator.json`
  passes every fixed floor at 4,163 scalar steps/s, 2.91M Blaze env-ticks/s,
  and 26.04 CUDA fps. Comparator evaluation is bounded to the already-notified
  piston and an existing fixed-pool tile lookup; no scan, allocation, or
  inactive work was added.
- R-04 now advances to observer directionality, then wire and indirect
  normal-cube/quasi-connectivity inputs.

## 2026-07-30 (directional live-observer piston input)

- Replaced an invalid saved-powered observer setup with two live-pulse
  fixtures. Vanilla `onBlockAdded` clears a preloaded observer powered bit, so
  the first attempt at
  `c/magma/trace/out/matrix_redstone_piston_observer_direction_probe_1/summary.md`
  is retained as fixture-diagnostic evidence and is not counted as a parity
  proof. The valid fixtures start unpowered, drain the placement pulse for
  eight setup ticks, place piston 33:5 at tick zero, and edit the watched cell
  at tick one.
- The deliberate omission at
  `c/magma/trace/out/matrix_redstone_piston_observer_direction_probe_2/summary.md`
  proves both directions at the first uncontaminated tick. A SOUTH-watching
  observer 218:3 pulses to 218:11 and should output north into the piston; an
  EAST-watching 218:5 pulses to 218:13 but must leave that piston retracted.
  The rotated negative matches behavior, queue, all raw blocks, all 21
  supported state features, and light. The positive has exactly two
  mismatches: base 33:5 instead of 33:13 and absent moving head 36:5.
- The bounded piston power query now recognizes ID 218 only while powered and
  only on the observer's metadata-derived output face. Native regressions
  cover the live oriented start through settlement and the rotated
  no-extension control. The comparator control and both observer cases pass
  3/3 at
  `c/magma/trace/out/matrix_redstone_piston_observer_direction_fix_1/summary.md`.
- The expanded affected family passes 24/24 at
  `c/magma/trace/out/matrix_redstone_piston_directional_observer_family_1/summary.md`.
  The complete aggregate at
  `c/magma/trace/out/matrix_redstone_piston_directional_observer_full_1/summary.md`
  passes 169/169 overall, state, and raw-block gates, with 166 required
  behavior gates plus three not-required rows, in 699.626 seconds.
- Two performance samples taken while an unrelated training process consumed
  roughly 63 CPU cores are preserved at
  `c/magma/trace/out/perf_guard_redstone_piston_directional_observer.json` and
  `c/magma/trace/out/perf_guard_redstone_piston_directional_observer_rerun_1.json`.
  CPU/CUDA measured 3,257/23.33 and 3,338/23.45, below their fixed floors,
  while Blaze passed at 2.92M and 2.93M env-ticks/s. With no code or affinity
  change after that workload exited, the full guard at
  `c/magma/trace/out/perf_guard_redstone_piston_directional_observer_rerun_2.json`
  passes every floor at 4,221 scalar steps/s, 2.92M Blaze env-ticks/s, and
  25.56 CUDA fps.
- The observer check runs only in the already-notified represented-piston
  query; the `piston_count == 0` idle path remains one branch with no scan or
  allocation. R-04 now moves to wire and indirect normal-cube power before
  pushable-block traversal.

## 2026-07-30 (directional dust and indirect normal-cube piston power)

- Added four saved-source, tick-zero-piston fixtures derived from
  `BlockPistonBase.shouldBeExtended`, `World.getRedstonePower`, and
  `BlockRedstoneWire.getWeakPower`. Direct powered dust south of the piston
  connects on the north-south axis and must extend it; the equally powered
  east-west line is a no-extension control. The indirect positive puts
  powered dust on the south-adjacent stone so dust strongly powers its support
  and the Java-normal cube relays to the piston. The indirect negative keeps
  the cube but places east-west dust beyond it, where it does not strongly
  power that cube.
- The deliberate five-case matrix at
  `c/magma/trace/out/matrix_redstone_piston_wire_indirect_probe_1/summary.md`
  retains the prior rotated-observer negative. The observer and both new
  negatives pass every gate. Both positives retain exact shared prestate,
  source/cube/dust blocks, empty queue, light, and all 21 supported state
  features; each has exactly two raw mismatches: base 33:5 instead of 33:13
  and absent moving head 36:5.
- The bounded piston query now reuses the existing directional dust
  weak-power predicate and Java-normal-cube strong-power helper. Native
  regressions cover both starts through settlement and both perpendicular
  negatives. The corrected matrix passes 5/5 at
  `c/magma/trace/out/matrix_redstone_piston_wire_indirect_fix_1/summary.md`.
- The expanded affected piston family passes 28/28 at
  `c/magma/trace/out/matrix_redstone_piston_directional_wire_indirect_family_1/summary.md`.
  The complete aggregate at
  `c/magma/trace/out/matrix_redstone_piston_directional_wire_indirect_full_1/summary.md`
  passes 173/173 overall, state, and raw-block gates, with 170 required
  behavior gates plus three not-required rows, in 779.460 seconds.
- The GPU-1 performance guard at
  `c/magma/trace/out/perf_guard_redstone_piston_directional_wire_indirect.json`
  passes every frozen floor at 4,006 scalar steps/s, 2.91M Blaze env-ticks/s,
  and 25.62 CUDA fps. Both helpers inspect only the six neighbors of an
  already-notified represented piston; the one-branch empty path, allocation
  behavior, and inactive tick cost are unchanged.
- R-04 now advances to explicit output-face and piston self/strong-power
  boundaries, then the `pos.up()` quasi-connectivity neighborhood before
  pushable-block traversal.

## 2026-07-30 (piston output-face boundaries and quasi-connectivity)

- Added three source-derived geometry fixtures around a tick-zero EAST
  piston. A redstone block in the output/front cell is excluded from
  `shouldBeExtended`'s direct loop. A block one up and one south is in the
  fixed `pos.up()` quasi-connectivity neighborhood and must extend. Mirroring
  that block one down and one south is the no-extension control.
- The deliberate matrix at
  `c/magma/trace/out/matrix_redstone_piston_quasi_connectivity_probe_1/summary.md`
  passes the prior indirect-power control and both new negatives. Only the
  quasi positive fails, with exact shared prestate, source, empty queue,
  light, and 21 supported state features plus exactly the absent
  extended-base/moving-head pair.
- The piston query now applies the same bounded represented-source predicate
  to the five Java `pos.up()` neighbors except DOWN. Native regressions cover
  quasi start/settlement, front-face exclusion, and below-diagonal rejection.
  The corrected matrix passes 4/4 at
  `c/magma/trace/out/matrix_redstone_piston_quasi_connectivity_fix_1/summary.md`,
  the affected family passes 31/31 at
  `c/magma/trace/out/matrix_redstone_piston_quasi_connectivity_family_1/summary.md`,
  and the aggregate at
  `c/magma/trace/out/matrix_redstone_piston_quasi_connectivity_full_1/summary.md`
  passes 176/176 overall, state, and raw blocks, with 173 required behavior
  gates plus three not-required rows, in 743.154 seconds.

## 2026-07-30 (single-stone piston push)

- Added start, second-observation progress, and third-observation settlement
  cases for one stone in front of the powered EAST piston with air beyond.
  Java writes moving block 36 at both head and stone destinations, retains
  both through progress, then settles head 34:5 and stone 1:0.
- All three deliberate rows at
  `c/magma/trace/out/matrix_redstone_piston_single_stone_push_probe_1/summary.md`
  retain exact shared prestate, source, queue, 21 supported state features,
  and light. Each first differs across only base/front/destination. The
  existing quasi negative remains exact.
- The fixed moving-piston pool now admits exactly one front stone when the
  destination is air, storing independent head and moved-stone tiles without
  adding an idle scan or allocation. Native checks cover start, both moving
  positions through progress, and exact settlement. The corrected matrix
  passes 4/4 at
  `c/magma/trace/out/matrix_redstone_piston_single_stone_push_fix_1/summary.md`.
- The combined piston family passes 34/34 at
  `c/magma/trace/out/matrix_redstone_piston_single_stone_push_family_1/summary.md`.
  The complete aggregate at
  `c/magma/trace/out/matrix_redstone_piston_single_stone_push_full_1/summary.md`
  passes 179/179 overall, state, and raw-block gates, with 176 required
  behavior gates plus three not-required rows, in 760.646 seconds.
- Two performance samples under unrelated saturating work are preserved:
  `c/magma/trace/out/perf_guard_redstone_piston_quasi_connectivity.json`
  measured 3,156 scalar steps/s, 2.92M Blaze env-ticks/s, and 21.98 CUDA fps;
  `c/magma/trace/out/perf_guard_redstone_piston_single_stone_push.json`
  measured 3,628, 2.92M, and 23.63. CPU/CUDA failed while Blaze passed in both.
  After the 64-thread self-play and transient NNUE/CUDA-toolchain workers
  retired, the unchanged ordinary-affinity full guard at
  `c/magma/trace/out/perf_guard_redstone_piston_single_stone_push_rerun_1.json`
  passes every floor at 4,163 scalar steps/s, 2.91M Blaze env-ticks/s, and
  25.54 CUDA fps.
- R-04 now advances to multi-block structure traversal, 12-block rejection,
  and destroy reactions before retraction/sticky/slime behavior.

## 2026-07-31 (straight stone-line piston traversal and 12-block limit)

- Extended the represented normal-piston movement from one stone to a bounded
  straight line. The extension path reads forward only until the first
  non-stone cell or the vanilla 12-block maximum, rejects a blocked or
  13-stone line, and creates fixed-pool moving tiles from farthest to nearest
  before creating the moving head. It adds no idle scan or heap allocation.
- Added two-stone start and settlement fixtures, a legal 12-stone start, and
  an intact 13-stone rejection. The first exploratory long-line fixture
  crossed the staged player; the second moved clear of the player but put the
  legal destination outside the cleared platform on a generated leaf. Those
  runs remain at
  `c/magma/trace/out/matrix_redstone_piston_stone_line_limit_probe_1/summary.md`,
  `c/magma/trace/out/matrix_redstone_piston_stone_line_limit_probe_2/summary.md`,
  and
  `c/magma/trace/out/matrix_redstone_piston_stone_line_limit_fix_1/summary.md`
  as rejected/partial fixture diagnostics, not broad parity evidence.
- The final x=5 long fixtures keep the piston, source, complete stone line,
  and destination inside the verified platform. The corrected focused matrix
  passes 5/5 at
  `c/magma/trace/out/matrix_redstone_piston_stone_line_limit_fix_2/summary.md`.
  Native tests additionally settle the two-stone and 12-stone structures and
  verify every member of the rejected 13-stone line remains intact.
- The affected piston family passes 38/38 at
  `c/magma/trace/out/matrix_redstone_piston_stone_line_limit_family_1/summary.md`.
  The full matrix at
  `c/magma/trace/out/matrix_redstone_piston_stone_line_limit_full_1/summary.md`
  passes 183/183 overall, state, and raw-block gates, with 180 required
  behavior gates plus three not-required rows, in 824.064 seconds.
- The first unchanged performance guard is preserved at
  `c/magma/trace/out/perf_guard_redstone_piston_stone_line_limit.json`.
  At host load 75.98 under an unrelated 60-thread self-play process it
  measured 3,076 scalar steps/s, 2.92M Blaze env-ticks/s, and 22.48 CUDA fps:
  Blaze passed, while CPU and the CPU-fed renderer failed. The slice remains
  correctness-qualified but not performance-promoted until the same
  ordinary-affinity command passes after that contention retires.
- R-04 now advances to push/destroy block reactions, then retraction, sticky
  pistons, slime attachment, moving-tile save/reload, rendering, and entity
  collision.

## 2026-07-31 (first piston block reactions and moving-item collision)

- Added exact front and terminal dandelion DESTROY fixtures plus birch-planks
  NORMAL start/settlement fixtures. The terminal case moves one stone after
  destroying the flower; obsidian remains the independent no-move control.
- Java's destroyed flower creates an EntityItem before the moving piston tile
  ticks. Magma now follows the same ordinary-entity-before-tile order, consumes
  the exact World.rand and Math.random draws, preserves the authoritative EID
  and stack, and sweeps the already-ticked item against the moving block.
  Collision uses exact six-facing piston-head plate/arm shapes, full cubes for
  represented normal blocks, movement-area overlap plus 0.01, and the
  per-entity/per-axis 0.51 piston clamp. Native tests prove the exact front
  x=13.635 and terminal x=14.635 positions and controlled sweeps in all six
  directions.
- Reused Java worlds initially exposed unrelated falling entities and
  process-global RNG consumers between parked capture and the controlled
  piston event. The oracle now forces immediate background falling-block
  resolution except in the dedicated falling-sand case, drains setup-only
  entities to a 40-tick quiet boundary, restores World.rand, Math.random, and
  next-entity-ID cursors immediately before the mutation, and explicitly
  drains the just-queued piston block event at the intended server boundary.
  Rejected contaminated diagnostics remain preserved in the earlier
  `matrix_redstone_piston_destroy_reaction_fix_{2,3,4}` directories.
- The stricter setup boundary exposed genuine pending flowing/still water
  callbacks in the survival fixtures. Capturing block IDs 8 and 9 preserves
  those callbacks rather than restaging the fixture later; the focused
  survival matrix passes 3/3 at
  `c/magma/trace/out/matrix_survival_scheduled_water_capture_1/summary.md`.
  The dedicated falling-sand harness regression also passes.
- The final focused reaction proof passes 3/3 at
  `c/magma/trace/out/matrix_redstone_piston_destroy_reaction_fix_5/summary.md`.
  The planks/control proof passes 3/3 at
  `c/magma/trace/out/matrix_redstone_piston_normal_reaction_planks_1/summary.md`.
  The expanded piston family passes 42/42 at
  `c/magma/trace/out/matrix_redstone_piston_block_reactions_family_1/summary.md`.
  After correcting scheduled-water capture, the full matrix passes 187/187
  at `c/magma/trace/out/matrix_block_reactions_full_rerun_1/summary.md`, with
  187 state/raw-block gates, 184 required behavior gates, and three
  not-required rows in 936.851 seconds.
- Performance remains unpromoted under unrelated host contention. The
  ordinary sample at
  `c/magma/trace/out/perf_guard_redstone_piston_block_reactions.json`
  measured 3,236 scalar steps/s, 2.92M Blaze env-ticks/s, and 22.31 CUDA fps.
  A diagnostic pinned to quieter CPUs 0-59 at
  `c/magma/trace/out/perf_guard_redstone_piston_block_reactions_isolated_0_59_1.json`
  measured 3,288, 2.93M, and 23.67. Blaze passed; the independent unchanged
  mc-sim CPU benchmark and CPU-fed renderer failed during the same host load,
  so no floor or baseline was changed.

## 2026-07-31 (metadata-preserving flower piston drops)

- Added an allium fixture using red flower ID 38 metadata 2. Java source shows
  `BlockFlower.damageDropped` preserves the variant metadata. The deliberate
  oracle probe at
  `c/magma/trace/out/redstone_piston_front_allium_destroy_probe_1/summary.md`
  has exact shared prestate and first differs only at the retracted base,
  intact flower, and missing EntityItem.
- The bounded flower DESTROY path now accepts dandelion 37:0 and every valid
  red-flower state 38:0..8. It still rejects all other DESTROY blocks until
  their exact payload and teardown side effects are represented. The native
  regression proves item 38:2, timers, EID, and the moving-head sweep; the
  focused Java-vs-Magma fix passes at
  `c/magma/trace/out/redstone_piston_front_allium_destroy_fix_1/summary.md`.
- The expanded piston family passes 43/43 at
  `c/magma/trace/out/matrix_redstone_piston_flower_destroy_family_1/summary.md`.
  The full aggregate passes 188/188 at
  `c/magma/trace/out/matrix_redstone_piston_flower_destroy_full_1/summary.md`,
  with 188 state gates, 185 required behavior gates plus three not-required
  rows, and 188 raw-block gates.
- Host load exceeded 90 under unrelated self-play, training, search, and
  backup jobs after the aggregate. No performance sample from that window is
  accepted; the clean full guard remains the next promotion requirement.

## 2026-07-31 (orientation-stripping torch piston drop)

- Added a supported floor-torch 50:5 directly in front of the powered piston.
  This is the first represented DESTROY state whose dropped item metadata
  differs from block metadata: Java emits item 50:0 because orientation is a
  block property, not item damage.
- The deliberate probe at
  `c/magma/trace/out/redstone_piston_front_floor_torch_destroy_probe_1/summary.md`
  has exact shared prestate and first differs only at the retracted piston,
  intact torch, and missing item. Java's item state confirms count 1,
  metadata 0, age 1, pickup delay 9, and the exact swept x=13.635 position.
- Replaced the implicit ID/meta copy with an explicit exact payload mapper for
  every admitted piston DESTROY state. Dandelion 37:0 maps to 37:0, red
  flowers 38:0..8 preserve variant damage, and torch orientations 50:1..5 map
  to item 50:0. Every other DESTROY state remains rejected until its algorithm
  and side effects are represented. The native regression covers the
  orientation strip and sweep; the focused oracle fix passes at
  `c/magma/trace/out/redstone_piston_front_floor_torch_destroy_fix_1/summary.md`.
- The piston family passes 44/44 at
  `c/magma/trace/out/matrix_redstone_piston_torch_destroy_family_1/summary.md`.
  The complete aggregate passes 189/189 at
  `c/magma/trace/out/matrix_redstone_piston_torch_destroy_full_1/summary.md`,
  with 189 state/raw-block gates, 186 required behavior gates, and three
  not-required rows.
- This change adds no idle work: payload resolution occurs only after a
  represented powered piston encounters a registry-DESTROY block. Performance
  promotion remains pending because unrelated host load stayed above 90.

## 2026-07-31 (block-to-item redstone-wire piston drop)

- Added supported unpowered wire block 55 in the piston output cell.
  `BlockRedstoneWire.getItemDropped` returns registered redstone item 331, so
  this is the first represented DESTROY payload whose item ID differs from
  the block ID.
- The deliberate probe at
  `c/magma/trace/out/redstone_piston_front_wire_destroy_probe_1/summary.md`
  has exact shared prestate and the same two-cell piston omission plus missing
  item as the preceding payload probes. Java emits exactly one EntityItem
  331:0 with the expected timers and swept x=13.635.
- Extended the constant payload mapper so every wire metadata 0..15 maps to
  item 331:0. The native test proves the item-ID conversion and moving-head
  sweep; the focused Java-vs-Magma fix passes at
  `c/magma/trace/out/redstone_piston_front_wire_destroy_fix_1/summary.md`.
- The piston family passes 45/45 at
  `c/magma/trace/out/matrix_redstone_piston_wire_destroy_family_1/summary.md`.
  The full aggregate passes 190/190 at
  `c/magma/trace/out/matrix_redstone_piston_wire_destroy_full_1/summary.md`,
  with 190 state/raw-block gates, 187 required behavior gates, and three
  not-required rows.
- Payload lookup remains inside the already-active piston DESTROY path and
  adds no disabled/idle scan, allocation, or world-capacity work. A valid
  clean performance run is still pending while unrelated host work saturates
  the CPU.

## 2026-07-31 (zero-drop fire piston destruction)

- Added a proof-safe source-triggered fixture that pushes one stone into
  supported terminal fire. Java replaces both cells with moving block 36:5,
  creates no local EntityItem, and retains the fire's already-scheduled
  future block-51 callback.
- The deliberate probe at
  `c/magma/trace/out/redstone_piston_stone_then_fire_destroy_probe_1/summary.md`
  has exact shared prestate and a passing state gate, including the ordered
  callback. Magma rejects only the piston traversal, leaving the base, stone,
  and fire intact after the common source mutation.
- Split the bounded DESTROY payload result into unsupported, no-drop, and
  one-item outcomes. Fire metadata 0..15 takes the no-drop path and returns
  before World.rand, Math.random, entity-capacity, or entity-ID work. Native
  checks prove two moving tiles, zero entities, and unchanged RNG/ID cursors.
  The focused oracle fix passes at
  `c/magma/trace/out/redstone_piston_stone_then_fire_destroy_fix_1/summary.md`.
- The piston family passes 46/46 at
  `c/magma/trace/out/matrix_redstone_piston_fire_no_drop_family_1/summary.md`.
  The complete matrix passes 191/191 at
  `c/magma/trace/out/matrix_redstone_piston_fire_no_drop_full_1/summary.md`:
  191 state gates, 188 required behavior gates plus three not-required rows,
  and 191 raw-block gates.
- `game/test_runtime.sh`, `game/test_script.sh`, the state-capsule selftest,
  Python/shell syntax checks, and `git diff --check` pass. The four dedicated
  oracle instances are stopped and `schemas.index` is canonical.
- No performance result was promoted. Host load was 71.63 with an unrelated
  44-thread self-play process consuming roughly 44 cores plus active
  training/search jobs. GPU 1 was idle, but the scalar and CPU-fed renderer
  measurements remained contaminated; frozen floors and the last clean
  179-case performance promotion are unchanged.

## 2026-07-31 (Forge-suppressed snow-layer piston drops)

- Audited the captured Java 1.11.2 piston-DESTROY registry before choosing
  the next payload class. Snow layers expose hidden state not covered by the
  previous zero/one-item cases: Forge's `BlockSnow` state-sensitive drop
  count is layers + 1, but its piston patch passes chance -1 specifically to
  suppress every snowball and mimic vanilla behavior.
- Added supported snow block 78 metadata 3 (four layers). The deliberate
  probe at
  `c/magma/trace/out/redstone_piston_snow_multidrop_probe_1/summary.md`
  has exact shared prestate and passing supported state features. Java
  extends and removes the snow while Magma rejects the two-cell transition.
  Java creates no local EntityItem.
- Added a distinct filtered-candidate payload result. Metadata 0..7 creates
  meta + 2 candidate snowball stacks and consumes that many
  `World.rand.nextFloat()` checks; no spawn, Math.random, entity-capacity, or
  entity-ID work occurs. The metadata-3 native regression locks exactly five
  LCG steps, ending at internal seed `0x90493252C18B`, with all other cursors
  unchanged. The focused oracle fix passes at
  `c/magma/trace/out/redstone_piston_snow_suppressed_drop_fix_1/summary.md`.
- The expanded piston family passes 47/47 at
  `c/magma/trace/out/matrix_redstone_piston_snow_suppressed_drop_family_1/summary.md`.
  The complete matrix passes 192/192 at
  `c/magma/trace/out/matrix_redstone_piston_snow_suppressed_drop_full_1/summary.md`:
  192 state gates, 189 required behavior gates plus three not-required rows,
  and 192 raw-block gates.
- Native runtime and script-route tests, state-capsule selftest, Python/shell
  syntax, and `git diff --check` pass. Dedicated oracle instances 0-3 are
  stopped and `schemas.index` is canonical.
- Performance remains unpromoted at host load 79.77. An unrelated 44-thread
  self-play process still consumes roughly 44 cores and multiple search and
  training jobs are active. GPU 1 is idle, but the scalar and CPU-fed
  renderer gates are not comparable; frozen floors remain unchanged.
- The registry backlog is now ordered by hidden-state risk: deterministic
  single-cell/non-tile drops (mushrooms next), randomized or stateful
  count/metadata drops, liquids/support chains, paired/multiblock teardown,
  and finally tile-bearing teardown once capsule state is exact.

## 2026-07-31 (deterministic brown/red mushroom piston drops)

- Added separate mycelium-supported fixtures for brown mushroom 39:0 and red
  mushroom 40:0. Both share `BlockMushroom`, inherit the default one-item
  `Block.getDrops` path, and remain valid independent of skylight.
- The deliberate pair at
  `c/magma/trace/out/redstone_piston_mushroom_destroy_probe_1/summary.md`
  proves Java emits exact item 39:0 and 40:0 entities respectively, with the
  established RNG-derived state and x=13.635 moving-head sweep. Magma's
  pre-fix divergence is confined to the rejected piston extension and missing
  item state.
- Added only metadata-0 states for IDs 39 and 40 to the spawning payload
  mapper. A native loop locks both registered item IDs, timers, cursors, and
  swept trajectories. Both focused cases pass at
  `c/magma/trace/out/redstone_piston_mushroom_destroy_fix_1/summary.md`.
- The piston family passes 49/49 at
  `c/magma/trace/out/matrix_redstone_piston_mushroom_destroy_family_1/summary.md`.
  The complete matrix passes 194/194 at
  `c/magma/trace/out/matrix_redstone_piston_mushroom_destroy_full_1/summary.md`:
  194 state gates, 191 required behavior gates plus three not-required rows,
  and 194 raw-block gates.
- Native runtime/script tests, capsule selftest, Python/shell syntax, and
  `git diff --check` pass. Oracles 0-3 are stopped and the schema index is
  canonical.
- Host load fell to 28.68 on the 192-CPU machine with GPU 1 idle, making the
  unchanged full guard comparable again. The guard at
  `c/magma/trace/out/perf_guard_redstone_piston_mushroom_destroy_1.json`
  passes every frozen floor: 4,319 scalar steps/s, 2.93M Blaze env-ticks/s,
  and 26.71 1080p CUDA fps. No baseline or threshold changed.
- R-04 proceeds to remaining deterministic attached single-cell drops before
  randomized/stateful count and metadata algorithms.

## 2026-07-31 (attached-ladder piston drop and indirect event boundary)

- Added an east-facing staged-piston fixture whose front ladder 65:5 is
  attached to the piston and whose tick-zero input places a side redstone
  block. Java destroys the ladder, extends, and emits one item 65:0;
  orientation metadata is not retained as item damage. The deliberate engine
  omission is preserved at
  `c/magma/trace/out/redstone_piston_attached_ladder_destroy_probe_1/summary.md`.
- Added ladder metadata 2..5 to the explicit deterministic one-item DESTROY
  payload mapper with output metadata zero. The native regression locks the
  exact item stack, cursor state, and moving-head swept trajectory.
- The first Java-vs-magma run after that engine fix is deliberately retained
  at
  `c/magma/trace/out/redstone_piston_attached_ladder_destroy_fix_1/summary.md`.
  Raw blocks, light, and behavior were exact, but Java created the item with
  EID four past the captured cursor and different RNG-derived pose/motion.
  This isolated an oracle boundary defect rather than an engine rule.
- The locked Java input path restored World.rand, Math.random, and
  `Entity.nextEntityID` immediately before the edit, but drained queued
  piston events only when the edited block itself was ID 29 or 33. This
  fixture edits redstone block 152 beside an existing piston, so the piston
  event waited until later in the server tick and ran after excluded
  world/client work advanced the process-global cursors.
- The bridge now calls the existing block-event drain after every controlled
  tick-boundary edit. An empty queue is a no-op; direct and indirectly queued
  piston events now share the same post-restore input boundary. The focused
  ladder result is exact at
  `c/magma/trace/out/redstone_piston_attached_ladder_destroy_fix_2/summary.md`,
  including item ID/meta, EID, pose, motion, yaw, age, pickup delay, blocks,
  and light.
- The expanded piston family passes 50/50 at
  `c/magma/trace/out/matrix_redstone_piston_ladder_destroy_family_1/summary.md`.
  The complete matrix passes 195/195 at
  `c/magma/trace/out/matrix_redstone_piston_ladder_destroy_full_1/summary.md`:
  195 state gates, 192 required behavior gates plus three not-required rows,
  and 195 raw-block gates.
- `game/test_runtime.sh`, `game/test_script.sh`, state-capsule selftest,
  Python/shell syntax checks, and `git diff --check` pass. Dedicated oracle
  instances 0-3 are stopped and `schemas.index` is canonical.
- With host load 27.68 and GPU 1 idle, the unchanged full performance guard
  at
  `c/magma/trace/out/perf_guard_redstone_piston_ladder_destroy_1.json`
  passes every frozen floor: 4,158 scalar steps/s, 2.92M Blaze env-ticks/s,
  and 27.12 1080p CUDA fps. No baseline or threshold changed.
- R-04 continues through the remaining deterministic attached single-cell
  drops before randomized/stateful count and metadata algorithms.

## 2026-07-31 (deterministic cobweb-to-string piston drop)

- Audited the remaining captured 1.11.2 piston-DESTROY registry and selected
  cobweb as the next smallest distinct class: block 30 is support-independent
  and single-cell, while `BlockWeb.getItemDropped` maps it to registered
  string item 287 rather than the source block ID.
- Added a front-cobweb fixture powered by stable side redstone with the piston
  placed at tick zero. The deliberate probe at
  `c/magma/trace/out/redstone_piston_cobweb_destroy_probe_1/summary.md` has
  exact shared prestate. Java changes the base to 33:13, replaces cobweb 30:0
  with moving head 36:5, and emits one item 287:0; magma's pre-fix result
  leaves the base retracted and cobweb intact.
- Added only metadata-0 cobweb to the explicit deterministic one-item payload
  mapper, changing the drop ID to 287 and retaining damage zero. The native
  regression locks item tuple, entity cursor/timers, and the exact
  moving-head swept trajectory. The focused Java-vs-magma fix passes at
  `c/magma/trace/out/redstone_piston_cobweb_destroy_fix_1/summary.md`.
- The expanded piston family passes 51/51 at
  `c/magma/trace/out/matrix_redstone_piston_cobweb_destroy_family_1/summary.md`.
  The complete matrix passes 196/196 at
  `c/magma/trace/out/matrix_redstone_piston_cobweb_destroy_full_1/summary.md`:
  196 state gates, 193 required behavior gates plus three not-required rows,
  and 196 raw-block gates.
- `game/test_runtime.sh`, `game/test_script.sh`, state-capsule selftest, and
  Python/shell syntax checks pass. Dedicated oracle instances 0-3 are stopped
  and `schemas.index` is canonical.
- At host load 29.40 with GPU 1 idle, the unchanged full performance guard at
  `c/magma/trace/out/perf_guard_redstone_piston_cobweb_destroy_1.json` passes
  every frozen floor: 4,149 scalar steps/s, 2.91M Blaze env-ticks/s, and
  26.24 1080p CUDA fps. No baseline or threshold changed.
- R-04 continues through the remaining deterministic attached/single-cell
  drops before randomized/stateful count and metadata algorithms.

## 2026-07-31 (ordinary/lit pumpkin piston drops and light removal)

- Audited ordinary pumpkin 86 and lit pumpkin 91 in the captured 1.11.2
  source. Both are `BlockPumpkin`, inherit quantity one and damage zero, and
  encode only horizontal facing in metadata 0..3. They are
  support-independent DESTROY states; ID 91 additionally emits block light
  15 before destruction.
- Added separate EAST-facing metadata-3 fixtures. The deliberate pair at
  `c/magma/trace/out/redstone_piston_pumpkin_destroy_probe_1/summary.md` has
  exact shared prestates. Java extends and replaces each source with moving
  head 36:5, emits item 86:0 or 91:0, and removes the lit pumpkin light field;
  magma's pre-fix result leaves each piston retracted and pumpkin intact.
- Added exactly IDs 86/91 metadata 0..3 to the deterministic one-item payload
  mapper, preserving the block/item ID while stripping facing metadata.
  Native tests lock both IDs, damage zero, cursor/timer state, and exact
  moving-head sweep. Both focused Java-vs-magma cases pass at
  `c/magma/trace/out/redstone_piston_pumpkin_destroy_fix_1/summary.md`,
  including complete raw block-light parity for ID 91.
- The expanded piston family passes 53/53 at
  `c/magma/trace/out/matrix_redstone_piston_pumpkin_destroy_family_1/summary.md`.
  The complete matrix passes 198/198 at
  `c/magma/trace/out/matrix_redstone_piston_pumpkin_destroy_full_1/summary.md`:
  198 state gates, 195 required behavior gates plus three not-required rows,
  and 198 raw-block gates.
- At host load 22.50 with GPU 1 idle, the unchanged full performance guard at
  `c/magma/trace/out/perf_guard_redstone_piston_pumpkin_destroy_1.json`
  passes every frozen floor: 4,199 scalar steps/s, 2.91M Blaze env-ticks/s,
  and 26.47 1080p CUDA fps. No baseline or threshold changed.
- R-04 continues through remaining deterministic attached/single-cell drops
  before randomized/stateful count and metadata algorithms.

## 2026-07-31 (structure-void piston destruction with empty drop override)

- Confirmed in captured 1.11.2 source that structure void 217:0 explicitly
  returns DESTROY mobility, has no tile or support dependency, and overrides
  `dropBlockAsItemWithChance` with an empty method. Unlike fire's
  quantity-zero path, no drop list or chance loop is entered.
- Added a direct front-structure-void fixture. The deliberate probe at
  `c/magma/trace/out/redstone_piston_structure_void_destroy_probe_1/summary.md`
  has exact shared prestate and already-passing state parity because Java
  creates no item. Java's only outcomes are base 33:5-to-33:13 and structure
  void 217:0-to-moving-head 36:5; magma's pre-fix result rejects both.
- Added exactly canonical metadata zero to the no-items payload result.
  Native tests prove one moving head, no entity, no drop-RNG/Math.random/
  entity-ID/capacity consumption, and the later common piston-pitch draw. The
  focused exact result passes at
  `c/magma/trace/out/redstone_piston_structure_void_destroy_fix_1/summary.md`.
- The expanded piston family passes 54/54 at
  `c/magma/trace/out/matrix_redstone_piston_structure_void_destroy_family_1/summary.md`.
  The complete matrix passes 199/199 at
  `c/magma/trace/out/matrix_redstone_piston_structure_void_destroy_full_1/summary.md`:
  199 state gates, 196 required behavior gates plus three not-required rows,
  and 199 raw-block gates.
- At host load 44.12 with GPU 1 idle, the unchanged full performance guard at
  `c/magma/trace/out/perf_guard_redstone_piston_structure_void_destroy_1.json`
  passes every frozen floor: 4,287 scalar steps/s, 2.92M Blaze env-ticks/s,
  and 25.12 1080p CUDA fps. The renderer is closer to its floor than recent
  low-load samples but remains above 24.2915; no threshold changed.
- R-04 continues through remaining deterministic attached/single-cell drops
  before randomized/stateful count and metadata algorithms.

## 2026-07-31 (redstone-control piston destruction and item SELF collision)

- Audited captured 1.11.2 source and blockstate metadata for lever 69,
  stone/wood buttons 77/143, stone/wood/weighted pressure plates
  70/72/147/148, redstone torches 75/76, and repeaters 93/94. Default drops
  have quantity one and damage zero; both torch states drop item 76 and both
  repeater states drop item 356.
- Added 11 valid supported front-block fixtures. Their deliberate omission at
  `c/magma/trace/out/redstone_piston_control_destroy_probe_1/summary.md`
  isolates rejected extension and missing items from exact shared prestates.
  The explicit payload mapper and native exhaustive regression cover all 118
  admitted canonical states and reject noncanonical raw metadata.
- Added four five-observation powered-control fixtures whose removal must
  notify an indirect lamp: floor lever, stone pressure plate, lit floor
  redstone torch, and powered repeater. The mixed deliberate failure at
  `c/magma/trace/out/redstone_piston_control_break_notify_probe_1/summary.md`
  preserved two exact cases while isolating torch notification geometry and
  repeater item motion. Block-specific break notification handling makes all
  four exact at
  `c/magma/trace/out/redstone_piston_control_break_notify_fix_2/summary.md`,
  including the exact lamp queue rows `[124],[124],[124],[],[]` and final
  124-to-123 transition.
- The repeater trace exposed the earliest remaining causal difference:
  EntityItem performs ordinary SELF movement before TileEntityPiston swept
  movement. Java's item collided with the half-extended head, zeroed its X
  velocity, and was then swept to x=14.135; magma had treated moving block 36
  as collisionless during SELF. The bounded item path now resolves exact
  Y/X/Z movement against represented moving-piston shapes and normal cubes.
  A native half-extended-head fixture locks exact pose and motion. The new
  arrays and shape scan are skipped entirely when the fixed entity pool has
  no active rows.
- All 15 focused payload/side-effect cases pass at
  `c/magma/trace/out/redstone_piston_control_destroy_bundle_1/summary.md`.
  The piston family candidate passes 69/69 at
  `c/magma/trace/out/matrix_redstone_piston_control_destroy_family_1/summary.md`;
  the full candidate passes 214/214 at
  `c/magma/trace/out/matrix_redstone_piston_control_destroy_full_1/summary.md`:
  214 state gates, 211 required behavior gates plus three not-required rows,
  and 214 raw-block gates.
- `game/test_runtime.sh`, `game/test_script.sh`, capsule selftest,
  Python/shell syntax, and scoped `git diff --check` pass. A no-item
  branch-layout optimization followed the initial aggregate.
- The first exact-source rerun then passed 14/15 focused cases and exposed a
  later item divergence at
  `c/magma/trace/out/redstone_piston_control_destroy_bundle_current_1/summary.md`.
  Java's west-moving lever item reached the settled piston head at tick 3,
  collided with its east plate, stopped at x=14.125, and zeroed X motion;
  magma continued to x=14.1218076551 because its SELF path represented moving
  heads and full cubes but not static ID 34.
- Reused the captured `BlockPistonExtension` two-box plate plus non-SHORT arm
  for all six static-head facings. The active-item-only scan covers the
  shape's 0.25-block neighbor overhang, while the no-item path remains
  unchanged. A native exact-row regression locks x=14.125 and zero X motion.
  The focused correction passes at
  `c/magma/trace/out/redstone_piston_control_settled_head_fix_1/summary.md`.
- The final-source focused bundle passes 15/15 at
  `c/magma/trace/out/redstone_piston_control_destroy_bundle_current_fix_1/summary.md`.
  The piston family passes 69/69 at
  `c/magma/trace/out/matrix_redstone_piston_control_settled_head_family_1/summary.md`,
  and the complete exact-current-source matrix passes 214/214 at
  `c/magma/trace/out/matrix_redstone_piston_control_settled_head_full_1/summary.md`:
  214 state gates, 211 required behavior gates plus three not-required rows,
  and 214 raw-block gates.
- The unchanged performance guard has not yet promoted this slice. Three
  retained attempts passed Blaze at 2.91-2.93M env-ticks/s, but the unchanged
  independent CPU benchmark measured 2,937/3,251/3,126 against its 3,858.9
  floor and CUDA measured 21.57/23.22/22.92 against 24.2915 while an unrelated
  64-core self-play job drove host load near 95. All measured binaries predate
  the slice (2026-07-29), so these samples are environmental controls rather
  than promotion evidence. Floors remain unchanged; rerun on a quiet host.

## 2026-07-31 (dead-bush randomized-count piston drops)

- Selected dead bush 32:0 as the smallest remaining distinct piston DESTROY
  algorithm. Captured `BlockDeadBush` source returns
  `World.rand.nextInt(3)` separate stick 280:0 stacks; every stack then
  consumes its own chance/offset draws, four Math.random doubles, and entity
  ID.
- Added a sand-supported fixture. The deliberate probe at
  `c/magma/trace/out/redstone_piston_dead_bush_destroy_probe_1/summary.md`
  is non-vacuous: Java chooses count two, extends, and creates two exact stick
  entities while magma rejects the extension.
- Added the randomized-count payload without broadening other states. The
  count is drawn before capacity commitment; insufficient fixed-pool capacity
  restores the RNG cursor and rejects the whole extension, preventing partial
  world/entity mutation. Native tests lock count two, both per-stack RNG and
  consecutive EIDs, count zero with no Math.random/entity consumption, and
  atomic rejection with only one free entity slot.
- The focused Java-vs-magma result passes at
  `c/magma/trace/out/redstone_piston_dead_bush_destroy_fix_1/summary.md`,
  including both item entities and raw block/light state. The piston family
  passes 70/70 at
  `c/magma/trace/out/matrix_redstone_piston_dead_bush_family_1/summary.md`.
  The complete exact-current-source matrix passes 215/215 at
  `c/magma/trace/out/matrix_redstone_piston_dead_bush_full_1/summary.md`: 215
  state gates, 212 required behavior gates plus three not-required rows, and
  215 raw-block gates.
- Performance promotion remains pending rather than sampled under host load
  above 90 from the unrelated 64-core self-play job. Frozen floors remain
  unchanged.

## 2026-07-31 (sapling type/stage piston drops)

- Audited captured `BlockSapling`: its 12 canonical states are wood type
  0..5 crossed with stage 0/1. Inherited quantity and item ID remain one and
  6, while `damageDropped` retains type and strips stage bit 8.
- Added supported oak stage-0 (6:0) and dark-oak stage-1 (6:13) fixtures. The
  deliberate pair at
  `c/magma/trace/out/redstone_piston_sapling_destroy_probe_1/summary.md` has
  exact shared prestates and Java items 6:0/6:5 while magma rejects both
  extensions.
- Added only metadata 0..5 and 8..13 to the payload mapper, with output
  metadata `meta & 7`. Native tests exhaust all 12 states and prove raw
  6/7/14/15 remain visible without RNG, entity, piston, or world mutation.
  Both focused oracle cases pass at
  `c/magma/trace/out/redstone_piston_sapling_destroy_fix_1/summary.md`.
- The piston family passes 72/72 at
  `c/magma/trace/out/matrix_redstone_piston_sapling_family_1/summary.md`.
  The complete exact-current-source matrix passes 217/217 at
  `c/magma/trace/out/matrix_redstone_piston_sapling_full_1/summary.md`: 217
  state gates, 214 required behavior gates plus three not-required rows, and
  217 raw-block gates.
- Performance promotion remains pending under the unrelated 64-core host
  workload; frozen floors and the latest clean guard remain unchanged.

## 2026-07-31 (successful-piston sound RNG and 218-case promotion)

- Audited captured `BlockPistonBase.eventReceived` after the sapling slice.
  Every successful extension samples `world.rand.nextFloat()` for sound pitch
  after `doMove` and the extended-base state write. Audio may remain disabled,
  but omitting that draw changes later gameplay RNG.
- Rejected an initial two-tick discriminator after a rerun showed integrated-
  client particles advancing the process-global entity-ID cursor between
  ticks. Replaced it with two opposed staged pistons around one empty center
  cell. One tick-zero redstone placement invokes both in the vanilla
  WEST-then-EAST neighbor order inside one controlled server boundary.
- The deliberate no-pitch-draw result is preserved at
  `c/magma/trace/out/redstone_piston_dual_sound_rng_probe_1/summary.md`: the
  first item and all raw blocks match, while the second item diverges at tick
  zero. Consuming one World.rand float only after a successful extension
  makes the focused case exact at
  `c/magma/trace/out/redstone_piston_dual_sound_rng_fix_1/summary.md`.
- Native regressions lock two same-boundary extensions/items and exact RNG/ID
  cursors, revise zero-item and randomized-count expectations to include the
  final pitch draw, and retain atomic no-draw rejection for failed extension.
  `game/test_runtime.sh`, `game/test_script.sh`, capsule selftest, Python
  syntax, and scoped diff checks pass.
- The piston family passes 73/73 at
  `c/magma/trace/out/matrix_redstone_piston_dual_sound_rng_family_1/summary.md`.
  The complete exact-current-source matrix passes 218/218 at
  `c/magma/trace/out/matrix_redstone_piston_dual_sound_rng_full_1/summary.md`:
  218 state gates, 215 required behavior gates plus three not-required rows,
  and 218 raw-block gates.
- Fixed the local CUDA build default/discovery for the installed RTX PRO 6000
  Blackwell GPUs: `/usr/local/cuda/bin/nvcc`, native `sm_120`, and a guarded
  cudart `-L` flag. The bit-exact CPU/CUDA raster test passes on GPU 1.
- After the unrelated 64-core workload retired, the unchanged guard at
  `c/magma/trace/out/perf_guard_redstone_piston_dual_sound_rng_1.json` passed
  every frozen floor on exact current binaries: 4,222 scalar steps/s versus
  3,858.9, 2.92M Blaze env-ticks/s versus 2.793M, and 26.25 1080p CUDA fps
  versus 24.2915. No threshold or baseline changed.
- Dedicated oracle instances 0-3 are stopped and `schemas.index` is restored
  to its canonical order. R-04 continues with the next randomized/stateful
  DESTROY algorithm.

## 2026-07-31 (process-global block RNG and tall-grass piston drops)

- Audited captured `BlockTallGrass`, `ForgeHooks.getGrassSeed`, and the
  one-entry 1.11.2 grass-seed weight list. Tall grass first consumes
  `Block.RANDOM.nextInt(8)`. A successful seed branch then consumes the
  weight-10 selection and the overridden fortune-0 `nextInt(1)` count draw
  before the ordinary World.rand, Math.random, and entity-ID spawn sequence.
- Extended the authoritative oracle state with the private 48-bit
  `Block.RANDOM` cursor as `world_rng.block_seed48`. The parked oracle can set
  it at the final pre-tick boundary, capsules validate and replay it, and the
  native runtime serializes the same state. This is replay state, not a claim
  that vanilla world NBT persists the process-global JVM cursor.
- Added a dirt-supported tall-grass fixture and controlled both branches.
  Internal seed 0 emits one wheat-seed item 295:0 after three block-RNG draws;
  seed 1396 rejects the 1/8 drop after one draw and creates no item. Native
  tests cover canonical metadata 0..2, invalid raw metadata 3, exact cursors,
  and atomic rollback when the fixed entity pool is full.
- The deliberate negative at
  `c/magma/trace/out/redstone_piston_tall_grass_block_rng_probe_1/summary.md`
  omits only the otherwise invisible `nextInt(1)`. State and raw blocks still
  pass, while the specialized behavior gate fails at tick zero, proving the
  new cursor comparison is non-vacuous.
- Both positive branches pass at
  `c/magma/trace/out/redstone_piston_tall_grass_fix_1/summary.md`. The piston
  family passes 75/75 at
  `c/magma/trace/out/matrix_redstone_piston_tall_grass_family_1/summary.md`.
  The complete exact-current-source matrix passes 220/220 at
  `c/magma/trace/out/matrix_redstone_piston_tall_grass_full_1/summary.md`: all
  220 state and raw-block gates pass, with 217 required behavior gates and
  three not-required rows.
- The unchanged performance guard at
  `c/magma/trace/out/perf_guard_redstone_piston_tall_grass_1.json` passes all
  frozen floors: 4,266 scalar steps/s versus 3,858.9, 2.92M Blaze env-ticks/s
  versus 2.793M, and 24.67 1080p CUDA fps versus 24.2915. No threshold or
  baseline changed.
- Java 8 compilation, native runtime and script tests, capsule selftest,
  Python syntax, and scoped diff checks pass. Oracle instances 0-3 are
  stopped and `schemas.index` is canonical. R-04 next audits randomized
  count/metadata drops, beginning with leaves and crops.

## 2026-07-31 (wheat piston drops, farmland notification, and causal cursor gates)

- Selected wheat as the smallest bounded crop DESTROY algorithm after auditing
  captured 1.11.2 `BlockCrops` source. Ages 0..6 create one wheat-seed 295:0
  stack without a count draw. Mature age 7 creates wheat 296:0, then consumes
  exactly three `World.rand.nextInt(14)` trials; every result at most 7 creates
  a separate seed stack. Internal seed zero produces 0/4/9, hence wheat plus
  two seed entities before the ordinary per-stack chance/offset,
  `Math.random`, entity-ID, and final piston-pitch draws.
- Added farmland-supported mature 59:7 and immature 59:3 fixtures, a parked
  `World.rand` internal-seed setter, exact item-stack/order/EID validation, and
  explicit expected cursor values. The non-vacuous unsupported negative is
  preserved at
  `c/magma/trace/out/redstone_piston_mature_wheat_probe_3/summary.md`: Java
  extends and emits three exact item entities while magma leaves the crop and
  cursor bundle unchanged.
- Implemented all eight canonical wheat ages with capacity preflight and
  atomic RNG rollback. A native metadata-8 control exposed that raw invalid
  block states were still being treated as movable. The live registry export
  now generates a canonical-metadata mask, and piston traversal rejects every
  state whose `getStateFromMeta` failed without partially mutating world,
  entity, piston, or RNG state.
- The first behavior-correct oracle run had exact items and cursor state but a
  one-cell raw mismatch: Java changed farmland 60:0 below the crop to dirt 3:0.
  Primary source showed `BlockFarmland.neighborChanged` performs that change
  when the block above has solid material; moving piston 36 is solid even
  though normal/full-cube is not an equivalent predicate. The registry export
  now also captures `Material.isSolid` for every canonical legacy state. The
  generated artifact digest is
  `65ffa359e5bec3874dce5225f23b9f36e1f54df93150037faa1ba325167d1ebd`,
  pinned by the capsule validator.
- Both focused wheat cases pass at
  `c/magma/trace/out/redstone_piston_wheat_fix_3/summary.md`: 22 matching state
  features, zero divergences, one explicit unsupported death-time subfield,
  exact behavior, and exact 10,625-cell transitions. The mature controlled
  cursor is `0x0D0352014D90`; the immature cursor is `0x5D5692ACE2BF`.
- Enforcing the new cursor feature across the first 222-case aggregate exposed
  11 multi-edit false positives. Java-only loaded-chunk/client work between
  edits changes the next absolute process-global starting cursor, although
  each edit's blocks, light, entities, schedules, and local behavior were
  exact. The diagnostic aggregate is retained at
  `c/magma/trace/out/matrix_redstone_piston_wheat_full_1/summary.md`.
- Replaced the post-edit-only assertion with a bracketing causal checkpoint.
  Equal pre-edit bundles still require complete absolute equality. When later
  pre-edit starts differ, the gate compares exact raw 48-bit LCG transition
  counts for `World.rand`, `Math.random`, and `Block.RANDOM`, entity-ID delta,
  and modulo-32-bit `updateLCG` delta. `game/test_script.sh` includes a negative
  control whose deliberately wrong LCG draw count must diverge. All 11 old
  failures plus both wheat cases pass at
  `c/magma/trace/out/controlled_transition_fix_1/summary.md`.
- The final exact-current-source matrix passes 222/222 at
  `c/magma/trace/out/matrix_redstone_piston_wheat_full_2/summary.md`: all 222
  state gates, 219 required behavior gates plus three not-required rows, and
  all 222 raw-block gates pass. Native runtime/script suites, Python syntax,
  capsule selftest, and scoped whitespace checks pass.
- The unchanged full performance guard at
  `c/magma/trace/out/perf_guard_redstone_piston_wheat_1.json` passes every
  frozen floor: 4,241 scalar steps/s versus 3,858.9, 2.92M Blaze env-ticks/s
  versus 2.793M, and 26.71 1080p CUDA fps versus 24.2915. The new canonical
  mask lookup occurs only while traversing an active piston, the crop branch
  only during active destruction, and causal checkpoint fields are trace-only.
  No threshold or baseline changed.
- Oracle instances 0-3 are stopped and `schemas.index` is restored to its
  canonical order. R-04 next audits leaves, whose sapling/apple chance paths
  and decay/support behavior are broader than wheat.

## 2026-07-31 (leaf piston drops, decay, collision, and 229-case promotion)

- Added four controlled piston DESTROY fixtures covering oak sapling+apple,
  jungle no-drop, dark-oak apple-only, and acacia sapling branches. Exact
  chance/order RNG, item metadata, EIDs, trajectories, capacity rollback, and
  adjacent CHECK_DECAY notifications match Java.
- Added three controlled leaf random-tick fixtures. Magma now uses vanilla's
  9-cube scan, four connectivity rounds, old/new-log support, metadata-bit
  clearing, decay destruction/drops, and neighbor marking. The first fix run
  exposed an earlier item collision against a full-cube leaf ceiling; the
  captured full-cube registry predicate now drives that collision path.
- Focused piston, decay, and combined evidence passes 4/4, 3/3, and 7/7 at
  `c/magma/trace/out/redstone_piston_leaves_fix_1/`,
  `c/magma/trace/out/leaf_decay_fix_2/`, and
  `c/magma/trace/out/leaves_focused_fix_1/`. The piston family passes 81/81,
  and the full exact-current-source matrix passes 229/229 at
  `c/magma/trace/out/matrix_redstone_piston_leaves_{family,full}_1/`.
- Native runtime/player/script tests pass. The unchanged GPU-1 performance
  guard passes at `c/magma/trace/out/perf_guard_redstone_piston_leaves_1.json`:
  4,107 scalar steps/s, 2.92M batched steps/s, and 26.47 1080p CUDA fps.
  Oracle instances are stopped and `schemas.index` is canonical.

## 2026-07-31 (reed support cascade and 230-case promotion)

- Added a valid sand/water-supported two-block reed column. The retained
  pre-fix probe shows Magma rejecting lower 83:7 at the first tick while Java
  extends, emits item 338:0, recursively drops upper 83:11, and emits a second
  exact item. The focused correction matches raw blocks, both entities and
  trajectories, EIDs, World/Math RNG, and piston pitch at
  `c/magma/trace/out/redstone_piston_reed_column_{probe,fix}_1/`.
- The runtime preflights capacity for the complete upward reed chain. Native
  coverage proves one free entity slot rejects a two-drop cascade without
  partial RNG, entity, piston, or world mutation.
- The piston family passes 82/82 and the full aggregate passes 230/230 at
  `c/magma/trace/out/matrix_redstone_piston_reed_column_{family,full}_1/`.
  Runtime/script/capsule tests pass. The unchanged GPU-1 performance guard
  passes at `c/magma/trace/out/perf_guard_redstone_piston_reed_column_1.json`:
  4,105 scalar steps/s, 2.91M batched steps/s, and 26.26 1080p CUDA fps.

## 2026-07-31 (cactus settlement support and 232-case promotion)

- Added a valid supported cactus fixture beside the eventual destination of a
  piston-moved stone. The stable pre-fix probe differs only at tick three:
  Java settles block 36 to stone 1:0, destroys cactus 81:9, and emits item
  81:0; Magma previously retained the cactus. Evidence is at
  `c/magma/trace/out/redstone_piston_cactus_settlement_probe_2/summary.md`.
- Implemented the Java 1.11.2 cactus stability predicate: sand/cactus below,
  no horizontal solid material or lava, and no liquid above. Invalid cactus
  drops metadata zero before the air write, then recursively notifies an
  upward column. Canonical cactus DESTROY payload mapping is included.
- Added extension-only capacity preflight for cactus columns that will become
  invalid when moved solid blocks settle. Native coverage proves that a
  two-cactus cascade with one free entity slot rejects without partial RNG,
  piston, entity, or world mutation. It also locks direct same-boundary drop
  timing and tick-three age-0 settlement timing.
- Separated delayed world behavior from integrated-client cursor noise. The
  edge settlement fixture keeps its emergent item outside the 16-block state
  entity radius while still comparing exact raw blocks and tile timing; a
  nearby controlled solid-neighbor fixture independently proves exact item
  pose, metadata, World/Math RNG, EID, age, and pickup delay. Both pass at
  `c/magma/trace/out/redstone_piston_cactus_fix_2/summary.md`.
- The expanded piston/support family passes 84/84, and the complete current
  source matrix passes 232/232 at
  `c/magma/trace/out/matrix_redstone_piston_cactus_{family,full}_1/`: all 232
  state/raw-block gates, 229 required behavior gates, and three not-required
  rows pass. Runtime/script/capsule tests, Python syntax, and whitespace checks
  pass.
- The idle-host frozen performance guard passes at
  `c/magma/trace/out/perf_guard_redstone_piston_cactus_idle_1.json`: 4,209
  scalar steps/s, 2.91M Blaze GPU steps/s, and 26.55 1080p CUDA fps versus
  unchanged floors of 3,858.9, 2.793M, and 24.2915. Oracle instances 0-3 are
  stopped, the user's interactive Java client remains running, and
  `schemas.index` is restored to canonical order.

## 2026-07-31 (chorus support chain and 241-case promotion)

- Retained clean negative probes for both chorus flower 200 and chorus plant
  199 after end-stone support removal. Java queues support loss at +1 and
  removes the block; Magma previously retained it.
- Implemented exact direct and horizontal survival predicates, scheduled
  support loss, and one-layer-per-tick plant cascade. Flowers emit no item.
  Plants consume `World.rand.nextInt(2)` and emit chorus fruit 432:0 only on
  the zero branch. Direct piston destruction and moved-stone settlement cover
  both paths with exact block state, queue, item, EID, and RNG cursors.
- Focused support, direct-payload, and corrected settlement evidence passes at
  `c/magma/trace/out/chorus_support_fix_1/summary.md`,
  `c/magma/trace/out/chorus_piston_payload_fix_1/summary.md`, and
  `c/magma/trace/out/chorus_flower_settlement_fix_4/summary.md`. Native tests
  also prove full-pool rejection is atomic.
- The expanded four-random-seed aggregate passes 241/241 at
  `c/magma/trace/out/matrix_redstone_piston_chorus_full_1/summary.md`: all 241
  state/raw-block gates, 235 required behavior gates, and six not-required
  rows pass. Runtime/script/capsule tests and scoped whitespace checks pass.
- The idle-host performance guard passes at
  `c/magma/trace/out/perf_guard_redstone_piston_chorus_idle_1.json`: 5,036
  scalar steps/s, 2.93M Blaze GPU steps/s, and 31.27 1080p CUDA fps versus
  unchanged floors of 3,858.9, 2.793M, and 24.2915. The guard used less than
  1 GB RSS for its primary processes; host memory after completion was 14 GB
  used of 916 GB with no swap and no oracle pool clients running.

## 2026-07-31 (double plants, beds, and 248-case promotion)

- Added exact paired teardown fixtures for lower rose, double grass seed and
  no-drop branches, double fern, upper rose, bed foot-first, and bed
  head-first piston destruction. Clean probes preserve the missing pair
  removals and item payloads before the fixes.
- Double plants now remove both halves in notification order. Lower variants
  retain exact item metadata, grass uses the controlled `World.rand.nextInt(8)`
  branch, fern emits nothing, and upper-half destruction delegates payload
  ownership to the lower half. All five focused cases pass at
  `c/magma/trace/out/double_plant_piston_fix_1/summary.md`; the intermediate
  four-seed aggregate passes 246/246 at
  `c/magma/trace/out/matrix_redstone_piston_double_plant_full_1/summary.md`.
- Beds now remove both foot/head cells whether the piston targets the foot or
  head, and emit exactly one bed item 355:0 from the foot-owned callback. Both
  controlled cursor transitions and full-pool atomic rejection are covered by
  native tests and the 2/2 focused pass at
  `c/magma/trace/out/bed_piston_fix_1/summary.md`.
- The complete exact-current-source matrix passes 248/248 at
  `c/magma/trace/out/matrix_redstone_piston_bed_full_1/summary.md`: 248 state
  gates, 242 required behavior gates plus six not-required rows, and 248 raw
  block gates. Runtime/script/capsule tests pass.
- The quiet-host GPU-1 performance guard passes at
  `c/magma/trace/out/perf_guard_redstone_piston_bed_idle_1.json`: 5,066 scalar
  steps/s, 2.93M Blaze GPU steps/s, and 31.13 1080p CUDA fps versus unchanged
  floors of 3,858.9, 2.793M, and 24.2915.

## 2026-07-31 (oracle pool scaling and reset-safe readiness)

- Added `trace/benchmark_oracle_pool.py` and a fixed 64-case workload. The
  benchmark records cases/minute, host CPU, isolated pool CPU, peak host RAM,
  pool RSS, and mean/P95 case latency while enforcing 250/350 GiB warning and
  hard host-memory thresholds.
- The controlled 64/64 scaling result at
  `c/magma/trace/out/oracle_pool_scale_8_16_32_2/benchmark.md` measures 21.23,
  35.38, and 53.68 cases/minute at 8, 16, and 32 clients. Host CPU was 42.4%,
  62.4%, and 78.6%; peak host RAM was 59.4, 82.6, and 140.5 GiB. Thirty-two
  clients deliver 2.53x the eight-client throughput at 63.2% scaling
  efficiency.
- The first 32-client attempt exposed a startup race rather than a parity
  failure: a listening QRL socket preceded a reset-safe integrated world, and
  a reset during Forge startup corrupted that one instance. Pool readiness now
  requires a successful in-world observation plus a five-second continuous
  settling window and relaunches only an unhealthy isolated instance. The
  corrected 32-client run passes 64/64.
- Concurrent missing-client launch and one shared readiness barrier reduce
  startup latency. The full 248-case promotion then completed in 209.437
  seconds, 71.05 cases/minute, compared with 1,232.674 seconds for the prior
  246-case four-client run.
- Idle headless clients remain expensive: the 32-client pool measured about
  105 CPU cores and 111 GiB RSS after the sweep. The operational policy is now
  to launch the 32-client lane on demand and stop exact pool instances in
  parallel afterward. Stopping the pool returned the host to 16 GiB used RAM
  and 98.6% idle CPU without touching non-pool clients.
- `trace/run_oracle_pool_benchmark.sh` places the complete benchmark process
  tree in `netherite-oracle.slice`, with `MemoryHigh=300G` and
  `MemoryMax=350G`, then retires exact pool instances in parallel on exit. The
  parent slice reports the kernel limit as 375,809,638,400 bytes.

## 2026-07-31 (seven door types and 256-case promotion)

- Captured eight clean piston negatives: lower-first teardown for oak 64,
  iron 71, spruce 193, birch 194, jungle 195, acacia 196, and dark-oak 197,
  plus an independent oak upper-first ownership case. Java makes the expected
  piston-base, targeted-half, and paired-half mutations while the old runtime
  leaves every door and piston unchanged. Evidence is retained at
  `c/magma/trace/out/redstone_piston_door_probe_1/summary.md`.
- Implemented the complete canonical metadata domain. Native coverage checks
  all 56 lower states and 28 upper states. Lower halves map to registered item
  IDs 324, 330, and 427..431 with metadata zero; upper halves emit nothing
  directly and remove/drop through the lower callback. Both directions
  preflight fixed-pool capacity before any block, piston, RNG, or entity
  mutation.
- The focused Java-vs-Magma correction passes 8/8 at
  `c/magma/trace/out/redstone_piston_door_fix_1/summary.md`. Exact items, EIDs,
  World/Math RNG cursors, three-cell block transitions, and light all match.
- The complete four-seed matrix passes 256/256 at
  `c/magma/trace/out/matrix_redstone_piston_door_full_1/summary.md`: 256 state
  gates, 250 required behavior gates plus six not-required rows, and 256 raw
  block gates. It completed in 219.355 seconds on the 32-client lane; the
  cgroup peaked at 120,761,757,696 bytes, below its 300/350 GiB limits.
- Runtime/script/capsule/Python/whitespace regressions pass. The quiet-host
  performance guard at
  `c/magma/trace/out/perf_guard_redstone_piston_door_idle_1.json` records 5,080
  scalar steps/s, 2.93M Blaze GPU steps/s, and 31.26 1080p CUDA fps, all above
  unchanged floors. The oracle pool was stopped afterward, returning the host
  to 16 GiB used RAM and 98.7% idle CPU.

## 2026-07-31 (occupied flower-pot teardown and 255-case promotion)

- Added exact Java authority, state-capsule, trace, and raw-runtime coverage
  for flower-pot tile state: position, contained registry item, and metadata.
  Block 140 is registry DESTROY even though it has a tile entity because
  vanilla checks that reaction before its general tile-entity rejection.
- The retained first Java-vs-Magma correction at
  `c/magma/trace/out/redstone_piston_flower_pot_fix_1/summary.md` caught an
  important Forge addition: an occupied pot drops pot item 390:0 first and
  its contained item second. The corrected focused case passes end to end at
  `c/magma/trace/out/redstone_piston_flower_pot_fix_2/summary.md`.
- Magma now keeps flower pots in an allocate-on-use cold pool, restores them
  through the state capsule, preflights both item-entity slots, emits both
  ordered drops with exact IDs and RNG cursors, and retires the tile state.
  Native coverage includes round trip, ordinary block replacement, success,
  one-free-slot rejection, and full-pool atomicity.
- The complete current matrix passes 255/255 at
  `c/magma/trace/out/matrix_redstone_piston_flower_pot_full_1/summary.md`: 255
  state gates, 251 required behavior gates plus four not-required rows, and
  255 raw-block gates. It completed in 202.281 seconds on the 32-client lane;
  the oracle slice reported a 120,761,757,696-byte peak, below its 300/350 GiB
  controls.
- Runtime, capsule, Python, Forge build, and whitespace checks pass. The
  quiet-host guard at
  `c/magma/trace/out/perf_guard_redstone_piston_flower_pot_idle_1.json`
  records 4,162 scalar steps/s, 2.93M Blaze GPU steps/s, and 28.81 1080p CUDA
  fps, all above unchanged floors. All 32 oracle clients were stopped after
  promotion.

## 2026-07-31 (ownerless skull teardown and 256-case promotion)

- Added exact Java authority, capsule, trace, and Magma cold-pool state for
  skull type, rotation, position, and whether a player owner exists. The v1
  capsule accepts all ownerless types 0..5 and rotations 0..15, while
  explicitly rejecting player-profile heads until dropped ItemStack owner NBT
  can be retained exactly.
- Native coverage exercises all 96 ownerless type/rotation combinations. The
  tile type becomes ItemSkull 397 metadata, rotation does not enter the item,
  and successful piston teardown retires the tile with exact entity ID and
  World/Math RNG cursors. Replacing the block and full entity-pool rollback
  are covered separately.
- The focused real-game dragon-head proof passes at
  `c/magma/trace/out/redstone_piston_skull_fix_1/summary.md`: 24 observed state
  features, controlled behavior, raw block transitions, light, item 397:5,
  and tile retirement all match.
- The complete matrix passes 256/256 at
  `c/magma/trace/out/matrix_redstone_piston_skull_full_1/summary.md`: 256 state
  gates, 252 required behavior gates plus four not-required rows, and 256 raw
  block gates. It completed in 206.681 seconds on the 32-client lane. All
  pool instances were stopped afterward, returning the host to about 20 GiB
  used RAM.
- Capsule, Python, shell, native runtime, Forge build, and whitespace checks
  pass. The quiet-host guard at
  `c/magma/trace/out/perf_guard_redstone_piston_skull_idle_1.json` records
  4,369 scalar steps/s, 2.93M Blaze GPU steps/s, and 29.91 1080p CUDA fps, all
  above unchanged floors.

## 2026-07-31 (plain shulker-box teardown and 257-case promotion)

- Added exact Java authority, capsule, trace, and Magma state for closed,
  unnamed, unlocked shulker boxes with plain 27-slot inventories. All 16 block
  colors and six facing metadata values share the exact direct-drop path.
  Nested contained-item NBT, custom names, locks, and loot tables are rejected
  explicitly until their full ItemStack representation exists.
- The drop is the corresponding unstackable colored ItemBlock with its
  `BlockEntityTag`. Unlike ordinary piston-destroyed blocks, shulker
  `breakBlock` calls `spawnAsEntity` directly, so the controlled World.rand
  cursor consumes only three offset floats before the piston sound draw.
  Native coverage exercises all 96 color/facing states, payload retention,
  tile retirement, item-stack limits, exact World/Math RNG, and full entity
  pool rollback.
- The focused real-game purple-box proof passes at
  `c/magma/trace/out/redstone_piston_shulker_box_fix_2/summary.md`: the pre-tick
  tile, post-tick tagged item, entity state, RNG cursors, block transitions,
  and light all match.
- The complete matrix passes 257/257 at
  `c/magma/trace/out/matrix_redstone_piston_shulker_box_full_2/summary.md`: 257
  state gates, 253 required behavior gates plus four not-required rows, and
  257 raw block gates. It completed in 218.740 seconds on the 32-client lane.
  All numbered clients were stopped afterward; the cgroup historical peak
  remains 120,761,757,696 bytes, and the host returned to about 23 GiB used.
- Capsule, Python, shell, native runtime, Forge build, and whitespace checks
  pass. The quiet-host guard at
  `c/magma/trace/out/perf_guard_redstone_piston_shulker_box_idle_1.json`
  records 4,490 scalar steps/s, 2.93M Blaze GPU steps/s, and 29.04 1080p CUDA
  fps, all above unchanged floors.

## 2026-07-31 (signed player-profile skull NBT and 258-case promotion)

- Replaced the player-skull Boolean boundary with lossless standard
  uncompressed NBT. The Java authority now serializes the exact
  `NBTUtil.writeGameProfile` compound, including UUID, name, property lists,
  values, and optional signatures. Generic tagged EntityItems outside the
  existing compact plain-shulker subset expose their complete ItemStack tag.
- Added `trace/nbt_codec.py` as the shared typed semantic codec. It covers all
  twelve vanilla NBT tag types, Java modified UTF-8, raw float/double bits,
  ordered lists, and order-independent compounds. Payloads are bounded to 1
  MiB, depth 64, and 65,536 nodes; a capsule is bounded to 16 MiB total NBT.
  Malformed type, length, UTF, trailing-byte, profile-schema, checksum, and
  semantic-difference controls all fail closed.
- The state capsule writes a length- and SHA-256-checked owner sidecar rather
  than pushing nested or multi-kilobyte data through the flat C script parser.
  C validates and retains the profile in an allocate-on-use cold blob, then
  wraps its compound payload under `SkullOwner` when piston teardown creates
  ItemSkull 397:3. Failed validation and capacity rejection leave the previous
  tile state, RNG cursors, world, and entity pools unchanged.
- Native coverage proves all NBT tag types, independent blob ownership, exact
  wrapper bytes, malformed rejection, profile round-trip, drop creation, tile
  retirement, and controlled RNG/EID transitions. Codec, capsule, semantic
  diff negative control, script route, Python syntax, and JDK 8 Forge build
  gates pass.
- The signed-profile focused real-game proof passes at
  `c/magma/trace/out/redstone_piston_player_skull_fix_2/summary.md`: 24 state
  features, exact tagged ItemStack semantics, behavior, 10,625 raw blocks,
  and 10,625 block-light cells match with zero divergence.
- The 32-client aggregate passes 258/258 in 217.468 seconds at
  `c/magma/trace/out/matrix_redstone_piston_player_skull_full_1/summary.md`:
  258 state gates, 254 required behavior gates plus four not-required rows,
  and 258 raw-block gates pass. All numbered clients were stopped by the
  cleanup trap; the host returned to 22 GiB used RAM with no swap.
- The quiet-host GPU-1 performance guard passes at
  `c/magma/trace/out/perf_guard_redstone_piston_player_skull_idle_1.json`:
  4,808 scalar steps/s, 2.93M Blaze GPU steps/s, and 29.86 1080p CUDA fps
  versus unchanged floors of 3,858.9, 2.793M, and 24.2915. The NBT path has no
  loaded-world or per-tick scan. The next consumer is richer shulker-box state.

## 2026-07-31 (complete closed-shulker NBT and 260-case promotion)

- Extended the bounded exact-NBT transport from signed player skulls to closed
  shulker boxes. The capsule and runtime now retain nested contained-item tags,
  custom names, locks, and deferred loot-table/nonzero-seed state. The dropped
  colored ItemBlock carries the complete `BlockEntityTag`; Java's duplicate
  `display.Name` is preserved, and deferred loot is not materialized during
  piston destruction.
- Added rich-inventory and deferred-loot fixtures plus semantic validators for
  typed slot widths, nested payloads, name duplication, loot/inventory
  exclusivity, checksums, total NBT budget, and malformed/overstack controls.
  The focused real-game result passes 2/2 at
  `c/magma/trace/out/redstone_piston_shulker_nbt_probe_2/summary.md` with 24
  state matches, exact entities/tile retirement/RNG, and 10,625 exact blocks
  and block-light cells per case.
- Added `trace/run_oracle_matrix_pool.sh` as the reusable 32-client lane. The
  matrix now bounds each case at 180 seconds, recycles only an unhealthy
  numbered client, archives the failed attempt, and retries that case once.
  A deliberate one-second timeout control exercised both recycle and retry,
  then produced the expected infrastructure-fail after its second forced
  timeout and stopped the exact client.
  A first aggregate exposed two transient reset timeouts; all four affected
  cases passed on fresh workers. The definitive hardened aggregate passes
  260/260 with no retries in 222.136 seconds at
  `c/magma/trace/out/matrix_redstone_piston_shulker_nbt_full_2/summary.md`: 260
  state gates, 256 required behavior gates plus four not-required rows, and
  260 raw-block gates pass.
- The 32-client pool remained below the historical 120,761,757,696-byte cgroup
  peak, its 300/350 GiB controls, and swap remained unused. Exact cleanup left
  no Java/trace processes and returned the host to 27 GiB used. The controlled
  pool benchmark remains 2.53x faster at 32 than eight clients, while the
  prior full-matrix comparison was about 5.9x faster at 32 than four.
- The quiet-host GPU-1 guard passes at
  `c/magma/trace/out/perf_guard_redstone_piston_shulker_nbt_idle_1.json`:
  4,983 scalar steps/s, 2.93M Blaze env-ticks/s, and 31.15 1080p CUDA fps
  versus unchanged floors of 3,858.9, 2.793M, and 24.2915. The new NBT state
  is allocate-on-use and adds no loaded-world or per-tick scan. R-04 now moves
  to broader piston BLOCK states, collision, retraction, sticky, and slime
  behavior.

## 2026-07-31 (piston BLOCK audit, fluid destruction, and 266-case promotion)

- Audited `BlockPistonBase`, `BlockPistonStructureHelper`, the exact generated
  1.11.2 block registry, and the runtime reaction predicate. Existing code
  already rejected the complete generated BLOCK registry; new real-Java
  controls now prove representative mobility, unbreakable, tile-entity, and
  already-extended-piston paths with an anvil, end portal frame, empty chest,
  and stable powered extended piston fixture.
- The first unsupported registry reaction was fluid DESTROY. Flowing/static
  water and lava IDs 8..11 now take the zero-drop path for all metadata 0..15.
  They extend the piston without allocating an item entity or advancing the
  entity-ID cursor; the existing ordered notifications, moving head/base
  transitions, and one piston-sound World.rand draw remain shared.
- Native regressions cover all 64 fluid states and a full entity pool. Focused
  source-water and source-lava cases pass the real Java oracle with exact
  state, behavior, raw block transitions, block light, entities, and RNG.
- The complete 32-client matrix passes 266/266 with no retries in 220.390
  seconds at
  `c/magma/trace/out/matrix_redstone_piston_fluid_block_full_1/summary.md`:
  266 state gates, 262 required behavior gates plus four not-required rows,
  and 266 raw-block gates pass. This is 72.4 cases/minute of measured matrix
  time. Exact cleanup stopped all numbered clients; the cgroup historical
  peak remains 120,761,757,696 bytes under its 300/350 GiB controls.
- The quiet-host GPU-1 guard passes at
  `c/magma/trace/out/perf_guard_redstone_piston_fluid_block_idle_1.json`:
  4,929 scalar steps/s, 2.93M Blaze env-ticks/s, and 31.66 1080p CUDA fps
  versus unchanged floors of 3,858.9, 2.793M, and 24.2915. The fluid branch is
  reached only for active piston destruction and adds no idle scan or
  allocation. R-04 next takes the remaining registry DESTROY payloads,
  beginning with cake 92.

## 2026-07-31 (cake teardown, comparator handoff, and 269-case promotion)

- Added cake 92 metadata 0..6 to the exact zero-drop piston path. It consumes
  no drop RNG, entity ID, or fixed-pool capacity; metadata 7..15 remains an
  explicit noncanonical rejection. Native coverage exhausts both domains and
  proves successful destruction with a full entity pool.
- Added a three-bite cake/comparator/lamp composition. The saved output 8
  remains powered while cake becomes moving head 36:5, clears at the exact +2
  comparator callback, and hands the lamp its +4 off callback. Moving piston
  36 and settled head 34 are valid zero-strength comparator inputs, including
  a separate saved-head proof.
- Focused whole-cake, circuit, and saved-head Java-vs-Magma cases pass. The
  expanded 32-client matrix passes 269/269 in 237.138 seconds at
  `c/magma/trace/out/matrix_redstone_piston_cake_full_2/summary.md`: 269 state
  and raw-block gates, 265 required behavior gates, and four not-required
  rows. One dead worker was recycled and its affected crop case passed the
  automatic second attempt. All numbered clients stopped afterward.
- GPU-1 performance passes at
  `c/magma/trace/out/perf_guard_redstone_piston_cake_idle_1.json`: 5,031
  scalar steps/s, 2.93M Blaze env-ticks/s, and 31.47 1080p CUDA fps versus
  unchanged floors of 3,858.9, 2.793M, and 24.2915.

## 2026-07-31 (melon randomized drops and 271-case promotion)

- Audited melon block 103 from the generated registry and primary 1.11.2
  sources. Every metadata value is DESTROY. Fortune zero consumes
  `World.rand.nextInt(5)` followed by `nextInt(1)`, then emits three through
  seven separate melon-item 360:0 stacks before the ordinary per-stack
  chance/offset and `Math.random` sequences.
- Controlled internal World seeds zero and one capture the lower and upper
  boundaries. The corrected seven-drop setup keeps the tape/world seed fixed
  and varies only the controlled World.rand cursor; this avoids shifting the
  one-tick piston block-event boundary. Both focused cases pass exactly at
  `c/magma/trace/out/redstone_piston_melon_fix_1/summary.md`, including raw
  blocks, entities, consecutive IDs, World/Math RNG, age, and pickup delay.
- Native coverage exhausts all sixteen metadata values at the three-drop
  cursor, checks the seven-drop cursor, and proves that only six free entity
  slots reject atomically without partial piston, block, RNG, or EID state.
  The implementation adds no per-tick scan or allocation.
- The complete 32-client matrix passes 271/271 with no retries in 214.462
  seconds at
  `c/magma/trace/out/matrix_redstone_piston_melon_full_1/summary.md`: 271 state
  gates, 267 required behavior gates plus four not-required rows, and 271 raw
  block gates. Exact cleanup stopped every numbered client.
- The unchanged GPU-1 guard passes at
  `c/magma/trace/out/perf_guard_redstone_piston_melon_1.json`: 4,948 scalar
  steps/s, 2.93M Blaze env-ticks/s, and 31.13 1080p CUDA fps. The next registry
  gap is pumpkin/melon stem 104/105, whose eight ages use three process-global
  `Block.RANDOM.nextInt(15)` drop trials.

## 2026-07-31 (pumpkin/melon stem drops and 274-case promotion)

- Added pumpkin and melon stems 104/105 after primary-source and generated
  registry audit. Every age 0..7 consumes three process-global
  `Block.RANDOM.nextInt(15)` trials. A result at most the age emits one
  separate pumpkin-seed 361:0 or melon-seed 362:0 stack before ordinary
  chance/offset and Math.random processing.
- Controlled block cursors 1 and 15 prove the age-0 zero-drop and age-7
  three-drop boundaries for both item mappings. The moving head's same-tick
  notification also converts supporting farmland 60:0 to dirt 3:0. Focused
  Java-vs-Magma evidence passes 3/3 at
  `c/magma/trace/out/redstone_piston_stem_fix_1/summary.md` with exact blocks,
  items, EIDs, Block/World/Math RNG, age, and pickup delay.
- Native coverage exhausts both IDs and all valid ages with zero/two/three
  drops, adds a separate one-drop boundary, rejects metadata 8..15 without
  partial state, permits a zero-drop result with a full entity pool, and
  rejects a three-drop result with only two free slots atomically.
- The complete 32-client matrix passes 274/274 in 243.841 seconds at
  `c/magma/trace/out/matrix_redstone_piston_stem_full_1/summary.md`: 274 state
  and raw-block gates, 270 required behavior gates, and four not-required
  rows. One unrelated west-button case lost worker 24, then passed after the
  runner recycled only that client and retried it. Exact cleanup stopped all
  numbered clients.
- GPU-1 performance passes at
  `c/magma/trace/out/perf_guard_redstone_piston_stem_1.json`: 5,103 scalar
  steps/s, 2.94M Blaze env-ticks/s, and 31.55 1080p CUDA fps versus unchanged
  floors. Registry order now moves to zero-drop vine 106.

## 2026-07-31 (vine, waterlily, nether wart, and 278-case promotion)

- Added the next generated-registry DESTROY entries. Vine 106 accepts every
  attachment mask but emits no item under an ordinary piston break. Waterlily
  111 accepts every raw metadata value and emits one normalized item 111:0
  while its source-water support remains unchanged. The deliberate old-C
  probe and corrected focused proof are at
  `c/magma/trace/out/redstone_piston_vine_waterlily_probe_1/summary.md` and
  `c/magma/trace/out/redstone_piston_vine_waterlily_fix_1/summary.md`.
- Nether wart 115 emits one item 372:0 at ages 0..2 and consumes
  `World.rand.nextInt(3)` to emit two through four separate stacks at age 3.
  Controlled age-0 and four-drop Java captures first fail cleanly on old C at
  `c/magma/trace/out/redstone_piston_nether_wart_probe_1/summary.md`, then pass
  2/2 at `c/magma/trace/out/redstone_piston_nether_wart_fix_1/summary.md` with
  exact blocks, items, EIDs, World/Math RNG, age, and pickup delay.
- Native coverage exhausts all sixteen vine and waterlily raw metadata values,
  all four wart ages, invalid wart metadata 4..15, both mature wart count
  boundaries, full-pool zero-drop success, and atomic insufficient-capacity
  rejection for waterlily and four-drop wart paths.
- The complete 32-client matrix passes 278/278 in 244.461 seconds at
  `c/magma/trace/out/matrix_redstone_piston_vine_waterlily_wart_full_1/summary.md`:
  278 state and raw-block gates, 274 required behavior gates, and four
  not-required rows. One unrelated `lava_source_dispatch_seed_0` worker timed
  out; the runner recycled the isolated client and the case passed its one
  automatic retry. Exact cleanup stopped all numbered clients.
- GPU-1 performance passes at
  `c/magma/trace/out/perf_guard_redstone_piston_vine_waterlily_wart_1.json`:
  4,738 scalar steps/s, 2.93M Blaze env-ticks/s, and 29.33 1080p CUDA fps
  versus unchanged floors. Registry order now advances past nether wart 115.

## 2026-07-31 (dragon egg, cocoa, and 281-case promotion)

- Added dragon egg 122 and cocoa 127 piston DESTROY payloads. Dragon egg emits
  one normalized item 122:0. Cocoa emits one brown dye 351:3 at ages 0/1 and
  three separate dyes at age 2. Focused Java-vs-Magma evidence passes 1/1 and
  2/2 with exact blocks, item stacks, RNG cursors, EIDs, and pickup state.
- Native coverage exhausts canonical metadata and proves atomic rejection for
  insufficient fixed item capacity, including the mature three-stack branch.
- The first aggregate exposed an existing chorus scheduled-callback RNG race:
  the tick-boundary block edit could overwrite the callback fixture cursor.
  The fixture is now applied after block mutation. A new `--repeat` matrix
  option dispatched the case across 16 independent clients and passed 16/16.
- The definitive 32-client matrix passes 281/281 with no retries in 220.296
  seconds at
  `c/magma/trace/out/matrix_redstone_piston_cocoa_full_2/summary.md`: 281 state
  and raw-block gates, 277 required behavior gates, and four not-required rows.
- GPU-1 performance passes at
  `c/magma/trace/out/perf_guard_redstone_piston_cocoa_1.json`: 5,133 scalar
  steps/s, 2.93M Blaze env-ticks/s, and 31.84 1080p CUDA fps versus unchanged
  floors. Registry order now advances to tripwire hook 131.

## 2026-08-01 (tripwire activation and 287-case promotion)

- Added exact tripwire hook 131 and wire 132 piston DESTROY payloads, including
  attached-line teardown and the hook's delayed consistency recheck.
- Added a deterministic attached-line fixture with a stationary dropped item.
  Java powers the contacted wire, both hooks, and both lamps, then retains the
  exact hook/wire scheduled-callback order while the item remains. The old C
  probe fails on exactly those five raw cells; the corrected focused proof
  passes with exact raw blocks, block light, scheduled work, and entity state.
- Hook weak/strong power now participates in dust, repeater, comparator,
  piston, and ordinary neighbor power queries. Tripwire collision dispatch is
  tied to represented entity movement and scheduled callbacks, with no idle
  world scan. Hook and wire light opacity is also exact.
- Native coverage proves initial hook scheduling, item activation, callback
  draining/rescheduling, powered metadata, lamps, and light. The six-case
  tripwire family passes 6/6 at
  `c/magma/trace/out/redstone_tripwire_family_fix_1/summary.md`.
- The definitive 32-client matrix passes 287/287 with no retries in 230.676
  seconds at `c/magma/trace/out/matrix_redstone_tripwire_full_1/summary.md`:
  287 state and raw-block gates, 283 required behavior gates, and four
  not-required rows.
- GPU-1 performance passes at
  `c/magma/trace/out/perf_guard_redstone_tripwire_1.json`: 5,020 scalar
  steps/s, 2.93M Blaze env-ticks/s, and 28.82 1080p CUDA fps versus unchanged
  floors. Registry-order piston work now advances beyond tripwire 132; entity
  exit/release and non-item Java trigger coverage remain explicit gaps.

## 2026-08-01 (tripwire player crossing/release and 288-case promotion)

- Added an exact moving-player fixture that walks across a three-wire line and
  leaves its collision bounds. The deliberate old-C probe exposed tripwire as
  a full solid block; canonical hook/wire block properties now make player,
  item, and mob collision treat both thin blocks as non-solid.
- Matched the Java lifecycle through line attachment, powered hooks/wire and
  lamps, occupied-wire scheduling, player exit, +10 release, and the remaining
  attached/unpowered metadata. Hook power is now accepted by the adjacent lamp
  path, and attachment updates include the wire that initiated recalculation.
- Native runtime coverage proves attach, occupied reschedule, release, lamp
  drain, and the empty final callback queue. The focused tripwire family passes
  7/7 at
  `c/magma/trace/out/redstone_tripwire_family_release_fix_1/summary.md`.
- The clean aggregate passes 288/288 with no retries in 230.123 seconds at
  `c/magma/trace/out/matrix_redstone_tripwire_release_full_2/summary.md`: 288
  state and raw-block gates, 284 required behavior gates, and four
  not-required rows.
- GPU-1 performance passes at
  `c/magma/trace/out/perf_guard_redstone_tripwire_release_1.json`: 4,991 scalar
  steps/s, 2.93M Blaze env-ticks/s, and 30.54 1080p CUDA fps versus unchanged
  floors. Additional non-item tripwire triggers remain explicit oracle work.

## 2026-08-01 (carrot/potato piston drops and 291-case promotion)

- Advanced registry-order piston DESTROY behavior through carrot 141 and
  potato 142. All crop ages retain the correct item identity; mature crops
  consume the exact three `World.rand.nextInt(14)` bonus-stack trials, and
  mature potato additionally consumes `Block.RANDOM.nextInt(50)` before item
  spawning to append poisonous potato 394 when selected.
- Added mature-carrot, immature-potato, and poisonous mature-potato Java
  fixtures. The deliberate old-C probe rejects all three at tick zero at
  `c/magma/c/magma/trace/out/redstone_piston_carrot_potato_probe_1/summary.md`;
  the corrected focused result passes 3/3 at
  `c/magma/trace/out/redstone_piston_carrot_potato_fix_1/summary.md`.
- Native coverage exhausts all 16 canonical age states, rejects invalid
  metadata, proves poison selection and rejection, and rolls back both RNG
  cursors atomically when four item stacks do not fit. Existing wheat plus the
  new crops pass 5/5 at
  `c/magma/trace/out/redstone_piston_crop_family_fix_1/summary.md`.
- The full aggregate passes 291/291 with no retries in 257.080 seconds at
  `c/magma/trace/out/matrix_redstone_piston_carrot_potato_full_1/summary.md`:
  291 state and raw-block gates, 287 required behavior gates, and four
  not-required rows.
- GPU-1 performance passes at
  `c/magma/trace/out/perf_guard_redstone_piston_carrot_potato_1.json`: 4,513
  scalar steps/s, 2.93M Blaze env-ticks/s, and 25.34 1080p CUDA fps versus
  unchanged floors. Crop drop work runs only during an active piston event.

## 2026-08-01 (comparator piston destruction and 294-case promotion)

- Added piston DESTROY payloads for comparator blocks 149/150 across all 32
  canonical block states. Each emits item 404:0 and retires its exact saved
  comparator tile.
- Added unpowered, powered-output-lamp, and transient saved block-150 Java
  fixtures. The corrected focused suite passes 3/3 at
  `c/magma/trace/out/redstone_piston_comparator_destroy_fix_4/summary.md`; the
  existing and new comparator/piston family passes 5/5 at
  `c/magma/trace/out/redstone_piston_comparator_family_fix_1/summary.md`.
- Matched break notification order: powered output teardown creates the exact
  +4 lamp release, while piston placement can enqueue block 150's +2
  self-correction before destruction and leave its stale callback ordered until
  due. Native tests also prove tile retirement and fixed-pool rejection.
- One 293/294 aggregate had a single unrelated random-walk `on_ground` capture
  flake; it passed 3/3 on isolated fresh clients. The unchanged clean rerun
  passes 294/294 in 267.206 seconds at
  `c/magma/trace/out/matrix_redstone_piston_comparator_destroy_full_2/summary.md`:
  294 state and raw-block gates, 290 required behavior gates, and four
  not-required rows.
- GPU-1 metrics pass at 2.93M Blaze env-ticks/s and 28.15 1080p CUDA fps. An
  initial unpinned scalar sample was contaminated by an unrelated 48-core host
  workload; the idle-core confirmation passes at 4,122 steps/s against the
  unchanged 3,858.9 floor. Comparator work is event-only and adds no idle scan
  or allocation.

## 2026-08-01 (beetroot piston destruction and 296-case promotion)

- Added beetroot block 207 piston DESTROY behavior after primary-source and
  generated-registry audit. Ages 0..2 emit one seed 435:0. Mature age 3 emits
  beetroot 434:0, consumes three `World.rand.nextInt(6)` bonus trials, and
  appends the selected seed stacks before ordinary item-spawn RNG.
- The deliberate old-C comparison fails both fixtures at tick zero at
  `c/magma/trace/out/redstone_piston_beetroot_probe_1/summary.md`. The focused
  corrected proof passes 2/2 at
  `c/magma/trace/out/redstone_piston_beetroot_fix_1/summary.md`, and the shared
  wheat/carrot/potato/beetroot family passes 7/7 at
  `c/magma/trace/out/redstone_piston_crop_family_beetroot_fix_1/summary.md`.
- Native runtime coverage exhausts the four valid ages, rejects metadata 4,
  checks exact item/EID/World/Math/Block RNG state, verifies farmland-to-dirt
  notification, and rejects a three-stack mature result atomically when only
  two entity slots remain.
- The full 32-client matrix passes 296/296 in 345.746 seconds at
  `c/magma/trace/out/matrix_redstone_piston_beetroot_full_1/summary.md`: 296
  state and raw-block gates, 292 required behavior gates, and four
  not-required rows. One unrelated pumpkin-stem oracle job timed out, then
  passed after its isolated client was recycled.
- GPU-1 guards pass at 2.93M Blaze env-ticks/s and 28.59 1080p CUDA fps. With
  unrelated host load above 200, scalar affinity samples measured 3,735 and
  3,844 steps/s against the unchanged 3,858.9 floor and are retained as
  non-passes. The preceding quiet-host scalar result is 4,122 steps/s.
  Beetroot adds no idle scan or allocation and is reached only during an
  active piston DESTROY event.
- The generated registry inventory after beetroot contains only structure
  void 217 and shulker boxes 219..234, already implemented and covered. The
  registry-order DESTROY audit is therefore complete through 234; remaining
  piston work moves to collision/pushing, retraction/sticky behavior, slime
  structures, moving-tile save/reload, and rendering.

## 2026-08-01 (ordinary piston retraction and 299-case coverage)

- Added the first settled normal-piston retraction lifecycle. Removing power
  from an EAST-facing extended base replaces it with source moving block 36:5,
  removes its ordinary head, advances moving progress 0.5 then 1.0, and
  settles unextended base 33:5 on the third tile tick.
- Matched the contraction sound's causal RNG side effect exactly:
  `World.rand.nextFloat()` advances controlled seed 0 to 11 while the entity
  cursor, Math.random, Block.RANDOM, and scheduled queue remain unchanged.
- The deliberate old-C case fails at
  `c/magma/trace/out/redstone_piston_empty_retraction_probe_1/summary.md`. The
  start/progress/settled correction passes 3/3 at
  `c/magma/trace/out/redstone_piston_empty_retraction_fix_1/summary.md`, and
  the combined extension/retraction family passes 6/6 at
  `c/magma/trace/out/redstone_piston_extension_retraction_family_fix_1/summary.md`.
- Native coverage checks the exact moving-tile fields, every progress boundary,
  head removal, settlement, and RNG cursor. The existing full native runtime
  suite remains green.
- The 299-case current-source aggregate passes 298 rows in 269.569 seconds at
  `c/magma/trace/out/matrix_redstone_piston_empty_retraction_full_1/summary.md`.
  Its sole failure is the known `random_seed_1` landing capture at tick 16;
  three fresh clients pass 3/3 at
  `c/magma/trace/out/random_seed_1_retraction_isolated_remeasure_1/summary.md`.
- GPU-1 guards pass at 2.94M Blaze env-ticks/s and 24.84 1080p CUDA fps.
  Scalar samples under elevated host load measured 3,725 and 3,665 steps/s
  against the unchanged 3,858.9 floor and remain recorded as non-passes.
  Retraction begins only on a neighbor edit and reuses the bounded moving-tile
  tick, adding no idle scan or allocation.

## 2026-08-01 (sticky one-stone lifecycle and clean 305-case promotion)

- Added exact EAST-facing sticky-piston extension and one-stone pull behavior.
  Extension uses moving sticky-head metadata 13 while the moving stone retains
  facing metadata 5; retraction creates source and pulled-stone tiles in Java
  order and settles base 29:5 plus stone in the former head cell.
- The deliberate old-C pull proof fails at
  `c/magma/trace/out/redstone_sticky_piston_one_stone_pull_probe_1/summary.md`.
  Six start/progress/settled correction cases pass at
  `c/magma/trace/out/redstone_sticky_piston_one_stone_fix_1/summary.md`, and the
  normal/sticky lifecycle family passes 12/12 at
  `c/magma/trace/out/redstone_piston_normal_sticky_lifecycle_family_1/summary.md`.
- Native coverage checks exact moving-tile type, facing, source/extending
  flags, insertion order, every progress boundary, final blocks, and RNG
  cursors for a complete extension/retraction cycle.
- The full 32-client aggregate passes 305/305 with no retries in 268.544
  seconds at
  `c/magma/trace/out/matrix_redstone_sticky_piston_one_stone_full_1/summary.md`:
  305 exact state and raw-block gates, 301 required behavior gates, and four
  not-required rows.
- GPU-1 guards pass at 2.93M Blaze env-ticks/s and 28.08 1080p CUDA fps. A
  scalar sample under elevated host load measured 3,650 steps/s against the
  unchanged 3,858.9 floor and remains a non-pass; the preceding clean sample
  is 4,122 steps/s. Sticky work is event-driven and adds no idle scan or
  allocation.

## 2026-08-01 (piston minimum pulses and 307-case composite promotion)

- Added exact one-observation minimum-pulse reversal for EAST-facing normal
  and sticky one-stone extensions. Power loss force-settles the extending
  head; normal moved blocks finish their motion, while a still-extending
  sticky target is settled and deliberately not pulled back.
- The deliberate old-C proof fails at
  `c/magma/trace/out/redstone_sticky_piston_minimum_pulse_probe_1/summary.md`.
  The corrected normal/sticky pair passes 2/2 at
  `c/magma/trace/out/redstone_piston_minimum_pulse_normal_sticky_fix_1/summary.md`,
  and the lifecycle plus observer family passes 15/15 at
  `c/magma/trace/out/redstone_piston_minimum_pulse_lifecycle_family_1/summary.md`.
- Native coverage checks the exact in-flight tile sets, forced settlement,
  source retraction, observer pulse, two sound RNG draws, and final blocks.
- The first full aggregate passes 306/307 in 269.863 seconds at
  `c/magma/trace/out/matrix_redstone_piston_minimum_pulse_full_1/summary.md`;
  its only scheduled-fire capture miss passes 3/3 on fresh clients. The second
  passes that row and 305 others in 299.716 seconds at
  `c/magma/trace/out/matrix_redstone_piston_minimum_pulse_full_2/summary.md`;
  its only random-walk landing miss passes 3/3, and its unrelated
  infrastructure retry passes. The composite covers all 307 current cases,
  303 required behavior gates, and four not-required rows.
- GPU-1 guards pass at 2.93M Blaze env-ticks/s and 28.56 1080p CUDA fps
  against unchanged floors. Scalar measurement is deferred under an unrelated
  48-core host workload; the last clean sample remains 4,122 steps/s. The new
  branch has no idle scan or allocation.

## 2026-08-01 (slime piston structures and clean 311-case promotion)

- Ported the bounded `BlockPistonStructureHelper` move-list insertion,
  collision reorder, and slime side-branch traversal needed by an EAST piston
  with one attached UP stone. Extension consumes the Java list in reverse and
  creates side-stone, slime, then head moving tiles.
- Generalized the same helper for sticky retraction. The old head is hidden
  during preflight, the base moving tile is created first, and the attached
  stone and slime follow in exact reverse structure order. Failed oversized
  preflight still retracts only the base, matching Java event handling.
- Deliberate old-C probes isolate the missing side-block movement in extension
  and pull. Four corrected Java cases cover start and settlement. The affected
  normal/sticky/minimum-pulse family passes 18/18 at
  `c/magma/trace/out/redstone_piston_slime_structure_family_1/summary.md`.
- Native tests cover exact moving-tile order and fields, shared progress and
  settlement, the 12-block aggregate limit, base-only oversized sticky
  retraction, and immovable side-obsidian exclusion. The ordinary non-slime
  straight-line path remains unchanged.
- The full 32-client aggregate passes 311/311 in 285.943 seconds at
  `c/magma/trace/out/matrix_redstone_piston_slime_structure_full_1/summary.md`:
  311 exact state and raw-block gates, 307 required behavior gates, and four
  not-required rows. One unrelated downward-water client timed out and passed
  on the automatic fresh-client retry.
- GPU-1 guards pass at 2.93M Blaze env-ticks/s and 27.48 1080p CUDA fps against
  unchanged floors. Scalar timing remains deferred under the unrelated
  48-core host workload; the last clean sample is 4,122 steps/s. Slime work
  uses fixed 12-move/48-destroy arrays and runs only during an active piston
  event, with no heap allocation or idle world scan.

## 2026-08-01 (slime terminal destroys and 315-case composite promotion)

- Added the first terminal DESTROY behavior to bounded slime structures for
  both normal extension and sticky pull. Destruction runs in Java's reverse
  order before reverse movement and preserves exact block, drop, World/Math
  RNG, entity-ID, and moving-tile outcomes.
- The deliberate old-C cobweb probes fail 2/2. Four corrected start/settled
  cases pass 4/4 at
  `c/magma/trace/out/redstone_piston_slime_cobweb_destroy_promoted_1/summary.md`,
  and the expanded piston lifecycle family passes 23/23 at
  `c/magma/trace/out/redstone_piston_slime_destroy_family_1/summary.md`.
- Native coverage includes extension and pull, two terminal destroy branches
  with exact ordering, and an insufficient-capacity negative that proves no
  partial piston, drop, RNG, or entity-ID mutation occurs.
- The full 32-client aggregate passes 313/315 in 264.611 seconds at
  `c/magma/trace/out/matrix_redstone_piston_slime_destroy_full_1/summary.md`.
  The unrelated random-walk and trapped-chest capture misses each pass 3/3 on
  isolated fresh clients, so the composite covers all 315 cases: 311 required
  behavior gates plus four not-required rows.
- GPU-1 guards pass at 2.94M Blaze env-ticks/s and 25.12 1080p CUDA fps against
  unchanged floors. Scalar timing remains deferred under the unrelated
  48-core host workload; the last clean sample remains 4,122 steps/s. The new
  path uses existing fixed 12-move/48-destroy storage only during a piston
  event. Paired, cascading, stateful, and randomized terminal payloads remain
  explicitly rejected and open.

## 2026-08-01 (slime bed pairs and clean 323-case promotion)

- Added canonical bed-pair terminal destruction to bounded slime structures
  for normal extension and sticky pull. Foot targets emit the bed directly;
  head targets emit nothing until the ordered notification removes the foot,
  which owns the single item.
- The deliberate probe has four expected foot-target failures and four
  capacity-available head passes at
  `c/magma/trace/out/redstone_piston_slime_bed_pair_probe_1/summary.md`. All
  eight corrected start/settled cases pass at
  `c/magma/trace/out/redstone_piston_slime_bed_pair_fix_1/summary.md`.
- Preflight accounts for a direct foot drop or a deferred foot drop before any
  piston mutation. Native tests cover extension, sticky pull, settlement, and
  a full-pool head negative with unchanged blocks, RNG, entity ID, and piston
  state.
- The affected family includes the previous piston/slime cases and both
  ordinary-bed controls and passes 33/33 at
  `c/magma/trace/out/redstone_piston_slime_bed_pair_family_1/summary.md`. One
  old case needed the built-in fresh-client infrastructure retry.
- The full 32-client aggregate passes 323/323 with no retry in 294.370 seconds
  at
  `c/magma/trace/out/matrix_redstone_piston_slime_bed_pair_full_1/summary.md`:
  319 required behavior gates plus four not-required rows.
- GPU-1 guards pass at 2.93M Blaze env-ticks/s and 25.06 1080p CUDA fps against
  unchanged floors. Scalar timing remains deferred under the unrelated
  high-core workload; the last clean result is 4,122 steps/s. The new checks
  run only during active bounded piston preflight and allocate nothing.

## 2026-08-01 (slime door pairs and clean 331-case promotion)

- Added canonical door-pair terminal destruction to bounded slime structures
  for normal extension and sticky pull. A lower target supplies its direct
  door item; an upper target defers the one item to its lower mate during the
  ordered notification pass.
- The deliberate old-C probe fails four lower-target cases at admission and
  passes four capacity-available upper-target cases, exposing the latter's
  missing capacity reservation. All eight corrected start/settled cases pass
  at
  `c/magma/trace/out/redstone_piston_slime_door_pair_fix_1/summary.md`.
- Native tests exhaust all seven door block/item mappings across 56 canonical
  lower states and 28 canonical upper states. A full-pool upper-target test
  proves rejection is atomic across blocks, RNG, entity ID, and piston state.
- The affected piston/slime family, including eight ordinary-door controls,
  passes 49/49 with no retry at
  `c/magma/trace/out/redstone_piston_slime_door_pair_family_1/summary.md`.
- The full 32-client aggregate passes 331/331 with no retry in 295.020 seconds
  at
  `c/magma/trace/out/matrix_redstone_piston_slime_door_pair_full_1/summary.md`:
  327 required behavior gates plus four not-required rows.
- GPU-1 guards pass at 2.93M Blaze env-ticks/s and 24.48 1080p CUDA fps against
  unchanged floors. Scalar timing remains deferred under the unrelated
  high-core workload; the last clean result is 4,122 steps/s. Door admission
  is bounded to an active piston preflight and allocates nothing.

## 2026-08-01 (slime double plants and 339-case composite promotion)

- Added paired teardown for deterministic double plants on normal slime
  extension and sticky slime pull. Lower halves own the variant item; upper
  halves defer that item to an otherwise untouched lower mate during ordered
  notification. Double ferns correctly produce no item.
- The deliberate old-C probe records four lower-target failures and four
  capacity-unaware upper-target passes. All eight corrected double-rose
  start/settled cases pass at
  `c/magma/trace/out/redstone_piston_slime_double_rose_pair_fix_1/summary.md`.
- Native coverage exhausts 40 canonical lower/upper state combinations across
  sunflowers, lilacs, double ferns, double roses, and peonies. It also proves
  full-pool upper rejection is atomic, exercises sticky pull, and keeps
  randomized double grass explicitly rejected until its multi-entry shadow-RNG
  preflight is exact.
- The affected piston/slime family, including all five ordinary double-plant
  controls, passes 62/62 at
  `c/magma/trace/out/redstone_piston_slime_double_rose_pair_family_1/summary.md`.
- The full aggregate passes 338/339 in 285.933 seconds at
  `c/magma/trace/out/matrix_redstone_piston_slime_double_rose_pair_full_1/summary.md`.
  Its unrelated random-walk capture miss passes 3/3 on a fresh isolated client
  at `c/magma/trace/out/slime_double_rose_unrelated_random_remeasure_1/summary.md`,
  so the composite covers all 339 cases: 335 required behavior gates plus four
  not-required rows.
- GPU-1 guards pass at 2.93M Blaze env-ticks/s and 27.98 1080p CUDA fps against
  unchanged floors. Scalar timing remains deferred under the unrelated
  high-core workload; the last clean result is 4,122 steps/s. The new branch
  runs only in active bounded piston preflight and allocates nothing.

## 2026-08-01 (random double-grass pairs and 349-case composite promotion)

- Added exact randomized double-grass pair teardown for normal slime
  extension and sticky slime pull. Direct lower halves roll during reverse
  destruction; upper targets defer the roll to their lower mate during the
  later reverse notification phase.
- Added a mixed two-terminal fixture that performs one roll in each phase.
  Seed zero selects both wheat-seed drops, proving exact World/Math RNG,
  entity-ID, item state, and ten-cell mutation outcomes. The deliberate old-C
  probe fails 10/10; the corrected gate passes 10/10 at
  `c/magma/trace/out/redstone_piston_slime_double_grass_pair_fix_1/summary.md`.
- Preflight uses a local 48-bit RNG cursor and two bounded reverse passes. It
  predicts exact capacity without mutating blocks, RNG, or entities. Native
  tests prove no-drop admission with a full pool, selected-drop atomic
  rejection, mixed direct/deferred order, and two-drop rejection with one free
  slot.
- The affected piston/slime family passes 72/72 at
  `c/magma/trace/out/redstone_piston_slime_double_grass_pair_family_1/summary.md`.
  One preexisting sticky-pull progress capture needed the built-in fresh-client
  infrastructure retry and then passed.
- The full aggregate passes 348/349 in 301.386 seconds at
  `c/magma/trace/out/matrix_redstone_piston_slime_double_grass_pair_full_1/summary.md`.
  Its unrelated random-walk capture miss passes 3/3 on a fresh isolated client
  at `c/magma/trace/out/slime_double_grass_unrelated_random_remeasure_1/summary.md`,
  so the composite covers all 349 cases: 345 required behavior gates plus four
  not-required rows.
- GPU-1 guards pass at 2.93M Blaze env-ticks/s and 27.48 1080p CUDA fps against
  unchanged floors. Scalar timing remains deferred under the unrelated
  high-core workload; the last clean result is 4,122 steps/s. The new work is
  active-event-only, fixed-array, and allocation-free.

## 2026-08-01 (slime reed/cactus columns and 365-case promotion)

- Added lower- and middle-cell three-high reed fixtures for normal slime
  extension and sticky slime pull. Java drops the direct target first, then
  recursively drops each still-present reed above it during the later ordered
  notification pass. Middle removal leaves the supported lower cell intact.
- Rejected the first cactus fixture design as invalid evidence: a cactus that
  already touches the terminal side stone destroys itself during Java fixture
  staging. The corrected fixtures begin with the stone diagonal to a valid
  column. Block 36 remains non-solid during motion, then settled stone
  invalidates the lower or middle cactus on the third observation.
- Slime preflight now admits reed terminals, shadows every reachable upward
  drop through exact World.rand transitions, and reserves the fixed entity
  pool before mutation. The existing cactus settlement counter now accepts
  arbitrary bounded move origins, so slime extension and pull share the same
  atomic capacity policy as straight pistons. All work is fixed-array,
  allocation-free, and restricted to an active piston event.
- Native tests prove lower three-drop order and cursors, middle two-drop lower
  retention on sticky pull, two-free-slot reed rejection, successful
  three-high cactus settlement, insufficient-capacity extension rejection,
  capacity-limited sticky retraction that settles only the base, and one
  combined structure whose three direct reed plus three delayed cactus drops
  reject four free slots before mutation. The full runtime component suite
  passes.
- The deliberate valid-world probe records all eight reed rows failing before
  the fix. The corrected 16-case gate passes every behavior/raw outcome after
  the final combined-capacity fix at
  `c/magma/trace/out/redstone_piston_slime_reed_cactus_column_final_1/summary.md`.
  The affected family covers 88/88 at
  `c/magma/trace/out/redstone_piston_slime_reed_cactus_column_family_1/summary.md`.
- The complete final-source 32-client matrix covers all 365 behavior and exact
  raw-block outcomes in 307.943 seconds at
  `c/magma/trace/out/matrix_redstone_piston_slime_reed_cactus_column_final_1/summary.md`:
  361 required behavior gates plus four not-required rows. It has 361 strict
  full-state passes. Four required third-tick cactus settlement rows keep
  randomized trajectories diagnostic because Java resumes ambient
  loaded-chunk RNG/EID work after the controlled input; exact start state,
  item type/count/age/pickup, and raw volume still pass.
- Final-source GPU-1 guards pass unchanged floors at 2.93M Blaze env-ticks/s and 25.8
  1080p CUDA fps. Scalar timing remains deferred under the unrelated high-core
  workload; the last clean result is 4,122 steps/s.

## 2026-08-01 (sticky retraction boundaries and 385-case promotion)

- Added start/settled Java fixtures for empty sticky retraction in all six
  facings, EAST immovable obsidian, headless sticky and normal bases, and a
  headless sticky base whose unrelated front stone and obsidian pull target
  must both survive. The initial DOWN fixture was invalid because its target
  occupied the flat-world stone layer; the strict mutation validator caught
  it, and the corrected elevated fixture passes old C.
- The corrected deliberate old-C probe isolates the headless sticky start and
  settled rows failing at
  `c/magma/trace/out/redstone_sticky_piston_retraction_boundary_probe_2/summary.md`.
  Java's queued retraction event does not require the extended base's head to
  remain present; magma's early matching-head return was the first divergence.
- Retraction now admits a missing head. Front clearing follows the Java
  branches: normal pistons always clear it, admitted sticky pulls clear it,
  and a matching settled head removes itself when the base becomes block 36.
  A base-only sticky event preserves unrelated front content. The change is
  event-only, fixed-state, and allocation-free.
- Native coverage proves base-only motion and settlement in all six facings,
  exact contraction RNG, unchanged pull targets and entity cursor, headless
  normal/sticky admission, and unrelated-front preservation. The full runtime
  component suite passes.
- The strict 20-case fix passes at
  `c/magma/trace/out/redstone_sticky_piston_retraction_boundary_fix_2/summary.md`.
  The 56-case affected family, including normal/sticky lifecycles, minimum
  pulses, all represented slime pull payloads, and reed/cactus columns, passes
  every behavior/raw outcome at
  `c/magma/trace/out/redstone_sticky_piston_retraction_boundary_family_1/summary.md`;
  54 state rows are strict and two delayed cactus trajectories are diagnostic.
- The 385-case aggregate passes 384 rows in 365.673 seconds at
  `c/magma/trace/out/matrix_redstone_sticky_piston_retraction_boundary_full_1/summary.md`.
  One unrelated scheduled-fire callback rescheduled without spreading despite
  its callback-seed hook; it passes 3/3 on fresh isolated clients at
  `c/magma/trace/out/fire_spread_scheduled_retraction_isolated_remeasure_1/summary.md`.
  The composite therefore covers all 385 behavior/raw outcomes: 381 required
  gates plus four not-required rows, 381 strict state passes, and four explicit
  delayed-cactus diagnostics.
- GPU-1 guards pass unchanged floors at 2.93M Blaze env-ticks/s and 27.7 1080p
  CUDA fps. Scalar timing remains deferred under the unrelated high-core
  workload; the last clean result is 4,122 steps/s.

## 2026-08-01 (six-facing sticky stone lifecycle and 405-case promotion)

- Added start/settled Java fixtures for one-stone sticky extension and pull in
  DOWN, UP, NORTH, SOUTH, and WEST, completing all six facings with the existing
  EAST progress fixtures. The pre-change 20-case probe passed exactly, proving
  the bounded runtime was already direction-generic.
- Added exhaustive native coverage for exact moving base/head/stone metadata,
  two-tile ownership, third-tile settlement, and sound RNG through extension
  and pull in every direction. The full runtime component suite passes.
- The 28-case lifecycle family passes at
  `c/magma/trace/out/redstone_sticky_piston_directional_stone_family_1/summary.md`.
  The expanded aggregate passes all 405 behavior and raw-block outcomes in
  356.999 seconds at
  `c/magma/trace/out/matrix_redstone_sticky_piston_directional_stone_full_1/summary.md`;
  401 state rows are strict and four delayed cactus trajectories remain
  explicit diagnostics.
- GPU-1 guards remain above the frozen floors at 2.93M Blaze env-ticks/s and
  29.31 1080p CUDA fps. This slice adds no production runtime work.

## 2026-08-01 (sticky target reactions and repower settlement)

- Added a cross-facing sticky-retraction matrix: DOWN birch planks move with
  metadata, UP structure void remains as a DESTROY reaction, NORTH empty chest
  remains as a tile state, SOUTH extended piston remains BLOCK, and WEST
  unextended piston moves through vanilla's piston special case. All ten
  start/settled pre-change cases pass exactly. Native tests lock each reaction.
- Added a two-edit boundary that removes power to begin retraction, then moves
  power to the opposite side while the base remains block 36. Java returns to
  the original extended piston/head state; magma previously settled to an
  unextended base with no head. The exact two-cell failure is at
  `c/magma/trace/out/redstone_sticky_piston_repower_during_retraction_probe_2/summary.md`.
- Moving-tile settlement now records restored piston bases and performs their
  bounded power checks after all retiring tiles are removed. This matches
  Java's `setBlockState`/`onBlockAdded` re-extension boundary without advancing
  a new moving tile in the same tick. Native coverage proves delayed repower,
  the second sound-RNG draw, new-tile ownership, and final settlement.
- The focused fix passes at
  `c/magma/trace/out/redstone_sticky_piston_repower_during_retraction_fix_1/summary.md`;
  the 65-case affected family passes at
  `c/magma/trace/out/redstone_sticky_piston_target_reaction_repower_family_1/summary.md`.
  The full aggregate passes 416/416 in 409.606 seconds at
  `c/magma/trace/out/matrix_redstone_sticky_piston_repower_full_1/summary.md`,
  with 412 strict state rows and four delayed-cactus diagnostics.
- GPU-1 guards pass at 2.94M Blaze env-ticks/s and 27.38 1080p CUDA fps. The
  new work runs only while active piston tiles retire; the idle path is
  unchanged.

## 2026-08-01 (moving-piston checkpoint persistence and 421-case promotion)

- Added exact `TileEntityPiston` observations to the Java oracle and magma
  state trace: position, moved block/meta, facing, source/extending flags, and
  the float bits of current and last progress. Ordinary empty active sets also
  compare exactly, so absence is tested rather than inferred.
- Extended the version-1 neutral capsule with a strict, bounded moving-piston
  payload and magma loader. Native coverage restores a half-progress tile,
  advances it to progress 1.0, and proves exact third-tick settlement.
- Added `run_moving_piston_checkpoint_regression.sh`. Its three real-Java
  stone-push captures complete in 20.230 seconds. The resume verifier passes
  tile presence, next half-step, tile retirement, and exact final raw blocks
  at
  `c/magma/trace/out/redstone_moving_piston_checkpoint_verify_final_1/summary.md`.
- Comparing the new field across the aggregate exposed four repower/remove
  cases where magma allocated a progress-zero replacement tile one tick before
  Java. Restored powered bases now enter a fixed 64-entry next-tick recheck
  queue. Unpowered bases do not enter it. The focused six-case timing family
  and full native runtime suite pass.
- The final 32-client aggregate passes all 421 behavior/raw outcomes in
  367.658 seconds at
  `c/magma/trace/out/matrix_redstone_moving_piston_checkpoint_full_2/summary.md`:
  417 strict state rows plus the four existing delayed-cactus diagnostics.
- GPU-1 guards pass unchanged floors at 2.93M Blaze env-ticks/s and 25.02
  1080p CUDA fps. Scalar measurement is deferred because unrelated jobs are
  consuming roughly 64 CPU cores; the last quiet-host pass remains 4,122
  steps/s. The added runtime work uses fixed arrays, no heap allocation, and
  empty-count fast paths.

## 2026-08-01 (moving-piston living-mob push and 424-case promotion)

- Added a locked NoAI-pig collision fixture in front of an EAST moving piston.
  The deliberate old-C probe differs only in entity x at the first observation:
  Java moves the pig from 12.5 to `12.959999988079071`, while magma remains at
  12.5, at
  `c/magma/trace/out/redstone_piston_pig_push_probe_1/summary.md`.
- Moving piston tiles now sweep represented living mobs using the existing
  exact head/full-cube collision shapes, float mob dimensions, Java's
  overlap-plus-0.01 displacement, and its per-axis 0.51 clamp. The path uses
  fixed arrays and returns before all entity work when no moving tile exists.
- Fresh EAST and UP Java comparisons pass exact pig coordinates, complete
  observed state, raw blocks, and light at
  `c/magma/trace/out/redstone_piston_living_push_fix_1/summary.md` and
  `c/magma/trace/out/redstone_piston_up_pig_push_probe_1/summary.md`. A player
  negative remains stationary in both implementations; this does not claim
  player push support.
- Native coverage locks the sweep in all six facings and proves an off-axis
  pig is not displaced. The complete runtime component suite passes.
- The 32-client aggregate passes all 424 behavior/raw outcomes in 362.343
  seconds at
  `c/magma/trace/out/matrix_redstone_piston_living_push_full_1/summary.md`:
  420 strict state rows plus the four existing delayed-cactus diagnostics.
- Performance guards pass unchanged floors at 4,948 scalar steps/s, 2.93M
  Blaze env-ticks/s, and 28.94 1080p CUDA fps. The scalar run passed despite
  the unrelated high-core workload.

## 2026-08-01 (full-cube wall-clipped piston mob push and 425-case promotion)

- Added a full-cube wall immediately beyond the EAST moving-piston pig
  fixture. The deliberate old-C probe has exact blocks, light, and 24 state
  features, then isolates the entity mismatch: Java stops at
  `12.550000011920929`, while old magma reaches `12.959999988079071`, at
  `c/magma/trace/out/redstone_piston_pig_wall_push_probe_1/summary.md`.
- Moving-piston living displacement now resolves its bounded swept AABB
  against exact static full cubes before applying the axis offset. The path is
  entered only when a moving tile and represented living mob are both active;
  it does not alter item or idle simulation paths.
- Fresh Java wall, open EAST, and open UP cases pass 3/3 at
  `c/magma/trace/out/redstone_piston_pig_wall_push_fix_1/summary.md`. Native
  runtime coverage locks the stationary second wall observation and retains
  all six open-space facings. The complete runtime component suite passes.
- The 32-client aggregate passes all 425 behavior/raw outcomes in 383.029
  seconds at
  `c/magma/trace/out/matrix_redstone_piston_pig_wall_push_full_1/summary.md`:
  421 strict state rows plus the four existing delayed-cactus diagnostics.
  One unrelated client failure retried automatically and passed.
- GPU-1 guards pass at 2.93M Blaze env-ticks/s and 28.66 1080p CUDA fps. A
  pinned idle-core scalar rerun passes at 4,397 steps/s. The initial unpinned
  run is retained at
  `c/magma/trace/out/perf_guard_redstone_piston_pig_wall_push_cpu_1.json`; its
  3,684 steps/s measurement followed the Java pool under high host load.
  Non-full collision shapes remain the next piston-mob parity slice.

## 2026-08-01 (soul-sand piston mob clipping and 427-case promotion)

- Rejected a horizontal soul-sand probe as insufficient because full-cube and
  7/8-height shapes share the same side face. A DOWN piston above the pig
  isolates the top face instead: Java moves y=80 to `79.875`, while old magma
  remains at y=80. Blocks and light are exact at
  `c/magma/trace/out/redstone_piston_pig_soul_sand_vertical_probe_2/summary.md`.
- The bounded static collision resolver now emits soul sand's exact full-width,
  0.875-high box. Native coverage locks both observations at y=79.875. The
  complete runtime component suite passes.
- Fresh Java comparisons pass 5/5 for the vertical top face, horizontal side,
  full-cube wall, open EAST, and open UP at
  `c/magma/trace/out/redstone_piston_pig_soul_sand_fix_1/summary.md`.
- The 427-case aggregate ran in 337.767 seconds at
  `c/magma/trace/out/matrix_redstone_piston_pig_soul_sand_full_1/summary.md`.
  It has 422 direct strict state passes, four delayed-cactus diagnostics, and
  exact raw blocks in all 427 rows. Its only failure was an unrelated broad
  random-walk Java `on_ground` bit at tick 13; the immediate isolated rerun
  passes all 25 supported features at
  `c/magma/trace/out/random_seed_1_soul_sand_promotion_retry_1/summary.md`,
  yielding a 423-strict-state composite promotion.
- Performance guards pass at 5,046 pinned scalar steps/s, 2.94M Blaze
  env-ticks/s, and 31.27 1080p CUDA fps. Slabs, stairs, fences, and walls remain
  the next non-full collision-shape work.

## 2026-08-01 (single-slab piston mob clipping and 432-case promotion)

- Added a DOWN piston probe over a bottom stone slab. Java moves the pig from
  y=80 to `79.59000002384185`, then settles at the slab top y=79.5. Old magma
  falls through to `79.49`, then `78.97999999999999`, while blocks and light
  remain exact at
  `c/magma/trace/out/redstone_piston_pig_bottom_slab_probe_1/summary.md`.
- The first fix exposed a shared dimension error: local 1.11.2 EntityPig
  source uses a 0.9 x 0.9 box, while C grouped pigs with 1.4-high cows. The
  corrected dimension makes the open DOWN half-steps exact at
  `c/magma/trace/out/redstone_piston_down_pig_push_fix_1/summary.md` and leaves
  the existing EAST, UP, wall, soul-sand, pressure-plate, and melee gates
  passing.
- Static clipping now emits exact top/bottom half boxes for all four single
  slab IDs: stone 44, wood 126, stone slab2 182, and purpur 205. Fresh Java
  registry coverage passes 4/4 at
  `c/magma/trace/out/redstone_piston_pig_slab_registry_fix_1/summary.md`.
  Native coverage loops across the same IDs and retains the six-facing open
  sweep. The complete runtime component suite passes.
- The clean 32-client aggregate passes 432/432 behavior/raw outcomes in
  336.622 seconds at
  `c/magma/trace/out/matrix_redstone_piston_pig_slab_full_1/summary.md`: 428
  strict state rows plus the four existing delayed-cactus diagnostics.
- Performance guards pass at 4,956 pinned scalar steps/s, 2.94M Blaze
  env-ticks/s, and 31.03 1080p CUDA fps. The next collision-shape slice is
  stairs, fences, and walls.

## 2026-08-01 (stair piston mob clipping and 438-case promotion)

- Added a DOWN piston probe spanning the lower and upper lanes of a bottom
  EAST-ascending oak stair. Java settles the lower lane from
  `79.59000002384185` to y=79.5 and keeps the upper lane at y=80. Old magma
  lets both fall to `79.09000002384185`, while blocks and light remain exact at
  `c/magma/trace/out/redstone_piston_pig_straight_stair_probe_1/summary.md`.
- Static piston-mob clipping now ports the 1.11.2 `BlockStairs` collision
  algorithm: base slab plus quarter/eighth boxes, four facings, top/bottom
  halves, and neighbor-derived straight, inner-left/right, and
  outer-left/right shapes. The registry includes all fourteen stair IDs.
- Fresh Java comparisons pass both straight lanes, the kept and removed outer
  corners, an inner empty corner, and a top stair at
  `c/magma/trace/out/redstone_piston_pig_straight_stair_fix_1/summary.md`,
  `c/magma/trace/out/redstone_piston_pig_outer_stair_fix_1/summary.md`, and
  `c/magma/trace/out/redstone_piston_pig_inner_top_stair_fix_1/summary.md`.
  Native coverage loops through every ID, facing, half, and lane and locks the
  connected shapes. The complete runtime component suite passes.
- The 32-client aggregate passes 438/438 behavior/raw outcomes in 394.894
  seconds at
  `c/magma/trace/out/matrix_redstone_piston_pig_stair_full_1/summary.md`: 434
  strict state rows plus the four existing delayed-cactus diagnostics. One
  unrelated client infrastructure timeout passed its automatic isolated
  retry.
- Performance guards pass at 4,666 scalar steps/s on an idle physical core,
  2.93M Blaze env-ticks/s, and 24.69 1080p CUDA fps. The first CPU sample used
  a core whose hyperthread sibling was occupied and is retained as a rejected
  host-contaminated measurement. Fences, walls, and other non-full collision
  shapes are next.

## 2026-08-01 (fence, wall, and gate piston mob clipping, 448 cases)

- Added DOWN-piston fixtures for an isolated oak-fence post, a north arm
  connected through an open gate, an isolated cobblestone-wall post, a wall
  north arm, and the narrow side gap beside a straight north/south wall. Java
  holds each positive at y=80.5 while old magma falls to `79.99 ->
  79.47999999999999`; the gap is an exact Java/C negative at
  `c/magma/trace/out/redstone_piston_pig_fence_wall_probe_1/summary.md`.
- Ported the source collision and connection rules for all seven fence IDs and
  both wall variants, including material-separated wood/nether fences, four
  arms, multi-box fence corners, single-box wall unions, and both 3/8-wide
  straight-wall orientations. The five focused cases pass at
  `c/magma/trace/out/redstone_piston_pig_fence_wall_fix_1/summary.md`.
- Added all six fence-gate IDs to the same collision family. Closed gates use
  the exact 1.5-high X/Z axis box; open gates have no collision. The deliberate
  closed positive and open negative are captured at
  `c/magma/trace/out/redstone_piston_pig_fence_gate_probe_1/summary.md` and pass
  after the fix at
  `c/magma/trace/out/redstone_piston_pig_fence_gate_fix_1/summary.md`.
- Native coverage locks every registry ID, wall direction, gate state/axis,
  the fence L-corner negative, and both wall narrowing directions. The full
  runtime component suite passes.
- Matched Java's opaque-material/full-cube connection predicate, including its
  redstone-block and observer exceptions to normal-cube status and its gourd
  exclusion. The three discriminators pass at
  `c/magma/trace/out/redstone_piston_pig_fence_connection_registry_fix_5/summary.md`.
- The clean 32-client aggregate passes 448/448 behavior/raw outcomes in
  358.941 seconds at
  `c/magma/trace/out/matrix_redstone_piston_pig_fence_wall_registry_full_1/summary.md`:
  444 strict state rows plus the four existing delayed-cactus diagnostics.
- Performance guards pass at 4,856 pinned scalar steps/s, 2.93M Blaze
  env-ticks/s, and 30.29 1080p CUDA fps. Other non-full collision shapes remain
  open.

## 2026-08-01 (thin and fixed-surface piston mob clipping, 462 cases)

- Ported exact collision boxes for snow layers, carpet, beds, cake, enchanting
  tables, both daylight detector IDs, and end-portal frames. This includes
  snow's one-layer-short collision height, cake's bite-dependent west edge,
  and the end-frame eye's separate centered top box.
- Added 14 Java cases with explicit empty-footprint controls. The corrected
  pre-fix probe is
  `c/magma/trace/out/redstone_piston_pig_thin_surface_probe_2/summary.md`; all
  cases pass after the fix at
  `c/magma/trace/out/redstone_piston_pig_thin_surface_fix_2/summary.md`.
- Native coverage sweeps every snow metadata value, all carpet colors, both
  bed parts/facings, all cake bites, both detector registries and power values,
  and end-frame base/eye lanes. The complete runtime suite passes.
- The 32-client aggregate ran 462 cases in 438.929 seconds at
  `c/magma/trace/out/matrix_redstone_piston_pig_thin_surface_full_2/summary.md`.
  One older trapped-chest capture missed its Java open input and passed on a
  fresh client at
  `c/magma/trace/out/redstone_trapped_chest_viewer_power_open_close_retry_thin_surface_1/summary.md`,
  yielding 458 strict rows, four existing diagnostics, and 462 exact
  behavior/raw outcomes.
- Performance guards pass at 5,052 pinned scalar steps/s, 2.93M Blaze
  env-ticks/s, and 24.63 pinned 1080p CUDA fps. Failed samples on busy
  affinities are retained as host-contaminated evidence.

## 2026-08-01 (panes, iron bars, trapdoors, and 482-case promotion)

- Added exact static shapes for glass panes 101, iron bars 102, stained panes
  160, oak trapdoors 96, and iron trapdoors 167. Pane coverage includes the
  center post, four arms, glass/pane/full-cube and side-solid connector rules,
  stair-shape discriminators, farmland, bottom slabs, snow, and redstone
  blocks. Trapdoor coverage includes top/bottom closed states and all four
  open orientations for both IDs.
- The first focused comparison found the causal mismatch at tick 1: Java
  clipped the piston-pushed item against each positive pane lane, while magma
  let it continue falling. Shape generation was already correct. The item
  piston path lacked the bounded static-axis resolver already used for living
  mobs. Adding that clip makes all eight pane/iron-bar cases pass at
  `c/magma/trace/out/redstone_piston_pane_fix_1/summary.md`; all twelve
  trapdoor cases also pass. Native coverage locks all metadata and negative
  lanes, and the full runtime component suite passes.
- The 16-client aggregate ran 482 cases in 637.524 seconds at
  `c/magma/trace/out/matrix_redstone_piston_pane_trapdoor_full_2/summary.md`.
  Two older cases observed stale Java setup activity under the loaded pool and
  both passed immediately on fresh clients at
  `c/magma/trace/out/drowning_fire_retry_pane_trapdoor_1/summary.md`. The
  composite promotion is 478 strict state rows, four existing delayed-cactus
  diagnostics, and 482/482 exact behavior/raw outcomes.
- Performance guards pass frozen floors at 5,137 pinned scalar steps/s, 2.92M
  Blaze env-ticks/s, and 30.33 1080p CUDA fps on GPU 1 at
  `c/magma/trace/out/perf_guard_redstone_pane_trapdoor_item_static_clip_1.json`.
  Static item clipping is bounded to an active moving-piston/item overlap and
  adds no idle-world scan or allocation. Cauldrons and hoppers are the next
  compound static collision shapes.

## 2026-08-01 (cauldron and hopper compound collision, 492 cases)

- Ported the exact five-box static collision shapes for cauldron 118 and
  hopper 154: a full-footprint base plus four full-height 1/8 rim walls. The
  base heights are 5/16 and 5/8 respectively.
- Added center and four-rim Java item fixtures for each block. The hopper is
  powered from below so vanilla neighbor updates retain disabled metadata and
  item collection cannot contaminate the geometry proof. All ten cases pass.
- Added exact empty five-slot hopper persistence to the Java capture, neutral
  capsule validator, and magma cold container pool. Hopper transfer remains
  queued under R-05 automation and is not claimed by this slice.
- Native coverage sweeps both blocks, all metadata, center, and four rims. The
  complete runtime suite passes.
- The 16-client aggregate ran 492 cases in 644.095 seconds at
  `c/magma/trace/out/matrix_redstone_piston_cauldron_hopper_full_1/summary.md`.
  Its sole unrelated comparator aggregate flag passed on an immediate isolated
  retry at
  `c/magma/trace/out/comparator_extension_retry_cauldron_hopper_1/summary.md`,
  yielding 488 strict rows, four existing diagnostics, and 492/492 exact
  behavior/raw outcomes.
- Performance guards pass at 4,336 pinned scalar steps/s, 2.91M Blaze
  env-ticks/s, and 28.91 1080p CUDA fps on GPU 1 at
  `c/magma/trace/out/perf_guard_redstone_cauldron_hopper_static_shape_1.json`.
  Shape work remains bounded to an active moving-piston/entity overlap, and
  empty hopper state has no tick hook.

## 2026-08-01 (anvil, end-rod, and dragon-egg collision, 508 cases)

- Added paired Java occupied/empty-lane probes for both anvil axes, all three
  end-rod axes, and the dragon egg's inset footprint. The pre-fix result at
  `c/magma/trace/out/redstone_piston_item_directional_shape_probe_1/summary.md`
  fails only the six occupied lanes at tick 1 by y=0.26; controls and raw
  block/light outcomes are exact.
- Ported anvil's facing-selected 3/4-width full-height box, end rod's centered
  1/4 cross-section for every facing, and dragon egg's full-height 1/16 inset.
  Horizontal rod axes are tested at pig mid-height with perpendicular or
  above-rod controls, avoiding a false claim from a downward path above the
  horizontal rod.
- The corrected 16-case composite passes at
  `c/magma/trace/out/redstone_piston_directional_shape_fix_2/summary.md` and
  `c/magma/trace/out/redstone_piston_end_rod_z_upper_lane_fix_3/summary.md`.
  Native coverage spans all anvil damage classes, all six rod facings, and the
  dragon-egg positive/negative footprint. The complete runtime suite passes.
- The clean 16-client aggregate passes 508/508 outcomes in 728.010 seconds at
  `c/magma/trace/out/matrix_redstone_piston_directional_shape_full_1/summary.md`:
  504 strict state rows plus four existing diagnostics, with no retry.
- Performance guards pass at 4,201 pinned scalar steps/s, 2.89M Blaze
  env-ticks/s, and 28.77 1080p CUDA fps on GPU 1 at
  `c/magma/trace/out/perf_guard_redstone_directional_static_shape_1.json`.
  The new work remains bounded to an active piston/entity overlap.

## 2026-08-01 (ordinary and trapped chest collision, 517 cases)

- Added isolated ordinary/trapped chest fixtures, same-type north/west seam
  fixtures, empty-lane controls, and a mixed-type neighbor fixture. The
  pre-fix nine-case probe at
  `c/magma/trace/out/redstone_piston_item_chest_shape_probe_1/summary.md`
  first diverges at tick 1 by exactly 0.135 item Y on occupied surfaces while
  raw block/light state remains exact.
- Ported the exact 7/8-high, 1/16-inset chest collision box and Java's NORTH,
  SOUTH, WEST, EAST same-registry neighbor priority. Ordinary 54 and trapped
  146 therefore share geometry but do not connect to one another. All nine
  focused cases pass at
  `c/magma/trace/out/redstone_piston_chest_shape_fix_2/summary.md`; native
  coverage checks both IDs, isolated lanes, and all four same-type extensions.
  The complete runtime suite passes.
- The clean 16-client aggregate passes 517/517 outcomes in 769.246 seconds at
  `c/magma/trace/out/matrix_redstone_piston_chest_shape_full_1/summary.md`:
  513 strict state rows plus four existing diagnostics, with no retry.
- Performance promotion remains pending under three unrelated Tak generators
  using 64, 56, and 56 workers. Four samples are preserved at
  `c/magma/trace/out/perf_guard_redstone_chest_static_shape_1.json` through
  `perf_guard_redstone_chest_static_shape_4.json`. The scalar and render
  executables are unchanged July 31 binaries that previously passed at 4,201
  steps/s and 28.77 fps; under the saturated host they fail together while
  the unchanged batched CUDA metric still passes at 2.93M env-ticks/s. No
  floor was changed and no contended sample was promoted.

## 2026-08-01 (paired door collision, 526 cases)

- Added oak-door probes for every closed facing, both open EAST hinges, an
  occupied panel/lane discriminator, and an iron-door representative. The
  pre-fix nine-case result at
  `c/magma/trace/out/redstone_piston_item_door_shape_probe_1/summary.md` fails
  the seven occupied panels at tick 1 by y=0.26; both empty lanes and every
  raw block/light outcome already pass.
- Ported the exact 3/16-thick door collision panel using Java's paired actual
  state: facing/open from the lower half and hinge from the upper half. Native
  coverage locks all four closed facings, all eight open facing/hinge states,
  lane negatives, and representative states for oak, iron, spruce, birch,
  jungle, acacia, and dark-oak doors. The focused 9/9 set and complete native
  runtime suite pass at
  `c/magma/trace/out/redstone_piston_door_shape_fix_1/summary.md`.
- The four-client aggregate ran 526 cases in 3,822.606 seconds at
  `c/magma/trace/out/matrix_redstone_piston_door_shape_full_3/summary.md`: 521
  strict rows and four existing diagnostics pass there. One older
  trapped-chest row missed its Java click under load and passes fully on a
  fresh isolated client at
  `c/magma/trace/out/redstone_trapped_chest_viewer_power_open_close_retry_door_shape_1/summary.md`.
  Composite promotion is therefore 522 strict rows, four diagnostics, and
  526/526 exact behavior/raw outcomes.
- Performance recapture is pending while unrelated 80-thread Tak generation
  and training jobs saturate the host. The last clean comparable guard remains
  4,201 scalar steps/s, 2.89M Blaze env-ticks/s, and 28.77 CUDA fps. Door
  collision is evaluated only for an active piston/item intersection and adds
  no loaded-world scan, heap allocation, or idle tick hook.

## 2026-08-01 (remaining stable static collision families, 542 cases)

- Ported exact static collision boxes for cactus 81, lily pad 111, ender chest
  130, flower pot 140, skull 144, ladder 65, and cocoa 127. This includes
  distinct skull/ladder facing decoders, floor and wall skull shapes, and all
  cocoa facing/age sizes. Lily-pad geometry is the ordinary non-boat path;
  vanilla's special boat exception remains outside this slice.
- Built eight occupied geometry cases plus eight exact negative controls. The
  corrected pre-fix probes fail every occupied case at the first entity-position
  observation while every empty lane and raw block/light outcome passes. The
  16/16 corrected set passes at
  `c/magma/trace/out/redstone_piston_remaining_shape_fix_1/summary.md`.
- Native coverage sweeps both footprint families, the low-profile height
  boundaries, all wall-skull and ladder facings, all 12 valid cocoa states,
  and opposite-lane controls. The complete runtime suite passes.
- The four-client aggregate passes 542/542 outcomes in 4,519.773 seconds at
  `c/magma/trace/out/matrix_redstone_piston_remaining_shape_full_1/summary.md`:
  538 strict rows and the same four delayed-cactus diagnostics, with no retry
  or infrastructure failure.
- Performance recapture is deliberately pending while unrelated Tak generators
  and search workers saturate the host CPUs. The latest clean comparable guard
  remains 4,201 scalar steps/s, 2.89M Blaze env-ticks/s, and 28.77 CUDA fps.
  New work is constant-time and is reached only for an active piston/entity
  intersection, with no loaded-world scan, allocation, or idle hook.

## 2026-08-01 (connected chorus-plant collision, 545 cases)

- Ported `BlockChorusPlant` collision from the local 1.11.2 source: a centered
  5/8 cube plus one 3/16 arm for each actual-state connection. Horizontal and
  UP connections accept chorus plants or flowers; DOWN additionally accepts
  end stone. The bounded static-shape capacity is now seven boxes.
- Rejected two contaminated arm fixtures before promotion. A second plant
  became invalid when a downward moving head filled the target's upper cell;
  replacing it with a flower made the observed item touch the flower's own
  full-cube collision. The corrected horizontal-piston pair keeps two plants
  valid and isolates their joined arms without reaching either center body.
- The clean pre-fix proof at
  `c/magma/trace/out/redstone_piston_chorus_plant_shape_probe_3/summary.md`
  fails the center and connected arm at tick 1 by 0.0725 while the exact empty
  lane and all raw block/light outcomes pass. All three focused cases pass at
  `c/magma/trace/out/redstone_piston_chorus_plant_shape_fix_1/summary.md`.
- Native tests cover the center, four horizontal arms, UP and DOWN arms, and
  every corresponding empty lane. The complete runtime suite passes.
- The four-client aggregate passes 545/545 outcomes in 3,011.750 seconds at
  `c/magma/trace/out/matrix_redstone_piston_chorus_plant_shape_full_1/summary.md`:
  541 strict rows and the same four delayed-cactus diagnostics, with no retry
  or infrastructure failure.
- Performance recapture is pending while unrelated generators and search
  workers saturate the host CPUs. The latest clean comparable guard remains
  4,201 scalar steps/s, 2.89M Blaze env-ticks/s, and 28.77 CUDA fps. Shape
  storage is fixed on the stack and is reached only for active piston/entity
  intersections, with no scan, allocation, or idle hook.

## 2026-08-01 (farmland and grass-path collision, 549 cases)

- Ported the exact full-footprint 15/16-high collision box shared by farmland
  60 and grass path 208 in the local Java 1.11.2 sources. The stable farmland
  fixture uses moisture 0 below mature wheat, so it can neither dry into dirt
  nor grow during the horizontal-piston probe.
- The clean four-case pre-fix result at
  `c/magma/trace/out/redstone_piston_farmland_grass_path_shape_probe_1/summary.md`
  fails both occupied side lanes first at tick 1 by 0.26 item X. Both
  just-above controls and every raw block/light outcome pass. All four cases
  pass after the shape fix at
  `c/magma/trace/out/redstone_piston_farmland_grass_path_shape_fix_1/summary.md`.
- Native coverage checks all eight farmland moisture values and grass path on
  both sides of the 15/16 boundary. The complete runtime suite passes.
- The four-client matrix ran 549 cases in 2,751.851 seconds at
  `c/magma/trace/out/matrix_redstone_piston_farmland_grass_path_shape_full_1/summary.md`:
  543 ordinary passes, four existing diagnostics, and two contaminated older
  probes. Java had not received a final-staged trapped chest before its click,
  while a daylight detector crossed its ambient 20-tick update and changed
  metadata 0 to 15. Staging the chest early and beginning the frozen-noon
  detector at metadata 15 remove those unrelated phases. Both corrected rows
  and all four new rows pass 6/6 at
  `c/magma/trace/out/redstone_piston_farmland_grass_path_shape_fixture_hardening_1/summary.md`.
  Composite promotion is 545 strict rows, four delayed-cactus diagnostics, and
  549/549 exact behavior/raw outcomes.
- The idle GPU 1 guard passes at
  `c/magma/trace/out/perf_guard_redstone_piston_farmland_grass_path_shape_1.json`:
  4,990 scalar steps/s, 2.93M Blaze env-ticks/s, and 30.85 1080p CUDA fps. The
  added branch runs only during active piston/entity collision scanning and
  adds no allocation or idle-world work.

## 2026-08-02 (complete double-height plant models)

- Completed the six-species id-175 model family from the local 1.11.2 jar and
  `BlockDoublePlant` source. Sunflower, syringa, double grass, double fern,
  rose, and paeonia now keep distinct lower and upper sprites; only grass and
  fern receive biome grass tint. Upper halves reproduce Java actual-state
  lookup by reading the lower half's variant.
- Ported `double_sunflower_top.json` exactly as two half-height diagonal stem
  planes and a tilted two-sided flower head with distinct front/back textures.
  The atlas generator appends the ten missing textures without renumbering any
  established sprite.
- Expanded the existing jar-model, model-table, and emitted-mesh gates. They
  now prove all six lower/upper sprite and tint contracts, contextual upper
  selection, CUTOUT routing with no SOLID leakage, ordinary 24-vertex crosses,
  and the sunflower upper's 36 vertices split 24 top, six back, and six front.
  `make verify-harsh` and the complete native runtime suite pass.
- Added `raster/verify/scenarios/double_plant_gallery.yaml`. Its valid Java tape
  `20260802T004204Z_fast_s0_creative_flat_rd8_0c4e00d6` preserves all six pairs
  in one server tick. C has no physics divergence over 1,188 ticks, no world
  hash delta, and the structural pixel gate passes across 119 frames at
  `c/magma/trace/out/double_plant_gallery_valid_fix_1/`.
- GPU 1 performance passes at
  `c/magma/trace/out/perf_guard_double_plant_models_1.json`: 4,780 scalar
  steps/s, 2.93M Blaze env-ticks/s, and 31.37 1080p CUDA fps. The feature adds
  no simulation-tick work; contextual selection runs only while a chunk is
  meshed.

## 2026-08-02 (live blaze attack cycle, first B-03 slice)

- Replaced the live blaze's flat 40-tick ranged cooldown with the local
  1.11.2 `EntityBlaze.AIFireballAttack` state machine: 60-tick charge, shots at
  the next three six-tick edges, 100-tick rest, and exact charge reset and
  reacquisition semantics. Close blazes now use the six-point attack attribute.
- Propagated charged blazes and ordinary live fire counters through
  `GmEntityView.flags`, so the existing full-bright blaze and fire-layer render
  paths now receive authoritative live state. The deliberate pre-fix native
  regression fired at 0/40/80 and never set the flag; the corrected run fires
  at 60/66/72 and proves 78 charged ticks followed by 100 clear ticks.
- Replaced constant-speed small fireballs with zero initial motion, normalized
  0.1 acceleration, and the 0.95 motion factor. Entity hits deal five and ignite
  for five seconds; block hits place adjacent fire and no longer explode. The
  native gate also covers target loss/reacquisition, six-point melee, live
  daylight-burning flags, exact first/second speeds 0.095/0.18525, and retained
  solid terrain on impact.
- Added `c/magma/raster/verify/scenarios/blaze_attack_cycle.yaml`. The steady
  Java cycle in `scenario_blaze_attack_cycle_20260802T010552Z` charges at
  recorder tick 184, creates small fireballs at 243/249/255, and clears at 261.
  The structural pixel gate passes all 41 frames. The tape is not a complete
  replay promotion: it retains the recorder-start one-tick `on_ground` pulse
  with zero positional drift and one later world-hash delta.
- `game/test_mob_live.sh`, the complete runtime suite, and both product builds
  pass. Rebuilt-binary performance on GPU 1 passes at
  `c/magma/trace/out/perf_guard_live_blaze_attack_cycle_3.json`: 4,834 scalar
  steps/s, 2.93M batched Blaze env-ticks/s, and 31.80 1080p CUDA fps.
- B-03 remains active. Per-entity Gaussian spread/RNG, exact floating motion,
  projectile ray/AABB selection, and the `mobGriefing=false` branch were the
  next gaps at this first-slice boundary.

## 2026-08-02 (live small-fireball ray selection, second B-03 slice)

- Replaced the live small fireball's 0.25-block point sampling and 0.75-radius
  player proxy with continuous segment selection in Java's order: find the
  nearest block intercept, shorten the entity segment there, then intersect
  the player's real 0.6 by 1.8 AABB expanded by
  0.30000001192092896. Block impact now uses the actual intercepted face for
  adjacent-fire placement.
- Block rays use the represented 1.11.2 shaped bounding boxes and honor the
  `ignoreBlockWithoutBoundingBox=true` exclusion set. This fixes partial-block
  false positives while retaining full-cube and shaped-face impacts. Soul
  sand deliberately uses its full inherited ray box rather than its lower
  physical collision box, matching `Block.collisionRayTrace`.
- The deliberate pre-fix native run failed both narrow cases: it missed a
  segment through the upper corner of the expanded player box and killed a
  fireball traveling through the empty upper half of a lower slab. Both now
  pass, along with exact five damage, five-second ignition, face-adjacent fire,
  and the 60/66/72 blaze cadence suite. The complete runtime gate and both
  product builds pass.
- The inactive-projectile path remains a single branch. Active air traversal
  performs one block lookup per bounded segment cell and evaluates shaped
  boxes only for occupied cells. GPU 1 performance passes at
  `c/magma/trace/out/perf_guard_live_blaze_raycast_1.json`: 5,171 scalar
  steps/s, 2.93M batched Blaze env-ticks/s, and 31.47 1080p CUDA fps.
- B-03 remains active for per-entity Gaussian spread/RNG, blaze floating,
  other-entity candidates and exceptional custom block rays, the
  `mobGriefing=false` branch, and an independent live entity trajectory gate.

## 2026-08-02 (small-fireball gamerule and mob impacts, third B-03 slice)

- Added supported `--mob-griefing on|off` runtime state, defaulting to vanilla
  on. Small fireballs now retain whether their shooter is an `EntityLiving`;
  with the rule off a blaze-owned block impact dies without placing fire,
  while a shooterless fireball keeps vanilla's gamerule bypass and ignites.
- Retained the exact shooter entity ID on blaze shots and added represented
  mob/boat candidates to the continuous segment query. Candidate boxes use
  each entity's scaled dimensions expanded by 0.30000001192092896. The shooter
  is excluded through air tick 24 and becomes eligible on tick 25, matching
  `ProjectileHelper.forwardsRaycast`.
- Mob impacts now apply five damage and `setFire(5)` through the existing live
  hurt state. Longer fire durations are not shortened. Blaze, ghast, magma
  cube, pigman, and wither skeleton fire immunity is honored; boats use their
  damage-taken break rule. The deliberate pre-fix run failed both nearest-mob
  damage and tick-25 shooter admission; the corrected focused gate passes
  those plus immunity and the two opposite mobGriefing cases.
- Completed the two custom block-ray overrides in the local 1.11.2 block
  roster. Stairs reuse their existing actual-state slab/quarter/eighth physics
  boxes for ray selection; moving-piston block 36 is ignored because
  `BlockPistonMoving.collisionRayTrace` returns null unconditionally. Both
  focused cases failed before the change and pass after it.
- B-03 remains active for per-entity Gaussian spread/RNG, exact blaze floating,
  dragon/multipart candidates, and an independent live Java-vs-C trajectory
  gate.
- The focused config/mob gates, complete runtime suite, structural harsh gate,
  and CPU/CUDA product builds pass. GPU 1 performance passes at
  `c/magma/trace/out/perf_guard_live_blaze_entities_gamerule_1.json`: 4,971
  scalar steps/s, 2.93M batched Blaze env-ticks/s, and 29.56 1080p CUDA fps.
  GPUs 2 and 3 were simultaneously saturated by unrelated 300 W jobs; every
  metric remains above its machine-local regression floor.

## 2026-08-02 (projectile occupancy and glowstone-supported dust)

- Matched the vanilla distinction between projectile activation and scheduled
  occupancy. A small fireball never calls block collisions and cannot activate
  an unpowered tripwire, but an already-powered wire's scheduled all-entity
  query sees it and retains power. The deliberate old-C and corrected two-case
  gates are at `c/magma/trace/out/matrix_small_fireball_tripwire_probe_1/` and
  `c/magma/trace/out/matrix_small_fireball_tripwire_1/`.
- Added the explicit glowstone support accepted by
  `BlockRedstoneWire.canPlaceBlockAt`. The old C result powers neither dust nor
  lamp while the ordinary stone-supported control passes. Both circuits are
  exact after the fix at
  `c/magma/trace/out/matrix_wire_glowstone_1/summary.md`; the native aggregate
  also retains glass as a negative support.
- Extended the immutable registry capture to schema v2 with Java's exact
  stateful `isFullyOpaque` mask for all 4,096 legacy states. Independent top
  slab, upside-down stair, and correctly supported eight-layer snow fixtures
  each reproduce the old dust/lamp failure and pass together with glowstone at
  `c/magma/trace/out/matrix_wire_fully_opaque_1/summary.md`. Native negatives
  cover bottom halves, seven-layer snow, and glass.
- Removed the old air-only component-cover restriction. Flat dust now runs
  under a solid ceiling, glass above a lower wire permits a one-block climb,
  and stone above it blocks the upward edge while preserving lower strength
  15. The deliberate three-case probe and corrected four-case family are at
  `c/magma/trace/out/matrix_wire_headroom_probe_1/summary.md` and
  `c/magma/trace/out/matrix_wire_headroom_1/summary.md`.
- The current correctness promotion covers 565 composite outcomes. The native
  runtime suite passes at
  `c/magma/trace/out/test_runtime_wire_headroom.log`. GPU 1 performance passes
  at `c/magma/trace/out/perf_guard_wire_headroom.json`: 5,058 scalar steps/s,
  2.93M Blaze env-ticks/s, and 31.31 1080p CUDA fps.

## 2026-08-02 (directional output from vertically connected dust)

- Replaced Magma's same-level redstone-provider shortcut in dust weak output
  with Java 1.11.2's `BlockRedstoneWire.isPowerSourceAt` rule. Dust reached one
  block above or below now contributes to horizontal shape, repeaters and
  comparators connect only on their axis, observers connect only on their
  facing side, and the complete vanilla power-provider set is recognized.
- The deliberate old-C climb probe has an exact shared prestate with settled
  dust strengths 14/15. Its connected-face lamp passes, while its
  perpendicular lamp is the first and only divergent cell: Java places unlit
  123 and Magma incorrectly changes it to lit 124. The probe is preserved at
  `c/magma/trace/out/matrix_wire_directional_climb_probe_1/summary.md`.
- Corrected climb/descent connected and perpendicular controls plus
  wrong-axis/aligned repeater controls pass 6/6 at
  `c/magma/trace/out/matrix_wire_directional_output_candidate_1/summary.md`.
  Eight affected earlier wire/piston cases pass independently at
  `c/magma/trace/out/matrix_wire_directional_output_regression_1/summary.md`.
  Every row has 25 matching simulated features, exact ordered pending work,
  exact raw blocks, and exact light.
- The correctness promotion now covers 571 composite outcomes. Native
  climb/descent and repeater/comparator assertions and the complete runtime
  suite pass at
  `c/magma/trace/out/test_runtime_wire_directional_output.log`. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_wire_directional_output.json`: 5,098 scalar
  steps/s, 2.94M batched Blaze env-ticks/s, and 31.78 1080p CUDA fps. The
  shape rule runs only inside existing local redstone queries and adds no
  scan, allocation, or inactive-world tick work.

## 2026-08-02 (dust support-loss lifecycle)

- Added Java-authoritative support invalidation to dust neighbor updates.
  Unsupported wire now consumes the exact four `World.rand` drop draws,
  creates redstone item 331 with exact entity ID and Math RNG state, becomes
  air, recomputes connected dust, and notifies downstream consumers.
- The deliberate old-C fixture removes the stone support at tick zero. Java
  removes the wire and emits one age-1 redstone item; Magma leaves the wire
  floating, with raw block, RNG, entity cursor, and entity-set divergences.
  Removing an unrelated east neighbor is the clean negative control and
  passes before and after. The probe is preserved at
  `c/magma/trace/out/matrix_wire_support_loss_probe_1/summary.md`.
- The corrected negative/support pair and a powered two-wire cascade pass 3/3
  at `c/magma/trace/out/matrix_wire_support_loss_1/summary.md`. In the powered
  case, Java and Magma both remove the source-side wire, drain the survivor
  from 14 to 0, retain the lamp and its callback through rows 0..2, turn it
  off on row 3, and advance the same item through ages 1..5.
- The correctness promotion now covers 574 composite outcomes. Native tests
  additionally cover exact spawn cursors and bounded full-item-pool rejection;
  the complete runtime suite passes at
  `c/magma/trace/out/test_runtime_wire_support_loss.log`. GPU 1 performance
  passes at `c/magma/trace/out/perf_guard_wire_support_loss.json`: 5,046 scalar
  steps/s, 2.93M batched Blaze env-ticks/s, and 30.99 1080p CUDA fps. The
  support check is notification-driven and adds no idle work.

## 2026-08-02 (redstone-torch attachment loss)

- Added Java-authoritative floor and wall attachment validation to redstone
  torch neighbor updates. The support query reproduces Forge 1.11.2's
  directional slab, stair, farmland, snow, hopper, powered-block, fence,
  glass, stained-glass, and cobblestone-wall rules.
- Invalid support drops item 76:0 from either lit block 76 or unlit block 75
  with exact World/Math RNG and entity cursors, removes the torch, and retains
  the lit torch's second-ring redstone notifications. Full fixed-item-pool
  rejection remains atomic.
- The deliberate old-C floor and wall cases leave the torch floating; their
  unrelated-neighbor negative passes. The corrected set passes 3/3 at
  `c/magma/trace/out/matrix_redstone_torch_support_loss_candidate_1/summary.md`.
  Eight affected inverter, strong-power, and piston cases pass at
  `c/magma/trace/out/matrix_redstone_torch_support_loss_regression_1/summary.md`.
- The correctness promotion now covers 577 composite outcomes. The complete
  runtime suite passes at
  `c/magma/trace/out/test_runtime_redstone_torch_support_loss.log`. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_torch_support_loss.json`: 5,089
  scalar steps/s, 2.93M batched Blaze env-ticks/s, and 31.40 1080p CUDA fps.
  Attachment validation is notification-driven and adds no idle work.

## 2026-08-02 (lever and button attachment loss)

- Added exact stored-attachment validation to lever and stone/wood button
  neighbor updates. Another valid support face does not rescue a control whose
  serialized facing lost its own support. Directional Forge side solidity is
  shared with the torch implementation.
- Invalid support drops normalized item 69:0, 77:0, or 143:0 with exact
  World/Math RNG and entity cursors. Powered controls notify both their own
  neighborhood and attached support, preserving the lamp's exact +4 release.
  Already-saved stone/wood button callbacks remain queued after removal and
  drain as stale work without resurrecting the block.
- The deliberate old-C lever, wall-button, and ceiling-button cases leave the
  controls floating; the unrelated-neighbor lever control passes. The corrected
  cases pass 4/4 at
  `c/magma/trace/out/matrix_redstone_control_support_loss_1/summary.md`, and ten
  affected power/release/orientation cases pass at
  `c/magma/trace/out/matrix_redstone_control_support_loss_affected_1/summary.md`.
- Native coverage includes all eight lever metadata orientations,
  floor/wall/ceiling attachment, top slab, upside-down stair, eight-layer snow,
  hopper top, glass rejection, exact item lifecycle, and full-pool atomicity.
  The complete runtime suite passes at
  `c/magma/trace/out/test_runtime_redstone_control_support_loss.log`.
- The correctness promotion now covers 581 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_control_support_loss.json`: 4,851
  scalar steps/s, 2.93M batched Blaze env-ticks/s, and 31.16 1080p CUDA fps.
  Attachment validation remains notification-driven and adds no idle work.

## 2026-08-02 (pressure-plate support loss)

- Added Java-authoritative support validation for stone, wood, gold weighted,
  and iron weighted pressure plates. The block below must be a state whose
  captured 1.11.2 `isFullyOpaque` bit is true or any `BlockFence` ID.
- Losing support drops normalized item 70:0, 72:0, 147:0, or 148:0 with exact
  World/Math RNG and entity cursors. Powered plates notify both their own
  neighborhood and the former support neighborhood; restored callbacks remain
  stale until their original due tick. Fixed-item-pool rejection is atomic.
- The initial probe caught and rejected a bad edit offset that directly removed
  the plate. The corrected support edit gives four deliberate old-C failures
  and a passing oak-fence negative at
  `c/magma/trace/out/matrix_redstone_pressure_plate_support_loss_probe_2/summary.md`.
  The fixed family passes 5/5 at
  `c/magma/trace/out/matrix_redstone_pressure_plate_support_loss_candidate_2/summary.md`.
- Eleven existing Java cases for walkover, occupied rescheduling, mob/item
  sensitivity, ignored boat/XP/arrow controls, saved releases, and weighted
  strengths pass at
  `c/magma/trace/out/matrix_redstone_pressure_plate_support_loss_affected_1/summary.md`.
  The native aggregate exposed two old floating-plate fixtures; both now use
  physically valid support while retaining their falling/arrow sensitivity
  assertions. The corrected complete suite passes at
  `c/magma/trace/out/test_runtime_redstone_pressure_plate_support_loss.log`.
- The correctness promotion now covers 586 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_pressure_plate_support_loss.json`:
  4,951 scalar steps/s, 2.93M batched Blaze env-ticks/s, and 31.90 1080p CUDA
  fps. Support validation is notification-driven and adds no idle work.

## 2026-08-02 (repeater and comparator support loss)

- Added Java-exact floor-support validation for repeater blocks 93/94 and
  comparator blocks 149/150. The block below must have the captured 1.11.2
  stateful `isFullyOpaque` bit; fences and partial lower states do not qualify.
- Unsupported repeaters normalize to item 356:0 and comparators to item 404:0
  with exact World/Math RNG and entity cursors. Comparator tile state retires
  before directional output teardown. Saved diode callbacks remain stale until
  due, while powered output schedules the lamp's independent +4 release.
- The deliberate old-C matrix fails all five unsupported block/meta states and
  passes the top-slab negative at
  `c/magma/trace/out/matrix_redstone_diode_support_loss_probe_1/summary.md`.
  The corrected promotion passes 6/6 at
  `c/magma/trace/out/matrix_redstone_diode_support_loss_candidate_5/summary.md`.
  Affected saved timing, locking, analog/subtract, and powered piston teardown
  cases remain exact. A fixture-path typo found during that sweep was restored
  and its isolated rerun passes.
- Native coverage includes exact item lifetime, stale queues, powered lamp
  timing, comparator tile retirement, stateful full-opacity positives and
  negatives, fence rejection, and full-item-pool atomicity. The complete suite
  passes at `c/magma/trace/out/test_runtime_redstone_diode_support_loss.log`.
- The correctness promotion now covers 592 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_diode_support_loss.json`: 5,042 scalar
  steps/s, 2.93M batched Blaze env-ticks/s, and 30.69 1080p CUDA fps. Support
  validation remains notification-driven and adds no idle work.

## 2026-08-02 (tripwire-hook attachment loss)

- Added stored-facing wall validation to tripwire-hook neighbor updates using
  Forge's exact directional side-solid query. An unrelated valid side does not
  rescue a hook whose serialized attachment has disappeared.
- Support loss drops item 131:0 with exact World/Math RNG and entity cursors.
  An attached powered hook then runs the old block's teardown: the opposite
  hook and all three wires detach, east/west lamps queue in exact +4 order,
  and an already-saved hook callback remains stale at +10.
- The deliberate old-C run fails the unpowered and powered removals while the
  unrelated-support negative passes at
  `c/magma/trace/out/matrix_redstone_tripwire_hook_support_loss_probe_1/summary.md`.
  The corrected cases plus item, boat, XP, arrow, falling-block, small-fireball,
  player-crossing, and piston regressions pass 14/14 at
  `c/magma/trace/out/matrix_redstone_tripwire_hook_support_loss_affected_1/summary.md`.
- Native coverage includes all four hook facings, exact line/lamp/stale-queue
  teardown, item lifetime, and full-item-pool atomicity. The complete runtime
  suite passes at
  `c/magma/trace/out/test_runtime_redstone_tripwire_hook_support_loss.log`.
- The correctness promotion now covers 595 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_tripwire_hook_support_loss.json`:
  5,065 scalar steps/s, 2.93M batched Blaze env-ticks/s, and 31.36 1080p CUDA
  fps. Attachment validation is notification-driven and adds no idle work.

## 2026-08-02 (live tripwire removal lifecycle)

- Added the missing old-block lifecycle to direct runtime replacement of IDs
  131 and 132. The callback runs after the replacement is visible, matching
  Java's `Chunk.setBlockState` boundary in both controlled edits and ordinary
  player block edits.
- Direct removal of attached string now powers both hooks and both lamps in
  the initiating tick, retains one exact hook callback through +9, detaches at
  +10, and queues the east then west lamp releases for +14. Direct removal of
  an attached unpowered hook detaches the remaining line immediately, emits
  no item, and consumes the exact two `World.rand` detach-pitch draws.
- The first probe exposed a fixture-created pending hook callback. Moving the
  fixture to early staging with 12 controlled drain ticks produces an attached,
  queue-free pre-state and keeps the first difference at controlled tick zero.
  Both deliberate old-C failures are retained at
  `c/magma/trace/out/matrix_redstone_tripwire_live_break_probe_2/summary.md`;
  both exact corrections pass at
  `c/magma/trace/out/matrix_redstone_tripwire_live_break_candidate_2/summary.md`.
- The affected tripwire/piston/entity/projectile family passes 16/16 at
  `c/magma/trace/out/matrix_redstone_tripwire_live_break_affected_1/summary.md`.
  The complete native suite passes in 4:59 with a 280 MB peak at
  `c/magma/trace/out/test_runtime_redstone_tripwire_live_break.log`.
- The correctness promotion now covers 597 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_tripwire_live_break.json`: 5,095
  scalar steps/s, 2.93M batched Blaze env-ticks/s, and 31.86 1080p CUDA fps.
  The replacement hooks add no idle tick work, world scan, or allocation.

## 2026-08-02 (live tripwire on-add attachment)

- Added Java's missing `BlockTripWire.onBlockAdded` lifecycle to new ID-132
  placement in both controlled and ordinary player block-edit paths. The
  string state is visible before the bounded SOUTH-then-WEST hook scan.
- Filling the middle gap of a detached east-west line now finds the west hook,
  requires the opposite-facing east hook, attaches both hooks and all three
  strings in the initiating tick, and schedules only the west hook at +10.
  The callback persists through +9 and drains at +10 without changing the
  complete line or consuming RNG. Isolated string placement remains detached
  with no scheduled or entity side effect.
- The queue-free fixture is settled for 12 controlled setup ticks. Old magma
  fails the five-state completed-line mutation while the isolated negative
  passes at
  `c/magma/trace/out/matrix_redstone_tripwire_on_add_probe_1/summary.md`.
  Both corrected cases pass at
  `c/magma/trace/out/matrix_redstone_tripwire_on_add_candidate_2/summary.md`,
  and the expanded affected family passes 18/18 at
  `c/magma/trace/out/matrix_redstone_tripwire_on_add_affected_1/summary.md`.
- The correctness promotion now covers 599 composite outcomes. The complete
  native suite passes in 4:09 with a 282 MB peak at
  `c/magma/trace/out/test_runtime_redstone_tripwire_on_add.log`. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_tripwire_on_add.json`: 5,121 scalar
  steps/s, 2.94M batched Blaze env-ticks/s, and 32.04 1080p CUDA fps. The new
  scan has no idle-path work, allocation, or loaded-world traversal.

## 2026-08-02 (direct powered pressure-plate removal)

- Added Java's missing `BlockBasePressurePlate.breakBlock` lifecycle to direct
  replacement in both controlled and ordinary player edit paths. After the
  old powered state becomes air, it notifies the plate neighborhood and the
  support neighborhood in Java order.
- The focused fixture puts a lit lamp beside only the stone support. Removing
  powered stone 70:1, wood 72:1, gold 147:2, or iron 148:1 therefore queues
  the lamp at +4 and releases it on observation 3. Unpowered stone 70:0 has no
  second notification, scheduled work, item, entity, or RNG side effect.
- All four powered cases fail old magma at controlled tick zero while the
  unpowered negative passes at
  `c/magma/trace/out/matrix_redstone_pressure_plate_direct_break_probe_1/summary.md`.
  The corrected focused set passes 5/5 at
  `c/magma/trace/out/matrix_redstone_pressure_plate_direct_break_candidate_2/summary.md`.
  The affected plate family passes 21/21 at
  `c/magma/trace/out/matrix_redstone_pressure_plate_direct_break_affected_1/summary.md`.
- The correctness promotion now covers 604 composite outcomes. The complete
  native aggregate passes in 4:11 with a 283 MB peak at
  `c/magma/trace/out/test_runtime_redstone_pressure_plate_direct_break.log`.
  GPU 1 performance passes at
  `c/magma/trace/out/perf_guard_redstone_pressure_plate_direct_break.json`:
  5,152 scalar steps/s, 2.93M batched Blaze env-ticks/s, and 31.42 1080p CUDA
  fps. The new branch adds no idle work, allocation, item spawn, or RNG draw.

## 2026-08-02 (direct redstone-wire removal)

- Added Java's missing `BlockRedstoneWire.breakBlock` notification ring to
  direct wire replacement in both controlled and ordinary player edit paths.
  After the dust becomes air, each of its six adjacent positions notifies its
  own neighbors before the ordinary outer replacement notification.
- A powered 55:15 fixture now reaches a lit lamp adjacent only to the former
  support, queues its release at +4, and changes 124-to-123 on observation 3.
  Unpowered 55:0 remains a strict negative with no queue, entity, item, or RNG
  side effect. Old magma fails only the powered case at
  `c/magma/trace/out/matrix_redstone_wire_direct_break_probe_1/summary.md`;
  both corrected cases pass at
  `c/magma/trace/out/matrix_redstone_wire_direct_break_candidate_2/summary.md`.
- The represented wire/topology/support/strong-power/piston family passes
  35/35 at
  `c/magma/trace/out/matrix_redstone_wire_direct_break_affected_1/summary.md`.
  The first native aggregate found that the new negative fixture left a lamp
  beside a later torch support; clearing the completed fixture removed nine
  count-only false failures. The final suite passes in 3:58 with a 282 MB peak
  at `c/magma/trace/out/test_runtime_redstone_wire_direct_break_2.log`.
- The correctness promotion now covers 606 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_wire_direct_break.json`: 5,156 scalar
  steps/s, 2.93M batched Blaze env-ticks/s, and 31.94 1080p CUDA fps. The new
  traversal is replacement-driven and adds no idle work or allocation.

## 2026-08-02 (direct repeater/comparator removal)

- Added Java's missing diode `breakBlock` output notification to direct
  replacement of IDs 93, 94, 149, and 150. Comparator tile state now retires
  before the directional output-neighborhood callback in both controlled and
  ordinary player edit paths.
- Powered SOUTH-facing repeater 94:0 and comparator 149:8 fixtures strongly
  power a normal stone whose side lamp has no direct diode adjacency. Removing
  either diode queues the lamp at +4 and releases it on observation 3.
  Unpowered 93:0 and 149:0 remove without a queue, item, entity, or RNG change.
- The two powered rows fail old magma while the two unpowered controls pass at
  `c/magma/trace/out/matrix_redstone_diode_direct_break_probe_4/summary.md`.
  All four corrected cases pass at
  `c/magma/trace/out/matrix_redstone_diode_direct_break_candidate_1/summary.md`.
  Fourteen lifecycle-adjacent direct, scheduled, saved, support-loss, and
  piston cases pass at
  `c/magma/trace/out/matrix_redstone_diode_direct_break_affected_1/summary.md`.
- Native coverage includes all four direct states, exact comparator retirement,
  absolute +4 dispatch, and zero item/RNG work. The complete suite passes in
  4:12 with a 286 MB peak at
  `c/magma/trace/out/test_runtime_redstone_diode_direct_break.log`.
- The correctness promotion now covers 610 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_diode_direct_break.json`: 5,142 scalar
  steps/s, 2.93M batched Blaze env-ticks/s, and 30.83 1080p CUDA fps. The
  lifecycle runs only on direct diode replacement and adds no idle work.

## 2026-08-02 (direct powered-repeater placement)

- Added the shared diode `onBlockAdded` output notification to direct
  placement of repeater IDs 93 and 94 in both controlled and ordinary player
  edit paths.
- Placing SOUTH-facing powered 94:0 now strongly powers its north output stone
  and lights a lamp adjacent only to that stone in the same controlled tick.
  Unpowered 93:0 leaves the lamp dark with no queue, item, entity, or RNG work.
- Old magma fails only the powered row at
  `c/magma/trace/out/matrix_redstone_repeater_direct_add_probe_1/summary.md`.
  Both corrected rows pass at
  `c/magma/trace/out/matrix_redstone_repeater_direct_add_candidate_1/summary.md`;
  ten direct add/remove, direction, delay, and saved-work cases pass at
  `c/magma/trace/out/matrix_redstone_repeater_direct_add_affected_1/summary.md`.
- Native powered/unpowered placement coverage passes in the complete 3:55
  suite with a 286 MB peak at
  `c/magma/trace/out/test_runtime_redstone_repeater_direct_add.log`.
- The correctness promotion now covers 612 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_repeater_direct_add.json`: 5,145
  scalar steps/s, 2.93M batched Blaze env-ticks/s, and 30.66 1080p CUDA fps.
  The placement callback adds no idle work or allocation.

## 2026-08-02 (direct comparator placement)

- Added Java-order direct comparator placement to both controlled and ordinary
  player edit paths: inherited directional notification first, output-zero
  tile creation second. Capacity is preflighted before any world mutation.
- Aligned SOUTH-facing 149:1 placement now notifies a stale powered downstream
  repeater, which queues its release at +2 and hands its lamp a +6 release.
  Rotated 149:3 creates the tile but queues no work. Neither path consumes RNG
  or spawns an item.
- Old magma fails the aligned row while the rotated control passes at
  `c/magma/trace/out/matrix_redstone_comparator_direct_add_probe_2/summary.md`.
  Both corrected rows pass at
  `c/magma/trace/out/matrix_redstone_comparator_direct_add_candidate_1/summary.md`;
  14 direct, scheduled, saved, support-loss, and piston comparator cases pass
  at
  `c/magma/trace/out/matrix_redstone_comparator_direct_add_affected_1/summary.md`.
- Native coverage passes in the complete 3:57 suite with a 285 MB peak and
  zero swap at
  `c/magma/trace/out/test_runtime_redstone_comparator_direct_add.log`.
- The correctness promotion now covers 614 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_comparator_direct_add.json`: 5,030
  scalar steps/s, 2.93M batched Blaze env-ticks/s, and 31.66 1080p CUDA fps.
  The placement lifecycle adds no idle work or allocation.

## 2026-08-02 (direct redstone-wire placement)

- Added the complete Java `BlockRedstoneWire.onBlockAdded` notification
  traversal after connected-component recomputation in both controlled and
  ordinary player edit paths. It covers UP/DOWN centers, horizontal wire
  neighborhoods, and one-block vertical wire neighbors.
- A zero-power wire above a support now reaches a diagonal stale powered
  repeater and queues its exact +2 release. The unpowered counterpart remains
  queue-free. Neither path consumes RNG or spawns an item.
- With only the two new call sites disabled, the powered row fails while the
  control passes at
  `c/magma/trace/out/matrix_redstone_wire_direct_add_probe_5/summary.md`.
  Both corrected rows pass at
  `c/magma/trace/out/matrix_redstone_wire_direct_add_candidate_2/summary.md`;
  14 flat, vertical, branch, support, removal, repeater, and comparator cases
  pass at
  `c/magma/trace/out/matrix_redstone_wire_direct_add_affected_1/summary.md`.
- Native coverage passes in the complete 4:09 suite with a 287 MB peak and
  zero swap at
  `c/magma/trace/out/test_runtime_redstone_wire_direct_add.log`.
- The correctness promotion now covers 616 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_wire_direct_add.json`: 5,108 scalar
  steps/s, 2.93M batched Blaze env-ticks/s, and 31.90 1080p CUDA fps. The
  fixed traversal runs only on direct wire placement and adds no idle work.

## 2026-08-02 (direct redstone-lamp placement)

- Split direct lamp placement from the delayed neighbor-change lifecycle in
  both controlled and ordinary edit paths. `BlockRedstoneLight.onBlockAdded`
  now immediately selects lit or unlit state from current power.
- Requested lit 124 without power settles to 123 with no +4 callback.
  Requested unlit 123 with direct power settles to 124, while the unpowered
  control remains 123. All paths preserve RNG and emit no item.
- Old magma fails only the lit-unpowered state at
  `c/magma/trace/out/matrix_redstone_lamp_direct_add_probe_1/summary.md`. All
  three fixed states pass at
  `c/magma/trace/out/matrix_redstone_lamp_direct_add_candidate_1/summary.md`;
  17 lamp, lever, dust-topology, and indirect-power cases pass at
  `c/magma/trace/out/matrix_redstone_lamp_direct_add_affected_1/summary.md`.
- Native coverage passes in the complete 4:10 suite with a 287 MB peak and
  zero swap at
  `c/magma/trace/out/test_runtime_redstone_lamp_direct_add.log`.
- The correctness promotion now covers 619 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_lamp_direct_add.json`: 5,114 scalar
  steps/s, 2.93M batched Blaze env-ticks/s, and 31.90 1080p CUDA fps. Direct
  placement adds no idle work or allocation.

## 2026-08-02 (direct redstone-torch placement)

- Moved the lit-torch six-center on-add traversal before the outer placement
  notification in both controlled and ordinary edit paths, matching the Java
  `Chunk.setBlockState` then `World.markAndNotifyBlock` lifecycle.
- Torch support checks now see represented strong power through a normal
  support. A lit floor torch added over a repeater-powered support queues its
  own +2 update in addition to the two priority-ordered diode callbacks; the
  unlit placement control remains queue-free. RNG, items, raw blocks, and
  block light remain exact.
- Old magma fails the lit row while the unlit control passes at
  `c/magma/trace/out/matrix_redstone_torch_direct_add_probe_2/summary.md`.
  Both corrected rows pass at
  `c/magma/trace/out/matrix_redstone_torch_direct_add_candidate_1/summary.md`;
  all 16 affected placement, support-loss, strong-power, saved-callback,
  wall, and burnout cases pass at
  `c/magma/trace/out/matrix_redstone_torch_direct_add_affected_1/summary.md`.
- Native coverage passes in the complete 3:55 suite with a 288 MB peak and
  zero swap at
  `c/magma/trace/out/test_runtime_redstone_torch_direct_add.log`.
- The correctness promotion now covers 621 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_torch_direct_add.json`: 5,165 scalar
  steps/s, 2.93M batched Blaze env-ticks/s, and 31.47 1080p CUDA fps. The new
  work is placement- and callback-driven and adds no idle scan or allocation.

## 2026-08-02 (saved indirect redstone-torch callbacks)

- Expanded the save-state capsule proof for floor redstone-torch callbacks to
  ordinary normal supports when all six captured power providers and the
  bounded notification neighborhood are represented.
- Java 1.11.2 repeater locking reads only diode side inputs. The corresponding
  runtime proof now admits inert normal-cube side neighbors, allowing a stable
  powered repeater embedded in the platform to drive the torch support.
- A restored lit torch above that indirectly powered support turns off at +2.
  A valid stale lit callback and an indirectly powered unlit callback both
  drain at +2 without mutation or toggle-history changes.
- Old magma fails the two lit rows while the unlit control passes at
  `c/magma/trace/out/matrix_redstone_torch_saved_indirect_probe_1/summary.md`.
  All three corrected rows pass at
  `c/magma/trace/out/matrix_redstone_torch_saved_indirect_candidate_2/summary.md`;
  all 19 affected torch cases pass at
  `c/magma/trace/out/matrix_redstone_torch_saved_indirect_affected_1/summary.md`.
- Native coverage passes in the complete 4:10 suite with a 289 MB peak and
  zero swap at
  `c/magma/trace/out/test_runtime_redstone_torch_saved_indirect.log`.
- The correctness promotion now covers 624 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_torch_saved_indirect.json`: 5,069
  scalar steps/s, 2.93M batched Blaze env-ticks/s, and 31.45 1080p CUDA fps.
  The fix changes only saved-callback admission and adds no idle work or
  allocation.

## 2026-08-02 (saved wall redstone-torch callbacks)

- Extended the state-capsule torch proof from floor metadata 5 to EAST, WEST,
  SOUTH, and NORTH wall metadata 1/2/3/4. Each state resolves its exact support
  offset and retains the represented-provider and bounded-notification checks.
- Added isolated lit/powered and unlit/unpowered fixtures for every facing.
  All eight callbacks preserve absolute due time, priority, order, orientation,
  toggle history, raw blocks, and all 10,625 block-light cells.
- A deliberately disabled wall-admission run makes the EAST off/on pair fail
  at
  `c/magma/trace/out/matrix_redstone_torch_wall_saved_probe_2/summary.md`.
  All eight corrected rows pass at
  `c/magma/trace/out/matrix_redstone_torch_wall_saved_candidate_2/summary.md`;
  all 27 affected torch rows pass at
  `c/magma/trace/out/matrix_redstone_torch_wall_saved_affected_1/summary.md`.
- Capsule selftests cover all four supports plus missing-support rejection.
  Native coverage exercises all eight transitions and passes in the complete
  4:03 suite with a 291 MB peak and zero swap at
  `c/magma/trace/out/test_runtime_redstone_torch_wall_saved.log`.
- The correctness promotion now covers 632 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_torch_wall_saved.json`: 5,109 scalar
  steps/s, 2.93M batched Blaze env-ticks/s, and 30.37 1080p CUDA fps. The
  feature changes cold-load validation only and adds no per-tick work.

## 2026-08-02 (directional non-cube saved torch supports)

- Ported Forge 1.11.2's directional `Block.isSideSolid` torch-support surface
  into the save-state proof and runtime admission. The promoted cases cover a
  top slab, upside-down stair, eight-layer snow, hopper UP face, farmland
  horizontal face, and the solid face of a stair with actual-shape handling.
- Added six exact saved block-75 callbacks due at +2. Each relights to block 76
  with unchanged orientation, exact queue/order/toggle state, and exact raw
  block volume. The deliberate old-C run omits all six callbacks at
  `c/magma/trace/out/matrix_redstone_torch_saved_directional_support_probe_1/summary.md`.
  The focused correction passes 6/6 at
  `c/magma/trace/out/matrix_redstone_torch_saved_directional_support_candidate_3/summary.md`,
  and the complete affected torch set passes 33/33 at
  `c/magma/trace/out/matrix_redstone_torch_saved_directional_support_affected_1/summary.md`.
- Corrected hopper light opacity in the promoted gameplay light lookup. Java's
  registered hopper opacity is zero, so a redstone torch emitting 7 yields
  exact block light 6 in the hopper cell. The standalone light suite now locks
  both values and passes.
- Capsule selftests and native coverage admit all six valid supports and reject
  bottom slab, bottom stair, partial snow, hopper side, and wrong stair face.
  The optimized full native aggregate passes in 4:03 with a 291 MB peak and
  zero swap at
  `c/magma/trace/out/test_runtime_redstone_torch_saved_directional_support_optimized.log`.
- The correctness promotion now covers 638 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_torch_saved_directional_support.json`:
  5,095 scalar steps/s, 2.93M batched Blaze env-ticks/s, and 29.29 1080p CUDA
  fps. The change is cold-load validation plus one block-property branch; it
  adds no idle scan, heap allocation, or measurable throughput regression.

## 2026-08-02 (saved torch top-support exceptions)

- Added exact saved block-75 callbacks on every explicit Forge
  `Block.canPlaceTorchOnTop` exception: oak/nether/spruce/birch/jungle/
  dark-oak/acacia fences, glass, stained glass, and cobblestone wall.
- Disabling only the exception admission omits all ten callbacks and produces
  ten deliberate parity failures at
  `c/magma/trace/out/matrix_redstone_torch_saved_top_exceptions_probe_1/summary.md`.
  The consolidated corrected sweep passes 10/10 at
  `c/magma/trace/out/matrix_redstone_torch_saved_top_exceptions_candidate_3/summary.md`
  with exact queue dispatch, 75:5-to-76:5 transition, and all 10,625 block and
  light cells.
- The first admitted sweep found that fence IDs 188..192 and stained-glass ID
  95 incorrectly inherited opacity 255. Promoted stained glass with hardness
  0.3/opacity zero and corrected every newer `BlockFence` to opacity zero.
  The expanded 169-ID property oracle passes Java == CPU == CUDA.
- Capsule/native coverage exhausts all ten valid tops and rejects fence-side
  attachment. The complete native aggregate passes with a 291 MB peak and
  zero swap at
  `c/magma/trace/out/test_runtime_redstone_torch_saved_top_exceptions.log`.
- The correctness promotion now covers 648 composite outcomes. GPU 1
  performance passes at
  `c/magma/trace/out/perf_guard_redstone_torch_saved_top_exceptions.json`:
  5,018 scalar steps/s, 2.93M batched Blaze env-ticks/s, and 27.35 1080p CUDA
  fps. The feature adds no idle scan or heap allocation.

## 2026-08-02 (tripwire shears disarm)

- Implemented Java's shears-only `BlockTripWire.onBlockHarvested` ordering.
  The DISARMED bit reaches the removed-block callback before the string is
  replaced by air, so an attached line detaches without its ordinary powered
  alarm pulse and retains only the west hook's +10 recheck.
- The complete survival harvest matches shears metadata 0-to-1 wear, 0.005
  exhaustion, one exact string EntityItem, six World RNG draws, Math.random
  cursor, entity-ID cursor, scheduled work, raw blocks, and block light.
  Physical CPU and CUDA mining now charge the same harvest exhaustion and
  carry the held tool into the runtime break lifecycle.
- Added exact attached and detached tripwire selection boxes. The attached
  box spans Y 1/16 through 5/32; the detached box spans Y 0 through 1/2.
- Omitting only the DISARMED bit produces the deliberate failure at
  `c/magma/trace/out/matrix_redstone_tripwire_shears_probe_1/summary.md`.
  The corrected tripwire case and affected stone-mining regression pass 2/2
  at
  `c/magma/trace/out/matrix_redstone_tripwire_shears_affected_1/summary.md`.
- The full native runtime suite passes in 5:04 with a 284 MiB peak and zero
  swap at `c/magma/trace/out/test_runtime_redstone_tripwire_shears.log`.
  Correctness now covers 649 promoted composite outcomes. GPU 1 performance
  passes at `c/magma/trace/out/perf_guard_redstone_tripwire_shears.json`:
  4,364 scalar steps/s, 2.92M batched Blaze env-ticks/s, and 26.83 1080p CUDA
  fps. The change is harvest-event-only and adds no idle scan or allocation.

## 2026-08-02 (redstone-diode collision geometry)

- Added the inherited `BlockRedstoneDiode` full-footprint, 1/8-high collision
  box for unpowered/powered repeaters 93/94 and comparators 149/150. Selection
  geometry now uses the same source constant in CPU and CUDA paths.
- In the deliberate old-C pair, a downward moving piston leaves the pig at Y
  `79.09000002384185` instead of Java's `79.125`; this is the only divergent
  state field, while raw blocks and block light remain exact. Evidence is at
  `c/magma/trace/out/matrix_redstone_piston_diode_shape_probe_1/summary.md`.
- Both corrected Java-vs-magma cases pass with 25 matching state features and
  all 10,625 raw block/light cells exact at
  `c/magma/trace/out/matrix_redstone_piston_diode_shape_candidate_1/summary.md`.
  Native coverage exhausts all 64 ID/metadata combinations.
- The full native runtime suite passes in 4:28 with a 285 MiB peak and zero
  swap at `c/magma/trace/out/test_runtime_redstone_piston_diode_shape.log`.
  Correctness now covers 651 promoted composite outcomes. GPU 1 performance
  passes at `c/magma/trace/out/perf_guard_redstone_piston_diode_shape.json`:
  5,070 scalar steps/s, 2.94M batched Blaze env-ticks/s, and 31.20 1080p CUDA
  fps. The branch is active-piston-only and adds no idle scan or allocation.

## 2026-08-02 (brewing-stand collision geometry)

- Added Java's exact compound brewing-stand collision: a centered 1/8-wide
  stem reaching 7/8 plus the full-footprint 1/8 base, in Java insertion order.
  Selection remains base-only and is mirrored in the CPU and CUDA paths.
- The clean old-magma proof fails only the two entity trajectories while all
  10,625 raw block and light cells remain exact at
  `c/magma/trace/out/matrix_redstone_piston_brewing_stand_shape_probe_3/summary.md`.
  The corrected center and side-lane cases pass their exact Java behavior and
  raw gates at
  `c/magma/trace/out/matrix_redstone_piston_brewing_stand_shape_candidate_1/summary.md`.
- These two rows remain whole-state diagnostics because the brewing inventory
  tile is not represented. This promotion covers geometry only; the F-03
  fuel, timer, recipe, bottle, effect, UI, and comparator work remains queued.
- Native coverage checks both collision components for all eight bottle-bit
  states and selection for all eight states. The full runtime suite passes in
  5:19 with a 286 MiB peak and zero swap at
  `c/magma/trace/out/test_runtime_brewing_stand_shape.log`.
- Correctness now covers 653 promoted composite behavior/raw outcomes. GPU 1
  performance passes at `c/magma/trace/out/perf_guard_brewing_stand_shape.json`:
  4,329 scalar steps/s, 2.92M batched Blaze env-ticks/s, and 27.05 1080p CUDA
  fps. The collision branch is bounded to occupied static cells and adds no
  idle scan or allocation.

## 2026-08-02 (piston-base, closed-shulker, and ordinary-diode collision, 657 cases)

- Added Java's exact static box for normal/sticky piston bases. Retracted
  states are full cubes; each extended state removes the facing-side quarter.
  CPU and CUDA selection use the same twelve valid ID/facing/extension shapes.
- The deliberate old-magma rows fail only pig Y: Java stops at `80` on the
  retracted normal base and `79.75` on the extended-UP sticky base, while old
  magma reaches `79.09000002384185`. Raw blocks and block light are exact at
  `c/magma/trace/out/matrix_redstone_piston_base_shape_probe_1/summary.md`.
- Both corrected strict rows pass with 25 matching simulated features and all
  10,625 block/light cells exact at
  `c/magma/trace/out/matrix_redstone_piston_base_shape_candidate_1/summary.md`.
  Native coverage checks both IDs, all six facings, retracted/extended states,
  and the four horizontal empty-quarter negatives.
- Added closed shulker boxes 219..234 as full-cube static collision for the
  represented closed tile state. Old magma pushes the downward-piston pig to
  Y `79.09000002384185`; Java stops at `80`, with exact container NBT, blocks,
  block light, queues, and RNG at
  `c/magma/trace/out/matrix_redstone_piston_closed_shulker_shape_probe_1/summary.md`.
  The corrected strict row passes at
  `c/magma/trace/out/matrix_redstone_piston_closed_shulker_shape_candidate_1/summary.md`.
  This does not claim animated open-lid collision or lid pushing.
- Ported the same 1/8-high repeater/comparator box into ordinary player
  movement. In the deliberate airborne-insertion probe, Java lands at Y
  `78.125` on tick 10 while old magma passes through to the platform at Y
  `78`; the old behavior fails only on magma at
  `c/magma/trace/out/matrix_ordinary_player_diode_landing_probe_behavior_2/summary.md`.
  The corrected row has 25 matching features, zero divergences, and all
  10,625 block/light cells exact at
  `c/magma/trace/out/matrix_ordinary_player_diode_landing_candidate_1/summary.md`.
  The affected piston-player, tripwire-player, and pressure-plate-player
  family passes 4/4 at
  `c/magma/trace/out/matrix_ordinary_player_diode_landing_affected_1/summary.md`.
- Focused native coverage exhausts all four diode IDs and 16 metadata values,
  retains a collision-free redstone-wire control, and passes at
  `c/magma/trace/out/test_player_ctl_ordinary_diode_collision.log`. The shared
  CPU/CUDA player-survival driver agrees on all 2,432 output lines at
  `c/magma/trace/out/cpu_cuda_player_survival_ordinary_diode.log`.
- The combined full native suite passes in 5:51 with a 287 MiB peak and zero
  swap at
  `c/magma/trace/out/test_runtime_piston_base_closed_shulker_player_diode.log`.
  After the unrelated 56-thread job ended, the uncontaminated GPU 1 guard
  passed at
  `c/magma/trace/out/perf_guard_piston_base_closed_shulker_player_diode.json`:
  4,636 scalar steps/s, 2.92M batched Blaze env-ticks/s, and 28.19 1080p CUDA
  fps. Correctness now covers 657 promoted composite behavior/raw outcomes.

## 2026-08-02 (ordinary thin-surface collision, 665 cases)

- Ported exact ordinary-player collision for brewing stand 117, enchanting
  table 116, farmland 60, and grass path 208 into the shared CPU/CUDA movement
  path. Brewing contributes its ordered centered 7/8 stem plus 1/8 base;
  enchanting is 3/4 high; farmland and path are 15/16 high.
- The deliberate old-C fixtures fail at the first landing coordinate while
  retaining exact raw blocks and block light. Corrected brewing, enchanting,
  farmland, and path outcomes pass their Java behavior/raw gates at
  `c/magma/trace/out/matrix_ordinary_player_brewing_stand_landing_candidate_1/summary.md`,
  `c/magma/trace/out/matrix_ordinary_player_enchanting_table_landing_candidate_1/summary.md`,
  and
  `c/magma/trace/out/matrix_ordinary_player_farmland_grass_path_landing_candidate_1/summary.md`.
  Brewing remains state-diagnostic only for the known unsupported inventory
  tile; the other three rows have 25 matches and zero divergences.
- The consolidated affected player family passes 8/8 at
  `c/magma/trace/out/matrix_ordinary_player_fixed_surface_shapes_affected_1/summary.md`.
  Native coverage exhausts all represented metadata values and both brewing
  lanes at `c/magma/trace/out/test_player_ctl_ordinary_surface_shapes.log`;
  CPU/CUDA agree on 2,432 lines at
  `c/magma/trace/out/cpu_cuda_player_survival_fixed_surface_shapes.log`.
- Added exact ordinary-player collision for all four single-slab IDs and all
  metadata states, 1/16-high carpet, metadata-height snow layers, and the
  bitten inset cake box. Their deliberate old-C probes fail only player
  physics while preserving exact blocks and light; all four corrected outcomes
  pass Java behavior/raw gates.
- The expanded affected player family passes 12/12 at
  `c/magma/trace/out/matrix_ordinary_player_thin_surface_shapes_affected_1/summary.md`.
  Native shape coverage passes at
  `c/magma/trace/out/test_player_ctl_ordinary_slab_carpet_snow_cake_collision.log`,
  and CPU/CUDA agree on all 2,432 lines at
  `c/magma/trace/out/cpu_cuda_player_survival_slab_carpet_snow_cake.log`.
- The full native suite passes in 4:56 with a 288 MiB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_thin_surface_shapes.log`. The clean
  GPU 1 guard passes at
  `c/magma/trace/out/perf_guard_ordinary_thin_surface_shapes.json`: 4,671
  scalar steps/s, 2.93M batched Blaze env-ticks/s, and 24.38 1080p CUDA fps.
  Correctness now covers 665 promoted composite behavior/raw outcomes.

## 2026-08-02 (ordinary bed/detector/frame/chest/trapdoor/chorus, 671 cases)

- Ported exact ordinary-player collision for beds, both daylight detectors,
  end portal frames with optional eye, ender chests, both trapdoors, and
  actual-state chorus plants into the shared CPU/CUDA movement collector.
- Six deliberate old-C jump fixtures fail their exact landing height/tick while
  retaining exact raw blocks and block light. All corrected rows are strict at
  `c/magma/trace/out/matrix_ordinary_player_bed_daylight_frame_ender_chest_candidate_1/summary.md`
  and
  `c/magma/trace/out/matrix_ordinary_player_trapdoor_chorus_candidate_1/summary.md`.
  The affected player family passes 12/12 at
  `c/magma/trace/out/matrix_ordinary_player_bed_daylight_frame_ender_chest_trapdoor_chorus_affected_1/summary.md`.
- Focused native coverage exhausts all represented metadata, optional-eye,
  trapdoor-panel, and chorus-arm states. CPU/CUDA agree on all 2,432 outputs at
  `c/magma/trace/out/cpu_cuda_player_survival_bed_daylight_frame_ender_chest_trapdoor_chorus.log`.
  The full native suite passes under elevated host load in 6:24 with a 288 MiB
  peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_bed_daylight_frame_ender_chest_trapdoor_chorus.log`.
  After the unrelated 56-thread stage ended, the clean GPU 1 guard passed at
  `c/magma/trace/out/perf_guard_ordinary_bed_daylight_frame_ender_chest_trapdoor_chorus.json`:
  4,765 scalar steps/s, 2.93M Blaze env-ticks/s, and 30.50 1080p CUDA fps.
  Correctness now covers 671 promoted composite behavior/raw outcomes.

## 2026-08-02 (ordinary cauldron/hopper/flower-pot, 674 cases)

- Ported cauldron 118 and hopper 154 as exact five-box base-plus-rim collision
  and flower pot 140 as its centered 3/8-high box in ordinary-player movement.
- Fixed the earlier causal save-state divergence: live placement now
  materializes empty hopper and flower-pot tiles immediately. The focused
  causal rerun makes those tile features exact before the remaining physics
  divergence; after the geometry change all three corrected Java-vs-C rows are
  strict at
  `c/magma/trace/out/matrix_ordinary_player_cauldron_hopper_flower_pot_candidate_1/summary.md`.
- The affected player family passes 9/9 at
  `c/magma/trace/out/matrix_ordinary_player_cauldron_hopper_flower_pot_affected_1/summary.md`.
  Focused native coverage passes, and CPU/CUDA agree on all 2,432 outputs at
  `c/magma/trace/out/cpu_cuda_player_survival_cauldron_hopper_flower_pot.log`.
- The combined final-source native suite passes in 5:45 under elevated host
  load with a 288 MiB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_cactus_collision_damage.log`. After
  the unrelated 56-thread job ended, GPU 1 performance passed at
  `c/magma/trace/out/perf_guard_ordinary_cauldron_hopper_flower_pot_cactus.json`:
  4,775 scalar steps/s, 2.93M Blaze env-ticks/s, and 30.67 1080p CUDA fps.
  These three strict rows bring the promoted count to 674.

## 2026-08-02 (ordinary cactus collision and contact damage, 676 cases)

- Added cactus 81's exact 1/16 horizontal inset and 15/16 height to the shared
  ordinary-player collision collector. Native coverage exhausts all 16
  metadata states and proves the collision callback fires on the top surface.
- Ported `BlockCactus.onEntityCollidedWithBlock` through the authoritative
  one-point armor/hurt-resistance path, adding the default 0.1 exhaustion only
  for an accepted hit. The old-C jump probe first fails health, hurt time, and
  exhaustion at tick 1 and later lands at Y 79 on the old full cube. A causal
  damage-only rerun makes tick 1 exact before the geometry change.
- The stationary strict fixture reproduces damage at ticks 1/11/21, natural
  regeneration at ticks 10/20, 25 matching state features, and exact blocks
  and light. The jump fixture proves inset landing/contact but is explicitly
  state-diagnostic: the real Java client's self-velocity packet arrived at
  tick 3 in two captures and tick 4 in another. C is not fitted to that
  asynchronous race. Both behavior/raw proofs and ten related strict rows pass
  at
  `c/magma/trace/out/matrix_ordinary_player_cactus_affected_1/summary.md`.
- Focused player tests pass at
  `c/magma/trace/out/test_player_ctl_ordinary_cactus_collision_damage.log`;
  CPU/CUDA agree on all 2,432 outputs at
  `c/magma/trace/out/cpu_cuda_player_survival_cactus.log`. The final native and
  performance evidence is shared with the preceding promotion. One strict and
  one bounded-diagnostic outcome bring the promoted total to 676.

## 2026-08-02 (end-rod, skull, lily, and chest player collision candidates)

- Added exact ordinary-player collision for all end-rod axes, all six skull
  facings, lily pads, and joined ordinary/trapped chests in the shared CPU/CUDA
  movement collector.
- Fixed the earlier causal tile gaps: live skull placement creates default
  type 0/rotation 0, and live chest placement creates an empty 27-slot tile.
  Tile-only reruns make these state lists exact before the old geometry still
  fails, so the corrected landing result is not hiding an earlier divergence.
- Five corrected Java-vs-C rows are strict at
  `c/magma/trace/out/matrix_ordinary_player_end_rod_skull_candidate_1/summary.md`
  and
  `c/magma/trace/out/matrix_ordinary_player_lily_chests_candidate_2/summary.md`.
  The combined affected family passes 18/18 at
  `c/magma/trace/out/matrix_ordinary_player_lily_chests_affected_1/summary.md`.
- Focused tests exhaust metadata and chest join directions, including the
  ordinary/trapped cross-registry negative. CPU and CUDA agree on all 2,432
  emitted values at
  `c/magma/trace/out/cpu_cuda_player_survival_end_rod_skull_lily_chests.log`.
  The exact-source native suite passes in 6:27 with a 289 MiB peak and zero
  swap at `c/magma/trace/out/test_runtime_ordinary_lily_chests_final.log`.
- GPU 1 was shared with another process, so no throughput sample from this
  interval is admitted. These five strict outcomes remain candidates and the
  promoted total remains 676 until a clean performance recapture passes.

## 2026-08-02 (ordinary actual-state stair collision candidate)

- Ported actual-state collision for all 14 stair IDs into ordinary-player
  movement, including top/bottom straight, inner-left/right, and
  outer-left/right shapes.
- Added a bounded forward-then-brake oracle tape that moves the complete player
  box onto the low half. Java lands at Y 78.5 on tick 9 and grounds on tick 10;
  old magma lands at Y 79 on tick 8. The corrected strict row passes at
  `c/magma/trace/out/matrix_ordinary_player_stair_candidate_1/summary.md`.
- The 15-case affected family passes at
  `c/magma/trace/out/matrix_ordinary_player_stair_affected_1/summary.md`.
  Focused native tests cover every stair ID and actual-state shape; CPU/CUDA
  agree on 2,432 values at
  `c/magma/trace/out/cpu_cuda_player_survival_ordinary_stairs.log`.
- The native suite passes in 5:09 with a 289 MiB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_stairs.log`. GPU 1 remains at 100%
  utilization under the user's other process, so this strict row remains a
  performance-pending candidate and the promoted total stays 676.

## 2026-08-02 (ordinary connected-pane collision candidate)

- Added exact center-post plus neighbor-arm collision for iron bars, glass
  panes, and stained panes to the shared ordinary-player CPU/CUDA collector.
  Connectivity includes pane/glass/default-full-cube neighbors, Forge's
  farmland, full snow-layer, and redstone-block side-solid exceptions, and
  actual-state stair sides.
- The deliberate old-C north approach first diverges only in player physics at
  tick 1: Java reaches Z 7.862500011920929 and stops on tick 4, while the old
  full-cube fallback stops at Z 8.300000011920929. Raw blocks and light remain
  exact at
  `c/magma/trace/out/matrix_ordinary_player_glass_pane_approach_probe_1/summary.md`.
- The corrected approach and centered landing pass strict at
  `c/magma/trace/out/matrix_ordinary_player_glass_pane_candidate_1/summary.md`;
  the affected family passes 14/14 at
  `c/magma/trace/out/matrix_ordinary_player_glass_pane_affected_1/summary.md`.
  Native coverage exhausts all pane IDs and metadata plus every connection
  direction and side-solid control. CPU/CUDA agree on 2,432 values at
  `c/magma/trace/out/cpu_cuda_player_survival_ordinary_panes.log`.
- The aggregate suite passes in 4:41 with a 289 MiB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_panes.log`. GPU 1 is shared, so this
  strict row remains performance-pending and the promoted total stays 676.

## 2026-08-02 (ordinary piston-base/anvil/dragon-egg candidates)

- Added exact ordinary-player collision for normal/sticky piston bases in
  every retracted and six-facing extended state, both anvil axes, and the
  dragon egg's 1/16 horizontal inset.
- Replaced the first timed-edit fixtures after they exposed Java client block
  update latency. The final obstacles are pre-staged valid save states, so the
  approach starts outside a block already known to both engines. A controlled
  old-branch replay fails all three at tick 1 on full-cube Z 8.300000011920929
  with exact blocks and light at
  `c/magma/trace/out/matrix_ordinary_player_piston_base_anvil_dragon_egg_probe_static_old_1/summary.md`.
- All three corrected rows pass strict at
  `c/magma/trace/out/matrix_ordinary_player_piston_base_anvil_dragon_egg_candidate_static_2/summary.md`,
  and the affected family passes 16/16 at
  `c/magma/trace/out/matrix_ordinary_player_piston_base_anvil_dragon_egg_affected_1/summary.md`.
  Native tests exhaust metadata; CPU/CUDA agree on 2,432 outputs at
  `c/magma/trace/out/cpu_cuda_player_survival_ordinary_piston_base_anvil_dragon_egg.log`.
- The aggregate suite passes in 4:52 with a 289 MiB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_piston_base_anvil_dragon_egg.log`.
  These three strict rows remain performance-pending, so the promoted total
  stays 676.

## 2026-08-02 (ordinary fence and fence-gate candidates)

- Ported exact ordinary-player collision for all seven fence IDs and all six
  fence-gate IDs. This includes 1.5-high post/arm geometry, wood-vs-nether
  connections, exact opaque-neighbor exclusions, closed gate axes, and
  collision-free open gates.
- Staged old-C proofs show the closed gate, open gate, and isolated spruce
  fence all incorrectly stop at full-cube Z 8.300000011920929 on tick 1, with
  raw blocks and light exact at
  `c/magma/trace/out/matrix_ordinary_player_fence_gate_fence_probe_1/summary.md`.
  The corrected strict rows pass at
  `c/magma/trace/out/matrix_ordinary_player_fence_gate_fence_candidate_1/summary.md`.
- The affected family passes 16/16 at
  `c/magma/trace/out/matrix_ordinary_player_fence_gate_fence_affected_1/summary.md`.
  Native coverage exhausts IDs, metadata, arms, and connection controls;
  CPU/CUDA agree on 2,432 outputs at
  `c/magma/trace/out/cpu_cuda_player_survival_ordinary_fences_gates.log`.
- The aggregate passes in 4:41 with a 289 MiB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_fences_gates.log`. These three
  strict rows remain performance-pending, so the promoted total stays 676.

## 2026-08-02 (ordinary door, ladder, and cocoa candidates)

- Added exact paired-half collision for all seven door IDs, all ladder panels,
  and every cocoa age/facing pod to ordinary-player movement. The old-C four-
  case approach fails all shapes at full-cube Z 8.300000011920929 on tick 1,
  with blocks and light exact at
  `c/magma/trace/out/matrix_ordinary_player_door_ladder_cocoa_probe_1/summary.md`.
- The corrected geometry exposed a real second gap rather than a test issue:
  ladder travel was missing. Ported the exact horizontal 0.15 clamp,
  fall-distance reset, 0.2 collision climb impulse, vines, and matching open
  trapdoors over ladders from the local 1.11.2 source. The 20-tick Java trace
  now matches through clamp, climb, release, exhaustion, and landing at
  `c/magma/trace/out/matrix_ordinary_player_ladder_candidate_3/summary.md`.
- The affected family passes 18/18 at
  `c/magma/trace/out/matrix_ordinary_player_doors_ladder_cocoa_affected_1/summary.md`.
  Native tests exhaust door pairs and ladder/cocoa metadata and retain an
  exact climb trace. CPU/CUDA agree on 2,432 outputs at
  `c/magma/trace/out/cpu_cuda_player_survival_ordinary_doors_ladder_cocoa.log`.
- The aggregate passes in 4:25.50 with a 295,756 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_doors_ladder_cocoa.log`. GPU 1 is
  shared, so no throughput sample from this interval is admitted. These four
  strict rows remain performance-pending and the promoted total stays 676.

## 2026-08-02 (ordinary levitation candidate and fast effect gate)

- Added exact amplifier-aware levitation travel and same-tick expiry removal
  to the shared CPU/CUDA player kernel. The old fixture keeps potion durations
  exact but remains grounded at
  `c/magma/trace/out/matrix_ordinary_player_levitation_probe_1/summary.md`.
- The corrected 20-tick row matches Java's rise, tick-9 expiry, fall distance,
  and tick-12 landing at
  `c/magma/trace/out/matrix_ordinary_player_levitation_candidate_2/summary.md`.
  The affected movement/liquid/potion/ladder family passes 7/7 at
  `c/magma/trace/out/matrix_ordinary_player_levitation_affected_1/summary.md`.
- Extended the player-survival CPU/CUDA driver with an optional levitation
  amplifier so the new branch, rather than only the inactive path, is checked.
  All 2,432 values match at
  `c/magma/trace/out/cpu_cuda_player_survival_ordinary_levitation.log`.
- Added `game/test_player_effects.c` as a sub-second fail-fast check before the
  long runtime aggregate. It distinguishes bit-exact server motion from the
  standalone local packet path's 3.8e-6 second-position rounding. The final
  aggregate passes with a 295,312 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_levitation_final.log`. Its 9:10
  wall time is host-contaminated and is not a performance result. This strict
  row remains performance-pending and the promoted total stays 676.

## 2026-08-02 (ordinary Jump Boost II candidate)

- Added exact amplifier-aware Jump Boost to the shared player kernel and the
  integrated-server movement-packet prediction. The old-C fixture keeps
  potion state exact but follows the basic jump arc at
  `c/magma/trace/out/matrix_ordinary_player_jump_boost_probe_1/summary.md`.
- The corrected amplifier-1 fixture matches Java's exact 0.62 initial jump,
  tick-7 apex, tick-15 surface contact, tick-16 authoritative ground flag,
  exhaustion, and duration-39-through-10 potion trace at
  `c/magma/trace/out/matrix_ordinary_player_jump_boost_candidate_4/summary.md`.
  The affected random/potion/landing/ladder family passes 7/7 at
  `c/magma/trace/out/matrix_ordinary_player_jump_boost_affected_1/summary.md`.
- Extended the fail-fast native effect test across client and server impulses,
  and extended the player-survival CPU/CUDA driver with a Jump Boost amplifier
  argument. All 2,432 values match at
  `c/magma/trace/out/cpu_cuda_player_survival_ordinary_jump_boost_final.log`.
- Added Jump Boost fall-damage reduction to the shared vitals formula and
  ordinary runtime landing path. The direct Java/CPU/CUDA gate agrees on all
  400 rows at
  `c/magma/trace/out/java_cpu_cuda_player_vitals_jump_boost_ii.log`; the native
  nine-block drop checks four boosted damage against six ordinary damage.
- GPU 1 remains shared, so no timing from this interval is admitted. This
  nineteenth strict row remains performance-pending and the promoted total
  stays 676.

## 2026-08-02 (ordinary Water Breathing candidate)

- Added exact Water Breathing suppression to submerged air supply. The old-C
  fixture keeps the potion trace exact but loses air from tick 0 at
  `c/magma/trace/out/matrix_ordinary_player_water_breathing_probe_1/summary.md`.
- The corrected eight-tick row holds captured air through the effect's expiry
  tick, then resumes one-point-per-tick loss at
  `c/magma/trace/out/matrix_ordinary_player_water_breathing_candidate_1/summary.md`.
  Both random seeds, drowning, surface reset, Speed expiry, and Levitation pass
  7/7 with it at
  `c/magma/trace/out/matrix_ordinary_player_water_breathing_affected_1/summary.md`.
- The fixed-size potion scan is entered only when the eye is underwater, and
  the fail-fast native effect test covers the expiry boundary. GPU 1 remains
  shared, so this twentieth strict row is performance-pending and the promoted
  total stays 676. The combined final aggregate passes with a 296,036 KB peak
  and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_jump_boost_water_breathing_final.log`;
  its 14:10 wall time is host-contaminated and excluded from performance.

## 2026-08-02 (ordinary Fire Resistance candidate)

- Added exact Fire Resistance rejection to scheduled player burn damage and
  fire-block contact while preserving the fire counter. The old-C fixture
  accepts damage on the three-tick effect's expiry tick at
  `c/magma/trace/out/matrix_ordinary_player_fire_resistance_probe_1/summary.md`.
- The corrected 25-tick row suppresses the tick-2 hit, expires the potion, and
  accepts the next scheduled burn at tick 22 at
  `c/magma/trace/out/matrix_ordinary_player_fire_resistance_candidate_1/summary.md`.
  Both random seeds, fire counter/contact/extinguish, and Water Breathing pass
  7/7 at
  `c/magma/trace/out/matrix_ordinary_player_fire_resistance_affected_1/summary.md`.
- Extended the sub-second native player-effect test across the protected
  expiry hit and first unprotected later hit. Potion lookup runs only at a
  burn-damage or fire-contact boundary. GPU 1 remains shared, so no timing is
  admitted; this twenty-first strict row is performance-pending and the
  promoted total stays 676.

## 2026-08-02 (ordinary Hunger II and Poison II candidates)

- Added Hunger's amplifier-scaled per-tick exhaustion action before effect
  aging. Old magma keeps the three-tick lifecycle exact but misses exhaustion
  at `c/magma/trace/out/matrix_ordinary_player_hunger_probe_1/summary.md`.
  The exact 0.01, 0.02, 0.03, then hold trace passes at
  `c/magma/trace/out/matrix_ordinary_player_hunger_candidate_1/summary.md`,
  and the affected family passes 8/8 at
  `c/magma/trace/out/matrix_ordinary_player_hunger_affected_1/summary.md`.
- Added Poison's `25 >> amplifier` periodic action through the ordinary magic
  damage and hurt-resistance path, including its one-health floor. Old magma
  misses the measured duration-12 Poison II hit at
  `c/magma/trace/out/matrix_ordinary_player_poison_probe_2/summary.md`; the
  exact four-tick cadence passes at
  `c/magma/trace/out/matrix_ordinary_player_poison_candidate_1/summary.md`,
  and the affected random/drowning/melee/fire/potion family passes 10/10 at
  `c/magma/trace/out/matrix_ordinary_player_poison_affected_1/summary.md`.
- Extended the fail-fast native effect gate for Hunger expiry, Poison cadence,
  hurt aging, and the one-health floor. The full Fire Resistance, Hunger, and
  Poison aggregate passes with a 295,928 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_fire_resistance_hunger_poison_final.log`.
  Its 11:36 shared-host wall time is excluded. GPU 1 remains shared, so these
  twenty-second and twenty-third strict rows are performance-pending and the
  promoted total stays 676.

## 2026-08-02 (ordinary Regeneration candidate)

- Added Regeneration's `50 >> amplifier` periodic heal inside the existing
  active-potion loop. The fixture overlaps duration 50 with a scheduled burn;
  old magma keeps every measured field except health exact and remains at 19 at
  `c/magma/trace/out/matrix_ordinary_player_regeneration_probe_1/summary.md`.
- The corrected five-tick row takes and heals the damage on tick 2 while hurt
  time remains 9, then 8, then 7 at
  `c/magma/trace/out/matrix_ordinary_player_regeneration_candidate_1/summary.md`.
  The affected random/drowning/fire/potion family passes 9/9 at
  `c/magma/trace/out/matrix_ordinary_player_regeneration_affected_1/summary.md`.
- Extended the fail-fast native effect test across the same burn/recovery and
  food-timer boundary.

## 2026-08-02 (ordinary Wither II candidate)

- Added Wither's `40 >> amplifier` periodic armor-bypassing damage for effects
  in the ordinary potion list. Old magma keeps duration exact but misses the
  duration-20 hit at
  `c/magma/trace/out/matrix_ordinary_player_wither_probe_1/summary.md`.
- The corrected four-tick row matches health 19, hurt time 10 through 7, and
  duration 19 through 16 at
  `c/magma/trace/out/matrix_ordinary_player_wither_candidate_1/summary.md`.
  The affected random/drowning/melee/fire/potion family passes 10/10 at
  `c/magma/trace/out/matrix_ordinary_player_wither_affected_1/summary.md`.
- Extended the native effect gate for the Wither cadence. The combined
  Regeneration and Wither aggregate passes with a 295,708 KB peak and zero
  swap at
  `c/magma/trace/out/test_runtime_ordinary_regeneration_wither_final.log`.
  Its 11:10 shared-host wall time is excluded. GPU 1 remains shared, so these
  twenty-fourth and twenty-fifth strict rows are performance-pending and the
  promoted total stays 676.

## 2026-08-02 (ordinary Strength I and Weakness I melee candidates)

- Added Strength's amplifier-scaled `+3` attack-damage modifier at the queued
  melee boundary. Old magma deals one empty-hand point instead of four at
  `c/magma/trace/out/matrix_ordinary_player_strength_probe_1/summary.md`.
  The exact hit, immunity rejection, exhaustion, cooldown, and duration trace
  passes at
  `c/magma/trace/out/matrix_ordinary_player_strength_candidate_1/summary.md`,
  and the affected family passes 8/8 at
  `c/magma/trace/out/matrix_ordinary_player_strength_affected_1/summary.md`.
- Added Weakness's amplifier-scaled `-4` modifier and the zero-damage targeted
  rejection. The old row leaves health unchanged but incorrectly creates hurt
  state and exhaustion at
  `c/magma/trace/out/matrix_ordinary_player_weakness_probe_1/summary.md`; the
  corrected trace passes at
  `c/magma/trace/out/matrix_ordinary_player_weakness_candidate_1/summary.md`,
  with the affected family 8/8 at
  `c/magma/trace/out/matrix_ordinary_player_weakness_affected_1/summary.md`.
- Extended the native effect gate across exact Strength damage and Weakness
  zero rejection. The combined aggregate passes with a 296,176 KB peak and
  zero swap at
  `c/magma/trace/out/test_runtime_ordinary_strength_weakness_final.log`.
  Its 6:40 shared-host wall time is excluded. GPU 1 remains shared, so these
  twenty-sixth and twenty-seventh strict rows are performance-pending and the
  promoted total stays 676.

## 2026-08-02 (ordinary Haste II and Mining Fatigue I candidates)

- Wired active Haste and Mining Fatigue amplifiers into the existing ordinary
  player-break kernel, and applied their exact operation-2 constants to the
  cached player attack-speed attribute. The inactive path keeps base values
  and performs no potion scan per mining tick.
- The deliberate old Haste II row retains stone over 120 held ticks and reports
  the base 0.20 cooldown at
  `c/magma/trace/out/matrix_ordinary_player_haste_mining_probe_1/summary.md`.
  The corrected 0.24 cooldown, duration 179 through 60, and stone-to-air result
  pass at
  `c/magma/trace/out/matrix_ordinary_player_haste_mining_candidate_1/summary.md`.
- Mining Fatigue I reports the exact 0.18 cooldown and keeps the same stone
  intact through tick 179 at
  `c/magma/trace/out/matrix_ordinary_player_mining_fatigue_candidate_1/summary.md`.
  The ordinary no-effect row breaks it within that window. The complete
  affected family passes 9/9 at
  `c/magma/trace/out/matrix_ordinary_player_haste_fatigue_affected_1/summary.md`.
- Extended native effect coverage for attribute activation and clearing; the
  existing CPU/CUDA player-break cases cover both effects' mining arithmetic.
  The CPU aggregate passes with a 295,800 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_haste_fatigue_final.log`. GPU 1 is
  shared, so timing is deferred and these twenty-eighth and twenty-ninth rows
  remain performance-pending. The promoted total stays 676.

## 2026-08-02 (ordinary Resistance I candidate)

- Added Resistance's amplifier-scaled 20-percent reduction after armor in the
  shared contact, mob, magic, and explosion damage paths. The effect amplifier
  is cached only when the bounded potion list changes.
- The old-C cactus discriminator takes one full point while Java takes 0.8 at
  tick 1, with every other simulated feature and the raw world exact, at
  `c/magma/trace/out/matrix_ordinary_player_resistance_probe_1/summary.md`.
  The corrected four-tick trace passes at
  `c/magma/trace/out/matrix_ordinary_player_resistance_candidate_1/summary.md`.
- Nine affected damage/negative-control cases passed before the existing fire
  checker raised on a missing local exhaustion baseline. The repaired checker
  and fire-contact row pass at
  `c/magma/trace/out/matrix_ordinary_player_resistance_fire_contact_repair_1/summary.md`.
- A clean native mob run exposed an invalid weighted-pressure-plate test at an
  unloaded, unsupported cell. Moving it in range and staging stone support
  makes the suite genuinely pass at
  `c/magma/trace/out/test_mob_live_resistance_final.log`.
- The full CPU aggregate passes with a 296,616 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_resistance_final.log`. Its 11:34
  shared-host wall time is excluded. GPU 1 remains shared, so this thirtieth
  strict row is performance-pending and the promoted total stays 676.

## 2026-08-02 (ordinary Absorption I candidate)

- Added fixed player gold-heart state for Absorption, applied after armor and
  Resistance but before health. Removing or expiring the effect subtracts the
  original four points per amplifier level and clamps the remainder at zero.
- The old-C cactus discriminator loses health and adds exhaustion at tick 1,
  while Java consumes one gold-heart point with health 20 and exhaustion 0 at
  `c/magma/trace/out/matrix_ordinary_player_absorption_probe_1/summary.md`.
  The corrected effect expires at tick 2 and the later unprotected tick-11 hit
  reaches health and exhaustion exactly at
  `c/magma/trace/out/matrix_ordinary_player_absorption_candidate_1/summary.md`.
- The complete affected damage and negative-control family passes 11/11 at
  `c/magma/trace/out/matrix_ordinary_player_absorption_affected_1/summary.md`.
  Native player-effect and mob suites pass. The CPU aggregate passes with a
  294,844 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_ordinary_absorption_final.log`. Its 8:48
  shared-host time is excluded. GPU 1 remains shared, so this thirty-first
  strict row is performance-pending and the promoted total stays 676.

## 2026-08-02 (ordinary Slowness II strict coverage)

- Added the missing strict Slowness II expiry row around the already-present
  exact negative operation-2 movement modifier. This is coverage closure, not
  a claimed old-C product fix.
- Five active duration rows, measurably slower travel, expiry, and restored
  base speed pass at
  `c/magma/trace/out/matrix_ordinary_player_slowness_candidate_1/summary.md`.
  The paired random/Speed/Haste/Fatigue family passes 6/6 at
  `c/magma/trace/out/matrix_ordinary_player_slowness_affected_1/summary.md`.
- No runtime code or hot-path work changed. The thirty-second strict row stays
  in the current performance-pending batch while GPU 1 is shared; promoted
  total remains 676.

## 2026-08-03 (ordinary Health Boost II candidate)

- Added direct `max_health` and `absorption` fields to Java and magma
  authoritative state. The strengthened Absorption row directly matches gold
  hearts `4,3,0` at
  `c/magma/trace/out/matrix_ordinary_player_absorption_attributes_candidate_1/summary.md`.
- Added Health Boost's four-points-per-level maximum-health attribute to the
  cached potion attributes and made the shared vitals cap runtime-configurable.
  Old magma holds maximum health 20 and misses Java's natural-regeneration
  timer at
  `c/magma/trace/out/matrix_ordinary_player_health_boost_probe_1/summary.md`.
- The corrected Health Boost II trace matches maximum health
  `28,28,20,20,20`, food timer `1,2,0,0,0`, health, absorption, duration, and
  exact raw world at
  `c/magma/trace/out/matrix_ordinary_player_health_boost_candidate_2/summary.md`.
  Ten affected random, damage, fall, and periodic-effect rows pass 10/10 at
  `c/magma/trace/out/matrix_ordinary_player_health_boost_affected_1/summary.md`.
- Native player-effect coverage passes, and the scalar vitals kernel still
  matches its 400-line Java golden. GPU 1 is shared, so this thirty-third
  strict row remains performance-pending and the promoted total stays 676.

## 2026-08-03 (physical redstone control use)

- Added integrated-server right-click activation for lever 69, stone button
  77, and wooden button 143. The implementations reuse the existing bounded
  neighbor, observer, and scheduled-tick paths; idle runtime work is unchanged.
- The old product could raycast all three targets but excluded them from the
  server-use packet. Corrected physical fixtures match two lever toggles,
  button +20/+30 release, lamp +4 handoff, cooldown, queue, all simulated
  state, raw blocks, and light 3/3 at
  `c/magma/trace/out/matrix_redstone_player_control_use_candidate_2/summary.md`.
  Nine affected random and redstone rows pass at
  `c/magma/trace/out/matrix_redstone_player_control_use_affected_1/summary.md`.
- Added a focused native control-use test to the fail-fast aggregate. The full
  suite passes in 10:55 with a 295,220 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_redstone_player_control_use_final.log`.

## 2026-08-03 (physical repeater and comparator use)

- Added a pitched-down physical-use tape and exact floor fixtures. The old-C
  probe isolates repeater `93:2` versus Java `93:6` and comparator `149:2`
  versus Java `149:6`; the only state divergence is the absent click cooldown
  reset at
  `c/magma/trace/out/matrix_redstone_player_diode_use_probe_1/summary.md`.
- Added repeater delay cycling with ordinary neighbor/observer notification
  and comparator mode toggling with immediate output-tile recomputation. Both
  corrected strict behavior rows pass at
  `c/magma/trace/out/matrix_redstone_player_diode_use_candidate_2/summary.md`.
- Extended the focused native gate for both interactions. The aggregate passes
  at `c/magma/trace/out/test_runtime_redstone_player_diode_use_final.log`;
  clean performance promotion remains pending because GPU 1 is shared.
- Both random controls, repeater delays/off/lock, and comparator
  compare/subtract/priority paths pass 9/9 at
  `c/magma/trace/out/matrix_redstone_player_diode_use_affected_1/summary.md`.

## 2026-08-03 (physical wooden access-block use)

- Added three real-click fixtures for an upper oak door, an opposite-facing
  oak fence gate, and a bottom oak trapdoor. The old door path cannot resolve
  the paired lower half; old gate/trapdoor paths mutate locally but miss the
  authoritative success swing. The exact probe is
  `c/magma/trace/out/matrix_ordinary_player_wooden_access_use_probe_1/summary.md`.
- Routed all six wooden door IDs, all six fence-gate IDs, and wooden trapdoor
  96 through integrated-server use. The corrected 3/3 matrix matches paired
  metadata, gate facing flip, cooldown, pending work, raw blocks, and light at
  `c/magma/trace/out/matrix_ordinary_player_wooden_access_use_candidate_1/summary.md`.
- Focused native loops exhaust both six-ID families and retain iron door and
  iron trapdoor refusal. The later exact-source aggregate passes at
  `c/magma/trace/out/test_runtime_redstone_daylight_detector_use_final.log`;
  clean performance remains pending because GPU 1 is shared.
- The neighboring random, collision, and piston family passes 10/10 at
  `c/magma/trace/out/matrix_ordinary_player_wooden_access_use_affected_1/summary.md`.

## 2026-08-03 (redstone-powered access blocks)

- Added one-tick redstone-block placement probes for oak door, gate, and
  trapdoor. Old magma leaves each access block unchanged while Java changes
  door lower/upper `1/8` to `5/10`, gate `0` to `12`, and trapdoor `0` to `4`
  at
  `c/magma/trace/out/matrix_redstone_power_wooden_access_probe_1/summary.md`.
- Added neighbor-driven power transitions for all seven door IDs, six fence
  gates, and both trapdoors. The corrected strict rows pass 3/3 at
  `c/magma/trace/out/matrix_redstone_power_wooden_access_candidate_1/summary.md`.
  Native loops also lock exact source-removal closure without an idle scan.
- The matching source-removal fixtures pass 3/3 Java strict at
  `c/magma/trace/out/matrix_redstone_unpower_wooden_access_candidate_1/summary.md`.
  Random, physical-use, collision, and piston neighbors pass 10/10 at
  `c/magma/trace/out/matrix_redstone_power_wooden_access_affected_1/summary.md`.
- The later exact-source aggregate passes at
  `c/magma/trace/out/test_runtime_redstone_daylight_detector_use_final.log`.
  Clean performance remains pending because GPU 1 is shared.

## 2026-08-03 (physical daylight-detector use)

- Added physical right-click inversion for normal 151 and inverted 178
  daylight detectors. The old-C proof misses the sole detector mutation and
  successful-click cooldown in both directions at
  `c/magma/trace/out/matrix_redstone_player_daylight_detector_use_probe_1/summary.md`.
- Reused the existing exact saved-skylight capsule input and implemented the
  clear-weather 1.11.2 celestial-angle, sky-subtraction, and detector output
  calculation. Noon transitions `151:15` to `178:0` and back pass strict at
  `c/magma/trace/out/matrix_redstone_player_daylight_detector_use_candidate_2/summary.md`.
- Added detector metadata to bounded weak-power queries and native noon plus
  midnight checks. The affected family passes 10/10 at
  `c/magma/trace/out/matrix_redstone_player_daylight_detector_use_affected_1/summary.md`.
- The full native aggregate passes in 5:37 with a 294,856 KB peak and zero
  swap at
  `c/magma/trace/out/test_runtime_redstone_daylight_detector_use_final.log`.
  GPU 1 is shared, so clean performance remains pending.

## 2026-08-03 (periodic daylight-detector circuits)

- Added an exact restored-tile probe at total time 140. Old magma misses the
  normal detector's sole `151:0` to `151:15` mutation on Java total time 160 at
  `c/magma/trace/out/matrix_redstone_daylight_detector_periodic_probe_1/summary.md`.
- Added a fixed-capacity active detector list and 20-tick metadata updates with
  no idle world scan. The direct strict candidate passes at
  `c/magma/trace/out/matrix_redstone_daylight_detector_periodic_candidate_1/summary.md`.
- Added normal lamp-on and inverted lamp-off circuits. Their exact-light proof
  found detector IDs 151/178 missing from the zero-opacity override; after the
  correction both pass blocks, block light, and skylight at
  `c/magma/trace/out/matrix_redstone_daylight_detector_periodic_lamp_candidate_2/summary.md`.
- The 13-case neighboring family is exact across
  `c/magma/trace/out/matrix_redstone_daylight_detector_periodic_light_affected_1/summary.md`
  plus the one stable rerun at
  `c/magma/trace/out/matrix_redstone_daylight_detector_periodic_light_affected_rerun_1/summary.md`.
  Weather-strength attenuation remains F-01 work, and clean performance stays
  deferred while GPU 1 is shared.

## 2026-08-03 (physical cake eating)

- Added one locked integer food fixture so a legal cake activation starts
  below full hunger and is restored identically through the existing state
  capsule. The corrected old-C probe isolates cake `92:0` versus `92:1`, food
  18 versus 20, saturation 5.0 versus 5.4, and the success swing at
  `c/magma/trace/out/matrix_ordinary_player_use_cake_probe_2/summary.md`.
- Routed cake through integrated-server block use. The first-bite row passes
  exact state, raw blocks, and block light at
  `c/magma/trace/out/matrix_ordinary_player_use_cake_candidate_1/summary.md`.
  Native checks cover a hungry serving, a full-player consumed activation, and
  removal of the final metadata-6 serving.
- Added a complete physical lifecycle: four centered bites, a 15-degree east
  turn to follow the receding hitbox, three final bites, exact food/saturation
  and swing cooldowns, then block removal. It passes at
  `c/magma/trace/out/matrix_ordinary_player_eat_whole_cake_candidate_3/summary.md`.
- Random, comparator, collision, piston destruction, pig push, and comparator
  power-off neighbors pass 9/9 at
  `c/magma/trace/out/matrix_ordinary_player_use_cake_affected_1/summary.md`.
  The exact-source aggregate passes in 5:50 with a 296,124 KB peak and zero
  swap at
  `c/magma/trace/out/test_runtime_daylight_periodic_cake_final.log`.
  Clean GPU performance remains deferred while GPU 1 is shared.

## 2026-08-03 (physical flower-pot insertion)

- Added a 60-degree physical-use fixture with held red flower `38:2`. The
  old-magma proof isolates Java's predicted client consumption, next-tick pot
  tile commit, and late success swing; old magma places a stray flower block
  instead at
  `c/magma/trace/out/matrix_ordinary_player_pot_red_flower_probe_2/summary.md`.
- Added the predicted-client/authoritative-server pot insertion path. Exact
  inventory, tile contents, cooldown, raw blocks, and block light pass at
  `c/magma/trace/out/matrix_ordinary_player_pot_red_flower_candidate_3/summary.md`.
- Native tests exhaust all 21 canonical pottable item/meta pairs, plus occupied
  pot and invalid-item refusal. Random, collision, piston entity-push, and
  occupied-pot destruction neighbors pass 7/7 at
  `c/magma/trace/out/matrix_ordinary_player_pot_red_flower_affected_1/summary.md`.
  The exact-source aggregate passes in 5:41 with a 296,388 KB peak and zero
  swap at
  `c/magma/trace/out/test_runtime_daylight_periodic_cake_flower_pot_final.log`.
  Clean GPU performance remains deferred while GPU 1 is shared.

## 2026-08-03 (physical jukebox record insertion)

- Added a horizontal record-13 physical-use fixture. Old magma leaves the
  jukebox tile empty, metadata zero, record held, and cooldown untouched at
  `c/magma/trace/out/matrix_ordinary_player_insert_record_13_probe_1/summary.md`.
- Added exact empty-jukebox item use. Record 13 installs as tile item 2256,
  metadata changes `84:0` to `84:1`, inventory consumes on the authoritative
  server tick, and the success swing matches at
  `c/magma/trace/out/matrix_ordinary_player_insert_record_13_candidate_2/summary.md`.
- Moved canonical held/full inventory observation to the already-locked server
  player, eliminating variable client packet latency from strict item-use
  gates. Random controls, cake/flower interactions, and saved jukebox
  comparator strengths 1/12 pass 7/7 under that observation at
  `c/magma/trace/out/matrix_ordinary_player_insert_record_13_affected_2/summary.md`.
- Native tests exhaust all 12 vanilla record IDs. Sound event 1010, playback,
  and physical record ejection remain explicitly open. The final exact-source
  aggregate passes in 5:38.92 with a 296,332 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_daylight_periodic_cake_flower_pot_jukebox_final.log`.
  Clean GPU performance remains deferred while GPU 1 is shared.

## 2026-08-03 (physical jukebox record ejection)

- Added a filled-jukebox physical-use probe. Old magma leaves metadata 1 and
  record 13 in the tile, creates no item, and misses the success cooldown at
  `c/magma/trace/out/matrix_ordinary_player_eject_record_13_probe_1/summary.md`.
- Added exact tile clearing, `84:1` to `84:0`, three `World.rand` spawn
  offsets, four `Math.random` constructor draws, entity-ID allocation, and
  record EntityItem creation. The corrected record-13 row passes at
  `c/magma/trace/out/matrix_ordinary_player_eject_record_13_candidate_6/summary.md`.
- Added a forced-overlap record-wait case. It pins the new entity's otherwise
  clock-seeded `Entity.rand` cursor at the parked input boundary and proves
  exact `pushOutOfBlocks` position, velocity, yaw, age, and pickup delay at
  `c/magma/trace/out/matrix_ordinary_player_eject_record_wait_push_candidate_2/summary.md`.
  The complete insertion/ejection/cake/flower/comparator family passes 7/7 at
  `c/magma/trace/out/matrix_ordinary_player_eject_record_affected_1/summary.md`.
- Native tests insert and eject all 12 vanilla records and prove full-item-pool
  rejection leaves metadata, tile contents, RNG cursors, and entity ID
  unchanged. The final exact-source aggregate passes in 5:15.70 with a
  296,108 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_daylight_periodic_cake_flower_pot_jukebox_ejection_final.log`.
  Sound event 1010 and record playback remain A-01 work. GPU performance is
  still deferred while GPU 1 is shared, so the promoted total stays 676.

## 2026-08-03 (redstone-powered TNT ignition and fuse)

- Added exact neighbor-powered and direct-powered-placement ignition for TNT
  46. The old product either retained the existing TNT or installed the newly
  placed TNT, while Java removed it and created one `EntityTNTPrimed`; the two
  independent negatives are at
  `c/magma/trace/out/matrix_redstone_tnt_ignite_probe_1/summary.md` and
  `c/magma/trace/out/matrix_redstone_tnt_direct_add_probe_1/summary.md`.
- Added the bounded live primed-TNT state, exact base-constructor entity-ID
  order, initial `Math.random` motion, fuse 80, and pre-explosion
  gravity/collision/drag/ground-bounce tick path. The 79-tick Java fixture is
  exact through fuse 1 at
  `c/magma/trace/out/matrix_redstone_tnt_fuse_probe_1/summary.md`.
- Added authoritative Java fuse capture, magma raw-entity output, strict fuse
  comparison, and native tests for both ignition entries, cursor transitions,
  and the full pre-explosion lifetime. The final affected family passes 8/8 at
  `c/magma/trace/out/matrix_redstone_tnt_affected_2/summary.md`.
- The exact-source aggregate passes in 5:16.39 with a 295,856 KB peak and zero
  swap at
  `c/magma/trace/out/test_runtime_redstone_tnt_ignition_fuse_final.log`.
  GPU timing remains deferred while GPU 1 is shared, so the promoted total
  remains 676.
- Fuse-zero explosion is deliberately still open. The existing generic
  explosion helper has fixed per-ray RNG density and an incomplete
  `doExplosionB`; substituting it would create a false parity claim. Prime
  sound, rendering, and capsule restore also remain separate work.

## 2026-08-03 (physical TNT ignition items)

- Added a real horizontal flint-and-steel TNT fixture. Old magma damages the
  tool at client tick 2, keeps TNT, and places fire in the adjacent cell while
  Java performs authoritative TNT removal, tool damage, and entity creation at
  tick 3. The deliberate two-cell negative is at
  `c/magma/trace/out/matrix_ordinary_player_ignite_tnt_flint_probe_1/summary.md`.
- Routed TNT through the physical server-use packet only for flint and steel or
  fire charge. Both corrected rows match item durability/consumption, attack
  cooldown, raw blocks/light, constructor cursors, and every primed-entity
  field through fuse 75 at
  `c/magma/trace/out/matrix_ordinary_player_ignite_tnt_items_candidate_1/summary.md`.
- The first fire-charge candidate exposed the empty-main-hand lifecycle: after
  the last charge is consumed, Java resets cooldown in
  `EntityPlayer.onUpdate`. The corrected late reset is strict without changing
  the flint path.
- Native tests cover both items, invalid-item refusal, and atomic rejection
  when all 16 represented primed-TNT slots are occupied. Both items and eight
  neighboring gameplay rows pass 10/10 at
  `c/magma/trace/out/matrix_ordinary_player_ignite_tnt_items_affected_1/summary.md`.
- The exact-source aggregate passes under concurrent two-core validation in
  5:39.24 with a 298,736 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_tnt_player_ignition_final.log`. GPU timing is
  deferred while GPU 1 is shared, so the promoted total remains 676.

## 2026-08-03 (burning-arrow TNT ignition)

- Added an exact stationary-arrow fire fixture and positive/negative TNT
  collision pair. The old C runtime retains TNT while Java removes it and
  creates primed TNT at
  `c/magma/trace/out/matrix_redstone_tnt_burning_arrow_probe_1/summary.md`.
- Added projectile fire aging before collision, positive-fire TNT contact,
  same-boundary primed-TNT ticking, and native boundary tests for fire 100 to
  99 ignition and fire 1 to 0 refusal.
- The first wider run exposed unrelated world-generation animals consuming
  process-global entity IDs and `Math.random` earlier in the same Java tick.
  The oracle now restores the saved cursors at the exact armed
  burning-arrow/TNT contact. The hook is dormant outside that fixture. Three
  positive and three negative repetitions pass 6/6 at
  `c/magma/trace/out/matrix_redstone_tnt_burning_arrow_repeat_3/summary.md`.
- Both arrow rows and ten neighboring button, tripwire, pressure-plate, lamp,
  piston, and TNT cases pass 12/12 at
  `c/magma/trace/out/matrix_redstone_tnt_burning_arrow_affected_2/summary.md`.
- The exact-source aggregate passes under unrelated host training load in
  5:40.55 with a 303,104 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_tnt_burning_arrow_final.log`. Fuse-zero
  explosion and explosion-triggered short-fuse priming remain open. GPU timing
  remains deferred while GPU 1 is shared, so the promoted total stays 676.

## 2026-08-03 (fuse-zero no-drop TNT crater)

- Added a one-tick saved `EntityTNTPrimed` fixture and isolated six-glass
  crater. The clean old-C probe retires fuse 1 without changing blocks while
  Java removes exactly six glass cells at
  `c/magma/trace/out/matrix_tnt_fuse_zero_glass_probe_2/summary.md`.
- Added the live explosion-ray path with 1,352 exact `World.rand.nextFloat()`
  density draws, the two following `doExplosionB` sound-pitch draws, and
  promoted live stone resistance. The existing synthetic rand-0.5/
  hardness-only oracle remains unchanged.
- The Java oracle restores the saved world cursor only at the armed
  `EntityTNTPrimed.explode` boundary. Case-specific staged headroom removes
  terrain, drops, and ambient entity effects from this causal proof.
- Three independent crater captures pass 3/3 at
  `c/magma/trace/out/matrix_tnt_fuse_zero_glass_repeat_1/summary.md`; the new
  row and 12 neighboring TNT/redstone controls pass 13/13 at
  `c/magma/trace/out/matrix_tnt_fuse_zero_glass_affected_1/summary.md`.
- General explosion parity remains open for drops, fire, the complete
  resistance table, exposure-based damage and knockback, emitted sound,
  particles, and rendering. The exact-source aggregate passes under shared-host
  load in 5:35.70 with a 299,292 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_tnt_fuse_zero_glass_final.log`. GPU timing
  remains deferred while GPU 1 is shared, so the promoted total remains 676.

## 2026-08-03 (explosion-triggered TNT chain priming)

- Added a one-hit TNT chain fixture beside five glass cells. The clean old-C
  probe removes the crater blocks but creates no replacement primed entity at
  `c/magma/trace/out/matrix_tnt_explosion_chain_prime_probe_1/summary.md`.
- Captured and restored the causal world, `Math.random`, and entity-ID cursors
  at `EntityTNTPrimed.explode`. The runtime now consumes the two sound-pitch
  draws before block processing, constructs hit TNT, samples its short fuse
  with `World.rand.nextInt(20)`, and ticks fuse 10 to 9 on the same boundary.
- The behavior checker independently derives the expected RNG cursors, entity
  ID, and fuse from the saved state. Three captures pass 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_chain_prime_repeat_1/summary.md`, and
  the affected TNT/redstone family passes 14/14 at
  `c/magma/trace/out/matrix_tnt_explosion_chain_prime_affected_1/summary.md`.
- The first aggregate exposed a native-fixture dependency: the negative arrow
  control intentionally left TNT inside the later crater radius, and the new
  behavior correctly chained it. Isolating the no-chain fixture restored the
  strict aggregate, which passes in 5:16.98 with a 303,588 KB peak and zero
  swap at
  `c/magma/trace/out/test_runtime_tnt_explosion_chain_prime_final_2.log`.
- Multiple hit TNTs, affected-block iteration order, drops, fire, the complete
  resistance table, entity exposure/damage/knockback, emitted sound, particles,
  and rendering remain open. GPU timing remains deferred while GPU 1 is shared,
  so the promoted total remains 676.

## 2026-08-03 (open-air TNT player damage and packet knockback)

- Added an isolated open-air player-blast fixture. The deliberate old-product
  probe applies seven damage from its eye-centered approximation but creates no
  hurt state or knockback, while Java applies three damage and the tracked
  velocity response at
  `c/magma/trace/out/matrix_tnt_explosion_player_open_air_probe_2/summary.md`.
- Added a cold detonation diagnostic for the explicitly armed oracle fixture.
  It measures density 1, damage 3, and the explosion-packet impulse without
  changing ordinary captures.
- Corrected the event boundary to use player feet for damage range and eye
  height for impulse direction. The subsequent self-tracking velocity packet
  truncates motion to 1/8000 before client ground friction; hurt time,
  exhaustion, and FoodStats advance on the same saved tick. The strict result
  passes at
  `c/magma/trace/out/matrix_tnt_explosion_player_open_air_candidate_3/summary.md`.
- Three independent captures pass 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_player_open_air_repeat_1/summary.md`;
  the crater, chain, ignition, arrow, piston, lamp, fuse, and new player row
  pass 15/15 at
  `c/magma/trace/out/matrix_tnt_explosion_player_open_air_affected_1/summary.md`.
- Tightened the native fixture to stage its claimed open-air corridor and split
  its cursor, damage-lifecycle, and client-packet assertions. The full CPU
  aggregate passes in 5:13.29 with a 301,368 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_tnt_explosion_player_open_air_final.log`.
  Obstructed density, armor/resistance variants, non-player entities, drops,
  fire, sound, particles, and rendering remain open. GPU timing remains
  deferred while GPU 1 is shared, so the promoted total remains 676.

## 2026-08-03 (full-cube obstructed TNT player exposure)

- Added one glass-occluded saved TNT fixture. The block masks 21 of Java's 45
  standing-player sample rays, yielding density 0.53333336, damage 4, and
  packet X -0.09516628. The old density-one product over-damages to health 13
  and reaches X 8.321625 at
  `c/magma/trace/out/matrix_tnt_explosion_player_obstructed_probe_4/summary.md`.
- Ported the Java float sample grid and event-time block-ray test against the
  authoritative server-player box. Entity exposure now runs before affected
  blocks are removed, matching `Explosion.doExplosionA`; the existing two
  sound-pitch draws still precede block processing.
- Exposed the existing C server-player pose/motion in trace diagnostics. This
  lets the behavior gate compare the stable authoritative result while
  accepting the measured one-observation Java client packet race. Three
  corrected captures pass 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_player_obstructed_candidate_2/summary.md`.
- The new bounded diagnostic and 15 strict crater, chain, ignition, arrow,
  piston, lamp, and fuse controls pass 16/16 at
  `c/magma/trace/out/matrix_tnt_explosion_player_obstructed_affected_1/summary.md`.
- Added `game/test_tnt_explosion.c`, which checks the same-seed crater, cursor,
  damage lifecycle, and client/server knockback in 23.49 seconds with a 37,460
  KB peak. The full CPU aggregate passes in 6:05.53 with a 307,616 KB peak and
  zero swap at
  `c/magma/trace/out/test_runtime_tnt_explosion_player_obstructed_final_3.log`.
  The 45 short rays run only on an explosion event; there is no idle scan or
  allocation. Non-collidable ray targets, broader shaped occluders,
  armor/blast-protection variants, and non-player entities remain open. GPU
  timing remains deferred while GPU 1 is shared, so the promoted total remains
  676.

## 2026-08-03 (TNT player armor, Resistance, and Blast Protection)

- Added one exact armor fixture path with optional enchantment payload. Java
  equips the stack before the parked boundary so armor and toughness
  attributes are live; C applies the same stack after capsule restore. Trace
  diagnostics retain armor slots 36..39 without expanding the capsule's main
  inventory contract.
- Explosion damage now enters the shared player damage path with an explicit
  explosion source. The path preserves enchantments while damaging armor and
  follows Java's armor, Resistance, enchantment protection, absorption order.
  Plain diamond chestplate damage is 2.184, Blast Protection IV damage is
  1.4851201, and the combined Resistance I result is 1.188096. Durability
  advances zero to one and enchantment 3:4 survives exactly.
- Java 1.11.2 floors Blast Protection's sub-one knockback reduction to zero in
  this TNT fixture. The unchanged client packet and authoritative server
  velocity match exactly. The plain/enchantment pair passes at
  `c/magma/trace/out/matrix_tnt_explosion_player_defense_candidate_2/summary.md`;
  three combined repeats pass at
  `c/magma/trace/out/matrix_tnt_explosion_player_defense_combined_candidate_1/summary.md`.
- The eight-case crater/chain/exposure/defense regression passes at
  `c/magma/trace/out/matrix_tnt_explosion_player_defense_regression_1/summary.md`.
  The focused native test stays bounded at 24.92 seconds and 37,444 KB by
  retaining one integrated world and checking the three defense formulas at
  the shared combat boundary. The full CPU aggregate passes in 5:32.46 with a
  308,096 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_tnt_explosion_player_defense_final.log`.
  GPU timing remains deferred while GPU 1 is shared, so these four correctness
  rows remain performance-pending and the promoted total stays 676.

## 2026-08-03 (open-air TNT damage to a non-player living entity)

- Added a bounded paired pig/primed-TNT fixture without widening the other
  mutually exclusive entity-fixture paths. The pig is spawned first so this
  row isolates living blast response from reverse loaded-entity ordering.
- The old product leaves the locked pig at health 10 with zero motion. The
  corrected explosion path enumerates represented living AABBs only when an
  explosion occurs, samples vanilla exposure rays, applies the ordinary
  hurt-resistance lifecycle, and adds the eye-directed impulse. Java and C
  both finish at health 1, motion X -0.2480964714222078, motion Y
  0.030753624425608167, hurt time 10, and hurt-resistant time 20 at
  `c/magma/trace/out/matrix_tnt_explosion_mob_candidate_2/summary.md`.
- The nine-case crater/chain/player-defense/pig regression passes at
  `c/magma/trace/out/matrix_tnt_explosion_living_regression_1/summary.md`.
  The focused native test passes in 23.93 seconds with a 37,496 KB peak. The
  full CPU aggregate passes in 5:55.93 with a 308,092 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_tnt_explosion_mob_final.log`. The event-only
  target walk uses a fixed stack buffer and adds no idle scan or heap
  allocation. Reverse ordering and the remaining entity categories stay
  explicit follow-up work. GPU timing remains deferred while GPU 1 is shared,
  so this correctness row is performance-pending and the promoted total stays
  676.

## 2026-08-03 (TNT-first living-entity update order)

- Reversed the paired fixture's spawn order to put fuse-one TNT before the
  pig. The old grouped runtime applies the right blast but misses the pig's
  subsequent same-tick update: Java has hurt 9, invulnerability 19, and the
  fresh impulse multiplied by 0.98, while C leaves 10/20 and the undamped
  impulse at
  `c/magma/trace/out/matrix_tnt_explosion_mob_tnt_first_probe_1/summary.md`.
- The bounded controlled scheduler now uses saved entity IDs to choose whether
  primed TNT or the pig updates first. NoAI controlled updates implement
  Java's non-server-world motion damping without movement. Three independent
  TNT-first captures pass 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_mob_tnt_first_repeat_1/summary.md`;
  both order rows pass strict at
  `c/magma/trace/out/matrix_tnt_explosion_mob_order_final_1/summary.md`.
- The complete crater/chain/player-defense/two-order family passes 10/10 at
  `c/magma/trace/out/matrix_tnt_explosion_ordered_living_regression_1/summary.md`.
  The native order test passes in 24.57 seconds with a 37,440 KB peak. The
  exact-source CPU aggregate passes in 6:05.70 with a 308,416 KB peak and zero
  swap at
  `c/magma/trace/out/test_runtime_tnt_explosion_mob_order_final.log`. The
  active controlled-entity path remains fixed-capacity and allocation-free;
  general interleaved multi-entity order still needs an explicit order field
  in the state capsule. GPU timing remains deferred while GPU 1 is shared, so
  this correctness row is performance-pending and the promoted total stays
  676.
- Added lethal no-loot variants at pig health 8. Pig-first records death time
  0 on the blast tick and retires at tick 20; TNT-first records death time 1
  after its same-tick pig update and retires at tick 19. Three pig-first
  repeats pass at
  `c/magma/trace/out/matrix_tnt_explosion_mob_lethal_repeat_1/summary.md`, and
  all four surviving/lethal order rows pass at
  `c/magma/trace/out/matrix_tnt_explosion_mob_living_order_final_1/summary.md`.

## 2026-08-03 (outline-only TNT exposure occluder)

- Added a supported standing-torch blast fixture between the locked pig and
  fuse-one TNT. The support-only geometry misses every exposure ray, while
  Java's outline-only torch blocks one third even though its entity collision
  box is null. The old runtime reuses projectile filtering, applies open-air
  damage 9, and fails only pig state at
  `c/magma/trace/out/matrix_tnt_explosion_mob_torch_occluded_probe_2/summary.md`;
  both engines destroy exactly the torch and retain its obsidian support.
- Split the common short-segment block ray by Java's actual call flags.
  Projectiles retain `ignoreBlockWithoutBoundingBox=true`; explosion density
  uses `false`, skips only failed `canCollideCheck` blocks and the moving
  piston's null ray override, and intersects the existing exact outline boxes.
  Java and C now produce exposure 2/3, damage 6, health 4, motion X
  -0.1653976525440392, motion Y 0.0205024168947584, and fresh 10/20 hurt
  timers. The strict candidate passes and three captures repeat 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_mob_torch_occluded_candidate_1/summary.md`
  and
  `c/magma/trace/out/matrix_tnt_explosion_mob_torch_occluded_repeat_1/summary.md`.
- Every behavior and raw-block gate in the 13-case TNT family passes at
  `c/magma/trace/out/matrix_tnt_explosion_outline_regression_1/summary.md`.
  The focused native test combines outline exposure with TNT-first damping and
  passes in 22.68 seconds with a 37,464 KB peak. The full CPU aggregate passes
  in 5:35.75 with a 308,148 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_tnt_explosion_outline_final.log`. The branch
  is active-explosion-only, fixed-capacity, and allocation-free. GPU timing is
  still deferred while GPU 1 is shared, so the promoted total remains 676.

## 2026-08-03 (surviving dropped-item TNT response)

- Extended the paired oracle fixture to permit one exact stationary
  `EntityItem` plus fuse-one TNT, and exposed the item's private integer health
  on both sides. The old C result leaves health 5 and motion zero at
  `c/magma/trace/out/matrix_tnt_explosion_item_surviving_probe_1/summary.md`.
- Added event-only item exposure, damage, and raw impulse using the exact
  0.25-cube AABB and float eye height. The bounded scheduler also honors the
  saved item-before-TNT entity IDs, so age reaches 1 before detonation and the
  fresh impulse is not damped. Java and C both finish at health 1, unchanged
  position, X -0.12494932090351177, and Y 0.003413794015269717. The candidate
  passes and repeats 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_item_surviving_candidate_1/summary.md`
  and
  `c/magma/trace/out/matrix_tnt_explosion_item_surviving_repeat_1/summary.md`.
- All behavior/raw-block gates in the 14-case TNT family pass at
  `c/magma/trace/out/matrix_tnt_explosion_item_regression_1/summary.md`. The
  focused native test passes in 22.96 seconds with a 37,440 KB peak. The full
  CPU aggregate passes in 5:50.37 with a 306,756 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_tnt_explosion_item_final.log`. The new work
  is fixed-capacity and active-explosion-only. GPU timing remains deferred
  while GPU 1 is shared, so the correctness candidate is not yet added to the
  promoted total of 676.
- Added the reverse saved order with TNT's entity ID before the item. The old
  path retains the original position and undamped impulse; the corrected
  gravity-free item update moves once and damps motion by float 0.98. The
  strict candidate passes and repeats 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_item_tnt_first_candidate_1/summary.md`
  and
  `c/magma/trace/out/matrix_tnt_explosion_item_tnt_first_repeat_1/summary.md`.
- A zero-motion fast path keeps existing stationary pressure-plate fixtures
  parked while accelerated fixtures take the full move/friction path. Both
  TNT orders, two plate controls, and representative down/east piston-item
  controls pass at
  `c/magma/trace/out/matrix_tnt_explosion_item_order_affected_3/summary.md`.
  The final native test passes in 23.65 seconds with a 37,456 KB peak, and the
  full CPU aggregate passes in 5:27.94 with a 306,960 KB peak and zero swap at
  `c/magma/trace/out/test_tnt_explosion_item_order_final.log` and
  `c/magma/trace/out/test_runtime_tnt_explosion_item_order_final.log`. GPU
  timing remains deferred while GPU 1 is shared.
- Added ordinary lethal and Nether Star protection controls. The six-block
  stone item is removed on the blast tick; the seven-block Nether Star keeps
  private health 5 but receives the exact raw impulse. Both behavior gates
  pass and repeat 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_item_lifecycle_candidate_1/summary.md`
  and
  `c/magma/trace/out/matrix_tnt_explosion_item_lifecycle_repeat_1/summary.md`.
  Native AABB/damage/exception coverage passes in 23.92 seconds with a 37,468
  KB peak at
  `c/magma/trace/out/test_tnt_explosion_item_lifecycle_final.log`. No runtime
  source changed after the already-green final aggregate; GPU timing remains
  deferred while GPU 1 is shared.

## 2026-08-03 (TNT-first boat explosion response)

- Added a paired gravity-free boat/fuse-one-TNT oracle fixture with TNT's
  entity ID first. The old product retires TNT but leaves the boat parked at
  `c/magma/trace/out/matrix_tnt_explosion_boat_probe_1/summary.md`.
- Explosion targets now include the exact boat AABB. Damage 4 becomes boat
  damage-taken 40 and survives Java's strict greater-than-40 destruction
  threshold. The subsequent same-tick no-gravity boat update applies float
  0.9 momentum, yielding exact position X 9.387742502803361, Y
  83.00814089616274 and motion X -0.11225749719663904, Y
  0.008140896162744845.
- The explicit state/behavior/raw-block gate repeats 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_boat_repeat_1/summary.md`. Both pig
  orders, both item orders, and two boat/redstone controls pass 7/7 at
  `c/magma/trace/out/matrix_tnt_explosion_boat_affected_1/summary.md`.
- The focused native test passes in 23.58 seconds with a 37,512 KB peak at
  `c/magma/trace/out/test_tnt_explosion_boat_final.log`. The full CPU aggregate
  passes in 5:42.36 with a 306,756 KB peak, zero swap, and exit 0 at
  `c/magma/trace/out/test_runtime_tnt_explosion_boat_final.log`. The work is
  fixed-capacity and active-explosion/active-entity only. GPU timing remains
  deferred while GPU 1 is shared, so the promoted total remains 676.

## 2026-08-03 (arrow-first TNT explosion response)

- Added a bounded stationary-arrow/fuse-one-TNT fixture with the arrow's
  entity ID first. The old path ages the arrow and retires TNT but leaves
  arrow motion zero at
  `c/magma/trace/out/matrix_tnt_explosion_arrow_probe_1/summary.md`.
- The active-explosion path now samples the arrow's exact 0.5-cube AABB and
  zero eye height. Because the arrow already updated on this boundary, it
  retains position and zero rotation while receiving raw motion X
  -0.12499536788906258 and Y -0.0003794502612004625.
- The explicit state/behavior/raw-block gate repeats 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_arrow_repeat_1/summary.md`. Burning
  and non-burning TNT contact, three arrow/redstone controls, and representative
  mob/item/boat blast rows pass 9/9 at
  `c/magma/trace/out/matrix_tnt_explosion_arrow_affected_1/summary.md`.
- The focused native test passes in 23.61 seconds with a 37,476 KB peak at
  `c/magma/trace/out/test_tnt_explosion_arrow_final.log`. The full CPU aggregate
  passes in 5:43.21 with a 306,640 KB peak, zero swap, and exit 0 at
  `c/magma/trace/out/test_runtime_tnt_explosion_arrow_final.log`. The fixed
  32-slot scan runs only during an active explosion and adds no idle work or
  allocation. GPU timing remains deferred while GPU 1 is shared, so the
  promoted total remains 676.

## 2026-08-03 (XP-orb-first TNT explosion response)

- Added a paired XP-orb/fuse-one-TNT oracle fixture with the orb's entity ID
  first, and reflected the orb's private `xpOrbHealth` into the authoritative
  trace. The old product performs the exact ordinary orb tick but leaves
  health unobserved and omits the explosion impulse at
  `c/magma/trace/out/matrix_tnt_explosion_xp_probe_1/summary.md`.
- The active-explosion path now samples the orb's exact 0.5-cube AABB and
  float-derived eye height, applies damage 4, and adds the raw impulse after
  the orb's attraction/gravity/move/drag update. Java and C agree exactly on
  health 1, age 1, position, motion, pickup delay, and both color counters.
  The explicit behavior/state/raw-block gate repeats 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_xp_repeat_1/summary.md`.
- XP pickup, XP tripwire and stone-plate controls, both item orderings, and
  representative mob/boat/arrow blast rows pass 9/9 at
  `c/magma/trace/out/matrix_tnt_explosion_xp_affected_1/summary.md`. The
  focused native test passes in 23.59 seconds with a 37,460 KB peak at
  `c/magma/trace/out/test_tnt_explosion_xp_final.log`. The full CPU aggregate
  passes in 6:01.94 with a 306,612 KB peak, zero swap, and exit 0 at
  `c/magma/trace/out/test_runtime_tnt_explosion_xp_final.log`.
- The implementation uses the existing fixed 16-orb store only on an active
  explosion, with no idle scan or allocation. GPU timing remains deferred
  while GPU 1 is shared, so this correctness candidate does not change the
  promoted total of 676. The next bounded target probe is a saved falling
  block.

## 2026-08-03 (TNT-first falling-sand explosion response)

- Reused the scheduled-sand fixture to create a real falling entity one
  boundary after fuse-three TNT begins ticking. On the final boundary TNT has
  the lower entity ID and Java explodes before the sand's second update. The
  old grouped C runtime advances the sand first and leaves it with zero blast
  motion at
  `c/magma/trace/out/matrix_tnt_explosion_falling_sand_probe_3/summary.md`.
- The bounded scheduler now defers a later-ID falling block behind active TNT,
  and the explosion path samples the exact 0.98-cube AABB and generic
  float-derived eye height. The same-tick sand update then applies gravity,
  movement, and float-0.98 drag. Java and C finish at exact X
  9.376391166346687, Y 82.90807990783813, motion X
  -0.12113665933789819, motion Y -0.06068168302984159, and fall time 2.
- The explicit three-boundary behavior/state/raw-block gate repeats 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_falling_sand_repeat_1/summary.md`.
  Ordinary sand landing, sand tripwire, crater/chain, and representative
  entity explosion rows pass 10/10 at
  `c/magma/trace/out/matrix_tnt_explosion_falling_sand_affected_1/summary.md`.
- The focused native test passes in 25.35 seconds with a 37,480 KB peak at
  `c/magma/trace/out/test_tnt_explosion_falling_sand_final.log`. The full CPU
  aggregate passes in 5:44.24 with a 307,048 KB peak, zero swap, and exit 0 at
  `c/magma/trace/out/test_runtime_tnt_explosion_falling_sand_final.log`. The
  fixed 16-entry scan is active-object-only and allocation-free. GPU timing
  remains deferred while GPU 1 is shared, so the promoted total remains 676.
  The next bounded target is the existing small-fireball fixture.

## 2026-08-03 (TNT-first small-fireball explosion response)

- Paired the existing exact `EntitySmallFireball` fixture with fuse-one TNT,
  with TNT's saved ID first. The old runtime retires TNT but leaves the
  fireball parked at
  `c/magma/trace/out/matrix_tnt_explosion_small_fireball_probe_1/summary.md`.
- Added active-explosion sampling for the fireball's exact 0.3125-cube AABB
  and float-derived eye height, plus the bounded saved-ID ordering needed to
  run its update after the detonation. The resulting position and motion are
  exact. Replacing libc `atan2` and double 0.95 with Java's table-based
  `MathHelper.atan2` and float motion factor also closes the last raw
  velocity/rotation discrepancy instead of relying on the general state
  tolerance.
- The explicit behavior/state/raw-block gate passes and repeats 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_small_fireball_behavior_1/summary.md`
  and
  `c/magma/trace/out/matrix_tnt_explosion_small_fireball_repeat_1/summary.md`.
  Ten neighboring TNT/entity cases and both small-fireball tripwire controls
  pass 11/11 at
  `c/magma/trace/out/matrix_tnt_explosion_small_fireball_affected_1/summary.md`;
  the ordinary eight-tick active trajectory remains exact at
  `c/magma/trace/out/small_fireball_trajectory_tnt_final`.
- The focused native test passes in 23.97 seconds with a 44,640 KB peak at
  `c/magma/trace/out/test_tnt_explosion_small_fireball_final.log`. The full CPU
  aggregate passes in 5:38.06 with a 306,696 KB peak, zero swap, and exit 0 at
  `c/magma/trace/out/test_runtime_tnt_explosion_small_fireball_final.log`.
  The scan is fixed-capacity and active-explosion-only; rotation work is
  active-projectile-only. GPU timing remains deferred while GPU 1 is shared,
  so the promoted total remains 676. The next bounded explosion target is a
  second already-primed TNT.

## 2026-08-03 (already-primed TNT explosion response)

- Extended the exact fixture path by one bounded second primed-TNT record,
  preserving both Java entity IDs, fuses, positions, and motion. With the
  fuse-one source ID first, old magma retires the source but leaves the target
  on its gravity-only trajectory at
  `c/magma/trace/out/matrix_tnt_explosion_primed_tnt_probe_1/summary.md`.
- Added the exact 0.98-cube target AABB and TNT's overridden zero eye height to
  the active explosion enumeration. `attackEntityFrom` remains false, but the
  explosion impulse is independent of damage. The existing primed-TNT loop
  naturally preserves both loaded orders: source-first moves and damps the
  fresh impulse, while target-first retains its pre-blast position and raw
  later impulse. A final guard also permits a lone primed TNT to receive a
  non-TNT explosion instead of overfitting the count to self-detonation.
- Source-first and target-first each repeat 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_primed_tnt_repeat_1/summary.md` and
  `c/magma/trace/out/matrix_tnt_explosion_primed_tnt_target_first_repeat_1/summary.md`.
  Both pass together on the final source at
  `c/magma/trace/out/matrix_tnt_explosion_primed_tnt_orders_final_2/summary.md`,
  and ten neighboring TNT/entity controls plus both orders pass 12/12 at
  `c/magma/trace/out/matrix_tnt_explosion_primed_tnt_affected_2/summary.md`.
- The focused native test passes in 25.83 seconds with a 46,080 KB peak at
  `c/magma/trace/out/test_tnt_explosion_primed_tnt_orders_final.log`. The full
  CPU aggregate passes in 5:47.15 with a 307,080 KB peak, zero swap, and exit
  0 at `c/magma/trace/out/test_runtime_tnt_explosion_primed_tnt_final.log`.
  The work is fixed-capacity and active-explosion-only. GPU timing remains
  deferred while GPU 1 is shared, so the promoted total remains 676. The next
  bounded target is an End crystal.

## 2026-08-03 (standalone and TNT-triggered End-crystal response)

- Added an exact standalone `EntityEnderCrystal` save-state fixture carrying
  authoritative ID, position, `innerRotation`, and base-plate state through
  the Java oracle, capsule replay, magma script, and entity comparator. The
  ordinary one-tick control advances rotation 0 to 1 exactly at
  `c/magma/trace/out/matrix_end_crystal_idle_candidate_1/summary.md`.
- Paired a lower-ID fuse-one TNT with a crystal seven blocks away in high open
  air. Disabling only the new response leaves magma's crystal alive at
  rotation 1 while Java retires it at
  `c/magma/trace/out/matrix_tnt_explosion_end_crystal_probe_1/summary.md`.
  The corrected active-explosion path destroys the crystal and synchronously
  runs its size-six smoking explosion. Both engines retire TNT and crystal,
  leave the distant player unchanged, produce zero raw block/light mutations,
  and finish at the same `World.rand` cursor after 2,708 advances.
- The explicit state/behavior/block gate repeats 3/3 at
  `c/magma/trace/out/matrix_tnt_explosion_end_crystal_repeat_1/summary.md`.
  The standalone control and 13 neighboring TNT/entity rows pass 14/14 at
  `c/magma/trace/out/matrix_tnt_explosion_end_crystal_affected_final_1/summary.md`.
- Focused native coverage passes in 24.95 seconds with a 37,468 KB peak at
  `c/magma/trace/out/test_tnt_explosion_end_crystal_final.log`. The full CPU
  aggregate passes in 5:43.06 with a 306,684 KB peak and zero swap at
  `c/magma/trace/out/test_tnt_explosion_end_crystal_candidate.log`. The CPU
  performance guard passes at 4,312 scalar steps/s at
  `c/magma/trace/out/perf_guard_tnt_end_crystal_cpu_1.json`.
- The crystal pool is fixed at 16 entries. Empty-pool ticking is one branch,
  and target enumeration runs only during an active explosion. GPU 1 remains
  shared, so Blaze/CUDA promotion evidence is deferred and the promoted total
  remains 676. Dragon-fight notification and beam state remain open.

## 2026-08-03 (End-crystal fire maintenance)

- Extended the exact capsule runner to enter dimension 1 before staging the
  fixture. A crystal is saved above obsidian so vanilla fire support does not
  immediately remove the result. With the runtime write disabled, Java makes
  exactly one raw air-to-fire mutation at the crystal while magma makes none
  at
  `c/magma/trace/out/matrix_end_crystal_fire_probe_2/summary.md`.
- The corrected first tick advances `innerRotation` 0 to 1 and creates that
  exact fire cell. It repeats 3/3 at
  `c/magma/trace/out/matrix_end_crystal_fire_repeat_1/summary.md`; the
  Overworld idle control, End fire row, and TNT-triggered destruction pass
  together at
  `c/magma/trace/out/matrix_end_crystal_fire_affected_final_1/summary.md`.
  Raw blocks are exact. Ambient End block light outside the captured capsule
  is excluded; the diagnostic candidate had zero light mismatches within 15
  blocks of the controlled fire.
- Focused native coverage passes in 26.12 seconds with a 46,092 KB peak and
  zero swap at `c/magma/trace/out/test_end_crystal_fire_final.log`. The native
  fixture explicitly generates the fresh End chunk before editing it. This
  avoids first-tick world generation replacing a test-only pre-generation
  write. The full CPU aggregate passes in 5:42.21 with a 306,804 KB peak, zero
  swap, and exit 0 at
  `c/magma/trace/out/test_runtime_end_crystal_fire_final.log`. GPU 1 remains
  shared and was not touched.

## 2026-08-03 (saved End-crystal beam target and live view)

- Extended authoritative crystal state and the save-state fixture with an
  explicit beam-presence bit and exact target block. The old product loses
  only `has_beam` at tick 0 while all other observed state and the complete
  22,869-cell raw block/light volume stay exact at
  `c/magma/trace/out/matrix_end_crystal_beam_probe_1/summary.md`.
- The corrected runtime preserves target `(20,102,1)` as rotation advances 0
  to 1. The row repeats 3/3 at
  `c/magma/trace/out/matrix_end_crystal_beam_repeat_1/summary.md`, and idle,
  beam, End-fire, and TNT-destruction controls pass 4/4 at
  `c/magma/trace/out/matrix_end_crystal_beam_affected_1/summary.md`.
- Saved standalone crystals now enter both frame-capture and interactive-game
  render-view collectors with their exact entity ID, rotation, bottom flag,
  and beam target. Focused native coverage passes in 24.97 seconds with a
  44,664 KB peak and zero swap at
  `c/magma/trace/out/test_end_crystal_beam_candidate.log`. The collector is
  fixed-capacity, allocation-free, and exits after a single count check when
  no saved crystals exist. The CPU-only performance guard passes at 4,195
  scalar steps/s at
  `c/magma/trace/out/perf_guard_end_crystal_beam_cpu_1.json`. The full CPU
  aggregate passes in 6:04.60 with a 306,856 KB peak, zero swap, and exit 0 at
  `c/magma/trace/out/test_runtime_end_crystal_beam_final.log`; the shared-host
  wall time is excluded from performance promotion. Beam geometry and pixels
  remain a separate renderer slice; GPU 1 was not touched.

## 2026-08-03 (End-crystal beam geometry and pixel path)

- Captured a real-Java superflat scenario whose saved End crystal targets a
  nearby block. Before the fix, magma renders the crystal but omits the large
  diagonal beam; `pxdiff.py` measures the missing structure as content. The
  golden tape and report are
  `c/magma/raster/verify/tapes/scenario_end_crystal_beam_20260803T160737Z.jsonl`
  and
  `c/magma/raster/verify/trace/report/tape_scenario_end_crystal_beam_20260803T160737Z.md`.
- Ported `RenderDragon.renderCrystalBeams` into a separate entity pass with
  standard item lighting disabled and the owning entity's lightmap retained:
  exact target/bob translation and rotations, tapered eight-sided smooth
  strip, scrolling UVs, and two-sided coverage for vanilla's disabled-cull
  state. The generated asset header now also carries the real 16x256
  `endercrystal_beam.png` as a standalone contiguous texture; the shared
  CPU/CUDA shader supports opt-in GL repeat without changing any atlas pass.
  The jar checker proves every beam texel and its 838-opaque/3258-transparent
  alpha histogram. Native tests pin absent/present behavior, atomic capacity,
  ring centers, black-to-white endpoints, V scroll, and integer UV repeat.
- The pixel-only sky strip at tick 14 falls from 1,799 differing pixels before
  the renderer to 284 after it, an 84.2% reduction. Whole-frame mean/channel
  falls from 9.70 to 8.91. The aligned output is at
  `c/magma/trace/out/end_crystal_beam_sky_after.png`. The scenario remains a
  diagnostic rather than a false full-scene pass because it also contains
  known terrain/fog, HUD, crystal-base, and slime residuals. The remaining
  284-pixel beam-region residual stays open in V-01.
- Replaced the tape's ambiguous `(-1,-1,-1)` no-target encoding with an
  explicit presence bit while preserving legacy-tape parsing. This keeps every
  signed BlockPos target representable. Idle, beam, End-fire, and TNT-first
  controls pass 4/4 at
  `c/magma/trace/out/matrix_end_crystal_beam_geometry_affected_1/summary.md`.
  The exact jar and focused renderer gates pass, as does the full CPU aggregate
  in 4:51.43 with a 306,704 KB peak, zero swap, and exit 0 at
  `c/magma/trace/out/test_runtime_end_crystal_beam_geometry_final.log`.
  The CPU guard passes at 5,044 scalar steps/s at
  `c/magma/trace/out/perf_guard_end_crystal_beam_geometry_cpu_1.json`.
  GPU 1 was shared and no GPU process, CUDA test, or CUDA timing run was
  touched; the promoted total remains 676.

## 2026-08-03 (dragon-fight crystal notification)

- Split arena-crystal destruction into the same synchronous boundaries as
  Java: mark dead, complete the crystal's size-six explosion, then call the
  dragon notification. Melee, player-arrow, small-fireball, and recursive
  explosion routes now share that transition. The live render type was also
  corrected from the obsolete marker value 8 to the crystal renderer's 31.
- Added a parked real-Java probe around the actual
  `EntityDragon.onCrystalDestroyed` method and a shared C comparator. The
  engines agree on healing/player `100 -> 90` plus strafe,
  non-healing/player `100 -> 100` plus strafe, and healing/generic with no
  nearby player `100 -> 90` while holding. The runner is
  `c/magma/trace/test_dragon_crystal_notification.py`.
- Focused dragon lifecycle and exact TNT-chain tests pass at
  `c/magma/trace/out/test_dragon_live_crystal_notify.log` and
  `c/magma/trace/out/test_tnt_explosion_dragon_notify.log`. Idle, saved beam,
  End fire, and TNT-first Java-vs-C controls pass 4/4 at
  `c/magma/trace/out/matrix_dragon_crystal_notify_affected_1/summary.md`.
  The full CPU aggregate passes in 4:48.18 with a 306,408 KB peak and zero
  swap at `c/magma/trace/out/test_runtime_dragon_crystal_notify_final.log`;
  the CPU guard passes at 4,808 SPS at
  `c/magma/trace/out/perf_guard_dragon_crystal_notify_final_cpu_1.json`.
- Respawn-sequence abort/reset, the full vanilla phase graph, and lethal
  crystal-notification transition stay open. GPU 1 was shared and no GPU
  process or CUDA test was touched, so the promoted total remains 676.

## 2026-08-03 (live dragon healing beam and healer cadence)

- Added dragon-owned `ticksExisted` and live healing-crystal relation state to
  the simulation, tape schema, replay loader, and render-view path. Live arena
  crystals now reach the existing complete End-crystal renderer with type 31,
  rotation, and base state. The shared 96-vertex two-sided beam emitter serves
  both saved target beams and dragon healing beams.
- Captured a stable 43-frame real-Java llvmpipe scenario at
  `c/magma/raster/verify/tapes/scenario_dragon_healing_beam_20260803T173302Z.jsonl`.
  Its structural pixel gate passes. A controlled beam-off replay exposed that
  the first implementation incorrectly rendered fullbright at night. Keeping
  standard item lighting disabled while retaining the dragon's lightmap
  improves tick 10 from 2.72 to 2.71 mean/channel over the whole frame and
  10.64 to 10.20 in the beam-containing region. The remaining mid-beam
  residual is measured as `texel-selection` and remains open in V-01.
- Ported the exact bounded `updateDragonEnderCrystal` ordering: clear a dead
  current crystal, heal a living one on `ticksExisted % 10 == 0`, consume the
  one-in-ten entity-RNG selection gate, then select the nearest living crystal
  in Java's dragon-AABB-expanded query with first-on-tie behavior. A locked
  real-Java command oracle agrees with C on nine transitions at
  `c/magma/trace/out/test_dragon_healer_java_c.log`.
- Focused dragon/render tests and the deterministic CPU dragon runner pass.
  The full native aggregate passes in 5:35.65 with a 306,600 KB peak and exit
  0 at `c/magma/trace/out/test_runtime_dragon_healer_1.log`. The CPU guard
  passes with a 4,984 steps/s median against the frozen 4,062 baseline and
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_dragon_healer_cpu_1.json`.
- This closes the bounded healer method for a given entity-RNG stream, not the
  complete natural dragon fight. Missing phase-AI RNG consumers can shift the
  later stream, and the full phase graph plus remaining beam texel selection
  stay open. GPU 1 was shared and no GPU process, CUDA test, or CUDA timing run
  was touched; the promoted total remains 676.

## 2026-08-03 (bounded bed-explosion fire)

- Source tracing corrected the initial assumption before implementation:
  `EntityEnderCrystal` and primed TNT call the non-flaming
  `createExplosion(..., smoking=true)` overload. Invalid-dimension beds call
  `newExplosion(..., flaming=true, smoking=true)`. The new positive therefore
  belongs only to the bed-style branch, with the other overload as a negative.
- Extended the shared explosion ray kernel to retain affected air positions
  separately from destroyed non-air blocks. The live flaming pass runs after
  removals, checks exact full-block support, consumes the independent
  `Explosion.explosionRNG.nextInt(3)` gate, places fire, and schedules its
  first update through the existing World.rand fire path.
- Added a parked real-Java command around the actual 1.11.2 `Explosion` class.
  It restores both the world and private explosion RNG cursors and constructs
  one eligible obsidian-supported air cell. Java and C agree on the flaming
  one-fire outcome and the otherwise identical non-flaming zero-fire control
  at `c/magma/trace/out/test_explosion_fire_java_c.log`. The existing 128-line
  Java/CPU explosion kernel remains exact.
- Focused native coverage passes in 26.47 seconds with an 82,960 KB peak at
  `c/magma/trace/out/test_bed_explosion_fire_1.log`. The full CPU aggregate
  passes in 5:54.29 with a 306,676 KB peak at
  `c/magma/trace/out/test_runtime_bed_explosion_fire_1.log`; the scalar guard
  passes at a 4,414 steps/s median against the frozen 4,062 baseline and
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_bed_explosion_fire_cpu_1.json`.
- Java does not serialize the clock-seeded per-explosion RNG in a world save.
  Exact replay therefore injects its captured internal cursor; standalone
  magma uses a deterministic event-local fallback. Broader multi-support
  HashSet order, capsule transport for this transient cursor, drops, and the
  full resistance table stay open. The branch is explosion-only and
  allocation-free. GPU 1 was shared and untouched, so the promoted total
  remains 676.

## 2026-08-03 (exact supported gravel lifecycle)

- Added a locked real-Java gravel case before changing the runtime. The
  pre-fix result showed the boundary clearly: Java removed gravel at
  `(12,80,8)`, exposed one `EntityFallingBlock` for ticks 1 through 9, landed
  gravel at `(12,78,8)` on tick 10, and drained its `+2` callback on tick 12;
  magma retained the source and made no mutations. Evidence is at
  `c/magma/trace/out/matrix_falling_gravel_prefix/summary.md`.
- Generalized the existing proof-safe BlockFalling callback from hard-coded
  sand to metadata-0 sand or gravel. The fixed-capacity entity now preserves
  the scheduled block ID through spawn, view rendering, exact gravity/drag,
  landing, and stability rescheduling. The state capsule validates the same
  clear-column and stone-support contract for block 13.
- Added native gravel coverage for the pending boundary, entity-ID cursor,
  block/render identity, exact first and ninth trajectory points, tick-10
  landing, `+2` callback and drain, and nonzero-metadata rejection. The full
  native aggregate passes in 6:15.92 with a 303,808 KB peak at
  `c/magma/trace/out/test_runtime_falling_gravel_focused_1.log`.
- The final Java/C matrix runs sand as an affected control beside gravel. Both
  pass with 26 simulated features matching, exact behavior gates, and all
  10,625 raw cells exact at
  `c/magma/trace/out/matrix_falling_blocks_java_c_final/summary.md`. The CPU
  guard passes at a 5,085 steps/s median against the frozen 4,062 baseline and
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_gravel_cpu_1.json`.
- Water/lava/fire passthrough, unsupported landing and drop behavior, gravel
  flint RNG, lateral motion, and anvils remain open. The inactive path adds no
  allocation. GPU 1 was shared and no GPU process, CUDA test, or timing run was
  touched; the promoted total remains 676.

## 2026-08-03 (exact falling-block passthrough materials)

- Traced the next BlockFalling boundary to Java's shared
  `canFallThrough` predicate, which admits fire plus air, water, and lava. Added
  independent one-cell fixtures for sand through still water, gravel through
  static lava, and sand through fire, all over the existing stone support.
- Captured the pre-fix boundary before changing C. Java spawned the exact
  falling entity in every case, while magma rejected the restored callback at
  the first C admission with `invalid schedule_tick`; the three red results
  are at `c/magma/trace/out/matrix_falling_passthrough_prefix/summary.md`.
- Generalized only the proof-safe material predicate. The existing
  fixed-capacity BlockFalling state continues to own EID allocation, exact
  gravity/drag, rendering identity, landing replacement, and stability
  scheduling. No allocation or RNG was added.
- The final five-row matrix includes air/sand and air/gravel as affected
  controls. All five pass with 26 simulated features matching, exact
  trajectories and callback queues, exact two-cell raw mutations, and exact
  10,625-cell block and block-light volumes at
  `c/magma/trace/out/matrix_falling_passthrough_final/summary.md`.
- Native coverage checks all three passthrough IDs, both falling IDs, the
  entity-ID cursor, ninth trajectory point, replacement, queue drain, and
  unchanged World.rand. The full CPU aggregate passes in 6:14.21 with a
  309,664 KB peak at
  `c/magma/trace/out/test_runtime_falling_passthrough_1.log`. The scalar guard
  passes at a 4,346 steps/s median against the frozen 4,062 baseline and
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_passthrough_cpu_1.json`.
- This closes a single replaceable material cell, not dynamic fluid flow or
  columns, fire callbacks, nonreplaceable placement/drop behavior, gravel
  flint RNG, lateral motion, or anvils. GPU 1 was shared and untouched, so
  CUDA and Blaze remain deferred and the promoted total stays 676.

## 2026-08-03 (bottom-slab falling-block item drop)

- Added a real-Java fixture for metadata-0 sand over a bottom stone slab. The
  pre-fix trace establishes the first causal boundary: Java exposes the
  falling entity through fallTime 11 and creates an age-1, pickup-delay-9 sand
  item at tick 12, while magma rejects the scheduled callback. The trace is at
  `c/magma/trace/out/matrix_falling_sand_bottom_slab_drop_prefix/summary.md`.
- Added a parked command around the actual Java
  `EntityFallingBlock.onUpdate` method and a small full-runtime C oracle. For
  both sand and gravel, the engines agree on 24 total falling updates, source
  and slab state, one matching item, exact first-tick position/motion/yaw,
  four Math.random calls, and two entity IDs. Three repeats pass at
  `c/magma/trace/out/falling_drop_java_c_repeat_3.log`.
- The runtime now retains a fractional landing height for the bottom slab and
  takes the failed-placement drop branch atomically. It does not place over
  the slab or queue a stability callback. The common item collision path keeps
  one block lookup and only reads metadata after recognizing a slab ID.
- The 13-tick loaded-world row passes its structural behavior, exact sole
  source removal, unchanged slab, and 10,625/10,625 blocks and block-light
  cells at
  `c/magma/trace/out/matrix_falling_sand_bottom_slab_drop_candidate/summary.md`.
  Its entity-set row is diagnostic because ambient Java falling blocks consume
  the global Math/EID streams after the saved boundary; the parked oracle is
  the strict event-local authority. All five earlier falling controls remain
  strict at
  `c/magma/trace/out/matrix_falling_blocks_after_slab_drop/summary.md`.
- The CPU performance guard passes at a 4,230 steps/s median against the 4,062
  baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_slab_drop_cpu_1.json`. Other partial
  landing shapes, lateral motion, anvils, timeouts, and capacity pressure stay
  open. GPU 1 was shared and untouched.
- The final native aggregate passes in 6:10.03 with a 310,780 KB peak at
  `c/magma/trace/out/test_runtime_falling_slab_drop_final.log`.

## 2026-08-03 (scheduled fire with `doFireTick=false`)

- Added a strict real-Java scheduled-fire case with the global gamerule off.
  The callback remains pending for observations 0-1, drains on observation 2,
  leaves the source fire and east plank unchanged, consumes no callback RNG,
  and creates no successor. The pre-fix capsule omission is retained at
  `c/magma/trace/out/matrix_fire_tick_disabled_prefix_3/summary.md`.
- Corrected the oracle command mapping to send Minecraft's literal
  `true`/`false` values, transported the captured global through the neutral
  capsule, and added `set_do_fire_tick` to the C script/runtime boundary. A due
  disabled fire entry is popped by the common scheduler and returns at the
  outer guard before proof-region, RNG, block, or reschedule work.
- The exact-source clean-build row passes with 26 matching simulated features,
  zero divergences, an exact pending lifetime, zero raw mutations, and all
  10,625 block cells equal at
  `c/magma/trace/out/matrix_fire_tick_disabled_clean_final/summary.md`. Three
  repeats pass at
  `c/magma/trace/out/matrix_fire_tick_disabled_repeat_3/summary.md`. Enabled
  scheduled and direct-callback controls remain strict at
  `c/magma/trace/out/matrix_fire_rule_pair/summary.md` and
  `c/magma/trace/out/matrix_fire_callback_control/summary.md`.
- Native coverage verifies the default, setter validation, exact queue drain,
  unchanged blocks, and unchanged World.rand, Math.random, Block.RANDOM,
  updateLCG, and entity-ID cursors. The full aggregate passes in 7:10.18 with a
  306,748 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_fire_disabled.log`.
- The scalar guard attempt measured 3,373 steps/s while unrelated TAK jobs were
  consuming 64 CPU threads. That sample is preserved at
  `c/magma/trace/out/perf_guard_fire_tick_disabled_cpu_1.json` but is not a
  comparable promotion measurement. The feature changes no `mc-sim` code and
  adds only one callback-local branch, with no idle scan or allocation. The
  preceding clean 4,230 steps/s result remains the current floor evidence until
  the competing workload retires. GPU 1 was shared and untouched.

## 2026-08-03 (isolated burnout and infinite fire sources)

- Added a strict age-four no-neighbor regression for the already-ported
  ordinary-source burnout branch. It proves the original-age decision,
  `nextInt(3)` metadata write, `nextInt(10)` reschedule, retained stale
  successor, and sole fire-to-air mutation at
  `c/magma/trace/out/matrix_fire_burnout_age_4_candidate/summary.md`.
- Locked the first real missing source boundary before changing C. Java kept
  age-15 fire on overworld netherrack and one successor while the old capsule
  filtered the entry, producing the sole queue divergence at
  `c/magma/trace/out/matrix_fire_netherrack_source_prefix/summary.md`.
  The shared proof now admits netherrack, whose vanilla `isFireSource(UP)`
  predicate was already present in the callback core.
- Added separate scheduled and parked direct netherrack cases. The scheduled
  case owns pending lifetime, dispatch, and replacement queue; the parked case
  owns the uncontaminated seven-draw cursor because loaded-world Java performs
  unrelated RNG work after a scheduled callback. All six repeats pass at
  `c/magma/trace/out/matrix_fire_netherrack_source_repeat_3/summary.md`. The
  netherrack native aggregate passes in 6:08.25 with a 309,508 KB peak and zero
  swap at `c/magma/trace/out/test_runtime_fire_netherrack.log`.
- Extended the same proof to dimension-one bedrock, matching Java's End-only
  `isFireSource(UP)` rule. End scheduled observation crosses a different
  server-world boundary and is not used as a cursor authority; the parked
  direct callback proves the exact seven draws, unchanged age-15 fire, and
  persistent source queue. Three repeats pass over 22,869 cells at
  `c/magma/trace/out/matrix_fire_end_bedrock_source_callback_repeat_3/summary.md`.
  Native coverage separately proves exact delayed dispatch and +39 successor.
- All seven old and new fire controls pass together with 26 matching simulated
  features, zero divergences, exact queues/cursors, and exact raw blocks at
  `c/magma/trace/out/matrix_fire_eternal_source_family/summary.md`. The final
  native aggregate passes in 6:31.14 with a 313,392 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_fire_end_bedrock.log`.
- After the unrelated 64-thread self-play workload retired, the comparable CPU
  guard passed at 4,282 steps/s against the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_fire_eternal_sources_cpu_1.json`. Proof checks
  execute only for an active fire callback and add no allocation or idle scan.
  GPU 1 remained shared and untouched.

## 2026-08-03 (steady-rain age-15 fire branches)

- Added a selectable clear/rain oracle setup. Rain keeps vanilla
  `doWeatherCycle` enabled, waits until effective rain is visible, and captures
  authoritative rain/thunder timers plus all five `BlockFire.canDie`
  `isRainingAt` probes. The neutral capsule restores that weather and exposure
  context only for the bounded admitted fire callback.
- Added scheduled and parked direct fixtures for both sides of the exact
  age-15 rain threshold. `Random(1024)` draws
  `0.6392364501953125 < 0.65F`, changes only fire 51:15 to air, schedules no
  successor, and advances `0x5DEECE26D -> 0xA3A500C65674`. The audit-requested
  negative uses `Random(0)`: `0.7309677600860596` fails the threshold,
  `nextInt(10)=8` creates the stale +38 successor before isolated burnout, and
  the cursor advances `0x5DEECE66D -> 0xD4D95138AB6F`.
- All four success/failure scheduled/direct cases pass with 26 matching
  simulated features, zero divergences, exact queues/cursors, the sole
  fire-to-air mutation, and all 10,625 raw cells equal at
  `c/magma/trace/out/matrix_fire_rain_age15_branches_candidate/summary.md`.
  Three independent repeats per branch pass at
  `c/magma/trace/out/matrix_fire_rain_age15_branches_repeat_3/summary.md`.
  All 11 earlier clear/gamerule/source and new rain branches pass together at
  `c/magma/trace/out/matrix_fire_weather_branches_family/summary.md`.
- The final-source native aggregate includes missing-context rejection, the
  one-draw early return, the failed-roll reschedule/burnout path, exact weather
  timer advancement, and exact RNG cursors. It passes in 6:26.65 with a
  316,732 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_fire_rain_age15.log`. The final scalar guard
  passes at 4,187 steps/s against the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_fire_rain_age15_final_cpu.json`. The proof is visited
  only for an active fire callback, performs bounded cell checks, allocates
  nothing, and adds no idle scan. GPU 1 stayed shared and untouched.
- Extended the same admitted proof to a covered source with all four cardinal
  `isRainingAt` probes exposed. Scheduled and parked direct fixtures both
  extinguish through `BlockFire.canDie`; all 10,625 final cells and 26 simulated
  state features are exact. The first scheduled repeat exposed an oracle-harness
  race: resetting World.rand at `ServerTickEvent.START` allowed two to five
  unrelated draws before `WorldServer.tickUpdates`. A narrowly armed BlockFire
  mixin now restores and records the cursor at the actual callback boundary.
  The 18-case three-repeat rain set passes at
  `c/magma/trace/out/matrix_fire_rain_exact_hook_repeat_3/summary.md`, with
  `0x5DEECE26D -> 0xA3A500C65674` on every successful scheduled callback and
  `0x5DEECE66D -> 0xD4D95138AB6F` on every failed-roll callback. All 13 fire
  controls pass together at
  `c/magma/trace/out/matrix_fire_weather_branches_exact_hook_family/summary.md`.
  This follow-on changed only oracle instrumentation, fixtures, and gates; the
  already-bounded magma proof and its performance result are unchanged.

## 2026-08-03 (thunder-state age-15 fire)

- Added deterministic oracle `thunder` setup. It keeps weather-cycle ticking,
  waits 83-85 controlled ticks for effective `isThundering`, and captures the
  exact rain/thunder timers before the shared boundary. The capsule advertises
  and restores a separate exact steady-thunder fire slice.
- Vanilla BlockFire has no thunder-specific branch after effective rain is
  established. Magma now admits thundering inside the same bounded age-15
  air/stone proof, consumes the same `Random(1024)` float, removes only fire
  51:15, and schedules no successor. Scheduled and parked direct cases pass
  three independent times each at
  `c/magma/trace/out/matrix_fire_thunder_age15_repeat_3/summary.md`; all 15
  affected fire/weather rows pass at
  `c/magma/trace/out/matrix_fire_weather_thunder_family/summary.md`.
- The full native suite adds a thundering direct-callback assertion and passes
  in 7:28.51 with a 313,616 KB peak and exit status zero at
  `c/magma/trace/out/test_runtime_fire_thunder.log`. The base inactive path is
  unchanged and the active proof removes one rejection check. Scalar recaptures
  at 3,148 and 3,576 steps/s are retained only as contaminated diagnostics:
  the host was at load 81-85 under an unrelated 64-thread self-play process.
  The last clean 4,187 steps/s result remains above the 3,858.9 floor; GPU 1
  stayed shared and untouched.

## 2026-08-03 (Nether netherrack infinite fire source)

- Extended the bounded dry fire capsule and runtime proof to dimension -1.
  Age-15 fire above netherrack survives unchanged, consumes the exact seven
  direct-callback draws through cursor `0xB6679B27AF7E`, and retains one +39
  source schedule, matching Java.
- The first candidate correctly exposed five unrelated naturally generated
  Nether fires inside the default capture box. A 17-cubed box still captures
  the target's complete causal neighborhood while excluding those unrelated
  callbacks; it is a stricter boundary, not a relaxed comparison. The corrected
  candidate passes, and three independent repeats prove 4,913/4,913 raw cells
  plus the exact source queue at
  `c/magma/trace/out/matrix_fire_nether_netherrack_repeat_3/summary.md`.
- All 16 current clear/gamerule/source/rain/thunder controls pass together at
  `c/magma/trace/out/matrix_fire_weather_nether_family/summary.md`. The native
  aggregate adds separate dimension -1 delayed-dispatch coverage and passes in
  6:39.34 with a 314,964 KB peak, zero swap, and exit status zero at
  `c/magma/trace/out/test_runtime_fire_nether_netherrack.log`.
- The runtime change only expands an active-callback dimension predicate and
  adds no idle scan or allocation. CPU recapture remains deferred while the
  unrelated 64-thread host workload runs; the last clean result is 4,187
  steps/s against the unchanged 3,858.9 floor. GPU 1 stayed shared and
  untouched, so the promoted total remains 676.

## 2026-08-03 (fire ignites adjacent TNT)

- Added the missing bridge from `BlockFire.tryCatchFire` to the already exact
  primed-TNT machinery. Like Java, magma retains the original TNT state,
  writes fire or air according to the fire roll, then invokes TNT's EXPLODE
  destruction path. A bounded capacity guard rejects the callback before RNG
  or block mutation if the fixed TNT pool cannot represent every direct TNT.
- The first admitted red row was exact for both block mutations across all
  10,625 cells, but had precisely the expected two state divergences: no
  primed entity and no Math-RNG/entity-ID cursor advance. That causal failure
  is retained at
  `c/magma/trace/out/matrix_fire_tnt_candidate_red_2/summary.md`.
- With public `Random(4)`, the fixed direct callback ages source fire 0-to-1,
  burns the east TNT to air, preserves the source queue, advances World RNG
  `0x5DEECE669 -> 0x1411389CAF08`, advances Math RNG exactly twice, allocates
  one exact entity ID, and matches the primed TNT's first-tick fuse 79,
  position, and motion. Three independent repeats pass at
  `c/magma/trace/out/matrix_fire_tnt_repeat_3/summary.md`; all 17 current
  fire/weather rows pass at
  `c/magma/trace/out/matrix_fire_weather_tnt_family/summary.md`.
- The native aggregate adds both atomic pool-full rejection and the successful
  constructor/same-boundary tick. It passes with exit status zero in 5:28.44,
  at 315,368 KB peak RSS and zero major faults, with timing metadata at
  `c/magma/trace/out/test_runtime_fire_tnt.time`. The new work occurs only for
  an active fire callback that successfully targets TNT, uses fixed storage,
  and adds no idle scan or allocation. GPU 1 stayed shared and untouched; the
  promoted total remains 676 pending clean performance evidence.

## 2026-08-03 (bounded source-column fire humidity)

- Transported the real Java source-column `isBlockinHighHumidity` predicate
  for one admitted fire callback. The active callback now applies vanilla's
  `-50` direct-target denominators and halves its volumetric spread threshold;
  C does not infer biome humidity from its own world generator.
- Added two independent normal/swamp discriminator pairs. `Random(0)` makes
  normal `nextInt(300)=229` miss east TNT while humid `nextInt(250)=29`
  ignites it with exact World/Math/entity cursors. `Random(776)` makes both
  direct paths miss, then normal threshold two admits the first volumetric
  roll two as age-one fire while humid threshold one rejects it.
- The direct pair passes at
  `c/magma/trace/out/matrix_fire_humidity_candidate_fix_1/summary.md`. Both
  spread cases pass three independent repeats at
  `c/magma/trace/out/matrix_fire_humidity_spread_repeat_3/summary.md`, and all
  21 affected fire/weather rows pass at
  `c/magma/trace/out/matrix_fire_weather_humidity_spread_family/summary.md`.
- The full helper/runtime aggregate passes in 6:39.05 at 321,096 KB peak and
  exit zero at `c/magma/trace/out/test_runtime_fire_humidity.time`. A runtime
  rerun with the final spread assertions passes in 5:53.34 at 253,264 KB peak,
  zero major faults, and exit zero at
  `c/magma/trace/out/test_runtime_fire_humidity_spread.time`. The branch runs
  only inside an active fire callback, uses no allocation or idle scan, and
  leaves the promoted total at 676 until a clean performance recapture.

## 2026-08-03 (rain-aware direct fire target)

- Added a paired precipitation-transparent tall-grass proof on an eternal
  netherrack source. A roof two cells above the east target changes only
  `isRainingAt(east)`, leaving source exposure and all successful fire rolls
  matched. An earlier plank probe was rejected because a full plank itself
  blocks precipitation and therefore could not measure this branch.
- The valid old-runtime probe ages source fire zero-to-one in both engines but
  differs at exactly one cell: Java burns the exposed tall grass to air while
  magma creates age-zero fire. Its child queue and World cursor are the only
  causal state differences at
  `c/magma/trace/out/matrix_fire_rain_direct_target_red_3/summary.md`; the
  roofed negative already passes.
- The runtime now honors the transported east-target rain predicate after a
  successful chance/fate roll. Exposed `Random(5)` ends at
  `0x72D7583447FB` with no child, while the covered control consumes age and
  schedule draws through `0xB29D468F3AAD` and queues the exact +35 fire.
  Both pass 3/3 at
  `c/magma/trace/out/matrix_fire_rain_direct_target_repeat_3/summary.md`, and
  all 23 affected rows pass at
  `c/magma/trace/out/matrix_fire_weather_rain_target_family/summary.md`.
- Native coverage locks both branches; the runtime aggregate passes in
  5:32.01 at 253,108 KB peak with zero major faults and exit zero at
  `c/magma/trace/out/test_runtime_fire_rain_direct_target.time`. The work is
  active-callback-only, fixed-state, and allocation-free. Clean performance
  promotion remains deferred while the unrelated 64-thread workload runs.

## 2026-08-03 (rain-aware volumetric fire candidate)

- Added an exposed/covered pair for the volume-loop rain guard. Carpet keeps
  the west air candidate encouraged without blocking precipitation; five
  roofs two cells above its `canDie` probe positions create the covered twin
  without changing the source or candidate support.
- With public `Random(125)`, both callbacks consume the same source and direct
  draws and reach the first volume roll `0 <= 3`. Before the fix, magma created
  fire in the exposed candidate while Java suppressed it; that sole raw-cell
  divergence and its child-queue/cursor consequences are retained at
  `c/magma/trace/out/matrix_fire_rain_volume_red_3/summary.md`.
- The runtime now consumes the successful threshold roll and applies the
  transported candidate `canDie` predicate before any age/schedule draws.
  Exposed ends at World cursor `0x06F23450DB83` with no mutation or child;
  covered creates age-zero fire, queues +35, and ends at
  `0xE9AD9F0B0D75`. Candidate rows pass at
  `c/magma/trace/out/matrix_fire_rain_volume_candidate_1/summary.md`, all six
  promotion repeats pass at
  `c/magma/trace/out/matrix_fire_rain_volume_repeat_3_final/summary.md`, and
  the 25-case affected family passes at
  `c/magma/trace/out/matrix_fire_weather_rain_volume_family_final/summary.md`.
- The full native runtime aggregate passes in 5:40.24 at 252,144 KB peak with
  zero major faults and exit zero at
  `c/magma/trace/out/test_runtime_fire_rain_volume.time`. The added state is
  one callback-local Boolean; there is no allocation, idle scan, or broader
  world exposure inference. Clean scalar/GPU promotion remains deferred under
  the unrelated 64-thread CPU workload and shared GPU 1.

## 2026-08-03 (first non-plank fire material)

- Added a strict dry east-wool callback alongside the plank control. The fire
  kernel already contained Java's wool encouragement 30 and flammability 60;
  the missing behavior was the bounded runtime/capsule proof admission.
- Public `Random(36)` burns white wool to age-zero fire, leaves the source at
  age zero, queues both callbacks at +35, and consumes the exact eleven draws
  through `0x8EBD372F3662`. No Math/Block/update-LCG/entity cursor changes.
  The old runtime's proof rejection is retained at
  `c/magma/trace/out/matrix_fire_wool_red_1/summary.md`.
- The corrected row passes at
  `c/magma/trace/out/matrix_fire_wool_candidate_1/summary.md`, all three
  independent repeats pass at
  `c/magma/trace/out/matrix_fire_wool_repeat_3/summary.md`, and all 26 affected
  fire/weather cases pass at
  `c/magma/trace/out/matrix_fire_weather_wool_family/summary.md`.
- The native aggregate passes in 6:32.33 at 324,036 KB peak with zero major
  faults and exit zero at `c/magma/trace/out/test_runtime_fire_wool.time`.
  There is no kernel rewrite, allocation, idle scan, or off-profile work.

## 2026-08-03 (fire-driven Nether portal activation)

- Routed generic, live-player, spread, projectile, and explosion fire
  additions through one `BlockFire.onBlockAdded` equivalent. In dimensions
  zero and minus one it first invokes the existing verified portal matcher;
  successful activation returns before support checks, fire scheduling, or
  RNG, matching Java.
- The valid old-runtime fixture has 26 matching state features and exact
  controlled cursors but leaves fire plus five air cells where Java creates
  six X-axis portal 90:1 cells. That isolated red is retained at
  `c/magma/trace/out/matrix_fire_portal_red_1/summary.md`.
- The corrected smallest 2x3 frame creates all six portal cells atomically
  with no queue or cursor draw. A top-obsidian-missing negative retains 51:0,
  consumes only `Random(36).nextInt(10)=9` through
  `0xBA4D5B0FA320`, and queues the exact +39 callback. Both pass three repeats
  at `c/magma/trace/out/matrix_fire_portal_pair_repeat_3_final/summary.md`, and
  all 28 affected fire/weather cases pass at
  `c/magma/trace/out/matrix_fire_weather_portal_family_final/summary.md`.
- A cheap necessary-frame scan checks at most 21 cells below and 21 distances
  on both horizontal axes before entering the 32-cubed matcher. It runs only
  when new fire is added, not on idle ticks or recurring source callbacks. The
  native aggregate passes in 6:32.19 at 324,464 KB peak with zero major faults
  and exit zero at
  `c/magma/trace/out/test_runtime_fire_portal_final.time`.

## 2026-08-03 (falling sand on a top-half slab)

- Added the complementary stone-slab collision case. A top-half slab has a
  full-height collision surface, so sand follows the existing exact nine-tick
  trajectory and lands in the air cell at y=78 instead of taking the
  bottom-slab failed-placement item-drop branch.
- The strict candidate passes with 26 matching state features, exact source
  removal and placement above the unchanged slab, and all 10,625 block and
  block-light cells at
  `c/magma/trace/out/matrix_falling_top_slab_candidate_1/summary.md`. Three
  independent repeats pass at
  `c/magma/trace/out/matrix_falling_top_slab_repeat_3/summary.md`.
- All six strict air/fluid/fire/top-slab landing controls plus the known
  diagnostic bottom-slab row pass together at
  `c/magma/trace/out/matrix_falling_top_slab_family/summary.md`. Native
  coverage locks entity identity, all nine trajectory points, y=78 placement,
  the unchanged slab, and the `+2` callback lifetime.
- The final native aggregate passes in 6:03.99 with a 326,712 KB peak, zero
  major faults, and exit zero at
  `c/magma/trace/out/test_runtime_falling_top_slab.log`. The added work is one
  metadata check while a falling entity is landing, with no allocation or
  idle scan.

## 2026-08-03 (oak-log fire material)

- Added the first low-flammability direct target to the bounded fire proof.
  The kernel already carried Java's oak-log encouragement/flammability 5/5
  row; only the capsule and runtime admission fences excluded block 17.
- Public `Random(57)` leaves source age zero, queues it at +31, burns only the
  east log to age-zero fire, queues that child at +38, and ends the exact
  eleven-draw World cursor at `0x27DB2C1FBC09`. Math, Block, update-LCG, and
  entity-ID cursors remain unchanged.
- The candidate and all three repeats pass at
  `c/magma/trace/out/matrix_fire_log_candidate_1/summary.md` and
  `c/magma/trace/out/matrix_fire_log_repeat_3/summary.md`. All 29 then-current
  fire/weather rows pass at
  `c/magma/trace/out/matrix_fire_weather_log_family/summary.md`.
- The change adds two active proof-fence ID comparisons and no kernel rewrite,
  allocation, idle scan, or off-profile work. Its final native evidence is
  shared with the portal expansion below.

## 2026-08-03 (Nether portal axes and legal staging bounds)

- Added strict real-Java activation for the smallest Z-axis frame: six 90:2
  cells, no fire queue, and no controlled cursor consumption. It passes beside
  the existing X-axis and broken-frame controls.
- Found and isolated a product defect in the live adapter rather than the
  verified matcher. A legal 2x16 X frame ignited at its lowest cell places its
  top obsidian one block beyond the old centered 32-cube. With a corrected
  25x18x25 capture, the old path leaves fire plus 31 air cells where Java makes
  32 portal cells at
  `c/magma/trace/out/matrix_fire_portal_height_red_2/summary.md`.
- The adapter now performs Java's downward empty-cell descent and
  floor-qualified bottom-row edge scans, then aligns the unchanged 32-cube to
  each viable axis. A full legal 21x21 frame fits in local coordinates 0..22,
  so no CPU/CUDA state dimension or allocation changes.
- The fixed tall candidate passes at
  `c/magma/trace/out/matrix_fire_portal_height_candidate_2/summary.md`;
  Z/tall repeats pass 6/6 at
  `c/magma/trace/out/matrix_fire_portal_axis_height_repeat_3/summary.md`; all
  31 affected fire/weather rows pass at
  `c/magma/trace/out/matrix_fire_weather_portal_axis_height_family/summary.md`.
- Focused native tests cover smallest Z, both 21x21 axis maxima, a missing-top
  maximum-frame negative, and generic runtime Z/height-16 fire placement at
  `c/magma/trace/out/test_portal_live_axis_bounds.log`. The combined full
  aggregate passes in 6:17.39 at 331,732 KB peak, zero major faults, and exit
  zero at `c/magma/trace/out/test_runtime_fire_log_portal_axis.log`, with
  timing metadata at
  `c/magma/trace/out/test_runtime_fire_log_portal_axis.time`.
- After stopping only the two oracle clients, the five-sample scalar guard
  passes at a 4,392 steps/s median against the frozen 4,062 baseline and
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_fire_log_portal_axis_cpu_1.json`. GPU 1 remains
  shared and untouched.

## 2026-08-03 (dry tall-grass fire material)

- Admitted metadata-one tall grass to the dry NORMAL fire proof. Public
  `Random(4)` ages source fire zero-to-one, burns the east target 31:1 to air,
  creates no child callback, consumes nine World draws through
  `0x1411389CAF08`, and leaves Math, Block, update-LCG, and EID cursors alone.
- The candidate and three independent repeats pass at
  `c/magma/trace/out/matrix_fire_tallgrass_candidate_1/summary.md` and
  `c/magma/trace/out/matrix_fire_tallgrass_repeat_3/summary.md`. All 30
  affected fire/weather rows pass at
  `c/magma/trace/out/matrix_fire_weather_log_tallgrass_family/summary.md`.
- Native and scalar evidence is shared with the grass-path falling promotion
  below. The callback change is proof-fence-only and adds no allocation, idle
  scan, or off-profile work.

## 2026-08-03 (falling sand on grass path)

- Added the next partial-collision failed-placement case. Metadata-zero grass
  path presents a 15/16 surface, so sand follows the exact trajectory through
  observation 9, clips to y=77.9375 on observation 10, cannot replace the
  occupied path cell, and creates one item without a stability callback.
- The loaded-world gate now follows the item through observation 12 and locks
  exact Y, Y velocity, health, age, pickup delay, sole source removal, and
  unchanged support. Both shaped rows pass at
  `c/magma/trace/out/matrix_falling_shaped_item_lifecycle_final/summary.md`;
  all eight affected falling rows pass at
  `c/magma/trace/out/matrix_falling_grass_path_family/summary.md`.
- Extended the parked real-Java falling command so ambient entity churn cannot
  hide cursor errors. Java and C agree on 44 updates and four exact
  sand/gravel drops across bottom slab and grass path, including item motion,
  yaw, four Math.random calls, and two EIDs, at
  `c/magma/trace/out/test_falling_drop_grass_path.log`.
- The combined native aggregate passes in 6:09.90 at 330,292 KB peak RSS,
  zero major faults, zero swap, and exit zero at
  `c/magma/trace/out/test_runtime_tallgrass_grass_path.log`. The clean scalar
  guard passes at 5,102 steps/s against the frozen 4,062 baseline and 3,858.9
  floor at
  `c/magma/trace/out/perf_guard_tallgrass_grass_path_cpu_1.json`. GPU 1 stayed
  shared and untouched, so the promoted total remains 676 pending CUDA/Blaze
  timing. The exact slice assumes `doEntityDrops=true`; the false gamerule and
  broader support shapes remain open.

## 2026-08-03 (falling sand on soul sand)

- Added the 7/8-height shaped-support boundary between bottom slab and grass
  path. Sand remains active through observation 10 at
  y=77.93686484559572, clips to y=77.875 and drops on observation 11, leaves
  soul sand 88:0 unchanged, and creates no stability callback.
- The proof-fence rejection, corrected candidate, and three repeats are at
  `c/magma/trace/out/matrix_falling_soul_sand_red_1/summary.md`,
  `c/magma/trace/out/matrix_falling_soul_sand_candidate_1/summary.md`, and
  `c/magma/trace/out/matrix_falling_soul_sand_repeat_3/summary.md`. All nine
  affected falling rows pass at
  `c/magma/trace/out/matrix_falling_soul_sand_family/summary.md`.
- The parked Java command now covers bottom slab, soul sand, and grass path.
  Java and C agree on 66 falling updates and six exact sand/gravel item drops,
  including motion, yaw, Math cursor, and EIDs, at
  `c/magma/trace/out/test_falling_drop_shaped_supports.log`.

## 2026-08-03 (bookshelf fire-table correction)

- Found a real duplicated table defect: Java gives bookshelf id 47
  encouragement/flammability 30/20, while both runtime and shared CPU/CUDA
  tables encoded 5/20. A direct `Random(36)` row already passed flammability;
  the isolated netherrack-source volume row makes Java accept
  `Random(263).nextInt(100)=2` against threshold two while old C rejects it
  against threshold one.
- The old result has exactly one wrong cell, one missing child callback, and
  the corresponding queue/cursor divergences at
  `c/magma/trace/out/matrix_fire_bookshelf_table_red_1/summary.md`. Both fixed
  rows pass at
  `c/magma/trace/out/matrix_fire_bookshelf_table_candidate_1/summary.md`, all
  six repeats pass at
  `c/magma/trace/out/matrix_fire_bookshelf_table_repeat_3/summary.md`, and all
  32 fire/weather controls pass at
  `c/magma/trace/out/matrix_fire_bookshelf_table_family/summary.md`.
- A 37-line CPU assertion covers the shared host/device table at
  `c/magma/trace/out/test_world_tick_bookshelf_table_cpu.log`. CUDA execution
  remains deferred because GPU 1 is shared.
- The combined soul-sand/bookshelf native aggregate passes in 6:38.12 at
  335,628 KB peak RSS, zero major faults, zero swap, and exit zero at
  `c/magma/trace/out/test_runtime_soul_sand_bookshelf.log`. The clean scalar
  guard passes at 4,853 steps/s against the frozen 4,062 baseline and 3,858.9
  floor at `c/magma/trace/out/perf_guard_soul_sand_bookshelf_cpu_1.json`. GPU 1
  stayed untouched, so the promoted total remains 676 pending CUDA/Blaze
  evidence.

## 2026-08-03 (falling sand on enchanting table)

- Added the 3/4-height failed-placement boundary for metadata-zero enchanting
  tables. Sand follows Java through fallTime 10, clips to y=77.75 and drops on
  update 11, removes only the source, and leaves id 116 unchanged.
- The proof-fence negative is retained at
  `c/magma/trace/out/matrix_falling_enchanting_table_red_1/summary.md`. The
  first implemented candidate exposed a real second defect: the generic item
  integrator treated the partial table as a full cube and snapped the new item
  to y=78. The corrected partial-surface ascent matches Java's exact item Y,
  Y velocity, health, age, and pickup delay through update 12.
- The corrected candidate and all nine repeats pass at
  `c/magma/trace/out/matrix_enchanting_table_hay_candidate_3/summary.md` and
  `c/magma/trace/out/matrix_enchanting_table_hay_repeat_3/summary.md`. The
  parked oracle now agrees on 88 falling updates and eight exact sand/gravel
  drops at `c/magma/trace/out/test_falling_drop_enchanting_table.log`.

## 2026-08-03 (hay/carpet fire-table coverage)

- Added direct and volumetric hay fixtures for the already-correct Java 60/20
  encouragement/flammability row. `Random(36)` proves direct flammability;
  `Random(391)` reaches volume roll 3, distinguishing hay threshold 3 from
  bookshelf threshold 2, and ends at exact cursor `0xF572AB2A46D7`.
- Added explicit shared host/device assertions for both hay id 170 and carpet
  id 171. The 38-line CPU check passes at
  `c/magma/trace/out/test_world_tick_hay_carpet_table_cpu.log`; GPU execution
  remains deferred because GPU 1 is shared.
- All 44 affected falling/fire controls pass at
  `c/magma/trace/out/matrix_enchanting_table_hay_family/summary.md`: 40 strict
  state rows and four expected loaded-world shaped-drop diagnostics, with all
  behavior and block gates green. The full native aggregate passes in 6:48.67
  at 346,044 KB peak RSS, zero major faults, zero swap, and exit zero at
  `c/magma/trace/out/test_runtime_enchanting_table_hay.log`.
- After both exact oracle clients stopped, the scalar guard passed at 4,671
  steps/s against the frozen 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_enchanting_table_hay_cpu_1.json`. GPU 1 stayed
  untouched, so the promoted total remains 676 pending CUDA/Blaze evidence.

## 2026-08-03 (falling sand on farmland and cake)

- Added dry and fully moist farmland as the same nonreplaceable 15/16-height
  falling support. Both drop on update 10, retain metadata 0 or 7, and do not
  trample because the falling entity is not living. Added centered whole cake
  as a nonreplaceable 1/2-height support with an update-12 drop.
- The old runtime rejects both classes at
  `c/magma/trace/out/matrix_falling_cake_farmland_red_1/summary.md`. All three
  corrected rows pass at
  `c/magma/trace/out/matrix_falling_cake_farmland_candidate_1/summary.md`, all
  nine repeats pass at
  `c/magma/trace/out/matrix_falling_cake_farmland_repeat_3/summary.md`, and all
  ten shaped supports keep green behavior/block gates at
  `c/magma/trace/out/matrix_falling_cake_farmland_shaped_family/summary.md`.
- The generalized parked oracle covers both falling identities and every
  farmland/snow metadata value. Java and C agree on 486 updates across 44
  cases at `c/magma/trace/out/test_falling_drop_cake_farmland.log`. Review
  caught and fixed a delegated iteration-list omission of soul sand before
  accepting this evidence.
- The final native aggregate passes in 5:49.79 at 354,736 KB peak RSS, zero
  major faults, zero swap, and exit zero at
  `c/magma/trace/out/test_runtime_cake_farmland.log`. The scalar guard passes
  at 5,088 steps/s against the 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_cake_farmland_cpu_1.json`. GPU 1 was
  shared and untouched, so the promoted total remains 676 pending CUDA/Blaze
  evidence.

## 2026-08-03 (falling sand on supported carpet)

- Added the 1/16-height carpet failed-placement boundary using a valid fixture
  with stone below carpet 171:0. Sand remains active through update 12, clips
  to y=77.0625 and drops on update 13, then matches the exact item lifecycle
  through update 15.
- The proof-fence negative is retained at
  `c/magma/trace/out/matrix_falling_carpet_red_1/summary.md`. The corrected
  candidate and three repeats pass at
  `c/magma/trace/out/matrix_falling_carpet_candidate_1/summary.md` and
  `c/magma/trace/out/matrix_falling_carpet_repeat_3/summary.md`. All five
  shaped supports keep green behavior and block gates at
  `c/magma/trace/out/matrix_falling_shaped_support_family/summary.md`.
- The parked Java command now agrees with C on 114 falling updates and ten
  exact sand/gravel item drops at
  `c/magma/trace/out/test_falling_drop_carpet.log`. Runtime item collision now
  treats carpet as a partial surface, avoiding the same upward-item full-cube
  snap previously found on enchanting tables.
- Added explicit shared CPU/CUDA drift coverage for coal block's already-correct
  5/5 fire row. The 39-line CPU result passes at
  `c/magma/trace/out/test_world_tick_carpet_coal_cpu.log`; this was not a table
  correction.
- The full native aggregate passes in 7:26.14 at 348,624 KB peak RSS, zero
  major faults, zero swap, and exit zero at
  `c/magma/trace/out/test_runtime_carpet.log`. Ordinary and CPU-70 scalar
  guards were contaminated by an unrelated 64-thread self-play job plus
  backup/training work and are preserved as non-promotable failures at
  `c/magma/trace/out/perf_guard_falling_carpet_cpu_1.json` and
  `c/magma/trace/out/perf_guard_falling_carpet_cpu70_diagnostic_1.json`. GPU 1
  was shared and untouched, so the promoted total remains 676 pending a clean
  performance capture.

## 2026-08-03 (falling sand across snow-layer metadata)

- Corrected the earlier audit assumption using Java source: `BlockSnow`'s
  collision height is `(layers - 1) / 8`, not its visual `(layers / 8)`
  outline. Metadata zero therefore has no collision and is replaceable; sand
  reaches y=77, replaces the snow on update 13, creates no item, and drains a
  normal `+2` stability callback. Metadata seven collides at y=77.875 and
  takes the exact update-11 item-drop path.
- The proof-fence negative is at
  `c/magma/trace/out/matrix_falling_snow_red_1/summary.md`. Both endpoints pass
  at `c/magma/trace/out/matrix_falling_snow_candidate_1/summary.md`, all six
  repeats pass at `c/magma/trace/out/matrix_falling_snow_repeat_3/summary.md`,
  and all seven shaped supports keep green behavior/block gates at
  `c/magma/trace/out/matrix_falling_snow_shaped_family/summary.md`. The
  one-layer replacement is a strict 26-feature pass.
- Generalized the parked Java/C oracle across metadata 0..7 for both sand and
  gravel. It agrees on 302 updates and 26 replacement/drop cases, exact Math
  and EID cursors, and final support metadata at
  `c/magma/trace/out/test_falling_drop_snow_layers.log`.
- The final native aggregate passes in 5:46.00 at 350,056 KB peak RSS, zero
  major faults, zero swap, and exit zero at
  `c/magma/trace/out/test_runtime_snow_layers.log`. The clean scalar guard
  passes at 4,645 steps/s against the 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_snow_cpu_1.json`. GPU 1 was shared and
  untouched, so the promoted total remains 676 pending CUDA/Blaze evidence.

## 2026-08-03 (`doEntityDrops=false` falling placement)

- Added `doEntityDrops` to the authoritative Java snapshot, canonical trace
  schema, legacy-compatible capsule validation, tick-zero script restore, and
  C runtime state. Review kept this independent of `doFireTick` and scoped the
  implementation to the represented falling-block failed-placement caller.
- The deliberate old-C branch creates an item where Java creates none at
  `c/magma/trace/out/matrix_falling_entitydrops_false_red_1/summary.md`. The
  corrected bottom-slab row is strict with 26 matching simulated features,
  exact source-only raw mutation, unchanged support and `Math.random`, and one
  consumed entity ID at
  `c/magma/trace/out/matrix_falling_entitydrops_false_candidate_2/summary.md`.
  All three strict repeats pass at
  `c/magma/trace/out/matrix_falling_entitydrops_false_repeat_3/summary.md`.
- All 11 affected shaped-support cases retain green behavior and block gates
  at `c/magma/trace/out/matrix_falling_entitydrops_family/summary.md`. The
  parked real-Java oracle now agrees with C on 510 falling updates across 46
  sand/gravel cases, including both disabled-rule identities, at
  `c/magma/trace/out/test_falling_drop_entitydrops_false.log`.
- The full native aggregate passes in 6:35.42 at 355,964 KB peak RSS, zero
  major faults, zero swap, and exit zero at
  `c/magma/trace/out/test_runtime_entitydrops.log`. After stopping both oracle
  clients, the scalar guard passes at 4,487 steps/s against the 3,858.9 floor
  at `c/magma/trace/out/perf_guard_falling_entitydrops_cpu_1.json`. GPU 1 was
  shared and untouched, so the promoted total remains 676 pending CUDA/Blaze
  evidence. The next bounded falling seam is the out-of-world/600-tick timeout.

## 2026-08-03 (falling-block timeout and despawn)

- Added Java's post-move timeout order for active sand/gravel: increment
  `fallTime`, apply gravity, move, apply 0.98 drag, then retire outside Y 1..256
  after 100 updates or at any height after 600 updates. The item branch reuses
  the exact falling-drop constructor and respects `doEntityDrops`.
- The deliberate disabled-timeout build remains alive and fails at
  `c/magma/trace/out/test_falling_timeout_red.log`. The reviewed oracle covers
  high Y, low Y, and age timeout with drops both enabled and disabled. Java and
  C agree on all six cases at
  `c/magma/trace/out/test_falling_timeout.log`, including exact position,
  velocity, item lifecycle, unchanged blocks, Math cursor, and entity IDs.
  Review caught the initially omitted lower-world predicate and an error-path
  Java item leak before promotion.
- Refactoring the shared item constructor retains all 510 prior updates and 46
  shaped-support cases at
  `c/magma/trace/out/test_falling_drop_timeout_refactor.log`. The native suite
  additionally proves that full item-table pressure retires the expired block
  rather than retrying forever; inability to represent the item at that
  artificial cap remains documented as a resource divergence.
- The final native aggregate passes in 5:33.15 at 359,180 KB peak RSS, zero
  major faults, zero swap, and exit zero at
  `c/magma/trace/out/test_runtime_falling_timeout_2.log`. With all oracle
  clients stopped, the scalar guard passes at 5,031 steps/s against the
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_timeout_cpu_1.json`. The branch is
  active-entity-only and adds no idle scan or allocation. GPU 1 stayed shared
  and untouched, so the promoted total remains 676 pending CUDA/Blaze evidence.
  Lateral swept collision and dynamic landing-cell selection are next.

## 2026-08-03 (lateral falling motion and dynamic landing)

- Replaced the scalar landing shortcut for active falling entities with the
  retained absolute 0.98-by-0.98 Entity AABB and Java's Y-X-Z swept collision
  order. Position now follows `resetPositionToBB`, including the observable
  rounding at large absolute coordinates; flags, clipped velocities, float
  fall distance, gravity, and drag follow the same update order.
- Added free diagonal and X-wall parked real-Java fixtures. The free case lands
  at relative cell `(4,-3,2)` and the wall case at `(0,-3,0)` after 13 updates.
  Both final block volumes, source removal, cursor state, and all 26 motion rows
  pass at
  `c/magma/trace/out/test_falling_lateral_ordered_final.log` and
  `c/magma/trace/out/test_falling_lateral_ordered_repeat.log`.
- Seeded a same-time, same-priority stone callback before landing in both
  engines and now compare its rank against the sand stability callback. The
  Java fixture rejects pre-existing pending work in its X/Z footprint instead
  of removing and reinserting callbacks with new stable IDs.
- The first native aggregate exposed a stale pressure-plate expectation.
  Source review and the expanded real-game oracle establish the actual rule:
  all four plates have null collision but are nonreplaceable, wood/gold/iron
  activate to metadata one, and stone excludes `EntityFallingBlock`. Fixing
  live-item plate collision also removed a false one-block upward snap. The
  expanded regression passes 614 falling updates across 54 cases at
  `c/magma/trace/out/test_falling_drop_pressure_plate_final_3.log`; all six
  timeout cases remain exact at
  `c/magma/trace/out/test_falling_timeout_after_bbox.log`.
- The full native aggregate passes in 4:59.15 at 359,648 KB peak RSS, zero
  major faults, zero swap, and exit zero at
  `c/magma/trace/out/test_runtime_falling_lateral_2.log` and
  `c/magma/trace/out/test_runtime_falling_lateral_2.time`. With the Java client
  stopped, scalar throughput passes at 4,986 steps/s against the 4,062 baseline
  and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_lateral_cpu_1.json`. GPU 1 stayed
  shared and untouched, so the promoted total remains 676 pending CUDA/Blaze
  evidence. The claim remains bounded to free/X-wall static shapes; corner/Z,
  partial-height lateral obstacles, moving pistons, dragon egg, and anvils are
  next work.

## 2026-08-04 (scheduled dragon-egg falling)

- Added the real 1.11.2 dragon-egg trigger contract: placement and ordinary
  neighbor changes schedule one de-duplicated `+5` callback. Supported eggs
  drain without spawning; unsupported metadata-0 eggs use the existing
  retained-AABB falling entity and remove their source on update one.
- Successful landing now routes the egg through ordinary block placement,
  including neighbor notification and one fresh `+5` on-added callback. The
  parked Java/C oracle matches the supported negative and all 13 fall rows in
  three consecutive runs at
  `c/magma/trace/out/test_falling_dragon_egg_candidate_2.log` and
  `c/magma/trace/out/test_falling_dragon_egg_repeat_2.log`.
- Extended capsule proof admission to supported callbacks and air-below
  callbacks with a represented landing surface. Both forms pass self-test at
  `c/magma/trace/out/state_capsule_dragon_egg_final_2.log`. A separate native
  fixed-pool case fills all 16 falling slots and proves atomic source/cursor
  retention at `c/magma/trace/out/test_falling_dragon_egg_capacity.log`.
- The final native aggregate passes in 4:59.26 at 365,004 KB peak RSS with
  zero major faults and zero swap at
  `c/magma/trace/out/test_runtime_dragon_egg_final_2.log` and `.time`. With the
  Java client stopped, scalar throughput passes at 5,081 steps/s against the
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_dragon_egg_cpu_1.json`. GPU 1 remained
  untouched and the promoted total remains 676 pending CUDA/Blaze evidence.

## 2026-08-04 (scheduled anvil falling and impact RNG)

- Added the real scheduled anvil path for canonical metadata 0..11. Placement
  and support loss retain one `+2` callback, the source is removed on falling
  update one, and the centered stone-floor fixture lands on update 13 with a
  fresh supported `+2` callback.
- A proposed fence-top shortcut was rejected during review. Its 1.5-high
  collision keeps impact distance below one, but Java's post-collision
  below-cell probe still sees air, clears `onGround`, and never places the
  anvil. The accepted fixture therefore models the real impact branch instead
  of fitting an invalid no-RNG case.
- Falling anvils now retain a per-entity 48-bit `java.util.Random` cursor and
  the pre-impact float fall distance. The isolated fixture consumes exactly
  one `nextFloat`: a high roll preserves metas 0, 1, 4, and 8; internal seed
  zero advances meta 0 to 4 and makes tier-2 meta 8 break without placing or
  dropping an item. Living-entity damage and landing/break sound events remain
  separately scoped.
- The parked Java/C comparator passes all 78 updates and seven cases at
  `c/magma/trace/out/test_falling_anvil_after_restore_fence.log`, after three
  complete identical repeats at
  `c/magma/trace/out/test_falling_anvil_repeat_3.log`. The previous dragon-egg,
  lateral, timeout, and 614-update shaped/drop families all remain green in
  their `*_after_anvil.log` results.
- Capsule restore accepts only supported canonical anvil callbacks. A falling
  callback is intentionally filtered because a world save does not contain
  the clock-seeded `Entity.rand` that the future entity will receive. The
  positive/negative capsule self-test passes at
  `c/magma/trace/out/state_capsule_anvil_1.log`.
- The CPU game build and full native aggregate pass. The aggregate runs in
  5:24.87 at 253,084 KB peak RSS with zero major faults and zero swap at
  `c/magma/trace/out/test_runtime_anvil_2.log` and `.time`. Scalar throughput
  passes at 4,475 steps/s against the 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_cpu_1.json`. GPU 1 remained
  shared and untouched, so the promoted total remains 676 pending CUDA/Blaze
  evidence.

## 2026-08-04 (anvil failed-placement item metadata)

- Added the failed-placement branch to the scheduled-anvil oracle. A bottom
  stone slab retains the occupied landing cell, so the entity reaches Y 216.5
  on update 14, measures a 3.36419439 pre-impact distance, consumes one
  controlled `Entity.rand.nextFloat`, and drops instead of placing.
- Corrected `runtime_falling_spawn_item` to use
  `BlockAnvil.damageDropped`: horizontal orientation is discarded and only
  damage tier remains. Input metas 0, 1, 4, and 8 now emit item metas 0, 0, 1,
  and 2. Other falling-block item metadata is unchanged.
- The expanded parked Java/C comparator pins all 134 trajectory rows across 11
  cases, plus item constructor/first-tick kinematics, stack, age, pickup delay,
  entity ID, one entity RNG draw, and eight `Math.random` LCG steps. It passes
  twice at `c/magma/trace/out/test_falling_anvil_drop_1.log` and
  `c/magma/trace/out/test_falling_anvil_drop_repeat.log`.
- Native regressions cover all four metadata mappings. The full aggregate
  passes in 6:07.55 with a 370,148 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_anvil_drop.log` and `.time`. With the Java
  client stopped, scalar throughput passes at 5,183 steps/s against the
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_drop_cpu_1.json`. GPU 1 remained
  untouched, so the promoted total remains 676 pending CUDA/Blaze evidence.

## 2026-08-04 (bounded BlockFalling instant path)

- Added explicit `BlockFalling.fallInstantly` runtime state for the synchronous
  worldgen callback. A due falling block removes its source, scans down through
  the admitted column, restores its full ID/metadata above the first blocker,
  and schedules the landed block without creating an entity or consuming any
  entity/RNG cursor.
- The scheduled-anvil oracle now covers metas 0, 1, 4, and 8 in both ordinary
  and instant modes. The expanded 15-case suite passes consecutively at
  `c/magma/trace/out/test_falling_instant_final_1.log` and
  `c/magma/trace/out/test_falling_instant_final_2.log`. A focused rerun also
  proves the public unsupported-anvil callback restore at
  `c/magma/trace/out/test_falling_instant_public_restore_repeat.log`; native
  coverage runs metadata-0 sand through the same generic branch.
- A mixed water/lava/fire oracle extension was deliberately left out of this
  bounded promotion because staging those materials introduces their own
  scheduled-fluid callbacks. Dynamic fall-through columns and the
  area-not-loaded predicate remain separate causal slices.
- The production CPU game builds, and the full native aggregate passes in
  5:08.99 with a 370,040 KB peak and zero swap at
  `c/magma/trace/out/test_runtime_falling_instant.log` and `.time`. Scalar
  throughput passes at 5,103 steps/s against the 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_instant_cpu_1.json`. The default false
  mode adds no idle work. GPU 1 remained untouched, so the promoted total
  remains 676 pending CUDA/Blaze evidence.

## 2026-08-04 (bounded falling-anvil player impact)

- Added the first controlled living target to scheduled anvil falling: a
  fresh, unarmored, effect-free player centered on the stone-floor landing
  cell. Update 13 derives impact two from the 2.902239 pre-impact distance and
  applies exact raw damage four, health 20 to 16, hurt resistance 20, hurt
  time 10, last damage four, and 0.1 food exhaustion.
- The first oracle attempt was correctly red: moving the real server player
  with `setPosition` changed its AABB but not its vertical chunk-list index, so
  Java's landing query could not enumerate it. The fixture now performs the
  real non-ticking world entity update and restores position, motion, health,
  timers, private last-damage/respawn/exhaustion fields, and interpolation
  state in `finally`.
- The real-game trace exposed an additional causal cursor: accepted ANVIL
  damage has no attacking entity, so `EntityLivingBase.attackEntityFrom`
  chooses `attackedAtYaw` with one `Math.random()` `nextDouble`. Magma now
  consumes those two LCG steps before the anvil's independent
  `Entity.rand.nextFloat`; the no-target controls prove the cursor is otherwise
  unchanged.
- The expanded Java/C comparator passes 147 falling updates and 16 cases in
  two consecutive runs at
  `c/magma/trace/out/test_falling_anvil_impact_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_impact_final_2.log`. Java builds, the
  CPU product links, and the full native aggregate passes in 5:02.51 with a
  372,576 KB peak, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_anvil_impact.log` and `.time`.
- With the Java client stopped, scalar throughput passes at 4,998 steps/s
  against the frozen 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_impact_cpu_1.json`. The target
  check is impact-only behind the existing active falling-block branch and
  adds no idle scan or allocation. GPU 1 remained untouched, so the promoted
  total remains 676 pending CUDA/Blaze evidence. Hurt-immunity variants are
  the next bounded anvil slice.

## 2026-08-04 (falling-anvil hurt-immunity boundary)

- Extended the controlled landing fixture with two player states injected
  immediately before update 13. Prior raw damage four rejects the equal
  raw-four anvil hit completely; prior raw damage two accepts only the
  two-point difference. The exact results are health 20/exhaustion 0 and
  health 18/exhaustion 0.1 respectively, with hurt resistance 20, hurt time
  10, and last damage four in both cases.
- Corrected the product's hurt-direction RNG condition. A fresh accepted hit
  consumes Java's source-less `attackedAtYaw` `Math.random()` double; a
  stronger delta inside the existing immunity window sets yaw zero and skips
  fresh status, direction, sound, and global RNG work. An equal rejected hit
  returns before all of them. The falling anvil still consumes its one
  degradation `nextFloat` after either target result.
- The integrated client can perform ambient entity-ID work after a real-player
  damage response. The comparator gives those cases a quiescence boundary
  before installing the next controlled global cursor; each fixture still
  checks the exact constructor-local ID transition.
- The 18-case Java/C suite passes 173 falling updates in two consecutive runs
  at `c/magma/trace/out/test_falling_anvil_immunity_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_immunity_final_2.log`. The full native
  aggregate passes in 5:22.12 at 378,148 KB peak RSS, zero major faults, and
  zero swap at `c/magma/trace/out/test_runtime_anvil_immunity.log` and `.time`.
- The CPU product links and the stopped-oracle scalar guard passes at 5,089
  steps/s against the 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_immunity_cpu_1.json`. The branch
  remains active-impact-only, GPU 1 was untouched, and the promoted total stays
  676 pending CUDA/Blaze evidence.

## 2026-08-04 (falling-anvil fully absorbed fresh hit)

- Added the accepted zero-residual damage boundary. A fresh unarmored player
  starts with four absorption points; the raw-four anvil hit consumes them to
  zero, leaves health 20 and exhaustion zero, but still opens the 20/10 hurt
  window and records last damage four.
- Corrected fresh-hurt RNG branching for that result. Java still selects the
  source-less hurt direction and consumes one `Math.random()` `nextDouble`
  even though absorption leaves no health damage. Magma now advances those two
  LCG steps for any accepted fresh raw hit, while immunity rejection and
  stronger-delta cases remain unchanged.
- Isolated controlled cursor evidence from the integrated client's unrelated
  constructors. The Java fixture snapshots `Math.random` immediately at the
  terminal impact boundary and derives its controlled Entity-ID cursor from
  the exact falling/item IDs; later raw client-side cursors remain diagnostic.
- The 19-case Java/C suite passes 186 updates in consecutive runs at
  `c/magma/trace/out/test_falling_anvil_absorption_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_absorption_final_2.log`. Java builds,
  the CPU product links, and the full native aggregate passes in 5:16.55 at
  375,032 KB peak RSS with zero major faults and zero swap at
  `c/magma/trace/out/test_runtime_anvil_absorption.log` and `.time`.
- With the Java oracle stopped, scalar throughput passes at 4,948 steps/s
  against the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_absorption_cpu_1.json`. The path
  remains impact-only, GPU 1 was untouched, and the promoted total remains 676
  pending CUDA/Blaze evidence.

## 2026-08-04 (falling-anvil Resistance-I boundary)

- Added a fresh unarmored player carrying only Resistance I. Vanilla applies
  its amplifier-zero 20-percent reduction after armor, producing 3.2 health
  damage and health 16.8 from raw anvil damage four. Exhaustion is 0.1, while
  hurt resistance/time are 20/10 and raw `lastDamage` remains four.
- The existing product potion-damage kernel was already exact, including
  float operation order. The new work is oracle and native coverage, with the
  same two `Math.random` LCG steps and one falling-entity `nextFloat` pinned.
- Hardened the failed-placement control against the integrated JVM's shared
  client/server Entity-ID allocator. The fixture identifies the newly loaded
  server item by pre/post membership, preserves the raw observed ID for
  diagnostics, and compares the exact controlled server allocation order.
- The 20-case Java/C suite passes 199 updates in consecutive runs at
  `c/magma/trace/out/test_falling_anvil_resistance_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_resistance_final_2.log`. Java and the
  CPU product build, and the full native aggregate passes in 5:04.39 at
  380,944 KB peak RSS with zero major faults and zero swap at
  `c/magma/trace/out/test_runtime_anvil_resistance.log` and `.time`.
- With the oracle stopped, scalar throughput passes at 5,072 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_resistance_cpu_1.json`. GPU 1 was
  untouched and the promoted total remains 676 pending CUDA/Blaze evidence.

## 2026-08-04 (controlled-pig falling-anvil impact)

- Added the first bounded non-player target: one controlled NoAI pig centered
  in the retained landing AABB. The raw-four ANVIL hit moves health 10 to 6
  and records raw last damage four.
- Reproduced the three independent random streams in causal order. The pig
  consumes a `nextDouble` in `setBeenAttacked` and two `nextFloat` calls for
  hurt-sound pitch, global `Math.random` consumes the source-less direction
  `nextDouble`, and the falling entity later consumes its degradation
  `nextFloat`. IDs, world RNG, trajectory, landing, and scheduling are pinned.
- Made the observation boundary explicit instead of hiding a timer mismatch.
  Java is sampled immediately after impact at 20/10 hurt timers; magma's public
  runtime has then ticked the controlled mob once and stores 19/9. The
  comparator requires exactly this one-tick offset and exact equality for the
  remaining state.
- The 21-case Java/C suite passes 212 updates in consecutive runs at
  `c/magma/trace/out/test_falling_anvil_pig_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_pig_final_2.log`. Java and the CPU
  product build, and the full native aggregate passes in 5:21.64 at 385,988 KB
  peak RSS with zero major faults at
  `c/magma/trace/out/test_runtime_anvil_pig.log` and `.time`.
- With the oracle stopped, scalar throughput passes at 5,144 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_pig_cpu_1.json`. The target scan
  is active-impact-only, GPU 1 was untouched, and the promoted total remains
  676 pending CUDA/Blaze evidence.

## 2026-08-04 (exact anvil impact phase and ordered pig pair)

- Removed the single-pig comparator's observation-boundary waiver. A cold
  fixture hook invokes the product's existing falling-entity phase directly,
  so Java and magma now compare the same immediate impact state at 20/10 hurt
  timers. The ordinary product tick still advances controlled living entities
  afterward and its native regression requires the correct 19/9 saved state.
- Added two freshly inserted NoAI pigs in one chunk section. Java's falling
  target query exposes insertion order A then B; two fresh ascending magma
  slots reproduce that bounded order. Distinct target seeds prevent an
  accidental swap from passing. This does not claim arbitrary slot reuse or
  natural-world ordering.
- Both targets move health 10 to 6 and retain raw last damage four. Each target
  RNG advances four LCG steps, two constructors plus two source-less impacts
  advance global Math 16 steps, falling RNG advances once, and world RNG is
  unchanged. Logical IDs, trajectory, landing, and scheduling are exact.
- The 22-case Java/C suite passes 225 updates exactly in consecutive runs at
  `c/magma/trace/out/test_falling_anvil_pigs_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_pigs_final_2.log`. Java and the CPU
  product build. The native aggregate passes in 5:22.90 at 385,692 KB peak RSS
  with zero major faults at `c/magma/trace/out/test_runtime_anvil_pigs.log`
  and `.time`.
- With the oracle stopped, scalar throughput passes at 5,069 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_pigs_cpu_1.json`. GPU 1 remained
  untouched and the promoted total remains 676 pending CUDA/Blaze evidence.

## 2026-08-04 (controlled-cow falling-anvil impact)

- Added cow as the second explicitly supported controlled NoAI anvil target.
  The product predicate remains narrow to pig/cow; sheep, chicken, hostiles,
  natural AI, armor, and effects are unchanged.
- Cow inherits the same fresh EntityAnimal damage sequence. Health moves 10 to
  6, immediate hurt timers are 20/10, and raw last damage is four. The pinned
  cow RNG advances four LCG steps, its constructor plus source-less impact
  advance global Math eight, falling RNG advances once, and world RNG stays
  unchanged. Query membership and order, IDs, trajectory, landing, and schedule
  are exact.
- The 23-case Java/C suite passes 238 updates exactly in consecutive runs at
  `c/magma/trace/out/test_falling_anvil_cow_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_cow_final_2.log`. Java and the CPU
  product build. The native aggregate includes an ordered pig/pig/cow fixture
  and passes in 5:18.17 at 385,956 KB peak RSS with zero major faults at
  `c/magma/trace/out/test_runtime_anvil_cow.log` and `.time`.
- With the oracle stopped, scalar throughput passes at 5,052 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_cow_cpu_1.json`. GPU 1 remained
  untouched and the promoted total remains 676 pending CUDA/Blaze evidence.

## 2026-08-04 (plain diamond chestplate anvil impact)

- Added the first bounded armored-player target in a survival-valid oracle
  state: one plain undamaged diamond chestplate in slot 38. Raw damage four
  through armor eight and toughness two leaves exact float health
  17.02400016784668 (`0x41883127`) and advances chest durability zero to one.
  Hurt timers are 20/10, raw last damage remains four, and exhaustion is 0.1.
- Strengthened all represented player-impact cases with the exact player
  `Entity.rand` cursor. Fresh ordinary, absorbed, Resistance-I, and armored
  hits advance four LCG steps for `setBeenAttacked` and hurt-sound pitch;
  equal rejection and stronger immunity-window delta leave it unchanged.
  Global Math and falling-entity cursors remain independently checked.
- The first oracle run exposed that the launcher's player was creative even
  after damage immunity was disabled, which suppressed armor durability in
  `ItemStack.damageItem`. The fixture now saves, forces, and restores survival
  capability state, preventing an internally inconsistent creative-damage
  reference from becoming a golden.
- The 24-case Java/C suite passes 251 falling updates exactly in consecutive
  runs at `c/magma/trace/out/test_falling_anvil_armor_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_armor_final_2.log`. Java and the CPU
  product build. The full native aggregate passes in 5:02.36 at 387,440 KB
  peak RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_anvil_armor.log` and `.time`.
- With the oracle stopped, scalar throughput passes at 5,143 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_armor_cpu_1.json`. The work is
  impact-only, GPU 1 remained untouched, and the promoted total remains 676
  pending CUDA/Blaze evidence. Enchantments, multiple pieces, creative
  durability, and the anvil helmet pre-hook remain open.

## 2026-08-04 (anvil diamond-helmet pre-hook)

- Added the ANVIL/FALLING_BLOCK head-slot operation that Java runs before hurt
  immunity and ordinary armor. For raw four it consumes one player
  `Entity.rand.nextFloat`, damages the head stack by
  `int(16 + nextFloat * 8)`, then scales the attack amount to three.
- With the pinned player seed, the special damage is 19 and ordinary armor
  adds one, leaving the diamond helmet in slot 39 at durability 20. Diamond
  helmet armor three/toughness two leaves exact float health
  17.215999603271484 (`0x4189ba5e`); `lastDamage` correctly records the scaled
  raw value three, timers are 20/10, and exhaustion is 0.1.
- The player cursor advances five LCG steps in exact causal order: head-hook
  `nextFloat`, `setBeenAttacked` `nextDouble`, and two hurt-pitch
  `nextFloat` calls. Global Math advances two and falling RNG advances one.
  Existing chest, immunity, absorption, Resistance-I, passive-target, drop,
  landing, schedule, ID, and world controls remain exact in the same gate.
- The 25-case Java/C suite passes 264 updates exactly in consecutive runs at
  `c/magma/trace/out/test_falling_anvil_helmet_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_helmet_final_2.log`. Java and the CPU
  product build. The full native aggregate passes in 4:59.85 at 393,744 KB
  peak RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_anvil_helmet.log` and `.time`.
- With the oracle stopped, scalar throughput passes at 5,151 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_helmet_cpu_1.json`. The hook is
  active-impact-only, GPU 1 remained untouched, and the promoted total stays
  676 pending CUDA/Blaze evidence. Unbreaking, near-break/removal, and
  item-break effects remain open.

## 2026-08-04 (controlled-sheep falling-anvil impact)

- Added sheep as the third bounded controlled NoAI passive target. Corrected
  its represented max health from the generic passive value ten to vanilla
  eight and split its 0.9 by 1.3 AABB from the cow's 0.9 by 1.4 box. The
  Java/C fixture now exports and compares the actual target AABB.
- A raw-four ANVIL hit leaves sheep health four, immediate hurt timers 20/10,
  and raw last damage four. The pinned sheep RNG advances four LCG steps,
  constructor plus source-less hurt direction advance global Math eight,
  falling RNG advances once, and world RNG remains unchanged.
- The 26-case Java/C suite passes 277 updates exactly in consecutive runs at
  `c/magma/trace/out/test_falling_anvil_sheep_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_sheep_final_2.log`. Java and the CPU
  product build. The full native aggregate extends the ordered public-tick
  fixture to pig/pig/cow/sheep and passes in 5:03.68 at 252,404 KB peak RSS,
  zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_anvil_sheep.log`.
- With the oracle stopped, scalar throughput passes at 4,916 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_sheep_cpu_1.json`. The impact
  scan remains active-only, GPU 1 was untouched, and the promoted total stays
  676 pending CUDA/Blaze evidence. Chicken is deferred to a separate
  death/drop slice.

## 2026-08-04 (controlled-chicken no-loot anvil death)

- Added chicken as the first lethal controlled passive impact. Vanilla max
  health four and its 0.4 by 0.7 AABB are exact. The oracle saves, disables,
  and restores `doMobLoot` because `EntityLivingBase.onDeath` runs mob loot at
  the immediate damage boundary rather than at death tick 20.
- Immediately after the landing update, chicken health is zero, the protected
  living `dead` flag is true, public `Entity.isDead` is false, `deathTime` is
  zero, hurt timers are 20/10, and raw last damage is four. No item or XP
  entity is allocated. Target RNG advances four LCG steps, constructor plus
  source-less direction advance global Math eight, falling RNG advances one,
  world RNG is unchanged, and the ID cursor advances only for falling block
  plus chicken.
- The first full native run exposed that the ordinary mobs-enabled loop aged
  hurt timers but omitted the same-tick controlled death update. The shared
  loop now advances zero-health controlled death before AI/travel. The direct
  oracle remains at `deathTime=0`; falling-first/chicken-second public order
  ends the same tick at `deathTime=1`.
- The current-source 27-case Java/C suite passes all 290 updates in consecutive
  runs at `c/magma/trace/out/test_falling_anvil_chicken_final_3.log` and
  `c/magma/trace/out/test_falling_anvil_chicken_final_4.log`. Java and the CPU
  product build. The full native aggregate passes in 5:05.39 at 252,668 KB
  peak RSS with zero major faults and zero swap at
  `c/magma/trace/out/test_runtime_anvil_chicken_final.log`.
- With the oracle stopped, scalar throughput passes at 5,157 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_chicken_cpu_1.json`. GPU 1 was
  untouched and the promoted total remains 676 pending CUDA/Blaze evidence.
  Loot-enabled RNG/drops, audio and status events, particles, and terminal
  removal remain open.

## 2026-08-04 (controlled-chicken exact anvil loot)

- Enabled the real 1.11.2 chicken loot boundary for the controlled lethal
  impact. The pinned target seed executes feather count zero through two and
  raw-chicken count one pools, producing ordered stacks item 288 count two and
  item 365 count one. Immediate health/death/timer state remains exact and no
  XP entity is produced for the null-attacker fixture.
- Preserved both server `EntityItem` constructors exactly: logical IDs,
  position, motion, yaw, hover phase, age zero, pickup delay ten, health five,
  lifespan 6000, and ground/dead flags all match. Their next public tick also
  matches. Target RNG advances seven LCG steps, global Math advances 24,
  falling RNG advances once, world RNG remains unchanged, and the logical ID
  cursor advances four.
- Removed a test-harness race exposed by fresh JVMs. An integrated-client
  mirror chicken consumes six draws from process-global `Math.random`, so the
  controlled oracle chicken is now inserted server-only while `LivingDrops`
  captures and manually spawns the real server-constructed items. This is a
  fixture isolation seam, not a product approximation.
- Added an atomic native capacity negative control. If the fixed item pool has
  only one free slot for this two-stack roll, health, death flags, timers,
  target/global RNG, IDs, and items all remain unchanged. This bounded-storage
  policy is intentionally stronger than Java's allocation behavior.
- Two fresh-JVM Java/C runs pass all 303 updates and 28 cases at
  `c/magma/trace/out/test_falling_anvil_chicken_loot_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_chicken_loot_final_2.log`. Both product
  builds pass. The full native aggregate passes in 4:25.85 at 253,480 KB peak
  RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_anvil_chicken_loot_final.log`.
- The stopped-oracle scalar guard passes at 5,180 steps/s against the 4,062
  baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_chicken_loot_cpu_1.json`. GPU 1
  remained untouched and the promoted total remains 676 pending CUDA/Blaze
  evidence. Feather-count zero/one rolls, cooked chicken, looting/killer XP,
  broader mobs, emitted events/particles, terminal removal, and item
  yaw/hover capsule persistence remain open.

## 2026-08-04 (all chicken feather counts and cooked loot)

- Added deterministic target cursors for feather counts zero and one alongside
  the existing count-two case. The target always consumes seven LCG steps.
  Count zero creates only meat, so total global Math consumption is 16 steps
  and the logical ID cursor advances three. Counts one/two create feather and
  meat entities, retaining 24 Math steps and four IDs.
- Reused the represented mob fire counter for the loot table's
  `EntityOnFire` furnace-smelt condition. A fresh Java entity exposes exact
  inactive fire `-1`; the burning fixture pins 100. Burning changes raw
  chicken item 365 to cooked item 366 and consumes no RNG. The comparator
  requires both the exact counter and `isBurning`, preventing a mode-specific
  cooked shortcut from passing.
- Fixed a harness ambiguity exposed by the zero-feather case. The C serializer
  now emits the failed-placement `ticked_item` only for the falling-block drop
  mode; a single mob-loot entity remains solely in the ordered `mob_drops`
  stream.
- Added direct native coverage for all three feather cardinalities, cooked/raw
  meat, stack order, dynamic capacity, target/global RNG, and logical IDs.
  Existing two-stack capacity rejection remains atomic.
- Two fresh-JVM Java/C gates pass all 342 updates and 31 cases at
  `c/magma/trace/out/test_falling_anvil_chicken_cooked_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_chicken_cooked_final_2.log`. Java and
  the product build. The full native aggregate passes in 5:02.42 at 253,492 KB
  peak RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_anvil_chicken_cooked_final.log`.
- The stopped-oracle scalar guard passes at 5,088 steps/s against the 4,062
  baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_chicken_cooked_cpu_1.json`. GPU 1
  remained untouched and the promoted total remains 676 pending CUDA/Blaze
  evidence. Killer/looting/XP, emitted events/particles, terminal removal,
  controlled fire progression, broader mobs, and capsule persistence remain
  open.

## 2026-08-04 (controlled-chicken terminal death lifecycle)

- Continued the cooked `doMobLoot=true` chicken through its complete public
  death lifecycle while the Java server remains parked. The fixture updates
  only the chicken and its two already-spawned loot entities in loaded-entity
  order, avoiding unrelated world-tick RNG and entity-ID work.
- Ticks one through 19 retain `EntityLivingBase.dead=true` and
  `Entity.isDead=false`, advance death time one through 19, age hurt timers,
  decrement fire 100 through 81, and age both items with pickup delay ten to
  zero. Tick 20 reaches death time 20, fire 80, sets `Entity.isDead`, removes
  the chicken from the loaded set, and retains zero XP for the null-attacker
  fixture. Both loot entities remain loaded at age 20.
- Matched the terminal particle RNG side effect exactly. Each of 20 particles
  reads three target-local Gaussians followed by three floats. Gaussian cache
  reuse and rejection sampling consume 200 LCG steps for the pinned seed; with
  the seven impact/loot steps, the terminal cursor is
  `0x87abf0c5165a` after 207 total steps and the Gaussian cache is empty.
  Particle payload emission remains open event/pixel work.
- Hardened the native aggregate fixture by disabling ambient normal-mob logic
  during this controlled falling-only case. The old fixture silently reduced
  fire 100 to 89 during the staged fall, while the real parked Java target was
  not publicly ticked. This was fixture contamination, not a product change.
- Three fresh-JVM Java/C gates pass 362 exact update rows and 31 cases at
  `c/magma/trace/out/test_falling_anvil_chicken_post_final_1.log` through
  `c/magma/trace/out/test_falling_anvil_chicken_post_final_3.log`. Java and the
  CPU product build. The final native aggregate passes in 5:43.15 at 401,080
  KB peak RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_anvil_chicken_post_final.log`.
- With the oracle stopped, scalar throughput passes at 5,059 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_chicken_post_cpu_1.json`. The
  lifecycle branch runs only for a zero-health controlled target, GPU 1 was
  untouched, and the promoted total remains 676 pending CUDA/Blaze evidence.

## 2026-08-04 (controlled-chicken impact status and sound events)

- Added synchronous oracle observation at the true server boundaries:
  `WorldServer.setEntityState` for status and
  `ServerWorldEventHandler.playSoundToAllNearExcept` for the final sound packet
  payload after Forge substitution. The hooks are armed only for the parked
  target identity and world, and cleanup disarms them before fixture teardown.
- Every lethal chicken fixture now requires the exact ordered stream status 2,
  `minecraft:entity.chicken.death` in category `neutral`, then status 3 after
  synchronous loot. Sound position, volume one, and float pitch are exact. The
  target RNG still advances four steps without loot and seven with loot.
- Added an allocation-free 285-record product event ring with monotonic
  sequence numbers and an explicit overwrite counter. Native regressions cover
  the lethal stream, all loot cardinalities, capacity-rejection atomicity,
  fresh nonlethal hurt sound, nonfresh lethal delta with status 3 only, invalid
  reads, exact fill, and one-record wrap.
- Added `--case` to the falling-anvil comparator so one fixture can be rerun in
  about nine seconds while diagnosing a full-matrix failure. It isolated the
  only observed event-format discrepancy: Java's registry string includes the
  `minecraft:` namespace. The corrected focused plain and cooked lifecycle
  cases both pass.
- Two uncontaminated fresh-JVM full gates pass 362 exact update rows and 31
  cases at
  `c/magma/trace/out/test_falling_anvil_chicken_events_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_chicken_events_final_3.log`. A separate
  attempt was rejected when the existing integrated-client global-Math race
  appeared in an unrelated sheep fixture.
- Java and the CPU product build. The final native aggregate passes in 5:02.85
  at 253,536 KB peak RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_anvil_chicken_events_final.log`. The
  stopped-oracle scalar guard passes at 5,169 steps/s against the 4,062 baseline
  and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_chicken_events_cpu_1.json`.
  Producers do no idle scan or allocation. GPU 1 remained untouched, and the
  promoted total remains 676 pending CUDA/Blaze evidence.

## 2026-08-04 (controlled-passive ordered impact events)

- Generalized the observed impact event stream from chicken to controlled pig,
  cow, and sheep. A fresh nonlethal target emits status 2 and its exact
  namespaced hurt sound. Cow overrides source volume to 0.4; pig and sheep use
  one. All use category `neutral`, exact target position, and the same two-float
  adult pitch formula after `setBeenAttacked`.
- Replaced single-target and position-based oracle attribution with an identity
  map plus a thread-local `Entity.playSound` source stack. The nested
  `ServerWorldEventHandler` hook still captures the final post-Forge packet
  payload. Two pigs at identical coordinates therefore retain exact insertion
  order and distinct logical EIDs:
  `status2/pig1 sound/status2/pig2 sound`.
- Generalized the C ring producer and serializer to all four controlled
  passives. Native tests require the complete 11-record
  pig/pig/cow/sheep/chicken stream, exact sound enums, volumes, positions, and
  bitwise pitches. An equal-damage hurt-resistance rejection adds no event and
  advances neither target nor global RNG.
- Focused Java/C gates pass independently for pig, two pigs, cow, and sheep.
  Two fresh-JVM full gates pass all 362 rows and 31 cases at
  `c/magma/trace/out/test_falling_anvil_passive_events_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_passive_events_final_2.log`.
- Java and the CPU product build. The full native aggregate passes in 5:03.90
  at 252,700 KB peak RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_anvil_passive_events_final.log`. The
  stopped-oracle scalar guard passes at 5,037 steps/s against the 4,062 baseline
  and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_passive_events_cpu_1.json`.
  Producers append only after an accepted controlled hit. GPU 1 remained
  untouched and the promoted total remains 676 pending CUDA/Blaze evidence.

## 2026-08-04 (falling-anvil landing and break world events)

- Added exact synchronous observation of `World.playEvent` at the parked Java
  falling-anvil boundary. A scoped `IWorldEventListener` records sequence,
  event ID, integer block position, and data without routing through client
  audio or packet timing.
- Successful placement emits 1031 only after the anvil block is installed.
  Terminal degradation of damage-tier-two meta emits 1029 at the same landing
  cell with no replacement block. Both carry data zero. Supported, instant,
  and failed-placement/drop fixtures emit no event.
- Added a separate fixed 16-record runtime world-event ring with monotonic
  sequence and overwrite counters. Its capacity is bounded by the existing
  falling-entity pool, and the producer runs only for an active terminal
  anvil, with no idle scan or allocation. Native regressions assert both exact
  positive payloads, the supported negative, and invalid reader bounds.
- The focused six-fall matrix passes 78 exact updates at
  `c/magma/trace/out/test_falling_anvil_world_events_focus.log`; supported,
  drop, and instant negative controls pass at
  `c/magma/trace/out/test_falling_anvil_world_events_negatives.log`.
- Two fresh-JVM full gates pass all 362 rows and 31 cases at
  `c/magma/trace/out/test_falling_anvil_world_events_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_world_events_final_2.log`. The full
  native family passes in 6:09.52 at 405,236 KB peak RSS with zero major
  faults and zero swap at
  `c/magma/trace/out/test_runtime_anvil_world_events_final.log`.
- Java and the CPU product build. The stopped-oracle scalar guard passes at
  5,004 steps/s against the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_world_events_cpu_1.json`. GPU 1
  remained untouched and the promoted total remains 676 pending CUDA/Blaze
  evidence.
- Hardened the oracle instance launcher to exclude ForgeGradle's legacy
  `getVersionJson` task as well as assets in offline mode. This removes a
  network-hung startup path and restored fresh instance readiness to about ten
  seconds.

## 2026-08-04 (controlled-chicken terminal particle payloads)

- Added scoped real-Java observation of the synchronous
  `IWorldEventListener.spawnParticle` callbacks at chicken death tick 20.
  Ticks one through 19 emit nothing; tick 20 emits exactly 20 ID-zero
  `EXPLOSION_NORMAL` particles with `ignoreRange=true`, no integer parameters,
  and six bit-serialized doubles per particle.
- Matched vanilla's exact draw and numeric order: three target-local
  `nextGaussian` velocities followed by three `nextFloat` position offsets,
  with chicken width 0.4 and height 0.7 retained as floats until each offset is
  widened to double. The final target cursor remains the already-pinned
  207-step state `0x87abf0c5165a` with an empty Gaussian cache.
- The first focused comparison isolated four velocity components one to three
  ULP from Java. The cause was host libm `log` versus Java 8
  `StrictMath.log`; the shared RNG now uses fdlibm evaluation order on host and
  device. The focused payload is bit-exact and the independent
  `entity_random` Java/CPU oracle remains exact for all 17 outputs.
- Added a fixed 95-batch product ring, one atomic 20-particle batch per
  represented living slot, with monotonic sequence and overwrite counters.
  Native coverage fills all 95 slots in one tick and proves one-batch
  oldest-first wrap. The producer is terminal-entity-only, with no idle scan,
  heap allocation, or unbounded storage.
- Two fresh-JVM full gates pass all 362 rows and 31 cases at
  `c/magma/trace/out/test_falling_anvil_terminal_particles_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_terminal_particles_final_2.log`, both
  in 1:26 with about 49 MB harness RSS and zero swap. Java and the product
  build.
- The complete native family passes in 6:28.65 at 433,092 KB peak RSS with
  zero swap at
  `c/magma/trace/out/test_runtime_terminal_particles.log`. The stopped-oracle
  scalar guard passes at 5,115 steps/s against the 4,062 baseline and 3,858.9
  floor at
  `c/magma/trace/out/perf_guard_falling_anvil_terminal_particles_cpu_1.json`.
  An `sm_120 --fmad=false` CUDA compile-only check passes; GPU 1 was not
  executed while shared, so the promoted total remains 676 pending CUDA/Blaze
  evidence. Particle rendering/consumption and broader living types remain
  open.

## 2026-08-04 (controlled-chicken player-credit terminal XP)

- Added the first exact player-credit XP branch to the controlled chicken
  lifecycle. `recentlyHit=20` enters death tick 20 at one, so the adult animal
  consumes one `World.rand.nextInt(3)`, returns XP three for the pinned cursor,
  and creates one unsplit orb before `setDead` and terminal particles.
  `recentlyHit=19` expires first and creates no orb.
- Matched the exact `EntityXPOrb` constructor and its same-world-tick update:
  four `Math.random` doubles drive yaw and XYZ motion; gravity, move, and drag
  then produce age/color one. The Java/C comparator requires raw yaw and all
  six position/motion bit words, value, private health, pickup delay,
  target-color cursor, dimension, logical EID, World.rand, Math.random, and
  next-entity-ID state.
- Added per-living-slot recent-hit/attacking-player state and a bounded
  95-orb terminal allocator. It preflights capacity before RNG or ID mutation.
  Native regressions cover the positive boundary, the 19-tick expired
  negative, and full-pool atomic rejection.
- Hardened the oracle instead of accepting intermittent retries. Controlled
  pig, cow, sheep, and chicken fixtures now join real server chunk/entity
  lists without tracker mirrors, preventing integrated-client constructors
  from racing the server-only Math cursor. The XP fixture runs outside the
  160-block tracker radius with its falling area explicitly loaded. The
  offline instance launcher also skips ForgeGradle's legacy `getAssetIndex`
  network task.
- Focused positive and expired cases pass independently in 8.7 seconds. Two
  consecutive full gates pass 428 updates and 33 cases at
  `c/magma/trace/out/test_falling_anvil_chicken_xp_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_chicken_xp_final_2.log`, both about
  1:36 with about 49 MB harness RSS and zero swap.
- Java and the product build. The full native family passes in 5:58.13 at
  436,636 KB peak RSS with zero swap at
  `c/magma/trace/out/test_runtime_chicken_xp.log`. The stopped-oracle scalar
  guard passes at 4,397 steps/s against the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_chicken_xp_cpu_1.json`. An
  `sm_120 --fmad=false` CUDA compile-only probe passes. GPU 1 was not executed,
  so the promoted total remains 676 pending CUDA/Blaze evidence. Looting,
  other XP event branches, broader living types, and XP rendering remain open.

## 2026-08-04 (controlled-pig loot and player-credit terminal XP)

- Extended synchronous controlled-passive loot from chicken to an adult,
  unsaddled pig. The fixed target cursor consumes the one-entry pig pool and
  `set_count` draws and produces one raw-porkchop EntityItem stack, item 319
  count three. The stack retains exact causal EID, position, motion, yaw,
  hover phase, health, lifespan, age, and pickup delay.
- Kept fixed-pool failure transactional per target. The pig preflight reserves
  one item slot and one ID before any damage, event, RNG, or drop mutation.
  Native coverage proves the exact positive target/Math/ID cursors and a
  completely full item-pool rejection.
- Continued the player-credited pig through all 20 death ticks. Tick 20 emits
  one exact value-three XP orb before terminal removal and the 20 pig-sized
  particles. Java and magma match every post row, item age, credit timer,
  event, particle bit payload, orb bit payload, target/World/Math cursor, and
  final causal ID.
- Generalized the real-Java passive lifecycle fixture without adding another
  harness. Captured loot is inserted into the real server entity list without
  a tracker mirror, preventing client constructors from racing static
  `Math.random`. The isolated real item/orb `onUpdate` methods are driven
  directly; this removed an intermittent chunk-wrapper skipped tick. Failed
  contaminated runs were not promoted.
- Three consecutive focused pig gates pass in about nine seconds. Two
  consecutive complete gates pass 461 updates and 34 cases at
  `c/magma/trace/out/test_falling_anvil_pig_loot_xp_final_1.log` and
  `c/magma/trace/out/test_falling_anvil_pig_loot_xp_final_2.log`, both about
  1:42 with about 46 MB harness RSS and zero swap.
- Java and the product build. The full native family passes in 6:07.52 at
  437,744 KB peak RSS with zero swap at
  `c/magma/trace/out/test_runtime_pig_loot_xp.log`. With the oracle stopped,
  scalar throughput passes at 4,713 steps/s against the 4,062 baseline and
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_pig_loot_xp_cpu_1.json`. The new
  branch is active lethal-pig-only and adds no idle scan or allocation. GPU 1
  was untouched, so the promoted total remains 676 pending CUDA/Blaze
  evidence. Cow/sheep loot, looting modifiers, other killer/Forge XP branches,
  and item/orb persistence remain open.

## 2026-08-04 (controlled-cow loot and player-credit terminal XP)

- Extended exact adult passive loot to cow's two ordered pools. The pinned
  target emits leather item 334 count one followed by raw beef item 363 count
  one, with exact causal IDs, stack state, pose/motion, yaw, hover phase,
  pickup delay, and age. Native coverage also locks the on-fire cooked-beef
  item 364 branch.
- Preserved exact RNG and event order. The fresh lethal cow consumes eight
  target LCG steps across attacked-state, death-pitch, two one-entry choices,
  and two count draws. Two item constructors leave global Math at step 24.
  Status 2, the neutral cow death sound at volume 0.4, and status 3 occur in
  exact order.
- Continued player credit through all 20 death ticks. Tick 20 creates one
  value-three XP orb at causal ID 520004, updates it in the same world tick,
  emits 20 exact cow-sized particles, and retires the cow at final causal ID
  520005. Java's seed-specific Gaussian rejection draws leave the target
  cursor at initial plus 216 LCG steps; every raw orb/particle word and global
  cursor matches.
- Kept the fixed item table transactional per controlled target. This pinned
  cow requires two item slots. A one-free-slot native fixture proves rejection
  before health, event, RNG, ID, or drop mutation; positive native cases lock
  raw/cooked pool order and cursors.
- Three consecutive focused Java-vs-magma comparisons pass in about 8.7
  seconds each. Two complete gates pass 494 updates and 35 cases at
  `c/magma/trace/out/test_falling_anvil_cow_loot_xp_full_1.log` and
  `c/magma/trace/out/test_falling_anvil_cow_loot_xp_full_2.log`, both about
  1:45 with 49,000 KB peak harness RSS and zero swap.
- Java and the CPU product build. The complete native wrapper passes in
  5:39.50 at 440,644 KB peak RSS with zero swap at
  `c/magma/trace/out/test_runtime_cow_loot_xp_full.log`. With the oracle
  stopped, scalar throughput passes at 4,520 steps/s against the 4,062
  baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_cow_loot_xp_cpu_1.json`, while
  unrelated host training was active. The cow path is accepted-lethal-only
  and adds no idle scan or allocation. GPU 1 was untouched, so the promoted
  total remains 676 pending CUDA/Blaze evidence. Sheep and broader mob loot,
  child behavior, looting modifiers, other killer/Forge XP branches, and
  item/orb persistence remain open.

## 2026-08-04 (controlled white-sheep loot and player-credit terminal XP)

- Extended exact adult passive loot to the represented unsheared white sheep.
  The color-specific table emits wool item 35 meta zero count one first, then
  enters the nested base-sheep table and emits raw mutton item 423 count two.
  Both item entities retain exact causal IDs, stack state, constructor
  payloads, pickup delay, age, yaw, and hover phase.
- Measured the real game before accepting audit arithmetic. The fresh-hit path
  consumes four target LCG steps and the sheep tables consume four more, not
  the audit's proposed six total. Immediate target, two-item Math step 24,
  status/sheep-death-sound/status order, and next causal ID 520004 all match.
- Continued player credit through all 20 death ticks. Tick 20 produces XP
  three, one same-tick-updated orb at ID 520004, and 20 exact 0.9 by 1.3 sheep
  particles before final ID 520005. Sheep's seed-specific Gaussian rejection
  path leaves its target cursor at initial plus 224 LCG steps. Every post row,
  item age, raw orb/particle word, and World/Math cursor is exact.
- Added source-backed native coverage for cooked mutton item 424. The fixed
  item table preflights both required stacks; a one-free-slot fixture proves
  rejection before health, event, RNG, ID, or drop mutation. Other fleece
  colors and sheared state remain explicitly outside this represented slice.
- Three consecutive focused Java-vs-magma comparisons pass in about 8.7
  seconds each. Two complete gates pass 527 updates and 36 cases at
  `c/magma/trace/out/test_falling_anvil_sheep_loot_xp_full_1.log` and
  `c/magma/trace/out/test_falling_anvil_sheep_loot_xp_full_2.log`, both about
  1:49 with about 49 MB peak harness RSS and zero swap.
- Java and the CPU product build. The complete native wrapper passes in
  5:38.53 at 443,364 KB peak RSS with zero swap at
  `c/magma/trace/out/test_runtime_sheep_loot_xp_full.log`. With the oracle
  stopped, scalar throughput passes at 4,548 steps/s against the 4,062
  baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_sheep_loot_xp_cpu_1.json`, while
  unrelated host training was active. The branch is accepted-lethal-only and
  adds no idle scan or allocation. GPU 1 was untouched, so the promoted total
  remains 676 pending CUDA/Blaze evidence.

## 2026-08-04 (general sheep fleece and sheared-state parity)

- Added Minecraft's exact five-bit adult-sheep state to the live mob store:
  fleece metadata 0..15 plus the sheared bit. The state validates at the cold
  fixture boundary, resets on slot reuse, reaches live entity views, and now
  drives both fleece tint/wool-layer rendering and lethal loot selection.
- Added strict real-Java red and sheared fixtures. Unsheared red emits wool
  35:14 count one and raw mutton 423 count two, retaining target plus eight,
  Math step 24, orb ID 520004, terminal target plus 224, and final ID 520005.
  Sheared red bypasses the color table, emits only raw mutton 423 count two,
  and uses target plus six, Math step 16, orb ID 520003, terminal target plus
  222, and final ID 520004. All 20 lifecycle rows and every raw particle/orb
  word match.
- Rejected speculative audit arithmetic before promotion. The real client
  measured sheared mutton count two and terminal cursor plus 222, not count one
  and plus 212. The bundled 1.11.2 loot JSON also proves every unsheared color
  retains the same two outer one-entry draws as white.
- Expanded native coverage across white raw/cooked, red, and sheared loot,
  exact wool metadata, RNG/Math/ID cursors, setter bounds, live render state,
  slot reset, and capacity. An unsheared two-slot target rejects atomically
  with one slot free; switching that unchanged target to sheared succeeds in
  the same slot.
- Focused white, red, sheared, and nonlethal comparisons pass. Two consecutive
  full Java-vs-magma gates pass 593 updates and 38 cases in 2:02.36 and 2:00.64
  at `c/magma/trace/out/test_falling_anvil_sheep_state_full_1.log` and
  `c/magma/trace/out/test_falling_anvil_sheep_state_full_2.log`, about 46 MB
  peak harness RSS and zero swap each. Java, the CPU product, and entity-render
  suites pass.
- The clean native aggregate passes in 5:17.19 at 253,716 KB peak RSS, zero
  major faults, and zero swap at
  `c/magma/trace/out/test_runtime_sheep_state_full.log`. With the oracle
  stopped, scalar throughput passes at 4,664 steps/s against the 4,062 baseline
  and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_falling_anvil_sheep_state_cpu_1.json`. GPU 1
  remained untouched and the promoted total stays 676. Natural color spawning,
  shearing/graze regrowth, and sheep state-capsule persistence remain open.

## 2026-08-04 (exact player sheep shearing)

- Added the survival entity-use path for represented sheep: three-block
  expanded-AABB targeting, nearer-block occlusion, and the one-tick delayed
  integrated-server transition for main-hand and empty-main/offhand shears.
- Matched adult eligibility, the five-bit fleece state, one-to-three separate
  wool stacks, neutral shear sound, durability and Unbreaking III. Every wool
  entity retains exact ID, item/meta/count, pickup delay, position, motion,
  yaw, hover phase, and the sheep, Math, and injected local-Random cursors.
  Child and already-sheared targets are handled without mutation.
- Added a seven-case parked real-Java comparator plus native capacity and
  runtime controls. Two consecutive strict passes cover ten exact wool
  entities at `c/magma/trace/out/test_shearing_full_1.log` and
  `c/magma/trace/out/test_shearing_full_2.log`. The fixed item pool rejects an
  unrepresentable multi-drop atomically.
- Regression review also corrected player damage against boats to use the
  actual cooled attack amount times ten and repaired obsolete small-fireball
  fixtures whose unsupported fire was correctly removed by `BlockFire`.
  `game/test_mob_live.sh` and the entity-render suite pass.
- The complete native aggregate passes in 5:29.76 at 446,192 KB peak RSS,
  zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_shearing_full.log`. Java and the CPU product
  build from the final source.
- With the oracle stopped, scalar throughput passes at 4,788 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_shearing_cpu_1.json`. The interaction has no
  idle allocation or loaded-world scan. GPU 1 remained untouched.
- Still open: broken-shears client particles/sound and their RNG draws,
  general main/offhand use precedence, natural fleece colors, ordinary age
  progression, grazing/regrowth AI, and sheep/item capsule persistence.

## 2026-08-04 (exact sheep grazing and fleece regrowth)

- Added the represented `EntityAIEatGrass` task for ordinary AI-enabled sheep.
  The natural scheduler checks one entity RNG draw every three mob ticks, uses
  the child/adult 50/1000 bounds, gives metadata-one tallgrass priority over
  the grass block below, and suppresses movement while the 40-tick task runs.
- Matched the exact effect update at timer four: status 10, world event 2001
  with state IDs 4127 or 2, block removal or grass-to-dirt mutation under
  `mobGriefing`, unconditional fleece regrowth, and child growth by 1200 with
  the vanilla zero clamp. NoAI sheep do not schedule the task, and represented
  panic interrupts an active task before its effect.
- Added live timer-derived head and neck pose fields. Their tick-boundary
  formulas now use the exact vanilla timer; partial-tick interpolation and the
  real-Java grazing pixel matrix remain separate rendering work.
- Eleven strict real-Java cases cover grass, tallgrass priority, no-grief
  variants, child growth and clamp, unsheared child behavior, air, fern, and an
  RNG miss. Two consecutive comparisons pass at
  `c/magma/trace/out/test_grazing_full_1.log` and
  `c/magma/trace/out/test_grazing_full_2.log`, about 3.2 seconds and 30,252 KB
  peak RSS each with zero swap.
- The focused live-product gate passes at
  `c/magma/trace/out/test_grazing_runtime.log`. The first complete native run
  caught a real integration regression: a NoAI cold-fixture sheep consumed a
  scheduler RNG draw and changed the falling-anvil aggregate. Guarding the
  scheduler with the entity's NoAI state restored the exact event and RNG
  sequence. The corrected aggregate passes in 5:23.18 at 445,928 KB peak RSS,
  zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_grazing_full_2.log`.
- With the oracle stopped, scalar throughput passes at 4,718 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_grazing_cpu_1.json`. The task adds no heap
  allocation and only a three-tick-gated loaded-sheep scan. GPU 1 was untouched.
- Still open: non-panic goal conflicts, exact client partial-tick grazing
  pixels, save-capsule persistence, and general passive breeding, tempt,
  follow-parent, swimming, and ownership systems. Natural fleece selection is
  covered by the following slice.

## 2026-08-04 (exact natural sheep fleece colors)

- Added `EntitySheep.getRandomSheepColor` at the native spawn boundary using
  the shared runtime `World.rand` cursor. Rolls below 18 select black, gray,
  silver, or brown from the exact boundaries; the remaining branch consumes
  a second bound-500 draw for rare pink or white. Generic component fixtures
  and reload-style state remain explicit and do not reroll.
- Matched the Java `onInitialSpawn` data write: only the fleece low nibble is
  replaced, so an existing sheared bit survives. Natural product sheep use the
  World cursor, never the synthetic per-entity constructor stream. Render and
  loot already consume the resulting metadata without another conversion.
- Added six boundary seeds that exercise every result and both RNG lengths.
  Two consecutive strict gates pass 13 direct/onInitialSpawn comparisons at
  `c/magma/trace/out/test_sheep_color_full_1.log` and
  `c/magma/trace/out/test_sheep_color_full_2.log`, each in about 2.2 seconds at
  30,252 KB peak RSS and zero swap.
- The focused product gate covers all six branches, cursor refusal, high-bit
  preservation, live-view export, and an ordinary passive sheep spawn. It
  passes in 0.02 seconds at 18,756 KB peak RSS and zero swap at
  `c/magma/trace/out/test_sheep_color_runtime.log`. Java, mob, renderer, and
  product builds pass.
- The complete native aggregate passes in 5:50.27 at 445,892 KB peak RSS,
  zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_sheep_color_full.log`. With the oracle
  stopped, scalar throughput passes at 4,598 steps/s against the 4,062
  baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_sheep_color_cpu_1.json`. Selection adds no
  allocation or idle scan and at most two LCG steps per new sheep. GPU 1 was
  untouched.
- Still open: the surrounding passive spawner's approximate hash-based
  candidate/type/pack ordering, exact six-color pixels, sheep state-capsule
  persistence, higher-priority grazing task conflicts, and the broader animal
  lifecycle systems.

## 2026-08-04 (exact sheep feeding, genetics, mating, and birth)

- Added exact wheat interaction for represented sheep through the real player
  hand route. Adults enter 600 ticks of love and emit status 18; children use
  vanilla `ageUp`, forced-age accumulation, and the 40-tick client timer.
  Survival and creative consumption, main/offhand precedence, delayed server
  use, and solid-block occlusion are covered.
  Growth arithmetic uses explicit Java 32-bit wrapping rather than signed C
  overflow; two extreme-age oracle rows include `Integer.MIN_VALUE`, and a
  focused signed-overflow/float-cast sanitizer run passes.
- Added the complete 16 by 16 sheep child-color matrix. All nine crafting
  recipe results and both fallback choices match Java, including the exact
  one-bit `World.rand` cursor only when no recipe exists.
- Added the exact mate lifecycle. The direct callback waits through update 59
  and births on update 60 only below distance squared nine. It matches parent
  age 6000 and love reset, child age -24000 and fleece, child and XP global
  IDs, three child and four XP constructor `Math.random` doubles, seven heart
  particles, the parent's Gaussian cache, `doMobLoot`, Forge cancellation,
  and null-child ordering.
  Newborns explicitly reset their size to one even when the fixed store reuses
  a formerly size-four slime/magma slot. Child and XP allocation also advances
  both local allocators across every consumed global ID, including saturated
  stores, so a later local spawn cannot reuse a consumed ID.
- Integrated ordinary sheep mating into the bounded three-tick goal cadence.
  It selects the nearest compatible represented mate, yields to panic,
  preempts grazing, and does not restart on the same setup tick after a failed
  continuation. Living-store and XP-store saturation have explicit tested
  fallbacks and dropped counters without overwriting existing state.
  This scheduler path is native-tested but not yet promoted as full-tick Java
  parity: Java may tick the newly appended child later in the birth tick, and
  its clock-seeded `Entity.rand` and follow-parent/navigation selection are not
  yet captured.
- Two consecutive focused Java comparisons pass 13 feeding cases, including
  Java-wrapped extreme child ages, 512
  genetics rows, and eight mating lifecycles at
  `c/magma/trace/out/test_sheep_feed_full_3.log`,
  `c/magma/trace/out/test_sheep_feed_full_4.log`,
  `c/magma/trace/out/test_sheep_genetics_full_1.log`,
  `c/magma/trace/out/test_sheep_genetics_full_2.log`,
  `c/magma/trace/out/test_sheep_mating_full_3.log`, and
  `c/magma/trace/out/test_sheep_mating_full_4.log`. Steady-state comparisons
  take about 2.2 seconds at roughly 30 MB peak RSS with zero swap; the second
  expanded feeding run also rebuilt header-dependent native objects.
- The focused ordinary/runtime gate passes in 0.03 seconds at 18,720 KB peak
  RSS. The final-source complete native aggregate passes in 5:47.68 at 446,060
  KB peak RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_sheep_mating_full_final.log`. The stopped-oracle
  scalar guard passes at 4,804 steps/s against the 4,062 baseline and 3,858.9
  floor at `c/magma/trace/out/perf_guard_sheep_mating_cpu_final.json`. The inactive
  path allocates nothing; only in-love sheep perform the bounded mate scan.
  GPU 1 was untouched.
- An attempted every-tick reconciliation of the mob-local and runtime-global
  entity-ID cursors failed the full aggregate by contaminating unrelated
  farmland, cake, and falling-anvil fixtures. It was rejected. The final code
  reconciles cursors only when an ordinary birth actually allocates an
  entity, preserving inactive tick state.
- Still open: exact full-world-tick newborn state and child RNG, non-sheep
  breeding, tempting and full navigation, follow-parent behavior, broader AI
  task conflicts, player breeding statistics and criteria, passive lifecycle
  persistence, and behavior under more complex simultaneous pool pressure.

## 2026-08-04 (exact same-tick sheep newborn boundary)

- Added a parked real `World.updateEntities` oracle for one isolated
  high-air sheep birth. It snapshots and clears unrelated entity, weather,
  player, and tile lists, starts the real mate task, restores a valid update-60
  delay at the task callback, and pins the newborn's valid saved `Entity.rand`
  state. Java then proves its live loaded-entity loop reaches the appended
  child and XP orb later in that same tick.
- Added the matching native birth-local queue and first-update state. Parent
  and child ticks/age/living-sound/task counters, raw position and motion,
  first gravity/move step, negative lower-task RNG path, Gaussian state,
  particles, child/XP append order, world/Math cursors, and IDs now match.
  The promotion is deliberately bounded to one coincident compatible pair and
  does not claim a general dynamically sized entity loop.
- Three recipe/fallback and `doMobLoot` cases pass exact Java-vs-magma
  comparison twice and again after integration at
  `c/magma/trace/out/test_sheep_mating_tick_full_1.log`,
  `c/magma/trace/out/test_sheep_mating_tick_full_2.log`, and
  `c/magma/trace/out/test_sheep_mating_tick_after_anvil.log`. The final
  header-rebuilt comparison passes at
  `c/magma/trace/out/test_sheep_mating_tick_final_source.log`. The neighboring
  isolated falling-anvil sheep oracle also remains green.
- The aggregate exposed two stale anvil expectations. A real world tick gives
  the loaded NoAI sheep 11 ambient RNG rolls before impact and a twelfth after
  impact, while the deliberately isolated anvil fixture advances only the
  falling entity. Updating the aggregate to the real world ordering preserves
  both contracts. The final header-rebuilt native suite passes in 6:17.39 at
  446,552 KB peak RSS, one major fault, and zero swap at
  `c/magma/trace/out/test_runtime_sheep_mating_tick_full_final_source.log`.
- A focused ASan/UBSan run found no mating error but exposed signed-left-shift
  UB in the shared Java `Random.nextLong` helper. Unsigned modulo-2^64
  composition removes the UB while retaining the signed low-word rule; the
  17-line Java/CPU/CUDA RNG oracle and the sanitizer both pass. Java and the
  CPU product build from final source.
- With the oracle stopped, scalar throughput passes at 4,583 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_sheep_mating_tick_cpu_final.json`. The newborn
  queue is fixed stack storage, adds no heap allocation, and is consumed only
  after an actual birth. GPU 1 was used only for the short RNG parity gate;
  no CUDA/Blaze performance path changed.
- Still open: grounded noncoincident navigation and simultaneous-birth
  loaded-entity interleavings, unpinned clock-dependent child construction,
  accepted wander/watch/tempt/follow-parent tasks, other breedable species,
  breeding statistics and criteria, and lifecycle persistence.

## 2026-08-04 (airborne mating and living-entity pushes)

- Expanded the real full-tick sheep oracle from three coincident controls to
  seven cases. The new matrix covers an overlapping 0.25-block pair and
  airborne pairs at 1.0, 2.0, and 2.75 blocks while retaining recipe,
  fallback-color, and `doMobLoot=false` controls.
- Removed native direct mating movement when PathNavigateGround cannot start.
  Java's high-air parents begin with `onGround=false`, so their navigator
  returns no path and leaves body yaw and horizontal motion unchanged. This
  promotes that observable result; persistent path and helper state remains
  in the grounded-navigation slice.
- Added the exact represented living-entity push boundary after travel. It
  uses strict AABB intersection, Java's float-rounded square root and impulse
  constants, serially visible motion updates, and child/adult widths. Births
  now also start ungrounded, and passive children use their Java half-size
  physics box. The last detail was exposed by an oracle restart whose player X
  coordinate made the incorrect adult-width midpoint rounding observable by
  one double ULP.
- The final seven-case Java-vs-magma gate passes twice, including one oracle
  restart, at `c/magma/trace/out/test_sheep_mating_collision_final.log` and
  `c/magma/trace/out/test_sheep_mating_collision_final_2.log`. The first run
  takes 2.39 seconds at 104,480 KB peak RSS with zero swap. The focused
  ASan/UBSan gate passes at
  `c/magma/trace/out/test_sheep_mating_collision_sanitized_final.log`. The
  final-source native aggregate passes in 5:43.70 at 446,296 KB peak RSS, zero
  major faults, and zero swap at
  `c/magma/trace/out/test_runtime_sheep_mating_collision_final.log`.
- With the oracle stopped, scalar throughput passes at 4,837 steps/s against
  the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_sheep_mating_collision_cpu_final.json`. The push
  scan uses fixed storage, performs no allocation, is absent when there are no
  live mobs, and is bounded by the existing 95-mob product capacity. GPU 1 was
  untouched.
- Still open: grounded path finding and move/look-helper state, simultaneous
  birth and slot-reuse loaded-list ordering, entity cramming, player/team/
  riding collision rules, accepted lower-priority animal tasks, other
  breedable species, statistics/criteria, and lifecycle persistence.

## 2026-08-04 (grounded mating first-step navigation)

- Added a temporary isolated stone platform to the full-tick mate oracle and
  expanded the strict matrix from seven to ten cases. The new rows cover an
  axial path, a diagonal path, and a turn that reaches MoveHelper's 90-degree
  clamp while retaining every airborne, overlap, recipe, fallback, and
  `doMobLoot=false` control.
- The integrated client and server share process-global `Entity.nextEntityID`
  and `Math.random`. Longer pathfinding made unrelated client construction
  race the birth. A fixture-only `spawnBaby` HEAD hook now restores those two
  cursors at the exact causal boundary and asserts one pin, while the existing
  spawn redirect pins the newborn's valid saved private RNG. Child and XP
  constructor consumption remains visible and exact.
- Native grounded mating now uses the integer path-node center, Minecraft
  1.11.2's table-based `MathHelper.atan2`, MoveHelper angle wrapping and
  limiting, and the real sheep move speed for both `landMovementFactor` and
  `moveForward`. This changes only an active grounded mate task; airborne
  tasks retain the proven no-path behavior.
- All ten Java-vs-magma full birth ticks pass before and after a cold oracle
  restart in 0.74 and 0.82 seconds, 30,252 KB peak RSS, and zero swap at
  `c/magma/trace/out/test_sheep_mating_grounded_navigation.log` and
  `c/magma/trace/out/test_sheep_mating_grounded_navigation_restart.log`. The
  focused ASan/UBSan runtime passes in 0.08 seconds at 102,164 KB peak RSS at
  `c/magma/trace/out/test_sheep_mating_grounded_navigation_sanitized.log`.
- The broad native aggregate passes in 6:40.87 at 446,224 KB peak RSS, zero
  major faults, and zero swap at
  `c/magma/trace/out/test_runtime_sheep_mating_grounded_navigation.log`. With
  Java stopped, two scalar guards pass at median 4,339 and 5,018 steps/s
  against the 4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_sheep_mating_grounded_navigation_cpu.json` and
  `c/magma/trace/out/perf_guard_sheep_mating_grounded_navigation_cpu_2.json`.
  GPU 1 was observed but not used or modified.
- Still open: obstacle/gap pathfinding, persistent multi-tick navigator and
  head/look-helper state, simultaneous-birth and slot-reuse loaded ordering,
  accepted lower tasks, other breedable species, statistics/criteria, and
  lifecycle persistence. The global effort estimate remains approximately
  16%.

## 2026-08-04 (simultaneous sheep-birth dynamic order)

- Expanded the parked real `World.updateEntities` oracle from one mating pair
  to two independent pairs in the same entity boundary. The fixture loads all
  four parents first and captures every real dispatch at
  `updateEntityWithOptionalForce`. Pair A restores the shared Math/EID cursor;
  pair B deliberately inherits the child and optional XP constructor draws so
  causal ordering is measured rather than duplicated in fixture arithmetic.
- Replaced the native single pending child RNG with a fixed FIFO and retained
  a bounded same-tick event tail in exact append order. Two births now dispatch
  parents A1, A2, B1, B2, child A, XP A, child B, XP B. The no-loot control
  proves the contiguous-child alternative. Native assertions also pin both XP
  first-update states, final Math/EID cursors, both newborn private RNG states,
  and the complete trace order. A review found that the trace buffer's old
  living-plus-XP bound could truncate at maximum simultaneous load; its proven
  fixed capacity now includes the child/XP tail.
- All twelve exact Java-vs-magma cases pass before and after a cold oracle
  restart at
  `c/magma/trace/out/test_sheep_mating_simultaneous_order.log` and
  `c/magma/trace/out/test_sheep_mating_simultaneous_order_restart.log`. The
  strengthened final-source run passes at
  `c/magma/trace/out/test_sheep_mating_simultaneous_order_final.log`. It checks
  exact loaded and dispatch order, parent/child/XP IDs, first physics, entity
  and world RNGs, shared Math cursor, particles, and hook counts.
- The focused ASan/UBSan runtime passes at
  `c/magma/trace/out/test_sheep_mating_simultaneous_order_sanitized.log`. The
  broad native aggregate passes in 5:12.02 at 446,248 KB peak RSS, zero major
  faults, and zero swap at
  `c/magma/trace/out/test_runtime_sheep_mating_simultaneous_order.log`. With
  Java stopped, scalar guards pass at 4,922 and 4,626 steps/s against the 4,062
  baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_sheep_mating_simultaneous_order_cpu_1.json` and
  `c/magma/trace/out/perf_guard_sheep_mating_simultaneous_order_cpu_2.json`.
  The queues use fixed storage and no heap allocation; GPU 1 was untouched.
- This promotes the dynamic append tail, not a general persistent loaded list.
  Pre-existing living and XP entities still dispatch by separate native pool,
  and non-mob runtime entities have independent order. The next discriminating
  fixture puts a pre-existing XP orb before the parents so the current grouping
  fails at the first dispatch. Slot reuse, broader persistent ordering,
  obstacle/multi-tick navigation, lower animal tasks, and other breedable
  species remain open. The portfolio remains 13 `DONE`, seven `ACTIVE`, and
  12 `QUEUED`, with the effort-weighted global estimate approximately 16%.

## 2026-08-04 (persistent living/XP order and slot reuse)

- Added the next discriminating real-Java fixtures: an XP orb is inserted into
  `World.loadedEntityList` before a mating pair, and the expiry control starts
  it at age 5999 so the later breed XP reuses the native XP slot. Old C fails
  the first case by grouping the parents before the older orb at
  `c/magma/trace/out/test_sheep_mating_preexisting_xp_old_c.log`.
- Replaced the separate native living/XP initial scans with one fixed
  generation-tagged list. Every represented living and XP creation appends a
  typed slot reference; terminal removal invalidates its generation, so a new
  entity in the same slot cannot inherit the stale position. Both the ordinary
  and controlled schedulers retain this order across ticks. Dynamic births
  still use the previously proven child/XP event tail for their same-boundary
  first updates.
- All fourteen exact Java-vs-magma cases pass on current source and after a
  cold oracle restart at
  `c/magma/trace/out/test_sheep_mating_persistent_order_final.log` and
  `c/magma/trace/out/test_sheep_mating_persistent_order_restart.log`. The
  focused native gate checks the expiring-orb slot collision plus two
  consecutive XP-before-sheep ticks in both schedulers at
  `c/magma/trace/out/test_sheep_mating_persistent_order_native.log`. The direct
  ASan/UBSan build passes at
  `c/magma/trace/out/test_sheep_mating_persistent_order_sanitized.log`.
- The broad aggregate passes in 6:13.91 at 446,400 KB peak RSS, zero major
  faults, and zero swap at
  `c/magma/trace/out/test_runtime_persistent_order.log`. With Java stopped,
  scalar guards pass at 4,936 and 5,108 steps/s against the 4,062 baseline and
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_sheep_mating_persistent_order_cpu_1.json` and
  `c/magma/trace/out/perf_guard_sheep_mating_persistent_order_cpu_2.json`.
  The list is fixed at 380 references, adds no allocation, and leaves GPU 1
  untouched.
- Added a fifteenth real-Java case with a no-AI cow loaded before the parents
  at health zero and death time 19. It reaches terminal removal before the
  mating update; the newborn then reuses the cow's native slot under a fresh
  generation. Exact update order is cow, parents, child, breed XP, and final
  loaded order is parents, child, breed XP. The matrix passes ten consecutive
  stress repetitions and after a cold restart. The final comparison is
  `c/magma/trace/out/test_sheep_mating_living_slot_reuse_final.log`.
- The living-slot focused native and ASan/UBSan gates pass at
  `c/magma/trace/out/test_sheep_mating_living_slot_reuse_native.log` and
  `c/magma/trace/out/test_sheep_mating_living_slot_reuse_sanitized.log`. The
  exact spawn API now accepts health zero, matching a valid serialized
  in-progress death state. The final broad aggregate passes in 5:07.96 at
  446,216 KB peak RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_living_slot_reuse.log`. Clean scalar guards
  pass at 5,033 and 5,174 steps/s at
  `c/magma/trace/out/perf_guard_sheep_mating_living_slot_reuse_cpu_1.json` and
  `c/magma/trace/out/perf_guard_sheep_mating_living_slot_reuse_cpu_2.json`.
- Closed the represented living/XP save-state ordering gap. Java authoritative
  capture now attaches each entity's original `loadedEntityList` rank before
  its existing distance sort and 64-entity cap. Capsule validation accepts old
  single-restorable-entity rows, requires complete unique non-negative ranks
  when present, and refuses ambiguous older multi-entity capsules rather than
  silently sorting them by EID. New capsules emit NoAI pigs and XP orbs by the
  captured Java rank.
- The discriminating capsule payload is distance-sorted as pig 91 then XP 92,
  while its actual loaded ranks restore XP 92 then pig 91. The selftest also
  rejects duplicate and partial ranks and ambiguous old multi-entity input. It
  passes in 0.15 seconds at 31,704 KB peak RSS at
  `c/magma/trace/out/state_capsule_loaded_order.log`. The focused native case
  runs that reverse-EID order through both ordinary and controlled schedulers:
  tick one retains `[92,91]`, and tick two dispatches `[92,91]` before exact XP
  expiry leaves `[91]`. It passes in 0.17 seconds at 63,560 KB at
  `c/magma/trace/out/test_sheep_mating_capsule_order_native.log` and under
  ASan/UBSan in 0.77 seconds at 132,236 KB at
  `c/magma/trace/out/test_sheep_mating_capsule_order_sanitized.log`.
- The Java project builds cleanly apart from its four established warnings.
  The 15-case exact birth boundary remains green after the cold restart at
  `c/magma/trace/out/test_sheep_mating_capsule_order_final_2.log`. With the
  oracle stopped, scalar guards pass at 5,131 and 5,134 steps/s against the
  4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_capsule_loaded_order_cpu_1.json` and
  `c/magma/trace/out/perf_guard_capsule_loaded_order_cpu_2.json`. Capture and
  import are cold bounded paths; no per-tick work, allocation, CUDA change, or
  GPU 1 work was added. The broad native aggregate passes in 5:07.15 at
  446,316 KB peak RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_capsule_loaded_order.log`.
- This promotes persistent and imported ordering only across the represented
  living and XP stores. Items, TNT, projectiles, falling blocks, crystals,
  dragons, players, teams, and riding relations remain outside this list.
  Global cross-store order, obstacle/multi-tick navigation, accepted lower
  animal tasks, and other breedable species remain open. The portfolio stays
  13 `DONE`, seven `ACTIVE`, and 12 `QUEUED`; the global estimate remains
  approximately 16%.

## 2026-08-04 (cow, pig, and chicken breeding-item feeding)

- Promoted the shared `EntityAnimal` breeding-item path from sheep-only to all
  represented farm animals. Cows and sheep accept wheat 296; pigs accept
  carrot 391, potato 392, and beetroot 434; chickens accept wheat seeds 295,
  pumpkin seeds 361, melon seeds 362, and beetroot seeds 435. The existing
  fixed per-slot state now applies exact adult love 600, player credit, status
  18, creative/survival consumption, child age-up/forced-age arithmetic,
  ordinary love ticking, and damage reset without a new allocation or scan.
- Generalized the delayed runtime packet route across the four animal types
  while preserving main/offhand selection and sheep-shears precedence. Adult
  cow buckets and pig saddles conservatively preempt an offhand feed; their
  milk, saddle, mount, named-tag, inventory, and sound side effects remain open
  and are not claimed by this slice. A plain name tag passes and a child cow
  bucket correctly passes to offhand breeding food.
- Extended the parked real-game fixture and native oracle behind an explicit
  species selector. The final matrix has 53 exact cases: every accepted item,
  cross-species food rejection, adult/child/cooldown/already-love states,
  creative and survival inventory, player credit, status 18, exact extreme-age
  arithmetic, and hand order. Two final runs pass in 2.23 and 2.30 seconds at
  30,252 KB peak RSS at `c/magma/trace/out/test_animal_feed_final_2.log` and
  `c/magma/trace/out/test_animal_feed_repeat.log`.
- Focused native coverage passes in 0.23 seconds at 38,888 KB peak RSS at
  `c/magma/trace/out/test_animal_feed_native_final.log`. AddressSanitizer passes in
  0.70 seconds at 132,384 KB peak RSS at
  `c/magma/trace/out/test_animal_feed_asan.log`, and the existing direct sheep
  mating oracle remains green at
  `c/magma/trace/out/test_animal_feed_sheep_mating_regression.log`. The Java
  project builds with its four established warnings at
  `c/magma/trace/out/test_animal_feed_gradle_final.log`.
- With Java stopped, scalar guards pass at 4,928 and 5,045 steps/s against the
  4,062 baseline and 3,858.9 floor at
  `c/magma/trace/out/perf_guard_animal_feed_cpu_1.json` and
  `c/magma/trace/out/perf_guard_animal_feed_cpu_2.json`. GPU 1 was not used.
  The broad native aggregate passes in 5:10.07 at 445,584 KB peak RSS, zero
  major faults, and zero swap at
  `c/magma/trace/out/test_runtime_animal_feed.log`.
  Cow/pig/chicken mating and birth are the next bounded feature slice; the
  global estimate remains approximately 16%.

## 2026-08-04 (direct cow, pig, and chicken birth boundary)

- Generalized the existing direct `EntityAIMate` callback without changing the
  live non-sheep scheduler. Parents must be distinct and exactly the same
  represented species. On update 60 inside squared distance 9, the callback
  preserves the Java order: child EID, three `Math.random` doubles, sheep-only
  World-RNG fleece selection, Forge boundary, parent cooldown/love reset,
  same-species age-24000 child, seven species-sized heart particles from the
  initiator RNG, then optional breed XP 1..7 and its global ID.
- Parameterized the parked Java and native direct fixtures for sheep, cow, pig,
  and chicken. The 23 cases retain the eight sheep genetics/lifecycle rows and
  add each non-sheep species' no-love, update-59, exact-distance-three, birth,
  and no-loot rows. Two final comparisons pass at
  `c/magma/trace/out/test_animal_mating_final.log` and
  `c/magma/trace/out/test_animal_mating_final_repeat.log`. They compare child
  type/age/pose, parent cooldown/love and private RNG, species-sized particles,
  XP, IDs, and shared World/Math cursors.
- Native component coverage additionally rejects a cow/pig pair, proves that
  non-sheep birth does not consume World RNG, and keeps the sheep-only API from
  admitting non-sheep parents. The final focused component passes in 0.16
  seconds at 63,984 KB peak RSS at
  `c/magma/trace/out/test_animal_mating_native_final.log`. AddressSanitizer
  passes in 0.49 seconds at 109,880 KB peak RSS at
  `c/magma/trace/out/test_animal_mating_asan.log`.
  Scalar guards pass at 5,166 and 5,129 steps/s against the 3,858.9 floor at
  `c/magma/trace/out/perf_guard_animal_mating_direct_cpu_1.json` and
  `c/magma/trace/out/perf_guard_animal_mating_direct_cpu_2.json`. GPU 1 was not
  used. The broad native aggregate passes in 5:28.64 at 446,096 KB peak RSS,
  zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_animal_mating_direct.log` and `.time`.
- At this checkpoint scope was intentionally immediate. Java child construction uses a fresh
  private `Random`, and chicken consumes it for `timeUntilNextEgg` 6000..11999.
  The next slice below transports those post-construction fields; non-sheep
  next-tick/save-state continuation and the live mating scheduler remained open
  here. Sheep retained its existing exact child-RNG continuation path.

## 2026-08-04 (four-species newborn private-state transport)

- Replaced the sheep-only birth RNG FIFO with a bounded animal-child FIFO. A
  successful represented birth now installs the supplied 48-bit private RNG,
  Gaussian cache/value, and, for chicken, `timeUntilNextEgg`. The old sheep
  entry points remain compatibility wrappers. Cancelled, null-child, and
  full-living-store paths retain their queued state until a child is actually
  allocated.
- The parked direct fixture pins the Java child RNG after construction but
  before `World.spawnEntity`. Chicken's constructor-derived egg timer remains
  untouched and is captured from the real child, checked to be in 6000..11999,
  then transported into the native comparison. Two 23-case strict passes at
  `c/magma/trace/out/test_animal_child_state_final_1.log` and
  `c/magma/trace/out/test_animal_child_state_final_2.log` cover all four species
  with a populated Gaussian cache.
- The final focused runtime passes in 0.15 seconds at 63,984 KB peak RSS at
  `c/magma/trace/out/test_animal_child_state_native_final.log`. The all-source
  AddressSanitizer build passes in 0.48 seconds at 110,252 KB peak RSS at
  `c/magma/trace/out/test_animal_child_state_asan.log`. Stopped-oracle scalar
  guards pass at 5,037 and 5,033 steps/s against the 3,858.9 floor at
  `c/magma/trace/out/perf_guard_animal_child_state_cpu_1.json` and
  `c/magma/trace/out/perf_guard_animal_child_state_cpu_2.json`. The broad native
  aggregate passes in 5:04.33 at 446,556 KB peak RSS, zero major faults, and
  zero swap at `c/magma/trace/out/test_runtime_animal_child_state.log` and
  `.time`. GPU 1 was untouched.
- The hot path adds no allocation or scan. Reusing the existing 96-entry RNG
  queue plus two 96-entry integer arrays costs 768 bytes. Cow, pig, and chicken
  live mating, their same-boundary newborn tick, ordinary chicken egg laying,
  and save/reload handling of non-persistent `Entity.rand` remain open.

## 2026-08-04 (cow, pig, and chicken live mating scheduler)

- Generalized the ordinary passive mating scheduler from sheep to represented
  cows, pigs, and chickens. The existing fixed task state now performs the
  same-species nearest-mate selection, update-60 birth, persistent child/XP
  append, and same-boundary newborn dispatch for all four species. Sheep-only
  grazing and fleece genetics remain isolated; no per-species scan or heap
  task graph was added.
- Added the missing `EntityChicken.onLivingUpdate` tail. Ordinary construction
  consumes `nextInt(6000)+6000` after the two UUID `nextLong` calls; births then
  replace that private RNG and timer from the captured child FIFO. Five float
  arrays preserve `wingRotation`, `destPos`, `oFlapSpeed`, `oFlap`, and
  `wingRotDelta`, while one byte preserves jockey state. The tail runs after
  movement, collision, age, and love, so airborne fall damping and positive
  breeding-cooldown timer decrement use the same post-super state as Java.
- Extended the parked full-tick oracle behind a new
  `mate_animal_tick_locked` action while leaving the legacy sheep action and
  schema unchanged. The final matrix retains all 15 sheep/order controls and
  adds airborne cow and pig plus airborne and stationary-grounded chicken.
  Two 19-case comparisons pass in 1.13 and 1.02 seconds at 30,252 KB peak RSS
  at `c/magma/trace/out/test_animal_live_scheduler_final_1.log` and
  `c/magma/trace/out/test_animal_live_scheduler_final_2.log`. The 23-case
  direct boundary remains green at
  `c/magma/trace/out/test_animal_live_scheduler_direct_regression.log`.
- The focused native gate passes in 0.18 seconds at 64,100 KB peak RSS, and the
  all-source ASan/UBSan gate passes in 0.76 seconds at 132,748 KB at
  `c/magma/trace/out/test_animal_live_scheduler_native.log` and
  `c/magma/trace/out/test_animal_live_scheduler_asan.log`. The broad aggregate
  passes in 5:34.22 at 446,192 KB peak RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_animal_live_scheduler.log` and `.time`.
  With Java stopped, scalar guards pass at 5,108 and 5,079 steps/s against the
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_animal_live_scheduler_cpu_1.json` and
  `c/magma/trace/out/perf_guard_animal_live_scheduler_cpu_2.json`. The new
  persistent state costs 2,016 bytes, allocates nothing, and GPU 1 was
  untouched.
- A moving grounded chicken at quarter-block separation remains a deliberate
  negative: it reaches the already-filed approximate persistent
  navigator/species-speed path, while the stationary grounded control isolates
  the chicken subclass tail and passes. Timer expiry currently advances the
  chicken private RNG and resets the timer but does not emit the egg sound,
  create and same-boundary-tick the egg item, or advance the shared Math/EID
  cursors. Those are the next bounded animal-life divergence; the global effort
  estimate remains approximately 16%.

## 2026-08-04 (live chicken egg threshold)

- Promoted the isolated adult `EntityChicken` timer-expiry boundary. The native
  tail now preserves Java's exact order: `super` state first, two private-RNG
  pitch floats and egg sound, global EID allocation, four `Math.random`
  constructor values for hover/yaw/X/Z motion, egg spawn, then private-RNG
  `nextInt(6000)+6000`. The item appended during the chicken update receives
  its own same-boundary tick, reaching age 1 and pickup delay 9 with exact
  position, rotation, and motion.
- Extended the generic live-animal fixture without changing the legacy sheep
  schema. A focused threshold discriminator and two repeated full 20-case
  Java-vs-magma matrices pass at
  `c/magma/trace/out/test_chicken_egg_threshold.log`,
  `c/magma/trace/out/test_animal_live_egg_full_1.log`, and
  `c/magma/trace/out/test_animal_live_egg_full_2.log`. They compare exact item
  and sound payloads, raw float fields, dynamic update/entity order, private
  and Math RNG cursors, global EID, and post-boundary chicken timer.
- Focused native coverage also fills the bounded item store and proves the
  failure policy: no item is appended, `spawn_fail_count` increments, and the
  sound, RNG, EID, and timer side effects still occur. The all-source
  ASan/UBSan run passes at
  `c/magma/trace/out/test_chicken_egg_asan.log`. The broad native aggregate
  passes in 5:28.18 at 446,428 KB peak RSS, zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_chicken_egg.log` and `.time`. The Java JDK 8
  build passes.
- With the oracle stopped, scalar guards pass at 5,115 and 4,952 steps/s
  against the 3,858.9 floor at
  `c/magma/trace/out/perf_guard_chicken_egg_cpu_1.json` and
  `c/magma/trace/out/perf_guard_chicken_egg_cpu_2.json`. The ordinary no-expiry
  path adds no allocation or scan, and GPU 1 was untouched. Multi-chicken and
  arbitrary cross-store `loadedEntityList` ordering remain open, as do moving
  grounded navigation, accepted lower tasks, persistence, and statistics. The
  global effort estimate remains approximately 16%.

## 2026-08-04 (grounded cow, pig, and chicken mating speed)

- Promoted the first unobstructed grounded `EntityAIMate` movement step for
  cows, pigs, and chickens. The existing generic path already matched Java's
  integer PathPoint center, table-based `MathHelper.atan2`, 90-degree
  MoveHelper clamp, travel, collision push, birth, and same-boundary dynamic
  tail. The remaining discriminator was the movement attribute: cow is 0.20,
  pig and chicken are 0.25, while the previous fallback was sheep's 0.23.
- Kept the correction inside the active mating MoveHelper. Its species value
  is written to both `moveForward` and `landMovementFactor`, matching Java's
  two uses without changing approximate wander/panic behavior or shared CUDA
  kernels. The branch adds no allocation, global scan, or idle work.
- Added cow axial, pig diagonal, and chicken 90-degree-clamped grounded rows to
  the existing strict live-animal matrix. The new cases first failed solely on
  initiating-parent position/motion bits, then the complete 23-case matrix
  passed twice in 1.38 and 1.40 seconds at 30,252 KB peak RSS at
  `c/magma/trace/out/test_animal_grounded_geometry_final_1.log` and
  `c/magma/trace/out/test_animal_grounded_geometry_final_2.log`. Every earlier
  sheep, live species, chicken lifecycle, and egg-threshold control remains in
  those runs.
- The focused native component passes at
  `c/magma/trace/out/test_animal_grounded_speed_native_final.log`; the
  all-source ASan/UBSan gate passes in 0.95 seconds at 133,624 KB peak RSS at
  `c/magma/trace/out/test_animal_grounded_speed_asan_final.log`. The product
  game builds. The broad aggregate passes in 5:32.58 at 446,068 KB peak RSS,
  zero major faults, and zero swap at
  `c/magma/trace/out/test_runtime_animal_grounded_speed.log` and `.time`.
- With Java stopped, scalar guards pass at 5,038 and 5,164 steps/s against the
  3,858.9 floor at
  `c/magma/trace/out/perf_guard_animal_grounded_speed_cpu_1.json` and
  `c/magma/trace/out/perf_guard_animal_grounded_speed_cpu_2.json`. GPU 1 was
  untouched. Obstacle/gap and multi-tick persistent navigation, accepted lower
  tasks, global cross-store ordering, persistence, and statistics remain open;
  the global effort estimate remains approximately 16%.

## 2026-08-04 (exact cow milking)

- Promoted the adult survival `EntityCow.processInteract` bucket path instead
  of merely preempting an incorrect offhand feed. Main and offhand routing,
  creative/child pass-through, positive breeding cooldown, single-bucket hand
  replacement, stacked-bucket insertion into the first empty main slot, and
  full-inventory item toss now follow the real interaction.
- The real-game oracle exposed a Forge side effect absent from the apparent
  vanilla method body: replacing the destroyed held bucket emits
  `item.armor.equip_generic` after `entity.cow.milk`. Both sounds use the
  player as source at player coordinates. The native event stream now matches
  that order; insertion and toss cases emit only the milk sound.
- Added a fixed-pose detailed fixture that snapshots and restores all 36 main
  slots, offhand, player pose/motion, creative state, player RNG, shared Math
  RNG, global EID, and fixture-owned dropped entities. Four strict cases cover
  empty-slot insertion, full-inventory single-bucket replacement, axial toss,
  and an angled toss with independent seeds. They compare the complete
  inventory, sound list, RNG/EID cursors, and immediate item pose/motion,
  health, age, pickup delay, lifespan, and raw float fields. The existing
  animal interaction matrix grows from 53 to 59 cases and covers adult,
  creative, child, cooldown, main/offhand, and feed-preemption behavior.
- The product route remains one delayed packet kind and runs no new idle scan
  or allocation. A full-inventory toss alone scans the fixed 36-slot main
  inventory and fixed 48-item exact store; the spawned item then uses the
  existing live-item tick. GPU 1 was untouched. When all exact item slots are
  occupied the transition is rejected atomically, and general bucket-family
  stack limits outside this local interaction remain open. The global effort
  estimate remains approximately 16%.
- Acceptance evidence: `test_cow_milking.py` passes four exact detailed
  Java/native cases, and `test_sheep_feed.py` passes its expanded 59-case
  interaction matrix. The focused all-source ASan run passes at 132,468 KB
  peak RSS, the JDK 8 oracle build and product game build pass, and the full
  native runtime suite passes in 5:29.92 at 446,032 KB peak RSS. Two CPU
  throughput samples measured 4,944 and 5,026 steps/s against the 3,858.9
  steps/s floor. The oracle is stopped after capture and GPU 1 was not used.

## 2026-08-04 (exact pig saddle application)

- Promoted the unsaddled `EntityPig.processInteract` saddle boundary instead
  of only swallowing the click. Adult survival and creative pigs, child no-op
  handling, main/offhand client order, delayed integrated-server application,
  exact inventory consumption, and solid-box occlusion are represented.
- The event stream matches the real `entity.pig.saddle` packet at pig
  coordinates, category `neutral`, volume 0.5, and pitch 1. The per-slot saddle
  bit is reset on slot reuse and exposed through narrow fixture hooks; no idle
  scan, allocation, shared CPU/CUDA kernel, or GPU path changed.
- The Java control-flow audit also found that an unnamed pig name tag is
  handled by `EntityPig.processInteract` even when `ItemNameTag` makes no
  mutation. The prior runtime incorrectly passed that main-hand click to an
  offhand carrot; it now preempts the offhand exactly.
- The existing strict interaction matrix first grew from 59 to 64 saddle
  cases, then to 72 with immediate mount association. It passes Java versus
  native in 2.31 seconds at 30,252 KB peak RSS. The mount rows prove empty-hand
  and held-saddle mounting, feed-before-mount, cooldown/in-love fall-through,
  name-tag precedence, sneaking, and already-saddled child behavior. The
  product uses the same delayed packet kind, suppresses independent player
  walking while riding, attaches at the vanilla 0.325 Y offset during live pig
  ticks, and clears the association on later sneak or pig death.
- Focused native and the final all-source ASan gate pass at 132,604 KB peak
  RSS. The mount checkpoint broad aggregate passes in 5:09.03 at 446,380 KB
  peak RSS, 99% CPU, and zero swap. The stopped-oracle CPU guard measures
  5,143 steps/s against the 3,858.9 floor, and the JDK 8 full build passes.
  Pig steering/boost, exact dismount placement, moving ridden-tick promotion,
  saddle visuals/death drop, custom names, and persistence remain separate
  slices. GPU 1 was untouched and the global effort estimate remains
  approximately 16%.

## 2026-08-04 (bounded pig steering and boost)

- Promoted the client-authoritative ridden-pig travel boundary and the
  server-authoritative carrot-on-a-stick use boundary. Either hand can steer;
  rider yaw and half pitch, step height, jump factor, base movement speed,
  raw position/motion, the pig's two limb updates, passenger placement, and
  the no-stick client damping branch now match 1.11.2. Boost start uses the
  pig's private RNG for the exact 140..980 duration and retains vanilla's
  sine curve, post-increment expiry edge, active-boost rejection, creative
  behavior, and damage-18/19 durability boundary.
- The parked Java fixture keeps server item use separate from client vehicle
  travel. Client-world setup runs on Minecraft's client thread, selects a
  genuinely loaded chunk, installs and restores a bounded flat-stone arena,
  and rejects an EmptyChunk rather than treating its unloaded-gravity branch
  as a golden. The server fixture no longer teleports the real server player,
  avoiding client chunk-unload side effects during repeated comparisons.
  `c/magma/trace/test_pig_ride.py` passes all 16 complete Java/native JSON
  transitions with raw float/double bits, inventory, boost state, passenger
  association, and RNG compared exactly.
- Native state remains fixed-capacity and allocation-free: three boost fields
  plus pose/limb scalars are indexed by the existing pig slot, and the mounted
  path is one active-pig branch inside the existing mob tick. Item 398 uses
  the shared unstackable/damageable rules. The playable runtime queues main or
  offhand boost use through its existing one-tick integrated-server boundary;
  focused component/runtime coverage passes with exact RNG, durability,
  delayed mutation, first boost sample, and forward movement.
- The product game and JDK 8 full build pass. The all-source ASan-only gate
  passes in 1.01 seconds at 133,764 KB peak RSS. The broad native aggregate
  passes in 6:31.85 at 446,328 KB peak RSS, 99% CPU, zero major faults, and
  zero swap. The final uncontended scalar guard passes at 4,898 steps/s
  against the 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_ride_cpu_final.json`. GPU 1 was untouched.
- Exact dismount placement, water/obstacle/gap/step travel, persistent
  multi-tick continuation, client boost notification/render state,
  enchanted-stick NBT, saddle visuals/death drop, and vehicle persistence
  remain open. The global effort estimate remains approximately 16%.

## 2026-08-04 (ordered saddled-pig death drop)

- Promoted the ordinary `EntityPig.onDeath` saddle boundary. Java emits the
  base pig loot before the saddle, while the saddle itself is outside the
  `doMobLoot` condition. Native therefore appends one saddle after raw or
  cooked pork, emits the saddle alone when mob loot is disabled, consumes no
  pig-private RNG for it, and retains exact item-constructor Math RNG and EID
  ordering.
- `c/magma/trace/test_pig_death.py` passes five Java/native transitions across
  saddled and unsaddled state, `doMobLoot` on and off, raw and cooked pork,
  ordered item state, entity IDs, and final RNG cursors. The integrated runtime
  also passes a normal gameplay melee death with exact pork-then-saddle order
  and rider cleanup. Fixed-capacity rejection remains atomic when the bounded
  item store cannot hold every resulting drop; Java has no equivalent limit.
- The JDK 8 oracle build and product game build pass. Focused native tests pass,
  and the all-source ASan-only gate passes in 0.88 seconds at 133,772 KB peak
  RSS. The broad aggregate passes in 5:37.78 at 445,476 KB peak RSS, 99% CPU,
  zero major faults, and zero swap. Four affected pig, cow, sheep, and chicken
  falling-anvil oracle cases still match exactly.
- The final stopped-oracle CPU guard passes at 4,619 steps/s against the
  3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_death_cpu_final.json`. GPU 1 was untouched.
  Exact dismount placement and death-time passenger relocation, saddle visuals,
  and persistence remain open. The global effort estimate remains
  approximately 16%.

## 2026-08-04 (bounded exact pig dismount placement)

- Promoted explicit server pig dismount placement for represented full-stone
  and water layouts. The native action-only helper follows Java's fixed nine
  candidate order, repeated final candidate, pig-facing rotation, solid/water
  fallback distinction, and no-epsilon pig-top fallback. It rebuilds the
  rider AABB from the current exact dimensions while preserving motion, look,
  on-ground state, and fall distance, then clears the passenger association.
- The existing strict pig gate grew from 16 to 22 Java/native transitions.
  Flat yaw 0/90, first-candidate obstruction at both facings, water support,
  and total obstruction compare raw player/pig position and AABB bits, ride
  state, EIDs, and unchanged RNG cursors. A proposed full-stone raised fallback
  row was rejected during primary review because Java cannot reach that branch
  from the normal float-widened pig passenger height with that block layout.
- The JDK 8 and product builds pass. Focused native runtime coverage passes,
  and the all-source ASan-only gate passes in 3.30 seconds at 138,376 KB peak
  RSS. The broad aggregate passes in 5:50.59 at 445,192 KB peak RSS, 99% CPU,
  zero major faults, and zero swap.
- The stopped-oracle CPU guard passes at 4,688 steps/s against the 3,858.9
  floor in `c/magma/trace/out/perf_guard_pig_dismount_cpu_final.json`. The
  path is allocation-free and runs only for explicit sneak while riding, so
  it adds no idle-frame work. GPU 1 was untouched. Non-full/state-shaped
  support, the raised branch, and death-time passenger relocation remain open;
  the global effort estimate remains approximately 16%.

## 2026-08-04 (ridden pig death-time lifecycle and terminal relocation)

- Split the ordinary product pig death boundary from terminal retirement.
  Pork then saddle still emit at `onDeath`, but the loaded pig, saddle, and
  represented passenger now remain through `deathTime` 0..19. Dying pigs
  reject repeat damage/drop emission. Time 20 emits terminal XP/particles,
  performs the exact nine-candidate dismount while pig geometry still exists,
  resets fall distance through the represented `EntityLivingBase.updateRidden`
  tail, and only then invalidates the loaded slot. Non-pig death paths are
  unchanged. The hot path adds one pig-only branch and no allocation or scan.
- Added a parked real-Java save-state boundary at health zero/death time 19.
  It invokes `WorldServer.updateEntities`, captures the actual pig then
  recursive-passenger dispatch order, and restores the server player, entity
  lists, tile lists, arena blocks, RNGs, and global EID cursor. Flat and eight-
  neighbor-blocked layouts at yaw 0/90 compare complete player position/AABB,
  motion, look, contact and fall state, removed-pig state/AABB/saddle,
  terminal private RNG, and shared cursors. The existing strict pig matrix
  grows from 22 to 26 and passes in 10.59 seconds at 30,252 KB peak RSS.
- Primary review corrected two delegated assumptions: terminal relocation is
  in the death-time-20 pig tick through passenger recursion, not a later
  player tick, and an all-blocked dismount arena must leave the pig's center
  cell empty while blocking the eight lateral candidates. The prior five-case
  ordered death-drop gate and 33-tick controlled anvil pig loot/XP case still
  pass exactly.
- The focused product regression covers death times 0, 1..19, and 20,
  duplicate-damage rejection, passenger pose retention, terminal AABB rebuild,
  and retirement. The all-source ASan-only gate passes in 6.22 seconds at
  138,172 KB peak RSS. The product and JDK 8 full builds pass. The broad native
  aggregate passes in 5:30.92 at 445,464 KB peak RSS, 99% CPU, zero major
  faults, and zero swap. The stopped-oracle scalar guard passes at 4,760
  steps/s against the 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_death_lifecycle_cpu_final.json`. Java was
  stopped after capture and GPU 1 was untouched.
- The locked passive-death fixture remains the exact loot-construction oracle.
  Ordinary active-combat loot construction still has the older coarse product
  limitations for `doMobLoot`, pork count/cooking, item pose, and global EID
  order. Non-full dismount support, the raised branch, death-window
  gravity/collision outside the stationary NoAI boundary, persistent obstacle
  travel, saddle rendering, and entity persistence remain open. The global
  effort estimate remains approximately 16%.

## 2026-08-05 (ordinary player-melee pig death and moving corpse)

- Promoted the normal player-source `EntityPig.attackEntityFrom` boundary,
  rather than fitting the product to the earlier direct `onDeath` fixture. A
  real normal-AI pig starts saddled, mounted, airborne, and moving; the player
  attacks from one block east. Native now matches health/hurt/recent-hit and
  player-credit state, status 2, the two knockback-resistance RNG checks,
  exact knockback, death sound pitch, private loot RNG, raw-versus-cooked pork
  count, `doMobLoot`, pork-before-saddle construction, status 3, Math RNG, and
  global EIDs.
- The ordinary dying pig now continues the server travel branch after
  `onDeathUpdate`. Gravity, block collision, friction, pose/AABB, and passenger
  placement run in Java order through death times 1..19. Passenger updates
  rebuild the player AABB, zero mounted motion, and reset fall distance without
  incorrectly changing `onGround`. The existing exact time-20 XP, particles,
  dismount, and retirement boundary remains unchanged.
- Added `c/magma/trace/test_pig_lethal.py` and
  `c/magma/game/test_pig_lethal_oracle.c`. Four raw/cooked/no-loot/yaw/seed
  cases compare complete events and raw-bit snapshots at ticks 0, 1, and 19,
  including pig/player/item state, update order, and every RNG/EID cursor.
  All four pass. The prior five-case death-drop and 26-case ride/dismount/death
  matrices remain green, as do the affected pig, cow, sheep, and chicken
  falling-anvil lifecycle cases.
- A repeated legacy matrix initially exposed an oracle isolation race: delayed
  integrated-client `EntityItem` mirrors share Java's process-global EID and
  Math RNG state. The fixture now cancels normal server drop spawning, retains
  the real captured items, and reinserts them into the isolated server world
  without mirror packets. Five repeated legacy matrices and three repeated
  lethal matrices pass after the fix.
- JDK 8 and product builds, focused native tests, `git diff --check`, and the
  all-source ASan gate pass. The broad native aggregate passes in 5:30.77 at
  446,096 KB peak RSS, 99% CPU, zero major faults, and zero swap, effectively
  unchanged from the preceding 5:30.92 / 445,464 KB gate. The stopped-oracle
  scalar guard passes at 4,771 steps/s against the 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_lethal_cpu_final.json`. The exact route is
  event-driven, adds no idle scan or allocation, and does not touch CUDA. Java
  was stopped after capture and GPU 1 was untouched. Non-player-source pig
  hit/loot construction, shaped dismount support, persistent obstacle travel,
  saddle rendering, and persistence remain open. Global effort remains about
  16%.

## 2026-08-05 (state-shaped pig dismount lower supports)

- Promoted the normal-player lower-support distinctions used by pig
  dismount placement. Candidate clearance now reuses the existing exact
  player collision-shape collector over the already-cached runtime chunk
  window, while the support predicate matches vanilla
  `isSideSolid(UP)` for full blocks, slabs, stairs, farmland, snow layers,
  hopper, and redstone block. The path remains action-only, allocation-free,
  and adds no idle scan.
- Six isolated-support Java fixtures distinguish stone, top and bottom stone
  slabs, eight and seven snow layers, and water. Top slab and eight-layer snow
  select the first lower-supported candidate; bottom slab and seven-layer
  snow fall back to the pig top; water uses the separate material fallback.
  The complete strict pig ride/dismount/death matrix grows from 26 to 32 and
  matches raw player/pig positions and AABBs, passenger state, terminal update
  order, inventory, EIDs, and all RNG cursors.
- The JDK 8 and product builds, focused native tests, prior four-case lethal
  trace, and all-source AddressSanitizer matrix pass. The broad native
  aggregate passes in 5:18.30 at 445,524 KB peak RSS, 99% CPU, zero major
  faults, and zero swap. The stopped-oracle scalar guard passes at 5,028
  steps/s against the 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_shaped_dismount_cpu.json`. Java is stopped,
  GPU 1 was untouched, and global effort remains about 16%.
- Broader shaped and dynamic dismount layouts, persistent obstacle travel,
  non-player-source pig damage construction, saddle rendering, and entity
  persistence remain open. The source's raised-support branch is not
  reachable for a normal seated standard player against the tested static
  vanilla UP-solid blocks because those blocks collide with the candidate
  box before support selection.

## 2026-08-05 (persistent ridden-pig obstacle travel)

- Extended the parked client-authoritative pig fixture from isolated ticks to
  persistent 48-tick traces. One-block step, two-block wall, two-cell gap, and
  bottom stone slab keep the same pig, player, arena, boost cursor, and entity
  RNG alive throughout. The complete strict pig ride/dismount/death matrix
  grows from 32 to 36 cases, and two consecutive full comparisons pass. All
  192 new rows match raw pig position/motion/AABB, fall/contact state, pose and
  limbs, passenger state, boost state, and private RNG.
- Fixed the two measured earliest causes. The full-cube trace first differed
  by one fall-distance bit at tick 28 because Java subtracts resolved Y in
  double precision before storing the float. Once that was exact, the bottom
  slab first differed at tick 10 because live mob movement reduced every
  state to the old coarse full-cube list. Active ridden pigs now collect exact
  state-aware collision AABBs from the existing cached chunk window and run
  them through the shared step solver. Fall distance and horizontal/vertical
  collision flags persist per living slot. Ordinary mobs retain the previous
  Pcf path.
- The path stays bounded and allocation-free. A fixed 512-AABB workspace is
  trailing scratch storage in `GmMobLive`; collectors overwrite every used
  entry, so initialization skips its 24 KB without changing observable state.
  The state-aware branch runs only for an actively ridden pig and adds no idle
  scan. The scalar guard passes at 4,884 steps/s against the 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_obstacle_travel_cpu.json`.
- The JDK 8 and product builds, focused mob test, CPU physics/entity-spine
  gates, CUDA compile-only entity-spine gate, and all-source AddressSanitizer
  runs over all four traces pass. The broad native aggregate passes in 5:34.18
  at 445,716 KB peak RSS; the already-built runtime alone passes in 4:53.18 at
  252,968 KB. Both report zero major faults and zero swap. GPU 1 execution was
  untouched while shared by another process.
- This promotes the tested dry full-cube and bottom-slab geometry, not general
  movement media or contact callbacks. Water, webs, ladders, slime, server
  correction/packet timing, saddle rendering, and entity
  persistence remain open. The global effort estimate remains approximately
  16%.

## 2026-08-05 (ridden-pig soul-sand contact)

- Added a persistent six-tick soul-sand-floor ridden-pig trace and an
  otherwise identical stone-floor control. Before the fix, soul sand first
  differed at tick 1 only in raw horizontal motion; raw position, vertical
  motion, geometry/contact state, passenger state, boost state, and private
  RNG matched.
- Java applies `BlockSoulSand.onEntityCollidedWithBlock` after movement
  collision resolution and before final horizontal friction. The active
  ridden path now collects bounded soul-sand cells from the existing cached
  chunk window and applies the 0.4 horizontal multiplier once per overlapping
  cell at that exact boundary. The fixed contact-cell workspace is trailing
  scratch, overwritten on use, allocation-free, and skipped by mob
  initialization. Ordinary mobs and the idle path are unchanged.
- The complete strict ride/dismount/death matrix grows from 36 to 38 cases and
  passes twice. JDK 8, the product build, focused mob/player tests, CPU
  physics/entity-spine, CUDA compile-only entity-spine, all-source
  AddressSanitizer, and the broad native aggregate pass. The broad gate takes
  5:33.74 at 445,864 KB peak RSS, 99% CPU, zero major faults, and zero swap.
  The stopped-oracle scalar guard passes at 4,780 steps/s against the 3,858.9
  floor in `c/magma/trace/out/perf_guard_pig_soul_sand_cpu.json`. GPU 1
  execution was untouched. Water, ladders, slime, other block-contact
  callbacks, server correction/packet timing, saddle rendering, and entity
  persistence remain open. Global effort remains approximately 16%.

## 2026-08-05 (ridden-pig persistent web latch)

- Added a persistent six-tick cobweb-corridor trace and exposed `isInWeb`
  directly on both sides. Java leaves the first overlapping move unscaled,
  sets the latch from the final contracted AABB and resets fall distance, then
  consumes it on the following move with exact 0.25 X/Z and
  0.05000000074505806 Y factors while zeroing stored motion. Continued overlap
  relatches after each move.
- The initial callback/latch candidate failed first at tick 1 in raw position,
  horizontal motion, passenger position, and limb state while the latch
  matched. The exact-AABB movement wrapper bypassed the existing web-consume
  path. It now applies that operation at Java's move boundary. Web and soul
  sand share one tagged fixed contact list in x/y/z callback order, so the
  active-ridden path performs one bounded scan with no heap allocation and no
  idle-path work.
- The complete strict ride/dismount/death matrix grows from 38 to 39 cases and
  passes twice in 19.73 and 20.00 seconds at 30,252 KB harness RSS. JDK 8, the
  product build, focused mob/player gates, CPU physics/entity-spine, CUDA
  compile-only entity-spine, and all-source AddressSanitizer over all seven
  persistent layouts pass. The broad native aggregate passes in 5:18.13 at
  445,732 KB peak RSS, 99% CPU, zero major faults, and zero swap. The
  stopped-oracle scalar guard passes at 4,815 steps/s against the 3,858.9
  floor in `c/magma/trace/out/perf_guard_pig_web_cpu.json`. GPU 1 execution
  was untouched. Water, slime, other block-contact callbacks, server
  correction/packet timing, saddle rendering, and entity persistence remain
  open. Global effort remains approximately 16%.

## 2026-08-05 (ridden-pig north-ladder climb)

- Added paired six-tick clear and north-facing two-high ladder traces. The pig
  starts in a valid non-overlapping state 0.0125 blocks short of the ladder
  plane, and the trace exposes `isOnLadder` on both sides. Before the fix, the
  ladder row first differed at tick 0 only in vertical motion. Java clamps
  living-entity ladder motion before movement and applies a 0.2 upward impulse
  after horizontal collision and before gravity/drag.
- The active-ridden path now finds the exact feet cell in the tagged contact
  list, applies Java's X/Z and descending-Y clamps and fall reset before
  movement, then applies the climb impulse at the measured post-collision
  boundary. No new scratch, heap allocation, idle scan, or ordinary-mob work
  is added. Position, AABB, collision, ladder state, passenger, pose/limbs,
  boost, and RNG all remain in the strict comparison.
- The complete strict ride/dismount/death matrix grows from 39 to 41 cases and
  passes twice in 21.51 and 22.11 seconds at 30,252 KB harness RSS. JDK 8, the
  product build, focused mob/player gates, CPU physics/entity-spine, CUDA
  compile-only entity-spine, and all-source AddressSanitizer over all nine
  persistent layouts pass. The broad native aggregate passes in 5:39.28 at
  445,380 KB peak RSS, 99% CPU, zero major faults, and zero swap. The
  stopped-oracle scalar guard passes at 4,693 steps/s against the 3,858.9
  floor in `c/magma/trace/out/perf_guard_pig_ladder_cpu.json`. GPU 1 execution
  was untouched. Other ladder facings, vines, trapdoor ladders,
  side/top/falling contacts, clamp-limit cases, water, slime, other callbacks,
  server correction/packet timing, saddle rendering, and persistence remain
  open. Global effort remains approximately 16%.

## 2026-08-05 (ridden-pig slime landing bounce)

- Added paired six-tick stone/slime landing traces. Both begin 0.5 blocks
  above the support surface, airborne, with vertical motion -0.6. The stone
  control was exact before the fix. The slime row first differed at tick 0
  only in vertical motion: Java retained `3fdfdc9c5810624e`, while magma
  produced `bfb41205c28f5c29`.
- The active-ridden path now preserves the pre-sweep vertical motion, selects
  the exact block 0.2 below the final feet position from the tagged contact
  list, and applies Java's living slime negation after collision and before
  gravity/drag. Slime's 0.8 slipperiness is selected on the following grounded
  boundary. This adds one contact tag, no state, scratch, allocation, extra
  scan, or idle work.
- The complete strict ride/dismount/death matrix grows from 41 to 43 cases and
  passes twice in 21.77 and 22.19 seconds at 30,252 KB harness RSS. JDK 8, the
  product build, focused mob/player gates, CPU physics/entity-spine, CUDA
  compile-only entity-spine, and all-source AddressSanitizer over all eleven
  persistent layouts pass. The broad native aggregate passes in 5:34.91 at
  445,960 KB peak RSS, 99% CPU, zero major faults, and zero swap. The
  stopped-oracle scalar guard passes at 5,004 steps/s against the 3,858.9
  floor in `c/magma/trace/out/perf_guard_pig_slime_cpu.json`. GPU 1 execution
  was untouched. Slime sneaking/nonliving variants, mixed web/slime contact,
  water, other callbacks, server correction/packet
  timing, saddle rendering, and persistence remain open. Global effort remains
  approximately 16%.

## 2026-08-05 (ridden-pig low-speed slime walking damping)

- Added paired six-tick stone/slime traces starting 0.01 blocks above the
  surface, airborne, with vertical motion -0.05. With bounce already exact,
  the stone control passed and the slime row first differed at tick 1 only in
  horizontal motion. Java Z was `3f7cb9da7117f2aa`, while magma retained
  `3f91b0be16e1b080`; vertical motion matched at `bfa933c36e171b09`.
- The landing helper now applies Java's
  `0.4 + abs(post-onLanded motionY) * 0.2` X/Z multiplier when the final body
  is grounded and vertical speed is below 0.1, before generic contacts,
  gravity, and friction. It adds only arithmetic after the already-selected
  slime contact, with no new state, scratch, scan, allocation, or idle work.
- The complete strict ride/dismount/death matrix grows from 43 to 45 cases and
  passes twice in 22.61 and 23.01 seconds at no more than 30,252 KB harness
  RSS. JDK 8, the product build, focused mob/player gates, CPU
  physics/entity-spine, CUDA compile-only entity-spine, and all-source
  AddressSanitizer over all thirteen persistent layouts pass. The broad native
  aggregate passes in 5:36.20 at 446,292 KB peak RSS, 99% CPU, zero major
  faults, and zero swap. The stopped-oracle scalar guard passes at 4,869
  steps/s against the 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_slime_walk_cpu.json`. GPU 1 execution was
  untouched. Slime sneaking/nonliving and mixed-contact variants, water, other
  callbacks, server correction/packet timing, saddle rendering, and
  persistence remain open. Global effort remains approximately 16%.

## 2026-08-05 (ridden-pig still-water travel)

- Added a six-tick client-controlled pig trace continuously immersed in a
  bounded still-source-water arena, with the existing dry stone trace as its
  control. The trace now exposes `isInWater`. Before the fix, tick 0 used dry
  physics: Java motion Y/Z were `bf947ae147ae147b` /
  `3f90624dd0e56040`, while magma produced `bfb41205c28f5c29` /
  `3f9f7318c2ca56c0` and reported no water contact.
- The active mounted path now reuses the exact entity-level water/current
  probe before client NoAI damping, then applies water acceleration, the exact
  AABB move and block callbacks, 0.8 drag, 0.02 gravity, and the horizontal
  edge-climb branch in 1.11.2 order. This is a bounded active-pig probe with no
  heap allocation, global scan, ordinary-mob work, or idle-path work.
- A repeated dry trace at the oracle's current fixture coordinate exposed an
  older one-ULP AABB reconstruction loss on tick 28. Mounted pigs now retain
  the exact swept AABB across ticks instead of rebuilding it from the center;
  the dry discriminator and the water trace both pass.
- The strict ride/dismount/death matrix grows from 45 to 46 cases and passes
  twice in 24.80 and 24.68 seconds at 30,252 KB harness RSS. JDK 8, product,
  focused mob/player, CPU physics/entity/player, CUDA compile-only
  entity/player, and all-source AddressSanitizer over all fourteen persistent
  layouts pass. The broad native aggregate passes in 5:38.36 at 446,104 KB
  peak RSS, 99% CPU, zero major faults, and zero swap. The stopped-oracle
  scalar guard passes at 4,753 steps/s against the 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_water_cpu.json`. Java was stopped and GPU
  1 execution was untouched. Dry-to-water entry timing, flowing current,
  discriminating horizontal edge climb, lava, splash/sound effects, swim AI,
  and broader entities remain open. Global effort remains approximately 16%.

## 2026-08-05 (ridden-pig dry-to-still-water entry)

- Added a four-tick trace that starts a mounted pig 0.05 blocks short of one
  still water source. The first failure was not motion: both sides completed
  the dry travel branch. Java then reported `isInWater=true` and ended its
  pig-private RNG cursor at 75,376,161,696,822, while magma remained dry and
  ended at 27,500,032,739,863.
- Oracle source and the raw state prove the boundary. During `Entity.move`,
  `EntityLivingBase.updateFallState` re-runs `handleWaterMovement` after the
  dry AABB move. Entry resets fall distance and calls `Entity.resetHeight`,
  whose sound-pitch, bubble, and splash loops consume 97 random floats for an
  adult pig. The active-ridden path now performs that bounded post-move water
  probe and advances the exact width-dependent RNG footprint. Rendered sound
  and particles remain outside this promoted slice.
- The complete ride/dismount/death matrix grows from 46 to 47 cases and passes
  twice; the timed pass takes 23.65 seconds at 30,252 KB harness RSS. JDK 8,
  product, focused mob, and all-source AddressSanitizer over all fifteen
  persistent layouts pass. The broad native aggregate passes in 5:35.01 at
  446,124 KB peak RSS, 99% CPU, zero major faults, and zero swap. The
  stopped-oracle scalar guard passes at 4,870 steps/s against the 3,858.9
  floor in `c/magma/trace/out/perf_guard_pig_water_entry_cpu.json`. The added
  probe has no heap allocation or global scan and runs only for an active
  steerable ridden pig. GPU 1 execution was untouched. Flowing-current entry,
  falling entry/fall-distance ordering, horizontal edge-climb coverage, lava,
  emitted splash/sound events, swim AI, and broader entities remain open.

## 2026-08-05 (ridden-pig flowing and falling water entry)

- Added a perpendicular-current entry row. A level-0 source at the dry entry
  cell has one adjacent level-1 flowing-water cell, producing a pure +X
  current without changing forward travel. The first mismatch was tick-0
  motion X only: Java `3f7f4f50dd2f1aa0`, magma `3f8cac083126e979`.
  Native's first still-water hook added 0.014 after the completed land tail;
  Java applies it from `EntityLivingBase.updateFallState` after the AABB move
  but before callbacks, gravity, and horizontal drag.
- Added a host-only active-steerable land helper that splits exact AABB
  resolution from fall bookkeeping at that interposition point. It then runs
  the existing liquid/current probe, resolved-displacement fall update,
  landing and block callbacks, ladder impulse, gravity, and land drag in Java
  order. It reuses the fixed collision/contact scratch and does not alter the
  shared CPU/CUDA headers, ordinary mobs, or idle path.
- The first falling fixture was rejected after inspection because its initial
  contracted AABB was already wet. The corrected row starts at Y=221.0 with
  motion Y=-1.0 over a source and floor opening. It is dry at base-tick time,
  enters during the 0.98-block move, consumes the 97 splash draws, and matches
  fall-distance bits `3f7ae148`. This proves water resets prior fall distance
  before `Entity.updateFallState` adds the resolved descent.
- The complete ride/dismount/death matrix grows from 47 to 49 cases and passes
  twice; the corrected timed pass takes 26.18 seconds at 30,252 KB harness
  RSS. JDK 8, product, focused mob, and corrected all-source AddressSanitizer
  over all seventeen persistent layouts pass. The broad native aggregate
  passes in 5:38.70 at 446,100 KB peak RSS, 99% CPU, zero major faults, and
  zero swap. The stopped-oracle scalar guard passes at 4,927 steps/s against
  the 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_water_flow_entry_cpu.json`. GPU 1 execution
  was untouched. Broader current gradients, falling-water metadata/downward
  currents, horizontal edge climb, emitted splash/sound events, lava, swim AI,
  and broader entities remain open. Global effort remains approximately 16%.

## 2026-08-05 (ridden-pig water edge climb and broad-gate repair)

- Added one-tick `water_edge_climb` and `water_edge_blocked` Java/native
  layouts. Both collide with the same wall while off ground. The dry
  destination takes Java's water edge-climb replacement and matches motion Y
  bits `3fd3333340000000` (`0.30000001192092896`); water in the destination
  suppresses the branch and matches `3fdcd35a8d0e5603`. A first grounded
  candidate was rejected because step-height retry avoided the wall and did
  not exercise the claimed branch.
- The complete ride/dismount/death matrix grows from 49 to 51 cases and passes
  twice in 28.34 and 27.59 seconds at 47,520 and 30,252 KB harness RSS. JDK 8,
  product, focused mob, and nineteen-layout all-source AddressSanitizer gates
  pass. The stopped-oracle scalar guard passes at 5,076 steps/s against the
  3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_water_edge_cpu.json`. GPU 1 was untouched.
- The strict aggregate exposed a pre-existing item-spawn loss. Java assigns
  zero to its first process-global entity ID, but `gm_live_spawn_item_exact`
  rejected `eid <= 0` after the world and Math cursors advanced. Native now
  rejects only negative IDs, and `test_play_compose` locks a stationary exact
  item with ID zero. Script-route expectations now reflect the restored first
  log, exact inventory fields, and EntityItem health.
- Repaired the broad test harness rather than bypassing failures. Runtime
  scripts linking `runtime.o` now also link `nbt_blob.o`; the reed fixture has
  required adjacent water; boat pressure plates have supporting blocks; the
  route sends physical combat click edges, preserves the one-tick integrated
  server delay, and waits through hurt resistance. Its End phase naturally
  regenerates health, attacks crystals behind pillar caps, and places a
  carried blast-cover block for the bed explosion.
- `make -C c/magma test-game` passes in 6:43.01 at 675,952 KB peak RSS, three
  major faults, zero swap, and includes a passing fresh-spawn-through-credits
  survival route. Broader gradients, falling-water metadata/downward
  currents, emitted splash/sound events, lava, swim AI, and broader entities
  remain open. Global effort remains approximately 16%.

## 2026-08-05 (ridden-pig lava travel and authoritative contact)

- Added client trace layouts for six ticks of still-lava travel, four ticks of
  dry-to-lava entry, positive and blocked one-tick horizontal edge climb, and
  a water/lava overlap control. Native now uses the exact lava movement branch:
  0.02F acceleration, AABB collision/callback ordering, 0.5 drag on all axes,
  0.02 gravity, and Java's exact liquid edge-climb replacement. Water retains
  travel precedence while both raw material predicates remain observable.
- Added a separate parked-server contact fixture with a real
  `EntityPlayerMP` passenger. The server passenger cannot steer the vehicle,
  so the fixture measures authoritative base-tick contact rather than falsely
  treating the parked server copy as client movement. Dry and sustained
  12-tick rows match fall distance, `isInLava`, health, fire, hurt timers, last
  damage, alive state, pig-private RNG, and global Math RNG. The sustained row
  crosses the tick-10 fresh-hit boundary and locks ON_FIRE-before-lava order.
- Native now retains distinct server fall distance, lava predicate, living
  sound timer, and pig RNG for the mounted server copy. Fresh environment
  damage matches `setBeenAttacked`'s `nextDouble`, Math yaw draw, two hurt-sound
  floats, ambient `nextInt(1000)`, health/hurt-resistance arbitration, and the
  300-tick lava fire floor without advancing the client movement RNG.
- The complete ride/dismount/death matrix grows from 51 to 58 strict cases and
  passes twice; the timed pass takes 31.79 seconds at 30,252 KB harness RSS.
  JDK 8, product, focused mob, and 26-layout all-source AddressSanitizer gates
  pass; the sanitizer run takes 40.59 seconds at 96,424 KB. The broad native
  aggregate passes in 6:40.42 at 675,992 KB peak RSS and includes the complete
  fresh-spawn-through-credits route. The stopped-oracle scalar guard remains
  5,076 steps/s against the 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_lava_cpu.json`. GPU 1 was untouched.
  Flowing-lava metadata, packet-stage cactus/fire contact, fire resistance and
  wet extinguish, contact death/drop, emitted effects, swim AI, and broader
  entities remain open. Global effort remains approximately 16%.

## 2026-08-05 (ridden-pig vehicle-packet cactus and fire contact)

- Added an isolated server fixture that invokes the real
  `NetHandlerPlayServer.processVehicleMove` on a mounted pig, then executes the
  same pig's ordinary base tick. The pig is deliberately unspawned but remains
  the player's actual riding entity: the server accepts the packet, while the
  isolated loaded-entity list cannot send passenger synchronization that would
  make the client emit extra vehicle packets.
- The integrated client can advance Java's process-global Math RNG during a
  scheduled server action. Each packet action therefore restores its saved
  packet cursor and captures it immediately after packet plus pig update. Dry,
  cactus, and fire traces are stable across repeats and match for 12 ticks
  through health, fire, fall distance, hurt and hurt-resistance timers, last
  damage, alive state, pig-private RNG, and Math RNG.
- Added the corresponding cold native packet-contact action. The grounded
  packet's `-1e-6` move clears only the authoritative server fall ledger;
  cactus callback damage runs before generic in-fire damage, and the fire
  counter then enters the ordinary server base-tick phase. No scan, allocation,
  or extra work was added to active travel, ordinary mobs, or idle simulation.
- The complete matrix grows from 58 to 61 cases and passes twice in 31.01 and
  30.84 seconds at 30,252 KB harness RSS. JDK 8, product, focused mob, and
  29-layout all-source AddressSanitizer gates pass; the sanitizer run takes
  43.38 seconds at 96,480 KB. The full native aggregate passes in 6:57.37 at
  676,000 KB peak RSS and includes the fresh-spawn-through-credits route. The
  stopped-oracle scalar guard passes at 5,008 steps/s against the 3,858.9 floor
  in `c/magma/trace/out/perf_guard_pig_packet_contact_cpu.json`. GPU 1 was
  untouched. Automatic runtime packet contact integration, combined
  cactus/fire ordering, wet extinguish, fire resistance, packet lava, contact
  death/drop, emitted effects, and broader entities remain open. Global effort
  remains approximately 16%.

## 2026-08-05 (combined packet contact and wet extinguish)

- Added a combined cactus-plus-fire ordering row. The valid fixture places
  cactus and supported fire side by side with the pig AABB spanning both cells.
  Cactus is the sole accepted tick-zero hit and RNG consumer. The following
  generic in-fire damage is rejected by fresh hurt resistance, but still starts
  the fire counter. A first fire-above-cactus candidate was discarded after
  Java showed that it did not preserve a valid callback overlap.
- Added a two-tick wet packet row. It establishes real still-water state with
  `handleWaterMovement`, then sets a 100-tick burning precondition before the
  packet. Java consumes exactly two pig floats for the packet-move extinguish
  sound. The following ordinary base tick reports `isInWater=true`, clears fall
  distance, normalizes fire to zero, and prevents ON_FIRE damage.
- Native now retains the authoritative server water predicate and follows the
  same water-before-fire-before-lava base-tick order. The packet action handles
  cactus, generic in-fire, and wet cleanup in source order. The added water
  probe is limited to the actively ridden server pig, reuses the fixed chunk
  window, performs no allocation or global scan, and adds no ordinary-mob or
  idle-path work.
- The complete matrix grows from 61 to 63 cases and passes twice in 33.21 and
  34.00 seconds at 30,252 KB harness RSS. JDK 8, product, focused mob, and
  31-layout all-source AddressSanitizer gates pass; the sanitizer run takes
  45.11 seconds at 95,084 KB. The full native aggregate passes in 6:55.31 at
  675,992 KB peak RSS and includes the fresh-spawn-through-credits route. The
  stopped-oracle scalar guard passes at 5,141 steps/s against the 3,858.9 floor
  in `c/magma/trace/out/perf_guard_pig_packet_wet_cpu.json`. GPU 1 was
  untouched. Automatic runtime packet contact dispatch, fire resistance,
  packet lava, contact death/drop, emitted effects, and broader entities remain
  open. Global effort remains approximately 16%.

## 2026-08-05 (ridden-pig server Fire Resistance)

- Added real hidden-particle Fire Resistance to the authoritative Java pig
  contact fixtures and exposed exact remaining duration in every contact row.
  A duration-two packet-fire case blocks packet IN_FIRE and the same tick's
  ON_FIRE pulse for two updates, then accepts the first hit after expiry. A
  duration-one lava case blocks damage while still setting 300 fire ticks and
  halving fall distance, then resumes ordinary damage on the next update. A
  combined cactus/fire control proves the active effect does not suppress the
  preceding non-fire callback.
- Added a server-only duration to the represented mounted-pig native state.
  The fire damage path reuses `pec_fire_resist_blocks` before hurt arbitration,
  events, health, and RNG. Ignition and lava fall handling remain outside that
  rejection. Duration decrements after the current server fire/lava phase and
  the new check runs only in already-active ridden contact branches, with no
  ordinary-mob scan or allocation.
- The complete matrix grows from 63 to 66 cases and passes twice in 33.26 and
  33.96 seconds at 30,252 KB harness RSS. JDK 8, product, focused mob, and
  34-layout all-source AddressSanitizer gates pass; the sanitizer run takes
  47.81 seconds at 95,068 KB. The full native aggregate passes in 6:51.39 at
  675,956 KB peak RSS and includes the fresh-spawn-through-credits route. The
  stopped-oracle scalar guard passes at 5,126 steps/s against the 3,858.9 floor
  in `c/magma/trace/out/perf_guard_pig_fire_resistance_cpu.json`. GPU 1 was
  untouched. Automatic runtime packet contact dispatch, general mob
  potion/effect storage, packet lava, contact death/drop, emitted effects, and
  broader entities remain open. Global effort remains approximately 16%.

## 2026-08-05 (ridden-pig packet-stage lava)

- Strengthened every real vehicle-packet contact trace with an immediate
  post-packet snapshot in addition to the existing post-base state. This
  exposes the otherwise-hidden IN_FIRE phase that `Entity.move` applies when
  the mounted pig's packet move intersects lava.
- Added a twelve-tick normal packet-lava row and a two-tick Fire Resistance
  expiry row. The normal packet first reaches health 9, fire 160, and
  lastDamage 1, then the ordinary base phase finishes at health 6, fire 300,
  and lastDamage 4. The resistance row proves rejected damage still preserves
  ignition and lava fall handling before normal damage resumes after expiry.
- Renamed the native packet input to generic flammable contact and feeds both
  fire and lava through that cold action boundary. No persistent product
  state, allocation, global scan, ordinary-mob work, or idle-path work was
  added.
- The complete matrix grows from 66 to 68 cases and passes twice in 32.13 and
  32.35 seconds at 30,252 KB harness RSS. JDK 8, product, focused mob, and
  36-layout all-source AddressSanitizer gates pass; the sanitizer run takes
  46.21 seconds at 95,072 KB. The immediately preceding unchanged-product
  broad aggregate passes in 6:51.39 at 675,956 KB peak RSS. The stopped-oracle
  scalar guard passes at 5,088 steps/s against the 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_packet_lava_cpu.json`. GPU 1 was untouched.
  Automatic runtime packet-contact dispatch, general mob effect storage,
  contact death/drop, emitted effects, and broader entities remain open.
  Global effort remains approximately 16%.

## 2026-08-05 (automatic ridden-pig shared-pose packet dispatch)

- Integrated the represented mounted-pig vehicle packet into the public
  runtime. Active carrot-stick travel queues the mounted EID, and the server
  consumes it immediately after the ordinary player packet, before player
  timers, hazards, and controlled-living updates. The handler derives cactus,
  flammable, and wet contact from the exact mounted-pig AABB and authoritative
  server water state, then reuses the verified packet-contact transition.
- Added an automatic cold checkpoint and nine runtime-only comparison rows.
  Their native fixture advances only through `gm_runtime_tick`; it never calls
  the packet helper directly. Position and AABB comparison found a tick-zero
  fixture contamination: a real server `EntityPlayerMP` passenger cannot
  steer, while native had advanced its authoritative pig by 0.05625 blocks.
  Clearing the stick for that measured server phase fixes the earliest state
  rather than masking later contact differences.
- The complete 77-case Java/native matrix passes twice in 36.15 and 36.29
  seconds at 30,252 KB harness RSS. The all-source AddressSanitizer build runs
  the same 77 cases in 139.48 seconds at 100,880 KB. JDK 8, product, and the
  focused mob gate pass. The broad game aggregate, including the complete
  fresh-spawn-through-credits route, passes in 488.59 seconds at 676,552 KB
  peak RSS. With the oracle stopped, scalar throughput passes at 5,061 steps/s
  against the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_runtime_packet_cpu.json`. GPU 1 was
  untouched.
- The promoted queue intentionally carries identity against the runtime's
  shared accepted pose. Arbitrary vehicle target deltas, collision rollback,
  speed and wrong-move validation, correction packets, and independent
  client/server vehicle poses remain open. Global effort remains approximately
  16%.

## 2026-08-05 (bounded dry ridden-pig vehicle movement and correction)

- Added a real `NetHandlerPlayServer.processVehicleMove` oracle fixture that
  captures the mounted pig and both hidden tracker triplets immediately after
  one moving packet. Open-space acceptance, two-high-wall rollback, and the
  greater-than-100 speed rejection distinguish the accepted, wrong-move, and
  speed branches without relying on inferred final state.
- Added the corresponding cold native transition. It reproduces the
  `-1e-6` move, old/target 0.0625 contractions, X/Z residual threshold,
  incoming-rotation collision rollback, old-rotation speed correction, and
  accepted tracker advancement. Exact-filtering broadphase block candidates
  fixed the first wall mismatch, where below-floor candidates had made a
  collision-free contracted AABB appear occupied.
- The complete 80-case Java/native matrix passes twice in 38.62 and 38.60
  seconds at 30,252 KB harness RSS. The all-source AddressSanitizer and
  LeakSanitizer run passes in 151.36 seconds at 100,900 KB. JDK 8, product,
  focused mob, and broad game gates pass; the broad gate takes 633.66 seconds
  at 676,164 KB while the oracle is concurrently using about eight CPU cores.
  The stopped-oracle scalar guard passes at 5,140 steps/s against the 4,062
  baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_vehicle_move_cpu.json`. GPU 1 was
  untouched.
- This slice is deliberately dry, cold, same-height, and single-server-body.
  Independent runtime client/server poses, persistent tracker state, moving
  block/liquid callbacks, vertical packets, and end-to-end correction delivery
  remain open. Global effort remains approximately 16%.

## 2026-08-05 (runtime independent ridden-pig vehicle packet)

- Replaced the playable pig packet's identity-only fields with the complete
  client target pose and added an independent authoritative pig body outside
  the client EwStore ping-pong copies. Mount initializes that body; dismount,
  drop, and terminal retirement invalidate it. The runtime consumes a pending
  packet before ordinary timers and controlled-living work without mutating
  the client pose or AABB at the immediate packet checkpoint.
- Added three runtime composition rows. The accepted row obtains its nonzero
  target from a real client `gm_runtime_tick`, restores the pre-emission server
  snapshot, retains the actual queued payload, and consumes it on the next
  tick. Wall and greater-than-100 corrections are explicitly labeled injected
  packets. Their server packet state is compared with the real Java handler;
  this verifies native runtime plumbing at that boundary, not a Java
  integrated-client-to-server end-to-end route.
- Acceptance review caught an unsafe generalization before promotion: moving
  callbacks cannot reuse the shared-pose contact helper. Java callbacks observe
  the collision-resolved temporary server AABB before rollback, while
  `EntityLivingBase.updateFallState` owns asymmetric water refresh there.
  Runtime now preserves the verified exact zero-delta contact
  path but gates nonzero contacts until that resolved-body composition exists.
- The complete 83-case matrix passes twice in 44.17 and 43.22 seconds, with
  30,252 KB steady-state harness RSS. A clean-oracle all-source AddressSanitizer
  and LeakSanitizer run passes in 159.34 seconds at 102,288 KB. JDK 8, product,
  focused mob, and broad game gates pass; the broad fresh-spawn-through-credits
  aggregate takes 417.74 seconds at 675,980 KB. Stopped-oracle throughput is
  5,132 steps/s against the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_runtime_vehicle_move_cpu.json`. GPU 1 was
  untouched.
- One third-run failure in a long-lived Java oracle disappeared for both native
  binaries in isolation and on a clean oracle; the clean full ASan run passed.
  Resetting all integrated-server fixture side effects across repeated complete
  matrices remains harness work. The next product boundary is authoritative
  server base-tick advancement plus resolved moving contact and correction
  delivery. Global effort remains approximately 16%.

## 2026-08-05 (runtime ridden-pig authoritative dry base state)

- Advanced the represented mounted pig's independent authoritative body after
  runtime vehicle-packet handling. Liquid predicates now probe the server AABB,
  environment events use server coordinates, and the steerable
  `EntityPlayerMP` branch copies rider yaw/pitch while zeroing server XYZ
  motion. Existing shared health/timers and the server-private RNG retain their
  established order. The stationary packet move now clears both the legacy and
  independent server fall ledgers before callbacks.
- Strengthened all three runtime moving-packet rows with an exact post-base
  server snapshot in addition to the immutable immediate handler checkpoint.
  The fixture retains the carrot-on-a-stick through the server phase, then
  distinguishes the consumed packet from the naturally queued next client
  packet. Raw pose/AABB, motion, rotation, on-ground/fall state, tracker
  triplets, liquid/combat/effect fields, and both RNG cursors match Java for
  accepted, wall-corrected, and speed-corrected packets. The older direct
  one-body seam is explicitly isolated from the runtime dual-body shadow.
- The complete 83-case matrix passes twice in 43.64 and 44.49 seconds at
  30,252 KB RSS. A clean-oracle all-source ASan/UBSan/LSan run passes in 176.78
  seconds at 124,084 KB. JDK 8, product, focused mob, and broad game gates pass;
  the broad fresh-spawn-through-credits aggregate takes 414.01 seconds at
  676,440 KB, one major fault, and zero swap. Stopped-oracle throughput is
  5,182 steps/s against the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_server_base_cpu.json`. GPU 1 was untouched.
  This promotion remains dry and one-packet. Divergent dual-pose water/lava
  base ticks, complete previous/render/limb bookkeeping, resolved moving
  callbacks, vertical and same-epoch multiple packets, and correction
  delivery/application remain open. Global effort remains approximately 16%.

## 2026-08-05 (resolved first-packet ridden-pig moving contacts)

- Moved authoritative vehicle-packet block and flammable callbacks inside the
  collision-resolved temporary server move, before acceptance or XYZ rollback.
  The same factored scan, contact transition, and checkpoint now covers
  stationary and moving packets without mutating the client EwStore. Accepted
  fire/lava moves and corrected cactus contact match Java; fire and cactus
  placed beyond an intervening wall remain untouched, proving the runtime does
  not scan the raw packet target.
- Locked `EntityLivingBase.updateFallState`'s asymmetric water behavior. A dry
  first packet entering water immediately sets `inWater`, clears fall/fire,
  and affects same-move wet contact. A wet packet leaving water retains that
  flag until the following base update. The fresh Java fixture still has
  `firstUpdate=true`, so later-update resetHeight splash RNG remains explicitly
  open for the next two-packet row.
- The complete 90-case Java/native matrix passes twice in 47.24 and 47.76
  seconds at 30,252 KB RSS. The all-source ASan/UBSan/LSan matrix passes in
  201.10 seconds at 124,100 KB. One preceding attempt hit the known Java
  packet-lava Fire Resistance contamination without a sanitizer diagnostic;
  the exact row and complete repeat passed. JDK 8, product, focused mob, and
  broad fresh-spawn-through-credits gates pass; the broad gate takes 415.64
  seconds at 676,204 KB with zero major faults and zero swap. Stopped-oracle
  throughput is 4,951 steps/s against the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_moving_contact_cpu.json`. GPU 1 was
  untouched. Global effort remains approximately 16%.

## 2026-08-05 (persistent ridden-pig packet epochs and water-entry RNG)

- Promoted the Java `NetHandlerPlayServer` vehicle tracker lifecycle. Multiple
  accepted packets in one network epoch retain the original `lowestRidden`
  speed origin and advance only `lowestRidden1`; the represented end-of-tick
  server boundary then re-seeds both baselines from the authoritative pig.
  The exact fixture includes two same-epoch packets and a cross-epoch 10.1
  absolute target that would false-correct against a stale mount origin.
- Added construction-correct `Entity.firstUpdate` state to the authoritative
  vehicle shadow. Mount derives it from the pig's represented entity age rather
  than re-arming it. A fresh entry remains the zero-draw control; later mounted
  entry and a pre-ticked saved-state mount both consume the exact adult-pig
  resetHeight sequence on the server private RNG: 2 pitch draws, 19 bubble
  triples, and 19 splash pairs, or 97 `nextFloat` transitions. The measured
  immediate packet seed is exactly Java LCG^97 of the pre-packet seed, with no
  Math RNG movement.
- The final 94-case Java/native matrix passes in 53.98 seconds at 30,252 KB
  after the preceding 93-case matrix passed twice in 49.14 and 49.07 seconds.
  A clean-oracle all-source ASan/UBSan/LSan matrix passes all 94 cases in
  201.61 seconds at 119,344 KB. JDK 8, product, focused mob, and broad game
  gates pass; the broad fresh-spawn-through-credits aggregate takes 408.88
  seconds at 675,984 KB with zero major faults and zero swap. Stopped-oracle
  throughput is 5,049 steps/s against the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_packet_chain_cpu.json`. GPU 1 was
  untouched. Vertical packets, higher-count/mixed-result bursts, correction
  delivery/application, and full authoritative bookkeeping remain open.
  Global effort remains approximately 16%.

## 2026-08-05 (vertical ridden-pig vehicle packets)

- Removed the two native same-Y admission guards after source review confirmed
  that the existing packet mover already implements Java's vertical delta,
  `-1e-6` movement bias, collision resolution, target placement, and rollback
  order. The wrong-movement norm remains horizontal-only: the decompiled Java
  finite-Y residual condition clears every finite vertical residual.
- Added exact direct and integrated-runtime rows for accepted upward movement,
  floor-target correction, and ceiling-target correction. A two-packet
  same-network-epoch ascent additionally verifies the independent vertical
  tracker: `lowestRiddenY1` advances after each acceptance while
  `lowestRiddenY` stays at the epoch origin.
- The complete 101-case Java/native matrix passes twice in 54.84 and 54.75
  seconds at 30,252 KB. The all-source ASan/UBSan/LSan matrix passes in 250.99
  seconds at 124,340 KB. JDK 8, product, focused mob, and broad
  fresh-spawn-through-credits gates pass; the broad aggregate takes 429.19
  seconds at 675,996 KB with zero major faults and zero swap. Stopped-oracle
  throughput is 4,961 steps/s against the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_vertical_packet_cpu.json`. GPU 1 was
  untouched. Higher-count/mixed-result and long mixed-axis packet bursts,
  correction delivery/application, and full authoritative bookkeeping remain
  open. Global effort remains approximately 16%.

## 2026-08-05 (mixed vehicle burst and client correction delivery)

- Added a four-packet same-network-epoch discriminator with accept, collision
  rollback, speed rollback, then accept. Java and native retain the original
  primary vehicle tracker through all four packets, advance only the secondary
  triplet on acceptance, and preserve the distinct correction rotations.
- Added a real client-side `NetHandlerPlayClient.handleMoveVehicle` oracle.
  Its outbound capture swallows only the immediate acknowledgement, leaving
  the parked integrated server untouched. Collision- and speed-style rows
  prove bit-exact corrected pose/AABB and acknowledgement payload, preserved
  motion and on-ground state, and the unchanged passenger position at the
  callback boundary. Runtime applies that correction to the client pig and
  carries the acknowledgement separately from the subsequent prediction in a
  fixed two-entry FIFO; both drain in the next represented server epoch.
- The complete 104-case Java/native matrix passes twice in 55.45 and 55.83
  seconds at 30,252 KB. One intervening long-session dry-contact fixture
  failure passed immediately in isolation and after a clean oracle restart.
  The all-source ASan/UBSan/LSan matrix passes in 242.12 seconds at 123,988 KB.
  JDK 8, product, focused mob, and broad fresh-spawn-through-credits gates
  pass; the broad aggregate takes 431.72 seconds at 675,992 KB with zero major
  faults and zero swap. Stopped-oracle throughput is 4,909 steps/s against the
  4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_pig_correction_delivery_cpu.json`. GPU 1 was
  untouched. Long mixed-axis and repeated-correction bursts, broader packet
  scheduling, and full authoritative bookkeeping remain open. Global effort
  remains approximately 16%.

## 2026-08-05 (splash and lingering potion mob effects)

- Ported the exact instant-health/harm arithmetic, including undead reversal
  and Java's rounded splash factor, into the shared CPU/CUDA core. The
  `entity_random` Java/CPU oracle now passes all 39 rows; the CUDA translation
  unit compiles with CUDA 12.8 while GPU 1 remains untouched.
- Splash potions now target represented living mobs with direct-hit factor
  1.0 and distance falloff `1-sqrt(distanceSquared)/4`. Instant healing and
  harming share the existing mob hurt-immunity, death, drops, and aggro path;
  water bottles deal the vanilla one point of damage to blazes and endermen.
- Lingering instant potions now track independent five-tick reapplication
  deadlines per mob EID as well as the player deadline, including the global
  radius-on-use shrink. The focused brewing/runtime suite passes 160 checks,
  including two independently affected cows and undead reversal.
- Focused mob and route gates pass. A clean `make -j1 test-game` passes from
  fresh spawn through credits in 431.36 seconds at 676,196 KB peak RSS, with
  zero major faults and zero swap. Stopped-oracle throughput is 5,007 steps/s
  against the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_brewing_mob_cloud_cpu.json`. Remaining potion
  gaps include general non-instant mob effect storage, self-return player
  collision, cloud particles, custom NBT, and projectile/cloud persistence.
  Global effort remains approximately 16%.

## 2026-08-05 (controlled potion and cloud state continuation)

- Added cold runtime and JSONL fixture hooks for an in-flight splash/lingering
  potion and an active area-effect cloud. Potion fixtures retain item/type,
  EID, age, position, and motion. Cloud fixtures retain potion type, EID,
  position, age, duration, wait/reapplication delays, radius, radius-on-use,
  radius-per-tick, and the represented player deadline.
- State output now exposes potion age and every represented cloud lifecycle
  scalar. Strict native and script regressions resume an age-7 throwable
  through its exact next position/drag/gravity transition and an age-9 cloud
  through the age-10 scan, exact float radius, deadline 30, and quarter-duration
  Speed II application. The focused suite passes 173 checks.
- Script, mob, fresh-spawn-through-credits, and long runtime gates pass. Their
  parallel wall times were 4.78, 6.83, 33.80, and 336.37 seconds; the long
  runtime gate peaked at 446,624 KB RSS. Stopped-oracle throughput is 4,959
  steps/s against the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_potion_state_fixtures_cpu.json`. These cold
  hooks add no ordinary tick work. Automatic Java-capsule translation, full
  entity NBT/custom effects, and saved reapplication-map reconstruction remain
  open. GPU 1 was untouched. Global effort remains approximately 16%.

## 2026-08-05 (persistent splash and lingering mob status effects)

- Added a bounded per-living-mob status-effect store with vanilla
  `PotionEffect.combine` amplifier/duration precedence, exact pre-decrement
  cadence, expiry, and observable state output. Splash delivery uses rounded
  distance-scaled duration and lingering delivery uses quarter duration plus
  the existing independent per-EID reapplication deadline.
- Promoted live regeneration, poison, undead regeneration/poison rejection,
  and duration-one fire resistance. Periodic damage shares the represented
  hurt-resistance path, poison preserves one health, and regeneration respects
  each represented type's maximum health. Attribute/world/render behaviors for
  the other stored effects remain open rather than being silently claimed.
- The Java/CPU arithmetic oracle passes 47 values and the CUDA 12.8 translation
  unit compiles for `sm_120` without running GPU 1. A clean build and the
  217-check focused suite pass. Script, mob, route, and long runtime gates pass
  in 4.95, 6.92, 34.06, and 338.03 seconds; the long gate peaks at 447,056 KB
  RSS. CPU throughput is 5,099 steps/s against the 4,062 baseline and 3,858.9
  floor in `c/magma/trace/out/perf_guard_brewing_mob_status_cpu.json`. Global
  effort remains approximately 16%.

## 2026-08-05 (mob potion movement, melee, and jump attributes)

- Promoted five stored mob effects through ordinary live AI: Speed and
  Slowness apply the exact operation-2 movement modifiers, Strength and
  Weakness apply exact clamped operation-0 attack damage, and Jump Boost adds
  the Java float impulse before gravity and drag. Effect expiry precedes AI
  travel and attack, so a duration-one attribute effect does not leak into the
  next movement boundary.
- The focused live suite now passes 248 checks. It distinguishes faster and
  slower one-tick zombie chase, exact duration-one expiry, six/zero/two-point
  Strength/Weakness zombie hits, and Jump Boost II's exact
  `0.5292000003695486` post-travel vertical motion. The Java/CPU shared-math
  oracle passes 58 values; all four affected CUDA 12.8 translation units
  compile for `sm_120` without executing GPU 1.
- Mob and route gates pass in 6.15 and 32.57 seconds. A clean
  `make -j1 test-game` passes in 438.52 seconds at 676,224 KB peak RSS, zero
  major faults, and zero swap. CPU throughput remains 4,955 steps/s against
  the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_brewing_mob_attributes_cpu.json`. Global effort
  remains approximately 16%.

## 2026-08-05 (mob potion Resistance and Wither damage)

- Added exact Resistance reduction after raw hurt-immunity acceptance. The
  represented general melee, explosion, direct fire, falling-anvil, small
  fireball, area-damage, instant-potion, and periodic-magic routes share it;
  amplifier V and above reduce accepted damage to zero without bypassing the
  normal accepted-hit bookkeeping.
- Promoted Wither through the exact pre-decrement cadence and shared periodic
  magic-damage path. Lethal pulses now retire ordinary mobs through their
  existing drop path; a focused cow case proves both represented item drops.
  The focused live suite passes 286 checks, including Resistance expiry,
  Resistance plus Wither, and controlled and ordinary lethal Wither.
- The Java/CPU arithmetic oracle passes 61 values and the CUDA 12.8 target
  compiles for `sm_120` without executing GPU 1. Mob, route, and long runtime
  gates pass in 6.46, 32.98, and 329.81 seconds; the long gate peaks at 446,580
  KB RSS. The final 286-check all-source focused ASan/UBSan/LSan gate passes in
  3.42 seconds at 136,724 KB. CPU throughput is 4,938 steps/s against the 4,062
  baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_brewing_mob_resistance_wither_cpu.json`.
  Global effort remains approximately 16%.

## 2026-08-05 (mob Health Boost, Absorption, and Levitation)

- Promoted three more stored effects. Health Boost changes the live maximum
  health used by regeneration and instant healing, then clamps current health
  when it expires. Absorption grants gold-heart capacity, consumes it after
  Resistance and before health across every represented ordinary mob damage
  route, removes the grant on expiry, and exposes maximum health plus remaining
  absorption in raw entity state.
- Matched `onChangedPotionEffect` for Absorption: reapplying even the same
  amplifier runs remove/apply and replenishes a partly consumed shield; a
  stronger replacement installs its full grant. Levitation uses the exact
  post-move target interpolation and drag in the shared CPU/CUDA living spine,
  with duration-one expiry preceding travel.
- The focused live suite passes 335 checks and the Java/CPU arithmetic oracle
  passes 69 values. All four affected living-spine CUDA targets plus the
  arithmetic target compile for `sm_120` without executing GPU 1. Captured
  zombie-drop physics remains bit-exact for all 41 frames. Script, mob, route,
  and long runtime gates pass; the latter takes 332.38 seconds at 446,592 KB
  RSS. The final all-source focused ASan/UBSan/LSan gate passes in 4.18 seconds
  at 136,756 KB. CPU throughput is 4,997 steps/s against the 4,062 baseline and
  3,858.9 floor in
  `c/magma/trace/out/perf_guard_brewing_mob_health_levitation_cpu.json`.
  Global effort remains approximately 16%.

## 2026-08-05 (mob Water Breathing and general drowning)

- Added reloadable air state to every represented living mob. The shared
  Forge eye-in-water predicate uses exact type-specific eye heights; dry eyes
  reset air to 300, submerged eyes decrement it, and Water Breathing holds the
  current value before duration aging, including the duration-one expiry tick.
- The exact 320-tick drowning boundary resets -20 to zero, consumes the 48
  bubble-particle `Entity.rand.nextFloat` calls, then routes two damage through
  hurt immunity, Resistance, Absorption, and represented death/drop. Raw mob
  rows expose `air`, and `set_mob_air` resumes a saved pre-tick counter.
- The focused suite passes 368 checks, including full cadence, exact RNG,
  expiry/resume, dry reset, damage timers, same-tick death progression, and
  JSON reload. Java and CPU agree on 76 raw arithmetic/RNG values; CUDA 12.8
  compiles the target for `sm_120` without executing GPU 1. Product, brewing,
  script, mob, route, and long runtime gates pass; the long gate takes 325.43
  seconds at 446,692 KB RSS. The all-source ASan/UBSan/LSan gate passes in 4.71
  seconds at 137,520 KB. CPU throughput is 5,060 steps/s against the 4,062
  baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_brewing_mob_water_breathing_drowning_cpu.json`.
  Global effort remains approximately 16%.

## 2026-08-05 (mob Invisibility render state)

- Promoted active Invisibility from the bounded mob effect list into each live
  entity view. The product's normal survival viewer now omits an invisible
  mob's base geometry and the slime gel layer; duration-one expiry restores
  both on the next rendered state. The independently emitted burning overlay
  remains visible, matching `Render.doRenderShadowAndFire` ordering.
- Focused runtime coverage now passes 375 checks. Separate render regressions
  cover recorded and live invisible base models, visible/invisible slime gel,
  and the invisible-burning fire control. The brewing/JSON continuation,
  entity-render, item-render, product C, CUDA product-link, JDK 8, mob, and
  spawn-through-credits route gates all pass.
- The all-source ASan/UBSan/LSan focused gate passes in 4.86 seconds at
  137,268 KB RSS. The long runtime aggregate passes in 319.30 seconds at
  446,812 KB RSS. CPU throughput is 4,943 steps/s against the 4,062 baseline
  and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_brewing_mob_invisibility_cpu.json`. GPU 1 was
  not executed. Target detection, active-effect particles, special
  spectator/team visibility, and the remaining mob effect behaviors stay
  open. Global effort remains approximately 16%.

## 2026-08-05 (player Night Vision lightmap and fog)

- Ported `EntityRenderer.getNightVisionBrightness` exactly, including the
  duration-at-or-below-200 warning flicker driven by render partial ticks.
  Night Vision normalization now runs after the dimension provider lightmap
  colors and after fogColor1 for clear, terrain, water, and lava fog. Headless
  capture and the interactive product share the same fixed 256-entry per-frame
  lightmap for world, entity, hand, and particle shading.
- Extended the Java numeric oracle with 768 Night Vision lightmap RGB values
  across Overworld, Nether, and End plus 10 duration/partial-tick boundary
  values. CPU matches every raw float bit and packed color value. The
  `scenario_night_vision_dark_20260805T202020Z` sealed-tunnel oracle is
  physics-clean over 50 ticks; inventory/entity/world gates pass, and the
  structural pixel gate passes three non-stale frames with no unexplained
  clusters. The effect-warning pixels remain open until tapes record each
  framebuffer's render partial tick; their arithmetic is already bit-gated.
- Product C, CUDA 12.8 `sm_120` compile/link, JDK 8, sky, the broad native
  aggregate, and the direct fresh-spawn-through-credits route pass. The direct
  route takes 33.03 seconds at 512,640 KB peak RSS. The all-source focused
  ASan/UBSan/LSan gate passes 375 checks in 5.04 seconds at 137,444 KB. CPU
  throughput is 4,905 steps/s against the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_night_vision_cpu.json`. The standalone sky
  test now links its existing camera math dependency. GPU 1 was not executed.
  The render addition has no tick-time scan or allocation. Global effort
  remains approximately 16%.

## 2026-08-05 (player Blindness fog and sprint starts)

- Ported the 1.11.2 Blindness render path: exact last-19-tick far-distance
  interpolation, duration-dependent sky and terrain linear fog ranges,
  post-fogColor1 void darkening, and precedence over water/lava fog. Headless
  capture and the interactive product use the same clear and per-pass fog
  state. Flat and default worlds retain their distinct void-fog factors.
- Cached Blindness on both player prediction paths. It blocks Ctrl and
  double-tap sprint starts through the final represented duration, permits a
  same-tick start after expiry, and does not cancel an already active sprint.
  Critical-hit suppression remains open with the broader unimplemented player
  critical-hit path.
- Java and CPU agree at zero raw-float differences across seven fog distances
  and 210 RGB cases. The accepted sealed-tunnel tape
  `scenario_blindness_dark_20260805T204050Z` is physics-clean for 50 ticks;
  inventory, entity, world-state, and structural pixel gates pass four
  non-stale frames. The full native suite, including fresh spawn through
  credits, passes in 435.64 seconds at 676,472 KB peak RSS. Product C, CUDA
  12.8 `sm_120` compile/link, and JDK 8 pass without executing GPU 1. CPU
  throughput is 4,888 steps/s against the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_blindness_cpu.json`. The CUDA game target now
  supplies its required mc-sim include path. Global effort remains
  approximately 16%.

## 2026-08-05 (player critical attacks, weapon attributes, and hit durability)

- Replaced the represented mob-hit shortcut with the exact 1.11.2 core player
  attack boundary. Weapon base damage is cooldown-scaled before the critical
  multiplier, enchantment damage is added afterward, Blindness and all
  movement predicates gate criticals, and represented target armor applies in
  Java order. Sharpness, Smite, and Bane use the target creature category.
- Added the exact attack-speed modifiers for all 25 vanilla swords, axes,
  pickaxes, shovels, and hoes, including Java's double-to-float cooldown-period
  rounding. Accepted hits now spend one sword/hoe durability or two tool
  durability with exact Unbreaking trials; all those items also expose their
  material durability rather than silently behaving as unbreakable stacks.
- The locked real-Java oracle passes 38 raw target-health, cooldown, and held
  durability cases. The focused native player-effect test passes, JDK 8 passes,
  and CUDA 12.8 compiles and links the `sm_120` product target without running
  shared GPU 1. The fresh-spawn-through-credits route passes in 33.29 seconds
  at 511,224 KB after its legal combat driver was corrected to wait out sword
  cooldowns and budget for ordinary tool breakage. The final full native
  aggregate passes in 430.30 seconds at 676,384 KB.
- CPU throughput is 5,066 steps/s against the 4,062 baseline and 3,858.9 floor
  in `c/magma/trace/out/perf_guard_player_critical_cpu.json`. The hot path adds
  no allocation or world scan. Critical/enchantment sounds and particles,
  sprint and enchantment knockback, sweep attacks, Fire Aspect, Thorns,
  velocity acknowledgement, statistics, and criteria remain open. Global
  effort remains approximately 16%.

## 2026-08-05 (ordinary, sprint, and enchanted player knockback)

- Added the fresh accepted-hit `EntityLivingBase` impulse, including the two
  zero-resistance trials, existing horizontal-motion halving, grounded
  vertical-motion halving and 0.4 clamp, and target-relative direction. The
  represented entity RNG also consumes the intervening hurt/death sound-pitch
  floats even though audio emission remains open.
- Full-strength sprint attacks and Knockback I/II now apply the second
  yaw-table impulse in Java order. The attacker's horizontal motion is scaled
  by 0.6 and sprinting is cancelled only when that second impulse exists;
  partial-cooldown sprinting keeps both its sprint state and only the ordinary
  target impulse. Controlled and ordinary pig paths share the same added edge.
- The locked real-Java attack oracle now passes 44 cases, including six raw
  target/player double-motion and sprint-state cases. The focused player test,
  mob suite, JDK 8 build, and spawn-through-credits route pass; the route takes
  33.00 seconds at 512,644 KB. CUDA 12.8 links the `sm_120` product in 1.92
  seconds at 123,100 KB without executing shared GPU 1. The final full native
  aggregate passes in 435.09 seconds at 676,172 KB.
- CPU throughput is 4,994 steps/s against the 4,062 baseline and 3,858.9 floor
  in `c/magma/trace/out/perf_guard_player_knockback_cpu.json`. Knockback adds
  constant work only on accepted attacks and no allocation or world scan.
  Sounds/particles, sweeping, Fire Aspect, Thorns, player-target velocity
  acknowledgement, statistics, and criteria remain open. Global effort
  remains approximately 16%.

## 2026-08-05 (player Fire Aspect commit and rollback)

- Added Fire Aspect I/II to the represented player attack order. A previously
  unlit living target receives the one-second preignition before damage and
  loot selection, then an accepted hit commits four or eight seconds. Existing
  longer fire is never shortened. If hurt immunity rejects the hit, only a
  newly added preignition is extinguished; a target already burning keeps its
  original counter.
- The locked real-Java attack oracle passes 50 cases after adding six raw fire,
  health, and durability boundaries. Native coverage includes all six plus an
  ordinary lethal pig attack proving the preignition selects cooked rather
  than raw pork.
- Focused player, mob, JDK 8, fresh-spawn-through-credits, and full native
  gates pass. The final aggregate takes 435.13 seconds at 676,192 KB. CUDA
  12.8 links the `sm_120` product without executing shared GPU 1. CPU
  throughput is 4,947 steps/s against the 4,062 baseline and 3,858.9 floor in
  `c/magma/trace/out/perf_guard_player_fire_aspect_cpu.json`; the added work is
  bounded to attack handling with no allocation or world scan. Sounds,
  particles, sweeping, Thorns, player-target velocity acknowledgement,
  statistics, and criteria remain open. Global effort remains approximately
  16%.

## 2026-08-07 (jukebox record audio and recorder consolidation)

- Locked all 12 real-Java jukebox insertion/ejection event pairs and matched
  them in the native world and sound rings.
- Expanded the owned manifest to 70 events/146 variants. Four fixed record
  voices stream OGG data through bounded 64 KiB chunks; ordinary effects keep
  the existing predecoded pool, and headless/RL paths remain audio-free.
- Removed the duplicate stale Java `Recorder` mod after merging its unique
  elytra flag-7 and pre-travel-look tape fields into canonical `qrl.Recorder`.
- Java, focused parity/audio, full runtime, and clean product builds pass. The
  runtime aggregate is 6:06.97 at 448,576 KiB RSS with zero swap; CPU throughput
  is 4,228 steps/s against the 3,858.9 floor. GPU 1 was not executed.

## 2026-08-07 (firework blast and twinkle audio)

- Added a locked client oracle around the real `ParticleFirework.Starter` and
  `WorldClient.playSound` path. Eight direct Java/native cases cover small and
  large blasts, the 16-block far threshold, the greater-than-10-block delay,
  exact seeded pitch bits, and flicker twinkles at `2 * explosions + 14` ticks.
- Firework item metadata now retains the aggregate large-ball and flicker bits
  across star/rocket crafting without growing `ICStack`. Native playback owns
  all six blast/twinkle events, schedules far sounds at Java's exact due tick,
  and keeps twinkles in a fixed active-only store. The manifest is now 76
  events/152 variants; headless paths still initialize no audio.
- The direct comparator passes all eight cases plus a deliberate delay
  negative control. Java and clean native product builds pass, as do the
  focused six-family parity gate and every locally available quick-sweep
  stage. The two snapshot-backed Blaze stages skip because their `.bsnp`
  inputs are not present. The exact-current full runtime aggregate passes in
  6:40.65 at 448,124 KiB peak RSS with zero major faults and zero swap; CPU
  throughput is 4,132 steps/s against the 3,858.9 floor. GPU 1 was untouched.
- The sweep exposed numeric model-table drift in contextual upper double
  plants and glass panes plus a stale TNT reverse-map assertion. Their product
  IDs and gates are synchronized again. The village oracle gate now uses the
  canonical repository Gradle cache, avoiding a second wedged daemon/cache.

## 2026-08-07 (complete block-break material audio)

- Added a real 1.11.2 Java oracle that enumerates every registered block and
  the exact `RenderGlobal` world-event-2001 break sound. Native matches all 235
  non-air IDs across wood, gravel, grass, stone, metal, glass, cloth, sand,
  snow, ladder, anvil, and slime, including raw volume/pitch bits and legacy
  metadata invariance. A deliberate metal-to-stone substitution is rejected.
- Progressive player destruction now carries an explicit break-effect bit
  through `GmBlockEdit`, preserving bare-hand breaks without confusing them
  with non-break air edits. Player mining and sheep grazing both enter the
  same fixed world/sound rings. The asset manifest is now 88 events/199
  variants, while headless paths still perform no playback work.
- The exhaustive comparator, material negative control, player-control,
  grazing, OpenAL, clean product, and expanded seven-family parity gates pass.
  Every locally available quick-sweep stage passes; the two snapshot-backed
  Blaze stages skip because their `.bsnp` inputs are absent. The exact-current
  runtime aggregate passes in 6:44.47 at 446,676 KiB peak RSS with zero major
  faults and zero swap. CPU throughput is 4,085 steps/s against the 3,858.9
  floor. GPU 1 was untouched.

## 2026-08-07 (complete block-placement material audio)

- Extended the real 1.11.2 block-sound oracle through `ItemBlock.onItemUse`'s
  post-placement `SoundType`. Java and native now agree for every one of the
  235 registered non-air block IDs, all valid metadata states, twelve distinct
  placement families, and raw volume/pitch bits. Independent break and place
  metal-to-stone negative controls both fail as intended.
- Successful ordinary block placement carries an explicit placement-effect
  bit through `GmBlockEdit`. The runtime emits the placed block's sound at the
  exact centered position after applying the world edit, without fabricating
  a world event. Break and place share one constant-time material classifier.
  The manifest is now 100 events/244 variants; fixed playback pools, bounded
  record streaming, and audio-free headless/RL behavior are unchanged.
- The exhaustive Java/native comparator, player-control, direct runtime,
  OpenAL, seven-family parity, clean native product, and clean JDK 8 builds
  pass. Every locally available quick-sweep step passes; the two snapshot
  stages skip because their `.bsnp` inputs are absent. The exact-current
  aggregate passes in 6:39.86 at 437,400 KiB peak RSS with zero major faults.
  CPU throughput is 4,142 steps/s against the frozen 3,858.9 floor. GPU 1 was
  untouched.

## 2026-08-07 (complete progressive-mining hit audio)

- Extended the complete real-Java block-sound registry oracle with
  `SoundType.getHitSound`, using the cadence/category/scalar semantics locked
  from `PlayerControllerMP.onPlayerDamageBlock`. Java and native agree for all
  235 registered non-air IDs, every valid metadata state, twelve hit families,
  and the raw `(volume + 1) / 8` and `pitch * 0.5` float bits. Independent
  break, place, and hit family substitutions are all rejected.
- The player controller now emits a bounded transient on damage update zero
  and every fourth damage update thereafter. Runtime playback preserves the
  exact NEUTRAL category and centered block position. The integration gate
  also exposed and fixed stale post-break delay and attack-edge latches across
  explicit controller resets. The manifest is now 112 events/307 variants;
  fixed playback pools and audio-free headless/RL behavior are unchanged.
- The exhaustive comparator, cadence, runtime, OpenAL, focused parity, clean
  native product, and clean JDK 8 builds pass. The exact-current aggregate
  passes in 6:22.74 at 448,576 KiB peak RSS with zero major faults or swap.
  CPU throughput is 4,197 steps/s against the frozen 3,858.9 floor. GPU 1 was
  untouched. Every locally available quick-sweep step passes; the two
  snapshot-backed Blaze stages skip because their `.bsnp` inputs are absent.

## 2026-08-08 (player landing and block-material audio)

- Extended the real-Java block-sound oracle through
  `SoundType.getFallSound`. Java and native agree for all 235 registered
  non-air IDs, every valid metadata state, twelve fall families, and the raw
  `volume * 0.5F` and `pitch * 0.75F` bits. Independent break, place, hit, and
  fall material substitutions are rejected.
- Damage-producing player landings append the exact ordered pair at the
  player's position: player small/big fall followed by the supporting block's
  fall family, both in the PLAYERS category. The damage threshold is pinned on
  both sides of four, and hay applies its 0.2 multiplier before selection. The
  manifest is now 126 events/372 variants; playback pools and audio-free
  headless/RL behavior remain unchanged.
- Focused registry, player-control, runtime-order, and OpenAL gates pass. The
  exact-current aggregate passes in 6:30.95 at 449,568 KiB peak RSS with zero
  major faults or swap. CPU throughput is 4,353 steps/s against the frozen
  3,858.9 floor. Every locally available quick-sweep step passes; the two
  snapshot-backed Blaze stages skip because their `.bsnp` inputs are absent.
  GPU 1 was untouched.

## 2026-08-08 (distance-gated player footstep audio)

- Extended the real-Java block-sound oracle through
  `SoundType.getStepSound`. Java and native agree for all 235 registered
  non-air IDs, every valid metadata state, twelve step families, and exact
  `volume * 0.15F` and pitch bits. All five action-specific material sabotage
  controls are rejected.
- Player movement now accumulates actual post-collision displacement with
  Java's float/double order and emits on the first tick-10 integer threshold.
  Supporting fence/wall/gate fallback, snow-layer override, ladder vertical
  distance, ground-sneak suppression, and riding suppression are retained.
  Idle ticks skip the lookup and square root. The manifest is now 138 events
  and 435 variants.
- Focused registry, cadence, snow, sneak, runtime, and OpenAL gates pass. The
  exact-current aggregate passes in 6:24.97 at 450,656 KiB peak RSS with zero
  major faults or swap. CPU throughput is 4,055 steps/s against the 3,858.9
  floor and within 0.2% of the 4,062 baseline. Every locally available
  quick-sweep stage passes; the two snapshot-backed Blaze stages skip because
  their `.bsnp` inputs are absent. GPU 1 was untouched.

## 2026-08-08 (player swim and splash audio)

- Added exact client-player water-entry splash and distance-threshold swim
  events. Splash records the pre-move source; swim records the post-collision
  source and pre-water-drag motion. Both retain Java's weighted volume and cap.
- Added the separate client Entity.rand cursor. Real Java and native match raw
  volume/pitch bits, and splash advances its two pitch plus 65 unrendered
  particle draws before a chained swim. The negative control catches a
  one-bit post-splash cursor error. The manifest is now 140 events/441 variants.
- Focused oracle/runtime/player/OpenAL and clean Java gates pass. The native
  aggregate passes in 6:06.00 at 450,080 KiB peak RSS with zero major faults or
  swap. CPU throughput is 4,312 steps/s against the 3,858.9 floor and above the
  4,062 baseline. The batched player layout and GPU path are unchanged; GPU 1
  was untouched. Every locally available quick-sweep stage passes; the two
  snapshot-backed Blaze stages skip because their `.bsnp` inputs are absent.
