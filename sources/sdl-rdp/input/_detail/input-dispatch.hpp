#pragma once
#include <sdl-rdp/core/activation.hpp>
#include <sdl-rdp/core/event-queue.hpp>
#include <sdl-rdp/core/session-access.hpp>
#include <sdl-rdp/input/input-events.hpp>

#include <concepts>
#include <ranges>
#include <span>

namespace Backend {
template <class Result> auto InputEvents::WhenActive(Result idle, std::invocable auto action) -> Result {
  auto const session = _session.Lock();
  return _activation.Active() ? Result(action()) : idle;
}
template <std::unsigned_integral Flags>
auto PushButtons(EventQueue& events, std::span<Flags const> buttons, Flags flags, unsigned first, bool down) -> void {
  for (auto const [index, button] : std::views::enumerate(buttons))
    if (flags & button)
      events.Push({ .type = SDLRDP_MOUSE_BUTTON, .mouse_button = { .button = first + unsigned(index), .down = down } });
}
}
