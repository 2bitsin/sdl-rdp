#pragma once
#include "framebuffer.hpp"
#include <sdl-rdp/SDL3/rdp/owneddriver.hpp>
#include <sdl-rdp/SDL3/rdp/storage/drive.hpp>
// SDL declares this tag as a struct; the members stay private.
struct SDL_VideoData : private sdl3::rdp::OwnedDriver<sdl3::rdp::Driver> {
public:
  using FramebufferRef = std::optional<std::reference_wrapper<sdl3::rdp::video::Framebuffer>>;
  using sdl3::rdp::OwnedDriver<sdl3::rdp::Driver>::Backend;
       SDL_VideoData(std::shared_ptr<sdl3::rdp::Driver> driver, SDL_HintCallback codec, SDL_HintCallback aspect);
  auto Display() const                                      -> SDL_DisplayID;
  auto Display(SDL_DisplayID display)                       -> void;
  auto AttachTouch(SDL_TouchID touch)                       -> void;
  auto DetachTouch()                                        -> void;
  auto Window() const                                       -> std::optional<std::reference_wrapper<SDL_Window>>;
  auto Bind(SDL_Window& window)                             -> void;
  auto Unbind()                                             -> void;
  auto Picture() const                                      -> std::pair<int, int>;
  auto Picture(int width, int height)                       -> void;
  auto RefreshMode(SDL_DisplayMode const& current)          -> SDL_DisplayMode&;
  auto Framebuffer() noexcept                               -> FramebufferRef;
  auto Attach(sdl3::rdp::backend::Surface surface) noexcept -> void;
  auto Detach() noexcept                                    -> void;
private:
  SDL_DisplayID                                        _display       { };
  std::optional<sdl3::rdp::AuthenticationDisplay>      _authentication;
  std::optional<std::reference_wrapper<SDL_Window>>    _window;
  std::optional<sdl3::rdp::backend::TouchRegistration> _touch;
  std::pair<int, int>                                  _picture;
  std::array<SDL_DisplayMode, 2>                       _refresh_modes { };
  std::optional<sdl3::rdp::video::Framebuffer>         _framebuffer;
  sdl3::rdp::backend::HintObserver const               _codec;
  sdl3::rdp::backend::HintObserver const               _aspect;
};
namespace sdl3::rdp::video::detail::videodata {
auto BoundWindow(SDL_VideoData const& data) -> SDL_Window&;
auto CurrentVideo()                         -> SDL_VideoData&;
}
namespace sdl3::rdp::video {
using detail::videodata::BoundWindow;
using detail::videodata::CurrentVideo;
}
