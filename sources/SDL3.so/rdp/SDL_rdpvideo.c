#include "SDL_rdpvideo.h"
#include "SDL_rdpclipboard.h"
#include "SDL_rdpwindow.h"
#include "SDL_rdpframebuffer.h"
#include "SDL_rdpevents.h"
#include "src/events/SDL_keyboard_c.h"
#include "src/events/SDL_mouse_c.h"

static int SDL_RDP_GetInteger(const char *name, int fallback)
{
    const char *hint = SDL_GetHint(name);
    char *end;
    long value;
    if (!hint) {
        return fallback;
    }
    value = SDL_strtol(hint, &end, 10);
    return !*hint || *end || value < 0 || value > SDL_MAX_SINT32 ? -1 : (int)value;
}

static void SDL_RDP_Log(void *user, sdlrdp_log_level level, const char *text)
{
    static const SDL_LogPriority priorities[] = {
        SDL_LOG_PRIORITY_ERROR, SDL_LOG_PRIORITY_WARN, SDL_LOG_PRIORITY_INFO
    };
    SDL_assert((unsigned)level < SDL_arraysize(priorities));
    SDL_LogMessage(SDL_LOG_CATEGORY_VIDEO, priorities[level], "%s", text);
}

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

static bool SDL_RDP_VideoInit(SDL_VideoDevice *_this)
{
    SDL_VideoData *data = _this->internal;
    sdlrdp_config config;
    SDL_zero(config);
    config.log = SDL_RDP_Log;
    config.user = NULL;
    config.bind = SDL_GetHint(SDL_HINT_RDP_BIND);
    config.cert_dir = SDL_GetHint(SDL_HINT_RDP_CERT_DIR);
    config.port = SDL_RDP_GetInteger(SDL_HINT_RDP_PORT, 3389);
    config.width = SDL_RDP_GetInteger(SDL_HINT_RDP_WIDTH, 1024);
    config.height = SDL_RDP_GetInteger(SDL_HINT_RDP_HEIGHT, 768);
    config.wait_for_client = SDL_GetHintBoolean(SDL_HINT_RDP_WAIT_FOR_CLIENT, false);
    if (config.port > 65535 || !config.width || config.width > SDL_MAX_SINT32 ||
        !config.height || config.height > SDL_MAX_SINT32) {
        return SDL_SetError("Invalid RDP port or dimensions");
    }
    if (!SDL_RDP_ParseCodec(SDL_GetHint(SDL_HINT_RDP_CODEC), &config.codec) ||
        !SDL_RDP_ParseAspect(SDL_GetHint(SDL_HINT_RDP_ASPECT), &config.aspect)) {
        return false;
    }
    if (data->backend.open(&config, &data->handle) != 0) {
        return SDL_SetError("%s", data->backend.last_error());
    }
    if (!SDL_RDP_InitDisplay(data, &config) ||
        !SDL_AddHintCallback(SDL_HINT_RDP_CODEC, SDL_RDP_CodecHintChanged, data) ||
        !SDL_AddHintCallback(SDL_HINT_RDP_ASPECT, SDL_RDP_AspectHintChanged, data)) {
        return false;
    }
    SDL_AddKeyboard(SDL_DEFAULT_KEYBOARD_ID, NULL);
    SDL_AddMouse(SDL_DEFAULT_MOUSE_ID, NULL);
    SDL_RDP_InitMouse();
    return true;
}

static void SDL_RDP_VideoQuit(SDL_VideoDevice *_this)
{
    SDL_VideoData *data = _this->internal;
    SDL_RemoveHintCallback(SDL_HINT_RDP_ASPECT, SDL_RDP_AspectHintChanged, data);
    SDL_RemoveHintCallback(SDL_HINT_RDP_CODEC, SDL_RDP_CodecHintChanged, data);
    if (data->handle) {
        data->backend.close(data->handle);
        data->handle = NULL;
    }
}

static void SDL_RDP_DeleteDevice(SDL_VideoDevice *device)
{
    if (device->internal) {
        SDL_RDP_UnloadBackend(&device->internal->backend);
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
    if (!device->internal || !SDL_RDP_LoadBackend(&device->internal->backend)) {
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
