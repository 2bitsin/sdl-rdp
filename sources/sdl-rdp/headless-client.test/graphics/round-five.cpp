#include <sdl-rdp/headless-client.test/graphics/round-five.hpp>

#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/headless-client.test/backend/await-acknowledged.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/backend/status.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/link/event.hpp>

#include <freerdp/input.h>
#include <freerdp/settings.h>
#include <oxbox/utilities/number-text.hpp>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <format>
#include <functional>
#include <future>
#include <regex>
#include <string>

namespace sdl_rdp::headless_client_test::graphics::detail::round_five {
using sdl_rdp::configuration::Codec;
using sdl_rdp::headless_client_test::backend::AwaitAllAcknowledged;
using sdl_rdp::headless_client_test::backend::FirstEvent;
using sdl_rdp::headless_client_test::backend::As;
using sdl_rdp::headless_client_test::backend::Holds;
using sdl_rdp::headless_client_test::backend::RequiredStatus;
using sdl_rdp::headless_client_test::client::Pixels;
using sdl_rdp::headless_client_test::frame::GraphicsScene;
using sdl_rdp::headless_client_test::frame::HashPattern;
using sdl_rdp::link::CodecChanged;
using sdl_rdp::link::MouseMove;
using sdl_rdp::link::RefreshChanged;
using sdl_rdp::utilities::Extent;

auto RoundFive::WhenAcknowledgedFrame(Client& client, FrameObserver& observer, Pixels const& pixels, std::size_t i,
                                      Wait wait) -> void {
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
  auto waiting = std::async(std::launch::async, wait, std::ref(*backend));
  ASSERT_TRUE(client.Until([&] { return observer.Frames().size() == i; }));
  EXPECT_EQ(waiting.wait_for(std::chrono::milliseconds(0)), std::future_status::timeout);
  ASSERT_TRUE(observer.Ack());
  ASSERT_EQ(waiting.get(), 1);
  EXPECT_TRUE(std::ranges::none_of(backend.Poll(), Holds<RefreshChanged>));
}
auto RoundFive::ThenAcknowledgementTimeout(Pixels const& pixels) -> void {
  EXPECT_TRUE(backend.WaitFrame(std::chrono::milliseconds{ 10000 }));
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
  EXPECT_TRUE(backend.WaitFrame(std::chrono::milliseconds{ 10000 }));
  EXPECT_TRUE(backend.WaitFrame(std::chrono::milliseconds{ 0 }));
  EXPECT_EQ(RequiredStatus(*backend).acknowledgements, 0u);
  ThenTimedOutFrames("[0-9]+", 1);
}
auto RoundFive::ThenTimedOutFrames(std::string_view sent, std::size_t minimum) -> void {
  backend.Close();
  auto        text  = logs.Text(true);
  std::smatch match;
  ASSERT_TRUE(
      std::regex_search(text, match, std::regex(std::format(R"(Frames: {} sent,[^\n]*, ([0-9]+) timed out\.)", sent))))
      << text;
  EXPECT_GE(oxbox::utilities::ParseNumber<std::uint32_t>(match.str(1)), minimum);
}
auto RoundFive::ThenAspectMouse(Client& client) -> void {
  ASSERT_TRUE(freerdp_input_send_mouse_event(client.Instance()->context->input, PTR_FLAGS_MOVE, 639, 479));
  auto events = Events(1);
  ASSERT_EQ(events.size(), 1u);
  auto const& motion = As<MouseMove>(events[0]);
  EXPECT_EQ(motion.x, 639);
  EXPECT_EQ(motion.y, 349);
}
auto RoundFive::ThenAgedWindowResumes(Pixels const& pixels) -> void {
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
  EXPECT_FALSE(backend.WaitFrame(std::chrono::milliseconds{ 0 }));
  ASSERT_TRUE(Observer().AckFrame(4, 0));
  ASSERT_NO_FATAL_FAILURE(AwaitFrames(GraphicsClient(), Observer().Observed().frames, 7));
  ThenGraphicsTimeoutStatistics();
}
auto RoundFive::ThenColourDepth(std::uint32_t depth) -> void {
  Client client(backend.Port(), false);
  ASSERT_TRUE(freerdp_settings_set_uint32(client.Instance()->context->settings, FreeRDP_ColorDepth, depth));
  ASSERT_NO_FATAL_FAILURE(Connect(client, false));
  client.Tolerance(depth == 16 ? 7 : 0);
  EXPECT_EQ(freerdp_settings_get_uint32(client.Instance()->context->settings, FreeRDP_ColorDepth), depth);
  Pixels pixels(320uz * 200);
  HashPattern(pixels);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
}
auto RoundFive::ThenProgressiveDamageCost(Client& client, GraphicsObserver& observer, std::uint64_t before) -> void {
  EXPECT_EQ(observer.Observed().progressive_headers, 1u);
  EXPECT_EQ(observer.Observed().surfaces.size(), 1u);
  EXPECT_LT(client.Received() - before, 4096u);
  ThenQoe(client, observer);
}
auto RoundFive::ThenAutoChangesToRaw(Client& client, Pixels& pixels) -> void {
  (*backend).Presentation().SetCodec(Codec::Raw);
  pixels = GraphicsScene(4, false);
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 640, 480));
  ASSERT_TRUE(
      client.Until([&] { return backend.WaitFrame(std::chrono::milliseconds{ 0 }) && client.Matches(pixels); }));
  auto events  = backend.Poll();
  auto changed = FirstEvent<CodecChanged>(events);
  if (!changed) FAIL() << "a codec change is reported";
  EXPECT_EQ(changed->codec, Codec::Raw);
  RecordProperty("trace", "auto connects as progressive; live raw preference produces exact RGB and CODEC_CHANGED raw");
}
auto RoundFive::ThenGraphicsTimeoutStatistics() -> void {
  ASSERT_NO_FATAL_FAILURE(ThenTimedOutFrames("7", 2));
  EXPECT_EQ(Observer().Observed().frames.size(), 7u);
}
auto RoundFive::ThenGraphicsAcknowledgementsCounted() -> void {
  ASSERT_TRUE(Observer().Ack());
  ASSERT_NO_FATAL_FAILURE(AwaitAllAcknowledged(GraphicsClient(), backend, logs));
  EXPECT_EQ(RequiredStatus(*backend).acknowledgements, 3u);
}
auto RoundFive::ThenGraphicsWindowReleases(Pixels const& pixels) -> void {
  ASSERT_TRUE(Observer().AckFrame(0, 0));
  ASSERT_TRUE(GraphicsClient().Until([&] { return backend.WaitFrame(std::chrono::milliseconds{ 0 }); }));
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
  EXPECT_FALSE(backend.WaitFrame(std::chrono::milliseconds{ 1 }));
}
auto RoundFive::ThenLegacyWindowReleases(Client& client, FrameObserver const& observer, Pixels const& pixels) -> void {
  auto* update = client.Instance()->context->update;
  ASSERT_TRUE(update->SurfaceFrameAcknowledge(update->context, observer.Frames().front()));
  EXPECT_TRUE(backend.WaitFrame(std::chrono::milliseconds{ 10000 }));
  ASSERT_NO_FATAL_FAILURE(Present(pixels, 320, 200));
  EXPECT_FALSE(backend.WaitFrame(std::chrono::milliseconds{ 1 }));
}
auto RoundFive::RunPictureSizes(bool graphics) -> void {
  ASSERT_NO_FATAL_FAILURE(Open());
  Client client(backend.Port(), true, 640, 480);
  if (graphics) client.EnableGraphics();
  GraphicsObserver observer(client);
  Pixels           pixels(640uz * 480, 0x123456);
  ASSERT_NO_FATAL_FAILURE(ShowFirstPicture(client, pixels));
  for (auto size : { Extent{ .width = 320, .height = 200 }, Extent{ .width = 640, .height = 480 } }) {
    ASSERT_NO_FATAL_FAILURE(ResizePicture(client, observer, pixels, size, graphics));
  }
}
}
