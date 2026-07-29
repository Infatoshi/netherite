#!/usr/bin/env bash
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SPEC="${1:-}"

if [[ -z "$SPEC" ]]; then
	echo "usage: $0 SCENARIO.yaml" >&2
	exit 2
fi
if [[ ! -f "$SPEC" ]]; then
	echo "scenario spec not found: $SPEC" >&2
	exit 2
fi
SPEC="$(cd "$(dirname "$SPEC")" && pwd)/$(basename "$SPEC")"
RESULT="$(mktemp /tmp/netherite_scenario_result.XXXXXX)"

trap 'flock -u 9 2>/dev/null || true; rm -f "$RESULT"' EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

exec 9>/tmp/qrl_25575.lock
echo "[scenario] waiting for exclusive oracle lock /tmp/qrl_25575.lock"
flock 9
echo "[scenario] oracle lock acquired"

SCENARIO_ORACLE_LOCKED=1 uv run --no-project \
	--with pyyaml --with pyarrow --with numpy --with scipy --with pillow \
	--with nbt --with python-xlib \
	python "$HERE/scenario.py" record "$SPEC" --result-file "$RESULT"

TAPE="$(<"$RESULT")"
flock -u 9
echo "[scenario] oracle stopped; lock released"

set +e
uv run --no-project --with numpy --with scipy --with pillow --with nbt \
	python "$HERE/../trace/replay_tape.py" "$TAPE" --cpu --report
RC=$?
set -e

echo "[scenario] archived tape: $TAPE"
echo "[scenario] gate rc: $RC (0=pass, 3=pixel, 4=physics)"
exit "$RC"
