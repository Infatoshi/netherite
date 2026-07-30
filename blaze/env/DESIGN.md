# Design: batched CUDA env for the mine-coal stage (blaze)

Produced 2026-07-14 by a design pass over magma + blaze. Code is ground truth; file:line
references were verified at design time.

## Part 1 — What the code actually does (verified findings)

### 1.1 How the real env ticks the player (blaze reuse, exact seams)

- `gm_rl_run` (magma/game/rl_mode.c:500) loops: read one JSON action line, `gm_runtime_tick`, `rl_emit_obs`. Binary obs (`--rl-bin`) is the packed `RlBinObs` struct with magic `0x524c4f42` "BOLR" (rl_mode.c:255-275).
- `gm_runtime_tick` (magma/game/runtime.c:261) calls `gm_player_tick` (runtime.c:315-319) over a 3x3-chunk physics `window` (`Chunk[PSV_NCHUNKS]`, PSV_DIM=3 -> 9 chunks, blaze/core/player_survival.h:39-43), recentered on chunk crossing (runtime.c:23-36) with `ox/oz` world offsets. The window is refilled from GmWorld only on recenter/mutation (runtime.c:290-298).
- **The physics itself is blaze source, already `MC_HD` (host+device)**: `gm_player_tick` (magma/game/player_ctl.c:590) calls `psv_physics_tick` — blaze/core/player_survival.h:344-497, marked `MC_HD static inline`, using only LUT trig (`mc_sin`/`mc_cos`), doubles, IEEE ops, **no RNG**. It composes `ppw_move_flying` + `mc_entity_move_step` (physics_collision_math/player_physics_world headers, all MC_HD). The `MC_HD` macro is defined in blaze/core/mc.h:8-21 (`__host__ __device__` under `__CUDACC__`, empty otherwise) — this is the single-source CPU==CUDA mechanism; each `cpu/foo.c` and `cuda/foo.cu` is a thin driver over the same `core/foo.h`.
- Sprint is a small state machine in player_ctl.c:287-305 (double-tap-W with `sprint_toggle_timer=7`, food>6 gate); it can self-trigger from forward-press edges even though the learned policy never sends `sprint`.

### 1.2 Block breaking (dig timing)

- Progressive dig is a **static-global state machine in player_ctl.c** (`s_dig_progress`, `s_dig_h*`, `s_dig_hitting`, `s_dig_delay`, `s_atk_prev`; player_ctl.c:27-49), mirroring `PlayerControllerMP`: press = acquire tick, held ticks accrue `pb_relative_hardness` per tick, break at >=1.0, then `blockHitDelay = 5` swallows 5 ticks (player_ctl.c:364-459).
- The hardness math is blaze, MC_HD, and golden-verified: `pb_relative_hardness` (blaze/core/player_break.h:124-131) = `dig_speed / hardness / 30` (harvestable) with `/5` if airborne and `/5` if eyes underwater (player_break.h:114-118). Wooden pick (id 270) vs stone (hardness 1.5, mc_blocks.h:50) and coal ore (hardness 3.0, mc_blocks.h:58-60). `pb_can_harvest` gates coal-ore drops on a pickaxe (player_break.h:74-81).
- Dig targeting uses `gm_raycast_sel_reach` (magma/game/sel_box.c:250-292): fixed-step DDA (`PSV_RAY_DT=0.05`, reach `PSV_REACH=5.0`), LUT trig, with per-block selection boxes (`gm_sel_box`, sel_box.c:34). This file is magma-side (not MC_HD), but pure deterministic math.
- Harvest drops: `harvest_drop` (player_ctl.c:188-211) — coal ore (16) -> item 263 x1; stone -> cobble; gravel flint via a **stateless positional hash** (player_ctl.c:198-202), no RNG stream. Tool wear via `ita_on_block_destroyed` (player_ctl.c:222-234). Drop entity spawned at block center, `pickup_delay=10` (runtime.c:374-377).

### 1.3 Item drops and pickup

