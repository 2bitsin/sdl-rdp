#pragma once
#include "refresh.hpp"
#include "audio.hpp"
#include "auth.hpp"
#include "clipboard.hpp"
#include "contract.hpp"
#include "drive.hpp"
#include "encoder.hpp"
#include "errors.hpp"
#include "gfx.hpp"
#include "input.hpp"
#include "legacy-frame.hpp"
#include "logging.hpp"
#include "rdp-handles.hpp"
#include "rect.hpp"
#include "sdl-rdp-backend.h"
#include "trace.hpp"
#include "wake-event.hpp"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <format>
#include <freerdp/freerdp.h>
#include <freerdp/server/disp.h>
#include <freerdp/server/rdpsnd.h>
#include <freerdp/update.h>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>
#include <winpr/synch.h>

namespace Backend {
using utilities::Ensures;
using utilities::Expects;
struct Credentials {
  std::filesystem::path certificate;
  std::filesystem::path key;
};
std::filesystem::path DefaultCertificateDirectory();
Credentials           EnsureCertificate(std::filesystem::path const& directory);
struct Pointer {
  unsigned          width  = 0;
  unsigned          height = 0;
  unsigned          hot_x  = 0;
  unsigned          hot_y  = 0;
  std::vector<BYTE> pixels;
  std::vector<BYTE> mask;
};
class Peer;
struct State {
public:
                                     State(State const&)       = delete;
                                     State(State&&)            = delete;
  explicit                           State(sdlrdp_config const& config, bool tracing = false);
                                     ~State();
  State&                             operator = (State const&) = delete;
  State&                             operator = (State&&)      = delete;
  void                               Log(sdlrdp_log_level level, std::string const& text) const;
  void                               Listen(std::stop_token const& quit);
  void                               Push(sdlrdp_event event);
  unsigned                           Poll(sdlrdp_event* out, unsigned max);
  int                                Wait(int timeout);
  void                               Wakeup();
  void                               Takeover(Peer& peer, sdlrdp_event event);
  void                               Depart(Peer& peer);
  void                               ReplacePeer(Peer& old);
  std::shared_ptr<std::vector<BYTE>> AcquireBuffer();
  void Present(void const* pixels, int pitch, unsigned w, unsigned h, std::span<sdlrdp_rect const> damage);
  void Publish(std::shared_ptr<std::vector<BYTE>> next, unsigned w, unsigned h, std::span<sdlrdp_rect const> damage);
  void                               SetPointer(unsigned w, unsigned h, unsigned x, unsigned y, void const* pixels);
  void                               Resize(unsigned w, unsigned h);
  bool                               ChangePicture(unsigned w, unsigned h);
  void                               SetAspect(sdlrdp_aspect value);
  void                               SetRefresh(RefreshMode mode, unsigned ceiling);
  int                                WaitFrame(int timeout);
  void                               OpenAudio();
  unsigned                           AudioRate();
  void                               EnsurePicture();
  int                                WriteAudio(void const* frames, unsigned count);
  int                                WaitAudio(int timeout);
  void                               CloseAudio();
  sdlrdp_rect                        Picture(unsigned w = 0, unsigned h = 0) const;
  Refresh                                         refresh;
  Trace                                           trace;
  LogRoute                                        log_route;
  Authentication                                  authentication;
  void                                            (*log)            (void*, sdlrdp_log_level, char const*);
  void*                                           user;
  std::atomic_uint                                next_drive         { 1                                          };
  Clipboard                                       clipboard;
  Pointer                                         pointer;
  uint64_t                                        pointer_generation = 0;
  unsigned                                        avc_bitrate_kbps   = 0;
  unsigned                                        audio_latency      = 100;
  bool                                            audio_open         = false;
  std::condition_variable_any                     audio_changed;
  std::atomic<sdlrdp_codec>                       codec;
  unsigned                                        width;
  unsigned                                        height;
  unsigned                                        port               = 0;
  sdlrdp_aspect                                   aspect             {                                            };
  uint64_t                                        presented          = 0;
  Peer*                                           current            = nullptr;
  std::condition_variable                         frame_changed;
  Credentials                                     credentials;
  ListenerHandle                                  listener;
  EventHandle                                     stop;
  EventHandle                                     reap               { CreateEvent(nullptr, TRUE, FALSE, nullptr) };
  // frame_guard protects the shadow and every peer's dirty region.
  std::recursive_mutex                            session_guard;
  std::mutex                                      peers_guard;
  std::mutex                                      events_guard;
  std::mutex                                      frame_guard;
  std::mutex                                      producer_guard;
  std::shared_ptr<std::vector<BYTE>>              shadow;
  std::vector<std::shared_ptr<std::vector<BYTE>>> buffers;
  unsigned                                        frame_width        = 0;
  unsigned                                        frame_height       = 0;
  std::condition_variable                         changed;
  std::deque<sdlrdp_event>                        events;
  unsigned long                                   wake_generation    = 0;
  std::vector<std::unique_ptr<Peer>>              peers;
  std::jthread                                    thread;

private:
  static BOOL Accepted(freerdp_listener* listener, freerdp_peer* client);
};
class Peer // NOLINT(clang-analyzer-optin.performance.Padding): Member teardown order.
{
public:
Peer(Peer const&) = delete;
Peer(Peer&&)      = delete;
  Peer& operator = (Peer const&) = delete;
  Peer& operator = (Peer&&)      = delete;
  using Clock = std::chrono::steady_clock;
  struct Column {
    unsigned first;
    unsigned second;
    float    weight;
  };
  struct Pending {
    UINT32            id       = 0;
    uint64_t          sequence = 0;
    Clock::time_point sent;
  };
Peer(PeerHandle accepted, State& state);
~Peer();
  void                    Start();
  void                    Post(sdlrdp_rect area);
  void                    InstallCallbacks() const;
  std::pair<DWORD, DWORD> PollParameters(std::span<HANDLE> handles);
  BOOL                    TakeControl();
  bool                    SendEncoded(bool encoded, std::stop_token const& quit);
  bool                    Configure();
  void                    AuthenticationEnded();
  void                    Serve(std::stop_token const& quit);
  DWORD                   EventHandles(std::span<HANDLE> handles);
  bool                    PollStep(std::stop_token const& quit, std::span<HANDLE> handles);
  bool                    Drain();
  bool                    ReadyFrame();
  bool                    TransportStep(std::stop_token const& quit, std::span<HANDLE const> ready);
  bool                    EncodeAndSend(std::stop_token const& quit);
  bool                    SendPointer();
  void                    TransportEnded();
  static Peer&            Held(freerdp_peer* client);
  void                    RecordAcknowledgement(Clock::duration elapsed);
  void                    AcceptAcknowledgement(UINT32 /*id*/);
  sdlrdp_rect             CaptureFrame();
  bool                    ResizeDesktop(sdlrdp_rect picture);
  bool                    BeginFrame();
  bool                    Marker(UINT16 action);
  void                    FrameSent(std::size_t bytes);
  void                    LogFrames();
  // Refresh updates require owner.frame_guard; the encoder reads effective_refresh atomically.
  void                    RestartRefresh();
  void                    PublishRefresh(unsigned previous);
  void                    MeasureWire(std::size_t bytes);
  bool                    Pacing();
  void                    GraphicsDeadline();
  DWORD                   Timeout();
  bool                    Channels(std::span<HANDLE const> ready);
  bool                    OpenStaticChannels(std::span<HANDLE const> ready);
  bool                    OpenDisplayControl();
  BOOL                    ActivateChannel(UINT32 id);
  bool                    GraphicsChannel(std::span<HANDLE const> ready);
  bool                    Graphics() const { return gfx && gfx->Confirmed(); }
  void                    AnnounceConnection(sdlrdp_codec codec);
  void                    EndAudio();
  bool                    SoundChannel(std::span<HANDLE const> ready);
  Refresh                                  refresh;
  std::atomic_uint                         effective_refresh       { 60 };
  uint64_t                                 outq_total              = 0;
  unsigned                                 outq_max                = 0;
  bool                                     wire_unavailable_logged = false;
  AuthenticationState                      authentication;
  Encoder                                  encoder;
  uint64_t                                 pointer_generation      = 0;
  // RDP has no acknowledgement deadline; one second bounds a viewer-tolerable frozen picture.
  static constexpr auto                    AcknowledgementTimeout  = std::chrono::seconds(1);
  static constexpr unsigned                FrameWindow             = 2;
  static constexpr DWORD                   AppendedHandleCount     = 5 + Input::MaxHandles;
  std::unique_ptr<GfxChannel>              gfx;
  bool                                     gfx_attempted           = false;
  UINT32                                   gfx_id                  = UINT32_MAX;
  static constexpr auto                    GraphicsConnectionWait  = std::chrono::seconds(3);
  Clock::time_point                        activated_at;
  std::chrono::nanoseconds                 graphics_ready_time     {    };
  RDPGFX_QOE_FRAME_ACKNOWLEDGE_PDU         graphics_qoe            {    };
  std::optional<sdlrdp_event>              connection;
  bool                                     sound_attempted         = false;
  std::unique_ptr<AudioChannel>            sound;
  PeerHandle                               client;
  int                                      socket_descriptor       = client->sockfd;
  State&                                   owner;
  WakeEvent                                wake;
  DWORD                                    handle_count            { 0  };
  std::vector<std::string>                 trace_pending;
  Region                                   dirty;
  Region                                   sending;
  std::shared_ptr<std::vector<BYTE> const> snapshot;
  unsigned                                 snapshot_width          = 0;
  unsigned                                 snapshot_height         = 0;
  std::vector<Column>                      scale_columns;
  int                                      scale_x                 = -1;
  int                                      scale_width             = 0;
  unsigned                                 scale_source            = 0;
  uint64_t                                 sequence                = 0;
  uint64_t                                 acknowledged            = 0;
  UINT32                                   frame_id                = 0;
  std::deque<Pending>                      pending;

