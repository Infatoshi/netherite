#!/usr/bin/env bash
# Standalone build + verify for the REAL biome grass/foliage/water tint
# (world/light.c cr_*_color_biome, colormap-driven) against a verbatim-Minecraft
# golden (game/Golden.java). No game Makefile dependency beyond light.o + the
# generated colormap header. Steps:
#   1. compile+run Golden.java (verbatim ColorizerGrass/Foliage over the real
#      256x256 colormap PNGs read from the client jar via ImageIO) -> golden lines
#   2. compile+run the C test in `dump` mode -> C-computed lines
#   3. diff (proves the baked table is current + the PIL/ImageIO decode paths and
#      the C index math all agree), then run the C asserts.
set -euo pipefail

MAGMA="$(cd "$(dirname "$0")/.." && pwd)"
BLAZE="$(cd "$(dirname "$0")/../../blaze" && pwd)"
JAVA_HOME_DIR="${JAVA_HOME_DIR:-/usr/lib/jvm/java-8-openjdk-amd64}"
JAR="${MC_JAR:-$(cd "$(dirname "$0")/../.." && pwd)/java/Minecraft/run/gradle/caches/minecraft/net/minecraft/minecraft/1.11.2/minecraft-1.11.2.jar}"
cd "$MAGMA"

CC="${CC:-gcc}"
CFLAGS="-O2 -ffp-contract=off -Wall -Wextra"
INCLUDES="-I. -Icore -I${BLAZE}/core"

# Same world/light object closure test_world_live.sh uses (light.c pulls populate
# + the block-model/facebakery/math/shade/caps deps). world_live.c is not needed.
SRCS=(
  game/test_biome_color.c
  world/mesh_mc.c
  world/light.c
  world/populate_mc.c
  assets/blockmodels.c
  renderkernels/rk_31_facebakery_make_quad.c
  core/math.c
  core/shade.c
  game/caps.c
)

OUT="game/test_biome_color"
echo "== compiling C =="
$CC $CFLAGS $INCLUDES "${SRCS[@]}" -o "$OUT" -lm

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
