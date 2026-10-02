#!/usr/bin/env bash
# The tick's writable globals: every symbol in a writable data section
# (.data, .bss, .tdata, .tbss) of the objects built from engine/, the
# renderer's files aside, must match a line of tests/globals.allow. Anything
# else is state two environments in one process would share (env.h says
# where it goes). -v lists every symbol with its reason.
#   bash tests/globals.sh [-v]    (from csrc/, after make)
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
obj=../out/native/obj
render='^(raster.*|render.*|texanim|font|pngread|gui_screens|block_item_model|item_color|hud_live)$'
allow=$(grep -v '^#' tests/globals.allow | awk 'NF {print $1}')
verbose=0; [ "${1:-}" = -v ] && verbose=1
bad=0; n=0
for s in engine/*.c; do
  b=$(basename "$s" .c)
  [[ $b =~ $render ]] && continue
  o=$obj/$b.o
  [ -f "$o" ] || { echo "globals: $o missing (run make first)"; exit 2; }
  while read -r sym; do
    n=$((n + 1)); hit=
    for a in $allow; do
      if [[ "$b:$sym" =~ ^(${a%%:*}):(${a#*:})$ ]]; then hit=$a; break; fi
    done
    if [ -z "$hit" ]; then echo "globals: $b:$sym is writable and not in tests/globals.allow"; bad=$((bad + 1));
    elif [ $verbose = 1 ]; then echo "$b:$sym  ($hit)"; fi
  done < <(objdump -t "$o" | awk '$0 ~ /[ \t]\.(data|bss|tdata|tbss)([ \t.]|$)/ && $0 !~ /\.rel\.ro/ && $NF !~ /^\.(data|bss|tdata|tbss)/ {print $NF}')
done
[ $bad = 0 ] && echo "globals: $n writable symbols in the tick's objects, every one allowed (tests/globals.allow)"
exit $((bad > 0))
