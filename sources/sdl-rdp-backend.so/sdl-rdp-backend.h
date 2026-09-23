#ifndef SDL_RDP_BACKEND_H
#define SDL_RDP_BACKEND_H
#include <stdint.h>
#include <stddef.h>
#ifndef __cplusplus
#include <uchar.h>
#endif
#ifdef __cplusplus
extern "C" {
#endif
typedef struct sdlrdp_handle sdlrdp_handle;
typedef struct { int x, y, w, h; } sdlrdp_rect;
typedef struct { unsigned num, den; } sdlrdp_aspect;
typedef enum { SDLRDP_LOG_ERROR, SDLRDP_LOG_WARN, SDLRDP_LOG_INFO } sdlrdp_log_level;
/* Planar is lossless BitmapUpdate; RemoteFX and NSCodec use lossy SurfaceBits. */
typedef enum {
  SDLRDP_CODEC_AUTO, SDLRDP_CODEC_PLANAR, SDLRDP_CODEC_REMOTEFX,
  SDLRDP_CODEC_NSCODEC, SDLRDP_CODEC_RAW, SDLRDP_CODEC_PROGRESSIVE, SDLRDP_CODEC_AVC420
} sdlrdp_codec;
typedef enum { SDLRDP_AUTH_NONE, SDLRDP_AUTH_TLS, SDLRDP_AUTH_NLA } sdlrdp_auth;
typedef struct {
  const char* bind; /* NULL selects 0.0.0.0; numeric IPv4. */
  unsigned port; /* 0 selects an ephemeral port. */
  const char* cert_dir; /* NULL selects the per-user data directory. */
  unsigned width, height;
  int wait_for_client; /* Open waits for activation when nonzero. */
  /* Called on worker threads; log_user and callback must live until close returns. */
  void (*log)(void* user, sdlrdp_log_level level, const char* text);
  void* log_user;
  sdlrdp_codec codec;
  sdlrdp_aspect aspect; /* Display aspect; either zero selects square pixels. */
  unsigned audio_latency_ms; /* 0 selects 500 ms ahead of confirmed playback. */
  sdlrdp_auth auth; /* Zero defaults to no authentication. */
  /* Peer worker callbacks; pointers and auth_user must live until close returns.
     Missing callbacks fail closed unless a fixed password supplies the fallback. */
  int (*verify)(void* auth_user, const char* domain, const char* user, const char* password);
  int (*lookup)(void* auth_user, const char* domain, const char* user, unsigned char nt_hash[16]);
  void* auth_user;
  const char *user, *password, *domain; /* UTF-8, copied by open; NULL means unset. */
  unsigned avc_bitrate_kbps; /* 0 selects a pixel-count-scaled default. */
} sdlrdp_config;
typedef enum {
  SDLRDP_CONNECTED, SDLRDP_DISCONNECTED, SDLRDP_RESIZE, SDLRDP_KEY,
  SDLRDP_MOUSE_MOVE, SDLRDP_MOUSE_BUTTON, SDLRDP_MOUSE_WHEEL, SDLRDP_CODEC_CHANGED, SDLRDP_SCREEN, SDLRDP_REFRESH,
  SDLRDP_CLIPBOARD, SDLRDP_TEXT, SDLRDP_MOUSE_RELATIVE, SDLRDP_TOUCH, SDLRDP_AUDIO, SDLRDP_DRIVE
} sdlrdp_event_type;
typedef enum { SDLRDP_TOUCH_DOWN, SDLRDP_TOUCH_MOVE, SDLRDP_TOUCH_UP, SDLRDP_TOUCH_CANCEL } sdlrdp_touch_phase;
typedef struct {
  sdlrdp_event_type type;
  union {
    struct { unsigned width, height, bpp; char client_name[64]; sdlrdp_codec codec;
      unsigned screen_width, screen_height, refresh_millihertz, keyboard_layout;
      char user[256], domain[256]; int authenticated; } connected;
    struct { int added; unsigned id; char name[512]; } drive;
    struct { unsigned freq; int connected; } audio;
    struct { sdlrdp_codec codec; } codec_changed;
    struct { unsigned width, height; } resize;
    struct { unsigned width, height; } screen;
    struct { unsigned millihertz; } refresh;
    struct { unsigned scancode; int extended; int down; } key;
    struct { int x, y; } mouse_move;
    struct { unsigned button; int down; } mouse_button;
    struct { float dx, dy; } mouse_wheel;
    struct { char32_t codepoint; int down; } text;
    struct { int dx, dy; } mouse_relative;
    struct { unsigned id; float x, y, pressure; sdlrdp_touch_phase phase; } touch;
  };
} sdlrdp_event;
/* Fixed-pair fallbacks for drivers; nonzero means accepted/known. */
int sdlrdp_verify_pair(const sdlrdp_config*, const char* domain, const char* user, const char* password);
int sdlrdp_lookup_pair(const sdlrdp_config*, const char* domain, const char* user, unsigned char nt_hash[16]);
const char* sdlrdp_last_error(void);
#define SDLRDP_ABI_VERSION 7
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
int sdlrdp_set_relative_mouse(sdlrdp_handle*, int enabled);
/* Unsupported preferences fall back to the best negotiated codec. */
int sdlrdp_set_codec(sdlrdp_handle*, sdlrdp_codec);
int sdlrdp_set_pointer(sdlrdp_handle*, unsigned w, unsigned h, unsigned hot_x, unsigned hot_y, const void* argb);
int sdlrdp_set_clipboard_text(sdlrdp_handle*, const char* utf8);
/* Owned by the handle until its next clipboard API call or close. */
const char* sdlrdp_get_clipboard_text(sdlrdp_handle*);
int sdlrdp_has_clipboard_text(sdlrdp_handle*);
int sdlrdp_resize(sdlrdp_handle*, unsigned, unsigned);
int sdlrdp_set_aspect(sdlrdp_handle*, sdlrdp_aspect);
/* mode: 0 fixed, 1 auto-client, 2 auto-client-average, 3 auto-sender. */
int sdlrdp_set_refresh(sdlrdp_handle*, unsigned mode, unsigned ceiling_hz);
/* 1 when all but the latest present are acknowledged or no acknowledging peer; 0 on timeout, -1 on error. Negative waits indefinitely. */
int sdlrdp_wait_frame(sdlrdp_handle*, int timeout_ms);
int sdlrdp_audio_open(sdlrdp_handle*);
unsigned sdlrdp_audio_rate(sdlrdp_handle*);
int sdlrdp_audio_write(sdlrdp_handle*, const void* frames, unsigned count);
int sdlrdp_audio_wait(sdlrdp_handle*, int timeout_ms);
void sdlrdp_audio_close(sdlrdp_handle*);
typedef struct sdlrdp_file sdlrdp_file;
typedef struct { unsigned id; char name[512]; } sdlrdp_drive;
/* modified is Unix time in seconds. */
typedef struct { uint64_t size; int directory; int64_t modified; } sdlrdp_stat;
typedef struct { char name[1024]; uint64_t size; int directory; } sdlrdp_dirent;
enum { SDLRDP_FILE_READ = 1, SDLRDP_FILE_WRITE = 2, SDLRDP_FILE_CREATE = 4,
       SDLRDP_FILE_TRUNCATE = 8, SDLRDP_FILE_DIRECTORY = 16 };
/* Blocking calls; coordinate file lifetime with close. Reads/writes accept at most INT_MAX bytes. */
int sdlrdp_drive_list(sdlrdp_handle*, sdlrdp_drive*, unsigned max);
int sdlrdp_drive_open(sdlrdp_handle*, unsigned drive, const char* path, unsigned flags, sdlrdp_file**);
int sdlrdp_drive_read(sdlrdp_handle*, sdlrdp_file*, uint64_t offset, void*, size_t);
int sdlrdp_drive_write(sdlrdp_handle*, sdlrdp_file*, uint64_t offset, const void*, size_t);
int sdlrdp_drive_stat(sdlrdp_handle*, unsigned drive, const char* path, sdlrdp_stat*);
/* Offset is the number of entries consumed; concurrent directory changes may reorder entries. */
int sdlrdp_drive_enumerate(sdlrdp_handle*, unsigned drive, const char* path, unsigned offset,
                          sdlrdp_dirent*, unsigned max);
int sdlrdp_drive_mkdir(sdlrdp_handle*, unsigned drive, const char* path);
int sdlrdp_drive_remove(sdlrdp_handle*, unsigned drive, const char* path);
int sdlrdp_drive_rename(sdlrdp_handle*, unsigned drive, const char* path, const char* destination);
int sdlrdp_drive_fstat(sdlrdp_handle*, sdlrdp_file*, sdlrdp_stat*);
int sdlrdp_drive_flush(sdlrdp_handle*, sdlrdp_file*);
/* Frees the file handle even when returning -1. */
int sdlrdp_drive_close(sdlrdp_handle*, sdlrdp_file*);
#ifdef __cplusplus
}
#endif
#endif
