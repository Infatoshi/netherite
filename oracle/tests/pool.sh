#!/usr/bin/env bash
# Hand one oracle run to a warm pool member (Pool.java), if one is free.
#   bash tests/pool.sh [--state FILE] POOLDIR HARNESS CWD oracle-args...
# CWD is the directory a fresh JVM would run in (out/java/run/NAME), for the
# paths a fresh run resolves against it.
# POOLDIR holds one directory per member (port, pid, harness, lock). The run's
# output streams back as if the JVM were local; the exit code is the run's.
# Exit 125: no member took the run (none running, all busy, another harness,
# the member refused it, or its flags are ones the pool never runs), and the
# caller runs a fresh JVM instead.
# Exit 124: a member took the run and failed it: the job threw (POOL-FAULT,
# Pool.fault: rc 71 and the error named, the member ends) or the member ended
# under it (a crash, -XX:+ExitOnOutOfMemoryError, an OOM kill). Its output so
# far went to the caller, which runs it again in a fresh JVM, never in
# another member: a pooled run never passes on a partial result.
# --state FILE (tests/poolauto.sh, the shared pool): on exit 125, FILE says
# refused (a member refused the run, or the flags) or none (no member was
# free); the caller then says what it does, so the no-live-member hint is
# not printed.
# Each member's `used` file is touched when a run ends there: its watchdog's
# idle clock.
#   bash tests/pool.sh --never oracle-args...   exit 0 when the args hold a flag the pool never runs
set -uo pipefail
# the flags a member refuses before anything runs (Main.configurePool; keep the two in step; a
# coverage run goes to coverage members only, by their key and configurePool): refused
# here by rule, without asking a member (the shared members' logs held 4,978 --save-at-end and
# 792 --snap-at refusals by 2026-09-28; members run both, and --detail-from and --until, since
# lane/poolsave)
never() {
  local a
  for a in "$@"; do
    case $a in
      --play|--port|--trace|--chunklog|--spawndump|--spawndumpdim|--renderstate|--enbt|--pool|--raw-gui|--raw-rec|--raw-drive|--cw-diff|--cw-cells|--rerecord-client)
        echo "$a"; return 0 ;;
    esac
  done
  return 1
}
if [ "${1:-}" = --never ]; then shift; never "$@" > /dev/null; exit; fi
state=
[ "${1:-}" = --state ] && { state=$2; shift 2; }
dir=$1 harness=$2 cwd=$3
shift 3
[ -d "$dir" ] || exit 125
if flag=$(never "$@"); then
  [ -n "$state" ] && echo refused > "$state"
  echo "pool: the pool never runs $flag; fresh JVM" >&2
  exit 125
fi
job=$(jq -cn --arg h "$harness" --arg c "$cwd" '{harness:$h,cwd:$c,args:$ARGS.positional}' --args -- "$@") || exit 125
n=0 live=0 refused=
for m in "$dir"/*/; do
  [ -d "$m" ] || continue
  n=$((n + 1))
  [ -f "$m/port" ] && [ -f "$m/pid" ] || continue
  [ "$(cat "$m/harness" 2>/dev/null)" = "$harness" ] || continue
  kill -0 "$(cat "$m/pid")" 2>/dev/null || continue
  live=$((live + 1))
  exec 9>"$m/lock"
  flock -n 9 || { exec 9>&-; continue; }
  # a member retires under its lock (poolauto.sh): gone once the lock is ours
  port=$(cat "$m/port" 2>/dev/null) || { exec 9>&-; continue; }
  exec 3<>"/dev/tcp/127.0.0.1/$port" || { exec 9>&-; continue; }
  printf '%s\n' "$job" >&3
  echo "pool: member $(basename "$m") takes the run" >&2
  rc= fault=
  while IFS= read -r line <&3; do
    case $line in
      "POOL-RC "*) rc=${line#POOL-RC }; break ;;
      "POOL-REFUSE "*) echo "pool: member $(basename "$m") refused: ${line#POOL-REFUSE }" >&2; rc=125; refused=1; break ;;
      "POOL-FAULT "*) fault=${line#POOL-FAULT } ;;
      *) printf '%s\n' "$line" ;;
    esac
  done
  exec 3<&-
  touch "$m/used"
  exec 9>&-
  # no rc: the member ended under the run; its log's last word says why
  [ -z "$rc" ] && [ -z "$fault" ] && fault="it ended during the run: $(grep -a 'Terminating due to\|ORACLE POOL END\|OutOfMemoryError\|#@!@# Game crashed' "$m/log" 2>/dev/null | tail -1)"
  if [ -n "$fault" ]; then
    echo "pool: member $(basename "$m") failed the run (rc ${rc:-none}): $fault; it retires, the run goes to a fresh JVM" >&2
    exit 124
  fi
  [ "$rc" = -1 ] && rc=125
  [ "$rc" = 125 ] && continue # refused: another member may take it (another --rd)
  exit "$rc"
done
if [ -n "$state" ]; then if [ -n "$refused" ]; then echo refused; else echo none; fi > "$state"; exit 125; fi
# a pool with no live member costs every run a JVM and client start (2-7 s on
# the GPU host) and used to fall back without a word (lane/playfuzz, 2026-09-26)
[ "$n" -gt 0 ] && [ "$live" = 0 ] && echo "pool: no live member of $n in $dir runs this harness; fresh JVM (make -C oracle pool-start)" >&2
exit 125
