"""Batched-GPU PPO for the FULL spawn-to-torch chain, zero scripted actions.

Env: VecBlaze on the 13 fresh-spawn t0 snapshots (128^3 regions), full
[N,13] action rows built from 9 discrete heads (dyaw, dpitch, forward, jump,
attack, use, craft, interact, hotbar; +smelt with IRON_CHAIN=1). The env
fires +10/done=1 on the FIRST
TORCH (blaze_set_success_item(50)); mining coal no longer ends the episode.

Reward is computed by reward_chain.ChainReward (spec-driven, GPU tensors)
from the blaze_step_full status readout (9 inv counts + hotbar_sel + held
item + container): one-time milestone bonuses, death/out penalty, per-decision
time cost, and stage-gated dense shaping. The resolved spec prints at start
and is saved as a JSON sidecar next to checkpoints. Override weights via
REWARD_JSON (COAL_CHEW/HUNT_DESC envs still override the v2 touchstones).
Rewards may use snapshot oracles (static log lists); the POLICY inputs are
strictly transferable (camera planes, coal scalars, inv/status, pose) and
computable from the real env's BOLR obs.

Curriculum: self-generated start states. When a lane crosses a milestone
(3 logs / pick / 3 cobble / coal), its live state is captured into a fixed
(seed, stage) snapshot slot (blaze_capture); resets sample per-seed frontier
stages (the earliest stage whose next-milestone success is still low) plus a
fixed share of true t0 starts. No scripted actions anywhere - every captured
state was produced by the policy itself.

Run (anvil, GPU0 - preflight nvidia-smi):
  cd magma && uv run --no-project --with numpy,torch,matplotlib \
      python blaze/env/ppo_chain_cu.py
"""
import contextlib
import json
import os
import struct
import sys
import time
from collections import deque

import numpy as np
import torch
import torch.nn.functional as F
from torch import nn

HERE = os.path.dirname(os.path.abspath(__file__))
RL = os.path.join(os.path.dirname(HERE), "rl")
sys.path.insert(0, HERE)
sys.path.insert(0, RL)

from reward_chain import ChainReward, ChainRewardSpec

from blaze import CAM_H, CAM_W, CUDA_SO, VecBlaze

OUT = os.path.join(RL, "out")
SNAPS = os.path.join(OUT, "snaps")

# ---- run config ----
TRAIN_SEEDS = [int(s) for s in os.environ.get(
    "TRAIN_SEEDS", "2,3,10,14,16,20,27,29,32,44,46").split(",")]
N_ENVS = int(os.environ.get("N_ENVS", "4096"))
DEVICE = int(os.environ.get("BLAZE_DEV", "0"))       # 0 = PRO 6000
REPEAT = 4
STACK = 2
T_CHUNK = int(os.environ.get("T_CHUNK", "32"))
EP_DEC = int(os.environ.get("EP_DEC", "1500"))       # decisions (x4 ticks)
MAX_TICKS = float(os.environ.get("MAX_TICKS", "3e9"))
MAX_WALL = float(os.environ.get("MAX_WALL", str(6.5 * 3600)))
GAMMA = float(os.environ.get("GAMMA", "0.995"))
LAM, CLIP = 0.95, 0.2
EPOCHS = int(os.environ.get("EPOCHS", "2"))
MB = int(os.environ.get("MB", "8192"))
LR = float(os.environ.get("LR", "3e-4"))
LR_FLOOR = float(os.environ.get("LR_FLOOR", "1e-4"))
LR_DECAY_TICKS = float(os.environ.get("LR_DECAY_TICKS", "1.5e9"))
ENT = float(os.environ.get("ENT", "0.01"))
GRAD_CLIP = 0.5
CKPT_TICKS = 2_000_000
TRAIL = 200
T0_SHARE = float(os.environ.get("T0_SHARE", "0.30"))
CAP_REFRESH = int(os.environ.get("CAP_REFRESH", "25"))   # chunks between
                                                       # re-captures per cell
WARM = os.environ.get("WARM")
OUT_NAME = os.environ.get("OUT_NAME", "chain_net_cu")   # checkpoint stem
CURVE_NAME = os.environ.get("CURVE_NAME", "chain_curve_cu")
RNG_SEED = int(os.environ.get("RNG_SEED", "0"))
# Episode success item id: 50=torch (default), 274=stone pick, 257=iron pick
# (IRON_CHAIN=1 required for craft:6/7 + smelt head).
SUCCESS_ITEM = int(os.environ.get("SUCCESS_ITEM", "50"))

# reward spec (weights + v2 touchstones; REWARD_JSON / COAL_CHEW / HUNT_DESC)
REWARD_SPEC = ChainRewardSpec.resolve()
COAL_CHEW = REWARD_SPEC.coal_chew      # kept for the launch line echo
HUNT_DESC = REWARD_SPEC.hunt_desc

# ---- benchmark / optimization knobs (blaze/rl/flywheel) ----
# BENCH_MEASURE_CHUNKS>0 turns the trainer into a fixed-work benchmark: run
# BENCH_WARMUP_CHUNKS chunks, then time BENCH_MEASURE_CHUNKS complete chunks
# (rollout + GAE + PPO update) with a device sync on both ends, print one
# BENCH line per sample and exit WITHOUT writing checkpoints or curves.
MAX_CHUNKS = int(os.environ.get("MAX_CHUNKS", "0"))
BENCH_WARMUP_CHUNKS = int(os.environ.get("BENCH_WARMUP_CHUNKS", "0"))
BENCH_MEASURE_CHUNKS = int(os.environ.get("BENCH_MEASURE_CHUNKS", "0"))
# BENCH_PHASES=1 adds per-phase CUDA-event timing inside the measured chunks.
# It inserts extra events and one host sync per chunk, so it is DIAGNOSTIC
# ONLY: never read chunk_wall_ms from a BENCH_PHASES run as the metric M.
BENCH_PHASES = int(os.environ.get("BENCH_PHASES", "0")) != 0
# SMOKE_TELEMETRY=1 prints a per-chunk reward/loss/grad line used by the
# 30-chunk correctness smoke (compare_smoke.py).
SMOKE_TELEMETRY = int(os.environ.get("SMOKE_TELEMETRY", "0")) != 0
# ---- candidate switches (each is an A/B against the same source tree) ----
# FUSED_SAMPLE : one fused Gumbel-argmax + gather over the concatenated head
#   logits instead of 9 torch.distributions.Categorical objects (rollout
#   sampling + logprob, and the update-side logprob/entropy).
#   DEFAULT ON - this is the one kept candidate of the flywheelopt lane:
#   1715.6 -> 1590.4 ms/chunk at the pinned M config (+7.3%, 458.4k ->
#   494.5k env-ticks/s). It wins by deleting host synchronisation, not by
#   doing less arithmetic: each Categorical's argument validation ends in a
#   `.all()` read back to the host, 9 per forward x 81 forwards per chunk,
#   and each one drains the pipeline. GPU busy time is unchanged (1540 ->
#   1534 ms measured by nsys); the whole gain is recovered idle.
#   Set FUSED_SAMPLE=0 for the pre-lane path. NOTE: the two paths consume
#   different RNG streams, so a run is not replayable across the flag even
#   at the same RNG_SEED - see blaze/rl/flywheel/RNG_PROTOCOL.md.
FUSED_SAMPLE = int(os.environ.get("FUSED_SAMPLE", "1")) != 0
# GRAPH_ROLLOUT=1 : capture the rollout inference step (obs float convert +
#                   policy forward + sample + logprob + action-row decode)
#                   into a CUDA graph replayed per decision.
GRAPH_ROLLOUT = int(os.environ.get("GRAPH_ROLLOUT", "0")) != 0
# GRAPH_UPDATE=1  : capture one PPO minibatch step (fwd + bwd + Adam) into a
#                   CUDA graph. Requires a full-size (MB) minibatch; the
#                   ragged tail minibatch runs eagerly.
GRAPH_UPDATE = int(os.environ.get("GRAPH_UPDATE", "0")) != 0
# ACT_CACHE=1     : cache the yaw/pitch/forward lookup tensors instead of
#                   rebuilding them from Python tuples every decision.
ACT_CACHE = int(os.environ.get("ACT_CACHE", "0")) != 0
# DIST_NOVALIDATE=1: turn off torch.distributions argument validation (its
#                   support check ends in a host-synchronizing .all()).
DIST_NOVALIDATE = int(os.environ.get("DIST_NOVALIDATE", "0")) != 0
# CHANNELS_LAST=1 : policy net + conv-path activations in torch.channels_last
#                   (NHWC). Default OFF until the chanlast A/B keeps it.
#                   CHANNELS_LAST=0 is the current NCHW path, bit-identical.
CHANNELS_LAST = int(os.environ.get("CHANNELS_LAST", "0")) != 0
# CPOLICY=1: rollout policy forward + sample via the C/CUDA path in
#   blaze/rl/cpolicy/ (uint8 obs -> conv -> fc -> heads -> Gumbel/greedy).
#   Update stays torch. Weights uploaded from the torch net once per chunk.
#   DEFAULT OFF. Sampling RNG is independent of torch (see
#   blaze/rl/cpolicy/RNG_PROTOCOL.md). Incompatible with GRAPH_ROLLOUT.
CPOLICY = int(os.environ.get("CPOLICY", "0")) != 0
# CUDNN_BENCH=1   : torch.backends.cudnn.benchmark = True. Static shapes at
#                   the pinned config, so algo autotune amortizes over warmup.
CUDNN_BENCH = int(os.environ.get("CUDNN_BENCH", "0")) != 0
if CUDNN_BENCH:
    torch.backends.cudnn.benchmark = True
