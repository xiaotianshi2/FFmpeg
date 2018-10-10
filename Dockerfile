FROM ubuntu:18.04
# Set the working directory to /app
WORKDIR /app

RUN \
    apt-get update && \
    apt-get install software-properties-common -y && \ 
    add-apt-repository universe && \
    add-apt-repository ppa:xorg-edgers/ppa && \
    apt-get install -y \
    alsa \
    apt-utils \
    automake \
    autoconf \
    build-essential \
    cmake \
    dkms \
    git-core \
    libasound2-dev \
    libass-dev \
    libfreetype6-dev \
    libgl1-mesa-glx \
    libgl1 \
    libpulse-dev \
    libssl-dev \
    libtool \
    libva-dev \
    libva2 \
    libva-drm2 \
    libva-x11-2 \
    libxv1 \    
    libx264-152 \
    libx264-dev \
    nasm \
    pkg-config \
    pulseaudio \
    texinfo \
    yasm \
    zlib1g-dev

# Copy the current directory contents into the container at /app
COPY . /app

RUN ./configure  \
    --enable-gpl \ 
    --enable-openssl \
    --enable-version3 \
    --enable-static \
    --disable-sdl2 \
    --disable-debug \
    --disable-ffplay \
    --disable-libxcb \
    --disable-libxcb-shm \
    --disable-indev=sndio \
    --disable-outdev=sndio \
    --disable-outdev=xv \
    --enable-libx264 \
    --enable-libpulse \
    --enable-nonfree \
    --enable-vaapi \
    --enable-libfreetype \
    --enable-fontconfig \
    --extra-libs="-lpthread -lm" \
    --pkg-config-flags=--static
#--enable-decklink \
#--extra-cflags="-Idecklink"

RUN make -j8
