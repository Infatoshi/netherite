#!/usr/bin/env bash
# The class store (oracle/Makefile's build, lane/trialcache): every full build's
# classes, kept under STORE by what they were compiled from, so a checkout
# whose sources were compiled before (a merge trial's fresh worktree, a lane
# after merging master, master after a merge the trial already built) unpacks
# them instead of running javac over 1,400 sources (30 to 125 s at load 150).
#   bash tests/classtore.sh get STORE JAVA8 LIBDIR CLASSES JAVACFLAGS...   (from oracle/)
#   bash tests/classtore.sh put STORE JAVA8 LIBDIR CLASSES JAVACFLAGS...
# An entry is named HARNESS-KEY: HARNESS is the harness id (tests/harness.sh's
# hash, computed here from the bytes, never from its stat cache) and KEY hashes
# every src/ and harness/ .java file's path and SHA-1, the compiler (javac's real
# path, tools.jar's size and mtime), javac's flags and the class path's jars,
# so the sources are named exactly and classes built from other sources, or
# by another compiler, are never served. get: rc 0 when CLASSES now holds the
# entry's classes (CLASSES is replaced), 1 on a miss. put: stores CLASSES
# (only a full build's, after javac returned 0) unless the entry exists.
# Least recently used entries past the newest KEEP (100, about 5 MB each) go.
# Linux only (the Makefile leaves it out on macOS).
set -uo pipefail
op=$1 store=$2 java8=$3 lib=$4 classes=$5; shift 5; flags=$*
KEEP=100
list=$(find src harness -name '*.java' -print0 | LC_ALL=C sort -z | xargs -0 sha1sum) || exit 1
h=$(find src harness -name '*.java' | LC_ALL=C sort | xargs cat | sha1sum); h=${h:0:12}
k=$({ printf '%s\n' "$list"; readlink -f "$java8/bin/javac"; stat -L -c '%s %Y' "$java8/lib/tools.jar" 2> /dev/null; \
  echo "$flags"; ls "$lib"; } | sha1sum); k=${k:0:16}
e=$store/$h-$k
case $op in
get)
  [ -f "$e/classes.tar.gz" ] || exit 1
  rm -rf "$classes" && mkdir -p "$classes" || exit 1
  if tar -xzf "$e/classes.tar.gz" -C "$classes" && [ -f "$classes/netherite/oracle/Main.class" ] \
      && [ -f "$classes/net/minecraft/client/Minecraft.class" ]; then
    touch "$e"
    echo "classes of harness $h from the class store ($(find "$classes" -name '*.class' | wc -l) classes, no javac)"
    exit 0
  fi
  rm -rf "$classes"; exit 1 ;;
put)
  [ -d "$e" ] && exit 0
  mkdir -p "$store" && t=$(mktemp -d "$store/.put.XXXXXX") || exit 1
  if tar -czf "$t/classes.tar.gz" -C "$classes" . && printf '%s\n' "$list" > "$t/sources.sha1" \
      && mv -T "$t" "$e" 2> /dev/null; then
    ls -dt "$store"/*-*/ 2> /dev/null | tail -n +$((KEEP + 1)) | xargs -r rm -rf
  fi
  rm -rf "$t"; exit 0 ;;
esac
exit 2
