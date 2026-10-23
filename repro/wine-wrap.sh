#!/bin/sh
export WINEPREFIX="${WINEPREFIX:-/workspace/city2-win/repro/wineprefix}" WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
export WINEPATH="/workspace/city2-win/repro/sysroot/mingw64/bin"
exec wine "$@"
