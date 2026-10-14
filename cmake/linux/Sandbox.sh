#!/bin/sh
# Launch the Sandbox from this directory. The binary's RPATH ($ORIGIN and $ORIGIN/lib)
# finds libraries next to it; SDL3 is linked into the binary, not shipped as a .so.
cd "$(dirname "$0")" || exit 1
exec ./Sandbox "$@"
