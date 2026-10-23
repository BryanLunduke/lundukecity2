#!/bin/bash
set +e
cd /src
rm -rf /src/build-sniper-pr2
meson setup /src/build-sniper-pr2 --buildtype=release 2>&1 | grep -E 'Dependency|Build targets|ERROR|WARNING'
ninja -C /src/build-sniper-pr2 -j4 -k 0 > /tmp/ninja.log 2>&1; echo "ninja-exit=$?"
grep -E 'warning:|error:' /tmp/ninja.log | head -20; tail -2 /tmp/ninja.log
echo "--- tests as root"
meson test -C /src/build-sniper-pr2 --no-rebuild --num-processes 4 2>&1 | grep -E '^ *[0-9]+/|Ok:|Fail:|Skipped:'
echo "--- tests as uid 65534 with DISPLAY=:91"
chmod -R a+rwX /src/build-sniper-pr2 2>/dev/null
HOME=/tmp/nb DISPLAY=:91 setpriv --reuid=65534 --regid=65534 --clear-groups meson test -C /src/build-sniper-pr2 --no-rebuild --num-processes 4 2>&1 | grep -E '^ *[0-9]+/|Ok:|Fail:|Skipped:'
echo "--- ldd"
ldd /src/build-sniper-pr2/src/lunduke-city-2 | grep -E 'cairo|sigc|SDL|pango|not found'
