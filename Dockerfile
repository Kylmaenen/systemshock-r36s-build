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
    && perl -0pi -e 's#static int mix_music_in_sdl_device = 0;\n#static int mix_music_in_sdl_device = 0;\nstatic int shock_audio_rate = 48000;\n#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#int snd_using_primary_audio_mix\(void\) \{#static void ShockPostMix(void *userdata, unsigned char *stream, int len) {\n    SDL_AudioStream *as = userdata != NULL ? *(SDL_AudioStream **)userdata : NULL;\n\n    if (as != NULL && SDL_AudioStreamAvailable(as) > 0) {\n        Uint8 *mix_stream = SDL_malloc((size_t)len);\n        if (mix_stream != NULL) {\n            int got;\n            SDL_memset(mix_stream, 0, (size_t)len);\n            got = SDL_AudioStreamGet(as, mix_stream, len);\n            if (got > 0)\n                SDL_MixAudioFormat(stream, mix_stream, AUDIO_S16SYS, (Uint32)got, SDL_MIX_MAXVOLUME);\n            SDL_free(mix_stream);\n        }\n    }\n}\n\nint snd_using_primary_audio_mix(void) {#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#int snd_start_digital\(void\) \{#int snd_output_rate(void) {\n    return shock_audio_rate;\n}\n\nint snd_start_digital(void) {#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#    SDL_AudioSpec spec, obtained;\n    spec.freq = 48000;\n    spec.format = AUDIO_S16SYS;\n    spec.channels = 2;\n    spec.samples = 2048;\n    spec.callback = ShockAudioCallback;\n    spec.userdata = \(void \*\)&cutscene_audiostream;\n\n    extern SDL_AudioDeviceID device;\n    device = SDL_OpenAudioDevice\(NULL, 0, &spec, &obtained, 0\);\n\n    if \(device == 0\) \{\n        ERROR\("Could not open SDL audio: %s", SDL_GetError\(\)\);\n    \} else \{\n        INFO\("Opened Music Stream, deviceID %d, freq %d, size %d, format %d, channels %d, samples %d", device,\n             obtained.freq, obtained.size, obtained.format, obtained.channels, obtained.samples\);\n    \}\n#    extern SDL_AudioDeviceID device;\n    int mix_freq = 0, mix_channels = 0;\n    Uint16 mix_format = 0;\n\n    device = 0;\n#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#    if \(Mix_OpenAudio\(48000, AUDIO_S16SYS, 2, 2048\) < 0\) \{\n        ERROR\("%s: Couldn.t open audio device", __FUNCTION__\);\n        INFO\("%s: Routing MIDI through the primary SDL audio device", __FUNCTION__\);\n        mix_music_in_sdl_device = 1;\n        SDL_PauseAudioDevice\(device, 0\);\n    \} else \{\n        Mix_HookMusic\(MusicCallback, \(void \*\)&MusicDev\);\n        Mix_VolumeMusic\(MIX_MAX_VOLUME\); // use max volume for music stream\n    \}#    if (Mix_OpenAudio(48000, AUDIO_S16SYS, 2, 2048) < 0) {\n        ERROR("%s: Couldn.t open SDL_mixer audio device: %s", __FUNCTION__, Mix_GetError());\n        return ERR_NOEFFECT;\n    } else {\n        Mix_QuerySpec(&mix_freq, &mix_format, &mix_channels);\n        if (mix_freq > 0)\n            shock_audio_rate = mix_freq;\n        INFO("Opened SDL_mixer audio, freq %d, format %d, channels %d", mix_freq, mix_format, mix_channels);\n        Mix_SetPostMix(ShockPostMix, (void *)&cutscene_audiostream);\n        Mix_HookMusic(MusicCallback, (void *)&MusicDev);\n        Mix_VolumeMusic(MIX_MAX_VOLUME); // use max volume for music stream\n    }#' src/MacSrc/SDLSound.c \
    && perl -0pi -e 's#static SDL_mutex \*MyMutex;\n#static SDL_mutex *MyMutex;\n\nextern int snd_output_rate(void);\n#' src/MacSrc/Xmi.c \
    && perl -0pi -e 's#int musicrate = 48000;#int musicrate = snd_output_rate();#' src/MacSrc/Xmi.c \
    && perl -0pi -e 's#extern int snd_using_primary_audio_mix\(void\);\n#extern int snd_using_primary_audio_mix(void);\nextern int snd_output_rate(void);\n#' src/GameSrc/cutsloop.c \
    && perl -0pi -e 's#SDL_PauseAudioDevice\(device, ([01])\);#if (device != 0) SDL_PauseAudioDevice(device, $1);#g' src/GameSrc/cutsloop.c \
    && perl -0pi -e 's#fix_int\(amovie->a.sampleRate\), AUDIO_S16SYS, 2, 48000\)#fix_int(amovie->a.sampleRate), AUDIO_S16SYS, 2, snd_output_rate())#' src/GameSrc/cutsloop.c \
    && perl -0pi -e 's#extern int snd_using_primary_audio_mix\(void\);\n#extern int snd_using_primary_audio_mix(void);\nextern int snd_output_rate(void);\n#' src/GameSrc/audiolog.c \
    && perl -0pi -e 's#SDL_PauseAudioDevice\(device, ([01])\);#if (device != 0) SDL_PauseAudioDevice(device, $1);#g' src/GameSrc/audiolog.c \
    && perl -0pi -e 's#fix_int\(palog->a.sampleRate\), AUDIO_S16SYS, 2, 48000\)#fix_int(palog->a.sampleRate), AUDIO_S16SYS, 2, snd_output_rate())#' src/GameSrc/audiolog.c \
    && grep -R "Opened SDL_mixer audio\|Mix_SetPostMix\|snd_output_rate\|SDL_PauseAudioDevice" -n src/MacSrc/SDLSound.c src/MacSrc/Xmi.c src/GameSrc/cutsloop.c src/GameSrc/audiolog.c

RUN cmake . && make -j4

FROM scratch AS export
COPY --from=build /root/systemshock/systemshock /sshock.aarch64

