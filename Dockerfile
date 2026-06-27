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
        wget \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /root
RUN git clone --depth 1 https://github.com/ricardo-ayres/systemshock.git

# Build against the same full FluidSynth ABI provided by dArkOSRE. The shared
# library is used only for linking; the exported game resolves libfluidsynth.so.3
# from the console at runtime.
RUN mkdir -p /opt/fluidsynth-sysroot \
    && wget -q https://deb.debian.org/debian/pool/main/f/fluidsynth/libfluidsynth3_2.4.4+dfsg-1+deb13u2_arm64.deb -O /tmp/libfluidsynth3.deb \
    && wget -q https://deb.debian.org/debian/pool/main/f/fluidsynth/libfluidsynth-dev_2.4.4+dfsg-1+deb13u2_arm64.deb -O /tmp/libfluidsynth-dev.deb \
    && dpkg-deb -x /tmp/libfluidsynth3.deb /opt/fluidsynth-sysroot \
    && dpkg-deb -x /tmp/libfluidsynth-dev.deb /opt/fluidsynth-sysroot \
    && mkdir -p /root/systemshock/build_ext/fluidsynth-lite/src \
    && ln -s /opt/fluidsynth-sysroot/usr/include /root/systemshock/build_ext/fluidsynth-lite/include \
    && ln -s /opt/fluidsynth-sysroot/usr/lib/aarch64-linux-gnu/libfluidsynth.so /root/systemshock/build_ext/fluidsynth-lite/src/libfluidsynth.so \
    && readelf -d /opt/fluidsynth-sysroot/usr/lib/aarch64-linux-gnu/libfluidsynth.so.3.3.4 | grep 'SONAME.*libfluidsynth.so.3'

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

RUN cmake \
        -DENABLE_FLUIDSYNTH=BUNDLED \
        -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-shlib-undefined" \
        . \
    && make -j4 \
    && readelf -d systemshock | grep 'NEEDED.*libfluidsynth.so.3'

FROM scratch AS export
COPY --from=build /root/systemshock/systemshock /sshock.aarch64