- `gm_live_spawn_item` / `gm_live_spawn_stack` (magma/game/live_sim.c): spawn with **zero motion** (memset), lifespan 6000. Item physics `gm_live_tick`: `my -= 0.04`, snap-to-block-top ground model, friction 0.6*0.98 grounded / 0.98 air — deterministic, no RNG. Max 48 active items (`GM_LIVE_MAX`) plus bounded overflow hold when full.
- Pickup `gm_live_tick_player` (live_sim.c:127-148): box test `|dx|<=1.0, |dz|<=1.0, py-0.25 <= ey <= py+2.8`, `pickup_delay==0`, merges via `isr_add_item_stack_to_inventory`.

### 1.4 Semantic camera and ore scan

- `rl_emit_obs` (rl_mode.c:288-498): scan cache over `+-16` horizontal, `-24/+40` vertical band (rl_mode.c:45-47), per-column topmost non-air **plus every log/coal/table block regardless of depth** ("always", rl_mode.c:322-323). Coal list = nearest 32 coal-ore blocks in that window, sorted by squared distance to `(x+0.5) - eye` (rl_mode.c:347-371).
- Camera: 64x36, 70-deg FOV, Amanatides-Woo DDA (`rl_raycast`, rl_mode.c:144-170), depth `(int)(t*4)` clamp 255, edge = hit within 0.05 of face border (rl_mode.c:175-187). **Important divergence**: rl_mode's camera uses **libm sin/cos on doubles** (rl_mode.c:380-388), while the blaze port `core/obs_camera.h` uses the **MathHelper LUT** (`mc_sin`, obs_camera.h:110-112) precisely so CPU==CUDA is bitwise (obs_camera.h:10-15). These two produce near-identical but NOT bit-identical frames.
- The CUDA camera kernel already exists and is verified: blaze/cuda/obs_camera.cu (`run_oc`, one thread per (pose,pixel), obs_camera.cu:28-38), commits `91d79cb` ("batched 64x36 semantic camera, CPU==CUDA") and `7b4ef65` (edge channel, bitwise). It prints a rays/s line to stderr (obs_camera.cu:6) — the previously measured ~396M rays/s figure comes from this driver; re-measure in M3.

### 1.5 World storage, snapshot hooks, existing tooling

- Blocks live in `GmWorld` (world_live.c), read/written via `gm_world_block/gm_world_meta/gm_world_set_block_meta` (world_live.c:223, :253). Packed state is `(id<<4)|meta` u16 (blaze/core/mc_world.h:29). A `Chunk` is 128KB blocks + 64KB light + biome (mc_world.h:38-43).
- Snapshot *load* machinery already exists: `gm_runtime_load_block` / `gm_runtime_snapshot_region` (runtime.c:772-783), `gm_runtime_set_pose_state` (runtime.c:499-507), `gm_runtime_set_inventory` (runtime.c:804-812). What's missing is the *export* side.
- `magma/trace/world_dump.c` exists but is a worldgen-verification tool (fresh world from seed, "CRWD"/"CRWS" format) — it cannot capture post-prefix mutations (mined tunnel, placed table) or player/inventory state. **A new state-dump in rl_mode.c is the right path.**
- `region_tensor.h` (blaze/core/region_tensor.h) gives the dense `[nx][ny][nz]` u16 layout the camera already consumes (`OcRegion`, obs_camera.h:40-50).
- Precedent for batched CUDA envs: `cuda_batch_tick.h` (one env per block, CPU scalar loop == CUDA); precedent for a Python binding: `blaze/py/mcsim_gym.cpp` (pybind11 — but see binding recommendation below).

### 1.6 What the trainer consumes (exact obs subset)

From ppo_coal.py: `planes()` (ppo_coal.py:73-92) uses only `cam`, `depth`, `edge`, `pitch`, plus `nearest_coal` (ppo_coal.py:57-70) from the `coal` list and `x/y/z/yaw`. Reward (ppo_coal.py:223-239): `-0.005`/tick, `0.5 * (prev_dist - dist)` shaping, `+0.03` if `attack` and `cam[18*64+32] == 16` (crosshair on coal), `+10` and done when `inv_counts[7]` (item 263) increases. Actions: 5 heads — dyaw {-15,0,15}, dpitch {-10,0,10}, forward {0,1}, jump {0,1}, attack {0,1} (`act_dict`, ppo_coal.py:51-54), repeat 4 with dyaw/dpitch zeroed on repeats (ppo_coal.py:204-218). **No craft, no interact, no sneak/sprint/strafe/use is ever sent in the learned stage.** `blocks`, `logs`, `hotbar_*` are never read by the trainer.

