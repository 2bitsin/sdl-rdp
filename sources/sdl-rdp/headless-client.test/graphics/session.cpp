#include <sdl-rdp/headless-client.test/graphics/session.hpp>

#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/client/has-cookie.hpp>
#include <sdl-rdp/link/event.hpp>

#include <freerdp/settings.h>
#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>

namespace sdl_rdp::headless_client_test::graphics::detail::session {
using sdl_rdp::configuration::Codec;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged;
using sdl_rdp::headless_client_test::backend::Contains;
using sdl_rdp::headless_client_test::backend::Logs;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::client::HasCookie;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::link::Disconnected;
using sdl_rdp::utilities::AspectRatio;
using sdl_rdp::utilities::Rect;

namespace {
auto ConnectConfirmed(Client& client, Logs& logs, std::invocable<Client&> auto connect) -> void {
  ASSERT_NO_FATAL_FAILURE(connect(client));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }, std::chrono::seconds(30)))
      << logs.Text(true);
}
}
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
  ConnectConfirmed(client, logs, [this](Client& connecting) { Connect(connecting); });
}
auto GraphicsSession::GivenGraphicsClient(Codec codec) -> void {
  ASSERT_NO_FATAL_FAILURE(Open(640, 480, { }, codec));
  graphics_client = std::make_unique<Client>(backend.Port(), true, 640, 480);
  graphics_client->EnableGraphics();
}
auto GraphicsSession::GivenPipelinedGraphics() -> void {
  ASSERT_NO_FATAL_FAILURE(Open(320, 200, { }, Codec::Progressive));
  graphics_client = std::make_unique<Client>(backend.Port(), true);
  graphics_client->EnableGraphics();
  graphics_observer = std::make_unique<GraphicsObserver>(*graphics_client);
  ConnectGraphics(*graphics_client, *graphics_observer);
}
auto GraphicsSession::ThenLegacyFallback(Client& client) -> void {
  ASSERT_TRUE(client.Connect());
  ASSERT_NO_FATAL_FAILURE(ThenConnectedCodec(client, Codec::Raw));
  EXPECT_TRUE(logs.Contains(LogLevel::Warn, "GFX confirmation timed out"));
}
auto GraphicsSession::PresentMatching(Client& client, Pixels const& pixels) -> void {
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
}
auto GraphicsSession::PresentGraphicsFrames(Client& client, GraphicsObserver& observer, Pixels const& pixels,
                                            std::uint32_t first, std::uint32_t last) -> void {
  std::ranges::for_each(std::views::iota(first, last + 1), [&](std::size_t count) {
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
    ASSERT_NO_FATAL_FAILURE(AwaitFrames(client, observer.Observed().frames, count));
  });
}
auto GraphicsSession::ConnectGraphics(Client& client, GraphicsObserver& observer) -> void {
  observer.Observed().automatic = false;
  ConnectConfirmed(client, logs, [this](Client& connecting) { Connect(connecting); });
}
auto GraphicsSession::Connect(Client& client, bool ack) -> void {
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge, ack ? 2 : 0));
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
}
auto GraphicsSession::ShowFirstPicture(Client& client, Pixels const& pixels) -> void {
  ASSERT_NO_FATAL_FAILURE(Connect(client, false));
  ASSERT_NO_FATAL_FAILURE(PresentMatching(client, pixels));
}
}
