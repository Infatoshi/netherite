"""Minimal client for the NetheriteMod socket bridge (stdlib only).

Fully-discrete env: step(action_dict) -> obs_dict. Action keys (all optional, 0/1
unless noted): forward, back, left, right, jump, sneak, sprint, attack, use,
hotbar (0-8), yaw (-1/0/1, 15deg step), pitch (-1/0/1, 15deg step).
Obs: x/y/z, yaw/pitch, vx/vy/vz, on_ground, health/food/air/xp, look{...}, entities[...].
Requires the MC client to be in a singleplayer world.
"""
import json
import socket
import sys
import time


class NetheriteEnv:
    def __init__(self, host="127.0.0.1", port=25575):
        self.s = socket.create_connection((host, port), timeout=15)
        # a tp into ungenerated chunks can block the server thread on worldgen
        # for minutes on a fresh save; per-recv timeout stays short but _cmd
        # keeps re-reading until read_deadline (the response always arrives on
        # this same connection; a retry read stays protocol-synced).
        # NB: raw recv with our own buffer - a makefile() object raises
        # "cannot read from timed out object" on ANY read after one timeout,
        # which silently broke the retry loop.
        self.s.settimeout(15)
        self._buf = b""

    def _cmd(self, obj, read_deadline=300.0):
        self.s.sendall((json.dumps(obj) + "\n").encode())
        t0 = time.time()
        while b"\n" not in self._buf:
            try:
                chunk = self.s.recv(65536)
            except socket.timeout:
                if time.time() - t0 > read_deadline:
                    raise
                continue
            if not chunk:
                raise ConnectionError("bridge closed")
            self._buf += chunk
        line, self._buf = self._buf.split(b"\n", 1)
        return json.loads(line.decode())

    def reset(self, world=None, timeout=120.0):
        """Reset; if no world is loaded, the bridge launches one (async) - poll until ready.

        world: optional {"seed": int, "mode": "survival"|"creative",
                          "type": "default"|"flat", "structures": bool}
        """
        import time
        msg = {"cmd": "reset", "world": world or {}}
        deadline = time.time() + timeout
        while True:
            o = self._cmd(msg)
            if o.get("ok"):
                return o
            if not o.get("loading"):
                return o  # a real error
            if time.time() > deadline:
                raise TimeoutError("world did not load in time")
            time.sleep(1.0)

    def stats(self):
        return self._cmd({"cmd": "stats"})

    def close_world(self):
        return self._cmd({"cmd": "close"})

    def overclock(self, ms=1):
        """ms=1 uncaps the server tick (run at compute speed); ms=50 restores realtime."""
        return self._cmd({"cmd": "overclock", "action": {"ms": ms}})

    def do(self, text):
        """Run a server command, e.g. '/fill ~ ~ ~ ~ ~ ~ minecraft:water' (cheats are on)."""
        return self._cmd({"cmd": "cmd", "action": {"text": text}})

    def spawn(self, count=60, radius=8):
        """Spawn `count` cows around the player (server-thread safe, no chat)."""
        return self._cmd({"cmd": "spawn", "action": {"count": count, "radius": radius}})

    def fluid(self, ftype="water", radius=8):
        """Flood a (2*radius+1) x 5 x (2*radius+1) box of flowing water/lava around the player."""
        return self._cmd({"cmd": "fluid", "action": {"type": ftype, "radius": radius}})

    def step(self, action):
        return self._cmd({"cmd": "step", "action": action})

    def obs(self):
        return self._cmd({"cmd": "obs"})

    def camera(self, file=None):
        """Dump render camera provenance (eye, effective FOV, options, world).

        Requires rebuilt NetheriteMod with cmd \"camera\". Optional file path writes
        the JSON on the game side as well as returning it.
        """
        action = {}
        if file:
            action["file"] = file
        return self._cmd({"cmd": "camera", "action": action})

    def close(self):
        self.s.close()


