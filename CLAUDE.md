# mc-1.11.2-env

Monorepo for the Minecraft 1.11.2 reimplementation effort. Two top-level code trees, docs at root:

- **`java/`** - the real (JVM) game: the Forge+Malmo mod, the decompiled 1.11.2 oracle, and all
  Java-side build/run/orchestration scripts. This is what you actually play.
- **`c/`** - the from-scratch C/CUDA reimplementation + verification:
  - `c/render-opt/` - 40 render-path compute kernels bit-verified vs REAL MC, the live JNI drop-ins
    (sin/lightmap/biome/AO) the Java mod loads at runtime, and the whole-frame pixel-diff harness.
  - `c/mc-sim/` - the simulation in C/CUDA (worldgen all dims, blocks, fluids, light, physics,
    pathfinding, mobs, crafting, portals, ender dragon). CPU==CUDA verified from one source.
  - `c/craster/` - from-scratch software rasterizer (C + CUDA, no OpenGL in the render path).
- Root docs: this file + AGENTS.md (entry), DEVLOG.md (compressed history), README.md.

Build scripts live with the tree they build: Java scripts in `java/`, each C project has its own Makefile.

## Living docs (code is ground truth; do not re-expand journals)
- Product + fidelity: `c/craster/PRODUCT.md`, `VERIFY.md`, `OPEN_DIVERGENCES.md`, `SPEC.md`
- Sim contract: `c/mc-sim/SPEC.md` | Render catalog: `c/render-opt/SPEC.md`
- History only: root `DEVLOG.md`

## Working on anvil (Cursor/agents): demos go to the Mac, humans play over Moonlight
anvil is HEADLESS - you cannot view a png/mp4 here. To show the user a demo image/video, scp it to the MacBook.
Human play of the REAL game = Moonlight (Run A below). Never open a game window directly on anvil.
Agent rules: AGENTS.md / .cursorrules.

Home: cross-platform git repo; private remote `github.com/Infatoshi/mc-1.11.2-env` is canonical. Working checkout at `~/dev/minecraft/mc-1.11.2-env` on anvil ONLY - the game lives and runs on anvil; the Mac is just the Moonlight/SSH client. (Native-Mac checkout deleted 2026-07-10; macOS 26.5 killed legacy GL under Rosetta.)

Clean, fully playable Minecraft 1.11.2 (Forge 13.20.1.2588 + Malmo/MineRL mod), recompiled from decompiled source. Full vanilla game - plays to the End.

## Run A0: mcwindow (the play route, 2026-07-11)
- MineRL-style window: the qrl mod streams its own framebuffer as JPEG (`HumanStream.java`, 127.0.0.1:25580);
  `java/mcwindow_server.py` (anvil, `DISPLAY=:0`, python-xlib) relays frames to the Mac on :25581 and injects
  viewer mouse/keys via XTEST (pointer clamped to the game window). No screen capture anywhere.
- Mac: `mc` alias = `ssh anvil java/mcwindow_host.sh && uv run ... ~/dev/mcwindow.py 100.74.69.18`
  (viewer copy of `java/mcwindow.py`; click captures mouse, Ctrl+Alt+Shift+Z releases).
- Game still launches via `java/sunshine_launch_mc.sh` on :0 (hardware GL); Moonlight (Run A) stays as fallback.

## Run A: Moonlight stream from anvil (fallback play route)
- Sunshine runs as a user service on anvil, streaming X display :0 (AMD iGPU hardware GL; NVIDIA stays free).
  App "Minecraft 1.11.2 (mc-env)" -> `java/sunshine_launch_mc.sh` (sets env, fixes the headless CRTC - anvil has
  no monitor, the launcher forces a virtual 1920x1080 mode; mode name must be plain WIDTHxHEIGHT or LWJGL2 crashes).
- From the Mac: open Moonlight, pick anvil, launch the app (or "Desktop" if the game is already running).
- Human-play verification taping: `c/craster/VERIFY.md`.

