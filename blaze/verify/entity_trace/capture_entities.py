"""capture_entities.py - REAL-GAME per-tick ENTITY trace capture (PORT_MATRIX P1).

Drives the live qrl mod (setblocks/getblocks/getblocks + the summon/getentities/
killentities extension) to record, per game tick, the raw-bit state of every
non-player entity in a prepared sealed arena. Companion to tick_trace's block
capture; here the moving thing is an ENTITY, not a block.

One harness, N scenarios. Each scenario freezes the world (randomTickSpeed 0,
doMobSpawning/doDaylightCycle false, clear weather, kill stray entities), clears a
cuboid high in the air far from spawn, builds an explicit flat/sealed block layout
(so the C side loads a byte-identical static world), summons ONE entity with an
EXACT initial position + motion (bow/AI RNG bypassed), then steps N WorldServer
ticks dumping every entity each tick.

Output: one <name>.jsonl per scenario under scenarios/.
  line 0  header: {name,kind,ai,ox,oy,oz,nx,ny,nz,ticks,blocks:[id<<4|meta,...]}
  line i  {"t":<tick offset from summon>, "ents":[{eid,type,x,y,z,mx,my,mz,
           yaw,pitch,onGround,fall,air,inGround,ticksInAir,health}]}
  doubles (x/y/z/mx/my/mz) are Double.doubleToRawLongBits (signed decimal);
  floats (yaw/pitch/fall/health) are Float.floatToRawIntBits. The C harness
  bit-casts these back, so pos/motion survive JSON with zero rounding.

Run headless against the booted client (Run A/B):
  uv run --no-project python blaze/verify/entity_trace/capture_entities.py
  uv run --no-project python .../capture_entities.py arrow_flat   # subset
"""
import json
import os
import socket
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
SCN_DIR = os.path.join(HERE, "scenarios")

# block ids (mirror blaze/core/mc_blocks.h)
AIR, STONE = 0, 1
WATER, ICE = 9, 79

# world-space anchor for every scenario cuboid (far from spawn, high in the air).
OX, OY, OZ = 1000, 100, 1000
PLAYER = "@p"


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
            if o.get("ok") or not o.get("loading"):
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
        return r.get("num_ticks", 0)

    def killentities(self):
        return self.cmd({"cmd": "killentities", "action": {}})

    def summon(self, kind, x, y, z, mx=None, my=None, mz=None, noai=False):
        a = {"type": kind, "x": x, "y": y, "z": z, "noai": noai}
        if mx is not None:
            a.update(mx=mx, my=my, mz=mz)
        r = self.cmd({"cmd": "summon", "action": a})
        if not r.get("ok"):
            raise RuntimeError("summon failed: %s" % r)
        return r.get("eid"), r.get("num_ticks", 0)

    def getentities(self):
        r = self.cmd({"cmd": "getentities", "action": {}})
        if not r.get("ok"):
            raise RuntimeError("getentities failed: %s" % r)
        return r.get("ents", []), r.get("player"), r.get("num_ticks", 0)

    def summon_item(self, item, count, meta, x, y, z, mx, my, mz, pickupdelay):
        r = self.cmd({"cmd": "summon", "action": {
            "type": "item", "item": item, "count": count, "meta": meta,
            "x": x, "y": y, "z": z, "mx": mx, "my": my, "mz": mz,
            "pickupdelay": pickupdelay}})
        if not r.get("ok"):
            raise RuntimeError("summon item failed: %s" % r)
        return r.get("eid"), r.get("num_ticks", 0)

    def summon_orb(self, value, x, y, z, mx, my, mz):
        r = self.cmd({"cmd": "summon", "action": {
            "type": "xporb", "value": value,
            "x": x, "y": y, "z": z, "mx": mx, "my": my, "mz": mz}})
        if not r.get("ok"):
            raise RuntimeError("summon orb failed: %s" % r)
        return r.get("eid"), r.get("num_ticks", 0)

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
        return vals


# ---- scenario definitions ----------------------------------------------------
# Each returns (meta, cells, summon_spec). cells is {(lx,ly,lz): state} in LOCAL
# coords; summon_spec is (kind, lx, ly, lz float, mx, my, mz, noai). Motion floats
# are EXACT decimals -> exact doubles.

