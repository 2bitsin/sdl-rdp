#include <sdl-rdp/headless-client.test/backend/waiting-open.hpp>
#include <oxbox/utilities/number-text.hpp>
#include <sdl-rdp/configuration/setup.hpp>
#include <sdl-rdp/diagnostics/log-level.hpp>
#include <sdl-rdp/diagnostics/log-sink.hpp>
#include <sdl-rdp/headless-client.test/backend/instance.hpp>
#include <sdl-rdp/link/event.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <chrono>
#include <poll.h>
#include <string_view>
#include <sys/socket.h>
#include <variant>

namespace sdl_rdp::headless_client_test::backend::detail::waiting_open {
using sdl_rdp::configuration::Setup;
using sdl_rdp::diagnostics::LogLevel;
using sdl_rdp::diagnostics::LogSink;
using sdl_rdp::link::Connected;
using sdl_rdp::utilities::Expects;
using sdl_rdp::utilities::Narrowed;
using sdl_rdp::utilities::Required;

namespace {
auto OpeningSockets() -> std::array<Descriptor, 2> {
  std::array<int, 2> sockets { };
  auto               result  = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets.data());
  Expects(result == 0, "opening process has a control socket");
  return { Descriptor(sockets[0]), Descriptor(sockets[1]) };
}
auto SendOpeningResult(int socket, int value) -> void {
  auto sent = send(socket, &value, sizeof(value), MSG_NOSIGNAL);
  Expects(sent == sizeof(value), "opening process publishes its result");
}
// Publishes the listener's port over the control socket as soon as the backend logs it.
class PortPublisher final : public LogSink {
public:
  explicit PortPublisher(int socket) : _socket{ socket } { }
  auto     Log(LogLevel level, std::string_view line) -> void override {
    constexpr std::string_view prefix = "Listening on port ";
    if (level != LogLevel::Info || !line.starts_with(prefix)) return;
    auto const port = Required(oxbox::utilities::ParseNumberAfter<int>(line, prefix), "listener logged a numeric port");
    SendOpeningResult(_socket, port);
  }

private:
  int _socket;
};
auto OpenedWithClient(Setup const& config, LogSink& log) -> bool {
  BackendInstance backend;
  if (!backend.TryOpen(config, log)) return false;
  if (!backend.Wait(std::chrono::milliseconds{ 0 })) return false;
  auto const events = backend.Poll();
  return !events.empty() && std::holds_alternative<Connected>(events.front());
}
auto RunOpeningProcess(Setup const& config, int socket) -> int {
  PortPublisher publisher { socket };
  auto          opened    = OpenedWithClient(config, publisher);
  SendOpeningResult(socket, opened ? 0 : -1);
  return opened ? 0 : 1;
}
}
WaitingOpen::WaitingOpen(Setup const& config)
    : sockets(OpeningSockets()), process([&] { return RunOpeningProcess(config, sockets[1].Get()); }) { }
WaitingOpen::~WaitingOpen() {
  // A waiting open returns only with a client; ending the process closes its listener.
  process.Kill();
}
auto WaitingOpen::Receive(std::chrono::milliseconds timeout) const -> std::optional<int> {
  pollfd ready  { .fd = sockets[0].Get(), .events = POLLIN, .revents = 0 };
  auto   polled = poll(&ready, 1, Narrowed<int>(timeout.count()));
  if (polled != 1) return std::nullopt;
  int  value    = -1;
  auto received = recv(ready.fd, &value, sizeof(value), MSG_DONTWAIT);
  if (received != sizeof(value)) return std::nullopt;
  return value;
}
}
