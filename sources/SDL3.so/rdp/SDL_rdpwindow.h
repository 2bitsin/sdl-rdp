#ifndef SDL_rdpwindow_h_
#define SDL_rdpwindow_h_
#include "SDL_rdpvideo.h"
bool SDL_RDP_ResizePicture(SDL_VideoData *data, int w, int h);
SDL_FullscreenResult SDL_RDP_SetWindowFullscreen(SDL_VideoDevice *_this, SDL_Window *window, SDL_VideoDisplay *display, SDL_FullscreenOp fullscreen);
bool SDL_RDP_CreateWindow(SDL_VideoDevice *_this, SDL_Window *window, SDL_PropertiesID props);
void SDL_RDP_DestroyWindow(SDL_VideoDevice *_this, SDL_Window *window);
void SDL_RDP_SetWindowSize(SDL_VideoDevice *_this, SDL_Window *window);
void SDL_RDP_ShowWindow(SDL_VideoDevice *_this, SDL_Window *window);
#endif
