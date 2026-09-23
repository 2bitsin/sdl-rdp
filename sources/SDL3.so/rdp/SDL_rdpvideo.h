#ifndef SDL_rdpvideo_h_
#define SDL_rdpvideo_h_
#include "SDL_rdpdyn.h"
#include "src/video/SDL_sysvideo.h"
struct SDL_VideoData {
  SDL_RDP_Backend backend;
  sdlrdp_handle*  handle;
  SDL_DisplayID   display;
  SDL_Window*     window;
  int             picture_width;
  int             picture_height;
  unsigned        audio_rate;
  SDL_DisplayMode refresh_modes[2];
};
void SDL_RDP_InitMouse(void);
void SDLCALL SDL_RDP_AspectHintChanged(void* userdata, char const* name, char const* oldValue, char const* newValue);
char const* SDL_RDP_CodecName(sdlrdp_codec codec);
#endif
