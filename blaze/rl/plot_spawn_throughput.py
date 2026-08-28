#!/usr/bin/env python3
"""Dark-theme spawn-throughput figure from a measured JSON sidecar.

Input default: blaze/rl/figures/spawn_throughput.json
Output default: blaze/rl/figures/spawn_throughput.png

This is a figure generator, not a bench. Re-run the anvil sweep to refresh
the JSON; this script only draws what was measured.
"""
from __future__ import annotations

import argparse
import json
import os
import sys

import matplotlib.pyplot as plt
import numpy as np


HERE = os.path.dirname(os.path.abspath(__file__))
DEFAULT_JSON = os.path.join(HERE, "figures", "spawn_throughput.json")
DEFAULT_PNG = os.path.join(HERE, "figures", "spawn_throughput.png")

BG = "#0e1117"
AX = "#161b22"
FG = "#e6edf3"
MUTED = "#8b949e"
GRID = "#30363d"
SIM = "#58a6ff"
TRAIN = "#f0883e"
PACK = "#7ee787"
NN = "#d2a8ff"
ENV = "#58a6ff"
HOST = "#8b949e"
UPD = "#f85149"
DEC = "#79c0ff"


def _style():
    plt.rcParams.update(
        {
            "figure.facecolor": BG,
            "axes.facecolor": AX,
            "axes.edgecolor": GRID,
            "axes.labelcolor": FG,
            "text.color": FG,
            "xtick.color": FG,
            "ytick.color": FG,
            "grid.color": GRID,
            "legend.facecolor": AX,
            "legend.edgecolor": GRID,
            "savefig.facecolor": BG,
            "font.size": 10,
        }
    )


def _load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def _rows(data, kind):
    return [r for r in data.get("runs", []) if r.get("kind") == kind and r.get("ok")]


def _fail_ns(data, kind):
    return [r for r in data.get("runs", []) if r.get("kind") == kind and not r.get("ok")]


def _c1(r, key, fallback=None):
    k1 = key + "_c1"
    if r.get(k1) is not None:
        return r[k1]
    if fallback is not None:
        return r.get(fallback, 0.0)
    return r.get(key, 0.0)


def _t8_r4(trn):
    return [r for r in trn if r.get("rollout_steps") == 8 and r.get("repeat", 4) == 4]


