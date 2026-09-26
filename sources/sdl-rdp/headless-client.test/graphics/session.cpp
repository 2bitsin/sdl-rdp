#include <sdl-rdp/headless-client.test/graphics/session.hpp>

#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/waits.hpp>
#include <sdl-rdp/link/event.hpp>

#include <freerdp/settings.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ranges>

namespace sdl_rdp::headless_client_test::graphics::detail::session {
using sdl_rdp::configuration::Codec;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged;
using sdl_rdp::headless_client_test::backend::ConnectWithCookie;
using sdl_rdp::headless_client_test::backend::Contains;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::backend::UntilLogged;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::client::UntilMatches;
using sdl_rdp::link::Disconnected;
using sdl_rdp::utilities::AspectRatio;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Rect;

auto GraphicsSession::ThenWriteDisconnect(Client& client) -> void {
  EXPECT_TRUE(backend.WaitFrame(std::chrono::milliseconds{ 0 }));
  ASSERT_TRUE(client.Disconnect());
  EXPECT_TRUE(Contains<Disconnected>(UntilEvent<Disconnected>()));
  backend.Close();
  EXPECT_FALSE(logs.Contains(LogLevel::Error, "")) << logs.Text(true);
  RecordProperty("trace", logs.Text(true));
}
auto GraphicsSession::Open(std::uint32_t w, std::uint32_t h, std::optional<AspectRatio> aspect, Codec codec,
                           std::uint32_t audio_latency) -> void {
  auto config = LoopbackConfig(certificates.Path(), { .width = w, .height = h });
  config.aspect           = aspect;
  config.codec            = codec;
  config.audio_latency_ms = audio_latency;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config, logs));
}
auto GraphicsSession::GraphicsClient() -> Client& {
  return *graphics_client;
}
auto GraphicsSession::Observer() -> GraphicsObserver& {
  return *graphics_observer;
}
auto GraphicsSession::PresentProgressiveDamage(Client& client, Pixels const& pixels, Rect damage) -> void {
  backend.Present(pixels, 640, 480, damage);
  ASSERT_NO_FATAL_FAILURE(AwaitAllAcknowledged(client, backend, logs));
  EXPECT_LE(client.MaxError(pixels), 24u);
}
auto GraphicsSession::ConnectPipeline(Client& client) -> void {
  client.EnableGraphics();
  ConnectConfirmed(client);
}
auto GraphicsSession::GivenGraphicsClient(Codec codec, Extent size) -> void {
  ASSERT_NO_FATAL_FAILURE(Open(size.width, size.height, { }, codec));
  graphics_client = std::make_unique<Client>(backend.Port(), true, size.width, size.height);
  graphics_client->EnableGraphics();
}
auto GraphicsSession::GivenPipelinedGraphics() -> void {
  ASSERT_NO_FATAL_FAILURE(GivenGraphicsClient(Codec::Progressive, { .width = 320, .height = 200 }));
  graphics_observer                       = std::make_unique<GraphicsObserver>(*graphics_client);
  graphics_observer->Observed().automatic = false;
  ConnectConfirmed(*graphics_client);
}
auto GraphicsSession::ThenLegacyFallback(Client& client) -> void {
  ASSERT_TRUE(client.Connect());
  ASSERT_NO_FATAL_FAILURE(ThenConnectedCodec(client, Codec::Raw));
  EXPECT_TRUE(logs.Contains(LogLevel::Warn, "GFX confirmation timed out"));
}
auto GraphicsSession::PresentMatching(Client& client, Pixels const& pixels) -> void {
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  ASSERT_TRUE(UntilMatches(client, pixels)) << logs.Text(true);
}
auto GraphicsSession::PresentGraphicsFrames(Client& client, GraphicsObserver& observer, Pixels const& pixels,
                                            std::uint32_t first, std::uint32_t last) -> void {
  std::ranges::for_each(std::views::iota(first, last + 1), [&](std::size_t count) {
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
    ASSERT_NO_FATAL_FAILURE(AwaitFrames(client, observer.Observed().frames, count));
  });
}
auto GraphicsSession::ConnectConfirmed(Client& client) -> void {
  ASSERT_NO_FATAL_FAILURE(Connect(client));
  ASSERT_TRUE(UntilLogged(client, logs, "GFX confirmed", std::chrono::seconds(30))) << logs.Text(true);
}
auto GraphicsSession::Connect(Client& client, bool ack) -> void {
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge, ack ? 2 : 0));
  ConnectWithCookie(client, logs);
}
auto GraphicsSession::ShowFirstPicture(Client& client, Pixels const& pixels) -> void {
  ASSERT_NO_FATAL_FAILURE(Connect(client, false));
  ASSERT_NO_FATAL_FAILURE(PresentMatching(client, pixels));
}
}
