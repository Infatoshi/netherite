#!/usr/bin/env bash
# A whole game on the native client across many sittings, every sitting
# checked on the Java oracle and chained to the next.
#
#   csrc/play/session.sh [PLAY ARGS...]                a new world: a new run, its session 1 (fresh-play-s1)
#   csrc/play/session.sh --continue [PLAY ARGS...]     the next session of the run you played last
#   csrc/play/session.sh --run NAME [PLAY ARGS...]     the next session of run NAME (a new run if none)
#   csrc/play/session.sh --list                        your runs, the last played first
#   csrc/play/session.sh [--run NAME] --status         sessions, rows, the last checkpoint, the End
#                                                        (without --run: the run you played last)
#   csrc/play/session.sh [--run NAME] --check TAPE     check a tape played from the next session's start
# A new world's run is named world-DATE-TIME; a run played by hand (not an
# --input-script, the fuzzers') is marked (out/human/NAME/human), which is
# what --continue, --list and --status look through.
#   --from SNAPSHOT: a new run's session 1 starts there (a golden chain
#   snapshot, csrc/tests/playfuzz), not at fresh-play-s1
#   --no-chain: check the session alone (no Save and Quit, no next start;
#   the fuzzer's one-session runs); --no-build: play and check with the
#   binaries as built, play inside the memory budget (the fuzzer builds
#   once, before its sessions)
#
# Session N starts from start/NAME-sN (session 1: out/java/snapshots/fresh-play-s1,
# a fresh seed-1 world). out/native/play writes tape N. The check has the Java
# oracle replay tape N's inputs from session N's start (make replay, the tape's
# header names the seed or checkpoint), write a row per tick and, after the
# last row, Save and Quit into cp/NAME-sN-A (SAVEEND); the native whole-server
# check (test_snapshots, and play --check: the client's own tick pair) then
# replays Java's rows from session N's snapshot and compares every field. On a
# PASS a second Java run joins that checkpoint again and writes its Snapshot at
# tick 2 into start/NAME-s(N+1): session N+1
# starts there, in play and in its check. On a FAIL out/native/diverge names
# the first differing row and field and session N stays next.
#
# The state is out/human/NAME (NAME defaults to game): next (the session to
# play), sessions.tsv (one line per checked attempt), start/, play/ (tapes,
# logs, check dirs), cp/. On the dev host (the canonical checkout; $home
# empty, csrc/play/host.sh) everything runs here. Anywhere else (the
# Mac) play runs locally and the rest on that host's checkout ($R):
# the start snapshot comes over with rsync, the tape goes back, and the check
# runs there. PLAY ARGS go to out/native/play (e.g. --input-script FILE, the
# headless SDL input path); --fault KIND@ROW plants a divergence in the native
# check (test_snapshots --fault), to prove that a failure stops the chain.
set -uo pipefail
root=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
cd "$root"
source csrc/play/host.sh
FIRST=out/java/snapshots/fresh-play-s1
# the check's two oracle JVMs (the replay, the next start) are short: C1 only
# spends a third less CPU than tiered C2 and writes the same bytes (oracle/Makefile JVMJIT)
JIT=-XX:TieredStopAtLevel=1

run= cont=0 human=0 from=0 mode=play tape= fault= chain=1 build=1
play_args=()
while [[ $# -gt 0 ]]; do
    case $1 in
        --run) run=$2; shift 2;;
        --continue) cont=1; shift;;
        --list) mode=list; shift;;
        --latest) mode=latest; shift;;
        --human) human=1; shift;;
        --status) mode=status; shift;;
        --check) mode=check; tape=$2; shift 2;;
        --start) mode=start; shift;;
        --fault) fault=$2; shift 2;;
        --from) FIRST=$2; from=1; shift 2;;
        --no-chain) chain=0; shift;;
        --no-build) build=0; shift;;
        -h|--help) sed -n '2,42p' "$0"; exit 0;;
        *) play_args+=("$1"); shift;;
    esac
