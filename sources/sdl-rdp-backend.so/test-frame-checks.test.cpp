#include "_detail/test-frame-checks.hpp"

#include "_detail/test-peer-status.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdlib>
#include <freerdp/gdi/gdi.h>
#include <freerdp/input.h>
#include <gtest/gtest.h>
#include <oxbox/utilities/span.hpp>
#include <ranges>
#include <string>
#include <utility>
#include <winpr/synch.h>

namespace BackendGate {
namespace {
struct GraphicsCounts {
  std::size_t desktops;
  std::size_t resets;
  std::size_t frames;
};
auto ThenMonitor(MONITOR_DEF const& monitor, Backend::Extent size) -> void {
  EXPECT_EQ(monitor.left, 0);
  EXPECT_EQ(monitor.top, 0);
  EXPECT_EQ(monitor.right, static_cast<std::int32_t>(size.width) - 1);
  EXPECT_EQ(monitor.bottom, static_cast<std::int32_t>(size.height) - 1);
  EXPECT_EQ(monitor.flags, 1u);
}
auto CountsOf(Headless::GraphicsObserver const& observer) -> GraphicsCounts {
  return { .desktops = observer.Observed().desktops.size(), .resets = observer.Observed().resets.size(),
           .frames   = observer.Observed().frames.size() };
}
auto ThenResetGeometry(Headless::GraphicsObserver::Reset const& reset, GraphicsCounts before, Backend::Extent size)
    -> void {
  EXPECT_EQ(reset.width, size.width);
  EXPECT_EQ(reset.height, size.height);
  EXPECT_EQ(reset.desktops, before.desktops + 1);
  EXPECT_EQ(reset.frames, before.frames);
}
auto ThenGraphicsReset(Headless::GraphicsObserver const& observer, GraphicsCounts before, Backend::Extent size)
    -> void {
  ASSERT_EQ(observer.Observed().resets.size(), before.resets + 1);
  auto const& reset = observer.Observed().resets.back();
  ThenResetGeometry(reset, before, size);
  ASSERT_EQ(reset.monitors.size(), 1u);
  ThenMonitor(reset.monitors[0], size);
  EXPECT_EQ(observer.Observed().frames.size(), before.frames + 1);
}
}
void FrameChecks::Present(std::vector<UINT32> const& pixels, unsigned w, unsigned h) {
  sdlrdp_rect const full{ 0, 0, int(w), int(h) };
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), w * 4, w, h, &full, 1), 0);
}
void FrameChecks::FillLegacyWindow(Client& client, FrameObserver& observer, std::vector<UINT32>& pixels) {
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 1; }));
  std::ranges::fill(pixels, 0x223344);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 2; }));
  for (unsigned i = 0; i < 10; ++i) {
    std::ranges::fill(pixels, 0x334455 + i);
    Present(pixels, 640, 480);
  }
  EXPECT_EQ(observer.Frames().size(), 2u);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
}
void FrameChecks::SuppressAndCheckInput(Client& client) {
  auto* update = client.Instance()->context->update;
  ASSERT_TRUE(update->SuppressOutput(client.Instance()->context, 0, nullptr));
  ASSERT_EQ(Events(2).size(), 2u);
  ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x1e));
  auto suppressed = Events(1);  // Input follows SuppressOutput on the same connection.
  ASSERT_EQ(suppressed.size(), 1u);
  ASSERT_EQ(suppressed.front().type, SDLRDP_KEY);
}
void FrameChecks::ThenDesktopGeometry(Client const& client, unsigned w, unsigned h) {
  EXPECT_EQ(client.Instance()->context->gdi->width, int(w));
  EXPECT_EQ(client.Instance()->context->gdi->height, int(h));
}
void FrameChecks::ThenAspectGeometry(Client& client) {
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].connected.screen_width, 1024u);
  EXPECT_EQ(events[1].type, SDLRDP_SCREEN);
  EXPECT_EQ(events[1].screen.height, 768u);
  ThenDesktopGeometry(client, 640, 480);
}
void FrameChecks::ThenScaledHighlight(Client& client) {
  auto actual    = oxbox::utilities::SpanCast<std::uint32_t const>(
      std::span(client.Instance()->context->gdi->primary_buffer, 640uz * 480 * 4));
  auto rows      = std::views::iota(0, 480);
  auto brightest =
      std::ranges::max_element(rows, { }, [&](int y) { return actual[static_cast<std::size_t>(y) * 640] & 255; });
  EXPECT_LE(std::abs(*brightest - 240), 1);
}
void FrameChecks::ThenSparseDamage(Client& client, FrameObserver& observer, std::vector<UINT32> const& pixels,
                                   std::size_t bounding, sdlrdp_codec codec) {
  auto                       bytes  = client.Received();
  std::array<sdlrdp_rect, 2> damage { { { .x = 0   , .y = 0  , .w = 8, .h = 8 },
                                        { .x = 1016, .y = 760, .w = 8, .h = 8 } } };
  ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 4096, 1024, 768, damage.data(), 2), 0);
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == 3; }));
  auto used = client.Received() - bytes;
  testing::Test::RecordProperty("region_bytes_" + std::to_string(codec), std::to_string(used));
  EXPECT_LT(used, bounding / 100);
}
void FrameChecks::ThenProducerFrame(Client& client, FrameObserver& observer, std::atomic<unsigned> const& presents) {
  std::vector<UINT32> final(1024uz * 768);
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
void FrameChecks::ThenReadable(Client const& client) {
  std::array<HANDLE, 64> handles{ };
  auto count = freerdp_get_event_handles(client.Instance()->context, handles.data(), handles.size());
  ASSERT_GT(count, 0u);
  ASSERT_LT(WaitForMultipleObjects(count, handles.data(), FALSE, 10000), WAIT_OBJECT_0 + count);
}
void FrameChecks::ThenQoe(Client& client, Headless::GraphicsObserver& observer) {
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU qoe{ observer.Observed().frames.back().frameId, 1234, 7, 9 };
  ASSERT_EQ(observer.Channel()->QoeFrameAcknowledge(observer.Channel(), &qoe), CHANNEL_RC_OK);
  ASSERT_TRUE(client.Until([&] {
    auto const received = RequiredGraphics(*backend).Qoe();
    return received.timestamp == qoe.timestamp && received.timeDiffSE == 7 && received.timeDiffEDR == 9;
  }));
  EXPECT_FALSE(logs.Contains("GFX QoE"));
}
auto FrameChecks::ResizePicture(Client& client, Headless::GraphicsObserver& observer,
                                std::vector<std::uint32_t>& pixels, Backend::Extent size, bool graphics) -> void {
  auto const before = CountsOf(observer);
  pixels.assign(static_cast<std::size_t>(size.width) * size.height, 0x654321);
  Present(pixels, size.width, size.height);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
  ASSERT_GT(observer.Observed().desktops.size(), before.desktops);
  EXPECT_EQ(observer.Observed().desktops.back(), (std::pair{ size.width, size.height }));
  ThenDesktopGeometry(client, size.width, size.height);
  if (!graphics) return;
  ThenGraphicsReset(observer, before, size);
}
}
