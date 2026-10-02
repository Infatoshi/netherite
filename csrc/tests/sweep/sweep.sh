#!/usr/bin/env bash
# sweep: the fuzzers run continuously on master's binaries and only find.
# Each round plays a mix of fresh sessions (csrc/tests/playfuzz/chain.sh
# chains from fresh, gN and sN starts with kits, agentrun.sh cases from s1,
# the seed-1 checkpoints and gN, csrc/tests/rawfuzz/run.sh sessions) with
# seeds no round used before, turns every FAIL into a cause signature
# (csrc/tests/sweep/sig.sh over out/native/diverge), clusters them and
# keeps one table of open causes for the orchestrator to hand out, one
# cluster per fix lane. Nothing here fixes a divergence.
#
#   bash csrc/tests/sweep/sweep.sh [--rounds N] [--sessions S] [--plant PATCH]
#   bash csrc/tests/sweep/sweep.sh --status [ROUNDS]
#   bash csrc/tests/sweep/sweep.sh --assign ID LANE | --open ID
#   bash csrc/tests/sweep/sweep.sh --recheck      the repros of every cluster on the sweep's build now
#
# Rounds run forever unless --rounds. Each round runs in the sweep's own
# worktree (~/dev/nv2-lanes/.sweep) pinned to master's head: before the round
# it moves to the new head (detached), links master's recordings (make
# lane-share) and rebuilds native and java there, so a round never mixes two
# builds. --plant PATCH applies a patch (a planted bug) to that worktree for
# the round and undoes it before the next one. The output is master's
# out/sweep (untracked, it outlives any lane):
#   jobs          the parallelism, read before every unit starts (default 8):
#                 the orchestrator turns it up or down without a restart
#   ledger.tsv    every seed used, per fuzzer (fuzzer seed case round); a
#                 fuzzer's next seed is past its floor and past every used one
#   rounds/R/     plan.tsv, units/ (each unit's output), sessions.tsv (one
#                 line per session: round fuzzer case session start - (unused)
#                 result rows seconds detail), fails/ (diverge and sig per FAIL)
#   causes.tsv    one line per cluster (signature): id status first-round
#                 first-time last-round sessions fuzzer dim field class path
#                 where example-repro note. status: open, assigned LANE, fixed
#                 COMMIT, or infra: a raw session's or agent case's Java run
#                 failed, which reads nothing native (a harness failure, kept
#                 with its logs, never counted as a new cause). A cluster
#                 marked assigned is not reported as new.
#                 When the build moves, every open, assigned and fixed
#                 cluster's repros run on the new build: all passing marks
#                 it fixed with the commit, a fixed one failing reopens it.
#   finds/ID/NAME the failing session's repro: rec/ (the recording: the start
#                 snapshot and Java's rows), the inputs to play it again,
#                 diverge.txt, sig, info and repro.sh (check: the native
#                 replay of rec; rerun: the whole session again)
#   rate.tsv      per round and fuzzer: sessions rows PASS FAIL new clusters,
#                 the round's wall seconds and the sweep's own CPU seconds
#   sweep.log     what each round did
# --status prints the last rounds, the open clusters with their repros, and
# new causes per 1,000 sessions: per round, per fuzzer and per start.
set -uo pipefail
here=$(cd "$(dirname "$0")" && pwd)
args=("$@")
M=$(cd "$(git -C "$here" rev-parse --path-format=absolute --git-common-dir 2>/dev/null)/.." 2>/dev/null && pwd)
TREE=$HOME/dev/nv2-lanes/.sweep
OUT=$M/out/sweep
rounds=0 sessions=330 plant= mode=run
MIX_AGENT=40 MIX_RAW=5 K=8
FLOOR_chain=50001 FLOOR_agent=100001 FLOOR_raw=100001
while [[ $# -gt 0 ]]; do
    case $1 in
        --rounds) rounds=$2; shift 2;;
        --sessions) sessions=$2; shift 2;;
        --plant) plant=$(readlink -f "$2"); shift 2;;
        --status) mode=status; nstat=${2:-5}; [[ $nstat =~ ^[0-9]+$ ]] && shift; shift;;
        --assign) mode=assign; aid=$2 alane=$3; shift 3;;
        --open) mode=reopen; aid=$2; shift 2;;
        --recheck) mode=recheck; shift;;
        --recount) mode=recount; rr=$2; shift 2;;
        --tree) TREE=$2; shift 2;;
        --out) OUT=$2; shift 2;;
        --master) M=$2; shift 2;;
        -h|--help) sed -n '2,48p' "$0"; exit 0;;
        *) sed -n '2,48p' "$0"; exit 2;;
    esac
done
mkdir -p "$OUT/rounds" "$OUT/finds"
export UV_CACHE_DIR=${UV_CACHE_DIR:-$HOME/.cache/uv} TMPDIR=$HOME/dev/nw/.tmp
mkdir -p "$TMPDIR"
[[ -s $OUT/jobs ]] || echo 8 > "$OUT/jobs"
[[ -f $OUT/ledger.tsv ]] || printf 'fuzzer\tseed\tcase\tround\n' > "$OUT/ledger.tsv"
[[ -f $OUT/causes.tsv ]] || printf 'id\tstatus\tfirst_round\tfirst_time\tlast_round\tsessions\tfuzzer\tdim\tfield\tclass\tpath\twhere\texample\tnote\n' > "$OUT/causes.tsv"
[[ -f $OUT/rate.tsv ]] || printf 'round\ttime\tbuild\tfuzzer\tsessions\trows\tpass\tfail\tnew\twall_s\tcpu_s\n' > "$OUT/rate.tsv"

log() { echo "$(date '+%F %T') $*" | tee -a "$OUT/sweep.log"; }
jobs_now() { local j; j=$(tr -dc 0-9 < "$OUT/jobs" 2>/dev/null); [[ -n $j && $j -ge 1 ]] && echo "$j" || echo 8; }

