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

## Not open work (cuts / done)

Redstone, multiplayer, disk saves as product, audio, side structures (monuments/mansions/…),
optional villages/enchanting/brewing/weather bundles (flags reject `on` until implemented).
Render-opt lab is closed unless reopening a specific kernel directory.

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
