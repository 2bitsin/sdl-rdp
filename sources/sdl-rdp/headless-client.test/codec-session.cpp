#include <sdl-rdp/headless-client.test/codec-session.hpp>

#include <sdl-rdp/headless-client.test/input-steps.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <string>

namespace BackendGate {
auto CodecSession::SetUp() -> void {
  sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), 320, 200, 0, Logs::Collect, &logs };
  config.codec = GetParam().codec;
  std::filesystem::remove_all(certificates.Path());
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  backend.reset(handle);
  ASSERT_NE(sdlrdp_port(handle), 0u);
  std::array<UINT32, 8> bars{ 0x00ffffff, 0x00ffff00, 0x0000ffff, 0x0000ff00, 0x00ff00ff, 0x00ff0000, 0x000000ff, 0 };
  std::ranges::generate(pixels, [&, index = 0u]() mutable {
    auto x = index % 320;
    auto y = index++ / 320;
    return x < 40 && y < 30 ? 0x00010101u : bars[x / 40];
  });
}
auto CodecSession::ConnectCodec(Client& client) -> void {
  client.Tolerance(GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0);
  ASSERT_TRUE(freerdp_connect(client.Instance().get())) << logs.Text(true);
}
auto CodecSession::Reopen(unsigned width, unsigned height) -> void {
  backend.reset();
  sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), width, height, 0 };
  config.codec = GetParam().codec;
  sdlrdp_handle* handle = nullptr;
  ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
  backend.reset(handle);
}
auto CodecSession::Frame(Client& client, sdlrdp_rect area) -> void {
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) && client.Matches(pixels); }))
      << logs.Text();
}
auto CodecSession::ThenMotion(sdlrdp_event const& event) -> void {
  EXPECT_EQ(event.type, SDLRDP_MOUSE_MOVE);
  EXPECT_EQ(event.mouse_move.x, 10);
  EXPECT_EQ(event.mouse_move.y, 20);
}
auto CodecSession::ThenPointerEvents(std::span<sdlrdp_event const> events) -> void {
  ThenMotion(events[2]);
  Headless::ThenMouseButtons(events);
  EXPECT_EQ(events[5].type, SDLRDP_MOUSE_WHEEL);
  EXPECT_EQ(events[5].mouse_wheel.dx, 0);
  EXPECT_EQ(events[5].mouse_wheel.dy, 1);
}
auto CodecSession::Input(Client& client) -> void {
  Headless::SendKeyboardAndMouse(client, 10, 20);
  if (::testing::Test::HasFatalFailure()) return;
  auto events = Events(6);
  ASSERT_EQ(events.size(), 6u);
  Headless::ThenKeyboard(events);
  ThenPointerEvents(events);
}
auto CodecSession::ThenChangedCodec(sdlrdp_codec expected) -> void {
  auto events = EventsUntil(
      [](auto const& events) {
        return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_CODEC_CHANGED; });
      },
      false);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, SDLRDP_CODEC_CHANGED);
  EXPECT_EQ(events[0].codec_changed.codec, expected);
}
auto CodecSession::ThenCodecChange(sdlrdp_codec expected, sdlrdp_codec previous) -> void {
  if (expected != previous) {
    ThenChangedCodec(expected);
  } else {
    std::array<sdlrdp_event, 4> events { };
    auto                        count  = sdlrdp_poll(backend.get(), events.data(), events.size());
    EXPECT_TRUE(std::ranges::all_of(std::span(events).first(count),
                                    [](auto const& event) { return event.type == SDLRDP_REFRESH; }));
  }
}
auto CodecSession::ThenConnected() -> void {
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  ASSERT_EQ(events[0].type, SDLRDP_CONNECTED);
  ThenConnectionDetails(events[0]);
}
auto CodecSession::ThenConnectionDetails(sdlrdp_event const& event) -> void {
  EXPECT_EQ(event.connected.width, 320u);
  EXPECT_EQ(event.connected.height, 200u);
  EXPECT_EQ(event.connected.bpp, 32u);
  EXPECT_EQ(event.connected.codec, GetParam().codec);
}
auto CodecSession::ThenDisconnected() -> void {
  auto events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
}
auto CodecSession::RecordFrameCost(Client& client, uint64_t bytes) -> void {
  RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
  RecordProperty("wire_bytes", std::to_string(client.Received() - bytes));
  RecordProperty("codec", std::to_string(GetParam().codec));
  RecordProperty("surface", bool(GetParam().surface) ? "true" : "false");
}
}
