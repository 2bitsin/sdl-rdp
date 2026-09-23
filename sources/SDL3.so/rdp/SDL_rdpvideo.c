#include "SDL_rdpvideo.h"

#include "SDL_rdpauth.h"
#include "SDL_rdpclipboard.h"
#include "SDL_rdpdrive.h"
#include "SDL_rdpevents.h"
#include "SDL_rdpframebuffer.h"
#include "SDL_rdpwindow.h"
#include "src/events/SDL_keyboard_c.h"
#include "src/events/SDL_mouse_c.h"

static void SDLCALL SDL_RDP_CodecHintChanged(void* userdata, char const* name, char const* oldValue,
                                             char const* newValue) {
  SDL_VideoData* data  = userdata;
  sdlrdp_codec   codec = SDLRDP_CODEC_AUTO;
  newValue = SDL_RDP_HintChangedValue(name, oldValue, newValue);
  if (data->handle && SDL_RDP_ParseCodec(newValue, &codec) && data->backend.set_codec(data->handle, codec) != 0) {
    SDL_SetError("%s", data->backend.last_error());
  }
}

static int SDL_RDP_ConfigureRefresh(SDL_VideoData* data) {
  SDL_assert(data);
  char const* value = SDL_RDP_Setting(SDL_HINT_RDP_REFRESH);
  unsigned    kind  = 0;
  int         hz    = 60;
  if (value && SDL_strcmp(value, "auto-client") == 0)
    kind = 1;
  else if (value && SDL_strcmp(value, "auto-client-average") == 0)
    kind = 2;
  else if (value && SDL_strcmp(value, "auto-sender") == 0)
    kind = 3;
  else
    hz = SDL_RDP_GetInteger(SDL_HINT_RDP_REFRESH, 60);
  if (hz <= 0 || hz > SDL_MAX_SINT32 / 1000) {
    SDL_SetError("SDL_RDP_REFRESH must be positive integer Hz representable in millihertz, auto-client, "
                 "auto-client-average or auto-sender");
    SDL_LogError(SDL_LOG_CATEGORY_VIDEO, "%s", SDL_GetError());
    return 0;
  }
  if (data->backend.set_refresh(data->handle, kind, hz) != 0) return 0;
  return hz;
}

static bool SDL_RDP_InitDisplay(SDL_VideoData* data, sdlrdp_config const* config) {
  SDL_assert(data);
  SDL_assert(config);
  SDL_DisplayMode mode = { 0 };
  mode.format                 = SDL_PIXELFORMAT_XRGB8888;
  mode.w                      = (int)config->width;
  mode.h                      = (int)config->height;
  mode.refresh_rate_numerator = SDL_RDP_ConfigureRefresh(data);
  if (!mode.refresh_rate_numerator) return false;
  mode.refresh_rate             = (float)mode.refresh_rate_numerator;
  mode.refresh_rate_denominator = 1;
  data->display                 = SDL_AddBasicVideoDisplay(&mode);
  if (data->display) SDL_RDP_AuthDisplay(SDL_GetDisplayProperties(data->display));
  if (data->display) SDL_RDP_UpdateDrives(&data->backend, data->handle, SDL_GetDisplayProperties(data->display));
  return data->display && SDL_SetNumberProperty(SDL_GetDisplayProperties(data->display),
                                                SDL_PROP_DISPLAY_RDP_PORT_NUMBER, data->backend.port(data->handle));
}

static int const SDL_RDP_EmulatorModes[][2] = { { 320, 200 },   { 320, 240 },  { 320, 256 },   { 400, 300 },
                                                { 512, 384 },   { 640, 350 },  { 640, 400 },   { 640, 480 },
                                                { 720, 400 },   { 720, 480 },  { 800, 600 },   { 1024, 768 },
                                                { 1280, 720 },  { 1280, 800 }, { 1920, 1080 }, { 1920, 1200 },
                                                { 2560, 1440 }, { 3840, 2160 } };

static bool SDL_RDP_GetDisplayModes(SDL_VideoDevice* _this, SDL_VideoDisplay* display) {
  (void)_this;
  SDL_DisplayMode mode = display->desktop_mode;
  SDL_AddFullscreenDisplayMode(display, &mode);
  for (unsigned i = 0; i < SDL_arraysize(SDL_RDP_EmulatorModes); ++i) {
    mode.w = SDL_RDP_EmulatorModes[i][0];
    mode.h = SDL_RDP_EmulatorModes[i][1];
    SDL_AddFullscreenDisplayMode(display, &mode);
  }
  return true;
}

