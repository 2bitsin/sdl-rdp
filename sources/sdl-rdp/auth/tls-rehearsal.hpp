#pragma once
#include <sdl-rdp/core/certificate.hpp>
#include <sdl-rdp/freerdp-facade/releases-peer.hpp>
#include <sdl-rdp/utilities/socket-pair.hpp>

#include <chrono>

namespace Backend {
// Bounds each blocked send or receive of the rehearsal client: a local handshake takes milliseconds, a hung peer never.
inline constexpr std::chrono::milliseconds RehearsalBlockedCallLimit{ 10'000 };

// FreeRDP 3.15 fills tcp.c's socket BIO tables (peer context) and tls.c's (TLS accept) on first use without a lock.
class TlsRehearsal {
public:
  explicit TlsRehearsal(Credentials const& credentials, std::chrono::milliseconds limit = RehearsalBlockedCallLimit);
  auto     Perform() && -> void;

private:
  std::chrono::milliseconds blocked_call_limit;
  SocketPair                ends;
  PeerHandle                server;
};
}
