#!/bin/bash
# divergences.sh [--out FILE] [--sweep DIR] [--ledger DIR]: render divergences.md, the ledger of every
# known difference between the native engine and the Java oracle (vanilla
# 1.7.10 plus SPEC.md's pins and config.yaml's switches), what is known and
# not looked at, and what bounds the rest. Generated, never hand-edited; the
# merge queue regenerates it on master after every merge
# (out/flywheel/bin/divgen-post.sh).
#
# Read, in this tree:
#   csrc/tests/divergences/*.tsv   the ledger's rows, one file per lane
#                                    (header: id found by area field cause status commit pin repro)
#   csrc/tests/divergences_rounds.tsv  fuzz rounds before the sweep
#   csrc/tests/divergences_gaps.tsv    what the generators never do (probes)
#   SPEC.md (Pins), config.yaml and oracle/harness/netherite/oracle/WorldConf.java,
#   csrc/tests/{frame,rawjudge,render}_budget.txt, AGENTS.md ("Not judged"),
#   csrc/play/play.c and csrc/engine (what the playable client lacks)
# and in the main tree's out/ (master's, the canonical checkout):
#   out/sweep/causes.tsv, rate.tsv      the sweep's clusters and find rate
#   out/java/coverage/summary.txt, methods.tsv   make -C oracle coverage
#   out/native/coverage/summary.txt     make -C csrc coverage (this tree's first)
#
# A ledger row (tab-separated, "#" lines are comments, the header first):
#   id      a unique slug: the pin recording's name when there is one
#           (pfc-ridechunk-g23, rf-s1-1436), else LANE-shortcause
#   found   the day it was found, YYYY-MM-DD
#   by      the lane, and the finder in parentheses where it applies:
#           lane/chainfuzz3 (chainfuzz); rlfuzz, playfuzz, rawfuzz, sweep,
#           human session, audit, coverage, index, frame judge, gate
#   area    one of the words in AREAS below: Java -> C (the C engine,
#           csrc/engine, against the Java oracle) takes the first 22; C -> CUDA
#           (a device kernel in csrc/cuda, or the pool's result from
#           csrc/runtime, against the C engine) takes cuda
#   field   the first differing field as the checker named it (d.ents,
#           cp->x, w.bc, d.cseed, px), or -
#   cause   one line: what was different and why
#   status  fixed (the fix is on master and a recording or test pins it),
#           open (seen, not fixed), accepted (a documented difference: a pin,
#           a switch, a frame budget), superseded (a later change made it
#           moot; the cause says which)
#   commit  the fix's short hash on master, or -
#   pin     what proves it: out/java/snapshots/NAME, clientframes/NAME,
#           rawjudge/NAME, a test name, or -
#   repro   for an open row, the command or directory that shows it; else -
# One row per cause: several sessions with one cause are one row, the extra
# pins in the cause. Never invent a hash or a recording; write - instead.
#
# Rows with the same id merge: the most advanced status wins (fixed, then
# superseded, then accepted, then open; a tie goes to the file that sorts
# last), its "-" fields are filled from the other rows, the found date is the
# earliest and every pin is kept. Everything is sorted, and no clock is read,
# so a run on unchanged inputs writes the same bytes.
set -euo pipefail
export LC_ALL=C
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
MAIN=$(git -C "$ROOT" worktree list --porcelain 2>/dev/null | awk '/^worktree /{print substr($0, 10); exit}')
MAIN=${MAIN:-$ROOT}
OUT=$ROOT/divergences.md; SWEEP_ARG=; LEDGER_ARG=
while [ $# -gt 0 ]; do
    case $1 in
        --out) OUT=$2; shift 2;;
        --sweep) SWEEP_ARG=$2; shift 2;;
        --ledger) LEDGER_ARG=$2; shift 2;;
        *) echo "usage: divergences.sh [--out FILE] [--sweep DIR] [--ledger DIR]" >&2; exit 2;;
    esac
done
T=${TMPDIR:-$HOME/dev/nw/.tmp}; mkdir -p "$T"
T=$(mktemp -d "$T/divgen.XXXXXX"); trap 'rm -rf "$T"' EXIT

LEDGER=${LEDGER_ARG:-$ROOT/csrc/tests/divergences}
ROUNDS=$ROOT/csrc/tests/divergences_rounds.tsv
GAPS=$ROOT/csrc/tests/divergences_gaps.tsv
SWEEP=${SWEEP_ARG:-$MAIN/out/sweep}
JCOV=$MAIN/out/java/coverage
NCOV=
for d in "$ROOT/out/native/coverage" "$MAIN/out/native/coverage"; do
    [ -f "$d/summary.txt" ] && { NCOV=$d; break; }
