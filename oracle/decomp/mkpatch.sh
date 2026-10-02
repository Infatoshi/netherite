#!/usr/bin/env bash
# Regenerate oracle.patch from oracle/src: the pristine decompile (src.sh
# pristine), determinized as oracle-src.sh does it, against the working
# oracle/src. Then count the patch's literal lines that occur verbatim in the
# pristine decompile.
#   bash oracle/decomp/mkpatch.sh [PATCH]    (make -C oracle oracle-patch)
# PATCH defaults to oracle/decomp/oracle.patch; oracle-src-check.sh writes a
# scratch one to compare. Writing the default patch also re-stamps oracle/src
# (out/java/src.stamp), so its edits count as the patch's.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
# (an old caller's REV PATCH: the REV is ignored, oracle/src is the working tree's)
[ $# = 2 ] && shift
PATCH=$(realpath -m "${1:-$HERE/oracle.patch}")
cd "$HERE/../.."
[ -d oracle/src ] || { echo "mkpatch: no oracle/src (make -C oracle src)"; exit 1; }
P=$(bash "$HERE/src.sh" pristine)
W=$PWD/out/java/mkpatch
rm -rf "$W" && mkdir -p "$W"
cp -R "$P" "$W/base"
bash "$HERE/determinize.sh" "$W/base"
TMPDIR=$W perl "$HERE/oraclepatch.pl" make "$W/base" oracle/src > "$PATCH.tmp"
mv "$PATCH.tmp" "$PATCH"
perl "$HERE/oraclepatch.pl" verbatim "$PATCH" "$P" > "$W/verbatim.tsv"
if [ "$PATCH" = "$HERE/oracle.patch" ]; then
  cat "$PATCH" "$HERE/determinize.sh" | { type -P sha1sum > /dev/null && sha1sum || shasum; } | cut -c1-40 > out/java/src.stamp
fi
echo "mkpatch: $PATCH ($(wc -l < "$PATCH") lines; verbatim lines in $W/verbatim.tsv)"
