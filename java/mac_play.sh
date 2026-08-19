#!/bin/bash
# Native macOS (Apple Silicon) launch of the Java 1.11.2 client + netheritemod.
# No gradle: ancient ForgeGradle cannot resolve offline from a foreign-machine
# cache (its metadata bins embed absolute paths), so we launch the JVM directly
# with the dev-runtime classpath recorded from anvil's working runClient
# (java/mac_classpath.txt, paths relative to java/Minecraft/).
#
# One-time bootstrap on a fresh Mac clone (see also docs/BOOTSTRAP.md):
#   1. rsync run/gradle/{caches,wrapper}, .gradle/minecraft/forgeSrc*.jar and
#      build/libs/MalmoMod-*.jar from anvil (LAN).
#   2. Patch the anvil-absolute paths baked into the generated GradleStart
#      classes:  cd run/gradle/caches/minecraft/net/minecraftforge/forge/*/start
#        find . -name '*.class' -print0 | xargs -0 \
#          <repo>/out/java/patch_class_paths /home/infatoshi /Users/infatoshi
#   3. arm64 natives into run/gradle/caches/minecraft/net/minecraft/natives/1.11.2:
#      liblwjgl.dylib + openal.dylib from MinecraftMachina lwjgl release
#      2.9.4-20150209-mmachina.2 (stock natives-osx crashes in
#      nGetCurrentDisplayMode on modern macOS); libjinput-osx.jnilib from
#      r58Playz/jinput-m1.
#   4. Zulu JDK 8 macOS aarch64 extracted at java/jdk8-macos-arm64/ (gitignored):
#      tarball zulu8.96.0.19-ca-jdk8.0.502-macosx_aarch64.tar.gz, so that
#      java/jdk8-macos-arm64/Contents/Home/bin/java exists.
# Must be run from a terminal in the local GUI session (over ssh CoreGraphics
# has no WindowServer connection and LWJGL crashes querying display modes).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
export JAVA_HOME="$ROOT/java/jdk8-macos-arm64/Contents/Home"
export PATH="$JAVA_HOME/bin:/usr/local/bin:/usr/bin:/bin:$HOME/.local/bin"
LOG=/tmp/mc_mac_launch.log
: > "$LOG"

[ -x "$JAVA_HOME/bin/java" ] || { echo "no JDK at $JAVA_HOME (see header)"; exit 1; }
NATIVES="$ROOT/java/Minecraft/run/gradle/caches/minecraft/net/minecraft/natives/1.11.2"
/usr/bin/file "$NATIVES/liblwjgl.dylib" 2>/dev/null | grep -q arm64 \
  || { echo "no arm64 liblwjgl.dylib in $NATIVES (see header step 3)"; exit 1; }

# Human-play profile: regenerate run/options.txt + run/qrl_launch.json from
# vanilla.yaml every launch (same contract as sunshine_launch_mc.sh on anvil).
cd "$ROOT/java"
uv run --no-project --with pyyaml python mc_cli.py --config vanilla.yaml --no-launch >> "$LOG" 2>&1

CP=""
missing=0
while IFS= read -r entry; do
  p="$ROOT/java/Minecraft/$entry"
  [ -e "$p" ] || { echo "MISSING classpath entry: $p" | tee -a "$LOG"; missing=1; }
  CP="$CP$p:"
done < "$ROOT/java/mac_classpath.txt"
[ "$missing" = 1 ] && { echo "ABORT: missing classpath entries (see $LOG)"; exit 1; }

cd "$ROOT/java/Minecraft/run"
exec java -Dmixin.debug=true -Xmx2G \
  -Dfile.encoding=UTF-8 -Duser.country=US -Duser.language=en \
  -cp "${CP%:}" GradleStart --username Player0 >> "$LOG" 2>&1
