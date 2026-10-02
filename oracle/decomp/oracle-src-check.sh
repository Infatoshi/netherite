#!/usr/bin/env bash
# The check that oracle/src is rebuildable from public inputs plus our patch
# (make -C oracle oracle-src-check):
# 1. oracle.patch is what mkpatch.sh makes from the working oracle/src (a lane
#    that edits oracle/src runs make -C oracle oracle-patch and commits the
#    patch; oracle/src itself is never committed);
# 2. in a fresh directory outside the checkout, holding only oracle/decomp's
#    scripts and the patch, with a clean environment, oracle-src.sh builds
#    the tree from the downloads and the decompile in Docker, and its git
#    tree hash is the working oracle/src's (made by src.sh from the cached
#    pristine tree, or git tag mcp-pristine);
# 3. make -C oracle build compiles the built tree (beside this checkout's
#    oracle/ and out/java libraries, the class store off).
# QUICK=1 stops after 1. COLD=1 starts 2 from an empty download cache and
# rebuilds the Docker image without its layer cache. ROOT is where the fresh
# directory goes (default ~/.cache/netherite-oraclesrc/check); it is removed
# on success, kept on failure. Prints each part's seconds.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
REPO=$(cd "$HERE/../.." && pwd)
ROOT=${ROOT:-$HOME/.cache/netherite-oraclesrc/check}

if [ -n "${QUICK:-}" ]; then rm -rf "$P"; exit 0; fi

# the fresh directory: nothing but the scripts and the patch
mkdir -p "$P/proof"
cp "$HERE"/{oracle-src.sh,determinize.sh,Dockerfile,oraclepatch.pl,oracle.patch,treehash.sh} "$P/proof/"
cache=$HOME/.cache/netherite-oraclesrc
[ -z "${COLD:-}" ] || cache=$P/cache
t=$(date +%s)
(cd "$P/proof" && env -i HOME="$HOME" PATH="$PATH" TMPDIR="$P" CACHE="$cache" ${COLD:+DOCKER_REBUILD=1} \
  bash ./oracle-src.sh "$P/proof/src") || { echo "oracle-src-check: FAIL oracle-src.sh (kept $P)"; exit 1; }
got=$(cd "$P/proof" && env -i HOME="$HOME" PATH="$PATH" bash ./treehash.sh src)
echo "oracle-src-check: built $([ -n "${COLD:-}" ] && echo cold || echo warm) in $(($(date +%s) - t)) s, tree $got"
[ "$got" = "$want" ] || { echo "oracle-src-check: FAIL tree $got != oracle/src $want (kept $P)"; exit 1; }

# make -C oracle build from the built tree
t=$(date +%s)
B=$P/build
mkdir -p "$B/oracle" "$B/out/java"
for e in "$REPO"/oracle/*; do case $(basename "$e") in src) ;; harness) cp -R "$e" "$B/oracle/" ;; *) ln -s "$e" "$B/oracle/" ;; esac; done  # make finds src and harness: real directories
mv "$P/proof/src" "$B/oracle/src"
for e in lib client natives assets; do ln -s "$(readlink -f "$REPO/out/java/$e")" "$B/out/java/$e"; done
make -s -C "$B/oracle" build CLASSTORE= > "$P/build.log" 2>&1 || { tail -20 "$P/build.log"; echo "oracle-src-check: FAIL make build (kept $P)"; exit 1; }
echo "oracle-src-check: make -C oracle build: $(grep '^built' "$P/build.log") ($(($(date +%s) - t)) s)"
rm -rf "$P"
echo "oracle-src-check: PASS"
