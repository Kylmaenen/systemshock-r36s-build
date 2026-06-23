#define _GNU_SOURCE

#include <SDL2/SDL.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static unsigned long render_copy_calls;
static unsigned long render_present_calls;
static unsigned long open_audio_device_calls;

static void *next_symbol(const char *name) {
    void *symbol = dlsym(RTLD_NEXT, name);
    if (symbol == NULL)
        fprintf(stderr, "SDL_SHIM: dlsym(%s) failed: %s\n", name, dlerror());
    return symbol;
}

static const char *sdl_error(void) {
    typedef const char *(*get_error_fn)(void);
    static get_error_fn real_get_error;

    if (real_get_error == NULL)
        real_get_error = (get_error_fn)next_symbol("SDL_GetError");

    return real_get_error != NULL ? real_get_error() : "<SDL_GetError unavailable>";
}

static void set_sdl_error(const char *message) {
    typedef int (*set_error_fn)(const char *, ...);
    static set_error_fn real_set_error;

    if (real_set_error == NULL)
        real_set_error = (set_error_fn)next_symbol("SDL_SetError");

    if (real_set_error != NULL)
        real_set_error("%s", message);
}

__attribute__((constructor)) static void shim_loaded(void) {
    fprintf(stderr, "SDL_SHIM: loaded\n");
}

char *SDL_strtokr(char *string, const char *delimiters, char **saveptr) {
    return strtok_r(string, delimiters, saveptr);
}

SDL_bool SDL_SetHint(const char *name, const char *value) {
    typedef SDL_bool (*set_hint_fn)(const char *, const char *);
    static set_hint_fn real_set_hint;
    const char *effective_value = value;

    if (real_set_hint == NULL)
        real_set_hint = (set_hint_fn)next_symbol("SDL_SetHint");

    if (name != NULL && strcmp(name, SDL_HINT_RENDER_DRIVER) == 0) {
        const char *forced = getenv("SHOCK_SDL_FORCE_RENDERER");
        if (forced != NULL && forced[0] != '\0')
            effective_value = forced;

        fprintf(stderr, "SDL_SHIM: SDL_SetHint(%s, %s) -> %s\n", name,
                value != NULL ? value : "<null>",
                effective_value != NULL ? effective_value : "<null>");
    }

    return real_set_hint != NULL ? real_set_hint(name, effective_value) : SDL_FALSE;
}

int SDL_GL_SetAttribute(SDL_GLattr attr, int value) {
    typedef int (*set_attribute_fn)(SDL_GLattr, int);
    static set_attribute_fn real_set_attribute;
    int effective_value = value;

    if (real_set_attribute == NULL)
        real_set_attribute = (set_attribute_fn)next_symbol("SDL_GL_SetAttribute");

    if (attr == SDL_GL_CONTEXT_PROFILE_MASK &&
        getenv("SHOCK_SDL_FORCE_GLES") != NULL) {
        effective_value = SDL_GL_CONTEXT_PROFILE_ES;
        fprintf(stderr, "SDL_SHIM: SDL_GL_CONTEXT_PROFILE_MASK %d -> ES (%d)\n",
                value, effective_value);
    }

    return real_set_attribute != NULL
               ? real_set_attribute(attr, effective_value)
               : -1;
}

SDL_AudioDeviceID SDL_OpenAudioDevice(const char *device, int iscapture,
                                      const SDL_AudioSpec *desired,
                                      SDL_AudioSpec *obtained,
                                      int allowed_changes) {
    typedef SDL_AudioDeviceID (*open_audio_device_fn)(const char *, int, const SDL_AudioSpec *, SDL_AudioSpec *, int);
    static open_audio_device_fn real_open_audio_device;
    SDL_AudioDeviceID result;

    if (real_open_audio_device == NULL)
        real_open_audio_device = (open_audio_device_fn)next_symbol("SDL_OpenAudioDevice");

    open_audio_device_calls++;
    if (open_audio_device_calls == 1 && getenv("SHOCK_SDL_SKIP_FIRST_AUDIO_DEVICE") != NULL) {
        set_sdl_error("first SDL_OpenAudioDevice skipped so SDL_mixer can own audio");
        fprintf(stderr, "SDL_SHIM: SDL_OpenAudioDevice #%lu skipped for SDL_mixer handoff\n",
                open_audio_device_calls);
        (void)device;
        (void)iscapture;
        (void)desired;
        (void)obtained;
        (void)allowed_changes;
        return 0;
    }

    result = real_open_audio_device != NULL
                 ? real_open_audio_device(device, iscapture, desired, obtained, allowed_changes)
                 : 0;
    fprintf(stderr, "SDL_SHIM: SDL_OpenAudioDevice #%lu -> %u (%s)\n",
            open_audio_device_calls, result, result != 0 ? "ok" : sdl_error());
    return result;
}

