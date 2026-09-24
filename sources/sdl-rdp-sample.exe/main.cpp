#include "_detail/check.hpp"
#include "auth.hpp"
#include "clipboard.hpp"
#include "drives.hpp"
#include "events.hpp"
#include "input.hpp"

#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <format>
#include <memory>
#include <numbers>
#include <ranges>
#include <string>

namespace {

void Draw(SDL_Window* window, unsigned frame, bool full) {
  auto* surface = SDL_GetWindowSurface(window);
  Check(surface != nullptr);
  Check(SDL_FillSurfaceRect(surface, nullptr, 0x00010101));
  SDL_Rect const block{ int(frame % unsigned(surface->w)), 40, 32, 32 };
  Check(SDL_FillSurfaceRect(surface, &block, 0x0000ff00));
  SDL_Rect const damage{ 0, 40, surface->w, std::min(32, std::max(0, surface->h - 40)) };
  Check(full ? SDL_UpdateWindowSurface(window) : SDL_UpdateWindowSurfaceRects(window, &damage, 1));
}

void CycleCodec() {
  static constexpr std::array codecs  { "auto", "planar", "remotefx", "nscodec", "raw", "progressive" };
  char const*                 hint    = SDL_GetHint(SDL_HINT_RDP_CODEC);
  auto const*                 current = std::ranges::find(codecs, std::string_view(hint ? hint : "auto"));
  auto                        next    = current == codecs.end() ? 0 : (current - codecs.begin() + 1) % codecs.size();
  Check(SDL_SetHint(SDL_HINT_RDP_CODEC, codecs[next]));
}

void PrintCodecChange(SDL_Window* window, std::string& previous) {
  std::string codec = SDL_GetStringProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_RDP_CODEC_STRING, "");
  if (codec != previous) {
    SDL_Log("event CODEC_CHANGED codec=%s", codec.c_str());
    previous = std::move(codec);
  }
}

void WindowShortcut(SDL_Event const& event, SDL_Window* window) {
  if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
  if (event.key.scancode == SDL_SCANCODE_F4)
    Check(SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)));
  if (event.key.scancode == SDL_SCANCODE_F1) CycleCodec();
}
bool ProcessEvent(SDL_Event const& event, SDL_Window* window, unsigned frame, bool& full, bool partial) {
  PrintEvent(event, window, frame);
  InputMode(event, window);
  if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED || event.type == SDL_EVENT_WINDOW_EXPOSED) full = true;
  WindowShortcut(event, window);
  if (event.type == SDL_EVENT_QUIT || (event.type == SDL_EVENT_KEY_DOWN && event.key.scancode == SDL_SCANCODE_ESCAPE))
    return false;
  if (event.type == SDL_EVENT_MOUSE_MOTION) {
    Draw(window, frame, full || !partial);
    full = false;
  }
  return true;
}
void DrawScheduled(SDL_Window* window, unsigned& frame, bool& full, Uint64& next, bool tight, bool partial) {
  if (SDL_GetTicks() < next) return;
  Draw(window, frame++, full || !partial);
  full = false;
  next = SDL_GetTicks() + (tight ? 0 : 100);
}
void Run(SDL_Window* window, bool tight, bool partial, DriveOptions drives) {
  std::string codec;
  bool        full  = true;
  unsigned    frame = 0;
  Uint64      next  = 0;
  for (;;) {
    SDL_Event event;
    if (SDL_WaitEventTimeout(&event, tight ? 0 : 10) && !ProcessEvent(event, window, frame, full, partial)) return;
    if (RunDrives(drives)) drives = { };
    PrintCodecChange(window, codec);
    DrawScheduled(window, frame, full, next, tight, partial);
  }
}

void SDLCALL FeedTone(void* userdata, SDL_AudioStream* stream, int additional, int /*unused*/) {
  auto&                   frame   = *static_cast<Uint64*>(userdata);
  std::array<Sint16, 960> samples { };
  while (additional > 0) {
    auto count = std::min(additional / int(2 * sizeof(Sint16)), 480);
    if (!count) return;
    for (int i = 0; i < count; ++i, ++frame) {
      auto value = Sint16(std::lround(32767 * std::pow(10.0, -12.0 / 20.0) *
                                            std::sin(2 * std::numbers::pi * 440 * double(frame) / 48000)));
      samples[2uz * i] = samples[(2uz * i) + 1] = value;
    }
    Check(SDL_PutAudioStreamData(stream, samples.data(), count * 2 * int(sizeof(Sint16))));
    additional -= count * 2 * int(sizeof(Sint16));
  }
}

SDL_AudioStream* OpenTone(Uint64& frame) {
  SDL_AudioSpec const spec   { SDL_AUDIO_S16, 2, 48000 };
  auto*               stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, FeedTone, &frame);
  Check(stream != nullptr);
  auto device = SDL_GetAudioStreamDevice(stream);
  PrintAudioFormat(device);
  Check(SDL_ResumeAudioStreamDevice(stream));
  return stream;
}

