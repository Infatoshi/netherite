#!/usr/bin/env bash
# The harness id (oracle/Makefile HARNESS): the first 12 hex of the sha1 of
# every src/ and harness/ .java file's bytes, in sorted path order.
#   bash tests/harness.sh OUT      (from oracle/)
# Kept in OUT/harness.id beside a stamp of the files' paths, sizes and
# mtimes (one find, nothing read): the id is recomputed only when a source
# was added, removed or written since. It was a make variable evaluated at
# every use, 5 or 6 times per make replay (1,400 files through cat and
# sha1sum each time): 1 to 4 s of a short replay at load 80 (lane/replaylat).
# Linux only (find -printf); the Makefile computes it directly on macOS.
set -uo pipefail
out=$1
st=$(find src harness -name '*.java' -printf '%p %s %T@\n' | LC_ALL=C sort | sha1sum)
st=${st%% *}
if read -r s h 2> /dev/null < "$out/harness.id" && [ "$s" = "$st" ] && [ ${#h} = 12 ]; then
  echo "$h"
  exit 0
fi
h=$(find src harness -name '*.java' | LC_ALL=C sort | xargs cat | sha1sum)
h=${h:0:12}
mkdir -p "$out" && printf '%s %s\n' "$st" "$h" > "$out/harness.id.$$" && mv -f "$out/harness.id.$$" "$out/harness.id"
echo "$h"
