"""Render the measured PPO learning curve used in the paper."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


HERE = Path(__file__).resolve().parent
DATA = HERE / "data" / "chain_curve_pin1_1h.npy"
OUT = HERE / "figures" / "rl_curve.pdf"
PREVIEW = HERE / "figures" / "rl_curve.png"


def main() -> None:
    curve = np.load(DATA)
    ticks_m = curve[:, 0] / 1e6
    t0_success = curve[:, 2]
    mean_stage_success = curve[:, 3]

    plt.rcParams.update(
        {
            "font.size": 9,
            "axes.labelsize": 9,
            "legend.fontsize": 8,
            "xtick.labelsize": 8,
            "ytick.labelsize": 8,
            "pdf.fonttype": 42,
            "ps.fonttype": 42,
        }
    )
    fig, ax = plt.subplots(figsize=(6.7, 3.15))
    ax.plot(ticks_m, t0_success, color="#1f77b4", linewidth=1.35,
            label="Cold-spawn full-chain success")
    ax.plot(ticks_m, mean_stage_success, color="#d95f02", linewidth=1.35,
            label="Mean curriculum-stage success")

    finite = np.isfinite(t0_success)
    best_idx = np.flatnonzero(finite)[np.argmax(t0_success[finite])]
    ax.scatter(ticks_m[best_idx], t0_success[best_idx], s=22,
               color="#1f77b4", edgecolor="white", linewidth=0.6, zorder=3)
    ax.annotate(
        f"best checkpoint: {t0_success[best_idx]:.2f}",
        xy=(ticks_m[best_idx], t0_success[best_idx]),
        xytext=(-92, 20),
        textcoords="offset points",
        arrowprops={"arrowstyle": "-", "color": "#555555", "lw": 0.7},
    )

    ax.set(xlabel="Environment ticks (millions)", ylabel="Success rate",
           xlim=(0, ticks_m[-1]), ylim=(-0.02, 1.02))
    ax.grid(axis="y", color="#dddddd", linewidth=0.6)
    ax.spines[["top", "right"]].set_visible(False)
    ax.legend(loc="upper left", frameon=False)
    fig.tight_layout(pad=0.4)
    OUT.parent.mkdir(parents=True, exist_ok=True)
    fig.savefig(OUT, bbox_inches="tight")
    fig.savefig(PREVIEW, dpi=200, bbox_inches="tight")
    plt.close(fig)


if __name__ == "__main__":
    main()
