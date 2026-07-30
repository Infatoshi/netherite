"""Spawn-to-torch chain reward: one declarative spec + one stateful calculator.

Single home for every reward term the chain PPO trainer pays. Extracted from
ppo_chain_cu.py's inline block; bit-identical behavior for the default spec
(the extraction is op-for-op: same tensors, same op order, same dtypes).

Design contract:
- Weights/thresholds live in ChainRewardSpec (numbers). Chain STRUCTURE (what
  the milestones are, which gates guard which term) lives in ChainReward.step.
- Fully GPU-tensorized: step() takes the blaze status/cam/pose/scal/done
  readouts and returns the per-lane reward vector; no host syncs.
- Reward may consult snapshot oracles (static log positions) but POLICY
  inputs never do (see ppo_chain_cu.py build_frame/build_scal).
- Reproducibility: the trainer prints the resolved spec at start and writes a
  JSON sidecar next to every checkpoint. Load overrides with REWARD_JSON.
  COAL_CHEW/HUNT_DESC env vars still override the two touchstone fields
  (backward compatible with the v2 recipe launches).

Term reference (gates evaluated per decision, best-so-far counters):
  time cost                : -time_cost every decision
  milestone / one-shot     : per new log (clamped), first plank/stick/table,
                             first table-open(container), first wooden pick,
                             per new cobble (clamped), first stone pick,
                             first coal, first torch
  terminal                 : death_penalty when done==2 (non-success)
  chop shaping  (pre-logs3): dist coef * (prev_logd - logd), +-clamp;
                             + chop crosshair bonus (attack & crosshair log)
  dig shaping   (pick, ~cobble3): descend coef * clamp(dy,0,2);
                             crosshair-on-dig + hold-pick trickles
  dig-progress  (pick held): dig-prog permille delta, gated pick+stone pixel
  coal hunting  (cobble3, ~coal): dist coef * (prev_cd - cd) when coal in
                             scan, +-clamp; crosshair-coal within maxd;
                             hold-pick trickle; optional coal_chew (progress-
                             tied crosshair-on-coal) and hunt_desc (new depth
                             record while coal scan empty)
"""
import json
import os
from dataclasses import dataclass, asdict

import torch

CX, CY = 32, 18   # crosshair pixel (cam is [N,36,64])

# status column layout (blaze_fill_status): 9 inv counts + hotbar_sel +
# held item + container-open + dig progress permille (live, 0 idle)
IX_LOG, IX_PLANK, IX_STICK, IX_COBBLE, IX_TABLE, IX_WPICK, IX_SPICK, \
    IX_COAL, IX_TORCH = range(9)
# inv item ids for the 9 status columns (must match CU_INV_IDS in blaze_core.h)
# log=17 plank=5 stick=280 cobble=4 table=58 wpick=270 spick=274 coal=263 torch=50
# iron-chain columns (CU_STATUS_K 17): furnace=61 ironore=15 ingot=265 ipick=257
IX_FURN, IX_IRONORE, IX_INGOT, IX_IPICK = 13, 14, 15, 16


