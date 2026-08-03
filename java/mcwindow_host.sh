#!/usr/bin/env bash
# Host side of the `mc` alias: ensure game + mcwindow_server are up, return
# once the viewer port is listening. Idempotent; safe to run per session.
set -u
REPO="$HOME/dev/netherite"

if ! pgrep -f '[m]cwindow_server.py' >/dev/null; then
  DISPLAY=:0 nohup uv run --no-project --with python-xlib python \
    "$REPO/java/mcwindow_server.py" >/tmp/mcwindow_server.log 2>&1 &
  disown
fi

for _ in $(seq 1 40); do
  ss -ltn 2>/dev/null | grep -q ':25581 ' && { echo "mcwindow server up"; exit 0; }
  sleep 0.25
done
echo "mcwindow server failed; see /tmp/mcwindow_server.log" >&2
exit 1
