#!/usr/bin/env bash
# chain: the fuzzer's sessions played the way a human plays a whole game,
# one after another, each from the Save and Quit of the one before.
#
#   bash csrc/tests/playfuzz/chain.sh [-j N] [-k K] CHAIN...    CHAIN is START:BASE[:K] (fresh:1 g6:3:12)
#   bash csrc/tests/playfuzz/chain.sh [-j N] [-k K] csrc/tests/playfuzz/chains.txt
#   -keep: a passing chain keeps its run and every session's check dir too
#   (pin.sh --chain pins a session that passes now and failed before a fix)
#
# A chain is a session.sh run, out/human/pfc-START-BASE (made anew), from
# START (fresh, gN: as run.sh's cases; sN for N in 1..22 is seed 1's
# golden checkpoint 1/SN, where the snapshot s1chain-S(N+1) starts, and s23
# is 1/S23, back at spawn after the End: out/java/playfuzz/start-s23)
# through K sessions (default 8), all played by out/native/play through its
# SDL input path (--no-draw: rows are the check, frames are judged
# elsewhere) and checked by
# csrc/play/session.sh without --no-chain: on a PASS Java's Save and Quit
# at the session's last tick, joined again, is the next session's start.
# Session I's script is playfuzz_ends BASE*100+I: playfuzz_gen's player for that seed,
# its end chosen so the save lands inside a state (a screen open, an item in
# the air, mid-fight, mid-dig, mid-use, in the air, in bed). About three
# sessions in five carry playfuzz_gen's kit (the speedrun actions, staged by Dev
# header entries): those play on play-dev, and the chain is a dev one from
# its first kit on (session.sh plays and joins a dev start as dev). A FAIL stops the
# chain and names the session; its run keeps everything for pin.sh --chain.
# After each PASS the saved state is read from the checkpoint (the manifest's
# quiescence), the next start's Snapshot (the save joined again) and Java's
# last row into tags.
# Every session is a line in out/playfuzz/chain-sessions.tsv:
#   CHAIN  SESSION  SEED  END  PASS|FAIL  ticks  rows  mismatches  seconds  tags|detail
# and every chain one line here and in out/playfuzz/chains.tsv:
#   CHAIN  PASS|FAIL  sessions-passed/K  rows  seconds  detail
set -uo pipefail
root=$(cd "$(dirname "$0")/../../.." && pwd)
cd "$root"
J=4 K=8 KEEP=0
while [[ ${1:-} == -* ]]; do
    case $1 in
        -j) J=$2; shift 2;;
        -k) K=$2; shift 2;;
        -keep) KEEP=1; shift;;
        *) sed -n '2,25p' "$0"; exit 2;;
    esac
done
[[ $# -ge 1 ]] || { sed -n '2,25p' "$0"; exit 2; }
export K KEEP

start_dir() {
    case $1 in
        fresh) echo out/java/snapshots/fresh-play-s1;;
        g30) echo out/java/playfuzz/start-g30;;
        g[0-9]*) local n=${1#g}; (( n >= 1 && n <= 29 )) && echo "out/java/snapshots/gold-g$((n + 1))-s42";;
        s23) echo out/java/playfuzz/start-s23;;
        s[0-9]*) local n=${1#s}; (( n >= 1 && n <= 22 )) && echo "out/java/snapshots/s1chain-S$((n + 1))";;
    esac
}