# FOLD_SCALE=1    : skip the obs_float depth-plane /=255 elementwise and fold
#                   that scale into a view of conv1's weights (depth input
#                   channels pre-multiplied by 1/255; bias unchanged). The
#                   effective weight is `W * scale_mask` so autograd maps
#                   gradients back onto the canonical unscaled parameters
#                   and Adam still steps the stored weights correctly.
FOLD_SCALE = int(os.environ.get("FOLD_SCALE", "0")) != 0
# TF32_CONV=1     : enable cuDNN + matmul TF32. CHANGES NUMERICS: keep only
#                   if check_correctness.sh still PASSes at existing
#                   tolerances (never loosen them). Default 0.
TF32_CONV = int(os.environ.get("TF32_CONV", "0")) != 0
if TF32_CONV:
    torch.backends.cudnn.allow_tf32 = True
    torch.backends.cuda.matmul.allow_tf32 = True
# A CUDA graph cannot contain a pageable H2D copy and cannot contain a host
# sync, so graph capture forces both of the above on.
if GRAPH_ROLLOUT or GRAPH_UPDATE:
    ACT_CACHE = True
    DIST_NOVALIDATE = True
if CPOLICY and GRAPH_ROLLOUT:
    raise RuntimeError("CPOLICY=1 is incompatible with GRAPH_ROLLOUT=1")

# ---- action heads ----
# dyaw{-15,0,15} dpitch{-10,0,10} fwd{-1,0,1} jump attack use
# craft{none,0..5} interact hotbar{none,0..8}
# IRON_CHAIN=1 widens craft to {none,0..7} (furnace, iron pick) and appends
# a smelt{0,1} head. Default off: HEADS (and any saved net, e.g.
# chain_net_cu_v2.pt) stay byte-compatible with the stone chain.
IRON_CHAIN = int(os.environ.get("IRON_CHAIN", "0")) != 0
HEADS = [3, 3, 3, 2, 2, 2, (9 if IRON_CHAIN else 7), 2, 10] \
    + ([2] if IRON_CHAIN else [])
NHEAD = len(HEADS)
YAWS = (-15.0, 0.0, 15.0)
PITCHES = (-10.0, 0.0, 10.0)
FWD = (-1.0, 0.0, 1.0)

# ---- obs ----
NPLANES = 9        # log leaves coal stone dirt table solid depth edge
NCH = NPLANES * STACK
# status columns (blaze_fill_status): 9 inv counts + hotbar_sel + held +
# container; inv order = rl_inv_ids
IX_LOG, IX_PLANK, IX_STICK, IX_COBBLE, IX_TABLE, IX_WPICK, IX_SPICK, \
    IX_COAL, IX_TORCH = range(9)
SEL_ITEMS = (17, 5, 280, 4, 58, 270, 274, 263, 50)   # one-hot categories
NSCAL = 6 + 9 + 1 + len(SEL_ITEMS) + 1 + 1           # 27
# milestone stages: 0=t0 1=logs3 2=wpick 3=cobble3 4=coal
# IRON_CHAIN extends: 5=torch 6=furnace 7=ironore 8=ingot  (success=ipick ends)
N_STAGES = 9 if IRON_CHAIN else 5
MILE_NAMES = (("t0", "logs3", "pick", "cobble3", "coal", "torch",
               "furnace", "ironore", "ingot") if IRON_CHAIN
              else ("t0", "logs3", "pick", "cobble3", "coal"))

CX, CY = 32, 18    # crosshair pixel


def build_frame(cam, depth, edge):
    """[N,9,36,64] uint8 planes. Transferable: pure function of the BOLR
    cam/depth/edge fields (identical in blaze and the real env)."""
    return torch.stack([
        (cam == 17).to(torch.uint8),                    # log
        (cam == 18).to(torch.uint8),                    # leaves
        (cam == 16).to(torch.uint8),                    # coal ore
        ((cam == 1) | (cam == 4)).to(torch.uint8),      # stone/cobble
        ((cam == 2) | (cam == 3)).to(torch.uint8),      # grass/dirt
        (cam == 58).to(torch.uint8),                    # crafting table
        (cam != 0).to(torch.uint8),                     # solid
        depth, edge], dim=1)


def build_scal(scal, status, pose, tfrac):
    """[N,28] float32 policy scalars: env coal scalars, inv counts,
    container, held-item one-hot, hotbar flag-free extras. All fields exist
    identically in the real env's BOLR record."""
    inv = status[:, :9].float().clamp(max=10.0) / 10.0
    held = status[:, 10]
    onehot = torch.stack([(held == i).float() for i in SEL_ITEMS], dim=1)
    cont = (status[:, 11] > 0).float().unsqueeze(1)
    y = (pose[:, 1] / 64.0).unsqueeze(1)
    return torch.cat([scal, inv, cont, onehot, y,
                      tfrac.unsqueeze(1)], dim=1)


class PhaseTimer:
    """CUDA-event phase accumulator (BENCH_PHASES only).

    Records a start/end event pair per `range()` and sums elapsed_time per
    name at the end of the chunk. Events are pooled so a 32-decision chunk
    does not allocate 300 of them per chunk. DIAGNOSTIC ONLY: the extra
    events perturb the chunk wall, so a BENCH_PHASES run's chunk_wall_ms is
    never the scoreboard metric."""

    def __init__(self, enabled):
        self.enabled = enabled
        self.pairs = []
        self.pool = []
        self._i = 0

    def reset(self):
        self.pairs = []
        self._i = 0

    def _ev(self):
        if self._i >= len(self.pool):
            self.pool.append(torch.cuda.Event(enable_timing=True))
        e = self.pool[self._i]
        self._i += 1
        return e

    @contextlib.contextmanager
    def range(self, name):
        if not self.enabled:
            yield
            return
        a, b = self._ev(), self._ev()
        a.record()
        try:
            yield
        finally:
            b.record()
            self.pairs.append((name, a, b))

    def report(self):
        if not self.enabled or not self.pairs:
            return {}
        torch.cuda.synchronize()
        acc = {}
        for name, a, b in self.pairs:
            acc[name] = acc.get(name, 0.0) + a.elapsed_time(b)
        return acc


