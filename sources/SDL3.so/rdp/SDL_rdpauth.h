#ifndef SDL_rdpauth_h_
#define SDL_rdpauth_h_
#include "SDL_rdpdyn.h"
bool SDL_RDP_AuthConfig(sdlrdp_config* config, SDL_RDP_Backend* backend);
void SDL_RDP_AuthRelease(void);
void SDL_RDP_AuthDisplay(SDL_PropertiesID properties);
#endif