  uint64_t                                                               avc_frames       = 0;
  std::chrono::nanoseconds                                               avc_convert      { };
  std::chrono::nanoseconds                                               avc_upload       { };
  std::chrono::nanoseconds                                               avc_encode       { };
  uint64_t                                                               acks_timed_out   = 0;
  uint64_t                                                               frames_sent      = 0;
  uint64_t                                                               frames_coalesced = 0;
  uint64_t                                                               dirty_presents   = 0;
  uint64_t                                                               ack_count        = 0;
  uint64_t                                                               ack_over_100ms   = 0;
  std::chrono::nanoseconds                                               encoded_at_start { };
  std::chrono::nanoseconds                                               encode_total     { };
  std::chrono::nanoseconds                                               encode_max       { };
  std::chrono::nanoseconds                                               ack_total        { };
  std::chrono::nanoseconds                                               ack_max          { };
  unsigned                                                               screen_width     = 0;
  unsigned                                                               screen_height    = 0;
  bool                                                                   ack_enabled      = false;
  bool                                                                   suppressed       = false;
  HANDLE                                                                 channels         = nullptr;
  std::unique_ptr<DispServerContext, Releases<disp_server_context_free>> disp;
  std::unique_ptr<ClipboardChannel>                                      clipboard;
  std::shared_ptr<DriveChannel>                                          drive;
  UINT32                                                                 display_id       = UINT32_MAX;
  bool                                                                   disp_open        = false;
  bool                                                                   resizing         = false;
  sdlrdp_rect                                                            desktop          { };
  bool                                                                   activated        = false;
  std::atomic_bool                                                       active           = false;
  std::atomic_bool                                                       finished         = false;
  std::jthread                                                           thread;

private:
  static BOOL Capabilities(freerdp_peer* client);
  static BOOL Acknowledge(rdpContext* /*context*/, UINT32 /*id*/);
  static BOOL Suppress(rdpContext* /*context*/, BYTE /*allow*/, RECTANGLE_16 const* /*unused*/);
  static BOOL ChannelCreated(void* /*user*/, UINT32 /*id*/, INT32 /*status*/);
  static UINT Layout(DispServerContext* /*context*/, DISPLAY_CONTROL_MONITOR_LAYOUT_PDU const* /*pdu*/);
  static BOOL Activate(freerdp_peer* client);
  static BOOL Keyboard(rdpInput* input, UINT16 flags, UINT8 code);
  static BOOL Mouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);
  static BOOL ExtendedMouse(rdpInput* input, UINT16 flags, UINT16 x, UINT16 y);
  void        RecordAcknowledgements(std::deque<Pending>::iterator const& last, Clock::time_point now);
  enum class EncodeState{ Idle, Legacy, Graphics, LegacyReady };
  void        TransitionEncode(EncodeState next);
  bool        PrepareFrame();
  EncodeState encode_state{ EncodeState::Idle };
  LegacyFrame legacy      {                   };
};
}
struct sdlrdp_handle {
  std::unique_ptr<Backend::State> state;
  Backend::ErrorStore             errors;
};
namespace Backend {
void SetError(sdlrdp_handle* handle, std::string text);
}
