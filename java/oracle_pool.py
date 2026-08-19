#!/usr/bin/env python3
"""oracle_pool.py - N isolated headless Java oracle clients on one box.

`start_vnc_client.sh` is a SINGLETON launcher: display :1, bridge port 25575,
game dir `java/Minecraft/run`, and a cleanup step that does
`pkill -f GradleStart` / `pkill -f "Xvfb :1"`. Every one of those is a global
name, so a second copy of it does not start a second oracle - it kills the
first one. This module generalises all four into per-instance resources:

    instance i  ->  display :(display_base+i)      own Xvfb, own X lock
                    port    (port_base+i)          own NetheriteMod bridge
                    <root>/instance_NN/run         own gameDir: options.txt,
                                                   qrl_launch.json, config/,
                                                   saves/, logs/
                    own process group              killpg, never pkill

The game dir is the load-bearing one. `netheritemod.Recorder.loadLaunchCfg`
resolves `qrl_launch.json` from the process cwd and nothing else, and gradle
runs `runClient` with cwd = runDir, so `-PrunDir=<abs>` (already in
`java/Minecraft/build.gradle`) hands each instance a private port, world seed,
strip/determinism block AND a private `saves/` tree. Same seed in N instances
is then not a world-lock collision, it is N byte-identical worlds - which is
what makes a cross-instance bit-identity check possible at all.

Process groups: the client is launched with `start_new_session=True`, so the
gradle wrapper JVM is a session/group leader and `os.killpg` reaches the
game JVM it forked. This is why `--no-daemon` is mandatory - with a gradle
daemon the game is a child of a long-lived daemon in ANOTHER process group and
the pgid kill silently misses it. A wedged (SIGSTOP-ed) instance also ignores
SIGTERM, so every kill sends SIGCONT first.

CLI:

    uv run --no-project python java/oracle_pool.py start -n 4
    uv run --no-project python java/oracle_pool.py status
    uv run --no-project python java/oracle_pool.py exec --all -- \\
        uv run --no-project python java/qrl_lockstep_gate.py --port {port}
    uv run --no-project python java/oracle_pool.py stop --all

Python:

    from oracle_pool import OraclePool
    with OraclePool(count=4) as pool:
        pool.start()
        results = pool.broadcast(lambda inst: my_scenario(inst.port))
"""
import argparse
import concurrent.futures
import json
import os
import queue
import shutil
import signal
import socket
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
MCDIR = os.path.join(HERE, "Minecraft")
TEMPLATE_RUN = os.path.join(MCDIR, "run")
DEFAULT_ROOT = os.path.join(HERE, "pool")

DISPLAY_BASE = 100
PORT_BASE = 25600
VNC_BASE = 5920
# 25575 belongs to the classic single-client flow (start_vnc_client.sh); the
# pool must never allocate it, whatever --port-base a caller passes.
RESERVED_PORTS = (25575,)
RESERVED_DISPLAYS = (0, 1)

READY_SETTLE = 5.0        # seconds of uninterrupted readiness before "in-world"
READY_TIMEOUT = 420.0     # per pool-boot wall budget
PROBE_TIMEOUT = 3.0       # per readiness/heartbeat probe


# --------------------------------------------------------------------------
# process helpers


def pid_state(pid):
    """Single-letter /proc state, or "" if the pid is gone."""
    try:
        with open("/proc/%d/stat" % pid) as f:
            stat = f.read()
        return stat[stat.rfind(")") + 2:].split()[0]
    except (OSError, IndexError):
        return ""


def pid_alive(pid):
    """Alive means RUNNABLE, not merely present in /proc.

    os.kill(pid, 0) succeeds on a zombie, and everything the pool launches is
    a Popen child nobody wait()s, so a killed client stays visible forever to
    its launcher. Both readiness ("the client crashed, fail now") and shutdown
    ("zero strays") get the wrong answer from that.
    """
    return bool(pid) and pid_state(pid) not in ("", "Z")


def pid_cmdline(pid):
    try:
        with open("/proc/%d/cmdline" % pid, "rb") as f:
            return f.read().replace(b"\0", b" ").decode("utf-8", "replace")
    except OSError:
        return ""


def pid_cwd(pid):
    try:
        return os.readlink("/proc/%d/cwd" % pid)
    except OSError:
        return ""


def pid_rss_bytes(pid):
    """Resident set of one pid, from /proc/<pid>/statm (pages)."""
    try:
        with open("/proc/%d/statm" % pid) as f:
            return int(f.read().split()[1]) * os.sysconf("SC_PAGE_SIZE")
    except (OSError, ValueError, IndexError):
        return 0


def pid_cpu_seconds(pid):
    try:
        with open("/proc/%d/stat" % pid) as f:
            stat = f.read()
        fields = stat[stat.rfind(")") + 2:].split()
        ticks = int(fields[11]) + int(fields[12])
        return ticks / os.sysconf("SC_CLK_TCK")
    except (OSError, ValueError, IndexError):
        return 0.0


