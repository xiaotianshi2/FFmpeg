#!/bin/bash

MINGW_PATH="/c/Program Files/mingw-w64/x86_64-8.1.0-posix-seh-rt_v6-rev0"
HOME=$MINGW_PATH/msys/1.0/home/$USER 

make -r -j4

cp ffmpeg ../playtrivia-video-tests/ffmpeg-exmg-lls-windows
# cp ffmpeg ~/go/src/bitbucket.org/exmachina/video-encoder/assets/ffmpeg-exmg-lls-windows
