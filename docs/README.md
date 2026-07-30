# docs/

How-tos, product gates, and history. **Agents start at root `AGENTS.md`**, not here.

## Read in this order (first touch)

1. Root `AGENTS.md` - commands, gotchas, where code lives
2. `BOOTSTRAP.md` - first clone: regenerate oracle + assets from your MC install
3. `RUNBOOK.md` - play / VNC / qrl / sweep
4. Only when your task needs it:
   - `GATES.md` - what "shipped" means (four product gates)
   - `MACOS_METAL.md` - native Apple Silicon architecture, commands, and limits
   - `c/magma/VERIFY.md` - how we prove fidelity against real MC
   - `c/magma/PRODUCT.md` - game product contract
   - `c/magma/OPEN_DIVERGENCES.md` - open bugs with repros
   - `c/*/SPEC.md` - architecture for that tree
5. `DEVLOG.md` - compressed history and hard lessons (optional)
6. `archive/` - old reports and pre-mainline experiments. **Ignore unless archaeology.**

## Layout

| File | Role |
|------|------|
| `BOOTSTRAP.md` | Regenerate Mojang-derived oracle/assets locally |
| `RUNBOOK.md` | How to run the game, agent stack, RL bridge, sweep |
| `GATES.md` | Product name "netherite" + four ship gates + sweep |
| `MACOS_METAL.md` | Native Apple Silicon setup, backend boundary, measurements |
| `DEVLOG.md` | Journey, hard lessons, dated notes |
| `archive/` | GROK_REPORT, legacy-games learnings dump |

## Not in docs/ (on purpose)

Living contracts stay next to the code they govern so a change and its rules
travel together:

- `c/magma/PRODUCT.md`, `VERIFY.md`, `OPEN_DIVERGENCES.md`, `SPEC.md`
- `c/mc-sim/SPEC.md`, `c/render-opt/SPEC.md`
- Small per-module `README.md` files

Tape/trace session reports are harness output under
`c/magma/raster/verify/trace/report/` - not documentation.
