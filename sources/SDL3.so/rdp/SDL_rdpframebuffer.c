#include "SDL_rdpframebuffer.h"
#include "src/SDL_properties_c.h"
#define SDL_RDP_SURFACE "SDL.internal.window.surface"

bool SDL_RDP_CreateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, SDL_PixelFormat *format, void **pixels, int *pitch)
{
    SDL_Surface *surface;
    int w, h;
    if (!SDL_GetWindowSizeInPixels(window, &w, &h)) {
        return false;
    }
    surface = SDL_CreateSurface(w, h, SDL_PIXELFORMAT_XRGB8888);
    if (!surface) {
        return false;
    }
    SDL_SetSurfaceProperty(SDL_GetWindowProperties(window), SDL_RDP_SURFACE, surface);
    *format = surface->format;
    *pixels = surface->pixels;
    *pitch = surface->pitch;
    return true;
}

static sdlrdp_rect SDL_RDP_ConvertRect(const SDL_Rect *rect)
{
    sdlrdp_rect converted = { rect->x, rect->y, rect->w, rect->h };
    return converted;
}

bool SDL_RDP_UpdateWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window, const SDL_Rect *rects, int numrects)
{
    SDL_VideoData *data = _this->internal;
    SDL_Surface *surface = SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_RDP_SURFACE, NULL);
    sdlrdp_rect *damage;
    int i, result;
    bool isstack;
    if (!surface) {
        return SDL_SetError("Couldn't find RDP surface for window");
    }
    if (numrects <= 0) {
        return true;
    }
    damage = SDL_small_alloc(sdlrdp_rect, (size_t)numrects, &isstack);
    if (!damage) {
        return false;
    }
    for (i = 0; i < numrects; ++i) {
        damage[i] = SDL_RDP_ConvertRect(&rects[i]);
    }
    result = data->backend.present(data->handle, surface->pixels, surface->pitch,
                                   surface->w, surface->h, damage, numrects);
    SDL_small_free(damage, isstack);
    if (result == 0 && SDL_RDP_SettingBoolean(SDL_HINT_RDP_VSYNC, true)) {
        result = data->backend.wait_frame(data->handle, 100) < 0 ? -1 : 0;
    }
    return result == 0 || SDL_SetError("%s", data->backend.last_error());
}

void SDL_RDP_DestroyWindowFramebuffer(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_ClearProperty(SDL_GetWindowProperties(window), SDL_RDP_SURFACE);
}
