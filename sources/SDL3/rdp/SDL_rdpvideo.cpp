#include "SDL_rdpvideo.hpp"
#include "SDL_rdpconstants.hpp"
#include "boundary.hpp"
#include <oxbox/utilities/hash.hpp>
#include <cstdint>
namespace rdp {
using namespace oxbox::utilities::literals;
auto SetAspect(Driver const& driver, std::optional<std::string> const& value) -> void {
  if (driver.Call<Operation::SET_ASPECT>(Aspect(value)) != 0) driver.Throw();
}
auto PublishAspect(SDL_Window& window, std::optional<std::string> const& value) -> void {
  auto const aspect = value.value_or("");
  SDL_SetStringProperty(SDL_GetWindowProperties(&window), SDL_PROP_WINDOW_RDP_ASPECT_STRING, aspect.c_str());
}
namespace {
constexpr int DefaultRefreshHz = 60;
constexpr int MaximumRefreshHz = SDL_MAX_SINT32 / MillihertzPerHertz;
// Values are the frozen backend set_refresh mode argument.
enum class RefreshMode : std::uint32_t { FIXED = 0, CLIENT = 1, CLIENT_AVERAGE = 2, SENDER = 3 };
auto ApplyCodec(SDL_VideoData& data, std::optional<std::string> const& value) -> void {
  if (data.Backend().Call<Operation::SET_CODEC>(Codec(value)) != 0) data.Backend().Throw();
}
auto ApplyAspect(SDL_VideoData& data, std::optional<std::string> const& value) -> void {
  SetAspect(data.Backend(), value);
  if (auto const window = data.Window()) PublishAspect(*window, value);
}
// SDL hint observers receive an opaque context and nullable C strings.
template <auto APPLY>
auto SDLCALL HintChanged(void* context, char const* name, char const* old_value, char const* new_value) -> void {
  utilities::Expects(context != nullptr, "hint observer has video state");
  utilities::Expects(name != nullptr, "hint observer has a name");
  auto& data = *static_cast<SDL_VideoData*>(context);
  Boundary([&] { APPLY(data, data.Backend().Options().Changed(name, Text(old_value), Text(new_value))); });
}
auto ConfiguredRefresh(Settings const& settings) -> RefreshMode {
  switch (oxbox::utilities::HashString(settings.Get(SDL_HINT_RDP_REFRESH).value_or(""))) {
  case "auto-client"_hash:         return RefreshMode::CLIENT;
  case "auto-client-average"_hash: return RefreshMode::CLIENT_AVERAGE;
  case "auto-sender"_hash:         return RefreshMode::SENDER;
  default:                         return RefreshMode::FIXED;
  }
}
auto StartRefresh(Driver const& driver) -> int {
  auto const mode = ConfiguredRefresh(driver.Options());
  auto const hz   = mode == RefreshMode::FIXED
                      ? driver.Options().Integer(SDL_HINT_RDP_REFRESH, DefaultRefreshHz, 1, MaximumRefreshHz)
                      : DefaultRefreshHz;
  if (driver.Call<Operation::SET_REFRESH>(std::to_underlying(mode), hz) != 0) driver.Throw();
  return hz;
}
auto DesktopDisplayMode(Driver const& driver) -> SDL_DisplayMode {
  SDL_DisplayMode mode{ };
  mode.format                   = SDL_PIXELFORMAT_XRGB8888;
  mode.w                        = static_cast<int>(driver.Config().width);
  mode.h                        = static_cast<int>(driver.Config().height);
  mode.refresh_rate_numerator   = StartRefresh(driver);
  mode.refresh_rate_denominator = 1;
  mode.refresh_rate             = static_cast<float>(mode.refresh_rate_numerator);
  return mode;
}
auto InitDisplay(SDL_VideoData& data) -> void {
  auto const mode = DesktopDisplayMode(data.Backend());
  data.Display(SDL_AddBasicVideoDisplay(&mode));
  if (!data.Display()) throw std::runtime_error(SDL_GetError());
  auto const properties = SDL_GetDisplayProperties(data.Display());
  UpdateDrives(data.Backend(), properties);
  if (!SDL_SetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, data.Backend().Call<Operation::PORT>()))
    throw std::runtime_error(SDL_GetError());
}
constexpr auto StandardModes = std::to_array<std::pair<int, int>>({
    { 320 , 200  },
    { 320 , 240  },
    { 320 , 256  },
    { 400 , 300  },
    { 512 , 384  },
    { 640 , 350  },
    { 640 , 400  },
    { 640 , 480  },
    { 720 , 400  },
    { 720 , 480  },
    { 800 , 600  },
    { 1024, 768  },
    { 1280, 720  },
    { 1280, 800  },
    { 1920, 1080 },
    { 1920, 1200 },
    { 2560, 1440 },
    { 3840, 2160 },
});
// SDL's video callback table supplies a borrowed device and display.
auto DisplayModes([[maybe_unused]] SDL_VideoDevice* unused_device, SDL_VideoDisplay* display) -> bool {
  utilities::Expects(display != nullptr, "mode enumeration has a display");
  auto mode = display->desktop_mode;
  SDL_AddFullscreenDisplayMode(display, &mode);
  for (auto const& [width, height] : StandardModes) {
    mode.w = width;
    mode.h = height;
    SDL_AddFullscreenDisplayMode(display, &mode);
  }
  return true;
}
// SDL's video callback table supplies borrowed device, display and mode pointers.
auto DisplayMode(SDL_VideoDevice* device, [[maybe_unused]] SDL_VideoDisplay* unused_display, SDL_DisplayMode* mode)
    -> bool {
  utilities::Expects(device != nullptr, "mode change has a device");
  utilities::Expects(mode != nullptr, "mode change has a mode");
  return ResizePicture(*device->internal, mode->w, mode->h);
}
auto RelativeMouse(bool enabled) -> bool {
  auto const& driver = CurrentVideo().Backend();
  return driver.Call<Operation::SET_RELATIVE_MOUSE>(enabled) == 0 || driver.Fail();
}
// SDL's video initialization callback borrows its device.
auto VideoInit(SDL_VideoDevice* device) -> bool {
  utilities::Expects(device != nullptr, "video initialization has a device");
  return Boundary([&] {
    InitDisplay(*device->internal);
    SDL_AddKeyboard(SDL_DEFAULT_KEYBOARD_ID, nullptr);
    SDL_AddMouse(SDL_DEFAULT_MOUSE_ID, nullptr);
    SDL_GetMouse()->SetRelativeMouseMode = RelativeMouse;
    InitMouse();
    return true;
  });
}
// SDL's video shutdown callback borrows its device.
auto VideoQuit(SDL_VideoDevice* device) -> void {
  utilities::Expects(device != nullptr, "video shutdown has a device");
  device->internal->Display(0);
}
// SDL returns ownership of the device and its opaque internal state to this callback.
auto DeleteDevice(SDL_VideoDevice* device) -> void {
  utilities::Expects(device != nullptr, "device destruction owns a device");
  std::unique_ptr<SDL_VideoDevice> const owner{ device                                  };
  std::unique_ptr<SDL_VideoData> const   state{ std::exchange(owner->internal, nullptr) };
}
auto RequestsRdp() -> bool {
  auto const hint = Text(SDL_GetHint(SDL_HINT_VIDEO_DRIVER));
  return hint && hint->contains("rdp");
}
// SDL's bootstrap takes ownership of the returned video device.
auto CreateDevice() -> SDL_VideoDevice* {
  return Boundary([&]() -> SDL_VideoDevice* {
    if (!RequestsRdp()) return nullptr;
    auto device = std::make_unique<SDL_VideoDevice>();
    auto data   = std::make_unique<SDL_VideoData>(Rendezvous::Acquire(), HintChanged<ApplyCodec>,
                                                  HintChanged<ApplyAspect>);
    device->is_dummy        = true;
    device->VideoInit       = VideoInit;
    device->VideoQuit       = VideoQuit;
    device->GetDisplayModes = DisplayModes;
    device->SetDisplayMode  = DisplayMode;
    device->free            = DeleteDevice;
    InitWindow(*device);
    InitFramebuffer(*device);
    InitEvents(*device);
    InitClipboard(*device);
    device->internal = data.release();
    return device.release();
  });
}
}
}
// SDL's C bootstrap table requires this named object with static storage.
extern "C" VideoBootStrap const RDP_bootstrap = { "rdp", "SDL RDP video driver", rdp::CreateDevice, nullptr, false };
