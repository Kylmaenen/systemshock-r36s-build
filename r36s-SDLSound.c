#include "Xmi.h"
#include "MusicDevice.h"

static snd_digi_parms digi_parms_by_channel[SND_MAX_SAMPLES];

#ifdef USE_SDL_MIXER

#include <SDL_mixer.h>

static Mix_Chunk *samples_by_channel[SND_MAX_SAMPLES];
static Uint32 sample_positions[SND_MAX_SAMPLES];
static int sample_loops[SND_MAX_SAMPLES];
static SDL_atomic_t sample_active[SND_MAX_SAMPLES];
static SDL_atomic_t sample_volume[SND_MAX_SAMPLES];
static SDL_atomic_t sample_pan[SND_MAX_SAMPLES];

typedef struct CachedSample {
    int snd_ref;
    int len;
    Mix_Chunk *chunk;
} CachedSample;

#define SAMPLE_CACHE_SIZE 1024
static CachedSample sample_cache[SAMPLE_CACHE_SIZE];
static int sample_cache_count = 0;
static SDL_mutex *stream_mutex = NULL;
static int shock_audio_rate = 32000;
static int shock_audio_chunk = 4096;

extern SDL_AudioStream *cutscene_audiostream;
extern struct MusicDevice *MusicDev;

extern void MusicCallback(void *userdata, Uint8 *stream, int len);

static int ShockEnvInt(const char *name, int fallback, int minv, int maxv) {
    const char *value = SDL_getenv(name);
    int parsed;
    if (value == NULL || value[0] == 0)
        return fallback;
    parsed = SDL_atoi(value);
    if (parsed < minv || parsed > maxv)
        return fallback;
    return parsed;
}

static Mix_Chunk *ShockGetCachedSample(int snd_ref, int len, uchar *smp) {
    int i;
    Mix_Chunk *chunk;

    for (i = 0; i < sample_cache_count; ++i) {
        if (sample_cache[i].snd_ref == snd_ref && sample_cache[i].len == len)
            return sample_cache[i].chunk;
    }

    chunk = Mix_LoadWAV_RW(SDL_RWFromConstMem(smp, len), 1);
    if (chunk == NULL)
        return NULL;

    if (sample_cache_count >= SAMPLE_CACHE_SIZE) {
        Mix_FreeChunk(chunk);
        return NULL;
    }

    sample_cache[sample_cache_count].snd_ref = snd_ref;
    sample_cache[sample_cache_count].len = len;
    sample_cache[sample_cache_count].chunk = chunk;
    sample_cache_count++;
    return chunk;
}

static Sint16 ShockClampSample(int value) {
    if (value > 32767)
        return 32767;
    if (value < -32768)
        return -32768;
    return (Sint16)value;
}

static void ShockMixSamples(Uint8 *stream, int len) {
    int channel;

    for (channel = 0; channel < SND_MAX_SAMPLES; ++channel) {
        Mix_Chunk *sample;
        Uint32 output_pos = 0;

        if (!SDL_AtomicGet(&sample_active[channel]))
            continue;

        sample = samples_by_channel[channel];
        if (sample == NULL || sample->abuf == NULL || sample->alen < 4) {
            SDL_AtomicSet(&sample_active[channel], 0);
            continue;
        }

        while (output_pos + 4 <= (Uint32)len && SDL_AtomicGet(&sample_active[channel])) {
            Uint32 pos = sample_positions[channel];
            Uint32 available;
            Uint32 take;
            Uint32 i;
            int volume;
            int pan;
            int left_gain;
            int right_gain;

            if (pos >= sample->alen) {
                if (sample_loops[channel] == -1 || sample_loops[channel] > 0) {
                    if (sample_loops[channel] > 0)
                        sample_loops[channel]--;
                    pos = 0;
                } else {
                    SDL_AtomicSet(&sample_active[channel], 0);
                    samples_by_channel[channel] = NULL;
                    break;
                }
            }

            available = sample->alen - pos;
            take = (Uint32)len - output_pos;
            if (take > available)
                take = available;
            take &= ~3u;
            if (take == 0) {
                sample_positions[channel] = sample->alen;
                continue;
            }

            volume = SDL_AtomicGet(&sample_volume[channel]);
            pan = SDL_AtomicGet(&sample_pan[channel]);
            if (volume < 0) volume = 0;
            if (volume > 128) volume = 128;
            if (pan < 1) pan = 1;
            if (pan > 127) pan = 127;
            left_gain = volume * (254 - 2 * pan);
            right_gain = volume * (2 * pan);

            for (i = 0; i < take; i += 4) {
                Sint16 *dst = (Sint16 *)(stream + output_pos + i);
                const Sint16 *src = (const Sint16 *)(sample->abuf + pos + i);
                dst[0] = ShockClampSample((int)dst[0] + ((int)src[0] * left_gain) / (128 * 255));
                dst[1] = ShockClampSample((int)dst[1] + ((int)src[1] * right_gain) / (128 * 255));
            }

            sample_positions[channel] = pos + take;
            output_pos += take;
        }
    }
}

