#include <sdl-rdp/session/listener.hpp>

#include <sdl-rdp/auth/tls-rehearsal.hpp>
#include <sdl-rdp/configuration/configuration.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/diagnostics.hpp>
#include <sdl-rdp/diagnostics/failure-log.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/freerdp-facade/rdp-handles.hpp>
#include <sdl-rdp/freerdp-facade/wait-handle.hpp>
#include <sdl-rdp/peer/peer.hpp>
#include <sdl-rdp/session/exceptions.hpp>
#include <sdl-rdp/session/session.hpp>
#include <sdl-rdp/utilities/contained.hpp>
#include <sdl-rdp/utilities/contract.hpp>
#include <sdl-rdp/utilities/exceptions.hpp>
#include <sdl-rdp/utilities/socket.hpp>

#include <algorithm>
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
using sdl_rdp::freerdp_facade::Forever;
using sdl_rdp::freerdp_facade::ManualResetEvent;
using sdl_rdp::freerdp_facade::WaitHandle;
using sdl_rdp::utilities::Contained;
using sdl_rdp::utilities::Ensures;
using sdl_rdp::utilities::Ipv4Endpoint;
using sdl_rdp::utilities::ListeningSocket;
using sdl_rdp::utilities::ParsedIpv4;

namespace {
constexpr int         ListenBacklog      = 8;
constexpr std::size_t WaitHandleCapacity = 32;
constexpr std::size_t ListenerOwnHandles = 2;
// FreeRDP's lazily filled BIO tables are process-wide, so one rehearsal per process fills them for every listener.
auto RehearseTls(Credentials const& credentials) -> void {
  static std::once_flag once;
  std::call_once(once, [&credentials] { TlsRehearsal{ credentials }.Perform(); });
}
auto Endpoint(Setup const& config) -> Ipv4Endpoint {
  auto const bind    = config.bind.value_or("0.0.0.0");
  auto const address = ParsedIpv4(bind);
  if (!address) throw AddressNotIpv4{ bind };
  return { .address = *address, .port = config.port };
}
auto NewStopEvent() -> EventHandle {
  return ManualResetEvent("Listener stop event");
}
}
Listener::Listener(Configuration const& configuration, Credentials const& credentials, Diagnostics const& diagnostics,
                   Session& session, PeerFactory make)
    : LoggedFailures{ diagnostics }, _session{ session }, _make{ std::move(make) },
      _listener{ ListeningSocket(Endpoint(configuration.Config()), ListenBacklog), *this }, _stop{ NewStopEvent() } {
  RehearseTls(credentials);
  Logger().Log(LogLevel::Info, std::format("Listening on port {}", _listener.Port()));
  _thread = std::jthread([this](std::stop_token const& quit) {
    std::ignore = Contained([&] { Listen(quit); }, FailureLog{ Logger(), "Listener" });
  });
  Ensures(_listener.Port() != 0, "bound port is available");
}
auto Listener::Port() const noexcept -> std::uint32_t {
  return _listener.Port();
}
auto Listener::Accepted(Connection accepted) -> void {
  Logger().Log(LogLevel::Info, std::format("Peer accepted: {}.", accepted.Hostname()));
  _session.Add(_make(std::move(accepted)));
}
auto Listener::Listen(std::stop_token const& quit) -> void {
  std::stop_callback const                   wake(quit, [this] { _stop.Set(); });
  std::array<WaitHandle, WaitHandleCapacity> handles { };
  auto const                                 budget  = std::span{ handles }.first(handles.size() - ListenerOwnHandles);
  while (!quit.stop_requested()) {
    auto const count = _listener.EventHandles(budget).size();
    if (!count) break;
    std::ranges::copy(std::array{ WaitHandle{ _stop }, _session.ReapEvent() },
                      std::span{ handles }.subspan(count).begin());
    std::ignore = WaitHandle::Any(std::span{ handles }.first(count + ListenerOwnHandles), Forever);
    if (quit.stop_requested() || !_listener.Pump()) break;
    _session.Reap();
  }
}
}
