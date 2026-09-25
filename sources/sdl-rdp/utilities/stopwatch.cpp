#include <sdl-rdp/utilities/stopwatch.hpp>

#include <utility>

namespace sdl_rdp::utilities::detail::stopwatch {
auto Stopwatch::Elapsed() const noexcept -> Clock::duration {
  return Clock::now() - _start;
}
auto Stopwatch::Lap() noexcept -> Clock::duration {
  auto const now = Clock::now();
  return now - std::exchange(_start, now);
}
}
