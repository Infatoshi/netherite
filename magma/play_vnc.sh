#!/usr/bin/env bash
# Play magma_game over VNC on anvil (headless). Starts Xvfb :1 + openbox + x11vnc
# (localhost:5900, pw redstone) and launches the C game on that display. From the Mac:
#   ssh -f -N -L 5901:localhost:5900 anvil   then   open vnc://localhost:5901
# Controls: WASD move, mouse look, space jump, shift sneak, left-click break,
#   right-click place, 1-9 / wheel hotbar, ESC quits.
#
# Registry: view_radius_active via --set (default 6; higher = longer view, lower FPS.
# 8 = pool cap / verified). Accepts legacy MAGMA_VIEW_RADIUS env as the radius value
# only for this launcher script (the game binary no longer reads that env).
#      This REPLACES any Java game running on :1 (same display/port as start_vnc_client.sh).
set -u
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export DISPLAY=:1
VNCPW="redstone"
W=1280; H=720
RADIUS="${MAGMA_VIEW_RADIUS:-6}"

echo "=== building magma_game ==="
make -C "$DIR" game >/dev/null || { echo "build failed"; exit 1; }

echo "=== (re)starting display :1 + VNC ==="
pkill -f "Xvfb :1"  2>/dev/null; pkill -f "x11vnc" 2>/dev/null; pkill -f "openbox" 2>/dev/null
pkill -f "magma_game" 2>/dev/null
sleep 0.5
nohup Xvfb :1 -screen 0 ${W}x${H}x24 > "$DIR/xvfb.log" 2>&1 &
sleep 1
nohup openbox > "$DIR/openbox.log" 2>&1 &
mkdir -p "$HOME/.vnc"
x11vnc -storepasswd "$VNCPW" "$HOME/.vnc/passwd" >/dev/null 2>&1
nohup x11vnc -display :1 -rfbauth "$HOME/.vnc/passwd" -localhost -forever -shared \
      -rfbport 5900 -noxdamage > "$DIR/x11vnc.log" 2>&1 &
sleep 1

echo "STARTED display=:1 vncport=5900 pw=$VNCPW view_radius=$RADIUS"
echo "  Mac: ssh -f -N -L 5901:localhost:5900 anvil ; open vnc://localhost:5901"
echo "=== launching magma_game (ESC to quit) ==="
exec "$DIR/magma_game" --seed 0 --w "$W" --h "$H" --view-distance "$RADIUS"
