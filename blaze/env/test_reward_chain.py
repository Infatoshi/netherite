"""Bitwise parity test: ChainReward module vs the pre-refactor inline reward.

The reference here is a VERBATIM copy of the reward block that lived inline in
ppo_chain_cu.py (kept as golden reference - do not "improve" it; it archives
the exact op sequence). If this test passes bitwise on every term across
randomized rollouts with mid-stream resets, the extraction is proven
behavior-preserving.

Run: cd magma && uv run --no-project --with numpy,torch \
    python blaze/env/test_reward_chain.py
"""
import os
import sys

import torch

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

from reward_chain import ChainReward, ChainRewardSpec           # noqa: E402
from reward_chain import IX_LOG, IX_PLANK, IX_STICK, IX_COBBLE  # noqa: E402
from reward_chain import IX_TABLE, IX_WPICK, IX_COAL, IX_TORCH  # noqa: E402

CX, CY = 32, 18


def ref_init(n, dev):
    return dict(
        best=torch.zeros((n, 9), dtype=torch.int32, device=dev),
        flag_cont=torch.zeros(n, dtype=torch.bool, device=dev),
        prev_logd=torch.full((n,), -1.0, device=dev),
        prev_coald=torch.full((n,), -1.0, device=dev),
        prev_y=torch.zeros(n, device=dev),
        prev_digp=torch.zeros(n, device=dev),
        min_y=torch.full((n,), 1e9, device=dev),
    )


def ref_reset(st, idx):
    st["best"][idx] = 0
    st["flag_cont"][idx] = False
    st["prev_logd"][idx] = -1.0
    st["prev_coald"][idx] = -1.0
    st["prev_y"][idx] = -1e9
    st["prev_digp"][idx] = 1e9
    st["min_y"][idx] = 1e9


def ref_step(st, status, cam, acts, pose, scal, done, lane_seed_t, log_t,
             COAL_CHEW=0.0, HUNT_DESC=0.0):
    """VERBATIM archive of the inline reward block from ppo_chain_cu.py."""
    best = st["best"]
    flag_cont = st["flag_cont"]
    prev_logd = st["prev_logd"]
    prev_coald = st["prev_coald"]
    prev_y = st["prev_y"]
    prev_digp = st["prev_digp"]
    min_y = st["min_y"]
    dev = status.device
    N_ENVS = status.shape[0]

    newmax = torch.maximum(best, status[:, :9])
    d = (newmax - best).float()
    r = torch.full((N_ENVS,), -0.01, device=dev)
    r += d[:, IX_LOG].clamp(max=5.0) * 1.0
    r += (best[:, IX_PLANK].eq(0) & newmax[:, IX_PLANK].gt(0)) \
        .float() * 2.0
    r += (best[:, IX_STICK].eq(0) & newmax[:, IX_STICK].gt(0)) \
        .float() * 2.0
    r += (best[:, IX_TABLE].eq(0) & newmax[:, IX_TABLE].gt(0)) \
        .float() * 3.0
    cont_now = status[:, 11] == 1
    r += (cont_now & ~flag_cont).float() * 4.0
    flag_cont |= cont_now
    r += (best[:, IX_WPICK].eq(0) & newmax[:, IX_WPICK].gt(0)) \
        .float() * 6.0
    r += d[:, IX_COBBLE].clamp(max=4.0) * 1.0
    r += (best[:, IX_COAL].eq(0) & newmax[:, IX_COAL].gt(0)) \
        .float() * 6.0
    r += (best[:, IX_TORCH].eq(0) & newmax[:, IX_TORCH].gt(0)) \
        .float() * 12.0
    st["best"] = best = newmax

    center = cam[:, CY, CX]
    atk = acts[:, 4] == 1

    chopping = (best[:, IX_LOG] < 3) & (best[:, IX_PLANK] == 0) & \
        (best[:, IX_WPICK] == 0)
    if chopping.any():
        idx = chopping.nonzero(as_tuple=True)[0]
        p3 = pose[idx, :3].clone()
        p3[:, 1] += 1.62
        ld = (log_t[lane_seed_t[idx]] -
              p3.unsqueeze(1)).square().sum(-1).min(dim=1) \
            .values.sqrt()
        havep = prev_logd[idx] >= 0
        shp = torch.zeros_like(ld)
        shp[havep] = 0.5 * (prev_logd[idx][havep] - ld[havep])
        r[idx] += shp.clamp(-1.0, 1.0)
        prev_logd[idx] = ld
        r[idx] += (atk[idx] & (center[idx] == 17)).float() * 0.03
    prev_logd[~chopping] = -1.0

    digging = (best[:, IX_WPICK] > 0) & (best[:, IX_COBBLE] < 3)
    if digging.any():
        dy = (prev_y - pose[:, 1]).clamp(0.0, 2.0)
        r += digging.float() * 0.25 * dy
        held_pick = status[:, 10] == 270
        on_stone = (center == 1) | (center == 4) | (center == 3) | \
            (center == 2)
        r += (digging & atk & held_pick & on_stone).float() * 0.02
        r += (digging & held_pick).float() * 0.005
    st["prev_y"] = prev_y = pose[:, 1].clone()

    haspick = best[:, IX_WPICK] > 0
    digp = status[:, 12].float()
    dprog = (digp - prev_digp).clamp(min=0.0)
    stone_px = (center == 1) | (center == 16)
    r += (haspick & (status[:, 10] == 270) & stone_px).float() \
        * 0.0015 * dprog
    st["prev_digp"] = prev_digp = digp

    hunting = (best[:, IX_WPICK] > 0) & (best[:, IX_COBBLE] >= 3) & \
        (best[:, IX_COAL] == 0)
    no_coal_scan = (scal[:, 0] == 0) & (scal[:, 1] == 0) & \
        (scal[:, 2] == 0) & (scal[:, 3] == 1)
    if hunting.any():
        have_nc = ~no_coal_scan
        cd = scal[:, 3] * 24.0
        ok = hunting & have_nc
        havep = ok & (prev_coald >= 0)
        shp = torch.zeros(N_ENVS, device=dev)
        shp[havep] = 0.5 * (prev_coald[havep] - cd[havep])
        r += shp.clamp(-1.0, 1.0)
        prev_coald[ok] = cd[ok]
        prev_coald[~ok] = -1.0
        r += (hunting & atk & (center == 16) &
              (cd <= 3.5)).float() * 0.03
        r += (hunting & ((status[:, 10] == 270) |
                         (status[:, 10] == 274))).float() * 0.005
        if COAL_CHEW > 0.0:
            r += (hunting & (center == 16) &
                  ((status[:, 10] == 270) |
                   (status[:, 10] == 274))).float() * COAL_CHEW * dprog
    else:
        prev_coald.fill_(-1.0)

    if HUNT_DESC > 0.0:
        rec_gain = (min_y - pose[:, 1]).clamp(0.0, 2.0)
        rec_gain = torch.where(min_y > 1e8,
                               torch.zeros_like(rec_gain), rec_gain)
        r += (hunting & no_coal_scan).float() * HUNT_DESC * rec_gain
    st["min_y"] = min_y = torch.minimum(min_y, pose[:, 1])

    r += (done == 2).float() * -5.0
    return r


