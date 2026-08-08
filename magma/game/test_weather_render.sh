#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/.."
export JAVA_HOME=${JAVA_HOME:-/usr/lib/jvm/java-8-openjdk-amd64}
export PATH="$JAVA_HOME/bin:$PATH"
cc -O2 -ffp-contract=off -Wall -Wextra -Icore -I. -I../blaze/core \
  game/test_weather_render.c -lm -o /tmp/test_weather_render
/tmp/test_weather_render >/tmp/weather-c.txt
javac -d /tmp game/WeatherRenderGolden.java
java -cp /tmp WeatherRenderGolden >/tmp/weather-java.txt
cmp /tmp/weather-java.txt /tmp/weather-c.txt
echo "weather_render: PASS (24 precipitation + 1344 lightning Java-locked vertices)"
