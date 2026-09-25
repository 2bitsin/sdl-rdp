#pragma once
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <span>
#include <vector>
namespace sdl3::rdp::video::detail::framebuffer {
using sdl3::rdp::backend::Surface;

class Framebuffer {
public:
  explicit Framebuffer(Surface surface) noexcept;
  auto     Present(Driver const& driver, std::span<SDL_Rect const> rects) -> bool;
private:
  auto Damage(std::span<SDL_Rect const> rects) -> std::span<sdlrdp_rect const>;
  Surface                  _surface;
  std::vector<sdlrdp_rect> _damage;
};
auto InitFramebuffer(SDL_VideoDevice& device) -> void;
}

namespace sdl3::rdp::video {
using detail::framebuffer::Framebuffer;
using detail::framebuffer::InitFramebuffer;
}