## Part 2 — Architecture

### 2.1 Overview

```
prefix (real CPU env, once per seed x curriculum stage)
   └─ "snapshot" command -> .bsnp file
snapshot loader (host) -> device-resident snapshot cache (per seed x stage)
CUDA batched env (blaze.so, device D):
   k_reset(env_mask)        : copy snapshot -> per-env state
   k_tick(actions, T=4)     : 1 thread = 1 env, runs T full game ticks
   k_obs(...)               : 1 thread = 1 pixel (N*2304 threads), camera+scalars
PyTorch: obs land in torch.cuda tensors (trainer passes .data_ptr() in)
```

### 2.2 Per-env state (allocate ONCE at init; kernels only mutate bytes)

Layout: **AoS array of a flat `Blaze` struct for player/dig/item state** (one thread owns one env; the struct is walked serially so SoA buys little), **one big pooled buffer for region tensors** (the only large per-env allocation), SoA only for the tensors the trainer reads (obs/reward/done), which must be contiguous `[N, ...]` torch layouts anyway.

Per env:

| Buffer | Size | Notes |
|---|---|---|
| region tensor u16 `(id<<4)\|meta` | 64 x 128 x 64 = 1.0 MB | `OcRegion`-compatible; x/z +-32 around handoff, y 0..127 covers shaft + surface |
| `CuPlayer` (McEntity + yaw/pitch/vitals + sprint/jump state + IsrInv 36 slots) | ~1 KB | mirror of `PsvPlayer` (player_survival.h:72-96) |
| dig state (`progress, hx,hy,hz, hitting, delay, atk_prev`) | 32 B | the player_ctl.c statics, per-envified |
| item drops `CuItem[8]` | ~512 B | `GmLiveEnt` fields; learned stage never has >2 live drops |
| coal list: static ore positions `[64]` + alive bitmask | ~512 B | precomputed at snapshot load |
| obs out: cam u16[2304], depth u8[2304], edge u8[2304], scalars f32[6], reward f32, done u8, pos/yaw f32[5] | ~10 KB | written into caller-provided torch tensors |

Total ~ **1.05 MB/env** -> N=4096 ~ 4.3 GB; N=16384 ~ 17 GB. Fits GPU0 (RTX PRO 6000, 96 GB) with huge headroom. Snapshot cache: 8 seeds x ~4 curriculum stages x 1 MB ~ 32 MB.

`cudaSetDevice(device_index)` at init; the API takes an explicit device int (box is shared; default 0).

### 2.3 Kernels

1. **`k_tick`** — one thread per env, 128 threads/block, executes `T` game ticks (T=4 for action repeat, dyaw/dpitch applied only on sub-tick 0, matching ppo_coal.py:205-208). Per tick, in the exact real-env order (runtime.c:261-460 restricted to the learned-stage path):
   1. dig raycast + progressive dig state machine (port of player_ctl.c:364-459, calling `pb_relative_hardness`), block edits written to the region tensor; drop spawn into `CuItem` slots;
   2. sprint state machine (player_ctl.c:287-305), sneak scaling skipped (never sent);
   3. `psv_physics_tick` **verbatim** (player_survival.h:344) — but reading blocks via a region-tensor accessor (see fidelity note below);
   4. vitals slice `gm_vitals_apply`-equivalent (only fall-distance/food>6 matter; no mobs, no water underground in the common case);
   5. item tick + pickup (port of live_sim.c:66-148);
   6. coal-list alive update (mined ore -> dead) and inventory coal-count check -> `done`, `+10` reward accumulation; per-tick `-0.005` and distance shaping accumulated in-kernel.
2. **`k_obs`** — one thread per pixel (`N * 2304` threads), calls `oc_pixel` (obs_camera.h:103) against the env's region tensor; one thread per env computes the 6 scalars (formulas from ppo_coal.py:81-91) and nearest-coal distance. Run **once per decision** (after the T=4 tick kernel), not per tick — same economy the real env already exploits with `"cam":0` (rl_mode.c:283-287).
3. **`k_reset`** — masked: for done envs, memcpy snapshot region + player/inventory/dig/item state from the device snapshot cache.

