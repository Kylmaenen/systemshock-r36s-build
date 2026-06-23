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
RUN git apply --check /tmp/shockolate-sdl-renderer-fallback.patch \
    && git apply /tmp/shockolate-sdl-renderer-fallback.patch \
    && git apply --check /tmp/shockolate-audio-fallback-v2.patch \
    && git apply /tmp/shockolate-audio-fallback-v2.patch \
    && git apply --check /tmp/shockolate-audio-resume-v3.patch \
    && git apply /tmp/shockolate-audio-resume-v3.patch \
    && git apply --check /tmp/shockolate-adlmidi-dosbox-v4.patch \
    && git apply /tmp/shockolate-adlmidi-dosbox-v4.patch \
    && perl -0pi -e 's#static int mix_music_in_sdl_device = 0;\n#static int mix_music_in_sdl_device = 0;\nstatic int shock_audio_rate = 22050;\n#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#int snd_start_digital\(void\) \{#int snd_output_rate(void) {\n    return shock_audio_rate;\n}\n\nint snd_start_digital(void) {#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#SDL_AudioSpec spec, obtained;\n    spec.freq = 48000;#SDL_AudioSpec spec, obtained;\n    spec.freq = 22050;#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#    \} else \{\n        INFO\("Opened Music Stream#    } else {\n        shock_audio_rate = obtained.freq;\n        INFO("Opened Music Stream#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#    Mix_Chunk \*sample = Mix_LoadWAV_RW\(SDL_RWFromConstMem\(smp, len\), 1\);\n    if \(sample == NULL\)\n        return ERR_NOEFFECT;#    Mix_Chunk *sample = Mix_LoadWAV_RW(SDL_RWFromConstMem(smp, len), 1);\n    if (sample == NULL) {\n        INFO("%s: Failed to load sample ref=%d len=%d: %s", __FUNCTION__, snd_ref, len, Mix_GetError());\n        return ERR_NOEFFECT;\n    }#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#            if \(samples_by_channel\[channel\] == NULL\) \{\n                samples_by_channel#            if (samples_by_channel[channel] == NULL) {\n                INFO("%s: Queue fallback sample ref=%d len=%d chunk=%u channel=%d loops=%d",\n                     __FUNCTION__, snd_ref, len, sample->alen, channel, loops);\n                samples_by_channel#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#\n        Mix_FreeChunk\(sample\);\n        return ERR_NOEFFECT;\n    \}\n\n    int channel = Mix_PlayChannel#\n        INFO("%s: No fallback channel for sample ref=%d len=%d", __FUNCTION__, snd_ref, len);\n        Mix_FreeChunk(sample);\n        return ERR_NOEFFECT;\n    }\n\n    int channel = Mix_PlayChannel#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#DEBUG\("%s: Failed to play sample", __FUNCTION__\);#INFO("%s: Failed to play sample ref=%d len=%d: %s", __FUNCTION__, snd_ref, len, Mix_GetError());#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#static SDL_mutex \*MyMutex;\n#static SDL_mutex *MyMutex;\n\nextern int snd_output_rate(void);\n#' src/MacSrc/Xmi.c \
    && perl -0pi -e 's#int musicrate = 48000;#int musicrate = snd_output_rate();#' src/MacSrc/Xmi.c \
    && perl -0pi -e 's#extern int snd_using_primary_audio_mix\(void\);\n#extern int snd_using_primary_audio_mix(void);\nextern int snd_output_rate(void);\n#' src/GameSrc/cutsloop.c \
    && perl -0pi -e 's#fix_int\(amovie->a.sampleRate\), AUDIO_S16SYS, 2, 48000\)#fix_int(amovie->a.sampleRate), AUDIO_S16SYS, 2, snd_output_rate())#' src/GameSrc/cutsloop.c \
    && perl -0pi -e 's#extern int snd_using_primary_audio_mix\(void\);\n#extern int snd_using_primary_audio_mix(void);\nextern int snd_output_rate(void);\n#' src/GameSrc/audiolog.c \
    && perl -0pi -e 's#fix_int\(palog->a.sampleRate\), AUDIO_S16SYS, 2, 48000\)#fix_int(palog->a.sampleRate), AUDIO_S16SYS, 2, snd_output_rate())#' src/GameSrc/audiolog.c \
    && grep -R "snd_output_rate\|shock_audio_rate\|Queue fallback sample" -n src/MacSrc/SDLSound.c src/MacSrc/Xmi.c src/GameSrc/cutsloop.c src/GameSrc/audiolog.c

RUN cmake . && make -j4

FROM scratch AS export
COPY --from=build /root/systemshock/systemshock /sshock.aarch64

