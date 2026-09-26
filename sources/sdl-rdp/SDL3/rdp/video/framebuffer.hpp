#pragma once
#include <sdl-rdp/SDL3/rdp/driver.hpp>
#include <sdl-rdp/SDL3/rdp/sdl/resources.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <cstddef>
#include <span>
#include <vector>
namespace sdl3::rdp::video::detail::framebuffer {
using sdl3::rdp::sdl::Surface;
using sdl_rdp::utilities::Rect;

class Framebuffer {
public:
  explicit Framebuffer(Surface surface) noexcept;
  auto     Present(Driver& driver, std::span<SDL_Rect const> rects) -> void;
private:
  auto Damage(std::span<SDL_Rect const> rects) -> std::span<Rect const>;
  Surface           _surface;
  std::vector<Rect> _damage;
};
auto InitFramebuffer(SDL_VideoDevice& device) -> void;
}

namespace sdl3::rdp::video {
using detail::framebuffer::Framebuffer;
using detail::framebuffer::InitFramebuffer;
}