CHAIN_STARTS=(fresh $(seq -f 'g%g' 1 30) $(seq -f 's%g' 1 23))
AGENT_STARTS=(s1 cp-mobs-s1 cp-nether-s1 cp-end-s1 cp-far-s1 snap-afternether-s1 $(seq -f 'g%g' 1 30))
RAW_STARTS=(fresh fresh42 $(seq -f 'S%g' 1 23) $(seq -f 'G%g' 1 30))

# ---------------------------------------------------------------- causes.tsv
# (every writer holds $OUT/.causes.lock)
with_causes() { ( flock 8; "$@" ) 8> "$OUT/.causes.lock"; }
set_status() { # ID STATUS [NOTE]
    awk -F'\t' -v OFS='\t' -v id="$1" -v st="$2" -v note="${3:-}" \
        '$1 == id { $2 = st; if (note != "") $14 = ($14 == "" || $14 == "-" ? note : $14 "; " note) } { print }' \
        "$OUT/causes.tsv" > "$OUT/causes.tsv.new" && mv "$OUT/causes.tsv.new" "$OUT/causes.tsv"
}

# ---------------------------------------------------------------- the tree
# the sweep's worktree at master's head (or HEAD), built; sets H and B
prep_tree() {
    H=$(git -C "$M" rev-parse master)
    if [[ ! -d $TREE ]]; then git -C "$M" worktree add -q --detach "$TREE" "$H" || return 1; fi
    if [[ -s $OUT/plant.applied ]]; then
        git -C "$TREE" apply -R "$OUT/plant.applied" || { log "sweep: the planted patch does not undo in $TREE"; return 1; }
        rm -f "$OUT/plant.applied"
    fi
    git -C "$TREE" diff --quiet HEAD || { log "sweep: $TREE has local changes; refusing to build a mixed tree"; return 1; }
    git -C "$TREE" checkout -q --detach "$H" || return 1
    mkdir -p "$TREE/out/java"
    local d
    for d in lib natives client assets; do [[ -e $TREE/out/java/$d ]] || ln -s "$M/out/java/$d" "$TREE/out/java/$d"; done
    (cd "$M" && make -s lane-share NAME="$(basename "$TREE")" LANES="$(dirname "$TREE")") > /dev/null 2>&1
    B=${H:0:10}
    if [[ -n $plant ]]; then
        cp "$plant" "$OUT/plant.applied"
        git -C "$TREE" apply "$OUT/plant.applied" || { rm -f "$OUT/plant.applied"; log "sweep: $plant does not apply"; return 1; }
        B=$B+plant-$(sha1sum < "$plant" | cut -c1-8)
    fi
    make -s -C "$TREE/csrc" -j16 all play play-dev "$TREE/out/native/test_snapshots" "$TREE/out/native/diverge" > "$OUT/build.log" 2>&1 \
        || { log "sweep: native build failed at $B ($OUT/build.log)"; return 1; }
    make -s -C "$TREE/oracle" build >> "$OUT/build.log" 2>&1 || { log "sweep: java build failed at $B ($OUT/build.log)"; return 1; }
    echo "$B" > "$OUT/build.now"
}

# ---------------------------------------------------------------- repros
# one repro on the sweep's current build B: PASS, or FAIL with the signature
# it fails with now (a repro can show a second cause once the first is
# fixed, or hide one behind it: that one is filed as a find of its own)
recheck_one() { # DIR B
    local dir=$1 b=$2 name fz key detail div
    name=$(basename "$dir")
    if bash "$dir/repro.sh" check "$TREE" > "$dir/check.$b.log" 2>&1; then
        printf 'PASS\t%s\t-\n' "$dir"; return
    fi
    fz=${name%%-*} div=$dir/diverge.$b.txt
    detail=$(grep -a -m1 '^FAIL' "$dir/check.$b.log")
    case $fz in
        chain|agent) (cd "$TREE" && timeout 3600 out/native/diverge "$dir/$name") > "$div" 2>&1;;
        raw) if grep -q "the engine on Java's acts" "$dir/check.$b.log"; then
                 (cd "$TREE" && timeout 3600 out/native/diverge "$TREE/out/native/rawjudge/$name/check") > "$div" 2>&1
             else : > "$div"; fi;;
    esac
    key="$(cut -f1,2 "$dir/sig")	$(bash "$here/sig.sh" "$div" "$detail")"
    printf 'FAIL\t%s\t%s\n' "$dir" "$(tr '\t' '|' <<< "$key")"
}

