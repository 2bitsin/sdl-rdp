#include "SDL_rdpdyn.h"
#define SDL_RDP_LOAD(name) \
    backend->name = (void *)SDL_LoadFunction(backend->object, "sdlrdp_" #name); \
    if (!backend->name) { SDL_RDP_UnloadBackend(backend); return false; }

static bool SDL_RDP_LoadAuthentication(SDL_RDP_Backend *backend)
{
    if (backend->version() != SDLRDP_ABI_VERSION) {
        SDL_RDP_UnloadBackend(backend);
        return SDL_SetError("RDP backend ABI version mismatch (expected %u)", SDLRDP_ABI_VERSION);
    }
    SDL_RDP_LOAD(verify_pair);
    SDL_RDP_LOAD(lookup_pair);
    return true;
}

static bool SDL_RDP_LoadChannels(SDL_RDP_Backend *backend)
{
    SDL_RDP_LOAD(set_clipboard_text);
    SDL_RDP_LOAD(get_clipboard_text);
    SDL_RDP_LOAD(has_clipboard_text);
    SDL_RDP_LOAD(audio_open);
    SDL_RDP_LOAD(audio_rate);
    SDL_RDP_LOAD(audio_write);
    SDL_RDP_LOAD(audio_wait);
    SDL_RDP_LOAD(audio_close);
    SDL_RDP_LOAD(drive_list);
    SDL_RDP_LOAD(drive_open);
    SDL_RDP_LOAD(drive_read);
    SDL_RDP_LOAD(drive_write);
    SDL_RDP_LOAD(drive_stat);
    SDL_RDP_LOAD(drive_enumerate);
    SDL_RDP_LOAD(drive_mkdir);
    SDL_RDP_LOAD(drive_remove);
    SDL_RDP_LOAD(drive_rename);
    SDL_RDP_LOAD(drive_fstat);
    SDL_RDP_LOAD(drive_flush);
    SDL_RDP_LOAD(drive_close);
    return true;
}

bool SDL_RDP_LoadBackend(SDL_RDP_Backend *backend)
{
    const char *path = SDL_GetHint(SDL_HINT_RDP_BACKEND);
    backend->object = SDL_LoadObject(path && *path ? path : SDL_RDP_DYNAMIC);
    if (!backend->object) {
        return false;
    }
    SDL_RDP_LOAD(last_error);
    SDL_RDP_LOAD(version);
    if (!SDL_RDP_LoadAuthentication(backend)) return false;
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
    if (!SDL_RDP_LoadChannels(backend)) return false;
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
