#!/usr/bin/env bash
# Apollo installer: checks dependencies, builds, and puts `apollo` on your PATH.
#
#   ./install.sh                 install to /usr/local
#   PREFIX=~/.local ./install.sh install somewhere that needs no sudo
set -euo pipefail

cd "$(dirname "$0")"
PREFIX="${PREFIX:-/usr/local}"

say()  { printf '\033[1m%s\033[0m\n' "$*"; }
warn() { printf '  warning: %s\n' "$*"; }
die()  { printf '\033[31merror:\033[0m %s\n' "$*" >&2; exit 1; }

say "Apollo installer"
echo

# --- dependencies ----------------------------------------------------------
[[ "$(uname -s)" == "Darwin" ]] || die "Apollo currently supports macOS only."

command -v c++ >/dev/null 2>&1 || die "No C++ compiler. Install the Xcode command line tools:
  xcode-select --install"

SFML_DIR="${SFML_DIR:-}"
if [[ -z "$SFML_DIR" ]]; then
  for candidate in /usr/local /opt/homebrew; do
    if [[ -f "$candidate/include/SFML/Graphics.hpp" ]]; then SFML_DIR="$candidate"; break; fi
  done
fi
[[ -n "$SFML_DIR" ]] || die "SFML 3 not found. Install it with:
  brew install sfml
Then re-run this script (or set SFML_DIR=/path/to/sfml)."

echo "  compiler   $(command -v c++)"
echo "  SFML       $SFML_DIR"
command -v sshpass >/dev/null 2>&1 \
  || warn "sshpass not installed — only needed for password-based SSH. Key auth is preferred."
echo

# --- build -----------------------------------------------------------------
say "Building..."
make clean >/dev/null 2>&1 || true
make -j"$(sysctl -n hw.ncpu)" SFML_DIR="$SFML_DIR"
echo

# --- install ---------------------------------------------------------------
say "Installing to $PREFIX..."
if [[ -w "$PREFIX/bin" ]] || mkdir -p "$PREFIX/bin" 2>/dev/null; then
  make install PREFIX="$PREFIX" SFML_DIR="$SFML_DIR"
else
  echo "  $PREFIX needs elevated permissions."
  sudo make install PREFIX="$PREFIX" SFML_DIR="$SFML_DIR"
fi
echo

say "Done. Next:"
echo "  apollo          launch (the setup wizard runs on first start)"
echo "  apollo doctor   verify the installation"
echo "  apollo --help   see every command"
