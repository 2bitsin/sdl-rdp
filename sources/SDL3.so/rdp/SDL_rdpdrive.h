#ifndef SDL_rdpdrive_h_
#define SDL_rdpdrive_h_
#include "SDL_rdpdyn.h"
bool SDL_RDP_FindDrive(SDL_RDP_Backend *, sdlrdp_handle *, const char *, unsigned *);
SDL_IOStream *SDLCALL SDL_RDP_OpenFile(const char *, const char *, const char *);
void SDL_RDP_UpdateDrives(SDL_RDP_Backend *, sdlrdp_handle *, SDL_PropertiesID);
#endif
