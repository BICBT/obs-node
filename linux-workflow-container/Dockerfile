FROM ubuntu:20.04

ENV DEBIAN_FRONTEND=noninteractive

# Install build tools
RUN apt-get update && \
    apt-get install -y \
        libssl-dev \
        git \
        cmake \
        build-essential \
        checkinstall

# Build srt
RUN mkdir ~/ffmpeg_sources && \
    cd ~/ffmpeg_sources  && \
    git clone --depth 1 -b v1.4.0 https://github.com/Haivision/srt.git && \
    mkdir srt/build && \
    cd ~/ffmpeg_sources/srt/build && \
    cmake -DENABLE_C_DEPS=ON -DENABLE_SHARED=ON -DENABLE_STATIC=OFF -fPIC .. && \
    make && \
    make install

# Install ffmpeg build dependencies
RUN apt-get -y install \
        autoconf \
        automake \
        git-core \
        libass-dev \
        libfreetype6-dev \
        libgnutls28-dev \
        libsdl2-dev \
        libtool \
        libva-dev \
        libvdpau-dev \
        libvorbis-dev \
        libxcb1-dev \
        libxcb-shm0-dev \
        libxcb-xfixes0-dev \
        pkg-config \
        texinfo \
        wget \
        yasm \
        zlib1g-dev \
        libx264-dev \
        libx265-dev \
        libnuma-dev \
        libfdk-aac-dev \
        libmp3lame-dev

# Build shared ffmpeg with srt support
RUN cd ~/ffmpeg_sources && \
    wget -O ffmpeg-4.2.2.tar.bz2 https://ffmpeg.org/releases/ffmpeg-4.2.2.tar.bz2 && \
    tar xjvf ffmpeg-4.2.2.tar.bz2 && \
    cd ~/ffmpeg_sources/ffmpeg-4.2.2 && \
    PATH="$HOME/bin:$PATH" PKG_CONFIG_PATH="$HOME/ffmpeg_build/lib/pkgconfig" ./configure --prefix=/usr \
        --enable-gpl         \
        --enable-version3    \
        --enable-nonfree     \
        --disable-static     \
        --enable-shared      \
        --disable-debug      \
        --enable-avresample  \
        --enable-libfdk-aac  \
        --enable-libfreetype \
        --enable-libx264     \
        --enable-libx265     \
        --enable-protocol=libsrt \
        --enable-libsrt && \
    make && \
    make install && \
    checkinstall -y --deldoc=yes  && \
    ldconfig

# Install obs-studio build dependencies
RUN apt-get install -y \
        libmbedtls-dev \
        libasound2-dev \
        libcurl4-openssl-dev \
        libfdk-aac-dev \
        libfontconfig-dev \
        libfreetype6-dev \
        libgl1-mesa-dev \
        libjack-jackd2-dev \
        libjansson-dev \
        libluajit-5.1-dev \
        libpulse-dev \
        libqt5x11extras5-dev \
        libspeexdsp-dev \
        libswresample-dev \
        libswscale-dev \
        libudev-dev \
        libv4l-dev \
        libvlc-dev \
        libx11-dev \
        libx264-dev \
        libxcb-shm0-dev \
        libxcb-xinerama0-dev \
        libxcomposite-dev \
        libxinerama-dev \
        pkg-config \
        python3-dev \
        qtbase5-dev \
        libqt5svg5-dev \
        swig \
        libxcb-randr0-dev \
        libxcb-xfixes0-dev \
        libx11-xcb-dev \
        libxcb1-dev \
        x11vnc \
        libnvidia-encode-450 \
        xserver-xorg-video-dummy \
        xserver-xorg-input-void \
        patchelf

# Install obs-node build dependencies
RUN apt-get install -y \
        nodejs \
        npm