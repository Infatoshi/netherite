# Product gates (netherite)

**netherite** is the product name: a from-scratch C/CUDA reimplementation of
Minecraft 1.11.2 (magma + blaze), bit-verified against the real Java game,
plus a batched CUDA RL env (blaze). Humans play the oracle; tapes record; magma
replays; every divergence is a bug with a repro.

Agent entry: root `AGENTS.md`. How to run: `docs/RUNBOOK.md`.

## The four ship gates

### Gate 1 - game quality
Accept: a human plays magma spawn -> End with zero divergences; canonical tapes replay
bit-exact (physics tol 1e-9, pixels day-bit-exact / isolated star pixels at night, no
unexplained diff clusters).
Status: OPEN, machinery green. The physics canonical tape
(`verify/tapes/20260721T215812Z_..._77b5b462.jsonl`, 3,617 ticks, bot-recorded)
replays with no physics divergence end-to-end; the 12k human tape holds the pixel
baselines (A 0.96/ch, B 1.66/ch). Remaining bugs live in
`magma/OPEN_DIVERGENCES.md`; the full spawn->End human session has not yet been
played clean. Verification procedure: `magma/VERIFY.md`.

### Gate 2 - RL
Accept: spawn -> torches learned with ZERO scripted stages in the batched CUDA env
(blaze), the policy transfers to real magma, and a one-time JVM demo runs.
Status: OPEN, core accept MET (2026-07-15): the full spawn->torch chain is learned with
zero scripted stages (PPO in blaze, milestone reward + stage-gated dense shaping,
frontier curriculum from the policy's OWN captured states via blaze_capture). Real-env
cold-spawn transfer, sampled best-of-5: full chain on 11/13 seeds INCLUDING both
held-out seeds (11, 33); the 2 misses (2, 16) reach cobble with a self-crafted pick and
fail at finding coal. ~1.92B env-ticks / ~7.9 h active train. Net: rl/out/chain_net_cu.pt
(untracked). JVM demo SHIPPED (2026-07-15): a bit-matching Java SemanticCamera in
NetheriteMod (mod id qrl; obs_camera.h geometry) + float look / craft:N / interact bridge extensions let
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
Legacy kernel was kept behind create-time opts legacy_recenter=1. It is deleted
as of the 2026-09-02 CUDA compile split; blaze_create rejects a nonzero value.
Re-confirmed at HEAD 2026-07-17: 1.02M env-ticks/s at N=9216.
2026-07-19 update: 4.06M env-ticks/s full-feature t0 at N=8192 (4x the pin).
Three stacked wins, each gated byte-exact: interact container-list (k_tick
32.2 -> 15.6 ms/step), float32 DDA obs camera (k_obs 7.09 -> 1.03; ids
differ 1 px per 1.66M rays vs double), warp-per-env k_tick (create opts
warp_tick, default on; k_tick 15.3 -> 6.7). Trainer end-to-end 0.256 -> 0.43M t/s: the
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

### Gate 4 - ops
Accept: one native command runs the short unit pyramid green.
Status: `make test` (root Makefile). Full CUDA, tape, and raster gates stay
owner make targets until C is the only path (`make verify` is not added yet).

```bash
make test
```

Nightly all-tape: `verify/nightly_verify.sh`. Per-kernel CPU==CUDA:
`make -C blaze verify-<kernel>`.

## Remaining to stop asking

Survey 2026-08-21 (Codex + Fable + Grok). Fable ranks. Asks stop when the
four gates close under `SPEC.md` (C only, no LibTorch) and
`magma/PRODUCT.md`. Do not add redstone, multiplayer, other versions, or
dragon-fight RL.

The playable magma tick stays CPU. CUDA and Metal on magma are raster.
Batched GPU sim is Blaze. "Full game on GPU" means the
`blaze/env/port_matrix.yaml` DAG, not a second magma CUDA tick.
Blaze Metal tick (M3) waits until CUDA survival rows pass M1 and M2
(`blaze/SPEC.md`).

M2 is blaze-CPU vs CUDA bitwise. Focused rows (`verify_cuda.py --chain`) run
every kernel in the row's `m2_kernels:` list (`port_matrix.yaml`; default
`raw, warp, scalar`). `--m2-kernel raw` is `blaze_tick_raw` -> `k_tick_raw`.
`--m2-kernel warp` is `blaze_tick` -> `k_tick_warp` (create opts `warp_tick=1`,
default in `blaze.conf` / `blaze/rl/ppo.conf` / `blaze/env/blaze_abi.h`).
`--m2-kernel scalar` is `blaze_tick` -> `k_tick` (`warp_tick=0`). Training
`blaze_step` uses the same pick (`blaze_cuda.cu` blaze_step_full). A row is
VERIFIED only when every listed kernel passes. mining_slice M2 stays BLOCKED
on a fresh clone (`blaze/rl/out/snaps/*_d*.bsnp` missing).

