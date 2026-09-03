# Design: Dimensions and Region Swapping in Blaze (CPU first, CUDA mirror)

Author: Gemini (netherite session 2026-09-03). Reviewer: Fable. Status: agreed spec for dimension identity and portal transfer on the port-matrix `portals_dimensions` and `nether_route` rows.

## Goal and non-goals

Goal: full dimension support in blaze across N envs (1024 typical), bit-equal to the verified magma CPU reference (`magma/game/portal_live.c`, `magma/game/runtime.c:1601-1646`, `magma/game/world_live.c:430-472`). Same gate standard as every other port row: magma-vs-blaze-CPU lockstep digest equality (M1), blaze-CPU-vs-CUDA bitwise (M2). Gate runner: `blaze/env/port_matrix.py` over `blaze/env/port_matrix.yaml`.

Non-goals: allocating 3 active regions per env (prohibited by memory budget); simulating multiple dimensions concurrently within one env; dragon-fight RL (out of scope per `docs/GATES.md`); custom dimension generation outside magma's verified chunk providers (`chunk_provider_nether.h`, `chunk_provider_end.h`).

## Memory constraint and data layout

Blaze allocates flat per-env pools sized for one active region volume ($rnx \times rny \times rnz$, standard 128x128x128 = 2,097,152 cells, ~6.31 MB per env):
- `cells`: $2 \times rvol$ bytes (4.19 MB)
- `light`: $1 \times rvol$ bytes (2.10 MB)
- `biome`: $rnx \times rnz$ bytes (16.38 KB)
- `grass_sec`: covering section grid (16 KB)
- `window`: 9x9 chunk physics window ($9 \times 9 \times 65,536$ u16 states, ~10.6 MB pooled)

Allocating three active regions per env would consume >18.9 MB per env in VRAM. At N=1024 envs, that would demand ~19.4 GB just for voxel storage, exceeding device headroom and reducing max training batch size.

Therefore, **each env keeps exactly ONE active region** in memory, plus explicit dimension tracking fields:
- `dimension`: `int32_t` (0 = Overworld, -1 = Nether, 1 = The End; matches Oracle `DimensionType.java:8-10`)
- `portal_time`: `int32_t` (accumulated continuous contact ticks with portal blocks; triggers transfer at 82 ticks)
- `portal_cooldown`: `int32_t` (ticks before transit can trigger again; initialized to 100 on transit, refreshed to 100 while remaining in portal pane)
- `dim_bank`: pointer to the per-dimension snapshot bank holding pre-baked `.bsnp` regions for that world seed (`CuSnapshot[3]`, index 0 = Nether, 2 = End). Overworld is not stored here.
- `dim_ow`: pointer to this env's own assigned overworld snapshot. Returning from the Nether reloads that snapshot, never a shared `snaps[0]`.

Bank paths are named explicitly. `BlazeCreateOpts.nether_bank` / `end_bank` win if set at `blaze_create`. Otherwise `blaze_load_snapshots` reads a sidecar `<overworld>.bsnp.banks` next to the first loaded snapshot (`nether=` / `end=` relative to that directory). A named path that is missing fails closed (`nether bank missing: PATH`). Filename rewriting (`strstr(..., "portals.bsnp")`) is not used.

## State partitioning across a dimension swap

When an env transits dimensions, state is cleanly partitioned into persistent (cross-swap) state and dimension-local (cleared or swapped) state.

### State that crosses the swap (persistent)
- **Player vitals**: health, foodLevel, saturation, exhaustion, foodTimer (`PsvPlayer` / `PvStats`).
- **Player inventory**: all 37 slots (`inv[37][3]` 36 main + offhand), hotbar selection (`hotbar_sel`), cursor stack (`cursor`), crafting stats (`parity_craft_attempts`, `parity_craft_successes`).
- **Player vitals & status**: `fire`, `air`, active potion effects (`potions[PSV_POTION_MAX]`, `n_potions`).
- **World clock & time**: `tick`, `ww.worldTime`, `ww.totalTime`, daylight cycle status (`magma/game/runtime.c:2275-2283`).
- **Weather state & timers**: `ww.rainTime`, `ww.thunderTime`, `ww.raining`, `ww.thundering`, `rain_strength`, `thunder_strength` (`magma/game/world_live.c:1442`, `blaze/core/world_weather.h`).
- **RNG cursors**: `world_rand.seed` (Oracle `World.java:108` 48-bit JavaRandom cursor), `update_lcg` (Oracle `World.java:95`).
- **Player orientation**: `yaw` and `pitch` are preserved across the portal (`magma/game/runtime.c:1639`, Oracle `EntityPlayerMP.java:633`).

