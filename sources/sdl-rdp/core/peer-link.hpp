#pragma once
#include <sdl-rdp/core/dynamic-channels.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/releases-peer.hpp>
#include <sdl-rdp/freerdp-facade/wake-event.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/freerdp.h>
#include <winpr/wtsapi.h>
#include <concepts>
#include <cstdint>

namespace Backend {
using ChannelManager = std::unique_ptr<void, Releases<WTSCloseServer>>;
class PeerLink : private Pinned {
public:
  explicit PeerLink(PeerHandle accepted);
  auto     Client() const noexcept              -> freerdp_peer&;
  auto     Context() const noexcept             -> rdpContext&;
  auto     Settings() const noexcept            -> rdpSettings&;
  auto     Channels() const noexcept            -> WaitHandle;
  auto     Dynamic() noexcept                   -> DynamicChannels&;
  auto     Socket() const noexcept              -> int;
  auto     WriteBlocked() const                 -> bool;
  auto     Signal()                             -> void;
  auto     Settle()                             -> void;
  auto     Wake() const noexcept                -> WaitHandle;
  auto     Invalidate() noexcept                -> void;
  auto     Handles(std::invocable auto collect) -> std::uint32_t {
    if (!_handle_count) _handle_count = collect();
    return _handle_count;
  }
  auto Refuse(std::uint32_t reason) -> void;
  auto Close()                      -> void;

private:
  PeerHandle      _client;
  int             _socket;
  WakeEvent       _wake;
  ChannelManager  _channels;
  DynamicChannels _dynamic;
  std::uint32_t   _handle_count{ };
};
auto DynamicChannelsReady(PeerLink const& link)     -> bool;
auto Joined(PeerLink const& link, char const* name) -> bool;
}
