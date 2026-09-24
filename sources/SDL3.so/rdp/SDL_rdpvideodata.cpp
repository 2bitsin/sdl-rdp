#include "SDL_rdpvideodata.hpp"
SDL_VideoData::SDL_VideoData(std::shared_ptr<rdp::Driver> driver, SDL_HintCallback codec, SDL_HintCallback aspect)
    : _driver{ std::move(driver)                 }
    , _codec { SDL_HINT_RDP_CODEC, codec, this   }
    , _aspect{ SDL_HINT_RDP_ASPECT, aspect, this } { }
auto SDL_VideoData::Backend() const -> rdp::Driver const& { return *_driver; }
auto SDL_VideoData::Backend() -> rdp::Driver& { return *_driver; }
auto SDL_VideoData::Display() const -> SDL_DisplayID { return _display; }
void SDL_VideoData::Display(SDL_DisplayID display) {
  _authentication.reset();
  _display = display;
  if (display) _authentication.emplace(*_driver, SDL_GetDisplayProperties(display));
}
void SDL_VideoData::AttachTouch(SDL_TouchID touch) { _touch.emplace(touch); }
void SDL_VideoData::DetachTouch() { _touch.reset(); }
auto SDL_VideoData::Window() const -> std::optional<std::reference_wrapper<SDL_Window>> { return _window; }
void SDL_VideoData::Bind(SDL_Window& window) { _window = window; }
void SDL_VideoData::Unbind() { _window.reset(); }
auto SDL_VideoData::Picture() const -> std::pair<int, int> { return _picture; }
void SDL_VideoData::Picture(int width, int height) { _picture = {width, height}; }
// SDL keeps a pointer to the current mode, so the new mode is written to the slot it does not point at.
auto SDL_VideoData::RefreshMode(SDL_DisplayMode const& current) -> SDL_DisplayMode& {
  auto& mode = &current == &_refresh_modes.front() ? _refresh_modes.back() : _refresh_modes.front();
  mode = current;
  return mode;
}
auto SDL_VideoData::Framebuffer() const -> std::optional<std::reference_wrapper<SDL_Surface const>> {
  if (!_framebuffer) return std::nullopt;
  return std::cref(*_framebuffer->Get());
}
void SDL_VideoData::Framebuffer(std::optional<rdp::Surface> surface) { _framebuffer = std::move(surface); }
namespace rdp {
auto BoundWindow(SDL_VideoData const& data) -> SDL_Window& {
  auto const window = data.Window();
  if (!window) throw std::logic_error("RDP video has no window");
  return window->get();
}
auto CurrentVideo() -> SDL_VideoData& {
  utilities::Expects(SDL_GetVideoDevice() != nullptr, "context-free video callbacks run while video is initialised");
  auto* const device = SDL_GetVideoDevice();
  utilities::Expects(device->internal != nullptr, "the RDP video device has its state");
  return *device->internal;
}
}
