#!/usr/bin/env bash
# sig: a failure's cause signature, from what out/native/diverge printed (or,
# when nothing diverged row by row, the failing line itself).
#
#   bash csrc/tests/sweep/sig.sh DIVERGE_FILE [DETAIL]
#
# DIVERGE_FILE is out/native/diverge's output (- or a missing file: none);
# DETAIL is the fuzzer's own failure line (a play check's FAIL, a rawjudge
# FAIL, a play or oracle exit), read when the diverge output names no row.
# Prints one tab-separated line: FIELD CLASS PATH WHERE
#   FIELD  the first differing row field (d.ents, cp->x, w.bc, act.in.keys...),
#          array indices dropped (cp->inv[36] is cp->inv[])
#   CLASS  the first differing entity's class in the state after that row
#          (missing or extra natively counts), else a differing tile
#          entity's id, else -
#   PATH   the first difference in that state: the entity's first NBT path
#          (Motion[], Item.Count), missing/extra, or the first world scalar
#          (world.rand), Det stream (det.server.nextId), chunk (chunk.id,
#          chunk.extra), tile entity (tile.Furnace.BurnTime) or pending tick
#          (pending); - when the state equals the oracle's
#   WHERE  the native function or phase named by the failure when it names
#          one (a replay that failed before any row, a harness refusal, a
#          planted fault), digits and paths dropped; else -
# Every number that identifies one run (entity ids, rows, positions, counts)
# is dropped, so two sessions of one cause print the same line.
set -uo pipefail
f=${1:--}
detail=${2:-}
[[ $f != - && -f $f ]] || f=/dev/null
awk -v detail="$detail" '
function norm(s) {
    gsub(/\[[0-9]+\]/, "[]", s)
    gsub(/e:[0-9]+/, "e:#", s); return s
}
# a path cut to its first two components (Offers.Recipes[] for any offer
# field): which leaf of a sub-tree differs first depends on the values
function two(s,   n, a, i, out) {
    n = split(s, a, "."); out = a[1]; if (n > 1) out = out "." a[2]; return out
}
# a message without its run: numbers, coordinates, paths, hashes
function msg(s) {
    gsub(/\/[^ ]*/, "", s); gsub(/\([^)]*\)/, "", s); gsub(/-?[0-9]+(\.[0-9]+)?([eE][-+]?[0-9]+)?/, "#", s)
    gsub(/[0-9a-f]{12,}/, "#", s); gsub(/[ \t]+/, " ", s); sub(/^ /, "", s); sub(/ $/, "", s)
    gsub(/\t/, " ", s); return substr(s, 1, 90)
}
NR == 1 && /first mismatch at row/ {
    s = $0; sub(/.*first mismatch at row [0-9]+: /, "", s); sub(/ want .*/, "", s); sub(/ \(.*/, "", s)
    field = norm(s)
}
/the replay failed before any row mismatch/ { rfail = 1; next }
rfail && /^  FAIL/ && where == "" { where = msg($0) }
/^fault:|^  fault:/ { where = "fault " msg($0) }
/^state after row/ { sd = 1; sec = ""; next }
!sd { next }
/^  state diff/ || /^entity [0-9]+, rows/ { sd = 0; next }
/^  world scalars:/ { sec = "world"; next }
/^  entities:/ { sec = "ents"; next }
/^  chunks:/ { sec = "chunks"; next }
/^  pending block ticks:/ { sec = "pending"; next }
sec == "world" && /^    dim -?[0-9]+ [A-Za-z]+: want/ && wpath == "" {
    s = $0; sub(/^    dim -?[0-9]+ /, "", s); sub(/:.*/, "", s); wpath = "world." s
}
sec == "world" && /^    det / && wpath == "" {
    # det ROLE KEY: or det split ./net/minecraft/PKG/CLASS.java:FIELD ROLE:
    s = $0; sub(/^    det /, "", s); sub(/: want .*/, "", s)
    if (s ~ /^split /) { sub(/^split /, "", s); sub(/ [A-Z]+$/, "", s); sub(/.*\//, "", s); sub(/\.java:/, ".", s); wpath = "det.split." s }
    else { gsub(/ /, ".", s); wpath = "det." norm(s) }
}
sec == "ents" && /^    dim -?[0-9]+: id [0-9]+ / && cls == "" && !want_nbt {
    s = $0; sub(/^    dim -?[0-9]+: id [0-9]+ /, "", s); c = s; sub(/ .*/, "", c); cls = c
    if (s ~ /^\(no id/) cls = "(no-id)"
    # the world it is in: where the start is says less (a chain session
    # starts anywhere, a traveller differs in the world it went to)
    w = $0; sub(/^    dim /, "", w); sub(/:.*/, "", w); cls = cls "@" w
    if (s ~ /is missing natively/) epath = "missing"
    else if (s ~ /is extra natively/) epath = "extra"
    else want_nbt = 1
    next
}
# the paths of the first entity: the first that is not kinematic (Pos, Motion,
# FallDistance and OnGround follow from any earlier difference, and the dump
# lists paths in key order, not in the order they went wrong), else kin
sec == "ents" && want_nbt && /^      [^ ]/ {
    s = $0; sub(/^ +/, "", s); sub(/: want .*/, "", s); s = norm(s); top = s; sub(/[.[].*/, "", top)
    if (top ~ /^(Pos|Motion|FallDistance|OnGround)$/) kin = 1
    else if (epath == "") epath = two(s)
    next
}
sec == "ents" && want_nbt && !/^      [^ ]/ { if (epath == "") epath = kin ? "kin" : "-"; want_nbt = 0 }
sec == "ents" && /holds id [0-9]+ twice/ && epath == "" { epath = "duplicate" }
sec == "ents" && /list order differs/ && epath == "" { epath = "order" }
sec == "chunks" && cpath == "" {
    if ($0 ~ /is loaded natively, not in oracle/) cpath = "chunk.extra"
    else if ($0 ~ /is loaded in java, not natively/) cpath = "chunk.missing"
    else if ($0 ~ /inhabitedTime want/) cpath = "chunk.inhabitedTime"
    else if ($0 ~ /tile entities in oracle/) cpath = "tile.count"
    else if ($0 ~ /cells differ/) {
        split("id meta sky block-light", k, " ")
        s = $0; sub(/.*\): /, "", s); split(s, v, ", ")
        for (i = 1; i <= 4; i++) if (v[i] + 0 > 0) { cpath = "chunk." k[i]; break }
    }
    else if ($0 ~ /^    dim -?[0-9]+: tile entity /) {
        s = $0; sub(/.*tile entity /, "", s); sub(/ .*/, "", s); tcls = s; want_tile = 1; next
    }
}
sec == "chunks" && want_tile && /^      [^ ]/ {
    s = $0; sub(/^ +/, "", s); sub(/: want .*/, "", s); cpath = "tile." tcls "." two(norm(s)); want_tile = 0
}
sec == "pending" && /entry [0-9]+ want/ && ppath == "" { ppath = "pending" }
sec == "pending" && /java [0-9]+, native [0-9]+$/ && ppath == "" { ppath = "pending" }
END {
    if (want_nbt && epath == "") epath = kin ? "kin" : "-"
    if (field == "") {
        # no row mismatch in the diverge output: the fuzzer line says what failed
        d = detail
        if (d ~ /FAIL play check [^:]*: row [0-9]+ /) {
            s = d; sub(/.*FAIL play check [^:]*: row [0-9]+:? /, "", s)
            if (s ~ / want /) { sub(/ want .*/, "", s); field = "play:" norm(s) }
            else { field = "play"; where = msg(s) }
        } else if (d ~ /FAIL rawjudge (rows|gui)/ && d ~ / t=[0-9]+/) {
            s = d; k = (d ~ /rawjudge gui/) ? "gui" : "rows"
            sub(/.* t=[0-9]+:? /, "", s)
            if (s ~ /^act\./) { sub(/ want .*/, "", s); field = "raw:" norm(s) }
            else if (s ~ /^Java.s screen sent/) field = "raw:gui"
            else if (s ~ / want /) { sub(/ want .*/, "", s); field = "raw:" norm(s) }
            else { field = "raw:" k; where = msg(s) }
        } else if (d ~ /FAIL rawjudge [a-z]* ?[^ ]*: the client exited/) { field = "raw:exit"; where = msg(d) }
        else if (d ~ /play exited|play:/) { field = "play-exit"; where = msg(d) }
        else if (d ~ /^oracle:|the Java session failed/) { field = "oracle"; where = msg(d) }
        else if (rfail) field = "replay"
        else { field = "other"; if (where == "") where = msg(d) }
    }
    if (cls == "" && tcls != "") cls = tcls
    path = epath != "" ? epath : wpath != "" ? wpath : cpath != "" ? cpath : ppath != "" ? ppath : "-"
    if (cls == "") cls = "-"
    if (where == "") where = "-"
    printf "%s\t%s\t%s\t%s\n", field, cls, path, where
}' "$f"