def pids_in_group(pgid):
    """Every live pid whose process group is pgid (procfs scan, no pkill).

    Zombies are excluded on purpose. Nothing in a CLI-shaped pool ever wait()s
    the launcher, so a client whose gradle wrapper exited leaves a permanent
    zombie in /proc carrying the same pgid; counting it made a whole pool of
    CRASHED launches look alive, and the boot sat in the readiness loop for the
    full 420 s timeout instead of failing in 15 s with the gradle error.
    """
    out = []
    for name in os.listdir("/proc"):
        if not name.isdigit():
            continue
        try:
            with open("/proc/%s/stat" % name) as f:
                stat = f.read()
            fields = stat[stat.rfind(")") + 2:].split()
            if fields[0] != "Z" and int(fields[2]) == pgid:
                out.append(int(name))
        except (OSError, ValueError, IndexError):
            continue
    return out


def kill_group(pgid, owned, term_wait=20.0):
    """Kill a whole process group: SIGCONT, SIGTERM, then SIGKILL.

    Ownership is decided by `owned(pid) -> bool` over the LIVE MEMBERS, never
    by the leader alone: `reap_launcher` deliberately kills the gradle wrapper
    (the group leader) while the game JVM keeps running in the same group, and
    a pgid read back from disk can also have been recycled by an unrelated
    process between runs. No member we recognise, no signal - killing a
    recycled pgid is how a pool "cleanup" takes out a bystander.
    """
    if not pgid:
        return "gone"
    members = pids_in_group(pgid)
    if not members:
        return "gone"
    if not any(owned(pid) for pid in members):
        return "stale"
    for sig in (signal.SIGCONT, signal.SIGTERM):
        # SIGCONT first: a SIGSTOP-ed JVM never runs its SIGTERM handler, so a
        # wedged instance would otherwise survive the polite kill and only die
        # on the SIGKILL fallback - after the full term_wait.
        try:
            os.killpg(pgid, sig)
        except OSError:
            pass
    deadline = time.monotonic() + term_wait
    while time.monotonic() < deadline:
        reap_child(pgid)
        if not pids_in_group(pgid):
            return "term"
        time.sleep(0.2)
    try:
        os.killpg(pgid, signal.SIGKILL)
    except OSError:
        pass
    deadline = time.monotonic() + 10.0
    while time.monotonic() < deadline:
        reap_child(pgid)
        if not pids_in_group(pgid):
            return "kill"
        time.sleep(0.2)
    return "survived"


def reap_child(pid):
    """Best-effort wait() so a killed child does not linger as a zombie."""
    try:
        os.waitpid(pid, os.WNOHANG)
    except (ChildProcessError, OSError):
        pass


def kill_pid(pid, guard, term_wait=10.0):
    if not pid_alive(pid):
        return "gone"
    if guard is not None and guard not in pid_cmdline(pid):
        return "stale"
    for sig in (signal.SIGCONT, signal.SIGTERM):
        try:
            os.kill(pid, sig)
        except OSError:
            pass
    deadline = time.monotonic() + term_wait
    while time.monotonic() < deadline:
        reap_child(pid)
        if not pid_alive(pid):
            return "term"
        time.sleep(0.2)
    try:
        os.kill(pid, signal.SIGKILL)
    except OSError:
        pass
    for _ in range(50):
        reap_child(pid)
        if not pid_alive(pid):
            break
        time.sleep(0.1)
    return "kill"


def port_listening(port, host="127.0.0.1", timeout=1.0):
    try:
        with socket.create_connection((host, port), timeout=timeout):
            return True
    except OSError:
        return False


def probe_in_world(port, host="127.0.0.1", timeout=PROBE_TIMEOUT):
    """True only when the bridge answers `obs` with a live in-world player.

    A listening socket is NOT readiness: NetheriteMod binds the bridge during
    mod init, long before the integrated server has a world, and a SIGSTOP-ed
    JVM keeps its listen backlog so the connect still succeeds. Both cases are
    rejected here because the response never arrives / never says ok.
    """
    try:
        with socket.create_connection((host, port), timeout=timeout) as s:
            s.settimeout(timeout)
            s.sendall(b'{"cmd":"obs","action":{}}\n')
            buf = b""
            while b"\n" not in buf:
                chunk = s.recv(65536)
                if not chunk:
                    return False
                buf += chunk
            o = json.loads(buf.split(b"\n", 1)[0].decode())
            return o.get("ok") is True and "x" in o
    except (OSError, ValueError):
        return False


# --------------------------------------------------------------------------
# instance


