#!/bin/sh
# Build from Git Bash / MSYS2 on Windows. Usage: ./build.sh [make args]
# Needs MSYS2 with mingw-w64-ucrt-x86_64-arm-none-eabi-gcc and make.
MSYS=${MSYS:-/c/msys64}
PYDIR=$(dirname "$(command -v python 2>/dev/null || echo /usr/bin/python)")
exec "$MSYS/usr/bin/env" MSYSTEM=UCRT64 CHERE_INVOKING=1 PYDIR="$PYDIR" \
  "$MSYS/usr/bin/bash" -lc 'export PATH="/ucrt64/bin:$PYDIR:$PATH"; make "$@"' make "$@"
