#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/freerdp-facade/advanced-input-channel.hpp>
#include <sdl-rdp/freerdp-facade/signalled.hpp>
#include <sdl-rdp/freerdp-facade/touch-channel.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/input/channel.hpp>
#include <sdl-rdp/input/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <span>

namespace sdl_rdp::input::detail::input {
using sdl_rdp::diagnostics::Diagnostics;
using sdl_rdp::freerdp_facade::AdvancedInputChannel;
using sdl_rdp::freerdp_facade::Signalled;
using sdl_rdp::freerdp_facade::TouchChannel;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Pinned;

inline constexpr std::size_t InputHandleLimit = 2;
class Input : private Pinned {
public:
       Input(PeerLink& link, InputEvents& events, Diagnostics const& diagnostics) noexcept;
  auto Channels(Signalled const& ready)         -> bool;
  auto Handles(std::span<WaitHandle> out) const -> std::span<WaitHandle>;

private:
  auto Open() -> bool;
  PeerLink&                          _link;
  InputChannel<AdvancedInputChannel> _advanced;
  InputChannel<TouchChannel>         _touch;
  bool                               _opened  { };
};
}

namespace sdl_rdp::input {
using detail::input::Input;
using detail::input::InputHandleLimit;
}
