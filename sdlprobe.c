#include <SDL2/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define ARRAY_LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

typedef struct WindowCase {
    const char *name;
    Uint32 flags;
    int profile;
    int major;
    int minor;
    int red;
    int green;
    int blue;
    int alpha;
    int depth;
    int stencil;
    int buffer;
} WindowCase;

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

static void print_drivers(void) {
    int i;
    printf("SDL version linked: %d.%d.%d\n", SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL);
    SDL_version compiled;
    SDL_VERSION(&compiled);
    printf("SDL version compiled: %d.%d.%d\n", compiled.major, compiled.minor, compiled.patch);

    printf("Video drivers:");
    for (i = 0; i < SDL_GetNumVideoDrivers(); i++)
        printf(" %s", SDL_GetVideoDriver(i));
    printf("\n");

    printf("Render drivers:\n");
    for (i = 0; i < SDL_GetNumRenderDrivers(); i++) {
        SDL_RendererInfo info;
        if (SDL_GetRenderDriverInfo(i, &info) == 0)
            printf("  %d: %s flags=0x%08x max=%dx%d\n", i, info.name, info.flags,
                   info.max_texture_width, info.max_texture_height);
    }
}

static void print_displays(void) {
    int displays = SDL_GetNumVideoDisplays();
    int d;
    printf("Current video driver: %s\n", SDL_GetCurrentVideoDriver());
    printf("Displays: %d\n", displays);
    for (d = 0; d < displays; d++) {
        SDL_DisplayMode current;
        int modes = SDL_GetNumDisplayModes(d);
        int m;
        if (SDL_GetCurrentDisplayMode(d, &current) == 0)
            printf("  display %d current: %dx%d fmt=%s refresh=%d modes=%d\n", d,
                   current.w, current.h, SDL_GetPixelFormatName(current.format),
                   current.refresh_rate, modes);
        else
            printf("  display %d current: ERROR %s modes=%d\n", d, SDL_GetError(), modes);

        for (m = 0; m < modes && m < 8; m++) {
            SDL_DisplayMode mode;
            if (SDL_GetDisplayMode(d, m, &mode) == 0)
                printf("    mode %d: %dx%d fmt=%s refresh=%d\n", m, mode.w, mode.h,
                       SDL_GetPixelFormatName(mode.format), mode.refresh_rate);
        }
    }
}

static void apply_gl_attrs(const WindowCase *c) {
    SDL_GL_ResetAttributes();
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, c->profile);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, c->major);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, c->minor);
    SDL_GL_SetAttribute(SDL_GL_RED_SIZE, c->red);
    SDL_GL_SetAttribute(SDL_GL_GREEN_SIZE, c->green);
    SDL_GL_SetAttribute(SDL_GL_BLUE_SIZE, c->blue);
    SDL_GL_SetAttribute(SDL_GL_ALPHA_SIZE, c->alpha);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, c->depth);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, c->stencil);
    SDL_GL_SetAttribute(SDL_GL_BUFFER_SIZE, c->buffer);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
}

static int try_case(const WindowCase *c) {
    SDL_Window *window;
    SDL_GLContext ctx = NULL;
    SDL_Renderer *renderer = NULL;
    int ok = 0;

    printf("\nCASE %s\n", c->name);
    printf("  flags=0x%08x profile=%s gl=%d.%d rgba=%d/%d/%d/%d depth=%d stencil=%d buffer=%d\n",
           c->flags,
           c->profile == SDL_GL_CONTEXT_PROFILE_ES ? "ES" :
           c->profile == SDL_GL_CONTEXT_PROFILE_CORE ? "CORE" : "OTHER",
           c->major, c->minor, c->red, c->green, c->blue, c->alpha,
           c->depth, c->stencil, c->buffer);

    SDL_ClearError();
    apply_gl_attrs(c);
    window = SDL_CreateWindow("sdlprobe", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                              640, 480, c->flags);
    if (window == NULL) {
        printf("  SDL_CreateWindow: FAIL: %s\n", SDL_GetError());
        return 0;
    }
    printf("  SDL_CreateWindow: OK\n");

    if ((c->flags & SDL_WINDOW_OPENGL) != 0) {
        SDL_ClearError();
        ctx = SDL_GL_CreateContext(window);
        if (ctx == NULL) {
            printf("  SDL_GL_CreateContext: FAIL: %s\n", SDL_GetError());
        } else {
            int actual_profile = 0, actual_major = 0, actual_minor = 0;
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, &actual_profile);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, &actual_major);
            SDL_GL_GetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, &actual_minor);
            printf("  SDL_GL_CreateContext: OK profile=0x%x version=%d.%d\n",
                   actual_profile, actual_major, actual_minor);
            SDL_GL_SwapWindow(window);
            ok = 1;
        }
    }

    SDL_ClearError();
    renderer = SDL_CreateRenderer(window, -1, 0);
    if (renderer == NULL) {
        printf("  SDL_CreateRenderer(default): FAIL: %s\n", SDL_GetError());
    } else {
        SDL_RendererInfo info;
        if (SDL_GetRendererInfo(renderer, &info) == 0)
            printf("  SDL_CreateRenderer(default): OK name=%s flags=0x%08x\n", info.name, info.flags);
        SDL_SetRenderDrawColor(renderer, 32, 160, 96, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        SDL_DestroyRenderer(renderer);
        ok = 1;
    }

    if (ctx != NULL)
        SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(window);
    SDL_Delay(150);
    return ok;
}

