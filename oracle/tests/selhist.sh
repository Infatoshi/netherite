#!/usr/bin/env bash
# The selective self-check's history proof, one oracle tree at a time
# (lane/selfselect): every recording of a list replayed fresh on the oracle
# of a past commit, with JaCoCo's agent on net.minecraft.* and netherite.*,
# so one run gives both the recording's outcome on that tree (the full
# self-check's line) and the methods it ran (the coverage a later merge's
# selection reads).
#
#   bash tests/selhist.sh COMMIT OUTDIR LIST [J]
#   bash tests/selhist.sh --fp COMMIT OUTDIR [nopatch]
#
# LIST: one recording directory per line (NAME is its basename, or
# "NAME DIR"). The tree is a detached worktree of COMMIT under
# $HOME/dev/nw/.tmp/selhist, its out/java linked to this tree's (libraries,
# assets, checkpoints). Trees older than lane/javacov have no --coverage,
# and a Makefile of any age can take the agent only through JAVA: so the
# tree gets SelCov.java (the agent's data written to -Dselcov.out at
# Oracle.finish's halt) and every replay runs `make replay CACHE=0 POOL=0
# JAVA=<a wrapper adding the agent>`, fresh, under this tree's
# tests/headless.sh (the memory budget). Writes OUTDIR/outcome.tsv (NAME,
# the selfcheck line), OUTDIR/exec/NAME.exec, OUTDIR/logs/NAME.log, then
# the analysis: OUTDIR/cov (recs.tsv and recs/NAME.methods.tsv, CovReport)
# OUTDIR/cov/fingerprints.tsv (SelfSelect fingerprint of the tree's classes)
# and OUTDIR/cov/inputs.tsv (tests/selinputs.sh of the tree). A finished OUTDIR (OUTDIR/done) is left as it is.
set -u
# the coverage hook: SelCov.dump() before Oracle.finish's halt
selcov() {
    cat <<'EOF'
package netherite.oracle;

/** selhist.sh's coverage hook: the JaCoCo agent's data to -Dselcov.out at the run's end. */
final class SelCov
{
    static void dump()
    {
        String p = System.getProperty("selcov.out");
        if (p == null) return;
        try
        {
            Object a = Class.forName("org.jacoco.agent.rt.RT").getMethod("getAgent").invoke(null);
            byte[] d = (byte[])Class.forName("org.jacoco.agent.rt.IAgent").getMethod("getExecutionData", boolean.class).invoke(a, false);
            java.nio.file.Files.write(java.nio.file.Paths.get(p), d);
        }
        catch (Throwable t) { System.out.println("SELCOV FAIL " + t); }
    }
}
EOF
}
# --fp COMMIT OUTDIR [nopatch]: only the fingerprints of COMMIT's classes (as
# the replays ran them, SelCov hook included unless nopatch), rebuilt in a
# worktree of their own, to OUTDIR/cov/fingerprints.tsv (a newer
# SelfSelect's format) and OUTDIR/cov/inputs.tsv (the current selinputs.sh)
if [ "$1" = --fp ]; then
    C=$2 OUTD=$3
    HERE=$(cd "$(dirname "$0")/.." && pwd); ROOT=$(cd "$HERE/.." && pwd)
    WT=$HOME/dev/nw/.tmp/selhist/fp-$C
    git -C "$ROOT" worktree remove --force "$WT" 2>/dev/null
    git -C "$ROOT" worktree add -q --detach "$WT" "$C" || exit 2
    mkdir -p "$WT/out/java" "$OUTD/cov"
    for d in lib natives client assets; do ln -s "$ROOT/out/java/$d" "$WT/out/java/$d"; done
    if [ "${4:-}" != nopatch ]; then
        selcov > "$WT/oracle/harness/netherite/oracle/SelCov.java"
        sed -i 's/^\( *\)Runtime.getRuntime().halt(rc);/\1SelCov.dump(); Runtime.getRuntime().halt(rc);/' "$WT/oracle/harness/netherite/oracle/Oracle.java"
    fi
    make -s -C "$WT/oracle" build > "$OUTD/fpbuild.log" 2>&1 || { echo "selhist --fp $C: build failed"; exit 2; }
    java -cp "$ROOT/out/java/covtool:$ROOT/out/java/jacoco-0.8.12/lib/jacococli.jar" SelfSelect fingerprint "$WT/out/java/classes" "$OUTD/cov/fingerprints.tsv" 2>&1 | grep -v "^Picked"
    bash "$HERE/tests/selinputs.sh" "$WT/oracle" > "$OUTD/cov/inputs.tsv"
    git -C "$ROOT" worktree remove --force "$WT"
    exit 0
