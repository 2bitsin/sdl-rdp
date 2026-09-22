#pragma once
#include "contract.hpp"
#include <freerdp/freerdp.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/codecs.h>
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
#include <atomic>
#include <string_view>
#include <freerdp/client/disp.h>
#include <freerdp/client/channels.h>
#include <freerdp/channels/channels.h>
#include <freerdp/addin.h>
#include <freerdp/client/cmdline.h>
#include <freerdp/event.h>

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
    instance->PostConnect = [](freerdp* client) -> BOOL {
      auto context = client->context;
      return freerdp_client_codecs_reset(context->codecs, FREERDP_CODEC_ALL,
        freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
        freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight))
        && gdi_init(client, PIXEL_FORMAT_BGRX32);
    };
    Expects(freerdp_context_new(instance.get()), "client context allocated");
    instance->context->update->DesktopResize = [](rdpContext* context) -> BOOL {
      auto w = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth);
      auto h = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight);
      return freerdp_client_codecs_reset(context->codecs, FREERDP_CODEC_ALL, w, h)
        && gdi_resize(context->gdi, w, h);
    };
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
    if (pixels.size() != std::size_t(gdi->width) * gdi->height) return false;
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
struct FrameObserver {
  inline static thread_local FrameObserver* active = nullptr;
  rdpUpdate* update;
  pSurfaceFrameMarker original;
  std::vector<UINT32> ids;
  bool coherent = true;
  explicit FrameObserver(Client& client) : update(client.instance->context->update), original(update->SurfaceFrameMarker) {
    Expects(!active, "one frame observer per thread");
    active = this;
    update->SurfaceFrameMarker = Receive;
  }
  ~FrameObserver() { update->SurfaceFrameMarker = original; active = nullptr; }
  static BOOL Receive(rdpContext* context, SURFACE_FRAME_MARKER const* marker) {
    if (marker->frameAction != SURFACECMD_FRAMEACTION_END) return TRUE;
    active->ids.push_back(marker->frameId);
    auto gdi = context->gdi;
    auto pixels = reinterpret_cast<UINT32 const*>(gdi->primary_buffer);
    active->coherent &= (pixels[0] & 0xffffff) == (pixels[(gdi->height - 1) * gdi->width] & 0xffffff);
    return TRUE;
  }
  bool Ack() { return !ids.empty() && update->SurfaceFrameAcknowledge(update->context, ids.back()); }
};
struct DisplayClient {
  inline static std::atomic<DispClientContext*> channel = nullptr;
  inline static std::atomic_bool ready = false;
  static void Connected(void*, ChannelConnectedEventArgs const* event) {
    if (std::string_view(event->name) != DISP_DVC_CHANNEL_NAME) return;
    channel = static_cast<DispClientContext*>(event->pInterface);
    channel.load()->DisplayControlCaps = [](DispClientContext*, UINT32, UINT32, UINT32) -> UINT {
      ready = true; return CHANNEL_RC_OK;
    };
  }
  explicit DisplayClient(Client& client) {
    channel = nullptr; ready = false;
    freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
    auto context = client.instance->context;
    Expects(freerdp_settings_set_bool(context->settings, FreeRDP_SupportDisplayControl, TRUE), "display control enabled");
    PubSub_SubscribeChannelConnected(context->pubSub, Connected);
    client.instance->LoadChannels = [](freerdp* instance) -> BOOL {
      char const* channel[] = {"disp"};
      auto settings = instance->context->settings;
      auto entry = reinterpret_cast<PVIRTUALCHANNELENTRYEX>(freerdp_load_channel_addin_entry(
        "drdynvc", nullptr, nullptr, FREERDP_ADDIN_CHANNEL_STATIC | FREERDP_ADDIN_CHANNEL_ENTRYEX));
      return entry && freerdp_client_add_dynamic_channel(settings, 1, channel)
        && freerdp_channels_client_load_ex(instance->context->channels, settings, entry, settings) == 0;
    };
  }
};
}
