#include <sdl-rdp/session/listener.hpp>

#include <sdl-rdp/auth/tls-rehearsal.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/freerdp-facade/callback-owner.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
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
#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <format>
#include <mutex>
#include <span>
#include <tuple>
#include <utility>

namespace sdl_rdp::session::detail::listener {
using sdl_rdp::auth::TlsRehearsal;
using sdl_rdp::configuration::Setup;
using sdl_rdp::diagnostics::FailureLog;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::freerdp_facade::CallbackOwner;
using sdl_rdp::freerdp_facade::Forever;
using sdl_rdp::freerdp_facade::ManualResetEvent;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::utilities::AllocationFailed;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Descriptor;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::SystemCall;

auto CloseListener(freerdp_listener* listener) noexcept -> void {
  listener->Close(listener);
}
namespace {
constexpr int         ListenBacklog      = 8;
constexpr std::size_t WaitHandleCapacity = 32;
constexpr std::size_t ListenerOwnHandles = 2;
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
auto Address(Setup const& config) -> sockaddr_in {
  sockaddr_in address{ };
  address.sin_family = AF_INET;
  address.sin_port   = htons(config.port);
  auto const bind = config.bind.value_or("0.0.0.0");
  if (inet_pton(AF_INET, bind.c_str(), &address.sin_addr) != 1) throw AddressNotIpv4{ bind };
  return address;
}
auto Generic(sockaddr_in& address) -> sockaddr& {
  // POSIX socket calls take an IPv4 address through the generic sockaddr it begins with.
  return reinterpret_cast<sockaddr&>(address);
}
auto StartListening(Descriptor const& socket, sockaddr_in& address) -> void {
  int reuse = 1;
  SystemCall(setsockopt(socket.Get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)), "Socket options");
  SystemCall(::bind(socket.Get(), &Generic(address), sizeof(address)), "Listener bind");
  SystemCall(::listen(socket.Get(), ListenBacklog), "Listener listen");
  socklen_t size = sizeof(address);
  SystemCall(getsockname(socket.Get(), &Generic(address), &size), "Listener socket name");
}
auto AdoptListenerSocket(freerdp_listener& listener, Descriptor socket) -> void {
  if (!listener.OpenFromSocket(&listener, socket.Get())) throw ListenerSetupFailed{ "socket adoption" };
  std::ignore = socket.Release();
}
auto Bind(freerdp_listener& listener, Setup const& config) -> std::uint32_t {
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
  return ManualResetEvent("Listener stop event");
}
}
Listener::Listener(Configuration const& configuration, Credentials const& credentials, Diagnostics const& diagnostics,
                   Session& session, PeerFactory make)
    : _diagnostics{ diagnostics }, _session{ session }, _make{ std::move(make) }, _listener{ NewListener(credentials) },
      _stop{ NewStopEvent() }, _port{ Bind(*_listener, configuration.Config()) } {
  _listener->info = this;
  // abi: psPeerAccepted, BOOL is int
  _listener->PeerAccepted = [](freerdp_listener* accepting, freerdp_peer* client) noexcept -> int {
    Expects(accepting != nullptr, "the listener calls back with itself");
    Expects(client != nullptr, "an accepted peer exists");
    auto&      owner    = CallbackOwner<Listener, &freerdp_listener::info>(*accepting);
    auto const accepted = [&] {
      owner.Accept(PeerHandle{ client });
      return true;
    };
    // True transfers ownership even when construction failed and RAII already released the peer.
    return Contained(true, accepted, FailureLog{ owner._diagnostics, "Peer construction" });
  };
  _diagnostics.Log(LogLevel::Info, std::format("Listening on port {}", _port));
  _thread = std::jthread([this](std::stop_token const& quit) {
    std::ignore = Contained([&] { Listen(quit); }, FailureLog{ _diagnostics, "Listener" });
  });
  Ensures(_port != 0, "bound port is available");
}
auto Listener::Port() const noexcept -> std::uint32_t {
  return _port;
}
auto Listener::Accept(PeerHandle accepted) -> void {
  _diagnostics.Log(LogLevel::Info, std::format("Peer accepted: {}.", accepted->hostname));
  _session.Add(_make(std::move(accepted)));
}
auto Listener::Listen(std::stop_token const& quit) -> void {
  std::stop_callback const                   wake(quit, [this] { SetEvent(_stop.get()); });
  std::array<WaitHandle, WaitHandleCapacity> handles { };
  auto const                                 budget  = std::span{ handles }.first(handles.size() - ListenerOwnHandles);
  while (!quit.stop_requested()) {
    auto const count = WaitHandle::Collected<&freerdp_listener::GetEventHandles>(*_listener, budget).size();
    if (!count) break;
    std::ranges::copy(std::array{ WaitHandle{ _stop }, _session.ReapEvent() },
                      std::span{ handles }.subspan(count).begin());
    std::ignore = WaitHandle::Any(std::span{ handles }.first(count + ListenerOwnHandles), Forever);
    if (quit.stop_requested() || !_listener->CheckFileDescriptor(_listener.get())) break;
    _session.Reap();
  }
}
}