Mid-episode snapshot resume is a per-row flag, not a third tier:
`resume: true` in `port_matrix.yaml` runs `verify_resume_parity.py` after
M1 (magma+blaze-cpu) and after M2 (`--cuda`). Snapshot writes are version
10 on lane `resumegate` (v7/v8/v9 still load). mining_slice resume is
BLOCKED on the v1 `s14_t0_r48_no_liquid.bsnp` (no recapture on gamer).

| # | Remaining | Gate | Class | Host |
|---|-----------|------|-------|------|
| 1 | Native `out/blaze/rl/ppo` reproduces spawn->torch (t0 ~0.4, transfer ~11/13). Wood-break t0 0.495 matched (2026-08-21). 2026-09-03 re-run, mb=8192, 3 seeds: 08-21 tree best 0.60-0.66, wip/nn-fable 14b2698 best 0.05-0.41 and 5-6x slower per chunk; regression open, bisect running. Staged-curriculum chain4 (2026-08-22) reached t0 0.215 at 510M ticks, stage4->torch 8/8 seeds; spawn->torch t0 ~0.4 still open. | 2 | grindable | anvil gpu0 |
| 2 | Native transfer/eval into magma is wired (`make -C blaze/rl test-eval-magma`; `eval --backend magma --transfer closed` or `--transfer replay`). 13-seed tries=5 n=65 measured on gamer 2026-08-26: `retrain_0821_best.bin` magma closed == cpu == cuda seed-by-seed (torches 0/13, t0:6 logs3:7). Replay MATCH 56/65; 9 DIVERGE all cam 1-5 px, not blessed. `ppo_ckpt_best.bin` cpu==cuda==magma-closed t0:13. Camera stays 64x36 `oc_pixel`. Gate 2 accept still needs a net that places torches. Next measures on anvil. | 2 | grindable | anvil |
| 3 | Magma 60 fps at 1080p. Last CUDA measure 35.93 fps (`--set bench=1`). Raster twins are a two-machine gate; do not edit one kernel overnight. | 3 | grindable | anvil gpu1 + Mac |
| 4 | Port-matrix after spawn-to-torch. VERIFIED through furnaces/hazards/mobs_*/boats/elytra/xp. Still `supported: false`: `portals_dimensions`, `nether_route`, `dragon_victory`. A policy cannot spawn->dragon matching magma until that DAG plus blaze sweep 1 (fixed region) and 2 (actions/obs). Dragon-fight RL stays out of scope. See `blaze/OPEN_DIVERGENCES.md` "Spawn -> dragon". | 2 | grindable DAG | anvil cpu then gpu |
| 5 | Blaze Metal tick (M3). Sequence-blocked on row 4. | 2 | needs-design | Mac later |
| 6 | Magma live tick on GPU. No gate accepts it. | none | keep-cpu | none |
| 7 | Human spawn->End with zero first-divergence. | 1 | human | Moonlight |
| 8 | State-clean auto-campaign pixels: hand lighting, HUD hotbar (`magma/OPEN_DIVERGENCES.md`). Particle additive is source-closed: vanilla ParticleManager is SRC_ALPHA; magma already matches. Remaining particle residual is missing types, not blend. | 1 | grindable | anvil gpu1 |
| 9 | Spawner miniature data path (TileEntities -> script -> store -> emit). CLOSED lane/sim 2026-08-21. | 1 | closed | mac |
| 10 | Live blaze `isBurning` (AIFireballAttack). CLOSED lane/sim 2026-08-21. Replay was already fixed. | 1 | closed | mac |
| 11 | Fortress placement vs oracle MCA (seed 0 y/z). | 1 | grindable | anvil cpu |
| 12 | World spawn selection (item 16). | 1 | grindable | anvil oracle |
| 13 | Python still owns replay/pixels/M2 verify. Binary tape not written. No root `make verify`. | 4 | grindable slices | anvil cpu |

