#include "_detail/tls-rehearsal.hpp"

#include "_detail/contract.hpp"
#include "_detail/system-call.hpp"
#include "_detail/tls-accept-refused.hpp"
#include "_detail/unsignalled-socket-bio.hpp"

#include <cerrno>
#include <chrono>
#include <freerdp/freerdp.h>
#include <freerdp/peer.h>
#include <freerdp/transport_io.h>
#include <future>
#include <initializer_list>
#include <openssl/ssl.h>
#include <stdexcept>
#include <sys/socket.h>
#include <sys/time.h>
#include <tuple>
#include <utility>

namespace Backend {
namespace {
using utilities::Ensures;
using utilities::Expects;
using SslContext = std::unique_ptr<SSL_CTX, Releases<SSL_CTX_free>>;
using SslSession = std::unique_ptr<SSL, Releases<SSL_free>>;

auto AdoptedPeer(Descriptor socket) -> PeerHandle {
  PeerHandle peer{ freerdp_peer_new(socket.Get()) };
  if (!peer) throw std::runtime_error("TLS rehearsal peer allocation failed.");
  std::ignore = socket.Release();
  return peer;
}
auto ServingPeer(Descriptor socket, Credentials const& credentials) -> PeerHandle {
  Expects(socket.Owns(), "the server end is open");
  auto peer = AdoptedPeer(std::move(socket));
  if (!freerdp_peer_context_new(peer.get())) throw std::runtime_error("TLS rehearsal peer context failed.");
  Ensures(peer->context != nullptr, "the peer has a context");
  InstallServerCredentials(*peer->context->settings, credentials);
  return peer;
}
auto AcceptTls(freerdp_peer& peer) -> bool {
  Expects(peer.context != nullptr, "the peer has a context");
  auto const* const io = freerdp_get_io_callbacks(peer.context);
  Expects(io != nullptr, "the peer has transport callbacks");
  Expects(io->TLSAccept != nullptr, "the peer can accept TLS");
  return io->TLSAccept(freerdp_get_transport(peer.context)) == TRUE;
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
  auto const connected = [socket, limit] {
    try {
      LimitBlockedCalls(socket, limit);
      return Handshake(socket);
    } catch (std::exception const&) {
      return false;
    }
  }();
  StopDirection(socket, SHUT_WR);
  return connected;
}
}
TlsRehearsal::TlsRehearsal(Credentials const& credentials, std::chrono::milliseconds limit)
    : blocked_call_limit(Bounding(limit)), server(ServingPeer(ends.TakeServer(), credentials)) { }
auto TlsRehearsal::Perform() && -> void {
  auto       handshake = std::async(std::launch::async, ConnectTls, ends.Client(), blocked_call_limit);
  auto const accepted  = AcceptTls(*server);
  // FreeRDP 3.15 keeps the server socket open after a failed accept, so the client would wait for it.
  StopDirection(ends.Client(), SHUT_RD);
  auto const connected = handshake.get();
  if (!accepted) throw TlsAcceptRefused("TLS rehearsal accept failed.");
  if (!connected) throw std::runtime_error("TLS rehearsal client handshake failed.");
}
}
