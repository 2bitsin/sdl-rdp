#include "SDL_rdpclipboard.h"
#include "src/events/SDL_clipboardevents_c.h"
#include "src/video/SDL_clipboard_c.h"

static bool SDL_RDP_SetClipboardText(SDL_VideoDevice *_this, const char *text)
{
    SDL_VideoData *data = _this->internal;
    if (data->backend.set_clipboard_text(data->handle, text) != 0) {
        return SDL_SetError("%s", data->backend.last_error());
    }
    return true;
}

static char *SDL_RDP_GetClipboardText(SDL_VideoDevice *_this)
{
    SDL_VideoData *data = _this->internal;
    const char *text = data->backend.get_clipboard_text(data->handle);
    if (!text) {
        SDL_SetError("%s", data->backend.last_error());
        return NULL;
    }
    return SDL_strdup(text);
}

static bool SDL_RDP_HasClipboardText(SDL_VideoDevice *_this)
{
    SDL_VideoData *data = _this->internal;
    int result = data->backend.has_clipboard_text(data->handle);
    if (result < 0) {
        return SDL_SetError("%s", data->backend.last_error());
    }
    return result != 0;
}

void SDL_RDP_InitClipboard(SDL_VideoDevice *device)
{
    device->SetClipboardText = SDL_RDP_SetClipboardText;
    device->GetClipboardText = SDL_RDP_GetClipboardText;
    device->HasClipboardText = SDL_RDP_HasClipboardText;
}

void SDL_RDP_ClipboardUpdate(SDL_VideoData *data)
{
    const char *types[] = { "text/plain;charset=utf-8" };
    size_t count = data->backend.has_clipboard_text(data->handle) > 0 ? 1 : 0;
    char **copy = SDL_CopyClipboardMimeTypes(types, count, true);
    if (copy) {
        SDL_SendClipboardUpdate(false, copy, count);
    }
}
