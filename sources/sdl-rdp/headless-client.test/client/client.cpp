#include <sdl-rdp/headless-client.test/client/client.hpp>

#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/headless-client.test/client/channels.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/addin.h>
#include <freerdp/channels/channels.h>
#include <freerdp/client/channels.h>
#include <freerdp/client/rdpgfx.h>
#include <freerdp/codecs.h>
#include <freerdp/event.h>
#include <freerdp/gdi/gdi.h>
#include <freerdp/gdi/gfx.h>
#include <freerdp/settings.h>
#include <gtest/gtest.h>
#include <winpr/synch.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <string_view>
#include <utility>

namespace Headless {
auto FreeGraphics(freerdp* instance) noexcept -> void {
  gdi_free(instance);
}
namespace {
// abi: pPostConnect, BOOL is int
auto ClientPostConnect(freerdp* client) -> int {
  auto* context = client->context;
  return freerdp_client_codecs_reset(context->codecs, FREERDP_CODEC_ALL,
                                     freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth),
                                     freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight))
         && gdi_init(client, PIXEL_FORMAT_BGRX32);
}
// abi: pDesktopResize, BOOL is int
auto ClientDesktopResize(rdpContext* context) -> int {
  auto w = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopWidth);
  auto h = freerdp_settings_get_uint32(context->settings, FreeRDP_DesktopHeight);
  return freerdp_client_codecs_reset(context->codecs, FREERDP_CODEC_ALL, w, h) && gdi_resize(context->gdi, w, h);
}
auto ConfigureClientCodecs(rdpSettings* settings, bool surface) -> void {
  Expects(freerdp_settings_set_bool(settings, FreeRDP_RemoteFxCodec, true), "client RemoteFX support is configured");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_NSCodec, true), "client NSCodec support is configured");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_IgnoreCertificate, true),
          "client certificate verification policy is configured");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_NlaSecurity, false), "client NLA policy is configured");
  Expects(freerdp_settings_set_bool(settings, FreeRDP_SupportGraphicsPipeline, false),
          "client graphics pipeline support is configured");
  if (!surface)
    Expects(freerdp_settings_set_uint32(settings, FreeRDP_SurfaceCommandsSupported, 0), "surface commands disabled");
}
auto ConfigureClient(rdpSettings* settings, std::uint32_t port, bool surface, std::uint32_t width, std::uint32_t height)
    -> void {
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
// abi: pLoadChannels, BOOL is int
auto LoadGraphicsChannel(freerdp* instance) -> int {
  return LoadDynamicChannel(instance, "rdpgfx");
}
auto KeyboardFlags(KeyState state) -> std::uint16_t {
  switch (state) {
  case KeyState::Down: return KBD_FLAGS_DOWN;
  case KeyState::Up:   return KBD_FLAGS_RELEASE;
  default:             ::utilities::Unreachable(state);
  }
}
auto ChannelError(std::uint32_t a, std::uint32_t b) -> std::uint32_t {
  auto channel = [&](std::uint32_t shift) { return std::abs(int((a >> shift) & 255) - int((b >> shift) & 255)); };
  return Backend::Narrowed<std::uint32_t>(std::max({ channel(0), channel(8), channel(16) }));
}
}

