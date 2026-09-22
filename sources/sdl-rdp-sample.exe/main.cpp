#include <SDL3/SDL.h>
#include <format>
#include <charconv>
#include <string>
#include <cstdlib>
#include <memory>
#include <algorithm>
#include <ranges>
#include <array>
#include <cmath>
#include <numbers>

void Check(bool result)
{
    if (!result) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s", SDL_GetError());
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

void PrintGeometry(const SDL_Event &event, SDL_Window *window)
{
    if (event.type == SDL_EVENT_WINDOW_EXPOSED || event.type == SDL_EVENT_DISPLAY_DESKTOP_MODE_CHANGED) {
        auto mode = SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
        int w, h;
        Check(SDL_GetWindowSize(window, &w, &h));
        SDL_Log("event GEOMETRY window=%dx%d desktop=%dx%d", w, h, mode->w, mode->h);
    }
}

void PrintAudioFormat(SDL_AudioDeviceID device)
{
    SDL_AudioSpec actual;
    Check(SDL_GetAudioDeviceFormat(device, &actual, nullptr));
    SDL_Log("audio device=%s freq=%d", SDL_GetAudioDeviceName(device), actual.freq);
}

void PrintEvent(const SDL_Event &event, SDL_Window *window, unsigned frame)
{
    auto line = std::format("event {} type={}", EventName(event.type), event.type);
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN: case SDL_EVENT_KEY_UP:
        line += std::format(" scancode={} key={} down={}", int(event.key.scancode), event.key.key, int(event.key.down));
        break;
    case SDL_EVENT_MOUSE_MOTION:
        line += std::format(" x={:.0f} y={:.0f} frame={}", event.motion.x, event.motion.y, frame);
        break;
    case SDL_EVENT_MOUSE_BUTTON_DOWN: case SDL_EVENT_MOUSE_BUTTON_UP:
        line += std::format(" button={} down={}", event.button.button, int(event.button.down));
        break;
    case SDL_EVENT_AUDIO_DEVICE_FORMAT_CHANGED:
        PrintAudioFormat(event.adevice.which);
        break;
    case SDL_EVENT_MOUSE_WHEEL:
        line += std::format(" x={} y={}", event.wheel.x, event.wheel.y);
        break;
    case SDL_EVENT_DISPLAY_CURRENT_MODE_CHANGED: {
        auto mode = SDL_GetCurrentDisplayMode(SDL_GetPrimaryDisplay());
        line += std::format(" refresh={} numerator={} denominator={}", mode->refresh_rate,
                            mode->refresh_rate_numerator, mode->refresh_rate_denominator);
        break;
    }
    case SDL_EVENT_WINDOW_EXPOSED:
        line += std::format(" client_name={} codec={}", SDL_GetStringProperty(SDL_GetWindowProperties(window),
                    SDL_PROP_WINDOW_RDP_CLIENT_NAME_STRING, ""),
                    SDL_GetStringProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_RDP_CODEC_STRING, ""));
        break;
    default:
        if (event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST)
            line += std::format(" data1={} data2={}", event.window.data1, event.window.data2);
        break;
    }
    SDL_Log("%s", line.c_str());
    PrintGeometry(event, window);
}

void Draw(SDL_Window *window, unsigned frame, const SDL_FPoint &pointer)
{
    auto surface = SDL_GetWindowSurface(window);
    Check(surface != nullptr);
    Check(SDL_FillSurfaceRect(surface, nullptr, 0x00010101));
    SDL_Rect block{int(frame % unsigned(surface->w)), 40, 32, 32};
    Check(SDL_FillSurfaceRect(surface, &block, 0x0000ff00));
    Check(SDL_UpdateWindowSurface(window));
}

void CycleCodec()
{
    static constexpr std::array codecs{"auto", "planar", "remotefx", "nscodec", "raw"};
    const char *hint = SDL_GetHint(SDL_HINT_RDP_CODEC);
    auto current = std::ranges::find(codecs, std::string_view(hint ? hint : "auto"));
    auto next = current == codecs.end() ? 0 : (current - codecs.begin() + 1) % codecs.size();
    Check(SDL_SetHint(SDL_HINT_RDP_CODEC, codecs[next]));
}

void PrintCodecChange(SDL_Window *window, std::string &previous)
{
    std::string codec = SDL_GetStringProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_RDP_CODEC_STRING, "");
    if (codec != previous) {
        SDL_Log("event CODEC_CHANGED codec=%s", codec.c_str());
        previous = std::move(codec);
    }
}

void Run(SDL_Window *window, bool tight)
{
    std::string codec;
    SDL_FPoint pointer{-8, -8};
    unsigned frame = 0;
    Uint64 next = 0;
    for (;;) {
        SDL_Event event;
        if (SDL_WaitEventTimeout(&event, tight ? 0 : 10)) {
            PrintEvent(event, window, frame);
            if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.scancode == SDL_SCANCODE_F1)
                CycleCodec();
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE)) return;
            if (event.type == SDL_EVENT_MOUSE_MOTION) {
                pointer = {event.motion.x, event.motion.y};
                Draw(window, frame, pointer);
            }
        }
        PrintCodecChange(window, codec);
        if (SDL_GetTicks() >= next) {
            Draw(window, frame++, pointer);
            next = SDL_GetTicks() + (tight ? 0 : 100);
        }
    }
}