fi
C=$1 OUTD=$2 LIST=$3 J=${4:-10}
HERE=$(cd "$(dirname "$0")/.." && pwd)
ROOT=$(cd "$HERE/.." && pwd)
[ -f "$OUTD/done" ] && { echo "selhist $C: done already ($OUTD)"; exit 0; }
mkdir -p "$OUTD/exec" "$OUTD/logs" "$OUTD/line"
OUTD=$(cd "$OUTD" && pwd)
WT=$HOME/dev/nw/.tmp/selhist/wt-$C
JAVA8=/usr/lib/jvm/java-8-openjdk-amd64/bin/java
AGENT=$ROOT/out/java/jacoco-0.8.12/lib/jacocoagent.jar
[ -f "$AGENT" ] || { echo "selhist: no JaCoCo agent at $AGENT (make -C oracle coverage fetches it)"; exit 2; }
if [ ! -d "$WT/oracle" ]; then
    mkdir -p "$(dirname "$WT")"
    git -C "$ROOT" worktree add -q --detach "$WT" "$C" || exit 2
fi
mkdir -p "$WT/out/java"
for d in lib natives client assets checkpoints jacoco-0.8.12; do
    [ -e "$WT/out/java/$d" ] || ln -s "$ROOT/out/java/$d" "$WT/out/java/$d"
done
# the coverage hook: SelCov.dump() before Oracle.finish's halt
O=$WT/oracle/harness/netherite/oracle
if [ ! -f "$O/SelCov.java" ]; then
    selcov > "$O/SelCov.java"
    n=$(grep -c '^ *Runtime.getRuntime().halt(rc);' "$O/Oracle.java")
    [ "$n" = 1 ] || { echo "selhist $C: Oracle.java has $n halt(rc) lines, expected 1"; exit 2; }
    sed -i 's/^\( *\)Runtime.getRuntime().halt(rc);/\1SelCov.dump(); Runtime.getRuntime().halt(rc);/' "$O/Oracle.java"
