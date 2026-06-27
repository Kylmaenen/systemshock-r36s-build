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
COPY shockolate-adlmidi-dosbox-v4.patch /tmp/shockolate-adlmidi-dosbox-v4.patch
COPY apply-r36s-audio-patches.sh /tmp/apply-r36s-audio-patches.sh
RUN sed -i "/MUSIC_RENDER_FRAMES/{s|#define MUSIC_RENDER_FRAMES 512|enum { MUSIC_RENDER_FRAMES = 512 };|; s|#define MUSIC_FIFO_TARGET_BYTES 32768|enum { MUSIC_FIFO_TARGET_BYTES = 32768 };|;}" /tmp/apply-r36s-audio-patches.sh \
    && git apply --check /tmp/shockolate-sdl-renderer-fallback.patch \
    && git apply /tmp/shockolate-sdl-renderer-fallback.patch \
    && git apply --check /tmp/shockolate-audio-fallback-v2.patch \
    && git apply /tmp/shockolate-audio-fallback-v2.patch \
    && git apply --check /tmp/shockolate-audio-resume-v3.patch \
    && git apply /tmp/shockolate-audio-resume-v3.patch \
    && git apply --check /tmp/shockolate-adlmidi-dosbox-v4.patch \
    && git apply /tmp/shockolate-adlmidi-dosbox-v4.patch \
    && sh /tmp/apply-r36s-audio-patches.sh \
    && grep -n "SDL_NewAudioStream\\|int musicrate\\|snd_sample_preload" src/MacSrc/Xmi.c src/MacSrc/SDLSound.c

RUN cmake . && make -j4

FROM scratch AS export
COPY --from=build /root/systemshock/systemshock /sshock.aarch64
