#include "SDL_rdpvideo.h"

void SDLCALL SDL_RDP_AspectHintChanged(void* userdata, char const* name, char const* oldValue, char const* newValue) {
  SDL_VideoData* data = userdata;
  sdlrdp_aspect aspect;
  newValue = SDL_RDP_HintChangedValue(name, oldValue, newValue);
  if (!data->handle || !SDL_RDP_ParseAspect(newValue, &aspect)) return;
  if (data->backend.set_aspect(data->handle, aspect) != 0) {
    SDL_SetError("%s", data->backend.last_error());
    return;
  }
  if (data->window)
    SDL_SetStringProperty(SDL_GetWindowProperties(data->window), SDL_PROP_WINDOW_RDP_ASPECT_STRING,
                          newValue ? newValue : "");
}
