#pragma once

namespace Backend {
class Tap {
public:
           Tap(int index, double ratio, unsigned extent);
  unsigned First() const  noexcept;
  unsigned Second() const noexcept;
  float    Weight() const noexcept;

private:
  unsigned _first;
  unsigned _second;
  float    _weight;
};
}
