#!/usr/bin/env bash
# lane_stage.sh LANE HOST [--tape NAME]... - stage one delegated lane.
#
# Mac side: worktree ~/dev/nw/LANE on branch lane/LANE from master, pushed,
# with the same gitignored inputs agent_worktree.sh links (tapes, snaps,
# java run/ and oracle-src, asset headers, goldens).
# Remote side (HOST = gamer | anvil): clone ~/nlanes/LANE from the host's
# ~/dev/netherite, origin -> GitHub, checkout lane/LANE, symlink java inputs,
# then rsync from the Mac: goldens, blaze snaps, the canonical tape, and every
# --tape NAME (jsonl + sidecars + _frames + _world).
#
#   bash scripts/lane_stage.sh portaledge gamer
#   bash scripts/lane_stage.sh raster anvil --tape scenario_slime_bounce_20260730T095754Z
set -euo pipefail

LANE=${1:?usage: lane_stage.sh LANE HOST [--tape NAME]...}
HOST=${2:?usage: lane_stage.sh LANE HOST [--tape NAME]...}
shift 2
TAPES=(20260721T215812Z_fast_s0_survival_default_rd8_77b5b462)
while [ $# -gt 0 ]; do
    case "$1" in
        --tape) TAPES+=("$2"); shift 2 ;;
        *) echo "unknown arg: $1" >&2; exit 2 ;;
    esac
done

ROOT=$(git rev-parse --show-toplevel)
WT="$HOME/dev/nw/$LANE"
BR="lane/$LANE"

# --- Mac worktree -----------------------------------------------------------
if [ ! -e "$WT" ]; then
    git -C "$ROOT" worktree add -b "$BR" "$WT" master >/dev/null
    git -C "$ROOT" push -q origin "$BR"
    echo "worktree $WT on $BR from $(git -C "$ROOT" rev-parse --short master)"
fi
mkdir -p "$WT/verify/tapes"
for e in "$ROOT"/verify/tapes/*; do
    [ -e "$e" ] || continue
    t="$WT/verify/tapes/$(basename "$e")"; [ -e "$t" ] || ln -s "$e" "$t"
done
if [ -d "$ROOT/blaze/rl/out/snaps" ] && [ ! -e "$WT/blaze/rl/out/snaps" ]; then
    mkdir -p "$WT/blaze/rl/out"; ln -s "$ROOT/blaze/rl/out/snaps" "$WT/blaze/rl/out/snaps"
fi
for j in java/Minecraft/run java/oracle-src; do
    [ -e "$ROOT/$j" ] && [ ! -e "$WT/$j" ] && ln -s "$ROOT/$j" "$WT/$j"
done
for e in "$ROOT"/magma/assets/*.h; do
    [ -e "$e" ] || continue
    t="$WT/magma/assets/$(basename "$e")"; [ -e "$t" ] || cp "$e" "$t"
done
touch "$WT"/magma/assets/*.h 2>/dev/null || true
for d in verify/mc_capture/goldens verify/ui_hud/goldens verify/ui_entities/goldens; do
    [ -d "$ROOT/$d" ] || continue
    [ -e "$WT/$d" ] || { mkdir -p "$(dirname "$WT/$d")"; ln -s "$ROOT/$d" "$WT/$d"; }
done
echo "mac worktree staged"

# --- remote clone -----------------------------------------------------------
ssh "$HOST" "set -e
mkdir -p ~/nlanes
[ -d ~/nlanes/$LANE ] || git clone -q ~/dev/netherite ~/nlanes/$LANE
cd ~/nlanes/$LANE
git remote set-url origin https://github.com/Infatoshi/netherite.git
git fetch -q origin
git checkout -q $BR 2>/dev/null || git checkout -q -b $BR origin/$BR
git reset -q --hard origin/$BR
ln -sfn ~/dev/netherite/java/oracle-src java/oracle-src
ln -sfn ~/dev/netherite/java/Minecraft/run java/Minecraft/run
mkdir -p verify/tapes out/verify blaze/rl/out
echo remote clone at \$(git rev-parse --short HEAD)"

R="$HOST:~/nlanes/$LANE"
for d in verify/mc_capture/goldens verify/ui_hud/goldens verify/ui_entities/goldens; do
    [ -d "$ROOT/$d" ] || continue
    rsync -aq "$ROOT/$d/" "$R/$d/"
done
[ -d "$ROOT/blaze/rl/out/snaps" ] && rsync -aq "$ROOT/blaze/rl/out/snaps/" "$R/blaze/rl/out/snaps/"
for T in "${TAPES[@]}"; do
    n=0
    for e in "$ROOT"/verify/tapes/"$T"*; do
        [ -e "$e" ] || continue
        rsync -aq "$e" "$R/verify/tapes/"; n=$((n+1))
    done
    [ $n -gt 0 ] || { echo "tape not found locally: $T" >&2; exit 1; }
    echo "staged tape $T ($n entries)"
done
echo "lane $LANE staged on $HOST"
