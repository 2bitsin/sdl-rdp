#include <sdl-rdp/video/tap.hpp>

#include <sdl-rdp/utilities/contract.hpp>

#include <algorithm>
#include <cstdint>

namespace sdl_rdp::video::detail::tap {
using sdl_rdp::utilities::Expects;

namespace {
constexpr double PixelCenter = 0.5;
auto Position(int index, double ratio, std::uint32_t extent) -> double {
  Expects(extent > 0, "sampled extent is positive");
  return std::clamp(((index + PixelCenter) * ratio) - PixelCenter, 0.0, static_cast<double>(extent - 1));
}
}
// The position is clamped to 0..extent - 1, so truncation takes its floor inside the extent.
Tap::Tap(int index, double ratio, std::uint32_t extent)
    : _first{ static_cast<std::uint32_t>(Position(index, ratio, extent)) }, _second{ std::min(_first + 1, extent - 1) },
      _weight{ static_cast<float>(Position(index, ratio, extent) - _first) } { }
auto Tap::First() const noexcept -> std::uint32_t {
  return _first;
}
auto Tap::Second() const noexcept -> std::uint32_t {
  return _second;
}
auto Tap::Weight() const noexcept -> float {
  return _weight;
}
}
