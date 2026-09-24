#include <sdl-rdp/headless-client.test/graphics-session.hpp>
#include <sdl-rdp/headless-client.test/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend-instance.hpp>

#include <sdl-rdp/headless-client.test/has-cookie.hpp>

#include <freerdp/settings.h>
#include <algorithm>
#include <chrono>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <ranges>

namespace BackendGate {
namespace {
auto ConnectConfirmed(Client& client, Logs& logs, std::invocable<Client&> auto connect) -> void {
  ASSERT_NO_FATAL_FAILURE(connect(client));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }, std::chrono::seconds(30)))
      << logs.Text(true);
}
}
auto GraphicsSession::ThenWriteDisconnect(Client& client) -> void {
  EXPECT_EQ(sdlrdp_wait_frame(backend.Handle(), 0), 1);
  ASSERT_TRUE(client.Disconnect());
  EXPECT_TRUE(std::ranges::contains(UntilEvent(SDLRDP_DISCONNECTED), SDLRDP_DISCONNECTED, &sdlrdp_event::type));
  backend.Close();
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "")) << logs.Text(true);
  RecordProperty("trace", logs.Text(true));
}
auto GraphicsSession::Open(std::uint32_t w, std::uint32_t h, sdlrdp_aspect aspect, sdlrdp_codec codec,
                           std::uint32_t audio_latency) -> void {
  sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), w, h, 0, Logs::Collect, &logs };
  config.aspect           = aspect;
  config.codec            = codec;
  config.audio_latency_ms = audio_latency;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config));
}
auto GraphicsSession::GraphicsClient() -> Client& {
  return *graphics_client;
}
auto GraphicsSession::GraphicsObserver() -> Headless::GraphicsObserver& {
  return *graphics_observer;
}
auto GraphicsSession::PresentProgressiveDamage(Client& client, std::vector<std::uint32_t> const& pixels,
                                               sdlrdp_rect damage) -> void {
  ASSERT_EQ(backend.Present(pixels, 640, 480, damage), 0);
  ASSERT_NO_FATAL_FAILURE(sdl_rdp::headless_client_test::AwaitAllAcknowledged(client, backend, logs));
  EXPECT_LE(client.MaxError(pixels), 24u);
}
auto GraphicsSession::ConnectPipeline(Client& client) -> void {
  client.EnableGraphics();
  ConnectConfirmed(client, logs, [this](Client& connecting) { Connect(connecting); });
}
auto GraphicsSession::GivenGraphicsClient(sdlrdp_codec codec) -> void {
  ASSERT_NO_FATAL_FAILURE(Open(640, 480, { }, codec));
  graphics_client = std::make_unique<Client>(sdlrdp_port(backend.Handle()), true, 640, 480);
  graphics_client->EnableGraphics();
}
auto GraphicsSession::GivenPipelinedGraphics() -> void {
  ASSERT_NO_FATAL_FAILURE(Open(320, 200, { }, SDLRDP_CODEC_PROGRESSIVE));
  graphics_client = std::make_unique<Client>(sdlrdp_port(backend.Handle()), true);
  graphics_client->EnableGraphics();
  graphics_observer = std::make_unique<Headless::GraphicsObserver>(*graphics_client);
  ConnectGraphics(*graphics_client, *graphics_observer);
}
auto GraphicsSession::ThenLegacyFallback(Client& client) -> void {
  ASSERT_TRUE(client.Connect());
  ASSERT_NO_FATAL_FAILURE(ThenConnectedCodec(client, SDLRDP_CODEC_RAW));
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "GFX confirmation timed out"));
}
auto GraphicsSession::PresentMatching(Client& client, std::vector<std::uint32_t> const& pixels) -> void {
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
}
auto GraphicsSession::PresentGraphicsFrames(Client& client, Headless::GraphicsObserver& observer,
                                            std::vector<std::uint32_t> const& pixels, std::uint32_t first,
                                            std::uint32_t last) -> void {
  std::ranges::for_each(std::views::iota(first, last + 1), [&](std::size_t count) {
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
    ASSERT_NO_FATAL_FAILURE(AwaitFrames(client, observer.Observed().frames, count));
  });
}
auto GraphicsSession::ConnectGraphics(Client& client, Headless::GraphicsObserver& observer) -> void {
  observer.Observed().automatic = false;
  ConnectConfirmed(client, logs, [this](Client& connecting) { Connect(connecting); });
}
auto GraphicsSession::Connect(Client& client, bool ack) -> void {
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge, ack ? 2 : 0));
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
}
auto GraphicsSession::ShowFirstPicture(Client& client, std::vector<std::uint32_t> const& pixels) -> void {
  ASSERT_NO_FATAL_FAILURE(Connect(client, false));
  ASSERT_NO_FATAL_FAILURE(PresentMatching(client, pixels));
}
}
