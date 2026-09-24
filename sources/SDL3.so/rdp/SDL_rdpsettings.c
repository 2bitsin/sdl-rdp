#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "SDL_rdpdyn.h"
#include "SDL_rdpini.h"
#include "SDL_rdpregistry.h"
#include "src/SDL_hints_c.h"
#if defined(SDL_PLATFORM_WINDOWS)
#include "src/core/windows/SDL_windows.h"
#elif defined(SDL_PLATFORM_LINUX) || defined(SDL_PLATFORM_MACOS)
#include <dlfcn.h>
#endif

static char* SDL_RDP_LibraryPath(void) {
  char* path = NULL;
#if defined(SDL_PLATFORM_WINDOWS)
  HMODULE module;
  WCHAR*  filename;
  DWORD   length;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          (LPCWSTR)SDL_RDP_LibraryPath, &module))
    return NULL;
  filename = SDL_malloc(32768 * sizeof(*filename));
  if (!filename) return NULL;
  length = GetModuleFileNameW(module, filename, 32768);
  if (length && length < 32768) path = WIN_StringToUTF8W(filename);
  SDL_free(filename);
#elif defined(SDL_PLATFORM_LINUX) || defined(SDL_PLATFORM_MACOS)
  Dl_info info;
  if (dladdr((void*)SDL_RDP_LibraryPath, &info) && info.dli_fname) path = SDL_strdup(info.dli_fname);
#endif
  return path;
}

static char* SDL_RDP_LibraryIni(void) {
  char* path   = SDL_RDP_LibraryPath();
  char* slash  = NULL;
  char* result = NULL;
  if (!path) return NULL;
  slash = SDL_strrchr(path, '/');
#if defined(SDL_PLATFORM_WINDOWS)
  {
    char* backslash = SDL_strrchr(path, '\\');
    if (backslash && (!slash || backslash > slash)) slash = backslash;
  }
#endif
  if (slash) {
    slash[1] = '\0';
    SDL_asprintf(&result, "%slibSDL3.ini", path);
  }
  SDL_free(path);
  return result;
}

static void SDL_RDP_IniEntry(void* user, int index, char const* key, char const* value, unsigned line) {
  struct SDL_RDP_Registry* const state = SDL_RDP_Registry();
  char const*                    path  = user;
  if (index == -2) {
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "%s:%u: malformed ini line (missing '=')", path, line);
  } else if (index < 0) {
    SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "%s:%u: unknown RDP setting '%s'", path, line, key);
  } else {
    state->ini_values[index] = value;
  }
}

static bool SDL_RDP_ReadIni(char const* path, bool required) {
  struct SDL_RDP_Registry* const state = SDL_RDP_Registry();
  SDL_PathInfo                   info;
  state->ini_text = SDL_LoadFile(path, NULL);
  if (state->ini_text) {
    SDL_RDP_IniParse(state->ini_text, SDL_RDP_IniEntry, (void*)path);
    return true;
  }
  state->ini_failed = required || SDL_GetPathInfo(path, &info);
  if (state->ini_failed) state->ini_failed_path = SDL_strdup(path);
  SDL_ClearError();
  return state->ini_failed;
}

static void SDL_RDP_LoadIni(void) {
  char const* explicit_path = SDL_GetHintFromCode(SDL_HINT_RDP_INI);
  char*       library_path  = NULL;
  bool        found         = false;
  if (!explicit_path) explicit_path = SDL_getenv(SDL_HINT_RDP_INI);
  if (explicit_path) {
    SDL_RDP_ReadIni(explicit_path, true);
    return;
  }
  library_path = SDL_RDP_LibraryIni();
  found        = library_path && SDL_RDP_ReadIni(library_path, false);
  SDL_free(library_path);
  if (!found) SDL_RDP_ReadIni("libSDL3.ini", false);
}

bool SDL_RDP_SettingsReady(void) {
  struct SDL_RDP_Registry* const state = SDL_RDP_Registry();
  if (SDL_ShouldInit(&state->ini_init)) {
    SDL_RDP_LoadIni();
    SDL_SetInitialized(&state->ini_init, true);
  }
  return !state->ini_failed ||
         SDL_SetError("Could not read RDP settings file %s", state->ini_failed_path ? state->ini_failed_path : "");
}

static char const* SDL_RDP_SettingFallback(char const* name) {
  struct SDL_RDP_Registry* const state = SDL_RDP_Registry();
  int                            index = SDL_RDP_IniIndex(name);
  if (index >= 0 && index != SDL_RDP_SETTING_INI && state->ini_values[index]) return state->ini_values[index];
  return SDL_getenv(name);
}

char const* SDL_RDP_Setting(char const* name) {
  char const* value = NULL;
  if (!SDL_RDP_SettingsReady()) return NULL;
  value = SDL_GetHintFromCode(name);
  return value ? value : SDL_RDP_SettingFallback(name);
}

char const* SDL_RDP_HintChangedValue(char const* name, char const* oldValue, char const* newValue) {
  char const* code = SDL_GetHintFromCode(name);
  /* SDL_ResetHint invokes callbacks before removing the old code value. */
  if (oldValue != newValue && code && (!newValue || SDL_strcmp(code, newValue) != 0))
    return SDL_RDP_SettingFallback(name);
  return SDL_RDP_Setting(name);
}

bool SDL_RDP_SettingBoolean(char const* name, bool fallback) {
  return SDL_GetStringBoolean(SDL_RDP_Setting(name), fallback);
}
