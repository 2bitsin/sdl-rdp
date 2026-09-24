#include "_detail/state.hpp"

#include "_detail/copy-rows.hpp"

#include <algorithm>
#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <cstddef>
#include <cstring>
#include <freerdp/channels/channels.h>
#include <freerdp/settings.h>
#include <ranges>
#include <stdexcept>
#include <system_error>
#include <unistd.h>
#include <utility>
#include <winpr/ssl.h>
#include <winpr/synch.h>
#include <winpr/wtsapi.h>

namespace Backend {
namespace {
struct Socket {
public:
  Socket(Socket const&) = delete;
  Socket(Socket&&)      = delete;
  Socket()              = default;
  ~Socket() {
    if (descriptor >= 0) ::close(descriptor);
  }
  Socket& operator = (Socket const&) = delete;
  Socket& operator = (Socket&&)      = delete;
  int     Get() const { return descriptor; }
  void    Release() { descriptor = -1; }

private:
  int descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
};
void PrepareListenerSocket(int descriptor) {
  if (descriptor < 0)
    throw std::runtime_error(std::format("Socket creation failed: {}.", std::system_category().message(errno)));
  int reuse = 1;
  if (setsockopt(descriptor, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
    throw std::runtime_error(std::format("Socket options failed: {}.", std::system_category().message(errno)));
}
void LogDeparture(Peer& peer) {
  if (peer.activated) {
    peer.LogFrames();
    if (peer.sound) peer.sound->LogAudio();
    auto const* name = freerdp_settings_get_string(peer.client->context->settings, FreeRDP_ClientHostname);
    peer.owner.Log(SDLRDP_LOG_INFO, std::format("Client {} disconnected.", name ? name : peer.client->hostname));
    peer.owner.trace.Line("disconnect");
  }
}
unsigned Bind(freerdp_listener* listener, sdlrdp_config const& config) {
  Expects(listener != nullptr, "listener exists");
  Socket      socket;
  sockaddr_in address{ };
  address.sin_family = AF_INET;
  address.sin_port   = htons(config.port);
  PrepareListenerSocket(socket.Get());
  if (inet_pton(AF_INET, config.bind ? config.bind : "0.0.0.0", &address.sin_addr) != 1)
    throw std::runtime_error("Listener address is not numeric IPv4.");
  if (::bind(socket.Get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
      ::listen(socket.Get(), 8) != 0)
    throw std::runtime_error(std::format("Listener bind/listen failed: {}.", std::system_category().message(errno)));
  socklen_t size = sizeof(address);
  if (getsockname(socket.Get(), reinterpret_cast<sockaddr*>(&address), &size) != 0)
    throw std::runtime_error(std::format("Listener socket name failed: {}.", std::system_category().message(errno)));
  if (!listener->OpenFromSocket(listener, socket.Get()))
    throw std::runtime_error("FreeRDP listener socket adoption failed.");
  socket.Release();
  return ntohs(address.sin_port);
}
void ComposeRow(std::span<BYTE const> source, std::span<BYTE const> former, std::span<BYTE> target, auto damage) {
  for (int x = 0; std::cmp_less(x, target.size() / 4);) {
    auto covered{ std::ranges::find_if(damage, [x](auto rect) { return rect.x <= x && x < rect.x + rect.w; }) };
    auto ahead  { damage | std::views::filter([x](auto rect) { return rect.x > x; })                          };
    auto nearest{ std::ranges::min_element(ahead, { }, &sdlrdp_rect::x)                                       };
    auto end    { covered != damage.end() ? covered->x + covered->w
                  : nearest != ahead.end() ? nearest->x
                                           : int(target.size() / 4) };
    auto output { target.subspan(x * 4, (end - x) * 4)                                                        };
    auto input  { covered != damage.end() ? source : former                                                   };
    if (input.empty())
      std::ranges::fill(output, 0);
    else
      std::ranges::copy(input.subspan(x * 4, output.size()), output.begin());
    x = end;
  }
}
void ComposePicture(std::span<BYTE const> source, unsigned pitch, std::span<BYTE const> former, std::span<BYTE> target,
                    unsigned width, unsigned height, std::span<sdlrdp_rect const> damage) {
  auto stride = Avc::Aligned(width) * 4;
  std::ranges::for_each(std::views::iota(0u, height), [&](unsigned row) {
    auto active = damage | std::views::filter(
                               [row](auto rect) { return row >= unsigned(rect.y) && row < unsigned(rect.y + rect.h); });
    ComposeRow(source.subspan(std::size_t(row) * pitch, std::size_t(width) * 4),
               former.empty() ? former : former.subspan(std::size_t(row) * stride, std::size_t(width) * 4),
               target.subspan(std::size_t(row) * stride, std::size_t(width) * 4), active);
  });
}
} // namespace
void State::Publish(std::shared_ptr<std::vector<BYTE>> next, unsigned w, unsigned h,
                    std::span<sdlrdp_rect const> damage) {
  std::scoped_lock const lock(peers_guard, frame_guard);
  Picture(w, h);
  auto resized = frame_width != w || frame_height != h;
  shadow       = std::move(next);
  frame_width  = width = w;
  frame_height = height = h;
  ++presented;
  for (auto const& peer : peers)
    if (peer->active) {
      ++peer->dirty_presents;
      if (resized) {
        peer->dirty.clear();
        peer->Post({ 0, 0, int(w), int(h) });
      } else
        for (auto area : damage)
          peer->Post(area);
    }
}
void Trace::Emit(std::string const& text) const {
  owner.Log(SDLRDP_LOG_INFO, text);
}
State::State(sdlrdp_config const& config, bool tracing)
    : trace{ *this, tracing }, log_route(config), authentication(config), log(config.log), user(config.log_user),
      avc_bitrate_kbps(config.avc_bitrate_kbps), codec(config.codec), width(config.width), height(config.height),
      aspect(config.aspect), credentials(EnsureCertificate(config.cert_dir ? std::filesystem::path(config.cert_dir)
                                                                           : DefaultCertificateDirectory())),
      listener(freerdp_listener_new()), stop(CreateEvent(nullptr, TRUE, FALSE, nullptr)) {

  if (!listener || !stop || !reap) throw std::runtime_error("listener allocation failed");
  static std::once_flag wts;
  std::call_once(wts, [] { WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi()); });
  audio_latency = config.audio_latency_ms ? config.audio_latency_ms : 500;
  Picture();
  winpr_InitializeSSL(WINPR_SSL_INIT_DEFAULT);
  listener->info         = this;
  listener->PeerAccepted = Accepted;
  port                   = Bind(listener.get(), config);
  thread                 = std::jthread([this](std::stop_token const& quit) { Listen(quit); });
  Ensures(port != 0, "bound port is available");
}
void State::Log(sdlrdp_log_level level, std::string const& text) const {
  if (log) log(user, level, text.c_str());
}
State::~State() {
  thread.request_stop();
  SetEvent(stop.get());
  if (thread.joinable()) thread.join();
  for (auto const& peer : peers) {
    peer->thread.request_stop();
    peer->wake.Transition(WakeEvent::Phase::Pending);
  }
  for (auto const& peer : peers)
    if (peer->thread.joinable()) peer->thread.join();
  peers.clear();
}
BOOL State::Accepted(freerdp_listener* listener, freerdp_peer* client) {
  Expects(listener != nullptr, "listener exists");
  Expects(client != nullptr, "client transport exists");
  auto&      self     = *static_cast<State*>(listener->info);
  PeerHandle accepted(client);
  try {
    self.Log(SDLRDP_LOG_INFO, std::format("Peer accepted: {}.", client->hostname));
    auto                   peer = std::make_unique<Peer>(std::move(accepted), self);
    std::scoped_lock const lock(self.peers_guard);
    self.peers.push_back(std::move(peer));
    self.peers.back()->Start();
  } catch (std::exception const& error) {
    self.Log(SDLRDP_LOG_ERROR, std::format("Peer construction failed: {}.", error.what()));
  }
  // TRUE transfers ownership even when construction failed and RAII already released the peer.
  return TRUE;
}
void State::Listen(std::stop_token const& quit) {
  Expects(listener != nullptr, "listener exists");
  Expects(stop != nullptr, "listener owns its stop event");
  std::array<HANDLE, 32> handles{ };
  while (!quit.stop_requested()) {
    auto count = listener->GetEventHandles(listener.get(), handles.data(), 30);
    if (!count) break;
    handles[count++] = stop.get();
    handles[count++] = reap.get();
    if (WaitForMultipleObjects(count, handles.data(), FALSE, INFINITE) == WAIT_FAILED || quit.stop_requested() ||
        !listener->CheckFileDescriptor(listener.get()))
      break;
    ResetEvent(reap.get());
    std::scoped_lock const lock(peers_guard);
    std::erase_if(peers, [](auto const& peer) { return peer->finished.load(); });
  }
}
void State::ReplacePeer(Peer& old) {
  if (old.drive) old.drive->Disconnect();
  Push({ .type = SDLRDP_DISCONNECTED });
  if (old.sound && old.sound->Rate()) Push({ .type = SDLRDP_AUDIO, .audio = { .freq = 0, .connected = 0 } });
  freerdp_set_error_info(old.client->context->rdp, ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION);
  freerdp_send_error_info(old.client->context->rdp);
  old.client->Close(old.client.get());
  old.thread.request_stop();
  old.wake.Transition(WakeEvent::Phase::Pending);
}
std::shared_ptr<std::vector<BYTE>> State::AcquireBuffer() {
  auto unused = std::ranges::find_if(buffers, [](auto const& buffer) { return buffer.use_count() == 1; });
  if (unused == buffers.end()) {
    buffers.push_back(std::make_shared<std::vector<BYTE>>());
    unused = buffers.end() - 1;
  }
  return *unused;
}
void State::Takeover(Peer& peer, sdlrdp_event event) {
  Expects(event.type == SDLRDP_CONNECTED, "activation carries session facts");
  std::scoped_lock const session(session_guard);
  std::scoped_lock const lock(peers_guard, frame_guard);
  std::ranges::for_each(peers, [&](auto const& old) {
    if (old.get() != &peer && old->active.exchange(false)) ReplacePeer(*old);
  });
  current           = &peer;
  peer.active       = peer.activated = true;
  peer.activated_at = Peer::Clock::now();
  if (shadow) peer.Post({ 0, 0, int(frame_width), int(frame_height) });
  if (freerdp_settings_get_bool(peer.client->context->settings, FreeRDP_SupportGraphicsPipeline))
    peer.connection = event;
  else {
    Push(event);
    Push({ .type = SDLRDP_SCREEN, .screen = { .width = peer.screen_width, .height = peer.screen_height } });
  }
  frame_changed.notify_all();
  audio_changed.notify_all();
}
void State::Depart(Peer& peer) {
  Expects(peer.client != nullptr, "departing peer exists");
  std::scoped_lock const lock(session_guard);
  if (peer.drive) peer.drive->Disconnect();
  LogDeparture(peer);
  {
    std::scoped_lock const frame(frame_guard);
    if (current == &peer) current = nullptr;
    if (peer.active.exchange(false)) {
      Push({ .type = SDLRDP_DISCONNECTED });
      if (peer.sound && peer.sound->Rate()) Push({ .type = SDLRDP_AUDIO, .audio = { .freq = 0, .connected = 0 } });
    }
  }
  frame_changed.notify_all();
  audio_changed.notify_all();
  Ensures(!peer.active, "departed peer cannot inject input");
}
void State::Push(sdlrdp_event event) {
  {
    std::scoped_lock const lock(events_guard);
    events.push_back(event);
  }
  changed.notify_all();
}
unsigned State::Poll(sdlrdp_event* out, unsigned max) {
  if (max) Expects(out != nullptr, "output covers requested events");
  std::scoped_lock const lock(events_guard);
  auto                   count = std::min<std::size_t>(max, events.size());
  std::copy_n(events.begin(), count, out);
  events.erase(events.begin(), events.begin() + std::ptrdiff_t(count));
  return count;
}
int State::Wait(int timeout) {
  std::unique_lock lock(events_guard);
  auto             generation = wake_generation;
  auto             ready      = [&] { return !events.empty() || generation != wake_generation; };
  if (timeout < 0)
    changed.wait(lock, ready);
  else
    changed.wait_for(lock, std::chrono::milliseconds(timeout), ready);
  return !events.empty();
}
void State::Wakeup() {
  {
    std::scoped_lock const lock(events_guard);
    ++wake_generation;
  }
  changed.notify_all();
}
void State::Present(void const* pixels, int pitch, unsigned w, unsigned h, std::span<sdlrdp_rect const> damage) {
  Expects(pixels != nullptr, "source framebuffer exists");
  Expects(std::cmp_greater_equal(pitch, w * 4), "source pitch covers framebuffer rows");
  if (damage.empty()) return;
  std::scoped_lock const producer(producer_guard);
  auto                   next     = AcquireBuffer();
  next->resize(std::size_t(Avc::Aligned(w)) * Avc::Aligned(h) * 4);
  std::shared_ptr<std::vector<BYTE>> previous;
  {
    std::scoped_lock const lock(frame_guard);
    if (frame_width == w && frame_height == h) previous = shadow;
  }
  ComposePicture({ static_cast<BYTE const*>(pixels), std::size_t(pitch) * h }, pitch,
                 previous ? std::span<BYTE const>(*previous) : std::span<BYTE const>{ }, *next, w, h, damage);
  Avc::ReplicateEdges(*next, w, h);
  Publish(std::move(next), w, h, damage);
}
} // namespace Backend
