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

RUN mkdir -p /root/systemshock/build_ext \
    && cd /root/systemshock/build_ext \
    && git clone --depth 1 https://github.com/EtherTyper/fluidsynth-lite.git \
    && cd fluidsynth-lite \
    && cmake . \
    && cmake --build . -j4

WORKDIR /root/systemshock
COPY shockolate-sdl-renderer-fallback.patch /tmp/shockolate-sdl-renderer-fallback.patch
COPY shockolate-audio-fallback-v2.patch /tmp/shockolate-audio-fallback-v2.patch
COPY shockolate-audio-resume-v3.patch /tmp/shockolate-audio-resume-v3.patch
COPY apply-r36s-audio-patches.sh /tmp/apply-r36s-audio-patches.sh
RUN git apply --check /tmp/shockolate-sdl-renderer-fallback.patch \
    && git apply /tmp/shockolate-sdl-renderer-fallback.patch \
    && git apply --check /tmp/shockolate-audio-fallback-v2.patch \
    && git apply /tmp/shockolate-audio-fallback-v2.patch \
    && git apply --check /tmp/shockolate-audio-resume-v3.patch \
    && git apply /tmp/shockolate-audio-resume-v3.patch \
    && sh /tmp/apply-r36s-audio-patches.sh \
    && grep -n "ADLMIDI_EMU_NUKED_174\\|int musicrate\\|Mix_SetPostMix" src/MusicSrc/MusicDevice.c src/MacSrc/Xmi.c src/MacSrc/SDLSound.c

RUN cmake -DENABLE_FLUIDSYNTH=BUNDLED . && make -j4

FROM scratch AS export
COPY --from=build /root/systemshock/systemshock /sshock.aarch64
