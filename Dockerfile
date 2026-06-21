FROM --platform=linux/arm64 ubuntu:19.10 AS build

ENV DEBIAN_FRONTEND=noninteractive

COPY sources.list /etc/apt/sources.list

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        build-essential \
        ca-certificates \
        cmake \
        git \
        libsdl2-dev \
        libsdl2-image-dev \
        libsdl2-mixer-dev \
        pkg-config \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /root
RUN git clone --depth 1 https://github.com/ricardo-ayres/systemshock.git

WORKDIR /root/systemshock
COPY shockolate-sdl-renderer-fallback.patch /tmp/shockolate-sdl-renderer-fallback.patch
RUN git apply --check /tmp/shockolate-sdl-renderer-fallback.patch \
    && git apply /tmp/shockolate-sdl-renderer-fallback.patch

RUN cmake -S . -B build \
        -DENABLE_OPENGL=OFF \
        -DENABLE_SDL2=ON \
        -DENABLE_SOUND=ON \
        -DENABLE_FLUIDSYNTH=OFF \
    && cmake --build build --parallel 2

FROM scratch AS export
COPY --from=build /root/systemshock/build/systemshock /sshock.aarch64

