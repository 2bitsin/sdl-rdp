#ifndef SDL_rdpevents_h_
#define SDL_rdpevents_h_
#include "SDL_rdpvideo.h"
void SDL_RDP_DesktopMode(SDL_VideoData* data, int w, int h);
void SDL_RDP_PumpEvents(SDL_VideoDevice* _this);
int  SDL_RDP_WaitEventTimeout(SDL_VideoDevice* _this, Sint64 timeoutNS);
void SDL_RDP_SendWakeupEvent(SDL_VideoDevice* _this, SDL_Window* window);
#endif