done
# out/human/options.txt, when it exists (a Minecraft options.txt, 1.7.10's or
# a later version's: copy one from the launcher), is the person's own keys,
# fov, brightness and mouse speed (play --options: none of it is game state);
# a scripted session (--input-script, the fuzzers) keeps the default keys its
# script was written for
if [[ -f out/human/options.txt && " ${play_args[*]-} " != *" --input-script "* ]]; then
    play_args=(--options out/human/options.txt ${play_args[@]+"${play_args[@]}"})
fi
on_home() { [[ -z $home || $(hostname -s) == "$home" ]]; }

# the runs played by hand (out/human/NAME/human), the last played first: NAME
# and the time of its last check (or of its start)
human_runs() {
    local d t
    for d in out/human/*/; do
        [[ -f $d/human ]] || continue
        t=$(stat -c %Y "$d/sessions.tsv" 2>/dev/null || stat -c %Y "$d/next" 2>/dev/null || echo 0)
        printf '%s\t%s\n' "$t" "$(basename "$d")"
    done | sort -rn
}
latest_run() {
    if on_home; then human_runs | head -1 | cut -f2
    else ssh $home "cd $R && bash csrc/play/session.sh --latest"; fi
}
list_runs() {
    local t name p tk n
    human_runs | while IFS=$'\t' read -r t name; do
        read -r p tk < <(awk -F'\t' '$7 == "PASS" {p++; t += $4} END {print p + 0, t + 0}' "out/human/$name/sessions.tsv" 2>/dev/null)
        n=$(cat "out/human/$name/next" 2>/dev/null)
        printf '%-26s %3s sessions passed %8s ticks   next: session %-3s  last played %s\n' \
            "$name" "${p:-0}" "${tk:-0}" "${n:-1}" "$(date -d "@$t" '+%b %-d %-I:%M %p')"
    done
}
# which run: --run NAME; --continue, the run played by hand last; otherwise
# playing makes a new run from a fresh world, and --status and --check take
# the last one
if [[ -z $run && $mode != list && $mode != latest ]]; then
    if ((cont)) || [[ $mode != play ]]; then
        run=$(latest_run)
        [[ -n $run ]] || { echo "session: no run played by hand yet: csrc/play/session.sh starts a new world"; exit 2; }
    else
        run=world-$(date +%Y%m%d-%H%M%S)
    fi
fi
[[ $mode == list || $mode == latest || $run =~ ^[A-Za-z0-9_.-]+$ ]] || { echo "session: --run takes letters, digits, . _ -"; exit 2; }
st=out/human/$run
# a session played by hand (no --input-script) marks its run as a person's
[[ $mode == play && " ${play_args[*]-} " != *" --input-script "* ]] && human=1

# the next session's number, its next attempt and its start snapshot; the
# first call makes the run
next_start() {
    mkdir -p "$st/start" "$st/play" "$st/cp"
    if [[ ! -f $st/next ]]; then
        [[ -s $FIRST/manifest.json ]] || { echo "session: no $FIRST (make lane-share, or record it: oracle/tests/fresh-play-s1.jsonl)" >&2; return 1; }
        ln -sfn "$(readlink -f "$FIRST")" "$st/start/$run-s1"
        echo 1 > "$st/next"
        printf 'session\tattempt\ttape\tticks\trows\tmismatches\tresult\tdetail\n' > "$st/sessions.tsv"
    fi
    n=$(cat "$st/next")
    # an attempt is used once its check has a log (a played tape waits for its check)
    a=1
    while [[ -e $st/play/$run-s$n-$a.log ]]; do a=$((a + 1)); done
    echo "$n $a $st/start/$run-s$n"
}

# ------------------------------------------------------------------ status
status() {
    [[ -f $st/next ]] || { echo "$run: no sessions yet (csrc/play/session.sh --run $run starts one)"; return 0; }
    local n passed=0 ticks=0 rows=0 end=no credits=no dragon="not met yet" last=none
    n=$(cat "$st/next")
    while IFS=$'\t' read -r s a t tk r m res det; do
        [[ $s == session ]] && continue
        printf '  session %-3s attempt %-2s %-5s %6s ticks %6s rows %3s mismatches  %s\n' "$s" "$a" "$res" "$tk" "$r" "$m" "$det"
        [[ $res == PASS ]] || continue
        passed=$((passed + 1)); ticks=$((ticks + tk)); rows=$((rows + r))
        local java=$st/cp/$run-s$s-$a/tape.jsonl
        [[ -f $java ]] || continue
        # the rows: the player's world (sp.dim, 1 is the End) and the client's
        # screen (GuiWinGame: the credits, which only the End's exit portal
        # shows, and it is only there once the dragon has died)
        grep -q '"dim":1,' "$java" && end=yes
        if grep -q '"gui":"GuiWinGame"' "$java"; then
            credits="yes (session $s)"; dragon="dead (the exit portal was reached)"
        fi
        # the session's end state (the next session's start, the checkpoint
        # joined again; an older checkpoint's own pre/ Snapshot): with the End
        # loaded (worldServers index 2), a dragon there is alive, none is dead
        local ents=$st/start/$run-s$((s + 1))/entities.jsonl
        [[ -f $ents ]] || ents=$st/cp/$run-s$s-$a/pre/entities.jsonl
        if [[ $credits == no && -f $ents ]] && jq -e -s 'any(.[]; .dim == 2)' "$ents" > /dev/null 2>&1; then
            if jq -e -s 'any(.[]; .dim == 2 and .class == "EntityDragon")' "$ents" > /dev/null 2>&1; then
                dragon="alive at the end of session $s"
            else
                dragon="dead (none in the loaded End at the end of session $s)"
            fi
        fi
        last=$st/cp/$run-s$s-$a
    done < "$st/sessions.tsv"
    echo "$run: $passed sessions passed, $ticks ticks, $rows rows checked; next is session $n from $st/start/$run-s$n"
    echo "  last checkpoint: $last"
    echo "  reached the End: $end   dragon: $dragon   exit portal: $credits"
}

# ------------------------------------------------------------------ check (the canonical host)
check() {
    local src=$1
    mkdir -p "$st"
    exec 9> "$st/lock"
    flock -n 9 || { echo "session: another check of $run is running"; return 1; }
    # every command below runs with the lock closed (9>&-): a process that
    # outlives the check (a shared oracle pool member a run starts) must not
    # hold it, or the next check of the run finds it taken
    read -r n a snap < <(next_start) || return 1
    snap=$(readlink -f "$snap")
    local name=$run-s$n-$a
    local t=$st/play/$name.jsonl log=$st/play/$name.log chk=$st/play/$name.check cp=$st/cp/$name
    [[ $(readlink -f "$src") == $(readlink -f "$t" 2>/dev/null) ]] || cp "$src" "$t"
    : > "$log"
    local t0=$SECONDS
    result() {   # result RESULT TICKS ROWS MISMATCHES DETAIL
        printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$n" "$a" "$t" "$2" "$3" "$4" "$1" "$5" >> "$st/sessions.tsv"
        if [[ $1 == PASS ]] && ((!chain)); then
            echo "PASS session $n: $2 ticks, $3 rows, 0 mismatches ($((SECONDS - t0)) s), not chained"
        elif [[ $1 == PASS ]]; then
            echo "PASS session $n: $2 ticks, $3 rows, 0 mismatches ($((SECONDS - t0)) s); next: session $((n + 1)) from $st/start/$run-s$((n + 1))"
        else
            echo "FAIL session $n attempt $a: $5"
            echo "  session $n is still next (from $st/start/$run-s$n); log $log"
        fi
    }
    [[ $(wc -l < "$t") -ge 2 ]] || { result FAIL 0 0 - "the tape has no input rows"; return 1; }
    # the tape was played from this session's start: the same header start
    local want got
    want=$(head -1 "$snap/tape.jsonl" | jq -c '{seed, start}')
    got=$(head -1 "$t" | jq -c '{seed, start}')
    [[ $want == "$got" ]] || { result FAIL 0 0 - "the tape does not start where session $n does ($got, want $want)"; return 1; }

    # 1. Java replays the inputs, rows every tick, then Save and Quit
    rm -rf "$cp"
    local jrun=out/java/run/play-$name.replay
    echo "== java replay" >> "$log"
    # (the checkpoint without its pre/ Snapshot: the next start is one)
    make -s -C oracle replay JVMJIT="$JIT" REF="../$t" $( ((chain)) && echo SAVEEND="../$cp" SAVEPRE=0) >> "$log" 2>&1 9>&-
    local rc=$?
    local ticks
    ticks=$(grep -a -o 'ORACLE REPLAY OK ticks=[0-9]*' "$log" | tail -1 | cut -d= -f2)
    if [[ $rc != 0 ]] || [[ -z $ticks ]] || { ((chain)) && ! grep -aq '^ORACLE CHECKPOINT ' "$log"; }; then
        rm -rf "$cp"
        result FAIL 0 0 - "the Java replay did not finish with a checkpoint (rc $rc): $(grep -a 'ORACLE\|Exception' "$log" | tail -1)"
        return 1
    fi
    # 2. the native whole-server check of Java's rows from the same snapshot
    # (test_snapshots, and play --check: the client's own tick pair), and 3.
    # beside them the next session's start: the checkpoint joined again, its
    # Snapshot at tick 2 (thrown away unless the checks pass)
    rm -rf "$chk"; mkdir -p "$chk"
    local f
    for f in "$snap"/*; do [[ $(basename "$f") == tape.jsonl ]] || ln -s "$(readlink -f "$f")" "$chk/"; done
    cp "$jrun/replay.jsonl" "$chk/tape.jsonl"
    local ff=()
    [[ -n $fault ]] && ff=(--fault "$fault")
    local next=$st/start/$run-s$((n + 1)) script=$st/play/$name.next.jsonl set
    echo '{"cmd":"run","class":"Snapshot"}' > "$script"
    rm -rf "$next"; mkdir -p "$next"
    # the world the tape recorded (the checkpoint verifies it), not config.yaml's:
    # its world keys, and the difficulty and render distance of its options
    set=$(head -1 "$t" | jq -r '(.world | to_entries | map("\(.key)=\(.value)")) + [
        "difficulty=" + (["peaceful", "easy", "normal", "hard"][.options.difficulty // 2]),
        "render_distance=\(.options.rd // 4)"] | join(" ")')
    echo "== native checks and the next start $((SECONDS - t0)) s" >> "$log"
    # a dev session's save joins again only as a dev run (Main refuses a dev
    # checkpoint without --dev)
    local dev=
    head -1 "$t" | grep -q '"dev":true' && dev=1
    if ((chain)); then
        make -s -C oracle script JVMJIT="$JIT" DEV="$dev" SET="$set" FROM="../$cp" SCRIPT="../$script" TAPE="../$next/tape.jsonl" > "$log.next" 2>&1 9>&- &
    else
        echo '{"manifest":"not chained (--no-chain)"}' > "$next/manifest.json"; : > "$log.next"; true &
    fi
    local next_pid=$!
    # a dev tape (the fuzzer's kits: Dev header entries) checks on play-dev,
    # the product refuses it; play reads the header's setup from the start
    # directory's own tape, so the check directory is the start
    local pbin=out/native/play
    head -1 "$t" | grep -q '"dev":true' && pbin=out/native/play-dev
    bash csrc/tests/memslot.sh "$snap" "$pbin" "$chk" --check "$chk/tape.jsonl" > "$log.play" 2>&1 9>&- &
    local play_pid=$!
    local tj=$SECONDS
    bash csrc/tests/memslot.sh "$snap" out/native/test_snapshots "${ff[@]}" "$chk" >> "$log" 2>&1 9>&-
    rc=$?
    local tts=$((SECONDS - tj))
    wait "$play_pid"
    local play_rc=$? tpl=$((SECONDS - tj))
    wait "$next_pid"
    local next_rc=$? tnx=$((SECONDS - tj))
    echo "== play --check (rc $play_rc)" >> "$log"; cat "$log.play" >> "$log"
    echo "== next start (rc $next_rc) $((SECONDS - t0)) s" >> "$log"; cat "$log.next" >> "$log"
    # the seconds of each part: the Java replay, then from its end the three
    # run side by side (the native check, play --check, the next start)
    echo "== timing replay $((tj - t0)) test_snapshots $tts play_check $tpl next_start $tnx" >> "$log"
    rm -f "$log.play" "$log.next"
    local line rows mism first
    line=$(grep -a '^replay: .* rows simulated' "$log" | tail -1)
    rows=$(sed -E 's/^replay: ([0-9]+) rows.*/\1/' <<< "$line")
    mism=$(sed -E 's/.*, ([0-9]+) field mismatch.*/\1/' <<< "$line")
    first=$(grep -a '^FAIL' "$log" | head -1)
    if [[ $rc != 0 ]] || [[ -z $line ]] || [[ $mism != 0 ]] || ! grep -aq ': OK: ' "$log"; then
        echo "== diverge $((SECONDS - t0)) s" >> "$log"
        out/native/diverge "$root/$chk" "${ff[@]}" > "$st/play/$name.diverge" 2>&1 9>&-
        rm -rf "$cp" "$next"
        result FAIL "$ticks" "${rows:-0}" "${mism:-?}" "${first:-no replay line} (out/native/diverge: $st/play/$name.diverge)"
        sed -n '1,12p' "$st/play/$name.diverge"
        return 1
    fi
    if [[ $play_rc != 0 ]]; then
        rm -rf "$cp" "$next"
        result FAIL "$ticks" "$rows" 0 "play --check: $(grep -a 'FAIL' "$log" | head -1)"
        return 1
    fi
    if [[ $next_rc != 0 ]] || [[ ! -s $next/manifest.json ]]; then
        rm -rf "$next"
        result FAIL "$ticks" "$rows" 0 "the next start snapshot was not written from $cp (rc $next_rc): $(grep -a 'ORACLE\|Exception' "$log" | tail -1)"
        return 1
    fi
    echo "== done $((SECONDS - t0)) s" >> "$log"
    if ((chain)); then
        echo $((n + 1)) > "$st/next"
        result PASS "$ticks" "$rows" 0 "checkpoint $cp"
    else
        rm -rf "$next"
        result PASS "$ticks" "$rows" 0 "not chained"
    fi
}