SDL_Window *SDL_CreateWindow(const char *title, int x, int y, int w, int h, Uint32 flags) {
    typedef SDL_Window *(*create_window_fn)(const char *, int, int, int, int, Uint32);
    static create_window_fn real_create_window;
    SDL_Window *result;

    if (real_create_window == NULL)
        real_create_window = (create_window_fn)next_symbol("SDL_CreateWindow");

    result = real_create_window != NULL ? real_create_window(title, x, y, w, h, flags) : NULL;
    fprintf(stderr, "SDL_SHIM: SDL_CreateWindow(%dx%d, flags=0x%08x) -> %p (%s)\n",
            w, h, flags, (void *)result, result != NULL ? "ok" : sdl_error());
    return result;
}

SDL_Renderer *SDL_CreateRenderer(SDL_Window *window, int index, Uint32 flags) {
    typedef SDL_Renderer *(*create_renderer_fn)(SDL_Window *, int, Uint32);
    typedef int (*get_renderer_info_fn)(SDL_Renderer *, SDL_RendererInfo *);
    static create_renderer_fn real_create_renderer;
    static get_renderer_info_fn real_get_renderer_info;
    SDL_Renderer *result;
    SDL_RendererInfo info;

    if (real_create_renderer == NULL)
        real_create_renderer = (create_renderer_fn)next_symbol("SDL_CreateRenderer");
    if (real_get_renderer_info == NULL)
        real_get_renderer_info = (get_renderer_info_fn)next_symbol("SDL_GetRendererInfo");

    result = real_create_renderer != NULL ? real_create_renderer(window, index, flags) : NULL;
    if (result != NULL && real_get_renderer_info != NULL &&
        real_get_renderer_info(result, &info) == 0) {
        fprintf(stderr, "SDL_SHIM: SDL_CreateRenderer(flags=0x%08x) -> %s, flags=0x%08x, max=%dx%d\n",
                flags, info.name, info.flags, info.max_texture_width, info.max_texture_height);
    } else {
        fprintf(stderr, "SDL_SHIM: SDL_CreateRenderer(flags=0x%08x) -> %p (%s)\n",
                flags, (void *)result, result != NULL ? "info unavailable" : sdl_error());
    }
    return result;
}

int SDL_RenderCopy(SDL_Renderer *renderer, SDL_Texture *texture,
                   const SDL_Rect *srcrect, const SDL_Rect *dstrect) {
    typedef int (*render_copy_fn)(SDL_Renderer *, SDL_Texture *, const SDL_Rect *, const SDL_Rect *);
    static render_copy_fn real_render_copy;
    int result;

    if (real_render_copy == NULL)
        real_render_copy = (render_copy_fn)next_symbol("SDL_RenderCopy");

    result = real_render_copy != NULL ? real_render_copy(renderer, texture, srcrect, dstrect) : -1;
    render_copy_calls++;
    if (render_copy_calls <= 5 || render_copy_calls % 6000 == 0)
        fprintf(stderr, "SDL_SHIM: SDL_RenderCopy #%lu -> %d (%s)\n",
                render_copy_calls, result, result == 0 ? "ok" : sdl_error());
    return result;
}

void SDL_RenderPresent(SDL_Renderer *renderer) {
    typedef void (*render_present_fn)(SDL_Renderer *);
    static render_present_fn real_render_present;

    if (real_render_present == NULL)
        real_render_present = (render_present_fn)next_symbol("SDL_RenderPresent");

    render_present_calls++;
    if (render_present_calls <= 5 || render_present_calls % 6000 == 0)
        fprintf(stderr, "SDL_SHIM: SDL_RenderPresent #%lu\n", render_present_calls);

    if (real_render_present != NULL)
        real_render_present(renderer);
}