class Instance:
    """One isolated oracle: its own display, port, game dir and process group."""

    def __init__(self, pool, idx):
        self.pool = pool
        self.idx = idx
        self.display_num = pool.display_base + idx
        self.display = ":%d" % self.display_num
        self.port = pool.port_base + idx
        self.vnc_port = pool.vnc_base + idx
        self.dir = os.path.join(pool.root, "instance_%02d" % idx)
        self.run_dir = os.path.join(self.dir, "run")
        self.project_cache = os.path.join(self.dir, "gradle-project")
        self.state_path = os.path.join(self.dir, "instance.json")
        self.xvfb_log = os.path.join(self.dir, "xvfb.log")
        self.client_log = os.path.join(self.dir, "runclient.log")
        self.vnc_log = os.path.join(self.dir, "x11vnc.log")
        self.pgid = 0
        self.xvfb_pid = 0
        self.vnc_pid = 0
        self.boots = 0
        self.started_at = 0.0
        self.ready_at = 0.0

    # -- persisted registry (so a later CLI call can find/stop us) ----------

    def owns(self, pid):
        """Is `pid` part of THIS instance?

        cwd is the primary test: gradle runs `runClient` with cwd = runDir and
        the game JVM inherits it, so the game is identifiable even after the
        gradle wrapper (which carries -PrunDir on its command line) has been
        reaped. Both tests are per-instance strings - never a pattern like
        "GradleStart" or "Xvfb", which is what makes start_vnc_client.sh's
        pkill cleanup unusable in a pool.
        """
        return pid_cwd(pid) == self.run_dir or self.guard in pid_cmdline(pid)

    @property
    def guard(self):
        """Substring that identifies THIS instance's launcher in a cmdline."""
        return "-PrunDir=%s" % self.run_dir

    def save_state(self):
        os.makedirs(self.dir, exist_ok=True)
        with open(self.state_path, "w") as f:
            json.dump({
                "idx": self.idx, "port": self.port, "display": self.display,
                "pgid": self.pgid, "xvfb_pid": self.xvfb_pid,
                "vnc_pid": self.vnc_pid, "run_dir": self.run_dir,
                "boots": self.boots, "started_at": self.started_at,
                "ready_at": self.ready_at,
            }, f, indent=2, sort_keys=True)

    def load_state(self):
        try:
            with open(self.state_path) as f:
                s = json.load(f)
        except (OSError, ValueError):
            return False
        self.pgid = int(s.get("pgid") or 0)
        self.xvfb_pid = int(s.get("xvfb_pid") or 0)
        self.vnc_pid = int(s.get("vnc_pid") or 0)
        self.boots = int(s.get("boots") or 0)
        self.started_at = float(s.get("started_at") or 0.0)
        self.ready_at = float(s.get("ready_at") or 0.0)
        return True

    # -- provisioning ------------------------------------------------------

    def provision(self):
        """Build this instance's private game dir. Returns bytes copied."""
        os.makedirs(self.run_dir, exist_ok=True)
        os.makedirs(self.project_cache, exist_ok=True)
        copied = 0

        src_opts = os.path.join(TEMPLATE_RUN, "options.txt")
        if os.path.exists(src_opts):
            dst_opts = os.path.join(self.run_dir, "options.txt")
            with open(src_opts) as f:
                lines = f.read().splitlines()
            # maxFps is the pool's single biggest CPU knob: under llvmpipe the
            # client renders as fast as it is allowed to, and the trace profile
            # asks for 260. See OraclePool.max_fps.
            if self.pool.max_fps:
                lines = [("maxFps:%d" % self.pool.max_fps)
                         if ln.split(":", 1)[0] == "maxFps" else ln
                         for ln in lines]
            with open(dst_opts, "w") as f:
                f.write("\n".join(lines) + "\n")
            copied += os.path.getsize(dst_opts)
        src_cfg = os.path.join(TEMPLATE_RUN, "config")
        dst_cfg = os.path.join(self.run_dir, "config")
        if os.path.isdir(src_cfg) and not os.path.isdir(dst_cfg):
            shutil.copytree(src_cfg, dst_cfg)
            copied += dir_bytes(dst_cfg)

        cfg = json.loads(json.dumps(self.pool.launch_cfg))
        cfg["port"] = self.port
        cfg["profile"] = "oracle-pool"
        with open(os.path.join(self.run_dir, "qrl_launch.json"), "w") as f:
            json.dump(cfg, f, indent=2)
            f.write("\n")

        if self.pool.world_template:
            dst = os.path.join(self.run_dir, "saves", self.pool.world_folder)
            if not os.path.isdir(dst):
                os.makedirs(os.path.dirname(dst), exist_ok=True)
                shutil.copytree(self.pool.world_template, dst)
                copied += dir_bytes(dst)
        return copied

    def wipe_world(self):
        saves = os.path.join(self.run_dir, "saves")
        if os.path.isdir(saves):
            shutil.rmtree(saves)

    # -- lifecycle ---------------------------------------------------------

    def display_pids(self):
        """Live `Xvfb :<n>` servers on THIS instance's display number.

        The display number is allocated exclusively by the pool, so matching on
        it is per-instance - unlike start_vnc_client.sh's `pkill -f "Xvfb :1"`,
        which would also match :10 and :100.
        """
        out = []
        for name in os.listdir("/proc"):
            if not name.isdigit():
                continue
            argv = pid_cmdline(int(name)).split()
            if len(argv) >= 2 and os.path.basename(argv[0]) == "Xvfb" \
                    and argv[1] == self.display:
                out.append(int(name))
        return out

    def start_display(self):
        # An Xvfb left behind by a crashed run keeps serving this display, and
        # a fresh Xvfb on a taken display exits immediately with "Server is
        # already active". Before the fix below, start_display saw xdpyinfo
        # succeed (against the STALE server), returned happily, and recorded a
        # pid that was already dead - so stop() reported "gone" and leaked the
        # real Xvfb. Claim the display first, then verify OUR server is the one
        # answering.
        for pid in self.display_pids():
            kill_pid(pid, guard=self.display + " ")
        lock = "/tmp/.X%d-lock" % self.display_num
        if os.path.exists(lock) and not display_owner_alive(self.display_num):
            try:
                os.unlink(lock)
            except OSError:
                pass
        cmd = ["Xvfb", self.display, "-screen", "0", self.pool.geometry,
               "+extension", "GLX", "+render", "-noreset"]
        with open(self.xvfb_log, "wb") as log:
            p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL, start_new_session=True)
        self.xvfb_pid = p.pid
        deadline = time.monotonic() + 20.0
        env = dict(os.environ, DISPLAY=self.display)
        while time.monotonic() < deadline:
            if not pid_alive(self.xvfb_pid):
                raise RuntimeError("Xvfb %s exited during startup (%s)"
                                   % (self.display, self.xvfb_log))
            if subprocess.run(["xdpyinfo"], env=env, stdout=subprocess.DEVNULL,
                              stderr=subprocess.DEVNULL, check=False).returncode == 0:
                return
            time.sleep(0.1)
        raise RuntimeError("Xvfb %s never became ready" % self.display)

    def start_vnc(self):
        cmd = ["x11vnc", "-display", self.display, "-nopw", "-localhost",
               "-forever", "-shared", "-rfbport", str(self.vnc_port), "-noxdamage"]
        with open(self.vnc_log, "wb") as log:
            p = subprocess.Popen(cmd, stdout=log, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL, start_new_session=True)
        self.vnc_pid = p.pid

    def client_env(self):
        env = dict(os.environ)
        env["JAVA_HOME"] = self.pool.java_home
        env["PATH"] = os.path.join(self.pool.java_home, "bin") + ":" + env.get("PATH", "")
        env["DISPLAY"] = self.display
        env["LIBGL_ALWAYS_SOFTWARE"] = "1"
        env["MESA_GL_VERSION_OVERRIDE"] = "2.1"
        # llvmpipe defaults its rasteriser pool to one thread per host CPU, so
        # a single client happily eats the whole box. In a pool the budget is
        # per instance, not per host. See OraclePool.lp_threads.
        if self.pool.lp_threads:
            env["LP_NUM_THREADS"] = str(self.pool.lp_threads)
        # Fresh openjdk-8 enables the GNOME Atk bridge, which throws AWTError
        # under Xvfb and aborts GradleStart before the bridge binds.
        env["JAVA_TOOL_OPTIONS"] = (env.get("JAVA_TOOL_OPTIONS", "")
                                    + " -Djavax.accessibility.assistive_technologies=").strip()
        return env

    def start_client_gradle(self):
        """Launch through ./gradlew runClient. NEVER run two of these at once.

        Measured on gamer 2026-08-08: four concurrent runClient invocations on
        one checkout, both cache layouts, both a hard failure inside 15 s.
          * shared --project-cache-dir: `:deobfMcMCP` NPE in one instance,
            `Could not resolve net.minecraftforge:forgeBin:...-PROJECT(Minecraft)`
            in the other three - they race on the deobf jar the project cache
            holds.
          * per-instance --project-cache-dir: the `jaxb` task deletes and
            regenerates `src/main/java/com/microsoft/Malmo/Schemas/*.java`, so
            the builds delete each other's SOURCES (NoSuchFileException on
            .../Schemas/DrawCuboid.java).
        The second is not fixable by cache layout - it is a shared source tree -
        which is why the pool captures ONE gradle launch and spawns the rest
        directly. Callers hold pool.gradle_lock.
        """
        cmd = ["./gradlew", "-g", self.pool.gradle_home,
               "--offline", "--no-daemon", "-x", "getAssets",
               "--project-cache-dir", (self.project_cache
                                       if self.pool.per_instance_project_cache
                                       else self.pool.gradle_project_cache),
               "-PrunDir=%s" % self.run_dir,
               "-PmcUsername=%s" % self.pool.username,
               "runClient", "--stacktrace"]
        with open(self.client_log, "wb") as log:
            p = subprocess.Popen(cmd, cwd=MCDIR, env=self.client_env(),
                                 stdout=log, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL, start_new_session=True)
        # start_new_session makes the child a session AND process-group leader,
        # so its pid IS the pgid every descendant inherits.
        self.pgid = p.pid
        self.started_at = time.monotonic()
        self.ready_at = 0.0

    def start_client_direct(self, argv):
        """Launch the game JVM straight from a captured argv.

        Everything gradle contributes is baked into `argv` (jvm args, the full
        classpath, GradleStart, --username). The only thing that differs per
        instance is the working directory - and that IS the isolation boundary,
        because Recorder.loadLaunchCfg reads qrl_launch.json from cwd, so
        cwd=runDir hands this instance its port, its seed and its saves/.
        """
        with open(self.client_log, "wb") as log:
            p = subprocess.Popen(argv, cwd=self.run_dir, env=self.client_env(),
                                 stdout=log, stderr=subprocess.STDOUT,
                                 stdin=subprocess.DEVNULL, start_new_session=True)
        self.pgid = p.pid
        self.started_at = time.monotonic()
        self.ready_at = 0.0

    def game_pid(self):
        """This instance's GradleStart JVM, once gradle has forked it."""
        for pid in pids_in_group(self.pgid):
            if pid_cwd(pid) == self.run_dir and "GradleStart" in pid_cmdline(pid):
                return pid
        return 0

    def start(self, argv=None):
        self.stop(quiet=True)
        self.provision()
        self.start_display()
        if self.pool.vnc:
            self.start_vnc()
        if argv:
            self.start_client_direct(argv)
        else:
            self.start_client_gradle()
        self.boots += 1
        self.save_state()

    def reap_launcher(self):
        """Drop the gradle wrapper JVM once the game is up.

        With --no-daemon the wrapper runs the whole build in-process and then
        sits blocked on the JavaExec child for the client's entire lifetime,
        holding ~2.3 GiB it will never use again - on this box that is more
        than half of one instance's footprint. The game JVM is a plain fork,
        keeps the same process group, and its stdout is a PrintStream (which
        swallows the EPIPE), so it survives the wrapper's death; the pgid kill
        still reaches it because ownership is decided over group MEMBERS.
        """
        if not self.pgid or not pid_alive(self.pgid):
            return False
        if self.guard not in pid_cmdline(self.pgid):
            return False
        if not any(pid != self.pgid and self.owns(pid)
                   for pid in pids_in_group(self.pgid)):
            return False  # no game JVM yet - reaping now would kill the launch
        try:
            os.kill(self.pgid, signal.SIGKILL)
        except OSError:
            return False
        return True

    def stop(self, quiet=False):
        """Tear this instance down. Never touches another instance."""
        res = {}
        res["client"] = kill_group(self.pgid, owned=self.owns)
        # Belt and braces: anything that escaped the group but still belongs to
        # this instance (never a pattern match on "GradleStart", which is what
        # makes the singleton launcher's pkill unusable in a pool).
        for pid in self.stragglers():
            kill_pid(pid, guard=None)
        if self.vnc_pid:
            res["x11vnc"] = kill_pid(self.vnc_pid, guard="-rfbport %d" % self.vnc_port)
        # By recorded pid, then by display number: a crashed run can leave an
        # Xvfb whose pid nobody wrote down, and "zero strays" has to mean zero.
        res["xvfb"] = kill_pid(self.xvfb_pid, guard="Xvfb %s " % self.display)
        for pid in self.display_pids():
            res["xvfb"] = kill_pid(pid, guard=self.display + " ")
        self.pgid = self.xvfb_pid = self.vnc_pid = 0
        if os.path.isdir(self.dir):
            self.save_state()
        if not quiet:
            print("instance=%d stopped %s" % (self.idx, res), flush=True)
        return res

    # -- health ------------------------------------------------------------

    def stragglers(self):
        """Live pids that belong to this instance, wherever they ended up."""
        out = []
        for name in os.listdir("/proc"):
            if not name.isdigit():
                continue
            pid = int(name)
            if self.owns(pid):
                out.append(pid)
        return out

    def client_alive(self):
        return bool(self.pgid) and bool(pids_in_group(self.pgid))

    def ready(self):
        return probe_in_world(self.port, self.pool.host)

    def rss_bytes(self):
        return sum(pid_rss_bytes(p) for p in pids_in_group(self.pgid))

    def cpu_seconds(self):
        return sum(pid_cpu_seconds(p) for p in pids_in_group(self.pgid))

    def log_tail(self, n=40):
        try:
            with open(self.client_log, errors="replace") as f:
                return "\n".join(f.read().splitlines()[-n:])
        except OSError:
            return "(no runclient.log)"