@dataclass
class ChainRewardSpec:
    """All chain reward weights. Defaults reproduce the v1/v2 trainer."""
    # global
    time_cost: float = 0.01           # subtracted per decision
    death_penalty: float = 5.0        # subtracted on done==2
    # milestones (one-shot on best-so-far; per-count where clamped)
    w_log_per: float = 1.0
    log_clamp: float = 5.0
    w_plank_first: float = 2.0
    w_stick_first: float = 2.0
    w_table_first: float = 3.0
    w_container_open: float = 4.0
    w_wpick_first: float = 6.0
    w_cobble_per: float = 1.0
    cobble_clamp: float = 4.0
    w_spick_first: float = 0.0   # default 0: not in v1/v2 recipe; abuse via JSON
    w_coal_first: float = 6.0
    w_torch_first: float = 12.0
    # chop shaping (while below 3 logs and no planks/pick yet)
    chop_dist_coef: float = 0.5
    chop_dist_clamp: float = 1.0
    chop_crosshair: float = 0.03
    # dig shaping (pick in inventory, below 3 cobble)
    dig_descend_coef: float = 0.25
    dig_stone_atk: float = 0.02
    dig_hold_pick: float = 0.005
    # dig-progress permille shaping (pick held, stone/coal under crosshair)
    digprog_coef: float = 0.0015
    # coal hunting (pick, >=3 cobble, no coal yet)
    coal_dist_coef: float = 0.5
    coal_dist_clamp: float = 1.0
    coal_crosshair: float = 0.03
    coal_crosshair_maxd: float = 3.5
    coal_hold_pick: float = 0.005
    # v2 touchstones (default 0 = exact v1 behavior)
    coal_chew: float = 0.0
    hunt_desc: float = 0.0
    # iron chain milestones (default 0 = exact stone-chain behavior; enable
    # via REWARD_JSON; needs CU_STATUS_K>=17 status cols 13..16)
    w_furnace_first: float = 0.0
    w_furnace_open: float = 0.0   # first container==2
    w_ironore_per: float = 0.0
    ironore_clamp: float = 3.0
    w_ingot_first: float = 0.0
    w_ipick_first: float = 0.0

    @staticmethod
    def resolve():
        """defaults <- REWARD_JSON file (if set) <- COAL_CHEW/HUNT_DESC env."""
        spec = ChainRewardSpec()
        path = os.environ.get("REWARD_JSON")
        if path:
            with open(path) as f:
                for k, v in json.load(f).items():
                    if not hasattr(spec, k):
                        raise KeyError(f"unknown reward field {k!r} in {path}")
                    setattr(spec, k, float(v))
        if "COAL_CHEW" in os.environ:
            spec.coal_chew = float(os.environ["COAL_CHEW"])
        if "HUNT_DESC" in os.environ:
            spec.hunt_desc = float(os.environ["HUNT_DESC"])
        return spec

    def dump(self, path):
        with open(path, "w") as f:
            json.dump(asdict(self), f, indent=2, sort_keys=True)
            f.write("\n")