done
AREAS="worldgen light blocks liquids mob-ai mob-spawn mob-other physics items containers combat survival dimension-travel save-reload client-sync client-state render input gui network oracle other cuda"
rel() { case $1 in "$ROOT"/*) echo "${1#"$ROOT"/}";; "$MAIN"/*) echo "master's ${1#"$MAIN"/}";; *) echo "$1";; esac; }

# ------------------------------------------------------------------ the ledger
# merged.tsv: id found by area field cause status commit pin repro files
# history first (lanes divhista..c backfilled it), then the lanes' own files: a status tie goes
# to the file read last, so a lane's row outranks the history it re-checks
files=$( (ls "$LEDGER"/hist-*.tsv 2>/dev/null | sort; ls "$LEDGER"/*.tsv 2>/dev/null | grep -v "/hist-[^/]*\.tsv$" | sort) || true)
: > "$T/merged.tsv"; : > "$T/problems"
if [ -n "$files" ]; then
    # shellcheck disable=SC2086
    awk -F'\t' -v OFS='\t' -v areas="$AREAS" -v probs="$T/problems" '
    BEGIN {
        split(areas, a, " "); for (i in a) okarea[a[i]] = 1
        rank["open"] = 1; rank["accepted"] = 2; rank["superseded"] = 3; rank["fixed"] = 4
        H = "id\tfound\tby\tarea\tfield\tcause\tstatus\tcommit\tpin\trepro"
    }
    FNR == 1 { f = FILENAME; sub(/.*\//, "", f); head = 0 }
    /^#/ || /^[ \t]*$/ { next }
    !head { head = 1; if ($0 != H) print f ": the first line is not the header" > probs; next }
    {
        if (NF != 10) { print f ":" FNR ": " NF " fields, not 10 (" $1 ")" > probs; next }
        if (!($7 in rank)) { print f ":" FNR ": " $1 ": status \"" $7 "\" is not fixed, open, accepted or superseded" > probs; next }
        if (!($4 in okarea)) print f ":" FNR ": " $1 ": area \"" $4 "\" is not in the list" > probs
        if ($2 !~ /^20[0-9][0-9]-[01][0-9]-[0-3][0-9]$/ && $2 != "-") print f ":" FNR ": " $1 ": found \"" $2 "\" is not YYYY-MM-DD" > probs
        if ($7 == "fixed" && ($8 == "-" || $9 == "-")) print f ":" FNR ": " $1 ": fixed without a commit or a pin" > probs
        if ($7 == "open" && $10 == "-") print f ":" FNR ": " $1 ": open without a repro" > probs
        id = $1
        if (!(id in n)) { order[++ids] = id }
        k = ++n[id]
        for (i = 1; i <= 10; i++) R[id, k, i] = $i
        R[id, k, 11] = f
    }
    function fill(id, w, i,   k) {
        if (R[id, w, i] != "-" && R[id, w, i] != "") return R[id, w, i]
        for (k = 1; k <= n[id]; k++) if (R[id, k, i] != "-" && R[id, k, i] != "") return R[id, k, i]
        return "-"
    }
    END {
        for (j = 1; j <= ids; j++) {
            id = order[j]; w = 1
            for (k = 2; k <= n[id]; k++) if (rank[R[id, k, 7]] >= rank[R[id, w, 7]]) w = k
            found = "-"; pins = ""; fl = ""; delete seen; delete seenf
            for (k = 1; k <= n[id]; k++) {
                d = R[id, k, 2]
                if (d != "-" && (found == "-" || d < found)) found = d
                m = split(R[id, k, 9], p, /[ ,;]+/)
                for (q = 1; q <= m; q++) if (p[q] != "" && p[q] != "-" && !(p[q] in seen)) { seen[p[q]] = 1; pins = pins (pins == "" ? "" : ", ") p[q] }
                if (!(R[id, k, 11] in seenf)) { seenf[R[id, k, 11]] = 1; fl = fl (fl == "" ? "" : ",") R[id, k, 11] }
            }
            st = R[id, w, 7]
            print id, found, fill(id, w, 3), fill(id, w, 4), fill(id, w, 5), fill(id, w, 6), st,
                  (st == "open" ? R[id, w, 8] : fill(id, w, 8)), (pins == "" ? "-" : pins),
                  (st == "open" ? fill(id, w, 10) : "-"), fl
        }
    }' $files > "$T/merged.tsv"
fi
sort -t$'\t' -k1,1 "$T/merged.tsv" -o "$T/merged.tsv"
# the two bridges: Java -> C (every other area) and C -> CUDA (area cuda)
awk -F'\t' '$4 != "cuda"' "$T/merged.tsv" > "$T/b1.tsv"
awk -F'\t' '$4 == "cuda"' "$T/merged.tsv" > "$T/b2.tsv"
sort -t: -k1,1 -k2,2n "$T/problems" -o "$T/problems"

# ------------------------------------------------------------------ the sweep
: > "$T/clusters"; : > "$T/sweeprate"
if [ -f "$SWEEP/causes.tsv" ]; then
    awk -F'\t' 'NR > 1 && NF >= 13' "$SWEEP/causes.tsv" | sort -t$'\t' -k1,1 > "$T/clusters"
fi
if [ -f "$SWEEP/rate.tsv" ]; then
    awk -F'\t' 'NR > 1 && $4 == "all"' "$SWEEP/rate.tsv" | sort -t$'\t' -k1,1n > "$T/sweeprate"
fi

# ------------------------------------------------------------------ the commit
# the last commit that changed anything but this file: a regenerate commit
# does not move it, so a second run writes the same bytes
commit=$(git -C "$ROOT" log -1 --format=%h -- . ':(exclude)divergences.md' 2>/dev/null || echo -)
cdate=$(TZ=America/Edmonton git -C "$ROOT" log -1 --date=format-local:'%Y-%m-%d %-I:%M %p' --format=%cd -- . ':(exclude)divergences.md' 2>/dev/null | tr AP ap | sed 's/M$/m/' || echo -)

{
# ================================================================== 1. summary
cat <<EOF
# Divergences: the C engine against the Java oracle

The C engine (csrc/engine, its playable client out/native/play) is checked
row by row against the Java oracle (vanilla 1.7.10 plus SPEC.md's pins and
config.yaml's switches). Sections 1 to 6 are that bridge, Java -> C. The
second bridge, C -> CUDA (a device kernel in csrc/cuda, or the result of
the pool in csrc/runtime, against the C engine), is section 7.

Generated by \`csrc/tests/divergences.sh\` from the ledger in
\`csrc/tests/divergences/*.tsv\` and the live sources named in each section;
never edit it by hand. A lane that finds or fixes a divergence adds its row to
\`csrc/tests/divergences/LANE.tsv\`; the merge regenerates this file.

As of commit $commit ($cdate MT).

## 1. Summary

EOF
awk -F'\t' -v areas="$AREAS" -v nprob="$(wc -l < "$T/problems")" '
{ c[$7]++; a[$4, $7]++; t[$4]++; all++ }
END {
    printf "Java -> C: %d divergences in the ledger, %d fixed, %d open, %d accepted, %d superseded.\n",
        all, c["fixed"], c["open"], c["accepted"], c["superseded"]
    if (nprob > 0) printf "%d ledger lines have problems (listed at the end).\n", nprob
    print ""
    if (all == 0) exit
    print "| area | fixed | open | accepted | superseded |"
    print "|---|---:|---:|---:|---:|"
    n = split(areas, A, " ")
    for (i = 1; i <= n; i++) if (t[A[i]]) printf "| %s | %d | %d | %d | %d |\n", A[i], a[A[i], "fixed"], a[A[i], "open"], a[A[i], "accepted"], a[A[i], "superseded"]
    for (k in t) { known = 0; for (i = 1; i <= n; i++) if (A[i] == k) known = 1; if (!known) extra[k] = 1 }
    m = 0; for (k in extra) E[++m] = k
    for (i = 1; i <= m; i++) for (j = i + 1; j <= m; j++) if (E[j] < E[i]) { x = E[i]; E[i] = E[j]; E[j] = x }
    for (i = 1; i <= m; i++) printf "| %s | %d | %d | %d | %d |\n", E[i], a[E[i], "fixed"], a[E[i], "open"], a[E[i], "accepted"], a[E[i], "superseded"]
    printf "| all | %d | %d | %d | %d |\n", c["fixed"], c["open"], c["accepted"], c["superseded"]
    print ""
}' "$T/b1.tsv"
awk -F'\t' '{ c[$7]++; n++ } END { printf "C -> CUDA (section 7): %d %s, %d open, %d fixed.\n\n", n, (n == 1 ? "row" : "rows"), c["open"], c["fixed"] }' "$T/b2.tsv"

if [ -s "$T/clusters" ]; then
    awk -F'\t' '{ s = $2; sub(/ .*/, "", s); c[s]++; n++ }
    END { printf "The sweep (master'"'"'s out/sweep/causes.tsv) holds %d clusters: %d open, %d assigned, %d fixed.\n\n", n, c["open"], c["assigned"], c["fixed"] }' "$T/clusters"
