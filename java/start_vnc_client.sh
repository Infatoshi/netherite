#!/usr/bin/env bash
# Launch Minecraft 1.11.2 (compiled from the decompiled source workspace) into a
# virtual display on anvil, served over VNC (localhost-only; tunnel from the Mac).
set -u
# Prefer a caller-supplied JAVA_HOME when it already has a working java binary
# (e.g. a bootstrapped JDK under /tmp). Fall back to the system OpenJDK 8.
if [ -z "${JAVA_HOME:-}" ] || [ ! -x "${JAVA_HOME}/bin/java" ]; then
    export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
fi
export PATH="$JAVA_HOME/bin:$PATH"
export DISPLAY=:1
export LIBGL_ALWAYS_SOFTWARE=1          # force llvmpipe software GL (keep off the busy NVIDIA GPU)
export MESA_GL_VERSION_OVERRIDE=2.1      # MC 1.11.2 expects a GL 2.x context
# Fresh openjdk-8 packages enable GNOME Atk assistive tech by default; under
# Xvfb that throws AWTError and aborts GradleStart before the bridge binds.
export JAVA_TOOL_OPTIONS="${JAVA_TOOL_OPTIONS:-} -Djavax.accessibility.assistive_technologies="
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"   # repo root
MCDIR="$DIR/Minecraft"
VNCPW="redstone"                         # 8-char VNC password (VNC protocol caps at 8)

# --- clean any previous session ---
pkill -f "GradleStart"  2>/dev/null
pkill -f "runClient"    2>/dev/null
pkill -f "Xvfb :1"      2>/dev/null
pkill -f "x11vnc"       2>/dev/null
pkill -f "openbox"      2>/dev/null
sleep 2

# --- 1. virtual display with GLX ---
nohup Xvfb :1 -screen 0 1280x720x24 +extension GLX +render -noreset > "$DIR/xvfb.log" 2>&1 &
sleep 2

# --- 2. window manager (so the MC window is movable/visible) ---
nohup openbox > "$DIR/openbox.log" 2>&1 &
sleep 1

# --- 3. VNC server, bound to localhost only ---
mkdir -p "$HOME/.vnc"
x11vnc -storepasswd "$VNCPW" "$HOME/.vnc/passwd" >/dev/null 2>&1
nohup x11vnc -display :1 -rfbauth "$HOME/.vnc/passwd" -localhost -forever -shared \
      -rfbport 5900 -noxdamage > "$DIR/x11vnc.log" 2>&1 &
sleep 2

# --- 4. sanity: software GL up on :1 ---
echo "=== glxinfo on :1 ==="
glxinfo 2>/dev/null | grep -E "OpenGL renderer|OpenGL version" || echo "WARN: glxinfo failed"

# --- 5. launch the from-source client ---
cd "$MCDIR" || exit 1
# --offline: all deps are cached in run/gradle; without it gradle re-HEADs the dead
# fernflower-2.0-SNAPSHOT on repo.spongepowered.org every 24h and a repo 500 kills
# the launch. -x getAssets: resources.download.minecraft.net 400s the old hash
# URLs and --offline does NOT stop getAssets (raw HTTP in the task), so exclude
# it. Set MC_GRADLE_ONLINE=1 to force a real resolve + asset pass (new deps only).
GRADLE_NET_FLAG="--offline -x getAssets"; [ "${MC_GRADLE_ONLINE:-0}" = 1 ] && GRADLE_NET_FLAG=""
# Any args to this script are forwarded verbatim to gradle (mc_cli.py passes
# -PmcUsername=... / -PqrlPort=...; the JVM gets no invented env vars).
nohup ./gradlew -g run/gradle $GRADLE_NET_FLAG runClient --stacktrace "$@" > "$DIR/runclient.log" 2>&1 &
echo "RUNCLIENT_PID $!"
echo "STARTED display=:1 vncport=5900 pw=$VNCPW  (Mac: ssh -f -N -L 5901:localhost:5900 anvil; open vnc://localhost:5901)"
