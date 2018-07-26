#!/bin/bash

# Bash setup (exit on error)
set -e


# Make
# ====

make -j 4


# Copy
# ====

cp ffmpeg ../playtrivia-video-tests/ffmpeg-exmg-lls-osx
