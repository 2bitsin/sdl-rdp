#pragma once
#include "SDL_rdpdrive.hpp"
#include "SDL_rdpframebuffer.hpp"
// SDL declares this tag as a struct; the members stay private.
struct SDL_VideoData {
public:
       SDL_VideoData(std::shared_ptr<rdp::Driver> driver, SDL_HintCallback codec, SDL_HintCallback aspect);
  auto Backend() const                             -> rdp::Driver const&;
  auto Display() const                             -> SDL_DisplayID;
  auto Display(SDL_DisplayID display)              -> void;
  auto AttachTouch(SDL_TouchID touch)              -> void;
  auto DetachTouch()                               -> void;
  auto Window() const                              -> std::optional<std::reference_wrapper<SDL_Window>>;
  auto Bind(SDL_Window& window)                    -> void;
  auto Unbind()                                    -> void;
  auto Picture() const                             -> std::pair<int, int>;
  auto Picture(int width, int height)              -> void;
  auto RefreshMode(SDL_DisplayMode const& current) -> SDL_DisplayMode&;
  auto Framebuffer() noexcept                      -> std::optional<std::reference_wrapper<rdp::Framebuffer>>;
  auto Attach(rdp::Surface surface) noexcept       -> void;
  auto Detach() noexcept                           -> void;
private:
  std::shared_ptr<rdp::Driver>                      _driver;
  SDL_DisplayID                                     _display       { };
  std::optional<rdp::AuthenticationDisplay>         _authentication;
  std::optional<std::reference_wrapper<SDL_Window>> _window;
  std::optional<rdp::TouchRegistration>             _touch;
  std::pair<int, int>                               _picture;
  std::array<SDL_DisplayMode, 2>                    _refresh_modes { };
  std::optional<rdp::Framebuffer>                   _framebuffer;
  rdp::HintObserver const                           _codec;
  rdp::HintObserver const                           _aspect;
};
namespace rdp {
auto BoundWindow(SDL_VideoData const& data) -> SDL_Window&;
auto CurrentVideo()                         -> SDL_VideoData&;
}