static void ShockPostMix(void *userdata, unsigned char *stream, int len) {
    SDL_AudioStream *as;

    if (stream_mutex != NULL)
        SDL_LockMutex(stream_mutex);

    as = userdata != NULL ? *(SDL_AudioStream **)userdata : NULL;
    if (as != NULL && SDL_AudioStreamAvailable(as) > 0) {
        Uint8 *mix_stream = SDL_malloc((size_t)len);
        if (mix_stream != NULL) {
            int got;
            SDL_memset(mix_stream, 0, (size_t)len);
            got = SDL_AudioStreamGet(as, mix_stream, len);
            if (got > 0)
                SDL_MixAudioFormat(stream, mix_stream, AUDIO_S16SYS, (Uint32)got, SDL_MIX_MAXVOLUME);
            SDL_free(mix_stream);
        }
    }

    if (stream_mutex != NULL)
        SDL_UnlockMutex(stream_mutex);

    ShockMixSamples(stream, len);
}

void snd_audio_lock(void) {
    if (stream_mutex != NULL)
        SDL_LockMutex(stream_mutex);
}

void snd_audio_unlock(void) {
    if (stream_mutex != NULL)
        SDL_UnlockMutex(stream_mutex);
}

int snd_using_primary_audio_mix(void) { return 0; }
int snd_output_rate(void) { return shock_audio_rate; }

int snd_start_digital(void) {
    int mix_freq = 0;
    int mix_channels = 0;
    Uint16 mix_format = 0;

    extern SDL_AudioDeviceID device;
    device = 0;
    stream_mutex = SDL_CreateMutex();

    if (Mix_Init(MIX_INIT_MP3) < 0)
        ERROR("%s: Init failed", __FUNCTION__);

    shock_audio_rate = ShockEnvInt("SHOCK_AUDIO_RATE", 32000, 22050, 96000);
    shock_audio_chunk = ShockEnvInt("SHOCK_AUDIO_CHUNK", 4096, 512, 16384);
    INFO("Requested SDL_mixer audio, freq %d, chunk %d", shock_audio_rate, shock_audio_chunk);

    if (Mix_OpenAudio(shock_audio_rate, AUDIO_S16SYS, 2, shock_audio_chunk) < 0) {
        ERROR("%s: Couldn't open SDL_mixer audio device: %s", __FUNCTION__, Mix_GetError());
        return ERR_NOEFFECT;
    }

    Mix_QuerySpec(&mix_freq, &mix_format, &mix_channels);
    if (mix_freq > 0)
        shock_audio_rate = mix_freq;
    INFO("Opened SDL_mixer audio, freq %d, format %d, channels %d, chunk %d",
         mix_freq, mix_format, mix_channels, shock_audio_chunk);

    Mix_AllocateChannels(SND_MAX_SAMPLES);
    Mix_HookMusic(MusicCallback, (void *)&MusicDev);
    Mix_SetPostMix(ShockPostMix, (void *)&cutscene_audiostream);
    Mix_VolumeMusic(MIX_MAX_VOLUME);

    InitReadXMI();

    atexit(Mix_CloseAudio);
    return OK;
}

int snd_sample_play(int snd_ref, int len, uchar *smp, struct snd_digi_parms *dprm) {
    Mix_Chunk *sample = ShockGetCachedSample(snd_ref, len, smp);
    int channel;
    int volume;

    if (sample == NULL) {
        DEBUG("%s: Failed to load sample", __FUNCTION__);
        return ERR_NOEFFECT;
    }

    for (channel = 0; channel < SND_MAX_SAMPLES; ++channel) {
        if (!SDL_AtomicGet(&sample_active[channel])) {
            samples_by_channel[channel] = sample;
            sample_positions[channel] = 0;
            sample_loops[channel] = dprm->loops > 0 ? dprm->loops - 1 : -1;
            digi_parms_by_channel[channel] = *dprm;

            volume = (dprm->vol * 128) / 100;
            if (volume > 128) volume = 128;
            if (volume < 0) volume = 0;
            SDL_AtomicSet(&sample_volume[channel], volume);
            SDL_AtomicSet(&sample_pan[channel], dprm->pan);
            SDL_MemoryBarrierRelease();
            SDL_AtomicSet(&sample_active[channel], 1);
            return channel;
        }
    }

    return ERR_NOEFFECT;
}

