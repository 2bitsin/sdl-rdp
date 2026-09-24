#pragma once
#include "activated-channel.hpp"
#include "channel-slot.hpp"
#include "contract.hpp"
#include "peer-link.hpp"
#include "pinned.hpp"

#include <algorithm>
#include <span>

namespace Backend {
class InputEvents;
template <class Protocol> class InputChannel final : private Pinned {
public:
  InputChannel(PeerLink& link, InputEvents& events) noexcept
      : _link{ link }, _events{ events }, _dynamic{ [this] { return Activate(); } }, _slot{ link.Dynamic(), _dynamic } {
  }
  auto Open()                              -> bool;
  auto Pump(std::span<HANDLE const> ready) -> bool;
  auto Event() const                       -> HANDLE;

private:
  auto Activate() -> bool;
  friend                     Protocol;
  PeerLink&                  _link;
  InputEvents&               _events;
  typename Protocol::Context _context;
  ActivatedChannel           _dynamic;
  ChannelSlot                _slot;
  bool                       _ready  { };
};
template <class Protocol> auto InputChannel<Protocol>::Open() -> bool {
  _context = Protocol::Open(_link, *this);
  return _context != nullptr;
}
template <class Protocol> auto InputChannel<Protocol>::Pump(std::span<HANDLE const> ready) -> bool {
  return !_ready || !std::ranges::contains(ready, Event()) || Protocol::Service(_context);
}
template <class Protocol> auto InputChannel<Protocol>::Event() const -> HANDLE {
  return _ready ? Protocol::Handle(_context) : nullptr;
}
template <class Protocol> auto InputChannel<Protocol>::Activate() -> bool {
  Expects(_context != nullptr, "an activated input channel is open");
  _ready = true;
  return Protocol::Activate(_context);
}
}
