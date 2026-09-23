#include "_detail/gfx-protocol.hpp"
#include "_detail/avc.hpp"
#include <freerdp/primitives.h>
#include <iostream>
#include <gtest/gtest.h>
#include <array>
#include "_detail/headless-gfx.hpp"
#include "_detail/headless-tls.hpp"
#include "_detail/test-logs.hpp"
#include "_detail/test-pattern.hpp"
#include <filesystem>
#include "_detail/state.hpp"
#include <regex>
#include <random>

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
TEST_F(GraphicsResize, RawAspectMatchesBilinear) {
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_RAW), 0);
  ASSERT_EQ(sdlrdp_set_aspect(backend.get(), {4, 3}), 0);
  Headless::Client client(sdlrdp_port(backend.get()), true, 320, 240);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  std::vector<UINT32> pixels(320 * 200);
  std::mt19937 random(17);
  std::ranges::generate(pixels, [&] { return random() & 0x00ffffff; });
  sdlrdp_rect full{0, 0, 320, 200};
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
  ASSERT_TRUE(client.Until([&] { return !observer.frames.empty(); })) << logs.Text(true);
  auto gdi = client.instance->context->gdi;
  ASSERT_EQ(gdi->width, 320); ASSERT_EQ(gdi->height, 240);
  ASSERT_EQ(observer.frames.size(), 1u);
  for (int y = 0; y < 240; ++y) {
    auto position = std::clamp((y + 0.5) * (200.0 / 240) - 0.5, 0.0, 199.0);
    auto first = unsigned(position), second = std::min(first + 1, 199u);
    auto weight = float(position - first);
    auto actual = reinterpret_cast<UINT32 const*>(gdi->primary_buffer + y * gdi->stride);
    for (int x = 0; x < 320; ++x) {
      UINT32 expected = 0;
      for (unsigned c = 0; c < 3; ++c) {
        float a = (pixels[first * 320 + x] >> (c * 8)) & 255;
        float b = (pixels[second * 320 + x] >> (c * 8)) & 255;
        expected |= UINT32(BYTE(a + (b - a) * weight + 0.5f)) << (c * 8);
      }
      ASSERT_EQ(actual[x] & 0xffffff, expected) << x << ',' << y;
    }
  }
}
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

