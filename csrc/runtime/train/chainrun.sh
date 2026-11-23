#!/bin/bash
# chainrun.sh NAME RUN [--lib DIR] [--chain-bin FILE] [--seed K] [--n N] [--groups G] [--threads T] [--stall MIN] [--rung-stall MIN] [--hours H]
#             [--toward X,Z] [--resume-k K] [--initial-ids A,B,...] [--fresh-memory 0|1]
#             [--verify-from CHECKPOINT] [--verify-above RUNG_NUMBER] [--adopt-pid PID --part P]
#             [-- MORE SEARCH ARGS]   (lane/chain2)
# --verify-above is for a continuation whose --from node already has an external Java EQUAL proof. It only schedules
# new rungs above that number; it does not call any copied node Java EQUAL. The caller keeps the source proof receipt.
#
# A whole search from a run file's start to a proved dragon kill, unattended:
#   - `chain search` with the ladder lane/chain learned (LADDER and MOPTS below, every requirement on from the start)
#     into ~/.cache/netherite-native/chain/NAME-1;
#   - a new furthest rung is a line of out/chainrun/NAME/events.log (the clock, the wall time since the start), and its
#     earliest node is proved on the Java client in the background (chainverify.sh, one Java verifier on the host at a
#     time: flock out/chain/.verify.lock); a DIFF goes to the part's bad list (the search reads it: that node and its
#     descendants are drawn no more) and to events.log with its row and field;
#   - when the search dies (a crash, the host) or stalls (no new node at the top rung for --stall minutes, default 40;
#     --rung-stall also caps time without a higher rung even when same-rung siblings keep saving),
#     the next part resumes from the last one's best nodes (--resume, its groups held back while the chains replay:
#     a few minutes), into NAME-2, NAME-3 ...;
#   - a dragon_dead node is proved at once; at VERIFY EQUAL the search stops, and the kill is kept in
#     ~/.cache/netherite-native/dragon-kill/NAME (the stitched script, world.yaml, Java's recording, episode.nrec: the
#     labelled steps) with a line in its index.tsv. The last line of events.log is KILL or GAVE UP.
# --hours H gives up after H hours (default 8). Run it detached (setsid nohup); it writes out/chainrun/NAME/pid.
set -u
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root" || exit 2
name=$1 run=$2
shift 2
lib=$root/out/runtime chain_bin=$root/out/runtime/train/chain seed=1 n=128 groups=8 threads=20 stall=40 rung_stall=0 hours=8 toward="" verify=1 from="" frombad="" part0=0 t0arg="" vfree=8 resume_k=48 vfrom="" initial_ids="" adopt_pid="" fresh_memory=0 verify_above=-1
extra=() over=()
while [ $# -gt 0 ]; do
    case $1 in
        --lib) lib=$(cd "$2" && pwd); shift 2 ;;
        --chain-bin) [ -x "$2" ] || { echo "chainrun: chain binary missing: $2"; exit 2; }
                     chain_bin=$(realpath "$2"); shift 2 ;;
        --seed) seed=$2; shift 2 ;;
        --n) n=$2; shift 2 ;;
        --groups) groups=$2; shift 2 ;;
        --threads) threads=$2; shift 2 ;;
        --stall) stall=$2; shift 2 ;;
        --rung-stall) rung_stall=$2; shift 2 ;;
        --hours) hours=$2; shift 2 ;;
        --toward) toward=$2; shift 2 ;;
        --from) from=$2; shift 2 ;;
        --from-bad) frombad=$2; shift 2 ;;
        --verify-from) [ -f "$2/manifest.json" ] && [ -f "$2/save/level.dat" ] ||
                           { echo "chainrun: FROM is not a complete checkpoint: $2"; exit 2; }
                       vfrom=$(realpath -e "$2") || exit 2; shift 2 ;;
        --verify-above) verify_above=$2; shift 2 ;;
        --resume-k) resume_k=$2; shift 2 ;;
        --fresh-memory) fresh_memory=$2; shift 2 ;;
        --initial-ids) initial_ids=$2; shift 2 ;;
        --part) part0=$2; shift 2 ;;
        --adopt-pid) adopt_pid=$2; shift 2 ;;
        --t0) t0arg=$2; shift 2 ;;
        --verify-free) vfree=$2; shift 2 ;;
        --no-verify) verify=0; shift ;;
        --prove) prove=1; shift ;;
        --mopt) over+=("$2"); shift 2 ;;
        --) shift; extra=("$@"); break ;;
        *) echo "chainrun: unknown $1"; exit 2 ;;
    esac
