#include "_detail/test-backend-events.hpp"

#include "_detail/test-peer-status.hpp"

#include <algorithm>
#include <array>
#include <iterator>

namespace BackendGate {
bool BackendEvents::Acknowledged() const {
  Expects(backend != nullptr, "backend exists");
  auto const status = CurrentStatus(*backend);
  return status && status->acknowledged >= Presented(*backend);
}
std::vector<sdlrdp_event> BackendEvents::Events() const {
  std::array<sdlrdp_event, 256> batch { };
  auto                          count = sdlrdp_poll(backend.get(), batch.data(), batch.size());
  return { batch.begin(), batch.begin() + count };
}
std::vector<sdlrdp_event> BackendEvents::Events(unsigned wanted) {
  return EventsUntil([=](auto const& events) { return events.size() >= wanted; }, false);
}
auto BackendEvents::Accumulate(std::vector<sdlrdp_event>& result, bool include_refresh) const -> void {
  std::ranges::copy_if(Events(), std::back_inserter(result),
                       [=](auto const& event) { return include_refresh || event.type != SDLRDP_REFRESH; });
}
auto BackendEvents::Await(Client* client) const -> bool {
  if (client) return client->Pump();
  sdlrdp_wait(backend.get(), 50);
  return true;
}
}
