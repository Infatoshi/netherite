# Benchmarks

What the `nether` CLI costs, how fast the simulator and trainer run on two machines, and how to measure a change
yourself. Every number names its source: a receipt file, a commit, or a command you can rerun. Two hardware
configurations appear:

- **64-core Zen 5 + RTX PRO 6000 Blackwell (sm_120)**: AMD Threadripper PRO 9985WX, 64 cores, 128 threads.
- **16-core Ryzen 9 9950X3D + RTX 3090 (sm_86)**: 16 cores, 32 threads, two L3s, one RTX 3090 (24 GB).

## CLI latency

Measured 2026-11-23 on the 64-core Zen 5 machine with `hyperfine -N` (no shell), at commit c0e25716. The milliseconds
are wall time per invocation. `--profile` (or `NETHER_PROFILE=1`) splits a run into the phases below, printed on
stderr at exit.

| command | runs | median ms | min ms | max ms | target |
|---|---|---|---|---|---|
| `nether --help` | 30 | 0.58 | 0.38 | 0.77 | under 5 |
| `nether list` | 30 | 0.47 | 0.38 | 0.72 | under 5 |
| `nether eval --dir CAND` (256 End episodes) | 30 | 2.38 | 2.06 | 2.92 | under 25 |
| `nether eval --dir CAND --compare-dir BASE` (256 + 256) | 30 | 4.09 | 3.62 | 5.32 | under 25 |
| `nether flywheel GYM --dry-run` | 5 | 5.17 | 4.30 | 5.55 | |
| `nether flywheel --env E --sim S --algo A --dry-run` (composed) | 5 | 5.69 | 5.07 | 5.91 | |
| `nether flywheel GYM --gpu G` on a busy GPU (the guard's refusal) | 5 | 46.77 | 46.04 | 67.18 | |
| `nether gate --quick --no-build --json F` | 3 | 2728 | 2693 | 2770 | |
| `nether gate --quick --json F` | 3 | 3310 | 3285 | 3773 | |

The eval rows read the archived overnight End evals: candidate `eval16_d25-ppo2c` against baseline
`eval16_d25-init`. The compare prints dragon 137/256 (53.52%) against 0/256 with an exact McNemar p of 1.15e-41,
the same as the overnight report's line 135 (receipt `out/receipts/bench/eval-compare.txt`).

Phase breakdown, one `--profile` run each, in ms:

| command | startup | yaml_parse | json_parse | lock_guard | spawn_exec | stats | other | total |
|---|---|---|---|---|---|---|---|---|
| `--help` | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.023 | 0.023 |
| `list` | 0.020 | 0.000 | 0.000 | 0.000 | 0.000 | 0.000 | 0.118 | 0.138 |
| `eval --dir` | 0.015 | 0.000 | 1.897 | 0.000 | 0.000 | 0.005 | 0.044 | 1.960 |
| `eval --dir --compare-dir` | 0.020 | 0.000 | 3.729 | 0.000 | 0.000 | 0.027 | 0.045 | 3.821 |
| `flywheel GYM --gpu G` (busy) | 0.009 | 0.026 | 0.000 | 39.679 | 3.230 | 0.000 | 1.457 | 44.401 |
| `flywheel --env --sim --algo --dry-run` | 0.019 | 0.070 | 0.000 | 0.000 | 4.195 | 0.000 | 1.251 | 5.535 |

Reading it: an offline eval is almost all JSON parsing of `episodes.jsonl` (1.9 ms a 256-episode file). The GPU
guard's 40 ms is one `nvidia-smi` query. `spawn_exec` is the wall time of the children the main thread spawns
and waits on. `other` includes waiting on work a verb runs asynchronously (harvest's seeds), so for harvest it is
most of the total.

The quick gate's checks, per check, from the last of the 3 runs (ms; `--no-build` skips the two builds):

| check | `--no-build` | with build |
|---|---|---|
| macro_freeze | 24.1 | 23.8 |
| native_build | skipped | 125.9 |
| globals | 144.4 | 136.1 |
| runtime_build | skipped | 461.1 |
| digest_128x2 (a pinned 128-env, 2,000-step `vec_bench` trajectory) | 2550.8 | 2551.9 |
| train_wire_check | skipped (needs `--gpu G`) | skipped |

The gate is now the digest run: 2.55 s of 2.73 s. Two changes made it so:

- The globals check went from 0.86 s to 0.11 s (one `objdump` over every object and one `awk`, the same output
  line for line).
- The gate now runs the globals script directly instead of `make globals`, whose `all` prerequisite remade
  `out/assets` on every call (0.65 s to 0.14 s inside the gate).

The first quick gate on this lane's fresh worktree took 37 s and failed `digest_128x2`. That 37 s was mostly the
runtime's cold build (34.1 s), so it is not comparable to the warm numbers above. The digest failed because its
start was not recorded and the pinned observation digests matched an older texture cut. The gate now records the
start on demand, and the pins match what a fresh `nether setup` makes on either machine.

### Scaling

`nether ab` with two variants (`vec_bench`, 64 envs, `--groups 2` against `--groups 4`, CPU only, about 0.55 s a
run), 3 repeats per round count. Every run of every repeat printed the same trajectory digest, 0e6e0be28daa3f7c.

| rounds | median wall s | min | max | s a round |
|---|---|---|---|---|
| 1 | 1.13 | 1.12 | 1.17 | 1.13 |
| 2 | 2.28 | 2.25 | 2.32 | 1.14 |
| 4 | 4.52 | 4.50 | 4.60 | 1.13 |

`nether harvest --env wood-punch`: spawn starts with a log within 16 blocks, `rawstarts --near-block 17:16` on the
seed set, then `test_snapshots` on each start. Each run used a fresh output directory, 3 repeats:

| seeds | threads | median wall s | min | max | passed (train/eval) | rejected | overlap | indexes' SHA-256 |
|---|---|---|---|---|---|---|---|---|
| 8 | 8 | 0.59 | 0.59 | 0.60 | 6 (5/1) | 2 | 0 | a07399453723 |
| 16 | 8 | 0.93 | 0.93 | 0.94 | 11 (7/4) | 5 | 0 | b1e630093253 |
| 8 | 1 | 2.17 | 2.15 | 2.24 | 6 (5/1) | 2 | 0 | a07399453723 |
| 16 | 1 | 3.96 | 3.82 | 4.00 | 11 (7/4) | 5 | 0 | b1e630093253 |

Single-threaded, a seed costs about 0.23 s plus 0.25 s of make checks once. Every repeat produced byte-identical
train and eval indexes, at either thread count. The 8-seed split is the 16-seed split cut to seeds 1 to 8 (train
1, 2, 3, 6, 7; eval 4), because a seed's side is SHA-256(split key, seed) and does not depend on the other seeds.

## Simulator and trainer throughput

Env-steps a second. Each ratio is against its own base, in the same session, as interleaved `abrun` rounds
(4 unless stated) with the trajectory digests held equal. These come from the 2026-11-22/23 overnight hill climb's
scoreboard and commits; the source column names the line.

| what | hardware | base | result | ratio | source |
|---|---|---|---|---|---|
| CPU `vec_bench`, 16-seed Overworld mix, steady, PGO + skip-ambient + distant tick radius 48 | 64-core Zen 5 + RTX PRO 6000 Blackwell (sm_120), CPU only | 307,233 | 507,900 | 1.653x | overnight scoreboard line 20, board line 114 (commit 4bc813ff vs 2e329bc3) |
| Full PPO training (`ntloop`, 64x64 recurrent actor `s1.ls.hb.hs.dk.r1.m4.b.f.sb`, no macros), peak iteration | 64-core Zen 5 + RTX PRO 6000 Blackwell (sm_120), one GPU | | 333,911 | | `r1-ppo2c.iters.jsonl` line 11 (14 rows, 0 NaN skips) |
| The same run, middle iteration (upper median of 14) | same | | 329,893 | | overnight report line 105 |
| `ntloop`, learning off, steady, 128 worlds in 16 groups of 8, CUDA graph, bit-exact | 16-core Ryzen 9 9950X3D + RTX 3090 (sm_86) | 64,664 | 101,878 | 1.58x | overnight report line 41 (commit 87d10ec3) |
| `ntloop`, learning off, + distant tick radius 48, steady / reset-heavy | same | 64,664 / 47,607 | 171,552 / 130,577 | 2.653x / 2.743x | overnight scoreboard lines 26 and 27 |
| Full PPO `r1`, 160 worlds in 5 groups of 32, distant tick radius 64, steady | same | 60,020 | 122,466 | 2.040x | overnight scoreboard line 28 (END EQUAL, 0 mismatches) |

The bases are not one shape. The RTX 3090 rows compare against 256 worlds in 8 groups of 32, so their ratios
combine the world shape, the CUDA graph and the distant tick radius.

### A live flywheel step

One composed autoresearch step on the 16-core Ryzen 9 9950X3D + RTX 3090 (sm_86), 2026-11-23 11:09 am, from a
temporary clone of this branch with the GPU idle (48 MiB) before each run:

```bash
nether flywheel --env wood-punch --sim fast-64 --algo ppo-wood \
  --set budget_steps=300000 --set eval.episodes=16 --gpu 0 --gpu-wait 30
```

Each run trains a fresh net for 300,000 env-steps on the harvested wood-punch bank (32 seeds: 15 train starts and
6 eval starts, harvested in 2.1 s), then runs the 16-episode exact eval and writes a BASELINE row.

| run | wall s | GPU guard wait s | step s (train + eval) | env-steps | env-steps/s (whole run) | NaN skips | eval return |
|---|---|---|---|---|---|---|---|
| 1 | 10.29 | 0.04 | 10.20 | 300,160 | 38,044 | 0 | 0.379 |
| 2 | 15.38 | 5.06 | 10.26 | 300,160 | 37,900 | 0 | -0.048 |
| 3 | 15.40 | 5.05 | 10.29 | 300,160 | 37,894 | 0 | 0.590 |

All three runs are under the 60 s target. The trainer's own window, 512 envs in 4 groups after the envs are made,
ran at 56,112 env-steps/s in run 1. The whole-run rate in `results.tsv` includes making the envs. A run this
short writes only `iters.jsonl`'s final row, which carries no rate, so the flywheel divides steps by wall time.

The guard waits in runs 2 and 3 are the previous run's utilization still in `nvidia-smi`'s sample (33% the moment
after exit). Without `--gpu-wait` the guard refuses with exit 3, as it should, since it cannot tell whose work
that was.

The eval returns differ between runs, although the init (ntinit seed 1) and the eval's sampler seed (1000) are the
same each time. Which part varies (training or the eval) was not tested. This smoke checks the loop end to end, not
the policy.

## Historical milestones

| commit | change | measured | hardware |
|---|---|---|---|
| 2e329bc3 | region files read with `open` and `pread`, not stdio's locked `FILE` | reset-heavy 127,943 to 182,377 env-steps/s (+42.6%); steady 268,728 to 268,160 (overlap); system time 399 to 105 s | 64-core Zen 5, CPU only |
| fbdb488a | the CUDA graph's front by copy nodes, the cast on a side stream | stage kernel 77 us to 0.9 us, cast end to stage 21 us to 2.1 us; end to end about 1% (0.99x steady, 1.03x reset, overlap), main-thread busy 89.0% to 53.7% at 16 groups | 16-core 9950X3D + RTX 3090 |
| d8acb606 | the fast casts' air loop with one back edge (RCF_FLAT) | `rcbench` fast32 0.5701 to 0.3225 ms a launch (1.77x); `ntloop` 29,158 to 30,128 (+3.3%, the host bounds it) | 16-core 9950X3D + RTX 3090 |
| aee37378 | device-cast `ntloop` profile-guided build | +1.83% steady / +0.69% reset-heavy (41,044 to 41,796) | 16-core 9950X3D + RTX 3090 |
| aee37378 | the same | +3.00% steady / +3.94% reset-heavy (233,550 to 240,548) | 64-core Zen 5, CPU only |
| a414c99b | 128 worlds in 16 groups of 8 instead of 256 in 8 of 32 | steady 63,967 to 97,461 (1.52x), reset-heavy 45,640 to 70,144 (1.54x), digests equal | 16-core 9950X3D + RTX 3090 |

## How to benchmark a change

Compare like with like: one machine, one session, interleaved rounds, the same trajectory digest. `nether ab`
does this with no shell:

```bash
nether ab --rounds 4 --perf \
  base='out/runtime/vec_bench --config out/runtime/train/cfg/host.yaml --ns 128 --groups 2 --digest' \
  cand='out/var/cand/vec_bench --config out/runtime/train/cfg/host.yaml --ns 128 --groups 2 --digest'
```

Round r runs the variants in order on odd rounds and reversed on even rounds. Each run's metric (default
`env-steps/s`) and digest are read from its output, and a run whose digest differs fails the comparison. The table
gives each variant's median, min, max, spread and ratio to the first variant, and says "apart" when the ranges do
not overlap. Use `--json F` to keep every run, `--tsv F` to append a scoreboard row, `--gpu G` (with
`--gpu-guard`) for a GPU variant and `--lock F` to queue behind other measurements.

- Rank on medians of at least 4 rounds: single runs on these machines have swung 172k to 298k env-steps/s.
- Use the fixed-work mode (`--warm W --steps S`), never a wall-time window, so both builds do the same work.
- For a CLI change, `nether --profile VERB ...` shows which phase moved.
