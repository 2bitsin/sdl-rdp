#pragma once
#include <chrono>
#include <concepts>
#include <functional>
#include <utility>

namespace sdl_rdp::utilities::detail::stopwatch {
class Stopwatch {
public:
  using Clock = std::chrono::steady_clock;
  auto Elapsed() const noexcept -> Clock::duration;
  auto Lap() noexcept           -> Clock::duration;

private:
  Clock::time_point _start{ Clock::now() };
};
template <std::invocable StepTy>
auto Timed(StepTy&& step) -> Stopwatch::Clock::duration {
  Stopwatch const watch;
  std::invoke(std::forward<StepTy>(step));
  return watch.Elapsed();
}
}

namespace sdl_rdp::utilities {
using detail::stopwatch::Stopwatch;
using detail::stopwatch::Timed;
}
