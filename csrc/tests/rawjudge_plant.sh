#!/usr/bin/env bash
# The raw judge's negative proof: out/native/play built with one input
# mapping broken on purpose (the strafe-left key A mapped to strafe right,
# key_for in play.c) must fail csrc/play/rawjudge.sh on the recording DIR
# at the first tick whose Java act holds or presses key.left, and on the
# left key itself.
#   bash csrc/tests/rawjudge_plant.sh DIR
set -uo pipefail
root=$(cd "$(dirname "$0")/../.." && pwd)
cd "$root"
dir=${1:?usage: rawjudge_plant.sh DIR}
dir=${dir%/}
tmp=$(mktemp -d "${TMPDIR:-/tmp}/rawplant.XXXXXX")
src=csrc/play/.plant-play.c
trap 'rm -rf "$tmp" "$src"' EXIT
sed 's/    case SDLK_A: return K_LEFT;/    case SDLK_A: return K_RIGHT;/' csrc/play/play.c > "$src"
grep -q 'case SDLK_A: return K_RIGHT;' "$src" || { echo "FAIL plant: key_for's SDLK_A line was not found"; exit 1; }
v() { make -s -C csrc --no-print-directory --eval 'print-%: ; @echo $($*)' "print-$1"; }
$(v CC) $(v CFLAGS) $(v LDFLAGS) $(v SDL_CFLAGS) -o "$tmp/play" "$src" $(v LIB) $(v SDL_LIBS) -lz -lm \
    || { echo "FAIL plant: the planted client did not build"; exit 1; }
S=$(jq -r .tick "out/java/$(cat "$dir/start")/manifest.json")
want=$(jq -r --argjson s "$S" 'select(.t != null and .t >= $s and .act.in.keys["key.left"] != null) | .t' "$dir/tape.jsonl" | head -1)
[[ -n $want ]] || { echo "FAIL plant: $dir never holds key.left"; exit 1; }
out=$(bash csrc/play/rawjudge.sh "$dir" --play "$tmp/play" --no-frames --tag plant)
echo "$out" | sed 's/^/  /'
line=$(grep -m1 '^FAIL rawjudge rows' <<< "$out")
got=$(grep -o 't=[0-9]*' <<< "$line" | head -1 | cut -d= -f2)
if [[ $got == "$want" ]] && grep -q 'key.left' <<< "$line"; then
    echo "PASS plant: the client with A mapped to strafe right fails the judge at tick $got, the first tick Java's act holds key.left"
else
    echo "FAIL plant: expected a key.left difference at tick $want, got: ${line:-no FAIL rows line}"
    exit 1
fi