# every cluster's repros on build B (the tree moved): a cluster none of
# whose repros fails with its own signature is fixed on B; a fixed one whose
# repro fails with it again is reopened; a repro failing with another
# signature is staged into round R's fails as a find of that cause
recheck_all() { # B R
    local b=$1 r=$2 id st key pass held other dir res k name
    [[ $(tail -n +2 "$OUT/causes.tsv" | wc -l) -gt 0 ]] || return 0
    # (a repro filed under another cause since holds that one's place)
    # (an infra cluster's repro is a Java run that never finished: nothing to check natively)
    find "$OUT/finds" -mindepth 3 -maxdepth 3 -name repro.sh -printf '%h\n' | sort \
        | while read -r dir; do
              [[ -f $dir/superseded ]] && continue
              [[ $(awk -F'\t' -v id="$(basename "$(dirname "$dir")")" '$1 == id { print $2 }' "$OUT/causes.tsv") == infra ]] && continue
              echo "$dir"
          done > "$OUT/.recheck.dirs"
    export -f recheck_one; export TREE here
    xargs -P "$(jobs_now)" -I{} bash -c 'recheck_one "$1" "$2"' _ {} "$b" < "$OUT/.recheck.dirs" > "$OUT/rounds/$r/recheck.tsv"
    awk -F'\t' 'NR > 1 { print $1 "\t" $2 "\t" $7 "|" $8 "|" $9 "|" $10 "|" $11 "|" $12 }' "$OUT/causes.tsv" > "$OUT/.recheck"
    while IFS=$'\t' read -r id st key; do
        read -r pass held other < <(awk -F'\t' -v p="$OUT/finds/$id/" -v k="$key" 'index($2, p) == 1 {
            if ($1 == "PASS") a++; else if ($3 == k) h++; else o++ } END { print a + 0, h + 0, o + 0 }' "$OUT/rounds/$r/recheck.tsv")
        ((pass + held + other > 0)) || continue
        if ((held == 0)) && [[ $st != fixed* ]]; then
            with_causes set_status "$id" "fixed $b" "fixed on $b ($pass repros pass$( ((other)) && echo ", $other fail with another cause"))"
            log "recheck: $id fixed on $b ($pass pass, $other fail with another cause)"
        elif ((held > 0)) && [[ $st == fixed* ]]; then
            with_causes set_status "$id" open "regression on $b ($held of $((pass + held + other)) repros fail with it)"
            log "recheck: $id reopened on $b ($held repros fail with it)"
        fi
    done < "$OUT/.recheck"
    # the repros that fail with another signature now
    while IFS=$'\t' read -r res dir k; do
        [[ $res == FAIL ]] || continue
        id=$(basename "$(dirname "$dir")")
        [[ $k == "$(awk -F'\t' -v id="$id" '$1 == id { print $3 }' "$OUT/.recheck")" ]] && continue
        name=$(basename "$dir")
        mkdir -p "$OUT/rounds/$r/fails"
        rm -rf "$OUT/rounds/$r/fails/$name"; cp -r "$dir" "$OUT/rounds/$r/fails/$name"
        rm -f "$OUT/rounds/$r/fails/$name/cluster"
        tr '|' '\t' <<< "$k" > "$OUT/rounds/$r/fails/$name/sig"
        cp "$dir/diverge.$b.txt" "$OUT/rounds/$r/fails/$name/diverge.txt"
        echo "recheck of $id's repro on $b: it fails with another signature" >> "$OUT/rounds/$r/fails/$name/info"
        echo "failed with another signature on $b: filed again in round $r" > "$dir/superseded"
        log "recheck: $name ($id) fails on $b with another signature: $k"
    done < "$OUT/rounds/$r/recheck.tsv"
    rm -f "$OUT/.recheck" "$OUT/.recheck.dirs"
}

