#!/usr/bin/env bash
# The keyframe segments `make test` runs for one snapshot recording.
#   bash tests/segments.sh REC_DIR [SEGROWS]
# REC_DIR is out/java/snapshots/NAME; its keyframe set is
# out/java/keyframes/NAME/set.json (oracle/tests/keyframes.sh). Prints one line
# per segment, "ROWS TAG FLAGS...": the rows it replays, a tag naming it
# (s<first row>) and the test_snapshots flags that run it (the keyframe it
# starts from, the row it stops before, and the next keyframe as its
# round-trip when that keyframe's state matched at the set's making, from
# the keyframe before it, which is where this segment starts). The
# segments are keyframe to keyframe, keyframes closer than SEGROWS rows
# (default 1) to the previous one or to the end skipped. Prints nothing and
# exits 1 when there is no set or it was made from another tape: the
# recording then runs whole.
rec=${1%/}
min=${2:-1}
set_dir="$(dirname "$rec")/../keyframes/$(basename "$rec")"
[ -f "$set_dir/set.json" ] || exit 1
sha=$(tail -n 1 "$rec/tape.jsonl" | tr -d '\r\n' | sha1sum | cut -c1-40)
jq -r --arg sha "$sha" --argjson min "$min" --arg kd "$set_dir" '
  .keyframes as $all | if .tape.lastSha1 != $sha then empty else
    .start as $s | .tape.rows as $n | .roundtrip as $rt |
    (.keyframes | to_entries | map({key: (.value | tostring), value: (if .key == 0 then null else $all[.key - 1] end)})) as $pl |
    ($pl | from_entries) as $prev |
    (reduce .keyframes[] as $k ([]; if ($k - (.[-1] // $s)) >= $min and ($n - $k) >= $min then . + [$k] else . end)) as $ks |
    ([null] + $ks) as $from | ($ks + [null]) as $to |
    range(0; $from | length) as $i |
    [ (($to[$i] // $n) - ($from[$i] // $s)),
      "s\($from[$i] // $s)",
      (if $from[$i] != null then "--keyframe \($kd)/t\($from[$i])" else empty end),
      (if $to[$i] != null then "--to-row \($to[$i])" else empty end),
      (if $to[$i] != null and $rt[$to[$i] | tostring] == true and $prev[$to[$i] | tostring] == $from[$i]
       then "--state-diff \($kd)/t\($to[$i])" else empty end) ]
    | map(tostring) | join(" ")
  end' "$set_dir/set.json" | grep . || exit 1
