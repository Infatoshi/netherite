# mc-1.11.2-env

Playable Minecraft 1.11.2 (Forge + Malmo/qrl) from decompiled source, plus a C/CUDA
sim and software rasterizer (`c/`). Lives and runs on **anvil**; humans play from the
Mac (mcwindow or Moonlight). Private repo only (Mojang source).

## Docs
- **Agent entry:** `CLAUDE.md` / `AGENTS.md`
- **Product + verify:** `c/craster/PRODUCT.md`, `VERIFY.md`, `OPEN_DIVERGENCES.md`
- **History (short):** `DEVLOG.md`

## Human play
- **mcwindow (preferred):** see Run A0 in `CLAUDE.md`
- **Moonlight fallback:** Sunshine app "Minecraft 1.11.2 (mc-env)" → `java/sunshine_launch_mc.sh`
- **Tapes:** `c/craster/VERIFY.md`

## Agent / trace stack
```bash
bash java/start_vnc_client.sh   # Xvfb :1 + VNC + client
# Mac: ssh -f -N -L 5901:localhost:5900 anvil && open vnc://localhost:5901  # pw redstone
```

## RL bridge
qrl on `127.0.0.1:25575`. Client: `uv run --no-project python java/qrl_client.py`.
Needs a live client (play or VNC stack).

## Trees
- `java/` — real game, qrl mod, oracle-src, launch scripts
- `c/render-opt/` — verified render kernels + JNI drop-ins (lab closed)
- `c/mc-sim/` — sim kernels CPU==CUDA
- `c/craster/` — product C game + rasterizer

JDK8 required on anvil. Do not redistribute.

## Legacy notes

- `docs/legacy-games-minecraft-learnings.md` — consolidated SPECs/learnings from older `~/games/minecraft` experiments (1.7.10 flashmine/netherite/oracle, etc.).
