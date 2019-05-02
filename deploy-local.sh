#!/bin/bash

# Bash setup (exit on error)
set -e

#docker run --name exmg-lls-container exmg-lls /bin/true

docker exec -it exmg-lls-container make -j4
docker cp exmg-lls-container:/app/ffmpeg ffmpeg
docker cp exmg-lls-container:/app/ffmpeg_g ffmpeg_g

cp ffmpeg_g ../playtrivia-video-tests/ffmpeg-exmg-lls-linux
