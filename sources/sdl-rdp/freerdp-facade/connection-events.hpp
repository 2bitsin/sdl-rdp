#pragma once
#include <sdl-rdp/freerdp-facade/failure-sink.hpp>
#include <sdl-rdp/utilities/nt-owf.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace sdl_rdp::freerdp_facade::detail::connection_events {
using sdl_rdp::utilities::NtOwf;

struct Identity {
  std::string user;
  std::string domain;
};
// What a connection's peer and update slots report, installed by Connection::Observe.
class ConnectionEvents : public FailureSink {
public:
  virtual auto Activate()                             -> bool                 = 0;
  virtual auto Capabilities()                         -> bool                 = 0;
  virtual auto Logon(bool automatic)                  -> bool                 = 0;
  virtual auto NtlmHash(Identity const& identity)     -> std::optional<NtOwf> = 0;
  virtual auto NtlmRefused(std::string_view cause)    -> void                 = 0;
  virtual auto FrameAcknowledged(std::uint32_t frame) -> void                 = 0;
  virtual auto SuppressOutput(bool allow)             -> void                 = 0;
};
}

namespace sdl_rdp::freerdp_facade {
using detail::connection_events::ConnectionEvents;
using detail::connection_events::Identity;
}
