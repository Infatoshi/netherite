#!/bin/bash
# lane/rawsearch: an eval's view books (ntloop --view-books K: DIR/view.jsonl, a line an episode) against its outcomes
# (DIR/episodes.jsonl), each env's first episode, one line:
#   bash csrc/runtime/train/viewbooks.sh DIR ITEM [LABEL]
# For the won and the lost episodes apart: how many, the share that had the object on screen at all (saw), that lost it
# from view after seeing it at least once (lost), that got it back after losing it (reacq), that had it in view at the
# episode's end (end), and the mean steps with it in view (in) and out of view after the first sighting (out).
set -e
d=$1 item=$2 label=${3:-VIEW $1}
[ -f "$d/view.jsonl" ] && [ -f "$d/episodes.jsonl" ] || { echo "$label: no view books in $d"; exit 0; }
jq -n -r --arg item "$item" --arg label "$label" --slurpfile ep "$d/episodes.jsonl" --slurpfile vb "$d/view.jsonl" '
  ($ep | group_by(.env) | map(.[0] | {key: (.env | tostring), value: ((.items[$item] // 0) > 0)}) | from_entries) as $won |
  ($vb | map(select(.ep == 0 and ($won[.env | tostring] != null)))) as $v |
  def r(x): (x * 1000 | round / 1000);
  def part(name; xs): xs as $x | ($x | length) as $n |
    if $n == 0 then "\(name) 0" else
    "\(name) \($n): saw \(r($x | map(select(.first >= 0)) | length / $n)), lost \(r($x | map(select(.lost > 0)) | length / $n)), " +
    "reacq \(r($x | map(select(.reacq > 0)) | length / $n)), end \(r($x | map(select(.in_end == 1)) | length / $n)), " +
    "in \(r($x | map(.in) | add / $n)), out \(r($x | map(.out_after) | add / $n))" end;
  "\($label): " + part("won"; $v | map(select($won[.env | tostring]))) + "; " +
  part("lost"; $v | map(select($won[.env | tostring] | not)))'
