#include <sdl-rdp/utilities/deadline.hpp>
#include <sdl-rdp/utilities/contract.hpp>

namespace Backend {
auto DeadlineAfter(std::chrono::milliseconds timeout) -> Deadline {
  utilities::Expects(timeout >= std::chrono::milliseconds::zero(), "a deadline lies in the future");
  return std::chrono::steady_clock::now() + timeout;
}
// The C ABI spells an unbounded wait as a negative timeout.
auto AbiDeadline(int timeout_ms) -> Deadline {
  return timeout_ms < 0 ? Deadline::max() : DeadlineAfter(std::chrono::milliseconds(timeout_ms));
}
}
