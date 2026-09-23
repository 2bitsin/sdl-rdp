#ifndef SDL_rdpregistry_h_
#define SDL_rdpregistry_h_
#include "SDL_rdpdyn.h"
#include "SDL_rdpini.h"

struct SDL_RDP_Registry {
  SDL_InitState    shared_init;
  SDL_Mutex*       shared_lock;
  unsigned         shared_refs;
  SDL_RDP_Backend  shared_backend;
  sdlrdp_handle*   shared_handle;
  sdlrdp_config    shared_config;
  SDL_AtomicInt    auth_properties;
  SDL_RDP_Backend* auth_backend;
  sdlrdp_config*   auth_config;
  SDL_InitState    ini_init;
  char const* ini_values[SDL_RDP_SETTING_COUNT];
  char* ini_text;
  bool  ini_failed;
  char* ini_failed_path;
};

struct SDL_RDP_Registry* SDL_RDP_Registry(void);
#endif