# ---- fused multi-head categorical sampling (FUSED_SAMPLE) ----
# The 9 heads have different widths, so "concatenate the logits" needs a
# padded [N, NHEAD, WMAX] block with -inf in the pad columns. Once padded,
# sampling all 9 heads is one Gumbel-argmax over the last dim and the joint
# log-prob is one log_softmax + gather + sum: 3 kernels for the whole action
# instead of ~9 Categorical objects x (validate, sample, log_prob, entropy).
WMAX = max(HEADS)
_HEAD_MASK = None                 # [NHEAD, WMAX] bool, True on real columns


def head_mask(dev):
    global _HEAD_MASK
    if _HEAD_MASK is None or _HEAD_MASK.device != dev:
        m = torch.zeros(NHEAD, WMAX, dtype=torch.bool, device=dev)
        for h, w in enumerate(HEADS):
            m[h, :w] = True
        _HEAD_MASK = m
    return _HEAD_MASK


def pad_logits(logits):
    """list of NHEAD [N,w_h] -> [N,NHEAD,WMAX] with -inf padding."""
    n = logits[0].shape[0]
    out = logits[0].new_full((n, NHEAD, WMAX), float("-inf"))
    for h, lg in enumerate(logits):
        out[:, h, :HEADS[h]] = lg
    return out


def fused_rollout(logits, burnin, noop9):
    """Sample every head at once, force the burn-in lanes to the no-op row,
    and return (acts [N,NHEAD] int64, logp [N]) for the FINAL acts.

    Gumbel-argmax is exactly categorical sampling: argmax_i (logit_i + G_i)
    with G_i iid Gumbel(0,1) has p_i proportional to exp(logit_i). Pad
    columns are -inf so they can never win. Ordering matches the eager path
    (sample -> burn-in override -> log_prob of the overwritten row); see
    blaze/rl/flywheel/RNG_PROTOCOL.md."""
    lp = pad_logits(logits)
    u = torch.rand(lp.shape, device=lp.device, dtype=lp.dtype)
    # Exp(1) = -log U, then Gumbel(0,1) = -log Exp(1). Both clamps guard the
    # log's zero end; note the parentheses -- `-torch.log(u).clamp_min(e)`
    # clamps the NEGATED value and hands log() a negative argument, which
    # yields NaN everywhere and an argmax that always returns category 0.
    e = (-torch.log(u.clamp_min(1e-20))).clamp_min(1e-20)
    g = -torch.log(e)
    acts = (lp + g).argmax(dim=2)
    acts = torch.where(burnin.unsqueeze(1), noop9.unsqueeze(0), acts)
    logp = torch.log_softmax(lp, dim=2).gather(
        2, acts.unsqueeze(2)).squeeze(2).sum(dim=1)
    return acts, logp


def fused_logp_entropy(logits, acts):
    """Joint log-prob of `acts` [N,NHEAD] and summed per-head entropy, in
    3 kernels instead of 9 Categorical objects."""
    lp = pad_logits(logits)
    ls = torch.log_softmax(lp, dim=2)
    logp = ls.gather(2, acts.unsqueeze(2)).squeeze(2).sum(dim=1)
    p = ls.exp()
    ent = -(p * torch.nan_to_num(ls, neginf=0.0)).sum(dim=2).sum(dim=1)
    return logp, ent


def eager_sample(logits, burnin, noop9, where=False):
    """The unmodified 9-Categorical rollout sampler.

    where=True swaps the boolean-mask burn-in write for the identical
    torch.where form; masked assignment is a data-dependent shape and
    therefore cannot be captured into a CUDA graph."""
    dists = [torch.distributions.Categorical(logits=lg) for lg in logits]
    acts = torch.stack([d.sample() for d in dists], dim=1)
    if where:
        acts = torch.where(burnin.unsqueeze(1), noop9.unsqueeze(0), acts)
    else:
        acts[burnin] = noop9
    logp = sum(d.log_prob(acts[:, h]) for h, d in enumerate(dists))
    return acts, logp


class RolloutStep:
    """One decision's inference: obs convert -> policy forward -> head
    sampling -> action-row decode.

    With graph=True the whole sequence is captured once into a CUDA graph and
    replayed per decision. The shapes are static (N_ENVS never changes), so
    the only per-decision inputs that must be staged are the scalars and the
    burn-in mask; the observation stack is read in place (its storage is
    allocated once and only ever mutated through slice writes, which is
    checked against the captured pointer on every call).

    With cpolicy set (CPOLICY=1) the torch path is replaced by the C/CUDA
    fused forward in blaze/rl/cpolicy/. Weights must be re-uploaded after
    every optimizer step via upload_weights(); the trainer does this once
    per chunk before the rollout loop."""

    def __init__(self, net, dev, noop9, graph=False, cpolicy=None):
        self.net, self.dev, self.noop9 = net, dev, noop9
        self.graph = graph
        self.cpolicy = cpolicy
        self.g = None

    def upload_weights(self):
        if self.cpolicy is not None:
            self.cpolicy.upload_from_net(self.net)

    def _body(self):
        logits, vals = self.net(obs_float(self.i_obs, self.f_obs),
                                self.i_scal)
        if FUSED_SAMPLE:
            acts, logp = fused_rollout(logits, self.i_burnin, self.noop9)
        else:
            acts, logp = eager_sample(logits, self.i_burnin, self.noop9,
                                      where=True)
        return acts, logp, vals, acts_to_rows(acts, self.dev)

    def _capture(self, stack_u8, scal, burnin):
        self.i_obs = stack_u8
        self._obs_ptr = stack_u8.data_ptr()
        self.i_scal = scal.detach().clone()
        self.i_burnin = burnin.detach().clone()
        if CHANNELS_LAST:
            self.f_obs = torch.empty(
                stack_u8.shape, dtype=torch.float32, device=self.dev,
                memory_format=torch.channels_last)
        else:
            self.f_obs = torch.empty(stack_u8.shape, dtype=torch.float32,
                                     device=self.dev)
        s = torch.cuda.Stream(device=self.dev)
        s.wait_stream(torch.cuda.current_stream(self.dev))
        with torch.cuda.stream(s), torch.no_grad():
            for _ in range(3):
                self._body()
        torch.cuda.current_stream(self.dev).wait_stream(s)
        self.g = torch.cuda.CUDAGraph()
        with torch.cuda.graph(self.g), torch.no_grad():
            self.o = self._body()
        print(f"GRAPH rollout captured (N={stack_u8.shape[0]}, "
              f"fused={FUSED_SAMPLE})", flush=True)

    def step(self, stack_u8, scal, burnin, pt):
        if self.cpolicy is not None:
            with torch.no_grad():
                with pt.range("rollout/cpolicy"):
                    acts, logp, vals, _ent = self.cpolicy.forward_sample(
                        stack_u8, scal, burnin, self.noop9, mode=0)
                with pt.range("rollout/act_decode"):
                    rows = acts_to_rows(acts, self.dev)
                return acts, logp, vals, rows
        if not self.graph:
            with torch.no_grad():
                with pt.range("rollout/obs_build"):
                    obs = obs_float(stack_u8)
                with pt.range("rollout/policy_fwd"):
                    logits, vals = self.net(obs, scal)
                with pt.range("rollout/sample"):
                    if FUSED_SAMPLE:
                        acts, logp = fused_rollout(logits, burnin, self.noop9)
                    else:
                        acts, logp = eager_sample(logits, burnin, self.noop9)
                with pt.range("rollout/act_decode"):
                    rows = acts_to_rows(acts, self.dev)
                return acts, logp, vals, rows
        if self.g is None:
            self._capture(stack_u8, scal, burnin)
        elif stack_u8.data_ptr() != self._obs_ptr:
            raise RuntimeError(
                "rollout graph captured a different obs allocation; the "
                "observation stack must be mutated in place")
        with pt.range("rollout/graph"):
            self.i_scal.copy_(scal)
            self.i_burnin.copy_(burnin)
            self.g.replay()
        return self.o


