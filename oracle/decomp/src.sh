#!/usr/bin/env bash
# oracle/src, made here and never committed: the pristine decompile, the
# determinism pass and oracle.patch (make -C oracle src; every build runs it).
#
#   bash src.sh            make oracle/src current with oracle.patch
#   bash src.sh pristine   print the cached pristine tree's path (making it)
#
# The pristine tree (MCP 9.08's decompile of the 1.7.10 client, flattened as
# oracle-src.sh step 4 leaves it) is kept in CACHE/pristine/KEY (default
# ~/.cache/netherite-oraclesrc), KEY a hash of what shapes its text: the jar,
# MCP and the Dockerfile. It comes from git tag mcp-pristine where this
# checkout has it (the private history; the same bytes, lane/oraclebuild),
# else from oracle-src.sh --pristine (MC_HOME's jar and libraries, MCP 9.08,
# Docker; 25 to 50 s, once).
#
# oracle/src is only replaced when it holds no edit of its own: the stamp
# (out/java/src.stamp) names the patch and determinize.sh it was made from,
# and a file under src newer than the stamp is an edit not yet in
# oracle.patch (run make -C oracle oracle-patch, or FORCE=1 to drop it).
set -euo pipefail
sha1sum() { if type -P sha1sum > /dev/null; then command sha1sum "$@"; else shasum "$@"; fi; }
HERE=$(cd "$(dirname "$0")" && pwd)
J=$(cd "$HERE/.." && pwd)
REPO=$(cd "$J/.." && pwd)
CACHE=${CACHE:-$HOME/.cache/netherite-oraclesrc}
STAMP=$REPO/out/java/src.stamp
JAR_MD5=e6b7a531b95d0c172acb704d1f54d1b3
MCP_SHA256=23acbf233732c412db88d2975b3c85d020d02d5fdafbbdc9f2a6117ffd2184c6
KEY=$( { echo "$JAR_MD5 $MCP_SHA256 flat1"; cat "$HERE/Dockerfile"; } | sha1sum | cut -c1-16)
P=$CACHE/pristine/$KEY

pristine() {
  [ -f "$P/Start.java" ] && return 0
  mkdir -p "$CACHE/pristine"
  local t=$P.tmp.$$
  rm -rf "$t"
  if git -C "$REPO" rev-parse -q --verify 'refs/tags/mcp-pristine^{commit}' > /dev/null 2>&1; then
    mkdir -p "$t.a"
    local tagged=oracle/src
    # The private pristine tag predates the java/ to oracle/ rename.
    git -C "$REPO" cat-file -e mcp-pristine:oracle/src 2>/dev/null || tagged=java/src
    git -C "$REPO" archive mcp-pristine "$tagged" | tar -x -C "$t.a" --strip-components=2
    mkdir -p "$t" && mv "$t.a/net/minecraft"/* "$t"/ && mv "$t.a/Start.java" "$t"/ && rm -rf "$t.a"
  else
    echo "src: no pristine decompile cached; making it from the 1.7.10 install (oracle-src.sh --pristine)" >&2
    bash "$HERE/oracle-src.sh" --pristine "$t" >&2
  fi
  mv "$t" "$P" 2>/dev/null || rm -rf "$t"   # a concurrent maker may have won
  [ -f "$P/Start.java" ]
}

if [ "${1:-}" = pristine ]; then pristine; echo "$P"; exit 0; fi

want=$(cat "$HERE/oracle.patch" "$HERE/determinize.sh" | sha1sum | cut -c1-40)
S=$J/src
if [ -d "$S" ] && [ -f "$STAMP" ]; then
  edited=$(find "$S" -newer "$STAMP" -print -quit)
  if [ -z "$edited" ] && [ "$(cat "$STAMP")" = "$want" ]; then exit 0; fi
  if [ -n "$edited" ] && [ -z "${FORCE:-}" ]; then
    [ "$(cat "$STAMP")" = "$want" ] && exit 0   # an edit in progress over the current patch: keep it
    echo "src: oracle/src has edits ($edited) and oracle.patch changed: run make -C oracle oracle-patch first (FORCE=1 drops the edits)" >&2
    exit 1
  fi
fi
pristine
N=$J/src.new.$$
trap 'rm -rf "$N"' EXIT
rm -rf "$N" && cp -R "$P" "$N"
bash "$HERE/determinize.sh" "$N"
perl "$HERE/oraclepatch.pl" apply "$N" "$HERE/oracle.patch"
if [ -d "$S" ] && [ ! -f "$STAMP" ] && [ -z "${FORCE:-}" ] && ! diff -rq "$S" "$N" > /dev/null 2>&1; then
  echo "src: oracle/src differs from oracle.patch's tree and has no stamp: run make -C oracle oracle-patch to keep its edits (FORCE=1 replaces it)" >&2
  exit 1
fi
rm -rf "$S.old.$$"; [ -d "$S" ] && mv "$S" "$S.old.$$"
mv "$N" "$S"
rm -rf "$S.old.$$"
mkdir -p "$(dirname "$STAMP")"
echo "$want" > "$STAMP"
rm -f "$REPO/out/java/classes/.stamp"   # the build's source list was read before src existed
echo "src: oracle/src from oracle.patch ($(find "$S" -name '*.java' | wc -l | tr -d ' ') files)"
