#!/bin/sh
set -eu

# The complete clip is already resident in memory. Convert and enqueue it before
# playback so the audio callback never contends with per-frame stream writes.
perl -0pi -e 's#    cutscene_audiostream = SDL_NewAudioStream\(AUDIO_U8, 1, fix_int\(amovie->a.sampleRate\), AUDIO_S16SYS, 2, snd_output_rate\(\)\);\n\n    cutscene_audiobuffer_pos = cutscene_audiobuffer;#    snd_audio_lock();\n    cutscene_audiostream = SDL_NewAudioStream(AUDIO_U8, 1, fix_int(amovie->a.sampleRate), AUDIO_S16SYS, 2, snd_output_rate());\n    cutscene_audiobuffer_pos = cutscene_audiobuffer;\n    int prebuffer_blocks = 2;\n    while (cutscene_audiobuffer_size > 0 && prebuffer_blocks-- > 0)\n    {\n      SDL_AudioStreamPut(cutscene_audiostream, cutscene_audiobuffer_pos, MOVIE_DEFAULT_BLOCKLEN);\n      cutscene_audiobuffer_pos += MOVIE_DEFAULT_BLOCKLEN;\n      cutscene_audiobuffer_size--;\n    }\n    snd_audio_unlock();#' src/GameSrc/cutsloop.c

perl -0pi -e 's#    cutscene_audiostream = SDL_NewAudioStream\(AUDIO_U8, 1, fix_int\(palog->a.sampleRate\), AUDIO_S16SYS, 2, snd_output_rate\(\)\);\n\n    audiolog_audiobuffer_pos = audiolog_audiobuffer;#    snd_audio_lock();\n    cutscene_audiostream = SDL_NewAudioStream(AUDIO_U8, 1, fix_int(palog->a.sampleRate), AUDIO_S16SYS, 2, snd_output_rate());\n    audiolog_audiobuffer_pos = audiolog_audiobuffer;\n    int prebuffer_blocks = 2;\n    while (audiolog_audiobuffer_size > 0 && prebuffer_blocks-- > 0) {\n        int i;\n        int vol = curr_alog_vol * 127 / 100;\n        for (i = 0; i < MOVIE_DEFAULT_BLOCKLEN; i++)\n            audiolog_audiobuffer_pos[i] = 128 + ((int)audiolog_audiobuffer_pos[i] - 128) * vol / 128;\n        SDL_AudioStreamPut(cutscene_audiostream, audiolog_audiobuffer_pos, MOVIE_DEFAULT_BLOCKLEN);\n        audiolog_audiobuffer_pos += MOVIE_DEFAULT_BLOCKLEN;\n        audiolog_audiobuffer_size--;\n    }\n    snd_audio_unlock();#' src/GameSrc/audiolog.c

# Raise only FluidSynth master gain; in-game music volume remains user-controlled.
perl -0pi -e 's#fluid_settings_setnum\(settings, "synth.gain", 0\.5\);#fluid_settings_setnum(settings, "synth.gain", 0.9);#' src/MusicSrc/MusicDevice.c

grep -n "prebuffer_blocks = 2\\|prebuffer_blocks-- > 0" src/GameSrc/cutsloop.c src/GameSrc/audiolog.c\ngrep -n "synth.gain.*, 0.9" src/MusicSrc/MusicDevice.c