done
[ -f "$run" ] || { echo "chainrun: run file missing: $run"; exit 2; }
[[ $fresh_memory == 0 || $fresh_memory == 1 ]] || { echo "chainrun: --fresh-memory needs 0 or 1"; exit 2; }
if ! [[ $verify_above =~ ^-?[0-9]+$ ]] ||
   { [ "$verify_above" -ne -1 ] && { [ "$verify_above" -lt 0 ] || [ -z "$from" ]; }; }; then
    echo "chainrun: --verify-above needs a nonnegative rung and --from" >&2
    exit 2
fi
run_from=$(awk '$1 == "#" && $2 == "oracle_from:" {print $3; exit}' "$run")
run_start=$(awk '$1 == "start:" {print $2; exit}' "$run")
case $run_start in
    */out/chain/starts/*/start)
        [ -n "$run_from" ] || { echo "chainrun: checkpoint-derived pipeline.start has no # oracle_from in $run"; exit 2; } ;;
esac
if [ -n "$run_from" ]; then
    case $run_from in /*) ;; *) echo "chainrun: oracle_from must be absolute: $run_from"; exit 2 ;; esac
    [ -f "$run_from/manifest.json" ] && [ -f "$run_from/save/level.dat" ] ||
        { echo "chainrun: oracle_from is not a complete checkpoint: $run_from"; exit 2; }
    run_from=$(realpath -e "$run_from") || exit 2
    [ -z "$vfrom" ] || [ "$vfrom" = "$run_from" ] ||
        { echo "chainrun: --verify-from differs from run file oracle_from"; exit 2; }
    vfrom=$run_from
fi
# the ladder (chain.c's flags), each requirement lane/chain found on the way to its kill, on from the start
LADDER=(--seg 180 --every 30 --meat 16 --cell 32 --keep 16 --live 96 --provisions 40 --carry 20 --kit 1 --dayff 2
        --spent 48 --late-seg 2500 --endfood 40 --end-seg 5000)
[ -n "$toward" ] && LADDER+=(--toward "$toward")
# the macros' options (vecopts.c), the privileged ones among them (the oracles) off by default
MOPTS=(dia_first=on food_scout=walk food_eat=any food_break=on bportal_buckets=any obsidian_oracle=on bportal_oracle=on
       nether_sweep=minus obsidian_dig_skip=on food_trips=many food_hunt=stock food_cook=furnace food_chase=far
       food_oracle=on nether_oracle=home obsidian_early_fill=on pearl_oracle=on pearl_reachable=on surface_ceiling=dig
       obsidian_carry=on nether_portal_home=on diamond_oracle=on stronghold_oracle=locate dragon_crystals=snowball
       armor_kit=iron snow_kit=far snow_first=on pearl_trim=on food_swim=on snow_heal=on sword_kit=on pearl_home=on
       pearl_target=any pearl_armor=on food_night=pearl dragon_food=fetch armor_fuel=coal furnace_kit=carry)
# --mopt NAME=WORD: that option's word instead of the list's (lane/chain2: food_hunt=stock32 for a fed End)
for o in "${over[@]}"; do
    keep=()
    for m in "${MOPTS[@]}"; do [ "${m%%=*}" = "${o%%=*}" ] || keep+=("$m"); done
    MOPTS=("${keep[@]}" "$o")
done
mo=()
for m in "${MOPTS[@]}"; do mo+=(--mopt "$m"); done
ch=$chain_bin
cache=${NETHER_CACHE:-.cache}/native
out=$root/out/chainrun/$name
if [ -z "$adopt_pid" ] && [ "${prove:-0}" != 1 ]; then
    [ ! -e "$cache/chain/$name-$((part0 + 1))" ] ||
        { echo "chainrun: refusing to replace existing search part $cache/chain/$name-$((part0 + 1))"; exit 2; }
    if [ -e "$out" ]; then
        same_part=$cache/chain/$name-$part0
        if [ "$part0" -le 0 ] || [ -z "$from" ] || [ ! -f "$from/nodes.tsv" ] ||
           [ "$(realpath -e "$from")" != "$(realpath -e "$same_part" 2>/dev/null)" ]; then
            echo "chainrun: refusing to reuse $name; choose a new name or --part P --from its own part P" >&2
            exit 2
        fi
    fi
fi
mkdir -p "$out/verify" "$root/out/chain"
ev=$out/events.log
echo $$ > "$out/pid"
t0=${t0arg:-$(date +%s)}
say() { echo "$(date '+%-I:%M:%S %p') +$(( ($(date +%s) - t0) / 60 ))m $*" | tee -a "$ev"; }
export CUDA_VISIBLE_DEVICES=
say "START $name: run $run, lib $lib, seed $seed, $n envs in $groups groups on $threads threads, stall $stall min, give up at $hours h, verify above $verify_above"

src="" srcbad="" part=$part0 spid="" vpid="" vnode="" vrung="" top=-1 topname="" last_new=$(date +%s) best_n=0 resuming=0
last_rung=$last_new
start_part() {
    part=$((part + 1))
    local d=$cache/chain/$name-$part prev=$cache/chain/$name-$((part - 1)) res=()
    local pbad=$out/bad-$((part - 1)).txt
    # --from DIR: the first part resumes from another search's directory (a driver started again)
    [ -n "$from" ] && [ $part -eq $((part0 + 1)) ] && prev=$from pbad=${frombad:-/dev/null}
    # a part that died or was stopped young (under 100 nodes) is not resumed from: the last part that held more is
    # (s42k2: part 9 began at spawn after part 8 died at its arguments)
    if [ -n "$src" ] && [ "$(cat "$prev/nodes.tsv" 2>/dev/null | wc -l)" -lt 100 ]; then prev=$src pbad=$srcbad; fi
    [ "$(cat "$prev/nodes.tsv" 2>/dev/null | wc -l)" -ge 100 ] && src=$prev srcbad=$pbad
    [ ! -e "$d" ] || { say "REFUSE part $part: $d already exists"; exit 2; }
    if [ -f "$prev/nodes.tsv" ]; then
        res=(--resume "$prev" --resume-k "$resume_k" --resume-bad "$pbad" --fresh-memory "$fresh_memory")
        [ "$part" -eq $((part0 + 1)) ] && [ -n "$initial_ids" ] && res+=(--resume-ids "$initial_ids")
        resuming=1
    else
        resuming=0
    fi
    touch "$out/bad-$part.txt"
    LD_LIBRARY_PATH=$lib setsid nohup "$ch" search --run "$run" --dir "$d" --seed $((seed * 100 + part)) --n "$n" \
        --groups "$groups" --threads "$threads" "${LADDER[@]}" "${mo[@]}" --bad "$out/bad-$part.txt" \
        --death-log "$out/deaths-$part.tsv" "${res[@]}" \
        "${extra[@]}" > "$out/search-$part.log" 2>&1 < /dev/null &
    spid=$!
    dir=$d
    last_new=$(date +%s)
    last_rung=$last_new
    best_n=0
    say "PART $part: pid $spid, dir $d${res:+, resumed from $prev}"
}

# the verifier's result: events, the bad list, the kill kept
verify_done() {
    local r
    r=$(tail -1 "$out/verify/$vnode.log" 2>/dev/null)
    say "VERIFY part $part node $vnode ($vrung): $r"
    local vd=$out/verify/$part-$vnode
    case $r in
        "VERIFY EQUAL"*)
            if [ "$vrung" = dragon_dead ]; then
                keep_kill "$vd" || { say "ARCHIVE FAIL part $part node $vnode: proof is EQUAL but its archive is incomplete"; return 1; }
                echo "$vrung" >> "$out/good-$part.txt"
                return 0
            fi
            echo "$vrung" >> "$out/good-$part.txt"
            # Keep the Java recording for the highest proved rung. Earlier EQUAL proofs retain their scripts,
            # world files and receipts but give up their large recording and row dumps when a higher one lands.
            local rank old_rank old_vd
            rank=$(awk -F'\t' -v id="$vnode" '$1 == id {print $3; exit}' "$dir/nodes.tsv")
            old_rank=-1 old_vd=""
            [ -f "$out/furthest-proof" ] && read -r old_rank old_vd < "$out/furthest-proof"
            if [ -n "$rank" ] && [ "$rank" -gt "$old_rank" ]; then
                case "$old_vd" in
                    "$out"/verify/*) [ "$old_vd" = "$vd" ] || rm -rf "$old_vd/java" "$old_vd"/*.rows* ;;
                esac
                printf '%s\t%s\n' "$rank" "$vd" > "$out/furthest-proof"
                rm -f "$vd"/*.rows*
            else
                rm -rf "$vd/java" "$vd"/*.rows*
            fi ;;
        "VERIFY FAIL"*"CHECK DIFF"*) echo "$vnode" >> "$out/bad-$part.txt"; rm -rf "$vd/java" "$vd"/*.rows* ;;
        "VERIFY DIFF"*)
            # the node whose own segment holds the row (a segment is its reset step and its steps)
            local row who
            row=$(sed -n 's/.* at row \([0-9]*\).*/\1/p' <<< "$r")
            who=$("$ch" path "$dir" "$vnode" 2>/dev/null |
                  awk -v row="$row" '$1 == "node" {t += 1 + $5; if (!f && t >= row - 2) {print $2; f = 1}}')
            echo "${who:-$vnode}" >> "$out/bad-$part.txt"
            read -r top topname best_n < <(part_top)
            last_rung=$(date +%s)
            last_new=$last_rung
            say "DIFF: node ${who:-$vnode} and its descendants dropped; the proof kept in $vd (java/, the rows)" ;;
        *) rm -rf "$vd/java" "$vd"/*.rows* ;;
    esac
    return 1
}

# Count only nodes whose whole ancestry remains eligible after a Java DIFF.
part_top() {
    awk -F'\t' -v badfile="$out/bad-$part.txt" '
        BEGIN {
            while ((getline line < badfile) > 0)
                if (line ~ /^[0-9]+$/) bad[line + 0] = 1
            name = "spawn"
        }
        $1 ~ /^[0-9]+$/ {
            id = $1 + 0; parent = $2 + 0
            if (bad[parent]) bad[id] = 1
            if (bad[id]) next
            if ($3 > rung) {rung = $3; name = $4; count = 0}
            if ($3 == rung) count++
        }
        END {print rung + 0, name, count + 0}
    ' "$dir/nodes.tsv"
}

keep_kill() {
    local vd=$1 k=$cache/dragon-kill/$name
    local ticks rows wall seedw diffw run_start
    LD_LIBRARY_PATH=$lib "$ch" path "$dir" "$vnode" > "$vd/path.txt" || return 1
    ticks=$(awk '$1 == "node" {t += 1 + $5} END {if (t > 0) print t}' "$vd/path.txt")
    rows=$(sed -n 's/^VERIFY EQUAL [0-9]* \([0-9]*\) rows.*/\1/p' "$out/verify/$vnode.log" | tail -1)
    [ -n "$ticks" ] && [ "$ticks" = "$rows" ] || return 1
    mkdir -p "$k"
    rm -f "$vd"/sim.rows "$vd"/java.rows "$vd"/java.rows.all
    cp -r "$vd"/. "$k/" || return 1
    run_start=$(awk '$1 == "start:" {print $2; exit}' "$run")
    [ -n "$run_start" ] && [ -d "$run_start" ] || return 1
    mkdir -p "$k/start" || return 1
    cp -a "$run_start"/. "$k/start"/ || return 1
    if [ -n "$vfrom" ]; then
        mkdir -p "$k/checkpoint" || return 1
        cp -a "$vfrom"/. "$k/checkpoint"/ || return 1
        if [ -f "$k/start/tape.jsonl" ]; then
            head -1 "$k/start/tape.jsonl" | jq -c --arg dir "$k/checkpoint" '.start.dir = $dir' > "$k/start/tape.new.jsonl" || return 1
            tail -n +2 "$k/start/tape.jsonl" >> "$k/start/tape.new.jsonl" || return 1
            mv "$k/start/tape.new.jsonl" "$k/start/tape.jsonl" || return 1
        fi
    fi
    awk -v start="$run_start" -v archived="$k/start" -v from="$vfrom" -v cp="$k/checkpoint" '
        $1 == "start:" && $2 == start {sub(start, archived)}
        $1 == "#" && $2 == "oracle_from:" && $3 == from && from != "" {sub(from, cp)}
        {print}
    ' "$run" > "$k/run.yaml" || return 1
    LD_LIBRARY_PATH=$lib "$ch" stitch "$dir" "$vnode" -o "$k/script.jsonl" --run "$k/run.yaml" --nrec "$k/episode.nrec" \
        > "$k/nrec.log" 2>&1 || return 1
    wall=$(( ($(date +%s) - t0) / 60 ))
    seedw=$(awk '$1 == "seed:" {print $2; exit}' "$k/world.yaml")
    diffw=$(awk '$1 == "difficulty:" {print $2; exit}' "$k/world.yaml")
    [ -f "$cache/dragon-kill/index.tsv" ] ||
        printf 'name\tseed\tdifficulty\tticks\tgame_min\twall_min\tproof\tnode\tsearch\n' > "$cache/dragon-kill/index.tsv"
    printf '%s\t%s\t%s\t%s\t%.1f\t%s\tVERIFY EQUAL %s rows\t%s\t%s\n' "$name" "$seedw" "${diffw:-normal}" "$ticks" \
        "$(echo "$ticks / 1200" | bc -l)" "$wall" "$rows" "$vnode" "$dir" >> "$cache/dragon-kill/index.tsv"
    say "KILL $name: node $vnode, $ticks ticks, proved EQUAL over $rows rows, $wall min of wall time; kept in $k"
    killed=1
}

