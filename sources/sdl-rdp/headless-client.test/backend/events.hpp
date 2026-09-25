#pragma once
#include "certificate-directory.hpp"
#include "instance.hpp"
#include "logs.hpp"
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/client/client.hpp>
#include <sdl-rdp/headless-client.test/client/display.hpp>
#include <sdl-rdp/headless-client.test/frame/observer.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp/utilities/contract.hpp>

#include <chrono>
#include <concepts>
#include <cstddef>
#include <memory>
#include <vector>

namespace BackendGate {
using Clock = std::chrono::steady_clock;
using Headless::Client;
using Headless::DisplayClient;
using Headless::FrameObserver;
using Headless::GraphicsScene;
using Headless::Logs;
using utilities::Expects;
using utilities::Required;
// Both fixtures share the same bounded event accumulation; predicates inspect the
// whole sequence, so an early poll cannot lose half of a transition.
class BackendEvents {
protected:
  auto EventsUntil(auto predicate, bool include_refresh, std::predicate auto await) -> std::vector<sdlrdp_event> {
    std::vector<sdlrdp_event> result;
    auto                      deadline = Clock::now() + std::chrono::seconds(10);
    do {
      Accumulate(result, include_refresh);
      if (predicate(result) || !await()) break;
    } while (Clock::now() < deadline);
    return result;
  }
  auto Events(std::size_t wanted)                                                -> std::vector<sdlrdp_event>;
  auto UntilEvent(sdlrdp_event_type type, bool include_refresh = true)           -> std::vector<sdlrdp_event>;
  auto UntilEvent(Client const& client, sdlrdp_event_type type, bool include_refresh = true)
      -> std::vector<sdlrdp_event>;
  auto ThenConnectedCodec(Client const& client, sdlrdp_codec expected)           -> void;
  auto Accumulate(std::vector<sdlrdp_event>& result, bool include_refresh) const -> void;
  auto AwaitBackend() const                                                      -> bool;
  CertificateDirectory      certificates;
  Logs                      logs;
  Headless::BackendInstance backend;
};
}
