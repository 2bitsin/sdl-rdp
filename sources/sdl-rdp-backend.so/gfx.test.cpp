#include <cstddef>
#include "_detail/gfx-protocol.hpp"
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include "_detail/headless-gfx.hpp"
#include "_detail/headless-tls.hpp"
#include "_detail/test-logs.hpp"
#include "_detail/test-pattern.hpp"
#include "_detail/state.hpp"
#include <filesystem>

namespace {
TEST(GraphicsCapability, HighestSupportedVersion)
{
  std::array<RDPGFX_CAPSET, 4> caps{
    { { .version = RDPGFX_CAPVERSION_81, .length = 4, .flags = 0 },
     { .version = 0xffffffff, .length = 16, .flags = 0 },
     { .version = RDPGFX_CAPVERSION_107, .length = 4, .flags = 0 },
     { .version = RDPGFX_CAPVERSION_10, .length = 4, .flags = 0 } }
  };
  EXPECT_EQ(Backend::SelectCapability(caps).version, RDPGFX_CAPVERSION_107);
  std::ranges::reverse(caps);
  EXPECT_EQ(Backend::SelectCapability(caps).version, RDPGFX_CAPVERSION_107);
  EXPECT_EQ(Backend::SelectCapability({}).version, 0u);
  RDPGFX_CAPSET unknown{ 0xffffffff, 16, 0 };
  EXPECT_EQ(Backend::SelectCapability({ &unknown, 1 }).version, 0u);
}
TEST(GraphicsCapability, Version101ReservedLength)
{
  std::array<RDPGFX_CAPSET, 2> caps{
    { { .version = RDPGFX_CAPVERSION_10, .length = 4, .flags = 0 }, { .version = RDPGFX_CAPVERSION_101, .length = 4, .flags = 0xffffffff } }
  };
  EXPECT_EQ(Backend::SelectCapability(caps).version, RDPGFX_CAPVERSION_10);
  caps.back().length = 16;
  auto selected = Backend::SelectCapability(caps);
  EXPECT_EQ(selected.version, RDPGFX_CAPVERSION_101);
  EXPECT_EQ(selected.length, 16u);
  EXPECT_EQ(selected.flags, 0u);
}
TEST(GraphicsCapability, MasksFlagsAndDisablesAvc)
{
  constexpr auto handled = RDPGFX_CAPS_FLAG_THINCLIENT | RDPGFX_CAPS_FLAG_SMALL_CACHE | RDPGFX_CAPS_FLAG_SCALEDMAP_DISABLE;
  for (UINT32 const version : { RDPGFX_CAPVERSION_8, RDPGFX_CAPVERSION_81, RDPGFX_CAPVERSION_10,
                                RDPGFX_CAPVERSION_102, RDPGFX_CAPVERSION_107 }) {
    RDPGFX_CAPSET cap     { version, 4, 0xffffffff };
    auto          selected = Backend::SelectCapability({ &cap, 1 });
    EXPECT_EQ(selected.flags, handled | (version >= RDPGFX_CAPVERSION_10 ? RDPGFX_CAPS_FLAG_AVC_DISABLED : 0));
    EXPECT_EQ(selected.length, 4u);
    cap.length = 3;
    EXPECT_EQ(Backend::SelectCapability({ &cap, 1 }).version, 0u);
  }
}
TEST(GraphicsTimestamp, PacksIndependentFields)
{
  SYSTEMTIME time{ };
  EXPECT_EQ(Backend::FrameTimestamp(time), 0u);
  time.wHour = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00400000u);
  time.wHour   = 0;
  time.wMinute = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00010000u);
  time.wMinute = 0;
  time.wSecond = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x00000400u);
  time.wSecond       = 0;
  time.wMilliseconds = 1;
  EXPECT_EQ(Backend::FrameTimestamp(time), 1u);
  time.wHour         = 23;
  time.wMinute       = 59;
  time.wSecond       = 59;
  time.wMilliseconds = 999;
  EXPECT_EQ(Backend::FrameTimestamp(time), 0x05fbefe7u);
}
}