struct Options {
  DriveOptions drives;
  char const*  clip        = nullptr;
  int          width       = 640;
  int          height      = 480;
  int          mode_width  = 0;
  int          mode_height = 0;
  bool         tight       = false;
  bool         fullscreen  = false;
  bool         tone        = false;
  bool         partial     = false;
};

void ParseSize(std::string_view size, int& width, int& height) {
  auto first = std::from_chars(size.data(), size.data() + size.size(), width);
  Check(first.ec == std::errc{ } && first.ptr != size.data() + size.size() && *first.ptr == 'x');
  auto second = std::from_chars(first.ptr + 1, size.data() + size.size(), height);
  Check(second.ec == std::errc{ } && second.ptr == size.data() + size.size() && width > 0 && height > 0);
}

void Fullscreen(SDL_Window* window, Options const& options) {
  if (options.mode_width) {
    auto mode = *SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    mode.w = options.mode_width;
    mode.h = options.mode_height;
    Check(SDL_SetWindowFullscreenMode(window, &mode));
  }
  if (options.fullscreen) Check(SDL_SetWindowFullscreen(window, true));
}

bool FlagOption(std::string_view name, Options& options) {
  constexpr std::array<std::pair<std::string_view, bool Options::*>, 4> flags{ { { "--tone", &Options::tone },
                                                                                 { "--tight"  , &Options::tight   },
                                                                                 { "--partial", &Options::partial },
                                                                                 { "--fullscreen",
                                                                                   &Options::fullscreen } } };
  auto const* found = std::ranges::find(flags, name, &decltype(flags)::value_type::first);
  if (found == flags.end()) return false;
  options.*found->second = true;
  return true;
}
bool ValueOption(std::string_view name, char const* value, Options& options) {
  if (name == "--clip")
    options.clip = value;
  else if (name == "--ls")
    options.drives.list = value;
  else if (name == "--cat")
    options.drives.cat = value;
  else if (name == "--write")
    options.drives.write = value;
  else if (name == "--aspect")
    Check(SDL_SetHint(SDL_HINT_RDP_ASPECT, value));
  else if (name == "--size")
    ParseSize(value, options.width, options.height);
  else if (name == "--mode")
    ParseSize(value, options.mode_width, options.mode_height);
  else
    return false;
  return true;
}
Options ParseOptions(int argc, char** argv, Authenticator& authentication) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    std::string_view const arg(argv[i]);
    if (authentication.Option(arg, i, argc, argv) || FlagOption(arg, options)) continue;
    if (i + 1 < argc && ValueOption(arg, argv[i + 1], options))
      ++i;
    else {
      SDL_SetError("Unknown or incomplete option: %s", argv[i]);
      Check(false);
    }
  }
  return options;
}

SDL_Cursor* CreateCursor() {
  auto* surface = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_ARGB8888);
  Check(surface != nullptr);
  Check(SDL_FillSurfaceRect(surface, nullptr, 0xffff0000));
  auto* cursor = SDL_CreateColorCursor(surface, 0, 0);
  SDL_DestroySurface(surface);
  Check(cursor != nullptr);
  Check(SDL_SetCursor(cursor));
  return cursor;
}

void ConfigureVideo() {
  SDL_Log("SDL_GetVersion() %d", SDL_GetVersion());
  std::string drivers = "drivers";
  std::ranges::for_each(std::views::iota(0, SDL_GetNumVideoDrivers()),
                        [&](int index) { drivers += std::format(" {}", SDL_GetVideoDriver(index)); });
  SDL_Log("%s", drivers.c_str());
  if (char const* codec = SDL_getenv("SDL_RDP_CODEC")) {
    std::string const requested(codec);
    Check(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SDL_RDP_CODEC"));
    Check(SDL_SetHint(SDL_HINT_RDP_CODEC, requested.c_str()));
  }
}
void RunWindow(Options const& options) {
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> const window(
      SDL_CreateWindow("SDL RDP sample", options.width, options.height, 0), SDL_DestroyWindow);
  Check(window != nullptr);
  Fullscreen(window.get(), options);
  std::unique_ptr<SDL_Cursor, decltype(&SDL_DestroyCursor)> const cursor(CreateCursor(), SDL_DestroyCursor);
  Check(SDL_StartTextInput(window.get()));
  Uint64 tone_frame = 0;
  std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> const tone(
      options.tone ? OpenTone(tone_frame) : nullptr, SDL_DestroyAudioStream);
  Run(window.get(), options.tight, options.partial, options.drives);
}

}
int main(int argc,
         char** argv) { // NOLINT(bugprone-exception-escape): Allocation failure terminates the sample.
  Authenticator authentication;
  auto          options        = ParseOptions(argc, argv, authentication);
  authentication.Defaults();
  ConfigureVideo();
  Check(SDL_Init(SDL_INIT_VIDEO | (options.tone ? SDL_INIT_AUDIO : 0)));
  authentication.Install();
  if (options.clip) Check(SDL_SetClipboardText(options.clip));
  auto display = SDL_GetPrimaryDisplay();
  SDL_Log("port %lld",
          (long long)SDL_GetNumberProperty(SDL_GetDisplayProperties(display), SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0));
  RunWindow(options);
  SDL_Quit();
}