### State that does NOT cross the swap (swapped or reset)
- **World blocks & lighting**: `cells`, `light`, `biome`, `grass_sec` are overwritten with the destination dimension's region data from the snapshot bank.
- **Physics window & candidate caches**: `window` chunks refilled around the new destination origin; `coal_cand`, `ore`, `nore`, `ore_xy` swapped to destination dimension ore tables.
- **Living entities (mobs)**: `n_mobs = 0`; living mobs do not traverse portals in the verified magma reference (`magma/game/runtime.c:1622`, `1640`: `gm_mobs_init(&r->mobs, r->seed ^ (long long)nd)`).
- **Ground items**: `n_items = 0`, `n_overflow = 0` (`magma/game/runtime.c:1622`, `1641`: `memset(&r->entities, 0, sizeof r->entities)`).
- **Projectiles**: active arrows/fireballs zeroed (`memset(e->projectiles, 0, ...)`).
- **Falling blocks**: `n_falls = 0`.
- **Fluids**: `fluid_dim = nd`; active cellular automaton fluid regions reset.
- **Open containers**: closed (`container = 0`, `container_wx/wy/wz = 0`).
- **Mining/digging progress**: cancelled (`dig_hitting = 0`, `dig_progress = 0.0f`).
- **Portal transit state**: `portal_time = 0`, `portal_cooldown = 100` (`magma/game/runtime.c:1624`, `1642`).

## The swap procedure

1. **Ignition follower**:
   When a fire block (id 51) is placed adjacent to an obsidian frame, `cu_world_set_state` triggers `gm_portal_ignite` (`magma/game/runtime.c:1433`, `magma/game/portal_live.c:9-28`). It evaluates frame validity via `np_try_spawn_portal` (`blaze/core/nether_portal.h:280`) and spawns portal blocks (id 90, meta 1 or 2).

2. **Contact accumulation & cooldown**:
   Each tick, `feet` and `head` block IDs are checked:
   - Feet block at `(floor(px + ox), floor(py), floor(pz + oz))`
   - Head block at `(floor(px + ox), floor(py + 1.0), floor(pz + oz))`
   - If `feet == 90 || head == 90`:
     - If `portal_cooldown > 0`: `portal_cooldown = 100` (refreshes cooldown while standing in portal pane; Oracle `Entity.java` `setPortal`, `magma/game/runtime.c:1609`).
     - If `portal_cooldown == 0` and `dimension != 1`:
       - `portal_time++`. If `portal_time >= 82`: transit triggers (`magma/game/runtime.c:1630`).
   - If `feet != 90 && head != 90`: `portal_time = 0`.

3. **End portal contact**:
   - If `(feet == 119 || head == 119)` and `dimension == 0`:
     - Transit is instantaneous (`portal_time` not required; Oracle `EntityPlayerMP.java:669`, `magma/game/runtime.c:1612-1625`).
     - Target dimension is 1. Player teleported to fixed End spawn platform `(100.5, 49.0, 0.5)` facing `yaw = 90.0f, pitch = 0.0f`.
     - 5x5 obsidian platform ensured at $y=48$ ($x \in [98, 102], z \in [-2, 2]$) with 3 blocks of air above ($y \in [49, 51]$).
   - If `(feet == 119 || head == 119)` and `dimension == 1`:
     - If dragon death processed: `credits = 1`, `won = 1` (`magma/game/runtime.c:1610-1611`).