Do not grind: particle `rand`, particle blend=3 (vanilla is SRC_ALPHA),
server elytra HP, Magma GPU tick, Metal tick, raster 1.6x kernel twins,
Mac 30M-tick train. Slime rim, rain lightmap, and portal/underwater
Oracle A/B are grindable (see `magma/OPEN_DIVERGENCES.md` 2026-08-21
captures).

Harness holes: `NnUpdateStats` has `entropy_mean` but the chunk log omits
it; no KL/clipfrac; Metal `n == max_n`; schema-1
ckpt has no Adam/curriculum; C replay has no PNG path. Native 13-seed
eval is `out/blaze/rl/eval` (cpu/cuda/magma).

Pixel and recorder forensics stay in `magma/OPEN_DIVERGENCES.md`.

## Pinned engineering gates (standalone)

These are not the four ship gates above; they are locked harnesses that reject
unintentional drift. Run them after touching the corresponding surfaces.

| Gate | Invoke | What it pins |
|------|--------|--------------|
| Kernel pair parity | `bash scripts/kernel_parity_gate.sh` | CUDA/Metal kernel hashes + cpu==gpu frames |
| Wrapper worldgen census | `bash verify/worldgen/wrapper_gate.sh` | magma product populate wrapper vs blaze `owr` reference under fluid=OFF + shroomlight=stale (seeds 0 7 9 19, origin 2x2) |
| Sound seam | `make -C magma test-audio-live` | ring order/seq/overflow accounting, `--set audio=0` stays disabled and silent, and a held attack emits exactly one break plus >=1 hit through the real tick |
| Block sound map | `make -C magma test-block-audio-oracle` | every registered non-air block id's break/place/hit SoundType vs REAL Java (`gradle blockBreakSoundGolden`), raw volume/pitch float bits |

### Sound seam

Audio is magma-only (`docs/SCOPE.md`); blaze has none and no gate checks it.
The contract the gates pin is that sound is a **pure sink**: producers append
to `GmRuntime.sound_events`, nothing reads it back, and no emitter draws from a
seeded stream. So the default (audio-off) build must be bit-identical to an
audio-on one - `test-audio-live` checks the ring half, and the canonical-tape
verdict is the end-to-end check (adding the seam left every measured number in
`tape_*.gate.json` unchanged; only `magma_binary` moved).

`test-block-audio-oracle` needs the Java oracle and follows the tri-state
convention: rc=0 pass, rc=1 real divergence or malformed table, rc=3 BLOCKED
when gradle/JDK 8 is unreachable. It carries a per-action negative control
(block 41 metal -> stone) so a comparator that stopped comparing cannot pass.

Playback itself is opt-in and needs dev headers:
`make -C magma MAGMA_AUDIO_OPENAL=1 game` (Debian/Ubuntu: `libopenal-dev
libvorbis-dev`). Without the flag `game/audio_live.c` compiles to stubs. The
build hard-errors rather than auto-detecting, so a binary's behaviour never
depends on what happens to be installed.

### Wrapper worldgen census

Sidecar: `verify/worldgen/known_divergences.json`. Per seed: `diff_cells`,
class breakdown (`fluid` / `mushroom` / `other`), and `diff_sha256` (sha256 of
sorted `x,y,z,blaze_state,magma_state` lines). Counts alone are not enough:
opposite-sign cell changes can cancel.

Blessed 2026-08-02 as KNOWN wrapper mechanics (not bugs): ore-family multi-window
apply (stone -> granite/diorite/andesite) and load-order raster-vs-reverse delta.
Any drift from the sidecar FAILS (rc=1) with a per-seed count/class/hash report.
`--update` rewrites the sidecar; that is maintainer judgment only.

Diagnostic (not the gate): `bash verify/worldgen/wrapper_diff.sh`.

OPEN policy note (not pinned): `--set fluid_ca=1` changes 279/10936/4885 cells at
seeds 0/7/9; no gate pins magma default-off vs blaze always-on.

Not wired into `scripts/delegate_gate.sh` / `scripts/regression_pins.txt` (those
files are frozen). Invoke the wrapper gate standalone after populate/wrapper or
owr-path changes.

## Out of scope

- Dragon-fight RL (the sim supports the fight; no RL training on it)
- Multiplayer / servers
- Any Minecraft version other than 1.11.2
- Multi-GPU training or inference (single-GPU pins only)