fi

# sessions and rows the fuzzers checked
awk -F'\t' -v sweep="$T/sweeprate" '
function c(x,   s) { s = sprintf("%d", x); while (s ~ /[0-9][0-9][0-9][0-9]/) sub(/[0-9][0-9][0-9]$|[0-9][0-9][0-9],/, ",&", s); return s }
/^#/ || $1 == "date" { next }
{ r++; if ($5 != "-") { s += $5; sr++ } if ($6 != "-") { w += $6 } if ($8 != "-") nw += $8 }
END {
    while ((getline l < sweep) > 0) { split(l, f, "\t"); ss += f[5]; sw += f[6]; sn += f[9]; rr++ }
    printf "The fuzzers have checked %s sessions and %s rows (the sweep %s sessions, %s rows in %d round%s; before it, %s sessions in the %d of %d fuzz rounds whose lane counted them). ", c(s + ss), c(w + sw), c(ss), c(sw), rr, (rr == 1 ? "" : "s"), c(s), sr, r
    printf "They found %d new causes in those rounds, and the sweep %d new clusters.\n\n", nw, sn
}' "$ROUNDS"

# the find rate: each fuzzer's last rounds with a known rate, then the sweep
awk -F'\t' -v sweep="$T/sweeprate" '
/^#/ || $1 == "date" { next }
$5 != "-" && $8 != "-" && $5 > 0 { k = $3; n[k]++; L[k, n[k]] = sprintf("%.1f", 1000 * $8 / $5) }
END {
    printf "New causes per 1,000 sessions, the last rounds before the sweep (oldest first):"
    m = 0; for (k in n) K[++m] = k
    for (i = 1; i <= m; i++) for (j = i + 1; j <= m; j++) if (K[j] < K[i]) { x = K[i]; K[i] = K[j]; K[j] = x }
    for (i = 1; i <= m; i++) {
        k = K[i]; s = ""
        for (q = (n[k] > 4 ? n[k] - 3 : 1); q <= n[k]; q++) s = s (s == "" ? "" : ", ") L[k, q]
        printf "%s %s %s", (i == 1 ? "" : ";"), k, s
    }
    printf ".\n"
    q = 0
    while ((getline l < sweep) > 0) { split(l, f, "\t"); if (f[5] > 0) S[++q] = sprintf("%.1f", 1000 * f[9] / f[5]) }
    if (q) { s = ""; for (i = (q > 6 ? q - 5 : 1); i <= q; i++) s = s (s == "" ? "" : ", ") S[i]; printf "The sweep, its last rounds: %s.\n", s }
    else print "The sweep has no finished round yet (master'"'"'s out/sweep/rate.tsv)."
    print ""
}' "$ROUNDS"

