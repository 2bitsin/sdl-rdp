#include "SDL_rdpdyn.h"
#include "SDL_rdpauth.h"

static SDL_InitState shared_init;
static SDL_Mutex *shared_lock;
static unsigned shared_refs;
static SDL_RDP_Backend shared_backend;
static sdlrdp_handle *shared_handle;
static sdlrdp_config shared_config;

int SDL_RDP_GetInteger(const char *name, int fallback)
{
    const char *hint = SDL_RDP_Setting(name);
    char *end;
    long value;
    if (!hint) {
        return fallback;
    }
    value = SDL_strtol(hint, &end, 10);
    return !*hint || *end || value < 0 || value > SDL_MAX_SINT32 ? -1 : (int)value;
}

static void SDL_RDP_Log(void *user, sdlrdp_log_level level, const char *text)
{
    static const SDL_LogPriority priorities[] = {
        SDL_LOG_PRIORITY_ERROR, SDL_LOG_PRIORITY_WARN, SDL_LOG_PRIORITY_INFO
    };
    SDL_assert((unsigned)level < SDL_arraysize(priorities));
    SDL_LogMessage(SDL_LOG_CATEGORY_VIDEO, priorities[level], "%s", text);
}

bool SDL_RDP_ParseAspect(const char *value, sdlrdp_aspect *aspect)
{
    char *end;
    unsigned long num, den;
    SDL_zero(*aspect);
    if (!value || !*value) return true;
    num = SDL_strtoul(value, &end, 10);
    if (end == value || *end != ':' || !num || num > SDL_MAX_UINT32)
        return SDL_SetError("Invalid RDP aspect: %s", value);
    value = end + 1;
    den = SDL_strtoul(value, &end, 10);
    if (end == value || *end || !den || den > SDL_MAX_UINT32)
        return SDL_SetError("Invalid RDP aspect denominator: %s", value);
    aspect->num = (unsigned)num;
    aspect->den = (unsigned)den;
    return true;
}

static bool SDL_RDP_Config(sdlrdp_config *config)
{
    if (!SDL_RDP_SettingsReady()) return false;
    SDL_zero(*config);
    config->log = SDL_RDP_Log;
    config->log_user = NULL;
    config->bind = SDL_RDP_Setting(SDL_HINT_RDP_BIND);
    config->cert_dir = SDL_RDP_Setting(SDL_HINT_RDP_CERT_DIR);
    config->port = SDL_RDP_GetInteger(SDL_HINT_RDP_PORT, 3389);
    config->width = SDL_RDP_GetInteger(SDL_HINT_RDP_WIDTH, 1024);
    config->height = SDL_RDP_GetInteger(SDL_HINT_RDP_HEIGHT, 768);
    config->audio_latency_ms = SDL_RDP_GetInteger(SDL_HINT_RDP_AUDIO_LATENCY, 500);
    config->avc_bitrate_kbps = SDL_RDP_GetInteger(SDL_HINT_RDP_AVC_BITRATE, 0);
    if (config->avc_bitrate_kbps > SDL_MAX_UINT32 / 1000)
        return SDL_SetError("Invalid RDP AVC bitrate");
    config->wait_for_client = SDL_RDP_SettingBoolean(SDL_HINT_RDP_WAIT_FOR_CLIENT, false);
    if (config->audio_latency_ms > SDL_MAX_SINT32 || config->port > 65535 || !config->width || config->width > SDL_MAX_SINT32 ||
        !config->height || config->height > SDL_MAX_SINT32) {
        return SDL_SetError("Invalid RDP port or dimensions");
    }
    if (!SDL_RDP_ParseCodec(SDL_RDP_Setting(SDL_HINT_RDP_CODEC), &config->codec) ||
        !SDL_RDP_ParseAspect(SDL_RDP_Setting(SDL_HINT_RDP_ASPECT), &config->aspect)) {
        return false;
    }
    return SDL_RDP_AuthConfig(config, &shared_backend);
}

bool SDL_RDP_AcquireBackend(SDL_RDP_Backend *backend, sdlrdp_handle **handle, sdlrdp_config *config)
{
    bool ok = true;
    if (SDL_ShouldInit(&shared_init)) {
        shared_lock = SDL_CreateMutex();
        SDL_SetInitialized(&shared_init, shared_lock != NULL);
    }
    if (!shared_lock) return false;
    SDL_LockMutex(shared_lock);
    if (!shared_refs) {
        ok = SDL_RDP_Config(&shared_config) && SDL_RDP_LoadBackend(&shared_backend);
        if (ok && shared_backend.open(&shared_config, &shared_handle) != 0) {
            SDL_SetError("%s", shared_backend.last_error());
            ok = false;
        }
        if (!ok) { SDL_RDP_UnloadBackend(&shared_backend); SDL_RDP_AuthRelease(); }
    }
    if (ok) {
        ++shared_refs;
        *backend = shared_backend;
        *handle = shared_handle;
        if (config) *config = shared_config;
    }
    SDL_UnlockMutex(shared_lock);
    return ok;
}

void SDL_RDP_ReleaseBackend(void)
{
    SDL_LockMutex(shared_lock);
    SDL_assert(shared_refs > 0);
    if (!--shared_refs) {
        shared_backend.close(shared_handle);
        shared_handle = NULL;
        SDL_RDP_AuthRelease();
        SDL_RDP_UnloadBackend(&shared_backend);
    }
    SDL_UnlockMutex(shared_lock);
}
