#include "videodata.hpp"
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
SDL_VideoData::SDL_VideoData(std::shared_ptr<sdl3::rdp::Driver> driver, SDL_HintCallback codec, SDL_HintCallback aspect)
    : sdl3::rdp::OwnedDriver<sdl3::rdp::Driver>{ std::move(driver) }, _codec{ SDL_HINT_RDP_CODEC, codec, this },
      _aspect{ SDL_HINT_RDP_ASPECT, aspect, this } { }
auto SDL_VideoData::Display() const -> SDL_DisplayID {
  return _display;
}
auto SDL_VideoData::Display(SDL_DisplayID display) -> void {
  _authentication.reset();
  _display = display;
  if (display) _authentication.emplace(Backend(), SDL_GetDisplayProperties(display));
}
auto SDL_VideoData::AttachTouch(SDL_TouchID touch) -> void {
  _touch.emplace(touch);
}
auto SDL_VideoData::DetachTouch() -> void {
  _touch.reset();
}
auto SDL_VideoData::Window() const -> std::optional<std::reference_wrapper<SDL_Window>> {
  return _window;
}
auto SDL_VideoData::Bind(SDL_Window& window) -> void {
  _window = window;
}
auto SDL_VideoData::Unbind() -> void {
  _window.reset();
}
auto SDL_VideoData::Picture() const -> std::pair<int, int> {
  return _picture;
}
auto SDL_VideoData::Picture(int width, int height) -> void {
  _picture = { width, height };
}
// SDL keeps a pointer to the current mode, so the new mode is written to the slot it does not point at.
auto SDL_VideoData::RefreshMode(SDL_DisplayMode const& current) -> SDL_DisplayMode& {
  auto& mode = &current == &_refresh_modes.front() ? _refresh_modes.back() : _refresh_modes.front();
  mode = current;
  return mode;
}
auto SDL_VideoData::Framebuffer() noexcept -> FramebufferRef {
  if (!_framebuffer) return std::nullopt;
  return std::ref(*_framebuffer);
}
auto SDL_VideoData::Attach(sdl3::rdp::backend::Surface surface) noexcept -> void {
  _framebuffer.emplace(std::move(surface));
}
auto SDL_VideoData::Detach() noexcept -> void {
  _framebuffer.reset();
}
namespace sdl3::rdp::video::detail::videodata {
auto BoundWindow(SDL_VideoData const& data) -> SDL_Window& {
  auto const window = data.Window();
  if (!window) throw NoWindow{ };
  return window->get();
}
auto CurrentVideo() -> SDL_VideoData& {
  utilities::Expects(SDL_GetVideoDevice() != nullptr, "context-free video callbacks run while video is initialised");
  auto* const device = SDL_GetVideoDevice();
  utilities::Expects(device->internal != nullptr, "the RDP video device has its state");
  return *device->internal;
}
}