class UpdateStep:
    """One PPO minibatch (forward + backward + grad clip + Adam) captured
    into a CUDA graph.

    Only full-size (MB) minibatches go through the graph; the ragged tail
    stays eager. The minibatch gather is done with index_select(out=) into
    the graph's static input buffers, so it REPLACES the eager path's gather
    rather than adding a copy on top of it.

    Warm-up before capture runs three real optimizer steps (cuDNN/Adam state
    has to exist and be laid out before the capture records its pointers), so
    the parameters and the whole optimizer state are snapshotted first and
    restored in place afterwards: capture itself records without executing,
    so the net that comes out is bit-identical to the net that went in."""

    def __init__(self, net, opt, dev, mb_size):
        self.net, self.opt, self.dev, self.mb = net, opt, dev, mb_size
        self.g = None
        self.lr_t = None

    def _alloc(self, fO, fS, fA):
        m = self.mb
        self.s_obs = torch.empty((m,) + fO.shape[1:], dtype=fO.dtype,
                                 device=self.dev)
        if CHANNELS_LAST:
            self.f_obs = torch.empty(
                (m,) + fO.shape[1:], dtype=torch.float32, device=self.dev,
                memory_format=torch.channels_last)
        else:
            self.f_obs = torch.empty((m,) + fO.shape[1:], dtype=torch.float32,
                                     device=self.dev)
        self.s_scal = torch.empty((m, fS.shape[1]), dtype=fS.dtype,
                                  device=self.dev)
        self.s_act = torch.empty((m, fA.shape[1]), dtype=fA.dtype,
                                 device=self.dev)
        self.s_lp = torch.empty(m, dtype=torch.float32, device=self.dev)
        self.s_adv = torch.empty(m, dtype=torch.float32, device=self.dev)
        self.s_ret = torch.empty(m, dtype=torch.float32, device=self.dev)

    def _stage(self, fO, fS, fA, fLP, fADV, fRET, mb, mo):
        torch.index_select(fO, 0, mb, out=self.s_obs)
        torch.index_select(fS, 0, mb, out=self.s_scal)
        torch.index_select(fA, 0, mb, out=self.s_act)
        torch.index_select(fLP, 0, mb, out=self.s_lp)
        torch.index_select(fADV, 0, mo, out=self.s_adv)
        torch.index_select(fRET, 0, mo, out=self.s_ret)

    def _body(self):
        logits, vals = self.net(obs_float(self.s_obs, self.f_obs),
                                self.s_scal)
        if FUSED_SAMPLE:
            logp, entr = fused_logp_entropy(logits, self.s_act)
        else:
            dists = [torch.distributions.Categorical(logits=lg)
                     for lg in logits]
            logp = sum(d.log_prob(self.s_act[:, h])
                       for h, d in enumerate(dists))
            entr = sum(d.entropy() for d in dists)
        ratio = torch.exp(logp - self.s_lp)
        pg = -torch.min(ratio * self.s_adv,
                        torch.clamp(ratio, 1 - CLIP, 1 + CLIP) * self.s_adv)
        loss = pg.mean() + 0.5 * ((self.s_ret - vals) ** 2).mean() \
            - ENT * entr.mean()
        self.opt.zero_grad(set_to_none=False)
        loss.backward()
        torch.nn.utils.clip_grad_norm_(self.net.parameters(), GRAD_CLIP)
        self.opt.step()
        return ratio.detach(), entr.mean().detach()

    def _snapshot(self):
        st = [p.detach().clone() for p in self.net.parameters()]
        ost = []
        for p in self.net.parameters():
            s = self.opt.state.get(p, {})
            ost.append({k: (v.detach().clone() if torch.is_tensor(v) else v)
                        for k, v in s.items()})
        return st, ost

    def _restore(self, snap):
        """Undo the warm-up. Keys the snapshot does NOT have are keys the
        optimizer had not created yet, so they are zeroed rather than left
        alone: leaving them is how a graphed update silently starts life with
        three steps of momentum the eager path never took."""
        st, ost = snap
        with torch.no_grad():
            for p, q in zip(self.net.parameters(), st):
                p.copy_(q)
            for p, s in zip(self.net.parameters(), ost):
                cur = self.opt.state.get(p, {})
                for k, v in cur.items():
                    if k in s:
                        if torch.is_tensor(v) and torch.is_tensor(s[k]):
                            v.copy_(s[k])
                        else:
                            cur[k] = s[k]
                    elif torch.is_tensor(v):
                        v.zero_()
                    else:
                        cur[k] = 0

    def capture(self, fO, fS, fA, fLP, fADV, fRET, mb, mo):
        self._alloc(fO, fS, fA)
        self._stage(fO, fS, fA, fLP, fADV, fRET, mb, mo)
        snap = self._snapshot()
        s = torch.cuda.Stream(device=self.dev)
        s.wait_stream(torch.cuda.current_stream(self.dev))
        with torch.cuda.stream(s):
            for _ in range(3):
                self._body()
        torch.cuda.current_stream(self.dev).wait_stream(s)
        self.g = torch.cuda.CUDAGraph()
        with torch.cuda.graph(self.g):
            self.o_ratio, self.o_ent = self._body()
        self._restore(snap)
        print(f"GRAPH update captured (MB={self.mb}, fused={FUSED_SAMPLE})",
              flush=True)

    def step(self, fO, fS, fA, fLP, fADV, fRET, mb, mo, lr_now):
        if self.g is None:
            self.capture(fO, fS, fA, fLP, fADV, fRET, mb, mo)
        else:
            self._stage(fO, fS, fA, fLP, fADV, fRET, mb, mo)
        if self.lr_t is not None:
            self.lr_t.fill_(lr_now)
        self.g.replay()
        return self.o_ratio, self.o_ent


_OF_BUF = {}


def obs_float(u8, buf=None):
    """uint8 planes -> float32, depth scaled /255. ONE growable buffer per
    device (only dim0 varies across calls); a [:n] view is returned. A
    per-shape cache is a LEAK: ragged last-minibatch sizes mint a new
    multi-GB buffer per distinct n (v1 of this pinned ~10GB/25 chunks and
    OOMed the pin run). Safe because fwd consumes the view before the next
    copy_ overwrites (in-order on one stream).

    `buf` supplies a caller-owned destination. CUDA-graph capture NEEDS this:
    the shared buffer is grown by the first MB-sized update call, and a grow
    frees the storage a rollout graph captured at N_ENVS, which would leave
    the graph replaying into freed memory with no error at all.

    CHANNELS_LAST=1: the float buffer is allocated (or converted) in
    torch.channels_last so the conv stack sees NHWC activations without a
    per-call transpose tax. Callers that own `buf` (CUDA-graph static
    inputs) must pre-allocate it channels_last too, so capture does not
    allocate on the hot path.

    FOLD_SCALE=1: skip the depth /=255 here; ChainPolicy folds that factor
    into conv1's depth input channels instead (mathematically equivalent)."""
    n = u8.shape[0]
    if buf is None:
        key = (u8.device, u8.shape[1:], CHANNELS_LAST)
        buf = _OF_BUF.get(key)
        if buf is None or buf.shape[0] < n:
            if CHANNELS_LAST:
                buf = torch.empty(
                    (n,) + u8.shape[1:], dtype=torch.float32,
                    device=u8.device, memory_format=torch.channels_last)
            else:
                buf = torch.empty((n,) + u8.shape[1:], dtype=torch.float32,
                                  device=u8.device)
            _OF_BUF[key] = buf
    out = buf[:n]
    out.copy_(u8, non_blocking=True)
    if not FOLD_SCALE:
        for k in range(STACK):
            out[:, NPLANES * k + 7] /= 255.0      # depth plane
    if CHANNELS_LAST and not out.is_contiguous(
            memory_format=torch.channels_last):
        out = out.contiguous(memory_format=torch.channels_last)
    return out


def _depth_wscale(device=None, dtype=torch.float32):
    """[1, NCH, 1, 1] multiplier: 1/255 on depth input channels, 1 elsewhere.

    Multiplying conv1.weight by this and feeding unscaled float(obs) is
    algebraically identical to the unscaled-weight / scaled-obs path, and
    because the scale is a constant graph edge autograd writes the correct
    gradient onto the stored (canonical) weight."""
    s = torch.ones(NCH, device=device, dtype=dtype)
    for k in range(STACK):
        s[NPLANES * k + 7] = 1.0 / 255.0
    return s.view(1, -1, 1, 1)