#include "_detail/avc.hpp"
#include <iostream>
#include <freerdp/primitives.h>

namespace {
std::vector<UINT32> Yuv420Reference(std::vector<UINT32> const& pixels, unsigned width, unsigned height) {
  auto w      { Backend::Avc::Aligned(width)              };
  auto h      { Backend::Avc::Aligned(height)             };
  auto padded { std::vector<BYTE>(std::size_t(w) * h * 4) };
  std::ranges::for_each(std::views::iota(0u, h), [&](unsigned y) {
    std::ranges::for_each(std::views::iota(0u, w), [&](unsigned x) {
      auto pixel = pixels[std::min(y, height - 1) * width + std::min(x, width - 1)];
      std::memcpy(padded.data() + (std::size_t(y) * w + x) * 4, &pixel, 4);
    });
  });
  auto        yuv       { std::vector<BYTE>(std::size_t(w) * h * 3 / 2)              };
  BYTE*       planes [] { yuv.data(), yuv.data() + w * h, yuv.data() + w * h * 5 / 4 };
  UINT32      strides[] { w, w / 2, w / 2                                            };
  prim_size_t size      { w, h                                                       };
  EXPECT_EQ(primitives_get()->RGBToYUV420_8u_P3AC4R(padded.data(), PIXEL_FORMAT_BGRX32,
    w * 4, planes, strides, &size), 0);
  BYTE const* source [] { planes[0], planes[1], planes[2]         };
  auto        decoded   { std::vector<UINT32>(std::size_t(w) * h) };
  auto        cropped   { std::vector<UINT32>(pixels.size())      };
  EXPECT_EQ(primitives_get()->YUV420ToRGB_8u_P3AC4R(source, strides,
    reinterpret_cast<BYTE*>(decoded.data()), w * 4, PIXEL_FORMAT_BGRX32, &size), 0);
  std::ranges::for_each(std::views::iota(0u, height), [&](unsigned y) {
    std::copy_n(decoded.data() + y * w, width, cropped.data() + y * width);
  });
  return cropped;
}
TEST(Avc, Bitrate)
{
  EXPECT_EQ(Backend::Avc::Bitrate(1920, 1080), 16000000u);
  EXPECT_EQ(Backend::Avc::Bitrate(960, 540), 4000000u);
  EXPECT_EQ(Backend::Avc::Bitrate(320, 200), 2000000u);
  EXPECT_EQ(Backend::Avc::Bitrate(320, 200, 1234), 1234000u);
  EXPECT_EQ(Backend::Avc::Bitrate(32766, 32766), UINT32_MAX);
}
TEST(Avc, ReplicatesPadding) {
  std::array<BYTE, 32> source{1,2,3,4, 5,6,7,8, 9,10,11,12, 99,99,99,99,
    13,14,15,16, 17,18,19,20, 21,22,23,24, 99,99,99,99};
  std::vector<BYTE> padded(16 * 16 * 4);
  std::copy_n(source.data(), 12, padded.data());
  std::copy_n(source.data() + 16, 12, padded.data() + 64);
  Backend::Avc::ReplicateEdges(padded, 3, 2);
  ASSERT_EQ(padded.size(), 16u * 16 * 4);
  for (unsigned y = 0; y < 16; ++y)
    for (unsigned x = 0; x < 16; ++x)
      for (unsigned c = 0; c < 4; ++c)
        EXPECT_EQ(padded[(y * 16 + x) * 4 + c], source[std::min(y, 1u) * 16 + std::min(x, 2u) * 4 + c]);
}
TEST(Avc, RegionMetablock)
{
  Backend::Avc::Regions regions;
  regions.Add({ 17, 19, 7, 5 });
  regions.Add({ 2, 3, 4, 6 });
  EXPECT_EQ(regions.Bytes(), 24u);
  EXPECT_EQ(regions.bounds.x, 2);
  EXPECT_EQ(regions.bounds.y, 3);
  EXPECT_EQ(regions.bounds.w, 22);
  EXPECT_EQ(regions.bounds.h, 21);
  EXPECT_EQ(regions.rects[0].right, 24);
  EXPECT_EQ(regions.rects[0].bottom, 24);
  for (auto q : regions.quality) {
    EXPECT_EQ(q.qpVal, 0x9a);
    EXPECT_EQ(q.qualityVal, 100);
    EXPECT_EQ(q.p, 1);
    EXPECT_EQ(q.qp, 26);
  }
}
TEST(GraphicsCapability, AllowsAvcWhenOfferedAndAvailable)
{
  for (UINT32 const version : { RDPGFX_CAPVERSION_8, RDPGFX_CAPVERSION_81, RDPGFX_CAPVERSION_10,
                                RDPGFX_CAPVERSION_101, RDPGFX_CAPVERSION_102, RDPGFX_CAPVERSION_107 }) {
    RDPGFX_CAPSET cap{ version, version == RDPGFX_CAPVERSION_101 ? 16u : 4u, RDPGFX_CAPS_FLAG_AVC420_ENABLED };
    for (bool const available : { false, true }) {
      auto   selected = Backend::SelectCapability({ &cap, 1 }, available);
      UINT32 expected = 0;
      if (version == RDPGFX_CAPVERSION_81 && available) expected = RDPGFX_CAPS_FLAG_AVC420_ENABLED;
      if (version >= RDPGFX_CAPVERSION_10 && version != RDPGFX_CAPVERSION_101 && !available)
        expected = RDPGFX_CAPS_FLAG_AVC_DISABLED;
      EXPECT_EQ(selected.flags, expected) << version << ' ' << available;
    }
    cap.flags     = version >= RDPGFX_CAPVERSION_10 ? RDPGFX_CAPS_FLAG_AVC_DISABLED : 0;
    auto selected = Backend::SelectCapability({ &cap, 1 }, true);
    EXPECT_EQ(selected.flags, version >= RDPGFX_CAPVERSION_10 && version != RDPGFX_CAPVERSION_101
                                  ? RDPGFX_CAPS_FLAG_AVC_DISABLED
                                  : 0u);
  }
}
class AvcGraphics : public testing::Test {
protected:
  Headless::Logs logs;
  std::filesystem::path directory;
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> backend{nullptr, sdlrdp_close};
  void Open(sdlrdp_codec codec = SDLRDP_CODEC_AVC420, unsigned width = 320, unsigned height = 200) {
    char pattern[] = "/tmp/sdlrdp-avc-XXXXXX";
    auto path = mkdtemp(pattern);
    ASSERT_NE(path, nullptr); directory = path;
    sdlrdp_config config{"127.0.0.1", 0, directory.c_str(), width, height, 0, Headless::Logs::Collect, &logs};
    config.codec = codec;
    sdlrdp_handle* handle = nullptr;
    ASSERT_EQ(sdlrdp_open(&config, &handle), 0) << sdlrdp_last_error();
    backend.reset(handle);
    Headless::InitializeTls(sdlrdp_port(handle));
  }
  void TearDown() override {
    backend.reset();
    if (!directory.empty()) std::filesystem::remove_all(directory);
  }
  void ScaledPattern(Headless::Client& client, Headless::GraphicsObserver& observer,
                     std::vector<UINT32> const& pixels) {
    ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_RAW), 0);
    ASSERT_EQ(sdlrdp_set_aspect(backend.get(), {3, 2}), 0);
    auto        before { observer.frames.size() };
    sdlrdp_rect full   { 0, 0, 320, 200         };
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() > before; }));
    auto gdi = client.instance->context->gdi;
    ASSERT_EQ(gdi->width, 321);
    ASSERT_EQ(gdi->height, 214);
    std::vector<UINT32> scaled(321 * 214);
    std::ranges::for_each(std::views::iota(0, 214), [&](int y) {
      std::copy_n(reinterpret_cast<UINT32 const*>(gdi->primary_buffer + y * gdi->stride), 321, scaled.data() + y * 321);
    });
    auto reference = Yuv420Reference(scaled, 321, 214);
    ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) == 1; }));
    ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_AVC420), 0);
    auto avc_before = observer.avc_nals.size();
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &full, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.avc_nals.size() > avc_before; }));
    auto error = client.MaxError(scaled, &reference);
    std::cout << "Scaled maximum channel error: " << error << '\n';
    RecordProperty("scaled_maximum_channel_error", error);
    EXPECT_LE(error, 8u);
    EXPECT_TRUE(observer.avc_nals.back() & (1u << 5));

  }
  void Frame(Headless::Client& client, Headless::GraphicsObserver& observer,
      std::vector<UINT32> const& pixels, unsigned width, unsigned height, sdlrdp_rect damage) {
    auto before     { observer.frames.size()   };
    auto avc_before { observer.avc_nals.size() };
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), width * 4, width, height, &damage, 1), 0);
    ASSERT_TRUE(client.Until([&] { return observer.frames.size() > before; })) << logs.Text(true);
    auto reference = observer.avc_nals.size() > avc_before ? Yuv420Reference(pixels, width, height) : pixels;
    auto error = client.MaxError(pixels, &reference);
    std::cout << "Graphics maximum channel error: " << error << '\n';
    RecordProperty("maximum_channel_error_" + std::to_string(observer.frames.size()), error);
    EXPECT_LE(error, 8u) << logs.Text(true);
    ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) == 1; }));
  }
};
TEST_F(AvcGraphics, AutoWithAvcStartsWithIdr)
{
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  Open(SDLRDP_CODEC_AUTO);
  Headless::Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics(true);
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> const pixels(320uz * 200, 0x55aaff);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 320, 200, { 0, 0, 320, 200 }));
  bool reported = false;
  ASSERT_TRUE(client.Until([&] {
    std::array<sdlrdp_event, 32> events;
    auto                         count  = sdlrdp_poll(backend.get(), events.data(), events.size());
    for (unsigned i = 0; i < count; ++i) {
      if (events[i].type == SDLRDP_CONNECTED) reported |= events[i].connected.codec == SDLRDP_CODEC_AVC420;
      if (events[i].type == SDLRDP_CODEC_CHANGED) reported |= events[i].codec_changed.codec == SDLRDP_CODEC_AVC420;
    }
    return reported;
  }));
  ASSERT_EQ(observer.avc_nals.size(), 1u);
  EXPECT_TRUE(observer.avc_nals.front() & (1u << 5));
  EXPECT_EQ(observer.progressive_headers, 0u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "falls back"), 0u);
}
TEST_F(AvcGraphics, AutoWithoutAvcUsesProgressiveSilently)
{
  Open(SDLRDP_CODEC_AUTO);
  Headless::Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  ASSERT_TRUE(freerdp_settings_set_bool(client.instance->context->settings, FreeRDP_GfxH264, FALSE));
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> const pixels(320uz * 200, 0x55aaff);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 320, 200, { 0, 0, 320, 200 }));
  bool reported = false;
  ASSERT_TRUE(client.Until([&] {
    std::array<sdlrdp_event, 32> events;
    auto                         count  = sdlrdp_poll(backend.get(), events.data(), events.size());
    for (unsigned i = 0; i < count; ++i) {
      if (events[i].type == SDLRDP_CONNECTED) reported |= events[i].connected.codec == SDLRDP_CODEC_PROGRESSIVE;
      if (events[i].type == SDLRDP_CODEC_CHANGED) reported |= events[i].codec_changed.codec == SDLRDP_CODEC_PROGRESSIVE;
    }
    return reported;
  }));
  EXPECT_TRUE(observer.avc_nals.empty());
  EXPECT_EQ(observer.progressive_headers, 1u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "falls back"), 0u);
}
TEST_F(AvcGraphics, DecodesPFrameAndResize)
{
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  Open();
  Headless::Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics(true);
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); })) << logs.Text(true);
  std::vector<UINT32>             pixels(320uz * 200);
  constexpr std::array<UINT32, 4> colors{ 0xff0000, 0x00ff00, 0x0000ff, 0x55aaff };
  for (unsigned y = 0; y < 200; ++y)
    for (unsigned x = 0; x < 320; ++x) pixels[(y * 320) + x] = colors[x / 80];
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 320, 200, { 0, 0, 320, 200 }));
  sdlrdp_rect const damage{ 18, 20, 8, 6 };
  for (int y = 20; y < 26; ++y)
    for (int x = 18; x < 26; ++x) pixels[(y * 320) + x] = 0x55aaff;
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 320, 200, damage));
  ASSERT_EQ(observer.avc_nals.size(), 2u);
  EXPECT_EQ(observer.avc_nals[0] & ((1u << 5) | (1u << 7) | (1u << 8)), (1u << 5) | (1u << 7) | (1u << 8));
  EXPECT_TRUE(observer.avc_nals[1] & (1u << 1));
  EXPECT_FALSE(observer.avc_nals[1] & (1u << 5));
  ASSERT_EQ(observer.avc_rects.size(), 1u);
  ASSERT_EQ(observer.avc_quality.size(), 1u);
  EXPECT_EQ(observer.avc_rects[0].left, 18);
  EXPECT_EQ(observer.avc_rects[0].right, 26);
  EXPECT_EQ(observer.avc_quality[0].qpVal, 0x9a);
  EXPECT_EQ(observer.avc_quality[0].qualityVal, 100);
  pixels.assign(354uz * 226, 0x335577);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 354, 226, { 0, 0, 354, 226 }));
  EXPECT_EQ(observer.surfaces.size(), 2u);
  EXPECT_EQ(observer.commands, 3u);
  ASSERT_EQ(observer.avc_nals.size(), 3u);
  EXPECT_TRUE(observer.avc_nals.back() & (1u << 5));
  EXPECT_FALSE(logs.Contains("falls back")) << logs.Text(true);
}
TEST_F(AvcGraphics, ClientWithoutAvcFallsBackAndReportsChange)
{
  Open();
  Headless::Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics();
  ASSERT_TRUE(freerdp_settings_set_bool(client.instance->context->settings, FreeRDP_GfxH264, FALSE));
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> const pixels(320uz * 200, 0x55aaff);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 320, 200, { 0, 0, 320, 200 }));
  bool changed = false;
  ASSERT_TRUE(client.Until([&] {
    std::array<sdlrdp_event, 32> events;
    auto                         count  = sdlrdp_poll(backend.get(), events.data(), events.size());
    for (unsigned i = 0; i < count; ++i)
      changed |= events[i].type == SDLRDP_CODEC_CHANGED && events[i].codec_changed.codec == SDLRDP_CODEC_PROGRESSIVE;
    return changed;
  }));
  EXPECT_EQ(observer.progressive_headers, 1u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "AVC420 falls back"), 1u);
}
}

