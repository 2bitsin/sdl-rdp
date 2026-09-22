#include "SDL_rdpwindow.h"
#include "src/events/SDL_windowevents_c.h"

bool SDL_RDP_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props)
{
    if (_this->internal->window) {
        return SDL_SetError("RDP supports one window");
    }
    _this->internal->window = window;
    window->x = 0;
    window->y = 0;
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
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->pending.w, window->pending.h);
}

void SDL_RDP_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window)
{
}
