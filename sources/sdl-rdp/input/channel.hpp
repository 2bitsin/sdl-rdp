#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/input/activated-channel.hpp>
#include <sdl-rdp/link/channel-slot.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <algorithm>
#include <span>

namespace Backend {
class InputEvents;
template <class Protocol> class InputChannel final : private Pinned {
public:
  InputChannel(PeerLink& link, InputEvents& events) noexcept
      : _link{ link }, _events{ events }, _dynamic{ [this] { return Activate(); } }, _slot{ link.Dynamic(), _dynamic } {
  }
  auto Open()                                  -> bool;
  auto Pump(std::span<WaitHandle const> ready) -> bool;
  auto Event() const                           -> WaitHandle;

private:
  auto Activate()                     -> bool;
  auto FailureSource() const noexcept -> InputEvents const&;
  friend                     Protocol;
  PeerLink&                  _link;
  InputEvents&               _events;
  typename Protocol::Context _context;
  ActivatedChannel           _dynamic;
  ChannelSlot                _slot;
  bool                       _ready  { };
};
template <class Protocol> auto InputChannel<Protocol>::FailureSource() const noexcept -> InputEvents const& {
  return _events;
}
template <class Protocol> auto InputChannel<Protocol>::Open() -> bool {
  _context = Protocol::Open(_link, *this);
  return _context != nullptr;
}
template <class Protocol> auto InputChannel<Protocol>::Pump(std::span<WaitHandle const> ready) -> bool {
  return !_ready || !std::ranges::contains(ready, Event()) || Protocol::Service(_context);
}
template <class Protocol> auto InputChannel<Protocol>::Event() const -> WaitHandle {
  return _ready ? Protocol::Handle(_context) : nullptr;
}
template <class Protocol> auto InputChannel<Protocol>::Activate() -> bool {
  Expects(_context != nullptr, "an activated input channel is open");
  _ready = true;
  return Protocol::Activate(_context);
}
}
