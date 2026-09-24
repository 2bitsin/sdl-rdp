#pragma once
#include "dynamic-channels.hpp"
#include "pinned.hpp"
#include "rdp-handles.hpp"
#include "releases-peer.hpp"
#include "wake-event.hpp"

#include <freerdp/freerdp.h>
#include <winpr/wtsapi.h>
#include <concepts>

namespace Backend {
using ChannelManager = std::unique_ptr<void, Releases<WTSCloseServer>>;
class PeerLink : private Pinned {
public:
  explicit PeerLink(PeerHandle accepted);
  auto     Client() const noexcept              -> freerdp_peer&;
  auto     Context() const noexcept             -> rdpContext&;
  auto     Settings() const noexcept            -> rdpSettings&;
  auto     Channels() const noexcept            -> HANDLE;
  auto     Dynamic() noexcept                   -> DynamicChannels&;
  auto     Socket() const noexcept              -> int;
  auto     WriteBlocked() const                 -> bool;
  auto     Signal()                             -> void;
  auto     Settle()                             -> void;
  auto     Wake() const noexcept                -> HANDLE;
  auto     Invalidate() noexcept                -> void;
  auto     Handles(std::invocable auto collect) -> DWORD {
    if (!_handle_count) _handle_count = collect();
    return _handle_count;
  }
  auto Refuse(UINT32 reason) -> void;
  auto Close()               -> void;

private:
  PeerHandle      _client;
  int             _socket;
  WakeEvent       _wake;
  ChannelManager  _channels;
  DynamicChannels _dynamic;
  DWORD           _handle_count{ };
};
auto DynamicChannelsReady(PeerLink const& link)     -> bool;
auto Joined(PeerLink const& link, char const* name) -> bool;
}
