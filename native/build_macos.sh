#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
mkdir -p bin
CXX=${CXX:-c++}
# Apple Clang usually has no OpenMP runtime by default; try it, then fall back.
if "$CXX" -O3 -std=c++17 -DNDEBUG -Xpreprocessor -fopenmp ghvcore.cpp -lomp -o bin/ghvcore 2>/dev/null; then
  echo "Built ghvcore with OpenMP"
else
  "$CXX" -O3 -std=c++17 -DNDEBUG ghvcore.cpp -o bin/ghvcore
  echo "Built ghvcore without OpenMP"
fi
if "$CXX" -O3 -std=c++17 -DNDEBUG -Xpreprocessor -fopenmp ghvdecode.cpp -lomp -o bin/ghvdecode 2>/dev/null; then
  echo "Built ghvdecode with OpenMP"
else
  "$CXX" -O3 -std=c++17 -DNDEBUG ghvdecode.cpp -o bin/ghvdecode
  echo "Built ghvdecode without OpenMP"
fi
printf 'Built native/bin/ghvcore and native/bin/ghvdecode\n'