Throughput check: camera dominates. 1M env-ticks/s at repeat 4 = 250k decisions/s x 2304 rays = **576M rays/s** needed; obs_camera.cu measured ~396M rays/s on earlier hardware — sm_120 should clear this, and underground rays terminate in 1-3 voxels (far cheaper than the surface-pose benchmark). The tick kernel is trivial by comparison. If camera falls short: next lever is smaller region `ny` or u8 region ids.

### 2.4 Obs -> torch, binding

**Recommendation: plain C ABI `.so` + ctypes, NOT torch.utils.cpp_extension.**
- The trainer allocates `torch.empty((N,36,64), dtype=torch.int16, device=dev)` etc. and passes `.data_ptr()`; the .so launches kernels writing into those pointers. Zero-copy, no dlpack needed.
- Justification: nvcc flags must be **exactly** blaze's determinism flags (`--fmad=false`, no fast-math; blaze/Makefile:22, SPEC.md:46-47,139). cpp_extension injects its own flag set and torch-ABI coupling; a bare .so built by the existing Makefile pattern (magma already builds sm_120 objects with `NVFLAGS ?= -O2 --fmad=false -arch=sm_120`, magma/Makefile:8) keeps the CPU==CUDA guarantee auditable. Stream/sync: `blaze_step` uses its own stream + `cudaStreamSynchronize` before returning.

## Part 3 — Fidelity contract

### Must match EXACTLY (bit-exact target)

| Behavior | Why achievable |
|---|---|
| Player positions/velocity/onGround per tick (doubles) | `psv_physics_tick` is MC_HD single-source, LUT trig, no RNG; blaze's whole CPU==CUDA program is bit-verified under `-ffp-contract=off` / `--fmad=false` (SPEC.md:46-47). Reusing the source verbatim achieves bit-exact positions. Caveat: block reads go through a region-tensor accessor instead of `psv_get_block(Chunk*)` — values are identical u16 states, and `psv_collect_blocks`'s AABB iteration order must be reproduced exactly (copy the loop from player_survival.h:150), so float streams are unchanged. |
| Dig tick counts (acquire tick, per-tick progress float accumulation, `>=1.0` break, blockHitDelay=5, airborne/underwater /5) | direct port of player_ctl.c:364-459 with per-env state; `pb_*` reused verbatim (MC_HD already). Progress is float accumulation — same source, same flags -> bit-exact. |
| Dig/selection raycast target | port sel_box.c:250-292; underground blocks are full cubes, but port `gm_sel_box` defaults + ore/stone cases anyway. |
| Drop identity/count, tool wear, pickup box and delay | player_ctl.c:188-239, live_sim.c:127-148 — deterministic integer/double math. |
| Item drop physics | live_sim.c:71-108, deterministic. |
| Coal list membership + ordering | static ore set within (+-16 xz, -24/+40 y window around player block) minus mined, sorted by d2 — semantically identical to the rl_mode scan for coal (the "always" rule means depth never hides coal). |
| Inventory merge rules | `inventory_stack_rules.h` reused verbatim (MC_HD). |

### Near-identical acceptable (with defined tolerance)

- **Camera**: rl_mode.c's camera (libm double trig) vs `oc_pixel` (LUT float trig). **Fix: change rl_mode.c to use the core/obs_camera.h ray source** in M1, making the real env and batched env share one camera source -> bit-exact cam/depth/edge, and rl_mode gets the faster verified code. Fallback tolerance if rejected: <=0.5% of pixels differing per frame, depth +-1 LSB on differing pixels.
- **Reward scalars**: Python computes `nearest_coal`/shaping in float64 (ppo_coal.py:57-70); the kernel computes in float64 too — target exact within 1e-9; reward equality is not part of the sim-fidelity gate.

### Deliberately dropped (out of scope for the learned stage)

- Fluids CA (`gm_fluid_tick`, fluid_live.c:159) — no water/lava in the training shafts. Snapshot loader should reject (or flag) snapshots whose region contains liquid ids 8-11 within the burrow envelope.
- Mobs, dragon, projectiles, furnaces, crafting, containers, portals, weather, plants.
- Falling blocks: no sand/gravel settling code found in magma's live path; confirm during M1 by mining under gravel in the real env.

