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
