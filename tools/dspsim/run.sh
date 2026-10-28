#!/usr/bin/env bash
# Builds the DSP program and the simulator's harness, and runs one against
# the other (see dspsim.cpp).
#
# Needs teakra, built: by default the copy the libteak fork's teaksim builds
# (TEAKSIM=<libteak>/tools/teaksim, TEAKRA_DIR=<teakra sources>).
set -eu
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
TEAKSIM="${TEAKSIM:-$ROOT/../libteak/tools/teaksim}"
TEAKRA_DIR="${TEAKRA_DIR:-$ROOT/../teak-llvm-1/helpSrc/teakra-master}"
LIB="$TEAKSIM/build/teakra/src/libteakra.a"
[ -f "$LIB" ] || { echo "dspsim: teakra is not built ($LIB): run make in $TEAKSIM"; exit 2; }
cd "$ROOT"
make -s -f Makefile.teak >/dev/null
mkdir -p build/dspsim
g++ -O2 -std=c++17 -I"$TEAKRA_DIR/include" -Ishared/dsp -Ishared -Wno-unused-function \
    -o build/dspsim/dspsim tools/dspsim/dspsim.cpp "$LIB"
build/dspsim/dspsim build/teak.elf
# (... and again with everything as loud as it goes.)
DSPSIM_SCENE=loud build/dspsim/dspsim build/teak.elf
