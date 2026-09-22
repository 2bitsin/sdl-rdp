#pragma once
#include "sdl-rdp-backend.h"
#include <freerdp/freerdp.h>
#include "rdp-handles.hpp"
#include "contract.hpp"
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>
#include <span>
#include <optional>
#include <winpr/synch.h>
#include <format>

namespace Backend {
using utilities::Expects;
using utilities::Ensures;
struct Credentials { std::filesystem::path certificate, key; };
Credentials EnsureCertificate(std::filesystem::path const& directory);
void Merge(std::optional<sdlrdp_rect>& region, sdlrdp_rect area);
class Peer;
struct State {
  explicit State(sdlrdp_config const& config);
  ~State();
  void Log(sdlrdp_log_level level, std::string const& text) const;
  void (*log)(void*, sdlrdp_log_level, const char*);
  void* user;
  void Listen(std::stop_token quit);
  void Push(sdlrdp_event event);
  unsigned Poll(sdlrdp_event* out, unsigned max);
  int Wait(int timeout);
  void Wakeup();
  void Present(void const* pixels, int pitch, unsigned w, unsigned h,
               std::span<sdlrdp_rect const> damage);
  static BOOL Accepted(freerdp_listener* listener, freerdp_peer* client);
  unsigned width, height, port = 0;
  Credentials credentials;
  ListenerHandle listener;
  EventHandle stop;
  EventHandle reap{CreateEvent(nullptr, TRUE, FALSE, nullptr)};
  // frame_guard protects the shadow and every peer's dirty region.
  std::mutex peers_guard, events_guard, frame_guard;
  std::vector<BYTE> shadow;
  unsigned frame_width = 0, frame_height = 0;
  std::condition_variable changed;
  std::deque<sdlrdp_event> events;
  unsigned long wake_generation = 0;
  std::vector<std::unique_ptr<Peer>> peers;
  std::jthread thread;
};
class Peer {
public:
  Peer(PeerHandle client, State& owner);
  ~Peer();
  void Start();
  void Post(sdlrdp_rect area);
  bool Configure();
  void Serve(std::stop_token quit);
  bool Drain();
  static Peer& Held(freerdp_peer* client);
  static BOOL Activate(freerdp_peer* client);
  static BOOL Keyboard(rdpInput* input, UINT16 flags, UINT8 code);
  static BOOL Mouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);
  static BOOL ExtendedMouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);
  PeerHandle client;
  State& owner;
  EventHandle wake;
  std::optional<sdlrdp_rect> dirty;
  std::atomic_bool active = false, finished = false;
  std::jthread thread;
};
bool SendFrame(rdpContext* context, State& state, sdlrdp_rect area);
}
struct sdlrdp_handle { std::unique_ptr<Backend::State> state; };
