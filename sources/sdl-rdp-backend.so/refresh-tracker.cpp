#include "_detail/refresh-tracker.hpp"

#include <utility>

namespace Backend {
auto RefreshTracker::Effective() const noexcept -> unsigned {
  return _effective.load();
}
auto RefreshTracker::Mode() const noexcept -> RefreshMode {
  return _refresh.Mode();
}
auto RefreshTracker::AwaitingEmpty() const noexcept -> bool {
  return _refresh.AwaitingEmpty();
}
auto RefreshTracker::TestAndSetUnavailableLogged() noexcept -> bool {
  return std::exchange(_unavailable_logged, true);
}
}