# ------------------------------------------------------------------ play
play() {
    local n a snap t
    if on_home; then
        read -r n a snap < <(next_start) || exit 1
        ((human)) && touch "$st/human"
        if ((build)); then
            make -s -C csrc play > /dev/null || { echo "session: native build failed"; exit 1; }
            make -s -C oracle build > /dev/null || { echo "session: java build failed"; exit 1; }
        fi
        t=$st/play/$run-s$n-$a.jsonl
        echo "session $n (attempt $a) of $run from $snap; quit the window to end the session"
        ((human && n == 1 && a == 1)) && echo "  a new world: csrc/play/session.sh --continue (or --run $run) plays its next session"
        # a scripted session (the fuzzer) reserves its memory like the checks
        local runner=()
        ((build)) || runner=(bash csrc/tests/memslot.sh --key play "$snap")
        # Dev header entries (--dev-ops: the fuzzer's kits), or a start a dev
        # session saved: play-dev, which the product binary is not
        local bin=out/native/play a
        for a in "${play_args[@]}"; do [[ $a == --dev-ops ]] && bin=out/native/play-dev; done
        head -1 "$snap/tape.jsonl" | grep -q '"dev":true' && bin=out/native/play-dev
        ${runner[@]+"${runner[@]}"} "$bin" "$snap" --tape "$t" "${play_args[@]}" || { echo "session: play exited with $?"; exit 1; }
        check "$t"
        return
    fi
    # the Mac: the start comes from $home, the check runs there
    [[ $(git rev-parse HEAD) == $(ssh $home "cd $R && git rev-parse HEAD") ]] \
        || echo "session: warning: this checkout is not at $home's commit; the check runs $home's code"
    local fromarg="" humanarg=""
    ((from)) && fromarg="--from $FIRST"
    ((human)) && humanarg="--human"
    read -r n a snap < <(ssh $home "cd $R && bash csrc/play/session.sh --run $run $fromarg $humanarg --start") \
        || { echo "session: $home did not name the next start"; exit 1; }
    mkdir -p "$st/start/$run-s$n" "$st/play"
    rsync -aL --delete "$home:$R/$snap/" "$st/start/$run-s$n/" || exit 1
    # a checkpoint start (every session after the first) loads its chunks from
    # the checkpoint's save, which the snapshot names by the host's path; play
    # finds it beside the snapshot instead (session.c resolve_checkpoint:
    # DIR/../../checkpoints/SEED/NAME), so bring it there
    local kind sdir seed
    IFS=$'\t' read -r kind sdir seed < <(head -1 "$st/start/$run-s$n/tape.jsonl" | jq -r '[.start.kind // "", .start.dir // "", .seed] | @tsv')
    if [[ $kind == checkpoint && -n $sdir ]]; then
        mkdir -p "$st/checkpoints/$seed/${sdir##*/}"
        rsync -aL --delete "$home:$sdir/" "$st/checkpoints/$seed/${sdir##*/}/" || exit 1
    fi
    # play's default scene and HUD assets (recordings, not in git), brought up
    # to the host's every time: a Mac with an older copy (hud_mixed without
    # table.bin) crashed play (exit 139); rsync sends only what changed
    local d
    for d in out/java/render/forest-fast out/java/render/hud_mixed; do
        mkdir -p "$d" && rsync -aL --delete "$home:$R/$d/" "$d/" || exit 1
    done
    make -s -C csrc play > /dev/null || { echo "session: native build failed"; exit 1; }
    t=$st/play/$run-s$n-$a.jsonl
    echo "session $n (attempt $a) of $run from $snap ($home); quit the window to end the session"
    ((human && n == 1 && a == 1)) && echo "  a new world: csrc/play/session.sh --continue (or --run $run) plays its next session"
    out/native/play "$st/start/$run-s$n" --tape "$t" "${play_args[@]}" || { echo "session: play exited with $?"; exit 1; }
    ssh $home "mkdir -p $R/$st/play" && rsync -a "$t" "$home:$R/$st/play/$run-s$n-$a.in.jsonl" || exit 1
    local ff=""
    [[ -n $fault ]] && ff="--fault $fault"
    ssh $home "cd $R && bash csrc/play/session.sh --run $run $ff --check $st/play/$run-s$n-$a.in.jsonl"
}

case $mode in
    status)
        if on_home || [[ -f $st/next ]]; then status; else ssh $home "cd $R && bash csrc/play/session.sh --run $run --status"; fi;;
    start) next_start && { ((human)) && touch "$st/human"; true; };;
    latest) human_runs | head -1 | cut -f2;;
    list) if on_home; then list_runs; else ssh $home "cd $R && bash csrc/play/session.sh --list"; fi;;
    check) on_home || { echo "session: --check runs on $home"; exit 2; }; check "$tape";;
    play) play;;
esac
