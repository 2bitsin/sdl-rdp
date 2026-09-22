#include <SDL3/SDL.h>
#include <cstdio>
#include <print>
#include <string>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <ranges>

void Check(bool result)
{
    if (!result) {
        std::println(stderr, "{}", SDL_GetError());
        std::exit(1);
    }
}

const char *EventName(Uint32 type)
{
    switch (type) {
    case SDL_EVENT_KEYBOARD_ADDED: return "KEYBOARD_ADDED";
    case SDL_EVENT_MOUSE_ADDED: return "MOUSE_ADDED";
    case SDL_EVENT_WINDOW_MOVED: return "MOVED";
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED: return "SAFE_AREA_CHANGED";
    case SDL_EVENT_QUIT: return "QUIT";
    case SDL_EVENT_WINDOW_SHOWN: return "SHOWN";
    case SDL_EVENT_WINDOW_HIDDEN: return "HIDDEN";
    case SDL_EVENT_WINDOW_OCCLUDED: return "OCCLUDED";
    case SDL_EVENT_WINDOW_EXPOSED: return "EXPOSED";
    case SDL_EVENT_WINDOW_RESIZED: return "RESIZED";
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED: return "PIXEL_SIZE_CHANGED";
    case SDL_EVENT_WINDOW_FOCUS_GAINED: return "FOCUS_GAINED";
    case SDL_EVENT_WINDOW_FOCUS_LOST: return "FOCUS_LOST";
    case SDL_EVENT_WINDOW_MOUSE_ENTER: return "MOUSE_ENTER";
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: return "MOUSE_LEAVE";
    case SDL_EVENT_DISPLAY_ADDED: return "DISPLAY_ADDED";
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED: return "DISPLAY_CURRENT_MODE_CHANGED";
    case SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED: return "DISPLAY_DESKTOP_MODE_CHANGED";
    case SDL_EVENT_KEY_DOWN: return "KEY_DOWN";
    case SDL_EVENT_KEY_UP: return "KEY_UP";
    case SDL_EVENT_MOUSE_MOTION: return "MOUSE_MOTION";
    case SDL_EVENT_MOUSE_BUTTON_DOWN: return "MOUSE_BUTTON_DOWN";
    case SDL_EVENT_MOUSE_BUTTON_UP: return "MOUSE_BUTTON_UP";
    case SDL_EVENT_MOUSE_WHEEL: return "MOUSE_WHEEL";
    default: return "OTHER";
    }
}

void PrintEvent(const SDL_Event &event, SDL_Window *window)
{
    auto line = std::format("event {} type={}", EventName(event.type), event.type);
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
        line += std::format(" scancode={} key={} down={}", int(event.key.scancode), event.key.key, int(event.key.down));
        break;
    case SDL_EVENT_MOUSE_MOTION:
        line += std::format(" x={:.0f} y={:.0f}", event.motion.x, event.motion.y);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        line += std::format(" button={} down={}", event.button.button, int(event.button.down));
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        line += std::format(" x={} y={}", event.wheel.x, event.wheel.y);
        break;
    case SDL_EVENT_WINDOW_EXPOSED:
        line += std::format(" client_name={}", SDL_GetStringProperty(SDL_GetWindowProperties(window),
                    SDL_PROP_WINDOW_RDP_CLIENT_NAME_STRING, ""));
        break;
    default:
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST)
            line += std::format(" data1={} data2={}", event.window.data1, event.window.data2);
        break;
    }
    std::println("{}", line);
    std::fflush(stdout);
}

void Draw(SDL_Window *window, unsigned frame, const SDL_FPoint &pointer)
{
    auto surface = SDL_GetWindowSurface(window);
    Check(surface != nullptr);
    Check(SDL_FillSurfaceRect(surface, nullptr, 0x00010101));
    SDL_Rect block{int(frame % unsigned(surface->w)), 40, 32, 32};
    Check(SDL_FillSurfaceRect(surface, &block, 0x0000ff00));
    SDL_Rect cursor{int(pointer.x), int(pointer.y), 8, 8};
    Check(SDL_FillSurfaceRect(surface, &cursor, 0x00ff0000));
    Check(SDL_UpdateWindowSurface(window));
}

void Run(SDL_Window *window)
{
    SDL_FPoint pointer{-8, -8};
    unsigned frame = 0;
    Uint64 next = 0;
    for (;;) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, 10)) {
            PrintEvent(event, window);
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)) return;
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                pointer = {event.motion.x, event.motion.y};
                Draw(window, frame, pointer);
            }
        }
        if (SDL_GetTicks() >= next) {
            Draw(window, frame++, pointer);
            next = SDL_GetTicks() + 100;
        }
    }
}

int main()
{
    std::println("SDL_GetVersion() {}", SDL_GetVersion());
    std::string drivers = "drivers";
    std::ranges::for_each(std::views::iota(0, SDL_GetNumVideoDrivers()),
        [&](int index) { drivers += std::format(" {}", SDL_GetVideoDriver(index)); });
    std::println("{}", drivers);
    Check(SDL_Init(SDL_INIT_VIDEO));
    auto display = SDL_GetPrimaryDisplay();
    std::println("port {}", SDL_GetNumberProperty(SDL_GetDisplayProperties(display),
                SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0));
    std::fflush(stdout);
    {
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("SDL RDP sample", 640, 480, 0), SDL_DestroyWindow);
        Check(window != nullptr);
        Run(window.get());
    }
    SDL_Quit();
}
