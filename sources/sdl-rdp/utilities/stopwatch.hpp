#pragma once
#include <chrono>

namespace Backend {
class Stopwatch {
public:
  using Clock = std::chrono::steady_clock;
  auto Elapsed() const noexcept -> Clock::duration;
  auto Lap() noexcept           -> Clock::duration;

private:
  Clock::time_point _start{ Clock::now() };
};
}
