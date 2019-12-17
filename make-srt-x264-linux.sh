#!/bin/bash

# enable-version3 enables LGPL licence version 3
# build result written to the output directory so we can easily link against it
export PKG_CONFIG_PATH=/usr/local/lib/pkgconfig:/usr/lib/pkgconfig
export PATH=$PATH:/usr/local/cuda/bin
./configure \
  --enable-version3 \
  --disable-sdl2 \
  --enable-debug \
  --disable-ffplay \
  --disable-libxcb \
  --disable-libxcb-shm \
  --disable-indev=sndio \
  --disable-outdev=sndio \
  --disable-outdev=xv \
  --enable-libpulse \
  --enable-libvmaf \
  --enable-gpl \
  --enable-cuda-nvcc \
  --enable-cuvid \
  --enable-nvenc \
  --enable-libnpp \
  --extra-cflags="-I/usr/local/cuda/include/" \
  --extra-ldflags="-L/usr/local/cuda/lib64/" \
  --enable-libx264 \
  --enable-protocol=libsrt\
  --enable-libsrt \
  --enable-nonfree \
  --disable-shared \
  --enable-static \
  --pkg-config-flags=--static \
  --prefix="output"

make -j4
cp ffmpeg_g ../exmachina-ffmpeg-example-f9117b8783ef/.
cp ffmpeg ../exmachina-ffmpeg-example-f9117b8783ef/.
#note srt configure options ./configure --enable-static --disable-shared --enable-debug
#cp ffmpeg_g ../playtrivia-video-tests/ffmpeg-exmg-lls-linux
#cp ffmpeg_g ~/go/src/bitbucket.org/exmachina/video-encoder/assets/ffmpeg-exmg-lls-linux
