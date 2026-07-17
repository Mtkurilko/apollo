#!/bin/bash
# Kept for muscle memory — the real build lives in the Makefile, which compiles
# incrementally, in parallel, and at -O2 (this script used to build at -O0).
set -e
cd "$(dirname "$0")"
make -j"$(sysctl -n hw.ncpu)" "$@"
make install
