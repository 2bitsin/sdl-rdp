#include "_detail/test-round-five.hpp"

#include "_detail/test-peer-status.hpp"

#include <algorithm>
#include <cstdint>
#include <format>
#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <oxbox/utilities/number-text.hpp>
#include <regex>
#include <string>

namespace BackendGate {
auto RoundFive::ThenAcknowledgementTimeout(std::vector<UINT32> const& pixels) -> void {
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 1);
  EXPECT_EQ(RequiredStatus(*backend).acknowledgements, 0u);
  ThenTimedOutFrames("[0-9]+", 1);
}
auto RoundFive::ThenTimedOutFrames(std::string_view sent, unsigned minimum) -> void {
  backend.reset();
  auto        text  = logs.Text(true);
  std::smatch match;
  ASSERT_TRUE(std::regex_search(text, match, std::regex(std::format(R"(Frames: {} sent,[^\n]*, ([0-9]+) timed out\.)",
                                                                    sent))))
      << text;
  EXPECT_GE(oxbox::utilities::ParseNumber<unsigned>(match.str(1)), minimum);
}
auto RoundFive::ThenAspectMouse(Client& client) -> void {
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.Instance()->context->input, PTR_FLAGS_MOVE, 639, 479));
  auto events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  EXPECT_EQ(events[0].mouse_move.x, 639);
  EXPECT_EQ(events[0].mouse_move.y, 349);
}
auto RoundFive::ThenAgedWindowResumes(std::vector<UINT32> const& pixels) -> void {
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 0), 0);
  ASSERT_TRUE(GraphicsObserver().AckFrame(4, 0));
  AwaitFrames(GraphicsClient(), GraphicsObserver().Observed().frames, 7);
  if (::testing::Test::HasFatalFailure()) return;
  ThenGraphicsTimeoutStatistics();
}
auto RoundFive::ThenColourDepth(unsigned depth) -> void {
  Client client(sdlrdp_port(backend.get()), false);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_ColorDepth, depth));
  Connect(client, false);
  client.Tolerance(depth == 16 ? 7 : 0);
  EXPECT_EQ(freerdp_settings_get_uint32(client.Instance()->context->settings, FreeRDP_ColorDepth), depth);
  std::vector<UINT32> pixels(320uz * 200);
  std::ranges::generate(pixels, [i = 0u]() mutable { return (i++ * 2654435761u) & 0xffffff; });
  Present(pixels, 320, 200);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
}
auto RoundFive::ThenProgressiveDamageCost(Client& client, Headless::GraphicsObserver& observer,
                                          uint64_t before) -> void {
  EXPECT_EQ(observer.Observed().progressive_headers, 1u);
  EXPECT_EQ(observer.Observed().surfaces.size(), 1u);
  EXPECT_LT(client.Received() - before, 4096u);
  ThenQoe(client, observer);
}
auto RoundFive::ThenAutoChangesToRaw(Client& client, std::vector<UINT32>& pixels) -> void {
  ASSERT_EQ(sdlrdp_set_codec(backend.get(), SDLRDP_CODEC_RAW), 0);
  pixels = GraphicsScene(4, false);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return sdlrdp_wait_frame(backend.get(), 0) && client.Matches(pixels); }));
  auto events  = Events();
  auto changed = std::ranges::find(events, SDLRDP_CODEC_CHANGED, &sdlrdp_event::type);
  ASSERT_NE(changed, events.end());
  EXPECT_EQ(changed->codec_changed.codec, SDLRDP_CODEC_RAW);
  RecordProperty("trace",
                 "auto connects as progressive; live raw preference produces exact RGB and CODEC_CHANGED raw");
}
auto RoundFive::ThenGraphicsTimeoutStatistics() -> void {
  ThenTimedOutFrames("7", 2);
  EXPECT_EQ(GraphicsObserver().Observed().frames.size(), 7u);
}
auto RoundFive::ThenGraphicsAcknowledgementsCounted() -> void {
  ASSERT_TRUE(GraphicsObserver().Ack());
  ASSERT_TRUE(GraphicsClient().Until([&] { return Acknowledged(); }));
  EXPECT_EQ(RequiredStatus(*backend).acknowledgements, 3u);
}
auto RoundFive::ThenGraphicsWindowReleases(std::vector<UINT32> const& pixels) -> void {
  ASSERT_TRUE(GraphicsObserver().AckFrame(0, 0));
  ASSERT_TRUE(GraphicsClient().Until([&] { return sdlrdp_wait_frame(backend.get(), 0) == 1; }));
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
}
auto RoundFive::ThenLegacyWindowReleases(Client& client, FrameObserver const& observer,
                                         std::vector<UINT32> const& pixels) -> void {
  auto* update = client.Instance()->context->update;
  ASSERT_TRUE(update->SurfaceFrameAcknowledge(update->context, observer.Frames().front()));
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 10000), 1);
  Present(pixels, 320, 200);
  EXPECT_EQ(sdlrdp_wait_frame(backend.get(), 1), 0);
}
auto RoundFive::RunPictureSizes(bool graphics) -> void {
  Open();
  Client client(sdlrdp_port(backend.get()), true, 640, 480);
  if (graphics) client.EnableGraphics();
  Headless::GraphicsObserver observer(client);
  Connect(client, false);
  std::vector<std::uint32_t> pixels(640uz * 480, 0x123456);
  Present(pixels, 640, 480);
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text(true);
  for (auto size : { Backend::Extent{ .width = 320, .height = 200 }, Backend::Extent{ .width = 640, .height = 480 } }) {
    ResizePicture(client, observer, pixels, size, graphics);
    if (::testing::Test::HasFatalFailure()) return;
  }
}
}
