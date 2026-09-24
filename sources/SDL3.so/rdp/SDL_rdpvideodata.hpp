#pragma once
#include "SDL_rdpdrive.hpp"
// SDL declares this tag as a struct; the members stay private.
struct SDL_VideoData {
public:
       SDL_VideoData(std::shared_ptr<rdp::Driver> driver, SDL_HintCallback codec, SDL_HintCallback aspect);
  auto Backend() const                             -> rdp::Driver const&;
  auto Backend()                                   -> rdp::Driver&;
  auto Display() const                             -> SDL_DisplayID;
  void Display(SDL_DisplayID display);
  void AttachTouch(SDL_TouchID touch);
  void DetachTouch();
  auto Window() const                              -> std::optional<std::reference_wrapper<SDL_Window>>;
  void Bind(SDL_Window& window);
  void Unbind();
  auto Picture() const                             -> std::pair<int, int>;
  void Picture(int width, int height);
  auto RefreshMode(SDL_DisplayMode const& current) -> SDL_DisplayMode&;
  auto Framebuffer() const                         -> std::optional<std::reference_wrapper<SDL_Surface const>>;
  void Framebuffer(std::optional<rdp::Surface> surface);
private:
  std::shared_ptr<rdp::Driver>                      _driver;
  SDL_DisplayID                                     _display       { };
  std::optional<rdp::AuthenticationDisplay>         _authentication;
  std::optional<std::reference_wrapper<SDL_Window>> _window;
  std::optional<rdp::TouchRegistration>             _touch;
  std::pair<int, int>                               _picture;
  std::array<SDL_DisplayMode, 2>                    _refresh_modes { };
  std::optional<rdp::Surface>                       _framebuffer;
  rdp::HintObserver const                           _codec;
  rdp::HintObserver const                           _aspect;
};
namespace rdp {
auto BoundWindow(SDL_VideoData const& data) -> SDL_Window&;
auto CurrentVideo()                         -> SDL_VideoData&;
}