fi
cat > "$OUTD/java.sh" <<EOF
#!/bin/bash
exec $JAVA8 -javaagent:$AGENT=output=none,includes=net.minecraft.*:netherite.*,jmx=false -Dselcov.out=\$PWD/selcov.exec "\$@"
EOF
chmod +x "$OUTD/java.sh"
make -s -C "$WT/oracle" build > "$OUTD/build.log" 2>&1 || { echo "selhist $C: build failed"; tail -5 "$OUTD/build.log"; exit 2; }
export WT OUTD HERE
one() {
    n=$1 d=$2
    [ -f "$OUTD/line/$n" ] && return 0
    rn=$(basename "$d")-tape.replay
    rm -rf "$WT/out/java/run/$rn"
    s=$(date +%s%N)
    make -s --no-print-directory -C "$WT/oracle" replay REF="$d/tape.jsonl" CACHE=0 POOL=0 JAVA="$OUTD/java.sh" \
        XVFB="bash $HERE/tests/headless.sh 24" > "$OUTD/logs/$n.log" 2>&1; rc=$?
    e=$(date +%s%N); sec=$(( (e - s) / 1000000000 ))
    if grep -aq "^ORACLE REPLAY OK" "$OUTD/logs/$n.log" && [ $rc -eq 0 ]; then
        line="OK	$(grep -a "^ORACLE REPLAY OK" "$OUTD/logs/$n.log" | sed -E "s/.*ticks=([0-9]+).*/\1/")"
    elif grep -aq "^ORACLE DIVERGE" "$OUTD/logs/$n.log"; then
        line="DIVERGE	$(grep -a "^ORACLE DIVERGE" "$OUTD/logs/$n.log" | head -1 | cut -c16-)"
    else
        line="FAIL	rc=$rc $(grep -av "^\[" "$OUTD/logs/$n.log" | grep -v "^ORACLE QUEUE" | tail -1 | cut -c1-200)"
    fi
    [ -f "$WT/out/java/run/$rn/selcov.exec" ] && mv "$WT/out/java/run/$rn/selcov.exec" "$OUTD/exec/$n.exec"
    rm -rf "$WT/out/java/run/$rn"
    printf "%s\t%s\t%s\n" "$n" "$line" "$sec" > "$OUTD/line/$n"
}
export -f one
t0=$(date +%s)
awk 'NF == 1 { n = $1; sub(/\/$/, "", n); sub(/.*\//, "", n); print n, $1 } NF == 2 { print }' "$LIST" |
    xargs -P "$J" -L 1 bash -c 'one "$1" "$2"' _
awk 'NF == 1 { n = $1; sub(/\/$/, "", n); sub(/.*\//, "", n); print n, $1 } NF == 2 { print }' "$LIST" | while read -r n d; do
    cat "$OUTD/line/$n"
done > "$OUTD/outcome.tsv"
t1=$(date +%s)
echo "selhist $C: $(wc -l < "$OUTD/outcome.tsv") replays in $((t1 - t0)) s at J=$J: $(cut -f2 "$OUTD/outcome.tsv" | sort | uniq -c | tr '\n' ' ')"
# the analysis: the methods each recording ran, and the tree's fingerprints
mkdir -p "$OUTD/cov"
rm -rf "$OUTD/cov/exec"; ln -s "$OUTD/exec" "$OUTD/cov/exec"
awk -F'\t' -v OFS='\t' '{ st = $2; if ($2 != "OK") st = $2 " " $3; print $1, "-", st, ($2 == "OK" ? $3 : 0), $NF }' "$OUTD/outcome.tsv" > "$OUTD/cov/recs.tsv"
TOOLCP="$ROOT/out/java/covtool:$ROOT/out/java/jacoco-0.8.12/lib/jacococli.jar:$ROOT/out/java/lib/gson-2.2.4.jar"
$JAVA8 -Xmx6G -XX:+UseSerialGC -cp "$TOOLCP" CovReport "$WT/out/java/classes" "$ROOT/index" "$OUTD/cov" 16 > "$OUTD/cov/report.log" 2>&1 || { echo "selhist $C: CovReport failed"; tail -5 "$OUTD/cov/report.log"; exit 1; }
$JAVA8 -cp "$TOOLCP" SelfSelect fingerprint "$WT/out/java/classes" "$OUTD/cov/fingerprints.tsv" || { echo "selhist $C: fingerprint failed"; exit 1; }
rm -f "$OUTD"/cov/recs/*.lines.tsv "$OUTD/cov/merged.exec" "$OUTD/cov/lines.tsv" "$OUTD/cov/methods.tsv" "$OUTD/cov/unexecuted.tsv"
gzip -f "$OUTD"/cov/recs/*.methods.tsv
bash "$HERE/tests/selinputs.sh" "$WT/oracle" > "$OUTD/cov/inputs.tsv"
rm -rf "$OUTD/exec" "$OUTD/cov/exec" "$OUTD/line"
git -C "$ROOT" worktree remove --force "$WT"
touch "$OUTD/done"
echo "selhist $C: analysed in $(( $(date +%s) - t1 )) s ($OUTD)"
