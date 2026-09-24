#pragma once
#include "contract.hpp"
#include "copy-rows.hpp"
#include "encoder.hpp"
#include "headless-client.hpp"
#include "headless-gfx.hpp"
#include "rect.hpp"
#include "sdl-rdp-backend.h"
#include "handle.hpp"
#include "peer.hpp"
#include "test-io.hpp"
#include "test-logs.hpp"
#include "test-pattern.hpp"
#include "test-peer-status.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <oxbox/utilities/number-text.hpp>
#include <oxbox/utilities/text.hpp>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <format>
#include <freerdp/addin.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/client/disp.h>
#include <freerdp/event.h>
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <future>
#include <gtest/gtest.h>
#include <memory>
#include <mutex>
#include <optional>
#include <openssl/pem.h>
#include <openssl/x509v3.h>
#include <random>
#include <regex>
#include <span>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <utility>
#include <vector>
#include <winpr/synch.h>
#include <winpr/wlog.h>

namespace BackendGate {
using Clock = std::chrono::steady_clock;
using Headless::Client;
using Headless::DisplayClient;
using Headless::GraphicsScene;
using utilities::Expects;
using utilities::Required;
struct CertificateDirectory {
public:
  CertificateDirectory(CertificateDirectory const&) = delete;
  CertificateDirectory(CertificateDirectory&&)      = delete;
  CertificateDirectory() {
    std::array<char, 40> pattern{ };
    std::ranges::copy(std::string("/tmp/sdlrdp-gate-XXXXXX"), pattern.begin());
    auto* result = mkdtemp(pattern.data());
    Expects(result != nullptr, "temporary directory created");
    path = result;
  }
                               ~CertificateDirectory() { std::filesystem::remove_all(path); }
  CertificateDirectory&        operator = (CertificateDirectory const&) = delete;
  CertificateDirectory&        operator = (CertificateDirectory&&)      = delete;
  std::filesystem::path const& Path() const { return path; }

private:
  std::filesystem::path path;
};
inline std::size_t ResidentBytes() {
  auto const statm = Headless::ReadText("/proc/self/statm");
  auto const pages = Required(oxbox::utilities::ParseNumbers<std::size_t, 7>(oxbox::utilities::Trimmed(statm), ' '),
                              "statm holds seven page counts");
  return pages[1] * std::size_t(sysconf(_SC_PAGESIZE));
}
using Headless::Logs;

struct Mode {
public:
  bool         surface;
  sdlrdp_codec codec;
};
inline bool HasCookie(Client const& client) {
  Expects(client.Instance() != nullptr, "client instance exists");
  Expects(client.Instance()->context, "client instance has a context");
  auto const* cookie = static_cast<ARC_SC_PRIVATE_PACKET const*>(
      freerdp_settings_get_pointer(client.Instance()->context->settings, FreeRDP_ServerAutoReconnectCookie));
  return cookie && cookie->cbLen == 28;
}
class FrameCounter {
public:
           FrameCounter(FrameCounter const&) = delete;
           FrameCounter(FrameCounter&&)      = delete;
  explicit FrameCounter(Client& client)
      : update(client.Instance()->context->update), surface(update->SurfaceBits), bitmap(update->BitmapUpdate) {
    Expects(!active, "no observer is already installed");
    Expects(surface, "surface callback is installed");
    Expects(bitmap, "bitmap callback is installed");
    active               = this;
    update->SurfaceBits  = ReceiveSurface;
    update->BitmapUpdate = ReceiveBitmap;
  }
  ~FrameCounter() {
    update->SurfaceBits  = surface;
    update->BitmapUpdate = bitmap;
    active               = nullptr;
  }
  FrameCounter& operator = (FrameCounter const&) = delete;
  FrameCounter& operator = (FrameCounter&&)      = delete;
  static BOOL   ReceiveSurface(rdpContext* context, SURFACE_BITS_COMMAND const* command) {
    Expects(active, "observer is installed");
    Expects(command, "wire command is supplied");
    auto result = active->surface(context, command);
    if (result && std::cmp_equal(command->destBottom, context->gdi->height)) ++active->frames;
    return result;
  }
  static BOOL ReceiveBitmap(rdpContext* context, BITMAP_UPDATE const* command) {
    Expects(active, "observer is installed");
    Expects(command, "wire command is supplied");
    auto result = active->bitmap(context, command);
    ++active->bitmap_pdus;
    if (result && std::ranges::any_of(std::span(command->rectangles, command->number), [=](auto const& rectangle) {
          return rectangle.destBottom + 1 == context->gdi->height;
        }))
      ++active->frames;
    return result;
  }
  unsigned Frames() const { return frames; }
  unsigned BitmapPdus() const { return bitmap_pdus; }

private:
  unsigned                                 frames      = 0;
  unsigned                                 bitmap_pdus = 0;
  inline static thread_local FrameCounter* active      = nullptr;
  rdpUpdate*                               update;
  pSurfaceBits                             surface;
  pBitmapUpdate                            bitmap;
};
// Both fixtures share the same bounded event accumulation; predicates inspect the
// whole sequence, so an early poll cannot lose half of a transition.
struct BackendEvents {
protected:
  bool Acknowledged() const {
    Expects(backend != nullptr, "backend exists");
    auto const status = CurrentStatus(*backend);
    return status && status->acknowledged >= Presented(*backend);
  }
  std::vector<sdlrdp_event> Events() const {
    std::array<sdlrdp_event, 256> batch { };
    auto                          count = sdlrdp_poll(backend.get(), batch.data(), batch.size());
    return { batch.begin(), batch.begin() + count };
  }
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
  std::vector<sdlrdp_event> Events(unsigned wanted) {
    return EventsUntil([=](auto const& events) { return events.size() >= wanted; }, false);
  }
  CertificateDirectory                                    certificates;
  Logs                                                    logs;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend     { nullptr, sdlrdp_close };
};
using Headless::FrameObserver;
}
