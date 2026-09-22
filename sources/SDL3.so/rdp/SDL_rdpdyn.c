#include "SDL_rdpdyn.h"
#define SDL_RDP_ABI_VERSION 1
#define SDL_RDP_LOAD(name) \
    backend->name = (void *)SDL_LoadFunction(backend->object, "sdlrdp_" #name); \
    if (!backend->name) { SDL_RDP_UnloadBackend(backend); return false; }

bool SDL_RDP_LoadBackend(SDL_RDP_Backend *backend)
{
    const char *path = SDL_GetHint(SDL_HINT_RDP_BACKEND);
    backend->object = SDL_LoadObject(path && *path ? path : SDL_VIDEO_DRIVER_RDP_DYNAMIC);
    if (!backend->object) {
        return false;
    }
    SDL_RDP_LOAD(version);
    SDL_RDP_LOAD(open);
    SDL_RDP_LOAD(close);
    SDL_RDP_LOAD(port);
    SDL_RDP_LOAD(present);
    SDL_RDP_LOAD(poll);
    SDL_RDP_LOAD(wait);
    SDL_RDP_LOAD(wakeup);
    if (backend->version() != SDL_RDP_ABI_VERSION) {
        SDL_RDP_UnloadBackend(backend);
        return SDL_SetError("RDP backend ABI version mismatch (expected %u)", SDL_RDP_ABI_VERSION);
    }
    SDL_ClearError();
    return true;
}

void SDL_RDP_UnloadBackend(SDL_RDP_Backend *backend)
{
    if (backend->object) {
        SDL_UnloadObject(backend->object);
    }
    SDL_zero(*backend);
}
