# Product gates (netherite)

**netherite** is the product name: a from-scratch C/CUDA reimplementation of
Minecraft 1.11.2 (magma + mc-sim), bit-verified against the real Java game,
plus a batched CUDA RL env (blaze). Humans play the oracle; tapes record; magma
replays; every divergence is a bug with a repro.

Agent entry: root `AGENTS.md`. How to run: `docs/RUNBOOK.md`.

## The four ship gates

### Gate 1 - game quality
Accept: a human plays magma spawn -> End with zero divergences; canonical tapes replay
bit-exact (physics tol 1e-9, pixels day-bit-exact / isolated star pixels at night, no
unexplained diff clusters).
Status: OPEN, machinery green. The physics canonical tape
(`c/magma/raster/verify/tapes/20260721T215812Z_..._77b5b462.jsonl`, 3,617 ticks, bot-recorded)
replays with no physics divergence end-to-end; the 12k human tape holds the pixel
baselines (A 0.96/ch, B 1.66/ch). Remaining bugs live in
`c/magma/OPEN_DIVERGENCES.md`; the full spawn->End human session has not yet been
played clean. Verification procedure: `c/magma/VERIFY.md`.

### Gate 2 - RL
Accept: spawn -> torches learned with ZERO scripted stages in the batched CUDA env
(blaze), the policy transfers to real magma, and a one-time JVM demo runs.
Status: OPEN, core accept MET (2026-07-15): the full spawn->torch chain is learned with
zero scripted stages (PPO in blaze, milestone reward + stage-gated dense shaping,
frontier curriculum from the policy's OWN captured states via blaze_capture). Real-env
cold-spawn transfer, sampled best-of-5: full chain on 11/13 seeds INCLUDING both
held-out seeds (11, 33); the 2 misses (2, 16) reach cobble with a self-crafted pick and
fail at finding coal. ~1.92B env-ticks / ~7.9 h active train. Net: rl/out/chain_net_cu.pt
(untracked). JVM demo SHIPPED (2026-07-15): a bit-matching Java SemanticCamera in the
qrl mod (obs_camera.h geometry) + float look / craft:N / interact bridge extensions let
the same net drive the real JVM game scriptlessly to cobble3 (log -> planks -> sticks
-> table -> wooden pick -> 45 cobble at y=22); best-of-6 sampled, video delivered.
Transfer is attenuated vs magma (biome/tree ids outside the trained cam vocabulary;
JVM terrain dynamics) - documented in DEVLOG-adjacent agent report, not a fidelity bug.
Fidelity machinery green at HEAD: blaze CPU is byte-exact vs the real env on the gated
obs fields including the full 2058-action spawn-to-torch chain tape (verify_cpu.py
--chain), CUDA is bitwise == CPU on the full 12-action ABI incl. craft/interact
(verify_cuda.py default/--chain/--mixed), vec env bit-exact vs the per-env loop
(test_vec_env.py).
Net-of-record update (2026-07-17): the v2 recipe's BEST checkpoint
rl/out/chain_net_cu_v2.pt (safety copy chain_net_record.pt) scores 12/13
full-chain on the sampled 13-seed real-env eval (both held-out seeds 5/5; sole
miss = seed 16 at coal), superseding the v1 net's 11/13. IMPORTANT: evaluate
{name}.pt (best-on-t0), never {name}_last.pt - the same leg's tail checkpoint
scores only 10/13 (see the gate-3 collapse hazard). Independent confirmation of
the recipe tonight: a 1.35-h from-scratch run's best checkpoint
(chain_net_pin1_best.pt) ties v1 at 11/13 with roughly half of v1's 1.92B ticks.

### Gate 3 - perf pins
Accept: >=1M env-ticks/s FULL-FEATURE at N>=8192; spawn-to-torch trains <1h; magma
60 fps at 1080p.
Status: throughput pin MET (2026-07-15). Full-feature t0 (128^3 regions, full 12-action
decode) on the idle RTX PRO 6000: 1.02-1.03M env-ticks/s at N=9216 (0.94M at N=8192;
legacy same-GPU A/B 0.37M). The fix stack: ore-list CSR bucketing (338fd04) + warp-
cooperative cu_recenter (7778cd9, merged fada44c) - the old kernel had one lane doing
~150k serial memory ops per chunk crossing while its warp stalled; now the warp ballots
crossing envs and all 32 lanes stride each refill. Both bit-identical, full ladder green
on sm_86 and sm_120 (default/chain/mixed CUDA gates + CPU 8-stream/chain zero-diff).
Mining slice: 0.90-1.98M at N=4096-16384 (pre-H numbers; H adds 1.2-1.4x there).
Legacy kernel kept behind load-time BLAZE_LEGACY_RECENTER=1 (zero tick cost).
Re-confirmed at HEAD 2026-07-17: 1.02M env-ticks/s at N=9216.
2026-07-19 update: 4.06M env-ticks/s full-feature t0 at N=8192 (4x the pin).
Three stacked wins, each gated byte-exact: interact container-list (k_tick
32.2 -> 15.6 ms/step), float32 DDA obs camera (k_obs 7.09 -> 1.03; ids
differ 1 px per 1.66M rays vs double), warp-per-env k_tick (BLAZE_WARP_TICK,
default on; k_tick 15.3 -> 6.7). Trainer end-to-end 0.256 -> 0.43M t/s: the
pin-budget 0.95B-tick run now takes 37 min (best t0 0.325, real-env transfer
7/13 incl. held-out s11) vs the 60-min pin1 leg. A 1h/1.53B-tick leg ran
~600M past the peak and reproduced the collapse (best 0.100) - budget by
TICKS (~0.9B), not wall-clock, and per-chunk ent/kl/clip telemetry is now
printed by ppo_chain_cu.py to diagnose the collapse next long run.
Train pin MET (2026-07-17): from scratch, zero scripted stages, v2 recipe
(spec-driven reward_chain.py, sidecar JSON per checkpoint), N=6144 on the idle
RTX PRO 6000: 0.254-0.261M env-ticks/s end-to-end, 0.90B ticks in 3600s ->
trailing t0 full-chain 0.360, best-window 0.420. The enabler was an obs_float
prealloc leak fix (ragged-minibatch shapes minted a pinned buffer per distinct
dim0; one growable buffer + [:n] view stopped the growth that OOMed N=6144+
configs and lifted sustained trainer throughput from 0.069M to 0.24-0.26M t/s).
Real-env sampled transfer of the 1h checkpoint (best-of-5 x 6000 ticks, 13
seeds): 8/13 torches incl. held-out 11; misses are 3 coal + 2 cobble3. v1
quality (11/13) needs >1h. Artifacts: rl/out/chain_net_pin1_1h.{pt,json}
+ chain_curve_pin1_1h.{npy,png}, train log pin1_train.log.
Known hazard (2026-07-17, both legs reproduce): past the ~0.4 t0 peak,
continued PPO training collapses full-chain rate to ~0.05 within ~30-40M
ticks (v2 leg fell 0.42->0.27 over its back half; pin1 continuation fell
0.39->0.05 by chunk 240). The trainer's best-on-t0 checkpoint ({name}.pt)
preserves the peak state; _last is the regressed tail. Cause undiagnosed
(entropy/KL telemetry is not printed per update) - evaluate the BEST
checkpoint, never the tail.
fps pin NOT met (re-measured 2026-07-27, MAGMA_BENCH env-gated 12-stage
timers, still cam, vd=8, SDL dummy video, 600 measured frames at 1080p):
CPU backend 4.51 fps (was 5.00 on 07-17; the cutout-coverage fix added real
geometry); RTX PRO 6000 CUDA backend 35.93 fps mean / 35.98 p50 (was 24.67
on 07-17 - the raster stage fell 28.6 -> 17.5 ms). Stage means now: raster
17.5, hud 4.4, mesh 2.3, present 1.3, cuda in+out 1.7, sky 0.43 ms. 60 fps
needs a ~1.6x raster kernel (bit-parity-constrained) plus hud caching and
io/present overlap. The 3090's 11.74 (07-17, host-rendered sky) was not
re-measured - it has a co-tenant. CPU backend is not on the 60 fps path.

### Gate 4 - ops (this deliverable)
Accept: one command runs the verification pyramid green.
Status: SHIPPED as `netherite_sweep.sh` (repo root). `--quick` is green today except
steps listed as SKIP (known-broken `make test-config` at HEAD; artifact-gated steps).
A FAIL exits nonzero; SKIPs never do.

## Running the sweep

```bash
bash netherite_sweep.sh --quick          # builds + unit batteries + blaze CPU gate + vec-env (<10 min)
bash netherite_sweep.sh --full           # + mc-sim CUDA oracle, blaze CUDA gate, canonical tape replay (GPU1), raster parity, RL smoke (<40 min)
bash netherite_sweep.sh --full --gpu 0   # device for blaze/mc-sim CUDA steps (tape replay + parity stay pinned to GPU1)
```

Each step wraps an existing gate (make target or script - nothing reimplemented), has
its own timeout and log (path printed at start), and reports [PASS]/[FAIL]/[SKIP] plus
a summary table. GPU steps preflight `nvidia-smi` and SKIP when the device is >50%
util - the box is shared. Missing artifacts (snapshots, tapes, prefixes) SKIP with a
reason. Deeper/slower layers of the pyramid stay where they live: the nightly all-tape
sweep is `c/magma/raster/verify/nightly_verify.sh`, per-kernel CPU==CUDA verifies are
`make -C c/mc-sim verify-<kernel>`.

## Out of scope

- Dragon-fight RL (the sim supports the fight; no RL training on it)
- Multiplayer / servers
- Any Minecraft version other than 1.11.2
- Multi-GPU training or inference (single-GPU pins only)