def smoke():
    e = NetheriteEnv()
    o = e.reset()
    print("reset:", json.dumps(o)[:200])
    if not o.get("ok"):
        print("NOT IN A WORLD -> open Singleplayer first. (socket itself is up.)")
        return
    x0, z0 = o["x"], o["z"]
    for _ in range(20):
        o = e.step({"forward": 1})
    print(f"after 20x forward: dx={o['x']-x0:+.3f} dz={o['z']-z0:+.3f} vx={o['vx']:+.3f} vz={o['vz']:+.3f}")
    y0 = o["yaw"]
    for _ in range(6):
        o = e.step({"yaw": 1})
    print(f"after 6x yaw+: yaw {y0:.1f} -> {o['yaw']:.1f}  (expect +90)")
    o = e.step({"pitch": 1})
    print(f"pitch step -> {o['pitch']:.1f}")
    print("look:", json.dumps(o.get("look")))
    print("nearby entities:", len(o.get("entities", [])))
    e.close()
    print("SMOKE OK")


def profile(seed=0, mode="survival", wtype="default", nsteps=100):
    import time
    e = NetheriteEnv()
    print(f"launching world seed={seed} mode={mode} type={wtype} ...")
    t0 = time.time()
    o = e.reset({"seed": seed, "mode": mode, "type": wtype})
    wall = time.time() - t0
    if not o.get("ok"):
        print("reset failed:", o)
        return
    st = e.stats()
    print(f"WORLD INIT: wall={wall:.1f}s  server_init_ms={st.get('init_ms', 0):.0f}")
    # step throughput (each step = 1 game tick, realtime-capped at 20/s)
    t0 = time.time()
    for _ in range(nsteps):
        e.step({"forward": 1})
    dt = time.time() - t0
    print(f"STEP THROUGHPUT: {nsteps} steps in {dt:.2f}s = {nsteps/dt:.1f} steps/s")
    st = e.stats()
    print(f"FPS (render): {st.get('fps')}")
    print(f"TICK: mean={st.get('tick_ms_mean', 0):.2f}ms max={st.get('tick_ms_max', 0):.2f}ms "
          f"of 50ms budget  -> headroom={st.get('tick_headroom_ms', 0):.2f}ms  "
          f"uncapped~{st.get('max_tps_uncapped', 0):.0f} TPS")
    print(f"entities loaded: {st.get('entities')}")
    e.close()
    print("PROFILE OK")


def load_test():
    """Measure per-tick compute as load is added (entities, fluids), then max uncapped TPS.

    Tick cost is measured at realtime (compute per tick is the same capped or not);
    overclock is only used briefly to report the uncapped TPS ceiling.
    """
    import time
    e = NetheriteEnv()
    print("launching world (creative, cheats) ...")
    e.reset({"seed": 0, "mode": "creative"})

    def tick_ms(label, settle=5.0):
        time.sleep(settle)  # let the 100-sample tick window refill under the new load
        st = e.stats()
        print(f"{label:12s} tick={st['tick_ms_mean']:5.2f}ms mean /{st['tick_ms_max']:6.2f}ms max  "
              f"entities={st['entities']}")
        return st["tick_ms_mean"]

    base = tick_ms("IDLE")
    e.spawn(count=60, radius=8)
    cows = tick_ms("+60 COWS")
    e.fluid("water", radius=8)
    water = tick_ms("+WATER")
    e.fluid("lava", radius=8)
    lava = tick_ms("+LAVA")
    print(f"DELTAS: entities(60 cows) +{cows-base:.2f}ms | water +{water-cows:.2f}ms | lava +{lava-water:.2f}ms")

    # uncapped TPS ceiling (brief; load present)
    print("overclock:", e.overclock(1))
    s1 = e.stats(); n1 = s1["num_ticks"]; t0 = time.time()
    time.sleep(3.0)
    s2 = e.stats(); tps = (s2["num_ticks"] - n1) / (time.time() - t0)
    print(f"UNCAPPED: {tps:.0f} TPS under load (tick {s2['tick_ms_mean']:.2f}ms)  vs 20 TPS realtime cap")
    e.overclock(50)
    e.close()
    print("LOAD TEST OK")


if __name__ == "__main__":
    arg = sys.argv[1] if len(sys.argv) > 1 else ""
    if arg == "smoke":
        sys.exit(smoke())
    if arg == "load":
        sys.exit(load_test())
    sys.exit(profile())
