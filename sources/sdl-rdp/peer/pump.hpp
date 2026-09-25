#pragma once
#include <sdl-rdp/diagnostics/forward.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/link/forward.hpp>
#include <sdl-rdp/peer/forward.hpp>
#include <sdl-rdp/utilities/pinned.hpp>
#include <sdl-rdp/video/frame/forward.hpp>

#include <winpr/wtypes.h>
#include <span>
#include <stop_token>

namespace sdl_rdp::peer::detail::pump {
using sdl_rdp::diagnostics::TraceQueue;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::link::PeerLink;
using sdl_rdp::link::SessionAccess;
using sdl_rdp::utilities::Pinned;
using sdl_rdp::video::frame::FrameSender;

class PeerPump : private Pinned {
public:
  PeerPump(PeerLink& link, SessionAccess& session, ChannelSet& channels, Redirection& redirection, FrameSender& sender,
           TransportEnd& end, TraceQueue& traces) noexcept;
  auto Service(std::stop_token const& quit, std::span<WaitHandle const> ready) -> bool;

private:
  auto Exchange(std::stop_token const& quit, std::span<WaitHandle const> ready) -> bool;
  auto Deliver(std::stop_token const& quit)                                     -> bool;
  auto Ended()                                                                  -> bool;
  PeerLink&      _link;
  SessionAccess& _session;
  ChannelSet&    _channels;
  Redirection&   _redirection;
  FrameSender&   _sender;
  TransportEnd&  _end;
  TraceQueue&    _traces;
};
}

namespace sdl_rdp::peer {
using detail::pump::PeerPump;
}
