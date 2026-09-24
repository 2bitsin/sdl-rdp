#pragma once
#include "SDL_rdpdriver.hpp"
#include <span>
#include <vector>
namespace rdp {
class Framebuffer {
public:
  explicit Framebuffer(Surface surface) noexcept;
  auto     Present(Driver const& driver, std::span<SDL_Rect const> rects) -> bool;
private:
  auto _Damage(std::span<SDL_Rect const> rects) -> std::span<sdlrdp_rect const>;
  Surface                  _surface;
  std::vector<sdlrdp_rect> _damage;
};
}
