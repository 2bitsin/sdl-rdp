#include <sdl-rdp/auth/tls-rehearsal.hpp>

#include <sdl-rdp/auth/exceptions.hpp>
#include <sdl-rdp/auth/unsignalled-socket-bio.hpp>
#include <sdl-rdp/freerdp-facade/settings.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/posix.hpp>

#include <openssl/ssl.h>
#include <cerrno>
#include <chrono>
#include <future>
#include <initializer_list>
#include <string_view>
#include <sys/socket.h>
#include <sys/time.h>
#include <tuple>
#include <utility>

namespace sdl_rdp::auth::detail::tls_rehearsal {
using sdl_rdp::auth::detail::unsignalled_socket_bio::UnsignalledSocketBio;
using sdl_rdp::freerdp_facade::Bio;
using sdl_rdp::utilities::ConnectedSockets;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Releases;
using sdl_rdp::utilities::SystemCall;

namespace {
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using SslContext = std::unique_ptr<SSL_CTX, Releases<SSL_CTX_free>>;
using SslSession = std::unique_ptr<SSL, Releases<SSL_free>>;

auto Serving(Descriptor socket, Credentials const& credentials) -> Connection {
  Expects(socket.Owns(), "the server end is open");
  Connection server{ std::move(socket) };
  server.Settings().InstallServerCredentials(credentials.Key(), credentials.Certificate());
  return server;
}
auto Timeval(std::chrono::microseconds span) -> timeval {
  auto const seconds = std::chrono::floor<std::chrono::seconds>(span);
  auto const rest    = span - seconds;
  return { .tv_sec = static_cast<time_t>(seconds.count()), .tv_usec = static_cast<suseconds_t>(rest.count()) };
}
auto LimitBlockedCalls(int socket, std::chrono::milliseconds limit) -> void {
  auto const bound = Timeval(limit);
  for (auto const option : { SO_RCVTIMEO, SO_SNDTIMEO })
    SystemCall(::setsockopt(socket, SOL_SOCKET, option, &bound, sizeof(bound)), "rehearsal blocked call limit");
}
auto Bounding(std::chrono::milliseconds limit) -> std::chrono::milliseconds {
  Expects(limit > std::chrono::milliseconds::zero(), "a zero limit would mean none");
  return limit;
}
auto AttachBio(SSL& session, Bio bio) -> void {
  Expects(bio != nullptr, "the BIO exists");
  auto* const shared = bio.release();
  SSL_set_bio(&session, shared, shared);
}
auto Handshake(int socket) -> bool {
  SslContext const context{ SSL_CTX_new(TLS_client_method()) };
  if (!context) return false;
  SslSession const session{ SSL_new(context.get()) };
  if (!session) return false;
  SSL_set_verify(session.get(), SSL_VERIFY_NONE, nullptr);
  AttachBio(*session, UnsignalledSocketBio(socket));
  return SSL_connect(session.get()) == 1;
}
auto StopDirection(int socket, int direction) noexcept -> void {
  if (::shutdown(socket, direction) != 0) Ensures(errno == ENOTCONN, "only a gone peer refuses a shutdown");
}
auto ConnectTls(int socket, std::chrono::milliseconds limit) noexcept -> bool {
  Expects(socket >= 0, "the client socket is open");
  auto const handshake = [socket, limit] {
    LimitBlockedCalls(socket, limit);
    return Handshake(socket);
  };
  // The false result reaches Perform, which throws the client's failure on the calling thread.
  auto const connected = Contained(false, handshake, [](std::string_view) noexcept { });
  StopDirection(socket, SHUT_WR);
  return connected;
}
}
TlsRehearsal::TlsRehearsal(Credentials const& credentials, std::chrono::milliseconds limit)
    : blocked_call_limit(Bounding(limit)), ends(ConnectedSockets()),
      server(Serving(std::move(ends.server), credentials)) { }
auto TlsRehearsal::Perform() && -> void {
  auto       handshake = std::async(std::launch::async, ConnectTls, ends.client.Get(), blocked_call_limit);
  auto const accepted  = server.AcceptTls();
  // FreeRDP 3.32 transport.c:708 keeps the server socket open after a failed accept, so the client would wait for it.
  StopDirection(ends.client.Get(), SHUT_RD);
  auto const connected = handshake.get();
  if (!accepted) throw TlsAcceptRefused{ };
  if (!connected) throw TlsHandshakeFailed{ };
}
}
