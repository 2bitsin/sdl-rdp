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
    int picture_width, picture_height;
    SDL_DisplayMode refresh_modes[2];
};
void SDL_RDP_InitMouse(void);
bool SDL_RDP_ParseAspect(const char *value, sdlrdp_aspect *aspect);
void SDLCALL SDL_RDP_AspectHintChanged(void *userdata, const char *name, const char *oldValue, const char *newValue);
const char *SDL_RDP_CodecName(sdlrdp_codec codec);
bool SDL_RDP_ParseCodec(const char *name, sdlrdp_codec *codec);
#endif
