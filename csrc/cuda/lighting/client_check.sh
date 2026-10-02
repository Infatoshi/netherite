#!/bin/bash
# client_check.sh [-j J] NAME...: the client world's light deferred to the
# device (test_snapshots --light-defer cuda, out/native/cuda/lighting_client) against the
# host's own run (out/native/test_snapshots), each recording under
# out/java/snapshots: every row's client light digest (tests/cwdigest.h: the
# light, band storage, heights, stamps and write sequences of every client
# chunk, and the world's epoch and write sequence) byte for byte, and the
# rows themselves (both replays pass). One line each, then the totals;
# exit 0 when all are equal. Logs and digests in out/native/cuda/lighting_client/NAME.*
cd "$(dirname "$0")/../../.." || exit 2
J=4
[ "$1" = -j ] && { J=$2; shift 2; }
O=out/native/cuda/lighting_client
mkdir -p $O
one() {
  n=$1; d=out/java/snapshots/$n; O=out/native/cuda/lighting_client
  [ -d $d ] || { echo "$n FAIL no such recording"; return; }
  bash csrc/tests/memslot.sh $d out/native/test_snapshots --cw-digest $O/$n.host $d > $O/$n.host.log 2>&1; h=$?
  bash csrc/tests/memslot.sh $d $O/test_snapshots --light-defer cuda --cw-digest $O/$n.dev $d > $O/$n.dev.log 2>&1; g=$?
  rows=$(wc -l < $O/$n.host 2>/dev/null)
  if [ $h != 0 ] || [ $g != 0 ]; then echo "$n FAIL replay rc host $h device $g: $(grep -E '^FAIL|light defer:|lightcap|lighting' $O/$n.dev.log | head -2 | cut -c1-200)"; return; fi
  if ! cmp -s $O/$n.host $O/$n.dev; then echo "$n FAIL client light digest differs at row $(cmp $O/$n.host $O/$n.dev 2>&1 | sed -n 's/.*line \([0-9]*\).*/\1/p' | head -1 | xargs -I{} sed -n '{}p' $O/$n.host | cut -d' ' -f1)"; return; fi
  s=$(grep -o '[0-9]* client light calls and [0-9]* relight checks deferred; [0-9]* runs' $O/$n.dev.log)
  echo "$n OK $rows rows, every client light digest equal ($s)"
}
export -f one
printf '%s\n' "$@" | xargs -P $J -I{} bash -c 'one {}' | tee $O/check.txt
ok=$(grep -c ' OK ' $O/check.txt); tot=$#
echo "gpu-lighting-client-check: $ok/$tot recordings OK: the client light deferred to the device equals the host's after every row"
[ $ok = $tot ]