namespace {
class GraphicsCost : public testing::Test {
protected:
  Headless::Logs logs;
  std::filesystem::path directory;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend{nullptr, sdlrdp_close};
  void Open(unsigned width = 1280, unsigned height = 800, sdlrdp_codec codec = SDLRDP_CODEC_PROGRESSIVE) {
    char pattern[] = "/tmp/sdlrdp-cost-XXXXXX";
    auto path = mkdtemp(pattern);
    ASSERT_NE(path, nullptr);
    directory = path;
    sdlrdp_config config{"127.0.0.1", 0, directory.c_str(), width, height, 0, Headless::Logs::Collect, &logs};
    config.codec = codec;
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0);
    backend.reset(handle);
    Headless::InitializeTls(sdlrdp_port(handle));
  }
  void TearDown() override {
    backend.reset();
    if (!directory.empty()) std::filesystem::remove_all(directory);
  }
};
TEST_F(GraphicsCost, FullRandomFrame) {
  ASSERT_NO_FATAL_FAILURE(Open());
  Headless::Client client(sdlrdp_port(backend.get()), true, 1280, 800);
  client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  EXPECT_EQ(client.instance->context->codecs->ThreadingFlags, THREADING_FLAGS_DISABLE_THREADS);
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> pixels(1280 * 800);
  std::mt19937 random(17);
  std::ranges::generate(pixels, [&] { return random() & 0x00ffffff; });
  sdlrdp_rect full{0, 0, 1280, 800};
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 5120, 1280, 800, &full, 1), 0);
  ASSERT_TRUE(client.Until([&] {
    std::scoped_lock lock(backend->state->frame_guard);
    return backend->state->current->acknowledged == 1;
  }));
  ASSERT_EQ(observer.frames.size(), 1u);
  freerdp_disconnect(client.instance.get());
  backend.reset();
  auto text = logs.Text(true);
  std::smatch match;
  ASSERT_TRUE(std::regex_search(text, match, std::regex(
    R"(Frames: 1 sent, 0 coalesced; encode ([0-9.]+) ms mean, ([0-9.]+) ms max; acknowledgement ([0-9.]+) ms mean, ([0-9.]+) ms max, ([0-9]+) over 100 ms\.)"))) << text;
  auto milliseconds = std::stod(match[1]);
  RecordProperty("encode_ms", milliseconds);
  RecordProperty("statistics", match.str());
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "Frames:"), 1u);
  EXPECT_EQ(match[1], match[2]);
  EXPECT_EQ(match[3], match[4]);
  // measured 2026-09-23 on the dev box, 88 cores: 138 ms without threads, 228 ms with the WinPR pool
  EXPECT_LT(milliseconds, 200.0);
}
void RecordAvcCost(Headless::Logs& logs) {
  auto text = logs.Text(true);
  std::smatch match;
  ASSERT_TRUE(std::regex_search(text, match, std::regex(
    R"(Frames: 10 sent, 0 coalesced; encode ([0-9.]+) ms mean, ([0-9.]+) ms max \(convert ([0-9.]+), upload ([0-9.]+), nvenc ([0-9.]+)\); acknowledgement)"))) << text;
  for (auto [name, index] : {std::pair{"encode_ms", 1}, {"convert_ms", 3}, {"upload_ms", 4}, {"nvenc_ms", 5}})
    testing::Test::RecordProperty(name, match[index].str());
  testing::Test::RecordProperty("statistics", match.str());
  // measured 2026-09-23 on an RTX 3090 at 1920x1080: 8.8 ms mean, 16.4 ms max after the row-copy dispatch (45.2 before)
  EXPECT_LT(std::stod(match[1]), 20.0);
  std::cout << match.str() << '\n';
}
TEST_F(GraphicsCost, AvcFullFrame) {
  auto selected = primitives_get();
  auto generic = primitives_get_generic();
  auto cpu = primitives_get_by_type(PRIMITIVES_ONLY_CPU);
  ASSERT_NE(selected, nullptr); ASSERT_NE(generic, nullptr); ASSERT_NE(cpu, nullptr);
  RecordProperty("primitives_flags", std::to_string(primitives_flags(selected)));
  RecordProperty("primitives_extcpu", bool(primitives_flags(selected) & PRIM_FLAGS_HAVE_EXTCPU));
  RecordProperty("rgb_to_yuv420_generic", selected->RGBToYUV420_8u_P3AC4R == generic->RGBToYUV420_8u_P3AC4R);
  RecordProperty("rgb_to_yuv420_cpu_optimized", cpu->RGBToYUV420_8u_P3AC4R != generic->RGBToYUV420_8u_P3AC4R);
  std::cout << "FreeRDP primitives flags: " << primitives_flags(selected)
    << "; RGBToYUV420 generic: " << (selected->RGBToYUV420_8u_P3AC4R == generic->RGBToYUV420_8u_P3AC4R)
    << "; CPU optimized available: " << (cpu->RGBToYUV420_8u_P3AC4R != generic->RGBToYUV420_8u_P3AC4R) << '\n';
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  ASSERT_NO_FATAL_FAILURE(Open(1920, 1080, SDLRDP_CODEC_AVC420));
  Headless::Client client(sdlrdp_port(backend.get()), true, 1920, 1080);
  client.EnableGraphics(true);
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> pixels(1920 * 1080);
  sdlrdp_rect full{0, 0, 1920, 1080};
  for (unsigned frame = 0; frame < 10; ++frame) {
    Headless::MovingTilePattern(pixels, 1920, 1080, frame);
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 7680, 1920, 1080, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] {
      std::scoped_lock lock(backend->state->frame_guard);
      return backend->state->current->acknowledged == frame + 1;
    })) << logs.Text(true);
  }
  ASSERT_EQ(observer.avc_nals.size(), 10u);
  ASSERT_EQ(observer.frames.size(), 10u);
  freerdp_disconnect(client.instance.get());
  backend.reset();
  RecordAvcCost(logs);
}

}
