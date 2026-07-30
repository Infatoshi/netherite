"""capture_scenarios.py - REAL-GAME tick-trace capture for the P1 block-tick families.

Drives the live qrl mod (java/qrl_client.py protocol + the setblocks/getblocks
extension) to record, per game tick, an id<<4|meta cuboid of a prepared scenario.
One harness, N scenarios: each scenario clears a fixed cuboid high in the air far
from spawn, floods it with an explicit initial block layout (so the C side loads a
byte-identical start), then steps N WorldServer ticks dumping the cuboid every tick.

Output: one <name>.jsonl per scenario under scenarios/.
  line 0  header: {name,family,ox,oy,oz,nx,ny,nz,seed,ticks,doFireTick,
                   randomTickSpeed,skylight,blocklight}
  line i  {"tick":i-1,"blocks":[id<<4|meta, ...]}   (getblocks order: y,z,x major)

Deterministic families (water/lava flow, falling) are captured with randomTickSpeed
0 so ONLY scheduled ticks run; random families (fire is scheduled but its spread
uses this.rand; grass/crops/melt are random-tick driven) get the rule they need.

Run headless against the booted client:
  uv run --no-project python blaze/verify/tick_trace/capture_scenarios.py
  uv run --no-project python .../capture_scenarios.py --debug water  # timing probe
"""
import json
import os
import socket
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SCN_DIR = os.path.join(HERE, "scenarios")

# block ids (mirror blaze/core/mc_blocks.h)
AIR, STONE, GRASS, DIRT, COBBLE = 0, 1, 2, 3, 4
PLANKS, FLOWING_WATER, WATER = 5, 8, 9
FLOWING_LAVA, LAVA, SAND, GRAVEL = 10, 11, 12, 13
LOG, LEAVES, WHEAT, FARMLAND = 17, 18, 59, 60
SNOW_LAYER, ICE, GLOWSTONE = 78, 79, 89


def st(id_, meta=0):
    return ((id_ & 0xFFF) << 4) | (meta & 0xF)


