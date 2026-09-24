#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/input/advanced-protocol.hpp>
#include <sdl-rdp/input/touch-protocol.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <cstddef>
#include <span>

namespace Backend {
inline constexpr std::size_t InputHandleLimit = 2;
class InputEvents;
class PeerLink;
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
