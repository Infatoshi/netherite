# mc-1.11.2-env (AGENTS.md)

Home: Anvil-primary - canonical at `anvil:~/dev/minecraft/mc-1.11.2-env`. Build/run HERE.
MacBook is control plane / Moonlight / image viewing only.

## Living docs
- Entry + runs: `CLAUDE.md` (full) or this file (short)
- Product/fidelity: `c/craster/PRODUCT.md`, `VERIFY.md`, `OPEN_DIVERGENCES.md`
- Contracts: `c/mc-sim/SPEC.md`, `c/render-opt/SPEC.md`, `c/craster/SPEC.md`
- Compressed history: `DEVLOG.md` (code/goldens are ground truth)

## CRITICAL: anvil is headless
- Demos: scp png/mp4 to Mac; never assume local image display.
- Human play: Moonlight/Sunshine `:0` or mcwindow; tapes via `c/craster/VERIFY.md`.
- Agent/trace: Xvfb `:1` + `bash java/start_vnc_client.sh` (VNC 5900, pw `redstone`).
- One client owns qrl port 25575 at a time.

## Commands
- JDK8: `export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64`; `java/Minecraft/ ./gradlew -g run/gradle build`
- Craster: `make -C c/craster game` / `make verify-harsh` / tape loop in VERIFY.md
- mc-sim: `uv run --no-project python c/mc-sim/oracle/runner.py <name>`
- render-opt: `cd c/render-opt && uv run --no-project python harness/runner.py kernels/<dir>`
- Python: UV only

## Gotchas
- Kill game: `pkill -9 -f '[G]radleStart'` (`[G]` required)
- Launch game standalone (`setsid nohup ...`); do not chain kill+launch+poll
- Goldens from real MC only; `-ffp-contract=off` for C bit-match
- NEVER push public (decompiled Mojang source). Private remote only.

## Style
No emojis, no em dashes. Minimal changes. Verify before claiming done.