void SDLCALL FeedTone(void *userdata, SDL_AudioStream *stream, int additional, int)
{
    auto &frame = *static_cast<Uint64 *>(userdata);
    std::array<Sint16, 960> samples;
    while (additional > 0) {
        auto count = std::min(additional / int(2 * sizeof(Sint16)), 480);
        if (!count) return;
        for (int i = 0; i < count; ++i, ++frame) {
            auto value = Sint16(std::lround(32767 * std::pow(10.0, -12.0 / 20.0) *
                std::sin(2 * std::numbers::pi * 440 * double(frame) / 48000)));
            samples[2 * i] = samples[2 * i + 1] = value;
        }
        Check(SDL_PutAudioStreamData(stream, samples.data(), count * 2 * sizeof(Sint16)));
        additional -= count * 2 * sizeof(Sint16);
    }
}

SDL_AudioStream *OpenTone(Uint64 &frame)
{
    const SDL_AudioSpec spec{SDL_AUDIO_S16, 2, 48000};
    auto stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, FeedTone, &frame);
    Check(stream != nullptr);
    auto device = SDL_GetAudioStreamDevice(stream);
    PrintAudioFormat(device);
    Check(SDL_ResumeAudioStreamDevice(stream));
    return stream;
}

struct Options {
    int width = 640, height = 480;
    bool tight = false, fullscreen = false, tone = false;
};

Options ParseOptions(int argc, char **argv)
{
    Options options;
    for (int i = 1; i < argc; ++i) {
        std::string_view arg(argv[i]);
        if (arg == "--tone") options.tone = true;
        else if (arg == "--tight") options.tight = true;
        else if (arg == "--fullscreen") options.fullscreen = true;
        else if (arg == "--aspect" && i + 1 < argc) Check(SDL_SetHint(SDL_HINT_RDP_ASPECT, argv[++i]));
        else if (arg == "--size" && i + 1 < argc) {
            std::string_view size(argv[++i]);
            auto first = std::from_chars(size.data(), size.data() + size.size(), options.width);
            Check(first.ec == std::errc{} && first.ptr != size.data() + size.size() && *first.ptr == 'x');
            auto second = std::from_chars(first.ptr + 1, size.data() + size.size(), options.height);
            Check(second.ec == std::errc{} && second.ptr == size.data() + size.size() && options.width > 0 && options.height > 0);
        } else { SDL_SetError("Unknown or incomplete option: %s", argv[i]); Check(false); }
    }
    return options;
}

SDL_Cursor *CreateCursor()
{
    auto surface = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_ARGB8888);
    Check(surface != nullptr);
    Check(SDL_FillSurfaceRect(surface, nullptr, 0xffff0000));
    auto cursor = SDL_CreateColorCursor(surface, 0, 0);
    SDL_DestroySurface(surface);
    Check(cursor != nullptr);
    Check(SDL_SetCursor(cursor));
    return cursor;
}

int main(int argc, char **argv)
{
    auto options = ParseOptions(argc, argv);
    SDL_Log("SDL_GetVersion() %d", SDL_GetVersion());
    std::string drivers = "drivers";
    std::ranges::for_each(std::views::iota(0, SDL_GetNumVideoDrivers()),
        [&](int index) { drivers += std::format(" {}", SDL_GetVideoDriver(index)); });
    SDL_Log("%s", drivers.c_str());
    if (const char *codec = SDL_getenv("SDL_RDP_CODEC")) {
        std::string requested(codec);
        Check(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SDL_RDP_CODEC"));
        Check(SDL_SetHint(SDL_HINT_RDP_CODEC, requested.c_str()));
    }
    Check(SDL_Init(SDL_INIT_VIDEO | (options.tone ? SDL_INIT_AUDIO : 0)));
    auto display = SDL_GetPrimaryDisplay();
    SDL_Log("port %lld", (long long)SDL_GetNumberProperty(SDL_GetDisplayProperties(display), SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0));
    {
        std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> window(
            SDL_CreateWindow("SDL RDP sample", options.width, options.height, options.fullscreen ? SDL_WINDOW_FULLSCREEN : 0), SDL_DestroyWindow);
        Check(window != nullptr);
        std::unique_ptr<SDL_Cursor, decltype(&SDL_DestroyCursor)> cursor(CreateCursor(), SDL_DestroyCursor);
        Uint64 tone_frame = 0;
        std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> tone(
            options.tone ? OpenTone(tone_frame) : nullptr, SDL_DestroyAudioStream);
        Run(window.get(), options.tight);
    }
    SDL_Quit();
}
