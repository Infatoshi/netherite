# Runbook

How to play, tape, and drive the env. Agent entry is root `AGENTS.md`.
Verification procedure lives in `c/magma/VERIFY.md`.

Anvil is headless. Humans view its Linux processes via Mac (mcwindow or
Moonlight). The separate native Apple Silicon build opens a local SDL window.

## Run M: native Apple Silicon game

```bash
bash scripts/setup_macos.sh

# CPU reference binary
c/magma/magma_game --backend cpu --world superflat --view-distance 4

# Native Metal raster binary
c/magma/magma_game_metal --backend metal --world superflat --view-distance 4
```

Backend choice is explicit. `magma_game_metal` still accepts `--backend cpu`
for an in-process comparison; plain `magma_game` rejects Metal rather than
falling back. Use `SDL_VIDEODRIVER=dummy ... --frames N` for a deterministic
noninteractive smoke. Architecture and limitations: `docs/MACOS_METAL.md`.

Native Metal also supports the shared deterministic headless capture path:

```bash
# Script ticks to sparse PPM files named by the original simulation tick.
c/magma/magma_game_metal --backend metal --world superflat --view-distance 1 \
  --headless --ticks 100 --script events.jsonl --frames-out frames/ \
  --frame-offset 1 --frame-every 5 --render off --pace unlimited

# Direct uint8 [N,H,W,3] NPY output. The same form works with --rl and actions
# supplied on stdin.
c/magma/magma_game_metal --backend metal --world superflat --view-distance 1 \
  --headless --ticks 100 --script events.jsonl --frames-out frames.npy \
  --render off --pace unlimited
```

## Run A0: mcwindow (preferred human play)

- qrl mod streams its framebuffer as JPEG (`HumanStream.java`, 127.0.0.1:25580).
- `java/mcwindow_server.py` on anvil (`DISPLAY=:0`) relays frames to the Mac on
  :25581 and injects viewer mouse/keys via XTEST.
- Mac: `mc` alias typically does
  `ssh anvil java/mcwindow_host.sh && uv run ... ~/dev/mcwindow.py <anvil-ip>`
  (click captures mouse; Ctrl+Alt+Shift+Z releases).
- Game still launches via `java/sunshine_launch_mc.sh` on `:0` (hardware GL).

## Run A: Moonlight (fallback human play)

- Sunshine streams X display `:0` (AMD iGPU; NVIDIA stays free for compute).
- App "Minecraft 1.11.2 (mc-env)" -> `java/sunshine_launch_mc.sh`
  (forces virtual 1920x1080; mode name must be plain WIDTHxHEIGHT or LWJGL2 crashes).
- From Mac: Moonlight -> anvil -> app (or Desktop if game already running).
- Taping: `c/magma/VERIFY.md`.

## Run B: headless VNC (agent / trace stack)

```bash
bash java/start_vnc_client.sh   # Xvfb :1 + openbox + x11vnc + gradlew runClient
# Mac: ssh -f -N -L 5901:localhost:5900 anvil
#      open vnc://localhost:5901   # pw redstone
```

- CLI (no menu): `uv run --no-project --with pyyaml python java/mc_cli.py --vnc`
  (reads `java/fast.yaml`; human-play profile is `java/vanilla.yaml`).
- Only one client owns qrl port 25575 at a time. Stop the other before switching
  `:0` vs `:1`.

## Run C: RL bridge (discrete env, Python)

- qrl TCP on `127.0.0.1:25575`, newline-JSON: `reset` / `step` / `obs`.
- Client: `uv run --no-project python java/qrl_client.py` -> `QRLEnv()`.
- Needs a live client (Run A or B). World loads headlessly.
- Human tape: `recstart` / `recstop` on the bridge; see `c/magma/VERIFY.md`.

## Batched env (blaze)

Product RL path is `c/magma/rl/blaze/` (not the discrete qrl bridge).
Gates and snapshots: `docs/BOOTSTRAP.md` (RL artifacts section) and `docs/GATES.md`.

On Apple Silicon:

```bash
make -C c/magma blaze_metal_dylib

cd c/magma
uv run --no-project --with numpy python \
  rl/blaze/verify_metal.py
BLAZE_BACKEND=metal N_ENVS=32 T_CHUNK=2 MB=16 \
  uv run --no-project --with numpy,torch python rl/blaze/mps_smoke.py
```

The Python API also accepts `VecBlaze(..., backend="metal",
output_device="host"|"mps")`. Host output is a view of persistent shared Metal
storage. MPS output uses an explicit synchronized copy whose time is reported.

## One-command verification pyramid

```bash
bash netherite_sweep.sh --quick   # builds + unit batteries + blaze CPU + vec-env
bash netherite_sweep.sh --full    # + CUDA oracles, tape replay, raster parity

# Apple Silicon CPU/Metal pyramid:
bash netherite_macos_sweep.sh --quick
bash netherite_macos_sweep.sh --full
```

Details and ship criteria: `docs/GATES.md`.
