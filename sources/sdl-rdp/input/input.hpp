#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/input/forward.hpp>
#include <sdl-rdp/input/protocol.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <span>

namespace sdl_rdp::input::detail::input {
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Pinned;

inline constexpr std::size_t InputHandleLimit = 2;
class Input : private Pinned {
public:
       Input(PeerLink& link, InputEvents& events) noexcept;
  auto Channels(std::span<WaitHandle const> ready) -> bool;
  auto Handles(std::span<WaitHandle> out) const    -> std::span<WaitHandle>;

private:
  auto Open() -> bool;
  PeerLink&                      _link;
  InputChannel<AdvancedProtocol> _advanced;
  InputChannel<TouchProtocol>    _touch;
  bool                           _opened  { };
};
}

namespace sdl_rdp::input {
using detail::input::Input;
using detail::input::InputHandleLimit;
}