class Bridge:
    def __init__(self, host="127.0.0.1", port=25575):
        self.s = socket.create_connection((host, port), timeout=180)
        self.f = self.s.makefile("rwb")

    def cmd(self, obj):
        self.f.write((json.dumps(obj) + "\n").encode())
        self.f.flush()
        line = self.f.readline()
        if not line:
            raise ConnectionError("bridge closed")
        return json.loads(line.decode())

    def reset(self, world, timeout=180.0):
        deadline = time.time() + timeout
        while True:
            o = self.cmd({"cmd": "reset", "world": world})
            if o.get("ok"):
                return o
            if not o.get("loading"):
                return o
            if time.time() > deadline:
                raise TimeoutError("world load timeout")
            time.sleep(1.0)

    def step(self, n=1):
        o = None
        for _ in range(n):
            o = self.cmd({"cmd": "step", "action": {}})
        return o

    def runcmds(self, cmds):
        return self.cmd({"cmd": "runcmds", "action": {"cmds": cmds}})

    def setblocks(self, blocks):
        r = self.cmd({"cmd": "setblocks", "action": {"blocks": blocks}})
        return r.get("num_ticks", 0)      # placement tick (atomic with the writes)

    def getblocks(self, ox, oy, oz, nx, ny, nz, path):
        r = self.cmd({"cmd": "getblocks", "action": {
            "x0": ox, "y0": oy, "z0": oz,
            "x1": ox + nx - 1, "y1": oy + ny - 1, "z1": oz + nz - 1, "file": path}})
        if not r.get("ok"):
            raise RuntimeError("getblocks failed: %s" % r)
        with open(path, "rb") as fh:
            raw = fh.read()
        os.remove(path)
        vals = [raw[2 * i] | (raw[2 * i + 1] << 8) for i in range(len(raw) // 2)]
        assert len(vals) == nx * ny * nz, (len(vals), nx * ny * nz)
        return vals, r.get("num_ticks", 0)


# ---- scenario builders -------------------------------------------------------
# A scenario returns (meta_dict, initial_blocks) where initial_blocks is a dict
# {(lx,ly,lz): state} in LOCAL coords [0,nx)x[0,ny)x[0,nz); cells omitted -> AIR.

def scn_water():
    nx, ny, nz = 10, 3, 3
    b = {}
    for x in range(nx):                      # stone floor
        for z in range(nz):
            b[(x, 0, z)] = st(STONE)
    for x in range(nx):                       # channel walls (contain the flow)
        b[(x, 1, 0)] = st(STONE)
        b[(x, 1, 2)] = st(STONE)
    b[(0, 1, 1)] = st(STONE)                  # near end cap (seal the trough; no cliff at x<0)
    b[(9, 1, 1)] = st(STONE)                  # far end cap
    b[(1, 1, 1)] = st(FLOWING_WATER)          # source id8 m0 -> first update placeStaticBlock->id9
    return dict(name="water", family="fluid", nx=nx, ny=ny, nz=nz, ticks=60,
                doFireTick=0, randomTickSpeed=0, skylight=15, blocklight=0), b


def scn_lava():
    nx, ny, nz = 8, 3, 3
    b = {}
    for x in range(nx):
        for z in range(nz):
            b[(x, 0, z)] = st(STONE)
    for x in range(nx):
        b[(x, 1, 0)] = st(STONE)
        b[(x, 1, 2)] = st(STONE)
    b[(0, 1, 1)] = st(STONE)                  # near end cap (seal the trough; no cliff at x<0)
    b[(7, 1, 1)] = st(STONE)
    b[(1, 1, 1)] = st(FLOWING_LAVA)           # source id10 m0 -> first update placeStaticBlock->id11; spreads 3
    return dict(name="lava", family="fluid", nx=nx, ny=ny, nz=nz, ticks=150,
                doFireTick=0, randomTickSpeed=0, skylight=15, blocklight=0), b


def scn_falling():
    nx, ny, nz = 3, 9, 3
    b = {}
    for x in range(nx):
        for z in range(nz):
            b[(x, 0, z)] = st(STONE)          # landing floor
    b[(1, 6, 1)] = st(SAND)                   # floating sand, falls to y=1
    b[(1, 7, 1)] = st(GRAVEL)                 # floating gravel on top
    return dict(name="falling", family="falling", nx=nx, ny=ny, nz=nz, ticks=20,
                doFireTick=0, randomTickSpeed=0, skylight=15, blocklight=0), b


def scn_fire():
    nx, ny, nz = 5, 5, 5
    b = {}
    for x in range(nx):                        # stone base
        for z in range(nz):
            b[(x, 0, z)] = st(STONE)
    for x in range(1, 4):                      # 3x3 planks slab (fuel bed at y=1)
        for z in range(1, 4):
            b[(x, 1, z)] = st(PLANKS)
    b[(2, 2, 1)] = st(PLANKS)                  # planks flanks for the fire to spread into
    b[(2, 2, 3)] = st(PLANKS)
    b[(1, 2, 2)] = st(PLANKS)
    b[(3, 2, 2)] = st(PLANKS)
    b[(2, 2, 2)] = st(51)                      # fire, on the slab, flanked by fuel
    return dict(name="fire", family="fire", nx=nx, ny=ny, nz=nz, ticks=80,
                doFireTick=1, randomTickSpeed=0, skylight=15, blocklight=0), b


def scn_grass():
    nx, ny, nz = 5, 3, 5
    b = {}
    for x in range(nx):
        for z in range(nz):
            b[(x, 0, z)] = st(DIRT)           # dirt patch
    b[(0, 0, 0)] = st(GRASS)                   # one grass seed to spread
    return dict(name="grass", family="grass", nx=nx, ny=ny, nz=nz, ticks=80,
                doFireTick=0, randomTickSpeed=100, skylight=15, blocklight=0), b


def scn_crops():
    nx, ny, nz = 3, 3, 3
    b = {}
    for x in range(nx):
        for z in range(nz):
            b[(x, 0, z)] = st(FARMLAND, 7)    # wet farmland
    for x in range(nx):
        for z in range(nz):
            b[(x, 1, z)] = st(WHEAT, 0)       # wheat age 0
    return dict(name="crops", family="crops", nx=nx, ny=ny, nz=nz, ticks=120,
                doFireTick=0, randomTickSpeed=100, skylight=15, blocklight=0), b


def scn_ice():
    nx, ny, nz = 3, 3, 3
    b = {}
    for x in range(nx):
        for z in range(nz):
            b[(x, 0, z)] = st(STONE)
    for x in range(nx):
        for z in range(nz):
            b[(x, 1, z)] = st(ICE)
    b[(0, 2, 0)] = st(GLOWSTONE)              # light source -> melt
    return dict(name="ice", family="melt", nx=nx, ny=ny, nz=nz, ticks=60,
                doFireTick=0, randomTickSpeed=100, skylight=15, blocklight=12), b


def scn_snow():
    nx, ny, nz = 3, 3, 3
    b = {}
    for x in range(nx):
        for z in range(nz):
            b[(x, 0, z)] = st(STONE)
    for x in range(nx):
        for z in range(nz):
            b[(x, 1, z)] = st(SNOW_LAYER, 0)
    b[(0, 2, 0)] = st(GLOWSTONE)
    return dict(name="snow", family="melt", nx=nx, ny=ny, nz=nz, ticks=60,
                doFireTick=0, randomTickSpeed=100, skylight=15, blocklight=12), b


SCENARIOS = [scn_water, scn_lava, scn_falling, scn_fire,
             scn_grass, scn_crops, scn_ice, scn_snow]

# world-space anchor for every scenario cuboid (far from spawn, high in the air).
OX, OY, OZ = 1000, 100, 1000
PLAYER = "@p"


def liquid_last(id_):
    # place solids first, liquids/fire last so containment walls exist before flow.
    return id_ in (FLOWING_WATER, WATER, FLOWING_LAVA, LAVA, 51)


def prepare(br, meta, cells):
    nx, ny, nz = meta["nx"], meta["ny"], meta["nz"]
    # clear a generous box around the cuboid so no superflat/stray block interferes,
    # then place the explicit layout.
    pad = 4
    br.runcmds([
        "gamerule doDaylightCycle false",
        "gamerule doMobSpawning false",
        "gamerule doWeatherCycle false",
        "gamerule randomTickSpeed %d" % meta["randomTickSpeed"],
        "gamerule doFireTick %s" % ("true" if meta["doFireTick"] else "false"),
        "weather clear 1000000",
        "time set 6000",
        "gamerule spawnRadius 0",
        "tp %s %d %d %d" % (PLAYER, OX + nx // 2, OY + ny + 2, OZ + nz // 2),
    ])
    br.step(6)  # let chunks load around the player
    # clear the padded box to air (fill handles up to 32768 blocks)
    br.runcmds(["fill %d %d %d %d %d %d minecraft:air" % (
        OX - pad, OY - 1, OZ - pad,
        OX + nx - 1 + pad, OY + ny - 1 + pad, OZ + nz - 1 + pad)])
    br.step(2)
    # build the explicit initial layout (solids first, liquids/fire last)
    blocks = []
    for (lx, ly, lz), s in cells.items():
        blocks.append((lx, ly, lz, s >> 4, s & 0xF))
    blocks.sort(key=lambda t: liquid_last(t[3]))
    return br.setblocks([[OX + lx, OY + ly, OZ + lz, i, m] for (lx, ly, lz, i, m) in blocks])


def num_ticks(br):
    return br.cmd({"cmd": "stats"}).get("num_ticks", 0)


def capture(br, builder):
    meta, cells = builder()
    nx, ny, nz = meta["nx"], meta["ny"], meta["nz"]
    tmp = os.path.join(SCN_DIR, "_tmp.bin")
    base = prepare(br, meta, cells)   # PLACEMENT tick (atomic with the setblocks writes)
    hdr = dict(meta)
    hdr.update(ox=OX, oy=OY, oz=OZ, seed=0)
    out = os.path.join(SCN_DIR, meta["name"] + ".jsonl")
    # `step` is not exactly one WorldServer tick, so gate frames on the server tick
    # counter and record each frame's ACTUAL tick offset ("t") from the PLACEMENT tick.
    # Every tick label comes back ATOMICALLY from getblocks (captured inside the same
    # server task as the dump), so labels are exact regardless of step/stats jitter.
    # onBlockAdded schedules fire relative to placement (water first flows at t=5).
    with open(out, "w") as fh:
        fh.write(json.dumps(hdr) + "\n")
        v, t0 = br.getblocks(OX, OY, OZ, nx, ny, nz, tmp)   # frame 0 (post-setblocks)
        fh.write(json.dumps({"t": t0 - base, "blocks": v}) + "\n")
        prev = t0
        for _ in range(meta["ticks"]):
            guard = 0
            while guard < 40:
                br.step(1); guard += 1
                v, nt = br.getblocks(OX, OY, OZ, nx, ny, nz, tmp)
                if nt > prev:
                    break
            prev = nt
            fh.write(json.dumps({"t": nt - base, "blocks": v}) + "\n")
    print("wrote %s (%d frames, %dx%dx%d)" % (out, meta["ticks"] + 1, nx, ny, nz))
    return out


def debug_water(br):
    """Timing probe: verify one step == one WorldServer tick via water flow (+5/cell)."""
    meta, cells = scn_water()
    nx, ny, nz = meta["nx"], meta["ny"], meta["nz"]
    tmp = os.path.join(SCN_DIR, "_tmp.bin")
    prepare(br, meta, cells)

    def flow_front(v):
        # rightmost x at y=1,z=1 that holds any water (source 9 or flowing 8)
        front = -1
        for x in range(nx):
            s = v[((1) * nz + 1) * nx + x]
            if (s >> 4) in (WATER, FLOWING_WATER):
                front = x
        return front
    v, _ = br.getblocks(OX, OY, OZ, nx, ny, nz, tmp)
    print("tick  0 front=%d" % flow_front(v))
    for t in range(1, 41):
        br.step(1)
        v, _ = br.getblocks(OX, OY, OZ, nx, ny, nz, tmp)
        print("tick %2d front=%d" % (t, flow_front(v)))


def diag(br):
    """Fast diagnostic: does the target chunk actually tick, and does a placed liquid flow?"""
    nx, ny, nz = 10, 3, 3
    tmp = os.path.join(SCN_DIR, "_tmp.bin")
    br.runcmds([
        "gamerule randomTickSpeed 0", "gamerule doFireTick false",
        "gamerule doDaylightCycle false", "weather clear 1000000", "time set 6000",
        "tp %s %d %d %d" % (PLAYER, OX + 5, OY + 6, OZ + 1),
    ])
    br.step(6)
    st0 = br.cmd({"cmd": "stats"})
    br.step(10)
    st1 = br.cmd({"cmd": "stats"})
    print("[diag] num_ticks: %s -> %s (delta=%s) for 10 steps" % (
        st0.get("num_ticks"), st1.get("num_ticks"),
        (st1.get("num_ticks", 0) - st0.get("num_ticks", 0))))
    for src_name, src in (("FLOWING_WATER", FLOWING_WATER), ("WATER", WATER)):
        br.runcmds(["fill %d %d %d %d %d %d minecraft:air" % (
            OX - 2, OY - 1, OZ - 2, OX + nx + 1, OY + ny + 1, OZ + nz + 1)])
        cells = {}
        for x in range(nx):
            for z in range(nz):
                cells[(x, 0, z)] = st(STONE)
        for x in range(nx):
            cells[(x, 1, 0)] = st(STONE)
            cells[(x, 1, 2)] = st(STONE)
        cells[(9, 1, 1)] = st(STONE)
        cells[(1, 1, 1)] = st(src)
        blocks = [[OX + lx, OY + ly, OZ + lz, s >> 4, s & 0xF] for (lx, ly, lz), s in cells.items()]
        blocks.sort(key=lambda t: liquid_last(t[3]))
        br.setblocks(blocks)
        v0, _ = br.getblocks(OX, OY, OZ, nx, ny, nz, tmp)
        br.step(20)
        v1, _ = br.getblocks(OX, OY, OZ, nx, ny, nz, tmp)
        row0 = [v0[(1 * nz + 1) * nx + x] >> 4 for x in range(nx)]
        row1 = [v1[(1 * nz + 1) * nx + x] >> 4 for x in range(nx)]
        print("[diag] src=%-13s y1z1 row  t0=%s" % (src_name, row0))
        print("[diag] src=%-13s y1z1 row t20=%s" % (src_name, row1))


def main():
    br = Bridge()
    # reuse the auto-launched world; enable cheats so runcmds fill/gamerule/tp apply.
    br.reset({})
    if sys.argv[1:2] == ["--diag"]:
        diag(br)
        return
    # NOTE: do NOT overclock. Free-running the server desyncs the num_ticks read from
    # the getblocks scheduled-task dump (frames land on inconsistent tick boundaries) and
    # inflates per-frame offsets. Normal 20 TPS keeps each frame ~1 tick apart with a tight
    # step -> stats -> getblocks barrier.
    args = sys.argv[1:]
    if args and args[0] == "--debug":
        debug_water(br)
        return
    names = set(a for a in args if not a.startswith("-"))
    for builder in SCENARIOS:
        meta, _ = builder()
        if names and meta["name"] not in names:
            continue
        capture(br, builder)
    print("all scenarios captured")


if __name__ == "__main__":
    main()
