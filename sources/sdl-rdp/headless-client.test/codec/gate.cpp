#include <sdl-rdp/headless-client.test/codec/gate.hpp>

#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/headless-client.test/backend/waits.hpp>
#include <sdl-rdp/headless-client.test/frame/counter.hpp>
#include <sdl-rdp/headless-client.test/frame/pattern.hpp>
#include <sdl-rdp/headless-client.test/utilities/io.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <freerdp/gdi/gdi.h>
#include <oxbox/utilities/number-text.hpp>
#include <oxbox/utilities/text.hpp>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <ranges>
#include <span>
#include <string>
#include <unistd.h>

namespace sdl_rdp::headless_client_test::codec::detail::gate {
using sdl_rdp::configuration::Codec;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::headless_client_test::backend::As;
using sdl_rdp::headless_client_test::backend::UntilCookie;
using sdl_rdp::headless_client_test::client::UntilMatches;
using sdl_rdp::headless_client_test::frame::FillArea;
using sdl_rdp::headless_client_test::frame::FrameCounter;
using sdl_rdp::headless_client_test::utilities::ReadText;
using sdl_rdp::link::Connected;
using sdl_rdp::link::Event;
using sdl_rdp::link::ScreenChanged;
using sdl_rdp::utilities::Extent;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Rect;
using sdl_rdp::utilities::Required;

namespace {
auto ResidentBytes() -> std::size_t {
  auto const statm = ReadText("/proc/self/statm");
  auto const pages = Required(oxbox::utilities::ParseNumbers<std::size_t, 7>(oxbox::utilities::Trimmed(statm), ' '),
                              "statm holds seven page counts");
  return pages[1] * Narrowed<std::size_t>(sysconf(_SC_PAGESIZE));
}
}
auto Gate::ThenPictureDesktop() -> void {
  EXPECT_EQ(client->DesktopSize(), (Extent{ .width = 640, .height = 480 }));
  EXPECT_FALSE(logs.Contains("failed"));
}
auto Gate::WhenBurstPictures(Rect area) -> void {
  auto before = ResidentBytes();
  for (std::uint32_t frame = 0; frame < 200; ++frame) {
    std::ranges::fill(pixels, 0x00010101u * (frame + 1));
    backend.Present(pixels, 320, 200, area);
  }
  auto after = ResidentBytes();
  RecordProperty("burst_rss_growth", std::to_string(Narrowed<std::int64_t>(after) - Narrowed<std::int64_t>(before)));
  EXPECT_LE(after, before + (pixels.size() * 16));
  ASSERT_TRUE(UntilMatches(*client, pixels)) << logs.Text();
}
auto Gate::ThenInitialScreen(Event const& event) -> void {
  auto const& screen = As<ScreenChanged>(event);
  EXPECT_EQ(screen.width, 320u);
  EXPECT_EQ(screen.height, 200u);
}
auto Gate::ThenResizedConnection() -> void {
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(As<Connected>(events[0]).width, 640u);
  ThenInitialScreen(events[1]);
}
auto Gate::ThenCleanDisconnect() -> void {
  backend.Close();
  EXPECT_TRUE(logs.Contains("accepted"));
  EXPECT_TRUE(logs.Contains(LogLevel::Info, "disconnected"));
  EXPECT_FALSE(logs.Contains(LogLevel::Error, "Peer transport failed")) << logs.Text();
}
auto Gate::PresentMeasuredFrame() -> void {
  ASSERT_TRUE(UntilCookie(*client));
  FrameCounter const counter(*client);
  auto               bytes   = client->Received();
  ASSERT_NO_FATAL_FAILURE(Frame({ .x = 0, .y = 0, .w = 320, .h = 200 }));
  if (GetParam().codec == Codec::Planar) EXPECT_LE(counter.BitmapPdus(), 1 + ((client->Received() - bytes) / 0xFFFF));
  RecordFrameCost(bytes);
}
auto Gate::WhenDamagedBlock() -> void {
  Rect const block{ .x = 73, .y = 51, .w = 40, .h = 30 };
  FillArea(pixels, 320, block, 0x00020202u);
  Frame(block);
}
auto Gate::ThenClientDisconnects() -> void {
  ASSERT_TRUE(client->Disconnect());
  ASSERT_NO_FATAL_FAILURE(ThenDisconnected());
  ThenCleanDisconnect();
}
}
