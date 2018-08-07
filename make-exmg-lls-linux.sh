#!/bin/bash

# enable-version3 enables LGPL licence version 3
./configure --enable-gpl --enable-decklink --enable-openssl --enable-version3 --enable-static --disable-sdl2 --disable-debug --disable-ffplay --disable-libxcb --disable-libxcb-shm --disable-indev=sndio --disable-outdev=sndio --disable-outdev=xv --enable-libx264 --enable-libpulse --enable-nonfree --pkg-config-flags=--static --extra-cflags="-Idecklink"

make -j4

cp ffmpeg ../playtrivia-video-tests/
cp ffmpeg ~/go/src/bitbucket.org/exmachina/video-encoder/cmd/encoder-gui/ffmpeg-exmg-lls-linux
