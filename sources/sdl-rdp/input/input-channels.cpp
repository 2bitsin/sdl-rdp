#include <sdl-rdp/input/input.hpp>

#include <sdl-rdp/core/peer-link.hpp>

#include <algorithm>
#include <array>
#include <ranges>

namespace Backend {
Input::Input(PeerLink& link, InputEvents& events) noexcept
    : _link{ link }, _advanced{ link, events }, _touch{ link, events } { }
auto Input::Channels(std::span<HANDLE const> ready) -> bool {
  if (!DynamicChannelsReady(_link)) return true;
  if (!_opened) return Open();
  return _advanced.Pump(ready) && _touch.Pump(ready);
}
auto Input::Handles(std::span<HANDLE> out) const -> std::span<HANDLE> {
  Expects(out.size() >= InputHandleLimit, "handle span has room for the input channels");
  auto const open   = [](HANDLE event) { return event != nullptr; };
  auto const events = std::array{ _advanced.Event(), _touch.Event() };
  auto const next   = std::ranges::copy(events | std::views::filter(open), out.begin()).out;
  return { next, out.end() };
}
auto Input::Open() -> bool {
  _opened = true;
  _link.Invalidate();
  return _advanced.Open() && _touch.Open();
}
}
