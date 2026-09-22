#include "SDL_rdpwindow.h"
#include "src/events/SDL_windowevents_c.h"

bool SDL_RDP_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    if (_this->internal->window) {
        return SDL_SetError("RDP supports one window");
    }
    _this->internal->window = window;
    SDL_RDP_AspectHintChanged(_this->internal, SDL_HINT_RDP_ASPECT, NULL, SDL_GetHint(SDL_HINT_RDP_ASPECT));
    window->x = window->windowed.x = window->floating.x = 0;
    window->y = window->windowed.y = window->floating.y = 0;
    SDL_RDP_ApplyWindowSize(_this->internal, window->w, window->h);
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_OCCLUDED, 0, 0);
    return true;
}

void SDL_RDP_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
    if (_this->internal->window == window) {
        _this->internal->window = NULL;
    }
}

void SDL_RDP_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window)
{
    SDL_RDP_ApplyWindowSize(_this->internal, window->pending.w, window->pending.h);
    window->last_size_pending = false;
}

void SDL_RDP_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
}

void SDL_RDP_ApplyWindowSize(SDL_VideoData *data, int w, int h)
{
    SDL_Window *window = data->window;
    if (data->backend.resize(data->handle, w, h) != 0) {
        SDL_SetError("%s", data->backend.last_error());
        return;
    }
    if (window->w != w || window->h != h) {
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, w, h);
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, w, h);
    }
}

SDL_FullscreenResult SDL_RDP_SetWindowFullscreen(SDL_VideoDevice *_this, SDL_Window *window, SDL_VideoDisplay *display, SDL_FullscreenOp fullscreen)
{
    const SDL_DisplayMode *mode = &display->desktop_mode;
    SDL_RDP_ApplyWindowSize(_this->internal,
        fullscreen == SDL_FULLSCREEN_OP_LEAVE ? window->windowed.w : mode->w,
        fullscreen == SDL_FULLSCREEN_OP_LEAVE ? window->windowed.h : mode->h);
    return SDL_FULLSCREEN_SUCCEEDED;
}
