#ifndef SDL_rdpdrive_h_
#define SDL_rdpdrive_h_
#include "SDL_rdpdyn.h"
bool SDL_RDP_FindDrive(SDL_RDP_Backend* backend, sdlrdp_handle* handle, char const* name, unsigned* id);
SDL_IOStream* SDLCALL SDL_RDP_OpenFile(char const* drive, char const* path, char const* mode);
void                  SDL_RDP_UpdateDrives(SDL_RDP_Backend* backend, sdlrdp_handle* handle, SDL_PropertiesID props);
#endif
