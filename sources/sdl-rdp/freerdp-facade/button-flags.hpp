#pragma once
#include <sdl-rdp/freerdp-facade/input-sink.hpp>

#include <array>
#include <bitset>
#include <concepts>
#include <cstddef>
#include <utility>

namespace sdl_rdp::freerdp_facade::detail::button_flags {
// A flag word read through a flag-to-button table, written once for the slow-path and ainput pointer decoders.
template <std::unsigned_integral FlagTy, std::size_t COUNT>
using ButtonFlags = std::array<std::pair<FlagTy, PointerButton>, COUNT>;
template <std::unsigned_integral FlagTy, std::size_t COUNT>
auto Buttons(FlagTy flags, ButtonFlags<FlagTy, COUNT> const& table) -> std::bitset<PointerButtonCount> {
  std::bitset<PointerButtonCount> buttons;
  for (auto const [flag, button] : table)
    if (flags & flag) buttons.set(std::to_underlying(button));
  return buttons;
}
}

namespace sdl_rdp::freerdp_facade {
using detail::button_flags::ButtonFlags;
using detail::button_flags::Buttons;
}
