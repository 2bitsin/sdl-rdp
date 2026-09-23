#include "_detail/gfx-protocol.hpp"
#include <gtest/gtest.h>
#include <array>
#include "_detail/headless-gfx.hpp"
#include "_detail/headless-tls.hpp"
#include "_detail/test-logs.hpp"
#include <filesystem>

namespace {
TEST(GraphicsCapability, HighestSupportedVersion) {
  std::array<RDPGFX_CAPSET, 4> caps{{{RDPGFX_CAPVERSION_81, 4, 0},
    {0xffffffff, 16, 0}, {RDPGFX_CAPVERSION_107, 4, 0}, {RDPGFX_CAPVERSION_10, 4, 0}}};
  EXPECT_EQ(Backend::SelectCapability(caps).version, RDPGFX_CAPVERSION_107);
  std::reverse(caps.begin(), caps.end());
  EXPECT_EQ(Backend::SelectCapability(caps).version, RDPGFX_CAPVERSION_107);
  EXPECT_EQ(Backend::SelectCapability({}).version, 0u);
  RDPGFX_CAPSET unknown{0xffffffff, 16, 0};
  EXPECT_EQ(Backend::SelectCapability({&unknown, 1}).version, 0u);
}
TEST(GraphicsCapability, Version101ReservedLength) {
  std::array<RDPGFX_CAPSET, 2> caps{{{RDPGFX_CAPVERSION_10, 4, 0}, {RDPGFX_CAPVERSION_101, 4, 0xffffffff}}};
  EXPECT_EQ(Backend::SelectCapability(caps).version, RDPGFX_CAPVERSION_10);
  caps.back().length = 16;
  auto selected = Backend::SelectCapability(caps);
  EXPECT_EQ(selected.version, RDPGFX_CAPVERSION_101);
  EXPECT_EQ(selected.length, 16u);
  EXPECT_EQ(selected.flags, 0u);
}
TEST(GraphicsCapability, MasksFlagsAndDisablesAvc) {
  constexpr auto handled = RDPGFX_CAPS_FLAG_THINCLIENT | RDPGFX_CAPS_FLAG_SMALL_CACHE
    | RDPGFX_CAPS_FLAG_SCALEDMAP_DISABLE;
  for (UINT32 version : {RDPGFX_CAPVERSION_8, RDPGFX_CAPVERSION_81, RDPGFX_CAPVERSION_10,
      RDPGFX_CAPVERSION_102, RDPGFX_CAPVERSION_107}) {
    RDPGFX_CAPSET cap{version, 4, 0xffffffff};
    auto selected = Backend::SelectCapability({&cap, 1});
    EXPECT_EQ(selected.flags, handled | (version >= RDPGFX_CAPVERSION_10 ? RDPGFX_CAPS_FLAG_AVC_DISABLED : 0));
    EXPECT_EQ(selected.length, 4u);
    cap.length = 3;
    EXPECT_EQ(Backend::SelectCapability({&cap, 1}).version, 0u);
  }
}
TEST(GraphicsTimestamp, PacksIndependentFields) {
  SYSTEMTIME time{};
  EXPECT_EQ(Backend::FrameTimestamp(time), 0u);
  time.wHour = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00400000u);
  time.wHour = 0; time.wMinute = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00010000u);
  time.wMinute = 0; time.wSecond = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00000400u);
  time.wSecond = 0; time.wMilliseconds = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 1u);
  time.wHour = 23; time.wMinute = 59; time.wSecond = 59; time.wMilliseconds = 999;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x05fbefe7u);
}
}

namespace {
class GraphicsResize : public testing::Test {
protected:
  std::filesystem::path certificates;
  Headless::Logs logs;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend{nullptr, sdlrdp_close};
  void SetUp() override {
    char path[] = "/tmp/sdlrdp-gfx-resize-XXXXXX";
    ASSERT_NE(mkdtemp(path), nullptr);
    certificates = path;
    sdlrdp_config config{"127.0.0.1", 0, certificates.c_str(), 640, 480, 0, Headless::Logs::Collect, &logs};
    config.codec = SDLRDP_CODEC_PROGRESSIVE;
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
    backend.reset(handle);
    Headless::InitializeTls(sdlrdp_port(handle));
  }
  void TearDown() override {
    backend.reset();
    if (!certificates.empty()) std::filesystem::remove_all(certificates);
  }
};
TEST_F(GraphicsResize, ProgressiveContextAndFullDamage) {
  Headless::Client client(sdlrdp_port(backend.get()), true, 640, 480);
  client.EnableGraphics();
  client.tolerance = 24;
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  unsigned generations = 0;
  for (auto [w, h] : {std::pair{640u, 480u}, std::pair{320u, 200u}, std::pair{640u, 480u}}) {
    std::vector<UINT32> pixels(w * h, 0x335577 + generations * 0x221100);
    SCOPED_TRACE(std::to_string(w) + "x" + std::to_string(h));
    sdlrdp_rect damage{0, 0, int(w), int(h)};
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), w * 4, w, h, &damage, 1), 0);
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
    EXPECT_EQ(observer.progressive_headers, ++generations);
    EXPECT_EQ(observer.deleted, generations - 1);
    ASSERT_EQ(observer.surfaces.size(), generations);
    EXPECT_EQ(observer.surfaces.back().width, w); EXPECT_EQ(observer.surfaces.back().height, h);
    auto frames = observer.frames.size();
    damage = {0, 0, 1, 1};
    pixels.front() ^= 0x222222;
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), w * 4, w, h, &damage, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() > frames; })) << logs.Text(true);
    EXPECT_LE(client.MaxError(pixels), client.tolerance);
    EXPECT_EQ(observer.progressive_headers, generations);
  }
}
}