def draw(data, out_png, baseline=None):
    _style()
    trn = sorted(
        _rows(data, "train"),
        key=lambda r: (r["n_envs"], r.get("repeat", 4), r.get("rollout_steps", 32)),
    )
    fail_trn = _fail_ns(data, "train")
    t8 = _t8_r4(trn)
    t32 = [r for r in trn if r.get("rollout_steps") == 32 and r.get("repeat", 4) == 4]
    old_trn = []
    old_t8 = []
    if baseline:
        old_trn = sorted(
            _rows(baseline, "train"),
            key=lambda r: (r["n_envs"], r.get("repeat", 4), r.get("rollout_steps", 32)),
        )
        old_t8 = _t8_r4(old_trn)

    fig, axes = plt.subplots(1, 3, figsize=(13.4, 4.5), dpi=140)
    fig.suptitle(
        "Spawn t0 CUDA lockstep after Fable nn  (%s, %s)"
        % (data.get("host", "host"), data.get("gpu", "gpu")),
        fontsize=12,
        color=FG,
        y=1.03,
    )

    ax = axes[0]
    if t8:
        ker = [r for r in t8 if (r.get("ms_ktick_step") or 99) < 20]
        if ker:
            ax.plot(
                [r["n_envs"] for r in ker],
                [r["kernel_ticks_per_s"] / 1e6 for r in ker],
                "o-",
                color=SIM,
                label="k_tick+k_obs+k_final  T=8",
            )
        ax.plot(
            [r["n_envs"] for r in t8],
            [_c1(r, "ticks_per_s") / 1e6 for r in t8],
            "s-",
            color=TRAIN,
            label="trainer e2e  T=8 Fable",
        )
    if old_t8:
        ax.plot(
            [r["n_envs"] for r in old_t8],
            [_c1(r, "ticks_per_s") / 1e6 for r in old_t8],
            "s--",
            color="#ffa657",
            label="trainer e2e  T=8 pre-Fable",
        )
    elif t32:
        ax.plot(
            [r["n_envs"] for r in t32],
            [r["ticks_per_s"] / 1e6 for r in t32],
            "s--",
            color="#ffa657",
            label="trainer e2e  T=32 long",
        )
    ax.axhline(4.06, color=MUTED, ls=":", lw=1)
    ax.text(128, 4.06, "  M3 sim-only N=8192  4.06M", color=MUTED, va="bottom", fontsize=7)
    oom_ns = sorted({r["n_envs"] for r in fail_trn if r.get("oom") or "cudaMalloc" in (r.get("error") or "") or "cudnn" in (r.get("error") or "").lower()})
    for n in oom_ns:
        if n >= 2048:
            ax.axvline(n, color=UPD, ls=":", lw=1, alpha=0.6)
    ax.set_xscale("log", base=2)
    ax.set_yscale("log")
    ax.set_xlabel("worlds N")
    ax.set_ylabel("env-ticks / s  (millions)")
    ax.set_title("throughput vs worlds")
    ax.grid(True, which="both", alpha=0.35)
    ax.legend(loc="lower right", fontsize=7)

    ax = axes[1]
    keys = [
        ("ms_pack", PACK, "pack obs"),
        ("ms_nn", NN, "policy fwd+sample"),
        ("ms_env", ENV, "env H2D+kernels+D2H"),
        ("ms_host", HOST, "host copy/reset"),
        ("ms_upd", UPD, "PPO update"),
    ]
    if t8:
        old_by_n = {r["n_envs"]: r for r in old_t8}
        grouped = bool(old_t8)
        n_bars = len(t8)
        x = np.arange(n_bars, dtype=float)
        w = 0.34 if grouped else 0.72
        totals = np.array(
            [sum(_c1(r, k) for k, _, _ in keys) or 1.0 for r in t8]
        )
        bottom = np.zeros(n_bars)
        for k, color, label in keys:
            frac = np.array([_c1(r, k) for r in t8]) / totals * 100.0
            ax.bar(
                x - (w / 2 if grouped else 0.0),
                frac,
                bottom=bottom,
                color=color,
                label=label,
                width=w,
            )
            bottom += frac
        if grouped:
            old_idx = []
            old_rows = []
            for i, r in enumerate(t8):
                prev = old_by_n.get(r["n_envs"])
                if prev is None:
                    continue
                old_idx.append(i)
                old_rows.append(prev)
            if old_rows:
                xo = np.array(old_idx, dtype=float)
                old_tot = np.array(
                    [sum(_c1(r, k) for k, _, _ in keys) or 1.0 for r in old_rows]
                )
                bottom = np.zeros(len(old_rows))
                first = True
                for k, color, _label in keys:
                    frac = np.array([_c1(r, k) for r in old_rows]) / old_tot * 100.0
                    ax.bar(
                        xo + w / 2,
                        frac,
                        bottom=bottom,
                        color=color,
                        width=w,
                        hatch="///",
                        edgecolor=GRID,
                        linewidth=0.4,
                        label="pre-Fable" if first else None,
                    )
                    bottom += frac
                    first = False
        ax.set_xticks(x)
        ax.set_xticklabels(["%d" % r["n_envs"] for r in t8])
        ax.set_ylim(0, 100)
        ax.set_xlabel(
            "worlds N  (T=8, repeat=4, first steady; left=Fable, hatched=pre)"
            if grouped
            else "worlds N  (T=8, repeat=4, first steady chunk)"
        )
        ax.set_ylabel("chunk wall %")
        ax.set_title("where a trainer chunk goes")
        ax.legend(loc="upper right", fontsize=7)
        ax.grid(True, axis="y", alpha=0.35)
        for i, r in enumerate(t8):
            env = _c1(r, "ms_env")
            rest = totals[i] - env
            if rest > env:
                ax.text(i, 4, "host+policy", ha="center", fontsize=6, color=FG)
                break
    else:
        ax.text(0.5, 0.5, "no T=8 trainer rows", ha="center", va="center", color=MUTED)
        ax.set_axis_off()

    ax = axes[2]
    t8_all = [r for r in trn if r.get("rollout_steps") == 8]
    n_rep = 256 if any(r["n_envs"] == 256 for r in t8_all) else None
    if n_rep is None:
        ax.text(0.5, 0.5, "no action_repeat sweep", ha="center", va="center", color=MUTED)
        ax.set_axis_off()
    else:
        rows = sorted(
            [r for r in t8_all if r["n_envs"] == n_rep],
            key=lambda r: r.get("repeat", 4),
        )
        reps = [r.get("repeat", 4) for r in rows]
        ax.plot(
            reps,
            [r.get("kernel_ticks_per_s", 0) / 1e3 for r in rows],
            "o-",
            color=SIM,
            label="kernel ticks/s",
        )
        ax.plot(
            reps,
            [_c1(r, "ticks_per_s") / 1e3 for r in rows],
            "s-",
            color=TRAIN,
            label="trainer ticks/s Fable",
        )
        ax.plot(
            reps,
            [_c1(r, "decisions_per_s") / 1e3 for r in rows],
            "s--",
            color="#ffa657",
            label="trainer decisions/s",
        )
        old_rep = [
            r
            for r in old_trn
            if r.get("rollout_steps") == 8 and r["n_envs"] == n_rep
        ]
        if old_rep:
            old_rep = sorted(old_rep, key=lambda r: r.get("repeat", 4))
            ax.plot(
                [r.get("repeat", 4) for r in old_rep],
                [_c1(r, "ticks_per_s") / 1e3 for r in old_rep],
                "s:",
                color="#ffa657",
                label="trainer ticks/s pre-Fable",
            )
        ax.set_xticks(reps)
        ax.set_xlabel("action_repeat  (N=%d, T=8)" % n_rep)
        ax.set_ylabel("thousands / s")
        ax.set_title("ticks vs decisions")
        ax.grid(True, alpha=0.35)
        ax.legend(loc="best", fontsize=7)

    fig.tight_layout()
    os.makedirs(os.path.dirname(out_png) or ".", exist_ok=True)
    fig.savefig(out_png, bbox_inches="tight")
    plt.close(fig)
    print("wrote", out_png)


def main(argv=None):
    ap = argparse.ArgumentParser()
    ap.add_argument("--json", default=DEFAULT_JSON)
    ap.add_argument("-o", "--out", default=DEFAULT_PNG)
    ap.add_argument(
        "--baseline",
        default=os.path.join(HERE, "figures", "spawn_throughput_prefable.json"),
        help="pre-Fable JSON to overlay (dashed). Empty string skips.",
    )
    args = ap.parse_args(argv)
    if not os.path.isfile(args.json):
        print("missing", args.json, file=sys.stderr)
        return 2
    baseline = None
    if args.baseline and os.path.isfile(args.baseline):
        baseline = _load(args.baseline)
    draw(_load(args.json), args.out, baseline=baseline)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
