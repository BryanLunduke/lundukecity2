#!/bin/bash
export HOME=${H:-/workspace/city2-verify/home-pr2} DISPLAY=${DISP:-:47} SDL_AUDIODRIVER=dummy
unset XDG_CONFIG_HOME XDG_STATE_HOME
log=$1; shift
nohup /workspace/city2-verify/build-pr2/src/lunduke-city-2 "$@" > /tmp/c2p-$log.log 2>&1 &
echo $!
