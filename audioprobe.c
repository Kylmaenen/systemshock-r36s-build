#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

static SDL_AudioSpec g_obtained;
static int g_callback_count;
static int g_callback_bytes;
static double g_phase;

static const char *envv(const char *name) {
    const char *v = getenv(name);
    return (v != NULL && v[0] != '\0') ? v : "<unset>";
}

static void set_or_unset(const char *name, const char *value) {
    if (value != NULL && value[0] != '\0')
        setenv(name, value, 1);
    else
        unsetenv(name);
}

static const char *fmt(Uint16 format) {
    switch (format) {
    case AUDIO_U8: return "AUDIO_U8";
    case AUDIO_S8: return "AUDIO_S8";
    case AUDIO_U16LSB: return "AUDIO_U16LSB";
    case AUDIO_S16LSB: return "AUDIO_S16LSB";
    case AUDIO_U16MSB: return "AUDIO_U16MSB";
    case AUDIO_S16MSB: return "AUDIO_S16MSB";
    case AUDIO_S32LSB: return "AUDIO_S32LSB";
    case AUDIO_S32MSB: return "AUDIO_S32MSB";
    case AUDIO_F32LSB: return "AUDIO_F32LSB";
    case AUDIO_F32MSB: return "AUDIO_F32MSB";
    default: return "UNKNOWN";
    }
}

static void tone_callback(void *userdata, Uint8 *stream, int len) {
    (void)userdata;
    SDL_memset(stream, 0, (size_t)len);
    g_callback_count++;
    g_callback_bytes += len;

    if (g_obtained.format == AUDIO_S16SYS && g_obtained.channels > 0) {
        Sint16 *samples = (Sint16 *)stream;
        int frames = len / ((int)sizeof(Sint16) * g_obtained.channels);
        for (int i = 0; i < frames; i++) {
            Sint16 s = (Sint16)(sin(g_phase) * 6000.0);
            g_phase += 2.0 * 3.14159265358979323846 * 440.0 / (double)g_obtained.freq;
            if (g_phase > 2.0 * 3.14159265358979323846)
                g_phase -= 2.0 * 3.14159265358979323846;
            for (int c = 0; c < g_obtained.channels; c++)
                samples[i * g_obtained.channels + c] = s;
        }
    }
}

static void print_sdl_versions(void) {
    SDL_version linked;
    SDL_GetVersion(&linked);
    printf("SDL compiled=%d.%d.%d linked=%d.%d.%d\n",
           SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL,
           linked.major, linked.minor, linked.patch);
    printf("SDL_mixer compiled=%d.%d.%d linked=%d.%d.%d\n",
           SDL_MIXER_MAJOR_VERSION, SDL_MIXER_MINOR_VERSION, SDL_MIXER_PATCHLEVEL,
           Mix_Linked_Version()->major, Mix_Linked_Version()->minor, Mix_Linked_Version()->patch);
}

static void print_audio_drivers(void) {
    int n = SDL_GetNumAudioDrivers();
    printf("Audio drivers:");
    for (int i = 0; i < n; i++)
        printf(" %s", SDL_GetAudioDriver(i));
    printf("\n");
}

static void print_devices(void) {
    int out = SDL_GetNumAudioDevices(0);
    int cap = SDL_GetNumAudioDevices(1);
    printf("Current audio driver: %s\n", SDL_GetCurrentAudioDriver());
    printf("Playback devices: %d\n", out);
    for (int i = 0; i < out; i++)
        printf("  out[%d]=%s\n", i, SDL_GetAudioDeviceName(i, 0));
    printf("Capture devices: %d\n", cap);
    for (int i = 0; i < cap; i++)
        printf("  cap[%d]=%s\n", i, SDL_GetAudioDeviceName(i, 1));
}

static SDL_AudioDeviceID open_device(int freq, int samples, int allow_changes, const char *tag) {
    SDL_AudioSpec want;
    SDL_AudioDeviceID dev;

    SDL_zero(want);
    SDL_zero(g_obtained);
    want.freq = freq;
    want.format = AUDIO_S16SYS;
    want.channels = 2;
    want.samples = (Uint16)samples;
    want.callback = tone_callback;

    g_callback_count = 0;
    g_callback_bytes = 0;
    g_phase = 0.0;
    SDL_ClearError();
    dev = SDL_OpenAudioDevice(NULL, 0, &want, &g_obtained, allow_changes);
    if (dev == 0) {
        printf("%s SDL_OpenAudioDevice freq=%d samples=%d allow=0x%x: FAIL: %s\n",
               tag, freq, samples, allow_changes, SDL_GetError());
        return 0;
    }

    printf("%s SDL_OpenAudioDevice freq=%d samples=%d allow=0x%x: OK id=%u got freq=%d fmt=%s channels=%d samples=%d size=%u\n",
           tag, freq, samples, allow_changes, (unsigned)dev, g_obtained.freq, fmt(g_obtained.format),
           g_obtained.channels, g_obtained.samples, g_obtained.size);
    return dev;
}

