#ifndef SDL_rdpdyn_h_
#define SDL_rdpdyn_h_
#include "SDL_internal.h"
#include "sdl-rdp-backend.so/sdl-rdp-backend.h"
typedef struct SDL_RDP_Backend
{
    SDL_SharedObject *object;
    const char *(*last_error)(void);
    unsigned (*version)(void);
    int (*verify_pair)(const sdlrdp_config *, const char *, const char *, const char *);
    int (*lookup_pair)(const sdlrdp_config *, const char *, const char *, unsigned char[16]);
    int (*open)(const sdlrdp_config *, sdlrdp_handle **);
    void (*close)(sdlrdp_handle *);
    unsigned (*port)(const sdlrdp_handle *);
    int (*present)(sdlrdp_handle *, const void *, int, unsigned, unsigned, const sdlrdp_rect *, unsigned);
    unsigned (*poll)(sdlrdp_handle *, sdlrdp_event *, unsigned);
    int (*wait)(sdlrdp_handle *, int);
    int (*set_relative_mouse)(sdlrdp_handle *, int);
    int (*set_refresh)(sdlrdp_handle *, unsigned, unsigned);
    int (*set_codec)(sdlrdp_handle *, sdlrdp_codec);
    int (*set_pointer)(sdlrdp_handle *, unsigned, unsigned, unsigned, unsigned, const void *);
    int (*wait_frame)(sdlrdp_handle *, int);
    int (*resize)(sdlrdp_handle *, unsigned, unsigned);
    int (*set_aspect)(sdlrdp_handle *, sdlrdp_aspect);
    int (*set_clipboard_text)(sdlrdp_handle *, const char *);
    const char *(*get_clipboard_text)(sdlrdp_handle *);
    int (*has_clipboard_text)(sdlrdp_handle *);
    int (*audio_open)(sdlrdp_handle *);
    unsigned (*audio_rate)(sdlrdp_handle *);
    int (*audio_write)(sdlrdp_handle *, const void *, unsigned);
    int (*audio_wait)(sdlrdp_handle *, int);
    void (*audio_close)(sdlrdp_handle *);
    int (*drive_list)(sdlrdp_handle*, sdlrdp_drive*, unsigned max);
    int (*drive_open)(sdlrdp_handle*, unsigned drive, const char* path, unsigned flags, sdlrdp_file**);
    int (*drive_read)(sdlrdp_handle*, sdlrdp_file*, uint64_t offset, void*, size_t);
    int (*drive_write)(sdlrdp_handle*, sdlrdp_file*, uint64_t offset, const void*, size_t);
    int (*drive_stat)(sdlrdp_handle*, unsigned drive, const char* path, sdlrdp_stat*);
    int (*drive_enumerate)(sdlrdp_handle*, unsigned drive, const char* path, unsigned offset,
                          sdlrdp_dirent*, unsigned max);
    int (*drive_mkdir)(sdlrdp_handle*, unsigned drive, const char* path);
    int (*drive_remove)(sdlrdp_handle*, unsigned drive, const char* path);
    int (*drive_rename)(sdlrdp_handle*, unsigned drive, const char* path, const char* destination);
    int (*drive_fstat)(sdlrdp_handle*, sdlrdp_file*, sdlrdp_stat*);
    int (*drive_flush)(sdlrdp_handle*, sdlrdp_file*);
    int (*drive_close)(sdlrdp_handle*, sdlrdp_file*);
    void (*wakeup)(sdlrdp_handle *);
} SDL_RDP_Backend;
const char *SDL_RDP_Setting(const char *name);
const char *SDL_RDP_HintChangedValue(const char *name, const char *oldValue, const char *newValue);
int SDL_RDP_GetInteger(const char *name, int fallback);
bool SDL_RDP_SettingBoolean(const char *name, bool fallback);
bool SDL_RDP_SettingsReady(void);
bool SDL_RDP_LoadBackend(SDL_RDP_Backend *backend);
void SDL_RDP_UnloadBackend(SDL_RDP_Backend *backend);
bool SDL_RDP_AcquireBackend(SDL_RDP_Backend *backend, sdlrdp_handle **handle, sdlrdp_config *config);
void SDL_RDP_ReleaseBackend(void);
void SDL_RDP_AudioRate(unsigned rate);
bool SDL_RDP_ParseCodec(const char *name, sdlrdp_codec *codec);
bool SDL_RDP_ParseAspect(const char *value, sdlrdp_aspect *aspect);
#endif
