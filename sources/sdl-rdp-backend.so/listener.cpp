#include "_detail/listener.hpp"

#include "_detail/callback-owner.hpp"
#include "_detail/configuration.hpp"
#include "_detail/contract.hpp"
#include "_detail/descriptor.hpp"
#include "_detail/diagnostics.hpp"
#include "_detail/peer.hpp"
#include "_detail/session.hpp"
#include "_detail/system-call.hpp"
#include "_detail/tls-rehearsal.hpp"

#include <arpa/inet.h>
#include <array>
#include <format>
#include <freerdp/channels/channels.h>
#include <mutex>
#include <stdexcept>
#include <tuple>
#include <utility>
#include <winpr/ssl.h>
#include <winpr/synch.h>
#include <winpr/wtsapi.h>

namespace Backend {
namespace {
constexpr int         ListenBacklog      = 8;
constexpr std::size_t WaitHandleCapacity = 32;
constexpr DWORD       ListenerOwnHandles = 2;
// Process-wide and idempotent; OpenSSL 3 releases its state at exit, so neither has a release call.
void InitializeProcess(Credentials const& credentials) {
  static std::once_flag once;
  std::call_once(once, [&credentials] {
    WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi());
    if (!winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT)) throw std::runtime_error("OpenSSL initialisation failed.");
    // FreeRDP's lazily filled BIO tables are process-wide, so one rehearsal per process fills them for every listener.
    TlsRehearsal{ credentials }.Perform();
  });
}
sockaddr_in Address(sdlrdp_config const& config) {
  sockaddr_in address{ };
  address.sin_family = AF_INET;
  address.sin_port   = htons(config.port);
  if (inet_pton(AF_INET, config.bind ? config.bind : "0.0.0.0", &address.sin_addr) != 1)
    throw std::runtime_error("Listener address is not numeric IPv4.");
  return address;
}
void StartListening(Descriptor const& socket, sockaddr_in& address) {
  int reuse = 1;
  SystemCall(setsockopt(socket.Get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)), "Socket options");
  SystemCall(::bind(socket.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)), "Listener bind");
  SystemCall(::listen(socket.Get(), ListenBacklog), "Listener listen");
  socklen_t size = sizeof(address);
  SystemCall(getsockname(socket.Get(), reinterpret_cast<sockaddr*>(&address), &size), "Listener socket name");
}
void AdoptListenerSocket(freerdp_listener& listener, Descriptor socket) {
  if (!listener.OpenFromSocket(&listener, socket.Get()))
    throw std::runtime_error("FreeRDP listener socket adoption failed.");
  std::ignore = socket.Release();
}
unsigned Bind(freerdp_listener& listener, sdlrdp_config const& config) {
  Descriptor socket  { SystemCall(::socket(AF_INET, SOCK_STREAM, 0), "Socket creation") };
  auto       address = Address(config);
  StartListening(socket, address);
  AdoptListenerSocket(listener, std::move(socket));
  return ntohs(address.sin_port);
}
ListenerHandle NewListener(Credentials const& credentials) {
  InitializeProcess(credentials);
  ListenerHandle listener{ freerdp_listener_new() };
  if (!listener) throw std::runtime_error("listener allocation failed");
  return listener;
}
EventHandle NewStopEvent() {
  EventHandle stop{ CreateEvent(nullptr, TRUE, FALSE, nullptr) };
  if (!stop) throw std::runtime_error("listener stop event allocation failed");
  return stop;
}
}
Listener::Listener(Configuration const& configuration, Diagnostics const& diagnostics, Session& session,
                   PeerFactory make)
    : _diagnostics{ diagnostics }, _session{ session }, _make{ std::move(make) },
      _listener{ NewListener(configuration.ServerCredentials()) },
      _stop{ NewStopEvent() }, _port{ Bind(*_listener, configuration.Config()) } {
  _listener->info = this;
  _listener->PeerAccepted = [](freerdp_listener* accepting, freerdp_peer* client) -> BOOL {
    CallbackOwner<Listener>(accepting->info).Accept(client);
    // TRUE transfers ownership even when construction failed and RAII already released the peer.
    return TRUE;
  };
  _diagnostics.Log(SDLRDP_LOG_INFO, std::format("Listening on port {}", _port));
  _thread = std::jthread([this](std::stop_token const& quit) { Listen(quit); });
  Ensures(_port != 0, "bound port is available");
}
unsigned Listener::Port() const noexcept {
  return _port;
}
void Listener::Accept(freerdp_peer* client) {
  PeerHandle accepted{ client };
  try {
    _diagnostics.Log(SDLRDP_LOG_INFO, std::format("Peer accepted: {}.", client->hostname));
    _session.Add(_make(std::move(accepted)));
  } catch (std::exception const& error) {
    _diagnostics.Log(SDLRDP_LOG_ERROR, std::format("Peer construction failed: {}.", error.what()));
  }
}
void Listener::Listen(std::stop_token const& quit) {
  std::stop_callback const               wake(quit, [this] { SetEvent(_stop.get()); });
  std::array<HANDLE, WaitHandleCapacity> handles{ };
  while (!quit.stop_requested()) {
    auto count = _listener->GetEventHandles(_listener.get(), handles.data(), handles.size() - ListenerOwnHandles);
    if (!count) break;
    handles[count++] = _stop.get();
    handles[count++] = _session.ReapEvent();
    if (WaitForMultipleObjects(count, handles.data(), FALSE, INFINITE) == WAIT_FAILED || quit.stop_requested() ||
        !_listener->CheckFileDescriptor(_listener.get()))
      break;
    _session.Reap();
  }
}
}
