#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
make game/test_weather_lightning
./game/test_weather_lightning
