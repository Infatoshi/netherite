#!/usr/bin/env bash
# RL-06: values the vanilla client cannot produce are refused the same way
# by the oracle harness (Act.java) and the native session (session.c
# session_parse_act, survival.c surv_client_click):
#   bash csrc/tests/agent_values.sh
# 1. Agent form: oracle/tests/agent-values-s1.jsonl (make script, through the
#    result cache) logs each expected ORACLE REFUSE line: hotbar and pitch
#    out of range refuse the step, a bad click and a gui op off its screen
#    refuse the op; the recording out/java/snapshots/agent-values-s1 holds
#    none of them and passes test_snapshots.
# 2. Tape form: copies of that recording with one row changed. A row's hb 9
#    or 40 or its pitch 120: the Java replay ends on ORACLE REFUSE and
#    test_snapshots fails that row, both naming the value. A click on slot
#    60 under the inventory: the Java replay refuses it and diverges on the
#    row's gui (the tape claims a click that did not run), test_snapshots
#    fails the row naming the slot.
set -uo pipefail
cd "$(dirname "$0")/../.."
root=$PWD
rec=out/java/snapshots/agent-values-s1
work=out/java/try/agent-values
rm -rf "$work"; mkdir -p "$work"
fail=0
ok()  { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }

# 1. the agent form
make -s -C oracle script DEV=1 SEED=1 SCRIPT=tests/agent-values-s1.jsonl TAPE="$root/$work/agent/tape.jsonl" > "$work/agent.log" 2>&1
for want in 'act: hotbar 9 outside 0..8' 'act: hotbar 40 outside 0..8' 'act: hotbar -1 outside 0..8' \
    'act: pitch 120.0 outside -90..90' 'act: pitch -90.5 outside -90..90' \
    "slot 60 outside window 0's 45 slots" "slot -5 outside window 0's 45 slots" 'slot -999 in mode 2' \
    'slot -1 in mode 5' 'window 7 is not the open window 0' 'respawn without the game over or credits screen' \
    'wake without the sleep screen' 'trsel without the merchant window 0' 'close without a container screen' \
    'gui [["close"]] without a screen'; do
  if grep -aF "ORACLE REFUSE" "$work/agent.log" | grep -F "$want" > /dev/null; then :
  else bad "agent form: no ORACLE REFUSE for $want"; fi
done
n=$(grep -ac "ORACLE REFUSE" "$work/agent.log")
[ "$n" = 16 ] && ok "agent form: 16 refusals" || bad "agent form: $n refusals, want 16"
if cmp -s <(grep -v '"harness"' "$work/agent/tape.jsonl") <(grep -v '"harness"' "$rec/tape.jsonl"); then
  ok "agent form: the recording's rows"
else bad "agent form: the tape differs from $rec"; fi
out/native/test_snapshots "$rec" > "$work/rec.native" 2>&1 && ok "native: $rec replays" || bad "native: $rec"

# 2. the tape form: one changed row each (line t + 2; the header is line 1)
doctor() {  # NAME T SED
  local d=$work/$1; mkdir -p "$d"
  for f in "$root/$rec"/*; do [ "$(basename "$f")" = tape.jsonl ] || ln -s "$f" "$d/"; done
  sed "$(($2 + 2))$3" "$rec/tape.jsonl" > "$d/tape.jsonl"
}
doctor hb9 24 's/"hb":0,/"hb":9,/'
doctor hb40 24 's/"hb":0,/"hb":40,/'
doctor pitch120 25 's/"look":\[0.0,0.0,/"look":[0.0,120.0,/'
doctor click60 28 's/"act":{/"act":{"gui":[["click",0,60,0,0]],/'
out/native/par -j 4 "make -s -C oracle replay REF=$root/$work/{}/tape.jsonl" hb9 hb40 pitch120 click60 > "$work/replays.txt" 2>&1
for c in hb9 hb40 pitch120 click60; do
  out/native/test_snapshots "$work/$c" > "$work/$c.native" 2>&1; echo $? > "$work/$c.rc"
done
logs=$(sed -n 's/.*logs in \(.*\))/\1/p' "$work/replays.txt")
jlog() { cat "$logs"/*"$1"* 2>/dev/null; }
check_refused() {  # CASE ROW TEXT
  local j n
  j=$(jlog "$1" | grep -a "ORACLE REFUSE" | head -1)
  n=$(grep -a "row $2: $3" "$work/$1.native" | head -1)
  if [[ "$j" == *"tape row: $3"* ]] && [ -n "$n" ] && [ "$(cat "$work/$1.rc")" != 0 ]; then
    ok "tape $1: java '${j#ORACLE REFUSE }', native '$n'"
  else bad "tape $1: java '${j:-no refusal}', native '${n:-no refusal}'"; fi
}
check_refused hb9 24 'hotbar 9 outside 0..8'
check_refused hb40 24 'hotbar 40 outside 0..8'
check_refused pitch120 25 'pitch 120'
j=$(jlog click60 | grep -a "ORACLE REFUSE" | head -1)
n=$(grep -a "row 28: click slot 60 outside window 0's 45 slots" "$work/click60.native" | head -1)
if [[ "$j" == *"slot 60 outside window 0's 45 slots"* ]] && jlog click60 | grep -a "ORACLE DIVERGE t=28 act.gui" > /dev/null && [ -n "$n" ]; then
  ok "tape click60: java '${j#ORACLE REFUSE }' and its diverge at t=28, native '$n'"
else bad "tape click60: java '${j:-no refusal}', native '${n:-no refusal}'"; fi
exit $fail