def dir_bytes(path):
    total = 0
    for root, _dirs, files in os.walk(path):
        for name in files:
            try:
                total += os.path.getsize(os.path.join(root, name))
            except OSError:
                pass
    return total


def display_owner_alive(num):
    sock = "/tmp/.X11-unix/X%d" % num
    if not os.path.exists(sock):
        return False
    return subprocess.run(["xdpyinfo"], env=dict(os.environ, DISPLAY=":%d" % num),
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                          check=False).returncode == 0


# --------------------------------------------------------------------------
# pool


class PoolError(RuntimeError):
    pass


class OraclePool:
    def __init__(self, count, root=DEFAULT_ROOT, port_base=PORT_BASE,
                 display_base=DISPLAY_BASE, vnc_base=VNC_BASE, seed=917351,
                 world_type="flat", world_template=None, vnc=False,
                 geometry="854x480x24", username="Player0", host="127.0.0.1",
                 java_home=None, gradle_home=None, max_fps=20, lp_threads=1,
                 reap_launcher=True, launch_mode="auto",
                 per_instance_project_cache=True, launch_cfg=None):
        if count < 1:
            raise PoolError("count must be >= 1")
        self.count = count
        self.root = os.path.abspath(root)
        self.port_base = port_base
        self.display_base = display_base
        self.vnc_base = vnc_base
        self.vnc = vnc
        self.geometry = geometry
        self.username = username
        self.host = host
        self.per_instance_project_cache = per_instance_project_cache
        # Two knobs that decide how many clients fit on a box, both measured
        # (see docs/DEVLOG.md, PR5 wave-4). They trade RENDER fidelity for
        # density and change no server-side state, so they are right for
        # state/bridge gate work and WRONG for pixel-golden capture - a pool
        # that records goldens must run the pinned trace profile
        # (max_fps=None, lp_threads=None) and is correspondingly smaller.
        self.max_fps = max_fps
        self.lp_threads = lp_threads
        self.reap_launcher = reap_launcher
        if launch_mode not in ("auto", "gradle"):
            raise PoolError("launch_mode must be auto or gradle")
        self.launch_mode = launch_mode
        self.gradle_project_cache = os.path.join(self.root, "gradle-project")
        self.java_home = java_home or os.environ.get(
            "JAVA_HOME", "/usr/lib/jvm/java-8-openjdk-amd64")
        self.gradle_home = gradle_home or os.path.join(TEMPLATE_RUN, "gradle")

        for i in range(count):
            if port_base + i in RESERVED_PORTS:
                raise PoolError(
                    "port %d is reserved for the single-client flow "
                    "(start_vnc_client.sh); move --port-base"
                    % (port_base + i))
            if display_base + i in RESERVED_DISPLAYS:
                raise PoolError("display :%d is reserved" % (display_base + i))

        self.launch_cfg = launch_cfg or self._template_cfg(seed, world_type)
        world = self.launch_cfg.get("world", {})
        self.world_folder = "qrl_%d%s" % (
            int(world.get("seed", 0)),
            "_flat" if str(world.get("type", "")).lower() == "flat" else "")
        self.world_template = os.path.abspath(world_template) if world_template else None
        if self.world_template and not os.path.isdir(self.world_template):
            raise PoolError("world template not found: %s" % self.world_template)
        if self.world_template and os.path.basename(self.world_template) != self.world_folder:
            raise PoolError(
                "world template dir must be named %s (NetheriteMod derives the "
                "folder from seed/type), got %s"
                % (self.world_folder, os.path.basename(self.world_template)))

        self.instances = [Instance(self, i) for i in range(count)]

    @staticmethod
    def _template_cfg(seed, world_type):
        src = os.path.join(TEMPLATE_RUN, "qrl_launch.json")
        with open(src) as f:
            cfg = json.load(f)
        cfg.setdefault("world", {})
        cfg["world"]["seed"] = int(seed)
        cfg["world"]["type"] = world_type
        return cfg

    # -- context manager ---------------------------------------------------

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.stop_all()
        return False

    # -- launch spec (gradle capture) --------------------------------------

    @property
    def spec_path(self):
        return os.path.join(self.root, "launch_spec.json")

    def mod_jar_mtime(self):
        libs = os.path.join(MCDIR, "build", "libs")
        newest = 0.0
        for name in os.listdir(libs) if os.path.isdir(libs) else []:
            if name.endswith(".jar"):
                newest = max(newest, os.path.getmtime(os.path.join(libs, name)))
        return newest

    def load_spec(self):
        """A cached game argv, if it is still newer than the built mod."""
        try:
            with open(self.spec_path) as f:
                spec = json.load(f)
        except (OSError, ValueError):
            return None
        if spec.get("mod_jar_mtime", 0) < self.mod_jar_mtime():
            return None
        argv = spec.get("argv") or []
        if len(argv) < 3 or not os.path.exists(argv[0]):
            return None
        return argv

    def capture_spec(self, inst, timeout=180.0, verbose=True):
        """Read the game's argv out of /proc while gradle is still holding it.

        ForgeGradle has no "print the launch command" task, so the honest way
        to learn the command is to watch gradle issue it once. The captured
        argv is then verified the only way that counts - every other instance
        boots from it and has to reach in-world readiness.
        """
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            pid = inst.game_pid()
            if pid:
                try:
                    with open("/proc/%d/cmdline" % pid, "rb") as f:
                        argv = f.read().decode("utf-8", "replace").split("\0")
                except OSError:
                    argv = []
                argv = [a for a in argv if a]
                if len(argv) >= 3:
                    with open(self.spec_path, "w") as f:
                        json.dump({"argv": argv, "captured_at": time.time(),
                                   "captured_from": inst.run_dir,
                                   "mod_jar_mtime": self.mod_jar_mtime()},
                                  f, indent=2)
                    if verbose:
                        print("captured launch spec from instance=%d (%d argv)"
                              % (inst.idx, len(argv)), flush=True)
                    return argv
            if not inst.client_alive():
                raise PoolError("instance %d exited before the game JVM "
                                "appeared:\n%s" % (inst.idx, inst.log_tail()))
            time.sleep(0.25)
        raise PoolError("no GradleStart JVM for instance %d within %.0fs"
                        % (inst.idx, timeout))

    # -- boot --------------------------------------------------------------

    def start(self, wait=True, stagger=0.0, settle=READY_SETTLE,
              timeout=READY_TIMEOUT, verbose=True):
        os.makedirs(self.root, exist_ok=True)
        t0 = time.monotonic()
        if self.launch_mode == "gradle":
            # Correct but slow: gradle's build phase is not concurrency-safe on
            # one checkout, so each launch waits for the previous one's game JVM
            # to appear - the point at which gradle is done touching src/ and
            # build/. N serial gradle phases instead of one.
            for inst in self.instances:
                inst.start()
                if verbose:
                    print("instance=%d starting (gradle, serialized) port=%d"
                          % (inst.idx, inst.port), flush=True)
                self.capture_spec(inst, verbose=False)
        else:
            argv = self.load_spec()
            pending = list(self.instances)
            if argv is None:
                # One serialized gradle launch pays for the whole pool: it
                # builds whatever is stale and hands us the game's argv.
                head = pending.pop(0)
                head.start()
                if verbose:
                    print("instance=%d starting (gradle capture) display=%s "
                          "port=%d" % (head.idx, head.display, head.port),
                          flush=True)
                argv = self.capture_spec(head, verbose=verbose)
            for inst in pending:
                inst.start(argv=argv)
                if verbose:
                    print("instance=%d starting display=%s port=%d run=%s"
                          % (inst.idx, inst.display, inst.port, inst.run_dir),
                          flush=True)
                if stagger:
                    time.sleep(stagger)
        if not wait:
            return {}
        ready = self.wait_ready(self.instances, settle=settle, timeout=timeout,
                                verbose=verbose)
        if verbose:
            print("pool: %d/%d ready in %.1fs"
                  % (len(ready), self.count, time.monotonic() - t0), flush=True)
        return ready

    def wait_ready(self, instances, settle=READY_SETTLE, timeout=READY_TIMEOUT,
                   verbose=True):
        """Block until every instance answers `obs` for `settle` seconds."""
        pending = {inst.idx: inst for inst in instances}
        first_ok = {}
        ready = {}
        deadline = time.monotonic() + timeout
        while pending:
            for idx in sorted(pending):
                inst = pending[idx]
                if inst.ready():
                    t = first_ok.setdefault(idx, time.monotonic())
                    if time.monotonic() - t < settle:
                        continue
                    inst.ready_at = time.monotonic()
                    inst.save_state()
                    ready[idx] = inst.ready_at - inst.started_at
                    if self.reap_launcher:
                        inst.reap_launcher()
                    if verbose:
                        print("instance=%d ready port=%d after %.1fs"
                              % (idx, inst.port, ready[idx]), flush=True)
                    del pending[idx]
                    continue
                first_ok.pop(idx, None)
                if not inst.client_alive():
                    raise PoolError("instance %d exited during startup:\n%s"
                                    % (idx, inst.log_tail()))
            if not pending:
                break
            if time.monotonic() > deadline:
                raise PoolError("instances not ready within %.0fs: %s"
                                % (timeout, sorted(pending)))
            time.sleep(0.5)
        return ready

    # -- health / repair ---------------------------------------------------

    def wedged(self, grace=0.0):
        """Instances that are up but not answering.

        A dead client and a SIGSTOP-ed client are both 'wedged' here: the
        distinction that matters to the pool is 'cannot serve work', and the
        repair (kill the group, re-provision, re-boot) is the same for both.
        """
        bad = []
        for inst in self.instances:
            if inst.ready_at == 0.0:
                continue
            if inst.ready():
                continue
            if grace and time.monotonic() - inst.ready_at < grace:
                continue
            bad.append(inst)
        return bad

    def replace(self, inst, settle=READY_SETTLE, timeout=READY_TIMEOUT,
                verbose=True):
        """Reap and re-boot ONE instance. Siblings are never signalled."""
        if verbose:
            print("instance=%d wedged -> reaping (boot %d)"
                  % (inst.idx, inst.boots), flush=True)
        inst.stop(quiet=not verbose)
        inst.wipe_world()
        # A replacement re-uses the captured argv, so reaping one wedged
        # instance never re-enters gradle and therefore cannot disturb the
        # siblings through the shared source tree.
        inst.start(argv=None if self.launch_mode == "gradle" else self.load_spec())
        got = self.wait_ready([inst], settle=settle, timeout=timeout,
                              verbose=verbose)
        if verbose:
            print("instance=%d replaced, ready in %.1fs"
                  % (inst.idx, got[inst.idx]), flush=True)
        return got[inst.idx]

    def reap_wedged(self, grace=0.0, verbose=True):
        replaced = []
        for inst in self.wedged(grace=grace):
            self.replace(inst, verbose=verbose)
            replaced.append(inst.idx)
        return replaced

    # -- dispatch ----------------------------------------------------------

    def broadcast(self, fn, timeout=None):
        """Run fn(instance) on EVERY instance concurrently -> {idx: result}.

        Exceptions are returned, not raised: one wedged instance must not lose
        the other N-1 results.
        """
        out = {}
        with concurrent.futures.ThreadPoolExecutor(max_workers=self.count) as ex:
            futs = {ex.submit(fn, inst): inst.idx for inst in self.instances}
            for fut in concurrent.futures.as_completed(futs, timeout=timeout):
                idx = futs[fut]
                try:
                    out[idx] = fut.result()
                except Exception as exc:  # noqa: BLE001 - reported per instance
                    out[idx] = exc
        return out

    def map(self, fn, jobs, timeout=None):
        """Dispatch a job list over the pool; fn(instance, job) -> result.

        Free instances pull the next job, so a slow case does not stall the
        pool. Results come back in job order.
        """
        free = queue.Queue()
        for inst in self.instances:
            free.put(inst)
        results = [None] * len(jobs)

        def run(job):
            inst = free.get()
            try:
                return fn(inst, job)
            finally:
                free.put(inst)

        with concurrent.futures.ThreadPoolExecutor(max_workers=self.count) as ex:
            futs = {ex.submit(run, job): i for i, job in enumerate(jobs)}
            for fut in concurrent.futures.as_completed(futs, timeout=timeout):
                i = futs[fut]
                try:
                    results[i] = fut.result()
                except Exception as exc:  # noqa: BLE001 - reported per job
                    results[i] = exc
        return results

    def run_script(self, inst, argv, timeout=600, cwd=None):
        """Run a command against one instance. {port}/{display}/{run}/{idx}."""
        cmd = [a.format(port=inst.port, display=inst.display,
                        run=inst.run_dir, idx=inst.idx) for a in argv]
        env = dict(os.environ, DISPLAY=inst.display, QRL_PORT=str(inst.port))
        p = subprocess.run(cmd, cwd=cwd or os.path.dirname(HERE), env=env,
                           stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout, check=False)
        return {"idx": inst.idx, "port": inst.port, "rc": p.returncode,
                "out": p.stdout.decode("utf-8", "replace")}

    # -- shutdown ----------------------------------------------------------

    def stop_all(self, verbose=True):
        for inst in self.instances:
            inst.load_state()
            inst.stop(quiet=not verbose)

    def attach(self):
        """Re-hydrate pgids from disk (for a CLI call in a new process)."""
        for inst in self.instances:
            inst.load_state()
        return self


