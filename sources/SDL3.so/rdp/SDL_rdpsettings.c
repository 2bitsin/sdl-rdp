#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include "SDL_rdpdyn.h"
#include "SDL_rdpini.h"
#include "src/SDL_hints_c.h"
#if defined(SDL_PLATFORM_WINDOWS)
#include "src/core/windows/SDL_windows.h"
#elif defined(SDL_PLATFORM_LINUX) || defined(SDL_PLATFORM_MACOS)
#include <dlfcn.h>
#endif

static SDL_InitState ini_init;
static const char *ini_values[SDL_RDP_SETTING_COUNT];
static char *ini_text;
static bool ini_failed;
static char *ini_failed_path;

static char *SDL_RDP_LibraryIni(void)
{
    char *path = NULL, *slash;
#if defined(SDL_PLATFORM_WINDOWS)
    HMODULE module;
    WCHAR *filename;
    DWORD length;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           (LPCWSTR)SDL_RDP_LibraryIni, &module)) return NULL;
    filename = SDL_malloc(32768 * sizeof(*filename));
    if (!filename) return NULL;
    length = GetModuleFileNameW(module, filename, 32768);
    if (length && length < 32768) path = WIN_StringToUTF8W(filename);
    SDL_free(filename);
#elif defined(SDL_PLATFORM_LINUX) || defined(SDL_PLATFORM_MACOS)
    Dl_info info;
    if (dladdr((void *)SDL_RDP_LibraryIni, &info) && info.dli_fname) path = SDL_strdup(info.dli_fname);
#endif
    if (!path) return NULL;
    slash = SDL_strrchr(path, '/');
#if defined(SDL_PLATFORM_WINDOWS)
    {
        char *backslash = SDL_strrchr(path, '\\');
        if (backslash && (!slash || backslash > slash)) slash = backslash;
    }
#endif
    if (slash) {
        char *result = NULL;
        slash[1] = '\0';
        SDL_asprintf(&result, "%slibSDL3.ini", path);
        SDL_free(path);
        return result;
    }
    SDL_free(path);
    return NULL;
}

static void SDL_RDP_IniEntry(void *user, int index, const char *key, const char *value, unsigned line)
{
    const char *path = user;
    if (index == -2) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "%s:%u: malformed ini line (missing '=')", path, line);
    } else if (index < 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_VIDEO, "%s:%u: unknown RDP setting '%s'", path, line, key);
    } else {
        ini_values[index] = value;
    }
}

static bool SDL_RDP_ReadIni(const char *path, bool required)
{
    SDL_PathInfo info;
    ini_text = SDL_LoadFile(path, NULL);
    if (ini_text) {
        SDL_RDP_IniParse(ini_text, SDL_RDP_IniEntry, (void *)path);
        return true;
    }
    ini_failed = required || SDL_GetPathInfo(path, &info);
    if (ini_failed) ini_failed_path = SDL_strdup(path);
    SDL_ClearError();
    return ini_failed;
}

static void SDL_RDP_LoadIni(void)
{
    const char *explicit_path = SDL_GetHintFromCode(SDL_HINT_RDP_INI);
    char *library_path;
    bool found;
    if (!explicit_path) explicit_path = SDL_getenv(SDL_HINT_RDP_INI);
    if (explicit_path) {
        SDL_RDP_ReadIni(explicit_path, true);
        return;
    }
    library_path = SDL_RDP_LibraryIni();
    found = library_path && SDL_RDP_ReadIni(library_path, false);
    SDL_free(library_path);
    if (!found) SDL_RDP_ReadIni("libSDL3.ini", false);
}

bool SDL_RDP_SettingsReady(void)
{
    if (SDL_ShouldInit(&ini_init)) {
        SDL_RDP_LoadIni();
        SDL_SetInitialized(&ini_init, true);
    }
    return !ini_failed || SDL_SetError("Could not read RDP settings file %s", ini_failed_path ? ini_failed_path : "");
}

static const char *SDL_RDP_SettingFallback(const char *name)
{
    int index = SDL_RDP_IniIndex(name);
    if (index >= 0 && index != SDL_RDP_SETTING_INI && ini_values[index]) return ini_values[index];
    return SDL_getenv(name);
}

const char *SDL_RDP_Setting(const char *name)
{
    const char *value;
    if (!SDL_RDP_SettingsReady()) return NULL;
    value = SDL_GetHintFromCode(name);
    return value ? value : SDL_RDP_SettingFallback(name);
}

const char *SDL_RDP_HintChangedValue(const char *name, const char *oldValue, const char *newValue)
{
    const char *code = SDL_GetHintFromCode(name);
    /* SDL_ResetHint invokes callbacks before removing the old code value. */
    if (oldValue != newValue && code && (!newValue || SDL_strcmp(code, newValue) != 0)) return SDL_RDP_SettingFallback(name);
    return SDL_RDP_Setting(name);
}

bool SDL_RDP_SettingBoolean(const char *name, bool fallback)
{
    return SDL_GetStringBoolean(SDL_RDP_Setting(name), fallback);
}