## Run B: headless on anvil + VNC (agent/trace stack, llvmpipe)
- `bash java/start_vnc_client.sh` -> Xvfb :1 (llvmpipe sw GL) + openbox + x11vnc (localhost:5900, pw `redstone`) + `./gradlew runClient`.
- From Mac: `ssh -f -N -L 5901:localhost:5900 anvil` then `open vnc://localhost:5901`.
- CLI-driven instance (no menu, config as source of truth): `uv run --no-project --with pyyaml python java/mc_cli.py --vnc` (reads `java/fast.yaml`; human-play profile is `java/vanilla.yaml`, applied by `sunshine_launch_mc.sh`).
- Only ONE instance owns the qrl bridge port (25575) at a time - stop the other before switching between :0 and :1 stacks.

## Run C: RL bridge (discrete env, Python)
- `qrl` mod: local TCP server on 127.0.0.1:25575, newline-JSON. `reset` (auto-launches `qrl_world`, seed 0), `step`, `obs`. Tick-synced.
- Action (all optional 0/1): forward/back/left/right/jump/sneak/sprint/attack/use, hotbar 0-8, yaw/pitch in {-1,0,1} (15deg steps). Obs: x/y/z, yaw/pitch, vx/vy/vz, on_ground, health/food/air/xp, dead/deaths, dim, look{...}, entities[8].
- Client: `uv run --no-project python java/qrl_client.py` (stdlib only) - `QRLEnv().reset()/.step(action)`. Needs the client running (Run A/B); world loads headlessly.
- Human-play tape: `recstart`/`recstop` commands record every tick (inputs/physics/entities/frames) for craster replay - see `c/craster/VERIFY.md`.

## Constraints / gotchas
- JDK8 required: `/usr/lib/jvm/java-8-openjdk-amd64` (anvil). (Mac route is dead - see above - but if ever revived: x86_64 Temurin 8 under Rosetta via `/usr/libexec/java_home -v 1.8`.)
- macOS runClient needs `-XstartOnFirstThread` (LWJGL2 main-thread requirement); `build.gradle` adds it guarded by an os.name=="mac" check, so Linux is unaffected. Do NOT remove.
- GPU policy: the Xvfb :1 stack uses software GL (`LIBGL_ALWAYS_SOFTWARE=1`); the Moonlight :0 stack uses the AMD iGPU (radeonsi). Both stay off the shared NVIDIA cards (sunshine's NVENC encode is the only NVIDIA use).
- `java/start_vnc_client.sh` runs gradle `--offline` (the dead fernflower-2.0-SNAPSHOT re-HEAD 500s the launch every ~24h); set `MC_GRADLE_ONLINE=1` to force a real resolve when adding a dependency.
- `build.gradle` MixinGradle dep is repointed to `org.spongepowered:mixingradle:0.6-SNAPSHOT` (the jitpack `dcfaf61` artifact is permanently dead). Do NOT revert.
- `java/Minecraft/` and `java/Schemas/` MUST stay siblings (`build.gradle` copySchemas reads `../Schemas`).
- The Java JNI bridge classes hardcode the drop-in lib path `.../mc-1.11.2-env/c/render-opt/dropin`; if the repo moves, update those (and `c/mc-sim/ref/*` symlinks).
- Remote: PRIVATE only at `github.com/Infatoshi/mc-1.11.2-env`. NEVER make public - bundles decompiled Mojang source (local/private use only).

## Layout detail
- `java/Minecraft/` - Forge+Malmo gradle workspace; run `./gradlew` here. `src/main` = the Malmo/qrl mod.
- `java/Schemas/` - Malmo XML schemas (JAXB input); sibling of Minecraft.
- `java/oracle-src/` - decompiled 1.11.2 oracle, MCP-named, read-only reference. `net/minecraft` = 2050 files / 338,782 LOC; `net/minecraftforge` = 616 / 101k. (`c/mc-sim/ref/mc-src` symlinks here.)
- `java/` scripts: `sunshine_launch_mc.sh`, `start_vnc_client.sh`, `mc_cli.py`, `qrl_client.py`, mcwindow host/server/viewer, `fast.yaml`/`vanilla.yaml`.
- `c/mc-sim/ref/` symlinks: `mc-src -> ../../../java/oracle-src`, `render-opt -> ../../render-opt`, `netherite-csrc -> ~/games/minecraft/netherite/csrc` (external).
- Regenerable, gitignored: `java/Minecraft/run/`, `java/Minecraft/build/`, `c/*/build/`, `*.log`.
