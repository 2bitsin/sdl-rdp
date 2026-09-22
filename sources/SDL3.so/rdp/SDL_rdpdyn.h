#ifndef SDL_rdpdyn_h_
#define SDL_rdpdyn_h_
#include "SDL_internal.h"
#include "sdl-rdp-backend.so/sdl-rdp-backend.h"
typedef struct SDL_RDP_Backend
{
    SDL_SharedObject *object;
    unsigned (*version)(void);
    int (*open)(const sdlrdp_config *, sdlrdp_handle **);
    void (*close)(sdlrdp_handle *);
    unsigned (*port)(const sdlrdp_handle *);
    int (*present)(sdlrdp_handle *, const void *, int, unsigned, unsigned, const sdlrdp_rect *, unsigned);
    unsigned (*poll)(sdlrdp_handle *, sdlrdp_event *, unsigned);
    int (*wait)(sdlrdp_handle *, int);
    void (*wakeup)(sdlrdp_handle *);
} SDL_RDP_Backend;
bool SDL_RDP_LoadBackend(SDL_RDP_Backend *backend);
void SDL_RDP_UnloadBackend(SDL_RDP_Backend *backend);
#endif
