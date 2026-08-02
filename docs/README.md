# docs/

How-tos, product gates, and history. **Agents start at root `AGENTS.md`**, not here.

## Read in this order (first touch)

1. Root `AGENTS.md` - commands, gotchas, where code lives
2. `BOOTSTRAP.md` - first clone: regenerate oracle + assets from your MC install
3. `RUNBOOK.md` - play / VNC / NetheriteMod (mod id qrl) / sweep
4. Only when your task needs it:
   - `GATES.md` - what "shipped" means (four product gates)
   - `magma/VERIFY.md` - how we prove fidelity against real MC
   - `magma/PRODUCT.md` - game product contract
   - `magma/OPEN_DIVERGENCES.md` - open bugs with repros (closed forensics: `CLOSED_DIVERGENCES.md`)
   - `c/*/SPEC.md` - architecture for that tree
5. `DEVLOG.md` - compressed history and hard lessons (optional)
6. `archive/` - old reports and pre-mainline experiments. **Ignore unless archaeology.**

## Layout

| File | Role |
|------|------|
| `BOOTSTRAP.md` | Regenerate Mojang-derived oracle/assets locally |
| `RUNBOOK.md` | How to run the game, agent stack, RL bridge, sweep |
| `GATES.md` | Product name "netherite" + four ship gates + sweep |
| `DEVLOG.md` | Journey, hard lessons, dated notes |
| `archive/` | GROK_REPORT, legacy-games learnings dump |

## Not in docs/ (on purpose)

Living contracts stay next to the code they govern so a change and its rules
travel together:

- `magma/PRODUCT.md`, `VERIFY.md`, `OPEN_DIVERGENCES.md`, `SPEC.md`
- `blaze/SPEC.md`, `java/render-opt/SPEC.md`
- Small per-module `README.md` files

Tape/trace session reports are harness output under
`verify/trace/report/` - not documentation.
