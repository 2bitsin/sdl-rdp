#pragma once
#include <sdl-rdp/utilities/pinned.hpp>

#include <winpr/wtypes.h>
#include <span>
#include <stop_token>

namespace Backend {
class ChannelSet;
class FrameSender;
class PeerLink;
class Redirection;
class SessionAccess;
class TraceQueue;
class TransportEnd;
class PeerPump : private Pinned {
public:
  PeerPump(PeerLink& link, SessionAccess& session, ChannelSet& channels, Redirection& redirection, FrameSender& sender,
           TransportEnd& end, TraceQueue& traces) noexcept;
  auto Service(std::stop_token const& quit, std::span<HANDLE const> ready) -> bool;

private:
  auto Exchange(std::stop_token const& quit, std::span<HANDLE const> ready) -> bool;
  auto Deliver(std::stop_token const& quit)                                 -> bool;
  auto Ended()                                                              -> bool;
  PeerLink&      _link;
  SessionAccess& _session;
  ChannelSet&    _channels;
  Redirection&   _redirection;
  FrameSender&   _sender;
  TransportEnd&  _end;
  TraceQueue&    _traces;
};
}
