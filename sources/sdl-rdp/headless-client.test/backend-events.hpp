#pragma once
#include "certificate-directory.hpp"
#include "client.hpp"
#include "display-client.hpp"
#include "frame-observer.hpp"
#include "logs.hpp"
#include "pattern.hpp"
#include <sdl-rdp/session/handle.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp-abi/sdl-rdp-backend.h>

#include <chrono>
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
  auto Acknowledged() const                                                               -> bool;
  auto Events() const                                                                     -> std::vector<sdlrdp_event>;
  auto EventsUntil(auto predicate, bool include_refresh = true, Client* client = nullptr) -> std::vector<sdlrdp_event> {
    std::vector<sdlrdp_event> result;
    auto                      deadline = Clock::now() + std::chrono::seconds(10);
    do {
      Accumulate(result, include_refresh);
      if (predicate(result) || !Await(client)) break;
    } while (Clock::now() < deadline);
    return result;
  }
  auto Events(unsigned wanted)                                                   -> std::vector<sdlrdp_event>;
  auto Accumulate(std::vector<sdlrdp_event>& result, bool include_refresh) const -> void;
  auto Await(Client* client) const                                               -> bool;
  CertificateDirectory                                    certificates;
  Logs                                                    logs;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend     { nullptr, sdlrdp_close };
};
}
