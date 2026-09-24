#include <sdl-rdp/headless-client.test/lease-count.hpp>

#include <sdl-rdp/utilities/contract.hpp>

namespace Headless {
auto LeaseCount::Acquire() -> void {
  std::scoped_lock const lock(_guard);
  ++_leases;
}
// The notify stays under the lock so Drain cannot return, and its owner free this count, before it completes.
auto LeaseCount::Release() noexcept -> void {
  std::scoped_lock const lock(_guard);
  utilities::Expects(_leases > 0, "a lease is outstanding");
  --_leases;
  _released.notify_all();
}
auto LeaseCount::Drain() -> void {
  std::unique_lock lock(_guard);
  _released.wait(lock, [this] { return _leases == 0; });
}
}
