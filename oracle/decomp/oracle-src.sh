#!/usr/bin/env bash
# Build oracle/src from public inputs plus our patch, so the decompiled tree
# never has to be distributed:
#
#   bash oracle-src.sh OUT        (make -C oracle oracle-src: OUT=../out/java/oracle-src)
#   bash oracle-src.sh --pristine OUT   stop after step 4: the flattened
#                                 decompile, before determinize and the patch
#                                 (src.sh keeps it in CACHE/pristine)
#
# 1. the 1.7.10 client jar, its version json, libraries and natives: from
#    MC_HOME if it holds them, else from Mojang's version manifest, each
#    checked by the manifest's sha1 (the jar also by md5 e6b7a531...);
# 2. MCP 9.08 (mcp908.zip, sha256 23acbf23...): MCP_ZIP if set, else the
#    public URLs below, else MCP_ARCHIVE (the old local tar.zst);
# 3. MCP's decompile in Docker (Dockerfile here: python2, astyle, JDK 8);
# 4. flatten src/minecraft/net/minecraft/X to X (Start.java stays at the top);
# 5. determinize.sh, the mechanical determinism pass;
# 6. oracle.patch (oraclepatch.pl apply), every later edit, generated from
#    git by mkpatch.sh.
#
# Needs only this directory (oracle-src.sh, determinize.sh, Dockerfile,
# oraclepatch.pl, oracle.patch), bash, curl, jq, unzip, perl and Docker.
# Downloads are kept by hash in CACHE (default ~/.cache/netherite-oraclesrc);
# the work tree is OUT.work, removed at the end. OUT is replaced.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PRISTINE=
[ "${1:-}" = --pristine ] && { PRISTINE=1; shift; }
OUT=${1:?usage: oracle-src.sh [--pristine] OUT}
case $OUT in /*) ;; *) OUT=$PWD/$OUT ;; esac
CACHE=${CACHE:-$HOME/.cache/netherite-oraclesrc}
MC_HOME=${MC_HOME:-}
MCP_ZIP=${MCP_ZIP:-}
MCP_ARCHIVE=${MCP_ARCHIVE:-}
MANIFEST=https://piston-meta.mojang.com/mc/game/version_manifest_v2.json
MCP_URLS="http://www.modcoderpack.com/files/mcp908.zip https://web.archive.org/web/20171108231530id_/http://www.modcoderpack.com/files/mcp908.zip"
MCP_SHA256=23acbf233732c412db88d2975b3c85d020d02d5fdafbbdc9f2a6117ffd2184c6
JAR_MD5=e6b7a531b95d0c172acb704d1f54d1b3
IMAGE=netherite-v2-mcp:$(cat "$HERE/Dockerfile" | sha1sum | cut -c1-12)
W=$OUT.work
t0=$(date +%s)
step() { printf "oracle-src: %-34s %4d s\n" "$1" $(($(date +%s) - t0)); }
die() { echo "oracle-src: $*" >&2; exit 1; }
sum() { case $1 in sha1) sha1sum "$2" ;; sha256) sha256sum "$2" ;; md5) md5sum "$2" ;; esac | cut -d' ' -f1; }

# fetch KIND HASH DEST URL...: DEST from the cache (by hash) or the first URL
# whose bytes have that hash
fetch() {
  local kind=$1 want=$2 dest=$3 url c=$CACHE/$1/$2; shift 3
  if [ ! -f "$c" ] || [ "$(sum "$kind" "$c")" != "$want" ]; then
    mkdir -p "$CACHE/$kind"
    for url in "$@"; do
      curl -fsSL --retry 3 --max-time 300 -o "$c.part" "$url" || continue
      [ "$(sum "$kind" "$c.part")" = "$want" ] && { mv "$c.part" "$c"; break; }
      echo "oracle-src: $url: $kind mismatch" >&2
    done
    rm -f "$c.part"
    [ -f "$c" ] || return 1
  fi
  mkdir -p "$(dirname "$dest")"; cp "$c" "$dest"
}

rm -rf "$W" && mkdir -p "$W"
M=$W/mcp J=$W/mcp/jars

# MCP 9.08
if [ -n "$MCP_ZIP" ]; then
  [ "$(sum sha256 "$MCP_ZIP")" = $MCP_SHA256 ] || die "$MCP_ZIP: sha256 mismatch"
  cp "$MCP_ZIP" "$W/mcp908.zip"
elif ! fetch sha256 $MCP_SHA256 "$W/mcp908.zip" $MCP_URLS; then
  [ -n "$MCP_ARCHIVE" ] || die "mcp908.zip: no URL answered with sha256 $MCP_SHA256 (set MCP_ZIP or MCP_ARCHIVE)"
  zstd -dc "$MCP_ARCHIVE" | tar -xOf - mc-oracle/mcp908.zip > "$W/mcp908.zip"
  [ "$(sum sha256 "$W/mcp908.zip")" = $MCP_SHA256 ] || die "$MCP_ARCHIVE: mcp908.zip sha256 mismatch"
fi
unzip -q "$W/mcp908.zip" -d "$M"
step "MCP 9.08"

# the client: its version json, the jar, the libraries and natives MCP checks for
V=$J/versions/1.7.10
mkdir -p "$V"
if [ -n "$MC_HOME" ] && [ -f "$MC_HOME/versions/1.7.10/1.7.10.json" ]; then
  cp "$MC_HOME/versions/1.7.10/1.7.10.json" "$V/"
else
  mkdir -p "$CACHE"; [ -f "$CACHE/manifest.json" ] || curl -fsSL --retry 3 -o "$CACHE/manifest.json" $MANIFEST
  read -r vurl vsha < <(jq -r '.versions[] | select(.id == "1.7.10") | .url + " " + .sha1' "$CACHE/manifest.json")
  fetch sha1 "$vsha" "$V/1.7.10.json" "$vurl" || die "1.7.10.json: sha1 $vsha"
fi
read -r curl_ csha < <(jq -r '.downloads.client | .url + " " + .sha1' "$V/1.7.10.json")
if [ -n "$MC_HOME" ] && [ -f "$MC_HOME/versions/1.7.10/1.7.10.jar" ]; then
  cp "$MC_HOME/versions/1.7.10/1.7.10.jar" "$V/"
else
  fetch sha1 "$csha" "$V/1.7.10.jar" "$curl_" || die "client jar: sha1 $csha"
fi
[ "$(sum md5 "$V/1.7.10.jar")" = $JAR_MD5 ] || die "client jar md5 mismatch"
# every artifact and linux natives classifier the json names, as path sha1 url
jq -r '.libraries[] | .downloads | (.artifact // empty), (.classifiers["natives-linux"] // empty) | .path + " " + .sha1 + " " + .url' "$V/1.7.10.json" |
  while read -r p s u; do
    if [ -n "$MC_HOME" ] && [ -f "$MC_HOME/libraries/$p" ] && [ "$(sum sha1 "$MC_HOME/libraries/$p")" = "$s" ]; then
      mkdir -p "$(dirname "$J/libraries/$p")"; cp "$MC_HOME/libraries/$p" "$J/libraries/$p"
    else
      fetch sha1 "$s" "$J/libraries/$p" "$u" || die "$p: sha1 $s"
    fi
  done
mkdir -p "$V/1.7.10-natives"
find "$J/libraries" -name '*natives-linux*.jar' | LC_ALL=C sort | while read -r j; do unzip -oq "$j" -x 'META-INF/*' -d "$V/1.7.10-natives" 2>/dev/null || true; done
step "client, libraries, natives"

# MCP's decompile (fernflower, MCP's patches, renames, astyle). --norecompile:
# the recompile only checks the result, the source is the same.
# DOCKER_REBUILD=1: rebuild it without the layer cache (a cold run)
if [ -n "${DOCKER_REBUILD:-}" ] || ! docker image inspect "$IMAGE" >/dev/null 2>&1; then
  docker build -q ${DOCKER_REBUILD:+--no-cache} -t "$IMAGE" -f "$HERE/Dockerfile" "$HERE" > "$W/docker-build.log" 2>&1 \
    || { tail -20 "$W/docker-build.log" >&2; die "docker build failed"; }
fi
step "image $IMAGE"
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$M:/mcp" "$IMAGE" ./decompile.sh --client --norecompile > "$W/decompile.log" 2>&1 \
  || { tail -20 "$W/decompile.log" >&2; die "MCP decompile failed ($W/decompile.log)"; }
S=$M/src/minecraft
[ -f "$S/Start.java" ] || die "no decompiled source ($W/decompile.log)"
step "decompile ($(find "$S" -name '*.java' | wc -l) files)"

# flatten, determinize, patch
T=$W/src
mkdir -p "$T" && mv "$S"/net/minecraft/* "$T"/ && mv "$S/Start.java" "$T/"
[ -z "$(ls -A "$S/net/minecraft")" ] || die "unexpected files beside net/minecraft"
if [ -n "$PRISTINE" ]; then
  rm -rf "$OUT" && mv "$T" "$OUT" && rm -rf "$W"
  echo "oracle-src: pristine $OUT ($(find "$OUT" -type f | wc -l) files)"; exit 0
fi
bash "$HERE/determinize.sh" "$T"
step "determinize"
perl "$HERE/oraclepatch.pl" apply "$T" "$HERE/oracle.patch"
step "patch"
rm -rf "$OUT" && mv "$T" "$OUT" && rm -rf "$W"
echo "oracle-src: $OUT ($(find "$OUT" -type f | wc -l) files)"
