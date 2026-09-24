#include "_detail/refresh-tracker.hpp"

#include <utility>

namespace Backend {
unsigned RefreshTracker::Effective() const noexcept {
  return _effective.load();
}
RefreshMode RefreshTracker::Mode() const noexcept {
  return _refresh.Mode();
}
bool RefreshTracker::AwaitingEmpty() const noexcept {
  return _refresh.AwaitingEmpty();
}
bool RefreshTracker::TestAndSetUnavailableLogged() noexcept {
  return std::exchange(_unavailable_logged, true);
}
}
