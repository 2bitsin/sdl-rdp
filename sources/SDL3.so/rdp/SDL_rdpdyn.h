#ifndef SDL_rdpdyn_h_
#define SDL_rdpdyn_h_
#include "SDL_internal.h"
#include "sdl-rdp-backend.so/sdl-rdp-backend.h"
typedef struct SDL_RDP_Backend {
  SDL_SharedObject* object;
  char const*       (*last_error)        (void);
  unsigned          (*version)           (void);
  int               (*verify_pair)       (sdlrdp_config const*, char const*, char const*, char const*);
  int               (*lookup_pair)       (sdlrdp_config const*, char const*, char const*, unsigned char[16]);
  int               (*open)              (sdlrdp_config const*, sdlrdp_handle**);
  void              (*close)             (sdlrdp_handle*);
  unsigned          (*port)              (sdlrdp_handle const*);
  int (*present)(sdlrdp_handle*, void const*, int, unsigned, unsigned, sdlrdp_rect const*, unsigned);
  unsigned          (*poll)              (sdlrdp_handle*, sdlrdp_event*, unsigned);
  int               (*wait)              (sdlrdp_handle*, int);
  int               (*set_relative_mouse)(sdlrdp_handle*, int);
  int               (*set_refresh)       (sdlrdp_handle*, unsigned, unsigned);
  int               (*set_codec)         (sdlrdp_handle*, sdlrdp_codec);
  int               (*set_pointer)       (sdlrdp_handle*, unsigned, unsigned, unsigned, unsigned, void const*);
  int               (*wait_frame)        (sdlrdp_handle*, int);
  int               (*resize)            (sdlrdp_handle*, unsigned, unsigned);
  int               (*set_aspect)        (sdlrdp_handle*, sdlrdp_aspect);
  int               (*set_clipboard_text)(sdlrdp_handle*, char const*);
  char const*       (*get_clipboard_text)(sdlrdp_handle*);
  int               (*has_clipboard_text)(sdlrdp_handle*);
  int               (*audio_open)        (sdlrdp_handle*);
  unsigned          (*audio_rate)        (sdlrdp_handle*);
  int               (*audio_write)       (sdlrdp_handle*, void const*, unsigned);
  int               (*audio_wait)        (sdlrdp_handle*, int);
  void              (*audio_close)       (sdlrdp_handle*);
  int               (*drive_list)        (sdlrdp_handle*, sdlrdp_drive*, unsigned max);
  int (*drive_open)(sdlrdp_handle*, unsigned drive, char const* path, unsigned flags, sdlrdp_file**);
  int               (*drive_read)        (sdlrdp_handle*, sdlrdp_file*, uint64_t offset, void*, size_t);
  int               (*drive_write)       (sdlrdp_handle*, sdlrdp_file*, uint64_t offset, void const*, size_t);
  int               (*drive_stat)        (sdlrdp_handle*, unsigned drive, char const* path, sdlrdp_stat*);
  int (*drive_enumerate)(sdlrdp_handle*, unsigned drive, char const* path, unsigned offset, sdlrdp_dirent*,
                         unsigned max);
  int               (*drive_mkdir)       (sdlrdp_handle*, unsigned drive, char const* path);
  int               (*drive_remove)      (sdlrdp_handle*, unsigned drive, char const* path);
  int               (*drive_rename)      (sdlrdp_handle*, unsigned drive, char const* path, char const* destination);
  int               (*drive_fstat)       (sdlrdp_handle*, sdlrdp_file*, sdlrdp_stat*);
  int               (*drive_flush)       (sdlrdp_handle*, sdlrdp_file*);
  int               (*drive_close)       (sdlrdp_handle*, sdlrdp_file*);
  void              (*wakeup)            (sdlrdp_handle*);
} SDL_RDP_Backend;
char const* SDL_RDP_Setting(char const* name);
char const* SDL_RDP_HintChangedValue(char const* name, char const* oldValue, char const* newValue);
int         SDL_RDP_GetInteger(char const* name, int fallback);
bool        SDL_RDP_SettingBoolean(char const* name, bool fallback);
bool        SDL_RDP_SettingsReady(void);
bool        SDL_RDP_LoadBackend(SDL_RDP_Backend* backend);
void        SDL_RDP_UnloadBackend(SDL_RDP_Backend* backend);
bool        SDL_RDP_AcquireBackend(SDL_RDP_Backend* backend, sdlrdp_handle** handle, sdlrdp_config* config);
void        SDL_RDP_ReleaseBackend(void);
void        SDL_RDP_AudioRate(unsigned rate);
bool        SDL_RDP_ParseCodec(char const* name, sdlrdp_codec* codec);
bool        SDL_RDP_ParseAspect(char const* value, sdlrdp_aspect* aspect);
#endif
