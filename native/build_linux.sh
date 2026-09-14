#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
mkdir -p bin
if c++ -O3 -std=c++17 -DNDEBUG -fopenmp ghvcore.cpp -o bin/ghvcore 2>/dev/null; then
  echo "Built ghvcore with OpenMP"
else
  c++ -O3 -std=c++17 -DNDEBUG ghvcore.cpp -o bin/ghvcore
  echo "Built ghvcore without OpenMP"
fi
c++ -O3 -std=c++17 -DNDEBUG ghvdecode.cpp -o bin/ghvdecode
printf 'Built native/bin/ghvcore and native/bin/ghvdecode\n'