# --------------------------------------------------------------------------
# CLI


def build_pool(args):
    return OraclePool(
        count=args.instances, root=args.root, port_base=args.port_base,
        display_base=args.display_base, seed=args.seed,
        world_type=args.world_type, world_template=args.world_template,
        vnc=getattr(args, "vnc", False),
        max_fps=args.max_fps or None, lp_threads=args.lp_threads or None,
        reap_launcher=not getattr(args, "keep_launcher", False),
        launch_mode=getattr(args, "launch_mode", "auto"),
        per_instance_project_cache=not getattr(args, "shared_project_cache", False),
    )


def cmd_start(args):
    pool = build_pool(args)
    t0 = time.monotonic()
    ready = pool.start(wait=not args.no_wait, stagger=args.stagger,
                       settle=args.settle)
    if args.no_wait:
        return 0
    print("BOOT %d instances in %.1fs (per-instance: %s)"
          % (len(ready), time.monotonic() - t0,
             ", ".join("%d=%.1fs" % (k, v) for k, v in sorted(ready.items()))))
    for inst in pool.instances:
        print("  instance=%d port=%d display=%s rss=%.0f MiB"
              % (inst.idx, inst.port, inst.display, inst.rss_bytes() / 1048576.0))
    return 0


def cmd_status(args):
    pool = build_pool(args).attach()
    rc = 0
    for inst in pool.instances:
        alive = inst.client_alive()
        ready = inst.ready() if alive else False
        state = "ready" if ready else ("starting" if alive else "stopped")
        if state != "ready":
            rc = 1
        print("instance=%d state=%-8s display=%s port=%d pgid=%s procs=%d "
              "rss=%.0f MiB cpu=%.1fs"
              % (inst.idx, state, inst.display, inst.port, inst.pgid or "-",
                 len(pids_in_group(inst.pgid)), inst.rss_bytes() / 1048576.0,
                 inst.cpu_seconds()))
    return rc


