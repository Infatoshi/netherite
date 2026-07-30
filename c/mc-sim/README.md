# mc-sim

The simulation core has a scalar CPU reference, the existing Linux/CUDA
drivers, and native Apple Silicon Metal parity drivers for RNG and the semantic
camera. FP64-heavy simulation remains on CPU on macOS because MSL has no
`double`; it is never silently reduced to float32.

```bash
make verify-cpu-trunk
make verify-metal       # Apple Silicon: CPU vs Metal, tails + determinism
```

Data-oriented C Minecraft 1.11.2 *simulation* for batched RL. One shared core
compiles CPU (oracle) and CUDA (batch/parity); MSL ports use explicit compatible
layouts rather than textual CUDA-header conversion. Rendering is elsewhere
(render-opt/magma). Contract: `SPEC.md`. History: `../../docs/DEVLOG.md`.

```bash
make oracle
uv run --no-project python oracle/runner.py <name>
MC_SM=sm_86 make   # override arch
```

Layout: `core/` shared headers, `cpu/`, `cuda/`, and `metal/` drivers,
`oracle/` goldens, `py/` gym smoke.
