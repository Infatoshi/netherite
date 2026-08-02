"""Compare two 30-chunk SMOKE logs: is the candidate's learning degraded?

Fast is worthless if it stops learning. Each smoke run prints one SMOKE line
per chunk (reward_mean, adv_absmean, value_mean, entropy, approx-KL, param
norm) from the same RNG_SEED and the same snapshots, so the two runs see the
same curriculum and the same starting weights.

The comparison is deliberately weak on point values and strong on trend: the
env is stochastic and the candidate's sampler consumes a different RNG stream
(RNG_PROTOCOL.md), so chunk-for-chunk equality is not available and demanding
it would be a lie. What must hold:

  * every value finite in both runs
  * the candidate's mean reward over the last third is not below the
    baseline's by more than TOL_SIGMA times the baseline's own chunk-to-chunk
    standard deviation
  * entropy has not collapsed (candidate >= 0.5 * baseline final entropy)
  * approx-KL stays the same order (candidate <= 4x baseline mean KL)
  * the parameter norm is moving in both (a frozen net is a silent failure)

Usage: python compare_smoke.py BASE.log CAND.log
"""
import math
import re
import sys

TOL_SIGMA = 2.0
LINE = re.compile(r"^SMOKE chunk=(\d+) ticks=(\d+) (.*)$")


def load(path):
    rows = []
    with open(path) as f:
        for ln in f:
            m = LINE.match(ln.strip())
            if not m:
                continue
            d = {"chunk": int(m.group(1)), "ticks": int(m.group(2))}
            for kv in m.group(3).split():
                k, _, v = kv.partition("=")
                d[k] = float(v)
            rows.append(d)
    if not rows:
        sys.exit(f"no SMOKE lines in {path}")
    return rows


def col(rows, k):
    return [r[k] for r in rows]


def mean(x):
    return sum(x) / len(x)


def std(x):
    m = mean(x)
    return (sum((v - m) ** 2 for v in x) / max(len(x) - 1, 1)) ** 0.5


def main():
    base, cand = load(sys.argv[1]), load(sys.argv[2])
    n = min(len(base), len(cand))
    base, cand = base[:n], cand[:n]
    tail = slice(2 * n // 3, n)
    fails = []

    for tag, rows in (("base", base), ("cand", cand)):
        for k in ("reward_mean", "value_mean", "ent", "kl", "pnorm"):
            if not all(math.isfinite(v) for v in col(rows, k)):
                fails.append(f"{tag}.{k} non-finite")

    br, cr = col(base, "reward_mean")[tail], col(cand, "reward_mean")[tail]
    sigma = std(col(base, "reward_mean"))
    drop = mean(br) - mean(cr)
    ok_r = drop <= TOL_SIGMA * sigma
    print(f"reward_mean last-third: base={mean(br):.6g} cand={mean(cr):.6g} "
          f"drop={drop:+.6g} allowed<={TOL_SIGMA * sigma:.6g} "
          f"(base chunk sigma {sigma:.6g})  {'OK' if ok_r else 'FAIL'}")
    if not ok_r:
        fails.append("reward trend degraded")

    be, ce = col(base, "ent")[-1], col(cand, "ent")[-1]
    ok_e = ce >= 0.5 * be
    print(f"final entropy: base={be:.6g} cand={ce:.6g} "
          f"(need >= {0.5 * be:.6g})  {'OK' if ok_e else 'FAIL'}")
    if not ok_e:
        fails.append("entropy collapse")

    bk, ck = mean(col(base, "kl")), mean(col(cand, "kl"))
    ok_k = ck <= 4.0 * bk
    print(f"mean approx-KL: base={bk:.6g} cand={ck:.6g} "
          f"(need <= {4.0 * bk:.6g})  {'OK' if ok_k else 'FAIL'}")
    if not ok_k:
        fails.append("KL blowup")

    for tag, rows in (("base", base), ("cand", cand)):
        d = abs(col(rows, "pnorm")[-1] - col(rows, "pnorm")[0])
        print(f"{tag} param-norm travel over {n} chunks: {d:.6g}")
        if d <= 0:
            fails.append(f"{tag} net frozen")

    if fails:
        print(f"\nSMOKE FAIL: {fails}")
        return 1
    print(f"\nSMOKE PASS over {n} chunks")
    return 0


if __name__ == "__main__":
    sys.exit(main())