static int run_matrix(const char *label, const char *video_driver,
                      const char *render_driver, const char *gl_driver,
                      const char *egl_driver) {
    static const WindowCase cases[] = {
        {"shockolate-original", SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI,
         SDL_GL_CONTEXT_PROFILE_CORE, 2, 0, 8, 8, 8, 8, 24, 8, 32},
        {"original-no-alpha-stencil", SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI,
         SDL_GL_CONTEXT_PROFILE_CORE, 2, 0, 8, 8, 8, 0, 16, 0, 24},
        {"gles2-rgba8888", SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI,
         SDL_GL_CONTEXT_PROFILE_ES, 2, 0, 8, 8, 8, 8, 16, 0, 32},
        {"gles2-rgb565", SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI,
         SDL_GL_CONTEXT_PROFILE_ES, 2, 0, 5, 6, 5, 0, 16, 0, 16},
        {"fullscreen-desktop-gles2", SDL_WINDOW_OPENGL | SDL_WINDOW_FULLSCREEN_DESKTOP,
         SDL_GL_CONTEXT_PROFILE_ES, 2, 0, 5, 6, 5, 0, 16, 0, 16},
        {"no-opengl-software-renderer", SDL_WINDOW_SHOWN,
         SDL_GL_CONTEXT_PROFILE_ES, 2, 0, 5, 6, 5, 0, 16, 0, 16}
    };
    int i;
    int successes = 0;

    set_or_unset("SDL_VIDEODRIVER", video_driver);
    set_or_unset("SDL_RENDER_DRIVER", render_driver);
    set_or_unset("SDL_VIDEO_GL_DRIVER", gl_driver);
    set_or_unset("SDL_VIDEO_EGL_DRIVER", egl_driver);

    printf("\n==============================\n");
    printf("MATRIX %s\n", label);
    printf("SDL_VIDEODRIVER=%s\n", envv("SDL_VIDEODRIVER"));
    printf("SDL_RENDER_DRIVER=%s\n", envv("SDL_RENDER_DRIVER"));
    printf("SDL_VIDEO_GL_DRIVER=%s\n", envv("SDL_VIDEO_GL_DRIVER"));
    printf("SDL_VIDEO_EGL_DRIVER=%s\n", envv("SDL_VIDEO_EGL_DRIVER"));

    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        printf("SDL_Init(VIDEO): FAIL: %s\n", SDL_GetError());
        return 0;
    }

    print_drivers();
    print_displays();

    for (i = 0; i < ARRAY_LEN(cases); i++)
        successes += try_case(&cases[i]) ? 1 : 0;

    SDL_Quit();
    printf("MATRIX %s successes=%d/%d\n", label, successes, ARRAY_LEN(cases));
    return successes;
}

int main(void) {
    int total = 0;
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);

    printf("SDLPROBE start\n");
    printf("Initial LD_LIBRARY_PATH=%s\n", envv("LD_LIBRARY_PATH"));
    printf("Initial LD_PRELOAD=%s\n", envv("LD_PRELOAD"));

    total += run_matrix("default-env", NULL, NULL, NULL, NULL);
    total += run_matrix("kmsdrm-default", "kmsdrm", NULL, NULL, NULL);
    total += run_matrix("kmsdrm-gles-system", "kmsdrm", "opengles2", "libGLESv2.so", "libEGL.so");
    total += run_matrix("kmsdrm-software-system-egl", "kmsdrm", "software", "libGLESv2.so", "libEGL.so");

    printf("\nSDLPROBE total_successes=%d\n", total);
    return total > 0 ? 0 : 2;
}
