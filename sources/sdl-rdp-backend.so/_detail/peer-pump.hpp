#pragma once
#include "pinned.hpp"

#include <span>
#include <stop_token>
#include <winpr/wtypes.h>

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
  bool Service(std::stop_token const& quit, std::span<HANDLE const> ready);

private:
  bool Exchange(std::stop_token const& quit, std::span<HANDLE const> ready);
  bool Deliver(std::stop_token const& quit);
  bool Ended();
  PeerLink&      _link;
  SessionAccess& _session;
  ChannelSet&    _channels;
  Redirection&   _redirection;
  FrameSender&   _sender;
  TransportEnd&  _end;
  TraceQueue&    _traces;
};
}
