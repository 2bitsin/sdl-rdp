#pragma once
#include "contract.hpp"
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/settings.h>
#include <winpr/synch.h>
#include <array>
#include <chrono>
#include <cstring>
#include <memory>
#include <vector>
#include <algorithm>
#include <ranges>
#include <cstdlib>
#include <numeric>

namespace Headless {
using Clock = std::chrono::steady_clock;
using utilities::Expects;
struct ReleaseClient {
  void operator()(freerdp* instance) const {
    freerdp_disconnect(instance);
    gdi_free(instance);
    freerdp_context_free(instance);
    freerdp_free(instance);
  }
};
class Client {
public:
  std::unique_ptr<freerdp, ReleaseClient> instance{freerdp_new()};
  unsigned tolerance = 0;
  explicit Client(unsigned port, bool surface, unsigned width = 320, unsigned height = 200) {
    Expects(instance != nullptr, "client allocated");
    instance->PostConnect = [](freerdp* client) { return gdi_init(client, PIXEL_FORMAT_BGRX32); };
    Expects(freerdp_context_new(instance.get()), "client context allocated");
    auto settings = instance->context->settings;
    Expects(freerdp_settings_set_string(settings, FreeRDP_ServerHostname, "127.0.0.1")
      && freerdp_settings_set_string(settings, FreeRDP_Username, "test")
      && freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, port)
      && freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, width)
      && freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, height)
      && freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32)
      && freerdp_settings_set_uint32(settings, FreeRDP_ThreadingFlags, THREADING_FLAGS_DISABLE_THREADS)
      && freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE)
      && freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE)
      && freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, TRUE)
      && freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE)
      && freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, FALSE), "client configured");
    if (!surface) Expects(freerdp_settings_set_uint32(settings, FreeRDP_SurfaceCommandsSupported, 0),
                         "surface commands disabled");
  }
  bool Pump(unsigned timeout = 10) {
    std::array<HANDLE, 64> handles{};
    auto count = freerdp_get_event_handles(instance->context, handles.data(), handles.size());
    return count && WaitForMultipleObjects(count, handles.data(), FALSE, timeout) != WAIT_FAILED
      && freerdp_check_event_handles(instance->context);
  }
  bool Matches(std::vector<UINT32> const& pixels) {
    auto gdi = instance->context->gdi;
    Expects(gdi->stride == gdi->width * 4, "decoded rows are packed");
    auto actual = reinterpret_cast<UINT32 const*>(gdi->primary_buffer);
    if (!tolerance) return std::equal(pixels.begin(), pixels.end(), actual,
      [](auto a, auto b) { return ((a ^ b) & 0x00ffffff) == 0; });
    return std::equal(pixels.begin(), pixels.end(), actual, [&](auto a, auto b) {
      return std::abs(int(a & 255) - int(b & 255)) <= int(tolerance)
        && std::abs(int((a >> 8) & 255) - int((b >> 8) & 255)) <= int(tolerance)
        && std::abs(int((a >> 16) & 255) - int((b >> 16) & 255)) <= int(tolerance);
    });
  }

  unsigned MaxError(std::vector<UINT32> const& pixels) const {
    Expects(instance->context->gdi != nullptr, "decoded framebuffer exists");
    auto actual = reinterpret_cast<UINT32 const*>(instance->context->gdi->primary_buffer);
    return std::transform_reduce(pixels.begin(), pixels.end(), actual, 0u,
      [](auto a, auto b) { return std::max(a, b); }, [](auto a, auto b) {
        return unsigned(std::max({std::abs(int(a & 255) - int(b & 255)),
          std::abs(int((a >> 8) & 255) - int((b >> 8) & 255)),
          std::abs(int((a >> 16) & 255) - int((b >> 16) & 255))}));
      });
  }
  UINT64 Received() const {
    UINT64 bytes = 0;
    Expects(freerdp_get_stats(instance->context->rdp, &bytes, nullptr, nullptr, nullptr), "transport statistics available");
    return bytes;
  }

  bool Until(auto ready) {
    auto deadline = Clock::now() + std::chrono::seconds(3);
    while (!ready() && Clock::now() < deadline) {
      for (unsigned batch = 0; batch < 16; ++batch) if (!Pump(batch ? 0 : 10)) return false;
    }
    return ready();
  }
};
}
