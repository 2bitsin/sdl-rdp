#include "support.test/next-frame.hpp"

#include "support.test/frame-pattern.hpp"

#include <algorithm>
#include <functional>

namespace SampleGate {
using utilities::Expects;

NextFrame::NextFrame(Headless::Client& value, unsigned frame)
    : client(value), column(frame % 640), hook(value, std::bind_front(&NextFrame::Observe, this)) { }
auto NextFrame::Received() const -> bool {
  return received;
}
auto NextFrame::Matches() const -> testing::AssertionResult const& {
  return matches;
}
auto NextFrame::Observe(Headless::PictureUpdate const& update) -> void {
  auto const completes = [](sdlrdp_rect region) { return region.y + region.h == 480; };
  if (update.delivered && std::ranges::any_of(update.regions, completes)) Inspect();
}
auto NextFrame::Inspect() -> void {
  auto const* context = client.Instance()->context;
  Expects(context, "callback context exists");
  Expects(context->gdi, "decoded framebuffer exists");
  auto const index  = (40 * 640) + static_cast<int>(column);
  bool const origin = PatternPixel(context->gdi, index) == 0x00ff00 &&
                      (!column || PatternPixel(context->gdi, index - 1) != 0x00ff00);
  if (!received && origin) {
    matches  = Pattern(client, true);
    received = true;
  }
}
}
