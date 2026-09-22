#include "SDL_rdpwindow.h"
#include "src/events/SDL_windowevents_c.h"

bool SDL_RDP_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    if (_this->internal->window) {
        return SDL_SetError("RDP supports one window");
    }
    window->internal = SDL_calloc(1, sizeof(*window->internal));
    if (!window->internal) {
        return false;
    }
    window->internal->requested_w = window->w;
    window->internal->requested_h = window->h;
    _this->internal->window = window;
    window->x = window->windowed.x = window->floating.x = 0;
    window->y = window->windowed.y = window->floating.y = 0;
    SDL_RDP_ApplyWindowSize(_this->internal);
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_OCCLUDED, 0, 0);
    return true;
}

void SDL_RDP_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_free(window->internal);
    window->internal = NULL;
    if (_this->internal->window == window) {
        _this->internal->window = NULL;
    }
}

void SDL_RDP_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    window->internal->requested_w = window->pending.w;
    window->internal->requested_h = window->pending.h;
    SDL_RDP_ApplyWindowSize(_this->internal);
    window->last_size_pending = false;
}

void SDL_RDP_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
}

void SDL_RDP_ApplyWindowSize(SDL_VideoData *data)
{
    SDL_Window *window = data->window;
    const SDL_DisplayMode *mode = &SDL_GetVideoDisplay(data->display)->desktop_mode;
    int w = SDL_min(window->internal->requested_w, mode->w);
    int h = SDL_min(window->internal->requested_h, mode->h);
    if (window->w != w || window->h != h) {
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, w, h);
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, w, h);
    }
}