namespace {
TEST_F(AvcGraphics, SmallSurfaceFallsBack)
{
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  Open(SDLRDP_CODEC_AVC420, 32, 32);
  Headless::Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics(true);
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); })) << logs.Text(true);
  std::vector<UINT32> const pixels(32uz * 32, 0x55aaff);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 32, 32, { 0, 0, 32, 32 }));
  EXPECT_TRUE(observer.avc_nals.empty());
  EXPECT_EQ(observer.progressive_headers, 1u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "surface below NVENC minimum"), 1u);
}
TEST_F(AvcGraphics, AutoSmallSurfaceLogsFallbackWhenAvcRequested)
{
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  Open(SDLRDP_CODEC_AUTO, 32, 32);
  Headless::Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics(true);
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); })) << logs.Text(true);
  std::vector<UINT32> const pixels(32uz * 32, 0x55aaff);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 32, 32, { 0, 0, 32, 32 }));
  EXPECT_TRUE(observer.avc_nals.empty());
  EXPECT_EQ(observer.progressive_headers, 1u);
  EXPECT_FALSE(logs.Contains("falls back"));
  EXPECT_FALSE(logs.Contains("surface below NVENC minimum"));
  std::array<sdlrdp_event, 32> events{ };
  while (sdlrdp_poll(backend.get(), events.data(), events.size())) {}
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_AVC420), 0);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 32, 32, { 0, 0, 32, 32 }));
  EXPECT_TRUE(observer.avc_nals.empty());
  EXPECT_EQ(observer.progressive_headers, 1u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "surface below NVENC minimum"), 1u);
  EXPECT_EQ(logs.Count(SDLRDP_LOG_INFO, "falls back"), 1u);
  bool changed = false;
  ASSERT_TRUE(client.Until([&] {
    auto count = sdlrdp_poll(backend.get(), events.data(), events.size());
    for (unsigned i = 0; i < count; ++i)
      changed |= events[i].type == SDLRDP_CODEC_CHANGED && events[i].codec_changed.codec == SDLRDP_CODEC_PROGRESSIVE;
    return changed;
  }));
}
TEST_F(AvcGraphics, ProgressiveConnectionSwitchesToAvcWithIdr)
{
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  Open(SDLRDP_CODEC_PROGRESSIVE);
  Headless::Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics(true);
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); })) << logs.Text(true);
  std::vector<UINT32> const pixels(320uz * 200, 0x335577);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 320, 200, { 0, 0, 320, 200 }));
  EXPECT_EQ(observer.progressive_headers, 1u);
  EXPECT_TRUE(observer.avc_nals.empty());
  auto commands = observer.commands;
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_AVC420), 0);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 320, 200, { 18, 20, 8, 6 }));
  EXPECT_EQ(observer.commands, commands + 1);
  ASSERT_EQ(observer.avc_nals.size(), 1u);
  EXPECT_TRUE(observer.avc_nals.back() & (1u << 5));
}
TEST_F(AvcGraphics, CodecSwitchRestoresFullSurfaceAndIdr)
{
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  Open();
  Headless::Client client(sdlrdp_port(backend.get()), true);
  client.EnableGraphics(true);
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); })) << logs.Text(true);
  std::vector<UINT32> pixels(320uz * 200, 0xff0000);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 320, 200, { 0, 0, 320, 200 }));
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_RAW), 0);
  pixels.assign(pixels.size(), 0x335577);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 320, 200, { 0, 0, 320, 200 }));
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_AVC420), 0);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 320, 200, { 18, 20, 8, 6 }));
  ASSERT_EQ(observer.avc_nals.size(), 2u);
  EXPECT_TRUE(observer.avc_nals.back() & (1u << 5));
  ASSERT_FALSE(observer.avc_rects.empty());
  EXPECT_EQ(observer.avc_rects.back().right, 320);
  EXPECT_EQ(observer.avc_rects.back().bottom, 200);
}
TEST_F(AvcGraphics, KnownPatternColours) {
  if (!Backend::Avc::Encoder::Available()) GTEST_SKIP() << Backend::Avc::Encoder::UnavailableReason();
  ASSERT_NO_FATAL_FAILURE(Open(SDLRDP_CODEC_AVC420));
  Headless::Client client(sdlrdp_port(backend.get()), true, 320, 200);
  client.EnableGraphics(true);
  Headless::GraphicsObserver observer(client);
  ASSERT_TRUE(freerdp_connect(client.instance.get()));
  ASSERT_TRUE(client.Until([&] { return logs.Contains("GFX confirmed"); }));
  std::vector<UINT32> pixels(320 * 200);
  Headless::MovingTilePattern(pixels, 320, 200, 0);
  ASSERT_NO_FATAL_FAILURE(Frame(client, observer, pixels, 320, 200, {0, 0, 320, 200}));
  EXPECT_EQ(observer.avc_nals.size(), 1u);
  ASSERT_NO_FATAL_FAILURE(ScaledPattern(client, observer, pixels));
}
TEST(AvcConfiguration, IntraRefreshUsesConfiguredFrameRate) {
  struct Case { unsigned fps, period, count; };
  constexpr std::array cases{
    Case{  1,   2,  1}, Case{ 24,  48, 12}, Case{ 30,  60, 15},
    Case{ 59, 118, 29}, Case{ 60, 120, 30}, Case{144, 288, 72}
  };
  std::ranges::for_each(cases, [](auto value) {
    auto refresh = Backend::Avc::IntraRefreshFor(value.fps);
    EXPECT_EQ(refresh.period, value.period);
    EXPECT_EQ(refresh.count,  value.count);
  });
}

}