def cmd_stop(args):
    pool = build_pool(args).attach()
    pool.stop_all()
    return 0


def cmd_exec(args):
    pool = build_pool(args).attach()
    if not args.argv:
        print("nothing to run: pass the command after --", file=sys.stderr)
        return 2
    res = pool.broadcast(lambda inst: pool.run_script(inst, args.argv,
                                                      timeout=args.timeout))
    rc = 0
    for idx in sorted(res):
        r = res[idx]
        if isinstance(r, Exception):
            print("instance=%d EXC %s" % (idx, r))
            rc = 1
            continue
        print("=== instance=%d port=%d rc=%d ===" % (idx, r["port"], r["rc"]))
        print(r["out"].rstrip())
        rc = rc or r["rc"]
    return rc


def cmd_provision(args):
    """Measure the per-instance game-dir cost without launching anything."""
    pool = build_pool(args)
    os.makedirs(pool.root, exist_ok=True)
    for inst in pool.instances:
        t0 = time.monotonic()
        n = inst.provision()
        print("instance=%d provisioned %.1f MiB in %.2fs -> %s"
              % (inst.idx, n / 1048576.0, time.monotonic() - t0, inst.run_dir))
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("-n", "--instances", type=int, default=4)
    ap.add_argument("--root", default=DEFAULT_ROOT)
    ap.add_argument("--port-base", type=int, default=PORT_BASE)
    ap.add_argument("--display-base", type=int, default=DISPLAY_BASE)
    ap.add_argument("--seed", type=int, default=917351)
    ap.add_argument("--world-type", default="flat", choices=("flat", "default"))
    ap.add_argument("--world-template", default=None,
                    help="save dir to copy into every instance (skips worldgen)")
    ap.add_argument("--max-fps", type=int, default=20,
                    help="client frame cap (0 = keep the trace profile's 260)")
    ap.add_argument("--lp-threads", type=int, default=1,
                    help="llvmpipe rasteriser threads (0 = mesa default = ncpu)")
    sub = ap.add_subparsers(dest="cmd", required=True)

    s = sub.add_parser("start")
    s.add_argument("--vnc", action="store_true")
    s.add_argument("--no-wait", action="store_true")
    s.add_argument("--stagger", type=float, default=0.0)
    s.add_argument("--settle", type=float, default=READY_SETTLE)
    s.add_argument("--shared-project-cache", action="store_true",
                   help="share Minecraft/.gradle across instances (contends)")
    s.add_argument("--keep-launcher", action="store_true",
                   help="do not reap the gradle wrapper JVM once ready")
    s.add_argument("--launch-mode", default="auto", choices=("auto", "gradle"),
                   help="auto: one gradle launch, rest direct from its argv; "
                        "gradle: every instance through gradle, serialized")
    s.set_defaults(func=cmd_start)

    s = sub.add_parser("status")
    s.set_defaults(func=cmd_status)

    s = sub.add_parser("stop")
    s.set_defaults(func=cmd_stop)

    s = sub.add_parser("provision")
    s.set_defaults(func=cmd_provision)

    s = sub.add_parser("exec")
    s.add_argument("--all", action="store_true", default=True)
    s.add_argument("--timeout", type=int, default=900)
    s.add_argument("argv", nargs=argparse.REMAINDER)
    s.set_defaults(func=cmd_exec)

    args = ap.parse_args()
    if getattr(args, "argv", None) and args.argv and args.argv[0] == "--":
        args.argv = args.argv[1:]
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
