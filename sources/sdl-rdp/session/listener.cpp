#include <sdl-rdp/session/listener.hpp>

#include <sdl-rdp/auth/tls-rehearsal.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/manual-reset-event.hpp>
#include <sdl-rdp/peer/peer.hpp>
#include <sdl-rdp/session/exceptions.hpp>
#include <sdl-rdp/session/session.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/descriptor.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/system-call.hpp>

#include <freerdp/channels/channels.h>
#include <winpr/ssl.h>
#include <winpr/synch.h>
#include <winpr/wtsapi.h>
#include <arpa/inet.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <tuple>
#include <utility>

namespace Backend {
auto CloseListener(freerdp_listener* listener) noexcept -> void {
  listener->Close(listener);
}
namespace {
constexpr int           ListenBacklog      = 8;
constexpr std::size_t   WaitHandleCapacity = 32;
constexpr std::uint32_t ListenerOwnHandles = 2;
// Process-wide and idempotent; OpenSSL 3 releases its state at exit, so neither has a release call.
auto InitializeProcess(Credentials const& credentials) -> void {
  static std::once_flag once;
  std::call_once(once, [&credentials] {
    WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());
    if (!winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT)) throw ListenerSetupFailed{ "OpenSSL initialisation" };
    // FreeRDP's lazily filled BIO tables are process-wide, so one rehearsal per process fills them for every listener.
    TlsRehearsal{ credentials }.Perform();
  });
}
auto Address(sdlrdp_config const& config) -> sockaddr_in {
  sockaddr_in address{ };
  address.sin_family = AF_INET;
  address.sin_port   = htons(config.port);
  auto const* const bind = config.bind ? config.bind : "0.0.0.0";
  if (inet_pton(AF_INET, bind, &address.sin_addr) != 1) throw AddressNotIpv4{ bind };
  return address;
}
auto Generic(sockaddr_in& address) -> sockaddr* {
  // POSIX socket calls take an IPv4 address through the generic sockaddr it begins with.
  return reinterpret_cast<sockaddr*>(&address);
}
auto StartListening(Descriptor const& socket, sockaddr_in& address) -> void {
  int reuse = 1;
  SystemCall(setsockopt(socket.Get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)), "Socket options");
  SystemCall(::bind(socket.Get(), Generic(address), sizeof(address)), "Listener bind");
  SystemCall(::listen(socket.Get(), ListenBacklog), "Listener listen");
  socklen_t size = sizeof(address);
  SystemCall(getsockname(socket.Get(), Generic(address), &size), "Listener socket name");
}
auto AdoptListenerSocket(freerdp_listener& listener, Descriptor socket) -> void {
  if (!listener.OpenFromSocket(&listener, socket.Get())) throw ListenerSetupFailed{ "socket adoption" };
  std::ignore = socket.Release();
}
auto Bind(freerdp_listener& listener, sdlrdp_config const& config) -> std::uint32_t {
  Descriptor socket  { SystemCall(::socket(AF_INET, SOCK_STREAM, 0), "Socket creation") };
  auto       address = Address(config);
  StartListening(socket, address);
  AdoptListenerSocket(listener, std::move(socket));
  return ntohs(address.sin_port);
}
auto NewListener(Credentials const& credentials) -> ListenerHandle {
  InitializeProcess(credentials);
  ListenerHandle listener{ freerdp_listener_new() };
  if (!listener) throw AllocationFailed{ "Listener" };
  return listener;
}
auto NewStopEvent() -> EventHandle {
  return sdl_rdp::freerdp_facade::ManualResetEvent("Listener stop event");
}
}
Listener::Listener(Configuration const& configuration, Credentials const& credentials, Diagnostics const& diagnostics,
                   Session& session, PeerFactory make)
    : _diagnostics{ diagnostics }, _session{ session }, _make{ std::move(make) }, _listener{ NewListener(credentials) },
      _stop{ NewStopEvent() }, _port{ Bind(*_listener, configuration.Config()) } {
  _listener->info = this;
  // abi: psPeerAccepted, BOOL is int
  _listener->PeerAccepted = [](freerdp_listener* accepting, freerdp_peer* client) noexcept -> int {
    auto&      owner    = CallbackOwner<Listener>(accepting->info);
    auto const accepted = [&] {
      owner.Accept(client);
      return true;
    };
    // True transfers ownership even when construction failed and RAII already released the peer.
    return Contained(true, accepted, FailureLog{ owner._diagnostics, "Peer construction" });
  };
  _diagnostics.Log(SDLRDP_LOG_INFO, std::format("Listening on port {}", _port));
  _thread = std::jthread([this](std::stop_token const& quit) { Listen(quit); });
  Ensures(_port != 0, "bound port is available");
}
auto Listener::Port() const noexcept -> std::uint32_t {
  return _port;
}
auto Listener::Accept(freerdp_peer* client) -> void {
  PeerHandle accepted{ client };
  _diagnostics.Log(SDLRDP_LOG_INFO, std::format("Peer accepted: {}.", client->hostname));
  _session.Add(_make(std::move(accepted)));
}
auto Listener::Listen(std::stop_token const& quit) -> void {
  std::stop_callback const                   wake(quit, [this] { SetEvent(_stop.get()); });
  std::array<WaitHandle, WaitHandleCapacity> handles{ };
  while (!quit.stop_requested()) {
    auto count = _listener->GetEventHandles(_listener.get(), handles.data(), handles.size() - ListenerOwnHandles);
    if (!count) break;
    handles[count++] = _stop.get();
    handles[count++] = _session.ReapEvent();
    if (WaitForMultipleObjects(count, handles.data(), false, INFINITE) == WAIT_FAILED || quit.stop_requested()
        || !_listener->CheckFileDescriptor(_listener.get()))
      break;
    _session.Reap();
  }
}
}
