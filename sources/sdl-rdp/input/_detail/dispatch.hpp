#pragma once
#include <sdl-rdp/input/events.hpp>
#include <sdl-rdp/link/activation.hpp>
#include <sdl-rdp/link/event-queue.hpp>
#include <sdl-rdp/link/session-access.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>

#include <concepts>
#include <cstdint>
#include <ranges>
#include <span>

namespace Backend {
template <class Result> auto InputEvents::WhenActive(Result idle, std::invocable auto action) -> Result {
  auto const session = _session.Lock();
  return _activation.Active() ? Result{ action() } : idle;
}
template <std::unsigned_integral Flags>
auto PushButtons(EventQueue& events, std::span<Flags const> buttons, Flags flags, std::uint32_t first, bool down)
    -> void {
  for (auto const [index, button] : std::views::enumerate(buttons))
    if (flags & button)
      events.Push({ .type         = SDLRDP_MOUSE_BUTTON,
                    .mouse_button = { .button = first + Narrowed<std::uint32_t>(index), .down = down } });
}
}
