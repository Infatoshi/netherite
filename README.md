# netherite

From-scratch C/CUDA Minecraft **1.11.2** (bit-verified vs the real Java game) +
batched CUDA RL.

<p align="center">
  <img src="docs/assets/zoom_farm.gif" width="800"
       alt="one agent's observation zooming out to 7,200 live batched worlds">
</p>
<p align="center"><i>One env's semantic camera, zooming out to 7,200 live
worlds stepping in lockstep on one GPU (recorded from a real batch).</i></p>

## Platforms

| | Support |
|--|---------|
| **Linux x86_64** | Full stack (build, CUDA train, Java oracle). Canonical: anvil. |
| **macOS** | CPU and Metal game backends. Control plane and image review. No CUDA, no `runClient`. |
| **Windows** | Not a build host. |

No Mojang content is shipped. You need a legal Minecraft copy and JDK 8.

## Using an LLM on this repo

Open **[`AGENTS.md`](AGENTS.md)**. Or paste:

```
Read AGENTS.md in this repo and follow it. Task: <what you want done>
```

## First clone

```bash
make -C java bootstrap-oracle   # JDK 8; downloads MC 1.11.2 via ForgeGradle
make assets
make test                       # short native units, <180s
make                            # magma_game; Metal on Darwin
```

Linux one-shot (bootstrap + `make test`):

```bash
bash scripts/setup_and_verify.sh
```

Details: [`docs/BOOTSTRAP.md`](docs/BOOTSTRAP.md). How to play: [`docs/RUNBOOK.md`](docs/RUNBOOK.md).
