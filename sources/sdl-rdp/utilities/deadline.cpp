#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/contract.hpp>

namespace sdl_rdp::utilities::detail::deadline {
auto DeadlineAfter(std::chrono::milliseconds timeout) -> Deadline {
  Expects(timeout >= std::chrono::milliseconds::zero(), "a deadline lies in the future");
  return std::chrono::steady_clock::now() + timeout;
}
auto DeadlineWithin(std::chrono::nanoseconds timeout) -> Deadline {
  if (timeout < std::chrono::nanoseconds::zero()) return Deadline::max();
  auto const now       = std::chrono::steady_clock::now();
  auto const remaining = Deadline::max() - now - std::chrono::milliseconds{ 1 };
  if (timeout >= remaining) return Deadline::max();
  return now + std::chrono::ceil<std::chrono::milliseconds>(timeout);
}
}
