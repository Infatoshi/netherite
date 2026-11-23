#!/usr/bin/env bash
# Provenance: regenerate the pristine MCP 9.08 decompile of the owned 1.7.10
# client jar into out/java/decomp/src/minecraft. oracle/src is the source of truth;
# its first commit (tag mcp-pristine) is exactly this output, so
#   diff -r out/java/decomp/src/minecraft <(git archive mcp-pristine oracle/src)
# should be empty, and `git diff mcp-pristine -- oracle/src` shows every change
# made to vanilla since. Needs Docker (MCP's scripts are python2).
# oracle-src.sh is the whole route from public downloads to oracle/src.
set -euo pipefail
cd "$(dirname "$0")/../.."
MC_HOME=${MC_HOME:-$HOME/.minecraft}
MCP_ARCHIVE=${MCP_ARCHIVE:-$HOME/dev/minecraft/legacy-cold/mc-oracle.tar.zst}
O=out/java/decomp
JAR=$MC_HOME/versions/1.7.10/1.7.10.jar
[ "$(md5sum "$JAR" | cut -d' ' -f1)" = e6b7a531b95d0c172acb704d1f54d1b3 ] || { echo "client jar md5 mismatch"; exit 1; }
rm -rf $O && mkdir -p $O
zstd -dc "$MCP_ARCHIVE" | tar -xOf - mc-oracle/mcp908.zip > $O/mcp908.zip
[ "$(sha256sum $O/mcp908.zip | cut -d' ' -f1)" = 23acbf233732c412db88d2975b3c85d020d02d5fdafbbdc9f2a6117ffd2184c6 ]
(cd $O && unzip -q mcp908.zip -d mcp)
mkdir -p $O/mcp/jars/versions/1.7.10
cp "$JAR" "$MC_HOME/versions/1.7.10/1.7.10.json" $O/mcp/jars/versions/1.7.10/
cp -r "$MC_HOME/libraries" $O/mcp/jars/
N=$O/mcp/jars/versions/1.7.10/1.7.10-natives; mkdir -p $N
for j in $(find $O/mcp/jars/libraries -name '*natives-linux*.jar'); do unzip -oq "$j" -x 'META-INF/*' -d $N || true; done
docker build -q -t netherite-v2-mcp -f oracle/decomp/Dockerfile oracle/decomp >/dev/null
docker run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$PWD/$O/mcp:/mcp" netherite-v2-mcp ./decompile.sh --client
mv $O/mcp/src/minecraft $O/src-minecraft && rm -rf $O/src && mkdir -p $O/src && mv $O/src-minecraft $O/src/minecraft
echo "pristine: $O/src/minecraft ($(find $O/src/minecraft -name '*.java' | wc -l) files)"
