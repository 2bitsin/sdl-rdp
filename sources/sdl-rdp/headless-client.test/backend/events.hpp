#pragma once
#include "instance.hpp"
#include "logs.hpp"
#include <sdl-rdp/configuration/codec.hpp>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/session/backend.hpp>

#include <oxbox/platform/scratch-area.hpp>
#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <format>
#include <memory>
#include <optional>
#include <ranges>
#include <span>
#include <stdexcept>
#include <variant>
#include <vector>

namespace sdl_rdp::headless_client_test::backend::detail::events {
using sdl_rdp::configuration::Codec;
using sdl_rdp::link::Event;
using Clock = std::chrono::steady_clock;
using sdl_rdp::headless_client_test::client::Client;

template <typename EventTy> auto Holds(Event const& event) -> bool {
  return std::holds_alternative<EventTy>(event);
}
// The event as one alternative; a mismatch throws, which gtest reports as the test's failure and ends the body.
template <typename EventTy> auto As(Event const& event) -> EventTy const& {
  if (!Holds<EventTy>(event))
    throw std::logic_error(std::format("event holds alternative {}, not the one expected", event.index()));
  return std::get<EventTy>(event);
}
// A predicate over events that is false for any other alternative.
template <typename EventTy> auto Where(std::predicate<EventTy const&> auto matches) {
  return [matches](Event const& event) { return Holds<EventTy>(event) && matches(std::get<EventTy>(event)); };
}
template <typename EventTy> auto Contains(std::span<Event const> events) -> bool {
  return std::ranges::any_of(events, Holds<EventTy>);
}
template <typename EventTy> auto Count(std::span<Event const> events) -> std::size_t {
  return static_cast<std::size_t>(std::ranges::count_if(events, Holds<EventTy>));
}
template <typename EventTy> auto FirstEvent(std::span<Event const> events) -> std::optional<EventTy> {
  auto const found = std::ranges::find_if(events, Holds<EventTy>);
  if (found == events.end()) return std::nullopt;
  return std::get<EventTy>(*found);
}
template <typename EventTy> auto EventsOf(std::span<Event const> events) -> std::vector<EventTy> {
  return events | std::views::filter(Holds<EventTy>)
         | std::views::transform([](Event const& event) { return std::get<EventTy>(event); })
         | std::ranges::to<std::vector>();
}
// Both fixtures share the same bounded event accumulation; predicates inspect the
// whole sequence, so an early poll cannot lose half of a transition.
class BackendEvents {
protected:
  auto EventsUntil(auto predicate, bool include_refresh, std::predicate auto await) -> std::vector<Event> {
    std::vector<Event> result;
    auto               deadline = Clock::now() + std::chrono::seconds(10);
    do {
      Accumulate(result, include_refresh);
      if (predicate(result) || !await()) break;
    } while (Clock::now() < deadline);
    return result;
  }
  template <typename EventTy> auto UntilEvent(bool include_refresh = true) -> std::vector<Event> {
    return EventsUntil(Contains<EventTy>, include_refresh, [this] { return AwaitBackend(); });
  }
  template <typename EventTy> auto UntilEvent(Client& client, bool include_refresh = true) -> std::vector<Event> {
    return EventsUntil(Contains<EventTy>, include_refresh, [&client] { return client.Pump(); });
  }
  auto Events(std::size_t wanted)                                         -> std::vector<Event>;
  auto ThenConnectedCodec(Client& client, Codec expected)                 -> void;
  auto Accumulate(std::vector<Event>& result, bool include_refresh) const -> void;
  auto AwaitBackend() const                                               -> bool;
  oxbox::platform::ScratchArea certificates{ "events", "sdl-rdp" };
  Logs                         logs;
  BackendInstance              backend;
};
}

namespace sdl_rdp::headless_client_test::backend {
using detail::events::BackendEvents;
using detail::events::Clock;
using detail::events::Contains;
using detail::events::Count;
using detail::events::As;
using detail::events::EventsOf;
using detail::events::FirstEvent;
using detail::events::Holds;
using detail::events::Where;
}
