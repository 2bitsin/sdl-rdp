#include <sdl-rdp/headless-client.test/frame/checks.hpp>

#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/utilities/geometry.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/input.h>
#include <gtest/gtest.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <ranges>
#include <string>
#include <utility>

namespace sdl_rdp::headless_client_test::frame::detail::checks {
using sdl_rdp::configuration::Codec;
using sdl_rdp::freerdp_facade::MaximumWaitHandles;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::headless_client_test::backend::As;
using sdl_rdp::headless_client_test::backend::Holds;
using sdl_rdp::headless_client_test::backend::RequiredGraphics;
using sdl_rdp::headless_client_test::client::DecodedPixels;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::client::UntilMatches;
using sdl_rdp::link::Connected;
using sdl_rdp::link::Key;
using sdl_rdp::link::ScreenChanged;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rect;

namespace {
struct GraphicsCounts {
  std::size_t desktops;
  std::size_t resets;
  std::size_t frames;
};
auto ThenMonitor(MONITOR_DEF const& monitor, Extent size) -> void {
  EXPECT_EQ(monitor.left, 0);
  EXPECT_EQ(monitor.top, 0);
  EXPECT_EQ(monitor.right, static_cast<std::int32_t>(size.width) - 1);
  EXPECT_EQ(monitor.bottom, static_cast<std::int32_t>(size.height) - 1);
  EXPECT_EQ(monitor.flags, 1u);
}
auto CountsOf(GraphicsObserver const& observer) -> GraphicsCounts {
  return { .desktops = observer.Observed().desktops.size(),
           .resets   = observer.Observed().resets.size(),
           .frames   = observer.Observed().frames.size() };
}
auto ThenResetGeometry(GraphicsObserver::Reset const& reset, GraphicsCounts before, Extent size) -> void {
  EXPECT_EQ(reset.width, size.width);
  EXPECT_EQ(reset.height, size.height);
  EXPECT_EQ(reset.desktops, before.desktops + 1);
  EXPECT_EQ(reset.frames, before.frames);
}
auto ThenGraphicsReset(GraphicsObserver const& observer, GraphicsCounts before, Extent size) -> void {
  ASSERT_EQ(observer.Observed().resets.size(), before.resets + 1);
  auto const& reset = observer.Observed().resets.back();
  ThenResetGeometry(reset, before, size);
  ASSERT_EQ(reset.monitors.size(), 1u);
  ThenMonitor(reset.monitors[0], size);
  EXPECT_EQ(observer.Observed().frames.size(), before.frames + 1);
}
}
auto FrameChecks::Present(Pixels const& pixels, std::uint32_t w, std::uint32_t h) -> void {
  Rect const full{ .x = 0, .y = 0, .w = Narrowed<int>(w), .h = Narrowed<int>(h) };
  backend.Present(pixels, w, h, full);
}
auto FrameChecks::FillLegacyWindow(Client& client, FrameObserver& observer, Pixels& pixels) -> void {
  ASSERT_NO_FATAL_FAILURE(PresentObserved(client, observer, pixels, 1));
  std::ranges::fill(pixels, 0x223344);
  ASSERT_NO_FATAL_FAILURE(PresentObserved(client, observer, pixels, 2));
  for (std::size_t i = 0; i < 10; ++i) {
    std::ranges::fill(pixels, 0x334455 + i);
    ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  }
  EXPECT_EQ(observer.Frames().size(), 2u);
  EXPECT_FALSE(backend.WaitFrame(std::chrono::milliseconds{ 0 }));
}
auto FrameChecks::PresentAcknowledged(Client& client, FrameObserver& observer, Pixels const& pixels, Extent size)
    -> void {
  auto const before = observer.Frames().size();
  ASSERT_NO_FATAL_FAILURE(Present(pixels, size.width, size.height));
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() > before; }));
  ASSERT_TRUE(observer.Ack());
  ASSERT_TRUE(backend.WaitFrame(std::chrono::seconds{ 10 }));
}
auto FrameChecks::PresentObserved(Client& client, FrameObserver const& observer, Pixels const& pixels,
                                  std::size_t frames) -> void {
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == frames; }));
}
auto FrameChecks::SuppressAndCheckInput(Client& client) -> void {
  auto* update = client.Instance()->context->update;
  ASSERT_TRUE(update->SuppressOutput(client.Instance()->context, 0, nullptr));
  ASSERT_EQ(Events(2).size(), 2u);
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x1e));
  auto suppressed = Events(1);  // Input follows SuppressOutput on the same connection.
  ASSERT_EQ(suppressed.size(), 1u);
  ASSERT_TRUE(Holds<Key>(suppressed.front()));
}
auto FrameChecks::ThenDesktopGeometry(Client& client, std::uint32_t w, std::uint32_t h) -> void {
  EXPECT_EQ(client.DesktopSize(), (Extent{ .width = w, .height = h }));
}
auto FrameChecks::ThenAspectGeometry(Client& client) -> void {
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(As<Connected>(events[0]).screen_width, 1024u);
  EXPECT_EQ(As<ScreenChanged>(events[1]).height, 768u);
  ThenDesktopGeometry(client, 640, 480);
}
auto FrameChecks::ThenScaledHighlight(Client& client) -> void {
  auto actual    = DecodedPixels(client);
  auto rows      = std::views::iota(0, 480);
  auto brightest = std::ranges::max_element(rows, { },
                                            [&](int y) { return actual[static_cast<std::size_t>(y) * 640] & 255; });
  EXPECT_LE(std::abs(*brightest - 240), 1);
}
auto FrameChecks::ThenSparseDamage(Client& client, FrameObserver& observer, Pixels const& pixels, std::size_t bounding,
                                   Codec codec) -> void {
  auto                bytes  = client.Received();
  std::array<Rect, 2> damage { { { .x = 0, .y = 0, .w = 8, .h = 8 }, { .x = 1016, .y = 760, .w = 8, .h = 8 } } };
  backend.Present(pixels, 1024, 768, damage);
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 3; }));
  auto used = client.Received() - bytes;
  testing::Test::RecordProperty("region_bytes_" + std::to_string(std::to_underlying(codec)), std::to_string(used));
  EXPECT_LT(used, bounding / 100);
}
auto FrameChecks::ThenProducerFrame(Client& client, FrameObserver& observer, std::atomic<std::size_t> const& presents)
    -> void {
  Pixels final(1024uz * 768);
  std::fill_n(final.begin(), 1024, presents.load());
  std::fill_n(final.end() - 1024, 1024, presents.load());
  ASSERT_TRUE(client.Until([&] {
    observer.Ack();
    return client.Matches(final);
  }));
  EXPECT_TRUE(observer.Coherent());
  EXPECT_LE(observer.Frames().size(), presents.load());
  EXPECT_TRUE(std::ranges::is_sorted(observer.Frames()));
  testing::Test::RecordProperty("presents", std::to_string(presents.load()));
  testing::Test::RecordProperty("acknowledged_frames", std::to_string(observer.Frames().size()));
}
auto FrameChecks::ThenReadable(Client& client) -> void {
  std::array<WaitHandle, MaximumWaitHandles> handles{ };
  auto const ready = WaitHandle::Collected<freerdp_get_event_handles>(*client.Instance()->context, handles);
  ASSERT_FALSE(ready.empty());
  ASSERT_TRUE(WaitHandle::Any(ready, 10000).has_value());
}
auto FrameChecks::ThenQoe(Client& client, GraphicsObserver& observer) -> void {
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU qoe     { observer.Observed().frames.back().frameId, 1234, 7, 9 };
  auto&                            channel = observer.Channel();
  ASSERT_EQ(channel.QoeFrameAcknowledge(&channel, &qoe), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] {
    auto const received = RequiredGraphics(*backend).qoe;
    return received.timestamp == qoe.timestamp && received.time_diff_se == 7 && received.time_diff_edr == 9;
  }));
  EXPECT_FALSE(logs.Contains("GFX QoE"));
}
auto FrameChecks::ResizePicture(Client& client, GraphicsObserver& observer, Pixels& pixels, Extent size, bool graphics)
    -> void {
  auto const before = CountsOf(observer);
  pixels.assign(static_cast<std::size_t>(size.width) * size.height, 0x654321);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, size.width, size.height));
  ASSERT_TRUE(UntilMatches(client, pixels)) << logs.Text(true);
  ASSERT_GT(observer.Observed().desktops.size(), before.desktops);
  EXPECT_EQ(observer.Observed().desktops.back(), (std::pair{ size.width, size.height }));
  ThenDesktopGeometry(client, size.width, size.height);
  if (!graphics) return;
  ThenGraphicsReset(observer, before, size);
}
}