void snd_end_sample(int hnd_id) {
    if (hnd_id < 0 || hnd_id >= SND_MAX_SAMPLES)
        return;
    SDL_AtomicSet(&sample_active[hnd_id], 0);
    samples_by_channel[hnd_id] = NULL;
    sample_positions[hnd_id] = 0;
    sample_loops[hnd_id] = 0;
}

bool snd_sample_playing(int hnd_id) {
    if (hnd_id < 0 || hnd_id >= SND_MAX_SAMPLES)
        return false;
    return SDL_AtomicGet(&sample_active[hnd_id]) != 0;
}

snd_digi_parms *snd_sample_parms(int hnd_id) {
    if (hnd_id < 0 || hnd_id >= SND_MAX_SAMPLES)
        return &digi_parms_by_channel[0];
    return &digi_parms_by_channel[hnd_id];
}

void snd_kill_all_samples(void) {
    int channel;
    for (channel = 0; channel < SND_MAX_SAMPLES; ++channel)
        snd_end_sample(channel);

    snd_audio_lock();
    if (cutscene_audiostream != NULL)
        SDL_AudioStreamClear(cutscene_audiostream);
    snd_audio_unlock();
}

void snd_sample_reload_parms(snd_digi_parms *sdp) {
    int channel;
    int volume;

    if (sdp < digi_parms_by_channel || sdp >= digi_parms_by_channel + SND_MAX_SAMPLES)
        return;
    channel = (int)(sdp - digi_parms_by_channel);
    if (!SDL_AtomicGet(&sample_active[channel]))
        return;

    volume = (sdp->vol * 128) / 100;
    if (volume > 128) volume = 128;
    if (volume < 0) volume = 0;
    SDL_AtomicSet(&sample_volume[channel], volume);
    SDL_AtomicSet(&sample_pan[channel], sdp->pan);
}

int is_playing = 0;

int MacTuneLoadTheme(char *theme_base, int themeID) {
    char filename[40];
    FILE *f;
    int i;

#define NUM_SCORES 8
#define SUPERCHUNKS_PER_SCORE 4
#define NUM_TRANSITIONS 9
#define NUM_LAYERS 32
#define MAX_KEYS 10
#define NUM_LAYERABLE_SUPERCHUNKS 22
#define KEY_BAR_RESOLUTION 2

    extern uchar track_table[NUM_SCORES][SUPERCHUNKS_PER_SCORE];
    extern uchar transition_table[NUM_TRANSITIONS];
    extern uchar layering_table[NUM_LAYERS][MAX_KEYS];
    extern uchar key_table[NUM_LAYERABLE_SUPERCHUNKS][KEY_BAR_RESOLUTION];

    StopTheMusic();

    FreeXMI();

    if (strncmp(theme_base, "thm", 3)) {
        sprintf(filename, "res/sound/%s/%s.xmi", MusicDev->musicType, theme_base);
        ReadXMI(filename);
    } else {
        sprintf(filename, "res/sound/%s/thm%i.xmi", MusicDev->musicType, themeID);
        ReadXMI(filename);

        sprintf(filename, "res/sound/thm%i.bin", themeID);
        extern FILE *fopen_caseless(const char *path, const char *mode); // see caseless.c
        f = fopen_caseless(filename, "rb");
        if (f != 0) {
            fread(track_table, NUM_SCORES * SUPERCHUNKS_PER_SCORE, 1, f);
            fread(transition_table, NUM_TRANSITIONS, 1, f);
            fread(layering_table, NUM_LAYERS * MAX_KEYS, 1, f);
            fread(key_table, NUM_LAYERABLE_SUPERCHUNKS * KEY_BAR_RESOLUTION, 1, f);

            fclose(f);
        }
    }

    return OK;
}

void MacTuneKillCurrentTheme(void) { StopTheMusic(); }

#else

// Sound stubs that do nothing, when SDL Mixer is not found

int snd_start_digital(void) { return OK; }
int snd_sample_play(int snd_ref, int len, uchar *smp, struct snd_digi_parms *dprm) { return OK; }
int snd_alog_play(int snd_ref, int len, uchar *smp, struct snd_digi_parms *dprm) { return OK; }
void snd_end_sample(int hnd_id) {}
void snd_kill_all_samples(void) {}
int MacTuneLoadTheme(char *theme_base, int themeID) { return OK; }
void MacTuneKillCurrentTheme(void) {}
snd_digi_parms *snd_sample_parms(int hnd_id) { return &digi_parms_by_channel[0]; }
bool snd_sample_playing(int hnd_id) { return false; }
void snd_sample_reload_parms(snd_digi_parms *sdp) {}

#endif

// Unimplemented sound stubs

void snd_startup(void) {}
int snd_stop_digital(void) { return 1; }
