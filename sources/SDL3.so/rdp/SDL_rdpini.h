#ifndef SDL_rdpini_h_
#define SDL_rdpini_h_

// NOLINTNEXTLINE(cppcoreguidelines-macro-usage): The C driver shares this X-macro table with its C++ tests.
#define SDL_RDP_SETTING_NAMES(X)                                                                                       \
  X(INI)                                                                                                               \
  X(BACKEND)                                                                                                           \
  X(BIND)                                                                                                              \
  X(CERT_DIR)                                                                                                          \
  X(CODEC)                                                                                                             \
  X(AUDIO_LATENCY)                                                                                                     \
  X(AUDIO_LEAD)                                                                                                        \
  X(VSYNC)                                                                                                             \
  X(ASPECT)                                                                                                            \
  X(HEIGHT)                                                                                                            \
  X(PORT)                                                                                                              \
  X(WAIT_FOR_CLIENT)                                                                                                   \
  X(WIDTH)                                                                                                             \
  X(REFRESH)                                                                                                           \
  X(USER)                                                                                                              \
  X(PASSWORD)                                                                                                          \
  X(DOMAIN) X(AUTH)

#define SDL_RDP_SETTING_ENUM(name) SDL_RDP_SETTING_##name,
enum { SDL_RDP_SETTING_NAMES(SDL_RDP_SETTING_ENUM) SDL_RDP_SETTING_COUNT };
#undef SDL_RDP_SETTING_ENUM

extern char const* const SDL_RDP_SettingNames[SDL_RDP_SETTING_COUNT];
typedef void (*SDL_RDP_IniCallback)(
    void* user, int index, char const* key, char const* value,
    unsigned line); // NOLINT(modernize-use-using): This callback declaration must also compile as C.
/* Splits text in place; callback strings live in text. -1: unknown key, -2: malformed. */
void SDL_RDP_IniParse(char* text, SDL_RDP_IniCallback callback, void* user);
int  SDL_RDP_IniIndex(char const* name);
#endif
