#include <sdl-rdp/sample-gate.test/frame/next.hpp>

#include <sdl-rdp/sample-gate.test/frame/pattern.hpp>

#include <algorithm>
#include <cstdint>
#include <functional>

namespace sdl_rdp::sample_gate_test::frame::detail::next {
using sdl_rdp::utilities::Expects;

NextFrame::NextFrame(Client& value, std::uint32_t frame)
    : client(value), column(frame % 640), hook(value, std::bind_front(&NextFrame::Observe, this)) { }
auto NextFrame::Received() const -> bool {
  return received;
}
auto NextFrame::Matches() const -> testing::AssertionResult const& {
  return matches;
}
auto NextFrame::Observe(PictureUpdate const& update) -> void {
  auto const completes = [](sdlrdp_rect region) { return region.y + region.h == 480; };
  if (update.delivered && std::ranges::any_of(update.regions, completes)) Inspect();
}
auto NextFrame::Inspect() -> void {
  auto const* context = client.Instance()->context;
  Expects(context != nullptr, "callback context exists");
  Expects(context->gdi != nullptr, "decoded framebuffer exists");
  auto const& gdi    = *context->gdi;
  auto const  index  = (40 * 640) + static_cast<int>(column);
  bool const  origin = PatternPixel(gdi, index) == 0x00ff00 && (!column || PatternPixel(gdi, index - 1) != 0x00ff00);
  if (!received && origin) {
    matches  = Pattern(client, true);
    received = true;
  }
}
}
