#!/usr/bin/env python3
"""Run the unmodified Python trainer with profiling-only NVTX phase ranges.

The lane is forbidden from editing ppo_chain_cu.py. This launcher reads that
file, inserts ranges into a transient source string at stable comment anchors,
and executes it with the original __file__ so imports and path resolution are
unchanged. It is only a ceiling diagnostic, never a benchmark candidate.
"""

from pathlib import Path


HERE = Path(__file__).resolve().parent
TRAINER = HERE.parents[1] / "env" / "ppo_chain_cu.py"


def replace_once(source, old, new):
    count = source.count(old)
    if count != 1:
        raise RuntimeError(f"profiling anchor count {count}, expected 1: {old!r}")
    return source.replace(old, new, 1)


def instrument(source):
    source = replace_once(
        source,
        "        # weights may have changed in the previous chunk's PPO update\n",
        "        torch.cuda.nvtx.range_push(f'cgraph_ceiling/chunk/{chunk}')\n"
        "        torch.cuda.nvtx.range_push('cgraph_ceiling/rollout')\n"
        "        # weights may have changed in the previous chunk's PPO update\n",
    )
    source = replace_once(
        source,
        "        # ---- GAE ----\n",
        "        torch.cuda.nvtx.range_pop()\n"
        "        torch.cuda.nvtx.range_push('cgraph_ceiling/gae')\n"
        "        # ---- GAE ----\n",
    )
    source = replace_once(
        source,
        "        # ---- PPO ----\n",
        "        torch.cuda.nvtx.range_pop()\n"
        "        torch.cuda.nvtx.range_push('cgraph_ceiling/update')\n"
        "        # ---- PPO ----\n",
    )
    anchor = "        if bench_sample:\n            torch.cuda.synchronize(dev)\n"
    index = source.rfind(anchor)
    if index < 0:
        raise RuntimeError("profiling end anchor not found")
    source = (
        source[:index]
        + "        torch.cuda.nvtx.range_pop()\n"
        + "        torch.cuda.nvtx.range_pop()\n"
        + source[index:]
    )
    return source


code = compile(instrument(TRAINER.read_text()), str(TRAINER), "exec")
scope = {"__file__": str(TRAINER), "__name__": "__main__"}
exec(code, scope)
