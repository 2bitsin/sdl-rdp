#ifndef SDL_rdpframebuffer_h_
#define SDL_rdpframebuffer_h_
#include "SDL_rdpvideo.h"
bool SDL_RDP_CreateWindowFramebuffer(SDL_VideoDevice* _this, SDL_Window* window, SDL_PixelFormat* format, void** pixels,
                                     int* pitch);
bool SDL_RDP_UpdateWindowFramebuffer(SDL_VideoDevice* _this, SDL_Window* window, SDL_Rect const* rects, int numrects);
void SDL_RDP_DestroyWindowFramebuffer(SDL_VideoDevice* _this, SDL_Window* window);
#endif
