#!/usr/bin/env python3
"""rng_cursor.py - java.util.Random cursor accounting for Java<->magma parity.

Why this exists
---------------
Vanilla 1.11.2 consumes ``java.util.Random`` draws in a fixed order every server
tick.  magma has to consume the same number of draws in the same order, or it is
walking a different random stream from that tick onward.  The failure mode is
nasty: nothing looks wrong for hundreds of ticks, and then a mob turns the wrong
way or a grass block spreads to the wrong cell, and the divergence surfaces as a
physics or pixel mismatch with no obvious cause.  ``pxdiff`` can tell you *which
pixel* is wrong; it cannot tell you that the real event was one extra
``rand.nextInt()`` four hundred ticks earlier.

The trick that makes this tractable is that ``java.util.Random`` *is* its state.
The generator is a 48-bit LCG::

    seed <- (seed * 0x5DEECE66D + 0xB) mod 2^48

and every public method (``nextInt``, ``nextDouble``, ``nextFloat``, ...) is
built out of one or more ``next()`` steps of exactly that recurrence.  The
constructor scrambles the user seed, but ``next()`` never does anything else.
So a snapshot of the 48-bit state at a tick boundary is a *cursor*, and two
cursors bracketing a tick determine EXACTLY how many ``next()`` calls that tick
consumed: walk the recurrence forward from the first and count steps until you
hit the second.  That converts "the streams drifted somewhere in this run" into
"tick 4213 consumed 7 world draws, the reference consumed 6".

The walk is cheap in the direction that matters.  It exits as soon as it reaches
the target, so a healthy tick costs exactly as many multiply-adds as the server
actually drew.  Only an unreachable pair (a genuine divergence, or a
``setSeed``) pays the full budget, and the gate stops at the first one anyway.

Streams
-------
The Java bridge (``rng_capture`` / ``rng_dump`` in
``java/Minecraft/src/main/java/netheritemod/Recorder.java``) captures four
server-side streams at ``ServerTickEvent.START``.  The set follows the one
bluecoconut's PR #5 state capsule marks ``exact``
(``world.rng.{java,math,block}_random_seed48`` plus ``world.rng.update_lcg``):

``world``
    ``World.rand``.  The one that matters: random ticks, weather, mob spawning,
    and every ``playSound`` draw.  Sound playback consumes draws off this
    stream; PR #5's data-only sound-EVENT ring (kept in the review verdict,
    not yet ported - see DEVLOG) would plug into this accounting as a fifth
    stream when it lands.
``math``
    ``java.lang.Math``'s process-global generator.  Shared with the whole JVM,
    so a stray ``Math.random()`` anywhere moves it.  Captured to prove
    non-interference, not because magma models it.
``block``
    ``Block.RANDOM``, the static fallback for blocks with no world in hand.
``lcg``
    ``World.updateLCG``.  NOT a ``java.util.Random`` - a plain int32 recurrence
    ``lcg = lcg * 3 + 1013904223`` that picks random-tick coordinates.  It
    advances independently of ``world``, so it gets its own accounting.

Deviation from PR #5
--------------------
bluecoconut's Recorder can also *restore* these cursors (``setJavaRandomSeed48``
and friends), which is how their controlled oracles arm a fixture.  That is
sound for their use - drive a single mechanic from a known cursor - but it is
NOT portable to a whole-tape replay here, and their own capability ledger says
so: ``magma/trace/state_capsule.py`` marks the individual seeds ``exact`` but
the aggregate ``world.rng_cursors`` only ``captured_only``.  This port therefore
implements capture and verification, not injection.  See ``INJECTION_NOTE``
below for the architectural reason.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field

# ---- java.util.Random, verbatim from the JDK contract ----
JR_MULT = 0x5DEECE66D
JR_ADD = 0xB
JR_MASK = (1 << 48) - 1

# ---- World.updateLCG (net/minecraft/world/World.java:95) ----
LCG_MULT = 3
LCG_ADD = 1013904223
LCG_MASK = (1 << 32) - 1

#: Default ceiling on a single tick's draw count before we call it unreachable.
#: A busy server tick draws on the order of hundreds; 1<<16 leaves two orders of
#: magnitude of headroom while keeping a genuine divergence cheap to detect.
DEFAULT_MAX_DRAWS = 1 << 16

INJECTION_NOTE = """\
Injection (forcing magma's RNG to a captured Java cursor) is deliberately NOT
implemented, and the mismatch is architectural rather than a missing feature.

PR #5 restores cursors into a LIVE JVM, where java.util.Random is a stateful
object and writing its AtomicLong is the whole operation.  magma has no such
object to write.  blaze/core/mc_rng.h:1-11 states the doctrine outright:
JavaRandom (the exact 48-bit LCG) is used ONLY for worldgen, and every runtime
sim draw goes through mc_hash_seed(world_seed, tick, x, y, z, purpose) - a
stateless SplitMix64 hash - specifically so CPU==CUDA holds regardless of thread
scheduling.  magma/game/randtick.h:10 repeats it as the randtick convention.
That statelessness is load-bearing: it is exactly what the grass randtick
occupancy skip exploits for its 2.2x trainer speedup (attempts are evaluated in
any order and skipped wholesale when a 16^3 section holds no grass), and it is
why the PR review rejected pulling their stateful JavaGaussianRandom into the
batched env.  A counter-based stream has no cursor position to force.

The gap is already acknowledged in the replay path: magma/game/script.c:488-490
disables randtick entirely for tape replay with the comment "oracle world RNG is
unseedable".  This gate does not close that gap - it measures it, and it names
the tick when something that IS supposed to match stops matching.

blaze/core/world_tick_vanilla.h:98 DOES carry a faithful stateful JavaRandom
plus an int32 updateLCG for the vanilla world-tick model, so injection has a
credible home there someday.  That file is off-limits for this port, and wiring
a Java cursor into it is a design change to the vanilla tick model, not a
test-infra port.

Scale note, measured first-hand on this branch (see docs/DEVLOG.md): a live
1.11.2 integrated server burns ~25,000 World.rand draws per tick, and 97.4% of
them are mob spawning.  Mirroring that ordered stream draw-for-draw in a batched
env is not a small change; it is a different architecture.\
"""


def jr_step(seed: int) -> int:
    """One java.util.Random next() step of the 48-bit LCG."""
    return (seed * JR_MULT + JR_ADD) & JR_MASK


def lcg_step(v: int) -> int:
    """One World.updateLCG step, kept in unsigned-32 form."""
    return (v * LCG_MULT + LCG_ADD) & LCG_MASK


def _walk(a: int, b: int, step, max_steps: int) -> int | None:
    """Steps needed to carry `a` to `b`, or None if unreachable within budget.

    Early-exits on arrival, so a healthy transition costs only as many steps as
    were actually consumed.
    """
    if a == b:
        return 0
    s = a
    for i in range(1, max_steps + 1):
        s = step(s)
        if s == b:
            return i
    return None


def draws_between(a: int, b: int, max_draws: int = DEFAULT_MAX_DRAWS) -> int | None:
    """java.util.Random next() calls that carry cursor `a` to cursor `b`."""
    return _walk(a, b, jr_step, max_draws)


def lcg_steps_between(a: int, b: int, max_steps: int = DEFAULT_MAX_DRAWS) -> int | None:
    """World.updateLCG advances that carry `a` to `b` (both unsigned-32)."""
    return _walk(a & LCG_MASK, b & LCG_MASK, lcg_step, max_steps)


#: Capture columns that participate in divergence detection, in report order.
#: (record key, human name, walker or None for exact-compare-only)
STREAMS = (
    ("world_seed48", "world_rand", draws_between),
    ("math_seed48", "math_rand", draws_between),
    ("block_seed48", "block_rand", draws_between),
    ("update_lcg", "update_lcg", lcg_steps_between),
)


class CursorError(Exception):
    """A sidecar is malformed or two sidecars are not comparable."""


def load_sidecar(path: str) -> list[dict]:
    """Read a JSON Lines cursor sidecar written by the bridge's rng_dump."""
    out: list[dict] = []
    with open(path, encoding="utf-8") as fh:
        for lineno, line in enumerate(fh, 1):
            line = line.strip()
            if not line:
                continue
            try:
                rec = json.loads(line)
            except ValueError as exc:
                raise CursorError(f"{path}:{lineno}: bad JSON: {exc}") from exc
            for key in ("seq", "world_seed48", "math_seed48", "block_seed48",
                        "update_lcg"):
                if key not in rec:
                    raise CursorError(f"{path}:{lineno}: missing field {key!r}")
            out.append(rec)
    if not out:
        raise CursorError(f"{path}: no cursor records")
    return out


def parse_sidecar(text: str) -> list[dict]:
    """Same as load_sidecar but from an in-memory rng_dump `records` blob."""
    out = []
    for lineno, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if not line:
            continue
        try:
            out.append(json.loads(line))
        except ValueError as exc:
            raise CursorError(f"record {lineno}: bad JSON: {exc}") from exc
    return out


@dataclass
class Transition:
    """Per-tick draw accounting between two consecutive cursors."""

    index: int
    world_time: int
    draws: dict = field(default_factory=dict)     # stream name -> count or None
    unreachable: list = field(default_factory=list)

    @property
    def ok(self) -> bool:
        return not self.unreachable


def profile(records: list[dict],
            max_draws: int = DEFAULT_MAX_DRAWS) -> list[Transition]:
    """Recover the per-tick draw count of every stream across a capture.

    An `unreachable` stream means the cursor did not arrive by stepping: either
    the tick drew more than `max_draws`, or something called setSeed().  In a
    world whose chunks are already generated the latter does not happen on the
    server tick path - World.setRandomSeed is worldgen-only (structure placement
    in MapGenStructure and friends) - so an unreachable world stream in steady
    state is a real finding, not noise.
    """
    out = []
    for i in range(len(records) - 1):
        a, b = records[i], records[i + 1]
        t = Transition(index=i, world_time=a.get("world_time", -1))
        for key, name, walker in STREAMS:
            n = walker(a[key], b[key], max_draws)
            t.draws[name] = n
            if n is None:
                t.unreachable.append(name)
        out.append(t)
    return out


@dataclass
class Divergence:
    """The first tick at which two captures disagree, and how."""

    index: int
    world_time: int
    seq: int
    stream: str
    expected: int
    actual: int
    #: "ahead by N" / "behind by N" / "unrelated" - a pure phase shift means a
    #: draw-count mismatch, anything else means a reseed or a different world.
    relation: str
    draws_expected: int | None = None
    draws_actual: int | None = None

    def describe(self) -> str:
        d = ""
        if self.draws_expected is not None and self.draws_actual is not None:
            delta = self.draws_actual - self.draws_expected
            d = (f"   tick consumed {self.draws_actual} draws, "
                 f"reference {self.draws_expected} ({delta:+d})")
        return (f"tick {self.world_time} (record {self.index}, seq {self.seq}): "
                f"stream {self.stream} diverged - {self.relation}\n"
                f"    expected 0x{self.expected:012x} "
                f"actual 0x{self.actual:012x}{d}")


def _classify(expected: int, actual: int, walker,
              max_draws: int) -> str:
    ahead = walker(expected, actual, max_draws)
    if ahead is not None:
        return f"candidate is {ahead} draw(s) AHEAD of reference"
    behind = walker(actual, expected, max_draws)
    if behind is not None:
        return f"candidate is {behind} draw(s) BEHIND reference"
    return "unrelated cursors (reseed, or a different world/run)"


def first_divergence(reference: list[dict], candidate: list[dict],
                     max_draws: int = DEFAULT_MAX_DRAWS) -> Divergence | None:
    """First record where `candidate` disagrees with `reference`, or None.

    Compares by position, which is what the capture guarantees: both sidecars
    are one record per server tick in tick order.  Reporting stops at the first
    divergence on purpose - an RNG offset is persistent, so every later tick is
    a consequence of this one and listing them all buries the cause.
    """
    n = min(len(reference), len(candidate))
    for i in range(n):
        r, c = reference[i], candidate[i]
        for key, name, walker in STREAMS:
            if r[key] == c[key]:
                continue
            de = da = None
            if i + 1 < len(reference) and i + 1 < len(candidate):
                de = walker(r[key], reference[i + 1][key], max_draws)
                da = walker(c[key], candidate[i + 1][key], max_draws)
            return Divergence(
                index=i,
                world_time=c.get("world_time", -1),
                seq=c.get("seq", -1),
                stream=name,
                expected=r[key],
                actual=c[key],
                relation=_classify(r[key], c[key], walker, max_draws),
                draws_expected=de,
                draws_actual=da,
            )
        rg = bool(r.get("world_have_gaussian"))
        cg = bool(c.get("world_have_gaussian"))
        if rg != cg:
            return Divergence(
                index=i, world_time=c.get("world_time", -1),
                seq=c.get("seq", -1), stream="world_gaussian",
                expected=int(rg), actual=int(cg),
                relation="stashed nextGaussian half differs "
                         "(an odd number of nextGaussian calls diverged)")
        # Optional fifth family (det_entity_rng). Absent on default dumps.
        rm, cm = _ents_seed48(r), _ents_seed48(c)
        if rm is not None and cm is not None:
            for eid in sorted(set(rm) | set(cm)):
                ev, av = rm.get(eid), cm.get(eid)
                if ev == av:
                    continue
                relation = "entity missing on one side"
                de = da = None
                if ev is not None and av is not None:
                    relation = _classify(ev, av, draws_between, max_draws)
                    if i + 1 < len(reference) and i + 1 < len(candidate):
                        nr, nc = _ents_seed48(reference[i + 1]), _ents_seed48(candidate[i + 1])
                        if nr is not None and nc is not None and eid in nr and eid in nc:
                            de = draws_between(ev, nr[eid], max_draws)
                            da = draws_between(av, nc[eid], max_draws)
                return Divergence(
                    index=i,
                    world_time=c.get("world_time", -1),
                    seq=c.get("seq", -1),
                    stream="entity_rand[%d]" % eid,
                    expected=-1 if ev is None else ev,
                    actual=-1 if av is None else av,
                    relation=relation,
                    draws_expected=de,
                    draws_actual=da,
                )
    return None


def _ents_seed48(rec):
    """Map eid -> seed48 from an optional `ents` array. None if the key is absent."""
    ents = rec.get("ents")
    if ents is None:
        return None
    if isinstance(ents, str):
        try:
            ents = json.loads(ents)
        except ValueError:
            return None
    out = {}
    for e in ents:
        try:
            out[int(e["eid"])] = int(e["seed48"])
        except (KeyError, TypeError, ValueError):
            continue
    return out


def shift_stream(records: list[dict], index: int, stream_key: str = "world_seed48",
                 draws: int = 1) -> list[dict]:
    """Return a copy with `stream_key` advanced by `draws` from `index` onward.

    This is the offline negative control: it models exactly what a real RNG
    misalignment does - an extra draw at one tick, and a stream that stays
    offset forever after - without needing a live JVM.
    """
    step = lcg_step if stream_key == "update_lcg" else jr_step
    out = [dict(r) for r in records]
    for i in range(index, len(out)):
        v = out[i][stream_key]
        for _ in range(draws):
            v = step(v)
        out[i][stream_key] = v
    return out


def _main(argv: list[str]) -> int:
    import argparse

    ap = argparse.ArgumentParser(
        description="java.util.Random cursor accounting for tape captures")
    ap.add_argument("reference", help="reference cursor sidecar (.jsonl)")
    ap.add_argument("candidate", nargs="?",
                    help="candidate sidecar; omit to self-profile the reference")
    ap.add_argument("--max-draws", type=int, default=DEFAULT_MAX_DRAWS)
    ap.add_argument("--profile", action="store_true",
                    help="print per-tick recovered draw counts")
    args = ap.parse_args(argv)

    ref = load_sidecar(args.reference)
    print(f"reference: {len(ref)} cursor records from {args.reference}")

    if args.candidate is None:
        prof = profile(ref, args.max_draws)
        bad = [t for t in prof if not t.ok]
        if args.profile:
            for t in prof:
                print(f"  tick {t.world_time}: " + "  ".join(
                    f"{k}={v}" for k, v in t.draws.items()))
        tot = sum(t.draws["world_rand"] or 0 for t in prof)
        print(f"{len(prof)} transitions, {tot} world draws total, "
              f"{len(bad)} unreachable")
        for t in bad[:5]:
            print(f"  UNREACHABLE tick {t.world_time}: {', '.join(t.unreachable)}")
        return 1 if bad else 0

    cand = load_sidecar(args.candidate)
    print(f"candidate: {len(cand)} cursor records from {args.candidate}")
    div = first_divergence(ref, cand, args.max_draws)
    if div is None:
        print(f"PASS  no RNG divergence across {min(len(ref), len(cand))} ticks")
        return 0
    print("FAIL  " + div.describe())
    return 1


if __name__ == "__main__":
    import sys

    sys.exit(_main(sys.argv[1:]))
