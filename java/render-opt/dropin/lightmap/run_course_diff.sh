#!/usr/bin/env bash
# Run all three modes for a given time-of-day, then pixel-diff. CPU-only, sequential.
# Usage: run_course_diff.sh [time]
set -u
TIMEVAL="${1:-6000}"
DROP="$(cd "$(dirname "$0")" && pwd)"
for m in off native sabotage; do
  echo "===== capture $m t=$TIMEVAL ====="
  bash "$DROP/capture_course.sh" "$m" "$TIMEVAL"
done
echo "===== diff t=$TIMEVAL ====="
cd "$DROP" && uv run --no-project --with numpy --with pillow python "$DROP/diff_course.py" "$TIMEVAL"
