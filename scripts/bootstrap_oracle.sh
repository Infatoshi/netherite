#!/bin/bash
# Regenerate java/oracle-src (the decompiled MC 1.11.2 + Forge reference tree)
# from YOUR OWN Minecraft installation via ForgeGradle. Mojang-derived source
# is never distributed with this repo; this script reproduces it locally,
# byte-identical, exactly as the committed C code expects (MCP names pinned by
# java/Minecraft/build.gradle).
#
# Requires: JDK 8 (JAVA_HOME or java-8 on PATH), network on first run
# (downloads MC 1.11.2 + mappings from Mojang/Forge maven into the repo-local
# gradle home java/Minecraft/run/gradle).
#
# Usage: bash scripts/bootstrap_oracle.sh
set -euo pipefail
REPO="$(cd "$(dirname "$0")/.." && pwd)"
MCW="$REPO/java/Minecraft"
SRC="$MCW/build/tmp/recompileMc/sources"
DST="$REPO/java/oracle-src"

if [ -z "${JAVA_HOME:-}" ]; then
    for j in /usr/lib/jvm/java-8-openjdk-amd64 \
             /usr/lib/jvm/temurin-8-jdk-amd64 \
             /Library/Java/JavaVirtualMachines/temurin-8.jdk/Contents/Home; do
        [ -d "$j" ] && export JAVA_HOME="$j" && break
    done
fi
[ -n "${JAVA_HOME:-}" ] || { echo "ERROR: JDK 8 not found; set JAVA_HOME"; exit 2; }
"$JAVA_HOME/bin/java" -version 2>&1 | grep -q '1\.8\.' \
    || { echo "ERROR: $JAVA_HOME is not a JDK 8"; exit 2; }

if [ ! -d "$SRC/net/minecraft" ]; then
    echo "== ForgeGradle setupDecompWorkspace (downloads + decompiles MC 1.11.2) =="
    ( cd "$MCW" && ./gradlew -g run/gradle setupDecompWorkspace --stacktrace )
fi
[ -d "$SRC/net/minecraft" ] || { echo "ERROR: decomp output missing at $SRC"; exit 1; }

echo "== populating java/oracle-src =="
mkdir -p "$DST"
rsync -a --delete "$SRC/net/" "$DST/net/"
n=$(find "$DST/net" -name '*.java' | wc -l)
echo "oracle-src ready: $n java files"
[ "$n" -ge 2600 ] || { echo "ERROR: expected >=2600 files, got $n"; exit 1; }
