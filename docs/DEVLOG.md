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
