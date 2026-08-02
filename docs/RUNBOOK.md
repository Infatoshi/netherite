# Runbook

How to play, tape, and drive the env. Agent entry is root `AGENTS.md`.
Verification procedure lives in `magma/VERIFY.md`.

Anvil is headless. Humans view via Mac (mcwindow or Moonlight). Never open a
game window expecting a local monitor on anvil.

## Run A0: mcwindow (preferred human play)

- NetheriteMod (mod id qrl) streams its framebuffer as JPEG (`HumanStream.java`, 127.0.0.1:25580).
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
- Taping: `magma/VERIFY.md`.

## Run B: headless VNC (agent / trace stack)

```bash
bash java/start_vnc_client.sh   # Xvfb :1 + openbox + x11vnc + gradlew runClient
# Mac: ssh -f -N -L 5901:localhost:5900 anvil
#      open vnc://localhost:5901   # pw redstone
```

- CLI (no menu): `uv run --no-project --with pyyaml python java/mc_cli.py --vnc`
  (reads `java/fast.yaml`; human-play profile is `java/vanilla.yaml`).
- Only one client owns NetheriteMod port 25575 at a time. Stop the other before switching
  `:0` vs `:1`.

## Run C: RL bridge (discrete env, Python)

- NetheriteMod TCP on `127.0.0.1:25575`, newline-JSON: `reset` / `step` / `obs`.
- Client: `uv run --no-project python java/qrl_client.py` -> `NetheriteEnv()`.
- Needs a live client (Run A or B). World loads headlessly.
- Human tape: `recstart` / `recstop` on the bridge; see `magma/VERIFY.md`.

## Batched CUDA env (blaze)

Product RL path is `blaze/env/` (not the discrete NetheriteMod bridge).
Gates and snapshots: `docs/BOOTSTRAP.md` (RL artifacts section) and `docs/GATES.md`.

## One-command verification pyramid

```bash
bash netherite_sweep.sh --quick   # builds + unit batteries + blaze CPU + vec-env
bash netherite_sweep.sh --full    # + CUDA oracles, tape replay, raster parity
```

Details and ship criteria: `docs/GATES.md`.

## Metal backend (MacBook)

`bash scripts/mac_metal_verify.sh` from the repo root on the MacBook builds
`magma_game_metal`, runs the CPU-vs-Metal raster parity gate
(`make -C magma test-raster-parity-metal`), and replays the zombie smoke tape
with `replay_tape.py --metal`. Tapes must be rsynced from anvil first (exact
command in the script header). Metal parity is UNVERIFIED until that script
passes on the MacBook; status and scope: `magma/VERIFY.md` "Metal backend
(macOS)".
