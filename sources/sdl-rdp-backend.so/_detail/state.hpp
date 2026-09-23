#pragma once
#include "sdl-rdp-backend.h"
#include <freerdp/freerdp.h>
#include "rdp-handles.hpp"
#include "contract.hpp"
#include "rect.hpp"
#include "logging.hpp"
#include "trace.hpp"
#include "encoder.hpp"
#include "legacy-frame.hpp"
#include "clipboard.hpp"
#include "input.hpp"
#include "audio.hpp"
#include "gfx.hpp"
#include "auth.hpp"
#include "drive.hpp"
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
  explicit State(sdlrdp_config const& config, bool tracing = false);
  ~State();
  void Log(sdlrdp_log_level level, std::string const& text) const;
  Trace trace;
  LogRoute log_route;
  Authentication authentication;
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
  std::atomic_uint next_drive{1};
  Clipboard clipboard;
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
  unsigned avc_bitrate_kbps = 0;
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
  void AuthenticationEnded();
  AuthenticationState authentication;
  void Serve(std::stop_token quit);
  DWORD EventHandles(std::span<HANDLE> handles);
  Encoder encoder;
  bool Drain();
  bool TransportStep(std::stop_token quit);
  bool EncodeAndSend(std::stop_token quit);
  bool SendPointer();
  uint64_t pointer_generation = 0;
  void TransportEnded();
  static Peer& Held(freerdp_peer* client);
  static BOOL Capabilities(freerdp_peer* client);
  static BOOL Acknowledge(rdpContext*, UINT32);
  void AcceptAcknowledgement(UINT32);
  static BOOL Suppress(rdpContext*, BYTE, RECTANGLE_16 const*);
  bool BeginFrame();
  bool Marker(UINT16 action);
  void FrameSent(std::size_t bytes = 0);
  void LogFrames();
  // RDP has no acknowledgement deadline; one second bounds a viewer-tolerable frozen picture.
  static constexpr auto     AcknowledgementTimeout = std::chrono::seconds(1);
  static constexpr unsigned FrameWindow            = 2;
  bool Pacing();
  void GraphicsDeadline();
  DWORD Timeout();
  static constexpr DWORD AppendedHandleCount = 5 + Input::MaxHandles;
  bool Channels();
  bool OpenStaticChannels();
  bool OpenDisplayControl();
  bool GraphicsChannel();
  bool Graphics() const { return gfx && gfx->confirmed; }
  std::unique_ptr<GfxChannel> gfx;
  bool gfx_attempted = false;
  UINT32 gfx_id = UINT32_MAX;
  static constexpr auto GraphicsConnectionWait = std::chrono::seconds(3);
  Clock::time_point activated_at{};
  std::chrono::nanoseconds graphics_ready_time{};
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU graphics_qoe{};
  std::optional<sdlrdp_event> connection;
  void AnnounceConnection(sdlrdp_codec codec);
  static BOOL ChannelCreated(void*, UINT32, INT32);
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
  uint64_t sequence = 0, acknowledged = 0;
  UINT32 frame_id = 0;
  struct Pending { UINT32 id; uint64_t sequence; Clock::time_point sent{}; };
  std::deque<Pending> pending;

  Clock::time_point last_ack{};
  double ack_interval = 0;
  uint64_t avc_frames = 0;
  std::chrono::nanoseconds avc_convert{}, avc_upload{}, avc_encode{};
  uint64_t acks_timed_out = 0;
  uint64_t frames_sent = 0, frames_coalesced = 0, dirty_presents = 0, ack_count = 0, ack_over_100ms = 0;
  std::chrono::nanoseconds encoded_at_start{}, encode_total{}, encode_max{}, ack_total{}, ack_max{};
  unsigned refresh = 0, screen_width = 0, screen_height = 0;
  bool ack_enabled = false, ack_seen = false, suppressed = false;
  HANDLE channels = nullptr;
  std::unique_ptr<DispServerContext, Releases<disp_server_context_free>> disp;
  std::unique_ptr<ClipboardChannel> clipboard;
  std::shared_ptr<DriveChannel> drive;
  UINT32 display_id = UINT32_MAX;
  bool disp_open = false, resizing = false;
  sdlrdp_rect desktop{};
  bool activated = false;
  std::atomic_bool active = false, finished = false;
  std::jthread thread;
private:
  enum class EncodeState { Idle, Legacy, Graphics, LegacyReady };
  void TransitionEncode(EncodeState next);
  bool PrepareFrame();
  EncodeState encode_state { EncodeState::Idle };
  LegacyFrame legacy       {};
};
}
struct sdlrdp_handle { std::unique_ptr<Backend::State> state; };
