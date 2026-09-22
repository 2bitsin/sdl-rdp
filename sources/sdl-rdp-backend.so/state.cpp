#include "_detail/state.hpp"
#include <winpr/ssl.h>
#include <winpr/synch.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <algorithm>
#include <array>
#include <utility>
#include <stdexcept>
#include <cstring>
#include <cerrno>
#include "_detail/copy-rows.hpp"

namespace Backend {
namespace {
struct Socket {
  int descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
  ~Socket() { if (descriptor >= 0) ::close(descriptor); }
};
unsigned Bind(freerdp_listener* listener, sdlrdp_config const& config)
{
  Expects(listener != nullptr, "listener exists");
  Socket socket;
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(config.port);
  if (socket.descriptor < 0) throw std::runtime_error(std::format("Socket creation failed: {}.", std::strerror(errno)));
  int reuse = 1;
  if (setsockopt(socket.descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
    throw std::runtime_error(std::format("Socket options failed: {}.", std::strerror(errno)));
  if (inet_pton(AF_INET, config.bind ? config.bind : "0.0.0.0", &address.sin_addr) != 1)
    throw std::runtime_error("Listener address is not numeric IPv4.");
  if (::bind(socket.descriptor, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0
      || ::listen(socket.descriptor, 8) != 0)
    throw std::runtime_error(std::format("Listener bind/listen failed: {}.", std::strerror(errno)));
  socklen_t size = sizeof(address);
  if (getsockname(socket.descriptor, reinterpret_cast<sockaddr*>(&address), &size) != 0)
    throw std::runtime_error(std::format("Listener socket name failed: {}.", std::strerror(errno)));
  if (!listener->OpenFromSocket(listener, socket.descriptor))
    throw std::runtime_error("FreeRDP listener socket adoption failed.");
  socket.descriptor = -1;
  return ntohs(address.sin_port);
}
}
State::State(sdlrdp_config const& config)
 : log(config.log), user(config.user), width(config.width), height(config.height),
   credentials(EnsureCertificate(config.cert_dir ? config.cert_dir : "_rdp")),
   listener(freerdp_listener_new()), stop(CreateEvent(nullptr, TRUE, FALSE, nullptr))
{
  if (!listener || !stop || !reap) throw std::runtime_error("listener allocation failed");
  winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT);
  listener->info = this;
  listener->PeerAccepted = Accepted;
  port = Bind(listener.get(), config);
  thread = std::jthread([this](std::stop_token quit) { Listen(quit); });
  Ensures(port != 0, "bound port is available");
}
void State::Log(sdlrdp_log_level level, std::string const& text) const
{
  if (log) log(user, level, text.c_str());
}
State::~State()
{
  thread.request_stop();
  SetEvent(stop.get());
  if (thread.joinable()) thread.join();
  peers.clear();
}
BOOL State::Accepted(freerdp_listener* listener, freerdp_peer* client)
{
  Expects(listener && client, "listener delivered a peer");
  auto& self = *static_cast<State*>(listener->info);
  PeerHandle accepted(client);
  try {
    self.Log(SDLRDP_LOG_INFO, std::format("Peer accepted: {}.", client->hostname));
    auto peer = std::make_unique<Peer>(std::move(accepted), self);
    std::scoped_lock lock(self.peers_guard);
    self.peers.push_back(std::move(peer));
    self.peers.back()->Start();
  } catch (std::exception const& error) {
    self.Log(SDLRDP_LOG_ERROR, std::format("Peer construction failed: {}.", error.what()));
  }
  // TRUE transfers ownership even when construction failed and RAII already released the peer.
  return TRUE;
}
void State::Listen(std::stop_token quit)
{
  Expects(listener && stop, "listener owns socket and stop event");
  std::array<HANDLE, 32> handles{};
  while (!quit.stop_requested()) {
    auto count = listener->GetEventHandles(listener.get(), handles.data(), 30);
    if (!count) break;
    handles[count++] = stop.get();
    handles[count++] = reap.get();
    if (WaitForMultipleObjects(count, handles.data(), FALSE, INFINITE) == WAIT_FAILED
        || quit.stop_requested() || !listener->CheckFileDescriptor(listener.get())) break;
    ResetEvent(reap.get());
    std::scoped_lock lock(peers_guard);
    std::erase_if(peers, [](auto const& peer) { return peer->finished.load(); });
  }
}
void State::Push(sdlrdp_event event)
{
  { std::scoped_lock lock(events_guard); events.push_back(event); }
  changed.notify_all();
}
unsigned State::Poll(sdlrdp_event* out, unsigned max)
{
  Expects(out || max == 0, "output covers requested events");
  std::scoped_lock lock(events_guard);
  auto count = std::min<std::size_t>(max, events.size());
  std::copy_n(events.begin(), count, out);
  events.erase(events.begin(), events.begin() + count);
  return count;
}
int State::Wait(int timeout)
{
  std::unique_lock lock(events_guard);
  auto generation = wake_generation;
  auto ready = [&] { return !events.empty() || generation != wake_generation; };
  if (timeout < 0) changed.wait(lock, ready);
  else changed.wait_for(lock, std::chrono::milliseconds(timeout), ready);
  return !events.empty();
}
void State::Wakeup()
{
  { std::scoped_lock lock(events_guard); ++wake_generation; }
  changed.notify_all();
}
void State::Present(void const* pixels, int pitch, unsigned w, unsigned h,
                    std::span<sdlrdp_rect const> damage)
{
  Expects(pixels && pitch >= int(w * 4), "source covers framebuffer rows");
  std::scoped_lock lock(peers_guard, frame_guard);
  std::optional<sdlrdp_rect> region;
  if (frame_width != w || frame_height != h) {
    shadow = std::vector<BYTE>(std::size_t(w) * h * 4);
    frame_width = w; frame_height = h;
    region = sdlrdp_rect{0, 0, int(w), int(h)};
    for (auto const& peer : peers) peer->dirty.reset();
  }
  for (auto area : damage) {
    auto source = std::span(static_cast<BYTE const*>(pixels), std::size_t(pitch) * h);
    CopyRows(source.subspan(std::size_t(area.y) * pitch + area.x * 4), pitch,
      std::span(shadow).subspan((std::size_t(area.y) * w + area.x) * 4), w * 4, area.h, area.w * 4);
    Merge(region, area);
  }
  if (region) for (auto const& peer : peers) if (peer->active) peer->Post(*region);
}
}
