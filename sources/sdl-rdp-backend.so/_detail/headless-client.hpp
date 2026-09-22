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
      && freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, TRUE)
      && freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE)
      && freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, FALSE), "client configured");
    if (!surface) Expects(freerdp_settings_set_uint32(settings, FreeRDP_SurfaceCommandsSupported, 0),
                         "surface commands disabled");
  }
  bool Pump() {
    std::array<HANDLE, 64> handles{};
    auto count = freerdp_get_event_handles(instance->context, handles.data(), handles.size());
    return count && WaitForMultipleObjects(count, handles.data(), FALSE, 10) != WAIT_FAILED
      && freerdp_check_event_handles(instance->context);
  }
  bool Matches(std::vector<UINT32> const& pixels) {
    auto gdi = instance->context->gdi;
    return gdi->stride == gdi->width * 4
      && std::memcmp(pixels.data(), gdi->primary_buffer, pixels.size() * 4) == 0;
  }
  bool Until(auto ready) {
    auto deadline = Clock::now() + std::chrono::seconds(3);
    while (!ready() && Clock::now() < deadline) if (!Pump()) return false;
    return ready();
  }
};
}