class ChainPolicy(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv = nn.Sequential(
            nn.Conv2d(NCH, 32, 5, stride=2), nn.ReLU(),
            nn.Conv2d(32, 64, 3, stride=2), nn.ReLU(), nn.Flatten())
        with torch.no_grad():
            z = torch.zeros(1, NCH, CAM_H, CAM_W)
            if CHANNELS_LAST:
                z = z.contiguous(memory_format=torch.channels_last)
            nflat = self.conv(z).shape[1]
        self.fc = nn.Sequential(nn.Linear(nflat + NSCAL, 256), nn.ReLU())
        self.heads = nn.ModuleList([nn.Linear(256, n) for n in HEADS])
        self.value = nn.Linear(256, 1)
        # Non-persistent: not part of the checkpoint; rebuilt on load.
        self.register_buffer("_depth_wscale", _depth_wscale(),
                             persistent=False)
        if CHANNELS_LAST:
            # Conv weights in NHWC; .to(device) preserves this format.
            self.to(memory_format=torch.channels_last)

    def _conv_fwd(self, p):
        if not FOLD_SCALE:
            return self.conv(p)
        c1 = self.conv[0]
        # buffer tracks .to(device) with the module; multiply is a tiny
        # [32,NCH,5,5] elementwise vs the obs depth-plane scale it replaces.
        w = c1.weight * self._depth_wscale
        h = F.conv2d(p, w, c1.bias, stride=c1.stride, padding=c1.padding,
                     dilation=c1.dilation, groups=c1.groups)
        # conv Sequential tail: ReLU, Conv2d, ReLU, Flatten
        return self.conv[1:](h)

    def forward(self, p, s):
        if CHANNELS_LAST and not p.is_contiguous(
                memory_format=torch.channels_last):
            p = p.contiguous(memory_format=torch.channels_last)
        h = self.fc(torch.cat([self._conv_fwd(p), s], dim=1))
        return [head(h) for head in self.heads], self.value(h).squeeze(-1)


_ROW_CONST = {}


def row_consts(dev):
    """Cached [3] float64 lookup tables for the yaw/pitch/forward heads.

    The uncached path rebuilds three tensors from Python tuples on EVERY
    decision, i.e. three pageable host-to-device copies per decision, and a
    pageable H2D copy is a synchronizing copy. Also required for CUDA-graph
    capture (a pageable copy cannot be captured)."""
    c = _ROW_CONST.get(dev)
    if c is None:
        c = tuple(torch.tensor(v, dtype=torch.float64, device=dev)
                  for v in (YAWS, PITCHES, FWD))
        _ROW_CONST[dev] = c
    return c


def acts_to_rows(a, dev):
    """[N,NHEAD] int64 head samples -> [N,13] float64 raw action rows."""
    n = a.shape[0]
    rows = torch.zeros((n, 13), dtype=torch.float64, device=dev)
    if ACT_CACHE:
        t_yaw, t_pit, t_fwd = row_consts(dev)
    else:
        t_yaw = torch.tensor(YAWS, dtype=torch.float64, device=dev)
        t_pit = torch.tensor(PITCHES, dtype=torch.float64, device=dev)
        t_fwd = torch.tensor(FWD, dtype=torch.float64, device=dev)
    rows[:, 2] = t_yaw[a[:, 0]]
    rows[:, 3] = t_pit[a[:, 1]]
    rows[:, 0] = t_fwd[a[:, 2]]
    rows[:, 4] = a[:, 3].double()                    # jump
    rows[:, 7] = a[:, 4].double()                    # attack
    rows[:, 8] = a[:, 5].double()                    # use
    rows[:, 10] = a[:, 6].double() - 1.0             # craft: 0->-1
    rows[:, 11] = a[:, 7].double()                   # interact
    rows[:, 9] = a[:, 8].double() - 1.0              # hotbar: 0->-1
    if IRON_CHAIN:
        rows[:, 12] = a[:, 9].double()               # smelt
    return rows


# ---- static log oracle (reward only, never a policy input) ----
HEAD_SIZE, N_ITEMS_OFF, RDIMS_OFF, ITEM_SIZE = 752, 724, 728, 76


def snapshot_logs(path):
    """World coords of every log (id 17) in a .bsnp region."""
    with open(path, "rb") as f:
        buf = f.read()
    assert buf[:4] == b"BSNP", path
    n_items = struct.unpack_from("<I", buf, N_ITEMS_OFF)[0]
    rx0, ry0, rz0, rnx, rny, rnz = struct.unpack_from("<6i", buf, RDIMS_OFF)
    cells = np.frombuffer(buf, "<u2", rnx * rny * rnz,
                          HEAD_SIZE + ITEM_SIZE * n_items)
    ids = (cells >> 4).reshape(rnx, rny, rnz)
    ix, iy, iz = np.nonzero(ids == 17)
    return np.stack([ix + rx0 + 0.5, iy + ry0 + 0.5, iz + rz0 + 0.5],
                    axis=1).astype(np.float32)


def stage_of_best(best, best_iron=None):
    """[N] int64 milestone stage from the BEST-SO-FAR counters (instantaneous
    counts drop when crafts consume items; milestones must not regress).
    best_iron: optional [N,4] furnace/ironore/ingot/ipick bests when IRON_CHAIN."""
    st = torch.zeros(best.shape[0], dtype=torch.int64, device=best.device)
    st[(best[:, IX_LOG] >= 3) | (best[:, IX_PLANK] >= 1)] = 1
    st[best[:, IX_WPICK] >= 1] = 2
    st[(best[:, IX_WPICK] >= 1) & (best[:, IX_COBBLE] >= 3)] = 3
    st[best[:, IX_COAL] >= 1] = 4
    if IRON_CHAIN and best_iron is not None:
        st[best[:, IX_TORCH] >= 1] = 5
        st[best_iron[:, 0] >= 1] = 6          # furnace item
        st[best_iron[:, 1] >= 1] = 7          # iron ore
        st[best_iron[:, 2] >= 1] = 8          # ingot
    return st


class StageCurriculum:
    """Per-seed frontier sampling over the milestone stages.

    Availability: stage 0 (t0) always; stages 1..4 once the policy has
    captured a state there. Sampling: T0_SHARE of lanes start at t0; the
    rest go to the seed's frontier stage (earliest available stage whose
    next-milestone success is < MASTER) with rehearsal spread."""
    MASTER = 0.6
    MIN_EPS = 15
    WIN = 60

    def __init__(self, nseeds, rng):
        self.rng = rng
        self.nseeds = nseeds
        self.avail = np.zeros((nseeds, N_STAGES), dtype=bool)
        self.avail[:, 0] = True
        self.hist = [[deque(maxlen=self.WIN) for _ in range(N_STAGES)]
                     for _ in range(nseeds)]

    def record(self, seed_i, stage, ok):
        self.hist[seed_i][stage].append(float(ok))

    def succ(self, seed_i, stage):
        h = self.hist[seed_i][stage]
        return (sum(h) / len(h)) if h else 0.0

    def sample(self, k):
        seeds = self.rng.integers(0, self.nseeds, size=k)
        stages = np.zeros(k, dtype=np.int64)
        for j in range(k):
            si = seeds[j]
            if self.rng.random() < T0_SHARE:
                continue                      # t0
            av = [s for s in range(N_STAGES) if self.avail[si, s]]
            frontier = av[-1]
            for s in av:
                h = self.hist[si][s]
                if len(h) < self.MIN_EPS or \
                        sum(h) / len(h) < self.MASTER:
                    frontier = s
                    break
            w = np.full(len(av), 0.15)
            w[av.index(frontier)] += 1.0 - 0.15 * len(av)
            stages[j] = self.rng.choice(av, p=w / w.sum())
        return seeds, stages

    def matrix(self):
        return " | ".join(
            "".join(f"{self.succ(si, s):.2f} " if self.avail[si, s]
                    else " --  " for s in range(N_STAGES))
            for si in range(self.nseeds))


def main():
    os.makedirs(OUT, exist_ok=True)
    if DIST_NOVALIDATE:
        torch.distributions.Distribution.set_default_validate_args(False)
    torch.manual_seed(RNG_SEED)
    rng = np.random.default_rng(RNG_SEED)
    dev = torch.device(f"cuda:{DEVICE}")
    print(f"device cuda:{DEVICE} = {torch.cuda.get_device_name(DEVICE)}",
          flush=True)
    nseeds = len(TRAIN_SEEDS)

    paths = [os.path.join(SNAPS, f"s{s}_t0.bsnp") for s in TRAIN_SEEDS]
    for p in paths:
        assert os.path.exists(p), p

    env = VecBlaze(N_ENVS, device=DEVICE, so_path=CUDA_SO)
    env.set_success_item(SUCCESS_ITEM)        # default 50 torch; 274=spick abuse
    print(f"success_item={SUCCESS_ITEM}  REWARD_JSON={os.environ.get('REWARD_JSON')}",
          flush=True)
    env.load_snapshots(paths)

    # capture slots: seed_i x stage(1..4) -> fixed slot id; pre-seed each
    # slot with the seed's t0 state so slot ids exist before first capture
    def cap_slot(si, stage):
        return nseeds + si * (N_STAGES - 1) + (stage - 1)

    env.assign([i % nseeds for i in range(N_ENVS)])
    env.reset()
    for si in range(nseeds):
        for stg in range(1, N_STAGES):
            env.capture(si, cap_slot(si, stg))   # lane si holds seed si's t0

    # static log oracle per seed, padded [S, Lmax, 3]
    logs = [snapshot_logs(p) for p in paths]
    lmax = max(len(x) for x in logs)
    log_pad = np.full((nseeds, lmax, 3), 1e9, dtype=np.float32)
    for i, x in enumerate(logs):
        log_pad[i, :len(x)] = x
    log_t = torch.as_tensor(log_pad, device=dev)
    print(f"log oracle: {[len(x) for x in logs]} logs/seed", flush=True)

    net = ChainPolicy().to(dev)
    if WARM:
        net.load_state_dict(torch.load(os.path.join(OUT, WARM),
                                       weights_only=True, map_location=dev))
        print(f"warm start from {WARM}", flush=True)
    if CHANNELS_LAST:
        # Re-assert after .to(dev) / load_state_dict (both preserve format
        # for conv weights in current torch, but make the intent explicit).
        net.to(memory_format=torch.channels_last)
    if GRAPH_UPDATE:
        # capturable=True keeps Adam's step counter on the device (the CPU
        # counter is a host read, which cannot be captured), and lr must be a
        # device tensor so the per-chunk decay can be written without
        # re-capturing the graph.
        lr_t = torch.tensor(LR, dtype=torch.float32, device=dev)
        opt = torch.optim.Adam(net.parameters(), lr=lr_t, capturable=True)
    else:
        lr_t = None
        opt = torch.optim.Adam(net.parameters(), lr=LR)

    curr = StageCurriculum(nseeds, rng)
    lane_seed = np.array([i % nseeds for i in range(N_ENVS)])
    lane_snap = np.array(lane_seed)                   # snapshot slot per lane
    lane_stage_start = np.zeros(N_ENVS, dtype=np.int64)
    lane_stage = np.zeros(N_ENVS, dtype=np.int64)     # max stage this episode
    env.assign(list(lane_snap))
    env.reset()

    # rolling per-lane state
    stack_u8 = torch.zeros((N_ENVS, NCH, CAM_H, CAM_W), dtype=torch.uint8,
                           device=dev)
    scal_cur = torch.zeros((N_ENVS, NSCAL), dtype=torch.float32, device=dev)
    burnin = torch.ones(N_ENVS, dtype=torch.bool, device=dev)
    ep_dec = torch.randint(0, EP_DEC, (N_ENVS,), dtype=torch.int32,
                           device=dev,
                           generator=torch.Generator(device=dev)
                           .manual_seed(RNG_SEED + 1))
    ep_ret = torch.zeros(N_ENVS, dtype=torch.float32, device=dev)
    lane_seed_t = torch.as_tensor(lane_seed, device=dev)

    # reward bookkeeping (per lane, reset on episode start)
    rew = ChainReward(N_ENVS, dev, REWARD_SPEC)

    # chunk buffers
    obs_b = torch.zeros((T_CHUNK, N_ENVS, NCH, CAM_H, CAM_W),
                        dtype=torch.uint8, device=dev)
    scal_b = torch.zeros((T_CHUNK, N_ENVS, NSCAL), device=dev)
    act_b = torch.zeros((T_CHUNK, N_ENVS, NHEAD), dtype=torch.int64,
                        device=dev)
    logp_b = torch.zeros((T_CHUNK, N_ENVS), device=dev)
    val_b = torch.zeros((T_CHUNK, N_ENVS), device=dev)
    rew_b = torch.zeros((T_CHUNK, N_ENVS), device=dev)
    term_b = torch.zeros((T_CHUNK, N_ENVS), dtype=torch.bool, device=dev)
    cut_b = torch.zeros((T_CHUNK, N_ENVS), dtype=torch.bool, device=dev)
    valid_b = torch.zeros((T_CHUNK, N_ENVS), dtype=torch.bool, device=dev)

    # curriculum capture bookkeeping
    cap_last = np.full((nseeds, N_STAGES), -10**9, dtype=np.int64)
    trail_t0 = deque(maxlen=TRAIL)         # full-chain success from t0
    mile_hist = np.zeros(N_STAGES + 1, dtype=np.int64)  # episodes by reached
    n_eps = 0
    curve = []
    ticks = 0
    next_ckpt = CKPT_TICKS
    chunk = 0
    t_start = time.perf_counter()
    stop_reason = None
    best_t0 = {"v": -1.0, "ticks": 0}

    def save_all():
        v = (sum(trail_t0) / len(trail_t0)) if trail_t0 else 0.0
        if len(trail_t0) >= 50 and v > best_t0["v"]:
            best_t0["v"], best_t0["ticks"] = v, ticks
            torch.save({k: t.cpu() for k, t in net.state_dict().items()},
                       os.path.join(OUT, f"{OUT_NAME}.pt"))
        torch.save({k: t.cpu() for k, t in net.state_dict().items()},
                   os.path.join(OUT, f"{OUT_NAME}_last.pt"))
        np.save(os.path.join(OUT, f"{CURVE_NAME}.npy"),
                np.array(curve, dtype=np.float64))
        REWARD_SPEC.dump(os.path.join(OUT, f"{OUT_NAME}_reward.json"))

    def save_png():
        try:
            import matplotlib
            matplotlib.use("Agg")
            import matplotlib.pyplot as plt
        except ImportError:
            return
        c = np.array(curve)
        if len(c) < 2:
            return
        fig, ax = plt.subplots(figsize=(9, 5))
        for col, lab in ((2, "t0 full chain"), (3, "mean stage succ")):
            ax.plot(c[:, 0] / 1e6, c[:, col], label=lab)
        ax.set_xlabel("env ticks (M)")
        ax.set_ylabel("success")
        ax.set_ylim(-0.02, 1.02)
        ax.legend()
        ax.set_title("PPO spawn-to-torch chain (blaze)")
        fig.tight_layout()
        fig.savefig(os.path.join(OUT, f"{CURVE_NAME}.png"), dpi=140)
        plt.close(fig)

    print(f"N={N_ENVS} lanes, {nseeds} seeds {TRAIN_SEEDS}, "
          f"chunk {T_CHUNK} decisions, EP_DEC {EP_DEC}, device cuda:{DEVICE}",
          flush=True)
    print(f"rng seed={RNG_SEED} (torch/numpy), episode seed={RNG_SEED + 1}",
          flush=True)
    print(f"reward spec: {REWARD_SPEC}", flush=True)

    noop9 = torch.zeros(NHEAD, dtype=torch.int64, device=dev)
    noop9[0] = noop9[1] = 1                  # dyaw 0, dpitch 0
    noop9[2] = 1                             # fwd 0

    ptimer = PhaseTimer(BENCH_PHASES)
    cpolicy_fwd = None
    if CPOLICY:
        sys.path.insert(0, os.path.join(RL, "cpolicy"))
        from wrapper import CPolicyFwd  # noqa: E402
        cpolicy_fwd = CPolicyFwd(DEVICE, N_ENVS)
        print(f"CPOLICY=1: cpolicy_fwd.so max_n={N_ENVS} device={DEVICE}",
              flush=True)
    roll = RolloutStep(net, dev, noop9, graph=GRAPH_ROLLOUT,
                       cpolicy=cpolicy_fwd)
    if cpolicy_fwd is not None:
        roll.upload_weights()
    upd = UpdateStep(net, opt, dev, MB) if GRAPH_UPDATE else None
    if upd is not None:
        upd.lr_t = lr_t
    print(f"opt flags: fused_sample={FUSED_SAMPLE} act_cache={ACT_CACHE} "
          f"novalidate={DIST_NOVALIDATE} channels_last={CHANNELS_LAST} "
          f"graph_rollout={GRAPH_ROLLOUT} graph_update={GRAPH_UPDATE} "
          f"cpolicy={CPOLICY} cudnn_bench={CUDNN_BENCH} "
          f"fold_scale={FOLD_SCALE} tf32_conv={TF32_CONV}", flush=True)

    while True:
        bench_sample = BENCH_MEASURE_CHUNKS and \
            BENCH_WARMUP_CHUNKS <= chunk < BENCH_WARMUP_CHUNKS + \
            BENCH_MEASURE_CHUNKS
        if bench_sample:
            ptimer.reset()
            torch.cuda.synchronize(dev)
            bench_t0 = time.perf_counter()
        # weights may have changed in the previous chunk's PPO update
        if cpolicy_fwd is not None:
            roll.upload_weights()
        for t in range(T_CHUNK):
            acts, logp, vals, rows = roll.step(stack_u8, scal_cur, burnin,
                                               ptimer)

            obs_b[t] = stack_u8
            scal_b[t] = scal_cur
            act_b[t] = acts
            logp_b[t] = logp
            val_b[t] = vals
            valid_b[t] = ~burnin

            with ptimer.range("env/step"):
                cam, depth, edge, scal, _env_rew, done, pose = env.step(
                    rows, repeat=REPEAT)
            status = env.status

            # ---- reward (reward_chain.ChainReward; see reward_chain.py) ----
            with ptimer.range("reward"):
                r = rew.step(status, cam, acts, pose, scal, done, lane_seed_t,
                             log_t)

            term = done > 0
            success = done == 1
            ep_dec += (~burnin).int()
            trunc = (~term) & (ep_dec >= EP_DEC)
            ended = term | trunc
            rew_b[t] = r
            term_b[t] = term
            cut_b[t] = ended
            ep_ret += r

            with ptimer.range("stack_roll"):
                frame = build_frame(cam, depth, edge)
                stack_u8[:, :-NPLANES] = stack_u8[:, NPLANES:].clone()
                stack_u8[:, -NPLANES:] = frame
                if burnin.any():
                    bi = burnin
                    stack_u8[bi] = frame[bi].repeat(1, STACK, 1, 1)
                tfrac = ep_dec.float() / EP_DEC
                scal_cur = build_scal(scal, status, pose, tfrac)
                burnin = torch.zeros_like(burnin)

            # ---- milestone captures (policy-generated start states) ----
            ptimer_curr = ptimer.range("curriculum")
            ptimer_curr.__enter__()
            stg_now = stage_of_best(
                rew.best, getattr(rew, "best_iron", None))
            live = ~term
            for stg in range(1, N_STAGES):
                cand = (stg_now == stg) & live & \
                    torch.as_tensor(lane_stage < stg, device=dev)
                if stg == N_STAGES - 1:      # torch needs a stick in hand
                    cand &= status[:, IX_STICK] >= 1
                if not cand.any():
                    continue
                lanes = cand.nonzero(as_tuple=True)[0].cpu().numpy()
                for lane in lanes[:8]:
                    si = int(lane_seed[lane])
                    if chunk - cap_last[si, stg] < CAP_REFRESH:
                        continue
                    env.capture(int(lane), cap_slot(si, stg))
                    cap_last[si, stg] = chunk
                    curr.avail[si, stg] = True
                # only capture-once per (seed,stage) per decision: cheap
                # enough, and lane_stage bump stops repeat triggers
                lane_stage[lanes] = np.maximum(lane_stage[lanes], stg)

            if ended.any():
                idx = ended.nonzero(as_tuple=True)[0]
                idx_h = idx.cpu().numpy()
                stg_h = stg_now[idx].cpu().numpy()
                suc_h = success[idx].cpu().numpy()
                for lane, reach, suc in zip(idx_h, stg_h, suc_h):
                    si = int(lane_seed[lane])
                    st0 = int(lane_stage_start[lane])
                    reached = N_STAGES if suc else int(reach)
                    mile_hist[min(reached, N_STAGES)] += 1
                    # per-(seed,startstage): did it reach the NEXT milestone
                    nxt = st0 + 1
                    ok = bool(suc) if st0 >= N_STAGES - 1 else reached >= nxt
                    curr.record(si, st0, ok)
                    if st0 == 0:
                        trail_t0.append(float(suc))
                n_eps += len(idx_h)

                seeds_n, stages_n = curr.sample(len(idx_h))
                lane_seed[idx_h] = seeds_n
                lane_stage_start[idx_h] = stages_n
                lane_stage[idx_h] = stages_n
                slot = np.where(
                    stages_n == 0, seeds_n,
                    nseeds + seeds_n * (N_STAGES - 1) + (stages_n - 1))
                lane_snap[idx_h] = slot
                env.assign(list(lane_snap))
                mask = np.zeros(N_ENVS, dtype=np.uint8)
                mask[idx_h] = 1
                env.reset(mask)
                lane_seed_t = torch.as_tensor(lane_seed, device=dev)
                ep_dec[idx] = 0
                ep_ret[idx] = 0.0
                burnin[idx] = True
                rew.reset(idx)

            ptimer_curr.__exit__(None, None, None)
            ticks += N_ENVS * REPEAT

        # ---- GAE ----
        with ptimer.range("gae"), torch.no_grad():
            _, next_val = net(obs_float(stack_u8), scal_cur)
            adv = torch.zeros_like(rew_b)
            gae = torch.zeros(N_ENVS, device=dev)
            nextv = next_val
            for t in reversed(range(T_CHUNK)):
                nonterm = (~term_b[t]).float()
                keep = (~cut_b[t]).float()
                delta = rew_b[t] + GAMMA * nextv * nonterm - val_b[t]
                gae = delta + GAMMA * LAM * keep * gae
                adv[t] = gae
                nextv = val_b[t]
            ret = adv + val_b

        # ---- PPO ----
        sel = valid_b.flatten().nonzero(as_tuple=True)[0]
        n_tr = sel.numel()
        ent_sum = kl_sum = clip_sum = 0.0
        upd_n = 0
        if n_tr:
            fO = obs_b.flatten(0, 1)
            fS = scal_b.flatten(0, 1)
            fA = act_b.flatten(0, 1)
            fLP = logp_b.flatten()
            fADV = adv.flatten()[sel]
            fRET = ret.flatten()[sel]
            fADV = (fADV - fADV.mean()) / (fADV.std() + 1e-8)
            lr_now = max(LR_FLOOR,
                         LR * (1.0 - min(1.0, ticks / LR_DECAY_TICKS)
                               * (1.0 - LR_FLOOR / LR)))
            if lr_t is not None:
                lr_t.fill_(lr_now)          # graph-visible device lr
            else:
                for g in opt.param_groups:
                    g["lr"] = lr_now
            for _ in range(EPOCHS):
                perm = sel[torch.randperm(n_tr, device=dev)]
                order = torch.searchsorted(sel, perm)
                for k in range(0, n_tr, MB):
                    mb = perm[k:k + MB]
                    mo = order[k:k + MB]
                    if upd is not None and mb.numel() == MB:
                        with ptimer.range("ppo/mb_graph"):
                            ratio, entr = upd.step(
                                fO, fS, fA, fLP, fADV, fRET, mb, mo, lr_now)
                        ent_sum = ent_sum + entr
                        kl_sum = kl_sum + \
                            (ratio - 1.0 - torch.log(ratio)).mean().detach()
                        clip_sum = clip_sum + \
                            ((ratio - 1.0).abs() > CLIP).float().mean()
                        upd_n += 1
                        continue
                    with ptimer.range("ppo/mb_fwd"):
                        logits, vals = net(obs_float(fO[mb]), fS[mb])
                        if FUSED_SAMPLE:
                            logp, entr = fused_logp_entropy(logits, fA[mb])
                        else:
                            dists = [torch.distributions.Categorical(logits=lg)
                                     for lg in logits]
                            logp = sum(d.log_prob(fA[mb][:, h])
                                       for h, d in enumerate(dists))
                            entr = sum(d.entropy() for d in dists)
                        ratio = torch.exp(logp - fLP[mb])
                        a_mb = fADV[mo]
                        pg = -torch.min(
                            ratio * a_mb,
                            torch.clamp(ratio, 1 - CLIP, 1 + CLIP) * a_mb)
                        loss = pg.mean() \
                            + 0.5 * ((fRET[mo] - vals) ** 2).mean() \
                            - ENT * entr.mean()
                    with ptimer.range("ppo/mb_bwd"):
                        opt.zero_grad()
                        loss.backward()
                        torch.nn.utils.clip_grad_norm_(net.parameters(),
                                                       GRAD_CLIP)
                    with ptimer.range("ppo/mb_opt"):
                        opt.step()
                    with torch.no_grad():
                        # collapse telemetry: policy entropy, approx KL
                        # (E[r-1-log r]), clip fraction; tensor sums, no
                        # host sync until the print
                        ent_sum = ent_sum + entr.mean().detach()
                        kl_sum = kl_sum + \
                            (ratio - 1.0 - torch.log(ratio)).mean().detach()
                        clip_sum = clip_sum + \
                            ((ratio - 1.0).abs() > CLIP).float().mean()
                        upd_n += 1

        if bench_sample:
            torch.cuda.synchronize(dev)
            bench_ms = (time.perf_counter() - bench_t0) * 1000.0
            sample_i = chunk - BENCH_WARMUP_CHUNKS
            sample_ticks = N_ENVS * T_CHUNK * REPEAT
            print(f"BENCH chunk={sample_i} wall_ms={bench_ms:.6f} "
                  f"env_ticks_per_s={sample_ticks / (bench_ms / 1000.0):.3f}",
                  flush=True)
            ph = ptimer.report()
            if ph:
                print("BENCH_PHASES " + json.dumps(
                    {k: round(v, 4) for k, v in sorted(ph.items())}),
                    flush=True)
        if SMOKE_TELEMETRY:
            pnorm = torch.linalg.vector_norm(torch.cat(
                [p.detach().flatten() for p in net.parameters()])).item()
            print(f"SMOKE chunk={chunk} ticks={ticks} "
                  f"reward_mean={rew_b.mean().item():.9g} "
                  f"adv_absmean={adv.abs().mean().item():.9g} "
                  f"value_mean={val_b.mean().item():.9g} "
                  f"ent={float(ent_sum) / max(upd_n, 1):.9g} "
                  f"kl={float(kl_sum) / max(upd_n, 1):.9g} "
                  f"pnorm={pnorm:.9g}",
                  flush=True)

        chunk += 1
        wall = time.perf_counter() - t_start
        t0succ = (sum(trail_t0) / len(trail_t0)) if trail_t0 else float("nan")
        stage_means = [np.mean([curr.succ(si, s) for si in range(nseeds)
                                if curr.avail[si, s]] or [0.0])
                       for s in range(N_STAGES)]
        curve.append((ticks, wall, t0succ, float(np.mean(stage_means)),
                      *stage_means))
        if chunk % 5 == 0 or chunk == 1:
            mh = "/".join(str(int(x)) for x in mile_hist)
            # live best-so-far inventory rates (fraction of lanes that ever
            # held each milestone this episode) — abuse runs watch spick climb
            b = rew.best.float()
            live_rates = (
                f"log3={(b[:, IX_LOG] >= 3).float().mean().item():.2f} "
                f"wpick={(b[:, IX_WPICK] > 0).float().mean().item():.2f} "
                f"cob3={(b[:, IX_COBBLE] >= 3).float().mean().item():.2f} "
                f"spick={(b[:, IX_SPICK] > 0).float().mean().item():.2f} "
                f"coal={(b[:, IX_COAL] > 0).float().mean().item():.2f} "
                f"torch={(b[:, IX_TORCH] > 0).float().mean().item():.2f}")
            if IRON_CHAIN and getattr(rew, "best_iron", None) is not None:
                bi = rew.best_iron.float()
                live_rates += (
                    f" furn={(bi[:, 0] > 0).float().mean().item():.2f}"
                    f" ore={(bi[:, 1] > 0).float().mean().item():.2f}"
                    f" ing={(bi[:, 2] > 0).float().mean().item():.2f}"
                    f" ipick={(bi[:, 3] > 0).float().mean().item():.2f}")
            print(f"chunk {chunk:4d}  ticks {ticks/1e6:7.2f}M  wall "
                  f"{wall:7.1f}s  {ticks/max(wall,1e-9)/1e6:.3f}M t/s  "
                  f"t0 {t0succ:.2f} (n {len(trail_t0)})  stage-succ "
                  + " ".join(f"{m:.2f}" for m in stage_means)
                  + f"  eps {n_eps}  reached {mh}  live {live_rates}"
                  + (f"  ent {float(ent_sum)/upd_n:.3f} "
                     f"kl {float(kl_sum)/upd_n:.4f} "
                     f"clip {float(clip_sum)/upd_n:.2f}" if upd_n else "")
                  + f"  memg {torch.cuda.memory_allocated()/1e9:.2f}",
                  flush=True)
        if chunk % 25 == 0:
            print(f"  cells (per seed {TRAIN_SEEDS}, stages "
                  f"{MILE_NAMES}): {curr.matrix()}", flush=True)
        if ticks >= next_ckpt and not BENCH_MEASURE_CHUNKS:
            save_all()
            next_ckpt = (ticks // CKPT_TICKS + 1) * CKPT_TICKS
        if chunk % 20 == 0 and not BENCH_MEASURE_CHUNKS:
            save_png()

        if BENCH_MEASURE_CHUNKS and \
                chunk >= BENCH_WARMUP_CHUNKS + BENCH_MEASURE_CHUNKS:
            stop_reason = f"benchmark ({BENCH_MEASURE_CHUNKS} measured chunks)"
        elif MAX_CHUNKS and chunk >= MAX_CHUNKS:
            stop_reason = f"chunk budget ({chunk})"
        elif ticks >= MAX_TICKS:
            stop_reason = f"tick budget ({ticks/1e6:.1f}M)"
        elif wall >= MAX_WALL:
            stop_reason = f"wall budget ({wall:.0f}s)"
        if stop_reason:
            break

    if not BENCH_MEASURE_CHUNKS and not SMOKE_TELEMETRY:
        save_all()
        save_png()
    print(f"stop: {stop_reason}", flush=True)
    print(f"episodes {n_eps}; reached-milestone histogram "
          f"{list(mile_hist)}", flush=True)
    print(f"trailing t0 full-chain: {t0succ:.3f}  best {best_t0['v']:.3f} "
          f"at {best_t0['ticks']/1e6:.1f}M", flush=True)
    env.close()


if __name__ == "__main__":
    main()
