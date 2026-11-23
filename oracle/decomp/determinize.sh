#!/usr/bin/env bash
# The mechanical determinism pass over the decompiled vanilla source. It was run
# once, on the pristine tree, to make the commit after tag mcp-pristine; running
# it on pristine again reproduces that commit exactly.
#
#   new Random()          -> Det.newRandom()     per-thread-role seeder
#   static ... new Random -> Det.splitRandom(n)  shared statics, seeded by name
#   Math.random()         -> Det.mathRandom()    per-thread-role stream
#   UUID.randomUUID()     -> Det.uuid()          Entity and AttributeModifier only
#   Math.sin/cos/atan2/.. -> StrictMath.*        fdlibm: same bits on every JVM
#
# src/ was flattened after this pass ran (src/net/minecraft/block -> src/block;
# packages are unchanged). The static names still embed the original path,
# "./net/minecraft/item/Item.java:itemRand": they seed the streams, so they
# must not change. On a flattened pristine tree this script reproduces the same
# text. DIR (default ../src) is the tree to rewrite: oracle-src.sh and
# mkpatch.sh run it on a fresh flattened decompile.
#   bash determinize.sh [DIR]
set -euo pipefail
cd "${1:-$(dirname "$0")/../src}"
perl -pi -e 's/(static (?:final )?Random \w+ = )new Random\(\);/$1netherite.oracle.Det.splitRandom();/; s/\bnew Random\(\)/netherite.oracle.Det.newRandom()/g; s/\bMath\.random\(\)/netherite.oracle.Det.mathRandom()/g' \
  $(grep -rlE 'new Random\(\)|Math\.random\(\)' --include='*.java' . | LC_ALL=C sort)
perl -pi -e 's/UUID\.randomUUID\(\)/netherite.oracle.Det.uuid()/' ./entity/Entity.java ./entity/ai/attributes/AttributeModifier.java
perl -pi -e '($p = $ARGV) =~ s{^\./}{./net/minecraft/}; s/(Random (\w+) = netherite\.oracle\.Det\.splitRandom\()\)/$1"$p:$2")/' \
  $(grep -rl 'Det.splitRandom()' --include='*.java' . | LC_ALL=C sort)
perl -pi -e 's/(?<![\w.])Math\.(sin|cos|tan|asin|acos|atan2|atan|exp|log10|log|pow|cbrt|hypot)\(/StrictMath.$1(/g' \
  $(grep -rlE '\bMath\.(sin|cos|tan|asin|acos|atan|atan2|exp|log|log10|pow|cbrt|hypot)\(' --include='*.java' . | LC_ALL=C sort)
