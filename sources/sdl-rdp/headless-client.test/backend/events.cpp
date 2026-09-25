#include <sdl-rdp/headless-client.test/backend/events.hpp>

#include <gtest/gtest.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <iterator>

namespace sdl_rdp::headless_client_test::backend::detail::events {
using sdl_rdp::configuration::Codec;
using sdl_rdp::link::Connected;
using sdl_rdp::link::Event;
using sdl_rdp::link::RefreshChanged;

auto BackendEvents::Events(std::size_t wanted) -> std::vector<Event> {
  return EventsUntil([=](auto const& events) { return events.size() >= wanted; }, false,
                     [this] { return AwaitBackend(); });
}
auto BackendEvents::ThenConnectedCodec(Client& client, Codec expected) -> void {
  auto const events    = UntilEvent<Connected>(client);
  auto const connected = FirstEvent<Connected>(events);
  if (!connected) FAIL() << logs.Text();
  EXPECT_EQ(connected->codec, expected);
}
auto BackendEvents::Accumulate(std::vector<Event>& result, bool include_refresh) const -> void {
  std::ranges::copy_if(backend.Poll(), std::back_inserter(result),
                       [=](auto const& event) { return include_refresh || !Holds<RefreshChanged>(event); });
}
auto BackendEvents::AwaitBackend() const -> bool {
  std::ignore = backend.Wait(std::chrono::milliseconds{ 50 });
  return true;
}
}
