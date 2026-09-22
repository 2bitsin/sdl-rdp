#pragma once
#include "sdl-rdp-backend.h"
#include <freerdp/freerdp.h>
#include "rdp-handles.hpp"
#include "contract.hpp"
#include "rect.hpp"
#include "logging.hpp"
#include "encoder.hpp"
#include "audio.hpp"
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
#include <freerdp/update.h>
#include <freerdp/server/disp.h>
#include <freerdp/server/rdpsnd.h>

namespace Backend {
using utilities::Expects;
using utilities::Ensures;
struct Credentials { std::filesystem::path certificate, key; };
std::filesystem::path DefaultCertificateDirectory();
Credentials EnsureCertificate(std::filesystem::path const& directory);
struct Pointer {
  unsigned width = 0, height = 0, hot_x = 0, hot_y = 0;
  std::vector<BYTE> pixels, mask;
};
class Peer;
struct State {
  explicit State(sdlrdp_config const& config);
  ~State();
  void Log(sdlrdp_log_level level, std::string const& text) const;
  LogRoute log_route;
  void (*log)(void*, sdlrdp_log_level, const char*);
  void* user;
  void Listen(std::stop_token quit);
  void Push(sdlrdp_event event);
  unsigned Poll(sdlrdp_event* out, unsigned max);
  int Wait(int timeout);
  void Wakeup();
  void Takeover(Peer& peer, sdlrdp_event event);
  void Depart(Peer& peer);
  void Present(void const* pixels, int pitch, unsigned w, unsigned h,
               std::span<sdlrdp_rect const> damage);
  void SetPointer(unsigned w, unsigned h, unsigned x, unsigned y, void const* pixels);
  Pointer pointer;
  uint64_t pointer_generation = 0;
  void Resize(unsigned w, unsigned h);
  void SetAspect(sdlrdp_aspect value);
  int WaitFrame(int timeout);
  void OpenAudio();
  unsigned AudioRate();
  void EnsurePicture();
  int WriteAudio(void const* frames, unsigned count);
  int WaitAudio(int timeout);
  void CloseAudio();
  unsigned audio_latency = 100;
  bool audio_open = false;
  std::condition_variable_any audio_changed;
  sdlrdp_rect Picture(unsigned w = 0, unsigned h = 0) const;
  static BOOL Accepted(freerdp_listener* listener, freerdp_peer* client);
  std::atomic<sdlrdp_codec> codec;
  unsigned width, height, port = 0;
  sdlrdp_aspect aspect{};
  uint64_t presented = 0;
  Peer* current = nullptr;
  std::condition_variable frame_changed;
  Credentials credentials;
  ListenerHandle listener;
  EventHandle stop;
  EventHandle reap{CreateEvent(nullptr, TRUE, FALSE, nullptr)};
  // frame_guard protects the shadow and every peer's dirty region.
  std::recursive_mutex session_guard;
  std::mutex peers_guard, events_guard, frame_guard, producer_guard;
  std::shared_ptr<std::vector<BYTE>> shadow;
  std::vector<std::shared_ptr<std::vector<BYTE>>> buffers;
  unsigned frame_width = 0, frame_height = 0;
  std::condition_variable changed;
  std::deque<sdlrdp_event> events;
  unsigned long wake_generation = 0;
  std::vector<std::unique_ptr<Peer>> peers;
  std::jthread thread;
};
class Peer {
public:
  using Clock = std::chrono::steady_clock;
  Peer(PeerHandle client, State& owner);
  ~Peer();
  void Start();
  void Post(sdlrdp_rect area);
  bool Configure();
  void Serve(std::stop_token quit);
  DWORD EventHandles(std::span<HANDLE> handles);
  Encoder encoder;
  bool Drain();
  bool SendPointer();
  uint64_t pointer_generation = 0;
  void TransportEnded();
  static Peer& Held(freerdp_peer* client);
  static BOOL Capabilities(freerdp_peer* client);
  static BOOL Acknowledge(rdpContext*, UINT32);
  static BOOL Suppress(rdpContext*, BYTE, RECTANGLE_16 const*);
  bool BeginFrame();
  bool Marker(UINT16 action);
  bool Pacing();
  DWORD Timeout();
  bool Channels();
  bool SoundChannel();
  bool sound_attempted = false;
  std::unique_ptr<AudioChannel> sound;
  static UINT Layout(DispServerContext*, DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const*);
  static BOOL Activate(freerdp_peer* client);
  static BOOL Keyboard(rdpInput* input, UINT16 flags, UINT8 code);
  static BOOL Mouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);
  static BOOL ExtendedMouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);
  PeerHandle client;
  State& owner;
  EventHandle wake;
  Region dirty, sending;
  std::shared_ptr<std::vector<BYTE> const> snapshot;
  unsigned snapshot_width = 0, snapshot_height = 0;
  struct Column { unsigned first, second; float weight; };
  std::vector<Column> scale_columns;
  int scale_x = -1, scale_width = 0;
  unsigned scale_source = 0;
  unsigned rect_index = 0, row = 0;
  uint64_t sequence = 0, acknowledged = 0;
  UINT32 frame_id = 0;
  struct Pending { UINT32 id; uint64_t sequence; };
  std::deque<Pending> pending;

  Clock::time_point first_sent{}, last_ack{};
  double ack_interval = 0;
  unsigned refresh = 0, screen_width = 0, screen_height = 0;
  bool ack_enabled = false, ack_seen = false, suppressed = false;
  HANDLE channels = nullptr;
  std::unique_ptr<DispServerContext, Releases<disp_server_context_free>> disp;
  bool disp_open = false, resizing = false, frame_started = false;
  sdlrdp_rect desktop{};
  bool activated = false;
  std::atomic_bool active = false, finished = false;
  std::jthread thread;
};
bool SendFrame(Peer& peer);
}
struct sdlrdp_handle { std::unique_ptr<Backend::State> state; };
