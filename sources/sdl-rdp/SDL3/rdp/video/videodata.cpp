#include "videodata.hpp"
#include "device.hpp"
#include <sdl-rdp/SDL3/rdp/exceptions.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/boundary.hpp>
#include <sdl-rdp/SDL3/rdp/settings/options.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/session/backend.hpp>

namespace sdl3::rdp::video::detail::videodata {
using sdl3::rdp::sdl::Boundary;
using sdl3::rdp::settings::Text;
using sdl_rdp::configuration::Codec;
using sdl_rdp::settings::Aspect;
using sdl_rdp::utilities::Expects;
namespace {
auto ApplyCodec(SDL_VideoData& data, Codec value) -> void {
  data.Driver().Backend().Presentation().SetCodec(value);
}
auto ApplyAspect(SDL_VideoData& data, Aspect const& value) -> void {
  SetAspect(data.Driver(), value);
  if (auto const window = data.Window()) PublishAspect(*window, value);
}
// SDL hint observers receive an opaque context and nullable C strings.
template <auto FIELD, auto APPLY>
auto SDLCALL HintChanged(void* context, [[maybe_unused]] char const* name, char const* old_value, char const* new_value)
    -> void {
  Expects(context != nullptr, "hint observer has video state");
  auto& data = *static_cast<SDL_VideoData*>(context);
  Boundary([&] { APPLY(data, data.Driver().Options().Changed<FIELD>(Text(old_value), Text(new_value))); });
}
}
}

using sdl3::rdp::sdl::Surface;
using sdl3::rdp::video::detail::videodata::ApplyAspect;
using sdl3::rdp::video::detail::videodata::ApplyCodec;
using sdl3::rdp::video::detail::videodata::HintChanged;
using sdl_rdp::settings::Settings;

SDL_VideoData::SDL_VideoData(std::shared_ptr<sdl3::rdp::Driver> driver)
    : _driver{ std::move(driver) }, _codec{ SDL_HINT_RDP_CODEC, HintChanged<&Settings::codec, ApplyCodec>, this },
      _aspect{ SDL_HINT_RDP_ASPECT, HintChanged<&Settings::aspect, ApplyAspect>, this } { }
auto SDL_VideoData::Driver() noexcept -> sdl3::rdp::Driver& {
  return *_driver;
}
auto SDL_VideoData::PollEvents() -> std::span<sdl_rdp::link::Event const> {
  Driver().Backend().Events().Poll(_polled);
  return _polled;
}
auto SDL_VideoData::Display() const -> SDL_DisplayID {
  return _display;
}
auto SDL_VideoData::Display(SDL_DisplayID display) -> void {
  _authentication.reset();
  _display = display;
  if (display) _authentication.emplace(Driver().Credentials(), SDL_GetDisplayProperties(display));
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
auto SDL_VideoData::Attach(Surface surface) noexcept -> void {
  _framebuffer.emplace(std::move(surface));
}
auto SDL_VideoData::Detach() noexcept -> void {
  _framebuffer.reset();
}
namespace sdl3::rdp::video::detail::videodata {
using sdl_rdp::utilities::Expects;

auto BoundWindow(SDL_VideoData const& data) -> SDL_Window& {
  auto const window = data.Window();
  if (!window) throw NoWindow{ };
  return window->get();
}
auto CurrentVideo() -> SDL_VideoData& {
  auto* const device = SDL_GetVideoDevice();
  Expects(device != nullptr, "context-free video callbacks run while video is initialised");
  Expects(device->internal != nullptr, "the RDP video device has its state");
  return *device->internal;
}
}
