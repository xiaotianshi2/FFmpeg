#!/bin/bash

# enable-version3 enables LGPL licence version 3
# build result written to the output directory so we can easily link against it
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib/pkgconfig
export PATH=$PATH:/usr/local/cuda/bin
export LIBVA_DRIVER_NAME=iHD
./configure \
  --enable-version3 \
  --disable-sdl2 \
  --enable-debug \
  --disable-libxcb \
  --disable-libxcb-shm \
  --disable-indev=sndio \
  --disable-outdev=sndio \
  --disable-outdev=xv \
  --enable-libpulse \
  --enable-gpl \
  --enable-libx264 \
  --enable-libvpx \
  --enable-libsvthevc \
  --enable-protocol=libsrt\
  --enable-libsrt \
  --enable-nonfree \
  --disable-shared \
  --enable-static \
  --pkg-config-flags=--static \
  --prefix="$PWD/output"

make -j4
cp ffmpeg_g ../../exmachina-ffmpeg-example-f9117b8783ef/.
cp ffmpeg ../../exmachina-ffmpeg-example-f9117b8783ef/.
cp ffmpeg_g ../../exmachina-ffmpeg-example-7439fd887d01/.
cp ffmpeg ../../exmachina-ffmpeg-example-7439fd887d01/.
#note srt configure options ./configure --enable-static --disable-shared --enable-debug
#cp ffmpeg_g ../playtrivia-video-tests/ffmpeg-exmg-lls-linux
#cp ffmpeg_g ~/go/src/bitbucket.org/exmachina/video-encoder/assets/ffmpeg-exmg-lls-linux

#svt-hevc libraray install notes:
#git clone https://github.com/OpenVisualCloud/SVT-HEVC.git
#cd SVT-HEVC/Build/linux
#./build release static install
#cd to folder "../FFmpeg"
#- git am ../SVT-HEVC/ffmpeg_plugin/0001*.patch
#./make-srt-x264-vp9-svthevc-linux.sh
