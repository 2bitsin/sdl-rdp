#ifndef SDL_rdpclipboard_h_
#define SDL_rdpclipboard_h_
#include "SDL_rdpvideo.h"
void SDL_RDP_InitClipboard(SDL_VideoDevice *device);
void SDL_RDP_ClipboardUpdate(SDL_VideoData *data);
#endif