# the next node to prove: an unproved dragon_dead node first, else the top rung's earliest unproved node (not under
# the bad list); one at a time
pick_verify() {
    local tsv=$dir/nodes.tsv
    [ -f "$tsv" ] || return 1
    # A higher EQUAL recording covers all of its own lower-rung ancestors. Keep the serial Java slot for a new
    # furthest rung or a kill instead of replaying every earlier rung after the top has already proved.
    local proved_floor=$verify_above
    if [ -s "$out/good-$part.txt" ]; then
        local good_floor
        good_floor=$(awk -F'\t' -v goodfile="$out/good-$part.txt" '
            FILENAME == goodfile {good[$1] = 1; next}
            $1 ~ /^[0-9]+$/ && good[$4] && $3 > floor {floor = $3}
            END {print floor + 0}
        ' "$out/good-$part.txt" "$tsv")
        [ "$good_floor" -gt "$proved_floor" ] && proved_floor=$good_floor
    fi
    # Node ids increase after their parents. Propagate Java-DIFF ancestry in one pass, then try the highest
    # healthy unproved rung. A historical bad top rung must not hide every later sibling, and a fixed first-20
    # window can contain only bad descendants after a divergence.
    awk -F'\t' -v badfile="$out/bad-$part.txt" -v goodfile="$out/good-$part.txt" -v floor="$proved_floor" '
        BEGIN {
            while ((getline line < badfile) > 0) bad[line + 0] = 1
            while ((getline line < goodfile) > 0) good[line] = 1
        }
        $1 ~ /^[0-9]+$/ {
            id = $1 + 0; parent = $2 + 0
            if (bad[parent]) bad[id] = 1
            if (!bad[id] && !good[$4] && $3 > floor) print $3 "\t" id "\t" $4
        }
    ' "$tsv" | sort -t $'\t' -k1,1nr -k2,2n > "$out/.candidates"
    local rung id picked_name
    while IFS=$'\t' read -r rung id picked_name; do
        grep -qx "$id" "$out/tried-$part.txt" 2>/dev/null && continue
        echo "$id" >> "$out/tried-$part.txt"
        vnode=$id
        vrung=$picked_name
        return 0
    done < "$out/.candidates"
    return 1
}

killed=0 sdone=""
# --prove (with --from DIR --part P): no search started; DIR's dragon_dead nodes proved as part P's, the kill kept
if [ -n "$adopt_pid" ]; then
    [[ $adopt_pid =~ ^[0-9]+$ ]] && [ "$part0" -gt 0 ] ||
        { echo "chainrun: --adopt-pid needs a numeric PID and --part P"; exit 2; }
    dir=$cache/chain/$name-$part0
    [ -d "$dir" ] && kill -0 "$adopt_pid" 2>/dev/null &&
        [ "$(readlink -f "/proc/$adopt_pid/cwd")" = "$root" ] &&
        [ "$(readlink -f "/proc/$adopt_pid/exe")" = "$ch" ] &&
        [ "$(tr '\0' '\n' < "/proc/$adopt_pid/environ" | awk -F= '$1 == "LD_LIBRARY_PATH" {print $2; exit}')" = "$lib" ] &&
        [[ $(tr '\0' ' ' < "/proc/$adopt_pid/cmdline") == *"search --run $run --dir $dir "* ]] ||
        { echo "chainrun: PID $adopt_pid is not this lane's search for $dir"; exit 2; }
    part=$part0 spid=$adopt_pid resuming=0
    read -r top topname best_n < <(part_top)
    say "ADOPT part $part: pid $spid, dir $dir, top $topname ($best_n nodes)"
elif [ "${prove:-0}" = 1 ]; then
    dir=$from spid=2147483646
    touch "$out/bad-$part.txt"
    say "PROVE $dir's dragon_dead nodes as part $part"
else
    start_part
fi
while :; do
    sleep 30
    now=$(date +%s)
    # the top rung and the nodes at it
    if [ -f "$dir/nodes.tsv" ]; then
        read -r t tn tc < <(part_top)
        if [ "${t:-0}" -gt "$top" ]; then
            top=$t topname=$tn best_n=0 last_rung=$now
            say "RUNG $tn (rung $t), part $part"
        fi
        # the stall's clock: the last new node at the top rung (a gain inside the rung saves one too: the iron and
        # armor toward iron_armor, the clock toward the night, the distance toward the stronghold, the dragon's health)
        # (lane/chain2: best_n kept the last rung's count over a new top, a false STALL 11 min after on_land)
        if [ "${t:-0}" -ge "$top" ] && [ "${tc:-0}" -gt "$best_n" ]; then best_n=$tc last_new=$now; fi
    fi
    [ "$resuming" = 1 ] && rg -q '^resume: the replays done at' "$out/search-$part.log" && resuming=0
    # the verifier
    if [ $verify = 1 ]; then
        if [ -n "$vpid" ] && ! kill -0 "$vpid" 2>/dev/null; then
            vpid=""
            verify_done && break
        fi
        # (a Java verifier only with --verify-free GB available on the host: the desktop's programs come first)
        if [ -z "$vpid" ] && [ "$resuming" = 0 ] &&
           [ "$(awk '/^MemAvailable/ {print int($2 / 1048576)}' /proc/meminfo)" -ge "$vfree" ] &&
           pick_verify; then
            LD_LIBRARY_PATH=$lib setsid flock -o out/chain/.verify.lock bash csrc/runtime/train/chainverify.sh "$run" \
                "$dir" "$vnode" "$out/verify/$part-$vnode" "$lib" "$vfrom" > "$out/verify/$vnode.log" 2>&1 < /dev/null &
            vpid=$!
            say "VERIFYING part $part node $vnode ($vrung)"
        fi
    fi
    # the search: dead, stalled, or out of time
    if [ $(( now - t0 )) -ge $(( hours * 3600 )) ]; then
        say "GAVE UP after $hours h: top $topname"
        kill "$spid" 2>/dev/null
        break
    fi
    # (lane/chain2: a search ends by itself once a node reaches dragon_dead; s42k2-27's kill was taken for a death and
    # part 28 started over it. Its proof first: the verifier on another node stopped, the dragon_dead nodes proved one
    # after another, a new part only when none is left to try)
    if [ "$verify" = 1 ] && ! kill -0 "$spid" 2>/dev/null &&
       awk -F'\t' '$4 == "dragon_dead" {f = 1} END {exit !f}' "$dir/nodes.tsv" 2>/dev/null; then
        if [ "$sdone" != "$part" ]; then
            sdone=$part
            say "SEARCH DONE part $part: a dragon_dead node; its proof next"
            [ -n "$vpid" ] && [ "$vrung" != dragon_dead ] && { kill -- -"$vpid" 2>/dev/null; kill "$vpid" 2>/dev/null; wait "$vpid" 2>/dev/null; vpid=""; }
            continue
        fi
        [ -n "$vpid" ] && continue
        say "no dragon_dead node of part $part proved; a new part"
        [ "${prove:-0}" = 1 ] && break     # (--prove: nothing to resume a search into; s42k2's rc 127 was its fake pid)
    fi
    if ! kill -0 "$spid" 2>/dev/null; then
        wait "$spid" 2>/dev/null
        say "DIED part $part (rc $?): $(tail -2 "$out/search-$part.log" | tr '\n' ' ' | cut -c1-200)"
        if rg -q '^FAIL: RESUME_MEMORY ' "$out/search-$part.log"; then
            say "REFUSE part $part: incompatible or missing macro memory; pass --fresh-memory 1 only for a deliberate fresh controller"
            exit 2
        fi
        [ -n "$vpid" ] && { kill "$vpid" 2>/dev/null; wait "$vpid" 2>/dev/null; vpid=""; }
        start_part
    elif { [ "$rung_stall" -gt 0 ] && [ $(( now - last_rung )) -ge $(( rung_stall * 60 )) ]; } ||
         [ $(( now - last_new )) -ge $(( stall * 60 )) ]; then
        if [ "$rung_stall" -gt 0 ] && [ $(( now - last_rung )) -ge $(( rung_stall * 60 )) ]; then
            say "STALL part $part: no higher rung than $topname in $rung_stall min; restarting from its best nodes"
        else
            say "STALL part $part: no new $topname node in $stall min; restarting from its best nodes"
        fi
        kill "$spid" 2>/dev/null
        wait "$spid" 2>/dev/null
        [ -n "$vpid" ] && { kill "$vpid" 2>/dev/null; wait "$vpid" 2>/dev/null; vpid=""; }
        start_part
    fi
done
[ $killed = 1 ] && kill "$spid" 2>/dev/null
# the parts' frontier tables beside the events
for p in $(seq 1 $part); do cp "$cache/chain/$name-$p/frontier.txt" "$out/frontier-$p.txt" 2>/dev/null; done
say "DONE $name (killed $killed)"
