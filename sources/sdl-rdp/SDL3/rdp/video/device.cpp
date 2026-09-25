#include "device.hpp"
#include "clipboard.hpp"
#include "events.hpp"
#include "window.hpp"
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
#include <sdl-rdp/SDL3/rdp/input/mouse.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/boundary.hpp>
#include <sdl-rdp/SDL3/rdp/settings/options.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <utility>
namespace sdl3::rdp::video::detail::device {
using sdl3::rdp::input::InitMouse;
using sdl3::rdp::sdl::Boundary;
using sdl3::rdp::settings::Text;
using sdl3::rdp::storage::UpdateDrives;
using sdl_rdp::settings::Aspect;
using sdl_rdp::settings::Settings;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
auto SetAspect(Driver& driver, Aspect const& value) -> void {
  driver.Backend().Presentation().SetAspect(value.Ratio());
}
auto PublishAspect(SDL_Window& window, Aspect const& value) -> void {
  SDL_SetStringProperty(SDL_GetWindowProperties(&window), SDL_PROP_WINDOW_RDP_ASPECT_STRING, value.Text().c_str());
}
namespace {
auto StartRefresh(Driver& driver) -> int {
  auto const refresh = driver.Options().Value<&Settings::refresh>();
  driver.Backend().Presentation().SetRefresh(refresh.Mode(), refresh.Hz());
  return Narrowed<int>(refresh.Hz());
}
auto DesktopDisplayMode(Driver& driver) -> SDL_DisplayMode {
  SDL_DisplayMode mode{ };
  mode.format                   = SDL_PIXELFORMAT_XRGB8888;
  mode.w                        = static_cast<int>(driver.Config().Width());
  mode.h                        = static_cast<int>(driver.Config().Height());
  mode.refresh_rate_numerator   = StartRefresh(driver);
  mode.refresh_rate_denominator = 1;
  mode.refresh_rate             = static_cast<float>(mode.refresh_rate_numerator);
  return mode;
}
auto InitDisplay(SDL_VideoData& data) -> void {
  auto const mode = DesktopDisplayMode(data.Driver());
  data.Display(SDL_AddBasicVideoDisplay(&mode));
  if (!data.Display()) throw RelayedFailure{ SDL_GetError() };
  auto const properties = SDL_GetDisplayProperties(data.Display());
  UpdateDrives(data.Driver(), properties);
  if (!SDL_SetNumberProperty(properties, SDL_PROP_DISPLAY_RDP_PORT_NUMBER, data.Driver().Backend().Port()))
    throw RelayedFailure{ SDL_GetError() };
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
  Expects(display != nullptr, "mode enumeration has a display");
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
  Expects(device != nullptr, "mode change has a device");
  Expects(mode != nullptr, "mode change has a mode");
  return Boundary([&] {
    ResizePicture(*device->internal, mode->w, mode->h);
    return true;
  });
}
auto RelativeMouse(bool enabled) -> bool {
  return Boundary([&] {
    CurrentVideo().Driver().Backend().SetRelativeMouse(enabled);
    return true;
  });
}
// SDL's video initialization callback borrows its device.
auto VideoInit(SDL_VideoDevice* device) -> bool {
  Expects(device != nullptr, "video initialization has a device");
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
  Expects(device != nullptr, "video shutdown has a device");
  device->internal->Display(0);
}
// SDL returns ownership of the device and its opaque internal state to this callback.
auto DeleteDevice(SDL_VideoDevice* device) -> void {
  Expects(device != nullptr, "device destruction owns a device");
  std::unique_ptr<SDL_VideoDevice> const owner{ device                                  };
  std::unique_ptr<SDL_VideoData> const   state{ std::exchange(owner->internal, nullptr) };
}
auto RequestsRdp() -> bool {
  auto const hint = Text(SDL_GetHint(SDL_HINT_VIDEO_DRIVER));
  return hint && hint->contains("rdp");
}
// SDL's bootstrap takes ownership of the returned video device.
auto CreateDevice() -> SDL_VideoDevice* {
  return Boundary([&]() -> std::unique_ptr<SDL_VideoDevice> {
           if (!RequestsRdp()) return nullptr;
           auto device = std::make_unique<SDL_VideoDevice>();
           auto data   = std::make_unique<SDL_VideoData>(Rendezvous::Acquire());
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
           return device;
         })
      .release();
}
}
// SDL's C bootstrap table requires this named object with static storage; C linkage names the global symbol.
extern "C" VideoBootStrap const RDP_bootstrap = { "rdp", "SDL RDP video driver", CreateDevice, nullptr, false };
}
