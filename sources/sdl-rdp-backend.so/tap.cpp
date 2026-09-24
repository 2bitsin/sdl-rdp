#include "_detail/tap.hpp"

#include "_detail/contract.hpp"

#include <algorithm>

namespace Backend {
namespace {
constexpr double PixelCenter = 0.5;
auto Position(int index, double ratio, unsigned extent) -> double {
  Expects(extent > 0, "sampled extent is positive");
  return std::clamp(((index + PixelCenter) * ratio) - PixelCenter, 0.0, double(extent - 1));
}
}
Tap::Tap(int index, double ratio, unsigned extent)
    : _first{ unsigned(Position(index, ratio, extent)) }, _second{ std::min(_first + 1, extent - 1) },
      _weight{ float(Position(index, ratio, extent) - _first) } { }
auto Tap::First() const noexcept -> unsigned {
  return _first;
}
auto Tap::Second() const noexcept -> unsigned {
  return _second;
}
auto Tap::Weight() const noexcept -> float {
  return _weight;
}
}