4. **Nether coordinate scaling & portal search/placement**:
   - When entering Nether (`nd = -1`): $scale = 0.125$ ($1/8$). Target $nx = \lfloor v.x \times 0.125 \rfloor$, $nz = \lfloor v.z \times 0.125 \rfloor$ (`magma/game/runtime.c:1635-1636`, Oracle `PlayerList.java:659`).
   - When entering Overworld (`nd = 0`): $scale = 8.0$. Target $nx = \lfloor v.x \times 8.0 \rfloor$, $nz = \lfloor v.z \times 8.0 \rfloor$.
   - `gm_portal_find_or_make` (`magma/game/portal_live.c:30-63`):
     - Scans radius 128 for existing portal block 90. If found, targets `(x + 0.5, y, z + 0.5)`.
     - If not found within region: locates solid ground in radius 16, creates standard 4x5 obsidian frame with portal blocks, and targets `(bx + 1.5, by, bz + 0.5)`.

5. **Region copy from snapshot bank**:
   - Source buffers for target dimension `nd` copied into active env:
     - `memcpy(env->cells, bank->cells[nd], vol * sizeof(u16))`
     - `memcpy(env->light, bank->light[nd], vol * sizeof(u8))`
     - `memcpy(env->biome, bank->biome[nd], bvol * sizeof(u8))`
   - Environment bounds updated: `rx0`, `ry0`, `rz0`, `rnx`, `rny`, `rnz`, `rvol`.
   - Player position updated to destination coordinates; `cu_recenter(env)` rebuilds physics window.
   - `env->dimension = nd`, `env->portal_cooldown = 100`, `env->portal_time = 0`.

## Snapshot format decision (keep v11; no v12 bump)

Decision: **Remain on snapshot version 11; do not bump to v12.**

Rationale:
1. **Per-dimension snapshot banks use standard modular .bsnp files**: Each dimension region (Overworld, Nether, The End) is saved as an independent, fully valid `.bsnp` v11 snapshot file baked by magma for the identical world seed.
2. **Preserves existing struct layouts and tooling**: Modifying `RlSnapHead` would change `sizeof(RlSnapHead)` and break binary compatibility with all existing fixtures (`s10_t0_r64_no_liquid.bsnp`, etc.) and the verified `blaze_snapshot_load` parser. Appending a v12 trailer is unnecessary because the snapshot bank associates dimension identity with the file slot.
3. **Clean separation of concerns**: An individual `.bsnp` represents a static spatial region of voxels, light, and biomes. Dimension identity is an active runtime environment property, not an intrinsic property of a voxel grid.

## Acceptance fixture and port matrix

The acceptance fixture for `portals_dimensions` requires:
- An overworld snapshot `verify/fixtures/port/s10_t0_r64_portals.bsnp` (seed 10, v9) with a lit Nether portal frame (block 90) at z=11, player at (8.5, 65.0, 10.0).
- A matching Nether snapshot `verify/fixtures/port/s10_t0_r64_nether.bsnp` (seed 10, v11, tick 90) region (-63,0,-63) 128^3, arrival (1.5, 103.0, 1.5). Sibling named by `s10_t0_r64_portals.bsnp.banks`.
- An action tape `blaze/rl/fixtures/portals_s10.json` (170 actions): 10 forward into the overworld pane, 80 idle through the 82-tick transfer (swap at observation 87), 20 forward +Z out of the arrival pane onto netherrack, 60 idle in dimension -1.
- M1 lockstep check: `player,portals,dimensions,world,random_ticks` must be bit-equal between `magma_game` and `blaze_cpu.so` on every tick, including after the swap.

Bake (byte-identical to the committed files; `make -C blaze/rl bake-portals-dimensions`):

```
out/blaze/rl/test_portals_dimensions --write-fixture \
    verify/fixtures/port/s10_t0_r64_placement.bsnp \
    verify/fixtures/port/s10_t0_r64_portals.bsnp

# 10 forward + 80 idle, dump snapshot_r=64 at tick 90
{ i=0; while [ $i -lt 10 ]; do echo '{"forward":1.0}'; i=$((i+1)); done
  i=0; while [ $i -lt 80 ]; do echo '{}'; i=$((i+1)); done
  echo '{"snapshot":"verify/fixtures/port/s10_t0_r64_nether.bsnp","snapshot_r":64}'
} | magma/magma_game --rl-bin --render off --pace unlimited --seed 10 --mobs off \
    --snapshot-in verify/fixtures/port/s10_t0_r64_portals.bsnp
```

