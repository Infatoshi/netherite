#!/usr/bin/env bash
# The phase profile's table (make phase-profile): one line per recording from
# OUT/phase-profile.NAME.txt. Ranked by user-space instructions, which do not
# depend on the host's load (time does, by 3x on a busy GPU host): per tick the
# mean, p50, p99 and max in thousands, the phase the most expensive tick spent
# most in, and each column's share of the instructions, a per-world phase
# summed over the worlds. ~ columns are sub-marks (inclusive, nested in the
# phases): LIGHT the light engine, LFLAGS, RELIGHT and RTICK S6's light
# flags, enqueueRelightChecks and random block ticks, LOAD chunk provision
# (loadChunk with its populate rule), GEN raw generation. Time columns (TSC,
# the host's load in them): ms per tick and the share of the s2c queue
# copies (S2C: s2c_out's copy-on-append, 0 on almost every row).
#   bash tests/phase_profile_table.sh OUT NAME...
set -uo pipefail
out=$1; shift
printf '%-20s %6s %8s %8s %8s %9s %-16s %8s %7s' recording rows 'kins/t' 'p50' 'p99' 'max' 'max tick in' 'ms/tick' allocs/t
for c in S4 S6 S11 S2 C1+C2 N1-3 '~LIGHT' '~LFLAGS' '~RELIGHT' '~RTICK' '~LOAD' '~GEN'; do printf ' %7s' "$c"; done
printf ' %7s %8s %8s\n' 'S2C t%' 'env MB' 'RSS MB'
for r in "$@"; do
  f=$out/phase-profile.$r.txt
  [ -f "$f" ] || { echo "$r: no $f"; continue; }
  awk -v r="$r" '
    /^phase profile: [0-9]+ rows profiled/ { rows = $3 }
    /^phase profile: tick time total/ { for (i = 1; i <= NF; ++i) if ($i == "mean") mean = $(i + 1) }
    /^phase profile: tick instructions total/ {
      for (i = 1; i <= NF; ++i) {
        if ($i == "mean" && !km) km = $(i + 1)
        if ($i == "p50") k50 = $(i + 1)
        if ($i == "p99") k99 = $(i + 1)
        if ($i == "max") kmax = $(i + 1)
      }
    }
    /^phase profile: allocations per tick mean/ { al = $7; sub(",", "", al) }
    /^phase profile: worst by instructions #1/ {
      s = $0; sub(/^.*allocations: /, "", s); split(s, a, /[,|]/); top = a[1]; gsub(/^ +| +$/, "", top); gsub(/ /, ":", top)
    }
    /^phase profile: [-~(0-9A-Z]/ && NF >= 16 && $3 != "column" {
      c = $3; sh = $12; sub("%", "", sh); ts = $6; sub("%", "", ts)
      if (c ~ /^-?[0-9]\./) sub(/^-?[0-9]\./, "", c)
      share[c] += sh; tshare[c] += ts
    }
    /^mem peak/ { for (i = 1; i <= NF; ++i) if ($i == "total") model = $(i + 1) }
    /^phase profile: peak RSS/ { rss = $5 }
    END {
      printf "%-20s %6d %8.1f %8.1f %8.1f %9.1f %-16s %8.3f %7s", r, rows, km, k50, k99, kmax, top, mean / 1000, al
      printf " %6.1f%% %6.1f%% %6.1f%% %6.1f%% %6.1f%% %6.1f%%", share["S4"], share["S6"], share["S11"], share["S2"], share["C1"] + share["C2"], share["N1"] + share["N2"] + share["N1E"] + share["N3"]
      printf " %6.1f%% %6.1f%% %6.1f%% %6.1f%% %6.1f%% %6.1f%%", share["~LIGHT"], share["~LFLAGS"], share["~RELIGHT"], share["~RTICK"], share["~LOAD"], share["~GEN"]
      printf " %6.1f%% %8s %8s\n", tshare["~S2CCOPY"], model, rss
    }' "$f"
done
