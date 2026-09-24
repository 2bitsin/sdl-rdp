#include <sdl-rdp/headless-client.test/client.hpp>

#include <sdl-rdp/headless-client.test/client-channels.hpp>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/codecs.h>
#include <freerdp/event.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/gdi/gfx.h>
#include <freerdp/settings.h>
#include <winpr/synch.h>
#include <algorithm>
#include <array>
#include <cstdlib>
#include <numeric>
#include <string_view>
#include <utility>

namespace Headless {
namespace {
auto ClientPostConnect(freerdp* client) -> BOOL {
  auto* context = client->context;
  return freerdp_client_codecs_reset(context->codecs, FREERDP_CODEC_ALL,
                                     freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
                                     freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight))
         && gdi_init(client, PIXEL_FORMAT_BGRX32);
}
auto ClientDesktopResize(rdpContext* context) -> BOOL {
  auto w = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth);
  auto h = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight);
  return freerdp_client_codecs_reset(context->codecs, FREERDP_CODEC_ALL, w, h) && gdi_resize(context->gdi, w, h);
}
auto ConfigureClientCodecs(rdpSettings* settings, bool surface) -> void {
  Expects(freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, TRUE), "client RemoteFX support is configured");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_NSCodec, TRUE), "client NSCodec support is configured");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, TRUE),
          "client certificate verification policy is configured");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, FALSE), "client NLA policy is configured");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, FALSE),
          "client graphics pipeline support is configured");
  if (!surface)
    Expects(freerdp_settings_set_uint32(settings, FreeRDP_SurfaceCommandsSupported, 0), "surface commands disabled");
}
auto ConfigureClient(rdpSettings* settings, unsigned port, bool surface, unsigned width, unsigned height) -> void {
  Expects(freerdp_settings_set_string(settings, FreeRDP_ServerHostname, "127.0.0.1"),
          "client server hostname is configured");
  Expects(freerdp_settings_set_string(settings, FreeRDP_Username, "test"), "client username is configured");
  Expects(freerdp_settings_set_uint32(settings, FreeRDP_ServerPort, port), "client server port is configured");
  Expects(freerdp_settings_set_uint32(settings, FreeRDP_DesktopWidth, width), "client desktop width is configured");
  Expects(freerdp_settings_set_uint32(settings, FreeRDP_DesktopHeight, height), "client desktop height is configured");
  Expects(freerdp_settings_set_uint32(settings, FreeRDP_ColorDepth, 32), "client color depth is configured");
  Expects(freerdp_settings_set_uint32(settings, FreeRDP_ThreadingFlags, THREADING_FLAGS_DISABLE_THREADS),
          "client configured");
  ConfigureClientCodecs(settings, surface);
}
auto ConnectGraphicsDecoder(void* raw, ChannelConnectedEventArgs const* event) -> void {
  if (std::string_view(event->name) != RDPGFX_DVC_CHANNEL_NAME) return;
  auto* context = static_cast<rdpContext*>(raw);
  Expects(gdi_graphics_pipeline_init(context->gdi, static_cast<RdpgfxClientContext*>(event->pInterface)),
          "graphics decoder initialized");
}
auto DisconnectGraphicsDecoder(void* raw, ChannelDisconnectedEventArgs const* event) -> void {
  if (std::string_view(event->name) != RDPGFX_DVC_CHANNEL_NAME) return;
  auto* context = static_cast<rdpContext*>(raw);
  gdi_graphics_pipeline_uninit(context->gdi, static_cast<RdpgfxClientContext*>(event->pInterface));
}
auto LoadGraphicsChannel(freerdp* instance) -> BOOL {
  return LoadDynamicChannel(instance, "rdpgfx");
}
auto ChannelError(UINT32 a, UINT32 b) -> unsigned {
  auto channel = [&](unsigned shift) { return std::abs(int((a >> shift) & 255) - int((b >> shift) & 255)); };
  return unsigned(std::max({ channel(0), channel(8), channel(16) }));
}
}

