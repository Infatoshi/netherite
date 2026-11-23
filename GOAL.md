# GOAL: Learned pixel-to-raw-input Minecraft agent on one RTX 3090 (reset 2026-11-19, Elliot)

The C/CUDA 1.7.10 engine (874/875 files bit-exact against Java, 283,085 env-steps/s at 16x16 and 148,028 env-steps/s at 64x64 on one RTX 3090) and the macro-search phase (9 Java-verified dragon kills across seeds 1, 2, 42 in `~/.cache/netherite-native/dragon-kill/index.tsv`) are **complete**.

The six-day C-macro heuristic micro-patching loop is **closed** (pre-reset state tagged `archive/pre-reset-2026-11-19`).

## Hard Rules (non-negotiable)

1. **Freeze `csrc/mods/macros/`.** No new C macro heuristic flags, no isolated stage-table sweeps on cherry-picked saved states, and no macro-tuning commits without a trained checkpoint and weight updates. Macros remain frozen purely as throwaway scaffolding for checkpoint harvesting (`chain.c`) and initial distillation (`ntloop --teacher`).
2. **Evaluate on fresh held-out seeds with statistical power.** `dev128` (5/128 Nether) is overfit and underpowered. All headline evaluations run on at least 2,000 fresh held-out seeds from spawn (unbranched, one attempt per world), reporting every milestone per original spawn and Java replay verification (`VERIFY EQUAL`).
3. **Pixels/legal obs for the actor, privileged state for the critic only.** Asymmetric Actor-Critic: privileged C-struct oracles may feed the value critic during training and `chain.c` during checkpoint harvesting, never the deployed actor.
4. **Compute placement.** Primary training runs on **desktop-3090** (RTX 3090 GPU + 32 CPU cores). **gpu-workstation** CPU (128 cores) may be used for parallel CPU checkpoint harvesting or 2,000+ seed evals (CPU-only overnight unless Elliot explicitly approves a gpu-workstation GPU).

## Active Sequence (in order)

1. **Multi-seed state bank:** Harvest verified savestates across all 13 milestones on 500 to 2,000+ fresh seeds (spawn, iron, portal, fortress, flat-biome night pearl hunt, stronghold, End) so PPO trains every stage in parallel instead of starving behind a 3.9% Nether filter.
2. **Learned skill chooser (64x64 Asymmetric Actor-Critic CUDA PPO):** Train a neural policy over the frozen macro set from banked states with an imitation loss that anneals to zero.
   - **Acceptance gate:** Beat the fixed-priority baseline's forward Nether rate (5/128, 3.9%) on 2,000+ fresh held-out worlds from spawn with the exact same frozen macros.
3. **Iterative macro stripping (`ntloop --teacher` + macro-call penalty):** Delete macros one at a time while PPO fills the gaps with raw keyboard/mouse inputs from 64x64 observations.
   - **Headline metric:** Share of episode steps driven directly by the neural network (5% at reset -> 100% raw inputs), paired with end-to-end milestone rates on fresh held-out worlds and Java `VERIFY EQUAL` replay.
4. **Structural pearl-supply check:** Before any pearl behavior changes, measure endermen seen/night across 1,000 worlds by biome and movement pattern (cycling the 128-block mob cap + two-block roof stare-aggro).

## Parallel Track (gated on Elliot)

- **Act I public release** (engine + `nether setup` + PufferLib env + 3-track deterministic replay leaderboard + Java-verified replays): **blocked on Elliot's derivative-work legal review** of the 874-file C port before any public repo push or video.

## Status (2026-11-19 1:35 am)

- Reset executed on `master`: stopped `lane/diapick`, removed 21 dead worktrees, deleted 40 stale branches, archived Astra's 2026-11-18 review to `~/.cache/netherite-native/astra-review-2026-11-18/`, tagged `archive/pre-reset-2026-11-19`.
- Forward baseline at reset: `21/128` diamond pickaxe, `7/128` obsidian, `5/128` Nether (3.9%), `0/128` dragon kills; `0` trained policies since Nov 1; network step share `5%`.
- Next: build the multi-seed state bank and boot 64x64 Asymmetric Actor-Critic CUDA PPO on desktop-3090's RTX 3090.