static bool SDL_RDP_SetDisplayMode(SDL_VideoDevice* _this, SDL_VideoDisplay* display, SDL_DisplayMode* mode) {
  SDL_assert(_this);
  SDL_assert(mode);
  (void)display;
  SDL_VideoData* data = _this->internal;
  // SDL_SetDisplayModeForDisplay sets current_mode on success before the client reactivates.
  return SDL_RDP_ResizePicture(data, mode->w, mode->h);
}

static bool SDL_RDP_RelativeMouse(bool enabled) {
  SDL_VideoData* data = SDL_GetVideoDevice()->internal;
  return data->backend.set_relative_mouse(data->handle, enabled) == 0 || SDL_SetError("%s", data->backend.last_error());
}

static bool SDL_RDP_VideoInit(SDL_VideoDevice* _this) {
  SDL_VideoData* data   = _this->internal;
  sdlrdp_config  config;
  if (!SDL_RDP_AcquireBackend(&data->backend, &data->handle, &config)) {
    return false;
  }
  if (!SDL_RDP_InitDisplay(data, &config) || !SDL_AddHintCallback(SDL_HINT_RDP_CODEC, SDL_RDP_CodecHintChanged, data) ||
      !SDL_AddHintCallback(SDL_HINT_RDP_ASPECT, SDL_RDP_AspectHintChanged, data)) {
    return false;
  }
  SDL_AddKeyboard(SDL_DEFAULT_KEYBOARD_ID, NULL);
  SDL_AddMouse(SDL_DEFAULT_MOUSE_ID, NULL);
  SDL_GetMouse()->SetRelativeMouseMode = SDL_RDP_RelativeMouse;
  SDL_RDP_InitMouse();
  return true;
}

static void SDL_RDP_VideoQuit(SDL_VideoDevice* _this) {
  SDL_VideoData* data = _this->internal;
  SDL_RDP_AuthDisplay(0);
  SDL_RemoveHintCallback(SDL_HINT_RDP_ASPECT, SDL_RDP_AspectHintChanged, data);
  SDL_RemoveHintCallback(SDL_HINT_RDP_CODEC, SDL_RDP_CodecHintChanged, data);
  if (data->handle) {
    SDL_RDP_ReleaseBackend();
    data->handle = NULL;
  }
}

static void SDL_RDP_DeleteDevice(SDL_VideoDevice* device) {
  if (device->internal) {
    SDL_free(device->internal);
  }
  SDL_free(device);
}

static void SDL_RDP_InitWindowCallbacks(SDL_VideoDevice* device) {
  device->CreateSDLWindow          = SDL_RDP_CreateWindow;
  device->DestroyWindow            = SDL_RDP_DestroyWindow;
  device->SetWindowFullscreen      = SDL_RDP_SetWindowFullscreen;
  device->SetWindowSize            = SDL_RDP_SetWindowSize;
  device->ShowWindow               = SDL_RDP_ShowWindow;
  device->CreateWindowFramebuffer  = SDL_RDP_CreateWindowFramebuffer;
  device->UpdateWindowFramebuffer  = SDL_RDP_UpdateWindowFramebuffer;
  device->DestroyWindowFramebuffer = SDL_RDP_DestroyWindowFramebuffer;
}

static void SDL_RDP_InitDevice(SDL_VideoDevice* device) {
  device->is_dummy         = true;
  device->VideoInit        = SDL_RDP_VideoInit;
  device->VideoQuit        = SDL_RDP_VideoQuit;
  device->GetDisplayModes  = SDL_RDP_GetDisplayModes;
  device->SetDisplayMode   = SDL_RDP_SetDisplayMode;
  device->PumpEvents       = SDL_RDP_PumpEvents;
  device->WaitEventTimeout = SDL_RDP_WaitEventTimeout;
  device->SendWakeupEvent  = SDL_RDP_SendWakeupEvent;
  SDL_RDP_InitWindowCallbacks(device);
  SDL_RDP_InitClipboard(device);
  device->free = SDL_RDP_DeleteDevice;
}

static SDL_VideoDevice* SDL_RDP_CreateDevice(void) {
  SDL_VideoDevice* device = NULL;
  char const*      hint   = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
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
  SDL_RDP_InitDevice(device);
  return device;
}

VideoBootStrap RDP_bootstrap = {
  "rdp", "SDL RDP video driver", SDL_RDP_CreateDevice, NULL, false
};
