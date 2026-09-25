#include <sdl-rdp/headless-client.test/codec/session.hpp>

#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/headless-client.test/backend/config.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/input/steps.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <utility>

namespace sdl_rdp::headless_client_test::codec::detail::session {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::backend::As;
using sdl_rdp::headless_client_test::backend::Holds;
using sdl_rdp::headless_client_test::backend::LoopbackConfig;
using sdl_rdp::headless_client_test::input::SendKeyboardAndMouse;
using sdl_rdp::headless_client_test::input::ThenKeyboard;
using sdl_rdp::headless_client_test::input::ThenMouseButtons;
using sdl_rdp::link::CodecChanged;
using sdl_rdp::link::Connected;
using sdl_rdp::link::Disconnected;
using sdl_rdp::link::Event;
using sdl_rdp::link::MouseMove;
using sdl_rdp::link::MouseWheel;
using sdl_rdp::link::RefreshChanged;
using sdl_rdp::utilities::Rect;

auto CodecSession::SetUp() -> void {
  auto config = LoopbackConfig(certificates.Path());
  config.codec = GetParam().codec;
  std::filesystem::remove_all(certificates.Path());
  ASSERT_NO_FATAL_FAILURE(backend.Open(config, logs));
  ASSERT_NE(backend.Port(), 0u);
  std::array<std::uint32_t, 8> bars{ 0x00ffffff, 0x00ffff00, 0x0000ffff, 0x0000ff00,
                                     0x00ff00ff, 0x00ff0000, 0x000000ff, 0 };
  std::ranges::generate(pixels, [&, index = 0u]() mutable {
    auto x = index % 320;
    auto y = index++ / 320;
    return x < 40 && y < 30 ? 0x00010101u : bars[x / 40];
  });
}
auto CodecSession::ConnectCodec(Client& client) -> void {
  client.Tolerance(CodecTolerance(GetParam().codec, GetParam().surface));
  ASSERT_TRUE(client.Connect()) << logs.Text(true);
}
auto CodecSession::Reopen(std::uint32_t width, std::uint32_t height) -> void {
  backend.Close();
  auto config = LoopbackConfig(certificates.Path(), { .width = width, .height = height });
  config.codec = GetParam().codec;
  ASSERT_NO_FATAL_FAILURE(backend.Open(config, logs));
}
auto CodecSession::Frame(Client& client, Rect area) -> void {
  backend.Present(pixels, 320, 200, area);
  ASSERT_TRUE(client.Until([&] { return backend.WaitFrame(std::chrono::milliseconds{ 0 }) && client.Matches(pixels); }))
      << logs.Text();
}
auto CodecSession::ThenMotion(Event const& event) -> void {
  auto const& motion = As<MouseMove>(event);
  EXPECT_EQ(motion.x, 10);
  EXPECT_EQ(motion.y, 20);
}
auto CodecSession::ThenPointerEvents(std::span<Event const> events) -> void {
  ThenMotion(events[2]);
  ThenMouseButtons(events);
  auto const& wheel = As<MouseWheel>(events[5]);
  EXPECT_EQ(wheel.dx, 0);
  EXPECT_EQ(wheel.dy, 1);
}
auto CodecSession::Input(Client& client) -> void {
  ASSERT_NO_FATAL_FAILURE(SendKeyboardAndMouse(client, 10, 20));
  auto events = Events(6);
  ASSERT_EQ(events.size(), 6u);
  ThenKeyboard(events);
  ThenPointerEvents(events);
}
auto CodecSession::ThenChangedCodec(Codec expected) -> void {
  auto events = UntilEvent<CodecChanged>(false);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(As<CodecChanged>(events[0]).codec, expected);
}
auto CodecSession::ThenCodecChange(Codec expected, Codec previous) -> void {
  if (expected != previous) {
    ASSERT_NO_FATAL_FAILURE(ThenChangedCodec(expected));
  } else {
    EXPECT_TRUE(std::ranges::all_of(backend.Poll(), Holds<RefreshChanged>));
  }
}
auto CodecSession::ThenConnected() -> void {
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  ThenConnectionDetails(events[0]);
}
auto CodecSession::ThenConnectionDetails(Event const& event) -> void {
  auto const& connected = As<Connected>(event);
  EXPECT_EQ(connected.width, 320u);
  EXPECT_EQ(connected.height, 200u);
  EXPECT_EQ(connected.bpp, 32u);
  EXPECT_EQ(connected.codec, GetParam().codec);
}
auto CodecSession::ThenDisconnected() -> void {
  auto events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  ASSERT_TRUE(Holds<Disconnected>(events[0]));
}
auto CodecSession::RecordFrameCost(Client& client, std::uint64_t bytes) -> void {
  RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
  RecordProperty("wire_bytes", std::to_string(client.Received() - bytes));
  RecordProperty("codec", std::to_string(std::to_underlying(GetParam().codec)));
  RecordProperty("surface", GetParam().surface ? "true" : "false");
}
}
