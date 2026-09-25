#include "window.hpp"
#include "device.hpp"
#include <sdl-rdp/SDL3/rdp/sdl/boundary.hpp>
#include <sdl-rdp/session/backend.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
namespace sdl3::rdp::video::detail::window {
using sdl3::rdp::sdl::Boundary;
using sdl_rdp::settings::Settings;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::RAIIWrap;
namespace {
// SDL rejects desktop updates while fullscreen_active is set.
using FullscreenState = std::pair<std::reference_wrapper<SDL_VideoDisplay>, bool>;
auto SuspendFullscreen(SDL_VideoDisplay& display) -> FullscreenState {
  return { display, std::exchange(display.fullscreen_active, false) };
}
auto RestoreFullscreen(FullscreenState const& state) noexcept -> void {
  state.first.get().fullscreen_active = state.second;
}
using FullscreenSuspension = RAIIWrap<FullscreenState, SuspendFullscreen, RestoreFullscreen>;
}
auto DesktopMode(SDL_VideoData const& data, int width, int height) -> void {
  Expects(width > 0, "desktop has width");
  Expects(height > 0, "desktop has height");
  auto& display = *SDL_GetVideoDisplay(data.Display());
  auto  mode    = display.desktop_mode;
  mode.w = width;
  mode.h = height;
  FullscreenSuspension const suspended{ display };
  SDL_SetDesktopDisplayMode(&display, &mode);
}
auto ResizePicture(SDL_VideoData& data, int width, int height) -> void {
  Expects(width > 0, "picture has width");
  Expects(height > 0, "picture has height");
  data.Driver().Backend().Presentation().Resize(
      { .width = Narrowed<std::uint32_t>(width), .height = Narrowed<std::uint32_t>(height) });
  data.Picture(width, height);
  auto const& mode = SDL_GetVideoDisplay(data.Display())->desktop_mode;
  if (mode.w != width || mode.h != height) DesktopMode(data, width, height);
}
namespace {
auto PlaceAtOrigin(SDL_Window& window) -> void {
  window.x = window.windowed.x = window.floating.x = 0;
  window.y = window.windowed.y = window.floating.y = 0;
}
// SDL's video callback table supplies borrowed device and window pointers.
auto CreateWindow(SDL_VideoDevice* device, SDL_Window* window, [[maybe_unused]] SDL_PropertiesID unused_properties)
    -> bool {
  Expects(device != nullptr, "window creation has a device");
  Expects(window != nullptr, "window creation has a window");
  return Boundary([&] {
    auto& data = *device->internal;
    if (data.Window()) return SDL_SetError("RDP supports one window");
    auto const aspect = data.Driver().Options().Value<&Settings::aspect>();
    SetAspect(data.Driver(), aspect);
    PlaceAtOrigin(*window);
    ResizePicture(data, window->w, window->h);
    data.Bind(*window);
    PublishAspect(*window, aspect);
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_OCCLUDED, 0, 0);
    return true;
  });
}
// SDL's video callback table supplies borrowed device and window pointers.
auto DestroyWindow(SDL_VideoDevice* device, SDL_Window* window) -> void {
  Expects(device != nullptr, "window destruction has a device");
  Expects(window != nullptr, "window destruction has a window");
  auto& data = *device->internal;
  if (data.Window() && &BoundWindow(data) == window) data.Unbind();
}
// SDL's video callback table supplies borrowed device and window pointers.
auto SetWindowSize(SDL_VideoDevice* device, SDL_Window* window) -> void {
  Expects(device != nullptr, "resize has a device");
  Expects(window != nullptr, "resize has a window");
  Boundary([&] {
    ResizePicture(*device->internal, window->pending.w, window->pending.h);
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->pending.w, window->pending.h);
  });
  window->last_size_pending = false;
}
// SDL's video callback table requires a show callback even for a headless window.
auto ShowWindow([[maybe_unused]] SDL_VideoDevice* unused_device, [[maybe_unused]] SDL_Window* unused_window) -> void { }
// SDL's fullscreen callback supplies borrowed device, window and display pointers.
auto Fullscreen(SDL_VideoDevice* device, SDL_Window* window, SDL_VideoDisplay* display, SDL_FullscreenOp operation)
    -> SDL_FullscreenResult {
  Expects(device != nullptr, "fullscreen has a device");
  Expects(window != nullptr, "fullscreen has a window");
  Expects(display != nullptr, "fullscreen has a display");
  auto const  leaving = operation == SDL_FULLSCREEN_OP_LEAVE;
  auto const& mode    = window->requested_fullscreen_mode.w ? window->requested_fullscreen_mode : display->desktop_mode;
  auto const  width   = leaving ? window->windowed.w : mode.w;
  auto const  height  = leaving ? window->windowed.h : mode.h;
  return Boundary(
      [&] {
        ResizePicture(*device->internal, width, height);
        if (leaving) SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, width, height);
        return SDL_FULLSCREEN_SUCCEEDED;
      },
      SDL_FULLSCREEN_FAILED);
}
}
auto InitWindow(SDL_VideoDevice& device) -> void {
  device.CreateSDLWindow     = CreateWindow;
  device.DestroyWindow       = DestroyWindow;
  device.SetWindowSize       = SetWindowSize;
  device.ShowWindow          = ShowWindow;
  device.SetWindowFullscreen = Fullscreen;
}
}
