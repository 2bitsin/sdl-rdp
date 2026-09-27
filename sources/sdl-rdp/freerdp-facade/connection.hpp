#pragma once
#include <sdl-rdp/freerdp-facade/connection-events.hpp>
#include <sdl-rdp/freerdp-facade/input-sink.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/utilities/posix.hpp>
#include <sdl-rdp/utilities/releases.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <string_view>

struct rdp_context;
struct rdp_freerdp_peer;

namespace sdl_rdp::freerdp_facade::detail::connection {
using sdl_rdp::freerdp_facade::ConnectionEvents;
using sdl_rdp::freerdp_facade::Identity;
using sdl_rdp::freerdp_facade::InputSink;
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Releases;

// abi: the release steps of a peer, handed the peer; the context goes before the peer that holds it.
auto ReleasePeer(rdp_freerdp_peer* peer) noexcept -> void;
// abi: the release step of an observation, handed the observed peer.
auto Unobserve(rdp_freerdp_peer* peer) noexcept -> void;
using PeerHandle = std::unique_ptr<rdp_freerdp_peer, Releases<ReleasePeer>>;
// Ends before its Connection: its release writes into the observed peer.
using Observation = std::unique_ptr<rdp_freerdp_peer, Releases<Unobserve>>;
// The error info a server sends before it closes a connection (MS-RDPBCGR 2.2.5.1.1).
enum class Refusal : std::uint8_t { ServerDenied, OtherConnection };
// The last error FreeRDP recorded on a connection, as far as the server tells them apart.
enum class Cause : std::uint8_t {
  None,
  TransportFailed,
  AuthenticationFailed,
  LogoffByUser,
  OtherConnection,
  RpcInitiated,
  ServerDenied,
  Other,
};
struct LastError {
  Cause            cause{ };
  std::string_view name;
};
// One client connection: FreeRDP's peer and its context, created together and released together.
class Connection {
public:
  explicit           Connection(PeerHandle accepted);
  explicit           Connection(Descriptor socket);
  [[nodiscard]] auto Observe(ConnectionEvents& events, InputSink& input) -> Observation;
  auto               Settings() const noexcept                           -> SettingsReader;
  auto               Settings() noexcept                                 -> SettingsView;
  auto               Initialize()                                        -> bool;
  auto               Pump()                                              -> bool;
  auto               EventHandles(std::span<WaitHandle> budget) const    -> std::span<WaitHandle>;
  auto               Active() const                                      -> bool;
  auto               WriteBlocked() const                                -> bool;
  auto               DrainOutput()                                       -> bool;
  auto               Socket() const noexcept                             -> int;
  auto               Hostname() const noexcept                           -> std::string_view;
  auto               Claimed() const                                     -> Identity;
  auto               Authenticated() const noexcept                      -> bool;
  auto               Identify(Identity const& identity)                  -> void;
  auto               SetAuthenticated(bool authenticated) noexcept       -> void;
  auto               OfferReconnect(std::uint32_t logon_id)              -> bool;
  auto               AcceptTls()                                         -> bool;
  auto               Error() const                                       -> LastError;
  auto               Refuse(Refusal refusal)                             -> void;
  auto               Disconnect() noexcept                               -> void;
  auto               Close()                                             -> void;
  // The raw context the channel, update and channel-context rounds (facade-3 to facade-5) still reach through.
  auto Context() noexcept -> rdp_context&;

private:
  auto Peer() const noexcept -> rdp_freerdp_peer&;
  int        _socket;
  PeerHandle _peer;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::connection::Cause;
using detail::connection::Connection;
using detail::connection::LastError;
using detail::connection::Observation;
using detail::connection::PeerHandle;
using detail::connection::Refusal;
}
