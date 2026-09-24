#pragma once
#include "advanced-protocol.hpp"
#include "pinned.hpp"
#include "touch-protocol.hpp"

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
