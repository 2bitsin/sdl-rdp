#include "_detail/tap.hpp"

#include "_detail/contract.hpp"

#include <algorithm>

namespace Backend {
namespace {
constexpr double PixelCenter = 0.5;
double Position(int index, double ratio, unsigned extent) {
  Expects(extent > 0, "sampled extent is positive");
  return std::clamp(((index + PixelCenter) * ratio) - PixelCenter, 0.0, double(extent - 1));
}
}
Tap::Tap(int index, double ratio, unsigned extent)
    : _first { unsigned(Position(index, ratio, extent)) }, _second{ std::min(_first + 1, extent - 1) },
      _weight{ float(Position(index, ratio, extent) - _first) } { }
unsigned Tap::First() const noexcept {
  return _first;
}
unsigned Tap::Second() const noexcept {
  return _second;
}
float Tap::Weight() const noexcept {
  return _weight;
}
}
