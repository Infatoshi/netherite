# blaze

Data-oriented C/CUDA Minecraft 1.11.2 *simulation* for batched RL. One shared core
compiles CPU (oracle) and CUDA (batch / parity). Rendering is elsewhere (render-opt /
magma). Contract: `SPEC.md`. History: `../../docs/DEVLOG.md`.

```bash
make oracle
uv run --no-project python oracle/runner.py <name>
MC_SM=sm_86 make   # override arch
```

Layout: `core/` shared headers, `cpu/` + `cuda/` drivers, `oracle/` goldens, `py/` gym smoke.
