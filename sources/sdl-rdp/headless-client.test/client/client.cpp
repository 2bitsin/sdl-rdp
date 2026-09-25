#include <sdl-rdp/headless-client.test/client/client.hpp>

#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
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
using sdl_rdp::freerdp_facade::FirstRefused;
using sdl_rdp::freerdp_facade::Refusal;
using sdl_rdp::freerdp_facade::Set;
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
auto ConfigureClientCodecs(rdpSettings& settings, bool surface) -> void {
  std::array<std::pair<FreeRDP_Settings_Keys_Bool, bool>, 5> const codecs_and_security{ {
      { FreeRDP_RemoteFxCodec          , true  },
      { FreeRDP_NSCodec                , true  },
      { FreeRDP_IgnoreCertificate      , true  },
      { FreeRDP_NlaSecurity            , false },
      { FreeRDP_SupportGraphicsPipeline, false },
  } };

  auto const refused_codec_or_security = FirstRefused(settings, codecs_and_security);
  Expects(!refused_codec_or_security.has_value(),
          Refusal("client codec and security policy", refused_codec_or_security));
  if (surface) return;
  auto const surface_off = Set(settings, FreeRDP_SurfaceCommandsSupported, std::uint32_t{ 0 });
  Expects(surface_off, "surface commands disabled");
}
auto ConfigureClient(rdpSettings& settings, std::uint32_t port, bool surface, std::uint32_t width, std::uint32_t height)
    -> void {
  std::array<std::pair<FreeRDP_Settings_Keys_String, std::string_view>, 2> const server_and_user           { {
      { FreeRDP_ServerHostname, "127.0.0.1" },
      { FreeRDP_Username      , "test"      },
  } };
  std::array<std::pair<FreeRDP_Settings_Keys_UInt32, std::uint32_t>, 5> const    port_desktop_and_threading{ {
      { FreeRDP_ServerPort    , port                            },
      { FreeRDP_DesktopWidth  , width                           },
      { FreeRDP_DesktopHeight , height                          },
      { FreeRDP_ColorDepth    , 32                              },
      { FreeRDP_ThreadingFlags, THREADING_FLAGS_DISABLE_THREADS },
  } };

  auto const refused_server_or_user = FirstRefused(settings, server_and_user);
  Expects(!refused_server_or_user.has_value(), Refusal("client server and user", refused_server_or_user));
  auto const refused_port_desktop_or_threading = FirstRefused(settings, port_desktop_and_threading);
  Expects(!refused_port_desktop_or_threading.has_value(),
          Refusal("client port, desktop and threading", refused_port_desktop_or_threading));
  ConfigureClientCodecs(settings, surface);
}
auto ConnectGraphicsDecoder(void* raw, ChannelConnectedEventArgs const* event) -> void {
  if (std::string_view(event->name) != RDPGFX_DVC_CHANNEL_NAME) return;
  auto*      context     = static_cast<rdpContext*>(raw);
  auto const initialized = gdi_graphics_pipeline_init(context->gdi,
                                                      static_cast<RdpgfxClientContext*>(event->pInterface));
  Expects(initialized, "graphics decoder initialized");
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
  ConfigureClient(*instance->context->settings, port, surface, width, height);
}
auto Client::EnableGraphics(GraphicsOptions options) -> void {
  std::array<std::pair<FreeRDP_Settings_Keys_Bool, bool>, 5> const graphics{ {
      { FreeRDP_GfxH264                   , options.h264                 },
      { FreeRDP_GfxSendQoeAck             , options.qoe_acknowledgements },
      { FreeRDP_GfxAVC444                 , false                        },
      { FreeRDP_SupportGraphicsPipeline   , true                         },
      { FreeRDP_SynchronousDynamicChannels, true                         },
  } };

  auto*      context          = instance->context;
  auto const refused_graphics = FirstRefused(*context->settings, graphics);
  Expects(!refused_graphics.has_value(), Refusal("the client's graphics pipeline preferences", refused_graphics));
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  PubSub_SubscribeChannelConnected(context->pubSub, ConnectGraphicsDecoder);
  PubSub_SubscribeChannelDisconnected(context->pubSub, DisconnectGraphicsDecoder);
  instance->LoadChannels = LoadGraphicsChannel;
}
auto Client::Credentials(Login const& login, bool nla) -> void {
  std::array<std::pair<FreeRDP_Settings_Keys_String, std::string_view>, 4> const credentials{ {
      { FreeRDP_Username                 , login.user     },
      { FreeRDP_Password                 , login.password },
      { FreeRDP_Domain                   , login.domain   },
      { FreeRDP_AuthenticationPackageList, "!kerberos"    },
  } };
  std::array<std::pair<FreeRDP_Settings_Keys_Bool, bool>, 4> const               security   { {
      { FreeRDP_NlaSecurity, nla   },
      { FreeRDP_ExtSecurity, nla   },
      { FreeRDP_TlsSecurity, !nla  },
      { FreeRDP_RdpSecurity, false },
  } };

  auto&      settings           = *instance->context->settings;
  auto const refused_credential = FirstRefused(settings, credentials);
  Expects(!refused_credential.has_value(), Refusal("client credentials", refused_credential));
  auto const refused_security = FirstRefused(settings, security);
  Expects(!refused_security.has_value(), Refusal("client security policy", refused_security));
}
auto Client::Connect() -> bool {
  auto const connected = freerdp_connect(instance.get()) != 0;
  if (connected)
    utilities::Ensures(instance->context->codecs->ThreadingFlags == THREADING_FLAGS_DISABLE_THREADS,
                       "the connected client decodes on its pump thread");
  return connected;
}
auto Client::Key(std::uint16_t scancode, KeyState state) -> bool {
  return freerdp_input_send_keyboard_event(instance->context->input, KeyboardFlags(state), scancode);
}
auto Client::Disconnect() -> bool {
  return freerdp_disconnect(instance.get()) != 0;
}
auto Client::Pump(std::uint32_t timeout) -> bool {
  std::array<Backend::WaitHandle, 64> handles{ };
  auto count = freerdp_get_event_handles(instance->context, handles.data(), handles.size());
  return count && WaitForMultipleObjects(count, handles.data(), false, timeout) != WAIT_FAILED
         && freerdp_check_event_handles(instance->context);
}
auto Tap(Client& client, std::uint16_t scancode) -> void {
  ASSERT_TRUE(client.Key(scancode, KeyState::Down)) << "send key down " << scancode;
  ASSERT_TRUE(client.Key(scancode, KeyState::Up)) << "send key up " << scancode;
}
auto PumpInBackground(Client& client) -> std::jthread {
  return std::jthread([&client](std::stop_token const& stop) {
    while (!stop.stop_requested() && client.Pump()) {
    }
  });
}
auto Client::Matches(Pixels const& pixels) -> bool {
  auto* gdi = instance->context->gdi;
  Expects(std::cmp_equal(gdi->stride, gdi->width * 4), "decoded rows are packed");
  if (pixels.size() != Backend::Narrowed<std::size_t>(gdi->width) * gdi->height) return false;
  auto const* actual = reinterpret_cast<std::uint32_t const*>(gdi->primary_buffer);
  if (!tolerance)
    return std::equal(pixels.begin(), pixels.end(), actual, [](auto a, auto b) { return ((a ^ b) & 0x00ffffff) == 0; });
  return std::equal(pixels.begin(), pixels.end(), actual,
                    [&](auto a, auto b) { return ChannelError(a, b) <= tolerance; });
}
auto Client::MaxError(Pixels const& pixels, Pixels const* reference) const -> std::uint32_t {
  Expects(instance->context->gdi != nullptr, "decoded framebuffer exists");
  auto const* actual   = reinterpret_cast<std::uint32_t const*>(instance->context->gdi->primary_buffer);
  auto const& expected = reference ? *reference : pixels;
  Expects(expected.size() == pixels.size(), "reference matches source dimensions");
  return std::transform_reduce(
      expected.begin(), expected.end(), actual, 0u, [](auto a, auto b) { return std::max(a, b); }, ChannelError);
}
auto Client::Received() const -> std::uint64_t {
  std::uint64_t bytes   = 0;
  auto const    counted = freerdp_get_stats(instance->context->rdp, &bytes, nullptr, nullptr, nullptr);
  Expects(counted, "transport statistics available");
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
