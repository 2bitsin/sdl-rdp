#ifndef SDL_rdpwindow_h_
#define SDL_rdpwindow_h_
#include "SDL_rdpvideo.h"
struct SDL_WindowData
{
    int requested_w, requested_h;
};
void SDL_RDP_ApplyWindowSize(SDL_VideoData *data);
bool SDL_RDP_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props);
void SDL_RDP_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window);
void SDL_RDP_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window);
void SDL_RDP_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window);
#endif
