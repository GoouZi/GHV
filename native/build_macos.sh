#!/usr/bin/env bash
set -e
cd "$(dirname "$0")"
mkdir -p bin
c++ -O3 -std=c++17 -DNDEBUG ghvcore.cpp -o bin/ghvcore
c++ -O3 -std=c++17 -DNDEBUG ghvdecode.cpp -o bin/ghvdecode
printf 'Built native/bin/ghvcore and native/bin/ghvdecode\n'
