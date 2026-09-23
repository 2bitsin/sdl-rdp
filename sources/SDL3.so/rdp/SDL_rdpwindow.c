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
    if (!SDL_RDP_ResizePicture(_this->internal, window->w, window->h)) {
        _this->internal->window = NULL;
        return false;
    }
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
    int w = window->pending.w, h = window->pending.h;
    if (SDL_RDP_ResizePicture(_this->internal, w, h)) {
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, w, h);
    }
    window->last_size_pending = false;
}

void SDL_RDP_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
}

bool SDL_RDP_ResizePicture(SDL_VideoData *data, int w, int h)
{
    return data->backend.resize(data->handle, w, h) == 0 ||
        SDL_SetError("%s", data->backend.last_error());
}

SDL_FullscreenResult SDL_RDP_SetWindowFullscreen(SDL_VideoDevice *_this, SDL_Window *window, SDL_VideoDisplay *display, SDL_FullscreenOp fullscreen)
{
    const SDL_DisplayMode *mode = window->requested_fullscreen_mode.w ?
        &window->requested_fullscreen_mode : &display->desktop_mode;
    SDL_VideoData *data = _this->internal;
    int w = fullscreen == SDL_FULLSCREEN_OP_LEAVE ? window->windowed.w : mode->w;
    int h = fullscreen == SDL_FULLSCREEN_OP_LEAVE ? window->windowed.h : mode->h;
    if ((w != window->w || h != window->h) && !SDL_RDP_ResizePicture(data, w, h)) {
        return SDL_FULLSCREEN_FAILED;
    }
    if (fullscreen == SDL_FULLSCREEN_OP_LEAVE) {
        SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, w, h);
    }
    return SDL_FULLSCREEN_SUCCEEDED;
}