def flat_floor(nx, nz, layers=1):
    b = {}
    for ly in range(layers):
        for x in range(nx):
            for z in range(nz):
                b[(x, ly, z)] = st(STONE)
    return b


def scn_arrow_flat():
    nx, ny, nz = 48, 8, 5
    b = flat_floor(nx, nz)
    # arrow at local (2.5, 4, 2.5), fired horizontally +X at 1.5 b/tick
    spec = ("arrow", 2.5, 4.0, 2.5, 1.5, 0.0, 0.0, False)
    return dict(name="arrow_flat", kind="arrow", ai=0, nx=nx, ny=ny, nz=nz, ticks=60), b, spec


def scn_arrow_45():
    nx, ny, nz = 48, 24, 5
    b = flat_floor(nx, nz)
    # 45deg up: mx=my=1.0 -> arcs and lands
    spec = ("arrow", 2.5, 4.0, 2.5, 1.0, 1.0, 0.0, False)
    return dict(name="arrow_45", kind="arrow", ai=0, nx=nx, ny=ny, nz=nz, ticks=60), b, spec


def scn_arrow_wall():
    nx, ny, nz = 20, 8, 5
    b = flat_floor(nx, nz)
    wx = 15                                   # vertical stone wall at local x=15
    for ly in range(1, ny):
        for z in range(nz):
            b[(wx, ly, z)] = st(STONE)
    spec = ("arrow", 2.5, 4.0, 2.5, 2.0, 0.0, 0.0, False)   # horizontal into the wall
    return dict(name="arrow_wall", kind="arrow", ai=0, nx=nx, ny=ny, nz=nz, ticks=40), b, spec


def scn_zombie_drop():
    nx, ny, nz = 5, 10, 5
    b = flat_floor(nx, nz)
    # Pure gravity fall from 5 blocks above the floor top (feet at local y=6).
    # NOTE: 1.11.2 NoAI does NOT fall - EntityLiving.isServerWorld() returns
    # `super && !isAIDisabled()`, and EntityLivingBase.moveEntityWithHeading gates ALL
    # gravity/movement on isServerWorld(), so a NoAI mob is frozen server-side. To get a
    # clean gravity/drag/landing golden we leave AI ON and keep the player far (>16 blocks,
    # no target in range): moveForward/Strafing stay 0, so horizontal is 0 and vertical is
    # the pure EntityLivingBase.travel recurrence. See RESULTS.md.
    spec = ("zombie", 2.5, 6.0, 2.5, None, None, None, False)
    return dict(name="zombie_drop", kind="zombie", ai=0, nx=nx, ny=ny, nz=nz, ticks=40,
                player_far=True), b, spec


def scn_zombie_ai_pen():
    nx, ny, nz = 20, 6, 20
    b = flat_floor(nx, nz)
    for ly in range(1, ny):                   # seal the pen (walls) so it can't wander out
        for x in range(nx):
            b[(x, ly, 0)] = st(STONE)
            b[(x, ly, nz - 1)] = st(STONE)
        for z in range(nz):
            b[(0, ly, z)] = st(STONE)
            b[(nx - 1, ly, z)] = st(STONE)
    # zombie WITH AI, ~8 blocks from the player (player tp'd to local (4,1,10)).
    # 100 frames captures acquisition + approach (distance closes to melee) without running
    # long enough for the player to die -> auto-respawn teleport (which would look like a
    # false "pursuit stall" in the trace). Player forced to survival so it is a valid target.
    spec = ("zombie", 12.0, 1.0, 10.0, None, None, None, False)
    return dict(name="zombie_ai_pen", kind="zombie", ai=1, nx=nx, ny=ny, nz=nz, ticks=100), b, spec


# ---- P2: EntityItem + EntityXPOrb scenarios (item_trace_verify.c) ------------
# meta["summons"] drives multi-entity spawn; pickupdelay=32767 so the parked player
# cannot vacuum items. Motion is exact decimal -> exact double.

