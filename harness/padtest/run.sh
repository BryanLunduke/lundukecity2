#!/bin/bash
# usage: run.sh logname [args]  -> prints PID
export HOME=/workspace/city2-verify/home DISPLAY=${DISP:-:47} SDL_AUDIODRIVER=dummy
unset XDG_CONFIG_HOME XDG_STATE_HOME
log=$1; shift
nohup /workspace/city2-verify/build/src/lunduke-city-2 "$@" > /tmp/c2-$log.log 2>&1 &
echo $!