Client::Client(std::uint32_t port, bool surface, std::uint32_t width, std::uint32_t height) {
  Expects(instance != nullptr, "client allocated");
  instance->PostConnect = ClientPostConnect;
  instance->ContextSize = ObserverSet::ContextSize();
  auto const allocated = freerdp_context_new(instance.get());
  Expects(allocated, "client context allocated");
  observers->Bind(*instance->context);
  instance->context->update->DesktopResize = ClientDesktopResize;
  ConfigureClient(instance->context->settings, port, surface, width, height);
}
auto Client::EnableGraphics(GraphicsOptions options) const -> void {
  auto*      context     = instance->context;
  auto const h264_set    = freerdp_settings_set_bool(context->settings, FreeRDP_GfxH264, options.h264);
  auto const qoe_set     = freerdp_settings_set_bool(context->settings, FreeRDP_GfxSendQoeAck,
                                                     options.qoe_acknowledgements);
  auto const avc444_set  = freerdp_settings_set_bool(context->settings, FreeRDP_GfxAVC444, false);
  auto const pipeline_on = freerdp_settings_set_bool(context->settings, FreeRDP_SupportGraphicsPipeline, true);
  auto const synchronous = freerdp_settings_set_bool(context->settings, FreeRDP_SynchronousDynamicChannels, true);
  Expects(h264_set, "the client's H.264 preference is set");
  Expects(qoe_set, "the client's QoE acknowledgement preference is set");
  Expects(avc444_set, "the client's AVC444 preference is set");
  Expects(pipeline_on, "the client's graphics pipeline is enabled");
  Expects(synchronous, "the client's dynamic channels are synchronous");
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
  Expects(freerdp_settings_set_bool(settings, FreeRDP_RdpSecurity, false), "client RDP security policy is configured");
  Expects(freerdp_settings_set_string(settings, FreeRDP_AuthenticationPackageList, "!kerberos"),
          "client credentials configured");
}
auto Client::Connect() const -> bool {
  auto const connected = freerdp_connect(instance.get()) != 0;
  if (connected)
    utilities::Ensures(instance->context->codecs->ThreadingFlags == THREADING_FLAGS_DISABLE_THREADS,
                       "the connected client decodes on its pump thread");
  return connected;
}
auto Client::Key(std::uint16_t scancode, KeyState state) const -> bool {
  return freerdp_input_send_keyboard_event(instance->context->input, KeyboardFlags(state), scancode);
}
auto Client::Disconnect() const -> bool {
  return freerdp_disconnect(instance.get()) != 0;
}
auto Client::Pump(std::uint32_t timeout) const -> bool {
  std::array<Backend::WaitHandle, 64> handles{ };
  auto count = freerdp_get_event_handles(instance->context, handles.data(), handles.size());
  return count && WaitForMultipleObjects(count, handles.data(), false, timeout) != WAIT_FAILED
         && freerdp_check_event_handles(instance->context);
}
auto Tap(Client const& client, std::uint16_t scancode) -> void {
  ASSERT_TRUE(client.Key(scancode, KeyState::Down)) << "send key down " << scancode;
  ASSERT_TRUE(client.Key(scancode, KeyState::Up)) << "send key up " << scancode;
}
auto PumpInBackground(Client const& client) -> std::jthread {
  return std::jthread([&client](std::stop_token const& stop) {
    while (!stop.stop_requested() && client.Pump()) {
    }
  });
}
auto Client::Matches(std::vector<std::uint32_t> const& pixels) -> bool {
  auto* gdi = instance->context->gdi;
  Expects(std::cmp_equal(gdi->stride, gdi->width * 4), "decoded rows are packed");
  if (pixels.size() != Backend::Narrowed<std::size_t>(gdi->width) * gdi->height) return false;
  auto const* actual = reinterpret_cast<std::uint32_t const*>(gdi->primary_buffer);
  if (!tolerance)
    return std::equal(pixels.begin(), pixels.end(), actual, [](auto a, auto b) { return ((a ^ b) & 0x00ffffff) == 0; });
  return std::equal(pixels.begin(), pixels.end(), actual,
                    [&](auto a, auto b) { return ChannelError(a, b) <= tolerance; });
}
auto Client::MaxError(std::vector<std::uint32_t> const& pixels, std::vector<std::uint32_t> const* reference) const
    -> std::uint32_t {
  Expects(instance->context->gdi != nullptr, "decoded framebuffer exists");
  auto const* actual   = reinterpret_cast<std::uint32_t const*>(instance->context->gdi->primary_buffer);
  auto const& expected = reference ? *reference : pixels;
  Expects(expected.size() == pixels.size(), "reference matches source dimensions");
  return std::transform_reduce(
      expected.begin(), expected.end(), actual, 0u, [](auto a, auto b) { return std::max(a, b); }, ChannelError);
}
auto Client::Received() const -> std::uint64_t {
  std::uint64_t bytes = 0;
  Expects(freerdp_get_stats(instance->context->rdp, &bytes, nullptr, nullptr, nullptr),
          "transport statistics available");
  return bytes;
}
auto Client::Instance() const -> ClientInstance const& {
  return instance;
}
auto Client::Tolerance() const -> std::uint32_t {
  return tolerance;
}
auto Client::Tolerance(std::uint32_t value) -> void {
  tolerance = value;
}
auto Client::UntilDesktop(std::uint32_t width, std::uint32_t height) -> bool {
  return Until([&] {
    auto const& gdi = *instance->context->gdi;
    return std::cmp_equal(gdi.width, width) && std::cmp_equal(gdi.height, height);
  });
}
}