# ================================================================== 2. fixed
cat <<'EOF'
## 2. Known and fixed (known knowns)

Every fixed divergence between the Java oracle and the C engine, grouped by area, newest
first, with the commit that fixed it on master and what pins it (a recording under out/java: snapshots,
clientframes or rawjudge; or a test).

EOF
row() {  # row STATUS MODE [FILE]: the rows of one status, by area, newest first
    awk -F'\t' -v st="$1" -v areas="$AREAS" -v mode="$2" '
    function esc(s) { gsub(/\|/, "\\|", s); return s }
    $7 == st { k = ++n[$4]; R[$4, k] = $0; seen[$4] = 1 }
    END {
        na = split(areas, A, " ")
        for (k in seen) { known = 0; for (i = 1; i <= na; i++) if (A[i] == k) known = 1; if (!known) A[++na] = k }
        for (i = 1; i <= na; i++) {
            a = A[i]; if (!n[a]) continue
            # newest first; the id breaks ties
            for (p = 1; p <= n[a]; p++) for (q = p + 1; q <= n[a]; q++) {
                split(R[a, p], x, "\t"); split(R[a, q], y, "\t")
                if (y[2] > x[2] || (y[2] == x[2] && y[1] < x[1])) { t = R[a, p]; R[a, p] = R[a, q]; R[a, q] = t }
            }
            printf "### %s (%d)\n\n", a, n[a]
            for (p = 1; p <= n[a]; p++) {
                split(R[a, p], f, "\t")
                fld = (f[5] == "-" ? "" : ", field `" f[5] "`")
                c = esc(f[6]); if (c !~ /[.!?]$/) c = c "."
                if (mode == "fixed") printf "- **%s** (%s, %s%s): %s Fix %s, pin %s.\n", f[1], f[2], f[3], fld, c, (f[8] == "-" ? "-" : "`" f[8] "`"), f[9]
                else if (mode == "open") printf "- **%s** (%s, %s%s): %s Repro: `%s`.\n", f[1], f[2], f[3], fld, c, f[10]
                else printf "- **%s** (%s, %s%s): %s%s%s\n", f[1], f[2], f[3], fld, c, (f[8] == "-" ? "" : " Commit `" f[8] "`."), (f[9] == "-" ? "" : " Pin " f[9] ".")
            }
            print ""
        }
    }' "${3:-$T/b1.tsv}"
}
if grep -q $'\tfixed\t' "$T/b1.tsv"; then row fixed fixed; else echo "None in the ledger yet."; echo; fi

