#include <sample/auth.hpp>
#include <sample/check.hpp>
#include <sample/clipboard.hpp>
#include <sample/drives.hpp>
#include <sample/events.hpp>
#include <sample/input.hpp>

#include <SDL3/SDL.h>
#include <oxbox/utilities/number-text.hpp>
#include <oxbox/utilities/text.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <format>
#include <memory>
#include <numbers>
#include <ranges>
#include <string>

namespace sample::detail::main {
namespace {

auto Draw(SDL_Window* window, std::uint32_t frame, bool full) -> void {
  auto* surface = SDL_GetWindowSurface(window);
  Check(surface != nullptr);
  Check(SDL_FillSurfaceRect(surface, nullptr, 0x00010101));
  Check(surface->w > 0);
  // The column is below the positive surface width, so both conversions keep the value.
  SDL_Rect const block{ static_cast<int>(frame % static_cast<std::uint32_t>(surface->w)), 40, 32, 32 };
  Check(SDL_FillSurfaceRect(surface, &block, 0x0000ff00));
  SDL_Rect const damage{ 0, 40, surface->w, std::min(32, std::max(0, surface->h - 40)) };
  Check(full ? SDL_UpdateWindowSurface(window) : SDL_UpdateWindowSurfaceRects(window, &damage, 1));
}

auto CycleCodec() -> void {
  static constexpr std::array codecs  { "auto", "planar", "remotefx", "nscodec", "raw", "progressive" };
  char const*                 hint    = SDL_GetHint(SDL_HINT_RDP_CODEC);
  auto const*                 current = std::ranges::find(codecs, std::string_view(hint ? hint : "auto"));
  auto                        next    = current == codecs.end() ? 0 : (current - codecs.begin() + 1) % codecs.size();
  Check(SDL_SetHint(SDL_HINT_RDP_CODEC, codecs[next]));
}

auto PrintCodecChange(SDL_Window* window, std::string& previous) -> void {
  std::string codec = SDL_GetStringProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_RDP_CODEC_STRING, "");
  if (codec != previous) {
    SDL_Log("event CODEC_CHANGED codec=%s", codec.c_str());
    previous = std::move(codec);
  }
}

auto WindowShortcut(SDL_Event const& event, SDL_Window* window) -> void {
  if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) return;
  if (event.key.scancode == SDL_SCANCODE_F4)
    Check(SDL_SetWindowFullscreen(window, !(SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN)));
  if (event.key.scancode == SDL_SCANCODE_F1) CycleCodec();
}
auto ProcessEvent(SDL_Event const& event, SDL_Window* window, std::uint32_t frame, bool& full, bool partial) -> bool {
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
auto DrawScheduled(SDL_Window* window, std::uint32_t& frame, bool& full, std::uint64_t& next, bool tight, bool partial)
    -> void {
  if (SDL_GetTicks() < next) return;
  Draw(window, frame++, full || !partial);
  full = false;
  next = SDL_GetTicks() + (tight ? 0 : 100);
}
auto Run(SDL_Window* window, bool tight, bool partial, DriveOptions drives) -> void {
  std::string   codec;
  bool          full  = true;
  std::uint32_t frame = 0;
  std::uint64_t next  = 0;
  for (;;) {
    SDL_Event event;
    if (SDL_WaitEventTimeout(&event, tight ? 0 : 10) && !ProcessEvent(event, window, frame, full, partial)) return;
    if (RunDrives(drives)) drives = { };
    PrintCodecChange(window, codec);
    DrawScheduled(window, frame, full, next, tight, partial);
  }
}

auto SDLCALL FeedTone(void* userdata, SDL_AudioStream* stream, int additional, int /*unused*/) -> void {
  auto&                         frame   = *static_cast<std::uint64_t*>(userdata);
  std::array<std::int16_t, 960> samples { };
  while (additional > 0) {
    auto count = std::min(additional / int(2 * sizeof(std::int16_t)), 480);
    if (!count) return;
    for (int i = 0; i < count; ++i, ++frame) {
      // A -12 dBFS sine stays inside std::int16_t.
      auto value = static_cast<std::int16_t>(std::lround(
          32767 * std::pow(10.0, -12.0 / 20.0) * std::sin(2 * std::numbers::pi * 440 * double(frame) / 48000)));
      samples[2uz * i] = samples[(2uz * i) + 1] = value;
    }
    Check(SDL_PutAudioStreamData(stream, samples.data(), count * 2 * int(sizeof(std::int16_t))));
    additional -= count * 2 * int(sizeof(std::int16_t));
  }
}

auto OpenTone(std::uint64_t& frame) -> SDL_AudioStream* {
  SDL_AudioSpec const spec   { SDL_AUDIO_S16, 2, 48000 };
  auto*               stream = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, FeedTone, &frame);
  Check(stream != nullptr);
  auto device = SDL_GetAudioStreamDevice(stream);
  PrintAudioFormat(device);
  Check(SDL_ResumeAudioStreamDevice(stream));
  return stream;
}

struct Extent {
  int width  = 0;
  int height = 0;
};

