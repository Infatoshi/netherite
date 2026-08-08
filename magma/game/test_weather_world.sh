#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make game/test_weather_world
./game/test_weather_world
