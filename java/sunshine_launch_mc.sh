#!/usr/bin/env bash
# Sunshine app launcher for the mc-env 1.11.2 client on anvil's :0 (AMD iGPU, hardware GL).
# Sunshine runs apps with a minimal environment; gradle needs HOME and a sane PATH or it
# dies within seconds ("Could not create service ... GradleUserHomeScopeServices").
# Logs to /tmp/mc_stream_launch.log so a silent sunshine exit is diagnosable.
set -u
# Sunshine may strip HOME; recover from passwd if unset.
: "${HOME:=$(getent passwd "$(id -un)" | cut -d: -f6)}"
export HOME
export PATH="/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin"
export DISPLAY="${DISPLAY:-:0}"
export XAUTHORITY="${XAUTHORITY:-/run/user/1002/gdm/Xauthority}"
export JAVA_HOME=/usr/lib/jvm/java-8-openjdk-amd64
: > /tmp/mc_stream_launch.log

# anvil is headless: make sure :0 has an active CRTC or the stream is black.
# The connector force needs root once per boot (passwordless sudo is configured).
if ! xrandr | grep -q ' connected'; then
  echo on | sudo -n tee /sys/kernel/debug/dri/1/HDMI-A-1/force >/dev/null 2>&1
  xrandr >/dev/null 2>&1
  sleep 1
fi
# Mode name MUST be plain WIDTHxHEIGHT: LWJGL2's xrandr parser crashes
# (ArrayIndexOutOfBoundsException in XRandR.findPrimary) on names like 1920x1080_60.
if ! xrandr | grep -qE '^\s+1920x1080\s'; then
  xrandr --newmode "1920x1080" 173.00 1920 2048 2248 2576 1080 1083 1088 1120 -hsync +vsync 2>/dev/null
  xrandr --addmode HDMI-A-0 1920x1080 2>/dev/null
fi
xrandr --output HDMI-A-0 --mode 1920x1080 2>/dev/null

# Single-instance guard: if the game is already up (e.g. an armed recording
# session), just surface its window instead of racing a second client onto the
# same save. Exiting 0 makes sunshine treat the app as detached and stream the
# desktop, which is exactly what we want.
if pgrep -f '[G]radleStart --username' >/dev/null; then
  WID=$(xdotool search --name '^Minecraft 1.11.2$' 2>/dev/null | head -1)
  [ -n "$WID" ] && { xdotool windowactivate "$WID" 2>/dev/null
                     wmctrl -i -r "$WID" -b add,fullscreen 2>/dev/null; }
  echo "game already running; surfaced existing window" >> /tmp/mc_stream_launch.log
  exit 0
fi

# Human-play profile: regenerate run/options.txt + run/qrl_launch.json from
# vanilla.yaml EVERY launch (full vanilla gamerules/sound/menus; also heals
# saves whose level.dat still carries old harness gamerules). The agent stack
# writes fast.yaml over these via mc_cli.py when it launches - last writer
# wins, and only one instance runs at a time by design.
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT/java"
uv run --no-project --with pyyaml python mc_cli.py --config vanilla.yaml --no-launch \
  >> /tmp/mc_stream_launch.log 2>&1 \
  || echo "WARN: vanilla.yaml config write failed; launching with on-disk config" \
       >> /tmp/mc_stream_launch.log

cd "$ROOT/java/Minecraft"
exec ./gradlew -g run/gradle --offline -x getAssets runClient \
  >> /tmp/mc_stream_launch.log 2>&1
