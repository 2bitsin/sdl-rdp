#include "SDL_rdpvideo.h"

bool SDL_RDP_ParseAspect(const char *value, sdlrdp_aspect *aspect)
{
    char *end;
    unsigned long num, den;
    SDL_zero(*aspect);
    if (!value || !*value) return true;
    num = SDL_strtoul(value, &end, 10);
    if (end == value || *end != ':' || !num || num > SDL_MAX_UINT32)
        return SDL_SetError("Invalid RDP aspect: %s", value);
    value = end + 1;
    den = SDL_strtoul(value, &end, 10);
    if (end == value || *end || !den || den > SDL_MAX_UINT32)
        return SDL_SetError("Invalid RDP aspect denominator: %s", value);
    aspect->num = (unsigned)num;
    aspect->den = (unsigned)den;
    return true;
}

void SDLCALL SDL_RDP_AspectHintChanged(void *userdata, const char *name, const char *oldValue, const char *newValue)
{
    SDL_VideoData *data = userdata;
    sdlrdp_aspect aspect;
    if (!data->handle || !SDL_RDP_ParseAspect(newValue, &aspect)) return;
    if (data->backend.set_aspect(data->handle, aspect) != 0) {
        SDL_SetError("%s", data->backend.last_error());
        return;
    }
    if (data->window)
        SDL_SetStringProperty(SDL_GetWindowProperties(data->window), SDL_PROP_WINDOW_RDP_ASPECT_STRING, newValue ? newValue : "");
}
