#include <sdl-rdp/headless-client.test/client/client.hpp>

#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/headless-client.test/client/channels.hpp>
#include <sdl-rdp/headless-client.test/client/framebuffer.hpp>
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
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <numeric>
#include <string_view>
#include <tuple>
#include <utility>

namespace sdl_rdp::headless_client_test::client::detail::client {
using sdl_rdp::freerdp_facade::BoolKey;
using sdl_rdp::freerdp_facade::MaximumWaitHandles;
using sdl_rdp::freerdp_facade::NumberKey;
using sdl_rdp::freerdp_facade::StringKey;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Unreachable;
auto FreeGraphics(freerdp* instance) noexcept -> void {
  gdi_free(instance);
}
namespace {
// abi: pPostConnect, BOOL is int
auto ClientPostConnect(freerdp* client) -> int {
  Expects(client != nullptr, "post-connect names its client");
  auto&      context  = *client->context;
  auto const settings = SettingsView{ *context.settings };
  return freerdp_client_codecs_reset(context.codecs, FREERDP_CODEC_ALL, settings.Get(NumberKey::DesktopWidth),
                                     settings.Get(NumberKey::DesktopHeight))
         && gdi_init(client, PIXEL_FORMAT_BGRX32);
}
// abi: pDesktopResize, BOOL is int
auto ClientDesktopResize(rdpContext* context) -> int {
  Expects(context != nullptr, "resize names its client context");
  auto&      resized  = *context;
  auto const settings = SettingsView{ *resized.settings };
  auto const w        = settings.Get(NumberKey::DesktopWidth);
  auto const h        = settings.Get(NumberKey::DesktopHeight);
  return freerdp_client_codecs_reset(resized.codecs, FREERDP_CODEC_ALL, w, h) && gdi_resize(resized.gdi, w, h);
}
auto ConfigureClientCodecs(SettingsView settings, bool surface) -> void {
  std::array<std::pair<BoolKey, bool>, 5> const codecs_and_security{ {
      { BoolKey::RemoteFxCodec          , true  },
      { BoolKey::NSCodec                , true  },
      { BoolKey::IgnoreCertificate      , true  },
      { BoolKey::NlaSecurity            , false },
      { BoolKey::SupportGraphicsPipeline, false },
  } };

  settings.Apply(codecs_and_security);
  if (!surface) settings.Set(NumberKey::SurfaceCommandsSupported, 0U);
}
auto ConfigureClient(rdpSettings& native, std::uint32_t port, bool surface, std::uint32_t width, std::uint32_t height)
    -> void {
  std::array<std::pair<StringKey, std::string_view>, 2> const server_and_user { {
      { StringKey::ServerHostname, "127.0.0.1" },
      { StringKey::Username      , "test"      },
  } };
  std::array<std::pair<NumberKey, std::uint32_t>, 4> const    port_and_desktop{ {
      { NumberKey::ServerPort   , port   },
      { NumberKey::DesktopWidth , width  },
      { NumberKey::DesktopHeight, height },
      { NumberKey::ColorDepth   , 32     },
  } };

  SettingsView const settings{ native };
  settings.Apply(server_and_user);
  settings.Apply(port_and_desktop);
  auto const unthreaded = freerdp_settings_set_uint32(&native, FreeRDP_ThreadingFlags, THREADING_FLAGS_DISABLE_THREADS);
  Expects(unthreaded, "client codec threads disabled");
  ConfigureClientCodecs(settings, surface);
}
auto StartGraphicsDecoder(rdpContext& context, RdpgfxClientContext& channel) -> void {
  auto const initialized = gdi_graphics_pipeline_init(context.gdi, &channel);
  Expects(initialized, "graphics decoder initialized");
}
auto StopGraphicsDecoder(rdpContext& context, RdpgfxClientContext& channel) -> void {
  gdi_graphics_pipeline_uninit(context.gdi, &channel);
}
// abi: pChannelConnectedEventHandler and pChannelDisconnectedEventHandler
template <auto apply, class EventTy> auto GraphicsDecoderEvent(void* raw, EventTy const* event) -> void {
  Expects(raw != nullptr, "the channel event names its client context");
  Expects(event != nullptr, "channel event is supplied");
  if (std::string_view(event->name) != RDPGFX_DVC_CHANNEL_NAME) return;
  Expects(event->pInterface != nullptr, "the graphics channel interface exists");
  apply(*static_cast<rdpContext*>(raw), *static_cast<RdpgfxClientContext*>(event->pInterface));
}
auto KeyboardFlags(KeyState state) -> std::uint16_t {
  switch (state) {
  case KeyState::Down: return KBD_FLAGS_DOWN;
  case KeyState::Up:   return KBD_FLAGS_RELEASE;
  default:             Unreachable(state);
  }
}
auto Decoded(freerdp const& instance) -> rdpGdi const& {
  auto const* gdi = instance.context->gdi;
  Expects(gdi != nullptr, "decoded framebuffer exists");
  Expects(std::cmp_equal(gdi->stride, gdi->width * 4), "decoded rows are packed");
  return *gdi;
}
auto ChannelError(std::uint32_t a, std::uint32_t b) -> std::uint32_t {
  auto channel = [&](std::uint32_t shift) {
    return std::abs(Narrowed<int>((a >> shift) & 255) - Narrowed<int>((b >> shift) & 255));
  };
  return Narrowed<std::uint32_t>(std::max({ channel(0), channel(8), channel(16) }));
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
  std::array<std::pair<BoolKey, bool>, 5> const graphics{ {
      { BoolKey::GfxH264                   , options.h264                 },
      { BoolKey::GfxSendQoeAck             , options.qoe_acknowledgements },
      { BoolKey::GfxAVC444                 , false                        },
      { BoolKey::SupportGraphicsPipeline   , true                         },
      { BoolKey::SynchronousDynamicChannels, true                         },
  } };

  auto* context = instance->context;
  SettingsOf(*this).Apply(graphics);
  freerdp_register_addin_provider(freerdp_channels_load_static_addin_entry, 0);
  PubSub_SubscribeChannelConnected(context->pubSub,
                                   GraphicsDecoderEvent<StartGraphicsDecoder, ChannelConnectedEventArgs>);
  PubSub_SubscribeChannelDisconnected(context->pubSub,
                                      GraphicsDecoderEvent<StopGraphicsDecoder, ChannelDisconnectedEventArgs>);
  instance->LoadChannels = ChannelLoader<LoadDynamicChannel, "rdpgfx">;
}
auto Client::Credentials(Login const& login, bool nla) -> void {
  std::array<std::pair<StringKey, std::string_view>, 4> const credentials{ {
      { StringKey::Username                 , login.user     },
      { StringKey::Password                 , login.password },
      { StringKey::Domain                   , login.domain   },
      { StringKey::AuthenticationPackageList, "!kerberos"    },
  } };
  std::array<std::pair<BoolKey, bool>, 4> const               security   { {
      { BoolKey::NlaSecurity, nla   },
      { BoolKey::ExtSecurity, nla   },
      { BoolKey::TlsSecurity, !nla  },
      { BoolKey::RdpSecurity, false },
  } };

  auto const settings = SettingsOf(*this);
  settings.Apply(credentials);
  settings.Apply(security);
}
auto Client::Connect() -> bool {
  auto const connected = freerdp_connect(instance.get()) != 0;
  if (connected)
    Ensures(instance->context->codecs->ThreadingFlags == THREADING_FLAGS_DISABLE_THREADS,
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
  std::array<WaitHandle, MaximumWaitHandles> handles{ };
  auto const ready = WaitHandle::Collected<freerdp_get_event_handles>(*instance->context, handles);
  if (ready.empty()) return false;
  std::ignore = WaitHandle::Any(ready, timeout);
  return freerdp_check_event_handles(instance->context);
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
  auto const actual = DecodedPixels(*this);
  if (pixels.size() != actual.size()) return false;
  if (!tolerance) return std::ranges::equal(pixels, actual, [](auto a, auto b) { return ((a ^ b) & 0x00ffffff) == 0; });
  return std::ranges::equal(pixels, actual, [&](auto a, auto b) { return ChannelError(a, b) <= tolerance; });
}
auto Client::MaxError(Pixels const& pixels) const -> std::uint32_t {
  auto const actual = DecodedPixels(*this);
  Expects(pixels.size() == actual.size(), "the source matches the decoded frame");
  return std::transform_reduce(
      pixels.begin(), pixels.end(), actual.begin(), 0u, [](auto a, auto b) { return std::max(a, b); }, ChannelError);
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
auto Client::DesktopSize() const -> Extent {
  auto const& decoded = Decoded(*instance);
  return { .width = Narrowed<std::uint32_t>(decoded.width), .height = Narrowed<std::uint32_t>(decoded.height) };
}
auto DecodedPixels(Client const& client) -> std::span<std::uint32_t const> {
  return Framebuffer(Decoded(*client.Instance()));
}
auto UntilDesktop(Client& client, std::uint32_t width, std::uint32_t height) -> bool {
  return client.Until([&] { return client.DesktopSize() == Extent{ .width = width, .height = height }; });
}
auto SettingsOf(Client& client) -> SettingsView {
  Expects(client.Instance()->context != nullptr, "the client has a context");
  return SettingsView{ *client.Instance()->context->settings };
}
auto UntilMatches(Client& client, Pixels const& pixels) -> bool {
  return client.Until([&] { return client.Matches(pixels); });
}
}
