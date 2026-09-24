#include "_detail/test-backend-events.hpp"
#include "_detail/waiting-open.hpp"
#include <oxbox/utilities/number-text.hpp>
#include <poll.h>
#include <sys/socket.h>

namespace BackendGate {
namespace {
std::array<Backend::Descriptor, 2> OpeningSockets() {
  std::array<int, 2> sockets { };
  auto               result  = socketpair(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0, sockets.data());
  Expects(result == 0, "opening process has a control socket");
  return { Backend::Descriptor(sockets[0]), Backend::Descriptor(sockets[1]) };
}
void SendOpeningResult(int socket, int value) {
  auto sent = send(socket, &value, sizeof(value), MSG_NOSIGNAL);
  Expects(sent == sizeof(value), "opening process publishes its result");
}
void PublishListeningPort(void* user, sdlrdp_log_level level, char const* text) {
  Expects(user != nullptr, "control socket exists");
  Expects(text != nullptr, "log message exists");
  std::string_view const     line(text);
  constexpr std::string_view prefix = "Listening on port ";
  if (level != SDLRDP_LOG_INFO || !line.starts_with(prefix)) return;
  auto const port = Required(oxbox::utilities::ParseNumberAfter<int>(line, prefix), "listener logged a numeric port");
  SendOpeningResult(*static_cast<int*>(user), port);
}
bool OpenedWithClient(sdlrdp_config const& config) {
  sdlrdp_handle* handle = nullptr;
  auto           opened = sdlrdp_open(&config, &handle);
  std::unique_ptr<sdlrdp_handle, decltype(&sdlrdp_close)> const backend(handle, sdlrdp_close);
  sdlrdp_event event{ };
  return opened == 0 && sdlrdp_wait(handle, 0) == 1 && sdlrdp_poll(handle, &event, 1) == 1 &&
         event.type == SDLRDP_CONNECTED;
}
int RunOpeningProcess(sdlrdp_config config, int socket) {
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
std::optional<int> WaitingOpen::Receive(std::chrono::milliseconds timeout) const {
  pollfd ready  { .fd = sockets[0].Get(), .events = POLLIN, .revents = 0 };
  auto   polled = poll(&ready, 1, int(timeout.count()));
  if (polled != 1) return std::nullopt;
  int  value    = -1;
  auto received = recv(ready.fd, &value, sizeof(value), MSG_DONTWAIT);
  if (received != sizeof(value)) return std::nullopt;
  return value;
}
}
