#!/usr/bin/env bash
set -euo pipefail

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(mktemp -d "$ROOT/.worldgen-animals.XXXXXX")
trap 'rm -rf -- "$OUT"' EXIT
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}

"$ROOT/magma/magma_game" --headless --render off --pace unlimited \
  --seed -9055566058453653051 --view-distance 2 --ticks 10 \
  --state-out "$OUT/state.jsonl" >"$OUT/stdout" 2>"$OUT/stderr"

(
  cd "$ROOT/java/Minecraft"
  ./gradlew -g run/gradle -q worldgenAnimalsGolden \
    -PanimalSeed=-9055566058453653051 -PanimalCx=-3 -PanimalCz=-2 \
    -PanimalWorldSeed=-9055566058453653051 2>"$OUT/java.stderr"
) | sed -n '/^E /p' >"$OUT/java.txt"

uv run --no-project python - "$OUT/state.jsonl" "$OUT/java.txt" <<'PY'
import json
import sys

rows = [json.loads(line) for line in open(sys.argv[1], encoding="utf-8")]
first, last = rows[0], rows[-1]
first_types = [entity["type"] for entity in first["entities"]]
last_types = [entity["type"] for entity in last["entities"]]
assert len(first_types) == 14, first_types
assert set(first_types) == {61}, first_types  # desert worldgen rabbits
assert last_types == first_types, (first_types, last_types)
assert first["entity_id_cursor"] == last["entity_id_cursor"] == 15
java_rows = [line.split() for line in open(sys.argv[2], encoding="utf-8")]
assert len(java_rows) == 2, java_rows
for native, java in zip(first["entities"][:2], java_rows):
    assert java[1] == "Rabbit", java
    expected = tuple(map(float, java[2:6]))
    actual = (native["x"], native["y"], native["z"], native["yaw"])
    assert actual == expected, (actual, expected)
    assert native["rabbit_type"] == int(java[6]), (native, java)
    # The first native row is after its ordinary first entity tick; a child
    # advances -24000 -> -23999 there while an adult remains zero.
    expected_age = int(java[7])
    assert native["growing_age"] == (
        expected_age + (expected_age < 0)), (native, java)
    assert "uuid_most" in native and "entity_seed48" in native, native
print("worldgen animals: PASS (direct Java position/yaw/type/age, initialized RNG/UUID, no duplicate packs)")
PY
