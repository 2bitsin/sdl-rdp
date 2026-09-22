#ifndef SDL_rdpvideo_h_
#define SDL_rdpvideo_h_
#include "src/video/SDL_sysvideo.h"
#include "SDL_rdpdyn.h"
struct SDL_VideoData
{
    SDL_RDP_Backend backend;
    sdlrdp_handle *handle;
    SDL_DisplayID display;
    SDL_Window *window;
    bool connected;
};
#endif
