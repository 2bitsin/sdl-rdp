#include "SDL_rdpevents.h"
#include "src/events/SDL_keyboard_c.h"
#include "src/events/SDL_mouse_c.h"
#include "src/events/SDL_windowevents_c.h"
#include "src/events/scancodes_windows.h"

static void SDL_RDP_Resize(SDL_VideoData *data, unsigned width, unsigned height)
{
    SDL_VideoDisplay *display = SDL_GetVideoDisplay(data->display);
    SDL_DisplayMode mode = display->desktop_mode;
    if (!width || !height || width > SDL_MAX_SINT32 || height > SDL_MAX_SINT32) {
        return;
    }
    if (mode.w != (int)width || mode.h != (int)height) {
        mode.w = (int)width;
        mode.h = (int)height;
        SDL_SetDesktopDisplayMode(display, &mode);
        SDL_SetCurrentDisplayMode(display, &mode);
    }
    if (data->window->w > (int)width || data->window->h > (int)height) {
        int w = SDL_min(data->window->w, (int)width);
        int h = SDL_min(data->window->h, (int)height);
        SDL_SendWindowEvent(data->window, SDL_EVENT_WINDOW_RESIZED, w, h);
        SDL_SendWindowEvent(data->window, SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED, w, h);
    }
}

static void SDL_RDP_Connected(SDL_VideoData *data, const sdlrdp_event *event)
{
    SDL_Window *window = data->window;
    SDL_RDP_Resize(data, event->connected.width, event->connected.height);
    SDL_SetStringProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_RDP_CLIENT_NAME_STRING,
                          event->connected.client_name);
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_EXPOSED, 0, 0);
    SDL_SetKeyboardFocus(window);
    SDL_SetMouseFocus(window);
}

static void SDL_RDP_Disconnected(SDL_VideoData *data)
{
    SDL_SendWindowEvent(data->window, SDL_EVENT_WINDOW_OCCLUDED, 0, 0);
    SDL_SetKeyboardFocus(NULL);
    SDL_SetMouseFocus(NULL);
}

static void SDL_RDP_Input(SDL_Window *window, const sdlrdp_event *event)
{
    static const Uint8 buttons[] = { 0, SDL_BUTTON_LEFT, SDL_BUTTON_MIDDLE, SDL_BUTTON_RIGHT, SDL_BUTTON_X1, SDL_BUTTON_X2 };
    switch (event->type) {
    case SDLRDP_KEY:
        SDL_SendKeyboardKey(0, SDL_DEFAULT_KEYBOARD_ID, event->key.scancode,
            windows_scancode_table[(event->key.scancode & 0xFF) | (event->key.extended ? 0x80 : 0)], event->key.down != 0);
        break;
    case SDLRDP_MOUSE_MOVE:
        SDL_SendMouseMotion(0, window, SDL_DEFAULT_MOUSE_ID, false, (float)event->mouse_move.x, (float)event->mouse_move.y);
        break;
    case SDLRDP_MOUSE_BUTTON:
        if (event->mouse_button.button > 0 && event->mouse_button.button < SDL_arraysize(buttons)) {
            SDL_SendMouseButton(0, window, SDL_DEFAULT_MOUSE_ID, buttons[event->mouse_button.button], event->mouse_button.down != 0);
        }
        break;
    case SDLRDP_MOUSE_WHEEL:
        SDL_SendMouseWheel(0, window, SDL_DEFAULT_MOUSE_ID, (float)event->mouse_wheel.dx,
                           (float)event->mouse_wheel.dy, SDL_MOUSEWHEEL_NORMAL);
        break;
    default:
        SDL_assert(!"unhandled rdp event");
        break;
    }
}

static void SDL_RDP_Dispatch(SDL_VideoData *data, const sdlrdp_event *event)
{
    if (!data->window) {
        return;
    }
    switch (event->type) {
    case SDLRDP_CONNECTED: SDL_RDP_Connected(data, event); break;
    case SDLRDP_DISCONNECTED: SDL_RDP_Disconnected(data); break;
    case SDLRDP_RESIZE: SDL_RDP_Resize(data, event->resize.width, event->resize.height); break;
    default: SDL_RDP_Input(data->window, event); break;
    }
}

void SDL_RDP_PumpEvents(SDL_VideoDevice *_this)
{
    SDL_VideoData *data = _this->internal;
    sdlrdp_event events[64];
    unsigned count, i;
    while ((count = data->backend.poll(data->handle, events, SDL_arraysize(events))) != 0) {
        for (i = 0; i < count; ++i) {
            SDL_RDP_Dispatch(data, &events[i]);
        }
    }
}

int SDL_RDP_WaitEventTimeout(SDL_VideoDevice *_this, Sint64 timeoutNS)
{
    Sint64 milliseconds = timeoutNS < 0 ? -1 : timeoutNS / SDL_NS_PER_MS + (timeoutNS % SDL_NS_PER_MS != 0);
    return _this->internal->backend.wait(_this->internal->handle, (int)SDL_min(milliseconds, SDL_MAX_SINT32));
}

void SDL_RDP_SendWakeupEvent(SDL_VideoDevice *_this, SDL_Window *window)
{
    _this->internal->backend.wakeup(_this->internal->handle);
}