def synth_batch(n, steps, dev, gen, nseeds=3, lmax=11):
    """Random blaze-shaped inputs exercising every gate transition."""
    lanes = torch.arange(n, device=dev)
    lane_seed_t = torch.randint(0, nseeds, (n,), device=dev, generator=gen)
    log_t = torch.randn(nseeds, lmax, 3, device=dev, generator=gen) * 12
    out = []
    for _ in range(steps):
        inv = torch.zeros((n, 17), dtype=torch.int32, device=dev)
        inv[:, :9] = torch.randint(0, 6, (n, 9), device=dev,
                                   generator=gen).int()
        # iron cols 13..16 random: the DEFAULT spec must ignore them bitwise
        inv[:, 13:17] = torch.randint(0, 4, (n, 4), device=dev,
                                      generator=gen).int()
        inv[:, 10] = torch.randint(0, 400, (n,), device=dev,
                                   generator=gen).int()
        inv[:, 10][lanes[: n // 3]] = 270          # wood pick third of lanes
        inv[:, 10][n // 3: n // 2] = 274           # stone pick some lanes
        inv[:, 11] = torch.randint(0, 2, (n,), device=dev,
                                   generator=gen).int()
        inv[:, 12] = torch.randint(0, 1001, (n,), device=dev,
                                   generator=gen).int()
        cam = torch.randint(0, 64, (n, 36, 64), dtype=torch.int16,
                            device=dev, generator=gen)
        cam[:, CY, CX] = torch.randint(0, 20, (n,), dtype=torch.int16,
                                       device=dev, generator=gen)
        acts = torch.randint(0, 2, (n, 9), dtype=torch.int64, device=dev,
                             generator=gen)
        pose = torch.randn(n, 5, device=dev, generator=gen) * 30
        scal = torch.rand(n, 6, device=dev, generator=gen)
        scal[: n // 4, 0] = 0.0                    # some no-scan lanes
        scal[: n // 4, 1] = 0.0
        scal[: n // 4, 2] = 0.0
        scal[: n // 4, 3] = 1.0
        done = torch.randint(0, 3, (n,), dtype=torch.uint8, device=dev,
                             generator=gen)
        out.append((inv, cam, acts, pose, scal, done))
    return lane_seed_t, log_t, out


def run_once(n=64, steps=200, chew=0.0, desc=0.0, seed=0):
    dev = torch.device("cuda" if torch.cuda.is_available() else "cpu")
    gen = torch.Generator(device=dev)
    gen.manual_seed(seed)
    lane_seed_t, log_t, batch = synth_batch(n, steps, dev, gen)
    spec = ChainRewardSpec()
    spec.coal_chew, spec.hunt_desc = chew, desc
    mod = ChainReward(n, dev, spec)
    ref = ref_init(n, dev)
    diffs = 0
    for t, (inv, cam, acts, pose, scal, done) in enumerate(batch):
        rm = mod.step(inv, cam, acts, pose, scal, done, lane_seed_t, log_t)
        rr = ref_step(ref, inv, cam, acts, pose, scal, done, lane_seed_t,
                      log_t, COAL_CHEW=chew, HUNT_DESC=desc)
        assert torch.equal(rm, rr), f"reward bits differ at step {t}: " \
            f"{(rm != rr).nonzero()[:5].tolist()}"
        # random mid-stream episode-end resets (10% of lanes)
        ended = torch.rand(n, device=dev, generator=gen) < 0.1
        if ended.any():
            idx = ended.nonzero(as_tuple=True)[0]
            mod.reset(idx)
            ref_reset(ref, idx)
        # state must track bitwise too (spot check every 25 steps)
        if t % 25 == 0:
            for k, v in ref.items():
                vm = getattr(mod, k)
                assert torch.equal(vm, v), \
                    f"state {k} differs at step {t}"
    return diffs


def main():
    for chew, desc, seed in ((0.0, 0.0, 0), (0.006, 0.0, 1), (0.0, 0.25, 2),
                             (0.006, 0.25, 3), (0.006, 0.25, 42)):
        run_once(chew=chew, desc=desc, seed=seed)
    # spec resolve/dump roundtrip
    import tempfile
    for k in ("COAL_CHEW", "HUNT_DESC", "REWARD_JSON"):
        os.environ.pop(k, None)
    spec = ChainRewardSpec.resolve()
    assert spec.coal_chew == 0.0 and spec.hunt_desc == 0.0
    with tempfile.NamedTemporaryFile("w", suffix=".json", delete=False) as f:
        f.write('{"w_coal_first": 9.5, "hunt_desc": 0.7}')
        path = f.name
    os.environ["REWARD_JSON"] = path
    os.environ["COAL_CHEW"] = "0.006"
    spec = ChainRewardSpec.resolve()
    assert spec.w_coal_first == 9.5, spec.w_coal_first
    assert spec.hunt_desc == 0.7 and spec.coal_chew == 0.006
    os.unlink(path)
    for k in ("COAL_CHEW", "REWARD_JSON"):
        os.environ.pop(k, None)
    test_iron_milestones()
    print("test_reward_chain: bitwise parity OK (v1 + v2 param grid, "
          "spec resolve OK, iron milestones OK)")


def test_iron_milestones():
    """Deterministic check of the iron terms (cols 13..16 + container==2)."""
    from reward_chain import IX_FURN, IX_IRONORE, IX_INGOT, IX_IPICK
    dev = torch.device("cpu")
    spec = ChainRewardSpec()
    spec.time_cost = 0.0
    spec.w_furnace_first = 5.0
    spec.w_furnace_open = 4.0
    spec.w_ironore_per = 1.0
    spec.ironore_clamp = 3.0
    spec.w_ingot_first = 8.0
    spec.w_ipick_first = 20.0
    mod = ChainReward(1, dev, spec)
    cam = torch.zeros((1, 36, 64), dtype=torch.int16)
    acts = torch.zeros((1, 9), dtype=torch.int64)
    pose = torch.zeros((1, 5))
    scal = torch.zeros((1, 6))
    done = torch.zeros(1, dtype=torch.uint8)
    lst = torch.zeros(1, dtype=torch.int64)
    logt = torch.full((1, 1, 3), 1e6)

    def step(furn=0, ore=0, ingot=0, ipick=0, cont=0):
        st = torch.zeros((1, 17), dtype=torch.int32)
        st[0, 11] = cont
        st[0, IX_FURN], st[0, IX_IRONORE] = furn, ore
        st[0, IX_INGOT], st[0, IX_IPICK] = ingot, ipick
        return mod.step(st, cam, acts, pose, scal, done, lst, logt).item()

    assert step() == 0.0
    assert step(furn=1) == 5.0                     # furnace first, once
    assert step(furn=1) == 0.0
    assert step(furn=1, cont=2) == 4.0             # furnace open, once
    assert step(furn=1, ore=5, cont=2) == 3.0      # ore per-count, clamped
    assert step(furn=1, ore=5, ingot=1) == 8.0     # ingot first
    assert step(furn=1, ipick=1) == 20.0           # iron pick first
    assert step(furn=1, ore=6, ingot=2, ipick=1) == 1.0  # +1 ore over best
    mod.reset(torch.tensor([0]))
    assert step(furn=1) == 5.0                     # reset re-arms one-shots


if __name__ == "__main__":
    main()
