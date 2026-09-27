#pragma once
#include <sdl-rdp/auth/certificate.hpp>
#include <sdl-rdp/freerdp-facade/connection.hpp>
#include <sdl-rdp/utilities/posix.hpp>

#include <chrono>

namespace sdl_rdp::auth::detail::tls_rehearsal {
using sdl_rdp::freerdp_facade::Connection;
using sdl_rdp::utilities::SocketPair;

// Bounds each blocked send or receive of the rehearsal client: a local handshake takes milliseconds, a hung peer never.
inline constexpr std::chrono::milliseconds RehearsalBlockedCallLimit{ 10'000 };

// FreeRDP 3.32 tcp.c:431/646 and tls.c:672 fill BIO tables on first use without a lock (2bitsin/FreeRDP#2).
class TlsRehearsal {
public:
  explicit TlsRehearsal(Credentials const& credentials, std::chrono::milliseconds limit = RehearsalBlockedCallLimit);
  auto     Perform() && -> void;

private:
  std::chrono::milliseconds blocked_call_limit;
  SocketPair                ends;
  Connection                server;
};
}

namespace sdl_rdp::auth {
using detail::tls_rehearsal::TlsRehearsal;
}
