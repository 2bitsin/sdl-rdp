#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/abi/backend.h>

#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <iterator>

namespace BackendGate {
namespace {
auto Contains(sdlrdp_event_type type) {
  return [type](std::vector<sdlrdp_event> const& events) {
    return std::ranges::contains(events, type, &sdlrdp_event::type);
  };
}
}
auto BackendEvents::Events(std::size_t wanted) -> std::vector<sdlrdp_event> {
  return EventsUntil([=](auto const& events) { return events.size() >= wanted; }, false,
                     [this] { return AwaitBackend(); });
}
auto BackendEvents::UntilEvent(sdlrdp_event_type type, bool include_refresh) -> std::vector<sdlrdp_event> {
  return EventsUntil(Contains(type), include_refresh, [this] { return AwaitBackend(); });
}
auto BackendEvents::UntilEvent(Client const& client, sdlrdp_event_type type, bool include_refresh)
    -> std::vector<sdlrdp_event> {
  return EventsUntil(Contains(type), include_refresh, [&client] { return client.Pump(); });
}
auto BackendEvents::ThenConnectedCodec(Client const& client, sdlrdp_codec expected) -> void {
  auto const events    = UntilEvent(client, SDLRDP_CONNECTED);
  auto const connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end()) << logs.Text();
  EXPECT_EQ(connected->connected.codec, expected);
}
auto BackendEvents::Accumulate(std::vector<sdlrdp_event>& result, bool include_refresh) const -> void {
  std::ranges::copy_if(backend.Poll(), std::back_inserter(result),
                       [=](auto const& event) { return include_refresh || event.type != SDLRDP_REFRESH; });
}
auto BackendEvents::AwaitBackend() const -> bool {
  sdlrdp_wait(backend.Handle(), 50);
  return true;
}
}