static void run_sdl_open_tests(void) {
    static const int freqs[] = {48000, 44100, 22050, 11025};
    static const int samples[] = {2048, 1024, 4096};

    for (int f = 0; f < ARRAY_LEN(freqs); f++) {
        for (int s = 0; s < ARRAY_LEN(samples); s++) {
            SDL_AudioDeviceID dev1 = open_device(freqs[f], samples[s], 0, "primary");
            if (dev1 != 0) {
                SDL_AudioDeviceID dev2;
                SDL_PauseAudioDevice(dev1, 0);
                SDL_Delay(700);
                SDL_PauseAudioDevice(dev1, 1);
                printf("primary callbacks=%d bytes=%d\n", g_callback_count, g_callback_bytes);

                dev2 = open_device(freqs[f], samples[s], 0, "secondary-while-primary-open");
                if (dev2 != 0)
                    SDL_CloseAudioDevice(dev2);
                SDL_CloseAudioDevice(dev1);
            }
        }
    }
}

static void run_mixer_only_tests(void) {
    static const int freqs[] = {48000, 44100, 22050};
    static const int chunks[] = {2048, 1024, 4096};

    for (int f = 0; f < ARRAY_LEN(freqs); f++) {
        for (int c = 0; c < ARRAY_LEN(chunks); c++) {
            int got_freq = 0, got_channels = 0;
            Uint16 got_format = 0;
            SDL_ClearError();
            if (Mix_OpenAudio(freqs[f], AUDIO_S16SYS, 2, chunks[c]) < 0) {
                printf("mixer-only Mix_OpenAudio freq=%d chunk=%d: FAIL: %s\n", freqs[f], chunks[c], Mix_GetError());
                continue;
            }
            Mix_QuerySpec(&got_freq, &got_format, &got_channels);
            printf("mixer-only Mix_OpenAudio freq=%d chunk=%d: OK got freq=%d fmt=%s channels=%d\n",
                   freqs[f], chunks[c], got_freq, fmt(got_format), got_channels);
            Mix_CloseAudio();
        }
    }
}

static void run_mixer_after_sdl_test(void) {
    SDL_AudioDeviceID dev = open_device(48000, 2048, 0, "sdl-before-mixer");
    if (dev == 0)
        return;

    SDL_ClearError();
    if (Mix_OpenAudio(48000, AUDIO_S16SYS, 2, 2048) < 0)
        printf("mixer-after-sdl Mix_OpenAudio: FAIL: %s\n", Mix_GetError());
    else {
        printf("mixer-after-sdl Mix_OpenAudio: OK (unexpected, two opens possible)\n");
        Mix_CloseAudio();
    }
    SDL_CloseAudioDevice(dev);
}

static void run_matrix(const char *label, const char *driver, const char *alsa_set_buffer) {
    printf("\n==============================\n");
    printf("AUDIO MATRIX %s\n", label);
    set_or_unset("SDL_AUDIODRIVER", driver);
    set_or_unset("SDL_AUDIO_ALSA_SET_BUFFER_SIZE", alsa_set_buffer);
    printf("SDL_AUDIODRIVER=%s\n", envv("SDL_AUDIODRIVER"));
    printf("SDL_AUDIO_ALSA_SET_BUFFER_SIZE=%s\n", envv("SDL_AUDIO_ALSA_SET_BUFFER_SIZE"));
    printf("AUDIODEV=%s\n", envv("AUDIODEV"));
    printf("ALSA_CARD=%s\n", envv("ALSA_CARD"));
    printf("PULSE_SERVER=%s\n", envv("PULSE_SERVER"));

    if (SDL_Init(SDL_INIT_AUDIO) != 0) {
        printf("SDL_Init(AUDIO): FAIL: %s\n", SDL_GetError());
        return;
    }

    print_devices();
    run_mixer_only_tests();
    run_mixer_after_sdl_test();
    run_sdl_open_tests();
    SDL_Quit();
}

int main(void) {
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("AUDIOPROBE start\n");
    printf("LD_LIBRARY_PATH=%s\n", envv("LD_LIBRARY_PATH"));
    printf("LD_PRELOAD=%s\n", envv("LD_PRELOAD"));
    print_sdl_versions();
    print_audio_drivers();

    run_matrix("default", NULL, NULL);
    run_matrix("default-alsa-buffer", NULL, "1");
    run_matrix("alsa", "alsa", NULL);
    run_matrix("alsa-buffer", "alsa", "1");
    run_matrix("pulse", "pulse", NULL);
    run_matrix("dsp", "dsp", NULL);
    run_matrix("dummy", "dummy", NULL);

    printf("AUDIOPROBE done\n");
    return 0;
}
