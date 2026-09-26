#pragma once
#include <sdl-rdp/freerdp-facade/signalled.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/input/activated-channel.hpp>
#include <sdl-rdp/input/forward.hpp>
#include <sdl-rdp/link/channel-slot.hpp>
#include <sdl-rdp/link/peer-link.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <optional>

namespace sdl_rdp::input::detail::channel {
using sdl_rdp::freerdp_facade::Signalled;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::ChannelSlot;
using sdl_rdp::link::PeerLink;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Pinned;

template <class Protocol> class InputChannel final : private Pinned {
public:
  InputChannel(PeerLink& link, InputEvents& events) noexcept
      : _link{ link }, _events{ events }, _dynamic{ [this] { return Activate(); } }, _slot{ link.Dynamic(), _dynamic } {
  }
  auto Open()                       -> bool;
  auto Pump(Signalled const& ready) -> bool;
  auto Event() const                -> std::optional<WaitHandle>;

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
template <class Protocol> auto InputChannel<Protocol>::Pump(Signalled const& ready) -> bool {
  return !ready.Contains(Event()) || Protocol::Service(_context);
}
template <class Protocol> auto InputChannel<Protocol>::Event() const -> std::optional<WaitHandle> {
  if (!_ready) return std::nullopt;
  return Protocol::Handle(_context);
}
template <class Protocol> auto InputChannel<Protocol>::Activate() -> bool {
  Expects(_context != nullptr, "an activated input channel is open");
  _ready = true;
  return Protocol::Activate(_context);
}
}

namespace sdl_rdp::input {
using detail::channel::InputChannel;
}
