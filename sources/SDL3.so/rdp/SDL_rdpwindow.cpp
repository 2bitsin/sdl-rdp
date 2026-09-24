#include "SDL_rdpvideo.hpp"
#include "boundary.hpp"
namespace rdp {
namespace {
// SDL rejects desktop updates while fullscreen_active is set.
using FullscreenState = std::pair<std::reference_wrapper<SDL_VideoDisplay>, bool>;
auto SuspendFullscreen(SDL_VideoDisplay& display) -> FullscreenState {
  return {display, std::exchange(display.fullscreen_active, false)};
}
auto RestoreFullscreen(FullscreenState const& state) noexcept -> void {
  state.first.get().fullscreen_active = state.second;
}
using FullscreenSuspension = utilities::RAIIWrap<FullscreenState, SuspendFullscreen, RestoreFullscreen>;
}
auto DesktopMode(SDL_VideoData const& data, int width, int height) -> void {
  utilities::Expects(width > 0, "desktop has width");
  utilities::Expects(height > 0, "desktop has height");
  auto& display = *SDL_GetVideoDisplay(data.Display());
  auto  mode    = display.desktop_mode;
  mode.w = width;
  mode.h = height;
  FullscreenSuspension const suspended{ display };
  SDL_SetDesktopDisplayMode(&display, &mode);
}
auto ResizePicture(SDL_VideoData& data, int width, int height) -> bool {
  utilities::Expects(width > 0, "picture has width");
  utilities::Expects(height > 0, "picture has height");
  if (data.Backend().Call<Operation::RESIZE>(width, height) != 0) return data.Backend().Fail();
  data.Picture(width, height);
  auto const& mode = SDL_GetVideoDisplay(data.Display())->desktop_mode;
  if (mode.w != width || mode.h != height) DesktopMode(data, width, height);
  return true;
}
namespace {
auto PlaceAtOrigin(SDL_Window& window) -> void {
  window.x = window.windowed.x = window.floating.x = 0;
  window.y = window.windowed.y = window.floating.y = 0;
}
// SDL's video callback table supplies borrowed device and window pointers.
auto CreateWindow(SDL_VideoDevice* device, SDL_Window* window,
                  [[maybe_unused]] SDL_PropertiesID unused_properties) -> bool {
  utilities::Expects(device != nullptr, "window creation has a device");
  utilities::Expects(window != nullptr, "window creation has a window");
  return Boundary([&] {
    auto& data = *device->internal;
    if (data.Window()) return SDL_SetError("RDP supports one window");
    auto const aspect = data.Backend().Options().Get(SDL_HINT_RDP_ASPECT);
    SetAspect(data.Backend(), aspect);
    PlaceAtOrigin(*window);
    if (!ResizePicture(data, window->w, window->h)) return false;
    data.Bind(*window);
    PublishAspect(*window, aspect);
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_OCCLUDED, 0, 0);
    return true;
  });
}
// SDL's video callback table supplies borrowed device and window pointers.
auto DestroyWindow(SDL_VideoDevice* device, SDL_Window* window) -> void {
  utilities::Expects(device != nullptr, "window destruction has a device");
  auto& data = *device->internal;
  if (data.Window() && &BoundWindow(data) == window) data.Unbind();
}
// SDL's video callback table supplies borrowed device and window pointers.
auto SetWindowSize(SDL_VideoDevice* device, SDL_Window* window) -> void {
  utilities::Expects(device != nullptr, "resize has a device");
  utilities::Expects(window != nullptr, "resize has a window");
  if (ResizePicture(*device->internal, window->pending.w, window->pending.h))
    SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, window->pending.w, window->pending.h);
  window->last_size_pending = false;
}
// SDL's video callback table requires a show callback even for a headless window.
auto ShowWindow([[maybe_unused]] SDL_VideoDevice* unused_device, [[maybe_unused]] SDL_Window* unused_window) -> void { }
// SDL's fullscreen callback supplies borrowed device, window and display pointers.
auto Fullscreen(SDL_VideoDevice* device, SDL_Window* window, SDL_VideoDisplay* display,
                SDL_FullscreenOp operation) -> SDL_FullscreenResult {
  utilities::Expects(device != nullptr, "fullscreen has a device");
  utilities::Expects(window != nullptr, "fullscreen has a window");
  utilities::Expects(display != nullptr, "fullscreen has a display");
  auto const  leaving = operation == SDL_FULLSCREEN_OP_LEAVE;
  auto const& mode    = window->requested_fullscreen_mode.w ? window->requested_fullscreen_mode : display->desktop_mode;
  auto const  width   = leaving ? window->windowed.w : mode.w;
  auto const  height  = leaving ? window->windowed.h : mode.h;
  if (!ResizePicture(*device->internal, width, height)) return SDL_FULLSCREEN_FAILED;
  if (leaving) SDL_SendWindowEvent(window, SDL_EVENT_WINDOW_RESIZED, width, height);
  return SDL_FULLSCREEN_SUCCEEDED;
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