def scn_item_settle():
    """Item with exact horizontal motion settles on stone (air drag + ground friction)."""
    nx, ny, nz = 16, 8, 16
    b = flat_floor(nx, nz)
    meta = dict(name="item_settle", kind="item", ai=0, nx=nx, ny=ny, nz=nz, ticks=80,
                player_far=True,
                summons=[{"type": "item", "item": "minecraft:stone", "count": 1, "meta": 0,
                          "lx": 4.0, "ly": 3.0, "lz": 4.0,
                          "mx": 0.2, "my": 0.0, "mz": 0.1, "pickupdelay": 32767}])
    return meta, b, None


def scn_item_ice():
    """Same as settle but on ice: ground friction uses slipperiness 0.98 instead of 0.6."""
    nx, ny, nz = 16, 8, 16
    b = {}
    for x in range(nx):
        for z in range(nz):
            b[(x, 0, z)] = st(ICE)
    meta = dict(name="item_ice", kind="item", ai=0, nx=nx, ny=ny, nz=nz, ticks=80,
                player_far=True,
                summons=[{"type": "item", "item": "minecraft:cobblestone", "count": 1, "meta": 0,
                          "lx": 4.0, "ly": 3.0, "lz": 4.0,
                          "mx": 0.3, "my": 0.0, "mz": 0.0, "pickupdelay": 32767}])
    return meta, b, None


def scn_item_merge():
    """Two compatible stacks 0.4 apart: combineItems merge within the 20-tick search window.

    pickupdelay must NOT be 32767 - EntityItem.combineItems rejects the infinite-delay
    sentinel. 200 is long enough that a far player cannot vacuum during the 40-tick window
    but merge is allowed.
    """
    nx, ny, nz = 12, 6, 12
    b = flat_floor(nx, nz)
    meta = dict(name="item_merge", kind="item", ai=0, nx=nx, ny=ny, nz=nz, ticks=40,
                player_far=True,
                summons=[
                    {"type": "item", "item": "minecraft:stone", "count": 20, "meta": 0,
                     "lx": 5.0, "ly": 1.0, "lz": 5.0,
                     "mx": 0.0, "my": 0.0, "mz": 0.0, "pickupdelay": 200},
                    {"type": "item", "item": "minecraft:stone", "count": 15, "meta": 0,
                     "lx": 5.4, "ly": 1.0, "lz": 5.0,
                     "mx": 0.0, "my": 0.0, "mz": 0.0, "pickupdelay": 200},
                ])
    return meta, b, None


def scn_item_water():
    """Item dropped into a walled source-water pool (flow == 0; same gravity/drag as air)."""
    nx, ny, nz = 12, 8, 12
    b = flat_floor(nx, nz)
    # 5x5 pool of source water at y=1, walls around so getFlow is zero.
    for x in range(3, 8):
        for z in range(3, 8):
            b[(x, 1, z)] = st(WATER)
    for ly in range(1, 3):
        for x in range(2, 9):
            b[(x, ly, 2)] = st(STONE)
            b[(x, ly, 8)] = st(STONE)
        for z in range(2, 9):
            b[(2, ly, z)] = st(STONE)
            b[(8, ly, z)] = st(STONE)
    meta = dict(name="item_water", kind="item", ai=0, nx=nx, ny=ny, nz=nz, ticks=60,
                player_far=True,
                summons=[{"type": "item", "item": "minecraft:stone", "count": 1, "meta": 0,
                          "lx": 5.5, "ly": 4.0, "lz": 5.5,
                          "mx": 0.0, "my": 0.0, "mz": 0.0, "pickupdelay": 32767}])
    return meta, b, None


def scn_xporb_attract():
    """XP orb ~6 blocks from a survival player: free-fall then attraction until pickup."""
    nx, ny, nz = 20, 6, 12
    b = flat_floor(nx, nz)
    # player parked at local (4,1,5); orb at (10,2,5) ~6 blocks away on X.
    meta = dict(name="xporb_attract", kind="xporb", ai=0, nx=nx, ny=ny, nz=nz, ticks=120,
                player_near=(4.0, 1.0, 5.0),
                summons=[{"type": "xporb", "value": 1,
                          "lx": 10.0, "ly": 2.0, "lz": 5.0,
                          "mx": 0.0, "my": 0.0, "mz": 0.0}])
    return meta, b, None


