#include "SDL_rdpauth.h"
#include "SDL_rdpdyn.h"
#include "SDL_rdpregistry.h"

static struct SDL_RDP_Registry
    registry;

struct SDL_RDP_Registry* SDL_RDP_Registry(void) {
  return &registry;
}

int SDL_RDP_GetInteger(char const* name, int fallback) {
  char const* hint  = SDL_RDP_Setting(name);
  char*       end   = NULL;
  long        value = 0;
  if (!hint) {
    return fallback;
  }
  value = SDL_strtol(hint, &end, 10);
  return !*hint || *end || value < 0 || value > SDL_MAX_SINT32 ? -1 : (int)value;
}

static void SDL_RDP_Log(void* user, sdlrdp_log_level level, char const* text) {
  (void)user;
  static SDL_LogPriority const priorities[] = { SDL_LOG_PRIORITY_ERROR, SDL_LOG_PRIORITY_WARN, SDL_LOG_PRIORITY_INFO };
  SDL_assert((unsigned)level < SDL_arraysize(priorities));
  SDL_LogMessage(SDL_LOG_CATEGORY_VIDEO, priorities[level], "%s", text);
}

bool SDL_RDP_ParseAspect(char const* value, sdlrdp_aspect* aspect) {
  char*         end = NULL;
  unsigned long num = 0;
  unsigned long den = 0;
  *aspect = (sdlrdp_aspect){ 0 };
  if (!value || !*value) return true;
  num = SDL_strtoul(value, &end, 10);
  if (end == value || *end != ':' || !num || num > SDL_MAX_UINT32) return SDL_SetError("Invalid RDP aspect: %s", value);
  value = end + 1;
  den   = SDL_strtoul(value, &end, 10);
  if (end == value || *end || !den || den > SDL_MAX_UINT32)
    return SDL_SetError("Invalid RDP aspect denominator: %s", value);
  aspect->num = (unsigned)num;
  aspect->den = (unsigned)den;
  return true;
}

static void SDL_RDP_ConfigSettings(sdlrdp_config* config) {
  config->log              = SDL_RDP_Log;
  config->log_user         = NULL;
  config->bind             = SDL_RDP_Setting(SDL_HINT_RDP_BIND);
  config->cert_dir         = SDL_RDP_Setting(SDL_HINT_RDP_CERT_DIR);
  config->port             = SDL_RDP_GetInteger(SDL_HINT_RDP_PORT, 3389);
  config->width            = SDL_RDP_GetInteger(SDL_HINT_RDP_WIDTH, 1024);
  config->height           = SDL_RDP_GetInteger(SDL_HINT_RDP_HEIGHT, 768);
  config->audio_latency_ms = SDL_RDP_GetInteger(SDL_HINT_RDP_AUDIO_LATENCY, 500);
  config->avc_bitrate_kbps = SDL_RDP_GetInteger(SDL_HINT_RDP_AVC_BITRATE, 0);
}

static bool SDL_RDP_Config(sdlrdp_config* config) {
  struct SDL_RDP_Registry* const state = SDL_RDP_Registry();
  if (!SDL_RDP_SettingsReady()) return false;
  *config = (sdlrdp_config){ 0 };
  SDL_RDP_ConfigSettings(config);
  if (config->avc_bitrate_kbps > SDL_MAX_UINT32 / 1000) return SDL_SetError("Invalid RDP AVC bitrate");
  config->wait_for_client = SDL_RDP_SettingBoolean(SDL_HINT_RDP_WAIT_FOR_CLIENT, false);
  if (config->audio_latency_ms > SDL_MAX_SINT32 || config->port > 65535 || !config->width ||
      config->width > SDL_MAX_SINT32 || !config->height || config->height > SDL_MAX_SINT32) {
    return SDL_SetError("Invalid RDP port or dimensions");
  }
  if (!SDL_RDP_ParseCodec(SDL_RDP_Setting(SDL_HINT_RDP_CODEC), &config->codec) ||
      !SDL_RDP_ParseAspect(SDL_RDP_Setting(SDL_HINT_RDP_ASPECT), &config->aspect)) {
    return false;
  }
  return SDL_RDP_AuthConfig(config, &state->shared_backend);
}

static bool SDL_RDP_OpenShared(struct SDL_RDP_Registry* state) {
  bool ok = SDL_RDP_Config(&state->shared_config) && SDL_RDP_LoadBackend(&state->shared_backend);
  if (ok && state->shared_backend.open(&state->shared_config, &state->shared_handle) != 0) {
    SDL_SetError("%s", state->shared_backend.last_error());
    ok = false;
  }
  if (!ok) {
    SDL_RDP_UnloadBackend(&state->shared_backend);
    SDL_RDP_AuthRelease();
  }
  return ok;
}

bool SDL_RDP_AcquireBackend(SDL_RDP_Backend* backend, sdlrdp_handle** handle, sdlrdp_config* config) {
  struct SDL_RDP_Registry* const state = SDL_RDP_Registry();
  bool                           ok    = true;
  if (SDL_ShouldInit(&state->shared_init)) {
    state->shared_lock = SDL_CreateMutex();
    SDL_SetInitialized(&state->shared_init, state->shared_lock != NULL);
  }
  if (!state->shared_lock) return false;
  SDL_LockMutex(state->shared_lock);
  if (!state->shared_refs) ok = SDL_RDP_OpenShared(state);
  if (ok) {
    ++state->shared_refs;
    *backend = state->shared_backend;
    *handle  = state->shared_handle;
    if (config) *config = state->shared_config;
  }
  SDL_UnlockMutex(state->shared_lock);
  return ok;
}

void SDL_RDP_ReleaseBackend(void) {
  struct SDL_RDP_Registry* const state = SDL_RDP_Registry();
  SDL_LockMutex(state->shared_lock);
  SDL_assert(state->shared_refs > 0);
  if (!--state->shared_refs) {
    state->shared_backend.close(state->shared_handle);
    state->shared_handle = NULL;
    SDL_RDP_AuthRelease();
    SDL_RDP_UnloadBackend(&state->shared_backend);
  }
  SDL_UnlockMutex(state->shared_lock);
}
