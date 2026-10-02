#!/usr/bin/env bash
# history: how sig.sh's signature clusters the fuzz lanes' past failures,
# whose causes are known (each was fixed by one commit).
#
#   bash csrc/tests/sweep/history.sh rediverge TREE LIST OUTDIR
#   bash csrc/tests/sweep/history.sh score TABLE
#
# rediverge: LIST has one line per fix, COMMIT PIN..., a commit that fixed
# the divergence each PIN (a recording directory, the failing session kept)
# shows. TREE (a worktree made here, detached) is moved to the commit's
# first parent (the tree the failure was found on), native and java are
# built there, and out/native/diverge runs on every pin of that commit:
# OUTDIR/PIN.txt, what the sweep would have read.
# score: TABLE is tab-separated, one line per failure:
#   lane  case  fuzzer  dim  cause  field  diverge-file-or--  [recording]
# Each failure's full signature (fuzzer, dim and sig.sh over its diverge
# output; with the recording, dim is the player's at the first differing
# row, the sweep's rule) where the file exists, and its coarse one (fuzzer,
# the start's dim and the
# first differing field: all a lane's notes record) for every line. For
# each it prints the causes, the signatures, the causes split over more than
# one signature (a wrong split: the orchestrator would hand one cause to two
# lanes) and the signatures holding more than one cause (a wrong merge: a
# cause hidden behind another's cluster), each listed.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
M=$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir)/.." && pwd)
case ${1:-} in
rediverge)
    T=$2 list=$3 out=$4
    mkdir -p "$out"
    [[ -d $T ]] || git -C "$M" worktree add -q --detach "$T" master || exit 1
    mkdir -p "$T/out/java"
    for d in lib natives client assets; do [[ -e $T/out/java/$d ]] || ln -s "$M/out/java/$d" "$T/out/java/$d"; done
    # a pin names its start checkpoint by a path in the tree that saved it,
    # often gone; the oracle then reads its own tree's out/java/checkpoints:
    # master's and each pin's tree's, linked
    for src in "$M/out/java/checkpoints" $(cut -d' ' -f2- "$list" | tr ' ' '\n' | grep /out/java/ | sed 's|/out/java/.*|/out/java/checkpoints|' | sort -u); do
        for sd in "$src"/*/; do
            [[ -d $sd ]] || continue
            mkdir -p "$T/out/java/checkpoints/$(basename "$sd")"
            for c in "$sd"*/; do
                [[ -e $T/out/java/checkpoints/$(basename "$sd")/$(basename "$c") ]] || ln -s "$(readlink -f "$c")" "$T/out/java/checkpoints/$(basename "$sd")/$(basename "$c")"
            done
        done
    done
    while read -r c pins; do
        [[ -n $c && $c != \#* ]] || continue
        git -C "$T" checkout -q --detach "$c^" || { echo "history: no $c^"; continue; }
        if ! make -s -C "$T/$([ -d "$T/csrc" ] && echo csrc || echo native)" -j8 "$T/out/native/test_snapshots" "$T/out/native/diverge" > "$out/build-$c.log" 2>&1 \
            || ! make -s -C "$T/$([ -d "$T/oracle" ] && echo oracle || echo java)" build >> "$out/build-$c.log" 2>&1; then
            echo "history: $c^ does not build ($out/build-$c.log)"; continue
        fi
        for p in $pins; do
            ( cd "$T" && timeout 3600 out/native/diverge "$p" > "$out/$(basename "$p").txt" 2>&1
              echo "$c $(basename "$p"): $(head -1 "$out/$(basename "$p").txt" | cut -c1-120)" ) &
        done
        wait
    done < "$list"
    ;;
score)
    tab=$2
    while IFS=$'\t' read -r lane cs fz dim cause field div rec; do
        [[ -n $lane && $lane != \#* ]] || continue
        full=-
        if [[ $div != - && -f $div ]]; then
            rd=$dim r=
            r=$(head -1 "$div" | sed -n 's/.*first mismatch at row \([0-9]*\):.*/\1/p')
            if [[ -n ${rec:-} && -n $r && -f $rec/tape.jsonl ]]; then
                rd=$(jq -r --argjson r "$r" 'select(.t == $r and .sp != null) | .sp.dim' "$rec/tape.jsonl" | head -1)
                [[ -n $rd && $rd != null ]] || rd=$dim
            fi
            full="$fz|$rd|$(bash "$here/sig.sh" "$div" | tr '\t' '|')"
            # the table's field is the notes' reading; the diverge file's wins
            field=$(cut -d'|' -f3 <<< "$full")
        fi
        field=$(sed 's/\[[0-9][0-9]*\]/[]/g' <<< "$field")
        printf '%s\t%s\t%s\t%s\t%s\n' "$lane" "$cs" "$cause" "$fz|$dim|$field" "$full"
    done < "$tab" > "${TMPDIR:-/tmp}/history.$$"
    for k in full coarse; do
        col=5; [[ $k == coarse ]] && col=4
        awk -F'\t' -v col=$col -v k=$k '
            $col != "-" { n++; key = $col; c = $3
                if (!((c, key) in seen)) { seen[c, key] = 1; nk[c]++; nc[key]++; keys[c] = keys[c] " " key; causes[key] = causes[key] " " c }
                allc[c] = 1; allk[key] = 1 }
            END {
                for (c in allc) nC++; for (x in allk) nK++
                for (c in allc) if (nk[c] > 1) { sc++; sx += nk[c] - 1; spl = spl sprintf("    split  %s:%s\n", c, keys[c]) }
                for (x in allk) if (nc[x] > 1) { mc++; mx += nc[x] - 1; mrg = mrg sprintf("    merged %s:%s\n", x, causes[x]) }
                printf "%s signature: %d failures, %d causes, %d signatures; %d causes split (%d extra signatures), %d signatures merge causes (%d causes hidden)\n",
                    k, n, nC, nK, sc, sx, mc, mx
                printf "%s%s", spl, mrg
            }' "${TMPDIR:-/tmp}/history.$$"
    done
    rm -f "${TMPDIR:-/tmp}/history.$$"
    ;;
*) sed -n '2,24p' "$0"; exit 2;;
esac
