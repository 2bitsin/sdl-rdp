#include "_detail/test-gate.hpp"

#include "_detail/test-io.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <freerdp/gdi/gdi.h>
#include <oxbox/utilities/number-text.hpp>
#include <oxbox/utilities/text.hpp>
#include <string>
#include <unistd.h>

namespace BackendGate {
namespace {
std::size_t ResidentBytes() {
  auto const statm = Headless::ReadText("/proc/self/statm");
  auto const pages = Required(oxbox::utilities::ParseNumbers<std::size_t, 7>(oxbox::utilities::Trimmed(statm), ' '),
                              "statm holds seven page counts");
  return pages[1] * std::size_t(sysconf(_SC_PAGESIZE));
}
}
void Gate::ThenPictureDesktop(Client const& client) {
  EXPECT_EQ(client.Instance()->context->gdi->width, 640);
  EXPECT_EQ(client.Instance()->context->gdi->height, 480);
  EXPECT_FALSE(logs.Contains("failed"));
}
void Gate::WhenBurstPictures(Client& client, sdlrdp_rect area) {
  auto before = ResidentBytes();
  for (unsigned frame = 0; frame < 200; ++frame) {
    std::ranges::fill(pixels, 0x00010101u * (frame + 1));
    ASSERT_EQ(sdlrdp_present(backend.get(), pixels.data(), 1280, 320, 200, &area, 1), 0);
  }
  auto after = ResidentBytes();
  RecordProperty("burst_rss_growth", std::to_string(std::int64_t(after) - std::int64_t(before)));
  EXPECT_LE(after, before + (pixels.size() * 16));
  ASSERT_TRUE(client.Until([&] { return client.Matches(pixels); })) << logs.Text();
}
void Gate::ThenInitialScreen(sdlrdp_event const& event) {
  EXPECT_EQ(event.type, SDLRDP_SCREEN);
  EXPECT_EQ(event.screen.width, 320u);
  EXPECT_EQ(event.screen.height, 200u);
}
void Gate::ThenResizedConnection() {
  auto events = Events(2);
  ASSERT_EQ(events.size(), 2u);
  EXPECT_EQ(events[0].type, SDLRDP_CONNECTED);
  EXPECT_EQ(events[0].connected.width, 640u);
  ThenInitialScreen(events[1]);
}
void Gate::ThenCleanDisconnect() {
  backend.reset();
  EXPECT_TRUE(logs.Contains("accepted"));
  EXPECT_TRUE(logs.Contains(SDLRDP_LOG_INFO, "disconnected"));
  EXPECT_FALSE(logs.Contains(SDLRDP_LOG_ERROR, "Peer transport failed")) << logs.Text();
}
}