# ---------------------------------------------------------------- the plan
next_seed() { # FUZZER: past the floor and every seed the ledger holds
    local fl=FLOOR_$1
    awk -F'\t' -v f="$1" -v fl="${!fl}" 'BEGIN { m = fl - 1 } $1 == f && $2 + 0 > m { m = $2 + 0 } END { print m + 1 }' "$OUT/ledger.tsv"
}
plan_round() { # R S: plan.tsv (fuzzer case expected-sessions), the ledger
    local r=$1 s=$2 na nr nc i seed st
    nr=$(( (s * MIX_RAW + 99) / 100 )); na=$(( s * MIX_AGENT / 100 )); nc=$(( (s - nr - na + K - 1) / K ))
    : > "$OUT/rounds/$r/plan.tsv"
    seed=$(next_seed chain)
    for ((i = 0; i < nc; i++)); do
        st=${CHAIN_STARTS[$(( (seed + i) % ${#CHAIN_STARTS[@]} ))]}
        printf 'chain\t%s:%s\t%s\n' "$st" $((seed + i)) "$K"
        printf 'chain\t%s\t%s:%s\t%s\n' $((seed + i)) "$st" $((seed + i)) "$r" >> "$OUT/ledger.tsv"
    done > "$OUT/rounds/$r/plan.chain"
    seed=$(next_seed raw)
    for ((i = 0; i < nr; i++)); do
        st=${RAW_STARTS[$(( (seed + i) % ${#RAW_STARTS[@]} ))]}
        printf 'raw\t%s:%s\t1\n' "$st" $((seed + i))
        printf 'raw\t%s\t%s:%s\t%s\n' $((seed + i)) "$st" $((seed + i)) "$r" >> "$OUT/ledger.tsv"
    done > "$OUT/rounds/$r/plan.raw"
    seed=$(next_seed agent)
    for ((i = 0; i < na; i++)); do
        st=${AGENT_STARTS[$(( (seed + i) % ${#AGENT_STARTS[@]} ))]}
        printf 'agent\t%s:%s\t1\n' "$st" $((seed + i))
        printf 'agent\t%s\t%s:%s\t%s\n' $((seed + i)) "$st" $((seed + i)) "$r" >> "$OUT/ledger.tsv"
    done > "$OUT/rounds/$r/plan.agent"
    # the long units first (a chain is K sessions in a row; a raw session
    # plays in real time), so the round's tail is the short agent cases
    paste -d'\n' "$OUT/rounds/$r/plan.chain" <(awk '{print; print ""}' "$OUT/rounds/$r/plan.raw") | grep . > "$OUT/rounds/$r/plan.tsv"
    cat "$OUT/rounds/$r/plan.agent" >> "$OUT/rounds/$r/plan.tsv"
    rm -f "$OUT/rounds/$r/plan".{chain,raw,agent}
}

# ---------------------------------------------------------------- one unit
unit() { # R FUZZER CASE: one fuzzer run of one case in the sweep's tree
    local r=$1 fz=$2 c=$3 out="$OUT/rounds/$1/units/$2-${3/:/-}.out"
    local cmd
    case $fz in
        chain) cmd=(timeout -k 60 5400 bash csrc/tests/playfuzz/chain.sh -j 1 -k "$K" "$c");;
        agent) cmd=(timeout -k 60 1800 bash csrc/tests/playfuzz/agentrun.sh -j 1 "$c");;
        raw) local nf=(); grep -q -- '-F)' "$TREE/csrc/tests/rawfuzz/run.sh" && nf=(-F)
             cmd=(timeout -k 60 900 bash csrc/tests/rawfuzz/run.sh ${nf[@]+"${nf[@]}"} -j 1 "$c");;
    esac
    # (timeout signals its whole process group: a stuck Java client too. A
    # raw session plays 1.5 to 5 minutes; one whose RawDrive never ends,
    # rf-S12-100058, ticked for 28 minutes and 2,000 CPU s)
    local t0=$SECONDS
    (cd "$TREE" && /usr/bin/time -f 'unit-cpu %U %S' nice -n 10 "${cmd[@]}") > "$out" 2>&1
    echo "unit-rc $? wall $((SECONDS - t0))" >> "$out"
    # what a judged session leaves that no one reads: the raw session's game
    # directories (the sweep keeps its own copy of a FAIL)
    [[ $fz == raw ]] && rm -rf "$TREE/out/java/run/rf-${c%%:*}-${c##*:}-tape" "$TREE/out/java/run/rf-${c%%:*}-${c##*:}-tape.replay"
}

run_units() { # R: every unit of plan.tsv, at most jobs_now at once
    local r=$1 fz c k
    mkdir -p "$OUT/rounds/$r/units"
    while IFS=$'\t' read -r fz c k; do
        while (( $(jobs -rp | wc -l) >= $(jobs_now) )); do wait -n; done
        unit "$r" "$fz" "$c" &
    done < "$OUT/rounds/$r/plan.tsv"
    wait
}

# ---------------------------------------------------------------- results
# the dimension the player is in at the first differing row, read from the
# failing recording's own rows (the start's when the failure names no row):
# a cause shows in the world it happens in, whatever the session started in
# (the planted jump bug of round 4 split in two by the start's dimension: a
# session from the Nether had walked through its portal first)
fail_dim() { # TAPE DIVERGE DETAIL
    local tape=$1 div=$2 detail=$3 r= d=
    r=$(head -1 "$div" 2>/dev/null | sed -n 's/.*first mismatch at row \([0-9]*\):.*/\1/p')
    [[ -n $r ]] || r=$(sed -n 's/.* t=\([0-9]*\).*/\1/p; s/.*: row \([0-9]*\) .*/\1/p' <<< "$detail" | head -1)
    [[ -n $r ]] && d=$(jq -r --argjson r "$r" 'select(.t == $r and .sp != null) | .sp.dim' "$tape" 2>/dev/null | head -1)
    [[ -n $d && $d != null ]] || d=$(jq -r 'select(.sp != null) | .sp.dim' "$tape" 2>/dev/null | head -1)
    [[ -n $d && $d != null ]] && echo "$d" || echo -
}

collect() { # R: rounds/R/sessions.tsv
    local r=$1 fz c k f line
    local S=$OUT/rounds/$r/sessions.tsv off
    off=$(( $(cat "$OUT/rounds/$r/chain-offset" 2>/dev/null || echo 0) + 1 ))
    : > "$S"
    while IFS=$'\t' read -r fz c k; do
        f="$OUT/rounds/$r/units/$fz-${c/:/-}.out"
        case $fz in
        chain)
            # the chain's session lines (chain.sh appends them), else its own line
            # (only the lines this round appended: a case run before, by hand
            # or by a round that stopped, left its own)
            # (not grep -q in a pipe: under pipefail its early exit fails the
            # pipe when tail is still writing, and a PASS chain read as none)
            line=$(tail -n +"$off" "$TREE/out/playfuzz/chain-sessions.tsv" 2>/dev/null | awk -F'\t' -v OFS='\t' -v r="$r" -v c="$c" '$1 == c {
                    split(c, a, ":"); print r, "chain", c, $2, a[1], "-", $5, $7 + 0, $9 + 0, $10 }')
            if [[ -n $line ]]; then
                echo "$line" >> "$S"
            else
                line=$(grep -m1 "^$c	" "$f")
                printf '%s\tchain\t%s\t1\t%s\t-\tFAIL\t0\t0\t%s\n' "$r" "$c" "${c%%:*}" "the chain did not run: ${line:-$(tail -n 2 "$f" | head -1)}" >> "$S"
            fi;;
        agent)
            line=$(grep -m1 "^$c	" "$f")
            if [[ -n $line ]]; then
                awk -F'\t' -v OFS='\t' -v r="$r" '{ split($1, a, ":"); print r, "agent", $1, 1, a[1], "-", $2, $4 + 0, $7 + 0, $8 }' <<< "$line" >> "$S"
            else
                printf '%s\tagent\t%s\t1\t%s\t-\tFAIL\t0\t0\t%s\n' "$r" "$c" "${c%%:*}" "the case did not run: $(tail -n 2 "$f" | head -1)" >> "$S"
            fi;;
        raw)
            local n="rf-${c%%:*}-${c##*:}"
            line=$(grep -m1 "^$n	" "$f")
            if [[ -n $line ]]; then
                # rows and screen ops are the judge; frames have no reviewed
                # budget for a new session (every one is "no budget line")
                awk -F'\t' -v OFS='\t' -v r="$r" -v c="$c" '{
                    res = ($2 == "PASS" && $3 == "PASS") ? "PASS" : "FAIL"
                    split(c, a, ":"); print r, "raw", c, 1, a[1], "-", res, $5 + 0, $6 + 0, (res == "PASS" ? "" : $7) }' <<< "$line" >> "$S"
            else
                printf '%s\traw\t%s\t1\t%s\t-\tFAIL\t0\t0\t%s\n' "$r" "$c" "${c%%:*}" "the session did not run: $(tail -n 2 "$f" | head -1)" >> "$S"
            fi;;
        esac
    done < "$OUT/rounds/$r/plan.tsv"
}

# ---------------------------------------------------------------- attribution
# one FAIL: its recording and inputs into fails/NAME, the diverge output, the
# signature. Prints NAME<TAB>signature fields
attribute() { # R FUZZER CASE SESSION DETAIL
    local r=$1 fz=$2 c=$3 i=$4 detail=$5 st=${3%%:*} n=${3##*:}
    local name d rec= div= dim=-
    case $fz in
        chain) name=chain-pfc-$st-$n-s$i;;
        agent) name=agent-rf-$st-$n;;
        raw) name=raw-rf-$st-$n;;
    esac
    d=$OUT/rounds/$r/fails/$name
    [[ -d $d ]] && chmod -R u+w "$d"
    rm -rf "$d"; mkdir -p "$d"
    case $fz in
    chain)
        local run=pfc-$st-$n hu=$TREE/out/human/pfc-$st-$n
        rec=$hu/play/$run-s$i-1.check
        [[ -f $hu/play/$run-s$i-1.diverge ]] && cp "$hu/play/$run-s$i-1.diverge" "$d/diverge.txt"
        [[ -d $rec ]] && cp -rL "$rec" "$d/$name"
        [[ -e $hu/start/$run-s$i ]] && cp -rL "$hu/start/$run-s$i" "$d/start"
        cp "$TREE/out/playfuzz/scripts/$run-s$i.jsonl" "$d/script.jsonl" 2>/dev/null
        [[ -s $TREE/out/playfuzz/scripts/$run-s$i.kit.jsonl ]] && cp "$TREE/out/playfuzz/scripts/$run-s$i.kit.jsonl" "$d/kit.jsonl"
        cp "$hu/play/$run-s$i-1.log" "$d/session.log" 2>/dev/null;;
    agent)
        rec=$TREE/out/java/rlfuzz/rf-$st-$n
        [[ -d $rec ]] && cp -rL "$rec" "$d/$name"
        cp "$TREE/out/rlfuzz/scripts/rf-$st-$n.jsonl" "$d/script.jsonl" 2>/dev/null
        cp "$TREE/out/rlfuzz/logs/rf-$st-$n.log" "$d/oracle.log" 2>/dev/null
        # the whole-server replay passed and play --check failed: no row for diverge
        if [[ -d $rec ]]; then
            (cd "$TREE" && timeout 3600 out/native/diverge "$rec") > "$d/diverge.txt" 2>&1
        fi;;
    raw)
        local sd=$TREE/out/java/rawfuzz/rf-$st-$n
        if [[ -d $sd ]]; then
            mkdir -p "$d/$name"
            cp "$sd"/{drive.jsonl,tape.jsonl,tape.raw.jsonl,judge.log,tape.log,play.log} "$d/$name/" 2>/dev/null
            [[ -f $sd/start ]] && cp "$sd/start" "$d/$name/start"
            # the engine on Java's acts diverged: diverge on the judge's check dir
            local chk=$TREE/out/native/rawjudge/rf-$st-$n/check
            if grep -q "the engine on Java's acts" "$sd/judge.log" 2>/dev/null && [[ -d $chk ]]; then
                (cd "$TREE" && timeout 3600 out/native/diverge "$chk") > "$d/diverge.txt" 2>&1
            fi
            detail=$(grep -m1 '^FAIL rawjudge \(rows\|gui\)' "$sd/judge.log" 2>/dev/null || echo "$detail")
        fi;;
    esac
    chmod -R u+w "$d" 2>/dev/null
    # the checkpoint the recording starts from, where a copy of it resolves
    # it (session.c resolve_checkpoint: ../../checkpoints/SEED/BASE beside
    # the recording): a copy of one that lives in the run (the previous
    # session's save), a link to a golden one
    local sdir seed cpd orig f
    if [[ $fz != raw && -f $d/$name/tape.jsonl ]]; then
        read -r orig seed < <(head -1 "$d/$name/tape.jsonl" | jq -r '[.start.dir // "-", .seed] | @tsv')
        sdir=$orig
        [[ $sdir == - || $sdir == /* ]] || sdir=$TREE/$sdir
        if [[ $sdir != - ]]; then
            cpd=$d/checkpoints/$seed/$(basename "$sdir")
            mkdir -p "$(dirname "$cpd")"
            if [[ $sdir == */out/human/* && -d $sdir ]]; then
                # the previous session's save goes with its run: a copy, and
                # the recording's and the start's headers name it (for Java
                # too, whose fallback is its own tree's out/java/checkpoints)
                cp -rL "$sdir" "$cpd"
                chmod -R u+w "$d"
                for f in "$d/$name/tape.jsonl" "$d/start/tape.jsonl"; do
                    [[ -f $f ]] && sed -i "1s|\"dir\":\"$orig\"|\"dir\":\"$OUT/finds/@ID@/checkpoints/$seed/$(basename "$sdir")\"|" "$f"
                done
            elif [[ -d $sdir ]]; then ln -sfn "$(readlink -f "$sdir")" "$cpd"
            elif [[ -d $TREE/out/java/checkpoints/$seed/$(basename "$sdir") ]]; then ln -sfn "$(readlink -f "$TREE/out/java/checkpoints/$seed/$(basename "$sdir")")" "$cpd"
            fi
        fi
    fi
    [[ -f $d/$name/tape.jsonl ]] && dim=$(fail_dim "$d/$name/tape.jsonl" "$d/diverge.txt" "$detail")
    echo "$detail" > "$d/detail"
    local sig
    sig=$(bash "$here/sig.sh" "$d/diverge.txt" "$detail")
    printf '%s\t%s\t%s\n' "$fz" "${dim:--}" "$sig" > "$d/sig"
    printf 'round %s\nbuild %s\nfuzzer %s\ncase %s\nsession %s\ndetail %s\n' "$r" "$(cat "$OUT/build.now")" "$fz" "$c" "$i" "$detail" > "$d/info"
    write_repro "$d" "$fz" "$c" "$i" "$name"
}

write_repro() { # DIR FUZZER CASE SESSION NAME
    local d=$1 fz=$2 c=$3 i=$4 name=$5 st=${3%%:*}
    {
        echo '#!/usr/bin/env bash'
        echo "# $fz $c session $i: the failing session kept by csrc/tests/sweep/sweep.sh"
        echo '#   bash repro.sh check [TREE]   the native check of rec/ (Java'"'"'s rows) on TREE'"'"'s build'
        echo '#   bash repro.sh rerun [TREE]   the whole session again from its start and inputs'
        echo '# TREE is a netherite-v2 tree with built binaries (default: the sweep'"'"'s worktree).'
        echo 'set -uo pipefail'
        echo 'd=$(cd "$(dirname "$0")" && pwd)'
        echo "T=\$(cd \"\${2:-$TREE}\" && pwd)"
        echo 'cd "$T"'
        echo 'case ${1:-check} in'
        case $fz in
        chain|agent)
            echo 'check) o=$(out/native/test_snapshots "$d/'"$name"'" 2>&1); rc=$?'
            echo '       grep -a "^FAIL\|rows simulated" <<< "$o" | head -3'
            echo '       [[ $rc == 0 ]] && grep -aq ": OK: " <<< "$o" || exit 1'
            echo '       bash csrc/tests/play_check.sh "$d/'"$name"'" "$T/out/native" | tail -2; exit ${PIPESTATUS[0]};;'
            if [[ $fz == chain ]]; then
                local dev=
                [[ -f $d/kit.jsonl ]] && dev=' --dev-ops "$d/kit.jsonl"'
                echo "rerun) rm -rf out/human/sweep-rr-$name"
                echo "       exec bash csrc/play/session.sh --run sweep-rr-$name --from \"\$d/start\" --no-chain --no-build --no-draw --input-script \"\$d/script.jsonl\"$dev;;"
            else
                echo "rerun) exec bash csrc/tests/playfuzz/agentrun.sh -j 1 \"$st:@\$d/script.jsonl\";;"
            fi;;
        raw)
            echo 'check) o=$(bash csrc/play/rawjudge.sh "$d/'"$name"'" --no-frames 2>&1); echo "$o" | grep "^PASS\|^FAIL"'
            echo '       grep -q "^PASS rawjudge rows" <<< "$o" && grep -q "^PASS rawjudge gui" <<< "$o";;'
            echo "rerun) exec bash csrc/tests/rawfuzz/run.sh -j 1 $c;;"
        esac
        echo '*) sed -n 2,5p "$0"; exit 2;;'
        echo 'esac'
    } > "$d/repro.sh"
}

# ---------------------------------------------------------------- clusters
# one staged find (a fails/NAME directory with its sig) into causes.tsv and,
# for the first three sessions of each cause, finds/ID/NAME (the caller
# holds the causes lock); prints new when it made a cluster
cluster_one() { # DIR R NOW
    local d=$1 r=$2 now=$3 key id st name f
    name=$(basename "$d")
    key=$(cat "$d/sig")
    id=$(awk -F'\t' -v k="$key" 'NR > 1 && ($7 "\t" $8 "\t" $9 "\t" $10 "\t" $11 "\t" $12) == k { print $1; exit }' "$OUT/causes.tsv")
    if [[ -z $id ]]; then
        id=$(printf 'c%04d' $(( $(awk -F'\t' 'NR > 1 { n = substr($1, 2) + 0; if (n > m) m = n } END { print m + 0 }' "$OUT/causes.tsv") + 1 )))
        # a raw session's or an agent case's Java run comes first and reads
        # nothing native: its failure is the harness's, never a divergence
        local st0=open note=-
        grep -q '^recheck of' "$d/info" && note=$(sed -n 's/^\(recheck of [^:]*\):.*/found by the \1/p' "$d/info")
        if [[ $(cut -f1 <<< "$key") != chain && $(cut -f3 <<< "$key") == oracle ]]; then
            st0=infra note="the Java run failed before any native check: a harness failure, not a divergence"
        fi
        printf '%s\t%s\t%s\t%s\t%s\t1\t%s\t%s\t%s\n' "$id" "$st0" "$r" "$now" "$r" "$key" "bash $OUT/finds/$id/$name/repro.sh" "$note" >> "$OUT/causes.tsv"
        if [[ $st0 == infra ]]; then echo "$id" >> "$OUT/rounds/$r/infra"
        else echo "$id" >> "$OUT/rounds/$r/new"; echo new; fi
    else
        st=$(awk -F'\t' -v id="$id" '$1 == id { print $2 }' "$OUT/causes.tsv")
        awk -F'\t' -v OFS='\t' -v id="$id" -v r="$r" '$1 == id { $5 = r; $6 = $6 + 1 } { print }' "$OUT/causes.tsv" > "$OUT/causes.tsv.new" && mv "$OUT/causes.tsv.new" "$OUT/causes.tsv"
        # a fixed cause seen again on a later build is a regression
        if [[ $st == fixed* && $(cat "$OUT/build.now") != "${st#fixed }" ]]; then
            set_status "$id" open "regression: $name in round $r"
        fi
    fi
    echo "$id" > "$d/cluster"
    chmod -R u+w "$d"
    # the checkpoint the session started from, copied from its run: the
    # headers name the cluster's copy
    for f in "$d/start/tape.jsonl" "$d/$name/tape.jsonl"; do [[ -f $f ]] && sed -i "1s|@ID@|$id|" "$f"; done
    if [[ $(find "$OUT/finds/$id" -mindepth 1 -maxdepth 1 -type d ! -name checkpoints 2>/dev/null | wc -l) -lt 3 && ! -e $OUT/finds/$id/$name ]]; then
        mkdir -p "$OUT/finds/$id"
        cp -r "$d" "$OUT/finds/$id/$name"
        if [[ -d $OUT/finds/$id/$name/checkpoints ]]; then
            mkdir -p "$OUT/finds/$id/checkpoints"
            cp -a --update=none "$OUT/finds/$id/$name/checkpoints/." "$OUT/finds/$id/checkpoints/"
            rm -rf "$OUT/finds/$id/$name/checkpoints"
        fi
        rm -f "$OUT/finds/$id/$name/cluster"
    fi
    # the round keeps each fail's record, not its recording
    find "$d" -mindepth 1 -maxdepth 1 ! -name sig ! -name info ! -name detail ! -name cluster ! -name 'diverge*.txt' -exec rm -rf {} +
}

cluster_round() { # R: every staged find of round R; prints FAILS NEW
    local r=$1 now d nf=0 new=0
    now=$(date '+%F %H:%M')
    for d in "$OUT/rounds/$r/fails"/*/; do
        [[ -f $d/sig && ! -f $d/cluster ]] || continue
        nf=$((nf + 1))
        [[ $(cluster_one "${d%/}" "$r" "$now") == new ]] && new=$((new + 1))
    done
    echo "$nf $new"
}

# ---------------------------------------------------------------- a round
round() {
    local r t0 cpu0 cpu1 nf new
    r=$(( $(cat "$OUT/round" 2>/dev/null || echo 0) + 1 ))
    mkdir -p "$OUT/rounds/$r"
    t0=$SECONDS
    local last; last=$(cat "$OUT/build.last" 2>/dev/null)
    prep_tree || return 1
    log "round $r: build $B ($(git -C "$TREE" log -1 --format=%s | cut -c1-60)), jobs $(jobs_now)"
    echo "$B" > "$OUT/rounds/$r/build"
    if [[ -n $last && $last != "$B" ]]; then
        log "round $r: the build moved ($last to $B): every cluster's repros again"
        recheck_all "$B" "$r"
    fi
    echo "$B" > "$OUT/build.last"
    plan_round "$r" "$sessions"
    log "round $r: $(cut -f1 "$OUT/rounds/$r/plan.tsv" | sort | uniq -c | awk '{printf "%s %s units, ", $1, $2}')$(awk -F'\t' '{s += $3} END {print s}' "$OUT/rounds/$r/plan.tsv") sessions planned"
    local tu=$SECONDS
    mkdir -p "$TREE/out/playfuzz"
    cat "$TREE/out/playfuzz/chain-sessions.tsv" 2>/dev/null | wc -l > "$OUT/rounds/$r/chain-offset"
    run_units "$r"
    local wall=$((SECONDS - tu))
    collect "$r"
    # every FAIL attributed, several at once
    awk -F'\t' '$7 != "PASS"' "$OUT/rounds/$r/sessions.tsv" > "$OUT/rounds/$r/fails.tsv"
    export -f attribute write_repro fail_dim jobs_now; export OUT TREE here
    awk -F'\t' '{print $1 "\t" $2 "\t" $3 "\t" $4 "\t" $10}' "$OUT/rounds/$r/fails.tsv" \
        | tr '\n' '\0' | xargs -0 -r -P "$(jobs_now)" -I{} bash -c 'IFS=$'"'"'\t'"'"' read -r a b c d e <<< "$1"; attribute "$a" "$b" "$c" "$d" "$e"' _ {}
    read -r nf new < <(with_causes cluster_round "$r")
    # the sweep's own CPU (the pool's JVMs are not its children: not counted)
    local cpu
    cpu=$(cat "$OUT/rounds/$r/units"/*.out 2>/dev/null | awk '/^unit-cpu/ {s += $2 + $3} END {printf "%d", s}')
    rate_round "$r" "$wall" "$cpu"
    cleanup_round "$r"
    echo "$r" > "$OUT/round"
    log "round $r done: $(awk -F'\t' '{n++; p += $7 == "PASS"; w += $8} END {printf "%d sessions, %d PASS, %d FAIL, %d rows", n, p, n - p, w}' "$OUT/rounds/$r/sessions.tsv"), $nf fails in ${new:-0} new clusters, $((SECONDS - t0)) s ($wall s of play), ${cpu} CPU s"
}

rate_round() { # R WALL CPU
    local r=$1 now; now=$(date '+%F %H:%M')
    local b; b=$(cat "$OUT/rounds/$r/build")
    for fz in chain agent raw all; do
        local nnew=0
        if [[ -f $OUT/rounds/$r/new ]]; then
            nnew=$(while read -r id; do awk -F'\t' -v id="$id" '$1 == id {print $7}' "$OUT/causes.tsv"; done < "$OUT/rounds/$r/new" \
                | awk -v f="$fz" 'f == "all" || $1 == f' | wc -l)
        fi
        awk -F'\t' -v OFS='\t' -v f="$fz" -v r="$r" -v now="$now" -v b="$b" -v nw="$nnew" -v wall="$2" -v cpu="$3" '
            f == "all" || $2 == f { n++; rows += $8; p += $7 == "PASS" }
            END { print r, now, b, f, n + 0, rows + 0, p + 0, n - p, nw, (f == "all" ? wall : "-"), (f == "all" ? cpu : "-") }' \
            "$OUT/rounds/$r/sessions.tsv" >> "$OUT/rate.tsv"
    done
}

# what a round's judged sessions leave in the sweep's tree once the finds
# hold their copies
cleanup_round() {
    local r=$1 fz c i st n
    while IFS=$'\t' read -r _ fz c i _; do
        st=${c%%:*} n=${c##*:}
        case $fz in
            chain) rm -rf "$TREE/out/human/pfc-$st-$n" "$TREE/out/java/run/play-pfc-$st-$n-"* "$TREE/out/java/run/pfc-$st-$n-"* "$TREE/out/java/diverge/pfc-$st-$n-"*;;
            agent) rm -rf "$TREE/out/java/rlfuzz/rf-$st-$n" "$TREE/out/java/diverge/rf-$st-$n";;
            raw) rm -rf "$TREE/out/native/rawjudge/rf-$st-$n" "$TREE/out/java/diverge/check";;
        esac
    done < "$OUT/rounds/$r/fails.tsv"
    # a passing raw session keeps its tapes and goldens in run.sh; the
    # sweep's sessions.tsv is the record
    while IFS=$'\t' read -r _ fz c _ _ _ res _; do
        [[ $fz == raw ]] || continue
        rm -rf "$TREE/out/java/rawfuzz/rf-${c%%:*}-${c##*:}"
        [[ $res == PASS ]] && rm -rf "$TREE/out/native/rawjudge/rf-${c%%:*}-${c##*:}"
    done < "$OUT/rounds/$r/sessions.tsv"
}

# ---------------------------------------------------------------- status
status() {
    local n=$1
    echo "sweep: $OUT, tree $TREE at $(cat "$OUT/build.last" 2>/dev/null || echo -), jobs $(jobs_now)"
    echo "last rounds (sessions rows PASS FAIL new, per fuzzer; wall and sweep CPU seconds):"
    awk -F'\t' 'NR > 1 { print $1 }' "$OUT/rate.tsv" | sort -un | tail -n "$n" | while read -r r; do
        awk -F'\t' -v r="$r" '$1 == r {
            if ($4 == "all") all = sprintf("%s  %s  round %s on %s: %d sessions, %d rows, %d PASS, %d FAIL, %d new; %s s, %s CPU s (%.1f per session)",
                "", $2, r, $3, $5, $6, $7, $8, $9, $10, $11, $5 ? $11 / $5 : 0)
            else parts = parts sprintf("    %-5s %4d sessions %8d rows %4d PASS %3d FAIL %2d new\n", $4, $5, $6, $7, $8, $9) }
            END { print all; printf "%s", parts }' "$OUT/rate.tsv"
    done
    echo "open clusters (not assigned):"
    awk -F'\t' 'NR > 1 && $2 == "open" { printf "  %s  %s sessions, first round %s (%s), last %s  %s %s %s %s %s %s\n    %s\n", $1, $6, $3, $4, $5, $7, "dim " $8, $9, $10, $11, ($12 == "-" ? "" : $12), $13 }' "$OUT/causes.tsv"
    echo "assigned, fixed and infra (harness failures):"
    awk -F'\t' 'NR > 1 && $2 != "open" { printf "  %s  %s  (%s sessions) %s %s %s %s\n", $1, $2, $6, $7, $9, $10, $11 }' "$OUT/causes.tsv"
    echo "new causes per 1,000 sessions:"
    awk -F'\t' 'NR > 1 && $4 == "all" { printf "  round %s  %4d sessions  %2d new  %6.1f per 1,000\n", $1, $5, $9, $5 ? 1000 * $9 / $5 : 0 }' "$OUT/rate.tsv" | tail -n "$n"
    awk -F'\t' 'NR > 1 && $4 != "all" { s[$4] += $5; w[$4] += $9 } END { for (f in s) printf "  %-5s all rounds: %5d sessions, %3d new, %6.1f per 1,000\n", f, s[f], w[f], s[f] ? 1000 * w[f] / s[f] : 0 }' "$OUT/rate.tsv" | sort
    # per start: every round's sessions against the clusters first seen there
    echo "  per start (all rounds; sessions, clusters first found there, per 1,000):"
    {
        cat "$OUT/rounds"/*/sessions.tsv 2>/dev/null | awk -F'\t' '{ print "S\t" $2 ":" $5 }'
        for f in "$OUT/rounds"/*/new; do
            [[ -f $f ]] || continue
            while read -r id; do
                local nm; nm=$(ls "$OUT/finds/$id" 2>/dev/null | grep -v checkpoints | head -1)
                [[ -n $nm ]] && awk '/^fuzzer / {f = $2} /^case / {split($2, a, ":"); c = a[1]} END {print "N\t" f ":" c}' "$OUT/finds/$id/$nm/info"
            done < "$f"
        done
    } | awk -F'\t' '$1 == "S" { s[$2]++ } $1 == "N" { w[$2]++ } END { for (k in s) printf "    %-24s %5d sessions %3d new %7.1f\n", k, s[k], w[k], 1000 * w[k] / s[k] }' | sort -k4,4nr -k2,2nr | head -n 25
}

# ---------------------------------------------------------------- main
case $mode in
    status) status "$nstat"; exit 0;;
    assign) with_causes set_status "$aid" "assigned $alane" "assigned to $alane $(date '+%F %H:%M')"; grep "^$aid	" "$OUT/causes.tsv"; exit 0;;
    reopen) with_causes set_status "$aid" open "reopened by hand $(date '+%F %H:%M')"; grep "^$aid	" "$OUT/causes.tsv"; exit 0;;
    recount) # a round's sessions.tsv and rate lines again from its units' output
             off=$(awk -F'\t' -v r="$rr" '$1 == r && $4 == "all" { print $10 "\t" $11 }' "$OUT/rate.tsv" | tail -n 1)
             collect "$rr"
             awk -F'\t' -v r="$rr" '$1 != r' "$OUT/rate.tsv" > "$OUT/rate.tsv.new" && mv "$OUT/rate.tsv.new" "$OUT/rate.tsv"
             rate_round "$rr" "$(cut -f1 <<< "$off")" "$(cut -f2 <<< "$off")"
             sort -t$'\t' -k1,1n -s "$OUT/rate.tsv" | awk 'NR == 1 || $1 != "round"' > "$OUT/rate.tsv.new"
             { head -n 1 "$OUT/rate.tsv"; grep -v '^round' "$OUT/rate.tsv.new"; } > "$OUT/rate.tsv.new2"; mv "$OUT/rate.tsv.new2" "$OUT/rate.tsv"; rm -f "$OUT/rate.tsv.new"
             grep "^$rr	" "$OUT/rate.tsv"; exit 0;;
    recheck) prep_tree || exit 1
             r=$(( $(cat "$OUT/round" 2>/dev/null || echo 0) + 1 )); mkdir -p "$OUT/rounds/$r"
             recheck_all "$B" "$r"; echo "$B" > "$OUT/build.last"
             # a repro that failed with another signature is a find of round R's
             [[ -d $OUT/rounds/$r/fails ]] && with_causes cluster_round "$r" > /dev/null
             exit 0;;
esac
# (the lock's descriptor is closed for everything a round starts: a pool
# member a fuzzer starts outlives the round and would hold the lock)
exec 9> "$OUT/.sweep.lock"
flock -n 9 || { echo "sweep: another sweep holds $OUT/.sweep.lock"; exit 1; }
# the rounds run from a copy of these scripts (out/sweep/bin): bash reads a
# script as it runs it, so an edit, a merge or a checkout of the tree they
# came from would change a sweep mid-round
if [[ $here != "$OUT/bin" ]]; then
    mkdir -p "$OUT/bin"
    cp "$here"/sweep.sh "$here"/sig.sh "$OUT/bin/"
    exec 9>&-
    exec bash "$OUT/bin/sweep.sh" --master "$M" "${args[@]}"
fi
done_rounds=0 failed=0
while (( rounds == 0 || done_rounds < rounds )); do
    if ! round 9>&-; then
        failed=$((failed + 1))
        # --rounds N is a bounded run: three rounds that cannot start end it
        (( rounds > 0 && failed >= 3 )) && { log "sweep: three rounds did not start; stopping"; exit 1; }
        log "sweep: the round did not run; again in 5 minutes"; sleep 300; continue
    fi
    failed=0
    done_rounds=$((done_rounds + 1))
    # a plant is for one round
    plant=
done