Client::Client(unsigned port, bool surface, unsigned width, unsigned height) {
  Expects(instance != nullptr, "client allocated");
  instance->PostConnect = ClientPostConnect;
  Expects(freerdp_context_new(instance.get()), "client context allocated");
  instance->context->update->DesktopResize = ClientDesktopResize;
  ConfigureClient(instance->context->settings, port, surface, width, height);
}
auto Client::EnableGraphics(bool h264) const -> void {
  auto* context = instance->context;
  Expects(freerdp_settings_set_bool(context->settings, FreeRDP_GfxH264, h264),
          "graphics pipeline enabled on the client pump thread");
  Expects(freerdp_settings_set_bool(context->settings, FreeRDP_GfxAVC444, FALSE),
          "graphics pipeline enabled on the client pump thread");
  Expects(freerdp_settings_set_bool(context->settings, FreeRDP_SupportGraphicsPipeline, TRUE),
          "graphics pipeline enabled on the client pump thread");
  Expects(freerdp_settings_set_bool(context->settings, FreeRDP_SynchronousDynamicChannels, TRUE),
          "graphics pipeline enabled on the client pump thread");
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  PubSub_SubscribeChannelConnected(context->pubSub, ConnectGraphicsDecoder);
  PubSub_SubscribeChannelDisconnected(context->pubSub, DisconnectGraphicsDecoder);
  instance->LoadChannels = LoadGraphicsChannel;
}
auto Client::Credentials(char const* user, char const* password, char const* domain, bool nla) const -> void {
  auto* settings = instance->context->settings;
  Expects(freerdp_settings_set_string(settings, FreeRDP_Username, user), "client username is configured");
  Expects(freerdp_settings_set_string(settings, FreeRDP_Password, password), "client password is configured");
  Expects(freerdp_settings_set_string(settings, FreeRDP_Domain, domain), "client domain is configured");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, nla), "client NLA policy is configured");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_ExtSecurity, nla), "client NLA_EXT follows NLA");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_TlsSecurity, !nla), "client TLS policy is configured");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, FALSE), "client RDP security policy is configured");
  Expects(freerdp_settings_set_string(settings, FreeRDP_AuthenticationPackageList, "!kerberos"),
          "client credentials configured");
}
auto Client::Pump(unsigned timeout) const -> bool {
  std::array<HANDLE, 64> handles { };
  auto                   count   = freerdp_get_event_handles(instance->context, handles.data(), handles.size());
  return count && WaitForMultipleObjects(count, handles.data(), FALSE, timeout) != WAIT_FAILED
         && freerdp_check_event_handles(instance->context);
}
auto Client::Matches(std::vector<UINT32> const& pixels) -> bool {
  auto* gdi = instance->context->gdi;
  Expects(std::cmp_equal(gdi->stride, gdi->width * 4), "decoded rows are packed");
  if (pixels.size() != std::size_t(gdi->width) * gdi->height) return false;
  auto const* actual = reinterpret_cast<UINT32 const*>(gdi->primary_buffer);
  if (!tolerance)
    return std::equal(pixels.begin(), pixels.end(), actual, [](auto a, auto b) { return ((a ^ b) & 0x00ffffff) == 0; });
  return std::equal(pixels.begin(), pixels.end(), actual,
                    [&](auto a, auto b) { return ChannelError(a, b) <= tolerance; });
}
auto Client::MaxError(std::vector<UINT32> const& pixels, std::vector<UINT32> const* reference) const -> unsigned {
  Expects(instance->context->gdi != nullptr, "decoded framebuffer exists");
  auto const* actual   = reinterpret_cast<UINT32 const*>(instance->context->gdi->primary_buffer);
  auto const& expected = reference ? *reference : pixels;
  Expects(expected.size() == pixels.size(), "reference matches source dimensions");
  return std::transform_reduce(
      expected.begin(), expected.end(), actual, 0u, [](auto a, auto b) { return std::max(a, b); }, ChannelError);
}
auto Client::Received() const -> UINT64 {
  UINT64 bytes = 0;
  Expects(freerdp_get_stats(instance->context->rdp, &bytes, nullptr, nullptr, nullptr),
          "transport statistics available");
  return bytes;
}
auto Client::Instance() const -> std::unique_ptr<freerdp, ReleaseClient> const& {
  return instance;
}
auto Client::Tolerance() const -> unsigned {
  return tolerance;
}
auto Client::Tolerance(unsigned value) -> void {
  tolerance = value;
}
}
