#!/bin/bash

export LD_LIBRARY_PATH=/lib:/usr/lib:/usr/local/lib

./ffmpeg \
    -y \
    -vsync 1 \
    -f v4l2 \
    -framerate 30 \
    -i /dev/video0 \
    -f mpegts \
    -g 30 \
    -c:v libsvt_hevc \
    -rc 1 \
    -pix_fmt yuv420p \
    -b:v 500000 \
    -tune 0 \
    -asm_type 1 \
    srt://localhost:9998?pkt_size=1316
