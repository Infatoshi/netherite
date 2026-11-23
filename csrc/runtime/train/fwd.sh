#!/usr/bin/env bash
# lane/fwd: the headline evaluation, forward play from spawn on fresh worlds, one attempt a world (2026-11-18).
#
#   bash csrc/runtime/train/fwd.sh draw TAG N              N seeds, a line each "K SEED": seed K is the first 8 hex digits
#                                                          of sha256("netherite fwd TAG I") mod 2^31 for I = 0, 1, ...,
#                                                          skipping seeds 1..1000 (every seed set, library and archive
#                                                          seed is in that range) and repeats
#   bash csrc/runtime/train/fwd.sh start SEED DIR           the oracle's spawn start of SEED at easy, render distance 4
#                                                          (make -C oracle script, tests/fresh-play-s1.jsonl: the seed
#                                                          sets' way) into DIR/sSEED, then test_snapshots on it (C
#                                                          replays its 300 rows); one line: START OK or START FAIL
#   bash csrc/runtime/train/fwd.sh runfile SEED START       the run file (stdout): easy, render distance 4, the 64x64
#                                                          host ray cast, no device, one env, every chain macro with carry
#                                                          and pick (lane/wood: the headline base's since 2026-11-18;
#                                                          FWD_BASE=fwd for lane/fwd's base without it, FWD_MACROS_ADD
#                                                          appends switches, FWD_MACROS_DROP=NAME leaves one out)
#   bash csrc/runtime/train/fwd.sh attempt RUN SAMPLER TICKS MOPTS OUT [EXTRA CHAIN ARGS...]
#                                                          one attempt from RUN's start: chain search --n 1, sampler seed
#                                                          SAMPLER, --seg TICKS/20, --target-stage dragon_dead, MOPTS (a
#                                                          one-line comma list, each --mopt), the rows to OUT.*
#                                                          (OUT.tsv the rollout row, OUT.end.tsv, OUT.deaths.tsv,
#                                                          OUT.census, OUT.log, OUT.time: /usr/bin/time -v, OUT.rc;
#                                                          lane/portal: FWD_PORTAL=K adds OUT.build.tsv, --build-log,
#                                                          and OUT.ptrace.tsv, --portal-trace every K-th overworld step;
#                                                          FWD_BIN=PATH runs that chain, a variant build's)
#   bash csrc/runtime/train/fwd.sh panel PANEL.tsv J TICKS MOPTS OUTDIR [EXTRA...]
#                                                          every line "K SEED SAMPLER START" of PANEL.tsv as one attempt,
#                                                          J at a time, OUTDIR/wK.*; refuses under 24 GB available or
#                                                          18 GB free on /home, waits for memory between attempts
set -u
cd "$(dirname "$0")/../../.."
cmd=${1:-}
shift || true

draw() {
  local tag=$1 n=$2 i=0 k=0 h s
  declare -A seen
  while [ "$k" -lt "$n" ]; do
    h=$(printf 'netherite fwd %s %d' "$tag" "$i" | sha256sum | cut -c1-8)
    s=$((0x$h % 2147483648))
    i=$((i + 1))
    [ "$s" -le 1000 ] && continue
    [ -n "${seen[$s]:-}" ] && continue
    seen[$s]=1
    echo "$k $s"
    k=$((k + 1))
  done
}

