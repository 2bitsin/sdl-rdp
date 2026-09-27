#include <sdl-rdp/input/input.hpp>

#include <sdl-rdp/link/peer-link.hpp>

#include <algorithm>
#include <array>
#include <ranges>

namespace sdl_rdp::input::detail::input {
using sdl_rdp::utilities::Expects;

Input::Input(PeerLink& link, InputEvents& events, Diagnostics const& diagnostics) noexcept
    : _link{ link }, _advanced{ link, events, diagnostics }, _touch{ link, events, diagnostics } { }
auto Input::Channels(Signalled const& ready) -> bool {
  if (!_link.Channels().DynamicReady()) return true;
  if (!_opened) return Open();
  return _advanced.Pump(ready) && _touch.Pump(ready);
}
auto Input::Handles(std::span<WaitHandle> out) const -> std::span<WaitHandle> {
  Expects(out.size() >= InputHandleLimit, "handle span has room for the input channels");
  auto const events = std::array{ _advanced.Event(), _touch.Event() };
  auto const next   = std::ranges::copy(events | std::views::join, out.begin()).out;
  return { next, out.end() };
}
auto Input::Open() -> bool {
  _opened = true;
  _link.Invalidate();
  return _advanced.Open() && _touch.Open();
}
}
