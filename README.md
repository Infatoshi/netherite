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
| **macOS** | Viewer / SSH only (Moonlight, mcwindow). No native game or CUDA train. |
| **Windows** | Not supported as a build host. |

No Mojang content is shipped. You need a legal Minecraft ownership and JDK 8.

## Using an LLM on this repo

Open **[`AGENTS.md`](AGENTS.md)** (Claude also loads [`CLAUDE.md`](CLAUDE.md)). Or paste:

```
Read AGENTS.md in this repo and follow it. Task: <what you want done>
```

Full Minecraft 1.11.2 parity work is ordered and gated in
[`magma/PARITY_PROJECT.md`](magma/PARITY_PROJECT.md). Active evidence and
one-command reproductions live in
[`magma/OPEN_DIVERGENCES.md`](magma/OPEN_DIVERGENCES.md).
The current effort-weighted checkpoint is approximately 70%; its measured
scope, regression results, performance evidence, and open limits are recorded
in the parity project rather than inferred from raw test or feature counts.

## Clean Linux box (one command)

```bash
bash scripts/setup_and_verify.sh          # bootstrap + build + --quick sweep
bash scripts/setup_and_verify.sh --demo   # + physics/pixel tape replay + SBS MP4
# -> demos/pixel_match_sbs.mp4  (oracle | magma side-by-side)
```

Prism is optional. Bootstrap uses ForgeGradle; details in [`docs/BOOTSTRAP.md`](docs/BOOTSTRAP.md).
Pixel demo uses the shipped canonical tape under `verify/demo/`.
