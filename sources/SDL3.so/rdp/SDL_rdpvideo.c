#include "SDL_rdpvideo.h"
#include "SDL_rdpclipboard.h"
#include "SDL_rdpwindow.h"
#include "SDL_rdpframebuffer.h"
#include "SDL_rdpevents.h"
#include "src/events/SDL_keyboard_c.h"
#include "src/events/SDL_mouse_c.h"

static void SDLCALL SDL_RDP_CodecHintChanged(void *userdata, const char *name, const char *oldValue, const char *newValue)
{
    SDL_VideoData *data = userdata;
    sdlrdp_codec codec;
    if (data->handle && SDL_RDP_ParseCodec(newValue, &codec) &&
        data->backend.set_codec(data->handle, codec) != 0) {
        SDL_SetError("%s", data->backend.last_error());
    }
}

static bool SDL_RDP_InitDisplay(SDL_VideoData *data, const sdlrdp_config *config)
{
    SDL_DisplayMode mode;
    SDL_zero(mode);
    mode.format = SDL_PIXELFORMAT_XRGB8888;
    mode.w = (int)config->width;
    mode.h = (int)config->height;
    data->display = SDL_AddBasicVideoDisplay(&mode);
    return data->display && SDL_SetNumberProperty(SDL_GetDisplayProperties(data->display),
        SDL_PROP_DISPLAY_RDP_PORT_NUMBER, data->backend.port(data->handle));
}

static bool SDL_RDP_RelativeMouse(bool enabled)
{
    SDL_VideoData *data = SDL_GetVideoDevice()->internal;
    return data->backend.set_relative_mouse(data->handle, enabled) == 0 || SDL_SetError("%s", data->backend.last_error());
}

static bool SDL_RDP_VideoInit(SDL_VideoDevice *_this)
{
    SDL_VideoData *data = _this->internal;
    sdlrdp_config config;
    if (!SDL_RDP_AcquireBackend(&data->backend, &data->handle, &config)) {
        return false;
    }
    if (!SDL_RDP_InitDisplay(data, &config) ||
        !SDL_AddHintCallback(SDL_HINT_RDP_CODEC, SDL_RDP_CodecHintChanged, data) ||
        !SDL_AddHintCallback(SDL_HINT_RDP_ASPECT, SDL_RDP_AspectHintChanged, data)) {
        return false;
    }
    SDL_AddKeyboard(SDL_DEFAULT_KEYBOARD_ID, NULL);
    SDL_AddMouse(SDL_DEFAULT_MOUSE_ID, NULL);
    SDL_GetMouse()->SetRelativeMouseMode = SDL_RDP_RelativeMouse;
    SDL_RDP_InitMouse();
    return true;
}

static void SDL_RDP_VideoQuit(SDL_VideoDevice *_this)
{
    SDL_VideoData *data = _this->internal;
    SDL_RemoveHintCallback(SDL_HINT_RDP_ASPECT, SDL_RDP_AspectHintChanged, data);
    SDL_RemoveHintCallback(SDL_HINT_RDP_CODEC, SDL_RDP_CodecHintChanged, data);
    if (data->handle) {
        SDL_RDP_ReleaseBackend();
        data->handle = NULL;
    }
}

static void SDL_RDP_DeleteDevice(SDL_VideoDevice *device)
{
    if (device->internal) {
        SDL_free(device->internal);
    }
    SDL_free(device);
}

static SDL_VideoDevice *SDL_RDP_CreateDevice(void)
{
    SDL_VideoDevice *device;
    const char *hint = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
    if (!hint || !SDL_strstr(hint, "rdp")) {
        return NULL;
    }
    device = SDL_calloc(1, sizeof(*device));
    if (!device) {
        return NULL;
    }
    device->internal = SDL_calloc(1, sizeof(*device->internal));
    if (!device->internal) {
        SDL_RDP_DeleteDevice(device);
        return NULL;
    }
    device->is_dummy = true;
    device->VideoInit = SDL_RDP_VideoInit;
    device->VideoQuit = SDL_RDP_VideoQuit;
    device->PumpEvents = SDL_RDP_PumpEvents;
    device->WaitEventTimeout = SDL_RDP_WaitEventTimeout;
    device->SendWakeupEvent = SDL_RDP_SendWakeupEvent;
    device->CreateSDLWindow = SDL_RDP_CreateWindow;
    device->DestroyWindow = SDL_RDP_DestroyWindow;
    device->SetWindowFullscreen = SDL_RDP_SetWindowFullscreen;
    device->SetWindowSize = SDL_RDP_SetWindowSize;
    device->ShowWindow = SDL_RDP_ShowWindow;
    device->CreateWindowFramebuffer = SDL_RDP_CreateWindowFramebuffer;
    device->UpdateWindowFramebuffer = SDL_RDP_UpdateWindowFramebuffer;
    device->DestroyWindowFramebuffer = SDL_RDP_DestroyWindowFramebuffer;
    SDL_RDP_InitClipboard(device);
    device->free = SDL_RDP_DeleteDevice;
    return device;
}

VideoBootStrap RDP_bootstrap = {
    "rdp", "SDL RDP video driver", SDL_RDP_CreateDevice, NULL, false
};
