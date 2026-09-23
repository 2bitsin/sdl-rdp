#include "SDL_rdpini.h"
#include <SDL3/SDL_stdinc.h>

#define SDL_RDP_SETTING_STRING(name) "SDL_RDP_" #name,
const char *const SDL_RDP_SettingNames[SDL_RDP_SETTING_COUNT] = {
    SDL_RDP_SETTING_NAMES(SDL_RDP_SETTING_STRING)
};
#undef SDL_RDP_SETTING_STRING

int SDL_RDP_IniIndex(const char *name)
{
    int i;
    for (i = 0; i < SDL_RDP_SETTING_COUNT; ++i) {
        if (SDL_strcmp(name, SDL_RDP_SettingNames[i]) == 0) return i;
    }
    return -1;
}

static char *SDL_RDP_IniTrim(char *start, char *end)
{
    while (start < end && SDL_isspace((unsigned char)*start)) ++start;
    while (end > start && SDL_isspace((unsigned char)end[-1])) --end;
    *end = '\0';
    return start;
}

static void SDL_RDP_IniLine(char *start, char *end, unsigned line, SDL_RDP_IniCallback callback, void *user)
{
    char *key = SDL_RDP_IniTrim(start, end), *value, *equals;
    if (!*key || *key == '#' || *key == ';') return;
    end = key + SDL_strlen(key);
    if (*key == '[' && end[-1] == ']') return;
    equals = SDL_strchr(key, '=');
    if (!equals) {
        callback(user, -2, "", "", line);
        return;
    }
    value = SDL_RDP_IniTrim(equals + 1, end);
    key = SDL_RDP_IniTrim(key, equals);
    end = value + SDL_strlen(value);
    if (end - value >= 2 && *value == '"' && end[-1] == '"') {
        ++value;
        end[-1] = '\0';
    }
    callback(user, SDL_RDP_IniIndex(key), key, value, line);
}

void SDL_RDP_IniParse(char *text, SDL_RDP_IniCallback callback, void *user)
{
    unsigned line = 1;
    while (*text) {
        char *end = SDL_strchr(text, '\n'), *next;
        if (!end) end = text + SDL_strlen(text);
        next = *end ? end + 1 : end;
        SDL_RDP_IniLine(text, end, line++, callback, user);
        text = next;
    }
}
