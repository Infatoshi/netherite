#!/usr/bin/env bash
# The client's own view of the world at every frame of a snapshot recording,
# for play --check to hold the live client to:
#   tests/clientview.sh SNAPSHOT_DIR
# replays the recording on the oracle with a frame after every tick and the
# render state probe on (make -C oracle replay EVERY=1 --renderstate), then
# keeps, per frame at partial tick 1.0, what the client world holds apart
# from the server: its clock (WorldClient's world time, which only an S03
# resets), each chest's and ender chest's lid and previous lid (the client
# tile entity's own, which an S24 block event drives), each spawner's
# spin and delay (from the S35's Delay), and each mob's
# equipment item ids (the client copy's, which an S04 sets) and lead holder
# (an S1B type 1's), and the explosion particles an S27 makes (the huge or
# large explosion and the huge one's children), the items and orbs the
# client holds and the pickup effects an S0D makes. Writes
# SNAPSHOT_DIR/clientview.jsonl; the frames and the probe dump are dropped.
set -euo pipefail
dir=${1:?usage: clientview.sh SNAPSHOT_DIR}
dir=$(cd "$dir" && pwd)
root=$(cd "$(dirname "$0")/../.." && pwd)
tmp=$root/out/java/try/clientview.$(basename "$dir").$$
rm -rf "$tmp"
trap 'rm -rf "$tmp"' EXIT
make -s -C "$root/oracle" replay REF="$dir/tape.jsonl" FRAMES="$tmp/frames" EVERY=1 \
  ARGS="--renderstate $tmp/rs" > "$tmp.log" 2>&1 || { tail -5 "$tmp.log"; rm -f "$tmp.log"; exit 1; }
rm -f "$tmp.log"
jq -c 'select(.pt == 1.0) | {t, wt: .wo.wt,
  chests: [(.tes.list // [])[] | select(.k == "TileEntityChest" or .k == "TileEntityEnderChest") | [.x, .y, .z, .lid, .plid]],
  spawners: [(.tes.list // [])[] | select(.k == "TileEntityMobSpawner") | [.x, .y, .z, .rot, .prot, .delay]],
  equip: [(.mobs // [])[] | select(.class != "EntityClientPlayerMP" and .class != "EntityOtherPlayerMP")
          | [.id, [.equip[] | if . == null then 0 else .id end]]],
  leash: [(.mobs // [])[] | select(.leash != null) | [.id, .leash]],
  blasts: [(.fx // [])[] | select(.cls == "EntityHugeExplodeFX" or .cls == "EntityLargeExplodeFX")
           | [(if .cls == "EntityHugeExplodeFX" then 1 else 2 end), .x, .y, .z, .age]],
  items: [(.ents // [])[] | select(.k == "item" or .k == "orb") | [.x, .y, .z]],
  pickups: [(.fx // [])[] | select(.cls == "EntityPickupFX") | [.ix, .iy, .iz, .page]]}' "$tmp/rs/frames.jsonl" > "$dir/clientview.jsonl"
echo "clientview: $(wc -l < "$dir/clientview.jsonl") frames in $dir/clientview.jsonl"
