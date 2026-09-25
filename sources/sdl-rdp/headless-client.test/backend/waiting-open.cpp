#include <sdl-rdp/headless-client.test/backend/waiting-open.hpp>
#include <oxbox/utilities/number-text.hpp>
#include <sdl-rdp/abi/backend.h>
#include <sdl-rdp/headless-client.test/backend/events.hpp>
#include <sdl-rdp/utilities/narrowed.hpp>
#include <poll.h>
#include <sys/socket.h>

namespace BackendGate {
namespace {
auto OpeningSockets() -> std::array<Backend::Descriptor, 2> {
  std::array<int, 2> sockets { };
  auto               result  = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets.data());
  Expects(result == 0, "opening process has a control socket");
  return { Backend::Descriptor(sockets[0]), Backend::Descriptor(sockets[1]) };
}
auto SendOpeningResult(int socket, int value) -> void {
  auto sent = send(socket, &value, sizeof(value), MSG_NOSIGNAL);
  Expects(sent == sizeof(value), "opening process publishes its result");
}
auto PublishListeningPort(void* user, sdlrdp_log_level level, char const* text) -> void {
  Expects(user != nullptr, "control socket exists");
  Expects(text != nullptr, "log message exists");
  std::string_view const     line(text);
  constexpr std::string_view prefix = "Listening on port ";
  if (level != SDLRDP_LOG_INFO || !line.starts_with(prefix)) return;
  auto const port = Required(oxbox::utilities::ParseNumberAfter<int>(line, prefix), "listener logged a numeric port");
  SendOpeningResult(*static_cast<int*>(user), port);
}
auto OpenedWithClient(sdlrdp_config const& config) -> bool {
  Headless::BackendInstance backend;
  if (backend.TryOpen(config) != 0) return false;
  if (sdlrdp_wait(backend.Handle(), 0) != 1) return false;
  auto const events = backend.Poll();
  return !events.empty() && events.front().type == SDLRDP_CONNECTED;
}
auto RunOpeningProcess(sdlrdp_config config, int socket) -> int {
  config.log      = PublishListeningPort;
  config.log_user = &socket;
  auto opened = OpenedWithClient(config);
  SendOpeningResult(socket, opened ? 0 : -1);
  return opened ? 0 : 1;
}
}
WaitingOpen::WaitingOpen(sdlrdp_config const& config)
    : sockets(OpeningSockets()), process([&] { return RunOpeningProcess(config, sockets[1].Get()); }) { }
WaitingOpen::~WaitingOpen() {
  // The ABI returns no handle until a blocking open completes; ending the process closes its listener.
  process.Kill();
}
auto WaitingOpen::Receive(std::chrono::milliseconds timeout) const -> std::optional<int> {
  pollfd ready  { .fd = sockets[0].Get(), .events = POLLIN, .revents = 0 };
  auto   polled = poll(&ready, 1, Backend::Narrowed<int>(timeout.count()));
  if (polled != 1) return std::nullopt;
  int  value    = -1;
  auto received = recv(ready.fd, &value, sizeof(value), MSG_DONTWAIT);
  if (received != sizeof(value)) return std::nullopt;
  return value;
}
}
