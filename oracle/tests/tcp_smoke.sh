#!/usr/bin/env bash
# Drive a running `make oracle-serve PORT=...` over the TCP control port.
# Usage (from oracle/): bash tests/tcp_smoke.sh PORT FRAME_DIR   (against make serve PORT=...)
set -euo pipefail
P=$1; D=$(realpath -m "$2"); mkdir -p "$D"
exec 3<>"/dev/tcp/127.0.0.1/$P"
send() { echo "$1" >&3; IFS= read -r l <&3; echo "$l"; }
r1=$(send '{"cmd":"step","n":20,"act":{"hold":["forward","sprint"]}}')
r2=$(send "{\"cmd\":\"step\",\"n\":1,\"act\":{\"dlook\":[-45,30]},\"frame\":\"$D/a.png\"}")
r3=$(send '{"cmd":"state"}')
r4=$(send "{\"cmd\":\"frame\",\"path\":\"$D/b.png\"}")
r5=$(send '{"cmd":"quit"}')
exec 3>&-
for r in "$r1" "$r2" "$r3" "$r4" "$r5"; do echo "${r:0:220}"; done
px2=$(grep -o '"px":"[0-9a-f]*"' <<<"$r2" | head -1); px4=$(grep -o '"px":"[0-9a-f]*"' <<<"$r4" | tail -1)
[ -n "$px2" ] && [ "$px2" = "$px4" ] && echo "frame command reproduces the step frame: $px2" || { echo "frame mismatch: $px2 vs $px4"; exit 1; }