### Verification plan

`blaze_verify` harness (CPU build of the same core):
1. From a snapshot, step real magma (`--rl-bin`, load snapshot) and batch-of-1 CPU blaze with an identical 1000-action recorded sequence (real trained-episode streams from `coal_env0_episodes.jsonl` plus random-policy streams).
2. Compare per tick: `x/y/z/vx/vy/vz` raw double bits, onGround, yaw/pitch bits, dig progress bits, region block diffs, inventory, item entity states, coal list, cam/depth/edge buffers. Gate: **zero diffs** (post camera-unification).
3. Then CPU blaze vs CUDA blaze, batch of 4096 (mixed seeds/stages), same actions: **bitwise-identical** dumps (the standard blaze oracle bar, SPEC.md:21).

## Part 4 — Snapshot format and extraction

### Export path: extend rl_mode.c (new action key)

Add `"snapshot":"<path>"` handling in the action loop (beside `"craft"`/`"interact"`, rl_mode.c:553-557): before the tick, dump state. Required new accessors:
- `gm_player_ctl_dig_export/import` in player_ctl.c for the statics (`s_dig_progress/hx/hy/hz/hitting/delay/atk_prev/rc_delay/use_prev`) — currently only partially visible via `gm_player_dig_state` (player_ctl.c:678). Accepted simplification: require snapshots at quiescent points (>=6 ticks of no-op actions before dump so `s_dig_delay` drained and no dig in progress) and store zeros; the prefix generator can guarantee this.
- Sprint/jump fields are already in `PsvPlayer` (player_survival.h:79-90) — direct reads.

### `.bsnp` format (little-endian, packed)

```
magic "BSNP", u32 version=1
i64 seed, i64 tick
player: f64 x,y,z (world), f32 yaw,pitch, f64 vx,vy,vz,
        i32 on_ground, f32 fall_distance,
        i32 sprinting, sprint_toggle_timer, jump_factor_sprint, jump_ticks,
        f32 prev_move_forward, i32 prev_sneak
vitals: f32 health, i32 food, f32 saturation, f32 exhaustion
dig:    f32 progress, i32 hx,hy,hz,hitting,delay,atk_prev,rc_delay,use_prev
inv:    37 x (i32 item, i32 count, i32 meta)   [36 main + offhand]
items:  u32 n, n x (f64 x,y,z,mx,my,mz, i32 item,count,meta,age,pickup_delay,lifespan,on_ground)
region: i32 x0,y0,z0,nx,ny,nz, then nx*ny*nz u16 packed states (id<<4|meta),
        filled via gm_world_block/gm_world_meta after gm_world_ensure(...,3)
        (the ensure at rl_mode.c:312-314 pattern)
coal:   u32 n, n x (i32 x,y,z)   [all coal ore in region — convenience; also derivable]
```

Region bounds: `x0 = floor(px)-32, z0 = floor(pz)-32, nx=nz=64, y0=0, ny=128`. Handoff y is ~20-40; +-32 xz covers the <=1000-tick burrow envelope and camera rays up to 32 blocks laterally; out-of-region camera reads are air (obs_camera.h:47-49). Episode terminates with `done` (failure) if the player exits `[x0+2, x0+nx-2]` bounds.

### Prefix/curriculum baking

New `blaze/env/make_snapshots.py`: for each of the 8 training seeds, replay `coal_prefixes.json` (as `make_env`, ppo_coal.py:113-134), then run `cp.stage_coal(env, budget=..., stop_dist=d)` for d in {6.0, 4.5, 3.0} plus tick-budget checkpoints, sending `"snapshot"` after a 6-tick quiesce at each stage. Curriculum in the batched trainer = anneal the sampled stage from near (3.0) to far (6.0), replacing the in-episode scripted help (ppo_coal.py:159-166).

## Part 5 — File plan

New directory `blaze/env/`:

