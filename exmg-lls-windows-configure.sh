MINGW_PATH="/c/Program Files/mingw-w64/x86_64-8.1.0-posix-seh-rt_v6-rev0"
HOME=$MINGW_PATH/msys/1.0/home/$USER 


# enable-version3 enables LGPL licence version 3
./configure \
 --enable-gpl \
 --enable-static \
 --enable-version3 \
 --disable-sdl2 \
 --disable-debug \
 --disable-ffplay \
 --disable-ffprobe \
 --disable-libxcb \
 --disable-libxcb-shm \
 --disable-indev=sndio \
 --disable-outdev=sndio \
 --disable-outdev=xv \
 --enable-nonfree \
 --enable-libx264 \
 --enable-avisynth \
 --pkg-config-flags=--static \
 
 
# --extra-cflags="-I$HOME/ffmpeg/include -static"
# --extra-ldflags="-L$HOME/ffmpeg/lib -static"
 
# --extra-cflags=-I/usr/x86_64-w64-mingw32/sys-root/mingw/include \
# --extra-ldflags=-L/usr/x86_64-w64-mingw32/sys-root/mingw/lib
