#include <sdl-rdp/headless-client.test/backend-events.hpp>

#include <sdl-rdp/headless-client.test/peer-status.hpp>

#include <algorithm>
#include <array>
#include <iterator>

namespace BackendGate {
auto BackendEvents::Acknowledged() const -> bool {
  Expects(backend != nullptr, "backend exists");
  auto const status = CurrentStatus(*backend);
  return status && status->acknowledged >= Presented(*backend);
}
auto BackendEvents::Events() const -> std::vector<sdlrdp_event> {
  std::array<sdlrdp_event, 256> batch { };
  auto                          count = sdlrdp_poll(backend.get(), batch.data(), batch.size());
  return { batch.begin(), batch.begin() + count };
}
auto BackendEvents::Events(unsigned wanted) -> std::vector<sdlrdp_event> {
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
