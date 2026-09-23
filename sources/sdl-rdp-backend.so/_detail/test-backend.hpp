#pragma once
#include "test-backend-core.hpp"
#include "test-input-steps.hpp"
namespace BackendGate {
class CodecSession : public testing::TestWithParam<Mode>, protected BackendEvents {
protected:
  void SetUp() override {
    sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), 320, 200, 0, Logs::Collect, &logs };
    config.codec = GetParam().codec;
    static std::once_flag tls_initialized;
    std::call_once(tls_initialized, [&] { InitializeTls(config); });
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
  void ConnectCodec(Client& client) {
    client.Tolerance(GetParam().codec == SDLRDP_CODEC_REMOTEFX ? 40 : GetParam().codec == SDLRDP_CODEC_NSCODEC ? 3 : 0);
    ASSERT_TRUE(freerdp_connect(client.Instance().get())) << logs.Text(true);
  }
  void Reopen(unsigned width, unsigned height) {
    backend.reset();
    sdlrdp_config config{ "127.0.0.1", 0, certificates.Path().c_str(), width, height, 0 };
    config.codec = GetParam().codec;
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
    backend.reset(handle);
  }
  void Frame(Client& client, sdlrdp_rect area) {
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
    ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) && client.Matches(pixels); }))
        << logs.Text();
  }
  static void ThenMotion(sdlrdp_event const& event) {
    EXPECT_EQ(event.type, SDLRDP_MOUSE_MOVE);
    EXPECT_EQ(event.mouse_move.x, 10);
    EXPECT_EQ(event.mouse_move.y, 20);
  }
  static void ThenPointerEvents(std::span<sdlrdp_event const> events) {
    ThenMotion(events[2]);
    Headless::ThenMouseButtons(events);
    EXPECT_EQ(events[5].type, SDLRDP_MOUSE_WHEEL);
    EXPECT_EQ(events[5].mouse_wheel.dx, 0);
    EXPECT_EQ(events[5].mouse_wheel.dy, 1);
  }
  void Input(Client& client) {
    Headless::SendKeyboardAndMouse(client, 10, 20);
    if (::testing::Test::HasFatalFailure()) return;
    auto events = Events(6);
    ASSERT_EQ(events.size(), 6u);
    Headless::ThenKeyboard(events);
    ThenPointerEvents(events);
  }
  void ThenChangedCodec(sdlrdp_codec expected) {
    auto events = EventsUntil(
        [](auto const& events) {
          return std::ranges::any_of(events, [](auto const& event) { return event.type == SDLRDP_CODEC_CHANGED; });
        },
        false);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, SDLRDP_CODEC_CHANGED);
    EXPECT_EQ(events[0].codec_changed.codec, expected);
  }
  void ThenCodecChange(sdlrdp_codec expected, sdlrdp_codec previous) {
    if (expected != previous) {
      ThenChangedCodec(expected);
    } else {
      std::array<sdlrdp_event, 4> events { };
      auto                        count  = sdlrdp_poll(backend.get(), events.data(), events.size());
      EXPECT_TRUE(std::ranges::all_of(std::span(events).first(count),
                                      [](auto const& event) { return event.type == SDLRDP_REFRESH; }));
    }
  }
  void ThenConnected() {
    auto events = Events(2);
    ASSERT_EQ(events.size(), 2u);
    ASSERT_EQ(events[0].type, SDLRDP_CONNECTED);
    ThenConnectionDetails(events[0]);
  }
  static void ThenConnectionDetails(sdlrdp_event const& event) {
    EXPECT_EQ(event.connected.width, 320u);
    EXPECT_EQ(event.connected.height, 200u);
    EXPECT_EQ(event.connected.bpp, 32u);
    EXPECT_EQ(event.connected.codec, GetParam().codec);
  }
  void ThenDisconnected() {
    auto events = Events(1);
    ASSERT_EQ(events.size(), 1u);
    EXPECT_EQ(events[0].type, SDLRDP_DISCONNECTED);
  }
  void RecordFrameCost(Client& client, uint64_t bytes) {
    RecordProperty("max_channel_error", std::to_string(client.MaxError(pixels)));
    RecordProperty("wire_bytes", std::to_string(client.Received() - bytes));
    RecordProperty("codec", std::to_string(GetParam().codec));
    RecordProperty("surface", bool(GetParam().surface) ? "true" : "false");
  }
  std::vector<UINT32> pixels = std::vector<UINT32>(320uz * 200);
};
class Gate : public CodecSession {
protected:
  void ThenWaitingOpenCompletes(std::future<int>& opening, sdlrdp_handle* handle) {
    ASSERT_EQ(opening.wait_for(std::chrono::seconds(10)), std::future_status::ready);
    ASSERT_EQ(opening.get(), 0);
    backend.reset(handle);
    EXPECT_EQ(sdlrdp_wait(handle, 0), 1);
    sdlrdp_event event{ };
    ASSERT_EQ(sdlrdp_poll(handle, &event, 1), 1u);
    EXPECT_EQ(event.type, SDLRDP_CONNECTED);
  }
  void ThenPictureDesktop(Client const& client) {
    EXPECT_EQ(client.Instance()->context->gdi->width, 640);
    EXPECT_EQ(client.Instance()->context->gdi->height, 480);
    EXPECT_FALSE(logs.Contains("failed"));
  }
  void WhenBurstPictures(Client& client, sdlrdp_rect area) {
    auto before = ResidentBytes();
    for (unsigned frame = 0; frame < 200; ++frame) {
      std::ranges::fill(pixels, 0x00010101u * (frame + 1));
      ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
    }
    auto after = ResidentBytes();
    RecordProperty("burst_rss_growth", std::to_string(std::int64_t(after) - std::int64_t(before)));
    EXPECT_LE(after, before + (pixels.size() * 16));
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
  }
  static void ThenInitialScreen(sdlrdp_event const& event) {
    EXPECT_EQ(event.type, SDLRDP_SCREEN);
    EXPECT_EQ(event.screen.width, 320u);
    EXPECT_EQ(event.screen.height, 200u);
  }
  void ThenResizedConnection() {
    auto events = Events(2);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].type, SDLRDP_CONNECTED);
    EXPECT_EQ(events[0].connected.width, 640u);
    ThenInitialScreen(events[1]);
  }
  void ThenCleanDisconnect() {
    backend.reset();
    EXPECT_TRUE(logs.Contains("accepted"));
    EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "disconnected"));
    EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "Peer transport failed")) << logs.Text();
  }
};

inline std::string ModeName(testing::TestParamInfo<Mode> const& info) {
  Expects(info.param.codec >= SDLRDP_CODEC_AUTO, "codec is at least the first enumerator");
  Expects(info.param.codec <= SDLRDP_CODEC_AVC420, "codec does not exceed the final enumerator");
  constexpr std::array names{ "Auto", "Planar", "RemoteFX", "NSCodec", "Raw", "Progressive", "Avc420" };
  return std::string(names[info.param.codec]) + (info.param.surface ? "Surface" : "Bitmap");
}
using Headless::FrameObserver;

}

#include "test-backend-graphics.hpp"
