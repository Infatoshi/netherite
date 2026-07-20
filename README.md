<p align="center">
  <img src="docs/assets/logo.png" width="160" alt="netherite">
</p>

# netherite

From-scratch C/CUDA Minecraft **1.11.2** (bit-verified vs the real Java game) +
batched CUDA RL. Runs on anvil.

No Mojang content is shipped. Bootstrap once: [`docs/BOOTSTRAP.md`](docs/BOOTSTRAP.md).

## Using an LLM on this repo

Open **[`AGENTS.md`](AGENTS.md)** (Claude also loads [`CLAUDE.md`](CLAUDE.md), which
points there). Or paste:

```
Read AGENTS.md in this repo and follow it. Task: <what you want done>
```

That file has the commands, gotchas, and pointers into `docs/` / `c/*/SPEC.md`.
You should not need to browse the rest of the tree first.

## Quick check

```bash
bash netherite_sweep.sh --quick
```