Rebake 2026-09-03 matched committed hashes: overworld sha256 `0260d2908ab8c73dfe0b63210efa5c5a892ea94b55a2f4d2ebf9fc80b08e1621` (6458852 bytes), nether sha256 `4e0dd2640a6a9a31d818fddcaaf00d0f2a71843b9ec4e5bca766370d26e4dd62` (6316584 bytes).

## Citations to oracle-src and magma

- `oracle-src/net/minecraft/world/DimensionType.java:8-10, 27-35`: Dimension IDs (OVERWORLD 0, NETHER -1, THE_END 1).
- `oracle-src/net/minecraft/entity/player/EntityPlayerMP.java:641-686`: `changeDimension` lifecycle, invulnerableDimensionChange, portal stat triggers.
- `oracle-src/net/minecraft/server/management/PlayerList.java:621-733`: `transferPlayerToDimension` and `transferEntityToWorld`, coordinate factor 8.0D / 0.125D.
- `oracle-src/net/minecraft/world/Teleporter.java:32-70`: `placeInPortal`, search radius 128 in `placeInExistingPortal`, fallback `makePortal`.
- `oracle-src/net/minecraft/world/WorldProviderHell.java:17-22`: `doesWaterVaporize = true`, `hasNoSky = true`.
- `oracle-src/net/minecraft/world/WorldProviderEnd.java:23-28`: `hasNoSky = true`, celestial angle calculation.
- `oracle-src/net/minecraft/world/World.java:2529-2532`: `getProviderName`.
- `magma/game/portal_live.c:9-28`: `gm_portal_ignite` (NP_DIM local box).
- `magma/game/portal_live.c:30-63`: `gm_portal_find_or_make` (radius 128 scan, radius 16 fallback frame creation).
- `magma/game/portal_live.c:65-99`: `gm_end_portal_insert_eye`.
- `magma/game/runtime.c:1433`: fire block 51 ignition follower.
- `magma/game/runtime.c:1601-1646`: portal contact checking, 82-tick countdown, scale 0.125/8.0, dimension change, End portal platform.
- `magma/game/runtime.c:2261-2273`: `gm_runtime_set_dimension`.
- `magma/game/world_live.c:430-472`: `gm_world_create_type` (0=overworld, 2=nether, 3=end).
- `magma/world/light.c:204-238`: `light_create_type` dimension light configurations.

## TODO list for CUDA-side plumbing

When merging with the CUDA backend (`blaze/env/blaze_cuda.cu`):
0. **Host paths are already stashed**: `blaze_create` copies `BlazeCreateOpts.nether_bank` / `end_bank` into `CuVecCu`. `h_envs[i].dim_bank` stays NULL, so a CUDA transit fail-closes. `portals_dimensions` M2 is BLOCKED: "CUDA swap copy from bank not implemented".
1. **Device bank allocation**: Allocate host-pinned or device memory for inactive dimension snapshot banks (`cudaMalloc` or `cudaMallocHost`). Load the named banks the same way `blaze_cpu.c` does (create opts, then `<snap>.banks` sidecar).
2. **Asynchronous DMA swap**: On dimension transit, execute `cudaMemcpyAsync` to replace device `cells`, `light`, and `biome` pools from host snapshot bank.
3. **Window re-anchoring kernel**: Trigger `k_recenter` after host copies destination origin `rx0, ry0, rz0`. Rebuild `grass_sec` after the copy.
4. **Lane convergence**: Portal transfer is an env-level branch; lanes in the env's warp sync on swap completion before resuming `k_tick`.
5. **Per-env overworld pointer**: `dim_ow` must alias that env's assigned overworld snapshot, not a shared `snaps[0]`.
