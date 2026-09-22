#ifndef SDL_RDP_BACKEND_H
#define SDL_RDP_BACKEND_H
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sdlrdp_handle sdlrdp_handle;
typedef struct { int x, y, w, h; } sdlrdp_rect;
typedef enum { SDLRDP_LOG_ERROR, SDLRDP_LOG_WARN, SDLRDP_LOG_INFO } sdlrdp_log_level;
/* Planar is lossless BitmapUpdate; RemoteFX and NSCodec use lossy SurfaceBits. */
typedef enum {
  SDLRDP_CODEC_AUTO, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX,
  SDLRDP_CODEC_NSCODEC, SDLRDP_CODEC_RAW
} sdlrdp_codec;
typedef struct {
  const char* bind; /* NULL selects 0.0.0.0; numeric IPv4. */
  unsigned port; /* 0 selects an ephemeral port. */
  const char* cert_dir; /* NULL selects _rdp under cwd. */
  unsigned width, height;
  int wait_for_client; /* Open waits for activation when nonzero. */
  /* Called on worker threads; user and callback must live until close returns. */
  void (*log)(void* user, sdlrdp_log_level level, const char* text);
  void* user;
  sdlrdp_codec codec;
} sdlrdp_config;
typedef enum {
  SDLRDP_CONNECTED, SDLRDP_DISCONNECTED, SDLRDP_RESIZE, SDLRDP_KEY,
  SDLRDP_MOUSE_MOVE, SDLRDP_MOUSE_BUTTON, SDLRDP_MOUSE_WHEEL, SDLRDP_CODEC_CHANGED
} sdlrdp_event_type;
typedef struct {
  sdlrdp_event_type type;
  union {
    struct { unsigned width, height, bpp; char client_name[64]; sdlrdp_codec codec; } connected;
    struct { sdlrdp_codec codec; } codec_changed;
    struct { unsigned width, height; } resize;
    struct { unsigned scancode; int extended; int down; } key;
    struct { int x, y; } mouse_move;
    struct { unsigned button; int down; } mouse_button;
    struct { int dx, dy; } mouse_wheel;
  };
} sdlrdp_event;
const char* sdlrdp_last_error(void);
#define SDLRDP_ABI_VERSION 2
unsigned sdlrdp_version(void);
int sdlrdp_open(const sdlrdp_config*, sdlrdp_handle**);
/* Close joins workers; callers must finish concurrent ABI calls first. */
void sdlrdp_close(sdlrdp_handle*);
unsigned sdlrdp_port(const sdlrdp_handle*);
/* XRGB8888, copied before return; no transport waits. Returns 0 on success. */
int sdlrdp_present(sdlrdp_handle*, const void*, int, unsigned, unsigned, const sdlrdp_rect*, unsigned);
unsigned sdlrdp_poll(sdlrdp_handle*, sdlrdp_event*, unsigned);
/* Negative timeout waits indefinitely; wakeup without events returns 0. */
int sdlrdp_wait(sdlrdp_handle*, int);
void sdlrdp_wakeup(sdlrdp_handle*);
/* Unsupported preferences fall back to the best negotiated codec. */
int sdlrdp_set_codec(sdlrdp_handle*, sdlrdp_codec);
#ifdef __cplusplus
}
#endif
#endif
