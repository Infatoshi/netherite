#!/usr/bin/env bash
# The public copy of the tree: HEAD's tracked files with a fresh history, and
# the checks a public push needs (lane/polish).
#
#   bash tools/export.sh [--mc DIR] [--no-build] [--cold] [DIR]   (default out/export)
#
# 1. DIR/repo: git archive HEAD, one commit, no other history or tag.
# 2. Checks, each one line, PASS or FAIL (rc 1 on any FAIL):
#    tracked    no java/src, no raw texture (*.rgba, *.rgb), no jar or class
#               file, no Python (git ls-files '*.py' is empty)
#    jar        no file whose bytes are an entry of the 1.7.10 client jar
#               (out/java/client/1.7.10.jar)
#    language   no tracked source literal equals an en_US.lang value of four
#               or more bytes, except exact counted and justified data names
#    decompiled no file holds decompiled Java text: no "package net.minecraft",
#               and no run of RUN (5) consecutive lines of 30 characters or more
#               (imports aside) that each occur in the pristine decompile
#               (oracle/decomp/src.sh pristine; the C engine's longest run is 4:
#               ported expressions, lane/oraclebuild's measurement)
#    private    no user path (/home/NAME, /Users/NAME), no private or VPN IPv4
#               address, no email but the commit trailers' noreply, no key or
#               token, no host name of ours used as a host (HOSTS below)
# 3. Unless --no-build: a clone of DIR/repo in DIR/clone builds with make,
#    runs netherite setup against the 1.7.10 install (--mc DIR, else MC_HOME,
#    else ~/.minecraft) and netherite gate --quick. --cold gives setup an empty
#    decompile cache (the Docker route a stranger takes); otherwise the cached
#    pristine decompile serves it.
set -uo pipefail
ROOT=$(cd "$(dirname "$0")/.." && pwd)
cd "$ROOT"
MC= BUILD=1 COLD= OUTD=
while [ $# -gt 0 ]; do
    case $1 in
        --mc) MC=$2; shift 2;;
        --no-build) BUILD=; shift;;
        --cold) COLD=1; shift;;
        -h|--help) sed -n '2,29p' "$0"; exit 0;;
        *) OUTD=$1; shift;;
    esac
done
OUTD=$(realpath -m "${OUTD:-out/export}")
HOSTS="anvil tetra"     # the machines' names: never as a host in the public tree
RUN=5
fail=0
ok()  { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }

[ -z "$(git status --porcelain --untracked-files=no)" ] || echo "export: note: uncommitted edits are not exported (HEAD is)"
rm -rf "$OUTD" && mkdir -p "$OUTD/repo"
git archive HEAD | tar -x -C "$OUTD/repo"
(cd "$OUTD/repo" && git init -q -b main && git add -A && \
  git -c user.name="${EXPORT_NAME:-$(git -C "$ROOT" config user.name)}" -c user.email="${EXPORT_EMAIL:-$(git -C "$ROOT" config user.email)}" \
    commit -q -m "netherite: Minecraft 1.7.10 in C and CUDA, exact against the Java client") || { echo "export: git init failed"; exit 1; }
R=$OUTD/repo
cd "$R"
n=$(git ls-files | wc -l | tr -d ' ')
echo "export: $R ($n files, $(du -sh --exclude=.git . | cut -f1), one commit $(git rev-parse --short HEAD))"

# tracked
t=$( { git ls-files 'java/src/*' 'native/assets/*' 'csrc/assets/*' 'oracle/src/*'; git ls-files '*.rgba' '*.rgb' '*.jar' '*.class' '*.py'; } | sort -u)
if [ -z "$t" ]; then ok "tracked: no java/src, raw texture, jar, class or Python file"
else bad "tracked: $(echo "$t" | wc -l | tr -d ' ') files, first $(echo "$t" | head -3 | tr '\n' ' ')"; fi

# jar
JAR=$ROOT/out/java/client/1.7.10.jar
if [ -f "$JAR" ]; then
    J=$OUTD/jar; mkdir -p "$J" && unzip -q -o "$JAR" -x 'META-INF/*' -d "$J"
    find "$J" -type f -size +64c -exec sha1sum {} + | cut -c1-40 | sort -u > "$OUTD/jar.sha1"
    git ls-files -z | xargs -0 sha1sum | sort > "$OUTD/tree.sha1"
    hit=$(join -o 2.2 "$OUTD/jar.sha1" <(sort -k1,1 "$OUTD/tree.sha1") | head -3)
    rm -rf "$J"
    if [ -z "$hit" ]; then ok "jar: no tracked file is a client jar entry ($(wc -l < "$OUTD/jar.sha1" | tr -d ' ') entries)"
    else bad "jar: tracked files equal to jar entries: $hit"; fi
else bad "jar: no client jar at $JAR to compare with (make -C oracle deps)"; fi

# language values are read from the user's jar, never tracked in this tree.
if [ -f "$JAR" ] && unzip -p "$JAR" assets/minecraft/lang/en_US.lang > "$OUTD/en_US.lang"; then
    if perl tools/langcheck.pl "$OUTD/en_US.lang" tools/lang_allow.tsv > "$OUTD/langcheck.log" 2>&1; then
        ok "language: $(tail -1 "$OUTD/langcheck.log")"
    else bad "language: $(head -2 "$OUTD/langcheck.log" | tr '\n' ' ' | cut -c1-180)"; fi
else bad "language: cannot read en_US.lang from the user jar"; fi