runfile() {
  local seed=$1 start=$2
  local m=recipe,strip,food,obsidian,nether,pearl,stronghold,dragon,bportal,strip2,carry
  [ "${FWD_BASE:-}" = fwd ] || m=$m,pick
  m=$m${FWD_MACROS_ADD:+,$FWD_MACROS_ADD}
  # (lane/portal) FWD_MACROS_DROP=NAME: that switch left out
  if [ -n "${FWD_MACROS_DROP:-}" ]; then m=,$m,; m=${m/,$FWD_MACROS_DROP,/,}; m=${m#,}; m=${m%,}; fi
  cat <<EOF
# lane/fwd: the forward evaluation's run file for world seed $seed (easy, render distance 4, its spawn start)
sim:
  world:
    seed: $seed
    difficulty: easy
    render_distance: 4
  client:
    ticks_per_step: 1
    camera: raycast
    observation: 64x64
  engine:
    device_mesh: 0
    raycast: host
    device_generation: off
    device_light: off
    render: none
    tick: host
  pipeline:
    kind: gold
    start: $start
    worlds: 1
    groups: 1
    episode_seconds: 180
  performance:
    workers: 1
    streams: 0
    group: 0
    batch: 0
    depth: 0
    layout: any
    pin: 0
    place: none
    tune: off
policy:
  actions:
    macros: $m
    picks: off
EOF
}

attempt() {
  local run=$1 samp=$2 ticks=$3 mfile=$4 out=$5
  shift 5
  local mopts=() o x
  IFS=, read -r -a o < "$mfile"
  for x in "${o[@]}"; do mopts+=(--mopt "$x"); done
  local secs
  secs=$(awk -v t="$ticks" 'BEGIN {printf "%.6f", t / 20}')
  rm -rf "$out.dir"
  env CUDA_VISIBLE_DEVICES= /usr/bin/time -v -o "$out.time" "${FWD_BIN:-out/runtime/train/chain}" search \
    --run "$run" --dir "$out.dir" --n 1 --groups 1 --threads 1 --seed "$samp" --seg "$secs" \
    --target dragon_dead --target-stage dragon_dead --free "${FWD_FREE:-8}" \
    --rollout-out "$out.tsv" --end-log "$out.end.tsv" --death-log "$out.deaths.tsv" --owner-census "$out.census" \
    --ladder-log "$out.ladder.tsv" --death-trace "$out.dtrace.tsv" --stage-log "$out.stages.tsv" \
    ${FWD_CROSS:+--crossings "$FWD_CROSS"} ${FWD_WOOD:+--wood-log "$out.wood.tsv"} \
    ${FWD_LAVA:+--lava-log "$out.lava.tsv"} \
    ${FWD_PORTAL:+--build-log "$out.build.tsv" --portal-trace "-1/$FWD_PORTAL:$out.ptrace.tsv"} \
    --every 60 "${mopts[@]}" "$@" > "$out.log" 2>&1
  local rc=$?
  echo "$rc" > "$out.rc"
  rm -rf "$out.dir"
  # (the crossings' memory files compressed; a restore decompresses the one it reads)
  [ -n "${FWD_CROSS:-}" ] && [ -d "$FWD_CROSS" ] && zstd -q -T1 --rm "$FWD_CROSS"/*.mem 2>/dev/null
  return $rc
}

case $cmd in
  draw) draw "$@" ;;
  runfile) runfile "$@" ;;
  start)
    seed=$1 dir=$2
    d=$dir/s$seed
    mkdir -p "$d"
    log=$d.oracle.log
    # (CACHE=0: the oracle result cache would keep a second copy of each start, about 5 MB)
    if ! make -s -C oracle script CACHE=0 SEED="$seed" SET='difficulty=easy render_distance=4' \
         SCRIPT=tests/fresh-play-s1.jsonl TAPE="$d/tape.jsonl" > "$log" 2>&1; then
      echo "START FAIL $seed oracle (see $log)"; exit 1
    fi
    head -1 "$d/tape.jsonl" | grep -q '"difficulty":1,' || { echo "START FAIL $seed not easy"; exit 1; }
    head -1 "$d/tape.jsonl" | grep -q '"rd":4,' || { echo "START FAIL $seed not render distance 4"; exit 1; }
    r=$(out/native/test_snapshots "$d" 2>&1 | tail -3)
    if echo "$r" | grep -q ' 0 field mismatches' && echo "$r" | grep -q 'OK: every row replayed'; then
      echo "START OK $seed $(echo "$r" | grep -o '[0-9]* rows simulated') 0 field mismatches"
    else
      echo "START FAIL $seed replay: $r"; exit 1
    fi
    ;;
  attempt) attempt "$@" ;;
  table)
    # table PANEL.tsv OUTDIR: the panel's markdown table: each event (the stage log's: stages.h's chain, seven rods, twelve
    # pearls or eyes, twelve eyes, ten crystals the server lost) reached by how many of the panel's attempts, its exact
    # (Clopper-Pearson) 95% interval, and the arrival step's quantiles; then every attempt's end by class (kill, death
    # by the combat tracker's last hit: the death trace's cm_dt and cm_name, PRIVILEGED books; limit with the furthest
    # stage held; UNRESOLVED: no clean rc or no rollout row); the ticks simulated and the attempts' CPU and wall time
    panel=$1 outdir=$2
    {
      while read -r k seed samp start; do
        [ -z "$k" ] && continue
        f=$outdir/$k
        rc=$(cat "$f.rc" 2>/dev/null || echo none)
        row=$(awk -F'\t' 'NR > 1 && NF >= 17 {print $6, $7; exit}' "$f.tsv" 2>/dev/null)
        dt=$(awk -F'\t' '$2 == "0" {l = $43 " " $44} END {print l}' "$f.dtrace.tsv" 2>/dev/null)
        cpu=$(awk '/User time/ {u = $NF} /System time/ {s = $NF} END {print u + s}' "$f.time" 2>/dev/null)
        wall=$(awk '/Elapsed/ {n = split($NF, a, ":"); w = 0; for (i = 1; i <= n; ++i) w = w * 60 + a[i]; print w}' "$f.time" 2>/dev/null)
        # (lane/wood) a Java proof of the attempt: OUTDIR/wK.proof, chain-ka's output on it (KA PASS: every row equal;
        # END EQUAL: Java's last row equal to the attempt's end state)
        pf=0
        [ -f "$f.proof" ] && grep -q 'KA PASS' "$f.proof" && grep -q 'END EQUAL' "$f.proof" && pf=1
        echo "A $k $seed $rc ${row:-- -} ${dt:-- -} ${cpu:-0} ${wall:-0} $pf"
        [ -f "$f.stages.tsv" ] && awk -F'\t' -v k="$k" 'NR > 1 && $2 !~ /^END:/ {print "E", k, $2, $3}' "$f.stages.tsv"
      done < "$panel"
    } | awk '
      # (lane/statebank) the terms in log space: (1 - p)^n underflowed to 0 past about a thousand attempts, and every
      # interval of a 2,046-world panel came out as one point
      function bin_cdf(k, n, p,   i, lt, s) { if (k < 0) return 0; if (k >= n) return 1; lt = n * log(1 - p); s = lt > -700 ? exp(lt) : 0
        for (i = 1; i <= k; ++i) { lt += log((n - i + 1) / i) + log(p) - log(1 - p); if (lt > -700) s += exp(lt) } return s }
      function cp_lo(k, n,   lo, hi, m, j) { if (k == 0) return 0; lo = 0; hi = 1
        for (j = 0; j < 60; ++j) { m = (lo + hi) / 2; if (1 - bin_cdf(k - 1, n, m) < 0.025) lo = m; else hi = m } return lo }
      function cp_hi(k, n,   lo, hi, m, j) { if (k == n) return 1; lo = 0; hi = 1
        for (j = 0; j < 60; ++j) { m = (lo + hi) / 2; if (bin_cdf(k, n, m) > 0.025) lo = m; else hi = m } return hi }
      function q(arr, n, f,   i) { i = int(f * (n - 1) + 0.5) + 1; return arr[i] }
      function cname(dt, nm,   k) {
        split("in_fire on_fire lava in_wall drown starve cactus fall void generic magic wither anvil falling_block mob player thorns explosion_player arrow fireball thrown indirect_magic explosion", D, " ")
        split("pig cow mooshroom chicken sheep villager iron_golem slime magma_cube player zombie skeleton creeper spider player ghast squid bat enderman witch silverfish pigman blaze cave_spider", K, " ")
        k = (nm >= 0 && nm < 24) ? K[nm + 1] : (nm == -1 ? "" : "kind" nm)
        if (dt == 0 || dt == 1) return "fire"
        if (dt == 14 || dt == 18 || dt == 19 || dt == 22) return D[dt + 1] (k != "" ? ":" k : "")
        return (dt >= 0 && dt < 23) ? D[dt + 1] : "unknown" }
      $1 == "A" { n++; ord[n] = $2; rc[$2] = $4; res[$2] = $5; tk[$2] = $6; dtv[$2] = $7; nmv[$2] = $8; cpu += $9; wsum += $10
        prf[$2] = $11 + 0
        if ($10 > wmax) wmax = $10; if ($5 != "-") ticks += $6 }
      $1 == "E" { if (!($3 in seen)) { seen[$3] = 1; ne++; en[ne] = $3 } at[$3, ++cnt[$3]] = $4; hit[$2, $3] = 1
        if (!($2 in far) || ix[$3] > ix[far[$2]]) far[$2] = $3 }
      BEGIN { split("wooden_pickaxe stone_pickaxe furnace iron_ingot iron_pickaxe bucket flint_and_steel diamond_pickaxe obsidian nether blaze_rod blaze_powder rods_7 ender_pearl pearls_12 ender_eye eyes_12 end stronghold_room portal_lit crystals_gone1 crystals_gone3 crystals_gone6 crystals_gone9 crystals_gone10 dragon_75 dragon_50 dragon_25 dragon_dead", EV, " ")
        for (i = 1; i in EV; ++i) ix[EV[i]] = i }
      END {
        printf "arrival steps: min, quartiles and max among the attempts that reached the event (not of all attempts)\n"
        printf "crystals_goneN: the online counter (chain.c, --ladder-log) infers a crystal lost by its pillar coordinates and can reverse it while the first-hit log stays latched: not a count of destroyed entity ids\n\n"
        printf "| event | reached | of | rate | 95%% interval (exact) | arrival step min (reached only) | p25 | median | p75 | max |\n|---|---:|---:|---:|---|---:|---:|---:|---:|---:|\n"
        for (i = 1; i in EV; ++i) { e = EV[i]; c = cnt[e] + 0
          delete s; for (j = 1; j <= c; ++j) s[j] = at[e, j] + 0
          for (a = 2; a <= c; ++a) { v = s[a]; b = a - 1; while (b >= 1 && s[b] > v) { s[b + 1] = s[b]; --b } s[b + 1] = v }
          printf "| %s | %d | %d | %.1f%% | %.1f%% to %.1f%% |", e, c, n, 100 * c / n, 100 * cp_lo(c, n), 100 * cp_hi(c, n)
          if (c) printf " %d | %d | %d | %d | %d |\n", s[1], q(s, c, 0.25), q(s, c, 0.5), q(s, c, 0.75), s[c]; else printf " - | - | - | - | - |\n" }
        printf "\nzero of %d: one-sided 95%% upper bound %.2f%%\n\n", n, 100 * (1 - 0.05 ^ (1 / n))
        for (i = 1; i <= n; ++i) { k = ord[i]
          if (rc[k] != "0" || res[k] == "-") c = "UNRESOLVED (rc " rc[k] ")"
          else if (res[k] == "reached") c = prf[k] ? "kill (Java proof)" : "dragon sim-reached, UNPROVED"
          else if (res[k] == "death") c = "death: " cname(dtv[k], nmv[k])
          else c = "limit: furthest by event index " (k in far ? far[k] : "none past spawn")
          cls[c]++; if (!(c in cs)) { cs[c] = 1; nc++; co[nc] = c } who[c] = who[c] (who[c] == "" ? "" : " ") k }
        printf "limit rows: the furthest event by its fixed index in the event list above (not the last acquired, nor the item held at the end); a dragon row is a kill only with a Java proof (OUTDIR/wK.proof: chain-ka KA PASS and END EQUAL)\n\n"
        printf "| end | attempts | which |\n|---|---:|---|\n"
        for (i = 1; i <= nc; ++i) printf "| %s | %d | %s |\n", co[i], cls[co[i]], who[co[i]]
        printf "\nattempts %d; ticks simulated %d (%.1f game hours); attempt CPU %.2f h; attempt wall summed %.2f h, longest %.0f s\n",
          n, ticks, ticks / 72000, cpu / 3600, wsum / 3600, wmax }'
    ;;
  crosscheck)
    # crosscheck RUN SAMPLER MOPTS CROSSDIR STAGES EVENT K WORK: the crossing EVENT of the uninterrupted attempt whose
    # --crossings directory is CROSSDIR and stage log STAGES (its run file RUN, sampler SAMPLER), at step N (STAGES'
    # row), restored (--replay-steps CROSSDIR:N --replay-mem EVENT.mem) and played K steps past N, against the
    # uninterrupted attempt played again to N + K: every step's act in [N, N + K) (chain seg-cmp) and the END rows'
    # state. CROSS EQUAL or CROSS DIFF (exit 1)
    run=$1 samp=$2 mfile=$3 cdir=$4 st=$5 ev=$6 k=$7 work=$8
    n=$(awk -F'\t' -v e="$ev" '$2 == e {print $3; exit}' "$st")
    [ -n "$n" ] || { echo "CROSS REFUSE: no $ev row"; exit 2; }
    mkdir -p "$work"
    mem=$work/$ev.mem
    if [ -f "$cdir/$ev.mem.zst" ]; then zstd -q -d -f "$cdir/$ev.mem.zst" -o "$mem"; else cp "$cdir/$ev.mem" "$mem"; fi
    rm -rf "$work/u" "$work/r" "$work"/u.* "$work"/r.*
    t=$((n + k))
    FWD_CROSS=$work/u attempt "$run" "$samp" "$t" "$mfile" "$work/u"
    FWD_CROSS=$work/r attempt "$run" "$samp" "$t" "$mfile" "$work/r" --replay-steps "$cdir:$n" --replay-mem "$mem"
    cmpo=$(out/runtime/train/chain seg-cmp "$work/u" "$work/r" "$n" "$t")
    eu=$(grep -P '\tEND:' "$work/u.stages.tsv" | cut -f2-)
    er=$(grep -P '\tEND:' "$work/r.stages.tsv" | cut -f2-)
    if echo "$cmpo" | grep -q '^SEG EQUAL' && [ -n "$eu" ] && [ "$eu" = "$er" ]; then
      echo "CROSS EQUAL $ev at step $n, $k steps on: $cmpo; END $(echo "$eu" | cut -f1-10)"
    else
      echo "CROSS DIFF $ev at step $n: $cmpo"; echo "  u: $eu"; echo "  r: $er"; exit 1
    fi
    ;;
  panel)
    panel=$1 j=$2 ticks=$3 mfile=$4 outdir=$5
    shift 5
    mkdir -p "$outdir" "$outdir/runs"
    while read -r k seed samp start; do
      [ -z "$k" ] && continue
      case $k in \#*) continue ;; esac
      [ -e "$outdir/$k.rc" ] && continue
      while [ "$(jobs -rp | wc -l)" -ge "$j" ]; do wait -n; done
      while :; do
        avail=$(awk '/^MemAvailable:/{print int($2/1048576)}' /proc/meminfo)
        free=$(df -Pk /home | awk 'NR==2{print int($4/1048576)}')
        [ "$free" -lt 15 ] && { echo "$(date '+%F %T') STOP disk $free GB: killing this panel's attempts"
          for p in $(jobs -rp); do pkill -P "$p"; done; wait; exit 4; }
        [ "$avail" -ge 24 ] && [ "$free" -ge "${FWD_DISK_MIN:-18}" ] && break
        echo "$(date '+%F %T') wait: memory $avail GB, disk $free GB"
        sleep 60
      done
      [ -s "$start/manifest.json" ] || { echo "$(date '+%F %T') UNRESOLVED $k seed $seed: no start"; continue; }
      runfile "$seed" "$start" > "$outdir/runs/$k.yaml"
      echo "$(date '+%F %T') start $k seed $seed sampler $samp"
      if [ -n "${FWD_CROSSROOT:-}" ]; then
        FWD_CROSS=$FWD_CROSSROOT/$k attempt "$outdir/runs/$k.yaml" "$samp" "$ticks" "$mfile" "$outdir/$k" "$@" &
      else
        attempt "$outdir/runs/$k.yaml" "$samp" "$ticks" "$mfile" "$outdir/$k" "$@" &
      fi
    done < "$panel"
    wait
    echo "$(date '+%F %T') PANEL DONE"
    ;;
  *) sed -n 2,30p "$0"; exit 2 ;;
esac
