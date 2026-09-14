#!/usr/bin/env sh
set -eu
CXX=${CXX:-g++}
mkdir -p "$(dirname "$0")/bin"
"$CXX" -O3 -std=c++17 -DNDEBUG "$(dirname "$0")/ghvcore.cpp" -o "$(dirname "$0")/bin/ghvcore"
echo "Built native/bin/ghvcore"
