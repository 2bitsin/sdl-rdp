#include "_detail/test-graphics-session.hpp"

#include "_detail/test-has-cookie.hpp"

#include <algorithm>
#include <chrono>
#include <concepts>
#include <freerdp/settings.h>
#include <ranges>

namespace BackendGate {
namespace {
void ConnectConfirmed(Client& client, Logs& logs, std::invocable<Client&> auto connect) {
  connect(client);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }, std::chrono::seconds(30)))
      << logs.Text(true);
}
}
void GraphicsSession::ThenWriteDisconnect(Client& client) {
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  ASSERT_TRUE(freerdp_disconnect(client.Instance().get()));
  auto events = EventsUntil([](auto const& events) {
    return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_DISCONNECTED; });
  });
  EXPECT_NE(std::ranges::find(events, SDLRDP_DISCONNECTED, &sdlrdp_event::type), events.end());
  backend.reset();
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "")) << logs.Text(true);
  RecordProperty("trace", logs.Text(true));
}
void GraphicsSession::Open(unsigned w, unsigned h, sdlrdp_aspect aspect, sdlrdp_codec codec, unsigned audio_latency) {
  sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), w, h, 0, Logs::Collect, &logs };
  config.aspect           = aspect;
  config.codec            = codec;
  config.audio_latency_ms = audio_latency;
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0) << sdlrdp_last_error();
  backend.reset(handle);
}
Client& GraphicsSession::GraphicsClient() {
  return *graphics_client;
}
Headless::GraphicsObserver& GraphicsSession::GraphicsObserver() {
  return *graphics_observer;
}
void GraphicsSession::PresentProgressiveDamage(Client& client, std::vector<UINT32> const& pixels, sdlrdp_rect damage) {
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 640 * 4, 640, 480, &damage, 1), 0);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  EXPECT_LE(client.MaxError(pixels), 24u);
}
void GraphicsSession::ConnectPipeline(Client& client) {
  client.EnableGraphics();
  ConnectConfirmed(client, logs, [this](Client& connecting) { Connect(connecting); });
}
void GraphicsSession::ThenProgressivePicture(Client& client, std::vector<UINT32> const& pixels) {
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return Acknowledged(); }));
  EXPECT_LE(client.MaxError(pixels), 24u);
}
void GraphicsSession::GivenGraphicsClient(sdlrdp_codec codec) {
  Open(640, 480, { }, codec);
  if (::testing::Test::HasFatalFailure()) return;
  graphics_client = std::make_unique<Client>(sdlrdp_port(backend.get()), true, 640, 480);
  graphics_client->EnableGraphics();
}
void GraphicsSession::GivenPipelinedGraphics() {
  Open(320, 200, { }, SDLRDP_CODEC_PROGRESSIVE);
  if (::testing::Test::HasFatalFailure()) return;
  graphics_client = std::make_unique<Client>(sdlrdp_port(backend.get()), true);
  graphics_client->EnableGraphics();
  graphics_observer = std::make_unique<Headless::GraphicsObserver>(*graphics_client);
  ConnectGraphics(*graphics_client, *graphics_observer);
}
void GraphicsSession::ThenLegacyFallback(Client& client) {
  ASSERT_TRUE(freerdp_connect(client.Instance().get()));
  auto events    = EventsUntil(
      [](auto const& events) {
        return std::ranges::any_of(events, [](auto event) { return event.type == SDLRDP_CONNECTED; });
      },
      false, &client);
  auto connected = std::ranges::find(events, SDLRDP_CONNECTED, &sdlrdp_event::type);
  ASSERT_NE(connected, events.end()) << logs.Text();
  EXPECT_EQ(connected->connected.codec, SDLRDP_CODEC_RAW);
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_WARN, "GFX confirmation timed out"));
}
void GraphicsSession::PresentMatching(Client& client, std::vector<UINT32> const& pixels) {
  Present(pixels, 640, 480);
  if (::testing::Test::HasFatalFailure()) return;
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); }));
}
void GraphicsSession::PresentGraphicsFrames(Client& client, Headless::GraphicsObserver& observer,
                                            std::vector<UINT32> const& pixels, unsigned first, unsigned last) {
  std::ranges::for_each(std::views::iota(first, last + 1), [&](unsigned count) {
    Present(pixels, 320, 200);
    if (::testing::Test::HasFatalFailure()) return;
    AwaitFrames(client, observer.Observed().frames, count);
    if (::testing::Test::HasFatalFailure()) return;
  });
}
void GraphicsSession::ConnectGraphics(Client& client, Headless::GraphicsObserver& observer) {
  observer.Observed().automatic = false;
  ConnectConfirmed(client, logs, [this](Client& connecting) { Connect(connecting); });
}
void GraphicsSession::Connect(Client& client, bool ack) {
  ASSERT_TRUE(
      freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_FrameAcknowledge, ack ? 2 : 0));
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << logs.Text(true);
  ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
}
}
