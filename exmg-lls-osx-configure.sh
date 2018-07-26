#!/bin/bash

# Bash setup (exit on error)
set -e


# Build Dependencies 
# ==================
#
# https://trac.ffmpeg.org/wiki/CompilationGuide/macOS suggests:
# $ brew install automake fdk-aac git lame libass libtool libvorbis libvpx opus sdl shtool texi2html theora wget x264 x265 xvid nasm
#
# I've reduced that to the following:
# - nasm      For compilation
# - x264      For MP4 encoding

brew install nasm x264


# Configure
# =========
#
# OS X FFmpeg static builds (https://evermeet.cx/ffmpeg/) use the following arguments:
# --cc=/usr/bin/clang --prefix=/opt/ffmpeg --extra-version=tessus --enable-avisynth --enable-fontconfig --enable-gpl --enable-libaom --enable-libass --enable-libbluray --enable-libfreetype --enable-libgsm --enable-libmodplug --enable-libmp3lame --enable-libmysofa --enable-libopencore-amrnb --enable-libopencore-amrwb --enable-libopus --enable-librubberband --enable-libshine --enable-libsnappy --enable-libsoxr --enable-libspeex --enable-libtheora --enable-libtwolame --enable-libvidstab --enable-libvo-amrwbenc --enable-libvorbis --enable-libvpx --enable-libwavpack --enable-libx264 --enable-libx265 --enable-libxavs --enable-libxvid --enable-libzimg --enable-libzmq --enable-libzvbi --enable-version3 --pkg-config-flags=--static --disable-ffplay
#
# I've reduced that to the following:
# --cc=/usr/bin/clang                     To refer to C compiler?
# --enable-gpl --enable-libx264           For MP4 encoding
# --pkg-config-flags=--static             To produce a static build which can be easily shared
# --disable-ffplay                        To prevent needless building

./configure --cc=/usr/bin/clang --enable-gpl --enable-libx264 --pkg-config-flags=--static --disable-ffplay
