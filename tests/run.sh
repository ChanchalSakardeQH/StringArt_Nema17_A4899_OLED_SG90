#!/usr/bin/env bash
# Host-side checks for StringArt_Nema17_GUI. No ESP32 and no Arduino toolchain
# required -- everything builds against the stub headers in shims/.
#
#   ./run.sh            syntax check + unit tests
#
set -euo pipefail
cd "$(dirname "$0")"

SKETCH=../StringArt_Nema17_GUI/StringArt_Nema17_GUI.ino
BUILD=build
mkdir -p "$BUILD"

# The .ino is not valid C++ on its own: it relies on the Arduino build
# prepending an include and collecting the globals. Do that here.
{
  echo '#pragma once'
  cat "$SKETCH"
  echo
  echo 'SerialClass Serial; EspClass ESP; WiFiClass WiFi; FS LittleFS; TwoWire Wire;'
} > "$BUILD/sketch_body.h"

# web_page.h and oled_display.h are included by the sketch and live one level up.
CXXFLAGS="-std=gnu++17 -I shims -I $BUILD -I ../StringArt_Nema17_GUI"

echo "== prototype lint =="
python3 check_prototypes.py
echo

echo "== page check =="
python3 check_page.py
echo

echo "== syntax check =="
printf '#include <Arduino.h>\n#include <freertos_shim.h>\n#include "sketch_body.h"\n' > "$BUILD/syntax.cpp"
g++ -fsyntax-only $CXXFLAGS "$BUILD/syntax.cpp"
echo "ok"

echo
echo "== unit tests =="
g++ $CXXFLAGS tests.cpp -o "$BUILD/tests"
"$BUILD/tests"
