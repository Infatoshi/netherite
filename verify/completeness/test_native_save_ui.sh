#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
mkdir -p "$ROOT/.tmp"
WORK="$(mktemp -d "$ROOT/.tmp/native-save-ui.XXXXXX")"
XVFB_PID=""
GAME_PID=""

cleanup() {
    if [[ -n "$GAME_PID" ]] && kill -0 "$GAME_PID" 2>/dev/null; then
        kill "$GAME_PID" 2>/dev/null || true
        wait "$GAME_PID" 2>/dev/null || true
    fi
    if [[ -n "$XVFB_PID" ]] && kill -0 "$XVFB_PID" 2>/dev/null; then
        kill "$XVFB_PID" 2>/dev/null || true
        wait "$XVFB_PID" 2>/dev/null || true
    fi
    rm -rf "$WORK"
}
trap cleanup EXIT

for NUMBER in $(seq 91 119); do
    if [[ ! -e "/tmp/.X${NUMBER}-lock" ]]; then
        export DISPLAY=":$NUMBER"
        Xvfb "$DISPLAY" -screen 0 1024x768x24 >"$WORK/xvfb.log" 2>&1 &
        XVFB_PID=$!
        sleep 0.2
        if kill -0 "$XVFB_PID" 2>/dev/null; then
            break
        fi
        XVFB_PID=""
    fi
done
if [[ -z "$XVFB_PID" ]]; then
    echo "FAIL native save UI: no free Xvfb display" >&2
    exit 1
fi

export MAGMA_NATIVE_WORLD_ROOT="$WORK/saves"
export MAGMA_WORLD_SLOT="World1"
export ALSOFT_DRIVERS=null
"$ROOT/magma/magma_game" >"$WORK/game.log" 2>&1 &
GAME_PID=$!

WINDOW=""
for _ in $(seq 1 100); do
    WINDOW="$(xdotool search --name '^magma - game$' 2>/dev/null | head -n1 || true)"
    [[ -n "$WINDOW" ]] && break
    kill -0 "$GAME_PID" 2>/dev/null || {
        sed -n '1,160p' "$WORK/game.log" >&2
        echo "FAIL native save UI: game exited before opening a window" >&2
        exit 1
    }
    sleep 0.1
done
if [[ -z "$WINDOW" ]]; then
    echo "FAIL native save UI: window did not appear" >&2
    exit 1
fi

# ESC opens the pause menu on its first press. The second button is centered at
# framebuffer (426,232) for the default 854x480 window.
# Window creation precedes the first completed frame; wait until the event loop
# is live so the initial ESC cannot be discarded during renderer startup.
sleep 0.5
xdotool windowfocus --sync "$WINDOW"
xdotool key --window "$WINDOW" Escape
sleep 0.6
import -display "$DISPLAY" -window "$WINDOW" "$WORK/pause.png"
xdotool mousemove --window "$WINDOW" 426 232
xdotool click --window "$WINDOW" 1

for _ in $(seq 1 100); do
    [[ -f "$WORK/saves/World1/current" ]] && break
    kill -0 "$GAME_PID" 2>/dev/null || break
    sleep 0.1
done
[[ -f "$WORK/saves/World1/current" ]] || {
    sed -n '1,200p' "$WORK/game.log" >&2
    echo "FAIL native save UI: Save and Quit did not publish a slot" >&2
    exit 1
}
[[ "$(<"$WORK/saves/World1/current")" == "0000000000000001" ]] || {
    echo "FAIL native save UI: first save did not publish generation 1" >&2
    exit 1
}
import -display "$DISPLAY" -window "$WINDOW" "$WORK/world-list.png"
cmp -s "$WORK/pause.png" "$WORK/world-list.png" && {
    echo "FAIL native save UI: pause and world-list frames are identical" >&2
    exit 1
}

# World1 is selected after refresh. Play reloads it, then a second pause/save
# proves the loaded runtime can publish the next generation through the UI.
xdotool mousemove --window "$WINDOW" 324 434
xdotool click --window "$WINDOW" 1
sleep 0.6
xdotool windowfocus --sync "$WINDOW"
xdotool key --window "$WINDOW" Escape
sleep 0.4
xdotool mousemove --window "$WINDOW" 426 232
xdotool click --window "$WINDOW" 1
for _ in $(seq 1 100); do
    [[ "$(sed -n '1p' "$WORK/saves/World1/current" 2>/dev/null || true)" == \
       "0000000000000002" ]] && break
    kill -0 "$GAME_PID" 2>/dev/null || break
    sleep 0.1
done
[[ "$(sed -n '1p' "$WORK/saves/World1/current" 2>/dev/null || true)" == \
   "0000000000000002" ]] || {
    sed -n '1,240p' "$WORK/game.log" >&2
    echo "FAIL native save UI: reload/resave did not publish generation 2" >&2
    exit 1
}
[[ -d "$WORK/saves/World1/generation-0000000000000001" ]]
[[ -d "$WORK/saves/World1/generation-0000000000000002" ]]

# Back exits only the game window. The test owns and stops its Xvfb separately.
xdotool mousemove --window "$WINDOW" 528 434
xdotool click --window "$WINDOW" 1
for _ in $(seq 1 50); do
    kill -0 "$GAME_PID" 2>/dev/null || break
    sleep 0.1
done
if kill -0 "$GAME_PID" 2>/dev/null; then
    echo "FAIL native save UI: Back did not close the game" >&2
    exit 1
fi
wait "$GAME_PID"
GAME_PID=""
echo "PASS native save UI: pause, save-and-title, world selection, reload, resave"
