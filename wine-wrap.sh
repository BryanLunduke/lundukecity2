#!/bin/sh
# meson exe_wrapper: run a Windows test binary under Wine with the sysroot DLLs on PATH
export WINEPREFIX=/workspace/city2-win/wineprefix WINEDEBUG=-all WINEDLLOVERRIDES="mscoree,mshtml="
export WINEPATH="/workspace/city2-win/sysroot/mingw64/bin"
exec wine "$@"
