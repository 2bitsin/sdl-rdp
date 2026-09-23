#include "_detail/state.hpp"
#include <winpr/ssl.h>
#include <freerdp/channels/channels.h>
#include <winpr/wtsapi.h>
#include <freerdp/settings.h>
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
 : log_route(config), authentication(config), log(config.log), user(config.log_user), codec(config.codec), width(config.width), height(config.height), aspect(config.aspect),
   credentials(EnsureCertificate(config.cert_dir ? std::filesystem::path(config.cert_dir) : DefaultCertificateDirectory())),
   listener(freerdp_listener_new()), stop(CreateEvent(nullptr, TRUE, FALSE, nullptr))
{
  if (!listener || !stop || !reap) throw std::runtime_error("listener allocation failed");
  static std::once_flag wts;
  std::call_once(wts, [] { WTSRegisterWtsApiFunctionTable(FreeRDP_InitWtsApi()); });
  audio_latency = config.audio_latency_ms ? config.audio_latency_ms : 500;
  Picture();
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
  for (auto const& peer : peers) {
    peer->thread.request_stop();
    SetEvent(peer->wake.get());
  }
  for (auto const& peer : peers) if (peer->thread.joinable()) peer->thread.join();
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
void State::Takeover(Peer& peer, sdlrdp_event event)
{
  Expects(event.type == SDLRDP_CONNECTED, "activation carries session facts");
  std::scoped_lock session(session_guard);
  std::scoped_lock lock(peers_guard, frame_guard);
  for (auto const& old : peers) {
    if (old.get() == &peer || !old->active.exchange(false)) continue;
    if (old->drive) old->drive->Disconnect();
    Push({.type = SDLRDP_DISCONNECTED});
    if (old->sound && old->sound->Rate()) Push({.type = SDLRDP_AUDIO, .audio = {0, 0}});
    freerdp_set_error_info(old->client->context->rdp, ERRINFO_DISCONNECTED_BY_OTHER_CONNECTION);
    freerdp_send_error_info(old->client->context->rdp);
    old->client->Close(old->client.get());
    old->thread.request_stop();
    SetEvent(old->wake.get());
  }
  current = &peer;
  peer.active = peer.activated = true;
  peer.activated_at = Peer::Clock::now();
  if (shadow) peer.Post({0, 0, int(frame_width), int(frame_height)});
  if (freerdp_settings_get_bool(peer.client->context->settings, FreeRDP_SupportGraphicsPipeline))
    peer.connection = event;
  else {
    Push(event);
    Push({.type = SDLRDP_SCREEN, .screen = {peer.screen_width, peer.screen_height}});
  }
  frame_changed.notify_all();
  audio_changed.notify_all();
}
void State::Depart(Peer& peer)
{
  Expects(peer.client != nullptr, "departing peer exists");
  std::scoped_lock lock(session_guard);
  if (peer.drive) peer.drive->Disconnect();
  if (peer.activated) {
    peer.LogFrames();
    auto name = freerdp_settings_get_string(peer.client->context->settings, FreeRDP_ClientHostname);
    Log(SDLRDP_LOG_INFO, std::format("Client {} disconnected.", name ? name : peer.client->hostname));
  }
  {
    std::scoped_lock frame(frame_guard);
    if (current == &peer) current = nullptr;
    if (peer.active.exchange(false)) {
      Push({.type = SDLRDP_DISCONNECTED});
      if (peer.sound && peer.sound->Rate()) Push({.type = SDLRDP_AUDIO, .audio = {0, 0}});
    }
  }
  frame_changed.notify_all();
  audio_changed.notify_all();
  Ensures(!peer.active, "departed peer cannot inject input");
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
  if (damage.empty()) return;
  std::scoped_lock producer(producer_guard);
  auto unused = std::ranges::find_if(buffers, [](auto const& buffer) { return buffer.use_count() == 1; });
  if (unused == buffers.end()) { buffers.push_back(std::make_shared<std::vector<BYTE>>()); unused = buffers.end() - 1; }
  auto next = *unused;
  next->resize(std::size_t(w) * h * 4);
  std::shared_ptr<std::vector<BYTE>> previous;
  {
    std::scoped_lock lock(frame_guard);
    previous = shadow;
  }
  bool full = damage.size() == 1 && damage[0].x == 0 && damage[0].y == 0
    && damage[0].w == int(w) && damage[0].h == int(h);
  if (!full) {
    if (previous && previous->size() == next->size()) *next = *previous;
    else std::ranges::fill(*next, 0);
  }
  auto source = std::span(static_cast<BYTE const*>(pixels), std::size_t(pitch) * h);
  for (auto area : damage)
    CopyRows(source.subspan(std::size_t(area.y) * pitch + area.x * 4), pitch,
      std::span(*next).subspan((std::size_t(area.y) * w + area.x) * 4), w * 4, area.h, area.w * 4);
  std::scoped_lock lock(peers_guard, frame_guard);
  Picture(w, h);
  auto resized = frame_width != w || frame_height != h;
  shadow = std::move(next);
  frame_width = width = w; frame_height = height = h;
  ++presented;
  for (auto const& peer : peers) if (peer->active) {
    ++peer->dirty_presents;
    if (resized) { peer->dirty.clear(); peer->Post({0, 0, int(w), int(h)}); }
    else for (auto area : damage) peer->Post(area);
  }
}
}
