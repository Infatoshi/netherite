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

One-shot (preferred on a clean Linux box):

```bash
bash scripts/setup_and_verify.sh          # bootstrap + build + --quick
bash scripts/setup_and_verify.sh --full   # + CUDA gates
```

Stepwise equivalent:

```bash
# 1. decompiled oracle (java/oracle-src): downloads MC 1.11.2 + MCP mappings
#    via ForgeGradle setupDecompWorkspace, then copies the output tree.
bash scripts/bootstrap_oracle.sh

# 2. texture-derived C headers (magma/assets/*_atlas.h etc.), extracted
#    from your minecraft-1.11.2.jar (or set MC_JAR=/path/to/it).
bash scripts/bootstrap_assets.sh

# 3. build + verify
make -C magma game
bash netherite_sweep.sh --quick
```

What each step reproduces:

- `scripts/bootstrap_oracle.sh` -> `java/oracle-src/net/{minecraft,minecraftforge}`
  (2,666 files). This is the read-only reference the C reimplementation was
  verified against; `blaze/ref/mc-src` symlinks to it. The MCP mapping
  snapshot is pinned in `java/Minecraft/build.gradle`, so the decompiled
  output is deterministic. It first runs `scripts/fetch_mc_assets.py` to
  pre-seed the game-asset cache over https: the pinned ForgeGradle downloads
  assets over plain http, which Mojang's CDN now rejects with HTTP 400
  (`java.io.IOException: ... response code: 400`); pre-seeded objects are
  hash-checked and skipped by ForgeGradle, so the http path is never hit.
- `scripts/bootstrap_assets.sh` -> the 12 generated headers in
  `magma/assets/` (block/GUI/HUD/item/mob/sky atlases, colormaps, water
  animation frames). Each `build_*.py` extracts textures from the jar found
  by `assets/mc_jar.py`.

Pixel-baseline captures (`verify/mc_capture`, tape videos)
are also not distributed; the verify steps that need them SKIP until you
record your own via `magma/VERIFY.md`. Simulation-state gates (tick
traces, BOLR byte-exactness, CPU==CUDA) run without any captures.

## RL artifacts

The RL gate chain regenerates from the repo + the two committed reference
files in `blaze/rl/out/` (`chain_actions_s10.json`, the canonical
2058-tick spawn-to-torch action stream; `coal_prefixes.json`, per-seed probe
prefixes):

```bash
cd magma && make game blaze_so
T0=1 uv run --no-project --with numpy,torch python blaze/env/make_snapshots.py  # fresh-spawn t0
uv run --no-project --with numpy,torch python blaze/env/make_snapshots.py       # curriculum s*_d*
cd blaze/env && uv run --no-project --with numpy,torch python verify_cpu.py --chain
```

The last command must print `PASS: 1/1 chain stream zero-diff`: your locally
built simulator replays the recorded spawn-to-torch chain byte-exactly.
