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
COPY shockolate-audio-fallback-v2.patch /tmp/shockolate-audio-fallback-v2.patch
COPY shockolate-audio-resume-v3.patch /tmp/shockolate-audio-resume-v3.patch
RUN git apply --check /tmp/shockolate-sdl-renderer-fallback.patch \
    && git apply /tmp/shockolate-sdl-renderer-fallback.patch \
    && git apply --check /tmp/shockolate-audio-fallback-v2.patch \
    && git apply /tmp/shockolate-audio-fallback-v2.patch \
    && git apply --check /tmp/shockolate-audio-resume-v3.patch \
    && git apply /tmp/shockolate-audio-resume-v3.patch

RUN cmake . && make -j4

FROM scratch AS export
COPY --from=build /root/systemshock/systemshock /sshock.aarch64