struct Options {
  DriveOptions drives;
  char const*  clip       = nullptr;
  Extent       size       = { .width = 640, .height = 480 };
  Extent       mode       = { };
  bool         tight      = false;
  bool         fullscreen = false;
  bool         tone       = false;
  bool         partial    = false;
};

auto ParseSize(std::string_view size) -> Extent {
  auto const sides = oxbox::utilities::ParseNumbers<int, 2>(size, 'x').value_or(std::array{ 0, 0 });
  if (std::ranges::any_of(sides, [](int side) { return side <= 0; })) {
    SDL_SetError("Invalid size '%.*s': expected WxH with positive sides", int(size.size()), size.data());
    Check(false);
  }
  return { .width = sides[0], .height = sides[1] };
}

auto Fullscreen(SDL_Window* window, Options const& options) -> void {
  if (options.mode.width) {
    auto mode = *SDL_GetDesktopDisplayMode(SDL_GetPrimaryDisplay());
    mode.w = options.mode.width;
    mode.h = options.mode.height;
    Check(SDL_SetWindowFullscreenMode(window, &mode));
  }
  if (options.fullscreen) Check(SDL_SetWindowFullscreen(window, true));
}

auto FlagOption(std::string_view name, Options& options) -> bool {
  constexpr std::array<std::pair<std::string_view, bool Options::*>, 4> flags{ { { "--tone"   , &Options::tone    },
                                                                                 { "--tight"  , &Options::tight   },
                                                                                 { "--partial", &Options::partial },
                                                                                 { "--fullscreen",
                                                                                   &Options::fullscreen } } };
  auto const* found = std::ranges::find(flags, name, &decltype(flags)::value_type::first);
  if (found == flags.end()) return false;
  options.*found->second = true;
  return true;
}
auto ValueOption(std::string_view name, char const* value, Options& options) -> bool {
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
    options.size = ParseSize(value);
  else if (name == "--mode")
    options.mode = ParseSize(value);
  else
    return false;
  return true;
}
auto ParseOptions(int argc, char** argv, Authenticator& authentication) -> Options {
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

auto CreateCursor() -> SDL_Cursor* {
  auto* surface = SDL_CreateSurface(8, 8, SDL_PIXELFORMAT_ARGB8888);
  Check(surface != nullptr);
  Check(SDL_FillSurfaceRect(surface, nullptr, 0xffff0000));
  auto* cursor = SDL_CreateColorCursor(surface, 0, 0);
  SDL_DestroySurface(surface);
  Check(cursor != nullptr);
  Check(SDL_SetCursor(cursor));
  return cursor;
}

auto ConfigureVideo() -> void {
  SDL_Log("SDL_GetVersion() %d", SDL_GetVersion());
  auto const drivers = oxbox::utilities::Joined(std::views::iota(0, SDL_GetNumVideoDrivers()), " ", SDL_GetVideoDriver);
  SDL_Log("drivers %s", drivers.c_str());
  if (char const* codec = SDL_getenv("SDL_RDP_CODEC")) {
    std::string const requested(codec);
    Check(SDL_UnsetEnvironmentVariable(SDL_GetEnvironment(), "SDL_RDP_CODEC"));
    Check(SDL_SetHint(SDL_HINT_RDP_CODEC, requested.c_str()));
  }
}
auto RunWindow(Options const& options) -> void {
  std::unique_ptr<SDL_Window, decltype(&SDL_DestroyWindow)> const window(
      SDL_CreateWindow("SDL RDP sample", options.size.width, options.size.height, 0), SDL_DestroyWindow);
  Check(window != nullptr);
  Fullscreen(window.get(), options);
  std::unique_ptr<SDL_Cursor, decltype(&SDL_DestroyCursor)> const cursor(CreateCursor(), SDL_DestroyCursor);
  Check(SDL_StartTextInput(window.get()));
  std::uint64_t tone_frame = 0;
  std::unique_ptr<SDL_AudioStream, decltype(&SDL_DestroyAudioStream)> const tone(
      options.tone ? OpenTone(tone_frame) : nullptr, SDL_DestroyAudioStream);
  Run(window.get(), options.tight, options.partial, options.drives);
}

auto Main(int argc, char** argv) -> void {
  Authenticator authentication;
  auto          options        = ParseOptions(argc, argv, authentication);
  authentication.Defaults();
  ConfigureVideo();
  Check(SDL_Init(SDL_INIT_VIDEO | (options.tone ? SDL_INIT_AUDIO : 0)));
  authentication.Install();
  if (options.clip) Check(SDL_SetClipboardText(options.clip));
  auto display = SDL_GetPrimaryDisplay();
  SDL_Log("port %" SDL_PRIs64,
          SDL_GetNumberProperty(SDL_GetDisplayProperties(display), SDL_PROP_DISPLAY_RDP_PORT_NUMBER, 0));
  RunWindow(options);
  SDL_Quit();
}
}
}
auto main(int argc,
          char** argv) -> int { // NOLINT(bugprone-exception-escape): Allocation failure terminates the sample.
  sample::detail::main::Main(argc, argv);
}
