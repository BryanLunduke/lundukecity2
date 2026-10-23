#!/bin/sh
here="$(cd "$(dirname "$0")" && pwd)"
export LD_LIBRARY_PATH="$here/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export LUNDUKE_CITY_ASSETS="$here/share/lunduke-city-2/micropolis-assets"
exec "$here/lunduke-city-2" "$@"
