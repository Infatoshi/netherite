#!/usr/bin/env bash
# Standalone build + verify for the REAL biome grass/foliage/water tint
# (world/light.c cr_*_color_biome, colormap-driven) against a verbatim-Minecraft
# golden (game/Golden.java). No game Makefile dependency beyond the canonical
# world-mesh source closure. Steps:
#   1. compile+run Golden.java (verbatim ColorizerGrass/Foliage over the real
#      256x256 colormap PNGs read from the client jar via ImageIO) -> golden lines
#   2. compile+run the C test in `dump` mode -> C-computed lines
#   3. diff (proves the baked table is current + the PIL/ImageIO decode paths and
#      the C index math all agree), then run the C asserts.
set -euo pipefail

MAGMA="$(cd "$(dirname "$0")/.." && pwd)"
JAVA_HOME_DIR="${JAVA_HOME_DIR:-/usr/lib/jvm/java-8-openjdk-amd64}"
JAR="${MC_JAR:-$(cd "$(dirname "$0")/../../.." && pwd)/java/Minecraft/run/gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar}"
cd "$MAGMA"
# shellcheck source=game/standalone_test_common.sh
source "$(dirname "$0")/standalone_test_common.sh"

magma_standalone_require_block_assets

# world_live.c is not needed; light pulls the world-mesh closure (incl. config.c).
SRCS=(
  game/test_biome_color.c
  "${MAGMA_WORLD_MESH_SRCS[@]}"
)

OUT="game/test_biome_color"
echo "== compiling C =="
magma_standalone_build "$OUT" "${SRCS[@]}"

if [ -f "$JAR" ] && [ -x "$JAVA_HOME_DIR/bin/javac" ]; then
    echo "== golden: verbatim-Java (ImageIO) =="
    ( cd game && "$JAVA_HOME_DIR/bin/javac" Golden.java )
    "$JAVA_HOME_DIR/bin/java" -cp game Golden "$JAR" > /tmp/biome_golden.txt
    "./$OUT" dump > /tmp/biome_c.txt
    echo "== diff C dump vs Java golden =="
    if ! diff -u /tmp/biome_golden.txt /tmp/biome_c.txt; then
        echo "GOLDEN DRIFT: C dump != verbatim-Java Golden.java output" >&2
        exit 1
    fi
    echo "OK: C colormap path == verbatim-Java golden (independent ImageIO decode)"
else
    echo "== SKIP live Golden.java (jar or JDK8 missing); asserting baked table only =="
fi

echo "== running C asserts =="
"./$OUT"
