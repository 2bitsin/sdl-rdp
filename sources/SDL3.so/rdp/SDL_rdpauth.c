#include "SDL_rdpauth.h"

#include "SDL_rdpregistry.h"

typedef bool(SDLCALL* SDL_RDP_Verify)(void*, char const*, char const*, char const*);
typedef bool(SDLCALL* SDL_RDP_Lookup)(void*, char const*, char const*, Uint8[16]);

void SDL_RDP_AuthDisplay(SDL_PropertiesID properties) {
  struct SDL_RDP_Registry* const state = SDL_RDP_Registry();
  SDL_SetAtomicInt(&state->auth_properties, (int)properties);
}

static int SDL_RDP_VerifyCredentials(void* unused, char const* domain, char const* user, char const* password) {
  (void)unused;
  struct SDL_RDP_Registry* const state      = SDL_RDP_Registry();
  SDL_PropertiesID properties = (SDL_PropertiesID)SDL_GetAtomicInt(&state->auth_properties);
  SDL_RDP_Verify verify =
      properties ? (SDL_RDP_Verify)SDL_GetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_VERIFY_POINTER, NULL) : NULL;
  if (verify) {
    return verify(SDL_GetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_AUTH_USERDATA_POINTER, NULL), domain, user,
                  password);
  }
  return state->auth_backend->verify_pair(state->auth_config, domain, user, password);
}

static int SDL_RDP_LookupCredentials(void* unused, char const* domain, char const* user, unsigned char hash[16]) {
  (void)unused;
  struct SDL_RDP_Registry* const state      = SDL_RDP_Registry();
  SDL_PropertiesID properties = (SDL_PropertiesID)SDL_GetAtomicInt(&state->auth_properties);
  SDL_RDP_Lookup lookup =
      properties ? (SDL_RDP_Lookup)SDL_GetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_LOOKUP_POINTER, NULL) : NULL;
  if (lookup) {
    return lookup(SDL_GetPointerProperty(properties, SDL_PROP_DISPLAY_RDP_AUTH_USERDATA_POINTER, NULL), domain, user,
                  hash);
  }
  return state->auth_backend->lookup_pair(state->auth_config, domain, user, hash);
}

void SDL_RDP_AuthRelease(void) {
  struct SDL_RDP_Registry* const state = SDL_RDP_Registry();
  if (!state->auth_config) return;
  SDL_free((void*)state->auth_config->user);
  SDL_free((void*)state->auth_config->password);
  SDL_free((void*)state->auth_config->domain);
  state->auth_config->user = state->auth_config->password = state->auth_config->domain = NULL;
  state->auth_config       = NULL;
}

static bool SDL_RDP_CopyPair(sdlrdp_config* config) {
  char* user     = config->user ? SDL_strdup(config->user) : NULL;
  char* password = config->password ? SDL_strdup(config->password) : NULL;
  char* domain   = config->domain ? SDL_strdup(config->domain) : NULL;
  if ((config->user && !user) || (config->password && !password) || (config->domain && !domain)) {
    SDL_free(user);
    SDL_free(password);
    SDL_free(domain);
    return false;
  }
  config->user     = user;
  config->password = password;
  config->domain   = domain;
  return true;
}

static bool SDL_RDP_AuthMode(sdlrdp_config* config, char const* mode) {
  if (mode) {
    if (!SDL_strcmp(mode, "none"))
      config->auth = SDLRDP_AUTH_NONE;
    else if (!SDL_strcmp(mode, "tls"))
      config->auth = SDLRDP_AUTH_TLS;
    else if (!SDL_strcmp(mode, "nla"))
      config->auth = SDLRDP_AUTH_NLA;
    else
      return SDL_SetError("Invalid RDP authentication mode");
  }
  return true;
}

bool SDL_RDP_AuthConfig(sdlrdp_config* config, SDL_RDP_Backend* backend) {
  struct SDL_RDP_Registry* const state = SDL_RDP_Registry();
  char const* mode = SDL_RDP_Setting(SDL_HINT_RDP_AUTH);
  config->user     = SDL_RDP_Setting(SDL_HINT_RDP_USER);
  config->password = SDL_RDP_Setting(SDL_HINT_RDP_PASSWORD);
  config->domain   = SDL_RDP_Setting(SDL_HINT_RDP_DOMAIN);
  config->auth     = config->password ? SDLRDP_AUTH_NLA : SDLRDP_AUTH_NONE;
  if (!SDL_RDP_AuthMode(config, mode)) return false;
  if (!SDL_RDP_CopyPair(config)) return false;
  config->verify      = SDL_RDP_VerifyCredentials;
  config->lookup      = SDL_RDP_LookupCredentials;
  state->auth_config  = config;
  state->auth_backend = backend;
  return true;
}