# ================================================================== 3. open
cat <<'EOF'
## 3. Known and open (known unknowns, seen)

Every divergence between the Java oracle and the C engine seen and not fixed: the
ledger's open rows, then the sweep's
open and assigned clusters (master's out/sweep/causes.tsv), each with the
command or directory that shows it.

EOF
if grep -q $'\topen\t' "$T/b1.tsv"; then row open open; else echo "No open row in the ledger."; echo; fi
if [ -s "$T/clusters" ]; then
    echo "### The sweep's open clusters"
    echo
    awk -F'\t' '$2 == "open" || $2 ~ /^assigned/ {
        gsub(/\/[^ ]*\/out\//, "out/", $13)   # the repro, from the checkout (no host path)
        printf "- **sweep %s** (%s; first round %s, %s; last round %s; %s sessions; %s, dimension %s): field `%s`, class %s, path %s%s. Repro: `%s`.%s\n",
            $1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, ($12 == "-" ? "" : ", at " $12), $13, ($14 == "-" || $14 == "" ? "" : " Note: " $14 ".")
        n++ }
    END { if (!n) print "None: every cluster is fixed." }' "$T/clusters"
    echo
else
    echo "The sweep has written no clusters yet (master's out/sweep/causes.tsv)."
    echo
fi

# ================================================================== 4. accepted
cat <<'EOF'
## 4. Accepted differences (by design)

What the oracle or the native engine does differently on purpose, read live
from where each is documented.

### The oracle's pins (SPEC.md, "Pins")

Each pin fixes a vanilla nondeterminism (a race between threads, the wall
clock, a shared stream) the same way in the oracle and the native engine.

EOF
awk '
/^\*\*Pins\.\*\*/ { on = 1 }
on && /^[ \t]*$/ { on = 0 }
on && /^\*\*/ && !/^\*\*Pins\.\*\*/ { on = 0 }
on { sub(/^\*\*Pins\.\*\* */, ""); txt = txt " " $0 }
END {
    s = txt
    while (match(s, /The [A-Za-z -]+ pin \(/)) {
        pos[++n] = RSTART; s2 = substr(s, RSTART + RLENGTH); s = sprintf("%" (RSTART + RLENGTH - 1) "s", "") s2
    }
    for (i = 1; i <= n; i++) {
        e = (i < n ? pos[i + 1] : length(txt) + 1)
        p = substr(txt, pos[i], e - pos[i]); sub(/ +$/, "", p)
        match(p, /^The [A-Za-z -]+ pin/); name = substr(p, 5, RLENGTH - 8)
        # the first sentence: to the first ". " before a capital, past the name
        body = p; first = body
        if (match(body, /[a-z0-9)`'"'"'"]\. [A-Z]/)) first = substr(body, 1, RSTART + 1)
        sub(/^The [A-Za-z -]+ pin /, "", first)
        printf "- **%s pin** %s\n", name, first
    }
    if (!n) print "(SPEC.md has no Pins paragraph.)"
}' "$ROOT/SPEC.md"
echo
if grep -q $'\taccepted\t' "$T/b1.tsv"; then
    echo "### Accepted rows in the ledger"
    echo
    row accepted accepted | sed 's/^### /#### /'
fi

cat <<'EOF'
### Switched-off features (config.yaml)

A feature the native engine lacks is off in every world, in the oracle too
(WorldConf.UNSUPPORTED), so both play the same game; it leaves the list when
the native engine has it.

EOF
unsup=$(sed -n '/UNSUPPORTED = {/,/};/p' "$ROOT/oracle/harness/netherite/oracle/WorldConf.java" | grep -o '"[a-z_]*"' | tr -d '"' | sort)
awk -v unsup="$(echo $unsup)" '
BEGIN { n = split(unsup, u, " "); for (i = 1; i <= n; i++) { U[u[i]] = 1; K[u[i] ":"] = 1 } }
/^world:/ { w = 1; next }
/^[a-z]/ { w = 0 }
w && /^  [a-z_]+: +off/ && ($1 in K) {
    k = substr($1, 1, length($1) - 1); c = $0; sub(/^[^#]*#? */, "", c); if (c == $0) c = ""
    printf "- `%s`%s\n", k, (c == "" ? "" : ": " c)
    off[k] = 1
}
END { for (i = 1; i <= n; i++) if (!(u[i] in off)) printf "- `%s`: in WorldConf.UNSUPPORTED but not off in config.yaml\n", u[i] }' "$ROOT/config.yaml"
echo
if [ -f "$JCOV/methods.tsv" ]; then
    awk -F'\t' '!/^#/ { n[$6]++; if ($7 + 0 > 0) r[$6]++ }
    END { printf "Out of scope by design in the Java coverage'"'"'s scope column (master'"'"'s out/java/coverage/methods.tsv): %d switched-off methods (%d of them run), %d client-cosmetic, %d not needed; %d methods in scope.\n\n",
        n["switched_off"], r["switched_off"], n["client_cosmetic"], n["not_needed"], n["in_scope"] }' "$JCOV/methods.tsv"
fi

cat <<'EOF'
### Frames judged within a pixel budget

A budget line accepts a frame whose difference from the oracle's frame was
measured and named when it was accepted (a ratchet: a fix lowers it); every
judged frame has a line (frame_judge fails a frame without one). Per file (frame_budget: the
clientframes judges; render_budget: test_render's scenes; rawjudge_budget:
the Java-judged sessions) and recording family (the name's first word):
frames with a budget, those over 1,000 pixels off, the most pixels over 25 in
one frame, and the highest mean channel error.

| file | family | frames | over 1,000 px | worst pixels | worst mean |
|---|---|---:|---:|---|---|
EOF
for f in frame_budget render_budget rawjudge_budget; do
    p=$ROOT/csrc/tests/$f.txt; [ -f "$p" ] || continue
    awk -v file="$f.txt" '
    /^#/ || NF < 5 { next }
    {
        # frame: NAME TICK PT R G B N [drawn]; rawjudge: NAME TICK R G B N; render: SCENE R G B N
        if (file == "rawjudge_budget.txt" && NF >= 6) { r = $3; g = $4; b = $5; px = $6; at = $1 " " $2 }
        else if (file == "render_budget.txt" && NF == 5 && $5 ~ /^[0-9]+$/) { r = $2; g = $3; b = $4; px = $5; at = $1 }
        else if (file == "frame_budget.txt" && NF >= 7 && $3 ~ /^[0-9.]+$/ && $7 ~ /^[0-9]+$/) { r = $4; g = $5; b = $6; px = $7; at = $1 " " $2 " pt " $3 }
        else next
        fam = $1; sub(/-.*/, "", fam); if (file == "render_budget.txt") fam = "scenes"
        n[fam]++; tot++
        mean = r; if (g > mean) mean = g; if (b > mean) mean = b
        if (px + 0 > 1000) { big[fam]++; tbig++ }
        if (!(fam in wp) || px + 0 > wp[fam]) { wp[fam] = px + 0; wpa[fam] = at }
        if (!(fam in wm) || mean + 0 > wm[fam]) { wm[fam] = mean + 0; wma[fam] = at }
    }
    END {
        m = 0; for (k in n) K[++m] = k
        for (i = 1; i <= m; i++) for (j = i + 1; j <= m; j++) if (K[j] < K[i]) { x = K[i]; K[i] = K[j]; K[j] = x }
        for (i = 1; i <= m; i++) { k = K[i]
            printf "| %s | %s | %d | %d | %d (%s) | %.3f (%s) |\n", file, k, n[k], big[k], wp[k], wpa[k], wm[k], wma[k] }
        printf "| %s | all | %d | %d | | |\n", file, tot, tbig
    }' "$p"
done
echo

echo "### Raw input the judge does not compare (AGENTS.md)"
echo
awk '
/^Not judged:/ { on = 1 }
on { txt = txt (txt == "" ? "" : " ") $0; if (match(txt, /\. [`A-Z]/)) { print substr(txt, 1, RSTART); exit } if ($0 ~ /\.$/) { print txt; exit } }' "$ROOT/AGENTS.md"
echo

echo "### What the playable client (csrc/play) lacks"
echo
echo "Each line is a search of the native tree that finds nothing; it drops out when the client gains the part."
echo
probe() {  # probe TEXT REGEX FILE...: print TEXT when no FILE matches REGEX
    local text=$1 re=$2; shift 2
    grep -Eqs -- "$re" "$@" || echo "- $text"
}
probe "No sound output: csrc/play/play.c opens no audio device (the sound calls' random draws are kept; nothing is played)." 'SDL_OpenAudio|SDL_OpenAudioDevice|SDL_AudioStream|Mix_' "$ROOT/csrc/play/play.c"
probe "No player list (Tab): play.c binds no key to it." 'K_PLAYERLIST|playerlist|player_list' "$ROOT/csrc/play/play.c"
probe "The pause menu (Escape) draws only Back to Game: no Options, Achievements, Statistics, Open to LAN, or Save and Quit (closing the window ends the session)." 'Save and Quit|"Options\.\.\."' "$ROOT/csrc/engine/raster_hud.c" "$ROOT/csrc/play/play.c"
probe "No title, options, video settings or controls screens: the player's keys, fov, gamma and mouse speed come from --options (an options.txt)." 'GuiMainMenu|GuiOptions|GuiVideoSettings|GuiControls' "$ROOT"/csrc/engine/*.c "$ROOT/csrc/play/play.c"
probe "No screenshot key (F2)." 'SDLK_F2\b' "$ROOT/csrc/play/play.c"
echo

# ================================================================== 5. untested
cat <<'EOF'
## 5. Known untested (known unknowns, not looked at)

Code no recording runs cannot be shown to match: a divergence there stays
unseen until something runs it.

### Java methods no recording runs (make -C oracle coverage)

EOF
if [ -f "$JCOV/summary.txt" ]; then
    st=$(cat "$JCOV/state" 2>/dev/null || echo)
    case $st in
        done*) echo "From master's out/java/coverage (made on ${st#done }).";;
        "") echo "From master's out/java/coverage (its state file is missing: it may be mid-rebuild by out/flywheel/bin/covmap.sh).";;
        *) echo "From master's out/java/coverage (state \"$st\": being rebuilt; these are the last complete numbers).";;
    esac
    echo
    sed -n '1,3p' "$JCOV/summary.txt" | sed 's/^/    /'
    echo
    echo "By package, the most unrun in-scope methods first:"
    echo
    echo "| package | run | in scope | run % | classes never run |"
    echo "|---|---:|---:|---:|---:|"
    awk '/^in-scope methods by package/ { on = 1; next }
    on && NF == 0 { on = 0 }
    on && $3 == "/" { pk = $1; run = $2; tot = $4; pct = $5; cn = $6; ct = $8
        if (run < tot) printf "%d\t%s\t%d\t%d\t%s\t%d of %d\n", tot - run, pk, run, tot, pct, cn, ct }' "$JCOV/summary.txt" \
        | sort -t$'\t' -k1,1nr -k2,2 | awk -F'\t' '{ printf "| %s | %s | %s | %s | %s |\n", $2, $3, $4, $5, $6 }'
    echo
    echo "In-scope classes none of whose methods any recording runs (scenarios never recorded):"
    echo
    awk '/^in-scope classes that no recording runs/ { on = 1; next }
    on && /^anonymous classes/ { next }
    on && /^  and [0-9]+ anonymous/ { sub(/^  /, ""); print "- " $0; on = 0; next }
    on && NF == 0 { on = 0 }
    on && /^  [a-z]/ { printf "- %s `%s` (%s methods)\n", $1, $2, $3 }' "$JCOV/summary.txt"
    echo
else
    echo "Not measured: master has no out/java/coverage/summary.txt (make -C oracle coverage)."
    echo
fi

echo "### Native functions no recording runs (make -C csrc coverage)"
echo
if [ -n "$NCOV" ]; then
    echo "From $(rel "$NCOV")/summary.txt:"
    echo
    sed -n '1,/^$/p' "$NCOV/summary.txt" | sed '/^$/d; s/^/    /' | head -n 12
    echo
    nu=$(grep -vc '^#' "$NCOV/unexecuted.tsv" 2>/dev/null || true)
    echo "$nu functions in $(rel "$NCOV")/unexecuted.tsv; the files with the most:"
    echo
    grep -v '^#' "$NCOV/unexecuted.tsv" | cut -f1 | sort | uniq -c | sort -k1,1nr -k2,2 | head -n 15 \
        | awk '{ printf "- `%s`: %d\n", $2, $1 }'
    echo
    echo "Files none of whose functions any recording runs (file, functions, layer, the Java it ports):"
    echo
    awk '/^files none of whose functions/ { on = 1; next }
    on && NF == 0 { on = 0 }
    on && /^  / { f = $1; n = $2; l = $3; $1 = $2 = $3 = ""; sub(/^ +/, ""); printf "- `%s`: %s, %s%s\n", f, n, l, ($0 == "-" || $0 == "" ? "" : ", " $0) }' "$NCOV/summary.txt"
    echo
else
    echo "Not measured on this tree or master: no out/native/coverage/summary.txt (make -C csrc coverage)."
    echo
fi

cat <<'EOF'
### What the fuzzers never do

The generated sessions' blind spots: a line stays while its probe matches
none of csrc/tests/playfuzz/playgen.h, gen.c, ends.c, agentgen.c and
csrc/tests/rawfuzz/gen.c (csrc/tests/divergences_gaps.tsv). Directed
recordings may cover the same ground once; the random search never goes
there.

EOF
G="$ROOT/csrc/tests/playfuzz/playgen.h $ROOT/csrc/tests/playfuzz/gen.c $ROOT/csrc/tests/playfuzz/ends.c $ROOT/csrc/tests/playfuzz/agentgen.c $ROOT/csrc/tests/rawfuzz/gen.c"
grep -v '^#' "$GAPS" | tail -n +2 | while IFS=$'\t' read -r gap re; do
    [ -n "$gap" ] || continue
    # shellcheck disable=SC2086
    if [ "$re" = - ] || ! grep -Eqs -- "$re" $G; then echo "- $gap"; fi
done
echo
echo "The switched-off features (section 4) are attempted by the fuzzers (boats, minecarts, the fishing rod) but do nothing in either engine."
echo

# ================================================================== 6. unknown
cat <<'EOF'
## 6. Unknown unknowns

They cannot be listed: a divergence nobody has seen has no row. What bounds
them is how hard the search has looked and how fast it still finds: the
number of new causes per 1,000 sessions, round by round. A rate that falls to
zero over many sessions says the fuzzers' action space is clean, not the game;
widening the action space (kits, new starts, section 5's gaps) has found new
causes at once every time.

### New causes per 1,000 sessions, round by round

Before the sweep, from the fuzz lanes' reports and notes
(csrc/tests/divergences_rounds.tsv; "-" where the lane did not count):

| date | lane | fuzzer | round | sessions | rows | failing | new causes | per 1,000 |
|---|---|---|---|---:|---:|---:|---:|---:|
EOF
awk -F'\t' 'function c(x,   s) { if (x == "-") return x; s = sprintf("%d", x); while (s ~ /[0-9][0-9][0-9][0-9]/) sub(/[0-9][0-9][0-9]$|[0-9][0-9][0-9],/, ",&", s); return s }
/^#/ || $1 == "date" { next }
{ per = ($5 != "-" && $8 != "-" && $5 > 0) ? sprintf("%.1f", 1000 * $8 / $5) : "-"
  printf "| %s | %s | %s | %s | %s | %s | %s | %s | %s |\n", $1, $2, $3, $4, c($5), c($6), $7, $8, per }' "$ROUNDS"
echo
echo "The sweep (master's out/sweep/rate.tsv, every fuzzer together):"
echo
if [ -s "$T/sweeprate" ]; then
    echo "| round | time | build | sessions | rows | pass | fail | new clusters | per 1,000 |"
    echo "|---:|---|---|---:|---:|---:|---:|---:|---:|"
    awk -F'\t' 'function c(x,   s) { s = sprintf("%d", x); while (s ~ /[0-9][0-9][0-9][0-9]/) sub(/[0-9][0-9][0-9]$|[0-9][0-9][0-9],/, ",&", s); return s }
{ printf "| %s | %s | %s | %s | %s | %s | %s | %s | %s |\n", $1, $2, $3, c($5), c($6), $7, $8, $9, ($5 > 0 ? sprintf("%.1f", 1000 * $9 / $5) : "-") }' "$T/sweeprate"
else
    echo "No finished sweep round yet."
fi
echo
cat <<'EOF'
### What would surface them

- The sweep running on every master (csrc/tests/sweep/sweep.sh): new seeds
  on every start, clusters handed to fix lanes, a fixed cluster that fails
  again reopened as a regression.
- Widening the action space: each gap in section 5 that a generator learns;
  new starts (other seeds, the Nether and the End, late-game saves).
- Coverage-driven scenes: each unrun in-scope method in section 5 run by a
  recording that checks it every tick.
- Human sessions (csrc/play/session.sh) and Java-judged speedruns
  (csrc/play/rawjudge.sh): the inputs no model of a player thinks of.
- Longer sessions: a Minecraft day or more in one session (random ticks,
  weather, the moon, despawns, village life at their natural rates).
EOF
echo

# ================================================================== 7. bridge 2
cat <<'EOF'
## 7. C -> CUDA: device kernels and the pool against the C engine

Each device kernel (csrc/cuda) and the env pool's result (csrc/runtime)
are checked bit-exact against the C engine (make -C csrc gpu-lighting-check and
the phase checks). Its rows are the ledger's area `cuda`, open first.

EOF
if [ -s "$T/b2.tsv" ]; then
    for st in open fixed superseded accepted; do
        grep -q "$(printf '\t%s\t' "$st")" "$T/b2.tsv" && row "$st" "$st" "$T/b2.tsv" | sed "s/^### cuda (/### $st (/"
    done
else
    echo "No row yet."
    echo
fi
if [ -s "$T/problems" ]; then
    echo "## Ledger problems"
    echo
    sed 's/^/- /' "$T/problems"
    echo
fi
echo "Sources: $(for f in $files; do printf '%s ' "$(rel "$f")"; done)$(rel "$ROUNDS") $(rel "$GAPS")."
} > "$T/out.md"

mv "$T/out.md" "$OUT.new.$$" && mv -f "$OUT.new.$$" "$OUT"
echo "divergences.sh: wrote $(rel "$OUT") ($(grep -c . "$T/merged.tsv" || true) ledger ids, $(wc -l < "$T/problems") problems)"