# decompiled
P=$(bash "$ROOT/oracle/decomp/src.sh" pristine 2>/dev/null)
if [ -d "$P" ]; then
    git ls-files > "$OUTD/files"
    out=$(perl -e '
      use strict; use File::Find;
      my ($P, $RUN) = @ARGV; my %v; my $MIN = 30;
      find({no_chdir => 1, wanted => sub { return unless /\.java$/; open my $h, "<", $_ or return;
        while (<$h>) { s/^\s+|\s+$//g; s/\s+/ /g; $v{$_} = 1 if length($_) >= $MIN && !/^(import|package) / } }}, $P);
      open my $L, "<", "'"$OUTD"'/files" or die; my ($worst, $wf, @bad) = (0, "");
      while (my $f = <$L>) { chomp $f; next unless -f $f && -T $f; open my $h, "<", $f or next; my ($run, $best) = (0, 0);
        while (<$h>) { push @bad, "$f: package net.minecraft" if /^\s*package net\.minecraft\b/;
          my $l = $_; $l =~ s/^\s+|\s+$//g; $l =~ s/\s+/ /g; next unless length $l;
          if (length($l) >= $MIN && $v{$l}) { $run++; $best = $run if $run > $best } else { $run = 0 } }
        ($worst, $wf) = ($best, $f) if $best > $worst; push @bad, "$f: $best consecutive decompiled lines" if $best >= $RUN }
      print scalar(@bad) ? "BAD " . join("; ", @bad[0 .. ($#bad < 4 ? $#bad : 4)]) : "OK longest run $worst ($wf)";
    ' "$P" "$RUN")
    case $out in OK*) ok "decompiled: no decompiled Java text (${out#OK }, fails at $RUN)";; *) bad "decompiled: ${out#BAD }";; esac
else bad "decompiled: no pristine decompile to compare with (bash oracle/decomp/src.sh pristine)"; fi

# private
pv=()
u=$(git grep -I -n -E '/home/[a-z][a-z0-9_-]*|/Users/[a-z][A-Za-z0-9_-]*' | head -3); [ -z "$u" ] || pv+=("user paths: $u")
ip=$(git grep -I -n -E '\b(10\.[0-9]{1,3}|192\.168|172\.(1[6-9]|2[0-9]|3[01])|100\.(6[4-9]|[7-9][0-9]|1[01][0-9]|12[0-7]))\.[0-9]{1,3}\.[0-9]{1,3}\b' | head -3); [ -z "$ip" ] || pv+=("private IPs: $ip")
em=$(git grep -I -n -E '[A-Za-z0-9._%+-]+@[A-Za-z0-9-]+(\.[A-Za-z0-9-]+)*\.[a-z]{2,}' | grep -v 'noreply@anthropic\.com' | head -3); [ -z "$em" ] || pv+=("emails: $em")
k=$(git grep -I -n -E -- '-----BEGIN [A-Z ]*PRIVATE KEY|\bsk-[A-Za-z0-9_-]{20,}|\bghp_[A-Za-z0-9]{30,}|\bgithub_pat_[A-Za-z0-9_]{30,}|\bAKIA[0-9A-Z]{16}\b|\bxox[abps]-[A-Za-z0-9-]{10,}|\bhf_[A-Za-z0-9]{30,}|\bAIza[0-9A-Za-z_-]{35}' | head -3); [ -z "$k" ] || pv+=("keys: $k")
for h in $HOSTS; do
    hh=$(git grep -I -n -E "(ssh|scp|rsync)[^|;]* $h\b|\b$h:[~/a-z]|\b(on|from|to|at) $h\b|\b$h's (3090|RTX|tree|merge|cores|load|nv2|checkout|master)|\b$h(-primary| host)\b" | grep -v -E "\b(an?|the|falling|landing) $h\b" | head -3)
    [ -z "$hh" ] || pv+=("host $h: $hh")
done
if [ ${#pv[@]} = 0 ]; then ok "private: no user path, private IP, email, key or host name"
else for x in "${pv[@]}"; do bad "private: $(echo "$x" | cut -c1-300)"; done; fi

# build, setup, quick gate from a clean clone
if [ -n "$BUILD" ]; then
    MCD=${MC:-${MC_HOME:-$HOME/.minecraft}}
    C=$OUTD/clone
    rm -rf "$C" && git clone -q "$R" "$C" || { bad "clone"; exit 1; }
    env_cold=()
    [ -z "$COLD" ] || env_cold=(CACHE="$OUTD/cache")
    if (cd "$C" && make -s > "$OUTD/make.log" 2>&1); then ok "build: make (out/bin/netherite)"; else bad "build: make ($OUTD/make.log)"; fi
    if (cd "$C" && env "${env_cold[@]}" out/bin/netherite setup --mc "$MCD" > "$OUTD/setup.log" 2>&1); then ok "setup: netherite setup --mc $MCD ($(tail -1 "$OUTD/setup.log" | cut -c1-80))"
    else bad "setup: netherite setup ($OUTD/setup.log: $(tail -1 "$OUTD/setup.log" | cut -c1-120))"; fi
    if (cd "$C" && out/bin/netherite gate --quick > "$OUTD/gate.log" 2>&1); then ok "gate: netherite gate --quick ($(grep -c '^netherite gate: PASS ' "$OUTD/gate.log") recordings, $(grep -c '^PASS' "$OUTD/gate.log") oracle gates)"
    else bad "gate: netherite gate --quick ($OUTD/gate.log: $(grep -E '^FAIL|FAIL' "$OUTD/gate.log" | head -2 | tr '\n' ' ' | cut -c1-160))"; fi
fi
echo "export: $([ $fail = 0 ] && echo PASS || echo FAIL) ($R)"
exit $fail
