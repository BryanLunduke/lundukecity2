#!/bin/bash
set +e
export DEBIAN_FRONTEND=noninteractive
dpkg -i /src/harness/debs/*.deb >/tmp/dpkg.log 2>&1 || { tail -20 /tmp/dpkg.log; exit 3; }
which Xvfb || echo "no Xvfb in SDK; using host X socket"
dpkg -l libcairomm-1.0-dev libsigc++-2.0-dev | tail -2
rm -rf /src/build-sniper
meson setup /src/build-sniper --buildtype=release 2>&1 | grep -E 'sdl3|cairomm|pangocairo|Build targets|ERROR'
ninja -C /src/build-sniper -j4 -k 0 2>&1 | grep -E 'warning|error|Linking target src' | head -30
meson test -C /src/build-sniper --no-rebuild --num-processes 4 2>&1 | grep -E '^ *[0-9]+/[0-9]+|Ok:|Fail:|Skipped:'
echo "--- ldd"
ldd /src/build-sniper/src/lunduke-city-2 | grep -E 'cairo|sigc|SDL|pango|stdc|not found'
echo "--- smoke dummy"
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LUNDUKE_CITY2_SMOKE=1 HOME=/tmp/h1 /src/build-sniper/src/lunduke-city-2 /src/shots/dullsville.cty; echo smoke-exit=$?
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy LUNDUKE_CITY2_DEMO=window LUNDUKE_CITY2_SHOT=/src/shots/30-sniper-sdk-demo-window.png HOME=/tmp/h2 /src/build-sniper/src/lunduke-city-2; echo demo-exit=$?
echo "--- x11 under xvfb"
mkdir -p /tmp/h3
HOME=/tmp/h3 DISPLAY=:91 SDL_AUDIODRIVER=dummy LUNDUKE_CITY2_DEMO=menu LUNDUKE_CITY2_SHOT=/src/shots/31-sniper-sdk-x11-menu.png /src/build-sniper/src/lunduke-city-2; echo x11-exit=$?
