#pragma once
#include <cstdint>

namespace sdl_rdp::video::detail::tap {
class Tap {
public:
       Tap(int index, double ratio, std::uint32_t extent);
  auto First() const noexcept  -> std::uint32_t;
  auto Second() const noexcept -> std::uint32_t;
  auto Weight() const noexcept -> float;

private:
  std::uint32_t _first;
  std::uint32_t _second;
  float         _weight;
};
}

namespace sdl_rdp::video {
using detail::tap::Tap;
}
