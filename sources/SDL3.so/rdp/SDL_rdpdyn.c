#include "SDL_rdpdyn.h"
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
    SDL_RDP_LOAD(last_error);
    SDL_RDP_LOAD(version);
    SDL_RDP_LOAD(open);
    SDL_RDP_LOAD(close);
    SDL_RDP_LOAD(port);
    SDL_RDP_LOAD(present);
    SDL_RDP_LOAD(poll);
    SDL_RDP_LOAD(wait);
    SDL_RDP_LOAD(wakeup);
    SDL_RDP_LOAD(set_codec);
    SDL_RDP_LOAD(set_relative_mouse);
    SDL_RDP_LOAD(wait_frame);
    SDL_RDP_LOAD(resize);
    SDL_RDP_LOAD(set_aspect);
    SDL_RDP_LOAD(set_pointer);
    SDL_RDP_LOAD(set_clipboard_text);
    SDL_RDP_LOAD(get_clipboard_text);
    SDL_RDP_LOAD(has_clipboard_text);
    if (backend->version() != SDLRDP_ABI_VERSION) {
        SDL_RDP_UnloadBackend(backend);
        return SDL_SetError("RDP backend ABI version mismatch (expected %u)", SDLRDP_ABI_VERSION);
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
