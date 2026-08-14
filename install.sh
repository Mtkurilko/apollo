#!/bin/sh
# Build and install Apollo.
#
#   ./install.sh                 install to /usr/local (sudo only if needed)
#   PREFIX=~/.local ./install.sh install somewhere that needs no password
#   ./install.sh --uninstall     take it away again
#
# The only things this needs on the machine are a C++17 compiler, CMake and
# git. FTXUI is fetched and built into the binary, so nothing is left behind
# for a package manager to break later.

set -eu

PREFIX="${PREFIX:-/usr/local}"
BUILD="${BUILD:-build}"
JOBS="${JOBS:-$( (sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4) )}"

say()  { printf '  %s\n' "$*"; }
step() { printf '\n\033[1m%s\033[0m\n' "$*"; }
die()  { printf '\n\033[31merror\033[0m %s\n' "$*" >&2; exit 1; }

if [ "${1:-}" = "--uninstall" ]; then
    step "Removing Apollo"
    if [ -w "$PREFIX/bin" ] || [ ! -e "$PREFIX/bin/apollo" ]; then
        rm -f "$PREFIX/bin/apollo"
    else
        sudo rm -f "$PREFIX/bin/apollo"
    fi
    say "removed $PREFIX/bin/apollo"
    say "your settings in ~/.apollo were left alone"
    exit 0
fi

step "Checking what is here"

for tool in cmake git; do
    command -v "$tool" >/dev/null 2>&1 || die "$tool is not installed.
       macOS:  brew install $tool
       Debian: sudo apt install $tool"
    say "$tool  $(command -v "$tool")"
done

CXX="${CXX:-}"
if [ -z "$CXX" ]; then
    for candidate in c++ clang++ g++; do
        if command -v "$candidate" >/dev/null 2>&1; then CXX="$candidate"; break; fi
    done
fi
[ -n "$CXX" ] || die "no C++ compiler found. On macOS: xcode-select --install"
say "compiler  $(command -v "$CXX")"

# FTXUI is downloaded on the first configure; say so before it looks stuck.
if [ ! -d "$BUILD/_deps" ]; then
    say "ftxui  will be downloaded and built in (once)"
fi

step "Building"
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release >/dev/null || die "cmake could not configure the build"
cmake --build "$BUILD" -j "$JOBS" || die "the build failed"
say "built $BUILD/apollo"

step "Checking it works"
"$BUILD/apollo_tests" >/dev/null 2>&1 && say "tests pass" || say "tests did not pass — installing anyway, but please report it"

step "Installing to $PREFIX"
# Writability is decided by the nearest directory that actually exists: a
# prefix that has yet to be created is not unwritable, it is just absent.
existing="$PREFIX"
while [ ! -d "$existing" ]; do
    parent=$(dirname "$existing")
    [ "$parent" = "$existing" ] && break
    existing="$parent"
done

if [ -w "$existing" ]; then
    cmake --install "$BUILD" --prefix "$PREFIX" >/dev/null
else
    say "$existing is not writable; asking for your password"
    sudo cmake --install "$BUILD" --prefix "$PREFIX" >/dev/null
fi
say "installed $PREFIX/bin/apollo"

case ":$PATH:" in
    *":$PREFIX/bin:"*)
        step "Done"
        say "run: apollo"
        ;;
    *)
        step "Almost done"
        say "$PREFIX/bin is not on your PATH. Add this to your shell's startup file:"
        printf '\n      export PATH="%s/bin:$PATH"\n\n' "$PREFIX"
        say "then run: apollo"
        ;;
esac