SCENARIOS = [scn_arrow_flat, scn_arrow_45, scn_arrow_wall,
             scn_zombie_drop, scn_zombie_ai_pen,
             scn_item_settle, scn_item_ice, scn_item_merge, scn_item_water,
             scn_xporb_attract]


def prepare(br, meta, cells):
    nx, ny, nz = meta["nx"], meta["ny"], meta["nz"]
    pad = 4
    # freeze the world; tp player near/above the arena (for the AI scenario the
    # player must be visible ~8 blocks from the pen zombie -> put them in the pen).
    player_near = meta.get("player_near")      # (lx,ly,lz) for xp-orb attraction
    if meta["ai"]:
        pxyz = (OX + 4, OY + 1, OZ + 10)              # pen: player ~8 blocks from the zombie
    elif player_near:
        pxyz = (OX + player_near[0], OY + player_near[1], OZ + player_near[2])
    elif meta.get("player_far"):
        pxyz = (OX + 40, OY + ny + 3, OZ + 40)        # drop/items: out of vacuum/target range
    else:
        pxyz = (OX + nx // 2, OY + ny + 3, OZ + nz // 2)
    # park position for the initial tp: for AI / player_near park 40 blocks ABOVE the final
    # floor spot so the air-fill does not drop the player through the removed floor. Other
    # scenarios keep the single early tp (player far/irrelevant).
    need_floor_tp = bool(meta["ai"] or player_near)
    park = (pxyz[0], pxyz[1] + 40, pxyz[2]) if need_floor_tp else pxyz
    br.runcmds([
        "gamerule doDaylightCycle false",
        "gamerule doMobSpawning false",
        "gamerule doWeatherCycle false",
        "gamerule randomTickSpeed 0",
        "gamerule doFireTick false",
        "gamerule mobGriefing false",
        "weather clear 1000000",
        "time set 6000",
        "gamerule spawnRadius 0",
        "tp %s %s %s %s" % (PLAYER, park[0], park[1], park[2]),
    ])
    br.step(6)                                 # let chunks load around the player
    br.runcmds(["fill %d %d %d %d %d %d minecraft:air" % (
        OX - pad, OY - 1, OZ - pad,
        OX + nx - 1 + pad, OY + ny - 1 + pad, OZ + nz - 1 + pad)])
    br.step(2)
    blocks = [[OX + lx, OY + ly, OZ + lz, s >> 4, s & 0xF]
              for (lx, ly, lz), s in cells.items()]
    br.setblocks(blocks)
    br.step(2)
    if meta["ai"]:
        # NOW tp the player onto the rebuilt floor (before the air-fill it would fall through
        # the removed floor -> embed below/in stone, out of the mob's line of sight, and the
        # zombie never acquires). Force survival + easy difficulty (a hostile mob only targets
        # a SURVIVAL player in 1.11.2), and resistance so its melee cannot kill the player ->
        # a death/respawn teleport inside the window would masquerade as a pursuit stall.
        br.runcmds(["tp %s %s %s %s" % (PLAYER, pxyz[0], pxyz[1], pxyz[2]),
                    "gamemode survival %s" % PLAYER, "difficulty easy",
                    "effect %s minecraft:resistance 1000000 4 true" % PLAYER,
                    # the pen has no roof; at noon (time 6000) the zombie burns in daylight
                    # (~1 HP/20t) -> a declining health column and eventual fire death that
                    # would pollute the AI golden. Force night (doDaylightCycle is already
                    # false, so it stays night): zombies do not burn -> health stays 20.
                    "time set 18000"])
        br.step(2)                             # let the player settle on the floor
    elif player_near:
        # XP orb attraction: non-spectator player within 8 blocks, grounded at known feet.
        # Integer-block tp (vanilla chat accepts floats but integer is unambiguous for @p),
        # creative first so a fall cannot kill mid-settle, then survival for attraction.
        px, py, pz = int(pxyz[0]), int(pxyz[1]), int(pxyz[2])
        br.runcmds(["gamemode creative %s" % PLAYER,
                    "tp %s %d %d %d" % (PLAYER, px, py + 2, pz)])
        br.step(2)
        br.runcmds(["tp %s %d %d %d" % (PLAYER, px, py, pz),
                    "gamemode survival %s" % PLAYER])
        br.step(4)                             # settle on the rebuilt floor
    br.killentities()                          # clean slate: only our summon exists
    br.step(1)


def capture(br, builder):
    meta, cells, spec = builder()
    nx, ny, nz = meta["nx"], meta["ny"], meta["nz"]
    tmp = os.path.join(SCN_DIR, "_tmp.bin")
    prepare(br, meta, cells)
    # snapshot the static world cuboid (never changes: randomTickSpeed 0, mobGriefing off)
    world_blocks = br.getblocks(OX, OY, OZ, nx, ny, nz, tmp)

    # summon: EITHER the single arrow/zombie spec tuple OR meta["summons"] (item/orb list).
    if meta.get("summons"):
        base = None
        for sm in meta["summons"]:
            if sm["type"] == "item":
                eid, b = br.summon_item(sm.get("item", "minecraft:stone"),
                                        sm.get("count", 1), sm.get("meta", 0),
                                        OX + sm["lx"], OY + sm["ly"], OZ + sm["lz"],
                                        sm["mx"], sm["my"], sm["mz"], sm.get("pickupdelay", 32767))
            elif sm["type"] == "xporb":
                eid, b = br.summon_orb(sm.get("value", 1),
                                       OX + sm["lx"], OY + sm["ly"], OZ + sm["lz"],
                                       sm["mx"], sm["my"], sm["mz"])
            else:
                raise RuntimeError("unknown summon type %s" % sm["type"])
            if base is None:
                base = b
    else:
        kind, lx, ly, lz, mx, my, mz, noai = spec
        eid, base = br.summon(kind, OX + lx, OY + ly, OZ + lz, mx, my, mz, noai)

    hdr = dict(meta)
    # summons/player_near are capture-only; strip them from the golden header
    hdr.pop("summons", None)
    hdr.pop("player_near", None)
    hdr.pop("player_far", None)
    hdr.update(ox=OX, oy=OY, oz=OZ, blocks=world_blocks)
    out = os.path.join(SCN_DIR, meta["name"] + ".jsonl")

    def arena_ents(ents):
        """Drop stray loaded entities outside the scenario cuboid (wolves/cows near spawn)."""
        keep = []
        for e in ents:
            # raw-bit fields are signed decimals of doubleToRawLongBits
            x = struct_unpack_d(e["x"]); y = struct_unpack_d(e["y"]); z = struct_unpack_d(e["z"])
            if (OX - 1 <= x <= OX + nx + 1 and
                    OY - 1 <= y <= OY + ny + 1 and
                    OZ - 1 <= z <= OZ + nz + 1):
                keep.append(e)
        return keep

    with open(out, "w") as fh:
        fh.write(json.dumps(hdr) + "\n")
        ents, player, t0 = br.getentities()    # frame 0 (post-summon)
        fh.write(json.dumps({"t": t0 - base, "ents": arena_ents(ents), "player": player}) + "\n")
        prev = t0
        for _ in range(meta["ticks"]):
            guard = 0
            while guard < 40:
                br.step(1)
                guard += 1
                ents, player, nt = br.getentities()
                if nt > prev:
                    break
            prev = nt
            fh.write(json.dumps({"t": nt - base, "ents": arena_ents(ents), "player": player}) + "\n")
    print("wrote %s (%d frames, arena %dx%dx%d, %s ai=%d)" % (
        out, meta["ticks"] + 1, nx, ny, nz, meta["kind"], meta["ai"]))
    return out


def struct_unpack_d(bits):
    import struct
    return struct.unpack("d", struct.pack("q", int(bits)))[0]


def main():
    br = Bridge()
    br.reset({})
    args = [a for a in sys.argv[1:] if not a.startswith("-")]
    names = set(args)
    for builder in SCENARIOS:
        meta, _, _ = builder()
        if names and meta["name"] not in names:
            continue
        capture(br, builder)
    print("all entity scenarios captured")


if __name__ == "__main__":
    main()