# the saved state of a checkpoint (CP, its next start's snapshot PRE, Java's
# last ROW), as space-separated tags
tags() {
    local cp=$1 row=$2 pre=$3
    {
        jq -r '(if .dimension == -1 then "nether" elif .dimension == 1 then "end" else "overworld" end),
               (.quiescence | (if .guiClosed then empty else "screen-open" end),
                              (if .onGround then empty else "airborne" end),
                              (if .noPortal then empty else "in-portal" end),
                              (if .noItemUse then empty else "item-in-use" end),
                              (if (.inFlight | length) > 0 then "projectile" else empty end))' "$cp/manifest.json"
        [[ -n $row ]] && jq -r '(.cp.gui // empty | "gui:" + .),
               (if (.cp.cur // null) != null then "cursor-stack" else empty end),
               (if (.sp.hp // 1) <= 0 then "dead" else empty end),
               (.act.in.keys // {} | (if (.["key.attack"] // [0])[0] == 1 then "attack-held" else empty end),
                                     (if (.["key.use"] // [0])[0] == 1 then "use-held" else empty end),
                                     (if (.["key.sneak"] // [0])[0] == 1 then "sneaking" else empty end),
                                     (if (.["key.forward"] // [0])[0] == 1 then "moving" else empty end))' <<< "$row"
        jq -r -s '(.[] | select(.player == 1 or .class == "EntityPlayerMP") | .rt.f |
                      (if ((.hurtTime // "i:0") | ltrimstr("i:") | tonumber) > 0 then "hurt" else empty end),
                      (if ((.fire // "i:0") | ltrimstr("i:") | tonumber) > 0 then "burning" else empty end),
                      (if .inWater == "b:1" then "in-water" else empty end),
                      (if .sleeping == "b:1" then "in-bed" else empty end)),
                  (if any(.[]; .class == "EntityItem" and .nbt.OnGround == "b:0" and ((.nbt.Age // "s:0") | ltrimstr("s:") | tonumber) < 100)
                   then "item-in-air" else empty end),
                  (if any(.[]; .class == "EntityItem") then "items-on-ground" else empty end),
                  (if any(.[]; .class | test("Arrow|Snowball|Egg|EnderPearl|Potion|ExpBottle|Fireball|EnderEye")) then "thrown-entity" else empty end),
                  (if any(.[]; .class == "EntityFallingBlock") then "falling-block" else empty end),
                  (if any(.[]; .class == "EntityXPOrb") then "xp-orb" else empty end),
                  ((.[] | select(.player == 1 or .class == "EntityPlayerMP") | .id) as $p |
                   if any(.[]; (.rt.f.attackTarget // "") == "str:e:\($p)" or (.rt.f.entityToAttack // "") == "str:e:\($p)")
                   then "mob-targeting-player" else empty end),
                  (if any(.[]; ((.rt.ai.nav.currentPath // "str:p:") != "str:p:") or ((.rt.f.pathToEntity // "str:p:") != "str:p:"))
                   then "mob-mid-path" else empty end)' "$pre/entities.jsonl" 2>/dev/null
        jq -r -s 'if any(.[]; .tiles[]? | select(.id == "str:Furnace") | (.BurnTime // "s:0") != "s:0") then "furnace-burning" else empty end' \
            "$pre/chunkstate.jsonl" 2>/dev/null
    } | sort -u | tr '\n' ' ' | sed 's/ $//'
}

one() {
    local c=$1 start base k
    IFS=: read -r start base k <<< "$c"
    k=${k:-$K}
    local run=pfc-$start-$base snap t0=$SECONDS
    snap=$(start_dir "$start")
    if [[ -z $snap || ! $base =~ ^[0-9]+$ ]]; then printf '%s\tFAIL\t0/%s\t0\t0\tno such chain\n' "$c" "$k"; return; fi
    local st=out/human/$run
    rm -rf "$st"
    local i rows=0 passed=0 detail=
    for ((i = 1; i <= k; i++)); do
        local seed=$((base * 100 + i)) script end s0=$SECONDS o line res tk r m det
        script=out/playfuzz/scripts/$run-s$i.jsonl
        # the start's hotbar: a usable and a thrown stack for the use and throw ends
        local hint=() sdir=$snap
        ((i > 1)) && sdir=$st/start/$run-s$i
        [[ -f $sdir/entities.jsonl ]] && read -r us ts < <(jq -r 'select(.player == 1 or .class == "EntityPlayerMP") | .nbt.Inventory // [] |
            map({s: (.Slot | ltrimstr("b:") | tonumber), id: (.id | ltrimstr("s:") | tonumber)}) | map(select(.s < 9)) as $h |
            ([$h[] | select(.id as $x | [260,282,297,319,320,322,349,350,357,360,363,364,365,366,367,391,392,393,400] | index($x))] +
             (if any($h[]; .id == 261) and any(.[]; .id == 262) then [$h[] | select(.id == 261)] else [] end)) as $u |
            [$h[] | select(.id as $x | [332,344,368,373,381,384] | index($x))] as $t |
            "\(if ($u | length) > 0 then $u[0].s + 1 else 0 end) \(if ($t | length) > 0 then $t[0].s + 1 else 0 end)"' "$sdir/entities.jsonl" | head -1)
        # and the whole inventory, for the container end's block (a chest, a furnace, a workbench)
        [[ -f $sdir/entities.jsonl ]] && hint+=(--inv "$(jq -r 'select(.player == 1 or .class == "EntityPlayerMP") | .nbt.Inventory // [] |
            map("\(.Slot | ltrimstr("b:")):\(.id | ltrimstr("s:")):\(.Count | ltrimstr("b:"))") | join(",")' "$sdir/entities.jsonl" | head -1)")
        [[ ${us:-0} != 0 ]] && hint+=(--use-slot "$us")
        [[ ${ts:-0} != 0 ]] && hint+=(--throw-slot "$ts")
        # playfuzz_gen's kit (the speedrun actions): the player at the start (the
        # snapshot's row 1) places its scene; a kit session plays on play-dev
        # with its Dev entries, and the chain is a dev one from there on
        local pose kit=${script%.jsonl}.kit.jsonl devops=()
        pose=$(sed -n 3p "$sdir/tape.jsonl" | jq -r '[.sp.x, .sp.y, .sp.z, .cp.yaw, .cp.pitch, .sp.dim] | map(tostring) | join(" ")')
        out/native/playfuzz_ends "$seed" ${hint[@]+"${hint[@]}"} --pose "$pose" --kit "$kit" > "$script"
        [[ -s $kit ]] && devops=(--dev-ops "$kit")
        end=$(out/native/playfuzz_ends "$seed" --which)
        o=$(bash csrc/play/session.sh --run "$run" --from "$snap" --no-build --no-draw --input-script "$script" ${devops[@]+"${devops[@]}"} 2>&1)
        # the session is judged: its oracle runs' game directories (a save and
        # logs each: the replay's and the next start's) are read no more
        rm -rf "out/java/run/play-$run-s$i-1.replay" "out/java/run/$run-s$((i + 1))-tape"
        line=$(tail -1 "$st/sessions.tsv" 2>/dev/null)
        IFS=$'\t' read -r sn _ _ tk r m res det <<< "$line"
        if [[ $sn != "$i" || $res != PASS ]]; then
            [[ $sn == "$i" ]] || { res=FAIL tk=0 r=0 m=-; det=$(grep -a 'session:\|FAIL\|play:' <<< "$o" | tail -1); }
            [[ -f $st/play/$run-s$i-1.diverge ]] && det="$det [$st/play/$run-s$i-1.diverge]"
            printf '%s\t%s\t%s\t%s\tFAIL\t%s\t%s\t%s\t%s\t%s\n' "$c" "$i" "$seed" "$end" "${tk:-0}" "${r:-0}" "${m:--}" "$((SECONDS - s0))" "$det" >> out/playfuzz/chain-sessions.tsv
            detail="session $i: $det"
            break
        fi
        local cp=$st/cp/$run-s$i-1 last
        last=$(tail -1 "$st/play/$run-s$i-1.check/tape.jsonl" 2>/dev/null)
        printf '%s\t%s\t%s\t%s\tPASS\t%s\t%s\t%s\t%s\t%s\n' "$c" "$i" "$seed" "$end" "$tk" "$r" "$m" "$((SECONDS - s0))" "$(tags "$cp" "$last" "$st/start/$run-s$((i + 1))")" >> out/playfuzz/chain-sessions.tsv
        rows=$((rows + r)); passed=$((passed + 1))
        # what the next session no longer needs: this session's start (the
        # previous checkpoint's rejoin) and that checkpoint, and this
        # session's check dir (Java's rows stay in the oracle cache)
        ((KEEP)) || { ((i > 1)) && rm -rf "$st/start/$run-s$i" "$st/cp/$run-s$((i - 1))-1"; rm -rf "$st/play/$run-s$i-1.check"; }
    done
    # a PASS keeps nothing of its run (its lines are in chain-sessions.tsv),
    # a FAIL keeps out/human for pin.sh
    ((passed == k && !KEEP)) && rm -rf "$st"
    local res=PASS
    ((passed == k)) || res=FAIL
    printf '%s\t%s\t%s/%s\t%s\t%s\t%s\n' "$c" "$res" "$passed" "$k" "$rows" "$((SECONDS - t0))" "$detail"
}
export -f one start_dir tags

chains=()
for a in "$@"; do
    if [[ -f $a ]]; then while read -r l; do [[ $l =~ ^[a-z] ]] && chains+=("${l%%[[:space:]#]*}"); done < "$a"
    else chains+=("$a"); fi
done

make -s -C csrc play play-dev "$root/out/native/test_snapshots" "$root/out/native/diverge" "$root/out/native/playfuzz_ends" > /dev/null || { echo "chain: native build failed"; exit 1; }
make -s -C oracle build > /dev/null 2>&1 || { echo "chain: java build failed"; exit 1; }
mkdir -p out/playfuzz/scripts
# the starts past a chain's last golden segment (back at spawn after the
# End): the checkpoint joined again, its Snapshot at tick 2, made once
for s in g30:42/G30 s23:1/S23; do
    d=out/java/playfuzz/start-${s%%:*} cp=out/java/checkpoints/${s#*:}
    printf '%s\n' "${chains[@]}" | grep -q "^${s%%:*}:" && [[ ! -s $d/manifest.json ]] || continue
    mkdir -p "$d"
    echo '{"cmd":"run","class":"Snapshot"}' > out/playfuzz/snapshot.jsonl
    set=$(jq -r '.world | to_entries | map("\(.key)=\(.value)") | join(" ")' "$cp/manifest.json")
    make -s -C oracle script SET="$set" FROM="../$cp" SCRIPT=../out/playfuzz/snapshot.jsonl \
        TAPE="../$d/tape.jsonl" > "out/playfuzz/start-${s%%:*}.log" 2>&1 \
        || { echo "chain: the ${s%%:*} start was not made (out/playfuzz/start-${s%%:*}.log)"; exit 1; }
done
printf '%s\n' "${chains[@]}" | nice -n 10 xargs -P "$J" -I{} bash -c 'one "$1"' _ {} | tee -a out/playfuzz/chains.tsv \
    | awk -F'\t' '{print} $2=="PASS"{p++} {split($3, a, "/"); s += a[1]; r += $4}
        END{printf "chain: %d/%d chains PASS, %d sessions passed, %d rows checked\n", p, NR, s, r; exit p != NR}'
