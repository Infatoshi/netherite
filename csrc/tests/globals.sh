#!/usr/bin/env bash
# The tick's writable globals: every symbol in a writable data section
# (.data, .bss, .tdata, .tbss) of the objects built from engine/, the
# renderer's files aside, must match a line of tests/globals.allow. Anything
# else is state two environments in one process would share (env.h says
# where it goes). -v lists every symbol with its reason.
#   bash tests/globals.sh [-v]    (from csrc/, after make)
# One objdump over every object and one awk for the matching (lane/nether-polish: 0.86 s to 0.11 s (make globals 0.95 to 0.22 s); a basename
# and an objdump an object and a bash regex loop a symbol before, the same lines out in the same order).
set -uo pipefail
cd "$(dirname "$0")/.." || exit 2
obj=../out/native/obj
render='^(raster.*|render.*|texanim|font|pngread|gui_screens|block_item_model|item_color|hud_live)$'
verbose=0; [ "${1:-}" = -v ] && verbose=1
objs=()
for s in engine/*.c; do
  b=${s##*/}; b=${b%.c}
  [[ $b =~ $render ]] && continue
  o=$obj/$b.o
  [ -f "$o" ] || { echo "globals: $o missing (run make first)"; exit 2; }
  objs+=("$o")
done
objdump -t "${objs[@]}" | awk -v verbose=$verbose -v allowf=tests/globals.allow '
  BEGIN { while ((getline l < allowf) > 0) { if (l ~ /^#/) continue; split(l, f, /[ \t]+/); if (f[1] == "") continue
            a = f[1]; i = index(a, ":"); na++; pat[na] = "^(" substr(a, 1, i - 1) "):(" substr(a, i + 1) ")$"; name[na] = a } }
  / file format / { b = $1; sub(/:$/, "", b); sub(/^.*\//, "", b); sub(/\.o$/, "", b); next }
  $0 ~ /[ \t]\.(data|bss|tdata|tbss)([ \t.]|$)/ && $0 !~ /\.rel\.ro/ && $NF !~ /^\.(data|bss|tdata|tbss)/ {
    n++; key = b ":" $NF; hit = ""
    for (k = 1; k <= na; ++k) if (key ~ pat[k]) { hit = name[k]; break }
    if (hit == "") { print "globals: " key " is writable and not in tests/globals.allow"; bad++ }
    else if (verbose) print key "  (" hit ")"
  }
  END { if (!bad) print "globals: " n " writable symbols in the tick'"'"'s objects, every one allowed (tests/globals.allow)"; exit bad > 0 }'
