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

```bash
# 1. decompiled oracle (java/oracle-src): downloads MC 1.11.2 + MCP mappings
#    via ForgeGradle setupDecompWorkspace, then copies the output tree.
bash scripts/bootstrap_oracle.sh

# 2. texture-derived C headers (c/craster/assets/*_atlas.h etc.), extracted
#    from your minecraft-1.11.2.jar (or set MC_JAR=/path/to/it).
bash scripts/bootstrap_assets.sh

# 3. build + verify
make -C c/craster game
bash netherite_sweep.sh --quick
```

What each step reproduces:

- `scripts/bootstrap_oracle.sh` -> `java/oracle-src/net/{minecraft,minecraftforge}`
  (2,666 files). This is the read-only reference the C reimplementation was
  verified against; `c/mc-sim/ref/mc-src` symlinks to it. The MCP mapping
  snapshot is pinned in `java/Minecraft/build.gradle`, so the decompiled
  output is deterministic.
- `scripts/bootstrap_assets.sh` -> the 12 generated headers in
  `c/craster/assets/` (block/GUI/HUD/item/mob/sky atlases, colormaps, water
  animation frames). Each `build_*.py` extracts textures from the jar found
  by `assets/mc_jar.py`.

Pixel-baseline captures (`c/craster/raster/verify/mc_capture`, tape videos)
are also not distributed; the verify steps that need them SKIP until you
record your own via `c/craster/VERIFY.md`. Simulation-state gates (tick
traces, BOLR byte-exactness, CPU==CUDA) run without any captures.

## RL artifacts

The RL gate chain regenerates from the repo + the two committed reference
files in `c/craster/rl/out/` (`chain_actions_s10.json`, the canonical
2058-tick spawn-to-torch action stream; `coal_prefixes.json`, per-seed probe
prefixes):

```bash
cd c/craster && make game cuenv_so
T0=1 uv run --no-project --with numpy,torch python rl/cuenv/make_snapshots.py  # fresh-spawn t0
uv run --no-project --with numpy,torch python rl/cuenv/make_snapshots.py       # curriculum s*_d*
cd rl/cuenv && uv run --no-project --with numpy,torch python verify_cpu.py --chain
```

The last command must print `PASS: 1/1 chain stream zero-diff`: your locally
built simulator replays the recorded spawn-to-torch chain byte-exactly.
