#pragma once

namespace Backend {
class Tap {
public:
       Tap(int index, double ratio, unsigned extent);
  auto First() const noexcept  -> unsigned;
  auto Second() const noexcept -> unsigned;
  auto Weight() const noexcept -> float;

private:
  unsigned _first;
  unsigned _second;
  float    _weight;
};
}
