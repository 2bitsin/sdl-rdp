#pragma once
#include <sdl-rdp/input/advanced-protocol.hpp>
#include <sdl-rdp/input/touch-protocol.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <span>

namespace Backend {
inline constexpr unsigned InputHandleLimit = 2;
class InputEvents;
class PeerLink;
class Input : private Pinned {
public:
       Input(PeerLink& link, InputEvents& events) noexcept;
  auto Channels(std::span<HANDLE const> ready) -> bool;
  auto Handles(std::span<HANDLE> out) const    -> std::span<HANDLE>;

private:
  auto Open() -> bool;
  PeerLink&                      _link;
  InputChannel<AdvancedProtocol> _advanced;
  InputChannel<TouchProtocol>    _touch;
  bool                           _opened  { };
};
}
