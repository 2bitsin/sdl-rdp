#ifndef SDL_rdpdyn_h_
#define SDL_rdpdyn_h_
#include "SDL_internal.h"
#include "sdl-rdp-backend.so/sdl-rdp-backend.h"
typedef struct SDL_RDP_Backend
{
    SDL_SharedObject *object;
    const char *(*last_error)(void);
    unsigned (*version)(void);
    int (*open)(const sdlrdp_config *, sdlrdp_handle **);
    void (*close)(sdlrdp_handle *);
    unsigned (*port)(const sdlrdp_handle *);
    int (*present)(sdlrdp_handle *, const void *, int, unsigned, unsigned, const sdlrdp_rect *, unsigned);
    unsigned (*poll)(sdlrdp_handle *, sdlrdp_event *, unsigned);
    int (*wait)(sdlrdp_handle *, int);
    int (*set_relative_mouse)(sdlrdp_handle *, int);
    int (*set_codec)(sdlrdp_handle *, sdlrdp_codec);
    int (*set_pointer)(sdlrdp_handle *, unsigned, unsigned, unsigned, unsigned, const void *);
    int (*wait_frame)(sdlrdp_handle *, int);
    int (*resize)(sdlrdp_handle *, unsigned, unsigned);
    int (*set_aspect)(sdlrdp_handle *, sdlrdp_aspect);
    int (*set_clipboard_text)(sdlrdp_handle *, const char *);
    const char *(*get_clipboard_text)(sdlrdp_handle *);
    int (*has_clipboard_text)(sdlrdp_handle *);
    int (*audio_open)(sdlrdp_handle *);
    unsigned (*audio_rate)(sdlrdp_handle *);
    int (*audio_write)(sdlrdp_handle *, const void *, unsigned);
    int (*audio_wait)(sdlrdp_handle *, int);
    void (*audio_close)(sdlrdp_handle *);
    void (*wakeup)(sdlrdp_handle *);
} SDL_RDP_Backend;
bool SDL_RDP_LoadBackend(SDL_RDP_Backend *backend);
void SDL_RDP_UnloadBackend(SDL_RDP_Backend *backend);
bool SDL_RDP_AcquireBackend(SDL_RDP_Backend *backend, sdlrdp_handle **handle, sdlrdp_config *config);
void SDL_RDP_ReleaseBackend(void);
bool SDL_RDP_ParseCodec(const char *name, sdlrdp_codec *codec);
bool SDL_RDP_ParseAspect(const char *value, sdlrdp_aspect *aspect);
#endif
