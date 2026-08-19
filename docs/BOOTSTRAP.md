# Bootstrap: regenerating the Mojang-derived content

Agent entry: root `AGENTS.md`. After bootstrap, how to run: `docs/RUNBOOK.md`.

This repository distributes NO Mojang-derived content: no decompiled game
source, no game textures, no captured game frames. The C code, the mod
source, the build system, and the verification harnesses are all here; the
Mojang-derived inputs they reference are regenerated locally, byte-identical,
from your own Minecraft installation.

Requirements: JDK 8, `uv`, network on first run. You must own Minecraft
(https://www.minecraft.net); the game files are fetched by ForgeGradle from
Mojang's official distribution endpoints exactly as any Forge 1.11.2 mod
development environment does.

Stepwise (first clone):

```bash
# 1. decompiled oracle (java/oracle-src): downloads MC 1.11.2 + MCP mappings
#    via ForgeGradle setupDecompWorkspace, then copies the output tree.
make -C java bootstrap-oracle

# 2. texture-derived C headers (magma/assets/*_atlas.h etc.), extracted
#    from your minecraft-1.11.2.jar (or make assets MC_JAR=/path/to/it).
make assets

# 3. build + verify
make play
make test
```

What each step reproduces:

- `make -C java bootstrap-oracle` ->
  `java/oracle-src/net/{minecraft,minecraftforge}` (2,666 files). The owner is
  `java/bootstrap_oracle`. This is the read-only reference the C reimplementation was
  verified against; `blaze/ref/mc-src` symlinks to it. The MCP mapping
  snapshot is pinned in `java/Minecraft/build.gradle`, so the decompiled
  output is deterministic. It first builds and runs
  `java/fetch_mc_assets` (`netherite.fetchassets.FetchMcAssets`) to
  pre-seed the game-asset cache over https: the pinned ForgeGradle downloads
  assets over plain http, which Mojang's CDN now rejects with HTTP 400
  (`java.io.IOException: ... response code: 400`); pre-seeded objects are
  hash-checked and skipped by ForgeGradle, so the http path is never hit.
  Java invokes `org.gradle.wrapper.GradleWrapperMain` with the wrapper jar and
  repo-local `java/Minecraft/run/gradle`. It does not invoke `gradlew`.
- `make assets` -> `make -C magma assets`. Writes the 13 texture headers
  and the sound hash manifest in `magma/assets/`. `out/magma/assets --jar PATH`
  reads the jar. `out/magma/sound --index PATH` reads the 1.11 asset index.
  Make may pass `MC_JAR` and `MC_ASSET_INDEX` as build variables.

Pixel-baseline captures (`verify/mc_capture`, tape videos)
are also not distributed; the verify steps that need them SKIP until you
record your own via `magma/VERIFY.md`. Simulation-state gates (tick
traces, BOLR byte-exactness, CPU==CUDA) run without any captures.

## RL artifacts

The RL gate chain regenerates from the repo plus the two committed reference
files in `blaze/rl/fixtures/` (`chain_actions_s10.json`, the canonical
2058-tick spawn-to-torch action stream; `coal_prefixes.json`, per-seed probe
prefixes):

```bash
cd magma && make game blaze_so
uv run --no-project --with numpy,torch python blaze/env/make_snapshots.py --t0 # fresh-spawn t0
uv run --no-project --with numpy,torch python blaze/env/make_snapshots.py       # curriculum s*_d*
cd blaze/env && uv run --no-project --with numpy,torch python verify_cpu.py --chain
```

The last command must print `PASS: 1/1 chain stream zero-diff`: your locally
built simulator replays the recorded spawn-to-torch chain byte-exactly.
