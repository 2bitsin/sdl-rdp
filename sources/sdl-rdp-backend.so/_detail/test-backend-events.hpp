#pragma once
#include "client.hpp"
#include "contract.hpp"
#include "display-client.hpp"
#include "frame-observer.hpp"
#include "handle.hpp"
#include "sdl-rdp-backend.h"
#include "test-certificate-directory.hpp"
#include "test-logs.hpp"
#include "test-pattern.hpp"

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
  bool                      Acknowledged() const;
  std::vector<sdlrdp_event> Events() const;
  std::vector<sdlrdp_event> EventsUntil(auto predicate, bool include_refresh = true, Client* client = nullptr) {
    std::vector<sdlrdp_event> result;
    auto                      deadline = Clock::now() + std::chrono::seconds(10);
    do {
      for (auto event : Events())
        if (include_refresh || event.type != SDLRDP_REFRESH) result.push_back(event);
      if (predicate(result)) break;
      if (client) {
        if (!client->Pump()) break;
      } else
        sdlrdp_wait(backend.get(), 50);
    } while (Clock::now() < deadline);
    return result;
  }
  std::vector<sdlrdp_event> Events(unsigned wanted);
  CertificateDirectory                                    certificates;
  Logs                                                    logs;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend     { nullptr, sdlrdp_close };
};
}
