# Games/Minecraft legacy — consolidated learnings

> **Location:** `mc-1.11.2-env/docs/legacy-games-minecraft-learnings.md`  
> Archived consolidation of first-party markdown from `~/games/minecraft` (flashmine, mc-oracle, netherite*, archives).  
> Living product docs remain under `c/craster/`, `c/mc-sim/`, `c/render-opt/`, and root `CLAUDE.md` / `DEVLOG.md`.


Generated: 2026-07-12 10:33 UTC
Source root: `anvil:~/games/minecraft`
Living mainline is **not** this tree: `anvil:~/dev/minecraft/mc-1.11.2-env`.

This file concatenates first-party markdown from the old Minecraft RL experiments (flashmine, mc-oracle, netherite*, archives). Vendor/venv/legal docs omitted.

## Executive synthesis (agent summary)

### What this tree was
A sequence of attempts to get **Minecraft into high-throughput RL**:

1. **mc-oracle** — instrument vanilla 1.7.10 (MCP) as differential truth (JSONL ticks). Not an env.
2. **netherite (legacy)** — subtract 1.7.10 Java to beat-the-game path; C engine + CUDA batch; PPO/dragon claims.
3. **netherite-v2 / v2-bench** — abandon custom raster; drive **real 1.20.1 + Sodium** via shmem PBO mod.
4. **flashmine** — pure **batched CUDA 1.7.10** tick+render on Blackwell (sm_120); trainability = mine-a-tree PPO.
5. **archive/backups** — frozen tarballs and 1.7.10 native-port experiments.

### Hard learnings that transfer to mc-1.11.2-env
- **Goldens = real MC only.** Port-vs-port and synthetic goldens lie.
- **Reimplementing the OpenGL renderer is a trap** (netherite-v2 pivot): hundreds of features (mobs, water, particles, HUD); 93% static-block match still unbounded bugs.
- **Oracle must be per-stage**, not only final RGB (flashmine + mc-oracle).
- **Determinism**: Philox/counter RNG keyed by `(env_id, tick, subsystem)`; no global `Random`.
- **Tick order must match vanilla**; document every intentional divergence.
- **Sparse/paletted chunks** matter for B=1024 (flashmine: ~21–83x speedup, ~6x less mem).
- **Java strip (subtraction) inherits correctness** better than greenfield for physics.
- **JVM envs** can be fast enough if draw-call bound (Sodium + async PBO) — different product than pure CUDA sim.
- **Copyright**: decompiled Mojang source is private; never public GitHub.
- **sm_120-only** builds on anvil Blackwell for CUDA sim work.

### Status vs mainline
`mc-1.11.2-env` is the active monorepo (playable 1.11.2 + render-opt + mc-sim + craster). This document preserves the older 1.7.10/1.20.1 strategy docs so they can be deleted from disk later without losing the rationale.


## Table of contents

1. [`archive/gtnh-1.7.10-mdk.README.md`](#doc-1)
2. [`archive/netherite-v0-1.8.9.README.md`](#doc-2)
3. [`archive/netherite-v1-archive.README.md`](#doc-3)
4. [`backups/minecraft-1.7.10-repo/20260421T065011Z/repo/AGENTS.md`](#doc-4)
5. [`backups/minecraft-1.7.10-repo/20260421T065011Z/repo/CLAUDE.md`](#doc-5)
6. [`backups/minecraft-1.7.10-repo/20260421T065011Z/repo/README.md`](#doc-6)
7. [`backups/minecraft-1.7.10-repo/20260421T065011Z/repo/SPEC.md`](#doc-7)
8. [`flashmine/CLAUDE.md`](#doc-8)
9. [`flashmine/SPEC.md`](#doc-9)
10. [`flashmine/docs/cuda_perf_log.md`](#doc-10)
11. [`flashmine/docs/forge_correction_report.md`](#doc-11)
12. [`flashmine/docs/jdk_drift_report.md`](#doc-12)
13. [`flashmine/src/oracle/README.md`](#doc-13)
14. [`flashmine/src/render/oracle_README.md`](#doc-14)
15. [`flashmine/tests/fuzz_failures/README.md`](#doc-15)
16. [`flashmine/tests/golden/README.md`](#doc-16)
17. [`flashmine/tools/oracle-mdk-1.7.10/README.md`](#doc-17)
18. [`flashmine/tools/oracle-mdk-1.7.10/REQUIREMENTS.md`](#doc-18)
19. [`flashmine/tools/oracle-mdk-1.7.10/docs/FAQ.md`](#doc-19)
20. [`flashmine/tools/oracle-mdk-1.7.10/docs/migration.md`](#doc-20)
21. [`flashmine/tools/oracle-mdk-1.7.10/docs/porting.md`](#doc-21)
22. [`flashmine/tools/oracle-render-mdk-1.7.10/README.md`](#doc-22)
23. [`flashmine/tools/oracle-render-mdk-1.7.10/REQUIREMENTS.md`](#doc-23)
24. [`flashmine/tools/oracle-render-mdk-1.7.10/docs/FAQ.md`](#doc-24)
25. [`flashmine/tools/oracle-render-mdk-1.7.10/docs/migration.md`](#doc-25)
26. [`flashmine/tools/oracle-render-mdk-1.7.10/docs/porting.md`](#doc-26)
27. [`flashmine/vanilla/README.md`](#doc-27)
28. [`mc-oracle/CLAUDE.md`](#doc-28)
29. [`mc-oracle/DESIGN.md`](#doc-29)
30. [`mc-oracle/SPEC.md`](#doc-30)
31. [`mc-oracle/cleanroom/random/README.md`](#doc-31)
32. [`netherite-macbook/netherite/AGENTS.md`](#doc-32)
33. [`netherite-macbook/netherite/CLAUDE.md`](#doc-33)
34. [`netherite-macbook/netherite/README.md`](#doc-34)
35. [`netherite-macbook/netherite/SPEC.md`](#doc-35)
36. [`netherite-v2-bench/AGENTS.md`](#doc-36)
37. [`netherite-v2-bench/CLAUDE.md`](#doc-37)
38. [`netherite-v2-bench/README.md`](#doc-38)
39. [`netherite-v2-bench/SPEC.md`](#doc-39)
40. [`netherite-v2-bench/csrc/cuda_interop/README.md`](#doc-40)
41. [`netherite/CLAUDE.md`](#doc-41)
42. [`netherite/GRAALVM_FEASIBILITY.md`](#doc-42)
43. [`netherite/README.md`](#doc-43)
44. [`netherite/SPEC.md`](#doc-44)
45. [`netherite/legacy/AGENTS.md`](#doc-45)
46. [`netherite/legacy/CLAUDE_legacy.md`](#doc-46)
47. [`netherite/legacy/TESTING.md`](#doc-47)

---


# Doc 1: `archive/gtnh-1.7.10-mdk.README.md` {#doc-1}

*Absolute path: `/home/infatoshi/games/minecraft/archive/gtnh-1.7.10-mdk.README.md`*

# gtnh-1.7.10-mdk

Archived 2026-04-18 from anvil ~/1.7.10.

GTNewHorizons ExampleMod 1.7.10 MDK -- Minecraft 1.7.10 + Forge
mod development kit. Used as an oracle for porting MC behavior
into CUDA simulations (Netherite project): deobfuscated MC source
at build/rfg/minecraft-src/java/ (1833 .java files, 23 MB) is
the 'ground truth' to grep against.

Custom content (yours):
- REQUIREMENTS.md (detailed setup guide, references simulation port)
- setup-and-launch.sh, make-bundle.sh
- run-smoke.log

Template boilerplate:
- src/main/java/com/myname/mymodid/ (just example mod scaffold)
- build.gradle.kts, dependencies.gradle, repositories.gradle
- LICENSE-template, CODEOWNERS, jitpack.yml
- README.md (upstream GTNH template)

Excluded from archive (regenerable):
- mdk/gradle-home/ (2.4 GB Gradle dep cache) -- re-downloaded by
  ./gradlew setupDecompWorkspace on first run (~270 MB, 5-15 min)

Included: everything else including mdk/build/ (has the expensive
Fernflower-decompiled MC source -- 23 MB, but regenerating takes
5-15 min + network).

2.5 GB original -> ~100 MB after exclusions -> 36 MB zstd. zstd -t
integrity verified.

Restore: zstd -d gtnh-1.7.10-mdk.tar.zst -o - | tar -xf -
Then: cd 1.7.10/mdk && ./gradlew setupDecompWorkspace


# Doc 2: `archive/netherite-v0-1.8.9.README.md` {#doc-2}

*Absolute path: `/home/infatoshi/games/minecraft/archive/netherite-v0-1.8.9.README.md`*

# netherite-v0 (MC 1.8.9)

Archived 2026-04-18 from macbook ~/1.8.9.

Earliest Minecraft project iteration. Predecessor to v1 (now in netherite-v1-archive.tar.zst) and v2 (active at ~/netherite, github.com/Infatoshi/netherite).

Stack: MC 1.8.9 + Forge + MCP 1.7.10 + CUDA rasterizer + Python physics sim + JS oracle bot.

State at archive time: single initial commit (aa83e9b), no git remote, 85 dirty files, 41k+ untracked files, no .gitignore.

Excluded from archive (regenerable):
- oracle/bot/node_modules/ (803 MB -- restore via 'npm install' in oracle/bot/)
- forge-workspace/build/ (Gradle build artifacts)
- __pycache__/, *.pyc
- .pytest_cache/
- netherite.egg-info/

Included: all source (simulation/, cuda-rasterizer/, oracle-mod/, oracle/ minus node_modules), forge-workspace source, mcp_1.7.10/, tests/, validation/, rl-env/, tasks/, docs, demos (demo_4x4*.mp4), specs (SPEC.md, CLAUDE.md, SETUP_LOG.md, setup-prompt.md, extracted-specs/).

1.8 GB original -> 1.07 GB after exclusions -> 363 MB zstd. zstd -t integrity verified.

Restore: zstd -d netherite-v0-1.8.9.tar.zst -o - | tar -xf -
Then: cd 1.8.9/oracle/bot && npm install


# Doc 3: `archive/netherite-v1-archive.README.md` {#doc-3}

*Absolute path: `/home/infatoshi/games/minecraft/archive/netherite-v1-archive.README.md`*

# netherite-v1-archive

Archived 2026-04-18 from macbook ~/netherite-v1-archive.

Custom CUDA rasterizer approach (v1) for Minecraft. Pivoted away from --
replaced by v2 in ~/netherite (MC 1.20.1 + Sodium + Fabric + tiny mod).

Contains: Forge workspace, mc-src (MC source), csrc (CUDA rasterizer),
log_trajectories*.json (4 replay logs), weights_iter300_seed1337.bin
(trained RL weights), fix_entities.py, fix_renderblocks.py, oracle/, TESTING.md.

Not a git repo. 1.1 GB uncompressed -> 245 MB zstd. zstd -t integrity verified.

To restore: zstd -d netherite-v1-archive.tar.zst -o - | tar -xf -

Context also in memory/netherite-cuda-rasterizer-status.md.


# Doc 4: `backups/minecraft-1.7.10-repo/20260421T065011Z/repo/AGENTS.md` {#doc-4}

*Absolute path: `/home/infatoshi/games/minecraft/backups/minecraft-1.7.10-repo/20260421T065011Z/repo/AGENTS.md`*

# Project Agent Notes

## Current Status

- This repo is the runnable Java reference workspace for Minecraft 1.7.10 using MCP `stable_12`.
- Editable reference code lives in `mc/src`; reference assets live in `mc/assets`.
- Runtime state lives in `.run`.
- `run/natives` is a compatibility symlink to `.run/natives` because the launcher still hardcodes that path.

## Reference Isolation

- Treat the repo root as the protected oracle workspace.
- Do not run disposable parallel experiments directly in the reference workspace.
- Create writable agent sandboxes with `./scripts/prepare_agent_workspace.sh <name>`.
- Prefer the stricter dispatch harness for parallel work:
  `uv run scripts/prepare_agent_dispatch.py --dispatch <name> --manifest <json>`
- Check oracle drift during or after a dispatch with:
  `uv run scripts/check_agent_dispatch.py --dispatch <name>`
- Sandboxes are created under `.agent-workspaces/<name>` and have their own `build/`, `.run/`, and `run/natives`.

## Build And Verification

- Build: `./gradlew build`
- Run client: `./gradlew runClient`
- Run server: `./gradlew runServer`
- Prepare strict dispatch: `uv run scripts/prepare_agent_dispatch.py --dispatch <name> --manifest <json>`
- Check dispatch drift: `uv run scripts/check_agent_dispatch.py --dispatch <name>`
- Configure native scaffold: `cmake -S native -B native/build`
- Build native scaffold: `cmake --build native/build`
- Test native scaffold: `ctest --test-dir native/build --output-on-failure`
- Python verification: `uv run pytest`
- Lint: `uv run ruff check . --fix`

## Domain Gotchas

- This is a Forge/FML-patched MCP workspace, not a pure vanilla source dump.
- The root `src/main/java/dev/workspace/package-info.java` placeholder must remain; removing it breaks the GTNH plugin setup.
- `mc/` is vendor reference code. Avoid automatic formatting there.
- The default `runClient` and `runServer` tasks are intentionally redirected to the Java 21 compatibility runtime.

## Native Port Rules

- The target is single-player vanilla behavior.
- Keep vanilla save compatibility first.
- Keep a logical client/server split even if the native build runs in one process.
- Use this Java repo as the oracle for fixtures, parity tests, and behavior checks.
- Extend the `native/` scaffold through existing public headers and module directories instead of inventing a second architecture.


# Doc 5: `backups/minecraft-1.7.10-repo/20260421T065011Z/repo/CLAUDE.md` {#doc-5}

*Absolute path: `/home/infatoshi/games/minecraft/backups/minecraft-1.7.10-repo/20260421T065011Z/repo/CLAUDE.md`*

# Project Context

## What This Repo Is

- Runnable Minecraft Java 1.7.10 reference workspace with MCP `stable_12` mappings.
- The repo root is the protected behavioral oracle for the planned native port.

## Where To Work

- Reference updates happen in the repo root only when intentionally changing the oracle.
- Disposable agent work happens in `.agent-workspaces/<name>`.
- Create a sandbox with `./scripts/prepare_agent_workspace.sh <name>`.
- Prefer strict multi-agent dispatches under `.agent-dispatches/<dispatch>/workers/<name>/workspace`.
- Create a dispatch with `uv run scripts/prepare_agent_dispatch.py --dispatch <name> --manifest <json>`.
- Check oracle drift with `uv run scripts/check_agent_dispatch.py --dispatch <name>`.

## Commands

- Build: `./gradlew build`
- Client: `./gradlew runClient`
- Server: `./gradlew runServer`
- Prepare strict dispatch: `uv run scripts/prepare_agent_dispatch.py --dispatch <name> --manifest <json>`
- Check dispatch drift: `uv run scripts/check_agent_dispatch.py --dispatch <name>`
- Configure native scaffold: `cmake -S native -B native/build`
- Build native scaffold: `cmake --build native/build`
- Test native scaffold: `ctest --test-dir native/build --output-on-failure`
- Verify: `uv run pytest`
- Lint: `uv run ruff check . --fix`

## Constraints That Matter

- Keep the reference workspace runnable at all times.
- Do not remove the `src/main/java/dev/workspace/package-info.java` placeholder.
- `run/natives` must keep resolving to `.run/natives`.
- Treat Forge/FML as part of the reference workspace, not something to strip out here.
- Native-port planning target is single-player vanilla with vanilla save compatibility first and a logical client/server split.
- The sanctioned native baseline is the `native/` C scaffold with opaque module boundaries and one-way dependencies.


# Doc 6: `backups/minecraft-1.7.10-repo/20260421T065011Z/repo/README.md` {#doc-6}

*Absolute path: `/home/infatoshi/games/minecraft/backups/minecraft-1.7.10-repo/20260421T065011Z/repo/README.md`*

# Minecraft 1.7.10 MCP Dev Workspace

This repository is set up as a vanilla-first Minecraft Java 1.7.10 development workspace using MCP `stable_12` mappings. The editable Minecraft source and resources live in the repo, not under `build/`, so you can change code or assets and rebuild directly from a persistent tree.

## Commands

Build:

```bash
./gradlew build
```

Run client:

```bash
./gradlew runClient
```

Run server:

```bash
./gradlew runServer
```

Prepare an isolated writable workspace for an agent:

```bash
./scripts/prepare_agent_workspace.sh agent-01
cd .agent-workspaces/agent-01
./gradlew build
```

Prepare a strict multi-agent dispatch with per-worker sandboxes and manifests:

```bash
uv run scripts/prepare_agent_dispatch.py \
  --dispatch native-contracts \
  --manifest scripts/agent_dispatch_manifest.example.json
uv run scripts/check_agent_dispatch.py --dispatch native-contracts
```

Configure and build the native C scaffold:

```bash
cmake -S native -B native/build
cmake --build native/build
ctest --test-dir native/build --output-on-failure
```

The default `runClient` and `runServer` tasks are redirected to the Java 21 compatibility runtime, which loads lwjgl3ify and Hodgepodge. That keeps the 1.7.10 dev environment usable on a modern Linux desktop.

## Codebase Structure

`mc/src/`
: Editable decompiled Minecraft and Forge Java source. This is the main tree you change when modifying vanilla 1.7.10 behavior.

`mc/assets/`
: Editable Minecraft and Forge resources, including textures, language files, sounds, and metadata used by the patched game.

`src/main/`
: Optional project-owned mod code and resources. This workspace is effectively a clean slate. The only file left there is a minimal `package-info.java` placeholder so the GTNH build plugin can resolve the declared `modGroup`.

`addon.late.gradle.kts`
: Project-specific build overrides. This file is what makes the workspace persistent:
- points `patchedMc` at `mc/`
- keeps setup tasks from overwriting that tree once it exists
- redirects legacy `runClient` and `runServer` to the Java 21 compatibility tasks
- excludes `mc/src` from Spotless so formatter checks only touch handwritten project code

`gradle.properties`
: Core build configuration. The important settings here are the Minecraft version, Forge version, MCP mapping channel/version, and project identity used for produced jars.

`dependencies.gradle` and `repositories.gradle`
: Where additional mod dependencies or repositories would go if you decide to layer your own mod code on top of the workspace later.

`.run/`
: Local runtime data for launches. This includes client options, server properties, logs, natives, and world/config state created during dev runs.

`.agent-workspaces/`
: Disposable writable snapshots for parallel agents. Each workspace has its own `build/`, `.run/`, and `run/natives` compatibility link so agents can build and launch without touching the reference workspace.

`.agent-dispatches/`
: Strict dispatch harness output. Each dispatch gets a root manifest, an oracle git-status snapshot, and per-worker subdirectories containing a pinned assignment manifest, generated worker prompt, and isolated workspace copy.

`native/`
: C-only baseline scaffold for the eventual native port. It enforces a one-process logical client/server split with fixed module boundaries, a standalone CMake build, and a smoke test future agents can extend.

`tests/test_workspace.py`
: Lightweight verification checks that assert the workspace layout and build wiring stay in the expected state.

## Notes

- `mc/` is vendor code. It is intended for editing, but not for automatic formatting.
- `.run/client/mods` is empty by default. The runtime still loads compatibility support mods from Gradle dependencies, not from loose mod jars in that folder.
- The generated artifacts in `build/libs/` are just launch/build outputs for the workspace; this repo is not currently set up as a gameplay mod project.
- Treat this repo root as the reference oracle. Parallel agent experiments should happen in `.agent-workspaces/<name>`, not in the reference workspace.
- For stricter parallel work, use `.agent-dispatches/<dispatch>/workers/<name>/workspace` instead of ad hoc sandboxes. That path comes with an assignment manifest and an oracle drift check.
- The native scaffold is intentionally rigid. Agents should extend module internals behind the existing public headers instead of inventing new top-level layout or bypassing the client/server boundary.


# Doc 7: `backups/minecraft-1.7.10-repo/20260421T065011Z/repo/SPEC.md` {#doc-7}

*Absolute path: `/home/infatoshi/games/minecraft/backups/minecraft-1.7.10-repo/20260421T065011Z/repo/SPEC.md`*

# Native Port Reference Spec

## Purpose

This repository is the behavioral oracle for a long-horizon native rewrite of Minecraft Java 1.7.10.
It is not the native port itself. The Java workspace must stay runnable so future agents can build it,
launch it, and compare native behavior against it.

## Scope

- Target product: single-player vanilla gameplay.
- Source of truth: the MCP-mapped Java workspace in this repository.
- Non-goal for this repo: removing Forge/FML from the reference workspace.

## Reference Workspace Laws

1. Treat the repo root as the protected reference workspace.
2. Do not use the reference workspace as a disposable scratch area for parallel experiments.
3. Parallel agent builds, launches, and destructive tests must run in isolated sandboxes, preferably under `.agent-dispatches/<dispatch>/workers/<name>/workspace`.
4. Changes to the reference workspace should only be intentional updates to the oracle or its build wiring.

## Dispatch Harness Laws

1. Multi-agent work should start from a dispatch manifest, not from free-form prompts.
2. Each worker gets a named sandbox, a pinned ownership manifest, and a generated worker prompt.
3. The dispatch records the oracle git status at creation time so drift can be checked later.
4. Worker ownership is by explicit repo-relative path list. Anything not listed is outside the worker write scope.
5. `.agent-workspaces/<name>` remains available as a low-level helper, but `.agent-dispatches/...` is the preferred stricter path.

## Behavior Laws For The Native Port

1. Preserve vanilla save compatibility first.
   Use vanilla NBT and Anvil region files before considering any native-only save format.
2. Preserve the logical client/server split.
   Single-player may run in one process, but client presentation and server authority should stay separate modules.
3. Preserve behavior, not Java class shape.
   The Java tree is the oracle. The native implementation does not need to mirror Java inheritance or file layout.
4. Treat Forge/FML as reference-only scaffolding unless a vanilla behavior depends on it.
5. Start CPU-first.
   CUDA is optional acceleration for stable, measured hotspots later. It is not the initial rendering or architecture plan.

## Parallelization Laws

1. Parallelize by subsystem contracts, not by arbitrary file slices.
2. Freeze key interfaces early: save IO, chunk storage, entity handles, server snapshots, allocator rules.
3. Require fixtures and golden tests for each subsystem so future agents can resume work without hidden context.
4. Keep the Java workspace buildable and runnable at all times so it remains a valid oracle.

## Baseline Native Architecture

The sanctioned starting point is the C-only scaffold under `native/`.

### Process Model

- One executable.
- Logical client/server split in one process.
- Server owns authoritative world state and publishes immutable snapshots.
- Client owns input collection and presentation-facing state.
- Render consumes snapshots only. It does not reach into server state.

### Public Module Layout

```text
native/include/mc/
  core.h
  platform.h
  nbt.h
  region.h
  game_data.h
  world.h
  sim.h
  snapshot.h
  server.h
  client.h
  render.h
  app.h
```

```text
native/src/
  core/
  platform/
  io_nbt/
  io_region/
  game_data/
  world/
  sim/
  snapshot/
  server/
  client/
  render/
  app/
```

### Dependency Rules

- `core` is the only shared foundation.
- `server` may depend on `world`, `sim`, `game_data`, `snapshot`, and IO modules.
- `client` may depend on `snapshot` and `platform`.
- `render` may depend on `snapshot` and `platform`.
- `render` must not depend on `world` or `sim`.
- Cross-module interaction happens through public headers and opaque types only.

### Ownership Rules

- Public headers expose opaque structs and stable plain-data messages.
- Module internals stay in `native/src/<module>/`.
- Allocations route through `mc_allocator`.
- Handles and snapshots cross module boundaries; raw internal pointers do not.


# Doc 8: `flashmine/CLAUDE.md` {#doc-8}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/CLAUDE.md`*

# flashmine

Batched CUDA Minecraft 1.7.10 simulator + fused renderer for RL. See SPEC.md.
**Read SPEC.md "Status" section first** -- it has the canonical state of every
subsystem, the test gates, the known divergences, the canonical JVM, the
trace format version, the render-oracle launch incantation, and the
prioritized next-up list.

**Current focus: trainability path.** Render P4 closed out (best-effort).
Active work is the "mine 1 tree" loop: player entity -> dig action -> reward
+ termination -> tree-gen scenario -> tiny PPO -> policy-transfer test.
SPEC's "What's next" list reorders to put these first (items 1-7); mob AI,
action-scatter, redstone v3, render polish are deferred (items 8-12).

## Build target
- sm_120 only (RTX PRO 6000 Blackwell). Narrow compile, no multi-arch fatbins.
- CUDA 13.2 / nvcc 12.8 on anvil.

## Hard rules
- Determinism: counter-based RNG (Philox) keyed on `(env_id, tick, subsystem, slot)`. No global RNG state.
- Tick subsystem ordering must match vanilla 1.7.10 exactly. Document any deviation.
- Render kernel is read-only w.r.t. world state. No write-back.
- Fixed per-env caps (entities, scheduled ticks). No dynamic alloc in tick path.

## Verification
- Per-stage oracle, not single framebuffer diff. See SPEC.md "Verification".
- Bitwise for discrete (block id, light, face_id, texel). atol/rtol for floats.
- Every new kernel ships with a scenario test under `tests/scenarios/`.

## Layout
- `src/sim/`     tick subsystems
- `src/render/`  fused render kernel + stage buffers
- `src/oracle/`  vanilla harness, diff tools
- `kernels/`    raw .cu files
- `tests/scenarios/` named physics scenarios
- `tests/golden/`    fixed (seed, actions) -> trace hash
- `tools/`      coremod sources, dumpers


# Doc 9: `flashmine/SPEC.md` {#doc-9}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/SPEC.md`*

# flashmine

Batched, lockstep CUDA simulator + fused renderer for Minecraft 1.7.10, designed
for high-throughput RL. Disaggregates client/server: tick kernel and render
kernel run on overlapping streams over double-buffered world state.

## North Star

- **B = 1024+ environments** ticking in lockstep on a single RTX PRO 6000 Blackwell (96GB).
- **Tick kernel** advances world state `t -> t+1` while **render kernel** reads
  state `t` from the other buffer. Streams overlap; no host sync per tick.
- **Deterministic** given `(seed, action_sequence)`. Counter-based RNG (Philox)
  keyed on `(env_id, tick, subsystem, slot)`. No `Random.nextInt` global state.
- **Verified** against vanilla 1.7.10 via a per-stage oracle, not a single diff.

## Target Version: Minecraft 1.7.10

Pre-flattening. Block state is `(id: u8, meta: u4)`. Chunk section =
`[16,16,16]` of `(id, meta)`, trivially tensorizable as `[B, S, 16,16,16]`.
Pre-1.13 redstone (locally evaluable). Large RL ecosystem (Malmo, MineRL legacy)
for cross-checks.

Out of scope: data-driven blocks, paletted chunk containers, Mojang's post-1.18
lighting rewrite, datapacks.

## Architecture

### World Representation (per env)

- `blocks: u8[S, 16, 16, 16]`         block id
- `meta:   u4[S, 16, 16, 16]`         packed nibbles
- `block_light: u4[S, 16, 16, 16]`
- `sky_light:   u4[S, 16, 16, 16]`
- `entities: struct-of-arrays, fixed cap E_max per env, free-list managed`
- `tile_entities: SoA, fixed cap`
- `scheduled_ticks: ring buffer per env, fixed cap`
- World size capped per env (e.g. 8x8 chunks, superflat or fixed seed) so
  everything is VRAM-resident. No streaming.

### Double Buffering

```
buffer_A (state_t)  --read-->  render_kernel  --> framebuffer_t
                    --read-->  tick_kernel    --write--> buffer_B (state_t+1)
```

Two CUDA streams. `cudaEventRecord` on tick completion gates the next render;
`cudaEventRecord` on render completion gates the next tick's overwrite of A.

### Tick Subsystems (ordered, deterministic)

1. Action application (player intents -> entity velocity / break / place / use)
2. Entity tick (movement, gravity, collision against block grid)
3. Block tick (scheduled + random ticks, fluid spread, falling blocks)
4. Redstone propagation (BFS over redstone graph, bounded depth)
5. Light update (BFS over block_light + sky_light deltas)
6. Tile entity tick (furnaces, hoppers)
7. Reward + termination computation

Each subsystem is one or more fused CUDA kernels. Ordering must match vanilla
exactly within a tick or oracle diffs cascade.

### Render Pipeline (fused kernel, custom CUDA)

Stages emit intermediate buffers for per-stage verification:

1. **Visibility**: `face_id_buf[B,H,W]`, `depth_buf[B,H,W]` -- discrete, bitwise oracle target
2. **Texture sample**: `texel_buf[B,H,W,4]` -- nearest-neighbor, bitwise
3. **Smooth lighting / AO**: `light_buf[B,H,W]` -- integer avg + tight-tol interp
4. **Shading + fog**: `shaded_buf[B,H,W,3]` -- atol=2/255
5. **Final RGB**: `rgb_buf[B,H,W,3]` -- statistical only (SSIM, mean L1)

Not Vulkan, not OpenGL. Programmable CUDA cores doing rasterization (or
ray-marching the voxel grid -- TBD, prototype both). Tensor cores unused for
now; revisit if shading becomes bandwidth-bound.

## Verification: Per-Stage Disaggregated Oracle

**Single framebuffer diff against vanilla GL is impossible in principle**
(driver-level FP ordering). Decompose by information type.

### Physics Oracle

Vanilla 1.7.10 server (Forge coremod) as ground truth. Trace format:

```
(seed, initial_snapshot, action_sequence) -> per-tick (world_snapshot, entity_state, reward)
```

Diffs:

| Subsystem            | Tolerance              |
|----------------------|------------------------|
| Block grid           | bitwise                |
| Light values         | bitwise                |
| Redstone power       | bitwise                |
| Entity position      | atol=1e-9 (doubles)    |
| Entity velocity      | atol=1e-6 (floats)     |
| Random tick selection| distribution match over many seeds |
| Reward signal        | bitwise (integer-valued) |

Divergence past ~hundreds of ticks is expected from FP ordering. Either match
vanilla's float op order in kernels, or verify statistically over seed sweeps.

### Render Oracle

Patched vanilla client (Forge coremod) dumps the same intermediate buffers
from its GL pipeline (FBO readback, debug shader replacement). One-time ugly
hack, then permanent oracle.

Per-stage CI gates with the tolerances above. Final RGB is smoke-test only.

### Continuous Verification

- **Scenario corpus** in `tests/scenarios/`: redstone clocks, mob pathfinding,
  water flow, falling sand, piston chains, explosions, lava+water.
- **Golden replays** in `tests/golden/`: fixed `(seed, actions)` -> expected
  trace hash. Run on every kernel change.
- **Property tests**: block conservation, entity count invariants, projectile
  energy bounds.
- **Differential fuzz**: random valid action sequences, short horizons, diff
  vs vanilla.
- **Policy transfer test**: train tiny policy on flashmine renderer, eval on
  vanilla-rendered frames. If transfer works, renderer is RL-sufficient.

## Phases

1. **P0 -- Skeleton**: world data structures, host-side scenario loader,
   vanilla oracle harness scaffold. No kernels yet. **DONE.**
2. **P1 -- Minimal tick**: terrain + player + gravity entities + place/break.
   No fluids, no AI, no redstone. B=1024 ticking deterministically. **DONE.**
3. **P2 -- Minimal render**: flat-shaded face_id + texture stages only.
   Visibility kernel done + bitwise gated vs vanilla GL. Texture stage stubbed.
   **PARTIAL.**
4. **P3 -- Subsystems**: fluids, mob AI, redstone, light propagation.
   Fluid + lighting + redstone v2 (torch/wire/lever) + random_tick complete.
   Mob AI absent. Redstone v3 (repeaters/comparators/pistons) absent.
   **MOSTLY DONE.**
5. **P4 -- Full render**: smooth lighting, AO, fog. Per-stage tolerances.
   **NOT STARTED** (only visibility done in P2).
6. **P5 -- RL integration**: gym-style API, policy training smoke test.
   **NOT STARTED.**

## Existing Assets (anvil)

Reuse, don't reinstall:

- `~/.minecraft/versions/1.7.10/1.7.10.jar` -- vanilla client jar (licensed via Prism Launcher).
- `~/.minecraft/assets/indexes/1.7.10.json` -- asset index for textures.
- `~/1.7.10/mdk/` -- GTNH RetroFuturaGradle MDK workspace for 1.7.10 modding. Use this to build the oracle coremod.
- `~/1.8.9/mcp_1.7.10/` -- MCP 1.7.10 toolchain (decompile.sh, mappings: fields.csv / methods.csv / joined.srg). `src/` is empty; run `decompile.sh` to populate decompiled vanilla source for reference.
- `~/1.8.9/mcp_1.7.10/comparisons/1.7.10_vs_1.8.9.md` -- prior architectural diff notes.
- `~/1.8.9/oracle-mod/` -- existing Forge 1.8.9 oracle coremod from netherite v1: `OracleFrameCapture`, `OracleTickHandler`, `RenderBenchmark`, `OracleEventHandler`. **Port to 1.7.10 in `tools/oracle-mod-1710/`** -- this is the render+tick dumper described in the Verification section. Most of the FBO readback and tick instrumentation logic is reusable.
- `~/.gradle/caches/retro_futura_gradle/` -- RFG cache, vanilla 1.7.10 assets already extracted.
- Prism Launcher: `flatpak run org.prismlauncher.PrismLauncher` (system + user installs present).

## Hardware

anvil: RTX PRO 6000 Blackwell 96GB (sm_120). Build target: sm_120 only,
narrow compile per memory/feedback_narrow-builds.md.

## Architectural Debt (P1.5 list, status as of 2026-04-19)

1. **Live-entity compaction.** **DONE** (codex `39f771b`). Per-env live count + offsets + global compact list. Architecture in place; perf gain blocked by item 3 at the time, no longer dominant after sparse landed.
2. **Device-side action scatter.** **DEFERRED.** Still does B host-to-device 1-byte memcpys per `set_block` action. Recommended next CUDA win.
3. **Sparse/paletted block grid.** **DONE** (codex `51833e3`). Two-level: `section_table[B,144]` + per-env section pool (cap 32). Block sections: `palette[16]` + 4-bit packed indices + 4-bit meta nibbles. Lights: 4-bit packed nibbles, null = 0/15. **21–83x speedup** at B=1024, 5.93x memory reduction (751 KB/env total).
4. **AABB sweep memory.** **DONE** (codex `a95c1ad`). On-the-fly cell iteration. Registers 148 -> 126, stack frame 3072 -> 0 bytes.
5. **Double-buffer plumbing.** **DONE** — visibility kernel runs on the second stream `s_render` (codex `391d145`).

## Status (2026-04-19, head `7b56d28`)

68 / 68 tests pass in ~15s on master (was 83s pre-sparse).

### Subsystems landed (3-tier bitwise: oracle ↔ cpp_ref ↔ CUDA)

- Entity tick: gravity, AABB collision (full cubes + slabs), step-up auto-step, fall-damage, in-water/lava/web, EntityFallingBlock placement, EntityItem (gravity / drag / age / pickup / ice slipperiness), MinimalMob (sterilized EntityLiving, type 7).
- Lighting BFS: per-chunk heightmap + 256-byte dirty mask, BFS queue cap 32K, block opacity table mirrors vanilla (incl. `BlockSlab.func_149713_g(255)` quirk).
- Fluid spread: scheduled-tick queue cap 4096/env, water (5-tick) + lava (30-tick) `BlockDynamicLiquid.updateTick` transcribed. Lava "delay quadruple" RNG branch deferred (uses xorshift64).
- Redstone v2: BlockRedstoneTorch (75/76, scheduled rate 2, 8-toggles/60-ticks burnout), BlockRedstoneWire (55, meta = power 0-15), BlockLever (69, meta bit 0x8 ON/OFF + facing). New no-notify metadata write hook in `world.{hpp,cpp}`. Wire/lever non-opaque in lighting.
- Random ticks: bit-exact `JavaRandom` (LCG `0x5DEECE66D`, rejection-sampling `nextInt(n)`), bit-exact coord LCG (`field_73005_l*3 + 1013904223`). `rt_speed=3` enabled because oracle now reflectively seeds `WorldServer.field_73005_l` + `ambientTickCountdown` + pins clear weather + `ForgeChunkManager` spawn-chunk pin. Block dispatch full for grass / ice / snow_layer; sapling / wheat / carrots / potatoes / fire / mycelium are RNG-consumption stubs.
- Tile entities: furnace (`burnTime` / `cookTime` / `currentItemBurnTime` + lit/unlit block switch + stub recipe table iron->ingot, sand->glass, cobble->stone + stub fuel coal/charcoal/planks/saplings), hopper (transferCooldown + push facing + pull above + item-entity pickup + redstone-disabled bit), chest (minimal static endpoint).
- Render visibility: per-pixel DDA voxel ray-march, 16x8 px blocks, grid z=B. `face_id` u32 = `block_id:8 | face_index:3 | y:8 | z_local:4 | x_local:4` + `depth f32`. Default 256x192. Camera schema in scenario JSON. **Bitwise live gate vs vanilla GL passes** (0 mismatches over 49152 pixels w/ 1-pixel silhouette band).

### Tick order (cpp_ref + CUDA, must match vanilla)

`fluid -> redstone -> lighting drain -> random_tick -> tile entities -> entity tick -> apply_action -> end-of-tick lighting drain`

(After trainability work lands: insert `dig_progress_advance` between `tile entities` and `entity tick`; insert `reward+termination` at end.)

### 3-tier oracle chain (proven)

- Pure vanilla 1.7.10 server (`-javaagent`, no Forge) bitwise-equivalent to Forge oracle on all 6 measured scenarios. **Forge correction factor: zero.**
- cpp_ref bitwise vs oracle on 14+ hand-authored scenarios + 40 Hypothesis-fuzzed entity-tick scenarios.
- CUDA bitwise vs cpp_ref on all 18 corpus scenarios at B=1.

### Canonical JVM

**Azul Zulu 8u482** (RFG-provisioned). Cross-JDK is bimodal: bitwise or catastrophic, no sub-ULP. JDK 25 drives the Gradle daemon only. Detail in `docs/jdk_drift_report.md`, Forge audit in `docs/forge_correction_report.md`, sparse perf in `docs/cuda_perf_log.md`.

### Trace format v4

`src/oracle/physics_trace_format.h`. Tick header is 16 bytes (`tick`, `num_chunks`, `num_entities`, `num_tile_entities` — last field renamed from v3 `flags` which was always 0). Entity record 64 bytes (incl. fall_distance, is_in_web, is_in_water, is_in_lava). Tile entity SoA: x/y/z/type_id/packed_state. Reader (`tools/diff_trace.py`) accepts both v3 and v4.

### Test gates

- `tests/test_cpp_ref_vs_oracle.py`: per-scenario bitwise via `tools/diff_trace.py` (skips if oracle trace missing in worktree)
- `tests/test_cuda_vs_cppref.py`: bytewise equality. `SCENARIOS_SKIP_SLIGHT = {mob_in_water, item_in_water}` -- known divergence (cpp_ref defers per-action lighting BFS, CUDA does it eagerly; lava unaffected because it emits block_light)
- `tests/test_golden_replays.py`: sha256(trace_bytes) per (scenario, backend), 18 scenarios, fast (~30s). No oracle traces required. Regen via `uv run python tools/regen_golden.py --yes --determinism-runs 2`.
- `tests/test_random_tick.py`: `JavaRandom` + `LCG` + dispatch table unit tests
- `tests/test_cuda_render_vs_oracle.py`: face_id bitwise vs vanilla GL (auto-defaults to `traces/render/mob_in_water_<seed>_render.bin`)
- `tests/test_trace_format.py`: roundtrip
- `tests/fuzz_entity_tick.py`: Hypothesis differential fuzz (off by default; `FUZZ_MAX_EXAMPLES=N`)

### Render oracle launch (live)

`tools/run_render_oracle.sh --scenario tests/scenarios/<name>.json --auto-dump-at <tick> --tag <name>`. Requires:
- VirtualGL (manually `.deb` from github.com/VirtualGL/virtualgl/releases — apt repo doesn't have it)
- `xhost +SI:localuser:$(whoami)` for `:0` access
- PRIME offload env: `__NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia`
- Gradle daemon: JDK 25 (`/usr/lib/jvm/java-25-openjdk-amd64`); MC runtime: RFG-provisioned JDK 8

### Known divergences (intentional, gated)

- `mob_in_water` + `item_in_water`: 3 sky_light cells differ at tick 0 between cpp_ref (deferred per-action lighting BFS drain) and CUDA (eager via `launch_apply_block_change`). Lava unaffected (emits block_light, overrides sky propagation). Documented in `tests/test_cuda_vs_cppref.py`.
- Sky_light isn't asserted on `mob_in_water` / `mob_in_lava` / `item_in_water` in the cpp_ref-vs-oracle test (latent divergence preserved through the chain).
- `random_tick` block-dispatch stubs (sapling / wheat / carrots / potatoes / fire / mycelium) consume RNG correctly but no-op on the mutation.
- `BlockDynamicLiquid` lava "delay quadruple" RNG branch uses xorshift64 instead of `World.rand` (doesn't fire in current corpus where lava tick rate is 30).
- EntityItem yaw nondeterministic across implementations (vanilla `Math.random()` in ctor; oracle force-zeroes since `646833e`; tolerance gates it).

## What's next (priority order)

**Trainability path (current focus)** — gates "mine 1 tree" demo, the smallest end-to-end RL loop.

1. **Player entity** — new entity type (8). Adds: eye position, look (yaw/pitch), reach distance (4.5), inventory SoA (36 slots × `(item_id, count, meta)`), selected hotbar slot. Tick: standard entity physics + collision. Block on cpp_ref + CUDA + oracle (vanilla `EntityPlayer` minimal subset, no GUI/keepalive).
2. **Dig action** — new scenario action `{type:"dig", params:{x,y,z,player_slot}}`. Server-side: validate reach + LOS, look up `Block.getBlockHardness`, accumulate per-(env,player) dig-progress counter. On completion: set block → air, spawn `EntityItem` with drop. Per-env active-dig SoA capacity 1 (one dig in flight per player).
3. **EntityItem pickup → inventory** — extend existing EntityItem pickup path (`tick_entity.cpp`) to credit the nearest player's inventory instead of dropping the item.
4. **Reward + termination** (SPEC tick subsystem 7) — scenario schema gains `reward_fn: {type, params}`. Initial types: `inventory_count_delta(item_id)`, `block_broken_delta(block_id)`. `termination: {max_ticks, target: {item_id, count}}`. Per-env `(reward: float, done: bool)` written to trace.
5. **Tree worldgen scenario** — scenario action `spawn_tree(x,y,z,type)` places log column + leaves shell. Hand-authored "mine_one_tree" scenario: player at (8.5, 5.0, 8.5), tree at (10, 5, 8), scripted dig actions targeting log blocks.
6. **PPO smoke test — DONE.** Slices 6a (pybind11 `flashmine.Env`), 6b (per-env action vectors + `per_env_action` JSON + `--trace-env=N` CLI + `k_dig_complete_per_env` kernel), 6c (gym wrapper `MineATreeEnv` + sb3 PPO trainer + 8-seed eval) all landed. **Result**: 30k-step PPO trains to `ep_rew_mean=3.84/4` in ~70s on CPU; eval over 8 seeds completes 7/8 with median 142 ticks (vs scripted 165). Per-env reward kernel + B>1 vectorized actions deferred to slice 6d.
7. **Policy-transfer test** (SPEC meta-oracle) — replay trained policy's action sequence into vanilla via oracle mod. Compare reward curves: should match within ±5% over a horizon. Requires (6) and porting the action vocabulary back to vanilla via oracle hooks. **Now unblocked.**

**Deferred (not on critical path for "mine 1 tree")**

8. **Mob AI** — pathfinding, target selection, breeding. Needed for non-trivial RL scenarios beyond solitary mining. Defer until basic loop works.
9. **Device-side action scatter** (debt item 2) — eliminates B host-to-device tiny memcpys per `set_block`. Last big CUDA bottleneck on action-heavy scenarios. Becomes hot once per-env action vectors land in (1)+(2).
10. **Redstone v3** — repeaters, comparators, pistons, observers, buttons, pressure plates.
11. **Variable-width section bitpacking** in sparse layout — current 4-bit fixed indices are mutation-friendly but not minimum-memory.
12. **Render P4 polish** — per-corner AO, biome `colorMultiplier`, MC-accurate fog. Best-effort version landed; bitwise is out of scope.

## Workflow notes for next session

- Master is at `7b56d28` (post-sparse merge).
- Worktrees: cleaned. If isolated-worktree agents leave locks, the lock string identifies the owning Claude PID — `git worktree unlock` + `remove -f` after merging the branch.
- 8 codex parallel sprints landed in this session. Pattern: dispatch with `--dangerously-bypass-approvals-and-sandbox` (codex's own sandbox needs explicit disable; Claude's outer sandbox bypass via `dangerouslyDisableSandbox: true` on the Bash call is also required to nest).
- For new oracle scenarios, force-add the `traces/<scenario>_<seed>.bin` baseline despite `.gitignore`. Tests need it for the cpp_ref-vs-oracle gate.
- For new scenarios that the golden replay layer should cover, run `uv run python tools/regen_golden.py --yes --determinism-runs 2` after committing the cpp_ref + CUDA changes.

## Non-Goals

- Multi-GPU (single-node first, scale later)
- Mod compatibility
- Pixel-perfect framebuffer match against vanilla
- Versions other than 1.7.10
- Determinism across GPU generations (only within sm_120)


# Doc 10: `flashmine/docs/cuda_perf_log.md` {#doc-10}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/docs/cuda_perf_log.md`*

# CUDA tick perf log

Benchmarks recorded against `flashmine_cuda --bench` on RTX PRO 6000 Blackwell
(sm_120, driver 595.58.03, CUDA 13.2, nvcc 12.8). Bench mode skips trace
readback; the per-tick `cudaStreamSynchronize` in the action loop is still in
the path.

## 2026-04-18 -- post web slice (HEAD = 9b8f7cb), entity_gravity, B=1024

Reported earlier: ~2.5 ms/tick. Reality: cold-start artifact.

```
# cold (first invocation in shell after long idle)
bench: B=1024 duration=30  total=0.073s  ms/tick=2.4221

# warm (second invocation, same args, seconds later)
bench: B=1024 duration=30  total=0.002s  ms/tick=0.0669

# warm + longer horizon (amortizes per-process CUDA context init)
bench: B=1024 duration=300 total=0.006s  ms/tick=0.0207
```

Root cause: a single 30-tick run measures **CUDA context init + first-kernel
JIT + GPU clock ramp** alongside ~30 launches that each take ~2 microseconds.
Init dominates at 30 ticks. By 300 ticks it amortizes to 0.02 ms/tick.

P1.5's reported 0.089 ms/tick was a warm-cache measurement; the "regression"
to 2.5 ms/tick was a cold-cache measurement. Same binary.

Bisect across `aac11f9` (oracle), `48a1989` (cpp_ref), `9b8f7cb` (CUDA), HEAD
all show identical warm-cache numbers. No code regression.

## Action items

* Add a warmup tick to `--bench` mode before starting the timer. Single line
  fix; not done in this slice (the agreed scope was step-up + fall-damage).
* When citing perf numbers, always quote both cold and warm or use a 300-tick
  minimum horizon.

## Per-tick sync

The action loop unconditionally calls `cudaStreamSynchronize` every tick (so
host-side `set_block` actions stay coherent). For action-free scenarios like
`entity_gravity` this is wasted; could be gated on "actions[].tick == t exists"
to drop the warm number further. Filed for P2.

## 2026-04-18 task d item 5, bench warmup + action sync cleanup

Change:

* `--bench` now runs tick 0 as a warmup tick when no trace output is requested.
  The printed line reports `warmup=1` and `timed=N`.
* `set_block` action kernels are queued on the tick stream instead of stream 0.
* The unconditional per-tick `cudaStreamSynchronize` and same-stream event wait
  were removed. Spawn actions still synchronize because their entity SoA uploads
  use synchronous host-to-device copies.

Baseline was the second warm invocation of the previous binary. Post-change
numbers are single invocations because the warmup tick is excluded from timing.

| scenario | B | before ms/tick | after ms/tick | delta |
|----------|---|----------------|---------------|-------|
| entity_gravity | 1024 | 37.0577 | 36.9549 | -0.28% |
| water_spread | 1024 | 37.0431 | 36.9527 | -0.24% |
| redstone_torch_clock | 1024 | 37.1084 | 37.0044 | -0.28% |
| falling_sand_lands | 1024 | 37.0214 | 36.9571 | -0.17% |

Interpretation: the item fixed timer hygiene and removed the host barrier from
set_block-only ticks. The current B=1024 steady-state number is still dominated
by the dense block snapshot/replay path, not the action synchronization itself.

## 2026-04-18 task d item 1, live-entity compaction

Change:

* Added per-env live counts, a per-env offset table, and a compact global
  live-index list.
* `launch_tick_entities` now counts live slots, prefixes counts into offsets,
  fills the compact index list, and launches the physics kernel over
  `sum(live_counts)` threads.
* Slot order is preserved within each env by scanning slots `[0, E_MAX)`.

The current launcher reads back the scalar `live_total` to size the CUDA grid.
That adds one small host synchronization per entity tick. This keeps the
production threading shape explicit, but the B=1024 benchmark remains dominated
by dense block snapshot/replay and does not show a throughput win yet.

| scenario | B | before ms/tick | after ms/tick | delta |
|----------|---|----------------|---------------|-------|
| entity_gravity | 1024 | 36.9549 | 37.0546 | +0.27% |
| water_spread | 1024 | 36.9527 | 37.0426 | +0.24% |
| redstone_torch_clock | 1024 | 37.0044 | 37.0746 | +0.19% |
| falling_sand_lands | 1024 | 36.9571 | 37.0398 | +0.22% |

`nvcc -Xptxas -v` after this item:

```
k_tick_entities: 148 registers, 3072 byte stack frame, 0 spills
k_count_live_entities: 38 registers, 0 byte stack frame, 0 spills
k_prefix_live_counts: 18 registers, 0 byte stack frame, 0 spills
k_fill_live_indices: 16 registers, 0 byte stack frame, 0 spills
```

## 2026-04-18 task d item 4, AABB sweep memory

Change:

* Removed the per-thread `AABB boxes[32]` and step-up `boxes2[32]` buffers
  from `move_entity_dev`.
* Collision sweeps now iterate candidate cells directly for each Y, X, and Z
  offset pass. The scan order stays x, z, y, matching the previous materialized
  list order and the C++ reference.

Perf versus the post-item-1 binary:

| scenario | B | before ms/tick | after ms/tick | delta |
|----------|---|----------------|---------------|-------|
| entity_gravity | 1024 | 37.0546 | 37.0454 | -0.02% |
| water_spread | 1024 | 37.0426 | 37.0177 | -0.07% |
| redstone_torch_clock | 1024 | 37.0746 | 37.0809 | +0.02% |
| falling_sand_lands | 1024 | 37.0398 | 37.0380 | -0.00% |

Register and stack delta from `nvcc -Xptxas -v` on `tick_entity.cu`:

| kernel | before | after | delta |
|--------|--------|-------|-------|
| k_tick_entities registers | 148 | 126 | -22 |
| k_tick_entities stack frame | 3072 bytes | 0 bytes | -3072 bytes |
| k_tick_entities spills | 0 stores, 0 loads | 0 stores, 0 loads | unchanged |

Helper compaction kernels were unchanged by this item:

```
k_count_live_entities: 38 registers, 0 byte stack frame, 0 spills
k_prefix_live_counts: 18 registers, 0 byte stack frame, 0 spills
k_fill_live_indices: 16 registers, 0 byte stack frame, 0 spills
```

## 2026-04-18 P1.5 debt item 3, sparse paletted block grid

Change:

* Replaced dense `d_blocks`, `d_meta`, `d_blight`, `d_slight`, and
  `d_blocks_snapshot` slabs with sparse per-env section tables and fixed-cap
  per-env section pools.
* Block sections use a 16-entry `uint8_t` block-id palette, 4-bit palette
  indices, and 4-bit metadata nibbles. Light sections use 4-bit nibbles.
* Null block sections read as `(id=0, meta=0)`. Null block-light sections read
  as `0`. Null sky-light sections read as `15`.
* Block writers copy a section snapshot only on the first id mutation in that
  section and set a dirty bit. The lighting replay pass now scans dirty
  sections only, replacing the dense full-world snapshot plus full-world diff.
* `flashmine_cuda --bench` now prints `alloc_bytes/env`, measured with
  `cudaMemGetInfo` before and after `DeviceState::allocate`.

Layout trade-off: the palette is fixed at 16 block IDs per section instead of
variable bit width. That keeps device `set_block` mutation stable and avoids
repacking 4096 cells when fluid, redstone, random tick, or falling blocks add a
new block ID. Overflow is recorded in a per-env flag; none of the 16 CUDA
bitwise scenarios overflowed.

Baseline below was measured immediately before this change on the same
worktree and build directory. The earlier 37 ms/tick entries were from the
post-item-4 binary; this local baseline rebuilt to ~45 ms/tick, but the cause
was still the dense block snapshot/replay wall.

| scenario | B | before ms/tick | after ms/tick | speedup | delta |
|----------|---|----------------|---------------|---------|-------|
| entity_gravity | 1024 | 45.4093 | 0.5665 | 80.2x | -98.75% |
| falling_sand_lands | 1024 | 45.4418 | 0.7364 | 61.7x | -98.38% |
| water_spread | 1024 | 45.4248 | 0.5847 | 77.7x | -98.71% |
| redstone_torch_clock | 1024 | 45.5045 | 0.8199 | 55.5x | -98.20% |
| mob_in_water | 1024 | 45.2547 | 2.1321 | 21.2x | -95.29% |
| redstone_wire_decay | 1024 | 44.9349 | 1.0079 | 44.6x | -97.76% |
| random_tick_grass_spread | 1024 | 45.4252 | 0.5465 | 83.1x | -98.80% |

Memory per env:

| storage | before bytes/env | after bytes/env | reduction |
|---------|------------------|-----------------|-----------|
| block+meta+light world arrays, excluding replay snapshot | 2,359,296 | 397,592 | 5.93x |
| block+meta+light world arrays, including replay snapshot/sparse dirty snapshots | 2,949,120 | 397,592 | 7.42x |
| total `DeviceState::allocate`, measured by `cudaMemGetInfo` | not instrumented in dense baseline | 751,616 typical | n/a |

The old dense total allocation was not printed by the pre-change binary. The
world-grid rows above are exact allocation-size deltas from the removed dense
arrays versus the new sparse tables, pools, dirty bits, and per-section
snapshots. The new measured total includes non-world state that did not change:
entity SoA, live compaction scratch, lighting BFS queue, fluid queue, random
tick state, and redstone queue/toggle buffers.

Verification:

* `uv run pytest tests/test_cuda_vs_cppref.py -x`: 16 passed.
* `uv run pytest`: 37 passed, 7 skipped.
* `uv run ruff check . --fix`: all checks passed.


# Doc 11: `flashmine/docs/forge_correction_report.md` {#doc-11}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/docs/forge_correction_report.md`*

# Forge Correction Report

Date: 2026-04-17
Author: oracle-vanilla-agent commission

## Question

Does the existing Forge-based physics oracle
(`tools/oracle-mdk-1.7.10/`) diverge in any measurable way from a pure
unpatched vanilla 1.7.10 dedicated server? If so, by how much per
subsystem?

## Method

Built a second oracle that drives the *unpatched* vanilla
`minecraft_server.1.7.10.jar` (Mojang sha
`952438ac4e01b4d115c5fc38f891710c4941df29`) via a `-javaagent`
(`tools/oracle-vanilla-agent/`). No Forge, no FML, no MCP
sources on the runtime classpath. Implementation notes:

- The vanilla server jar is fully obfuscated (Notch class names: `a.class`,
  `aa.class`, ...). We remap it once with **SpecialSource 1.8.3** and the
  Forge 1.7.10 `notch-srg.srg` (yielding MCP-style class names like
  `net.minecraft.world.World` but SRG-style methods/fields like
  `func_147465_d` / `field_72996_v`). The default-package relocation rule
  `PK: . net/minecraft/src` is stripped to keep package-private access
  intact for the 146 classes MCP did not name.
- The agent's `premain` wipes the world dir, writes
  `server.properties` (FLAT, scenario seed, no mob spawning, no
  structures) and `eula.txt`, then spins a watchdog that polls
  `MinecraftServer.func_71276_C()` until `worldServers[0]` is non-null.
  Once armed, it registers a `java.lang.reflect.Proxy`
  `IUpdatePlayerListBox` via `MinecraftServer.func_82010_a` -- vanilla
  ticks our callback once per server tick from
  `MinecraftServer.func_71190_q`.
- The same chunk-pin fix as the Forge oracle is applied: an 11x11
  chunk window is force-loaded around scenario spawn so headless
  entity ticking actually fires (vanilla
  `World.updateEntityWithOptionalForce` gates on a 5x5 chunk window
  per entity).
- `World.rand.setSeed(scenario.seed)` after world init.
- After `scenario.duration_ticks` frames the agent calls
  `MinecraftServer.func_71263_m()` (initiateShutdown) and
  `Runtime.halt(0)` after a 2 s grace.
- Trace bytes are produced via the exact same struct sequence as
  `src/oracle/physics_trace_format.h`, so `tools/diff_trace.py`
  consumes either oracle's output without modification.

JDK: Azul Zulu 8u482 (the same canonical JDK 8 the Forge oracle is
locked to per `docs/jdk_drift_report.md`). Both oracles use the
identical JVM build.

## Wall-clock

- Build (one-time): ~3 s for SpecialSource remap + ~1 s for `javac` + `jar`.
- Per scenario, end-to-end: 3 to 6 s (server boot ~1.5 s, then
  `duration_ticks` ticks at full speed, then graceful shutdown).
  All 6 scenarios complete in under 25 s wall-clock total.
- Compare with Forge oracle: ~12 to 25 s per scenario (gradle
  daemon + Forge init + LaunchWrapper + coremod transformer chain).
  Vanilla agent wins by ~5x.

## Results: per-scenario delta vs Forge oracle

`uv run python tools/diff_trace.py traces/<scenario>.bin
traces/<scenario>_vanilla.bin`:

| Scenario              | Block grid | Entities (pos/vel) | Reward |
|-----------------------|-----------:|-------------------:|-------:|
| entity_gravity_99     | 0 / 0 / 0 / 0 | max_pos=0, max_vel=0 | 0 / 0 |
| falling_sand_42       | 0 / 0 / 0 / 0 | max_pos=0, max_vel=0 | 0 / 0 |
| falling_sand_lands_17 | 0 / 0 / 0 / 0 | max_pos=0, max_vel=0 | 0 / 0 |
| item_drop_23          | 0 / 0 / 0 / 0 | max_pos=0, max_vel=0 | 0 / 0 |
| redstone_torch_clock_7| 0 / 0 / 0 / 0 | max_pos=0, max_vel=0 | 0 / 0 |
| water_spread_1234     | 0 / 0 / 0 / 0 | max_pos=0, max_vel=0 | 0 / 0 |

Block-grid columns are `block_id / meta / block-light / sky-light`
mismatches across the full 3x3 chunk dump window for every tick.

## Verdict

**Forge correction factor on the present scenario corpus: zero.**

The Forge-based oracle and the pure vanilla server produce
bitwise-identical traces on all six scenarios under Azul Zulu
8u482. This includes:

- Entity physics (gravity, drag, terminal collision, item rotation
  Math.random() seed -- all match)
- Falling-block landing -> placed-block conversion
- Random tick scheduling (`World.rand.nextInt` selection of block tick
  candidates)
- Redstone tick scheduling for the redstone-torch clock
- Fluid spread with neighbour-update ordering for water

This was the strong-form hypothesis: Forge in 1.7.10 inserts itself
into the tick pipeline (event hooks, FML common bus, GameRegistry
class registration) but does not actually preempt or perturb the
vanilla per-tick subsystem ordering for the operations these
scenarios exercise. None of our scenarios involve
NBT load/save (where Forge adds modid maps), mod-injected entity
types, or capabilities, all of which would hit Forge-only code paths.

## Recommendation

**Keep the Forge oracle as canonical** for two reasons:

1. Forge gives us cleaner type-safe Java (no SRG reflection),
   `gradlew runServer` integration, and the existing decompile
   workspace -- all valuable for adding new scenarios.
2. The vanilla oracle is a better *verification artefact* than
   *daily driver*: it proves the Forge oracle is not perturbing
   the tick stream, but day-to-day scenario authoring is faster
   in Forge.

**Add the vanilla oracle to CI as a per-PR canary.** Any future
scenario that uses NBT-bearing entities, mod blocks, or
capability-touching subsystems will start to diverge between the
two oracles, and we want to catch that immediately rather than
discover it after porting a kernel against a Forge-perturbed
ground truth.

Wire-up: `tools/run_vanilla_oracle.sh tests/scenarios/<name>.json`
already exists. CI need only run it for each scenario file and
diff against `traces/<scenario>_<seed>.bin`. Exit code is non-zero
on any divergence.

## Caveats and known limits

- The vanilla oracle works only at the 6-scenario corpus this
  report covers. If a new scenario requires a custom block
  registration, that block has to be a vanilla 1.7.10 block id;
  Forge mod blocks won't work in the vanilla oracle.
- `EntityItem` ctor calls `Math.random()` 3 times to seed yaw
  and motion. Both oracles see those calls in identical order
  (single Java thread), so yaw matches. The diff tool does not
  gate on yaw for items per SPEC.md, but if it did, this would
  still match.
- Cross-JVM determinism is still bounded to the canonical Azul
  Zulu 8u482 build per `docs/jdk_drift_report.md`. The vanilla
  oracle inherits that constraint.


# Doc 12: `flashmine/docs/jdk_drift_report.md` {#doc-12}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/docs/jdk_drift_report.md`*

# JDK drift sweep, flashmine physics oracle

Date: 2026-04-17
Author: ops sweep
Scope: do oracle traces drift bitwise across host JDKs, or are they only
"bitwise within JDK X"? Decides whether we must freeze a canonical JDK
for the oracle.

## TL;DR

**Recommendation: freeze on JDK 8 (Azul Zulu 8u482, auto-provisioned by RFG)
for both the oracle and any code that tries to bitwise-match it (cpp_ref,
CUDA tick kernel).**

Evidence:

- For 3 of 6 scenarios (`entity_gravity`, `falling_sand`, `item_drop`)
  all three JDKs (8, 17, 25) produce **bitwise-identical** traces.
- For the other 3 (`falling_sand_lands`, `redstone_torch_clock`,
  `water_spread`) JDK 17 / 25 diverge from JDK 8 by **macroscopic,
  unbounded** amounts (entity position errors > 1 km, hundreds of
  thousands of block-grid mismatches, slot-set churn in the thousands).
  Magnitude is far above the SPEC.md tolerance budget; this is not a
  sub-ULP wobble.
- The current `cpp_ref` (built and tuned against the existing
  `traces/*.bin`) is **JDK 8-aligned**: it matches JDK 8 cleanly on the
  five scenarios where it ever matched and diverges from JDK 17 / 25 in
  exactly the same way the JDK 8 trace does. So `cpp_ref` is implicitly
  pinned to JDK 8, and its current "passing" is a JDK-8 fact, not a
  cross-JVM fact.
- Net: our bitwise-determinism floor is "bitwise within JDK 8 runtime",
  not "bitwise across any JVM." Any JDK migration of the oracle would
  invalidate every golden trace and every cpp_ref/CUDA kernel match.

## What "JDK 25" actually meant before this sweep

The README said the oracle "runs under JDK 25." That refers to the
**Gradle daemon JDK**, which the GTNH `gtnhconvention` plugin requires
to be JDK 21+ (its `gtnhgradle` artifact is class file 25).

The Minecraft 1.7.10 dedicated server JVM is independent. The default
`runServer` task uses RetroFuturaGradle's auto-provisioned **Azul Zulu
JDK 8u482** under
`tools/oracle-mdk-1.7.10/gradle-home/jdks/azul_systems__inc_-8-amd64-linux.2/`.
Modern-JVM execution is opt-in via the `runServer17` / `runServer21` /
`runServer25` tasks (lwjgl3ify + Hodgepodge stack).

So all existing traces in `traces/*.bin` were always JDK-8-runtime
traces. This sweep is the first time we've actually run the oracle under
JDK 17 and JDK 25.

## Build matrix

| Runtime JDK | Provisioned by | Build OK | Server runs | Mod fires | Notes |
|-------------|----------------|----------|-------------|-----------|-------|
| Azul Zulu 8u482  | RFG toolchain | yes | yes | yes | Default `runServer`. |
| Azul Zulu 17.0.18 | RFG toolchain | yes | yes (with lwjgl3ify+Hodgepodge transformers) | yes | `runServer17`. ASM warnings, Hodgepodge tries to write `world/DIM1/forcedchunks.dat`, harmless. |
| Azul Zulu 21.0.10 | RFG toolchain | yes | not exercised in this sweep | n/a | Available, skipped per scope (8/17/25 only). |
| Azul Zulu 25.0.x  | RFG toolchain | yes | yes (lwjgl3ify+Hodgepodge) | yes | `runServer25`. **Required a one-line patch to `tools/oracle-mdk-1.7.10/build.gradle.kts`** to forward `-Dflashmine.scenario` and `-Dflashmine.outdir` system properties to the `runServer25` JavaExec task; it had only been wired for `runServer`, `runServer17`, `runServer21`. Without that, the mod logs `flashmine.scenario JVM property not set; oracle will idle` and writes no trace. |

Apt-installed JDKs (`/usr/lib/jvm/java-{8,17,21,25}-openjdk-amd64`) are
present too; only the JDK 25 one is used here, as the Gradle daemon
JVM. The MC server is always one of the Azul JDKs above (RFG toolchain
selection, not configurable per task).

`openjdk-17-jdk-headless` had to be installed via apt (was not on the
machine before this sweep). JDK 8 and JDK 25 were already present.
Reinstall route documented; no PPA needed on Ubuntu 24.04.

## Diff matrix

Six scenarios, three pairwise JDK comparisons each, plus three
cpp_ref-vs-JDK comparisons. All JDK-pair diffs are over identical seeds,
identical scenarios, single 60-tick max horizon.

Legend: B = block_id mismatches, M = meta, BL = block-light, SL =
sky-light, MS = missing sections, S = symmetric slot-set delta,
PE = max pos err (m), VE = max vel err (m/tick).

### Sanity (jdk25 vs jdk25, same file)

All zero. Confirms the loader and diff tool are reproducible.

### entity_gravity, seed 99, 30 ticks  (CLEAN)

| pair         | B | M | BL | SL | MS | S | PE | VE |
|--------------|---|---|----|----|----|---|----|----|
| jdk8 v jdk17 | 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |
| jdk8 v jdk25 | 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |
| jdk17 v jdk25| 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |
| cppref v jdk8 | 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |
| cppref v jdk17| 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |
| cppref v jdk25| 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |

Bitwise-clean across all JDKs and against cpp_ref. No fluid spread, no
random tick winner-shuffle, single entity following deterministic
gravity integration.

### falling_sand, seed 42, 40 ticks  (CLEAN modulo cppref sky-light)

| pair         | B | M | BL | SL | MS | S | PE | VE |
|--------------|---|---|----|----|----|---|----|----|
| jdk8 v jdk17 | 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |
| jdk8 v jdk25 | 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |
| jdk17 v jdk25| 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |
| cppref v jdk8 | 0 | 0 | 0  | 36 | 0  | 0 | 0  | 0  |
| cppref v jdk17| 0 | 0 | 0  | 36 | 0  | 0 | 0  | 0  |
| cppref v jdk25| 0 | 0 | 0  | 36 | 0  | 0 | 0  | 0  |

JDK-stable. cpp_ref has a constant 36-cell sky-light delta against all
three JDKs, identical magnitude — pre-existing cpp_ref vs vanilla
disagreement, JDK-orthogonal. Falling-sand entity itself converts to a
block within ~10 ticks, so cross-JVM Math drift never gets a chance to
accumulate.

### item_drop, seed 23, 30 ticks  (CLEAN)

| pair         | B | M | BL | SL | MS | S | PE | VE |
|--------------|---|---|----|----|----|---|----|----|
| jdk8 v jdk17 | 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |
| jdk8 v jdk25 | 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |
| jdk17 v jdk25| 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  |
| cppref v jdk* | 0 | 0 | 0  | 0  | 0  | 0 | 0  | 0  | (all three)

Fully bitwise-clean across the matrix. Item entity, no fluid, short
horizon.

### falling_sand_lands, seed 17, 60 ticks  (UNBOUNDED DIVERGENCE)

| pair          | B        | M     | BL     | SL      | MS    | S      | PE      | VE       |
|---------------|----------|-------|--------|---------|-------|--------|---------|----------|
| jdk8 v jdk17  | 1,780,945| 1,140 | 30,118 | 829,440 | 1,620 | 8,339  | 2.99e+02| 6.25e-01 |
| jdk8 v jdk25  | 1,733,340| 4,200 | 60,540 | 829,440 | 2,280 | 18,609 | 8.70e+01| 6.25e-01 |
| jdk17 v jdk25 | 4,798,366|10,200 |192,358 | 345,600 |   660 | 13,852 | 7.77e+02| 8.46e-01 |
| cppref v jdk8 | 0        | 0     | 0      |     115 |     0 | 0      | 0       | 0        |
| cppref v jdk17| 1,780,945| 1,140 | 30,118 | 829,440 | 1,620 | 8,339  | 2.99e+02| 6.25e-01 |
| cppref v jdk25| 1,733,340| 4,200 | 60,540 | 829,440 | 2,280 | 18,609 | 8.70e+01| 6.25e-01 |

Unbounded. Entities are off by hundreds of metres. Slot-set delta in
the thousands means entirely different entities are alive. cpp_ref
matches JDK 8 with only a stale 115-cell sky-light delta and reproduces
the JDK 8 vs JDK 17/25 diffs identically — confirming cpp_ref is a JDK
8-aligned reference.

### redstone_torch_clock, seed 7, 20 ticks  (UNBOUNDED DIVERGENCE)

| pair          | B       | M    | BL     | SL      | MS  | S    | PE      | VE       |
|---------------|---------|------|--------|---------|-----|------|---------|----------|
| jdk8 v jdk17  | 586,712 | 440  | 11,935 | 276,480 | 540 | 2,543| 0       | 0        |
| jdk8 v jdk25  | 599,492 | 120  | 32,414 | 276,480 | 540 | 3,563| 0       | 0        |
| jdk17 v jdk25 | 621,840 |1,220 | 46,800 |       0 |   0 | 3,110| 1.46e+03| 8.01e-01 |
| cppref v jdk8 |      32 | 0    |  2,695 |       0 |   0 | 0    | 0       | 0        |
| cppref v jdk17| 586,680 | 440  |  9,240 | 276,480 | 540 | 2,543| 0       | 0        |
| cppref v jdk25| 599,460 | 120  | 31,620 | 276,480 | 540 | 3,563| 0       | 0        |

Hundreds of thousands of block-grid mismatches in 20 ticks. cpp_ref
hits a small pre-existing 32-block + 2695-light delta against JDK 8 but
mirrors JDK 8 perfectly against the cross-JDK comparison. Redstone graph
order is sensitive to the entity tick list, which is disturbed by the
JDK switch.

### water_spread, seed 1234, 30 ticks  (PARTIAL DIVERGENCE)

| pair          | B       | M | BL     | SL      | MS  | S    | PE | VE |
|---------------|---------|---|--------|---------|-----|------|----|----|
| jdk8 v jdk17  | 906,630 | 0 | 47,820 | 414,720 | 810 | 4,993| 0  | 0  |
| jdk8 v jdk25  |       0 | 0 |      0 |       0 |   0 | 0    | 0  | 0  |
| jdk17 v jdk25 | 906,630 | 0 | 47,820 | 414,720 | 810 | 4,993| 0  | 0  |
| cppref v jdk8 |       0 | 0 |      0 |      29 |   0 | 0    | 0  | 0  |
| cppref v jdk17| 906,630 | 0 | 47,820 | 414,720 | 810 | 4,993| 0  | 0  |
| cppref v jdk25|       0 | 0 |      0 |      29 |   0 | 0    | 0  | 0  |

Notable: **JDK 8 == JDK 25 bitwise** here, but JDK 17 diverges from
both. Most likely explanation: the lwjgl3ify/Hodgepodge transformer
stack for `runServer17` injects different bytecode (different mixin
version) than for `runServer25`, perturbing fluid-tick scheduling. Not a
JVM math difference, a tooling-stack difference. JDK 25 happens to
match JDK 8 here because the fluid graph is small and the chunk pin
keeps neighbour iteration order intact.

## Categorisation

- **bitwise-clean across JDKs**: entity_gravity, falling_sand, item_drop.
- **sub-ULP float**: none observed. There is no scenario where the only
  delta is < 1e-6 vel or < 1e-9 pos. When the JVM differs at all, it
  differs catastrophically because the divergence reorders entity
  spawn/despawn and Random consumption.
- **unbounded divergence**: falling_sand_lands, redstone_torch_clock.
  Block grid changes by O(1e6) cells, entities off by hundreds of
  metres or wholly different.
- **partial / tooling-driven divergence**: water_spread (JDK 8 ≡ JDK 25,
  JDK 17 stands alone). Implicates lwjgl3ify/Hodgepodge mixin variation
  more than `Math.*` ULP.

The "bitwise within JDK X" floor is real; the floor is JDK 8.

## Recommendation

1. **Canonical JDK for oracle = Azul Zulu 8u482**, auto-provisioned by
   RFG into
   `tools/oracle-mdk-1.7.10/gradle-home/jdks/azul_systems__inc_-8-amd64-linux.2/`.
   This is the JDK that the default `runServer` task uses, that all
   existing `traces/*.bin` were recorded under, and that `cpp_ref`
   currently matches.
2. **Do not use `runServer17` / `runServer21` / `runServer25` for
   trace generation.** They go through lwjgl3ify+Hodgepodge bytecode
   rewrites and produce non-determinism even between adjacent modern
   JDKs (water_spread shows JDK 17 ≠ JDK 25). They are useful only for
   ad-hoc debugging.
3. **Pin in CI**: assert that `runServer` resolves to Zulu 8u482
   (or any 8u with identical `Math` class file hash). Fail the build if
   the toolchain provisions a different JDK 8 build.
4. **cpp_ref and CUDA tick kernel targets**: continue matching JDK 8.
   Do not re-baseline against JDK 25. The current cpp_ref pre-existing
   deltas (36 cells in falling_sand, 32 blocks + 2695 light cells in
   redstone, 29 in water_spread, 115 in falling_sand_lands sky-light)
   are JDK-orthogonal and should be tracked as known cpp_ref bugs.
5. **Future**: when we replace `World.rand` with Philox per the
   "Future work" section of `src/oracle/README.md`, JDK sensitivity
   will shrink because `Random.nextInt` consumption order stops gating
   block ticks. Until then, JDK 8 is non-negotiable.

## Repro

```bash
# JDK 8 (canonical)
JAVA_HOME=/usr/lib/jvm/java-25-openjdk-amd64 \
  ./tools/oracle-mdk-1.7.10/gradlew \
  -p tools/oracle-mdk-1.7.10 \
  -Dflashmine.scenario=$PWD/tests/scenarios/falling_sand.json \
  -Dflashmine.outdir=$PWD/traces/jdk_drift/jdk8 \
  runServer --console=plain

# JDK 17  -> swap task to runServer17
# JDK 25  -> swap task to runServer25
```

Tagged outputs live at `traces/<scenario>_<seed>_jdk{8,17,25}.bin`.
Per-pair raw diff log is in `/tmp/diffmatrix.out` of the sweep machine
(not committed; regenerable by re-running `tools/diff_trace.py` over
the tagged traces).

## Build.gradle.kts patch (committed with this report)

```diff
-tasks.matching { it.name == "runServer" || it.name == "runServer17" || it.name == "runServer21" }.configureEach {
+tasks.matching { it.name == "runServer" || it.name == "runServer17" || it.name == "runServer21" || it.name == "runServer25" }.configureEach {
```

Without this, `runServer25` silently idles instead of writing a trace.
Required to run the sweep at all; left in place because future audits
may want to repeat the comparison.


# Doc 13: `flashmine/src/oracle/README.md` {#doc-13}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/src/oracle/README.md`*

# Physics Oracle

Vanilla Minecraft 1.7.10 server, run headlessly via a Forge coremod, dumps a
deterministic per-tick trace for use as ground truth by the CUDA tick
kernel.

The on-disk format is documented in `physics_trace_format.h` (raw packed
binary, little-endian, no NBT). The mod sources live in
`tools/oracle-mdk-1.7.10/src/main/java/com/flashmine/oracle/`.

## Canonical JDK

**The oracle is bitwise-deterministic only under Azul Zulu JDK 8u482**,
auto-provisioned by RFG into
`tools/oracle-mdk-1.7.10/gradle-home/jdks/azul_systems__inc_-8-amd64-linux.2/`.
This is the JDK selected by the default `runServer` task and the JDK
under which all committed `traces/*.bin` were recorded.

Do not regenerate traces with `runServer17` / `runServer21` /
`runServer25`. Those go through lwjgl3ify+Hodgepodge bytecode rewrites
and diverge from JDK 8 by macroscopic, unbounded amounts on
fluid/redstone/multi-entity scenarios (block-grid mismatches in the
millions, entity position drift > 1 km within 60 ticks). JDK 17 and
JDK 25 also disagree with each other on `water_spread`. See
`docs/jdk_drift_report.md` for the full evidence and per-scenario
matrix.

The Gradle daemon JDK is independent and must be JDK 21+ (25 preferred)
because the GTNH `gtnhconvention` plugin pins to it; this is unrelated
to the MC server runtime JDK selected by the task name.

## One-time setup

The fork of the GTNH RetroFuturaGradle MDK at
`tools/oracle-mdk-1.7.10/` reuses the gradle cache populated by the
upstream MDK at `~/1.7.10/mdk/`. If you have not yet run setup there:

```
cd ~/1.7.10/mdk
./gradlew setupDecompWorkspace   # ~5 to 15 min, ~270 MB download
```

That populates `~/.gradle/caches/{minecraft,retro_futura_gradle,...}`. The
oracle MDK fork picks these up automatically.

JDK requirements: the GTNH convention plugin pins to **JDK 25** for the
Gradle daemon (its `gtnhgradle` artifact has a JVM 25+ class file). The
MC runtime JDK 8 is auto-provisioned by RFG into
`tools/oracle-mdk-1.7.10/gradle-home/jdks/`. On anvil, set
`JAVA_HOME=/usr/lib/jvm/java-25-openjdk-amd64` before invoking gradle.

## Run a scenario

```
cd ~/flashmine
JAVA_HOME=/usr/lib/jvm/java-25-openjdk-amd64 \
./tools/oracle-mdk-1.7.10/gradlew \
    -p tools/oracle-mdk-1.7.10 \
    -Dflashmine.scenario=$PWD/tests/scenarios/falling_sand.json \
    -Dflashmine.outdir=$PWD/traces \
    runServer
```

The mod also wipes the `tools/oracle-mdk-1.7.10/run/server/world/` directory
and rewrites `server.properties` (FLAT generator, scenario seed, no mob
spawning, no structures) before the dedicated server reads them. Two runs
of the same scenario therefore produce bitwise-identical trace files.

The mod reads the scenario, drives the integrated server through
`scenario.duration_ticks` ticks, writes
`traces/<scenario_name>_<seed>.bin`, then asks the server to shut down and
exits with code 0. Stdout from the server is verbose; trace progress is
logged at info level under the `flashmineoracle` logger name.

## Diff two traces

```
uv run python tools/diff_trace.py traces/a.bin traces/b.bin
```

Reports per-subsystem deltas using the SPEC.md tolerance table. Exit code
is non-zero if any subsystem violates its tolerance.

## Scenario schema

```json
{
  "seed": <u64>,
  "duration_ticks": <int>,
  "world_init": {
    "flat_layer_blocks": [<id_or_{id,meta}>, ...],   // bottom-up; index = y
    "spawn": { "x": <double>, "y": <double>, "z": <double> }
  },
  "camera": {                          // optional, used by render tests
    "eye": [<double>, <double>, <double>],
    "lookat": [<double>, <double>, <double>],
    "up": [<double>, <double>, <double>],
    "fov_deg": <double>,
    "near": <double>,
    "far": <double>
  },
  "actions": [
    { "tick": <int>, "type": "<name>", "params": { ... } }
  ]
}
```

`camera` is ignored by the physics oracle and scalar/CUDA physics trace
writers. The CUDA renderer consumes it when `--render-out` is supplied, and
the render oracle should use the same pose when producing a matching FMOR
bundle. If omitted, render code derives a conservative camera from the spawn
point for ad hoc dumps, but render parity tests should always pin it.

Currently supported `actions[].type` values:

| type                  | params                                  |
|-----------------------|-----------------------------------------|
| set_block             | `x`, `y`, `z`, `block_id`, `meta`       |
| spawn_falling_block   | `x`, `y`, `z`, `block_id`, `meta`, `dx`, `dy`, `dz` |
| spawn_tnt             | `x`, `y`, `z`, `dx`, `dy`, `dz`         |
| spawn_item            | `x`, `y`, `z`, `block_id`, `meta`, `dx`, `dy`, `dz` |
| spawn_mob             | `type`, `x`, `y`, `z` (doubles), `dx`, `dy`, `dz`, `health` |
| spawn_tile_entity     | `x`, `y`, `z`, `tile_type`, `meta`, `items` |

Add new action types in `ScenarioApplier.applyAction()`.

`spawn_tile_entity` supports `tile_type` values `furnace`, `hopper`, and
`chest`. `items` is an optional array of slot-addressed stack initializers:

```json
{ "slot": 0, "item_id": 15, "meta": 0, "count": 1 }
```

`item_id` may be replaced by `block_id` for block-backed stacks. Missing
`meta` defaults to 0 and missing `count` defaults to 1.

## Trace format versions

* **v1** (initial): entity record was 56 bytes (slot, type, pos, vel, yaw,
  pitch, hp_x10, on_ground, pad). All v1 traces are obsolete.
* **v2** (2026-04-18, web slice): appends `fall_distance` (float),
  `is_in_web` (uint8), and a 3-byte pad to each entity record. Total
  entity record size is now 64 bytes. Required for the EntityLivingBase
  MinimalMob slice (cobweb branch first; water/lava/step-up/fall-damage
  in later slices). All committed baseline traces under `traces/` were
  re-recorded under v2 on the same JDK 8 build. The version field in
  `flashmine_phys_file_header_t` is bumped to 2; `tools/diff_trace.py`,
  the C++ reference's `TraceWriter`, and the CUDA driver all read+write
  v2 only. Loading v1 traces will fail with "unsupported version".
* **v3** (2026-04-18, water+lava slice): appends `is_in_water` (uint8,
  `Entity.field_70171_ac` set by `handleWaterMovement`/`func_70072_I`)
  and `is_in_lava` (uint8, snapshot of `Entity.handleLavaMovement`/
  `func_70058_J` at dump time -- no persistent flag on the entity). Pad
  shrinks from 3 bytes (v2) to 1 byte (v3) so total record size stays
  64 bytes. Required for the water/lava buoyancy slice (mob_in_water,
  mob_in_lava, item_in_water scenarios). All committed baseline traces
  under `traces/` were re-recorded under v3 on the same JDK 8 build.
  Loading v2 traces will fail.
* **v4** (2026-04-18, tile entity slice): reuses the fourth per-tick header
  word as `num_tile_entities` and appends a tile-entity SoA after entity
  records and before the reward fields. Each tile record contributes
  `x[i32]`, `y[i32]`, `z[i32]`, `type_id[u32]`, and `packed_state[u64]`.
  Type IDs are `1=furnace`, `2=hopper`, and `3=chest`. The packed state is a
  deterministic compact inventory/timer summary rather than NBT. Existing
  v3 traces load as unsupported and must be re-recorded. All committed
  baseline traces under `traces/` were re-recorded under v4 on the canonical
  JDK 8 runtime, and the tile scenarios add new `furnace_smelt_iron` and
  `hopper_pull_chain` baselines.

## Determinism Notes

- **Chunk pinning for headless entity ticking**: vanilla 1.7.10 only ticks an
  entity if a 5x5 chunk window around it is loaded
  (`World.updateEntityWithOptionalForce` -> `checkChunksExist(x-32..x+32)`).
  Without a player on the dedicated server, only chunks touched by
  `setBlock` get loaded, which strands entities. `ScenarioApplier.applyWorldInit`
  therefore pre-loads an 11x11 chunk square around the scenario spawn via
  `theChunkProviderServer.loadChunk`. Without players, ChunkProviderServer's
  unload queue is never populated, so once loaded these chunks stay resident
  for the entire run. This was picked over spawning a FakePlayer because it
  adds zero entity state to the trace and avoids Forge FakePlayer quirks.
- `World.rand`, `World.updateLCG` (`field_73005_l`), and
  `World.ambientTickCountdown` are reseeded from `scenario.seed` immediately
  after chunk pinning and world initialization, before the first driven server
  tick. Vanilla initializes `updateLCG` and the ambient countdown from
  time-seeded `Random` instances; reseeding both is required for random block
  ticks to be bitwise repeatable.
- The oracle pins clear weather by setting rain/thunder flags false and their
  timers to `Integer.MAX_VALUE`. Fresh vanilla worlds otherwise spend the first
  server tick drawing rain and thunder durations from `World.rand`, which would
  perturb the random-tick stream even though flashmine physics scenarios do not
  exercise weather.
- The oracle forces the scenario spawn chunk with `ForgeChunkManager`. Forge's
  `World.setActivePlayerChunksAndCheckLight` patch copies persistent chunks
  into the active chunk set, so the spawn chunk receives vanilla
  `randomTickSpeed=3` random block ticks on a headless server without spawning
  a fake player.
- Player join order is deterministic in headless mode (no players join the
  dedicated server unless a scenario action spawns them).
- The committed random-tick traces are expected to be bitwise stable on the
  canonical JDK 8 runtime. Re-record random-tick traces whenever the oracle RNG
  seeding contract, forced-chunk policy, or weather setup changes.

## Adding a new scenario

1. Drop a JSON file under `tests/scenarios/`. Keep it tiny and reproducible.
2. Run the mod against it once. The output `.bin` is the golden trace.
3. (Optional) Snapshot the SHA-256 of the trace into
   `tests/golden/<name>.sha256` so any future drift is caught.

## Future work

- Wire reward + termination signals (currently zero-stubbed in the trace).
- Player spawning + action-vector application (movement, break, place,
  use) for RL action streams.
- Additional tile entities and fuller inventory behavior: dispensers,
  droppers, brewing stands, enchantment tables, beacons, double chests,
  minecart inventories, and full NBT-aware item stacks.


# Doc 14: `flashmine/src/render/oracle_README.md` {#doc-14}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/src/render/oracle_README.md`*

# Render Oracle

Ground-truth dumper for the per-stage render pipeline described in
`SPEC.md`. Built as a Forge 1.7.10 client mod that runs vanilla Minecraft
on real NVIDIA OpenGL and saves intermediate buffers to disk for diffing
against the fused CUDA renderer in `src/render/`.

## Layout

```
tools/oracle-render-mdk-1.7.10/   # Forked GTNH RFG MDK (don't touch oracle-mdk-1.7.10 -- that's the physics oracle)
  src/main/java/com/flashmine/oraclerender/
    OracleRenderMod.java          # @Mod entry point
    ClientProxy.java              # registers handler client-side
    CommonProxy.java              # no-op server side
    OracleConfig.java             # -Dflashmine.render.* sysprops
    RenderOracleHandler.java      # F8/F9 keys + RenderWorldLastEvent hook
    TraceWriter.java              # writes binary bundle, format below

src/oracle/trace_format.h         # canonical layout (also consumed by physics oracle's diff tools)
src/render/oracle_README.md       # this file
tools/diff_render.py              # uv-script, compares two trace bundles with per-stage tolerances
tools/test_trace_format.py        # roundtrip self-test for the binary format
traces/render/                    # output dir (.gitignored)
```

## Trace format

See `src/oracle/trace_format.h` for the canonical definition. Magic
`FMOR`, version 1. One file per dump:

```
[header 128B]
[stage_desc[5] 32B each]
[payload 0] face_id   uint32   H*W*4
[payload 1] texel     u8 RGBA  H*W*4
[payload 2] light     float32  H*W*4
[payload 3] shaded    u8 RGB   H*W*3
[payload 4] rgb       u8 RGB   H*W*3
```

All payloads are top-left origin (the writer flips GL's bottom-left
framebuffer). The format roundtrip is verified by
`tools/test_trace_format.py` (passes with no MC needed).

## Per-stage tolerances (matches `SPEC.md` "Render Pipeline")

| stage   | tolerance              | rationale                                   |
|---------|------------------------|---------------------------------------------|
| face_id | bitwise                | discrete IDs, no float math                 |
| texel   | bitwise                | nearest-neighbor sample, fixed atlas        |
| light   | atol=1e-4, rtol=1e-3   | smooth-lighting bilerp, host-side FP add    |
| shaded  | u8 atol=2/255          | fog + per-vertex shade, driver fadd ordering|
| rgb     | statistical only       | full pipeline including dithering           |

`tools/diff_render.py` applies these; pass `--strict` to fail nonzero on
any non-statistical mismatch.

## Triggering a dump

In-game:

- `F8` arms a single-shot dump on the next world-render frame.
- `F9` toggles continuous mode (every 10 frames).

Headless / CI:

```
-Dflashmine.render.autoDumpAt=120     # dump frame 120 then quit
-Dflashmine.render.tag=mytag          # filename prefix
-Dflashmine.render.outDir=...         # absolute dir; defaults to <repo>/traces/render
```

## Running on anvil (NVIDIA, headless via VirtualGL)

The mod must run on the real NVIDIA driver; Mesa software (Xvfb) defeats
the entire oracle. Ubuntu 24.04 does not ship VirtualGL in the default apt
repos on anvil; install the upstream stable `.deb` directly:

```sh
curl -L -o /tmp/virtualgl.deb \
  https://github.com/VirtualGL/virtualgl/releases/download/3.1.4/virtualgl_3.1.4_amd64.deb
sudo apt install /tmp/virtualgl.deb mesa-utils
```

The active GDM cookie on anvil is `/run/user/1000/gdm/Xauthority`, but it is
owned by the logged-in desktop user. If direct `DISPLAY=:0 glxinfo` fails with
`Authorization required`, grant local access from that session:

```sh
sudo -u elliots DISPLAY=:0 XAUTHORITY=/run/user/1000/gdm/Xauthority \
  xhost +SI:localuser:$(whoami)
```

Run through VirtualGL with PRIME offload so GLX selects NVIDIA instead of the
AMD iGPU/Mesa display path:

```sh
DISPLAY=:0 __NV_PRIME_RENDER_OFFLOAD=1 __GLX_VENDOR_LIBRARY_NAME=nvidia \
  vglrun -d :0 glxinfo -B | grep -i renderer

tools/run_render_oracle.sh \
  --scenario tests/scenarios/mob_in_water.json \
  --auto-dump-at 240 \
  --tag mob_in_water_161803_render
```

The expected renderer string on anvil is
`NVIDIA RTX PRO 6000 Blackwell Workstation Edition/PCIe/SSE2`.

The MDK launches MC with the JVM args from `gradle.properties`. The following
are added by `RenderOracleHandler`'s init logging so a misload is loud:

```
-Dorg.lwjgl.opengl.Display.allowSoftwareOpenGL=false
```

(currently set on the user's command line; can be moved into the MDK's
`runClientJvmArgs` if needed).

While running, `nvidia-smi` should show a `java` process holding GPU
memory.

## Build

First time:

```
cd ~/flashmine/tools/oracle-render-mdk-1.7.10
./gradlew setupDecompWorkspace        # 5 to 15 min, ~270 MB d/l
./gradlew build                       # produces build/libs/*.jar
./gradlew runClient                   # opens MC client with the mod
```

The MDK auto-provisions a Java 8 toolchain for the MC runtime; the host
needs JDK 21+ for the Gradle daemon. See `tools/oracle-render-mdk-1.7.10/REQUIREMENTS.md`.

## Diffing

```
uv run python tools/diff_render.py traces/render/a.bin traces/render/b.bin
uv run python tools/diff_render.py --strict a.bin b.bin     # CI mode
```

## Status (P2 scaffold)

Working end-to-end:

- Format: header + 5 stage descs + payloads, byte-exact, roundtrip-tested
  with `tools/test_trace_format.py`.
- Dump trigger: F8/F9 keybinds and `-Dflashmine.render.autoDumpAt`.
- `rgb` stage: real `glReadPixels(GL_RGB, GL_UNSIGNED_BYTE)` capture
  from the bound framebuffer at `RenderWorldLastEvent`. Vertically
  flipped to top-left origin.
- `face_id`, `texel`, `light`, `shaded`: offscreen MRT terrain replay via
  `OracleFrameCapture` and a debug shader. Attachment 0 is RGB10A2 and is
  read back as packed `uint32`; the shader reconstructs world position from
  `gl_FragCoord.z`, samples a 48x256x48 block-id texture, derives face index
  from screen-space world-position derivatives, and packs
  `(block_id, face, chunk-local xyz)` component-wise. Do not assemble the
  full 32-bit face ID in one GLSL float before packing; block-scaled IDs lose
  low coordinate bits in single precision.
  Attachments 1..3 feed the remaining stage payloads.
- CUDA visibility: `flashmine_cuda --render-out PATH` emits FMOR bundles
  at 256x192 by default. `face_id` is populated by the CUDA DDA kernel;
  higher stages are zero-filled until texture/light/shading land.
- Diff tool: per-stage tolerances, mean-L1, optional SSIM (skimage), and
  `--error-map-dir` PGM output for per-pixel stage errors.

Deferred:

- CUDA `texel`, `light`, `shaded`, and `rgb`.
- Exact texture/light/shading parity. The oracle now emits non-stub payloads
  for stages 1..3, but CUDA higher stages still intentionally zero-fill.

## Path to filling the stubs

The stubs are zero-filled buffers of the right shape, so downstream tools
work today. Real per-stage capture in 1.7.10 needs one of:

1. **MRT debug pass.** Replace the terrain shader with a debug shader
   that writes face_id / texel / light / shaded to four
   `GL_COLOR_ATTACHMENTn` color buffers of an FBO, then `glReadPixels`
   each. 1.7.10 uses fixed-function GL by default but `EXT_framebuffer_object`
   plus a custom `glUseProgram` works on every NVIDIA driver. Requires
   intercepting `RenderGlobal.renderSortedRenderers` (use a coremod
   transformer or RFG access transformer to expose the call site) and
   binding our FBO before the terrain pass.

2. **Multiple separated passes.** Cheaper to land but slower at runtime:
   replace the terrain shader once per stage, render the world four
   times, read back each FBO. Acceptable for the oracle since it's not
   on the perf-critical path.

3. **CPU-side reconstruction.** For `face_id` specifically, the
   information is already known to the server: every visible block face
   can be enumerated by replaying culling on the server's chunk
   snapshot. Cheap but doesn't validate the GL rasterizer.

The current implementation uses option 1 with a single MRT pass and a compact
world-grid texture keyed by reconstructed world position.

## Coordination with the physics oracle

Header and dtype enums are intentionally compatible with the physics
oracle: physics uses magic `FMOP`, render uses `FMOR`, both share
`fm_render_dtype`. If the physics agent introduces a different convention
in `src/oracle/trace_format.h`, the render writer should be updated to
match -- the format file is the single source of truth.


# Doc 15: `flashmine/tests/fuzz_failures/README.md` {#doc-15}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tests/fuzz_failures/README.md`*

# Fuzz failures

Each subdirectory is a UTC-timestamped artifact bundle from a Hypothesis
shrink in `tests/fuzz_entity_tick.py`. Layout:

```
<UTC>/
  scenario.json     # minimized failing scenario (Hypothesis output)
  scenario_path.txt # path to the temp scenario at run time (for context)
  oracle.bin        # Java oracle trace
  cpp_ref.bin       # cpp_ref trace
  diff_totals.json  # per-subsystem deltas at failure
```

## Triage workflow

```
uv run python tools/replay_failure.py tests/fuzz_failures/<UTC>/scenario.json
```

This re-runs both implementations and pipes through `diff_trace.py`. Use
to confirm the failure reproduces (rules out flakes), then bisect cpp_ref
or the oracle as needed.

## Known oracle gotcha (filtered out by the strategy, not a real cpp_ref bug)

Vanilla 1.7.10 `level-type=FLAT` with certain world seeds (notably 0)
generates surface lava/water lakes, ore veins, and passive mobs in the
dumped chunk window even with `spawn-animals=false`. Both water/lava
blocks and `EntityLivingBase` entities are deferred branches in cpp_ref
(see `src/sim/cpp_ref/tick_entity.cpp` file-top comment), so they would
false-positive against the oracle. The fuzz strategy hardcodes a
small set of empirically-clean worldgen seeds (`_GOOD_WORLDGEN_SEEDS`)
to dodge this. Add new seeds there only after manually confirming the
oracle dump for that seed contains no water/lava/mobs.


# Doc 16: `flashmine/tests/golden/README.md` {#doc-16}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tests/golden/README.md`*

# Golden Replay Schema

`tests/golden/<scenario>.json` stores the expected trace hashes for one
scenario in `tests/scenarios/<scenario>.json`. Golden replay files are a fast
checkin gate for cpp_ref and CUDA drift. They do not contain oracle traces and
the pytest gate does not read `traces/`.

Regenerate intentionally with:

```sh
uv run tools/regen_golden.py --yes
```

Always review the resulting `git diff` before committing. Regeneration replaces
the oracle value for future checks.

## Version 1

Required fields:

- `schema_version`: integer schema version, currently `1`.
- `scenario_name`: scenario file stem, for example `entity_gravity`.
- `seed`: integer seed copied from the scenario JSON.
- `cpp_ref_trace_sha256`: SHA-256 hex digest of cpp_ref trace bytes.
- `cuda_trace_sha256`: SHA-256 hex digest of CUDA trace bytes.
- `trace_bytes_size`: trace byte size. cpp_ref and CUDA must produce the same
  size for the scenario.
- `generated_timestamp`: UTC ISO-8601 timestamp for the regeneration run.
- `git_sha`: commit SHA used as the source revision for the regeneration run.


# Doc 17: `flashmine/tools/oracle-mdk-1.7.10/README.md` {#doc-17}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tools/oracle-mdk-1.7.10/README.md`*

# Example Forge Mod for Minecraft 1.7.10

[![](https://jitpack.io/v/GTNewHorizons/ExampleMod1.7.10.svg)](https://jitpack.io/#GTNewHorizons/ExampleMod1.7.10)
[![](https://github.com/GTNewHorizons/ExampleMod1.7.10/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/GTNewHorizons/ExampleMod1.7.10/actions/workflows/build-and-test.yml)

An example mod for Minecraft 1.7.10 with Forge focussed on a stable, updatable setup.

<!-- omit in toc -->
### Table of Contents

* [Example Forge Mod for Minecraft 1.7.10](#example-forge-mod-for-minecraft-1710)
    * [Motivation](#motivation)
    * [Help! I'm stuck!](#help-im-stuck)
    * [Getting started](#getting-started)
    * [Features](#features)
    * [Files](#files)
    * [Forge's Access Transformers](#forges-access-transformers)
    * [Mixins](#mixins)
    * [Advanced](#advanced)
    * [Feedback wanted](#feedback-wanted)


### Motivation

We had our fair share in struggles with build scripts for Minecraft Forge. There are quite a few pitfalls from non-obvious error messages. This Example Project provides you a build system you can adapt to over 90% of Minecraft Forge mods and can easily be updated if need be.

### Help! I'm stuck!

We all have been there! Check out our [FAQ](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/docs/FAQ.md). If that doesn't help, please open an issue.

### Getting started

Creating mod from scratch:
1. Unzip [project starter](https://github.com/GTNewHorizons/ExampleMod1.7.10/releases/download/master-packages/starter.zip) into project directory.
2. Replace placeholders in LICENSE-template and rename it to LICENSE, or remove LICENSE-template and put any other license you like on your code. This is an permissive OSS project and we encourage you participate in OSS movement by having permissive license like one in template. You can find out pros and cons of OSS software in [this article](https://www.freecodecamp.org/news/what-is-great-about-developing-open-source-and-what-is-not/)
3. Ensure your project is under VCS. For example initialise git repository by running `git init; git commit --message "initialized repository"`.
4. Replace placeholders (edit values in gradle.properties, change example package and class names, etc.)
5. Run `./gradlew setupDecompWorkspace`
6. Run `./gradlew build`
6. Make sure to check out the rest sections of this file.
7. You are good to go!

We also have described guidelines for existing mod [migration](docs/migration.md) and [porting](docs/porting.md)

### Features

 - Updatable: Replace [`build.gradle`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/build.gradle) with a newer version
 - Optional API artifact (.jar)
 - Optional version replacement in Java files
 - Optional shadowing of dependencies
 - Simplified setup of Mixin and example
 - Scala support (add sources under `src/main/scala/` instead of `src/main/java/`)
 - Optional named developer account for consistent player progression during testing
 - Boilerplate forge mod as starting point
 - Improved warnings for pitfalls
 - Git Tags integration for versioning
 - [Jitpack](https://jitpack.io) CI
 - GitHub CI:
   - Releasing your artifacts on new tags pushed. Push git tag named after version (e.g. 1.0.0) which will trigger a release of artifacts with according names.
   - Running smoke test for server startup. On any server crash occurring workflow will fail and print the crash log.

### Files
 - [`build.gradle`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/build.gradle): This is the core script of the build process. You should not need to tamper with it, unless you are trying to accomplish something out of the ordinary. __Do not touch this file! You will make a future update near impossible if you do so!__
 - [`gradle.properties`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/gradle.properties): The core configuration file. It includes
 - [`dependencies.gradle[.kts]`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/dependencies.gradle): Add your mod's dependencies in this file. This is separate from the main build script, so you may replace the [`build.gradle`](https://github.com/SinTh0r4s/ExampleMod1.7.10/blob/main/build.gradle) if an update is available.
 - [`repositories.gradle[.kts]`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/repositories.gradle): Add your dependencies' repositories. This is separate from the main build script, so you may replace the [`build.gradle`](https://github.com/SinTh0r4s/ExampleMod1.7.10/blob/main/build.gradle) if an update is available.
 - `addon.gradle[.kts]`: Any additional build logic. This is separate from the main build script, so you may replace the [`build.gradle`](https://github.com/SinTh0r4s/ExampleMod1.7.10/blob/main/build.gradle) if an update is available. See [Advanced](#advanced) for more details.
 - [`jitpack.yml`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/jitpack.yml): Ensures that your mod is available as import over [Jitpack](https://jitpack.io).
 - [`.github/workflows/gradle.yml`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/.github/workflows/gradle.yml): A simple CI script that will build your mod any time it is pushed to `master` or `main` and publish the result as release in your repository. This feature is free with GitHub if your repository is public.

### Forge's Access Transformers

You may activate Forge's Access Transformers by defining a configuration file in `gradle.properties`.

Check out the [`example-access-transformers`](https://github.com/GTNewHorizons/ExampleMod1.7.10/tree/example-access-transformers) branch for a working example!

> [!WARNING]
> Access Transformers are bugged and will deny you any sources for the decompiled Minecraft! Your development environment will still work, but you might face some inconveniences. For example, IntelliJ will not permit searches in dependencies without attached sources.

### Mixins

[Mixins](https://github.com/SpongePowered/Mixin) are used to modify vanilla or mod/library code during runtime without having to edit, recompile, and redistribute the original code. For example, mixins can change a hardcoded value, redirect a method call, inject additional code, access private fields/methods, make a class implement your interface, and more. Mixins are an advanced feature which most normal mods will not require.

Documentation about Mixin features can be found here: [Mixin Wiki](https://github.com/SpongePowered/Mixin/wiki) and [MixinExtras Wiki](https://github.com/LlamaLad7/MixinExtras/wiki)

There are many examples of mixins in these mods: [Hodgepodge](https://github.com/GTNewHorizons/Hodgepodge) and [Angelica](https://github.com/GTNewHorizons/Angelica)

To enable Mixins in your project, follow one of the example commits:
- use [normal mixins](https://github.com/GTNewHorizons/ExampleMod1.7.10/commit/beba55615fa8337b7639f0d5b18db6cc8d4826be) for basic and quick registration
- use [GTNH IMixins](https://github.com/GTNewHorizons/ExampleMod1.7.10/commit/055cd4f18765a421a86c706f53b62116988297e3) (recommended) for the same thing as below, but in a less verbose and more unified manner using the IMixins api
- use [GTNH Early/Late mixins](https://github.com/GTNewHorizons/ExampleMod1.7.10/commit/c4df59d92164775b69451f3e690239e93d1fc979) to have full control over the registration logic and check for presence of other mods during runtime to load your mixins

The extra required dependencies are handled automatically after mixins are enabled.

### Advanced

If your project requires custom gradle commands you may add a `addon.gradle[.kts]` to your project. It will be added automatically to the build script. Although we recommend against it, it is sometimes required. When in doubt, feel free to ask us about it. You may break future updates of this build system!
If you need access to properties modified later in the buildscript, you can also use a `addon.late.gradle[.kts]`.
For local tweaks that you don't want to commit to Git, like adding extra JVM arguments for testing, use `addon[.late].local.gradle[.kts]`.

### Feedback wanted

If you tried out this build script we would love to head your opinion! Is there any feature missing for you? Did something not work? Please open an issue and we will try to resolve it asap!

Happy modding,\
[SinTh0r4s](https://github.com/SinTh0r4s), [TheElan](https://github.com/TheElan) and [basdxz](https://github.com/basdxz)


# Doc 18: `flashmine/tools/oracle-mdk-1.7.10/REQUIREMENTS.md` {#doc-18}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tools/oracle-mdk-1.7.10/REQUIREMENTS.md`*

# Minecraft 1.7.10 MDK Requirements

## Host machine prerequisites

| Item | Version | Notes |
|---|---|---|
| OS | Linux / macOS / Windows | tested on Ubuntu 24.04 |
| JDK (for Gradle daemon) | 21+ (25 preferred) | pinned in `gradle/gradle-daemon-jvm.properties`. apt: `openjdk-25-jdk-headless` |
| JDK 8 (for MC runtime) | auto-provisioned | RFG downloads Mojang JDK 8u202 via Gradle toolchain on first run |
| Display server (X11/Wayland) | any | only needed for `runClient`; headless CI can use `xvfb-run` |
| Free disk | ~2 GB | Gradle caches + build artifacts |
| Network (first run only) | yes | ~270 MB one-time download |
| GPU | OpenGL 2.1+ | MC 1.7.10 uses fixed-function GL |

## What first-run downloads and caches (not in the bundle)

Locations after `./gradlew setupDecompWorkspace`:

- `~/.gradle/caches/minecraft/` — Minecraft 1.7.10 client jar, Forge 10.13.4.1614, LWJGL natives (~135 MB)
- `~/.gradle/caches/retro_futura_gradle/` — vanilla assets (686 files), MCP 9.05 mappings, Fernflower cache (~129 MB)
- `~/.gradle/caches/modules-2/` — transitive Java libs (Guava, JOpt, Apache Commons, etc.)
- `build/rfg/minecraft-src/java/` — **1833 deobfuscated `.java` files, 23 MB** (the "ground truth" source)

## Toolchain pipeline (what `setupDecompWorkspace` actually does)

1. Download MC 1.7.10 client + server jars, merge sides
2. Download Forge 10.13.4.1614-universal
3. Apply SpecialSource deobf (notch names → SRG names like `func_12345_a`)
4. Apply Forge Access Transformers (`fml_at.cfg`, `forge_at.cfg`)
5. Apply Exceptor (exception table fixups)
6. Fernflower decompile SRG jar
7. Apply MCP patches + cleanup
8. Apply 311 Forge source patches
9. Remap SRG names → MCP human-readable names (`worldObj`, `getBlockMetadata`)
10. Compile patched MC → `patchedMc.jar` on your mod's classpath

## Runtime commands

```bash
./gradlew setupDecompWorkspace   # one-time, 5-15 min + ~270 MB download
./gradlew runClient              # launch MC with your mod loaded
./gradlew runServer              # dedicated server
./gradlew build                  # produce obf + deobf mod jars in build/libs/
./gradlew sourcesJar             # ship your source
```

## Source layout for modding

- `src/main/java/` — **your mod code** (friend edits here). Template mod at `com/myname/mymodid/`.
- `src/main/resources/` — mod assets + `mcmod.info`.
- `build/rfg/minecraft-src/java/` — **read-only deobf MC + Forge source** — browse/grep this as ground truth.
- `gradle.properties` — set `modId`, `modGroup`, `modName`.

To port MC behavior into a simulation: grep `build/rfg/minecraft-src/java/net/minecraft/` for the class you care about. Example entry points:
- `world/World.java` — block reads/writes, tick loop
- `entity/Entity.java` — base physics (`moveEntity`)
- `block/Block.java` — block definitions
- `server/MinecraftServer.java` — tick scheduler

## Shippable bundle

Size: **2.6 MB** (mdk dir minus `build/` and `.gradle/`). Friend runs `./setup-and-launch.sh` to download deps, compile, smoke-test (launch + auto-close after 30s).


# Doc 19: `flashmine/tools/oracle-mdk-1.7.10/docs/FAQ.md` {#doc-19}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tools/oracle-mdk-1.7.10/docs/FAQ.md`*

# Things we cannot protect you from (yet)

### Select an mcp conf dir for the deobfuscator

You may or may not run into this popup. For now, the only solution is to point the deopfuscator into the right direction.

![](http://i.imgur.com/gzBMLrr.png)

Solution: Point it to `~/.gradle/caches/minecraft/net/minecraftforge/forge/1.7.10-10.13.4.1614-1.7.10/unpacked/conf`. On Windows, please use `%USERPROFILE%/.gradle/caches/minecraft/net/minecraftforge/forge/1.7.10-10.13.4.1614-1.7.10/unpacked/conf`.


# Doc 20: `flashmine/tools/oracle-mdk-1.7.10/docs/migration.md` {#doc-20}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tools/oracle-mdk-1.7.10/docs/migration.md`*

# Migration guides

## Generic migration
Migration for the typical mod which doesn't use anything special but Minecraft forge and some library dependencies.
For core plugin, Mixins, shadowing, access transformers, ASM or etc. you'll need to do some extra steps.
If they are missing in this document - we will gladly receive your suggestions/contribution.

1. Copy and replace all files from [template](https://github.com/GTNewHorizons/ExampleMod1.7.10/releases/download/master-packages/migration.zip) to your repository, but `build.gradle`
2. Copy all repositories from your `build.gradle(.kts)` to `repositories.gradle`
3. Copy all dependencies from your `build.gradle(.kts)` to `dependecies.gradle`
4. replace your `build.gradle(.kts)` with `build.gradle` from template. In case you have written some custom tasks/configurations not present in the template - move them into `addon.gradle`. It will automatically be integrated if present.
5. Adapt `gradle.properties` to your mod
6. Ensure `src/main/resources/mcmod.info` contains `${modId}`, `${modName}`. `${modVersion}` and `${minecraftVersion}`
7. Re-import the project to your IDE (e.g. restart with clean caches in IntelliJ IDEA)
8. Run `./gradlew clean setupDecompWorkspace`

## Mixin configuration
For the reference checkout the [example mixin configuration branch](https://github.com/GTNewHorizons/ExampleMod1.7.10/tree/example-mixins) of the template.

1. Extract mixins package and plugin configuration from `mixins.yourModId.json` to `gradle.properties`
2. Implement MixinPlugin according to example from the reference
3. Remove mixins.mymodid.json


# Doc 21: `flashmine/tools/oracle-mdk-1.7.10/docs/porting.md` {#doc-21}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tools/oracle-mdk-1.7.10/docs/porting.md`*

# Porting guidelines

This is a list of steps which should help you on your probably not so easy journey of porting some mod:

### 1. Setting up repository and build system
1. Checkout any information in mod REAMDE/Wiki/Docs to find out if there are any special tasks/configs that need to be applied to the build
2. Fork original repository to preserve commit history
3. Apply build migration as explained in [migration guidelines](migration.md) on your fork

### 2. Refining the fork
Try to get rid of dependencies on concrete jars (usually in the `lib` folder) if any present. This way it will be much easier to change (upgrade/downgrade) your project dependencies, when needed.

Check if they are in maven repository (usually authors put such information in the project readme), if it isn't accessible but project is open source with permissive enough license (e.g., MIT) - you still can publish them yourselves:
   1. Fork the repository
   2. Drop `jitpack.yml` and `.github/workflows/gradle.yml` in project root. You can find this file in this repository root.
   3. Make sure everything builds from console by running `./gradlew clean setupCIWorkspace`
   4. If all is fine/after fixing the errors - make a tag on Github or using console, this should trigger Github build hook and generate a release
   4. Lookup forked repository on `https://jitpack.io/`
   5. Find your release and click "Get it", which should scroll you down to the example of how to add the dependency (make sure you have jitpack repository in mod you are porting)
   6. Checkout build log beside button you clicked to make sure it succeeds

Now when you are sure dependency is available in maven repository - just add it as a normal gradle dependency in `dependencies.gradle`.

If there is not online dependency available, you may upload it as a jar to jitpack, see [jitpack single file publishing thread](https://gist.github.com/jitpack-io/f928a858aa5da08ad9d9662f982da983). Please ensure, that you have the rights to do so!

There may also be a case where mods depend on another mods - then you'll need to port any dependencies first. (Yay, dependency hell! :D)

### 3. Preparing for porting
Try to build the project and see check what types of errors are you getting. Generally, there should be 2 types of errors you encounter:
   - Missing references to packages/classes/methods/fields/parameters. Things get renamed, moved, restructured, removed or even not yet exist. That's the straightforward part - you'll need to adjust references and way things are invoked.
    In case of missing things, you'll either need to implement something that's imitates missing parts or resign from some functionality
   - Build related errors (e.g., something that is a part of the mod in never versions previously was an external library - you'll need to add it as a dependencies)

Fix all build related errors (so build system won't get in your way)

### 4. Porting the mod
After all these preparations nothing should be in the way of porting the mod, the only thing left is the actual code to change, which probably is a most tedious part of this process.

Good approach is to start working with smaller things first, building up your confidence in how the mod works and gradually approaching more complex stuff, here is a general algorithm:
   1. Begin with fixing moved/renamed things by deleting all bad imports and with help of the IDE re-import equivalents if present.
      IntelliJ IDEA has settings for unambiguous auto-import and import optimization on the fly, which can greatly speedup the process. Just pay attention to what is actually imported.
   2. Remove all nonworking code which is not easily fixable (e.g., class only introduced in newer forge) and provide stubs in its place.
       For example, replace reference to method of non existing class with your method in your class, it can have an empty body and mocked return so the code can compile and run without issues.
       Do not forget to track all things you've stubbed, if you are working on port alone - TODOs should be sufficient (most IDEs have a built in TODO browser).
   3. Build the project and attempt to run it
   4. If there were any critical errors which cause Minecraft to crash or mod to not work - try fixing them first, so you can test your changes
   5. Start fixing small things, ones that you think you have most chances to fix and work your way up
   6. If any there is any feature that is not worth it's time or you simply don't know how to do it - consider dropping it entirely and open an issue in your repository where you'll explain your findings and blockers.
       Maybe somebody with greater knowledge/more time/motivation will try to take bite at it.
9. Fix bugs you've introduced when porting.
    It is uncommon for mods to have lots of workarounds and hidden connections.
    You'll need to test things and check if they work as intended (gl;hf ;p)

### 5. Final words

If after reading this, you are not discouraged and still want to port it - good luck porting it! You'll definitively need it.


# Doc 22: `flashmine/tools/oracle-render-mdk-1.7.10/README.md` {#doc-22}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tools/oracle-render-mdk-1.7.10/README.md`*

# Example Forge Mod for Minecraft 1.7.10

[![](https://jitpack.io/v/GTNewHorizons/ExampleMod1.7.10.svg)](https://jitpack.io/#GTNewHorizons/ExampleMod1.7.10)
[![](https://github.com/GTNewHorizons/ExampleMod1.7.10/actions/workflows/build-and-test.yml/badge.svg)](https://github.com/GTNewHorizons/ExampleMod1.7.10/actions/workflows/build-and-test.yml)

An example mod for Minecraft 1.7.10 with Forge focussed on a stable, updatable setup.

<!-- omit in toc -->
### Table of Contents

* [Example Forge Mod for Minecraft 1.7.10](#example-forge-mod-for-minecraft-1710)
    * [Motivation](#motivation)
    * [Help! I'm stuck!](#help-im-stuck)
    * [Getting started](#getting-started)
    * [Features](#features)
    * [Files](#files)
    * [Forge's Access Transformers](#forges-access-transformers)
    * [Mixins](#mixins)
    * [Advanced](#advanced)
    * [Feedback wanted](#feedback-wanted)


### Motivation

We had our fair share in struggles with build scripts for Minecraft Forge. There are quite a few pitfalls from non-obvious error messages. This Example Project provides you a build system you can adapt to over 90% of Minecraft Forge mods and can easily be updated if need be.

### Help! I'm stuck!

We all have been there! Check out our [FAQ](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/docs/FAQ.md). If that doesn't help, please open an issue.

### Getting started

Creating mod from scratch:
1. Unzip [project starter](https://github.com/GTNewHorizons/ExampleMod1.7.10/releases/download/master-packages/starter.zip) into project directory.
2. Replace placeholders in LICENSE-template and rename it to LICENSE, or remove LICENSE-template and put any other license you like on your code. This is an permissive OSS project and we encourage you participate in OSS movement by having permissive license like one in template. You can find out pros and cons of OSS software in [this article](https://www.freecodecamp.org/news/what-is-great-about-developing-open-source-and-what-is-not/)
3. Ensure your project is under VCS. For example initialise git repository by running `git init; git commit --message "initialized repository"`.
4. Replace placeholders (edit values in gradle.properties, change example package and class names, etc.)
5. Run `./gradlew setupDecompWorkspace`
6. Run `./gradlew build`
6. Make sure to check out the rest sections of this file.
7. You are good to go!

We also have described guidelines for existing mod [migration](docs/migration.md) and [porting](docs/porting.md)

### Features

 - Updatable: Replace [`build.gradle`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/build.gradle) with a newer version
 - Optional API artifact (.jar)
 - Optional version replacement in Java files
 - Optional shadowing of dependencies
 - Simplified setup of Mixin and example
 - Scala support (add sources under `src/main/scala/` instead of `src/main/java/`)
 - Optional named developer account for consistent player progression during testing
 - Boilerplate forge mod as starting point
 - Improved warnings for pitfalls
 - Git Tags integration for versioning
 - [Jitpack](https://jitpack.io) CI
 - GitHub CI:
   - Releasing your artifacts on new tags pushed. Push git tag named after version (e.g. 1.0.0) which will trigger a release of artifacts with according names.
   - Running smoke test for server startup. On any server crash occurring workflow will fail and print the crash log.

### Files
 - [`build.gradle`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/build.gradle): This is the core script of the build process. You should not need to tamper with it, unless you are trying to accomplish something out of the ordinary. __Do not touch this file! You will make a future update near impossible if you do so!__
 - [`gradle.properties`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/gradle.properties): The core configuration file. It includes
 - [`dependencies.gradle[.kts]`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/dependencies.gradle): Add your mod's dependencies in this file. This is separate from the main build script, so you may replace the [`build.gradle`](https://github.com/SinTh0r4s/ExampleMod1.7.10/blob/main/build.gradle) if an update is available.
 - [`repositories.gradle[.kts]`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/repositories.gradle): Add your dependencies' repositories. This is separate from the main build script, so you may replace the [`build.gradle`](https://github.com/SinTh0r4s/ExampleMod1.7.10/blob/main/build.gradle) if an update is available.
 - `addon.gradle[.kts]`: Any additional build logic. This is separate from the main build script, so you may replace the [`build.gradle`](https://github.com/SinTh0r4s/ExampleMod1.7.10/blob/main/build.gradle) if an update is available. See [Advanced](#advanced) for more details.
 - [`jitpack.yml`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/jitpack.yml): Ensures that your mod is available as import over [Jitpack](https://jitpack.io).
 - [`.github/workflows/gradle.yml`](https://github.com/GTNewHorizons/ExampleMod1.7.10/blob/main/.github/workflows/gradle.yml): A simple CI script that will build your mod any time it is pushed to `master` or `main` and publish the result as release in your repository. This feature is free with GitHub if your repository is public.

### Forge's Access Transformers

You may activate Forge's Access Transformers by defining a configuration file in `gradle.properties`.

Check out the [`example-access-transformers`](https://github.com/GTNewHorizons/ExampleMod1.7.10/tree/example-access-transformers) branch for a working example!

> [!WARNING]
> Access Transformers are bugged and will deny you any sources for the decompiled Minecraft! Your development environment will still work, but you might face some inconveniences. For example, IntelliJ will not permit searches in dependencies without attached sources.

### Mixins

[Mixins](https://github.com/SpongePowered/Mixin) are used to modify vanilla or mod/library code during runtime without having to edit, recompile, and redistribute the original code. For example, mixins can change a hardcoded value, redirect a method call, inject additional code, access private fields/methods, make a class implement your interface, and more. Mixins are an advanced feature which most normal mods will not require.

Documentation about Mixin features can be found here: [Mixin Wiki](https://github.com/SpongePowered/Mixin/wiki) and [MixinExtras Wiki](https://github.com/LlamaLad7/MixinExtras/wiki)

There are many examples of mixins in these mods: [Hodgepodge](https://github.com/GTNewHorizons/Hodgepodge) and [Angelica](https://github.com/GTNewHorizons/Angelica)

To enable Mixins in your project, follow one of the example commits:
- use [normal mixins](https://github.com/GTNewHorizons/ExampleMod1.7.10/commit/beba55615fa8337b7639f0d5b18db6cc8d4826be) for basic and quick registration
- use [GTNH IMixins](https://github.com/GTNewHorizons/ExampleMod1.7.10/commit/055cd4f18765a421a86c706f53b62116988297e3) (recommended) for the same thing as below, but in a less verbose and more unified manner using the IMixins api
- use [GTNH Early/Late mixins](https://github.com/GTNewHorizons/ExampleMod1.7.10/commit/c4df59d92164775b69451f3e690239e93d1fc979) to have full control over the registration logic and check for presence of other mods during runtime to load your mixins

The extra required dependencies are handled automatically after mixins are enabled.

### Advanced

If your project requires custom gradle commands you may add a `addon.gradle[.kts]` to your project. It will be added automatically to the build script. Although we recommend against it, it is sometimes required. When in doubt, feel free to ask us about it. You may break future updates of this build system!
If you need access to properties modified later in the buildscript, you can also use a `addon.late.gradle[.kts]`.
For local tweaks that you don't want to commit to Git, like adding extra JVM arguments for testing, use `addon[.late].local.gradle[.kts]`.

### Feedback wanted

If you tried out this build script we would love to head your opinion! Is there any feature missing for you? Did something not work? Please open an issue and we will try to resolve it asap!

Happy modding,\
[SinTh0r4s](https://github.com/SinTh0r4s), [TheElan](https://github.com/TheElan) and [basdxz](https://github.com/basdxz)


# Doc 23: `flashmine/tools/oracle-render-mdk-1.7.10/REQUIREMENTS.md` {#doc-23}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tools/oracle-render-mdk-1.7.10/REQUIREMENTS.md`*

# Minecraft 1.7.10 MDK Requirements

## Host machine prerequisites

| Item | Version | Notes |
|---|---|---|
| OS | Linux / macOS / Windows | tested on Ubuntu 24.04 |
| JDK (for Gradle daemon) | 21+ (25 preferred) | pinned in `gradle/gradle-daemon-jvm.properties`. apt: `openjdk-25-jdk-headless` |
| JDK 8 (for MC runtime) | auto-provisioned | RFG downloads Mojang JDK 8u202 via Gradle toolchain on first run |
| Display server (X11/Wayland) | any | only needed for `runClient`; headless CI can use `xvfb-run` |
| Free disk | ~2 GB | Gradle caches + build artifacts |
| Network (first run only) | yes | ~270 MB one-time download |
| GPU | OpenGL 2.1+ | MC 1.7.10 uses fixed-function GL |

## What first-run downloads and caches (not in the bundle)

Locations after `./gradlew setupDecompWorkspace`:

- `~/.gradle/caches/minecraft/` — Minecraft 1.7.10 client jar, Forge 10.13.4.1614, LWJGL natives (~135 MB)
- `~/.gradle/caches/retro_futura_gradle/` — vanilla assets (686 files), MCP 9.05 mappings, Fernflower cache (~129 MB)
- `~/.gradle/caches/modules-2/` — transitive Java libs (Guava, JOpt, Apache Commons, etc.)
- `build/rfg/minecraft-src/java/` — **1833 deobfuscated `.java` files, 23 MB** (the "ground truth" source)

## Toolchain pipeline (what `setupDecompWorkspace` actually does)

1. Download MC 1.7.10 client + server jars, merge sides
2. Download Forge 10.13.4.1614-universal
3. Apply SpecialSource deobf (notch names → SRG names like `func_12345_a`)
4. Apply Forge Access Transformers (`fml_at.cfg`, `forge_at.cfg`)
5. Apply Exceptor (exception table fixups)
6. Fernflower decompile SRG jar
7. Apply MCP patches + cleanup
8. Apply 311 Forge source patches
9. Remap SRG names → MCP human-readable names (`worldObj`, `getBlockMetadata`)
10. Compile patched MC → `patchedMc.jar` on your mod's classpath

## Runtime commands

```bash
./gradlew setupDecompWorkspace   # one-time, 5-15 min + ~270 MB download
./gradlew runClient              # launch MC with your mod loaded
./gradlew runServer              # dedicated server
./gradlew build                  # produce obf + deobf mod jars in build/libs/
./gradlew sourcesJar             # ship your source
```

## Source layout for modding

- `src/main/java/` — **your mod code** (friend edits here). Template mod at `com/myname/mymodid/`.
- `src/main/resources/` — mod assets + `mcmod.info`.
- `build/rfg/minecraft-src/java/` — **read-only deobf MC + Forge source** — browse/grep this as ground truth.
- `gradle.properties` — set `modId`, `modGroup`, `modName`.

To port MC behavior into a simulation: grep `build/rfg/minecraft-src/java/net/minecraft/` for the class you care about. Example entry points:
- `world/World.java` — block reads/writes, tick loop
- `entity/Entity.java` — base physics (`moveEntity`)
- `block/Block.java` — block definitions
- `server/MinecraftServer.java` — tick scheduler

## Shippable bundle

Size: **2.6 MB** (mdk dir minus `build/` and `.gradle/`). Friend runs `./setup-and-launch.sh` to download deps, compile, smoke-test (launch + auto-close after 30s).


# Doc 24: `flashmine/tools/oracle-render-mdk-1.7.10/docs/FAQ.md` {#doc-24}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tools/oracle-render-mdk-1.7.10/docs/FAQ.md`*

# Things we cannot protect you from (yet)

### Select an mcp conf dir for the deobfuscator

You may or may not run into this popup. For now, the only solution is to point the deopfuscator into the right direction.

![](http://i.imgur.com/gzBMLrr.png)

Solution: Point it to `~/.gradle/caches/minecraft/net/minecraftforge/forge/1.7.10-10.13.4.1614-1.7.10/unpacked/conf`. On Windows, please use `%USERPROFILE%/.gradle/caches/minecraft/net/minecraftforge/forge/1.7.10-10.13.4.1614-1.7.10/unpacked/conf`.


# Doc 25: `flashmine/tools/oracle-render-mdk-1.7.10/docs/migration.md` {#doc-25}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tools/oracle-render-mdk-1.7.10/docs/migration.md`*

# Migration guides

## Generic migration
Migration for the typical mod which doesn't use anything special but Minecraft forge and some library dependencies.
For core plugin, Mixins, shadowing, access transformers, ASM or etc. you'll need to do some extra steps.
If they are missing in this document - we will gladly receive your suggestions/contribution.

1. Copy and replace all files from [template](https://github.com/GTNewHorizons/ExampleMod1.7.10/releases/download/master-packages/migration.zip) to your repository, but `build.gradle`
2. Copy all repositories from your `build.gradle(.kts)` to `repositories.gradle`
3. Copy all dependencies from your `build.gradle(.kts)` to `dependecies.gradle`
4. replace your `build.gradle(.kts)` with `build.gradle` from template. In case you have written some custom tasks/configurations not present in the template - move them into `addon.gradle`. It will automatically be integrated if present.
5. Adapt `gradle.properties` to your mod
6. Ensure `src/main/resources/mcmod.info` contains `${modId}`, `${modName}`. `${modVersion}` and `${minecraftVersion}`
7. Re-import the project to your IDE (e.g. restart with clean caches in IntelliJ IDEA)
8. Run `./gradlew clean setupDecompWorkspace`

## Mixin configuration
For the reference checkout the [example mixin configuration branch](https://github.com/GTNewHorizons/ExampleMod1.7.10/tree/example-mixins) of the template.

1. Extract mixins package and plugin configuration from `mixins.yourModId.json` to `gradle.properties`
2. Implement MixinPlugin according to example from the reference
3. Remove mixins.mymodid.json


# Doc 26: `flashmine/tools/oracle-render-mdk-1.7.10/docs/porting.md` {#doc-26}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/tools/oracle-render-mdk-1.7.10/docs/porting.md`*

# Porting guidelines

This is a list of steps which should help you on your probably not so easy journey of porting some mod:

### 1. Setting up repository and build system
1. Checkout any information in mod REAMDE/Wiki/Docs to find out if there are any special tasks/configs that need to be applied to the build
2. Fork original repository to preserve commit history
3. Apply build migration as explained in [migration guidelines](migration.md) on your fork

### 2. Refining the fork
Try to get rid of dependencies on concrete jars (usually in the `lib` folder) if any present. This way it will be much easier to change (upgrade/downgrade) your project dependencies, when needed.

Check if they are in maven repository (usually authors put such information in the project readme), if it isn't accessible but project is open source with permissive enough license (e.g., MIT) - you still can publish them yourselves:
   1. Fork the repository
   2. Drop `jitpack.yml` and `.github/workflows/gradle.yml` in project root. You can find this file in this repository root.
   3. Make sure everything builds from console by running `./gradlew clean setupCIWorkspace`
   4. If all is fine/after fixing the errors - make a tag on Github or using console, this should trigger Github build hook and generate a release
   4. Lookup forked repository on `https://jitpack.io/`
   5. Find your release and click "Get it", which should scroll you down to the example of how to add the dependency (make sure you have jitpack repository in mod you are porting)
   6. Checkout build log beside button you clicked to make sure it succeeds

Now when you are sure dependency is available in maven repository - just add it as a normal gradle dependency in `dependencies.gradle`.

If there is not online dependency available, you may upload it as a jar to jitpack, see [jitpack single file publishing thread](https://gist.github.com/jitpack-io/f928a858aa5da08ad9d9662f982da983). Please ensure, that you have the rights to do so!

There may also be a case where mods depend on another mods - then you'll need to port any dependencies first. (Yay, dependency hell! :D)

### 3. Preparing for porting
Try to build the project and see check what types of errors are you getting. Generally, there should be 2 types of errors you encounter:
   - Missing references to packages/classes/methods/fields/parameters. Things get renamed, moved, restructured, removed or even not yet exist. That's the straightforward part - you'll need to adjust references and way things are invoked.
    In case of missing things, you'll either need to implement something that's imitates missing parts or resign from some functionality
   - Build related errors (e.g., something that is a part of the mod in never versions previously was an external library - you'll need to add it as a dependencies)

Fix all build related errors (so build system won't get in your way)

### 4. Porting the mod
After all these preparations nothing should be in the way of porting the mod, the only thing left is the actual code to change, which probably is a most tedious part of this process.

Good approach is to start working with smaller things first, building up your confidence in how the mod works and gradually approaching more complex stuff, here is a general algorithm:
   1. Begin with fixing moved/renamed things by deleting all bad imports and with help of the IDE re-import equivalents if present.
      IntelliJ IDEA has settings for unambiguous auto-import and import optimization on the fly, which can greatly speedup the process. Just pay attention to what is actually imported.
   2. Remove all nonworking code which is not easily fixable (e.g., class only introduced in newer forge) and provide stubs in its place.
       For example, replace reference to method of non existing class with your method in your class, it can have an empty body and mocked return so the code can compile and run without issues.
       Do not forget to track all things you've stubbed, if you are working on port alone - TODOs should be sufficient (most IDEs have a built in TODO browser).
   3. Build the project and attempt to run it
   4. If there were any critical errors which cause Minecraft to crash or mod to not work - try fixing them first, so you can test your changes
   5. Start fixing small things, ones that you think you have most chances to fix and work your way up
   6. If any there is any feature that is not worth it's time or you simply don't know how to do it - consider dropping it entirely and open an issue in your repository where you'll explain your findings and blockers.
       Maybe somebody with greater knowledge/more time/motivation will try to take bite at it.
9. Fix bugs you've introduced when porting.
    It is uncommon for mods to have lots of workarounds and hidden connections.
    You'll need to test things and check if they work as intended (gl;hf ;p)

### 5. Final words

If after reading this, you are not discouraged and still want to port it - good luck porting it! You'll definitively need it.


# Doc 27: `flashmine/vanilla/README.md` {#doc-27}

*Absolute path: `/home/infatoshi/games/minecraft/flashmine/vanilla/README.md`*

# vanilla/

Vanilla Minecraft 1.7.10 assets, used as ground truth for the flashmine oracle.

- `1.7.10.jar` — vanilla client jar (sourced from `~/.minecraft/versions/1.7.10/`, originally Prism Launcher / Mojang licensed install). 5.0 MB.
- `src/` — decompiled, MCP-mapped Java source (1386 files, ~12 MB), extracted from RetroFuturaGradle's fernflower cache. Use as read-only reference for porting tick subsystems and matching float op order. **Do not modify.**

## Sources of truth

| Need                       | File                                              |
|----------------------------|---------------------------------------------------|
| Entity physics             | `src/net/minecraft/entity/Entity.java`            |
| Player movement            | `src/net/minecraft/entity/EntityLivingBase.java`  |
| World tick / random ticks  | `src/net/minecraft/world/World.java`              |
| Chunk format               | `src/net/minecraft/world/chunk/Chunk.java`        |
| Block behavior             | `src/net/minecraft/block/Block.java` and subclasses |
| Lighting                   | `src/net/minecraft/world/EnumSkyBlock.java`, `World.updateLightByType` |
| Renderer (rasterized GL)   | `src/net/minecraft/client/renderer/RenderGlobal.java`, `WorldRenderer.java` |

When in doubt about vanilla behavior, grep here before guessing.


# Doc 28: `mc-oracle/CLAUDE.md` {#doc-28}

*Absolute path: `/home/infatoshi/games/minecraft/mc-oracle/CLAUDE.md`*

# CLAUDE.md — mc-oracle workspace

Operational rules for this workspace. Companion to `SPEC.md` (feature list)
and `DESIGN.md` (clean-room naming + strip scope).

## What this workspace is

- Private workspace. **Never committed to PufferLib.** The decompiled 1.7.10
  Java tree under `mcp/src/minecraft_server/` is Mojang-copyrighted source.
- Purpose: differential oracle. Instrumented vanilla server emits canonical
  per-tick JSONL deltas. A future clean-room C sim in `PufferLib/ocean/voxel/`
  is diffed against it.
- Not a mod. We edit the MCP decompiled source and recompile via MCP's own
  `recompile.sh`. No Mixin, no Forge.

## What lives where

- `mcp/` — MCP 9.08 install; decompiled + patched source at `mcp/src/minecraft_server/`.
- `mcp/jars/` — world saves + server.properties; runtime output goes here.
- `mc_home/.minecraft/` — fake launcher home used by MCP to discover version JSON and libraries. Do not treat as a real install.
- `Dockerfile` — build context for `mcp-runner` image (Java 8 + Python 2 + patch/astyle).
- `diff_runs.py` — canonicalized JSONL differ.
- `DESIGN.md` — naming map (creeper→bomber, etc.) + keep/cut scope.
- `SPEC.md` — exhaustive feature list with status checkboxes. Update on every completed unit.

## Commands

Build the runner image (once, or after Dockerfile edit):
```
cd ~/mc-oracle && sudo docker build -t mcp-runner .
```

Recompile after editing Java source under `mcp/src/minecraft_server/`:
```
sudo docker run --rm -u $(id -u):$(id -g) \
  -v ~/mc-oracle/mcp:/mcp -v ~/mc-oracle/mc_home:/home/u \
  -e HOME=/home/u -w /mcp mcp-runner ./recompile.sh
```

Run oracle with delta logging and tick cap:
```
sudo docker run --rm -u $(id -u):$(id -g) \
  -v ~/mc-oracle/mcp:/mcp -v ~/mc-oracle/mc_home:/home/u \
  -e HOME=/home/u -e MC_ORACLE_MAX_TICKS=400 \
  -e MC_ORACLE_LOG=/mcp/jars/oracle.jsonl \
  -w /mcp mcp-runner ./startserver.sh
```

Diff two runs:
```
python3 ~/mc-oracle/diff_runs.py ~/mc-oracle/mcp/jars/oracle_run1.jsonl \
                                  ~/mc-oracle/mcp/jars/oracle_run2.jsonl
```

## Env vars the oracle respects

- `MC_ORACLE_MAX_TICKS` — run exactly N ticks then shutdown; also disables the 50ms tick sleep.
- `MC_ORACLE_LOG` — file path to write canonical JSONL deltas. Unset → no logging.

## Invariants (keep true)

- **Logical determinism**: same seed + same tick count + same action sequence must yield identical JSONL output. Verified for seed=12345, 400 ticks, no players. Any patch that breaks this is a regression — run `diff_runs.py` after any Java edit.
- **Seed plumbing**: `World.rand` and `updateLCG` are seeded from `worldInfo.getSeed()` at end of `World` ctor. Do not reintroduce `new Random()` without a fixed seed or entity-id derivation.
- **Canonical output order**: `setBlock` logs in call order (which is deterministic given pinned RNG); entity sweep sorts by `getEntityId()` ascending. If you add a new log type, keep it canonical.
- **Oracle tree isolation**: `~/mc-oracle/` must not be added to any PufferLib git index, symlink, or submodule. The C sim references the *spec*, not this source.

## Clean-room discipline

When writing C sim code in PufferLib:
- Read the Java source here to *understand*. Close the tab before writing C.
- Never carry over Mojang method names, variable names, or comment text.
- Use our names from `DESIGN.md` (creeper → bomber, etc.).
- Keep a per-system short design note in `PufferLib/docs/voxel/<system>.md` describing the algorithm in our own words. These are the audit trail.

## Non-goals

- Bit-exact Anvil region file parity. (Hashmap iteration order and gzip dictionary noise make this infeasible and pointless; logical state is what matters.)
- Modded-client compatibility.
- Multi-player / network protocol fidelity. Oracle bypasses network; actions come via a driver process, not MC packets.
- 3D rendering, textures, HUD polish, audio. See "Viz scope" below — viz is a tiny 2D-slice replay viewer, not a game client.

## Viz scope (one source of truth)

Viz exists **only** for `puffer eval` policy replay — watching a trained agent
play for human verification. It is never called during training.

What viz is:
- One `c_render(Voxel*)` function at the bottom of `PufferLib/ocean/voxel/voxel.h`.
- Lazy-init raylib window on first call (pattern: `ocean/snake/snake.h:314-322`).
- 2D top-down slice at player y-level, flat-color tiles by block id (no textures).
- Player arrow, mob dots, hotbar, health/hunger/XP bars, last-action overlay.
- PageUp/PageDown to slice up/down through the world.
- Budget: ~200-400 LOC.

What viz is not:
- 3D renderer, chunk mesher, textures, shaders, sky, weather visuals, particles.
- First-person camera, hand animation, inventory/crafting screens, container GUIs.
- Audio of any kind.

Rationale: training throughput is the real constraint. A 2D slice proves the
policy is doing the right thing (walked to tree → chopped → crafted pickaxe →
mined) with 10× less work than a 3D client. If the 2D view turns out to be
insufficient during debugging, extend — but don't build speculatively.

Texture/art concerns effectively vanish: we need a color palette (~175 RGB
triples), not 175 PNG files. No commissioning, no licensing for art assets.

## Voxel sim scope (one source of truth)

The clean-room C sim lives at `PufferLib/ocean/voxel/` and follows the Ocean
env pattern (reference: `PufferLib/ocean/craftax/`). It is **not** in this
workspace — `~/mc-oracle/` is private. Don't cross-reference source files
between the two trees. Read the oracle to understand behavior, close the
tab, write C at the other location.

Required pieces (see `SPEC.md` §16 for ordered build list):
1. `voxel.h` — state struct + `c_init`/`c_reset`/`c_step`/`c_close` + Log struct.
2. `binding.c` + `config/ocean/voxel.ini` — Ocean integration, mirrors `ocean/craftax/`.
3. Chunks (palette + meta + light), block registry, player physics, world gen,
   mob AI, combat, inventory/crafting, containers, redstone, weather, dimensions.
4. `c_render` per "Viz scope" above.

RNG dependency: use the C port at `~/mc-oracle/cleanroom/random/` (bitwise
parity with `java.util.Random` via fdlibm `log`). Copy the `.c`/`.h` into
the PufferLib tree when the voxel env is created — don't symlink across
workspaces. License header on `fdlibm_log.c` must be preserved.

Training vs viz separation: the sim must be callable with `c_render` never
invoked (headless training), and must run at Ocean-comparable SPS (target
~1M steps/sec/env on CPU per the Craftax benchmark we already hit).

## Update rhythm

- Every time a SPEC item changes state (TODO → IN_PROGRESS → DONE), update its checkbox and drop a one-line entry at the bottom of SPEC.md under "Progress log" with the date.
- If you discover a feature not listed in SPEC, add it before implementing. Uncataloged work is the fastest way to ship half a system.
- If you discover a feature listed in SPEC is wrong (scope creep, cut candidate), propose it in this session before deleting.

## Known patches applied (keep in sync with SPEC)

See SPEC.md §"Oracle instrumentation" for the current list. Current set:
`World.java`, `Entity.java`, `EntityItem.java`, `Item.java`, `MinecraftServer.java`,
`RegionFile.java`, plus new `oracle/OracleLog.java`.


# Doc 29: `mc-oracle/DESIGN.md` {#doc-29}

*Absolute path: `/home/infatoshi/games/minecraft/mc-oracle/DESIGN.md`*

# Voxel Env Oracle — Design

Private workspace. Nothing in this dir is committed to PufferLib.
The PufferLib env is clean-room, written from the spec below, not copied from
the decompiled source tree at `mcp/src/minecraft_server/`.

## Naming map (Mojang term -> clean-room term)

| Mojang          | Clean-room    | Notes                                    |
|-----------------|---------------|------------------------------------------|
| Minecraft       | Voxel / Ocean "voxel" env | Never use "Minecraft" in repo |
| Overworld       | surface       |                                          |
| Nether          | under         | DIM-1                                    |
| End             | void          | DIM1                                     |
| Creeper         | bomber        | suicide mob                              |
| Zombie          | walker        | melee undead                             |
| Skeleton        | archer        | ranged undead                            |
| Spider          | crawler       |                                          |
| Enderman        | shade         | teleport mob                             |
| Ghast           | drifter       | nether ranged                            |
| Blaze           | ember         | nether flier                             |
| Pigman          | brute         | nether humanoid                          |
| Villager        | trader        |                                          |
| Wolf            | hound         |                                          |
| Iron Golem      | warden        |                                          |
| Nether portal   | under-gate    |                                          |
| End portal      | void-gate     |                                          |
| Ender Dragon    | void-lord     | boss                                     |
| Wither          | triple        | boss (3 heads, keep mechanic)            |
| Redstone        | pulse-wire    |                                          |
| Diamond         | prism         |                                          |
| Emerald         | shard         |                                          |
| Obsidian        | glass-stone   |                                          |
| XP              | essence       |                                          |

Block ids, item ids, mob ids: use our own enum, not Mojang's numeric ids.
Texture filenames: `resources/voxel/tile_grass.png` etc., never `dirt.png`
matching Mojang's atlas naming.

## Feature strip list (1.7.10 scope)

### Keep (port to C)
- World gen: overworld terrain (perlin-ish noise), biomes (plains, forest,
  desert, swamp, tundra, jungle, mushroom, ocean, river, extreme hills).
  Caves, ravines, ore distribution, dungeons, villages, strongholds, mineshafts.
- Under dimension: netherrack terrain, fortresses, lava oceans.
- Void dimension: end islands, void-lord boss arena.
- All vanilla blocks (~175 in 1.7.10) and items. Crafting, smelting, brewing,
  enchanting tables, anvils.
- Mob AI: pathfinding (A* on walkable tiles), aggro, day/night spawn, mob caps.
- Player physics: AABB sweep, stepping, swimming, sneaking, sprinting, fall damage.
- Hunger + health + XP.
- Redstone. Yes, keep. Wiring + repeaters + pistons + comparators. It's a
  reasoning/credit-assignment testbed even if most RL tasks won't touch it.
- Day/night cycle, weather (rain, thunder), lightning mob conversion.
- Inventory + hotbar + armor + chests + furnaces + hoppers.
- Beds, spawn points, respawn.
- Villager trading (1.7.10 era — simpler than post-1.8).

### Cut (never port)
- All rendering: Java client, LWJGL, shaders, particles, beacon beams, weather visuals.
- Sound engine, music, note blocks audio (keep note-block mechanic, discard audio).
- Resource packs, language files, localization.
- Auth / Mojang session / Realms / server-list ping / LAN discovery.
- Main-menu GUIs, options screen, video settings, multiplayer screen.
- Achievements system (replace with our own RL task rewards).
- Statistics tracking (we emit our own Log struct).
- F3 debug, chat commands UI, book-and-quill text rendering.
- Maps (cartography item), paintings, item frames visuals (keep as storage).
- Command blocks, structure blocks.
- Fireworks (keep recipe + item, skip particle effects).
- Network protocol (server <-> client packets). Oracle uses internal tick RPC,
  not the MC wire protocol. C sim has no network layer.

### Defer (v2)
- Ocean monuments (added 1.8 — not in 1.7.10 anyway).
- Elytra, shulkers, End cities (1.9+).
- Shield, crossbow, trident (post-1.8).

## Oracle workflow

1. Decompiled server lives at `mcp/src/minecraft_server/`. Read-only reference.
2. Instrument 6 files (to be identified in next phase):
   - `MinecraftServer.java` — tick loop, replace 50ms sleep with RPC step
   - `WorldServer.java` — per-dim tick, capture block/entity deltas
   - `World.setBlock` callsites — delta ring buffer
   - `Entity.onUpdate` / `EntityLiving.updateAITasks` — entity deltas + RNG pin
   - `NetHandlerPlayServer` — bypass network, inject actions
   - `Random`-producing factories — seed pin
3. Recompile via `recompile.sh`, repackage server jar.
4. Golden-trace harness: Python driver opens Unix socket, sends
   `(seed, initial_snapshot, action_sequence)`, reads `(tick_deltas[])` from jar.
5. Same sequence replayed in C sim, asserted equal per the tolerance tiers
   defined in the earlier chat summary (bitwise for discrete, atol/rtol for
   floats, distributional for RNG-heavy mob AI).

## Clean-room discipline

- C sim author reads Java source to understand behavior, then closes it and
  writes the C from the mental model. No literal translation.
- No Mojang variable names, comment text, or structural peculiarities
  (e.g., don't replicate their `func_XXXXX_a` obfuscation artifacts).
- Per-system short spec doc in PufferLib repo (e.g., `docs/voxel/worldgen.md`,
  `docs/voxel/redstone.md`) describing the algorithm in our own words.
  These docs are the audit trail that the impl derived from understanding, not copying.
- Keep `mc-oracle/` out of all PufferLib git history. Add `~/mc-oracle` to
  nothing — it's a separate dir on disk, no symlinks into the PufferLib tree.


# Doc 30: `mc-oracle/SPEC.md` {#doc-30}

*Absolute path: `/home/infatoshi/games/minecraft/mc-oracle/SPEC.md`*

# SPEC.md — Voxel env (1.7.10 parity, clean-room)

Every feature needed for the PufferLib voxel env, scoped to 1.7.10.
Status: `[ ]` TODO, `[~]` in progress, `[x]` done, `[-]` cut per DESIGN.md.

Items marked **oracle** are instrumentation on the Java reference.
Items marked **sim** are for the clean-room C/CUDA impl in `PufferLib/ocean/voxel/`.
Items marked **viz** are for the `puffer eval` policy-replay viewer (2D top-down slice,
flat colors, no textures — see §17). Viz never runs during training.
Items marked **harness** are the Python diff-test infrastructure.

Naming: use `DESIGN.md` clean-room names in sim/viz code and in user-facing
JSON. Oracle code can keep Mojang names internally since it never ships.

### Version gate

Target is 1.7.10. Anything introduced in 1.8+ is simply not part of this spec.
Do not list post-1.7.10 features as "cut" — they do not exist in the oracle so
there is nothing to cut. Examples of things that **do not belong in this spec**:
elytra, shulkers, shields, off-hand slot, sweep attack, outer end islands,
chorus fruit, end gateways, ocean monuments, guardians, endermite, rabbit,
slime block, sponge-wet→dry smelting, banners, armor stand, iron trapdoor,
stone/andesite/granite/diorite variants, coarse dirt, grass path, prismarine,
sea lantern, beetroot, mending, observer, soul fire/sand anything,
1.9+ combat overhaul, redstone block (actually 1.8), lingering potions,
dragon's breath, rabbit-foot / pufferfish brewing, totems, horse armor variants
beyond iron/gold/diamond, barrier block, command-block minecart variants
beyond the ones already listed, armor-stand interactions, structure blocks,
debug stick, grindstone, smithing table.

Genuine **scope cuts** (exist in 1.7.10 but we choose not to implement) are:
command blocks, chat, creative/adventure/hardcore game modes, audio,
Mojang-art textures (viz uses our own), achievements UI (replaced by RL
reward hooks), multiplayer networking (oracle runs headless, single-agent).

---

## 0. Deterministic foundations

- [x] oracle: pin `World.rand` + `updateLCG` from `worldInfo.getSeed()`
- [x] oracle: derive `Entity.rand` and `Entity.entityUniqueID` from entity id
- [x] oracle: pin static `Item.itemRand`
- [x] oracle: replace `Math.random()` in `EntityItem` with `this.rand`
- [x] oracle: zero chunk write timestamps (`RegionFile`)
- [x] oracle: `MC_ORACLE_MAX_TICKS` env cap
- [ ] oracle: audit remaining `new Random()` / `Math.random()` / `System.nanoTime` / `UUID.randomUUID` call sites (remaining: `TileEntityDispenser`, `TileEntityEnchantmentTable`, `BlockDispenser`, `BlockFurnace`, `BlockBrewingStand`, `BlockChest`, `BlockHopper`, `ContainerEnchantment`, `EnchantmentHelper.enchantmentRand`, `MinecraftServer.field_147146_q`, `AttributeModifier` UUID, `EntityPlayerMP.field_143005_bX`). Not critical for current tests but will matter once players + containers are active.
- [x] harness: JSONL canonical diff (`diff_runs.py`)
- [ ] harness: protobuf/flatbuffers wire format (JSONL hits ~200MB/min)
- [ ] harness: per-tick state-hash so short-circuit diffs don't scan full history
- [ ] sim: 48-bit LCG Random compatible with `java.util.Random` (seed/next/nextInt/nextLong/nextFloat/nextDouble/nextGaussian/nextBytes)
- [ ] sim: global tick counter, per-entity id allocator, deterministic seed plumbing matching oracle formulas

---

## 1. World storage & blocks

### 1.1 Chunk format
- [ ] sim: 16×16×256 chunks, palette-encoded block array (id + 4-bit meta)
- [ ] sim: 16×16 biome array (1 byte)
- [ ] sim: sky light + block light (4-bit each)
- [ ] sim: heightmap (for skylight, grass growth, mob spawning)
- [ ] sim: tile-entity list (chest, furnace, etc. — see §8)
- [ ] sim: entity list per chunk
- [ ] sim: "populated" flag (world gen stage)
- [ ] sim: save/load (not region file format — our own bin dump)

### 1.2 Full 1.7.10 block list (~175)

Natural terrain:
- [ ] stone, cobblestone, mossy cobblestone
- [ ] dirt, grass block, podzol, mycelium, farmland (dry + hydrated)
- [ ] gravel, sand, red sand, sandstone (plain / chiseled / smooth)
- [ ] bedrock
- [ ] snow layer (1-8 levels), snow block, ice, packed ice
- [ ] clay, hardened clay, stained clay (×16 colors)
- [ ] netherrack, soul sand, glowstone
- [ ] end stone, obsidian

Ores + ore blocks:
- [ ] coal / iron / gold / redstone (lit+unlit) / diamond / emerald / lapis / nether quartz ores
- [ ] coal block, iron block, gold block, diamond block, emerald block, lapis block, redstone block, quartz block (plain / chiseled / pillar)

Wood (6 types):
- [ ] oak, spruce, birch, jungle, acacia, dark oak
- [ ] logs (each, 3 axis orientations)
- [ ] planks (each)
- [ ] leaves (each, with decay flag)
- [ ] saplings (each)
- [ ] stairs per plank type + cobble + brick + stone-brick + nether-brick + sandstone + quartz
- [ ] slabs per plank type + stone + cobble + stone-brick + brick + sandstone + nether-brick + quartz
- [ ] fence (oak only in 1.7.10), nether fence
- [ ] fence gate (oak only)
- [ ] wooden door (oak only), iron door, wooden trapdoor
- [ ] bookshelf

Plants:
- [ ] tall grass, large fern, dead bush
- [ ] double tall grass, double fern
- [ ] small flowers: dandelion, poppy, blue orchid, allium, azure bluet, red/orange/white/pink tulip, oxeye daisy
- [ ] large flowers: sunflower, lilac, rose bush, peony
- [ ] wheat crop (8 stages), carrot crop (8), potato crop (8)
- [ ] pumpkin, pumpkin stem, jack-o-lantern
- [ ] melon, melon stem
- [ ] cactus, sugar cane, cocoa bean (3 stages)
- [ ] nether wart (4 stages)
- [ ] mushrooms (brown, red; small + huge block variants)
- [ ] lily pad
- [ ] vines
- [ ] cobweb

Functional:
- [ ] crafting table, furnace (lit/unlit), chest, trapped chest, ender chest
- [ ] enchanting table, anvil (3 damage states), beacon
- [ ] brewing stand, cauldron (water levels 0-3)
- [ ] hopper, dropper, dispenser
- [ ] jukebox, note block
- [ ] bed (head + foot halves)
- [ ] flower pot (with contained-plant variants)
- [ ] cake (7 eat stages)
- [ ] ladder, iron bars, glass, glass pane, stained glass (×16), stained glass pane (×16)
- [ ] wall (cobblestone + mossy cobble)

Redstone:
- [ ] redstone wire, redstone torch (lit/unlit)
- [ ] repeater (4 delays × 2 states), comparator (2 modes × 2 states)
- [ ] lever, wooden button, stone button, pressure plate (wood/stone/light-weighted/heavy-weighted)
- [ ] tripwire, tripwire hook, daylight sensor
- [ ] piston, piston head (extended), sticky piston, moving piston (block entity)
- [ ] TNT, fire, nether portal, end portal, end portal frame (empty/with eye)
- [ ] rails (normal, powered, detector, activator)

Light-emitting:
- [ ] torch, jack-o-lantern, glowstone, redstone torch, lava (flowing/still), fire, redstone ore (lit), beacon
- [ ] redstone lamp (lit/unlit)

Liquids:
- [ ] water (source + 7 flowing levels), lava (source + 3 overworld / 7 nether flowing levels)
- [ ] water/lava interaction (cobble, stone, obsidian)

Technical:
- [-] command block (cut per DESIGN — scope, not version)
- [ ] mob spawner (with contained-entity nbt)
- [ ] monster egg (silverfish-infested stone variants)

### 1.3 Block mechanics
- [ ] sim: block drop table (what block breaks into, including fortune bonuses)
- [ ] sim: tool requirements (wood pick for stone, iron pick for diamond, etc.)
- [ ] sim: mining time (hardness × tool speed × enchantment × haste × underwater × onground penalties)
- [ ] sim: block place validity (only on solid top for crops/torches/redstone/etc.)
- [ ] sim: block break particles (**cut** — visual only)
- [ ] sim: neighbor-change notifications (trigger redstone / water / falling blocks / piston updates)
- [ ] sim: random tick per chunk (16 blocks per chunk per tick at default random-tick-speed=3)
- [ ] sim: scheduled tick queue (for fluids, redstone delays)

---

## 2. Lighting

- [ ] sim: skylight propagation (floodfill, max 15, blocked by opaque)
- [ ] sim: block light propagation (floodfill from torches/glowstone/lava/fire/etc., max 15, blocked by opaque)
- [ ] sim: light level at position (max of skylight × time-of-day-factor and block light)
- [ ] sim: light update on block change (remove + re-add, bounded BFS)
- [ ] sim: light-dependent mob spawn check (hostile ≤7, passive ≥9 on grass)
- [ ] sim: crop growth light check (≥9)
- [ ] sim: fire/ice melt light check

---

## 3. Fluid dynamics

### 3.1 Water
- [ ] sim: source block (level=0) vs flowing (level=1-7, 8=falling-down source)
- [ ] sim: flow horizontally up to 7 blocks from source, prefer down
- [ ] sim: infinite source when 2 source blocks adjacent horizontally at same level
- [ ] sim: entity push force (horizontal toward downslope)
- [ ] sim: swimming (buoyancy, drag)
- [ ] sim: displaces torches/crops/redstone/flowers on flow
- [ ] sim: freezes into ice in cold biomes near light (rare)
- [ ] sim: hydrates farmland within 4 blocks
- [ ] sim: converts adjacent lava (water-above-lava → cobble / water-side-lava → stone / lava-above-water-source → obsidian / water-source-adjacent-lava-source → obsidian)

### 3.2 Lava
- [ ] sim: overworld flows 3 blocks, nether flows 7
- [ ] sim: no infinite source
- [ ] sim: ignites flammable neighbors probabilistically
- [ ] sim: sets entities on fire
- [ ] sim: slows entity motion dramatically

### 3.3 Fluid tick scheduling
- [ ] sim: water tick rate 5 game ticks, lava 30 (overworld) / 10 (nether)

---

## 4. Falling blocks

- [ ] sim: sand, gravel, anvil fall when space below and no support
- [ ] sim: become `FallingBlock` entity mid-fall; reattach on landing
- [ ] sim: anvil inflicts impact damage on landed-on entities (2 × fall height)
- [ ] sim: dragon egg teleports on interact / on redstone / when supporting block broken (**optional**)

---

## 5. Dimensions

- [ ] sim: Overworld (surface): -30M..30M horizontal, 0..256 vertical
- [ ] sim: Under (Nether): 0..128 vertical (ceiling), all netherrack, lava ocean at y=31, 8:1 coord compression on portal
- [ ] sim: Void (End): floating islands, obsidian pillars with crystals, void kill at y<0
- [ ] sim: portal mechanics (nether portal lighting, 4-sec delay, linking; end portal 12-eye fill, one-way until dragon defeated → return portal)
- [ ] sim: per-dimension tick, per-dimension entity list
- [ ] sim: dimension save paths (DIM-1, DIM1)

---

## 6. World generation

### 6.1 Terrain (overworld)
- [ ] sim: 8 noise octaves (1.7 uses 3D perlin for mainNoise + 2 others for minLimit/maxLimit; detailed in decompiled `ChunkProviderGenerate`)
- [ ] sim: 5×5×17 low-res noise lattice per chunk, trilinear interp to 16×16×128 + sampling above
- [ ] sim: sea level y=64
- [ ] sim: bedrock floor y=0 (random 0-4 thick)
- [ ] sim: surface replacement (grass/dirt/sand/gravel/red-sand/podzol per biome)
- [ ] sim: biome-specific height modulation (extreme hills, ocean, etc.)

### 6.2 Biome system
- [ ] sim: 40+ biomes in 1.7.10 ("Amplified" terrain type is 1.7):
  - Plains, sunflower plains (M), forest, flower forest (M), birch forest, birch forest hills, birch forest M, roofed forest (dark oak), roofed forest M
  - Taiga, taiga hills, cold taiga, cold taiga hills, cold taiga M, mega taiga, mega taiga hills, mega spruce taiga (M)
  - Desert, desert hills, desert M
  - Ice plains, ice plains spikes (M), ice mountains
  - Jungle, jungle hills, jungle edge, jungle M, jungle edge M
  - Mesa, mesa plateau, mesa plateau F, mesa M, mesa plateau M, mesa plateau F M
  - Swampland, swampland M
  - River, frozen river
  - Ocean, deep ocean, frozen ocean
  - Mushroom island, mushroom island shore
  - Extreme hills, extreme hills edge, extreme hills+, extreme hills M, extreme hills+ M
  - Savanna, savanna plateau, savanna M, savanna plateau M
  - Beach, cold beach, stone beach
  - Hell (nether), sky (end)
- [ ] sim: biome climate (temperature, rainfall) → grass/leaves/water tint, rain vs snow decision, freeze check
- [ ] sim: biome assignment: layered cellular-automaton like in 1.7 `GenLayer` (Island, Zoom, AddIsland, AddSnow, DeepOcean, EdgeBiome, Hills, Shore, Smooth, Voronoi)
- [ ] sim: biome-specific surface decoration rules

### 6.3 Structures & features
- [ ] sim: caves (perlin worms, variable radius)
- [ ] sim: ravines (larger rarer perlin worms)
- [ ] sim: ore distributions per type (diamond 1-16, iron 1-64, coal 1-128, gold 1-32, redstone 1-16, lapis 1-30 with gaussian center, emerald 4-32 only in extreme hills, quartz 10-118 in nether)
- [ ] sim: lava lakes (surface + underground), water lakes
- [ ] sim: dungeons (4×4 mossy-cobble rooms, spawner center, 1-2 chests, rare)
- [ ] sim: villages (plains / desert / savanna / taiga variants with paths, houses, farms, wells, blacksmith with chest loot)
- [ ] sim: strongholds (3 per world, 1408-2688 block radius, library + portal room with 12 end-portal frames, rare eyes pre-filled)
- [ ] sim: mineshafts (branching corridors, rails, chest carts, spawners, cave spider spiderweb rooms)
- [ ] sim: desert well
- [ ] sim: desert pyramid (chest + TNT trap)
- [ ] sim: jungle temple (3-lever puzzle + chests + tripwire trap)
- [ ] sim: witch hut (swamp, no loot)
- [ ] sim: nether fortress (bridges, blaze spawners, wither skeleton spawners, nether wart gardens, chest loot)
- [ ] sim: end main island (fixed radius disc), obsidian pillars (10), end crystals on each

### 6.4 Decoration (per-chunk populate)
- [ ] sim: trees (small oak, large oak, spruce x2, birch, tall birch, jungle small, jungle giant with vines, acacia, dark oak 2×2, swamp oak with water)
- [ ] sim: tall grass, ferns, double tall grass/ferns
- [ ] sim: flowers (biome-weighted)
- [ ] sim: sugarcane near water
- [ ] sim: cacti on sand
- [ ] sim: dead bushes on sand
- [ ] sim: pumpkins in grass biomes
- [ ] sim: snow layers on exposed grass in cold biomes
- [ ] sim: vines on trees in jungle, on walls in swamp roofed forest
- [ ] sim: lily pads on swamp water
- [ ] sim: mushrooms in dark / on mycelium
- [ ] sim: nether: glowstone clusters on ceilings, lava springs, fire patches, soul sand patches

---

## 7. Entities (mobs, projectiles, dynamic)

### 7.1 Mob roster (1.7.10)

Passive:
- [ ] pig, cow, mooshroom, sheep (16 wool colors, shear → wool, regrow on grass eat), chicken (lays eggs every 5-10 min), ocelot (tame with raw fish → cat, 3 color variants), wolf (tame with bone → sit/aggro toggle), horse (breeds, 7 base colors × 5 markings, tame via repeated mount, saddle/armor slots), donkey (chest slot), mule (crossbreed), bat (night/cave), squid (water, 16-blocks-deep)
- [ ] villager (5 pro in 1.7.10: farmer brown, librarian white, priest purple, smith black apron, butcher white apron; trade tiers)

Hostile:
- [ ] zombie (burns in sun, picks up armor, zombie villager variant, baby zombie, aggro nearest player)
- [ ] skeleton (burns in sun, shoots bow, wither skeleton variant in nether fortress — melee sword, wither effect)
- [ ] creeper (silent approach, fuse on proximity, cancels if target escapes, charged variant from lightning)
- [ ] spider (climbs walls, neutral in light, hostile in dark, poison on hard), cave spider (mineshaft spawner, poison always)
- [ ] enderman (3 tall, teleports, aggro on look-at-head from 64 blocks, hurt by water/rain, picks up blocks)
- [ ] silverfish (hide in monster-egg blocks, swarm on damage)
- [ ] slime (swamp + superflat, 3 sizes, splits on death)
- [ ] magma cube (nether, jumps, 3 sizes, splits)
- [ ] blaze (nether fortress, flies, shoots 3-fireball burst)
- [ ] ghast (nether open areas, flies, shoots large fireball, reflectable)
- [ ] zombie pigman (nether baseline + overworld portal spawn, neutral, aggros as group on attack, sword-wielding)
- [ ] witch (swamp hut + rare, drinks buff potions, throws splash potions)

Bosses:
- [ ] ender dragon (end, flies around pillars, body-contact damage, heals from intact end crystals, takes damage only to head)
- [ ] wither (summoned by 4-soul-sand T + 3-wither-skull, flies, shoots 3 wither skulls, wither II debuff, blue skull breaks blocks, explosion on spawn)

Utility/tame:
- [ ] iron golem (village-spawned on villager/house threshold, attacks hostile mobs, neutral to player)
- [ ] snow golem (summoned 2-snow + pumpkin head, shoots snowballs, leaves snow trail, melts in hot biomes)

### 7.2 Entity physics
- [x] oracle: `pmove` action routes player displacement through `Entity.moveEntity` so the vanilla AABB sweep runs
- [ ] sim: AABB collision with world (axis-sweep)
- [ ] sim: gravity 0.08 blocks/tick², terminal vy ~-3.92
- [ ] sim: drag 0.02 air, 0.09 ground, 0.005 water sideways, 0.08 water-y
- [ ] sim: step-up 0.5 block when walking
- [ ] sim: fall damage (3+ blocks = height-3 dmg, reduced by feather falling)
- [ ] sim: knockback (horizontal push + vy=0.4, reduced by knockback resistance)
- [ ] sim: ladder/vine climbing (holds vy, climbs on forward input)
- [ ] sim: swimming (hold space in water rises; sinking otherwise)
- [ ] sim: invulnerability frames (10 ticks between hits)
- [ ] sim: burning (ticks down, 1 dmg per 20 ticks, doused by water/rain)
- [ ] sim: drowning (air=300, decrements underwater w/o respiration, then 1 dmg per 20 ticks at 0)
- [ ] sim: suffocation (head in solid block, 1 dmg per tick)
- [ ] sim: void damage (y<0: 4 dmg per half-second)

### 7.3 Mob AI
- [ ] sim: pathfinding (A* on voxel walkable tiles with step-up/swim cost, max ~24 nodes)
- [ ] sim: aggression targeting (find nearest player within follow-range)
- [ ] sim: revenge targeting (on damage)
- [ ] sim: look-at (rotate head toward target)
- [ ] sim: wander (random destination every N ticks when idle)
- [ ] sim: panic (passive mobs sprint away N ticks after damage)
- [ ] sim: avoid sun (zombie/skeleton seek shade during day)
- [ ] sim: avoid water (most land mobs)
- [ ] sim: breed (two same-type adults with food → heart particles → baby)
- [ ] sim: follow parent (baby follows adult within range)
- [ ] sim: tame (wolf bone, ocelot raw fish, horse repeated mount)
- [ ] sim: sit/stand (tamed pets)
- [ ] sim: attack melee (step into range, swing, damage on contact frame)
- [ ] sim: attack ranged (line up, fire arrow / fireball / snowball / wither skull)
- [ ] sim: teleport (enderman on water/arrow/chance)
- [ ] sim: pick up items (zombies/skeletons equip better armor, hold dropped swords)
- [ ] sim: villager profession behaviors (farmer harvests, librarian idles, etc. — cosmetic mostly)
- [ ] sim: trade (villager tiers, gem for emerald etc.)
- [ ] sim: explosion (creeper proximity fuse, TNT timer)
- [ ] sim: split (slime/magma cube die → n smaller)

### 7.4 Mob spawning
- [ ] sim: spawn attempt per player per tick on 15×15×9 chunks with spacing
- [ ] sim: per-dimension hostile/passive/ambient/water caps
- [ ] sim: biome-specific creature lists + weights
- [ ] sim: light level check (hostile ≤7 ambient, passive ≥9 on valid grass)
- [ ] sim: spawner blocks (dungeon, mineshaft, stronghold, fortress, monster egg — fixed mob type, fires on proximity)
- [ ] sim: structure-tied spawns (witch in hut, blaze in fortress)
- [ ] sim: passive animals initial chunk-gen spawn pass
- [ ] sim: despawn (hostile: instant at 128+ blocks from any player, chance-despawn 32-128; persistent flag on name-tagged / equipment-picked-up / spawn-egg'd)

### 7.5 Dynamic / projectile entities
- [ ] arrow (parabolic, sticks in block for 60s, damage = base × velocity², lava/water put out fire, enchants scale)
- [ ] snowball (on hit → 0 dmg to most, 3 dmg to blaze/dragon, despawn)
- [ ] egg (on hit chance to spawn chick)
- [ ] thrown ender pearl (teleports thrower, 5 dmg self)
- [ ] thrown eye of ender (flies toward nearest stronghold, lands + chance-break)
- [ ] splash potion (aoe on impact, applies effect)
- [ ] experience bottle (shatters → xp orbs)
- [ ] fireball (large, ghast, reflectable with punch; small, blaze, pierces once)
- [ ] wither skull (black normal or blue charged, breaks blocks)
- [ ] fishing bobber (physics in water, random bite timer, pulls entity on reel)
- [ ] firework rocket (rises with optional star payload; particles are viz-only)
- [ ] primed TNT (4 sec fuse, explosion, drops variant on gravel blowout)
- [ ] falling block (sand/gravel/anvil, see §4)
- [ ] item entity (drop, 5-min despawn, can be picked up after 10 tick cooldown, merges with nearby stack)
- [ ] experience orb (seeks player within 8 blocks, despawns 5 min)
- [ ] lightning bolt (damage + fire + charged-creeper/zombie-pigman/witch conversion + item smelting)
- [ ] minecart (normal / chest / furnace / tnt / hopper / spawner; command-block cart cut), rail-snap physics, powered rail accelerate, detector rail redstone, activator rail effects

---

## 8. Tile entities / containers

- [ ] chest (27 slots, double-combine when adjacent same-type)
- [ ] trapped chest (27 slots, emits redstone equal to open-viewers count)
- [ ] ender chest (shared 27-slot per-player inventory)
- [ ] furnace (3 slots: input/fuel/output; 200 tick smelt; 12800 tick coal burn etc.; exp stored on take)
- [ ] brewing stand (4 slots: ingredient + 3 potion slots; 400 tick brew; see §10.3)
- [ ] dispenser (9 slots; redstone → fire item with projectile or place effect — arrows shot, boats placed, buckets filled/emptied, tnt primed, fire lit, armor equipped on player)
- [ ] dropper (9 slots; ejects item without projectile semantics)
- [ ] hopper (5 slots; pulls from above container, pushes into pointed container, 8 tick transfer, locked on redstone power)
- [ ] jukebox (plays music disc — audio cut, keep "has disc" state for redstone/comparator)
- [ ] note block (pitch by right-click count, instrument by block below — audio cut, keep pitch state)
- [ ] beacon (pyramid check 1-4 levels of iron/gold/diamond/emerald; 1 or 2 selected effects; range scales)
- [ ] enchantment table (opens gui; see §10.4)
- [ ] sign (4 lines text; keep as data, text rendering optional)
- [ ] skull (5 types: skeleton, wither skeleton, zombie, player, creeper — wither summon trigger)
- [-] command block (scope cut)
- [ ] mob spawner (contained entity NBT, nearby-player activation, spawn delay, 4 attempts per activation)
- [ ] piston head / moving piston (block entity during animation)
- [ ] end portal (tile entity on activation)
- [ ] flower pot (contained plant id)
- [ ] daylight sensor (block entity for power calc)
- [ ] comparator (block entity for state)

---

## 9. Player

### 9.1 Inventory
- [ ] 36 main slots (9×4 with 9 hotbar)
- [ ] 4 armor slots (helmet, chest, legs, boots)
- [ ] 2×2 inventory crafting grid
- [ ] cursor-held slot
- [ ] selected hotbar index
- [ ] item stack count limits (64 default, 16 ender pearl/snowball/egg/bucket, 1 tool/armor/sword/bow)
- [ ] item damage (tools/armor), break at max

### 9.2 Stats
- [ ] health 0-20 (half-hearts 0-20)
- [ ] hunger 0-20, saturation hidden
- [ ] food depletes via sprinting, jumping, taking damage, healing
- [ ] natural regen when hunger ≥18 (1 HP per 80 ticks approx)
- [ ] starvation damage when hunger=0 (1 dmg per 80 ticks, stops at 1/10 HP on easy)
- [ ] xp level + xp bar partial
- [ ] air 0-300
- [ ] armor 0-20, damage reduction formula `dmg * (1 - armor/25)` (1.7.10 formula)
- [ ] status effects (potions, see §10.3)

### 9.3 Player actions (env action space)
- [x] oracle: `pmove` (relative dx/dy/dz via `moveEntity`)
- [x] oracle: `plook` (yaw + pitch + head yaw)
- [x] oracle: `pjump`, `psneak 0|1`, `psprint 0|1`
- [x] oracle: `pattack entId` (resolves via `loadedEntityList` scan, calls `attackTargetEntityWithCurrentItem`)
- [x] oracle: `puse` (right-click held item via `Item.onItemRightClick`)
- [x] oracle: `pplace x y z face item meta` (`ItemInWorldManager.activateBlockOrUseItem` with fallback direct-setBlock)
- [x] oracle: `pbreak x y z` (routes through `ItemInWorldManager.tryHarvestBlock`)
- [x] oracle: `pselect 0-8` (hotbar slot)
- [x] oracle: `pgive itemId count [meta]` (test-setup admin add)
- [ ] pick block (creative only — skip)
- [ ] drop item (Q single / stack)
- [ ] inventory open / craft / move stacks
- [-] chat (scope cut)
- [-] creative fly (scope cut — survival only)
- [ ] sleep in bed

### 9.4 Game modes
- [ ] survival (default)
- [-] creative, adventure, hardcore (scope cut; can re-enable as env kwargs later)

---

## 10. Recipes, transformations, progression

### 10.1 Crafting
- [ ] recipe registry (shaped + shapeless)
- [ ] all vanilla 1.7.10 recipes (~200+): tools, armor, blocks, food, utility, dye combos
- [ ] dye + wool, dye + glass (stained glass), dye + hardened clay (stained clay)
- [ ] tool+tool repair in grid (loses enchants)
- [ ] map cloning / extending
- [ ] firework star + rocket assembly
- [ ] book + quill, written book cloning

### 10.2 Smelting
- [ ] smelting recipe list (ore → ingot, sand → glass, cobble → stone, log → charcoal, food cooking, cactus → green dye, clay ball → brick, netherrack → nether brick item, etc.)
- [ ] fuel registry (coal 1600 tk, charcoal 1600, log 300, planks 300, stick 100, bucket of lava 20000, blaze rod 2400, wooden tools 200, sapling 100, etc.)
- [ ] xp reward on take

### 10.3 Brewing
- [ ] base bottle: water → awkward (nether wart) → primary (mundane, thick, potent — or specific effect ingredient)
- [ ] effect recipes:
  - regeneration (ghast tear)
  - swiftness (sugar)
  - fire resistance (magma cream)
  - poison (spider eye)
  - healing (glistering melon)
  - night vision (golden carrot)
  - weakness (fermented spider eye → water)
  - strength (blaze powder)
  - slowness (fermented spider eye + swiftness/leaping)
  - invisibility (fermented spider eye + night vision)
  - 1.7.10 has no leaping (no rabbit foot) and no water-breathing (no pufferfish brewing ingredient)
- [ ] modifiers:
  - redstone → extended duration (×2.67 typically)
  - glowstone → tier II (double potency, halved duration)
  - fermented spider eye → invert (healing→harming, swiftness→slowness, night vision→invisibility)
  - gunpowder → splash variant
- [ ] potion effect registry + active effect list on entities (duration tick-down, effect application per tick)
- [ ] splash potion AoE (4-block radius, falloff by distance)

### 10.4 Enchanting
- [ ] enchantment registry:
  - sword: sharpness, smite, bane of arthropods, knockback, fire aspect, looting, unbreaking
  - pickaxe/axe/shovel: efficiency, silk touch, fortune, unbreaking
  - armor: protection, fire protection, blast protection, projectile protection, feather falling, respiration, aqua affinity, thorns, unbreaking
  - bow: power, punch, flame, infinity, unbreaking
  - fishing rod: luck of the sea, lure, unbreaking
- [ ] enchantment level caps (I-V typically, varies per enchant)
- [ ] enchant incompatibility (sharpness excludes smite/bane; protection/fire-prot/blast-prot/projectile-prot mutually exclusive; silk touch excludes fortune/looting)
- [ ] enchanting table UI: 3 level slots (1-8, 10-20, 25-30 ish), displays pseudo-random enchant based on (player seed × book count × item type). Note: 1.7.10 enchanting costs *levels only*, no lapis.
- [ ] actual enchant roll on apply (uses player enchant seed; displayed enchant is a preview, actual result may differ)
- [ ] bookshelf count around table (up to 15, +1 max level per ~2 shelves)
- [ ] enchanted books
- [ ] anvil combine (item+item, item+book, rename; prior-work penalty doubles cost per combine; cost caps at 40; repair with material also supported)

### 10.5 XP
- [ ] orb entity (see §7.5), absorbed by player within 8 blocks
- [ ] level cost/reward formulas: level n → n+1 needs (2n+7) xp for levels 0-16, (5n-38) for 17-31, (9n-158) for 32+
- [ ] drops: mob kill (varies per mob), ore break (coal 0-2, diamond/emerald 3-7, lapis/redstone 1-5, quartz 2-5), breeding (1-7), furnace take (varies), bottle o' enchanting (3-11)

### 10.6 Achievements
- [-] cut per DESIGN. Replace with RL task hooks that emit on same triggers (place crafting table, pickup wood, smelt iron, etc.) for the env's Log struct.

---

## 11. Weather & day cycle

- [ ] day/night cycle: 24000 tick day, sunrise at 0, noon 6000, sunset 12000, midnight 18000
- [ ] moon phase (8 phases, one per day, affects slime spawns on surface)
- [ ] weather cycle: clear → rain → thunder → clear, randomized intervals via `WorldInfo.rainTime/thunderTime` (pinned in §0)
- [ ] rain effects: puts out fire blocks, fills cauldrons (1 level per ~1 min), extinguishes burning entities, mob pathing unaffected, does not rain in desert/savanna/mesa/nether/end/cold biomes (snows in cold)
- [ ] snow accumulation on exposed grass in cold biomes during weather tick
- [ ] ice formation on water surface in cold biomes (light-gated)
- [ ] thunder: lightning strike every ~100000 ticks per loaded chunk during thunderstorm; struck pig → pigman, struck creeper → charged, struck villager → witch, struck near-entity = 5 dmg + fire
- [ ] time commands / sleep-in-bed skip (all players asleep → jumps to morning if no hostile mobs within 8 blocks)

---

## 12. Redstone & mechanical

- [ ] wire: propagates power 0-15, -1 per block traveled, up/down one block via "hill" rule, T/corner rendering rules (viz)
- [ ] redstone torch: inverter, burnout protection if flipped >7 times/game tick, provides 15 straight up + through 1 block up
- [ ] repeater: 1-4 tick delay, one-directional, locks when side powered
- [ ] comparator: subtraction mode (front = back - max(side)) or compare (front = back if back ≥ sides else 0); reads containers for fullness, reads cake/cauldron/jukebox/end-portal-frame for special values
- [ ] lever, button (wooden=30t, stone=15t pulse), pressure plate (wood = any entity incl item, stone = living only, weighted gold = 1-15 based on stack count of items 1-64, weighted iron = 1-15 based on 1-640)
- [ ] tripwire + hook (string between two hooks, triggered by entity)
- [ ] daylight sensor (output = sky light level 0-15)
- [ ] piston: extends when powered, retracts when unpowered; pushes up to 12 blocks; sticky pulls 1 block on retract; cannot push obsidian/bedrock/container-tile-entities/spawners/portal-frames; air-gated push-line validation; 2-tick extend animation
- [ ] redstone lamp (lights when powered)
- [ ] hopper (§8 — locked on redstone)
- [ ] dispenser/dropper (see §8)
- [ ] powered rail (extends boost from adjacent charge), detector rail (emits on cart-on), activator rail (powers minecart effects)
- [ ] TNT (instant ignite on redstone, 80 tick fuse, 4-block blast, drops 30% of broken blocks as items)

---

## 13. Sub-mechanics

- [ ] fire spread (catches neighbors with flammability weight, burns out or destroys)
- [ ] leaf decay (scheduled check of "connected to log within 4 blocks"; if not → drop sapling/apple/nothing)
- [ ] grass/mycelium/podzol spread to adjacent dirt under light
- [ ] vines grow downward + sideways in shade
- [ ] cocoa bean stages on jungle log
- [ ] mushroom bonemeal → giant mushroom
- [ ] sapling bonemeal → tree (biome-specific tree type)
- [ ] crop bonemeal → advance 2-5 stages
- [ ] farmland hydration
- [ ] fence/wall/pane connection auto-join (viz + collision)
- [ ] redstone ore lights when walked on / punched for 30 ticks

---

## 14. Oracle instrumentation (Java side)

- [x] `oracle/OracleLog.java` — JSONL writer with env-var toggle
- [x] `World.java` — deterministic seed, setBlock hook
- [x] `Entity.java` — deterministic rand/UUID
- [x] `EntityItem.java` — deterministic motion
- [x] `Item.java` — pinned itemRand
- [x] `MinecraftServer.java` — tick cap + tick_begin/tick_end + entity sweep
- [x] `RegionFile.java` — zero timestamps
- [ ] `NetHandlerPlayServer.java` — stdin-driven action injection (bypass network)
- [ ] `EntityPlayerMP.java` — pinned session timestamp
- [ ] `ContainerEnchantment.java` / `EnchantmentHelper.java` — pinned RNG for display + roll
- [ ] `TileEntityDispenser` / `BlockDispenser` / `BlockFurnace` / `BlockBrewingStand` / `BlockChest` / `BlockHopper` — pinned container RNG
- [x] headless-player spawn hook (so tests can have a player without real client) — `OracleHeadlessPlayer.init`, stub `OracleNetworkManager`/`OracleNetHandler`, env gate `MC_ORACLE_HEADLESS_PLAYER=1` or any `p*` op in actions file
- [x] inventory/armor logging (per-tick `op:"player"` line: pos/yaw/pit/hp/hu/xp/slot + 64-bit FNV hash of 36 main + 4 armor + cursor)
- [ ] status-effect logging
- [ ] scheduled-tick logging (so we can diff pending-queue state, not just applied)
- [ ] light-level logging (optional, expensive — gate on env var)

---

## 15. Harness

- [x] per-tick JSONL diff (`diff_runs.py`)
- [ ] binary wire format (protobuf/flatbuffers)
- [ ] streaming differ (don't load both logs fully into memory)
- [ ] snapshot/restore: serialize oracle world state at any tick, reload it into a fresh run
- [ ] action-sequence file format (tick → action payload)
- [ ] Python test runner: parametrize over `(seed, actions[], expected_deltas_hash)` fixtures
- [ ] C-sim comparator: load oracle JSONL, step C sim, per-tick assert
- [ ] tolerance tiers in comparator:
  - bitwise: block id/meta, inventory counts, discrete tile coords, tick counter
  - atol/rtol: entity positions, velocities, health (float)
  - distributional: mob AI decisions, loot tables (run 10k, KS test)
- [ ] CI job: run fixture suite on every commit to `PufferLib/ocean/voxel/`

---

## 16. C sim (clean-room, PufferLib/ocean/voxel/)

Not started. Ordered rough build sequence:
- [ ] 1. `voxel.h` skeleton (state struct, c_init/reset/step/close, Log struct)
- [ ] 2. Java-compatible LCG Random port (next/nextInt/nextLong/nextFloat/nextDouble/nextGaussian/nextBytes)
- [ ] 3. Chunk + block store (palette, meta, light)
- [ ] 4. Player physics + AABB sweep
- [ ] 5. Block place/break with redstone / water / falling callbacks
- [ ] 6. World gen: biome layer, terrain noise, surface replacement
- [ ] 7. Light engine
- [ ] 8. Fluid (water first, lava second)
- [ ] 9. Random tick + scheduled tick queues
- [ ] 10. Mob spawn + passive/hostile cap
- [ ] 11. Mob AI (pathfind, aggro, attack)
- [ ] 12. Combat + damage + armor
- [ ] 13. Inventory + crafting + smelting
- [ ] 14. Redstone (wire, torch, repeater, comparator)
- [ ] 15. Piston
- [ ] 16. Structures (caves, dungeons, villages, strongholds)
- [ ] 17. Nether + End dimensions
- [ ] 18. Brewing + enchanting + anvil + XP
- [ ] 19. Containers (chest, furnace, hopper, dispenser)
- [ ] 20. Beacon + ender chest
- [ ] 21. Ender dragon + wither bosses
- [ ] 22. Weather + day/night

---

## 17. Viz (raylib, PufferLib/ocean/voxel/)

**Scope clarification**: viz is only for `puffer eval` policy replay — watching
a trained agent play. It does not run during training (`c_render` is never
called). The *only* requirement is "can a human verify the policy is doing
sensible things." 3D rendering, textures, and HUD polish are out of scope.

Minimum viable viewer (top-down 2D slice):
- [ ] raylib window, lazy-init in `c_render` on first call (pattern from `ocean/snake/snake.h:314-322`)
- [ ] 2D top-down draw of a 32×32 or 48×48 tile window around the player
- [ ] per-block-id color palette (flat `DrawRectangle`, no textures needed)
- [ ] player marker (arrow indicating facing)
- [ ] mob markers (colored dot + glyph per mob class)
- [ ] item-entity markers
- [ ] hotbar strip (9 slots, selected highlight, text count)
- [ ] status bars (HP, hunger, XP)
- [ ] action overlay (last action taken, e.g. "break x,y,z" or "craft wood_pick")
- [ ] y-level indicator + PageUp/PageDown to slice up/down through the world

Optional follow-ups (only if the 2D view proves insufficient for debugging):
- [ ] slice with transparency showing blocks ±2 y-levels
- [ ] mini-inventory popup on keypress
- [ ] crafting-state readout
- [ ] path trail (last N positions)

Hard non-goals:
- [-] 3D voxel renderer / chunk mesher
- [-] textures (for any block or entity) — flat colors only
- [-] sky, day-night lighting, weather visuals, particles
- [-] first-person view, hand animation, camera controls
- [-] full HUD, inventory screen, container screens
- [-] audio (no music, no sounds — audio events go into training signal only)

Rationale: training throughput is the main constraint. The viewer exists to
sanity-check "does the policy do what we intended," which a 2D slice answers
as well as a full 3D render and ships in ~200-400 LOC instead of ~5000.

---

## Progress log

- 2026-04-18: workspace set up, MCP 9.08 decompiled 1.7.10 server → 971 .java files, recompiled clean, server boots + saves in 1.8s.
- 2026-04-18: `§0` seed/RNG pins applied; `§14` OracleLog + tick hooks in place; `§15` JSONL diff green for seed=12345, 400 ticks, 1.09M log lines, 0 diverging ticks.
- 2026-04-18: `§14` headless-player path landed: `OracleHeadlessPlayer` + stub `OracleNetworkManager`/`OracleNetHandler` spawn an `EntityPlayerMP` at world-load with deterministic UUID, no real network. `OracleActions` now dispatches p-ops: `pmove/plook/pjump/psneak/psprint/pattack/puse/pplace/pbreak/pselect/pgive`. Per-tick `op:"player"` log line with FNV-hashed inventory. `test_headless_player.sh` runs the 200-tick fixture twice: 0 diverging ticks. 150-tick world-op regression still green.


# Doc 31: `mc-oracle/cleanroom/random/README.md` {#doc-31}

*Absolute path: `/home/infatoshi/games/minecraft/mc-oracle/cleanroom/random/README.md`*

# jrandom: clean-room C99 port of java.util.Random

Bit-compatible with `java.util.Random` (Java 8 reference). First building
block of a clean-room voxel engine targeting Minecraft 1.7.10 parity.

## Run

```
make test
```

Compiles `JRandomFixture.java` with Java 8, runs it to produce `fixture.txt`
(1000 samples per method, seed 42), compiles `test_jrandom.c` + `jrandom.c`
with `gcc -std=c99 -O2 -Wall -Wextra -Werror`, runs it to produce
`c_out.txt`, then diffs. Exit 0 means PASS.

## Acceptance envelope

Bitwise identity on every method: `nextInt`, `nextInt(100)`, `nextInt(256)`
(pow-2 fast path), `nextInt(7)` (rejection loop), `nextLong`, `nextBoolean`,
`nextBytes` (4096-byte block), `nextFloat`, `nextDouble`, `nextGaussian`.

Floats/doubles/gaussians are emitted as raw IEEE 754 bit patterns (hex) on
both sides so formatting never enters the picture.

## Gaussian: fdlibm log port

`Random.nextGaussian` uses `StrictMath.log` / `StrictMath.sqrt` (fdlibm).
On x86_64 glibc, hardware `sqrt` is IEEE-754 correctly-rounded and bitwise
matches StrictMath (verified over 10k seeded inputs). `log`, however, drifts
by up to 1 ULP on roughly 7% of inputs, so we ship a clean port of fdlibm's
`__ieee754_log` as `fdlibm_log.c` (source: netlib `e_log.c`, SHA256
`a2b1bd1b367026a35f36da53425cd618386c3bf69d23ac224a40791aa9855ac9`; Sun
1993 permissive notice preserved verbatim at the top of the file). The
polynomial, constants, and control flow are byte-for-byte equivalent to
the original; only the `__HI`/`__LO` endian macros are replaced with
`memcpy`-based helpers (portable, no aliasing UB).

`jrandom_next_gaussian` calls `fdlibm_log` and hardware `sqrt`, producing
bitwise-identical output to stock `new Random(seed).nextGaussian()` — the
fixture now calls real `Random.nextGaussian` directly. `make test` verifies
1000 samples per method plus a 100k-sample FNV-1a hash of Gaussian bit
patterns (section `=== nextGaussian_hash_100k ===`) to catch rare-input drift.


# Doc 32: `netherite-macbook/netherite/AGENTS.md` {#doc-32}

*Absolute path: `/home/infatoshi/games/minecraft/netherite-macbook/netherite/AGENTS.md`*

# Netherite v2

Use this file as the current project handoff for coding agents. It is intentionally high signal and current. Read `SPEC.md` for the full design.

## Non-negotiables
- Use `uv` for all Python commands. No bare `python` or `pip`.
- Java 21 is required.
- On macOS, use `JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home`.
- On anvil, use `JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64`.
- Python verification standard in this repo is `uv run ruff check . --fix` and `uv run pytest tests/`.
- Fabric multi-instance launches must use isolated `--gameDir` roots under `run/instances/<id>`. Sharing a game dir corrupts Fabric's `.fabric/processedMods` cache.

## Machines
- `local/macbook`: MacBook Pro M4 Max, 36 GB, Metal, macOS 26.3.
- `anvil`: Ryzen 9 9950X3D, 92 GB DDR5, RTX 3090 24 GB, Ubuntu 24.04.
- `anvil` display for headless GL is `DISPLAY=:2`.
- SSH to anvil with `ssh anvil`.

## Build, Run, Test
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew build
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient
uv run ruff check . --fix
uv run pytest tests/
```

## Current Truth
- MC version is `1.20.1`, Fabric only.
- Rendering is Minecraft + Sodium. Do not add custom renderers.
- Sodium and Lithium are runtime-loaded from `run/mods/`.
- Sodium's LWJGL version check is bypassed in `build.gradle` with `-Dsodium.checks.issue2561=false`.
- Shared memory paths are `/tmp/netherite_*` on macOS and `/dev/shm/netherite_*` on Linux.
- `bench_scaling.py` now supports `--width` and `--height`.
- `grid_demo.py` now defaults to `320x180` capture for human-facing demos.

## Render Resolution Diagnosis
- The project does natively render fewer pixels. This is not a Matplotlib crop.
- The current low-res path works by overriding `Window.getFramebufferWidth()` and `getFramebufferHeight()` in `src/main/java/com/netherite/mod/mixin/WindowMixin.java`.
- `FrameGrabber` then reads that full low-res framebuffer with `glReadPixels`.
- `FramebufferMixin` then blits that full low-res framebuffer to the real window with `glBlitFramebuffer`.
- That means `160x90` is a true native render size today.
- The problem is that the whole Minecraft client framebuffer is shrunk to `160x90`, not just the agent observation.
- Vanilla HUD elements do not fit cleanly into `160x90`. Hearts and hotbar clip. This is why the human-facing demo looked cropped.
- `320x180` is currently the minimum sane native resolution for human-facing display. The HUD fits and the full frame is preserved.
- Proper long-term fix: decouple human display resolution from agent capture resolution. Keep UI/display at `320x180` or higher, and make agent capture a separate lower-res path.

## Human Demo Artifacts
- Native `320x180` single tile: `recordings/320_180_native.png`
- Native `1920x1080` single tile: `recordings/1920_1080_native.png`
- `160x90` vs `320x180` HUD comparison: `recordings/160_vs_320_comparison.png`
- `1920x1080` vs `320x180` native comparison: `recordings/1080p_vs_180p_comparison.png`
- `B=8` grid demo at `320x180`: `recordings/anvil_b8_batched_demo_320x180.mp4`

## Recent Stability Fixes That Matter
- `PosixSemaphore.java` had a Linux `O_CREAT` issue. The Linux flag is now correct, and `sem_post` is guarded against invalid handles.
- `BakedModelManagerMixin.java` guards `BakedModelManager.shouldRerender` when `stateLookup == null`. This removed a startup crash path at scale.
- The Python start latch no longer keys on `frame_hash`. It now ignores pure frame jitter and latches on stable pose/chunk/seed state instead. This was necessary for multi-instance stability.
- Startup tracing was added in `env/startup_trace.py` and integrated into launcher/benchmark paths for bring-up debugging.

## Multi-Instance State
- `B=8` is now stable enough to benchmark on anvil.
- The main remaining architectural issue is not startup corruption. It is the display/capture coupling described above.

## Known Good Benchmark Settings
- Render distance `4`
- Simulation distance `5`
- `max_fps=9999`
- `uncapped=true`
- `use_semaphore=true`

## Latest Benchmarks

### Local MacBook, single instance, render enabled
Settings: headless, `RD=4`, `SD=5`, `max_fps=9999`, semaphore on, `300` measured steps after `75` warmup.

| Resolution | `step_sync` | `step` | `state_only` | `tick_only` |
|---|---:|---:|---:|---:|
| `160x90` | `217.4` | `666.4` | `741.2` | `730.7` |
| `320x180` | `192.7` | `592.6` | `753.8` | `617.6` |

Interpretation:
- `320x180` costs about `11%` on the render-coupled step paths on the MacBook.

### Anvil, apples-to-apples `1` and `8` env sweep, `160x90`
Command source is `env/bench_scaling.py` with `--envs 1,8 --strategies sync,batched,async --steps 100 --warmup 10 --width 160 --height 90 --render-distance 4 --simulation-distance 5 --max-fps 9999 --use-semaphore`.

| Envs | `sync` | `batched` | `async` |
|---|---:|---:|---:|
| `1` | `216.0` | `359.7` | `329.8` |
| `8` | `135.4` | `490.9` | `638.8` |

Raw log: `recordings/anvil_bench_160x90_apples.log`

### Anvil, apples-to-apples `1` and `8` env sweep, `320x180`
Command source is `env/bench_scaling.py` with `--envs 1,8 --strategies sync,batched,async --steps 100 --warmup 10 --width 320 --height 180 --render-distance 4 --simulation-distance 5 --max-fps 9999 --use-semaphore`.

| Envs | `sync` | `batched` | `async` |
|---|---:|---:|---:|
| `1` | `184.7` | `1020.5` | `312.7` |
| `8` | `122.9` | `439.3` | `567.6` |

Raw log: `recordings/anvil_bench_320x180.log`

### Anvil `320x180` vs `160x90` at `B=8`
- `sync`: `135.4 -> 122.9` which is `-9.2%`
- `batched`: `490.9 -> 439.3` which is `-10.5%`
- `async`: `638.8 -> 567.6` which is `-11.1%`

Interpretation:
- Treat the `B=8` penalty for `320x180` as about `10%`.
- The `1-env batched` result at `320x180` is clearly not stable enough to use as a comparison point. Do not build conclusions on that number.

## Current Best Human-Facing Resolution
- Use `320x180` for any human-facing native display or recording.
- Do not use `160x90` when you need a faithful full HUD view.

## Current Best Agent-Facing Assumption
- `160x90` is still fine for a pure agent observation if HUD fidelity is irrelevant.
- The codebase does not yet properly support `display=320x180` with `agent capture=160x90` as separate knobs. That is the next important rendering change.

## Commands That Matter

### Local human demo
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient \
  -Dnetherite.instance_id=0 -Dnetherite.seed=12345 -Dnetherite.rl=false \
  -Dnetherite.width=320 -Dnetherite.height=180 \
  -Dnetherite.uncapped=true -Dnetherite.max_fps=9999 \
  -Dnetherite.render_distance=4 -Dnetherite.simulation_distance=5 \
  -Dnetherite.graphics=fast -Dnetherite.particles=minimal -Dnetherite.clouds=off \
  -Dnetherite.smooth_lighting=false -Dnetherite.entity_shadows=false \
  -Dnetherite.biome_blend=0 -Dnetherite.vsync=false \
  --args="--width 854 --height 480 --username player"
```

### Anvil benchmark at `320x180`
```bash
ssh anvil '
  cd ~/netherite-v2-bench &&
  DISPLAY=:2 uv run env/bench_scaling.py \
    --envs 1,8 \
    --strategies sync,batched,async \
    --steps 100 --warmup 10 \
    --width 320 --height 180 \
    --java-home /usr/lib/jvm/java-21-openjdk-amd64 \
    --render-distance 4 --simulation-distance 5 --max-fps 9999 \
    --use-semaphore --env-timeout 30.0
'
```

## Do Not Waste Time On
- Do not revive the old CUDA rasterizer or Forge code.
- Do not attempt `@ModifyVariable` on `Framebuffer.draw()`. It does not hit the visible path.
- Do not share a Fabric `gameDir` across instances.
- Do not treat the current `160x90` human display issue as a Matplotlib crop bug. It is a framebuffer architecture issue.

## Best Next Steps
- Implement proper separation between display resolution and agent capture resolution.
- Keep `320x180` as the human display baseline.
- Re-run `B=16` on anvil once the display/capture split is clean.


# Doc 33: `netherite-macbook/netherite/CLAUDE.md` {#doc-33}

*Absolute path: `/home/infatoshi/games/minecraft/netherite-macbook/netherite/CLAUDE.md`*

# Netherite v2

Use this file as the Claude Code handoff. It is intentionally high signal and current. Read `SPEC.md` for the full design. Read `AGENTS.md` for the stricter workflow rules.

## Non-negotiables
- Use `uv` for all Python commands. No bare `python` or `pip`.
- Java 21 is required.
- On macOS, use `JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home`.
- On anvil, use `JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64`.
- Python verification standard in this repo is `uv run ruff check . --fix` and `uv run pytest tests/`.
- Fabric multi-instance launches must use isolated `--gameDir` roots under `run/instances/<id>`. Sharing a game dir corrupts Fabric's `.fabric/processedMods` cache.

## Machines
- `local/macbook`: MacBook Pro M4 Max, 36 GB, Metal, macOS 26.3.
- `anvil`: Ryzen 9 9950X3D, 92 GB DDR5, RTX 3090 24 GB, Ubuntu 24.04.
- `anvil` display for headless GL is `DISPLAY=:2`.
- SSH to anvil with `ssh anvil`.

## Build, Run, Test
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew build
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient
uv run ruff check . --fix
uv run pytest tests/
```

## Current Truth
- MC version is `1.20.1`, Fabric only.
- Rendering is Minecraft + Sodium. Do not add custom renderers.
- Sodium and Lithium are runtime-loaded from `run/mods/`.
- Sodium's LWJGL version check is bypassed in `build.gradle` with `-Dsodium.checks.issue2561=false`.
- Shared memory paths are `/tmp/netherite_*` on macOS and `/dev/shm/netherite_*` on Linux.
- `bench_scaling.py` now supports `--width` and `--height`.
- `grid_demo.py` now defaults to `320x180` capture for human-facing demos.

## Render Resolution Diagnosis
- The project does natively render fewer pixels. This is not a Matplotlib crop.
- The current low-res path works by overriding `Window.getFramebufferWidth()` and `getFramebufferHeight()` in `src/main/java/com/netherite/mod/mixin/WindowMixin.java`.
- `FrameGrabber` then reads that full low-res framebuffer with `glReadPixels`.
- `FramebufferMixin` then blits that full low-res framebuffer to the real window with `glBlitFramebuffer`.
- That means `160x90` is a true native render size today.
- The problem is that the whole Minecraft client framebuffer is shrunk to `160x90`, not just the agent observation.
- Vanilla HUD elements do not fit cleanly into `160x90`. Hearts and hotbar clip. This is why the human-facing demo looked cropped.
- `320x180` is currently the minimum sane native resolution for human-facing display. The HUD fits and the full frame is preserved.
- Proper long-term fix: decouple human display resolution from agent capture resolution. Keep UI/display at `320x180` or higher, and make agent capture a separate lower-res path.

## Human Demo Artifacts
- Native `320x180` single tile: `recordings/320_180_native.png`
- Native `1920x1080` single tile: `recordings/1920_1080_native.png`
- `160x90` vs `320x180` HUD comparison: `recordings/160_vs_320_comparison.png`
- `1920x1080` vs `320x180` native comparison: `recordings/1080p_vs_180p_comparison.png`
- `B=8` grid demo at `320x180`: `recordings/anvil_b8_batched_demo_320x180.mp4`

## Recent Stability Fixes That Matter
- `PosixSemaphore.java` had a Linux `O_CREAT` issue. The Linux flag is now correct, and `sem_post` is guarded against invalid handles.
- `BakedModelManagerMixin.java` guards `BakedModelManager.shouldRerender` when `stateLookup == null`. This removed a startup crash path at scale.
- The Python start latch no longer keys on `frame_hash`. It now ignores pure frame jitter and latches on stable pose/chunk/seed state instead. This was necessary for multi-instance stability.
- Startup tracing was added in `env/startup_trace.py` and integrated into launcher/benchmark paths for bring-up debugging.

## Multi-Instance State
- `B=8` is now stable enough to benchmark on anvil.
- The main remaining architectural issue is not startup corruption. It is the display/capture coupling described above.

## Known Good Benchmark Settings
- Render distance `4`
- Simulation distance `5`
- `max_fps=9999`
- `uncapped=true`
- `use_semaphore=true`

## Latest Benchmarks

### Local MacBook, single instance, render enabled
Settings: headless, `RD=4`, `SD=5`, `max_fps=9999`, semaphore on, `300` measured steps after `75` warmup.

| Resolution | `step_sync` | `step` | `state_only` | `tick_only` |
|---|---:|---:|---:|---:|
| `160x90` | `217.4` | `666.4` | `741.2` | `730.7` |
| `320x180` | `192.7` | `592.6` | `753.8` | `617.6` |

Interpretation:
- `320x180` costs about `11%` on the render-coupled step paths on the MacBook.

### Anvil, apples-to-apples `1` and `8` env sweep, `160x90`
Command source is `env/bench_scaling.py` with `--envs 1,8 --strategies sync,batched,async --steps 100 --warmup 10 --width 160 --height 90 --render-distance 4 --simulation-distance 5 --max-fps 9999 --use-semaphore`.

| Envs | `sync` | `batched` | `async` |
|---|---:|---:|---:|
| `1` | `216.0` | `359.7` | `329.8` |
| `8` | `135.4` | `490.9` | `638.8` |

Raw log: `recordings/anvil_bench_160x90_apples.log`

### Anvil, apples-to-apples `1` and `8` env sweep, `320x180`
Command source is `env/bench_scaling.py` with `--envs 1,8 --strategies sync,batched,async --steps 100 --warmup 10 --width 320 --height 180 --render-distance 4 --simulation-distance 5 --max-fps 9999 --use-semaphore`.

| Envs | `sync` | `batched` | `async` |
|---|---:|---:|---:|
| `1` | `184.7` | `1020.5` | `312.7` |
| `8` | `122.9` | `439.3` | `567.6` |

Raw log: `recordings/anvil_bench_320x180.log`

### Anvil `320x180` vs `160x90` at `B=8`
- `sync`: `135.4 -> 122.9` which is `-9.2%`
- `batched`: `490.9 -> 439.3` which is `-10.5%`
- `async`: `638.8 -> 567.6` which is `-11.1%`

Interpretation:
- Treat the `B=8` penalty for `320x180` as about `10%`.
- The `1-env batched` result at `320x180` is clearly not stable enough to use as a comparison point. Do not build conclusions on that number.

## Current Best Human-Facing Resolution
- Use `320x180` for any human-facing native display or recording.
- Do not use `160x90` when you need a faithful full HUD view.

## Current Best Agent-Facing Assumption
- `160x90` is still fine for a pure agent observation if HUD fidelity is irrelevant.
- The codebase does not yet properly support `display=320x180` with `agent capture=160x90` as separate knobs. That is the next important rendering change.

## Commands That Matter

### Local human demo
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient \
  -Dnetherite.instance_id=0 -Dnetherite.seed=12345 -Dnetherite.rl=false \
  -Dnetherite.width=320 -Dnetherite.height=180 \
  -Dnetherite.uncapped=true -Dnetherite.max_fps=9999 \
  -Dnetherite.render_distance=4 -Dnetherite.simulation_distance=5 \
  -Dnetherite.graphics=fast -Dnetherite.particles=minimal -Dnetherite.clouds=off \
  -Dnetherite.smooth_lighting=false -Dnetherite.entity_shadows=false \
  -Dnetherite.biome_blend=0 -Dnetherite.vsync=false \
  --args="--width 854 --height 480 --username player"
```

### Anvil benchmark at `320x180`
```bash
ssh anvil '
  cd ~/netherite-v2-bench &&
  DISPLAY=:2 uv run env/bench_scaling.py \
    --envs 1,8 \
    --strategies sync,batched,async \
    --steps 100 --warmup 10 \
    --width 320 --height 180 \
    --java-home /usr/lib/jvm/java-21-openjdk-amd64 \
    --render-distance 4 --simulation-distance 5 --max-fps 9999 \
    --use-semaphore --env-timeout 30.0
'
```

## Do Not Waste Time On
- Do not revive the old CUDA rasterizer or Forge code.
- Do not attempt `@ModifyVariable` on `Framebuffer.draw()`. It does not hit the visible path.
- Do not share a Fabric `gameDir` across instances.
- Do not treat the current `160x90` human display issue as a Matplotlib crop bug. It is a framebuffer architecture issue.

## Best Next Steps
- Implement proper separation between display resolution and agent capture resolution.
- Keep `320x180` as the human display baseline.
- Re-run `B=16` on anvil once the display/capture split is clean.


# Doc 34: `netherite-macbook/netherite/README.md` {#doc-34}

*Absolute path: `/home/infatoshi/games/minecraft/netherite-macbook/netherite/README.md`*

# Netherite

Python-controlled Minecraft RL environment. MC 1.20.1 + Fabric + Lithium/Sodium. Zero manual GUI interaction -- boot straight into a world with every setting configurable from Python.

## Prerequisites

- **Java 17+** (21 recommended). Check with `java -version`.
- **Python 3.11+** via [uv](https://docs.astral.sh/uv/).
- **Minecraft ownership is required.** Netherite uses Fabric Loom which downloads MC assets from Mojang's servers during build. This is permitted under Mojang's EULA for development. We do not redistribute Minecraft.

## Quick Start

```bash
# Clone
git clone <repo-url> && cd netherite

# Build (downloads MC 1.20.1 + Fabric + dependencies on first run)
JAVA_HOME=/path/to/jdk21 ./gradlew build

# Launch MC with the mod (opens a window, auto-creates world)
JAVA_HOME=/path/to/jdk21 ./gradlew runClient

# Optional: add Sodium + Lithium for performance
mkdir -p run/mods
# Download from https://modrinth.com/mod/sodium (1.20.1 Fabric)
# Download from https://modrinth.com/mod/lithium (1.20.1 Fabric)
# Drop jars into run/mods/
```

## Python Environment

```bash
cd netherite
uv venv && uv pip install numpy gymnasium pillow

# With MC running, read a frame:
uv run python -c "
import sys; sys.path.insert(0, 'env')
from config import NetheriteConfig
from netherite_env import NetheriteEnv
env = NetheriteEnv()
obs, _ = env.reset()
print(f'Frame: {obs[\"pov\"].shape}, pos: {obs[\"position\"]}')
env.close()
"
```

## Configuration

Every setting is controlled via `-Dnetherite.*` JVM flags, which map to `NetheriteConfig` fields in Python.

```python
from config import NetheriteConfig

cfg = NetheriteConfig(
    seed=42,
    render_distance=4,
    simulation_distance=4,
    graphics="fast",
    particles="minimal",
    clouds="off",
    smooth_lighting=False,
    do_mob_spawning=False,
    do_daylight_cycle=False,
    rl=True,  # auto-dismiss menus, disable pause
)

# Launch with these settings:
# ./gradlew runClient <cfg.to_gradle_args()>
```

### Key settings

| Setting | Default | Notes |
|---------|---------|-------|
| `seed` | 12345 | World seed |
| `render_distance` | 8 | Chunks. Lower = faster. |
| `simulation_distance` | 5 | Chunks. Lower = faster. |
| `graphics` | fast | fast/fancy/fabulous |
| `particles` | minimal | all/decreased/minimal |
| `clouds` | off | off/fast/fancy |
| `max_fps` | 60 | FPS cap |
| `rl` | false | True = auto-dismiss menus, suppress toasts |
| `game_mode` | survival | survival/creative/adventure/spectator |
| `do_mob_spawning` | false | Game rule |
| `do_daylight_cycle` | false | Game rule |

See `env/config.py` for the full list.

## RL Mode

Pass `-Dnetherite.rl=true` to enable training mode:
- Auto-dismisses pause menu and other screens
- Suppresses toast notifications and tutorials
- Disables pause on lost focus

Without it, MC behaves normally (ESC works, GUI is interactive).

```bash
./gradlew runClient -Dnetherite.rl=true -Dnetherite.seed=42
```

## Headless (Linux / Training)

On a Linux server without a display, use Xvfb:

```bash
Xvfb :99 -screen 0 854x480x24 &
export DISPLAY=:99

./gradlew runClient \
  -Dnetherite.rl=true \
  -Dnetherite.render_distance=4 \
  -Dnetherite.seed=42
```

Resolution is controlled by the Xvfb screen size.

## Multi-Instance

Each instance needs a unique ID and its own shmem buffers:

```bash
# Instance 0
./gradlew runClient -Dnetherite.instance_id=0 -Dnetherite.seed=100 &

# Instance 1
./gradlew runClient -Dnetherite.instance_id=1 -Dnetherite.seed=200 &
```

Or use the Python launcher:

```python
from config import NetheriteConfig
from launcher import Launcher

launcher = Launcher("/path/to/netherite")
configs = [NetheriteConfig(instance_id=i, seed=i*100, rl=True) for i in range(4)]
launcher.launch(configs)
launcher.wait_all_ready()
```

## Architecture

```
Python (netherite_env.py)
    |
    | shmem: /tmp/netherite_obs_{id}_{A,B}    (RGBA pixels, PBO double-buffered)
    | shmem: /tmp/netherite_state_{id}         (pos/health/inventory/entities)
    | shmem: /tmp/netherite_action_{id}        (movement/camera/interact)
    |
MC 1.20.1 (Java, Fabric)
    ├── Sodium         -- batched indirect rendering (optional)
    ├── Lithium        -- optimized game logic (optional)
    └── netherite mod  -- PBO readback, action injection, state export
```

## Shmem Protocol

All little-endian. Ready flag at offset 12 written last.

- **Obs** (`netherite_obs_{id}_{A,B}`): 8MB, 16B header + RGBA pixels
- **State** (`netherite_state_{id}`): 64KB, player pos/health/food/inventory/entities
- **Action** (`netherite_action_{id}`): 4KB, movement keys + camera delta

See `SPEC.md` for byte-level layout.

## Machines

- **macOS** (dev): Window opens, Sodium may not load (LWJGL compat). Lithium works.
- **Linux** (training): Xvfb + Sodium + Lithium. Sodium requires GPU with OpenGL 4.6.


# Doc 35: `netherite-macbook/netherite/SPEC.md` {#doc-35}

*Absolute path: `/home/infatoshi/games/minecraft/netherite-macbook/netherite/SPEC.md`*

# Netherite v2: Fast Minecraft RL Environment

## Goal

Fully Python-controlled Minecraft RL environment. Every game setting configurable from Python: resolution, render distance, game rules, graphics quality, JVM count, instances per JVM. Zero manual GUI interaction -- boot straight into a world. Performance depends on these knobs (resolution, render distance, graphics level, instance count) and is explored empirically, not targeted at a fixed multiplier.

Uses MC 1.20.1 + Sodium + Fabric + a tiny custom mod. No custom rendering code. MC's OpenGL renderer handles everything correctly (mobs, entities, water, particles, sky, HUD). We capture frames via PBO async readback and pipe them to Python through shared memory.

## Why Not Custom Rendering

Previous attempt: built a CUDA software rasterizer that replicated MC's OpenGL output. Achieved 93% pixel-exact match for static blocks, but hit unbounded rendering bugs: missing mobs/entities, water artifacts, x-ray vision, no particles/sky/HUD. Every fix revealed new issues. MC has hundreds of rendering features and reimplementing them all is a losing game.

The insight: MC's own renderer already handles everything. The bottleneck in MineRL isn't the GPU (it's mostly idle) -- it's the CPU-side overhead of thousands of GL draw calls per frame and synchronous frame readback. Sodium fixes the draw calls (batches everything into ~1-5 indirect draws). PBO async readback fixes the stall.

## Architecture

```
Python gym.Env (netherite_env.py)
    |
    | shmem: /tmp/netherite_obs_{id}_{A,B}    (pixels, ~1.6MB)
    | shmem: /tmp/netherite_state_{id}         (pos/health/inventory, ~64KB)
    | shmem: /tmp/netherite_action_{id}        (movement/camera/interact, 4KB)
    |
MC 1.20.1 Client (Java 17+, Fabric)
    ├── Sodium 0.5.x          -- rendering: one indirect draw for all chunks
    ├── Lithium 0.11.x        -- game logic: 30-40% faster ticking
    └── netherite-mod.jar      -- OUR MOD:
        ├── FrameGrabber.java      -- PBO double-buffered async readback -> shmem
        ├── ActionInjector.java    -- shmem -> player keyboard/mouse input
        ├── StateExporter.java     -- pos/health/inventory/entities -> shmem
        ├── WorldController.java   -- auto-create world, reset, teleport, seed control
        ├── NetheriteMod.java      -- entry point, registers tick handlers
        └── mixin/
            └── GameRendererMixin.java  -- hooks end-of-frame for PBO readback
```

## Tech Stack

| Component | Version | Source |
|---|---|---|
| Minecraft | 1.20.1 | Mojang |
| Fabric Loader | 0.16.14+ | fabricmc.net |
| Fabric API | 0.92.2+1.20.1 | fabricmc.net |
| Fabric Loom (gradle plugin) | 1.9-SNAPSHOT | fabricmc.net |
| Yarn Mappings | 1.20.1+build.10 | fabricmc.net |
| Sodium | mc1.20.1-0.5.13 | Modrinth maven (CaffeineMC, LGPL-3.0) |
| Lithium | mc1.20.1-0.11.2 | Modrinth maven (CaffeineMC, LGPL-3.0) |
| Java | 17+ (21 on anvil, whatever on macOS) | OpenJDK |
| Gradle | 8.12 | gradle.org |
| Python | 3.11+ | UV only |

## Shmem Protocol

All shmem uses memory-mapped files. On Linux: `/dev/shm/netherite_*`. On macOS: `/tmp/netherite_*`.

### Observation Buffer (pixels)
Path: `netherite_obs_{instance_id}_{A,B}` (double-buffered)
Size: 8MB per slot
```
Offset 0:  uint32 magic = 0x4E455432 ("NET2")
Offset 4:  uint32 frame_number
Offset 8:  uint32 data_size (W * H * 4)
Offset 12: uint32 ready_flag (written LAST)
Offset 16: uint8[W*H*4] RGBA pixels (GL bottom-up row order)
```
Writer (Java): clears ready_flag, writes pixels, sets ready_flag last.
Reader (Python): spins on ready_flag, reads data_size bytes.

### State Buffer
Path: `netherite_state_{instance_id}`
Size: 64KB
```
Offset 0:  uint32 magic = 0x4E455453 ("NETS")
Offset 4:  uint32 tick_number
Offset 8:  uint32 data_size
Offset 12: uint32 ready_flag
Offset 16: Player state:
  double x, y, z          (24 bytes)
  float yaw, pitch         (8 bytes)
  float health, max_health (8 bytes)
  int food_level           (4 bytes)
  float saturation         (4 bytes)
  int on_ground            (4 bytes)
  int in_water             (4 bytes)
Offset 72: Hotbar (9 slots):
  Per slot: int item_id (4) + int count (4) = 8 bytes each = 72 bytes
Offset 144: Nearby entities:
  int entity_count
  Per entity: int type_id (4) + double x,y,z (24) + float health (4) = 36 bytes
  Up to 32 entities = 1152 bytes max
```

### Action Buffer
Path: `netherite_action_{instance_id}`
Size: 4KB
```
Offset 0:  uint32 magic = 0x4E455441 ("NETA")
Offset 4:  uint32 tick_number
Offset 8:  uint32 data_size
Offset 12: uint32 ready_flag
Offset 16: Action payload:
  byte forward, back, left, right  (movement, 0 or 1)
  byte jump, sneak, sprint         (modifiers)
  byte attack, use                 (interact)
  byte camera_dx, camera_dy        (signed, -127 to 127)
```
Camera delta applied ONCE per new tick_number (frame-skip safe).
Movement keys held every tick (re-applied).

## Java Mod Implementation Details

### FrameGrabber.java (~120 lines)
- Creates 2 PBOs (GL_PIXEL_PACK_BUFFER) on init
- Each frame: kick async glReadPixels into PBO[N%2], map PBO[(N+1)%2] and copy to shmem
- Zero GPU stall -- one frame of latency (fine for RL)
- Triggered by GameRendererMixin @Inject at TAIL of GameRenderer.render()
- Uses LWJGL 3 GL calls directly (GL11, GL15, GL21 -- all on MC's classpath)
- Shmem via RandomAccessFile + FileChannel.map (MappedByteBuffer)
- Double-buffered shmem (A/B slots) so reader never sees partial writes

### ActionInjector.java (~100 lines)
- Maps shmem action buffer on init
- Each tick: read action, set KeyBinding states via KeyBinding.setKeyPressed()
- Camera rotation: player.setYaw() / player.setPitch()
- Frame-skip safe: track last tick_number, only apply camera delta once per new tick

### StateExporter.java (~120 lines)
- Maps shmem state buffer on init
- Each tick: write player pos/health/food, hotbar contents, nearby entities
- Entity scan: mc.world.getEntities() filtered by distance < 16 blocks, max 32
- Item IDs via Registries.ITEM.getRawId()
- Entity type IDs via Registries.ENTITY_TYPE.getRawId()

### WorldController.java (~100 lines)
- Detects title screen, auto-creates singleplayer world
- Fixed seed via -Dnetherite.seed=N system property
- GameRules: no daylight cycle, no weather cycle, no mob spawning (configurable)
- Reset support: delete world + recreate, or teleport to spawn
- Instance ID via -Dnetherite.instance=N

### NetheriteMod.java (~30 lines)
- Implements ClientModInitializer
- Registers ClientTickEvents.END_CLIENT_TICK -> ActionInjector.tick() + StateExporter.tick() + WorldController.tick()
- FrameGrabber triggered separately by mixin (needs to run in render thread, not tick thread)

### GameRendererMixin.java (~30 lines)
- @Mixin(GameRenderer.class)
- @Inject HEAD: skip-render mode (cancels render when `skipRender` is enabled)
- @Inject TAIL: triggers FrameGrabber.INSTANCE.onFrameReady() + render profiling
- This runs AFTER the full frame is rendered (world + entities + HUD + chat)

### FramebufferMixin.java (~40 lines)
- @Mixin(MinecraftClient.class) -- targets the call site, NOT the Framebuffer class
- @Redirect on `framebuffer.draw(II)V` inside `MinecraftClient.render(Z)V`
- When netherite.width/height are set, replaces MC's deferred shader blit with a direct `glBlitFramebuffer` from the FBO to the default framebuffer (screen)
- Stretches the small render (e.g. 160x90) to fill the actual GLFW window (e.g. 854x480) with GL_NEAREST filtering (pixelated)
- Key insight: MC's `Framebuffer.draw()` uses `RenderSystem.recordRenderCall()` (deferred lambda) which makes `@ModifyVariable` on the Framebuffer class ineffective. Direct GL blit bypasses this.

### WindowMixin.java (~60 lines)
- @Mixin(Window.class)
- Disables Retina framebuffer scaling on macOS (`GLFW_COCOA_RETINA_FRAMEBUFFER = FALSE`)
- Hides window in headless mode
- Disables VSync when uncapped
- Overrides `getFramebufferWidth()/getFramebufferHeight()` to return netherite.width/height (forces MC to render at low resolution)

## Python Gym Environment

### netherite_env.py
```python
class NetheriteEnv(gym.Env):
    observation_space = Dict({
        'pov': Box(0, 255, (H, W, 3), dtype=np.uint8),
        'inventory': Box(0, 64, (9, 2), dtype=np.int32),  # hotbar: item_id, count
        'health': Box(0, 20, (1,), dtype=np.float32),
        'position': Box(-1e6, 1e6, (3,), dtype=np.float64),
    })
    action_space = Dict({
        'forward': Discrete(2),
        'back': Discrete(2),
        'left': Discrete(2),
        'right': Discrete(2),
        'jump': Discrete(2),
        'sneak': Discrete(2),
        'sprint': Discrete(2),
        'attack': Discrete(2),
        'use': Discrete(2),
        'camera': Box(-180, 180, (2,), dtype=np.float32),  # delta yaw, pitch
    })
```

MineRL-compatible API. drop-in replacement.

### shmem_reader.py
- mmap the obs and state buffers
- Poll ready_flag, read pixels + state
- Flip Y (GL stores bottom-up)
- Convert RGBA -> RGB for observation

### shmem_writer.py
- mmap the action buffer
- Pack action dict into binary format
- Set ready_flag

## Build & Run

### Local Development (macOS)
```bash
cd ~/netherite
./gradlew build          # builds mod jar
./gradlew runClient      # launches MC with mod (you'll see the window)
```

### Headless Training (anvil, Linux)
```bash
# Start Xvfb for headless GL
Xvfb :99 -screen 0 854x480x24 &
export DISPLAY=:99

# Run with Sodium + Lithium
cd ~/netherite
./gradlew runClient -Dnetherite.instance=0 -Dnetherite.seed=12345

# Python training
uv run python train.py --num-envs 16
```

### Multi-Instance
Each instance gets unique:
- `-Dnetherite.instance=N` (shmem paths, save dirs)
- `-Dnetherite.seed=M` (world seed)
- Separate Gradle daemon or direct Java launch

## Version Choice

MC 1.20.1 was chosen for Sodium/Lithium mod ecosystem maturity and stable Fabric API/Yarn mappings. There is no hard technical reason against newer versions. Considerations for upgrading:

- **1.20.4+**: Minimum for VulkanMod (Vulkan renderer via MoltenVK on macOS, replaces Sodium's GL pipeline). No 1.20.1 build exists.
- **1.21.x**: Latest VulkanMod builds (0.6.1 for 1.21.11). Benchmarked at ~1000 FPS on M4 Max with Sodium in Prism Launcher.
- **Migration cost**: New yarn mappings, Fabric API version bump, mixin signature changes, FrameGrabber GL calls may need updating if VulkanMod replaces GL context.
- **VulkanMod vs Sodium**: Mutually exclusive. Both replace the renderer. VulkanMod uses Vulkan (lower dispatch overhead on macOS via MoltenVK), Sodium uses optimized GL (batched indirect draws). Cannot coexist.

## Performance Targets

| Metric | MineRL | Netherite v2 Target | Measured (M4 Max) |
|---|---|---|---|
| Frame readback | ~5ms (sync glReadPixels) | ~0ms (PBO async) | ~0.4ms (PBO) |
| Draw calls/frame | ~3000 (vanilla) | ~1-5 (Sodium indirect) | ~1-5 (Sodium) |
| Server TPS | 20 | 20+ (Lithium) | ~480 (uncapped) |
| Env step latency | ~50ms | ~16ms (60fps) | ~2.5ms (step), ~4.7ms (step_sync) |
| Throughput (B=1) | ~20 env/sec | ~60 env/sec | ~400 step, ~213 step_sync |
| Throughput (B=16) | ~20 env/sec | ~200+ env/sec | Not yet measured |
| Rendering correctness | 100% | 100% (same GL renderer) | 100% |

### Benchmark: Vanilla vs Sodium+Lithium (M4 Max, 160x90, headless, uncapped)

| Config | Variant | Vanilla | Sodium+Lithium | Delta |
|---|---|---|---|---|
| fps=260, RD=6 | step_sync | 143 | 213 | +49% |
| fps=260, RD=6 | step | 313 | 402 | +28% |
| fps=260, RD=6 | tick_only | 417 | 484 | +16% |
| fps=500, RD=6 | step_sync | 148 | 190 | +28% |
| fps=500, RD=2 | step_sync | 180 | 214 | +19% |

Reference: Prism Launcher + Sodium on MC 1.21.11 (same M4 Max): ~1000 FPS average. Gap vs our ~480 tick_only is due to per-tick mod overhead (StateExporter, ActionInjector, FrameGrabber, shmem writes).

## What Carries Over From v1

These optimizations from the 1.8.9 CUDA rasterizer project apply here too:
1. Shmem double-buffer protocol (proven, zero-copy)
2. Frame-skip-safe action injection (held keys repeat, camera delta once)
3. Pipelined render || train architecture
4. Multi-instance parameterization
5. TickAccelerator concept (strip unnecessary server work)
6. Python gym.Env design (action/obs spaces, rewards)

## What's New vs v1

1. No custom rendering code (Sodium handles it)
2. PBO async readback instead of glReadPixels sync
3. Fabric instead of Forge (lighter, faster startup)
4. MC 1.20.1 instead of 1.8.9 (modern Java, better APIs)
5. Mixin API instead of reflection hacks
6. Works on macOS for development (no CUDA needed)

## File Structure

```
~/netherite/
├── SPEC.md                          # this file
├── CLAUDE.md                        # build/run instructions for Claude
├── build.gradle                     # Fabric Loom build
├── settings.gradle                  # plugin repos
├── gradle.properties                # version pins
├── gradle/wrapper/                  # Gradle wrapper
├── src/main/java/com/netherite/mod/
│   ├── NetheriteMod.java            # entry point
│   ├── FrameGrabber.java            # PBO async readback
│   ├── ActionInjector.java          # shmem -> player input
│   ├── StateExporter.java           # game state -> shmem
│   ├── WorldController.java         # auto world creation/reset
│   └── mixin/
│       ├── GameRendererMixin.java   # end-of-frame hook, skip-render mode
│       ├── FramebufferMixin.java    # GL blit stretch (160x90 -> window)
│       ├── WindowMixin.java         # retina disable, framebuffer size override
│       ├── ClientFocusMixin.java    # keep running when unfocused
│       ├── ServerTickMixin.java     # uncapped TPS
│       ├── RenderTickCounterMixin.java  # uncapped FPS
│       └── ClientTickProfilerMixin.java # per-tick profiling
├── src/main/resources/
│   ├── fabric.mod.json              # mod metadata
│   └── netherite.mixins.json        # mixin config
└── env/                             # Python RL environment
    ├── netherite_env.py             # gym.Env
    ├── shmem_reader.py              # observation reader
    ├── shmem_writer.py              # action writer
    └── test_env.py                  # smoke test
```

## Phase Plan

### Phase 1: Mod works, pixels flow
- [x] Gradle project builds on macOS (Java 21 required, `JAVA_HOME` must point to OpenJDK 21)
- [x] MC launches, mod loads, auto-creates world
- [x] FrameGrabber writes pixels to shmem
- [x] Python script reads pixels, displays them (verify correctness)

### Phase 2: Full gym.Env
- [x] ActionInjector reads actions, player moves
- [x] StateExporter writes game state
- [x] netherite_env.py step/reset works
- [x] Benchmark: measure env/sec single instance

### Phase 3: Sodium + Lithium + Visual
- [x] Add Sodium jar, verify it loads alongside mod (0.5.13, LWJGL check bypassed in build.gradle)
- [x] Add Lithium jar (0.11.2)
- [x] Benchmark: Sodium+Lithium gives +28-49% step_sync, +28% step throughput vs vanilla
- [x] Pixelated agent view: 160x90 rendered at full FOV, GL-blit stretched to 854x480 window with GL_NEAREST

### Phase 4: Multi-instance + training
- [ ] Launch N instances on anvil
- [ ] Pipelined training loop
- [ ] Benchmark: env/sec at B=16
- [ ] Compare to MineRL baseline

## Future: CUDA-GL Interop (Anvil Only)

On anvil (RTX 3090), the current frame path has unnecessary CPU round-trips:

```
GPU renders frame → PBO readback (GPU→CPU) → shmem copy (CPU) → Python mmap (CPU) → numpy → GPU (training)
```

CUDA-GL interop eliminates the CPU entirely for observations:

```
GPU renders frame → cudaGraphicsGLRegisterImage → CUDA tensor (stays on GPU) → PyTorch policy
```

### Implementation Scope (~200 lines C + JNI bridge)

1. **C/CUDA library** (`libnetherite_interop.so`):
   - `cudaGraphicsGLRegisterBuffer()` to register MC's PBO as a CUDA resource
   - `cudaGraphicsMapResources()` + `cudaGraphicsResourceGetMappedPointer()` per frame
   - Exposes the mapped GPU pointer to Python via ctypes or a small pybind11 wrapper
   - `torch.as_tensor()` wraps the CUDA pointer as a PyTorch tensor (zero-copy)

2. **Java side changes**:
   - FrameGrabber exposes the PBO GL handle (already has it: `pbos[mapPbo]`)
   - JNI call or shmem-published PBO ID so the C library can register it
   - Alternatively: use a shared GL-CUDA texture instead of PBO

3. **Python side changes**:
   - Replace shmem frame reader with `interop.get_frame_tensor()` returning a CUDA tensor
   - Training loop consumes tensors directly, no numpy decode

### Expected Gains
- Eliminates ~0.4ms FrameGrabber shmem copy + ~0.3ms Python decode = ~0.7ms/step
- At current ~2.5ms/step, this is a ~28% reduction
- More importantly: frames never leave GPU VRAM, enabling higher resolutions without CPU bandwidth bottleneck

### Prerequisites
- Linux only (CUDA not available on macOS)
- MC must run with a real GL context (Xvfb on anvil)
- CUDA and GL contexts must share the same GPU
- Not compatible with VulkanMod (would need CUDA-Vulkan interop instead, similar API but `cudaImportExternalMemory`)

### Not Planned (for reference)
- CUDA-Vulkan interop: same concept but with `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT` + `cudaImportExternalMemory`. Only needed if VulkanMod replaces Sodium on anvil.
- Metal compute on macOS: MPS (Metal Performance Shaders) for training. PyTorch MPS backend exists but is less mature than CUDA. Would need Metal-GL interop for frame capture which Apple doesn't support well.

## Machines

- **local/macbook**: MacBook Pro M4 Max, 36GB, macOS. Development + testing.
- **anvil**: Ryzen 9 9950X3D, 92GB DDR5, RTX 3090 24GB, Ubuntu 24.04. Training at scale.
  - Access: `ssh anvil` (Tailscale, VPN can be flaky)
  - Java 21 installed, CUDA 13.2 (for PyTorch only, not rendering)
  - Xvfb for headless GL


# Doc 36: `netherite-v2-bench/AGENTS.md` {#doc-36}

*Absolute path: `/home/infatoshi/games/minecraft/netherite-v2-bench/AGENTS.md`*

# Netherite v2

Use this file as the current project handoff for coding agents. It is intentionally high signal and current. Read `SPEC.md` for the full design.

## Non-negotiables
- Use `uv` for all Python commands. No bare `python` or `pip`.
- Java 21 is required.
- On macOS, use `JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home`.
- On anvil, use `JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64`.
- Python verification standard in this repo is `uv run ruff check . --fix` and `uv run pytest tests/`.
- Fabric multi-instance launches must use isolated `--gameDir` roots under `run/instances/<id>`. Sharing a game dir corrupts Fabric's `.fabric/processedMods` cache.

## Machines
- `local/macbook`: MacBook Pro M4 Max, 36 GB, Metal, macOS 26.3.
- `anvil`: Ryzen 9 9950X3D, 92 GB DDR5, RTX 3090 24 GB, Ubuntu 24.04.
- `anvil` display for headless GL is `DISPLAY=:2`.
- SSH to anvil with `ssh anvil`.

## Build, Run, Test
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew build
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient
uv run ruff check . --fix
uv run pytest tests/
```

## Current Truth
- MC version is `1.20.1`, Fabric only.
- Rendering is Minecraft + Sodium. Do not add custom renderers.
- Sodium and Lithium are runtime-loaded from `run/mods/`.
- Sodium's LWJGL version check is bypassed in `build.gradle` with `-Dsodium.checks.issue2561=false`.
- Shared memory paths are `/tmp/netherite_*` on macOS and `/dev/shm/netherite_*` on Linux.
- `bench_scaling.py` now supports `--width` and `--height`.
- `grid_demo.py` now defaults to `320x180` capture for human-facing demos.

## Render Resolution Diagnosis
- The project does natively render fewer pixels. This is not a Matplotlib crop.
- The current low-res path works by overriding `Window.getFramebufferWidth()` and `getFramebufferHeight()` in `src/main/java/com/netherite/mod/mixin/WindowMixin.java`.
- `FrameGrabber` then reads that full low-res framebuffer with `glReadPixels`.
- `FramebufferMixin` then blits that full low-res framebuffer to the real window with `glBlitFramebuffer`.
- That means `160x90` is a true native render size today.
- The problem is that the whole Minecraft client framebuffer is shrunk to `160x90`, not just the agent observation.
- Vanilla HUD elements do not fit cleanly into `160x90`. Hearts and hotbar clip. This is why the human-facing demo looked cropped.
- `320x180` is currently the minimum sane native resolution for human-facing display. The HUD fits and the full frame is preserved.
- Proper long-term fix: decouple human display resolution from agent capture resolution. Keep UI/display at `320x180` or higher, and make agent capture a separate lower-res path.

## Human Demo Artifacts
- Native `320x180` single tile: `recordings/320_180_native.png`
- Native `1920x1080` single tile: `recordings/1920_1080_native.png`
- `160x90` vs `320x180` HUD comparison: `recordings/160_vs_320_comparison.png`
- `1920x1080` vs `320x180` native comparison: `recordings/1080p_vs_180p_comparison.png`
- `B=8` grid demo at `320x180`: `recordings/anvil_b8_batched_demo_320x180.mp4`

## Recent Stability Fixes That Matter
- `PosixSemaphore.java` had a Linux `O_CREAT` issue. The Linux flag is now correct, and `sem_post` is guarded against invalid handles.
- `BakedModelManagerMixin.java` guards `BakedModelManager.shouldRerender` when `stateLookup == null`. This removed a startup crash path at scale.
- The Python start latch no longer keys on `frame_hash`. It now ignores pure frame jitter and latches on stable pose/chunk/seed state instead. This was necessary for multi-instance stability.
- Startup tracing was added in `env/startup_trace.py` and integrated into launcher/benchmark paths for bring-up debugging.

## Multi-Instance State
- `B=8` is now stable enough to benchmark on anvil.
- The main remaining architectural issue is not startup corruption. It is the display/capture coupling described above.

## Known Good Benchmark Settings
- Render distance `4`
- Simulation distance `5`
- `max_fps=9999`
- `uncapped=true`
- `use_semaphore=true`

## Latest Benchmarks

### Local MacBook, single instance, render enabled
Settings: headless, `RD=4`, `SD=5`, `max_fps=9999`, semaphore on, `300` measured steps after `75` warmup.

| Resolution | `step_sync` | `step` | `state_only` | `tick_only` |
|---|---:|---:|---:|---:|
| `160x90` | `217.4` | `666.4` | `741.2` | `730.7` |
| `320x180` | `192.7` | `592.6` | `753.8` | `617.6` |

Interpretation:
- `320x180` costs about `11%` on the render-coupled step paths on the MacBook.

### Anvil, apples-to-apples `1` and `8` env sweep, `160x90`
Command source is `env/bench_scaling.py` with `--envs 1,8 --strategies sync,batched,async --steps 100 --warmup 10 --width 160 --height 90 --render-distance 4 --simulation-distance 5 --max-fps 9999 --use-semaphore`.

| Envs | `sync` | `batched` | `async` |
|---|---:|---:|---:|
| `1` | `216.0` | `359.7` | `329.8` |
| `8` | `135.4` | `490.9` | `638.8` |

Raw log: `recordings/anvil_bench_160x90_apples.log`

### Anvil, apples-to-apples `1` and `8` env sweep, `320x180`
Command source is `env/bench_scaling.py` with `--envs 1,8 --strategies sync,batched,async --steps 100 --warmup 10 --width 320 --height 180 --render-distance 4 --simulation-distance 5 --max-fps 9999 --use-semaphore`.

| Envs | `sync` | `batched` | `async` |
|---|---:|---:|---:|
| `1` | `184.7` | `1020.5` | `312.7` |
| `8` | `122.9` | `439.3` | `567.6` |

Raw log: `recordings/anvil_bench_320x180.log`

### Anvil `320x180` vs `160x90` at `B=8`
- `sync`: `135.4 -> 122.9` which is `-9.2%`
- `batched`: `490.9 -> 439.3` which is `-10.5%`
- `async`: `638.8 -> 567.6` which is `-11.1%`

Interpretation:
- Treat the `B=8` penalty for `320x180` as about `10%`.
- The `1-env batched` result at `320x180` is clearly not stable enough to use as a comparison point. Do not build conclusions on that number.

## Current Best Human-Facing Resolution
- Use `320x180` for any human-facing native display or recording.
- Do not use `160x90` when you need a faithful full HUD view.

## Current Best Agent-Facing Assumption
- `160x90` is still fine for a pure agent observation if HUD fidelity is irrelevant.
- The codebase does not yet properly support `display=320x180` with `agent capture=160x90` as separate knobs. That is the next important rendering change.

## Commands That Matter

### Local human demo
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient \
  -Dnetherite.instance_id=0 -Dnetherite.seed=12345 -Dnetherite.rl=false \
  -Dnetherite.width=320 -Dnetherite.height=180 \
  -Dnetherite.uncapped=true -Dnetherite.max_fps=9999 \
  -Dnetherite.render_distance=4 -Dnetherite.simulation_distance=5 \
  -Dnetherite.graphics=fast -Dnetherite.particles=minimal -Dnetherite.clouds=off \
  -Dnetherite.smooth_lighting=false -Dnetherite.entity_shadows=false \
  -Dnetherite.biome_blend=0 -Dnetherite.vsync=false \
  --args="--width 854 --height 480 --username player"
```

### Anvil benchmark at `320x180`
```bash
ssh anvil '
  cd ~/netherite-v2-bench &&
  DISPLAY=:2 uv run env/bench_scaling.py \
    --envs 1,8 \
    --strategies sync,batched,async \
    --steps 100 --warmup 10 \
    --width 320 --height 180 \
    --java-home /usr/lib/jvm/java-21-openjdk-amd64 \
    --render-distance 4 --simulation-distance 5 --max-fps 9999 \
    --use-semaphore --env-timeout 30.0
'
```

## Do Not Waste Time On
- Do not revive the old CUDA rasterizer or Forge code.
- Do not attempt `@ModifyVariable` on `Framebuffer.draw()`. It does not hit the visible path.
- Do not share a Fabric `gameDir` across instances.
- Do not treat the current `160x90` human display issue as a Matplotlib crop bug. It is a framebuffer architecture issue.

## Best Next Steps
- Implement proper separation between display resolution and agent capture resolution.
- Keep `320x180` as the human display baseline.
- Re-run `B=16` on anvil once the display/capture split is clean.


# Doc 37: `netherite-v2-bench/CLAUDE.md` {#doc-37}

*Absolute path: `/home/infatoshi/games/minecraft/netherite-v2-bench/CLAUDE.md`*

# Netherite v2

Use this file as the Claude Code handoff. It is intentionally high signal and current. Read `SPEC.md` for the full design. Read `AGENTS.md` for the stricter workflow rules.

## Non-negotiables
- Use `uv` for all Python commands. No bare `python` or `pip`.
- Java 21 is required.
- On macOS, use `JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home`.
- On anvil, use `JAVA_HOME=/usr/lib/jvm/java-21-openjdk-amd64`.
- Python verification standard in this repo is `uv run ruff check . --fix` and `uv run pytest tests/`.
- Fabric multi-instance launches must use isolated `--gameDir` roots under `run/instances/<id>`. Sharing a game dir corrupts Fabric's `.fabric/processedMods` cache.

## Machines
- `local/macbook`: MacBook Pro M4 Max, 36 GB, Metal, macOS 26.3.
- `anvil`: Ryzen 9 9950X3D, 92 GB DDR5, RTX 3090 24 GB, Ubuntu 24.04.
- `anvil` display for headless GL is `DISPLAY=:2`.
- SSH to anvil with `ssh anvil`.

## Build, Run, Test
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew build
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient
uv run ruff check . --fix
uv run pytest tests/
```

## Current Truth
- MC version is `1.20.1`, Fabric only.
- Rendering is Minecraft + Sodium. Do not add custom renderers.
- Sodium and Lithium are runtime-loaded from `run/mods/`.
- Sodium's LWJGL version check is bypassed in `build.gradle` with `-Dsodium.checks.issue2561=false`.
- Shared memory paths are `/tmp/netherite_*` on macOS and `/dev/shm/netherite_*` on Linux.
- `bench_scaling.py` now supports `--width` and `--height`.
- `grid_demo.py` now defaults to `320x180` capture for human-facing demos.

## Render Resolution Diagnosis
- The project does natively render fewer pixels. This is not a Matplotlib crop.
- The current low-res path works by overriding `Window.getFramebufferWidth()` and `getFramebufferHeight()` in `src/main/java/com/netherite/mod/mixin/WindowMixin.java`.
- `FrameGrabber` then reads that full low-res framebuffer with `glReadPixels`.
- `FramebufferMixin` then blits that full low-res framebuffer to the real window with `glBlitFramebuffer`.
- That means `160x90` is a true native render size today.
- The problem is that the whole Minecraft client framebuffer is shrunk to `160x90`, not just the agent observation.
- Vanilla HUD elements do not fit cleanly into `160x90`. Hearts and hotbar clip. This is why the human-facing demo looked cropped.
- `320x180` is currently the minimum sane native resolution for human-facing display. The HUD fits and the full frame is preserved.
- Proper long-term fix: decouple human display resolution from agent capture resolution. Keep UI/display at `320x180` or higher, and make agent capture a separate lower-res path.

## Human Demo Artifacts
- Native `320x180` single tile: `recordings/320_180_native.png`
- Native `1920x1080` single tile: `recordings/1920_1080_native.png`
- `160x90` vs `320x180` HUD comparison: `recordings/160_vs_320_comparison.png`
- `1920x1080` vs `320x180` native comparison: `recordings/1080p_vs_180p_comparison.png`
- `B=8` grid demo at `320x180`: `recordings/anvil_b8_batched_demo_320x180.mp4`

## Recent Stability Fixes That Matter
- `PosixSemaphore.java` had a Linux `O_CREAT` issue. The Linux flag is now correct, and `sem_post` is guarded against invalid handles.
- `BakedModelManagerMixin.java` guards `BakedModelManager.shouldRerender` when `stateLookup == null`. This removed a startup crash path at scale.
- The Python start latch no longer keys on `frame_hash`. It now ignores pure frame jitter and latches on stable pose/chunk/seed state instead. This was necessary for multi-instance stability.
- Startup tracing was added in `env/startup_trace.py` and integrated into launcher/benchmark paths for bring-up debugging.

## Multi-Instance State
- `B=8` is now stable enough to benchmark on anvil.
- The main remaining architectural issue is not startup corruption. It is the display/capture coupling described above.

## Known Good Benchmark Settings
- Render distance `4`
- Simulation distance `5`
- `max_fps=9999`
- `uncapped=true`
- `use_semaphore=true`

## Latest Benchmarks

### Local MacBook, single instance, render enabled
Settings: headless, `RD=4`, `SD=5`, `max_fps=9999`, semaphore on, `300` measured steps after `75` warmup.

| Resolution | `step_sync` | `step` | `state_only` | `tick_only` |
|---|---:|---:|---:|---:|
| `160x90` | `217.4` | `666.4` | `741.2` | `730.7` |
| `320x180` | `192.7` | `592.6` | `753.8` | `617.6` |

Interpretation:
- `320x180` costs about `11%` on the render-coupled step paths on the MacBook.

### Anvil, apples-to-apples `1` and `8` env sweep, `160x90`
Command source is `env/bench_scaling.py` with `--envs 1,8 --strategies sync,batched,async --steps 100 --warmup 10 --width 160 --height 90 --render-distance 4 --simulation-distance 5 --max-fps 9999 --use-semaphore`.

| Envs | `sync` | `batched` | `async` |
|---|---:|---:|---:|
| `1` | `216.0` | `359.7` | `329.8` |
| `8` | `135.4` | `490.9` | `638.8` |

Raw log: `recordings/anvil_bench_160x90_apples.log`

### Anvil, apples-to-apples `1` and `8` env sweep, `320x180`
Command source is `env/bench_scaling.py` with `--envs 1,8 --strategies sync,batched,async --steps 100 --warmup 10 --width 320 --height 180 --render-distance 4 --simulation-distance 5 --max-fps 9999 --use-semaphore`.

| Envs | `sync` | `batched` | `async` |
|---|---:|---:|---:|
| `1` | `184.7` | `1020.5` | `312.7` |
| `8` | `122.9` | `439.3` | `567.6` |

Raw log: `recordings/anvil_bench_320x180.log`

### Anvil `320x180` vs `160x90` at `B=8`
- `sync`: `135.4 -> 122.9` which is `-9.2%`
- `batched`: `490.9 -> 439.3` which is `-10.5%`
- `async`: `638.8 -> 567.6` which is `-11.1%`

Interpretation:
- Treat the `B=8` penalty for `320x180` as about `10%`.
- The `1-env batched` result at `320x180` is clearly not stable enough to use as a comparison point. Do not build conclusions on that number.

## Current Best Human-Facing Resolution
- Use `320x180` for any human-facing native display or recording.
- Do not use `160x90` when you need a faithful full HUD view.

## Current Best Agent-Facing Assumption
- `160x90` is still fine for a pure agent observation if HUD fidelity is irrelevant.
- The codebase does not yet properly support `display=320x180` with `agent capture=160x90` as separate knobs. That is the next important rendering change.

## Commands That Matter

### Local human demo
```bash
JAVA_HOME=/opt/homebrew/opt/openjdk@21/libexec/openjdk.jdk/Contents/Home ./gradlew runClient \
  -Dnetherite.instance_id=0 -Dnetherite.seed=12345 -Dnetherite.rl=false \
  -Dnetherite.width=320 -Dnetherite.height=180 \
  -Dnetherite.uncapped=true -Dnetherite.max_fps=9999 \
  -Dnetherite.render_distance=4 -Dnetherite.simulation_distance=5 \
  -Dnetherite.graphics=fast -Dnetherite.particles=minimal -Dnetherite.clouds=off \
  -Dnetherite.smooth_lighting=false -Dnetherite.entity_shadows=false \
  -Dnetherite.biome_blend=0 -Dnetherite.vsync=false \
  --args="--width 854 --height 480 --username player"
```

### Anvil benchmark at `320x180`
```bash
ssh anvil '
  cd ~/netherite-v2-bench &&
  DISPLAY=:2 uv run env/bench_scaling.py \
    --envs 1,8 \
    --strategies sync,batched,async \
    --steps 100 --warmup 10 \
    --width 320 --height 180 \
    --java-home /usr/lib/jvm/java-21-openjdk-amd64 \
    --render-distance 4 --simulation-distance 5 --max-fps 9999 \
    --use-semaphore --env-timeout 30.0
'
```

## Do Not Waste Time On
- Do not revive the old CUDA rasterizer or Forge code.
- Do not attempt `@ModifyVariable` on `Framebuffer.draw()`. It does not hit the visible path.
- Do not share a Fabric `gameDir` across instances.
- Do not treat the current `160x90` human display issue as a Matplotlib crop bug. It is a framebuffer architecture issue.

## Best Next Steps
- Implement proper separation between display resolution and agent capture resolution.
- Keep `320x180` as the human display baseline.
- Re-run `B=16` on anvil once the display/capture split is clean.


# Doc 38: `netherite-v2-bench/README.md` {#doc-38}

*Absolute path: `/home/infatoshi/games/minecraft/netherite-v2-bench/README.md`*

# Netherite

Python-controlled Minecraft RL environment. MC 1.20.1 + Fabric + Lithium/Sodium. Zero manual GUI interaction -- boot straight into a world with every setting configurable from Python.

## Prerequisites

- **Java 17+** (21 recommended). Check with `java -version`.
- **Python 3.11+** via [uv](https://docs.astral.sh/uv/).
- **Minecraft ownership is required.** Netherite uses Fabric Loom which downloads MC assets from Mojang's servers during build. This is permitted under Mojang's EULA for development. We do not redistribute Minecraft.

## Quick Start

```bash
# Clone
git clone <repo-url> && cd netherite

# Build (downloads MC 1.20.1 + Fabric + dependencies on first run)
JAVA_HOME=/path/to/jdk21 ./gradlew build

# Launch MC with the mod (opens a window, auto-creates world)
JAVA_HOME=/path/to/jdk21 ./gradlew runClient

# Optional: add Sodium + Lithium for performance
mkdir -p run/mods
# Download from https://modrinth.com/mod/sodium (1.20.1 Fabric)
# Download from https://modrinth.com/mod/lithium (1.20.1 Fabric)
# Drop jars into run/mods/
```

## Python Environment

```bash
cd netherite
uv venv && uv pip install numpy gymnasium pillow

# With MC running, read a frame:
uv run python -c "
import sys; sys.path.insert(0, 'env')
from config import NetheriteConfig
from netherite_env import NetheriteEnv
env = NetheriteEnv()
obs, _ = env.reset()
print(f'Frame: {obs[\"pov\"].shape}, pos: {obs[\"position\"]}')
env.close()
"
```

## Configuration

Every setting is controlled via `-Dnetherite.*` JVM flags, which map to `NetheriteConfig` fields in Python.

```python
from config import NetheriteConfig

cfg = NetheriteConfig(
    seed=42,
    render_distance=4,
    simulation_distance=4,
    graphics="fast",
    particles="minimal",
    clouds="off",
    smooth_lighting=False,
    do_mob_spawning=False,
    do_daylight_cycle=False,
    rl=True,  # auto-dismiss menus, disable pause
)

# Launch with these settings:
# ./gradlew runClient <cfg.to_gradle_args()>
```

### Key settings

| Setting | Default | Notes |
|---------|---------|-------|
| `seed` | 12345 | World seed |
| `render_distance` | 8 | Chunks. Lower = faster. |
| `simulation_distance` | 5 | Chunks. Lower = faster. |
| `graphics` | fast | fast/fancy/fabulous |
| `particles` | minimal | all/decreased/minimal |
| `clouds` | off | off/fast/fancy |
| `max_fps` | 60 | FPS cap |
| `rl` | false | True = auto-dismiss menus, suppress toasts |
| `game_mode` | survival | survival/creative/adventure/spectator |
| `do_mob_spawning` | false | Game rule |
| `do_daylight_cycle` | false | Game rule |

See `env/config.py` for the full list.

## RL Mode

Pass `-Dnetherite.rl=true` to enable training mode:
- Auto-dismisses pause menu and other screens
- Suppresses toast notifications and tutorials
- Disables pause on lost focus

Without it, MC behaves normally (ESC works, GUI is interactive).

```bash
./gradlew runClient -Dnetherite.rl=true -Dnetherite.seed=42
```

## Headless (Linux / Training)

On a Linux server without a display, use Xvfb:

```bash
Xvfb :99 -screen 0 854x480x24 &
export DISPLAY=:99

./gradlew runClient \
  -Dnetherite.rl=true \
  -Dnetherite.render_distance=4 \
  -Dnetherite.seed=42
```

Resolution is controlled by the Xvfb screen size.

## Multi-Instance

Each instance needs a unique ID and its own shmem buffers:

```bash
# Instance 0
./gradlew runClient -Dnetherite.instance_id=0 -Dnetherite.seed=100 &

# Instance 1
./gradlew runClient -Dnetherite.instance_id=1 -Dnetherite.seed=200 &
```

Or use the Python launcher:

```python
from config import NetheriteConfig
from launcher import Launcher

launcher = Launcher("/path/to/netherite")
configs = [NetheriteConfig(instance_id=i, seed=i*100, rl=True) for i in range(4)]
launcher.launch(configs)
launcher.wait_all_ready()
```

## Architecture

```
Python (netherite_env.py)
    |
    | shmem: /tmp/netherite_obs_{id}_{A,B}    (RGBA pixels, PBO double-buffered)
    | shmem: /tmp/netherite_state_{id}         (pos/health/inventory/entities)
    | shmem: /tmp/netherite_action_{id}        (movement/camera/interact)
    |
MC 1.20.1 (Java, Fabric)
    ├── Sodium         -- batched indirect rendering (optional)
    ├── Lithium        -- optimized game logic (optional)
    └── netherite mod  -- PBO readback, action injection, state export
```

## Shmem Protocol

All little-endian. Ready flag at offset 12 written last.

- **Obs** (`netherite_obs_{id}_{A,B}`): 8MB, 16B header + RGBA pixels
- **State** (`netherite_state_{id}`): 64KB, player pos/health/food/inventory/entities
- **Action** (`netherite_action_{id}`): 4KB, movement keys + camera delta

See `SPEC.md` for byte-level layout.

## Machines

- **macOS** (dev): Window opens, Sodium may not load (LWJGL compat). Lithium works.
- **Linux** (training): Xvfb + Sodium + Lithium. Sodium requires GPU with OpenGL 4.6.


# Doc 39: `netherite-v2-bench/SPEC.md` {#doc-39}

*Absolute path: `/home/infatoshi/games/minecraft/netherite-v2-bench/SPEC.md`*

# Netherite v2: Fast Minecraft RL Environment

## Goal

Fully Python-controlled Minecraft RL environment. Every game setting configurable from Python: resolution, render distance, game rules, graphics quality, JVM count, instances per JVM. Zero manual GUI interaction -- boot straight into a world. Performance depends on these knobs (resolution, render distance, graphics level, instance count) and is explored empirically, not targeted at a fixed multiplier.

Uses MC 1.20.1 + Sodium + Fabric + a tiny custom mod. No custom rendering code. MC's OpenGL renderer handles everything correctly (mobs, entities, water, particles, sky, HUD). We capture frames via PBO async readback and pipe them to Python through shared memory.

## Why Not Custom Rendering

Previous attempt: built a CUDA software rasterizer that replicated MC's OpenGL output. Achieved 93% pixel-exact match for static blocks, but hit unbounded rendering bugs: missing mobs/entities, water artifacts, x-ray vision, no particles/sky/HUD. Every fix revealed new issues. MC has hundreds of rendering features and reimplementing them all is a losing game.

The insight: MC's own renderer already handles everything. The bottleneck in MineRL isn't the GPU (it's mostly idle) -- it's the CPU-side overhead of thousands of GL draw calls per frame and synchronous frame readback. Sodium fixes the draw calls (batches everything into ~1-5 indirect draws). PBO async readback fixes the stall.

## Architecture

```
Python gym.Env (netherite_env.py)
    |
    | shmem: /tmp/netherite_obs_{id}_{A,B}    (pixels, ~1.6MB)
    | shmem: /tmp/netherite_state_{id}         (pos/health/inventory, ~64KB)
    | shmem: /tmp/netherite_action_{id}        (movement/camera/interact, 4KB)
    |
MC 1.20.1 Client (Java 17+, Fabric)
    ├── Sodium 0.5.x          -- rendering: one indirect draw for all chunks
    ├── Lithium 0.11.x        -- game logic: 30-40% faster ticking
    └── netherite-mod.jar      -- OUR MOD:
        ├── FrameGrabber.java      -- PBO double-buffered async readback -> shmem
        ├── ActionInjector.java    -- shmem -> player keyboard/mouse input
        ├── StateExporter.java     -- pos/health/inventory/entities -> shmem
        ├── WorldController.java   -- auto-create world, reset, teleport, seed control
        ├── NetheriteMod.java      -- entry point, registers tick handlers
        └── mixin/
            └── GameRendererMixin.java  -- hooks end-of-frame for PBO readback
```

## Tech Stack

| Component | Version | Source |
|---|---|---|
| Minecraft | 1.20.1 | Mojang |
| Fabric Loader | 0.16.14+ | fabricmc.net |
| Fabric API | 0.92.2+1.20.1 | fabricmc.net |
| Fabric Loom (gradle plugin) | 1.9-SNAPSHOT | fabricmc.net |
| Yarn Mappings | 1.20.1+build.10 | fabricmc.net |
| Sodium | mc1.20.1-0.5.13 | Modrinth maven (CaffeineMC, LGPL-3.0) |
| Lithium | mc1.20.1-0.11.2 | Modrinth maven (CaffeineMC, LGPL-3.0) |
| Java | 17+ (21 on anvil, whatever on macOS) | OpenJDK |
| Gradle | 8.12 | gradle.org |
| Python | 3.11+ | UV only |

## Shmem Protocol

All shmem uses memory-mapped files. On Linux: `/dev/shm/netherite_*`. On macOS: `/tmp/netherite_*`.

### Observation Buffer (pixels)
Path: `netherite_obs_{instance_id}_{A,B}` (double-buffered)
Size: 8MB per slot
```
Offset 0:  uint32 magic = 0x4E455432 ("NET2")
Offset 4:  uint32 frame_number
Offset 8:  uint32 data_size (W * H * 4)
Offset 12: uint32 ready_flag (written LAST)
Offset 16: uint8[W*H*4] RGBA pixels (GL bottom-up row order)
```
Writer (Java): clears ready_flag, writes pixels, sets ready_flag last.
Reader (Python): spins on ready_flag, reads data_size bytes.

### State Buffer
Path: `netherite_state_{instance_id}`
Size: 64KB
```
Offset 0:  uint32 magic = 0x4E455453 ("NETS")
Offset 4:  uint32 tick_number
Offset 8:  uint32 data_size
Offset 12: uint32 ready_flag
Offset 16: Player state:
  double x, y, z          (24 bytes)
  float yaw, pitch         (8 bytes)
  float health, max_health (8 bytes)
  int food_level           (4 bytes)
  float saturation         (4 bytes)
  int on_ground            (4 bytes)
  int in_water             (4 bytes)
Offset 72: Hotbar (9 slots):
  Per slot: int item_id (4) + int count (4) = 8 bytes each = 72 bytes
Offset 144: Nearby entities:
  int entity_count
  Per entity: int type_id (4) + double x,y,z (24) + float health (4) = 36 bytes
  Up to 32 entities = 1152 bytes max
```

### Action Buffer
Path: `netherite_action_{instance_id}`
Size: 4KB
```
Offset 0:  uint32 magic = 0x4E455441 ("NETA")
Offset 4:  uint32 tick_number
Offset 8:  uint32 data_size
Offset 12: uint32 ready_flag
Offset 16: Action payload:
  byte forward, back, left, right  (movement, 0 or 1)
  byte jump, sneak, sprint         (modifiers)
  byte attack, use                 (interact)
  byte camera_dx, camera_dy        (signed, -127 to 127)
```
Camera delta applied ONCE per new tick_number (frame-skip safe).
Movement keys held every tick (re-applied).

## Java Mod Implementation Details

### FrameGrabber.java (~120 lines)
- Creates 2 PBOs (GL_PIXEL_PACK_BUFFER) on init
- Each frame: kick async glReadPixels into PBO[N%2], map PBO[(N+1)%2] and copy to shmem
- Zero GPU stall -- one frame of latency (fine for RL)
- Triggered by GameRendererMixin @Inject at TAIL of GameRenderer.render()
- Uses LWJGL 3 GL calls directly (GL11, GL15, GL21 -- all on MC's classpath)
- Shmem via RandomAccessFile + FileChannel.map (MappedByteBuffer)
- Double-buffered shmem (A/B slots) so reader never sees partial writes

### ActionInjector.java (~100 lines)
- Maps shmem action buffer on init
- Each tick: read action, set KeyBinding states via KeyBinding.setKeyPressed()
- Camera rotation: player.setYaw() / player.setPitch()
- Frame-skip safe: track last tick_number, only apply camera delta once per new tick

### StateExporter.java (~120 lines)
- Maps shmem state buffer on init
- Each tick: write player pos/health/food, hotbar contents, nearby entities
- Entity scan: mc.world.getEntities() filtered by distance < 16 blocks, max 32
- Item IDs via Registries.ITEM.getRawId()
- Entity type IDs via Registries.ENTITY_TYPE.getRawId()

### WorldController.java (~100 lines)
- Detects title screen, auto-creates singleplayer world
- Fixed seed via -Dnetherite.seed=N system property
- GameRules: no daylight cycle, no weather cycle, no mob spawning (configurable)
- Reset support: delete world + recreate, or teleport to spawn
- Instance ID via -Dnetherite.instance=N

### NetheriteMod.java (~30 lines)
- Implements ClientModInitializer
- Registers ClientTickEvents.END_CLIENT_TICK -> ActionInjector.tick() + StateExporter.tick() + WorldController.tick()
- FrameGrabber triggered separately by mixin (needs to run in render thread, not tick thread)

### GameRendererMixin.java (~30 lines)
- @Mixin(GameRenderer.class)
- @Inject HEAD: skip-render mode (cancels render when `skipRender` is enabled)
- @Inject TAIL: triggers FrameGrabber.INSTANCE.onFrameReady() + render profiling
- This runs AFTER the full frame is rendered (world + entities + HUD + chat)

### FramebufferMixin.java (~40 lines)
- @Mixin(MinecraftClient.class) -- targets the call site, NOT the Framebuffer class
- @Redirect on `framebuffer.draw(II)V` inside `MinecraftClient.render(Z)V`
- When netherite.width/height are set, replaces MC's deferred shader blit with a direct `glBlitFramebuffer` from the FBO to the default framebuffer (screen)
- Stretches the small render (e.g. 160x90) to fill the actual GLFW window (e.g. 854x480) with GL_NEAREST filtering (pixelated)
- Key insight: MC's `Framebuffer.draw()` uses `RenderSystem.recordRenderCall()` (deferred lambda) which makes `@ModifyVariable` on the Framebuffer class ineffective. Direct GL blit bypasses this.

### WindowMixin.java (~60 lines)
- @Mixin(Window.class)
- Disables Retina framebuffer scaling on macOS (`GLFW_COCOA_RETINA_FRAMEBUFFER = FALSE`)
- Hides window in headless mode
- Disables VSync when uncapped
- Overrides `getFramebufferWidth()/getFramebufferHeight()` to return netherite.width/height (forces MC to render at low resolution)

## Python Gym Environment

### netherite_env.py
```python
class NetheriteEnv(gym.Env):
    observation_space = Dict({
        'pov': Box(0, 255, (H, W, 3), dtype=np.uint8),
        'inventory': Box(0, 64, (9, 2), dtype=np.int32),  # hotbar: item_id, count
        'health': Box(0, 20, (1,), dtype=np.float32),
        'position': Box(-1e6, 1e6, (3,), dtype=np.float64),
    })
    action_space = Dict({
        'forward': Discrete(2),
        'back': Discrete(2),
        'left': Discrete(2),
        'right': Discrete(2),
        'jump': Discrete(2),
        'sneak': Discrete(2),
        'sprint': Discrete(2),
        'attack': Discrete(2),
        'use': Discrete(2),
        'camera': Box(-180, 180, (2,), dtype=np.float32),  # delta yaw, pitch
    })
```

MineRL-compatible API. drop-in replacement.

### shmem_reader.py
- mmap the obs and state buffers
- Poll ready_flag, read pixels + state
- Flip Y (GL stores bottom-up)
- Convert RGBA -> RGB for observation

### shmem_writer.py
- mmap the action buffer
- Pack action dict into binary format
- Set ready_flag

## Build & Run

### Local Development (macOS)
```bash
cd ~/netherite
./gradlew build          # builds mod jar
./gradlew runClient      # launches MC with mod (you'll see the window)
```

### Headless Training (anvil, Linux)
```bash
# Start Xvfb for headless GL
Xvfb :99 -screen 0 854x480x24 &
export DISPLAY=:99

# Run with Sodium + Lithium
cd ~/netherite
./gradlew runClient -Dnetherite.instance=0 -Dnetherite.seed=12345

# Python training
uv run python train.py --num-envs 16
```

### Multi-Instance
Each instance gets unique:
- `-Dnetherite.instance=N` (shmem paths, save dirs)
- `-Dnetherite.seed=M` (world seed)
- Separate Gradle daemon or direct Java launch

## Version Choice

MC 1.20.1 was chosen for Sodium/Lithium mod ecosystem maturity and stable Fabric API/Yarn mappings. There is no hard technical reason against newer versions. Considerations for upgrading:

- **1.20.4+**: Minimum for VulkanMod (Vulkan renderer via MoltenVK on macOS, replaces Sodium's GL pipeline). No 1.20.1 build exists.
- **1.21.x**: Latest VulkanMod builds (0.6.1 for 1.21.11). Benchmarked at ~1000 FPS on M4 Max with Sodium in Prism Launcher.
- **Migration cost**: New yarn mappings, Fabric API version bump, mixin signature changes, FrameGrabber GL calls may need updating if VulkanMod replaces GL context.
- **VulkanMod vs Sodium**: Mutually exclusive. Both replace the renderer. VulkanMod uses Vulkan (lower dispatch overhead on macOS via MoltenVK), Sodium uses optimized GL (batched indirect draws). Cannot coexist.

## Performance Targets

| Metric | MineRL | Netherite v2 Target | Measured (M4 Max) |
|---|---|---|---|
| Frame readback | ~5ms (sync glReadPixels) | ~0ms (PBO async) | ~0.4ms (PBO) |
| Draw calls/frame | ~3000 (vanilla) | ~1-5 (Sodium indirect) | ~1-5 (Sodium) |
| Server TPS | 20 | 20+ (Lithium) | ~480 (uncapped) |
| Env step latency | ~50ms | ~16ms (60fps) | ~2.5ms (step), ~4.7ms (step_sync) |
| Throughput (B=1) | ~20 env/sec | ~60 env/sec | ~400 step, ~213 step_sync |
| Throughput (B=16) | ~20 env/sec | ~200+ env/sec | Not yet measured |
| Rendering correctness | 100% | 100% (same GL renderer) | 100% |

### Benchmark: Vanilla vs Sodium+Lithium (M4 Max, 160x90, headless, uncapped)

| Config | Variant | Vanilla | Sodium+Lithium | Delta |
|---|---|---|---|---|
| fps=260, RD=6 | step_sync | 143 | 213 | +49% |
| fps=260, RD=6 | step | 313 | 402 | +28% |
| fps=260, RD=6 | tick_only | 417 | 484 | +16% |
| fps=500, RD=6 | step_sync | 148 | 190 | +28% |
| fps=500, RD=2 | step_sync | 180 | 214 | +19% |

Reference: Prism Launcher + Sodium on MC 1.21.11 (same M4 Max): ~1000 FPS average. Gap vs our ~480 tick_only is due to per-tick mod overhead (StateExporter, ActionInjector, FrameGrabber, shmem writes).

## What Carries Over From v1

These optimizations from the 1.8.9 CUDA rasterizer project apply here too:
1. Shmem double-buffer protocol (proven, zero-copy)
2. Frame-skip-safe action injection (held keys repeat, camera delta once)
3. Pipelined render || train architecture
4. Multi-instance parameterization
5. TickAccelerator concept (strip unnecessary server work)
6. Python gym.Env design (action/obs spaces, rewards)

## What's New vs v1

1. No custom rendering code (Sodium handles it)
2. PBO async readback instead of glReadPixels sync
3. Fabric instead of Forge (lighter, faster startup)
4. MC 1.20.1 instead of 1.8.9 (modern Java, better APIs)
5. Mixin API instead of reflection hacks
6. Works on macOS for development (no CUDA needed)

## File Structure

```
~/netherite/
├── SPEC.md                          # this file
├── CLAUDE.md                        # build/run instructions for Claude
├── build.gradle                     # Fabric Loom build
├── settings.gradle                  # plugin repos
├── gradle.properties                # version pins
├── gradle/wrapper/                  # Gradle wrapper
├── src/main/java/com/netherite/mod/
│   ├── NetheriteMod.java            # entry point
│   ├── FrameGrabber.java            # PBO async readback
│   ├── ActionInjector.java          # shmem -> player input
│   ├── StateExporter.java           # game state -> shmem
│   ├── WorldController.java         # auto world creation/reset
│   └── mixin/
│       ├── GameRendererMixin.java   # end-of-frame hook, skip-render mode
│       ├── FramebufferMixin.java    # GL blit stretch (160x90 -> window)
│       ├── WindowMixin.java         # retina disable, framebuffer size override
│       ├── ClientFocusMixin.java    # keep running when unfocused
│       ├── ServerTickMixin.java     # uncapped TPS
│       ├── RenderTickCounterMixin.java  # uncapped FPS
│       └── ClientTickProfilerMixin.java # per-tick profiling
├── src/main/resources/
│   ├── fabric.mod.json              # mod metadata
│   └── netherite.mixins.json        # mixin config
└── env/                             # Python RL environment
    ├── netherite_env.py             # gym.Env
    ├── shmem_reader.py              # observation reader
    ├── shmem_writer.py              # action writer
    └── test_env.py                  # smoke test
```

## Phase Plan

### Phase 1: Mod works, pixels flow
- [x] Gradle project builds on macOS (Java 21 required, `JAVA_HOME` must point to OpenJDK 21)
- [x] MC launches, mod loads, auto-creates world
- [x] FrameGrabber writes pixels to shmem
- [x] Python script reads pixels, displays them (verify correctness)

### Phase 2: Full gym.Env
- [x] ActionInjector reads actions, player moves
- [x] StateExporter writes game state
- [x] netherite_env.py step/reset works
- [x] Benchmark: measure env/sec single instance

### Phase 3: Sodium + Lithium + Visual
- [x] Add Sodium jar, verify it loads alongside mod (0.5.13, LWJGL check bypassed in build.gradle)
- [x] Add Lithium jar (0.11.2)
- [x] Benchmark: Sodium+Lithium gives +28-49% step_sync, +28% step throughput vs vanilla
- [x] Pixelated agent view: 160x90 rendered at full FOV, GL-blit stretched to 854x480 window with GL_NEAREST

### Phase 4: Multi-instance + training
- [ ] Launch N instances on anvil
- [ ] Pipelined training loop
- [ ] Benchmark: env/sec at B=16
- [ ] Compare to MineRL baseline

## Future: CUDA-GL Interop (Anvil Only)

On anvil (RTX 3090), the current frame path has unnecessary CPU round-trips:

```
GPU renders frame → PBO readback (GPU→CPU) → shmem copy (CPU) → Python mmap (CPU) → numpy → GPU (training)
```

CUDA-GL interop eliminates the CPU entirely for observations:

```
GPU renders frame → cudaGraphicsGLRegisterImage → CUDA tensor (stays on GPU) → PyTorch policy
```

### Implementation Scope (~200 lines C + JNI bridge)

1. **C/CUDA library** (`libnetherite_interop.so`):
   - `cudaGraphicsGLRegisterBuffer()` to register MC's PBO as a CUDA resource
   - `cudaGraphicsMapResources()` + `cudaGraphicsResourceGetMappedPointer()` per frame
   - Exposes the mapped GPU pointer to Python via ctypes or a small pybind11 wrapper
   - `torch.as_tensor()` wraps the CUDA pointer as a PyTorch tensor (zero-copy)

2. **Java side changes**:
   - FrameGrabber exposes the PBO GL handle (already has it: `pbos[mapPbo]`)
   - JNI call or shmem-published PBO ID so the C library can register it
   - Alternatively: use a shared GL-CUDA texture instead of PBO

3. **Python side changes**:
   - Replace shmem frame reader with `interop.get_frame_tensor()` returning a CUDA tensor
   - Training loop consumes tensors directly, no numpy decode

### Expected Gains
- Eliminates ~0.4ms FrameGrabber shmem copy + ~0.3ms Python decode = ~0.7ms/step
- At current ~2.5ms/step, this is a ~28% reduction
- More importantly: frames never leave GPU VRAM, enabling higher resolutions without CPU bandwidth bottleneck

### Prerequisites
- Linux only (CUDA not available on macOS)
- MC must run with a real GL context (Xvfb on anvil)
- CUDA and GL contexts must share the same GPU
- Not compatible with VulkanMod (would need CUDA-Vulkan interop instead, similar API but `cudaImportExternalMemory`)

### Not Planned (for reference)
- CUDA-Vulkan interop: same concept but with `VK_EXTERNAL_MEMORY_HANDLE_TYPE_OPAQUE_FD_BIT` + `cudaImportExternalMemory`. Only needed if VulkanMod replaces Sodium on anvil.
- Metal compute on macOS: MPS (Metal Performance Shaders) for training. PyTorch MPS backend exists but is less mature than CUDA. Would need Metal-GL interop for frame capture which Apple doesn't support well.

## Machines

- **local/macbook**: MacBook Pro M4 Max, 36GB, macOS. Development + testing.
- **anvil**: Ryzen 9 9950X3D, 92GB DDR5, RTX 3090 24GB, Ubuntu 24.04. Training at scale.
  - Access: `ssh anvil` (Tailscale, VPN can be flaky)
  - Java 21 installed, CUDA 13.2 (for PyTorch only, not rendering)
  - Xvfb for headless GL


# Doc 40: `netherite-v2-bench/csrc/cuda_interop/README.md` {#doc-40}

*Absolute path: `/home/infatoshi/games/minecraft/netherite-v2-bench/csrc/cuda_interop/README.md`*

# csrc/cuda_interop

CUDA-GL interop layer for shipping agent observations directly from the JVM's
OpenGL framebuffer into a Python training process's CUDA tensor, with no CPU
hop.

## Status

**PoC validated on anvil (RTX 3090 + CUDA 13.2).** End-to-end chain proven:
160x90 RGBA buffer (57,600 bytes) round-tripped GL PBO -> CUDA -> IPC handle ->
separate process -> verified bit-exact.

Run output (anvil, Xorg :2 isolated to PCI:1:0:0):

```
[parent] PBO id=1, 57600 bytes loaded
[parent] device: NVIDIA GeForce RTX 3090 (CC 8.6, unifiedAddressing=1)
[parent] PBO mapped: devptr=0x75dafc3f1e00, size=57600
[parent] cudaIpcGetMemHandle ok
[child] verified 57600 bytes, OK
[parent] child exit=0
[parent] PoC PASSED
```

Not yet wired into `FrameGrabber.java` or `netherite_env.py`.

## Why

Current frame path: `GPU render -> PBO -> glMapBuffer (CPU) -> shmem (CPU) ->
Python mmap (CPU) -> numpy -> GPU (training)`.

Target frame path: `GPU render -> PBO -> cudaGraphicsResourceGetMappedPointer
(GPU) -> DtoD copy into IPC-able cudaMalloc buffer -> Python opens via
cudaIpcOpenMemHandle -> torch.as_tensor (still on GPU)`.

## Architecture constraint

`cudaIpcGetMemHandle` only works on memory allocated with `cudaMalloc`, NOT on
device pointers returned by `cudaGraphicsResourceGetMappedPointer`. This is why
we need an extra `cudaMemcpy(..., cudaMemcpyDeviceToDevice)` from the
GL-mapped PBO into a separate IPC-able buffer. The DtoD copy is essentially
free compared to the existing DtoH + shmem path.

## Build

Linux + CUDA only. `nvcc` and `libglfw3-dev` required.

```bash
cd csrc/cuda_interop
make poc
```

## Run (anvil)

```bash
DISPLAY=:2 csrc/cuda_interop/build/poc
```

Expected output: `[parent] PoC PASSED` (child exit 0).

## Implementation notes

- `cudaGraphicsResourceGetMappedPointer` returns a device pointer, but
  `cudaIpcGetMemHandle` rejects it. The handle must come from a separate
  `cudaMalloc`'d buffer; the GL-mapped pointer is only used as the source of
  a `cudaMemcpyDeviceToDevice` into that buffer. This costs one DtoD per
  frame (~zero overhead vs the existing GPU->CPU shmem path).
- The consumer must run in a process started via `exec` (not just `fork`).
  CUDA contexts do not survive a bare fork: `cudaSetDevice` after fork
  returns `cudaErrorInitializationError`. The PoC fork+execvp's itself with
  a `consumer` argv to model the real architecture (separate Python process).
- GLEW must be included before any header that pulls in `<GL/gl.h>` -
  `cuda_gl_interop.h` does, so `<GL/glew.h>` has to come first.


# Doc 41: `netherite/CLAUDE.md` {#doc-41}

*Absolute path: `/home/infatoshi/games/minecraft/netherite/CLAUDE.md`*

# Netherite: Minecraft 1.7.10 Minimal Oracle

## Project Overview

A stripped-down Minecraft 1.7.10 implementation targeting the "beat the game" critical path. This serves as a ground truth oracle for validating a future C/CUDA physics engine (MegaColonel) that will run thousands of parallel game instances for reinforcement learning.

## CRITICAL: Implementation Approach

**THIS IS A SUBTRACTION PROJECT, NOT A GREENFIELD BUILD.**

Do NOT write Minecraft from scratch. The MCP-decompiled 1.7.10 source already has working, correct implementations of physics, collision, entity AI, and world generation. We inherit that correctness by deleting what we don't need.

### Phase 1: MCP Environment Setup
1. Download MCP 9.08 (Mod Coder Pack for 1.7.10)
2. Obtain vanilla Minecraft 1.7.10 client and server jars
3. Run MCP decompilation process
4. Verify decompiled source compiles and runs

### Phase 2: Systematic Deletion
Remove systems we don't need, one at a time, verifying the game still works after each removal:

**Deletion Order (safest first):**
1. Decorative blocks (stained glass, flowers, carpets, etc.)
2. Command blocks, jukeboxes, note blocks
3. Brewing system (brewing stand, potions, cauldron)
4. Enchanting system (enchanting table, anvil, bookshelves logic)
5. Beacon system
6. Redstone system (wire, torches, repeaters, comparators, pistons, hoppers, droppers, dispensers)
7. Excluded entities (villagers, horses, wolves, ocelots, witch, slime, cave spider, iron golem, snow golem, wither)

After each deletion batch, run the game and verify:
- Game launches
- World generates
- Player can move, mine, craft
- Portals work
- Target mobs still spawn

### Phase 3: Oracle Instrumentation
Add recording/replay infrastructure to the stripped codebase:
1. Action recording (capture all player inputs with tick timestamps)
2. State export (dump world state in flat format)
3. Action replay (deterministic playback)
4. Validation (compare replayed state to original)

### Phase 4: Verification
1. Human plays through and beats the game on stripped version
2. Record the playthrough
3. Replay and verify byte-identical world state
4. Run oracle test suite

## Source Material

- **Base version**: Minecraft 1.7.10
- **Mappings**: MCP 9.08 (most thoroughly documented version)
- **MCP Download**: http://www.modcoderpack.com/ (or archived mirrors)
- **Reference**: `/mcp_1.7.10/` contains downloaded mappings and documentation

## Feature Scope

### KEEP (Critical Path)

**Biomes & Terrain**
- All biomes including oceans
- Caves, ravines
- Full procedural worldgen with fixed seed support

**Structures**
- Nether fortress (required for blaze)
- Stronghold (required for end portal)
- Dungeon, mineshaft, desert temple, jungle temple (incidental)

**Blocks (~80 types)**
- Stone, dirt, grass, sand, gravel, clay, obsidian
- All ores (coal, iron, gold, diamond, redstone, lapis, emerald)
- Crafting table, furnace, chest, ender chest
- Nether portal, end portal frame, end portal, end stone
- Nether brick, nether rack, soul sand, glowstone
- Mob spawner, ladder, fence, door, torch, TNT
- Water, lava, ice, snow, farmland, crops
- Bed, anvil, enchanting table (kept for structure gen)

**Entities (Critical Path)**
- Player, zombies, skeletons, creepers, spiders, endermen
- Blazes, ghasts, zombie pigmen, silverfish
- Ender dragon, ender crystals
- Items, XP orbs, falling blocks, TNT primed
- Arrows, snowballs, ender pearls, eyes of ender

**Systems**
- Crafting (all recipes for critical path items)
- Smelting (furnace recipes)
- Combat (melee, ranged, armor)
- Hunger/food
- Mob spawning (natural + spawner)
- Portal mechanics (nether + end)
- World generation (all biomes, structures)

### DELETE (Non-Critical)

- Decorative blocks, stained glass, flowers, carpets
- Command blocks, jukeboxes, note blocks
- Brewing system, potions, cauldron
- Beacon system
- Redstone system (wire, repeaters, pistons, hoppers, etc.)
- Villagers, horses, wolves, ocelots, witch, iron/snow golems, wither

## Current Status

**Phase 1**: COMPLETE - ForgeGradle 1.2 dev environment with MCP-decompiled 1.7.10 source
**Phase 2**: COMPLETE - Stripped server boots, loads all 3 dimensions
**Phase 3**: COMPLETE - Oracle instrumentation
**Phase 4**: IN PROGRESS - RL training pipeline

### Phase 3 Components

| Component | Status |
|-----------|--------|
| Action recording (OracleRecorder) | DONE |
| State export (OracleStateExporter) | DONE |
| Action replay (OracleReplay) | DONE |
| Validation (OracleValidator) | DONE |
| Determinism fixes | DONE |
| Checkpoint test system (10 checkpoints) | DONE |
| Vanilla client connection (FML handshake bypass) | DONE |
| Per-tick physics trace (OraclePhysicsTrace) | DONE |
| Config system (NetheriteConfig) | DONE |
| Live policy runner (OraclePolicyRunner) | DONE |
| Replay on player (startReplayOnPlayer) | DONE |

### Phase 4: RL Training Pipeline

| Component | Status |
|-----------|--------|
| CUDA batch environment (log-breaking task) | DONE |
| DDA voxel raycast (replaces brute-force lookDot) | DONE |
| Local grid search (replaces O(num_logs) scan) | DONE |
| PPO training (train_log.py) | DONE |
| Weight export to Java | DONE |
| Live MLP inference on Java server | DONE |
| Block breaking via world.rayTraceBlocks() | DONE |
| Replay trajectories on player | DONE |
| Proper aiming (policy converges to 4/5 logs) | NEEDS WORK |
| Theodolos GPU server restoration | DONE |
| Cross-platform Java 8 auto-detection (run.sh, build_oracle_patch_jar.sh) | DONE |
| NetHandler null-guards (headless bot teardown) | DONE |
| Policy/replay mutual stop | DONE |
| Deterministic policy spawn position | DONE |
| Strict pass loop orchestration (policy_pass_loop.sh) | DONE |
| Offline eval with terminal reward compensation | DONE |
| Reliable 5/5 log break in strict pass loop | NEEDS WORK |

### Performance Achieved
- Training: 14.3M rollout SPS, 6.2M effective SPS (small world, N=8000)
- 1 billion steps in 180 seconds
- Policy breaks 4/5 logs in ~326 ticks (~16s game time)
- 50x speedup from DDA raycast + small world + more envs
- Offline eval: 248/256 success (96.875%), mean_logs 4.9453, mean_ticks 560.13 (Feb 22, 2026)
- Strict pass loop: 3/3 FAIL at 4/5 logs, tick 2000 cap, `max_ticks_exceeded` (Feb 22, 2026)

### Checkpoint Test System

10 checkpoints covering the critical path:

| Checkpoint | Auto-test | Status |
|-----------|-----------|--------|
| water_bucket | PASS (tick 1) | Automated |
| fall_damage | PASS (tick 22) | Automated |
| mob_spawning | PASS (tick 2218) | Automated |
| nether_portal | Needs human | Setup verified |
| nether_fortress | Needs human | Setup verified |
| enderman_hunt | Needs human | Setup verified |
| stronghold | Needs human | Setup verified |
| crafting | Needs human | Setup verified |
| dragon_full | Needs human | Setup verified |
| dragon_1hp | Needs human | **Playtested -- credits reached** |

### Project State Snapshot (February 23, 2026)

**What is fixed**:
- Theodolos GPU server restored: `uv`, `.venv` (Python 3.13, PyTorch 2.5.1+cu121), CUDA lib build OK.
- `run.sh` / `build_oracle_patch_jar.sh` auto-detect Java 8 on Linux and macOS.
- Patch jar pipeline (`ORACLE_PATCH_JAR`) operational.
- Replay vs policy contention: `!policy` stops replay, `!replay` stops policy, ownership guards in NetHandler.
- `!policy` resets to deterministic spawn (-261.5, 67.0, -130.5) before first tick.
- Replay stop/cleanup API in `OracleReplay`.

**What is still broken**:
- Live policy deterministically stalls at 4/5 logs. Break ticks: ~288, ~508, ~1168, ~1470, then no 5th break before tick 2000.
- `rl_replay.nrec` (key600, 397 actions) is likely a 4-log trajectory. No canonical 5-log replay exists.
- CUDA offline eval shows 96.9% success but Java live policy fails 5/5 gate — possible obs mismatch.

**Known good artifacts**:
- Best weights: `csrc/cuda/runs/overfit/20260222-000204_n1024_h128_lr3e4_seed1337_fixedeval/weights_final.bin`
- Pass loop baseline: `run/passloop/20260222-192659/results.ndjson` (3/3 FAIL at 4/5)

**Remaining TODOs** (ordered):
1. Verify replay visual quality in-game (`!replay` after regen).
2. Verify live policy behavior in-game (`!policy` from fresh world).
3. Resolve deterministic 4/5 ceiling in live pass loop.
4. Produce a replay trajectory confirmed 5/5 logs; set as canonical `rl_replay.nrec`.
5. Re-run strict gate: `policy_pass_loop.sh --consecutive 3 --max-ticks 2000` must exit 0.

**Verification gates**:
- PASS criterion: 5/5 logs broken within <=2000 ticks, 3 consecutive runs.
- Offline gate: `eval_log_weights.py --pass-logs 5` must show >=95% success.
- Replay gate: `!replay` in-game must complete and break the expected number of logs.

Full state details and session startup checklist: `.cursor/rules/netherite_oracle_state.mdc`.

### Running Checkpoints

```bash
cd forge-workspace

# Auto-test (headless bot, exits with PASS/FAIL):
JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home \
  ORACLE_CHECKPOINT=water_bucket ORACLE_AUTOTEST=true \
  ./gradlew runServer --no-daemon

# Human playtest (connect vanilla 1.7.10 client to localhost:25570):
JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home \
  ORACLE_CHECKPOINT=dragon_1hp \
  ./gradlew runServer --no-daemon

# Run all 3 auto-testable checkpoints:
for cp in water_bucket fall_damage mob_spawning; do
  rm -rf run/world
  JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home \
    ORACLE_CHECKPOINT=$cp ORACLE_AUTOTEST=true \
    ./gradlew runServer --no-daemon
done
```

## Build System

### Manual Compilation (mc-src changes)

ForgeGradle only compiles mod code, not mc-src. Manual javac with Java 8 required.

**CRITICAL**: Do NOT use `-sourcepath mc-src` -- it pulls in stripped classes (Blocks.java missing fields) and crashes. Use a subset sourcepath with ONLY changed files. See AGENTS.md for the full compilation recipe.

**CRITICAL**: Must use Java 8 javac (`temurin-8.jdk`). System Java produces class files that ASM 5.0.3 can't read.

**CRITICAL**: Use Netty 4.0.10.Final (not 4.0.23). The 4.0.23 version has different `ChannelHandlerContext.read()` return type causing NoSuchMethodError at runtime.

**CRITICAL**: Always backup forgeSrc.jar before patching. Restore from backup before each recompile to avoid accumulating stale classes.

```bash
# Use the compile recipe in AGENTS.md -- it handles all edge cases
```

**CRITICAL**: log4j-2.0-beta9 must be FIRST on classpath. Use string concat in logger calls, NOT `{}` format patterns.

### Server Config

- Port: 25570 (`forge-workspace/run/server.properties`)
- online-mode: false (vanilla clients accepted)
- Seed: 42
- Max players: 2

## Key Technical Notes

- EntityPlayerMP.onUpdate() does NOT call super.onUpdate() -- server-side physics don't run for players (client-authoritative movement)
- `setBlock` flag 2 = client notify only; flag 3 = block update + client notify. Fluid flow requires flag 3
- `EntityPlayer.fall()` is protected -- use `attackEntityFrom(DamageSource.fall, damage)` from external code
- `setCurrentItemOrArmor(slot, stack)`: slot 0=held, 1=boots, 2=leggings, 3=chestplate, 4=helmet
- End island surface at (0,0) is ~y62-65. Spawn platforms must be at y=75+ to avoid suffocation
- Mob spawn exclusion: mobs can't spawn within 24 blocks of any player
- `transferPlayerToDimension` can crash if called during world init -- use 60-tick delay after player join
- `capabilities.disableDamage = true` implicitly enables flying -- must set `isFlying = false` and `allowFlying = false` every tick
- Headless bots need EmbeddedChannel + FML NetworkDispatcher pipeline or entity tracker NPEs (see AGENTS.md)
- Best approach for replay: apply actions to the HUMAN PLAYER, not a bot (avoids all networking issues)
- PLAYER_PHYS_INPUT (0x11): forward(f32) + strafe(f32) + yaw(f32) + pitch(f32) + jump(u8) = 17 bytes. Uses moveEntityWithHeading for physics.
- PLAYER_POS_LOOK (0x04): teleports (no physics). Only use for initial position, not replay.
- MC block breaking requires targeting specific coordinates. CUDA trains with DDA raycast, Java uses world.rayTraceBlocks()
- Training spawn position: (-261.5, 67.0, -130.5) on seed 42. Policy obs normalizer is calibrated for this region.

## Next Operator Runbook

### Server + Client Launch

```bash
cd forge-workspace
./run.sh                  # fresh world + server (port 25570)
./client.sh               # vanilla 1.7.10 client auto-connects
```

### In-Game Commands (type in MC chat)

- `!policy` — Start live MLP policy (breaks logs via raycast). Stops any active replay.
- `!replay` — Replay `rl_replay.nrec` on your player. Stops any active policy.

### Strict Pass Loop (Unattended)

```bash
cd forge-workspace
./policy_pass_loop.sh --hours 2 --consecutive 3 --max-ticks 2000 --no-client
# Results: run/passloop/<timestamp>/results.ndjson
# Exit 0 = PASS, exit 1 = FAIL
```

### Offline Checkpoint Evaluation (Theodolos)

```bash
ssh theodolos "cd ~/netherite && \
  uv run csrc/cuda/python/eval_log_weights.py \
  --weights policy_weights.bin --nsta state_small.nsta \
  --episodes 256 --pass-logs 5"
```

### Training (Theodolos)

```bash
ssh theodolos "cd ~/netherite && \
  PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True \
  uv run csrc/cuda/python/train_log.py \
  --nsta state_small.nsta \
  --N 1024 --iterations 1000 --horizon 128 --max-ticks 2048 --lr 3e-4 \
  --checkpoint-interval 50 --checkpoint-dir checkpoints \
  --export-weights policy_weights.bin"
```

### Replay Regeneration (Theodolos)

```bash
# Export best 5-log trajectory, convert to NREC
ssh theodolos "cd ~/netherite && \
  uv run csrc/cuda/python/eval_log_weights.py \
  --weights policy_weights.bin --nsta state_small.nsta \
  --episodes 256 --pass-logs 5 --export-traj best_5log_traj.json && \
  uv run csrc/cuda/python/convert_traj_to_nrec.py \
  best_5log_traj.json rl_replay.nrec"
# Then scp to local:
scp theodolos:~/netherite/rl_replay.nrec forge-workspace/run/saved/rl_replay.nrec
```


# Doc 42: `netherite/GRAALVM_FEASIBILITY.md` {#doc-42}

*Absolute path: `/home/infatoshi/games/minecraft/netherite/GRAALVM_FEASIBILITY.md`*

# GraalVM Native-Image Feasibility for MC 1.7.10 Headless Server

**Date:** 2026-03-03
**Goal:** Compile MC 1.7.10 dedicated server into a native binary to eliminate JVM overhead for running many parallel instances (RL training).

---

## Executive Summary

**Recommendation: Skip native-image. Use JVM with tuned flags instead.**

Native-image compilation of MC 1.7.10 is technically possible but the effort-to-reward ratio is poor for the RL training use case. The 3-5 day engineering effort yields ~40% memory savings per instance, but the same savings (and more) can be achieved by tuning JVM flags (`-Xmx`, `-Xms`, `-XX:+UseSerialGC`, `-XX:MaxMetaspaceSize`) at zero engineering cost. The real bottleneck for parallel instances is world simulation CPU time, not JVM overhead.

If you still want to pursue it, the path is viable -- MC 1.7.10 is actually simpler than modern MC versions where this has already been demonstrated (1.18.2, 1.21.3). But the ROI is negative for this project.

---

## Phase 1: GraalVM Native-Image Overview

### Current Version
- **Latest:** GraalVM 25.0.2 (January 2026, JDK 25)
- **LTS:** GraalVM for JDK 21
- **Download:** `https://download.oracle.com/graalvm/21/latest/graalvm-jdk-21_linux-x64_bin.tar.gz`
- Also available via SDKMAN: `sdk install java 21.0.x-graal`

### Java 8 Bytecode Compatibility
JDK 21/25 can execute class files compiled for Java 6/7/8. `native-image` accepts these class files. **However**, the runtime library is JDK 21/25, not Java 8. If MC 1.7.10 uses removed internal APIs (`sun.reflect.ReflectionFactory`, etc.), those break regardless of native-image -- it is a JDK version problem.

MC 1.7.10 + Forge uses `sun.reflect.ReflectionFactory` in `EnumHelper.java` (3 files). This would need to be patched or removed.

### Limitations Summary

| Feature | Status | Impact on MC 1.7.10 |
|---|---|---|
| Reflection | Requires config or code changes | CRITICAL -- core object creation pattern |
| Dynamic class loading | Not supported at runtime | CRITICAL -- Forge mod loader (strippable) |
| JNI | Supported with config | NONE -- server has no LWJGL |
| Serialization | Supported with config | NONE -- MC uses NBT, not Java serialization |
| sun.misc.Unsafe | Supported with caveats | NONE -- zero usage in codebase |
| Finalizers | Not invoked | LOW -- old code may use them |
| InvokeDynamic/Lambdas | Supported for javac-generated | NONE -- Java 6/7 target |
| Threads | Fully supported | NONE -- standard threading |

### Reachability Metadata System
Configuration files (`reflect-config.json`, `jni-config.json`, `resource-config.json`, etc.) declare what classes/methods/fields are accessed reflectively. Placed in `META-INF/native-image/` or passed via `-H:ConfigurationFileDirectories=`.

### native-image-agent
Run the app with `-agentlib:native-image-agent=config-output-dir=/path/` to automatically capture reflection/JNI/resource access. Coverage depends on exercised code paths -- multiple runs with `config-merge-dir` recommended.

### Build Characteristics
- Build time: 10-20 minutes for complex apps
- Build RAM: 16-32GB recommended
- Output size: 50-200MB typical
- GC options: Serial (CE+Oracle), G1 (Oracle only), Epsilon

---

## Phase 2: Prior Art

### Confirmed Working
| Project | MC Version | Status | Notes |
|---|---|---|---|
| hpi-swa/native-minecraft-server | 1.18.2 | Demo quality | 43% memory reduction, occasional startup failures |
| PaperMC native-image | 1.21.3 | Working | Required extensive tracing with actual client connections |
| MCNativeBuilder | 1.20.4 | Alpha | Automated build tool |

### Confirmed Failed
| Project | Status | Why |
|---|---|---|
| Spigot 1.15.2 | Dead end | Plugin dynamic loading, AWT/DND class init failures |
| MC Client (any version) | No confirmed working build | LWJGL/OpenGL rendering pipeline too complex |
| Any modded server | Not feasible | Dynamic mod loading is fundamentally incompatible |

### Key Takeaway
Vanilla/unmodded MC servers CAN be compiled. The hpi-swa project for 1.18.2 proves it. MC 1.7.10 is architecturally simpler (fewer abstractions, smaller codebase) and should be easier, not harder.

### Dependencies Status
| Library | native-image Support |
|---|---|
| Netty | Supported since 4.1.36 (2019), needs thorough metadata |
| Log4j2 | Supported since 2.25.0 |
| Gson | Workable with reflection config |
| LWJGL | Not needed for server |

---

## Phase 3: MC 1.7.10 Source Analysis

Source analyzed: `~/netherite/legacy/mc-src/` (1715 Java files)

### Server/Client Separation: CLEAN
- 339 files are client-only (`net.minecraft.client.*`)
- 139 files import LWJGL -- 135 are in client packages
- 4 non-client LWJGL files are all `@SideOnly(Side.CLIENT)` annotated
- `DedicatedServer.java` has zero LWJGL imports
- 1614 `@SideOnly` annotations enforce the split

**The dedicated server runs with zero LWJGL/OpenGL/OpenAL dependency.** This eliminates the hardest native-image challenge.

### Reflection Usage: CRITICAL but BOUNDED

The core incompatibility is the **reflective object factory pattern** used by 5 registration systems:

| System | File | Pattern | Classes to Register |
|---|---|---|---|
| Entities | `EntityList.java` | `HashMap<String, Class>` + `.getConstructor(World.class).newInstance()` | ~80 entity types |
| Tile Entities | `TileEntity.java` | `HashMap<String, Class>` + `.newInstance()` | ~20 types |
| Packets | `EnumConnectionState.java` | `BiMap<Integer, Class>` + `.newInstance()` | ~65 server-bound |
| Structures | `MapGenStructureIO.java` | `HashMap<String, Class>` + `.newInstance()` | ~15 types |
| Map Storage | `MapStorage.java` | `.getConstructor(String.class).newInstance()` | ~5 types |

Total: ~41 `Class.forName()` calls across ~28 files, ~105 reflective instantiations across ~30 files. Of these, ~15 files are server-relevant.

**Two fix paths:**
- **Path A (reflect-config.json):** Enumerate ~120 classes in metadata. Tedious but mechanical. ~2 hours.
- **Path B (code refactoring):** Replace `HashMap<String, Class>` with `HashMap<String, Supplier<T>>`. Eliminates reflection from hot path. ~6-8 files to modify. ~4 hours.

### Dynamic Class Loading: CRITICAL but STRIPPABLE

3 files with custom ClassLoaders, all in Forge:
- `ModClassLoader.java` -- Extends `URLClassLoader` (mod JAR loading)
- `ASMEventHandler.java` -- Runtime bytecode generation via ObjectWeb ASM
- `CoreModManager.java` -- Reflective classpath manipulation

**Since we have the decompiled source with Forge patches already applied, the entire Forge mod loading pipeline can be removed.** We only need the registry infrastructure (`GameData`, `GameRegistry`), not the dynamic discovery/loading system.

The `ASMEventHandler` runtime bytecode generation must be replaced with a static event dispatch mechanism or direct method calls. This is the single hardest piece of work.

### Static Mutable State: HIGH but Manageable

All registries are static mutable HashMaps populated during class initialization:
```
EntityList: 6 static maps
TileEntity: 2 static maps
Block/Item: static final RegistryNamespaced (mutable contents)
EnumConnectionState: BiMap per state
MinecraftServer: mutable singleton
```

For native-image: mark these with `--initialize-at-run-time` to defer population to runtime. This is configuration, not code changes.

For **multiple isolated instances in one binary**: this is the real problem. All these static fields mean the entire server is a singleton. Running N instances in one process requires either:
1. N separate processes (defeats the purpose)
2. Refactoring all static state into instance state (massive effort, 100+ hours)
3. Using `ProcessBuilder` to fork N copies of the native binary (works, each ~60% RAM of JVM)

### Other Findings
- **sun.misc.Unsafe:** 0 occurrences. Clean.
- **Java serialization:** 1 `implements Serializable` (Forge only). MC uses NBT exclusively.
- **NBT system:** Hardcoded switch statement, NOT reflective. Fully compatible.
- **Threading:** Standard `java.util.concurrent`, `synchronized` blocks. Fully compatible.
- **Forge `EnumHelper`:** Uses `sun.reflect.ReflectionFactory` (removed in JDK 21). Must be patched or removed.

---

## Phase 4: native-image-agent Tracing Plan

If proceeding despite the recommendation to skip:

### Step 1: Install GraalVM
```bash
sdk install java 21.0.6-graal
sdk use java 21.0.6-graal
```

### Step 2: Verify MC 1.7.10 Runs on JDK 21
Before native-image, confirm the server JAR even starts on JDK 21. Expected issues:
- `sun.reflect.ReflectionFactory` usage in `EnumHelper` -- patch or remove
- Removed `javax.xml` APIs (bundled in Java 8, module in Java 11+)
- SecurityManager deprecation warnings
- Any `--illegal-access` issues with internal API usage

```bash
java -jar minecraft_server.1.7.10.jar --nogui
```

### Step 3: Run with Tracing Agent
```bash
java -agentlib:native-image-agent=config-merge-dir=native-config/ \
     -jar minecraft_server.1.7.10.jar --nogui
```

Run multiple sessions:
1. Start server, let it generate spawn chunks, shut down
2. Start server, connect a client, walk around, break/place blocks, kill mobs
3. Start server, trigger redstone, open chests, use furnaces
4. Start server, go to nether, generate nether terrain

Each session merges metadata into `native-config/`.

### Step 4: Attempt Build
```bash
native-image -jar minecraft_server.1.7.10.jar \
    -H:ConfigurationFileDirectories=native-config/ \
    --no-fallback \
    --initialize-at-run-time=net.minecraft.server.MinecraftServer \
    --initialize-at-run-time=net.minecraft.entity.EntityList \
    --initialize-at-run-time=net.minecraft.tileentity.TileEntity \
    -o mc-server
```

### Step 5: Expected Failures
1. `sun.reflect.ReflectionFactory` not found -- patch `EnumHelper`
2. Missing reflection metadata for entity/tile/packet classes not exercised during tracing
3. Class initialization ordering issues with static registries
4. `ASMEventHandler` bytecode generation failure -- must be replaced
5. Netty reflection/unsafe access metadata gaps

---

## Effort Estimates

| Task | Effort | Severity |
|---|---|---|
| Get MC 1.7.10 running on JDK 21 | 4-8 hours | Prerequisite |
| Strip Forge mod loading system | 8-16 hours | Critical |
| Replace ASMEventHandler with static dispatch | 4-8 hours | Critical |
| Generate reflection metadata via tracing agent | 2-4 hours | Critical |
| Fix remaining build failures iteratively | 8-16 hours | Critical |
| Verify server functionality | 4-8 hours | Required |
| **Total** | **30-60 hours (4-8 days)** | |

---

## Memory Estimates

### Per-Instance RAM

| Configuration | Estimated RSS | Notes |
|---|---|---|
| JVM default (`-Xmx1G`) | 600-900 MB | Includes JIT compiler, class metadata, GC overhead |
| JVM tuned (`-Xmx256m -Xms256m -XX:+UseSerialGC -XX:MaxMetaspaceSize=64m`) | 350-450 MB | Minimum viable for small worlds |
| native-image (Serial GC) | 200-350 MB | No JIT, no class metadata overhead |
| native-image (G1 GC, Oracle only) | 250-400 MB | Better pause times, slightly higher baseline |

### Scaling to N Instances

| Instances | JVM Tuned | native-image | Savings |
|---|---|---|---|
| 10 | 3.5-4.5 GB | 2.0-3.5 GB | ~30-40% |
| 50 | 17-22 GB | 10-17 GB | ~30-40% |
| 100 | 35-45 GB | 20-35 GB | ~30-40% |

Note: native-image binary shares read-only pages across processes via OS page cache (code, constant data). JVM also shares some pages (libjvm.so, JIT code cache is per-process). The per-instance savings are real but not transformative.

### The Static State Problem

The singleton architecture (all registries in static fields) means you **cannot run multiple MC instances in a single process** without massive refactoring (100+ hours). Each instance must be a separate OS process regardless of JVM vs native-image. This makes the memory comparison process-to-process, where native-image's advantage is moderate.

---

## Hard Blockers

None that are truly impossible. But:

1. **JDK 8 to 21 migration** -- This must be done first, independent of native-image. MC 1.7.10 + Forge uses internal APIs removed in later JDKs. This is 4-8 hours of work and is the gating prerequisite.

2. **ASM bytecode generation in Forge event system** -- `ASMEventHandler` generates JVM bytecode at runtime. Completely incompatible with native-image. Must be replaced with static dispatch. This is solvable but is the hardest single task (~4-8 hours).

---

## Soft Blockers

1. **Reflective object factories** -- 5 registration systems use `Class.newInstance()`. Solvable via reflect-config.json (2 hours) or code refactoring to factory lambdas (4 hours).

2. **Forge mod loading pipeline** -- Must be gutted. Since we have decompiled source with patches applied, we keep the patches and remove the dynamic loading infrastructure (~8-16 hours).

3. **Netty metadata completeness** -- The tracing agent may not capture all Netty reflection paths on first pass. Iterative tracing with actual network traffic needed (~2-4 hours).

4. **`EnumHelper` using `sun.reflect.ReflectionFactory`** -- Remove or rewrite. Since we control the source and don't need runtime enum extension, just delete it (~1 hour).

5. **Static initialization ordering** -- Some classes have circular static initialization dependencies that work on JVM (lazy classloading) but fail with native-image (build-time initialization). Requires `--initialize-at-run-time` flags. Discovered iteratively during build attempts (~2-4 hours).

---

## Alternative Approaches (Recommended)

### Option 1: Tuned JVM (Zero Engineering Effort)
```bash
java -Xmx256m -Xms256m \
     -XX:+UseSerialGC \
     -XX:MaxMetaspaceSize=64m \
     -XX:+UseCompressedOops \
     -XX:+UseCompressedClassPointers \
     -jar minecraft_server.1.7.10.jar --nogui
```
Expected: 350-450 MB per instance. 96 GB system can run ~200 instances.

### Option 2: CRaC (Coordinated Restore at Checkpoint)
Checkpoint a warmed-up JVM and restore from snapshot. Near-instant startup, keeps JIT optimizations, no reflection issues. Available in Azul Zulu and OpenJDK builds. Much less invasive than native-image.

### Option 3: Shared Class Data (AppCDS)
JDK's Application Class Data Sharing reduces startup time and memory by sharing class metadata across instances.
```bash
# Generate class list
java -XX:DumpLoadedClassList=classes.lst -jar minecraft_server.jar --nogui
# Create shared archive
java -Xshare:dump -XX:SharedClassListFile=classes.lst -XX:SharedArchiveFile=mc.jsa -jar minecraft_server.jar
# Run with shared archive
java -Xshare:on -XX:SharedArchiveFile=mc.jsa -jar minecraft_server.jar --nogui
```

### Option 4: GraalVM JIT (Not native-image)
Use GraalVM as a drop-in JVM replacement for better JIT compilation. No code changes needed. Potential 10-20% throughput improvement.

---

## Final Recommendation

**Skip native-image for this project.** The reasons:

1. **30-60 hours of engineering** for ~30-40% memory savings per instance.
2. **The static singleton architecture** means each instance is a separate process regardless -- you cannot multiplex instances in a single native binary without rewriting the entire server architecture.
3. **Tuned JVM flags** get you to 350-450 MB per instance at zero cost. On 96 GB (theodolos), that is 200+ instances.
4. **The real bottleneck** for RL training is simulation speed (tick rate), not JVM overhead. Each MC tick is 50ms of game logic. Making the JVM faster at startup doesn't make ticks faster.
5. **Maintenance burden** -- every time you modify the MC server code (which you will, heavily, for RL integration), you need to re-trace and potentially fix native-image configuration.

If memory becomes the actual bottleneck after profiling with tuned JVM flags, revisit this analysis. The path is viable but the ROI is negative at current scale.


# Doc 43: `netherite/README.md` {#doc-43}

*Absolute path: `/home/infatoshi/games/minecraft/netherite/README.md`*




# Doc 44: `netherite/SPEC.md` {#doc-44}

*Absolute path: `/home/infatoshi/games/minecraft/netherite/SPEC.md`*

# Netherite SPEC

Bare-metal Minecraft 1.7.10 for reinforcement learning. C++ and CUDA. No OpenGL, no JVM, no networking. Just physics, rendering via CUDA raycasting, and a Python interface for RL training.

## Project History

This is the continuation of the Netherite project (now "legacy Netherite"), which took the subtraction approach: MCP-decompile Minecraft 1.7.10, strip non-essential systems, instrument with oracle recording/replay/validation, then build a CUDA batch RL environment on top.

Legacy Netherite achieved:
- Stripped Java oracle with deterministic record/replay
- C engine reimplementing core physics (4,469 lines)
- CUDA batch env hitting 14.3M rollout SPS on RTX 3090
- PPO training that breaks 4/5 logs, dragon checkpoint playtested to end credits
- 10-checkpoint validation system

This iteration ports the entire game to C++/CUDA as a standalone binary. The Java oracle at `~/netherite` (legacy) remains as the ground truth reference for validation.

## Goals

1. **Tick-perfect C++ Minecraft** -- a standalone game engine matching MC 1.7.10 physics exactly for the kept subsystems. Validated against the Java oracle via NSTA state snapshots and NREC action replays.

2. **CUDA voxel renderer** -- raycasting-based frame generation at 64x64 or 256x256. No OpenGL. Runs on GPU alongside the game state. Produces RGB frames as RL observations.

3. **Massively parallel RL** -- thousands of game instances on a single GPU. Each CUDA block runs one game instance. Observations are either raw frames (CNN policy) or structured state (MLP policy).

4. **Human-playable C++ client** -- the same binary can be run in interactive mode with keyboard/mouse input and a window displaying CUDA-rendered frames. This gives direct performance profiling of what is CPU-bound vs GPU-bound.

5. **Verifiability** -- any trained policy can export its trajectory as NREC, replay in the Java oracle, and produce byte-identical world state. This is the hard guarantee that the C++ engine matches Minecraft.

## Architecture

```
                    +---------------------------------+
                    |     Java Oracle (legacy)         |
                    |     ~/netherite                  |
                    |     ground truth validation      |
                    +---------------+-----------------+
                                    |
                          NSTA snapshots + NREC replays
                                    |
                    +---------------v-----------------+
                    |     C++ Engine (CPU)             |
                    |     tick-perfect game logic      |
                    |     single instance, full game   |
                    +-----+-------------------+-------+
                          |                   |
                __host__ __device__     human play mode
                    shared source              |
                          |              +-----v-------+
                    +-----v---------+    | Window      |
                    | CUDA Batch    |    | (GLFW/SDL)  |
                    | N instances   |    | displays    |
                    | parallel tick |    | CUDA frame  |
                    +-----+---------+    +-------------+
                          |
                    +-----v---------+
                    | CUDA Renderer |
                    | DDA raycasting|
                    | per instance  |
                    +-----+---------+
                          |
                    +-----v---------+
                    | Python API    |
                    | pybind11      |
                    | gym-like env  |
                    +---------------+
```

### No Client/Server Split

Vanilla Minecraft runs a server thread (20 TPS) and a client thread (uncapped FPS) communicating over packets. We delete all of that. There is one process, one world state, and two modes:

**RL mode (training):**
```
for each tick:
    actions = policy(observations)       // from Python or fused CUDA kernel
    world.tick(actions)                  // C++ physics, entities, blocks
    observations = render(world)         // CUDA DDA raycasting -> RGB tensor
    reward = compute_reward(world)       // extractable reward functions
```

Ticks and frames are 1:1. No interpolation. Synchronous. This is the fast path.

**Human play mode (verification/profiling):**
```
while running:
    if time_for_tick():                  // 20 TPS
        actions = read_input()           // keyboard/mouse
        world.tick(actions)

    t = interpolation_factor()           // 0.0-1.0 between ticks
    frame = render(world, t)             // lerp entity positions for smooth visuals
    display(frame)                       // blit CUDA surface to window
```

Decoupled tick and render rates. Entity positions lerped for smooth display. This is for humans playing/watching the agent.

## Game Scope

### Blocks (~90 types)

**KEEP:**

Terrain/Natural: stone (all variants), cobblestone, mossy cobblestone, dirt, grass, mycelium, sand, gravel, clay, sandstone, bedrock, obsidian, netherrack, soul sand, end stone, ice, packed ice, snow layer, snow block, farmland

Wood/Plant: logs (all 6), planks (all 6), leaves (all), saplings (all), vines, lily pad, sugar cane, cactus, cocoa, melon block, pumpkin, wheat crop, carrot crop, potato crop, nether wart

Ores/Minerals: coal ore, iron ore, gold ore, diamond ore, redstone ore, lapis ore, emerald ore + their storage blocks

Functional: crafting table, furnace, chest, ender chest, trapped chest, brewing stand, cauldron, anvil, enchanting table, bookshelf, bed, mob spawner, TNT, beacon

Structural: cobweb, glass, glass pane, fence (all), fence gate, door (wood + iron), trapdoor, ladder, slabs (all), stairs (all), iron bars, wall (cobble + mossy)

Portal/End: nether portal, end portal frame, end portal, dragon egg

Nether: nether brick, nether brick fence, nether brick stairs, glowstone

Fluid: water (source + flowing), lava (source + flowing)

Light: torch, jack o'lantern

**REMOVE:**

- Redstone: wire, torch, repeater, comparator, piston, sticky piston, hopper, dropper, dispenser, daylight sensor, pressure plates (weighted), tripwire
- Rails: all rail types, all minecart types
- Decorative: stained glass (16), stained clay (16), carpet (16), flower pots, skulls, banners
- Flowers: all flower types (purely cosmetic)
- Music: note block, jukebox
- Command: command block
- Misc: sponge (non-functional in 1.7.10), dead bush, double plants

### Entities

**KEEP:**

Hostile (critical path): zombie, skeleton, creeper, spider, cave spider, enderman, silverfish, blaze, ghast, zombie pigman, witch, ender dragon, ender crystal

Passive/Utility: cow, pig, sheep, chicken (food), villager (trading), iron golem (village defense)

Projectiles/Items: arrow, snowball, ender pearl, eye of ender, thrown potion, XP orb, item entity, falling block, primed TNT, fireball (ghast), small fireball (blaze)

**REMOVE:**

- Mobs: horse, donkey, mule, wolf, ocelot/cat, bat, squid, mooshroom, snow golem, wither, wither skeleton
- Vehicles: boat, all minecart variants
- Decorative: painting, item frame
- Misc: lightning bolt entity (no weather system)

### Systems

**KEEP:**

| System | Notes |
|--------|-------|
| World generation | All biomes, caves, ravines, structures (nether fortress, stronghold, village, dungeon, mineshaft, temples) |
| Chunk loading/unloading | Small radius for RL (3-5 chunks), larger for human play |
| Block tick scheduling | Random ticks (crops, fire spread), scheduled updates (fluid flow) |
| Entity tick loop | Movement, AI, pathfinding, collision |
| Player physics | moveEntityWithHeading, AABB collision, gravity, swimming |
| Crafting | Full recipe registry for critical path items |
| Smelting | Furnace recipes, fuel values |
| Brewing | Potion recipes, brewing stand ticking |
| Enchanting | Enchantment table, anvil combining, enchantment effects |
| Experience | XP orbs, level calculation |
| Combat | Melee damage, armor reduction, knockback, arrows, potion effects |
| Hunger/food | Saturation, exhaustion, natural healing |
| Mob spawning | Natural spawning algorithm, mob spawner blocks |
| Mob AI | Goal-based AI, A* pathfinding on voxel grid |
| Villager trading | Trade offers, emerald economy |
| Portal mechanics | Nether portal search/creation, end portal activation |
| Fluid flow | Water/lava finite state automaton (level 0-7, spread rules) |
| Block breaking | Hardness, tool multipliers, break progress timer |
| Inventory | Player inventory, container interaction (chest, furnace, etc.) |
| Dragon fight | End island, crystal healing, dragon AI phases, end credits |
| Day/night cycle | Time of day, affects mob spawning via light level |
| Light propagation | Block light + sky light, needed for mob spawn checks |
| Fire spread | Flammability, random tick fire propagation |
| Falling blocks | Sand/gravel entity conversion |

**REMOVE:**

| System | Why safe |
|--------|----------|
| Redstone | Zero coupling if all redstone blocks removed |
| Weather | Rain, thunder, snow accumulation, lightning |
| Scoreboard | Multiplayer-only |
| Achievements | Tracking only, no gameplay effect |
| Statistics | Tracking only |
| Map items | No gameplay effect |
| Sound | Entire audio engine |
| Particles | Entire particle system |
| Networking | Replaced by direct C++ method calls |
| GUI/HUD rendering | Replaced by CUDA renderer (minimal HUD if needed) |
| Resource pack loading | Keep raw PNG textures, delete the loading pipeline |
| World save/load to disk | Generate worlds in memory, use NSTA for snapshots |
| Chat/commands | Replace with direct API calls |
| World border | Multiplayer feature |
| Difficulty scaling | Use fixed difficulty |

## RNG Strategy

Vanilla MC scatters `java.util.Random` (48-bit LCG) instances across World.rand, per-entity randoms, and world gen randoms. Call order depends on runtime state, making cross-instance determinism fragile.

**Our approach: hash-based deterministic RNG.**

```cpp
// Same inputs -> same output, regardless of execution order
uint32_t det_random(int tick, int x, int z, int purpose) {
    uint64_t h = tick * 6364136223846793005ULL
               + x * 1442695040888963407ULL
               + z * 2862933555777941757ULL
               + purpose * 3037000493ULL;
    h ^= (h >> 17);
    h *= 0xbf58476d1ce4e5b9ULL;
    h ^= (h >> 31);
    return (uint32_t)(h & 0x7FFFFFFF);
}
```

Benefits:
- Deterministic: same (tick, position, purpose) always gives same result
- Order-independent: doesn't matter if chunk A processes before chunk B
- GPU-parallel: no sequential LCG state to maintain
- Validation: our oracle and parallel engine match each other exactly

Tradeoff: mob spawning patterns won't match vanilla MC. We don't care. We care that OUR C++ oracle and OUR CUDA batch engine produce identical results, and that player physics/actions replay correctly.

For world generation specifically, we replicate Java's LCG exactly (it's trivial) so that world seeds produce the same terrain as vanilla MC. This allows replaying in real MC for visual verification.

## Floating Point

All C++ code compiled WITHOUT `-ffast-math`. IEEE 754 strict throughout. The Java oracle uses `strictfp` on all classes.

Dangerous spots requiring exact matching:
- `Entity::moveEntity()` -- collision detection, double arithmetic
- Block breaking progress calculation
- Explosion damage/knockback
- Arrow/projectile trajectory
- Fall damage calculation

Every `(int)` cast in Java maps to a specific C++ truncation. Document these at each boundary. Java truncates toward zero and clamps NaN to 0; C++ `static_cast<int>` truncates toward zero but NaN is undefined -- use explicit clamping.

## CUDA Renderer

DDA voxel raycasting. The algorithm already exists in `cuda_policy.cu` for block breaking -- extend it to produce RGB output.

```
Per pixel:
  1. Compute ray origin (camera pos) + direction (yaw/pitch + pixel offset)
  2. DDA march through voxel grid (existing algorithm)
  3. On hit: block type -> texture atlas -> sample texel (16x16 per face)
  4. Simple lighting: ambient + directional sun, no smooth lighting
  5. Write RGB to output buffer
```

**Texture atlas:** ~90 block types * 6 faces * 16x16 * 3 bytes = ~155 KB. Fits in constant memory.

**Resolution tiers:**
- 64x64: 4,096 rays per frame. Negligible overhead. Good for fast RL training.
- 128x128: 16,384 rays. Still trivial.
- 256x256: 65,536 rays. ~20-30 DDA steps each = ~2M total steps. Fast on any modern GPU.

**Batch rendering (RL mode):** 1000 envs * 65,536 rays = 65M rays. Well within a 3090's throughput for simple DDA with no branching complexity.

**Human play mode:** Render one world at higher resolution (720p/1080p), blit CUDA surface to a window via GLFW or SDL. No OpenGL shaders needed -- just a pixel buffer copy.

## Observation Space

Two observation modes, selectable per training run:

### Mode 1: Structured (MLP policy)
The existing 26-float observation vector from legacy Netherite, extended:
- Player state: pos(3), velocity(3), yaw(1), pitch(1), onGround(1)
- Nearest target info: direction(2), distance(1), dy(1), look_dot(1)
- Break progress(1), target yaw/pitch(2)
- Task progress(1)
- Heightmap ring(8): 8 compass directions
- **New:** health(1), hunger(1), held_item_id(1), armor_value(1)

### Mode 2: Visual (CNN policy)
CUDA-rendered frames as RGB tensors:
- **3D local grid:** Small cube around player (e.g., 31x31x11 block IDs)
- **2D heightmap:** Disk around player (e.g., 31x31)
- **RGB frame:** 64x64x3 or 256x256x3 from CUDA renderer
- **Scalar features:** health, hunger, XP, held item, etc.

The CNN architecture from legacy Netherite (Conv3D on grid + Conv2D on heightmap + FC on scalars) extends naturally to include the rendered frame as an additional Conv2D input.

## Action Space

Continuous+discrete hybrid, matching legacy Netherite:

| Action | Type | Range | MC Equivalent |
|--------|------|-------|---------------|
| forward | continuous | tanh [-1, 1] | W/S key analog |
| strafe | continuous | tanh [-1, 1] | A/D key analog |
| yaw | continuous | raw * 10.0 deg | mouse X |
| pitch | continuous | clamp(raw * 5.0, -90, 90) | mouse Y |
| jump | discrete | binary (> 0) | spacebar |
| attack | discrete | binary (> 0) | left click |
| **New: use** | discrete | binary (> 0) | right click |
| **New: slot** | discrete | 0-8 | hotbar selection |
| **New: craft** | discrete | recipe ID or 0 | crafting action |

The `use` action enables placing blocks, eating food, throwing ender pearls, drinking potions. The `craft` action is a direct recipe ID rather than simulating GUI clicks -- the game applies the recipe if the player has ingredients.

## Reward Functions

Pluggable reward modules. Each is a C++ function `float reward(const World& prev, const World& curr, const Action& action)`:

| Reward | Description |
|--------|-------------|
| `reward_log_break` | +1 per log broken (existing, validated) |
| `reward_craft` | +R per item crafted, scaled by recipe complexity |
| `reward_smelt` | +R per item smelted |
| `reward_kill` | +R per mob killed, scaled by mob difficulty |
| `reward_health_delta` | Negative reward for taking damage |
| `reward_food` | +R for eating, preventing starvation |
| `reward_portal` | Large +R for entering nether, entering end |
| `reward_dragon` | Very large +R for killing ender dragon |
| `reward_explore` | +R for visiting new chunks |
| `reward_xp` | +R proportional to XP gained |

Rewards are composable. Training scripts specify which modules to activate and their weights.

## File Structure

```
~/netherite/                          # Project root
  SPEC.md                            # This file
  CLAUDE.md                          # Agent development guide
  CMakeLists.txt                     # Build system

  legacy/                            # Reference: old Java oracle + docs
    mc-src/                          # MCP-decompiled stripped Java
    forge-workspace/                 # ForgeGradle project
    CLAUDE.md                        # Legacy agent guide
    AGENTS.md                        # Legacy development guide
    TESTING.md                       # Legacy test procedures

  engine/                            # C++ game engine
    src/
      world.cpp                      # World state, chunk management
      world_gen.cpp                  # Terrain generation, biomes, structures
      tick.cpp                       # Main tick loop
      player.cpp                     # Player physics, inventory
      entity.cpp                     # Entity base, physics
      mob_*.cpp                      # Per-mob-type AI and behavior
      block.cpp                      # Block types, properties, interactions
      block_tick.cpp                 # Random ticks, scheduled updates
      fluid.cpp                      # Water/lava flow FSM
      collision.cpp                  # AABB collision detection
      combat.cpp                     # Damage, armor, knockback
      crafting.cpp                   # Recipe registry, crafting logic
      brewing.cpp                    # Potion recipes, brewing stand
      enchanting.cpp                 # Enchantment table, anvil
      spawn.cpp                      # Natural mob spawning
      pathfinding.cpp                # A* on voxel grid
      light.cpp                      # Block light + sky light propagation
      portal.cpp                     # Nether/end portal mechanics
      dragon.cpp                     # Ender dragon fight
      rng.cpp                        # Hash-based deterministic RNG
      nsta.cpp                       # State snapshot I/O
      nrec.cpp                       # Action recording I/O
    include/
      *.h                            # Headers for all engine modules

  cuda/                              # CUDA kernels
    src/
      batch_env.cu                   # N-instance parallel environment
      batch_tick.cu                  # Parallel game tick kernel
      renderer.cu                    # DDA voxel raycasting -> RGB
      policy.cu                      # Fused policy inference + env step
      device_init.cu                 # World upload to GPU
      host_api.cu                    # Host-device interface
      observation.cu                 # Observation extraction
      player_physics.cu              # GPU player physics
      collision.cu                   # GPU collision detection
    include/
      device_types.h                 # GPU memory structures
      cuda_policy.h                  # Policy architecture defs
      cuda_rng.h                     # GPU RNG
      cuda_renderer.h                # Renderer types

  python/                            # Python RL interface
    netherite/
      __init__.py
      env.py                         # Gym-like environment wrapper
      renderer.py                    # Visualization utilities
    train/
      ppo.py                         # PPO training loop
      eval.py                        # Policy evaluation
      sweep.py                       # Hyperparameter sweeps
    tools/
      convert_traj.py                # Trajectory format conversion
      validate_replay.py             # Cross-validate C++ vs Java oracle
      benchmark.py                   # Throughput benchmarking
    bindings.cpp                     # pybind11 bindings

  textures/                          # Raw 16x16 PNGs from MC 1.7.10
    blocks/
    entities/                        # Entity textures (optional, for rendered obs)

  tests/
    test_physics.cpp                 # Player physics validation
    test_collision.cpp               # AABB collision
    test_determinism.cpp             # Tick determinism across runs
    test_oracle_match.cpp            # C++ vs Java oracle comparison
    test_fluid.cpp                   # Water/lava flow
    test_spawn.cpp                   # Mob spawning
    test_rng.cpp                     # RNG determinism
    test_renderer.cpp                # Renderer output validation
    test_crafting.cpp                # Recipe correctness
    test_worldgen.cpp                # Terrain matches Java for same seed
```

## Carry-Over from Legacy

~15,000 lines carry over from the existing codebase:

| Component | Lines | Carry-over status |
|-----------|-------|-------------------|
| C engine (physics, collision, blocks, tick, spawn, fluid, world, rng) | 4,469 | Rename .c -> .cpp, minor refactoring |
| C headers (data structures) | 915 | Extend with new systems |
| CUDA batch env (batch_env, device_init, host_api) | 1,925 | Carries over as-is |
| CUDA policy kernel (DDA raycast) | 1,230 | DDA becomes renderer foundation |
| CUDA physics (player_physics, collision, observation) | 508 | Carries over |
| CUDA headers (device_types, cuda_policy, cuda_rng) | 724 | Carries over |
| Python training (PPO, eval, env wrapper) | ~3,500 | Update bindings |
| C tests (determinism, oracle_match, physics, fluid, spawn) | 2,032 | Carries over as validation suite |
| NSTA/NREC format code | ~360 | Exact carry-over |
| Makefiles | 126 | Rewrite as CMake |

## New Code Required

~10,000-14,000 lines to write:

| Component | Est. Lines | Source reference |
|-----------|-----------|-----------------|
| World generation (terrain, biomes, noise, structures) | 4,000-5,000 | Java mc-src WorldGen* classes |
| Mob AI + pathfinding | 2,000-3,000 | Java EntityAI* + PathFinder |
| Entity types (all kept mobs) | 2,000-3,000 | Java Entity* classes |
| Crafting/smelting/brewing tables | 500-800 | Java CraftingManager, FurnaceRecipes, BrewingRecipeRegistry |
| Enchanting system | 300-500 | Java Enchantment*, EnchantmentHelper |
| CUDA voxel renderer | 500-800 | Extend existing DDA in cuda_policy.cu |
| Texture atlas loader | 200-300 | New (load PNGs into flat array) |
| Python bindings (pybind11) | 300-500 | Replace existing ctypes wrapper |
| CMake build system | 100-200 | Replace Makefiles |

All new code is translated from the Java reference in `legacy/mc-src/`. This is mechanical translation, not design work.

## Build

CMake with CUDA support. Two targets:

```bash
# Build everything
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run tests
cd build && ctest

# Train (Python)
uv run python/train/ppo.py --nsta world.nsta --N 1024

# Human play
./build/netherite --play --seed 42

# Headless benchmark
./build/netherite --bench --N 1000 --ticks 10000
```

Dependencies:
- CUDA Toolkit 12.x
- CMake 3.20+
- Python 3.10+ with pybind11
- GLFW or SDL2 (human play mode only, optional)
- stb_image (PNG loading, header-only)
- No other dependencies. No Boost, no game engine, no graphics libraries.

## Machines

- **local (theodolos):** RTX 3090 24GB, 96GB RAM, CUDA 12.6. Primary training machine.
- **macbook:** M4 Max 36GB, Metal. Development, human playtesting. CUDA code compiles but runs on CPU fallback or Metal (future).

## Development Phases

### Phase 0: Repository Setup
- Archive legacy code into `legacy/` subdirectory
- Set up CMake build with CUDA
- Rename `.c` to `.cpp`, verify existing C engine compiles as C++
- Set up pybind11 skeleton
- Verify existing tests pass

### Phase 1: Complete C++ Engine
Port remaining Java systems to C++, one at a time, validating each against the Java oracle:

1. World generation (biggest chunk -- terrain, caves, biomes, structures)
2. Mob AI and pathfinding (goal system, A*)
3. All entity types (zombie, skeleton, ..., ender dragon)
4. Crafting + smelting + brewing
5. Enchanting
6. Villager trading
7. Portal mechanics (already partially in C engine)
8. Dragon fight sequence

Validation at each step: run identical seed + action sequence in both C++ and Java oracle, compare NSTA snapshots tick-by-tick.

### Phase 2: CUDA Renderer
- Extend DDA raycast from cuda_policy.cu into full RGB renderer
- Load texture atlas from PNGs
- Implement at 64x64 and 256x256
- Validate: render known world states, visual inspection
- Benchmark: rays/second at various resolutions and batch sizes

### Phase 3: Integrated RL Training
- Wire renderer into batch env observation pipeline
- CNN policy with rendered frame input
- PPO training on visual observations
- Compare learning curves: structured obs vs visual obs vs both

### Phase 4: Human Play Mode
- GLFW/SDL window displaying CUDA-rendered frames
- Keyboard/mouse input mapped to action space
- Tick rate control (20 TPS default, uncapped for fast-forward)
- Entity interpolation for smooth rendering between ticks
- This is for profiling and verification, not shipping a game

### Phase 5: Full Game RL
- Train agents on progressively harder reward schedules
- Log breaking -> tool crafting -> mining -> nether -> ender dragon
- Curriculum learning across reward modules
- Export best trajectories as NREC, replay in Java MC for visual verification

## Validation Protocol

Every system ported to C++ is validated against the Java oracle:

1. Generate world with fixed seed in both Java and C++
2. Run identical action sequence (from NREC recording)
3. Export NSTA snapshots at configurable tick intervals
4. Compare: block states, entity positions (atol=1e-10), player state, inventories
5. Any divergence is a bug in the C++ port

For GPU validation: run the same scenario on CPU oracle and GPU batch env, compare state. The `__host__ __device__` dual compilation makes this straightforward -- same source, different execution target.

Floating point validation: integers must match bitwise. Doubles must match within atol=1e-10 (accounting for FMA instruction differences). Any larger divergence indicates a porting error.

## Performance Targets

| Metric | Target | Legacy baseline |
|--------|--------|-----------------|
| Rollout SPS (structured obs, N=1000) | >10M | 14.3M (N=8000, small world) |
| Rollout SPS (visual 64x64, N=1000) | >2M | N/A |
| Rollout SPS (visual 256x256, N=1000) | >500K | N/A |
| Human play FPS | 60+ | N/A |
| Memory per instance (GPU) | <16 MB | ~8 MB (small world) |
| World gen time (single world) | <1s | N/A |

## Key Technical Decisions

1. **C++ over Rust** -- Java's OOP (inheritance, virtual dispatch) translates directly. CUDA interop is native. The existing C engine is already halfway there.

2. **Hash-based RNG over LCG** -- Order-independent, GPU-parallel, deterministic. We sacrifice vanilla MC spawn pattern matching but gain cross-instance consistency.

3. **DDA raycasting over rasterization** -- Voxel worlds are natural for raycasting. No mesh generation, no vertex buffers, no draw calls. One kernel, N pixels, done.

4. **pybind11 over ctypes** -- Type safety, numpy interop, cleaner API. The existing ctypes wrapper works but is fragile.

5. **CMake over Makefiles** -- CUDA language support, cross-platform (theodolos Linux + macbook), dependency management.

6. **No ECS** -- Minecraft's entity system is inheritance-based (Entity -> EntityLiving -> EntityMob -> EntityZombie). Forcing it into ECS would be a redesign, not a port. Keep the inheritance hierarchy, it matches the Java reference 1:1 for validation.

7. **Textures as raw arrays** -- No texture loading library beyond stb_image. 16x16 PNGs loaded into a flat CUDA array at startup. Indexed by block_type * 6 + face.


# Doc 45: `netherite/legacy/AGENTS.md` {#doc-45}

*Absolute path: `/home/infatoshi/games/minecraft/netherite/legacy/AGENTS.md`*

# Netherite Agent Development Guide

## Architecture Overview

Three-tier pipeline: **CUDA training** (GPU, millions of SPS) -> **Weight export** (binary) -> **Java live inference** (server, 20 TPS)

## CUDA Training (csrc/cuda/)

### Key Files
- `src/cuda_policy.cu` -- Fused MLP policy + env step kernel. Contains DDA raycast, local grid search, break logic
- `include/device_types.h` -- Env struct, constants (LOG_BREAK_TICKS, LOG_TARGET_COUNT, etc.)
- `src/device_init.cu` -- World upload, env allocation
- `python/train_log.py` -- PPO training script with checkpoint recording
- `python/netherite_cuda_env.py` -- Python wrapper for CUDA env
- `python/convert_traj_to_nrec.py` -- Trajectory JSON -> NREC binary converter

### Performance (RTX 3090)
- Small world (25 chunks, 1.6 MB block_3d): **14.3M rollout SPS** at N=8000
- Large world (625 chunks, 39 MB block_3d): **342K SPS** at N=2000
- Bottleneck: L2 cache misses on random block_3d reads. 39 MB >> 6 MB L2.
- Fix: Use small NSTA snapshot (5x5 chunks), radius-6 local search, N=8000+

### Training Command
```bash
ssh theodolos "cd ~/netherite && \
  PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True \
  ~/.local/bin/uv run csrc/cuda/python/train_log.py \
  --nsta ~/netherite/state_small.nsta \
  --N 8000 --iterations 1000 --horizon 128 --max-ticks 2048 --lr 3e-4 \
  --checkpoint-interval 50 --checkpoint-dir ~/netherite/checkpoints \
  --export-weights ~/netherite/policy_weights.bin"
```

### Long-Run Train + Checkpoint Eval Loop (Theodolos)
Runs block training, evaluates every checkpoint, appends local NDJSON metrics, and auto-promotes best weights on remote.

```bash
cd ~/netherite
uv run csrc/cuda/python/theodolos_train_eval_loop.py \
  --host theodolos \
  --hours 10 \
  --iterations-per-block 200 \
  --checkpoint-interval 25 \
  --eval-episodes 64 \
  --target-success-rate 1.0 \
  --target-mean-ticks-success 2000
```

Artifacts:
- Local: `csrc/cuda/runs/theodolos/<timestamp>/evals.ndjson`
- Local: `csrc/cuda/runs/theodolos/<timestamp>/status.json`
- Remote best weights: `~/netherite/csrc/cuda/runs/theodolos/<timestamp>/best_policy_weights.bin`

### Hyperparameter Coupling (Critical)
The PPO log-break setup is sensitive to the interaction of `N`, `horizon`, and `lr`.

- `effective_batch_per_iter = N * horizon`
- Larger `N` lowers gradient noise (more stable), but can underfit per-env behaviors if total step budget is fixed.
- Larger `horizon` improves temporal credit assignment, but uses staler policy data and can destabilize with high `lr`.
- `lr` must be tuned jointly with `N * horizon`; too low underfits, too high forgets.

Observed sweep result (Theodolos, February 21, 2026; ~40M steps/config):
- Best: `N=1024, horizon=128, lr=3e-4`
- Runner-up: `N=512, horizon=128, lr=3e-4`
- Poor: `horizon=256` configs and `lr=1e-4` configs in this budget range
- `N=2048, horizon=128, lr=3e-4` collapsed in this budget range

Practical default for this project:
- Start at `N=1024 --horizon 128 --lr 3e-4`
- Keep `horizon=128` unless also retuning optimizer/update settings
- If sweeping `N`, prioritize `768..1536` before jumping to `2048+`

Evaluation caveat:
- Fused step observations are pre-step; terminal ticks can under-report logs by 1.
- `csrc/cuda/python/eval_log_weights.py` now applies terminal reward compensation (`+1 log` on terminal break ticks), so strict `pass_logs=5` is usable there.
- Raw trajectory JSON generated by `train_log.py` can still show pre-step log counts unless post-step state is queried separately.

### Observation Layout (26 floats)
| Index | Field |
|-------|-------|
| 0-2 | posX, posY, posZ |
| 3-5 | motionX, motionY, motionZ |
| 6-7 | yaw, pitch |
| 8 | onGround |
| 9-10 | nearest log dir (normalized horizontal) |
| 11 | nearest log distance |
| 12 | nearest log dy |
| 13 | look_dot (cos angle to log) |
| 14 | break_progress (0-1) |
| 15-16 | target yaw/pitch to face log |
| 17 | logs_broken / LOG_TARGET_COUNT |
| 18-25 | heightmap ring (8 compass points) |

### MLP Architecture
```
26 -> Linear(64) + ReLU -> Linear(64) + ReLU -> Actor(6) + Critic(1)
Total: 6,349 params + 6 logstd + 52 norm = 6,401 floats (25 KB)
```

### Action Post-Processing
| Output | Transform |
|--------|-----------|
| forward | tanh(raw[0]) |
| strafe | tanh(raw[1]) |
| yaw | raw[2] * 10.0 (absolute, degrees) |
| pitch | clamp(raw[3] * 5.0, -90, 90) |
| jump | raw[4] > 0 |
| attack | raw[5] > 0 |

### Block Breaking
- Uses DDA voxel raycast (not brute-force lookDot check)
- Ray marches through block_3d grid along look vector
- Must hit log block's 1x1x1 AABB within 4.5 block reach
- Matches MC's actual `world.rayTraceBlocks()` behavior
- Break progress: 1/60 per tick (bare hand on log)
- Broken logs tracked per-env via packed block coordinates

### Key Lesson: Nearest-Log Search
- **DON'T** iterate all log_positions[] (O(num_logs) per env per step)
- **DO** use `d_find_nearest_log_local()` -- local grid search on block_3d (radius 6, O(13^3) lookups)
- **DO** use `d_raycast_log()` -- DDA march for break check (~20 steps)
- This gave 50x speedup (342K -> 17.4M SPS)

## Java Server (mc-src/net/minecraft/oracle/)

### Key Files
- `OraclePolicyRunner.java` -- Live MLP inference, block breaking via world.rayTraceBlocks()
- `OracleReplay.java` -- NREC trajectory replay via moveEntityWithHeading()
- `OraclePhysicsTrace.java` -- Per-tick physics state export (.ptrace format)
- `OracleRecorder.java` -- Action recording to .nrec
- `OracleStateExporter.java` -- World state snapshots (.nsta format)

### Compilation (CRITICAL)
Must use Java 8 javac. Must NOT use `-sourcepath mc-src` (pulls in stripped classes).

```bash
JAVA8="/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home/bin/javac"
FORGESRC="$HOME/.gradle/caches/minecraft/net/minecraftforge/forge/1.7.10-10.13.4.1614-1.7.10/forgeSrc-1.7.10-10.13.4.1614-1.7.10.jar"

# Create subset sourcepath with ONLY changed files
mkdir -p /tmp/mcsrc_subset/net/minecraft/{oracle,server,network}
cp mc-src/net/minecraft/oracle/*.java /tmp/mcsrc_subset/net/minecraft/oracle/
cp mc-src/net/minecraft/server/MinecraftServer.java /tmp/mcsrc_subset/net/minecraft/server/
cp mc-src/net/minecraft/network/NetHandlerPlayServer.java /tmp/mcsrc_subset/net/minecraft/network/

"$JAVA8" -cp "$CP" -d /tmp/build -sourcepath /tmp/mcsrc_subset <files>
cd /tmp/build && jar uf "$FORGESRC" $(find . -name "*.class" | sed 's|^\./||')
```

Always backup forgeSrc.jar before patching: `cp "$FORGESRC" "${FORGESRC}.bak"`

### Headless Bot Networking (HARD PROBLEM)
EntityPlayerMP requires a real Netty pipeline for the FML entity tracker.
Dummy NetworkManager causes NPE in `FMLOutboundHandler$OutboundTarget$4.selectNetworks`.

**Solution** (used in OraclePolicyRunner):
```java
NetworkManager netManager = new NetworkManager(false);
EmbeddedChannel channel = new EmbeddedChannel(new ChannelInboundHandlerAdapter());
channel.pipeline().addLast("packet_handler", netManager);
netManager.channelActive(channel.pipeline().context("packet_handler"));
netManager.setConnectionState(EnumConnectionState.PLAY);
NetworkDispatcher dispatcher = NetworkDispatcher.allocAndSet(netManager, configManager);
dispatcher.serverToClientHandshake(bot);
```

**Even better**: Apply replay actions to the human player directly (`startReplayOnPlayer`), avoiding bot creation entirely.

### EntityPlayerMP Flying Bug
`capabilities.disableDamage = true` implicitly enables flying.
Must force EVERY TICK in tickReplay:
```java
this.replayPlayer.capabilities.isFlying = false;
this.replayPlayer.capabilities.allowFlying = false;
```

### Chat Commands
- `!replay` -- Load rl_replay.nrec, apply actions to YOUR player
- `!policy` -- Start live MLP inference bot (ghost entity, breaks blocks via raycast)

### Launch Scripts (forge-workspace/)
- `./run.sh` -- Reset world (preserves weights + replay), start server
- `./client.sh` -- Launch vanilla 1.7.10 client, auto-connect to localhost:25570
- `./overnight_autopilot.sh` -- Fully unattended run orchestration (10h default, PID conflict checks, sparse screenshots, metadata logs)
- `./policy_pass_loop.sh` -- Repeated unattended attempts until strict pass criterion is met (`N` consecutive passes, tick cap per attempt)

### Unattended Log-Break Runs (No Human Input)
Use this when you want the policy to auto-start and break logs while collecting evidence.

```bash
cd forge-workspace
./overnight_autopilot.sh --hours 10 --force-clean
```

What it does:
- Kills conflicting stale MC server/client processes when `--force-clean` is set
- Starts `run.sh` and `client.sh` in background
- Builds `run/saved/oracle_patch.jar` from `mc-src` and prepends it to `runServer` classpath (`ORACLE_PATCH_JAR`) so Oracle server patches are active without mutating Gradle cache jars
- Auto-starts policy on server via `ORACLE_LIVE_POLICY=1` (no chat command)
- Captures sparse screenshots (startup, periodic, and on log-break events)
- Writes metadata to `run/autopilot/<timestamp>/metadata.ndjson`
- Exports periodic world snapshots (`state_dim*_tick*.nsta`) for non-visual verification

Useful flags:
- `--no-client` -- metadata-only mode (no screenshots, lower RAM/CPU)
- `--screenshot-seconds 1800` -- very sparse screenshots for long runs
- `--snapshot-ticks 2400` -- fewer NSTA exports (lower disk churn)
- `--keep-running` -- leave processes alive after timer expires
- `--skip-patch-jar` -- skip patch jar build/injection

### Strict Pass Loop (Exact Criterion)
For the exact gate of `5/5 logs within <=2000 ticks, 3 consecutive runs`:

```bash
cd forge-workspace
./policy_pass_loop.sh --hours 10 --consecutive 3 --max-ticks 2000
```

It writes per-run outcomes to:
- `run/passloop/<timestamp>/results.ndjson`
- `run/passloop/<timestamp>/attempts/attempt_*/server.log`

Default recovery tuning in pass loop:
- `--stuck-ticks 160`
- `--assist-ticks 120`
- `--teleport-stall-ticks 600`

## Weight File Format
Flat little-endian float32 binary, 6401 floats (25,604 bytes):
```
W1[64*26], b1[64], W2[64*64], b2[64], WA[6*64], bA[6],
WC[1*64], bC[1], logstd[6], obs_mean[26], obs_var[26]
```

## Theodolos (GPU Server)
- SSH: `ssh theodolos` (100.119.229.90)
- GPU: RTX 3090 (24 GB), CUDA 12.6
- Python: `~/netherite/.venv/bin/python` (Python 3.13, PyTorch 2.5.1+cu121)
- CUDA build: `export PATH=/usr/local/cuda/bin:$PATH && cd csrc/cuda && make`
- Small NSTA: `~/netherite/state_small.nsta` (25 chunks, 404 logs, 842 KB)
- Large NSTA: `~/netherite/forge-workspace/run/world/state_dim0_tick156.nsta`


# Doc 46: `netherite/legacy/CLAUDE_legacy.md` {#doc-46}

*Absolute path: `/home/infatoshi/games/minecraft/netherite/legacy/CLAUDE_legacy.md`*

# Netherite: Minecraft 1.7.10 Minimal Oracle

## Project Overview

A stripped-down Minecraft 1.7.10 implementation targeting the "beat the game" critical path. This serves as a ground truth oracle for validating a future C/CUDA physics engine (MegaColonel) that will run thousands of parallel game instances for reinforcement learning.

## CRITICAL: Implementation Approach

**THIS IS A SUBTRACTION PROJECT, NOT A GREENFIELD BUILD.**

Do NOT write Minecraft from scratch. The MCP-decompiled 1.7.10 source already has working, correct implementations of physics, collision, entity AI, and world generation. We inherit that correctness by deleting what we don't need.

### Phase 1: MCP Environment Setup
1. Download MCP 9.08 (Mod Coder Pack for 1.7.10)
2. Obtain vanilla Minecraft 1.7.10 client and server jars
3. Run MCP decompilation process
4. Verify decompiled source compiles and runs

### Phase 2: Systematic Deletion
Remove systems we don't need, one at a time, verifying the game still works after each removal:

**Deletion Order (safest first):**
1. Decorative blocks (stained glass, flowers, carpets, etc.)
2. Command blocks, jukeboxes, note blocks
3. Brewing system (brewing stand, potions, cauldron)
4. Enchanting system (enchanting table, anvil, bookshelves logic)
5. Beacon system
6. Redstone system (wire, torches, repeaters, comparators, pistons, hoppers, droppers, dispensers)
7. Excluded entities (villagers, horses, wolves, ocelots, witch, slime, cave spider, iron golem, snow golem, wither)

After each deletion batch, run the game and verify:
- Game launches
- World generates
- Player can move, mine, craft
- Portals work
- Target mobs still spawn

### Phase 3: Oracle Instrumentation
Add recording/replay infrastructure to the stripped codebase:
1. Action recording (capture all player inputs with tick timestamps)
2. State export (dump world state in flat format)
3. Action replay (deterministic playback)
4. Validation (compare replayed state to original)

### Phase 4: Verification
1. Human plays through and beats the game on stripped version
2. Record the playthrough
3. Replay and verify byte-identical world state
4. Run oracle test suite

## Source Material

- **Base version**: Minecraft 1.7.10
- **Mappings**: MCP 9.08 (most thoroughly documented version)
- **MCP Download**: http://www.modcoderpack.com/ (or archived mirrors)
- **Reference**: `/mcp_1.7.10/` contains downloaded mappings and documentation

## Feature Scope

### KEEP (Critical Path)

**Biomes & Terrain**
- All biomes including oceans
- Caves, ravines
- Full procedural worldgen with fixed seed support

**Structures**
- Nether fortress (required for blaze)
- Stronghold (required for end portal)
- Dungeon, mineshaft, desert temple, jungle temple (incidental)

**Blocks (~80 types)**
- Stone, dirt, grass, sand, gravel, clay, obsidian
- All ores (coal, iron, gold, diamond, redstone, lapis, emerald)
- Crafting table, furnace, chest, ender chest
- Nether portal, end portal frame, end portal, end stone
- Nether brick, nether rack, soul sand, glowstone
- Mob spawner, ladder, fence, door, torch, TNT
- Water, lava, ice, snow, farmland, crops
- Bed, anvil, enchanting table (kept for structure gen)

**Entities (Critical Path)**
- Player, zombies, skeletons, creepers, spiders, endermen
- Blazes, ghasts, zombie pigmen, silverfish
- Ender dragon, ender crystals
- Items, XP orbs, falling blocks, TNT primed
- Arrows, snowballs, ender pearls, eyes of ender

**Systems**
- Crafting (all recipes for critical path items)
- Smelting (furnace recipes)
- Combat (melee, ranged, armor)
- Hunger/food
- Mob spawning (natural + spawner)
- Portal mechanics (nether + end)
- World generation (all biomes, structures)

### DELETE (Non-Critical)

- Decorative blocks, stained glass, flowers, carpets
- Command blocks, jukeboxes, note blocks
- Brewing system, potions, cauldron
- Beacon system
- Redstone system (wire, repeaters, pistons, hoppers, etc.)
- Villagers, horses, wolves, ocelots, witch, iron/snow golems, wither

## Current Status

**Phase 1**: COMPLETE - ForgeGradle 1.2 dev environment with MCP-decompiled 1.7.10 source
**Phase 2**: COMPLETE - Stripped server boots, loads all 3 dimensions
**Phase 3**: COMPLETE - Oracle instrumentation
**Phase 4**: IN PROGRESS - RL training pipeline

### Phase 3 Components

| Component | Status |
|-----------|--------|
| Action recording (OracleRecorder) | DONE |
| State export (OracleStateExporter) | DONE |
| Action replay (OracleReplay) | DONE |
| Validation (OracleValidator) | DONE |
| Determinism fixes | DONE |
| Checkpoint test system (10 checkpoints) | DONE |
| Vanilla client connection (FML handshake bypass) | DONE |
| Per-tick physics trace (OraclePhysicsTrace) | DONE |
| Config system (NetheriteConfig) | DONE |
| Live policy runner (OraclePolicyRunner) | DONE |
| Replay on player (startReplayOnPlayer) | DONE |

### Phase 4: RL Training Pipeline

| Component | Status |
|-----------|--------|
| CUDA batch environment (log-breaking task) | DONE |
| DDA voxel raycast (replaces brute-force lookDot) | DONE |
| Local grid search (replaces O(num_logs) scan) | DONE |
| PPO training (train_log.py) | DONE |
| Weight export to Java | DONE |
| Live MLP inference on Java server | DONE |
| Block breaking via world.rayTraceBlocks() | DONE |
| Replay trajectories on player | DONE |
| Proper aiming (policy converges to 4/5 logs) | NEEDS WORK |
| Theodolos GPU server restoration | DONE |
| Cross-platform Java 8 auto-detection (run.sh, build_oracle_patch_jar.sh) | DONE |
| NetHandler null-guards (headless bot teardown) | DONE |
| Policy/replay mutual stop | DONE |
| Deterministic policy spawn position | DONE |
| Strict pass loop orchestration (policy_pass_loop.sh) | DONE |
| Offline eval with terminal reward compensation | DONE |
| Reliable 5/5 log break in strict pass loop | NEEDS WORK |

### Performance Achieved
- Training: 14.3M rollout SPS, 6.2M effective SPS (small world, N=8000)
- 1 billion steps in 180 seconds
- Policy breaks 4/5 logs in ~326 ticks (~16s game time)
- 50x speedup from DDA raycast + small world + more envs
- Offline eval: 248/256 success (96.875%), mean_logs 4.9453, mean_ticks 560.13 (Feb 22, 2026)
- Strict pass loop: 3/3 FAIL at 4/5 logs, tick 2000 cap, `max_ticks_exceeded` (Feb 22, 2026)

### Checkpoint Test System

10 checkpoints covering the critical path:

| Checkpoint | Auto-test | Status |
|-----------|-----------|--------|
| water_bucket | PASS (tick 1) | Automated |
| fall_damage | PASS (tick 22) | Automated |
| mob_spawning | PASS (tick 2218) | Automated |
| nether_portal | Needs human | Setup verified |
| nether_fortress | Needs human | Setup verified |
| enderman_hunt | Needs human | Setup verified |
| stronghold | Needs human | Setup verified |
| crafting | Needs human | Setup verified |
| dragon_full | Needs human | Setup verified |
| dragon_1hp | Needs human | **Playtested -- credits reached** |

### Project State Snapshot (February 23, 2026)

**What is fixed**:
- Theodolos GPU server restored: `uv`, `.venv` (Python 3.13, PyTorch 2.5.1+cu121), CUDA lib build OK.
- `run.sh` / `build_oracle_patch_jar.sh` auto-detect Java 8 on Linux and macOS.
- Patch jar pipeline (`ORACLE_PATCH_JAR`) operational.
- Replay vs policy contention: `!policy` stops replay, `!replay` stops policy, ownership guards in NetHandler.
- `!policy` resets to deterministic spawn (-261.5, 67.0, -130.5) before first tick.
- Replay stop/cleanup API in `OracleReplay`.

**What is still broken**:
- Live policy deterministically stalls at 4/5 logs. Break ticks: ~288, ~508, ~1168, ~1470, then no 5th break before tick 2000.
- `rl_replay.nrec` (key600, 397 actions) is likely a 4-log trajectory. No canonical 5-log replay exists.
- CUDA offline eval shows 96.9% success but Java live policy fails 5/5 gate — possible obs mismatch.

**Known good artifacts**:
- Best weights: `csrc/cuda/runs/overfit/20260222-000204_n1024_h128_lr3e4_seed1337_fixedeval/weights_final.bin`
- Pass loop baseline: `run/passloop/20260222-192659/results.ndjson` (3/3 FAIL at 4/5)

**Remaining TODOs** (ordered):
1. Verify replay visual quality in-game (`!replay` after regen).
2. Verify live policy behavior in-game (`!policy` from fresh world).
3. Resolve deterministic 4/5 ceiling in live pass loop.
4. Produce a replay trajectory confirmed 5/5 logs; set as canonical `rl_replay.nrec`.
5. Re-run strict gate: `policy_pass_loop.sh --consecutive 3 --max-ticks 2000` must exit 0.

**Verification gates**:
- PASS criterion: 5/5 logs broken within <=2000 ticks, 3 consecutive runs.
- Offline gate: `eval_log_weights.py --pass-logs 5` must show >=95% success.
- Replay gate: `!replay` in-game must complete and break the expected number of logs.

Full state details and session startup checklist: `.cursor/rules/netherite_oracle_state.mdc`.

### Running Checkpoints

```bash
cd forge-workspace

# Auto-test (headless bot, exits with PASS/FAIL):
JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home \
  ORACLE_CHECKPOINT=water_bucket ORACLE_AUTOTEST=true \
  ./gradlew runServer --no-daemon

# Human playtest (connect vanilla 1.7.10 client to localhost:25570):
JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home \
  ORACLE_CHECKPOINT=dragon_1hp \
  ./gradlew runServer --no-daemon

# Run all 3 auto-testable checkpoints:
for cp in water_bucket fall_damage mob_spawning; do
  rm -rf run/world
  JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home \
    ORACLE_CHECKPOINT=$cp ORACLE_AUTOTEST=true \
    ./gradlew runServer --no-daemon
done
```

## Build System

### Manual Compilation (mc-src changes)

ForgeGradle only compiles mod code, not mc-src. Manual javac with Java 8 required.

**CRITICAL**: Do NOT use `-sourcepath mc-src` -- it pulls in stripped classes (Blocks.java missing fields) and crashes. Use a subset sourcepath with ONLY changed files. See AGENTS.md for the full compilation recipe.

**CRITICAL**: Must use Java 8 javac (`temurin-8.jdk`). System Java produces class files that ASM 5.0.3 can't read.

**CRITICAL**: Use Netty 4.0.10.Final (not 4.0.23). The 4.0.23 version has different `ChannelHandlerContext.read()` return type causing NoSuchMethodError at runtime.

**CRITICAL**: Always backup forgeSrc.jar before patching. Restore from backup before each recompile to avoid accumulating stale classes.

```bash
# Use the compile recipe in AGENTS.md -- it handles all edge cases
```

**CRITICAL**: log4j-2.0-beta9 must be FIRST on classpath. Use string concat in logger calls, NOT `{}` format patterns.

### Server Config

- Port: 25570 (`forge-workspace/run/server.properties`)
- online-mode: false (vanilla clients accepted)
- Seed: 42
- Max players: 2

## Architecture

```
mc-src/                          # MCP-decompiled MC 1.7.10 source (modified)
  net/minecraft/oracle/          # Oracle instrumentation package
    OracleRecorder.java          # Binary action recording (.nrec format)
    OracleStateExporter.java     # Chunk/entity/player state export (.nsta format)
    OracleReplay.java            # Recording playback (supports replay-on-player)
    OracleValidator.java         # Snapshot comparison (accepts NSTA v1+v2)
    OracleAction.java            # Action type IDs (0x01-0x11)
    OraclePolicyRunner.java      # Live MLP inference + block breaking
    OraclePhysicsTrace.java      # Per-tick physics state trace (.ptrace)
    CheckpointInitializer.java   # Checkpoint test system (10 scenarios)
    TestCheckpoint.java          # Checkpoint enum definitions
    OracleTestHarness.java       # Automated test runner
  net/minecraft/server/          # Server core (MinecraftServer tick hooks)
  net/minecraft/network/         # NetHandlerPlayServer (recording + chat commands)
  cpw/mods/fml/.../NetworkDispatcher.java  # Vanilla client acceptance
forge-workspace/                 # ForgeGradle project
  build.gradle                   # Env var forwarding for ORACLE_* props
  run.sh                         # Reset world + launch server
  client.sh                      # Launch vanilla 1.7.10 client (no launcher needed)
  run/                           # Server runtime (world, logs, properties)
csrc/                            # C reimplementation of MC physics
  src/, include/, test/          # C engine (player physics, collision, spawn, fluid)
  cuda/                          # CUDA batch RL environment
    src/cuda_policy.cu           # Fused step kernel with DDA raycast
    python/train_log.py          # PPO training with checkpoints
    python/convert_traj_to_nrec.py  # Trajectory -> NREC converter
```

## Key Technical Notes

- EntityPlayerMP.onUpdate() does NOT call super.onUpdate() -- server-side physics don't run for players (client-authoritative movement)
- `setBlock` flag 2 = client notify only; flag 3 = block update + client notify. Fluid flow requires flag 3
- `EntityPlayer.fall()` is protected -- use `attackEntityFrom(DamageSource.fall, damage)` from external code
- `setCurrentItemOrArmor(slot, stack)`: slot 0=held, 1=boots, 2=leggings, 3=chestplate, 4=helmet
- End island surface at (0,0) is ~y62-65. Spawn platforms must be at y=75+ to avoid suffocation
- Mob spawn exclusion: mobs can't spawn within 24 blocks of any player
- `transferPlayerToDimension` can crash if called during world init -- use 60-tick delay after player join
- `capabilities.disableDamage = true` implicitly enables flying -- must set `isFlying = false` and `allowFlying = false` every tick
- Headless bots need EmbeddedChannel + FML NetworkDispatcher pipeline or entity tracker NPEs (see AGENTS.md)
- Best approach for replay: apply actions to the HUMAN PLAYER, not a bot (avoids all networking issues)
- PLAYER_PHYS_INPUT (0x11): forward(f32) + strafe(f32) + yaw(f32) + pitch(f32) + jump(u8) = 17 bytes. Uses moveEntityWithHeading for physics.
- PLAYER_POS_LOOK (0x04): teleports (no physics). Only use for initial position, not replay.
- MC block breaking requires targeting specific coordinates. CUDA trains with DDA raycast, Java uses world.rayTraceBlocks()
- Training spawn position: (-261.5, 67.0, -130.5) on seed 42. Policy obs normalizer is calibrated for this region.

## Next Operator Runbook

### Server + Client Launch

```bash
cd forge-workspace
./run.sh                  # fresh world + server (port 25570)
./client.sh               # vanilla 1.7.10 client auto-connects
```

### In-Game Commands (type in MC chat)

- `!policy` — Start live MLP policy (breaks logs via raycast). Stops any active replay.
- `!replay` — Replay `rl_replay.nrec` on your player. Stops any active policy.

### Strict Pass Loop (Unattended)

```bash
cd forge-workspace
./policy_pass_loop.sh --hours 2 --consecutive 3 --max-ticks 2000 --no-client
# Results: run/passloop/<timestamp>/results.ndjson
# Exit 0 = PASS, exit 1 = FAIL
```

### Offline Checkpoint Evaluation (Theodolos)

```bash
ssh theodolos "cd ~/netherite && \
  uv run csrc/cuda/python/eval_log_weights.py \
  --weights policy_weights.bin --nsta state_small.nsta \
  --episodes 256 --pass-logs 5"
```

### Training (Theodolos)

```bash
ssh theodolos "cd ~/netherite && \
  PYTORCH_CUDA_ALLOC_CONF=expandable_segments:True \
  uv run csrc/cuda/python/train_log.py \
  --nsta state_small.nsta \
  --N 1024 --iterations 1000 --horizon 128 --max-ticks 2048 --lr 3e-4 \
  --checkpoint-interval 50 --checkpoint-dir checkpoints \
  --export-weights policy_weights.bin"
```

### Replay Regeneration (Theodolos)

```bash
# Export best 5-log trajectory, convert to NREC
ssh theodolos "cd ~/netherite && \
  uv run csrc/cuda/python/eval_log_weights.py \
  --weights policy_weights.bin --nsta state_small.nsta \
  --episodes 256 --pass-logs 5 --export-traj best_5log_traj.json && \
  uv run csrc/cuda/python/convert_traj_to_nrec.py \
  best_5log_traj.json rl_replay.nrec"
# Then scp to local:
scp theodolos:~/netherite/rl_replay.nrec forge-workspace/run/saved/rl_replay.nrec
```


# Doc 47: `netherite/legacy/TESTING.md` {#doc-47}

*Absolute path: `/home/infatoshi/games/minecraft/netherite/legacy/TESTING.md`*

# Oracle Verification Testing

## Quick Start (Automated)

```bash
./oracle_test.sh        # Run with 200 ticks (default)
./oracle_test.sh 50     # Run with 50 ticks (faster, for smoke tests)
```

The script runs the full cycle: compile -> record -> replay -> validate.

## Current Status

The test harness works end-to-end: record, replay, and validate all complete
successfully. **Validation currently reports divergences** (~258 at 50 ticks) due
to non-deterministic entity/mob behavior. This is expected until the Entity.rand
seeding fix propagates through a full recompile of all entity classes (not just
Entity.java itself). The validation infrastructure correctly identifies and
reports these divergences.

## Prerequisites

1. **JDK 8** (Temurin recommended):
   ```bash
   export JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home
   ```

2. **ForgeGradle workspace set up** (one-time):
   ```bash
   cd forge-workspace
   JAVA_HOME=... ./gradlew setupDecompWorkspace --no-daemon
   ```

3. **EULA accepted**:
   ```bash
   echo "eula=true" > forge-workspace/run/eula.txt
   ```

4. **server.properties** configured (should already be):
   - `level-seed=42`
   - `online-mode=false`
   - `player-idle-timeout=0`

## How It Works

### Phase 1: Record

The server starts with `ORACLE_TEST=record`. The `OracleTestHarness` creates a
headless bot (fake `EntityPlayerMP` with a dummy `NetworkManager`) and scripts a
sequence of actions:

- Walk forward (position updates every tick)
- Change held item slot (ticks 40, 80)
- Swing arm (ticks 60, 120)
- Start/stop sprinting (ticks 100, 160)

The oracle recording hooks in `NetHandlerPlayServer` capture all actions to
`oracle_recording.nrec`. After N ticks, `OracleStateExporter` dumps the overworld
state (chunks + entities + player) to `oracle_record_dim0_tickN.nsta`. Server shuts
down automatically.

### Phase 2: Replay

The world is deleted and regenerated from seed 42. The server starts with
`ORACLE_TEST=replay`. The recording file must be present in the world directory.
The harness loads the `.nrec` recording and `OracleReplay` creates a new headless
bot, then injects the recorded actions at the same absolute tick numbers.

After the same N ticks, another state snapshot is exported to
`oracle_replay_dim0_tickN.nsta`. Server shuts down.

### Phase 3: Validate

`OracleValidator` compares the two `.nsta` snapshots:

- **Chunks**: byte-for-byte comparison of block IDs and metadata
- **Entities**: type match, position within epsilon (1e-6), health match
- **Player**: position, motion, health, food, saturation, inventory, dimension

Reports PASS if identical, FAIL with first divergence details.

## Manual Test Procedure

### 1. Compile and update forgeSrc

The build process compiles modified `mc-src/` files against log4j-2.0-beta9 and
updates the forgeSrc jar. The `oracle_test.sh` script handles this automatically.
To do it manually:

```bash
JAVA_HOME=/Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home
FORGESRC="$HOME/.gradle/caches/minecraft/net/minecraftforge/forge/1.7.10-10.13.4.1614-1.7.10/forgeSrc-1.7.10-10.13.4.1614-1.7.10.jar"
RECOMP="forge-workspace/build/tmp/recompCls"
LOG4J="$(find ~/.gradle/caches/modules-2 -name 'log4j-api-2.0-beta9.jar' | head -1)"
LOG4J_CORE="$(find ~/.gradle/caches/modules-2 -name 'log4j-core-2.0-beta9.jar' | head -1)"
GUAVA="$(find ~/.gradle/caches/modules-2 -name 'guava-17.0.jar' | head -1)"
NETTY="$(find ~/.gradle/caches/modules-2 -name 'netty-all-4.0.10.Final.jar' | head -1)"
COMMONS_LANG="$(find ~/.gradle/caches/modules-2 -name 'commons-lang3-3.3.2.jar' | head -1)"
AUTHLIB="$(find ~/.gradle/caches/modules-2 -name 'authlib-*.jar' -not -name '*-sources*' | head -1)"
GSON="$(find ~/.gradle/caches/modules-2 -name 'gson-*.jar' -not -name '*-sources*' -not -name '*-javadoc*' | head -1)"

CP="$LOG4J:$LOG4J_CORE:$FORGESRC:$RECOMP:$GUAVA:$NETTY:$COMMONS_LANG:$AUTHLIB:$GSON"

mkdir -p /tmp/oracle_build
$JAVA_HOME/bin/javac -cp "$CP" -d /tmp/oracle_build -sourcepath mc-src \
    mc-src/net/minecraft/oracle/*.java \
    mc-src/net/minecraft/server/MinecraftServer.java \
    mc-src/net/minecraft/network/NetworkManager.java \
    mc-src/net/minecraft/network/NetHandlerPlayServer.java \
    mc-src/net/minecraft/entity/Entity.java \
    mc-src/net/minecraft/world/Explosion.java \
    mc-src/cpw/mods/fml/common/network/FMLOutboundHandler.java

cd /tmp/oracle_build
$JAVA_HOME/bin/jar uf "$FORGESRC" $(find . -name "*.class" -type f | sed 's|^\./||')
```

**Important**: Must compile against log4j-2.0-beta9, NOT a newer version. The
runtime uses beta9 which lacks some overloads present in newer versions, causing
NoSuchMethodError at runtime.

### 2. Record

```bash
rm -rf forge-workspace/run/world
cd forge-workspace
ORACLE_TEST=record ORACLE_TEST_TICKS=50 ./gradlew runServer --no-daemon
```

Verify output includes:
```
[Oracle Test] RECORD mode: bot created at tick 1, running 50 ticks
[Oracle] Recording started: .../oracle_recording.nrec (seed=42, startTick=1)
[Oracle] Recording stopped: 50 actions written to .../oracle_recording.nrec
[Oracle Test] ORACLE_RECORD phase complete. Shutting down.
```

### 3. Replay

```bash
# Save recording, then set up fresh world with recording
cp forge-workspace/run/world/oracle_recording.nrec /tmp/
cp forge-workspace/run/world/oracle_record_dim0_*.nsta /tmp/oracle_record.nsta
rm -rf forge-workspace/run/world
mkdir -p forge-workspace/run/world
cp /tmp/oracle_recording.nrec forge-workspace/run/world/

cd forge-workspace
ORACLE_TEST=replay ORACLE_TEST_TICKS=50 ./gradlew runServer --no-daemon
```

### 4. Validate

```bash
# Copy record snapshot into world dir, then run validate mode
cp /tmp/oracle_record.nsta forge-workspace/run/world/oracle_record_dim0_tick51.nsta

cd forge-workspace
ORACLE_TEST=validate ./gradlew runServer --no-daemon
```

## Expected Divergences

Divergences are currently expected from:

1. **Mob positions/spawning**: Entity.rand is seeded per-entity but the recompCls
   (original Minecraft classes) still use unseeded Random for entities not
   recompiled from mc-src. Different mob positions cause different block
   interactions (trampled farmland, path changes, etc.).
2. **Entity count differences**: Different spawn timing means different entity
   counts in the snapshots.
3. **Metadata differences**: Block metadata changes caused by mob interactions
   (e.g., farmland moisture, door states near mobs).

Once all entity classes are recompiled with the seeded Random fix, these
divergences should reduce significantly.

## Troubleshooting

**Server doesn't start**: Check `forge-workspace/run/eula.txt` contains `eula=true`

**NoSuchMethodError on Logger**: You compiled against the wrong log4j version.
The runtime uses log4j-2.0-beta9. Ensure `log4j-api-2.0-beta9.jar` is FIRST on
the javac classpath.

**Recording not created**: The test harness starts recording explicitly. Check
logs for errors during bot creation.

**Replay "Recording file not found"**: The recording file must be in the world
directory before the server finishes loading. Pre-create the world dir and copy
the recording before starting the server.

**Build: NullPointerException in FMLOutboundHandler**: The headless bot's dummy
NetworkManager has no Netty channel. The null-safe check in FMLOutboundHandler
handles this. If you see this NPE, the forgeSrc jar wasn't updated with the
FMLOutboundHandler fix.

## File Locations

| File | Location |
|------|----------|
| Recording | `forge-workspace/run/world/oracle_recording.nrec` |
| Record snapshot | `forge-workspace/run/world/oracle_record_dim0_tickN.nsta` |
| Replay snapshot | `forge-workspace/run/world/oracle_replay_dim0_tickN.nsta` |
| Record log | `oracle_test_record.log` |
| Replay log | `oracle_test_replay.log` |
| Validate log | `oracle_test_validate.log` |
| Test harness | `mc-src/net/minecraft/oracle/OracleTestHarness.java` |
| Validator | `mc-src/net/minecraft/oracle/OracleValidator.java` |
| FML null-safe fix | `mc-src/cpw/mods/fml/common/network/FMLOutboundHandler.java` |
