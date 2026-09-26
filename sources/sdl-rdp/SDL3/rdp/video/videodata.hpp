#pragma once
#include "framebuffer.hpp"
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/resources.hpp>
#include <sdl-rdp/SDL3/rdp/storage/drive.hpp>
#include <sdl-rdp/link/event.hpp>
#include <memory>
#include <span>
#include <vector>
// SDL declares this tag as a struct; the members stay private.
struct SDL_VideoData {
public:
  using FramebufferRef = std::optional<std::reference_wrapper<sdl3::rdp::video::Framebuffer>>;
  explicit SDL_VideoData(std::shared_ptr<sdl3::rdp::Driver> driver);
  auto     Driver() noexcept                                -> sdl3::rdp::Driver&;
  auto     Display() const                                  -> SDL_DisplayID;
  auto     Display(SDL_DisplayID display)                   -> void;
  auto     AttachTouch(SDL_TouchID touch)                   -> void;
  auto     DetachTouch()                                    -> void;
  auto     Window() const                                   -> std::optional<std::reference_wrapper<SDL_Window>>;
  auto     Bind(SDL_Window& window)                         -> void;
  auto     Unbind()                                         -> void;
  auto     Picture() const                                  -> std::pair<int, int>;
  auto     Picture(int width, int height)                   -> void;
  auto     RefreshMode(SDL_DisplayMode const& current)      -> SDL_DisplayMode&;
  auto     Framebuffer() noexcept                           -> FramebufferRef;
  auto     Attach(sdl3::rdp::sdl::Surface surface) noexcept -> void;
  auto     Detach() noexcept                                -> void;
  auto     PollEvents()                                     -> std::span<sdl_rdp::link::Event const>;
private:
  std::shared_ptr<sdl3::rdp::Driver>                _driver;
  SDL_DisplayID                                     _display       { };
  std::optional<sdl3::rdp::AuthenticationDisplay>   _authentication;
  std::optional<std::reference_wrapper<SDL_Window>> _window;
  std::optional<sdl3::rdp::sdl::TouchRegistration>  _touch;
  std::pair<int, int>                               _picture;
  std::array<SDL_DisplayMode, 2>                    _refresh_modes { };
  std::optional<sdl3::rdp::video::Framebuffer>      _framebuffer;
  sdl3::rdp::sdl::HintObserver const                _codec;
  sdl3::rdp::sdl::HintObserver const                _aspect;
  std::vector<sdl_rdp::link::Event>                 _polled;
};
namespace sdl3::rdp::video::detail::videodata {
auto BoundWindow(SDL_VideoData const& data) -> SDL_Window&;
auto CurrentVideo()                         -> SDL_VideoData&;
}

namespace sdl3::rdp::video {
using detail::videodata::BoundWindow;
using detail::videodata::CurrentVideo;
}
