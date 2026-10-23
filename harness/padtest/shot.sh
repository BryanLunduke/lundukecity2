#!/bin/bash
# shot.sh out.png [x y w h]  -> full screen grab of :47
if [ -n "$2" ]; then
ffmpeg -loglevel error -y -f x11grab -video_size ${4}x${5} -i :47.0+$2,$3 -frames:v 1 "$1"
else
ffmpeg -loglevel error -y -f x11grab -video_size 1920x1200 -i :47.0 -frames:v 1 "$1"
fi
