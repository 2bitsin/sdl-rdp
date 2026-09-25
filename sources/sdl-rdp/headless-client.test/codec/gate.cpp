#include <sdl-rdp/headless-client.test/codec/gate.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>

#include <sdl-rdp/headless-client.test/client/has-cookie.hpp>
#include <sdl-rdp/headless-client.test/frame/counter.hpp>
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
using sdl_rdp::headless_client_test::client::HasCookie;
using sdl_rdp::headless_client_test::frame::FrameCounter;
using sdl_rdp::headless_client_test::utilities::ReadText;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Required;

namespace {
auto ResidentBytes() -> std::size_t {
  auto const statm = ReadText("/proc/self/statm");
  auto const pages = Required(oxbox::utilities::ParseNumbers<std::size_t, 7>(oxbox::utilities::Trimmed(statm), ' '),
                              "statm holds seven page counts");
  return pages[1] * Narrowed<std::size_t>(sysconf(_SC_PAGESIZE));
}
}
auto Gate::ThenPictureDesktop(Client& client) -> void {
  EXPECT_EQ(client.Instance()->context->gdi->width, 640);
  EXPECT_EQ(client.Instance()->context->gdi->height, 480);
  EXPECT_FALSE(logs.Contains("failed"));
}
auto Gate::WhenBurstPictures(Client& client, sdlrdp_rect area) -> void {
  auto before = ResidentBytes();
  for (std::uint32_t frame = 0; frame < 200; ++frame) {
    std::ranges::fill(pixels, 0x00010101u * (frame + 1));
    ASSERT_EQ(backend.Present(pixels, 320, 200, area), 0);
  }
  auto after = ResidentBytes();
  RecordProperty("burst_rss_growth", std::to_string(Narrowed<std::int64_t>(after) - Narrowed<std::int64_t>(before)));
  EXPECT_LE(after, before + (pixels.size() * 16));
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
}
auto Gate::ThenInitialScreen(sdlrdp_event const& event) -> void {
  EXPECT_EQ(event.type, SDLRDP_SCREEN);
  EXPECT_EQ(event.screen.width, 320u);
  EXPECT_EQ(event.screen.height, 200u);
}
auto Gate::ThenResizedConnection() -> void {
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[0].connected.width, 640u);
  ThenInitialScreen(events[1]);
}
auto Gate::ThenCleanDisconnect() -> void {
  backend.Close();
  EXPECT_TRUE(logs.Contains("accepted"));
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "disconnected"));
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "Peer transport failed")) << logs.Text();
}
auto Gate::PresentMeasuredFrame(Client& client) -> void {
  ASSERT_TRUE(client.Until([&] { return HasCookie(client); }));
  FrameCounter const counter(client);
  auto               bytes   = client.Received();
  ASSERT_NO_FATAL_FAILURE(Frame(client, { 0, 0, 320, 200 }));
  if (GetParam().codec == SDLRDP_CODEC_PLANAR)
    EXPECT_LE(counter.BitmapPdus(), 1 + ((client.Received() - bytes) / 0xFFFF));
  RecordFrameCost(client, bytes);
}
auto Gate::WhenDamagedBlock(Client& client) -> void {
  sdlrdp_rect const block { .x = 73, .y = 51, .w = 40, .h = 30 };
  auto const        left  = static_cast<std::size_t>(block.x);
  auto const        width = static_cast<std::size_t>(block.w);
  std::ranges::for_each(std::views::iota(block.y, block.y + block.h), [&](int y) {
    std::ranges::fill(std::span(pixels).subspan((static_cast<std::size_t>(y) * 320) + left, width), 0x00020202u);
  });
  Frame(client, block);
}
auto Gate::ThenClientDisconnects(Client& client) -> void {
  ASSERT_TRUE(client.Disconnect());
  ASSERT_NO_FATAL_FAILURE(ThenDisconnected());
  ThenCleanDisconnect();
}
}
