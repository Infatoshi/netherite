# cgraph-v1 receipt

Decision: **REJECT / REVERT**. The implementation and all correctness gates
completed, but the locked five-repetition A/B was slower than the Python
baseline and therefore fails the lane keep rule.

## Ceiling profile

The pre-implementation pinned Python profile measured `1602.047723 ms` per
chunk. Rollout had `74.552641 ms` idle in a `626.349663 ms` projected window;
PPO update had `77.914904 ms` idle in `968.051980 ms`; GAE had `3.572999 ms`
idle in `7.590919 ms`. Rollout plus update therefore exposed an upper bound of
`152.467545 ms`, or `9.517%`, and the lane proceeded. This was an upper bound,
not a promised gain, because rollout includes environment and reward work that
is outside the policy graph.

Raw trace: `python_ceiling.nsys-rep`. Derived values:
`ceiling_profile.json`.

## Implementation and build proof

`cgraph_train` is a C++ hot loop with no Python calls after process startup.
LibTorch's C++ dispatcher runs the FP32 NCHW convolution forward/backward via
cuDNN and linear layers via cuBLAS. TF32 and channels-last are hard-disabled.
The lane CUDA translation unit supplies graph-safe gradient reduction,
clipping, and Adam kernels. A padded fused Gumbel sampler handles all nine
categorical heads in one captured policy expression. A zero-weight padded
tail keeps every PPO sample in the one fixed-shape update graph.

The binary links `libcudnn.so.9`, `libcublas.so.13`, and
`libcublasLt.so.13`. `cuobjdump --list-elf` reports both `sm_86` and `sm_120`
cubins. Exact output and binary SHA-256 are in `build_proof.txt`.

At the pinned minibatch size, first update capture also logs two unsuccessful
`3,433,037,824`-byte allocator attempts with about `2.26 GB` free in the
graph-heavy process. The selected fallback completes: the final 30-chunk smoke
and all five benchmark processes exited cleanly. This high private-pool
footprint is another reason not to ship the slower candidate.

Initial seed-0 Torch weights were exported as 26 FP32 parameter tensors,
1,654,723 values, SHA-256
`76913e1d30249283b14c299930383fcd319b5806fdd86d06f912c0ce30633a97`.

## Gates

Equivalence, batch 6144:

- logits max abs: `0`
- value max abs: `0`
- same-action logp max abs: `0`
- entropy max abs: `1.90735e-6`
- greedy agreement: `1.000`
- gradient max abs: `2.09548e-9`
- gradient relative L2: `9.36384e-8`, budget `1e-5`
- post-clip Adam delta max abs: `1.28057e-6`, ceiling `2e-5`

The unmodified flywheel correctness suite passed all 38 checks. The paired
30-chunk smoke passed: last-third mean reward was `0.014993` Python versus
`0.0188527` cgraph, final entropy `9.11866` versus `8.91284`, mean approximate
KL `0.00778358` versus `0.0085617`, and both parameter norms moved.

## CUDA graph proof

The baseline measured-chunk NVTX range issued 22,736 direct kernel-launch API
calls. The candidate issued exactly 80 `cudaGraphLaunch` calls: 32 rollout
policy graph replays and 48 fixed-shape PPO minibatch graph replays. It also
issued 11,382 direct launch calls, primarily the existing `env.step`, reward,
GAE, selection, and curriculum operations outside the two required graphs.
Counts come from CUPTI runtime records in the preserved Nsight traces and are
recorded in `launch_proof.json`.

## Locked scoreboard

One `bench.py --lane cgraph-v1 --reps 5` invocation held the GPU0 lock for
both configurations.

- Python samples: `1577.194829`, `1579.407317`, `1579.544158`,
  `1581.600895`, `1586.045630` ms; median `1579.544158 ms`; stderr
  `1.494380 ms`.
- Cgraph samples: `1686.910`, `1690.570`, `1693.880`, `1699.880`,
  `1700.550` ms; median `1693.880 ms`.
- Absolute gain: `-114.335842 ms`.
- Relative gain: `-7.238534%`.
- MDD: `2.988760 ms`; 2% bar: `31.590883 ms`.

All correctness gates are green, but both performance clauses fail. The graph
capture is real; it simply does not recover enough launch overhead to offset
this C++/LibTorch driver's remaining environment, indexing, staging, and graph
execution costs. The result is a receipted null and should not replace the
Python trainer.