class ChainReward:
    """Stateful per-lane reward state + the per-decision computation.

    State mirrors the tensors the inline trainer kept (bitwise contract):
    best-so-far inv counters, container latch, previous-distance registers,
    previous y, previous dig permille, episode depth record.
    """

    def __init__(self, n_envs, device, spec: ChainRewardSpec):
        self.n = n_envs
        self.dev = device
        self.spec = spec
        # initial values identical to the trainer's fresh-init block
        self.best = torch.zeros((n_envs, 9), dtype=torch.int32, device=device)
        self.flag_cont = torch.zeros(n_envs, dtype=torch.bool, device=device)
        self.prev_logd = torch.full((n_envs,), -1.0, device=device)
        self.prev_coald = torch.full((n_envs,), -1.0, device=device)
        self.prev_y = torch.zeros(n_envs, device=device)
        self.prev_digp = torch.zeros(n_envs, device=device)
        self.min_y = torch.full((n_envs,), 1e9, device=device)
        # iron chain (only consulted when an iron weight is nonzero)
        self.best_iron = torch.zeros((n_envs, 4), dtype=torch.int32,
                                     device=device)
        self.flag_furn = torch.zeros(n_envs, dtype=torch.bool, device=device)
        self._iron_on = any((spec.w_furnace_first, spec.w_furnace_open,
                             spec.w_ironore_per, spec.w_ingot_first,
                             spec.w_ipick_first))

    def reset(self, idx):
        """Episode-end reset for lanes idx (LongTensor on self.dev)."""
        self.best[idx] = 0
        self.flag_cont[idx] = False
        self.prev_logd[idx] = -1.0
        self.prev_coald[idx] = -1.0
        self.prev_y[idx] = -1e9     # no spurious descend bonus post-reset
        self.prev_digp[idx] = 1e9   # no spurious dig delta across resets
        self.min_y[idx] = 1e9       # depth record restarts clean
        self.best_iron[idx] = 0
        self.flag_furn[idx] = False

    def step(self, status, cam, acts, pose, scal, done, lane_seed_t, log_t):
        """One decision of shaping. Returns r [N] float32.

        status/cam/pose/scal/done: blaze readouts for this decision.
        acts: [N,9] int64 sampled head actions (uses head 4 = attack).
        lane_seed_t: [N] int64 seed index per lane (log oracle rows).
        log_t: [S, Lmax, 3] float32 padded static-log oracle (reward only).
        """
        s = self.spec
        dev = self.dev
        n = status.shape[0]

        # ---- milestones on best-so-far counters ----
        newmax = torch.maximum(self.best, status[:, :9])
        d = (newmax - self.best).float()
        r = torch.full((n,), -s.time_cost, device=dev)
        r += d[:, IX_LOG].clamp(max=s.log_clamp) * s.w_log_per
        r += (self.best[:, IX_PLANK].eq(0) & newmax[:, IX_PLANK].gt(0)) \
            .float() * s.w_plank_first
        r += (self.best[:, IX_STICK].eq(0) & newmax[:, IX_STICK].gt(0)) \
            .float() * s.w_stick_first
        r += (self.best[:, IX_TABLE].eq(0) & newmax[:, IX_TABLE].gt(0)) \
            .float() * s.w_table_first
        cont_now = status[:, 11] == 1
        r += (cont_now & ~self.flag_cont).float() * s.w_container_open
        self.flag_cont |= cont_now
        r += (self.best[:, IX_WPICK].eq(0) & newmax[:, IX_WPICK].gt(0)) \
            .float() * s.w_wpick_first
        r += d[:, IX_COBBLE].clamp(max=s.cobble_clamp) * s.w_cobble_per
        if s.w_spick_first != 0.0:
            r += (self.best[:, IX_SPICK].eq(0) & newmax[:, IX_SPICK].gt(0)) \
                .float() * s.w_spick_first
        r += (self.best[:, IX_COAL].eq(0) & newmax[:, IX_COAL].gt(0)) \
            .float() * s.w_coal_first
        r += (self.best[:, IX_TORCH].eq(0) & newmax[:, IX_TORCH].gt(0)) \
            .float() * s.w_torch_first
        self.best = newmax

        # ---- iron milestones (default off: every weight 0) ----
        if self._iron_on:
            sti = status[:, IX_FURN:IX_IPICK + 1]
            newi = torch.maximum(self.best_iron, sti)
            di = (newi - self.best_iron).float()
            r += (self.best_iron[:, 0].eq(0) & newi[:, 0].gt(0)).float() \
                * s.w_furnace_first
            furn_now = status[:, 11] == 2
            r += (furn_now & ~self.flag_furn).float() * s.w_furnace_open
            self.flag_furn |= furn_now
            r += di[:, 1].clamp(max=s.ironore_clamp) * s.w_ironore_per
            r += (self.best_iron[:, 2].eq(0) & newi[:, 2].gt(0)).float() \
                * s.w_ingot_first
            r += (self.best_iron[:, 3].eq(0) & newi[:, 3].gt(0)).float() \
                * s.w_ipick_first
            self.best_iron = newi

        center = cam[:, CY, CX]
        atk = acts[:, 4] == 1

        # ---- chop stage: distance to nearest static log + crosshair ----
        chopping = (self.best[:, IX_LOG] < 3) & (self.best[:, IX_PLANK] == 0) \
            & (self.best[:, IX_WPICK] == 0)
        if chopping.any():
            idx = chopping.nonzero(as_tuple=True)[0]
            p3 = pose[idx, :3].clone()
            p3[:, 1] += 1.62
            ld = (log_t[lane_seed_t[idx]] -
                  p3.unsqueeze(1)).square().sum(-1).min(dim=1) \
                .values.sqrt()
            havep = self.prev_logd[idx] >= 0
            shp = torch.zeros_like(ld)
            shp[havep] = s.chop_dist_coef * \
                (self.prev_logd[idx][havep] - ld[havep])
            r[idx] += shp.clamp(-s.chop_dist_clamp, s.chop_dist_clamp)
            self.prev_logd[idx] = ld
            r[idx] += (atk[idx] & (center[idx] == 17)).float() \
                * s.chop_crosshair
        self.prev_logd[~chopping] = -1.0

        # ---- dig stage: descend + crosshair-on-stone with the pick ----
        digging = (self.best[:, IX_WPICK] > 0) & (self.best[:, IX_COBBLE] < 3)
        if digging.any():
            dy = (self.prev_y - pose[:, 1]).clamp(0.0, 2.0)
            r += digging.float() * s.dig_descend_coef * dy
            held_pick = status[:, 10] == 270
            on_stone = (center == 1) | (center == 4) | (center == 3) | \
                (center == 2)
            r += (digging & atk & held_pick & on_stone).float() \
                * s.dig_stone_atk
            r += (digging & held_pick).float() * s.dig_hold_pick
        self.prev_y = pose[:, 1].clone()

        # ---- dig-progress shaping (live permille, gated pick + stone) ----
        haspick = self.best[:, IX_WPICK] > 0
        digp = status[:, 12].float()
        dprog = (digp - self.prev_digp).clamp(min=0.0)
        stone_px = (center == 1) | (center == 16)
        r += (haspick & (status[:, 10] == 270) & stone_px).float() \
            * s.digprog_coef * dprog
        self.prev_digp = digp

        # ---- coal hunting: scan distance + gated crosshair ----
        hunting = (self.best[:, IX_WPICK] > 0) & (self.best[:, IX_COBBLE] >= 3) \
            & (self.best[:, IX_COAL] == 0)
        no_coal_scan = (scal[:, 0] == 0) & (scal[:, 1] == 0) & \
            (scal[:, 2] == 0) & (scal[:, 3] == 1)
        if hunting.any():
            have_nc = ~no_coal_scan
            cd = scal[:, 3] * 24.0
            ok = hunting & have_nc
            havep = ok & (self.prev_coald >= 0)
            shp = torch.zeros(n, device=dev)
            shp[havep] = s.coal_dist_coef * \
                (self.prev_coald[havep] - cd[havep])
            r += shp.clamp(-s.coal_dist_clamp, s.coal_dist_clamp)
            self.prev_coald[ok] = cd[ok]
            self.prev_coald[~ok] = -1.0
            r += (hunting & atk & (center == 16) &
                  (cd <= s.coal_crosshair_maxd)).float() * s.coal_crosshair
            r += (hunting & ((status[:, 10] == 270) |
                             (status[:, 10] == 274))).float() \
                * s.coal_hold_pick
            if s.coal_chew > 0.0:
                r += (hunting & (center == 16) &
                      ((status[:, 10] == 270) |
                       (status[:, 10] == 274))).float() \
                    * s.coal_chew * dprog
        else:
            self.prev_coald.fill_(-1.0)

        # ---- deeper-descent shaping while coal scan is empty ----
        if s.hunt_desc > 0.0:
            rec_gain = (self.min_y - pose[:, 1]).clamp(0.0, 2.0)
            rec_gain = torch.where(self.min_y > 1e8,
                                   torch.zeros_like(rec_gain), rec_gain)
            r += (hunting & no_coal_scan).float() * s.hunt_desc * rec_gain
        self.min_y = torch.minimum(self.min_y, pose[:, 1])

        # ---- terminal ----
        r += (done == 2).float() * -s.death_penalty
        return r