| File | Contents |
|---|---|
| `blaze_core.h` | `MC_HD` env structs + full tick logic: region accessors (psv_get_block-equivalent over the region tensor, `psv_collect_blocks` clone), per-env dig machine (port of player_ctl.c:364-459), sprint machine, item tick/pickup (live_sim.c ports), sel raycast (sel_box.c port), coal list, reward/done. Includes `player_survival.h`, `player_break.h`, `obs_camera.h`, `inventory_stack_rules.h`, `items_tools_armor.h`, `block_props_table.h`, `mc_world.h` from `$(BLAZE)/core`. |
| `blaze_snapshot.h/.c` | .bsnp reader (host), device snapshot cache upload. |
| `blaze_cpu.c` | CPU reference: same core stepped serially over N envs; also the M1 verify harness `blaze_verify` main. |
| `blaze_cuda.cu` | `k_reset`, `k_tick`, `k_obs`; C ABI: `blaze_create(int device, int n, cfg)`, `blaze_load_snapshots(paths)`, `blaze_assign(env->(seed,stage))`, `blaze_reset(u8 *mask_dev)`, `blaze_step(const i32 *act_dev[N][5], int repeat, u16 *cam, u8 *depth, u8 *edge, float *scal, float *rew, u8 *done, float *pose)`, `blaze_destroy`. All device pointers caller-owned. All allocations in `blaze_create` (no in-kernel malloc; heavy structs never on device stack, mirroring obs_camera.cu's host-alloc pattern). |
| `blaze.py` | ctypes wrapper; allocates torch tensors on `torch.device(f"cuda:{idx}")`, exposes `VecBlaze.step(actions)->(obs..., reward, done)`. |
| `make_snapshots.py` | prefix replay + snapshot baking (Part 4). |
| `ppo_coal_cu.py` | trainer variant (Part 6). |

Modifications to existing files:
- `game/rl_mode.c`: `"snapshot"` action key; camera swap to the obs_camera.h ray source (build an `OcRegion` view over the scan window — or a direct `gm_world_block`-backed variant kept source-identical in the ray loop).
- `game/player_ctl.c/h`: dig-state export/import accessors.
- `magma/Makefile`: targets `blaze/env/blaze.so` (`$(NVCC) -O2 --fmad=false -arch=sm_120 -Icore -I. -I$(BLAZE)/core --shared -Xcompiler -fPIC`) and `blaze/env/blaze_verify` (CC path with `-ffp-contract=off`). Take `SM` as an override var.

## Part 6 — Training integration (`ppo_coal_cu.py`)

Minimal deltas from ppo_coal.py:
- Replace `make_env`/`MagmaEnv` pool with one `VecBlaze(N=4096, device=0)`; episode = fixed 1000 ticks (250 decisions) with masked resets.
- `planes()` becomes tensor ops on the batched cam/depth/edge (`cam==16`, `isin(cam,(1,4))`, `cam!=0`, `depth/255`, edge) directly on GPU; frame stack via a rolling `[N, STACK, 5, 36, 64]` buffer.
- Scalars come from `k_obs` (identical formulas to ppo_coal.py:81-91).
- Reward identical, computed in-kernel: `-0.005`/tick, `0.5*(prev_dist-dist)` per tick, `+0.03` for attack-with-crosshair-on-coal using the decision-tick frame on skipped-camera ticks (kernel keeps last rendered center pixel), `+10` on coal-count increase -> done.
- Curriculum = snapshot-stage annealing instead of `stage_coal` help.
- PPO update code unchanged apart from batch shapes; move net to GPU.
- **Eval unchanged**: `eval_coal.py` still runs the real env — that is the ground-truth gate.

### Part 6a — Chain-trainer reward module (`reward_chain.py`)

The spawn-to-torch chain trainer (`ppo_chain_cu.py`) computes reward OUTSIDE the
kernels (status/cam/pose/scal readouts -> per-lane reward on torch tensors).
All of it lives in `reward_chain.py`: `ChainRewardSpec` (every weight/threshold
as a dataclass field, `resolve()` = defaults <- `REWARD_JSON` <-
`COAL_CHEW`/`HUNT_DESC` env) + `ChainReward` (stateful per-lane calculator:
best-so-far counters, shaping registers, episode resets). Contract: bit-identical
to the pre-extraction inline block, proven by `test_reward_chain.py` (verbatim
golden reference archived in the test; randomized rollouts + mid-stream resets,
v1 and v2 parameter grids). The trainer prints the resolved spec at start and
writes `<ckpt>_reward.json` next to every checkpoint, so any run is reproducible
from its sidecar via `REWARD_JSON`.

## Part 7 — Milestones and acceptance tests

- **M1 — Snapshot export + CPU batched reference.** rl_mode.c snapshot key + camera unification; `make_snapshots.py` produces .bsnp for the 8 seeds; `blaze_verify`: batch-of-1 CPU blaze vs real magma from the same snapshot over >=3 recorded 1000-action sequences per seed -> **zero diffs** in position bits, block edits, inventory, coal list, cam/depth/edge. Also: real env loaded-from-snapshot equals real env continued-past-snapshot (validates the export itself).
- **M2 — CUDA port.** `blaze_verify --cuda`: CPU blaze vs CUDA blaze, N=4096 mixed seeds, 1000 ticks, random actions -> **bitwise identical** state dumps (positions, blocks hash, inventory, obs buffers). Run under `compute-sanitizer` clean.
- **M3 — Throughput.** Report env-ticks/s and decisions/s at N=1024 and N=4096 (repeat 4, camera per decision), plus the k_obs rays/s line. Gate: >=1M env-ticks/s aggregate at N=4096 on GPU0.
- **M4 — Training + transfer.** `ppo_coal_cu.py` to >=60% success on the 8 training seeds *in the batched env*, then `eval_coal.py` (real env) reproduces >= the current real-env success on those seeds. Wall-clock comparison vs the current trainer recorded in DEVLOG.

## Part 8 — Risks and gotchas found in code

1. **Camera source split** (rl_mode.c libm vs obs_camera.h LUT) — resolve in M1, else fidelity is only near-identical and the coal-mask plane can flip boundary pixels.
2. **player_ctl.c static globals** (dig, rc_delay, server-motion shadow, cursor; player_ctl.c:27-54) — single-player assumptions; the batched port must per-envify, and snapshot must capture or quiesce them. Same for `rl_cache*` statics in rl_mode.c (harmless — obs only).
3. **Sprint can self-trigger** without the sprint key (player_ctl.c:293-299) — must be ported or positions diverge after any forward release/press within 7 ticks. Also food>6 gate -> vitals (exhaustion from jumps, gm_vitals_apply player_ctl.c:57-96) must be ported (cheap, keeps bit-exact health/food).
4. **`s_hurt_vel_reset` / server-motion shadow** (player_ctl.c:49-50, 355-362): only triggers on damage; learned stage can take fall damage in a shaft -> port the reset + 8000-quantization (player_ctl.c:359-360) or positions diverge after any fall >3 blocks.
5. **Fluids not simulated**: a burrow hitting lava diverges. Mitigation: snapshot loader flags liquids in region; those (seed,stage) pairs fall back to real-env-only or are excluded.
6. **No RNG in the learned-stage path** — confirmed: physics, dig, drops (positional hash), item physics are all RNG-free. The only live RNG is plant growth (live_sim.c:119) and eye-of-ender (runtime.c:182) — out of scope.
7. **Float discipline**: `-ffp-contract=off` (CC) / `--fmad=false` (nvcc) are mandatory (blaze/SPEC.md:46-47,139,156-157; magma/Makefile:7-8). Use `MC_NOINLINE` on any large ported function to keep nvcc compile times sane (mc.h:11-16).
8. **No in-kernel malloc / big device-stack frames** — the tick kernel must keep `McAABB blocks[PSV_MAX_BLOCKS]` (512 x 48B = 24KB) OFF the stack -> per-env scratch in the global pool, indexed by env id.
9. **Recenter/window semantics**: `psv_physics_tick` runs in window-local coords in the real env (ox/oz offsets, runtime.c:23-36). The batched env should run in region-local doubles with the same origin discipline; safest is to use the SAME local origin the real env had at snapshot time, stored in the .bsnp, and never recenter within an episode (double math is translation-sensitive). Verify a coordinate-offset equivalence case explicitly in M1.
10. **Shared box**: GPU0 also serves other workloads — device index parameter, no `cudaDeviceReset`, bounded memory (`N` configurable).
