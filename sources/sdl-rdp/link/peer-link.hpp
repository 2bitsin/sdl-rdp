#pragma once
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/freerdp-facade/wake-event.hpp>
#include <sdl-rdp/link/dynamic-channels.hpp>
#include <sdl-rdp/utilities/pinned.hpp>

#include <freerdp/freerdp.h>
#include <concepts>
#include <cstdint>
#include <string>
#include <string_view>

namespace sdl_rdp::link::detail::peer_link {
using sdl_rdp::freerdp_facade::ChannelManager;
using sdl_rdp::freerdp_facade::PeerHandle;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::freerdp_facade::WakeEvent;
using sdl_rdp::utilities::Pinned;

class PeerLink : private Pinned {
public:
  explicit PeerLink(PeerHandle accepted);
  auto     Client() const noexcept              -> freerdp_peer&;
  auto     Context() const noexcept             -> rdpContext&;
  auto     Settings() const noexcept            -> rdpSettings&;
  auto     Channels() const noexcept            -> ChannelManager const&;
  auto     Dynamic() noexcept                   -> DynamicChannels&;
  auto     Socket() const noexcept              -> int;
  auto     WriteBlocked() const                 -> bool;
  auto     Signal()                             -> void;
  auto     Settle()                             -> void;
  auto     Wake() const                         -> WaitHandle;
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
auto ClientHostname(PeerLink const& link)                -> std::string;
auto DynamicChannelsReady(PeerLink const& link)          -> bool;
auto Joined(PeerLink const& link, std::string_view name) -> bool;
}

namespace sdl_rdp::link {
using detail::peer_link::ClientHostname;
using detail::peer_link::DynamicChannelsReady;
using detail::peer_link::Joined;
using detail::peer_link::PeerLink;
}
