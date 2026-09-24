#pragma once
#include "graphics-observer.hpp"
#include "test-backend-core.hpp"
namespace BackendGate {
inline void ThenMonitor(auto const& monitor, unsigned w, unsigned h) {
  EXPECT_EQ(monitor.left, 0);
  EXPECT_EQ(monitor.top, 0);
  EXPECT_EQ(monitor.right, int(w) - 1);
  EXPECT_EQ(monitor.bottom, int(h) - 1);
  EXPECT_EQ(monitor.flags, 1u);
}
inline void ThenResetGeometry(auto const& reset, std::size_t desktops, std::size_t frames, unsigned w, unsigned h) {
  EXPECT_EQ(reset.width, w);
  EXPECT_EQ(reset.height, h);
  EXPECT_EQ(reset.desktops, desktops + 1);
  EXPECT_EQ(reset.frames, frames);
}
inline void ThenGraphicsReset(Headless::GraphicsObserver const& observer, std::size_t desktops, std::size_t resets,
                              std::size_t frames, unsigned w, unsigned h) {
  ASSERT_EQ(observer.Observed().resets.size(), resets + 1);
  auto const& reset = observer.Observed().resets.back();
  ThenResetGeometry(reset, desktops, frames, w, h);
  ASSERT_EQ(reset.monitors.size(), 1u);
  ThenMonitor(reset.monitors[0], w, h);
  EXPECT_EQ(observer.Observed().frames.size(), frames + 1);
}
class FrameChecks : protected BackendEvents {
protected:
  void Present(std::vector<UINT32> const& pixels, unsigned w, unsigned h) {
    sdlrdp_rect const full{ 0, 0, int(w), int(h) };
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), w * 4, w, h, &full, 1), 0);
  }
  void FillLegacyWindow(Client& client, FrameObserver& observer, std::vector<UINT32>& pixels) {
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
  void SuppressAndCheckInput(Client& client) {
    auto* update = client.Instance()->context->update;
    ASSERT_TRUE(update->SuppressOutput(client.Instance()->context, 0, nullptr));
    ASSERT_EQ(Events(2).size(), 2u);
    ASSERT_TRUE(freerdp_input_send_keyboard_event(client.Instance()->context->input, KBD_FLAGS_DOWN, 0x1e));
    auto suppressed = Events(1);  // Input follows SuppressOutput on the same connection.
    ASSERT_EQ(suppressed.size(), 1u);
    ASSERT_EQ(suppressed.front().type, SDLRDP_KEY);
  }
  static void ThenDesktopGeometry(Client const& client, unsigned w, unsigned h) {
    EXPECT_EQ(client.Instance()->context->gdi->width, int(w));
    EXPECT_EQ(client.Instance()->context->gdi->height, int(h));
  }
  void ThenAspectGeometry(Client& client) {
    auto events = Events(2);
    ASSERT_EQ(events.size(), 2u);
    EXPECT_EQ(events[0].connected.screen_width, 1024u);
    EXPECT_EQ(events[1].type, SDLRDP_SCREEN);
    EXPECT_EQ(events[1].screen.height, 768u);
    ThenDesktopGeometry(client, 640, 480);
  }
  static void ThenScaledHighlight(Client& client) {
    auto* actual    = reinterpret_cast<UINT32*>(client.Instance()->context->gdi->primary_buffer);
    auto  rows      = std::views::iota(0, 480);
    auto  brightest =
        std::ranges::max_element(rows, { }, [&](int y) { return actual[static_cast<std::ptrdiff_t>(y) * 640] & 255; });
    EXPECT_LE(std::abs(*brightest - 240), 1);
  }
  void ThenSparseDamage(Client& client, FrameObserver& observer, std::vector<UINT32> const& pixels,
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
  static void ThenProducerFrame(Client& client, FrameObserver& observer, std::atomic<unsigned> const& presents) {
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
  static void ThenReadable(Client const& client) {
    std::array<HANDLE, 64> handles{ };
    auto count = freerdp_get_event_handles(client.Instance()->context, handles.data(), handles.size());
    ASSERT_GT(count, 0u);
    ASSERT_LT(WaitForMultipleObjects(count, handles.data(), FALSE, 10000), WAIT_OBJECT_0 + count);
  }
  void ThenQoe(Client& client, Headless::GraphicsObserver& observer) {
    RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU qoe{ observer.Observed().frames.back().frameId, 1234, 7, 9 };
    ASSERT_EQ(observer.Channel()->QoeFrameAcknowledge(observer.Channel(), &qoe), CHANNEL_RC_OK);
    ASSERT_TRUE(client.Until([&] {
      auto const received = RequiredGraphics(*backend).Qoe();
      return received.timestamp == qoe.timestamp && received.timeDiffSE == 7 && received.timeDiffEDR == 9;
    }));
    EXPECT_FALSE(logs.Contains("GFX QoE"));
  }
  void ResizePicture(Client& client, Headless::GraphicsObserver& observer, std::vector<UINT32>& pixels, unsigned w,
                     unsigned h, bool graphics) {
    auto desktops = observer.Observed().desktops.size();
    auto resets   = observer.Observed().resets.size();
    auto frames   = observer.Observed().frames.size();
    pixels.assign(static_cast<std::size_t>(w) * h, 0x654321);
    Present(pixels, w, h);
    ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
    ASSERT_GT(observer.Observed().desktops.size(), desktops);
    EXPECT_EQ(observer.Observed().desktops.back(), (std::pair{ w, h }));
    ThenDesktopGeometry(client, w, h);
    if (!graphics) return;
    ThenGraphicsReset(observer, desktops, resets, frames, w, h);
  }
};
}
